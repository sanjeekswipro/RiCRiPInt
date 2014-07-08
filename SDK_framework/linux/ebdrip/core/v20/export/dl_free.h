/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:dl_free.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display-list free-ing API
 */

#ifndef __DL_FREE_H__
#define __DL_FREE_H__

#include "displayt.h" /* LISTOBJECT, DL_STATE */
#include "ndisplay.h" /* NFILLOBJECT */

/* ----- Exported functions ----- */

void free_dl_object( LISTOBJECT *killMe , DL_STATE *page ) ;

void free_fill( NFILLOBJECT *nfill , DL_STATE *page) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
