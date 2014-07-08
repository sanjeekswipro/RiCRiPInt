/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWSTRING_H__
#define __FWSTRING_H__

/* $HopeName: HQNframework_os!export:fwstring.h(EBDSDK_P.1) $
 * FrameWork External Internationalised String interface
 *
* Log stripped */

/* ----------------------- Overview ---------------------------------------- */

/* Encoding
 * ========
 * The framework uses a multibyte rather than a wide character encoding for
 * strings. This is allowed to be both platform and language dependent to
 * allow the maximum use of the native platform formats.
 * A native platform encoding for the current language is used if it satisfies
 * the following conditions:
 * 1) When treated as a sequence of bytes, the first zero byte found always
 *    marks the end of the string.
 * 2) The bytes 0x20 to 0x7e and \n, \r, \t are single byte characters.
 * 3) If the application can encounter characters which are not representable
 *    in this encoding then it must be possible to reserve an escape byte,
 *    which will be used to encode these as Unicode in the following three
 *    bytes, using a base 64 encoding using - 0-9 A-Z _ a-z.
 *    This will also be used to encode the escape byte itself.
 *    See also fwunicod.h
 *
 * Invalid encodings
 * =================
 * FwStr functions handle and preserve invalid multibyte encodings eg:
 *  a/ illegal    - a lead byte followed by an inappropriate trail byte
 *  b/ incomplete - a lead byte with too few trail bytes before string end
 * Each possible invalid encoding is assigned an FwTextCharacter by taking the
 * first byte and making it an int32, with the sign bit set. Consuming only
 * one byte of invalid encodings maximises the chances of resynching, and
 * minimises the number of invalid encodings that need to be represented.
 *
 * HOWEVER CASE b/ ABOVE CAN LEAD TO UNEXPECTED (BUT WELL DEFINED)
 * RESULTS WHEN EDITING STRINGS, AS THE BYTE(S) PLACED AFTER THE
 * INCOMPLETE LEAD BYTE, MAY FORM A COMPLETE CHARACTER, RESULTING IN
 * LOSS OF SYNCHRONIZATION FOR A WHILE IN THE ENCODING OF THE REST OF
 * THE STRING. THERE IS NO GENERAL SOLUTION TO THIS PROBLEM, AND THE
 * CLIENT NEEDS TO BE AWARE OF IT.
 *
 * This is most likely to happen with arbitrary byte input as strings, eg
 * platforms whose file systems store filenames simply as byte sequences.
 *
 *
 * Length and Size
 * ===============
 * The Length of a string is defined to be the number of bytes it contains,
 * excluding any zero terminator.
 * The Size of a string is defined to be the number of bytes it contains,
 * including any zero terminator.
 */

/* ----------------------- Includes ---------------------------------------- */

#include <string.h>     /* strlen */
#include <stdarg.h>     /* vsprintf */

/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
#include "fxstring.h"   /* Platform Dependent */


#ifdef __cplusplus
extern "C" {
#endif
#include "fwvector.h"


/* ----------------------- Macros ------------------------------------------ */

#ifdef DEBUG_BUILD
/* Include this define to get lots of debug checking */
/*
#define FWSTRING_DEBUG
*/
#endif /* DEBUG_BUILD */


/* Check that something else hasn't defined UVM or UVS. This is 
 * likely because compounds that are not built on FrameWork may
 * still be parsed looking for localizable strings. We must 
 * catch these cases.
 */
#if defined(UVM) || defined(UVS)
HQFAIL_AT_COMPILE_TIME()
#endif

/* U(ser)V(isible)M(essage) is the default way for clients to mark
 * literal strings used as sprintf templates in their source code that
 * need translation (allowing for variable field substitution).  This is
 * only a suggestion, clients are free to use their own mechanism.
 */
#define UVM( stringConstant ) (FWSTR_TEXTSTRING(stringConstant))

/* Special version of UVM for use with inline arrays, i.e. FwID_Text
 * This is required because FwID_Text foo = "baa" is OK,
 * but FwID_Text foo = (uint8 *)"baa" is not.
 */
#define UVMA( stringConstant )  stringConstant

/* U(ser)V(isible)S(tring) is the default way for clients to mark fixed
 * literal strings in their source code that need translation.  These
 * strings can be single words, phrases, or message fragments that have
 * a fixed translation.  This is only a suggestion, clients are free to
 * use their own mechanism.
 */
#define UVS( stringConstant ) (FWSTR_TEXTSTRING(stringConstant))

/* Special version of UVS for initialising inline arrays. */
#define UVSA( stringConstant )  stringConstant


/* 
 * Casts from FwTextBytes to chars and back
 */
 
#define FWSTR_CHAR_TO_BYTE(c) \
 HQASSERT_EXPR( ((int32)(c) >= -128) && ((int32)(c) < 256), "Not char", (FwTextByte)(c) )
#define FWSTR_BYTE_TO_CHAR(b) \
 HQASSERT_EXPR( ((b) >= 0) && ((b) < 256), "Not byte", (char)(b) )
 

/* Use this to mark and cast C string constants which are to be interpreted as text strings */
#define FWSTR_TEXTSTRING(pbz) ((FwTextString)(pbz))

/* Special version of FWSTR_TEXTSTRING for initialising inline arrays. */
#define FWSTR_TEXTSTRINGA( stringConstant )  stringConstant


/* ----------------------- Types ------------------------------------------- */

/* Character encoding types */
typedef uint8           FwByte;         /* b - FwTextByte or FwRawByte */
typedef FwByte          FwTextByte;     /* tb - multibyte character encoding */
typedef FwByte          FwRawByte;      /* rb - no interpretation */

typedef FwByte *        FwString;       /* pbz - FwTextString or FwRawString */
typedef FwTextByte *    FwTextString;   /* ptbz */
typedef FwRawByte *     FwRawString;    /* prbz */

typedef int32           FwTextCharacter;/* tc - id for (multi)byte char */
/* tc  > 0 <=> valid multibyte or escape encoding, not terminator
 * tc == 0 <=> the zero terminator
 * tc  < 0 <=> invalid multibyte encoding, see Invalid encodings comment above
 */

#define FWSTR_TC_FLAG_INVALID   ( 0x1 << 31)
#define FWSTR_TC_VALID( tc )    ( tc >= 0 )

#define FWSTR_TC_FLAG_ESCAPED   ( 0x1 << 30 )   /* Escape and base 64 used */
#define FWSTR_TC_ESCAPED( tc )  ( (tc) >= FWSTR_TC_FLAG_ESCAPED )


/* FwStrRecord is used to accumulate string output, handling the messy details
 * of either extending the buffer, or truncating output.
 * fFixed FALSE => extensible buffer allocated by FrameWork
 * fFixed TRUE  => fixed buffer provided by client
 */
typedef struct FwStrRecord
{
  FwString      pbz;            /* Output buffer, can hold Raw or Text bytes */
  FwByte *      pbLast;         /* Pointer to last byte in buffer */
  FwByte *      pbTerminator;   /* Pointer to current terminator position */
  uint8         fFixed;         /* TRUE <=> truncate rather than extend */
  
  /* Once TRUE further output dropped. Flag may be reset by
   * FwStrRecordShorten (see below).
   */
  uint8         fTruncated;     
} FwStrRecord;

/* Access macros, use thes in case ever become real functions */
#define FwStrRecordGetBuffer( pRecord ) ((pRecord)->pbz)
#define FwStrRecordGetLast( pRecord ) ((pRecord)->pbLast)
#define FwStrRecordGetTerminator( pRecord ) ((pRecord)->pbTerminator)
#define FwStrRecordIsFixed( pRecord ) ((pRecord)->fFixed)
#define FwStrRecordIsTruncated( pRecord ) ((pRecord)->fTruncated)

/* 
 * Simple utility functions for records
 */

/* Get length of string in record */
#define FwStrRecordStringLength( pRecord ) \
  ( (uint32)((pRecord)->pbTerminator - (pRecord)->pbz) )

/* Get space remaining */
#define FwStrRecordUnusedSize( pRecord ) \
  ( (size_t)((pRecord)->pbLast - (pRecord)->pbTerminator) )

/* Maximum space available */
#define FwStrRecordMaxSize( pRecord ) \
  ( (size_t)((pRecord)->pbLast - (pRecord)->pbz) + 1 )


/* Standard identifier name is 32 characters */
/*
 * IDString will be replaced by FwID_ASCII or FwID_Text as appropriate
 * ID_STRING_SIZE will be replaced by FW_ID_SIZE
*/
#define FW_ID_SIZE      32

/* Identifier composed of simple 7 bit ASCII */
typedef char            FwID_ASCII[ FW_ID_SIZE ];

/* Identifier composed of (potentially multibyte) international characters */
typedef FwTextByte      FwID_Text[ FW_ID_SIZE ];


/* Longer arrays e.g. for colorant names from plugins etc */
#define FW_ID2_SIZE     64

/* Array composed of simple 7 bit ASCII */
typedef char            FwID2_ASCII[ FW_ID2_SIZE ];

/* Array composed of (potentially multibyte) international characters */
typedef FwTextByte      FwID2_Text[ FW_ID2_SIZE ];


/* Context for the library */
typedef struct FwStrContext {
  FwTextString                  ptbzLocaleID;        /* Locale to force use of, if any (can be NULL) */
#ifdef FW_PLATFORM_STR_CONTEXT
  FwPlatformStrContext          platform;
#endif
} FwStrContext;


/* ----------------------- Functions --------------------------------------- */
/*
  Takes a string and a standard separator and produces a vector consisting of
  the strings btn the separators. If the seperator is NULL, it does nothing
  Useful for CSV fields.
  Looks like Perl :-)
*/
extern void FwStrTextSplit( FwTextString ptbz, FwTextString sep, FwVector * pvBuff );

/* interface to FwStrTextSplit  */
extern void FwStrTextSplitByChar( FwTextString ptbz, FwTextCharacter sep, FwVector * pvBuff );

/*
 *
 * pvsb: a vector of zero-terminated strings, starting at 0
 * n: the number of elements in pvsb to use, starting at 0 in the vector
 * (gets rounded to maximum size of the vector if <=0 or >max)
 * sep: the separator to join the elements of the vector ('' if no joining separator)
 * record: the record in which to put the joined string
 *
 */
extern int32 FwStrTextJoin( FwVector *pvsb, int32 n, FwTextString sep, FwStrRecord * record);

/* interface to FwStrTextJoin */
extern int32 FwStrTextJoinByChar( FwVector *pvsb, int32 n, FwTextCharacter sep, FwStrRecord * record);

/* 
 * Return the code page as a string.
 */
extern FwTextString FwStrGetDefaultCodePage(void);

/*
 * Functions for managing a FwStrRecord
 */

/* Create an extensible FwStrRecord, this allocates a buffer for pbz, so need
 * to call FwStrRecordClose when finished.  Buffer initially contains empty
 * string.
 */
extern void FwStrRecordOpen( FwStrRecord * pRecord );

/* Open a growable record, with a given initial size.
 * This may be more efficient, as it may avoid reallocations
 */
extern void FwStrRecordOpenSize
 ( FwStrRecord * pRecord, size_t cbSize );

/* Initialise a FwStrRecord for appending to existing contents of existing
 * fixed size buffer.
 * WARNING: The size is in bytes, so be careful not to open a buffer 
 * ending in the middle of a multi-byte character, which is likely to destroy
 * a message.
 */
extern void FwStrRecordOpenOn
 ( FwStrRecord * pRecord, FwByte * pb, size_t cbSize );

/* Finish with a FwStrRecord when you are interested in preserving the
 * buffer contents. For an extensible buffer the buffer is reallocated
 * to the minimum size needed. For a fixed buffer this is an empty operation
 * and is optional. It returns the buffer.
 */
extern FwString FwStrRecordClose( FwStrRecord * pRecord );

/* Finish with a FwStrRecord when you are not interested in preserving
 * the buffer contents. For an extensible buffer the buffer is
 * freed. For a fixed buffer this is an empty operation and is
 * optional.
 */
extern void FwStrRecordAbandon( FwStrRecord * pRecord );

/* Shorten a record (fixed or extensible) to a given length, returning the 
 * actual length of the record after shortening.
 * When the requested shortened length of the record is less than or equal
 * to its current length, the truncated flag is reset (as this is a 
 * deliberate truncation).
 * WARNING: The length is in bytes, so be careful not to truncate a record
 * in the middle of a multi-byte character, which is likely to destroy
 * a message. Such a truncation will raise an assert.
 */
extern size_t FwStrRecordShorten( FwStrRecord * pRecord, size_t cbLength );

/* Utility function which creates an extensible FwStrRecord
 * if pRecord->pbz is NULL, or shortens it to 0 if pRecord->pbz
 * is not NULL
 */
extern void FwStrRecordOpenOrClear( FwStrRecord * pRecord );

/* Use to assert that a record is valid.
 */
#ifdef FWSTRING_DEBUG
extern void FcStrRecordAssert(FwStrRecord* pRecord);
#define FWSTR_RECORD_ASSERT( pRecord ) (FcStrRecordAssert((pRecord)))
#else /* !FWSTRING_DEBUG */
#define FWSTR_RECORD_ASSERT( pRecord ) EMPTY_STATEMENT()
#endif /* FWSTRING_DEBUG */


/*
 * Primitive functions for outputting to a FwStrRecord
 * ---------------------------------------------------
 * ALL THESE FUNCTIONS _APPEND_ TO THE EXISTING CONTENTS OF THE RECORD.
 * Other string output functions are implemented in terms of these.
 * If truncation is done, Text input is truncated at character boundaries,
 * and Raw input is truncated at byte boundaries.
 * All these return the lengths of the strings actually inserted in output buffer.
 */

/* Put a single text character */
extern uint32 FwStrPutTextCharacter( FwStrRecord * pRecord, FwTextCharacter tc);
 
/* Put a whole zero-terminated text string */
extern uint32 FwStrPutTextString( FwStrRecord * pRecord, FwTextString ptbz );

/* Put a text string of a given length
 * WARNING: The length is in bytes, so be careful not to truncate a record
 * in the middle of a multi-byte character, which is likely to destroy
 * a message.
 */
extern uint32 FwStrNPutTextString
 ( FwStrRecord * pRecord, FwTextByte * ptb, size_t cbLength );

/* Put a single byte */
extern uint32 FwStrPutRawByte( FwStrRecord * pRecord, FwRawByte rb );

/* Put a whole zero-terminated byte string */
extern uint32 FwStrPutRawString( FwStrRecord * pRecord, FwRawString prbz );

/* Put a byte string of a given length */
extern size_t FwStrNPutRawString
  ( FwStrRecord * pRecord, FwRawByte * prb, size_t cbLength );

/* 
 * Extensions to sprintf:
 *   Positional format arguments.
 *    %[n]<format> means: the n'th (0-based) format on stack corresponds to
 *     this format.
 *     The square brackets come immediately after the %
 *     E.g. FwStrPrintf(&rcd, "%[1]s %[0]d", 3, "string") 
 *      -->  string 3       
 *          FwStrPrintf(&rcd, "%[1]s", FW_PRINTF_DUMMY_ARG, "string")
 *      -->  string
 *
 *   Branches: 
 *     Multiple strings can be stored within the same string. This provides
 *     a more compact form for messages, which is still useful for translation
 *     purposes.
 *     E.g. FwStrPrintf(&rcd, "%(branch1%|branch2%), 0)
 *       --> branch1
 *          FwStrPrintf(&rcd, "%(branch1%|branch2%), 1)
 *       --> branch2
 *     Arguments for branches must come before all other arguments.
 *     Branches may be nested, in which case the arguments are pulled on stack in
 *     a depth first order.
 *     Positional arguments (see above) have their base 0 as directly after 
 *     the final branch argument
 */

/* Dummy argument for FwStrPrintf (see above) */
#define FW_PRINTF_DUMMY   ((void*)((uintptr_t)0xFE9001C1))

/* sprintf, returns number of bytes actually inserted in output. 
 * Numeric values are output as for C locale
 * fMessage should set if the string is intended for later translation, this
 * allows control characters to be inserted to disambiguate difficult cases
 * such as two consecutive %s.
 */
extern uint32 FwStrPrintf
 ( FwStrRecord * pRecord, int32 fMessage, FwTextString ptbzFormat, ... );

/* As FwStrPrintf but numeric values are output in localised form
 */
extern uint32 FwStrPrintfLocal
 ( FwStrRecord * pRecord, int32 fMessage, FwTextString ptbzFormat, ... );

/* As vsprintf, returns number of bytes actually inserted in output.
 * fMessage should set if the string is intended for later translation, this
 * allows control characters to be inserted to disambiguate difficult cases
 * such as two consecutive %s.
 */
extern uint32 FwStrVPrintf
 ( FwStrRecord * pRecord, int32 fMessage, FwTextString ptbzFormat, va_list args );

/* As FwStrVPrintf but numeric values are output in localised form
 */
extern uint32 FwStrVPrintfLocal
 ( FwStrRecord * pRecord, int32 fMessage, FwTextString ptbzFormat, va_list args );


/*
 * Output an int32 to a string record
 */
extern uint32 FwStrPrintInt(
    FwStrRecord*          pRecord,                  /* Record to output to */
    int32                 value,                    /* Value to output */
    char                  type,                     /* Conversion type: d, i, o, x, X, u as for printf */
    int32                 nMinWidth,                /* Minimum width of output */
    int32                 nMinDigits,               /* Minimum no of digits in output */
    int32                 fLeftJustified,           /* TRUE <=> left justify value in output */
    int32                 fPadWithZero,             /* TRUE <=> pad output to required width with 0 (only valid if right-justified) */
    FwTextByte            tbPositiveChar,           /* Character to output before positive values (+ or ' ') */
    int32                 fUseCLocale,              /* TRUE <=> force use of "C" locale */
    int32                 fOptionalForm             /* TRUE <=> optional form (# flag) as for printf */
    );


/*
 * Output a 'promoted real' to a string record
 */
extern uint32 FwStrPrintReal(
    FwStrRecord*          pRecord,                  /* Record to output to */
    promoted_real         value,                    /* Value to output */
    char                  type,                     /* Conversion type: e, E, f, g, G as for printf */
    int32                 nMinWidth,                /* Minimum width of output */
    int32                 nPrecision,               /* Precision, as for printf */
    int32                 fLeftJustified,           /* TRUE <=> left justify value in output */
    int32                 fPadWithZero,             /* TRUE <=> pad output to required width with 0 (only valid if right-justified) */
    FwTextByte            tbPositiveChar,           /* Character to output before positive values (+ or ' ') */
    int32                 fUseCLocale,              /* TRUE <=> force use of "C" locale */
    int32                 fOptionalForm             /* TRUE <=> optional form (# flag) as for printf */
    );


/* Convenience functions
 * All return number of bytes actually inserted in output
 */

/* Equivalent to 'FwStrPrintf( pRecord, FALSE, FWSTR_TEXTSTRING( "%d" ), value );'
 */
extern uint32 FwStrPutInt
 ( FwStrRecord * pRecord, int32 value );

/* As FwStrPutInt but output is in localised form
 */
extern uint32 FwStrPutIntLocal
 ( FwStrRecord * pRecord, int32 value );

/* Equivalent to 'FwStrPrintf( pRecord, FALSE, FWSTR_TEXTSTRING( "%f" ), value );' */
extern uint32 FwStrPutFloat
 ( FwStrRecord * pRecord, double value );

/* Equivalent to 'FwStrPrintf( pRecord, FALSE, FWSTR_TEXTSTRING( "%.'nPrecision'g" ), value );' */
extern uint32 FwStrPutReal
 ( FwStrRecord * pRecord, double value, int32 nPrecision );


/*
 * FwTextString utilities
 * ----------------------
 */

/* String access */

/* Get first FwTextCharacter in an FwTextString, and advance input pointer */
extern FwTextCharacter FwStrGetTextCharacter( FwTextByte ** pptb );

/* Get previous FwTextCharacter in an FwTextString, & step input pointer back.
 * The most common use is to get the last character, so if *pptb == NULL it
 * behaves as if it was a pointer to the terminating zero instead.
 * If *pptb is at the start of the string, then it returns 0, and doesn't
 * change *pptb.
 */
extern FwTextCharacter FwStrGetPreviousTextCharacter
  ( FwTextString ptbzStart, FwTextByte** pptb );


/* String properties */

/* Count the number of FwTextCharacters in an FwTextString,
 * excluding the zero terminator.
 */
extern uint32 FwStrCountCharacters( FwTextString ptbz );

/* Count the number of bytes in an FwString, excluding the zero terminator */
#define FwStrCountBytes( pbz ) \
  ( (uint32) strlen( (const char*)(pbz) ) )


/* String predicates */

/* Is string empty */
#define FwStrIsEmpty(ptbz) HQASSERT_EXPR(NULL != ptbz, "NULL", (0 == *ptbz))
  
/* Compare two strings for equality of contents */
#define FwStrEqual( ptbz1, ptbz2 ) \
  ( strcmp((const char*)(ptbz1), (const char*)(ptbz2)) == 0 )

/* Compare two strings for equality of contents, up to given length */
#define FwStrNEqual( ptbz1, ptbz2, length ) \
  ( strncmp((const char*)(ptbz1), (const char*)(ptbz2), (length)) == 0 )

/* Compare two strings case-insensitively for equality of contents */
extern int32 FwStrIEqual( FwTextString ptbz1, FwTextString ptbz2 );

/* Compare two strings case-insensitively for equality of contents, up to given length */
extern int32 FwStrNIEqual( FwTextString ptbz1, FwTextString ptbz2, uint32 cbLength );

/* return TRUE <=> all characters are FWSTR_TC_VALID */
extern int32 FwStrValidate( FwTextString ptbz );

/* Compares two strings, return value as strcmp
 * NOTE: Use FwStrEqual where possible, because FwStrCompare
 * can be expensive.
 */
extern int32 FwStrCompare
  ( FwTextString ptbz1, FwTextString ptbz2 );

/* Compares two strings up to a given length, return value as strcmp */
extern int32 FwStrNCompare
  ( FwTextByte* ptb1, FwTextByte* ptb2, uint32 cbLength );


/* String searching */

/* As strchr, find first occurrence of character */
extern FwTextString FwStrChr( FwTextString ptbz, FwTextCharacter tc );

/* As strchr, find last occurrence of character */
extern FwTextString FwStrRChr( FwTextString ptbz, FwTextCharacter tc );

/* As strtok, find a token in a string */
extern FwTextString FwStrTok
  (FwTextString* pptbz, FwTextString ptbDelimiters);


/* String copying */

/* Allocates a new copy exiting if memory exhausted.
 */
extern FwTextString FwStrDuplicate( FwTextString ptbz );

/* Allocates a new copy returning NULL if memory exhausted.
 */
extern FwTextString FwStrDuplicateFailNull( FwTextString ptbz );

/* If src too long asserts and truncates */
extern int32 FwStrNCpy( FwTextString ptbzDest, FwTextString ptbzSrc, uint32 sizeDest );

/* Convenience macro for use with inline array destinations */
/* Gives a 'divide by zero' warning if ptbzDest is a pointer - it has to be an array */
/* Cannot be used with arrays which are the same size as FwTextStrings - use FwStrNCpy directly in this case */

#define FwStrCpy( ptbzDest, ptbzSrc ) \
  FwStrNCpy( ptbzDest, ptbzSrc, HQASSERT_EXPR( sizeof( ptbzDest ) != sizeof( FwTextString), "Destination could be pointer", sizeof(ptbzDest) ) )

/* Numeric Conversion */
 
/* Convert a string in "C" locale format to a double, returning FALSE if the string
 * does not contain a valid number.
 */
extern int32 FwStrToD
  (FwTextString* pptbz, double* prResult);

/* Convert a string in to an int32, returning FALSE if the string
 * does not contain a valid number.
 */
extern int32 FwStrToL
  (FwTextString* pptbz, uint32 nBase, int32* pnResult);

/* Convert a string in to an uint32, returning FALSE if the string
 * does not contain a valid number.
 */
extern uint32 FwStrToUL
  (FwTextString* pptbz, uint32 nBase, uint32* pnResult);


/*
 * FwTextCharacter utilities
 */

extern FwTextCharacter FwStrToUpper( FwTextCharacter tc );

extern FwTextCharacter FwStrToLower( FwTextCharacter tc );

extern int32 FwStrIsWhiteSpace( FwTextCharacter tc );

extern FwTextCharacter FwStrSkipWhiteSpace( FwTextByte ** pptb);

extern int32 FwStrIsDigit( FwTextCharacter tc );

/*
 * Locale
 */

/* Fills in FwStrRecord with a string which identifies the locale the application is
 * running in. This string is platform-dependent.
 */
extern void FwStrGetLocaleIDString(FwStrRecord * pRecord);

/* Fills in FwStrRecord with a string which identifies the locale the application is
 * running in. The string uses ISO 639 & 3166 codes, e.g. "en_GB".
 */
extern void FwStrGetISOLocaleString(FwStrRecord * pRecord);

/* Remove trailing/leading CR, LF, space and tab from a string which is to be split
 * by sepToken. Note that 'trailing' and 'leading' refers to each token separately which will
 * be obtained by splitting later. See the following example (sepToken == '|')
 * In: ' \t LF CR Make CD |  \tmakecd.sh CR LF  '
 * Out:'Make CD|makecd.sh'
 */
extern void RemoveMultipleWhiteSpacesAndEOL(FwTextString *str, int sepToken);

/*
 * Low level functions
 */

/* Possible byte types */

/* Lead byte of an escape character, ie the escape byte */
#define FWSTR_BYTE_ESC_LEAD       BIT( 0 )

/* Lead byte of a multi-byte character */
#define FWSTR_BYTE_NON_ESC_LEAD   BIT( 1 )

/* Trail byte of an escaped character */
#define FWSTR_BYTE_ESC_TRAIL      BIT( 2 )

/* Trail byte of a platform's multi-byte character */
#define FWSTR_BYTE_NON_ESC_TRAIL  BIT( 3 )

/* Returns a bit field of the above, giving the possible types of a given
 * byte.
 */ 
extern uint32 FwStrGetByteType(FwTextByte tb);


#ifdef __cplusplus
}
#endif

#endif /* ! __FWSTRING_H__ */

/* eof fwstring.h */
