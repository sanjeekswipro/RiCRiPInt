/** \file
 * \ingroup halftone
 *
 * $HopeName: SWv20!export:gs_spotfn.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone spot-function API
 */

#ifndef __GS_SPOTFN_H__
#define __GS_SPOTFN_H__

#include "objectt.h"            /* OBJECT */

/** Return an index for a C version of a named override spot function.

    \param spotfn An object containing the name of the spot function.
    \return -1 if no C function exists; a non-negative index otherwise.
*/
int32 findCSpotFunction(OBJECT *spotfn);

/** Return a name cache entry for a named override spot function.

    \param spotfn An object containing the name of the spot function.
    \return NULL if the object is not a spot function name, a NAMECACHE pointer
      for the name otherwise.
*/
NAMECACHE *findSpotFunctionName(OBJECT *spotfn);

/** Return a spot function procedure given the name cache entry of a named
    override spot function.

    \param name A name of an override spot function.
    \return NULL if the spot function is not known, an object reference
      to the spot function procedure or dictionary otherwise.
*/
OBJECT *findSpotFunctionObject(NAMECACHE *name);

#endif /* protection for multiple inclusion */

/* Log stripped */
