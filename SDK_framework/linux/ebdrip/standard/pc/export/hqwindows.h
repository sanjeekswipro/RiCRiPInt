/** \file
 * \ingroup platform
 *
 * $HopeName: HQNc-standard!pc:export:hqwindows.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Wrapper for windows.h.
 *
 * Its use is to resolve clashes between \#defines common to the WDK
 * compiler's headers and our headers, and to turn off warnings which occur
 * in MS Windows include files (winnt.h and winbase.h, for example).
 *
 * The warnings are:
 *
 * warning C4201: nonstandard extension used : nameless struct/union
 * warning C4214: nonstandard extension used : bit field types other than int
 * warning C4115: named type definition in parentheses
 * warning C4514: unreferenced inline function has been removed
 */

#ifndef __HQWINDOWS_H__
#define __HQWINDOWS_H__

#if !defined(_MSC_VER) && !defined(__GNUC__)
#error "Including windows.h when not using a Windows compiler."
#endif

#ifdef BUILDING_WITH_WDK

/* Undefine the MAX/MIN values which the WDK compiler defines in basetsd.h */

#ifdef MAXUINT8
/* Defined in hqtypes.h */
#undef MAXUINT8
#undef MAXINT8
#undef MININT8
#undef MAXUINT16
#undef MAXINT16
#undef MININT16
#undef MAXUINT32
#undef MAXINT32
#undef MININT32
#endif
#ifdef MAXUINT64
/* Defined in hqtypes.h or platform.h */
#undef MAXUINT64
#undef MAXINT64
#undef MININT64
#endif
#ifdef MAXUINT
/* Defined in hqtypes.h */
#undef MAXUINT
#endif
#ifdef MAXINT
/* Defined in hqtypes.h */
#undef MAXINT
#undef MININT
#endif

#endif  /* BUILDING_WITH_WDK */

#ifndef _WIN32_WINNT
/* Target Windows XP and later, to get SwitchToThread():

   http://msdn.microsoft.com/en-us/library/6sehtctf.aspx
*/
#define _WIN32_WINNT 0x0501
#endif

#pragma warning(push)
#pragma warning(disable : 4201 4214 4115 4514)

#include <windows.h>

#pragma warning(pop)

#endif  /* __HQWINDOWS_H__ */

