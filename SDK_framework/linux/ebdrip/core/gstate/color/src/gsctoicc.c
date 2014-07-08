/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gsctoicc.c(EBDSDK_P.1) $
 * $Id: color:src:gsctoicc.c,v 1.18.2.1.1.1 2013/12/19 11:24:53 anon Exp $
 *
 * Copyright (C) 2000-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Produces an ICC profile from a CIE PS color space.  Primarily
 * required for pdfout as PDF has does not include CIE spaces (only
 * CalGray, CalRGB, Lab and ICCBased).  Converting a CIEBasedA color
 * space to an RGB input profile should only be done when at least one
 * of the following is non-trivial: MatrixA, RangeLMN, DecodeLMN,
 * MatrixLMN. Otherwise a gray tone reproduction curve can be used
 * more efficiently instead (or CalGray)
 */

#define OBJECT_MACROS_ONLY

#include "core.h"

#include "calendar.h"       /* get_calendar_params */
#include "swerrors.h"       /* error_handler */

#include "gs_colorpriv.h" /* CLINK */
#include "gsccie.h"       /* cc_getCieBasedABCWhitePoint */
#include "gscheadpriv.h"  /* GS_CHAINinfo */

#include "gsctoicc.h"     /* externs */

/* ---------------------------------------------------------------------- */
#ifdef ASSERT_BUILD
#define DEBUG_STREAMPOS_INC(_pStream, _n) \
  (_pStream)->nStreamPosition += (_n)
#else
#define DEBUG_STREAMPOS_INC(_pStream, _n) EMPTY_STATEMENT()
#endif

/* ---------------------------------------------------------------------- */
/* ICC_WRITE[8|16|32|64]:
   Macros to help squirting bytes to the output stream.
   Note the macros return FALSE on hitting EOF.
 */
#define ICC_PUTC(_data, _pStream) \
  (*((_pStream)->iccPutc)) ((_data), ((_pStream)->iccState))

#define ICC_WRITE8(_pStream, _data)             \
MACRO_START                                     \
  uint8 _byte_ = _data;                         \
  if (! ICC_PUTC(_byte_, _pStream))             \
    return FALSE;                               \
  DEBUG_STREAMPOS_INC(_pStream, 1);             \
MACRO_END

#define ICC_WRITE16(_pStream, _data)            \
MACRO_START                                     \
  uint16 _data16_ = _data;                      \
  uint8 _byte0_, _byte1_;                       \
  _byte0_ = (uint8)((_data16_ >> 8)        );   \
  _byte1_ = (uint8)((_data16_     ) & 0xFFu);   \
  if (! ICC_PUTC(_byte0_, _pStream) ||          \
      ! ICC_PUTC(_byte1_, _pStream))            \
    return FALSE;                               \
  DEBUG_STREAMPOS_INC(_pStream, 2);             \
MACRO_END

#define ICC_WRITE32(_pStream, _data)                    \
MACRO_START                                             \
  uint32 _data32_ = _data;                              \
  uint8 _byte0_, _byte1_, _byte2_, _byte3_;             \
  _byte0_ = (uint8)(((_data32_ >> 24))        );        \
  _byte1_ = (uint8)(((_data32_ >> 16)) & 0xFFu);        \
  _byte2_ = (uint8)(((_data32_ >>  8)) & 0xFFu);        \
  _byte3_ = (uint8)(((_data32_      )) & 0xFFu);        \
  if (! ICC_PUTC(_byte0_, _pStream) ||                  \
      ! ICC_PUTC(_byte1_, _pStream) ||                  \
      ! ICC_PUTC(_byte2_, _pStream) ||                  \
      ! ICC_PUTC(_byte3_, _pStream))                    \
    return FALSE;                                       \
  DEBUG_STREAMPOS_INC(_pStream, 4);                     \
MACRO_END

#define ICC_WRITE64(_pStream, _data)            \
MACRO_START                                     \
  uint32 _data2x32_[2] = _data;                 \
  ICC_WRITE32(_data2x32_[0], pStream)           \
  ICC_WRITE32(_data2x32_[1], pStream)           \
MACRO_END

/* ---------------------------------------------------------------------- */
/* ICC_01_TO_U8:
   Converts a double in [0 1] range to an unsigned 8 number */
#define ICC_01_TO_U8(_d, _u8)                                                   \
MACRO_START                                                                     \
  double _value_ = (_d);                                                        \
  HQASSERT((_value_) >= 0.0 && (_value_) <= 1.0, "_value_ out of range");       \
  (_u8) = (uint8)((_value_) * 255.0);                                           \
MACRO_END

/* ICC_01_TO_U16:
   Converts a double in [0 1] range to an unsigned 16 number */
#define ICC_01_TO_U16(_d, _u16)                                                 \
MACRO_START                                                                     \
  double _value_ = (_d);                                                        \
  HQASSERT((_value_) >= 0.0 && (_value_) <= 1.0, "_value_ out of range");       \
  (_u16) = (uint16)((_value_) * 65535.0);                                       \
MACRO_END

/* ICC_S15FIXED16:
   Converts a double to a signed 15.16 number */
#define ICC_S15FIXED16(_d, _s15Fixed16)                         \
MACRO_START                                                     \
  double _value_ = (_d);                                        \
  int32 _ip_, _fp_;                                             \
  HQASSERT((_value_) >= 0, "s15.16 numbers represent "          \
           "tristimulas values which must not be negative");    \
  HQASSERT((_value_) >= -32768.0 &&                             \
           (_value_) <= (32767.0 + (65535.0 / 65536.0)),        \
           "Number not representable as a s15.16 number");      \
  (_value_) += 0.00000762939453125; /* (0.5 / 65536.0) */       \
  (_ip_) = (int32)(_value_);                                    \
  (_fp_) = (int32)(((_value_) - (double)(_ip_)) * 65536.0);     \
  (_s15Fixed16) = (((_ip_) << 16) | (_fp_));                    \
MACRO_END

/* ---------------------------------------------------------------------- */
/* Frequently occurring numbers */

#define icS15Fixed16_value0    (0x00000000ul)
#define icS15Fixed16_value1    (0x00010000ul)

#define nTagSize        (12u)
#define nTagCountSize    (4u)
#define nHeaderSize    (128u)
#define nXYZNumberSize  (20u)
#define nCopyrightSize  (35u)
#define nProfDescSize  (113u)

/* ---------------------------------------------------------------------- */

/* icc_Identity_Matrix:
   Only actually used for an XYZ input color space,
   otherwise mandated to be a 3x3 identity matrix
 */
static int32 icc_Identity_Matrix(iccStream_t* pStream)
{
  /* e00, e01, e02 */
  ICC_WRITE32(pStream, icS15Fixed16_value1);
  ICC_WRITE32(pStream, icS15Fixed16_value0);
  ICC_WRITE32(pStream, icS15Fixed16_value0);

  /* e10, e11, e12 */
  ICC_WRITE32(pStream, icS15Fixed16_value0);
  ICC_WRITE32(pStream, icS15Fixed16_value1);
  ICC_WRITE32(pStream, icS15Fixed16_value0);

  /* e20, e21, e22 */
  ICC_WRITE32(pStream, icS15Fixed16_value0);
  ICC_WRITE32(pStream, icS15Fixed16_value0);
  ICC_WRITE32(pStream, icS15Fixed16_value1);

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* icc_Identity_InOutTable:
   Produces a set [nInOutChannels many] of input or output independent
   lookup tables with an identity mapping. Tables of 8 or 16 bit depth.
   An 8 bit table is fixed at 256 sample, a 16 bit is variable and uses
   the minimum number of samples (2 samples)
 */
static int32 icc_Identity_InOutTable(iccStream_t* pStream,
                                     uint32       nInOutChannels,
                                     int32        f16Bit)
{
  HQASSERT(nInOutChannels >= 1, "nInOutChannels < 1");
  if (f16Bit) {
    /* 16 bit, variable sample size;
       always use the minimum (2) for identity mapping */
    do {
      ICC_WRITE16(pStream, (uint16)0x0000);
      ICC_WRITE16(pStream, (uint16)0xFFFF);
    } while ((--nInOutChannels) > 0);
  } else {
    /* 8 bit precision */
    /* 8 bit fixed at 256 samples */
    do {
      uint8 nSample = 0x00u;
      do {
        ICC_WRITE8(pStream, nSample);
      } while ((++nSample) > 0x00u);
    }
    while ((--nInOutChannels) > 0);
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static int32 icc_CLUTEntry(iccStream_t*    pStream,
                           CLINK*          pLink,
                           int32           f16Bit,
                           int32           nRepeat)
{
  static SYSTEMVALUE xyzRange[6] = {0.0, 2.0, 0.0, 2.0, 0.0, 2.0};

  SYSTEMVALUE xyz[3];

  /* A/BC to XYZ or DEF/G to ABC */
  if (! (*pLink->functions->invokeSingle)(pLink, pLink->pnext->iColorValues))
    return FALSE;

  if (pLink->iColorSpace == SPACE_CIEBasedDEF ||
      pLink->iColorSpace == SPACE_CIEBasedDEFG) {
    /* ABC to XYZ, when previous step was DEF/G to ABC.
       Note: overprintProcess and f100pcBlack are not required
       (cf cc_iterateChainSingle)
     */
    pLink = pLink->pnext;
    HQASSERT(pLink->iColorSpace == SPACE_CIEBasedABC,
             "A CIEBasedDEF/G color space should be followed by a CIEBasedABC");
    if (! (*pLink->functions->invokeSingle)(pLink, pLink->pnext->iColorValues))
      return FALSE;
  }
  xyz[0] = (SYSTEMVALUE) pLink->pnext->iColorValues[0];
  xyz[1] = (SYSTEMVALUE) pLink->pnext->iColorValues[1];
  xyz[2] = (SYSTEMVALUE) pLink->pnext->iColorValues[2];

  NARROW3(xyz, xyzRange);

  HQASSERT(xyzRange[0] == 0.0 && xyzRange[1] == 2.0 &&
           xyzRange[2] == 0.0 && xyzRange[3] == 2.0 &&
           xyzRange[4] == 0.0 && xyzRange[5] == 2.0,
           "xyzRange has changed so mapping to 01 range may be wrong");
  xyz[0] = (xyz[0] * 0.5);
  xyz[1] = (xyz[1] * 0.5);
  xyz[2] = (xyz[2] * 0.5);

  if (f16Bit) {
    icUInt16Number xp, yp, zp;

    ICC_01_TO_U16(xyz[0], xp);
    ICC_01_TO_U16(xyz[1], yp);
    ICC_01_TO_U16(xyz[2], zp);

    do {
      ICC_WRITE16(pStream, xp);
      ICC_WRITE16(pStream, yp);
      ICC_WRITE16(pStream, zp);
    } while ((--nRepeat) > 0);
  } else {
    icUInt8Number xp, yp, zp;

    ICC_01_TO_U8(xyz[0], xp);
    ICC_01_TO_U8(xyz[1], yp);
    ICC_01_TO_U8(xyz[2], zp);

    do {
      ICC_WRITE8(pStream, xp);
      ICC_WRITE8(pStream, yp);
      ICC_WRITE8(pStream, zp);
    } while ((--nRepeat) > 0);
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* icc_CLUT:
   Produces a n-dimensional LUT in 8 or 16 bit precision.
   Each input component has nGridPoints.
   PS CIE color spaces use absolute colorimetry and ICC profiles
   use relative colorimetry. However we treat the whitepoint as
   the illuminant so in practice they are equivalent.
 */
static int32 icc_CLUT(iccStream_t* pStream,
                      CLINK*       pLink,
                      SYSTEMVALUE* pRange,
                      uint8        nGridPoints,
                      int32        f16Bit)
{
  switch (pLink->iColorSpace) {
    case SPACE_CIEBasedABC:
    case SPACE_CIEBasedDEF: {
      SYSTEMVALUE inc1, inc2, inc3;
      SYSTEMVALUE cv1, cv2, cv3;

      inc1 = (pRange[1] - pRange[0]) / (nGridPoints - 1);
      inc2 = (pRange[3] - pRange[2]) / (nGridPoints - 1);
      inc3 = (pRange[5] - pRange[4]) / (nGridPoints - 1);

      for (cv1 = pRange[0]; cv1 <= pRange[1]; cv1 += inc1) {
        pLink->iColorValues[0] = (USERVALUE)cv1;

        for (cv2 = pRange[2]; cv2 <= pRange[3]; cv2 += inc2) {
          pLink->iColorValues[1] = (USERVALUE)cv2;

          for (cv3 = pRange[4]; cv3 <= pRange[5]; cv3 += inc3) {
            pLink->iColorValues[2] = (USERVALUE)cv3;

            if (! icc_CLUTEntry(pStream, pLink, f16Bit, 1 /* nRepeat */))
              return FALSE;
          }
        }
      }
    }
    break;

    case SPACE_CIEBasedA: {
      SYSTEMVALUE inc1;
      SYSTEMVALUE cv1;
      int32 nRepeat;

      /* Converting a CIEBasedA color space to an RGB input profile should
         only be done when at least one of the following is non-trivial:
         MatrixA, RangeLMN, DecodeLMN, MatrixLMN. Otherwise a gray tone
         reproduction curve can be used more efficiently instead (or CalGray) */

      inc1 = (pRange[1] - pRange[0]) / (nGridPoints - 1);

      nRepeat = nGridPoints * nGridPoints;

      pLink->iColorValues[1] = 0.0;
      pLink->iColorValues[2] = 0.0;

      for (cv1 = pRange[0]; cv1 <= pRange[1]; cv1 += inc1) {
        pLink->iColorValues[0] = (USERVALUE)cv1;

        if (! icc_CLUTEntry(pStream, pLink, f16Bit, nRepeat))
          return FALSE;
      }
    }
    break;

    case SPACE_CIEBasedDEFG: {
      SYSTEMVALUE inc1, inc2, inc3, inc4;
      SYSTEMVALUE cv1, cv2, cv3, cv4;

      inc1 = (pRange[1] - pRange[0]) / (nGridPoints - 1);
      inc2 = (pRange[3] - pRange[2]) / (nGridPoints - 1);
      inc3 = (pRange[5] - pRange[4]) / (nGridPoints - 1);
      inc4 = (pRange[7] - pRange[6]) / (nGridPoints - 1);

      for (cv1 = pRange[0]; cv1 <= pRange[1]; cv1 += inc1) {
        pLink->iColorValues[0] = (USERVALUE)cv1;

        for (cv2 = pRange[2]; cv2 <= pRange[3]; cv2 += inc2) {
          pLink->iColorValues[1] = (USERVALUE)cv2;

          for (cv3 = pRange[4]; cv3 <= pRange[5]; cv3 += inc3) {
            pLink->iColorValues[2] = (USERVALUE)cv3;

            for (cv4 = pRange[6]; cv4 <= pRange[7]; cv4 += inc4) {
              pLink->iColorValues[3] = (USERVALUE)cv4;

              if (! icc_CLUTEntry(pStream, pLink, f16Bit, 1 /* nRepeat */))
                return FALSE;
            }
          }
        }
      }
    }
    break;

    default:
      HQFAIL("Can only convert an CIEBasedA/ABC/DEF/DEFG "
             "color space to ICC profile");
      return error_handler(UNDEFINED);
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static uint32 icc_lutXType_size(int32 nInputChannels,
                                int32 nOutputChannels,
                                uint8 nGridPoints,
                                int32 f16Bit)
{
  uint32 nLutXTypeSize;

  if (f16Bit) {
    nLutXTypeSize = nInputChannels * 2; /* for identity input table */
    nLutXTypeSize += (int32) pow(nGridPoints, nInputChannels) * nOutputChannels;
    nLutXTypeSize += (nOutputChannels * 2); /* for identity output table */
    nLutXTypeSize = nLutXTypeSize << 1; /* tables are 16 bit */
    nLutXTypeSize += 52; /* standard entries */
  } else {
    nLutXTypeSize = (nInputChannels * 256); /* for identity input table */
    nLutXTypeSize += (int32) pow(nGridPoints, nInputChannels) * nOutputChannels;
    nLutXTypeSize += (nOutputChannels * 256); /* for identity output table */
    nLutXTypeSize += 48;
  }

  return nLutXTypeSize;
}

/* ---------------------------------------------------------------------- */
/* icc_lutXType:
   Produces a lut16Type or a lut8Type depending on f16Bit
 */
static int32 icc_lutXType(iccStream_t* pStream,
                          CLINK*       pLink,
                          int32        nInputChannels,
                          SYSTEMVALUE* pRange,
                          uint8        nGridPoints,
                          int32        f16Bit)
{
#if defined( ASSERT_BUILD )
  uint32 nStartPos = pStream->nStreamPosition;
#endif

  if (f16Bit)
    ICC_WRITE32(pStream, icSigLut16Type); /* 'mft2' multi-function table */
  else
    ICC_WRITE32(pStream, icSigLut8Type);  /* 'mft1' multi-function table */

  ICC_WRITE32(pStream, 0); /* four bytes reserved */

  ICC_WRITE8(pStream, (uint8) nInputChannels); /* number of input channels */

  ICC_WRITE8(pStream, 3); /* number of output channels (always XYZ) */

  ICC_WRITE8(pStream, nGridPoints); /* number of grid points per channel */

  ICC_WRITE8(pStream, 0); /* one byte reserved */

  if (! icc_Identity_Matrix(pStream))
    return FALSE;

  /* both tables are fixed at 256 for lut8Type */
  if (f16Bit) {
    /* lut16Type allows variable number of entries,
       always use the minimum for identity (2 entries) */
    ICC_WRITE16(pStream, 2); /* input table number of entries */
    ICC_WRITE16(pStream, 2); /* output table number of entries */
  }

  if (! icc_Identity_InOutTable(pStream, nInputChannels, f16Bit))
    return FALSE;

  if (! icc_CLUT(pStream, pLink, pRange, nGridPoints, f16Bit))
    return FALSE;

  if (! icc_Identity_InOutTable(pStream, 3, f16Bit))
    return FALSE;

  HQASSERT((nStartPos + icc_lutXType_size(nInputChannels, 3,
                                          nGridPoints, f16Bit))
           == pStream->nStreamPosition,
           "icc_lutXType_size does not match actual bytes written");
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static int32 icc_CreationDate(iccStream_t* pStream)
{
  int32 nYear, nMonth, nDay, nHour, nMinute, nSecond;
  int32 fRunning;

  if (! get_calendar_params(& nYear, & nMonth, & nDay,
                            & nHour, & nMinute, & nSecond,
                            & fRunning))
    return FALSE;

  ICC_WRITE16(pStream, (uint16)nYear);
  ICC_WRITE16(pStream, (uint16)nMonth);
  ICC_WRITE16(pStream, (uint16)nDay);
  ICC_WRITE16(pStream, (uint16)nHour);
  ICC_WRITE16(pStream, (uint16)nMinute);
  ICC_WRITE16(pStream, (uint16)nSecond);

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static int32 icc_XYZNumber(iccStream_t*   pStream,
                           SYSTEMVALUE    xyz[3])
{
  icS15Fixed16Number fx, fy, fz;

  ICC_S15FIXED16(xyz[0], fx);
  ICC_S15FIXED16(xyz[1], fy);
  ICC_S15FIXED16(xyz[2], fz);

  ICC_WRITE32(pStream, fx);
  ICC_WRITE32(pStream, fy);
  ICC_WRITE32(pStream, fz);

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static int32 icc_Header(iccStream_t*    pStream,
                        int32           nInputChannels,
                        int32           nProfileSize,
                        SYSTEMVALUE*    pWhitePoint)
{
#if defined( ASSERT_BUILD )
  uint32 nStartPos = pStream->nStreamPosition;
#endif

  int32 nReserved;

  /* Profile Size */
  ICC_WRITE32(pStream, nProfileSize);

  /* CMM Type Signature */
  ICC_WRITE32(pStream, 0x0L); /* no preferred CMM */

  /* Profile Version Number */
  ICC_WRITE32(pStream, icVersionNumber);

  /* Profile/Device Class Signature */
  ICC_WRITE32(pStream, icSigInputClass); /* 'scnr' input device profile */

  /* ColorSpace of Data */
  if (nInputChannels == 3)
    ICC_WRITE32(pStream, icSigRgbData);
  else {
    HQASSERT(nInputChannels == 4, "nInputChannels must be 3 or 4");
    ICC_WRITE32(pStream, icSigCmykData);
  }

  /* Profile Connection Space (PCS) */
  ICC_WRITE32(pStream, icSigXYZData);

  /* Date and Time of Creation */
  if (!icc_CreationDate(pStream))
    return FALSE;

  /* Profile File Signature */
  ICC_WRITE32(pStream, icMagicNumber);

  /* Primary Platform Signature */
  ICC_WRITE32(pStream, 0x0L); /* no primary platform */

  /* Various CMM Flags */
  ICC_WRITE32(pStream, 0x0L);

  /* Device Manufacturer */
  ICC_WRITE32(pStream, 0x6E6F6E65L); /* 'none' (cf. Distiller) */

  /* Device Model */
  ICC_WRITE32(pStream, 0x0L);

  /* Device Attributes */
  ICC_WRITE32(pStream, 0x0L);
  ICC_WRITE32(pStream, 0x0L);

  /* Rendering Intent */
  ICC_WRITE16(pStream, icRelativeColorimetric); /* (cf. Distiller, icPerceptual) */
  ICC_WRITE16(pStream, 0x0); /* reserved */

  /* XYZ values of Illuminant of the PCS.
     The ICC profile spec states that this should be the D50 illuminant,
     but that would mean adapting say a D65 referenced space to D50. Instead
     we rely on the profile reader to do the correct thing and not assume a
     D50 illuminant (The HQN icc importer deals with this)
   */
  if (! icc_XYZNumber(pStream, pWhitePoint))
    return FALSE;

  /* Profile Creator Signature */
  ICC_WRITE32(pStream, 0x4841524CL); /* 'HARL' */

  /* 44 bytes reserved for future expansion */
  nReserved = 44;
  while (nReserved > 0) {
    ICC_WRITE32(pStream, 0x0L);
    nReserved -= 4;
  }

  HQASSERT(pStream->nStreamPosition - nStartPos == nHeaderSize,
           "Header is not nHeaderSize");
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static int32 icc_Copyright(iccStream_t* pStream)
{
#if defined( ASSERT_BUILD )
  uint32 nStartPos = pStream->nStreamPosition;
#endif

  static char strCopyright[] = "(c) 2000 Harlequin Limited";
#define nStrCopyright (27)
  int32 i;

  HQASSERT(nStrCopyright == (sizeof(strCopyright)/sizeof(*strCopyright)),
           "nStrCopyright does not match the actual length of strCopyright");

  ICC_WRITE32(pStream, icSigTextType);
  ICC_WRITE32(pStream, 0); /* reserved */

  for (i = 0; i < nStrCopyright; ++i)
    ICC_WRITE8(pStream, (uint8) strCopyright[i]);

  HQASSERT(pStream->nStreamPosition - nStartPos == nCopyrightSize,
           "nCopyrightSize differs from bytes actually written");
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static int32 icc_ProfileDescription(iccStream_t* pStream)
{
#if defined( ASSERT_BUILD )
  uint32 nStartPos = pStream->nStreamPosition;
#endif

  static char strProfDesc[] = "PostScript CSA Profile";
#define nStrProfDesc (23)
  int32 i;

  HQASSERT(nStrProfDesc == (sizeof(strProfDesc)/sizeof(*strProfDesc)),
           "nStrProfDesc does not match the actual length of strProfDesc");

  ICC_WRITE32(pStream, icSigTextDescriptionType);
  ICC_WRITE32(pStream, 0); /* reserved */
  ICC_WRITE32(pStream, nStrProfDesc);

  for (i = 0; i < nStrProfDesc; ++i)
    ICC_WRITE8(pStream, (uint8) strProfDesc[i]);

  ICC_WRITE32(pStream, 0); /* Unicode code */
  ICC_WRITE32(pStream, 0); /* Unicode count */

  ICC_WRITE16(pStream, 0); /* Scriptcode code */
  ICC_WRITE8(pStream, 0);  /* Scriptcode count */

  for (i = 0; i < 67; ++i)
    ICC_WRITE8(pStream, 0u);

  HQASSERT(pStream->nStreamPosition - nStartPos == nProfDescSize,
           "nProfDescSize differs from bytes actually written");
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static int32 icc_GetDetails(CLINK*      pLink,
                            int32*      pnInputChannels,
                            SYSTEMVALUE pWhitePoint[3],
                            SYSTEMVALUE pBlackPoint[3],
                            SYSTEMVALUE pRange[8])
{
  switch (pLink->iColorSpace) {
    case SPACE_CIEBasedABC:
      cc_getCieBasedABCRange(pLink->p.ciebasedabc, 2, pRange+4);
      cc_getCieBasedABCRange(pLink->p.ciebasedabc, 1, pRange+2);
      /* FALLTHRU */
    case SPACE_CIEBasedA  :
      cc_getCieBasedABCRange(pLink->p.ciebaseda, 0, pRange);
      cc_getCieBasedABCWhitePoint(pLink, pWhitePoint);
      cc_getCieBasedABCBlackPoint(pLink, pBlackPoint);
      cc_getCieBasedABCWhitePoint(pLink, pWhitePoint);
      cc_getCieBasedABCBlackPoint(pLink, pBlackPoint);
      *pnInputChannels = 3; /* CIEBasedA converted to ABC profile */
      break;
    case SPACE_CIEBasedDEF:
      HQASSERT(pLink->pnext->iColorSpace == SPACE_CIEBasedABC ||
               pLink->pnext->oColorSpace == SPACE_CIEXYZ,
               "pLink->pnext must be an ABC to XYZ link");
      cc_getCieBasedDEFRange(pLink->p.ciebaseddef, 2, pRange+4);
      cc_getCieBasedDEFRange(pLink->p.ciebaseddef, 1, pRange+2);
      cc_getCieBasedDEFRange(pLink->p.ciebaseddef, 0, pRange);
      cc_getCieBasedABCWhitePoint(pLink->pnext, pWhitePoint);
      cc_getCieBasedABCBlackPoint(pLink->pnext, pBlackPoint);
      cc_getCieBasedABCWhitePoint(pLink->pnext, pWhitePoint);
      cc_getCieBasedABCBlackPoint(pLink->pnext, pBlackPoint);
      *pnInputChannels = 3;
      break;
    case SPACE_CIEBasedDEFG:
      HQASSERT(pLink->pnext->iColorSpace == SPACE_CIEBasedABC ||
               pLink->pnext->oColorSpace == SPACE_CIEXYZ,
               "pLink->pnext must be an ABC to XYZ link");
      cc_getCieBasedDEFGRange(pLink->p.ciebaseddefg, 3, pRange+6);
      cc_getCieBasedDEFGRange(pLink->p.ciebaseddefg, 2, pRange+4);
      cc_getCieBasedDEFGRange(pLink->p.ciebaseddefg, 1, pRange+2);
      cc_getCieBasedDEFGRange(pLink->p.ciebaseddefg, 0, pRange);
      cc_getCieBasedABCWhitePoint(pLink->pnext, pWhitePoint);
      cc_getCieBasedABCBlackPoint(pLink->pnext, pBlackPoint);
      cc_getCieBasedABCWhitePoint(pLink->pnext, pWhitePoint);
      cc_getCieBasedABCBlackPoint(pLink->pnext, pBlackPoint);
      *pnInputChannels = 4;
      break;
    default:
      HQFAIL("Can only convert an CIEBasedA/ABC/DEF/DEFG "
             "color space to ICC profile");
      return error_handler(UNDEFINED);
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* gsc_CIEToICC:
   Creates an ICC input profile from a CIE A or ABC color space.
   f16Bit indicates whether 8 or 16 bit precision is required.
   Converting a CIEBasedA color space to an RGB input profile
   should only be done when at least one of the following is
   non-trivial: MatrixA, RangeLMN, DecodeLMN, MatrixLMN.
   Otherwise a gray tone reproduction curve can be used more
   efficiently instead (or CalGray)
 */
int32 gsc_CIEToICC(GS_COLORinfo *colorInfo,
                   int32        colorType,
                   int32        f16Bit,
                   iccStream_t* pStream)
{
  CLINK* pLink;

  int32 nInputChannels = 0;
  SYSTEMVALUE pWhitePoint[3], pBlackPoint[3], pRange[8];
  int32 fBlackPoint;

  uint32 nTags;
  uint32 nByteOffset;

  uint32 nProfileSize;
  uint32 nTagTableSize;
  uint32 nAToB0Size;

  HQASSERT(colorInfo != NULL, "colorInfo is null");
  HQASSERT(colorInfo->chainInfo[colorType] != NULL,
           "color chain must already be setup");
  HQASSERT(colorType >= 0 && colorType < GSC_N_COLOR_TYPES,
           "colorType out of range");
  HQASSERT(f16Bit == TRUE || f16Bit == FALSE, "f16Bit must be boolean");
  HQASSERT(pStream != NULL, "pStream is null");

  if (gsc_constructChain(colorInfo, colorType))
    return FALSE;

  pLink = colorInfo->chainInfo[colorType]->context->pnext;
  HQASSERT(pLink != NULL, "pLink is null");

  /* Find the CIE color space on the color chain */
  while (pLink->iColorSpace != SPACE_CIEBasedA &&
         pLink->iColorSpace != SPACE_CIEBasedABC &&
         pLink->iColorSpace != SPACE_CIEBasedDEF &&
         pLink->iColorSpace != SPACE_CIEBasedDEFG) {
    HQASSERT(pLink->iColorSpace == SPACE_Separation ||
             pLink->iColorSpace == SPACE_DeviceN ||
             pLink->iColorSpace == SPACE_Indexed,
             "Should only be skipping Separation, DeviceN or Indexed to find CIE space");
    pLink = pLink->pnext;
  }

  HQASSERT(pLink != NULL &&
           (((pLink->iColorSpace == SPACE_CIEBasedA ||
              pLink->iColorSpace == SPACE_CIEBasedABC) &&
             pLink->oColorSpace == SPACE_CIEXYZ) ||
            ((pLink->iColorSpace == SPACE_CIEBasedDEF ||
              pLink->iColorSpace == SPACE_CIEBasedDEFG) &&
             pLink->oColorSpace == SPACE_CIEBasedABC &&
             pLink->pnext->iColorSpace == SPACE_CIEBasedABC &&
             pLink->pnext->oColorSpace == SPACE_CIEXYZ)),
           "Expected a clink from A/ABC to XYZ or DEF/G to ABC to XYZ");

  if (pLink == NULL)
    return error_handler(UNDEFINED);

#ifdef ASSERT_BUILD
  pStream->nStreamPosition = 0;
#endif

  /* Get WhitePoint, BlackPoint and Range */
  if (! icc_GetDetails(pLink, & nInputChannels,
                       pWhitePoint, pBlackPoint, pRange))
    return FALSE;

  fBlackPoint = (pBlackPoint[0] != 0.0 ||
                 pBlackPoint[1] != 0.0 ||
                 pBlackPoint[2] != 0.0);
  /* only do a black point if it is not the default */

  /* Profile Header */
  /* ------------------------------------------------------------- */

  nTags          = (fBlackPoint ? 5u : 4u);
  nTagTableSize  = nTagCountSize + (nTags * nTagSize);
  nAToB0Size     = icc_lutXType_size(nInputChannels, 3, 9u, f16Bit);
  nProfileSize   = (  nHeaderSize
                      + nTagTableSize
                      + nCopyrightSize
                      + nProfDescSize
                      + nXYZNumberSize  /* media whitepoint */
                      + (fBlackPoint ? nXYZNumberSize : 0u)
                      + nAToB0Size);

  if (! icc_Header(pStream, nInputChannels, nProfileSize, pWhitePoint))
    return FALSE;
  HQASSERT(pStream->nStreamPosition == nHeaderSize,
           "ICC Header should be nHeaderSize bytes");

  /* Tag Table */
  /* ------------------------------------------------------------- */

  /* Calculate next byte offset into the tag data */
  nByteOffset = 128u + nTagTableSize;

  /* Tag Count */
  ICC_WRITE32(pStream, nTags);

  /* Copyright Tag */
  ICC_WRITE32(pStream, icSigCopyrightTag);
  ICC_WRITE32(pStream, nByteOffset);
  ICC_WRITE32(pStream, nCopyrightSize);
  nByteOffset += nCopyrightSize;

  /* Profile Description Tag */
  ICC_WRITE32(pStream, icSigProfileDescriptionTag);
  ICC_WRITE32(pStream, nByteOffset);
  ICC_WRITE32(pStream, nProfDescSize);
  nByteOffset += nProfDescSize;

  /* Media WhitePoint Tag */
  ICC_WRITE32(pStream, icSigMediaWhitePointTag);
  ICC_WRITE32(pStream, nByteOffset);
  ICC_WRITE32(pStream, nXYZNumberSize);
  nByteOffset += nXYZNumberSize;

  if (fBlackPoint) {
    /* Media BlackPoint Tag (optional) */
    ICC_WRITE32(pStream, icSigMediaBlackPointTag);
    ICC_WRITE32(pStream, nByteOffset);
    ICC_WRITE32(pStream, nXYZNumberSize);
    nByteOffset += nXYZNumberSize;
  }

  /* AtoB0 Tag */
  ICC_WRITE32(pStream, icSigAToB0Tag);
  ICC_WRITE32(pStream, nByteOffset);
  ICC_WRITE32(pStream, nAToB0Size);
  nByteOffset += nAToB0Size;

  /* Tag Data */
  /* ------------------------------------------------------------- */

  /* Copyright Tag */
  if (! icc_Copyright(pStream))
    return FALSE;

  /* Profile Description Tag */
  if (! icc_ProfileDescription(pStream))
    return FALSE;

  /* Media WhitePoint Tag */
  ICC_WRITE32(pStream, icSigXYZType);
  ICC_WRITE32(pStream, 0u);
  if (! icc_XYZNumber(pStream, pWhitePoint))
    return FALSE;

  if (fBlackPoint) {
    /* Media BlackPoint Tag (optional) */
    ICC_WRITE32(pStream, icSigXYZType);
    ICC_WRITE32(pStream, 0u);
    if (! icc_XYZNumber(pStream, pBlackPoint))
      return FALSE;
  }

  /* AToB0 Tag */
  if (! icc_lutXType(pStream, pLink, nInputChannels,
                     pRange, 9 /* don't ask */, f16Bit))
    return FALSE;

  HQASSERT(nProfileSize == pStream->nStreamPosition &&
           nByteOffset == pStream->nStreamPosition,
           "ICC profile size or profile is wrong");
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* Log stripped */
/* eof gsctoicc.c */
