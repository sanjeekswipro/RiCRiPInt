/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __TIMEBOMB_H__
#define __TIMEBOMB_H__

/*
 * $HopeName: SWsecurity!export:timebomb.h(EBDSDK_P.1) $
 *
* Log stripped */

/* ----------------------- Macros ------------------------------------------- */

/* let's be paranoid about this one too */
#define GetDaysRemaining  getFormatType
#define IsWaterMarkRIP    getDirectoryType

/* ----------------------- External Functions ------------------------------- */

extern int32 GetDaysRemaining();
extern int32 IsWaterMarkRIP();

#endif /* __TIMEBOMB_H__ */

/* eof timebomb.h */
