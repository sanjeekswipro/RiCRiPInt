/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:pixlpckr.c(EBDSDK_P.1) $
 * $Id: src:pixlpckr.c,v 1.14.4.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Data normaliser - given an input and output pixel format, the PixelPacker
 * converts between the two
 */

#include "core.h"
#include "pixlpckr.h"

#include "objnamer.h"
#include "hqassert.h"
#include "mm.h"
#include "swerrors.h"
#include "hqmemcpy.h"

/* --Private macros-- */

#define PIXELPACKER_NAME "Imagefilter pixel packer"
#define MAX_8_BIT_VALUE (255)
#define MAX_16_BIT_VALUE (65535)

/* --Private datatypes-- */

struct PixelPacker_s {
  uint8 *decodeTable;
  uint32 sourceBpc;
  uint32 targetBpc;
  uint32 sourceCount;
  size_t decodeSize;
  PixelPackFunction workFunc;
  PixelPackWithMaskFunction maskWorkFunc;
  OBJECT_NAME_MEMBER
};

/* --Private prototypes-- */

static Bool createExpansionTable(PixelPacker* self);
static void chooseWorkFunction(PixelPacker* self);

static uint32 pack16BitTo16Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack12BitTo16Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack16BitTo8Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack12BitTo8Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack8BitTo8Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack4BitTo8Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack2BitTo8Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack1BitTo8Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack8BitTo1Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);
static uint32 pack16BitTo12Bit(PixelPacker* self, uint8* source, uint8* target, uint32 count);

static uint32 maskPack8Bit(PixelPacker* self,
                           uint8* source,
                           uint8* target,
                           uint32 count,
                           uint32 step);
static uint32 maskPack16Bit(PixelPacker* self,
                            uint8* source,
                            uint8* target,
                            uint32 count,
                            uint32 step);

/* --Public methods-- */

/* Construtor
 */
PixelPacker* pixelPackerNew(uint32 sourceBpc,
                            uint32 targetBpc,
                            uint32 sourceCount)
{
  PixelPacker *self;

  HQASSERT((sourceBpc == 1) || (sourceBpc == 2) || (sourceBpc == 4) ||
           (sourceBpc == 8) || (sourceBpc == 12) || (sourceBpc == 16),
           "pixelPackerNew - Bad bitdepth");
  HQASSERT((targetBpc == 1) || (targetBpc == 8) || (targetBpc == 12) ||
           (targetBpc == 16),
           "pixelPackerNew - Bad targetBpc, must be 8 or 16");
  HQASSERT(sourceCount > 0,
           "pixelPackerNew - Bad componentCount, must be greater than zero");

  self = (PixelPacker*) mm_alloc(mm_pool_temp, sizeof(PixelPacker),
                                 MM_ALLOC_CLASS_PIXEL_PACKER);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  NAME_OBJECT(self, PIXELPACKER_NAME);

  /* NULL pointer members */
  self->decodeTable = NULL;

  self->sourceBpc = sourceBpc;
  self->targetBpc = targetBpc;
  self->sourceCount = sourceCount;

  chooseWorkFunction(self);

  if (createExpansionTable(self))
    return self;

  /* Unsuccessful initialisation */
  pixelPackerDelete(self);
  return NULL;
}

/* Destructor
 */
void pixelPackerDelete(PixelPacker* self)
{
  /* It's not an error to try free a null object */
  if (self == NULL) {
    return;
  }

  VERIFY_OBJECT(self, PIXELPACKER_NAME);
  UNNAME_OBJECT(self);

  if (self->decodeTable != NULL) {
    mm_free(mm_pool_temp, self->decodeTable, self->decodeSize);
  }

  mm_free(mm_pool_temp, self, sizeof(PixelPacker));
}

/* Get the function that will do the packing for this PixelPacker
 */
PixelPackFunction pixelPackerGetPacker(PixelPacker* self)
{
  VERIFY_OBJECT(self, PIXELPACKER_NAME);

  return self->workFunc;
}

/* Get the function that will do the mask packing for this PixelPacker
 */
PixelPackWithMaskFunction pixelPackerGetMaskPacker(PixelPacker* self)
{
  VERIFY_OBJECT(self, PIXELPACKER_NAME);

  return self->maskWorkFunc;
}


/* --Private methods-- */

/* Choose an appropriate function to to the required source to target
 * conversion
 */
void chooseWorkFunction(PixelPacker* self)
{
  /* Decide which function to use to pack the data */
  switch (self->targetBpc) {
    case 1:
      switch (self->sourceBpc) {
        case 8:
          self->workFunc = pack8BitTo1Bit;
          break;
        default:
          HQFAIL("chooseWorkFunction - unhandled source/target combination");
      }
      break;
    case 8:
      switch (self->sourceBpc) {
        case 1:
          self->workFunc = pack1BitTo8Bit;
          break;
        case 2:
          self->workFunc = pack2BitTo8Bit;
          break;
        case 4:
          self->workFunc = pack4BitTo8Bit;
          break;
        case 8:
          self->workFunc = pack8BitTo8Bit;
          break;
        case 12:
          self->workFunc = pack12BitTo8Bit;
          break;
        case 16:
          self->workFunc = pack16BitTo8Bit;
          break;
        default:
          HQFAIL("chooseWorkFunction - unhandled source/target combination");
      }
      break;
    case 12:
      switch (self->sourceBpc) {
        case 16:
          self->workFunc = pack16BitTo12Bit;
          break;
        default:
          HQFAIL("chooseWorkFunction - unhandled source/target combination");
      }
      break;
    case 16:
      switch (self->sourceBpc) {
        case 12:
          self->workFunc = pack12BitTo16Bit;
          break;
        case 16:
          self->workFunc = pack16BitTo16Bit;
          break;

        default:
          HQFAIL("chooseWorkFunction - unhandled source/target combination");
      }
      break;
    default:
      HQFAIL("chooseWorkFunction - unhandled source/target combination");
  }

  /* Choose the mask pack function */
  switch (self->targetBpc) {
    case 8:
      self->maskWorkFunc = maskPack8Bit;
      break;

    case 16:
      self->maskWorkFunc = maskPack16Bit;
      break;

    default:
      self->maskWorkFunc = NULL;
  }
}

/* Create an expansion table which maps source values where bpc < 8 to full 8
bit range, or 12 bit data to 16 bit. 8 and 16 bit data naturally match the
possible outputs of 8 or 16 bit data, and do not need to be expanded.
*/
static Bool createExpansionTable(PixelPacker* self)
{
  self->decodeTable = NULL;

  /* If the bpc is less that 8, create a decode to map to 0 - 255. */
  if (self->sourceBpc < 8) {
    uint32 i, sourceRange;

    HQASSERT(self->targetBpc == 8, "createExpansionTable - target Bpc is not 8");

    sourceRange = 1 << self->sourceBpc;

    self->decodeSize = sizeof(uint8) * sourceRange;
    self->decodeTable = (uint8*) mm_alloc(mm_pool_temp, self->decodeSize,
                                          MM_ALLOC_CLASS_PIXEL_PACKER);
    if (self->decodeTable == NULL)
      return error_handler(VMERROR);

    sourceRange --;
    for (i = 0; i <= sourceRange; i ++)
      self->decodeTable[i] = (uint8)(((i / (double)sourceRange) * 255.0) + 0.5);
  }
  else {
    /* If the bpc is 12, and the target bpc is 16, then create a decode to
    map from 0 - 4095 to 0 - 65535. */
    if (self->sourceBpc == 12 && self->targetBpc == 16) {
      uint32 i;
      uint16* decode;

      self->decodeSize = sizeof(uint16) * 4096;
      self->decodeTable = (uint8*) mm_alloc(mm_pool_temp, self->decodeSize,
                                            MM_ALLOC_CLASS_PIXEL_PACKER);
      if (self->decodeTable == NULL)
        return error_handler(VMERROR);

      decode = (uint16*)self->decodeTable;
      for (i = 0; i < 4096; i ++)
        decode[i] = (uint16)(((i / 4095.0) * 65535.0) + 0.5);
    }
  }

  return TRUE; /* Allocation successful */
}

/* --Pack functions: Produce "count" items of packed data-- */

/* Pack: Source - 16 bit, Target - 16 bit.
 */
static uint32 pack16BitTo16Bit(PixelPacker* self,
                               uint8* source,
                               uint8* target8Bit,
                               uint32 count)
{
  uint16 *target = (uint16*)target8Bit;
  uint32 sourceCount;
  uint32 written = count;

  VERIFY_OBJECT(self, PIXELPACKER_NAME);

  sourceCount = self->sourceCount;

  /* Copy loop without decode */
  while (count >= 4) {
    *target = (uint16)((source[0] << 8) | source[1]);
    target += sourceCount;
    *target = (uint16)((source[2] << 8) | source[3]);
    target += sourceCount;
    *target = (uint16)((source[4] << 8) | source[5]);
    target += sourceCount;
    *target = (uint16)((source[6] << 8) | source[7]);
    target += sourceCount;
    source += 8;
    count -= 4;
  }
  while (count > 0) {
    *target = (uint16)((source[0] << 8) | source[1]);
    target += sourceCount;
    source += 2;
    count --;
  }
  return written;
}

/* Pack: Source - 12 bit, Target - 16 bit
 */
static uint32 pack12BitTo16Bit(PixelPacker* self,
                               uint8* source,
                               uint8* target8Bit,
                               uint32 count)
{
  uint8 temp;
  uint16 *target = (uint16*)target8Bit;
  uint16 *decode;
  uint32 sourceCount;
  uint32 written = count * 2;

  VERIFY_OBJECT(self, PIXELPACKER_NAME);
  sourceCount = self->sourceCount;

  decode = (uint16*)self->decodeTable;

  while ( count >= 8 ) {
    temp = source[1];
    *target = decode[(source[0] << 4) | (temp >> 4)];
    target += sourceCount;
    *target = decode[((temp & 15) << 8) | source[2]];
    target += sourceCount;
    temp = source[4];
    *target = decode[(source[3] << 4) | (temp >> 4)];
    target += sourceCount;
    *target = decode[((temp & 15) << 8) | source[5]];
    target += sourceCount;
    temp = source[7];
    *target = decode[(source[6] << 4) | (temp >> 4)];
    target += sourceCount;
    *target = decode[((temp & 15) << 8) | source[8]];
    target += sourceCount;
    temp = source[10];
    *target = decode[(source[9] << 4) | (temp >> 4)];
    target += sourceCount;
    *target = decode[((temp & 15) << 8) | source[11]];
    target += sourceCount;
    source += 12;
    count -= 8;
  }
  while (count > 0) {
    temp = source[1];
    *target = decode[(source[0] << 4) | (temp >> 4)];
    target += sourceCount;
    if (count == 1) {
      break;
    }
    *target = decode[((temp & 15) << 8) | source[2]];
    target += sourceCount ;
    source += 3;
    count -= 2;
  }
  return written;
}

/* Pack: Source - 16 bit, Target - 8 bit
 */
static uint32 pack16BitTo8Bit(PixelPacker* self,
                              uint8* source,
                              uint8* target,
                              uint32 count)
{
  uint32 sourceCount;
  uint32 written = count;

  VERIFY_OBJECT(self, PIXELPACKER_NAME);

  /* The source data is 16-bit, but the bytes are in the wrong order to
  simply cast the source pointer to a uint16. */
  sourceCount = self->sourceCount;

  while (count >= 8) {
    *target = source[0];
    target += sourceCount;
    *target = source[2];
    target += sourceCount;
    *target = source[4];
    target += sourceCount;
    *target = source[6];
    target += sourceCount;
    *target = source[8];
    target += sourceCount;
    *target = source[10];
    target += sourceCount;
    *target = source[12];
    target += sourceCount;
    *target = source[14];
    target += sourceCount;
    source += 16;
    count -= 8;
  }
  while (count > 0) {
    *target = source[0];
    target += sourceCount;
    source += 2;
    count --;
  }
  return written;
}

/* Pack: Source - 12 bit, Target - 8 bit
 */
static uint32 pack12BitTo8Bit(PixelPacker* self,
                              uint8* source,
                              uint8* target,
                              uint32 count)
{
  uint32 sourceCount;
  uint32 written = count;

  VERIFY_OBJECT(self, PIXELPACKER_NAME);

  sourceCount = self->sourceCount;

  /* Copy loop without decode */
  while ( count >= 8 ) {
    *target = source[0];
    target += sourceCount;
    *target = (uint8)(((source[1] & 15) << 4) | (source[2] >> 4));
    target += sourceCount;
    *target = source[3];
    target += sourceCount;
    *target = (uint8)(((source[4] & 15) << 4) | (source[5] >> 4));
    target += sourceCount;
    *target = source[6];
    target += sourceCount;
    *target = (uint8)(((source[7] & 15) << 4) | (source[8] >> 4));
    target += sourceCount;
    *target = source[9];
    target += sourceCount;
    *target = (uint8)(((source[10] & 15) << 4) | (source[11] >> 4));
    target += sourceCount;
    source += 12;
    count -= 8;
  }
  while (count > 0) {
    *target = source[0];
    target += sourceCount;
    if (count == 1) {
      break;
    }
    *target = (uint8)(((source[1] & 15) << 4) | (source[2] >> 4));
    target += sourceCount;
    source += 3;
    count -= 2;
  }
  return written;
}

/* Pack: Source - 8 bit, Target - 8 bit
 */
static uint32 pack8BitTo8Bit(PixelPacker* self,
                             uint8* source,
                             uint8* target,
                             uint32 count)
{
  uint32 sourceCount;
  uint32 written = count;

  VERIFY_OBJECT(self, PIXELPACKER_NAME);
  sourceCount = self->sourceCount;

  if ( sourceCount == 1 ) {
    HqMemCpy(target, source, count);
    return written;
  }

  /* Copy loop without decode */
  while (count >= 8) {
    *target = source[0];
    target += sourceCount;
    *target = source[1];
    target += sourceCount;
    *target = source[2];
    target += sourceCount;
    *target = source[3];
    target += sourceCount;
    *target = source[4];
    target += sourceCount;
    *target = source[5];
    target += sourceCount;
    *target = source[6];
    target += sourceCount;
    *target = source[7];
    target += sourceCount;
    source += 8;
    count -= 8;
  }
  while (count > 0) {
    *target = *source;
    target += sourceCount;
    source ++;
    count --;
  }
  return written;
}

/* Pack: Source - 4 bit, Target - 8 bit
 */
static uint32 pack4BitTo8Bit(PixelPacker* self,
                             uint8* source,
                             uint8* target,
                             uint32 count)
{
  uint8 *decode;
  uint32 sourceCount;
  uint32 written = count;

  VERIFY_OBJECT(self, PIXELPACKER_NAME);
  HQASSERT((self->decodeTable != NULL), "pack4BitTo8Bit - no valid decode array");

  sourceCount = self->sourceCount;

  decode = self->decodeTable;

  while (count >= 8) {
    *target = decode[(*source >> 4) & 15];
    target += sourceCount;
    *target = decode[*source & 15];
    target += sourceCount;
    source ++;
    *target = decode[(*source >> 4) & 15];
    target += sourceCount;
    *target = decode[*source & 15];
    target += sourceCount;
    source ++;
    *target = decode[(*source >> 4) & 15];
    target += sourceCount;
    *target = decode[*source & 15];
    target += sourceCount;
    source ++;
    *target = decode[(*source >> 4) & 15];
    target += sourceCount;
    *target = decode[*source & 15];
    target += sourceCount;
    source ++;

    count -= 8;
  }
  while (count > 0) {
    *target = decode[(*source >> 4) & 15];
    target += sourceCount;
    if (count == 1) {
      break;
    }

    *target = decode[*source & 15];
    target += sourceCount;

    source ++;
    count -= 2;
  }
  return written;
}

/* Pack: Source - 2 bit, Target - 8 bit
 */
static uint32 pack2BitTo8Bit(PixelPacker* self,
                             uint8* source,
                             uint8* target,
                             uint32 count)
{
  uint8 *decode;
  uint32 sourceCount;
  uint32 written = count;

  VERIFY_OBJECT(self, PIXELPACKER_NAME);
  HQASSERT((self->decodeTable != NULL), "pack2BitTo8Bit - no valid decode array");

  sourceCount = self->sourceCount;

  decode = self->decodeTable;

  while (count >= 8) {
    *target = decode[(*source >> 6) & 3];
    target += sourceCount;
    *target = decode[(*source >> 4) & 3];
    target += sourceCount;
    *target = decode[(*source >> 2) & 3];
    target += sourceCount;
    *target = decode[*source & 3];
    target += sourceCount;
    source ++;
    *target = decode[(*source >> 6) & 3];
    target += sourceCount;
    *target = decode[(*source >> 4) & 3];
    target += sourceCount;
    *target = decode[(*source >> 2) & 3];
    target += sourceCount;
    *target = decode[*source & 3];
    target += sourceCount;
    source ++;

    count -= 8;
  }
  switch (count) {
    case 7:
      target[6 * sourceCount] = decode[(source[1] >> 2) & 3];
    case 6:
      target[5 * sourceCount] = decode[(source[1] >> 4) & 3];
    case 5:
      target[4 * sourceCount] = decode[(source[1] >> 6) & 3];
    case 4:
      target[3 * sourceCount] = decode[source[0] & 3];
    case 3:
      target[2 * sourceCount] = decode[(source[0] >> 2) & 3];
    case 2:
      target[sourceCount] = decode[(source[0] >> 4) & 3];
    case 1:
      target[0] = decode[(source[0] >> 6) & 3];
  }
  return written;
}

/* Pack: Source - 1 bit, Target - 8 bit
 */
static uint32 pack1BitTo8Bit(PixelPacker* self,
                             uint8* source,
                             uint8* target,
                             uint32 count)
{
  uint8 *decode;
  uint32 sourceCount;
  uint32 written = count;

  VERIFY_OBJECT(self, PIXELPACKER_NAME);
  HQASSERT((self->decodeTable != NULL), "pack1BitTo8Bit - no valid decode array");

  sourceCount = self->sourceCount;

  decode = self->decodeTable;

  while (count >= 8) {
    *target = decode[(*source >> 7) & 1];
    target += sourceCount;
    *target = decode[(*source >> 6) & 1];
    target += sourceCount;
    *target = decode[(*source >> 5) & 1];
    target += sourceCount;
    *target = decode[(*source >> 4) & 1];
    target += sourceCount;
    *target = decode[(*source >> 3) & 1];
    target += sourceCount;
    *target = decode[(*source >> 2) & 1];
    target += sourceCount;
    *target = decode[(*source >> 1) & 1];
    target += sourceCount;
    *target = decode[*source & 1];
    target += sourceCount;
    source ++;

    count -= 8;
  }
  switch (count) {
    case 7:
      target[6 * sourceCount] = decode[(*source >> 1) & 1];
    case 6:
      target[5 * sourceCount] = decode[(*source >> 2) & 1];
    case 5:
      target[4 * sourceCount] = decode[(*source >> 3) & 1];
    case 4:
      target[3 * sourceCount] = decode[(*source >> 4) & 1];
    case 3:
      target[2 * sourceCount] = decode[(*source >> 5) & 1];
    case 2:
      target[sourceCount] = decode[(*source >> 6) & 1];
    case 1:
      target[0] = decode[(*source >> 7) & 1];
  }
  return written;
}

/* Pack: Source - 8 bit. Target - 1 bit
 */
static uint32 pack8BitTo1Bit(PixelPacker* self,
                             uint8* source,
                             uint8* target,
                             uint32 count)
{
  uint8 *base = target;

  UNUSED_PARAM(PixelPacker*, self);
  VERIFY_OBJECT(self, PIXELPACKER_NAME);

  while (count >= 8) {
    *target = (uint8)(source[0] & 128);
    *target |= (source[1] >> 7) << 6;
    *target |= (source[2] >> 7) << 5;
    *target |= (source[3] >> 7) << 4;
    *target |= (source[4] >> 7) << 3;
    *target |= (source[5] >> 7) << 2;
    *target |= (source[6] >> 7) << 1;
    *target |= source[7] >> 7;
    target ++;
    source += 8;

    count -= 8;
  }
  if (count) {
    *target = 0;
    switch (count) {
      case 7:
        *target |= (source[6] >> 7) << 1;
      case 6:
        *target |= (source[5] >> 7) << 2;
      case 5:
        *target |= (source[4] >> 7) << 3;
      case 4:
        *target |= (source[3] >> 7) << 4;
      case 3:
        *target |= (source[2] >> 7) << 5;
      case 2:
        *target |= (source[1] >> 7) << 6;
      case 1:
        *target |= source[0] & 128;
    }
    target ++;
  }
  return CAST_PTRDIFFT_TO_UINT32(target - base); /* Number of bytes written */
}

/* Pack: Source - 16 bit. Target - 12 bit
 */
static uint32 pack16BitTo12Bit(PixelPacker* self,
                               uint8* source8Bit,
                               uint8* target,
                               uint32 count)
{
  uint8 *base = target;
  uint16 *source = (uint16*)source8Bit;

  UNUSED_PARAM(PixelPacker*, self);
  VERIFY_OBJECT(self, PIXELPACKER_NAME);

  /* Count in this case is a number of bytes, which is twice the number of
     pixels */
  count = count >> 1;
  while (count >= 8)
  {
    target[0] = (uint8)(source[0] >> 4);
    target[1] = (uint8)((source[0] << 4) | (source[1] >> 8));
    target[2] = (uint8)source[1];
    target[3] = (uint8)(source[2] >> 4);
    target[4] = (uint8)((source[2] << 4) | (source[3] >> 8));
    target[5] = (uint8)source[3];
    target[6] = (uint8)(source[4] >> 4);
    target[7] = (uint8)((source[4] << 4) | (source[5] >> 8));
    target[8] = (uint8)source[5];
    target[9] = (uint8)(source[6] >> 4);
    target[10] = (uint8)((source[6] << 4) | (source[7] >> 8));
    target[11] = (uint8)source[7];
    target += 12;
    source += 8;
    count -= 8;
  }
  while(count >= 2) {
    target[0] = (uint8)(source[0] >> 4);
    target[1] = (uint8)((source[0] << 4) | (source[1] >> 8));
    target[2] = (uint8)source[1];
    target += 3;
    source += 2;
    count -= 2;
  }
  if (count == 1) {
    target[0] = (uint8)(source[0] >> 4);
    target[1] = (uint8)((source[0] << 4) | (source[1] >> 8));
    target += 2;
  }
  return CAST_PTRDIFFT_TO_UINT32(target - base);
}

/* Mask Pack functions: Produce "count" items of 1-bit data, incrementing the
 * read head by "step" items (either 8 or 16 bits) after each conversion.
 * Designed for type 3, interleave 1 masked images
 */

#define MASK_PACK_BODY(_maskPackType, _packer, _sourcePointer, _target, _count, _step, \
                       _bytesWritten) \
  MACRO_START \
    uint8 *base = (_target); \
    _maskPackType *source = (_maskPackType*)(_sourcePointer); \
   \
    VERIFY_OBJECT((_packer), PIXELPACKER_NAME); \
   \
    while ((_count) >= 8) { \
      *(_target) = (uint8)(((*source) > 0) << 7); \
      source += (_step); \
      *(_target) |= (uint8)(((*source) > 0) << 6); \
      source += (_step); \
      *(_target) |= (uint8)(((*source) > 0) << 5); \
      source += (_step); \
      *(_target) |= (uint8)(((*source) > 0) << 4); \
      source += (_step); \
      *(_target) |= (uint8)(((*source) > 0) << 3); \
      source += (_step); \
      *(_target) |= (uint8)(((*source) > 0) << 2); \
      source += (_step); \
      *(_target) |= (uint8)(((*source) > 0) << 1); \
      source += (_step); \
      *(_target) |= (uint8)((*source) > 0); \
      source += (_step); \
   \
      (_target) ++; \
      (_count) -= 8; \
    } \
    if ((_count)) { \
      *(_target) = 0; \
      switch ((_count)) { \
        case 7: \
          *(_target) |= (uint8)((source[(_step) * 6] > 0) << 1); \
        case 6: \
          *(_target) |= (uint8)((source[(_step) * 5] > 0) << 2); \
        case 5: \
          *(_target) |= (uint8)((source[(_step) * 4] > 0) << 3); \
        case 4: \
          *(_target) |= (uint8)((source[(_step) * 3] > 0) << 4); \
        case 3: \
          *(_target) |= (uint8)((source[(_step) * 2] > 0) << 5); \
        case 2: \
          *(_target) |= (uint8)((source[(_step) * 1] > 0) << 6); \
        case 1: \
          *(_target) |= (uint8)((source[0] > 0) << 7); \
      } \
      (_target) ++; \
    } \
    (_bytesWritten) = CAST_PTRDIFFT_TO_UINT32((_target) - (base)); /* Number of bytes written */ \
  MACRO_END

/* MaskPack: Source - 8 bit. Target - 1 bit
 */
static uint32 maskPack8Bit(PixelPacker* self,
                           uint8* sourcePointer,
                           uint8* target,
                           uint32 count,
                           uint32 step)
{
  uint32 bytesWritten;

  UNUSED_PARAM(PixelPacker*, self);

  MASK_PACK_BODY(uint8, self, sourcePointer, target, count, step, bytesWritten);
  return bytesWritten;
}

/* MaskPack: Source - 16 bit. Target - 1 bit
 */
static uint32 maskPack16Bit(PixelPacker* self,
                            uint8* sourcePointer,
                            uint8* target,
                            uint32 count,
                            uint32 step)
{
  uint32 bytesWritten;

  UNUSED_PARAM(PixelPacker*, self);

  MASK_PACK_BODY(uint16, self, sourcePointer, target, count, step, bytesWritten);
  return bytesWritten;
}

/* Log stripped */
