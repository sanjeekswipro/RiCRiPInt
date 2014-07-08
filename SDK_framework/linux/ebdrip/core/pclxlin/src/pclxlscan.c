/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlscan.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Functions to read/scan PCLXL data stream to extract various
 * PCLXL constructs.  All the "scan" functions return either 0 meaning
 * success or a positive value meaning success and this many bytes
 * were consumed or a negative error result code which indicates the
 * reason for failure.
 */

#include <string.h>

#include "core.h"
#include "swctype.h"
#include "mm.h"
#include "lowmem.h" /* mm_memory_is_low */
#include "swcopyf.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "control.h"
#include "display.h"
#include "monitor.h"

#include "pclxlscan.h"
#include "pclxlerrors.h"
#include "pclxldebug.h"
#include "pclxlattributes.h"
#include "pclxloperators.h"
#include "pclxltags.h"
#include "pclxlstream.h"

/* pclxl_handle_external_actions() allows the main PCLXL stream
 * scanning loop to handle externally requested/queued "actions" to be
 * performed
 *
 * If the actions, if any, are all handled successfully then we return
 * TRUE which allows the caller to continue parsing the PCLXL stream
 *
 * If we fail to perform one or more of the actions then we return
 * FALSE which indicates to the caller that it should abort processing
 * this PCLXL stream (at least)
 */
static Bool
pclxl_handle_external_actions(PCLXL_CONTEXT pclxl_context)
{
  UNUSED_PARAM(PCLXL_CONTEXT, pclxl_context);

  /* Handle low memory, interrupts, timeouts, etc. */
  if ( mm_memory_is_low || dosomeaction ) {
    if ( !handleNormalAction() )
      return FALSE ;
  }
  return TRUE;
}

/*
 * The byte-code tags used for PCLXL data types have been carefully chosen
 * so that the individual bits each have specific meanings
 *
 * Therefore we have a collection of "accessor" macros below
 * that each take a data type tag and return some information
 * about this data type, including its native PCLXL data type
 * the storage type (i.e. structure field) within the PCLXL_ATTRIBUTE structure
 * and the size (in bytes) of the various data types
 */

/*
 * PCLXL_BASE_DATA_TYPE_SIZE() uses the base data type to access an array
 * of sizes (in bytes) of each of the data types.
 * Note that the above mask could theoretically return values
 * in the range 0 to 7, hence the array containing 8 values
 * even though we never expect to see "6" or "7"
 */

static uint8 pclxl_base_data_type_sizes[] = { 1, 2, 4, 2, 4, 4, 0, 0 };
#define PCLXL_BASE_DATA_TYPE_SIZE(DT) (pclxl_base_data_type_sizes[PCLXL_BASE_DATA_TYPE((DT))])

#ifdef DEBUG_BUILD
static char* pclxl_base_data_type_names[] = { "ubyte", "uint16", "uint32", "int16", "int32", "real32" };
#define PCLXL_BASE_DATA_TYPE_NAME(DT) (pclxl_base_data_type_names[PCLXL_BASE_DATA_TYPE((DT))])
#endif


/*
 * PCLXL_STRUCT_DATA_TYPE returns
 * 0 for a simple data type, 1 for an (x,y) pair and 2 for an ((x1,y1),(x2,y2)) coordinate pair
 *
 * Therefore this macro may be used directly as a boolean discriminant
 * of whether this is a simple value (and thus stored directly in the PCLXL_ATTRIBUTE structure
 * or a "structured" value and thus stored in dynamically allocated memory
 * which is referenced from the PCLXL_ATTRIBUTE structure
 */

#define PCLXL_STRUCT_DATA_TYPE(DT) (((DT) & 0x30) >> 4)

#define PCLXL_STRUCT_DATA_TYPE_SIMPLE   0
#define PCLXL_STRUCT_DATA_TYPE_XY       1
#define PCLXL_STRUCT_DATA_TYPE_BOX      2
#define PCLXL_STRUCT_DATA_TYPE_UNKNOWN  3

/*
 * Or PCLXL_STRUCT_DATA_TYPE_MULTIPLIER() can use PCLXL_STRUCT_DATA_TYPE()
 * to obtain an index into an array of structure size multipliers
 * to return the number of instances of the simple data type
 * that are required by the structure
 */

static uint8 pclxl_struct_data_type_multipliers[] = { 1, 2, 4, 0 };
#define PCLXL_STRUCT_DATA_TYPE_MULTIPLIER(DT) (pclxl_struct_data_type_multipliers[PCLXL_STRUCT_DATA_TYPE((DT))])

#ifdef DEBUG_BUILD
static char* pclxl_struct_data_type_names[] = { "", "XY", "Box", "" };
#define PCLXL_STRUCT_DATA_TYPE_NAME(DT) (pclxl_struct_data_type_names[PCLXL_STRUCT_DATA_TYPE((DT))])
#endif

/**
 * \brief pclxl_scan_data_value() is passed a data type (PCLXL tag)
 * and the address of an essentially empty PCLXL_ATTRIBUTE.
 *
 * It examines the data type tag to determine how to read the
 * data value specified by the data type.
 *
 * For single valued data types (i.e. ubyte, uint16, uint32, int16, int32 and real32)
 * the value is read directly into a field in the PCLXL_ATTRIBUTE structure.
 *
 * For structured values ((
 *
 * For single valued data types (i.e. ubyte, uint16, uint32, int16, int32 and real32)
 * the value is read directly into a field in the PCLXL_ATTRIBUTE structure.
 *
 * For structured values e.g. (x,y) coordinates and ((x1,y1),(x2,y2)) coordinate pairs
 * (a.k.a. "box") and for arrays of either simple values or structured values
 * we allocate sufficient space in the heap and reference this from
 * the PCLXL_ATTRIBUTE
 */
static
Bool pclxl_scan_data_value(
  PCLXL_CONTEXT   pclxl_context,
  uint32          data_type,
  PCLXLSTREAM*    p_stream,
  PCLXL_ATTRIBUTE attribute)
{
  uint32 count;
  uint32 data_value_byte_size;
  uint8* allocated_bytes;

  HQASSERT((attribute != NULL), "Cannot read a data type value into a NULL PCLXL_ATTRIBUTE");
  HQASSERT((p_stream != NULL), "p_stream is NULL");

#ifdef DEBUG_BUILD
  /* Catch use of uninitialised value */
  HqMemSet8((uint8 *)&attribute->value, 0xff, sizeof(attribute->value));
#endif /* DEBUG_BUILD */

  if ( (PCLXL_STRUCT_DATA_TYPE(data_type) == 0) && !PCLXL_IS_ARRAY_TYPE(data_type) ) {
    /* Simple type - single value */
    attribute->array_length = 0;
    attribute->array_element_size = 0;

    PCLXL_DEBUG(PCLXL_DEBUG_DATA_TYPES,
                ("Data Type (0x%02x) = \"%s\" = %d byte%s", data_type,
                 PCLXL_BASE_DATA_TYPE_NAME(data_type), PCLXL_BASE_DATA_TYPE_SIZE(data_type),
                 (PCLXL_BASE_DATA_TYPE_SIZE(data_type) > 1 ? "s" : "")));

    if ( !pclxl_stream_read_data(p_stream, data_type, &attribute->value.v_ubyte, 1) ) {
      return(FALSE);
    }

    return(TRUE);

  } else { /* Structured type or an array */

    attribute->array_element_size = PCLXL_BASE_DATA_TYPE_SIZE(data_type)*PCLXL_STRUCT_DATA_TYPE_MULTIPLIER(data_type);

    if ( XL_TAG_TO_STRUCT(data_type) != XL_TAG_STRUCT_ARRAY ) {
      /* Structured type - use array in attribute */
      /** \todo Sensibly array length should reflect the number of elements in the
       * structured type but the rest of the code currently relies on 1! */
      attribute->array_length = 1;
      data_value_byte_size = attribute->array_element_size;
      allocated_bytes = attribute->value_array.v_ubytes;

      PCLXL_DEBUG(PCLXL_DEBUG_DATA_TYPES,
                  ("Data Type (0x%02x) = \"%s(%s)\" = %d bytes", data_type,
                   PCLXL_STRUCT_DATA_TYPE_NAME(data_type), PCLXL_BASE_DATA_TYPE_NAME(data_type),
                   attribute->array_element_size));

      /* Number of values depends on type of structure */
      count = PCLXL_STRUCT_DATA_TYPE_MULTIPLIER(data_type);

    } else { /* General array - allocate block of memory for array data */
      if ( !pclxl_stream_read_array_length(pclxl_context, p_stream, &attribute->array_length) ) {
        return(FALSE);
      }
      if ( attribute->array_length == 0 ) {
        PCLXL_DEBUG((PCLXL_DEBUG_DATA_TYPES | PCLXL_DEBUG_ATTRIBUTES),
                    ("PCLXL data stream contains a zero-lengthed array for data type 0x%02x", data_type));
        return(TRUE);
      }

      data_value_byte_size = attribute->array_length*attribute->array_element_size;

      if ( data_value_byte_size <= PCLXL_ATTRIBUTE_BUFFER_SIZE ) {
        /* Small sized array - use structured data type storage in attribute */
        allocated_bytes = attribute->value_array.buffer;

      } else {
        /* Allocate additional byte so can NUL terminate and treat as a C string */
        if ( (allocated_bytes = mm_alloc(pclxl_context->memory_pool, (data_value_byte_size + 1),
                                         MM_ALLOC_CLASS_PCLXL_ATTRIBUTE)) == NULL ) {
          PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_INSUFFICIENT_MEMORY,
                              ("Failed to allocate new Attribute (Data Type 0x%02x) structure", data_type));
          return(FALSE);
        }
      }
      allocated_bytes[data_value_byte_size] = 0;

      count = attribute->array_length;
    }

    switch ( XL_TAG_TO_TYPE(data_type) ) {
    case XL_TAG_TYPE_UBYTE:
      attribute->value.v_ubytes = (uint8*)allocated_bytes;
      break;

    case XL_TAG_TYPE_UINT16:
      attribute->value.v_uint16s = (uint16*)allocated_bytes;
      break;

    case XL_TAG_TYPE_UINT32:
      attribute->value.v_uint32s = (uint32*)allocated_bytes;
      break;

    case XL_TAG_TYPE_SINT16:
      attribute->value.v_int16s = (int16*)allocated_bytes;
      break;

    case XL_TAG_TYPE_SINT32:
      attribute->value.v_int32s = (int32*)allocated_bytes;
      break;

    case XL_TAG_TYPE_REAL32:
      attribute->value.v_real32s = (float*)allocated_bytes;
      break;
    }

    /* Read in value data */
    if ( !pclxl_stream_read_data(p_stream, data_type, allocated_bytes, count) ) {
      return(FALSE);
    }
  }

  return(TRUE);
}

static
Bool pclxl_scan_next(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  PCLXL_STREAM_OBJECT_STRUCT* stream_object)
{
  uint32 tag;
  Bool  seen_value = FALSE;

  HQASSERT((p_stream != NULL), "p_stream is NULL");
  HQASSERT((stream_object != NULL), "stream_object is NULL");

  for (;;) {
    if ( !pclxl_stream_next_tag(pclxl_context, p_stream, &tag) ) {
      return(FALSE);
    }

    if ( is_xldatatype(tag) ) {
      /* Read value for datatype */
      if ( !pclxl_scan_data_value(pclxl_context, tag, p_stream, stream_object->attribute) ) {
        return(FALSE);
      }
      stream_object->attribute->data_type = tag;
      seen_value = TRUE;

    } else if ( is_xlattribute(tag) ) {
      /* Read attribute id */
      if ( !pclxl_stream_read_attribute(p_stream, tag, &stream_object->attribute->attribute_id) ) {
        return(FALSE);
      }
      /* Check attribute is valid for current stream protocol */
      if ( !pclxl_stream_min_protocol(p_stream, pclxl_attr_protocol(stream_object->attribute)) ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE,
                            ("Attribute \"%s\" is only supported in %02x PCLXL protocol class/revision which is greater than this PCLXL data stream protocol class/revision %02x",
                             pclxl_get_attribute_name(stream_object->attribute),
                             pclxl_attr_protocol(stream_object->attribute),
                             pclxl_stream_protocol(p_stream)));
        return(FALSE);
      }
      /* Only return attribute if there is an associated value */
      if ( seen_value ) {
        stream_object->tag = tag;
        return(TRUE);
      }

    } else {
      /* Return operator tag as is */
      HQASSERT((is_xloperator(tag)),
               "pclxl_scan_next: unexpected tag from stream");
      stream_object->tag = tag;
      return(TRUE);
    }
  }

  /* NEVERREACHED */

} /* pclxl_scan_next */

/**
 * \brief Scans a PCLXL data stream, gathering attributes and looking for
 * operators. As operators are found they can perform drawing operations and/or
 * start a new parser context and/or push or pop a graphics state. After each
 * operator is handled the attribute list is always cleared
 */

int32
pclxl_scan(PCLXL_PARSER_CONTEXT parser_context)
{
  /* The basic algorithm is to read a single byte which is interpreted as a
   * PCLXL tag.
   *
   * This tag passed to pclxl_handle_tag() which looks up the tag details,
   * checks that this tag is valid within the current parser context (including
   * whether it is valid in the declared PCLXL protocol class/revision)
   *
   * If it is valid, then its associated handling function is called and then
   * depending upon whether this operation was successfully handled,
   * pclxl_handle_tag() may clear the attribute list within the current parser
   * context and will return the number of additional bytes (if any) that were
   * consumed as part of "handling" this tag
   */

  Bool read_stream_object_result;
  int32 status = PCLXL_SUCCESS ;
  PCLXL_STREAM_OBJECT_STRUCT stream_object = {0};
  PCLXLSTREAM *p_stream ;
  int saved_dl_safe_recursion = dl_safe_recursion ;
  int saved_gc_safety_level = gc_safety_level;

  /* Clear out attribute set in case we have gone recursive with an image etc. */
  pclxl_attr_set_empty(parser_context->attr_set);

  parser_context->last_command_was_pass_through = parser_context->doing_pass_through ;

  do {
    dl_safe_recursion = saved_dl_safe_recursion ;
    gc_safety_level = saved_gc_safety_level;

    if ( !pclxl_handle_external_actions(parser_context->pclxl_context) ) {
      /* To allow the parsing loop to be interrupted for whatever reason,
       * including to (fail to) handle low memory conditions we call a function
       * to check for and perform any externally requested/queued actions.
       *
       * Unfortunately we have failed to handle one of these actions
       */
      /* We are expecting pclxl_handle_external_action() to have logged any
       * error(s) that it detected
       */
      /**
       * \todo
       * So the only questions are:
       *
       * 1) Do we (attempt to) produce
       *    any sort of PCLXL error report (page or back-channel)?
       *
       *    At the moment I think *not*
       *
       * 2) Do we report this to the caller?
       *
       *    At the moment I thing we should report it
       */

      /*
       * In low-memory we seem to be losing some PCL errors, they are not being
       * reported and the rip is completing as if the job was successful.
       * So add a print to ensure we do not miss these cases for now.
       */
      monitorf((uint8 *)"Error handling internal PCL-ERROR\n");

      return PCLXL_FAIL;
    }

#ifdef DEBUG_BUILD
    /* Catch use of uninitialised data */
    HqMemSet8((uint8 *)&stream_object, 0xff, sizeof(stream_object));
#endif /* DEBUG_BUILD */

    /* Pick up the current stream to interpret */
    p_stream = pclxl_parser_current_stream(parser_context);
    HQASSERT((p_stream != NULL), "p_stream is NULL");

    if (parser_context->last_command_was_pass_through && ! parser_context->doing_pass_through) {
      parser_context->last_command_was_pass_through = FALSE ;
      /* Restore the cached operator. */
      stream_object = parser_context->cached_stream_object ;
      read_stream_object_result = parser_context->cached_read_stream_object_result ;

    } else {
      /* Pick up next attribute to fill in */
      stream_object.attribute = pclxl_attr_set_get_new(parser_context->attr_set);
      if ( stream_object.attribute == NULL ) {
        return(PCLXL_INSUFFICIENT_MEMORY);
      }

      /* Read the next object from current stream. */
      read_stream_object_result = pclxl_scan_next(parser_context->pclxl_context, p_stream, &stream_object);
    }

    if ( !read_stream_object_result ) {
      if ( pclxl_stream_eof(p_stream) ) {
        /* Finished this stream, pop from stack and continue with next */
        pclxl_parser_pop_stream(parser_context);
        p_stream = pclxl_parser_current_stream(parser_context);
        if ( p_stream != NULL ) {
          continue;
        }
        /* No more streams, so must be end of the job */
        PCLXL_DEBUG(PCLXL_DEBUG_BYTES_READ, ("Reached EOF"));
        return PCLXL_SUCCESS;
      }
      /* We have failed to read the next byte code tag from the PCLXL stream We
       * obviously cannot process it
       */
      /* We got some other negative, non-EOF result?!
       * We will assume that it has already been logged (i.e. captured in a
       * PCLXL_ERROR_INFO struct) But we now need to:
       *
       * 1) Abandon the current output page (if any)
       * 2) Produce any error/warning page
       * 3) Produce any "back channel" output
       * 4) return read_stream_object_result as the non-zero result code
       */
      return(PCLXL_FAIL);
    }

    if ( is_xlattribute(stream_object.tag) ) {
      /* Add attribute to current set */
      pclxl_attr_set_add(parser_context->attr_set, stream_object.attribute);

    } else {
      HQASSERT((is_xloperator(stream_object.tag)),
               "Didn't get operator as expected");
      if ( parser_context->last_command_was_pass_through ) {
        parser_context->exit_parser = TRUE ;

        if (stream_object.tag == PCLXL_OP_Passthrough) {
          parser_context->doing_pass_through = TRUE ;

        } else {
          /* Cache the command. */
          parser_context->cached_stream_object = stream_object;
          parser_context->cached_read_stream_object_result = read_stream_object_result;
          parser_context->doing_pass_through = FALSE;

          /* Do not dispatch this command as we want that to happen
             after the first PassThrough command has returned. */
          continue;
        }
      }

      if ( !pclxl_handle_operator(stream_object.tag, parser_context) ) {
        /* Reading the stream worked ok, but we failed to correctly handle the
         * most recently read stream object. We will assume that a suitable
         * error has been logged So we simply need to report this error
         */
        status = PCLXL_FAIL;
      }
    }

  } while (! parser_context->exit_parser && status == PCLXL_SUCCESS);

  if (parser_context->last_command_was_pass_through) {
    parser_context->exit_parser = FALSE ;
  }
  return status ;
}


/******************************************************************************
* Log stripped */
