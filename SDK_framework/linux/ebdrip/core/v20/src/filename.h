/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:filename.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API for PS filename access
 */

#ifndef __FILENAME_H__
#define __FILENAME_H__


Bool match_files_on_device( DEVICELIST *dev ,/* perform filenameforall on this device */
  uint8 *file_pattern , /* matched with this pattern */
  OBJECT *scratch  ,    /* the scratch string object */
  int32 stat ,          /* JUSTFILE or DEVICEANDFILE */
  SLIST **flist );
Bool match_on_device( DEVICELIST *dev , SLIST **flist );

#endif /* protection for multiple inclusion */


/* Log stripped */
