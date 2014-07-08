/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gsccalib.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Calibration color link code.
 */

#include <float.h>              /* FLT_EPSILON */
#include <math.h>               /* fabs */

#include "core.h"

#include "objects.h"            /* oType */
#include "dictops.h"            /* walk_dictionary */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "gcscan.h"             /* ps_scan_field */
#include "dlstate.h"            /* DL_STATE */
#include "gu_chan.h"            /* DEVICESPACEID */
#include "halftone.h"           /* ht_calibration_screen_info */
#include "mm.h"                 /* mm_sac_alloc */
#include "mps.h"                /* mps_res_t */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "objstack.h"           /* push */
#include "params.h"             /* SystemParams */
#include "spdetect.h"           /* get_separation_name */
#include "rcbcntrl.h"           /* rcbn_enabled */
#include "swerrors.h"           /* TYPECHECK */
#include "uvms.h"               /* UVS */

#include "gs_colorpriv.h"       /* CLINK */
#include "gsccalibpriv.h"


#define CAL_NOT_PRESENT                     (-1)
#define CALIBRATION_CACHE_SIZE              (256)

#define CALSET_DEVICE                       (0)
#define CALSET_TONE                         (1)
#define CALSET_INTENDED_PRESS               (2)
#define CALSET_ACTUAL_PRESS                 (3)
#define NUMBER_OF_CALSETS                   (4)

#define CAL_STATUS_NORMAL                   (0)
#define CAL_STATUS_NONE                     (1)
#define CAL_STATUS_BLACK                    (2)

#define MAGIC_COOKIE                        0x9E3B

struct CLINKCALIBRATIONinfo {
  COLORANTINDEX      *calIndexes;           /* [n_iColorants] */
  GS_CALIBRATIONinfo *calibrationInfo;
};

typedef struct CC_WARNINGS_INFO {
  Bool          resolutionPresent;
  Bool          exposurePresent;
  Bool          negativePrintPresent;
  Bool          halftoneNamePresent;
  Bool          frequencyPresent;

  Bool          incompatibleResolution;
  Bool          incompatibleExposure;
  Bool          incompatibleNegativePrint;
  Bool          incompatibleHalftoneName;
  Bool          incompatibleFrequency;

  Bool          missingCalibrationAbort;

  USERVALUE     xResolution;
  USERVALUE     yResolution;
  int32         exposure;
  Bool          negativePrint;
  NAMECACHE     *halftoneName;
  USERVALUE     lowFrequency;
  USERVALUE     highFrequency;

  Bool          calsetPresent[NUMBER_OF_CALSETS];
  Bool          missingCalsetWarningIssued[NUMBER_OF_CALSETS];
  int32         *curveStatus[NUMBER_OF_CALSETS];    /* [nColorants] */
} CC_WARNINGS_INFO;

struct GS_CALIBRATIONinfo {
  cc_counter_t  refCnt;
  size_t        structSize;

  int32         nColorants;
  OBJECT        calibrationObject;

  int32         calibrationId;

  int32         arrayStyleCalibration;
  int32         defaultIndex;
  int32         blackIndex;

  NAMECACHE     **colorantNames;            /* [nColorants] */
  NAMECACHE     **complementaryNames;       /* [nColorants] */
  USERVALUE     **calibrationData;          /* [nColorants][CALIBRATION_CACHE_SIZE] */
  Bool          *isLinear;                  /* [nColorants] */

  CC_WARNINGS_INFO  warnings;

  int32         *fencePost;                 /* [1] */

  /* The following are for temporary use in setcalibration only */
  OBJECT        *defaultCalibrationDict;
  OBJECT        *blackCalibrationDict;
  OBJECT        *grayCalibrationDict;
  Bool          parentForceSolids;
  Bool          parentNegativePrint;
  int32         tempIndex;
};

/* Used for extracting info in a first pass over the calibration dictionary */
typedef struct INITIALinfo {
  int32         calibrationType;
  int32         nColorants;
  int32         defaultIndex;
  int32         blackIndex;
  OBJECT        *defaultCalibrationDict;
  OBJECT        *blackCalibrationDict;
  OBJECT        *grayCalibrationDict;
  GS_COLORinfo  *colorInfo;
} INITIALinfo;


static void  calibration_destroy(CLINK *pLink);
static Bool calibration_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool calibration_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t calibration_scan( mps_ss_t ss, CLINK *pLink );

#if defined( ASSERT_BUILD )
static void calibrationAssertions(CLINK *pLink);
static void calibrationInfoAssertions(GS_CALIBRATIONinfo *pInfo);
#else
#define calibrationAssertions(_pLink) EMPTY_STATEMENT()
#define calibrationInfoAssertions(_pInfo) EMPTY_STATEMENT()
#endif

static size_t calibrationStructSize(int32 nColorants);
static void calibrationUpdatePtrs(CLINK *pLink, GS_CALIBRATIONinfo *calibrationInfo);

static Bool createcalibrationinfo( GS_CALIBRATIONinfo **calibrationInfo,
                                   int32              nColorants );
static size_t calibrationInfoStructSize(int32 nColorants);
static void calibrationInfoUpdatePtrs(GS_CALIBRATIONinfo *pInfo, int32 nColorants);

static Bool dictwalk_count_colorants( OBJECT *key,
                                      OBJECT *subDict,
                                      void   *args );
static Bool dictwalk_calibration( OBJECT *key,
                                  OBJECT *subDict,
                                  void   *args );
static Bool extractChannelCalibration(USERVALUE  *channelData,
                                      Bool       *isLinear,
                                      OBJECT     *deviceCurve,
                                      OBJECT     *toneCurve,
                                      OBJECT     *intendedPressCurve,
                                      OBJECT     *actualPressCurve,
                                      Bool       forceSolids,
                                      Bool       negativePrint);
static Bool convertObjectArray(OBJECT *objectArray, USERVALUE **floatArray);

static Bool getCalibrationCurves( OBJECT *calibrationCurve,
                                  OBJECT **deviceCurve,
                                  OBJECT **defaultCurve,
                                  USERVALUE **floatDeviceCurve,
                                  USERVALUE **floatDefaultCurve );

static void applyCalibrationInterpolationForFloats(USERVALUE   iColorValue,
                                                   USERVALUE   *oColorValue,
                                                   USERVALUE   *array,
                                                   int32       arrayLength );
static Bool extractWarnings( OBJECT                 *subDict,
                             CC_WARNINGS_INFO       *warnings );
static Bool printWarningIncompatibilities(CC_WARNINGS_INFO   *warnings,
                                          OBJECT             *deviceWarnings,
                                          OBJECT             *toneCurveWarnings,
                                          OBJECT             *intendedPressWarnings,
                                          OBJECT             *actualPressWarnings);
static void printIncompatibleResolutions(OBJECT *deviceResolution,
                                         OBJECT *toneCurveResolution,
                                         OBJECT *intendedPressResolution,
                                         OBJECT *actualPressResolution);
static void printIncompatibleExposures(OBJECT *deviceExposure,
                                       OBJECT *toneCurveExposure,
                                       OBJECT *intendedPressExposure,
                                       OBJECT *actualPressExposure);
static void printIncompatibleNegativePrints(OBJECT *deviceNegativePrint,
                                            OBJECT *toneCurveNegativePrint,
                                            OBJECT *intendedPressNegativePrint,
                                            OBJECT *actualPressNegativePrint);
static void printIncompatibleHalftoneNames(OBJECT *deviceHalftoneName,
                                           OBJECT *toneCurveHalftoneName,
                                           OBJECT *intendedPressHalftoneName,
                                           OBJECT *actualPressHalftoneName);
static void printIncompatibleFrequencies(OBJECT *deviceFrequency,
                                         OBJECT *toneCurveFrequency,
                                         OBJECT *intendedPressFrequency,
                                         OBJECT *actualPressFrequency);
static void printRequiredScreen(CC_WARNINGS_INFO *warnings);
static USERVALUE realOrIntegerElement(OBJECT *object, int32 index);

static CLINKfunctions CLINKcalibration_functions =
{
  calibration_destroy,
  calibration_invokeSingle,
  NULL /* calibration_invokeBlock */,
  calibration_scan
};

/* Array to give indices into calibration info data for Cyan, Magenta, Yellow, Black,
 * Red, Green, Blue, Gray.
 */
static struct calindexes {
 int32 name ;
 int32 complementaryName ;
 int32 index ;
} cal_cmykrgbg[] = {
 { NAME_Cyan    , NAME_Red     , 0 } ,
 { NAME_Magenta , NAME_Green   , 1 } ,
 { NAME_Yellow  , NAME_Blue    , 2 } ,
 { NAME_Black   , NAME_Gray    , 3 } ,
 { NAME_Red     , NAME_Cyan    , 0 } ,
 { NAME_Green   , NAME_Magenta , 1 } ,
 { NAME_Blue    , NAME_Yellow  , 2 } ,
 { NAME_Gray    , NAME_Black   , 3 } ,
} ;

#define CAL_INDBLACK   (3)
#define CAL_INDGRAY    (7)
#define CAL_INDMAX NUM_ARRAY_ITEMS(cal_cmykrgbg)


/* Matching dictionaries for Calibration */
static NAMETYPEMATCH calibrationParentDict[] = {
/* 0 */  { NAME_CalibrationType,                              1, { OINTEGER }},
/* 1 */  { NAME_ForceSolids | OOPTIONAL,                      1, { OBOOLEAN }},
/* 2 */  { NAME_NegativePrint | OOPTIONAL,                    1, { OBOOLEAN }},
/* 3 */  { NAME_WarningsCriteria | OOPTIONAL,                 1, { ODICTIONARY }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH calibrationSubDict[] = {
/* 0 */  { NAME_CalibrationType,                              1, { OINTEGER }},
/* 1 */  { NAME_DeviceCurve | OOPTIONAL,                      2, { OARRAY, OPACKEDARRAY }},
/* 2 */  { NAME_ToneCurve | OOPTIONAL,                        2, { OARRAY, OPACKEDARRAY }},
/* 3 */  { NAME_IntendedPressCurve | OOPTIONAL,               2, { OARRAY, OPACKEDARRAY }},
/* 4 */  { NAME_ActualPressCurve | OOPTIONAL,                 2, { OARRAY, OPACKEDARRAY }},
/* 5 */  { NAME_ForceSolids | OOPTIONAL,                      1, { OBOOLEAN }},
/* 6 */  { NAME_NegativePrint | OOPTIONAL,                    1, { OBOOLEAN }},
         DUMMY_END_MATCH
};

#define CALIB_SUBDICT_SIZE    NUM_ARRAY_ITEMS(calibrationSubDict)

static NAMETYPEMATCH warningsParentDict[] = {
/* 0 */  { NAME_MissingCalibrationAbort,                      1, { OBOOLEAN }},
/* 1 */  { NAME_DeviceCurve | OOPTIONAL,                      1, { ODICTIONARY }},
/* 2 */  { NAME_ToneCurve | OOPTIONAL,                        1, { ODICTIONARY }},
/* 3 */  { NAME_IntendedPressCurve | OOPTIONAL,               1, { ODICTIONARY }},
/* 4 */  { NAME_ActualPressCurve | OOPTIONAL,                 1, { ODICTIONARY }},
         DUMMY_END_MATCH
};

static NAMETYPEMATCH warningsSubDict[] = {
/* 0 */  { NAME_HWResolution | OOPTIONAL,                     2, { OARRAY, OPACKEDARRAY }},
/* 1 */  { NAME_Exposure | OOPTIONAL,                         1, { OINTEGER }},
/* 2 */  { NAME_NegativePrint | OOPTIONAL,                    1, { OBOOLEAN }},
/* 3 */  { NAME_HalftoneName | OOPTIONAL,                     1, { ONAME }},
/* 4 */  { NAME_Frequency | OOPTIONAL,                        2, { OARRAY, OPACKEDARRAY }},
         DUMMY_END_MATCH
};

#define WARNINGS_SUBDICT_SIZE    NUM_ARRAY_ITEMS(warningsSubDict)

#define MONITORF_INCOMPATIBLE               monitorf(UVS("Warning: Calibration curves are incompatible:\n"))
#define MONITORF_REQUIRED_SCREEN            monitorf(UVS("Warning: Required screen:\n"))
#define MONITORF_NO_MATCH                   monitorf(UVS("Warning: Uncalibrated screens:\n"))
#define MONITORF_NO_DEFAULT                 monitorf(UVS("Warning: Missing default calibrations:\n"))

#define MONITORF_RESOLUTION_WITH_LEADER     monitorf(UVS("******     Resolution\n"))
#define MONITORF_EXPOSURE_WITH_LEADER       monitorf(UVS("******     Exposure\n"))
#define MONITORF_NEGATIVE_PRINT_WITH_LEADER monitorf(UVS("******     NegativePrint\n"))
#define MONITORF_HALFTONE_NAME_WITH_LEADER  monitorf(UVS("******     Dot shape\n"))
#define MONITORF_FREQUENCY_WITH_LEADER      monitorf(UVS("******     Frequency\n"))

#define MONITORF_DEVICE_CURVE               monitorf(UVS("******       Device curve         : "))
#define MONITORF_TONE_CURVE                 monitorf(UVS("******       Tone curve           : "))
#define MONITORF_INTENDED_PRESS_CURVE       monitorf(UVS("******       Intended press curve : "))
#define MONITORF_ACTUAL_PRESS_CURVE         monitorf(UVS("******       Actual press curve   : "))

#define MONITORF_RESOLUTION                 monitorf(UVS("Resolution "))
#define MONITORF_EXPOSURE                   monitorf(UVS("Exposure "))
#define MONITORF_NEGATIVE_PRINT             monitorf(UVS("NegativePrint "))
#define MONITORF_HALFTONE_NAME              monitorf(UVS("Dot shape "))
#define MONITORF_FREQUENCY                  monitorf(UVS("Frequency "))
#define MONITORF_FREQUENCIES                monitorf(UVS("Frequencies: "))

#define MONITORF_COLORANT_VALUE(n)          monitorf((uint8 *) "%s ", n)
#define MONITORF_RESOLUTION_VALUES(x,y)     monitorf((uint8 *) "%f x %f", x, y)
#define MONITORF_EXPOSURE_VALUE(e)          (e >= 0) ? monitorf((uint8 *) "%d", e) : MONITORF_NA
#define MONITORF_NEGATIVE_PRINT_VALUE(b)    monitorf(b ? UVS("Negative") : UVS("Positive"))
#define MONITORF_HALFTONE_NAME_VALUE(n)     monitorf((uint8 *) "%s", n)
#define MONITORF_FREQUENCY_VALUES(x,y)      monitorf((uint8 *) "%f - %f", x, y)
#define MONITORF_FREQUENCY_VALUE(x)         x > 0 ? monitorf((uint8 *) "%f", x) : MONITORF_NA

#define MONITORF_LINE_LEADER                monitorf((uint8 *) "******   ")
#define MONITORF_NA                         monitorf(UVS("n/a"))
#define MONITORF_SPACE                      monitorf((uint8 *) " ")
#define MONITORF_SEPARATOR                  monitorf((uint8 *) ", ")
#define MONITORF_LINE_END                   monitorf((uint8 *) "\n")

#define MONITORF_NO_DEFAULT_CURVE           monitorf(UVS("No default available"))
#define MONITORF_BLACK_DEFAULT_CURVE        monitorf(UVS("Black used as default"))

/* ---------------------------------------------------------------------- */

/*
 * Calibration Link Data Access Functions
 * ======================================
 */

CLINK *cc_calibration_create(int32              nColorants,
                             COLORANTINDEX      *colorants,
                             COLORSPACE_ID      colorSpace,
                             GS_CALIBRATIONinfo *calibrationInfo,
                             GUCR_RASTERSTYLE   *hRasterStyle,
                             Bool               fCompositing)
{
  int32 i;
  int32 j;
  CLINK *pLink;

  Bool fLinearCalibration ;
  Bool fUseSeparationName ;

  NAMECACHE *sepname ;

  COLORANTINDEX checkColorant;
  COLORANTINDEX replaceColorant;
  COLORANTINDEX stdIndexes[CAL_INDMAX];

  pLink = cc_common_create(nColorants,
                           colorants,
                           colorSpace,
                           colorSpace,
                           CL_TYPEcalibration,
                           calibrationStructSize(nColorants),
                           &CLINKcalibration_functions,
                           CLID_SIZEcalibration);

  if (pLink == NULL)
    return NULL;

  calibrationUpdatePtrs(pLink, calibrationInfo);

  HQASSERT( calibrationInfo != NULL, "calibrationInfo is NULL");

  /* Determine the colorant indices for the colors in an old style 4 color
   * setcalibration array. Complimentaries are also mapped as CMYK to RGBGray.
   * Spot colors always map to K.
   * Old style 1 color arrays will be applied to all colorants.
   */
  if ((calibrationInfo->arrayStyleCalibration) &&
      (calibrationInfo->nColorants == 4)) {
    for ( i = 0 ; i < CAL_INDMAX ; ++i ) {
      if (!guc_colorantIndexPossiblyNewName(hRasterStyle ,
                                            system_names + cal_cmykrgbg[ i ].name,
                                            &stdIndexes[ i ] ))
        return NULL;
    }
  }

  /* When we're at the front end:-
   * Check to see if we are doing separation detection, have detected a color
   * and are allowed to apply the calibration curve for that detected color.
   * Note the colorant we replace with the colorant of the color that has been
   * detected is always the notional 'Black' for CMYK or RGBK modes.
   * If we're doing Recombination then we always need to use the correct channel.
   * Due to the OmitSeparations work we need to use the detected color when going
   * to CMYK (or RGBK) for the K channel so the resultant "K" uses it's correct
   * color calibration channel. We only do the K channel so that in the event of
   * us making a mistake with this, the other channels are ok. Now since K is
   * usually detected as K if we have made a mistake, then this means that you
   * won't see the problem. We also only do this if the OmitSeparations systemparam
   * is on so this can be turned off when unwanted.
   * When we're at the back end:- do not attempt to access separation detection
   * functions because they are only valid at the front end.
   */
  checkColorant = COLORANTINDEX_UNKNOWN ;
  replaceColorant = COLORANTINDEX_UNKNOWN ;
  if (!fCompositing && calibrationInfo->nColorants != 0) {
    sepname = get_separation_name(FALSE) ;
    if ( ! rcbn_enabled() &&
         sepname != system_names + NAME_Unknown &&
         sepname != system_names + NAME_Composite ) {
      int32         tnColorants;
      DEVICESPACEID deviceSpaceId;

      guc_deviceColorSpace( hRasterStyle ,
                            & deviceSpaceId , & tnColorants ) ;
      if (( deviceSpaceId == DEVICESPACE_CMYK ||
            deviceSpaceId == DEVICESPACE_RGBK ) &&
          ( guc_fOmitMonochrome( hRasterStyle ) ||
            guc_fOmitSeparations( hRasterStyle )) &&
          ( gucr_separationStyle( hRasterStyle ) ==
                GUCR_SEPARATIONSTYLE_SEPARATIONS )) {
        int32 kIndex = CAL_INDBLACK ;
        if (!guc_colorantIndexPossiblyNewName( hRasterStyle,
                                               system_names + cal_cmykrgbg[ kIndex ].name,
                                               &checkColorant ))
          return NULL;
        if (!guc_colorantIndexPossiblyNewName( hRasterStyle ,
                                               sepname,
                                               &replaceColorant ))
          return NULL;
      }
    }
  }

  /* Map the input colorants onto indices into the calibrationData array.
   */
  fLinearCalibration = TRUE ;
  fUseSeparationName = FALSE ;
  for (i = 0; i < nColorants; i++) {
    int32 calIndex = CAL_NOT_PRESENT;

    /* If we get a Black colorant for CMYK, then replace with the color detected colorant.
     */
    if (calibrationInfo->nColorants != 0) {
      COLORANTINDEX tColorant = pLink->iColorants[i] ;
      if (tColorant == checkColorant) {
        tColorant = replaceColorant ;
        fUseSeparationName = TRUE ;
      }

      calIndex = calibrationInfo->defaultIndex;
      if (calibrationInfo->arrayStyleCalibration) {
        /* For single arrays, always use element 0, for arrays of 4 arrays,
         * use the correct CMY or K component.
         */
        if (calibrationInfo->nColorants == 4) {
          for (j = 0 ; j < CAL_INDMAX ; j++) {
            if (tColorant == stdIndexes[j]) {
              calIndex = cal_cmykrgbg[j].index;
              break ;
            }
          }
        }
      }
      else {
        int32   warningIssued = FALSE;
        COLORANTINDEX calibColorant ;

        /* For dictionaries, find the matching component.
         */
        for (j = 0 ; j < calibrationInfo->nColorants ; j++) {
          if (!guc_colorantIndexPossiblyNewName( hRasterStyle,
                                                 calibrationInfo->colorantNames[j],
                                                 &calibColorant ))
            return NULL;
          if (tColorant == calibColorant) {
            calIndex = j;
            break ;
          }
        }

        /* If that fails, look for the complementary color (for std device colorants).
         * Particularly for RGB colorants refering to CMY calibrations.
         */
        if ( j == calibrationInfo->nColorants ) {
          for (j = 0 ; j < calibrationInfo->nColorants ; j++) {
            if (calibrationInfo->complementaryNames[j] != NULL) {
              if (!guc_colorantIndexPossiblyNewName( hRasterStyle,
                                                     calibrationInfo->complementaryNames[j],
                                                     &calibColorant ))
                return NULL;
              if ( tColorant == calibColorant ) {
                calIndex = j;
                break ;
              }
            }
          }
        }

        /* Check that either an explicit calibration exists for this colorant,
         * or that there is a default in all the individual calsets which require
         * it. In the gui product this means that the user has selected those
         * calsets.
         * If there is no calibration or a default, output a warning to the
         * monitor.
         */
        for (j = 0; j < NUMBER_OF_CALSETS; j++) {
          if (calibrationInfo->warnings.calsetPresent[j] &&
              !calibrationInfo->warnings.missingCalsetWarningIssued[j]) {
            int32   curveStatus;

            if (calIndex == CAL_NOT_PRESENT) {
              int32   tmpCalIndex = calibrationInfo->blackIndex;
              if (tmpCalIndex == CAL_NOT_PRESENT)
                curveStatus = CAL_STATUS_NONE;
              else
                curveStatus = CAL_STATUS_BLACK;

              if (j == NUMBER_OF_CALSETS - 1)
                calIndex = calibrationInfo->blackIndex;
            }
            else
              curveStatus = calibrationInfo->warnings.curveStatus[j][calIndex];

            /* Don't output a warning for the special colorant All (but still
             * substitute Black if it exists) because we use All in the EraseColor
             * so this warning would be issued by all jobs when no default
             * colorant exists regardless of whether it was really used.
             * Also don't issue a warning for None because we don't care about
             * it's calibration.
             */
            if ((curveStatus != CAL_STATUS_NORMAL) &&
                (tColorant != COLORANTINDEX_ALL) &&
                (tColorant != COLORANTINDEX_NONE)) {
              if (!warningIssued)
                MONITORF_NO_DEFAULT;
              switch (j) {
              case CALSET_DEVICE:         MONITORF_DEVICE_CURVE;          break;
              case CALSET_TONE:           MONITORF_TONE_CURVE;            break;
              case CALSET_INTENDED_PRESS: MONITORF_INTENDED_PRESS_CURVE;  break;
              case CALSET_ACTUAL_PRESS:   MONITORF_ACTUAL_PRESS_CURVE;    break;
              default:                    HQFAIL("Invalid curve type");   break;
              }
              switch (curveStatus) {
              case CAL_STATUS_NONE:   MONITORF_NO_DEFAULT_CURVE;      break;
              case CAL_STATUS_BLACK:  MONITORF_BLACK_DEFAULT_CURVE;   break;
              default:                HQFAIL("Invalid curve status"); break;
              }
              MONITORF_LINE_END;

              calibrationInfo->warnings.missingCalsetWarningIssued[j] = TRUE;
              warningIssued = TRUE;
            }
          }
        }

        /* Stop the job (if requested) for a calibration colorant.
         */
        if (warningIssued && calibrationInfo->warnings.missingCalibrationAbort) {
          (void)error_handler(RANGECHECK);
          return NULL;
        }
      }
    }

    /* Don't map for a colorant if it's calibration is linear. It's faster. */
    if (calIndex != CAL_NOT_PRESENT) {
      if (calibrationInfo->isLinear[calIndex])
        calIndex = CAL_NOT_PRESENT;
      else
        fLinearCalibration = FALSE ;
    }

    pLink->p.calibration->calIndexes[i] = calIndex;

    HQASSERT(calIndex == CAL_NOT_PRESENT || calIndex < calibrationInfo->nColorants,
             "Invalid value for calIndex");
  }

  if ( fLinearCalibration )
    fUseSeparationName = FALSE ;

  /* Now populate the CLID slots:
   * We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   * For CL_TYPEcalibration (looking at invokeSingle) we have:
   * a) calibrationInfo->calibrationData (32 bits (calibrationId if non linear))
   * b) application of separation detection (sepname) (32 bits)
   * =
   * 2 slots.
   */
  { CLID *idslot = pLink->idslot ;
    HQASSERT( pLink->idcount == CLID_SIZEcalibration , "Didn't create as requested" ) ;
    HQASSERT( calibrationInfo->calibrationId > 0 ,
              "population of idslot[ 0 ] will be incorrect" ) ;
    HQASSERT( replaceColorant == COLORANTINDEX_UNKNOWN ||
              replaceColorant >= 0 ,
              "replaceColorant is an unexpected value" ) ;
    HQASSERT( !fUseSeparationName ||
              replaceColorant >= 0 ,
              "population of idslot[ 1 ] will be incorrect" ) ;
    idslot[ 0 ] = (CLID)(fLinearCalibration ? 0 : calibrationInfo->calibrationId) ;
    idslot[ 1 ] = (CLID)(fUseSeparationName ? replaceColorant + 1 : 0) ;
  }

  calibrationAssertions(pLink);

  return pLink;
}

static void calibration_destroy(CLINK *pLink)
{
  calibrationAssertions(pLink);

  cc_common_destroy(pLink);
}

static Bool calibration_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  COLORANTINDEX           calIndex;
  CLINKCALIBRATIONinfo    *calibration;
  GS_CALIBRATIONinfo      *calibrationInfo;
  USERVALUE               colorValue;
  USERVALUE               high;
  int32                   i;
  int32                   lowIndex;
  int32                   highIndex;
  USERVALUE               low;
  USERVALUE               scaledValue;

  calibrationAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  calibration       = pLink->p.calibration;
  calibrationInfo   = pLink->p.calibration->calibrationInfo;

  for (i = 0; i < pLink->n_iColorants; i++) {
    colorValue = pLink->iColorValues[i] ;
    COLOR_01_ASSERT( colorValue , "calibration input" ) ;

    calIndex = calibration->calIndexes[i];

    /* Apply the calibration, but skip if there isn't one.
     * NB. If the calibration is linear we are pretending there isn't one.
     */
    if (calIndex != CAL_NOT_PRESENT) {
      scaledValue = colorValue * (CALIBRATION_CACHE_SIZE - 1);
      lowIndex = (int32) scaledValue;
      low = calibrationInfo->calibrationData[calIndex][lowIndex];
      highIndex = lowIndex;
      if (highIndex < CALIBRATION_CACHE_SIZE - 1)
        highIndex++;
      high = calibrationInfo->calibrationData[calIndex][highIndex];

      colorValue = low + (high - low) * (scaledValue - lowIndex);
    }

    oColorValues[i] = colorValue;
    COLOR_01_ASSERT( colorValue , "calibration output" ) ;
  }

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool calibration_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
    UNUSED_PARAM(CLINK *, pLink);
    UNUSED_PARAM(CLINKblock *, pBlock);

    calibrationAssertions(pLink);

    return TRUE;
}
#endif


/* calibration_scan - scan the calibration part of a CLINK */
static mps_res_t calibration_scan( mps_ss_t ss, CLINK *pLink )
{
  return cc_scan_calibration( ss, pLink->p.calibration->calibrationInfo );
}


static size_t calibrationStructSize(int32 nColorants)
{
  return sizeof(CLINKCALIBRATIONinfo) +
         nColorants * sizeof(COLORANTINDEX); /* calIndex */
}

static void calibrationUpdatePtrs(CLINK                 *pLink,
                                  GS_CALIBRATIONinfo    *calibrationInfo)
{
  pLink->p.calibration = (CLINKCALIBRATIONinfo *) ((uint8 *)pLink + cc_commonStructSize(pLink));
  pLink->p.calibration->calIndexes = (COLORANTINDEX *) (pLink->p.calibration + 1);
  pLink->p.calibration->calibrationInfo = calibrationInfo;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void calibrationAssertions(CLINK *pLink)
{
  GS_CALIBRATIONinfo  *calibrationInfo;

  cc_commonAssertions(pLink,
                      CL_TYPEcalibration,
                      calibrationStructSize(pLink->n_iColorants),
                      &CLINKcalibration_functions);

  calibrationInfo = pLink->p.calibration->calibrationInfo;
  if (calibrationInfo != NULL)
    calibrationInfoAssertions(calibrationInfo);

  switch (pLink->iColorSpace) {
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGBK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMY:
  case SPACE_DeviceGray:
  case SPACE_DeviceK:
  case SPACE_Separation:
  case SPACE_DeviceN:
    break;
  default:
    HQFAIL("Bad input color space");
    break;
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Calibration Info Data Access Functions
 * ======================================
 */

static Bool createcalibrationinfo( GS_CALIBRATIONinfo **calibrationInfo,
                                    int32              nColorants )
{
  int32               i;
  int32               j;
  GS_CALIBRATIONinfo  *pInfo;
  size_t              structSize;

  structSize = calibrationInfoStructSize(nColorants);

  pInfo = (GS_CALIBRATIONinfo *) mm_sac_alloc( mm_pool_color,
                                               structSize,
                                               MM_ALLOC_CLASS_NCOLOR );

  *calibrationInfo = pInfo;

  if (pInfo == NULL)
    return error_handler(VMERROR);

  calibrationInfoUpdatePtrs(pInfo, nColorants);

  pInfo->refCnt = 1;
  pInfo->structSize = structSize;

  pInfo->nColorants = nColorants;
  pInfo->calibrationObject = onull; /* Struct copy to set slot properties */

  pInfo->calibrationId = 0;

  pInfo->arrayStyleCalibration = FALSE;
  pInfo->defaultIndex = CAL_NOT_PRESENT;
  pInfo->blackIndex = CAL_NOT_PRESENT;

  for (i = 0; i < nColorants; i++) {
    pInfo->colorantNames[i] = NULL;
    pInfo->complementaryNames[i] = NULL;
    /* The default calibration is linear */
    pInfo->isLinear[i] = TRUE;
  }

  pInfo->warnings.resolutionPresent     = FALSE;
  pInfo->warnings.exposurePresent       = FALSE;
  pInfo->warnings.negativePrintPresent  = FALSE;
  pInfo->warnings.halftoneNamePresent   = FALSE;
  pInfo->warnings.frequencyPresent      = FALSE;
  pInfo->warnings.incompatibleResolution    = FALSE;
  pInfo->warnings.incompatibleExposure      = FALSE;
  pInfo->warnings.incompatibleNegativePrint = FALSE;
  pInfo->warnings.incompatibleHalftoneName  = FALSE;
  pInfo->warnings.incompatibleFrequency     = FALSE;

  pInfo->warnings.missingCalibrationAbort = FALSE;

  pInfo->warnings.xResolution   = 0;
  pInfo->warnings.yResolution   = 0;
  pInfo->warnings.exposure      = 0;
  pInfo->warnings.negativePrint = FALSE;
  pInfo->warnings.halftoneName  = NULL;
  pInfo->warnings.lowFrequency  = 0;
  pInfo->warnings.highFrequency = 0;

  for (i = 0; i < NUMBER_OF_CALSETS; i++) {
    pInfo->warnings.calsetPresent[i] = FALSE;
    pInfo->warnings.missingCalsetWarningIssued[i] = FALSE;
    for (j = 0; j < nColorants; j++)
      pInfo->warnings.curveStatus[i][j] = CAL_STATUS_NONE;
  }

  pInfo->defaultCalibrationDict = NULL;
  pInfo->blackCalibrationDict = NULL;
  pInfo->grayCalibrationDict = NULL;
  pInfo->parentForceSolids = FALSE;
  pInfo->parentNegativePrint = FALSE;
  pInfo->tempIndex = 0;

  calibrationInfoAssertions(pInfo);

  return TRUE;
}

Bool cc_samecalibrationinfo(void * pvCalibrationInfo, CLINK * pLink)
{
  /* Does the calibration clink we are being called for refer to the same info structure
     as the one that is currently being destroyed? */

  calibrationInfoAssertions((GS_CALIBRATIONinfo * ) pvCalibrationInfo);
  calibrationAssertions(pLink);

  return ((GS_CALIBRATIONinfo * ) pvCalibrationInfo) == pLink->p.calibration->calibrationInfo;
}

static void freecalibrationinfo( GS_CALIBRATIONinfo *calibrationInfo )
{
  mm_sac_free(mm_pool_color, calibrationInfo, calibrationInfo->structSize);
}

void cc_destroycalibrationinfo( GS_CALIBRATIONinfo **calibrationInfo )
{
  if ( *calibrationInfo != NULL ) {
    calibrationInfoAssertions(*calibrationInfo);
    CLINK_RELEASE(calibrationInfo, freecalibrationinfo);
  }
}

void cc_reservecalibrationinfo( GS_CALIBRATIONinfo *calibrationInfo )
{
  if ( calibrationInfo != NULL ) {
    calibrationInfoAssertions( calibrationInfo ) ;
    CLINK_RESERVE( calibrationInfo ) ;
  }
}

Bool cc_arecalibrationobjectslocal(corecontext_t *corecontext,
                                   GS_CALIBRATIONinfo *calibrationInfo )
{
  if ( calibrationInfo == NULL )
    return FALSE ;

  if ( illegalLocalIntoGlobal(&calibrationInfo->calibrationObject, corecontext))
    return TRUE ;

  return FALSE ;
}


/* cc_scan_calibration - scan the given calibration info
 *
 * This should match gsc_arecalibrationobjectslocal, since both need look at
 * all the VM pointers. */
mps_res_t cc_scan_calibration( mps_ss_t ss,
                               GS_CALIBRATIONinfo *calibrationInfo )
{
  size_t i;
  mps_res_t res;

  if ( calibrationInfo == NULL )
    return MPS_RES_OK;
  res = ps_scan_field( ss, &calibrationInfo->calibrationObject );
  if ( res != MPS_RES_OK ) return res;
  MPS_SCAN_BEGIN( ss )
    for ( i = 0; (int32)i < calibrationInfo->nColorants; ++i ) {
      if ( calibrationInfo->colorantNames[i] != NULL )
        MPS_RETAIN( &calibrationInfo->colorantNames[i], TRUE );
      if ( calibrationInfo->complementaryNames[i] != NULL )
        MPS_RETAIN( &calibrationInfo->complementaryNames[i], TRUE );
    }
    if ( calibrationInfo->warnings.halftoneName != NULL )
      MPS_RETAIN( &calibrationInfo->warnings.halftoneName, TRUE );
  MPS_SCAN_END( ss );
  return MPS_RES_OK;
}


static size_t calibrationInfoStructSize(int32 nColorants)
{
  return sizeof(GS_CALIBRATIONinfo) +                               /* calibration info */
         nColorants * sizeof(NAMECACHE *) +                         /* colorantNames */
         nColorants * sizeof(NAMECACHE *) +                         /* complementaryNames */
         nColorants * sizeof(USERVALUE *) +                         /* calibrationData ptrs */
         nColorants * sizeof(Bool) +                                /* isLinear */
         nColorants * NUMBER_OF_CALSETS * sizeof(int32) +           /* warnings.curveStatus */
         nColorants * CALIBRATION_CACHE_SIZE * sizeof(USERVALUE) +  /* calibrationData */
         sizeof(int32);                                             /* fencePost */
}

static void calibrationInfoUpdatePtrs(GS_CALIBRATIONinfo *pInfo, int32 nColorants)
{
  int32   i;

  HQASSERT(pInfo != NULL, "pInfo not set");

  /* Correct alignment for 64-bit platforms relies on the following being
   * ordered in decreasing order of alignment requirement.
   */
  pInfo->colorantNames = (NAMECACHE **) (pInfo + 1);
  pInfo->complementaryNames = (NAMECACHE **) (pInfo->colorantNames + nColorants);
  pInfo->calibrationData = (USERVALUE **) (pInfo->complementaryNames + nColorants);
  pInfo->isLinear = (Bool *) (pInfo->calibrationData + nColorants);

  pInfo->warnings.curveStatus[0] = (int32 *) (pInfo->isLinear + nColorants);
  for (i = 1; i < NUMBER_OF_CALSETS; i++)
    pInfo->warnings.curveStatus[i] = (int32 *) (pInfo->warnings.curveStatus[i-1] + nColorants);

  /* Build an array of ptrs which allows the calibration data to be accessed as a
   * multi-dimensional array
   */
  if (nColorants != 0) {
    pInfo->calibrationData[0] = (USERVALUE *) (pInfo->warnings.curveStatus[NUMBER_OF_CALSETS-1] + nColorants);

    for (i = 1; i < nColorants; i++) {
      pInfo->calibrationData[i] = pInfo->calibrationData[i - 1] + CALIBRATION_CACHE_SIZE;
    }

    pInfo->fencePost = (int32 *) (pInfo->calibrationData[nColorants - 1] + CALIBRATION_CACHE_SIZE);
  }
  else
    pInfo->fencePost = (int32 *) (pInfo +1);

  pInfo->fencePost[0] = MAGIC_COOKIE;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the transfer info access functions.
 */
static void calibrationInfoAssertions(GS_CALIBRATIONinfo *pInfo)
{
  int32   i;

  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->structSize == calibrationInfoStructSize(pInfo->nColorants),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
  HQASSERT(pInfo->nColorants < 100, "Excessive number of colorants in calibration");

  HQASSERT(pInfo->colorantNames == (NAMECACHE **) (pInfo + 1),
           "colorantNames not set");
  HQASSERT(pInfo->complementaryNames == (NAMECACHE **) (pInfo->colorantNames + pInfo->nColorants),
           "complementaryNames not set");
  HQASSERT(pInfo->calibrationData == (USERVALUE **) (pInfo->complementaryNames + pInfo->nColorants),
           "calibrationData not set");
  HQASSERT(pInfo->isLinear == (Bool *) (pInfo->calibrationData + pInfo->nColorants),
           "isLinear not set");

  HQASSERT(pInfo->warnings.curveStatus[0] == (int32 *) (pInfo->isLinear + pInfo->nColorants),
           "curveStatus not set");

  for (i = 1; i < NUMBER_OF_CALSETS; i++)
    HQASSERT(pInfo->warnings.curveStatus[i] == (int32 *) (pInfo->warnings.curveStatus[i-1] + pInfo->nColorants),
             "curveStatus not set");

   HQASSERT(pInfo->nColorants == 0 ||
           pInfo->calibrationData[0] == (USERVALUE *) (pInfo->warnings.curveStatus[NUMBER_OF_CALSETS-1] + pInfo->nColorants),
           "calibrationData ptr not set");

  for (i = 1; i < pInfo->nColorants; i++)
    HQASSERT(pInfo->calibrationData[i] == pInfo->calibrationData[i - 1] + CALIBRATION_CACHE_SIZE,
             "calibrationData ptr not set");

  if (pInfo->nColorants != 0)
    HQASSERT(pInfo->fencePost == (int32 *) (pInfo->calibrationData[pInfo->nColorants - 1] + CALIBRATION_CACHE_SIZE),
             "fencePost ptr not set");
  else
    HQASSERT(pInfo->fencePost == (int32 *) (pInfo + 1),
             "fencePost ptr not set");

  HQASSERT(pInfo->fencePost[0] == MAGIC_COOKIE,
           "fencePost magic cookie not set");
}
#endif

/* ---------------------------------------------------------------------- */
Bool gsc_setcalibration(GS_COLORinfo *colorInfo, OBJECT calibObj)
{
  GS_CALIBRATIONinfo      *calibrationInfo = NULL;
  int32                   calibrationType;
  int32                   i;
  INITIALinfo             initialInfo;
  OBJECT                  *olist ;
  OBJECT                  *subolist;

  static int32 CalibrationId = 0 ;

  initialInfo.calibrationType = 0;
  initialInfo.nColorants = 0;
  initialInfo.defaultIndex = CAL_NOT_PRESENT;
  initialInfo.blackIndex = CAL_NOT_PRESENT;
  initialInfo.defaultCalibrationDict = NULL;
  initialInfo.blackCalibrationDict = NULL;
  initialInfo.grayCalibrationDict = NULL;
  initialInfo.colorInfo = colorInfo;

  /* Assume for the moment that the data is in the correct format of a
   * dictionary of dictionaries or an array of numbers or an array of
   * an array of numbers. Get the number of color channels in the
   * calibration set so that the Info structure can be initialised.
   * Full type checking will be done later.
   */
  switch (oType(calibObj)) {
  case ODICTIONARY:
    calibrationType = ODICTIONARY;

    /* Walk across the dictionary to find its Type, either 1 or 5, and the
     * number of colorants.
     */
    if (!dictmatch(&calibObj, calibrationParentDict))
      goto error ;
    initialInfo.calibrationType = oInteger(*calibrationParentDict[0].result);   /* CalibrationType - required */
    if (initialInfo.calibrationType == 1) {
      initialInfo.nColorants = 1 ;
      initialInfo.defaultIndex = 0;
      initialInfo.defaultCalibrationDict = &calibObj;
    } else if (initialInfo.calibrationType == 5) {
      if (!walk_dictionary(&calibObj, dictwalk_count_colorants, (void *) &initialInfo))
        goto error ;
    } else {
      (void) error_handler(RANGECHECK);
      goto error ;
    }

    if (! oCanRead(*oDict(calibObj)) && !object_access_override(oDict(calibObj)) ) {
      (void) error_handler( INVALIDACCESS ) ;
      goto error ;
    }
    break ;
  case OARRAY :
  case OPACKEDARRAY :
    calibrationType = OREAL;
    if (theLen(calibObj) == 0)
      initialInfo.nColorants = 1 ;
    else {
      subolist = oArray(calibObj);
      switch (oType( *subolist )) {
      case OREAL:
      case OINTEGER:
        initialInfo.nColorants = 1 ;
        break;
      case OARRAY :
      case OPACKEDARRAY :
        calibrationType = OARRAY;
        initialInfo.nColorants = theLen(calibObj);
        break;
      }
    }

    if (!oCanRead(calibObj) && !object_access_override(&calibObj) ) {
      (void) error_handler( INVALIDACCESS ) ;
      goto error ;
    }
    break ;
  default:
    (void) error_handler( TYPECHECK ) ;
    goto error ;
  }

  HQASSERT(initialInfo.nColorants <= 100, "Rather a lot of colorants found in setcalibration");

  /* Create a temporary calibration info structure.
   */
  if ( ! createcalibrationinfo( &calibrationInfo, initialInfo.nColorants ))
    goto error ;

  Copy(&calibrationInfo->calibrationObject, &calibObj);

  calibrationInfo->calibrationId = ++CalibrationId ;

  /* Populate the temporary structure and do full type checking.
   */
  switch ( calibrationType ) {
  case ODICTIONARY :
    calibrationInfo->defaultIndex = initialInfo.defaultIndex;
    calibrationInfo->blackIndex = initialInfo.blackIndex;
    calibrationInfo->defaultCalibrationDict = initialInfo.defaultCalibrationDict;
    calibrationInfo->blackCalibrationDict = initialInfo.blackCalibrationDict;
    calibrationInfo->grayCalibrationDict = initialInfo.grayCalibrationDict;

    calibrationInfo->arrayStyleCalibration = FALSE;

    if (calibrationParentDict[1].result != NULL)    /* parentForceSolids */
      calibrationInfo->parentForceSolids = oBool(*calibrationParentDict[1].result);
    if (calibrationParentDict[2].result != NULL)    /* parentNegativePrint */
      calibrationInfo->parentNegativePrint = oBool(*calibrationParentDict[2].result);
    if (calibrationParentDict[3].result != NULL) {  /* warningsCriteria */
      CC_WARNINGS_INFO  *warnings = &calibrationInfo->warnings;

      if (!dictmatch(calibrationParentDict[3].result, warningsParentDict))
        return FALSE;

      /* Handle the MissingCalibrationAbort key - required */
      warnings->missingCalibrationAbort = oBool(*warningsParentDict[0].result);

      /* DeviceCurve */
      if (warningsParentDict[1].result != NULL) {
        warnings->calsetPresent[CALSET_DEVICE] = TRUE;
        if (!extractWarnings( warningsParentDict[1].result , warnings ))
          goto error ;
      }
      /* ToneCurve */
      if (warningsParentDict[2].result != NULL) {
        warnings->calsetPresent[CALSET_TONE] = TRUE;
        if (!extractWarnings( warningsParentDict[2].result , warnings ))
          goto error ;
      }
      /* IntendedPressCurve */
      if (warningsParentDict[3].result != NULL) {
        warnings->calsetPresent[CALSET_INTENDED_PRESS] = TRUE;
        if (!extractWarnings( warningsParentDict[3].result ,
                              warnings ))
          goto error ;
      }
      /* ActualPressCurve */
      if (warningsParentDict[4].result != NULL) {
        warnings->calsetPresent[CALSET_ACTUAL_PRESS] = TRUE;
        if (!extractWarnings( warningsParentDict[4].result , warnings ))
          goto error ;
      }

      if (!printWarningIncompatibilities(warnings,
                                         warningsParentDict[1].result,
                                         warningsParentDict[2].result,
                                         warningsParentDict[3].result,
                                         warningsParentDict[4].result))
        goto error ;
    }

    calibrationInfo->tempIndex = 0;
    if (initialInfo.calibrationType == 1) {
      OBJECT defaultKey = OBJECT_NOTVM_NAME(NAME_Default, LITERAL) ;
      if (!dictwalk_calibration(&defaultKey, &calibObj, (void *)calibrationInfo))
        goto error ;
    } else if (initialInfo.calibrationType == 5) {
      if (!walk_dictionary(&calibObj, dictwalk_calibration, (void *) calibrationInfo))
        goto error ;
    }
    break ;

  case OREAL:
    /* Extract just one channel
     */
    calibrationInfo->arrayStyleCalibration = TRUE;
    calibrationInfo->defaultIndex = 0;

    /* The default setting for ForceSolids and NegativePrint is off.
     * NB. For the array form of setcalibration, the calibration procset will
     * have set the calset data to implicitly give the required settings.
     */
    if (!extractChannelCalibration(calibrationInfo->calibrationData[0],
                                   &calibrationInfo->isLinear[0],
                                   &calibObj,         /* DeviceCurve */
                                   NULL, NULL, NULL,  /* ToneCurve, Intended/ActualPressCurves */
                                   FALSE,             /* ForceSolids */
                                   FALSE))            /* NegativePrint */
      goto error ;

    break ;

  case OARRAY:
    if ( initialInfo.nColorants != 4 ) {
      (void)error_handler( RANGECHECK );
      goto error ;
    }

    calibrationInfo->arrayStyleCalibration = TRUE;
    calibrationInfo->defaultIndex = 3;

    olist = oArray(calibObj);

    for (i = 0; i < initialInfo.nColorants; i++) {
      switch ( oType( *olist )) {
      case OARRAY :
      case OPACKEDARRAY :
        break ;
      default:
        (void)error_handler( TYPECHECK );
        goto error ;
      }

      if (! oCanRead(*olist) && !object_access_override(olist) ) {
        (void)error_handler( INVALIDACCESS );
        goto error ;
      }

      if (!extractChannelCalibration(calibrationInfo->calibrationData[i],
                                     &calibrationInfo->isLinear[i],
                                     olist,             /* DeviceCurve */
                                     NULL, NULL, NULL,  /* ToneCurve, Intended/ActualPressCurves */
                                     FALSE,             /* ForceSolids */
                                     FALSE))            /* NegativePrint */
        goto error ;

      olist++;
    }
    break ;

  default:
    HQFAIL("Invalid value for calibrationType");
    break ;
  }

  /* Success. Move the temporary structure into the gstate.
   */
  if ( ! cc_invalidateColorChains( colorInfo, TRUE ))
    goto error ;
  cc_destroycalibrationinfo(&colorInfo->calibrationInfo);
  colorInfo->calibrationInfo = calibrationInfo;

  return TRUE ;

error:
  cc_destroycalibrationinfo(&calibrationInfo);

  return FALSE ;
}

Bool gsc_getcalibration(GS_COLORinfo *colorInfo, STACK *stack)
{
  GS_CALIBRATIONinfo  *calibrationInfo = colorInfo->calibrationInfo;

  return push(calibrationInfo == NULL
              ? &onull : &calibrationInfo->calibrationObject, stack);
}

/* ---------------------------------------------------------------------- */

/* This routine is used to count the number of colorants in the calibration
 * dictionary.
 * "nColorants" is expected to be initialised to zero before the first call.
 */
static Bool dictwalk_count_colorants( OBJECT *key,
                                      OBJECT *subDict,
                                      void   *args )
{
  INITIALinfo   *initialInfo = (INITIALinfo *) args;
  COLORANTINDEX black_ci;

  HQASSERT( key, "key NULL in dictwalk_count_colorants" ) ;
  HQASSERT( subDict,  "subDict NULL in dictwalk_count_colorants" ) ;
  HQASSERT( args, "args NULL in dictwalk_count_colorants" ) ;

  /* Ignore values that are not dictionaries. */
  if (oType(*subDict) != ODICTIONARY)
    return TRUE;

  /* .. and the WarningsCriteria dictionary */
  if (oName(*key) == system_names + NAME_WarningsCriteria)
    return TRUE;

  /* Walk across the current dictionary. If it's not a calibration dictionary,
   * then it's an error.
   */
  if (!dictmatch(subDict, calibrationSubDict))
    return FALSE;

  /* Save the Default data for later retrieval.
   */
  if (oName(*key) == system_names + NAME_Default) {
    initialInfo->defaultIndex = initialInfo->nColorants;
    initialInfo->defaultCalibrationDict = subDict;
  }

  /* Save the Black data for later retrieval. Note that the Black colorant may
   * not be a fully fledged colorant even though we are calibrating using it
   * as we do for PhotoInk printers.
   */
  if (!guc_colorantIndexPossiblyNewName(initialInfo->colorInfo->deviceRS,
                                        oName(*key),
                                        &black_ci))
    return FALSE;
  if (oName(*key) == system_names + NAME_Black ||
      guc_getBlackColorantIndex(initialInfo->colorInfo->deviceRS) == black_ci) {
    initialInfo->blackIndex = initialInfo->nColorants;
    initialInfo->blackCalibrationDict = subDict;
  }

  if (oName(*key) == system_names + NAME_Gray) {
    initialInfo->grayCalibrationDict = subDict;

    /* Set the default colorant to Gray (if there is no explicit Default).
     * This allows us to use a Gray calset for the primary device in an
     * otherwise colored setup for separating devices with no Press calibration.
     */
    if (initialInfo->defaultIndex == CAL_NOT_PRESENT)
      initialInfo->defaultIndex = initialInfo->nColorants;
  }

  initialInfo->nColorants++;

  return TRUE;
}

/* This routine is used to set the calibration for a given colorant
 * "tempIndex" is expected to be initialised to zero before the first call
 * and indicates the number of calls for this dictionary so far.
 */
static Bool dictwalk_calibration( OBJECT *key,
                                  OBJECT *subDict,
                                  void   *args )
{
  GS_CALIBRATIONinfo      *calibrationInfo = (GS_CALIBRATIONinfo *) args;
  Bool                    forceSolids;
  int32                   i;
  int32                   negativePrint;
  int32                   tempIndex = calibrationInfo->tempIndex;
  CC_WARNINGS_INFO        *warnings = &calibrationInfo->warnings;

  HQASSERT( key, "key NULL in dictwalk_calibration" ) ;
  HQASSERT( subDict,  "subDict NULL in dictwalk_calibration" ) ;
  HQASSERT( args, "args NULL in dictwalk_calibration" ) ;

  /* Ignore values that are not dictionaries. */
  if (oType(*subDict) != ODICTIONARY)
    return TRUE;

  /* .. and the WarningsCriteria dictionary */
  if (oName(*key) == system_names + NAME_WarningsCriteria)
    return TRUE;

  /* Walk across the current dictionary. If it's not a calibration dictionary,
   * then it's an error.
   */
  if (!dictmatch(subDict, calibrationSubDict))
    return FALSE;

  HQASSERT( tempIndex < calibrationInfo->nColorants &&
            tempIndex >= 0, "tempIndex not valid in dictwalk_calibration" ) ;

  /* Walk across the Default calibration dictionary and initialise any curves
   * that may be missing from the current dictionary.
   */
  if (calibrationInfo->defaultCalibrationDict != NULL) {
    NAMETYPEMATCH           defaultSubDict[CALIB_SUBDICT_SIZE];

    for (i = 0; i < CALIB_SUBDICT_SIZE; i++)
      defaultSubDict[i] = calibrationSubDict[i];
    if (!dictmatch(calibrationInfo->defaultCalibrationDict, defaultSubDict))
      return FALSE;

    if (calibrationSubDict[1].result == NULL)                     /* DeviceCurve */
      calibrationSubDict[1].result = defaultSubDict[1].result;
    if (calibrationSubDict[2].result == NULL)                     /* ToneCurve */
      calibrationSubDict[2].result = defaultSubDict[2].result;
    if (calibrationSubDict[3].result == NULL)                     /* IntendedPressCurve */
      calibrationSubDict[3].result = defaultSubDict[3].result;
    if (calibrationSubDict[4].result == NULL)                     /* ActualPressCurve */
      calibrationSubDict[4].result = defaultSubDict[4].result;

    /* Don't copy across ForceSolids and NegativePrint. I don't think
     * it's sensible */
  }

  /* Walk across the Gray calibration dictionary and initialise the Device Curve
   * (if the Default is not present). This allows us to use Gray calsets with
   * otherwise colored setups for separating devices.
   */
  if (calibrationInfo->grayCalibrationDict != NULL) {
    NAMETYPEMATCH           graySubDict[CALIB_SUBDICT_SIZE];

    for (i = 0; i < CALIB_SUBDICT_SIZE; i++)
      graySubDict[i] = calibrationSubDict[i];
    if (!dictmatch(calibrationInfo->grayCalibrationDict, graySubDict))
      return FALSE;

    if (calibrationSubDict[1].result == NULL)                     /* DeviceCurve */
      calibrationSubDict[1].result = graySubDict[1].result;
  }

  /* Set the calset status to 'normal' for this colorant if an explicit or a
   * default calibration exists. This will control calibration warnings that
   * may be issued during the job if we come across uncalibrated colorants.
   */
  if (calibrationSubDict[1].result != NULL)             /* DeviceCurve */
    warnings->curveStatus[CALSET_DEVICE][tempIndex] = CAL_STATUS_NORMAL;
  if (calibrationSubDict[2].result != NULL)             /* ToneCurve */
    warnings->curveStatus[CALSET_TONE][tempIndex] = CAL_STATUS_NORMAL;
  if (calibrationSubDict[3].result != NULL)             /* IntendedPressCurve */
    warnings->curveStatus[CALSET_INTENDED_PRESS][tempIndex] = CAL_STATUS_NORMAL;
  if (calibrationSubDict[4].result != NULL)             /* ActualPressCurve */
    warnings->curveStatus[CALSET_ACTUAL_PRESS][tempIndex] = CAL_STATUS_NORMAL;

  /* Walk across the Black calibration dictionary and initialise any curves
   * that may still be missing from the current dictionary.
   * Also set the calset status to 'Black substituted' to control warnings
   * during the job (as above).
   */
  if (calibrationInfo->blackCalibrationDict != NULL) {
    NAMETYPEMATCH           blackSubDict[CALIB_SUBDICT_SIZE];

    for (i = 0; i < CALIB_SUBDICT_SIZE; i++)
      blackSubDict[i] = calibrationSubDict[i];
    if (!dictmatch(calibrationInfo->blackCalibrationDict, blackSubDict))
      return FALSE;

    if (calibrationSubDict[1].result == NULL && blackSubDict[1].result != NULL) { /* DeviceCurve */
      calibrationSubDict[1].result = blackSubDict[1].result;
      warnings->curveStatus[CALSET_DEVICE][tempIndex] = CAL_STATUS_BLACK;
    }
    if (calibrationSubDict[2].result == NULL && blackSubDict[2].result != NULL) { /* ToneCurve */
      calibrationSubDict[2].result = blackSubDict[2].result;
      warnings->curveStatus[CALSET_TONE][tempIndex] = CAL_STATUS_BLACK;
    }
    if (calibrationSubDict[3].result == NULL && blackSubDict[3].result != NULL) { /* IntendedPressCurve */
      calibrationSubDict[3].result = blackSubDict[3].result;
      warnings->curveStatus[CALSET_INTENDED_PRESS][tempIndex] = CAL_STATUS_BLACK;
    }
    if (calibrationSubDict[4].result == NULL && blackSubDict[4].result != NULL) { /* ActualPressCurve */
      calibrationSubDict[4].result = blackSubDict[4].result;
      warnings->curveStatus[CALSET_ACTUAL_PRESS][tempIndex] = CAL_STATUS_BLACK;
    }
  }


  calibrationInfo->colorantNames[tempIndex] = oName(*key) ;
  calibrationInfo->complementaryNames[tempIndex] = NULL;
  for ( i = 0 ; i < CAL_INDMAX ; ++i ) {
    if (oName(*key) == system_names + cal_cmykrgbg[ i ].name)
      calibrationInfo->complementaryNames[tempIndex] =
                        system_names + cal_cmykrgbg[ i ].complementaryName;
  }

  if (oInteger(*calibrationSubDict[0].result) != 1)   /* CalibrationType - required */
    return error_handler(RANGECHECK);

  /* The next 2 items, ForceSolids and NegativePrint, takes a default from the
   * parent dictionary (if it exists).
   */
  forceSolids = calibrationInfo->parentForceSolids;
  if (calibrationSubDict[5].result != NULL)                       /* ForceSolids */
    if (oBool(*calibrationSubDict[5].result))
      forceSolids = TRUE;

  negativePrint = calibrationInfo->parentNegativePrint;
  if (calibrationSubDict[6].result != NULL)                       /* NegativePrint */
    if (oBool(*calibrationSubDict[6].result))
      negativePrint = TRUE;

  if (!extractChannelCalibration(calibrationInfo->calibrationData[tempIndex],
                                 &calibrationInfo->isLinear[tempIndex],
                                 calibrationSubDict[1].result,    /* DeviceCurve */
                                 calibrationSubDict[2].result,    /* ToneCurve */
                                 calibrationSubDict[3].result,    /* IntendedPressCurve */
                                 calibrationSubDict[4].result,    /* ActualPressCurve */
                                 forceSolids,
                                 negativePrint))
    return FALSE;

  calibrationInfo->tempIndex++;

  HQASSERT(calibrationInfo->tempIndex <= calibrationInfo->nColorants,
           "Too many colorants found in setcalibration");

  return TRUE;
}

/* ---------------------------------------------------------------------- */

/* Convert the calibration data for this channel into a 256 point, evenly
 * spaced indexable array which will be used in the invoke instead of the
 * original data.
 * Also determine if the 256 point calibration array is linear or not.
 */
#define EPSILON   0.00001

static Bool extractChannelCalibration(USERVALUE  *channelData,         /* O */
                                      Bool       *isLinear,            /* O */
                                      OBJECT     *deviceCurve,         /* I */
                                      OBJECT     *toneCurve,           /* I */
                                      OBJECT     *intendedPressCurve,  /* I */
                                      OBJECT     *actualPressCurve,    /* I */
                                      Bool       forceSolids,          /* I */
                                      Bool       negativePrint)        /* I */
{
  USERVALUE   colorValue;
  int32       i;
  USERVALUE   orig_colorValue;

  OBJECT      *theDeviceCurve = NULL;
  OBJECT      *theDefaultDeviceCurve = NULL;
  OBJECT      *theToneCurve = NULL;
  OBJECT      *theDefaultToneCurve = NULL;
  OBJECT      *theIntendedPressCurve = NULL;
  OBJECT      *theDefaultIntendedPressCurve = NULL;
  OBJECT      *theActualPressCurve = NULL;
  OBJECT      *theDefaultActualPressCurve = NULL;

  USERVALUE   *floatDeviceCurve = NULL;
  USERVALUE   *floatDefaultDeviceCurve = NULL;
  USERVALUE   *floatToneCurve = NULL;
  USERVALUE   *floatDefaultToneCurve = NULL;
  USERVALUE   *floatIntendedPressCurve = NULL;
  USERVALUE   *floatDefaultIntendedPressCurve = NULL;
  USERVALUE   *floatActualPressCurve = NULL;
  USERVALUE   *floatDefaultActualPressCurve = NULL;
  int32       returnStatus = FALSE;



  *isLinear = TRUE;

  if (deviceCurve != NULL) {
    if ( !getCalibrationCurves( deviceCurve,
                                &theDeviceCurve,
                                &theDefaultDeviceCurve,
                                &floatDeviceCurve,
                                &floatDefaultDeviceCurve ))
      goto tidy_up;


    if (negativePrint) {
      if ( theDeviceCurve != NULL ) {
        for (i = 0; i < theLen(*theDeviceCurve); i++)
          floatDeviceCurve[i] = 1 - floatDeviceCurve[i];
      }

      if ( theDefaultDeviceCurve != NULL ) {
        for (i = 0; i < theLen(*theDefaultDeviceCurve); i++)
          floatDefaultDeviceCurve[i] = 1 - floatDefaultDeviceCurve[i];
      }
    }
  }


  if (toneCurve != NULL) {
    if ( !getCalibrationCurves( toneCurve,
                                &theToneCurve,
                                &theDefaultToneCurve,
                                &floatToneCurve,
                                &floatDefaultToneCurve ))
      goto tidy_up;


    /* Transposing the tone curves is required because we want tone curves to
     * put down values specified, which is opposite of normal calibration.
     * Therefore read into memory and swap the pairs of numbers before doing
     * the interpolation.  So the default tone curve, (which will be applied first),
     * will give NDC->SGV, and the tone curve will give SGV->SNV(desired).
     * If either of these has zero length, the other effectively gives
     * NDC->SNV(desired).
     */

    if ( theToneCurve != NULL ) {
      for (i = 0; i < theLen(*theToneCurve) / 2; i++) {
        USERVALUE   temp;
        temp = floatToneCurve[2*i];
        floatToneCurve[2*i] = floatToneCurve[2*i + 1];
        floatToneCurve[2*i + 1] = temp;
      }
    }

    if ( theDefaultToneCurve != NULL ) {
      for (i = 0; i < theLen(*theDefaultToneCurve) / 2; i++) {
        USERVALUE   temp;
        temp = floatDefaultToneCurve[2*i];
        floatDefaultToneCurve[2*i] = floatDefaultToneCurve[2*i + 1];
        floatDefaultToneCurve[2*i + 1] = temp;
      }
    }
  }


  if (intendedPressCurve != NULL) {
    if ( !getCalibrationCurves( intendedPressCurve,
                                &theIntendedPressCurve,
                                &theDefaultIntendedPressCurve,
                                &floatIntendedPressCurve,
                                &floatDefaultIntendedPressCurve ))
      goto tidy_up;


    /* Treatment of the intended press is different to the others because we
     * wish to backwards interpolate this array. Therefore read into memory
     * and transpose the curve (as for tone curves).
     */

    if ( theIntendedPressCurve != NULL ) {
      for (i = 0; i < theLen(*theIntendedPressCurve) / 2; i++) {
        USERVALUE   temp;
        temp = floatIntendedPressCurve[2*i];
        floatIntendedPressCurve[2*i] = floatIntendedPressCurve[2*i + 1];
        floatIntendedPressCurve[2*i + 1] = temp;
      }
    }

    if ( theDefaultIntendedPressCurve != NULL) {
      for (i = 0; i < theLen(*theDefaultIntendedPressCurve) / 2; i++) {
        USERVALUE   temp;
        temp = floatDefaultIntendedPressCurve[2*i];
        floatDefaultIntendedPressCurve[2*i] = floatDefaultIntendedPressCurve[2*i + 1];
        floatDefaultIntendedPressCurve[2*i + 1] = temp;
      }
    }
  }


  if (actualPressCurve != NULL) {
    if ( !getCalibrationCurves( actualPressCurve,
                                &theActualPressCurve,
                                &theDefaultActualPressCurve,
                                &floatActualPressCurve,
                                &floatDefaultActualPressCurve ))
      goto tidy_up;
  }


  for (i = 0; i < CALIBRATION_CACHE_SIZE; i++ ) {
    orig_colorValue = (USERVALUE) i / (CALIBRATION_CACHE_SIZE - 1);
    colorValue = orig_colorValue;

    /* Note that in the case of curves which are applied backwards, i.e. the
       intended press curves and the tone curves, we need to apply the default
       curve first.  This is because we are taking e.g. an NDC specified by the
       job, and reading an SGV from the default curve.  Then we are using this
       SGV to get the desired SNV from the 'device' curve, i.e. the
       intended press curve or the tone curve.  If either the default or the
       'device' curve is null, the other will effectively give NDC->SNV(desired).
       The opposite is true for curves which are applied forwards, (the
       actual press curve, and the device curve), i.e. the 'device' curve is
       applied before the default curve, so the 'device' curve gives
       SNV(desired)->SGV, and the default curve gives SGV->NDC(required).
       Similarly if one of these is missing, the other will effectively
       give SNV(desired)->NDC(required).  */


    if (theDefaultIntendedPressCurve != NULL)
      applyCalibrationInterpolationForFloats(colorValue,
                                             &colorValue,
                                             floatDefaultIntendedPressCurve,
                                             theLen(*theDefaultIntendedPressCurve));

    if (theIntendedPressCurve != NULL)
      applyCalibrationInterpolationForFloats(colorValue,
                                             &colorValue,
                                             floatIntendedPressCurve,
                                             theLen(*theIntendedPressCurve));

    if (theDefaultToneCurve != NULL)
      applyCalibrationInterpolationForFloats(colorValue,
                                             &colorValue,
                                             floatDefaultToneCurve,
                                             theLen(*theDefaultToneCurve));

    if (theToneCurve != NULL)
      applyCalibrationInterpolationForFloats(colorValue,
                                             &colorValue,
                                             floatToneCurve,
                                             theLen(*theToneCurve));

    if (theActualPressCurve != NULL)
      applyCalibrationInterpolationForFloats(colorValue,
                                             &colorValue,
                                             floatActualPressCurve,
                                             theLen(*theActualPressCurve));

    if (theDefaultActualPressCurve != NULL)
      applyCalibrationInterpolationForFloats(colorValue,
                                             &colorValue,
                                             floatDefaultActualPressCurve,
                                             theLen(*theDefaultActualPressCurve));

    if (theDeviceCurve != NULL)
      applyCalibrationInterpolationForFloats(colorValue,
                                             &colorValue,
                                             floatDeviceCurve,
                                             theLen(*theDeviceCurve));

    if (theDefaultDeviceCurve != NULL)
      applyCalibrationInterpolationForFloats(colorValue,
                                             &colorValue,
                                             floatDefaultDeviceCurve,
                                             theLen(*theDefaultDeviceCurve));


    /* Force output to 0 if the input is Black (=0.0) and forceSolids are on.
     * NB. This will cause interpolation to the solic black over the first
     * device code. It is not the same behaviour as previous versions but
     * doesn't matter. At some stage we will be adjusting the calibration over
     * the range of 80-100% to smooth out a jump at 100% or to alleviate
     * saturation over this range depending on whether the real device prints
     * darker or lighter than the golden device.
     */
    if (i == 0 && forceSolids)
      colorValue = 0.0;

    if ( fabs(colorValue - orig_colorValue) > EPSILON )
      *isLinear = FALSE;

    channelData[i] = colorValue;

    HQASSERT( colorValue >= 0.0 && colorValue <= 1.0,
              "Calibration result is out of bounds") ;
  }

  returnStatus = TRUE;

tidy_up:
  if (floatDeviceCurve != NULL)
    mm_sac_free(mm_pool_color,
            (mm_addr_t) floatDeviceCurve,
            (mm_size_t) sizeof(USERVALUE) * theLen(*theDeviceCurve));

  if (floatDefaultDeviceCurve != NULL)
    mm_sac_free(mm_pool_color,
          (mm_addr_t) floatDefaultDeviceCurve,
          (mm_size_t) sizeof(USERVALUE) * theLen(*theDefaultDeviceCurve));

  if (floatToneCurve != NULL)
    mm_sac_free(mm_pool_color,
            (mm_addr_t) floatToneCurve,
            (mm_size_t) sizeof(USERVALUE) * theLen(*theToneCurve));

  if (floatDefaultToneCurve != NULL)
    mm_sac_free(mm_pool_color,
            (mm_addr_t) floatDefaultToneCurve,
            (mm_size_t) sizeof(USERVALUE) * theLen(*theDefaultToneCurve));

  if (floatIntendedPressCurve != NULL)
    mm_sac_free(mm_pool_color,
            (mm_addr_t) floatIntendedPressCurve,
            (mm_size_t) sizeof(USERVALUE) * theLen(*theIntendedPressCurve));

  if (floatDefaultIntendedPressCurve != NULL)
    mm_sac_free(mm_pool_color,
            (mm_addr_t) floatDefaultIntendedPressCurve,
            (mm_size_t) sizeof(USERVALUE) * theLen(*theDefaultIntendedPressCurve));

  if (floatActualPressCurve != NULL)
    mm_sac_free(mm_pool_color,
            (mm_addr_t) floatActualPressCurve,
            (mm_size_t) sizeof(USERVALUE) * theLen(*theActualPressCurve));

  if (floatDefaultActualPressCurve != NULL)
    mm_sac_free(mm_pool_color,
            (mm_addr_t) floatDefaultActualPressCurve,
            (mm_size_t) sizeof(USERVALUE) * theLen(*theDefaultActualPressCurve));

  return returnStatus;
}


static Bool getCalibrationCurves( OBJECT *calibrationCurve,
                                  OBJECT **deviceCurve,
                                  OBJECT **defaultCurve,
                                  USERVALUE **floatDeviceCurve,
                                  USERVALUE **floatDefaultCurve )
{
  OBJECT *theDeviceCurve;
  OBJECT *theDefaultCurve;
  OBJECT *theo;
  int32  arrayLength;

  arrayLength = ( int32 )theLen(*calibrationCurve) ;
  theo = oArray( *calibrationCurve ) ;

  /* There are 2 valid formats for calibration curves.  Either a simple array,
     e.g. [device curve pairs] which can represent the device curve,
     or the tone curve, intended/actual press curve etc, or an array containing
     2 arrays, e.g. [[device curve pairs] [default curve pairs]].  If there is only
     a default curve that is represented by [[] [default curve pairs]]. */

  if ( arrayLength == 2 ) {

    if ( oType( *theo ) == OARRAY ) {

      /* get the device curve values */
      *deviceCurve = theDeviceCurve = theo ;

      if ( !gsc_validateCalibrationArray( theDeviceCurve ))
        goto error;

      if (!convertObjectArray( theDeviceCurve, floatDeviceCurve ))
        goto error;

      /* get the default curve values */
      theo++ ;
      *defaultCurve = theDefaultCurve = theo ;

      if ( !gsc_validateCalibrationArray( theDefaultCurve ))
        goto error;

      if (!convertObjectArray(theDefaultCurve, floatDefaultCurve))
        goto error;
    }
    else {
      /* there is only one point (curve pair) in the array */
      goto error;
    }
  }
  else {
    /* just got a single array */
    *deviceCurve = theDeviceCurve = calibrationCurve;

    if ( !gsc_validateCalibrationArray( theDeviceCurve ))
      goto error;

    if (!convertObjectArray(theDeviceCurve, floatDeviceCurve))
      goto error;
  }

  return TRUE;

error:
  return FALSE;
}



static Bool convertObjectArray(OBJECT *objectArray, USERVALUE **floatArray)
{
  int32       i;

  if (theLen(*objectArray) == 0) {
    *floatArray = NULL;
    return TRUE;
  }

  *floatArray = (USERVALUE *) mm_sac_alloc( mm_pool_color,
                                            sizeof(USERVALUE) * theLen(*objectArray),
                                            MM_ALLOC_CLASS_NCOLOR );
  if (*floatArray == NULL)
    return error_handler(VMERROR);

  for (i = 0; i < theLen(*objectArray); i++)
    if (!object_get_real(&oArray(*objectArray)[i], &(*floatArray)[i]) )
      return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------- */

Bool gsc_validateCalibrationArray(OBJECT *arrayObject)
{
  int32       arrayLength;
  int32       i;
  USERVALUE   ndc1 = 0.0;
  USERVALUE   ndc2 = 0.0;
  int32       ndcIncreasing;
  USERVALUE   snv1 = 0.0;
  USERVALUE   snv2 = 0.0;
  int32       snvIncreasing;
  OBJECT      *theo;

  /* Validate the calibration array, an array of snv, ndc pairs, or null.
     N.B. The array could represent calibration data in a calset with
     CalibrationDataType 1, in which case the pairs would be (snv, ndc),
     but it could also represent e.g. data for a calset with
     CalibrationDataType 2, in which case the pairs representing the
     device curve would be (snv, sgv), whereas the pairs representing the
     default curve would be (snv, ndc).
   */

  if (oType(*arrayObject) == ONULL)
    return TRUE;

  if (oType(*arrayObject) != OARRAY)
    return error_handler(TYPECHECK);


  arrayLength = ( int32 )theLen(*arrayObject) ;
  theo = oArray( *arrayObject ) ;

  if (arrayLength == 0)
    return TRUE;

  if ((arrayLength % 2) != 0)
    return error_handler( RANGECHECK ) ;

  if (arrayLength == 2)
      return error_handler( RANGECHECK ) ;

  /*
   * The ndc (or sgv) series should normally be strongly monotonic (due to the
   * coregui habit of adding an extra point to force white whites, this has not
   * been enforced).
   * The snv series weakly monotonic.
   * The values in the snv series are allowed to be outside the range of 0->1
   * but the range of the series must contain a subset of 0->1. The reason for
   * this is that we must be able to map snv's of 0->1 onto ndc's, using
   * extrapolation if necessary.
   * The values in the ndc entries are also allowed to be outside the range
   * of 0->1 with the same proviso of at least partially covering that range. The
   * reason here is to avoid some error prone work outside the core to truncate
   * calibration curves.
   */

  if (!object_get_real(&theo[0], &snv1) ||
      !object_get_real(&theo[1], &ndc1) ||
      !object_get_real(&theo[arrayLength - 2], &snv2) ||
      !object_get_real(&theo[arrayLength - 1], &ndc2) )
    return FALSE;

  if ( ndc1 <= 0.0f && ndc2 <= 0.0f )
    return error_handler( RANGECHECK ) ;
  if ( ndc1 >= 1.0f && ndc2 >= 1.0f )
    return error_handler( RANGECHECK ) ;

  if ( snv1 <= 0.0f && snv2 <= 0.0f )
    return error_handler( RANGECHECK ) ;
  if ( snv1 >= 1.0f && snv2 >= 1.0f )
    return error_handler( RANGECHECK ) ;

  ndcIncreasing = ndc2 > ndc1;
  snvIncreasing = snv2 > snv1;

  for (i = 0; i < arrayLength/2 - 1; i++, theo += 2 ) {
    if (!object_get_real(&theo[0], &snv1) ||
        !object_get_real(&theo[2], &snv2) ||
        !object_get_real(&theo[1], &ndc1) ||
        !object_get_real(&theo[3], &ndc2) )
      return FALSE;

    if (ndcIncreasing ? ndc2 < ndc1 : ndc1 < ndc2)
      return error_handler( RANGECHECK ) ;
    if (snvIncreasing ? snv2 < snv1 : snv1 < snv2)
      return error_handler( RANGECHECK ) ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

Bool cc_applyCalibrationInterpolation(USERVALUE   colorValue,
                                      USERVALUE   *result,
                                      OBJECT      *array )
{
  int32     arrayLength;
  int32     increasing;
  int32     left;
  int32     middle;
  int32     right;
  OBJECT    *theo;
  USERVALUE x1 = 0.0;
  USERVALUE x2 = 0.0;
  USERVALUE y1 = 0.0;
  USERVALUE y2 = 0.0;

  arrayLength = ( int32 )theLen(*array) ;
  theo = oArray( *array ) ;

  HQASSERT(theo , "theo NULL pointer" ) ;
  HQASSERT((arrayLength & 1) == 0 , "interpolating in array of odd size" ) ;
  HQASSERT( arrayLength != 2 , "only one point in array" ) ;

  /* Apply pairwise interpolation - first take out end cases;
   * elements are organised as (input, output) pairs in either ascending
   * or descending order of input value.
   * If the array is empty, apply a linear calibration.
   */
  if (arrayLength != 0) {
    if (!object_get_real(&theo[0], &x1) ||
        !object_get_real(&theo[arrayLength - 2], &x2) )
      return FALSE;
    increasing = x2 > x1;

    if ( colorValue == 0.0f && (increasing ? x1 == 0.0f : x2 == 0.0f) ) {
      if ( !object_get_real(&theo[increasing ? 1 : arrayLength - 1], &colorValue) )
        return FALSE;
    }
    else if ( colorValue == 1.0f && (increasing ? x2 == 1.0f : x1 == 1.0f) ) {
      if ( !object_get_real(&theo[increasing ? arrayLength - 1 : 1], &colorValue) )
        return FALSE;
    }
    else {
      /* find the bracketing elements */

      left = 0 ;
      right = arrayLength - 2 ;

      while ( right - left > 2 ) {
        middle = (( left + right ) >> 2) << 1 ;
        if (!object_get_real(&theo[middle], &x1) )
          return FALSE;
        if ( increasing ? (colorValue > x1) : (colorValue < x1) )
          left = middle ;
        else
          right = middle ;
      }

      /* now do the interpolation */
      if ( !object_get_real(&theo[left], &x1) ||
           !object_get_real(&theo[right], &x2) ||
           !object_get_real(&theo[left + 1], &y1) ||
           !object_get_real(&theo[right + 1], &y2) )
        return FALSE;
      if (fabs(x2 - x1) < FLT_EPSILON)
        /* We must be in a constant part of a device curve or actual press curve.
         * Not too sure what to do here but take the average.
         */
        colorValue = ( y1 + y2 ) / 2 ;
      else
        colorValue = y1 + (colorValue - x1 ) / ( x2 - x1 ) * ( y2 - y1 ) ;
    }
    NARROW_01( colorValue ) ;
  }

  *result = colorValue;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */

static void applyCalibrationInterpolationForFloats(USERVALUE   colorValue,
                                                   USERVALUE   *result,
                                                   USERVALUE   *array,
                                                   int32       arrayLength )
{
  int32     increasing;
  int32     left;
  int32     middle;
  int32     right;
  USERVALUE x1;
  USERVALUE x2;
  USERVALUE y1;
  USERVALUE y2;

  HQASSERT((arrayLength & 1 ) == 0 , "interpolating in array of odd size" ) ;
  HQASSERT(arrayLength != 2 , "only one point in array" ) ;

  /* Apply pairwise interpolation - first take out end cases;
   * elements are organised as (input, output) pairs in either ascending
   * or descending order of input value.
   * If the array is empty, apply a linear calibration.
   */
  if (arrayLength != 0) {
    x1 = array[ 0 ];
    x2 = array[ arrayLength - 2 ];
    increasing = x2 > x1;

    if ( colorValue == 0.0f && (increasing ? x1 == 0.0f : x2 == 0.0f) ) {
      colorValue = increasing ? array[ 1 ] : array[ arrayLength - 1 ];
    }
    else if ( colorValue == 1.0f && (increasing ? x2 == 1.0f : x1 == 1.0f) ) {
      colorValue = increasing ? array[ arrayLength - 1 ] : array[ 1 ];
    }
    else {
      /* Find the bracketing elements */
      left = 0 ;
      right = arrayLength - 2 ;

      while ( right - left > 2 ) {
        middle = (( left + right ) >> 2 ) << 1 ;
        if ( increasing ? (colorValue > array[ middle ]) : (colorValue < array[ middle ]) )
          left = middle ;
        else
          right = middle ;
      }

      /* Now do the interpolation */
      x1 = array[ left ] ;
      x2 = array[ right ] ;
      y1 = array[ left + 1 ] ;
      y2 = array[ right + 1 ] ;
      if (fabs(x2 - x1) < FLT_EPSILON)
        /* We must be in a constant part of a device curve or actual press curve.
         * Not too sure what to do here but take the average.
         */
        colorValue = ( y1 + y2 ) / 2 ;
      else
        colorValue = y1 + ( colorValue - x1 ) / ( x2 - x1 ) * ( y2 - y1 ) ;
    }
    NARROW_01( colorValue ) ;
  }

  *result = colorValue;
}

/* ---------------------------------------------------------------------- */

/* This routine is used to constrain the warnings criteria to the conjunction
 * of values from several calsets.
 */
static Bool extractWarnings( OBJECT                 *subDict,
                             CC_WARNINGS_INFO       *warnings )
{
  HQASSERT( subDict != NULL,  "subDict NULL in dictwalk_warnings" );
  HQASSERT( oType(*subDict) == ODICTIONARY, "subDict should be a dict" );
  HQASSERT( warnings != NULL, "warnings NULL in check_warnings" );

  /* Get the elements of the warnings sub-dictionary.
   */
  if (!dictmatch(subDict, warningsSubDict))
    return FALSE;

  /* HWResolution */
  if (warningsSubDict[0].result != NULL) {
    OBJECT    *theo = oArray(*warningsSubDict[0].result);
    USERVALUE xResolution;
    USERVALUE yResolution;

    if (theLen(*warningsSubDict[0].result) !=  2)
      return error_handler(RANGECHECK);

    if (oType(*theo) == OREAL)
      xResolution = oReal(*theo);
    else
    if (oType(*theo) == OINTEGER)
      xResolution = (USERVALUE) oInteger(*theo);
    else
      return error_handler(TYPECHECK);
    theo++;
    if (oType(*theo) == OREAL)
      yResolution = oReal(*theo);
    else
    if (oType(*theo) == OINTEGER)
      yResolution = (USERVALUE) oInteger(*theo);
    else
      return error_handler(TYPECHECK);

    if (warnings->resolutionPresent) {
      /* The current warnings dictionary should be consistent with other
       * warnings dictionaries.
       */
      if ((warnings->xResolution != xResolution) ||
          (warnings->yResolution != yResolution))
        warnings->incompatibleResolution = TRUE;
    }

    warnings->resolutionPresent = TRUE;
    warnings->xResolution = xResolution;
    warnings->yResolution = yResolution;
  }

  /* Exposure */
  if (warningsSubDict[1].result != NULL) {
    int32   exposure = oInteger(*warningsSubDict[1].result);

    if (warnings->exposurePresent) {
      /* The current warnings dictionary should be consistent with other
       * warnings dictionaries.
       */
      if (warnings->exposure != exposure)
        warnings->incompatibleExposure = TRUE;
    }

    warnings->exposurePresent = TRUE;
    warnings->exposure = exposure;
  }

  /* NegativePrint */
  if (warningsSubDict[2].result != NULL) {
    Bool negativePrint = oBool(*warningsSubDict[2].result);

    if (warnings->negativePrintPresent) {
      /* The current warnings dictionary should be consistent with other
       * warnings dictionaries.
       */
      if (warnings->negativePrint != negativePrint)
        warnings->incompatibleNegativePrint = TRUE;
    }

    warnings->negativePrintPresent = TRUE;
    warnings->negativePrint = negativePrint;
  }

  /* HalftoneName */
  if (warningsSubDict[3].result != NULL) {
    NAMECACHE *halftoneName = oName(*warningsSubDict[3].result);

    if (warnings->halftoneNamePresent) {
      /* The current warnings dictionary should be consistent with other
       * warnings dictionaries.
       */
      if (warnings->halftoneName != halftoneName)
        warnings->incompatibleHalftoneName = TRUE;
    }

    warnings->halftoneNamePresent = TRUE;
    warnings->halftoneName = halftoneName;
  }

  /* Frequency */
  if (warningsSubDict[4].result != NULL) {
    OBJECT    *theo = oArray(*warningsSubDict[4].result);
    USERVALUE lowFrequency;
    USERVALUE highFrequency;

    if (theLen(*warningsSubDict[4].result) !=  2)
      return error_handler(RANGECHECK);

    if (oType(*theo) == OREAL)
      lowFrequency = oReal(*theo);
    else
    if (oType(*theo) == OINTEGER)
      lowFrequency = (USERVALUE) oInteger(*theo);
    else
      return error_handler(TYPECHECK);
    theo++;
    if (oType(*theo) == OREAL)
      highFrequency = oReal(*theo);
    else
    if (oType(*theo) == OINTEGER)
      highFrequency = (USERVALUE) oInteger(*theo);
    else
      return error_handler(TYPECHECK);

    if (warnings->frequencyPresent) {
      /* The current warnings dictionary should be consistent with other
       * warnings dictionaries. We will end up with the common range across
       * all component curves.
       */
      if (lowFrequency < warnings->lowFrequency)
        lowFrequency = warnings->lowFrequency;
      if (highFrequency > warnings->highFrequency)
        highFrequency = warnings->highFrequency;
      if (lowFrequency > highFrequency)
        warnings->incompatibleFrequency = TRUE;
    }

    warnings->frequencyPresent = TRUE;
    warnings->lowFrequency = lowFrequency;
    warnings->highFrequency = highFrequency;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

static Bool printWarningIncompatibilities(CC_WARNINGS_INFO   *warnings,
                                          OBJECT             *deviceWarnings,
                                          OBJECT             *toneCurveWarnings,
                                          OBJECT             *intendedPressWarnings,
                                          OBJECT             *actualPressWarnings)
{
  int32   i;

  if (warnings->incompatibleResolution     ||
      warnings->incompatibleExposure       ||
      warnings->incompatibleNegativePrint  ||
      warnings->incompatibleHalftoneName   ||
      warnings->incompatibleFrequency) {
    NAMETYPEMATCH   deviceSubDict[WARNINGS_SUBDICT_SIZE];
    NAMETYPEMATCH   toneCurveSubDict[WARNINGS_SUBDICT_SIZE];
    NAMETYPEMATCH   intendedPressSubDict[WARNINGS_SUBDICT_SIZE];
    NAMETYPEMATCH   actualPressSubDict[WARNINGS_SUBDICT_SIZE];

    for (i = 0; i < WARNINGS_SUBDICT_SIZE; i++) {
      deviceSubDict[i] = warningsSubDict[i];
      toneCurveSubDict[i] = warningsSubDict[i];
      intendedPressSubDict[i] = warningsSubDict[i];
      actualPressSubDict[i] = warningsSubDict[i];
    }

    if ((deviceWarnings != NULL) && !dictmatch(deviceWarnings, deviceSubDict))
      return FALSE;
    if ((toneCurveWarnings != NULL) && !dictmatch(toneCurveWarnings, toneCurveSubDict))
      return FALSE;
    if ((intendedPressWarnings != NULL) && !dictmatch(intendedPressWarnings, intendedPressSubDict))
      return FALSE;
    if ((actualPressWarnings != NULL) && !dictmatch(actualPressWarnings, actualPressSubDict))
      return FALSE;

    MONITORF_INCOMPATIBLE;

    if (warnings->incompatibleResolution)
      printIncompatibleResolutions(deviceSubDict[0].result, toneCurveSubDict[0].result,
                                   intendedPressSubDict[0].result, actualPressSubDict[0].result);

    if (warnings->incompatibleExposure)
      printIncompatibleExposures(deviceSubDict[1].result, toneCurveSubDict[1].result,
                                 intendedPressSubDict[1].result, actualPressSubDict[1].result);

    if (warnings->incompatibleNegativePrint)
      printIncompatibleNegativePrints(deviceSubDict[2].result, toneCurveSubDict[2].result,
                                      intendedPressSubDict[2].result, actualPressSubDict[2].result);

    if (warnings->incompatibleHalftoneName)
      printIncompatibleHalftoneNames(deviceSubDict[3].result, toneCurveSubDict[3].result,
                                     intendedPressSubDict[3].result, actualPressSubDict[3].result);

    if (warnings->incompatibleFrequency)
      printIncompatibleFrequencies(deviceSubDict[4].result, toneCurveSubDict[4].result,
                                   intendedPressSubDict[4].result, actualPressSubDict[4].result);

    if (warnings->missingCalibrationAbort)
      return error_handler(RANGECHECK);
  }

  return TRUE;
}

static void printIncompatibleResolutions(OBJECT *deviceResolution,
                                         OBJECT *toneCurveResolution,
                                         OBJECT *intendedPressResolution,
                                         OBJECT *actualPressResolution)
{
  MONITORF_RESOLUTION_WITH_LEADER;
  if (deviceResolution != NULL) {
    MONITORF_DEVICE_CURVE;
    MONITORF_RESOLUTION_VALUES(realOrIntegerElement(deviceResolution, 0),
                               realOrIntegerElement(deviceResolution, 1));
    MONITORF_LINE_END;
  }
  if (toneCurveResolution != NULL) {
    MONITORF_TONE_CURVE;
    MONITORF_RESOLUTION_VALUES(realOrIntegerElement(toneCurveResolution, 0),
                               realOrIntegerElement(toneCurveResolution, 1));
    MONITORF_LINE_END;
  }
  if (intendedPressResolution != NULL) {
    MONITORF_INTENDED_PRESS_CURVE;
    MONITORF_RESOLUTION_VALUES(realOrIntegerElement(intendedPressResolution, 0),
                               realOrIntegerElement(intendedPressResolution, 1));
    MONITORF_LINE_END;
  }
  if (actualPressResolution != NULL) {
    MONITORF_ACTUAL_PRESS_CURVE;
    MONITORF_RESOLUTION_VALUES(realOrIntegerElement(actualPressResolution, 0),
                               realOrIntegerElement(actualPressResolution, 1));
    MONITORF_LINE_END;
  }
}

static void printIncompatibleExposures(OBJECT *deviceExposure,
                                       OBJECT *toneCurveExposure,
                                       OBJECT *intendedPressExposure,
                                       OBJECT *actualPressExposure)
{
  MONITORF_EXPOSURE_WITH_LEADER;
  if (deviceExposure != NULL) {
    MONITORF_DEVICE_CURVE;
    MONITORF_EXPOSURE_VALUE(oInteger(*deviceExposure));
    MONITORF_LINE_END;
  }
  if (toneCurveExposure != NULL) {
    MONITORF_TONE_CURVE;
    MONITORF_EXPOSURE_VALUE(oInteger(*toneCurveExposure));
    MONITORF_LINE_END;
  }
  if (intendedPressExposure != NULL) {
    MONITORF_INTENDED_PRESS_CURVE;
    MONITORF_EXPOSURE_VALUE(oInteger(*intendedPressExposure));
    MONITORF_LINE_END;
  }
  if (actualPressExposure != NULL) {
    MONITORF_ACTUAL_PRESS_CURVE;
    MONITORF_EXPOSURE_VALUE(oInteger(*actualPressExposure));
    MONITORF_LINE_END;
  }
}

static void printIncompatibleNegativePrints(OBJECT *deviceNegativePrint,
                                            OBJECT *toneCurveNegativePrint,
                                            OBJECT *intendedPressNegativePrint,
                                            OBJECT *actualPressNegativePrint)
{
  MONITORF_NEGATIVE_PRINT_WITH_LEADER;
  if (deviceNegativePrint != NULL) {
    MONITORF_DEVICE_CURVE;
    MONITORF_NEGATIVE_PRINT_VALUE(oBool(*deviceNegativePrint));
    MONITORF_LINE_END;
  }
  if (toneCurveNegativePrint != NULL) {
    MONITORF_TONE_CURVE;
    MONITORF_NEGATIVE_PRINT_VALUE(oBool(*toneCurveNegativePrint));
    MONITORF_LINE_END;
  }
  if (intendedPressNegativePrint != NULL) {
    MONITORF_INTENDED_PRESS_CURVE;
    MONITORF_NEGATIVE_PRINT_VALUE(oBool(*intendedPressNegativePrint));
    MONITORF_LINE_END;
  }
  if (actualPressNegativePrint != NULL) {
    MONITORF_ACTUAL_PRESS_CURVE;
    MONITORF_NEGATIVE_PRINT_VALUE(oBool(*actualPressNegativePrint));
    MONITORF_LINE_END;
  }
}

static void printIncompatibleHalftoneNames(OBJECT *deviceHalftoneName,
                                           OBJECT *toneCurveHalftoneName,
                                           OBJECT *intendedPressHalftoneName,
                                           OBJECT *actualPressHalftoneName)
{
  MONITORF_HALFTONE_NAME_WITH_LEADER;
  if (deviceHalftoneName != NULL) {
    MONITORF_DEVICE_CURVE;
    MONITORF_HALFTONE_NAME_VALUE(theICList(oName(*deviceHalftoneName)));
    MONITORF_LINE_END;
  }
  if (toneCurveHalftoneName != NULL) {
    MONITORF_TONE_CURVE;
    MONITORF_HALFTONE_NAME_VALUE(theICList(oName(*toneCurveHalftoneName)));
    MONITORF_LINE_END;
  }
  if (intendedPressHalftoneName != NULL) {
    MONITORF_INTENDED_PRESS_CURVE;
    MONITORF_HALFTONE_NAME_VALUE(theICList(oName(*intendedPressHalftoneName)));
    MONITORF_LINE_END;
  }
  if (actualPressHalftoneName != NULL) {
    MONITORF_ACTUAL_PRESS_CURVE;
    MONITORF_HALFTONE_NAME_VALUE(theICList(oName(*actualPressHalftoneName)));
    MONITORF_LINE_END;
  }
}

static void printIncompatibleFrequencies(OBJECT *deviceFrequency,
                                         OBJECT *toneCurveFrequency,
                                         OBJECT *intendedPressFrequency,
                                         OBJECT *actualPressFrequency)
{
  MONITORF_FREQUENCY_WITH_LEADER;
  if (deviceFrequency != NULL) {
    MONITORF_DEVICE_CURVE;
    MONITORF_FREQUENCY_VALUES(realOrIntegerElement(deviceFrequency, 0),
                              realOrIntegerElement(deviceFrequency, 1));
    MONITORF_LINE_END;
  }
  if (toneCurveFrequency != NULL) {
    MONITORF_TONE_CURVE;
    MONITORF_FREQUENCY_VALUES(realOrIntegerElement(toneCurveFrequency, 0),
                              realOrIntegerElement(toneCurveFrequency, 1));
    MONITORF_LINE_END;
  }
  if (intendedPressFrequency != NULL) {
    MONITORF_INTENDED_PRESS_CURVE;
    MONITORF_FREQUENCY_VALUES(realOrIntegerElement(intendedPressFrequency, 0),
                              realOrIntegerElement(intendedPressFrequency, 1));
    MONITORF_LINE_END;
  }
  if (actualPressFrequency != NULL) {
    MONITORF_ACTUAL_PRESS_CURVE;
    MONITORF_FREQUENCY_VALUES(realOrIntegerElement(actualPressFrequency, 0),
                              realOrIntegerElement(actualPressFrequency, 1));
    MONITORF_LINE_END;
  }
}

/* ---------------------------------------------------------------------- */

Bool gsc_report_uncalibrated_screens(GS_COLORinfo *colorInfo,
                                     DL_STATE     *page)
{
  GS_CALIBRATIONinfo    *calibrationInfo;
  Bool                  calibrationWarning;
  COLORANTINDEX         ci;
  HTTYPE                httype;
  SPOTNO                currentSpotno = 0;
  uint8                 *dummy_reportName = NULL;
  SYSTEMVALUE           firstFrequency = 0.0;
  Bool                  firstWarningIssued = FALSE;
  SYSTEMVALUE           frequency;
  Bool                  firstScreenForSpotno;
  Bool                  frequenciesAreDifferent;
  Bool                  issueWarning;
  int32                 levelsUsed;
  uint8                 reportName[200];
  void*                 screenHTHandle;
  Bool                  screenReported;
  NAMECACHE             *sfColor;
  NAMECACHE             *sfName;
  SPOTNO                spotno;
  Bool                  spotnoFound;
  SPOTNO                startSpotno;
  CC_WARNINGS_INFO      *warnings;

  /* No calibration, no warnings.
   */
  calibrationInfo = colorInfo->calibrationInfo;
  if (calibrationInfo == NULL)
    return TRUE;

  if (oType(calibrationInfo->calibrationObject) != ODICTIONARY)
    return TRUE;

  warnings = &calibrationInfo->warnings;

  /* Clear all screen reported flags to help prevent unnecessary duplicate
   * reporting of screens. */
  ht_clear_all_screen_reported_flags(page->eraseno);

  /* Cycle around the whole of the halftone cache looking for uncalibrated
   * screens that should be reported.
   */
  screenHTHandle = NULL;
  startSpotno = SPOT_NO_INVALID;
  do {
    spotnoFound = FALSE;
    issueWarning = FALSE;
    frequenciesAreDifferent = FALSE;
    firstScreenForSpotno = TRUE;
    do {
      screenHTHandle = ht_calibration_screen_info(screenHTHandle,
                                                  startSpotno, page->eraseno,
                                                  &spotno, &httype, &ci,
                                                  &calibrationWarning,
                                                  &levelsUsed,
                                                  &sfName,
                                                  &frequency,
                                                  &sfColor,
                                                  dummy_reportName,
                                                  0,
                                                  &screenReported);
      if (screenHTHandle != NULL) {
        spotnoFound = TRUE;

        if (firstScreenForSpotno) {
          currentSpotno = spotno;
          firstFrequency = frequency;
        }
        firstScreenForSpotno = FALSE;

        /* If a screen is not marked as uncalibrated or no levels are
           used, ignore it. */
        if (calibrationWarning && (levelsUsed != 0) && !screenReported)
          issueWarning = TRUE;
        if (frequency != firstFrequency)
          frequenciesAreDifferent = TRUE;
      }
    } while (screenHTHandle != NULL);
    startSpotno = spotno;

    if (spotnoFound && issueWarning) {
      if (!firstWarningIssued)
        printRequiredScreen(warnings);

      firstWarningIssued = TRUE;

      MONITORF_LINE_LEADER;

      /* If there is more than one entry for this spotno then we want to print
       * frequencies for each colorant. Otherwise just print a generic frequency.
       * First determine if they are different.
       */
      if (frequenciesAreDifferent) {
        MONITORF_FREQUENCIES;
        MONITORF_LINE_END;

        MONITORF_LINE_LEADER;
        MONITORF_SPACE;
        MONITORF_SPACE;
      }
      else
        MONITORF_FREQUENCY;

      screenHTHandle = NULL;
      do {
        int32   dummy_screenReported;

        screenHTHandle = ht_calibration_screen_info(screenHTHandle,
                                                    currentSpotno,
                                                    page->eraseno,
                                                    &spotno, &httype, &ci,
                                                    &calibrationWarning,
                                                    &levelsUsed,
                                                    &sfName,
                                                    &frequency,
                                                    &sfColor,
                                                    reportName,
                                                    sizeof (reportName),
                                                    &dummy_screenReported);
        if (frequenciesAreDifferent) {
          if (calibrationWarning && (levelsUsed != 0)) {
            MONITORF_COLORANT_VALUE(theICList(sfColor));
            MONITORF_SPACE;
            MONITORF_FREQUENCY_VALUE(frequency);
            MONITORF_LINE_END;

            MONITORF_LINE_LEADER;
            MONITORF_SPACE;
            MONITORF_SPACE;
          }
        } else {
          MONITORF_FREQUENCY_VALUE(frequency);
          MONITORF_SEPARATOR;
        }
      } while (screenHTHandle != NULL);


      MONITORF_RESOLUTION;
      MONITORF_RESOLUTION_VALUES(page->xdpi, page->ydpi);
      MONITORF_SEPARATOR;

      MONITORF_HALFTONE_NAME;
      MONITORF_HALFTONE_NAME_VALUE(reportName);
      MONITORF_SEPARATOR;

      MONITORF_EXPOSURE;
      MONITORF_EXPOSURE_VALUE(page->colorPageParams.exposure);
      MONITORF_SEPARATOR;

      MONITORF_NEGATIVE_PRINT_VALUE(page->colorPageParams.negativePrint);
      MONITORF_LINE_END;
    }
    /* stop if iteration reached end or wrapped around */
  } while (startSpotno != SPOT_NO_INVALID && startSpotno > currentSpotno);

  if (firstWarningIssued && warnings->missingCalibrationAbort)
    return error_handler(RANGECHECK);

  return TRUE;
}

static void printRequiredScreen(CC_WARNINGS_INFO *warnings)
{
  MONITORF_REQUIRED_SCREEN;
  MONITORF_LINE_LEADER;

  MONITORF_FREQUENCY;
  if (warnings->frequencyPresent)
    MONITORF_FREQUENCY_VALUES(warnings->lowFrequency, warnings->highFrequency);
  else
    MONITORF_NA;
  MONITORF_SEPARATOR;

  MONITORF_RESOLUTION;
  if (warnings->resolutionPresent)
    MONITORF_RESOLUTION_VALUES(warnings->xResolution, warnings->yResolution);
  else
    MONITORF_NA;
  MONITORF_SEPARATOR;

  MONITORF_HALFTONE_NAME;
  if (warnings->halftoneNamePresent) {
    uint8 buf[200];
    if (ht_getExternalScreenName(buf, warnings->halftoneName, sizeof (buf)))
      MONITORF_HALFTONE_NAME_VALUE(buf);
    else
      MONITORF_HALFTONE_NAME_VALUE(theICList(system_names + NAME_Unknown));
  }
  else
    MONITORF_NA;
  MONITORF_SEPARATOR;

  MONITORF_EXPOSURE;
  if (warnings->exposurePresent)
    MONITORF_EXPOSURE_VALUE(warnings->exposure);
  else
    MONITORF_NA;
  MONITORF_SEPARATOR;

  if (warnings->negativePrintPresent)
    MONITORF_NEGATIVE_PRINT_VALUE(warnings->negativePrint);
  else
    MONITORF_NA;
  MONITORF_LINE_END;

  MONITORF_NO_MATCH;
}

/* ---------------------------------------------------------------------- */


static Bool is_calibration_valid(GS_COLORinfo *colorInfo,
                                 DL_STATE     *page,
                                 NAMECACHE    *sfName,
                                 SYSTEMVALUE  frequency)
{
  CC_WARNINGS_INFO *warnings = &colorInfo->calibrationInfo->warnings;

  /* Compare with the expected values */
  if (warnings->resolutionPresent && !warnings->incompatibleResolution)
    if (warnings->xResolution != page->xdpi ||
        warnings->yResolution != page->ydpi)
      return FALSE;

  if (warnings->exposurePresent && !warnings->incompatibleExposure)
    if (warnings->exposure != page->colorPageParams.exposure)
      return FALSE;

  if (warnings->negativePrintPresent && !warnings->incompatibleNegativePrint)
    if (warnings->negativePrint != page->colorPageParams.negativePrint)
      return FALSE;

  if (page->colorPageParams.halftoning) {
    if (warnings->halftoneNamePresent && !warnings->incompatibleHalftoneName)
      if (warnings->halftoneName != sfName)
        return FALSE;

    if (warnings->frequencyPresent && !warnings->incompatibleFrequency)
      if ((warnings->lowFrequency > frequency) ||
          (warnings->lowFrequency > frequency))
        return FALSE;
  }
  return TRUE;
}


void cc_note_uncalibrated_screens(GS_COLORinfo *colorInfo,
                                  DL_STATE     *page,
                                  int32         requiredSpotno)
{
  Bool                  calibrationWarning;
  HTTYPE                httype;
  COLORANTINDEX         ci;
  int32                 dummy_levelsUsed;
  Bool                  dummy_screenReported;
  uint8                 *dummy_reportName = NULL;
  NAMECACHE             *dummy_sfColor;
  SYSTEMVALUE           frequency;
  void*                 htHandle;
  NAMECACHE             *sfName;
  SPOTNO                spotno;

  HQASSERT(colorInfo != NULL, "NULL colorInfo in gsc_note_calibrated_screen");

  /* No calibration, no warnings. */
  if (colorInfo->calibrationInfo == NULL)
    return;
  if (oType(colorInfo->calibrationInfo->calibrationObject) != ODICTIONARY)
    return;

  /* Loop round all colorants for this spot number and check the criteria.
   */
  htHandle = NULL;
  do {
    htHandle = ht_calibration_screen_info(htHandle,
                                          requiredSpotno, page->eraseno,
                                          &spotno, &httype, &ci,
                                          &calibrationWarning,
                                          &dummy_levelsUsed,
                                          &sfName,
                                          &frequency,
                                          &dummy_sfColor,
                                          dummy_reportName,
                                          0,
                                          &dummy_screenReported);

    /* If the screen is already marked as uncalibrated, don't bother rechecking.
     */
    if (htHandle != NULL && !calibrationWarning) {
      if (!is_calibration_valid(colorInfo, page, sfName, frequency))
        ht_setCalibrationWarning(spotno, httype, ci, TRUE);
    }
  } while (htHandle != NULL);
}

/* ---------------------------------------------------------------------- */

static USERVALUE realOrIntegerElement(OBJECT *object, int32 index)
{
  OBJECT    *array = oArray(*object);

  HQASSERT(theLen(*object) > index, "Index out of bounds");
  HQASSERT(oType(array[index]) == OREAL || oType(array[index]) == OINTEGER,
           "Invalid type, should have been error checked before here");

  return (oType(array[index]) == OREAL) ?
          oReal(array[index]) :
          (USERVALUE) oInteger(array[index]);
}

/* Log stripped */
