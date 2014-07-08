/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!ripcall.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Defines calling convention macro
 */

#ifndef __RIPCALL_H__
#define __RIPCALL_H__

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Defines explicit calling convention macro for RIP-exported APIs. */
/* This file deliberately doesn't depend on any other platform or OS include
   files. */
#if defined(WIN32)
/* Windows on x86 is the only platform on which calling conventions matter. */
#  define RIPCALL     __cdecl
#  define RIPFASTCALL __fastcall
#elif defined(_MSC_VER) && defined(_M_IX86) /* MSVC x86 */
/* In case we didn't include platform.h or windows.h, MSVC on x86 is
   windows-only. */
#  define RIPCALL     __cdecl
#  define RIPFASTCALL __fastcall
#else
#  define RIPCALL
#  define RIPFASTCALL
#endif

#ifndef RIPPROTO /* function prototypes */
#  ifndef SW_NO_PROTOTYPES
#    define RIPPROTO(args)  args
#  else
#    define RIPPROTO(args)  ()
#  endif
#endif

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
