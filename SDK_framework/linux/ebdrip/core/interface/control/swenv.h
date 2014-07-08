/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swenv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Entry points for Harlequin RIP environment queries
 */

#ifndef SWENV_H_INCLUDED
#define SWENV_H_INCLUDED 1

#include "ripcall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* These functions defined outside the core RIP */

extern uint8  *get_date RIPPROTO((int32 fLocal));

extern int32  get_rtime RIPPROTO((void));

extern int32  get_utime RIPPROTO((void));

extern uint8 *cvt_real RIPPROTO((double value, char type, int precision));

extern uint8 *get_guilocale RIPPROTO((void));

extern uint8 *get_oslocale RIPPROTO((void));

extern uint8 *get_operatingsystem RIPPROTO((void));

#ifdef __cplusplus
}
#endif


#endif /* SWENV_H_INCLUDED */
