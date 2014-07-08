/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:nsidetst.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External things defined in nsidetst.c
 */

#ifndef __NSIDETST_H__
#define __NSIDETST_H__

Bool path_in_path(PATHLIST *appath, PATHLIST *path, int32 filltype,
                  Bool *inside);
Bool point_in_path(SYSTEMVALUE px, SYSTEMVALUE py, PATHLIST *thepath,
                   int32 filltype, Bool *inside);

#endif /* protection for multiple inclusion */

/* Log stripped */
