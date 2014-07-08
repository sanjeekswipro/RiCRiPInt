/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!export:bitblts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bit blitting routines and structures.
 */

#ifndef __BITBLTS_H__
#define __BITBLTS_H__ 1

/** \defgroup bitblit Bi-level blits
    \ingroup rendering */
/** \{ */

#include "bitbltt.h" /* basic typedefs */
#include "tables.h" /* highest_bit_set_in_bits */

struct render_blit_t ; /* from render.h */
struct NFILLOBJECT ; /* from displayt.h */

/** \page blitstack Span/block/char interface.

   The span function called by DO_SPAN in the CORErender scan converter
   functions and friends refers to the start of a chain of span functions.
   Similarly, the character and block renderers are called by DO_CHAR and
   DO_BLOCK in various places. The DO_SNFILL and DO_IMG macros will be used
   in future. These functions may either process and blit the
   span/block/snfill/char/image themselves, or use the remaining functions in
   the chain to assist in performing the blit. DO_SPAN sets up the pointers
   so that another call of DO_SPAN within the first span function called will
   call the next span function in the chain. DO_BLOCK sets up the pointers so
   that another call of DO_BLOCK will call the next block function in the
   chain, but DO_SPAN will call the span function at the same level. In an
   snfill function, a call to DO_SNFILL will call the next snfill function in
   the chain, but DO_SPAN and DO_BLOCK will call the span or block functions
   at the same level. Similarly, in DO_CHAR another call of DO_CHAR will call
   the next char blit function, but DO_SPAN, DO_BLOCK and DO_SNFILL will call
   the span, block or snfill at the same level. This is needed for
   block/snfill/char functions which call span (or block or snfill) functions
   to implement their own functionality. Finally, inside DO_IMG, calls to
   DO_IMG will call the next level, DO_SPAN, DO_BLOCK, DO_SNFILL or DO_CHAR
   will call the same level.

   The calling sequences can be visualised as a matrix, with only sideways
   (from image to char/snfill/block/span, from char to snfill/block/span,
   from snfill to block/span or from block to span), and downwards (from
   coalesce to gouraud to pattern to intersect to maxblit to base) calls
   allowed.

   There is a defined order to the entries in this chain. The base span
   function is the lowest level blitter, which writes a span in the
   appropriate colour/halftone level into a band/form. The next level are the
   maxblit functions, for performing special overprint blitting. At the same
   index as max-blt (because they're mutually exclusive), are the PCL ROP
   blits. The intersection clipping blits are above these, they are used to
   knock out parts of a shape so that self-intersections will not cause
   compositing operations against the shape itself. In some cases, this may
   use the base function to blit a span into a temporary form, which is then
   combined with the desired output form. The next level are the three
   pattern blits (explained together here). The pattern recurse blit sets up
   blits for the next pattern level, or the base level. The pattern
   replicators copy the span over the area of the band in which the pattern
   is present. The pattern clip blits restrict the incoming spans either to
   the clip shapes or the pattern shapes at a particular pattern nesting
   level.

   Above these are gouraud span functions, which divide gouraud fill areas
   into smaller spans with colour changes between them, and then gouraud
   noise functions, which apply colour perturbations to the gouraud span
   functions on a rectangular grid. The final level is the coalescing span
   layer, which take multiple incoming spans and coalesce them into single
   spans before passing them to the lower level span functions. Each level of
   the blit chain has a private data pointer that can be set when the blits
   are setup. The private data pointer can be used by each blit layer to
   access state associated with that blit layer.

   There are macros to manipulate the special slots directly, which cause the
   span to be inserted into an existing chain. Modifying the order whilst in
   a blit function is not allowed, but replacing existing blit functions with
   different ones is acceptable (halftone and some contone blitters use this
   to optimise for particular color values or screen levels).

   Temporary changes to the blit chain to create character caches, clip
   masks, rotated image tiles, trap masks, or perform insideness testing are
   handled by substituting a totally different chain in place of the current
   blit chain in the render context.

   The blit functions which have been set are tracked using a mask; a table
   lookup using the mask is used to quickly find the next blit to execute.
*/

enum { BASE_BLIT_INDEX = 0,
       MAXBLT_BLIT_INDEX, ROP_BLIT_INDEX = MAXBLT_BLIT_INDEX,
       OM_BLIT_INDEX,
       INTERSECT_BLIT_INDEX,
       PATTERNRECURSE_BLIT_INDEX,
       PATTERN_BLIT_INDEX, PCL_PATTERN_BLIT_INDEX = PATTERN_BLIT_INDEX,
       PATTERNCLIP_BLIT_INDEX,
       GOURAUD_BASE_BLIT_INDEX,
       GOURAUD_BLIT_INDEX,
       COALESCE_BLIT_INDEX, /* Should be top blit index */
       BLIT_STACK_SIZE /* MUST be last element in enum */
} ;


struct blit_chain_t {
  /* These variables should ONLY be manipulated through the macros below */
  uint32 blit_mask, blit_span, blit_block, blit_snfill, blit_char, blit_img ;
  struct {
    blit_slice_t const *functions[3] ; /* Rect, Complex, No clipping */
    /*@dependent@*/
    void *data ; /* Private data for blit stack layer, owned by that layer */
  } layer[BLIT_STACK_SIZE] ;
} ;

#define BLIT_MASK(_bi) ((2 << (_bi)) - 1) /* Mask for bits 0.._bi */
#define BLIT_MASK_NEXT(_bi) ((1 << (_bi)) - 1) /* Mask for bits 0.._bi-1 */

void invalid_span(struct render_blit_t *ri,
                  dcoord y, dcoord xs, dcoord xe) ;
void invalid_block(struct render_blit_t *ri,
                   dcoord ys, dcoord ye, dcoord xs, dcoord xe) ;
void invalid_snfill(struct render_blit_t *ri,
                    struct NFILLOBJECT *nfill, dcoord ys, dcoord ye) ;
void invalid_char(struct render_blit_t *ri,
                  FORM *formptr, dcoord x, dcoord y) ;
void invalid_imgblt(struct render_blit_t *ri,
                    imgblt_params_t *params,
                    imgblt_callback_fn *callback,
                    Bool *result) ;

extern blit_slice_t invalid_slice ;

/** Initialiser for blit slices. */
#define BLIT_SLICE_INIT \
  { invalid_span, invalid_block, invalid_snfill, invalid_char, invalid_imgblt }

/** Initialiser for clip-optimised blit slices. */
#define BLITCLIP_SLICE_INIT \
  { BLIT_SLICE_INIT, /* Rect clipped */ \
    BLIT_SLICE_INIT, /* Complex clipped */ \
    BLIT_SLICE_INIT  /* Unclipped */ }

/** Initialiser for empty clip-optimised blit slices. */
#define NULL_SLICE_INIT \
  { NULL /*span*/, NULL /*block*/, NULL /*snfill*/, NULL /*char*/, NULL /*img*/ }

/** Initialiser for empty clip-optimised blit slices. */
#define NULLCLIP_SLICE_INIT \
  { NULL_SLICE_INIT, /* Rect clipped */ \
    NULL_SLICE_INIT, /* Complex clipped */ \
    NULL_SLICE_INIT  /* Unclipped */ }

void next_span(struct render_blit_t *ri,
               dcoord y, dcoord xs, dcoord xe) ;
void next_block(struct render_blit_t *ri,
                dcoord ys, dcoord ye, dcoord xs, dcoord xe) ;
void next_snfill(struct render_blit_t *ri,
                 struct NFILLOBJECT *nfill, dcoord ys, dcoord ye) ;
void next_char(struct render_blit_t *ri,
               FORM *formptr, dcoord x, dcoord y) ;
void next_imgblt(struct render_blit_t *ri,
                 imgblt_params_t *params,
                 imgblt_callback_fn *callback,
                 Bool *result) ;

extern blit_slice_t next_slice ;

void ignore_span(struct render_blit_t *ri,
                 dcoord y, dcoord xs, dcoord xe) ;
void ignore_block(struct render_blit_t *ri,
                  dcoord ys, dcoord ye, dcoord xs, dcoord xe) ;
void ignore_snfill(struct render_blit_t *ri,
                   struct NFILLOBJECT *nfill, dcoord ys, dcoord ye) ;
void ignore_char(struct render_blit_t *ri,
                 FORM *formptr, dcoord x, dcoord y) ;
void ignore_imgblt(struct render_blit_t *ri,
                   imgblt_params_t *params,
                   imgblt_callback_fn *callback,
                   Bool *result) ;

extern blit_slice_t ignore_slice ;

#if defined( DEBUG_BUILD )
#define DEBUG_BUILD_ONLY(_x) _x
#else
#define DEBUG_BUILD_ONLY(_x) EMPTY_STATEMENT()
#endif

/** Test if we are doing a particular blit layer. */
#define DOING_BLITS(_blits,_bi) (((_blits)->blit_mask & (1 << (_bi))) != 0)

/** Evaluates to true if only the base blit is active. */
#define DOING_BASE_BLIT_ONLY(_blits) \
  (((_blits)->blit_mask & (~(1 << BASE_BLIT_INDEX))) == 0)

#ifdef ASSERT_BUILD
/** Assert that all of the layers except the base blits are either unset, or
    are set to the forwarding function. */
#define ASSERT_BASE_OR_FORWARD_ONLY(rb_, fnfield_, nextfn_, msg_) MACRO_START \
  blit_chain_t *blits = (rb_)->blits ;                             \
  uint32 mask = blits->blit_mask & ~(1 << BASE_BLIT_INDEX) ;       \
  while ( mask != 0 ) {                                            \
    int32 index = highest_bit_set_in_bits[mask] ;                  \
    HQASSERT(blits->layer[index].functions[(rb_)->clipmode]->fnfield_ == (nextfn_), msg_) ; \
    mask &= BLIT_MASK_NEXT(index) ;                                \
  }                                                                \
MACRO_END
#else
#define ASSERT_BASE_OR_FORWARD_ONLY(rb_, fnfield_, nextfn_, msg_) EMPTY_STATEMENT()
#endif

/* Setting/clearing blits; a bitmask is maintained to tell which blits are
   set. This is used for fast resetting of the blit stack pointers when blits
   are cleared. We cannot assert that the rollover blits are reset in
   RESET_BLITS, because they need to be called in order to provoke a flush; a
   clipped out part of an object on the previous band rendered can leave them
   set. The pattern blits may also be left set if rendering is interrupted in
   the middle of the pattern. */
#define RESET_BLITS(_blits,_unclipped,_rectclip,_complexclip) MACRO_START \
  blit_chain_t *_blits_rb_ = (_blits) ; \
  HQASSERT(_blits_rb_, "RESET_BLITS blit pointer NULL") ; \
  _blits_rb_->blit_mask = 0 ; \
  SET_BLITS(_blits_rb_, BASE_BLIT_INDEX, _unclipped, _rectclip, _complexclip) ; \
MACRO_END

#define SET_BLITS(_blits,_bi,_unclipped,_rectclip,_complexclip) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  HQASSERT(_blits_, "SET_BLITS blit pointer NULL") ; \
  HQASSERT((_bi) >= BASE_BLIT_INDEX && (_bi) < BLIT_STACK_SIZE && \
           BLIT_STACK_SIZE <= HIGHEST_BIT_SET_LIMIT, "SET_BLITS index out of range") ; \
  _blits_->layer[_bi].functions[BLT_CLP_RECT] = (_rectclip) ; \
  _blits_->layer[_bi].functions[BLT_CLP_COMPLEX] = (_complexclip) ; \
  _blits_->layer[_bi].functions[BLT_CLP_NONE] = (_unclipped) ; \
  _blits_->blit_mask |= (1 << (_bi)) ; \
  _blits_->blit_span = _blits_->blit_block = _blits_->blit_snfill = \
    _blits_->blit_char = _blits_->blit_img = \
    highest_bit_set_in_bits[_blits_->blit_mask] ; \
MACRO_END

/* Special versions to alter spans and block functions which are already in
   use, without resetting the stack indices. These are used to modify the
   blit stack while it is in use, for rollovers and self-intersection
   clipping. */
#define SET_BLITS_CURRENT(_blits,_bi,_unclipped,_rectclip,_complexclip) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  HQASSERT(_blits_, "SET_BLITS_CURRENT blit pointer NULL") ; \
  HQASSERT((_bi) >= BASE_BLIT_INDEX && (_bi) < BLIT_STACK_SIZE && \
           BLIT_STACK_SIZE <= HIGHEST_BIT_SET_LIMIT, \
           "SET_BLITS_CURRENT index out of range") ; \
  HQASSERT(DOING_BLITS(_blits_, (_bi)), \
           "SET_BLITS_CURRENT blit layer not already set") ;               \
  _blits_->layer[_bi].functions[BLT_CLP_RECT] = (_rectclip) ; \
  _blits_->layer[_bi].functions[BLT_CLP_COMPLEX] = (_complexclip) ; \
  _blits_->layer[_bi].functions[BLT_CLP_NONE] = (_unclipped) ; \
MACRO_END

/* Set an individual blit slice. */
#define SET_BLIT_SLICE(_blits,_bi,_clp,_slice) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  HQASSERT(_blits_, "SET_BLIT_SLICE blit pointer NULL") ; \
  HQASSERT((_bi) >= BASE_BLIT_INDEX && (_bi) < BLIT_STACK_SIZE && \
           BLIT_STACK_SIZE <= HIGHEST_BIT_SET_LIMIT, \
           "SET_BLIT_SLICE index out of range") ;    \
  HQASSERT((_clp) == BLT_CLP_NONE || (_clp) == BLT_CLP_RECT || (_clp) == BLT_CLP_COMPLEX, \
           "SET_BLIT_SLICE clip out of range") ;                       \
  HQASSERT(DOING_BLITS(_blits_, (_bi)), \
           "SET_BLIT_SLICE blit layer not already set") ;               \
  _blits_->layer[_bi].functions[_clp] = (_slice) ;                      \
MACRO_END

/* N.B. Do allow clearing of the base blit, otherwise base span can't be
   set individually. */
#define CLEAR_BLITS(_blits,_bi) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  HQASSERT(_blits_, "CLEAR_BLITS blit pointer NULL") ; \
  HQASSERT((_bi) > BASE_BLIT_INDEX && (_bi) < BLIT_STACK_SIZE && \
           BLIT_STACK_SIZE <= HIGHEST_BIT_SET_LIMIT, "CLEAR_BLITS index out of range") ; \
  _blits_->layer[_bi].functions[BLT_CLP_RECT] = &invalid_slice ; \
  _blits_->layer[_bi].functions[BLT_CLP_COMPLEX] = &invalid_slice ; \
  _blits_->layer[_bi].functions[BLT_CLP_NONE] = &invalid_slice ; \
  _blits_->layer[_bi].data = NULL ; \
  _blits_->blit_mask &= ~(1 << (_bi)) ; \
  _blits_->blit_span = _blits_->blit_block = _blits_->blit_snfill = \
    _blits_->blit_char = _blits_->blit_img = \
    highest_bit_set_in_bits[_blits_->blit_mask] ; \
MACRO_END

#define SET_BLIT_DATA(_blits,_bi,_data) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  HQASSERT(_blits_, "SET_BLIT_DATA blit pointer NULL") ; \
  HQASSERT((_bi) >= BASE_BLIT_INDEX && (_bi) < BLIT_STACK_SIZE && \
           BLIT_STACK_SIZE <= HIGHEST_BIT_SET_LIMIT, "SET_BLIT_CHAR index out of range") ; \
  HQASSERT(DOING_BLITS(_blits_, (_bi)), \
           "Setting indexed blit which is not in use.") ; \
  _blits_->layer[_bi].data = (_data) ; \
MACRO_END

#define GET_BLIT_DATA(_blits,_bi,_data) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  HQASSERT(_blits_, "GET_BLIT_DATA blit pointer NULL") ; \
  HQASSERT((_bi) >= BASE_BLIT_INDEX && (_bi) < BLIT_STACK_SIZE && \
           BLIT_STACK_SIZE <= HIGHEST_BIT_SET_LIMIT, "GET_BLIT_DATA index out of range") ; \
  HQASSERT(DOING_BLITS(_blits_, (_bi)), \
           "Getting indexed blit which is not in use.") ; \
  (_data) = _blits_->layer[_bi].data ; \
MACRO_END

#define DO_SPAN(_rb, _y, _xs, _xe) MACRO_START \
  blit_chain_t *_blits_ = (_rb)->blits ; \
  uint32 _old_span_ = _blits_->blit_span ; \
  HQASSERT(_old_span_ < BLIT_STACK_SIZE, \
           "DO_SPAN blit out of range") ; \
  HQASSERT(_blits_->layer[_old_span_].functions[(_rb)->clipmode]->spanfn == next_span || \
           DOING_BLITS(_blits_, _old_span_), \
           "DO_SPAN blit mask not consistent") ; \
  _blits_->blit_span = highest_bit_set_in_bits[_blits_->blit_mask & BLIT_MASK_NEXT(_old_span_)] ; \
  (*(_blits_->layer[_old_span_].functions[(_rb)->clipmode]->spanfn))((_rb), (_y), (_xs), (_xe)) ; \
  _blits_->blit_span = _old_span_ ; \
MACRO_END

#define DO_BLOCK(_rb, _ys, _ye, _xs, _xe) MACRO_START \
  blit_chain_t *_blits_ = (_rb)->blits ; \
  uint32 _old_block_ = _blits_->blit_block ; \
  uint32 _old_span_ = _blits_->blit_span ; \
  HQASSERT(_old_span_ >= _old_block_, \
           "DO_BLOCK span, block blits out of order") ; \
  HQASSERT(_old_block_ < BLIT_STACK_SIZE, \
           "DO_BLOCK blits out of stack range") ; \
  HQASSERT(_blits_->layer[_old_block_].functions[(_rb)->clipmode]->blockfn == next_block || \
           DOING_BLITS(_blits_, _old_block_), \
           "DO_BLOCK blit mask not consistent") ; \
  _blits_->blit_span = _old_block_ ; \
  _blits_->blit_block = highest_bit_set_in_bits[_blits_->blit_mask & BLIT_MASK_NEXT(_old_block_)] ; \
  (*(_blits_->layer[_old_block_].functions[(_rb)->clipmode]->blockfn))((_rb), (_ys), (_ye), (_xs), (_xe)) ; \
  _blits_->blit_span = _old_span_ ; \
  _blits_->blit_block = _old_block_ ; \
MACRO_END

#define DO_SNFILL(_rb, _nfill, _ys, _ye) MACRO_START \
  blit_chain_t *_blits_ = (_rb)->blits ; \
  uint32 _old_snfill_ = _blits_->blit_snfill ; \
  uint32 _old_block_ = _blits_->blit_block ; \
  uint32 _old_span_ = _blits_->blit_span ; \
  HQASSERT(_old_span_ >= _old_block_ && _old_block_ >= _old_snfill_, \
           "DO_SNFILL span, block, snfill blits out of order") ; \
  HQASSERT(_old_snfill_ < BLIT_STACK_SIZE, \
           "DO_SNFILL blits out of stack range") ; \
  HQASSERT(_blits_->layer[_old_snfill_].functions[(_rb)->clipmode]->snfillfn == next_snfill || \
           DOING_BLITS(_blits_, _old_snfill_), \
           "DO_SNFILL blit mask not consistent") ; \
  _blits_->blit_block = _blits_->blit_span = _old_snfill_ ; \
  _blits_->blit_snfill = highest_bit_set_in_bits[_blits_->blit_mask & BLIT_MASK_NEXT(_old_snfill_)] ; \
  (*(_blits_->layer[_old_snfill_].functions[(_rb)->clipmode]->snfillfn))((_rb), (_nfill), (_ys), (_ye)) ; \
  _blits_->blit_span = _old_span_ ; \
  _blits_->blit_block = _old_block_ ; \
  _blits_->blit_snfill = _old_snfill_ ; \
MACRO_END

#define DO_CHAR(_rb, _form, _x, _y) MACRO_START \
  blit_chain_t *_blits_ = (_rb)->blits ; \
  uint32 _old_char_ = _blits_->blit_char ; \
  uint32 _old_snfill_ = _blits_->blit_snfill ; \
  uint32 _old_block_ = _blits_->blit_block ; \
  uint32 _old_span_ = _blits_->blit_span ; \
  HQASSERT(_old_span_ >= _old_block_ && _old_block_ >= _old_snfill_ && _old_snfill_ >= _old_char_, \
           "DO_CHAR span, block, snfill, char blits out of order") ; \
  HQASSERT(_old_char_ < BLIT_STACK_SIZE, \
           "DO_CHAR blits out of stack range") ; \
  HQASSERT(_blits_->layer[_old_char_].functions[(_rb)->clipmode]->charfn == next_char || \
           DOING_BLITS(_blits_, _old_char_), \
           "DO_CHAR blit mask not consistent") ; \
  _blits_->blit_block = _blits_->blit_span = _blits_->blit_snfill = _old_char_ ; \
  _blits_->blit_char = highest_bit_set_in_bits[_blits_->blit_mask & BLIT_MASK_NEXT(_old_char_)] ; \
  (*(_blits_->layer[_old_char_].functions[(_rb)->clipmode]->charfn))((_rb), (_form), (_x), (_y)) ; \
  _blits_->blit_span = _old_span_ ; \
  _blits_->blit_block = _old_block_ ; \
  _blits_->blit_snfill = _old_snfill_ ; \
  _blits_->blit_char = _old_char_ ; \
MACRO_END

#define DO_IMG(_rb, _params, _callback, _result) MACRO_START \
  blit_chain_t *_blits_ = (_rb)->blits ; \
  uint32 _old_img_ = _blits_->blit_img ; \
  uint32 _old_char_ = _blits_->blit_char ; \
  uint32 _old_snfill_ = _blits_->blit_snfill ; \
  uint32 _old_block_ = _blits_->blit_block ; \
  uint32 _old_span_ = _blits_->blit_span ; \
  HQASSERT(_old_span_ >= _old_block_ && _old_block_ >= _old_snfill_ && \
           _old_snfill_ >= _old_char_ && _old_char_ >= _old_img_, \
           "DO_IMG span, block, snfill, char, img blits out of order") ; \
  HQASSERT(_old_img_ < BLIT_STACK_SIZE, \
           "DO_IMG blits out of stack range") ; \
  HQASSERT(_blits_->layer[_old_img_].functions[(_rb)->clipmode]->imagefn == next_imgblt || \
           DOING_BLITS(_blits_, _old_img_), \
           "DO_IMG blit mask not consistent") ; \
  _blits_->blit_block = _blits_->blit_span = _blits_->blit_snfill = _blits_->blit_char = _old_img_ ; \
  _blits_->blit_img = highest_bit_set_in_bits[_blits_->blit_mask & BLIT_MASK_NEXT(_old_img_)] ; \
  (*(_blits_->layer[_old_img_].functions[(_rb)->clipmode]->imagefn))((_rb), (_params), (_callback), (_result)) ; \
  _blits_->blit_span = _old_span_ ; \
  _blits_->blit_block = _old_block_ ; \
  _blits_->blit_snfill = _old_snfill_ ; \
  _blits_->blit_char = _old_char_ ; \
  _blits_->blit_img = _old_img_ ; \
MACRO_END

/* Pattern replicators may want to ensure that the underlying span for a
   block gets called instead of the replicating span (if the block itself is
   replicated). The DO_BLOCK that called the original replicator will have
   set the top span to point at the replicating version, and the block to the
   underlying blits. These macros set the span to the underlying blit as
   well (and similarly the span and block in a replicated char). */
#define BLOCK_USE_NEXT_BLITS(_blits) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  _blits_->blit_span = _blits_->blit_block ; \
MACRO_END

#define SNFILL_USE_NEXT_BLITS(_blits) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  _blits_->blit_span = _blits_->blit_block = _blits_->blit_snfill ; \
MACRO_END

#define CHAR_USE_NEXT_BLITS(_blits) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  _blits_->blit_span = _blits_->blit_block = _blits_->blit_snfill = _blits_->blit_char ; \
MACRO_END

#define IMG_USE_NEXT_BLITS(_blits) MACRO_START \
  blit_chain_t *_blits_ = (_blits) ; \
  _blits_->blit_span = _blits_->blit_block = _blits_->blit_snfill = _blits_->blit_char = _blits_->blit_img ; \
MACRO_END


/*---------------------------------------------------------------------------*/

#ifdef ASSERT_BUILD
extern Bool assert_blts ;
#endif

#define BITBLT_ASSERT(_rb, _xs, _xe, _ys, _ye, _routine ) MACRO_START   \
  HQASSERT(RENDER_BLIT_CONSISTENT(_rb),                                 \
           _routine " : render context inconsistent" ) ;                \
  HQASSERT( _xs <= _xe , _routine " : xs,xe out of order" ) ;           \
  HQASSERT( _ys <= _ye , _routine " : ys,ye out of order" ) ;           \
  HQASSERT(assert_blts &&                                               \
           ((_rb)->p_ri->pattern_state == PATTERN_PAINTING ||           \
            (_xs + (_rb)->x_sep_position >= 0 &&                        \
             _xe + (_rb)->x_sep_position < theFormW(*(_rb)->outputform))), \
             _routine " : xs,xe span not contained inside form" ) ;     \
  HQASSERT(assert_blts &&                                               \
           ((_rb)->p_ri->pattern_state == PATTERN_PAINTING ||           \
            (bbox_contains_point(&(_rb)->p_ri->clip, _xs, _ys)          \
             && bbox_contains_point(&(_rb)->p_ri->clip, _xe, _ye))),    \
           _routine " : area not contained inside rectangular clip bounds") ; \
  HQASSERT(assert_blts &&                                               \
           ((_rb)->p_ri->pattern_state == PATTERN_PAINTING ||           \
            (_ys - theFormHOff(*(_rb)->outputform) - (_rb)->y_sep_position >= 0 && \
             _ye - theFormHOff(*(_rb)->outputform) - (_rb)->y_sep_position < theFormRH(*(_rb)->outputform))), \
           _routine " : ys,ye span not contained inside form" ) ;       \
  HQASSERT(assert_blts &&                                               \
           ((_rb)->p_ri->pattern_state == PATTERN_PAINTING ||           \
            (((_rb)->ylineaddr >= theFormA(*(_rb)->outputform) &&       \
              (_rb)->ylineaddr <  BLIT_ADDRESS(theFormA(*(_rb)->outputform), theFormS(*(_rb)->outputform))) || \
             (_rb)->ylineaddr == (_rb)->p_ri->p_rs->forms->maxbltbase)),\
           _routine " : ylineaddr not contained inside output form" ) ; \
MACRO_END

#define BITCLIP_ASSERT(_rb, _xs, _xe, _ys, _ye, _routine ) MACRO_START  \
  BITBLT_ASSERT(_rb, _xs, _xe, _ys, _ye, _routine ) ;                   \
  HQASSERT( (_rb)->ymaskaddr >= theFormA(*(_rb)->clipform) &&           \
            (_rb)->ymaskaddr <  BLIT_ADDRESS(theFormA(*(_rb)->clipform), theFormS(*(_rb)->clipform)), \
            _routine " : ymaskaddr not contained inside clip form" ) ;  \
MACRO_END

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
