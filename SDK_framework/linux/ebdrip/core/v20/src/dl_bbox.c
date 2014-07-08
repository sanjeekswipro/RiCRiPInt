/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_bbox.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to calculate display list BBoxes
 */

#include "core.h"
#include "swerrors.h" /* UNDEFINEDRESULT */

#include "display.h"  /* RENDER_*, etc */
#include "dl_shade.h" /* gouraudbbox */
#include "dl_bres.h"
#include "devops.h"   /* retainedform */
#include "vndetect.h" /* VIGNETTEOBJECT */
#include "graphics.h" /* CLIPPED_RHS */
#include "shading.h"  /* SHADINGinfo */
#include "matrix.h"   /* OMATRIX */
#include "bitblts.h"  /* FORM */
#include "pathops.h"  /* transform_bbox */
#include "hdlPrivate.h" /* hdlFromListobject etc */
#include "groupPrivate.h" /* groupBBox */
#include "imageo.h" /* IMAGEOBJECT */
#include "trap.h"     /* trapShapeBoundingBox */
#include "rcbshfil.h" /* rcbs_bbox */


void dlobj_bbox(LISTOBJECT *lobj, dbbox_t *bbox)
{
  HQASSERT(lobj, "No LISTOBJECT parameter");
  HQASSERT(bbox, "No bbox parameter");

  *bbox = lobj->bbox;
}

Bool dlobj_intersects(LISTOBJECT *lobj, dcoord x1, dcoord x2, Bool *intersects)
{
  dbbox_t bbox;

  HQASSERT(lobj != NULL, "lobj is null");
  HQASSERT(intersects != NULL, "intersects is null");

  dlobj_bbox(lobj, &bbox);
  *intersects = (bbox.x2 >= x1 && bbox.x1 <= x2);
  return TRUE;
}

/** Tries to find the largest rectangular area covered by the object without any
   holes.  Returns false if such an area could not be found for the object.
 */
Bool dlobj_interior_box(LISTOBJECT *lobj, dbbox_t *interior_box)
{
  CLIPOBJECT *clip ;
  const dbbox_t *bbox = &lobj->bbox ;

  HQASSERT(lobj->objectstate, "No object state, required for clip") ;
  clip = lobj->objectstate->clipstate ;

  *interior_box = clip->bounds ;

  /* Give up if complex clipping. */
  for ( ; clip ; clip = clip->context ) {
    if ( clip->fill ) {
      bbox_clear(interior_box) ;
      return FALSE ;
    }
  }

  switch ( lobj->opcode ) {
  case RENDER_erase :
    break ;
  case RENDER_rect :
    bbox_intersection(interior_box, bbox, interior_box) ;
    return !bbox_is_empty(interior_box) ;
  case RENDER_quad :
    bbox_intersection_coordinates(interior_box, bbox->x1 + 32, bbox->y1 + 32,
                                  bbox->x2 - 32, bbox->y2 - 32) ;
    return !bbox_is_empty(interior_box) ;
  case RENDER_fill :
  case RENDER_vignette :
  case RENDER_char :
    break ;
  case RENDER_image : {
    IMAGEOBJECT *imageobj = lobj->dldata.image ;

    if ( !imageobj->mask &&
         (imageobj->optimize & IMAGE_OPTIMISE_ROTATED) == 0 &&
         (imageobj->flags & IM_FLAG_COMPOSITE_ALPHA_CHANNEL) == 0 ) {
      bbox_intersection(interior_box, &imageobj->bbox, interior_box) ;
      return !bbox_is_empty(interior_box) ;
    }
    break ;
   }
  case RENDER_mask :
  case RENDER_gouraud :
  case RENDER_shfill :
  case RENDER_hdl :
  case RENDER_group :
  case RENDER_shfill_patch :
  case RENDER_backdrop :
  case RENDER_cell:
    break ;
  default:
    HQFAIL("Unrecognised or unimplemented DL object type") ;
    break ;
  }

  bbox_clear(interior_box) ;
  return FALSE ;
}

/* Log stripped */
