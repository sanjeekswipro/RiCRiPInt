/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlerrors.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Error handling function(s)
 */

#include <stdarg.h>

#include "core.h"
#include "swctype.h"
#include "fileio.h"
#include "swcopyf.h"
#include "hqmemset.h"
#include "monitor.h"
#include "namedef_.h"

#include "pclxldebug.h"
#include "pclxlerrors.h"
#include "pclxlcontext.h"
#include "pclxloperators.h"
#include "pclxlpsinterface.h"
#include "pclxlpage.h"
#include "pclxlfont.h"
#include "pclxlgraphicsstate.h"

/* In order to allow the recording of error information even in the
 * event of a failure to allocate a PCLXL_ERROR_INFO in virtual memory
 * we maintain a small statically declared pool of
 * PCLXL_ERROR_INFO_STRUCT which can be used in extremis.
 *
 * pclxl_get_next_static_error_info() allocates (and indeed
 * reallocates) the next available statically declared
 * PCLXL_ERROR_INFO[_STRUCT] from this statically delcared pool
 *
 * Note that this function *must never* fail Therefore whilst it will
 * *always* return a non-NULL PCLXL_ERROR_INFO it cannot guarantee not
 * to return an error info struct that is not already in use
 * elsewhere.
 */

static PCLXL_ERROR_INFO
pclxl_get_next_static_error_info()
{
  static PCLXL_ERROR_INFO_STRUCT error_infos[10];
  static size_t next_error_info = 0;
  static size_t error_infos_len = (sizeof(error_infos) / sizeof(error_infos[0]));

  size_t i;

  /* We would prefer to (re-)allocate an error info which was
   * (probably) not in use. We guess which one might not be in use by
   * looking for a zero error_code.
   */
  for ( i = 0 ;
        i < error_infos_len ;
        i++ )
  {
    PCLXL_ERROR_INFO error_info = &error_infos[((next_error_info + i) % error_infos_len)];

    if ( error_info->error_code == 0)
    {
      next_error_info = (next_error_info + i + 1) % error_infos_len;

      HqMemZero((uint8 *)error_info, sizeof(PCLXL_ERROR_INFO_STRUCT));

      return error_info;
    }
  }

  /*
   * If *none* of them have a zero error_code
   * then we go for the "next" error info regardless
   */

  {
    PCLXL_ERROR_INFO error_info = &error_infos[next_error_info];

    next_error_info = ((next_error_info + 1) % error_infos_len);

    HqMemZero((uint8 *)error_info, (int)sizeof(PCLXL_ERROR_INFO_STRUCT));

    return error_info;
  }

  /*NOTREACHED*/
}

/* Under normal circumstances we always attempt to allocate a new
 * error info struct in dynamic memory.
 *
 * Only if this allocation fails do we resort to the statically allocated pool
 *
 * It is upto the caller of pclxl_allocate_error_info() to
 * a) handle and/or log the allocation failure and
 * b) decide whether to resort to the statically allocated pool
 *
 * In all probability the caller will *have to* resort to
 * the statically allocated pool if it wants to log the failure
 * to allocate a dynamic error info
 */
static PCLXL_ERROR_INFO
pclxl_allocate_error_info(PCLXL_CONTEXT pclxl_context)
{
  PCLXL_ERROR_INFO new_error_info;

  if ( (pclxl_context == NULL) ||
       (pclxl_context->memory_pool == NULL) ||
       ((new_error_info = mm_alloc(pclxl_context->memory_pool,
                                   sizeof(PCLXL_ERROR_INFO_STRUCT),
                                   MM_ALLOC_CLASS_PCLXL_ERROR_INFO)) == NULL) )
  {
    /* We failed to allocate a PCLXL error info structure so we cannot
     * populate it.
     *
     * But we explicitly do *not* log this allocation failure because
     * we would almost certainly result in an infinite recursion.
     *
     * Therefore it is upto the caller to log this error probably
     * using a statically allocated error info
     */
    return pclxl_get_next_static_error_info();
  }
  else
  {
    HqMemZero((uint8 *)new_error_info, (int)sizeof(PCLXL_ERROR_INFO_STRUCT));

    new_error_info->pclxl_context = pclxl_context;

    return new_error_info;
  }

  /*NOTREACHED*/
}

/*
 * frees any dynamically allocated substructure beneath a PCLXL_ERROR_INFO
 * frees (i.e. mm_free()) the error info structure itself
 * IFF it was allocated from a memory pool
 *
 * We decide this according to whether we have a non-NULL PCLXL_CONTEXT
 *
 * And this is needed because we can also encounter
 * an error info that was allocated from the static pool
 * which is used in "emergency" (i.e. in low memory conditions)
 */

static void
pclxl_free_error_info(PCLXL_ERROR_INFO error_info)
{
  HQASSERT((error_info != NULL), "Cannot delete NULL error info");

  if ( error_info->pclxl_context != NULL )
  {
    mm_pool_t memory_pool = error_info->pclxl_context->memory_pool;

#ifdef DEBUG_BUILD

    if ( (error_info->formatted_text != NULL) &&
         (error_info->formatted_text_alloc_len > 0) )
    {
      mm_free(memory_pool,
              error_info->formatted_text,
              error_info->formatted_text_alloc_len);

      error_info->formatted_text = NULL;

      error_info->formatted_text_len = 0;

      error_info->formatted_text_alloc_len = 0;
    }

#endif

    if ( (error_info->missing_font_name != NULL) &&
         (error_info->missing_font_name_alloc_len > 0) )
    {
      mm_free(memory_pool,
              error_info->missing_font_name,
              error_info->missing_font_name_alloc_len);

      error_info->missing_font_name = NULL;

      error_info->missing_font_name_len = 0;

      error_info->missing_font_name_alloc_len = 0;
    }

    if ( (error_info->substitute_font_name != NULL) &&
         (error_info->substitute_font_name_alloc_len > 0) )
    {
      mm_free(memory_pool,
              error_info->substitute_font_name,
              error_info->substitute_font_name_alloc_len);

      error_info->substitute_font_name = NULL;

      error_info->substitute_font_name_len = 0;

      error_info->substitute_font_name_alloc_len = 0;
    }

    if ( (error_info->resource_name != NULL) &&
         (error_info->resource_name_alloc_len > 0) )
    {
      mm_free(memory_pool,
              error_info->resource_name,
              error_info->resource_name_alloc_len);

      error_info->resource_name = NULL;

      error_info->resource_name_len = 0;

      error_info->resource_name_alloc_len = 0;
    }

    mm_free(memory_pool,
            error_info,
            sizeof(PCLXL_ERROR_INFO_STRUCT));
  }
  else
  {
    HqMemZero((uint8 *) error_info, (int)sizeof(PCLXL_ERROR_INFO_STRUCT));
  }
}

/**
 * \brief pclxl_copy_string() takes a copy of an existing uint8* array
 * ensuring that the copy is nul-terminated so that it can be used
 * as a C style string.
 *
 * It also returns the length of the string and
 * IFF the copy is in dynamically allocated space
 * it returns the space actually allocated
 * (which will be 1 byte longer than the original string)
 */

static uint8*
pclxl_copy_string(PCLXL_ERROR_INFO error_info,
                  uint8*           string,
                  size_t           string_len,
                  size_t*          copied_string_len,
                  size_t*          copied_string_alloc_len)
{
  uint8* new_string;

  if ( (error_info->pclxl_context == NULL) ||
       (error_info->pclxl_context->memory_pool == NULL) ||
       ((new_string = mm_alloc(error_info->pclxl_context->memory_pool,
                               (string_len + 1),
                               MM_ALLOC_CLASS_PCLXL_ERROR_INFO)) == NULL) )
  {
    /*
     * Unfortunately there was either no "memory pool"
     * to allocate a string from
     * Or the allocation from the non-NULL pool failed
     *
     * Either way, there is no point in trying to log an error
     * because we are in the middle of this process already
     *
     * So we are going to allocate a string from a pool of
     * statically declared string arrays
     */

    static uint8 static_strings[42][64];
    static uint32 next_static_string = 0;
    static size_t static_strings_len = NUM_ARRAY_ITEMS(static_strings);
    static size_t static_string_len = sizeof(static_strings[0][0]);

    new_string = static_strings[next_static_string];

    HqMemZero((uint8 *)new_string, (int)static_string_len);

    next_static_string = ((next_static_string + 1) % CAST_SIZET_TO_UINT32(static_strings_len));

    *copied_string_alloc_len = 0;

    *copied_string_len = (string_len < (static_string_len - 1) ?
                          string_len :
                          (static_string_len - 1));

    (void) memcpy(new_string,
                  string,
                  *copied_string_len);

    return new_string;
  }
  else
  {
    /*
     * We have successfully allocated space for exactly 1 byte longer than
     * the original string length so we can copy the string
     * and nul-terminate the string too
     */

    *copied_string_alloc_len = (string_len + 1);
    *copied_string_len = string_len;

    (void) memcpy(new_string,
                  string,
                  string_len);

    new_string[string_len] = '\0';

    return new_string;
  }

  /*NOTREACHED*/
}

/*
 * Removes (but does not delete/free) the top error info
 * from an error info list, leaving the list pointing at the remainder
 * of the list
 */

static PCLXL_ERROR_INFO
pclxl_pop_error_info(PCLXL_ERROR_INFO_LIST* error_info_list)
{
  HQASSERT((error_info_list != NULL), "Cannot pop error info from a NULL error info list");

  if ( *error_info_list != NULL )
  {
    PCLXL_ERROR_INFO popped_error_info = *error_info_list;

    *error_info_list = popped_error_info->prev_error_info;

    popped_error_info->prev_error_info = NULL;

    return popped_error_info;
  }
  else
  {
    (void) PCLXL_WARNING_HANDLER(pclxl_get_context(),
                                 PCLXL_SS_KERNEL,
                                 PCLXL_ERROR_INFO_LIST_ERROR,
                                 ("Tried to pop an error info from an empty error info list"));

    return NULL;
  }
}

/*
 * \brief pops and deletes *all* error (or warning) infos
 * from a caller-supplied list of error infos
 *
 * Post condition: error info list points to NULL
 */

void
pclxl_delete_error_info_list(PCLXL_ERROR_INFO_LIST* error_info_list)
{
  HQASSERT((error_info_list != NULL), "Cannot delete error infos from a NULL error info list");

  while ( *error_info_list != NULL )
  {
    pclxl_free_error_info(pclxl_pop_error_info(error_info_list));
  }
}

/*
 * pclxl_delete_warning_infos() deletes warnings from an error info list
 * leaving just the errors if any.
 *
 * The PCLXL spec. says that error(s) should be reported immediatedly
 * and this causes any warnings encountered/collected so far to be discarded
 * But any warnings are all reported at the end of the job.
 *
 * At the moment, whenever we push an *error* onto an error info list
 * we first purge the list of any warnings.
 *
 * But, given that the PCLXL specification mentions the concept of
 * *only* reporting errors, then maybe we could choose to retain all
 * the warnings and errors. In which case we would not need this function.
 */

static void
pclxl_delete_warning_infos(PCLXL_ERROR_INFO_LIST* error_info_list)
{
  HQASSERT((error_info_list != NULL), "Cannot delete error infos from a NULL error info list");

  if ( (*error_info_list != NULL) )
  {
    PCLXL_ERROR_INFO error_info = *error_info_list;

    switch ( error_info->error_type )
    {
    case PCLXL_ET_ERROR:

      /*
       * We retain any errors in this error info list
       * by actually clearing any warnings from *their* "next_error_info" list
       */

      pclxl_delete_warning_infos(&error_info->prev_error_info);

      break;

    case PCLXL_ET_WARNING:

      /*
       * We delete warnings by popping the warning from the error info list
       * and pointing the error info list at its "next_error_info"
       * and then clearing this remaining list of *its* warnings
       */

      pclxl_free_error_info(pclxl_pop_error_info(error_info_list));

      pclxl_delete_warning_infos(error_info_list);

      break;
    }
  }
}

/*
 * pclxl_push_error_info() prepends a new error (or warning) info struct
 * onto the beginning of a list of error infos.
 *
 * Note that this means that the list of errors/warnings is actually
 * in reverse order compared to their order of occurrence
 * And so we must allow for this when traversion the list.
 */

static PCLXL_ERROR_INFO
pclxl_push_error_info(PCLXL_ERROR_INFO error_info,
                      PCLXL_ERROR_INFO_LIST* error_info_list)
{
  HQASSERT((error_info != NULL), "Cannot push NULL error info into list");
  HQASSERT((error_info_list != NULL), "Cannot push error info into NULL error info list");
  HQASSERT((error_info != *error_info_list), "Cannot push error info onto stack again as this would corrupt the list");
  HQASSERT(((error_info->prev_error_info == NULL) ||
            (error_info->prev_error_info == *error_info_list)),
           "New error_info \"next\" error info already points somewhere else");

  if ( *error_info_list == NULL )
  {
    /*
     * The error info list is currently empty
     * so we can simply "push" this new error (or warning) info
     * into the list as the first entry
     */

    *error_info_list = error_info;

    return error_info;
  }
  else if ( (*error_info_list)->error_type == PCLXL_ET_ERROR )
  {
    /*
     * Unfortunately there is already an error in the error info list
     *
     * In this case we simply discard this new error (or warning) info
     */

    pclxl_free_error_info(error_info);

    return NULL;
  }
  else if ( error_info->error_type == PCLXL_ET_ERROR )
  {
    pclxl_delete_warning_infos(error_info_list);

    /*
     * This *should* leave the list empty
     * But we'll be careful to insert it into any non-empty list
     */

    error_info->prev_error_info = *error_info_list;

    *error_info_list = error_info;

    return error_info;
  }
  else
  {
    /*
     * This new error_info is actually a warning
     * But the existing list at least *starts* with a warning
     * So we simply insert this next warning
     */

    error_info->prev_error_info = *error_info_list;

    *error_info_list = error_info;

    return error_info;
  }

  /*NOTREACHED*/
}

/*
 * Regardless of where/how we obtained an error info struct
 * or whether or not we are going to hold a single instance
 * or add it into a list, we must now populate the fields in the structure
 *
 * But note that in RELEASE_BUILDs we only store
 * the pclxl_context, subsystem, error_type, error_code,
 * operator position and operator tag.
 * And for warnings only, the missing_font_name, substitute_font_name
 * and/or resource_name depending on the type of warning.
 *
 * In DEBUG_BUILDs we additionally store the "C" source file name
 * (typically as obtained from the __FILE__ preprocessor macro),
 * the source line number (typically from __LINE__) and a formatted
 * error/warning message
 */

PCLXL_ERROR_INFO
pclxl_record_basic_error_info(PCLXL_CONTEXT    pclxl_context,
                              PCLXL_SUBSYSTEM  subsystem,
                              PCLXL_ERROR_TYPE error_type,
                              int32            error_code)
{
  uint32*   operator_pos;
  PCLXLSTREAM* p_stream;
  PCLXL_ERROR_INFO error_info = NULL;

  if ( (pclxl_context != NULL) &&
       ((error_info = pclxl_allocate_error_info(pclxl_context)) != NULL) )
  {
    PCLXL_PARSER_CONTEXT parser_context = pclxl_context->parser_context;

    error_info->subsystem = subsystem;
    error_info->error_type = error_type;
    error_info->error_code = error_code;

    p_stream = pclxl_parser_current_stream(parser_context);

    /* Record tag of stream being interpreted */
    error_info->operator_tag = ((p_stream != NULL)
                                  ? pclxl_stream_op_tag(p_stream)
                                  : PCLXL_CHAR_Null);
    /* Record operator index for current stream stack */
    operator_pos = error_info->operator_pos;
    while ( p_stream != NULL ) {
      *operator_pos++ = pclxl_stream_op_counter(p_stream);
      p_stream = pclxl_parser_next_stream(parser_context, p_stream);
    }
    error_info->last_pos = operator_pos - 1;

    error_info = pclxl_push_error_info(error_info,
                                       &pclxl_context->error_info_list);
  }

  return error_info;
}

static PCLXL_ERROR_INFO
pclxl_record_font_substitution_warning_info(PCLXL_ERROR_INFO warning_info,
                                            uint8*           missing_font_name,
                                            size_t           missing_font_name_len,
                                            uint8*           substitute_font_name,
                                            size_t           substitute_font_name_len)
{
  if ( warning_info != NULL )
  {
    warning_info->missing_font_name =
      pclxl_copy_string(warning_info,
                        missing_font_name,
                        missing_font_name_len,
                        &warning_info->missing_font_name_len,
                        &warning_info->missing_font_name_alloc_len);

    warning_info->substitute_font_name =
      pclxl_copy_string(warning_info,
                        substitute_font_name,
                        substitute_font_name_len,
                        &warning_info->substitute_font_name_len,
                        &warning_info->substitute_font_name_alloc_len);
  }

  return warning_info;
}

static PCLXL_ERROR_INFO
pclxl_record_resource_not_removed_warning_info(PCLXL_ERROR_INFO warning_info,
                                               uint8*           resource_name,
                                               size_t           resource_name_len)
{
  if ( warning_info != NULL )
  {
    warning_info->resource_name =
      pclxl_copy_string(warning_info,
                        resource_name,
                        resource_name_len,
                        &warning_info->resource_name_len,
                        &warning_info->resource_name_alloc_len);
  }

  return warning_info;
}

PCLXL_ERROR_INFO
pclxl_record_font_name_error_info(PCLXL_ERROR_INFO error_info,
                                  uint8*           font_name,
                                  size_t           font_name_len)
{
  if ( error_info != NULL )
  {
    error_info->resource_name =
      pclxl_copy_string(error_info,
                        font_name,
                        font_name_len,
                        &error_info->resource_name_len,
                        &error_info->resource_name_alloc_len);
  }

  return error_info;
}

PCLXL_ERROR_INFO
pclxl_record_source_location(PCLXL_ERROR_INFO error_info,
                             char*            source_file,
                             unsigned long    source_line)
{
  if ( error_info != NULL )
  {
    error_info->source_file = source_file;
    error_info->source_line = source_line;
  }

  return error_info;
}

#ifdef DEBUG_BUILD

uint8*
pclxl_record_debug_error_info(PCLXL_ERROR_INFO error_info,
                              uint8*           formatted_text)
{
  if ( error_info != NULL )
  {
    error_info->formatted_text =
      pclxl_copy_string(error_info,
                        formatted_text,
                        strlen((char*) formatted_text),
                        &error_info->formatted_text_len,
                        &error_info->formatted_text_alloc_len);

    return error_info->formatted_text;
  }
  else
  {
    return formatted_text;
  }
}

static uint8*
pclxl_format_text(uint8* buffer,
                  size_t buflen,
                  uint8* format,
                  va_list params)
{
  size_t l;

  /*
   * Note that we reserve the last two bytes
   * of the buffer for an optional '\n' character
   * and a terminating '\0'
   *
   * This is so that we can know it is safe to use
   * the resultant formatted text as a C string
   * and that it will always end with at least one newline character
   */

  (void) vswncopyf(buffer,
                   ((int32) buflen - 2),
                   format,
                   params);

  /*
   * let's just ensure that the formatted debug string
   * ends with a '\n' (newline) character
   */

  if ( ((l = strlen((char*) buffer)) > 0) && /* It is safe to access buffer[(l - 1)] */
       (buffer[(l - 1)] != '\n') &&          /* The buffer does not already end in a newline */
       (l < (buflen - 2)) )                  /* There is room for (at least) one more character + terminating '\0' byte */
  {
    buffer[l++] = '\n';
    buffer[l] = '\0';
  }

  return buffer;
}

void
pclxl_log_error(PCLXL_CONTEXT pclxl_context,
                char*         source_file,
                uint32        source_line,
                uint8*        formatted_text)
{
  if ( (formatted_text != NULL) &&
       ((pclxl_context == NULL) ||
        (pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_ERRORS)) )
  {
    monitorf((uint8*) "\nError in file %s at line %d: %s\n",
             source_file,
             source_line,
             formatted_text);
  }
}

void
pclxl_log_warning(PCLXL_CONTEXT pclxl_context,
                  char*         source_file,
                  uint32        source_line,
                  uint8*        formatted_text)
{
  if ( (formatted_text != NULL) &&
       ((pclxl_context == NULL) ||
        (pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_ERRORS)) )
  {
    monitorf((uint8*) "\nWarning in file %s at line %d: %s\n",
             source_file,
             source_line,
             formatted_text);
  }
}

uint8*
pclxl_format_debug_message(char* format,
                           ...)
{
  /*
   * Yep, I know, formatting the message into a static array
   * and then returning the address of this static array to the caller
   * means that this code is non-re-entrant
   *
   * But it is a diagnostic/debug message
   * and I know that the use of/call to this function
   * is always tightly coupled with a call to pclxl_record_debug_error_info()
   * which immediately takes a copy of this static string
   * into some safer (typically dynamic) storage
   */

  static uint8 buffer[1000];

  va_list argp;

  va_start(argp, format);

  HqMemZero(buffer, (int)sizeof(buffer));

  (void) pclxl_format_text(buffer,
                           sizeof(buffer),
                           (uint8*) format,
                           argp);

  va_end(argp);

  return buffer;
}

#endif

/*
 * pclxl_match_info() returns TRUE if
 * an existing error (or warning) info matches any/all of the supplied criteria
 * including subsystem, error/warning code/type, operator tag, operator position,
 * resource name, missing font name or substitute font name
 */

static Bool
pclxl_match_info(PCLXL_ERROR_INFO ew_info,
                 char*            source_file,
                 uint32           source_line,
                 PCLXL_SUBSYSTEM  subsystem,
                 uint32           operator_pos,
                 PCLXL_TAG        operator_tag,
                 PCLXL_ERROR_TYPE error_type,
                 int32            error_code,
                 uint8*           resource_name,
                 size_t           resource_name_len,
                 uint8*           missing_font_name,
                 size_t           missing_font_name_len,
                 uint8*           substitute_font_name,
                 size_t           substitute_font_name_len)
{
  if (
       ((source_file == NULL) || (ew_info->source_file == source_file)) &&
       ((source_line == 0) || (ew_info->source_line == source_line)) &&
       ((subsystem == NULL) || (ew_info->subsystem == subsystem)) &&
       ((operator_pos == 0) || (ew_info->operator_pos[0] == operator_pos)) &&
       ((operator_tag == 0) || (ew_info->operator_tag == operator_tag)) &&
       ((error_type == 0) || (ew_info->error_type == error_type)) &&
       ((error_code == 0) || (ew_info->error_code == error_code)) &&
       ((resource_name_len == 0) ||
        (resource_name == NULL) ||
        ((resource_name_len == ew_info->resource_name_len) &&
         (!memcmp((void*) ew_info->resource_name, (void*) resource_name, resource_name_len)))) &&
       ((missing_font_name_len == 0) ||
        (missing_font_name == NULL) ||
        ((missing_font_name_len == ew_info->missing_font_name_len) &&
         (!memcmp((void*) ew_info->missing_font_name, (void*) missing_font_name, missing_font_name_len)))) &&
       ((substitute_font_name_len == 0) ||
        (substitute_font_name == NULL) ||
        ((substitute_font_name_len == ew_info->substitute_font_name_len) &&
         (!memcmp((void*) ew_info->substitute_font_name, (void*) substitute_font_name, substitute_font_name_len))))
     )
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

static PCLXL_ERROR_INFO
pclxl_get_first_error_info(PCLXL_ERROR_INFO_LIST error_info_list)
{
  PCLXL_ERROR_INFO error_info = NULL;

  error_info = (PCLXL_ERROR_INFO) error_info_list;

  while ( (error_info != NULL) &&
          (error_info->prev_error_info != NULL) )
  {
    error_info = error_info->prev_error_info;
  }

  return error_info;
}

static PCLXL_ERROR_INFO
pclxl_get_next_error_info(PCLXL_ERROR_INFO_LIST error_info_list,
                          PCLXL_ERROR_INFO prev_error_info)
{
  PCLXL_ERROR_INFO next_error_info = NULL;

  HQASSERT((error_info_list != NULL), "Cannot get next error info from a NULL error info list");
  HQASSERT((prev_error_info != NULL), "Cannot get next error info when previous error info is NULL");

  next_error_info = (PCLXL_ERROR_INFO) error_info_list;

  while ( (next_error_info != NULL) &&
          (next_error_info->prev_error_info != NULL) &&
          (next_error_info->prev_error_info != prev_error_info) )
  {
    next_error_info = next_error_info->prev_error_info;
  }

  if ( (next_error_info != NULL) &&
       (next_error_info->prev_error_info == prev_error_info) )
  {
    return next_error_info;
  }
  else
  {
    return NULL;
  }
}

static uint32
pclxl_count_warnings(PCLXL_ERROR_INFO_LIST error_info_list,
                     char*                 source_file,
                     uint32                source_line,
                     PCLXL_SUBSYSTEM       subsystem,
                     uint32                operator_pos,
                     PCLXL_TAG             operator_tag,
                     PCLXL_ERROR_TYPE      error_type,
                     int32                 error_code,
                     uint8*                resource_name,
                     size_t                resource_name_len,
                     uint8*                missing_font_name,
                     size_t                missing_font_name_len,
                     uint8*                substitute_font_name,
                     size_t                substitute_font_name_len)
{
  PCLXL_ERROR_INFO ew_info = NULL;
  uint32 c = 0;

  for ( c = 0, ew_info = pclxl_get_first_error_info(error_info_list) ;
        ew_info != NULL ;
        ew_info = pclxl_get_next_error_info(error_info_list, ew_info) )
  {
    if ( pclxl_match_info(ew_info,
                          source_file,
                          source_line,
                          subsystem,
                          operator_pos,
                          operator_tag,
                          error_type,
                          error_code,
                          resource_name,
                          resource_name_len,
                          missing_font_name,
                          missing_font_name_len,
                          substitute_font_name,
                          substitute_font_name_len) )
    {
      c++;
    }
  }

  return c;
}

static void
pclxl_append_printable_resource_name(uint8* formatted_text,
                                     size_t formatted_text_max_len,
                                     uint8* resource_name,
                                     size_t resource_name_len,
                                     Bool   add_quotes,
                                     Bool   add_leading_0x)
{
  size_t i = 0;
  size_t l = strlen((char*) formatted_text);
  size_t unprintable_char_count = 0;

  UNUSED_PARAM(size_t, formatted_text_max_len);

  for ( i = 0, unprintable_char_count = 0 ;
        i < resource_name_len ;
        unprintable_char_count += (isprint(resource_name[i]) ? 0 : 1), i++ ) ;

  if ( unprintable_char_count > 0 )
  {
    if ( (add_leading_0x) &&
         (l < (formatted_text_max_len - 2)) )
    {
      (void) strcat((char*) formatted_text, "0x");
      l += 2;
    }

    for ( i = 0 ;
          ((i < resource_name_len) &&
           (i < 120) &&   /* We are not interested in gathering the
                           * entire value of *very* long resource/font names
                           */
           (l < (formatted_text_max_len - 2))) ;
          i++ )
    {
      static char hex_digits[] = "0123456789abcdef";

      formatted_text[l++] = hex_digits[(resource_name[i] >> 4)];
      formatted_text[l++] = hex_digits[(resource_name[i] & 0x0f)];
      formatted_text[l] = '\0';
    }
  }
  else
  {
    size_t rtml = (formatted_text_max_len - l - 2); /* the "2" is to allow for
                                                     * the double quotes
                                                     */
    size_t capped_rtml = ((rtml > 250) ? 250 : rtml);
    size_t resource_bytes_to_copy = ((resource_name_len < capped_rtml) ? resource_name_len : capped_rtml);

    if ( (add_quotes) &&
         (l < formatted_text_max_len) )
    {
      formatted_text[l++] = '"';
      formatted_text[l] = '\0';
    }

    (void) memcpy((char*) &formatted_text[l],
                  (char*) resource_name,
                  resource_bytes_to_copy);

    l += resource_bytes_to_copy;

    formatted_text[l] = '\0';

    if ( (add_quotes) &&
         (l < formatted_text_max_len) )
    {
      formatted_text[l++] = '"';
      formatted_text[l] = '\0';
    }
  }
}

int32
pclxl_font_substitution_warning(char*           source_file,
                                unsigned long   source_line,
                                PCLXL_CONTEXT   pclxl_context,
                                PCLXL_SUBSYSTEM subsystem,
                                uint8*          missing_font_name,
                                size_t          missing_font_name_len,
                                uint8*          substitute_font_name,
                                size_t          substitute_font_name_len)
{

#ifdef DEBUG_BUILD

  uint8 formatted_text[1000];

  static char substituted_for[] = " substituted for ";

  /*
   * We now need to format the supplied
   * missing and substitute font names
   * into a warning message string
   *
   * Note that we must watch out for non-printable font names
   */

  formatted_text[0] = '\0';

  pclxl_append_printable_resource_name(formatted_text,
                                       (sizeof(formatted_text) - 300),  /* To guarantee some space for
                                                                         * a) the other font name
                                                                         * b) the " substituted for " text
                                                                         * c) the trailing newline
                                                                         */
                                       substitute_font_name,
                                       substitute_font_name_len,
                                       TRUE,
                                       TRUE);

  if ( strlen((char*) formatted_text) < (sizeof(formatted_text) - sizeof(substituted_for)) )
  {
    (void) strcat((char*) formatted_text, substituted_for);
  }

  pclxl_append_printable_resource_name(formatted_text,
                                       (sizeof(formatted_text) - 1),  /* To guarantee space for a trailing new line */
                                       missing_font_name,
                                       missing_font_name_len,
                                       TRUE,
                                       TRUE);

  if ( strlen((char*) formatted_text) < (sizeof(formatted_text) - 1) )
  {
    (void) strcat((char*) formatted_text, "\n");
  }

#endif

  if ( (pclxl_context) &&
       (pclxl_count_warnings(pclxl_context->error_info_list,
                             NULL,        /* source_file */
                             0,           /* source_line */
                             NULL,        /* subsystem */
                             0,           /* operator_pos */
                             0,           /* operator_tag */
                             PCLXL_ET_WARNING,
                             PCLXL_FONT_SUBSTITUTED_BY_FONT,
                             NULL,        /* resource_name */
                             0,           /* resource_name_len */
                             missing_font_name,
                             missing_font_name_len,
                             substitute_font_name,
                             substitute_font_name_len) == 0) &&
       (pclxl_count_warnings(pclxl_context->error_info_list,
                             NULL,        /* source_file */
                             0,           /* source_line */
                             NULL,        /* subsystem */
                             0,           /* operator_pos */
                             0,           /* operator_tag */
                             PCLXL_ET_WARNING,
                             PCLXL_FONT_SUBSTITUTED_BY_FONT,
                             NULL,        /* resource_name */
                             0,           /* resource_name_len */
                             NULL,        /* missing_font_name */
                             0,           /* missing_font_name_len */
                             NULL,        /* substitute_font_name */
                             0            /* substitute_font_name_len */) < 20)
     )
  {
    /*
     * IFF there is somewhere to record this warning
     * (i.e. there is a non-NULL PCLXL_CONTEXT and thus
     *  a non-NULL error_info_list)
     * AND we haven't already logged this particular font susbtitution before
     * AND we haven't already logged 20 other font substitution warnings
     *
     * Then we allocate a PCLXL_ERROR_INFO structure instance
     * which we complete with this error information and then
     * "push" into the error_info_list.
     *
     * Note that the following allocate *cannot* fail
     * Because, in the event of a dynamic memory allocation failure
     * (or indeed in the event of a NULL PCLXL_CONTEXT),
     * then a PCLXL_ERROR_INFO is allocated from a static pool (array) of them
     */

    PCLXL_ERROR_INFO warning_info =
      pclxl_record_basic_error_info(pclxl_context,
                                    subsystem,
                                    PCLXL_ET_WARNING,
                                    PCLXL_FONT_SUBSTITUTED_BY_FONT);

    (void) pclxl_record_source_location(warning_info,
                                        source_file,
                                        source_line);

#ifdef DEBUG_BUILD

    (void) pclxl_record_debug_error_info(warning_info,
                                         formatted_text);

#endif

    (void) pclxl_record_font_substitution_warning_info(warning_info,
                                                       missing_font_name,
                                                       missing_font_name_len,
                                                       substitute_font_name,
                                                       substitute_font_name_len);
  }

#ifdef DEBUG_BUILD

  pclxl_log_warning(pclxl_context, source_file, source_line, formatted_text);

#endif

  return PCLXL_FONT_SUBSTITUTED_BY_FONT;
}

static uint8*
pclxl_error_code_string(int32 error_code,
                        uint8* error_string_buffer,
                        size_t error_string_buflen);

int32
pclxl_resource_not_removed_warning(char*           source_file,
                                   unsigned long   source_line,
                                   PCLXL_CONTEXT   pclxl_context,
                                   PCLXL_SUBSYSTEM subsystem,
                                   int32           warning_code,
                                   uint8*          resource_name,
                                   size_t          resource_name_len)
{

#ifdef DEBUG_BUILD

  uint8 formatted_text[1000];
  uint8* warning_name;

  static char space_dash_space[] = " - ";

  /*
   * We now need to format the supplied
   * missing and substitute font names
   * into a warning message string
   *
   * Note that we must watch out for non-printable font names
   */

  if ( (warning_name = pclxl_error_code_string(warning_code,
                                               formatted_text,
                                               sizeof(formatted_text))) != formatted_text )
  {
    (void) strcpy((char*) formatted_text, (char*) warning_name);
  }

  if ( strlen((char*) formatted_text) < (sizeof(formatted_text) - sizeof(space_dash_space)) )
  {
    (void) strcat((char*) formatted_text, space_dash_space);
  }


  pclxl_append_printable_resource_name(formatted_text,
                                       (sizeof(formatted_text) - 1),  /* To guarantee space for trailing '\n' */
                                       resource_name,
                                       resource_name_len,
                                       TRUE,
                                       TRUE);

  if ( strlen((char*) formatted_text) < (sizeof(formatted_text) - 1) )
  {
    (void) strcat((char*) formatted_text, "\n");
  }

#endif

  if ( (pclxl_context) &&
       (pclxl_count_warnings(pclxl_context->error_info_list,
                             NULL,        /* source_file */
                             0,           /* source_line */
                             NULL,        /* subsystem */
                             0,           /* operator_pos */
                             0,           /* operator_tag */
                             PCLXL_ET_WARNING,
                             warning_code,
                             resource_name,
                             resource_name_len,
                             NULL,        /* missing_font_name */
                             0,           /* missing_font_name_len */
                             NULL,        /* substitute_font_name */
                             0            /* substitute_font_name_len */) == 0) &&
       (pclxl_count_warnings(pclxl_context->error_info_list,
                             NULL,        /* source_file */
                             0,           /* source_line */
                             NULL,        /* subsystem */
                             0,           /* operator_pos */
                             0,           /* operator_tag */
                             PCLXL_ET_WARNING,
                             warning_code,
                             NULL,        /* resource_name */
                             0,           /* resource_name_len */
                             NULL,        /* missing_font_name */
                             0,           /* missing_font_name_len */
                             NULL,        /* substitute_font_name */
                             0            /* substitute_font_name_len */) < 2)
     )
  {
    /*
     * If there is somewhere to record this warning
     * (i.e. there is a non-NULL PCLXL_CONTEXT and thus
     *  a non-NULL error_info_list)
     * AND we haven't logged this particular warning
     * about this particular resource before
     * And we haven't logged more that 2 of this warning already
     *
     * Then we allocate a PCLXL_ERROR_INFO structure instance
     * which we complete with this error information and then
     * "push" into the error_info_list.
     *
     * Note that the following allocate *cannot* fail
     * Because, in the event of a dynamic memory allocation failure
     * (or indeed in the event of a NULL PCLXL_CONTEXT),
     * then a PCLXL_ERROR_INFO is allocated from a static pool (array) of them
     */

    PCLXL_ERROR_INFO warning_info =
      pclxl_record_basic_error_info(pclxl_context,
                                    subsystem,
                                    PCLXL_ET_WARNING,
                                    warning_code);

    (void) pclxl_record_source_location(warning_info,
                                        source_file,
                                        source_line);

#ifdef DEBUG_BUILD

    (void) pclxl_record_debug_error_info(warning_info,
                                         formatted_text);

#endif

    (void) pclxl_record_resource_not_removed_warning_info(warning_info,
                                                          resource_name,
                                                          resource_name_len);
  }

#ifdef DEBUG_BUILD

  pclxl_log_warning(pclxl_context, source_file, source_line, formatted_text);

#endif

  return warning_code;
}

static
struct pclxl_error_code_to_string {
  int32   error_code;
  uint8*  error_string;
} pclxl_error_codes_to_strings[] = {
  { PCLXL_SUCCESS, (uint8*) "Success" },
  { PCLXL_EOF, (uint8*) "EOF" },
  { PCLXL_PREMATURE_EOF, (uint8*) "PrematureEOF" },
  { PCLXL_INSUFFICIENT_MEMORY, (uint8*) "InsufficientMemory" },
  { PCLXL_UNSUPPORTED_BINDING, (uint8*) "UnsupportedBinding" },
  { PCLXL_UNSUPPORTED_CLASS_NAME, (uint8*) "UnsupportedClassName" },
  { PCLXL_UNSUPPORTED_PROTOCOL_VERSION, (uint8*) "UnsupportedProtocol" },
  { PCLXL_ILLEGAL_STREAM_HEADER, (uint8*) "IllegalStreamHeader" },
  { PCLXL_SCAN_DATA_TYPE_VALUE_FAILED, (uint8*) "ScanDataTypeValueError" },
  { PCLXL_SCAN_INVALID_UEL, (uint8*) "IllegalUEL" },

  { PCLXL_CONTEXT_STACK_ERROR, (uint8*) "PCLXL Context Stack Error" },
  { PCLXL_GRAPHICS_STATE_STACK_ERROR, (uint8*) "Graphics State Stack Error" },
  { PCLXL_PARSER_CONTEXT_STACK_ERROR, (uint8*) "Parser Context Stack Error" },
  { PCLXL_ATTRIBUTE_LIST_ERROR, (uint8*) "Attribute List Error" },
  { PCLXL_ERROR_INFO_LIST_ERROR, (uint8*) "Error Info List Error" },

  { PCLXL_TAG_NOT_USED, (uint8*) "Tag Not Used" },
  { PCLXL_TAG_RESERVED_FOR_FUTURE_USE, (uint8*) "Tag Reserved For Future Use" },
  { PCLXL_TAG_NOT_YET_IMPLEMENTED, (uint8*) "Tag Not Yet Implemented" },
  { PCLXL_TAG_HANDLED_ELSEWHERE, (uint8*) "Tag Handled Elsewhere" },

  { PCLXL_ILLEGAL_OPERATOR_SEQUENCE, (uint8*) "IllegalOperatorSequence" },
  { PCLXL_ILLEGAL_TAG, (uint8*) "IllegalTag" },
  { PCLXL_INTERNAL_OVERFLOW, (uint8*) "InternalOverflow" },
  { PCLXL_ILLEGAL_ARRAY_SIZE, (uint8*) "IllegalArraySize" },
  { PCLXL_ILLEGAL_ATTRIBUTE, (uint8*) "IllegalAttribute" },
  { PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION, (uint8*) "IllegalAttributeCombination" },
  { PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE, (uint8*) "IllegalAttributeDataType" },
  { PCLXL_ILLEGAL_ATTRIBUTE_VALUE, (uint8*) "IllegalAttributeValue" },
  { PCLXL_MISSING_ATTRIBUTE, (uint8*) "MissingAttribute" },

  { PCLXL_CURRENT_CURSOR_UNDEFINED, (uint8*) "NoCurrentPoint" },

  { PCLXL_DATA_SOURCE_NOT_OPEN, (uint8*) "DataSourceNotOpen" },
  { PCLXL_DATA_SOURCE_EXCESS_DATA, (uint8*) "ExtraData" },
  { PCLXL_ILLEGAL_DATA_LENGTH, (uint8*) "IllegalDataLength" },
  { PCLXL_ILLEGAL_DATA_VALUE, (uint8*) "IllegalDataValue" },
  { PCLXL_MISSING_DATA, (uint8*) "MissingData" },
  { PCLXL_DATA_SOURCE_NOT_CLOSED, (uint8*) "DataSourceNotClosed" },

  { PCLXL_IMAGE_PALETTE_MISMATCH, (uint8*) "ImagePaletteMismatch" },
  { PCLXL_PALETTE_UNDEFINED, (uint8*) "PaletteUndefined" },

  { PCLXL_ILLEGAL_MEDIA_SIZE, (uint8*) "IllegalMediaSize" },
  { PCLXL_ILLEGAL_MEDIA_SOURCE, (uint8*) "IllegalMediaSource" },
  { PCLXL_ILLEGAL_MEDIA_DESTINATION, (uint8*) "IllegalMediaDestination" },
  { PCLXL_ILLEGAL_ORIENTATION, (uint8*) "IllegalOrientation" },

  { PCLXL_MAX_GS_LEVELS_EXCEEDED, (uint8*) "MaxGSLevelsExceeded" },
  { PCLXL_STREAM_UNDEFINED, (uint8*) "StreamUndefined" },
  { PCLXL_COLOR_SPACE_MISMATCH, (uint8*) "ColorSpaceMismatch" },
  { PCLXL_BAD_PATTERN_ID, (uint8*) "BadPatternID" },
  { PCLXL_CLIP_MODE_MISMATCH, (uint8*) "ClipModeMismatch" },

  { PCLXL_CANNOT_REPLACE_CHARACTER, (uint8*) "CannotReplaceCharacter" },
  { PCLXL_FONT_UNDEFINED, (uint8*) "FontUndefined" },
  { PCLXL_FONT_NAME_ALREADY_EXISTS, (uint8*) "FontNameAlreadyExists" },
  { PCLXL_FST_MISMATCH, (uint8*) "FSTMismatch" },
  { PCLXL_UNSUPPORTED_CHARACTER_CLASS, (uint8*) "UnsupportedCharacterClass" },
  { PCLXL_UNSUPPORTED_CHARACTER_FORMAT, (uint8*) "UnsupportedCharacterFormat" },
  { PCLXL_ILLEGAL_CHARACTER_DATA, (uint8*) "IllegalCharacterData" },
  { PCLXL_ILLEGAL_FONT_DATA, (uint8*) "IllegalFontData" },
  { PCLXL_ILLEGAL_FONT_HEADER_FIELDS, (uint8*) "IllegalFontHeaderFiles" },
  { PCLXL_ILLEGAL_NULL_SEGMENT_SIZE, (uint8*) "IllegalNullSegmentSize" },
  { PCLXL_ILLEGAL_FONT_SEGMENT, (uint8*) "IllegalFontSegment" },
  { PCLXL_MISSING_REQUIRED_SEGMENT, (uint8*) "MissingRequiredSegment" },
  { PCLXL_ILLEGAL_GLOBAL_TRUE_TYPE_SEGMENT, (uint8*) "IllegalGlobalTrueTypeSegment" },
  { PCLXL_ILLEGAL_GALLEY_CHARACTER_SEGMENT, (uint8*) "IllegalGalleyCharacterSegment" },
  { PCLXL_ILLEGAL_VERTICAL_TX_SEGMENT, (uint8*) "IllegalVerticalTxSegment" },
  { PCLXL_ILLEGAL_BITMAP_RESOLUTION_SEGMENT, (uint8*) "IllegalBitmapResolutionSegment" },
  { PCLXL_UNDEFINED_FONT_NOT_REMOVED, (uint8*) "UndefinedFontNotRemoved" },
  { PCLXL_INTERNAL_FONT_NOT_REMOVED, (uint8*) "InternalFontNotRemoved" },
  { PCLXL_MASS_STORAGE_FONT_NOT_REMOVED, (uint8*) "MassStorageFontNotRemoved" },
  { PCLXL_NO_CURRENT_FONT, (uint8*) "NoCurrentFont" },
  { PCLXL_BAD_FONT_DATA, (uint8*) "BadFontData" },
  { PCLXL_FONT_UNDEFINED_NO_SUBSTITUTE_FOUND, (uint8*) "FontUndefinedNoSubstituteFound" },
  { PCLXL_FONT_SUBSTITUTED_BY_FONT, (uint8*) "FontSubstitutedByFont" },
  { PCLXL_SYMBOL_SET_REMAP_UNDEFINED, (uint8*) "SymbolSetRemapUndefined" },

  { PCLXL_STREAM_NESTING_FULL, (uint8*) "StreamNestingFull" },
  { PCLXL_STREAM_NESTING_ERROR, (uint8*) "StreamNestingError" },
  { PCLXL_STREAM_ALREADY_RUNNING, (uint8*) "StreamAlreadyRunning" },
  { PCLXL_STREAM_CALLING_ITSELF, (uint8*) "StreamCallingItself" },
  { PCLXL_INTERNAL_STREAM_ERROR, (uint8*) "InternalStreamError" },
  { PCLXL_UNDEFINED_STREAM_NOT_REMOVED, (uint8*) "UndefinedStreamNotRemoved" },
  { PCLXL_INTERNAL_STREAM_NOT_REMOVED, (uint8*) "InternalStreamNotRemoved" },
  { PCLXL_MASS_STORAGE_STREAM_NOT_REMOVED, (uint8*) "MassStorageStreamNotRemoved" },

  /* Special error codes to control printing of known operator names when they
   * are not known for a stream class */
  { PCLXL_ILLEGAL_OPERATOR_TAG, (uint8*) "IllegalTag" },

  /* Note that we explicitly suppress [PCLXL_]INTERNAL_ERROR from
   * appearing in this table so that we force the error handling code
   * to go down its internal route for displaying internal errors {
   * PCLXL_INTERNAL_ERROR, (uint8*) "Internal Error" },
   */
};

static size_t pclxl_error_codes_to_strings_len = NUM_ARRAY_ITEMS(pclxl_error_codes_to_strings);

static uint8*
pclxl_error_code_string(int32 error_code,
                        uint8* error_string_buffer,
                        size_t error_string_buflen)
{
  size_t i = 0;

  for ( i = 0 ; i < pclxl_error_codes_to_strings_len ; i++ )
  {
    if ( error_code == pclxl_error_codes_to_strings[i].error_code ) return pclxl_error_codes_to_strings[i].error_string;
  }

  (void) swncopyf(error_string_buffer,
                  (int32) error_string_buflen,
                  (uint8*) "InternalError 0x%08x",
                  error_code);

  return error_string_buffer;
}

uint8* pclxl_subsystem_strings[PCLXL_NUM_SUBSYSTEMS] =
{
  (uint8*) "UNKNOWN",
  (uint8*) "KERNEL",
  (uint8*) "TEXT",
  (uint8*) "IMAGE",
  (uint8*) "JPEG",
  (uint8*) "STATE",
  (uint8*) "SCANLINE",
  (uint8*) "VECTOR",
  (uint8*) "USERSTREAM",
  (uint8*) "PATMGR",
  (uint8*) "RASTER"
};

static uint8*
pclxl_subsystem_string(PCLXL_ERROR_INFO error_info,
                       uint8* subsystem_buffer,
                       size_t subsystem_buflen)
{
  size_t i;

  UNUSED_PARAM(uint8*, subsystem_buffer);
  UNUSED_PARAM(size_t, subsystem_buflen);

#ifdef DEBUG_BUILD

  if ( (error_info->pclxl_context != NULL) &&
       (error_info->pclxl_context->config_params.debug_pclxl & PCLXL_DEBUG_SUBSYSTEM) &&
       (error_info->source_file != NULL) )
  {
    /*
     * We have a PCLXL_CONTEXT
     * and the PCLXL_DEBUG_SUBSYSTEM switch is on
     * and we have a non-NULL source file.
     *
     * Therefore, rather than displaying the relatively meaningless
     * but HP-compatible "subsystem name"
     * we will actually display the <filename>:<line_number>
     * because it represents much more useful information
     */

    char*  last_path_sep = (strrchr(error_info->source_file, '\\') ?
                            strrchr(error_info->source_file, '\\') :
                            strrchr(error_info->source_file, '/'));
    char*  file_name = (last_path_sep ?
                        last_path_sep + 1 :
                        error_info->source_file);

    /*
     * I am not sure whether we want to remove the file extension
     * (which is presumably ".c") or not
    char*  last_dot = strrchr(file_name, '.');
    size_t n = (last_dot ? (last_dot - file_name) : strlen(file_name));
     */

    (void) swncopyf(subsystem_buffer,
                    (int32) subsystem_buflen,
                    (uint8*) "%s:%d",
                    file_name,
                    error_info->source_line);

    return subsystem_buffer;
  }

#endif

  for ( i = 0 ;
        i < NUM_ARRAY_ITEMS(pclxl_subsystem_strings) ;
        i++ )
  {
    if ( error_info->subsystem == pclxl_subsystem_strings[i] )
    {
      /*
       * This is a new subsystem "enumeration"
       * which we use to index an array of statically declared strings
       */

      return pclxl_subsystem_strings[i];
    }
  }
  return pclxl_subsystem_strings[1]; /* pclxl_subsystem_strings[0]; */
}

static Bool
pclxl_write_to_back_channel(FILELIST* back_channel_stream,
                            uint8*    nul_terminated_string)
{
  if ( !file_write(back_channel_stream,
                   nul_terminated_string,
                   (int32) strlen((char*) nul_terminated_string)) )
  {
    HQFAIL("Failed to write to \"BackChannel\"");

#ifdef DEBUG_BUILD

    monitorf((uint8*) "%s", nul_terminated_string);

#endif

    return FALSE;
  }
  else
  {
    return TRUE;
  }
}

static void
pclxl_report_to_back_channel(PCLXL_CONTEXT pclxl_context,
                             Bool          include_warnings)
{
  uint32* operator_pos;
  uint8 buffer[1000];
  uint8 subsystem_buffer[64];
  uint8 hex_error_string[64];

  OBJECT backchannel_filename = OBJECT_NOTVM_STRING("%embedded%/BackChannel");
  OBJECT back_channel_file = OBJECT_NOTVM_NOTHING;

  FILELIST* back_channel_stream = NULL;

  if ( !ps_string(&backchannel_filename,
                  pclxl_context->config_params.backchannel_filename,
                  (int32) strlen((char*) pclxl_context->config_params.backchannel_filename)) )
  {
    HQFAIL("Failed to construct \"BackChannel\" file name");
  }
  else if ( !file_open(&backchannel_filename,
                       SW_WRONLY,  /* Open for write */
                       WRITE_FLAG, /* Same goes for the Postscript flags */
                       FALSE,      /* The "BackChannel" device does not support seeking to the end of the file */
                       0,          /* I have no idea what <baseflag> means, but 0 (zero) seems to work */
                       &back_channel_file) )
  {
    HQFAIL("Failed to open the \"BackChannel\"");
  }
  else if ( (back_channel_stream = oFile(back_channel_file)) == NULL )
  {
    /*
     * We failed to obtain the stream associated with this (open) file (object)
     */

    HQFAIL("Failed to obtain FILELIST* (i.e. stream) associated with the \"BackChannel\"");

    (void) file_close(&back_channel_file);
  }
  else if ( !pclxl_write_to_back_channel(back_channel_stream,
                                         (uint8*) "PCL XL error\n\n") )
  {
    /*
     * We failed to write the opening "PCL XL error" to the back channel stream
     * So there is little chance of writing the error/warning infos
     * So we give up, but remember to close the stream/file
     */

    (void) file_close(&back_channel_file);
  }
  else
  {
    PCLXL_ERROR_INFO error_info;

    for ( error_info = pclxl_get_first_error_info(pclxl_context->error_info_list) ;
          error_info != NULL ;
          error_info = pclxl_get_next_error_info(pclxl_context->error_info_list,
                                                 error_info) )
    {
      if ( error_info->error_type == PCLXL_ET_ERROR ) {
        Bool include_font_name = (((error_info->error_code <= PCLXL_FONT_UNDEFINED) &&
                                   (error_info->error_code >= PCLXL_ILLEGAL_CHARACTER_DATA)) ||
                                  ((error_info->error_code <= PCLXL_UNDEFINED_FONT_NOT_REMOVED) &&
                                   (error_info->error_code >= PCLXL_SYMBOL_SET_REMAP_UNDEFINED)));

        uint8 font_name[64];

        if ( include_font_name )
        {
          HqMemZero((uint8 *)font_name, sizeof(font_name));

          pclxl_append_printable_resource_name(font_name,
                                               sizeof(font_name),
                                               error_info->resource_name,
                                               error_info->resource_name_len,
                                               FALSE,
                                               TRUE);
        }

        (void) swncopyf(buffer,
                        sizeof(buffer),
                        (uint8*) "\n"
                                 "        Subsystem:  %s\n\n"
                                 "        Error:      %s%s%s\n\n"
                                 "        Operator:   %s\n\n"
                                 "        Position:   ",
                        pclxl_subsystem_string(error_info,
                                               subsystem_buffer,
                                               sizeof(subsystem_buffer)),
                        pclxl_error_code_string(error_info->error_code,
                                                hex_error_string,
                                                sizeof(hex_error_string)),
                        (include_font_name ? " - " : ""),
                        (include_font_name ? font_name : (uint8*) ""),
                        pclxl_error_get_tag_string(error_info->operator_tag, error_info->error_code));
        (void) pclxl_write_to_back_channel(back_channel_stream, buffer);

        /* Report operator position allowing for nested streams */
        operator_pos = error_info->last_pos;
        swncopyf(buffer, sizeof(buffer), (uint8*)"%d", *operator_pos);
        (void)pclxl_write_to_back_channel(back_channel_stream, buffer);
        /* HP only report the first two stream operator indexes - change if to a
         * while to report the whole stack
         */
        if ( operator_pos != error_info->operator_pos ) {
          swncopyf(buffer, sizeof(buffer), (uint8*)";%d", *(--operator_pos));
          (void)pclxl_write_to_back_channel(back_channel_stream, buffer);
        }
        (void)pclxl_write_to_back_channel(back_channel_stream, (uint8*)"\n");

      }
      else if ( include_warnings ) {
        switch ( error_info->error_code ) {

        case PCLXL_FONT_SUBSTITUTED_BY_FONT:

          {
            uint8 substitute_font_name[64];
            uint8 missing_font_name[64];

            HqMemZero((uint8 *)substitute_font_name, (int)sizeof(substitute_font_name));

            pclxl_append_printable_resource_name(substitute_font_name,
                                                 sizeof(substitute_font_name),
                                                 error_info->substitute_font_name,
                                                 error_info->substitute_font_name_len,
                                                 FALSE,
                                                 TRUE);

            HqMemZero((uint8 *)missing_font_name, (int)sizeof(missing_font_name));

            pclxl_append_printable_resource_name(missing_font_name,
                                                 sizeof(missing_font_name),
                                                 error_info->missing_font_name,
                                                 error_info->missing_font_name_len,
                                                 FALSE,
                                                 TRUE);

            (void) swncopyf(buffer,
                            sizeof(buffer),
                            (uint8*) "        Warning:    %-16.16s substituted for %s\n",
                            substitute_font_name,
                            missing_font_name);
          }
          (void) pclxl_write_to_back_channel(back_channel_stream, buffer);

          break;

        case PCLXL_UNDEFINED_FONT_NOT_REMOVED:
        case PCLXL_INTERNAL_FONT_NOT_REMOVED:
        case PCLXL_MASS_STORAGE_FONT_NOT_REMOVED:

          {
            uint8 resource_name[64];

            HqMemZero((uint8 *)resource_name, (int)sizeof(resource_name));

            pclxl_append_printable_resource_name(resource_name,
                                                 sizeof(resource_name),
                                                 error_info->resource_name,
                                                 error_info->resource_name_len,
                                                 FALSE,
                                                 TRUE);

            (void) swncopyf(buffer,
                            sizeof(buffer),
                            (uint8*) "        Warning: %s - %s\n",
                            pclxl_error_code_string(error_info->error_code,
                                                    hex_error_string,
                                                    sizeof(hex_error_string)),
                            resource_name);
          }
          (void) pclxl_write_to_back_channel(back_channel_stream, buffer);

          break;

        default:

          (void) swncopyf(buffer,
                          sizeof(buffer),
                          (uint8*) "\n"
                                   "          Warning:    %s\n\n",
                          pclxl_error_code_string(error_info->error_code,
                                                  hex_error_string,
                                                  sizeof(hex_error_string)));

          (void) pclxl_write_to_back_channel(back_channel_stream, buffer);

          break;
        }
      }
    }

    (void) pclxl_write_to_back_channel(back_channel_stream, (uint8*) "\n");

    (void) file_close(&back_channel_file);
  }

  /*NOTREACHED*/
}

/**
 * \brief pclxl_report_to_error_page() aborts (i.e. erases) the current page,
 * if any and aborts (i.e. silently ends) the current session, if any.
 * And then begins a new session and new page
 * and outputs each error info and, if also requested, each warning info
 *
 * Note that we are either expecting a single error info
 * or 1 or more warning infos.
 * But the algorithm attempts to output them all regardless
 */

static void
pclxl_report_to_error_page(PCLXL_CONTEXT pclxl_context,
                           Bool          include_warnings)
{
  uint32* operator_pos;
  /*
   * The first job is to re-set the page device
   * (possibly also selecting the default paper size)
   *
   * And then erase any current page content and begin a new page
   *
   * We cannot alter the device resolution
   * but we *can* choose our own user units
   *
   * We will also choose a 10-point version of the appropriate font
   * (probably "Line Printer    " or "Courier         ")
   */

  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_NON_GS_STATE   non_gs_state = &pclxl_context->non_gs_state;

  PCLXL_ERROR_INFO error_info = NULL;

  /*
   * We need to pick some default font name, point size
   * and symbol set to use for the text of our error page
   *
   * We *could* use the config params defaults
   * but this could result in the user selecting
   * some defaults that break our error page
   *
   * So we prefer to use some hard-coded (sensible) values thus:
   */

/* #define ERROR_PAGE_FONT_PCLXL_COURIER 1 */

#ifdef ERROR_PAGE_FONT_PCLXL_COURIER

  uint8*       error_page_font = (uint8*) "Courier         ";
  PCLXL_SysVal error_page_point_size = 8.5;
  uint32       error_page_symbol_set = 14; /* 21 = ISO ASCII,
                                            * 14 = ISO Latin 1 (which possibly fits our use of uint8 characters better) */

#endif

#define ERROR_PAGE_FONT_PCLXL_LINE_PRINTER 1

#ifdef ERROR_PAGE_FONT_PCLXL_LINE_PRINTER

  uint8*       error_page_font = (uint8*) "Line Printer    ";
  /*
   * The following "weird" point size
   * is necessary because although "Line Printer    "
   * is *implemented* as a permanent soft font
   * we are expecting it to be found via PCL_MISCOP_XL
   * which will not actually match this font by name.
   *
   * Instead it expects to find a font that exactly matches
   * a font with a *pitch* of 16.67
   * and it derives this pitch value as:
   * (72.0 / (<hmi> * <point_size>))
   * where the <hmi> for "Line Printer    " is 0.5
   *
   * Hence the value (144 / 16.67) = 8.6382723455309
   */
  PCLXL_SysVal error_page_point_size = 8.6382723455309;
  /*
   * And the symbol set specified here is irrelevant too
   * as the "Line Printer    "'s own symbolset will be used instead
   */
  uint32       error_page_symbol_set = 14;

#endif

#ifdef ERROR_PAGE_FONT_PCL5_LINE_PRINTER_10U

  uint8*       error_page_font = (uint8*) "\033(10U\033(s0p16.67h8.5v0s0b0T";
  /*
   * When selecting a font using PCL5 font selection criteria
   * the point size and symbol set are typically encoded
   * within the selection criteria string
   * so these next two values are actually irrelevant
   */
  PCLXL_SysVal error_page_point_size = 0.0;
  uint32       error_page_symbol_set = 0;

#endif

  /* We must also pick a "resolution" (i.e. user-coordinate system) for our
   * error/warning page
   *
   * We must then define some coordinates, based upon this chosen resolution at
   * which to place the various bits of text that make up the error (or warning)
   * report
   */
  PCLXL_SysVal error_page_resolution = 600;

  PCLXL_SysVal left_x_offset = 165; /* 7mm from left side */
  PCLXL_SysVal initial_y_offset = 600; /* ~25mm from top */
  PCLXL_SysVal indent_1_x_offset = 450; /* 19mm from left side */
  PCLXL_SysVal indent_2_x_offset = 875; /* ~37mm from left side */
  PCLXL_SysVal line_gap_y_increment = 150; /* ~6mm between baselines */
  PCLXL_SysVal inter_warning_info_y_increment = line_gap_y_increment;
  PCLXL_SysVal x = left_x_offset;
  PCLXL_SysVal y = initial_y_offset;
  ps_context_t *pscontext ;

  HQASSERT(pclxl_context->corecontext != NULL, "No core context") ;
  pscontext = pclxl_context->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  /*
   * Erase the contents of any partially drawn/rendered page
   */

  if ( !erasepage_(pscontext) )
  {
     (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                PCLXL_SS_KERNEL,
                                PCLXL_INTERNAL_ERROR,
                                ("Failed to erase current partially drawn page"));

     return;
  }

  /*
   * We must clear down any job-added graphics states
   * and any graphics state created by BeginPage but not removed by EndPage
   * and any graphics state created by BeginSession but not removed by EndSession
   * to leave just the original graphics state created by the pclxlexec_()'s
   * creation of the PCLXL_CONTEXT
   * created by pclxl_op_begin_session
   */

  while ( ((graphics_state = pclxl_context->graphics_state) != NULL) &&
          (graphics_state->parent_graphics_state != NULL) )
  {
    if ( !pclxl_pop_gs(pclxl_context, FALSE) )
    {
      return;
    }
  }

  /*
   * We must now "push" a PCLXL graphics state
   * that includes a Postscript "save"
   */

  if ( !pclxl_push_gs(pclxl_context, PCLXL_PS_SAVE_RESTORE) )
  {
    return;
  }
  else
  {
    graphics_state = pclxl_context->graphics_state;
  }

  HQASSERT(graphics_state != NULL, "There must be a non-NULL graphics state in order to produce an error/warnings page");

  /*
   * Re-initialize the graphic state structure(s)
   * back to default settings
   *
   * And from now on we must remember to "pop" this graphics state before
   * returning to the caller
   */

  if ( (!pclxl_set_default_graphics_state(pclxl_context, graphics_state)) ||
       (!pclxl_init_non_gs_state(pclxl_context, non_gs_state)) )
  {
    /*
     * Oh dear, we have failed to (re-)initialize
     * the graphics state or non_gs_state
     *
     * So we cannot begin to set up the error page (device)
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to re-initialize graphics state read for error/warning page"));

    (void) pclxl_pop_gs(pclxl_context, FALSE);

    return;
  }

  /*
   * There some things that pclxl_init_non_gs_state() does not
   * set correctly including
   *
   * 1) the PCLXL "user units" because these are usually specified
   *    by the BeginSession operator
   *    which would also specify page origin, duplex etc.
   *
   * 2) The media size and page orientation
   *    which would be set by the BeginPage operator
   */

  non_gs_state->measurement_unit = PCLXL_eInch;
  non_gs_state->units_per_measure.res_x = error_page_resolution;
  non_gs_state->units_per_measure.res_y = error_page_resolution;

  /*
   * We must now set up the page device
   * Which is basically the same series of steps
   * performed by pclxl_op_begin_session() and then
   * pclxl_op_begin_page()
   */

  if ( (!pclxl_get_default_media_details(pclxl_context,
                                         &non_gs_state->requested_media_details)) ||
       (non_gs_state->previous_media_details.media_size_value_type = 0,
        non_gs_state->previous_media_details.orientation = PCLXL_eDefaultOrientation,
        !pclxl_setup_page_device(pclxl_context,
                                 &non_gs_state->previous_media_details,
                                 &non_gs_state->requested_media_details,
                                 &non_gs_state->current_media_details)) ||
       (graphics_state = pclxl_context->graphics_state,
        !pclxl_set_default_ctm(pclxl_context, graphics_state, non_gs_state)) ||
       (!pclxl_set_default_graphics_state(pclxl_context, graphics_state)) ||
       (!pclxl_set_default_color(pclxl_context, graphics_state->color_space_details,
                                 &graphics_state->fill_details.brush_source)) ||
       (!pclxl_set_default_color(pclxl_context, graphics_state->color_space_details,
                                 &graphics_state->line_style.pen_source)) ||
       (!pclxl_ps_set_color(pclxl_context,
                            &graphics_state->line_style.pen_source, FALSE /* For an image? */)) ||
       (!setPclForegroundSource(pclxl_context->corecontext->page,
                                PCL_DL_COLOR_IS_FOREGROUND)) ||
       (!pclxl_set_page_clip(pclxl_context)) ||
       (!pclxl_set_default_font(pclxl_context,
                                error_page_font,
                                (uint32) strlen((char*) error_page_font),
                                error_page_point_size,
                                error_page_symbol_set,
                                FALSE)) ||
       (!pclxl_ps_select_font(pclxl_context, TRUE))
     )
  {
    /*
     * We have failed to get the default media size
     * So we cannot re-initialize the page (device)
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to set-up error/warning page (device)"));

    while ( ((graphics_state = pclxl_context->graphics_state) != NULL) &&
            (graphics_state->parent_graphics_state != NULL) )
    {
      if ( !pclxl_pop_gs(pclxl_context, FALSE) )
      {
        return;
      }
    }

    return;
  }

  (void) pclxl_ps_show_string(pclxl_context, left_x_offset, initial_y_offset,
                              (uint8*) "PCL XL error", NULL);

  y += inter_warning_info_y_increment;

  for ( error_info = pclxl_get_first_error_info(pclxl_context->error_info_list) ;
        error_info != NULL ;
        error_info = pclxl_get_next_error_info(pclxl_context->error_info_list,
                                               error_info) )
  {
    static uint8* subsystem_colon = (uint8*) "Subsystem:";
    static uint8* error_colon = (uint8*) "Error:";
    static uint8* warning_colon = (uint8*) "Warning:";
    static uint8* operator_colon = (uint8*) "Operator:";
    static uint8* position_colon = (uint8*) "Position:";
    static uint8* substituted_for = (uint8*) " substituted for ";
    static uint8* space_dash_space = (uint8*) " - ";

    uint8 subsystem_buffer[64];
    uint8 hex_error_string[64];
    uint8 position_string[16];

    if ( error_info->error_type == PCLXL_ET_ERROR ) {
      (void) pclxl_ps_show_string(pclxl_context, indent_1_x_offset, y,
                                  subsystem_colon, NULL);

      (void) pclxl_ps_show_string(pclxl_context, indent_2_x_offset, y,
                                  pclxl_subsystem_string(error_info,
                                                         subsystem_buffer,
                                                         sizeof(subsystem_buffer)),
                                  NULL);

      (void) pclxl_ps_show_string(pclxl_context, indent_1_x_offset, y += line_gap_y_increment,
                                  error_colon, NULL);
      (void) pclxl_ps_show_string(pclxl_context, indent_2_x_offset, y,
                                  pclxl_error_code_string(error_info->error_code,
                                                          hex_error_string,
                                                          sizeof(hex_error_string)),
                                  &x);

      if ( ((error_info->error_code <= PCLXL_FONT_UNDEFINED) &&
            (error_info->error_code >= PCLXL_ILLEGAL_CHARACTER_DATA)) ||
           ((error_info->error_code <= PCLXL_UNDEFINED_FONT_NOT_REMOVED) &&
            (error_info->error_code >= PCLXL_SYMBOL_SET_REMAP_UNDEFINED))
         )
      {
        uint8 resource_name[64];

        HqMemZero((uint8 *)resource_name, sizeof(resource_name));

        pclxl_append_printable_resource_name(resource_name,
                                             sizeof(resource_name),
                                             error_info->resource_name,
                                             error_info->resource_name_len,
                                             FALSE,
                                             TRUE);

        (void) pclxl_ps_show_string(pclxl_context, x, y,
                                    space_dash_space, &x);
        (void) pclxl_ps_show_string(pclxl_context, x, y,
                                    resource_name, NULL);
      }

      (void) pclxl_ps_show_string(pclxl_context, indent_1_x_offset, y += line_gap_y_increment,
                                  operator_colon, NULL);
      (void) pclxl_ps_show_string(pclxl_context, indent_2_x_offset, y,
                                  pclxl_error_get_tag_string(error_info->operator_tag, error_info->error_code),
                                  NULL);
      (void) pclxl_ps_show_string(pclxl_context, indent_1_x_offset, y += line_gap_y_increment,
                                  position_colon, NULL);

      /* Report operator position allowing for nested streams */
      operator_pos = error_info->last_pos;
      swncopyf(position_string, sizeof(position_string), (uint8*) "%d", *operator_pos);
      pclxl_ps_show_string(pclxl_context, indent_2_x_offset, y, position_string, &x);
      /* HP only report the first two stream operator indexes - change if to a
       * while to report the whole stack
       */
      if ( operator_pos != error_info->operator_pos ) {
        swncopyf(position_string, sizeof(position_string), (uint8*) ";%d", *(--operator_pos));
        pclxl_ps_show_string(pclxl_context, x, y, position_string, &x);
      }

    }
    else if ( include_warnings ) {
      switch ( error_info->error_code ) {

      case PCLXL_FONT_SUBSTITUTED_BY_FONT:

        {
          uint8 substitute_font_name[64];
          uint8 missing_font_name[64];

          HqMemZero((uint8 *)substitute_font_name, (int)sizeof(substitute_font_name));

          pclxl_append_printable_resource_name(substitute_font_name,
                                               sizeof(substitute_font_name),
                                               error_info->substitute_font_name,
                                               error_info->substitute_font_name_len,
                                               FALSE,
                                               TRUE);

          HqMemZero((uint8 *)missing_font_name, (int)sizeof(missing_font_name));

          pclxl_append_printable_resource_name(missing_font_name,
                                               sizeof(missing_font_name),
                                               error_info->missing_font_name,
                                               error_info->missing_font_name_len,
                                               FALSE,
                                               TRUE);

          (void) pclxl_ps_show_string(pclxl_context, indent_1_x_offset, y,
                                      warning_colon, NULL);
          (void) pclxl_ps_show_string(pclxl_context, indent_2_x_offset, y,
                                      substitute_font_name, &x);
          (void) pclxl_ps_show_string(pclxl_context, x, y,
                                      substituted_for, &x);
          (void) pclxl_ps_show_string(pclxl_context, x, y,
                                      missing_font_name, NULL);
        }

        break;

      case PCLXL_UNDEFINED_FONT_NOT_REMOVED:
      case PCLXL_INTERNAL_FONT_NOT_REMOVED:
      case PCLXL_MASS_STORAGE_FONT_NOT_REMOVED:

        {
          uint8 resource_name[64];

          HqMemZero((uint8 *)resource_name, (int)sizeof(resource_name));

          pclxl_append_printable_resource_name(resource_name,
                                               sizeof(resource_name),
                                               error_info->resource_name,
                                               error_info->resource_name_len,
                                               FALSE,
                                               TRUE);

          (void) pclxl_ps_show_string(pclxl_context, indent_1_x_offset, y,
                                      warning_colon, NULL);
          (void) pclxl_ps_show_string(pclxl_context, indent_2_x_offset, y,
                                      pclxl_error_code_string(error_info->error_code,
                                                              hex_error_string,
                                                              sizeof(hex_error_string)),
                                      &x);
          (void) pclxl_ps_show_string(pclxl_context, x, y,
                                      space_dash_space, &x);
          (void) pclxl_ps_show_string(pclxl_context, x, y,
                                      resource_name, NULL);
        }

        break;

      default:
        (void) pclxl_ps_show_string(pclxl_context, indent_1_x_offset, y,
                                    warning_colon, NULL);
        (void) pclxl_ps_show_string(pclxl_context, indent_2_x_offset, y,
                                    pclxl_error_code_string(error_info->error_code,
                                                            hex_error_string,
                                                            sizeof(hex_error_string)),
                                    NULL);
        break;
      }
    }

    y += inter_warning_info_y_increment;
  }

  (void) pclxl_ps_showpage(pclxl_context, 1);

  while ( ((graphics_state = pclxl_context->graphics_state) != NULL) &&
          (graphics_state->parent_graphics_state != NULL) )
  {
    if ( !pclxl_pop_gs(pclxl_context, FALSE) )
    {
      return;
    }
  }
}

void
pclxl_report_errors(PCLXL_CONTEXT pclxl_context)
{
  uint32 error_count = pclxl_count_warnings(pclxl_context->error_info_list,
                                            NULL, 0, NULL, 0, 0,
                                            PCLXL_ET_ERROR,
                                            0, NULL, 0, NULL, 0, NULL, 0);

  uint32 warning_count = pclxl_count_warnings(pclxl_context->error_info_list,
                                              NULL, 0, NULL, 0, 0,
                                              PCLXL_ET_WARNING,
                                              0, NULL, 0, NULL, 0, NULL, 0);

  if ( (error_count == 0) &&
       (warning_count == 0) )
  {
    /*
     * There's nothing to report
     */

    PCLXL_DEBUG((PCLXL_DEBUG_ERRORS | PCLXL_DEBUG_WARNINGS),
                ("There are no errors or warnings to report"));

    return;
  }
  else if ( pclxl_context->error_reporting == PCLXL_eNoReporting )
  {
    /*
     * We have been asked to suppress all errors and warnings
     */

    PCLXL_DEBUG((PCLXL_DEBUG_ERRORS | PCLXL_DEBUG_WARNINGS),
                ("Error/warning reporting is suppressed by eNoReporting but there were %d errors and %d warnings to report",
                 error_count,
                 warning_count));

    return;
  }

  /*
   * For error reporting to an "error page"
   * most of the complexity is in the page setup
   * and pagination of the error report list
   *
   * So we have a separate function that sets up the error page
   * and then iterates round the error (or warning) infos.
   */

  if ( (pclxl_context->error_reporting == PCLXL_eErrorPage) ||
       (pclxl_context->error_reporting == PCLXL_eBackChAndErrPage) ||
       ((error_count > 0) &&
        ((pclxl_context->error_reporting == PCLXL_eNWErrorPage) ||
         (pclxl_context->error_reporting == PCLXL_eNWBackChAndErrPage))) )
  {
    Bool include_warnings =
      ((pclxl_context->error_reporting == PCLXL_eErrorPage) ||
       (pclxl_context->error_reporting == PCLXL_eBackChAndErrPage));

    PCLXL_DEBUG((PCLXL_DEBUG_ERRORS | PCLXL_DEBUG_WARNINGS),
                ("Errors %s reported to error page",
                 (include_warnings ? "and warnings" : "(only)")));

    pclxl_report_to_error_page(pclxl_context, include_warnings);
  }

  /*
   * For error reporting to the "back channel"
   * We also have a dedicated function that opens the "back channel"
   * and iterates round the error (or warning) infos
   */

  if ( (pclxl_context->error_reporting == PCLXL_eBackChannel) ||
       (pclxl_context->error_reporting == PCLXL_eBackChAndErrPage) ||
       ((error_count > 0) &&
        ((pclxl_context->error_reporting == PCLXL_eNWBackChannel) ||
         (pclxl_context->error_reporting == PCLXL_eNWBackChAndErrPage))) )
  {
    Bool include_warnings = ((pclxl_context->error_reporting == PCLXL_eBackChannel) ||
                             (pclxl_context->error_reporting == PCLXL_eBackChAndErrPage));

    PCLXL_DEBUG((PCLXL_DEBUG_ERRORS | PCLXL_DEBUG_WARNINGS),
                ("Errors %s reported to \"Back Channel\" (\"%s\")",
                 (include_warnings ? "and warnings" : "(only)"),
                 pclxl_context->config_params.backchannel_filename));

    pclxl_report_to_back_channel(pclxl_context, include_warnings);

  }
}

/******************************************************************************
* Log stripped */
