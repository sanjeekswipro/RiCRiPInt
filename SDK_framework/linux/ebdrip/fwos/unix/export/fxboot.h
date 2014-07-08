/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FXBOOT_H__
#define __FXBOOT_H__

/*
 * $HopeName: HQNframework_os!unix:export:fxboot.h(EBDSDK_P.1) $
 * FrameWork External, Motif specific, Boot definitions
 */

/* 
* Log stripped */


/* ----------------------- Includes ---------------------------------------- */

/* See fw.h */
#include "fxcommon.h"   /* Common */
                        /* Is External */
                        /* Is Platform Dependent */

#define FW_PLATFORM_CONTROL_CONTEXT

typedef struct FwPlatformControlContext
{
  /* The application must pass in the argc and argv
   * that was provided to it.
   */
  int32   argc;
  char ** argv;
} FwPlatformControlContext;

/* ----------------------- Types ------------------------------------------- */

#endif /* !__FXBOOT_H__ */

/* eof fxboot.h */
