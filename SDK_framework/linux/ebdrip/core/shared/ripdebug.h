/** \file
 * \ingroup debug
 *
 * $HopeName: SWcore!shared:ripdebug.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A contract for registering debugging variables.
 */

#ifndef __RIPDEBUG_H__
#define __RIPDEBUG_H__


#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )

/** \addtogroup debug */
/** \{ */

/** \brief Register a debugging variable.

    \param[in] nameid The name-number field of a pre-defined \c NAMECACHE
    entry.
    \param[in] type One of the \c OBJECT types.
    \param[in] value A pointer to the debugging variable.

    This function must be provided by one of the compounds linked into the
    core. It associates a debugging variable with a type and a name, to allow
    the debugging variable to be set programmatically. In the Harlequin RIP
    SWcore, the name and type are used by the operator \c setripvar to allow
    PostScript configuration of the debug variable values.
*/
void register_ripvar(int32 nameid, int32 type, void *value) ;

/** \} */

#endif /* DEBUG_BUILD || ASSERT_BUILD */

#endif /* protection for multiple inclusion */


/* Log stripped */
