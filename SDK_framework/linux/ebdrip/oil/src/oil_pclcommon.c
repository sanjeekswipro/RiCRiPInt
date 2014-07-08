/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_pclcommon.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup OIL
 *
 * \brief Version agnostic PCL functions.
 */
#include "oil_pclcommon.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>


/*
 * The following codes implement the compression.
 */

static uint8 different_data[128];
static uint8 different_count;
static uint8 same_byte;
static uint8 same_count;
static __inline uint8 *compressFlushSame(uint8 *pDest);
static __inline uint8 *compressFlushDifferent(uint8 *pDest);
static __inline uint8 *compressAddDifferent(uint8 *pDest, uint8 dif_char);
static __inline uint8 *compressAddSame(uint8 *pDest, uint8 dup_char);

static __inline uint8 *compressFlushSame(uint8 *pDest)
{
  *(pDest++) = (uint8)(256 - (same_count - 1));
  *(pDest++) = same_byte;
  same_count = 0;
  return(pDest);
}

static __inline uint8 *compressFlushDifferent(uint8 *pDest)
{
  uint8 *pTmp = (uint8 *)&different_data;

  *(pDest++) = (uint8)(different_count - 1);
  while (different_count--)
    *(pDest++) = *(pTmp++);
  different_count = 0;

  return(pDest);
}

static __inline uint8 *compressAddDifferent(uint8 *pDest, uint8 dif_char)
{
  if (same_count)
    pDest = compressFlushSame(pDest);

  if (different_count > 1) {
    if (different_data[different_count-1] == dif_char) {
      if (different_data[different_count-2] == dif_char) {
        different_count-=2;
        same_byte = dif_char;
        pDest = compressAddSame(pDest, dif_char);
        pDest = compressAddSame(pDest, dif_char);
        pDest = compressAddSame(pDest, dif_char);
        return(pDest);
      }
    }
  }

  different_data[different_count++] = dif_char;
  if (128 == different_count)
    pDest = compressFlushDifferent(pDest);

  return(pDest);
}

static __inline uint8 *compressAddSame(uint8 *pDest, uint8 dup_char)
{
  if (different_count)
    pDest = compressFlushDifferent(pDest);

  if (dup_char != same_byte) {
    pDest = compressAddDifferent(pDest, dup_char);
    return(pDest);
  }

  same_count++;
  if (128 == same_count)
    pDest = compressFlushSame(pDest);

  return(pDest);
}

/**
 * \brief TIFF Packbits Compression code.
 * In PCL5, compression is indicated with the Esc*b\#m sequence,
 * where \# is one of these ASCII values:
 * 0 - Unencoded
 * 1 - Run-length encoding
 * 2 - Tagged Imaged File Format (TIFF) rev. 4.0
 * 3 - Delta row compression
 * 4 - Reserved
 * 5 - Adaptive compression
 *
 * In PCL6, where the data is passed in blocks of more than 1 line,
 * the whole line must be compressed, unless 'Adaptive' mode is
 * selected, when the compressed line is preceded with LINE_TIFF
 * and the 2-byte compressed length value.
 *
 * \param pDest pointer to the compressed buffer.
 * \param pSrc pointer to the source stream.
 * \param dlen length of the source stream.
 * \return length of the compressed stream.
 */
int32 pclCompressPackbits(uint8 *pDest, uint8 *pSrc, int32 dlen)
{
  uint8 *pOrgDest = pDest;
  different_count = 0;
  same_count = 0;
  same_byte = 0;

  for (; dlen; dlen--, pSrc++) {
    if (same_count)
      pDest = compressAddSame(pDest, *pSrc);
    else
      pDest = compressAddDifferent(pDest, *pSrc);
  }
  if (different_count)
    pDest = compressFlushDifferent(pDest);
  if (same_count)
    pDest = compressFlushSame(pDest);

  return((int32)(pDest - pOrgDest));
}



/**
 * \brief Delta Row compression for PCL5 and PCL6 (PCL XL extensions)
 *
 * Delta row:
 *   The three higher-order bits indicate the number of consecutive bytes to
 *   replace (000=1 to 111=8). The five lower-order bits contain the offset
 *   relative to the current byte of the byte to be replaced. The values of the
 *   offset have the following definitions:
 *
 *   Control Bits (3 bits)
 *     0-7 -  Number of consecutive bytes to replace (1 to 8).
 *
 *   Offset Bits (5 bits)
 *     0-30 - Relative offset of 0-30 bytes from the first untreated byte (either
 *            the first byte in a row, or the first byte following the most recent
 *            replacement byte). The first offset in a raster row is relative to the left
 *            graphics margin. One to eight replacement bytes follow this command byte.
 *            For example, assume that the current byte is the first byte in the row. If the
 *            offset is 7, bytes 0 through 6 are unchanged; and if there are five
 *            replacement bytes, bytes 7 through 11 are replaced. The new current byte
 *            is 12. A second offset of 3 means that bytes 12, 13, and 14 are unchanged and
 *            byte 15 is the next to be replaced.
 *
 *     31 -   Indicates that an additional offset byte follows the command byte. The
 *            value of the offset byte is added to the command byte offset (31) to get the
 *            actual offset. If the offset byte is 0, the offset is 31. If the offset byte
 *            value is 255, yet another offset byte follows. The last offset byte is
 *            indicated by a value less than 255. All the offset bytes are added to the
 *            offset in the command byte to get the actual offset value. For example, if
 *            there are two offset bytes and the last contains 175, the total offset would
 *            be 31+255+175=461.
 *
 *   The PCL XL implementation follows the PCL5 implementation except in the following:
 *
 *     1) the seed row is initialized to zeros and contains the number of bytes defined
 *        by SourceWidth in the BeginImage operator.
 *     2) the delta row is preceded by a 2-byte byte count which indicates the number
 *        of bytes to follow for the delta row. The byte count is expected to be in LSB MSB
 *        order.
 *     3) to repeat the last row, use the 2-byte byte count of 00 00.
 *
 * \param pDest pointer to the compressed buffer.
 * \param pPrev pointer to the seed row.
 * \param pNewRow pointer to the source row to be compressed.
 * \param cbLineWidthInBytes line width in bytes.
 * \param fPCL6 compressing for PCL6 (PCL XL extension).
 * \return length of the compressed stream in bytes.
 *
 */
int32 pclCompressDeltaRow(uint8* pDest, uint8* pPrev, uint8* pNewRow,
                          uint32 cbLineWidthInBytes, int32 fPCL6)
{
  uint8* ptrDest;
  uint8* ptrSrc;
  uint8* ptrSeed;
  uint8* ptrSrcEnd;
  uint8* ptrSrcLastPos;
  #ifndef _ARM9_
  int32* plSrc;
  int32* plSeed;
  int32* plSrcEnd;
  #endif
  int32  k;
  int32  nDifferingBytes;
  uint32 uOffset;
  uint32 nLen;

  ptrSrcEnd = pNewRow + cbLineWidthInBytes;
  #ifndef _ARM9_
  plSrcEnd = (int32*)ptrSrcEnd;
  #endif  

  ptrDest = pDest;
  /* PCLXL extension 2) skip the byte count that we will patch later */
  if (fPCL6)
    ptrDest += 2;

  for (ptrSrc = pNewRow, ptrSeed = pPrev, ptrSrcLastPos = pNewRow;
       ptrSrc < ptrSrcEnd;
       ++ ptrSrc, ++ ptrSeed)
  {
    #ifndef _ARM9_
    /* Looking for next diff dword, also breaking out if we reach end of the source data. 
       Cant do it on arm9 because it doesnt allow comparison of words on non-word-aligned boundaries */
    for (plSrc = (int32*) ptrSrc, plSeed = (int32*) ptrSeed;
         *plSrc == *plSeed && plSrc < plSrcEnd;
         ++ plSrc, ++plSeed)
;

    /* Looking for the different byte within int32, also breaking out if we reach end of the data. */
    for (ptrSrc = (uint8*) plSrc, ptrSeed = (uint8*) plSeed;
         *ptrSrc == *ptrSeed && ptrSrc < ptrSrcEnd;
         ++ptrSrc, ++ptrSeed)
;
    #else
    /* Looking for the different byte, also breaking out if we reach end of the data. */
    for (; *ptrSrc == *ptrSeed && ptrSrc < ptrSrcEnd;
         ++ptrSrc, ++ptrSeed)
;
    #endif

    /* If there is nothing else different on the rest of the line, then
       there is nothing left to encode. */
    if (ptrSrc >= ptrSrcEnd)
      break;

    /* How far into the seed/source is the first different byte since the
       beginning of the data or since the last identical byte. */
    uOffset = CAST_PTRDIFFT_TO_UINT32 (ptrSrc - ptrSrcLastPos);

    /* Remember where the first different byte is. */
    ptrSrcLastPos = ptrSrc;

    /* Looking for next same byte, also breaking out if we the end of the data. */
    while ((++ptrSrc < ptrSrcEnd) && (*ptrSrc != *++ptrSeed))
;

    /* Number of different consecutive bytes minus one...
       0 - 7 in the control bits means 1-8 bytes to follow.
       We take off the one here to make the rest of the code a bit faster.
       The following code can cope with nDifferingBytes > 7.*/
    nDifferingBytes = CAST_PTRDIFFT_TO_INT32 (ptrSrc - ptrSrcLastPos) - 1;

    /* We now have two values, uOffset and nDifferingBytes.

       uOffset, the number of bytes that are the same since the beginning
                of the data or since the last different byte
       nDifferingBytes, the number of bytes (minus one) that are not the
                same since the last different byte or since the beginning
                of the data.

       The following examples of offset all assume the number of differing
       bytes is 1... thus the top 3 bits are zero. Also, when number of
       differing bytes is 1, the raw data is only one byte long...
         uOffset = 0 becomes 0x00 raw data
         uOffset = 1 becomes 0x01 raw data
         uOffset = 30 becomes x1E raw data
         uOffset = 31 becomes x1F x00 raw data
         uOffset = 32 becomes x1F x01 raw data
         uOffset = 255 becomes x1F xE0 raw data
         uOffset = 2000 becomes x1f xff xff xff xff xff xff xff xb8 rawdata
                                 31 255 255 255 255 255 255 255 184 = 2000

       If differing bytes is more than one but less than eight, then
       the number of differing byte minus one, shifted five bits left (into
       top three bits) should be OR's (bitwise) with the first byte in the
       examples above.

       If differing bytes is more than eight, then simply loop around,
       processing eight bytes at a time, until the number of differing bytes
       is less than 8 (Only eight bytes can be patched onto the seed row
       during one run).
    */

    /* If the number of differing bytes is eight or more, then
       loop until less than 8, encoding 8 source bytes at a time. */
    while (nDifferingBytes>7)
    {
      /* Eight bytes of raw data are next.
         Do not increment the ptrDest just yet, we may overwrite it if
         the offset is greater than 30. This saves a jump when using
         an else statement.
      */
      *ptrDest = (uint8)(0xe0 | uOffset);
      if (uOffset>30)
      {
        /* top three bits mean 8 bytes of raw data following,
           bottom five bits mean offset is 31 + the next byte. */
        *ptrDest++ = 0xff;

        /* 31 Bytes already counted in nOffset above (minus 255 to allow compare with zero). */
        for (k = uOffset - 31 - 255; k >= 0; k -= 255)
        {
          *ptrDest++ = 0xff; /* Another 255 bytes */
        }
        /* Plus the remainder bytes, which can be zero.
           Also add the 255 that we took off in the for loop.
           Do not increment ptrDest here, leave it to the common
           part below, then offset can be any value. */
        *ptrDest = (uint8)(k + 255);
      }
      ++ptrDest;

      #ifndef _ARM9_
      /* Copy eight bytes. (Note: this uses int32 on non-int32 boundaries hence cant do it on arm9). */
      ((int32*)(ptrDest))[0] = ((int32*)ptrSrcLastPos)[0];
      ((int32*)(ptrDest))[1] = ((int32*)ptrSrcLastPos)[1];
      #else
      memcpy(ptrDest, ptrSrcLastPos, 8);
      #endif

      ptrDest+=8;
      ptrSrcLastPos+=8;

      /* Reset offset to zero. */
      uOffset = 0;

      /* We have dealt with eight differing source data bytes. */
      nDifferingBytes -= 8;
    }

    /* There are now eight or less differing source data bytes to copy. */
    *ptrDest = (uint8)((nDifferingBytes << 5) | uOffset);
    if (uOffset > 30)
    {
      *ptrDest++ = (uint8)((nDifferingBytes << 5) | 31);

        /* 31 Bytes already counted in nOffset above */
      for (k = uOffset - 31 - 255; k >= 0; k -= 255)
      {
        *ptrDest++ = 0xff; /* Another 255 bytes. */
      }
      *ptrDest = (uint8)(k + 255); /* Plus the remainder bytes. */
    }

    /* Copy the remaining 1 to 8 raw data bytes.
       The code below is equivalent to the following code
          memcpy(ptrDest+1, ptrSrcLastPos, nDifferingBytes+1);
          ptrDest+=(nDifferingBytes+1)+1;
          ptrSrcLastPos+=(nDifferingBytes+1); or ptrSrcLastPos = ptrSrc;

       Note: this uses int32 on non-int32 boundaries.
    */
    switch (nDifferingBytes)
    {
    case 7:
      #ifndef _ARM9_
      /* Copy eight bytes. (Note: this uses int32 on non-int32 boundaries hence cant do it on arm9). */
      ((int32*)(++ptrDest))[0] = ((int32*)ptrSrcLastPos)[0];
      ((int32*)(ptrDest))[1] = ((int32*)ptrSrcLastPos)[1];
      #else
      ptrDest++;
      memcpy(ptrDest, ptrSrcLastPos, 8);
      #endif

      ptrDest+=7;
      ptrSrcLastPos+=8;
      break;
    case 6:
      *++ptrDest = *ptrSrcLastPos++;
    case 5:
      *++ptrDest = *ptrSrcLastPos++;
    case 4:
      *++ptrDest = *ptrSrcLastPos++;
    case 3:
      #ifndef _ARM9_
      /* Copy four bytes. (Note: this uses int32 on non-int32 boundaries hence cant do it on arm9). */
      ((int32*)(++ptrDest))[0] = ((int32*)ptrSrcLastPos)[0];
      #else
      ptrDest++;
      memcpy(ptrDest, ptrSrcLastPos, 4);
      #endif

      ptrDest+=3;
      ptrSrcLastPos+=4;
      break;
    case 2:
      *++ptrDest = *ptrSrcLastPos++;
    case 1:
      *++ptrDest = *ptrSrcLastPos++;
    case 0:
      *++ptrDest = *ptrSrcLastPos++;
    default:
      break;
    }
    ++ptrDest;
  }

  nLen = (uint32)(ptrDest - pDest);

  /* PCL XL extension 2 - patch the byte count Minus the 2 bytes for byte count */
  if (fPCL6)
    PUTLEWORD(pDest, nLen - 2);

  return (nLen); /* return the byte count - including 2 bytes for byte count in case of PCL XL */
}

/**
 * \brief Media Size Table: there are some omissions in this list, but these suffice
 * for this example.
 */
static MEDIASIZEENTRY MediaSizeTable[] = {
  { "Letter",              8.50F,   11.0F,     eLetterPaper  },
  { "Legal",               8.50F,   14.0F,     eLegalPaper   },
  { "A4",                  8.27F,   11.69F,    eA4Paper      },
  { "Executive",           7.25F,   10.5F,     eExecPaper    },
  { "Ledger",              11.00F,  17.0F,     eLedgerPaper  },
  { "A3", /* 294x419.8 */  11.69F,  16.53F,    eA3Paper  },
  { "No. 10 Envelope",     4.12F,   9.5F,      eCOM10Envelope },
  { "DL Envelope", /* 110x220 */ 4.33F, 8.66F, eDLEnvelope },
  { "B4", /* 257x364 */    10.126F, 14.342F,   eJB4Paper },
  { "B5", /* 182x257 */    7.17F,   10.126F,   eJB5Paper },
  { "A5", /* 148x210 */    5.83F,   8.27F,     eA5Paper  },
  { "A6", /* 105x148 */    4.13F,   5.83F,     eA6Paper  },
  { "Slide", /* 105x148 */    7.5F,   10.0F,   eSlidePaper  },
  { "Tabloid", /* 279x432 */  11.0F,  17.0F,   eTabloid  },
  { "",   /* END MARKER */ 0.00F,   0.00F,     eUnknownPaper  },
};

/**
 * \brief Utility: look up the media size table based on media dimension.
 */
static MEDIASIZEENTRY *pclLookupMediaSize (uint32 nXPixels, uint32 nYPixels,
                                           double nXRes, double nYRes)
{
  int32   n;
  double  nXlower, nXupper;
  double  nYlower, nYupper;

  /* Calculate media size upper and lower bounds in inches.
   * We allow +/- a couple of mm slop.
   */
  nXlower = ((double)nXPixels - (nXRes / 10.0)) / nXRes;
  nXupper = ((double)nXPixels + (nXRes / 10.0)) / nXRes;
  nYlower = ((double)nYPixels - (nYRes / 10.0)) / nYRes;
  nYupper = ((double)nYPixels + (nYRes / 10.0)) / nYRes;

  n = 0;
  while (MediaSizeTable[n].szName[0]) {
    if ((nXlower < MediaSizeTable[n].nfWidth) &&
      (nXupper > MediaSizeTable[n].nfWidth) &&
      (nYlower < MediaSizeTable[n].nfHeight) &&
      (nYupper > MediaSizeTable[n].nfHeight)) {
        break;
    }
    n++;
  }
  if(MediaSizeTable[n].nPclEnum == eSlidePaper)
	  n = 0;

  return &MediaSizeTable[n];
}

MEDIASIZEENTRY* pclPerformMediaLookup (const RasterDescription* pRD,
                                       uint32* pbIsLandscape)
{
  MEDIASIZEENTRY* pMediaSizeEntry;

  *pbIsLandscape = FALSE;
  pMediaSizeEntry = pclLookupMediaSize (pRD->imageWidth, pRD->imageHeight,
    pRD->xResolution, pRD->yResolution);
  if (pMediaSizeEntry->nPclEnum == eUnknownPaper)
  {
    *pbIsLandscape = TRUE;
    pMediaSizeEntry = pclLookupMediaSize (pRD->imageHeight, pRD->imageWidth,
      pRD->yResolution, pRD->xResolution);
    if (pMediaSizeEntry->nPclEnum == eUnknownPaper)
    {
      *pbIsLandscape = FALSE;
      return NULL;
    }
  }

  return pMediaSizeEntry;
}

int32 pclGetPageCopies (const RasterDescription* pRD)
{
  return pRD->noCopies;
}

void invertMonoContoneData (uint8* pData, uint32 nBytes)
{
  uint32 nWords = nBytes / 4;
  uint32 nExtraBytes = nBytes % 4;
  uint8* ptrB;

  /* Invert words. */
  uint32 *ptrW = (uint32*) pData;
  while (nWords --)
    *ptrW ++ ^= 0xffffffffu;

  /* Invert remaining bytes. */
  ptrB = (uint8*) ptrW;
  while (nExtraBytes --)
    *ptrB ++ ^= 0xffu;
}

eRFLIST getRasterFormat (RasterDescription* pRD)
{
  if (pRD->numChannels == 1)
  {
    switch (pRD->rasterDepth)
    {
    case 1:
      return RF_MONO_HT;
    case 8:
      return RF_MONO_CT;
    default:
      /* Unknown format. */
      break;
    }
  }
  else if (pRD->rasterDepth == 8 && pRD->numChannels == 3)
  {
    if (pRD->interleavingStyle == interleavingStyle_pixel)
      return RF_RGB_CT;
  }

  return RF_UNKNOWN;
}

char* writeJobPJL (char* pHdr, RasterDescription* pRD, MEDIASIZEENTRY* pMediaSize, int32 nPCLVersion)
{
  int32 fDuplexing = FALSE, fDuplexShortSide = FALSE;
  /* the following retained as a reference for possible change when using XPS with print ticket*/
#if 0
  if (strlen((const char *)pRD->PtfDocumentDuplex)) {
    if (0 == strcmp("TwoSidedLongEdge", (const char *)pRD->PtfDocumentDuplex))
      fDuplexing = TRUE, fDuplexShortSide = FALSE, fSeenDuplexing = TRUE;
    else if (0 == strcmp("TwoSidedShortEdge", (const char *)pRD->PtfDocumentDuplex))
      fDuplexing = TRUE, fDuplexShortSide = TRUE, fSeenDuplexing = TRUE;
  }

  if (!fSeenDuplexing && strlen((const char *)pRD->PtfJobDuplex)) {
    if (0 == strcmp("TwoSidedLongEdge", (const char *)pRD->PtfJobDuplex))
      fDuplexing = TRUE, fDuplexShortSide = FALSE, fSeenDuplexing = TRUE;
    else if (0 == strcmp("TwoSidedShortEdge", (const char *)pRD->PtfJobDuplex))
      fDuplexing = TRUE, fDuplexShortSide = TRUE, fSeenDuplexing = TRUE;
  }
#endif
  fDuplexing = pRD->duplex;
  fDuplexShortSide = pRD->tumble;
  /* Write the PJL. */
  sprintf (pHdr, "%c%c-12345X", 0x1B, 0x25);
  pHdr += strlen(pHdr);

  sprintf (pHdr, "@PJL JOB" kCRLF
                 "@PJL COMMENT GGS Example PCL%d backend." kCRLF,
                 nPCLVersion);
  pHdr += strlen(pHdr);

  if (pMediaSize) {
    /* Don't specify non-standard paper sizes here. */
    sprintf (pHdr, "@PJL SET PAPER=%s" kCRLF, pMediaSize->szName);
    pHdr += strlen(pHdr);
  }

  sprintf (pHdr, "@PJL SET HOLD=OFF" kCRLF
                 "@PJL SET COPIES=%d" kCRLF, pclGetPageCopies (pRD));
  pHdr += strlen(pHdr);
  sprintf (pHdr, "@PJL SET ORIENTATION=PORTRAIT" kCRLF
                 "@PJL SET PAPERTYPE=NONE" kCRLF);
  pHdr += strlen(pHdr);

  sprintf (pHdr, "@PJL SET DUPLEX=%s" kCRLF, fDuplexing ? "ON" : "OFF");
  pHdr += strlen(pHdr);
  sprintf (pHdr, "@PJL SET BINDING=%s" kCRLF, fDuplexShortSide ? "SHORTEDGE" : "LONGEDGE");
  pHdr += strlen(pHdr);

  switch (getRasterFormat (pRD))
  {
  case RF_MONO_HT:
    sprintf(pHdr, "@PJL SET RENDERMODE=MONO" kCRLF);
    break;
  case RF_MONO_CT:
    sprintf(pHdr, "@PJL SET RENDERMODE=GRAYSCALE" kCRLF);
    break;
  case RF_RGB_CT:
    sprintf (pHdr, "@PJL SET DATAMODE=COLOR" kCRLF
                   "@PJL SET RENDERMODE=COLOR" kCRLF
                   "@PJL SET COLORSPACE=DEVICERGB" kCRLF);
    break;
  default:
    /* Unknown format. */
    assert (FALSE);
    break;
  }
  pHdr += strlen(pHdr);

  sprintf(pHdr, "@PJL SET RESOLUTION=%d" kCRLF, (int)pRD->xResolution);
  pHdr += strlen(pHdr);
  sprintf (pHdr,"@PJL SET BITSPERPIXEL=%d" kCRLF, pRD->rasterDepth);
  pHdr += strlen(pHdr);
  sprintf (pHdr, "@PJL ENTER LANGUAGE=%s" kCRLF, (nPCLVersion == 6) ? "PCLXL" : "PCL");
  pHdr += strlen(pHdr);

  return pHdr;
}

