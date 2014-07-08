/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:ggxml.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
#ifndef __GGXML_H__
#define __GGXML_H__ (1)

#include "swdevice.h"
#include "xmlcbcks.h"

/**
 * @file
 * @brief GG XML interfaces
 */

extern double size[2];
extern double bleedbox[4];
extern double contentbox[4];

extern const XML_NAMESPACE_ELEMENTS gg_elements;

extern
DEVICE_FILEDESCRIPTOR gg_open(
  DEVICE_FILEDESCRIPTOR fd);

extern
int32 gg_close(
  DEVICE_FILEDESCRIPTOR fd,
  int32 abort);

extern
int32 gg_write(
  uint8*  buffer,
  int32*  len);

extern
int32 gg_read(
  uint8*  buffer,
  int32*  len);

extern
int32 gg_eof(
  DEVICE_FILEDESCRIPTOR fd);

#endif /* !__GGXML_H__ */


/* EOF ggxml.h */
