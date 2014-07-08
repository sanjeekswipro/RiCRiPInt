/** \file
 * \ingroup corepcl
 *
 * $HopeName: SWv20!export:pclGstate.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Methods to manipulate the PCL graphics state.
 */
#ifndef __pclGstate_h__
#define __pclGstate_h__

#include "pcl5resources.h"
#include "matrix.h"

struct DL_STATE;
struct dl_color_t;

/* Values for ROP. */
enum {
  PCL_ROP_BLACK =   0,
  PCL_ROP_Dn    =  85,
  PCL_ROP_DTx   =  90,
  PCL_ROP_DSx   = 102,
  PCL_ROP_DSa   = 136,
  PCL_ROP_DTa   = 160,
  PCL_ROP_D     = 170,
  PCL_ROP_S     = 204,
  PCL_ROP_DSo   = 238,
  PCL_ROP_T     = 240,
  PCL_ROP_DTo   = 250,
  PCL_ROP_TSo   = 252,
  PCL_ROP_WHITE = 255
} ;

/* Values for the foreground color location. */

enum {
  /** Not set value. */
  PCL_FOREGROUND_NOT_SET,
  /** The object's DL color is the foreground color.

      This value is used for rect (area) fills, for characters, and for HPGL
      vectors. */
  PCL_DL_COLOR_IS_FOREGROUND,
  /** The foreground color is packed in the PCL attributes structure.

      This value is used for images, with either a black foreground if there
      is no pattern, the pattern palette colorspace is incompatible with the
      image colorspace, or there is a color pattern; or with the foreground
      color if the pattern is not color and the palette is compatible.

      This value is also used for the white spans of characters when source
      transparency is FALSE. The character's rendering (foreground) color is
      packed in the attrib, but it has a white DL colour. This allows the ROP
      optimisation tests to handle the character in the same way as the black
      spans, but the renderer will set up the blit colour to white. */
  PCL_FOREGROUND_IN_PCL_ATTRIB
} ;

#define PCL_VALID_FOREGROUND_SOURCE(_s) \
  ((_s) == PCL_DL_COLOR_IS_FOREGROUND || \
   (_s) == PCL_FOREGROUND_IN_PCL_ATTRIB)

/**
 * Pack the three RGB COLORVALUE's pointer to by cv_ into a single word (only
 * the top 8-bits of COLORVALUE's are used in PCL).
 */
#define PCL_PACK_RGB(cv_) \
  ((PclPackedColor)(((cv_)[0] >> 8) | ((cv_)[1] & 0xff00) | (((cv_)[2] & 0xff00) << 8)))

/**
 * Pack the four CMYK COLORVALUE's pointer to by cv_ into a single word (only
 * the top 8-bits of COLORVALUE's are used in PCL).
 */
#define PCL_PACK_CMYK(cv_) \
  ((PclPackedColor)(((cv_)[0] >> 8) | ((cv_)[1] & 0xff00) | \
                    (((cv_)[2] & 0xff00) << 8) | (((cv_)[3] & 0xff00) << 16)))

/**
 * Unpack the passed word (rgb_) previously packed by PCL_PACK_RGB() into the
 * passed COLORVALUE array cv_.
 */
#define PCL_UNPACK_RGB(rgb_, cv_) MACRO_START \
  (cv_)[0] = (COLORVALUE)((rgb_) << 8); \
  (cv_)[1] = (COLORVALUE)((rgb_) & 0xff00); \
  (cv_)[2] = (COLORVALUE)((rgb_) >> 8) & 0xff00; \
MACRO_END

/**
 * Unpack the passed word (cmyk_) previously packed by PCL_PACK_CMYK() into the
 * passed COLORVALUE array cv_.
 */
#define PCL_UNPACK_CMYK(cmyk_, cv_) MACRO_START \
  (cv_)[0] = (COLORVALUE)((cmyk_) << 8); \
  (cv_)[1] = (COLORVALUE)((cmyk_) & 0xff00); \
  (cv_)[2] = (COLORVALUE)((cmyk_) >> 8) & 0xff00; \
  (cv_)[3] = (COLORVALUE)((cmyk_) >> 16) & 0xff00; \
MACRO_END

/* Packed versions of black and white. These can't be an enum because the
   CMYK value is out of range for pedantic C89 (must be an int). */
#define PCL_PACKED_RGB_BLACK 0u

#define PCL_PACKED_RGB_WHITE 0x00ffffffu

/* There are multiple representations of black in CMYK (the one used here
 * is a rich, all channels full, additive black, required so ROP
 * calculations work), so this should not be used to test for a color being
 * black in general. */
#define PCL_PACKED_CMYK_BLACK 0x00000000u

#define PCL_PACKED_CMYK_WHITE 0xffffffffu

/**
 * Packed color type; this may contain either RGB or CMYK depending on the
 * color space being used.
 * Red or Cyan is in the least-significant byte.
 */
typedef uint32 PclPackedColor;

/**
 * Color patterns index into the current PCL palette; this palette is
 * exposed to the core via this structure. The palette is specified each time
 * the current pattern is set; to aid core-side identification of the palette
 * for caching purposes, a unique palette ID is associated with the palette; two
 * palettes with the same ID are assumed to be the same.
 */
typedef struct {
  /* The number of entries in the palette. */
  uint32 size;

  /* The unique identifier for the palette. */
  uint32 uid;

  /* The maximum palette size in PCL is 256, so we allocate enough space for
   * 256 entries; only 'size' entries are used. */
  PclPackedColor colors[256];
} Pcl5CachedPalette, PclXLCachedPalette;

/**
 * This special pattern should be used for solid erase fills. Using this global
 * will trigger the special behavior required by the erase fill, where pattern
 * transparency settings are ignored.
 */
extern pcl5_pattern erasePattern;

/**
 * This special pattern should be used for in place of the erase pattern when
 * drawing images; no special actions are taken and the current transparency
 * settings are honored.
 */
extern pcl5_pattern whitePattern;

/**
 * This special pattern should be used whenever a non-existant pattern is in
 * force. Any object using this pattern will be invisible.
 */
extern pcl5_pattern invalidPattern;

/**
 * Enable or disable the application of the PCL graphics state during
 * rendering. By default it is disabled, and should only be enabled for PCL
 * jobs.
 *
 * @param blackAndWhiteOnly Pcl5e is a truly monochrome output mode; objects are
 * either black or white. If we are in 5e mode, setting this parameter to TRUE
 * will avoid unnecessary color processing.
 */
void pclGstateEnable(corecontext_t* corecontext,
                     Bool enable,
                     Bool blackAndWhiteOnly);

/**
 * Returns true if PCL gstate application is enabled.
 */
Bool pclGstateIsEnabled(void);

/**
 * Returns true if PCL gstate is enabled and we're in PCL 5E mode.
 */
Bool pcl5eModeIsEnabled(void);

/**
 * Set the current pattern. 'pattern' should be null if the default
 * pattern (solid black) is required.
 * The passed cache entry may be retained by the core and used during rendering;
 * it should not be deallocated until rendering is complete.
 *
 * \param deviceDpi DPI of output device.
 * \param angle The rotation angle to apply to the pattern. Must be a multiple
 *        of 90 degrees.
 * \param origin Pattern origin in device coordinates.
 * \param palette Palette for color patterns. The passed palette should be valid
 *        until the patterned object has been added to the display list (at
 *        which point a copy is made if required). This may be NULL, in which
 *        case color patterns are treated as black and white.
 */
void setPcl5Pattern(pcl5_pattern *pattern,
                    uint32 deviceDpi,
                    uint32 angle,
                    IPOINT* origin,
                    Pcl5CachedPalette* palette);

/**
 * In PCL5 there are three sources for color - the current foreground color, the
 * color of the object itself, and the color of the current pattern.
 * Area fills and glyphs are black; any color they have is provided by the
 * foreground and the pattern color. Image are inherently colored, and their
 * final color may be a combination of all three sources. PCLXL doesn't have a
 * foreground color in the same way as PCL5, but in most cases we can pretend it
 * does.
 *
 * Each display list object has a single color - sometimes this should be used
 * as the foreground color (e.g. for area fills), other times not (in the case
 * of images, whose DL color is simply a marker for the colorants used in the
 * image).
 *
 * This method controls how the DL color of an object is used, and should be
 * called prior to adding an object to the display list.
 *
 * \param source  One of PCL_DL_COLOR_IS_FOREGROUND,
 *                PCL_FOREGROUND_IN_PCL_ATTRIB, or PCL_FOREGROUND_NOT_SET.
 *                A value of PCL_FOREGROUND_IN_PCL_ATTRIB causes the current
 *                color on the GSC_FILL chain to be copied into the current
 *                Pcl5Attrib state.
 */
Bool setPclForegroundSource(struct DL_STATE *page, uint8 source);

/** Returns one of PCL_DL_COLOR_IS_FOREGROUND, PCL_FOREGROUND_IN_PCL_ATTRIB
 *  or PCL_FOREGROUND_NOT_SET.
 */
uint8 getPclForegroundSource(void);

/** \brief Set the source transparency.
 *
 * \param transparent TRUE indicates that the source is transparent (source
 *                    white spans are ignored). FALSE indicates that the source
 *                    is opaque (spans will contribute to the ROP if the
 *                    texture is not transparent at this point).
 */
void setPclSourceTransparent(Bool transparent);

/** \brief Set the pattern transparency.
 *
 * \param transparent TRUE indicates that the texture is transparent (pattern
 *                    white spans are ignored). FALSE indicates that the texture
 *                    is opaque (spans will contribute to the ROP if the
 *                    source is not transparent at this point).
 */
void setPclPatternTransparent(Bool transparent);

/**
 * Set the current ROP.
 */
void setPclRop(uint8 rop);

/**
 * Returns the current rop.
 */
uint8 getPclRop(void);

/* PROTOTEST(plotchar_no_setg) */
/**
 * Returns the current source transparency state.
 */
Bool isPclSourceTransparent(void);

/* PROTOTEST(plotchar_no_setg) */
/**
 * Returns the current pattern transparency state.
 */
Bool isPclPatternTransparent(void);

/**
 * Color PCL is always rendered to an RGB or CMYK virtual device; this method
 * returns the current color, packed into an Pcl5PackedColor using either
 * PCL_PACK_RGB() or PCL_PACK_CMYK().
 */
Bool pclPackCurrentColor(struct DL_STATE *page, PclPackedColor *color);

#endif

/* Log stripped */

