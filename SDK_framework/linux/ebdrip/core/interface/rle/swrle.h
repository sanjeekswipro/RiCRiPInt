/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_rle!swrle.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * This file provides the information needed to interpret run-length output
 * from the pagebuffer device.
 */

#ifndef SWRLE_H
#define SWRLE_H

#ifdef __cplusplus
extern "C" {
#endif

                              /* bits
                                  0 -  5: record type (all cases) */
#define RUN_SIMPLE       0u
                              /*  8 - 15: tone-index (6 - 15 for 10-bit case)
                                          or first color-value (color RLE)
                                 16 - 31: repeat-count */
#define RUN_REPEAT       1u
                              /* 6 - 7: (unused) 8 - 31: run-count

                                        (mono only: then run-count pairs of
                                        tone-index, repeat-count bytes packed two
                                        pairs to a 32-bit word)

                                        (color RLE only: run-count groups of 10 bit
                                        fields packed into bytes 20-29, 10-19 and 0-9
                                        for as many words as necessary, where the
                                        first field of each run (subject to bits
                                        30-31, see below) is a 6-bit repeat count
                                        stored in the least significant bits of the
                                        10-bit field, and the remaining fields supply
                                        color-values for each explicit colorant of
                                        the current set of colorants given by
                                        RUN_COLORANTS. For those words where a new
                                        run starts, bits 30-31 are interpreted as:
                                          0 - repeat count omitted, use the same as
                                          the previous one,
                                          1 - repeat count is omitted, it is one
                                          greater than the previous one,
                                          2: repeat count is present,
                                          3 - repeat count is one less than the
                                          previous one)

                                        There are special cases for one and two
                                        component colors - see manual.
                                   */
#define RUN_POSITION     2u
                              /*  8 - 31: position */
#define RUN_SCREEN       3u
                              /*  8 - 31: screen-index */
#define RUN_HEADER       4u
                              /*  8 - 15: flag-value (must be set to zero when
                                          block is free)
                                 16 - 31: block length in bytes */
#define RUN_END_OF_LINE  5u
                              /*  8 - 31: reserved */
#define RUN_NO_OP        6u
                              /*  8 - 31: reserved */
#define RUN_REPEAT_COPY  8u
                              /*  8 - 23: reserved
                                 24 - 31: repeat id */
#define RUN_REPEAT_STORE 9u
                              /*  8 - 23: pair-count (then pair-count pairs
                                          as for RUN_REPEAT)
                                 24 - 31: repeat id */
#define RUN_SCREEN_STORE 10u
                              /*  8 - 23: screen index
                                 24 - 31: number of words of data to follow
                                          (this data is hardware specific) */

#define RUN_COLORANTS    11u  /*  (color RLE only)
                                  6     : (unused)
                                  7     : overprint (1) or knockout (0) implicit colorants
                                 29 - 31: number of colorant-numbers (color components)
                                 26 - 28: colorant-number
                                 23 - 25: colorant-number ... */

#define RUN_COLORANTS_LONG 12u
                              /*  (color RLE only)
                                  6     : (unused)
                                  7     : overprint (1) or knockout (0) implicit colorants
                                  8 -  9: (unused)
                                 10 - 19: first colorant-number
                                 20 - 29: number of colorant-numbers (color componments)
                                 30 - 31: (unused)
                                          Followed by as many words as are needed to
                                          store remaining colorant-numbers, packed 3
                                          to a word in bits 20-29, 10-19 and 0-9 in
                                          that order */

#define RUN_INFO         13u  /*  6     : (unused)
                                  7     : re-use bit
                                  8 - 11: count
                                 12 - 15: type
                                 16 - 31: identifier
                                          Followed by as many words as required for
                                          information of type type, except when
                                          re-use is set when the consumer should
                                          refer to the previous value for the given
                                          identifier */

#define RUN_PARTIAL_OVERPRINT 14u
                              /* as per RUN_COLORANTS (use the same masks and
                                 shifts), but numbers identify colorants partially
                                 overprinted. /RunPartialOverprint true required to
                                 receive this opcode */

#define RUN_PARTIAL_OVERPRINT_LONG 15u
                              /* as per RUN_COLORANTS_LONG (use the same masks and
                                 shifts), but numbers identify colorants partially
                                 overprinted. /RunPartialOverprint true required to
                                 receive this opcode */

#define RUN_OBJECT_TYPE 16u /* Specifies the type of object (or objects, in the
                               case of a composited span) from which this span
                               originated. Plus the rendering intent. */

#define RUN_TRANSPARENT 17u /* Specifies the alpha, blend mode and soft mask ID
                            in transparent RLE. */

#define RUN_SOFTMASK 18u /* Specifies a line of softmask alpha data. */

#define RUN_GROUP_OPEN 19u /* Opens a new group in transparent RLE. */

#define RUN_GROUP_CLOSE 20u /* Close a group in transparent RLE. */

#define MAX_RUN_OPCODE 20u

/* maximum block size in 32 bit words */
#define RLE_BLOCK_SIZE_WORDS            258

/* Some utility values to help decoding */

#define RLE_ALPHA_MAX                   1023u
#define RLE_COLORANTS_MAX               1023u

#define RLE_MASK_RECORD_TYPE            0x3Fu

#define RLE_MASK_HEADER_CLEAR_FLAG      0xFFFF00FFu

#define RLE_SHIFT_HEADER_LENGTH         16
#define RLE_MASK_HEADER_LENGTH          ((0xFFFFu << RLE_SHIFT_HEADER_LENGTH))

#define RLE_SHIFT_POSITION              8
#define RLE_MASK_POSITION               ((0xFFFFFFu << RLE_SHIFT_POSITION))

#define RLE_SHIFT_SIMPLE_TONE8          8
#define RLE_MASK_SIMPLE_TONE8           ((0xFFu << RLE_SHIFT_SIMPLE_TONE8))
#define RLE_SHIFT_SIMPLE_TONE10         6
#define RLE_MASK_SIMPLE_TONE10          ((0x3FFu << RLE_SHIFT_SIMPLE_TONE10))
#define RLE_SHIFT_SIMPLE_REPEAT         16
#define RLE_MASK_SIMPLE_REPEAT          ((0xFFFFu << RLE_SHIFT_SIMPLE_REPEAT))
#define RLE_SHIFT_SIMPLE_COLOR0         6
#define RLE_MASK_SIMPLE_COLOR0          ((0x3FFu << RLE_SHIFT_SIMPLE_COLOR0))

#define RLE_SHIFT_DECILE                20
#define RLE_MASK_DECILE                 ((0x3FFu << RLE_SHIFT_DECILE))
#define RLE_SHIFTNEXT_DECILE            10

#define RLE_SHIFT_COLORANTS_OVERPRINT   7
#define RLE_MASK_COLORANTS_OVERPRINT    ((0x1u << RLE_SHIFT_COLORANTS_OVERPRINT))

#define RLE_SHIFT_COLORANTS_COUNT       29
#define RLE_MASK_COLORANTS_COUNT        ((0x7u << RLE_SHIFT_COLORANTS_COUNT))

#define RLE_SHIFT_COLORANTS_N           26
#define RLE_MASK_COLORANTS_N            ((0x7u << RLE_SHIFT_COLORANTS_N))
#define RLE_SHIFTNEXT_COLORANTS         3

#define RLE_SHIFT_INFO_REUSE            7
#define RLE_MASK_INFO_REUSE             (0x1u << RLE_SHIFT_INFO_REUSE)
#define RLE_SHIFT_INFO_COUNT            8
#define RLE_LEN_INFO_COUNT              4
#define RLE_MASK_INFO_COUNT             (((1u << RLE_LEN_INFO_COUNT) -1) << RLE_SHIFT_INFO_COUNT)
#define RLE_SHIFT_INFO_TYPE             12
#define RLE_LEN_INFO_TYPE               4
#define RLE_MASK_INFO_TYPE              (((1u << RLE_LEN_INFO_TYPE) -1) << RLE_SHIFT_INFO_TYPE)
#define RLE_SHIFT_INFO_ID               16
#define RLE_LEN_INFO_ID                 16
#define RLE_MASK_INFO_ID                (((1u << RLE_LEN_INFO_ID) -1) << RLE_SHIFT_INFO_ID)

#define RLE_SHIFT_REPEAT_RUNCOUNT       8
#define RLE_MASK_REPEAT_RUNCOUNT        (0xFFFFu << RLE_SHIFT_REPEAT_RUNCOUNT)
#define RLE_SHIFT_REPEAT_REPEATCONTROL  30
#define RLE_MASK_REPEAT_REPEATCONTROL   (0x3u << RLE_SHIFT_REPEAT_REPEATCONTROL)
#define RLE_SHIFT_REPEAT_TONE8          24
#define RLE_MASK_REPEAT_TONE8           (0xFFu << RLE_SHIFT_REPEAT_TONE8)
#define RLE_SHIFT_REPEAT_TONE10         22
#define RLE_MASK_REPEAT_TONE10          (0x3FFu << RLE_SHIFT_REPEAT_TONE10)
#define RLE_SHIFT_REPEAT_COUNT8         16
#define RLE_MASK_REPEAT_COUNT8          (0xFFu << RLE_SHIFT_REPEAT_COUNT8)
#define RLE_SHIFT_REPEAT_COUNT10        16
#define RLE_MASK_REPEAT_COUNT10         (0x3Fu << RLE_SHIFT_REPEAT_COUNT10)
#define RLE_SHIFT_REPEAT_ID             24
#define RLE_MASK_REPEAT_ID              (0xFFu << RLE_SHIFT_REPEAT_ID)
#define RLE_SHIFTNEXT_REPEAT            16

#define RLE_SHIFT_SCREEN_ID             8
#define RLE_MASK_SCREEN_ID              ((0xFFFFFFu << RLE_SHIFT_SCREEN_ID))

#define RLE_SHIFT_OBJECT_TYPE           8
#define RLE_MASK_OBJECT_TYPE            (0xff << RLE_SHIFT_OBJECT_TYPE)

#define RLE_SHIFT_RENDERING_INTENT      16
#define RLE_MASK_RENDERING_INTENT       (0xff << RLE_SHIFT_RENDERING_INTENT)

#define RLE_EXTRACT(word_,shift_,mask_) (((word_) & (mask_)) >> (shift_))

/* Object type bit positions for RUN_OBJECT_TYPE. */
#define RLE_EMPTY_OBJECT                (0) /* RUN_OBJECT_TYPE value, not a bitmask */
#define RLE_USER_OBJECT                 (1 << 0)
#define RLE_NAMEDCOLOR_OBJECT           (1 << 1)
#define RLE_BLACK_OBJECT                (1 << 2)
#define RLE_LW_OBJECT                   (1 << 3)
#define RLE_TEXT_OBJECT                 (1 << 4)
#define RLE_VIGNETTE_OBJECT             (1 << 5)
#define RLE_IMAGE_OBJECT                (1 << 6)
#define RLE_COMPOSITED_OBJECT           (1 << 7)

#ifdef PLATFORM_IS_64BIT
#define POINTER_SIZE_IN_WORDS 2
#else /* i.e. PLATFORM_IS_32BIT */
#define POINTER_SIZE_IN_WORDS 1
#endif

/** Write the pointer to the next block to the start of the passed block.

  The size of this pointer is specified by POINTER_SIZE_IN_WORDS. */
#define /* void */ RLEBLOCK_SET_NEXT(/* uint32* */ block_, /* uint32* */ next_) { \
  uint32** link_ = (uint32**)(block_); \
  *link_ = next_; \
}

/** Given a pointer to a block, return a pointer to the next block. */
#define /* uint32* */ RLEBLOCK_GET_NEXT(/* uint32* */ block_) \
  (*(uint32**)(block_))


#define RLE_GET_RECORD_TYPE(/* uint32 */ recordHeader_) \
  ((recordHeader_) & RLE_MASK_RECORD_TYPE)

/** The macro evaluates to the size of an RLE block header in words - the next
pointer and the RUN_HEADER record together make the header. */
#define /* int32 */ RLEBLOCK_HEADER_SIZE (POINTER_SIZE_IN_WORDS + 1)

/** Return the block header word. */
#define /* uint32 */ RLEBLOCK_GET_HEADER(/* uint32* */ block_) \
  ((block_)[POINTER_SIZE_IN_WORDS])

/** Return a pointer to the contents of the block. */
#define /* uint32* */ RLEBLOCK_GET_CONTENTS(/* uint32* */ block_) \
  ((block_) + RLEBLOCK_HEADER_SIZE)

/** Return the size of the passed block, in bytes, including the size of the
header word. */
#define /* int32 */ RLEBLOCK_GET_SIZE(/* uint32* */ block_) \
  (int32)((block_)[POINTER_SIZE_IN_WORDS] >> 16)

#ifdef __cplusplus
}
#endif


#endif /* SWRLE_H */
