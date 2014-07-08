/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:objimpl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Declarations local to the implementation of the COREobjects compound.
 */

#ifndef __OBJIMPL_H__
#define __OBJIMPL_H__

/* Any invalid pointer will do here. */
#define NC_DICTCACHE_RESET (&onothing) /* ONOTHING static object */

extern NAMECACHE *namepurges ;

#if defined( ASSERT_BUILD )
/* Assert functions for interfaces */
Bool object_asserts(void) ;
#endif

/** Allocate the name cache, and store pre-defined entries into it. */
Bool ncache_init(void) ;

/** Destroy the name cache. */
void ncache_finish(void);

/*
Log stripped */
#endif /* Protection from multiple inclusion */
