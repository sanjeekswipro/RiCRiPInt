/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:stickarc.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements stick and arc fonts for HPGL.
 */

#include "core.h"

#include "stickarc.h"
#include "adjusttab.h"
#include "arcwidth.h"
#include "strokeoffset.h"
#include "stroketab.h"
#include "hpgl2vector.h"

#include "swerrors.h"
#include "control.h"
#include "gstate.h"
#include "gstack.h"
#include "graphics.h"
#include "pathcons.h"
#include "gu_cons.h"
#include "constant.h"
#include "pathops.h"
#include "mathfunc.h"
#include "gu_ctm.h"
#include "gu_fills.h"
#include "params.h"

/* Special flags in the stroke table. */
enum {
  RETURN_TO_ORIGIN   = 125,
  COMPOUND_CHAR_FLAG = 126,
  PENUP_FLAG         = 127,
  ALT_PENUP_FLAG     = 128,
  ARC_FLAG           = 129,
  CIRCLE_FLAG        = 130
} ;

/* Point size is 48 in grid space, as defined in the HP DesignJet Language Guide. */
#define GRID_POINT_SIZE (48.0)
#define STICK_SPACE     (48)

/* Made up numbers based on what looks good! */
/** \todo The numbers for arc fonts should probably be reviewed.
 *  It is possible that some may end up being shared with stick
 *  fonts, but as the stick font values have been fine tuned,
 *  and it is not known how good the values for arc fonts are,
 *  keep them separate for now.
 */
#define ARC_SPACE             (10) /* In addition to the proportional amount given by Arc_width */
#define ARC_ASPECT_RATIO      (0.7)
#define ARC_SIZE_ADJUSTMENT   (0.885)

typedef int16 Offset ;
typedef uint8 Element ;
typedef int8 Coord ;
typedef int8 Angle ;

static Bool move_to(OMATRIX *font_matrix, SYSTEMVALUE dx, SYSTEMVALUE dy)
{
  SYSTEMVALUE args[2] ;

  MATRIX_TRANSFORM_DXY(dx, dy, args[0], args[1], font_matrix) ;

  return gs_moveto(FALSE, args, &gstateptr->thepath) ;
}

static Bool draw_to(OMATRIX *font_matrix, SYSTEMVALUE dx, SYSTEMVALUE dy)
{
  SYSTEMVALUE args[2] ;

  MATRIX_TRANSFORM_DXY(dx, dy, args[0], args[1], font_matrix) ;

  return gs_lineto(FALSE, TRUE, args, &gstateptr->thepath) ;
}

static Bool arc_inside_char(OMATRIX *font_matrix, SYSTEMVALUE dx, SYSTEMVALUE dy,
                            SYSTEMVALUE angle)
{
  SYSTEMVALUE tan_angle, cos_angle ;
  SYSTEMVALUE x0, y0, x1, y1, x2, y2, factor ;
  SYSTEMVALUE args[6] ;

  if ( dx == 0 && dy == 0 )
    return TRUE ;

  if ( angle == 0 )
    return draw_to(font_matrix, dx, dy) ;

  /* Subdivide into two beziers if the angle is 90 degrees or more. */
  if ( fabs(angle) >= 90 ) {
    /* Should be 0.5, but this value looks better!  Remember the angles are
       stored as integers in stroketab.h, so nothing is particularly accurate. */
    angle *= 0.49 ;
    tan_angle = tan(angle * DEG_TO_RAD) ;
    x1 = 0.5 * (dx - dy * tan_angle) ;
    y1 = 0.5 * (dy + dx * tan_angle) ;
    return arc_inside_char(font_matrix, x1, y1, angle) &&
           arc_inside_char(font_matrix, dx-x1, dy-y1, angle) ;
  }

  /* x0,y0 is the starting point (all coords are relative) */
  x0 = 0 ;
  y0 = 0 ;

  /* x1,y1 is the intersection of tangents */
  tan_angle = tan(angle * DEG_TO_RAD) ;
  x1 = 0.5 * (dx - dy * tan_angle) ;
  y1 = 0.5 * (dy + dx * tan_angle) ;

  /* x2,y2 is the last point */
  x2 = dx ;
  y2 = dy ;

  /* factor is used to calculate the bezier mid control points */
  cos_angle = cos(angle * DEG_TO_RAD) ;
  factor = (4 * cos_angle) / (3 * (1 + cos_angle)) ;

  args[0] = x0 + factor * (x1 - x0) ;
  args[1] = y0 + factor * (y1 - y0) ;
  args[2] = x2 + factor * (x1 - x2) ;
  args[3] = y2 + factor * (y1 - y2) ;
  args[4] = x2 ;
  args[5] = y2 ;

  MATRIX_TRANSFORM_DXY(args[0], args[1], args[0], args[1], font_matrix) ;
  MATRIX_TRANSFORM_DXY(args[2], args[3], args[2], args[3], font_matrix) ;
  MATRIX_TRANSFORM_DXY(args[4], args[5], args[4], args[5], font_matrix) ;

  if ( !gs_curveto(FALSE, TRUE, args, &gstateptr->thepath) )
    return FALSE ;

  return TRUE ;
}

static Bool circle_inside_char(OMATRIX *font_matrix, SYSTEMVALUE dx, SYSTEMVALUE dy)
{
  /* Represent the circle by four cubic beziers. */
  return arc_inside_char(font_matrix, dx - dy, dy + dx, 45) &&
         arc_inside_char(font_matrix, dy + dx, dy - dx, 45) &&
         arc_inside_char(font_matrix, dy - dx,-dy - dx, 45) &&
         arc_inside_char(font_matrix,-dx - dy, dx - dy, 45) ;
}

static Bool strokepath_char(void)
{
  STROKE_PARAMS sparams ;

  set_gstate_stroke(&sparams, &gstateptr->thepath, &gstateptr->thepath, FALSE) ;

  if ( !hpgl2_stroke_internal(&sparams, STROKE_NOT_VIGNETTE) )
    return FALSE ;

  return TRUE ;
}

static Bool fill_char(void)
{
  if ( !dofill(&gstateptr->thepath, NZFILL_TYPE, GSC_FILL, FILL_NORMAL) )
    return FALSE;

  if ( !gs_newpath() )
    return FALSE ;

  return TRUE ;
}

static Bool parse_char_elements(int32 char_id, Bool fixedspace,
                                SYSTEMVALUE origin[2], OMATRIX *font_matrix)
{
  Offset i, offset, num_elements ;
  const Element *elements ;
  Bool pen_down = FALSE ;

  i = fixedspace ? 0 /* Stick */ : 1 /* Arc */ ;
  offset = Stroke_offset[i][char_id] ;
  num_elements = Stroke_offset[i][char_id + 1] - offset ;
  elements = &Stroke_table[offset] ;

  for ( i = 0 ; i < num_elements ; ++i ) {
    Element element = (Element)elements[i] ;

    switch ( element ) {
    case RETURN_TO_ORIGIN :
      if ( !gs_moveto(TRUE, origin, &gstateptr->thepath) )
        return FALSE ;

      pen_down = FALSE ;
      break ;
    case COMPOUND_CHAR_FLAG :
      HQASSERT(i + 2 < num_elements, "Expected at least two more elements") ;

      char_id = (int32)elements[++i] ;

      if ( !parse_char_elements(char_id, fixedspace, origin, font_matrix) )
        return FALSE ;

      if ( !gs_moveto(TRUE, origin, &gstateptr->thepath) )
        return FALSE ;

      char_id = (int32)elements[++i] ;

      if ( !parse_char_elements(char_id, fixedspace, origin, font_matrix) )
        return FALSE ;

      pen_down = TRUE ;
      break ;
    case PENUP_FLAG :
    case ALT_PENUP_FLAG :
      pen_down = FALSE ;
      break ;
    case ARC_FLAG : {
      Coord coord[2] ;
      Angle angle ;

      HQASSERT(i + 3 < num_elements, "Expected at least three more elements") ;

      coord[0] = (Coord)elements[++i] ;
      coord[1] = (Coord)elements[++i] ;
      angle = (Angle)elements[++i] ;

      /* The angles in the font table are wrong!  Emulate buggy
         Agfa code by multiplying the angle by the aspect ratio :-) */
      if ( !arc_inside_char(font_matrix, coord[0], coord[1], angle * (1.0 / ARC_ASPECT_RATIO)) )
        return FALSE ;

      pen_down = TRUE ;
      break ;
     }
    case CIRCLE_FLAG : {
      Coord coord[2] ;

      HQASSERT(i + 2 < num_elements, "Expected at least two more elements") ;

      coord[0] = (Coord)elements[++i] ;
      coord[1] = (Coord)elements[++i] ;

      if ( !circle_inside_char(font_matrix, coord[0], coord[1]) )
        return FALSE ;

      pen_down = TRUE ;
      break ;
     }
    default : {
      Coord coord[2] ;

      HQASSERT(i + 1 < num_elements, "Expected at least one more elements") ;

      coord[0] = element ;
      coord[1] = (Coord)elements[++i] ;

      if ( pen_down ) {
        if ( !draw_to(font_matrix, coord[0], coord[1]) )
          return FALSE ;
      } else {
        if ( !move_to(font_matrix, coord[0], coord[1]) )
          return FALSE ;
        pen_down = TRUE ;
      }
      break ;
     }
    }
  }

  return TRUE ;
}

/** stickarc_plotchar:
    Stick is fixed space and Arc is proportional.
    Roman-8 symbol set is assumed.
    Supports DOSHOW, DOCHARPATH and DOSTRINGWIDTH.
    The advance is returned in device space.
 */
Bool stickarc_plotchar(int32 char_id, OMATRIX *transform,
                       Bool fixedspace, int32 showtype,
                       SYSTEMVALUE pointsize, FVECTOR *advance)
{
  Bool result = FALSE ;
  LINESTYLE linestyle = gstateptr->thestyle ;
  uint8 scanconversion = gstateptr->thePDEVinfo.scanconversion ;
  SYSTEMVALUE origin[2], sx, sy ;
  OMATRIX font_matrix ;

  if ( char_id < 0 || char_id > 255 ) {
    advance->x = advance->y = 0 ;
    return TRUE ; /* ignore */
  }

  if (fixedspace) {
    sx = (1.0 / GRID_POINT_SIZE) * STICK_CHARACTER_ADVANCE(pointsize) ;
    sy = (1.0 / GRID_POINT_SIZE) * STICK_CHARACTER_HEIGHT(pointsize) ;
  }
  else {
    sy = (1.0 / GRID_POINT_SIZE) * pointsize ;
    sy *= ARC_SIZE_ADJUSTMENT ;
    sx = sy * ARC_ASPECT_RATIO ;
  }

  matrix_set_scale(&font_matrix, sx, sy) ;
  matrix_mult(&font_matrix, transform, &font_matrix) ;

  /* The advance is returned in device space. */
  advance->x = fixedspace ? STICK_SPACE : (Arc_width[char_id] + ARC_SPACE) ;
  advance->y = 0 ;
  MATRIX_TRANSFORM_DXY(advance->x, advance->y, advance->x, advance->y,
                       &font_matrix) ;
  MATRIX_TRANSFORM_DXY(advance->x, advance->y, advance->x, advance->y,
                       &gstateptr->thePAGEinfo.thectm) ;

  switch ( showtype ) {
  case DOSHOW :
  case DOCHARPATH :
    break ;
  case DOSTRINGWIDTH :
    return TRUE ; /* only need the advance, nothing more to do */
  default :
    HQFAIL("stickarc_plotchar supports DOSHOW, DOCHARPATH and DOSTRINGWIDTH only") ;
    return error_handler(UNREGISTERED) ;
  }

  if ( !gs_currentpoint(&gstateptr->thepath, &origin[0], &origin[1]) )
    return FALSE ;

  gstateptr->thestyle.startlinecap =
    gstateptr->thestyle.endlinecap =
    gstateptr->thestyle.dashlinecap = ROUND_CAP ;
  gstateptr->thestyle.linejoin = ROUND_JOIN ;

  gstateptr->thePDEVinfo.scanconversion = UserParams.CharScanConversion ;

#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!

  if ( !parse_char_elements(char_id, fixedspace, origin, &font_matrix) )
    goto cleanup ;

  if ( !strokepath_char() )
    goto cleanup ;

  if ( showtype == DOSHOW ) {
    if ( !fill_char() )
      goto cleanup ;
  }

  if ( !gs_moveto(TRUE, origin, &gstateptr->thepath) )
    goto cleanup ;

  result = TRUE ;
 cleanup :
  gstateptr->thestyle = linestyle ;
  gstateptr->thePDEVinfo.scanconversion = scanconversion ;

#undef return
  return TRUE ;
}

/* =============================================================================
* Log stripped */
