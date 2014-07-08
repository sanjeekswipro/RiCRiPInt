/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_bres.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Converts an arbitrary PDL path into a format suitable for Display-list
 * storage.
 *
 * A path is represented by a NFILL structure. Within the NFILL structure
 * each edge of the path is represented by a separate 'thread', that is a
 * series of vectors for which the y values are going in the same direction.
 * i.e. up or down. Each thread is stored in a NBRESS structure. The routines
 * in this file create and manage NFILL and NBRESS structures. Firstly they
 * create NFILL+NBRESS structures from arbitrary PDL input paths. Then they
 * manage the traversal of NFILLs during the rendering of vector objects in
 * the Display List.
 *
 * \todo BMJ 21-Aug-08 :  Need to figure out how to commonise the regular
 * DL allocs and the promises so I can use MM_ALLOC_CLASS_NFILL
 */

#include "core.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "dl_bres.h"
#include "asyncps.h"
#include "progupdt.h"

#include "swerrors.h"
#include "swdevice.h"
#include "swoften.h"
#include "mm.h"
#include "mmcompat.h"
#include "scanconv.h"
#include "monitor.h"
#include "timing.h"
#include "tables.h"

#include "matrix.h"
#include "display.h"
#include "dl_store.h"
#include "graphics.h"
#include "dlstate.h"
#include "ndisplay.h"
#include "stacks.h"
#include "often.h"
#include "bresfill.h"
#include "fbezier.h"
#include "gu_path.h"
#include "gstate.h"
#include "render.h"
#include "devops.h"
#include "params.h"
#include "fonth.h"
#include "system.h"

#include "pathops.h"
#include "idlom.h"

#include "routedev.h"
#include "dl_free.h"
#include "trap.h"
#include "rcbtrap.h"
#include "rcbcntrl.h"
#include "wclip.h"

/*
 * Set of functions to modularise access to list of dx,dy deltas stored at
 * the end of the NBRESS object.
 * Note list is of variable length and variable type, which is hard to
 * declare in 'C'. So its declared as array of 1 int32, and the appropriate
 * casts occur in these access functions
 */

/**
 * Initialise list of dx,dy deltas, and set it to empty.
 * \param[in]   dxy   list to initialise
 */
void dxylist_init(DXYLIST *dxy)
{
  dxy->deltai = dxy->ndeltas = 0;
  dxy->deltabits = 16;
}

/**
 * Reset list of dx,dy deltas so that next read will start at the beginning.
 * \param[in]   dxy   list to reset
 */
void dxylist_reset(DXYLIST *dxy)
{
  dxy->deltai = 0;
}

/**
 * Get the next dx,dy deltas from the given list, indicating if we are at the
 * end of the list or not.
 * \param[in]   dxy   list to read
 * \param[out]   px   pointer to x value to be incremented by next dx
 * \param[out]   py   pointer to y value to be incremented by next dy
 * \return            Is there a next delta available ?
 */
Bool dxylist_get(DXYLIST *dxy, dcoord *px, dcoord *py)
{
  HQASSERT(dxy->deltai <= dxy->ndeltas, "Corrupt DXYLIST");

  if ( dxy->deltai == dxy->ndeltas )
    return FALSE;

  switch ( dxy->deltabits ) {
    int32 dxdy;
    /*
     * Unpack from variable size container. Note dy is unsigned and at
     * the bottom of the container, so can just be ANDed out. But dx
     * is signed so care has to be taken with shift.
     * can just be
     */
  case 4:
    dxdy = (int32)(((int8 *)(dxy->delta))[dxy->deltai++]);
    HQASSERT(dxdy != 0, "Shouldn't have 0,0 in delta list") ;
    *px += (dcoord)BIT_SHIFT32_SIGNED_RIGHT_EXPR(dxdy, 4);
    *py += (dcoord)(dxdy & 0xF);
    break;

  case 8:
    dxdy = (int32)(((int16 *)(dxy->delta))[dxy->deltai++]);
    HQASSERT(dxdy != 0, "Shouldn't have 0,0 in delta list") ;
    *px += (dcoord)BIT_SHIFT32_SIGNED_RIGHT_EXPR(dxdy, 8);
    *py += (dcoord)(dxdy & 0xFF);
    break;

  case 16:
    dxdy = dxy->delta[dxy->deltai++];
    HQASSERT(dxdy != 0, "Shouldn't have 0,0 in delta list") ;
    *px += (dcoord)BIT_SHIFT32_SIGNED_RIGHT_EXPR(dxdy, 16);
    *py += (dcoord)(dxdy & 0xFFFF);
    break;

  default:
    HQFAIL("Illegal deltabits value");
  }

  return TRUE;
}

/**
 * Store given dx, dy value in the next slot in the delta list
 * Use default 16bit format, may get compacted to a smaller size later.
 * \param[in]   dxy   list to use
 * \param[in]    dx   x delta to store
 * \param[in]    dx   y delta to store
 */
void dxylist_store(DXYLIST *dxy, dcoord dx, dcoord dy)
{
  HQASSERT(dxy->ndeltas < 255, "Overflowing deltas");
  HQASSERT(dy >= 0, "dy should always be +ve");
  HQASSERT(dx != 0 || dy != 0, "Shouldn't be storing 0,0 in delta list") ;
  HQASSERT(!dxylist_toobig(dx, dy), "dx,dy too large");
  dxy->delta[dxy->ndeltas++] = ((dx<<16)|dy);
}

/**
 * Compare the two dx.dy lists for equality
 * \param[in]   dxy1  First list
 * \param[in]   dxy2  Second list
 * \return            Are the lists equal ?
 */
Bool dxylist_equal(DXYLIST *dxy1, DXYLIST *dxy2)
{
  int32 bytes;

  if ( dxy1->ndeltas != dxy2->ndeltas )
    return FALSE;

  if ( dxy1->deltabits != dxy2->deltabits )
    return FALSE;

  bytes = (dxy1->ndeltas * dxy1->deltabits * 2)/8;
  return ( HqMemCmp((const uint8 *)dxy1->delta, bytes,
                    (const uint8 *)dxy2->delta, bytes) == 0 );
}

/**
 * Test to see if the given dx,dy list is empty or not.
 * \param[in]   dxy  Delta list to test
 * \return           Is the list empty ?
 */
Bool dxylist_empty(DXYLIST *dxy)
{
  return dxy->ndeltas == 0;
}

/**
 * Test to see if the given dx,dy list is full or not.
 * \param[in]   dxy  Delta list to test
 * \return           Is the list full ?
 */
Bool dxylist_full(DXYLIST *dxy)
{
  return dxy->ndeltas == 255;
}

/**
 * Test to see if the given dx,dy pair are too big to store in a delta list
 * \param[in]   dx   x delta value to test
 * \param[in]   dy   y delta value to test
 * \return           Are the values too big to fit in a list ?
 */
Bool dxylist_toobig(dcoord dx, dcoord dy)
{
  return ( dx > 0x7FFF || dy > 0x7FFF || dx < -0x7FFF || dy < -0x7FFF );
}

/**
 * Read a given delta without altering the current index
 * \param[in]   dxy   list to read
 * \param[in]   pos   Index into delta list to read
 * \param[out]  pdx   pointer to dx value to be return
 * \param[out]  pdy   pointer to dy value to be return
 */
static void dxylist_read(DXYLIST *dxy, uint8 pos, dcoord *pdx, dcoord *pdy)
{
  uint8 deltai = dxy->deltai;

  HQASSERT(pos < dxy->ndeltas, "DXYLIST index out of range");
  dxy->deltai = pos;
  *pdx = *pdy = 0;
  (void)dxylist_get(dxy, pdx, pdy); /* Can't fail because of assert above */
  dxy->deltai = deltai;
}

/**
 * Write given dx, dy value in the specified slot in the delta list.
 * List must be uncompacted so can just do write in 16-bit format.
 * \param[in]   dxy   list to use
 * \param[in]   pos   position to write to
 * \param[in]    dx   x delta to store
 * \param[in]    dx   y delta to store
 */
static void dxylist_write(DXYLIST *dxy, uint8 pos, dcoord dx, dcoord dy)
{
  HQASSERT(pos < dxy->ndeltas, "DXYLIST index out of range");
  HQASSERT(dy >= 0, "dy should always be +ve");
  HQASSERT(!dxylist_toobig(dx, dy), "dx,dy too large");
  HQASSERT(dxy->deltabits == 16, "Can't write to a compacted dxylist");
  dxy->delta[pos] = (dx<<16)|dy;
}

static uint8 dxylist_used(DXYLIST *dxy)
{
  return dxy->ndeltas;
}

static void dxylist_compact(DXYLIST *dxy, mm_pool_t pool)
{
  if ( dxy->ndeltas >= 2 ) {
    uint8 i, bits = 4; /* Assume 4 bits will do */

    HQASSERT(dxy->deltabits == 16, "dxylist already compressed");
    for ( i = 0; i < dxy->ndeltas; i++ ) {
      int32 dxdy32 = dxy->delta[i];

      if ( dxdy32 & 0x0000FF00 )
        return;
      else if ( dxdy32 & 0x000000F0 )
        bits = 8;
      dxdy32 = BIT_SHIFT32_SIGNED_RIGHT_EXPR(dxdy32, 16);
      if ( dxdy32 < -127 || dxdy32 > 127 )
        return;
      else if ( dxdy32 < -7 || dxdy32 > 7 )
        bits = 8;
    }
    dxy->deltabits = bits;
    if ( bits == 4 ) {
      for ( i = 0; i < dxy->ndeltas; i++ ) {
        int32 dxdy32 = dxy->delta[i];
        int8  dxdy8, *dp = (int8 *)(dxy->delta);

        dxdy8 = (int8)((dxdy32&0xf)+((dxdy32>>12)&0xf0));
        dp[i] = dxdy8;
      }
      mm_dl_promise_shrink(pool, 3*dxy->ndeltas);
    }
    else { /* bits == 8 */
      for ( i = 0; i < dxy->ndeltas; i++ ) {
        int32 dxdy32 = dxy->delta[i];
        int16 dxdy16, *dp = (int16 *)(dxy->delta);

        dxdy16 = (int16)((dxdy32&0xff)+((dxdy32>>8)&0xff00));
        dp[i] = dxdy16;
      }
      mm_dl_promise_shrink(pool, 2*dxy->ndeltas);
    }
  }
}

/**
 * Size of local queue of points saved at the start of a path, and that later
 * glued back on the end.
 */
#define MAX_QUEUED 256

/* Global variables
 * ================
 */
struct nfill_builder_t {
  mm_pool_t pool;     /**< DL memory pool for threads. */
  uint8 *promise;     /**< promise memory allocation for nfill object */
  size_t size;        /**< Size of promise memory to allocate */
  /* Fill variables. */
  NFILLOBJECT nfill;  /**< NFILL being built before its copied into DL. */
  Bool xyswapable;    /**< Should X and Y be swappable for this fill? */
  Bool is_char;       /**< Does the fill originate from a text glyph? */
  Bool is_stroke;     /**< Does the fill originate from a stroke? */
  /* Thread variables. */
  NBRESS *fnbress;    /**< Current NBRESS under construction. */
  uint32 nosegments;  /**< Number of segments in threads. */
  int32 orientation;  /**< Orientation of current thread. */
  dcoord xdirection;  /**< Current thread X direction. */
  dcoord ydirection;  /**< Current thread Y direction. */
  Bool newsubpath;    /**< Does this thread start a new subpath? */
  Bool newthread;     /**< Do we need to start a new thread? */
  Bool had_degen;     /**< Have we stored a degnerate line segment ? */
  /* Segment variables. */
  dcoord curx;        /**< Current segment X position. */
  dcoord cury;        /**< Current segment Y position. */
  Bool   qon;         /**< Has the (x,y) queue been turned on */
  int32  queued;      /**< How many (x,y) points to we have queued up */
  int32  qdir;        /**< Which direction are the queued-points going */
  struct {
    dcoord x, y;
  } q[MAX_QUEUED];    /**< array of queued-up points */
  dcoord lasty;       /**< Final Y coordinate of thread. */
};

/* Flags for clipping, determining which sides of the clip box segments are
   omitted from. */
enum {
  CLIPPED_RIGHT     = 0x01,
  CLIPPED_ABOVE     = 0x02,
  CLIPPED_BELOW     = 0x04,
  CLIPPED_LEFT      = 0x08,
  CLIPPED_UNCLIPPED = 0x10
} ;

/**
 * Keep nfill information local to this file, but provide a function to
 * return the size of the main structure so that it can be allocated
 * externally if required.
 * \return       Size of nfill_builder_t structure
 */
size_t nfill_builder_size(void)
{
  return sizeof(nfill_builder_t);
}

/**
 * Begin the creation of an nfill structure.
 * All the nfill state variables during creation are stored in the passed
 * nbuild structure. A maximum amount of nfill memory to be used is also
 * specified.
 * \param[in,out] nbuild  Pointer to structure holding nfill state
 * \param[in]     size    Maximum nfill memory to use
 */
void start_nfill(DL_STATE *page, nfill_builder_t *nbuild,
                 size_t size, uint32 flags)
{
  HQASSERT(size > 0, "start_nfill: size must be > 0");
  HQASSERT(nbuild, "No NFILL builder");

  nbuild->pool = dl_choosepool(page->dlpools, MM_ALLOC_CLASS_NFILL);
  nbuild->size = size;
  nbuild->promise = NULL;
  nbuild->xyswapable = ((flags & NFILL_XYSWAP) != 0) ;
  nbuild->is_char    = ((flags & NFILL_ISCHAR) != 0) ;
  nbuild->is_stroke  = ((flags & NFILL_ISSTRK) != 0) ;
  nbuild->fnbress = NULL;
  nbuild->nosegments = 0;
  nbuild->orientation = NORIENTDOWN;
  nbuild->xdirection = nbuild->ydirection = 0;
  nbuild->newsubpath = TRUE;
  nbuild->newthread = TRUE;
  nbuild->had_degen = FALSE;
  nbuild->curx = nbuild->cury = nbuild->lasty = 0; /* For safety */
  nbuild->queued = nbuild->qdir = 0;
  nbuild->qon = TRUE;
  nbuild->nfill.rcbtrap = NULL;
  nbuild->nfill.clippedout = 0;
  nbuild->nfill.nthreads = 0;
  nbuild->nfill.startnptr = nbuild->nfill.endnptr = NULL;
}

/**
 * End the creation of an nfill structure.
 * \param[in,out] nbuild  Pointer to structure holding nfill state
 */
void end_nfill(nfill_builder_t *nbuild)
{
  HQASSERT(nbuild, "No NFILL builder");

  /* If something failed along the way, the builder's nfill pointer will not
     have been freed. */
  if ( nbuild->promise ) {
    mm_dl_promise_free(nbuild->pool);
    nbuild->promise = NULL;
  }
}

/**
 * Determine the size in bytes occupied by the given NFILL
 * \param[in] nfill NFILL structure to measure
 * \return          Number of bytes used
 */
size_t sizeof_nfill(const NFILLOBJECT *nfill)
{
  uint8 *start, *end;
  ptrdiff_t nfillsize;
  int32 thd;

  HQASSERT(nfill, "nfill NULL");

  /*
   * The NFILL object and the consecutive NBRESS chains were all allocated
   * together, the chains first and the parent nfill at the end.
   * Have to search through the NBRESS chains to find the first one in memory,
   * as they may have been sorted from their original allocation order.
   */
  end = (uint8 *)&(nfill->thread[nfill->nthreads]);
  start = end;
  for ( thd = 0; thd < nfill->nthreads; thd++ ) {
    if ( start > (uint8 *)(nfill->thread[thd]) )
      start = (uint8 *)(nfill->thread[thd]);
  }
  nfillsize = end - start;
  return nfillsize;
}

size_t sizeof_nbress(const NBRESS *nbress)
{
  size_t size ;

  HQASSERT(nbress, "No nbress");
  size = sizeof(NBRESS) - sizeof(int32) +
    (nbress->dxy.ndeltas * nbress->dxy.deltabits >> 2) ;
  size = SIZE_ALIGN_UP_P2(size, sizeof(NBRESS *)) ;
  return size ;
}

/**
 * Free a (possibly partially created) nfill structure
 * \param[in] nfill  NFILL structure to free
 * \param[in] pools  memory pool in which it was allocated
 */
void free_nfill(NFILLOBJECT *nfill, mm_pool_t *pools)
{
  uint8 *start, *end;
  ptrdiff_t nfillsize;
  int32 thd;

  HQASSERT(nfill, "nfill NULL");
  HQASSERT(nfill->nexty >= 0, "nexty should be >= 0");
  HQASSERT(nfill->nthreads > 0, "nthreads should be > 0");
  HQASSERT(nfill->y1clip >= 0, "y1clip should be >= 0");

  /*
   * Free the NFILL object and the consecutive NBRESS chains all in one go,
   * as that is how they were allocated. Have to search through the NBRESS
   * chains to find the first one in memory, as they may have been sorted
   * from their original allocation order.
   */
  end = (uint8 *)&(nfill->thread[nfill->nthreads]);
  start = end;
  for ( thd = 0; thd < nfill->nthreads; thd++ ) {
    if ( nfill->thread[thd] != NULL &&
         start > (uint8 *)(nfill->thread[thd]) )
      start = (uint8 *)(nfill->thread[thd]);
  }
  nfillsize = end - start;
  if ( nfill->rcbtrap )
    rcbt_freetrap(pools, nfill->rcbtrap);
  dl_free(pools, start, nfillsize, MM_ALLOC_CLASS_NFILL);
}

/**
 * Start a new segment within the NFILL at the given (x,y) location
 * \param[in,out]  nbuild   NFILL and state
 * \param[in]      sx       x ordinate
 * \param[in]      sy       y ordinate
 */
static void new_segment(nfill_builder_t *nbuild, dcoord sx, dcoord sy)
{
  HQASSERT(nbuild, "No NFILL builder");
  HQASSERT(!is_huge_point(sx, sy), "Huge co-ordinate passed to nfill");

  nbuild->curx = sx;
  nbuild->cury = sy;
  nbuild->nosegments = 0;
  nbuild->xdirection = nbuild->ydirection = 0;
  nbuild->newthread = TRUE;
  nbuild->newsubpath = TRUE;
  nbuild->had_degen  = FALSE;
  /* Starting a new segment, so queue up the 1st point and turn on queueing */
  nbuild->qdir = 0;
  nbuild->q[0].x = sx;
  nbuild->q[0].y = sy;
  nbuild->queued = 1;
  nbuild->qon = TRUE;
}

/**
 * Reverse the orientation of the given NBRESS thread.
 * If we only use word encoded threads, then they are fixed length and we
 * can simply reverse it. N.B. the first line segment is guaranteed to be
 * small enough to encode as a pair of bytes or words !
 * \param[in,out]  nbress  Thread being reveresed
 */
static void reverse_thread(NBRESS *nbress)
{
  dcoord x, y, dx1, dy1, dx2, dy2;
  uint8 i, n = dxylist_used(&(nbress->dxy));

  nbress->norient = (int8) -nbress->norient; /* flip the up/down bit */

  if ( n == 0 ) { /* Only one line in thread, just flip nx/y 1,2 */
    dcoord swp;

    /* Swap over the x coords. */
    swp = nbress->nx1;
    nbress->nx1 = nbress->nx2;
    nbress->nx2 = swp;
    /* Swap over the y coords. */
    swp = nbress->ny1;
    nbress->ny1 = nbress->ny2;
    nbress->ny2 = swp;
    return;
  }

  x = nbress->nx2;
  y = nbress->ny2;
  /* Swap over all middle dxdy's.
   * i.e. in pairs in the delta list excluding the last entry
   * which gets treated specially outside this loop
   */
  for ( i = 0; i < (n-2+1)/2; i++ ) {
    /* Load first dxdy. */
    dxylist_read(&(nbress->dxy), i, &dx1, &dy1);
    if ( dy1 == 0 )
      dx1 = -dx1;
    x -= dx1; y -= dy1;
    /* Load second dxdy. */
    dxylist_read(&(nbress->dxy), n-2-i, &dx2, &dy2);
    if ( dy2 == 0 )
      dx2 = -dx2;
    x -= dx2; y -= dy2;
    /* Store both dxdy's. */
    dxylist_write(&(nbress->dxy), n-2-i, dx1, dy1);
    dxylist_write(&(nbress->dxy), i, dx2, dy2);
  }

  /* And possibly the middle one.
   * Have been exchanging deltas in pairs, may be an odd one
   * in the middle that needs reversing
   */
  if ( n > 1 && ((n&1)==0) ) {
    /* Load middle dxdy. */
    dxylist_read(&(nbress->dxy), (n-2)/2, &dx1, &dy1);
    if ( dy1 == 0 )
      dx1 = -dx1;
    dxylist_write(&(nbress->dxy), (n-2)/2, dx1, dy1);
    x -= dx1;
    y -= dy1;
  }

  /* And the start/end dxdy.
   * Last point in the delta list gets treated specially,
   * swapped with (nx1,ny1)
   */
  dxylist_read(&(nbress->dxy), n-1, &dx1, &dy1);
  dxylist_write(&(nbress->dxy), n-1, nbress->nx1 - nbress->nx2,
                                     nbress->ny1 - nbress->ny2);

  nbress->nx2 = x;
  nbress->ny2 = y;
  if ( dy1 == 0 )
    dx1 = -dx1;
  x -= dx1;
  y -= dy1;
  nbress->nx1 = x;
  nbress->ny1 = y;
}

/**
 * NFILL structure has been completed, so tidy it up.
 * \param[in]  nbuild  NFILL structure plus state information
 */
static void finish_nbressthread(nfill_builder_t *nbuild)
{
  NBRESS *nbress;

  HQASSERT(nbuild, "No NFILL builder");
  nbress = nbuild->fnbress;
  HQASSERT(nbress, "Cannot finish NULL thread");

  /* set orientation; reverse thread if needed. */
  nbress->norient = (int8)nbuild->orientation;
  if ( nbuild->lasty < nbress->ny1)
    reverse_thread(nbress);

  /* Compress the delta storage if we can.
   * No point with text as it gets converted to a bitmap form
   * straight away anyway.
   */
  if ( !(nbuild->is_char) )
    dxylist_compact(&(nbress->dxy), nbuild->pool);
}

/**
 * Add an (x,y) co-ordinate to the current NFILL thread
 * \param[in,out]  nbuild   NFILL and state
 * \param[in]      nx       x ordinate
 * \param[in]      ny       y ordinate
 * \return                  Success status
 */
static Bool add_segment(nfill_builder_t *nbuild, dcoord nx, dcoord ny)
{
  dcoord dx, dy, sdx, sdy;
  NBRESS *nbress;
  Bool toobigforword;
  uint8 clippedout;

  HQASSERT(nbuild, "No NFILL builder");
  HQASSERT(!is_huge_point(nx, ny), "Huge co-ordinate passed to nfill");

  SwOftenUnsafe();

  dx = nx - nbuild->curx;
  dy = ny - nbuild->cury;

  /* Ignore degenerate points if we've got at least one point.
   * If we don't have any points, we have to store it any case there are no
   * more points coming (and we will lose the degenerate thread entirely).
   * But we record we have a degenerate start to a thread, and over-write it
   * later if more points do arrive.
   */
  if (( dx | dy ) == 0 ) {
    if ( nbuild->nosegments > 0 )
      return TRUE;
    nbuild->newthread = TRUE;
  }

  /*
   * Ignore lines that are completely clipped out. Test for clipped right
   * last, because we can optimise out fills which are clipped out on the left
   * if we know that they don't cover the clip area at all.
   */
  if ( ny < cclip_bbox.y1 && nbuild->cury < cclip_bbox.y1 )
    clippedout = CLIPPED_ABOVE;
  else if ( ny > cclip_bbox.y2 && nbuild->cury > cclip_bbox.y2 )
    clippedout = CLIPPED_BELOW;
  else if ( nx > cclip_bbox.x2 && nbuild->curx > cclip_bbox.x2 )
    clippedout = CLIPPED_RIGHT;
  else if ( nx < cclip_bbox.x1 && nbuild->curx < cclip_bbox.x1 )
    clippedout = CLIPPED_LEFT;
  else
    clippedout = CLIPPED_UNCLIPPED;

  nbuild->nfill.clippedout |= clippedout;
  /*
   * Don't actually remove clipped left segments, but mark that it would
   * have been clipped.
   */
  if ( !(clippedout == CLIPPED_LEFT || clippedout == CLIPPED_UNCLIPPED) ) {
    nbuild->curx = nx;
    nbuild->cury = ny;
    nbuild->newthread = TRUE;
    /* Throwing points away messes up the queuing system, so turn it off */
    nbuild->qon = FALSE;
    return TRUE;
  }

  /*
   * If we are on the first thread, and queuing is turned on, then save up
   * points until we get to the first point of inflection. i.e. up to the
   * point where the dy value changes sign.
   * These points will get replayed later.
   */
  if ( nbuild->qon && ( dy * nbuild->qdir ) >= 0 ) {
    if ( nbuild->qdir == 0 )
      nbuild->qdir = (dy < 0 ) ? -1 : 1;

    nbuild->q[nbuild->queued].x = nx;
    nbuild->q[nbuild->queued].y = ny;
    nbuild->queued++;
    if ( nbuild->queued == MAX_QUEUED-1 )
      nbuild->qon = FALSE; /* Turn off queue if we run out of space */
    nbuild->curx = nx;
    nbuild->cury = ny;
    return TRUE;
  }
  else /* Turn queue off as we are past 1st point of inflection */
    nbuild->qon = FALSE;

  sdx = dx;
  sdy = dy;
  nbress = nbuild->fnbress;

  /* The xyswapable test might be able to check X direction is consistent
     with Y direction; i.e. either (dx >= 0 && dy >= 0) || (dx <= 0 && dy <=
     0). In this case the swapxy_nfill() routine will need to be
     modified to detect orientation changes in the extension chain. X/Y
     swapability is used for two-pass rendering for characters, to perform
     dropout control. */
  toobigforword = dxylist_toobig(dx, dy);
  if ( toobigforword || (nbuild->ydirection < 0 && dy > 0) ||
       (nbuild->ydirection > 0 && dy < 0) || nbuild->newthread ||
       nbuild->xyswapable || dxylist_full(&(nbress->dxy)) ) {
    NBRESS *tbress;

    /* Remember if its a degenerate line, as we may be able to kill it later */
    nbuild->had_degen = (( dx | dy ) == 0 );

    if ( nbuild->promise == NULL ) { /* first thread */
      /*
       * Allocate the typical size promise we have requested. If this fails
       * halve it until it succeeds or we drop below a minimum threshold.
       */
      while ( mm_dl_promise(nbuild->pool, nbuild->size) != MM_SUCCESS ) {
        if ( nbuild->size < 4*sizeof(NFILLOBJECT) ) /* arbitrary lower limit */
          return FALSE;
        nbuild->size /= 2;
        error_clear();
      }
    }
    else /* close off old thread. */
      finish_nbressthread(nbuild);

    /* New thread needed. : Note thread has a variable length list at the end,
     * which you cannot declare in C. So its actually declared one int32 long
     * and we have to take that off the initial allocation.
     */
    tbress = (NBRESS *)mm_dl_promise_next(nbuild->pool, sizeof(NBRESS) -
                                          sizeof(int32));
    if ( nbuild->promise == NULL ) /* Remember start of promise */
      nbuild->promise = (uint8 *)tbress;
    if ( tbress == NULL )
      return FALSE;

    tbress->u1.next = nbress;
    nbress = tbress;
    nbuild->fnbress = nbress;
    nbuild->nfill.nthreads += 1;

    /* At start of a new thread, store loads of stuff. */
    /* Basically all of the BRESS stuff - but do it later. */
    nbress->nx1 = nbuild->curx;
    nbress->ny1 = nbuild->cury;
    nbress->nx2 = nx;
    nbress->ny2 = ny;
    dxylist_init(&(nbress->dxy));
    /** \todo ajcd 2009-02-14: While constructing the threads, the norient
        field is abused as a note that this chain was a new subpath. This
        marker may not be used anymore. */
    nbress->norient = (int8)nbuild->newsubpath;
    nbuild->newsubpath = FALSE;

    /* Need to start a new thread next time if the initial delta is too big
       for word encoding and the thread might need to be reversed. */
    nbuild->xdirection = nbuild->ydirection = 0;
    nbuild->newthread = toobigforword && ( dy <= 0 );
  }
  else {
    if ( nbuild->had_degen ) {
      /* Over-write previous degenerate line that was stored at the start
       * of this thread
       */
      HQASSERT(dxylist_empty(&(nbress->dxy)), "Degen point and list not empty");
      nbuild->had_degen = FALSE;
      nbress->nx2 = nx;
      nbress->ny2 = ny;
    } else {
      if ( !mm_dl_promise_next(nbuild->pool, sizeof(int32)) )
        return FALSE;

      /* Store (dx,dy) - may need to be reversed later. */
      if ( dy < 0 ) {
        dx = -dx;
        dy = -dy;
      }
      dxylist_store(&(nbress->dxy), dx, dy);
    }
  }

  HQASSERT(nbress->ny1 == nbress->ny2 ||
           ( sdy >= 0 && nbress->ny2 > nbress->ny1) ||
           ( sdy <= 0 && nbress->ny2 < nbress->ny1) ,
           "sdy direction got out of sync(1)");

  HQASSERT(sdy == 0 ||
           ( sdy > 0 && nbress->ny1 <= nbress->ny2) ||
           ( sdy < 0 && nbress->ny1 >= nbress->ny2) ,
           "sdy direction got out of sync(2)");

  HQASSERT(nbuild->ydirection == 0 ||
           ( nbuild->ydirection > 0 && sdy >= 0 ) ||
           ( nbuild->ydirection < 0 && sdy <= 0 ) ,
           "Y direction got out of sync(1)");

  HQASSERT(sdy == 0 ||
           ( nbuild->ydirection >= 0 && sdy > 0 ) ||
           ( nbuild->ydirection <= 0 && sdy < 0 ) ,
           "Y direction got out of sync(2)");

  if ( sdy != 0 )
    nbuild->ydirection = sdy;
  if ( sdx != 0 )
    nbuild->xdirection = sdx;

  HQASSERT(nbuild->ydirection == 0 ||
           ( nbuild->ydirection > 0 && ny > nbress->ny1) ||
           ( nbuild->ydirection < 0 && ny < nbress->ny1),
           "coords got bad");

  /* Store last y-coord, so that can reverse thread if needed. */
  nbuild->lasty = ny;

  nbuild->nosegments += 1;
  nbuild->curx = nx;
  nbuild->cury = ny;

  return TRUE;
}

/**
 * Routine called by Bezier flattening code for each point in the
 * flatened path.
 * \param[in] pt     Current (x,y) point
 * \param[in] data   Opaque handle passed through
 * \param[in] flags  Indicating what part of bezier we are on
 * \return           -1 on error
 */
static int32 bressbez_cb(FPOINT *pt, void *data, int32 flags)
{
  int32 cx, cy;
  nfill_builder_t *nbuild = data;

  UNUSED_PARAM(int32, flags);

  /* Add line segment since curve is good enougth. */
  SC_C2D_UNT_I(cx, pt->x, 0.0);
  SC_C2D_UNT_I(cy, pt->y, 0.0);

  return add_segment(nbuild, cx, cy) ? 1 : -1;
}

/**
 * End of sub-path, so flush anything out that has been saved up.
 * If we queued any co-ords at the beginning of the thread,
 * play them through now so that they can get put at the end
 * of the final thread in the path.
 * \param[in]  nbuild  NFILL structure plus state information
 * \return             Success status
 */
static Bool final_segment(nfill_builder_t *nbuild)
{
  if ( nbuild->queued > 0 ) { /* Have we queued any points up ? */
    int32 i;

    /* We should always end on the same point we started */
    HQASSERT(nbuild->q[0].x == nbuild->curx && nbuild->q[0].y == nbuild->cury &&
             nbuild->queued < MAX_QUEUED, "problem with nfill queued points");
    nbuild->qon = FALSE; /* Stop then getting re-queued ! */
    for ( i = 1 ; i < nbuild->queued; i++ ) {
      if ( !add_segment(nbuild, nbuild->q[i].x, nbuild->q[i].y) )
        return FALSE;
    }
    nbuild->queued = 0;
  }
  nbuild->newsubpath = TRUE;
  return TRUE;
}

/**
 * Flatten supplied path, converting it into a NBRESS chains.
 * \param[in] nbuild  NFILL plus state information
 * \param[in] path    Path to be converted
 * \return            Success status
 */
Bool addpath2_nfill(nfill_builder_t *nbuild, PATHLIST *path)
{
  HQASSERT(nbuild, "No NFILL builder");

  while ( path ) {
    LINELIST *line = path->subpath, *prev = NULL;
    dcoord cx, cy;
    SYSTEMVALUE sc_rnd = SC_PIXEL_ROUND;
    FPOINT ctrl_pts[4];
    int32 bezi, ltype;
    Bool closed = TRUE;

    HQASSERT(line->next != NULL, "Path must have at least a moveto and close");
    if ( line->next->type != MYCLOSE ) { /* Check here for degenerate. */
      while ( line ) {
        SwOftenUnsafe();

        ltype = line->type;
        switch ( ltype ) {
          case CURVETO:
            HQASSERT(prev != NULL, "Path cannot start with a curveto");
            ctrl_pts[0].x = prev->point.x + sc_rnd;
            ctrl_pts[0].y = prev->point.y + sc_rnd;
            for ( bezi = 1; bezi <= 3; bezi++ ) {
              HQASSERT(line->type == CURVETO,
                "Point in bezier should be a CURVETO");
              ctrl_pts[bezi].x = line->point.x + sc_rnd;
              ctrl_pts[bezi].y = line->point.y + sc_rnd;
              if ( bezi != 3 )
                line = line->next;
            }
            if ( !bezchop(ctrl_pts, bressbez_cb, nbuild, BEZ_POINTS|BEZ_CTRLS) )
              return FALSE;
            break;

          case MOVETO:
          case MYMOVETO:
            HQASSERT(closed, "Moveto with closing previous path");
            closed = FALSE;
            SC_C2D_UNT_I(cx, line->point.x, sc_rnd);
            SC_C2D_UNT_I(cy, line->point.y, sc_rnd);
            new_segment(nbuild, cx, cy);
            break;

          case LINETO:
          case MYCLOSE:
          case CLOSEPATH:
            SC_C2D_UNT_I(cx, line->point.x, sc_rnd);
            SC_C2D_UNT_I(cy, line->point.y, sc_rnd);
            if ( !add_segment(nbuild, cx, cy) )
              return FALSE;
            if ( ltype != LINETO ) {
              closed = TRUE;
              if ( !final_segment(nbuild) )
                return FALSE;
            }
            break;

          default:
            HQFAIL("Unknown segments type in path");
            break;
        }
        prev = line;
        line = line->next;
      }
    }
    path = path->next;
  }
  return TRUE;
}

/**
 * Make an nfill object from the path supplied.
 *
 * Flatten the path and convert it into a number of NBRESS chains,
 * storing the result in a NFILLOBJECT structure, which we return in the
 * variable '*nfillptr'.
 * Needs a unknown amount of memory, so do work in a retry loop,
 * stepping-up memory each time around.
 * \param[in]  path      Path to be converted
 * \param[in]  flags     Origin and nature of path being processed
 * \param[out] nfillptr  Pointer to resulting NFILL object
 * \return               Success status
 */
Bool make_nfill(DL_STATE *page, PATHLIST *path, uint32 flags,
                NFILLOBJECT **nfillptr)
{
  Bool result = FALSE;
  Bool wasHuge = FALSE;
  size_t promisesize;
  PATHINFO reducedpath;

  HQASSERT(path, "path is NULL");
  HQASSERT(nfillptr, "nfillptr is NULL");
  HQASSERT(!error_signalled(),
           "Should not be making an nfill in an error condition");
  *nfillptr = NULL;
  /*
   * The NFILL_IS*** bits are mutually exclusive,
   * but ISCHAR may also have XYSWAP set.
   */
  HQASSERT( ((flags & NFILL_XYSWAP) && (flags == (NFILL_ISCHAR|NFILL_XYSWAP)))
            || BIT_EXACTLY_ONE_SET(flags), "Invalid nfill flags" );

  reducedpath.firstpath = NULL;

  if ( is_huge_path(path) )
  {
    wasHuge = TRUE;
    /*
     * If the co-ordinates in the input path are so big that they will
     * kill the Bressenham fill algorithm, pre-process the path clipping
     * the co-ordinates down to more manageable values.
     */
    if ( !clip_huge_path(path, &reducedpath, NULL) )
      return FALSE;

    path = reducedpath.firstpath;

    if ( path == NULL )   /* clipped it, and nothing left */
      return TRUE;
  }

#ifdef PROBE_BUILD
  if ( !CURRENT_DEVICE_SUPPRESSES_MARKS() && !char_doing_cached() )
    probe_begin(SW_TRACE_DL_FILL, 0) ;
#endif

  /*
   * If we run out of promise memory, either in addpath2_nfill() or
   * complete_nfill(), then ask for a larger promise and retry
   * by going round the outer loop again. Otherwise drop through and
   * return success or vmerror if we ran out of retries (or if we
   * reduced memory to get the initial promise and still failed).
   */
  for ( promisesize = 25 * 1024; !result; promisesize *= 2 ) {
    nfill_builder_t nbuild;

    start_nfill(page, &nbuild, promisesize, flags);
    result = addpath2_nfill(&nbuild, path) &&
             complete_nfill(&nbuild, nfillptr);
    end_nfill(&nbuild);

    /* No point retrying if we failed to allocate the requested promisesize, or
       if the next promisesize will wrap around a size_t. */
    if ( nbuild.size != promisesize || (promisesize * 2) < promisesize )
      break;
  }

#ifdef PROBE_BUILD
  if ( !CURRENT_DEVICE_SUPPRESSES_MARKS() && !char_doing_cached() )
    probe_end(SW_TRACE_DL_FILL, (intptr_t)*nfillptr) ;
#endif

  if (wasHuge)
    path_free_list(reducedpath.firstpath, mm_pool_temp);

  return result ? TRUE : error_handler(VMERROR);
}

/**
 * test to see if the clip flags indicate the NFILL is totally clipped
 * \param[in]   clippedout   Clip flags
 * \return                   Is the Nfill totally clipped ?
 */
static Bool totally_clipped(uint8 clippedout)
{
  int32 clipx = (clippedout & (CLIPPED_UNCLIPPED|CLIPPED_LEFT|CLIPPED_RIGHT));
  int32 clipy = (clippedout & (CLIPPED_UNCLIPPED|CLIPPED_ABOVE|CLIPPED_BELOW));

  return ( clipx == CLIPPED_LEFT || clipx == CLIPPED_RIGHT ||
           clipy == CLIPPED_ABOVE || clipy == CLIPPED_BELOW );
}

/**
 * Finish the NFILL object we have been creating and return it to the caller
 * If the nfill is degenerate, then delete it and return a NULL object, but
 * still a successful status.
 * \param[in]   nbuild    NFILL record
 * \param[out]  nfillptr  Pointer to return NFILL object
 * \return                Success status
 */
Bool complete_nfill(nfill_builder_t *nbuild, NFILLOBJECT **nfillptr)
{
  NBRESS *nbress;
  NFILLOBJECT *nfill;
  int32 thd;

  HQASSERT(nbuild, "No NFILL builder");
  HQASSERT(nfillptr, "Nowhere to put nfill");
  *nfillptr = NULL;

  HQASSERT(nbuild->queued == 0, "Still have nfill co-ords queued-up");

  /* Nothing to do if no threads or nfill totally clipped. */
  if (nbuild->nfill.nthreads == 0 || totally_clipped(nbuild->nfill.clippedout)) {
    /* Destroy partially-constructed NFILLOBJECT, removing responsibility
       from nfill_builder_t. */
    if ( nbuild->promise )
      mm_dl_promise_free(nbuild->pool);
    nbuild->promise = NULL;
    return TRUE;
  }

  /* Finish off the last thread. */
  finish_nbressthread(nbuild);

  /* Next, allocate enough room for the NFILL object plus the thread array . */
  nfill = (NFILLOBJECT *)mm_dl_promise_next(nbuild->pool, sizeof(NFILLOBJECT) +
                   sizeof(NBRESS *) * (nbuild->nfill.nthreads-ST_NTHREADS));
  if ( nfill == NULL )
    return FALSE;

  /* Finished with allocating promise memory for this fill. */
  mm_dl_promise_end(nbuild->pool);

  /* Fill in top level FILLOBJECT's members. */
  nfill->type       = 0;
  nfill->rcbtrap    = NULL;
  nfill->clippedout = nbuild->nfill.clippedout;
  nfill->nthreads   = nbuild->nfill.nthreads;
  nfill->startnptr  = NULL;
  nfill->endnptr    = NULL;
  nfill->nexty      = MAXDCOORD; /* Forces initialisation first time round */
  nfill->y1clip     = cclip_bbox.y1;

  /* If requested, initialise the scan conversion rule from the
   * ScanConversion pagedev key. The pixel touching (Adobe rule) scan
   * converter is selected by default otherwise.
   */
  if ( nbuild->is_char )
    nfill->converter = UserParams.CharScanConversion;
  else if ( nbuild->is_stroke )
    nfill->converter = UserParams.StrokeScanConversion;
  else
    nfill->converter = gstateptr->thePDEVinfo.scanconversion;

  /* Followed by fix up of the threads. */
  for ( thd = 0, nbress = nbuild->fnbress; nbress; nbress = nbress->u1.next )
    nfill->thread[thd++] = nbress;

  /* Transfer responsibility for freeing NFILLOBJECT to caller. */
  nbuild->promise = NULL;
  *nfillptr = nfill;
  track_dl(sizeof_nfill(nfill), MM_ALLOC_CLASS_NFILL, TRUE);

  return TRUE;
}

/** Copy an NFILL into this page's memory, */
NFILLOBJECT *nfill_copy(DL_STATE *page, const NFILLOBJECT *nfill)
{
  NFILLOBJECT *newfill ;
  int32 i ;
  size_t chainsize, headersize ;
  mm_pool_t pool ;
  uint8 *base ;

  HQASSERT(page != NULL, "No DL page") ;
  HQASSERT(nfill != NULL, "No NFILL to copy") ;

  /* We need to walk over the chains individually to total their size, because
     the nfill that we're copying may be in stack memory, and therefore may
     not conform to the expected layout. */
  headersize = sizeof(NFILLOBJECT) + sizeof(NBRESS *) * (nfill->nthreads-ST_NTHREADS) ;
  chainsize = 0 ;
  for ( i = 0 ; i < nfill->nthreads ; ++i )
    chainsize += sizeof_nbress(nfill->thread[i]) ;

  pool = dl_choosepool(page->dlpools, MM_ALLOC_CLASS_NFILL) ;
  if ( (base = mm_alloc(pool, headersize + chainsize, MM_ALLOC_CLASS_NFILL)) == NULL )
    return NULL ;

  newfill = (NFILLOBJECT *)(base + chainsize) ;
  HqMemCpy(newfill, nfill, headersize) ;

  /* Copy chains first, then NFILLOBJECT */
  for ( i = 0 ; i < nfill->nthreads ; ++i ) {
    NBRESS *thread = nfill->thread[i] ;
    size_t size = sizeof(NBRESS) - sizeof(int32) +
      (thread->dxy.ndeltas * thread->dxy.deltabits >> 2) ;
    HqMemCpy(base, thread, size) ;
    newfill->thread[i] = (NBRESS *)base ;
    size = SIZE_ALIGN_UP_P2(size, sizeof(NBRESS *)) ;
    base += size ;
  }

  HQASSERT(base == (uint8 *)newfill, "Chains didn't match base") ;

  preset_nfill(newfill) ;

  return newfill ;
}

/** Append threads from an NFILL onto a pre-allocated NFILLOBJECT, */
NFILLOBJECT *nfill_preallocate(DL_STATE *page, size_t nbresssize,
                               int32 nthreads, int32 type, uint8 converter)
{
  NFILLOBJECT *newfill ;
  int32 i ;
  size_t headersize ;
  mm_pool_t pool ;
  uint8 *base ;

  HQASSERT(page != NULL, "No DL page") ;

  /* We need to walk over the chains individually to total their size, because
     the nfill that we're copying may be in stack memory, and therefore may
     not conform to the expected layout. */
  headersize = sizeof(NFILLOBJECT) + sizeof(NBRESS *) * (nthreads-ST_NTHREADS) ;
  pool = dl_choosepool(page->dlpools, MM_ALLOC_CLASS_NFILL) ;
  if ( (base = mm_alloc(pool, headersize + nbresssize, MM_ALLOC_CLASS_NFILL)) == NULL )
    return NULL ;

  newfill = (NFILLOBJECT *)(base + nbresssize) ;
  HqMemZero(newfill, headersize) ;
  newfill->type = type ;
  newfill->nexty = MAXDCOORD ;
  newfill->y1clip = 0 ;
  newfill->rcbtrap = NULL ;
  newfill->converter = converter ;
  newfill->clippedout = CLIPPED_UNCLIPPED ;
  newfill->nthreads = 0 ;
  newfill->startnptr = newfill->endnptr = NULL ;

  newfill->thread[0] = (NBRESS *)base ;
  for ( i = 1 ; i < nthreads ; ++i )
    newfill->thread[i] = NULL ;

  return newfill ;
}

/** Append threads from an NFILL onto a pre-allocated NFILLOBJECT, */
void nfill_append(DL_STATE *page, NFILLOBJECT *out, const NFILLOBJECT *in)
{
  int32 i ;
  uint8 *base ;

  UNUSED_PARAM(DL_STATE *, page) ;

  HQASSERT(page != NULL, "No DL page") ;
  HQASSERT(in != NULL, "No NFILL to copy") ;
  HQASSERT(out != NULL, "No NFILL to copy") ;

  base = (uint8 *)out->thread[out->nthreads] ;
  for ( i = 0 ; i < in->nthreads ; ++i ) {
    size_t chainsize = sizeof_nbress(in->thread[i]) ;
    uint8 *next = base + SIZE_ALIGN_UP_P2(chainsize, sizeof(NBRESS *)) ;
    HQASSERT(next <= (uint8 *)out, "nbress chains overflowed") ;
    HqMemCpy(base, in->thread[i], chainsize) ;
    out->thread[out->nthreads++] = (NBRESS *)base ;
    base = next ;
  }

  if ( base == (uint8 *)out ) {
    preset_nfill(out) ;
  } else {
    out->thread[out->nthreads] = (NBRESS *)base ;
  }
}

/**
 * Calculate nfill bounding box, and return indicator of whether it is
 * completely clipped out.
 * If the bbox pointer is set on entry, the adjusted bbox is returned in it.
 * \param[in]  nfill   NFILL record
 * \param[in]  clip    Clip bounding box or NULL for no clipping
 * \param[out] bbox    Returned bbox of nfill
 * \param[out] clipped Returns if nfill is completely clipped, may be NULL
 * \return             Is Nfill completely clipped out ?
 */
void bbox_nfill(NFILLOBJECT *nfill, const dbbox_t *clip, dbbox_t *bbox,
                Bool *clipped)
{
  dcoord x1, y1, x2, y2;
  int32 i;

  HQASSERT(bbox != NULL, "bbox null");
  HQASSERT(nfill != NULL, "nfill null");
  HQASSERT(nfill->nthreads != 0, "invalid degenerate nfill");

  /* Find bounding box of everything.  */
  x1 = x2 = nfill->thread[0]->nx1;
  y1 = y2 = nfill->thread[0]->ny1;
  if ( clipped )
    *clipped = FALSE;

  for ( i = 0; i < nfill->nthreads; ++i ) {
    NBRESS *nbress = nfill->thread[i];
    uint8 j, n = dxylist_used(&(nbress->dxy));
    dcoord x, y;

    if ( y1 > nbress->ny1 )
      y1 = nbress->ny1;
    else if ( y2 < nbress->ny1 )
      y2 = nbress->ny1;

    if ( x1 > nbress->nx1 )
      x1 = nbress->nx1;
    else if ( x2 < nbress->nx1 )
      x2 = nbress->nx1;

    if ( x1 > nbress->nx2 )
      x1 = nbress->nx2;
    else if ( x2 < nbress->nx2 )
      x2 = nbress->nx2;

    /* This is potentially inefficient ! */
    x = nbress->nx2; y = nbress->ny2;
    for ( j = 0; j < n; j++ ) {
      dcoord dx, dy;

      dxylist_read(&(nbress->dxy), j, &dx, &dy);
      x += dx; y += dy;
      if ( x > x2 )
        x2 = x;
      else if ( x < x1 )
        x1 = x;
    }
    if ( y < y1 )
      y1 = y;
    else if ( y > y2 )
      y2 = y;
  }

  HQASSERT(x1 <= x2, "funny x starting bbox");
  HQASSERT(y1 <= y2, "funny y starting bbox");

  if ( clip ) {
    HQASSERT(y2 >= clip->y1, "badly clipped y2");
    HQASSERT(y1 <= clip->y2, "badly clipped y1");
    HQASSERT(x1 <= clip->x2, "badly clipped x1");

    /* Test if whole fill was to left of clip area. */
    if ( x2 < clip->x1 && !(nfill->clippedout & CLIPPED_RIGHT) ) {
      if ( clipped )
        *clipped = TRUE;
    }
    else {
      /* Check bbox clipping. */
      if ( x1 < clip->x1 )
        x1 = clip->x1;
      if ( y1 < clip->y1 )
        y1 = clip->y1;
      if ( x2 > clip->x2 || (nfill->clippedout & CLIPPED_RIGHT) )
        x2 = clip->x2;
      if ( y2 > clip->y2 )
        y2 = clip->y2;
      HQASSERT(x1 <= x2, "funny x clipped bbox");
      HQASSERT(y1 <= y2, "funny y clipped bbox");

    }
  }
  /* Return bounding box of fill */
  bbox_store(bbox, x1, y1, x2, y2);
}

enum {
  QUAD_DELTA_BITS = 6,         /**< Size of combined delta and direction. */
  QUAD_XY1_PLUS = 0,                           /**< Delta added to bottom of bbox */
  QUAD_XY2_MINUS = 1 << (QUAD_DELTA_BITS - 1), /**< Delta subtracted from top of bbox */
  QUAD_DELTA_MASK = QUAD_XY2_MINUS - 1,        /**< Bits reserved for delta */
  /** Shift for higher y delta+flag */
  QUAD_YH_SHIFT = 0,
  /** Shift for lower y delta+flag */
  QUAD_YL_SHIFT = QUAD_YH_SHIFT + QUAD_DELTA_BITS,
  /** Shift for higher x delta+flag */
  QUAD_XH_SHIFT = QUAD_YL_SHIFT + QUAD_DELTA_BITS,
  /** Shift for lower x delta+flag */
  QUAD_XL_SHIFT = QUAD_XH_SHIFT + QUAD_DELTA_BITS,
  QUAD_X2_SHIFT = QUAD_XL_SHIFT + QUAD_DELTA_BITS, /**< Shift for bbox x2 index */
  QUAD_X1_SHIFT = QUAD_X2_SHIFT + 2, /**< Shift for bbox x1 index */
  QUAD_Y2_SHIFT = QUAD_X1_SHIFT + 2, /**< Shift for bbox y2 index */
  QUAD_SC_SHIFT = QUAD_Y2_SHIFT + 2, /**< Shift for scan conversion rule */
  QUAD_SC_MASK = -1 << QUAD_SC_SHIFT /**< Mask for extracting scan conversion */
} ;

/** \brief
 * Test to see if the given NFILL can be represented by a simpler
 * construct on the Display List.
 *
 * Rather than storing a pointer to an NFILL structure on the display list,
 * if the path is very simple we may be able to cram the info into the
 * 4 bytes occupied by the pointer. So test to see if we have a small enough
 * vector object to reduce to this simple case. Note the DL object includes
 * the path bounding box, so the 4 extra bytes is in addition to that base
 * information.
 *
 * Note that this function takes advantage of being called immediately after
 * make_nfill(), so the path segments have been extracted into NBRESS threads,
 * but not yet re-sorted. We can therefore trace a fully-connected contour
 * through the successive threads.
 *
 * The simplifed DL object is called a 'quad', but in fact may be degenerate
 * and actually have 2, 3 or 4 edges. There are two DL objects types to deal
 * with the two possible scan conversion rules.
 *
 * The current implementation supports a range of lines, triangles and
 * quadrilaterals as simple fills. It does this by representing lines and
 * triangles as a special case of a quad with the final co-ordinate repeated.
 * Then with the quad, it is noted that at least four of the 8 ordinates must
 * lie on the bbox edge. So four two-bit indices are sufficient to describe
 * which edge they lie on. Then the other four ordinates are stored as
 * five-bit offsets plus a direction flag from the top, left, bottom or right
 * sides of the bbox. This means this simple primitive is capable of
 * representing lines, triangles and quads (including self-intersecting ones)
 * for which all corners lie within 31 pixels of any side of the bbox.
 *
 * The simple fill can be though of as representing a quad by a set of four
 * (x,y) co-ords specified with origin at the top left of the bbox. These
 * four co-ords are then squashed into 32 bits as follows
 *
 * MSB                                         LSB
 *     2    2     2     2     1     6    1     6    1     6    1     6
 *  | sc | iy2 | ix1 | ix2 | xld | xl | xhd | xh | yld | yl | yhd | yh |
 *
 * sc is the scan conversion rule
 * The fill is normalised so that bbox.y1 is in point ordinal 0.
 * iy2 is the index of  a y ord which is bbox.y2
 * ix1 is the index of an x ord which is bbox.x1
 * ix2 is the index of an x ord which is bbox.x2
 * yhd/yh refer to the higher ordinal Y coordinate that's not bbox.y1 or bbox.y2
 * yld/yl refer to the lower ordinal Y coordinate that's not bbox.y1 or bbox.y2
 * xhd/xh refer to the higher ordinal X coordinate that's not bbox.x1 or bbox.x2
 * xld/xl refer to the lower ordinal X coordinate that's not bbox.x1 or bbox.x2
 * The flags yhd, yld, xhd, xld are zero if the delta is added to the lower
 * (top/left) bbox coordinate or one if the delta is subtracted from the higher
 * (bottom/right) bbox coordinate.
 *
 * The bbox ordinals are set up in a specific way, which means that x1 and y1
 * will be the smallest ordinal where the point is on the bbox, x2 and y2
 * will be the largest ordinal where the point is on the bbox (including the
 * repeated point(s) for lines and triangles). This property is used by the
 * quad_is_line(), quad_is_point(), quad_is_triangle(), and quad_is_rect()
 * predicates.
 *
 * \param[in]  nfill     NFILL record
 * \param[out] bbox      Returned bbox of quad (only if return value is TRUE)
 * \param[out] quad_ptr  Returned data of simplified path info
 * \retval     TRUE      If the nfill can be represented by a quad
 * \retval     FALSE     If the nfill cannot be represented by a quad
 */
static Bool nfill_is_quad(NFILLOBJECT *nfill, dbbox_t *bbox, uint32 *quad_ptr)
{
  uint32 quad;
  IPOINT points[5] ; /* One spare point for final connection point. */
  IPOINT coords[4] ; /* Re-ordered points in quad */
  int32 i, j, npoints, iy2, ix1, ix2 ; /* point indices */
  dcoord xcurr, ycurr ;
  unsigned int mask ; /* Mask of used filled coordindates */
  unsigned int shift ; /* Shift for delta */
  int8 norient ; /* Initial direction */

  /* Don't do it if recombine is on. */
  HQASSERT(sizeof(quad) >= 4, "Need at least 4 bytes for QUAD information");
  if ( rcbn_enabled() )
    return FALSE;

  /* If the scan converter is not one that quads support, bail out. The
     number 4 is related to the available storage space, not the number of
     scan converters possible, so don't change it. */
  if ( nfill->converter >= 4 )
    return FALSE ;

  /* If there were threads clipped out above, below, or right, then bail out,
     because we can't connect the contour. Left and unclipped threads are
     retained in creating the nfill. */
  if ( (nfill->clippedout & (CLIPPED_RIGHT|CLIPPED_ABOVE|CLIPPED_BELOW)) != 0 )
    return FALSE ;

  HQASSERT(nfill->nthreads != 0, "Degenerate NFILL ?");

  /* Initialise bounding box and current point to the first point. */
  xcurr = bbox->x1 = bbox->x2 = points[0].x = nfill->thread[0]->nx1 ;
  ycurr = bbox->y1 = bbox->y2 = points[0].y = nfill->thread[0]->ny1 ;
  npoints = 1 ;
  norient = nfill->thread[0]->norient ;

  /* Simple shapes must be fully connected shapes with 4 points or less, and
     every point within 31 units of the edge of the bounding box. Unpack the
     fill into a point list, building a bbox as we go, and checking all of
     the other constraints. */
  for ( i = 0 ; i < nfill->nthreads ; ++i ) {
    NBRESS *thread = nfill->thread[i] ;
    int32 nused = dxylist_used(&thread->dxy) ;

    if ( thread->nx1 == thread->nx2 && thread->ny1 == thread->ny2 ) {
      /* Degenerate segment. This should not be followed by a delta chain,
         because degenerate segments force new segment starts. */
      HQASSERT(nused == 0,
               "Degenerate point followed by non-empty delta list") ;
      /* It doesn't matter which of x1,y1 or x2,y2 we compare with
         xcurr,ycurr */
      if ( thread->nx1 != xcurr || thread->ny1 != ycurr )
        return FALSE ;
    } else {
      int32 lo = npoints - 1, hi = npoints + nused ;
      int32 index, increment ;
      dcoord xchain, ychain ;

      /* Determine if we can fit all of the points in the array. We've got 5
         slots in the array; having built the connected set of points, we'll
         check that the last point is the same as the first point. We already
         have the first (or last point), so the number of points we're going to
         add is one for (nx2,ny2), and one for each delta list point. We're
         adding nused+1 points to the total, which must remain below 5. */
      npoints = hi + 1 ;
      if ( npoints > 5 )
        return FALSE ;

      if ( thread->norient == norient ) {
        index = lo ;
        increment = 1 ;
      } else {
        HQASSERT(thread->norient == -norient,
                 "Thread orientation should be +1 or -1") ;
        index = hi ;
        increment = -1 ;
      }

      xchain = points[index].x = thread->nx1 ;
      ychain = points[index].y = thread->ny1 ;
      bbox_union_point(bbox, xchain, ychain) ;
      index += increment ;

      xchain = points[index].x = thread->nx2 ;
      ychain = points[index].y = thread->ny2 ;
      bbox_union_point(bbox, xchain, ychain) ;
      index += increment ;

      /* Add the delta chain. The delta chain is asserted not to store (0,0)
         in it, so we don't need to test if the segments are degenerate. */
      dxylist_reset(&thread->dxy) ;
      while ( dxylist_get(&thread->dxy, &xchain, &ychain) ) {
        HQASSERT(index >= 0 && index < 5, "Point index out of range") ;
        HQASSERT(points[index - increment].x != xchain ||
                 points[index - increment].y != ychain,
                 "Degenerate line segment from delta list") ;
        points[index].x = xchain ;
        points[index].y = ychain ;
        bbox_union_point(bbox, xchain, ychain) ;
        index += increment ;
      }

      HQASSERT((increment == 1 && index == hi + 1) ||
               (increment == -1 && index == lo - 1),
               "Quad point unpacking did not fit correctly") ;

      /* We've overwritten the start point, but we still remember what its
         value was. Check that this segment connected. */
      if ( points[lo].x != xcurr || points[lo].y != ycurr )
        return FALSE ;

      /* The final point we wrote is the new current point. */
      xcurr = points[hi].x ;
      ycurr = points[hi].y ;
    }
  }

  /* We may have just had one point, and filtered out all of the others as
     degenerate. If not, then remove the final connecting point. */
  if ( npoints > 1 ) {
    --npoints ;

    /* We've got five or fewer points, but does the last point connect to the
       first? */
    if ( points[0].x != points[npoints].x || points[0].y != points[npoints].y )
      return FALSE ;
  }

  /* We now have the bounding box and the connected points. Find the topmost,
     leftmost point. We also check that all points are within range of the
     bounding box edges. */
  for ( j = i = 0 ; i < npoints ; ++i ) {
    HQASSERT(points[i].x >= bbox->x1 && points[i].x <= bbox->x2,
             "Quad X point not within bounding box") ;
    if ( points[i].x > bbox->x1 + QUAD_DELTA_MASK &&
         points[i].x < bbox->x2 - QUAD_DELTA_MASK )
      return FALSE ;

    HQASSERT(points[i].y >= bbox->y1 && points[i].y <= bbox->y2,
             "Quad Y point not within bounding box") ;
    if ( points[i].y > bbox->y1 + QUAD_DELTA_MASK &&
         points[i].y < bbox->y2 - QUAD_DELTA_MASK )
      return FALSE ;

    if ( points[i].y == bbox->y1 &&
         (points[j].y != bbox->y1 || points[i].x < points[j].x) )
      j = i ;
  }

  /** \todo ajcd 2009-02-18: It's just possible that we may want to limit the
      size of the nfill that we allow as a quad to prevent having to preset
      and repair the nfill in every band. In this case, we'd want to limit it
      to something smaller than the band height. When using multi-threaded
      rendering, though, it's probably better to allow the presets to happen
      because we don't need to lock the NFILL that we're unpacking the quad
      into. */

  /* points[j] is the topmost, leftmost if more than one topmost. */
  HQASSERT(points[j].y == bbox->y1, "Did not find topmost point of quad") ;

  /* Re-order the points into the coords array. */
  for ( i = 0 ; i < npoints ; ++i ) {
    coords[i] = points[j] ;
    j = (j + 1) % npoints ;
  }

  /* If there were less than four points, pad by repeating the last point. */
  for ( ; i < 4 ; ++i )
    coords[i] = coords[i - 1] ;

  HQASSERT(i == 4, "Didn't iterate over coords correctly") ;

  /* Short loops to find the points that have X or Y coordinates lying on the
     boundaries. These are not merged with the copy loop because the exit
     conditions turn into overly complex tests. The order which the points
     are searched is used in the quad_is_*() predicates, so don't alter
     it. */
  ix1 = ix2 = iy2 = 4 ; /* invalid index */
  for ( i = 0 ; i < 4 ; ++i ) {
    if ( coords[i].x == bbox->x1 ) {
      ix1 = i ;
      break ;
    }
  }

  /* Search for x2 from the other end, so if we have a degenerate line we
     don't get the same point as x1. */
  for ( i = 4 ; i > 0 ; ) {
    if ( coords[--i].x == bbox->x2 ) {
      ix2 = i ;
      break ;
    }
  }

  /* y1 is point 0, so search backwards for y2. */
  for ( i = 4 ; i > 1 ; ) {
    if ( coords[--i].y == bbox->y2 ) {
      iy2 = i ;
      break ;
    }
  }

  HQASSERT(ix1 < 4 && ix1 < 4 && iy2 < 4, "BBox indices not all found") ;
  HQASSERT(iy2 != 0 && ix1 != ix2, "BBox indices collided") ;

  quad = ((nfill->converter << QUAD_SC_SHIFT) |
          (iy2 << QUAD_Y2_SHIFT) |
          (ix1 << QUAD_X1_SHIFT) |
          (ix2 << QUAD_X2_SHIFT)) ;

  /* Store remaining X and Y values into quad. We'll use a mask with bits 0-3
     representing X coordinate indices that are not yet done and bits 4-7
     representing Y coordinate indices that are not done. We repeatedly handle
     the topmost bit set, and remove it from the mask. */
  shift = QUAD_YH_SHIFT ;
  mask = ((16u << 0 /*iy1*/) | (16u << iy2) | (1u << ix1) | (1u << ix2)) ^ 255 ;
  HQASSERT(mask != 0, "Initial quad mask should have 4 empty slots") ;
  do {
    int32 bit = highest_bit_set_in_byte[mask] ;
    dcoord lo, hi ;

    if ( bit >= 4 ) {
      lo = coords[bit - 4].y - bbox->y1 ;
      hi = bbox->y2 - coords[bit - 4].y ;
    } else {
      lo = coords[bit].x - bbox->x1 ;
      hi = bbox->x2 - coords[bit].x ;
    }
    HQASSERT(lo >= 0 && hi >= 0, "Quad point not in bbox") ;

    /* Use whichever delta is smallest; this makes testing for rectangles
       easier, because x2/y2 coordinates will be offset from bbox x2/y2. */
    if ( hi < lo ) {
      HQASSERT(hi <= QUAD_DELTA_MASK, "Quad x2/y2 delta out of range") ;
      lo = hi | QUAD_XY2_MINUS ;
    } else {
      HQASSERT(lo <= QUAD_DELTA_MASK, "Quad x1/y1 delta out of range") ;
      lo |= QUAD_XY1_PLUS ;
    }

    quad |= (uint32)lo << shift ;
    shift += QUAD_DELTA_BITS ;

    mask &= ~(1u << bit) ;
  } while ( mask != 0 ) ;

  HQASSERT(shift == QUAD_X2_SHIFT, "Quad delta shift damaged flags") ;

  *quad_ptr = quad;

  return TRUE;
}

/** Do these quad parameters represent a rectangle? */
Bool quad_is_rect(uint32 quad)
{
  /* For this to be true, the points must be either wound clockwise or
     anti-clockwise around the perimeter. Which means that all of the offsets
     are zero, the perimeter flags are set appropriately. We know the
     leftmost top point has y1. We search for x2 and y2 from the end of the
     set of points, so the order of the flags and offsets can be one of:

       p0        p1        p2          p3
     (x1,y1) (x2-0,y1+0) (x2,y2-0) (x1+0,y2) [clockwise]
     (x1,y1) (x1+0,y2-0) (x2-0,y2) (x2,y1+0) [anti-clockwise]

     depending on the way the path was wound.
  */
  quad &= ~QUAD_SC_MASK ; /* Don't care about scan convert rule */
  /* This looks complicated, but the expressions on the right are all constant,
     so this will compile down to two integer comparisons. */
  return (quad == ((0 << QUAD_X1_SHIFT) | /* clockwise in device space */
                   (2 << QUAD_X2_SHIFT) |
                   (3 << QUAD_Y2_SHIFT) |
                   (QUAD_XY2_MINUS << QUAD_YH_SHIFT /*y2-0*/) |
                   (QUAD_XY1_PLUS << QUAD_YL_SHIFT /*y1+0*/) |
                   (QUAD_XY1_PLUS << QUAD_XH_SHIFT /*x1+0*/) |
                   (QUAD_XY2_MINUS << QUAD_XL_SHIFT /*x2-0*/)) ||
          quad == ((0 << QUAD_X1_SHIFT) | /* anti-clockwise in device space */
                   (3 << QUAD_X2_SHIFT) |
                   (2 << QUAD_Y2_SHIFT) |
                   (QUAD_XY1_PLUS << QUAD_YH_SHIFT /*y1+0*/) |
                   (QUAD_XY2_MINUS << QUAD_YL_SHIFT /*y2-0*/) |
                   (QUAD_XY2_MINUS << QUAD_XH_SHIFT /*x2-0*/) |
                   (QUAD_XY1_PLUS << QUAD_XL_SHIFT /*x1+0*/))) ;
}

/** Do these quad parameters represent a line? */
Bool quad_is_line(uint32 quad)
{
  /* Because the quad is normalised to the topmost leftmost point in p0
     before duplicating the last point, there are four configurations that
     can be produced from a straight line. For all of these configurations,
     it must be true that the two "floating" points are the same (and
     therefore are offset from the same bbox points), that their bbox offsets
     are both zero (both points of a line are at the corners of the bbox),
     and furthermore that they match the second point.

     The four line configurations are:

       p0        p1        p2          p3
     (x2,y1) (x1,y2-0) (x1+0,y2-0) (x1+0,y2) [Second point down and left]
     (x1,y1) (x1+0,y2-0) (x1+0,y2-0) (x2,y2) [Second point directly below]
     (x1,y1) (x2-0,y2-0) (x2-0,y2-0) (x2,y2) [Second point down and right]
     (x1,y1) (x2-0,y1+0) (x2-0,y1+0) (x2,y2) [Second point directly right]

  */
  quad &= ~QUAD_SC_MASK ; /* Don't care about scan convert rule */
  /* This looks complicated, but the expressions on the right are all constant,
     so this will compile down to four integer comparisons. */
  return (quad == ((1 << QUAD_X1_SHIFT) | /* Second point down and left */
                   (0 << QUAD_X2_SHIFT) |
                   (3 << QUAD_Y2_SHIFT) |
                   (QUAD_XY2_MINUS << QUAD_YH_SHIFT /*y2-0*/) |
                   (QUAD_XY2_MINUS << QUAD_YL_SHIFT /*y2-0*/) |
                   (QUAD_XY1_PLUS << QUAD_XH_SHIFT /*x1+0*/) |
                   (QUAD_XY1_PLUS << QUAD_XL_SHIFT /*x1+0*/)) ||
          quad == ((0 << QUAD_X1_SHIFT) | /* Second point directly below */
                   (3 << QUAD_X2_SHIFT) |
                   (3 << QUAD_Y2_SHIFT) |
                   (QUAD_XY2_MINUS << QUAD_YH_SHIFT /*y2-0*/) |
                   (QUAD_XY2_MINUS << QUAD_YL_SHIFT /*y2-0*/) |
                   (QUAD_XY1_PLUS << QUAD_XH_SHIFT /*x1+0*/) |
                   (QUAD_XY1_PLUS << QUAD_XL_SHIFT /*x1+0*/)) ||
          quad == ((0 << QUAD_X1_SHIFT) | /* Second point down and right */
                   (3 << QUAD_X2_SHIFT) |
                   (3 << QUAD_Y2_SHIFT) |
                   (QUAD_XY2_MINUS << QUAD_YH_SHIFT /*y2-0*/) |
                   (QUAD_XY2_MINUS << QUAD_YL_SHIFT /*y2-0*/) |
                   (QUAD_XY2_MINUS << QUAD_XH_SHIFT /*x2-0*/) |
                   (QUAD_XY2_MINUS << QUAD_XL_SHIFT /*x2-0*/)) ||
          quad == ((0 << QUAD_X1_SHIFT) | /* Second point directly right */
                   (3 << QUAD_X2_SHIFT) |
                   (3 << QUAD_Y2_SHIFT) |
                   (QUAD_XY1_PLUS << QUAD_YH_SHIFT /*y1+0*/) |
                   (QUAD_XY1_PLUS << QUAD_YL_SHIFT /*y1+0*/) |
                   (QUAD_XY2_MINUS << QUAD_XH_SHIFT /*x2-0*/) |
                   (QUAD_XY2_MINUS << QUAD_XL_SHIFT /*x2-0*/))) ;
}

/** Do these quad parameters represent a point? */
Bool quad_is_point(uint32 quad)
{
  /* For a quad to be a point, all of the points are the same. If this happens,
     the configuration of the offsets will be:

       p0        p1        p2          p3
     (x1,y1) (x1+0,y1+0) (x1+0,y1+0) (x2,y2)
  */
  quad &= ~QUAD_SC_MASK ; /* Don't care about scan convert rule */
  /* This looks complicated, but the expressions on the right are all constant,
     so this will compile down to an integer comparison. */
  return (quad == ((0 << QUAD_X1_SHIFT) |
                   (3 << QUAD_X2_SHIFT) |
                   (3 << QUAD_Y2_SHIFT) |
                   (QUAD_XY1_PLUS << QUAD_YH_SHIFT /*y1+0*/) |
                   (QUAD_XY1_PLUS << QUAD_YL_SHIFT /*y1+0*/) |
                   (QUAD_XY1_PLUS << QUAD_XH_SHIFT /*x1+0*/) |
                   (QUAD_XY1_PLUS << QUAD_XL_SHIFT /*x1+0*/))) ;
}

/** Do these quad parameters represent a triangle? */
Bool quad_is_triangle(uint32 quad)
{
  /* A quad representing a triangle is can be detected because the xh,yh and
     xl,yl coordinates are the same, and the quad is not a line or a point. */
  return ((((quad >> QUAD_YH_SHIFT) ^ (quad >> QUAD_YL_SHIFT)) & QUAD_DELTA_MASK) == 0 &&
          (((quad >> QUAD_XH_SHIFT) ^ (quad >> QUAD_XL_SHIFT)) & QUAD_DELTA_MASK) == 0 &&
          !quad_is_line(quad) && !quad_is_point(quad)) ;
}

/** Helper function to extract the points from a quad payload. */
static int32 quad_to_points(uint32 quad, const dbbox_t *bbox, IPOINT pts[4])
{
  unsigned int mask, shift ;
  unsigned int ix1, ix2, iy2 ;

  /* Get bbox point indices */
  iy2 = (quad >> QUAD_Y2_SHIFT) & 3 ;
  ix1 = (quad >> QUAD_X1_SHIFT) & 3 ;
  ix2 = (quad >> QUAD_X2_SHIFT) & 3 ;

  /* Fill in points indexed by bbox. */
  pts[0].y = bbox->y1 ;
  pts[iy2].y = bbox->y2 ;
  pts[ix1].x = bbox->x1 ;
  pts[ix2].x = bbox->x2 ;

  shift = QUAD_YH_SHIFT ;
  mask = ((16u << 0 /*iy1*/) | (16u << iy2) | (1u << ix1) | (1u << ix2)) ^ 255 ;
  HQASSERT(mask != 0, "Initial quad mask should have 4 empty slots") ;
  do {
    int32 bit = highest_bit_set_in_byte[mask] ;
    dcoord delta = (dcoord)(quad >> shift) & QUAD_DELTA_MASK ;
    unsigned int topdown = (quad >> shift) & QUAD_XY2_MINUS ;

    if ( bit >= 4 ) {
      pts[bit - 4].y = topdown ? bbox->y2 - delta : bbox->y1 + delta ;
    } else {
      pts[bit].x = topdown ? bbox->x2 - delta : bbox->x1 + delta ;
    }

    shift += QUAD_DELTA_BITS ;
    mask &= ~(1u << bit) ;
  } while ( mask != 0 ) ;

  HQASSERT(shift == QUAD_X2_SHIFT, "Quad delta shift wrong") ;

  /* Return the number of points; either 2, 3, or 4 for line, triangle,
     quad. */
  if ( pts[3].x == pts[2].x && pts[3].y == pts[2].y ) {
    if ( pts[2].x == pts[1].x && pts[2].y == pts[1].y ) {
      if ( pts[1].x == pts[0].x && pts[1].y == pts[0].y )
        return 1 ;

      return 2 ;
    }
    return 3 ;
  }
  return 4 ;
}

/**
 * Convert a simple nfill object back into a generic nfill ready
 * for rendering.
 */
void quad_to_nfill(LISTOBJECT *lobj, NFILLOBJECT *nfill, NBRESS threads[4])
{
  IPOINT pts[4];
  int32 i, n ;

  /* Setup NFILL. The threads are suitable for EOFILL or NZFILL. */
  nfill->type = EOFILL_TYPE;
  nfill->nexty = MAXDCOORD;
  nfill->y1clip = 0;
  nfill->rcbtrap = NULL ;
  nfill->converter = CAST_TO_UINT8(lobj->dldata.quad >> QUAD_SC_SHIFT) ;
  nfill->clippedout = CLIPPED_UNCLIPPED ;

  nfill->nthreads = n = quad_to_points(lobj->dldata.quad, &lobj->bbox, pts) ;
  nfill->startnptr = nfill->endnptr = NULL ;

  for ( i = 0 ; i < n ; ++i ) {
    NBRESS *thread = &threads[i] ;
    int32 j = (i + 1) % n ;

    HQASSERT(i == j || pts[i].x != pts[j].x || pts[i].y != pts[j].y,
             "Quad side is degenerate") ;

    if ( pts[j].y <= pts[i].y ) {
      thread->nx1 = pts[j].x ;
      thread->ny1 = pts[j].y ;
      thread->nx2 = pts[i].x ;
      thread->ny2 = pts[i].y ;
      thread->norient = NORIENTUP ;
    } else {
      thread->nx1 = pts[i].x ;
      thread->ny1 = pts[i].y ;
      thread->nx2 = pts[j].x ;
      thread->ny2 = pts[j].y ;
      thread->norient = NORIENTDOWN ;
    }
    dxylist_init(&thread->dxy);
    nfill->thread[i] = thread;
  }
}

void rect_to_nfill(const dbbox_t *bbox, NFILLOBJECT *nfill, NBRESS threads[2])
{
  /* Setup NFILL. The threads are suitable for EOFILL or NZFILL. */
  nfill->type = EOFILL_TYPE;
  nfill->nexty = MAXDCOORD;
  nfill->y1clip = 0;
  nfill->rcbtrap = NULL ;
  nfill->converter = SC_RULE_HARLEQUIN ;
  nfill->clippedout = CLIPPED_UNCLIPPED ;
  nfill->nthreads = 2 ;
  nfill->startnptr = nfill->endnptr = NULL ;

  threads[0].nx1 = threads[0].nx2 = bbox->x1 ;
  threads[1].nx1 = threads[1].nx2 = bbox->x2 ;
  threads[0].ny1 = threads[1].ny1 = bbox->y1 ;
  threads[0].ny2 = threads[1].ny2 = bbox->y2 ;

  threads[0].norient = NORIENTUP ;
  dxylist_init(&threads[0].dxy);
  nfill->thread[0] = &threads[0];

  threads[1].norient = NORIENTDOWN ;
  dxylist_init(&threads[1].dxy);
  nfill->thread[1] = &threads[1];
}

#if defined(DEBUG_BUILD)
/** Debug function to print quad points. */
void debug_print_quad(uint32 quad, const dbbox_t *bbox)
{
  IPOINT pts[4] ;
  int32 i, n ;

  n = quad_to_points(quad, bbox, pts) ;

  monitorf((uint8 *)"quad rule:%d", (quad >> QUAD_SC_SHIFT) & 3);

  for ( i = 0 ; i < n ; ++i ) {
    monitorf((uint8 *)" (%d,%d)", pts[i].x, pts[i].y);
  }

  monitorf((uint8 *)"\n");
}
#endif

/**
 * Create a Display list object for the passed NFILL and add it to the
 * Display List.
 * \param[in]  type   Originating type of vector object
 * \param[in]  nfill  NFILL record, may be NULL
 * \return            Success status
 */
Bool add2dl_nfill(DL_STATE *page, int32 type, NFILLOBJECT *nfill)
{
  LISTOBJECT *lobj;
  dbbox_t bbox;
  uint32 quad;
  Bool clipped;

  if ( nfill == NULL ) /* degenerate NFILL, adding is a no-op */
    return TRUE;

  HQASSERT((type & (~(ISCLIP|ISFILL|ISRECT|CLIPINVERT|SPARSE_NFILL))) ==
           NZFILL_TYPE || (type & (~(ISCLIP|ISFILL|ISRECT|CLIPINVERT|
                                     SPARSE_NFILL))) == EOFILL_TYPE,
           "'type' should be NZFILL_TYPE or EOFILL_TYPE");
  HQASSERT(!(type & ISFILL) || !(type & ~(ISFILL|NZFILL_TYPE|EOFILL_TYPE)),
            "ISFILL type of fill has extra bits set!" );
  HQASSERT(!(type & ISCLIP) ||
           !(type & ~(ISCLIP|NZFILL_TYPE|EOFILL_TYPE|CLIPINVERT|SPARSE_NFILL)),
            "ISCLIP type of fill has other than CLIPINVERT set!");
  HQASSERT(!(type & ISRECT) || !(type & ~(ISRECT|NZFILL_TYPE)),
            "ISRECT type of fill has other than NZFILL set!");
  /* until just now, literal 1's and 0's were used for the rule, and
   * nsidetst.c had it backwards.
   * So they're now symbolics, and NZFILL_TYPE isn't 1 to allow this assert
   * to find any bits I've missed (see task 4565).
   */

  /* this is a fill, so round off the last display node and start another */
  /* if it fails, either a DL merge error occured or get_listobject() */
  /* returned a null.  return FALSE in this case */
  if ( !finishaddchardisplay(page, 1) )
    return FALSE;

  /* Because of the way that separation imposition works, we cannot allow
     quads to extend beyond the right side of the page, or they can cause
     problems with "Separation 'x' is too far right". So, we want to allow
     quads only if contained in the cclip_box, but also discard them quickly
     if they don't intersect cclip_box. */
  if ( nfill_is_quad(nfill, &bbox, &quad) &&
       bbox_contains(&cclip_bbox, &bbox) ) {
    free_nfill(nfill, page->dlpools);
    if ( !make_listobject(page, RENDER_quad, &bbox, &lobj) )
      return FALSE;
    lobj->dldata.quad = quad;
  } else {
    /* Check for degenerate. */
    bbox_nfill(nfill, &cclip_bbox, &bbox, &clipped);
    if ( clipped ) {
      free_nfill(nfill, page->dlpools);
      return TRUE;
    }

    /* Get list object, and insert object into all relevant bands. */
    if ( !make_listobject(page, RENDER_fill, &bbox, &lobj) )
      return FALSE;

    /**
     * Questionable omission of info here;-) with type field
     * \todo BMJ 25-Jun-08 :  Moved the nfill type info from the dl object
     * into the nfill, but it still seems a bit haphazard as to what bits are
     * maintained - as per the previous comment. Need to review and rationalise
     * the passing of nfill type information through the system.
     */
    nfill->type = type & ~(ISFILL|ISRECT);
    lobj->dldata.nfill = nfill;
  }

  return add_listobject(page, lobj, NULL);
}

/**
 * Utility function to help sort NBRESS threads
 * \param[in]  nbress   Array of nbress threads
 * \param[in]  num      Count of threads
 * \return              NBRESS thread
 */
static NBRESS *byquickpivot(NBRESS *nbress[], int32 num)
{
  int32 i;
  NBRESS *firstval = nbress[0];

  for ( i = ( num >> 1 ) + 1; i < num; ++i ) {
    NBRESS *nextval = nbress[i];
    if ( COMPARE_THREADS_Y(firstval, nextval, >) )
      return firstval;
    if ( COMPARE_THREADS_Y(firstval, nextval, <) )
      return nextval;
  }
  for ( i = num >> 1; i > 0; --i ) {
    NBRESS *nextval = nbress[i];
    if ( COMPARE_THREADS_Y(firstval, nextval, >) )
      return firstval;
    if ( COMPARE_THREADS_Y(firstval, nextval, <) )
      return nextval;
  }
  return NULL;
}

/**
 * Utility function to help sort NBRESS threads
 * \param[in]  nbress   Array of nbress threads
 * \param[in]  num      Count of threads
 * \param[in]  pivot    Swap point
 * \return              Number of threads
 */
static int32 byquickpartition(NBRESS *nbress[], int32 num, NBRESS *pivot)
{
  NBRESS *pqswap;
  NBRESS **p, **q;

  SwOftenSafe();

  p = nbress;
  q = p + (num - 1);

  while ( p <= q ) {
    while ( COMPARE_THREADS_Y(*p, pivot, <) ) {
      ++p;
    }
    while ( !COMPARE_THREADS_Y(*q, pivot, <) ) {
      --q;
    }
    if ( p < q ) {
      pqswap = (*p); (*p) = (*q); (*q) = pqswap;
      ++p;
      --q;
    }
  }
  return CAST_PTRDIFFT_TO_INT32(p - nbress);
}

/**
 * Utility function to help sort NBRESS threads
 * \param[in]  nbress   Array of nbress threads
 * \param[in]  num      Count of threads
 */
static void byquicknbress(NBRESS *nbress[], int32 num)
{
  int32 k;
  NBRESS *pivot;

  HQASSERT(num > 1, "what a waste of a call");

  if ( (pivot = byquickpivot(nbress, num)) == NULL )
    return;

  k = byquickpartition(nbress, num, pivot);
  if ( k > 1 )
    byquicknbress(nbress, k);
  if ( num - k > 1 )
    byquicknbress(&nbress[k], num - k);
}

/**
 * Utility function to help sort NBRESS threads
 * \param[in]  nbress   Array of nbress threads
 * \param[in]  num      Count of threads
 */
static void bysortnbress(NBRESS *nbress[], int32 num)
{
  Bool flag = TRUE;
  NBRESS **top, **bottom;

  bottom = nbress;
  top = nbress + (num - 1);

  while ( bottom < top ) {
    NBRESS *tmp0, *tmp1, **loop;

    loop = bottom;
    tmp1 = loop[0];
    while ( loop < top ) {
      tmp0 = tmp1;
      ++loop;
      tmp1 = loop[0];
      if ( COMPARE_THREADS_Y(tmp0, tmp1, >) ) {
        flag = FALSE;
        loop[-1] = tmp1;
        loop[0] = tmp0;
        tmp1 = tmp0;
      }
    }

    if ( flag )
      return;
    --top;

    loop = top;
    tmp1 = loop[0];
    while ( loop > bottom ) {
      tmp0 = tmp1;
      --loop;
      tmp1 = loop[0];
      if ( COMPARE_THREADS_Y(tmp0, tmp1, <) ) {
        flag = TRUE;
        loop[1] = tmp1;
        loop[0] = tmp0;
        tmp1 = tmp0;
      }
    }

    if ( !flag )
      return;
    ++bottom;

    SwOftenUnsafe();
  }
}

/**
 * Get the NFILL record ready for rendering by initialising all the
 * DDA parameters etc.
 * \param[in]  nfill  NFILL record
 */
void preset_nfill(NFILLOBJECT *nfill)
{
  dcoord x1, y1, x2, y2, y1c;
  int32 thd;

  SwOftenUnsafe();

  y1c = nfill->y1clip;
  nfill->startnptr = nfill->endnptr = &(nfill->thread[0]);

  /* Setup the DDA constants for each thread. */
  for ( thd = 0; thd < nfill->nthreads; thd++ ) {
    NBRESS *nbress = nfill->thread[thd];

    dxylist_reset(&(nbress->dxy));
    nbress->flags = 0;
    x1 = nbress->nx1; y1 = nbress->ny1;
    x2 = nbress->nx2; y2 = nbress->ny2;

    /* Check for y-clipping on top only - used only when rotated images. */
    while ( y2 < y1c ) {
      Bool ok;

      x1 = x2; y1 = y2;
      ok = dxylist_get(&(nbress->dxy), &x2, &y2);
      HQASSERT(ok, "completely clipped out thread");
    }
    nbress->nmindy = 2*(y2-y1);
    nbress->u1.ncx = x1;
    DDA_SCAN_INITIALISE(nbress, x2-x1, y2-y1);

    if ( y1 < y1c ) { /* help sort by putting any that start before y1c first */
      NBRESS *swapbress = nfill->endnptr[0];;

      DDA_SCAN_STEP_N(nbress, y1c - y1);
      nfill->endnptr[0] = nfill->thread[thd];
      nfill->thread[thd] = swapbress;;
      nfill->endnptr++;
    }
  }
  nfill->nexty = y1c;

  /* Finally, sort the threads in y direction. */
  if ( nfill->nthreads > 4 )
    byquicknbress(nfill->thread, nfill->nthreads);
  else
    bysortnbress(nfill->thread, nfill->nthreads);
}

/**
 * Updates the NFILLOBJECT so that clipped on bottom to 'y1c'.
 * \param[in]  nfill  NFILL record
 * \param[in]  y1c    new y clip limit
 */
void repair_nfill(NFILLOBJECT *nfill, dcoord y1c)
{
  int32 thd;

  for ( thd = CAST_PTRDIFFT_TO_INT32(nfill->startnptr - nfill->thread);
        thd < nfill->nthreads; thd++ ) {
    dcoord x1, y1, x2, y2;
    NBRESS *nbress = nfill->thread[thd];

    /* Find a coord-pair (if any) that is greater than y1clip. */
    if ( nbress->ny1 >= y1c ) /* Finished when this condition met. */
      break;

    x1 = nbress->nx1; y1 = nbress->ny1;
    x2 = nbress->nx2; y2 = nbress->ny2;
    dxylist_reset(&(nbress->dxy));
    while ( y2 < y1c && nbress ) {
      x1 = x2; y1 = y2;
      if ( !dxylist_get(&(nbress->dxy), &x2, &y2) ) {
        nfill->thread[thd]  = nfill->startnptr[0];
        nfill->startnptr[0] = nbress;
        nfill->startnptr++;
        nbress = NULL;
      }
    }
    if ( nbress ) { /* New line needs preseting. */
      nbress->nmindy = 2*(y2-y1);
      nbress->u1.ncx = x1;

      DDA_SCAN_INITIALISE(nbress, x2-x1, y2-y1);
      if ( y1 < y1c )
        DDA_SCAN_STEP_N(nbress, y1c - y1);
    }
  }
  nfill->endnptr= &(nfill->thread[thd]);
  nfill->nexty = y1c;
  SwOftenUnsafe();
}

/**
 * Swap the X and Y coordinates of a whole NFILL, in preparation for the
 * second dropout control pass of character rendering.
 * \param[in]  nfill  NFILL record
 */
void swapxy_nfill(NFILLOBJECT *nfill)
{
  int32 thd;

  HQASSERT(nfill->y1clip == 0,
           "y1clip for swapped fill non-zero;columns will be truncated");

  for ( thd = 0; thd < nfill->nthreads; thd++ ) {
    NBRESS *nbress = nfill->thread[thd];
    dcoord x1, y1, x2, y2;

    x1 = nbress->nx1; y1 = nbress->ny1;
    x2 = nbress->nx2; y2 = nbress->ny2;

    HQASSERT(dxylist_empty(&(nbress->dxy)), "X/Y swap not valid for deltas");
    if ( x2 > x1 ) { /* Swap X and Y, same orientation */
      nbress->nx1 = y1;
      nbress->ny1 = x1;
      nbress->nx2 = y2;
      nbress->ny2 = x2;
    }
    else { /* Swap X and Y, reverse orientation */
      nbress->nx1 = y2;
      nbress->ny1 = x2;
      nbress->nx2 = y1;
      nbress->ny2 = x1;
      nbress->norient = (int8)-nbress->norient;
    }
  }
  nfill->nexty = MAXDCOORD; /* Forces initialisation first time round */
}

/**
 * Auxiliary function for sorting threads by X and then gradient. Version
 * 1.178 has a 32x32 long cross multiplication version of this, but it is
 * cumbersome and slow, so we will use double precision floating point. Using
 * double precision directly will quite probably be quicker; 48 bits of
 * precision gives up to 5.6 million pixels in Y before overflow, or about
 * 198 feet at 2500 dpi.
 * \param[in]  e1 numerator 1
 * \param[in]  d1 denominator 1
 * \param[in]  e2 numertaor 2
 * \param[in]  d2 denominator 2
 * \return        Sign of result of cross multiplication
 */
static int32 dda_cross_sign(dcoord e1, dcoord d1, dcoord e2, dcoord d2)
{
  double result = (double)e1 * (double)d2 - (double)e2 * (double)d1;

  if ( result < 0 )
    return -1;

  if ( result > 0 )
    return 1;

  return 0;
}

/**
 * Compare the X error and gradient. The current X coordinate is compared as
 * part of the COMPARE_THREADS_X macro, and this function is used only if the
 * X coordinates are the same. It should (but currently doesn't) perform the
 * cross-multiplication safely to compare X error and gradient, returning
 * less than zero, zero, or greater than zero to indicate the sort order.
 * \param[in]  t1 First thread
 * \param[in]  t2 Second thread
 * \return        Result of comparison
 */
int32 compare_nbress_xes(NBRESS *t1, NBRESS *t2)
{
  dcoord d1, d2;
  dcoord dd, dxe;
  int32 order;
#if defined(ASSERT_BUILD)
  int32 cross_sign;
#endif

  /* Threads may be compared against themselves during quicksort, if the
     thread is chosen as the pivot. Short-circuit this case. */
  if ( t1 == t2 )
    return 0;

  HQASSERT(t1->u1.ncx == t2->u1.ncx,
           "No need to compare X errors unless X is the same");

  /* We want to test the ordering of the X error, and then the gradient if
     the X error is the same. The X errors and gradients are likely to use
     different denominators, so we have to cross multiply the error
     (or gradient) with the denominator to do this.
     This suffers from two problems:

     1) Horizontal lines, represented with a denominator of 0, do not sort
        correctly against other lines when cross multiplying, because both
        sides of the cross multiplication will be zero.
     2) The multiplications can overflow 32 bits. This can happen with long
        near-vertical lines at high resolution (11 inches long at 3000 dpi).

     We will use some properties of the denominators and errors to speed up
     these tests. Initially,

     0 <= xe1 < d1    if t1 is not horizontal
     0 == xe1 == d1   if t1 is horizontal

     0 <= xe2 < d2    if t2 is not horizontal
     0 == xe2 == d2   if t2 is horizontal

     dxe = xe1 - xe2
     dd = d1 - d2

     -d2 < -xe2 <= dxe <= xe1 < d1   if t1, t2 are not horizontal
     -d2 <= dd <= d1

     For non-horizontals, we want to return:

     xe1 * d2 - xe2 * d1

     or its sign (the actual value returned doesn't matter, just the sign of
     it). We can start with a quick test to eliminate some of the cases; if
     xe1 > xe2 and d2 > d1, or xe1 < xe2 and d2 < d1, we know the result must
     be positive or negative respectively (the test can be slightly more
     relaxed, in that one of xe1 == xe2 or d2 == d1 is allowable, but not
     both at the same time). This test can be simplified further, to
     dxe > 0 and dd < 0 (or one of them zero), or dxe < 0 and dd > 0.

     We can also refactor the expression above to return the sign of

     dxe * d1 - dd * xe1

     because

     (xe1 - xe2) * d1 - (d1 - d2) * xe1
     = xe1 * d1 - xe2 * d1 - xe1 * d1 + xe1 * d2
     = -xe2 * d1 + xe1 * d2
     = xe1 * d2 - xe2 * d1

     or similarly, we could return the sign of

     dxe * d2 - dd * xe2

     These formulations may be useful because d2 >= xe2; however, dxe and/or
     dd may be negative.

     Horizontal threads can be sorted before or after other gradients and
     X errors, so long as the sort is consistent and stable. Stability means:

     sign(compare_nbress_xes(t1, t2)) == -sign(compare_nbress_xes(t2, t1))
*/

  d1 = t1->denom; /* d1 >= 0 */
  d2 = t2->denom; /* d2 >= 0 */
  HQASSERT(d1 >= 0 && d2 >= 0, "Denominator of DDA step is negative");
  HQASSERT(0 <= t1->xe && t1->xe <= d1, "X1 error out of range");
  HQASSERT(0 <= t2->xe && t2->xe <= d2, "X2 error out of range");

  /* Quick tests to see if cross-multiplied values will overflow. */
  dd = d1 - d2;
  dxe = t1->xe - t2->xe;

  if ( dd > 0 ) {
    /* d1 > d2, but d2 may be zero. If d2 is zero, then xe2 is zero, and
       therefore dxe >= 0. We need to sort horizontals consistently with
       respect to other threads, but we don't need to interleave them between
       other gradients. We sort horizontals before other gradients, so that
       degenerate segments are consistently sorted with respect to other
       gradients. */
    if ( d2 == 0 )
      /* Using -t2->si would interleave non-degenerate horizontals correctly,
         but degenerates can exist as the first segment of a thread, and
         would compare equal to all gradients, potentially confusing
         sorting functions. */
      return 1;

    /* We nest the tests here so that the common case dxe > 0 does not
       suffer. */
    if ( dxe <= 0 ) {
      if ( dxe == 0 && t1->xe == 0 ) {
        HQASSERT(dda_cross_sign(t1->xe, d1, t2->xe, d2) == 0,
                 "Cross product does not match quick test (zero)");
        return 0;
      }

      HQASSERT(dda_cross_sign(t1->xe, d1, t2->xe, d2) < 0,
               "Cross product does not match quick test (negative)");
      return -1;
    }

    if ( d1 <= 46340 ) {
      /* d1 > d2, and d1 < sqrt(2^31), so cross multiplication cannot
         overflow 32 bit signed integers. */
      HQASSERT(MAXDCOORD / d2 >= t1->xe && MAXDCOORD / d1 >= t2->xe,
               "Partial cross product result overflows");
      order = t1->xe * d2 - t2->xe * d1;
      if ( order == 0 ) {
        order = t1->si - t2->si;
        if ( order == 0 ) {
          HQASSERT(MAXDCOORD / d2 >= t1->sf && MAXDCOORD / d1 >= t2->sf,
                   "Partial cross product result overflows");
          order = t1->sf * d2 - t2->sf * d1;
#if defined(ASSERT_BUILD)
          cross_sign = dda_cross_sign(t1->sf, d1, t2->sf, d2);
#endif
        }
#if defined(ASSERT_BUILD)
        else {
          /* The cross product is meaningless for the si values, because they
             are not fractions. Force the consistency assert to succeed. */
          cross_sign = order;
        }
#endif
      }
#if defined(ASSERT_BUILD)
      else {
        cross_sign = dda_cross_sign(t1->xe, d1, t2->xe, d2);
      }
#endif

      HQASSERT((order < 0 && cross_sign < 0) ||
               (order > 0 && cross_sign > 0) ||
               (order == 0 && cross_sign == 0),
               "Returned order inconsistent with cross product");
      return order;
    }
  }
  else if ( dd < 0 ) {
    /* d1 < d2, but d1 may be zero. If d1 is zero, then xe1 is zero, and
       therefore dxe <= 0. We need to sort horizontals consistently with
       respect to other threads, but we don't need to interleave them between
       other gradients. We sort horizontals before other gradients, so that
       degenerate segments are consistently sorted with respect to other
       gradients. */
    if ( d1 == 0 )
      /* Using t1->si would interleave non-degenerate horizontals correctly,
         but degenerates can exist as the first segment of a thread, and
         would compare equal to all gradients, potentially confusing
         sorting functions. */
      return -1;

    /* We nest the tests here so that the common case dxe < 0 does not
       suffer. */
    if ( dxe >= 0 ) {
      if ( dxe == 0 && t1->xe == 0 ) {
        HQASSERT(dda_cross_sign(t1->xe, d1, t2->xe, d2) == 0,
                 "Cross product does not match quick test (zero)");
        return 0;
      }

      HQASSERT(dda_cross_sign(t1->xe, d1, t2->xe, d2) > 0,
               "Cross product does not match quick test (positive)");
      return 1;
    }

    if ( d2 <= 46340 ) {
      /* d1 < d2, and d2 < sqrt(2^31), so cross multiplication cannot
         overflow 32 bit signed integers. */
      HQASSERT(MAXDCOORD / d2 >= t1->xe && MAXDCOORD / d1 >= t2->xe,
               "Partial cross product result overflows");
      order = t1->xe * d2 - t2->xe * d1;
      if ( order == 0 ) {
        order = t1->si - t2->si;
        if ( order == 0 ) {
          HQASSERT(MAXDCOORD / d2 >= t1->sf && MAXDCOORD / d1 >= t2->sf,
                   "Partial cross product result overflows");
          order = t1->sf * d2 - t2->sf * d1;
#if defined(ASSERT_BUILD)
          cross_sign = dda_cross_sign(t1->sf, d1, t2->sf, d2);
#endif
        }
#if defined(ASSERT_BUILD)
        else
          /* The cross product is meaningless for the si values, because they
             are not fractions. Force the consistency assert to succeed. */
          cross_sign = order;
#endif
      }
#if defined(ASSERT_BUILD)
      else {
        cross_sign = dda_cross_sign(t1->xe, d1, t2->xe, d2);
      }
#endif

      HQASSERT((order < 0 && cross_sign < 0) ||
               (order > 0 && cross_sign > 0) ||
               (order == 0 && cross_sign == 0),
               "Returned order inconsistent with cross product");
      return order;
    }
  }
  else {
    /* The denominators are the same. We can greatly simplify the tests
       required. These tests sort horizontals against each other as well. */
    if ( dxe != 0 ) {
      HQASSERT((dxe < 0 && dda_cross_sign(t1->xe, d1, t2->xe, d2) < 0) ||
               (dxe > 0 && dda_cross_sign(t1->xe, d1, t2->xe, d2) > 0),
               "Returned order inconsistent with cross product");
      return dxe;
    }

    order = t1->si - t2->si;
    if ( order != 0 ) /* No assertion here, these are on the same basis */
      return order;

    order = t1->sf - t2->sf;
    return order;
  }

  HQASSERT(d1 > 0 && d2 > 0,
           "Horizontal lines should have been removed by denominator tests");

  order = dda_cross_sign(t1->xe, d1, t2->xe, d2);
  if ( order == 0 ) {
    order = t1->si - t2->si;
    if ( order == 0 )
      order = dda_cross_sign(t1->sf, d1, t2->sf, d2);
  }

  return order;
}

#if defined(DEBUG_BUILD) || defined(ASSET_BUILD)
/**
 * Create a debug print-out of the given NFILL object
 * \param[in]  nfill   Nfill object being printed
 * \param[in]  vlevel  Level of verbosity
 */
void debug_print_nfill(NFILLOBJECT *nfill, int32 vlevel)
{
  int32 i;
  dbbox_t bbox, clip;
  Bool clipped;

  bbox_store(&clip, MINDCOORD, MINDCOORD, MAXDCOORD, MAXDCOORD);
  bbox_nfill(nfill, &clip, &bbox, &clipped);

  monitorf((uint8 *)"nfill bbox(%d,%d,%d,%d) thd:%d(", bbox.x1,
           bbox.y1, bbox.x2, bbox.y2, nfill->nthreads);

  for ( i = 0; i < nfill->nthreads; i++ ) /* #co-ords in each thread */
    monitorf((uint8 *)"%d:", 2+nfill->thread[i]->dxy.ndeltas);
  monitorf((uint8 *)") ");

  if ( vlevel > 0 ) {
    for ( i = 0; i < nfill->nthreads; i++ ) {
      NBRESS *line = nfill->thread[i];
      dcoord x = line->nx2;
      dcoord y = line->ny2;

      monitorf((uint8*)"%d o %d %d m %d %d l ", line->norient, line->nx1,
          line->ny1, x, y);
      dxylist_reset(&(line->dxy));
      while ( dxylist_get(&(line->dxy), &x, &y) )
        monitorf((uint8*)"%d %d l ", x, y);
    }
    monitorf((uint8*)"s "); /* stroke the whole thing */
  }
  monitorf((uint8 *)"\n");
}
#endif /* DEBUG_BUILD || ASSERT_BUILD */

/* Log stripped */
