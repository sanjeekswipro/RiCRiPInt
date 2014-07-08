/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWTRANSL_H__
#define __FWTRANSL_H__

/*
 * $HopeName: HQNframework_os!export:fwtransl.h(EBDSDK_P.1) $
 * FrameWork External Translation Database Interface
 *
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */
/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
                        /* No Platform dependent */

#include "fwstring.h"   /* FwTextString */


/* ----------------------- Defines ----------------------------------------- */

/* FwTranslMessage() return values */
enum
{
  FwTranslationNone                       = 0,
  FwTranslated                            = 1,
  FwTranslMarkersStripped                 = 2
};

/* ----------------------- Types ------------------------------------------- */

/****************
* Forward Types *
****************/
struct FwErrorState;
struct FwClassRecord;

/* Context for the library */
typedef struct FwTranslContext
{
  int32                         _dummy_;        /* none yet */

} FwTranslContext;


/* ----------------------- Functions --------------------------------------- */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

/* Add a message translation file to the translation database.
 * Returns TRUE if file is in the proper format and was loaded.
 * File is kept open while loaded, unless it is preloaded.
 */
extern int32 FwTranslLoadMessageFile
(
  FwTextString           ptbzFileName,
  struct FwClassRecord * pStreamIOClass,
  struct FwErrorState  * pErrState
);

/* Removes the translations obtained from a message file from
 * the translation database, if none of them are in use or if
 * the fForce flag is TRUE.
 * Returns TRUE iff file was successfully unloaded.
 */
extern int32 FwTranslUnloadMessageFile
(
  FwTextString          ptbzFileName,
  int32                 fForce,
  struct FwErrorState * pErrState
);

/* Looks up a fixed string translation pair.
 * If english search string is found, returns TRUE and stores pointers
 * to internal copies of the english and localised strings (incrementing
 * the translation pair's lock count), otherwise returns FALSE.
 * Call FwTranslReleaseFixedPair to unlock returned strings
 */
extern int32 FwTranslLookupFixedPair
(
  FwTextString      ptbzEnglish,
  FwTextString    * pptbzFixedEnglish,
  FwTextString    * pptbzFixedLocal
);

/* As FwTranslLookupFixedPair but only translates the first cLength bytes
 * from ptbzEnglish
 */
extern int32 FwTranslLookupFixedPairN
(
  FwTextString      ptbzEnglish,
  uint32            cLength,
  FwTextString    * pptbzFixedEnglish,
  FwTextString    * pptbzFixedLocal
);

/* Decrements the lock count of a translation pair obtained from FwTranslLookupFixedPair
 * Pass the fixed English string as obtained from FwTranslLookupFixedPair
 */
extern void FwTranslReleaseFixedPair(FwTextString ptbzFixedEnglish);

/* Translates the input message from English to local language
 * if possible.
 * Returns:
 *   FwTranslated            if translation performed - the translation is appended to pRecOutput
 *   FwTranslationNone       if no translation could be performed - ptbzMessage is appended 
 *                             to pRecOutput
 *   FwTranslMarkersStripped if no translation could be performed but ptbzMessage contains
 *                             FCSTR_END_MARKERs - ptbzMessage is stripped of FCSTR_END_MARKERs
 *                             and appended to pRecOutput
 *
 * Translation succeeds if ptbzMessage differs from a known translatable message
 * only by extra leading and/or trailing \n's.  The leading/trailing \n's are added
 * to the translation, so are preserved.
 */
extern int32 FwTranslMessage
(
  FwTextString      ptbzMessage,
  FwStrRecord     * pRecOutput
);

/* As FwTranslMessage() but if message cannot be translated in its entirity
 * an attempt is made to translate it line by line.
 * Returns:
 *   FwTranslated            if translation performed in whole or in part
 *   FwTranslationNone       if no translation could be performed
 *   FwTranslMarkersStripped if no translation could be performed but ptbzMessage contains
 *                             FCSTR_END_MARKERs
 */
extern int32 FwTranslMessageLineByLine
(
  FwTextString      ptbzMessage,
  FwStrRecord     * pRecOutput
);

/* Fills in the font information as specified in the header of the translation file
 *
 * Sets *pptbzFontName to NULL if the default font is to be used
 * Sets *pFontSize to 0 if the default font size is to be used
 */
extern void FwTranslGetFontInfo
(
  FwTextString    * pptbzFontName,
  int32           * pFontSize
);

/* Check if the message file ptbzMessFile is present or not. A message file is 
 * present if and only if the message file exists and the locale code in the file
 * is the same as the message file name.
 */
extern int32 FwTranslMessageFilePresent
(
  FwTextString            ptbzMessFile,
  struct FwClassRecord  * pStreamIOClass
);

/* Check if the message file ptbzMessFile provides the LDK feature number.
 * Returns TRUE if it does, and sets *pnKeyFeature.
 */
extern int32 FwTranslMessageFileGetFeatureNumber
(
  FwTextString            ptbzMessFile,
  struct FwClassRecord  * pStreamIOClass,
  uint32                * pnKeyFeature
);

/* Copy ptbz to pRecOutput stripping any FCSTR_END_MARKERs
 */
extern void FwTranslStripMarkers
 (
  FwTextString  ptbz,
  FwStrRecord * pRecOutput
 );

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* __FWTRANSL_H__ */

/* end of fwtransl.h */
