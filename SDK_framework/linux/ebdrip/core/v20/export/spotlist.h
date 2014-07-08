/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:spotlist.h(EBDSDK_P.1) $
 * $Id: export:spotlist.h,v 1.5.1.1.1.1 2013/12/19 11:25:25 anon Exp $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Maintain a list of spot numbers and object types on the DL to help the final
 * backdrop render handle screen switching efficiently.
 */

#ifndef __SPOTLIST_H__
#define __SPOTLIST_H__

#include "displayt.h"

typedef struct SPOTNO_LINK SPOTNO_LINK;

void spotlist_init(DL_STATE *page, Bool partial);
Bool spotlist_add(DL_STATE *page, SPOTNO spotno, HTTYPE objtype);
Bool spotlist_add_safe(DL_STATE *page, SPOTNO spotno, HTTYPE objtype);
Bool spotlist_multi_spots(const DL_STATE *page);
Bool spotlist_out16(const DL_STATE *page, uint32 nComps,
                    COLORANTINDEX *colorants);

/* Made public to avoid allocation.  Contents must be accessed through the
   iterator functions only. */
typedef struct SPOTNO_ITER {
  SPOTNO_LINK *current;
  HTTYPE       objtype;
} SPOTNO_ITER;

void spotlist_iterate_init(SPOTNO_ITER *iter, SPOTNO_LINK *spotlist);
Bool spotlist_iterate(SPOTNO_ITER *iter, SPOTNO *spotno, HTTYPE *objtype);

#if defined( DEBUG_BUILD )
void spotlist_trace(const DL_STATE *page);
#else
#define spotlist_trace(_page)
#endif

#endif /* __SPOTLIST_H__ */

/* Log stripped */
