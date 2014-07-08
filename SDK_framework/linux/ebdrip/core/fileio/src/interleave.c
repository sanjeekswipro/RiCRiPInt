/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:interleave.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * InterleaveDecode filter. This filter can extract interleaved sample data,
 * rescaling the samples from any number of input bits in the range 1-16 to
 * any other range of output bits in 1-16, and adding padding.
 */

#include "core.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "mm.h"
#include "mmcompat.h"
#include "namedef_.h"
#include "objnamer.h"

#include "fileio.h"
#include "interleave.h"

/* These are used for object name verification as well as filter names. */
#define INTERLEAVE_NAME "InterleaveDecode"
#define INTERLEAVE_MULTI_NAME "InterleaveMultiDecode"

typedef struct {
  uint32 bits_in ;          /**< Number of input bits. */
  uint32 bits_out ;         /**< Number of output bits. */
  uint32 samples ;          /**< Number of samples in input window. */
  uint32 in_align_count ;   /**< Number of windows before input align. */
  uint32 in_align_bits ;    /**< Bits to drop when aligning input. */
  uint32 out_align_count ;  /**< Number of windows before output align. */
  uint32 out_align_bits ;   /**< Bits to pad when aligning output. */
  uint32 repeats ;          /**< Number of windows to get. */
  Bool seeksource ;         /**< Should we seek before filling buffer? */
  Hq32x2 position ;         /**< Position to seek in underlying stream. */

  /* Computed values. */
  int32 out_window ;        /**< Size of output window in bytes. */
  uint32 bits_before ;      /**< Bits in window before input. */
  uint32 bits_after ;       /**< Bits left in window after input. */
  int32 residual_in ;       /**< Left-over input bits from last window. */
  int32 residual_in_bits ;  /**< Left-over input bits from window. */
  uint32 residual_out ;     /**< Left-over output from last window. */
  uint32 residual_out_bits ; /**< Left-over output bits from window. */
  uint32 in_align_left ;    /**< Number of windows left before alignment */
  uint32 out_align_left ;   /**< Number of output left before alignment */
  uint32 repeats_left ;     /**< Number of windows left to get. */

  OBJECT_NAME_MEMBER

  uint16 mapping[1] ;       /**< Precomputed scale table, 2^input_bits long. */
} interleave_single_t ;

/* Default size of interleave buffer. May be larger if necessary to get a
   single output window in. */
#define INTERLEAVEBUFFSIZE 4096

Bool interleaveFilterInit(FILELIST *filter, OBJECT *args, STACK *stack)
{
  interleave_single_t *state ;
  OBJECT *theo ;
  int32 input_bits, output_bits, window_bits, buffsize ;
  int32 pop_args = 0 ;

  enum {
    match_InputBits, match_BitsBefore, match_Samples, match_BitsAfter,
    match_InputAlignRepeat, match_InputAlignBits,
    match_OutputBits, match_OutputAlignRepeat, match_OutputAlignBits,
    match_Repeat, match_SeekSource, match_Mapping, match_n_entries
  } ;
  static NAMETYPEMATCH match[match_n_entries + 1] = {
    { NAME_InputBits | OOPTIONAL, 1, { OINTEGER }},
    { NAME_BitsBefore | OOPTIONAL, 1, { OINTEGER }},
    { NAME_Samples, 1, { OINTEGER }}, /* The only mandatory element */
    { NAME_BitsAfter | OOPTIONAL, 1, { OINTEGER }},
    { NAME_InputAlignRepeat | OOPTIONAL, 1, { OINTEGER }},
    { NAME_InputAlignBits | OOPTIONAL, 1, { OINTEGER }},
    { NAME_OutputBits | OOPTIONAL, 1, { OINTEGER }},
    { NAME_OutputAlignRepeat | OOPTIONAL, 1, { OINTEGER }},
    { NAME_OutputAlignBits | OOPTIONAL, 1, { OINTEGER }},
    { NAME_Repeat | OOPTIONAL, 1, { OINTEGER }},
    { NAME_SeekSource | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_Mapping | OOPTIONAL, 1, { OARRAY }},
    DUMMY_END_MATCH
  } ;

  HQASSERT(filter , "filter NULL in InterleaveDecode") ;
  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;

  if ( !args && !isEmpty(*stack) ) {
    args = theTop(*stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler(INVALIDACCESS) ;
    if ( ! dictmatch(args, match) )
      return FALSE ;
    if ( ! FilterCheckArgs(filter, args))
      return FALSE ;
    OCopy(theIParamDict(filter), *args) ;
  } else /* No point in avoiding dictionary, or we become NullDecode */
    return error_handler(TYPECHECK) ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  /* This parameter must be extracted before creating the state, all others
     can be afterwards. */
  input_bits = 8 ; /* default */
  if ( (theo = match[match_InputBits].result) != NULL ) {
    input_bits = oInteger(*theo) ;
    if ( input_bits < 1 || input_bits > 16 )
      return error_handler(RANGECHECK) ;
  }

  state = (interleave_single_t *)mm_alloc(mm_pool_temp,
                                          offsetof(interleave_single_t, mapping[0]) + sizeof(uint16) * ((size_t)1 << input_bits),
                                          MM_ALLOC_CLASS_INTERLEAVE_STATE) ;
  if ( state == NULL )
    return error_handler(VMERROR) ;

  /* Store state in filter ASAP for cleanup. */
  theIFilterPrivate(filter) = state ;

  state->bits_in = CAST_SIGNED_TO_UINT32(input_bits) ;

  /* No default samples, it's mandatory, and we need at least one. */
  theo = match[match_Samples].result ;
  HQASSERT(theo, "No Samples for InterleaveDecode") ;
  if ( oInteger(*theo) < 1 )
    return error_handler(RANGECHECK) ;
  state->samples = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;

  /* default offset to start of samples */
  state->bits_before = 0 ;
  if ( (theo = match[match_BitsBefore].result) != NULL ) {
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    state->bits_before = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }

  /* default padding after samples */
  state->bits_after = 0 ;
  if ( (theo = match[match_BitsAfter].result) != NULL ) {
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    state->bits_after = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }

  /* Whole window size in bits */
  window_bits = state->bits_before + input_bits * state->samples + state->bits_after ;

  /* default is no input alignment */
  state->in_align_count = 0 ;
  if ( (theo = match[match_InputAlignRepeat].result) != NULL ) {
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    state->in_align_count = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }

  /* default input alignment size is one byte */
  state->in_align_bits = 8 ;
  if ( (theo = match[match_InputAlignBits].result) != NULL ) {
    if ( oInteger(*theo) < 1 )
      return error_handler(RANGECHECK) ;
    state->in_align_bits = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }
  { /* Convert from bit alignment to number of bits to drop. It doesn't
       matter if the calculation is wrong for in_align_count == 0, because
       the result is not used. */
    int32 alignment = (window_bits * state->in_align_count) % state->in_align_bits ;
    if ( alignment != 0 )
      alignment = state->in_align_bits - alignment ;
    state->in_align_bits = CAST_SIGNED_TO_UINT32(alignment) ;
  }

  /* default output width to same as input */
  output_bits = input_bits ;
  if ( (theo = match[match_OutputBits].result) != NULL ) {
    output_bits = oInteger(*theo) ;
    if ( output_bits < 1 || output_bits > 16 )
      return error_handler(RANGECHECK) ;
  }
  state->bits_out = CAST_SIGNED_TO_UINT32(output_bits) ;

  /* default is no output alignment */
  state->out_align_count = 0 ;
  if ( (theo = match[match_OutputAlignRepeat].result) != NULL ) {
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    state->out_align_count = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }

  /* default output alignment size is one byte. */
  state->out_align_bits = 8 ;
  if ( (theo = match[match_OutputAlignBits].result) != NULL ) {
    if ( oInteger(*theo) < 1 )
      return error_handler(RANGECHECK) ;

    state->out_align_bits = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }
  { /* Convert from bit alignment to number of bits to drop. It doesn't
       matter if the calculation is wrong for out_align_count == 0, because
       the result is not used. */
    int32 alignment = (window_bits * state->out_align_count) % state->out_align_bits ;
    if ( alignment != 0 )
      alignment = state->out_align_bits - alignment ;
    state->out_align_bits = CAST_SIGNED_TO_UINT32(alignment) ;
  }

  /* default is infinite repeats */
  state->repeats = 0 ;
  if ( (theo = match[match_Repeat].result) != NULL ) {
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    state->repeats = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }

  /* default is no seeking */
  state->seeksource = FALSE ;
  Hq32x2FromUint32(&state->position, 0u) ;
  if ( (theo = match[match_SeekSource].result) != NULL ) {
    state->seeksource = oBool(*theo) ;
    if ( state->seeksource && theIUnderFile(filter) != NULL ) {
      /* Do an immediate fileposition on the underlying file. */
      if ( (*theIMyFilePos(theIUnderFile(filter)))(theIUnderFile(filter), &state->position) == EOF )
        return error_handler(IOERROR) ;
    }
  }

  if ( (theo = match[match_Mapping].result) != NULL ) {
    int32 inlim = 1 << input_bits ;
    int32 outlim = 1 << output_bits ;
    int32 i ;

    if ( ! oCanRead(*theo) && ! object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;
    if ( theLen(*theo) != inlim )
      return error_handler(RANGECHECK) ;

    /* Must be an array of 1 << input_bits samples, each sample in the range
       0 <= sample < (1 << output_bits). */
    for ( i = 0, theo = oArray(*theo) ; i < inlim ; ++i, ++theo ) {
      if ( oType(*theo) != OINTEGER )
        return error_handler(TYPECHECK) ;
      if ( oInteger(*theo) < 0 || oInteger(*theo) >= outlim )
        return error_handler(RANGECHECK) ;
      state->mapping[i] = CAST_SIGNED_TO_UINT16(oInteger(*theo)) ;
    }
  } else { /* default is linear mapping */
    uint32 i ;
    uint32 maxin = (1u << input_bits) - 1 ;
    uint32 maxout = (1u << output_bits) - 1 ;

    HQASSERT(maxin <= 0xffffu, "Max input larger than expected") ;
    HQASSERT(maxout <= 0xffffu, "Max output larger than expected") ;

    /* Build a fast lookup table from input units to output units. The
       calculation used is the same as:

         floor(val / (2^Bin - 1) * (2^Bout - 1) + 0.5

       This calculation is scaled to avoid any floating point, and so it will
       still fit in an unsigned 32 bit int.
    */
    for ( i = 0 ; i <= maxin ; ++i ) {
#if 0 /** \todo ajcd 2008-03-30: Oops, can overflow 32 bits */
      uint32 o = (i * (maxout << 1) + maxin) / (maxin << 1) ;
#else
      /** \todo ajcd 2008-03-30: Lose some precision to avoid overflow.
          Max value is 65535 * 65535 + 32767 before the division. */
      uint32 o = (i * maxout + (maxin >> 1)) / maxin ;
#endif
      state->mapping[i] = CAST_UNSIGNED_TO_UINT16(o) ;
    }
  }

  /* Compute remaining fields of state. */
  state->residual_in = 0 ;
  state->residual_in_bits = 0 ;
  state->residual_out = 0 ;
  state->residual_out_bits = 0 ;

  /* Counters for windows left before alignment. */
  state->in_align_left = state->in_align_count ;
  state->out_align_left = state->out_align_count ;
  state->repeats_left = state->repeats ;

  /* The output window size is used when determining if there is space left
     in the decode buffer. */
  state->out_window = window_bits + state->out_align_bits ; /* may be aligned */
  state->out_window = (state->out_window + 7) >> 3 ; /* bits to bytes */

  /* The buffer will be sized so that at least one output window can be
     decoded at once. Start by computing the minimum value, and then
     letting it increase. */
  buffsize = INTERLEAVEBUFFSIZE ;
  if ( buffsize < state->out_window )
    buffsize = state->out_window ;

  theIBuffer(filter) = ( uint8 * )mm_alloc(mm_pool_temp ,
                                           buffsize + 1,
                                           MM_ALLOC_CLASS_INTERLEAVE_BUFFER) ;
  if ( theIBuffer(filter) == NULL )
    return error_handler(VMERROR) ;

  theIBufferSize(filter) = buffsize ;
  theIPtr(filter) = ++theIBuffer(filter) ;
  theICount(filter) = 0 ;

  theIFilterState(filter) = FILTER_INIT_STATE ;
  NAME_OBJECT(state, INTERLEAVE_NAME) ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void interleaveFilterDispose(FILELIST *filter)
{
  HQASSERT(filter, "filter NULL for InterleaveDecode") ;
  HQASSERT(isIInputFile(filter), "InterleaveDecode should be an input filter") ;

  if ( theIBuffer(filter) ) {
    mm_free(mm_pool_temp, (mm_addr_t)(theIBuffer(filter) - 1),
            theIBufferSize(filter) + 1) ;
    theIBuffer(filter) = NULL ;
  }

  if ( theIFilterPrivate(filter) ) {
    interleave_single_t *state = theIFilterPrivate(filter) ;

    UNNAME_OBJECT(state) ;

    mm_free(mm_pool_temp, (mm_addr_t)state,
            offsetof(interleave_single_t, mapping[0]) + sizeof(uint16) * ((size_t)1 << state->bits_in)) ;
    theIFilterPrivate(filter) = NULL ;
  }
}

static Bool interleaveDecode(FILELIST *filter, int32 *ret_bytes)
{
  interleave_single_t *state ;
  register uint8 *ptr ;
  register FILELIST *uflptr ;
  register int32 residual_in, residual_in_bits, input_bits, input_mask ;
  register uint32 residual_out, residual_out_bits, output_bits ;
  uint8 *limit ;
  register uint16 *mapping ;
  uint32 inbytes, repeats_left, in_align_left, out_align_left ;
  Bool ret_sign = -1 ;

  HQASSERT(filter, "filter NULL in interleaveDecodeBuffer.") ;
  HQASSERT(ret_bytes, "ret_bytes NULL in interleaveDecodeBuffer.") ;

  uflptr = theIUnderFile(filter) ;
  HQASSERT(uflptr, "No underlying file in interleaveDecodeBuffer.") ;

  ptr = theIBuffer(filter) ;
  HQASSERT(ptr, "No buffer pointer in interleaveDecodeBuffer.") ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, INTERLEAVE_NAME) ;

  if ( state->seeksource ) {
    /* Need to reset to last known position before reading input. */
    if ( (*theIMySetFilePos(uflptr))(uflptr, &state->position) == EOF )
      return error_handler(IOERROR) ;
  }

  /* Track bytes consumed as well as emitted, so we can update the
     input position. */
  inbytes = 0 ;

  /* Residual input from the last window is stored in right-aligned in
     residual_in. When there is residual input, the first bit in the input
     stream is the highest identified by residual_in_bits. */
  residual_in = state->residual_in ;
  residual_in_bits = state->residual_in_bits ;
  HQASSERT(residual_in_bits < 8 && residual_in_bits >= 0,
           "Residual input bits don't fit in a byte") ;

  residual_out = state->residual_out ;
  residual_out_bits = state->residual_out_bits ;
  HQASSERT(residual_out_bits < 8, "Residual output bits more than a byte") ;

  /* Simplify counting in loop by letting these counts wrap around for the
     infinity cases. */
  repeats_left = state->repeats_left ;
  in_align_left = state->in_align_left ;
  out_align_left = state->out_align_left ;

  if ( state->repeats > 0 && repeats_left == 0 )
    goto endoffile ;

  /* Unpack for performance. Premature optimisation and all that... */
  input_bits = state->bits_in ;
  input_mask = (1u << input_bits) - 1 ;
  output_bits = state->bits_out ;
  mapping = state->mapping ;

  /* Process a single output window at a time until the buffer is full. */
  limit = ptr + theIBufferSize(filter) - state->out_window ;
  while ( ptr <= limit ) {
    register uint32 count ;

    /* Skip initial samples to offset. The amount to skip is already measured
       in bits. */
    residual_in_bits -= state->bits_before ;
    while ( residual_in_bits < 0 ) {
      if ( (residual_in = Getc(uflptr)) == EOF )
        goto endoffile ;
      ++inbytes ;
      residual_in_bits += 8 ;
    }

    HQASSERT(residual_in_bits < 8 && residual_in_bits >= 0,
             "Residual input bits don't fit in a byte") ;

    /* Map "count" samples from input to output. */
    count = state->samples ;
    for (;;) {
      register int32 c ;

      HQASSERT(count > 0, "No samples to read") ;

      /* Accumulate sample value into residual_in, which may temporarily
         exceed a byte. */
      while ( residual_in_bits < input_bits ) {
        if ( (c = Getc(uflptr)) == EOF )
          goto endoffile ;
        ++inbytes ;
        residual_in <<= 8 ;
        residual_in |= c ;
        residual_in_bits += 8 ;
      }

      /* Map sample(s) stored in residual_in through lookup table. */
      do {
        residual_in_bits -= input_bits ;

        residual_out <<= output_bits ;
        residual_out |= mapping[(residual_in >> residual_in_bits) & input_mask] ;
        residual_out_bits += output_bits ;

        /* Flush one or two bytes to output if possible. */
        if ( residual_out_bits >= 8 ) {
          if ( residual_out_bits >= 16 ) {
            residual_out_bits -= 8 ;
            *ptr++ = (uint8)(residual_out >> residual_out_bits) ;
          }

          residual_out_bits -= 8 ;
          *ptr++ = (uint8)(residual_out >> residual_out_bits) ;
        }

        HQASSERT(residual_in_bits < 8 && residual_in_bits >= 0,
                 "Residual input bits don't fit in a byte") ;
        HQASSERT(residual_out_bits < 8, "Residual output bits more than a byte") ;

        if ( --count == 0 )
          goto donesamples ;
      } while ( residual_in_bits >= input_bits ) ;
    }

  donesamples:
    /* Skip remaining window samples. The amount to skip is measured in bits
       already. */
    residual_in_bits -= state->bits_after ;
    while ( residual_in_bits < 0 ) {
      if ( (residual_in = Getc(uflptr)) == EOF )
        goto endoffile ;
      ++inbytes ;
      residual_in_bits += 8 ;
    }

    HQASSERT(residual_in_bits < 8 && residual_in_bits >= 0,
             "Residual input bits don't fit in a byte") ;

    /* Finished a window. Determine if we should align the input and/or the
       output data. */
    if ( --in_align_left == 0 ) {
      residual_in_bits -= state->in_align_bits ;
      while ( residual_in_bits < 0 ) {
        if ( (residual_in = Getc(uflptr)) == EOF )
          goto endoffile ;
        ++inbytes ;
        residual_in_bits += 8 ;
      }

      in_align_left = state->in_align_count ;
    }

    if ( --out_align_left == 0 ) {
      register uint32 out_align_bits = state->out_align_bits ;

      residual_out_bits += out_align_bits ;
      if ( residual_out_bits < 8 ) {
        /* Didn't align enough bits to make a byte, so we just need to
           shift the residual output over. */
        residual_out <<= out_align_bits ;
      } else {
        /* The alignment will fill a byte. Align the residual output so first
           bit is high bit. The shift is calculated by undoing the addition
           of the aligment bit count, and determining how many bits were left
           to fill in a byte. */
        residual_out <<= 8 - residual_out_bits + out_align_bits ;
        do {
          *ptr++ = (uint8)residual_out ;
          residual_out = 0 ;
          residual_out_bits -= 8 ;
        } while ( residual_out_bits > 8 ) ;
      }

      out_align_left = state->out_align_count ;
    }

    /* If that was the last window, we're finished. */
    if ( --repeats_left == 0 )
      break ;
  }

  state->residual_in = residual_in ;
  state->residual_in_bits = residual_in_bits ;
  state->residual_out = residual_out ;
  state->residual_out_bits = residual_out_bits ;

  /* Only update the counts if we should have done anything with them in the
     first place. If not, they've have wrapped around, but we don't care. */
  if ( state->repeats > 0 )
    state->repeats_left = repeats_left ;

  if ( state->in_align_left > 0 )
    state->in_align_left = in_align_left ;

  if ( state->out_align_left > 0 )
    state->out_align_left = out_align_left ;

  ret_sign = 1 ; /* Not EOF, so return positive number of bytes. */

 endoffile:
  if ( ret_sign < 0 ) {
    HQASSERT(residual_out_bits < 8,
             "InterleaveDecode EOF with more than a byte of output left") ;
    if ( residual_out_bits > 0 ) {
      /* EOF was seen but some leftover output bits are already decided.
         Flush them to the output, aligned so the first bit is high bit. */
      *ptr++ = (uint8)(residual_out << (8 - residual_out_bits)) ;
    }
  }

  HQASSERT(ptr - theIBuffer(filter) <= theIBufferSize(filter),
           "Buffer overrun in InterleaveDecode") ;

  Hq32x2AddUint32(&state->position, &state->position, inbytes) ;
  *ret_bytes = ret_sign * CAST_PTRDIFFT_TO_INT32(ptr - theIBuffer(filter)) ;

  return TRUE ;
}

void interleave_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH(INTERLEAVE_NAME) ,
                       FILTER_FLAG | READ_FLAG,
                       0, NULL, 0,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       interleaveFilterInit,                 /* initfile */
                       FilterCloseFile,                      /* closefile */
                       interleaveFilterDispose,              /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       interleaveDecode,                     /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/****************************************************************************/

typedef struct {
  FILELIST *flptr ;         /**< The data source. */
  uint16 filter_id ;        /**< Filter ID of the data source. */
  Bool close_source ;       /**< Close this source when we quit. */
  uint32 bits_in ;          /**< Number of bits in source samples. */
  uint32 in_align_count ;   /**< Number of windows before input align. */
  uint32 in_align_bits ;    /**< Bits to drop when aligning input. */

  /* Computed values. */
  int32 residual_in ;       /**< Left-over input bits from last window. */
  int32 residual_in_bits ;  /**< Left-over input bits from window. */
  uint32 in_align_left ;    /**< Number of windows left before alignment */
} multi_source_t ;

typedef struct {
  uint32 n_sources ;        /**< Number of data sources. */
  uint32 out_align_count ;  /**< Number of composites before output align. */
  uint32 out_align_bits ;   /**< Bits to pad when aligning output. */
  uint32 repeats ;          /**< Number of windows to get. */

  /* Computed values. */
  int32 out_window ;        /**< Size of output window in bytes. */
  uint32 repeats_left ;     /**< Number of composites left to get. */
  uint32 out_align_left ;   /**< Number of repeats left before alignment */
  uint32 residual_out ;     /**< Left-over output from last window. */
  uint32 residual_out_bits ; /**< Left-over output bits from window. */

  OBJECT_NAME_MEMBER

  multi_source_t sources[1] ; /**< Extendible array of data sources. */
} interleave_multi_t ;

Bool interleaveMultiFilterInit(FILELIST *filter, OBJECT *args, STACK *stack)
{
  interleave_multi_t *state ;
  OBJECT *theo, *sources ;
  int32 n_sources, input_bits, in_align_count, in_align_bits, i ;
  uint32 window_size, buffsize ;
  size_t state_size ;
  int32 pop_args = 0 ;

  enum {
    match_DataSource, match_InputBits,
    match_InputAlignRepeat, match_InputAlignBits,
    match_OutputAlignRepeat, match_OutputAlignBits,
    match_Repeat, match_n_entries
  } ;
  static NAMETYPEMATCH match[match_n_entries + 1] = {
    { NAME_DataSource, 2, { OARRAY, OPACKEDARRAY }},
    { NAME_InputBits | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINTEGER }},
    { NAME_InputAlignRepeat | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINTEGER }},
    { NAME_InputAlignBits | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINTEGER }},
    { NAME_OutputAlignRepeat | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINTEGER }},
    { NAME_OutputAlignBits | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINTEGER }},
    { NAME_Repeat | OOPTIONAL, 1, { OINTEGER }},
    DUMMY_END_MATCH
  } ;

  HQASSERT(filter , "filter NULL in InterleaveMultiDecode") ;
  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;

  /* Check for optional arguments. May have input bits, output bits on stack,
     or dictionary. */
  if ( !args && !isEmpty(*stack) ) {
    args = theTop(*stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler(INVALIDACCESS) ;
    if ( ! dictmatch(args, match) )
      return FALSE ;
    if ( ! FilterCheckArgs(filter, args))
      return FALSE ;
    OCopy(theIParamDict(filter), *args) ;
  } else /* No point in avoiding dictionary, or we become NullDecode */
    return error_handler(TYPECHECK) ;

  /* This parameter must be extracted before creating the state, all others
     can be afterwards. */
  sources = match[match_DataSource].result ;
  n_sources = theLen(*sources) ;
  if ( n_sources < 1 ) /* 1 doesn't make sense, but we'll allow it anyway */
    return error_handler(RANGECHECK) ;

  state_size = offsetof(interleave_multi_t, sources[0]) + sizeof(multi_source_t) * n_sources ;
  state = (interleave_multi_t *)mm_alloc(mm_pool_temp, state_size,
                                         MM_ALLOC_CLASS_INTERLEAVE_STATE) ;
  if ( state == NULL )
    return error_handler(VMERROR) ;

  /* Store state in filter ASAP for cleanup. */
  theIFilterPrivate(filter) = state ;
  HqMemZero((uint8 *)state, CAST_SIZET_TO_INT32(state_size)) ;

  state->n_sources = CAST_SIGNED_TO_UINT32(n_sources) ;

  /* Set defaults for source parameters; if these are integers, the same
     value is used to all items. If arrays, and the array is shorter than
     the source array, the last value will be used for all subsequent
     sources. */
  input_bits = 8 ; /* default one byte */
  if ( (theo = match[match_InputBits].result) != NULL ) {
    if ( oType(*theo) == OINTEGER ) {
      input_bits = oInteger(*theo) ; /* Rangecheck when used */
    } else if ( theLen(*theo) > n_sources )
      return error_handler(RANGECHECK) ;
  }

  /* default is no input alignment */
  in_align_count = 0 ;
  if ( (theo = match[match_InputAlignRepeat].result) != NULL ) {
    if ( oType(*theo) == OINTEGER ) {
      in_align_count = oInteger(*theo) ; /* Rangecheck when used */
    } else if ( theLen(*theo) > n_sources )
      return error_handler(RANGECHECK) ;
  }

  /* default input alignment size is one byte */
  in_align_bits = 8 ;
  if ( (theo = match[match_InputAlignBits].result) != NULL ) {
    if ( oType(*theo) == OINTEGER ) {
      in_align_bits = oInteger(*theo) ; /* Rangecheck when used */
    } else if ( theLen(*theo) > n_sources )
      return error_handler(RANGECHECK) ;
  }

  /* Unpack the data sources using the normal filter source routines. Track
     the output size, we'll use it to work out the ideal buffer size. */
  window_size = 0 ;
  for ( i = 0, sources = oArray(*sources) ; i < n_sources ; ++i, ++sources ) {
    multi_source_t *source = &state->sources[i] ;

    if ( ! filter_target_or_source(filter, sources) )
      return FALSE ;

    /* filter_target_or_source has set theIUnderFile, theIUnderFilterId, and
       possibly the CST flag. Steal these and stick them in the source
       structure. */
    source->flptr = theIUnderFile(filter) ;
    source->filter_id = theIUnderFilterId(filter) ;
    source->close_source = (isICST(filter) != 0) ;

    theIUnderFile(filter) = NULL ;
    theIUnderFilterId(filter) = 0 ;
    ClearICSTFlag(filter) ;

    if ( (theo = match[match_InputBits].result) != NULL &&
         oType(*theo) != OINTEGER && theLen(*theo) > i ) {
      theo = &oArray(*theo)[i] ;
      if ( oType(*theo) != OINTEGER )
        return error_handler(TYPECHECK) ;
      input_bits = oInteger(*theo) ;
    }
    if ( input_bits < 1 )
      return error_handler(RANGECHECK) ;
    source->bits_in = CAST_SIGNED_TO_UINT32(input_bits) ;
    window_size += input_bits ;

    if ( (theo = match[match_InputAlignRepeat].result) != NULL &&
         oType(*theo) != OINTEGER && theLen(*theo) > i ) {
      theo = &oArray(*theo)[i] ;
      if ( oType(*theo) != OINTEGER )
        return error_handler(TYPECHECK) ;
      in_align_count = oInteger(*theo) ;
    }
    if ( in_align_count < 0 )
      return error_handler(RANGECHECK) ;
    source->in_align_count = CAST_SIGNED_TO_UINT32(in_align_count) ;

    if ( (theo = match[match_InputAlignBits].result) != NULL &&
         oType(*theo) != OINTEGER && theLen(*theo) > i ) {
      theo = &oArray(*theo)[i] ;
      if ( oType(*theo) != OINTEGER )
        return error_handler(TYPECHECK) ;
      in_align_bits = oInteger(*theo) ;
    }
    if ( in_align_bits < 1 )
      return error_handler(RANGECHECK) ;
    source->in_align_bits = CAST_SIGNED_TO_UINT32(in_align_bits) ;
    { /* Convert from bit alignment to number of bits to drop. It doesn't
         matter if the calculation is wrong for in_align_count == 0, because
         the result is not used. */
      int32 alignment = (input_bits * in_align_count) % in_align_bits ;
      if ( alignment != 0 )
        alignment = in_align_bits - alignment ;
      source->in_align_bits = CAST_SIGNED_TO_UINT32(alignment) ;
    }

    /* Computed values */
    source->residual_in = 0 ;
    source->residual_in_bits = 0 ;
    source->in_align_left = source->in_align_count ;
  }

  /* default is no output alignment */
  state->out_align_count = 0 ;
  if ( (theo = match[match_OutputAlignRepeat].result) != NULL ) {
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    state->out_align_count = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }

  /* default output alignment size is one byte. */
  state->out_align_bits = 8 ;
  if ( (theo = match[match_OutputAlignBits].result) != NULL ) {
    if ( oInteger(*theo) < 1 )
      return error_handler(RANGECHECK) ;

    state->out_align_bits = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }
  { /* Convert from bit alignment to number of bits to drop. It doesn't
       matter if the calculation is wrong for out_align_count == 0, because
       the result is not used. */
    int32 alignment = (window_size * state->out_align_count) % state->out_align_bits ;
    if ( alignment != 0 )
      alignment = state->out_align_bits - alignment ;
    state->out_align_bits = CAST_SIGNED_TO_UINT32(alignment) ;
  }

  /* default is infinite repeats */
  state->repeats = 0 ;
  if ( (theo = match[match_Repeat].result) != NULL ) {
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    state->repeats = CAST_SIGNED_TO_UINT32(oInteger(*theo)) ;
  }

  /* Compute remaining fields of state. */
  state->residual_out = 0 ;
  state->residual_out_bits = 0 ;
  state->out_align_left = state->out_align_count ;
  state->repeats_left = state->repeats ;

  /* We accumulated the number of bits for each composite in buffsize. Taking
     into account how many repeats there are until an output alignment, find
     out how many bytes it takes to store an output window. */
  if ( state->out_align_count > 0 )
    window_size *= state->out_align_count ;
  window_size += state->out_align_bits ;
  window_size = (window_size + 7) >> 3 ; /* bits to bytes */
  state->out_window = window_size ;

  buffsize = INTERLEAVEBUFFSIZE ;
  if ( buffsize < window_size )
    buffsize = window_size ;

  theIBuffer(filter) = ( uint8 * )mm_alloc(mm_pool_temp ,
                                           buffsize + 1,
                                           MM_ALLOC_CLASS_INTERLEAVE_BUFFER) ;
  if ( theIBuffer(filter) == NULL )
    return error_handler(VMERROR) ;

  theIBufferSize(filter) = buffsize ;
  theIPtr(filter) = ++theIBuffer(filter) ;
  theICount(filter) = 0 ;

  theIFilterState(filter) = FILTER_INIT_STATE ;
  NAME_OBJECT(state, INTERLEAVE_MULTI_NAME) ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}

static void interleaveMultiFilterDispose(FILELIST *filter)
{
  HQASSERT(filter, "filter NULL for InterleaveDecode") ;
  HQASSERT(isIInputFile(filter), "InterleaveDecode should be an input filter") ;

  if ( theIBuffer(filter) ) {
    mm_free(mm_pool_temp, (mm_addr_t)(theIBuffer(filter) - 1),
            theIBufferSize(filter) + 1) ;
    theIBuffer(filter) = NULL ;
  }

  if ( theIFilterPrivate(filter) ) {
    interleave_multi_t *state = theIFilterPrivate(filter) ;
    uint32 i ;

    /* Close all of the underlying sources we've been told to close */
    for ( i = 0 ; i < state->n_sources ; ++i ) {
      multi_source_t *source = &state->sources[i] ;
      if ( source->close_source &&
           isIOpenFileFilterById(source->filter_id, source->flptr) )
        (void)theIMyCloseFile(source->flptr)(source->flptr, CLOSE_EXPLICIT) ;
    }

    UNNAME_OBJECT(state) ;

    mm_free(mm_pool_temp, (mm_addr_t)state,
            offsetof(interleave_multi_t, sources[0]) + sizeof(multi_source_t) * state->n_sources) ;
    theIFilterPrivate(filter) = NULL ;
  }
}

static int32 interleaveMultiFilterFill(FILELIST *filter)
{
  interleave_multi_t *state ;
  register uint8 *ptr ;
  register uint32 n_sources, residual_out, residual_out_bits, out_align_left ;
  uint8 *limit ;
  uint32 repeats_left ;
  int32 result ;

  HQASSERT(filter, "filter NULL in interleaveMultiDecode.") ;

  if ( isIEof(filter) )
    return EOF ;

  ptr = theIPtr(filter) = theIBuffer(filter) ;
  HQASSERT(ptr, "No buffer pointer in interleaveMultiDecode.") ;

  state = theIFilterPrivate(filter) ;
  VERIFY_OBJECT(state, INTERLEAVE_MULTI_NAME) ;

  n_sources = state->n_sources ;

  residual_out = state->residual_out ;
  residual_out_bits = state->residual_out_bits ;
  HQASSERT(residual_out_bits < 8, "Residual output bits more than a byte") ;

  /* Simplify counting in loop by letting these counts wrap around for the
     infinity cases. */
  repeats_left = state->repeats_left ;
  out_align_left = state->out_align_left ;

  if ( state->repeats > 0 && repeats_left == 0 )
    goto endoffile ;

  /* Process a single output window at a time until the buffer is full. */
  limit = ptr + theIBufferSize(filter) - state->out_window ;
  while ( ptr <= limit ) {
    register uint32 count ;

    for ( count = 0 ; count < n_sources ; ++count ) {
      register multi_source_t *source = &state->sources[count] ;
      register int32 c ;
      register int32 residual_in, residual_in_bits, input_bits, input_mask ;

      /* Residual input from the last window is stored in right-aligned in
         residual_in. When there is residual input, the first bit in the input
         stream is the highest identified by residual_in_bits. */
      residual_in = source->residual_in ;
      residual_in_bits = source->residual_in_bits ;
      HQASSERT(residual_in_bits < 8 && residual_in_bits >= 0,
               "Residual input bits don't fit in a byte") ;

      /* Transfer as many whole bytes as we can from input to output. */
      for ( input_bits = source->bits_in ; input_bits >= 8 ; input_bits -= 8 ) {
        if ( (c = Getc(source->flptr)) == EOF )
          goto endoffile ;

        /* We know we have at least one byte to transfer after this. */
        residual_in <<= 8 ;
        residual_in |= c ;

        residual_out <<= 8 ;
        residual_out |= (uint8)(residual_in >> residual_in_bits) ;

        *ptr++ = (uint8)(residual_out >> residual_out_bits) ;
      }

      /* Accumulate sample value into residual_in, which may temporarily
         exceed a byte. */
      if ( residual_in_bits < input_bits ) {
        if ( (c = Getc(source->flptr)) == EOF )
          goto endoffile ;
        residual_in <<= 8 ;
        residual_in |= c ;
        residual_in_bits += 8 ;
      }

      /* Create a mask for the valid input bits */
      input_mask = (1u << input_bits) - 1 ;

      /* Pass sample stored in residual_in to output. */
      residual_in_bits -= input_bits ;

      residual_out <<= input_bits ;
      residual_out |= (residual_in >> residual_in_bits) & input_mask ;
      residual_out_bits += input_bits ;

      /* Flush a byte to output if possible. */
      if ( residual_out_bits >= 8 ) {
        residual_out_bits -= 8 ;
        *ptr++ = (uint8)(residual_out >> residual_out_bits) ;
      }

      HQASSERT(residual_in_bits < 8 && residual_in_bits >= 0,
               "Residual input bits don't fit in a byte") ;
      HQASSERT(residual_out_bits < 8, "Residual output bits more than a byte") ;

      /* Determine if we should align the input data. */
      if ( source->in_align_count > 0 && --source->in_align_left == 0 ) {
        residual_in_bits -= source->in_align_bits ;
        while ( residual_in_bits < 0 ) {
          if ( (residual_in = Getc(source->flptr)) == EOF )
            goto endoffile ;
          residual_in_bits += 8 ;
        }

        source->in_align_left = source->in_align_count ;
      }

      source->residual_in = residual_in ;
      source->residual_in_bits = residual_in_bits ;
    }

    /* Determine if we should align the output data. */
    if ( --out_align_left == 0 ) {
      register uint32 out_align_bits = state->out_align_bits ;

      residual_out_bits += out_align_bits ;
      if ( residual_out_bits < 8 ) {
        /* Didn't align enough bits to make a byte, so we just need to
           shift the residual output over. */
        residual_out <<= out_align_bits ;
      } else {
        /* The alignment will fill a byte. Align the residual output so first
           bit is high bit. The shift is calculated by undoing the addition
           of the aligment bit count, and determining how many bits were left
           to fill in a byte. */
        residual_out <<= 8 - residual_out_bits + out_align_bits ;
        do {
          *ptr++ = (uint8)residual_out ;
          residual_out = 0 ;
          residual_out_bits -= 8 ;
        } while ( residual_out_bits > 8 ) ;
      }

      out_align_left = state->out_align_count ;
    }

    /* If that was the last window, we're finished. */
    if ( --repeats_left == 0 )
      break ;
  }

  state->residual_out = residual_out ;
  state->residual_out_bits = residual_out_bits ;

  /* Only update the counts if we should have done anything with them in the
     first place. If not, they've have wrapped around, but we don't care. */
  if ( state->repeats > 0 )
    state->repeats_left = repeats_left ;

  if ( state->out_align_left > 0 )
    state->out_align_left = out_align_left ;

 endoffile:
  if ( ptr == theIBuffer(filter) ) {
    HQASSERT(residual_out_bits < 8,
             "InterleaveDecode EOF with more than a byte of output left") ;
    if ( residual_out_bits > 0 ) {
      /* EOF was seen but some leftover output bits are already decided.
         Flush them to the output, aligned so the first bit is high bit. */
      *ptr++ = (uint8)(residual_out << (8 - residual_out_bits)) ;
    }
  }

  HQASSERT(ptr - theIBuffer(filter) <= theIBufferSize(filter),
           "Buffer overrun in InterleaveMultiDecode") ;

  if ( ptr > theIBuffer(filter) ) {
    /* Something was read, so return first byte of buffer and update start. */
    result = *theIPtr(filter)++ ;
  } else { /* Nothing read, must be at EOF */
    SetIEofFlag(filter) ;
    result = EOF ;
  }

  /* Set remaining bytes in buffer after extraction of return byte. */
  theICount(filter) = CAST_PTRDIFFT_TO_INT32(ptr - theIPtr(filter)) ;

  return result ;
}

void interleave_multi_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH(INTERLEAVE_MULTI_NAME) ,
                       FILTER_FLAG | READ_FLAG,
                       0, NULL, 0,
                       interleaveMultiFilterFill,            /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       interleaveMultiFilterInit,            /* initfile */
                       FilterCloseFile,                      /* closefile */
                       interleaveMultiFilterDispose,         /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

/* Log stripped */
