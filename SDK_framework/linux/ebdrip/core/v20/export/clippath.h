/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!export:clippath.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External things defined in clippath.c
 */

#ifndef __CLIPPATH_H__
#define __CLIPPATH_H__

/** Perform clippath operation using current settings, replacing path in
    outpath with the clippath result, and returning the rule and flags
    appropriate for it in cliptype. */
Bool clippath_internal(corecontext_t *context, PATHINFO *outpath, int32 *cliptype) ;

Bool cliptoall(PATHINFO *path1, PATHINFO *path2,
               PATHINFO *topath, Bool no_degenerates) ;
Bool normalise_path(PATHINFO *fpath, int32 therule,
                    Bool userectclippath, Bool ignore_degenerates) ;
Bool make_devicebounds_path(PATHINFO *outpath) ;

#if defined( DEBUG_BUILD )
void init_clippath_debug(void);
#endif

#endif /* protection for multiple inclusion */

/* Log stripped */
