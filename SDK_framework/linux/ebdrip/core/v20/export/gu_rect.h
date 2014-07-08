/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:gu_rect.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rectangular path operators
 */

#ifndef __GU_RECT_H__
#define __GU_RECT_H__

/* FILE : gu_rect.h
   PURPOSE : header file for gu_rect.h declaring exported functions and
             variables
*/

#include "coretypes.h" /* Until everyone includes core.h */
#include "graphict.h"  /* RECTANGLE */
#include "matrix.h"    /* OMATRIX */


/* ----- Exported functions ----- */
extern int32 cliprectangles( RECTANGLE *prects , int32 nrects ) ;
extern int32 fillrectdisplay( DL_STATE *page, dbbox_t *rectptr ) ;
extern int32 strokerectangles( int32 colorType ,
                               RECTANGLE *prects ,
                               int32 nrects ,
                               OMATRIX *adjustment_matrix ) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
