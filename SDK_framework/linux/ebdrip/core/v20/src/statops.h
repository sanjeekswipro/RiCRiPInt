/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:statops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS status operators API
 */

#ifndef __STATOPS_H__
#define __STATOPS_H__


extern int32 hwareiomode;
extern int32 swareiomode;

extern int32 jobtimeout;

int32 curr_jobtimeout( void );
void setjobtimeout(corecontext_t *context, int32 new_timeout);

#endif /* protection for multiple inclusion */

/* Log stripped */
