/** \file
 * \ingroup cid
 *
 * $HopeName: COREfonts!src:cidmap.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for CID map lookups
 */

#ifndef __CIDMAP_H__
#define __CIDMAP_H__


#include "graphics.h" /* FONTinfo */
#include "fontt.h"    /* charcontext_t */

struct core_init_fns ;

void cidmap_C_globals(struct core_init_fns *fns) ;


/** Clear the CID map cache. */
void cidmap_cache_clear(void);


/** A hook for the VM system to remove fonts being restored away. */
void cidmap_restore(int32 savelevel) ;

/* CID map lookup functions. */
int32 cid0_lookup_char(FONTinfo *fontInfo, charcontext_t *context) ;
int32 cid2_lookup_char(FONTinfo *fontInfo, charcontext_t *context) ;

/* Extract a high-byte first integer from an offset, updating the offset
   pointer. */
uint32 cidmap_offset(uint8 **cidmap, uint32 nbytes) ;

/*
Log stripped */
#endif /* protection for multiple inclusion */
