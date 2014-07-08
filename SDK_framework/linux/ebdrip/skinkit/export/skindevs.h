/** \file
 * \ingroup devices
 *
 * $HopeName: SWskinkit!export:skindevs.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to the skin "device" subsystem
 */

#ifndef __SKINDEVS_H__
#define __SKINDEVS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "swdevice.h" /* Needs DEVICELIST */

int32 RIPCALL skindevices_last_error(DEVICELIST *dev);

void skindevices_set_last_error(int32 error);

#ifdef __cplusplus
}
#endif


#endif /* Protection from multiple inclusion */
