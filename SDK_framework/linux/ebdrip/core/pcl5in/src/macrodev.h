/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:macrodev.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 *
 * Implemention of a PCL5 macro reading device so that one can read
 * PCL5 macro streams via a FILELIST within the RIP.
 *
 * This technique keeps the PCL5/HPGL2 interpreter code much simplier.
 */

#ifndef __MACROSDEV_H__
#define __MACROSDEV_H__

#include "pcl5context_private.h"

/**
 * \brief Initialise the PCL5 macro device type, adding it to the list
 * of acceptable device types.
 */
Bool pcl5_macrodev_init(PCL5_RIP_LifeTime_Context *pcl5_context_context) ;

/**
 * \brief Finish the PCL5 macro device type.
 */
void pcl5_macrodev_finish(PCL5_RIP_LifeTime_Context *pcl5_context_context) ;

/* Mount and unmount the macro device. This is done per PCL5 job
   because the device needs access to the PCL5 context. */
Bool pcl5_mount_macrodev(PCL5Context *pcl5_ctxt) ;

void pcl5_unmount_macrodev(PCL5Context *pcl5_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
