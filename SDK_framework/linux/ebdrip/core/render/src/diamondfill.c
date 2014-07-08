/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!src:diamondfill.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Diamond fill function, used to generate image tiles
 */

#include "core.h"

#include "render.h"     /* render_blit_t et. al. */

#include "scanconv.h"
#include "scpriv.h"

#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"

#include "ndisplay.h"
#include "dl_bres.h"

/** Fill a parallelogram, given by a point and two edge vectors. The
    non-overlapping scan converter is used; this is used by the rotated image
    tile generation to create tiles which tesselate exactly. */
void diamond_fill(render_blit_t *rb,
                  dcoord x1, dcoord y1,
                  dcoord dx1, dcoord dy1,
                  dcoord dx2, dcoord dy2)
{
  NFILLOBJECT nfill;
  NBRESS nbress0[2], nbress1[2], nbress2[2], nbress3[2];
  dcoord x2, y2, x3, y3, x4, y4 ;

  /* Flip the vectors and adjust the reference point if they are negative in
     Y. */
  if ( dy1 < 0 ) {
    x1 += dx1 ; y1 += dy1 ;
    dx1 = -dx1 ; dy1 = -dy1 ;
  }

  if ( dy2 < 0 ) {
    x1 += dx2 ; y1 += dy2 ;
    dx2 = -dx2 ; dy2 = -dy2 ;
  }

  /* Convert the base point plus vectors into four points, going
     clockwise around the diamond. */
  if ( dy1 == dy2
       ? dx1 < dx2 /* Quick test for degenerate height and 45 degree case. */
       : dx1 * dy2 - dx2 * dy1 > 0 ) {
    dcoord tmp ;
    tmp = dx1 ; dx1 = dx2 ; dx2 = tmp ;
    tmp = dy1 ; dy1 = dy2 ; dy2 = tmp ;
  }

  x2 = x1 + dx1 ;
  y2 = y1 + dy1 ;
  x3 = x2 + dx2 ;
  y3 = y2 + dy2 ;
  x4 = x1 + dx2 ;
  y4 = y1 + dy2 ;

  HQASSERT(y1 <= y2 && y1 <= y4, "diamond_fill: top point is not top");
  HQASSERT(y3 >= y2 && y3 >= y4, "diamond_fill: bottom point is not bottom");

  /* The point and nbress indices are:

                  (x1,y1)
                     +
                    / \
           nbress0 /   \ nbress1
                  /     \
                 /       \
        (x2,y2) +         + (x4,y4)
                 \       /
                  \     /
           nbress2 \   / nbress3
                    \ /
                     +
                  (x3,y3)

  */

  /* Initialise NFILLOBJECT data. Use the tesselation fill rule. */
  nfill.y1clip = rb->p_ri->clip.y1;
  nfill.nthreads = 4 ;
  HQASSERT(ST_NTHREADS >= 4, "Nfill on stack not big enough");
  nfill.converter = SC_RULE_TESSELATE ;

  nbress0->nx1 = nbress1->nx1 = x1;
  nbress0->ny1 = nbress1->ny1 = y1;
  nbress2->nx1 = nbress0->nx2 = x2;
  nbress2->ny1 = nbress0->ny2 = y2;
  nbress3->nx1 = nbress1->nx2 = x4;
  nbress3->ny1 = nbress1->ny2 = y4;
  nbress2->nx2 = nbress3->nx2 = x3;
  nbress2->ny2 = nbress3->ny2 = y3;

  /* Set chain pointers for required threads (RHS) */
  dxylist_init(&(nbress0->dxy));
  dxylist_init(&(nbress1->dxy));
  dxylist_init(&(nbress2->dxy));
  dxylist_init(&(nbress3->dxy));
  nfill.thread[0] = nbress0;
  nfill.thread[1] = nbress1;
  nfill.thread[2] = nbress2;
  nfill.thread[3] = nbress3;

  /* Set orientations depending on whether we're doing RHS or not */
  nbress0->norient = NORIENTDOWN; /* counter-clockwise orient's */
  nbress1->norient = NORIENTUP;   /* Ready for NZFILL on area */
  nbress2->norient = NORIENTDOWN;
  nbress3->norient = NORIENTUP;

  /* Remove any completely clipped out lines */
  { int32 i = 0, j;
    dcoord y1clip = rb->p_ri->clip.y1;

    while ( i < nfill.nthreads ) {
      if ( nfill.thread[i]->ny2 >= y1clip )
        ++i ;
      else {
        if ( --nfill.nthreads == 0 )
          return ;
        for ( j = i ; j < nfill.nthreads ; ++j )
          nfill.thread[j] = nfill.thread[j+1];
      }
    }
  }

  /* Set up fill and go for it. */
  preset_nfill( & nfill ) ;
  /* Using SC_RULE_TESSELATE, so don't need to enable spanlist coalescing */
  scanconvert_tesselate(rb, &nfill, NZFILL_TYPE, NULL) ;
}


/* Log stripped */
