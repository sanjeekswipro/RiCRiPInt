/** \file
 * \ingroup devices
 *
 * $HopeName: COREdevices!export:devparam.h(EBDSDK_P.1) $
 * $Id: export:devparam.h,v 1.9.4.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Configuration parameters for the COREdevices module
 */

#ifndef __DEVPARAM_H__
#define __DEVPARAM_H__

#define OSD_ELEMENTS 2

/* ObeySearchDevice params control behaviour of searching for files on
   devices. */
#define AllowFilenameforall(params, dev) \
  (!(params)->ObeySearchDevice[0] || !isDeviceNoSearch(dev))
#define AllowSearch(params, dev) \
  (!(params)->ObeySearchDevice[1] || !isDeviceNoSearch(dev))

typedef struct devicesparams {
  int8 ObeySearchDevice[OSD_ELEMENTS];
                            /* obey or not the search flag for devices
                             * SearchDevices[0] - filenameforall
                             * SearchDevices[1] - all other file ops
                             */
} DEVICESPARAMS ;

#endif /* Protection from multiple inclusion */

/*
Log stripped */
