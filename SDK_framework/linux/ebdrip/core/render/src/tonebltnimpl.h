/** \file
 * \ingroup toneblit
 *
 * $HopeName: CORErender!src:tonebltnimpl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains a parameterised implementation of the tone blit functions
 * for generic contone output.
 *
 * On inclusion, the macro PACK_WIDTH_BITS should be #defined to the width (8
 * or 16) which will be used to specialise the packing unit of these
 * routines.
 *
 * The generic contone output surfaces are unoptimised catch-alls for tone
 * blitting. They can handle band/frame/pixel interleaved, any number of
 * channels and bit depths. Max/min blit will be very slow, but normal
 * knockouts and overprints shouldn't be too bad. Optimised override surfaces
 * are recommended for commonly used cases.
 *
 * This file is included multiple times, so should NOT have a guard around
 * it.
 */

/** \brief Stringify a token.

    We need a two-level macro expansion to ensure that the argument is fully
    macro-expanded before stringification (C99, 6.10.3.1). */
#define STRINGIFY(x_) STRINGIFY2(x_)
#define STRINGIFY2(x_) #x_

/** \brief Add the packing unit width to the end of a token name

    We need a three-level macro expansion to guarantee expansion of the
    preprocessing tokens we're concatenating. SUFFIX() introduces the
    PACK_WIDTH_BITS qualifier on names, but this is a macro which we want
    expanded itself. The SUFFIX2 level guarantees that its arguments are fully
    expanded (C99, 6.10.3.1) before expanding SUFFIX3, which performs the
    token concatenation. */
#define SUFFIX(x_) SUFFIX2(x_,PACK_WIDTH_BITS)
#define SUFFIX2(x_,y_) SUFFIX3(x_,y_)
#define SUFFIX3(x_,y_) x_ ## y_

/** The packing unit is an unsigned int of the appropriate width. */
#define pack_t SUFFIX(uint)

/** All ones, in a pack_t unit. Normal promotions should zero-extend this,
    because pack_t is an unsigned type. */
#define PACK_ONES ((pack_t)-1)

/** Mask to extract a bit index relative to a pack_t offset or size. */
#define PACK_MASK_BITS   (PACK_WIDTH_BITS - 1)

/** Number of bits to offset packed value by. The high bit of the value will
    be normalised to bit position (NORMALISE_BITS + PACKED_WIDTH_BITS - 1).
    This value is one less than the maximum channel size in bits
    (channel_output_t). */
#define NORMALISE_BITS 15

/* ---------------------------------------------------------------------- */

/** Optimised slices to knockout/overprint this pack depth. */
static blitclip_slice_t SUFFIX(slicesN)[2] = {
  BLITCLIP_SLICE_INIT /*knockout*/, BLITCLIP_SLICE_INIT /*overprint*/,
} ;

/* ---------------------------------------------------------------------- */

static void SUFFIX(bitfillNoverprint)(render_blit_t *rb, dcoord y,
                                      register dcoord xs, register dcoord xe) ;

static inline int32 SUFFIX(unpack_bits)(pack_t *addr, int32 bits, int32 mask,
                                        int32 s0, int32 s1, int32 s2)
{
  register int32 value ;

  HQASSERT(mask >= 0, "Mask overflowed to top bit") ;
  HQASSERT(NORMALISE_BITS - bits >= 0 && NORMALISE_BITS - bits <= 16,
           "Invalid number of bits") ;
  HQASSERT(s0 < NORMALISE_BITS + 16, "Shift is too large") ;
  HQASSERT(s1 == s0 - PACK_WIDTH_BITS, "Shift out of phase") ;
  HQASSERT(s2 == s1 - PACK_WIDTH_BITS, "Shift out of phase") ;

  value = addr[0] << s0 ;
  if ( bits < s1 ) {
    value |= addr[1] << s1 ;
    /* In the 16-bit case, this condition can never be true. If the compiler
       is smart enough, it might optimise it out. It may need some assistance
       to do that (e.g. __assume()). */
    if ( bits < s2 ) {
      value |= addr[2] << s2 ;
    }
  }

  return value & mask ;
}

static inline void SUFFIX(pack_bits)(int32 value, pack_t *addr,
                                     int32 bits, int32 mask,
                                     int32 s0, int32 s1, int32 s2)
{
  HQASSERT(mask >= 0, "Mask overflowed to top bit") ;
  HQASSERT((value & ~mask) == 0, "Value is not masked correctly") ;
  HQASSERT(NORMALISE_BITS - bits >= 0 && NORMALISE_BITS - bits <= 16,
           "Invalid number of bits") ;
  HQASSERT(s0 < NORMALISE_BITS + 16, "Shift is too large") ;
  HQASSERT(s1 == s0 - PACK_WIDTH_BITS, "Shift out of phase") ;
  HQASSERT(s2 == s1 - PACK_WIDTH_BITS, "Shift out of phase") ;

  addr[0] = (pack_t)((addr[0] & ~(mask >> s0)) | (value >> s0)) ;
  if ( bits < s1 ) {
    addr[1] = (pack_t)((addr[1] & ~(mask >> s1)) | (value >> s1)) ;
    /* In the 16-bit case, this condition can never be true. If the compiler
       is smart enough, it might optimise it out. It may need some assistance
       to do that (e.g. __assume()). */
    if ( bits < s2 ) {
      addr[2] = (pack_t)((addr[2] & ~(mask >> s2)) | (value >> s2)) ;
    }
  }
}

/* ---------------------------------------------------------------------- */
static void SUFFIX(bitfillNmax)(render_blit_t *rb, dcoord y,
                                register dcoord xs, register dcoord xe)
{
  channel_index_t index ;
  blit_color_t *color = rb->color ;
  const blit_colormap_t *map = color->map ;
  int32 packed_bits ;
  pack_t *baseaddr ;

  BITBLT_ASSERT(rb, xs, xe, y, y, STRINGIFY(SUFFIX(bitfillNmax))) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(rb->maxmode != BLT_MAX_NONE, "Should be maxblitting") ;

  /* Implement in two passes; blit the non max-blitted channels faster using
     bitfillNoverprint, and then the max-blitted channels slowly. */
  {
    /* cast away constness for temporary mask replacement hack: */
    render_state_t *p_rs = (render_state_t *)rb->p_ri->p_rs ;
    blit_packed_t *opmask = p_rs->cs.overprint_mask ;
#ifdef ASSERT_BUILD
    uint8 maxmode = rb->maxmode ;
    uint8 opmode = rb->opmode ;
    rb->maxmode = BLT_MAX_NONE ;
    rb->opmode = BLT_OVP_SOME ;
#endif
    p_rs->cs.overprint_mask = p_rs->cs.maxblit_mask ;
    SUFFIX(bitfillNoverprint)(rb, y, xs, xe) ;
    p_rs->cs.overprint_mask = opmask ;
#ifdef ASSERT_BUILD
    rb->maxmode = maxmode ;
    rb->opmode = opmode ;
#endif
  }

  packed_bits = CAST_SIZET_TO_INT32(map->packed_bits) ;

  xe = xe - xs + 1 ; /* total pixels to fill */

  xs += rb->x_sep_position ;
  xs *= packed_bits ;

  /* We have to treat the data as a byte stream, because the tone data is
     packed high bit first. In the case of max/min blit, the absolute value
     of the extracted bit pattern matters. If the packed channel crosses a
     byte boundary, we need to make sure the first byte in the stream is the
     higher byte of the result. */
  baseaddr = (pack_t *)rb->ylineaddr + xs / PACK_WIDTH_BITS ;
  xs &= PACK_MASK_BITS ;

  for ( index = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & blit_channel_maxblit) != 0 ) {
      /* Extract the packed channel current value, and max it with all
         locations in the raster. We keep the current value and the extracted
         value normalised to have their top bit in bit 30 (for 16-bit pack_t)
         or bit 22 (8-bit pack_t), so we don't have to test for right shifts.
         The max/min operations work correctly on these normalised values,
         because the maximum difference is MAXINT32. */
      int32 offset = CAST_UNSIGNED_TO_INT32(map->channel[index].bit_offset) ;
      int32 bits = CAST_UNSIGNED_TO_INT32(map->channel[index].bit_size) ;
      /* Create mask normalised to high bit 30 or 22 set */
      int32 mask = ((~0xffff >> bits) & 0xffff) << (PACK_WIDTH_BITS - 1) ;
      int32 pcv ; /* packed color value */
      register pack_t *addr = (pack_t *)&color->packed.channels.bytes[0] + offset / PACK_WIDTH_BITS ;
      int32 shift = offset & PACK_MASK_BITS ;
      int32 s0 = shift + NORMALISE_BITS ;
      int32 s1 = s0 - PACK_WIDTH_BITS ;
      int32 s2 = s1 - PACK_WIDTH_BITS ;

      HQASSERT((color->state[index] & blit_channel_present) != 0,
               "Maxblitting but the channel is not present") ;

      /** \todo ajcd 2010-04-06: What about type and alpha channels? */
      HQASSERT(map->channel[index].type == channel_is_color,
               "Maxblitting type or alpha channel") ;

      /* We always compare bits against a negated shift offset, so pre-adjust
         bits instead. */
      bits = NORMALISE_BITS - bits ;

      /* Extract and normalise the packed color to bit 30 or 22. */
      pcv = SUFFIX(unpack_bits)(addr, bits, mask, s0, s1, s2) ;

      /* If already min value, we'll never increase the channel. */
      if ( pcv != 0 ) {
        dcoord cxe = xe ; /* local pixel count */

        /* Now use the address and shift for the output data. */
        addr = baseaddr + (offset + xs) / PACK_WIDTH_BITS ;
        shift = (offset + xs) & PACK_MASK_BITS ;

        do {
          register int32 mcv ; /* max color value */

          s0 = shift + NORMALISE_BITS ;
          s1 = s0 - PACK_WIDTH_BITS ;
          s2 = s1 - PACK_WIDTH_BITS ;

          mcv = SUFFIX(unpack_bits)(addr, bits, mask, s0, s1, s2) ;
          INLINE_MAX32(mcv, mcv, pcv) ;
          SUFFIX(pack_bits)(mcv, addr, bits, mask, s0, s1, s2) ;

          shift += packed_bits ;
          addr += shift / PACK_WIDTH_BITS ;
          shift &= PACK_MASK_BITS ;
        } while ( --cxe > 0 ) ;
      }
    }
  }
}

static void SUFFIX(bitclipNmax)(render_blit_t *rb, dcoord y,
                                dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, STRINGIFY(SUFFIX(bitclipNmax))) ;

  bitclipn(rb, y , xs , xe , SUFFIX(bitfillNmax)) ;
}

/* ---------------------------------------------------------------------- */
static void SUFFIX(bitfillNmin)(render_blit_t *rb, dcoord y,
                                register dcoord xs, register dcoord xe)
{
  channel_index_t index ;
  blit_color_t *color = rb->color ;
  const blit_colormap_t *map = color->map ;
  int32 packed_bits ;
  pack_t *baseaddr ;

  BITBLT_ASSERT(rb, xs, xe, y, y, STRINGIFY(SUFFIX(bitfillNmin))) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(rb->maxmode != BLT_MAX_NONE, "Should be maxblitting") ;

  /* Implement in two passes; blit the non max-blitted channels faster using
     bitfillNoverprint, and then the max-blitted channels slowly. */
  {
    /* cast away constness for temporary mask replacement hack: */
    render_state_t *p_rs = (render_state_t *)rb->p_ri->p_rs ;
    blit_packed_t *opmask = p_rs->cs.overprint_mask ;
#ifdef ASSERT_BUILD
    uint8 maxmode = rb->maxmode ;
    uint8 opmode = rb->opmode ;
    rb->maxmode = BLT_MAX_NONE ;
    rb->opmode = BLT_OVP_SOME ;
#endif
    p_rs->cs.overprint_mask = p_rs->cs.maxblit_mask ;
    SUFFIX(bitfillNoverprint)(rb, y, xs, xe) ;
    p_rs->cs.overprint_mask = opmask ;
#ifdef ASSERT_BUILD
    rb->maxmode = maxmode ;
    rb->opmode = opmode ;
#endif
  }

  packed_bits = CAST_SIZET_TO_INT32(map->packed_bits) ;

  xe = xe - xs + 1 ; /* total pixels to fill */

  xs += rb->x_sep_position ;
  xs *= packed_bits ;

  /* We have to treat the data as a byte stream, because the tone data is
     packed high bit first. In the case of max/min blit, the absolute value
     of the extracted bit pattern matters. If the packed channel crosses a
     byte boundary, we need to make sure the first byte in the stream is the
     higher byte of the result. */
  baseaddr = (pack_t *)rb->ylineaddr + xs / PACK_WIDTH_BITS ;
  xs &= PACK_MASK_BITS ;

  for ( index = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & blit_channel_maxblit) != 0 ) {
      /* Extract the packed channel current value, and max it with all
         locations in the raster. We keep the current value and the extracted
         value normalised to have their top bit in bit 30 (for 16-bit pack_t)
         or bit 22 (8-bit pack_t), so we don't have to test for right shifts.
         The max/min operations work correctly on these normalised values,
         because the maximum difference is MAXINT32. */
      int32 offset = CAST_UNSIGNED_TO_INT32(map->channel[index].bit_offset) ;
      int32 bits = CAST_UNSIGNED_TO_INT32(map->channel[index].bit_size) ;
      /* Create mask normalised to high bit 30 or 22 set */
      int32 mask = ((~0xffff >> bits) & 0xffff) << (PACK_WIDTH_BITS - 1) ;
      int32 pcv ; /* packed color value */
      register pack_t *addr = (pack_t *)&color->packed.channels.bytes[0] + offset / PACK_WIDTH_BITS ;
      int32 shift = offset & PACK_MASK_BITS ;
      int32 s0 = shift + NORMALISE_BITS ;
      int32 s1 = s0 - PACK_WIDTH_BITS ;
      int32 s2 = s1 - PACK_WIDTH_BITS ;

      HQASSERT((color->state[index] & blit_channel_present) != 0,
               "Maxblitting but the channel is not present") ;

      /** \todo ajcd 2010-04-06: What about type and alpha channels? */
      HQASSERT(map->channel[index].type == channel_is_color,
               "Maxblitting type or alpha channel") ;

      /* We always compare bits against a negated shift offset, so pre-adjust
         bits instead. */
      bits = NORMALISE_BITS - bits ;

      /* Extract and normalise the packed color to bit 30 or 22. */
      pcv = SUFFIX(unpack_bits)(addr, bits, mask, s0, s1, s2) ;

      /* If already max value, we'll never reduce the channel. */
      if ( pcv != mask ) {
        dcoord cxe = xe ; /* local pixel count */

        /* Now use the address and shift for the output data. */
        addr = baseaddr + (offset + xs) / PACK_WIDTH_BITS ;
        shift = (offset + xs) & PACK_MASK_BITS ;

        do {
          register int32 mcv ; /* min color value */

          s0 = shift + NORMALISE_BITS ;
          s1 = s0 - PACK_WIDTH_BITS ;
          s2 = s1 - PACK_WIDTH_BITS ;

          mcv = SUFFIX(unpack_bits)(addr, bits, mask, s0, s1, s2) ;
          INLINE_MIN32(mcv, mcv, pcv) ;
          SUFFIX(pack_bits)(mcv, addr, bits, mask, s0, s1, s2) ;

          shift += packed_bits ;
          addr += shift / PACK_WIDTH_BITS ;
          shift &= PACK_MASK_BITS ;
        } while ( --cxe > 0 ) ;
      }
    }
  }
}

static void SUFFIX(bitclipNmin)(render_blit_t *rb, dcoord y,
                                dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, "bitclipNmin") ;

  bitclipn(rb, y , xs , xe , SUFFIX(bitfillNmin)) ;
}

/* ---------------------------------------------------------------------- */

static void SUFFIX(bitfillNoverprint)(render_blit_t *rb, dcoord y,
                                      register dcoord xs, register dcoord xe)
{
  register pack_t *addr ;
  register const pack_t *packed, *opmask ;
  const pack_t *packed_base, *opmask_base ;
  int32 wpacked, packed_bits ;
  const blit_color_t *color = rb->color ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, STRINGIFY(SUFFIX(bitfillNoverprint))) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(rb->opmode == BLT_OVP_SOME, "Should be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;

  packed_base = packed = (const pack_t *)&color->packed.channels.bytes[0] ;
  packed_bits = CAST_SIZET_TO_INT32(color->map->packed_bits) ;
  opmask_base = opmask = (const pack_t *)&rb->p_ri->p_rs->cs.overprint_mask->bytes[0] ;

  xe = xe - xs + 1 ; /* total pixels to fill */
  xe *= packed_bits ; /* Width of fill in bits. */

  xs += rb->x_sep_position ;
  xs *= packed_bits ; /* start bit position */
  addr = (pack_t *)rb->ylineaddr + xs / PACK_WIDTH_BITS ; /* start address */
  xs &= PACK_MASK_BITS ; /* Bit shift of starting pixel */

  if ( packed_bits < PACK_WIDTH_BITS ) {
    /* The color has been expanded, because there wouldn't have been enough
       to read a whole word. Convert packed bits into the number of bits we
       can read at once. The packed channel data has enough space to store
       the expanded bytes for all packed_bits values up to blit_t width (32
       or 64 bits). */
    packed_bits = (int32)color->map->expanded_bytes * 8 ;
  }
  wpacked = packed_bits ;
  HQASSERT(wpacked >= PACK_WIDTH_BITS,
           "Generic tone blit color not suitably large or expanded") ;

  /* Partial left-span to align to pack_t. */
  if ( xs != 0 ) {
    pack_t mask ;
    int32 filled = PACK_WIDTH_BITS - xs ; /* bits filled */

    mask = (pack_t)((PACK_ONES & *opmask) >> xs) ;

    wpacked -= filled ; /* bits left in packed color */
    xe -= filled ;    /* bits left after this word */
    if ( xe < 0 ) { /* Fill is entirely within one pack_t */
      mask &= (pack_t)(PACK_ONES << -xe) ;
      *addr = (pack_t)((*addr & ~mask) | ((*packed >> xs) & mask)) ;
      return ;
    }

    *addr = (pack_t)((*addr & ~mask) | ((*packed >> xs) & mask)) ;
    ++addr ;

    /* xs is now converted to the first bit index in the current packed color
       word, and keeps this meaning until the end of the function. */
    xs = filled ;
  }

  if ( xe > 0 ) {
    /* Complete words in middle. We've now aligned the output address with
       a pack_t boundary, so xs is going to be used as the packed color bit
       phase shift from now on. */
    while ( (xe -= PACK_WIDTH_BITS) >= 0 ) {
      register pack_t cdata, cmask ;

      HQASSERT(wpacked > 0, "No color bits left") ;

      /* Extract remainder of previous color word. */
      cdata = (pack_t)(*packed++ << xs) ;
      cmask = (pack_t)(*opmask++ << xs) ;

      if ( (wpacked -= PACK_WIDTH_BITS - xs) <= 0 ) {
        /* Overran available bits. Adjust how many to take from the next
           color word to compensate. */
        xs -= wpacked ; /* New phase shift. */
        HQASSERT(xs >= 0 && xs < PACK_WIDTH_BITS, "New phase shift out of range") ;
        wpacked = packed_bits ;
        packed = packed_base ;
        opmask = opmask_base ;
      }

      /* If we didn't get enough bits from the first color word, merge with
         some of the next color word. */
      if ( xs > 0 ) {
        cdata |= *packed >> (PACK_WIDTH_BITS - xs) ;
        cmask |= *opmask >> (PACK_WIDTH_BITS - xs) ;
        if ( (wpacked -= xs) == 0 ) {
          /* Overran available bits. Need to take some from the start to
             fill the current word. */
          xs = -wpacked ; /* New phase shift */
          HQASSERT(xs >= 0 && xs < PACK_WIDTH_BITS, "New phase shift out of range") ;
          wpacked += packed_bits ;
          packed = packed_base ;
          opmask = opmask_base ;
          cdata |= *packed >> (PACK_WIDTH_BITS - xs) ;
          cmask |= *opmask >> (PACK_WIDTH_BITS - xs) ;
        }
      }

      *addr = (pack_t)((*addr & ~cmask) | (cdata & cmask)) ;
      ++addr ;
    }

    /* Partial right span. */
    if ( -xe < PACK_WIDTH_BITS ) {
      register pack_t cdata, cmask ;

      HQASSERT(wpacked > 0, "No color bits left") ;

      /* Extract remainder of previous color word. */
      cdata = (pack_t)(*packed++ << xs) ;
      cmask = (pack_t)(*opmask++ << xs) ;

      if ( (wpacked -= PACK_WIDTH_BITS - xs) <= 0 ) {
        /* Overran available bits. Adjust how many to take from the next
           color word to compensate. */
        xs -= wpacked ; /* New phase shift */
        HQASSERT(xs >= 0 && xs < PACK_WIDTH_BITS, "New phase shift out of range") ;
        wpacked = packed_bits ;
        packed = packed_base ;
        opmask = opmask_base ;
      }

      /* If we didn't get enough bits from the first color word, merge with
         some of the next color word. */
      if ( xs > 0 ) {
        cdata |= *packed >> (PACK_WIDTH_BITS - xs) ;
        cmask |= *opmask >> (PACK_WIDTH_BITS - xs) ;
        if ( (wpacked -= xs) < 0 ) {
          /* Overran available bits. Need to take some from the start to
             fill the current word. */
          xs = -wpacked ; /* New phase shift */
          HQASSERT(xs > 0 && xs < PACK_WIDTH_BITS, "New phase shift out of range") ;
          cdata |= *packed_base >> (PACK_WIDTH_BITS - xs) ;
          cmask |= *opmask_base >> (PACK_WIDTH_BITS - xs) ;
        }
      }

      cmask &= (pack_t)(PACK_ONES << -xe) ;
      *addr = (pack_t)((*addr & ~cmask) | (cdata & cmask)) ;
    }
  }
}

static void SUFFIX(bitclipNoverprint)(render_blit_t *rb, dcoord y,
                                      register dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, STRINGIFY(SUFFIX(bitclipNoverprint))) ;

  bitclipn(rb, y, xs, xe, SUFFIX(bitfillNoverprint)) ;
}

/** Generic pixel-interleaved blitter for knockouts. */
static void SUFFIX(bitfillNknockout)(render_blit_t *rb, dcoord y,
                                     dcoord xs, register dcoord xe)
{
  register pack_t *addr ;
  register const pack_t *packed ;
  const pack_t *packed_base ;
  int32 wpacked, packed_bits ;
  const blit_color_t *color = rb->color ;

  UNUSED_PARAM(dcoord, y);

  BITBLT_ASSERT(rb, xs, xe, y, y, STRINGIFY(SUFFIX(bitfillNknockout))) ;

  HQASSERT(rb->outputform->type == FORMTYPE_BANDBITMAP,
           "Output form is not tonemap") ;

  HQASSERT(color->valid & blit_color_packed, "Packed color not set for span") ;
  HQASSERT(rb->opmode == BLT_OVP_NONE, "Should not be overprinting") ;
  HQASSERT(rb->maxmode == BLT_MAX_NONE, "Should not be maxblitting") ;

  packed_base = packed = (const pack_t *)&color->packed.channels.bytes[0] ;
  packed_bits = CAST_SIZET_TO_INT32(color->map->packed_bits) ;

  xe = xe - xs + 1 ; /* total pixels to fill */
  xe *= packed_bits ; /* Width of fill in bits. */

  xs += rb->x_sep_position ;
  xs *= packed_bits ; /* start bit position */
  addr = (pack_t *)rb->ylineaddr + xs / PACK_WIDTH_BITS ; /* start address */
  xs &= PACK_MASK_BITS ; /* Bit shift of starting pixel */

  if ( packed_bits < PACK_WIDTH_BITS ) {
    /* The color has been expanded, because there wouldn't have been enough
       to read a whole word. Convert packed bits into the number of bits we
       can read at once. The packed channel data has enough space to store
       the expanded bytes for all packed_bits values up to blit_t width (32
       or 64 bits). */
    packed_bits = (int32)color->map->expanded_bytes * 8 ;
  }
  wpacked = packed_bits ;
  HQASSERT(wpacked >= PACK_WIDTH_BITS,
           "Generic tone blit color not suitably large or expanded") ;

  /* Partial left-span to align to byte boundary. */
  if ( xs != 0 ) {
    pack_t mask = (pack_t)(PACK_ONES >> xs) ;
    int32 filled = PACK_WIDTH_BITS - xs ; /* bits filled */

    wpacked -= filled ; /* bits left in packed color */
    xe -= filled ;    /* bits left after this word */
    if ( xe < 0 ) { /* Fill is entirely within one pack_t */
      mask &= (pack_t)(PACK_ONES << -xe) ;
      *addr = (pack_t)((*addr & ~mask) | ((*packed >> xs) & mask)) ;
      return ;
    }

    *addr = (pack_t)((*addr & ~mask) | (*packed >> xs)) ;
    ++addr ;

    /* xs is now converted to the first bit index in the current packed color
       word, and keeps this meaning until the end of the function. */
    xs = filled ;
  }

  if ( xe > 0 ) {
    /* Complete words in middle. We've now aligned the output address with
       a pack_t boundary, so xs is going to be used as the packed color bit
       phase shift from now on. */
    while ( (xe -= PACK_WIDTH_BITS) >= 0 ) {
      register pack_t cdata ;

      HQASSERT(wpacked > 0, "No color bits left") ;

      /* Extract remainder of previous color word. */
      cdata = (pack_t)(*packed++ << xs) ;

      if ( (wpacked -= PACK_WIDTH_BITS - xs) <= 0 ) {
        /* Overran available bits. Adjust how many to take from the next
           color word to compensate. */
        xs -= wpacked ; /* New phase shift. */
        HQASSERT(xs >= 0 && xs < PACK_WIDTH_BITS, "New phase shift out of range") ;
        wpacked = packed_bits ;
        packed = packed_base ;
      }

      /* If we didn't get enough bits from the first color word, merge with
         some of the next color word. */
      if ( xs > 0 ) {
        cdata |= *packed >> (PACK_WIDTH_BITS - xs) ;
        if ( (wpacked -= xs) <= 0 ) {
          /* Overran available bits. Need to take some from the start to
             fill the current word. */
          xs = -wpacked ; /* New phase shift */
          HQASSERT(xs >= 0 && xs < PACK_WIDTH_BITS, "New phase shift out of range") ;
          wpacked += packed_bits ;
          packed = packed_base ;
          cdata |= *packed >> (PACK_WIDTH_BITS - xs) ;
        }
      }

      *addr++ = cdata ;
    }

    /* Partial right span. */
    if ( -xe < PACK_WIDTH_BITS ) {
      pack_t mask ;
      register pack_t cdata ;

      HQASSERT(wpacked > 0, "No color bits left") ;

      /* Extract remainder of previous color word. */
      cdata = (pack_t)(*packed++ << xs) ;

      if ( (wpacked -= PACK_WIDTH_BITS - xs) <= 0 ) {
        /* Overran available bits. Adjust how many to take from the next
           color word to compensate. */
        xs -= wpacked ; /* New phase shift */
        HQASSERT(xs >= 0 && xs < PACK_WIDTH_BITS, "New phase shift out of range") ;
        wpacked = packed_bits ;
        packed = packed_base ;
      }

      /* If we didn't get enough bits from the first color word, merge with
         some of the next color word. */
      if ( xs > 0 ) {
        cdata |= *packed >> (PACK_WIDTH_BITS - xs) ;
        if ( (wpacked -= xs) < 0 ) {
          /* Overran available bits. Need to take some from the start to
             fill the current word. */
          xs = -wpacked ; /* New phase shift */
          HQASSERT(xs > 0 && xs < PACK_WIDTH_BITS, "New phase shift out of range") ;
          cdata |= *packed_base >> (PACK_WIDTH_BITS - xs) ;
        }
      }

      mask = (pack_t)(PACK_ONES << -xe) ;
      *addr = (pack_t)((*addr & ~mask) | (cdata & mask)) ;
    }
  }
}

static void SUFFIX(bitclipNknockout)(render_blit_t *rb, dcoord y,
                                     register dcoord xs, register dcoord xe)
{
  BITCLIP_ASSERT(rb, xs, xe, y, y, STRINGIFY(SUFFIX(bitclipNknockout))) ;

  bitclipn(rb, y , xs , xe , SUFFIX(bitfillNknockout)) ;
}

/** Self-modifying blits for N-bit tone span fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void SUFFIX(bitfillN)(render_blit_t *rb, dcoord y, dcoord xs, dcoord xe)
{
  int op = (rb->opmode == BLT_OVP_SOME) ;
  blit_slice_t *slice = &SUFFIX(slicesN)[op][rb->clipmode] ;

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->spanfn)(rb, y, xs, xe) ;
}

/* ---------------------------------------------------------------------- */
/** Self-modifying blits for N-bit tone block fns. This works out
    what the appropriate blit to call is, calls it, and also installs it
    in place of the current blit. */
static void SUFFIX(blkfillN)(render_blit_t *rb, register dcoord  ys,
                             register dcoord  ye, dcoord xs, dcoord xe)
{
  int op = (rb->opmode == BLT_OVP_SOME) ;
  blit_slice_t *slice = &SUFFIX(slicesN)[op][rb->clipmode] ;

  /* Replace this blit in the stack with the appropriate specialised
     function */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode, slice) ;

  (*slice->blockfn)(rb, ys, ye, xs, xe) ;
}

/* ---------------------------------------------------------------------- */
/* ---------------------------------------------------------------------- */

/** The generic pixel interleaved surface description. */
static surface_t SUFFIX(toneN) = SURFACE_INIT ;
static const surface_t *SUFFIX(indexed)[N_SURFACE_TYPES] ;

static void SUFFIX(init_toneblt_N)(surface_set_t *set,
                                   void (*blitmap_optimise)(blit_colormap_t *color))
{
  /* Knockout optimised slice. */
  SUFFIX(slicesN)[0][BLT_CLP_NONE].spanfn =
    SUFFIX(slicesN)[0][BLT_CLP_RECT].spanfn = SUFFIX(bitfillNknockout) ;
  SUFFIX(slicesN)[0][BLT_CLP_COMPLEX].spanfn = SUFFIX(bitclipNknockout) ;

  SUFFIX(slicesN)[0][BLT_CLP_NONE].blockfn =
    SUFFIX(slicesN)[0][BLT_CLP_RECT].blockfn = blkfillspan ;
  SUFFIX(slicesN)[0][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  SUFFIX(slicesN)[0][BLT_CLP_NONE].charfn =
    SUFFIX(slicesN)[0][BLT_CLP_RECT].charfn =
    SUFFIX(slicesN)[0][BLT_CLP_COMPLEX].charfn = charbltn ;

  SUFFIX(slicesN)[0][BLT_CLP_NONE].imagefn =
    SUFFIX(slicesN)[0][BLT_CLP_RECT].imagefn =
    SUFFIX(slicesN)[0][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* Overprint optimised slice. */
  SUFFIX(slicesN)[1][BLT_CLP_NONE].spanfn =
    SUFFIX(slicesN)[1][BLT_CLP_RECT].spanfn = SUFFIX(bitfillNoverprint) ;
  SUFFIX(slicesN)[1][BLT_CLP_COMPLEX].spanfn = SUFFIX(bitclipNoverprint) ;

  SUFFIX(slicesN)[1][BLT_CLP_NONE].blockfn =
    SUFFIX(slicesN)[1][BLT_CLP_RECT].blockfn = blkfillspan ;
  SUFFIX(slicesN)[1][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  SUFFIX(slicesN)[1][BLT_CLP_NONE].charfn =
    SUFFIX(slicesN)[1][BLT_CLP_RECT].charfn =
    SUFFIX(slicesN)[1][BLT_CLP_COMPLEX].charfn = charbltn ;

  SUFFIX(slicesN)[1][BLT_CLP_NONE].imagefn =
    SUFFIX(slicesN)[1][BLT_CLP_RECT].imagefn =
    SUFFIX(slicesN)[1][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* Base blits */
  SUFFIX(toneN).baseblits[BLT_CLP_NONE].spanfn =
    SUFFIX(toneN).baseblits[BLT_CLP_RECT].spanfn =
    SUFFIX(toneN).baseblits[BLT_CLP_COMPLEX].spanfn = SUFFIX(bitfillN) ;

  SUFFIX(toneN).baseblits[BLT_CLP_NONE].blockfn =
    SUFFIX(toneN).baseblits[BLT_CLP_RECT].blockfn =
    SUFFIX(toneN).baseblits[BLT_CLP_COMPLEX].blockfn = SUFFIX(blkfillN) ;

  SUFFIX(toneN).baseblits[BLT_CLP_NONE].charfn =
    SUFFIX(toneN).baseblits[BLT_CLP_RECT].charfn =
    SUFFIX(toneN).baseblits[BLT_CLP_COMPLEX].charfn = charbltn ;

  SUFFIX(toneN).baseblits[BLT_CLP_NONE].imagefn =
    SUFFIX(toneN).baseblits[BLT_CLP_RECT].imagefn =
    SUFFIX(toneN).baseblits[BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* No object map on the side blits for toneN */

  /* Max blits */
  SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_NONE].spanfn =
    SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_RECT].spanfn = SUFFIX(bitfillNmax) ;
  SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].spanfn = SUFFIX(bitclipNmax) ;

  SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_NONE].blockfn =
    SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_RECT].blockfn = blkfillspan ;
  SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_NONE].charfn =
    SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_RECT].charfn =
    SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].charfn = charbltn ;

  SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_NONE].imagefn =
    SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_RECT].imagefn =
    SUFFIX(toneN).maxblits[BLT_MAX_MAX][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_NONE].spanfn =
    SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_RECT].spanfn = SUFFIX(bitfillNmin) ;
  SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].spanfn = SUFFIX(bitclipNmin) ;

  SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_NONE].blockfn =
    SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_RECT].blockfn = blkfillspan ;
  SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].blockfn = blkclipspan ;

  SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_NONE].charfn =
    SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_RECT].charfn =
    SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].charfn = charbltn ;

  SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_NONE].imagefn =
    SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_RECT].imagefn =
    SUFFIX(toneN).maxblits[BLT_MAX_MIN][BLT_CLP_COMPLEX].imagefn = imagebltn ;

  /* No ROP blits for toneN */

  init_pcl_pattern_blit(&SUFFIX(toneN)) ;

  /* Builtins for intersect, pattern and gouraud */
  surface_intersect_builtin(&SUFFIX(toneN)) ;
  surface_pattern_builtin(&SUFFIX(toneN)) ;
  surface_gouraud_builtin_tone_multi(&SUFFIX(toneN)) ;

  SUFFIX(toneN).areafill = areahalfN ;
  SUFFIX(toneN).prepare = render_prepare_N ;
  SUFFIX(toneN).blit_colormap_optimise = blitmap_optimise ;

  SUFFIX(toneN).n_rollover = 3 ;
  SUFFIX(toneN).screened = FALSE ;

  builtin_clip_N_surface(&SUFFIX(toneN), SUFFIX(indexed)) ;

  /* The surface we've just completed is part of a set implementing 8 bpc
     RGB output. Add it and all of the associated surfaces to the surface
     array for this set. */
  set->indexed = SUFFIX(indexed) ;
  set->n_indexed = NUM_ARRAY_ITEMS(SUFFIX(indexed)) ;

  SUFFIX(indexed)[SURFACE_OUTPUT] = &SUFFIX(toneN) ;
  surface_set_trap_builtin(set, SUFFIX(indexed));
  surface_set_transparency_builtin(set, &SUFFIX(toneN), SUFFIX(indexed)) ;
  set->packing_unit_bits = PACK_WIDTH_BITS ;

  /* Now that we've filled in the toneN surface description, hook it up so
     that it can be found. */
  surface_set_register(set) ;
}

/* Get rid of local macro definitions, in case we're included again. */
#undef STRINGIFY
#undef STRINGIFY2
#undef SUFFIX
#undef SUFFIX2
#undef SUFFIX3
#undef PACK_ONES
#undef PACK_MASK_BITS
#undef PACK_WIDTH_BITS
#undef NORMALISE_BITS
#undef pack_t

/* Log stripped */
