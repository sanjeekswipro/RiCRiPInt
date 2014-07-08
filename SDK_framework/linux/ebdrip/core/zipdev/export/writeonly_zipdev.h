/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!export:writeonly_zipdev.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exported interface to the PostScript Write-only ZIP device.
 */

#ifndef __WRITEONLY_ZIPDEV_H__
#define __WRITEONLY_ZIPDEV_H__

struct core_init_fns ; /* from SWcore */

/** Write-only Zip device ID; this is unique - there is a master list in the
Scriptworks Information database in Lotus Notes. */
#define WRITEONLY_ZIP_DEVICE_TYPE 32

void writeonly_zipdev_C_globals(struct core_init_fns *fns) ;

#endif

/* Log stripped */

