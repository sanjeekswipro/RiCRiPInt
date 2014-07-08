/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gs_callps.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API for caching color calls to PS interpreter
 */

#ifndef __GSCALLPS_H__
#define __GSCALLPS_H__

typedef struct CALLPSCACHE CALLPSCACHE;

Bool call_psproc(OBJECT *psfunc, SYSTEMVALUE in, SYSTEMVALUE *out, int32 nOut);
CALLPSCACHE *create_callpscache(int32 fnType, int32 nOut, int32 id,
                                SYSTEMVALUE *range, OBJECT *psfunc);
void reserve_callpscache(CALLPSCACHE *cpsc);
void destroy_callpscache(CALLPSCACHE **cpsc);
int32 id_callpscache(CALLPSCACHE *cpsc);
void lookup_callpscache(CALLPSCACHE *cpsc, USERVALUE in, USERVALUE *out);

/* Log stripped */

#endif /* __GSCALLPS_H__ */
