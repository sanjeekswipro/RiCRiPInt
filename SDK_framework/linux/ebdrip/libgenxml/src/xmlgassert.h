#ifndef __XMLGASSERT_H__
#define __XMLGASSERT_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgassert.h(EBDSDK_P.1) $
 * $Id: src:xmlgassert.h,v 1.5.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */

/* ============================================================================
 * Map XMLGASSERT to HQASSERT if XMLG_SW_COMPAT has been defined. If not,
 * XMLGASSERT becomes a no-op.
 */

#if defined(XMLG_SW_COMPAT)
#include "std.h"
#define XMLGASSERT(x,y) HQASSERT(x,y)
#else

#ifndef MACRO_START
#define MACRO_START do {
#define MACRO_END } while(0)
#endif

#ifndef EMPTY_STATEMENT
#define EMPTY_STATEMENT() MACRO_START MACRO_END
#endif

#define XMLGASSERT(x,y) EMPTY_STATEMENT()

#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLGASSERT_H__*/
