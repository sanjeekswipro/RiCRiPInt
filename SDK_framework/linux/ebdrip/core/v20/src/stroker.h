/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!src:stroker.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Structs and enums used in stroker() path stroking algorithm
 */

#ifndef __STROKER_H__
#define __STROKER_H__

/*
 * The external API to the stroker algorithm
 */
int32 stroker(STROKE_PARAMS *sp,  PATHLIST *path);

/*
 * The callback methods used by the stroker() algorithm
 */
typedef struct STROKE_METHODS
{
  Bool (*stroke_begin)(STROKE_PARAMS *sp, void **data, int32 ntries) ;
  Bool (*stroke_end)(STROKE_PARAMS *sp, Bool result, void *data) ;
  Bool (*segment_begin)(STROKE_PARAMS *sp, void *data) ;
  Bool (*segment_end)(STROKE_PARAMS *sp, Bool join_AtoB, void *data) ;
  Bool (*quad)(STROKE_PARAMS *sp, FPOINT pts[4], void *data) ;
  Bool (*triangle)(STROKE_PARAMS *sp, FPOINT pts[3], Bool side_A, void *data) ;
  Bool (*curve)(STROKE_PARAMS *sp, FPOINT pts[5], Bool side_A, void *data) ;
  Bool (*line)(STROKE_PARAMS *sp, FPOINT pts[2], void *data) ;
  Bool (*miter)(STROKE_PARAMS *sp, FPOINT pts[1], Bool side_A,
                Bool intersect, void *data) ;
} STROKE_METHODS;

/*
 * A point held as its original amd its strokedadjusted value.
 */
typedef struct POINT_INFO
{
  FPOINT point, adj;
} POINT_INFO;

/*
 * The status of where we are within a dash element.
 */
enum
{
  DASH_START  = 1,
  DASH_MIDDLE = 2,
  DASH_END    = 3,
  DASH_GAP    = 4
};

/*
 * The status of which section we are within the sub-path we are processing.
 * This used to determine what type of linecap we use.
 * So the bit we put the startcap on is the FIRST_SECTION, the bit that has
 * an endcap is the LAST_SECTION, and anything in between may have dashcaps.
 * Unstroked segments work by restarting the state machine, but ensuring we
 * are no longer in the FIRST_SECTION, thus preventing incorrect use of
 * startcaps.
 * Note : these are flags, as a sub-path consiting of a single point needs
 * to be dealt with as FIRST_SECTION|LAST_SECTION.
 */
enum
{
  FIRST_SECTION  = 0x1,
  MIDDLE_SECTION = 0x2,
  LAST_SECTION   = 0x4
};

/*
 * The state of the miter we are processing. The first four value gives
 * possible options for the external miter, plus we also track wether the
 * internal miter is to be used or not. Miter state may be any of the
 * first four, possibly OR'd with the last one.
 */
enum
{
  MITER_AT_INFINITY = 0x01, /* lines double back, so miter at infinity */
  MITER_TOO_LONG    = 0x02, /* bigger than miterlimit, and not clipped */
  MITER_OK          = 0x04, /* use miter, it is within limits */
  MITER_CLIPPED     = 0x08, /* too big, but clipped (for XPS) */

  MITER_INTERNAL    = 0x10  /* Internal miter is hidden, so use it */
};

/*
 * All the state information of the stroking algorithm.
 */
typedef struct STROKER_STATE
{
  STROKE_PARAMS *sp;    /* The input stroke parameters, e.g. linewidth etc. */
  STROKE_METHODS *sm;   /* the callback methods */
  void *data;           /* opaque private data for callback methods */
  Bool poorstrokepath;
  Bool adobesetlinejoin;

  Bool isclosed;        /* is the current path closed or open */
  int32 section;        /* which section are we in (for line cap choice) */
  int32 npoints;        /* how many points has the stroking algorithm seen */
  int32 points_coincident; /* how many coincident points have been rejected */
  POINT_INFO p[3];      /* The last three points seen */
  Bool have_norm;       /* Have we calculated the normal for the line */
  FVECTOR norm[2];      /* the two normals for the last three points seen */
  int32 axes_mirrored;  /* i.e. which way is clockwise */
  Bool bez_internal;    /* Is the current point part of a flattened bezier */
  struct /* remember the path beginning, in case we need it at the end */
  {
    POINT_INFO p1;      /* The 1st point in the path and ... */
    FVECTOR n1;         /* The 1st normal, in case we need to re-do the cap */
    Bool have_p2;       /* Have we seen the 2nd point yet ? */
    FPOINT p2;          /* The 2nd point in the path, for start<->end join */
  } begin;
  struct /* private state info for dash code */
  {
    Bool startsolid;        /* does the path start on a solid dash */
    int32 state;            /* are we on a dash or in a gap */
    FPOINT point;           /* start co-ord of the dash */
    FPOINT lastdash;        /* last dash for supressing co-incident points */
    SYSTEMVALUE remains;    /* how much of the dash length is left to process */
    SYSTEMVALUE totalLen;   /* total length of all the dashes */
    int32 index;            /* current index into the array of dashes */
  } dash;
  struct /* private state info for unstroked segments code */
  {
    Bool start;   /* Does path start with an unstroked segment */
    Bool current; /* Is current segment unstroked */
    Bool any;     /* Are there any unstroked segments in the path */
    Bool line;    /* Have there been unstroked segments in this line */
  } unstroked;
} STROKER_STATE;

#endif /* protection for multiple inclusion */

/* Log stripped */
