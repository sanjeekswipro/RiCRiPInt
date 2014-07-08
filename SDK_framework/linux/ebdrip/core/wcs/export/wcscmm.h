/** \file
 * \ingroup wcs
 *
 * $HopeName: COREwcs!export:wcscmm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Windows Color System Color Management Module
 */

#ifndef __WCSCMM_H__
#define __WCSCMM_H__

#include "swcmm.h"

struct core_init_fns ; /* from SWcore */

void wcscmm_C_globals(struct core_init_fns *fns) ;

sw_cmm_api* wcscmm_instance() ;

#endif

/*
* Log stripped */
