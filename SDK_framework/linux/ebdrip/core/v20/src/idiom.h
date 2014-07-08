/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:idiom.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Idiom recognition API
 */

#ifndef __IDIOM_H__
#define __IDIOM_H__

#include "objecth.h"

/* EXTERNAL DEFINITIONS */

void  idiom_purge( int32 slevel ) ;
Bool  idiom_replace( OBJECT *theo ) ;

void  idiom_resethash( void ) ;
void  idiom_inchashdepth( void ) ;
void  idiom_dechashdepth(Bool fConstantArray) ;
void  idiom_calchash( OBJECT *proc ) ;
void  idiom_updatehash( OBJECT *theo ) ;

/* MACRO DEFINITIONS */

#endif /* IDIOM_H multiple inclusion protection */


/* Log stripped */
