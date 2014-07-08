/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Operators API
 */

#ifndef __PDFOPS_H__
#define __PDFOPS_H__

/** \defgroup pdfops PDF Operators.
    \ingroup pdfin */

#include "pdfexec.h"

/* ----- External constants ----- */

#define OPSTATE_ERROR      0x00
#define OPSTATE_PDL        0x01
#define OPSTATE_PATHOBJECT 0x02
#define OPSTATE_TEXTOBJECT 0x04
#define OPSTATE_EXTOBJECT  0x08
#define OPSTATE_IMGOBJECT  0x10
#define OPSTATE_TYPE3FONT  0x20
#define OPSTATE_VIGOBJECT  0x40

#define OPSTATE_PDL_OR_TEXT  OPSTATE_PDL | OPSTATE_TEXTOBJECT
#define OPSTATE_PDL_OR_PATH  OPSTATE_PDL | OPSTATE_PATHOBJECT
#define OPSTATE_PATH_OR_TEXT OPSTATE_PATHOBJECT | OPSTATE_TEXTOBJECT
#define OPSTATE_ALL 0xff

/* Unique identifiers for the pdfop structure. These do not have to be in any
   particular order, and in particular don't need to match the ordering used
   for the perfect hash (see pdf_whichop) */
enum {
  PDFOP_NONE = 0,  /* NONE must be zero */
  PDFOP_b,
  PDFOP_B,
  PDFOP_B1s,
  PDFOP_b1s,
  PDFOP_BDC,
  PDFOP_BIV,
  PDFOP_BMC,
  PDFOP_BT,
  PDFOP_BX,
  PDFOP_c,
  PDFOP_cm,
  PDFOP_cQ,
  PDFOP_cq,
  PDFOP_cs,
  PDFOP_CS,
  PDFOP_d,
  PDFOP_d0,
  PDFOP_d1,
  PDFOP_Do,
  PDFOP_DP,
  PDFOP_EIV,
  PDFOP_EMC,
  PDFOP_ET,
  PDFOP_EX,
  PDFOP_f,
  PDFOP_f1s,
  PDFOP_G,
  PDFOP_g,
  PDFOP_gs,
  PDFOP_h,
  PDFOP_i,
  PDFOP_ID,
  PDFOP_j,
  PDFOP_J,
  PDFOP_k,
  PDFOP_K,
  PDFOP_l,
  PDFOP_m,
  PDFOP_M,
  PDFOP_MP,
  PDFOP_n,
  PDFOP_PS,
  PDFOP_q,
  PDFOP_Q,
  PDFOP_re,
  PDFOP_RG,
  PDFOP_rg,
  PDFOP_ri,
  PDFOP_s,
  PDFOP_S,
  PDFOP_SC,
  PDFOP_sc,
  PDFOP_scn,
  PDFOP_SCN,
  PDFOP_sh,
  PDFOP_T1q,
  PDFOP_T1s,
  PDFOP_T2q,
  PDFOP_Tc,
  PDFOP_Td,
  PDFOP_TD,
  PDFOP_Tf,
  PDFOP_TJ,
  PDFOP_Tj,
  PDFOP_TL,
  PDFOP_Tm,
  PDFOP_Tr,
  PDFOP_Ts,
  PDFOP_Tw,
  PDFOP_Tz,
  PDFOP_v,
  PDFOP_W,
  PDFOP_w,
  PDFOP_W1s,
  PDFOP_y
} ;

/* ----- External structures ----- */

typedef int32 (* PDFOPFN)( PDFCONTEXT *pdfc ) ;

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

uint8 *pdf_op_name( void *pdfop ) ;
uint8 pdf_op_number( void *pdfop ) ;
Bool pdf_op_marking( void *pdfop ) ;
PDFOPFN pdf_op_call( void *pdfop ) ;

Bool pdf_whichop( int32 ch1 , int32 ch2 , int32 ch3 ) ;
int32 pdf_execops( PDFCONTEXT *pdfc , int32 state , int stream_type ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
