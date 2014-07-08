/* $HopeName: HQNchecksum!src:csumrc.c(EBDSDK_P.1) $ */
/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* Log stripped */

#include "std.h"
#include "hqcrc.h"

static uint32 crc_table[256];
static int32 madetable = 0;

static void makecrctable(void)
{
  uint32 i;

  for (i=0; i < 256; i++) {
    int32 shift = 0;
    uint32 extra = 0x8d;
    uint32 j = i;
    uint32 total = 0;

    while (j > 0) {
      if (j & 1) {
	total ^= extra << shift;
      }
      shift++;
      j >>= 1;
    };
    crc_table[i] = total;
  }
  madetable = 1;
}

uint32 HQNCALL HQCRCchecksum(uint32 crc, uint32 *data, int32 len)
{
  uint32 temp, word, mask;
  uint32 *edata;

  if(!madetable){
    makecrctable();
  }
  mask = 0xFF;
  for (edata = data + len; data < edata;) {
    word = *data++;
    temp = crc_table[crc >> 24];
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = crc_table[crc >> 24];
    word >>= 8;
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = crc_table[crc >> 24];
    word >>= 8;
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = crc_table[crc >> 24];
    word >>= 8;
    crc = (crc << 8) ^ (word & mask) ^ temp;
  }
  return(crc);
}

/* same as HQCRCchecksum but takes the bytes from the word in reverse order */
uint32 HQNCALL HQCRCchecksumreverse(uint32 crc, uint32 *data, int32 len)
{
  uint32 temp, word, dataword,  mask;
  uint32 *edata;

  if(!madetable){
    makecrctable();
  }

  mask = 0xFF;

  for (edata = data + len; data < edata;) {
    dataword = *data++;
    temp = crc_table[crc >> 24];
    word = dataword >> 24;
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = crc_table[crc >> 24];
    word = dataword >> 16;
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = crc_table[crc >> 24];
    word = dataword >> 8;
    crc = (crc << 8) ^ (word & mask) ^ temp;
    temp = crc_table[crc >> 24];
    word = dataword;
    crc = (crc << 8) ^ (word & mask) ^ temp;
  }

  return(crc);
}
/* EOF csumrc.c */
