/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:ciepsfns.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1995-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header file for ciepsfns.c, which defines CIE procedure callback table.
 */

#ifndef __CIEPSFNS_H__
#define __CIEPSFNS_H__

#define CIE_PROCTABLE_SIZE 19

typedef SYSTEMVALUE (*CIECallBack)(SYSTEMVALUE arg, void *extra) ;

extern CIECallBack cieproctable[CIE_PROCTABLE_SIZE] ;

#endif /* protection for multiple inclusion */

/* Log stripped */
