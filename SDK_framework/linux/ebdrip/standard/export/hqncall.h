/** \file
 * \ingroup platform
 *
 * $HopeName: HQNc-standard!export:hqncall.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2012-2013 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Calling convention for functions provided by Global Graphics libraries and
 * DLLs.
 */
#ifndef __HQNCALL_H__
#define __HQNCALL_H__


/* This file deliberately doesn't depend on any other platform or OS include
   files. */
#if defined(WIN32)
/* Windows on x86 is the only platform on which calling conventions matter. */
#  define HQNCALL __fastcall /*__cdecl*/
#elif defined(_MSC_VER) && defined(_M_IX86) /* MSVC x86 */
/* In case we didn't include platform.h or windows.h, MSVC on x86 is
   windows-only. */
#  define HQNCALL __fastcall /*__cdecl*/
#else
#  define HQNCALL
#endif

#endif /* __HQNCALL_H__ */

