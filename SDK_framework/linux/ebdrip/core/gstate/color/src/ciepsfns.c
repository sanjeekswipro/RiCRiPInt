/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:ciepsfns.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1995-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * CIE procedure callback table; this table contains C versions of
 * commonly-used Encode, Decode, Transform and RenderTable procedures used in
 * device independent colour rendering.
 */

#include "core.h"

#include "objects.h"
#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"

#include "ciepsfns.h"

static SYSTEMVALUE RGB_Decode_LMN(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Lab_D65_Decode_L(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Lab_Decode_M(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Lab_D65_Decode_N(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Lab_Decode_A(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Lab_Decode_B(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Lab_Decode_C(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE L_star_Decode_LMN(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Default_Encode_LMN(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Default_Transform_P(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Default_Transform_Q(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Default_Transform_R(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE ICC_Transform_P(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE ICC_Transform_Q(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE ICC_Transform_R(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Default_RenderTable(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Photoshop_RGB_Decode_LMN(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Lab_D50_Decode_L(SYSTEMVALUE arg, void *extra) ;
static SYSTEMVALUE Lab_D50_Decode_N(SYSTEMVALUE arg, void *extra) ;

CIECallBack cieproctable[CIE_PROCTABLE_SIZE] = {
  /* 0 */  RGB_Decode_LMN,
  /* 1 */  Lab_D65_Decode_L,
  /* 2 */  Lab_Decode_M,
  /* 3 */  Lab_D65_Decode_N,
  /* 4 */  Lab_Decode_A,
  /* 5 */  Lab_Decode_B,
  /* 6 */  Lab_Decode_C,
  /* 7 */  L_star_Decode_LMN,
  /* 8 */  Default_Encode_LMN,
  /* 9 */  Default_Transform_P,
  /* 10 */ Default_Transform_Q,
  /* 11 */ Default_Transform_R,
  /* 12 */ ICC_Transform_P,
  /* 13 */ ICC_Transform_Q,
  /* 14 */ ICC_Transform_R,
  /* 15 */ Default_RenderTable,
  /* 16 */ Photoshop_RGB_Decode_LMN,
  /* 17 */ Lab_D50_Decode_L,
  /* 18 */ Lab_D50_Decode_N
} ;


#define LAB_STAR_DECODE(arg) MACRO_START \
  register SYSTEMVALUE val = (arg) ; \
  \
  if ( val >= 6.0 / 29.0 ) \
    val = val * val * val ; \
  else { \
    val -= 4.0 / 29.0 ; \
    val *= 108.0 / 841.0 ; \
  } \
  arg = val ; \
MACRO_END


/* Implements Decode for RGB space with CCIR XA/11 recommended phosphor and
   white point (RB2, p.189):
     { 1 0.45 div exp }
*/
static SYSTEMVALUE RGB_Decode_LMN(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  if ( arg < 0.0 )
    arg = 0.0 ;
  else
    arg = (SYSTEMVALUE)pow((double)arg , (double)(1.0 / 0.45)) ;

  return arg ;
}


/* The next three functions implement Decode for L, M, N channels for
   CIE 1976 L*a*b* with CCIR XA/11 recommended white point (RB2, p.191).
   Decode L:
     { dup 6 29 div ge {dup dup mul mul}
        {4 29 div sub 108 841 div mul} ifelse 0.9505 mul}
*/
static SYSTEMVALUE Lab_D65_Decode_L(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  LAB_STAR_DECODE(arg) ;

  return arg * 0.9505 ;
}

/* Decode M:
   { dup 6 29 div ge {dup dup mul mul}
        {4 29 div sub 108 841 div mul} ifelse}
*/
static SYSTEMVALUE Lab_Decode_M(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  LAB_STAR_DECODE(arg) ;

  return arg ;
}

/* Decode N:
   { dup 6 29 div ge {dup dup mul mul}
        {4 29 div sub 108 841 div mul} ifelse 1.0890 mul}
*/
static SYSTEMVALUE Lab_D65_Decode_N(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  LAB_STAR_DECODE(arg) ;

  return arg * 1.0890 ;
}


/* The next four functions implement Decodes for L* dimension of CIE 1976
   L*a*b* space with CCIR XA/11 recommended white point (RB2, p.193).
   Decode A:
     { 16 add 116 div }
*/
static SYSTEMVALUE Lab_Decode_A(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  return (arg + 16.0) / 116.0 ;
}

/* Decode B:
     { 500 div }
*/
static SYSTEMVALUE Lab_Decode_B(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  return arg / 500.0 ;
}

/* Decode C:
     { 200 div }
*/
static SYSTEMVALUE Lab_Decode_C(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  return arg / 200.0 ;
}

/* Decode LMN:
     { 16 add 116 div dup 6 29 div ge {dup dup mul mul}
        {4 29 div sub 108 841 div mul} ifelse}
*/
static SYSTEMVALUE L_star_Decode_LMN(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  arg += 16.0 ;
  arg /= 116.0 ;

  LAB_STAR_DECODE(arg) ;

  return arg ;
}


/* The next function is the default ScriptWorks EncodeLMN:
     { 0.45 /exp load stopped { pop pop 0 } if }
*/
static SYSTEMVALUE Default_Encode_LMN(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  if ( arg < 0.0 )
    arg = 0.0 ;
  else
    arg = (SYSTEMVALUE)pow((double) arg , (double)0.45) ;

  return arg ;
}


/* The next three functions implement the default ScriptWorks Transform PQRs.
   These functions need access to the tristimulus values which are calculated
   in cietodev.c.
   Transform P:
     {4 index 3 get div 2 index 3 get mul 4 {exch pop} repeat}
*/
static SYSTEMVALUE Default_Transform_P(SYSTEMVALUE arg, void *extra)
{
  OBJECT *tristimulus = (OBJECT *)extra ;

  arg /= oReal(tristimulus[3]) ;    /* divide by white src P */
  arg *= oReal(tristimulus[15]) ;   /* multiply by white dest P */

  return arg ;
}

/* Transform Q:
     {4 index 4 get div 2 index 4 get mul 4 {exch pop} repeat}
*/
static SYSTEMVALUE Default_Transform_Q(SYSTEMVALUE arg, void *extra)
{
  OBJECT *tristimulus = (OBJECT *)extra ;

  arg /= oReal(tristimulus[4]) ;    /* divide by white src Q */
  arg *= oReal(tristimulus[16]) ;   /* multiply by white dest Q */

  return arg ;
}

/* Transform R:
     {4 index 5 get div 2 index 5 get mul 4 {exch pop} repeat}
*/
static SYSTEMVALUE Default_Transform_R(SYSTEMVALUE arg, void *extra)
{
  OBJECT *tristimulus = (OBJECT *)extra ;

  arg /= oReal(tristimulus[5]) ;    /* divide by white src R */
  arg *= oReal(tristimulus[17]) ;   /* multiply by white dest R */

  return arg ;
}


/* The next three functions implement the Transform PQRs put into crd's installed
   from ICC profiles. They implement a simple scaling of XYZ values (PQR == XYZ in
   the instance of ICC installed profiles.
   Transform P:
     {4 index 0 get div 2 index 0 get mul 4 {exch pop} repeat}
*/
static SYSTEMVALUE ICC_Transform_P(SYSTEMVALUE arg, void *extra)
{
  OBJECT *tristimulus = (OBJECT *)extra ;

  arg /= oReal(tristimulus[0]) ;    /* divide by white src X */
  arg *= oReal(tristimulus[12]) ;   /* multiply by white dest X */

  return arg ;
}

/* Transform Q:
     {4 index 1 get div 2 index 1 get mul 4 {exch pop} repeat}
*/
static SYSTEMVALUE ICC_Transform_Q(SYSTEMVALUE arg, void *extra)
{
  OBJECT *tristimulus = (OBJECT *)extra ;

  arg /= oReal(tristimulus[1]) ;    /* divide by white src Y */
  arg *= oReal(tristimulus[13]) ;   /* multiply by white dest Y */

  return arg ;
}

/* Transform R:
     {4 index 2 get div 2 index 2 get mul 4 {exch pop} repeat}
*/
static SYSTEMVALUE ICC_Transform_R(SYSTEMVALUE arg, void *extra)
{
  OBJECT *tristimulus = (OBJECT *)extra ;

  arg /= oReal(tristimulus[2]) ;    /* divide by white src Z */
  arg *= oReal(tristimulus[14]) ;   /* multiply by white dest Z */

  return arg ;
}


/* This function implements the default ScriptWorks RenderTable function:
     {0.998 mul 0.001 add}
*/
static SYSTEMVALUE Default_RenderTable(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  return arg * 0.998 + 0.001 ;
}


/* Implements Decode for CIE based RGB used by Photoshop:
     { 1.8 exp }
*/
static SYSTEMVALUE Photoshop_RGB_Decode_LMN(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  if ( arg < 0.0 )
    arg = 0.0 ;
  else
    arg = (SYSTEMVALUE)pow((double)arg , (double)1.8) ;

  return arg ;
}


/* The next two functions implement Decode for L, N channels for
   CIE 1976 L*a*b* with D50 white point. M channel is same as D65 white point.
   Decode L:
     { dup 6 29 div ge {dup dup mul mul}
        {4 29 div sub 108 841 div mul} ifelse 0.9642 mul}
*/
static SYSTEMVALUE Lab_D50_Decode_L(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  LAB_STAR_DECODE(arg) ;

  return arg * 0.9642 ;
}

/*
   Decode N:
     { dup 6 29 div ge {dup dup mul mul}
        {4 29 div sub 108 841 div mul} ifelse 0.8249 mul}
*/
static SYSTEMVALUE Lab_D50_Decode_N(SYSTEMVALUE arg, void *extra)
{
#ifdef HAS_PRAGMA_UNUSED
#pragma unused(extra)
#endif
  UNUSED_PARAM(void *, extra);

  LAB_STAR_DECODE(arg) ;

  return arg * 0.8249 ;
}


/* Log stripped */
