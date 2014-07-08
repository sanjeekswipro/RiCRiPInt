/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!src:rtree.c(EBDSDK_P.1) $
 * $Id: src:rtree.c,v 1.3.1.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * General R Tree implementation. Provides an n-dimensional spatial
 * index for optimal queries with arbitrary search keys.
 */

#include "core.h"
#include "monitor.h"

#include "rtree.h"
#include "swmemapi.h"

#include <float.h>
#include <math.h>

#define RTREE_DEGENERATE(x) ((x)->boundary[0] > (x)->boundary[RTREE_DIMENSIONS])

typedef struct rtree_branch {
  /** Bounding box for this branch. */
  RTREE_RECT rect;

  /** Data associated with the rect. If the owning node is a leaf (\c
      level == 0), the this pointer is to the private data for the
      record associated with the bounding box rect. If the owning node
      is a non-leaf (\c level > 0), the pointer is to the child node
      of this branch. */
  union {
    /*@null@*/
    void *data ;

    /*@null@*/ /*@only@*/
    RTREE_NODE *child ;
  } u ;
}
RTREE_BRANCH ;

/** Max branching factor of a node. There's some suggestion we'll get
    the best performance if this is such that the size of an \c
    RTREE_NODE coincides with the natural VM page size. */
#define MAX_BRANCH_FACTOR ( int )(( 4096 - ( 2 * sizeof( uint32 ))) / \
                                  sizeof( RTREE_BRANCH ))

/** The minimum branching factor for a node. This can be tweaked,
    although note that it must be at most MAX_BRANCH_FACTOR / 2 */
#define MIN_BRANCH_FACTOR ( MAX_BRANCH_FACTOR / 2 )

/** The most fundamental structure of the R tree: the node. */

typedef struct rtree_node {
  /** Count of sub-branches. */
  uint32 count ;

  /** The level of this node in the tree: 0 is leaf, others have
      positive values. */
  uint32 level ;

  /** Sub-branch array. */
  RTREE_BRANCH branch[ MAX_BRANCH_FACTOR ] ;

  /** instance of memory management API. */
  sw_memory_instance *mem ;
} rtree_node ;

/** definitions and global variables. */

/* &&&& ACK! */

static RTREE_BRANCH BranchBuf[MAX_BRANCH_FACTOR + 1];
static int BranchCount;
static RTREE_RECT CoverSplit;
static double CoverSplitArea;

/* Variables for finding a partition. */

static struct PartitionVars {
  uint32 partition[ MAX_BRANCH_FACTOR + 1 ] ;
  uint32 total ;
  uint32 minfill ;
  uint32 taken[ MAX_BRANCH_FACTOR + 1 ] ;
  uint32 count[ 2 ] ;
  RTREE_RECT cover[ 2 ] ;
  double area[ 2 ] ;
}
Partition ;

/** Structure for temporarily holding under-populated nodes. */

typedef struct rtree_listnode {
  struct rtree_listnode *next;
  RTREE_NODE *node;
  sw_memory_instance *mem;
}
RTREE_LISTNODE ;

static void rtree_split_node( RTREE_NODE *n, RTREE_BRANCH *b,
                              /*@out@*/ RTREE_NODE **nn ) ;

/** Initialize a rectangle to have all 0 coordinates. */
static void rtree_init_rect( /*@out@*/ RTREE_RECT *R )
{
  RTREE_RECT *r = R;
  int i;

  for ( i = 0; i < 2 * RTREE_DIMENSIONS ; i++ )
    r->boundary[i] = 0;
}

/** Return a rect whose first low side is higher than its opposite
    side - interpreted as an undefined rect. */
static RTREE_RECT rtree_undefined_rect( void )
{
  RTREE_RECT r ;
  int i ;

  r.boundary[ 0 ] = 1 ;
  r.boundary[ RTREE_DIMENSIONS ] = -1 ;
  for ( i = 1; i < RTREE_DIMENSIONS; i++ ) {
    r.boundary[ i ] = 0 ;
    r.boundary[ i + RTREE_DIMENSIONS ] = 0 ;
  }

  return r ;
}

static void rtree_indent( int depth )
{
  int i;

  for ( i = 0; i < depth; i++ ) {
    monitorf((uint8 *)"  " );
  }
}

/** Print out the data for a rectangle. */
static void rtree_print_rect( RTREE_RECT *R, int depth )
{
  RTREE_RECT *r = R;
  int i;

  HQASSERT( r != NULL , "Unexpected NULL pointer" ) ;

  rtree_indent( depth );
  monitorf(( uint8 * )"rect:\n" );
  for ( i = 0; i < RTREE_DIMENSIONS; i++ ) {
    rtree_indent( depth + 1 );
    monitorf(( uint8 * )"%f\t%f\n",
             r->boundary[i], r->boundary[i + RTREE_DIMENSIONS] );
  }
}

/** Define the RTREE_DIMENSIONS-dimensional volume the unit sphere in
    that dimension into the symbol "UNIT_SPHERE_VOLUME". Note that if
    the gamma function is available in the math library and if the
    compiler supports static initialization using functions, this is
    easily computed for any dimension. If not, the value can be
    precomputed and taken from a table. The following code can do it
    either way. */

#ifdef gamma

/* Computes the volume of an N-dimensional sphere. Derived from
   formula in "Regular Polytopes" by H.S.M Coxeter. */
static double sphere_volume( double dimension )
{
  static const double log_pi = log( 3.1415926535 );
  double log_gamma, log_volume;

  log_gamma = gamma( dimension / 2.0 + 1 );
  log_volume = dimension / 2.0 * log_pi - log_gamma;
  return exp( log_volume );
}

static const double UNIT_SPHERE_VOLUME = sphere_volume( RTREE_DIMENSIONS );

#else

/* Precomputed volumes of the unit spheres for the first few dimensions */
static const double UNIT_SPHERE_VOLUMES[] = {
  0.000000,                     /* dimension   0 */
  2.000000,                     /* dimension   1 */
  3.141593,                     /* dimension   2 */
  4.188790,                     /* dimension   3 */
  4.934802,                     /* dimension   4 */
  5.263789,                     /* dimension   5 */
  5.167713,                     /* dimension   6 */
  4.724766,                     /* dimension   7 */
  4.058712,                     /* dimension   8 */
  3.298509,                     /* dimension   9 */
  2.550164,                     /* dimension  10 */
  1.884104,                     /* dimension  11 */
  1.335263,                     /* dimension  12 */
  0.910629,                     /* dimension  13 */
  0.599265,                     /* dimension  14 */
  0.381443,                     /* dimension  15 */
  0.235331,                     /* dimension  16 */
  0.140981,                     /* dimension  17 */
  0.082146,                     /* dimension  18 */
  0.046622,                     /* dimension  19 */
  0.025807,                     /* dimension  20 */
};

#if RTREE_DIMENSIONS > 20
#error "Not enough precomputed sphere volumes"
#else
#define UNIT_SPHERE_VOLUME UNIT_SPHERE_VOLUMES[RTREE_DIMENSIONS]
#endif /* RTREE_DIMENSIONS > 20 */

#endif /* gamma */

/** The exact volume of the bounding sphere for the given Rect. */
static double rtree_rect_spherical_volume( RTREE_RECT *r )
{
  int i;
  double sum_of_squares = 0, radius;

  HQASSERT( r != NULL , "Unexpected NULL pointer" ) ;

  if ( RTREE_DEGENERATE( r )) {
    return 0.0 ;
  }

  for ( i = 0; i < RTREE_DIMENSIONS; i++ ) {
    double half_extent = ( r->boundary[i + RTREE_DIMENSIONS] -
                           r->boundary[i] ) / 2;

    sum_of_squares += half_extent * half_extent;
  }

  radius = sqrt( sum_of_squares );

  return ( pow( radius, RTREE_DIMENSIONS ) * UNIT_SPHERE_VOLUME );
}

/** Find the union of two rectangles. */

static void rtree_union_rect( RTREE_RECT *r1 , RTREE_RECT *r2 ,
                              /*@out@*/ RTREE_RECT *dest )
{
  HQASSERT( r1 != NULL && r2 != NULL && dest != NULL ,
            "Unexpected NULL pointer" ) ;

  if ( RTREE_DEGENERATE( r1 )) {
    *dest = *r2 ;
  }
  else if ( RTREE_DEGENERATE( r2 )) {
    *dest = *r1 ;
  }
  else {
    uint32 i ;

    for ( i = 0 ; i < RTREE_DIMENSIONS ; i++ ) {
      uint32 j = i + RTREE_DIMENSIONS ;

      dest->boundary[ i ] = min( r1->boundary[ i ] , r2->boundary[ i ]) ;
      dest->boundary[ j ] = max( r1->boundary[ j ] , r2->boundary[ j ]) ;
    }
  }
}

/** Decide whether two rectangles overlap. */

static int rtree_overlap( RTREE_RECT *R, RTREE_RECT *S )
{
  RTREE_RECT *r = R, *s = S;
  int i, j;

  HQASSERT( r != NULL && s != NULL , "Unexpected NULL pointer" ) ;

  for ( i = 0; i < RTREE_DIMENSIONS; i++ ) {
    j = i + RTREE_DIMENSIONS;            /* index for high sides */
    if ( r->boundary[i] > s->boundary[j] || s->boundary[i] > r->boundary[j] ) {
      return FALSE;
    }
  }
  return TRUE;
}

#if CONTAIN_TEST_REQUIRED
/** Decide whether rectangle R is contained in rectangle S. Compiled
    out for now, but kept in the sources because it might be useful to
    be able to search a spatial index for containment rather than just
    overlap at some point in the future. When that's true, we'll
    either pass in the test to use as a function pointer (like the
    comparison function in Red-Black binary trees in \c rbtree.c) or
    just have a flag. */

static int rtree_contain( RTREE_RECT *r, RTREE_RECT *s )
{
  int i, j, result;

  HQASSERT( r != NULL && s != NULL , "Unexpected NULL pointer" ) ;

  /* RTREE_DEGENERATE rect is contained in any other */
  if ( RTREE_DEGENERATE( r ))
    return TRUE;

  /* No rect (except an undefined one) is contained in an undef
     rect */
  if ( RTREE_DEGENERATE( s ))
    return FALSE;

  result = TRUE;
  for ( i = 0; i < RTREE_DIMENSIONS; i++ ) {
    j = i + RTREE_DIMENSIONS;            /* index for high sides */
    result = result && r->boundary[i] >= s->boundary[i]
      && r->boundary[j] <= s->boundary[j];
  }
  return result;
}
#endif

/** Initialize one branch cell in a node. */

static void rtree_init_branch( /*@out@*/ RTREE_BRANCH *b )
{
  rtree_init_rect( &( b->rect ));
  b->u.data = NULL;
}

/** Initialize a Node structure. */

static void rtree_init_node( /*@out@*/ RTREE_NODE *n,
                             /*@in@*/ sw_memory_instance *mem )
{
  int i;

  n->count = 0 ;
  n->level = 0 ;
  n->mem = mem ;
  for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
    rtree_init_branch( &( n->branch[ i ])) ;
  }
}

/** Make a new node and initialize to have all branch cells empty. */

RTREE_NODE *rtree_new_node( sw_memory_instance *mem )
{
  RTREE_NODE *n;

  n = ( RTREE_NODE * ) mem->implementation->alloc(
                        mem, sizeof ( RTREE_NODE ));
  HQASSERT( n != NULL , "Unexpected NULL pointer" ) ;
  if ( n != NULL )
    rtree_init_node( n, mem );

  return n;
}

static void rtree_free_node( /*@only@*/ RTREE_NODE *p )
{
  HQASSERT( p != NULL , "Unexpected NULL pointer" ) ;
  p->mem->implementation->free( p->mem, p );
}

static void rtree_print_branch( RTREE_BRANCH *b , int depth )
{
  rtree_print_rect( &( b->rect ) , depth ) ;
  if ( b->u.child != NULL ) {
    rtree_print_node( b->u.child , depth ) ;
  }
}

/** Print out the data in a node. */

void rtree_print_node( RTREE_NODE *n, int depth )
{
  uint32 i;

  HQASSERT( n != NULL , "Unexpected NULL pointer" ) ;

  rtree_indent( depth );
  monitorf(( uint8 * )"node" );

  if ( n->level == 0 )
    monitorf(( uint8 * )" LEAF" );
  else if ( n->level > 0 )
    monitorf(( uint8 * )" NONLEAF" );

  monitorf(( uint8 * )"  level=%d  count=%d  address=%p\n",
           n->level, n->count, n );

  for ( i = 0; i < n->count; i++ ) {
    if ( n->level == 0 ) {
      rtree_indent(depth);
      monitorf(( uint8 * )"\t%d: data = %d\n", i, n->branch[i].u.child);
    }
    else {
      rtree_indent( depth );
      monitorf(( uint8 * )"branch %d\n", i );
      rtree_print_branch( &n->branch[i], depth + 1 );
    }
  }
}

/** Find the smallest rectangle that includes all rectangles in
    branches of a node. */

static RTREE_RECT rtree_node_cover( RTREE_NODE *n )
{
  uint32 i;
  uint32 first_time = 1;
  RTREE_RECT r;

  HQASSERT( n != NULL , "Unexpected NULL pointer" ) ;

  rtree_init_rect( &r );
  for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
    if ( n->branch[i].u.child != NULL ) {
      if ( first_time ) {
        r = n->branch[i].rect;
        first_time = 0;
      }
      else {
        rtree_union_rect( &r , &( n->branch[ i ].rect ) , &r ) ;
      }
    }
  }

  return r ;
}

/** Pick a branch. Pick the one that will need the smallest increase
    in area to accomodate the new rectangle.  This will result in the
    least total area for the covering rectangles in the current node.
    In case of a tie, pick the one which was smaller before, to get
    the best resolution when searching. */

static int rtree_pick_branch( RTREE_RECT *r, RTREE_NODE *n )
{
  RTREE_RECT *rr;
  uint32 i ;
  Bool first_time = TRUE ;
  double increase ;
  double bestIncr = -1.0 ;
  double area ;
  double bestArea = -1.0 ;
  int best = -1 ;
  RTREE_RECT tmp_rect;

  HQASSERT( r != NULL && n != NULL , "Unexpected NULL pointer" ) ;

  for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
    if ( n->branch[i].u.child != NULL ) {
      rr = &n->branch[i].rect;
      area = rtree_rect_spherical_volume( rr );
      rtree_union_rect( r , rr , &tmp_rect ) ;
      increase = rtree_rect_spherical_volume( &tmp_rect ) - area;
      if ( increase - bestIncr < DBL_EPSILON || first_time ) {
        best = i;
        bestArea = area;
        bestIncr = increase;
        first_time = FALSE ;
      }
      else if ( fabs( increase - bestIncr ) <= DBL_EPSILON &&
                area - bestArea < DBL_EPSILON ) {
        best = i;
        bestArea = area;
        bestIncr = increase;
      }
    }
  }

  return best ;
}

/** Load branch buffer with branches from full node plus the extra
    branch. */

static void rtree_get_branches( RTREE_NODE *n, RTREE_BRANCH *b )
{
  uint32 i;

  HQASSERT( n != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( b != NULL , "Unexpected NULL pointer" ) ;

  /* Load the branch buffer */
  for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
    HQASSERT( n->branch[i].u.child != NULL ,
              "n should have every entry full" ) ;
    BranchBuf[i] = n->branch[i];
  }
  BranchBuf[MAX_BRANCH_FACTOR] = *b;
  BranchCount = MAX_BRANCH_FACTOR + 1;

  /* Calculate rect containing all in the set */
  CoverSplit = BranchBuf[0].rect;
  for ( i = 1; i < MAX_BRANCH_FACTOR + 1; i++ ) {
    rtree_union_rect( &CoverSplit, &BranchBuf[i].rect , &CoverSplit );
  }
  CoverSplitArea = rtree_rect_spherical_volume( &CoverSplit );

  rtree_init_node( n, n->mem );
}

/** Initialize a PartitionVars structure. */

static void rtree_init_pvars( struct PartitionVars *p, int maxrects,
                              int minfill )
{
  int i;

  HQASSERT( p != NULL , "Unexpected NULL pointer" ) ;

  p->count[0] = p->count[1] = 0;
  p->cover[0] = p->cover[1] = rtree_undefined_rect();
  p->area[0] = p->area[1] = 0.0 ;
  p->total = maxrects;
  p->minfill = minfill;
  for ( i = 0; i < maxrects; i++ ) {
    p->taken[i] = FALSE;
    p->partition[i] = 0;
  }
}

/** Put a branch in one of the groups. */

static void rtree_classify( int i, int group, struct PartitionVars *p )
/*@modifies p->partition[i],p->taken[i],p->cover[group],
            p->area[group],p->count[group]@*/
{
  HQASSERT( p != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( !p->taken[i] , "Clash in rtree_classify" ) ;

  p->partition[i] = group;
  p->taken[i] = TRUE;

  if ( p->count[group] == 0 ) {
    p->cover[group] = BranchBuf[i].rect;
  }
  else {
    rtree_union_rect( &BranchBuf[ i ].rect , &p->cover[ group ] ,
                      &p->cover[ group ]) ;
  }
  p->area[group] = rtree_rect_spherical_volume( &p->cover[group] );
  p->count[group]++;
}

/** Pick two rects from set to be the first elements of the two groups.
    Pick the two that waste the most area if covered by a single rectangle. */

static void rtree_pick_seeds( struct PartitionVars *p )
{
  uint32 i ;
  uint32 j ;
  uint32 seed0 = 0 ;
  uint32 seed1 = 0 ;
  double worst ;
  double waste ;
  double area[ MAX_BRANCH_FACTOR + 1 ] = { 0.0 } ;

  for ( i = 0; i < p->total; i++ ) {
    area[i] = rtree_rect_spherical_volume( &BranchBuf[i].rect );
  }

  worst = -CoverSplitArea - 1;
  for ( i = 0; i < p->total - 1; i++ ) {
    for ( j = i + 1; j < p->total; j++ ) {
      RTREE_RECT one_rect ;

      rtree_union_rect( &BranchBuf[i].rect, &BranchBuf[j].rect , &one_rect ) ;

      waste = rtree_rect_spherical_volume( &one_rect ) - area[i] - area[j];
      if ( waste - worst >= DBL_EPSILON ) {
        worst = waste;
        seed0 = i;
        seed1 = j;
      }
    }
  }

  rtree_classify( seed0 , 0 , p ) ;
  rtree_classify( seed1 , 1 , p ) ;
}

/** Choose a partition: As the seeds for the two groups, pick the two
    rects that would waste the most area if covered by a single
    rectangle, i.e. evidently the worst pair to have in the same
    group.  Of the remaining, one at a time is chosen to be put in one
    of the two groups.  The one chosen is the one with the greatest
    difference in area expansion depending on which group - the rect
    most strongly attracted to one group and repelled from the other.
    If one group gets too full (more would force other group to
    violate min fill requirement) then other group gets the rest.
    These last are the ones that can go in either group most
    easily. */

static void rtree_find_split( struct PartitionVars *p, int minfill )
{
  uint32 i ;
  double biggestDiff ;
  int group, chosen = -1, betterGroup = -1;

  HQASSERT( p != NULL , "Unexpected NULL pointer" ) ;

  rtree_init_pvars( p, BranchCount, minfill );
  rtree_pick_seeds( p );

  while ( p->count[0] + p->count[1] < p->total &&
          p->count[0] < p->total - p->minfill &&
          p->count[1] < p->total - p->minfill ) {
    biggestDiff = -1.0 ;
    for ( i = 0; i < p->total; i++ ) {
      if ( !p->taken[i] ) {
        RTREE_RECT *r, rect_0, rect_1;
        double growth0, growth1, diff;

        r = &BranchBuf[i].rect;
        rtree_union_rect( r, &p->cover[0] , &rect_0 ) ;
        rtree_union_rect( r, &p->cover[1] , &rect_1 ) ;
        growth0 = rtree_rect_spherical_volume( &rect_0 ) - p->area[0];
        growth1 = rtree_rect_spherical_volume( &rect_1 ) - p->area[1];
        diff = growth1 - growth0;
        if ( diff >= 0 )
          group = 0;
        else {
          group = 1;
          diff = -diff;
        }

        if ( diff - biggestDiff >= DBL_EPSILON ) {
          biggestDiff = diff;
          chosen = i;
          betterGroup = group;
        }
        else if ( fabs( diff - biggestDiff ) <= DBL_EPSILON &&
                  p->count[group] < p->count[betterGroup] ) {
          chosen = i;
          betterGroup = group;
        }
      }
    }
    rtree_classify( chosen, betterGroup, p );
  }

  /* If one group is too full, put remaining rects in the other */
  if ( p->count[0] + p->count[1] < p->total ) {
    if ( p->count[0] >= p->total - p->minfill )
      group = 1;
    else
      group = 0;
    for ( i = 0; i < p->total; i++ ) {
      if ( !p->taken[i] )
        rtree_classify( i, group, p );
    }
  }

  HQASSERT( p->count[0] + p->count[1] == p->total ,
            "Unexpected NULL pointer" ) ;
  HQASSERT( p->count[0] >= p->minfill && p->count[1] >= p->minfill ,
            "Unexpected NULL pointer" ) ;
}

/* Add a branch to a node.  Split the node if necessary.  Returns
   FALSE if node not split.  Old node updated.  Returns TRUE if node
   split, sets *new_node to address of new node.  Old node updated,
   becomes one of two. */

static Bool rtree_add_branch( RTREE_BRANCH *b, RTREE_NODE *n,
                              /*@out@*/ RTREE_NODE **new_node )
{
  uint32 i;

  HQASSERT( b != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( n != NULL , "Unexpected NULL pointer" ) ;

  if ( n->count < MAX_BRANCH_FACTOR) {      /* split won't be necessary */
    for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {      /* find empty branch */
      if ( n->branch[i].u.child == NULL ) {
        n->branch[i] = *b;
        n->count++;
        break;
      }
    }
    *new_node = NULL ;
    return FALSE;
  }
  else {
    HQASSERT( new_node != NULL , "Unexpected NULL pointer" ) ;
    rtree_split_node( n, b, new_node );
    return TRUE;
  }
}

/** Copy branches from the buffer into two nodes according to the
    partition. */

static void rtree_load_nodes( RTREE_NODE *n, RTREE_NODE *q,
                              struct PartitionVars *p )
{
  uint32 i;

  HQASSERT( n != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( q != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( p != NULL , "Unexpected NULL pointer" ) ;

  for ( i = 0; i < p->total; i++ ) {
    RTREE_NODE *dummy ;

    HQASSERT( p->partition[i] == 0 || p->partition[i] == 1 ,
              "Invalid partition value" ) ;
    if ( p->partition[i] == 0 ) {
      ( void )rtree_add_branch( &BranchBuf[ i ] , n , &dummy ) ;
    }
    else if ( p->partition[i] == 1 ) {
      ( void )rtree_add_branch( &BranchBuf[ i ] , q , &dummy ) ;
    }
  }
}

/** Split a node.  Divides the nodes branches and the extra one
    between two nodes.  Old node is one of the new ones, and one
    really new one is created.  Tries more than one method for
    choosing a partition, uses best result. */

static void rtree_split_node( RTREE_NODE *n, RTREE_BRANCH *b,
                              /*@out@*/ RTREE_NODE **nn )
{
  struct PartitionVars *p;
  uint32 level ;

  HQASSERT( n != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( b != NULL , "Unexpected NULL pointer" ) ;

  /* Load all the branches into a buffer, initialize old node */
  level = n->level;
  rtree_get_branches( n, b );

  /* Find partition */
  p = &Partition;
  rtree_find_split( p, MIN_BRANCH_FACTOR );

  /* Put branches from buffer into 2 nodes according to chosen
     partition */
  *nn = rtree_new_node( n->mem ); /* inherit memory api*/
  ( *nn )->level = n->level = level;
  rtree_load_nodes( n, *nn, p );
  HQASSERT( n->count + ( *nn )->count == p->total , "Total out of whack" ) ;
}

/** Disconnect a dependent node. */

static void rtree_disconnect_branch( RTREE_NODE *n, uint32 i )
{
  HQASSERT( n != NULL && i < MAX_BRANCH_FACTOR ,
            "Nonsensical disconnect");
  HQASSERT( n->branch[i].u.child != NULL , "Unexpected NULL pointer" ) ;

  rtree_init_branch( &( n->branch[i] ));
  n->count--;
}

/** Make a new index, empty.  Consists of a single node. */

RTREE_NODE *rtree_new_index( /*@in@*/ sw_memory_instance *mem )
{
  RTREE_NODE *x;

  HQASSERT( mem != NULL, "Need a memory api");

  if ( mem->implementation->info.version <
        SW_MEMORY_API_VERSION_20071110 ) {
    HQFAIL("Rtree needs version 20071110 or later of SW_MEMORY_API");
    return NULL;
  }

  x = rtree_new_node( mem );
  x->level = 0;                 /* leaf */
  return x;
}

/** Search in an index tree or subtree for all data retangles that
    overlap the argument rectangle. Return the number of qualifying
    data rects. */

int rtree_search( RTREE_NODE *n, RTREE_RECT *r, SearchHitCallback shcb,
                  void *cbarg )
{
  int hitCount = 0;
  uint32 i;

  HQASSERT( n != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( r != NULL , "Unexpected NULL pointer" ) ;

  if ( n->level > 0 ) {
    /* This is an internal node in the tree. */
    for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
      if ( n->branch[ i ].u.child != NULL &&
           rtree_overlap( r, &n->branch[ i ].rect )) {
        hitCount += rtree_search(( RTREE_NODE * )n->branch[ i ].u.child ,
                                 r , shcb , cbarg ) ;
      }
    }
  }
  else {
    /* This is a leaf node. */
    for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
      if ( n->branch[ i ].u.child != NULL &&
           rtree_overlap( r, &n->branch[ i ].rect )) {
        hitCount++;
        if ( shcb != NULL ) {
          /* Call the user-provided callback. */
          if ( !shcb( & n->branch[ i ].rect ,
                      n->branch[ i ].u.child, cbarg )) {
            /* Callback wants to terminate search early. */
            return hitCount;
          }
        }
      }
    }
  }

  return hitCount;
}

/** Inserts a new data rectangle into the index structure. The opaque
    data pointer represents the associated private data record if the
    new node is to be a leaf, or the child node if it
    isn't. Recursively descends tree, propagates splits back up.
    Returns FALSE if node was not split.  Old node updated.  If node
    was split, returns TRUE and sets the pointer pointed to by
    new_node to point to the new node.  Old node updated to become one
    of two.  The level argument specifies the number of steps up from
    the leaf level to insert; e.g. a data rectangle goes in at level =
    0. */

static Bool rtree_insert_rect_internal( RTREE_RECT *r , void *data ,
                                        RTREE_NODE *n,
                                        /*@out@*/ RTREE_NODE **new_node,
                                        uint32 level )
{
  uint32 i;
  RTREE_BRANCH b;
  RTREE_NODE *n2;

  HQASSERT( r != NULL && n != NULL && new_node != NULL ,
            "Unexpected NULL pointer" ) ;
  HQASSERT( level <= n->level , "level out of range" ) ;

  /* Still above level for insertion, go down tree recursively */

  if ( n->level > level ) {
    i = rtree_pick_branch( r, n );
    HQASSERT( n->branch[ i ].u.child != NULL ,
              "Should have picked a viable branch" ) ;
    if ( !rtree_insert_rect_internal( r , data ,
                                      n->branch[ i ].u.child ,
                                      & n2 , level )) {
      /* Child was not split */

      rtree_union_rect( r , &( n->branch[ i ].rect ) , &n->branch[ i ].rect ) ;
      *new_node = NULL ;
      return FALSE;
    }
    else {
      /* Child was split */

      n->branch[ i ].rect = rtree_node_cover( n->branch[ i ].u.child );
      b.u.child = n2;
      b.rect = rtree_node_cover( n2 );
      return rtree_add_branch( &b, n, new_node );
    }
  }
  else if ( n->level == level ) {
    /* We've reached level for insertion. Add the rect, splitting if
       necessary. */

    b.rect = *r ;
    b.u.data = data ;

    return rtree_add_branch( &b, n, new_node ) ;
  }
  else {
    /* Not supposed to happen */
    HQFAIL( "This branch shouldn't be reached" ) ;
    *new_node = NULL ;
    return FALSE;
  }
}

/** Insert a data rectangle into an index structure.
    rtree_insert_rect provides for splitting the root; returns TRUE if
    root was split, FALSE if it was not.  The level argument specifies
    the number of steps up from the leaf level to insert; e.g. a data
    rectangle goes in at level = 0.  rtree_insert_rect_internal does
    the recursion. */

Bool rtree_insert_rect( RTREE_RECT *r , void *data ,
                        RTREE_NODE **root , uint32 level )
{
  uint32 i;
  RTREE_NODE *newnode;
  RTREE_BRANCH b;

  HQASSERT( r != NULL && root != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( level <= ( *root )->level , "Level out of range" ) ;
  for ( i = 0; i < RTREE_DIMENSIONS; i++ ) {
    HQASSERT( r->boundary[ i ] <= r->boundary[RTREE_DIMENSIONS + i] ,
              "Degenerate rectangle boundary" ) ;
  }

  if ( rtree_insert_rect_internal( r , data , *root , & newnode , level )) {
    RTREE_NODE *newroot;
    RTREE_NODE *dummy ;

    /* Root needs to be split - grow a new root and make the tree
       taller */
    newroot = rtree_new_node( (*root)->mem ) ;
    newroot->level = ( *root )->level + 1 ;
    b.rect = rtree_node_cover( *root ) ;
    b.u.child = *root ;
    ( void )rtree_add_branch( &b , newroot , &dummy ) ;
    b.rect = rtree_node_cover( newnode );
    b.u.child = newnode ;
    ( void )rtree_add_branch( &b , newroot , &dummy ) ;
    *root = newroot ;

    return TRUE ;
  }

  return FALSE ;
}

/** Allocate space for a node in the list used in DeletRect to store
    Nodes that are too empty. */

static /*@null@*/ /*@out@*/ /*@only@*/
  RTREE_LISTNODE *rtree_new_list_node( sw_memory_instance *mem )
{
  RTREE_LISTNODE *n = ( RTREE_LISTNODE * )
    mem->implementation->alloc( mem, sizeof ( RTREE_LISTNODE ));

  if ( n != NULL)
    n->mem = mem ;
  return n ;
}

static void rtree_free_list_node( /*@only@*/ RTREE_LISTNODE *p )
{
  if ( p->node != NULL ) {
    rtree_free_node( p->node ) ;
  }

  p->mem->implementation->free( p->mem, p ) ;
}

/** Add a node to the reinsertion list.  All its branches will later
    be reinserted into the index structure. */

static void rtree_reinsert( /*@only@*/ RTREE_NODE *n, RTREE_LISTNODE **ee )
{
  RTREE_LISTNODE *l;

  l = rtree_new_list_node( n->mem );
  HQASSERT( l != NULL , "Should test this rather than assert, "
            "but the example code's flow of control was trickyish" ) ;
  l->node = n;
  l->next = *ee;
  *ee = l;
}

/** Delete a rectangle from non-root part of an index structure.
    Called by rtree_delete_rect.  Descends tree recursively, merges
    branches on the way back up.  Returns 1 if record not found, 0 if
    success. */

static int rtree_delete_rect_internal( RTREE_RECT *r , void *data ,
                                       RTREE_NODE *n , RTREE_LISTNODE **ee )
{
  uint32 i;

  HQASSERT( r != NULL && n != NULL && ee != NULL ,
            "Unexpected NULL pointer" ) ;
  HQASSERT( data != NULL , "Unexpected NULL pointer" ) ;

  if ( n->level > 0 ) {         /* not a leaf node */
    for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
      if ( n->branch[ i ].u.child != NULL &&
           rtree_overlap( r, &( n->branch[ i ].rect )) ) {
        if ( !rtree_delete_rect_internal( r , data ,
                                         n->branch[ i ].u.child , ee )) {
          if ((( RTREE_NODE * )n->branch[ i ].u.child )->count >=
              MIN_BRANCH_FACTOR )
            n->branch[ i ].rect = rtree_node_cover( n->branch[ i ].u.child );
          else {
            /* not enough entries in child, */
            /* eliminate child node */

            rtree_reinsert( n->branch[ i ].u.child , ee ) ;
            rtree_disconnect_branch( n , i ) ;
          }
          return TRUE;
        }
      }
    }
    return FALSE;
  }
  else {                        /* a leaf node */

    for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
      if ( n->branch[ i ].u.child == data ) {
        rtree_disconnect_branch( n, i );
        return TRUE;
      }
    }
    return FALSE;
  }
}

/** Delete a data rectangle from an index structure.  Pass in a
    pointer to a Rect, the data pointer for the record, ptr to ptr to
    root node.  Returns FALSE if record not found, TRUE if success.
    Provides for eliminating the root if necessary. */

Bool rtree_delete_rect( RTREE_RECT *r , void *data , RTREE_NODE **nn )
{
  uint32 i;
  RTREE_NODE *tmp_nptr = NULL;
  RTREE_LISTNODE *reInsertList = NULL;
  RTREE_LISTNODE *e;

  HQASSERT( r != NULL && nn != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( *nn != NULL , "Unexpected NULL pointer" ) ;
  HQASSERT( data != NULL , "Unexpected NULL pointer" ) ;

  if ( !rtree_delete_rect_internal( r , data , *nn , & reInsertList )) {
    /* found and deleted a data item */

    /* reinsert any branches from eliminated nodes */
    while ( reInsertList ) {
      tmp_nptr = reInsertList->node;
      for ( i = 0 ; i < MAX_BRANCH_FACTOR ; i++ ) {
        if ( tmp_nptr->branch[ i ].u.child != NULL ) {
          ( void )rtree_insert_rect( &( tmp_nptr->branch[ i ].rect ) ,
                                     tmp_nptr->branch[ i ].u.child ,
                                     nn , tmp_nptr->level ) ;
        }
      }
      e = reInsertList ;
      reInsertList = reInsertList->next ;
      rtree_free_list_node( e ) ;
    }

    /* check for redundant root (not leaf, 1 child) and eliminate */
    if ( ( *nn )->count == 1 && ( *nn )->level > 0 ) {
      for ( i = 0; i < MAX_BRANCH_FACTOR; i++ ) {
        tmp_nptr = ( *nn )->branch[ i ].u.child ;
        if ( tmp_nptr )
          break;
      }
      HQASSERT( tmp_nptr != NULL , "Unexpected NULL pointer" ) ;
      rtree_free_node( *nn );
      *nn = tmp_nptr;
    }
    return TRUE;
  }
  else {
    return FALSE;
  }
}

/** Deletes the rtree rooted at (*p_node). */
void rtree_dispose( RTREE_NODE **p_node)
{
  RTREE_NODE *node = *p_node;

  if (p_node == NULL) {
    HQFAIL("Disposing NULL rtree!");
    return;
  }

  node = *p_node;
  if (node != NULL)  {/* just in case */
    if ( node->level > 0 ) {
      uint32 i = node->count;

      /* non-leaf; delete each branch node. */
      while ( i > 0 ) {
        rtree_dispose( &node->branch[--i].u.child );
        --(node->count);
      }
    }
  }

  rtree_free_node(node);
  *p_node = NULL;
}

#if PRINT_PVARS_REQUIRED
/** Debug dump for a partition from PartitionVars struct. Not called
    from the code. */

static void rtree_print_pvars( struct PartitionVars *p )
{
  uint32 i;

  HQASSERT( p != NULL , "Unexpected NULL pointer" ) ;

  monitorf(( uint8 * )"\npartition:\n" );
  for ( i = 0; i < p->total; i++ ) {
    monitorf(( uint8 * )"%3d\t", i );
  }
  monitorf(( uint8 * )"\n" );
  for ( i = 0; i < p->total; i++ ) {
    if ( p->taken[i] )
      monitorf(( uint8 * )"  t\t" );
    else
      monitorf(( uint8 * )"\t" );
  }
  monitorf(( uint8 * )"\n" );
  for ( i = 0; i < p->total; i++ ) {
    monitorf(( uint8 * )"%3d\t", p->partition[i] );
  }
  monitorf(( uint8 * )"\n" );

  monitorf(( uint8 * )"count[0] = %d  area = %f\n", p->count[0], p->area[0] );
  monitorf(( uint8 * )"count[1] = %d  area = %f\n", p->count[1], p->area[1] );
  if ( p->area[0] + p->area[1] >= DBL_EPSILON ) {
    monitorf(( uint8 * )"total area = %f  effectiveness = %3.2f\n",
            p->area[0] + p->area[1],
            ( float ) CoverSplitArea / ( p->area[0] + p->area[1] ) );
  }
  monitorf(( uint8 * )"cover[0]:\n" );
  rtree_print_rect( &p->cover[0], 0 );

  monitorf(( uint8 * )"cover[1]:\n" );
  rtree_print_rect( &p->cover[1], 0 );
}
#endif

/* Log stripped */
