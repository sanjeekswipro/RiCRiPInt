/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:polygon_buffer_impl.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of polygon buffer, based on existing path construction
 * operators of the rip.
 * This module contains the functions that enable polygon paths to be
 * created, destroyed, stroked and filled.
 * It does not deal with the management of polygon mode, only the
 * provision of the means to construct and use polygon paths.
 */

#include "core.h"
#include "paths.h"
#include "gu_path.h"
#include "graphics.h"
#include "system.h"
#include "gu_cons.h"
#include "pathcons.h"
#include "gu_fills.h"
#include "pathops.h"
#include "gstack.h"
#include "clipops.h"
#include "pclAttrib.h"

#include "pcl5types.h"
#include "polygon_buffer_impl.h"
#include "hpgl2linefill.h"
#include "hpgl2vector.h"
#include "printmodel.h"
#include "hpgl2polygon.h"
#include "hpgl2state.h"
#include "pictureframe.h"
#include "pcl5color.h"
#include "pcl5context_private.h"

typedef struct HPGL2PolygonBuffer {
  PATHINFO polygon_buffer;
  Bool sub_path_started;
} HPGL2PolygonBuffer;

/* --- internal state management for drawing. --- */

/* Report whether a subpath has had its first element inserted. */
static Bool hpgl2_start_of_sub_path(HPGL2PolygonBuffer *polygon_buffer)
{
  return ! polygon_buffer->sub_path_started;
}

/* Confirm that element has been written into the polygon buffer. */
static Bool hpgl2_sub_path_started(HPGL2PolygonBuffer *polygon_buffer)
{
  polygon_buffer->sub_path_started = TRUE;

  return TRUE;
}

/* --- polygon buffer management routines --- */

/* Creation of the polygon buffer in the print state. */
struct HPGL2PolygonBuffer* polygon_buffer_impl_state_create(void)
{
  HPGL2PolygonBuffer *buffer;

  buffer = mm_alloc(mm_pcl_pool, sizeof(struct HPGL2PolygonBuffer),
                     MM_ALLOC_CLASS_PCL_CONTEXT);

  if ( buffer == NULL )
    return NULL;

  path_init(&buffer->polygon_buffer);

  return buffer;
}

Bool polygon_buffer_impl_state_destroy(struct HPGL2PolygonBuffer *buff)
{
  if ( buff != NULL )
    mm_free(mm_pcl_pool, buff,
            sizeof(struct HPGL2PolygonBuffer));
  return TRUE;
}

static Bool hpgl2_free_polygon_path(PATHINFO *polygon_path)
{
  if ( polygon_path->lastline )
    path_free_list( polygon_path->firstpath, mm_pool_temp);

  path_init(polygon_path);
  return TRUE;
}

/* --- Polygon buffer path construction API. --- */

/* remove any existing path in the buffer. */
Bool hpgl2_empty_polygon_buffer(HPGL2PolygonBuffer *polygon_buff)
{
  hpgl2_free_polygon_path(&polygon_buff->polygon_buffer );
  polygon_buff->sub_path_started = FALSE;
  return TRUE;
}

/* --- path construction routines --- */

/* plot a new point in the buffer. */
Bool hpgl2_polygon_plot(HPGL2PolygonBuffer *polygon_buff,
                        Bool absolute, Bool pen_down,
                        Bool pen_selected, HPGL2Point *point)
{
  PATHINFO *polygon_buffer = &polygon_buff->polygon_buffer ;
  SYSTEMVALUE coords[2];

  UNUSED_PARAM(Bool, pen_selected);

  /* NB : point is in plotter coordinates at this point. The polygon
   * buffer only contains plotter units coords. */
  coords[0] = point->x;
  coords[1] = point->y;

  if ( hpgl2_start_of_sub_path(polygon_buff) ) {
    if ( ! gs_moveto(absolute, coords, polygon_buffer) )
      return FALSE ;

     hpgl2_sub_path_started(polygon_buff);
     return TRUE;
  }
  else
      return gs_lineto(absolute, pen_down, coords, polygon_buffer);

}

Bool hpgl2_polygon_current_point(HPGL2PolygonBuffer *polygon_buff,
                                 SYSTEMVALUE *x, SYSTEMVALUE *y)
{
  return gs_currentpoint(&polygon_buff->polygon_buffer, x, y);
}

/* Use hpgl2_polygon_moveto to directly manipulate polygon buffer. */
Bool hpgl2_polygon_moveto(HPGL2PolygonBuffer *polygon_buff, HPGL2Point *point, Bool absolute)
{
  SYSTEMVALUE coords[2];

  coords[0] = point->x;
  coords[1] = point->y;
  return gs_moveto(absolute, coords, &polygon_buff->polygon_buffer);
}

/* fill polygon_buffer will not disturb the pen position, which is always
 * the current point in the gsate.
 */
Bool fill_polygon_buffer( PCL5Context *pcl5_ctxt,
                          int32 fill_mode)
{
  PATHINFO *the_path;
  HPGL2PolygonBuffer *polygon_buff = get_hpgl2_polygon_buffer(pcl5_ctxt);
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  uint8 rop = getPclRop();
  Bool res = TRUE;

  the_path = &polygon_buff->polygon_buffer;

  HQASSERT(fill_mode == NZFILL_TYPE || fill_mode == EOFILL_TYPE,
      "Bad fill type");

  /* No need to draw anything for the destination rop */
  if (rop == PCL_ROP_D) {
    /** \todo Confirm whether we actually need to ensure the pen is
     * in the palette range in this case, (as we would normally
     * through hpgl2_sync_pen_color).
     */
    hpgl2_ensure_pen_in_palette_range(pcl5_ctxt) ;
    return TRUE ;
  }

  hpgl2_sync_fill_mode(pcl5_ctxt, FALSE);

  /* hatching fills require we draw actual lines rather than blit out
   * patterns. So hatching is a special case.
   * path to be filled will act as clip path over which hatching is to be
   * drawn.
   */
  if ( linefill_info->fill_params.fill_type == HPGL2_FILL_CROSSHATCH
      || linefill_info->fill_params.fill_type == HPGL2_FILL_HATCH ) {
    res = hpgl2_hatchfill_path(pcl5_ctxt, the_path, fill_mode, TRUE);
  }
  else {
    if (! linefill_info->transparency &&
        (rop == PCL_ROP_TSo || rop == PCL_ROP_T)) {
      switch (linefill_info->fill_params.fill_type) {
        case HPGL2_FILL_USER:
        case HPGL2_FILL_PCL_CROSSHATCH:
        case HPGL2_FILL_PCL_USER:
        case HPGL2_FILL_SHADING: {
          IPOINT origin = {0, 0};

          setPclSourceTransparent(FALSE);
          setPclPatternTransparent(FALSE);
          setPcl5Pattern(NULL, 0, 0, &origin, NULL);
          setPclRop(PCL_ROP_TSo);
          set_white();
          if (! setPclForegroundSource(pcl5_ctxt->corecontext->page,
                                       PCL_DL_COLOR_IS_FOREGROUND))
            return FALSE ;

          res = dofill(the_path, fill_mode, GSC_FILL,
                       FILL_NORMAL|FILL_POLYCACHE);

          hpgl2_sync_fill_mode(pcl5_ctxt, FALSE);
          setPclSourceTransparent(TRUE);
          setPclPatternTransparent(TRUE);
          setPclRop(rop);
          break;
        }
      }
    }
    if (res)
      res = dofill(the_path, fill_mode, GSC_FILL, FILL_NORMAL|FILL_POLYCACHE);
  }

  /* We may have messed with the transparency settings, so restore them now. */
  hpgl2_sync_transparency(linefill_info);

  /* filling the polygon might disturb the PCL pattern associated to
   * the pen color. We assume that the pen settings are the "default"
   * and that fill operation should be "atomic" and not make persistent
   * alterations to parts of gstate associated to the pen.
   */
  hpgl2_sync_pen_color(pcl5_ctxt, FALSE);
  return res;
}

/** \todo draw_polygon_buffer really should be integrated into the
 * hpgl2_stroke functionality.
 */
/** \todo How do deal with the handling of residuals for different
 * part of a polygon? Polygon might contain circles and lines.
 * Also, residual is to be saved/restored around PM command - but what about
 * EP command?
 */
Bool draw_polygon_buffer(PCL5Context *pcl5_ctxt)
{
  HPGL2PolygonBuffer *polygon_buff = get_hpgl2_polygon_buffer(pcl5_ctxt);
  HPGL2LineFillInfo *linefill_info = get_hpgl2_line_fill_info(pcl5_ctxt);
  STROKE_PARAMS params ;
  PATHINFO *path_info = &polygon_buff->polygon_buffer ;
  uint8 rop = getPclRop();

  /* No need to draw anything for the destination rop */
  if (rop == PCL_ROP_D) {
    /* TODO Confirm whether we actually need to ensure the pen is
     * in the palette range in this case, (as we would normally
     * through hpgl2_sync_pen_color).
     */
    hpgl2_ensure_pen_in_palette_range(pcl5_ctxt) ;
    return TRUE ;
  }

  set_gstate_stroke(&params, path_info, NULL, FALSE) ;

  /* Line type residue is not updated in CI, EA, EP, ER, EW, FPP, PM, RA, RR, WG ops. */
  if ( 1 <= linefill_info->line_type.type && linefill_info->line_type.type <= 8 ) {
    params.linestyle.dashoffset += (USERVALUE)linefill_info->line_type.residue ;
  }

  /* Use butt cap and no join for lines 0.35mm or less. */
  hpgl2_override_thin_line_attributes(pcl5_ctxt, &params.linestyle) ;

  hpgl2_sync_pen_color(pcl5_ctxt, FALSE);

  if (! linefill_info->transparency &&
      (rop == PCL_ROP_TSo || rop == PCL_ROP_T)) {
    switch (linefill_info->screened_vectors.fill_type) {
      case HPGL2_FILL_USER:
      case HPGL2_FILL_PCL_CROSSHATCH:
      case HPGL2_FILL_PCL_USER:
      case HPGL2_FILL_SHADING: {
        IPOINT origin = {0, 0};

        setPclSourceTransparent(FALSE);
        setPclPatternTransparent(FALSE);
        setPclRop(PCL_ROP_TSo);
        setPcl5Pattern(NULL, 0, 0, &origin, NULL);

        set_white();
        if (! setPclForegroundSource(pcl5_ctxt->corecontext->page,
                                     PCL_DL_COLOR_IS_FOREGROUND))
          return FALSE ;

        if (! hpgl2_stroke_internal(&params, STROKE_NORMAL) )
          return FALSE ;

        hpgl2_sync_pen_color(pcl5_ctxt, FALSE);
        setPclSourceTransparent(TRUE);
        setPclPatternTransparent(TRUE);
        setPclRop(rop);
        break;
      }
    }
  }

  /* drawing the polygon buffer does not clear the path. */
  if ( !hpgl2_stroke_internal(&params, STROKE_NORMAL) )
    return FALSE ;

  /* We may have messed with the transparency settings, so restore them now. */
  hpgl2_sync_transparency(linefill_info);

  return TRUE;
}

Bool hpgl2_polygon_curveto(HPGL2PolygonBuffer *polygon_buff,
                           HPGL2Point *p1,HPGL2Point *p2, HPGL2Point * p3,
                           Bool absolute, Bool stroked, Bool pen_selected)
{
  SYSTEMVALUE args[6];

  UNUSED_PARAM(Bool, pen_selected);

  args[0] = p1->x; args[1] = p1->y;
  args[2] = p2->x; args[3] = p2->y;
  args[4] = p3->x; args[5] = p3->y;
  return gs_curveto( absolute, stroked, args, &polygon_buff->polygon_buffer);
}

Bool hpgl2_polygon_closepath(HPGL2PolygonBuffer *polygon_buff, Bool pen_down)
{
  Bool res = TRUE;
  /* Need to accont for the fact that polygon buffer lines may be
   * unstroked. This function duplicates most of the guts of path_close,
   * but allows the closing segment to be unstroked.
   */

  LINELIST *theline = polygon_buff->polygon_buffer.lastline ;

  if ( ! theline )
    return TRUE ;

  if ( ! polygon_buff->sub_path_started )
    return TRUE;

  switch ( theILineType( theline )) {
  case MYCLOSE :
    theILineType( theline ) = (uint8) CLOSEPATH ;
    /* FALLTHRU */
  case CLOSEPATH : /* FALL THROUGH */
  default:
    theline = theISubPath( polygon_buff->polygon_buffer.lastpath ) ;
     res = path_segment( theX( theIPoint( theline )),
                         theY( theIPoint( theline )),
                         CLOSEPATH, pen_down, &polygon_buff->polygon_buffer ) ;
  }

  /* record that there is no "open" subpath */
  polygon_buff->sub_path_started = FALSE;
  return res;
}


/* Log stripped */
