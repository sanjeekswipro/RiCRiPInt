/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5ctm_private.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private type for PCL5 current transformation matrix handling.
 */
#ifndef _pcl5ctm_private_h_
#define _pcl5ctm_private_h_

#include "pcl5ctm.h"

/**
 * This structure contains matrices which define the PCL5 coordinate space. All
 * spaces are in PCL internal units.
 */
struct PCL5Ctms {
  /* Portrait, PCL internal units (7200 units per inch) coordinate system
  transform. */
  OMATRIX base;

  /* Resolution of the output device. */
  uint32 device_dpi;

  /* Coordinate system with page orientation applied. This is dependent on the
  base matrix. */
  OMATRIX orientation;

  /* Coordinate system with print direction applied. This is dependent on the
  orientation matrix. */
  OMATRIX print_direction;
};

#endif

/* Log stripped */

