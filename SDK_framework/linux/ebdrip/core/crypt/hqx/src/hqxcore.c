/** \file
 * \ingroup hqx
 *
 * $HopeName: COREcrypt!hqx:src:hqxcore.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions which are needed by the core in all cases.
 */

#include "core.h"

/* Convert a security number (HLS permit, dongle or LDK) which can now
   use more than 16 bits to a 16 bit value so that the hqxcrypt code
   continues to work as is (we do not want to purturb this code - if
   more bits are really needed then I suggest using OpenSSL and a new
   algorithm, currently we do not see the benefit for doing this). So
   we simply mask off the low two bytes. Security numbers (which are
   defined in a memory segment by us) are likely to be incremented by
   one (via the company product ordering process) so this approach
   should work OK. It is going to be quite some time before security
   numbers start repeating themselves unless a single OEM order
   contains many 1000's of RIP's which are all activated in
   sequence. */
int32 HqxCryptSecurityNo_from_DongleSecurityNo(int32 dongle_security_no)
{
  int32 rip_id = dongle_security_no;

  /* A prime number roughly half way between 0 and (2^16 - 1). We
     choose this when we wrap around every 65536 as we need to avoid a
     security number of zero (which is an invalid security number in
     the code [historic reasons]). If security numbers are being
     incremented by one, the previous security number is likely to
     have been 65535 and the next security number is likely to be 1,
     so pick a value half way inbetween. */
#define AVOID_ZERO_MAGIC_SERIAL_NUMBER 33289

  rip_id = rip_id & 0x0000ffff; /* Mask off high 2 bytes. */
  if ( rip_id == 0 ) {
    return AVOID_ZERO_MAGIC_SERIAL_NUMBER;
  }
  return rip_id;
}

int32 HqxCryptCustomerNo_from_DongleCustomerNo(int32 dongle_customer_no)
{
  /* Currently there is no bit manipulation on customer numbers but in
     the future we may change this. Match function names with security
     numbers to make call stack roughly the same. */
  return dongle_customer_no;
}

/* Log stripped */
