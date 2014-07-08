/** \file
 * \ingroup core
 *
 * $HopeName: COREtables!export:ascii.h(EBDSDK_P.1) $
 * $Id: export:ascii.h,v 1.5.10.1.1.1 2013/12/19 11:25:07 anon Exp $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for ASCII control characters. Separate from swctype.h, because
 * not part of the normal <ctype.h> interface.
 */

#ifndef __ASCII_H__
#define __ASCII_H__

/* ASCII character codes */
enum {
  NUL = 0,
  SOH,
  STX,
  ETX,
  EOT,
  ENQ,
  ACK,
  BEL,
  BS,
  HT, TAB = HT,
  LF,
  VT,
  FF,
  CR,
  SO,
  SI,
  DLE,
  DC1,
  DC2,
  DC3,
  DC4,
  NAK,
  SYN,
  ETB,
  CAN,
  EM,
  SUB,
  ESC,
  FS,
  GS,
  RS,
  US,
  SPACE,
  DEL = 127
} ;

/*
Log stripped */
#endif /* protection from multiple inclusion */
