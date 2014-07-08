/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!export:progress.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Fileio routines for progress device
 */

#ifndef __PROGRESS_H__
#define __PROGRESS_H__


#include "fileioh.h"

/** \addtogroup fileio */
/** \{ */

Bool initReadFileProgress(void);
void termReadFileProgress(void);
Bool setReadFileProgress(FILELIST *flptr);
Bool closeReadFileProgress(FILELIST *flptr);
void updateReadFileProgress(void);

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
