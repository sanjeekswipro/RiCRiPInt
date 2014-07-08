/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:filters.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS filters API
 */

#ifndef __FILTERS_H__
#define __FILTERS_H__

struct FILELIST ; /* from COREfileio */
struct OBJECT ; /* from COREobjects */
struct STACK ; /* from COREobjects */

/* ----- Exported functions ------ */
Bool ps_filter_preflight(struct FILELIST *filter) ;
Bool ps_filter_install(struct FILELIST *filter, struct OBJECT *args, struct STACK *stack) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
