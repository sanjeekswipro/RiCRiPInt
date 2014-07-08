/** \file
 * \ingroup toneblit
 *
 * $HopeName: SWv20!export:toneblt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Blit functions for contone output.
 */

#ifndef __TONEBLT_H__
#define __TONEBLT_H__

/** \defgroup toneblit Tone blits
    \ingroup rendering */
/** \{ */

#include "bitbltt.h" /* BITBLT_FUNCTION, blit_t, et al */
#include "objnamer.h"

struct render_blit_t ;

/* ---------------------------------------------------------------- */

/** \brief The generic bitmap char blit function.

    This function converts a bitmap-encoded char definition to a series of
    blocks or spans, and calls down the blit chain. */
void charbltn(struct render_blit_t *rb,
              FORM * formptr, dcoord x ,dcoord y);
void blkclipn(struct render_blit_t *rb,
              dcoord ys, dcoord ye, dcoord xs, dcoord xe,
              BITBLT_FUNCTION fillspan);
void blkclipn_coalesce(struct render_blit_t *rb,
                       dcoord ys, dcoord ye, dcoord xs, dcoord xe,
                       BLKBLT_FUNCTION fillblock);
void bitclipn(struct render_blit_t *rb,
              register dcoord y, register dcoord xs, register dcoord xe,
              BITBLT_FUNCTION fillspan);

/** \brief The band RLE char blit function.

    This function converts a BANDRLEENCODED form definition to a series of
    blocks or spans, and calls down the blit chain. It is used to replicate
    pattern shapes. */
void charbltspan(struct render_blit_t *rb,
                 FORM *formptr , dcoord x , dcoord y ) ;

/** \brief The generic image blit function.

    This function converts an image to a series of chars, fills, blocks or
    spans, and calls down the blit chain. */
void imagebltn(struct render_blit_t *rb,
               imgblt_params_t *params,
               imgblt_callback_fn *callback,
               Bool *result);

/** \brief Image blit function for masks and white halftone. */
void imageblt0(struct render_blit_t *rb,
               imgblt_params_t *params,
               imgblt_callback_fn *callback,
               Bool *result);

/** \brief Image blit function for masks and black halftone. */
void imageblt1(struct render_blit_t *rb,
               imgblt_params_t *params,
               imgblt_callback_fn *callback,
               Bool *result);

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
