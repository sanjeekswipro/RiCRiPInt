/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!export:gu_path.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for manipulating paths.
 */

#ifndef __GU_PATH_H__
#define __GU_PATH_H__

#include "mm.h"     /* mm_pool_t */
#include "matrix.h" /* OMATRIX */
#include "paths.h"  /* PATHINFO et. al. */

/* Routines to initialise a path and add line segments to an existing path.
   The coordinates are the device coordinates added to the path. No
   transformation or rangechecking is performed on the coordinates. */
void path_init(/*@notnull@*/ /*@out@*/ PATHINFO *path);
Bool path_moveto(SYSTEMVALUE x, SYSTEMVALUE y, int32 type,
                 /*@notnull@*/ /*@in@*/ PATHINFO *path);
Bool path_segment(SYSTEMVALUE x, SYSTEMVALUE y,
                  int32 type, Bool stroked,
                  /*@notnull@*/ /*@in@*/ PATHINFO *path);
Bool path_curveto(register SYSTEMVALUE args[6], Bool stroked,
                  /*@notnull@*/ /*@in@*/ PATHINFO *path);
Bool path_close(int32 type,
                /*@notnull@*/ /*@in@*/ PATHINFO *path);


/* Static pathlists. These structures are provided pre-prepared and linked
   together to make setting up and using small paths easier. In general, they
   should be used by calling the fill_* macros (or the add_* functions in
   gu_line/cline et. al.), and then immediately copied or passed to a
   function which will read them. The add_* macros will perform this second
   step, adding the sub-path filled to stoppath. The structures should never
   be re-linked in a different order or pointers to them stored. Assertions
   against theISystemAlloc() being PATHTYPE_STATIC can be used to detect
   these paths are being mis-used.

   The structures share as many linelist structures as possible, so
   interleaving calls to different fill_* macros is not safe without copying
   the results in between.

   The main head links are:

   i3cpath: PATHINFO for a 3-point closed path. Use fill_three to set points.
   i4cpath: PATHINFO for a 4-point closed path. Use fill_four to set points.
   
   p2cpath: PATHLIST for a 2-point degenerate path. Use fill_two to set points.
   p3cpath: PATHLIST for a 3-point closed path. Use fill_three to set points.
   p4cpath: PATHLIST for a 4-point closed path. Use fill_four to set points.

   There are other specialised links for arc and corner generation:

   p4curve:  PATHLIST for a closed four curve segment path.
   p2curve:  PATHLIST for a closed two curve segment path.
   p1curve:  PATHLIST for a closed one curve segment path.
   p1lcurve: PATHLIST for a closed line then one curve segment path.
   p2lcurve: PATHLIST for a closed line then two curve segment path.

   */

#define path_fill_two( x1 , y1 , x2 , y2 ) MACRO_START \
  HQASSERT(p2move.next == &plinez && \
           plinez.next == &pclosep && \
           pclosep.next == NULL, \
           "Static linelist order has been changed") ; \
  theX( thePoint( p2move )) = (x1) ; \
  theY( thePoint( p2move )) = (y1) ; \
  theX( thePoint( plinez )) = (x2) ; \
  theY( thePoint( plinez )) = (y2) ; \
  theX( thePoint( pclosep )) = (x1) ; \
  theY( thePoint( pclosep )) = (y1) ; \
MACRO_END

#define path_fill_three( x1 , y1 , x2 , y2 , x3 , y3 ) MACRO_START \
  HQASSERT(p3move.next == &pliney && \
           pliney.next == &plinez && \
           plinez.next == &pclosep && \
           pclosep.next == NULL, \
           "Static linelist order has been changed") ; \
  theX( thePoint( p3move )) = (x1) ; \
  theY( thePoint( p3move )) = (y1) ; \
  theX( thePoint( pliney )) = (x2) ; \
  theY( thePoint( pliney )) = (y2) ; \
  theX( thePoint( plinez )) = (x3) ; \
  theY( thePoint( plinez )) = (y3) ; \
  theX( thePoint( pclosep )) = (x1) ; \
  theY( thePoint( pclosep )) = (y1) ; \
  i3cpath.bboxtype = BBOX_NOT_SET ; \
MACRO_END

#define path_fill_four( x1 , y1 , x2 , y2 , x3 , y3 , x4 , y4 ) MACRO_START \
  HQASSERT(p4move.next == &plinex && \
           plinex.next == &pliney && \
           pliney.next == &plinez && \
           plinez.next == &pclosep && \
           pclosep.next == NULL, \
           "Static linelist order has been changed") ; \
  theX( thePoint( p4move )) = (x1) ; \
  theY( thePoint( p4move )) = (y1) ; \
  theX( thePoint( plinex )) = (x2) ; \
  theY( thePoint( plinex )) = (y2) ; \
  theX( thePoint( pliney )) = (x3) ; \
  theY( thePoint( pliney )) = (y3) ; \
  theX( thePoint( plinez )) = (x4) ; \
  theY( thePoint( plinez )) = (y4) ; \
  theX( thePoint( pclosep )) = (x1) ; \
  theY( thePoint( pclosep )) = (y1) ; \
  i4cpath.bboxtype = BBOX_NOT_SET ; \
MACRO_END

/* Routines to add closed subpaths with specific numbers of points to an
   existing path. */
Bool path_add_two( PATHINFO *topath ,
                   SYSTEMVALUE x1 , SYSTEMVALUE y1 ,
                   SYSTEMVALUE x2 , SYSTEMVALUE y2 ) ;

Bool path_add_three( PATHINFO *topath ,
                     SYSTEMVALUE x1 , SYSTEMVALUE y1 ,
                     SYSTEMVALUE x2 , SYSTEMVALUE y2 ,
                     SYSTEMVALUE x3 , SYSTEMVALUE y3 ) ;

Bool path_add_four( PATHINFO *topath ,
                    SYSTEMVALUE x1 , SYSTEMVALUE y1 ,
                    SYSTEMVALUE x2 , SYSTEMVALUE y2 ,
                    SYSTEMVALUE x3 , SYSTEMVALUE y3 ,
                    SYSTEMVALUE x4 , SYSTEMVALUE y4 ) ;

#define path_add_asquare( path_dst , x1 , y1 , x2 , y2 , dx , dy ) \
  path_add_four( path_dst , \
            (x1) + (dx) , (y1) + (dy) , \
            (x2) + (dx) , (y2) + (dy) , \
            (x2) - (dx) , (y2) - (dy) , \
            (x1) - (dx) , (y1) - (dy) )

extern LINELIST p4move, p3move, p2move, plinex, pliney, plinez, pclosep ;
extern LINELIST pcurvew1, pcurvew2, pcurvew3, pcurvex1, pcurvex2, pcurvex3,
                pcurvey1, pcurvey2, pcurvey3, pcurvez1, pcurvez2, pcurvez3 ;
extern LINELIST p4cmove, p2cmove, p1cmove ;
extern LINELIST p1lcmove , p1lcline , p2lcmove , p2lcline;
extern PATHLIST p4cpath, p3cpath, p2cpath, p4curve, p2curve, p1curve ;
extern PATHLIST p1lcurve , p2lcurve ;
extern PATHINFO i4cpath , i3cpath ;

#define fl_getftol() ( fl_ftol )
#define fl_getflat() ( fl_flat )

void fl_setflat( USERVALUE flat ) ;

/* BBox functions. path_bbox_subpath calculates the bbox of the first
   subpath in a PATHLIST, replacing the bbox (or adding it to bbox if the
   BBOX_UNION flag is set). The ignore flags determine how trailing movetos
   are treated, and are stored in the bbox type member of path_bbox's PATHINFO,
   so that the calculation need not be repeated if the correct bbox type was
   already set. path_bbox returns the bbox parameter, suitably filled in. If no
   bbox parameter is provided, then the BBOX_SAVE and BBOX_LOAD flags must be
   set, and the PATHINFO's cached bbox will be returned, suitably filled in.
   path_transform_bbox is used by HDLT to return a minimal bounding box in a
   transformed space. It cannot take the BBOX_SAVE or BBOX_LOAD options. */

void path_bbox_subpath(
  /*@notnull@*/ /*@in@*/        PATHLIST *thepath ,
  /*@notnull@*/ /*@out@*/       sbbox_t *bbox ,
                                uint32 flags ) ;

sbbox_t *path_bbox(
  /*@notnull@*/ /*@in@*/        PATHINFO *thepath ,
  /*@null@*/ /*@out@*/          sbbox_t *bbox ,
                                uint32 flags ) ;

sbbox_t *path_transform_bbox(
  /*@notnull@*/ /*@in@*/        PATHINFO *thepath ,
  /*@notnull@*/ /*@in@*/        sbbox_t *bbox ,
                                uint32 flags ,
  /*@notnull@*/ /*@in@*/        OMATRIX *matrix ) ;

/* Flatten a path; path_flatten copies the path, even if there are no curves
   in it. */
Bool path_flatten(
  /*@notnull@*/ /*@in@*/        PATHLIST *thepath ,
  /*@notnull@*/ /*@out@*/       PATHINFO *flatpath ) ;

/* Copying functions. path_copy copies a whole path, including the header.
   path_copy_list copies a linked list of subpaths. path_append_subpath_copy
   adds a copy of a single subpath to the end of a path. path_append_subpath
   directly links a single subpath to the end of a path, and should not be
   used for statically allocated paths. path_remove_last_subpath removes
   the last subpath from a path, adjusting the pointers as necessary, and
   returning the subpath removed. */
Bool path_copy(
  /*@notnull@*/ /*@out@*/       PATHINFO *topath ,
  /*@notnull@*/ /*@in@*/        PATHINFO *frompath ,
  /*@notnull@*/ /*@in@*/        mm_pool_t pool ) ;

Bool path_copy_lazy(
  /*@notnull@*/ /*@out@*/       PATHINFO *topath ,
  /*@notnull@*/ /*@in@*/        PATHINFO *frompath ,
  /*@notnull@*/ /*@in@*/        mm_pool_t pool ) ;

Bool path_copy_list(
  /*@notnull@*/ /*@in@*/        PATHLIST *frompath ,
  /*@null@*/ /*@out@*/          PATHLIST **fpath ,
  /*@null@*/ /*@out@*/          PATHLIST **lpath ,
  /*@null@*/ /*@out@*/          LINELIST **lline ,
  /*@notnull@*/ /*@in@*/        mm_pool_t pool ) ;

Bool path_append_subpath_copy(
  /*@notnull@*/ /*@in@*/        PATHLIST *thepath ,
  /*@notnull@*/ /*@out@*/       PATHINFO *topath ,
  /*@notnull@*/ /*@in@*/        mm_pool_t pool ) ;

void path_append_subpath(
  /*@notnull@*/ /*@in@*/        PATHLIST *thepath ,
  /*@notnull@*/ /*@in@*/        LINELIST *theline ,
  /*@notnull@*/ /*@out@*/       PATHINFO *topath ) ;

void path_remove_last_subpath(
  /*@notnull@*/ /*@in@*/ /*@out@*/ PATHINFO *frompath,
  /*@notnull@*/ /*@out@*/          PATHLIST **removed) ;

/* Transformation functions. path_transform performs a general matrix
   transformation to each point in a path. path_translate adds x,y offsets to
   each point in a path. */
void path_transform(
  /*@notnull@*/ /*@in@*/        PATHINFO *path ,
  /*@notnull@*/ /*@in@*/        OMATRIX *t_matrix ) ;

void path_translate(
  /*@notnull@*/ /*@in@*/        PATHINFO *path ,
                                SYSTEMVALUE tx ,
                                SYSTEMVALUE ty ) ;

/* Counting and reversing functions. path_count_subpaths counts the number of
   subpaths in a path (this is used when analysing vignette paths).
   path_reverse_linelists reverses the direction of each subpath, but does
   not reverse the order of subpaths. path_reverse_subpaths reverses the
   order of subpaths in a path, but does not reverse the direction of each
   subpath contour. */
Bool path_count_subpaths(
  /*@notnull@*/ /*@in@*/        PATHINFO *path ) ;

void  path_reverse_linelists(
  /*@notnull@*/ /*@in@*/        PATHLIST *thepath ,
  /*@null@*/ /*@out@*/          LINELIST **lastline ) ;

void path_reverse_subpaths(
  /*@notnull@*/ /*@in@*/        PATHLIST **thepath ,
  /*@null@*/ /*@out@*/          LINELIST **lastline ) ;

/* Get a path from a transformed array of (userspace) rectangles,
   obliterating previous path. */
Bool path_from_rectangles(PATHINFO *path, RECTANGLE *rects, uint32 nrects,
                          OMATRIX *matrix) ;

/* Utility function to compare paths, with an epsilon value. An epsilon of
   0 means exact comparison. */
enum { PATH_COMPARE_COORDS = 0, /* Coordinates are always compared */
       PATH_COMPARE_BBOX = 1,
       PATH_COMPARE_FLAGS = 2,
       PATH_COMPARE_NORMAL = (PATH_COMPARE_BBOX|PATH_COMPARE_FLAGS) } ;
Bool path_compare(PATHINFO *path1, PATHINFO *path2, uint32 flags,
                  SYSTEMVALUE epsilon) ;


/* Utility function to provide a checksum and storage space required for a
   path. */
void path_checksum(PATHINFO *path, uint32 *checkptr, uint32 *sizeptr) ;

/* Utility function to append path to another. */
enum { PATH_APPEND_PATCH = 0,   /* Source path is appended directly */
       PATH_APPEND_COPY = 1 } ; /* Copy of source path is appended */
Bool path_append(PATHINFO *topath, PATHINFO *path, uint32 flags) ;

#ifdef ASSERT_BUILD
Bool path_assert_valid(PATHINFO *path) ;
#endif

/* External variables defined in gu_path.c */

extern USERVALUE fl_ftol ;
extern USERVALUE fl_flat ;

extern int32 nooflines ;
extern uint8 flatorder ;
extern LINELIST *flatline ;

#endif /* protection for multiple inclusion */

/* Log stripped */
