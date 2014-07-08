/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!export:hqunicode.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to the Unicode manipulation library.
 *
 * The Unicode manipulation functions provide facilities to convert between
 * Unicode transformation representations and platform native encodings, to
 * compare normalised Unicode sequences, and to iterate over Unicode
 * sequences. Some of the functionality is homegrown, some is provided by
 * IBM's International Components for Unicode (ICU) library, included as a
 * sub-compound of HQNc-unicode.
 */


#ifndef _HQUNICODE_H_
#define _HQUNICODE_H_

#include "hqtypes.h"      /* Basic int*, uint* typedefs */
#include <stddef.h>       /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \defgroup unicode Unicode support.
 * \ingroup core
 *
 * These functions and types define the built-in lightweight UTF-8
 * functionality, and the skin to more complex functionality provided by
 * IBM's ICU library. We do not expose the ICU interface to the rest of the
 * world, because it has dependencies on int types and macros that we do not
 * want to rely on in other compounds.
 *
 * \{
 */

/* Unicode code point for Byte Order Mark */
#define UNICODE_BOM 0xFEFF

/* String representation of UTF-8 encoded Byte Order Mark (0xEF 0xBB 0xBF) */
#define UTF8_BOM "\357\273\277"
#define UTF8_BOM_LEN 3

/** \brief UTF-8 code unit */
typedef uint8 UTF8 ;

/** \brief UTF-16 code unit */
typedef uint16 UTF16 ;

/** \brief UTF-32 code unit, also for Unicode scalar values and code points.

    We use int32 rather than uint32 because the valid Unicode range is
    0-0x10ffff out of a representation supporting 0-0x7fffffff, and this
    gives us easily testable out of band values for function return values.
 */
typedef int32 UTF32 ;

/* \brief Typedef for Unicode and ICU memory handling functions. These are
   compatible with the ICU typedefs for UMemAllocFn, UMemReallocFn, and
   UMemFreeFn. */
typedef struct unicode_memory_t {
  void *context ;
  void *(*alloc_fn)(const void *context, size_t size);
  void *(*realloc_fn)(const void *context, void *mem, size_t size);
  void (*free_fn)(const void *context, void *mem);
} unicode_memory_t ;

/* \brief Typedef for Unicode and ICU file handling functions. */
typedef struct unicode_fileio_t {
  void *context ;
  void *(*open_fn)(const void *context, const char *filename, const char *mode) ;
  void (*close_fn)(const void *context, void *filestream) ;
  int32 (*read_fn)(const void *context, void *addr, int32 bytes, void *filestream) ;
  int32 (*write_fn)(const void *context, const void *addr, int32 bytes, void *filestream) ;
  void (*rewind_fn)(const void *context, void *filestream) ;
  int32 (*extent_fn)(const void *context, void *filestream) ;
  int (*eof_fn)(const void *context, void *filestream) ;
  int (*remove_fn)(const void *context, const char *fileName) ;
  int (*error_fn)(const void *context, void *filestream) ;
} unicode_fileio_t ;

/** \brief Initialise the Unicode sub-system. This function MUST be called
    before any routines that use the ICU functionality are used. The
    definitions of the memory and file handlers are. */
HqBool unicode_init(const char *prefix,
                    unicode_fileio_t *file_handler,
                    unicode_memory_t *mem_handler) ;

/** \brief Terminate the Unicode sub-system. This should be called before
    quitting. */
void unicode_finish(void) ;

/** Unicode iterator interfaces.

    These functions define an interface to extract Unicode characters from a
    UTF-8 or UTF-16 code unit sequence.
*/

/** \brief Parameter macro for passing constant strings and their lengths to
           functions.

    The string concatenation ensure that the macro parameter is a constant
    string.
 */
#define UTF8_AND_LENGTH(s) (UTF8 *)("" s ""), (uint32)(sizeof("" s "") - 1)

/** \brief Buffer structure for UTF-8 sequences.

    This structure is exposed so that knowledgeable clients can manipulate
    multiple-byte units at once.
 */
typedef struct utf8_buffer {
  UTF8 *codeunits ;   /**< Pointer to code units in UTF-8 sequence */
  uint32 unitlength ; /**< Number of remaining code units (bytes) in sequence */
} utf8_buffer ;

/** \brief Initialise an iteration over a UTF-8 sequence.
    \param iterator Pointer to an iterator structure to initialise.
    \param string   A UTF-8 string to iterate over.
    \param length   The length in code units (bytes) of the UTF-8 string to
                    iterate over.
 */
void utf8_iterator_init(/*@notnull@*/ /*@out@*/ utf8_buffer *iterator,
                        /*@null@*/ /*@in@*/ UTF8 *string,
                        uint32 length) ;

/** \brief Test if a UTF-8 iterator has any more code points to extract.
    \param iterator Pointer to an iterator to test.
 */
/*@-exportheader@*/
HqBool utf8_iterator_more(/*@notnull@*/ /*@in@*/ const utf8_buffer *iterator) ;
#define utf8_iterator_more(it) ((it)->unitlength != 0)

/** \brief Extract next code point from a UTF-8 iterator, incrementing the
           iterator.
    \param iterator Pointer to an iterator to extract.
    \return Code point of Unicode character, -1 if no more code points.

    Use this function only when the UTF-8 string is known to be valid; it
    will assert that the UTF-8 representation is correct.
 */
UTF32 utf8_iterator_get_next(/*@notnull@*/ /*@in@*/ utf8_buffer *iterator) ;

/** \brief Buffer structure for UTF-16 sequences.
 */
typedef struct utf16_buffer {
  UTF16 *codeunits ;  /**< Pointer to code units in UTF-16 sequence */
  uint32 unitlength ; /**< Number of remaining code units in sequence */
} utf16_buffer ;

/** \brief Initialise an iteration over a UTF-16 sequence.
    \param iterator Pointer to an iterator structure to initialise.
    \param string   A UTF-16 string to iterate over.
    \param length   The length in code units of the UTF-16 string to iterate
                    over.
 */
void utf16_iterator_init(/*@notnull@*/ /*@out@*/ utf16_buffer *iterator,
                         /*@notnull@*/ /*@in@*/ UTF16 *string,
                         uint32 length) ;

/** \brief Test if a UTF-16 iterator has any more code points to extract.
    \param iterator Pointer to an iterator to test.
 */
/*@-exportheader@*/
HqBool utf16_iterator_more(/*@notnull@*/ /*@in@*/ const utf16_buffer *iterator) ;
#define utf16_iterator_more(it) ((it)->unitlength != 0)

/** \brief Extract next code point from a UTF-16 iterator, incrementing the
           iterator.
    \param iterator Pointer to an iterator to extract.
    \return Code point of Unicode character, -1 if no more code points.

    Use this function only when the UTF-16 string is known to be valid; it
    will assert that the UTF-16 representation is correct.
 */
UTF32 utf16_iterator_get_next(/*@notnull@*/ /*@in@*/ utf16_buffer *iterator) ;

/** UTF validation functions.

     These functions provide validation and transformation sizing for UTF-8 and
     UTF-16 sequences. They can be used to determine how much buffer space to
     allocate.
*/

/** \brief Test if a unicode code point is valid. */
#define UNICODE_CODEPOINT_VALID(u) \
  (((u) >= 0 && (u) <= 0xd7ff) || ((u) >= 0xe000 && (u) <= 0x10ffff))

/** \brief Extract a limit pointer of a UTF buffer. */
#define UTF_BUFFER_LIMIT(buffer) ((buffer).codeunits + (buffer).unitlength)

/** \brief Extract next code point from a raw UTF-8 string, validating the UTF-8
           string and updating the string pointer.
    \param string Pointer to a string from which extract codepoints.
    \param limit The end of the UTF-8 string. If this is NULL, then
                 code units will be consumed until the codepoint is
                 determined to be valid or invalid. This can be used to iterate
                 over zero-terminated sequences. The zero terminator will be
                 returned as a valid codepoint, which the client should detect.
    \return Code point of Unicode character, -1 if not a valid UTF-8
            representation.

    This function can be used to iterate over codepoint-zero terminated
    strings; it will return the terminating zero as a codepoint in this case,
    the caller must detect that the end of the string has been reached. This
    function always updates the string pointer, even for invalid UTF-8
    representations. This function should only be called when codepoints are
    expected (limit is NULL or greater than the string start).
 */
UTF32 utf8_validate_next(/*@notnull@*/ /*@in@*/ const UTF8 **string,
                         /*@null@*/ /*@observer@*/ const UTF8 *limit) ;

/** \brief Get number of code units and codepoints in a UTF-8 string,
           validating the representation.
    \param string The start of the UTF-8 string.
    \param limit The end of the UTF-8 string. If this is NULL, then the string
                 is assumed to be zero terminated. In this case, the zero
                 terminator will not be included in the unit counts.
    \param utf8units The length of the string represented in UTF-8 code units.
                     This may be NULL if the value is not required.
    \param utf16units The length of the string represented in UTF-16 code units.
                      This may be NULL if the value is not required.
    \param codepoints The number of codepoints in the string (this is the same
                      as the string represented in UTF-32 code units).
                      This may be NULL if the value is not required.
    \return The number of invalid code points in the string. This is the sum of
            parseable but invalid code points and unparseable code units from
            which the scan recovered.

    Extract the number of code units and code points required to represent
    a UTF-8 string.
*/
uint32 utf8_validate(/*@notnull@*/ /*@in@*/ /*@observer@*/ const UTF8 *string,
                     /*@null@*/ /*@observer@*/ const UTF8 *limit,
                     /*@null@*/ /*@out@*/ uint32 *utf8units,
                     /*@null@*/ /*@out@*/ uint32 *utf16units,
                     /*@null@*/ /*@out@*/ uint32 *codepoints) ;

/** \brief Extract next code point from a raw UTF-16 string, validating the
           UTF-16 string and updating the string pointer.
    \param string Pointer to a string from which extract codepoints.
    \param limit The end of the UTF-16 string. If this is NULL, then
                 code units will be consumed until the codepoint is
                 determined to be valid or invalid. This can be used to iterate
                 over zero-terminated sequences. The zero terminator will be
                 returned as a valid codepoint, which the client should detect.
    \return Code point of Unicode character, -1 if not a valid UTF-16
            representation.

    This function can be used to iterate over codepoint-zero terminated
    strings; it will return the terminating zero as a codepoint in this case,
    the caller must detect that the end of the string has been reached. This
    function always updates the string pointer, even for invalid UTF-16
    representations. This function should only be called when codepoints are
    expected (limit is NULL or greater than the string start).
 */
UTF32 utf16_validate_next(/*@notnull@*/ /*@in@*/ const UTF16 **string,
                          /*@null@*/ /*@observer@*/ const UTF16 *limit) ;

/** \brief Get number of code units and codepoints in a UTF-16 string,
           validating the representation.
    \param string The start of the UTF-16 string.
    \param limit The end of the UTF-16 string. If this is NULL, then the string
                 is assumed to be zero terminated. In this case, the zero
                 terminator will not be included in the unit counts.
    \param utf8units The length of the string represented in UTF-8 code units.
                     This may be NULL if the value is not required.
    \param utf16units The length of the string represented in UTF-16 code units.
                      This may be NULL if the value is not required.
    \param codepoints The number of codepoints in the string (this is the same
                      as the string represented in UTF-32 code units).
                      This may be NULL if the value is not required.
    \return The number of invalid code points in the string. This is the sum of
            parseable but invalid code points and unparseable code units from
            which the scan recovered.

    Extract the number of code units and code points required to represent
    a UTF-16 string.
*/
uint32 utf16_validate(/*@notnull@*/ /*@in@*/ /*@observer@*/ const UTF16 *string,
                      /*@null@*/ /*@observer@*/ const UTF16 *limit,
                      /*@null@*/ /*@out@*/ uint32 *utf8units,
                      /*@null@*/ /*@out@*/ uint32 *utf16units,
                      /*@null@*/ /*@out@*/ uint32 *codepoints) ;

/** \brief Determine if a UTF-8 string is prefixed with a Byte Order Mark.
    \param string The start of the UTF-8 string.
    \param limit The end of the UTF-8 string. If this is NULL, then the string
                 is assumed to be zero terminated.
    \return TRUE if the string is prefixed with a Byte Order Mark.
*/
HqBool utf8_has_bom(/*@notnull@*/ /*@in@*/ /*@observer@*/ const UTF8 *string,
                    /*@null@*/ /*@observer@*/ const UTF8 *limit) ;

/** Unicode conversion functions.

     These functions provide conversion between UTF-8, UTF-16, and UTF-32.
     The UTF-8 iterator functions provide a character-by-character conversion
     of UTF-8 sequences to Unicode code points, which can be stored directly
     in UTF-32 values. Converters are provided between UTF-8 and UTF-16
     sequences, and between UTF-8 or UTF-16 representations and UTF-32 code
     points. Byte order marks are neither detected nor consumed by these
     functions; they should be stripped from input if desired before
     conversion.
*/

/** \brief Values returned by UTF conversion functions.

    The functions are designed so that a large conversion can be performed a
    buffer at a time, without regard to code unit boundaries. The caller can
    detect if the final code unit in a buffer does not complete a code point,
    and copy the remaining code units to the start of a new input buffer. The
    caller can also detect if the output buffer was exhausted, and process
    the code units converted so far, or provide a new buffer to continue
    conversion.
 */
enum {
  UTF_CONVERT_OK = 0,            /**< Conversion was successful */
  UTF_CONVERT_NOT_AVAILABLE,     /**< ICU error (unrecoverable) */
  UTF_CONVERT_INVALID_CODEPOINT, /**< Code point is invalid (unrecoverable) */
  UTF_CONVERT_INVALID_ENCODING,  /**< Encoding is invalid (unrecoverable) */
  UTF_CONVERT_NOT_CANONICAL,     /**< Non-shortest form UTF-8 representation (recoverable) */
  UTF_CONVERT_INPUT_EXHAUSTED,   /**< Input does not complete a Unicode code point */
  UTF_CONVERT_OUTPUT_EXHAUSTED   /**< Output does not have space for conversion */
} ;

/** \brief Convert a single Unicode code point to UTF-8.
    \param unicode Unicode code point value.
    \param output  Buffer to store UTF-8 representation of \c unicode in.
    \return One of the \c UTF_CONVERT_* enum codes.

    If the conversion is successful, the UTF-8 buffer is updated to reflect
    the remaining space and new end location of the string. To be certain of
    success, the buffer must have at least 4 bytes available.
*/
int unicode_to_utf8(UTF32 unicode,
                    /*@notnull@*/ /*@in@*/ utf8_buffer *output) ;

/** \brief Convert a single Unicode code point to UTF-16.
    \param unicode Unicode code point value.
    \param output  Buffer to store UTF-16 representation of \c unicode in.
    \return One of the \c UTF_CONVERT_* enum codes.

    If the conversion is successful, the UTF-16 buffer is updated to reflect
    the remaining space and new end location of the string. To be certain of
    success, the buffer must have at least 4 bytes (2 codeunits) available.
*/
int unicode_to_utf16(UTF32 unicode,
                     /*@notnull@*/ /*@in@*/ utf16_buffer *output) ;

/** \brief Convert a sequence of UTF-16 code units to a UTF-8 sequence.
    \param input Buffer containing a UTF-16 code unit sequence.
    \param output Buffer to store UTF-8 representation of the UTF-16 sequence in.
    \return One of the \c UTF_CONVERT_* enum codes.

    The buffers are updated to reflect the remaining space and new end
    locations after the last successful converted code point in the string.
*/
int utf16_to_utf8(/*@notnull@*/ /*@in@*/ utf16_buffer *input,
                  /*@notnull@*/ /*@in@*/ utf8_buffer *output) ;

/** \brief Convert a sequence of UTF-8 code units to a UTF-16 sequence.
    \param input Buffer containing a UTF-8 code unit sequence.
    \param output Buffer to store UTF-16 representation of the UTF-8 sequence in.
    \return One of the \c UTF_CONVERT_* enum codes.

    The buffers are updated to reflect the remaining space and new end
    locations after the last successful converted code point in the string.
*/
int utf8_to_utf16(/*@notnull@*/ /*@in@*/ utf8_buffer *input,
                  /*@notnull@*/ /*@in@*/ utf16_buffer *output) ;

/** \brief Opaque pointer onto converter object.

    The Unicode converter object is returned by \c unicode_convert_open().
*/
typedef struct unicode_convert_t unicode_convert_t ;

enum {
  UCONVERT_BOM_LEAVE, UCONVERT_BOM_REMOVE, UCONVERT_BOM_ADD
} ;

/** \brief Open a Unicode converter.
    \param inname The name of the input encoding.
    \param inlen Length of the input name.
    \param outname The name of the input encoding.
    \param outlen Length of the input name.
    \param bom An enumeration indicating if a Byte Order Mark should be added
               to Unicode output, removed from it, or left alone after
               conversion.
    \param subs The string to substitute for invalid characters in the encodings.
    \param subslen Length of the substitution string.
    \return A pointer to a Unicode converter.

    This creates a general encoding transformer, suitable for streamed
    conversions. It relies on the code page and Unicode conversion capability
    provided by the ICU library. If ICU is not built-in, or the input or
    output conversion is not available, the function returns NULL.

    Once opened, Unicode converters MUST be closed with
    unicode_convert_close().
*/
unicode_convert_t *unicode_convert_open(uint8 *inname, uint32 inlen,
                                        uint8 *outname, uint32 outlen,
                                        int bom,
                                        uint8 *subs, uint32 subslen) ;

/** \brief Convert a buffer of encoded data.
    \param converter The unicode converter to use for the conversion.
    \param input Buffer containing an encoded sequence.
    \param inlen Length of input data.
    \param output Buffer in which to store the converted sequence.
    \param outlen Space available in the output buffer.
    \param flush Flag indicating whether to flush converted output to buffer.
    \return One of the \c UTF_CONVERT_* enum codes.

    The buffers are updated to reflect the remaining space and new end
    locations after the last successful converted code point in the string.
*/
int unicode_convert_buffer(unicode_convert_t *converter,
                           uint8 **input, int32 *inlen,
                           uint8 **output, int32 *outlen,
                           HqBool flush) ;

/** \brief Close a Unicode converter.
    \param converter The unicode converter to close.
*/
void unicode_convert_close(unicode_convert_t **converter) ;

/** Unicode normalisation functions.

    These functions provide normalisation and normalised comparison between
    Unicode character sequences.
*/

/** \brief Normalisation forms. */
enum {
  UTF_NORMAL_FORM_NONE, /**< Not normalised. */
  UTF_NORMAL_FORM_C,    /**< Normalisation form C (decomposed, composed). */
  UTF_NORMAL_FORM_KC,   /**< Normalisation form KC (decomposed, compatibility composition). */
  UTF_NORMAL_FORM_D,    /**< Normalisation form D (decomposed). */
  UTF_NORMAL_FORM_KD    /**< Normalisation form KD (compatibility decomposed). */
} ;

/** \brief Ordering forms. */
enum {
  UTF_ORDER_UNSPECIFIED, /**< Consistent but unspecified ordering. */
  UTF_ORDER_CODEPOINT    /**< Ordered by codepoint. */
} ;

/** \brief Case folding options. */
enum {
  UTF_CASE_UNFOLDED, /**< No case conversion/comparison. */
  UTF_CASE_FOLDED,   /**< Case insensitive conversion/comparison. */
  UTF_CASE_UPPER,    /**< Upper case conversion/comparison. */
  UTF_CASE_LOWER,    /**< Lower case conversion/comparison. */
  UTF_CASE_TITLE     /**< Title case conversion/comparison. */
} ;

/** \brief Normalise a UTF-8 sequence.
    \param input Buffer containing a UTF-8 code unit sequence.
    \param output Buffer to store normalised UTF-8 code unit sequence in.
    \param normalform One of the \c UTF_NORMAL_FORM_* enum codes, specifying
           the normalisation form.
    \return One of the \c UTF_CONVERT_* enum codes.

    The buffers are updated to reflect the remaining space and new end
    locations after the last successful converted code point in the string.
    The input must be complete; partial code points are not accepted.
*/
int utf8_normalize(/*@notnull@*/ /*@in@*/ utf8_buffer *input,
                   /*@notnull@*/ /*@in@*/ utf8_buffer *output,
                   int normalform) ;

/** \brief Change case of a UTF-8 sequence.
    \param input Buffer containing a UTF-8 code unit sequence.
    \param output Buffer to store normalised UTF-8 code unit sequence in.
    \param casefold One of the \c UTF_NORMAL_FORM_* enum codes, specifying
           the case folding form.
    \return One of the \c UTF_CONVERT_* enum codes.

    The buffers are updated to reflect the remaining space and new end
    locations after the last successful converted code point in the string.
    The input must be complete; partial code points are not accepted.
*/
int utf8_case(/*@notnull@*/ /*@in@*/ utf8_buffer *input,
              /*@notnull@*/ /*@in@*/ utf8_buffer *output,
              int casefold) ;

/** \brief Compare UTF-8 sequences.
    \param left Buffer containing first UTF-8 code unit sequence.
    \param right Buffer containing second UTF-8 code unit sequence.
    \param casefold Fold case before comparison. Note that case folding may
                    require renormalisation.
    \param normalform One of the \c UTF_NORMAL_FORM_* enum codes, specifying
           the normalisation form, if any.
    \param order One of the \c UTF_ORDER_* enum codes, specifying the collation
                 order.
    \return -1, 0, or 1.

    This function compares UTF-8 sequences, possibly ignoring case and
    normalisation differences. The return value will be negative if left
    comes before right, zero if they are identical, and positive if left
    comes after right. The comparison is done consistently, but not in a
    guaranteed order, unless specified to be collation or codepoint order.
    The input must be complete; partial code points are not accepted.
*/
int utf8_compare(/*@notnull@*/ /*@in@*/ utf8_buffer *left,
                 /*@notnull@*/ /*@in@*/ utf8_buffer *right,
                 int casefold, int normalform, int order) ;

/** \brief Normalise a UTF-16 sequence.
    \param input Buffer containing a UTF-8 code unit sequence.
    \param output Buffer to store normalised UTF-8 code unit sequence in.
    \param normalform One of the \c UTF_NORMAL_FORM_* enum codes, specifying
           the normalisation form.
    \return One of the \c UTF_CONVERT_* enum codes.

    The buffers are updated to reflect the remaining space and new end
    locations after the last successful converted code point in the string.
    The input must be complete; partial code points are not accepted.
*/
int utf16_normalize(/*@notnull@*/ /*@in@*/ utf16_buffer *input,
                    /*@notnull@*/ /*@in@*/ utf16_buffer *output,
                    int normalform) ;

/** \brief Change case of a UTF-16 sequence.
    \param input Buffer containing a UTF-16 code unit sequence.
    \param output Buffer to store normalised UTF-16 code unit sequence in.
    \param casefold One of the \c UTF_NORMAL_FORM_* enum codes, specifying
           the case folding form.
    \return One of the \c UTF_CONVERT_* enum codes.

    The buffers are updated to reflect the remaining space and new end
    locations after the last successful converted code point in the string.
    The input must be complete; partial code points are not accepted.
*/
int utf16_case(/*@notnull@*/ /*@in@*/ utf16_buffer *input,
               /*@notnull@*/ /*@in@*/ utf16_buffer *output,
               int casefold) ;

/** \brief Compare UTF-16 sequences.
    \param left Buffer containing first UTF-16 code unit sequence.
    \param right Buffer containing second UTF-16 code unit sequence.
    \param casefold Fold case before comparison. Note that case folding may
                    require renormalisation.
    \param normalform One of the \c UTF_NORMAL_FORM_* enum codes, specifying
           the normalisation form, if any.
    \param order One of the \c UTF_ORDER_* enum codes, specifying the collation
                 order.
    \return -1, 0, or 1.

    This function compares UTF-16 sequences, possibly ignoring case and
    normalisation differences. The return value will be negative if left
    comes before right, zero if they are identical, and positive if left
    comes after right. The comparison is done consistently, but not in a
    guaranteed order, unless specified to be collation or codepoint order.
    The input must be complete; partial code points are not accepted.
*/
int utf16_compare(/*@notnull@*/ /*@in@*/ utf16_buffer *left,
                  /*@notnull@*/ /*@in@*/ utf16_buffer *right,
                  int casefold, int normalform, int order) ;

/* NOTE: These enumerations are copied directly from ICU uchar.h and
   renamed with the prefix UTF32_ rather than UCHAR_. They MUST match
   the enumerations in that header file exactly. This means the
   enumerations can be passed directly through to the ICU routines
   without going through any mapping. At the same time, we maintain a
   layer over ICU so that in theory, we could replace the Unicode
   backend with a different library. */
/** Unicode character property constants. */
typedef enum UTF32_Property {
    /*  Note: Place UTF32_ALPHABETIC before UTF32_BINARY_START so that
        debuggers display UTF32_ALPHABETIC as the symbolic name for 0,
        rather than UTF32_BINARY_START.  Likewise for other *_START
        identifiers. */

    /** Binary property Alphabetic. Same as u_isUAlphabetic, different from u_isalpha.
        Lu+Ll+Lt+Lm+Lo+Nl+Other_Alphabetic stable ICU 2.1 */
    UTF32_ALPHABETIC=0,
    /** First constant for binary Unicode properties. stable ICU 2.1 */
    UTF32_BINARY_START=UTF32_ALPHABETIC,
    /** Binary property ASCII_Hex_Digit. 0-9 A-F a-f stable ICU 2.1 */
    UTF32_ASCII_HEX_DIGIT,
    /** Binary property Bidi_Control.
        Format controls which have specific functions
        in the Bidi Algorithm. stable ICU 2.1 */
    UTF32_BIDI_CONTROL,
    /** Binary property Bidi_Mirrored.
        Characters that may change display in RTL text.
        Same as u_isMirrored.
        See Bidi Algorithm, UTR 9. stable ICU 2.1 */
    UTF32_BIDI_MIRRORED,
    /** Binary property Dash. Variations of dashes. stable ICU 2.1 */
    UTF32_DASH,
    /** Binary property Default_Ignorable_Code_Point (new in Unicode 3.2).
        Ignorable in most processing.
        <2060..206F, FFF0..FFFB, E0000..E0FFF>+Other_Default_Ignorable_Code_Point+(Cf+Cc+Cs-White_Space) stable ICU 2.1 */
    UTF32_DEFAULT_IGNORABLE_CODE_POINT,
    /** Binary property Deprecated (new in Unicode 3.2).
        The usage of deprecated characters is strongly discouraged. stable ICU 2.1 */
    UTF32_DEPRECATED,
    /** Binary property Diacritic. Characters that linguistically modify
        the meaning of another character to which they apply. stable ICU 2.1 */
    UTF32_DIACRITIC,
    /** Binary property Extender.
        Extend the value or shape of a preceding alphabetic character,
        e.g., length and iteration marks. stable ICU 2.1 */
    UTF32_EXTENDER,
    /** Binary property Full_Composition_Exclusion.
        CompositionExclusions.txt+Singleton Decompositions+
        Non-Starter Decompositions. stable ICU 2.1 */
    UTF32_FULL_COMPOSITION_EXCLUSION,
    /** Binary property Grapheme_Base (new in Unicode 3.2).
        For programmatic determination of grapheme cluster boundaries.
        [0..10FFFF]-Cc-Cf-Cs-Co-Cn-Zl-Zp-Grapheme_Link-Grapheme_Extend-CGJ stable ICU 2.1 */
    UTF32_GRAPHEME_BASE,
    /** Binary property Grapheme_Extend (new in Unicode 3.2).
        For programmatic determination of grapheme cluster boundaries.
        Me+Mn+Mc+Other_Grapheme_Extend-Grapheme_Link-CGJ stable ICU 2.1 */
    UTF32_GRAPHEME_EXTEND,
    /** Binary property Grapheme_Link (new in Unicode 3.2).
        For programmatic determination of grapheme cluster boundaries. stable ICU 2.1 */
    UTF32_GRAPHEME_LINK,
    /** Binary property Hex_Digit.
        Characters commonly used for hexadecimal numbers. stable ICU 2.1 */
    UTF32_HEX_DIGIT,
    /** Binary property Hyphen. Dashes used to mark connections
        between pieces of words, plus the Katakana middle dot. stable ICU 2.1 */
    UTF32_HYPHEN,
    /** Binary property ID_Continue.
        Characters that can continue an identifier.
        DerivedCoreProperties.txt also says "NOTE: Cf characters should be filtered out."
        ID_Start+Mn+Mc+Nd+Pc stable ICU 2.1 */
    UTF32_ID_CONTINUE,
    /** Binary property ID_Start.
        Characters that can start an identifier.
        Lu+Ll+Lt+Lm+Lo+Nl stable ICU 2.1 */
    UTF32_ID_START,
    /** Binary property Ideographic.
        CJKV ideographs. stable ICU 2.1 */
    UTF32_IDEOGRAPHIC,
    /** Binary property IDS_Binary_Operator (new in Unicode 3.2).
        For programmatic determination of
        Ideographic Description Sequences. stable ICU 2.1 */
    UTF32_IDS_BINARY_OPERATOR,
    /** Binary property IDS_Trinary_Operator (new in Unicode 3.2).
        For programmatic determination of
        Ideographic Description Sequences. stable ICU 2.1 */
    UTF32_IDS_TRINARY_OPERATOR,
    /** Binary property Join_Control.
        Format controls for cursive joining and ligation. stable ICU 2.1 */
    UTF32_JOIN_CONTROL,
    /** Binary property Logical_Order_Exception (new in Unicode 3.2).
        Characters that do not use logical order and
        require special handling in most processing. stable ICU 2.1 */
    UTF32_LOGICAL_ORDER_EXCEPTION,
    /** Binary property Lowercase. Same as u_isULowercase, different from u_islower.
        Ll+Other_Lowercase stable ICU 2.1 */
    UTF32_LOWERCASE,
    /** Binary property Math. Sm+Other_Math stable ICU 2.1 */
    UTF32_MATH,
    /** Binary property Noncharacter_Code_Point.
        Code points that are explicitly defined as illegal
        for the encoding of characters. stable ICU 2.1 */
    UTF32_NONCHARACTER_CODE_POINT,
    /** Binary property Quotation_Mark. stable ICU 2.1 */
    UTF32_QUOTATION_MARK,
    /** Binary property Radical (new in Unicode 3.2).
        For programmatic determination of
        Ideographic Description Sequences. stable ICU 2.1 */
    UTF32_RADICAL,
    /** Binary property Soft_Dotted (new in Unicode 3.2).
        Characters with a "soft dot", like i or j.
        An accent placed on these characters causes
        the dot to disappear. stable ICU 2.1 */
    UTF32_SOFT_DOTTED,
    /** Binary property Terminal_Punctuation.
        Punctuation characters that generally mark
        the end of textual units. stable ICU 2.1 */
    UTF32_TERMINAL_PUNCTUATION,
    /** Binary property Unified_Ideograph (new in Unicode 3.2).
        For programmatic determination of
        Ideographic Description Sequences. stable ICU 2.1 */
    UTF32_UNIFIED_IDEOGRAPH,
    /** Binary property Uppercase. Same as u_isUUppercase, different from u_isupper.
        Lu+Other_Uppercase stable ICU 2.1 */
    UTF32_UPPERCASE,
    /** Binary property White_Space.
        Same as u_isUWhiteSpace, different from u_isspace and u_isWhitespace.
        Space characters+TAB+CR+LF-ZWSP-ZWNBSP stable ICU 2.1 */
    UTF32_WHITE_SPACE,
    /** Binary property XID_Continue.
        ID_Continue modified to allow closure under
        normalization forms NFKC and NFKD. stable ICU 2.1 */
    UTF32_XID_CONTINUE,
    /** Binary property XID_Start. ID_Start modified to allow
        closure under normalization forms NFKC and NFKD. stable ICU 2.1 */
    UTF32_XID_START,
    /** Binary property Case_Sensitive. Either the source of a case
        mapping or _in_ the target of a case mapping. Not the same as
        the general category Cased_Letter. draft ICU 2.6 */
    UTF32_CASE_SENSITIVE,
    /** Binary property STerm (new in Unicode 4.0.1).
        Sentence Terminal. Used in UAX #29: Text Boundaries
        (http://www.unicode.org/reports/tr29/)
        draft ICU 3.0 */
    UTF32_S_TERM,
    /** Binary property Variation_Selector (new in Unicode 4.0.1).
        Indicates all those characters that qualify as Variation Selectors.
        For details on the behavior of these characters,
        see StandardizedVariants.html and 15.6 Variation Selectors.
        draft ICU 3.0 */
    UTF32_VARIATION_SELECTOR,
    /** Binary property NFD_Inert.
        ICU-specific property for characters that are inert under NFD,
        i.e., they do not interact with adjacent characters.
        Used for example in normalizing transforms in incremental mode
        to find the boundary of safely normalizable text despite possible
        text additions.

        There is one such property per normalization form.
        These properties are computed as follows - an inert character is:
        a) unassigned, or ALL of the following:
        b) of combining class 0.
        c) not decomposed by this normalization form.
        AND if NFC or NFKC,
        d) can never compose with a previous character.
        e) can never compose with a following character.
        f) can never change if another character is added.
           Example: a-breve might satisfy all but f, but if you
           add an ogonek it changes to a-ogonek + breve

        See also com.ibm.text.UCD.NFSkippable in the ICU4J repository,
        and icu/source/common/unormimp.h .
        draft ICU 3.0 */
    UTF32_NFD_INERT,
    /** Binary property NFKD_Inert.
        ICU-specific property for characters that are inert under NFKD,
        i.e., they do not interact with adjacent characters.
        Used for example in normalizing transforms in incremental mode
        to find the boundary of safely normalizable text despite possible
        text additions.
        \see UTF32_NFD_INERT
        draft ICU 3.0 */
    UTF32_NFKD_INERT,
    /** Binary property NFC_Inert.
        ICU-specific property for characters that are inert under NFC,
        i.e., they do not interact with adjacent characters.
        Used for example in normalizing transforms in incremental mode
        to find the boundary of safely normalizable text despite possible
        text additions.
        \see UTF32_NFD_INERT
        draft ICU 3.0 */
    UTF32_NFC_INERT,
    /** Binary property NFKC_Inert.
        ICU-specific property for characters that are inert under NFKC,
        i.e., they do not interact with adjacent characters.
        Used for example in normalizing transforms in incremental mode
        to find the boundary of safely normalizable text despite possible
        text additions.
        \see UTF32_NFD_INERT
        draft ICU 3.0 */
    UTF32_NFKC_INERT,
    /** Binary Property Segment_Starter.
        ICU-specific property for characters that are starters in terms of
        Unicode normalization and combining character sequences.
        They have ccc=0 and do not occur in non-initial position of the
        canonical decomposition of any character
        (like " in NFD(a-umlaut) and a Jamo T in an NFD(Hangul LVT)).
        ICU uses this property for segmenting a string for generating a set of
        canonically equivalent strings, e.g. for canonical closure while
        processing collation tailoring rules.
        draft ICU 3.0 */
    UTF32_SEGMENT_STARTER,
    /** One more than the last constant for binary Unicode properties. stable ICU 2.1 */
    UTF32_BINARY_LIMIT,

    /** Enumerated property Bidi_Class.
        Same as u_charDirection, returns UCharDirection values. stable ICU 2.2 */
    UTF32_BIDI_CLASS=0x1000,
    /** First constant for enumerated/integer Unicode properties. stable ICU 2.2 */
    UTF32_INT_START=UTF32_BIDI_CLASS,
    /** Enumerated property Block.
        Same as ublock_getCode, returns UBlockCode values. stable ICU 2.2 */
    UTF32_BLOCK,
    /** Enumerated property Canonical_Combining_Class.
        Same as u_getCombiningClass, returns 8-bit numeric values. stable ICU 2.2 */
    UTF32_CANONICAL_COMBINING_CLASS,
    /** Enumerated property Decomposition_Type.
        Returns UDecompositionType values. stable ICU 2.2 */
    UTF32_DECOMPOSITION_TYPE,
    /** Enumerated property East_Asian_Width.
        See http://www.unicode.org/reports/tr11/
        Returns UEastAsianWidth values. stable ICU 2.2 */
    UTF32_EAST_ASIAN_WIDTH,
    /** Enumerated property General_Category.
        Same as u_charType, returns UCharCategory values. stable ICU 2.2 */
    UTF32_GENERAL_CATEGORY,
    /** Enumerated property Joining_Group.
        Returns UJoiningGroup values. stable ICU 2.2 */
    UTF32_JOINING_GROUP,
    /** Enumerated property Joining_Type.
        Returns UJoiningType values. stable ICU 2.2 */
    UTF32_JOINING_TYPE,
    /** Enumerated property Line_Break.
        Returns ULineBreak values. stable ICU 2.2 */
    UTF32_LINE_BREAK,
    /** Enumerated property Numeric_Type.
        Returns UNumericType values. stable ICU 2.2 */
    UTF32_NUMERIC_TYPE,
    /** Enumerated property Script.
        Same as uscript_getScript, returns UScriptCode values. stable ICU 2.2 */
    UTF32_SCRIPT,
    /** Enumerated property Hangul_Syllable_Type, new in Unicode 4.
        Returns UHangulSyllableType values. draft ICU 2.6 */
    UTF32_HANGUL_SYLLABLE_TYPE,
    /** Enumerated property NFD_Quick_Check.
        Returns UNormalizationCheckResult values. draft ICU 3.0 */
    UTF32_NFD_QUICK_CHECK,
    /** Enumerated property NFKD_Quick_Check.
        Returns UNormalizationCheckResult values. draft ICU 3.0 */
    UTF32_NFKD_QUICK_CHECK,
    /** Enumerated property NFC_Quick_Check.
        Returns UNormalizationCheckResult values. draft ICU 3.0 */
    UTF32_NFC_QUICK_CHECK,
    /** Enumerated property NFKC_Quick_Check.
        Returns UNormalizationCheckResult values. draft ICU 3.0 */
    UTF32_NFKC_QUICK_CHECK,
    /** Enumerated property Lead_Canonical_Combining_Class.
        ICU-specific property for the ccc of the first code point
        of the decomposition, or lccc(c)=ccc(NFD(c)[0]).
        Useful for checking for canonically ordered text;
        see UNORM_FCD and http://www.unicode.org/notes/tn5/#FCD .
        Returns 8-bit numeric values like UTF32_CANONICAL_COMBINING_CLASS. draft ICU 3.0 */
    UTF32_LEAD_CANONICAL_COMBINING_CLASS,
    /** Enumerated property Trail_Canonical_Combining_Class.
        ICU-specific property for the ccc of the last code point
        of the decomposition, or tccc(c)=ccc(NFD(c)[last]).
        Useful for checking for canonically ordered text;
        see UNORM_FCD and http://www.unicode.org/notes/tn5/#FCD .
        Returns 8-bit numeric values like UTF32_CANONICAL_COMBINING_CLASS. draft ICU 3.0 */
    UTF32_TRAIL_CANONICAL_COMBINING_CLASS,
    /** One more than the last constant for enumerated/integer Unicode properties. stable ICU 2.2 */
    UTF32_INT_LIMIT,

    /** Bitmask property General_Category_Mask.
        This is the General_Category property returned as a bit mask.
        When used in u_getIntPropertyValue(c), same as U_MASK(u_charType(c)),
        returns bit masks for UCharCategory values where exactly one bit is set.
        When used with u_getPropertyValueName() and u_getPropertyValueEnum(),
        a multi-bit mask is used for sets of categories like "Letters".
        Mask values should be cast to uint32_t.
        stable ICU 2.4 */
    UTF32_GENERAL_CATEGORY_MASK=0x2000,
    /** First constant for bit-mask Unicode properties. stable ICU 2.4 */
    UTF32_MASK_START=UTF32_GENERAL_CATEGORY_MASK,
    /** One more than the last constant for bit-mask Unicode properties. stable ICU 2.4 */
    UTF32_MASK_LIMIT,

    /** Double property Numeric_Value.
        Corresponds to u_getNumericValue. stable ICU 2.4 */
    UTF32_NUMERIC_VALUE=0x3000,
    /** First constant for double Unicode properties. stable ICU 2.4 */
    UTF32_DOUBLE_START=UTF32_NUMERIC_VALUE,
    /** One more than the last constant for double Unicode properties. stable ICU 2.4 */
    UTF32_DOUBLE_LIMIT,

    /** String property Age.
        Corresponds to u_charAge. stable ICU 2.4 */
    UTF32_AGE=0x4000,
    /** First constant for string Unicode properties. stable ICU 2.4 */
    UTF32_STRING_START=UTF32_AGE,
    /** String property Bidi_Mirroring_Glyph.
        Corresponds to u_charMirror. stable ICU 2.4 */
    UTF32_BIDI_MIRRORING_GLYPH,
    /** String property Case_Folding.
        Corresponds to u_strFoldCase in ustring.h. stable ICU 2.4 */
    UTF32_CASE_FOLDING,
    /** String property ISO_Comment.
        Corresponds to u_getISOComment. stable ICU 2.4 */
    UTF32_ISO_COMMENT,
    /** String property Lowercase_Mapping.
        Corresponds to u_strToLower in ustring.h. stable ICU 2.4 */
    UTF32_LOWERCASE_MAPPING,
    /** String property Name.
        Corresponds to u_charName. stable ICU 2.4 */
    UTF32_NAME,
    /** String property Simple_Case_Folding.
        Corresponds to u_foldCase. stable ICU 2.4 */
    UTF32_SIMPLE_CASE_FOLDING,
    /** String property Simple_Lowercase_Mapping.
        Corresponds to u_tolower. stable ICU 2.4 */
    UTF32_SIMPLE_LOWERCASE_MAPPING,
    /** String property Simple_Titlecase_Mapping.
        Corresponds to u_totitle. stable ICU 2.4 */
    UTF32_SIMPLE_TITLECASE_MAPPING,
    /** String property Simple_Uppercase_Mapping.
        Corresponds to u_toupper. stable ICU 2.4 */
    UTF32_SIMPLE_UPPERCASE_MAPPING,
    /** String property Titlecase_Mapping.
        Corresponds to u_strToTitle in ustring.h. stable ICU 2.4 */
    UTF32_TITLECASE_MAPPING,
    /** String property Unicode_1_Name.
        Corresponds to u_charName. stable ICU 2.4 */
    UTF32_UNICODE_1_NAME,
    /** String property Uppercase_Mapping.
        Corresponds to u_strToUpper in ustring.h. stable ICU 2.4 */
    UTF32_UPPERCASE_MAPPING,
    /** One more than the last constant for string Unicode properties. stable ICU 2.4 */
    UTF32_STRING_LIMIT,

    /** Represents a nonexistent or invalid property or property value. stable ICU 2.4 */
    UTF32_INVALID_CODE = -1
} UTF32_Property ;

/* NOTE: These enumerations are copied directly from ICU uchar.h and
   renamed with the prefix UTF32_ rather than U_. They MUST match the
   enumerations in that header file exactly. This means the
   enumerations can be passed directly through to the ICU routines
   without going through any mapping. At the same time, we maintain a
   layer over ICU so that in theory, we could replace the Unicode
   backend with a different library. */
/** Unicode character category constants. */
typedef enum UTF32_CharCategory
{
    /** See note !!.  Comments of the form "Cn" are read by genpname. */

    /** Non-category for unassigned and non-character code points. stable ICU 2.0 */
    UTF32_UNASSIGNED              = 0,
    /** Cn "Other, Not Assigned (no characters in [UnicodeData.txt] have this property)" (same as UTF32_UNASSIGNED!) stable ICU 2.0 */
    UTF32_GENERAL_OTHER_TYPES     = 0,
    /** Lu stable ICU 2.0 */
    UTF32_UPPERCASE_LETTER        = 1,
    /** Ll stable ICU 2.0 */
    UTF32_LOWERCASE_LETTER        = 2,
    /** Lt stable ICU 2.0 */
    UTF32_TITLECASE_LETTER        = 3,
    /** Lm stable ICU 2.0 */
    UTF32_MODIFIER_LETTER         = 4,
    /** Lo stable ICU 2.0 */
    UTF32_OTHER_LETTER            = 5,
    /** Mn stable ICU 2.0 */
    UTF32_NON_SPACING_MARK        = 6,
    /** Me stable ICU 2.0 */
    UTF32_ENCLOSING_MARK          = 7,
    /** Mc stable ICU 2.0 */
    UTF32_COMBINING_SPACING_MARK  = 8,
    /** Nd stable ICU 2.0 */
    UTF32_DECIMAL_DIGIT_NUMBER    = 9,
    /** Nl stable ICU 2.0 */
    UTF32_LETTER_NUMBER           = 10,
    /** No stable ICU 2.0 */
    UTF32_OTHER_NUMBER            = 11,
    /** Zs stable ICU 2.0 */
    UTF32_SPACE_SEPARATOR         = 12,
    /** Zl stable ICU 2.0 */
    UTF32_LINE_SEPARATOR          = 13,
    /** Zp stable ICU 2.0 */
    UTF32_PARAGRAPH_SEPARATOR     = 14,
    /** Cc stable ICU 2.0 */
    UTF32_CONTROL_CHAR            = 15,
    /** Cf stable ICU 2.0 */
    UTF32_FORMAT_CHAR             = 16,
    /** Co stable ICU 2.0 */
    UTF32_PRIVATE_USE_CHAR        = 17,
    /** Cs stable ICU 2.0 */
    UTF32_SURROGATE               = 18,
    /** Pd stable ICU 2.0 */
    UTF32_DASH_PUNCTUATION        = 19,
    /** Ps stable ICU 2.0 */
    UTF32_START_PUNCTUATION       = 20,
    /** Pe stable ICU 2.0 */
    UTF32_END_PUNCTUATION         = 21,
    /** Pc stable ICU 2.0 */
    UTF32_CONNECTOR_PUNCTUATION   = 22,
    /** Po stable ICU 2.0 */
    UTF32_OTHER_PUNCTUATION       = 23,
    /** Sm stable ICU 2.0 */
    UTF32_MATH_SYMBOL             = 24,
    /** Sc stable ICU 2.0 */
    UTF32_CURRENCY_SYMBOL         = 25,
    /** Sk stable ICU 2.0 */
    UTF32_MODIFIER_SYMBOL         = 26,
    /** So stable ICU 2.0 */
    UTF32_OTHER_SYMBOL            = 27,
    /** Pi stable ICU 2.0 */
    UTF32_INITIAL_PUNCTUATION     = 28,
    /** Pf stable ICU 2.0 */
    UTF32_FINAL_PUNCTUATION       = 29,
    /** One higher than the last enum UCharCategory constant. stable ICU 2.0 */
    UTF32_CHAR_CATEGORY_COUNT
} UTF32_CharCategory;

/**
 * \brief Check a binary Unicode property for a code point.
 *
 * This interfaces directly to u_hasBinaryProperty in uchar.h from ICU
 * 2.1. See uchar.h for documentation.
 */
HqBool unicode_has_binary_property(UTF32 c,
                                   UTF32_Property which,
                                   HqBool *error_occured ) ;

/**
 * \brief Returns the general category value for the code point.
 *
 * This interfaces directly to u_charType in uchar.h from ICU 2.1. See
 * uchar.h for documentation.
 */
int8 unicode_char_type(UTF32 c,
                       HqBool *error_occured ) ;

/** \} */

#ifdef __cplusplus
}
#endif

#endif /* Protection from multiple inclusion */

/*
Log stripped */
