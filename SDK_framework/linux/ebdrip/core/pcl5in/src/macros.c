/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:macros.c(EBDSDK_P.1) $
 * $Id: src:macros.c,v 1.50.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Macros" category.
 *
 * Macro ID # (specify)     ESC & f # Y
 * Macro Control            ESC & f # X
 */

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "macros.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"
#include "pcl5state.h"
#include "resourcecache.h"
#include "namedef_.h"

#include "fileio.h"
#include "mmcompat.h"
#include "monitor.h"
#include "display.h"
#include "pcl5fonts.h"

#define MACRO_DATA_BLOCK_SIZE 1024

static Bool open_macro_stream_for_read(PCL5Context *pcl5_ctxt, MacroInfo *macro_info, FILELIST** readstream, Bool overlay) ;
static void close_macro_stream(PCL5Context *pcl5_ctxt, FILELIST **readstream) ;

typedef struct MACRO_DATA_BLOCK {
  uint8 data[MACRO_DATA_BLOCK_SIZE] ;
  struct MACRO_DATA_BLOCK *next ;
  struct MACRO_DATA_BLOCK *prev ;
} MACRO_DATA_BLOCK ;

typedef struct PCL5MacroStorage {
  /* Total bytes in the macro stream. */
  uint32 total_bytes ;
  /* List of data blocks for this macro. */
  MACRO_DATA_BLOCK *first_data_block ;
  /* Pointer to the last data block. */
  MACRO_DATA_BLOCK *last_data_block ;
  /* Where to put the next byte when creating this macro. */
  uint8 *write_pos ;
} PCL5MacroStorage ;

static pcl5_macro *macro_being_defined ;

MacroInfo* get_macro_info(PCL5Context *pcl5_ctxt)
{
  return &(pcl5_ctxt->print_state->mpe->macro_info) ;
}

void default_macro_info(MacroInfo* self)
{
  self->macro_numeric_id = 0 ;
  self->macro_string_id.buf = NULL ;
  self->macro_string_id.length = 0 ;

  self->overlay_macro_numeric_id = 0 ;
  self->overlay_macro_string_id.buf = NULL ;
  self->overlay_macro_string_id.length = 0 ;
}

static
void cleanup_macroinfo_overlay_strings(MacroInfo* self)
{
  pcl5_cleanup_ID_string(&(self->overlay_macro_string_id)) ;
}

void cleanup_macroinfo_strings(MacroInfo *macro_info)
{
  cleanup_macroinfo_overlay_strings(macro_info) ;
  pcl5_cleanup_ID_string(&(macro_info->macro_string_id)) ;
}

Bool save_macro_info(PCL5Context *pcl5_ctxt, MacroInfo *to_macro_info, MacroInfo *from_macro_info)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  /* Copy strings. */
  if (! pcl5_copy_ID_string(&(to_macro_info->macro_string_id),
                            &(from_macro_info->macro_string_id)))
    return FALSE ;

  if (! pcl5_copy_ID_string(&(to_macro_info->overlay_macro_string_id),
                            &(from_macro_info->overlay_macro_string_id)))
    return FALSE ;

  return TRUE ;
}

void restore_macro_info(PCL5Context *pcl5_ctxt, MacroInfo *to_macro_info, MacroInfo *from_macro_info)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(MacroInfo*, to_macro_info) ;

  cleanup_macroinfo_strings(from_macro_info) ;
}

static
Bool cache_macro_info(MacroInfo* self)
{
  /* Free any previously cached macro names. */
  cleanup_macroinfo_overlay_strings(self) ;

  self->overlay_macro_numeric_id = self->macro_numeric_id ;

  if (! pcl5_copy_ID_string(&(self->overlay_macro_string_id),
                            &(self->macro_string_id)))
    return FALSE ;

  return TRUE ;
}

Bool is_string_macro(MacroInfo* macro_info, Bool overlay)
{
  if (overlay) {
    return macro_info->overlay_macro_string_id.buf != NULL ;
  } else {
    return macro_info->macro_string_id.buf != NULL ;
  }
}

/* ============================================================================
 * Macro storage.
 *
 * Each macro (when in memory) is represented by a macro structure
 * which in turn has a linked list of macro data blocks which
 * represent the macro data stream. Its unknown if this is an optimal
 * representation until we get real world examples and this code
 * remains mainly as a prototype interface to excercise macro
 * execution in the interpreter.
 * ============================================================================
 */
static Bool create_macro_storage(PCL5MacroStorage **macro)
{
  PCL5MacroStorage *new_macro ;
  HQASSERT(macro != NULL, "macro is NULL") ;
  *macro = NULL ;

  if ((new_macro = mm_alloc(mm_pcl_pool, sizeof(PCL5MacroStorage),
                            MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL)
    return FALSE ;

  /* We lazily create the data blocks. */
  new_macro->first_data_block = NULL ;
  new_macro->last_data_block = NULL ;
  new_macro->total_bytes = 0 ;
  new_macro->write_pos = NULL ;

  *macro = new_macro ;

  return TRUE ;
}

static void destroy_macro_storage(PCL5MacroStorage **macro)
{
  MACRO_DATA_BLOCK *data_blocks ;

  HQASSERT(macro != NULL, "macro is NULL") ;
  HQASSERT(*macro != NULL, "*macro is NULL") ;

  /* Free macro data blocks. */
  data_blocks = (*macro)->first_data_block ;
  while (data_blocks != NULL) {
    MACRO_DATA_BLOCK *data = data_blocks ;
    data_blocks = data_blocks->next ;
    mm_free(mm_pcl_pool, data, sizeof(MACRO_DATA_BLOCK)) ;
  }

  /* Free of structure. */
  mm_free(mm_pcl_pool, *macro, sizeof(PCL5MacroStorage)) ;
  *macro = NULL ;
}

/* Purely for the cleanup callback. */
static void RIPCALL free_private_data_callback(void *private_data)
{
  PCL5MacroStorage *macro_storage ;
  HQASSERT(private_data != NULL, "private_data is NULL") ;
  macro_storage = private_data ;
  destroy_macro_storage(&macro_storage) ;
}

/* Records the specified byte within the active macro definition. */
Bool push_macro_byte(int32 ch)
{
  PCL5MacroStorage *macro ;

  HQASSERT(pcl5_recording_a_macro, "umm, we are not recording a macro") ;
  HQASSERT(ch >= 0 && ch <= 255, "umm, ch appears to be invalid") ;
  HQASSERT(macro_being_defined != NULL, "macro_being_defined is NULL") ;
  macro = macro_being_defined->detail.private_data ;
  HQASSERT(macro != NULL, "macro is NULL") ;

#if defined(DEBUG_BUILD)
  if ( debug_pcl5 & PCL5_CONTROL ) {
    monitorf((uint8*)"           ST(%c)\n", (uint8)ch) ;
  }
#endif

  /* Do we need a new data block to store this byte? */
  if (macro->total_bytes % MACRO_DATA_BLOCK_SIZE == 0) {
    MACRO_DATA_BLOCK *data ;

    data = mm_alloc(mm_pcl_pool, sizeof(MACRO_DATA_BLOCK),
                    MM_ALLOC_CLASS_PCL_CONTEXT) ;
    if (data == NULL)
      return FALSE ;
    macro->write_pos = &(data->data)[0] ;
    data->next = NULL ;
    data->prev = macro->last_data_block ;

    if (macro->first_data_block == NULL) {
      macro->first_data_block = data ;
    } else {
      macro->last_data_block->next = data ;
    }
    macro->last_data_block = data ;
  }

  HQASSERT( macro->write_pos >= &(macro->last_data_block->data)[0] &&
            macro->write_pos < (&(macro->last_data_block->data)[0] +
            MACRO_DATA_BLOCK_SIZE),
            "macro->write_pos has become corrupt" ) ;

  *(macro->write_pos)++ = (uint8)ch ;
  macro->total_bytes++ ;

  return TRUE ;
}

/* Pops a single byte from the active macro definition. */
Bool pop_macro_byte(PCL5Context *pcl5_ctxt)
{
  PCL5MacroStorage *macro ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  HQASSERT(pcl5_recording_a_macro, "umm, we are not recording a macro") ;
  HQASSERT(macro_being_defined != NULL, "macro_being_defined is NULL") ;
  macro = macro_being_defined->detail.private_data ;
  HQASSERT(macro != NULL, "macro is NULL") ;

  if (macro->total_bytes > 0) {
#if defined(DEBUG_BUILD)
    if ( debug_pcl5 & PCL5_CONTROL ) {
      monitorf((uint8*)"           POP\n") ;
    }
#endif

    macro->write_pos-- ;
    macro->total_bytes-- ;

    HQASSERT(macro->last_data_block != NULL,
             "last_data_block has become currupt") ;

    /* Are we at the start of the macro data block? If so, we can
       deallocate this macro data block. */
    if (macro->write_pos ==
        &(macro->last_data_block->data)[0]) {
      MACRO_DATA_BLOCK *old_data_block = macro->last_data_block ;

      if (macro->last_data_block->prev != NULL) {
        macro->last_data_block->prev->next =  NULL ;
        macro->last_data_block = macro->last_data_block->prev ;
      }
      if (macro->total_bytes == 0) {
        macro->first_data_block = NULL ;
        macro->write_pos = NULL ;
      } else {
        macro->write_pos = &(macro->last_data_block->data)[0] + MACRO_DATA_BLOCK_SIZE ;
      }
      mm_free(mm_pcl_pool, old_data_block, sizeof(MACRO_DATA_BLOCK)) ;
    }
  }

  return TRUE ;
}

Bool set_macro_string_id(PCL5Context *pcl5_ctxt, uint8 *string, int32 length)
{
  MacroInfo *macro_info ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  macro_info = get_macro_info(pcl5_ctxt) ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;


  /* Free any existing macro string ID */
  pcl5_cleanup_ID_string(&(macro_info->macro_string_id)) ;

  if ((macro_info->macro_string_id.buf = mm_alloc(mm_pcl_pool, length,
                                                  MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL)
    return FALSE ;

  macro_info->macro_string_id.length = length ;
  HqMemCpy(macro_info->macro_string_id.buf, string, length) ;

  return TRUE ;
}

Bool associate_macro_string_id(PCL5Context *pcl5_ctxt, uint8 *string, int32 length)
{
  MacroInfo *macro_info ;
  pcl5_macro *macro ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  macro_info = get_macro_info(pcl5_ctxt) ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;
  HQASSERT(string != NULL, "string is NULL") ;
  HQASSERT(length > 0, "length is not greater than zero") ;

  if ((macro = pcl5_string_id_cache_get_macro(pcl5_ctxt->resource_caches.string_macro, string, length)) != NULL) {
    if (is_string_macro(macro_info, FALSE)) {
      uint8 *string ;
      int32 length ;
      string = macro_info->macro_string_id.buf ;
      length = macro_info->macro_string_id.length ;

      if (! pcl5_string_id_cache_insert_aliased_macro(pcl5_ctxt->resource_caches.string_macro,
                                                      pcl5_ctxt->resource_caches.macro,
                                                      macro, string, length))
        return FALSE ;
    } else {
      pcl5_resource_numeric_id macro_numeric_id = macro_info->macro_numeric_id ;

      if (! pcl5_id_cache_insert_aliased_macro(pcl5_ctxt->resource_caches.macro, macro, macro_numeric_id))
        return FALSE ;
    }
  }

  return TRUE ;
}

Bool delete_macro_associated_string_id(PCL5Context *pcl5_ctxt)
{
  MacroInfo *macro_info ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  macro_info = get_macro_info(pcl5_ctxt) ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;

  if (is_string_macro(macro_info, FALSE)) {
    uint8 *string ;
    int32 length ;
    string = macro_info->macro_string_id.buf ;
    length = macro_info->macro_string_id.length ;

    pcl5_string_id_cache_remove(pcl5_ctxt->resource_caches.string_macro,
                                pcl5_ctxt->resource_caches.macro,
                                string, length, TRUE) ;
  } else {
    pcl5_resource_numeric_id macro_numeric_id = macro_info->macro_numeric_id ;

    pcl5_id_cache_remove(pcl5_ctxt->resource_caches.macro,
                         macro_numeric_id, TRUE) ;
  }

  return TRUE ;
}

/* ============================================================================
 * Macro commands.
 * ============================================================================
 */

void destroy_macros(PCL5Context *pcl5_ctxt, int32 type)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(type == 6 || type == 7, "Type is invalid.") ;

  if (type == 7) { /* Temporary only. */
    pcl5_id_cache_remove_all(pcl5_ctxt->resource_caches.macro,
                             FALSE) ;
    pcl5_string_id_cache_remove_all(pcl5_ctxt->resource_caches.string_macro,
                                    pcl5_ctxt->resource_caches.macro,
                                    FALSE) ;
  } else {
    pcl5_id_cache_remove_all(pcl5_ctxt->resource_caches.macro,
                             TRUE) ;
    pcl5_string_id_cache_remove_all(pcl5_ctxt->resource_caches.string_macro,
                                    pcl5_ctxt->resource_caches.macro,
                                    TRUE) ;
  }
  pcl5_id_cache_kill_zombies(pcl5_ctxt->resource_caches.macro) ;
}

static Bool start_macro_definition(PCL5Context *pcl5_ctxt, MacroInfo *macro_info)
{
  pcl5_macro macro, *old_macro, *new_macro ;
  PCL5MacroStorage *private_macro_data ;
  FILELIST *filter;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;

  pcl5_recording_a_macro = TRUE ;

  if (! create_macro_storage(&private_macro_data))
    return FALSE ;

  if (is_string_macro(macro_info, FALSE)) {
    uint8 *string ;
    int32 length ;
    string = macro_info->macro_string_id.buf ;
    length = macro_info->macro_string_id.length ;

    /* Clobber the macro if it already exists. */
    if ((old_macro = pcl5_string_id_cache_get_macro(pcl5_ctxt->resource_caches.string_macro, string, length)) != NULL) {
      pcl5_string_id_cache_remove(pcl5_ctxt->resource_caches.string_macro,
                                  pcl5_ctxt->resource_caches.macro,
                                  string, length, FALSE) ;
    }

    macro.detail.resource_type = SW_PCL5_MACRO ;
    macro.detail.numeric_id = 0 ;
    macro.detail.string_id.buf = NULL ;
    macro.detail.string_id.length = 0 ;
    macro.detail.permanent = FALSE ;
    macro.detail.device = NULL ;
    macro.detail.private_data = private_macro_data ;
    macro.detail.PCL5FreePrivateData = free_private_data_callback ;

    if (! pcl5_string_id_cache_insert_macro(pcl5_ctxt->resource_caches.string_macro,
                                            pcl5_ctxt->resource_caches.macro,
                                            string, length, &macro, &new_macro)) {
      destroy_macro_storage(&private_macro_data) ;
      return FALSE ;
    }

  } else {
    pcl5_resource_numeric_id macro_numeric_id = macro_info->macro_numeric_id ;

    /* Clobber the macro if it already exists. */
    if ((old_macro = pcl5_id_cache_get_macro(pcl5_ctxt->resource_caches.macro, macro_numeric_id)) != NULL) {
      pcl5_id_cache_remove(pcl5_ctxt->resource_caches.macro,
                           macro_numeric_id, FALSE) ;
      pcl5_id_cache_kill_zombies(pcl5_ctxt->resource_caches.macro) ;
    }

    macro.detail.resource_type = SW_PCL5_MACRO ;
    macro.detail.numeric_id = macro_numeric_id ;
    macro.detail.permanent = FALSE ;
    macro.detail.device = NULL ;
    macro.detail.private_data = private_macro_data ;
    macro.detail.PCL5FreePrivateData = free_private_data_callback ;

    if (! pcl5_id_cache_insert_macro(pcl5_ctxt->resource_caches.macro, macro_numeric_id, &macro, &new_macro)) {
      destroy_macro_storage(&private_macro_data) ;
      return FALSE ;
    }
  }

  if (! filter_layer(pcl5_ctxt->flptr,
                     NAME_AND_LENGTH("PCL5MacroRecord"), NULL, &filter)) {
    destroy_macro_storage(&private_macro_data) ;
    return FALSE ;
  }

  pcl5_ctxt->flptr = filter ;

  macro_being_defined = new_macro ;

  return TRUE ;
}

Bool stop_macro_definition(PCL5Context *pcl5_ctxt, MacroInfo *macro_info)
{
  PCL5MacroStorage *macro ;
  uint8 ch ;
  int32 byte ;
  Bool found_1 = FALSE ;
  FILELIST *filter;

  UNUSED_PARAM(MacroInfo *, macro_info) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;

  /* If we are not recording a macro, ignore this command and move
     on. */
  if (! pcl5_recording_a_macro)
    return TRUE ;

  HQASSERT(macro_being_defined != NULL, "macro_being_defined is NULL") ;

#ifdef ASSERT_BUILD
  if (is_string_macro(macro_info, FALSE)) {
    uint8 *string ;
    int32 length ;
    string = macro_info->macro_string_id.buf ;
    length = macro_info->macro_string_id.length ;

    HQASSERT((macro_being_defined->detail.string_id.length == length &&
              HqMemCmp(macro_being_defined->detail.string_id.buf,
                       macro_being_defined->detail.string_id.length,
                       string, length) == 0),
             "macro_being_defined is invalid") ;
  }
  else {
    pcl5_resource_numeric_id macro_numeric_id = macro_info->macro_numeric_id ;
    HQASSERT(macro_being_defined->detail.numeric_id == macro_numeric_id,
             "macro_being_defined is invalid") ;
  }
#endif

  /* We would have recorded this command as part of the macro, so remove it.
   * ESC & f 1 X, ESC & f -1.5 X, or similar.
   *
   * N.B. It may be that we don't really need to remove this at all, and
   *      would manage to ignore it OK while running the macro, though
   *      it is perhaps neater to get rid of it.
   */
  macro = macro_being_defined->detail.private_data ;
  HQASSERT(macro != NULL, "macro is NULL") ;

  while (macro->total_bytes > 0) {
    /* N.B. It is not necessarily safe to look at the byte at write_pos,
     *      but should be OK to look at the one before.
     */
    ch = *(macro->write_pos - 1) ;

    if (ch == '1')
      found_1 = TRUE ;
    else if (found_1 && is_group_char(ch) && ch != 'f') {
      /* The parameter or group character is not 'f', so we must have a
       * combined escape sequence, such as ESC & f 1 s 1 X, which we need
       * to leave as ESC & f 1 s
       */
      break ;
    }

    pop_macro_byte(pcl5_ctxt) ;

    if (ch == ESCAPE)
      break ;
  }

  filter = pcl5_ctxt->flptr ;
  pcl5_ctxt->flptr = theIUnderFile(pcl5_ctxt->flptr) ;
  byte = 0 ;

  /* This is a real hack. We know that the buffer size on the macro
     recording filter is 1 byte in length and we always read one byte
     ahead (within filters) so that an UnGetc() can be done
     efficiently. Hence we need to always do an UnGetc() on the
     underlying file as the macro recording filter has consumed one
     byte too many (from its underlying file). Note that byte is not
     actually used by UnGetc() so we don't care what it is. We just
     set it to zero. */
  UnGetc(byte, pcl5_ctxt->flptr) ;

  /* This is where any finalisation on storing the macro should go. */

  pcl5_recording_a_macro = FALSE ;
  macro_being_defined = NULL ;

  SetIClosingFlag( filter ) ;
  (*theIMyDisposeFile(filter))(filter) ;
  ClearIClosingFlag( filter ) ;
  SetIEofFlag( filter ) ;

  return TRUE ;
}

Bool execute_macro_definition(PCL5Context *pcl5_ctxt, MacroInfo *macro_info, Bool overlay)
{
  FILELIST *macro_stream ;
  Bool status = TRUE ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;

  /* Only a nested depth of 3 macros. */
  if (pcl5_macro_nest_level == MAX_MACRO_NEST_LEVEL)
    return TRUE ;

  if (open_macro_stream_for_read(pcl5_ctxt, macro_info, &macro_stream, overlay)) {
    uint32 old_interpreter_mode ;
    FILELIST *old_filelist ;
    MacroInfo old_macro_info ;
    MacroInfo *macro_info ;

    macro_info = get_macro_info(pcl5_ctxt) ;

    old_filelist = pcl5_ctxt->flptr ;
    old_interpreter_mode = pcl5_ctxt->interpreter_mode ;
    old_macro_info = *macro_info ;

    /* Clobber state to run the macro. */
    pcl5_ctxt->flptr = macro_stream ;
    pcl5_macro_nest_level++ ;

    if ( pcl5_ctxt->pcl5c_enabled) {
      pcl5_ctxt->interpreter_mode = PCL5C_MACRO_MODE ;
    } else {
      pcl5_ctxt->interpreter_mode = PCL5E_MACRO_MODE ;
    }

    status = pcl5_execops(pcl5_ctxt) ;

    /* an overlay macro can finish without having flushed its
       last piece of text to the DL. */
    if (overlay)
      pcl5_flush_text_run(pcl5_ctxt, 1);

    /* Restore state. */
    *macro_info = old_macro_info ;
    pcl5_ctxt->interpreter_mode = old_interpreter_mode ;
    pcl5_macro_nest_level-- ;
    pcl5_ctxt->flptr = old_filelist ;

    close_macro_stream(pcl5_ctxt, &macro_stream) ;
  }

  /* If we don't find the macro, ignore. */
  return status ;
}

void remove_macro_definition(PCL5Context *pcl5_ctxt, MacroInfo *macro_info)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;

  if (is_string_macro(macro_info, FALSE)) {
    uint8 *string ;
    int32 length ;
    string = macro_info->macro_string_id.buf ;
    length = macro_info->macro_string_id.length ;

    pcl5_string_id_cache_remove(pcl5_ctxt->resource_caches.string_macro,
                                pcl5_ctxt->resource_caches.macro,
                                string, length, FALSE) ;

  } else {
    pcl5_resource_numeric_id macro_numeric_id = macro_info->macro_numeric_id ;

    pcl5_id_cache_remove(pcl5_ctxt->resource_caches.macro,
                         macro_numeric_id, FALSE) ;
    pcl5_id_cache_kill_zombies(pcl5_ctxt->resource_caches.macro) ;
  }
}

void set_macro_permanent(PCL5Context *pcl5_ctxt, MacroInfo *macro_info, Bool permanent)
{
  if (is_string_macro(macro_info, FALSE)) {
    uint8 *string ;
    int32 length ;
    string = macro_info->macro_string_id.buf ;
    length = macro_info->macro_string_id.length ;
    pcl5_string_id_cache_set_permanent(pcl5_ctxt->resource_caches.string_macro, string, length, permanent) ;
  } else {
    pcl5_resource_numeric_id macro_numeric_id = macro_info->macro_numeric_id ;
    pcl5_id_cache_set_permanent(pcl5_ctxt->resource_caches.macro, macro_numeric_id, permanent) ;
  }
}

/* ============================================================================
 * Macro open/close and read bytes.
 * ============================================================================
 */

#define STREAM_CLOSED -1

typedef struct PCL5MacroReadInstance {
  /* Basically a read macro file descriptor. */
  int32 read_id ;
  /* Pointer to the data block we are reading. */
  MACRO_DATA_BLOCK *current_read_data_block ;
  /* Where to read the next byte from when executing this macro. */
  uint8 *read_pos ;
  /* When a macro is opened for reading, we layer a FILELIST onto
     it. */
  /* Used when reading a macro. How many bytes left to read. */
  uint32 total_remaining ;
  FILELIST read_stream ;
  /* Keep the FILELIST buffer the same size as a macro data block. */
  uint8 read_buf[MACRO_DATA_BLOCK_SIZE] ;
  /* Pointer to the macro we are reading. */
  PCL5MacroStorage *macro ;
} PCL5MacroReadInstance ;

/* How many read streams do we have? */
static uint32 num_read_streams ;
static PCL5MacroReadInstance read_streams[MAX_MACRO_NEST_LEVEL] ;

/* At least get access to the PCL5 macro device. Declare here so that
   we do not use this device in any other code. */
extern DEVICELIST *pcl5_macrodevice ;

static
Bool open_macro_stream_for_read(PCL5Context *pcl5_ctxt, MacroInfo *macro_info,
                                FILELIST** readstream, Bool overlay)
{
  pcl5_macro *zee_macro ;
  PCL5MacroStorage *macro ;
  PCL5MacroReadInstance *read_instance ;
  int32 i;
  int32 readstream_id ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;

  HQASSERT(readstream != NULL, "readstream is NULL") ;
  HQASSERT(pcl5_macrodevice != NULL, "macro device is NULL") ;

  *readstream = NULL ;

  if (num_read_streams == MAX_MACRO_NEST_LEVEL)
    return FALSE ;

  if (is_string_macro(macro_info, overlay)) {
    uint8 *string ;
    int32 length ;

    if (overlay) {
      string = macro_info->overlay_macro_string_id.buf ;
      length = macro_info->overlay_macro_string_id.length ;
    } else {
      string = macro_info->macro_string_id.buf ;
      length = macro_info->macro_string_id.length ;
    }
    if ((zee_macro = pcl5_string_id_cache_get_macro(pcl5_ctxt->resource_caches.string_macro, string, length)) == NULL)
      return FALSE ;

  } else {
    pcl5_resource_numeric_id macro_numeric_id ;

    if (overlay) {
      macro_numeric_id = macro_info->overlay_macro_numeric_id ;
    } else {
      macro_numeric_id = macro_info->macro_numeric_id ;
    }

    if ((zee_macro = pcl5_id_cache_get_macro(pcl5_ctxt->resource_caches.macro, macro_numeric_id)) == NULL)
      return FALSE ;
  }

  macro = zee_macro->detail.private_data ;
  HQASSERT(macro != NULL, "macro is NULL") ;

  /* Find an empty slot. */
  for (i=0; i < MAX_MACRO_NEST_LEVEL; i++) {
    if (read_streams[i].read_id == STREAM_CLOSED)
      break ;
  }
  readstream_id = i ;

  HQASSERT(readstream_id < MAX_MACRO_NEST_LEVEL,
           "Something odd with finding an empty slot.") ;

  read_instance = &read_streams[readstream_id] ;

  HQASSERT(read_instance->read_stream.typetag == tag_LIMIT,
           "FILELIST has become corrupt.") ;

  read_instance->read_id = readstream_id ;
  read_instance->current_read_data_block = macro->first_data_block ;
  read_instance->read_pos = &(read_instance->current_read_data_block->data)[0] ;
  read_instance->total_remaining = macro->total_bytes ;
  read_instance->macro = macro ;

  init_filelist_struct(&(read_instance->read_stream),
                       NAME_AND_LENGTH("PCL5MacroFileList"),
                       REALFILE_FLAG | READ_FLAG | OPEN_FLAG,
                       readstream_id /* descriptor */,
                       &(read_instance->read_buf)[0] /* buf */,
                       MACRO_DATA_BLOCK_SIZE /* buf size */,
                       FileFillBuff, /* fillbuff */
                       FileFlushBufError, /* flushbuff */
                       FileInit, /* initfile */
                       FileClose, /* closefile */
                       FileDispose, /* disposefile */
                       FileBytes, /* bytesavail */
                       FileReset, /* resetfile */
                       FilePos, /* filepos */
                       FileSetPos, /* setfilepos */
                       FileFlushFile, /* flushfile */
                       FileEncodeError, /* filterencode */
                       FileDecodeError, /* filterdecode */
                       FileLastError, /* lasterror */
                       -1 /* filterstate */,
                       pcl5_macrodevice /* device */,
                       NULL /* underfile */,
                       NULL /* next */) ;

  *readstream = &(read_instance->read_stream) ;

  return TRUE ;
}

static void close_macro_stream(PCL5Context *pcl5_ctxt, FILELIST **readstream)
{
  PCL5MacroReadInstance *read_instance ;
  DEVICE_FILEDESCRIPTOR readstream_id ;

  /* Unused by check that it is passed in. */
  UNUSED_PARAM(PCL5Context *, pcl5_ctxt) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  HQASSERT(readstream != NULL, "readstream is NULL") ;
  HQASSERT(*readstream != NULL, "*readstream is NULL") ;

  readstream_id = theIDescriptor((*readstream)) ;
  HQASSERT((readstream_id >= 0 && readstream_id < MAX_MACRO_NEST_LEVEL),
           "Read stream file descriptor is corrupt.") ;

  read_instance = &read_streams[readstream_id] ;

  read_instance->read_id = STREAM_CLOSED ;
  read_instance->current_read_data_block = NULL ;
  read_instance->read_pos = NULL ;
  read_instance->total_remaining = 0 ;
  read_instance->read_stream.typetag = tag_LIMIT ;

  *readstream = NULL ;
}

/* Do not call this directly, this function is used by the macro read
   device implementation only. */
int32 Getc_macro(PCL5Context *pcl5_ctxt, DEVICE_FILEDESCRIPTOR descriptor)
{
  PCL5MacroStorage *macro ;
  PCL5MacroReadInstance *read_instance ;

  HQASSERT(! pcl5_recording_a_macro, "umm, we seem to be recording a macro") ;
  /* Unused by check that it is passed in. */
  UNUSED_PARAM(PCL5Context *, pcl5_ctxt) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  HQASSERT((descriptor >= 0 && descriptor < MAX_MACRO_NEST_LEVEL),
           "Read stream file descriptor is corrupt.") ;

  read_instance = &read_streams[descriptor] ;
  macro = read_instance->macro ;

  HQASSERT(macro != NULL, "macro is invalid") ;
  HQASSERT(read_instance->current_read_data_block != NULL, "current_read_data_block is NULL") ;

  /* End of macro stream. */
  if (read_instance->total_remaining == 0)
    return EOF ;

  /* We need to move onto the next data block. */
  if (read_instance->read_pos - &(read_instance->current_read_data_block->data)[0]
      == MACRO_DATA_BLOCK_SIZE) {
    read_instance->current_read_data_block = read_instance->current_read_data_block->next ;
    read_instance->read_pos = &(read_instance->current_read_data_block->data)[0] ;
    HQASSERT(read_instance->current_read_data_block != NULL,
             "no next block yet there are still bytes remaining") ;
  }

  read_instance->total_remaining-- ;
  return (int32)*(read_instance->read_pos)++ ;
}

/* ============================================================================
 * Macro recording filter. Gets layered above the file stream when
 * recording a macro.
 * ============================================================================
 */

/* We only ever read one character at a time. */
#define MACROBUFFSIZE 1

static Bool macroFilterInit(FILELIST *filter, OBJECT *args, STACK *stack)
{
  int32 pop_args = 0 ;

  HQASSERT(filter != NULL, "filter is NULL") ;

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;
    OCopy( theIParamDict( filter ), *args ) ;
  } else
    args = NULL ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  theIBuffer( filter ) = mm_alloc( mm_pool_temp ,
                                   MACROBUFFSIZE + 1 ,
                                   MM_ALLOC_CLASS_PCL_CONTEXT ) ;

  if ( theIBuffer( filter ) == NULL )
    return error_handler( VMERROR ) ;

  theIBuffer( filter )++ ;
  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  theIBufferSize( filter ) = MACROBUFFSIZE ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void macroFilterDispose(FILELIST *filter)
{
  HQASSERT(filter != NULL, "filter is NULL") ;

  if ( theIBuffer( filter )) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )( theIBuffer( filter ) - 1 ) ,
             MACROBUFFSIZE + 1 ) ;
    theIBuffer( filter ) = NULL ;
  }
}

static Bool macroDecodeBuffer(FILELIST *filter, int32 *ret_bytes)
{
  register uint8    *ptr ;
  register FILELIST *uflptr ;
  register int32    c ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(ret_bytes != NULL, "ret_bytes is NULL") ;

  uflptr = theIUnderFile(filter) ;
  ptr = theIBuffer(filter) ;

  HQASSERT( uflptr , "uflptr NULL in asciihexDecodeBuffer." ) ;
  HQASSERT( ptr , "ptr NULL in asciihexDecodeBuffer." ) ;

  HQASSERT(pcl5_recording_a_macro, "umm, we don't seem to be recording a macro") ;
  HQASSERT(macro_being_defined != NULL, "macro_being_defined is NULL") ;

  if ((c = Getc(uflptr)) == EOF) {
    *ret_bytes = 0 ;
  } else {
    *ptr = (uint8)c ;
    *ret_bytes = 1 ;

    if (! push_macro_byte(c)) {
      *ret_bytes = 0 ;
      return error_handler(IOERROR) ;
    }
  }

  return TRUE ;
}

Bool pcl5_macro_record_filter_init(void)
{
  FILELIST *flptr ;

  if ( (flptr = mm_alloc_static(sizeof(FILELIST))) == NULL )
    return FALSE ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("PCL5MacroRecord"),
                       FILTER_FLAG | READ_FLAG ,
                       0 , NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       macroFilterInit,                      /* initfile */
                       FilterCloseFile,                      /* closefile */
                       macroFilterDispose,                   /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       macroDecodeBuffer ,                   /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;

  filter_standard_add(flptr);

  return TRUE ;
}

/* ============================================================================
 * Macro PCL5 operator callbacks are below here.
 * ============================================================================
 */

/* The Macro ID command specifies an ID number for use in subsequent macro commands.

   ESC & f # Y

   # = Macro ID number

   Default = 0
   Range = 0 - 32767

   This number is used in subsequent macro operations. The factory
   default macro ID is 0. */
Bool pcl5op_ampersand_f_Y(PCL5Context *pcl5_ctxt, Bool is_negative, PCL5Numeric value)
{
  MacroInfo *macro_info ;
  pcl5_resource_numeric_id macro_numeric_id = (pcl5_resource_numeric_id)value.integer ;

  UNUSED_PARAM(Bool, is_negative) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  macro_info = get_macro_info(pcl5_ctxt) ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;

  if (macro_numeric_id < 32767) {
    /* If current Macro ID is a string ID so deallocate it. */
    pcl5_cleanup_ID_string(&(macro_info->macro_string_id));
    macro_info->macro_numeric_id = macro_numeric_id ;
  }

  /* If its out of range, do nothing. */
  return TRUE ;
}

/* The macro control command provides mechanisms for definition,
   invocation, and deletion of macros.

   ESC & f # X

   # = 0 - Start macro definition (last ID specified)
       1 - Stop macro definition
       2 - Execute macro (last ID specified)
       3 - Call macro (last ID specified)
       4 - Enable macro for automatic overlay (last ID specified)
       5 - Disable automatic overlay
       6 - Delete all macros
       7 - Delete all temporary macros
       8 - Delete macro (last ID specified)
       9 - Make macro temporary (last ID specified)
      10 - Make macro permanent (last ID specified) */
Bool pcl5op_ampersand_f_X(PCL5Context *pcl5_ctxt, Bool is_negative, PCL5Numeric value)
{
  Bool status = TRUE ;
  MacroInfo *macro_info ;
   /* N.B. Printer treats negative values as positive */
  int32 macro_command = abs(value.integer) ;
  struct PCL5PrintState *p_state = pcl5_ctxt->print_state ;
  int32 old_macro_mode ;

  UNUSED_PARAM(Bool, is_negative) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  macro_info = get_macro_info(pcl5_ctxt) ;
  HQASSERT(macro_info != NULL, "macro_info is NULL") ;

   /* We do this lazily now */
  if (macro_command < 4 && ! pcl5_flush_text_run(pcl5_ctxt,1))
    return FALSE ;
  
  /* If we are recording a macro, the only macro control command we
     execute is the stop macro definition. */
  if (pcl5_recording_a_macro && macro_command != 1)
    return TRUE ;

  /* When executing a macro, the only commands allowed are call and
     execute. */
  if (pcl5_macro_nest_level > 0 && macro_command != 2 && macro_command != 3)
    return TRUE ;

  switch (macro_command) {
  case 0:
    status = start_macro_definition(pcl5_ctxt, macro_info) ;
    break ;
  case 1:
    status = stop_macro_definition(pcl5_ctxt, macro_info) ;
    break ;
  case 2:
    old_macro_mode = pcl5_current_macro_mode ;
    pcl5_current_macro_mode = PCL5_EXECUTING_EXECUTED_MACRO ;

    status = execute_macro_definition(pcl5_ctxt, macro_info, FALSE) ;

    pcl5_current_macro_mode = old_macro_mode ;
    break ;
  case 3: /* call */
    status = status && pcl5_mpe_save(pcl5_ctxt, CREATE_MACRO_ENV) ;
    if (status) {
      /* We need to test if underline mode should be part of the MPE? */
      int32 underline_mode = p_state->font_state.underline_mode ;
      old_macro_mode = pcl5_current_macro_mode ;
      pcl5_current_macro_mode = PCL5_EXECUTING_CALLED_MACRO ;

      status = execute_macro_definition(pcl5_ctxt, macro_info, FALSE) ;

      pcl5_current_macro_mode = old_macro_mode ;
      p_state->font_state.underline_mode = underline_mode ;
      status = reset_underline_details(pcl5_ctxt) && status ;
      status = pcl5_mpe_restore(pcl5_ctxt) && status ;
    }
    break ;
  case 4:
    p_state->allow_macro_overlay = TRUE ;
    /* We need to cache the ID as it is possible that we will execute
       another macro before we throw a page. */
    if (! cache_macro_info(macro_info))
      return FALSE ;
    break ;
  case 5:
    p_state->allow_macro_overlay = FALSE ;
    break ;
  case 6:
  case 7:
    destroy_macros(pcl5_ctxt, macro_command) ;
    break ;
  case 8:
    remove_macro_definition(pcl5_ctxt, macro_info) ;
    break ;
  case 9:
    set_macro_permanent(pcl5_ctxt, macro_info, FALSE) ;
    break ;
  case 10:
    set_macro_permanent(pcl5_ctxt, macro_info, TRUE) ;
    break ;
  default:
    /* We ignore invalid values. */
    break ;
  }

  return status ;
}

/* ============================================================================
 * Init/Finish PCL5 macro subsystem.
 * ============================================================================
 */
void init_C_globals_macros(void)
{
  macro_being_defined = NULL ;
}

Bool pcl5_macros_init(PCL5Context *pcl5_ctxt)
{
  int32 i ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  macro_being_defined = NULL ;
  num_read_streams = 0 ;

  for (i=0; i < MAX_MACRO_NEST_LEVEL; i++) {
    read_streams[i].read_id = STREAM_CLOSED ;
    read_streams[i].current_read_data_block = NULL ;
    read_streams[i].read_pos = NULL ;
    read_streams[i].total_remaining = 0 ;
    read_streams[i].read_stream.typetag = tag_LIMIT ;
  }

  return TRUE ;
}

void pcl5_macros_finish(PCL5Context *pcl5_ctxt)
{
#define DELETE_ALL_MACROS 6
  destroy_macros(pcl5_ctxt, DELETE_ALL_MACROS) ;
  macro_being_defined = NULL ;
}

/* ============================================================================
* Log stripped */
