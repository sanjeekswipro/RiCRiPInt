/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscsmpxformpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to create and cache colorspace objects and custom tinttransform data
 * for backend color transformations.
 */

#ifndef __GSCSMPXFORMPRIV_H__
#define __GSCSMPXFORMPRIV_H__

#include "gscsmpxform.h"
#include "gs_chaincachepriv.h"  /* GS_CHAIN_CACHE_STATE */
#include "gs_colorprivt.h"      /* GS_COLORinfoList */

typedef struct SPACECACHE SPACECACHE;

typedef struct GSC_SIMPLE_TRANSFORM GSC_SIMPLE_TRANSFORM;

Bool gsc_spacecache_init(SPACECACHE **spacecacheRef);

void gsc_spacecache_destroy(SPACECACHE **spacecacheRef,
                            GS_CHAIN_CACHE_STATE *chainCacheState,
                            GS_COLORinfoList *colorInfoList);

Bool cc_invokeSimpleTransform(GSC_SIMPLE_TRANSFORM *simpleTransform,
                              CLINK *pLink, USERVALUE *oCols);

GSC_SIMPLE_TRANSFORM *cc_csaGetSimpleTransform(OBJECT *colorSpaceObj);

DEVICESPACEID cc_spacecache_PCMofInputRS(GSC_SIMPLE_TRANSFORM *simpleTransform);

int32 cc_spacecache_id(GSC_SIMPLE_TRANSFORM *simpleTransform);


/* Log stripped */

#endif /* __GSCSMPXFORMPRIV_H__ */
