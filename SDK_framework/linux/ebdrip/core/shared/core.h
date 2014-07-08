/** \file
 * \ingroup ps
 *
 * $HopeName: SWcore!shared:core.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This header file includes files with the types and declarations that all
 * core modules should have access to. Note:
 *
 * 1) It should be the first include file in any Core source file, for
 *    compounds which cannot be built independently of the rest of the Core
 *    RIP.
 * 2) It should NOT be included from any header file.
 * 3) It should ONLY contain hash-includes; NO declarations or definitions are
 *    allowed.
 *
 * It is part of the Core configuration shared include directory so that
 * different Core configurations can alter the standard set of includes.
 */

#ifndef __CORE_H__
#define __CORE_H__

#include "std.h"        /* Standard typedefs, assert mechanism, etc. */
#include "swvalues.h"   /* SYSTEMVALUE and USERVALUE */
#include "coretypes.h"  /* Simple types from COREtypes */
#include "context.h"    /* Make core context available to everyone */
#include "exitcodes.h"  /* Fatal RIP exit codes SwExit() dispatch */
#include "cglobals.h"   /* Ability for modules to call init_C_globals functions. */

/* We might want to consider including swerrors.h for access to the standard
   error handler in future. */

#endif /* Protection from multiple inclusion */

/* Log stripped */
