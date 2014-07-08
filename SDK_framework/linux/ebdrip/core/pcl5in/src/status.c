/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:status.c(EBDSDK_P.1) $
 * $Id: src:status.c,v 1.19.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Status Readback" category.
 *
 * Status Readback:
 *
 * Set Location Type   ESC * s # T
 * Set Location Unit   ESC * s # U
 * Inquire Entity      ESC * s # I
 * Free Space          ESC * s # M
 * Flush All Pages     ESC & r # F
 * Echo                ESC * s # X
 */

#include "core.h"
#include "status.h"

#include "pcl5context_private.h"
#include "pcl5scan.h"
#include "pagecontrol.h"
#include "printenvironment_private.h"
#include "pclutils.h"
#include "resourcecache.h"

#include "swcopyf.h"
#include "fileio.h"
#include "monitor.h"
#include "mm.h"

/* Status inquiry types. */
#define FONT_INQUIRY 0
#define MACRO_INQUIRY 1
#define USER_PATTERN_INQUIRY 2
#define SYMBOL_SET_INQUIRY 3
#define FONT_EXTENDED_INQUIRY 4

/* Location types. */
#define LOCATION_TYPE_CURRENT 1
#define LOCATION_TYPE_ALL 2
#define LOCATION_TYPE_INTERNAL 3
#define LOCATION_TYPE_DOWNLOADED 4
#define LOCATION_TYPE_CART 5
#define LOCATION_TYPE_ROM 7

/* Error constants. */
static uint8* error_invalid_entity = (uint8*)"INVALID ENTITY";
static uint8* error_invalid_location = (uint8*)"INVALID LOCATION";
static uint8* error_internal = (uint8*)"INTERNAL ERROR";

/* Info constants. */
static uint8* info_entity = (uint8*)"ENTITY";
static uint8* info_patterns = (uint8*)"PATTERNS";

#if 0
/* Not used yet. */
static uint8* info_fonts = (uint8*)"FONTS";
static uint8* info_fonts_extended = (uint8*)"FONTS EXTENDED";
static uint8* info_macros = (uint8*)"MACROS";
static uint8* info_symbol_sets = (uint8*)"SYMBOL SETS";
#endif

/* The initial size of the buffer used in status message construction. */
#define INITIAL_BUFFER_SIZE 1024

/**
 * Structure used to construct status messages.
 */
typedef struct {
  GROWABLE_ARRAY array;
  uint32 message_length;
} StatusMessage;

/**
 * Append to the passed message.
 */
static Bool sm_append(StatusMessage* self, char* format, ...)
{
  int32 n;
  va_list ap;
  size_t remaining = garr_length(&self->array) - self->message_length;
  Bool finished = FALSE;
  uint8* insert_point;

  if (self->message_length > 0) {
    /* message_length includes the null terminator; we'll overwrite that. */
    self->message_length --;
  }

  /* Loop as we may need to expand the message buffer during writing. */
  while (! finished) {
    insert_point = garr_data(&self->array) + self->message_length;

    va_start(ap, format);
    n = vswncopyf(insert_point, CAST_SIZET_TO_INT32(remaining), (uint8*)format, ap);
    /* The returned size does not include the null terminator. */
    n ++;
    va_end(ap);

    if (CAST_SIGNED_TO_UINT32(n) >= remaining) {
      /* Buffer was too short - expand and try again. */
      if (! garr_extend(&self->array, garr_length(&self->array) * 2))
        return FALSE;

      remaining = garr_length(&self->array) - self->message_length;
    }
    else {
      self->message_length += n;
      finished = TRUE;
    }
  }

  return TRUE;
}

/**
 * Initialise the passed status message.
 */
static Bool sm_init(StatusMessage* self)
{
  garr_init(&self->array, mm_pcl_pool);
  self->message_length = 0;

  if (! garr_extend(&self->array, INITIAL_BUFFER_SIZE) ||
      ! sm_append(self, "PCL\r\n"))
    return FALSE;

  return TRUE;
}

/**
 * Write the passed message to the backchannel. The message will be deallocated
 * and should not be used again without calling sm_init().
 */
static Bool sm_write_to_backchannel(PCL5Context *pcl5_ctxt,
                                    OBJECT* bc_name, StatusMessage* self)
{
  OBJECT bc_file = OBJECT_NOTVM_NOTHING ;
  uint32 length;

  HQASSERT(oType(*bc_name) == OSTRING, "expected string object for backchannel");

  /* Append final line feed. */
  if (! sm_append(self, "\x0c"))
    return FALSE;

  /* Try to open the back channel for writing. */
  if ( !file_open(bc_name, SW_WRONLY, WRITE_FLAG, FALSE, 0, &bc_file) )
    return pcl_error_return(pcl5_ctxt->corecontext->error);

  HQASSERT(self->message_length > 0, "message should never be empty.");
  /* Don't write the terminating null. */
  length = self->message_length - 1;

  if (! file_write(oFile(bc_file), garr_data(&self->array), length) ||
      ! file_close(&bc_file)) {
    return pcl_error_return(pcl5_ctxt->corecontext->error);
  }

  garr_empty(&self->array);
  self->message_length = 0;

  return TRUE;
}

/* See header for doc. */
void default_status_readback_info(StatusReadBackInfo* self)
{
  self->location_type = 0;
  self->location_unit = 0;
}

StatusReadBackInfo* pcl5_get_status_readback(
  PCL5Context*  pcl5_ctxt)
{
  PCL5PrintEnvironment *mpe;

  mpe = get_current_mpe(pcl5_ctxt);
  return(&(mpe->status_readback));
}


/**
 * Write an error message to the back channel.
 */
static Bool status_readback_error(PCL5Context* pcl5_ctxt, uint8* info, uint8* error)
{
  OBJECT* backchannel = &(pcl5_ctxt->print_state->back_channel);
  StatusMessage message;

  sm_init(&message);
  sm_append(&message, "INFO %s\r\nERROR=%s\r\n", info, error);
  return sm_write_to_backchannel(pcl5_ctxt, backchannel, &message);
}

/**
 * User pattern status inquiry.
 */
static Bool user_pattern_inquiry(PCL5Context* pcl5_ctxt)
{
  StatusReadBackInfo* self = pcl5_get_status_readback(pcl5_ctxt);
  PCL5IdCache* user_patterns = pcl5_ctxt->resource_caches.user;
  OBJECT* backchannel = &(pcl5_ctxt->print_state->back_channel);
  Bool show_temps = FALSE;
  Bool show_perms = FALSE;
  Bool first_id = TRUE;
  PCL5IdCacheIterator iterator;
  StatusMessage message;
  pcl5_resource* resource;
  pcl5_pattern* pattern;

  if (! IN_RANGE(self->location_type, 1, 4)) {
    return status_readback_error(pcl5_ctxt, info_patterns, error_invalid_location);
  }

  /* Write header. */
  if (! sm_init(&message) ||
      ! sm_append(&message, "INFO %s\r\n", info_patterns))
    return FALSE;

  /* Which patterns are we interested in? */
  switch (self->location_type) {
    default:
      HQFAIL("location_type corrupt.");
    case LOCATION_TYPE_ALL:
    case LOCATION_TYPE_INTERNAL:
      show_temps = TRUE;
      show_perms = TRUE;
      break;

    case LOCATION_TYPE_DOWNLOADED:
      show_temps = self->location_unit == 0 || self->location_unit == 1;
      show_perms = self->location_unit == 0 || self->location_unit == 2;
      break;

    case LOCATION_TYPE_CURRENT: {
      PrintModelInfo* print_model = get_print_model(pcl5_ctxt);
      if (print_model->current_pattern_type != PCL5_USER_PATTERN) {
        if (! sm_append(&message, "ERROR=NONE\r\n"))
          return FALSE;
      }
      else {
        pattern = pcl5_id_cache_get_pattern(user_patterns,
                                            print_model->current_pattern_id);
        if (! sm_append(&message, "IDLIST=\"%d\"\r\nLOCTYPE=4\r\nLOCUNIT=%d\r\n",
                        (int32)print_model->current_pattern_id,
                        (pattern->detail.permanent ? 2 : 1)))
          return FALSE;
      }
      return sm_write_to_backchannel(pcl5_ctxt, backchannel, &message);
    }
  }

  /* We've not just been asked for the current pattern, so iterate those
   * required. We'll always write the id list - even though it may be empty. */
  if (! sm_append(&message, "IDLIST=\""))
    return FALSE;

  /* Iterate over the user pattern cache. */
  pcl5_id_cache_start_interation(user_patterns, &iterator);
  resource = pcl5_id_cache_iterate(&iterator);
  while (resource != NULL) {
    if ((resource->permanent && show_perms) ||
        (! resource->permanent && show_temps)) {
      if (first_id) {
        first_id = FALSE;
      }
      else {
        if (! sm_append(&message, ","))
          return FALSE;
      }
      if (! sm_append(&message, "%d", (int32)resource->numeric_id))
        return FALSE;
    }
    resource = pcl5_id_cache_iterate(&iterator);
  }
  if (! sm_append(&message, "\"\r\n"))
    return FALSE;

  return sm_write_to_backchannel(pcl5_ctxt, backchannel, &message);
}

/* Location type */
Bool pcl5op_star_s_T(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  StatusReadBackInfo* self = pcl5_get_status_readback(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign) ;

  self->location_type = max(0, value.integer);
  return TRUE;
}

/* Location unit */
Bool pcl5op_star_s_U(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  StatusReadBackInfo* self = pcl5_get_status_readback(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign) ;

  self->location_unit = max(0, value.integer);
  return TRUE;
}

/* Inquire entity */
Bool pcl5op_star_s_I(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  StatusReadBackInfo* self = pcl5_get_status_readback(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign);

  if (! IN_RANGE(value.integer, 0, 4)) {
    return status_readback_error(pcl5_ctxt, info_entity, error_invalid_entity);
  }

  if (self->location_type == 0) {
    return status_readback_error(pcl5_ctxt, info_entity, error_invalid_location);
  }

  switch (value.integer) {
    default:
      /* Unsupported inquiry. */
      return status_readback_error(pcl5_ctxt, info_entity, error_internal);

    case USER_PATTERN_INQUIRY:
      return user_pattern_inquiry(pcl5_ctxt);
  }
}

/* Free space */
Bool pcl5op_star_s_M(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  StatusMessage message;
  OBJECT* backchannel = &(pcl5_ctxt->print_state->back_channel);

  sm_init(&message);

  UNUSED_PARAM(int32, explicit_sign);
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt);

  if ( value.integer == 1 ) {
    if (! sm_append(&message,
                    "INFO MEMORY\r\nTOTAL=%lu\r\nLARGEST=%lu\r\n",
                    mm_total_size(), mm_no_pool_size(FALSE))) {
      return FALSE;
    }
  }
  else {
    if (! sm_append(&message, "INFO MEMORY\r\nERROR=INVALID UNIT\r\n"))
      return FALSE;
  }

  return sm_write_to_backchannel(pcl5_ctxt, backchannel, &message);
}

/* Flush pages */
Bool pcl5op_ampersand_r_F(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  UNUSED_PARAM(int32, explicit_sign);

  if ( (value.integer < 0) || (value.integer > 1) ) {
    return(TRUE);
  }

  /** \todo - call hooks to force flushing of any cached pgbs */

  /* Throw the current page only if forced to */
  if ( value.integer == 1 ) {
    return(throw_page(pcl5_ctxt, TRUE, TRUE));
  }

  return TRUE;
}

/* Echo */
Bool pcl5op_star_s_X(
  PCL5Context*  pcl5_ctxt,
  int32         explicit_sign,
  PCL5Numeric   value)
{
  StatusMessage message;
  OBJECT* backchannel = &(pcl5_ctxt->print_state->back_channel);

  UNUSED_PARAM(int32, explicit_sign);
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt);

  sm_init(&message);
  if (! sm_append(&message, "ECHO %d\r\n", value.integer))
    return FALSE;

  return sm_write_to_backchannel(pcl5_ctxt, backchannel, &message);
}

/* ============================================================================
* Log stripped */
