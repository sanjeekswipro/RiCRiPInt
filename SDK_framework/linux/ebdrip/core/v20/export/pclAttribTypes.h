/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:pclAttribTypes.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 */

#ifndef _pcl5AttribTypes_h_
#define _pcl5AttribTypes_h_

#include "pcl5resources.h"
#include "dl_storet.h"
#include "displayt.h"
#include "matrix.h"
#include "pclGstate.h"
#include "dl_color.h"

/** A PCL packed color which may be preconverted to a DL color. */
typedef union pcl_color_union {
  intptr_t i ; /**< Used for comparing color values and RLE span lengths. */
  PclPackedColor packed ;
  p_ncolor_t ncolor ;
} pcl_color_union ;

/** Line types for PCL patterns. */
enum {
  PCL_PATTERNLINE_FULL, /**< Fully expanded data */
  PCL_PATTERNLINE_RLE1, /**< Base index for RLE types. */
  /** Offset from line type to number of RLE runs. */
  PCL_PATTERNLINE_RLE0 = PCL_PATTERNLINE_RLE1 - 1,
  /** Maximum number of runs representable in pattern RLE data. */
  PCL_PATTERNLINE_RLEMAX = 255 - PCL_PATTERNLINE_RLE0
} ;

typedef struct PclDLPatternLine {
  uint8 type ;    /**< One of PCL_PATTERNLINE_*. */
  uint8 repeats ; /**< Number of identical lines after this. */
  uint16 offset ; /**< Offset from start of data. */
} PclDLPatternLine ;

/**
 * Pattern bitmap used during rendering. This is at device resolution,
 * and is oriented and offset as required.
 */
typedef struct {
  uint32 width, height;

  enum {
    PCL_PATTERN_PRECONVERT_NONE,    /**< Not preconverted, use packed colors. */
    PCL_PATTERN_PRECONVERT_STARTED, /**< Partially preconverted, use ncolors */
    PCL_PATTERN_PRECONVERT_DEVICE   /**< Fully preconverted, device ncolors. */
  } preconverted ; /**< Has the data been preconverted to DL colors? */

  /** Number of entries in the palette; if zero, no palette is used. */
  uint32 paletteSize;

  /** The palette. */
  pcl_color_union *palette;

  /** Pixel data; either palette or direct. */
  union {
    /** Indices into palette. */
    uint8* indices;

    /** Raw color data. */
    pcl_color_union *pixels ;
  } data;

  /** Line structure for PCL patterns. */
  PclDLPatternLine *lines ;

  /** Number of bytes in palette and data. */
  size_t dataBytes;

} PclDLPattern;

/**
 * The pattern iterator allows rows of pattern data to be read.
 */
typedef struct {
  pcl_color_union color ;  /**< Current color. */
  int32 cspan ;            /**< Number of pixels left in current color */
  int32 nlines ;           /**< Number of lines using this iterator. */
  /** Span data for current pixel, start of line, end of line. */
  union {
    uint8 *indices ;
    pcl_color_union *pixels ;
  } curr, start, end ;
  Bool rle ;               /**< This line is RLE. */
  uint32 paletteSize ;     /**< The size of the palette. */
  pcl_color_union *palette ; /**< The palette. */
} PclDLPatternIterator ;

/**
 * Data used during PCL DL pattern construction, and subsequently to check for
 * pattern equality.
 */
typedef struct {
  uint32 deviceDpi;
  Bool transformValid;
  OMATRIX* transform;
  Bool transposed;
  IPOINT origin;
  uint32 paletteUid;
  IPOINT targetSize;

  /* Cached pattern color information. */
  uint8 cached ;
  uint8 patternColors ;
  uint8 useForeground ;
  PclPackedColor patternColor ; /* pattern non-white color (if only one) */

  /* This field is only used during construction (not equality testing), and
   * will always be null in constructed instances. */
  Pcl5CachedPalette* palette;
} PclPatternConstructionState;

/**
 * PCLXL Pattern type.
 */
typedef struct {
  /* The size is the width and height in source pixels */
  IPOINT size, targetSize;
  Bool direct;

  /* The number of bits per pixel, either 1,4 or 8 bits.
   * Direct patterns are always converted to 8 bits.
   */
  int32 bits_per_pixel ;

  /* The number of bytes in each line. */
  int32 stride ;

  union {
    /* Indices into palette. */
    uint8* indices;

    /* Raw color data. */
    PclPackedColor* pixels;
  } data;
} PclXLPattern;

/* Types of cached pattern. */
#define PCL5_PATTERN 1
#define PCLXL_PATTERN 2

/**
 * PCL Pattern container; wrapper around either PCL5 or PCLXL pattern.
 */
typedef struct {
  /* One of PCL5_PATTERN, PCLXL_PATTERN. */
  uint32 type;
  union {
    void* pointer;
    pcl5_pattern* pcl5;
    PclXLPattern* pclxl;
  } pointer;
} PclCachedPattern;

/** What color(s) appear in PCL patterns? Color patterns are special in PCL
    as they cause the foreground color to be ignored in the calculation of
    'texture'. If only white and either black or another color is used, we
    can use the specialised pattern blitters for transparent patterns. */
enum {
  PCL_PATTERN_NONE = 0,  /**< No pattern (no colors) */
  PCL_PATTERN_WHITE = 1, /**< White color in pattern */
  PCL_PATTERN_BLACK = 2, /**< Black color in pattern */
  PCL_PATTERN_BLACK_AND_WHITE = PCL_PATTERN_WHITE|PCL_PATTERN_BLACK,
  PCL_PATTERN_OTHER = 4, /**< Non-white, non-black color in pattern. */
  PCL_PATTERN_OTHER_AND_WHITE = PCL_PATTERN_WHITE|PCL_PATTERN_OTHER,
  PCL_PATTERN_MANY = 6,  /**< More than one non-white color. */
  PCL_PATTERN_ALL = 7    /**< White and more than one non-white color. */
} ;

/** Is the object starting, ending, inside or outside of an XOR clipping
    idiom? */
enum {
  PCL_XOR_OUTSIDE,
  PCL_XOR_STARTING,
  PCL_XOR_INSIDE,
  PCL_XOR_ENDING
} ;

/**
 * PCL5 display list state attributes.
 */
struct PclAttrib {
  /* Base class; must be first member. */
  DlSSEntry storeEntry;

  uint8 sourceTransparent;  /** TRUE if the source is transparent. */
  uint8 patternTransparent; /** TRUE if the pattern is transparent. */
  uint8 rop;
  /** One of PCL_FOREGROUND_NOT_SET, PCL_DL_COLOR_IS_FOREGROUND, or
      PCL_FOREGROUND_IN_PCL_ATTRIB. */
  uint8 foregroundSource;

  /** A combination of the PCL_PATTERN_* enumeration values. */
  uint8 patternColors ;
  uint8 backdrop ;     /** TRUE if backdrop support is needed for this case. */
  uint8 patternBlit ;  /** TRUE if the pattern blitters should be used. */
  uint8 xorstate ;     /** One of the PCL_XOR_* enumeration values. */

  /* The foreground color, in RGB. This is only valid when 'foregroundSource'
   * is PCL_FOREGROUND_IN_PCL_ATTRIB. This will be combined with any pattern
   * color during rendering to produce 'Texture'. */
  PclPackedColor foreground;

  PclCachedPattern cacheEntry;

  /** \todo This may benefit from a cache of its own. */
  PclDLPattern* dlPattern;

  /* This field is used during construction (since the state within is
   * incorporated into the Pcl5DLPattern), and subsequently to check if two
   * Pcl5Attrib structures are the same. */
  PclPatternConstructionState constructionState;
};

#endif

/* Log stripped */

