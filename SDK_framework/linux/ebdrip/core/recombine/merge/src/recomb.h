/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!merge:src:recomb.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombine API
 */

#ifndef __RECOMB_H__
#define __RECOMB_H__


#include "displayt.h"   /* LISTOBJECT */
#include "rcbcomp.h"   /* rcbv_compare_t */

/*
 * Here lies the header for the recombination part of v20
 */

#if defined( ASSERT_BUILD )
extern int32 debug_recombine ;
extern int32 debug_vignette ;
#endif

/* merging display list things */

Bool rcb_valid_common_colorants( COLORANTINDEX *cis ,
                                 int32 nci ) ;

Bool rcb_merge_knockout( DL_STATE *page,
                         LISTOBJECT *knock_lobj ,
                         LISTOBJECT *vgimg_lobj ,
                         int32 op ,
                         rcbv_compare_t *compareinfo ,
                         Bool fRemoveKnockout ,
                         LISTOBJECT **ret_lobj ) ;

Bool rcb_isKnockout( LISTOBJECT *lobj ) ;

Bool rcb_merge_spots(DL_STATE *page,
                     LISTOBJECT* lobj_dst, LISTOBJECT* lobj_src) ;

#define RCB_BBOX_TO_BANDS(_page, _y1 , _y2 , _b1 , _b2 , _routine ) MACRO_START \
  int32 _b1_ = (_y1) ; \
  int32 _b2_ = (_y2) ; \
  int32 _last_band_ = (_page)->sizefactdisplaylist ; \
  int32 _fact_band_ = (_page)->sizefactdisplayband ; \
  HQASSERT( _b1_ <= _b2_ , "bbox out of order in"_routine ) ; \
  _b2_ += guc_getMaxOffsetIntoBand( gsc_getRS(gstateptr->colorInfo) ) ; \
  _b1_ /= _fact_band_ ; \
  _b2_ /= _fact_band_ ; \
  if ( _b1_ < 0 ) \
    _b1_ = 0 ; \
  if ( _b2_ >= _last_band_ ) \
    _b2_ = _last_band_ - 1 ; \
  HQASSERT( _b1_ >= 0 && _b1_ < _last_band_ , \
    _routine": b1 off of dl" ) ; \
  HQASSERT( _b2_ >= 0 && _b2_ < _last_band_ , \
    _routine": b2 off of dl" ) ; \
  (_b1) = _b1_ ; \
  (_b2) = _b2_ ; \
MACRO_END

#endif /* !__RECOMB_H__ */


/* Log stripped */
