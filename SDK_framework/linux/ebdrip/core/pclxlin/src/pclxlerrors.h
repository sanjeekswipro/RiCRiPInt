/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlerrors.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Error result codes and error handling function(s)
 */

#ifndef __PCLXLERRORS_H__
#define __PCLXLERRORS_H__ 1

#include "mm.h"

#include "pclxltypes.h"
#include "pclxlcontext.h"
#include "pclxlparsercontext.h"

/*
 * PCLXL supports "errors" which are always reported immediately
 * and "warnings" that are accumulated and then reported (assuming no errors)
 * at the end of the job.
 *
 * We always gathers errors and warnings into "error lists" beneath
 * the parser context and/or PCLXL context
 */

enum {
  PCLXL_ET_ERROR    = 1,
  PCLXL_ET_WARNING  = 2
};

typedef uint8 PCLXL_ERROR_TYPE;

#define PCLXL_NUM_SUBSYSTEMS  (11)
extern uint8* pclxl_subsystem_strings[PCLXL_NUM_SUBSYSTEMS];

typedef void* PCLXL_SUBSYSTEM;

#define PCLXL_SS_UNKNOWN    ((PCLXL_SUBSYSTEM) pclxl_sybsystem_strings[0])
#define PCLXL_SS_KERNEL     ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[1])
#define PCLXL_SS_TEXT       ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[2])
#define PCLXL_SS_IMAGE      ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[3])
#define PCLXL_SS_JPEG       ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[4])
#define PCLXL_SS_STATE      ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[5])
#define PCLXL_SS_SCANLINE   ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[6])
#define PCLXL_SS_VECTOR     ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[7])
#define PCLXL_SS_USERSTREAM ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[8])
#define PCLXL_SS_PATMGR     ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[9])
#define PCLXL_SS_RASTER     ((PCLXL_SUBSYSTEM) pclxl_subsystem_strings[10])

typedef struct pclxl_error_info_struct* PCLXL_ERROR_INFO;

typedef struct pclxl_error_info_struct
{
  PCLXL_ERROR_INFO      prev_error_info;  /* simple linked list so that callers can augment (or override) the (low level) error info */
  PCLXL_CONTEXT         pclxl_context;    /* the PCLXL context (if any) that was in use when the error was detected */
  char*                 source_file;      /* the PCLXL "C" source code file at which the error was detected */
  unsigned long         source_line;      /* the PCLXL "C" source code line at which the error was detected */
  PCLXL_SUBSYSTEM       subsystem;
  uint32                operator_pos[33]; /* the "position" i.e. operator count within the PCLXL data stream */
  uint32*               last_pos;         /* Position of last operator position recorded */
  PCLXL_TAG             operator_tag;     /* the most recently encountered PCLXL operator (if any) */
  int32                 error_code;       /* the (negative) integer error code */
  PCLXL_ERROR_TYPE      error_type;       /* is this error_info an error or a warning */
  uint8*                missing_font_name;
  size_t                missing_font_name_len;
  size_t                missing_font_name_alloc_len;
  uint8*                substitute_font_name;
  size_t                substitute_font_name_len;
  size_t                substitute_font_name_alloc_len;
  uint8*                resource_name;
  size_t                resource_name_len;
  size_t                resource_name_alloc_len;
#ifdef DEBUG_BUILD
  uint8*                formatted_text;   /* the associated human-readable error message text including any inserted parametric items */
  size_t                formatted_text_len;
  size_t                formatted_text_alloc_len;
#endif
} PCLXL_ERROR_INFO_STRUCT;

/*
 * In order to keep the purely "diagnostic" content
 * out of *release builds*
 * but still retain all this information in *debug* builds
 * almost all errors and warnings are logged via a pair of macros
 * (PCLXL_ERROR_HANDLER() and PCLXL_WARNING_HANDLER())
 * that expand differently under DEBUG_BUILDs and RELEASE_BUILDs
 *
 * These two macros each take exactly 4 parameters
 * where the 4th "parameter" is only used in DEBUG_BUILDs
 * and is actually a vararg parameter list that is supplied
 * to a function that formats a message string by combining a format
 * and any additional optional message parameters exactly like printf()/swncopyf()
 */


extern PCLXL_ERROR_INFO
pclxl_record_basic_error_info(PCLXL_CONTEXT    pclxl_context,
                              PCLXL_SUBSYSTEM  subsystem,
                              PCLXL_ERROR_TYPE error_type,
                              int32            error_code);

extern PCLXL_ERROR_INFO
pclxl_record_source_location(PCLXL_ERROR_INFO error_info,
                             char*            source_file,
                             unsigned long    source_line);


extern PCLXL_ERROR_INFO
pclxl_record_font_name_error_info(PCLXL_ERROR_INFO error_info,
                                  uint8*           font_name,
                                  size_t           font_name_len);

#ifdef DEBUG_BUILD

uint8*
pclxl_format_debug_message(char* format, ...);

extern uint8*
pclxl_record_debug_error_info(PCLXL_ERROR_INFO error_info,
                              uint8*           formatted_text);

extern void
pclxl_log_error(PCLXL_CONTEXT pclxl_context,
                char*         source_file,
                uint32        source_line,
                uint8*        formatted_text);

extern void
pclxl_log_warning(PCLXL_CONTEXT pclxl_context,
                  char*         source_file,
                  uint32        source_line,
                  uint8*        formatted_text);

#define PCLXL_ERROR_HANDLER(__PCLXL_CONTEXT__, __SUBSYSTEM__, __ERROR_CODE__, __DEBUG_FORMAT_AND_PARAMS__) \
(void) pclxl_log_error((__PCLXL_CONTEXT__), __FILE__, __LINE__, pclxl_record_debug_error_info(pclxl_record_source_location(pclxl_record_basic_error_info((__PCLXL_CONTEXT__), (__SUBSYSTEM__), PCLXL_ET_ERROR, (__ERROR_CODE__) ), __FILE__, __LINE__), pclxl_format_debug_message __DEBUG_FORMAT_AND_PARAMS__))

#define PCLXL_WARNING_HANDLER(__PCLXL_CONTEXT__, __SUBSYSTEM__, __WARNING_CODE__, __DEBUG_FORMAT_AND_PARAMS__) \
(void) pclxl_log_warning((__PCLXL_CONTEXT__), __FILE__, __LINE__, pclxl_record_debug_error_info(pclxl_record_source_location(pclxl_record_basic_error_info((__PCLXL_CONTEXT__), (__SUBSYSTEM__), PCLXL_ET_WARNING, (__WARNING_CODE__)), __FILE__, __LINE__), pclxl_format_debug_message __DEBUG_FORMAT_AND_PARAMS__))

#define PCLXL_FONT_ERROR_HANDLER(__PCLXL_CONTEXT__,                                                     \
                                 __SUBSYSTEM__,                                                         \
                                 __ERROR_CODE__,                                                        \
                                 __FONT_NAME__,                                                         \
                                 __FONT_NAME_LEN__,                                                     \
                                 __DEBUG_FORMAT_AND_PARAMS__)                                           \
(void) pclxl_log_error((__PCLXL_CONTEXT__),                                                             \
                       __FILE__,                                                                        \
                       __LINE__,                                                                        \
                       pclxl_record_debug_error_info(                                                   \
                                pclxl_record_source_location(                                           \
                                        pclxl_record_font_name_error_info(                              \
                                                pclxl_record_basic_error_info((__PCLXL_CONTEXT__),      \
                                                                              (__SUBSYSTEM__),          \
                                                                              PCLXL_ET_ERROR,           \
                                                                              (__ERROR_CODE__)          \
                                                                             ),                         \
                                                (__FONT_NAME__),                                        \
                                                (__FONT_NAME_LEN__)                                     \
                                                                         ),                             \
                                        __FILE__,                                                       \
                                        __LINE__),                                                      \
                                pclxl_format_debug_message __DEBUG_FORMAT_AND_PARAMS__))

#else

#define PCLXL_ERROR_HANDLER(__PCLXL_CONTEXT__, __SUBSYSTEM__, __ERROR_CODE__, __DEBUG_FORMAT_AND_PARAMS__) \
(void) pclxl_record_source_location(pclxl_record_basic_error_info((__PCLXL_CONTEXT__), (__SUBSYSTEM__), PCLXL_ET_ERROR, (__ERROR_CODE__)), __FILE__, __LINE__)

#define PCLXL_WARNING_HANDLER(__PCLXL_CONTEXT__, __SUBSYSTEM__, __WARNING_CODE__, __DEBUG_FORMAT_AND_PARAMS__) \
(void) pclxl_record_source_location(pclxl_record_basic_error_info((__PCLXL_CONTEXT__), (__SUBSYSTEM__), PCLXL_ET_WARNING, (__WARNING_CODE__)), __FILE__, __LINE__)

#define PCLXL_FONT_ERROR_HANDLER(__PCLXL_CONTEXT__, __SUBSYSTEM__, __ERROR_CODE__, __FONT_NAME__, __FONT_NAME_LEN__, __DEBUG_FORMAT_AND_PARAMS__) \
(void) pclxl_record_source_location(pclxl_record_font_name_error_info(pclxl_record_basic_error_info((__PCLXL_CONTEXT__), (__SUBSYSTEM__), PCLXL_ET_ERROR, (__ERROR_CODE__)), (__FONT_NAME__), (__FONT_NAME_LEN__)), __FILE__, __LINE__)

#endif

/**
 * \brief pclxl_font_substitution_warning() is
 * intended to be called exactly and only
 * from the point that a font substitution is required
 * It takes the "missing" font name (+ name length) and
 * the substitute font name (+ name length)
 * It basically cross-calls pclxl_warning_handler()
 * passing the correct warning code and warning message text
 */

extern int32
pclxl_font_substitution_warning(char*           source_file,
                                unsigned long   source_line,
                                PCLXL_CONTEXT   pclxl_context,
                                PCLXL_SUBSYSTEM subsystem,
                                uint8*          missing_font_name,
                                size_t          missing_font_name_len,
                                uint8*          substitute_font_name,
                                size_t          substitute_font_name_length);

extern int32
pclxl_resource_not_removed_warning(char*           source_file,
                                   unsigned long   source_line,
                                   PCLXL_CONTEXT   pclxl_context,
                                   PCLXL_SUBSYSTEM subsystem,
                                   int32           warning_code,
                                   uint8*          resource_name,
                                   size_t          resource_name_length);
/*
 * \brief once a collection of errors (or warnings) has been built up
 * (referred to as having been "logged" in the PCLXL spec.)
 * they are then reported to the "back channel" and/or error page(s)
 */

extern void
pclxl_report_errors(PCLXL_CONTEXT pclxl_context);

/**
 * \brief deletes an error info list
 * that may have been created beneath either a parser context
 * or a PCLXL context as part of the handling of an error
 * or indeed be the above-mentioned globally available pclxl_error_info instance
 *
 * In the latter case, the absence of a memory pool will cause
 * the global instance to be cleared/reset but not actually freed
 */

extern void
pclxl_delete_error_info_list(PCLXL_ERROR_INFO_LIST* error_info_list);

#define PCLXL_SUCCESS                             (0)
#define PCLXL_ZERO_BYTES_READ                     (0)

#define PCLXL_EOF                                 (-1)

#define PCLXL_ERROR_OFFSET                        (1000)

#define PCLXL_FAIL                                 (-1 - PCLXL_ERROR_OFFSET)

#define PCLXL_PREMATURE_EOF                        (-2 - PCLXL_ERROR_OFFSET)
#define PCLXL_INSUFFICIENT_MEMORY                  (-3 - PCLXL_ERROR_OFFSET)
#define PCLXL_UNSUPPORTED_BINDING                  (-4 - PCLXL_ERROR_OFFSET)
#define PCLXL_UNSUPPORTED_CLASS_NAME               (-5 - PCLXL_ERROR_OFFSET)
#define PCLXL_UNSUPPORTED_PROTOCOL_VERSION         (-6 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_STREAM_HEADER                (-7 - PCLXL_ERROR_OFFSET)
#define PCLXL_SCAN_DATA_TYPE_VALUE_FAILED          (-8 - PCLXL_ERROR_OFFSET)
#define PCLXL_SCAN_INVALID_UEL                     (-9 - PCLXL_ERROR_OFFSET)

#define PCLXL_CONTEXT_STACK_ERROR                 (-10 - PCLXL_ERROR_OFFSET)
#define PCLXL_GRAPHICS_STATE_STACK_ERROR          (-11 - PCLXL_ERROR_OFFSET)
#define PCLXL_PARSER_CONTEXT_STACK_ERROR          (-12 - PCLXL_ERROR_OFFSET)
#define PCLXL_ATTRIBUTE_LIST_ERROR                (-13 - PCLXL_ERROR_OFFSET)
#define PCLXL_ERROR_INFO_LIST_ERROR               (-14 - PCLXL_ERROR_OFFSET)

#define PCLXL_TAG_NOT_USED                        (-15 - PCLXL_ERROR_OFFSET)
#define PCLXL_TAG_RESERVED_FOR_FUTURE_USE         (-16 - PCLXL_ERROR_OFFSET)
#define PCLXL_TAG_NOT_YET_IMPLEMENTED             (-17 - PCLXL_ERROR_OFFSET)
#define PCLXL_TAG_HANDLED_ELSEWHERE               (-18 - PCLXL_ERROR_OFFSET)

#define PCLXL_ILLEGAL_OPERATOR_SEQUENCE           (-19 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_TAG                         (-20 - PCLXL_ERROR_OFFSET)
#define PCLXL_INTERNAL_OVERFLOW                   (-21 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_ARRAY_SIZE                  (-22 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_ATTRIBUTE                   (-23 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION       (-24 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE         (-25 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_ATTRIBUTE_VALUE             (-26 - PCLXL_ERROR_OFFSET)
#define PCLXL_MISSING_ATTRIBUTE                   (-27 - PCLXL_ERROR_OFFSET)

#define PCLXL_CURRENT_CURSOR_UNDEFINED            (-28 - PCLXL_ERROR_OFFSET)

#define PCLXL_DATA_SOURCE_NOT_OPEN                (-29 - PCLXL_ERROR_OFFSET)
#define PCLXL_DATA_SOURCE_EXCESS_DATA             (-30 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_DATA_LENGTH                 (-31 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_DATA_VALUE                  (-32 - PCLXL_ERROR_OFFSET)
#define PCLXL_MISSING_DATA                        (-33 - PCLXL_ERROR_OFFSET)
#define PCLXL_DATA_SOURCE_NOT_CLOSED              (-34 - PCLXL_ERROR_OFFSET)

#define PCLXL_IMAGE_PALETTE_MISMATCH              (-35 - PCLXL_ERROR_OFFSET)
#define PCLXL_PALETTE_UNDEFINED                   (-36 - PCLXL_ERROR_OFFSET)

#define PCLXL_ILLEGAL_MEDIA_SIZE                  (-37 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_MEDIA_SOURCE                (-38 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_MEDIA_DESTINATION           (-39 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_ORIENTATION                 (-40 - PCLXL_ERROR_OFFSET)

#define PCLXL_MAX_GS_LEVELS_EXCEEDED              (-41 - PCLXL_ERROR_OFFSET)
#define PCLXL_STREAM_UNDEFINED                    (-42 - PCLXL_ERROR_OFFSET)
#define PCLXL_COLOR_SPACE_MISMATCH                (-43 - PCLXL_ERROR_OFFSET)
#define PCLXL_BAD_PATTERN_ID                      (-44 - PCLXL_ERROR_OFFSET)
#define PCLXL_CLIP_MODE_MISMATCH                  (-45 - PCLXL_ERROR_OFFSET)

#define PCLXL_CANNOT_REPLACE_CHARACTER            (-46 - PCLXL_ERROR_OFFSET)
#define PCLXL_FONT_UNDEFINED                      (-47 - PCLXL_ERROR_OFFSET)
#define PCLXL_FONT_NAME_ALREADY_EXISTS            (-48 - PCLXL_ERROR_OFFSET)
#define PCLXL_FST_MISMATCH                        (-49 - PCLXL_ERROR_OFFSET)
#define PCLXL_UNSUPPORTED_CHARACTER_CLASS         (-50 - PCLXL_ERROR_OFFSET)
#define PCLXL_UNSUPPORTED_CHARACTER_FORMAT        (-51 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_CHARACTER_DATA              (-52 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_FONT_DATA                   (-53 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_FONT_HEADER_FIELDS          (-54 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_NULL_SEGMENT_SIZE           (-55 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_FONT_SEGMENT                (-56 - PCLXL_ERROR_OFFSET)
#define PCLXL_MISSING_REQUIRED_SEGMENT            (-57 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_GLOBAL_TRUE_TYPE_SEGMENT    (-58 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_GALLEY_CHARACTER_SEGMENT    (-59 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_VERTICAL_TX_SEGMENT         (-60 - PCLXL_ERROR_OFFSET)
#define PCLXL_ILLEGAL_BITMAP_RESOLUTION_SEGMENT   (-61 - PCLXL_ERROR_OFFSET)
#define PCLXL_UNDEFINED_FONT_NOT_REMOVED          (-62 - PCLXL_ERROR_OFFSET)
#define PCLXL_INTERNAL_FONT_NOT_REMOVED           (-63 - PCLXL_ERROR_OFFSET)
#define PCLXL_MASS_STORAGE_FONT_NOT_REMOVED       (-64 - PCLXL_ERROR_OFFSET)
#define PCLXL_NO_CURRENT_FONT                     (-65 - PCLXL_ERROR_OFFSET)
#define PCLXL_BAD_FONT_DATA                       (-66 - PCLXL_ERROR_OFFSET)
#define PCLXL_FONT_UNDEFINED_NO_SUBSTITUTE_FOUND  (-67 - PCLXL_ERROR_OFFSET)
#define PCLXL_FONT_SUBSTITUTED_BY_FONT            (-68 - PCLXL_ERROR_OFFSET)
#define PCLXL_SYMBOL_SET_REMAP_UNDEFINED          (-69 - PCLXL_ERROR_OFFSET)

#define PCLXL_STREAM_NESTING_FULL                 (-70 - PCLXL_ERROR_OFFSET)
#define PCLXL_STREAM_NESTING_ERROR                (-71 - PCLXL_ERROR_OFFSET)
#define PCLXL_STREAM_ALREADY_RUNNING              (-72 - PCLXL_ERROR_OFFSET)
#define PCLXL_STREAM_CALLING_ITSELF               (-73 - PCLXL_ERROR_OFFSET)
#define PCLXL_INTERNAL_STREAM_ERROR               (-74 - PCLXL_ERROR_OFFSET)
#define PCLXL_UNDEFINED_STREAM_NOT_REMOVED        (-75 - PCLXL_ERROR_OFFSET)
#define PCLXL_INTERNAL_STREAM_NOT_REMOVED         (-76 - PCLXL_ERROR_OFFSET)
#define PCLXL_MASS_STORAGE_STREAM_NOT_REMOVED     (-77 - PCLXL_ERROR_OFFSET)

#define PCLXL_ILLEGAL_OPERATOR_TAG                (-78 - PCLXL_ERROR_OFFSET)

#define PCLXL_INTERNAL_ERROR                      (-79 - PCLXL_ERROR_OFFSET)

#endif /* __PCLXLERRORS_H__ */

/******************************************************************************
* Log stripped */
