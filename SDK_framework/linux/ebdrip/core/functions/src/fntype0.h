/** \file
 * \ingroup samplefuncs
 *
 * $HopeName: COREfunctions!src:fntype0.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Function type 0 API
 */

#ifndef __FNTYPE0_H__
#define __FNTYPE0_H__

/** \defgroup samplefuncs Sample functions
    \ingroup funcs */
/** \{ */

Bool fntype0_initcache( FUNCTIONCACHE *fn );
Bool fntype0_unpack( FUNCTIONCACHE *fn , OBJECT *thed ,
		      OBJECT *thes , FILELIST *flptr ) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
