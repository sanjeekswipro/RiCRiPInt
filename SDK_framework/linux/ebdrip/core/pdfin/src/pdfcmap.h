/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfcmap.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Character map (CMAP) API
 */

#ifndef __PDFCMAP_H__
#define __PDFCMAP_H__


/* pdfcmap.h */

/* ----- External constants ----- */

enum { CMAP_None, CMAP_Embedded,
       CMAP_GB_EUC_H, CMAP_GB_EUC_V,
       CMAP_GBpc_EUC_H, CMAP_GBpc_EUC_V,
       CMAP_GBK_EUC_H, CMAP_GBK_EUC_V,
       CMAP_GBKp_EUC_H, CMAP_GBKp_EUC_V,
       CMAP_GBK2K_H, CMAP_GBK2K_V,
       CMAP_UniGB_UCS2_H, CMAP_UniGB_UCS2_V,
       CMAP_UniGB_UTF16_H, CMAP_UniGB_UTF16_V,
       CMAP_B5pc_H, CMAP_B5pc_V,
       CMAP_HKscs_B5_H, CMAP_HKscs_B5_V,
       CMAP_ETen_B5_H, CMAP_ETen_B5_V,
       CMAP_ETenms_B5_H, CMAP_ETenms_B5_V,
       CMAP_CNS_EUC_H, CMAP_CNS_EUC_V,
       CMAP_UniCNS_UCS2_H, CMAP_UniCNS_UCS2_V,
       CMAP_UniCNS_UTF16_H, CMAP_UniCNS_UTF16_V,
       CMAP_83pv_RKSJ_H,
       CMAP_90ms_RKSJ_H, CMAP_90ms_RKSJ_V,
       CMAP_90msp_RKSJ_H, CMAP_90msp_RKSJ_V,
       CMAP_90pv_RKSJ_H,
       CMAP_Add_RKSJ_H, CMAP_Add_RKSJ_V,
       CMAP_EUC_H, CMAP_EUC_V,
       CMAP_Ext_RKSJ_H, CMAP_Ext_RKSJ_V,
       CMAP_H, CMAP_V,
       CMAP_Hojo_EUC_H, CMAP_Hojo_EUC_V, /* PDF 1.2; removed from PDF 1.4 */
       CMAP_UniJIS_UCS2_H, CMAP_UniJIS_UCS2_V,
       CMAP_UniJIS_UCS2_HW_H, CMAP_UniJIS_UCS2_HW_V,
       CMAP_UniJIS_UTF16_H, CMAP_UniJIS_UTF16_V,
       CMAP_KSC_EUC_H, CMAP_KSC_EUC_V,
       CMAP_KSCms_UHC_H, CMAP_KSCms_UHC_V,
       CMAP_KSCms_UHC_HW_H, CMAP_KSCms_UHC_HW_V,
       CMAP_KSCpc_EUC_H,
       CMAP_KSC_Johab_H, CMAP_KSC_Johab_V, /* PDF 1.2, removed from PDF 1.4 */
       CMAP_UniKS_UCS2_H, CMAP_UniKS_UCS2_V,
       CMAP_UniKS_UTF16H, CMAP_UniKS_UTF16V,
       CMAP_Identity_H, CMAP_Identity_V
     } ;

/* ----- External structures ----- */

typedef struct pdf_cmapdetails PDF_CMAPDETAILS;

struct pdf_cmapdetails {
  int32  cmap_type ; /* Cmap encoding Type */
  OBJECT cmap_dict ;
  int32  wmode ;
} ;


extern int32 pdf_setcmapdetails( PDFCONTEXT *pdfc,
				 PDF_CMAPDETAILS *pdf_cmapdetails,
				 OBJECT *cmap) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
