/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpassthrough.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCLXL(in) pass through interface
 */

#ifndef __PCLXLPASSTHROUGH_H__
#define __PCLXLPASSTHROUGH_H__ 1

#include "fileio.h"
#include "mm.h"

#include "pcl.h"

#include "pclxltypes.h"
#include "pclxlcontext.h"

Bool pclxl_mount_pass_through_device(void) ;

void pclxl_unmount_pass_through_device(void) ;

void pclxl_free_passthrough_state_info(PCLXL_CONTEXT pclxl_context, PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info);

/**
 * \brief Initialise the PCL XL pass through device type, adding it to
 * the list of acceptable device types. This is done at RIP boot time
 * if PCL XL is enabled.
 */
Bool pclxl_pass_through_init(void) ;

/**
 * \brief Finish the PCL XL pass through device type. This is done at
 * RIP shutdown if PCL XL is enabled.
 */
void pclxl_pass_through_finish(void) ;

#endif /* __PCLXLPASSTHROUGH_H__ */

/******************************************************************************
* Log stripped */
