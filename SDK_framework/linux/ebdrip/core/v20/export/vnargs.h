/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:vnargs.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Vignette detection full type definitions
 */

#ifndef __VNARGS_H__
#define __VNARGS_H__

#include  "vntypes.h"
#include  "objects.h"
#include  "graphict.h"
#include  "dl_color.h"
#include  "displayt.h"
#include  "rcbtrap.h"


struct STROKE_PARAMS ;  /* from SWv20 */
struct idlomArgs ;      /* from SWv20 */


/* ----- External structures ----- */

struct VIGNETTEARGS {
  LISTOBJECT *vd_lobj;  /**< Display list object. */

  USERVALUE vd_ftol;    /**< Current flatness. */

  int32 vd_pathtype;
  int32 vd_filltype;
  PATHINFO vd_origpath; /**< Original path (or copied one). */
  sbbox_t vd_gbbox;     /**< Total bounding box. */

  int32 vd_numpaths;    /**< Number of sub-paths (1 or 2). */
  PATHLIST vd_plst[2];  /**< Where we put our two paths. */
  PATHINFO vd_path[2];  /**< Hi-level path object(s). */

  int32 vd_style;       /**< Style we think we've got. */
  int32 vd_match;       /**< Match we think we've got. */
  int32 vd_type[2];     /**< Type we think we've got. */
  int32 vd_orient[2];   /**< Orientation we think we've got. */

  uint8 vd_icolor_space_original; /**< Original input color space
                                       (does not get promoted) */
  USERVALUE vd_icolor_value_orig; /**< Original gray value before promoted
                                       to RGB or CMYK */
  uint8 vd_icolor_space;       /**< Input color space (maybe converted to RGB)*/
  int32 vd_icolor_dims;        /**< Dimension of input color space. */
  USERVALUE *vd_icolor_values; /**< Pointer to input color. */
  OBJECT vd_icolor_object;     /**< Colorspace object */

  /* For vignettes we may use a completely different color chain; for
   * example if we have a different intercept for a shaded fill. In
   * addition, certain controls like 100% Black Intercept don't apply
   * for such objects (even if use no intercept).
   * So, since we don't know until later on if an object is part of a
   * vignette or not, we need to transform the color twice; once with
   * its own colorType and once with colorType set to GSC_VIGNETTE.
   */
#ifdef  DE_OLD_VDL_COLOR
  dl_color_t vdl_color_all;  /**< full set of colorants */
  dl_color_t vdl_color_red;  /**< reduced set of colorants (optional) */
#else
  dl_color_t vdl_color;      /**< color of object in SHFILL style,
                                  including overprint info */
#endif

  struct STROKE_PARAMS *vd_sparams; /**< Link to stroke params (if relevant) */

  struct idlomArgs *vd_hdlt;        /**< Link to hdlt params (if relevant). */

  VIGNETTEARGS *vd_next;            /**< Forwards link. */
  VIGNETTEARGS *vd_prev;            /**< Backward link. */
  uint16 size;                      /**< allocation size of the structure */
#if PS2_PDFOUT
  uint8 basecolorspace;
  union  {
    struct {
      OBJECT sepname;
      OBJECT septinttransform;
     } sepcolor;
     USERVALUE orig_icolor[4];
     COLORinfo *orig_colinfo;
  } orig_color;
#endif

  RCBTRAP vd_rcbtrap ;
} ;

#endif /* protection for multiple inclusion */


/* Log stripped */
