/** \file
 * \ingroup hqbits
 *
 * $HopeName: HQNc-standard!export:hqbitops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2012 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Common byte and bitwise operations with optimised versions for various
 * architectures.
 */

#ifndef __HQBITOPS_H__
#define __HQBITOPS_H__

/**
 * \defgroup hqbits Harlequin standard bit and byte operations
 * \ingroup cstandard
 * \{
 */

/*@+fcnmacros -incondefs@*/

/****************************************************************************/
/* Loading from memory. */

/** Load a 16-bit unsigned little-endian number from memory. */
#define BYTE_LOAD16_UNSIGNED_LE(ptr_) \
  (uint16)(((uint8 *)(ptr_))[0] | (((uint8 *)(ptr_))[1] << 8))

/** Load a 16-bit unsigned big-endian number from memory. */
#define BYTE_LOAD16_UNSIGNED_BE(ptr_) \
  (uint16)(((uint8 *)(ptr_))[1] | (((uint8 *)(ptr_))[0] << 8))

/** Load a 16-bit unsigned platform-endian number from memory. */
#ifdef highbytefirst
#define BYTE_LOAD16_UNSIGNED_PLATFORM BYTE_LOAD16_UNSIGNED_BE
#else
#define BYTE_LOAD16_UNSIGNED_PLATFORM BYTE_LOAD16_UNSIGNED_LE
#endif

/** Load a 16-bit signed little-endian number from memory. */
#define BYTE_LOAD16_SIGNED_LE(ptr_) (int16)BYTE_LOAD16_UNSIGNED_LE(ptr_)

/** Load a 16-bit signed big-endian number from memory. */
#define BYTE_LOAD16_SIGNED_BE(ptr_) (int16)BYTE_LOAD16_UNSIGNED_BE(ptr_)

/** Load a 16-bit signed platform-endian number from memory. */
#ifdef highbytefirst
#define BYTE_LOAD16_SIGNED_PLATFORM BYTE_LOAD16_SIGNED_BE
#else
#define BYTE_LOAD16_SIGNED_PLATFORM BYTE_LOAD16_SIGNED_LE
#endif

/*--------------------------------------------------------------------------*/

/** Load a 32-bit unsigned little-endian number from memory. */
#define BYTE_LOAD32_UNSIGNED_LE(ptr_)                                   \
  ((uint32)(((uint8 *)(ptr_))[0]) |                                     \
   ((uint32)(((uint8 *)(ptr_))[1]) <<  8) |                             \
   ((uint32)(((uint8 *)(ptr_))[2]) << 16) |                             \
   ((uint32)(((uint8 *)(ptr_))[3]) << 24))

/** Load a 32-bit unsigned big-endian number from memory. */
#define BYTE_LOAD32_UNSIGNED_BE(ptr_)                                   \
  ((uint32)(((uint8 *)(ptr_))[3]) |                                     \
   ((uint32)(((uint8 *)(ptr_))[2]) <<  8) |                             \
   ((uint32)(((uint8 *)(ptr_))[1]) << 16) |                             \
   ((uint32)(((uint8 *)(ptr_))[0]) << 24))

/** Load a 32-bit unsigned platform-endian number from memory. */
#ifdef highbytefirst
#define BYTE_LOAD32_UNSIGNED_PLATFORM BYTE_LOAD32_UNSIGNED_BE
#else
#define BYTE_LOAD32_UNSIGNED_PLATFORM BYTE_LOAD32_UNSIGNED_LE
#endif

/** Load a 32-bit signed little-endian number from memory. */
#define BYTE_LOAD32_SIGNED_LE(ptr_) (int32)BYTE_LOAD32_UNSIGNED_LE(ptr_)

/** Load a 32-bit signed big-endian number from memory. */
#define BYTE_LOAD32_SIGNED_BE(ptr_)  (int32)BYTE_LOAD32_UNSIGNED_BE(ptr_)

/** Load a 32-bit signed platform-endian number from memory. */
#ifdef highbytefirst
#define BYTE_LOAD32_SIGNED_PLATFORM BYTE_LOAD32_SIGNED_BE
#else
#define BYTE_LOAD32_SIGNED_PLATFORM BYTE_LOAD32_SIGNED_LE
#endif

/*--------------------------------------------------------------------------*/

/** Load a 64-bit unsigned little-endian number from memory. */
#define BYTE_LOAD64_UNSIGNED_LE(ptr_)       \
  ((uint64)(((uint8 *)(ptr_))[0])         | \
   ((uint64)(((uint8 *)(ptr_))[1]) <<  8) | \
   ((uint64)(((uint8 *)(ptr_))[2]) << 16) | \
   ((uint64)(((uint8 *)(ptr_))[3]) << 24) | \
   ((uint64)(((uint8 *)(ptr_))[4]) << 32) | \
   ((uint64)(((uint8 *)(ptr_))[5]) << 40) | \
   ((uint64)(((uint8 *)(ptr_))[6]) << 48) | \
   ((uint64)(((uint8 *)(ptr_))[7]) << 56))

/** Load a 64-bit unsigned big-endian number from memory. */
#define BYTE_LOAD64_UNSIGNED_BE(ptr_)       \
  ((uint64)(((uint8 *)(ptr_))[7])         | \
   ((uint64)(((uint8 *)(ptr_))[6]) <<  8) | \
   ((uint64)(((uint8 *)(ptr_))[5]) << 16) | \
   ((uint64)(((uint8 *)(ptr_))[4]) << 24) | \
   ((uint64)(((uint8 *)(ptr_))[3]) << 32) | \
   ((uint64)(((uint8 *)(ptr_))[2]) << 40) | \
   ((uint64)(((uint8 *)(ptr_))[1]) << 48) | \
   ((uint64)(((uint8 *)(ptr_))[0]) << 56))

/** Load a 64-bit unsigned platform-endian number from memory. */
#ifdef highbytefirst
#define BYTE_LOAD64_UNSIGNED_PLATFORM BYTE_LOAD64_UNSIGNED_BE
#else
#define BYTE_LOAD64_UNSIGNED_PLATFORM BYTE_LOAD64_UNSIGNED_LE
#endif

/** Load a 64-bit signed little-endian number from memory. */
#define BYTE_LOAD64_SIGNED_LE(ptr_) (int64)BYTE_LOAD64_UNSIGNED_LE(ptr_)

/** Load a 64-bit signed big-endian number from memory. */
#define BYTE_LOAD64_SIGNED_BE(ptr_) (int64)BYTE_LOAD64_UNSIGNED_BE(ptr_)

/** Load a 64-bit signed platform-endian number from memory. */
#ifdef highbytefirst
#define BYTE_LOAD64_SIGNED_PLATFORM BYTE_LOAD64_SIGNED_BE
#else
#define BYTE_LOAD64_SIGNED_PLATFORM BYTE_LOAD64_SIGNED_LE
#endif


/****************************************************************************/
/* Storing to memory. */

/** Store a 16-bit little-endian number to memory. */
#define BYTE_STORE16_LE(ptr_, val_) MACRO_START                         \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ;                             \
  register uint16 _val_ = (uint16)(val_) ;                              \
  _ptr_[0] = (uint8)_val_ ;                                             \
  _ptr_[1] = (uint8)(_val_ >> 8) ;                                      \
MACRO_END

/** Store a 16-bit big-endian number to memory. */
#define BYTE_STORE16_BE(ptr_, val_) MACRO_START                         \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ;                             \
  register uint16 _val_ = (uint16)(val_) ;                              \
  _ptr_[1] = (uint8)_val_ ;                                             \
  _ptr_[0] = (uint8)(_val_ >> 8) ;                                      \
MACRO_END

/** Store a 16-bit platform-endian number to memory. */
#ifdef highbytefirst
#define BYTE_STORE16_PLATFORM BYTE_STORE16_BE
#else
#define BYTE_STORE16_PLATFORM BYTE_STORE16_LE
#endif

/*--------------------------------------------------------------------------*/

/** Store a 32-bit little-endian number to memory. */
#define BYTE_STORE32_LE(ptr_, val_) MACRO_START                         \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ;                             \
  register uint32 _val_ = (uint32)(val_) ;                              \
  _ptr_[0] = (uint8)_val_ ;                                             \
  _ptr_[1] = (uint8)(_val_ >> 8) ;                                      \
  _ptr_[2] = (uint8)(_val_ >> 16) ;                                     \
  _ptr_[3] = (uint8)(_val_ >> 24) ;                                     \
MACRO_END

/** Store a 32-bit big-endian number to memory. */
#define BYTE_STORE32_BE(ptr_, val_) MACRO_START                         \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ;                             \
  register uint32 _val_ = (uint32)(val_) ;                              \
  _ptr_[3] = (uint8)_val_ ;                                             \
  _ptr_[2] = (uint8)(_val_ >> 8) ;                                      \
  _ptr_[1] = (uint8)(_val_ >> 16) ;                                     \
  _ptr_[0] = (uint8)(_val_ >> 24) ;                                     \
MACRO_END

/** Store a 32-bit platform-endian number to memory. */
#ifdef highbytefirst
#define BYTE_STORE32_PLATFORM BYTE_STORE32_BE
#else
#define BYTE_STORE32_PLATFORM BYTE_STORE32_LE
#endif

/*--------------------------------------------------------------------------*/

/** Store a 64-bit little-endian number to memory. */
#define BYTE_STORE64_LE(ptr_, val_) MACRO_START                         \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ;                             \
  register uint64 _val_ = (uint64)(val_) ;                              \
  _ptr_[0] = (uint8)_val_ ;                                             \
  _ptr_[1] = (uint8)(_val_ >> 8) ;                                      \
  _ptr_[2] = (uint8)(_val_ >> 16) ;                                     \
  _ptr_[3] = (uint8)(_val_ >> 24) ;                                     \
  _ptr_[4] = (uint8)(_val_ >> 32) ;                                     \
  _ptr_[5] = (uint8)(_val_ >> 40) ;                                     \
  _ptr_[6] = (uint8)(_val_ >> 48) ;                                     \
  _ptr_[7] = (uint8)(_val_ >> 56) ;                                     \
MACRO_END

/** Store a 64-bit big-endian number to memory. */
#define BYTE_STORE64_BE(ptr_, val_) MACRO_START                         \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ;                             \
  register uint64 _val_ = (uint64)(val_) ;                              \
  _ptr_[7] = (uint8)_val_ ;                                             \
  _ptr_[6] = (uint8)(_val_ >> 8) ;                                      \
  _ptr_[5] = (uint8)(_val_ >> 16) ;                                     \
  _ptr_[4] = (uint8)(_val_ >> 24) ;                                     \
  _ptr_[3] = (uint8)(_val_ >> 32) ;                                     \
  _ptr_[2] = (uint8)(_val_ >> 40) ;                                     \
  _ptr_[1] = (uint8)(_val_ >> 48) ;                                     \
  _ptr_[0] = (uint8)(_val_ >> 56) ;                                     \
MACRO_END

/** Store a 64-bit platform-endian number to memory. */
#ifdef highbytefirst
#define BYTE_STORE64_PLATFORM BYTE_STORE64_BE
#else
#define BYTE_STORE64_PLATFORM BYTE_STORE64_LE
#endif


/****************************************************************************/
/* Useful masks for sub-unit extraction. */

/** All bytes in an unsigned type set to 0x01. */
#define MASK_BYTES_1(type_) (type_)(~(type_)0 / 0xffu)

/** All shorts in an unsigned type set to 0x0001. */
#define MASK_SHORTS_1(type_) (type_)(~(type_)0 / 0xffffu)

/** All words in an unsigned type set to 0x00000001. */
#define MASK_WORDS_1(type_) (type_)(~(type_)0 / 0xffffffffu)

/** Alternating bytes in an unsigned type set to 0x00,0xff. */
#define MASK_ALTERNATE_BYTES(type_) (type_)(MASK_SHORTS_1(type_) * 0xffu)

/** Alternating shorts in an unsigned type set to 0x0000,0xffff. */
#define MASK_ALTERNATE_SHORTS(type) (type_)(MASK_WORDS_1(type_) * 0xffffu)


/****************************************************************************/
/* Byte swapping. */

/** Swap bytes in a 16-bit value, returning an unsigned 16-bit value. */
#define BYTE_SWAP16_UNSIGNED(val_) \
  (uint16)((uint8)(val_) << 8 | (uint16)(val_) >> 8)

/** Swap bytes in a 16-bit value, returning a signed 16-bit value. */
#define BYTE_SWAP16_SIGNED(val_) (int16)BYTE_SWAP16_UNSIGNED(val_)

/** Swap bytes in a 16-bit value in memory. */
#define BYTE_SWAP16_PTR(ptr_) MACRO_START   \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ; \
  register uint8 _tmp_ = _ptr_[0] ;         \
  _ptr_[0] = _ptr_[1] ;                     \
  _ptr_[1] = _tmp_ ;                        \
MACRO_END

/** Swap bytes in a 16-bit value in a variable. */
#define BYTE_SWAP16_UNSIGNED_VAR(to_, from_) MACRO_START                \
  register uint16 _tmp_ = (uint16)(from_) ;                             \
  to_ = (uint16)((_tmp_ << 8) | (_tmp_ >> 8)) ;                         \
MACRO_END

/** Swap multiple 16-bit values in a buffer. Source and destination buffers
    can be the same. */
#define BYTE_SWAP16_BUFFER(to_, from_, bytes_) MACRO_START   \
  register uint8 *_from_ = (uint8 *)(from_) ;                \
  register uint8 *_end_ = _from_ + (uint32)(bytes_) ;        \
  register uint8 *_to_ = (uint8 *)(to_) ;                    \
  while ( _from_ < _end_ ) {                                 \
    uint8 _tmp_ = _from_[0] ;                                \
    _to_[0] = _from_[1] ;                                    \
    _to_[1] = _tmp_ ;                                        \
    _from_ += 2 ;                                            \
    _to_ += 2 ;                                              \
  }                                                          \
  HQASSERT(_from_ == _end_, "Ran off end of swap buffer") ;  \
MACRO_END

/*--------------------------------------------------------------------------*/

/** Swap bytes in a 32-bit value, returning an unsigned 32-bit value. */
#define BYTE_SWAP32_UNSIGNED(val_) \
  (uint32)((uint32)(val_) << 24 | (uint32)(val_) >> 24 | \
           ((uint32)(val_) & 0xff00u) << 8 | (((uint32)(val_) >> 8) & 0xff00u))

/** Swap bytes in a 32-bit value, returning a signed 32-bit value. */
#define BYTE_SWAP32_SIGNED(val_) (int32)BYTE_SWAP32_UNSIGNED(val_)

/** Swap bytes in a 32-bit value in memory. */
#define BYTE_SWAP32_PTR(ptr_) MACRO_START   \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ; \
  register uint8 _tmp_ = _ptr_[0] ;         \
  _ptr_[0] = _ptr_[3] ;                     \
  _ptr_[3] = _tmp_ ;                        \
  _tmp_ = _ptr_[1] ;                        \
  _ptr_[1] = _ptr_[2] ;                     \
  _ptr_[2] = _tmp_ ;                        \
MACRO_END

/** Swap bytes in a 32-bit value in a variable. */
#define BYTE_SWAP32_UNSIGNED_VAR(to_, from_) MACRO_START                \
  register uint32 _tmp_ = (uint32)(from_) ;                             \
  _tmp_ = (_tmp_ << 16) | (_tmp_ >> 16) ;                               \
  to_ = (((_tmp_ & MASK_ALTERNATE_BYTES(uint32)) << 8) |                \
         ((_tmp_ >> 8) & MASK_ALTERNATE_BYTES(uint32))) ;               \
MACRO_END

/** Swap multiple 32-bit values in a buffer. Source and destination buffers
    can be the same. */
#define BYTE_SWAP32_BUFFER(to_, from_, bytes_) MACRO_START    \
  register uint32 *_from_ = (uint32 *)(from_) ;               \
  register uint32 *_end_ = (uint32 *)((uint8 *)_from_ + (uint32)(bytes_)) ; \
  register uint32 *_to_ = (uint32 *)(to_) ;                   \
  while ( _from_ < _end_ ) {                                  \
    BYTE_SWAP32_UNSIGNED_VAR(_to_[0], _from_[0]) ;            \
    ++_from_ ;                                                \
    ++_to_ ;                                                  \
  }                                                           \
  HQASSERT(_from_ == _end_, "Ran off end of swap buffer") ;   \
MACRO_END

/** Swap multiple 32-bit values in a buffer, which may be unaligned. Source
    and destination buffers can be the same. */
#define BYTE_SWAP32_BUFFER_UNALIGNED(to_, from_, bytes_) MACRO_START    \
  register uint8 *_from_ = (uint8 *)(from_) ;                           \
  register uint8 *_end_ = _from_ + (uint32)(bytes_) ;                   \
  register uint8 *_to_ = (uint8 *)(to_) ;                               \
  while ( _from_ < _end_ ) {                                            \
    uint8 _tmp_ = _from_[0] ;                                           \
    _to_[0] = _from_[3] ;                                               \
    _to_[3] = _tmp_ ;                                                   \
    _tmp_ = _from_[1] ;                                                 \
    _to_[1] = _from_[2] ;                                               \
    _to_[2] = _tmp_ ;                                                   \
    _from_ += 4 ;                                                       \
    _to_ += 4 ;                                                         \
  }                                                                     \
  HQASSERT(_from_ == _end_, "Ran off end of swap buffer") ;             \
MACRO_END

/*--------------------------------------------------------------------------*/

/** Swap bytes in a 64-bit value, returning an unsigned 64-bit value. */
#define BYTE_SWAP64_UNSIGNED(val_) \
  (uint64)((uint64)(val_) << 56 | (uint64)(val_) >> 56 | \
           ((uint64)(val_) & 0xff00u) << 40 | \
             (((uint64)(val_) >> 40) & 0xff00u) | \
           ((uint64)(val_) & 0xff0000u) << 24 | \
             (((uint64)(val_) >> 24) & 0xff0000u) | \
           ((uint64)(val_) & 0xff000000u) << 8 | \
             (((uint64)(val_) >> 8) & 0xff000000u))

/** Swap bytes in a 64-bit value, returning a signed 64-bit value. */
#define BYTE_SWAP64_SIGNED(val_) (int64)BYTE_SWAP64_UNSIGNED(val_)

/** Swap bytes in a 64-bit value in memory. */
#define BYTE_SWAP64_PTR(ptr_) MACRO_START         \
  register uint8 *_ptr_ = (uint8 *)(ptr_) ;       \
  uint64 _tmp_ = BYTE_LOAD64_UNSIGNED_LE(_ptr_) ; \
  BYTE_STORE64_BE(_ptr_, _tmp_) ;                 \
MACRO_END

/** Swap bytes in a 64-bit value in a variable. */
#define BYTE_SWAP64_UNSIGNED_VAR(to_, from_) MACRO_START                \
  register uint64 _tmp_ = (uint64)(from_) ;                             \
  _tmp_ = (_tmp_ << 32) | (_tmp_ >> 32) ;                               \
  _tmp_ = (((_tmp_ & MASK_ALTERNATE_SHORTS(uint64)) << 16) |            \
           ((_tmp_ >> 16) & MASK_ALTERNATE_SHORTS(uint64))) ;           \
  to_ = (((_tmp_ & MASK_ALTERNATE_BYTES(uint64)) << 8) |                \
         ((_tmp_ >> 8) & MASK_ALTERNATE_BYTES(uint64))) ;               \
MACRO_END

/** Swap multiple 64-bit values in a buffer. Source and destination buffers
    can be the same. */
#define BYTE_SWAP64_BUFFER(to_, from_, bytes_) MACRO_START    \
  register uint64 *_from_ = (uint64 *)(from_) ;               \
  register uint64 *_end_ = (uint64 *)((uint8 *)_from_ + (uint32)(bytes_)) ; \
  register uint64 *_to_ = (uint64 *)(to_) ;                   \
  while ( _from_ < _end_ ) {                                  \
    BYTE_SWAP64_UNSIGNED_VAR(_to_[0], _from_[0]) ;            \
    ++_from_ ;                                                \
    ++_to_ ;                                                  \
  }                                                           \
  HQASSERT(_from_ == _end_, "Ran off end of swap buffer") ;   \
MACRO_END

/** Swap multiple 64-bit values in a buffer, which may be unaligned. Source
    and destination buffers can be the same. */
#define BYTE_SWAP64_BUFFER_UNALIGNED(to_, from_, bytes_) MACRO_START  \
  register uint8 *_from_ = (uint8 *)(from_) ;                \
  register uint8 *_end_ = _from_ + (uint32)(bytes_) ;        \
  register uint8 *_to_ = (uint8 *)(to_) ;                    \
  while ( _from_ < _end_ ) {                                 \
    uint64 _tmp_ = BYTE_LOAD64_UNSIGNED_LE(_from_) ;         \
    BYTE_STORE64_BE(_to_, _tmp_) ;                           \
    _from_ += 8 ;                                            \
    _to_ += 8 ;                                              \
  }                                                          \
  HQASSERT(_from_ == _end_, "Ran off end of swap buffer") ;  \
MACRO_END

/*--------------------------------------------------------------------------*/
/* Optimised swaps for particular compilers/platforms. */

#if defined(_MSC_VER)

#undef BYTE_SWAP16_PTR
#define BYTE_SWAP16_PTR(ptr_) \
  _swab((char *)(ptr_), (char *)(ptr_), 2)

#undef BYTE_SWAP16_BUFFER
#define BYTE_SWAP16_BUFFER(to_, from_, bytes_) \
  _swab((char *)(from_), (char *)(to_), (int)(bytes_))
#endif /* _MSC_VER */

#ifdef Unaligned_32bit_access

#undef BYTE_SWAP32_PTR
#define BYTE_SWAP32_PTR(ptr_) MACRO_START     \
  register uint32 *_ptr_ = (uint32 *)(ptr_) ; \
  BYTE_SWAP32_UNSIGNED_VAR(_ptr_[0], _ptr_[0]) ; \
MACRO_END

#undef BYTE_SWAP32_BUFFER_UNALIGNED
#define BYTE_SWAP32_BUFFER_UNALIGNED BYTE_SWAP32_BUFFER

#undef BYTE_SWAP64_PTR
#define BYTE_SWAP64_PTR(ptr_) MACRO_START     \
  register uint64 *_ptr_ = (uint64 *)(ptr_) ; \
  BYTE_SWAP64_UNSIGNED_VAR(_ptr_[0], _ptr_[0]) ; \
MACRO_END

#undef BYTE_SWAP64_BUFFER_UNALIGNED
#define BYTE_SWAP64_BUFFER_UNALIGNED BYTE_SWAP64_BUFFER

#endif /* Unaligned_32bit_access */

/****************************************************************************/
/* Sign extraction. These macros are used by the right shift, abs, and min
   macros. They extract and manipulate the sign bits, without using branches. */

/** Extract the sign bit, returning 0 for non-negative 32-bit numbers and -1
    for negative numbers. This is useful as a mask. The casts around the macro
    parameter are to ensure sign extension from any supplied integer. */
#define SIGN32_NEG(x) -(int32)((uint32)((int32)(x)) >> 31)

/** Extract the sign bit, returning 0 for non-negative 64-bit numbers and -1
    for negative numbers. This is useful as a mask. The casts around the macro
    parameter are to ensure sign extension from any supplied integer. */
#define SIGN64_NEG(x) -(int64)((uint64)((int64)(x)) >> 63)

/** Extract the sign of a 32-bit integer as -1, 0, or +1. */
#define SIGN32(x) (SIGN32_NEG(x) - SIGN32_NEG(-(x)))

/** Extract the sign of a 64-bit integer as -1, 0, or +1. */
#define SIGN64(x) (SIGN64_NEG(x) - SIGN64_NEG(-(x)))

/*--------------------------------------------------------------------------*/
/* Optimised versions of sign extraction macros. Some compilers default to
   signed shift right, so allow these to be used. */

#if defined(RIGHT_SHIFT_IS_SIGNED)

#undef SIGN32_NEG
#define SIGN32_NEG(x) ((int32)(x) >> 31)

#undef SIGN64_NEG
#define SIGN64_NEG(x) ((int64)(x) >> 63)

#endif /* RIGHT_SHIFT_IS_SIGNED */

/****************************************************************************/
/* Bit rotation. We only bother with 32 and 64-bit rotation, the primary
   use for this is bit blitting. */

/** Rotate bits left in a 32-bit value. This is NOT safe for zero shifts. */
#define BIT_ROTATE32_LEFT(to_, from_, shift_) MACRO_START               \
  (to_) = ((from_) << (shift_)) | ((uint32)(from_) >> (32 - (shift_))) ; \
MACRO_END

/** Rotate bits right in a 32-bit value. This is NOT safe for zero shifts. */
#define BIT_ROTATE32_RIGHT(to_, from_, shift_) MACRO_START              \
  (to_) = ((from_) >> (shift_)) | ((uint32)(from_) << (32 - (shift_))) ; \
MACRO_END

/** Rotate bits left in a 32-bit value. This IS safe for zero shifts. */
#define BIT_ROTATE32_LEFT_SAFE(to_, from_, shift_) MACRO_START          \
  if ( (shift_) != 0 )                                                  \
    BIT_ROTATE32_LEFT((to_), (from_), (shift_)) ;                       \
  else                                                                  \
    (to_) = (from_) ;                                                   \
MACRO_END

/** Rotate bits left in a 32-bit value. This IS safe for zero shifts. */
#define BIT_ROTATE32_RIGHT_SAFE(to_, from_, shift_) MACRO_START         \
  if ( (shift_) != 0 )                                                  \
    BIT_ROTATE32_RIGHT((to_), (from_), (shift_)) ;                      \
  else                                                                  \
    (to_) = (from_) ;                                                   \
MACRO_END

/*--------------------------------------------------------------------------*/

/** Rotate bits left in a 64-bit value. This is NOT safe for zero shifts. */
#define BIT_ROTATE64_LEFT(to_, from_, shift_) MACRO_START               \
  (to_) = ((from_) << (shift_)) | ((uint64)(from_) >> (64 - (shift_))) ; \
MACRO_END

/** Rotate bits right in a 64-bit value. This is NOT safe for zero shifts. */
#define BIT_ROTATE64_RIGHT(to_, from_, shift_) MACRO_START              \
  (to_) = ((uint64)(from_) >> (shift_)) | ((uint64)(from_) << (64 - (shift_))) ; \
MACRO_END

/** Rotate bits left in a 64-bit value. This IS safe for zero shifts. */
#define BIT_ROTATE64_LEFT_SAFE(to_, from_, shift_) MACRO_START          \
  if ( (shift_) != 0 )                                                  \
    BIT_ROTATE64_LEFT((to_), (from_), (shift_)) ;                       \
  else                                                                  \
    (to_) = (from_) ;                                                   \
MACRO_END

/** Rotate bits left in a 64-bit value. This IS safe for zero shifts. */
#define BIT_ROTATE64_RIGHT_SAFE(to_, from_, shift_) MACRO_START         \
  if ( (shift_) != 0 )                                                  \
    BIT_ROTATE64_RIGHT((to_), (from_), (shift_)) ;                      \
  else                                                                  \
    (to_) = (from_) ;                                                   \
MACRO_END

/*--------------------------------------------------------------------------*/
/* Optimised versions of the rotate functions. */

#if defined(Can_Shift_32)

#undef BIT_ROTATE32_LEFT_SAFE
#define BIT_ROTATE32_LEFT_SAFE BIT_ROTATE32_LEFT

#undef BIT_ROTATE32_RIGHT_SAFE
#define BIT_ROTATE32_RIGHT_SAFE BIT_ROTATE32_RIGHT

#undef BIT_ROTATE64_LEFT_SAFE
#define BIT_ROTATE64_LEFT_SAFE BIT_ROTATE64_LEFT

#undef BIT_ROTATE64_RIGHT_SAFE
#define BIT_ROTATE64_RIGHT_SAFE BIT_ROTATE64_RIGHT

#endif /* Can_Shift_32 */

/* Even more optimised versions of the rotate functions, using compiler
   instrinsics. */

#if defined(_MSC_VER)

#undef BIT_ROTATE32_LEFT
#define BIT_ROTATE32_LEFT(to_, from_, shift_) MACRO_START \
  (to_) = _rotl((uint32)(from_), (shift_)) ;              \
MACRO_END

#undef BIT_ROTATE32_LEFT_SAFE
#define BIT_ROTATE32_LEFT_SAFE(to_, from_, shift_) MACRO_START \
  (to_) = _rotl((uint32)(from_), (shift_)) ;                   \
MACRO_END

#undef BIT_ROTATE32_RIGHT
#define BIT_ROTATE32_RIGHT(to_, from_, shift_) MACRO_START \
  (to_) = _rotr((uint32)(from_), (shift_)) ;               \
MACRO_END

#undef BIT_ROTATE32_RIGHT_SAFE
#define BIT_ROTATE32_RIGHT_SAFE(to_, from_, shift_) MACRO_START \
  (to_) = _rotr((uint32)(from_), (shift_)) ;                    \
MACRO_END

#endif /* _MSC_VER */

/****************************************************************************/
/* Bit shifting. We only bother with arithmetic right shift, because all of
   the other shifts are well defined in C. */

/** Arithmetic shift right in a 32-bit value, suitable for use in an
    expression. This is safe for zero shifts (because the left shift of the
    negative sign by 31 ^ shift will set the same sign bit as is currently
    set). */
#define BIT_SHIFT32_SIGNED_RIGHT_EXPR(val_, shift_) \
  ((int32)((uint32)(val_) >> (shift_)) | (SIGN32_NEG(val_) << (31 ^ (shift_))))

/** Arithmetic shift right in a 32-bit value. This is safe for zero shifts
    (because the left shift of the negative sign by 31 ^ shift will set the
    same sign bit as is currently set). */
#define BIT_SHIFT32_SIGNED_RIGHT(to_, from_, shift_) MACRO_START        \
  register int32 _f_ = (int32)(from_) ;                                 \
  (to_) = BIT_SHIFT32_SIGNED_RIGHT_EXPR(_f_, (shift_)) ;                \
MACRO_END

/*--------------------------------------------------------------------------*/

/** Arithmetic shift right in a 64-bit value, suitable for use in an
    expression. This is safe for zero shifts (because the left shift of the
    negative sign by 63 ^ shift will set the same sign bit as is currently
    set). */
#define BIT_SHIFT64_SIGNED_RIGHT_EXPR(val_, shift_) \
  ((int64)((uint64)(val_) >> (shift_)) | (SIGN64_NEG(val_) << (63 ^ (shift_))))

/** Arithmetic shift right in a 64-bit value. This is safe for zero shifts
    (because the left shift of the negative sign by 63 ^ shift will set the
    same sign bit as is currently set). */
#define BIT_SHIFT64_SIGNED_RIGHT(to_, from_, shift_) MACRO_START        \
  register int64 _f_ = (int64)(from_) ;                                 \
  (to_) = BIT_SHIFT64_SIGNED_RIGHT_EXPR(_f_, (shift_)) ;                \
MACRO_END

/*--------------------------------------------------------------------------*/
/* Optimised versions of shift functions. Some compilers default to signed
   shift right, so allow these to be used. */

#if defined(RIGHT_SHIFT_IS_SIGNED)

#undef BIT_SHIFT32_SIGNED_RIGHT_EXPR
#define BIT_SHIFT32_SIGNED_RIGHT_EXPR(val_, shift_) \
  ((int32)(val_) >> (shift_))

#undef BIT_SHIFT64_SIGNED_RIGHT_EXPR
#define BIT_SHIFT64_SIGNED_RIGHT_EXPR(val_, shift_) \
  ((int64)(val_) >> (shift_))

#endif /* RIGHT_SHIFT_IS_SIGNED */

/****************************************************************************/
/* Versions of min/max/abs/range clip for 32-bit and 64-bit integers, using
   bitwise operations only, without branches. */

/** Get the minimum of two 32-bit integers without branches. This only works
    when the absolute difference between the values is less than MAXINT32. */
#define INLINE_MIN32(min_, a_ ,b_) MACRO_START                          \
  register int32 _d_ = (b_) - (a_) ;                                    \
  HQASSERT((a_) > (b_) ? _d_ < 0 : _d_ >= 0,                            \
           "Difference between integers too large for in-line min") ;   \
  _d_ &= SIGN32_NEG(_d_) ;                                              \
  (min_) = (a_) + _d_ ;                                                 \
MACRO_END

/** Get the maximum of two 32-bit integers without branches. This only works
    when the absolute difference between the values is less than MAXINT32. */
#define INLINE_MAX32(max_, a_ ,b_) MACRO_START                          \
  register int32 _d_ = (b_) - (a_) ;                                    \
  HQASSERT((a_) > (b_) ? _d_ < 0 : _d_ >= 0,                            \
           "Difference between integers too large for in-line max") ;   \
  _d_ &= SIGN32_NEG(_d_) ;                                              \
  (max_) = (b_) - _d_ ;                                                 \
MACRO_END

/** Get the minimum and maximum of two 32-bit integers without branches. This
    only works when the absolute difference between the values is less than
    MAXINT32. */
#define INLINE_MINMAX32(min_, max_, a_ ,b_) MACRO_START                 \
  register int32 _d_ = (b_) - (a_) ;                                    \
  HQASSERT((a_) > (b_) ? _d_ < 0 : _d_ >= 0,                            \
           "Difference between integers too large for in-line minmax") ; \
  _d_ &= SIGN32_NEG(_d_) ;                                              \
  (min_) = (a_) + _d_ ;                                                 \
  (max_) = (b_) - _d_ ;                                                 \
MACRO_END

/** Get the absolute value of a 32-bit integer without branches. */
#define INLINE_ABS32(abs_, val_) MACRO_START \
  register int32 _s_ = SIGN32_NEG(val_);     \
  (abs_) = ((int32)(val_) ^ _s_) - _s_ ;     \
MACRO_END

/** Range clip a 32-bit integer to minimum and maximum limits. This only
    works when the absolute difference between the values is less than
    MAXINT32. */
#define INLINE_RANGE32(to_, from_, min_, max_) MACRO_START       \
  register int32 _t_ ;                                           \
  HQASSERT((min_) <= (max_), "Range clip limits out of order") ; \
  INLINE_MIN32(_t_, (from_), (max_)) ;                           \
  INLINE_MAX32((to_), _t_, (min_)) ;                             \
MACRO_END

/** Range clip a 32-bit integer to (0,maximum) limits. */
#define INLINE_RANGE32_0(to_, from_, max_) MACRO_START       \
  register int32 _t_ = (int32)(from_) ;                      \
  HQASSERT((max_) >= 0, "Range clip limit too small") ;      \
  _t_ &= (int32)((uint32)_t_ >> 31) - 1 ; /* t = max(0,t) */ \
  INLINE_MIN32((to_), _t_, (max_)) ;                         \
MACRO_END

/*--------------------------------------------------------------------------*/

/** Get the minimum of two 64-bit integers without branches. This only works
    when the absolute difference between the values is less than MAXINT64. */
#define INLINE_MIN64(min_, a_ ,b_) MACRO_START                          \
  register int64 _d_ = (b_) - (a_) ;                                    \
  HQASSERT((a_) > (b_) ? _d_ < 0 : _d_ >= 0,                            \
           "Difference between integers too large for in-line min") ;   \
  _d_ &= SIGN64_NEG(_d_) ;                                              \
  (min_) = (a_) + _d_ ;                                                 \
MACRO_END

/** Get the maximum of two 64-bit integers without branches. This only works
    when the absolute difference between the values is less than MAXINT64. */
#define INLINE_MAX64(max_, a_ ,b_) MACRO_START                          \
  register int64 _d_ = (b_) - (a_) ;                                    \
  HQASSERT((a_) > (b_) ? _d_ < 0 : _d_ >= 0,                            \
           "Difference between integers too large for in-line max") ;   \
  _d_ &= SIGN64_NEG(_d_) ;                                              \
  (max_) = (b_) - _d_ ;                                                 \
MACRO_END

/** Get the minimum and maximum of two 64-bit integers without branches. This
    only works when the absolute difference between the values is less than
    MAXINT64. */
#define INLINE_MINMAX64(min_, max_, a_ ,b_) MACRO_START                 \
  register int64 _d_ = (b_) - (a_) ;                                    \
  HQASSERT((a_) > (b_) ? _d_ < 0 : _d_ >= 0,                            \
           "Difference between integers too large for in-line minmax") ; \
  _d_ &= SIGN64_NEG(_d_) ;                                              \
  (min_) = (a_) + _d_ ;                                                 \
  (max_) = (b_) - _d_ ;                                                 \
MACRO_END

/** Get the absolute value of a 64-bit integer without branches. */
#define INLINE_ABS64(abs_, val_) MACRO_START \
  register int64 _s_ = SIGN64_NEG(val_);     \
  (abs_) = ((int64)(val_) ^ _s_) - _s_ ;     \
MACRO_END

/** Range clip a 64-bit integer to minimum and maximum limits. This only
    works when the absolute difference between the values is less than
    MAXINT64. */
#define INLINE_RANGE64(to_, from_, min_, max_) MACRO_START       \
  register int64 _t_ ;                                           \
  HQASSERT((min_) <= (max_), "Range clip limits out of order") ; \
  INLINE_MIN64(_t_, (from_), (max_)) ;                           \
  INLINE_MAX64((to_), _t_, (min_)) ;                             \
MACRO_END

/** Range clip a 64-bit integer to (0,maximum) limits. */
#define INLINE_RANGE64_0(to_, from_, max_) MACRO_START       \
  register int64 _t_ = (int64)(from_) ;                      \
  HQASSERT((max_) >= 0, "Range clip limit too small") ;      \
  _t_ &= (int64)((uint64)_t_ >> 63) - 1 ; /* t = max(0,t) */ \
  INLINE_MIN64((to_), _t_, (max_)) ;                         \
MACRO_END

/*--------------------------------------------------------------------------*/

/** Masks to the least significant bit set for 32-bit integers. */
#define BIT_FIRST_SET_32(_n_) ( ((uint32) (_n_)) & ~( ((uint32) (_n_)) - 1 ) )

#define BIT_FIRST_CLEAR_32(_n_) ( BIT_FIRST_SET_32( ~(_n_) ) )

#define BIT_AT_MOST_ONE_SET_32(_n_) ( (_n_) == BIT_FIRST_SET_32(_n_) )

#define BIT_EXACTLY_ONE_SET_32(_n_) ( (_n_) != 0 && BIT_AT_MOST_ONE_SET_32(_n_) )

/** Masks to the least significant bit set for 64-bit integers. */
#define BIT_FIRST_SET_64(_n_) ( ((uint64) (_n_)) & ~( ((uint64) (_n_)) - 1 ) )

#define BIT_FIRST_CLEAR_64(_n_) ( BIT_FIRST_SET_64( ~(_n_) ) )

#define BIT_AT_MOST_ONE_SET_64(_n_) ( (_n_) == BIT_FIRST_SET_64(_n_) )

#define BIT_EXACTLY_ONE_SET_64(_n_) ( (_n_) != 0 && BIT_AT_MOST_ONE_SET_64(_n_) )

/** Index of least significant bit set of a 32 bit integer. */
#define _DEBRUIJN_SEQUENCE_32 (0x077CB531U)
#define BIT_FIRST_SET_INDEX_32(_n_)  \
 ("\000\001\034\002\035\016\030\003\036\026\024\017\031\021\004\010\037\033\015\027\025\023\020\007\032\014\022\006\013\005\012\011"[(BIT_FIRST_SET_32(_n_) * _DEBRUIJN_SEQUENCE_32) >> 27])

/** Index of least significant bit set of a 64-bit integer. */
#define _DEBRUIJN_SEQUENCE_64 UINT64(0x218A392CD3D5DBF)
#define BIT_FIRST_SET_INDEX_64(_n_) \
 ("\000\001\002\007\003\015\010\023\004\031\016\034\011\042\024\050\005\021\032\046\017\056\035\060\012\037\043\066\025\062\051\071\077\006\014\022\030\033\041\047\020\045\055\057\036\065\061\070\076\013\027\040\044\054\064\067\075\026\053\063\074\052\073\072"[(BIT_FIRST_SET_64(_n_) * _DEBRUIJN_SEQUENCE_64) >> 58])

/****************************************************************************/
/* Legacy defines. Do NOT add any new defines that are either unsized or not
   applicable to all integral types. */

/* Bitwise operations, assuming 2s complement, preferably unsigned
 * bits are numbered from 0, from the least significant bit.
 */

/* The number of bits in a byte */
#define BITS_BYTE 8

/* The number of bits in an integer type */
#define BITS( _type_ ) ( sizeof( _type_ ) * BITS_BYTE )

/* _i_th bit, k&r version cannot be evaluated by preprocessor eg #if */
#ifdef USE_TRADITIONAL
#define BIT( _i_ ) ( ( (uint32) 0x1 ) << (_i_) )
#else
#define BIT( _i_ ) ( ( 0x1u ) << (_i_) )
#endif

/* all bits set, k&r version cannot be evaluated by preprocessor eg #if */
#ifdef USE_TRADITIONAL
#define BITS_ALL ( ~ (uint32) 0x0 )
#else
#define BITS_ALL ( ~ 0x0ul )
#endif

/* bits from _i_ onwards inclusively, ie half open interval ( _i_,  ] */
#define BITS_STARTING_AT( _i_ ) ( BITS_ALL << ( _i_ ) )

/* bits below i exclusively, ie half open interval [ 0, _i_ ) */
#define BITS_BELOW( _i_ ) ( ~ BITS_STARTING_AT( _i_ ) )

/* bits in half open interval [ _i_, _j_ ) */
#define BITS_FROM_TO_BEFORE( _i_, _j_ ) \
 ( BITS_STARTING_AT( _i_ ) & BITS_BELOW( _j_ ) )


/* Masks to the least significant bit set */
#define BIT_FIRST_SET(_n_) BIT_FIRST_SET_32(_n_)

#define BIT_FIRST_CLEAR(_n_) BIT_FIRST_CLEAR_32(_n_)

#define BIT_AT_MOST_ONE_SET(_n_) BIT_AT_MOST_ONE_SET_32(_n_)

#define BIT_EXACTLY_ONE_SET(_n_) BIT_EXACTLY_ONE_SET_32(_n_)

#define BIT_FIRST_SET_INDEX(_n_) BIT_FIRST_SET_INDEX_32(_n_)

/** \} */

#endif  /* __HQBITOPS_H__ */

/*@=fcnmacros =incondefs@*/

