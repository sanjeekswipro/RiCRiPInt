/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpsdebug.h(EBDSDK_P.1) $
 * $Id: shared:xpsdebug.h,v 1.7.10.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * xps debug interface.
 */

#ifndef __XPSDEBUG_H__
#define __XPSDEBUG_H__ (1)

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
/** \brief Debug bitmask for XPS.
 *
 * This debug variable can be set from PostScript by using the \c setripvar
 * procedure in internaldict. The procedure is only available in debug RIPs.
 * \code
 *   mark {
 *     /debug_xps 5 1183615869 internaldict /setripvar get exec
 *   } stopped cleartomark
 * \endcode
 *
 * bit 0: Document commits
 * bit 1: Mark Glyph elements with their line number
 */
extern int32 debug_xps ;
#endif

#if defined(DEBUG_BUILD)

enum { /* Bitflags for debug_xps */
  DEBUG_XPS_COMMIT = 1,
  DEBUG_XPS_MARK_TEXT = 2,
  DEBUG_DISABLE_DIRECT_IMAGES = 4
} ;

#endif

/* ============================================================================
* Log stripped */
#endif
