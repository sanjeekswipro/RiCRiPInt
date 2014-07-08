/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:tifftags.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Utility routines to access Tiff tags
 */

#include "core.h"

#include "mm.h"         /* mm_alloc */
#include "swerrors.h"   /* IOERROR */
#include "objects.h"    /* OBJECT */
#include "monitor.h"

#include "t6params.h"
#include "ifdreadr.h"
#include "tifftags.h"
#include "hqmemcpy.h"   /* HqMemCpy */


/*
 * Array of supported tag names
 */
ifd_ifdentry_data_t tiff6_known_tags[] = {
  { TAG_NewSubfileType,              0, (uint8 *)("NewSubfileType"),               TRUE },
  { TAG_SubfileType,                 0, (uint8 *)("SubfileType"),                  TRUE },
  { TAG_ImageWidth,                  0, (uint8 *)("ImageWidth"),                   TRUE },
  { TAG_ImageLength,                 0, (uint8 *)("ImageLength"),                  TRUE },
  { TAG_BitsPerSample,               0, (uint8 *)("BitsPerSample"),                TRUE },
  { TAG_Compression,                 0, (uint8 *)("Compression"),                  TRUE },
  { TAG_PhotometricInterpretation,   0, (uint8 *)("PhotometricInterpretation"),    TRUE },
  { TAG_Thresholding,                0, (uint8 *)("Thresholding"),                 TRUE },
  { TAG_CellWidth,                   0, (uint8 *)("CellWidth"),                    TRUE },
  { TAG_CellLength,                  0, (uint8 *)("CellLength"),                   TRUE },
  { TAG_FillOrder,                   0, (uint8 *)("FillOrder"),                    TRUE },
  { TAG_DocumentName,                0, (uint8 *)("DocumentName"),                 TRUE },
  { TAG_ImageDescription,            0, (uint8 *)("ImageDescription"),             TRUE },
  { TAG_Make,                        0, (uint8 *)("Make"),                         TRUE },
  { TAG_Model,                       0, (uint8 *)("Model"),                        TRUE },
  { TAG_StripOffsets,                0, (uint8 *)("StripOffsets"),                 TRUE },
  { TAG_Orientation,                 0, (uint8 *)("Orientation"),                  TRUE },
  { TAG_SamplesPerPixel,             0, (uint8 *)("SamplesPerPixel"),              TRUE },
  { TAG_RowsPerStrip,                0, (uint8 *)("RowsPerStrip"),                 TRUE },
  { TAG_StripByteCounts,             0, (uint8 *)("StripByteCounts"),              TRUE },
  { TAG_MinSampleValue,              0, (uint8 *)("MinSampleValue"),               TRUE },
  { TAG_MaxSampleValue,              0, (uint8 *)("MaxSampleValue"),               TRUE },
  { TAG_XResolution,                 0, (uint8 *)("XResolution"),                  TRUE },
  { TAG_YResolution,                 0, (uint8 *)("YResolution"),                  TRUE },
  { TAG_PlanarConfiguration,         0, (uint8 *)("PlanarConfiguration"),          TRUE },
  { TAG_PageName,                    0, (uint8 *)("PageName"),                     TRUE },
  { TAG_XPosition,                   0, (uint8 *)("XPosition"),                    TRUE },
  { TAG_YPosition,                   0, (uint8 *)("YPosition"),                    TRUE },
  { TAG_FreeOffsets,                 0, (uint8 *)("FreeOffsets"),                  TRUE },
  { TAG_FreeByteCounts,              0, (uint8 *)("FreeByteCounts"),               TRUE },
  { TAG_GrayResponseUnit,            0, (uint8 *)("GrayResponseUnit"),             TRUE },
  { TAG_GrayResponseCurve,           0, (uint8 *)("GrayResponseCurve"),            TRUE },
  { TAG_T4Options,                   0, (uint8 *)("T4Options"),                    TRUE },
  { TAG_T6Options,                   0, (uint8 *)("T6Options"),                    TRUE },
  { TAG_ResolutionUnit,              0, (uint8 *)("ResolutionUnit"),               TRUE },
  { TAG_PageNumber,                  0, (uint8 *)("PageNumber"),                   TRUE },
  { TAG_TransferFunction,            0, (uint8 *)("TransferFunction"),             TRUE },
  { TAG_Software,                    0, (uint8 *)("Software"),                     TRUE },
  { TAG_DateTime,                    0, (uint8 *)("DateTime"),                     TRUE },
  { TAG_Artist,                      0, (uint8 *)("Artist"),                       TRUE },
  { TAG_HostComputer,                0, (uint8 *)("HostComputer"),                 TRUE },
  { TAG_Predictor,                   0, (uint8 *)("Predictor"),                    TRUE },
  { TAG_WhitePoint,                  0, (uint8 *)("WhitePoint"),                   TRUE },
  { TAG_PrimaryChromaticities,       0, (uint8 *)("PrimaryChromaticities"),        TRUE },
  { TAG_ColorMap,                    0, (uint8 *)("ColorMap"),                     TRUE },
  { TAG_HalftoneHints,               0, (uint8 *)("HalftoneHints"),                TRUE },
  { TAG_TileWidth,                   0, (uint8 *)("TileWidth"),                    TRUE },
  { TAG_TileLength,                  0, (uint8 *)("TileLength"),                   TRUE },
  { TAG_TileOffsets,                 0, (uint8 *)("TileOffsets"),                  TRUE },
  { TAG_TileByteCounts,              0, (uint8 *)("TileByteCounts"),               TRUE },
  { TAG_SubIFDs,                     0, (uint8 *)("SubIFDs"),                      FALSE },
  { TAG_InkSet,                      0, (uint8 *)("InkSet"),                       TRUE },
  { TAG_InkNames,                    0, (uint8 *)("InkNames"),                     TRUE },
  { TAG_NumberOfInks,                0, (uint8 *)("NumberOfInks"),                 TRUE },
  { TAG_DotRange,                    0, (uint8 *)("DotRange"),                     TRUE },
  { TAG_TargetPrinter,               0, (uint8 *)("TargetPrinter"),                TRUE },
  { TAG_ExtraSamples,                0, (uint8 *)("ExtraSamples"),                 TRUE },
  { TAG_SampleFormat,                0, (uint8 *)("SampleFormat"),                 TRUE },
  { TAG_SMinSampleValue,             0, (uint8 *)("SMinSampleValue"),              TRUE },
  { TAG_SMaxSampleValue,             0, (uint8 *)("SMaxSampleValue"),              TRUE },
  { TAG_TransferRange,               0, (uint8 *)("TransferRange"),                TRUE },
  { TAG_ClipPath,                    0, (uint8 *)("ClipPath"),                     FALSE },
  { TAG_XClipPathUnits,              0, (uint8 *)("XClipPathUnits"),               FALSE },
  { TAG_YClipPathUnits,              0, (uint8 *)("YClipPathUnits"),               FALSE },
  { TAG_Indexed,                     0, (uint8 *)("Indexed"),                      FALSE },
  { TAG_JPEGTables,                  0, (uint8 *)("JPEGTables"),                   TRUE },
  { TAG_OPIProxy,                    0, (uint8 *)("OPIProxy"),                     FALSE },
  { TAG_JPEGProc,                    0, (uint8 *)("JPEGProc"),                     TRUE },
  { TAG_JPEGInterchangeFormat,       0, (uint8 *)("JPEGInterchangeFormat"),        TRUE },
  { TAG_JPEGInterchangeFormatLength, 0, (uint8 *)("JPEGInterchangeFormatLength"),  TRUE },
  { TAG_JPEGRestartInterval,         0, (uint8 *)("JPEGRestartInterval"),          TRUE },
  { TAG_JPEGLosslessPredictors,      0, (uint8 *)("JPEGLosslessPredictors"),       TRUE },
  { TAG_JPEGPointTransforms,         0, (uint8 *)("JPEGPointTransforms"),          TRUE },
  { TAG_JPEGQTables,                 0, (uint8 *)("JPEGQTables"),                  TRUE },
  { TAG_JPEGDCTables,                0, (uint8 *)("JPEGDCTables"),                 TRUE },
  { TAG_JPEGACTables,                0, (uint8 *)("JPEGACTables"),                 TRUE },
  { TAG_YCbCrCoefficients,           0, (uint8 *)("YCbCrCoefficients"),            TRUE },
  { TAG_YCbCrSubSampling,            0, (uint8 *)("YCbCrSubSampling"),             TRUE },
  { TAG_YCbCrPositioning,            0, (uint8 *)("YCbCrPositioning"),             TRUE },
  { TAG_ReferenceBlackWhite,         0, (uint8 *)("ReferenceBlackWhite"),          TRUE },
  { TAG_ImageID,                     0, (uint8 *)("ImageID"),                      FALSE },
  { TAG_Copyright,                   0, (uint8 *)("Copyright"),                    TRUE },
  { TAG_IPTC_NAA,                    0, (uint8 *)("IPTC-NAA"),                     FALSE },
  { TAG_Site,                        0, (uint8 *)("Site"),                         TRUE },
  { TAG_ColorSequence,               0, (uint8 *)("ColorSequence"),                TRUE },
  { TAG_RasterPadding,               0, (uint8 *)("RasterPadding"),                TRUE },
  { TAG_BitsPerRunLength,            0, (uint8 *)("BitsPerRunLength"),             TRUE },
  { TAG_BitsPerExtendedRunLength,    0, (uint8 *)("BitsPerExtendedRunLength"),     TRUE },
  { TAG_ColorTable,                  0, (uint8 *)("ColorTable"),                   TRUE },
  { TAG_ImageColorIndicator,         0, (uint8 *)("ImageColorIndicator"),          TRUE },
  { TAG_BackgroundColorIndicator,    0, (uint8 *)("BackgroundColorIndicator"),     TRUE },
  { TAG_ImageColorValue,             0, (uint8 *)("ImageColorValue"),              TRUE },
  { TAG_BackgroundColorValue,        0, (uint8 *)("BackgroundColorValue"),         TRUE },
  { TAG_PixelIntensityRange,         0, (uint8 *)("PixelIntensityRange"),          TRUE },
  { TAG_TransparencyIndicator,       0, (uint8 *)("TransparencyIndicator"),        TRUE },
  { TAG_ColorCharacterization,       0, (uint8 *)("ColorCharacterization"),        TRUE },
  { TAG_Photoshop3ImageResource,     0, (uint8 *)("Photoshop 3.0 Image Resource"), TRUE },
  { TAG_ICCProfile,                  0, (uint8 *)("ICCProfile"),                   FALSE },
  { TAG_ImageSourceData,             0, (uint8 *)("ImageSourceData"),              FALSE },
  { TAG_Annotations,                 0, (uint8 *)("Annotations"),                  FALSE },

  { TAG_Unknown,                     0, (uint8 *)("UnknownTag"),                   FALSE }
};

STATIC uint8* pmi_string[] = {
  (uint8 *)("WhiteIsZero"),
  (uint8 *)("BlackIsZero"),
  (uint8 *)("RGB"),
  (uint8 *)("Palette Color"),
  (uint8 *)("Transparency Mask"),
  (uint8 *)("Separated (CMYK)"),
  (uint8 *)("YCbCr"),
  (uint8 *)("undefined"),
  (uint8 *)("CIE L*a*b*"),
  (uint8 *)("ICCLab")
};

STATIC uint8* orientation_string[] = {
  UNKNOWN_T,
  (uint8 *)("As-is"),
  (uint8 *)("Horizontal mirror"),
  (uint8 *)("Rotated 180 degrees"),
  (uint8 *)("Vertical mirror"),
  (uint8 *)("TL-BR mirror"),
  (uint8 *)("Rotated 90 degrees CW"),
  (uint8 *)("TR-BL mirror"),
  (uint8 *)("Rotated 90 degrees CCW")
};

STATIC uint8* resuints_string[] = {
  UNKNOWN_T,
  (uint8 *)("NONE"),
  (uint8 *)("Inch"),
  (uint8 *)("Centimeter")
};

STATIC uint8* extrasamples_string[] = {
  (uint8 *)("Unspecified data"),
  (uint8 *)("Associated alpha data"),
  (uint8 *)("Unassociated alpha data")
};


STATIC uint8* fillorder_string[] = {
  UNKNOWN_T,
  (uint8 *)("msb to lsb"),
  (uint8 *)("lsb to msb")
};

/*
 * tiff_pmi_string()
 */
uint8* tiff_pmi_string(
  uint32        pmi)            /* I */
{
  if ( /* (pmi >= PMI_WhiteIsZero) && */ (pmi <= PMI_ICCLab) ) {
    return(pmi_string[pmi]);
  }

  return(UNKNOWN_T);

} /* Function tiff_pmi_string */


/*
 * tiff_compression_string()
 */
uint8* tiff_compression_string(
  uint32        compression)        /* I */
{
  static uint8* compression_string[] = {
    UNKNOWN_T,
    (uint8 *)("None"),
    (uint8 *)("CCITT (basic group 3 fax)"),
    (uint8 *)("CCITT T.4 (group 3 fax)"),
    (uint8 *)("CCITT T.6 (group 4 fax)"),
    (uint8 *)("Lempel-Ziv-Welch"),
    (uint8 *)("JPEG (Discrete Cosine Transform - old)"),
    (uint8 *)("JPEG (Discrete Cosine Transform)"),
    (uint8 *)("Deflate (zlib)")
  };

  switch ( compression ) {
  case COMPRESS_None:
  case COMPRESS_CCITT:
  case COMPRESS_CCITT_T4:
  case COMPRESS_CCITT_T6:
  case COMPRESS_LZW:
  case COMPRESS_JPEG_OLD:
  case COMPRESS_JPEG:
  case COMPRESS_FLATE:
    /* These map linearly into an array */
    return(compression_string[compression]);

  case COMPRESS_FLATE_TIFFLIB:
    return((uint8 *)("Deflate (zlib) - obsolete id"));

  case COMPRESS_Packbits:
    /* Packbits is awkward */
    return((uint8 *)("Packbits"));
  }

  return(UNKNOWN_T);

} /* Function tiff_compression_string */




/*
 * tiff_orientation_string()
 */
uint8* tiff_orientation_string(
  uint32        orientation)      /* I */
{

  if ( (orientation > 0) && (orientation < NUM_ARRAY_ITEMS(orientation_string)) ) {
    return(orientation_string[orientation]);
  }

  return(UNKNOWN_T);

} /* Function tiff_orientation_string */


/*
 * tiff_resunits_string()
 */
uint8* tiff_resunits_string(
  uint32        res_units)      /* I */
{
  if ( (res_units >= RESUNIT_NONE) && (res_units <= RESUNIT_CM) ) {
    return(resuints_string[res_units]);
  }

  return(UNKNOWN_T);

} /* Function tiff_resunits_string */


/*
 * tiff_planarconfig_string()
 */
uint8* tiff_planarconfig_string(
  uint32        planar_config)  /* I */
{
  switch ( planar_config ) {
  case PLANAR_CONFIG_CHUNKY:
    return((uint8 *)("Chunky"));

  case PLANAR_CONFIG_PLANAR:
    return((uint8 *)("Planar"));

  default:
    return(UNKNOWN_T);
  }

  /* NEVER REACHED */

} /* Function tiff_type_string */


/*
 * tiff_extrasamples_string()
 */
uint8* tiff_extrasamples_string(
  uint32        extra_samples)  /* I */
{
  if ( /* (extra_samples >= EXTRASAMPLES_UNSPECIFIED_DATA) && */
       (extra_samples <= EXTRASAMPLES_UNASSOCIATED_ALPHA) ) {
    return(extrasamples_string[extra_samples]);
  }

  return(UNKNOWN_T);

} /* Function tiff_extrasamples_string */


/*
 * tiff_fillorder_string()
 */
uint8* tiff_fillorder_string(
  uint32        fill_order)     /* I */
{
  if ( (fill_order >= FILLORDER_MSB_TO_LSB) &&
       (fill_order <= FILLORDER_LSB_TO_MSB) ) {
    return(fillorder_string[fill_order]);
  }

  return(UNKNOWN_T);

} /* Function tiff_fillorder_string */


/* Log stripped */
