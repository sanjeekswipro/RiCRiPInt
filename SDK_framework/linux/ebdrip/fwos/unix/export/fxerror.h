/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FXERROR_H__
#define __FXERROR_H__

/*
 * $HopeName: HQNframework_os!unix:export:fxerror.h(EBDSDK_P.1) $
 * FrameWork External, Unix specific, Error definitions.
 */

/*
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

/* see fwcommon.h */
#include "fxcommon.h"   /* Common */
                        /* Is External */
                        /* Is Platform Dependent */

/* ----------------------- Macros ------------------------------------------ */

#define FW_PLATFORM_SUCCESS     0

/* ----------------------- Types ------------------------------------------- */

typedef int32   FwPlatformError;        /* As errno */

#endif /* ! __FXERROR_H__ */

/* eof fxerror.h */
