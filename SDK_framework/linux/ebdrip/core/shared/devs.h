/** \file
 * \ingroup devices
 *
 * $HopeName: SWcore!shared:devs.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to the core "device" subsystem
 */

#ifndef __DEVS_H__
#define __DEVS_H__

#include "swdevice.h" /* Needs DEVICELIST */

int32 RIPCALL devices_last_error(DEVICELIST *dev);

void devices_set_last_error(int32 error);

/*
Log stripped */
#endif /* Protection from multiple inclusion */
