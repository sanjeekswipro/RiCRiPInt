/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:memutil.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1995-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains general memory set functions, and other
 * generally useful memory functions.
 */

#include "std.h"
#include "caching.h"

/*
 * ************************ NOTE *****************************************
 *
 * memsetl(), memsetw() , memsetb()
 *
 * These three functions have been obsoleted by the introduction
 * of platform and O/S specific implementations HqMemSet8/16/32().
 * See hqmemset.c[h] for more detailed information.
 *
 * This source file will soon disappear
 *
 * ************************ NOTE ******************************************
 */

/* Log stripped */
