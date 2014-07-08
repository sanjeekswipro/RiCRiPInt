/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2technical.h(EBDSDK_P.1) $
 * $Id: src:hpgl2technical.h,v 1.5.6.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Technical Extentions" category.
 */

#ifndef __HPGL2TECHNICAL_H__
#define __HPGL2TECHNICAL_H__

#include "pcl5context.h"

typedef struct HPGL2TechnicalInfo {
  HPGL2Integer pixel_placement;
} HPGL2TechnicalInfo;

/**
 * Initialise default technical info.
 */
void default_HPGL2_technical_info(HPGL2TechnicalInfo* tech_info);

/**
 * Reset the default values, depending on whether the context is
 * IN or DF.
 */
void hpgl2_set_default_technical_info(HPGL2TechnicalInfo *tech_info,
                                      Bool initialize);

/**
 * Synchronise the gstate and the HPGL technical info.
 */
void hpgl2_sync_technical_info(HPGL2TechnicalInfo *tech_info,
                               Bool initialize);

/**
 * Synchronise the gstate with the HPGL pixel placement.
 */
void hpgl2_sync_pixelplacement(HPGL2TechnicalInfo* tech_info);

/* --- HPGL2 operators --- */

/* (BP, CT, DL, EC, FR, MG, MT, NR, OE, OH, OI, OP, OS, PS, QL, ST, VS are unsupported) */
Bool hpgl2op_MC(PCL5Context *pcl5_ctxt) ;
Bool hpgl2op_PP(PCL5Context *pcl5_ctxt) ;

/* ============================================================================
* Log stripped */
#endif
