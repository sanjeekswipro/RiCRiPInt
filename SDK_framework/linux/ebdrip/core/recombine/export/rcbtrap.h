/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!export:rcbtrap.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Structures for storing information about (Quark) trapped recombine objects.
 */

#ifndef __RCBTRAP_H__
#define __RCBTRAP_H__

#include "ndisplay.h" /* NFILLOBJECT */
#include "paths.h" /* PATHINFO */

enum rcbtraptype {
  RCBTRAP_NOTTESTED ,
  RCBTRAP_UNKNOWN ,
  RCBTRAP_LINE ,
  RCBTRAP_RECT ,
  RCBTRAP_OVAL ,
  RCBTRAP_OCT ,
  RCBTRAP_RND ,
  RCBTRAP_REV ,
  RCBTRAP_DONUT
} ;

typedef struct RCB2DVEC {
  float dx0 , dy0 ; /* 1st vector. */
  float dx1 , dy1 ; /* 2nd vector. */
} RCB2DVEC ;

typedef struct RCBSHAPE1 {
  RCB2DVEC v1 ;
} RCBSHAPE1 ;

typedef struct RCBSHAPE2 {
  RCB2DVEC v1 ;
  RCB2DVEC v2 ;
} RCBSHAPE2 ;

typedef union {
  RCBSHAPE1 s1 ;
  RCBSHAPE2 s2 ;
} RCBSHAPE ;

typedef struct RCBTRAP {
  int32 type ;
  float x , y ;
  /* union must be the last field in the struct as not
     all of the union may actually be allocated (yuck) */
  RCBSHAPE u;
} RCBTRAP ;

RCBTRAP *rcbt_alloctrap(mm_pool_t *pools, int32 traptype) ;

void rcbt_freetrap(mm_pool_t *pools, RCBTRAP *rcbtrap) ;

Bool rcbt_addtrap(mm_pool_t *pools, NFILLOBJECT *nfill, PATHINFO *path,
                  Bool fDonut, RCBTRAP *rcbtrap) ;
Bool rcbt_comparetrap(RCBTRAP *rcbtrap1, RCBTRAP *rcbtrap2,
                      Bool fAllowDonut, Bool fCheckCenter) ;
Bool rcbt_compareexacttrap(RCBTRAP *rcbtrap1, RCBTRAP *rcbtrap2,
                           Bool fAllowDonut) ;
Bool rcbt_comparerotatedtrap(RCBTRAP *rcbtrap1, RCBTRAP *rcbtrap2,
                             Bool fAllowDonut ) ;
Bool rcbt_compareimagetrap( RCBTRAP *rcbtrap1 , RCBTRAP *rcbtrap2 ,
                            Bool *pfSameCenterPoint ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
