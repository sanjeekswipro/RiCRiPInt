/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!src:gu_splat.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Each routine here do an inverse dct of a single coefficient.
 */

/*---------------------- INLCUDES --------------------------------*/

#include "core.h"
#include "dctimpl.h"
#include "gu_splat.h"

/*---------------------- MACROS ----------------------------------*/

#define COS_00 0x2000 /* FIX(0.12500000) */
#define COS_01 0x2C63 /* FIX(0.17338000) */
#define COS_02 0x29CF /* FIX(0.16332000) */
#define COS_03 0x25A1 /* FIX(0.14698400) */
#define COS_04 0x2000 /* FIX(0.12500000) */
#define COS_05 0x1924 /* FIX(0.09821190) */
#define COS_06 0x1151 /* FIX(0.06764950) */
#define COS_07 0x08D4 /* FIX(0.03448740) */
#define COS_10 0x2C63 /* FIX(0.17338000) */
#define COS_11 0x3D90 /* FIX(0.24048500) */
#define COS_12 0x39FE /* FIX(0.22653200) */
#define COS_13 0x3431 /* FIX(0.20387300) */
#define COS_14 0x2C63 /* FIX(0.17338000) */
#define COS_15 0x22E0 /* FIX(0.13622400) */
#define COS_16 0x1805 /* FIX(0.09383260) */
#define COS_17 0x0C3F /* FIX(0.04783540) */
#define COS_20 0x29CF /* FIX(0.16332000) */
#define COS_21 0x39FE /* FIX(0.22653200) */
#define COS_22 0x36A1 /* FIX(0.21338800) */
#define COS_23 0x312A /* FIX(0.19204400) */
#define COS_24 0x29CF /* FIX(0.16332000) */
#define COS_25 0x20DA /* FIX(0.12832000) */
#define COS_26 0x16A1 /* FIX(0.08838830) */
#define COS_27 0x0B89 /* FIX(0.04506000) */
#define COS_30 0x25A1 /* FIX(0.14698400) */
#define COS_31 0x3431 /* FIX(0.20387300) */
#define COS_32 0x312A /* FIX(0.19204400) */
#define COS_33 0x2C3F /* FIX(0.17283500) */
#define COS_34 0x25A1 /* FIX(0.14698400) */
#define COS_35 0x1D90 /* FIX(0.11548500) */
#define COS_36 0x145D /* FIX(0.07954740) */
#define COS_37 0x0A62 /* FIX(0.04055290) */
#define COS_40 0x2000 /* FIX(0.12500000) */
#define COS_41 0x2C63 /* FIX(0.17338000) */
#define COS_42 0x29CF /* FIX(0.16332000) */
#define COS_43 0x25A1 /* FIX(0.14698400) */
#define COS_44 0x2000 /* FIX(0.12500000) */
#define COS_45 0x1924 /* FIX(0.09821190) */
#define COS_46 0x1151 /* FIX(0.06764950) */
#define COS_47 0x08D4 /* FIX(0.03448740) */
#define COS_50 0x1924 /* FIX(0.09821190) */
#define COS_51 0x22E0 /* FIX(0.13622400) */
#define COS_52 0x20DA /* FIX(0.12832000) */
#define COS_53 0x1D90 /* FIX(0.11548500) */
#define COS_54 0x1924 /* FIX(0.09821190) */
#define COS_55 0x13C1 /* FIX(0.07716460) */
#define COS_56 0x0D9B /* FIX(0.05315190) */
#define COS_57 0x06F0 /* FIX(0.02709660) */
#define COS_60 0x1151 /* FIX(0.06764950) */
#define COS_61 0x1805 /* FIX(0.09383260) */
#define COS_62 0x16A1 /* FIX(0.08838830) */
#define COS_63 0x145D /* FIX(0.07954740) */
#define COS_64 0x1151 /* FIX(0.06764950) */
#define COS_65 0x0D9B /* FIX(0.05315190) */
#define COS_66 0x095F /* FIX(0.03661170) */
#define COS_67 0x04C7 /* FIX(0.01866450) */
#define COS_70 0x08D4 /* FIX(0.03448740) */
#define COS_71 0x0C3F /* FIX(0.04783540) */
#define COS_72 0x0B89 /* FIX(0.04506000) */
#define COS_73 0x0A62 /* FIX(0.04055290) */
#define COS_74 0x08D4 /* FIX(0.03448740) */
#define COS_75 0x06F0 /* FIX(0.02709660) */
#define COS_76 0x04C7 /* FIX(0.01866450) */
#define COS_77 0x0270 /* FIX(0.00951506) */

static int32 tile[ 8 ][ 8 ] ;

void splat_00(register int32 coeff)
{
  register int32 c0;

  c0 = COS_00 * coeff;

  tile[0][0] = c0; tile[0][1] = c0; tile[0][2] = c0; tile[0][3] = c0;
  tile[0][4] = c0; tile[0][5] = c0; tile[0][6] = c0; tile[0][7] = c0;

  tile[1][0] = c0; tile[1][1] = c0; tile[1][2] = c0; tile[1][3] = c0;
  tile[1][4] = c0; tile[1][5] = c0; tile[1][6] = c0; tile[1][7] = c0;

  tile[2][0] = c0; tile[2][1] = c0; tile[2][2] = c0; tile[2][3] = c0;
  tile[2][4] = c0; tile[2][5] = c0; tile[2][6] = c0; tile[2][7] = c0;

  tile[3][0] = c0; tile[3][1] = c0; tile[3][2] = c0; tile[3][3] = c0;
  tile[3][4] = c0; tile[3][5] = c0; tile[3][6] = c0; tile[3][7] = c0;

  tile[4][0] = c0; tile[4][1] = c0; tile[4][2] = c0; tile[4][3] = c0;
  tile[4][4] = c0; tile[4][5] = c0; tile[4][6] = c0; tile[4][7] = c0;

  tile[5][0] = c0; tile[5][1] = c0; tile[5][2] = c0; tile[5][3] = c0;
  tile[5][4] = c0; tile[5][5] = c0; tile[5][6] = c0; tile[5][7] = c0;

  tile[6][0] = c0; tile[6][1] = c0; tile[6][2] = c0; tile[6][3] = c0;
  tile[6][4] = c0; tile[6][5] = c0; tile[6][6] = c0; tile[6][7] = c0;

  tile[7][0] = c0; tile[7][1] = c0; tile[7][2] = c0; tile[7][3] = c0;
  tile[7][4] = c0; tile[7][5] = c0; tile[7][6] = c0; tile[7][7] = c0;
}

void splat_01(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_01;
  c1 = coeff * COS_03;
  c2 = coeff * COS_05;
  c3 = coeff * COS_07;

  tile[0][0] += c0; tile[0][1] += c1; tile[0][2] += c2; tile[0][3] += c3;
  tile[0][4] -= c3; tile[0][5] -= c2; tile[0][6] -= c1; tile[0][7] -= c0;

  tile[1][0] += c0; tile[1][1] += c1; tile[1][2] += c2; tile[1][3] += c3;
  tile[1][4] -= c3; tile[1][5] -= c2; tile[1][6] -= c1; tile[1][7] -= c0;

  tile[2][0] += c0; tile[2][1] += c1; tile[2][2] += c2; tile[2][3] += c3;
  tile[2][4] -= c3; tile[2][5] -= c2; tile[2][6] -= c1; tile[2][7] -= c0;

  tile[3][0] += c0; tile[3][1] += c1; tile[3][2] += c2; tile[3][3] += c3;
  tile[3][4] -= c3; tile[3][5] -= c2; tile[3][6] -= c1; tile[3][7] -= c0;

  tile[4][0] += c0; tile[4][1] += c1; tile[4][2] += c2; tile[4][3] += c3;
  tile[4][4] -= c3; tile[4][5] -= c2; tile[4][6] -= c1; tile[4][7] -= c0;

  tile[5][0] += c0; tile[5][1] += c1; tile[5][2] += c2; tile[5][3] += c3;
  tile[5][4] -= c3; tile[5][5] -= c2; tile[5][6] -= c1; tile[5][7] -= c0;

  tile[6][0] += c0; tile[6][1] += c1; tile[6][2] += c2; tile[6][3] += c3;
  tile[6][4] -= c3; tile[6][5] -= c2; tile[6][6] -= c1; tile[6][7] -= c0;

  tile[7][0] += c0; tile[7][1] += c1; tile[7][2] += c2; tile[7][3] += c3;
  tile[7][4] -= c3; tile[7][5] -= c2; tile[7][6] -= c1; tile[7][7] -= c0;
}

void splat_02(register int32 coeff)
{
  register int32 c0,c1;

  c0 = coeff * COS_02;
  c1 = coeff * COS_06;

  tile[0][0] += c0; tile[0][1] += c1; tile[0][2] -= c1; tile[0][3] -= c0;
  tile[0][4] -= c0; tile[0][5] -= c1; tile[0][6] += c1; tile[0][7] += c0;

  tile[1][0] += c0; tile[1][1] += c1; tile[1][2] -= c1; tile[1][3] -= c0;
  tile[1][4] -= c0; tile[1][5] -= c1; tile[1][6] += c1; tile[1][7] += c0;

  tile[2][0] += c0; tile[2][1] += c1; tile[2][2] -= c1; tile[2][3] -= c0;
  tile[2][4] -= c0; tile[2][5] -= c1; tile[2][6] += c1; tile[2][7] += c0;

  tile[3][0] += c0; tile[3][1] += c1; tile[3][2] -= c1; tile[3][3] -= c0;
  tile[3][4] -= c0; tile[3][5] -= c1; tile[3][6] += c1; tile[3][7] += c0;

  tile[4][0] += c0; tile[4][1] += c1; tile[4][2] -= c1; tile[4][3] -= c0;
  tile[4][4] -= c0; tile[4][5] -= c1; tile[4][6] += c1; tile[4][7] += c0;

  tile[5][0] += c0; tile[5][1] += c1; tile[5][2] -= c1; tile[5][3] -= c0;
  tile[5][4] -= c0; tile[5][5] -= c1; tile[5][6] += c1; tile[5][7] += c0;

  tile[6][0] += c0; tile[6][1] += c1; tile[6][2] -= c1; tile[6][3] -= c0;
  tile[6][4] -= c0; tile[6][5] -= c1; tile[6][6] += c1; tile[6][7] += c0;

  tile[7][0] += c0; tile[7][1] += c1; tile[7][2] -= c1; tile[7][3] -= c0;
  tile[7][4] -= c0; tile[7][5] -= c1; tile[7][6] += c1; tile[7][7] += c0;
}

void splat_03(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_01;
  c1 = coeff * COS_03;
  c2 = coeff * COS_05;
  c3 = coeff * COS_07;

  tile[0][0] += c1; tile[0][1] -= c3; tile[0][2] -= c0; tile[0][3] -= c2;
  tile[0][4] += c2; tile[0][5] += c0; tile[0][6] += c3; tile[0][7] -= c1;

  tile[1][0] += c1; tile[1][1] -= c3; tile[1][2] -= c0; tile[1][3] -= c2;
  tile[1][4] += c2; tile[1][5] += c0; tile[1][6] += c3; tile[1][7] -= c1;

  tile[2][0] += c1; tile[2][1] -= c3; tile[2][2] -= c0; tile[2][3] -= c2;
  tile[2][4] += c2; tile[2][5] += c0; tile[2][6] += c3; tile[2][7] -= c1;

  tile[3][0] += c1; tile[3][1] -= c3; tile[3][2] -= c0; tile[3][3] -= c2;
  tile[3][4] += c2; tile[3][5] += c0; tile[3][6] += c3; tile[3][7] -= c1;

  tile[4][0] += c1; tile[4][1] -= c3; tile[4][2] -= c0; tile[4][3] -= c2;
  tile[4][4] += c2; tile[4][5] += c0; tile[4][6] += c3; tile[4][7] -= c1;

  tile[5][0] += c1; tile[5][1] -= c3; tile[5][2] -= c0; tile[5][3] -= c2;
  tile[5][4] += c2; tile[5][5] += c0; tile[5][6] += c3; tile[5][7] -= c1;

  tile[6][0] += c1; tile[6][1] -= c3; tile[6][2] -= c0; tile[6][3] -= c2;
  tile[6][4] += c2; tile[6][5] += c0; tile[6][6] += c3; tile[6][7] -= c1;

  tile[7][0] += c1; tile[7][1] -= c3; tile[7][2] -= c0; tile[7][3] -= c2;
  tile[7][4] += c2; tile[7][5] += c0; tile[7][6] += c3; tile[7][7] -= c1;
}

void splat_04(register int32 coeff)
{
  register int32 c0;

  c0 = coeff * COS_04;

  tile[0][0] += c0; tile[0][1] -= c0; tile[0][2] -= c0; tile[0][3] += c0;
  tile[0][4] += c0; tile[0][5] -= c0; tile[0][6] -= c0; tile[0][7] += c0;

  tile[1][0] += c0; tile[1][1] -= c0; tile[1][2] -= c0; tile[1][3] += c0;
  tile[1][4] += c0; tile[1][5] -= c0; tile[1][6] -= c0; tile[1][7] += c0;

  tile[2][0] += c0; tile[2][1] -= c0; tile[2][2] -= c0; tile[2][3] += c0;
  tile[2][4] += c0; tile[2][5] -= c0; tile[2][6] -= c0; tile[2][7] += c0;

  tile[3][0] += c0; tile[3][1] -= c0; tile[3][2] -= c0; tile[3][3] += c0;
  tile[3][4] += c0; tile[3][5] -= c0; tile[3][6] -= c0; tile[3][7] += c0;

  tile[4][0] += c0; tile[4][1] -= c0; tile[4][2] -= c0; tile[4][3] += c0;
  tile[4][4] += c0; tile[4][5] -= c0; tile[4][6] -= c0; tile[4][7] += c0;

  tile[5][0] += c0; tile[5][1] -= c0; tile[5][2] -= c0; tile[5][3] += c0;
  tile[5][4] += c0; tile[5][5] -= c0; tile[5][6] -= c0; tile[5][7] += c0;

  tile[6][0] += c0; tile[6][1] -= c0; tile[6][2] -= c0; tile[6][3] += c0;
  tile[6][4] += c0; tile[6][5] -= c0; tile[6][6] -= c0; tile[6][7] += c0;

  tile[7][0] += c0; tile[7][1] -= c0; tile[7][2] -= c0; tile[7][3] += c0;
  tile[7][4] += c0; tile[7][5] -= c0; tile[7][6] -= c0; tile[7][7] += c0;
}

void splat_05(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_01;
  c1 = coeff * COS_03;
  c2 = coeff * COS_05;
  c3 = coeff * COS_07;

  tile[0][0] += c2; tile[0][1] -= c0; tile[0][2] += c3; tile[0][3] += c1;
  tile[0][4] -= c1; tile[0][5] -= c3; tile[0][6] += c0; tile[0][7] -= c2;

  tile[1][0] += c2; tile[1][1] -= c0; tile[1][2] += c3; tile[1][3] += c1;
  tile[1][4] -= c1; tile[1][5] -= c3; tile[1][6] += c0; tile[1][7] -= c2;

  tile[2][0] += c2; tile[2][1] -= c0; tile[2][2] += c3; tile[2][3] += c1;
  tile[2][4] -= c1; tile[2][5] -= c3; tile[2][6] += c0; tile[2][7] -= c2;

  tile[3][0] += c2; tile[3][1] -= c0; tile[3][2] += c3; tile[3][3] += c1;
  tile[3][4] -= c1; tile[3][5] -= c3; tile[3][6] += c0; tile[3][7] -= c2;

  tile[4][0] += c2; tile[4][1] -= c0; tile[4][2] += c3; tile[4][3] += c1;
  tile[4][4] -= c1; tile[4][5] -= c3; tile[4][6] += c0; tile[4][7] -= c2;

  tile[5][0] += c2; tile[5][1] -= c0; tile[5][2] += c3; tile[5][3] += c1;
  tile[5][4] -= c1; tile[5][5] -= c3; tile[5][6] += c0; tile[5][7] -= c2;

  tile[6][0] += c2; tile[6][1] -= c0; tile[6][2] += c3; tile[6][3] += c1;
  tile[6][4] -= c1; tile[6][5] -= c3; tile[6][6] += c0; tile[6][7] -= c2;

  tile[7][0] += c2; tile[7][1] -= c0; tile[7][2] += c3; tile[7][3] += c1;
  tile[7][4] -= c1; tile[7][5] -= c3; tile[7][6] += c0; tile[7][7] -= c2;
}

void splat_06(register int32 coeff)
{
  register int32 c0,c1;

  c0 = coeff * COS_02;
  c1 = coeff * COS_06;

  tile[0][0] += c1; tile[0][1] -= c0; tile[0][2] += c0; tile[0][3] -= c1;
  tile[0][4] -= c1; tile[0][5] += c0; tile[0][6] -= c0; tile[0][7] += c1;

  tile[1][0] += c1; tile[1][1] -= c0; tile[1][2] += c0; tile[1][3] -= c1;
  tile[1][4] -= c1; tile[1][5] += c0; tile[1][6] -= c0; tile[1][7] += c1;

  tile[2][0] += c1; tile[2][1] -= c0; tile[2][2] += c0; tile[2][3] -= c1;
  tile[2][4] -= c1; tile[2][5] += c0; tile[2][6] -= c0; tile[2][7] += c1;

  tile[3][0] += c1; tile[3][1] -= c0; tile[3][2] += c0; tile[3][3] -= c1;
  tile[3][4] -= c1; tile[3][5] += c0; tile[3][6] -= c0; tile[3][7] += c1;

  tile[4][0] += c1; tile[4][1] -= c0; tile[4][2] += c0; tile[4][3] -= c1;
  tile[4][4] -= c1; tile[4][5] += c0; tile[4][6] -= c0; tile[4][7] += c1;

  tile[5][0] += c1; tile[5][1] -= c0; tile[5][2] += c0; tile[5][3] -= c1;
  tile[5][4] -= c1; tile[5][5] += c0; tile[5][6] -= c0; tile[5][7] += c1;

  tile[6][0] += c1; tile[6][1] -= c0; tile[6][2] += c0; tile[6][3] -= c1;
  tile[6][4] -= c1; tile[6][5] += c0; tile[6][6] -= c0; tile[6][7] += c1;

  tile[7][0] += c1; tile[7][1] -= c0; tile[7][2] += c0; tile[7][3] -= c1;
  tile[7][4] -= c1; tile[7][5] += c0; tile[7][6] -= c0; tile[7][7] += c1;
}

void splat_07(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_01;
  c1 = coeff * COS_03;
  c2 = coeff * COS_05;
  c3 = coeff * COS_07;

  tile[0][0] += c3; tile[0][1] -= c2; tile[0][2] += c1; tile[0][3] -= c0;
  tile[0][4] += c0; tile[0][5] -= c1; tile[0][6] += c2; tile[0][7] -= c3;

  tile[1][0] += c3; tile[1][1] -= c2; tile[1][2] += c1; tile[1][3] -= c0;
  tile[1][4] += c0; tile[1][5] -= c1; tile[1][6] += c2; tile[1][7] -= c3;

  tile[2][0] += c3; tile[2][1] -= c2; tile[2][2] += c1; tile[2][3] -= c0;
  tile[2][4] += c0; tile[2][5] -= c1; tile[2][6] += c2; tile[2][7] -= c3;

  tile[3][0] += c3; tile[3][1] -= c2; tile[3][2] += c1; tile[3][3] -= c0;
  tile[3][4] += c0; tile[3][5] -= c1; tile[3][6] += c2; tile[3][7] -= c3;

  tile[4][0] += c3; tile[4][1] -= c2; tile[4][2] += c1; tile[4][3] -= c0;
  tile[4][4] += c0; tile[4][5] -= c1; tile[4][6] += c2; tile[4][7] -= c3;

  tile[5][0] += c3; tile[5][1] -= c2; tile[5][2] += c1; tile[5][3] -= c0;
  tile[5][4] += c0; tile[5][5] -= c1; tile[5][6] += c2; tile[5][7] -= c3;

  tile[6][0] += c3; tile[6][1] -= c2; tile[6][2] += c1; tile[6][3] -= c0;
  tile[6][4] += c0; tile[6][5] -= c1; tile[6][6] += c2; tile[6][7] -= c3;

  tile[7][0] += c3; tile[7][1] -= c2; tile[7][2] += c1; tile[7][3] -= c0;
  tile[7][4] += c0; tile[7][5] -= c1; tile[7][6] += c2; tile[7][7] -= c3;
}

void splat_10(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_01;
  c1 = coeff * COS_03;
  c2 = coeff * COS_05;
  c3 = coeff * COS_07;

  tile[0][0] += c0; tile[0][1] += c0; tile[0][2] += c0; tile[0][3] += c0;
  tile[0][4] += c0; tile[0][5] += c0; tile[0][6] += c0; tile[0][7] += c0;

  tile[1][0] += c1; tile[1][1] += c1; tile[1][2] += c1; tile[1][3] += c1;
  tile[1][4] += c1; tile[1][5] += c1; tile[1][6] += c1; tile[1][7] += c1;

  tile[2][0] += c2; tile[2][1] += c2; tile[2][2] += c2; tile[2][3] += c2;
  tile[2][4] += c2; tile[2][5] += c2; tile[2][6] += c2; tile[2][7] += c2;

  tile[3][0] += c3; tile[3][1] += c3; tile[3][2] += c3; tile[3][3] += c3;
  tile[3][4] += c3; tile[3][5] += c3; tile[3][6] += c3; tile[3][7] += c3;

  tile[4][0] -= c3; tile[4][1] -= c3; tile[4][2] -= c3; tile[4][3] -= c3;
  tile[4][4] -= c3; tile[4][5] -= c3; tile[4][6] -= c3; tile[4][7] -= c3;

  tile[5][0] -= c2; tile[5][1] -= c2; tile[5][2] -= c2; tile[5][3] -= c2;
  tile[5][4] -= c2; tile[5][5] -= c2; tile[5][6] -= c2; tile[5][7] -= c2;

  tile[6][0] -= c1; tile[6][1] -= c1; tile[6][2] -= c1; tile[6][3] -= c1;
  tile[6][4] -= c1; tile[6][5] -= c1; tile[6][6] -= c1; tile[6][7] -= c1;

  tile[7][0] -= c0; tile[7][1] -= c0; tile[7][2] -= c0; tile[7][3] -= c0;
  tile[7][4] -= c0; tile[7][5] -= c0; tile[7][6] -= c0; tile[7][7] -= c0;
}

void splat_11(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c0; tile[0][1] += c1; tile[0][2] += c2; tile[0][3] += c3;
  tile[0][4] -= c3; tile[0][5] -= c2; tile[0][6] -= c1; tile[0][7] -= c0;

  tile[1][0] += c1; tile[1][1] += c4; tile[1][2] += c5; tile[1][3] += c6;
  tile[1][4] -= c6; tile[1][5] -= c5; tile[1][6] -= c4; tile[1][7] -= c1;

  tile[2][0] += c2; tile[2][1] += c5; tile[2][2] += c7; tile[2][3] += c8;
  tile[2][4] -= c8; tile[2][5] -= c7; tile[2][6] -= c5; tile[2][7] -= c2;

  tile[3][0] += c3; tile[3][1] += c6; tile[3][2] += c8; tile[3][3] += c9;
  tile[3][4] -= c9; tile[3][5] -= c8; tile[3][6] -= c6; tile[3][7] -= c3;

  tile[4][0] -= c3; tile[4][1] -= c6; tile[4][2] -= c8; tile[4][3] -= c9;
  tile[4][4] += c9; tile[4][5] += c8; tile[4][6] += c6; tile[4][7] += c3;

  tile[5][0] -= c2; tile[5][1] -= c5; tile[5][2] -= c7; tile[5][3] -= c8;
  tile[5][4] += c8; tile[5][5] += c7; tile[5][6] += c5; tile[5][7] += c2;

  tile[6][0] -= c1; tile[6][1] -= c4; tile[6][2] -= c5; tile[6][3] -= c6;
  tile[6][4] += c6; tile[6][5] += c5; tile[6][6] += c4; tile[6][7] += c1;

  tile[7][0] -= c0; tile[7][1] -= c1; tile[7][2] -= c2; tile[7][3] -= c3;
  tile[7][4] += c3; tile[7][5] += c2; tile[7][6] += c1; tile[7][7] += c0;
}

void splat_12(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c0; tile[0][1] += c1; tile[0][2] -= c1; tile[0][3] -= c0;
  tile[0][4] -= c0; tile[0][5] -= c1; tile[0][6] += c1; tile[0][7] += c0;

  tile[1][0] += c2; tile[1][1] += c3; tile[1][2] -= c3; tile[1][3] -= c2;
  tile[1][4] -= c2; tile[1][5] -= c3; tile[1][6] += c3; tile[1][7] += c2;

  tile[2][0] += c4; tile[2][1] += c5; tile[2][2] -= c5; tile[2][3] -= c4;
  tile[2][4] -= c4; tile[2][5] -= c5; tile[2][6] += c5; tile[2][7] += c4;

  tile[3][0] += c6; tile[3][1] += c7; tile[3][2] -= c7; tile[3][3] -= c6;
  tile[3][4] -= c6; tile[3][5] -= c7; tile[3][6] += c7; tile[3][7] += c6;

  tile[4][0] -= c6; tile[4][1] -= c7; tile[4][2] += c7; tile[4][3] += c6;
  tile[4][4] += c6; tile[4][5] += c7; tile[4][6] -= c7; tile[4][7] -= c6;

  tile[5][0] -= c4; tile[5][1] -= c5; tile[5][2] += c5; tile[5][3] += c4;
  tile[5][4] += c4; tile[5][5] += c5; tile[5][6] -= c5; tile[5][7] -= c4;

  tile[6][0] -= c2; tile[6][1] -= c3; tile[6][2] += c3; tile[6][3] += c2;
  tile[6][4] += c2; tile[6][5] += c3; tile[6][6] -= c3; tile[6][7] -= c2;

  tile[7][0] -= c0; tile[7][1] -= c1; tile[7][2] += c1; tile[7][3] += c0;
  tile[7][4] += c0; tile[7][5] += c1; tile[7][6] -= c1; tile[7][7] -= c0;
}

void splat_13(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c1; tile[0][1] -= c3; tile[0][2] -= c0; tile[0][3] -= c2;
  tile[0][4] += c2; tile[0][5] += c0; tile[0][6] += c3; tile[0][7] -= c1;

  tile[1][0] += c4; tile[1][1] -= c6; tile[1][2] -= c1; tile[1][3] -= c5;
  tile[1][4] += c5; tile[1][5] += c1; tile[1][6] += c6; tile[1][7] -= c4;

  tile[2][0] += c5; tile[2][1] -= c8; tile[2][2] -= c2; tile[2][3] -= c7;
  tile[2][4] += c7; tile[2][5] += c2; tile[2][6] += c8; tile[2][7] -= c5;

  tile[3][0] += c6; tile[3][1] -= c9; tile[3][2] -= c3; tile[3][3] -= c8;
  tile[3][4] += c8; tile[3][5] += c3; tile[3][6] += c9; tile[3][7] -= c6;

  tile[4][0] -= c6; tile[4][1] += c9; tile[4][2] += c3; tile[4][3] += c8;
  tile[4][4] -= c8; tile[4][5] -= c3; tile[4][6] -= c9; tile[4][7] += c6;

  tile[5][0] -= c5; tile[5][1] += c8; tile[5][2] += c2; tile[5][3] += c7;
  tile[5][4] -= c7; tile[5][5] -= c2; tile[5][6] -= c8; tile[5][7] += c5;

  tile[6][0] -= c4; tile[6][1] += c6; tile[6][2] += c1; tile[6][3] += c5;
  tile[6][4] -= c5; tile[6][5] -= c1; tile[6][6] -= c6; tile[6][7] += c4;

  tile[7][0] -= c1; tile[7][1] += c3; tile[7][2] += c0; tile[7][3] += c2;
  tile[7][4] -= c2; tile[7][5] -= c0; tile[7][6] -= c3; tile[7][7] += c1;
}

void splat_14(register int32 coeff)
{
  register int32 c0,c1,c2,c3;
  
  c0 = coeff * COS_14;
  c1 = coeff * COS_34;
  c2 = coeff * COS_54;
  c3 = coeff * COS_74;

  tile[0][0] += c0; tile[0][1] -= c0; tile[0][2] -= c0; tile[0][3] += c0;
  tile[0][4] += c0; tile[0][5] -= c0; tile[0][6] -= c0; tile[0][7] += c0;

  tile[1][0] += c1; tile[1][1] -= c1; tile[1][2] -= c1; tile[1][3] += c1;
  tile[1][4] += c1; tile[1][5] -= c1; tile[1][6] -= c1; tile[1][7] += c1;

  tile[2][0] += c2; tile[2][1] -= c2; tile[2][2] -= c2; tile[2][3] += c2;
  tile[2][4] += c2; tile[2][5] -= c2; tile[2][6] -= c2; tile[2][7] += c2;

  tile[3][0] += c3; tile[3][1] -= c3; tile[3][2] -= c3; tile[3][3] += c3;
  tile[3][4] += c3; tile[3][5] -= c3; tile[3][6] -= c3; tile[3][7] += c3;

  tile[4][0] -= c3; tile[4][1] += c3; tile[4][2] += c3; tile[4][3] -= c3;
  tile[4][4] -= c3; tile[4][5] += c3; tile[4][6] += c3; tile[4][7] -= c3;

  tile[5][0] -= c2; tile[5][1] += c2; tile[5][2] += c2; tile[5][3] -= c2;
  tile[5][4] -= c2; tile[5][5] += c2; tile[5][6] += c2; tile[5][7] -= c2;

  tile[6][0] -= c1; tile[6][1] += c1; tile[6][2] += c1; tile[6][3] -= c1;
  tile[6][4] -= c1; tile[6][5] += c1; tile[6][6] += c1; tile[6][7] -= c1;

  tile[7][0] -= c0; tile[7][1] += c0; tile[7][2] += c0; tile[7][3] -= c0;
  tile[7][4] -= c0; tile[7][5] += c0; tile[7][6] += c0; tile[7][7] -= c0;
}

void splat_15(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c2; tile[0][1] -= c0; tile[0][2] += c3; tile[0][3] += c1;
  tile[0][4] -= c1; tile[0][5] -= c3; tile[0][6] += c0; tile[0][7] -= c2;

  tile[1][0] += c5; tile[1][1] -= c1; tile[1][2] += c6; tile[1][3] += c4;
  tile[1][4] -= c4; tile[1][5] -= c6; tile[1][6] += c1; tile[1][7] -= c5;

  tile[2][0] += c7; tile[2][1] -= c2; tile[2][2] += c8; tile[2][3] += c5;
  tile[2][4] -= c5; tile[2][5] -= c8; tile[2][6] += c2; tile[2][7] -= c7;

  tile[3][0] += c8; tile[3][1] -= c3; tile[3][2] += c9; tile[3][3] += c6;
  tile[3][4] -= c6; tile[3][5] -= c9; tile[3][6] += c3; tile[3][7] -= c8;

  tile[4][0] -= c8; tile[4][1] += c3; tile[4][2] -= c9; tile[4][3] -= c6;
  tile[4][4] += c6; tile[4][5] += c9; tile[4][6] -= c3; tile[4][7] += c8;

  tile[5][0] -= c7; tile[5][1] += c2; tile[5][2] -= c8; tile[5][3] -= c5;
  tile[5][4] += c5; tile[5][5] += c8; tile[5][6] -= c2; tile[5][7] += c7;

  tile[6][0] -= c5; tile[6][1] += c1; tile[6][2] -= c6; tile[6][3] -= c4;
  tile[6][4] += c4; tile[6][5] += c6; tile[6][6] -= c1; tile[6][7] += c5;

  tile[7][0] -= c2; tile[7][1] += c0; tile[7][2] -= c3; tile[7][3] -= c1;
  tile[7][4] += c1; tile[7][5] += c3; tile[7][6] -= c0; tile[7][7] += c2;
}

void splat_16(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c1; tile[0][1] -= c0; tile[0][2] += c0; tile[0][3] -= c1;
  tile[0][4] -= c1; tile[0][5] += c0; tile[0][6] -= c0; tile[0][7] += c1;

  tile[1][0] += c3; tile[1][1] -= c2; tile[1][2] += c2; tile[1][3] -= c3;
  tile[1][4] -= c3; tile[1][5] += c2; tile[1][6] -= c2; tile[1][7] += c3;

  tile[2][0] += c5; tile[2][1] -= c4; tile[2][2] += c4; tile[2][3] -= c5;
  tile[2][4] -= c5; tile[2][5] += c4; tile[2][6] -= c4; tile[2][7] += c5;

  tile[3][0] += c7; tile[3][1] -= c6; tile[3][2] += c6; tile[3][3] -= c7;
  tile[3][4] -= c7; tile[3][5] += c6; tile[3][6] -= c6; tile[3][7] += c7;

  tile[4][0] -= c7; tile[4][1] += c6; tile[4][2] -= c6; tile[4][3] += c7;
  tile[4][4] += c7; tile[4][5] -= c6; tile[4][6] += c6; tile[4][7] -= c7;

  tile[5][0] -= c5; tile[5][1] += c4; tile[5][2] -= c4; tile[5][3] += c5;
  tile[5][4] += c5; tile[5][5] -= c4; tile[5][6] += c4; tile[5][7] -= c5;

  tile[6][0] -= c3; tile[6][1] += c2; tile[6][2] -= c2; tile[6][3] += c3;
  tile[6][4] += c3; tile[6][5] -= c2; tile[6][6] += c2; tile[6][7] -= c3;

  tile[7][0] -= c1; tile[7][1] += c0; tile[7][2] -= c0; tile[7][3] += c1;
  tile[7][4] += c1; tile[7][5] -= c0; tile[7][6] += c0; tile[7][7] -= c1;
}

void splat_17(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c3; tile[0][1] -= c2; tile[0][2] += c1; tile[0][3] -= c0;
  tile[0][4] += c0; tile[0][5] -= c1; tile[0][6] += c2; tile[0][7] -= c3;

  tile[1][0] += c6; tile[1][1] -= c5; tile[1][2] += c4; tile[1][3] -= c1;
  tile[1][4] += c1; tile[1][5] -= c4; tile[1][6] += c5; tile[1][7] -= c6;

  tile[2][0] += c8; tile[2][1] -= c7; tile[2][2] += c5; tile[2][3] -= c2;
  tile[2][4] += c2; tile[2][5] -= c5; tile[2][6] += c7; tile[2][7] -= c8;

  tile[3][0] += c9; tile[3][1] -= c8; tile[3][2] += c6; tile[3][3] -= c3;
  tile[3][4] += c3; tile[3][5] -= c6; tile[3][6] += c8; tile[3][7] -= c9;

  tile[4][0] -= c9; tile[4][1] += c8; tile[4][2] -= c6; tile[4][3] += c3;
  tile[4][4] -= c3; tile[4][5] += c6; tile[4][6] -= c8; tile[4][7] += c9;

  tile[5][0] -= c8; tile[5][1] += c7; tile[5][2] -= c5; tile[5][3] += c2;
  tile[5][4] -= c2; tile[5][5] += c5; tile[5][6] -= c7; tile[5][7] += c8;

  tile[6][0] -= c6; tile[6][1] += c5; tile[6][2] -= c4; tile[6][3] += c1;
  tile[6][4] -= c1; tile[6][5] += c4; tile[6][6] -= c5; tile[6][7] += c6;

  tile[7][0] -= c3; tile[7][1] += c2; tile[7][2] -= c1; tile[7][3] += c0;
  tile[7][4] -= c0; tile[7][5] += c1; tile[7][6] -= c2; tile[7][7] += c3;
}

void splat_20(register int32 coeff)
{
  register int32 c0,c1;

  c0 = coeff * COS_02;
  c1 = coeff * COS_06;

  tile[0][0] += c0; tile[0][1] += c0; tile[0][2] += c0; tile[0][3] += c0;
  tile[0][4] += c0; tile[0][5] += c0; tile[0][6] += c0; tile[0][7] += c0;

  tile[1][0] += c1; tile[1][1] += c1; tile[1][2] += c1; tile[1][3] += c1;
  tile[1][4] += c1; tile[1][5] += c1; tile[1][6] += c1; tile[1][7] += c1;

  tile[2][0] -= c1; tile[2][1] -= c1; tile[2][2] -= c1; tile[2][3] -= c1;
  tile[2][4] -= c1; tile[2][5] -= c1; tile[2][6] -= c1; tile[2][7] -= c1;

  tile[3][0] -= c0; tile[3][1] -= c0; tile[3][2] -= c0; tile[3][3] -= c0;
  tile[3][4] -= c0; tile[3][5] -= c0; tile[3][6] -= c0; tile[3][7] -= c0;

  tile[4][0] -= c0; tile[4][1] -= c0; tile[4][2] -= c0; tile[4][3] -= c0;
  tile[4][4] -= c0; tile[4][5] -= c0; tile[4][6] -= c0; tile[4][7] -= c0;

  tile[5][0] -= c1; tile[5][1] -= c1; tile[5][2] -= c1; tile[5][3] -= c1;
  tile[5][4] -= c1; tile[5][5] -= c1; tile[5][6] -= c1; tile[5][7] -= c1;

  tile[6][0] += c1; tile[6][1] += c1; tile[6][2] += c1; tile[6][3] += c1;
  tile[6][4] += c1; tile[6][5] += c1; tile[6][6] += c1; tile[6][7] += c1;

  tile[7][0] += c0; tile[7][1] += c0; tile[7][2] += c0; tile[7][3] += c0;
  tile[7][4] += c0; tile[7][5] += c0; tile[7][6] += c0; tile[7][7] += c0;
}

void splat_21(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c0; tile[0][1] += c2; tile[0][2] += c4; tile[0][3] += c6;
  tile[0][4] -= c6; tile[0][5] -= c4; tile[0][6] -= c2; tile[0][7] -= c0;

  tile[1][0] += c1; tile[1][1] += c3; tile[1][2] += c5; tile[1][3] += c7;
  tile[1][4] -= c7; tile[1][5] -= c5; tile[1][6] -= c3; tile[1][7] -= c1;

  tile[2][0] -= c1; tile[2][1] -= c3; tile[2][2] -= c5; tile[2][3] -= c7;
  tile[2][4] += c7; tile[2][5] += c5; tile[2][6] += c3; tile[2][7] += c1;

  tile[3][0] -= c0; tile[3][1] -= c2; tile[3][2] -= c4; tile[3][3] -= c6;
  tile[3][4] += c6; tile[3][5] += c4; tile[3][6] += c2; tile[3][7] += c0;

  tile[4][0] -= c0; tile[4][1] -= c2; tile[4][2] -= c4; tile[4][3] -= c6;
  tile[4][4] += c6; tile[4][5] += c4; tile[4][6] += c2; tile[4][7] += c0;

  tile[5][0] -= c1; tile[5][1] -= c3; tile[5][2] -= c5; tile[5][3] -= c7;
  tile[5][4] += c7; tile[5][5] += c5; tile[5][6] += c3; tile[5][7] += c1;

  tile[6][0] += c1; tile[6][1] += c3; tile[6][2] += c5; tile[6][3] += c7;
  tile[6][4] -= c7; tile[6][5] -= c5; tile[6][6] -= c3; tile[6][7] -= c1;

  tile[7][0] += c0; tile[7][1] += c2; tile[7][2] += c4; tile[7][3] += c6;
  tile[7][4] -= c6; tile[7][5] -= c4; tile[7][6] -= c2; tile[7][7] -= c0;
}

void splat_22(register int32 coeff)
{
  register int32 c0,c1,c2;

  c0 = coeff * COS_22;
  c1 = coeff * COS_26;
  c2 = coeff * COS_66;

  tile[0][0] += c0; tile[0][1] += c1; tile[0][2] -= c1; tile[0][3] -= c0;
  tile[0][4] -= c0; tile[0][5] -= c1; tile[0][6] += c1; tile[0][7] += c0;

  tile[1][0] += c1; tile[1][1] += c2; tile[1][2] -= c2; tile[1][3] -= c1;
  tile[1][4] -= c1; tile[1][5] -= c2; tile[1][6] += c2; tile[1][7] += c1;

  tile[2][0] -= c1; tile[2][1] -= c2; tile[2][2] += c2; tile[2][3] += c1;
  tile[2][4] += c1; tile[2][5] += c2; tile[2][6] -= c2; tile[2][7] -= c1;

  tile[3][0] -= c0; tile[3][1] -= c1; tile[3][2] += c1; tile[3][3] += c0;
  tile[3][4] += c0; tile[3][5] += c1; tile[3][6] -= c1; tile[3][7] -= c0;

  tile[4][0] -= c0; tile[4][1] -= c1; tile[4][2] += c1; tile[4][3] += c0;
  tile[4][4] += c0; tile[4][5] += c1; tile[4][6] -= c1; tile[4][7] -= c0;

  tile[5][0] -= c1; tile[5][1] -= c2; tile[5][2] += c2; tile[5][3] += c1;
  tile[5][4] += c1; tile[5][5] += c2; tile[5][6] -= c2; tile[5][7] -= c1;

  tile[6][0] += c1; tile[6][1] += c2; tile[6][2] -= c2; tile[6][3] -= c1;
  tile[6][4] -= c1; tile[6][5] -= c2; tile[6][6] += c2; tile[6][7] += c1;

  tile[7][0] += c0; tile[7][1] += c1; tile[7][2] -= c1; tile[7][3] -= c0;
  tile[7][4] -= c0; tile[7][5] -= c1; tile[7][6] += c1; tile[7][7] += c0;
}

void splat_23(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c2; tile[0][1] -= c6; tile[0][2] -= c0; tile[0][3] -= c4;
  tile[0][4] += c4; tile[0][5] += c0; tile[0][6] += c6; tile[0][7] -= c2;

  tile[1][0] += c3; tile[1][1] -= c7; tile[1][2] -= c1; tile[1][3] -= c5;
  tile[1][4] += c5; tile[1][5] += c1; tile[1][6] += c7; tile[1][7] -= c3;

  tile[2][0] -= c3; tile[2][1] += c7; tile[2][2] += c1; tile[2][3] += c5;
  tile[2][4] -= c5; tile[2][5] -= c1; tile[2][6] -= c7; tile[2][7] += c3;

  tile[3][0] -= c2; tile[3][1] += c6; tile[3][2] += c0; tile[3][3] += c4;
  tile[3][4] -= c4; tile[3][5] -= c0; tile[3][6] -= c6; tile[3][7] += c2;

  tile[4][0] -= c2; tile[4][1] += c6; tile[4][2] += c0; tile[4][3] += c4;
  tile[4][4] -= c4; tile[4][5] -= c0; tile[4][6] -= c6; tile[4][7] += c2;

  tile[5][0] -= c3; tile[5][1] += c7; tile[5][2] += c1; tile[5][3] += c5;
  tile[5][4] -= c5; tile[5][5] -= c1; tile[5][6] -= c7; tile[5][7] += c3;

  tile[6][0] += c3; tile[6][1] -= c7; tile[6][2] -= c1; tile[6][3] -= c5;
  tile[6][4] += c5; tile[6][5] += c1; tile[6][6] += c7; tile[6][7] -= c3;

  tile[7][0] += c2; tile[7][1] -= c6; tile[7][2] -= c0; tile[7][3] -= c4;
  tile[7][4] += c4; tile[7][5] += c0; tile[7][6] += c6; tile[7][7] -= c2;
}

void splat_24(register int32 coeff)
{
  register int32 c0,c1;

  c0 = coeff * COS_24;
  c1 = coeff * COS_64;

  tile[0][0] += c0; tile[0][1] -= c0; tile[0][2] -= c0; tile[0][3] += c0;
  tile[0][4] += c0; tile[0][5] -= c0; tile[0][6] -= c0; tile[0][7] += c0;

  tile[1][0] += c1; tile[1][1] -= c1; tile[1][2] -= c1; tile[1][3] += c1;
  tile[1][4] += c1; tile[1][5] -= c1; tile[1][6] -= c1; tile[1][7] += c1;

  tile[2][0] -= c1; tile[2][1] += c1; tile[2][2] += c1; tile[2][3] -= c1;
  tile[2][4] -= c1; tile[2][5] += c1; tile[2][6] += c1; tile[2][7] -= c1;

  tile[3][0] -= c0; tile[3][1] += c0; tile[3][2] += c0; tile[3][3] -= c0;
  tile[3][4] -= c0; tile[3][5] += c0; tile[3][6] += c0; tile[3][7] -= c0;

  tile[4][0] -= c0; tile[4][1] += c0; tile[4][2] += c0; tile[4][3] -= c0;
  tile[4][4] -= c0; tile[4][5] += c0; tile[4][6] += c0; tile[4][7] -= c0;

  tile[5][0] -= c1; tile[5][1] += c1; tile[5][2] += c1; tile[5][3] -= c1;
  tile[5][4] -= c1; tile[5][5] += c1; tile[5][6] += c1; tile[5][7] -= c1;

  tile[6][0] += c1; tile[6][1] -= c1; tile[6][2] -= c1; tile[6][3] += c1;
  tile[6][4] += c1; tile[6][5] -= c1; tile[6][6] -= c1; tile[6][7] += c1;

  tile[7][0] += c0; tile[7][1] -= c0; tile[7][2] -= c0; tile[7][3] += c0;
  tile[7][4] += c0; tile[7][5] -= c0; tile[7][6] -= c0; tile[7][7] += c0;
}

void splat_25(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c4; tile[0][1] -= c0; tile[0][2] += c6; tile[0][3] += c2;
  tile[0][4] -= c2; tile[0][5] -= c6; tile[0][6] += c0; tile[0][7] -= c4;

  tile[1][0] += c5; tile[1][1] -= c1; tile[1][2] += c7; tile[1][3] += c3;
  tile[1][4] -= c3; tile[1][5] -= c7; tile[1][6] += c1; tile[1][7] -= c5;

  tile[2][0] -= c5; tile[2][1] += c1; tile[2][2] -= c7; tile[2][3] -= c3;
  tile[2][4] += c3; tile[2][5] += c7; tile[2][6] -= c1; tile[2][7] += c5;

  tile[3][0] -= c4; tile[3][1] += c0; tile[3][2] -= c6; tile[3][3] -= c2;
  tile[3][4] += c2; tile[3][5] += c6; tile[3][6] -= c0; tile[3][7] += c4;

  tile[4][0] -= c4; tile[4][1] += c0; tile[4][2] -= c6; tile[4][3] -= c2;
  tile[4][4] += c2; tile[4][5] += c6; tile[4][6] -= c0; tile[4][7] += c4;

  tile[5][0] -= c5; tile[5][1] += c1; tile[5][2] -= c7; tile[5][3] -= c3;
  tile[5][4] += c3; tile[5][5] += c7; tile[5][6] -= c1; tile[5][7] += c5;

  tile[6][0] += c5; tile[6][1] -= c1; tile[6][2] += c7; tile[6][3] += c3;
  tile[6][4] -= c3; tile[6][5] -= c7; tile[6][6] += c1; tile[6][7] -= c5;

  tile[7][0] += c4; tile[7][1] -= c0; tile[7][2] += c6; tile[7][3] += c2;
  tile[7][4] -= c2; tile[7][5] -= c6; tile[7][6] += c0; tile[7][7] -= c4;
}

void splat_26(register int32 coeff)
{
  register int32 c0,c1,c2;

  c0 = coeff * COS_22;
  c1 = coeff * COS_26;
  c2 = coeff * COS_66;

  tile[0][0] += c1; tile[0][1] -= c0; tile[0][2] += c0; tile[0][3] -= c1;
  tile[0][4] -= c1; tile[0][5] += c0; tile[0][6] -= c0; tile[0][7] += c1;

  tile[1][0] += c2; tile[1][1] -= c1; tile[1][2] += c1; tile[1][3] -= c2;
  tile[1][4] -= c2; tile[1][5] += c1; tile[1][6] -= c1; tile[1][7] += c2;

  tile[2][0] -= c2; tile[2][1] += c1; tile[2][2] -= c1; tile[2][3] += c2;
  tile[2][4] += c2; tile[2][5] -= c1; tile[2][6] += c1; tile[2][7] -= c2;

  tile[3][0] -= c1; tile[3][1] += c0; tile[3][2] -= c0; tile[3][3] += c1;
  tile[3][4] += c1; tile[3][5] -= c0; tile[3][6] += c0; tile[3][7] -= c1;

  tile[4][0] -= c1; tile[4][1] += c0; tile[4][2] -= c0; tile[4][3] += c1;
  tile[4][4] += c1; tile[4][5] -= c0; tile[4][6] += c0; tile[4][7] -= c1;

  tile[5][0] -= c2; tile[5][1] += c1; tile[5][2] -= c1; tile[5][3] += c2;
  tile[5][4] += c2; tile[5][5] -= c1; tile[5][6] += c1; tile[5][7] -= c2;

  tile[6][0] += c2; tile[6][1] -= c1; tile[6][2] += c1; tile[6][3] -= c2;
  tile[6][4] -= c2; tile[6][5] += c1; tile[6][6] -= c1; tile[6][7] += c2;

  tile[7][0] += c1; tile[7][1] -= c0; tile[7][2] += c0; tile[7][3] -= c1;
  tile[7][4] -= c1; tile[7][5] += c0; tile[7][6] -= c0; tile[7][7] += c1;
}

void splat_27(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c6; tile[0][1] -= c4; tile[0][2] += c2; tile[0][3] -= c0;
  tile[0][4] += c0; tile[0][5] -= c2; tile[0][6] += c4; tile[0][7] -= c6;

  tile[1][0] += c7; tile[1][1] -= c5; tile[1][2] += c3; tile[1][3] -= c1;
  tile[1][4] += c1; tile[1][5] -= c3; tile[1][6] += c5; tile[1][7] -= c7;

  tile[2][0] -= c7; tile[2][1] += c5; tile[2][2] -= c3; tile[2][3] += c1;
  tile[2][4] -= c1; tile[2][5] += c3; tile[2][6] -= c5; tile[2][7] += c7;

  tile[3][0] -= c6; tile[3][1] += c4; tile[3][2] -= c2; tile[3][3] += c0;
  tile[3][4] -= c0; tile[3][5] += c2; tile[3][6] -= c4; tile[3][7] += c6;

  tile[4][0] -= c6; tile[4][1] += c4; tile[4][2] -= c2; tile[4][3] += c0;
  tile[4][4] -= c0; tile[4][5] += c2; tile[4][6] -= c4; tile[4][7] += c6;

  tile[5][0] -= c7; tile[5][1] += c5; tile[5][2] -= c3; tile[5][3] += c1;
  tile[5][4] -= c1; tile[5][5] += c3; tile[5][6] -= c5; tile[5][7] += c7;

  tile[6][0] += c7; tile[6][1] -= c5; tile[6][2] += c3; tile[6][3] -= c1;
  tile[6][4] += c1; tile[6][5] -= c3; tile[6][6] += c5; tile[6][7] -= c7;

  tile[7][0] += c6; tile[7][1] -= c4; tile[7][2] += c2; tile[7][3] -= c0;
  tile[7][4] += c0; tile[7][5] -= c2; tile[7][6] += c4; tile[7][7] -= c6;
}

void splat_30(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_01;
  c1 = coeff * COS_03;
  c2 = coeff * COS_05;
  c3 = coeff * COS_07;

  tile[0][0] += c1; tile[0][1] += c1; tile[0][2] += c1; tile[0][3] += c1;
  tile[0][4] += c1; tile[0][5] += c1; tile[0][6] += c1; tile[0][7] += c1;

  tile[1][0] -= c3; tile[1][1] -= c3; tile[1][2] -= c3; tile[1][3] -= c3;
  tile[1][4] -= c3; tile[1][5] -= c3; tile[1][6] -= c3; tile[1][7] -= c3;

  tile[2][0] -= c0; tile[2][1] -= c0; tile[2][2] -= c0; tile[2][3] -= c0;
  tile[2][4] -= c0; tile[2][5] -= c0; tile[2][6] -= c0; tile[2][7] -= c0;

  tile[3][0] -= c2; tile[3][1] -= c2; tile[3][2] -= c2; tile[3][3] -= c2;
  tile[3][4] -= c2; tile[3][5] -= c2; tile[3][6] -= c2; tile[3][7] -= c2;

  tile[4][0] += c2; tile[4][1] += c2; tile[4][2] += c2; tile[4][3] += c2;
  tile[4][4] += c2; tile[4][5] += c2; tile[4][6] += c2; tile[4][7] += c2;

  tile[5][0] += c0; tile[5][1] += c0; tile[5][2] += c0; tile[5][3] += c0;
  tile[5][4] += c0; tile[5][5] += c0; tile[5][6] += c0; tile[5][7] += c0;

  tile[6][0] += c3; tile[6][1] += c3; tile[6][2] += c3; tile[6][3] += c3;
  tile[6][4] += c3; tile[6][5] += c3; tile[6][6] += c3; tile[6][7] += c3;

  tile[7][0] -= c1; tile[7][1] -= c1; tile[7][2] -= c1; tile[7][3] -= c1;
  tile[7][4] -= c1; tile[7][5] -= c1; tile[7][6] -= c1; tile[7][7] -= c1;
}

void splat_31(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c1; tile[0][1] += c4; tile[0][2] += c5; tile[0][3] += c6;
  tile[0][4] -= c6; tile[0][5] -= c5; tile[0][6] -= c4; tile[0][7] -= c1;

  tile[1][0] -= c3; tile[1][1] -= c6; tile[1][2] -= c8; tile[1][3] -= c9;
  tile[1][4] += c9; tile[1][5] += c8; tile[1][6] += c6; tile[1][7] += c3;

  tile[2][0] -= c0; tile[2][1] -= c1; tile[2][2] -= c2; tile[2][3] -= c3;
  tile[2][4] += c3; tile[2][5] += c2; tile[2][6] += c1; tile[2][7] += c0;

  tile[3][0] -= c2; tile[3][1] -= c5; tile[3][2] -= c7; tile[3][3] -= c8;
  tile[3][4] += c8; tile[3][5] += c7; tile[3][6] += c5; tile[3][7] += c2;

  tile[4][0] += c2; tile[4][1] += c5; tile[4][2] += c7; tile[4][3] += c8;
  tile[4][4] -= c8; tile[4][5] -= c7; tile[4][6] -= c5; tile[4][7] -= c2;

  tile[5][0] += c0; tile[5][1] += c1; tile[5][2] += c2; tile[5][3] += c3;
  tile[5][4] -= c3; tile[5][5] -= c2; tile[5][6] -= c1; tile[5][7] -= c0;

  tile[6][0] += c3; tile[6][1] += c6; tile[6][2] += c8; tile[6][3] += c9;
  tile[6][4] -= c9; tile[6][5] -= c8; tile[6][6] -= c6; tile[6][7] -= c3;

  tile[7][0] -= c1; tile[7][1] -= c4; tile[7][2] -= c5; tile[7][3] -= c6;
  tile[7][4] += c6; tile[7][5] += c5; tile[7][6] += c4; tile[7][7] += c1;
}

void splat_32(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c2; tile[0][1] += c3; tile[0][2] -= c3; tile[0][3] -= c2;
  tile[0][4] -= c2; tile[0][5] -= c3; tile[0][6] += c3; tile[0][7] += c2;

  tile[1][0] -= c6; tile[1][1] -= c7; tile[1][2] += c7; tile[1][3] += c6;
  tile[1][4] += c6; tile[1][5] += c7; tile[1][6] -= c7; tile[1][7] -= c6;

  tile[2][0] -= c0; tile[2][1] -= c1; tile[2][2] += c1; tile[2][3] += c0;
  tile[2][4] += c0; tile[2][5] += c1; tile[2][6] -= c1; tile[2][7] -= c0;

  tile[3][0] -= c4; tile[3][1] -= c5; tile[3][2] += c5; tile[3][3] += c4;
  tile[3][4] += c4; tile[3][5] += c5; tile[3][6] -= c5; tile[3][7] -= c4;

  tile[4][0] += c4; tile[4][1] += c5; tile[4][2] -= c5; tile[4][3] -= c4;
  tile[4][4] -= c4; tile[4][5] -= c5; tile[4][6] += c5; tile[4][7] += c4;

  tile[5][0] += c0; tile[5][1] += c1; tile[5][2] -= c1; tile[5][3] -= c0;
  tile[5][4] -= c0; tile[5][5] -= c1; tile[5][6] += c1; tile[5][7] += c0;

  tile[6][0] += c6; tile[6][1] += c7; tile[6][2] -= c7; tile[6][3] -= c6;
  tile[6][4] -= c6; tile[6][5] -= c7; tile[6][6] += c7; tile[6][7] += c6;

  tile[7][0] -= c2; tile[7][1] -= c3; tile[7][2] += c3; tile[7][3] += c2;
  tile[7][4] += c2; tile[7][5] += c3; tile[7][6] -= c3; tile[7][7] -= c2;
}

void splat_33(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c4; tile[0][1] -= c6; tile[0][2] -= c1; tile[0][3] -= c5;
  tile[0][4] += c5; tile[0][5] += c1; tile[0][6] += c6; tile[0][7] -= c4;

  tile[1][0] -= c6; tile[1][1] += c9; tile[1][2] += c3; tile[1][3] += c8;
  tile[1][4] -= c8; tile[1][5] -= c3; tile[1][6] -= c9; tile[1][7] += c6;

  tile[2][0] -= c1; tile[2][1] += c3; tile[2][2] += c0; tile[2][3] += c2;
  tile[2][4] -= c2; tile[2][5] -= c0; tile[2][6] -= c3; tile[2][7] += c1;

  tile[3][0] -= c5; tile[3][1] += c8; tile[3][2] += c2; tile[3][3] += c7;
  tile[3][4] -= c7; tile[3][5] -= c2; tile[3][6] -= c8; tile[3][7] += c5;

  tile[4][0] += c5; tile[4][1] -= c8; tile[4][2] -= c2; tile[4][3] -= c7;
  tile[4][4] += c7; tile[4][5] += c2; tile[4][6] += c8; tile[4][7] -= c5;

  tile[5][0] += c1; tile[5][1] -= c3; tile[5][2] -= c0; tile[5][3] -= c2;
  tile[5][4] += c2; tile[5][5] += c0; tile[5][6] += c3; tile[5][7] -= c1;

  tile[6][0] += c6; tile[6][1] -= c9; tile[6][2] -= c3; tile[6][3] -= c8;
  tile[6][4] += c8; tile[6][5] += c3; tile[6][6] += c9; tile[6][7] -= c6;

  tile[7][0] -= c4; tile[7][1] += c6; tile[7][2] += c1; tile[7][3] += c5;
  tile[7][4] -= c5; tile[7][5] -= c1; tile[7][6] -= c6; tile[7][7] += c4;
}

void splat_34(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_14;
  c1 = coeff * COS_34;
  c2 = coeff * COS_54;
  c3 = coeff * COS_74;

  tile[0][0] += c1; tile[0][1] -= c1; tile[0][2] -= c1; tile[0][3] += c1;
  tile[0][4] += c1; tile[0][5] -= c1; tile[0][6] -= c1; tile[0][7] += c1;

  tile[1][0] -= c3; tile[1][1] += c3; tile[1][2] += c3; tile[1][3] -= c3;
  tile[1][4] -= c3; tile[1][5] += c3; tile[1][6] += c3; tile[1][7] -= c3;

  tile[2][0] -= c0; tile[2][1] += c0; tile[2][2] += c0; tile[2][3] -= c0;
  tile[2][4] -= c0; tile[2][5] += c0; tile[2][6] += c0; tile[2][7] -= c0;

  tile[3][0] -= c2; tile[3][1] += c2; tile[3][2] += c2; tile[3][3] -= c2;
  tile[3][4] -= c2; tile[3][5] += c2; tile[3][6] += c2; tile[3][7] -= c2;

  tile[4][0] += c2; tile[4][1] -= c2; tile[4][2] -= c2; tile[4][3] += c2;
  tile[4][4] += c2; tile[4][5] -= c2; tile[4][6] -= c2; tile[4][7] += c2;

  tile[5][0] += c0; tile[5][1] -= c0; tile[5][2] -= c0; tile[5][3] += c0;
  tile[5][4] += c0; tile[5][5] -= c0; tile[5][6] -= c0; tile[5][7] += c0;

  tile[6][0] += c3; tile[6][1] -= c3; tile[6][2] -= c3; tile[6][3] += c3;
  tile[6][4] += c3; tile[6][5] -= c3; tile[6][6] -= c3; tile[6][7] += c3;

  tile[7][0] -= c1; tile[7][1] += c1; tile[7][2] += c1; tile[7][3] -= c1;
  tile[7][4] -= c1; tile[7][5] += c1; tile[7][6] += c1; tile[7][7] -= c1;
}

void splat_35(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c5; tile[0][1] -= c1; tile[0][2] += c6; tile[0][3] += c4;
  tile[0][4] -= c4; tile[0][5] -= c6; tile[0][6] += c1; tile[0][7] -= c5;

  tile[1][0] -= c8; tile[1][1] += c3; tile[1][2] -= c9; tile[1][3] -= c6;
  tile[1][4] += c6; tile[1][5] += c9; tile[1][6] -= c3; tile[1][7] += c8;

  tile[2][0] -= c2; tile[2][1] += c0; tile[2][2] -= c3; tile[2][3] -= c1;
  tile[2][4] += c1; tile[2][5] += c3; tile[2][6] -= c0; tile[2][7] += c2;

  tile[3][0] -= c7; tile[3][1] += c2; tile[3][2] -= c8; tile[3][3] -= c5;
  tile[3][4] += c5; tile[3][5] += c8; tile[3][6] -= c2; tile[3][7] += c7;

  tile[4][0] += c7; tile[4][1] -= c2; tile[4][2] += c8; tile[4][3] += c5;
  tile[4][4] -= c5; tile[4][5] -= c8; tile[4][6] += c2; tile[4][7] -= c7;

  tile[5][0] += c2; tile[5][1] -= c0; tile[5][2] += c3; tile[5][3] += c1;
  tile[5][4] -= c1; tile[5][5] -= c3; tile[5][6] += c0; tile[5][7] -= c2;

  tile[6][0] += c8; tile[6][1] -= c3; tile[6][2] += c9; tile[6][3] += c6;
  tile[6][4] -= c6; tile[6][5] -= c9; tile[6][6] += c3; tile[6][7] -= c8;

  tile[7][0] -= c5; tile[7][1] += c1; tile[7][2] -= c6; tile[7][3] -= c4;
  tile[7][4] += c4; tile[7][5] += c6; tile[7][6] -= c1; tile[7][7] += c5;
}

void splat_36(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c3; tile[0][1] -= c2; tile[0][2] += c2; tile[0][3] -= c3;
  tile[0][4] -= c3; tile[0][5] += c2; tile[0][6] -= c2; tile[0][7] += c3;

  tile[1][0] -= c7; tile[1][1] += c6; tile[1][2] -= c6; tile[1][3] += c7;
  tile[1][4] += c7; tile[1][5] -= c6; tile[1][6] += c6; tile[1][7] -= c7;

  tile[2][0] -= c1; tile[2][1] += c0; tile[2][2] -= c0; tile[2][3] += c1;
  tile[2][4] += c1; tile[2][5] -= c0; tile[2][6] += c0; tile[2][7] -= c1;

  tile[3][0] -= c5; tile[3][1] += c4; tile[3][2] -= c4; tile[3][3] += c5;
  tile[3][4] += c5; tile[3][5] -= c4; tile[3][6] += c4; tile[3][7] -= c5;

  tile[4][0] += c5; tile[4][1] -= c4; tile[4][2] += c4; tile[4][3] -= c5;
  tile[4][4] -= c5; tile[4][5] += c4; tile[4][6] -= c4; tile[4][7] += c5;

  tile[5][0] += c1; tile[5][1] -= c0; tile[5][2] += c0; tile[5][3] -= c1;
  tile[5][4] -= c1; tile[5][5] += c0; tile[5][6] -= c0; tile[5][7] += c1;

  tile[6][0] += c7; tile[6][1] -= c6; tile[6][2] += c6; tile[6][3] -= c7;
  tile[6][4] -= c7; tile[6][5] += c6; tile[6][6] -= c6; tile[6][7] += c7;

  tile[7][0] -= c3; tile[7][1] += c2; tile[7][2] -= c2; tile[7][3] += c3;
  tile[7][4] += c3; tile[7][5] -= c2; tile[7][6] += c2; tile[7][7] -= c3;
}

void splat_37(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c6; tile[0][1] -= c5; tile[0][2] += c4; tile[0][3] -= c1;
  tile[0][4] += c1; tile[0][5] -= c4; tile[0][6] += c5; tile[0][7] -= c6;

  tile[1][0] -= c9; tile[1][1] += c8; tile[1][2] -= c6; tile[1][3] += c3;
  tile[1][4] -= c3; tile[1][5] += c6; tile[1][6] -= c8; tile[1][7] += c9;

  tile[2][0] -= c3; tile[2][1] += c2; tile[2][2] -= c1; tile[2][3] += c0;
  tile[2][4] -= c0; tile[2][5] += c1; tile[2][6] -= c2; tile[2][7] += c3;

  tile[3][0] -= c8; tile[3][1] += c7; tile[3][2] -= c5; tile[3][3] += c2;
  tile[3][4] -= c2; tile[3][5] += c5; tile[3][6] -= c7; tile[3][7] += c8;

  tile[4][0] += c8; tile[4][1] -= c7; tile[4][2] += c5; tile[4][3] -= c2;
  tile[4][4] += c2; tile[4][5] -= c5; tile[4][6] += c7; tile[4][7] -= c8;

  tile[5][0] += c3; tile[5][1] -= c2; tile[5][2] += c1; tile[5][3] -= c0;
  tile[5][4] += c0; tile[5][5] -= c1; tile[5][6] += c2; tile[5][7] -= c3;

  tile[6][0] += c9; tile[6][1] -= c8; tile[6][2] += c6; tile[6][3] -= c3;
  tile[6][4] += c3; tile[6][5] -= c6; tile[6][6] += c8; tile[6][7] -= c9;

  tile[7][0] -= c6; tile[7][1] += c5; tile[7][2] -= c4; tile[7][3] += c1;
  tile[7][4] -= c1; tile[7][5] += c4; tile[7][6] -= c5; tile[7][7] += c6;
}

void splat_40(register int32 coeff)
{
  register int32 c0;

  c0 = coeff * COS_04;

  tile[0][0] += c0; tile[0][1] += c0; tile[0][2] += c0; tile[0][3] += c0;
  tile[0][4] += c0; tile[0][5] += c0; tile[0][6] += c0; tile[0][7] += c0;

  tile[1][0] -= c0; tile[1][1] -= c0; tile[1][2] -= c0; tile[1][3] -= c0;
  tile[1][4] -= c0; tile[1][5] -= c0; tile[1][6] -= c0; tile[1][7] -= c0;

  tile[2][0] -= c0; tile[2][1] -= c0; tile[2][2] -= c0; tile[2][3] -= c0;
  tile[2][4] -= c0; tile[2][5] -= c0; tile[2][6] -= c0; tile[2][7] -= c0;

  tile[3][0] += c0; tile[3][1] += c0; tile[3][2] += c0; tile[3][3] += c0;
  tile[3][4] += c0; tile[3][5] += c0; tile[3][6] += c0; tile[3][7] += c0;

  tile[4][0] += c0; tile[4][1] += c0; tile[4][2] += c0; tile[4][3] += c0;
  tile[4][4] += c0; tile[4][5] += c0; tile[4][6] += c0; tile[4][7] += c0;

  tile[5][0] -= c0; tile[5][1] -= c0; tile[5][2] -= c0; tile[5][3] -= c0;
  tile[5][4] -= c0; tile[5][5] -= c0; tile[5][6] -= c0; tile[5][7] -= c0;

  tile[6][0] -= c0; tile[6][1] -= c0; tile[6][2] -= c0; tile[6][3] -= c0;
  tile[6][4] -= c0; tile[6][5] -= c0; tile[6][6] -= c0; tile[6][7] -= c0;

  tile[7][0] += c0; tile[7][1] += c0; tile[7][2] += c0; tile[7][3] += c0;
  tile[7][4] += c0; tile[7][5] += c0; tile[7][6] += c0; tile[7][7] += c0;
}

void splat_41(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_14;
  c1 = coeff * COS_34;
  c2 = coeff * COS_54;
  c3 = coeff * COS_74;

  tile[0][0] += c0; tile[0][1] += c1; tile[0][2] += c2; tile[0][3] += c3;
  tile[0][4] -= c3; tile[0][5] -= c2; tile[0][6] -= c1; tile[0][7] -= c0;

  tile[1][0] -= c0; tile[1][1] -= c1; tile[1][2] -= c2; tile[1][3] -= c3;
  tile[1][4] += c3; tile[1][5] += c2; tile[1][6] += c1; tile[1][7] += c0;

  tile[2][0] -= c0; tile[2][1] -= c1; tile[2][2] -= c2; tile[2][3] -= c3;
  tile[2][4] += c3; tile[2][5] += c2; tile[2][6] += c1; tile[2][7] += c0;

  tile[3][0] += c0; tile[3][1] += c1; tile[3][2] += c2; tile[3][3] += c3;
  tile[3][4] -= c3; tile[3][5] -= c2; tile[3][6] -= c1; tile[3][7] -= c0;

  tile[4][0] += c0; tile[4][1] += c1; tile[4][2] += c2; tile[4][3] += c3;
  tile[4][4] -= c3; tile[4][5] -= c2; tile[4][6] -= c1; tile[4][7] -= c0;

  tile[5][0] -= c0; tile[5][1] -= c1; tile[5][2] -= c2; tile[5][3] -= c3;
  tile[5][4] += c3; tile[5][5] += c2; tile[5][6] += c1; tile[5][7] += c0;

  tile[6][0] -= c0; tile[6][1] -= c1; tile[6][2] -= c2; tile[6][3] -= c3;
  tile[6][4] += c3; tile[6][5] += c2; tile[6][6] += c1; tile[6][7] += c0;

  tile[7][0] += c0; tile[7][1] += c1; tile[7][2] += c2; tile[7][3] += c3;
  tile[7][4] -= c3; tile[7][5] -= c2; tile[7][6] -= c1; tile[7][7] -= c0;
}

void splat_42(register int32 coeff)
{
  register int32 c0,c1;

  c0 = coeff * COS_24;
  c1 = coeff * COS_64;

  tile[0][0] += c0; tile[0][1] += c1; tile[0][2] -= c1; tile[0][3] -= c0;
  tile[0][4] -= c0; tile[0][5] -= c1; tile[0][6] += c1; tile[0][7] += c0;

  tile[1][0] -= c0; tile[1][1] -= c1; tile[1][2] += c1; tile[1][3] += c0;
  tile[1][4] += c0; tile[1][5] += c1; tile[1][6] -= c1; tile[1][7] -= c0;

  tile[2][0] -= c0; tile[2][1] -= c1; tile[2][2] += c1; tile[2][3] += c0;
  tile[2][4] += c0; tile[2][5] += c1; tile[2][6] -= c1; tile[2][7] -= c0;

  tile[3][0] += c0; tile[3][1] += c1; tile[3][2] -= c1; tile[3][3] -= c0;
  tile[3][4] -= c0; tile[3][5] -= c1; tile[3][6] += c1; tile[3][7] += c0;

  tile[4][0] += c0; tile[4][1] += c1; tile[4][2] -= c1; tile[4][3] -= c0;
  tile[4][4] -= c0; tile[4][5] -= c1; tile[4][6] += c1; tile[4][7] += c0;

  tile[5][0] -= c0; tile[5][1] -= c1; tile[5][2] += c1; tile[5][3] += c0;
  tile[5][4] += c0; tile[5][5] += c1; tile[5][6] -= c1; tile[5][7] -= c0;

  tile[6][0] -= c0; tile[6][1] -= c1; tile[6][2] += c1; tile[6][3] += c0;
  tile[6][4] += c0; tile[6][5] += c1; tile[6][6] -= c1; tile[6][7] -= c0;

  tile[7][0] += c0; tile[7][1] += c1; tile[7][2] -= c1; tile[7][3] -= c0;
  tile[7][4] -= c0; tile[7][5] -= c1; tile[7][6] += c1; tile[7][7] += c0;
}

void splat_43(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_14;
  c1 = coeff * COS_34;
  c2 = coeff * COS_54;
  c3 = coeff * COS_74;

  tile[0][0] += c1; tile[0][1] -= c3; tile[0][2] -= c0; tile[0][3] -= c2;
  tile[0][4] += c2; tile[0][5] += c0; tile[0][6] += c3; tile[0][7] -= c1;

  tile[1][0] -= c1; tile[1][1] += c3; tile[1][2] += c0; tile[1][3] += c2;
  tile[1][4] -= c2; tile[1][5] -= c0; tile[1][6] -= c3; tile[1][7] += c1;

  tile[2][0] -= c1; tile[2][1] += c3; tile[2][2] += c0; tile[2][3] += c2;
  tile[2][4] -= c2; tile[2][5] -= c0; tile[2][6] -= c3; tile[2][7] += c1;

  tile[3][0] += c1; tile[3][1] -= c3; tile[3][2] -= c0; tile[3][3] -= c2;
  tile[3][4] += c2; tile[3][5] += c0; tile[3][6] += c3; tile[3][7] -= c1;

  tile[4][0] += c1; tile[4][1] -= c3; tile[4][2] -= c0; tile[4][3] -= c2;
  tile[4][4] += c2; tile[4][5] += c0; tile[4][6] += c3; tile[4][7] -= c1;

  tile[5][0] -= c1; tile[5][1] += c3; tile[5][2] += c0; tile[5][3] += c2;
  tile[5][4] -= c2; tile[5][5] -= c0; tile[5][6] -= c3; tile[5][7] += c1;

  tile[6][0] -= c1; tile[6][1] += c3; tile[6][2] += c0; tile[6][3] += c2;
  tile[6][4] -= c2; tile[6][5] -= c0; tile[6][6] -= c3; tile[6][7] += c1;

  tile[7][0] += c1; tile[7][1] -= c3; tile[7][2] -= c0; tile[7][3] -= c2;
  tile[7][4] += c2; tile[7][5] += c0; tile[7][6] += c3; tile[7][7] -= c1;
}

void splat_44(register int32 coeff)
{
  register int32 c0;

  c0 = coeff * COS_44;

  tile[0][0] += c0; tile[0][1] -= c0; tile[0][2] -= c0; tile[0][3] += c0;
  tile[0][4] += c0; tile[0][5] -= c0; tile[0][6] -= c0; tile[0][7] += c0;

  tile[1][0] -= c0; tile[1][1] += c0; tile[1][2] += c0; tile[1][3] -= c0;
  tile[1][4] -= c0; tile[1][5] += c0; tile[1][6] += c0; tile[1][7] -= c0;

  tile[2][0] -= c0; tile[2][1] += c0; tile[2][2] += c0; tile[2][3] -= c0;
  tile[2][4] -= c0; tile[2][5] += c0; tile[2][6] += c0; tile[2][7] -= c0;

  tile[3][0] += c0; tile[3][1] -= c0; tile[3][2] -= c0; tile[3][3] += c0;
  tile[3][4] += c0; tile[3][5] -= c0; tile[3][6] -= c0; tile[3][7] += c0;

  tile[4][0] += c0; tile[4][1] -= c0; tile[4][2] -= c0; tile[4][3] += c0;
  tile[4][4] += c0; tile[4][5] -= c0; tile[4][6] -= c0; tile[4][7] += c0;

  tile[5][0] -= c0; tile[5][1] += c0; tile[5][2] += c0; tile[5][3] -= c0;
  tile[5][4] -= c0; tile[5][5] += c0; tile[5][6] += c0; tile[5][7] -= c0;

  tile[6][0] -= c0; tile[6][1] += c0; tile[6][2] += c0; tile[6][3] -= c0;
  tile[6][4] -= c0; tile[6][5] += c0; tile[6][6] += c0; tile[6][7] -= c0;

  tile[7][0] += c0; tile[7][1] -= c0; tile[7][2] -= c0; tile[7][3] += c0;
  tile[7][4] += c0; tile[7][5] -= c0; tile[7][6] -= c0; tile[7][7] += c0;
}

void splat_45(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_14;
  c1 = coeff * COS_34;
  c2 = coeff * COS_54;
  c3 = coeff * COS_74;

  tile[0][0] += c2; tile[0][1] -= c0; tile[0][2] += c3; tile[0][3] += c1;
  tile[0][4] -= c1; tile[0][5] -= c3; tile[0][6] += c0; tile[0][7] -= c2;

  tile[1][0] -= c2; tile[1][1] += c0; tile[1][2] -= c3; tile[1][3] -= c1;
  tile[1][4] += c1; tile[1][5] += c3; tile[1][6] -= c0; tile[1][7] += c2;

  tile[2][0] -= c2; tile[2][1] += c0; tile[2][2] -= c3; tile[2][3] -= c1;
  tile[2][4] += c1; tile[2][5] += c3; tile[2][6] -= c0; tile[2][7] += c2;

  tile[3][0] += c2; tile[3][1] -= c0; tile[3][2] += c3; tile[3][3] += c1;
  tile[3][4] -= c1; tile[3][5] -= c3; tile[3][6] += c0; tile[3][7] -= c2;

  tile[4][0] += c2; tile[4][1] -= c0; tile[4][2] += c3; tile[4][3] += c1;
  tile[4][4] -= c1; tile[4][5] -= c3; tile[4][6] += c0; tile[4][7] -= c2;

  tile[5][0] -= c2; tile[5][1] += c0; tile[5][2] -= c3; tile[5][3] -= c1;
  tile[5][4] += c1; tile[5][5] += c3; tile[5][6] -= c0; tile[5][7] += c2;

  tile[6][0] -= c2; tile[6][1] += c0; tile[6][2] -= c3; tile[6][3] -= c1;
  tile[6][4] += c1; tile[6][5] += c3; tile[6][6] -= c0; tile[6][7] += c2;

  tile[7][0] += c2; tile[7][1] -= c0; tile[7][2] += c3; tile[7][3] += c1;
  tile[7][4] -= c1; tile[7][5] -= c3; tile[7][6] += c0; tile[7][7] -= c2;
}

void splat_46(register int32 coeff)
{
  register int32 c0,c1;

  c0 = coeff * COS_24;
  c1 = coeff * COS_64;

  tile[0][0] += c1; tile[0][1] -= c0; tile[0][2] += c0; tile[0][3] -= c1;
  tile[0][4] -= c1; tile[0][5] += c0; tile[0][6] -= c0; tile[0][7] += c1;

  tile[1][0] -= c1; tile[1][1] += c0; tile[1][2] -= c0; tile[1][3] += c1;
  tile[1][4] += c1; tile[1][5] -= c0; tile[1][6] += c0; tile[1][7] -= c1;

  tile[2][0] -= c1; tile[2][1] += c0; tile[2][2] -= c0; tile[2][3] += c1;
  tile[2][4] += c1; tile[2][5] -= c0; tile[2][6] += c0; tile[2][7] -= c1;

  tile[3][0] += c1; tile[3][1] -= c0; tile[3][2] += c0; tile[3][3] -= c1;
  tile[3][4] -= c1; tile[3][5] += c0; tile[3][6] -= c0; tile[3][7] += c1;

  tile[4][0] += c1; tile[4][1] -= c0; tile[4][2] += c0; tile[4][3] -= c1;
  tile[4][4] -= c1; tile[4][5] += c0; tile[4][6] -= c0; tile[4][7] += c1;

  tile[5][0] -= c1; tile[5][1] += c0; tile[5][2] -= c0; tile[5][3] += c1;
  tile[5][4] += c1; tile[5][5] -= c0; tile[5][6] += c0; tile[5][7] -= c1;

  tile[6][0] -= c1; tile[6][1] += c0; tile[6][2] -= c0; tile[6][3] += c1;
  tile[6][4] += c1; tile[6][5] -= c0; tile[6][6] += c0; tile[6][7] -= c1;

  tile[7][0] += c1; tile[7][1] -= c0; tile[7][2] += c0; tile[7][3] -= c1;
  tile[7][4] -= c1; tile[7][5] += c0; tile[7][6] -= c0; tile[7][7] += c1;
}

void splat_47(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_14;
  c1 = coeff * COS_34;
  c2 = coeff * COS_54;
  c3 = coeff * COS_74;

  tile[0][0] += c3; tile[0][1] -= c2; tile[0][2] += c1; tile[0][3] -= c0;
  tile[0][4] += c0; tile[0][5] -= c1; tile[0][6] += c2; tile[0][7] -= c3;

  tile[1][0] -= c3; tile[1][1] += c2; tile[1][2] -= c1; tile[1][3] += c0;
  tile[1][4] -= c0; tile[1][5] += c1; tile[1][6] -= c2; tile[1][7] += c3;

  tile[2][0] -= c3; tile[2][1] += c2; tile[2][2] -= c1; tile[2][3] += c0;
  tile[2][4] -= c0; tile[2][5] += c1; tile[2][6] -= c2; tile[2][7] += c3;

  tile[3][0] += c3; tile[3][1] -= c2; tile[3][2] += c1; tile[3][3] -= c0;
  tile[3][4] += c0; tile[3][5] -= c1; tile[3][6] += c2; tile[3][7] -= c3;

  tile[4][0] += c3; tile[4][1] -= c2; tile[4][2] += c1; tile[4][3] -= c0;
  tile[4][4] += c0; tile[4][5] -= c1; tile[4][6] += c2; tile[4][7] -= c3;

  tile[5][0] -= c3; tile[5][1] += c2; tile[5][2] -= c1; tile[5][3] += c0;
  tile[5][4] -= c0; tile[5][5] += c1; tile[5][6] -= c2; tile[5][7] += c3;

  tile[6][0] -= c3; tile[6][1] += c2; tile[6][2] -= c1; tile[6][3] += c0;
  tile[6][4] -= c0; tile[6][5] += c1; tile[6][6] -= c2; tile[6][7] += c3;

  tile[7][0] += c3; tile[7][1] -= c2; tile[7][2] += c1; tile[7][3] -= c0;
  tile[7][4] += c0; tile[7][5] -= c1; tile[7][6] += c2; tile[7][7] -= c3;
}

void splat_50(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_01;
  c1 = coeff * COS_03;
  c2 = coeff * COS_05;
  c3 = coeff * COS_07;

  tile[0][0] += c2; tile[0][1] += c2; tile[0][2] += c2; tile[0][3] += c2;
  tile[0][4] += c2; tile[0][5] += c2; tile[0][6] += c2; tile[0][7] += c2;

  tile[1][0] -= c0; tile[1][1] -= c0; tile[1][2] -= c0; tile[1][3] -= c0;
  tile[1][4] -= c0; tile[1][5] -= c0; tile[1][6] -= c0; tile[1][7] -= c0;

  tile[2][0] += c3; tile[2][1] += c3; tile[2][2] += c3; tile[2][3] += c3;
  tile[2][4] += c3; tile[2][5] += c3; tile[2][6] += c3; tile[2][7] += c3;

  tile[3][0] += c1; tile[3][1] += c1; tile[3][2] += c1; tile[3][3] += c1;
  tile[3][4] += c1; tile[3][5] += c1; tile[3][6] += c1; tile[3][7] += c1;

  tile[4][0] -= c1; tile[4][1] -= c1; tile[4][2] -= c1; tile[4][3] -= c1;
  tile[4][4] -= c1; tile[4][5] -= c1; tile[4][6] -= c1; tile[4][7] -= c1;

  tile[5][0] -= c3; tile[5][1] -= c3; tile[5][2] -= c3; tile[5][3] -= c3;
  tile[5][4] -= c3; tile[5][5] -= c3; tile[5][6] -= c3; tile[5][7] -= c3;

  tile[6][0] += c0; tile[6][1] += c0; tile[6][2] += c0; tile[6][3] += c0;
  tile[6][4] += c0; tile[6][5] += c0; tile[6][6] += c0; tile[6][7] += c0;

  tile[7][0] -= c2; tile[7][1] -= c2; tile[7][2] -= c2; tile[7][3] -= c2;
  tile[7][4] -= c2; tile[7][5] -= c2; tile[7][6] -= c2; tile[7][7] -= c2;
}

void splat_51(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c2; tile[0][1] += c5; tile[0][2] += c7; tile[0][3] += c8;
  tile[0][4] -= c8; tile[0][5] -= c7; tile[0][6] -= c5; tile[0][7] -= c2;

  tile[1][0] -= c0; tile[1][1] -= c1; tile[1][2] -= c2; tile[1][3] -= c3;
  tile[1][4] += c3; tile[1][5] += c2; tile[1][6] += c1; tile[1][7] += c0;

  tile[2][0] += c3; tile[2][1] += c6; tile[2][2] += c8; tile[2][3] += c9;
  tile[2][4] -= c9; tile[2][5] -= c8; tile[2][6] -= c6; tile[2][7] -= c3;

  tile[3][0] += c1; tile[3][1] += c4; tile[3][2] += c5; tile[3][3] += c6;
  tile[3][4] -= c6; tile[3][5] -= c5; tile[3][6] -= c4; tile[3][7] -= c1;

  tile[4][0] -= c1; tile[4][1] -= c4; tile[4][2] -= c5; tile[4][3] -= c6;
  tile[4][4] += c6; tile[4][5] += c5; tile[4][6] += c4; tile[4][7] += c1;

  tile[5][0] -= c3; tile[5][1] -= c6; tile[5][2] -= c8; tile[5][3] -= c9;
  tile[5][4] += c9; tile[5][5] += c8; tile[5][6] += c6; tile[5][7] += c3;

  tile[6][0] += c0; tile[6][1] += c1; tile[6][2] += c2; tile[6][3] += c3;
  tile[6][4] -= c3; tile[6][5] -= c2; tile[6][6] -= c1; tile[6][7] -= c0;

  tile[7][0] -= c2; tile[7][1] -= c5; tile[7][2] -= c7; tile[7][3] -= c8;
  tile[7][4] += c8; tile[7][5] += c7; tile[7][6] += c5; tile[7][7] += c2;
}

void splat_52(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c4; tile[0][1] += c5; tile[0][2] -= c5; tile[0][3] -= c4;
  tile[0][4] -= c4; tile[0][5] -= c5; tile[0][6] += c5; tile[0][7] += c4;

  tile[1][0] -= c0; tile[1][1] -= c1; tile[1][2] += c1; tile[1][3] += c0;
  tile[1][4] += c0; tile[1][5] += c1; tile[1][6] -= c1; tile[1][7] -= c0;

  tile[2][0] += c6; tile[2][1] += c7; tile[2][2] -= c7; tile[2][3] -= c6;
  tile[2][4] -= c6; tile[2][5] -= c7; tile[2][6] += c7; tile[2][7] += c6;

  tile[3][0] += c2; tile[3][1] += c3; tile[3][2] -= c3; tile[3][3] -= c2;
  tile[3][4] -= c2; tile[3][5] -= c3; tile[3][6] += c3; tile[3][7] += c2;

  tile[4][0] -= c2; tile[4][1] -= c3; tile[4][2] += c3; tile[4][3] += c2;
  tile[4][4] += c2; tile[4][5] += c3; tile[4][6] -= c3; tile[4][7] -= c2;

  tile[5][0] -= c6; tile[5][1] -= c7; tile[5][2] += c7; tile[5][3] += c6;
  tile[5][4] += c6; tile[5][5] += c7; tile[5][6] -= c7; tile[5][7] -= c6;

  tile[6][0] += c0; tile[6][1] += c1; tile[6][2] -= c1; tile[6][3] -= c0;
  tile[6][4] -= c0; tile[6][5] -= c1; tile[6][6] += c1; tile[6][7] += c0;

  tile[7][0] -= c4; tile[7][1] -= c5; tile[7][2] += c5; tile[7][3] += c4;
  tile[7][4] += c4; tile[7][5] += c5; tile[7][6] -= c5; tile[7][7] -= c4;
}

void splat_53(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c5; tile[0][1] -= c8; tile[0][2] -= c2; tile[0][3] -= c7;
  tile[0][4] += c7; tile[0][5] += c2; tile[0][6] += c8; tile[0][7] -= c5;

  tile[1][0] -= c1; tile[1][1] += c3; tile[1][2] += c0; tile[1][3] += c2;
  tile[1][4] -= c2; tile[1][5] -= c0; tile[1][6] -= c3; tile[1][7] += c1;

  tile[2][0] += c6; tile[2][1] -= c9; tile[2][2] -= c3; tile[2][3] -= c8;
  tile[2][4] += c8; tile[2][5] += c3; tile[2][6] += c9; tile[2][7] -= c6;

  tile[3][0] += c4; tile[3][1] -= c6; tile[3][2] -= c1; tile[3][3] -= c5;
  tile[3][4] += c5; tile[3][5] += c1; tile[3][6] += c6; tile[3][7] -= c4;

  tile[4][0] -= c4; tile[4][1] += c6; tile[4][2] += c1; tile[4][3] += c5;
  tile[4][4] -= c5; tile[4][5] -= c1; tile[4][6] -= c6; tile[4][7] += c4;

  tile[5][0] -= c6; tile[5][1] += c9; tile[5][2] += c3; tile[5][3] += c8;
  tile[5][4] -= c8; tile[5][5] -= c3; tile[5][6] -= c9; tile[5][7] += c6;

  tile[6][0] += c1; tile[6][1] -= c3; tile[6][2] -= c0; tile[6][3] -= c2;
  tile[6][4] += c2; tile[6][5] += c0; tile[6][6] += c3; tile[6][7] -= c1;

  tile[7][0] -= c5; tile[7][1] += c8; tile[7][2] += c2; tile[7][3] += c7;
  tile[7][4] -= c7; tile[7][5] -= c2; tile[7][6] -= c8; tile[7][7] += c5;
}

void splat_54(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_14;
  c1 = coeff * COS_34;
  c2 = coeff * COS_54;
  c3 = coeff * COS_74;

  tile[0][0] += c2; tile[0][1] -= c2; tile[0][2] -= c2; tile[0][3] += c2;
  tile[0][4] += c2; tile[0][5] -= c2; tile[0][6] -= c2; tile[0][7] += c2;

  tile[1][0] -= c0; tile[1][1] += c0; tile[1][2] += c0; tile[1][3] -= c0;
  tile[1][4] -= c0; tile[1][5] += c0; tile[1][6] += c0; tile[1][7] -= c0;

  tile[2][0] += c3; tile[2][1] -= c3; tile[2][2] -= c3; tile[2][3] += c3;
  tile[2][4] += c3; tile[2][5] -= c3; tile[2][6] -= c3; tile[2][7] += c3;

  tile[3][0] += c1; tile[3][1] -= c1; tile[3][2] -= c1; tile[3][3] += c1;
  tile[3][4] += c1; tile[3][5] -= c1; tile[3][6] -= c1; tile[3][7] += c1;

  tile[4][0] -= c1; tile[4][1] += c1; tile[4][2] += c1; tile[4][3] -= c1;
  tile[4][4] -= c1; tile[4][5] += c1; tile[4][6] += c1; tile[4][7] -= c1;

  tile[5][0] -= c3; tile[5][1] += c3; tile[5][2] += c3; tile[5][3] -= c3;
  tile[5][4] -= c3; tile[5][5] += c3; tile[5][6] += c3; tile[5][7] -= c3;

  tile[6][0] += c0; tile[6][1] -= c0; tile[6][2] -= c0; tile[6][3] += c0;
  tile[6][4] += c0; tile[6][5] -= c0; tile[6][6] -= c0; tile[6][7] += c0;

  tile[7][0] -= c2; tile[7][1] += c2; tile[7][2] += c2; tile[7][3] -= c2;
  tile[7][4] -= c2; tile[7][5] += c2; tile[7][6] += c2; tile[7][7] -= c2;
}

void splat_55(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c7; tile[0][1] -= c2; tile[0][2] += c8; tile[0][3] += c5;
  tile[0][4] -= c5; tile[0][5] -= c8; tile[0][6] += c2; tile[0][7] -= c7;

  tile[1][0] -= c2; tile[1][1] += c0; tile[1][2] -= c3; tile[1][3] -= c1;
  tile[1][4] += c1; tile[1][5] += c3; tile[1][6] -= c0; tile[1][7] += c2;

  tile[2][0] += c8; tile[2][1] -= c3; tile[2][2] += c9; tile[2][3] += c6;
  tile[2][4] -= c6; tile[2][5] -= c9; tile[2][6] += c3; tile[2][7] -= c8;

  tile[3][0] += c5; tile[3][1] -= c1; tile[3][2] += c6; tile[3][3] += c4;
  tile[3][4] -= c4; tile[3][5] -= c6; tile[3][6] += c1; tile[3][7] -= c5;

  tile[4][0] -= c5; tile[4][1] += c1; tile[4][2] -= c6; tile[4][3] -= c4;
  tile[4][4] += c4; tile[4][5] += c6; tile[4][6] -= c1; tile[4][7] += c5;

  tile[5][0] -= c8; tile[5][1] += c3; tile[5][2] -= c9; tile[5][3] -= c6;
  tile[5][4] += c6; tile[5][5] += c9; tile[5][6] -= c3; tile[5][7] += c8;

  tile[6][0] += c2; tile[6][1] -= c0; tile[6][2] += c3; tile[6][3] += c1;
  tile[6][4] -= c1; tile[6][5] -= c3; tile[6][6] += c0; tile[6][7] -= c2;

  tile[7][0] -= c7; tile[7][1] += c2; tile[7][2] -= c8; tile[7][3] -= c5;
  tile[7][4] += c5; tile[7][5] += c8; tile[7][6] -= c2; tile[7][7] += c7;
}

void splat_56(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c5; tile[0][1] -= c4; tile[0][2] += c4; tile[0][3] -= c5;
  tile[0][4] -= c5; tile[0][5] += c4; tile[0][6] -= c4; tile[0][7] += c5;

  tile[1][0] -= c1; tile[1][1] += c0; tile[1][2] -= c0; tile[1][3] += c1;
  tile[1][4] += c1; tile[1][5] -= c0; tile[1][6] += c0; tile[1][7] -= c1;

  tile[2][0] += c7; tile[2][1] -= c6; tile[2][2] += c6; tile[2][3] -= c7;
  tile[2][4] -= c7; tile[2][5] += c6; tile[2][6] -= c6; tile[2][7] += c7;

  tile[3][0] += c3; tile[3][1] -= c2; tile[3][2] += c2; tile[3][3] -= c3;
  tile[3][4] -= c3; tile[3][5] += c2; tile[3][6] -= c2; tile[3][7] += c3;

  tile[4][0] -= c3; tile[4][1] += c2; tile[4][2] -= c2; tile[4][3] += c3;
  tile[4][4] += c3; tile[4][5] -= c2; tile[4][6] += c2; tile[4][7] -= c3;

  tile[5][0] -= c7; tile[5][1] += c6; tile[5][2] -= c6; tile[5][3] += c7;
  tile[5][4] += c7; tile[5][5] -= c6; tile[5][6] += c6; tile[5][7] -= c7;

  tile[6][0] += c1; tile[6][1] -= c0; tile[6][2] += c0; tile[6][3] -= c1;
  tile[6][4] -= c1; tile[6][5] += c0; tile[6][6] -= c0; tile[6][7] += c1;

  tile[7][0] -= c5; tile[7][1] += c4; tile[7][2] -= c4; tile[7][3] += c5;
  tile[7][4] += c5; tile[7][5] -= c4; tile[7][6] += c4; tile[7][7] -= c5;
}

void splat_57(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c8; tile[0][1] -= c7; tile[0][2] += c5; tile[0][3] -= c2;
  tile[0][4] += c2; tile[0][5] -= c5; tile[0][6] += c7; tile[0][7] -= c8;

  tile[1][0] -= c3; tile[1][1] += c2; tile[1][2] -= c1; tile[1][3] += c0;
  tile[1][4] -= c0; tile[1][5] += c1; tile[1][6] -= c2; tile[1][7] += c3;

  tile[2][0] += c9; tile[2][1] -= c8; tile[2][2] += c6; tile[2][3] -= c3;
  tile[2][4] += c3; tile[2][5] -= c6; tile[2][6] += c8; tile[2][7] -= c9;

  tile[3][0] += c6; tile[3][1] -= c5; tile[3][2] += c4; tile[3][3] -= c1;
  tile[3][4] += c1; tile[3][5] -= c4; tile[3][6] += c5; tile[3][7] -= c6;

  tile[4][0] -= c6; tile[4][1] += c5; tile[4][2] -= c4; tile[4][3] += c1;
  tile[4][4] -= c1; tile[4][5] += c4; tile[4][6] -= c5; tile[4][7] += c6;

  tile[5][0] -= c9; tile[5][1] += c8; tile[5][2] -= c6; tile[5][3] += c3;
  tile[5][4] -= c3; tile[5][5] += c6; tile[5][6] -= c8; tile[5][7] += c9;

  tile[6][0] += c3; tile[6][1] -= c2; tile[6][2] += c1; tile[6][3] -= c0;
  tile[6][4] += c0; tile[6][5] -= c1; tile[6][6] += c2; tile[6][7] -= c3;

  tile[7][0] -= c8; tile[7][1] += c7; tile[7][2] -= c5; tile[7][3] += c2;
  tile[7][4] -= c2; tile[7][5] += c5; tile[7][6] -= c7; tile[7][7] += c8;
}

void splat_60(register int32 coeff)
{
  register int32 c0,c1;

  c0 = coeff * COS_02;
  c1 = coeff * COS_06;

  tile[0][0] += c1; tile[0][1] += c1; tile[0][2] += c1; tile[0][3] += c1;
  tile[0][4] += c1; tile[0][5] += c1; tile[0][6] += c1; tile[0][7] += c1;

  tile[1][0] -= c0; tile[1][1] -= c0; tile[1][2] -= c0; tile[1][3] -= c0;
  tile[1][4] -= c0; tile[1][5] -= c0; tile[1][6] -= c0; tile[1][7] -= c0;

  tile[2][0] += c0; tile[2][1] += c0; tile[2][2] += c0; tile[2][3] += c0;
  tile[2][4] += c0; tile[2][5] += c0; tile[2][6] += c0; tile[2][7] += c0;

  tile[3][0] -= c1; tile[3][1] -= c1; tile[3][2] -= c1; tile[3][3] -= c1;
  tile[3][4] -= c1; tile[3][5] -= c1; tile[3][6] -= c1; tile[3][7] -= c1;

  tile[4][0] -= c1; tile[4][1] -= c1; tile[4][2] -= c1; tile[4][3] -= c1;
  tile[4][4] -= c1; tile[4][5] -= c1; tile[4][6] -= c1; tile[4][7] -= c1;

  tile[5][0] += c0; tile[5][1] += c0; tile[5][2] += c0; tile[5][3] += c0;
  tile[5][4] += c0; tile[5][5] += c0; tile[5][6] += c0; tile[5][7] += c0;

  tile[6][0] -= c0; tile[6][1] -= c0; tile[6][2] -= c0; tile[6][3] -= c0;
  tile[6][4] -= c0; tile[6][5] -= c0; tile[6][6] -= c0; tile[6][7] -= c0;

  tile[7][0] += c1; tile[7][1] += c1; tile[7][2] += c1; tile[7][3] += c1;
  tile[7][4] += c1; tile[7][5] += c1; tile[7][6] += c1; tile[7][7] += c1;
}

void splat_61(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c1; tile[0][1] += c3; tile[0][2] += c5; tile[0][3] += c7;
  tile[0][4] -= c7; tile[0][5] -= c5; tile[0][6] -= c3; tile[0][7] -= c1;

  tile[1][0] -= c0; tile[1][1] -= c2; tile[1][2] -= c4; tile[1][3] -= c6;
  tile[1][4] += c6; tile[1][5] += c4; tile[1][6] += c2; tile[1][7] += c0;

  tile[2][0] += c0; tile[2][1] += c2; tile[2][2] += c4; tile[2][3] += c6;
  tile[2][4] -= c6; tile[2][5] -= c4; tile[2][6] -= c2; tile[2][7] -= c0;

  tile[3][0] -= c1; tile[3][1] -= c3; tile[3][2] -= c5; tile[3][3] -= c7;
  tile[3][4] += c7; tile[3][5] += c5; tile[3][6] += c3; tile[3][7] += c1;

  tile[4][0] -= c1; tile[4][1] -= c3; tile[4][2] -= c5; tile[4][3] -= c7;
  tile[4][4] += c7; tile[4][5] += c5; tile[4][6] += c3; tile[4][7] += c1;

  tile[5][0] += c0; tile[5][1] += c2; tile[5][2] += c4; tile[5][3] += c6;
  tile[5][4] -= c6; tile[5][5] -= c4; tile[5][6] -= c2; tile[5][7] -= c0;

  tile[6][0] -= c0; tile[6][1] -= c2; tile[6][2] -= c4; tile[6][3] -= c6;
  tile[6][4] += c6; tile[6][5] += c4; tile[6][6] += c2; tile[6][7] += c0;

  tile[7][0] += c1; tile[7][1] += c3; tile[7][2] += c5; tile[7][3] += c7;
  tile[7][4] -= c7; tile[7][5] -= c5; tile[7][6] -= c3; tile[7][7] -= c1;
}

void splat_62(register int32 coeff)
{
  register int32 c0,c1,c2;

  c0 = coeff * COS_22;
  c1 = coeff * COS_26;
  c2 = coeff * COS_66;

  tile[0][0] += c1; tile[0][1] += c2; tile[0][2] -= c2; tile[0][3] -= c1;
  tile[0][4] -= c1; tile[0][5] -= c2; tile[0][6] += c2; tile[0][7] += c1;

  tile[1][0] -= c0; tile[1][1] -= c1; tile[1][2] += c1; tile[1][3] += c0;
  tile[1][4] += c0; tile[1][5] += c1; tile[1][6] -= c1; tile[1][7] -= c0;

  tile[2][0] += c0; tile[2][1] += c1; tile[2][2] -= c1; tile[2][3] -= c0;
  tile[2][4] -= c0; tile[2][5] -= c1; tile[2][6] += c1; tile[2][7] += c0;

  tile[3][0] -= c1; tile[3][1] -= c2; tile[3][2] += c2; tile[3][3] += c1;
  tile[3][4] += c1; tile[3][5] += c2; tile[3][6] -= c2; tile[3][7] -= c1;

  tile[4][0] -= c1; tile[4][1] -= c2; tile[4][2] += c2; tile[4][3] += c1;
  tile[4][4] += c1; tile[4][5] += c2; tile[4][6] -= c2; tile[4][7] -= c1;

  tile[5][0] += c0; tile[5][1] += c1; tile[5][2] -= c1; tile[5][3] -= c0;
  tile[5][4] -= c0; tile[5][5] -= c1; tile[5][6] += c1; tile[5][7] += c0;

  tile[6][0] -= c0; tile[6][1] -= c1; tile[6][2] += c1; tile[6][3] += c0;
  tile[6][4] += c0; tile[6][5] += c1; tile[6][6] -= c1; tile[6][7] -= c0;

  tile[7][0] += c1; tile[7][1] += c2; tile[7][2] -= c2; tile[7][3] -= c1;
  tile[7][4] -= c1; tile[7][5] -= c2; tile[7][6] += c2; tile[7][7] += c1;
}

void splat_63(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c3; tile[0][1] -= c7; tile[0][2] -= c1; tile[0][3] -= c5;
  tile[0][4] += c5; tile[0][5] += c1; tile[0][6] += c7; tile[0][7] -= c3;

  tile[1][0] -= c2; tile[1][1] += c6; tile[1][2] += c0; tile[1][3] += c4;
  tile[1][4] -= c4; tile[1][5] -= c0; tile[1][6] -= c6; tile[1][7] += c2;

  tile[2][0] += c2; tile[2][1] -= c6; tile[2][2] -= c0; tile[2][3] -= c4;
  tile[2][4] += c4; tile[2][5] += c0; tile[2][6] += c6; tile[2][7] -= c2;

  tile[3][0] -= c3; tile[3][1] += c7; tile[3][2] += c1; tile[3][3] += c5;
  tile[3][4] -= c5; tile[3][5] -= c1; tile[3][6] -= c7; tile[3][7] += c3;

  tile[4][0] -= c3; tile[4][1] += c7; tile[4][2] += c1; tile[4][3] += c5;
  tile[4][4] -= c5; tile[4][5] -= c1; tile[4][6] -= c7; tile[4][7] += c3;

  tile[5][0] += c2; tile[5][1] -= c6; tile[5][2] -= c0; tile[5][3] -= c4;
  tile[5][4] += c4; tile[5][5] += c0; tile[5][6] += c6; tile[5][7] -= c2;

  tile[6][0] -= c2; tile[6][1] += c6; tile[6][2] += c0; tile[6][3] += c4;
  tile[6][4] -= c4; tile[6][5] -= c0; tile[6][6] -= c6; tile[6][7] += c2;

  tile[7][0] += c3; tile[7][1] -= c7; tile[7][2] -= c1; tile[7][3] -= c5;
  tile[7][4] += c5; tile[7][5] += c1; tile[7][6] += c7; tile[7][7] -= c3;
}

void splat_64(register int32 coeff)
{
  register int32 c0,c1;

  c0 = coeff * COS_24;
  c1 = coeff * COS_64;

  tile[0][0] += c1; tile[0][1] -= c1; tile[0][2] -= c1; tile[0][3] += c1;
  tile[0][4] += c1; tile[0][5] -= c1; tile[0][6] -= c1; tile[0][7] += c1;

  tile[1][0] -= c0; tile[1][1] += c0; tile[1][2] += c0; tile[1][3] -= c0;
  tile[1][4] -= c0; tile[1][5] += c0; tile[1][6] += c0; tile[1][7] -= c0;

  tile[2][0] += c0; tile[2][1] -= c0; tile[2][2] -= c0; tile[2][3] += c0;
  tile[2][4] += c0; tile[2][5] -= c0; tile[2][6] -= c0; tile[2][7] += c0;

  tile[3][0] -= c1; tile[3][1] += c1; tile[3][2] += c1; tile[3][3] -= c1;
  tile[3][4] -= c1; tile[3][5] += c1; tile[3][6] += c1; tile[3][7] -= c1;

  tile[4][0] -= c1; tile[4][1] += c1; tile[4][2] += c1; tile[4][3] -= c1;
  tile[4][4] -= c1; tile[4][5] += c1; tile[4][6] += c1; tile[4][7] -= c1;

  tile[5][0] += c0; tile[5][1] -= c0; tile[5][2] -= c0; tile[5][3] += c0;
  tile[5][4] += c0; tile[5][5] -= c0; tile[5][6] -= c0; tile[5][7] += c0;

  tile[6][0] -= c0; tile[6][1] += c0; tile[6][2] += c0; tile[6][3] -= c0;
  tile[6][4] -= c0; tile[6][5] += c0; tile[6][6] += c0; tile[6][7] -= c0;

  tile[7][0] += c1; tile[7][1] -= c1; tile[7][2] -= c1; tile[7][3] += c1;
  tile[7][4] += c1; tile[7][5] -= c1; tile[7][6] -= c1; tile[7][7] += c1;
}

void splat_65(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c5; tile[0][1] -= c1; tile[0][2] += c7; tile[0][3] += c3;
  tile[0][4] -= c3; tile[0][5] -= c7; tile[0][6] += c1; tile[0][7] -= c5;

  tile[1][0] -= c4; tile[1][1] += c0; tile[1][2] -= c6; tile[1][3] -= c2;
  tile[1][4] += c2; tile[1][5] += c6; tile[1][6] -= c0; tile[1][7] += c4;

  tile[2][0] += c4; tile[2][1] -= c0; tile[2][2] += c6; tile[2][3] += c2;
  tile[2][4] -= c2; tile[2][5] -= c6; tile[2][6] += c0; tile[2][7] -= c4;

  tile[3][0] -= c5; tile[3][1] += c1; tile[3][2] -= c7; tile[3][3] -= c3;
  tile[3][4] += c3; tile[3][5] += c7; tile[3][6] -= c1; tile[3][7] += c5;

  tile[4][0] -= c5; tile[4][1] += c1; tile[4][2] -= c7; tile[4][3] -= c3;
  tile[4][4] += c3; tile[4][5] += c7; tile[4][6] -= c1; tile[4][7] += c5;

  tile[5][0] += c4; tile[5][1] -= c0; tile[5][2] += c6; tile[5][3] += c2;
  tile[5][4] -= c2; tile[5][5] -= c6; tile[5][6] += c0; tile[5][7] -= c4;

  tile[6][0] -= c4; tile[6][1] += c0; tile[6][2] -= c6; tile[6][3] -= c2;
  tile[6][4] += c2; tile[6][5] += c6; tile[6][6] -= c0; tile[6][7] += c4;

  tile[7][0] += c5; tile[7][1] -= c1; tile[7][2] += c7; tile[7][3] += c3;
  tile[7][4] -= c3; tile[7][5] -= c7; tile[7][6] += c1; tile[7][7] -= c5;
}

void splat_66(register int32 coeff)
{
  register int32 c0,c1,c2;

  c0 = coeff * COS_22;
  c1 = coeff * COS_26;
  c2 = coeff * COS_66;

  tile[0][0] += c2; tile[0][1] -= c1; tile[0][2] += c1; tile[0][3] -= c2;
  tile[0][4] -= c2; tile[0][5] += c1; tile[0][6] -= c1; tile[0][7] += c2;

  tile[1][0] -= c1; tile[1][1] += c0; tile[1][2] -= c0; tile[1][3] += c1;
  tile[1][4] += c1; tile[1][5] -= c0; tile[1][6] += c0; tile[1][7] -= c1;

  tile[2][0] += c1; tile[2][1] -= c0; tile[2][2] += c0; tile[2][3] -= c1;
  tile[2][4] -= c1; tile[2][5] += c0; tile[2][6] -= c0; tile[2][7] += c1;

  tile[3][0] -= c2; tile[3][1] += c1; tile[3][2] -= c1; tile[3][3] += c2;
  tile[3][4] += c2; tile[3][5] -= c1; tile[3][6] += c1; tile[3][7] -= c2;

  tile[4][0] -= c2; tile[4][1] += c1; tile[4][2] -= c1; tile[4][3] += c2;
  tile[4][4] += c2; tile[4][5] -= c1; tile[4][6] += c1; tile[4][7] -= c2;

  tile[5][0] += c1; tile[5][1] -= c0; tile[5][2] += c0; tile[5][3] -= c1;
  tile[5][4] -= c1; tile[5][5] += c0; tile[5][6] -= c0; tile[5][7] += c1;

  tile[6][0] -= c1; tile[6][1] += c0; tile[6][2] -= c0; tile[6][3] += c1;
  tile[6][4] += c1; tile[6][5] -= c0; tile[6][6] += c0; tile[6][7] -= c1;

  tile[7][0] += c2; tile[7][1] -= c1; tile[7][2] += c1; tile[7][3] -= c2;
  tile[7][4] -= c2; tile[7][5] += c1; tile[7][6] -= c1; tile[7][7] += c2;
}

void splat_67(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c7; tile[0][1] -= c5; tile[0][2] += c3; tile[0][3] -= c1;
  tile[0][4] += c1; tile[0][5] -= c3; tile[0][6] += c5; tile[0][7] -= c7;

  tile[1][0] -= c6; tile[1][1] += c4; tile[1][2] -= c2; tile[1][3] += c0;
  tile[1][4] -= c0; tile[1][5] += c2; tile[1][6] -= c4; tile[1][7] += c6;

  tile[2][0] += c6; tile[2][1] -= c4; tile[2][2] += c2; tile[2][3] -= c0;
  tile[2][4] += c0; tile[2][5] -= c2; tile[2][6] += c4; tile[2][7] -= c6;

  tile[3][0] -= c7; tile[3][1] += c5; tile[3][2] -= c3; tile[3][3] += c1;
  tile[3][4] -= c1; tile[3][5] += c3; tile[3][6] -= c5; tile[3][7] += c7;

  tile[4][0] -= c7; tile[4][1] += c5; tile[4][2] -= c3; tile[4][3] += c1;
  tile[4][4] -= c1; tile[4][5] += c3; tile[4][6] -= c5; tile[4][7] += c7;

  tile[5][0] += c6; tile[5][1] -= c4; tile[5][2] += c2; tile[5][3] -= c0;
  tile[5][4] += c0; tile[5][5] -= c2; tile[5][6] += c4; tile[5][7] -= c6;

  tile[6][0] -= c6; tile[6][1] += c4; tile[6][2] -= c2; tile[6][3] += c0;
  tile[6][4] -= c0; tile[6][5] += c2; tile[6][6] -= c4; tile[6][7] += c6;

  tile[7][0] += c7; tile[7][1] -= c5; tile[7][2] += c3; tile[7][3] -= c1;
  tile[7][4] += c1; tile[7][5] -= c3; tile[7][6] += c5; tile[7][7] -= c7;
}

void splat_70(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_01;
  c1 = coeff * COS_03;
  c2 = coeff * COS_05;
  c3 = coeff * COS_07;

  tile[0][0] += c3; tile[0][1] += c3; tile[0][2] += c3; tile[0][3] += c3;
  tile[0][4] += c3; tile[0][5] += c3; tile[0][6] += c3; tile[0][7] += c3;

  tile[1][0] -= c2; tile[1][1] -= c2; tile[1][2] -= c2; tile[1][3] -= c2;
  tile[1][4] -= c2; tile[1][5] -= c2; tile[1][6] -= c2; tile[1][7] -= c2;

  tile[2][0] += c1; tile[2][1] += c1; tile[2][2] += c1; tile[2][3] += c1;
  tile[2][4] += c1; tile[2][5] += c1; tile[2][6] += c1; tile[2][7] += c1;

  tile[3][0] -= c0; tile[3][1] -= c0; tile[3][2] -= c0; tile[3][3] -= c0;
  tile[3][4] -= c0; tile[3][5] -= c0; tile[3][6] -= c0; tile[3][7] -= c0;

  tile[4][0] += c0; tile[4][1] += c0; tile[4][2] += c0; tile[4][3] += c0;
  tile[4][4] += c0; tile[4][5] += c0; tile[4][6] += c0; tile[4][7] += c0;

  tile[5][0] -= c1; tile[5][1] -= c1; tile[5][2] -= c1; tile[5][3] -= c1;
  tile[5][4] -= c1; tile[5][5] -= c1; tile[5][6] -= c1; tile[5][7] -= c1;

  tile[6][0] += c2; tile[6][1] += c2; tile[6][2] += c2; tile[6][3] += c2;
  tile[6][4] += c2; tile[6][5] += c2; tile[6][6] += c2; tile[6][7] += c2;

  tile[7][0] -= c3; tile[7][1] -= c3; tile[7][2] -= c3; tile[7][3] -= c3;
  tile[7][4] -= c3; tile[7][5] -= c3; tile[7][6] -= c3; tile[7][7] -= c3;
}

void splat_71(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c3; tile[0][1] += c6; tile[0][2] += c8; tile[0][3] += c9;
  tile[0][4] -= c9; tile[0][5] -= c8; tile[0][6] -= c6; tile[0][7] -= c3;

  tile[1][0] -= c2; tile[1][1] -= c5; tile[1][2] -= c7; tile[1][3] -= c8;
  tile[1][4] += c8; tile[1][5] += c7; tile[1][6] += c5; tile[1][7] += c2;

  tile[2][0] += c1; tile[2][1] += c4; tile[2][2] += c5; tile[2][3] += c6;
  tile[2][4] -= c6; tile[2][5] -= c5; tile[2][6] -= c4; tile[2][7] -= c1;

  tile[3][0] -= c0; tile[3][1] -= c1; tile[3][2] -= c2; tile[3][3] -= c3;
  tile[3][4] += c3; tile[3][5] += c2; tile[3][6] += c1; tile[3][7] += c0;

  tile[4][0] += c0; tile[4][1] += c1; tile[4][2] += c2; tile[4][3] += c3;
  tile[4][4] -= c3; tile[4][5] -= c2; tile[4][6] -= c1; tile[4][7] -= c0;

  tile[5][0] -= c1; tile[5][1] -= c4; tile[5][2] -= c5; tile[5][3] -= c6;
  tile[5][4] += c6; tile[5][5] += c5; tile[5][6] += c4; tile[5][7] += c1;

  tile[6][0] += c2; tile[6][1] += c5; tile[6][2] += c7; tile[6][3] += c8;
  tile[6][4] -= c8; tile[6][5] -= c7; tile[6][6] -= c5; tile[6][7] -= c2;

  tile[7][0] -= c3; tile[7][1] -= c6; tile[7][2] -= c8; tile[7][3] -= c9;
  tile[7][4] += c9; tile[7][5] += c8; tile[7][6] += c6; tile[7][7] += c3;
}

void splat_72(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c6; tile[0][1] += c7; tile[0][2] -= c7; tile[0][3] -= c6;
  tile[0][4] -= c6; tile[0][5] -= c7; tile[0][6] += c7; tile[0][7] += c6;

  tile[1][0] -= c4; tile[1][1] -= c5; tile[1][2] += c5; tile[1][3] += c4;
  tile[1][4] += c4; tile[1][5] += c5; tile[1][6] -= c5; tile[1][7] -= c4;

  tile[2][0] += c2; tile[2][1] += c3; tile[2][2] -= c3; tile[2][3] -= c2;
  tile[2][4] -= c2; tile[2][5] -= c3; tile[2][6] += c3; tile[2][7] += c2;

  tile[3][0] -= c0; tile[3][1] -= c1; tile[3][2] += c1; tile[3][3] += c0;
  tile[3][4] += c0; tile[3][5] += c1; tile[3][6] -= c1; tile[3][7] -= c0;

  tile[4][0] += c0; tile[4][1] += c1; tile[4][2] -= c1; tile[4][3] -= c0;
  tile[4][4] -= c0; tile[4][5] -= c1; tile[4][6] += c1; tile[4][7] += c0;

  tile[5][0] -= c2; tile[5][1] -= c3; tile[5][2] += c3; tile[5][3] += c2;
  tile[5][4] += c2; tile[5][5] += c3; tile[5][6] -= c3; tile[5][7] -= c2;

  tile[6][0] += c4; tile[6][1] += c5; tile[6][2] -= c5; tile[6][3] -= c4;
  tile[6][4] -= c4; tile[6][5] -= c5; tile[6][6] += c5; tile[6][7] += c4;

  tile[7][0] -= c6; tile[7][1] -= c7; tile[7][2] += c7; tile[7][3] += c6;
  tile[7][4] += c6; tile[7][5] += c7; tile[7][6] -= c7; tile[7][7] -= c6;
}

void splat_73(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c6; tile[0][1] -= c9; tile[0][2] -= c3; tile[0][3] -= c8;
  tile[0][4] += c8; tile[0][5] += c3; tile[0][6] += c9; tile[0][7] -= c6;

  tile[1][0] -= c5; tile[1][1] += c8; tile[1][2] += c2; tile[1][3] += c7;
  tile[1][4] -= c7; tile[1][5] -= c2; tile[1][6] -= c8; tile[1][7] += c5;

  tile[2][0] += c4; tile[2][1] -= c6; tile[2][2] -= c1; tile[2][3] -= c5;
  tile[2][4] += c5; tile[2][5] += c1; tile[2][6] += c6; tile[2][7] -= c4;

  tile[3][0] -= c1; tile[3][1] += c3; tile[3][2] += c0; tile[3][3] += c2;
  tile[3][4] -= c2; tile[3][5] -= c0; tile[3][6] -= c3; tile[3][7] += c1;

  tile[4][0] += c1; tile[4][1] -= c3; tile[4][2] -= c0; tile[4][3] -= c2;
  tile[4][4] += c2; tile[4][5] += c0; tile[4][6] += c3; tile[4][7] -= c1;

  tile[5][0] -= c4; tile[5][1] += c6; tile[5][2] += c1; tile[5][3] += c5;
  tile[5][4] -= c5; tile[5][5] -= c1; tile[5][6] -= c6; tile[5][7] += c4;

  tile[6][0] += c5; tile[6][1] -= c8; tile[6][2] -= c2; tile[6][3] -= c7;
  tile[6][4] += c7; tile[6][5] += c2; tile[6][6] += c8; tile[6][7] -= c5;

  tile[7][0] -= c6; tile[7][1] += c9; tile[7][2] += c3; tile[7][3] += c8;
  tile[7][4] -= c8; tile[7][5] -= c3; tile[7][6] -= c9; tile[7][7] += c6;
}

void splat_74(register int32 coeff)
{
  register int32 c0,c1,c2,c3;

  c0 = coeff * COS_14;
  c1 = coeff * COS_34;
  c2 = coeff * COS_54;
  c3 = coeff * COS_74;

  tile[0][0] += c3; tile[0][1] -= c3; tile[0][2] -= c3; tile[0][3] += c3;
  tile[0][4] += c3; tile[0][5] -= c3; tile[0][6] -= c3; tile[0][7] += c3;

  tile[1][0] -= c2; tile[1][1] += c2; tile[1][2] += c2; tile[1][3] -= c2;
  tile[1][4] -= c2; tile[1][5] += c2; tile[1][6] += c2; tile[1][7] -= c2;

  tile[2][0] += c1; tile[2][1] -= c1; tile[2][2] -= c1; tile[2][3] += c1;
  tile[2][4] += c1; tile[2][5] -= c1; tile[2][6] -= c1; tile[2][7] += c1;

  tile[3][0] -= c0; tile[3][1] += c0; tile[3][2] += c0; tile[3][3] -= c0;
  tile[3][4] -= c0; tile[3][5] += c0; tile[3][6] += c0; tile[3][7] -= c0;

  tile[4][0] += c0; tile[4][1] -= c0; tile[4][2] -= c0; tile[4][3] += c0;
  tile[4][4] += c0; tile[4][5] -= c0; tile[4][6] -= c0; tile[4][7] += c0;

  tile[5][0] -= c1; tile[5][1] += c1; tile[5][2] += c1; tile[5][3] -= c1;
  tile[5][4] -= c1; tile[5][5] += c1; tile[5][6] += c1; tile[5][7] -= c1;

  tile[6][0] += c2; tile[6][1] -= c2; tile[6][2] -= c2; tile[6][3] += c2;
  tile[6][4] += c2; tile[6][5] -= c2; tile[6][6] -= c2; tile[6][7] += c2;

  tile[7][0] -= c3; tile[7][1] += c3; tile[7][2] += c3; tile[7][3] -= c3;
  tile[7][4] -= c3; tile[7][5] += c3; tile[7][6] += c3; tile[7][7] -= c3;
}

void splat_75(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c8; tile[0][1] -= c3; tile[0][2] += c9; tile[0][3] += c6;
  tile[0][4] -= c6; tile[0][5] -= c9; tile[0][6] += c3; tile[0][7] -= c8;

  tile[1][0] -= c7; tile[1][1] += c2; tile[1][2] -= c8; tile[1][3] -= c5;
  tile[1][4] += c5; tile[1][5] += c8; tile[1][6] -= c2; tile[1][7] += c7;

  tile[2][0] += c5; tile[2][1] -= c1; tile[2][2] += c6; tile[2][3] += c4;
  tile[2][4] -= c4; tile[2][5] -= c6; tile[2][6] += c1; tile[2][7] -= c5;

  tile[3][0] -= c2; tile[3][1] += c0; tile[3][2] -= c3; tile[3][3] -= c1;
  tile[3][4] += c1; tile[3][5] += c3; tile[3][6] -= c0; tile[3][7] += c2;

  tile[4][0] += c2; tile[4][1] -= c0; tile[4][2] += c3; tile[4][3] += c1;
  tile[4][4] -= c1; tile[4][5] -= c3; tile[4][6] += c0; tile[4][7] -= c2;

  tile[5][0] -= c5; tile[5][1] += c1; tile[5][2] -= c6; tile[5][3] -= c4;
  tile[5][4] += c4; tile[5][5] += c6; tile[5][6] -= c1; tile[5][7] += c5;

  tile[6][0] += c7; tile[6][1] -= c2; tile[6][2] += c8; tile[6][3] += c5;
  tile[6][4] -= c5; tile[6][5] -= c8; tile[6][6] += c2; tile[6][7] -= c7;

  tile[7][0] -= c8; tile[7][1] += c3; tile[7][2] -= c9; tile[7][3] -= c6;
  tile[7][4] += c6; tile[7][5] += c9; tile[7][6] -= c3; tile[7][7] += c8;
}

void splat_76(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7;

  c0 = coeff * COS_12;
  c1 = coeff * COS_16;
  c2 = coeff * COS_32;
  c3 = coeff * COS_36;
  c4 = coeff * COS_52;
  c5 = coeff * COS_56;
  c6 = coeff * COS_72;
  c7 = coeff * COS_76;

  tile[0][0] += c7; tile[0][1] -= c6; tile[0][2] += c6; tile[0][3] -= c7;
  tile[0][4] -= c7; tile[0][5] += c6; tile[0][6] -= c6; tile[0][7] += c7;

  tile[1][0] -= c5; tile[1][1] += c4; tile[1][2] -= c4; tile[1][3] += c5;
  tile[1][4] += c5; tile[1][5] -= c4; tile[1][6] += c4; tile[1][7] -= c5;

  tile[2][0] += c3; tile[2][1] -= c2; tile[2][2] += c2; tile[2][3] -= c3;
  tile[2][4] -= c3; tile[2][5] += c2; tile[2][6] -= c2; tile[2][7] += c3;

  tile[3][0] -= c1; tile[3][1] += c0; tile[3][2] -= c0; tile[3][3] += c1;
  tile[3][4] += c1; tile[3][5] -= c0; tile[3][6] += c0; tile[3][7] -= c1;

  tile[4][0] += c1; tile[4][1] -= c0; tile[4][2] += c0; tile[4][3] -= c1;
  tile[4][4] -= c1; tile[4][5] += c0; tile[4][6] -= c0; tile[4][7] += c1;

  tile[5][0] -= c3; tile[5][1] += c2; tile[5][2] -= c2; tile[5][3] += c3;
  tile[5][4] += c3; tile[5][5] -= c2; tile[5][6] += c2; tile[5][7] -= c3;

  tile[6][0] += c5; tile[6][1] -= c4; tile[6][2] += c4; tile[6][3] -= c5;
  tile[6][4] -= c5; tile[6][5] += c4; tile[6][6] -= c4; tile[6][7] += c5;

  tile[7][0] -= c7; tile[7][1] += c6; tile[7][2] -= c6; tile[7][3] += c7;
  tile[7][4] += c7; tile[7][5] -= c6; tile[7][6] += c6; tile[7][7] -= c7;
}

void splat_77(register int32 coeff)
{
  register int32 c0,c1,c2,c3,c4,c5,c6,c7,c8,c9;

  c0 = coeff * COS_11;
  c1 = coeff * COS_13;
  c2 = coeff * COS_15;
  c3 = coeff * COS_17;
  c4 = coeff * COS_33;
  c5 = coeff * COS_35;
  c6 = coeff * COS_37;
  c7 = coeff * COS_55;
  c8 = coeff * COS_57;
  c9 = coeff * COS_77;

  tile[0][0] += c9; tile[0][1] -= c8; tile[0][2] += c6; tile[0][3] -= c3;
  tile[0][4] += c3; tile[0][5] -= c6; tile[0][6] += c8; tile[0][7] -= c9;

  tile[1][0] -= c8; tile[1][1] += c7; tile[1][2] -= c5; tile[1][3] += c2;
  tile[1][4] -= c2; tile[1][5] += c5; tile[1][6] -= c7; tile[1][7] += c8;

  tile[2][0] += c6; tile[2][1] -= c5; tile[2][2] += c4; tile[2][3] -= c1;
  tile[2][4] += c1; tile[2][5] -= c4; tile[2][6] += c5; tile[2][7] -= c6;

  tile[3][0] -= c3; tile[3][1] += c2; tile[3][2] -= c1; tile[3][3] += c0;
  tile[3][4] -= c0; tile[3][5] += c1; tile[3][6] -= c2; tile[3][7] += c3;

  tile[4][0] += c3; tile[4][1] -= c2; tile[4][2] += c1; tile[4][3] -= c0;
  tile[4][4] += c0; tile[4][5] -= c1; tile[4][6] += c2; tile[4][7] -= c3;

  tile[5][0] -= c6; tile[5][1] += c5; tile[5][2] -= c4; tile[5][3] += c1;
  tile[5][4] -= c1; tile[5][5] += c4; tile[5][6] -= c5; tile[5][7] += c6;

  tile[6][0] += c8; tile[6][1] -= c7; tile[6][2] += c5; tile[6][3] -= c2;
  tile[6][4] += c2; tile[6][5] -= c5; tile[6][6] += c7; tile[6][7] -= c8;

  tile[7][0] -= c9; tile[7][1] += c8; tile[7][2] -= c6; tile[7][3] += c3;
  tile[7][4] -= c3; tile[7][5] += c6; tile[7][6] -= c8; tile[7][7] += c9;
}

/**
 * Temp function to simplify inverse-DCT data paths.
 * Just copy the supplied data into the output tile
 */
void splat_tile(int32 src[8][8])
{
  int i, j;

#if 0
  for ( j = 0; j < 8; j++ )
  {
    for ( i = 0; i < 8; i++ )
    {
      printf("%08x ", tile[j][i]);
    }
    printf("\n");
  }
  printf("   ---- \n");
  for ( j = 0; j < 8; j++ )
  {
    for ( i = 0; i < 8; i++ )
    {
      printf("%08x ", src[j][i]);
    }
    printf("\n");
  }
  printf("   ---- \n");
  for ( j = 0; j < 8; j++ )
  {
    for ( i = 0; i < 8; i++ )
    {
      int32 d = src[j][i] - tile[j][i];
      if ( d < 0 ) d = -d;

      printf("%08x ", d);
    }
    printf("\n");
  }
  printf("   ---- \n");
#endif

  for ( j = 0; j < 8; j++ )
    for ( i = 0; i < 8; i++ )
      tile[i][j] = src[i][j];
}

void zero_tile(void)
{
 tile[0][0] = 0; tile[0][1] = 0; tile[0][2] = 0; tile[0][3] = 0;
 tile[0][4] = 0; tile[0][5] = 0; tile[0][6] = 0; tile[0][7] = 0;
 tile[1][0] = 0; tile[1][1] = 0; tile[1][2] = 0; tile[1][3] = 0;
 tile[1][4] = 0; tile[1][5] = 0; tile[1][6] = 0; tile[1][7] = 0;
 tile[2][0] = 0; tile[2][1] = 0; tile[2][2] = 0; tile[2][3] = 0;
 tile[2][4] = 0; tile[2][5] = 0; tile[2][6] = 0; tile[2][7] = 0;
 tile[3][0] = 0; tile[3][1] = 0; tile[3][2] = 0; tile[3][3] = 0;
 tile[3][4] = 0; tile[3][5] = 0; tile[3][6] = 0; tile[3][7] = 0;
 tile[4][0] = 0; tile[4][1] = 0; tile[4][2] = 0; tile[4][3] = 0;
 tile[4][4] = 0; tile[4][5] = 0; tile[4][6] = 0; tile[4][7] = 0;
 tile[5][0] = 0; tile[5][1] = 0; tile[5][2] = 0; tile[5][3] = 0;
 tile[5][4] = 0; tile[5][5] = 0; tile[5][6] = 0; tile[5][7] = 0;
 tile[6][0] = 0; tile[6][1] = 0; tile[6][2] = 0; tile[6][3] = 0;
 tile[6][4] = 0; tile[6][5] = 0; tile[6][6] = 0; tile[6][7] = 0;
 tile[7][0] = 0; tile[7][1] = 0; tile[7][2] = 0; tile[7][3] = 0;
 tile[7][4] = 0; tile[7][5] = 0; tile[7][6] = 0; tile[7][7] = 0;
}

void unfix_clip_tile(register int32 *block, register int32 skip)
{
  register int32 t;

  skip += 8 ;

  t = UNFIX(tile[0][0])+128; RANGE_LIMIT(t); block[0] = t;
  t = UNFIX(tile[0][1])+128; RANGE_LIMIT(t); block[1] = t;
  t = UNFIX(tile[0][2])+128; RANGE_LIMIT(t); block[2] = t;
  t = UNFIX(tile[0][3])+128; RANGE_LIMIT(t); block[3] = t;
  t = UNFIX(tile[0][4])+128; RANGE_LIMIT(t); block[4] = t;
  t = UNFIX(tile[0][5])+128; RANGE_LIMIT(t); block[5] = t;
  t = UNFIX(tile[0][6])+128; RANGE_LIMIT(t); block[6] = t;
  t = UNFIX(tile[0][7])+128; RANGE_LIMIT(t); block[7] = t;
  block += skip;

  t = UNFIX(tile[1][0])+128; RANGE_LIMIT(t); block[0] = t;
  t = UNFIX(tile[1][1])+128; RANGE_LIMIT(t); block[1] = t;
  t = UNFIX(tile[1][2])+128; RANGE_LIMIT(t); block[2] = t;
  t = UNFIX(tile[1][3])+128; RANGE_LIMIT(t); block[3] = t;
  t = UNFIX(tile[1][4])+128; RANGE_LIMIT(t); block[4] = t;
  t = UNFIX(tile[1][5])+128; RANGE_LIMIT(t); block[5] = t;
  t = UNFIX(tile[1][6])+128; RANGE_LIMIT(t); block[6] = t;
  t = UNFIX(tile[1][7])+128; RANGE_LIMIT(t); block[7] = t;
  block += skip;

  t = UNFIX(tile[2][0])+128; RANGE_LIMIT(t); block[0] = t;
  t = UNFIX(tile[2][1])+128; RANGE_LIMIT(t); block[1] = t;
  t = UNFIX(tile[2][2])+128; RANGE_LIMIT(t); block[2] = t;
  t = UNFIX(tile[2][3])+128; RANGE_LIMIT(t); block[3] = t;
  t = UNFIX(tile[2][4])+128; RANGE_LIMIT(t); block[4] = t;
  t = UNFIX(tile[2][5])+128; RANGE_LIMIT(t); block[5] = t;
  t = UNFIX(tile[2][6])+128; RANGE_LIMIT(t); block[6] = t;
  t = UNFIX(tile[2][7])+128; RANGE_LIMIT(t); block[7] = t;
  block += skip;

  t = UNFIX(tile[3][0])+128; RANGE_LIMIT(t); block[0] = t;
  t = UNFIX(tile[3][1])+128; RANGE_LIMIT(t); block[1] = t;
  t = UNFIX(tile[3][2])+128; RANGE_LIMIT(t); block[2] = t;
  t = UNFIX(tile[3][3])+128; RANGE_LIMIT(t); block[3] = t;
  t = UNFIX(tile[3][4])+128; RANGE_LIMIT(t); block[4] = t;
  t = UNFIX(tile[3][5])+128; RANGE_LIMIT(t); block[5] = t;
  t = UNFIX(tile[3][6])+128; RANGE_LIMIT(t); block[6] = t;
  t = UNFIX(tile[3][7])+128; RANGE_LIMIT(t); block[7] = t;
  block += skip;

  t = UNFIX(tile[4][0])+128; RANGE_LIMIT(t); block[0] = t;
  t = UNFIX(tile[4][1])+128; RANGE_LIMIT(t); block[1] = t;
  t = UNFIX(tile[4][2])+128; RANGE_LIMIT(t); block[2] = t;
  t = UNFIX(tile[4][3])+128; RANGE_LIMIT(t); block[3] = t;
  t = UNFIX(tile[4][4])+128; RANGE_LIMIT(t); block[4] = t;
  t = UNFIX(tile[4][5])+128; RANGE_LIMIT(t); block[5] = t;
  t = UNFIX(tile[4][6])+128; RANGE_LIMIT(t); block[6] = t;
  t = UNFIX(tile[4][7])+128; RANGE_LIMIT(t); block[7] = t;
  block += skip;

  t = UNFIX(tile[5][0])+128; RANGE_LIMIT(t); block[0] = t;
  t = UNFIX(tile[5][1])+128; RANGE_LIMIT(t); block[1] = t;
  t = UNFIX(tile[5][2])+128; RANGE_LIMIT(t); block[2] = t;
  t = UNFIX(tile[5][3])+128; RANGE_LIMIT(t); block[3] = t;
  t = UNFIX(tile[5][4])+128; RANGE_LIMIT(t); block[4] = t;
  t = UNFIX(tile[5][5])+128; RANGE_LIMIT(t); block[5] = t;
  t = UNFIX(tile[5][6])+128; RANGE_LIMIT(t); block[6] = t;
  t = UNFIX(tile[5][7])+128; RANGE_LIMIT(t); block[7] = t;
  block += skip;

  t = UNFIX(tile[6][0])+128; RANGE_LIMIT(t); block[0] = t;
  t = UNFIX(tile[6][1])+128; RANGE_LIMIT(t); block[1] = t;
  t = UNFIX(tile[6][2])+128; RANGE_LIMIT(t); block[2] = t;
  t = UNFIX(tile[6][3])+128; RANGE_LIMIT(t); block[3] = t;
  t = UNFIX(tile[6][4])+128; RANGE_LIMIT(t); block[4] = t;
  t = UNFIX(tile[6][5])+128; RANGE_LIMIT(t); block[5] = t;
  t = UNFIX(tile[6][6])+128; RANGE_LIMIT(t); block[6] = t;
  t = UNFIX(tile[6][7])+128; RANGE_LIMIT(t); block[7] = t;
  block += skip;

  t = UNFIX(tile[7][0])+128; RANGE_LIMIT(t); block[0] = t;
  t = UNFIX(tile[7][1])+128; RANGE_LIMIT(t); block[1] = t;
  t = UNFIX(tile[7][2])+128; RANGE_LIMIT(t); block[2] = t;
  t = UNFIX(tile[7][3])+128; RANGE_LIMIT(t); block[3] = t;
  t = UNFIX(tile[7][4])+128; RANGE_LIMIT(t); block[4] = t;
  t = UNFIX(tile[7][5])+128; RANGE_LIMIT(t); block[5] = t;
  t = UNFIX(tile[7][6])+128; RANGE_LIMIT(t); block[6] = t;
  t = UNFIX(tile[7][7])+128; RANGE_LIMIT(t); block[7] = t;
}

void unfix_tile(register int32 *block, register int32 skip)
{
  skip += 8 ;

  block[0] = UNFIX(tile[0][0]);   block[1] = UNFIX(tile[0][1]);
  block[2] = UNFIX(tile[0][2]);   block[3] = UNFIX(tile[0][3]);
  block[4] = UNFIX(tile[0][4]);   block[5] = UNFIX(tile[0][5]);
  block[6] = UNFIX(tile[0][6]);   block[7] = UNFIX(tile[0][7]);
  block += skip;

  block[0] = UNFIX(tile[1][0]);   block[1] = UNFIX(tile[1][1]);
  block[2] = UNFIX(tile[1][2]);   block[3] = UNFIX(tile[1][3]);
  block[4] = UNFIX(tile[1][4]);   block[5] = UNFIX(tile[1][5]);
  block[6] = UNFIX(tile[1][6]);   block[7] = UNFIX(tile[1][7]);
  block += skip;

  block[0] = UNFIX(tile[2][0]);   block[1] = UNFIX(tile[2][1]);
  block[2] = UNFIX(tile[2][2]);   block[3] = UNFIX(tile[2][3]);
  block[4] = UNFIX(tile[2][4]);   block[5] = UNFIX(tile[2][5]);
  block[6] = UNFIX(tile[2][6]);   block[7] = UNFIX(tile[2][7]);
  block += skip;

  block[0] = UNFIX(tile[3][0]);   block[1] = UNFIX(tile[3][1]);
  block[2] = UNFIX(tile[3][2]);   block[3] = UNFIX(tile[3][3]);
  block[4] = UNFIX(tile[3][4]);   block[5] = UNFIX(tile[3][5]);
  block[6] = UNFIX(tile[3][6]);   block[7] = UNFIX(tile[3][7]);
  block += skip;

  block[0] = UNFIX(tile[4][0]);   block[1] = UNFIX(tile[4][1]);
  block[2] = UNFIX(tile[4][2]);   block[3] = UNFIX(tile[4][3]);
  block[4] = UNFIX(tile[4][4]);   block[5] = UNFIX(tile[4][5]);
  block[6] = UNFIX(tile[4][6]);   block[7] = UNFIX(tile[4][7]);
  block += skip;

  block[0] = UNFIX(tile[5][0]);   block[1] = UNFIX(tile[5][1]);
  block[2] = UNFIX(tile[5][2]);   block[3] = UNFIX(tile[5][3]);
  block[4] = UNFIX(tile[5][4]);   block[5] = UNFIX(tile[5][5]);
  block[6] = UNFIX(tile[5][6]);   block[7] = UNFIX(tile[5][7]);
  block += skip;

  block[0] = UNFIX(tile[6][0]);   block[1] = UNFIX(tile[6][1]);
  block[2] = UNFIX(tile[6][2]);   block[3] = UNFIX(tile[6][3]);
  block[4] = UNFIX(tile[6][4]);   block[5] = UNFIX(tile[6][5]);
  block[6] = UNFIX(tile[6][6]);   block[7] = UNFIX(tile[6][7]);
  block += skip;

  block[0] = UNFIX(tile[7][0]);   block[1] = UNFIX(tile[7][1]);
  block[2] = UNFIX(tile[7][2]);   block[3] = UNFIX(tile[7][3]);
  block[4] = UNFIX(tile[7][4]);   block[5] = UNFIX(tile[7][5]);
  block[6] = UNFIX(tile[7][6]);   block[7] = UNFIX(tile[7][7]);
}

/* end of file gu_splat.c */

/* Log stripped */
