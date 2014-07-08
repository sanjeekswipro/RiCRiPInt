/* Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pclcommon.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup OIL
 * \brief PCL Device codes.
 */

#ifndef _PCLCOMMON_H_
#define _PCLCOMMON_H_

#include "std.h"
#include "skinras.h"


#define kCRLF "\x0d\x0a"


/* Compression methods */
enum {
  eNoCompression = 0 ,
  eRLECompression,
  eJPEGCompression,  /* Unsupported. */
  eDeltaRowCompression,
  eReserved,
  eAdaptiveCompression,
};

/** \brief Supported PCL raster formats. */
enum RFLIST {
  RF_MONO_HT = 0,  /* 1bpp Monochrome */
  RF_MONO_CT,      /* 8bpp Grayscale  */
  RF_RGB_CT,       /* 8bpp RGB color  */
  RF_UNKNOWN
};
typedef int eRFLIST;

/* Data tag types: */
#define  UByteDataType   0xc0
#define  UInt16DataType  0xc1
#define  UInt32DataType  0xc2
#define  SInt16DataType  0xc3
#define  SInt32DataType  0xc4
#define  Real32DataType  0xc5
#define  StringType      0xc7
#define  UByteArrayType  0xc8
#define  UInt16ArrayType 0xc9
#define  UInt32ArrayType 0xca
#define  SInt16ArrayType 0xcb
#define  SInt32ArrayType 0xcc
#define  Real32ArrayType 0xcd
#define  UByteXyType     0xd0
#define  UInt16XyType    0xd1
#define  UInt32XyType    0xd2
#define  SInt16XyType    0xd3
#define  SInt32XyType    0xd4
#define  Real32XyType    0xd5

/* Attibute data size type */
#define  ByteAttribId 0xf8

/* Attribute identifier enumerations: */
enum {
  eError = 0,
  eCMYColor,
  ePaletteDepth,
  eColorSpace,
  eNullBrush,
  eNullPen,
  ePaletteData,
  ePaletteIndex,
  ePatternSelectID, /* 8 */
  eGrayLevel,
  eUnspecified_10,
  eRGBColor,
  ePatternOrigin,
  eNewDestinationSize,
  ePrimaryArray,
  ePrimaryDepth,
  eUnspecified_16,
  eColorimetricColorSpace,
  eXYChromaticities,
  eWhitePointReference,
  eCRGBMinMax,
  eGammaGain,
  eUnspecified_22,
  eUnspecified_23,
  eUnspecified_24,
  eUnspecified_25,
  eUnspecified_26,
  eUnspecified_27,
  eUnspecified_28,
  eAllObjectTypes,
  eTextObjects,
  eVectorObjects,
  eRasterObjects, /* 32 */
  eDeviceMatrix,
  eDitherMatrixDataType,
  eDitherOrigin,
  eMediaDest, /* eMediaDestination,  36 */
  eMediaSize,
  eMediaSource,
  eMediaType,
  eOrientation,
  ePageAngle,
  ePageOrigin,
  ePageScale,
  eROP3,
  eTxMode,
  eUnspecified_46,
  eCustomMediaSize,
  eCustomMediaSizeUnits,
  ePageCopies,
  eDitherMatrixSize,
  eDitherMatrixDepth,
  eSimplexPageMode,
  eDuplexPageMode,
  eDuplexPageSide,
  eUnspecified_55,
  eUnspecified_56,
  eUnspecified_57,
  eUnspecified_58,
  eUnspecified_59,
  eUnspecified_60,
  eUnspecified_61,
  eUnspecified_62,
  eLineStartCapStyle,
  eLineEndCapStyle,
  eArcDirection,
  eBoundingBox,
  eDashOffset,
  eEllipseDimension,
  eEndPoint,
  eFillMode,
  eLineCapStyle,
  eLineJoinStyle,
  eMiterLength,
  eLineDashStyle,
  ePenWidth,
  ePoint, /* 76 */
  eNumberOfPoints,
  eSolidLine,
  eStartPoint,
  ePointType,
  eControlPoint1,
  eControlPoint2,
  eClipRegion,
  eClipMode,
  eXPairType,  /* 85 */
  eUnspecified_86,
  eUnspecified_87,
  eUnspecified_88,
  eUnspecified_89,
  eUnspecified_90,
  eUnspecified_91,
  eUnspecified_92,
  eUnspecified_93,
  eUnspecified_94,
  eUnspecified_95,
  eUnspecified_96,
  eColorDepthArray,
  eColorDepth,  /* ePixelDepth */
  eBlockHeight,
  eColorMapping,  /* ePixelEncoding */
  eCompressMode,
  eDestinationBox,
  eDestinationSize,
  ePatternPersistence,
  ePatternDefineID,
  eUnspecified_106,
  eSourceHeight,
  eSourceWidth,
  eStartLine,
  ePadBytesMultiple,  /* eXPairType */
  eBlockByteLength,   /* 111 eNumberOfXPairs */
  eYStart,
  eXStart,
  eXEnd,
  eNumberOfScanLines,
  eUnspecified_116,
  eUnspecified_117,
  eUnspecified_118,
  eUnspecified_119,
  eColorTreatment,
  eFileName,
  eBackgroundName,
  eFormName,
  eFormType,
  eFormSize,
  eUDLCName,
  eUnspecified_127,
  eUnspecified_128,
  eCommentData,
  eDataOrg,
  eUnspecified_131,
  eUnspecified_132,
  eUnspecified_133,
  eMeasure,
  eUnspecified_135,
  eSourceType,
  eUnitsPerMeasure,
  eQueryKey,
  eStreamName,
  eStreamDataLength,
  eUnspecified_141,
  eUnspecified_142,
  eErrorReport,
  eIOReadTimeOut,
};


/* Enumerate Operators : */
enum {
  eBeginSession = 0x41,
  eEndSession,
  eBeginPage,
  eEndPage,
  eSelfTest,
  eVendorUnique,
  eComment,
  eOpenDataSource,
  eCloseDataSource, /* 0x49 */
  eEchoComment,
  eQuery,
  eDiagnostic3,
  eOperator_4d,
  eOperator_4e,
  eBeginFontHeader,
  eReadFontHeader,
  eEndFontHeader,
  eBeginChar = 0x52,
  eReadChar,
  eEndChar,
  eRemoveFont,
  eSetCharAttribute,
  eSetDefaultGS,
  eSetColorTreatment,
  eSetGlobalAttributes,
  eClearGlobalAttributes,
  eBeginStream = 0x5B,
  eReadStream,
  eEndStream,
  eExecStream,
  eRemoveStream,
  ePopGS, /* 0x60 */
  ePushGS,
  eSetClipReplace,
  eSetBrushSource,
  eSetCharAngle,
  eSetCharScale,
  eSetCharShear,
  eSetClipIntersect,
  eSetClipRectangle,
  eSetClipToPage,
  eSetColorSpace,
  eSetCursor,
  eSetCursorRel,
  eSetHalftoneMethod,
  eSetFillMode,
  eSetFont,
  eSetLineDash = 0x70, /* 0x70   */
  eSetLineCap,
  eSetLineJoin,
  eSetMiterLimit,
  eSetPageDefaultCTM,
  eSetPageOrigin = 0x75,
  eSetPageRotation,
  eSetPageScale,
  eSetPaintTxMode,
  eSetPenSource,
  eSetPenWidth,
  eSetROP,
  eSetSourceTxMode,
  eSetCharBoldValue,
  eSetNeutralAxis,
  eSetClipMode,
  eSetPathToClip, /* 0x80 */
  eSetCharSubMode,
  eBeginUserDefinedLineCap,
  eEndUserDefinedLineCap,
  eCloseSubPath,
  eNewPath,
  ePaintPath,
  eBeginBackground,
  eEndBackground,
  eDrawBackground,
  eRemoveBackground,
  eBeginForm,
  eEndForm,
  eDrawForm,
  eRemoveForm,
  eRegisterFormAsPattern,
  eOperator_90,
  eArcPath,
  eSetColorTrapping,
  eBezierPath,
  eSetAdaptiveHalftoning,
  eBezierRelPath,
  eChord,
  eChordPath,
  eEllipse,
  eEllipsePath,
  eOperator_9a,
  eLinePath,
  eOperator_9c,
  eLineRelPath,
  ePie,
  ePiePath,
  eRectangle = 0xa0,  /* 0xa0 */
  eRectanglePath,
  eRoundRectangle,
  eRoundRectanglePath,
  eOperator_a4,
  eOperator_a5,
  eOperator_a6,
  eOperator_a7,
  eText,
  eTextPath,        /* 0xa8 */
  eSystemText,      /* 0xa9 */
  /* Reserved... */
  eBeginImage = 0xb0, /* 0xb0 */
  eReadImage,
  eEndImage,
  eBeginRastPattern,
  eReadRastPattern,
  eEndRastPattern,
  eBeginScan,
  /* ? */
  eEndScan = 0xb8,
  eScanLineRel,
  ePassThrough,
};

#define  eDefaultDataSource 0
#define  ByteAttribId       0xf8
#define  eEmbeddedData      0xfa
#define  eEmbeddedDataByte  0xfb

/* Enumerate Units Of Measure */
enum {
  eInch = 0,
  eMillimeter,
  eTenthsOfAMillimeter,
};

/* Enumerate Error Reporting : */
enum {
  eNoReporting = 0,
  eBackChannel,
  eErrorPage,
  eBackChAndErrPage,
  eBackChanAndErrPage,
  eNWBackChannel,
  eNWErrorPage,
  eNWBackChAndErrPage,
};

/* Enumerate Data Organization : */
enum {
  eBinaryHighByteFirst = 0,
  eBinaryLowByteFirst,
};

/* Enumerate Media Sources : */
enum {
  eDefaultSource = 0,
  eAutoSelect,
  eManualFeed,
  eMultiPurposeTray,
  eUpperCassette,
  eLowerCassette,
  eEnvelopeTray,
  eThirdCassette,
};


/* Enumerate Media Destinations : */
enum {
  eDefaultBin = 0,
  eFaceDownBin,
  eFaceUpBin,
  eJobOffsetBin,
};

/* Enumerate Orientations : */
enum {
  ePortraitOrientation = 0,
  eLandscapeOrientation,
  eReversePortrait,
  eReverseLandscape,
};

/* Enumerate Media Sizes : */
enum {
  eLetterPaper = 0,
  eLegalPaper,
  eA4Paper,
  eExecPaper,
  eLedgerPaper,
  eA3Paper,
  eCOM10Envelope,
  eMonarchEnvelope,
  eC5Envelope,
  eDLEnvelope,
  eJB4Paper,
  eJB5Paper,
  eB5Envelope,
  eB5Paper,
  eJPostcard,
  eJDoublePostcard,
  eA5Paper,
  eA6Paper,
  eJB6Paper,
  eJIS8KPaper,
  eJIS16KPaper,
  eJISExecPaper,
  eSlidePaper,
  eTabloid,
  eUnknownPaper
};

#define eSimplexFrontSide        0
/* Enumerate Duplex Page Modes : */
enum {
  eDuplexHorizontalBinding = 0,
  eDuplexVerticalBinding,
};

/* Enumerate Duplex Page Sides : */
enum {
  eFrontMediaSide = 0,
  eBackMediaSide,
};

/* Enumerate ColorSpaces */
enum {
  eBiLevel = 0,
  eGray,
  eRGB,
  eCMY,
  eCIELab,
  eCRGB,
  eSRGB,
};

/* Enumerate DitherMatrix : */
enum {
  eDeviceBest = 0 ,
  eDeviceIndependent,
};

/* HalftoneMethod Enumerations : */
enum {
  eHighLPI = 0,
  eMediumLPI,
  eLowLPI,
};

/* SetNeutralAxis Enumerations : */
enum {
  eTonerBlack = 0,
  eProcessBlack,
};

/* Enumerate Transparency : */
enum {
  eOpaque = 0,
  eTransparent,
};

/* Enumerate ColorDepth : */
enum {
  e1Bit = 0 ,
  e4Bit,
  e8Bit,
};

/* Enumerate ColorMapping : */
enum {
  eDirectPixel = 0,
  eIndexedPixel,
  eDirectPlane,
};

/* Macros for creating the PCL header */
#define PUTBYTE(_p_,_d_) *((_p_)++) = (_d_)
#define PUTLEWORD(_p_,_d_) \
{ *((_p_)++) = (uint8)((_d_) & 0xFF); \
  *((_p_)++) = (uint8)(((_d_) >> 8) & 0xFF); }
#define PUT4BYTE(_p_,_d_) \
{ *((_p_)++) = (uint8)((_d_) & 0xFF); \
  *((_p_)++) = (uint8)(((_d_) >> 8) & 0xFF); \
  *((_p_)++) = (uint8)(((_d_) >> 16) & 0xFF); \
  *((_p_)++) = (uint8)(((_d_) >> 24) & 0xFF); }

/*
 * \brief Media Size state.
 */
typedef struct _MEDIASIZEENTRY
{
  char    szName[32];
  float   nfWidth;   /* In inches */
  float   nfHeight;  /* In inches */
  uint32  nPclEnum;  /* Enumeration for this size */
} MEDIASIZEENTRY;

/**
 * \brief Find a \c MEDIASIZEENTRY for the active page image dimensions.
 *
 * \param[in] pRD
 * \param[out] pbIsLandscape  Whether landscape media is required.
 * \return a pointer to a \c MEDIASIZEENTRY object, or \c NULL if none found.
 */
MEDIASIZEENTRY* pclPerformMediaLookup (const RasterDescription* pRD,
                                       uint32* pbIsLandscape);

extern int32 pclCompressPackbits(uint8 *pDest, uint8 *pSrc, int32 dlen);

extern int32 pclCompressDeltaRow(uint8* pDest, uint8* pPrev, uint8* pNewRow,
                                 uint32 cbLineWidthInBytes, int32 fPCL6);

/**
 * \brief Examine a raster description to determine how many copies
 * of each page to output.
 *
 * \param pRD  The RasterDescription.
 * \return the copy count to use.
 */
int32 pclGetPageCopies (const RasterDescription* pRD);

/**
 * \brief Invert a sequence of bytes containing mono contone data.
 */
void invertMonoContoneData (uint8* pData, uint32 nBytes);

/**
 * \brief Determine the raster format from the RasterDescription.
 *
 * \param pRD
 * \return an enumeration describing the raster format.
 */
eRFLIST getRasterFormat (RasterDescription* pRD);

/**
 * \brief Write the job's PJL header.
 *
 * \param pHdr  The buffer to write the PJL in to. (Must be large enough.)
 * \param pRD
 * \param pMediaSize  May be NULL.
 * \param nPCLVersion  Must be 5 or 6.
 * \return a pointer to the end of the data written into the pHdr memory.
 */
char* writeJobPJL (char* pHdr, RasterDescription* pRD,
                   MEDIASIZEENTRY* pMediaSize, int32 nPCLVersion);

#endif /* !_PCLCOMMON_H_ */

