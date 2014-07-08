/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWUNICOD_H__
#define __FWUNICOD_H__

/* $HopeName: HQNframework_os!export:fwunicod.h(EBDSDK_P.1) $
 * FrameWork External, Platform Independent, Unicode interface
 *
* Log stripped */

/* Length and Size
 * ===============
 * As with fwstring.h length excludes zero terminator,
 * and size includes zero terminator.
 *
 * Lengths and sizes of FwTextString are measured in bytes.
 *
 * Lengths and sizes of Unicode strings are measured in Unicode chars,
 * ie the number of bytes would be twice as large.
 */

/* ----------------------- Includes ---------------------------------------- */

/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
#include "fxunicod.h"   /* Platform Dependent */

#include "fwstring.h"   /* FwTextString */

#ifdef __cplusplus
extern "C" {
#endif


/* ----------------------- Macros ------------------------------------------ */

/* substitution character used for invalid multi byte characters */
#define FW_UNICODE_UNKNOWN ( (FwUniChar) 0xFFFD )


/* ----------------------- Types ------------------------------------------- */

/* FwUniChar
 *
 * Each platform will typedef FwUniChar to be the most natural 16 bit integral
 * type to hold unicode characters.
 * The default when there is no such natural type is uint16.
 * Thus it is platform dependent whether FwUniChar is signed.
 */
#ifdef FX_UNI_CHAR
typedef FX_UNI_CHAR FwUniChar;
#else
typedef uint16      FwUniChar;
#endif

/* A Unicode string zero terminated unless explicitly stated otherwise */
typedef FwUniChar * FwUniString;


/* ----------------------- Functions --------------------------------------- */

/* Returns true <=> platform has support for conversion between FwTextString
 * and Unicode in both directions.
 * This does not imply the platform has a unicode interface to system calls.
 */
extern int32 FwUniSupported( void );


/******************************************************************************
* Conversions to FwTextString from Unicode                                    *
*                                                                             *
* Escape sequences are used where the underlying platform encoding cannot     *
* represent them, see fwstring.h                                              *
******************************************************************************/

/* Convert a FwUniChar to a FwTextCharacter
 */
extern FwTextCharacter FwUniCharToTextCharacter( FwUniChar uc );

/* Convert a zero terminated unicode string into a FwStrRecord.
 * Return the number of bytes inserted.
 */
extern uint32 FwStrPutUnicode( FwStrRecord * pRecord, FwUniString pucz );

/* Convert a unicode string of given length into a FwStrRecord
 * Return the number of bytes inserted.
 * Any zero Unicode char will cause premature termination of the input.
 */
extern uint32 FwStrNPutUnicode
( FwStrRecord * pRecord, FwUniChar * puc, uint32 cucInLength );

/* Convert a zero terminated unicode string into a newly allocated FwTextString
 * If pcbOutLength is non null *pcbOutLength is set to the length in bytes of
 * the returned FwTextString.
 */
extern FwTextString FwStrFromUnicode
( FwUniString pucz, uint32 * pcbOutLength );

/* Convert a unicode string of given length into a newly allocated FwTextString
 * Any zero Unicode char will cause premature termination of the input.
 * If pcbOutLength is non null *pcbOutLength is set to the length in bytes of
 * the returned FwTextString.
 */
extern FwTextString FwStrFromNUnicode
( FwUniChar * puc, uint32 cucInLength, uint32 * pcbOutLength );


/******************************************************************************
* Conversions to Unicode from FwTextString                                    *
*                                                                             *
* The behaviour when invalid encoding occur depends on fAllowInvalid.         *
* If FALSE it is considered an error and NULL returned.                       *
* If TRUE then FW_UNICODE_UKNOWN is substituted for each invalid character.   *
******************************************************************************/

/* Convert a zero terminated FwTextString into a newly allocated unicode string
 * If pcucOutLength is non null *pcucOutLength is set to the length in Unicode
 * characters of the returned FwTextString.
 */
extern FwUniString FwUniFromTextString
( int32 fAllowInvalid, FwTextString ptbz, uint32 * pcucOutLength  );

/* Convert a FwTextString of given length into a newly allocated unicode string
 * Any zero FwTextByte will cause premature termination of the input.
 * If pcucOutLength is non null *pcucOutLength is set to the length in Unicode
 * characters of the returned FwTextString.
 */
extern FwUniString FwUniFromNTextString
(
  int32        fAllowInvalid,
  FwTextByte * ptb,
  uint32       cbInLength,
  uint32 *     pcucOutLength
);

/* Convert a FwTextString of given length into a pre-allocated unicode string
 * Any zero FwTextByte will cause premature termination of the input.
 * Returns the number of Unicode characters written to puc (not including a
 * zero terminator, although one is written to the buffer).
 * The caller is responsible for ensuring the preallocated buffer is big
 * enough.
 */
extern uint32 FwUniFromNTextStringToBuffer
(
  int32        fAllowInvalid, /* whether to allow invalid characters */
  FwTextByte * ptb,           /* start of the FwTextString to convert */
  uint32       cbInLength,    /* length of the input in FwTextBytes */
  FwUniChar  * puc,           /* preallocated outbut buffer */
  uint32       cucAllocSize   /* size of the output buffer in FwUniChars */
);

/******************************************************************************
* Unicode Operations                                                          *
******************************************************************************/

/* return the number of unicode characters in a zero terminated unicode string
 * ie its length
 */
extern uint32 FwUniCountChars( FwUniChar * pucz );

/*
 * Duplicate a Unicode string.  Allocates a new copy, exiting if memory exhausted.
 */
extern FwUniString FwUniDuplicate( FwUniString ustring );

/******************************************************************************
* Filename Operations                                                         *
******************************************************************************/

/* As FwUniFromTextString(), but additionally does any filename format
 * conversion to make the FwUniString compatible with Host Server
 */
extern FwUniString FwUniFromFileName
( int32 fAllowInvalid, FwTextString ptbzFileName, int32 fIsDirectory, uint32 * pcucOutLength  );

/* As FwStrFromUnicode(), but additionally does any filename format
 * conversion to accept a FwUniString obtained via Host Server
 */
extern FwTextString FileNameFromUnicode
( FwUniString pucz, uint32 * pcbOutLength, int32 fIsDirectory );

/* As FwStrPutUnicode(), but additionally does any filename format
 * conversion to accept a FwUniString obtained via Host Server
 */
extern uint32 FileNamePutUnicode( FwStrRecord * pRecord, FwUniString pucz, int32 fIsDirectory );

#ifdef __cplusplus
}
#endif

#endif /* ! __FWUNICOD_H__ */


/* eof fwunicod.h */
