/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:pager.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS pager
 */

#include "core.h"
#include "swerrors.h"

#include "objects.h"
#include "control.h"
#include "pager.h"

#ifndef NULL
#define NULL 0
#endif


#if 0
static int32    diskpagesused = 0 ;
static DISKPAGE diskpages[ MAXIMUM_SEGMENT ] ;
#endif

/* ARGSUSED */
int8 *alloc_p_using_disk( int32 allocsize , int32 alloctype , int32 bankno )
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused( allocsize, alloctype, bankno )
#else
  UNUSED_PARAM(int32, allocsize);
  UNUSED_PARAM(int32, alloctype);
  UNUSED_PARAM(int32, bankno);
#endif /* HAS_PRAGMA_UNUSED */

  return (( int8 * )NULL ) ;
}






/* Log stripped */
