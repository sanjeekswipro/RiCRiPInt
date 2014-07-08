/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!export:rcbadjst.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombined objects require post-interpretation processing before they can be
 * rendered to the backdrop or straight out on to the page.  This includes
 * shfill decomposition to Gouraud objects; fixing up recombine-intercepted
 * objects in composite jobs. Notionally, this is the "Recombining separations"
 * stage.
 */

#ifndef __RCBADJST_H__
#define __RCBADJST_H__

struct Group;

/**
 * Preprocess the DL before rendering or compositing.  Do shfill decomposition
 * to Gouraud objects; fix up recombine-intercepted objects in composite jobs.
 */
Bool rcba_prepare_dl(struct Group *pageGroup, Bool savedOverprintBlack);

#endif /* protection for multiple inclusion */


/* Log stripped */
