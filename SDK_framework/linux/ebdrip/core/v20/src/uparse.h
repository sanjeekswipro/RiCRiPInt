/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:uparse.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External declarations for uparse.c
 * Filename retained for hysterical raisins.
 */

#ifndef __UPARSE_H__
#define __UPARSE_H__


#include "objecth.h"
#include "paths.h"

enum upath_to_path_flags { UPARSE_NONE = 0x00,
			   UPARSE_CLOSE	= 0x01,
			   UPARSE_APPEND = 0x02 } ;

Bool upath_to_path(OBJECT *theo, int32 flags, PATHINFO *ppath) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
