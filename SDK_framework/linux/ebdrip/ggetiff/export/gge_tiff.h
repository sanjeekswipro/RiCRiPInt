/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: GGEtiff!export:gge_tiff.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/*! \mainpage

  <b>Overview of the TIFF utility</b>

  The TIFF utility can be used to convert raw raster data from a RIP 
  module into a composite or separated TIFF files.

  Typical use of the utility involves initializing it and creating a 
  TIFF header by calling the single TIFF function (GGE_TIFF()) with 
  the ::GGE_TIFF_INSEL_HEADER input selector and the associated data 
  structure. 

  Either the GGE_TIFF_BAND_COMPOSITE or the GGE_TIFF_BAND_SEPARATED 
  input selector and its associated data structure are then used to 
  pass data to the TIFF utility to be output to a composite or 
  separated TIFF files.

  Finally, the GGE_TIFF_INSEL_CLOSE input selector is called to close
  the resultant TIFF file(s) and release any allocated memory.  


  <b>How to use this document</b>

  You should refer to the File List section for an overview of the 
  library file (gge_tiff.h) for the TIFF utility, including the 
  available input selectors.  You should then refer to the Data 
  Structures section for an overview of the associated data structures.

  The Data Fields and Globals section of this document provides various
  indexes allowing you to quickly locate documentation for a particular
  part of the TIFF API.

  Note: This document is designed for online viewing, with lots of
  hyperlinks (blue text) allowing you to quickly jump to related 
  information. We do not recommend that you print this document.

 */

/*! \file 
 *  \brief Defines the TIFF API.
 *
 * Copyright (c) 2004-2005 Global Graphics Software Ltd.
 *
 * Include this file to use the TIFF function. The parameters for this 
 * function includes:
 * 
 * 1.  An input selector, that defines the functionality required by 
 *     the function call. 
 * 
 * 2.  A data structure which stores data to be passed into and out from 
 *     the function call.
 * 
 * <b>Input selectors</b>
 * 
 * The following input selectors are available when using the TIFF 
 * function:
 * 
 * VERSION - Retrieves version information for the TIFF utility. 
 * 
 * SELECTOR_SUPPORTED - Allows you to check whether an input selector
 *                      is supported without calling it.
 * 
 * HEADER - Initializes the TIFF utility and passes it data for a TIFF 
 *          header.
 * 
 * BAND_SEPARATED - Passes band data to the TIFF utility to be output
 *                  as separated TIFF files.
 * 
 * BAND_COMPOSITE - Passes band data to the TIFF utility to be output 
 *                  as a composite TIFF file.
 * 
 * FLUSH - Writes any data that is held by the TIFF utility to the 
 *         local disk.
 * 
 * CLOSE - Closes the TIFF file that is held in memory by the TIFF 
 *         utility, and releases any allocated memory.
 *
 *
 * This TIFF utility only supports a limited subset of
 * of the TIFF specification Revision 6.
 *
 * <b>Input raster formats;</b>
 *
 * - KCMY 1 bit per pixel.
 * - KCMY 2 bit per pixel.
 * - KCMY 4 bits per pixel (2 pixels per byte).
 * - KCMY 8 bits per pixel.
 * - K 1 bit per pixel.
 * - K 2 bit per pixel.
 * - K 4 bit per pixel.
 * - K 8 bit per pixel.
 * - RGB 4 bits per pixel (2 pixels per byte).
 * - RGB 8 bits per pixel.
 *
 * <b>Output raster formats;</b>
 *
 * - K    Single file, mono/grayscale. 
 * - CMYK Separations - four files, each one mono/grayscale (plates).
 * - CMYK Pixel interleaved composite file - note that it must be 8 
 *        bits-per-pixel to be compatible with most TIFF readers. 1, 2 and
 *        4 bits-per-pixel input rasters will be converted to 
 *        8 bits-per-pixel.
 * - RGB Separations - three files, each one mono/grayscale (plates).
 * - RGB Pixel interleaved composite file - note that it must be 8 
 *        bits-per-pixel to be compatible with most TIFF readers. 
 *        4 bits-per-pixel input rasters will be converted to 
 *        8 bits-per-pixel.
 *
 */


/* This allos this header file to be included more than once */
#ifndef GGE_TIFF_HEADER
#define GGE_TIFF_HEADER

#ifdef __cplusplus
extern "C"
{
#endif

/**** Return Values for GGE_TIFF *************/

/*! The call was successful. */
#define GGE_TIFF_SUCCESS 0

/*! The supplied input selector was unknown to the TIFF utility.

  Check that the uSelector parameter is not zero. Check that its value
  exists in enum ::EGGE_TIFF_INPUT_SELECTOR. 
*/
#define GGE_TIFF_ERROR_INSEL_UNKNOWN 1   

/*! A supplied parameter was incorrect. Check that the pointer to the 
    data structure is valid. 
*/
#define GGE_TIFF_ERROR_INSEL_INVALID_PARAM 2   

/*! The supplied selector has an associated data pointer, but the 
    data structure size was incorrect. 

  Probably indicates that the version of the data structure has 
  changed. Check that the module is the correct version for your 
  application.
*/
#define GGE_TIFF_ERROR_INSEL_DATA_SIZE 3

/*! Failed to open TIFF file.

  Selector ::GGE_TIFF_INSEL_CLOSE should be called to free any 
  memory used by the GGETIFF module.
*/
#define GGE_TIFF_ERROR_OPEN_ERROR 4

/*! Failed to allocate memory.  

  Selector ::GGE_TIFF_INSEL_CLOSE should be called to free any 
  memory used by the GGETIFF module.

  TIFF file output will be affected.
*/
#define GGE_TIFF_ERROR_OUT_OF_MEMORY 5


/**** Selectors input (GGE_TIFF) *************/

enum EGGE_TIFF_INPUT_SELECTOR
{
    /*! Gets version information for the TIFF utility, including the
        major, minor, and revision details, as well as the build style.

        The information from the utility is stored in the
        \ref GGE_TIFF_Version "TGGE_TIFF_VERSION" data structure.

    */
    GGE_TIFF_INSEL_VERSION = 0,

    /*! Checks that the TIFF utility supports a selector, without
        calling the selector. The name of the input selector to check
        is stored in the \ref GGE_TIFF_SelectorSupported 
        "TGGE_TIFF_SELECTOR_SUPPORTED data structure.

    */
    GGE_TIFF_INSEL_SELECTOR_SUPPORTED,

    /*! Initializes the TIFF utility and passes it data for a TIFF 
        header.
        
        The data is stored in the \ref GGE_TIFF_Header 
        "TGGE_TIFF_HEADER" data structure.
     */
    GGE_TIFF_INSEL_HEADER,

    /*! Passes band data to the TIFF utility to be output as separated
        TIFF files (or single 'K'  file if creating monochrome output).
        The band data is stored in the \ref GGE_TIFF_BandSeparated
        "TGGE_TIFF_BAND_SEPARATED" data structure.
     */
    GGE_TIFF_INSEL_BAND_SEPARATED,

    /*! Passes band data to the TIFF utility to be output as a 
        composite TIFF file. The band data is stored in the 
        \ref GGE_TIFF_BandPixelInterleaved TGGE_TIFF_INSEL_BAND_PIXEL_INTERLEAVED data 
        structure. 
     */
    GGE_TIFF_INSEL_BAND_PIXEL_INTERLEAVED,

    /*! Passes band data to the TIFF utility to be output as a 
        composite TIFF file. The band data is stored in the 
        \ref GGE_TIFF_BandComposite TGGE_TIFF_BAND_COMPOSITE data 
        structure. 
     */
    GGE_TIFF_INSEL_BAND_COMPOSITE,

    /*! Writes any data that is held by the TIFF utility to the local 
        disk. 
     */
    GGE_TIFF_INSEL_FLUSH,

    /*! Closes the TIFF file that is held in memory by the TIFF 
        utility, and releases any allocated memory.

        The separated TIFF files are either 1, 4, or 8 bits-per-pixel.

        The composite TIFF file is  8 bits-per-pixel (as recommended by
        the TIFF specification). Note that  1, and 4 bits-per-pixel
        data will be converted into 8 bits-per-pixel. 
     */
    GGE_TIFF_INSEL_CLOSE,

    /*! Passes band data to the TIFF utility to be output as a 
        RGB palette TIFF file. The band data is stored in the 
        \ref GGE_TIFF_BandRGBPalette TGGE_TIFF_BAND_RGB_PALETTE data 
        structure. 
     */
    GGE_TIFF_INSEL_BAND_RGB_PALETTE,

    /*! Last selector becomes first selector of next layer - if there is a next layer. */
    GGE_TIFF_LAST_INPUT_SELECTOR
};


/*! \brief Stores version information for the TIFF utility. 

  This data structure stores data required and produced by the
  ::GGE_RSL_INSEL_VERSION input selector.
*/
struct GGE_TIFF_Version
{
    unsigned int cbSize; /*!< The size of this structure, in bytes. */

    /*! Major version number of the TIFF utility. 
    */
    unsigned int uMajorVer;  

    /*! Minor version number of the TIFF utility. 
    */
    unsigned int uMinorVer;  
    
    /*! Revision number of the TIFF utility. 
    */
    unsigned int uRevision;  
};

/*! Type definition for a \c struct ::GGE_TIFF_Version data structure. 
*/
typedef struct GGE_TIFF_Version TGGE_TIFF_VERSION;

/*! Type definition for a pointer to a struct GGE_TIFF_Version data 
    structure. 
*/
typedef struct GGE_TIFF_Version *PTGGE_TIFF_VERSION;

/*! \brief Stores the name of an input selector and a boolean 
           indicating whether the selector is supported.

  This data structure stores data required by the 
  ::GGE_TIFF_INSEL_SELECTOR_SUPPORTED input selector.

  You can use the data structure to ask the TIFF utility if it supports
  a particular input selector.
*/
struct GGE_TIFF_SelectorSupported
{
    unsigned int cbSize; /*!< The size of this structure, in bytes. */

    /* Input */
    unsigned int uSelector;             /*!< The specified selector. */

    /* Output */
    unsigned int bSupported;            /*!< Equal to 1 or non-zero if
                                             the specified selector is
                                             supported, otherwise it is
                                             zero. 
                                         */

};

/*! Type definition for a \c struct ::GGE_TIFF_SelectorSupported data structure. 
*/
typedef struct GGE_TIFF_SelectorSupported TGGE_TIFF_SELECTOR_SUPPORTED;

/*! Type definition for a pointer to a struct GGE_TIFF_SelectorSupported data 
    structure. 
*/
typedef struct GGE_TIFF_SelectorSupported *PTGGE_TIFF_SELECTOR_SUPPORTED;

/*! \brief Stores data to be used in a TIFF header.

  This data structure stores data required by the
  ::GGE_TIFF_INSEL_HEADER input selector.
*/
struct GGE_TIFF_Header
{
    unsigned int cbSize; /*!< The size of this structure, in bytes. */

    /* Inputs */

    /*! Separated TIFF files will be output if this is non-zero. A
        composite TIFF file (8 bits-per-pixel) will be generated if 
        this parameter is zero.
     */
    unsigned int bSeparated;

    /*! TIFF path and filname template. Colorant separation, and page 
        count will be appended to the filename template.
        For example, pszPathName = "c:\folder\foo", cmyk separated, 
        will become;

        - "c:\folder\foo-000-C.tif"
        - "c:\folder\foo-000-M.tif"
        - "c:\folder\foo-000-Y.tif"
        - "c:\folder\foo-000-K.tif"

    */
    char *pszPathName;                  
    
    /*! Number of colorants. */
    unsigned int uColorants;

    /*! Bytes per line. */
    unsigned int uBytesPerLine;

    /*! Bits per pixel. */
    unsigned int uBitsPerPixel;

    /*! Lines per page. */
    unsigned int uLinesPerPage;

    /*! Output raster width in pixels. */
    unsigned int uWidthPixels;

    /*! The horizontal resolution (DPI, dots per inch). */
    unsigned int uXDPI;

    /*! The vertical resolution (DPI, dots per inch). */
    unsigned int uYDPI;

    /*! Composite output shold be RGB palette instead of 8bpp CMYK. 
    
        Currently only supports 1 bpp CMYK and 2 bpp CMYK.
    */
    unsigned int bRGBPal;

    /*! Image Description.

        This will be inserted into TIFF tag 270. */
    unsigned char *pszDescription;

    /*! Swap the bytes in a word.

        The raster buffer for each colorant will have its bytes swapped...
        bytes 0,1,2,3, 4,5,6,7... swapped to 3,2,1,0, 7,6,5,4...
        before outputting. */
    unsigned int bSwapBytesInWord;
    
    /* Outputs */

    /*! (Output) Pathname strings. A pointer to an array of strings, for 
        each of the \c uColorants elements.

        The memory allocated for these strings is only valid between 
        calls to the ::GGE_TIFF_INSEL_HEADER and ::GGE_TIFF_INSEL_CLOSE
        input selectors.
    */
    char **paszPathNames;

};

/*! Type definition for a \c struct ::GGE_TIFF_Header data structure. 
*/
typedef struct GGE_TIFF_Header TGGE_TIFF_HEADER;

/*! Type definition for a pointer to a struct GGE_TIFF_Header data 
    structure. 
*/
typedef struct GGE_TIFF_Header *PTGGE_TIFF_HEADER;


/*! \brief Stores band data to be output as separated TIFF files.

  This data structure stores data required by the
  ::GGE_TIFF_INSEL_BAND_SEPARATED input selector.
*/
struct GGE_TIFF_BandSeparated
{
    unsigned int cbSize; /*!< The size of this structure, in bytes. */

    /*! Colorant. */
    unsigned int uColorant;

    /*! Number of lines in this band. */
    unsigned int uLinesThisBand;

    /*! Band data. */
    char * pBandBuffer;

};

/*! Type definition for a \c struct ::GGE_TIFF_BandSeparated data structure. 
*/
typedef struct GGE_TIFF_BandSeparated TGGE_TIFF_BAND_SEPARATED;

/*! Type definition for a pointer to a struct GGE_TIFF_BandSeparated data 
    structure. 
*/
typedef struct GGE_TIFF_BandSeparated *PTGGE_TIFF_BAND_SEPARATED;


/*! \brief Stores band data to be output as RGB pixel interleaved TIFF files.

  This data structure stores data required by the
  ::GGE_TIFF_INSEL_BAND_PIXEL_INTERLEAVED input selector.
*/
struct GGE_TIFF_BandPixelInterleaved
{
    unsigned int cbSize; /*!< The size of this structure, in bytes. */

    /*! Number of lines in this band. */
    unsigned int uLinesThisBand;

    /*! RGB Pixel interleaved band data. */
    char * pBandBuffer;

};

/*! Type definition for a \c struct ::GGE_TIFF_BandPixelInterleaved data structure. 
*/
typedef struct GGE_TIFF_BandPixelInterleaved TGGE_TIFF_BAND_PIXEL_INTERLEAVED;

/*! Type definition for a pointer to a struct GGE_TIFF_BandPixelInterleaved data 
    structure. 
*/
typedef struct GGE_TIFF_BandPixelInterleaved *PTGGE_TIFF_BAND_PIXEL_INTERLEAVED;


/*! \brief Stores band data to be output as a composite TIFF file. 

  This data structure stores data required by the 
  ::GGE_TIFF_INSEL_BAND_COMPOSITE input selector.
*/
struct GGE_TIFF_BandComposite
{
    unsigned int cbSize; /*!< The size of this structure, in bytes. */

    /*! Number of colorants. */
    unsigned int uColorants;

    /*! Number of lines in this band. */
    unsigned int uLinesThisBand;

    /*! Band data. A pointer to an array of buffers for each colorant. */
    char ** paBandBuffer;

};

/*! Type definition for a \c struct ::GGE_TIFF_BandComposite data structure. 
*/
typedef struct GGE_TIFF_BandComposite TGGE_TIFF_BAND_COMPOSITE;

/*! Type definition for a pointer to a struct GGE_TIFF_BandComposite data 
    structure. 
*/
typedef struct GGE_TIFF_BandComposite *PTGGE_TIFF_BAND_COMPOSITE;


/*! \brief Stores band data to be output as a composite TIFF file. 

  This data structure stores data required by the 
  ::GGE_TIFF_INSEL_BAND_RGB_PALETTE input selector.
*/
struct GGE_TIFF_BandRGBPalette
{
    unsigned int cbSize; /*!< The size of this structure, in bytes. */

    /*! Number of colorants. */
    unsigned int uColorants;

    /*! Number of lines in this band. */
    unsigned int uLinesThisBand;

    /*! Band data. A pointer to an array of buffers for each colorant. */
    char ** paBandBuffer;

};

/*! Type definition for a \c struct ::GGE_TIFF_BandComposite data structure. 
*/
typedef struct GGE_TIFF_BandRGBPalette TGGE_TIFF_BAND_RGB_PALETTE;

/*! Type definition for a pointer to a struct GGE_TIFF_BandComposite data 
    structure. 
*/
typedef struct GGE_TIFF_BandRGBPalette *PTGGE_TIFF_BAND_RGB_PALETTE;


/*! This is the type definition for the one and only TIFF function. 

    The function call consists of: an input selector, a pointer to a 
    data structure, and a return value.

    The input selector defines the functionality required by the call.
    For example, to create a TIFF header, you could use the input 
    selector ::GGE_TIFF_INSEL_HEADER. 

    The data structure stores data to be passed into and out from the
    function call. For example, the \ref GGE_TIFF_Header 
    "TGGE_TIFF_HEADER" data structure stores data for the TIFF header.
*/
unsigned int GGE_TIFF(enum EGGE_TIFF_INPUT_SELECTOR uSelector, void *pContext);

#ifdef __cplusplus
}
#endif

#endif


