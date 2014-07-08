/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:spdetect.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1995-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color detection code.
 */

#ifndef __SPDETECT_H__
#define __SPDETECT_H__

#include "gu_chan.h"            /* DEVICESPACEID */

/* ----- External global variables ----- */

/* These globals are purely an optimisation to reduce call overhead to their
 * corresponding routine. new_color_detected for detect_setcolor_separation
 * and new_screen_detected for setscreen_separation.
 */
extern int32 new_color_detected ;
extern int32 new_screen_detected ;

extern uint8* primary_colors[];

/* ----- Exported functions ----- */

extern void  enable_separation_detection( void ) ;
extern void  disable_separation_detection( void ) ;

extern int32 sub_page_is_composite( void ) ;
extern int32 sub_page_is_separations( void ) ;

extern int32 page_is_composite( void ) ;
extern int32 page_is_separations( void ) ;

extern NAMECACHE *get_separation_name(int32 f_ignore_disable) ;
extern void  get_separation_method(int32 f_ignore_disable, uint8** sepmethodname, int32* sepmethodlength);

extern int32 reset_separation_detection_on_new_page( GS_COLORinfo *colorInfo ) ;
extern int32 reset_separation_detection_on_sub_page( GS_COLORinfo *colorInfo ) ;
extern void  reset_separation_detection_on_restore( void ) ;
extern void  reset_setsystemparam_separation( void ) ;

extern int32 detected_multiple_seps( void ) ;

extern int32 detect_setsystemparam_separation( NAMECACHE *sepname,
                                               GS_COLORinfo *colorInfo ) ;
extern int32 detect_setcmykcolor_separation( GS_COLORinfo *colorInfo ) ;
extern Bool  detect_setcolor_separation( void ) ;
extern int32 detect_setscreen_separation( SYSTEMVALUE frequency_detected ,
                                          SYSTEMVALUE angle_detected ,
                                          GS_COLORinfo *colorInfo ) ;

extern int32 setscreen_separation( int32 screened ) ;

extern void  screen_normalize_angle(SYSTEMVALUE *angle);

extern int32 finalise_sep_detection(GS_COLORinfo *colorInfo);

extern inline Bool interceptSeparation(NAMECACHE *colname,
                                       NAMECACHE *sepname,
                                       DEVICESPACEID devicespace);

#endif /* protection for multiple inclusion */


/* Log stripped */
