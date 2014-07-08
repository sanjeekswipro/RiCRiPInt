/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:typeops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS type operator API
 */

#ifndef __TYPEOPS_H__
#define __TYPEOPS_H__

#include "objecth.h"

/* external prototypes */
Bool reduceOaccess(int32 new_acc, Bool do_dict, register OBJECT *theo);
Bool do_cvs(OBJECT *o1, uint8 *cto, int32 lto, int32 *lfrom) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
