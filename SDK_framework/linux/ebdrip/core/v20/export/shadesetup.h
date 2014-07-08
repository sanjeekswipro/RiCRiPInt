/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:shadesetup.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list functions for smooth shading.
 */

#ifndef __SHADESETUP_H__
#define __SHADESETUP_H__

#include "displayt.h" /* GOURAUDOBJECT, HDL, DLREF, etc */

/* Vignette object diversion */
Bool setup_shfill_dl(/*@notnull@*/ /*@in@*/ DL_STATE *page,
                     /*@notnull@*/ /*@in@*/ LISTOBJECT *lobj) ;
struct SHADINGinfo;
Bool reset_shfill_dl(DL_STATE *page, struct SHADINGinfo *sinfo,
                     Bool result, uint16 minbands,
                     USERVALUE noise, uint16 noisesize,
                     /*@notnull@*/ /*@out@*/ dbbox_t *bbox) ;

Bool vertex_pool_create(int32 ncomp) ;
void vertex_pool_destroy(void) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
