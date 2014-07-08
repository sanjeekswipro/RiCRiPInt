/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:displayt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Typedefs for low-level display list (DODL)
 */

#ifndef __DISPLAYT_H__
#define __DISPLAYT_H__


/* Do NOT include any other headers, or make this file depend on any other
   headers */

typedef struct DL_STATE DL_STATE ;
typedef struct HDL_LIST HDL_LIST;

typedef struct GSTAGSTRUCTUREOBJECT GSTAGSTRUCTUREOBJECT ;
typedef struct CLIPOBJECT CLIPOBJECT ;
typedef struct DLREF DLREF;
typedef struct DLRANGE DLRANGE;
typedef struct PATTERNOBJECT PATTERNOBJECT ;
typedef struct pattern_shape_t pattern_shape_t ;
typedef struct SoftMaskAttrib SoftMaskAttrib ;
typedef struct TranAttrib TranAttrib ;
typedef struct LateColorAttrib LateColorAttrib ;
typedef struct PclAttrib PclAttrib ;
typedef struct STATEOBJECT STATEOBJECT ;
typedef struct LISTOBJECT LISTOBJECT;
typedef struct rcbs_patch_t rcbs_patch_t;
typedef struct IMAGEOBJECT IMAGEOBJECT;
typedef struct VIGNETTEOBJECT VIGNETTEOBJECT;
typedef struct GOURAUDOBJECT GOURAUDOBJECT;
typedef struct SHADINGOBJECT SHADINGOBJECT;
typedef struct HDLtype HDLtype;
typedef struct HDL HDL;
typedef struct Group Group;
typedef struct CELL CELL;
typedef struct SpotList SpotList;

/** Initialiser for HDL ids. */
enum { HDL_ID_INVALID = 0 } ;

/** Rendering opcodes. Opcodes should be numbered sequentially from zero, they
    are used as array indices in various places. */
enum { /* NOTE: When changing, please update RENDER_TYPE in
          SWcore!testsrc:swaddin:swaddin.cpp and debug_opcode_names in
          SWv20!src:display.c */
  RENDER_void,         /**< Not used or not valid entry */
  RENDER_erase,        /**< The erase of the background raster */
  RENDER_char,         /**< One or more text glyphs */
  RENDER_rect,         /**< A rectangel orthogonal to the device axes */
  RENDER_quad,         /**< Simple 2, 3 or 4 sided shape */
  RENDER_fill,         /**< Generic filled vector object */
  RENDER_mask,         /**< A sampled binary mask with a color poured through */
  RENDER_image,        /**< A sampled image placed with a tranform matrix */
  RENDER_vignette,     /**< A set of rectangles heuristically detected */
  RENDER_gouraud,      /**< A triangle with color values at the corners */
  RENDER_shfill,       /**< Container object for shaded fills */
  RENDER_shfill_patch, /**< Internal object for recombine */
  RENDER_hdl,          /**< A sub-display list. */
  RENDER_group,        /**< A Transparency Group. */
  RENDER_backdrop,     /**< Near raster representation of composited group. */
  RENDER_cell,         /**< A compressed RLE cell. */
  N_RENDER_OPCODES     /**< Used for array initialisation sizes */
} ;

#if defined( DEBUG_BUILD )
/** For human-readable debug messages involving render opcodes. */
extern const char debug_opcode_names[N_RENDER_OPCODES][16] ;
#endif


/** The type of "erase numbers", generation numbers for DLs. */
typedef int32 dl_erase_nr;

/** Erase number of the first DL. */
#define FIRST_DL ((dl_erase_nr)0)

/** An erase number that is never used for a DL. */
#define INVALID_DL ((dl_erase_nr)-1)

/** Maximum value of \c dl_erase_nr. */
#define MAX_DL_ERASE_NR MAXINT32

/** \brief Erase types.

    These values are used extensively to determine how the handoff of a page
    from interpretation to rendering should be performed, and the subsequent
    disposition of the page. The values are in order of most to least
    thorough destruction of DL fields. */
typedef enum {
  DL_ERASE_ALL,      /**< Complete destruction of async DL and all fields. */
  DL_ERASE_CLEAR,    /**< Erasepage, setpagedevice, synchronous showpage. */
  DL_ERASE_BEGIN,    /**< Failed to construct input page. */
  DL_ERASE_PARTIAL,  /**< Partial paint destroying DL. */
  DL_ERASE_PRESERVE, /**< Partial paint with vignette candidate. */
  DL_ERASE_COPYPAGE, /**< No destruction of DL objects. */
  DL_ERASE_GONE      /**< Page already erased asynchronously. */
} dl_erase_t ;

/* Log stripped */
#endif /* protection for multiple inclusion */
