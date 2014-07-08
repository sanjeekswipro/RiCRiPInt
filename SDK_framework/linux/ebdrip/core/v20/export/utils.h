/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:utils.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS utilities
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include "coretypes.h" /* Remove once core.h is prevalent. */
#include "objecth.h"
#include "matrix.h"

int32 is_matrix( OBJECT *o , OMATRIX *matrix );
int32 is_matrix_noerror( OBJECT *o , OMATRIX *matrix );
int32 get1B( int32 *intptr );
int32 from_matrix( OBJECT *olist , OMATRIX *matrix , int32 glmode );

int32 calculateAdler32( uint8 *byteptr, int32 count ,
                        uint16 * s1 , uint16 * s2 );

#endif /* protection for multiple inclusion */

/* Log stripped */
