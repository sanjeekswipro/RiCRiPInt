/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:gu_rect.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rectangle optimisations.
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"  /* For OBJECT struct */
#include "namedef_.h"

#include "bitblts.h"  /* For FORM struct */
#include "bitblth.h"
#include "matrix.h"   /* For MATRIX, for graphics.h */
#include "graphics.h" /* For PATHLIST */
#include "gu_path.h"  /* For thecpath global variable */
#include "gstate.h"   /* For gstate global variable */
#include "clipops.h"  /* For gs_addclip() function */
#include "ndisplay.h" /* For NFILLOBJECT struct */
#include "routedev.h" /* For DEVICE_SETG() macro */
#include "plotops.h"  /* For degenerateClipping global variable */
#include "pathops.h"  /* For STOKE_PARAMS structure */
#include "gu_ctm.h"   /* For gs_modifyctm() function */
#include "pathcons.h"
#include "render.h"

#include "idlom.h"

#include "gu_rect.h"
#include "system.h"



/* ----- Static function declarations ----- */
static Bool set_up_rect_paths( PATHINFO *header, PATHLIST  **ret_path ,
                               LINELIST  **ret_lines , RECTANGLE *prects ,
                               int32 nrects ) ;


/**
   Utility for clipping rectangles.
   A path list containing the nrects subpaths of closed rectangles is
   constructed, and then handed to the gs_addclip routine.
*/
Bool cliprectangles( RECTANGLE *prects , int32 nrects )
{
  int32       result ;
  PATHINFO header ;
  PATHLIST   *path ;
  LINELIST   *lines ;

  if (! set_up_rect_paths( &header, &path , &lines , prects , nrects ))
    return FALSE ;

  ( void )gs_newpath() ;        /* clear current path */

  result = gs_addclip( NZFILL_TYPE, &header , TRUE ) ;
  mm_free_with_header(mm_pool_temp, path ) ;
  mm_free_with_header(mm_pool_temp, lines ) ;

  return  result  ;
}

/** Utility for filling rectangles. */
Bool fillrectdisplay( DL_STATE *page, dbbox_t *bbox )
{
  register int32 x1 , y1 , x2 , y2 ;
  render_blit_t *rb ;
  charcontext_t *charcontext ;

  UNUSED_PARAM(DL_STATE*, page);
  charcontext = char_current_context() ;
  HQASSERT(charcontext, "No character context") ;

  rb = charcontext->rb ;
  HQASSERT(RENDER_BLIT_CONSISTENT(rb),
           "Character caching state not consistent") ;
  bbox_load(bbox, x1, y1, x2, y2) ;

  /* General rectangular clipping. */
  if ( !bbox_intersects_coordinates(&rb->p_ri->clip, x1, y1, x2, y2) )
    return TRUE ;

  /* Check for degenerate clipping. */
  if ( degenerateClipping )     /* probably duplicated work! */
    return TRUE ;

  bbox_clip_x(&rb->p_ri->clip, x1, x2);
  bbox_clip_y(&rb->p_ri->clip, y1, y2);

  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                               y1 * theFormL(*rb->outputform)) ;
  if ( clipmapid >= 0 ) {
    rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
                                 y1 * theFormL(*rb->clipform)) ;
  }

  DO_BLOCK(rb, y1 , y2 , x1 , x2) ;

  return TRUE ;
}

/**
   Call routine to stroke a rectangle for each rectangle in prects.
   The stuff done in the setup is a by and large code from
   dostroke().
*/
Bool strokerectangles( int32 colorType ,
                        RECTANGLE *prects ,
                        int32 nrects ,
                        OMATRIX *adjustment_mptr )
{
  PATHLIST    *path ;
  LINELIST    *lines ;
  Bool        result ;
  STROKE_PARAMS params ;
  PATHINFO    header ;

  if ( ! set_up_rect_paths( & header , & path , & lines , prects , nrects ))
    return FALSE ;

  /* Note that we can't just patch in path for charpaths */
  set_gstate_stroke( & params , & header , NULL , TRUE ) ;

  if ( adjustment_mptr ) {
    params.usematrix = TRUE ;
    matrix_mult( adjustment_mptr , & thegsPageCTM(*gstateptr) , & params.orig_ctm ) ;
  }

  /* For now call dostroke with dovignettedetection as FALSE. This is because
   * vignette detection currently only handles that of simple stroked paths,
   * (in fact ones consisting only of single line segments), and even this
   * is mainly to handle legacy applications. Therefore have still left the
   * flush_vignette call in the rectstroke operator. May change later.
   */
  result = dostroke( & params , colorType , STROKE_NOT_VIGNETTE ) ;

  mm_free_with_header(mm_pool_temp, path ) ;
  mm_free_with_header(mm_pool_temp, lines ) ;

  return result ;
}

/**
   This routine allocs enough PATHLIST structures for the number of rectangles
   and enough LINELIST structures for each rectangle. It hooks them all up to
   make a valid path, and initializes all the points in the path.
*/
static Bool set_up_rect_paths( PATHINFO *header, PATHLIST **ret_path ,
                               LINELIST  **ret_lines , RECTANGLE *prects ,
                               int32 nrects )
{
  PATHLIST *path , *ppathlist ;
  LINELIST *lines , *plinelist ;
  int32    i ;
  register SYSTEMVALUE x, y, w, h ;
  register SYSTEMVALUE x1, y1, x2, y2, x3, y3 ;

  /* allocate enough temporary memory to hold linelists for all
   * the rectangles.
   */
  if ( NULL == (lines = (LINELIST *)mm_alloc_with_header(mm_pool_temp,
                                                         nrects * 5 * sizeof( LINELIST ),
                                                         MM_ALLOC_CLASS_LINELIST)))
    return error_handler( VMERROR ) ;
  if ( NULL == (path = (PATHLIST *)mm_alloc_with_header(mm_pool_temp,
                                                         nrects * sizeof( PATHLIST ),
                                                         MM_ALLOC_CLASS_PATHLIST))) {
    mm_free_with_header(mm_pool_temp, lines ) ;
    return error_handler( VMERROR ) ;
  }

  header->charpath_id = 0 ; /* it is not a charpath */
  ppathlist = path ;
  plinelist = lines ;
  for ( i = 0 ; i < nrects ; i++ , prects++ ) {

    theISubPath( ppathlist ) = plinelist ;
    ppathlist->next = ppathlist + 1 ;
    theISystemAlloc( ppathlist ) = PATHTYPE_STRUCT ;
    ppathlist->shared = 0 ;  /* Zero the shared count */

    /* make rectangle counter clockwise */
    x = prects->x ;
    y = prects->y ;
    if (( w = prects->w ) < 0.0 ) {
      w = - w ;
      x -= w ;
    }
    if (( h = prects->h ) < 0.0 ) {
      h = - h ;
      y -=  h ;
    }

    /* transform rectangle */
    if ( thegsPageCTM(*gstateptr).opt != MATRIX_OPT_BOTH ) {
      MATRIX_TRANSFORM_XY( x, y, x1, y1, & thegsPageCTM(*gstateptr)) ;
      MATRIX_TRANSFORM_DXY( w, h, x3, y3, & thegsPageCTM(*gstateptr)) ;
      x3 += x1 ;
      y3 += y1 ;
      x2 = x3 ;
      y2 = y1 ;
    }
    else {
      MATRIX_TRANSFORM_XY( x, y, x1, y1, & thegsPageCTM(*gstateptr)) ;
      MATRIX_TRANSFORM_DXY( w , 0.0, x2, y2, & thegsPageCTM(*gstateptr)) ;
      x2 += x1 ;
      y2 += y1 ;
      MATRIX_TRANSFORM_DXY( 0.0, h , x3, y3, & thegsPageCTM(*gstateptr)) ;
      x3 += x2 ;
      y3 += y2 ;
    }

    theILineType( plinelist ) = MOVETO ;
    theILineOrder( plinelist ) = ( uint8 )0 ;
    INIT_LINELIST_FLAGS( plinelist ) ;
    theX( theIPoint( plinelist )) = x1 ;
    theY( theIPoint( plinelist )) = y1 ;
    plinelist->next = plinelist + 1 ;
    theISystemAlloc( plinelist ) = PATHTYPE_STRUCT ;
    plinelist++ ;

    theILineType( plinelist ) = LINETO ;
    theILineOrder( plinelist ) = ( uint8 )0 ;
    INIT_LINELIST_FLAGS( plinelist ) ;
    theX( theIPoint( plinelist )) = x2 ;
    theY( theIPoint( plinelist )) = y2 ;
    plinelist->next = plinelist + 1 ;
    theISystemAlloc( plinelist ) = PATHTYPE_STRUCT ;
    plinelist++ ;

    theILineType( plinelist ) = LINETO ;
    theILineOrder( plinelist ) = ( uint8 )0 ;
    INIT_LINELIST_FLAGS( plinelist ) ;
    theX( theIPoint( plinelist )) = x3 ;
    theY( theIPoint( plinelist )) = y3 ;
    plinelist->next = plinelist + 1 ;
    theISystemAlloc( plinelist ) = PATHTYPE_STRUCT ;
    plinelist++ ;

    theILineType( plinelist ) = LINETO ;
    theILineOrder( plinelist ) = ( uint8 )0 ;
    INIT_LINELIST_FLAGS( plinelist ) ;
    theX( theIPoint( plinelist )) = x1 - ( x2 - x3 ) ;
    theY( theIPoint( plinelist )) = y1 - ( y2 - y3 ) ;
    plinelist->next = plinelist + 1 ;
    theISystemAlloc( plinelist ) = PATHTYPE_STRUCT ;
    plinelist++ ;

    theILineType( plinelist ) = CLOSEPATH ;
    theILineOrder( plinelist ) = ( uint8 )0 ;
    INIT_LINELIST_FLAGS( plinelist ) ;
    theX( theIPoint( plinelist )) = x1 ;
    theY( theIPoint( plinelist )) = y1 ;
    plinelist->next = NULL ;
    theISystemAlloc( plinelist ) = PATHTYPE_STRUCT ;
    plinelist++ ;

    ppathlist++ ;
  }

  /* reset next pointer in the last path list to NULL */
  path[ nrects - 1 ].next = NULL ;

  header->firstpath = path ;
  header->lastpath = ppathlist - 1 ;
  header->lastline = plinelist - 1 ;
  header->bboxtype = BBOX_NOT_SET;
  header->curved = FALSE;
  header->flags = PATHINFO_CLEAR_FLAGS;
  header->protection = PROTECTED_NONE;

  *ret_path = path ;
  *ret_lines = lines ;
  return TRUE ;
}

/* Log stripped */
