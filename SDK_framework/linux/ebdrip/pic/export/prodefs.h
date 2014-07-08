/** \file prodefs.h
 * \addtogroup pic Platform Independent Plugin Interface
 * \ingroup interface
 * \brief Plugin API Interface: Exported macros and typedefs required by the CRD Plugins.
 */

/* $HopeName: SWpic!export:prodefs.h(EBDSDK_P.1) $
 *
 * Copyright (c) 1995-2011 Global Graphics Software Ltd.  All rights reserved.
 *
 */

#ifndef _PRODEFS_H_
#define _PRODEFS_H_

/******************************************************************************/
/* Definitions */
/******************************************************************************/


/**
 * Unique codes for each color. Note that the codes are made up of a unique part, and the bottom
 * 12 bit part which indicates an index for use in the PROProfile_t.Linearization array field
 *@{
 */
#define PRO_GRAY_CHANNEL     (0x0001000 + 0) /**< \brief Gray profile channel  */
#define PRO_RED_CHANNEL      (0x0002000 + 0) /**< \brief Red profile channel   */
#define PRO_GREEN_CHANNEL    (0x0004000 + 1) /**< \brief Green profile channel */
#define PRO_BLUE_CHANNEL     (0x0008000 + 2) /**< \brief Blue profile channel  */
#define PRO_CYAN_CHANNEL     (0x0010000 + 0) /**< \brief Cyan profile channel  */
#define PRO_MAGENTA_CHANNEL  (0x0020000 + 1) /**< \brief Magenta profile channel */
#define PRO_YELLOW_CHANNEL   (0x0040000 + 2) /**< \brief Yellow profile channel  */
#define PRO_BLACK_CHANNEL    (0x0080000 + 3) /**< \brief Black profile channel */
#define PRO_DEFAULT_CHANNEL  (0x0100000 + 0) /**< \brief Default profile channel */
#define PRO_SPOT_CHANNEL     (0x0200000 + 0) /**< \brief Spot colour profile channel */
/*@}*/


/**
 * Used to convert between the above color codes, and the appropriate index for use when
 * referencing the PROProfile_t.pLinearizations array
 */
#define PRO_CHANNEL_INDEX(code) (code & 0xFFF)

/**
 * Mask out Linearization array index
 */
#define PRO_CHANNEL_COLOR(code) (code & ~0xFFF)


/**
 * Used in the PROProfile_t.ProfileColorSpace field
 */
#define PRO_DEVICE_UNKNOWN   (-1)   /**< \brief Signals the colorspace is (currently) unknown */

#define PRO_DEVICE_GRAY      (0)    /**< \brief The DeviceGray colorspace. For PROProfile_t.ProfileColorSpace. */
#define PRO_DEVICE_RGB       (1)    /**< \brief The DeviceRGB colorspace. For PROProfile_t.ProfileColorSpace. */
#define PRO_DEVICE_CMY       (2)    /**< \brief The DeviceCMY colorspace. For PROProfile_t.ProfileColorSpace. */
#define PRO_DEVICE_CMYK      (3)    /**< \brief The DeviceCMYK colorspace. For PROProfile_t.ProfileColorSpace. */
#define PRO_DEVICE_N         (4)    /**< \brief The DeviceN colorspace - arbitrary numbers of colors. For PROProfile_t.ProfileColorSpace.*/

#define PRO_DEVICE_TOTAL     (5)

/**
 * Used in various PROProfile.ValidFor... fields to indicate any/all are valid
 */
#define PRO_ANY_VALID        (0)

/** Reserved name for profile that defines default linearization.
 * For special press device this is identity function.
 * Color information needs to be well formed but will not be used.
 */
#define PRO_LINEAR           "Linear"

/*@{*/
/** Reserved names for colorants used within profiles.
 */
#define PRO_CYAN             "Cyan"
#define PRO_MAGENTA          "Magenta"
#define PRO_YELLOW           "Yellow"
#define PRO_BLACK            "Black"
#define PRO_RED              "Red"
#define PRO_GREEN            "Green"
#define PRO_BLUE             "Blue"
#define PRO_GRAY             "Gray"
#define PRO_DEFAULT          "Default"
/*@}*/

/** MAXPATCHNAMELENGTH indicates the allocated size of szPatchName (formerly "pszPatchName")
 * According to Peter Barada, this field will probably not exceed 5 characters. Here we've allocated
 * 8 for safety's sake.  - Owen 1/9/96
 */
#define MAXPATCHNAMELENGTH      8


/** PRO_PATCH_UNKNOWN indicates that the index of the white or black patch is unknown */
#define PRO_PATCH_UNKNOWN      -1

/** PRO_KEY_UNKNOWN indicates that a particular key is unknown (not present) in the profile */
#define PRO_KEY_UNKNOWN        -1

/******************************************************************************/
/* Typedefs */
/******************************************************************************/


/**
 * Defines a resolution in lines per inch.
 */
typedef struct PROResolution_s
{
  int32 X;
  int32 Y;
} PROResolution_t, *pPROResolution_t;



/**
 * Defiles the details of a screening strategy
 */
typedef struct PROHalftone_s
{
  int32  fHalftoned;
  int32  Frequency;
  uint8* pszSpotFunction;
} PROHalftone_t, *pPROHalftone_t;



/**
 * Defines a single point in a linearization curve.
 */
typedef struct PROCurveValue_s
{
  double ScriptWorksGoldenValue;
  double NormalizedDeviceCode;
} PROCurveValue_t, *pPROCurveValue_t;



/**
 * Defines a single entry in a profile conversion table
 */
typedef struct PROConversionValue_s
{
  double LinearizationSpaceValue;
  double ScriptWorksNominalValue;
} PROConversionValue_t, *pPROConversionValue_t;



/**
 * Defines a profile conversion table
 */
typedef struct PROConversionTable_s
{
  uint8*                pszTableName;
  int32                 NumberOfTableValues;
  uint8*                pszFilter;
  pPROConversionValue_t pTableValues;
  int32                 fRelative;
  int32                 fSubtractPaperWhite;
  uint8*                pszConversionFormula;
} PROConversionTable_t, *pPROConversionTable_t;



/**
 * Defines a set of linearization data for a single channel
 */
typedef struct PROLinearization_s
{
  uint8*                pszChannelColor;
  int32                 NumberOfDefaultCurveValues;
  pPROCurveValue_t      pDefaultCurve;
  int32                 NumberOfConversionTables;
  pPROConversionTable_t pConversionTables;
  int32                 iReserved; /* not presently used, must be zero */
} PROLinearization_t, *pPROLinearization_t;



/**
 * Defines the measurement data for a single patch
 *
 * szPatchName was formerly known as pszPatchName when it was a real pointer.
 * It has been changed to reflect its new status under Hungarian notation.
 */
typedef struct PROMeasurement_s
{
  char szPatchName[MAXPATCHNAMELENGTH];
/*
  double PatchColor[PRO_MAX_CHANNELS];
*/
  double MeasuredX;
  double MeasuredY;
  double MeasuredZ;
} PROMeasurement_t, *pPROMeasurement_t;



/***
 * Defines the intent-name --> external-CRD-name dictionary entry
 */
typedef struct PROExternalCRD_s
{
  uint8 *pszIntentName;
  uint8 *pszExternalCRDFileName;
} PROExternalCRD_t, *pPROExternalCRD_t;



/***
 * Defines the overall profile information
 * NOTE: Any new entries should go to the bottom of the list
 */
typedef struct PROProfile_s
{
  int32                 ProfileVersion;
  uint8*                pszProfileID;
  uint8*                pszLastModified;
  int32                 ProfileColorSpace;
  int32                 NumberOfProfileColorants;
  uint8**               ppszProfileColorants;
  uint8*                pszDeviceType;
  uint8*                pszDeviceSerialNumber;
  uint8*                pszMediaType;
  uint8*                pszColorantType;
  PROResolution_t       Resolution;
  PROHalftone_t         Halftone;
  uint8*                pszValidForComment;
  int32                 NumberOfValidDeviceTypes;
  uint8**               ppszValidForDeviceTypes;
  int32                 NumberOfValidMediaTypes;
  uint8**               ppszValidForMediaTypes;
  int32                 NumberOfValidColorantTypes;
  uint8**               ppszValidForColorantTypes;
  int32                 NumberOfValidResolutions;
  pPROResolution_t      pValidForResolutions;
  int32                 NumberOfValidHalftones;
  pPROHalftone_t        pValidForHalftones;
  uint8*                pszComments;
  uint8*                pszLinearizationName;
  int32                 NumberOfLinearizations;
  pPROLinearization_t   pLinearizations;
  uint8*                pszSourceICCProfileName;
  int32                 NumberOfExternalCRDs;
  pPROExternalCRD_t     pExternalCRDs;
  uint8*                pszMeasuringInstrument;
  uint8*                pszIlluminant;
  double                IlluminantX;
  double                IlluminantY;
  double                IlluminantZ;
  uint8*                pszObserver;
  int32                 NumberOfMeasurements;
  pPROMeasurement_t     pMeasurements;
  int32                 NumberOfPatchColors;
  double              * pPatchColors;
  int32                 iWhitePatch;
  int32                 iBlackPatch;
  int32                 RelativeWhitePresent;
  int32                 RelativeBlackPresent;
  double                RelativeWhiteX;
  double                RelativeWhiteY;
  double                RelativeWhiteZ;
  double                RelativeBlackX;
  double                RelativeBlackY;
  double                RelativeBlackZ;
  uint8*                pszDefaultBlackGeneration;
  int32                 RenderTableSizeNa;
  int32                 RenderTableSizeNb;
  int32                 RenderTableSizeNc;
  int32                 MeasurementDataPresent;
  uint8*                pszMeasurement;
  int32                 fColRenderTable16bit;
  int32                 fPercepRenderTable16bit;
  int32                 fSatRenderTable16bit;
  int32                 fRenderTableFixedRGB;
  double                IterationTolerance;
  double                ColMinRemapDist;
  int32                 fColLimitRemapToMinDist;
  double                YRRelativeDensityL;
  double                YRRelativeDensityA;
  double                YRRelativeDensityB;
  double                MRRelativeDensityL;
  double                MRRelativeDensityA;
  double                MRRelativeDensityB;
} PROProfile_t, *pPROProfile_t;


#ifdef NEVER
/*
* Log stripped */
#endif /* NEVER */
#endif /* _PRODEFS_H_*/

/* EOF prodefs.c */
