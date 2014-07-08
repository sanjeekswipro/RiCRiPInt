/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!export:fileparam.h(EBDSDK_P.1) $
 * $Id: export:fileparam.h,v 1.11.1.1.1.1 2013/12/19 11:24:48 anon Exp $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Configuration parameters for the COREfileio module
 */

#ifndef __FILEPARAM_H__
#define __FILEPARAM_H__

typedef struct fileioparams {
  uint8 LowMemRSDPurgeToDisk ;  /* Purge some data items to disk in low memory */
  uint8 spare[3] ;              /* Not used; padding to 4 bytes */
} FILEIOPARAMS ;

/*
Log stripped */
#endif /* Protection from multiple inclusion */
