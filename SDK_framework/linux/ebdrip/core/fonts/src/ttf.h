/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:ttf.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief An interface between the TrueType cache and the font cache system.
 */

#ifndef __TTF_H__
#define __TTF_H__

struct core_init_fns ; /* from SWcore */

void ttfont_C_globals(struct core_init_fns *fns) ;


/** Clear the TrueType cache (expect for items in use). */
void tt_cache_clear(void);


/** A hook for the VM system to remove fonts being restored away. */
void tt_restore(int32 savelevel) ;

/*
* Log stripped */
#endif
