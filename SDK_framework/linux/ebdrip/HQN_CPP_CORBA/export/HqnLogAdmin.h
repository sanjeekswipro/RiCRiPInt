/* $HopeName: HQN_CPP_CORBA!export:HqnLogAdmin.h(EBDSDK_P.1) $
 * Wrapper header file for HqnLogAdmin.hh
 *
 * Copyright (C) 1999-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * There is a name clash between the module TimeBase from
 * HQNomg_idl!idl:TimeBase.idl and the typedef TimeBase from
 * MacTypes.h (a Mac OS X system header file).
 *
 * With the change to using framework headers the system header
 * is CoreServices/CoreServices.h instead of MacTypes.h.
 *
 * This header formalises and centralises a hacky workaround
 * for the nameclash.  Include this header, not HqnLogAdmin.hh.
 */

#ifndef _incl_hqnlogadmin
#define _incl_hqnlogadmin

#ifdef MACOSX

#ifdef __MACTYPES__
/* MacTypes.h has already been included.
 * We need the #include below to be the first inclusion for the
 * hacky define of TimeBase to work.
 */
#error "CoreServices/CoreServices.h must not be included before Timebase.hh.  Fix by including HqnLogAdmin.h earlier." 
#endif

#endif

#include "HqnLogAdmin.hh"

#ifdef MACOSX

/* As we don't use the MacTypes.h typedef we #define TimeBase
 * to be something else around its inclusion, so avoiding the
 * name clash.
 */
#define TimeBase HackForTimeBaseNameClash
#include <CoreServices/CoreServices.h>
#undef TimeBase

#endif

#endif // _incl_hqnlogadmin


/* Log stripped */
