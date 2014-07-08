/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!export:bitbltt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Simple and incomplete typedefs for bit blitting.
 */

#ifndef __BITBLTT_H__
#define __BITBLTT_H__

/* Do NOT include any other headers, or make this file depend on any other
   headers */

/* The typedef for blit_t and the definitions of BLIT_SHIFT_BYTES, BLIT_LOAD,
    BLIT_STORE, and BLIT_ROTATE_* can be modified to select between 32 and
    64-bit blits. Note that other sizes of blit words apart from 32 and 64
    bits can NOT be used without modification to bitblit routines. */

#ifdef PLATFORM_IS_64BIT /* 64-bit blits */

/** This is the basic type for bit-blit operations. Rasters, font cache forms
    and halftone cell forms are padded to multiples of this size inside the
    core, and for output to the skin. */
typedef uint64 blit_t ;

/** Amount to shift to convert byte positions or counts to blit_t and
    vice-versa (i.e. log_2(sizeof(blit_t))). */
#define BLIT_SHIFT_BYTES 3

#ifdef bitsgoright
#define BLIT_LOAD(ptr_) (blit_t)BYTE_LOAD64_UNSIGNED_BE(ptr_)
#define BLIT_STORE(ptr_, val_) BYTE_STORE64_BE((ptr_), (val_))
#else /* !bitsgoright */
#define BLIT_LOAD(ptr_) (blit_t)BYTE_LOAD64_UNSIGNED_LE(ptr_)
#define BLIT_STORE(ptr_, val_) BYTE_STORE64_LE((ptr_), (val_))
#endif /* !bitsgoright */

#define BLIT_ROTATE_LEFT_SAFE(to_, from_, shift_) \
  BIT_ROTATE64_LEFT_SAFE((to_), (from_), (shift_))
#define BLIT_ROTATE_RIGHT_SAFE(to_, from_, shift_) \
  BIT_ROTATE64_RIGHT_SAFE((to_), (from_), (shift_))

#define BlitSet(_p, _v, _c) HqMemSet64((uint64 *)(_p), (uint64)(_v), (_c))


#else /* 32-bit blits */

/** This is the basic type for bit-blit operations. Rasters, font cache forms
    and halftone cell forms are padded to multiples of this size inside the
    core, and for output to the skin. */
typedef uint32 blit_t ;

/** Amount to shift to convert byte positions or counts to blit_t and
    vice-versa (i.e. log_2(sizeof(blit_t))). */
#define BLIT_SHIFT_BYTES 2

#ifdef bitsgoright
#define BLIT_LOAD(ptr_) (blit_t)BYTE_LOAD32_UNSIGNED_BE(ptr_)
#define BLIT_STORE(ptr_, val_) BYTE_STORE32_BE((ptr_), (val_))
#else /* !bitsgoright */
#define BLIT_LOAD(ptr_) (blit_t)BYTE_LOAD32_UNSIGNED_LE(ptr_)
#define BLIT_STORE(ptr_, val_) BYTE_STORE32_LE((ptr_), (val_))
#endif /* !bitsgoright */

#define BLIT_ROTATE_LEFT_SAFE(to_, from_, shift_) \
  BIT_ROTATE32_LEFT_SAFE((to_), (from_), (shift_))
#define BLIT_ROTATE_RIGHT_SAFE(to_, from_, shift_) \
  BIT_ROTATE32_RIGHT_SAFE((to_), (from_), (shift_))

#define BlitSet(_p, _v, _c) HqMemSet32((uint32 *)(_p), (uint32)(_v), (_c))

#endif /* 32-bit blits */

/** Amount to shift to convert bit positions or counts to blit_t and
    vice-versa (i.e. log_2(sizeof(blit_t) * 8)). */
#define BLIT_SHIFT_BITS (BLIT_SHIFT_BYTES + 3)

/** Width of blit_t in bytes. This is calculated as a constant expression
    rather than being based on sizeof() so that it can be used in
    preprocessor tests. */
#define BLIT_WIDTH_BYTES (1 << BLIT_SHIFT_BYTES)

/** Width of blit_t in bits. This is calculated as a constant expression
    rather than being based on sizeof() so that it can be used in
    preprocessor tests. */
#define BLIT_WIDTH_BITS (1 << BLIT_SHIFT_BITS)

/** Mask suitable for extracting a byte index relative to a blit word from a
    byte offset, size, or pointer. */
#define BLIT_MASK_BYTES (BLIT_WIDTH_BYTES - 1)

/** Mask suitable for extracting a bit index relative to a blit word from a
    bit offset or size. */
#define BLIT_MASK_BITS (BLIT_WIDTH_BITS - 1)

#define DEPTH_SHIFT_LIMIT BLIT_SHIFT_BITS

#define BONES     ((blit_t)0xff)
#define ALLONES   (~(blit_t)0)

#define rotatepword( word , rotate , repeat ) MACRO_START \
 word = SHIFTLEFT( word , rotate ) ; \
 word |= SHIFTRIGHT( word , repeat ) ; \
MACRO_END

#define shiftpword( word , shift , repeat ) MACRO_START \
 word = SHIFTLEFT( word , shift ) | SHIFTRIGHT( word , repeat - shift ) ; \
MACRO_END

#define BLIT_SHIFT_MERGE(dest, src1, src2, shift) MACRO_START \
  (dest) = (SHIFTLEFT((src1), (shift)) |                      \
            SHIFTRIGHT((src2), BLIT_WIDTH_BITS - (shift))) ;  \
MACRO_END

#if defined(Can_Shift_32)
#define BLIT_SHIFT_MERGE_SAFE BLIT_SHIFT_MERGE
#else
#define BLIT_SHIFT_MERGE_SAFE(dest, src1, src2, shift) MACRO_START \
  if ( !(shift) ) {                                                \
    (dest) = (src1) ;                                              \
  } else {                                                         \
    BLIT_SHIFT_MERGE((dest), (src1), (src2), (shift)) ;            \
  }                                                                \
MACRO_END
#endif

#ifdef bitsgoright

#define SHIFT1  (BLIT_WIDTH_BITS - 8) /**< Right shift for byte with first bits. */
#define SHIFT2  (BLIT_WIDTH_BITS - 16)
#define SHIFT3  (BLIT_WIDTH_BITS - 24)
#define SHIFT4  (BLIT_WIDTH_BITS - 32)
#define SHIFT5  (BLIT_WIDTH_BITS - 40)
#define SHIFT6  (BLIT_WIDTH_BITS - 48)
#define SHIFT7  (BLIT_WIDTH_BITS - 56)
#define SHIFT8  (BLIT_WIDTH_BITS - 64)

#define AONE (~(ALLONES >> 1))
#define SHIFTRIGHT(x,s) ((blit_t)(x) >> (s))
#define SHIFTLEFT(x,s)  ((blit_t)(x) << (s))

#define shiftpwordall(word, shift) \
  BLIT_ROTATE_LEFT_SAFE(word, word, shift)

#else /* !bitsgoright */

#define SHIFT1  0
#define SHIFT2  8
#define SHIFT3  16
#define SHIFT4  24
#define SHIFT5  32
#define SHIFT6  40
#define SHIFT7  48
#define SHIFT8  56

#define AONE ((blit_t)1)
#define SHIFTRIGHT(x,s) ((blit_t)(x) << (s))
#define SHIFTLEFT(x,s)  ((blit_t)(x) >> (s))

#define shiftpwordall(word, shift) \
  BLIT_ROTATE_RIGHT_SAFE(word, word, shift)

#endif /* !bitsgoright */

/** Align a memory pointer down to the nearest blit_t boundary. */
#define BLIT_ALIGN_DOWN(p) \
  ((blit_t *)((uintptr_t)(p) & ~BLIT_MASK_BYTES))

/** Align a memory pointer up to the nearest blit_t boundary. */
#define BLIT_ALIGN_UP(p) \
  BLIT_ALIGN_DOWN((uint8 *)(p) + BLIT_MASK_BYTES)

/** Round a byte size up to the nearest multiple of the blit_t size. */
#define BLIT_ALIGN_SIZE(p) \
  (((int32)(p) + BLIT_MASK_BYTES) & ~BLIT_MASK_BYTES)

/** Derive a blit pointer address by adding a byte offset to an existing blit
    pointer. The caller should ensure proper alignment of the pointer, by
    making sure the offset is a multiple of BLIT_WIDTH_BYTES. Pixel offsets
    are commonly converted into blit word offsets using the BLIT_OFFSET
    macro. */
#define BLIT_ADDRESS(ptr_, offset_) ((blit_t *)((uint8 *)(ptr_) + (offset_)))

/** This idiom is used sufficiently often to be worth defining. This converts
    a bit index to a blit word offset, expressed as a number of bytes. This
    is usually used with BLIT_ADDRESS to find the blit word in which a
    particular pixel lies. The original version of this used (i >>
    BLIT_SHIFT_BITS) << BLIT_SHIFT_BYTES; this variant uses a shift and mask
    to avoid using a shift twice in succession. Some CPUs have fewer shift
    units than ALUs, while the mask can usually be done quickly in an ALU,
    the shift may need to schedule a shared barrel shifter. */

#define BLIT_OFFSET(i) (((uint32)(i) >> 3) & ~(uint32)BLIT_MASK_BYTES)

/** Calculate the number of bytes for the blit_t words required to contain a
    number of bits. */
#define FORM_LINE_BYTES(w) BLIT_OFFSET((w) + BLIT_WIDTH_BITS - 1)


enum {
  FORMTYPE_INVALID,
  FORMTYPE_BLANK,
  FORMTYPE_CACHEBITMAPTORLE,
  FORMTYPE_CACHEBITMAP,
  FORMTYPE_BANDRLEENCODED,
  FORMTYPE_BANDBITMAP,
  FORMTYPE_HALFTONEBITMAP,
  FORMTYPE_CHARCACHE,
  /* The RLE cache form types indicate how many nibbles the RLE is
     represented in. The values must form a contiguous range. */
  FORMTYPE_CACHERLE1,
  FORMTYPE_CACHERLE2,
  FORMTYPE_CACHERLE3,
  FORMTYPE_CACHERLE4,
  FORMTYPE_CACHERLE5,
  FORMTYPE_CACHERLE6,
  FORMTYPE_CACHERLE7,
  FORMTYPE_CACHERLE8
} ;

typedef struct FORM {
  int32 type;          /* MUST be first element - see CHARCACHE data type */
  blit_t *addr;        /* MUST be in same place in structure as HTFORM */
  int32 w, h, l, size, rh, hoff;
} FORM;

/* A form that includes an offset. */
typedef struct OFFSETFORM {
  dcoord x, y;
  FORM* form;
} OFFSETFORM;

#define theFormT(_f)     ((_f).type)  /* deprecated, use . instead. */
#define theFormW(_f)     ((_f).w)     /* deprecated, use . instead. */
#define theFormH(_f)     ((_f).h)     /* deprecated, use . instead. */
#define theFormL(_f)     ((_f).l)     /* deprecated, use . instead. */
#define theFormS(_f)     ((_f).size)  /* deprecated, use . instead. */
#define theFormRH(_f)    ((_f).rh)    /* deprecated, use . instead. */
#define theFormHOff(_f)  ((_f).hoff)  /* deprecated, use . instead. */
#define theFormA(_f)     ((_f).addr)  /* deprecated, use . instead. */


#define ALIGN_FORM_SIZE(size) \
  (SIZE_ALIGN_UP(size + BLIT_ALIGN_SIZE(sizeof(FORM)), MM_TEMP_POOL_ALIGN) - BLIT_ALIGN_SIZE(sizeof(FORM)))


typedef struct form_array_t form_array_t ;

/** Form arrays are used for pattern shape masks which may cover multiple bands.
    Breaking the area up into forms allows a mix of encoding types across forms. */
struct form_array_t {
  dbbox_t bbox ; /**< The area covered by all of the forms together in this form array. */
  int32 rh ; /**< The nominal height of each form (first and last may have fewer lines). */
  FORM *forms ; /**< An array of forms allows fast indexing by formarray_findform. */
  uint32 nforms ; /**< The number of forms in the array. */
} ;

struct render_blit_t ; /* from render.h */
struct NFILLOBJECT ; /* from displayt.h */

/* Span, block, char, and area functions */
typedef struct blit_chain_t blit_chain_t ;

/** \brief Forward definition for parameters passed from RIP to surface. */
typedef struct imgblt_params_t imgblt_params_t ;

/** \brief Callback to the RIP to pass sections of the image to the fill
    function. */
typedef Bool (imgblt_callback_fn)(struct render_blit_t *rb,
                                  imgblt_params_t *params) ;

typedef void (* BITBLT_FUNCTION)(struct render_blit_t *rb,
                                 dcoord y , register dcoord xs , dcoord xe);
typedef void (* BLKBLT_FUNCTION )(struct render_blit_t *rb,
                                  dcoord ys, dcoord ye, dcoord xs, dcoord xe);
typedef void (* SNFILL_FUNCTION)(struct render_blit_t *rb,
                                 struct NFILLOBJECT *nfill,
                                 dcoord ys, dcoord ye);
typedef void (* CHARBLT_FUNCTION)(struct render_blit_t *rb,
                                  FORM *formptr, dcoord x, dcoord y);
typedef void (* IMGBLT_FUNCTION)(struct render_blit_t *rb,
                                 imgblt_params_t *params,
                                 imgblt_callback_fn *callback,
                                 Bool *result);
typedef void (* AREAFILL_FUNCTION)(struct render_blit_t *rb,
                                   FORM *formptr);

/** A set of consistent blit functions. */
typedef struct blit_slice_t {
  BITBLT_FUNCTION spanfn ;
  BLKBLT_FUNCTION blockfn ;
  SNFILL_FUNCTION snfillfn ;
  CHARBLT_FUNCTION charfn ;
  IMGBLT_FUNCTION imagefn ;
} blit_slice_t ;

/** \brief Indices for clip optimisations.

    These indices are stored in the \c clipmode field of \c render_blit_t,
    and are used to index \c blitclip_slice_t structures to find the best
    blit function for the purpose. */
enum {
  BLT_CLP_RECT,    /**< Blit obeys \c render_info_t clip restrictions. */
  BLT_CLP_COMPLEX, /**< Blit is filtered through clipping form. */
  BLT_CLP_NONE,    /**< Blit is guaranteed to be inside all restrictions. */
  BLT_CLP_N
} ;

/** \brief Clip-optimised blit slice pointers.

    These pointers are indexed by the clip optimisation enumeration values
    (stored in the \c clipmode field of \c render_blit_t), to obtain the
    fastest blit function for the purpose. */
typedef blit_slice_t blitclip_slice_t[3] ;

/** \brief Indices for overprint optimisations.

    These indices are used to extract optimised overprint functions for no
    overprinting, and partial overprinting. Full overprinting never calls
    blit functions, so the iteration limit is the same as the all value. */
enum {
  BLT_OVP_NONE, /**< No overprinting. */
  BLT_OVP_SOME, /**< Overprinting some channels. */
  BLT_OVP_ALL,  /**< Overprinting all channels. */
  BLT_OVP_N = BLT_OVP_ALL
} ;

/* Log stripped */
#endif /* protection for multiple inclusion */
