/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:diff.h(EBDSDK_P.1) $
 * $Id: src:diff.h,v 1.12.10.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2000-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Differencing predictors for PNG, Flate, etc.
 */

/* Differencing hooks */
typedef void ( * DIFF_DECODE_PREDICTOR_FN )( uint8 *current , uint8 *left ,
                                             uint8 *top , uint8 *topleft ) ;

typedef void ( * DIFF_ENCODE_PREDICTOR_FN )( uint8 *dest,
                                             uint8 *current , uint8 *left ,
                                             uint8 *top , uint8 *topleft ) ;

typedef struct {
  /* Parameters from the host filter */
  
  int32 predictor ;
  int32 columns ;
  int32 colors ;
  int32 bpc ;
  
  /* Slots used in the differencing predictor functions */

  DIFF_DECODE_PREDICTOR_FN dfn ;
  DIFF_ENCODE_PREDICTOR_FN efn ;
  int32 bpp ;
  int32 count ;
  uint8 *base ;
  uint8 *current ;
  uint8 *previous ;
  uint8 *limit ;
  uint32 size ;
}
DIFF_STATE ;

int32 diffInit( DIFF_STATE *state , OBJECT *theo ) ;

void diffClose( DIFF_STATE *state ) ;

int32 diffDecode( DIFF_STATE *state , uint8 *src , int32 src_len ,
                  uint8 *dest , int32 *dest_len ) ;

int32 diffEncode( DIFF_STATE *state , uint8 *src , int32 *src_len ,
                  uint8 *dest , int32 *dest_len ) ;

/* ----------------------------------------------------------------------------
* Log stripped */
