/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:implicitgroup.h(EBDSDK_P.1) $
 * $Id: export:implicitgroup.h,v 1.6.2.1.1.1 2013/12/19 11:25:24 anon Exp $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display-list implict group API
 */

#ifndef __IMPLICITGROUP_H__
#define __IMPLICITGROUP_H__

#include "displayt.h"

/* Determine what kind of implicit group is created for dostrokefill or
   dostroke. */
typedef enum {
  IMPLICIT_GROUP_STROKE  = 1,
  IMPLICIT_GROUP_PDF     = 2,
  IMPLICIT_GROUP_XPS     = 3,
  IMPLICIT_GROUP_TEXT_KO = 4
} IMPLICIT_GROUP_USAGE ;

Bool openImplicitGroup(DL_STATE *page, Group** group, int32 *gid,
                       IMPLICIT_GROUP_USAGE usage,
                       Bool transparent_pattern) ;

Bool closeImplicitGroup(Group **group, int32 gid, Bool result) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
