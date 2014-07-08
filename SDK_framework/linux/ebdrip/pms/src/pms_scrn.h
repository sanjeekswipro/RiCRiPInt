/* Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_scrn.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup PMS
 *  \brief Header file containing halftone table definitions
 *
 * This file contains halftone structures to be passed to RIP
 */

#ifndef _PMS_SCRN_H_
#define _PMS_SCRN_H_

/* screen files */
#ifdef SDK_SUPPORT_1BPP_EXT_EG
extern PMS_TyScreenInfo g_tGGScreenTbl_1bpp_Gfx[];
extern PMS_TyScreenInfo g_tGGScreenTbl_1bpp_Image[];
extern PMS_TyScreenInfo g_tGGScreenTbl_1bpp_Text[];
#endif
#ifdef SDK_SUPPORT_2BPP_EXT_EG
extern PMS_TyScreenInfo g_tGGScreenTbl_2bpp_Gfx[];
extern PMS_TyScreenInfo g_tGGScreenTbl_2bpp_Image[];
extern PMS_TyScreenInfo g_tGGScreenTbl_2bpp_Text[];
#endif
#ifdef SDK_SUPPORT_4BPP_EXT_EG
extern PMS_TyScreenInfo g_tGGScreenTbl_4bpp_Gfx[];
extern PMS_TyScreenInfo g_tGGScreenTbl_4bpp_Image[];
extern PMS_TyScreenInfo g_tGGScreenTbl_4bpp_Text[];
#endif

PMS_TyScreenInfo *l_apScreens[] = {
#ifdef SDK_SUPPORT_1BPP_EXT_EG
    /* 1-bpp */
    &g_tGGScreenTbl_1bpp_Gfx[0],
    &g_tGGScreenTbl_1bpp_Gfx[1],
    &g_tGGScreenTbl_1bpp_Gfx[2],
    &g_tGGScreenTbl_1bpp_Gfx[3],
    &g_tGGScreenTbl_1bpp_Image[0],
    &g_tGGScreenTbl_1bpp_Image[1],
    &g_tGGScreenTbl_1bpp_Image[2],
    &g_tGGScreenTbl_1bpp_Image[3],
    &g_tGGScreenTbl_1bpp_Text[0],
    &g_tGGScreenTbl_1bpp_Text[1],
    &g_tGGScreenTbl_1bpp_Text[2],
    &g_tGGScreenTbl_1bpp_Text[3],
#endif
#ifdef SDK_SUPPORT_2BPP_EXT_EG
    /* 2-bpp */
    &g_tGGScreenTbl_2bpp_Gfx[0],
    &g_tGGScreenTbl_2bpp_Gfx[1],
    &g_tGGScreenTbl_2bpp_Gfx[2],
    &g_tGGScreenTbl_2bpp_Gfx[3],
    &g_tGGScreenTbl_2bpp_Image[0],
    &g_tGGScreenTbl_2bpp_Image[1],
    &g_tGGScreenTbl_2bpp_Image[2],
    &g_tGGScreenTbl_2bpp_Image[3],
    &g_tGGScreenTbl_2bpp_Text[0],
    &g_tGGScreenTbl_2bpp_Text[1],
    &g_tGGScreenTbl_2bpp_Text[2],
    &g_tGGScreenTbl_2bpp_Text[3],
#endif
#ifdef SDK_SUPPORT_4BPP_EXT_EG
    /* 4-bpp */
    &g_tGGScreenTbl_4bpp_Gfx[0],
    &g_tGGScreenTbl_4bpp_Gfx[1],
    &g_tGGScreenTbl_4bpp_Gfx[2],
    &g_tGGScreenTbl_4bpp_Gfx[3],
    &g_tGGScreenTbl_4bpp_Image[0],
    &g_tGGScreenTbl_4bpp_Image[1],
    &g_tGGScreenTbl_4bpp_Image[2],
    &g_tGGScreenTbl_4bpp_Image[3],
    &g_tGGScreenTbl_4bpp_Text[0],
    &g_tGGScreenTbl_4bpp_Text[1],
    &g_tGGScreenTbl_4bpp_Text[2],
    &g_tGGScreenTbl_4bpp_Text[3],
#endif
    NULL
};
#define PMS_NUM_SCREENS ((sizeof(l_apScreens) / sizeof(l_apScreens[0]))-1)

#endif /* _PMS_SCRN_H_ */
