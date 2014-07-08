/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:gs_colorprivt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color chain private data
 */

#ifndef __GS_COLORPRIVT_H__
#define __GS_COLORPRIVT_H__


/* Types for the data shared (possibly) by several links in the color
   chains set up the gstate */
typedef struct GS_COLORinfoList         GS_COLORinfoList;
typedef struct GS_CHAINinfo             GS_CHAINinfo;
typedef struct GS_CHAIN_CONTEXT         GS_CHAIN_CONTEXT;
typedef struct GS_CONSTRUCT_CONTEXT     GS_CONSTRUCT_CONTEXT;
typedef struct GS_CHAIN_CACHE           GS_CHAIN_CACHE;
typedef struct GS_CHAIN_CACHE_LIST      GS_CHAIN_CACHE_LIST;
typedef struct GS_CHAIN_CACHE_INFO      GS_CHAIN_CACHE_INFO;

typedef struct GS_CRDinfo               GS_CRDinfo;
typedef struct GS_RGBtoCMYKinfo         GS_RGBtoCMYKinfo;
typedef struct GS_TRANSFERinfo          GS_TRANSFERinfo;
typedef struct GS_CALIBRATIONinfo       GS_CALIBRATIONinfo;
typedef struct GS_HALFTONEinfo          GS_HALFTONEinfo;
typedef struct GS_HCMSinfo              GS_HCMSinfo;
typedef struct GS_DEVICECODEinfo        GS_DEVICECODEinfo;
typedef struct GS_BLOCKoverprint        GS_BLOCKoverprint;

typedef struct CLINK                    CLINK;
typedef struct CLINKblock               CLINKblock;
typedef union  CLINKprivate             CLINKprivate;
typedef struct CLINKcietableabcd        CLINKcietablea;
typedef struct CLINKcietableabcd        CLINKcietableabc;
typedef struct CLINKcietableabcd        CLINKcietableabcd;
typedef struct CLINKciebasedabc         CLINKciebaseda;
typedef struct CLINKciebasedabc         CLINKciebasedabc;
typedef struct CLINKciebaseddef         CLINKciebaseddef;
typedef struct CLINKciebaseddefg        CLINKciebaseddefg;
typedef struct CLINKlab                 CLINKlab;
typedef struct CLINKcalrgbg             CLINKcalrgbg;
typedef struct CLINKiccbased            CLINKiccbased;
typedef struct CLINKindexed             CLINKindexed;
typedef struct CLINKtinttransform       CLINKtinttransform;
typedef struct CLINKintercepttransform  CLINKintercepttransform;
typedef struct CLINKallseptinttransform CLINKallseptinttransform;
typedef struct CLINKcrd                 CLINKcrd;
typedef struct CLINKCMYKtoGrayinfo      CLINKCMYKtoGrayinfo;
typedef struct CLINKRGBtoCMYKinfo       CLINKRGBtoCMYKinfo;
typedef struct CLINKRGBtoGrayinfo       CLINKRGBtoGrayinfo;
typedef struct CLINKDEVICECODEinfo      CLINKDEVICECODEinfo;
typedef struct CLINKNONINTERCEPTinfo    CLINKNONINTERCEPTinfo;
typedef struct CLINKTRANSFERinfo        CLINKTRANSFERinfo;
typedef struct CLINKCALIBRATIONinfo     CLINKCALIBRATIONinfo;
typedef struct CLINKPRESEPARATIONinfo   CLINKPRESEPARATIONinfo;
typedef struct CLINKLUMINOSITYinfo      CLINKLUMINOSITYinfo;
typedef struct CLINKinterceptdevicen    CLINKinterceptdevicen;
typedef struct CLINKcmmxform            CLINKcmmxform;
typedef struct CLINKalternatecmm        CLINKalternatecmm;
typedef struct CLINKcustomcmm           CLINKcustomcmm;
typedef struct CLINKneutralmapping      CLINKneutralmapping;
typedef struct CLINKblackevaluate       CLINKblackevaluate;
typedef struct CLINKblackremove         CLINKblackremove;
typedef struct CLINKblackreplace        CLINKblackreplace;

typedef struct TRANSFORM_LINK_INFO      TRANSFORM_LINK_INFO;
typedef struct ICC_PROFILE_INFO_CACHE   ICC_PROFILE_INFO_CACHE;
typedef struct ICC_PROFILE_INFO         ICC_PROFILE_INFO;
typedef struct CUSTOM_CMM_INFO          CUSTOM_CMM_INFO;
typedef struct HQN_PROFILE_INFO         HQN_PROFILE_INFO;

typedef struct GSTATE_MIRROR            GSTATE_MIRROR;

typedef struct BLACK_PRESERVE           BLACK_PRESERVE;



/* Log stripped */

#endif /* __GS_COLORPRIVT_H__ */
