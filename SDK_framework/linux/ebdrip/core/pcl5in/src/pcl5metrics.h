/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5metrics.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL5 metrics structure
 */
#ifndef __PCL5_METRICS_H__
#define __PCL5_METRICS_H__

typedef struct PCL5_Metrics {
  int32 pcl_pool_max_size ;
  int32 pcl_pool_max_objects ;
  int32 pcl_pool_max_frag;
  uint32 userPatterns;
} PCL5_Metrics ;

extern PCL5_Metrics pcl5_metrics ;

/* Log stripped */
#endif /* !__PCL5_METRICS_H__ */

