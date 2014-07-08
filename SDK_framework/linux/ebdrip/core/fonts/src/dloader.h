/** \file
 * \ingroup dld1
 *
 * $HopeName: COREfonts!src:dloader.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for DLD1 font format. The DLD1 font format is a
 * dynamically-loaded file-based version of the Type 1 font format. DLD1 is a
 * Harlequin/Global Graphics invention. The DLD1 font file may be encrypted
 * using the HQX crypt method, for extra security. The DLD1 font format is
 * described in the document "ScriptWorks Information/Development/Core
 * RIP/DLD1 Font Format"
 */

#ifndef __DLOADER_H__
#define __DLOADER_H__

struct core_init_fns ; /* from SWcore */

/** \defgroup dld1 DLD1 Font format
    \ingroup fonts */
/** \{ */

void dld_C_globals(struct core_init_fns *fns) ;

/** Clear the DLD font cache (expect for items in use). */
void dld_cache_clear(void);


/** A hook for the VM system to remove fonts being restored away. */
void dld_restore(int32 savelevel) ;

/** \} */

#endif /* protection for multiple inclusion */

/*
Log stripped */
