/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!export:pcl5.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Entry point for all generic PCL5 functions.
 *
 * Either include individual files directly or pick all of the generic
 * PCL5 headers via this single include.
 */

#ifndef __PCL5_H__
#define __PCL5_H__ (1)

#include "mm.h"

/**
 * \defgroup pcl5 PCL5 handling.
 * \ingroup pcl
 * \{ */

/**
 * \brief Memory pool for all PCL allocations.
 *
 * The lifetime of this memory pool is per pcl5exec or pclxlexe or
 * hpgl2exec. Recursive calls to any of these will use the same memory
 * pool.
 */
extern mm_pool_t mm_pcl_pool ;

typedef uint32 pcl5_contextid_t ;

/** \} */

/* ============================================================================
* Log stripped */
#endif /* !__PCL5_H__ */
