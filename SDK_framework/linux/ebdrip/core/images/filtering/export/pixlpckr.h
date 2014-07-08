/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!export:pixlpckr.h(EBDSDK_P.1) $
 * $Id: export:pixlpckr.h,v 1.7.7.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PixelPacker performs conversions between different types of image data.
 * It's output is always interleaved.
 *
 * It currently supports:
 * 1 bit interleaved/separated     to      8 bit interleaved
 * 2 bit interleaved/separated     to      8 bit interleaved
 * 4 bit interleaved/separated     to      8 bit interleaved
 * 8 bit interleaved/separated     to      8 bit interleaved
 * 8 bit interleaved               to      1 bit interleaved (output is the
 *                                         result of source > 0 test)
 * 12 bit interleaved/separated    to      8 bit interleaved (least significant 4
 *                                         bits discarded)
 * 12 bit interleaved/separated    to      16 bit interleaved (12-bit range
 *                                         expanded to 16-bit range)
 * 16 bit interleaved/separated    to      8 bit interleaved (least significant
 *                                         byte discarded)
 * 16 bit interleaved/separated    to      16 bit interleaved (source bytes swapped
 *                                         into 16-bit uints)
 * 16 bit interleaved              to      12 bit interleaved (12-bit range
 *                                         expanded to 16-bit range)
 * 16 bit interleaved              to      1 bit interleaved (output is the result
 *                                         of source > 0 test)
 *
 * To use this, simply set the source, then call the PixelPackFunction obtained
 * via getPacker().
 *
 * The data is packed into containers, which will be "sourceCount" (set in a
 * call to new()) pixels appart. This allows data to be interleaved, by calling
 * the pack function repeatedly with an offset target, eg.
 *
 * pp = pixelPackerNew(1, 8, 3);
 * func = pixelPackerGetPacker(pp);
 *
 * for (i = 0; i < 3; i ++)
 * {
 *   pixelPackerSetSource(pp, sources[i], i);
 *   func(pp, target + i, width);
 * }
 */
#ifndef __PIXELPACKER_H__
#define __PIXELPACKER_H__

#include "swvalues.h"

/* Pixel Packer */
typedef struct PixelPacker_s PixelPacker;

/* Work function interfaces */

/**
 * Pack 'count' items of source data into 'target'. See file comment above for
 * interleaving usage example.
 * Returns the number of bytes written.
 */
typedef uint32 (*PixelPackFunction)(PixelPacker* self,
                                    uint8* source,
                                    uint8* target,
                                    uint32 count);

/**
 * Mask Pack function; produce "count" items of 1-bit data, incrementing the
 * read head by "step" items (either 1 or two, for 8 or 16 bits) after each
 * conversion.
 * Designed for type 3, interleave 1 masked images.
 * Returns the number of bytes written.
 */
typedef uint32 (*PixelPackWithMaskFunction)(PixelPacker* self,
                                            uint8* source, uint8* target,
                                            uint32 count, uint32 step);

/**
 * Constructor.
 *
 * \param sourceCount The number of separate sources of input data; if the
 * source data is interleaved this should be 1. Otherwise the output data will
 * be written into containers 'sourceCount' entries apart.
 */
PixelPacker* pixelPackerNew(uint32 sourcebpc,
                            uint32 targetBpc,
                            uint32 sourceCount);

/**
 * Destructor.
 */
void pixelPackerDelete(PixelPacker* this);

/**
 * Return the pack work function.
 */
PixelPackFunction pixelPackerGetPacker(PixelPacker* this);

/**
 * Return the mask pack work function.
 */
PixelPackWithMaskFunction pixelPackerGetMaskPacker(PixelPacker* this);

#endif

/* Log stripped */
