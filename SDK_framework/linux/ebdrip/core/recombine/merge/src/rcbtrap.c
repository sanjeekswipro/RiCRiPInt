/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:rcbtrap.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine traps
 */

#include "core.h"
#include "mm.h"
#include "swerrors.h"

#include "display.h"
#include "dlstate.h"
#include "gstack.h"

#include "pathops.h"
#include "panalyze.h"
#include "gu_path.h"  /* BBOX_NOT_SET */
#include "params.h" /* UserParams.RecombineTrapWidth */
#include "rcbtrap.h"
#include "vndetect.h"

#if defined( ASSERT_BUILD )
static Bool debug_rcbtrap = FALSE ;
#endif

/* -------------------------------------------------------------------------- */
#if defined( ASSERT_BUILD )
Bool rcbt_asserttrap( RCBTRAP *rcbtrap )
{
  HQASSERT( rcbtrap , "rcbtrap NULL in rcbt_asserttrap" ) ;

  HQASSERT( rcbtrap->type == RCBTRAP_LINE ||
            rcbtrap->type == RCBTRAP_RECT ||
            rcbtrap->type == RCBTRAP_OVAL ||
            rcbtrap->type == RCBTRAP_OCT ||
            rcbtrap->type == RCBTRAP_RND ||
            rcbtrap->type == RCBTRAP_REV ||
        rcbtrap->type == RCBTRAP_DONUT ,
        "Unknown recombine trap type" ) ;

  return TRUE ;
}
#endif

/* -------------------------------------------------------------------------- */
RCBTRAP *rcbt_alloctrap(mm_pool_t *pools, int32 traptype)
{
  switch ( traptype ) {
  case RCBTRAP_LINE:
  case RCBTRAP_RECT:
  case RCBTRAP_OVAL:
  case RCBTRAP_DONUT:
    return dl_alloc(pools,
           sizeof(RCBTRAP) - sizeof(RCBSHAPE) + sizeof(RCBSHAPE1),
           MM_ALLOC_CLASS_RCB_TRAP);
  case RCBTRAP_OCT:
  case RCBTRAP_RND:
  case RCBTRAP_REV:
    return dl_alloc(pools,
           sizeof(RCBTRAP) - sizeof(RCBSHAPE) + sizeof(RCBSHAPE2),
           MM_ALLOC_CLASS_RCB_TRAP);
  default:
    HQFAIL( "Unknown trap type to alloc" ) ;
    return NULL ;
  }
  /* NOT REACHED */
}

/* -------------------------------------------------------------------------- */
void rcbt_freetrap(mm_pool_t *pools, RCBTRAP *rcbtrap)
{
  HQASSERT( rcbt_asserttrap( rcbtrap ) , "" ) ;
  switch ( rcbtrap->type ) {
  case RCBTRAP_LINE:
  case RCBTRAP_RECT:
  case RCBTRAP_OVAL:
  case RCBTRAP_DONUT:
    dl_free(pools, (mm_addr_t)rcbtrap,
            sizeof(RCBTRAP) - sizeof(RCBSHAPE) + sizeof(RCBSHAPE1),
            MM_ALLOC_CLASS_RCB_TRAP);
    break ;
  case RCBTRAP_OCT:
  case RCBTRAP_RND:
  case RCBTRAP_REV:
    dl_free(pools, (mm_addr_t)rcbtrap,
            sizeof(RCBTRAP) - sizeof(RCBSHAPE) + sizeof(RCBSHAPE2),
            MM_ALLOC_CLASS_RCB_TRAP);
    break ;
  default:
    HQFAIL( "Unknown trap type to free" ) ;
  }
}

/* -------------------------------------------------------------------------- */
Bool rcbt_addtrap(mm_pool_t *pools, NFILLOBJECT *nfill, PATHINFO *path,
                  int32 fDonut, RCBTRAP *src_rcbtrap)
{
  sbbox_t *bbox ;
  RCBTRAP *dst_rcbtrap ;
  RCBTRAP tmp_rcbtrap ;

  HQASSERT( nfill != NULL , "nfill NULL in rcbt_addtrap" ) ;
  HQASSERT( path  != NULL , "path  NULL in rcbt_addtrap" ) ;

  if ( src_rcbtrap == NULL ) {
    src_rcbtrap = & tmp_rcbtrap ;
    src_rcbtrap->type = RCBTRAP_NOTTESTED ;
  }

  /* First of all check for a rectangle or circle and obtain the
   * 'width'/'height' vectors.
   */
  if ( src_rcbtrap->type == RCBTRAP_NOTTESTED ) {
    int32 degenerate , orientation , type ;
    if ( ! pathisacircle( path , & degenerate , & orientation , & type ,
                          src_rcbtrap ) &&
         ! pathisarectangle( path , & degenerate , & orientation , & type ,
                             src_rcbtrap ) &&
         ! pathiscorneredrect( path , & degenerate , & orientation , & type ,
                               src_rcbtrap ))
      return TRUE ;
    if ( fDonut ) {
      if ( src_rcbtrap->type != RCBTRAP_OVAL )
        /* Actually not a donut therefore ignore it */
        return TRUE ;
      src_rcbtrap->type = RCBTRAP_DONUT ;
    }
  }
  else if ( src_rcbtrap->type == RCBTRAP_UNKNOWN ) {
    int32 degenerate , orientation , type ;
    if ( fDonut || ! pathiscorneredrect( path ,
               & degenerate , & orientation , & type , src_rcbtrap ))
      return TRUE ;
  }

  HQASSERT( src_rcbtrap->type != RCBTRAP_NOTTESTED ,
          "type should now not be RCBTRAP_NOTTESTED" ) ;
  HQASSERT( src_rcbtrap->type != RCBTRAP_UNKNOWN ,
          "type should now not be RCBTRAP_UNKNOWN" ) ;

  /* Second determine the center point. */
  bbox = & path->bbox ;
  HQASSERT( path->bboxtype != BBOX_NOT_SET ,
            "bbox of vn detected paths or clip paths should be set" ) ;
  src_rcbtrap->x = ( float )( 0.5 * ( bbox->x1 + bbox->x2 )) ;
  src_rcbtrap->y = ( float )( 0.5 * ( bbox->y1 + bbox->y2 )) ;

  dst_rcbtrap = rcbt_alloctrap(pools, src_rcbtrap->type) ;
  if ( dst_rcbtrap == NULL )
    return error_handler(VMERROR);

  /* Note: The center point is in device space,
     but the vectors are in default user space */

  dst_rcbtrap->type = src_rcbtrap->type ;
  dst_rcbtrap->x = src_rcbtrap->x ;
  dst_rcbtrap->y = src_rcbtrap->y ;
  switch ( src_rcbtrap->type ) {
  case RCBTRAP_LINE:
  case RCBTRAP_RECT:
  case RCBTRAP_OVAL:
  case RCBTRAP_DONUT:
    dst_rcbtrap->u.s1 = src_rcbtrap->u.s1 ;
    break ;
  case RCBTRAP_OCT:
  case RCBTRAP_RND:
  case RCBTRAP_REV:
    dst_rcbtrap->u.s2 = src_rcbtrap->u.s2 ;
    break ;
  default:
    HQFAIL( "Unknown trap type to fill in" ) ;
    return FALSE ;
  }

  HQASSERT( nfill->rcbtrap == NULL , "Already filled in rcbtrap field" ) ;
  nfill->rcbtrap = dst_rcbtrap ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcbt_comparevecs( RCB2DVEC *v1 , RCB2DVEC *v2 )
{
  double dx0 , dy0 , dx1 , dy1 ;
  double scn , scd , sc ;
  double len0 , len1 , dmaxlen ;

  HQASSERT( v1 != NULL , "v1 NULL in rcbt_comparevecs" ) ;
  HQASSERT( v2 != NULL , "v2 NULL in rcbt_comparevecs" ) ;

  dx0 = v1->dx0 ;
  dy0 = v1->dy0 ;
  dx1 = v2->dx0 ;
  dy1 = v2->dy0 ;

  /* Comparing lengths so need twice trap width; include a small epsilon */
  dmaxlen = ( 2.0 * (( double ) UserParams.RecombineTrapWidth )) + PA_EPSILON ;

  /* Check difference between vector lengths is within trap width constraint */
  len0 = sqrt(( dx0 * dx0 ) + ( dy0 * dy0 )) ;
  len1 = sqrt(( dx1 * dx1 ) + ( dy1 * dy1 )) ;
  if ( fabs( len0 - len1 ) > dmaxlen ) {
    HQTRACE( debug_rcbtrap , ( "Trap match failed (1): %f exceeded allowed Trap Width" ,
                               fabs( len0 - len1 ))) ;
    return FALSE ;
  }

  scn = fabs( dx0 ) + fabs( dy0 ) ;
  scd = fabs( dx1 ) + fabs( dy1 ) ;
  if ( scd > PA_EPSILON ) {
    sc = scn / scd ;
    dx1 *= sc ;
    dy1 *= sc ;
    if (( fabs( dx1 - dx0 ) < PA_EPSILON_LARGE &&
          fabs( dy1 - dy0 ) < PA_EPSILON_LARGE ) ||
        ( fabs( dx1 + dx0 ) < PA_EPSILON_LARGE &&
          fabs( dy1 + dy0 ) < PA_EPSILON_LARGE )) {
      dx0 = v1->dx1 ;
      dy0 = v1->dy1 ;
      dx1 = v2->dx1 ;
      dy1 = v2->dy1 ;

      /* Check difference between vector lengths is within trap width constraint */
      len0 = sqrt(( dx0 * dx0 ) + ( dy0 * dy0 )) ;
      len1 = sqrt(( dx1 * dx1 ) + ( dy1 * dy1 )) ;
      if ( fabs( len0 - len1 ) > dmaxlen ) {
        HQTRACE( debug_rcbtrap , ( "Trap match failed (2): %f exceeded allowed Trap Width" ,
                                   fabs( len0 - len1 ))) ;
        return FALSE ;
      }

      scn = fabs( dx0 ) + fabs( dy0 ) ;
      scd = fabs( dx1 ) + fabs( dy1 ) ;
      if ( scd > PA_EPSILON ) {
        sc = scn / scd ;
        dx1 *= sc ;
        dy1 *= sc ;
        if (( fabs( dx1 - dx0 ) < PA_EPSILON_LARGE &&
              fabs( dy1 - dy0 ) < PA_EPSILON_LARGE ) ||
            ( fabs( dx1 + dx0 ) < PA_EPSILON_LARGE &&
              fabs( dy1 + dy0 ) < PA_EPSILON_LARGE )) {
          return TRUE ;
        }
      }
    }
  }
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcbt_compareexactvecs( RCB2DVEC *v1 , RCB2DVEC *v2 )
{
  double dx0 , dy0 , dx1 , dy1 ;

  HQASSERT( v1 != NULL , "v1 NULL in rcbt_compareexactvecs" ) ;
  HQASSERT( v2 != NULL , "v2 NULL in rcbt_compareexactvecs" ) ;

  dx0 = v1->dx0 ;
  dy0 = v1->dy0 ;
  dx1 = v2->dx0 ;
  dy1 = v2->dy0 ;
  if (( fabs( dx0 - dx1 ) < 1.0 && fabs( dy0 - dy1 ) < 1.0 ) ||
      ( fabs( dx0 + dx1 ) < 1.0 && fabs( dy0 + dy1 ) < 1.0 )) {
    dx0 = v1->dx1 ;
    dy0 = v1->dy1 ;
    dx1 = v2->dx1 ;
    dy1 = v2->dy1 ;
    if (( fabs( dx0 - dx1 ) < 1.0 && fabs( dy0 - dy1 ) < 1.0 ) ||
        ( fabs( dx0 + dx1 ) < 1.0 && fabs( dy0 + dy1 ) < 1.0 ))
      return TRUE ;
  }

  return FALSE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcbt_comparerotatedvecs( RCB2DVEC *v1 , RCB2DVEC *v2 )
{
  double dx0 , dy0 , dx1 , dy1 ;
  double scn , scd , sc ;
  double len0 , len1 , dmaxlen ;

  HQASSERT( v1 != NULL , "v1 NULL in rcbt_comparerotatedvecs" ) ;
  HQASSERT( v2 != NULL , "v2 NULL in rcbt_comparerotatedvecs" ) ;

  dx0 = v1->dx0 ;
  dy0 = v1->dy0 ;
  dx1 = v2->dx1 ;
  dy1 = v2->dy1 ;

  /* Comparing lengths so need twice trap width; include a small epsilon */
  dmaxlen = ( 2.0 * (( double ) UserParams.RecombineTrapWidth )) + PA_EPSILON ;

  /* Check difference between vector lengths is within trap width constraint */
  len0 = sqrt(( dx0 * dx0 ) + ( dy0 * dy0 )) ;
  len1 = sqrt(( dx1 * dx1 ) + ( dy1 * dy1 )) ;
  if ( fabs( len0 - len1 ) > dmaxlen ) {
    HQTRACE( debug_rcbtrap , ( "Trap match failed (R1): %f exceeded allowed Trap Width" ,
                               fabs( len0 - len1 ))) ;
    return FALSE ;
  }

  scn = fabs( dx0 ) + fabs( dy0 ) ;
  scd = fabs( dx1 ) + fabs( dy1 ) ;
  if ( scd > PA_EPSILON ) {
    sc = scn / scd ;
    dx1 *= sc ;
    dy1 *= sc ;
    if (( fabs( dx1 - dx0 ) < PA_EPSILON_LARGE &&
          fabs( dy1 - dy0 ) < PA_EPSILON_LARGE ) ||
        ( fabs( dx1 + dx0 ) < PA_EPSILON_LARGE &&
          fabs( dy1 + dy0 ) < PA_EPSILON_LARGE )) {
      dx0 = v1->dx1 ;
      dy0 = v1->dy1 ;
      dx1 = v2->dx0 ;
      dy1 = v2->dy0 ;

      /* Check difference between vector lengths is within trap width constraint */
      len0 = sqrt(( dx0 * dx0 ) + ( dy0 * dy0 )) ;
      len1 = sqrt(( dx1 * dx1 ) + ( dy1 * dy1 )) ;
      if ( fabs( len0 - len1 ) > dmaxlen ) {
        HQTRACE( debug_rcbtrap , ( "Trap match failed (R2): %f exceeded allowed Trap Width" ,
                                   fabs( len0 - len1 ))) ;
        return FALSE ;
      }

      scn = fabs( dx0 ) + fabs( dy0 ) ;
      scd = fabs( dx1 ) + fabs( dy1 ) ;
      if ( scd > PA_EPSILON ) {
        sc = scn / scd ;
        dx1 *= sc ;
        dy1 *= sc ;
        if (( fabs( dx1 - dx0 ) < PA_EPSILON_LARGE &&
              fabs( dy1 - dy0 ) < PA_EPSILON_LARGE ) ||
            ( fabs( dx1 + dx0 ) < PA_EPSILON_LARGE &&
              fabs( dy1 + dy0 ) < PA_EPSILON_LARGE )) {
          return TRUE ;
        }
      }
    }
  }
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcbt_containvecs1( RCB2DVEC *v1 , RCB2DVEC *v2 )
{
  Bool result ;

  HQASSERT( v1 != NULL , "v1 NULL in rcbt_containvecs1" ) ;
  HQASSERT( v2 != NULL , "v2 NULL in rcbt_containvecs1" ) ;

  result = ( fabs( v1->dx0 ) + PA_EPSILON_LARGE > fabs( v2->dx0 ) &&
             fabs( v1->dy0 ) + PA_EPSILON_LARGE > fabs( v2->dy0 ) &&
             fabs( v1->dx1 ) + PA_EPSILON_LARGE > fabs( v2->dx1 ) &&
             fabs( v1->dy1 ) + PA_EPSILON_LARGE > fabs( v2->dy1 )) ||
           ( fabs( v2->dx0 ) + PA_EPSILON_LARGE > fabs( v1->dx0 ) &&
             fabs( v2->dy0 ) + PA_EPSILON_LARGE > fabs( v1->dy0 ) &&
             fabs( v2->dx1 ) + PA_EPSILON_LARGE > fabs( v1->dx1 ) &&
             fabs( v2->dy1 ) + PA_EPSILON_LARGE > fabs( v1->dy1 )) ;

  HQTRACE( debug_rcbtrap && ! result ,
           ( "rcbt_comparevecs(1) matched but rcbt_containvecs1 didn't" )) ;

  return result ;
}

/* -------------------------------------------------------------------------- */
static Bool rcbt_containvecs2( RCB2DVEC *v1 , RCB2DVEC *v2 ,
                               RCB2DVEC *v3 , RCB2DVEC *v4 )
{
  Bool result ;

  HQASSERT( v1 != NULL , "v1 NULL in rcbt_containvecs2" ) ;
  HQASSERT( v2 != NULL , "v2 NULL in rcbt_containvecs2" ) ;
  HQASSERT( v3 != NULL , "v3 NULL in rcbt_containvecs2" ) ;
  HQASSERT( v4 != NULL , "v4 NULL in rcbt_containvecs2" ) ;

  result = ( fabs( v1->dx0 ) + PA_EPSILON_LARGE > fabs( v2->dx0 ) &&
             fabs( v1->dy0 ) + PA_EPSILON_LARGE > fabs( v2->dy0 ) &&
             fabs( v1->dx1 ) + PA_EPSILON_LARGE > fabs( v2->dx1 ) &&
             fabs( v1->dy1 ) + PA_EPSILON_LARGE > fabs( v2->dy1 ) &&
             fabs( v3->dx0 ) + PA_EPSILON_LARGE > fabs( v4->dx0 ) &&
             fabs( v3->dy0 ) + PA_EPSILON_LARGE > fabs( v4->dy0 ) &&
             fabs( v3->dx1 ) + PA_EPSILON_LARGE > fabs( v4->dx1 ) &&
             fabs( v3->dy1 ) + PA_EPSILON_LARGE > fabs( v4->dy1 )) ||
           ( fabs( v2->dx0 ) + PA_EPSILON_LARGE > fabs( v1->dx0 ) &&
             fabs( v2->dy0 ) + PA_EPSILON_LARGE > fabs( v1->dy0 ) &&
             fabs( v2->dx1 ) + PA_EPSILON_LARGE > fabs( v1->dx1 ) &&
             fabs( v2->dy1 ) + PA_EPSILON_LARGE > fabs( v1->dy1 ) &&
             fabs( v4->dx0 ) + PA_EPSILON_LARGE > fabs( v3->dx0 ) &&
             fabs( v4->dy0 ) + PA_EPSILON_LARGE > fabs( v3->dy0 ) &&
             fabs( v4->dx1 ) + PA_EPSILON_LARGE > fabs( v3->dx1 ) &&
             fabs( v4->dy1 ) + PA_EPSILON_LARGE > fabs( v3->dy1 )) ;

  HQTRACE( debug_rcbtrap && ! result ,
           ( "rcbt_comparevecs(2) matched but rcbt_containvecs2 didn't" )) ;

  return result ;
}

/* -------------------------------------------------------------------------- */
static Bool rcbt_containrotatedvecs1( RCB2DVEC *v1 , RCB2DVEC *v2 )
{
  Bool result ;

  HQASSERT( v1 != NULL , "v1 NULL in rcbt_containrotatedvecs1" ) ;
  HQASSERT( v2 != NULL , "v2 NULL in rcbt_containrotatedvecs1" ) ;

  result = ( fabs( v1->dx0 ) + PA_EPSILON_LARGE > fabs( v2->dx1 ) &&
             fabs( v1->dy0 ) + PA_EPSILON_LARGE > fabs( v2->dy1 ) &&
             fabs( v1->dx1 ) + PA_EPSILON_LARGE > fabs( v2->dx0 ) &&
             fabs( v1->dy1 ) + PA_EPSILON_LARGE > fabs( v2->dy0 )) ||
           ( fabs( v2->dx0 ) + PA_EPSILON_LARGE > fabs( v1->dx1 ) &&
             fabs( v2->dy0 ) + PA_EPSILON_LARGE > fabs( v1->dy1 ) &&
             fabs( v2->dx1 ) + PA_EPSILON_LARGE > fabs( v1->dx0 ) &&
             fabs( v2->dy1 ) + PA_EPSILON_LARGE > fabs( v1->dy0 )) ;

  HQTRACE( debug_rcbtrap &&! result ,
           ( "rcbt_comparerotatedvecs(1) matched but "
             "rcbt_containrotatedvecs1 didn't" )) ;

  return result ;
}

/* -------------------------------------------------------------------------- */
static Bool rcbt_containrotatedvecs2( RCB2DVEC *v1 , RCB2DVEC *v2 ,
                                      RCB2DVEC *v3 , RCB2DVEC *v4 )
{
  Bool result ;

  HQASSERT( v1 != NULL , "v1 NULL in rcbt_containrotatedvecs2" ) ;
  HQASSERT( v2 != NULL , "v2 NULL in rcbt_containrotatedvecs2" ) ;
  HQASSERT( v3 != NULL , "v3 NULL in rcbt_containrotatedvecs2" ) ;
  HQASSERT( v4 != NULL , "v4 NULL in rcbt_containrotatedvecs2" ) ;

  result = ( fabs( v1->dx0 ) + PA_EPSILON_LARGE > fabs( v2->dx1 ) &&
             fabs( v1->dy0 ) + PA_EPSILON_LARGE > fabs( v2->dy1 ) &&
             fabs( v1->dx1 ) + PA_EPSILON_LARGE > fabs( v2->dx0 ) &&
             fabs( v1->dy1 ) + PA_EPSILON_LARGE > fabs( v2->dy0 ) &&
             fabs( v3->dx0 ) + PA_EPSILON_LARGE > fabs( v4->dx1 ) &&
             fabs( v3->dy0 ) + PA_EPSILON_LARGE > fabs( v4->dy1 ) &&
             fabs( v3->dx1 ) + PA_EPSILON_LARGE > fabs( v4->dx0 ) &&
             fabs( v3->dy1 ) + PA_EPSILON_LARGE > fabs( v4->dy0 )) ||
           ( fabs( v2->dx0 ) + PA_EPSILON_LARGE > fabs( v1->dx1 ) &&
             fabs( v2->dy0 ) + PA_EPSILON_LARGE > fabs( v1->dy1 ) &&
             fabs( v2->dx1 ) + PA_EPSILON_LARGE > fabs( v1->dx0 ) &&
             fabs( v2->dy1 ) + PA_EPSILON_LARGE > fabs( v1->dy0 ) &&
             fabs( v4->dx0 ) + PA_EPSILON_LARGE > fabs( v3->dx1 ) &&
             fabs( v4->dy0 ) + PA_EPSILON_LARGE > fabs( v3->dy1 ) &&
             fabs( v4->dx1 ) + PA_EPSILON_LARGE > fabs( v3->dx0 ) &&
             fabs( v4->dy1 ) + PA_EPSILON_LARGE > fabs( v3->dy0 )) ;

  HQTRACE( debug_rcbtrap && ! result ,
           ( "rcbt_comparerotatedvecs(2) matched but "
             "rcbt_containrotatedvecs2 didn't" )) ;

  return result ;
}

/* -------------------------------------------------------------------------- */
Bool rcbt_comparetrap( RCBTRAP *rcbtrap1 , RCBTRAP *rcbtrap2 ,
                       Bool fAllowDonut, Bool fCheckCenter )
{
  HQASSERT( rcbt_asserttrap( rcbtrap1 ) , "" ) ;
  HQASSERT( rcbt_asserttrap( rcbtrap2 ) , "" ) ;

  /* For now we only match rectangles, ovals, octagons, rounded rects &
   * inverse rounded rects. To be a match the types must match, the centers
   * of the two objects must be the same, and the 'width' and 'height'
   * vectors must be parallel.
   */
  if (( rcbtrap1->type == rcbtrap2->type ) ||
      ( rcbtrap1->type == RCBTRAP_LINE && rcbtrap2->type == RCBTRAP_RECT ) ||
      ( rcbtrap1->type == RCBTRAP_RECT && rcbtrap2->type == RCBTRAP_LINE )) {
    double ex = ( gstateptr->pa_eps.edx < 1.0 ? 1.0 : gstateptr->pa_eps.edx ) ;
    double ey = ( gstateptr->pa_eps.edy < 1.0 ? 1.0 : gstateptr->pa_eps.edy ) ;
    if ( ! fCheckCenter ||
         ( fabs( rcbtrap1->x - rcbtrap2->x ) < ex &&
           fabs( rcbtrap1->y - rcbtrap2->y ) < ey ) ) {
      switch ( rcbtrap1->type ) {
      case RCBTRAP_DONUT:
        if ( ! fAllowDonut )
          return FALSE ;
        /* FALL THRU */
      case RCBTRAP_LINE:
      case RCBTRAP_RECT:
      case RCBTRAP_OVAL:
        return rcbt_comparevecs ( & rcbtrap1->u.s1.v1 , & rcbtrap2->u.s1.v1 ) &&
               rcbt_containvecs1( & rcbtrap1->u.s1.v1 , & rcbtrap2->u.s1.v1 ) ;
      case RCBTRAP_OCT:
      case RCBTRAP_RND:
      case RCBTRAP_REV:
        return rcbt_comparevecs ( & rcbtrap1->u.s2.v1 , & rcbtrap2->u.s2.v1 ) &&
               rcbt_comparevecs ( & rcbtrap1->u.s2.v2 , & rcbtrap2->u.s2.v2 ) &&
               rcbt_containvecs2( & rcbtrap1->u.s2.v1 , & rcbtrap2->u.s2.v1 ,
                                  & rcbtrap1->u.s2.v2 , & rcbtrap2->u.s2.v2 ) ;
      default:
        HQFAIL( "Unknown trap type to fill in" ) ;
        return FALSE ;
      }
    }
  }
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
Bool rcbt_compareexacttrap( RCBTRAP *rcbtrap1 , RCBTRAP *rcbtrap2 ,
                            Bool fAllowDonut )
{
  HQASSERT( rcbt_asserttrap( rcbtrap1 ) , "" ) ;
  HQASSERT( rcbt_asserttrap( rcbtrap2 ) , "" ) ;

  /* For now we only match rectangles, ovals, octagons, rounded rects &
   * inverse rounded rects. To be a match the types must match, the centers
   * of the two objects must be the same, and the 'width' and 'height'
   * vectors must be parallel.
   */
  if (( rcbtrap1->type == rcbtrap2->type ) ||
      ( rcbtrap1->type == RCBTRAP_LINE && rcbtrap2->type == RCBTRAP_RECT ) ||
      ( rcbtrap1->type == RCBTRAP_RECT && rcbtrap2->type == RCBTRAP_LINE )) {
    if ( fabs( rcbtrap1->x - rcbtrap2->x ) < 1.0 &&
         fabs( rcbtrap1->y - rcbtrap2->y ) < 1.0 ) {
      switch ( rcbtrap1->type ) {
      case RCBTRAP_DONUT:
        if ( ! fAllowDonut )
          return FALSE ;
        /* FALL THRU */
      case RCBTRAP_LINE:
      case RCBTRAP_RECT:
      case RCBTRAP_OVAL:
        return rcbt_compareexactvecs( & rcbtrap1->u.s1.v1 , & rcbtrap2->u.s1.v1 ) ;

      case RCBTRAP_OCT:
      case RCBTRAP_RND:
      case RCBTRAP_REV:
        return rcbt_compareexactvecs( & rcbtrap1->u.s2.v1 , & rcbtrap2->u.s2.v1 ) &&
               rcbt_compareexactvecs( & rcbtrap1->u.s2.v2 , & rcbtrap2->u.s2.v2 ) ;
      default:
        HQFAIL( "Unknown trap type to fill in" ) ;
        return FALSE ;
      }
    }
  }
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
Bool rcbt_comparerotatedtrap( RCBTRAP *rcbtrap1 , RCBTRAP *rcbtrap2 ,
                              Bool fAllowDonut )
{
  HQASSERT( rcbt_asserttrap( rcbtrap1 ) , "" ) ;
  HQASSERT( rcbt_asserttrap( rcbtrap2 ) , "" ) ;

  /* For now we only match rectangles, ovals, octagons, rounded rects &
   * inverse rounded rects. To be a match the types must match, the centers
   * of the two objects must be the same, and the 'width' and 'height'
   * vectors must be parallel.
   */
  if (( rcbtrap1->type == rcbtrap2->type ) ||
      ( rcbtrap1->type == RCBTRAP_LINE && rcbtrap2->type == RCBTRAP_RECT ) ||
      ( rcbtrap1->type == RCBTRAP_RECT && rcbtrap2->type == RCBTRAP_LINE )) {
    double ex = ( gstateptr->pa_eps.edx < 1.0 ? 1.0 : gstateptr->pa_eps.edx ) ;
    double ey = ( gstateptr->pa_eps.edy < 1.0 ? 1.0 : gstateptr->pa_eps.edy ) ;
    if ( fabs( rcbtrap1->x - rcbtrap2->x ) < ex &&
         fabs( rcbtrap1->y - rcbtrap2->y ) < ey ) {
      switch ( rcbtrap1->type ) {
      case RCBTRAP_DONUT:
      if ( ! fAllowDonut )
        return FALSE ;
      /* FALL THRU */
      case RCBTRAP_LINE:
      case RCBTRAP_RECT:
      case RCBTRAP_OVAL:
        return rcbt_comparerotatedvecs ( & rcbtrap1->u.s1.v1 , & rcbtrap2->u.s1.v1 ) &&
               rcbt_containrotatedvecs1( & rcbtrap1->u.s1.v1 , & rcbtrap2->u.s1.v1 ) ;
      case RCBTRAP_OCT:
      case RCBTRAP_RND:
      case RCBTRAP_REV:
        return rcbt_comparerotatedvecs ( & rcbtrap1->u.s2.v1 , & rcbtrap2->u.s2.v1 ) &&
               rcbt_comparerotatedvecs ( & rcbtrap1->u.s2.v2 , & rcbtrap2->u.s2.v2 ) &&
               rcbt_containrotatedvecs2( & rcbtrap1->u.s2.v1 , & rcbtrap2->u.s2.v1 ,
                                         & rcbtrap1->u.s2.v2 , & rcbtrap2->u.s2.v2 ) ;
      default:
        HQFAIL( "Unknown trap type to fill in" ) ;
        return FALSE ;
      }
    }
  }
  return FALSE ;
}

/* ----------------------------------------------------------------------- */
Bool rcbt_compareimagetrap( RCBTRAP *rcbtrap1 , RCBTRAP *rcbtrap2 ,
                            Bool *pfSameCenterPoint )
{
  double ex = ( gstateptr->pa_eps.edx < 1.0 ? 1.0 : gstateptr->pa_eps.edx ) ;
  double ey = ( gstateptr->pa_eps.edy < 1.0 ? 1.0 : gstateptr->pa_eps.edy ) ;

  HQASSERT( rcbt_asserttrap( rcbtrap1 ) , "rcbt: rcbtrap1 bad" ) ;
  HQASSERT( rcbt_asserttrap( rcbtrap2 ) , "rcbt: rcbtrap2 bad" ) ;

  HQASSERT( rcbtrap1->type == RCBTRAP_RECT , "rcbtrap1 not RCBTRAP_RECT" ) ;
  HQASSERT( rcbtrap2->type == RCBTRAP_RECT , "rcbtrap2 not RCBTRAP_RECT" ) ;

  /* An image and trap may have differing center points if certain apps have
     optimised away rows or columns of image data which lie outside the clip.
     Hence this routine does not enforce the center points condition.
  */

  * pfSameCenterPoint = ( fabs( rcbtrap1->x - rcbtrap2->x ) < ex &&
                          fabs( rcbtrap1->y - rcbtrap2->y ) < ey ) ;

  return
    rcbt_comparevecs ( & rcbtrap1->u.s1.v1 , & rcbtrap2->u.s1.v1 ) &&
    rcbt_containvecs1( & rcbtrap1->u.s1.v1 , & rcbtrap2->u.s1.v1 ) ;
}

void init_C_globals_rcbtrap(void)
{
#if defined( ASSERT_BUILD )
  debug_rcbtrap = FALSE ;
#endif
}


/* Log stripped */
