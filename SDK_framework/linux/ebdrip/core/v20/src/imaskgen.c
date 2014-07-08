/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:imaskgen.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to separate the mask and image data from a scanline of data
 * received while processing masked images containing interleaved mask
 * and image data.
 *
 * This initial implementation is via a set of general macros which work
 * for any number of bits of mask and image data per sample. However,
 * when parameterised with a constant number of bits, the loop unrolled
 * version produces much faster code (although still not as fast as a
 * hand code). This enables fast versions to be implemented for the common
 * cases in a way which is unlikely to produce cut/paste type bugs.
 * 
 * Currently only 8 bit 3 colour and 8 bit 4 colour have been optimised
 * this way to test the principle.
 * 
 */


/*
 * Enable the option in HQNc-standard!export:hqassert.h to expand __FILENAME__
 * only once into a static variable rather than at every call to HQASSERT()
 * This avoids the internal compiler error discovered by paulc in Revision 1.1
 */

#define HQASSERT_LOCAL_FILE


#include "core.h"
#include "caching.h"

#include "imaskgen.h"

HQASSERT_FILE();

/* Fetch big-endian ordered data from a sample */

#define FETCH_32(sptr, sdata) MACRO_START \
  sdata = (sptr[0] << 24) | (sptr[1] << 16) | (sptr[2] << 8) | sptr[3]; \
  sptr += 4; \
MACRO_END

#define FETCH_24(sptr, sdata) MACRO_START \
  sdata = (sptr[0] << 16) | (sptr[1] << 8) | sptr[2]; \
  sptr += 3; \
MACRO_END

#define FETCH_16(sptr, sdata) MACRO_START \
  sdata = (sptr[0] << 8) | sptr[1]; \
  sptr += 2; \
MACRO_END

#define FETCH_8(sptr, sdata) MACRO_START \
  sdata = sptr[0]; \
  sptr += 1; \
MACRO_END

#define FETCH_DATA(ssize, sptr, sdata, sleft) MACRO_START \
  FETCH_##ssize(sptr, sdata); \
  sleft += ssize; \
MACRO_END

/* Store big-endian ordered data to a sample stream */


#define STORE_32(dptr, ddata) MACRO_START \
  dptr[0] = (uint8)(ddata >> 24); \
  dptr[1] = (uint8)(ddata >> 16); \
  dptr[2] = (uint8)(ddata >> 8); \
  dptr[3] = (uint8)ddata; \
  dptr += 4; \
MACRO_END

#define STORE_24(dptr, ddata) MACRO_START \
  dptr[0] = (uint8)(ddata >> 16); \
  dptr[1] = (uint8)(ddata >> 8); \
  dptr[2] = (uint8)ddata; \
  dptr += 3; \
MACRO_END

#define STORE_16(dptr, ddata) MACRO_START \
  dptr[0] = (uint8)(ddata >> 8); \
  dptr[1] = (uint8)ddata; \
  dptr += 2; \
MACRO_END

#define STORE_8(dptr, ddata) MACRO_START \
  dptr[0] = (uint8)ddata; \
  dptr += 1; \
MACRO_END

#define STORE_DATA(dsize, dptr, ddata, dleft) MACRO_START \
  STORE_##dsize(dptr, ddata); \
  ddata = 0; \
  dleft = dsize; \
MACRO_END

#define STORE_REM(dsize, dptr, ddata, dleft) MACRO_START \
  int32 _offset_; \
 \
  _offset_ = dsize; \
  while (dleft < _offset_) { \
    _offset_ -= 8; \
    *dptr++ = (uint8)(ddata >> _offset_); \
  } \
MACRO_END

/* Extract a consecutive group of bits from the current image sample from the */
/* source stream and append it to the destination stream */

#define EXTRACT_IMAGE_BITS(bits, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft) MACRO_START \
  int32 _len_; \
 \
  if (sleft == 0) \
    FETCH_DATA(ssize, sptr, sdata, sleft); \
  _len_ = bits; \
  if (sleft >= dleft) { \
    /* length is limited by space remaining in destination */ \
    if (_len_ > dleft) \
      _len_ = dleft; \
    sleft -= _len_; \
    dleft -= _len_; \
    HQASSERT(sleft - dleft >= 0, "EXTRACT_IMAGE_BITS: bad >>"); \
    /* optimise 32 bit merging (and avoid testing Can_Shift_32) */ \
    if (_len_ == 32) \
      ddata = sdata; \
    else \
      ddata |= ((sdata & (BITS_BELOW(_len_) << sleft)) >> (sleft - dleft)); \
    if (dleft == 0) \
      STORE_DATA(dsize, dptr, ddata, dleft); \
  } \
  else { \
    /* length is limited by space remaining in source */ \
    if (_len_ > sleft) \
      _len_ = sleft; \
    sleft -= _len_; \
    dleft -= _len_; \
    HQASSERT(dleft - sleft >= 0, "EXTRACT_IMAGE_BITS: bad <<"); \
    HQASSERT(_len_ < 32, "EXTRACT_IMAGE_BITS: _len_ >= 32"); \
    ddata |= ((sdata & (BITS_BELOW(_len_) << sleft)) << (dleft - sleft)); \
    HQASSERT(dleft > 0, "EXTRACT_IMAGE_BITS: bad >>"); \
    /* no need to test if destination byte is full... */ \
  } \
  bits -= _len_; \
MACRO_END

/* Extract all the image data from the next sample in the source stream */
/* and append it to the destination stream */
/* This is explicitly loop unrolled so that the compiler can generate */
/* efficient code for constant values of "bits". Note that it only supports */
/* groups image samples which can be extracted in at most four groups of */
/* consecutive bits within the source and destination stream fetches and */
/* stores */

#define EXTRACT_IMAGE_UNROLL(bits, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft) MACRO_START \
  int32 _rem_; \
 \
  _rem_ = (bits); \
  EXTRACT_IMAGE_BITS(_rem_, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft); \
  if (_rem_ > 0) { \
    EXTRACT_IMAGE_BITS(_rem_, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft); \
    if (_rem_ > 0) { \
      EXTRACT_IMAGE_BITS(_rem_, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft); \
      if (_rem_ > 0) { \
        EXTRACT_IMAGE_BITS(_rem_, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft); \
        HQASSERT(_rem_ == 0, "EXTRACT_IMAGE_UNROLL: not unrolled enough"); \
      } \
    } \
  } \
MACRO_END

/* Extract all the image data from the next sample in the source stream */
/* and append it to the destination stream. This version has no restriction */
/* on length but is not optimised much better by compilers for constant */
/* "bits" than for a variable number */

#define EXTRACT_IMAGE(bits, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft) MACRO_START \
  int32 _rem_; \
 \
  _rem_ = (bits); \
  do { \
    EXTRACT_IMAGE_BITS(_rem_, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft); \
  } while (_rem_ > 0); \
MACRO_END

/* Extract the group of mask bits from one sample and generate a single */
/* mask bit according to its value. Note this code assumes the fetching */
/* from the source has been choosen so that the mask does not span a */
/* boundary in the source stream. All source lengths work for 1, 2, 4 and */
/* 8 bit data but only 24 bit source lengths are guaranteed to work for all */
/* 12 bit images */

#define EXTRACT_MASK(bits, ssize, sptr, sdata, sleft, dsize, dptr, ddata, dleft) MACRO_START \
  if ((sleft -= bits) < 0) { \
    HQASSERT(sleft + bits == 0, "EXTRACT_MASK: mask crosses source boundary"); \
    FETCH_DATA(ssize, sptr, sdata, sleft); \
  } \
  -- dleft; \
  if ((sdata & (BITS_BELOW(bits) << sleft)) != 0) \
    ddata |= (1 << dleft); \
  if (dleft == 0) \
    STORE_DATA(dsize, dptr, ddata, dleft); \
MACRO_END

/* This allows a variable to be asserted as having a known, "constant" */
/* value at run-time and assigned with the same value at compile time */
/* to allow value-specific compiler optimisations. */

#define OPTIMISE_CONSTANT(var, value) MACRO_START \
  HQASSERT(var == value, "OPTIMISE_CONSTANT: bad value"); \
  var = value; \
MACRO_END

/* Setup initial variable values for the start of a split (and re-assign */
/* loop-invariant values to aid compiler optimisation) */

#define SPLIT_START(msize, isize, sdata, sleft, mdata, mleft, idata, ileft) MACRO_START \
  sdata = 0; \
  sleft = 0; \
  mdata = 0; \
  mleft = msize; \
  idata = 0; \
  ileft = isize; \
MACRO_END

#define LOOP_INVARIANT(msize, isize, sleft, mdata, mleft, idata, ileft) MACRO_START \
  HQASSERT(sleft == 0, "sleft != 0"); \
  HQASSERT(mdata == 0, "mdata != 0"); \
  HQASSERT(mleft == msize, "mleft != msize"); \
  HQASSERT(idata == 0, "idata != 0"); \
  HQASSERT(ileft == isize, "ileft != isize"); \
  SPLIT_START(msize, isize, sdata, sleft, mdata, mleft, idata, ileft); \
MACRO_END

/* Macro to split a scanline of image source data into separate mask and image samples */
#define SPLIT_STREAM(samples, mbits, ibits, ssize, sptr, msize, mptr, isize, iptr) MACRO_START \
  uint32 sdata, mdata, idata; \
  int32 sleft, mleft, ileft; \
 \
  SPLIT_START(msize, isize, sdata, sleft, mdata, mleft, idata, ileft); \
  while (samples > 0) { \
    -- samples; \
    EXTRACT_MASK(mbits, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE(ibits, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
  } \
 \
  STORE_REM(msize, mptr, mdata, mleft); \
  STORE_REM(isize, iptr, idata, ileft); \
MACRO_END

/* Macro to split a scanline of image source data into image and AlphaMask mask samples */
#define SPLIT_STREAM_ALPHA(samples, mbits, ibits, ssize, sptr, msize, mptr, isize, iptr) MACRO_START \
  uint32 sdata, mdata, idata; \
  int32 sleft, mleft, ileft; \
 \
  SPLIT_START(msize, isize, sdata, sleft, mdata, mleft, idata, ileft); \
  while (samples > 0) { \
    -- samples; \
    EXTRACT_IMAGE(mbits, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE(ibits, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
  } \
 \
  STORE_REM(msize, mptr, mdata, mleft); \
  STORE_REM(isize, iptr, idata, ileft); \
MACRO_END


/* Macro to split a scanline of image source data into separate image then mask samples.
   Same as SPLIT_STREAM_ALPHA but with alpha mask after color values (eg RGBA,RGBA,...).
 */
#define SPLIT_STREAM_ALPHA_LAST(samples, mbits, ibits, ssize, sptr, msize, mptr, isize, iptr) MACRO_START \
  uint32 sdata, mdata, idata; \
  int32 sleft, mleft, ileft; \
 \
  SPLIT_START(msize, isize, sdata, sleft, mdata, mleft, idata, ileft); \
  while (samples > 0) { \
    -- samples; \
    EXTRACT_IMAGE(ibits, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(mbits, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
  } \
 \
  STORE_REM(msize, mptr, mdata, mleft); \
  STORE_REM(isize, iptr, idata, ileft); \
MACRO_END

/* Constant parameterisable, loop-unrolled code to split a scanline of 
   image data into mask and image samples in so that compilers can 
   generate significantly faster code than the general case */

#define SPLIT_STREAM_FAST(MBITS, IBITS, samples, mbits, ibits, ssize, sptr, msize, mptr, isize, iptr) MACRO_START \
  uint32 sdata, mdata, idata; \
  int32 sleft, mleft, ileft; \
 \
  OPTIMISE_CONSTANT(mbits, MBITS); \
  OPTIMISE_CONSTANT(ibits, IBITS); \
 \
  SPLIT_START(msize, isize, sdata, sleft, mdata, mleft, idata, ileft); \
  while (samples >= 8) { \
    samples -= 8; \
    LOOP_INVARIANT(msize, isize, sleft, mdata, mleft, idata, ileft); \
    EXTRACT_MASK(MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_MASK(MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_MASK(MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_MASK(MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_MASK(MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_MASK(MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_MASK(MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_MASK(MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
  } \
  LOOP_INVARIANT(msize, isize, sleft, mdata, mleft, idata, ileft); \
  SPLIT_STREAM(samples, MBITS, IBITS, ssize, sptr, msize, mptr, isize, iptr); \
MACRO_END

/* As above, but for splitting an AlphaMask mask's samples from those of the image proper. */
#define SPLIT_STREAM_FAST_ALPHA(MBITS, IBITS, samples, mbits, ibits, ssize, sptr, msize, mptr, isize, iptr) MACRO_START \
  uint32 sdata, mdata, idata; \
  int32  sleft, mleft, ileft; \
                              \
  OPTIMISE_CONSTANT(mbits, MBITS); \
  OPTIMISE_CONSTANT(ibits, IBITS); \
                                   \
  SPLIT_START(msize, isize, sdata, sleft, mdata, mleft, idata, ileft); \
  while (samples >= 8) {                                               \
    samples -= 8;                                                      \
    LOOP_INVARIANT(msize, isize, sleft, mdata, mleft, idata, ileft);   \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
  } \
  LOOP_INVARIANT(msize, isize, sleft, mdata, mleft, idata, ileft); \
  SPLIT_STREAM_ALPHA(samples, MBITS, IBITS, ssize, sptr, msize, mptr, isize, iptr); \
MACRO_END


#define SPLIT_STREAM_FAST_ALPHA_LAST(MBITS, IBITS, samples, mbits, ibits, ssize, sptr, msize, mptr, isize, iptr) MACRO_START \
  uint32 sdata, mdata, idata; \
  int32 sleft, mleft, ileft; \
 \
  OPTIMISE_CONSTANT(mbits, MBITS); \
  OPTIMISE_CONSTANT(ibits, IBITS); \
 \
  SPLIT_START(msize, isize, sdata, sleft, mdata, mleft, idata, ileft); \
  while (samples >= 8) { \
    samples -= 8;                                                      \
    LOOP_INVARIANT(msize, isize, sleft, mdata, mleft, idata, ileft);   \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
    EXTRACT_IMAGE_UNROLL(IBITS, ssize, sptr, sdata, sleft, isize, iptr, idata, ileft); \
    EXTRACT_IMAGE(       MBITS, ssize, sptr, sdata, sleft, msize, mptr, mdata, mleft); \
  } \
  LOOP_INVARIANT(msize, isize, sleft, mdata, mleft, idata, ileft); \
  SPLIT_STREAM_ALPHA_LAST(samples, MBITS, IBITS, ssize, sptr, msize, mptr, isize, iptr); \
MACRO_END


/* Optimised for 8-bit mask and 32-bit (e.g. CMYK) image data */

static void imask_split_8_32( uint8 *sptr, int32 samples,
                              uint8 *mptr, int32 mbits,
                              uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM_FAST(8, 32, samples, mbits, ibits, 32, sptr, 8, mptr, 32, iptr);
  return;
}

static void AlphaMask_split_8_32( uint8 *sptr, int32 samples,
                                  uint8 *mptr, int32 mbits,
                                  uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM_FAST_ALPHA(8, 32, samples, mbits, ibits, 32, sptr, 8, mptr, 32, iptr);
  return;
}

static void AlphaMaskLast_split_8_32( uint8 *sptr, int32 samples,
                                      uint8 *mptr, int32 mbits,
                                      uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM_FAST_ALPHA_LAST(8, 32, samples, mbits, ibits, 32, sptr, 8, mptr, 32, iptr);
  return;
}

/* Optimised for 8-bit mask and 24-bit (e.g. RGB) image data */

static void imask_split_8_24( uint8 *sptr, int32 samples,
                              uint8 *mptr, int32 mbits,
                              uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM_FAST(8, 24, samples, mbits, ibits, 32, sptr, 8, mptr, 32, iptr);
  return;
}

static void AlphaMask_split_8_24( uint8 *sptr, int32 samples,
                                  uint8 *mptr, int32 mbits,
                                  uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM_FAST_ALPHA(8, 24, samples, mbits, ibits, 32, sptr, 8, mptr, 32, iptr);
  return;
}

static void AlphaMaskLast_split_8_24( uint8 *sptr, int32 samples,
                                      uint8 *mptr, int32 mbits,
                                      uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM_FAST_ALPHA_LAST(8, 24, samples, mbits, ibits, 32, sptr, 8, mptr, 32, iptr);
  return;
}

#define SPLIT_LAST_16_16(sptr, iptr, mptr) MACRO_START \
  iptr[0] = sptr[0]; \
  mptr[0] = sptr[1]; \
  sptr += 2; \
  iptr += 1; \
  mptr += 1; \
MACRO_END

#define SPLIT_LAST_16_48(sptr, iptr, mptr) MACRO_START \
  iptr[0] = sptr[0]; \
  iptr[1] = sptr[1]; \
  iptr[2] = sptr[2]; \
  mptr[0] = sptr[3]; \
  sptr += 4; \
  iptr += 3; \
  mptr += 1; \
MACRO_END

#define SPLIT_LAST_16_64(sptr, iptr, mptr) MACRO_START \
  iptr[0] = sptr[0]; \
  iptr[1] = sptr[1]; \
  iptr[2] = sptr[2]; \
  iptr[3] = sptr[3]; \
  mptr[0] = sptr[4]; \
  sptr += 5; \
  iptr += 4; \
  mptr += 1; \
MACRO_END

#define SPLIT_STREAM_16(sptr8, samples, iptr8, mptr8, IBITS, ibits) MACRO_START \
  uint16* sptr = (uint16*)sptr8; \
  uint16* iptr = (uint16*)iptr8; \
  uint16* mptr = (uint16*)mptr8; \
 \
  OPTIMISE_CONSTANT(ibits, IBITS); \
 \
  while (samples >= 8) { \
    samples -= 8; \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
  } \
  while (samples >= 1) { \
    samples -= 1; \
    SPLIT_LAST_16_##IBITS(sptr, iptr, mptr); \
  } \
MACRO_END

/* Optimised for 16-bit mask and 16-bit (gray), 48-bit (RGB) or 64-bit (CMYK) image data */

static void AlphaMaskLast_split_16_16( uint8 *sptr8, int32 samples,
                                       uint8 *mptr8, int32 mbits,
                                       uint8 *iptr8, int32 ibits )
{
  UNUSED_PARAM(int32, mbits);
  SPLIT_STREAM_16(sptr8, samples, iptr8, mptr8, 16, ibits);
}

static void AlphaMaskLast_split_16_48( uint8 *sptr8, int32 samples,
                                       uint8 *mptr8, int32 mbits,
                                       uint8 *iptr8, int32 ibits )
{
  UNUSED_PARAM(int32, mbits);
  SPLIT_STREAM_16(sptr8, samples, iptr8, mptr8, 48, ibits);
}

static void AlphaMaskLast_split_16_64( uint8 *sptr8, int32 samples,
                                       uint8 *mptr8, int32 mbits,
                                       uint8 *iptr8, int32 ibits )
{
  UNUSED_PARAM(int32, mbits);
  SPLIT_STREAM_16(sptr8, samples, iptr8, mptr8, 64, ibits);
}


/* General "mbits" mask and "ibits" image data (excluding 16-bit samples) */

static void imask_split_any( uint8 *sptr, int32 samples,
                             uint8 *mptr, int32 mbits,
                             uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM(samples, mbits, ibits, 32, sptr, 32, mptr, 32, iptr);
  return;
}

static void AlphaMask_split_any( uint8 *sptr, int32 samples,
                                 uint8 *mptr, int32 mbits,
                                 uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM_ALPHA(samples, mbits, ibits, 32, sptr, 32, mptr, 32, iptr);
  return;
}

static void AlphaMaskLast_split_any( uint8 *sptr, int32 samples,
                                     uint8 *mptr, int32 mbits,
                                     uint8 *iptr, int32 ibits )
{
  SPLIT_STREAM_ALPHA_LAST(samples, mbits, ibits, 32, sptr, 32, mptr, 32, iptr);
  return;
}

/* ------------------------------------------------------------------------- */

/* imask_split_samples:
   Used for Type 3 Masked Images.  Mask channel is written out as
   1-bit, regardless of original mask bit depth.
 */
void imask_split_samples( uint8 *sptr, int32 samples,
                          uint8 *mptr, int32 mbits,
                          uint8 *iptr, int32 ibits )
{
  void (*p_imask_split)( uint8 *sptr, int32 samples,
			 uint8 *mptr, int32 mbits,
			 uint8 *iptr, int32 ibits );

  p_imask_split = imask_split_any;

  switch (mbits) {
  case 1:
  case 2:
  case 4:
    break;

  case 8:
    if (ibits == 24)
      p_imask_split = imask_split_8_24;
    else if (ibits == 32)
      p_imask_split = imask_split_8_32;
    break;

  case 12:
    break;

  default:
    HQFAIL("imask_split_samples: bad mbits");
    break;
  }

  (*p_imask_split)( sptr, samples, mptr, mbits, iptr, ibits );

  return;
}

#ifdef highbytefirst
#define BIG_2_PLATFORM(a) (a)
#else
#define BIG_2_PLATFORM(a) ((uint16)(((a & 0xffu)<<8)|(a>>8)))
#endif
#define PLATFORM_2_BIG(a) BIG_2_PLATFORM(a)

#define CLAMP_AND_CAST_UNSIGNED_TO_UINT16(a)  ((uint16)((a>0xffffu)? 0xffffu: a))
#define CLAMP_AND_CAST_UNSIGNED_TO_UINT8(a)  ((uint8)((a>0xffu)? 0xffu: a))

/* alpha_split_samples:
   Used for separating alpha channel (up to 16-bit alpha) in PNG and TIFF
   images.
   - alpha_last indicates alpha occurs after the color values (RGBA,RGBA,...).
   - premultiplied indicates color values are premultiplied by the alpha.
 */
void alpha_split_samples( Bool alpha_last, Bool premultiplied,
                          uint8 *sptr, int32 samples,
                          uint8 *mptr, int32 mbits,
                          uint8 *iptr, int32 ibits )
{
  void (*p_amask_split)( uint8 *sptr, int32 samples,
			 uint8 *mptr, int32 mbits,
			 uint8 *iptr, int32 ibits );

  p_amask_split = (alpha_last ? AlphaMaskLast_split_any : AlphaMask_split_any);

  switch (mbits) {
  case 1:
  case 2:
  case 4:
    break;

  case 8:
    if (ibits == 24)
      p_amask_split = (alpha_last ? AlphaMaskLast_split_8_24 : AlphaMask_split_8_24);
    else if (ibits == 32)
      p_amask_split = (alpha_last ? AlphaMaskLast_split_8_32 : AlphaMask_split_8_32);
    break;

  case 12:
    break;

  case 16:
    /* Currently, the methods for splitting require alpha to follow each set
       of color values.  */
    HQASSERT(alpha_last, "alpha_split_samples: 16-bit splitting expects to be last");
    switch (ibits) {
    case 16:
      p_amask_split = AlphaMaskLast_split_16_16;
      break;
    case 48:
      p_amask_split = AlphaMaskLast_split_16_48;
      break;
    case 64:
      p_amask_split = AlphaMaskLast_split_16_64;
      break;
    default:
      HQFAIL("alpha_split_samples: can only support 1, 3 or 4 color components");
      break;
    }
    break;

  default:
    HQFAIL("alpha_split_samples: bad mbits");
    break;
  }

  (*p_amask_split)( sptr, samples, mptr, mbits, iptr, ibits );

  if (premultiplied) {
    /* The compositing code expects color values NOT to be premultiplied.  If color
       values are premultiplied, divide out the alpha now.  Currently only support
       8 bit associated alpha tiff images (this is all PhotoShop creates). */
    if (mbits == 8){
      uint32 alpha;
#define ONE (uint32)0xffu
    
      if (ibits == 24) {
        while (samples--) {
          alpha = *mptr++;
          if (!alpha) {
            *iptr++ = 0u;
            *iptr++ = 0u; 
            *iptr = 0u;
          } else {
            *iptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT8((ONE * *iptr)/alpha);
            iptr++;
            *iptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT8((ONE * *iptr)/alpha);
            iptr++;
            *iptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT8((ONE * *iptr)/alpha);
          }
          iptr++;
        }
      } else if (ibits == 32) {
        while (samples--) {
          alpha = *mptr++;
          if (!alpha) {
            *iptr++ = 0u;
            *iptr++ = 0u;
            *iptr++ = 0u;
            *iptr = 0u;
          } else {
            *iptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT8((ONE * *iptr)/alpha);
            iptr++;
            *iptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT8((ONE * *iptr)/alpha);
            iptr++;
            *iptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT8((ONE * *iptr)/alpha);
            iptr++;
            *iptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT8((ONE * *iptr)/alpha);
          }
          iptr++;
        }
      } else {
        /* slower */
        uint32 chans = ibits / mbits;
        uint32 i;
        while (samples--) {
          alpha = *mptr++;
          i = chans;
          if (!alpha) {
            while(i--)
              *iptr++ = 0u;
          } else {
            while (i--) {
              *iptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT8((ONE * *iptr)/alpha);
              iptr++;
            }
          }
        }
      }
    } else if (mbits == 16 ){
      uint32 alpha;
      uint16 * m16ptr = (uint16 *)mptr;
      uint16 * i16ptr = (uint16 *)iptr;
#undef ONE
#define ONE (uint32)0xffffu
    
      if (ibits == 48) {
        while (samples--) {
          alpha = BIG_2_PLATFORM(*m16ptr);
          m16ptr++;
          if (!alpha) {
            *i16ptr++ = 0u;
            *i16ptr++ = 0u; 
            *i16ptr = 0u;
          } else {
            *i16ptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT16((ONE * BIG_2_PLATFORM(*i16ptr))/alpha);
            *i16ptr = PLATFORM_2_BIG(*i16ptr); 
            i16ptr++;
            *i16ptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT16((ONE * BIG_2_PLATFORM(*i16ptr))/alpha);          
            *i16ptr = PLATFORM_2_BIG(*i16ptr); 
            i16ptr++;
            *i16ptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT16((ONE * BIG_2_PLATFORM(*i16ptr))/alpha);
            *i16ptr = PLATFORM_2_BIG(*i16ptr); 
         }
         i16ptr++;
        }
      } else if (ibits == 64) {
        while (samples--) {
          alpha = BIG_2_PLATFORM(*m16ptr);
          m16ptr++;
          if (!alpha) {
            *i16ptr++ = 0u;
            *i16ptr++ = 0u;
            *i16ptr++ = 0u;
            *i16ptr = 0u;
          } else {
            *i16ptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT16((ONE * BIG_2_PLATFORM(*i16ptr))/alpha);
            *i16ptr = PLATFORM_2_BIG(*i16ptr); 
            i16ptr++;
            *i16ptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT16((ONE * BIG_2_PLATFORM(*i16ptr))/alpha);
            *i16ptr = PLATFORM_2_BIG(*i16ptr); 
            i16ptr++;
            *i16ptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT16((ONE * BIG_2_PLATFORM(*i16ptr))/alpha);
            *i16ptr = PLATFORM_2_BIG(*i16ptr); 
            i16ptr++;
            *i16ptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT16((ONE * BIG_2_PLATFORM(*i16ptr))/alpha);
            *i16ptr = PLATFORM_2_BIG(*i16ptr); 
         }
          i16ptr++;
        }
      } else {
        /* slower */
        uint32 chans = ibits / mbits;
        uint32 i;
        while (samples--) {
          alpha = BIG_2_PLATFORM(*m16ptr);
          m16ptr++;
          i = chans;
          if (!alpha) {
            while(i--)
              *i16ptr++ = 0u;
          } else {
            while (i--) {
              *i16ptr = CLAMP_AND_CAST_UNSIGNED_TO_UINT16((ONE * BIG_2_PLATFORM(*i16ptr))/alpha);
              *i16ptr = PLATFORM_2_BIG(*i16ptr); 
              i16ptr++;
            }
          }
        }
      }
    }
    else {
      HQFAIL("alpha_split_samples can only handle 8 & 16-bit premultiplied color values");
    }
  }

  return;
}

/* -------------------------------------------------------------------------- */

/* EOF */

/* Log stripped */
