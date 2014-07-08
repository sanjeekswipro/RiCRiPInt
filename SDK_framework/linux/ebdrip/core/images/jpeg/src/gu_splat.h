/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!src:gu_splat.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API doing inverse dct of a single coefficient.
 */

#ifndef __GU_SPLAT_H__
#define  __GU_SPLAT_H__

/*--------------------------- prototypes ---------------------------*/

extern void unfix_tile(register int32 *block, int32 skip);
extern void unfix_clip_tile(register int32 *block, int32 skip);
extern void zero_tile(void);
extern void splat_tile(int32 src[8][8]);
extern void splat_00(register int32 value);
extern void splat_01(register int32 value);
extern void splat_02(register int32 value);
extern void splat_03(register int32 value);
extern void splat_04(register int32 value);
extern void splat_05(register int32 value);
extern void splat_06(register int32 value);
extern void splat_07(register int32 value);
extern void splat_10(register int32 value);
extern void splat_11(register int32 value);
extern void splat_12(register int32 value);
extern void splat_13(register int32 value);
extern void splat_14(register int32 value);
extern void splat_15(register int32 value);
extern void splat_16(register int32 value);
extern void splat_17(register int32 value);
extern void splat_20(register int32 value);
extern void splat_21(register int32 value);
extern void splat_22(register int32 value);
extern void splat_23(register int32 value);
extern void splat_24(register int32 value);
extern void splat_25(register int32 value);
extern void splat_26(register int32 value);
extern void splat_27(register int32 value);
extern void splat_30(register int32 value);
extern void splat_31(register int32 value);
extern void splat_32(register int32 value);
extern void splat_33(register int32 value);
extern void splat_34(register int32 value);
extern void splat_35(register int32 value);
extern void splat_36(register int32 value);
extern void splat_37(register int32 value);
extern void splat_40(register int32 value);
extern void splat_41(register int32 value);
extern void splat_42(register int32 value);
extern void splat_43(register int32 value);
extern void splat_44(register int32 value);
extern void splat_45(register int32 value);
extern void splat_46(register int32 value);
extern void splat_47(register int32 value);
extern void splat_50(register int32 value);
extern void splat_51(register int32 value);
extern void splat_52(register int32 value);
extern void splat_53(register int32 value);
extern void splat_54(register int32 value);
extern void splat_55(register int32 value);
extern void splat_56(register int32 value);
extern void splat_57(register int32 value);
extern void splat_60(register int32 value);
extern void splat_61(register int32 value);
extern void splat_62(register int32 value);
extern void splat_63(register int32 value);
extern void splat_64(register int32 value);
extern void splat_65(register int32 value);
extern void splat_66(register int32 value);
extern void splat_67(register int32 value);
extern void splat_70(register int32 value);
extern void splat_71(register int32 value);
extern void splat_72(register int32 value);
extern void splat_73(register int32 value);
extern void splat_74(register int32 value);
extern void splat_75(register int32 value);
extern void splat_76(register int32 value);
extern void splat_77(register int32 value);

#endif

/* Log stripped */
