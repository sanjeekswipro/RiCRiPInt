/*
 * Copyright (c) 2004-2012 Global Graphics Software Ltd.
 *
 * $HopeName: GGEtiff!src:gge_tiff.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/*! \file 
*  \brief TIFF support functions
*
* Include this file to use the TIFF support functions.
*
*/

#include "std.h"
#include "pms.h"
#include "gge_tiff.h"
#include "pms_filesys.h"
#include <stdio.h> /* For FILE */

#ifndef MAX_PATH
/*! Define the maximum file and path name length for tiif output. */
#define MAX_PATH 260
#endif

#include <string.h>
#include "pms_malloc.h"

#ifdef highbytefirst
/*! Shorts are store in four bytes. Only the top two bytes are used. */
#define OUTPUT_SHORT(_val_) (((_val_) & 0x0000FFFF) << 16)
#else
/*! Shorts are store in four bytes. Only the top two bytes are used. */
#define OUTPUT_SHORT(_val_) (_val_)
#endif


/*! Define a common malloc function for this TIFF module */
#define GGE_malloc(x)  OSMalloc(x, PMS_MemoryPoolPMS)
/*! Define a common free function for this TIFF module */
#define GGE_free(x)  OSFree(x, PMS_MemoryPoolPMS)

/*! GGETIFF major version */
#define VER_MAJOR 2

/*! GGETIFF minor version */
#define VER_MINOR 1

/*! GGETIFF revision */
#define VER_REVISION 0

/*! Packbits compression is switched on by default. Uncompressed 
    output is much larger and therefore slower to write to disk. */
#define GGETIFF_COMPRESS

#ifndef NULL
/*! Define NULL if it isn't already defined */
#define NULL 0
#endif

/*! GGETIFF module supports upto four colorants */
#define MAX_COLORANTS 4

/*! \brief Parameters for the GGE TIFF module. */
typedef struct tagGGE_TIFF_globals {
    
    char * paColorantData[MAX_COLORANTS]; /**< The colorant data */
    
    PMS_TyBackChannelWriteFileOut tFile[MAX_COLORANTS];  /**< Only the first element is used when outputting composite.
                                                              Other array elements used when outputting separations. */
    FILE *hFile[MAX_COLORANTS];  /**< Only the first element is used when outputting composite.
                                      Other array elements used when outputting separations. */
    unsigned long ulPos[MAX_COLORANTS]; /**< The position in the output stream. Needed for backchannel. */

    unsigned int uColorants;      /**< Number of colorant in the page being output */
    unsigned int uBytesPerLine;   /**< Number of bytes per raster line in the page being output */
    unsigned int uBitsPerPixel;   /**< Number of bits per pixel in the page being output */
    unsigned int uLinesPerPage;   /**< Number of raster lines in the page being output */
    unsigned int uWidthPixels;    /**< Number of pixels wide in the image output */
    unsigned int uXDPI;           /**< Horizontal resolution of the page being output */
    unsigned int uYDPI;           /**< Vertical resolution of the page being output */
    unsigned int uLineCount;      /**< Cumulative line count, used for banded style output */

    unsigned int bSeparated;      /**< Flag indicating that seperated output is required.
                                       Each colorant will be output as a seperate file */
    unsigned int bRGBPal;         /**< Used to maximize reader compatibility, and reduces
                                       file size. CMYK1bpp and CMYK2bpp output formats can
                                       be written using a 16 or 256 color palette.
                                       Each CMYK color combination is given a unique palette
                                       index. */
    
    char *pszImageDescription;    /**< ASCII string used to record the detail about the 
                                       raster format and job details */

    char **paszPathNames;         /**< Passed back to application, freed on close */

#ifdef GGETIFF_COMPRESS
    unsigned char *pCompressBuffer; /**< Compression buffer memory */
    unsigned int  uCompressLen;     /**< Compression buffer length, not including 
                                         the worst case compression. It used to check 
                                         if we need to realloc a new buffer length */
#endif

    unsigned int *paStripLen;       /**< Length of each strip. Each raster line is
                                         a strip in the TIFF file. Required for packbits. */
    unsigned int uStrips;           /**< Number of strips in the page being output. 
                                         Currently always the same as lines per page */
    unsigned int bSwapBytesInWord;  /**< Bytes 0,1,2,3 in each colorant are swapped 
                                         to 3,2,1,0 during output. */

    unsigned char *apBufferedWrite[MAX_COLORANTS]; /**< Buffered write buffers. */
    unsigned int nWritePos[MAX_COLORANTS];         /**< Current position in write buffers. */

}TGGE_TIFF_GLOBALS, *PTGGE_TIFF_GLOBALS;

/*! TIFF module variables */
TGGE_TIFF_GLOBALS gtGGETIFFGlobals;

/*! TIFF field types according to TIFF specification revision 6 */
#define TIFF_TYPE_ASCII 2
#define TIFF_TYPE_SHORT 3
#define TIFF_TYPE_LONG 4
#define TIFF_TYPE_RATIONAL 5

#pragma pack(push,1)
/*! TIFF tag structure. TIFF specification revision 6 */
typedef struct tagtifftag
{
    short tag;
    short type;
    unsigned int count;
    unsigned int value;
}TIFFTAG;

/*! TIFF structure for rational values. TIFF specification revision 6 */
typedef struct tagrational
{
    unsigned int numer;
    unsigned int denom;
}TIFFRATIONAL;
#pragma pack(pop)

/*! Amount of data to store before writting. Set to zero for no buffering. */
#define GGETIFF_BUFFERED_SIZE (64 * 1024)

unsigned int OutputBandSep(PTGGE_TIFF_BAND_SEPARATED ptGGE_TIFFBandSep);
unsigned int OutputBandCompCMYK(PTGGE_TIFF_BAND_COMPOSITE ptGGE_TIFFBandComp);
unsigned int OutputBandCompRGB(PTGGE_TIFF_BAND_COMPOSITE ptGGE_TIFFBandComp);
unsigned int OutputBandRGBPal(PTGGE_TIFF_BAND_RGB_PALETTE ptGGE_TIFFBandRGBPalette);
unsigned int OutputBandRGBInterleaved(PTGGE_TIFF_BAND_PIXEL_INTERLEAVED ptGGE_TIFFBandPixelInterleaved);
unsigned int OutputTIFFHeader(); /* Called during header selector */
unsigned int OutputImageHeader(); /* Called during close file selector */
unsigned int UpdateMono();
unsigned int UpdateCMYK();
unsigned int UpdateRGB();
unsigned int UpdateRGBPal();

#define GGETIFF_IS_COMP_OPEN() \
  ((!g_bBackChannelPageOutput && gtGGETIFFGlobals.hFile[0]) || \
  (g_bBackChannelPageOutput && gtGGETIFFGlobals.tFile[0].szFilename[0]))

#define GGETIFF_IS_SEP_OPEN(_sep_) \
  ((!g_bBackChannelPageOutput && gtGGETIFFGlobals.hFile[_sep_]) || \
  (g_bBackChannelPageOutput && gtGGETIFFGlobals.tFile[_sep_].szFilename[0]))

/**
 * \brief Open PDF data stream output.
 *
 */
int GGETIFF_OpenOutput(int nSep, const char *pszFilename)
{
  /* Buffered write buffer */
  if(!gtGGETIFFGlobals.apBufferedWrite[nSep])
  {
    gtGGETIFFGlobals.apBufferedWrite[nSep] = GGE_malloc(GGETIFF_BUFFERED_SIZE);
    gtGGETIFFGlobals.nWritePos[nSep] = 0;
    if(!gtGGETIFFGlobals.apBufferedWrite[nSep])
    {
      HQFAIL("Failed to allocate write buffer.");
    }
  }

  gtGGETIFFGlobals.ulPos[nSep] = 0;
  if(g_bBackChannelPageOutput)
  {
    if(strlen(pszFilename) >= PMS_MAX_JOBNAME_LENGTH)
    {
      HQFAIL("filename length is longer than can be stored in this structure.");
      return FALSE;
    }

    strcpy(gtGGETIFFGlobals.tFile[nSep].szFilename, pszFilename);
  }
  else
  {
    gtGGETIFFGlobals.hFile[nSep] = fopen(pszFilename, "wb");
    if(!gtGGETIFFGlobals.hFile[nSep])
      return FALSE;
  }

  return TRUE;
}


/**
 * \brief Flush buffered output.
 *
 */
static int FlushBufferedWrite(int nSep, unsigned char *pBuffer, int nLength)
{
  int nWritten = 0;
  int nResult = 0;

  if(nLength > 0) {
    if(g_bBackChannelPageOutput) {
      nResult = PMS_WriteDataStream(PMS_WRITE_FILE_OUT,
                                    &gtGGETIFFGlobals.tFile[nSep],
                                    pBuffer, nLength, &nWritten);
    }
    else {
      nWritten = (int)fwrite(pBuffer, 1, nLength, gtGGETIFFGlobals.hFile[nSep]);
    }
    HQASSERTV(nWritten==nLength,
              ("GGETIFF_WriteOutput(), Failed to output data... written %d out of %d bytes", nWritten, nLength));
  }

  return nWritten;
}

/**
 * \brief Write TIFF data stream output.
 *
 */
int GGETIFF_WriteOutput(int nSep, unsigned char *pBuffer, int nLength)
{
  int nWritten = 0;

  if((gtGGETIFFGlobals.nWritePos[nSep] + nLength) > GGETIFF_BUFFERED_SIZE)
  {
    /* output buffered data */
    (void)FlushBufferedWrite(nSep, gtGGETIFFGlobals.apBufferedWrite[nSep], gtGGETIFFGlobals.nWritePos[nSep]);
    gtGGETIFFGlobals.nWritePos[nSep] = 0;

    if(nLength > GGETIFF_BUFFERED_SIZE)
    {
      nWritten = FlushBufferedWrite(nSep, pBuffer, nLength);
    }
    else
    {
      memcpy(gtGGETIFFGlobals.apBufferedWrite[nSep] + gtGGETIFFGlobals.nWritePos[nSep],
             pBuffer, nLength);
      gtGGETIFFGlobals.nWritePos[nSep] += nLength;
      nWritten = nLength;
    }
  }
  else
  {
    memcpy(gtGGETIFFGlobals.apBufferedWrite[nSep] + gtGGETIFFGlobals.nWritePos[nSep],
           pBuffer, nLength);
    gtGGETIFFGlobals.nWritePos[nSep] += nLength;
    nWritten = nLength;
  }


  gtGGETIFFGlobals.ulPos[nSep]+=nWritten;

  return nWritten;
}

/**
 * \brief Write TIFF data stream output at absolute position.
 *
 */
int GGETIFF_WriteOutputAtPos(int nSep, unsigned char *pBuffer, int nLength, unsigned long ulPos)
{
  int nWritten = 0;
  int nResult = 0;

  /* output buffered data */
  (void)FlushBufferedWrite(nSep, gtGGETIFFGlobals.apBufferedWrite[nSep], gtGGETIFFGlobals.nWritePos[nSep]);
  gtGGETIFFGlobals.nWritePos[nSep] = 0;

  if(g_bBackChannelPageOutput)
  {
    gtGGETIFFGlobals.tFile[nSep].ulAbsPos = ulPos;
    gtGGETIFFGlobals.tFile[nSep].uFlags = 1;
    nResult = PMS_WriteDataStream(PMS_WRITE_FILE_OUT, &gtGGETIFFGlobals.tFile[nSep], (unsigned char *)pBuffer, nLength, &nWritten);
    gtGGETIFFGlobals.tFile[nSep].ulAbsPos = 0;
    gtGGETIFFGlobals.tFile[nSep].uFlags = 0;

    HQASSERTV(nResult==0,
              ("GGETIFF_WriteOutputAtPos(), Failed to output data... result %d", nResult));
  }
  else
  {
    long ulPos;
    ulPos = ftell(gtGGETIFFGlobals.hFile[nSep]);
    fseek(gtGGETIFFGlobals.hFile[nSep], 4, /* SEEK_SET */ 0);
    nWritten = (int)fwrite(pBuffer, 1, nLength, gtGGETIFFGlobals.hFile[nSep]);
    fseek(gtGGETIFFGlobals.hFile[nSep], ulPos, /* SEEK_SET */ 0);
  }

  HQASSERTV(nWritten==nLength,
            ("GGETIFF_WriteOutputAtPos(), Failed to output data... written %d out of %d bytes", nWritten, nLength));

  return nWritten;
}



/*! CMYK 1bpp can be output using a palette of 16 colors.

  This not only improve reader compatibilty, but the file size is smaller
  than output a TIFF compliant CMYK image (CMYK 1bpp should be output as 8bpp CMYK).

  CMYK color information is not lost. A simple lookup of the palette index
  in the table below will give the original CMYK 1bpp color.

  C  M  Y  K      R      G      B    Palette Index
  0, 0, 0, 0  65535, 65535, 65535,   0
  0, 0, 0, 1      0,     0,     0,   1
  0, 0, 1, 0  65535, 65535,     0,   2
  0, 0, 1, 1      0,     0,     0,   3
  0, 1, 0, 0  65535,     0, 65535,   4
  0, 1, 0, 1      0,     0,     0,   5
  0, 1, 1, 0  65535,     0,     0,   6
  0, 1, 1, 1      0,     0,     0,   7
  1, 0, 0, 0      0, 65535, 65535,   8
  1, 0, 0, 1      0,     0,     0,   9
  1, 0, 1, 0      0, 65535,     0,   10
  1, 0, 1, 1      0,     0,     0,   11
  1, 1, 0, 0      0,     0, 65535,   12
  1, 1, 0, 1      0,     0,     0,   13
  1, 1, 1, 0      0,     0,     0,   14
  1, 1, 1, 1      0,     0,     0    15

  Note: Nine of the 16 colors in the table above are actually
  black. The palette could be reduced to eight entries.
*/
unsigned short gPalette1bpp[16 * 3] = { 
    /* reds */
    65535, 0, 65535, 0, 65535, 0, 65535, 0,
        0, 0,     0, 0,     0, 0,     0, 0,
    /* greens */
    65535, 0, 65535, 0,     0, 0,     0, 0,
    65535, 0, 65535, 0,     0, 0,     0, 0,
    /* blues */
    65535, 0,     0, 0, 65535, 0,     0, 0,
    65535, 0,     0, 0, 65535, 0,     0, 0
};

/*! Macro to convert a CMYK 1bpp color into the palette index */
#define GGE_TIFF_PALETTE_1BPP_INDEX(_c_, _m_, _y_, _k_) ((_c_<<3)|(_m_<<2)|(_y_<<1)|_k_)


/* This is an alternative table to the standard table above.
   This table will output different colors for the blacks - very helpful 
   when testing overprint features.
   C  M  Y  K        R      G      B    ID    
   0, 0, 0, 0    65535, 65535, 65535,   0     
   0, 0, 0, 1        0,     0,     0,   1     -> 40000 40000 40000 for K Only 
   0, 0, 1, 0    65535, 65535,     0,   2     
   0, 0, 1, 1        0,     0,     0,   3     -> 40000 40000 0 for K+Y 
   0, 1, 0, 0    65535,     0, 65535,   4     
   0, 1, 0, 1        0,     0,     0,   5     -> 40000 0 40000 for K+M
   0, 1, 1, 0    65535,     0,     0,   6     
   0, 1, 1, 1        0,     0,     0,   7     -> 40000 0 0 for K+M+Y 
   1, 0, 0, 0        0, 65535, 65535,   8     
   1, 0, 0, 1        0,     0,     0,   9     -> 0 40000 40000 for K+C 
   1, 0, 1, 0        0, 65535,     0,   10    
   1, 0, 1, 1        0,     0,     0,   11    -> 0 40000 0 for K+C+Y
   1, 1, 0, 0        0,     0, 65535,   12    
   1, 1, 0, 1        0,     0,     0,   13    -> 0 0 40000 for K+C+M
   1, 1, 1, 0        0,     0,     0,   14    -> 12800 65535 0 for CMY
   1, 1, 1, 1        0,     0,     0    15    -> 65535 0 12800 for CMYK
*/
/* Uncomment to enable this table, and comment out the one above.
 unsigned short gPalette1bpp[16 * 3] = { 
 reds
    65535, 40000, 65535, 40000, 65535, 40000, 65535, 40000,
        0,     0,     0,     0,     0,     0, 12800, 65535,
 greens
    65535, 40000, 65535, 40000,     0,     0,     0,     0,
    65535, 40000, 65535, 40000,     0,     0, 65535,     0,
 blues
    65535, 40000,     0,     0, 65535, 40000,     0,     0,
    65535, 40000,     0,     0, 65535, 40000,     0, 12800
};
*/

/*! CMYK 2bpp can be output using a palette of 256 colors. */
unsigned short gPalette2bpp[256 * 3] = { 
/* reds */
65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535,
43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690,
21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690, 43690,
29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127, 29127,
14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845, 21845,
14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563, 14563,
7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282, 7282,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* greens */
65535, 65535, 65535, 65535, 43690, 43690, 43690, 43690, 21845, 21845, 21845, 21845, 0, 0, 0, 0,
65535, 65535, 65535, 65535, 43690, 43690, 43690, 43690, 21845, 21845, 21845, 21845, 0, 0, 0, 0,
65535, 65535, 65535, 65535, 43690, 43690, 43690, 43690, 21845, 21845, 21845, 21845, 0, 0, 0, 0,
65535, 65535, 65535, 65535, 43690, 43690, 43690, 43690, 21845, 21845, 21845, 21845, 0, 0, 0, 0,
43690, 43690, 43690, 43690, 29127, 29127, 29127, 29127, 14563, 14563, 14563, 14563, 0, 0, 0, 0,
43690, 43690, 43690, 43690, 29127, 29127, 29127, 29127, 14563, 14563, 14563, 14563, 0, 0, 0, 0,
43690, 43690, 43690, 43690, 29127, 29127, 29127, 29127, 14563, 14563, 14563, 14563, 0, 0, 0, 0,
43690, 43690, 43690, 43690, 29127, 29127, 29127, 29127, 14563, 14563, 14563, 14563, 0, 0, 0, 0,
21845, 21845, 21845, 21845, 14563, 14563, 14563, 14563, 7282, 7282, 7282, 7282, 0, 0, 0, 0,
21845, 21845, 21845, 21845, 14563, 14563, 14563, 14563, 7282, 7282, 7282, 7282, 0, 0, 0, 0,
21845, 21845, 21845, 21845, 14563, 14563, 14563, 14563, 7282, 7282, 7282, 7282, 0, 0, 0, 0,
21845, 21845, 21845, 21845, 14563, 14563, 14563, 14563, 7282, 7282, 7282, 7282, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* blues */
65535, 43690, 21845, 0, 65535, 43690, 21845, 0, 65535, 43690, 21845, 0, 65535, 43690, 21845, 0,
65535, 43690, 21845, 0, 65535, 43690, 21845, 0, 65535, 43690, 21845, 0, 65535, 43690, 21845, 0,
65535, 43690, 21845, 0, 65535, 43690, 21845, 0, 65535, 43690, 21845, 0, 65535, 43690, 21845, 0,
65535, 43690, 21845, 0, 65535, 43690, 21845, 0, 65535, 43690, 21845, 0, 65535, 43690, 21845, 0,
43690, 29127, 14563, 0, 43690, 29127, 14563, 0, 43690, 29127, 14563, 0, 43690, 29127, 14563, 0,
43690, 29127, 14563, 0, 43690, 29127, 14563, 0, 43690, 29127, 14563, 0, 43690, 29127, 14563, 0,
43690, 29127, 14563, 0, 43690, 29127, 14563, 0, 43690, 29127, 14563, 0, 43690, 29127, 14563, 0,
43690, 29127, 14563, 0, 43690, 29127, 14563, 0, 43690, 29127, 14563, 0, 43690, 29127, 14563, 0,
21845, 14563, 7282, 0, 21845, 14563, 7282, 0, 21845, 14563, 7282, 0, 21845, 14563, 7282, 0,
21845, 14563, 7282, 0, 21845, 14563, 7282, 0, 21845, 14563, 7282, 0, 21845, 14563, 7282, 0,
21845, 14563, 7282, 0, 21845, 14563, 7282, 0, 21845, 14563, 7282, 0, 21845, 14563, 7282, 0,
21845, 14563, 7282, 0, 21845, 14563, 7282, 0, 21845, 14563, 7282, 0, 21845, 14563, 7282, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*! Macro to convert a CMYK 2bpp color into the palette index */
#define GGE_TIFF_PALETTE_2BPP_INDEX(_c_, _m_, _y_, _k_) ((_k_<<6)|(_c_<<4)|(_m_<<2)|_y_)

/**
* \brief Packbits compression function.
*
* \param pDest Pointer to the compression buffer.
* \param pSrc  Pointer to the source data.
* \param ilen  Length of the source data.
*
* Run length encodes the data. at least 3 uchar's must occur together to be
* worth packing, since packed data is of the form:
* (uchar)repeat_count, (uchar)repeated_value. Uncompressed data is stored as
* (uchar)number_of_uchars_following, (uchar)Data,
*
* The caller must ensure the compression buffer is long enough to compress
* the source data. The worst case compression length is (src length * 128)/127.
* i.e. An extra byte for every 127 bytes.
*
* \return 0 Length of compressed data.
 */
static int packD8(unsigned char *pDest, unsigned char *pSrc, int ilen)
{
    unsigned char           c;
    register unsigned char  *p;
    unsigned char           *cbp;   /* Compressed buffer pointer */
    int                     i, j, last_i, end_loop;
    cbp = pDest;
    last_i = 0;
    end_loop = ilen - 2;
    for(p = pSrc, i = 0; i < end_loop; p++, i++)
    {
        if((c = *p) != p[1])
        {
            continue;
        }

        /* If only a two byte repeat, and we have literal
        ** data preceeding, then it is best to send the
        ** two bytes as literal data. */
        if((i != last_i) && (c != p[2]))
        {
            continue;
        }

        /* We have found a repeated string. First send out the
        ** literal string in front of it if needed. */
        if((j = i - last_i) > 0)
        {
            /* dump max sized literal records first */
            while(j > 128)
            {
                *cbp++ = 128 - 1;
                memcpy (cbp, &pSrc[last_i], 128);
                cbp += 128;
                last_i += 128;
                j -= 128;
            }

            /* Send last literal record */
            *cbp++ = (unsigned char) (j - 1);
            memcpy (cbp, &pSrc[last_i], j);
            cbp += j;
        }

        j = ilen - i - 1;                   /* Max we can check + 1 */
        p += 2;                             /* Start scanning two bytes ahead */
        while(--j > 0)
        {
            if(c != *p)
            {
                break;
            }

            p++;
        }

        /* p now points to the first byte that doesn't compare */
        j = (int)(p - &pSrc[i]);                   /* Number of repeats */
        i += j;                             /* Advance our index past repeats */

        /* Place in buffer the repeat records */
        while(j > 128)
        {
            *cbp++ = (unsigned char) -127;  /* Repeat 128 times */
            *cbp++ = c;
            j -= 128;
        }

        if(j > 1)
        {
            /* Don't put in a one byte repeat! */
            *cbp++ = (unsigned char) ((-j) + 1);
            *cbp++ = c;
            j = 0;
        }

        last_i = i - j;     /* Mark our last index converted */
        i--;                /* Adjust for increment in for loop */
        p = &pSrc[i];        /* Get p back in sync */
    }                       /* end of for */

    /* Check to see if we need to send trailing literal records */
    if((j = ilen - last_i) > 0)
    {
        /* dump max sized literal records first */
        while(j > 128)
        {
            *cbp++ = 128 - 1;
            memcpy (cbp, &pSrc[last_i], 128);
            cbp += 128;
            last_i += 128;
            j -= 128;
        }

        /* Send last literal record */
        *cbp++ = (unsigned char) (j - 1);
        memcpy (cbp, &pSrc[last_i], j);
        cbp += j;
    }                       /* end of if */

    return (int)(cbp - pDest);
}

/**
 * \brief Set version structure.
 *
 * \param pVersion Pointer to GGE TIFF version structure to populate.
 */
static void SetVersionStructure(PTGGE_TIFF_VERSION pVersion) 
{
    /* Set version */
    pVersion->uMajorVer = VER_MAJOR;
    pVersion->uMinorVer = VER_MINOR;
    pVersion->uRevision = VER_REVISION;
}

/**
 * \brief Free memory allocated by module.
 */
static void FreeTIFFResources()
{
    unsigned int i;

    for(i = 0; i < MAX_COLORANTS; i++)
    {
      if(gtGGETIFFGlobals.apBufferedWrite[i])
      {
        GGE_free(gtGGETIFFGlobals.apBufferedWrite[i]);
        gtGGETIFFGlobals.apBufferedWrite[i] = NULL;
        gtGGETIFFGlobals.nWritePos[i] = 0;
      }
    }

    if(gtGGETIFFGlobals.bSeparated)
    {
        for(i = 0; i < gtGGETIFFGlobals.uColorants; i++)
        {
            if(g_bBackChannelPageOutput)
            {
                gtGGETIFFGlobals.tFile[i].szFilename[0]='\0';
            }
            else
            {
                if(gtGGETIFFGlobals.hFile[i])
                {
                    fclose(gtGGETIFFGlobals.hFile[i]);
                    gtGGETIFFGlobals.hFile[i] = NULL;
                }
            }

            if(gtGGETIFFGlobals.paszPathNames && gtGGETIFFGlobals.paszPathNames[i])
            {
                GGE_free(gtGGETIFFGlobals.paszPathNames[i]);
            }
        }
    }
    else
    {
        if(gtGGETIFFGlobals.paszPathNames && gtGGETIFFGlobals.paszPathNames[0])
        {
            GGE_free(gtGGETIFFGlobals.paszPathNames[0]);
        }
        if(g_bBackChannelPageOutput)
        {
            gtGGETIFFGlobals.tFile[0].szFilename[0]='\0';
        }
        else
        {
            if(gtGGETIFFGlobals.hFile[0])
            {
                fclose(gtGGETIFFGlobals.hFile[0]);
                gtGGETIFFGlobals.hFile[0] = NULL;
            }
        }
    }

    if(gtGGETIFFGlobals.paszPathNames)
    {
        GGE_free(gtGGETIFFGlobals.paszPathNames);
        gtGGETIFFGlobals.paszPathNames = NULL;
    }

    if(gtGGETIFFGlobals.pszImageDescription)
    {
        GGE_free(gtGGETIFFGlobals.pszImageDescription);
    }

#ifdef GGETIFF_COMPRESS
    if(gtGGETIFFGlobals.pCompressBuffer)
    {
        GGE_free(gtGGETIFFGlobals.pCompressBuffer);
        gtGGETIFFGlobals.pCompressBuffer = NULL;
        gtGGETIFFGlobals.uCompressLen = 0;
    }
#endif

    if(gtGGETIFFGlobals.paStripLen)
    {
        GGE_free(gtGGETIFFGlobals.paStripLen);
        gtGGETIFFGlobals.paStripLen = NULL;
        gtGGETIFFGlobals.uStrips = 0;
    }

    /* a lazy way to NULL all pointers in the TIFF global stucture */
    memset(&gtGGETIFFGlobals, 0x00, sizeof(gtGGETIFFGlobals));
}

/**
 * \brief GGE TIFF API call.
 *
 * \param uSelector GGE TIFF function to process.
 * \param pContext Pointer to a GGE TIFF structure that has meaning to the selector.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int GGE_TIFF(enum EGGE_TIFF_INPUT_SELECTOR uSelector, void *pContext)
{
    unsigned int uRetVal = GGE_TIFF_SUCCESS;
    unsigned int i;
    
    switch(uSelector)
    {
    case GGE_TIFF_INSEL_VERSION:
        {
            PTGGE_TIFF_VERSION ptGGE_TIFFVersion = (PTGGE_TIFF_VERSION)pContext;
            
            if(pContext == NULL)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFVersion->cbSize != sizeof(TGGE_TIFF_VERSION))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_DATA_SIZE;
                break;
            }
            
            SetVersionStructure(ptGGE_TIFFVersion);
        }
        break;
        
    case GGE_TIFF_INSEL_SELECTOR_SUPPORTED:
        {
            PTGGE_TIFF_SELECTOR_SUPPORTED ptGGE_TIFFSelectorSupported = (PTGGE_TIFF_SELECTOR_SUPPORTED)pContext;
            
            if(ptGGE_TIFFSelectorSupported == NULL)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFSelectorSupported->cbSize != sizeof(TGGE_TIFF_SELECTOR_SUPPORTED))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_DATA_SIZE;
                break;
            }
            
            switch(ptGGE_TIFFSelectorSupported->uSelector)
            {
            default:
                ptGGE_TIFFSelectorSupported->bSupported = 0;
                break;
            case GGE_TIFF_INSEL_VERSION:
                
            case GGE_TIFF_INSEL_SELECTOR_SUPPORTED:
            case GGE_TIFF_INSEL_HEADER:
            case GGE_TIFF_INSEL_BAND_SEPARATED:
            case GGE_TIFF_INSEL_BAND_COMPOSITE:
            case GGE_TIFF_INSEL_FLUSH:
            case GGE_TIFF_INSEL_CLOSE:
                ptGGE_TIFFSelectorSupported->bSupported = 1;
                break;
            }
        }
        break;
        
    case GGE_TIFF_INSEL_HEADER:
        {
            PTGGE_TIFF_HEADER ptGGE_TIFFHeader = (PTGGE_TIFF_HEADER)pContext;
            char sCMYKColors[4] = {'C', 'M', 'Y', 'K'};
            char sRGBColors[3] = {'R', 'G', 'B'};
            char sColor;
            
            if(ptGGE_TIFFHeader == NULL)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFHeader->cbSize != sizeof(TGGE_TIFF_HEADER))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_DATA_SIZE;
                break;
            }
            
            if(ptGGE_TIFFHeader->uColorants > MAX_COLORANTS)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFHeader->pszPathName == NULL)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(strlen(ptGGE_TIFFHeader->pszPathName) > (MAX_PATH - 6))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFHeader->uBitsPerPixel == 0)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }

            if(ptGGE_TIFFHeader->uBitsPerPixel > 16)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }

            if(ptGGE_TIFFHeader->uBytesPerLine == 0)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }

            memset(&gtGGETIFFGlobals, 0x00, sizeof(gtGGETIFFGlobals));

            gtGGETIFFGlobals.uColorants = ptGGE_TIFFHeader->uColorants;
            gtGGETIFFGlobals.uBytesPerLine = ptGGE_TIFFHeader->uBytesPerLine;
            gtGGETIFFGlobals.uBitsPerPixel = ptGGE_TIFFHeader->uBitsPerPixel;
            gtGGETIFFGlobals.uLinesPerPage = ptGGE_TIFFHeader->uLinesPerPage;
            gtGGETIFFGlobals.uWidthPixels = ptGGE_TIFFHeader->uWidthPixels;
            gtGGETIFFGlobals.uXDPI = ptGGE_TIFFHeader->uXDPI;
            gtGGETIFFGlobals.uYDPI = ptGGE_TIFFHeader->uYDPI;
            gtGGETIFFGlobals.bSeparated = ptGGE_TIFFHeader->bSeparated;
            gtGGETIFFGlobals.uLineCount = 0;
            gtGGETIFFGlobals.bRGBPal = ptGGE_TIFFHeader->bRGBPal;
            gtGGETIFFGlobals.bSwapBytesInWord = ptGGE_TIFFHeader->bSwapBytesInWord;

            if(ptGGE_TIFFHeader->pszDescription)
            {
                gtGGETIFFGlobals.pszImageDescription = (char*)GGE_malloc(strlen((char*)ptGGE_TIFFHeader->pszDescription)+1);
                if(gtGGETIFFGlobals.pszImageDescription)
                {
                    strcpy((char*)gtGGETIFFGlobals.pszImageDescription, (char*)ptGGE_TIFFHeader->pszDescription);
                }
                else
                {
                    HQFAIL("Failed to allocate memory for TIFF image description. Continuing without image description.");
                }
            }

            if(gtGGETIFFGlobals.bSeparated)
            {
                gtGGETIFFGlobals.paszPathNames = (char**)GGE_malloc(sizeof(char *) * gtGGETIFFGlobals.uColorants);

                if(!gtGGETIFFGlobals.paszPathNames)
                {
                    HQFAIL("Failed to allocate memory for separation filename strings. No TIFF output.");
                    FreeTIFFResources();
                    uRetVal = GGE_TIFF_ERROR_OUT_OF_MEMORY;
                    break;
                }

                for(i = 0; i < gtGGETIFFGlobals.uColorants; i++)
                {
                    gtGGETIFFGlobals.paszPathNames[i] = (char *)GGE_malloc(MAX_PATH + 1);
                    if(!gtGGETIFFGlobals.paszPathNames[i])
                    {
                        HQFAIL("Failed to allocate memory for separation filename.");
                        FreeTIFFResources();
                        uRetVal = GGE_TIFF_ERROR_OUT_OF_MEMORY;
                        break;
                    }

                    if(gtGGETIFFGlobals.uColorants == 4)
                      sColor = sCMYKColors[i];
                    else if(gtGGETIFFGlobals.uColorants == 3)
                      sColor = sRGBColors[i];
                    else
                      sColor = 'K';
                    sprintf(&gtGGETIFFGlobals.paszPathNames[i][0], "%s-%c.tiff",
                        ptGGE_TIFFHeader->pszPathName,
                        sColor
                        );

                    if(!GGETIFF_OpenOutput(i, gtGGETIFFGlobals.paszPathNames[i]))
                    {
                      HQFAILV(("Failed to open file %s.", gtGGETIFFGlobals.paszPathNames[i]));
                        FreeTIFFResources();
                        uRetVal = GGE_TIFF_ERROR_OUT_OF_MEMORY;
                        break;
                    }
                }
            }
            else
            {
                gtGGETIFFGlobals.paszPathNames = (char**)GGE_malloc(sizeof(char *));
                if(!gtGGETIFFGlobals.paszPathNames)
                {
                    HQFAIL("Failed to allocate memory for filename.");
                    FreeTIFFResources();
                    uRetVal = GGE_TIFF_ERROR_OUT_OF_MEMORY;
                    break;
                }

                gtGGETIFFGlobals.paszPathNames[0] = (char *)GGE_malloc(MAX_PATH + 1);
                if(!gtGGETIFFGlobals.paszPathNames[0])
                {
                    HQFAIL("Failed to allocate memory for pathname.");
                    FreeTIFFResources();
                    uRetVal = GGE_TIFF_ERROR_OUT_OF_MEMORY;
                    break;
                }
                gtGGETIFFGlobals.paszPathNames[0][0] = '\0';

                sprintf(&gtGGETIFFGlobals.paszPathNames[0][0], "%s.tiff",
                    ptGGE_TIFFHeader->pszPathName
                    );

                if(!GGETIFF_OpenOutput(0, gtGGETIFFGlobals.paszPathNames[0]))
                {
                  HQFAILV(("Failed to open file %s.", gtGGETIFFGlobals.paszPathNames[0]));
                    FreeTIFFResources();
                    uRetVal = GGE_TIFF_ERROR_OUT_OF_MEMORY;
                    break;
                }
            }

            if(uRetVal == GGE_TIFF_SUCCESS)
            {
              uRetVal = OutputTIFFHeader();

              ptGGE_TIFFHeader->paszPathNames = gtGGETIFFGlobals.paszPathNames;
            }
        }
        break;
        
    case GGE_TIFF_INSEL_BAND_SEPARATED:
        {
            PTGGE_TIFF_BAND_SEPARATED ptGGE_TIFFBandSep = (PTGGE_TIFF_BAND_SEPARATED)pContext;
            
            if(ptGGE_TIFFBandSep == NULL)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFBandSep->cbSize != sizeof(TGGE_TIFF_BAND_SEPARATED))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_DATA_SIZE;
                break;
            }
            
            if(!gtGGETIFFGlobals.bSeparated)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            gtGGETIFFGlobals.uLineCount += ptGGE_TIFFBandSep->uLinesThisBand;

            uRetVal = OutputBandSep(ptGGE_TIFFBandSep);
        }
        break;
        
    case GGE_TIFF_INSEL_BAND_PIXEL_INTERLEAVED:
        {
            PTGGE_TIFF_BAND_PIXEL_INTERLEAVED ptGGE_TIFFBandPixelInterleaved = (PTGGE_TIFF_BAND_PIXEL_INTERLEAVED)pContext;
            
            if(ptGGE_TIFFBandPixelInterleaved == NULL)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFBandPixelInterleaved->cbSize != sizeof(TGGE_TIFF_BAND_PIXEL_INTERLEAVED))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_DATA_SIZE;
                break;
            }
            
            if(gtGGETIFFGlobals.bSeparated)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            gtGGETIFFGlobals.uLineCount += ptGGE_TIFFBandPixelInterleaved->uLinesThisBand;

            if (gtGGETIFFGlobals.uColorants==3)
            {
              uRetVal = OutputBandRGBInterleaved(ptGGE_TIFFBandPixelInterleaved);
            }
            else
            {
              uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
            }
        }
        break;

    case GGE_TIFF_INSEL_BAND_COMPOSITE:
        {
            PTGGE_TIFF_BAND_COMPOSITE ptGGE_TIFFBandComp = (PTGGE_TIFF_BAND_COMPOSITE)pContext;
            
            if(ptGGE_TIFFBandComp == NULL)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFBandComp->cbSize != sizeof(TGGE_TIFF_BAND_COMPOSITE))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_DATA_SIZE;
                break;
            }
            
            if(gtGGETIFFGlobals.bSeparated)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            gtGGETIFFGlobals.uLineCount += ptGGE_TIFFBandComp->uLinesThisBand;

            if(gtGGETIFFGlobals.uColorants==4)
            {
              uRetVal = OutputBandCompCMYK(ptGGE_TIFFBandComp);
            }
            else if (gtGGETIFFGlobals.uColorants==3)
            {
              uRetVal = OutputBandCompRGB(ptGGE_TIFFBandComp);
            }
            else
            {
              uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
            }
        }
        break;
        
    case GGE_TIFF_INSEL_BAND_RGB_PALETTE:
        {
            PTGGE_TIFF_BAND_RGB_PALETTE ptGGE_TIFFBandRGBPal = (PTGGE_TIFF_BAND_RGB_PALETTE)pContext;
            
            if(ptGGE_TIFFBandRGBPal == NULL)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(ptGGE_TIFFBandRGBPal->cbSize != sizeof(TGGE_TIFF_BAND_COMPOSITE))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_DATA_SIZE;
                break;
            }
            
            if(gtGGETIFFGlobals.bSeparated)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }
            
            if(gtGGETIFFGlobals.uColorants!=4)
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }

            if(!((gtGGETIFFGlobals.uBitsPerPixel==1)||(gtGGETIFFGlobals.uBitsPerPixel==2)))
            {
                uRetVal = GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
                break;
            }

            gtGGETIFFGlobals.uLineCount += ptGGE_TIFFBandRGBPal->uLinesThisBand;

            uRetVal = OutputBandRGBPal(ptGGE_TIFFBandRGBPal);
        }
        break;
        
    case GGE_TIFF_INSEL_FLUSH:
        {
            /*  not sure that we have to do any writes here. */
            if(g_bBackChannelPageOutput)
            {
              /* back channel flush not supported */
              break;
            }
            else
            {
                for(i = 0; i < MAX_COLORANTS; i++)
                {
                    if(gtGGETIFFGlobals.hFile[i])
                    {
                        fflush(gtGGETIFFGlobals.hFile[i]);
                    }
                }
            }
        }
        break;
        
    case GGE_TIFF_INSEL_CLOSE:
        {
            /* If an error has previously occured, then the
               resources have been freed... but the caller
               may still call close selector. */
            if(gtGGETIFFGlobals.uColorants != 0)
            {
                unsigned int i;

                OutputImageHeader();
                for(i = 0; i < gtGGETIFFGlobals.uColorants; i++)
                {
                  (void)FlushBufferedWrite(i, gtGGETIFFGlobals.apBufferedWrite[i], gtGGETIFFGlobals.nWritePos[i]);
                  gtGGETIFFGlobals.nWritePos[i] = 0;
                }
            }
            FreeTIFFResources();
        }
        break;

    case GGE_TIFF_LAST_INPUT_SELECTOR:
    default:
        break;
        
    }

    if(uRetVal != GGE_TIFF_SUCCESS)
    {
        FreeTIFFResources();
    }

    return uRetVal;
}

/**
 * \brief Output separations.
 *
 * Each colorant will be written to a seperate file.
 *
 * \param ptGGE_TIFFBandSep Pointer to a TGGE_TIFF_BAND_SEPARATED structure.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int OutputBandSep(PTGGE_TIFF_BAND_SEPARATED ptGGE_TIFFBandSep)
{
    unsigned int y;
#ifdef GGETIFF_COMPRESS
    unsigned int uCompressed;
#endif

    if((gtGGETIFFGlobals.uStrips != gtGGETIFFGlobals.uLinesPerPage) || (!gtGGETIFFGlobals.paStripLen))
    {
      if(gtGGETIFFGlobals.paStripLen)
      {
        GGE_free(gtGGETIFFGlobals.paStripLen);
      }
      gtGGETIFFGlobals.paStripLen = GGE_malloc(gtGGETIFFGlobals.uColorants * gtGGETIFFGlobals.uLinesPerPage * sizeof(unsigned int));
      if(!gtGGETIFFGlobals.paStripLen)
      {
          HQFAIL("Failed to allocate memory for strip lengths.");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uStrips = gtGGETIFFGlobals.uLinesPerPage;
    }

#ifdef GGETIFF_COMPRESS
    if(gtGGETIFFGlobals.uCompressLen != gtGGETIFFGlobals.uBytesPerLine)
    {
      if(gtGGETIFFGlobals.pCompressBuffer)
      {
        GGE_free(gtGGETIFFGlobals.pCompressBuffer);
      }

      gtGGETIFFGlobals.pCompressBuffer = GGE_malloc(((gtGGETIFFGlobals.uBytesPerLine * 128)+127)/127);
      if(!gtGGETIFFGlobals.pCompressBuffer)
      {
          HQFAIL("Failed to allocate memory for the compression buffer.");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uCompressLen = gtGGETIFFGlobals.uBytesPerLine;
    }
#endif

    /* Write Raster */
    if(GGETIFF_IS_SEP_OPEN(ptGGE_TIFFBandSep->uColorant))
    {
      if(gtGGETIFFGlobals.bSwapBytesInWord)
      {
        char *pSwapBuffer;
        char *pDst;
        char *pSrc;
        unsigned int x;

        pSwapBuffer = (char*)GGE_malloc(gtGGETIFFGlobals.uBytesPerLine);
        if(!pSwapBuffer)
        {
            HQFAIL("Failed to allocate memory for the byte swap buffer.");
            FreeTIFFResources();
        }
        for(y=0; y < ptGGE_TIFFBandSep->uLinesThisBand; y++)
        {
          pDst = pSwapBuffer;
          pSrc = ptGGE_TIFFBandSep->pBandBuffer + (y * gtGGETIFFGlobals.uBytesPerLine);
          for(x=0; x < (gtGGETIFFGlobals.uBytesPerLine & ~3); x+=4)
          {
            pDst[0] = pSrc[3];
            pDst[1] = pSrc[2];
            pDst[2] = pSrc[1];
            pDst[3] = pSrc[0];
            pSrc+=4;
            pDst+=4;
          }

#ifdef GGETIFF_COMPRESS
          uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pSwapBuffer, gtGGETIFFGlobals.uBytesPerLine);
          gtGGETIFFGlobals.paStripLen[(y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandSep->uLinesThisBand)] = uCompressed;
          GGETIFF_WriteOutput(ptGGE_TIFFBandSep->uColorant, gtGGETIFFGlobals.pCompressBuffer, uCompressed);
#else
          gtGGETIFFGlobals.paStripLen[(y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandSep->uLinesThisBand)] = gtGGETIFFGlobals.uBytesPerLine;
          GGETIFF_WriteOutput(ptGGE_TIFFBandSep->uColorant, pSwapBuffer, gtGGETIFFGlobals.uBytesPerLine );
#endif
        }
        GGE_free(pSwapBuffer);
      }
      else
      {
#ifdef GGETIFF_COMPRESS
        unsigned char *p = (unsigned char*)ptGGE_TIFFBandSep->pBandBuffer;
        for(y=0; y < ptGGE_TIFFBandSep->uLinesThisBand; y++)
        {
          uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, p, gtGGETIFFGlobals.uBytesPerLine);
          gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandSep->uLinesThisBand] = uCompressed;
          GGETIFF_WriteOutput( ptGGE_TIFFBandSep->uColorant, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
          p+=gtGGETIFFGlobals.uBytesPerLine;
        }
#else
        for(y=0; y < ptGGE_TIFFBandSep->uLinesThisBand; y++)
        {
          gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandSep->uLinesThisBand] = gtGGETIFFGlobals.uBytesPerLine;
        }
        GGETIFF_WriteOutput( ptGGE_TIFFBandSep->uColorant, ptGGE_TIFFBandSep->pBandBuffer, gtGGETIFFGlobals.uBytesPerLine * ptGGE_TIFFBandSep->uLinesThisBand);
#endif
      }
    }
    else
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }
        
    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Output RGB Pixel interleaved.
 *
 * All colorants will be written to the same file.
 *
 * \param ptGGE_TIFFBandPixelInterleaved Pointer to a TGGE_TIFF_INSEL_BAND_PIXEL_INTERLEAVED structure.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int OutputBandRGBInterleaved(PTGGE_TIFF_BAND_PIXEL_INTERLEAVED ptGGE_TIFFBandPixelInterleaved)
{
    unsigned int uLineLength;
    unsigned int y;
    unsigned char *pSrc;
#ifdef GGETIFF_COMPRESS
    unsigned int uCompressed;
#endif

    if(!ptGGE_TIFFBandPixelInterleaved->pBandBuffer)
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }

    if(!GGETIFF_IS_COMP_OPEN())
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }

    uLineLength = gtGGETIFFGlobals.uBytesPerLine;

    if((gtGGETIFFGlobals.uStrips != gtGGETIFFGlobals.uLinesPerPage) || (!gtGGETIFFGlobals.paStripLen))
    {
      if(gtGGETIFFGlobals.paStripLen)
      {
        GGE_free(gtGGETIFFGlobals.paStripLen);
      }
      gtGGETIFFGlobals.paStripLen = GGE_malloc(gtGGETIFFGlobals.uLinesPerPage * sizeof(unsigned int));
      if(!gtGGETIFFGlobals.paStripLen)
      {
          PMS_FAIL("Failed to allocate memory for strip lengths.\n");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uStrips = gtGGETIFFGlobals.uLinesPerPage;
    }

#ifdef GGETIFF_COMPRESS
    if(gtGGETIFFGlobals.uCompressLen != uLineLength)
    {
        if(gtGGETIFFGlobals.pCompressBuffer)
        {
            GGE_free(gtGGETIFFGlobals.pCompressBuffer);
        }

        gtGGETIFFGlobals.pCompressBuffer = GGE_malloc(((uLineLength * 128)+127)/127);
        if(!gtGGETIFFGlobals.pCompressBuffer)
        {
            PMS_FAIL("Failed to allocate memory for the compression buffer.\n");
            FreeTIFFResources();
            return GGE_TIFF_ERROR_OUT_OF_MEMORY;
        }
        gtGGETIFFGlobals.uCompressLen = uLineLength;
    }
#endif

    pSrc = (unsigned char *)ptGGE_TIFFBandPixelInterleaved->pBandBuffer;
    for(y = 0; y < ptGGE_TIFFBandPixelInterleaved->uLinesThisBand; y++)
    {
#ifdef GGETIFF_COMPRESS
        uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, pSrc, uLineLength);
        gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandPixelInterleaved->uLinesThisBand] = uCompressed;
        GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
        gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandPixelInterleaved->uLinesThisBand] = uLineLength;
        GGETIFF_WriteOutput( 0, pSrc, uLineLength );
#endif
        pSrc+=uLineLength;
    }

    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Output composite CMYK.
 *
 * All colorants will be written to the same file.
 *
 * \param ptGGE_TIFFBandComp Pointer to a TGGE_TIFF_BAND_COMPOSITE structure.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int OutputBandCompCMYK(PTGGE_TIFF_BAND_COMPOSITE ptGGE_TIFFBandComp)
{
    int i;
    unsigned int x, y;
    char *pSrc0;
    char *pSrc1;
    char *pSrc2;
    char *pSrc3;
    char *pDst;
    char *pBufImage;
    unsigned int uLineLength;
    static unsigned char aMask[4] = {0xc0, 0x30, 0x0c, 0x03};
    static unsigned char aShift[4] = {6, 4, 2, 0};
#ifdef GGETIFF_COMPRESS
    unsigned int uCompressed;
#endif
    
    if(!ptGGE_TIFFBandComp->paBandBuffer)
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }

    for(x = 0; x < ptGGE_TIFFBandComp->uColorants; x++)
    {
        if(!ptGGE_TIFFBandComp->paBandBuffer[x])
        {
            FreeTIFFResources();
            return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
        }
    }

    if(!GGETIFF_IS_COMP_OPEN())
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }

    pBufImage = NULL;

    switch(gtGGETIFFGlobals.uBitsPerPixel)
    {
    default:
    case 1:
        uLineLength = gtGGETIFFGlobals.uBytesPerLine * gtGGETIFFGlobals.uColorants * 8;
        pBufImage = (char*)GGE_malloc(uLineLength);
        break;
    case 2:
        uLineLength = gtGGETIFFGlobals.uBytesPerLine * gtGGETIFFGlobals.uColorants * 4;
        pBufImage = (char*)GGE_malloc(uLineLength);
        break;
    case 4:
        uLineLength = gtGGETIFFGlobals.uBytesPerLine * gtGGETIFFGlobals.uColorants * 2;
        pBufImage = (char*)GGE_malloc(uLineLength);
        break;
    case 8:
    case 16:
        uLineLength = gtGGETIFFGlobals.uBytesPerLine * gtGGETIFFGlobals.uColorants * 1;
        pBufImage = (char*)GGE_malloc(uLineLength);
        break;
    }

    if(!pBufImage)
    {
        HQFAIL("Failed to allocate memory for the band buffer.");
        FreeTIFFResources();
        return GGE_TIFF_ERROR_OUT_OF_MEMORY;
    }

    if((gtGGETIFFGlobals.uStrips != gtGGETIFFGlobals.uLinesPerPage) || (!gtGGETIFFGlobals.paStripLen))
    {
      if(gtGGETIFFGlobals.paStripLen)
      {
        GGE_free(gtGGETIFFGlobals.paStripLen);
      }
      gtGGETIFFGlobals.paStripLen = GGE_malloc(gtGGETIFFGlobals.uLinesPerPage * sizeof(unsigned int));
      if(!gtGGETIFFGlobals.paStripLen)
      {
          HQFAIL("Failed to allocate memory for strip lengths.");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uStrips = gtGGETIFFGlobals.uLinesPerPage;
    }

#ifdef GGETIFF_COMPRESS
    if(gtGGETIFFGlobals.uCompressLen != uLineLength)
    {
      if(gtGGETIFFGlobals.pCompressBuffer)
      {
        GGE_free(gtGGETIFFGlobals.pCompressBuffer);
      }

      gtGGETIFFGlobals.pCompressBuffer = GGE_malloc(((uLineLength * 128)+127)/127);
      if(!gtGGETIFFGlobals.pCompressBuffer)
      {
          HQFAIL("Failed to allocate memory for the compression buffer.");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uCompressLen = uLineLength;
    }
#endif


    pSrc0 = ptGGE_TIFFBandComp->paBandBuffer[0];
    pSrc1 = ptGGE_TIFFBandComp->paBandBuffer[1];
    pSrc2 = ptGGE_TIFFBandComp->paBandBuffer[2];
    pSrc3 = ptGGE_TIFFBandComp->paBandBuffer[3];

    switch(gtGGETIFFGlobals.uBitsPerPixel)
    {
    default:
    case 1:
        {        
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
              pDst = pBufImage;
              if(!gtGGETIFFGlobals.bSwapBytesInWord)
              {
                for(x = 0; x < gtGGETIFFGlobals.uBytesPerLine; x++)
                {
                  for(i = 7; i >= 0; i--)
                  {
                    *pDst = ((*pSrc0)&(1<<i))?0xff:0x00;
                    pDst++;
                    *pDst = ((*pSrc1)&(1<<i))?0xff:0x00;
                    pDst++;
                    *pDst = ((*pSrc2)&(1<<i))?0xff:0x00;
                    pDst++;
                    *pDst = ((*pSrc3)&(1<<i))?0xff:0x00;
                    pDst++;
                  }
                  pSrc0++;
                  pSrc1++;
                  pSrc2++;
                  pSrc3++;
                }
              }
              else
              {
                for(x = 0; x < (gtGGETIFFGlobals.uBytesPerLine&~3); x+=4)
                {
                  for(i = 7; i >= 0; i--)
                  {
                    *pDst++ = ((pSrc0[3])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc1[3])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc2[3])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc3[3])&(1<<i))?0xff:0x00;
                  }
                  for(i = 7; i >= 0; i--)
                  {
                    *pDst++ = ((pSrc0[2])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc1[2])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc2[2])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc3[2])&(1<<i))?0xff:0x00;
                  }
                  for(i = 7; i >= 0; i--)
                  {
                    *pDst++ = ((pSrc0[1])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc1[1])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc2[1])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc3[1])&(1<<i))?0xff:0x00;
                  }
                  for(i = 7; i >= 0; i--)
                  {
                    *pDst++ = ((pSrc0[0])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc1[0])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc2[0])&(1<<i))?0xff:0x00;
                    *pDst++ = ((pSrc3[0])&(1<<i))?0xff:0x00;
                  }
                  pSrc0+=4;
                  pSrc1+=4;
                  pSrc2+=4;
                  pSrc3+=4;
                }
              }

#ifdef GGETIFF_COMPRESS
              uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
              gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uCompressed;
              GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
              gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uLineLength;
              GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
    
            }
        }
        break;
    case 2:
        {
          /* todo gtGGETIFFGlobals.bSwapBytesInWord */
          if(gtGGETIFFGlobals.bSwapBytesInWord)
          {
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
              pDst = pBufImage;
              for(x = 0; x < (gtGGETIFFGlobals.uBytesPerLine&~3); x+=4)
              {
                for(i = 0; i < 4; i++)
                {
                  *pDst = ((pSrc0[3] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc1[3] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc2[3] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc3[3] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                }
                pSrc0++;
                pSrc1++;
                pSrc2++;
                pSrc3++;

                for(i = 0; i < 4; i++)
                {
                  *pDst = ((pSrc0[2] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc1[2] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc2[2] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc3[2] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                }
                pSrc0++;
                pSrc1++;
                pSrc2++;
                pSrc3++;

                for(i = 0; i < 4; i++)
                {
                  *pDst = ((pSrc0[1] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc1[1] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc2[1] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc3[1] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                }
                pSrc0++;
                pSrc1++;
                pSrc2++;
                pSrc3++;

                for(i = 0; i < 4; i++)
                {
                  *pDst = ((pSrc0[0] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc1[0] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc2[0] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((pSrc3[0] & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                }
                pSrc0++;
                pSrc1++;
                pSrc2++;
                pSrc3++;
              }
            }
          }
          else
          {
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
              pDst = pBufImage;
              for(x = 0; x < gtGGETIFFGlobals.uBytesPerLine; x++)
              {
                for(i = 0; i < 4; i++)
                {
                  *pDst = ((*pSrc0 & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((*pSrc1 & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((*pSrc2 & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                  *pDst = ((*pSrc3 & aMask[i]) >> aShift[i]) * 85;
                  pDst++;
                }
                pSrc0++;
                pSrc1++;
                pSrc2++;
                pSrc3++;
              }
            }
#ifdef GGETIFF_COMPRESS
                uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uCompressed;
                GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uLineLength;
                GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
            }
        }
        break;
    case 4:
        {
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
              pDst = pBufImage;
              if(!gtGGETIFFGlobals.bSwapBytesInWord)
              {
                for(x = 0; x < gtGGETIFFGlobals.uBytesPerLine; x++)
                {
                  *pDst = ((((*pSrc0)&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((*pSrc1)&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((*pSrc2)&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((*pSrc3)&0xF0)>>4)*17);
                  pDst++;
              
                  *pDst = (((*pSrc0)&0x0F)*17);
                  pDst++;
                  *pDst = (((*pSrc1)&0x0F)*17);
                  pDst++;
                  *pDst = (((*pSrc2)&0x0F)*17);
                  pDst++;
                  *pDst = (((*pSrc3)&0x0F)*17);
                  pDst++;
              
                  pSrc0++;
                  pSrc1++;
                  pSrc2++;
                  pSrc3++;
                }
              }
              else
              {
                for(x = 0; x < (gtGGETIFFGlobals.uBytesPerLine&~3); x+=4)
                {
                  *pDst = ((((pSrc0[3])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc1[3])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc2[3])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc3[3])&0xF0)>>4)*17);
                  pDst++;
              
                  *pDst = (((pSrc0[3])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc1[3])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc2[3])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc3[3])&0x0F)*17);
                  pDst++;
              

                  *pDst = ((((pSrc0[2])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc1[2])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc2[2])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc3[2])&0xF0)>>4)*17);
                  pDst++;
              
                  *pDst = (((pSrc0[2])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc1[2])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc2[2])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc3[2])&0x0F)*17);
                  pDst++;
              
                  *pDst = ((((pSrc0[1])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc1[1])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc2[1])&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((pSrc3[1])&0xF0)>>4)*17);
                  pDst++;
              
                  *pDst = (((pSrc0[1])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc1[1])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc2[1])&0x0F)*17);
                  pDst++;
                  *pDst = (((pSrc3[1])&0x0F)*17);
                  pDst++;


                  *pDst = ((((*pSrc0)&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((*pSrc1)&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((*pSrc2)&0xF0)>>4)*17);
                  pDst++;
                  *pDst = ((((*pSrc3)&0xF0)>>4)*17);
                  pDst++;
              
                  *pDst = (((*pSrc0)&0x0F)*17);
                  pDst++;
                  *pDst = (((*pSrc1)&0x0F)*17);
                  pDst++;
                  *pDst = (((*pSrc2)&0x0F)*17);
                  pDst++;
                  *pDst = (((*pSrc3)&0x0F)*17);
                  pDst++;
              
                  pSrc0+=4;
                  pSrc1+=4;
                  pSrc2+=4;
                  pSrc3+=4;
                }
              }

#ifdef GGETIFF_COMPRESS
              uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
              gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uCompressed;
              GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
              gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uLineLength;
              GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
    
            }
        }
        break;
    case 8:
        {
          /* todo implement swapbytesinword */
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
                pDst = pBufImage;
                for(x = 0; x < gtGGETIFFGlobals.uBytesPerLine; x++)
                {
                    *pDst = *pSrc0;
                    pDst++;
                    *pDst = *pSrc1;
                    pDst++;
                    *pDst = *pSrc2;
                    pDst++;
                    *pDst = *pSrc3;
                    pDst++;
                
                    pSrc0++;
                    pSrc1++;
                    pSrc2++;
                    pSrc3++;
                }
#ifdef GGETIFF_COMPRESS
                uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uCompressed;
                GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uLineLength;
                GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
    
            }
        }
        break;
    case 16:
        {
          /* todo implement swapbytesinword */
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
                pDst = pBufImage;
                for(x = 0; x < (gtGGETIFFGlobals.uBytesPerLine/2); x++)
                {
                    *pDst++ = *pSrc0;
                    *pDst++ = *(pSrc0+1);
                    *pDst++ = *pSrc1;
                    *pDst++ = *(pSrc1+1);
                    *pDst++ = *pSrc2;
                    *pDst++ = *(pSrc2+1);
                    *pDst++ = *pSrc3;
                    *pDst++ = *(pSrc3+1);
                
                    pSrc0 += 2;
                    pSrc1 += 2;
                    pSrc2 += 2;
                    pSrc3 += 2;
                }
#ifdef GGETIFF_COMPRESS
                uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uCompressed;
                GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uLineLength;
                GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
            }
        }
        break;
    }

    if(pBufImage)
        GGE_free(pBufImage);
   
    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Output composite RGB.
 *
 * All colorants will be written to the same file.
 *
 * \param ptGGE_TIFFBandComp Pointer to a TGGE_TIFF_BAND_COMPOSITE structure.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int OutputBandCompRGB(PTGGE_TIFF_BAND_COMPOSITE ptGGE_TIFFBandComp)
{
    unsigned int x, y;
    char *pSrc0;
    char *pSrc1;
    char *pSrc2;
    char *pDst;
    char *pBufImage;
    unsigned int uLineLength;
#ifdef GGETIFF_COMPRESS
    unsigned int uCompressed;
#endif

    if(!ptGGE_TIFFBandComp->paBandBuffer)
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }

    for(x = 0; x < ptGGE_TIFFBandComp->uColorants; x++)
    {
        if(!ptGGE_TIFFBandComp->paBandBuffer[x])
        {
            FreeTIFFResources();
            return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
        }
    }

    if(!GGETIFF_IS_COMP_OPEN())
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }

    pBufImage = NULL;

    switch(gtGGETIFFGlobals.uBitsPerPixel)
    {
    case 4:
        uLineLength = gtGGETIFFGlobals.uBytesPerLine * gtGGETIFFGlobals.uColorants * 2;
        pBufImage = (char*)GGE_malloc(uLineLength);
        break;
    case 8:
    case 16:
    default:
        uLineLength = gtGGETIFFGlobals.uBytesPerLine * gtGGETIFFGlobals.uColorants * 1;
        pBufImage = (char*)GGE_malloc(uLineLength);
        break;
    }

    if(!pBufImage)
    {
        HQFAIL("Failed to allocate memory for the band buffer.");
        FreeTIFFResources();
        return GGE_TIFF_ERROR_OUT_OF_MEMORY;
    }

    if((gtGGETIFFGlobals.uStrips != gtGGETIFFGlobals.uLinesPerPage) || (!gtGGETIFFGlobals.paStripLen))
    {
      if(gtGGETIFFGlobals.paStripLen)
      {
        GGE_free(gtGGETIFFGlobals.paStripLen);
      }
      gtGGETIFFGlobals.paStripLen = GGE_malloc(gtGGETIFFGlobals.uLinesPerPage * sizeof(unsigned int));
      if(!gtGGETIFFGlobals.paStripLen)
      {
          HQFAIL("Failed to allocate memory for strip lengths.");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uStrips = gtGGETIFFGlobals.uLinesPerPage;
    }

#ifdef GGETIFF_COMPRESS
    if(gtGGETIFFGlobals.uCompressLen != uLineLength)
    {
      if(gtGGETIFFGlobals.pCompressBuffer)
      {
        GGE_free(gtGGETIFFGlobals.pCompressBuffer);
      }

      gtGGETIFFGlobals.pCompressBuffer = GGE_malloc(((uLineLength * 128)+127)/127);
      if(!gtGGETIFFGlobals.pCompressBuffer)
      {
          HQFAIL("Failed to allocate memory for the compression buffer.");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uCompressLen = uLineLength;
    }
#endif

    pSrc0 = ptGGE_TIFFBandComp->paBandBuffer[0];
    pSrc1 = ptGGE_TIFFBandComp->paBandBuffer[1];
    pSrc2 = ptGGE_TIFFBandComp->paBandBuffer[2];

    switch(gtGGETIFFGlobals.uBitsPerPixel)
    {
    case 4:
        {
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
                pDst = pBufImage;
                for(x = 0; x < gtGGETIFFGlobals.uBytesPerLine; x++)
                {
                    *pDst = ((((*pSrc0)&0xF0)>>4)*17);
                    pDst++;
                    *pDst = ((((*pSrc1)&0xF0)>>4)*17);
                    pDst++;
                    *pDst = ((((*pSrc2)&0xF0)>>4)*17);
                    pDst++;
                
                    *pDst = (((*pSrc0)&0x0F)*17);
                    pDst++;
                    *pDst = (((*pSrc1)&0x0F)*17);
                    pDst++;
                    *pDst = (((*pSrc2)&0x0F)*17);
                    pDst++;
                
                    pSrc0++;
                    pSrc1++;
                    pSrc2++;
                }
            
#ifdef GGETIFF_COMPRESS
                uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uCompressed;
                GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uLineLength;
                GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
    
            }
        }
        break;
    case 16:
        {
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
                pDst = pBufImage;
                for(x = 0; x < (gtGGETIFFGlobals.uBytesPerLine/2); x++)
                {
                    *pDst++ = *pSrc0;
                    *pDst++ = *(pSrc0+1);
                    *pDst++ = *pSrc1;
                    *pDst++ = *(pSrc1+1);
                    *pDst++ = *pSrc2;
                    *pDst++ = *(pSrc2+1);
                
                    pSrc0 += 2;
                    pSrc1 += 2;
                    pSrc2 += 2;
                }
#ifdef GGETIFF_COMPRESS
                uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uCompressed;
                GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uLineLength;
                GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
    
            }
        }
        break;
     default:
    case 8:
        {
            for(y = 0; y < ptGGE_TIFFBandComp->uLinesThisBand; y++)
            {
                pDst = pBufImage;
                for(x = 0; x < gtGGETIFFGlobals.uBytesPerLine; x++)
                {
                    *pDst = *pSrc0;
                    pDst++;
                    *pDst = *pSrc1;
                    pDst++;
                    *pDst = *pSrc2;
                    pDst++;
                
                    pSrc0++;
                    pSrc1++;
                    pSrc2++;
                }
#ifdef GGETIFF_COMPRESS
                uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uCompressed;
                GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandComp->uLinesThisBand] = uLineLength;
                GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
    
            }
        }
        break;
    }

    if(pBufImage)
    {
        GGE_free(pBufImage);
    }
   
    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Output palette-color format.
 *
 * CMYK1bpp and CMYK2bpp will be converted to a 16 of 256 color palette.
 *
 * \param ptGGE_TIFFBandRGBPalette Pointer to a TGGE_TIFF_BAND_RGB_PALETTE structure.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int OutputBandRGBPal(PTGGE_TIFF_BAND_RGB_PALETTE ptGGE_TIFFBandRGBPalette)
{
    unsigned int x, y;
    char *pSrc0;
    char *pSrc1;
    char *pSrc2;
    char *pSrc3;
    char *pDst;
    char *pBufImage;
    unsigned int uLineLength;
#ifdef GGETIFF_COMPRESS
    unsigned int uCompressed;
#endif

    if(!ptGGE_TIFFBandRGBPalette->paBandBuffer)
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }

    for(x = 0; x < ptGGE_TIFFBandRGBPalette->uColorants; x++)
    {
        if(!ptGGE_TIFFBandRGBPalette->paBandBuffer[x])
        {
            FreeTIFFResources();
            return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
        }
    }

    if(!GGETIFF_IS_COMP_OPEN())
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }

    pBufImage = NULL;

    switch(gtGGETIFFGlobals.uBitsPerPixel)
    {
    default:
    case 1:
        uLineLength = gtGGETIFFGlobals.uBytesPerLine * 4;
        pBufImage = (char*)GGE_malloc(uLineLength);
        break;
    }

    if(!pBufImage)
    {
        HQFAIL("Failed to allocate memory for the band buffer.");
        FreeTIFFResources();
        return GGE_TIFF_ERROR_OUT_OF_MEMORY;
    }

    if((gtGGETIFFGlobals.uStrips != gtGGETIFFGlobals.uLinesPerPage) || (!gtGGETIFFGlobals.paStripLen))
    {
      if(gtGGETIFFGlobals.paStripLen)
      {
        GGE_free(gtGGETIFFGlobals.paStripLen);
      }
      gtGGETIFFGlobals.paStripLen = GGE_malloc(gtGGETIFFGlobals.uLinesPerPage * sizeof(unsigned int));
      if(!gtGGETIFFGlobals.paStripLen)
      {
          HQFAIL("Failed to allocate memory for strip lengths.");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uStrips = gtGGETIFFGlobals.uLinesPerPage;
    }

#ifdef GGETIFF_COMPRESS
    if(gtGGETIFFGlobals.uCompressLen != uLineLength)
    {
      if(gtGGETIFFGlobals.pCompressBuffer)
      {
        GGE_free(gtGGETIFFGlobals.pCompressBuffer);
      }

      gtGGETIFFGlobals.pCompressBuffer = GGE_malloc(((uLineLength * 128)+127)/127);
      if(!gtGGETIFFGlobals.pCompressBuffer)
      {
          HQFAIL("Failed to allocate memory for the compression buffer.");
          FreeTIFFResources();
          return GGE_TIFF_ERROR_OUT_OF_MEMORY;
      }
      gtGGETIFFGlobals.uCompressLen = uLineLength;
    }
#endif

    pSrc0 = ptGGE_TIFFBandRGBPalette->paBandBuffer[0];
    pSrc1 = ptGGE_TIFFBandRGBPalette->paBandBuffer[1];
    pSrc2 = ptGGE_TIFFBandRGBPalette->paBandBuffer[2];
    pSrc3 = ptGGE_TIFFBandRGBPalette->paBandBuffer[3];

    switch(gtGGETIFFGlobals.uBitsPerPixel)
    {
    case 1:
        {        
            /* RGB palette... converts 1bpp cmyk into and RGB palette that contains 16 colors.

                  CMYK  pal    CMYK  pal    CMYK  pal    CMYK  pal
                  0000  0      0100  4      1000  8      0100  12
                  0001  1      0101  5      1001  9      0101  13
                  0010  2      0110  6      1010  10     0110  14
                  0011  3      0111  7      1011  11     0111  15

            Every odd index in the table above has K=100%, which means the RGB color will be 0,0,0=Black
            however we keep the each 'black' color index unique so that it is possible to reverse the
            RGB palette TIFF image back to CMYK 1bpp, if required.

            High byte first systems
              1bpp is straight forward, pixels are left to right.

            Low byte first systems
              1bpp pixels are stored in each byte...
                  Byte 1: pixels 25-32;
                  Byte 2: pixels 17-24;
                  Byte 3: pixels 9-16;
                  Byte 4: pixels 1-8;
                  Byte 5: pixels 57-64;
                  Byte 6: pixels 49-56;
                  Byte 7: pixels 41-48;
                  Byte 8: pixels 33-40;
                  ... and so on.

              So we need to read 4th byte then 3rd, 2nd, 1st, then 8th, 7th, 6th, 5th and on.
            */
            /* Straight forward left to right */
            for(y = 0; y < ptGGE_TIFFBandRGBPalette->uLinesThisBand; y++)
            {
                pDst = pBufImage;
                /* Not required when pixel perfect, but useful when debugging stride lengths 
                #  pSrc0 = ptGGE_TIFFBandRGBPalette->paBandBuffer[0] + (y*gtGGETIFFGlobals.uBytesPerLine);
                #  pSrc1 = ptGGE_TIFFBandRGBPalette->paBandBuffer[1] + (y*gtGGETIFFGlobals.uBytesPerLine);
                #  pSrc2 = ptGGE_TIFFBandRGBPalette->paBandBuffer[2] + (y*gtGGETIFFGlobals.uBytesPerLine);
                #  pSrc3 = ptGGE_TIFFBandRGBPalette->paBandBuffer[3] + (y*gtGGETIFFGlobals.uBytesPerLine);
                */
                if(!gtGGETIFFGlobals.bSwapBytesInWord)
                {
                  for(x = 0; x < gtGGETIFFGlobals.uBytesPerLine; x++)
                  {
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x80)>>7), (((*pSrc1)&0x80)>>7), (((*pSrc2)&0x80)>>7), (((*pSrc3)&0x80)>>7) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x40)>>6), (((*pSrc1)&0x40)>>6), (((*pSrc2)&0x40)>>6), (((*pSrc3)&0x40)>>6) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x20)>>5), (((*pSrc1)&0x20)>>5), (((*pSrc2)&0x20)>>5), (((*pSrc3)&0x20)>>5) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x10)>>4), (((*pSrc1)&0x10)>>4), (((*pSrc2)&0x10)>>4), (((*pSrc3)&0x10)>>4) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x08)>>3), (((*pSrc1)&0x08)>>3), (((*pSrc2)&0x08)>>3), (((*pSrc3)&0x08)>>3) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x04)>>2), (((*pSrc1)&0x04)>>2), (((*pSrc2)&0x04)>>2), (((*pSrc3)&0x04)>>2) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x02)>>1), (((*pSrc1)&0x02)>>1), (((*pSrc2)&0x02)>>1), (((*pSrc3)&0x02)>>1) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x01)>>0), (((*pSrc1)&0x01)>>0), (((*pSrc2)&0x01)>>0), (((*pSrc3)&0x01)>>0) ) );
                      /* pixels reversed in a byte
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x01)>>0), (((*pSrc1)&0x01)>>0), (((*pSrc2)&0x01)>>0), (((*pSrc3)&0x01)>>0) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x02)>>1), (((*pSrc1)&0x02)>>1), (((*pSrc2)&0x02)>>1), (((*pSrc3)&0x02)>>1) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x04)>>2), (((*pSrc1)&0x04)>>2), (((*pSrc2)&0x04)>>2), (((*pSrc3)&0x04)>>2) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x08)>>3), (((*pSrc1)&0x08)>>3), (((*pSrc2)&0x08)>>3), (((*pSrc3)&0x08)>>3) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x10)>>4), (((*pSrc1)&0x10)>>4), (((*pSrc2)&0x10)>>4), (((*pSrc3)&0x10)>>4) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x20)>>5), (((*pSrc1)&0x20)>>5), (((*pSrc2)&0x20)>>5), (((*pSrc3)&0x20)>>5) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x40)>>6), (((*pSrc1)&0x40)>>6), (((*pSrc2)&0x40)>>6), (((*pSrc3)&0x40)>>6) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x80)>>7), (((*pSrc1)&0x80)>>7), (((*pSrc2)&0x80)>>7), (((*pSrc3)&0x80)>>7) ) );
                                  */

                      pSrc0++;
                      pSrc1++;
                      pSrc2++;
                      pSrc3++;
                  } /* for x */
                }
                else
                {
                  for(x = 0; x < (gtGGETIFFGlobals.uBytesPerLine&~3); x+=4)
                  {
                      /* Read 4th byte, then 3rd, 2nd and 1st, but only if the line is long enough. */
                      pSrc0+=3;
                      pSrc1+=3;
                      pSrc2+=3;
                      pSrc3+=3;
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x80)>>7), (((*pSrc1)&0x80)>>7), (((*pSrc2)&0x80)>>7), (((*pSrc3)&0x80)>>7) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x40)>>6), (((*pSrc1)&0x40)>>6), (((*pSrc2)&0x40)>>6), (((*pSrc3)&0x40)>>6) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x20)>>5), (((*pSrc1)&0x20)>>5), (((*pSrc2)&0x20)>>5), (((*pSrc3)&0x20)>>5) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x10)>>4), (((*pSrc1)&0x10)>>4), (((*pSrc2)&0x10)>>4), (((*pSrc3)&0x10)>>4) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x08)>>3), (((*pSrc1)&0x08)>>3), (((*pSrc2)&0x08)>>3), (((*pSrc3)&0x08)>>3) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x04)>>2), (((*pSrc1)&0x04)>>2), (((*pSrc2)&0x04)>>2), (((*pSrc3)&0x04)>>2) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x02)>>1), (((*pSrc1)&0x02)>>1), (((*pSrc2)&0x02)>>1), (((*pSrc3)&0x02)>>1) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x01)>>0), (((*pSrc1)&0x01)>>0), (((*pSrc2)&0x01)>>0), (((*pSrc3)&0x01)>>0) ) );

                      pSrc0--;
                      pSrc1--;
                      pSrc2--;
                      pSrc3--;
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x80)>>7), (((*pSrc1)&0x80)>>7), (((*pSrc2)&0x80)>>7), (((*pSrc3)&0x80)>>7) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x40)>>6), (((*pSrc1)&0x40)>>6), (((*pSrc2)&0x40)>>6), (((*pSrc3)&0x40)>>6) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x20)>>5), (((*pSrc1)&0x20)>>5), (((*pSrc2)&0x20)>>5), (((*pSrc3)&0x20)>>5) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x10)>>4), (((*pSrc1)&0x10)>>4), (((*pSrc2)&0x10)>>4), (((*pSrc3)&0x10)>>4) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x08)>>3), (((*pSrc1)&0x08)>>3), (((*pSrc2)&0x08)>>3), (((*pSrc3)&0x08)>>3) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x04)>>2), (((*pSrc1)&0x04)>>2), (((*pSrc2)&0x04)>>2), (((*pSrc3)&0x04)>>2) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x02)>>1), (((*pSrc1)&0x02)>>1), (((*pSrc2)&0x02)>>1), (((*pSrc3)&0x02)>>1) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x01)>>0), (((*pSrc1)&0x01)>>0), (((*pSrc2)&0x01)>>0), (((*pSrc3)&0x01)>>0) ) );

                      pSrc0--;
                      pSrc1--;
                      pSrc2--;
                      pSrc3--;
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x80)>>7), (((*pSrc1)&0x80)>>7), (((*pSrc2)&0x80)>>7), (((*pSrc3)&0x80)>>7) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x40)>>6), (((*pSrc1)&0x40)>>6), (((*pSrc2)&0x40)>>6), (((*pSrc3)&0x40)>>6) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x20)>>5), (((*pSrc1)&0x20)>>5), (((*pSrc2)&0x20)>>5), (((*pSrc3)&0x20)>>5) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x10)>>4), (((*pSrc1)&0x10)>>4), (((*pSrc2)&0x10)>>4), (((*pSrc3)&0x10)>>4) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x08)>>3), (((*pSrc1)&0x08)>>3), (((*pSrc2)&0x08)>>3), (((*pSrc3)&0x08)>>3) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x04)>>2), (((*pSrc1)&0x04)>>2), (((*pSrc2)&0x04)>>2), (((*pSrc3)&0x04)>>2) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x02)>>1), (((*pSrc1)&0x02)>>1), (((*pSrc2)&0x02)>>1), (((*pSrc3)&0x02)>>1) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x01)>>0), (((*pSrc1)&0x01)>>0), (((*pSrc2)&0x01)>>0), (((*pSrc3)&0x01)>>0) ) );

                      pSrc0--;
                      pSrc1--;
                      pSrc2--;
                      pSrc3--;
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x80)>>7), (((*pSrc1)&0x80)>>7), (((*pSrc2)&0x80)>>7), (((*pSrc3)&0x80)>>7) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x40)>>6), (((*pSrc1)&0x40)>>6), (((*pSrc2)&0x40)>>6), (((*pSrc3)&0x40)>>6) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x20)>>5), (((*pSrc1)&0x20)>>5), (((*pSrc2)&0x20)>>5), (((*pSrc3)&0x20)>>5) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x10)>>4), (((*pSrc1)&0x10)>>4), (((*pSrc2)&0x10)>>4), (((*pSrc3)&0x10)>>4) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x08)>>3), (((*pSrc1)&0x08)>>3), (((*pSrc2)&0x08)>>3), (((*pSrc3)&0x08)>>3) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x04)>>2), (((*pSrc1)&0x04)>>2), (((*pSrc2)&0x04)>>2), (((*pSrc3)&0x04)>>2) ) );
                      *pDst++ = ((GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x02)>>1), (((*pSrc1)&0x02)>>1), (((*pSrc2)&0x02)>>1), (((*pSrc3)&0x02)>>1) ) << 4 )|
                                  GGE_TIFF_PALETTE_1BPP_INDEX( (((*pSrc0)&0x01)>>0), (((*pSrc1)&0x01)>>0), (((*pSrc2)&0x01)>>0), (((*pSrc3)&0x01)>>0) ) );

                      /* move on to next set of 4 bytes */
                      pSrc0+=4;
                      pSrc1+=4;
                      pSrc2+=4;
                      pSrc3+=4;
                  } /* for x */
                }
#ifdef GGETIFF_COMPRESS
                uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandRGBPalette->uLinesThisBand] = uCompressed;
                GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandRGBPalette->uLinesThisBand] = uLineLength;
                GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
            } /* for y */
        }
        break;
    case 2:
        {        
            for(y = 0; y < ptGGE_TIFFBandRGBPalette->uLinesThisBand; y++)
            {
                pDst = pBufImage;
                if(gtGGETIFFGlobals.bSwapBytesInWord)
                {
                  for(x = 0; x < (gtGGETIFFGlobals.uBytesPerLine&~3); x+=4)
                  {
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[3])&0xC0)>>6), (((pSrc1[3])&0xC0)>>6), (((pSrc2[3])&0xC0)>>6), (((pSrc3[3])&0xC0)>>6) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[3])&0x30)>>4), (((pSrc1[3])&0x30)>>4), (((pSrc2[3])&0x30)>>4), (((pSrc3[3])&0x30)>>4) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[3])&0x0C)>>2), (((pSrc1[3])&0x0C)>>2), (((pSrc2[3])&0x0C)>>2), (((pSrc3[3])&0x0C)>>2) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[3])&0x03)>>0), (((pSrc1[3])&0x03)>>0), (((pSrc2[3])&0x03)>>0), (((pSrc3[3])&0x03)>>0) ) ;

                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[2])&0xC0)>>6), (((pSrc1[2])&0xC0)>>6), (((pSrc2[2])&0xC0)>>6), (((pSrc3[2])&0xC0)>>6) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[2])&0x30)>>4), (((pSrc1[2])&0x30)>>4), (((pSrc2[2])&0x30)>>4), (((pSrc3[2])&0x30)>>4) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[2])&0x0C)>>2), (((pSrc1[2])&0x0C)>>2), (((pSrc2[2])&0x0C)>>2), (((pSrc3[2])&0x0C)>>2) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[2])&0x03)>>0), (((pSrc1[2])&0x03)>>0), (((pSrc2[2])&0x03)>>0), (((pSrc3[2])&0x03)>>0) ) ;

                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[1])&0xC0)>>6), (((pSrc1[1])&0xC0)>>6), (((pSrc2[1])&0xC0)>>6), (((pSrc3[1])&0xC0)>>6) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[1])&0x30)>>4), (((pSrc1[1])&0x30)>>4), (((pSrc2[1])&0x30)>>4), (((pSrc3[1])&0x30)>>4) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[1])&0x0C)>>2), (((pSrc1[1])&0x0C)>>2), (((pSrc2[1])&0x0C)>>2), (((pSrc3[1])&0x0C)>>2) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[1])&0x03)>>0), (((pSrc1[1])&0x03)>>0), (((pSrc2[1])&0x03)>>0), (((pSrc3[1])&0x03)>>0) ) ;

                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[0])&0xC0)>>6), (((pSrc1[0])&0xC0)>>6), (((pSrc2[0])&0xC0)>>6), (((pSrc3[0])&0xC0)>>6) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[0])&0x30)>>4), (((pSrc1[0])&0x30)>>4), (((pSrc2[0])&0x30)>>4), (((pSrc3[0])&0x30)>>4) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[0])&0x0C)>>2), (((pSrc1[0])&0x0C)>>2), (((pSrc2[0])&0x0C)>>2), (((pSrc3[0])&0x0C)>>2) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((pSrc0[0])&0x03)>>0), (((pSrc1[0])&0x03)>>0), (((pSrc2[0])&0x03)>>0), (((pSrc3[0])&0x03)>>0) ) ;

                      pSrc0+=4;
                      pSrc1+=4;
                      pSrc2+=4;
                      pSrc3+=4;
                  }
                }
                else
                {
                  for(x = 0; x < gtGGETIFFGlobals.uBytesPerLine; x++)
                  {
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((*pSrc0)&0xC0)>>6), (((*pSrc1)&0xC0)>>6), (((*pSrc2)&0xC0)>>6), (((*pSrc3)&0xC0)>>6) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((*pSrc0)&0x30)>>4), (((*pSrc1)&0x30)>>4), (((*pSrc2)&0x30)>>4), (((*pSrc3)&0x30)>>4) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((*pSrc0)&0x0C)>>2), (((*pSrc1)&0x0C)>>2), (((*pSrc2)&0x0C)>>2), (((*pSrc3)&0x0C)>>2) ) ;
                      *pDst++ = GGE_TIFF_PALETTE_2BPP_INDEX( (((*pSrc0)&0x03)>>0), (((*pSrc1)&0x03)>>0), (((*pSrc2)&0x03)>>0), (((*pSrc3)&0x03)>>0) ) ;

                      pSrc0++;
                      pSrc1++;
                      pSrc2++;
                      pSrc3++;
                  }
                }
            
#ifdef GGETIFF_COMPRESS
                uCompressed = packD8(gtGGETIFFGlobals.pCompressBuffer, (unsigned char*)pBufImage, uLineLength);
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandRGBPalette->uLinesThisBand] = uCompressed;
                GGETIFF_WriteOutput( 0, gtGGETIFFGlobals.pCompressBuffer, uCompressed );
#else
                gtGGETIFFGlobals.paStripLen[y + gtGGETIFFGlobals.uLineCount - ptGGE_TIFFBandRGBPalette->uLinesThisBand] = uLineLength;
                GGETIFF_WriteOutput( 0, pBufImage, uLineLength );
#endif
            }
        }
        break;
      default:
        /* No RGB palette support for other bit depths */
        break;
    }

    if(pBufImage)
        GGE_free(pBufImage);
   
    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Output TIFF 8-byte image header.
 *
 * Specifies that the output file is TIFF format, endian-ness, and
 * location of the TIFF IFD.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int OutputTIFFHeader()
{
    unsigned int i;
    unsigned char ch;
    unsigned int u;
    short s;
    
    if(gtGGETIFFGlobals.bSeparated)
    {
        for(i = 0; i < gtGGETIFFGlobals.uColorants; i++)
        {
            if(GGETIFF_IS_SEP_OPEN(i))
            {
#ifdef highbytefirst
                ch='M';
#else
                ch='I';
#endif
                GGETIFF_WriteOutput(i, &ch, 1);
                GGETIFF_WriteOutput(i, &ch, 1);
            
            
                s=42;
                GGETIFF_WriteOutput(i, (unsigned char*)&s, sizeof(s));
            
            
                u=0; /*  IFD POS - NOTE MUST PUT THE CORRECT VALUE AFTER DATA HAS BEEN WRITTEN */
                GGETIFF_WriteOutput(i, (unsigned char*)&u, sizeof(u));
            }
            else
            {
                FreeTIFFResources();
                return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
            }
        }
    }
    else
    {
        if(GGETIFF_IS_COMP_OPEN())
        {
#ifdef highbytefirst
            ch='M';
#else
            ch='I';
#endif
            GGETIFF_WriteOutput(0, &ch, 1);
            GGETIFF_WriteOutput(0, &ch, 1);
        
        
            s=42;
            GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
        
        
            u=0; /*  IFD POS - NOTE MUST PUT THE CORRECT VALUE AFTER DATA HAS BEEN WRITTEN */
            GGETIFF_WriteOutput(0, (unsigned char*)&u, sizeof(u));
        }
        else
        {
            FreeTIFFResources();
            return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
        }
    }

    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Output TIFF IFD.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int OutputImageHeader()
{
    if(gtGGETIFFGlobals.bSeparated)
        return UpdateMono();
    else if(gtGGETIFFGlobals.uColorants == 3)
    {
        return UpdateRGB();
    }
    else
    {
        if(gtGGETIFFGlobals.bRGBPal)
            return UpdateRGBPal();
        else
            return UpdateCMYK();
    }
}


/**
 * \brief Update TIFF IFD for mono output.
 *
 * Once the raster has been output, the TIFF IFD can be output.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int UpdateMono()
{   
    TIFFTAG tag;
    TIFFRATIONAL f;
    short s;
    unsigned int u;
    unsigned char szSoftware[]="GGEtiff ";
    unsigned int i, y;
    unsigned int uOffset, uValue;
    unsigned int uImageWidth = gtGGETIFFGlobals.uWidthPixels;
    unsigned int uDescriptionLen;

    if(gtGGETIFFGlobals.pszImageDescription)
        uDescriptionLen = (unsigned int)strlen(gtGGETIFFGlobals.pszImageDescription);
    else
        uDescriptionLen = 0;

    for(i = 0; i < gtGGETIFFGlobals.uColorants; i++)
    {
        if(!GGETIFF_IS_SEP_OPEN(i))
            continue;

        uOffset = gtGGETIFFGlobals.ulPos[i];

        /* tag count */
        s=14;
        GGETIFF_WriteOutput(i, (unsigned char*)&s, sizeof(s));
        
        
        /* NewSubfileType */
        tag.tag = 0xFE;
        tag.type = TIFF_TYPE_LONG;
        tag.count = 1;
        tag.value = 0;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* ImageWidth */
        tag.tag = 256;
        tag.type = TIFF_TYPE_LONG;
        tag.count = 1;
        tag.value = uImageWidth;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* ImageLength */
        tag.tag = 257;
        tag.type = TIFF_TYPE_LONG;
        tag.count = 1;
        if(gtGGETIFFGlobals.uLinesPerPage == 0)
        {
            tag.value = gtGGETIFFGlobals.uLineCount / 4;
        }
        else
        {
            tag.value = gtGGETIFFGlobals.uLinesPerPage;
        }
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        /* BitsPerSample */
        tag.tag = 258;
        tag.type = TIFF_TYPE_SHORT;
        tag.count = 1;
        tag.value = OUTPUT_SHORT(gtGGETIFFGlobals.uBitsPerPixel);
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        /* Compression */
        tag.tag = 259;
        tag.type = TIFF_TYPE_SHORT;
        tag.count = 1;
#ifdef GGETIFF_COMPRESS
        tag.value = OUTPUT_SHORT(32773);
#else
        tag.value = OUTPUT_SHORT(1);
#endif
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* Photometric Interpretation */
        tag.tag = 262;
        tag.type = TIFF_TYPE_SHORT;
        tag.count = 1;
        tag.value = 0;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        /* Image Description */
        tag.tag = 270;
        tag.type = TIFF_TYPE_ASCII;
        tag.count = uDescriptionLen;
        tag.value = 198 + uOffset;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));

        /* StripsOffsets+ */
        tag.tag = 273;
        tag.type = TIFF_TYPE_LONG;
        tag.count = gtGGETIFFGlobals.uStrips;
        tag.value = 198 + uOffset + uDescriptionLen;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* RowsPerStrip */
        tag.tag = 278;
        tag.type = TIFF_TYPE_LONG;
        tag.count = 1;
        tag.value = 1;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* StripByteCounts+ */
        tag.tag = 279;
        tag.type = TIFF_TYPE_LONG;
        tag.count = gtGGETIFFGlobals.uStrips;
        tag.value = 198 + uOffset + uDescriptionLen + (gtGGETIFFGlobals.uStrips * 4);
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* Xres+ */
        tag.tag = 282;
        tag.type = TIFF_TYPE_RATIONAL;
        tag.count = 1;
        tag.value = 174 + uOffset;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* Yres+ */
        tag.tag = 283;
        tag.type = TIFF_TYPE_RATIONAL;
        tag.count = 1;
        tag.value = 182 + uOffset;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* Res Unit */
        tag.tag = 296;
        tag.type = TIFF_TYPE_SHORT;
        tag.count = 1;
        tag.value = OUTPUT_SHORT(2);
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* Software+ */
        tag.tag = 0x0131;
        tag.type = TIFF_TYPE_ASCII;
        tag.count = 8;
        tag.value = 190 + uOffset;
        GGETIFF_WriteOutput(i, (unsigned char*)&tag, sizeof(tag));
        
        
        /* Date */
     /* tag.tag = 0x0132;
        tag.type = TIFF_TYPE_ASCII;
        tag.count = 20;
        tag.value = 198 + uOffset;
        GGETIFF_WriteOutput((char*)&tag, 1, sizeof(tag), hFile); */
        

        /* null terminator */
        u = 0;
        GGETIFF_WriteOutput(i, (unsigned char*)&u, sizeof(u));
        
        /* x dpi */
        f.numer = gtGGETIFFGlobals.uXDPI;
        f.denom = 1;
        GGETIFF_WriteOutput(i, (unsigned char*)&f, sizeof(f));
        
        /* y dpi */
        f.numer = gtGGETIFFGlobals.uYDPI;
        f.denom = 1;
        GGETIFF_WriteOutput(i, (unsigned char*)&f, sizeof(f));
        
        /* software string */
        HQASSERT(strlen((char*)szSoftware)==8, "software string must be eight bytes");
        GGETIFF_WriteOutput(i, &szSoftware[0], 8);

        /* image description string */
        if(gtGGETIFFGlobals.pszImageDescription)
        {
            GGETIFF_WriteOutput(i, (unsigned char*)gtGGETIFFGlobals.pszImageDescription, uDescriptionLen );
        }

        /* data string */
        /* GGETIFF_WriteOutput(i, szDate, 1, 20, hFile); */

        /* strip offsets */
        uValue = 8;
        for(y=0; y<gtGGETIFFGlobals.uLinesPerPage; y++)
        {
          GGETIFF_WriteOutput(i, (unsigned char*)&uValue, 4);
          uValue += gtGGETIFFGlobals.paStripLen[y + (i * gtGGETIFFGlobals.uLinesPerPage)];
        }

        /* strip lengths */
        for(y=0; y<gtGGETIFFGlobals.uLinesPerPage; y++)
        {
          GGETIFF_WriteOutput(i, (unsigned char*)&gtGGETIFFGlobals.paStripLen[y + (i * gtGGETIFFGlobals.uLinesPerPage)], 4);
        }


        /* IFD Pos */
        u=uOffset; /* Update IFD Pos */
        GGETIFF_WriteOutputAtPos( i, (unsigned char*)&u, sizeof(u), 4);
    }

    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Update TIFF IFD for CMYK output.
 *
 * Once the raster has been output, the TIFF IFD can be output.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int UpdateCMYK()
{
    TIFFTAG tag;
    TIFFRATIONAL f;
    short s;
    unsigned int u, y;
    unsigned char szSoftware[]="GGEtiff ";
    unsigned int uOffset, uValue;
    unsigned int uDescriptionLen;

    if(!GGETIFF_IS_COMP_OPEN())
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }
    
    if(gtGGETIFFGlobals.pszImageDescription)
        uDescriptionLen = (unsigned int)strlen(gtGGETIFFGlobals.pszImageDescription);
    else
        uDescriptionLen = 0;

    uOffset = gtGGETIFFGlobals.ulPos[0];
    
    /* tag count */
    s=16;
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
    
    
    /* NewSubfileType */
    tag.tag = 0xFE;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = 0;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* ImageWidth */
    tag.tag = 256;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = gtGGETIFFGlobals.uWidthPixels;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* ImageLength */
    tag.tag = 257;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    if(gtGGETIFFGlobals.uLinesPerPage == 0)
    {
        tag.value = gtGGETIFFGlobals.uLineCount;
    }
    else
    {
        tag.value = gtGGETIFFGlobals.uLinesPerPage;
    }
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* BitsPerSample+ */
    tag.tag = 258;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 4;
    tag.value = 198 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Compression */
    tag.tag = 259;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
#ifdef GGETIFF_COMPRESS
    tag.value = OUTPUT_SHORT(32773);
#else
    tag.value = OUTPUT_SHORT(1);
#endif
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));

    
    /* Photometric Interpretation */
    tag.tag = 262;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(5); /* cmyk */
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    /* Image Description */
    tag.tag = 270;
    tag.type = TIFF_TYPE_ASCII;
    tag.count = uDescriptionLen;
    tag.value = 230 + uOffset; 
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));

    /* StripsOffsets+ */
    tag.tag = 273;
    tag.type = TIFF_TYPE_LONG;
    tag.count = gtGGETIFFGlobals.uStrips;
    tag.value = 230 + uOffset + uDescriptionLen;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    /* SamplesPerPixel */
    tag.tag = 277;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(4); /* Four colors... cmyk */
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));

    /* RowsPerStrip */
    tag.tag = 278;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* StripByteCounts+ */
    tag.tag = 279;
    tag.type = TIFF_TYPE_LONG;
    tag.count = gtGGETIFFGlobals.uStrips;
    tag.value = 230 + uOffset + uDescriptionLen + (gtGGETIFFGlobals.uStrips * 4);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Xres+ */
    tag.tag = 282;
    tag.type = TIFF_TYPE_RATIONAL;
    tag.count = 1;
    tag.value = 206 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Yres+ */
    tag.tag = 283;
    tag.type = TIFF_TYPE_RATIONAL;
    tag.count = 1;
    tag.value = 214 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Pixel interleaved */
    tag.tag = 284;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(1);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Res Unit */
    tag.tag = 296;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(2);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Software+ */
    tag.tag = 0x0131;
    tag.type = TIFF_TYPE_ASCII;
    tag.count = 8;
    tag.value = 222 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Date */
 /* tag.tag = 0x0132;
    tag.type = TIFF_TYPE_ASCII;
    tag.count = 20;
    tag.value = 198 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    */
    
    
    /* null terminator */
    u = 0;
    GGETIFF_WriteOutput(0, (unsigned char*)&u, sizeof(u));
    
    u = gtGGETIFFGlobals.ulPos[0] - uOffset;

    /* bits per pixel */
    /* All CMYK output is 8bpp or 16bpp */
    if(gtGGETIFFGlobals.uBitsPerPixel == 16)
    {
      s = 16;
    }
    else
    {
      s = 8;
    }
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));

    /* x dpi */
    f.numer = gtGGETIFFGlobals.uXDPI;
    f.denom = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&f, sizeof(f));
    
    /* y dpi */
    f.numer = gtGGETIFFGlobals.uYDPI;
    f.denom = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&f, sizeof(f));
    
    /* software string */
    HQASSERT(strlen((char*)szSoftware)==8, "software string must be eight bytes");
    GGETIFF_WriteOutput(0, &szSoftware[0], 8);
    
    /* image description string */
    if(gtGGETIFFGlobals.pszImageDescription)
    {
        GGETIFF_WriteOutput(0, (unsigned char*)gtGGETIFFGlobals.pszImageDescription, uDescriptionLen);
    }

    /* date string */
    /* HQASSERT(sizeof(szDate)==20, "date string must be twenty bytes");
    GGETIFF_WriteOutput(0, szDate, 20); */

    /* strip offsets */
    uValue = 8;
    for(y=0; y<gtGGETIFFGlobals.uLinesPerPage; y++)
    {
      GGETIFF_WriteOutput(0, (unsigned char*)&uValue, 4);
      uValue += gtGGETIFFGlobals.paStripLen[y];
    }

    /* strip lengths */
    for(y=0; y<gtGGETIFFGlobals.uLinesPerPage; y++)
    {
      GGETIFF_WriteOutput(0, (unsigned char*)&gtGGETIFFGlobals.paStripLen[y], 4);
    }

    /* IFD Pos */
    u=uOffset; /* Update IFD Pos */
    GGETIFF_WriteOutputAtPos( 0, (unsigned char*)&u, sizeof(u), 4);


    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Update TIFF IFD for RGB output.
 *
 * Once the raster has been output, the TIFF IFD can be output.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int UpdateRGB()
{
    TIFFTAG tag;
    TIFFRATIONAL f;
    short s;
    unsigned int u, y;
    unsigned char szSoftware[]="GGEtiff ";
    unsigned int uOffset, uValue;
    unsigned int uDescriptionLen;

    if(!GGETIFF_IS_COMP_OPEN())
    {
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }
    
    if(gtGGETIFFGlobals.pszImageDescription)
        uDescriptionLen = (unsigned int)strlen(gtGGETIFFGlobals.pszImageDescription);
    else
        uDescriptionLen = 0;

    uOffset = gtGGETIFFGlobals.ulPos[0];
    
    /* tag count */
    s=16;
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
    
    
    /* NewSubfileType */
    tag.tag = 0xFE;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = 0;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* ImageWidth */
    tag.tag = 256;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = gtGGETIFFGlobals.uWidthPixels;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* ImageLength */
    tag.tag = 257;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    if(gtGGETIFFGlobals.uLinesPerPage == 0)
    {
        tag.value = gtGGETIFFGlobals.uLineCount;
    }
    else
    {
        tag.value = gtGGETIFFGlobals.uLinesPerPage;
    }
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* BitsPerSample+ */
    tag.tag = 258;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 3;
    tag.value = 198 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Compression */
    tag.tag = 259;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
#ifdef GGETIFF_COMPRESS
    tag.value = OUTPUT_SHORT(32773);
#else
    tag.value = OUTPUT_SHORT(1);
#endif
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));

    
    /* Photometric Interpretation */
    tag.tag = 262;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(2); /* rgb */
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    /* Image Description */
    tag.tag = 270;
    tag.type = TIFF_TYPE_ASCII;
    tag.count = uDescriptionLen;
    tag.value = 228 + uOffset; 
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    /* StripsOffsets+ */
    tag.tag = 273;  
    tag.type = TIFF_TYPE_LONG;
    tag.count = gtGGETIFFGlobals.uStrips;
    tag.value = 228 + uOffset + uDescriptionLen;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    /* SamplesPerPixel */
    tag.tag = 277;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(3); /* rgb */
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));

    /* RowsPerStrip */
    tag.tag = 278;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* StripByteCounts+ */
    tag.tag = 279;
    tag.type = TIFF_TYPE_LONG;
    tag.count = gtGGETIFFGlobals.uStrips;
    tag.value = 228 + uOffset + uDescriptionLen + (gtGGETIFFGlobals.uStrips * 4);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Xres+ */
    tag.tag = 282;
    tag.type = TIFF_TYPE_RATIONAL;
    tag.count = 1;
    tag.value = 204 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Yres+ */
    tag.tag = 283;
    tag.type = TIFF_TYPE_RATIONAL;
    tag.count = 1;
    tag.value = 212 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Pixel interleaved */
    tag.tag = 284;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(1);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Res Unit */
    tag.tag = 296;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(2);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Software+ */
    tag.tag = 0x0131;
    tag.type = TIFF_TYPE_ASCII;
    tag.count = 8;
    tag.value = 220 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Date */
    /* tag.tag = 0x0132;
    tag.type = TIFF_TYPE_ASCII;
    tag.count = 20;
    tag.value = 198 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    */
    
    
    /* null terminator */
    u = 0;
    GGETIFF_WriteOutput(0, (unsigned char*)&u, sizeof(u));
    
    u = gtGGETIFFGlobals.ulPos[0] - uOffset;

    /* bits per pixel */
    s = (short)gtGGETIFFGlobals.uBitsPerPixel;
    /* All RGB output is 8bpp or 16bpp */
    if(gtGGETIFFGlobals.uBitsPerPixel == 16)
    {
      s = 16;
    }
    else
    {
      s = 8;
    }
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));

    /* x dpi */
    f.numer = gtGGETIFFGlobals.uXDPI;
    f.denom = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&f, sizeof(f));
    
    /* y dpi */
    f.numer = gtGGETIFFGlobals.uYDPI;
    f.denom = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&f, sizeof(f));
    
    /* software string */
    HQASSERT(strlen((char*)szSoftware)==8, "software string must be eight bytes");
    GGETIFF_WriteOutput(0, &szSoftware[0], 8);
    
    /* image description string */
    if(gtGGETIFFGlobals.pszImageDescription)
    {
        GGETIFF_WriteOutput(0, (unsigned char*)gtGGETIFFGlobals.pszImageDescription, uDescriptionLen);
    }

    /* data string */
    /* GGETIFF_WriteOutput(szDate, 1, 20, hFile); */

    /* strip offsets */
    uValue = 8;
    for(y=0; y<gtGGETIFFGlobals.uLinesPerPage; y++)
    {
      GGETIFF_WriteOutput(0, (unsigned char*)&uValue, 4);
      uValue += gtGGETIFFGlobals.paStripLen[y];
    }

    /* strip lengths */
    for(y=0; y<gtGGETIFFGlobals.uLinesPerPage; y++)
    {
      GGETIFF_WriteOutput(0, (unsigned char*)&gtGGETIFFGlobals.paStripLen[y], 4);
    }

    /* IFD Pos */
    u=uOffset; /* Update IFD Pos */
    GGETIFF_WriteOutputAtPos( 0, (unsigned char*)&u, sizeof(u), 4);

    return GGE_TIFF_SUCCESS;
}

/**
 * \brief Update TIFF IFD for palette-color output.
 *
 * Once the raster has been output, the TIFF IFD can be output.
 *
 * \return 0 if successful, otherwise a GGE TIFF error code.
 */
unsigned int UpdateRGBPal()
{
    TIFFTAG tag;
    TIFFRATIONAL f;
    short s;
    unsigned int u, y;
    unsigned char szSoftware[]="GGEtiff ";
    unsigned int uOffset, uValue;
    unsigned int uImageWidth = gtGGETIFFGlobals.uWidthPixels;
    unsigned int uDescriptionLen;


    if(!GGETIFF_IS_COMP_OPEN())
    {
        FreeTIFFResources();
        return GGE_TIFF_ERROR_INSEL_INVALID_PARAM;
    }
    
    if(gtGGETIFFGlobals.pszImageDescription)
        uDescriptionLen = (unsigned int)strlen(gtGGETIFFGlobals.pszImageDescription);
    else
        uDescriptionLen = 0;

    uOffset = gtGGETIFFGlobals.ulPos[0];
    
    /* tag count */
    s=15;
    GGETIFF_WriteOutput(0, (unsigned char*)&s, sizeof(s));
    
    
    /* NewSubfileType */
    tag.tag = 0xFE;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = 0;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* ImageWidth */
    tag.tag = 256;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = uImageWidth;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* ImageLength */
    tag.tag = 257;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    if(gtGGETIFFGlobals.uLinesPerPage == 0)
    {
        tag.value = gtGGETIFFGlobals.uLineCount;
    }
    else
    {
        tag.value = gtGGETIFFGlobals.uLinesPerPage;
    }
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* BitsPerSample */
    tag.tag = 258;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    if(gtGGETIFFGlobals.uBitsPerPixel==1)
        tag.value = OUTPUT_SHORT(4); /* rgb pal 4 = 16 colors, 8= 256 colors */
    else
        tag.value = OUTPUT_SHORT(8); /* rgb pal 4 = 16 colors, 8= 256 colors */
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Compression */
    tag.tag = 259;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
#ifdef GGETIFF_COMPRESS
    tag.value = OUTPUT_SHORT(32773);
#else
    tag.value = OUTPUT_SHORT(1);
#endif
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));

    
    /* Photometric Interpretation */
    tag.tag = 262;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(3); /* rgb pal */
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
   
    
    /* Image Description */
    tag.tag = 270;
    tag.type = TIFF_TYPE_ASCII;
    tag.count = uDescriptionLen;
    tag.value = 210 + uOffset; /* rgb pal */
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));

   
    /* StripsOffsets+ */
    tag.tag = 273;
    tag.type = TIFF_TYPE_LONG;
    tag.count = gtGGETIFFGlobals.uStrips;
    tag.value = 210 + uOffset + uDescriptionLen;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));


    /* RowsPerStrip */
    tag.tag = 278;
    tag.type = TIFF_TYPE_LONG;
    tag.count = 1;
    tag.value = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* StripByteCounts+ */
    tag.tag = 279;
    tag.type = TIFF_TYPE_LONG;
    /* 2 colors id per byte for 1bpp */
    /* 1 color id per byte for 2bpp */
    tag.count = gtGGETIFFGlobals.uStrips;
    tag.value = 210 + uOffset + uDescriptionLen + (gtGGETIFFGlobals.uLinesPerPage * 4);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Xres+ */
    tag.tag = 282;
    tag.type = TIFF_TYPE_RATIONAL;
    tag.count = 1;
    tag.value = 186 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Yres+ */
    tag.tag = 283;
    tag.type = TIFF_TYPE_RATIONAL;
    tag.count = 1;
    tag.value = 194 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Res Unit */
    tag.tag = 296;
    tag.type = TIFF_TYPE_SHORT;
    tag.count = 1;
    tag.value = OUTPUT_SHORT(2);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Software+ */
    tag.tag = 0x0131; /* 305 */
    tag.type = TIFF_TYPE_ASCII;
    tag.count = 8;
    tag.value = 202 + uOffset;
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    

    /* Color map */
    tag.tag = 320;
    tag.type = TIFF_TYPE_SHORT;
    if(gtGGETIFFGlobals.uBitsPerPixel==1)
        tag.count = 3*16;
    else if(gtGGETIFFGlobals.uBitsPerPixel==2)
        tag.count = 3*256;
    tag.value = 210 + uOffset + uDescriptionLen + (gtGGETIFFGlobals.uLinesPerPage * 4 * 2);
    GGETIFF_WriteOutput(0, (unsigned char*)&tag, sizeof(tag));
    
    
    /* Date */
    /* tag.tag = 0x0132;
     tag.type = TIFF_TYPE_ASCII;
     tag.count = 20;
     tag.value = 198 + uOffset;
     GGETIFF_WriteOutput((char*)&tag, 1, sizeof(tag), hFile); */
    
    
    /* null terminator */
    u = 0;
    GGETIFF_WriteOutput(0, (unsigned char*)&u, sizeof(u));
    
    u = gtGGETIFFGlobals.ulPos[0] - uOffset;

    /* x dpi */
    f.numer = gtGGETIFFGlobals.uXDPI;
    f.denom = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&f, sizeof(f));
    
    /* y dpi */
    f.numer = gtGGETIFFGlobals.uYDPI;
    f.denom = 1;
    GGETIFF_WriteOutput(0, (unsigned char*)&f, sizeof(f));
    
    /* software string */
    HQASSERT(strlen((char*)szSoftware)==8, "software string must be eight bytes");
    GGETIFF_WriteOutput(0, &szSoftware[0], 8);

    /* image description string */
    if(gtGGETIFFGlobals.pszImageDescription)
    {
        GGETIFF_WriteOutput(0, (unsigned char*)gtGGETIFFGlobals.pszImageDescription, uDescriptionLen);
    }

    /* strip offsets */
    uValue = 8;
    for(y=0; y<gtGGETIFFGlobals.uLinesPerPage; y++)
    {
      GGETIFF_WriteOutput(0, (unsigned char*)&uValue, 4);
      uValue += gtGGETIFFGlobals.paStripLen[y];
    }

    /* strip lengths */
    for(y=0; y<gtGGETIFFGlobals.uLinesPerPage; y++)
    {
      GGETIFF_WriteOutput(0, (unsigned char*)&gtGGETIFFGlobals.paStripLen[y], 4);
    }

    /* color map */
    if(gtGGETIFFGlobals.uBitsPerPixel==1)
    {
        GGETIFF_WriteOutput(0, (unsigned char*)&gPalette1bpp, 16*3*sizeof(short));
    }
    else if(gtGGETIFFGlobals.uBitsPerPixel==2)
    {
        GGETIFF_WriteOutput(0, (unsigned char*)&gPalette2bpp, 256*3*sizeof(short));
    }

    /* date string */
    /* GGASSERT(sizeof(szDate)==20, "date string must be twenty bytes");
    GGETIFF_WriteOutput(0, szDate, 20); */
    
    /* IFD Pos */
    u=uOffset; /* Update IFD Pos */
    GGETIFF_WriteOutputAtPos( 0, (unsigned char*)&u, sizeof(u), 4);


    return GGE_TIFF_SUCCESS;
}

 
