/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:factorypatterns.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#ifndef _factorypatterns_h_
#define _factorypatterns_h_

#include "pcl5context.h"

/**
 * Create global pattern caches and populate with built-in patterns.
 */
Bool init_pattern_caches(PCL5_RIP_LifeTime_Context *pcl5_rip_context) ;

/**
 * Destroy global pattern caches.
 */
void destroy_pattern_caches(PCL5_RIP_LifeTime_Context *pcl5_rip_context) ;

#endif

/* Log stripped */

