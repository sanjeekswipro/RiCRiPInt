/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!export:shadex.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exported functions for smooth shading required by PDF.
 */

#ifndef __SHADEX_H__
#define __SHADEX_H__

/** \defgroup shfill Smooth shading.
 * \ingroup core
 */

#include "dl_color.h" /* dl_color_t */
#include "objects.h"  /* OBJECT, for fndecode in SHADINGinfo */
#include "display.h"  /* LateColorAttrib */

struct GSTATE ; /* from SWv20/COREgstate */

/* Incomplete definitions of types defined by shading */
typedef struct SHADINGinfo SHADINGinfo ;
typedef struct SHADINGvertex SHADINGvertex ;
typedef struct SHADINGsource SHADINGsource ;

/** \brief Information about shading dictionary.
 *
 * If there are no functions, ncolors = ncomps == channels to interpolate across
 * If there are functions, ncomps == channels to interpolate across = 1,
 *                         ncolors == number of function outputs.
 */
struct SHADINGinfo {
  DL_STATE *page ;
  int32 type, ncolors ;
  sbbox_t bbox ;
  uint16 smoothnessbands ; /* number of bands required to achieve requested smoothness */
  uint16 padding ;
  uint8 antialias, inpattern2, base_index, preseparated ;
  USERVALUE *scratch ;
  int32 spotno ;
  int32 coercion ;      /* name of coercion */

  /* Shading function application */
  int32 nfuncs, noutputs, base_fuid, ncomps ;
  struct OBJECT *funcs ;
  struct OBJECT *opacity_func ;
  struct OBJECT fndecode ;
  struct rcbs_function_t **rfuncs ; /* Recombine linearised functions */
  COLORANTINDEX *rfcis ;    /* Pseudo colorantindexes for functions */
  USERVALUE rflat ;         /* Recombine flatness */
  void *base_addr ;         /* Base address for freeing */
  LateColorAttrib *lca ;     /* Color attribs for probing transparency groups */
} ;

/** The shading vertex structure is accessed by HDLT, so is exported. */
struct SHADINGvertex {
  SYSTEMVALUE x, y ;
  USERVALUE *comps ;
  USERVALUE opacity ;
  uint8 converted, probeconverted, upwards ;
  uint8 spare ;
  dl_color_t dlc ;       /* color converted to current target (blend space or virtual device) */
  dl_color_t dlc_probe ; /* final device color (approx) used for linearity testing only */
} ;

/* Gouraud triangles are decomposed by bisection of the triangle edges. We
   used to store the coordinates as double precision, bisect in real space
   and convert to integer at the last minute. However, converting from real to
   integer involves a significant performance penalty, so coordinates are now
   rounded to integers before storing in the object, and fixed-point
   subdivision (using Hq32x2 coordinates) is used during rendering. */

/* Definition of the object added to DL */
struct GOURAUDOBJECT {
  int32 gsize;
  dcoord coords[6] ;
  void *base ;  /* Base address for freeing. */
  uintptr_t flags ;
  /* COLORVALUES promised from here..., plus colorWorkspace if necessary */
} ;

struct SHADINGOBJECT {
  int32 nchannels;
  void *colorWorkspace;  /* NULL or space for number of DDA channels */
  void *colorWorkspace_base; /* For freeing */
  uint16 mbands ;        /* Minimum # bands to divide whole colour range */
  uint16 noisesize ;     /* Pixels sharing noise value */
  USERVALUE noise ;      /* Factor of cband to add as noise */
  SHADINGinfo *info;
  HDL *hdl;
} ;

void gouraud_init(void);
void gouraud_finish(void);

/** Size of Gouraud rendering workspace for nchannels. */
size_t gouraud_dda_size(uint32 nchannels);

#define RENDERSH_MAX_FIXED_CHANNELS 8 /* enough to accommodate All
                                         (pads to 7 colorants in a paint mask, when 6 or
                                         fewer channels in all */

/** Set the smoothness set in the current graphics state.

    \param stack Stack to get the smoothness from.
    \return TRUE on success, FALSE on failure. Failure may occur if there is
      no value on the stack, or it's the wrong type.
*/
Bool gs_setsmooth(/*@notnull@*/ /*@in@*/ struct STACK *stack) ;

/** Return the smoothness set in a graphics state.

    \param gs Graphics state to get the smoothness from.
    \return The smoothness set in the graphics state.
*/
USERVALUE gs_currsmooth(/*@notnull@*/ /*@in@*/ corecontext_t *context,
                        /*@notnull@*/ /*@in@*/ struct GSTATE *gs) ;

/** Flags for \c gs_shfill. */
enum { GS_SHFILL_SHFILL = 0, GS_SHFILL_PATTERN2 = 1, GS_SHFILL_VIGNETTE = 2 } ;

/** Build display list for a shading dictionary.

    \param stack Stack to get shaded fill dictionary from.
    \param pgstate Graphics state to use to set up the display list.
    \param flags Flags indicating how the shaded fill was generated.
    \return TRUE on success, FALSE on failure. Failure may occur if there is
      no value on the stack, it's the wrong type, or .
*/
Bool gs_shfill(/*@notnull@*/ /*@in@*/ struct STACK *stack,
               /*@notnull@*/ /*@in@*/ struct GSTATE *pgstate,
               int32 flags) ;

/** Evaluate the value of a shading color (possibly with function
    application) for specific values of the interpolated components, leaving
    a DL color in \c dl_currentcolor ;

    \param comps Input colour or parameter components.
    \param upwards If TRUE, evaluate upwards at function discontinuities. If
      FALSE, evaluate downwards.
    \param sinfo A shading to evaluate the colour for.
    \return TRUE on success, FALSE on failure.
*/
Bool shading_color_function(USERVALUE *comps, Bool upwards, SHADINGinfo *sinfo) ;

/** Cross product check to ensure that Gouraud triangles won't exceed limits
    when rendering. */
Bool cross_check(dcoord x0, dcoord y0, dcoord x1, dcoord y1, dcoord x2, dcoord y2) ;

/* Debug */
#if defined( DEBUG_BUILD )
extern int32 shading_debug_flag ;

enum {SHADING_DEBUG_BLENDGOURAUD =  0x1,
      SHADING_DEBUG_OUTLINE =       0x2,
      SHADING_DEBUG_INFO =          0x4,
      SHADING_DEBUG_RADIALCOONS =   0x8,
      SHADING_DEBUG_DISCONTINUITY = 0x10,
      SHADING_DEBUG_TENSOR_COLOR =  0x20,
      SHADING_DEBUG_OUTLINE_PATCH = 0x40,
      SHADING_DEBUG_TRACE_COLORS =  0x80,
      SHADING_RECOMBINE_NOFIX =     0x100,
      SHADING_DEBUG_RADIAL_Q1 =     0x200,
      SHADING_DEBUG_RADIAL_Q2 =     0x400,
      SHADING_DEBUG_RADIAL_Q3 =     0x800,
      SHADING_DEBUG_RADIAL_Q4 =     0x1000,
      SHADING_DEBUG_TENSOR_EDGES =  0x2000,
      SHADING_DEBUG_TENSOR_THIRDS = 0x4000,
      SHADING_DEBUG_PATCH_STRAYS =  0x8000
} ;

void init_shading_debug(void) ;
#endif

#if defined(METRICS_BUILD) || defined(DEBUG_BUILD)
extern int32 n_gouraud_triangles ;
#endif

#ifdef METRICS_BUILD
extern int32 n_coons_patches ;
extern int32 n_tensor_patches ;
extern int32 n_decomposed_triangles ;
extern int32 max_decomposition_depth ;
extern int32 max_decomposition_triangles ;
extern int32 max_gouraud_dl_size ;
extern int32 total_gouraud_dl_size;
/* Temporaries for instrumentation */
extern int32 this_decomposed_triangles ;
extern int32 this_decomposition_depth ;
#endif /* DEBUG_BUILD */

#endif /* protection for multiple inclusion */


/* Log stripped */
