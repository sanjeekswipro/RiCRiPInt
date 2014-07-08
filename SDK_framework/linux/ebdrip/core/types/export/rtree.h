/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:rtree.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A set of generally useful binary tree implementations.
 * Assumes std.h has been included.
 */

#ifndef __RTREE_H__
#define __RTREE_H__

#include "coretypes.h"

#define RTREE_DIMENSIONS 3

/** Multi-dimensional "rectangle" which acts as the key for our R-Tree
    spatial indexes */

typedef struct rtree_rect {
  /** xmin,ymin,...,xmax,ymax,... */
  dcoord boundary[ 2 * RTREE_DIMENSIONS ] ;
}
RTREE_RECT ;

/* Opaque forward reference. */
typedef struct rtree_node RTREE_NODE ;
struct sw_memory_instance ;

/** If passed to a tree search, this callback function will be called
    with the data pointer of each rect that overlaps the search rect
    plus whatever user specific pointer was passed to the search.  It
    can terminate the search early by returning 0 in which case the
    search will return the number of hits found up to that point. */

typedef int ( *SearchHitCallback ) ( RTREE_RECT *r , void *data , void *arg ) ;

extern int rtree_search( RTREE_NODE * , RTREE_RECT * , SearchHitCallback ,
                         void * ) ;
extern int rtree_insert_rect( RTREE_RECT * , void *data ,
                              RTREE_NODE ** , uint32 ) ;
extern int rtree_delete_rect( RTREE_RECT * , void *data , RTREE_NODE ** ) ;
extern RTREE_NODE *rtree_new_index( struct sw_memory_instance * ) ;
extern RTREE_NODE *rtree_new_node( struct sw_memory_instance * ) ;
extern void rtree_print_node( RTREE_NODE * , int ) ;
extern void rtree_dispose( RTREE_NODE **) ;

#endif /* __RTREE_H__ */

/* Log stripped */
