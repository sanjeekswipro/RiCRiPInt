/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscsmplkpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color conversion private data
 */

#ifndef __GSCSMPLKPRIV_H__
#define __GSCSMPLKPRIV_H__


#include "gscsmplk.h"
#include "mps.h"

/* cmykto... */

CLINK *cc_cmyktogray_create( Bool fCompositing ) ;

CLINK *cc_cmykton_create( OBJECT customProcedure,
                          int32 n_oColorants ) ;

CLINK *cc_cmyktorgb_create( void ) ;
CLINK *cc_cmyktorgbk_create( void ) ;
CLINK *cc_cmyktocmy_create( void ) ;
CLINK *cc_cmyktolab_create( void ) ;

/* rgbto... */

CLINK *cc_rgbtocmyk_create( GS_RGBtoCMYKinfo   *rgbtocmykInfo,
                            Bool               preserveBlack,
                            COLOR_PAGE_PARAMS  *colorPageParams ) ;
Bool cc_rgbtocmykiscomplex( CLINK *pLink ) ;

CLINK *cc_rgbtogray_create(Bool fCompositing) ;

CLINK *cc_rgbton_create( OBJECT customProcedure,
                         int32 n_oColorants ) ;

CLINK *cc_rgbtorgbk_create( void ) ;
CLINK *cc_rgbtocmy_create( void ) ;
CLINK *cc_rgbtolab_create( void ) ;

/* cmyto... */
CLINK *cc_cmytorgb_create( void ) ;

/* grayto... */

CLINK *cc_graytocmyk_create( void ) ;
CLINK *cc_graytorgb_create( void ) ;

CLINK *cc_grayton_create( OBJECT customProcedure,
                          int32 n_oColorants ) ;

CLINK *cc_graytok_create( void ) ;
CLINK *cc_graytocmy_create( void ) ;
CLINK *cc_graytolab_create( void ) ;

void  cc_destroyrgbtocmykinfo( GS_RGBtoCMYKinfo **rgbtocmykInfo ) ;

void  cc_reservergbtocmykinfo( GS_RGBtoCMYKinfo *rgbtocmykInfo ) ;

Bool cc_arergbtocmykobjectslocal(corecontext_t *corecontext,
                                 GS_RGBtoCMYKinfo *rgbtocmykInfo ) ;

mps_res_t cc_scan_rgbtocmyk( mps_ss_t ss, GS_RGBtoCMYKinfo *rgbtocmykInfo ) ;

/* Log stripped */

#endif /* __GSCSMPLKPRIV_H__ */
