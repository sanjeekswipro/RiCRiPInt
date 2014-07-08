/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2dispatch.h(EBDSDK_P.1) $
 * $Id: src:hpgl2dispatch.h,v 1.8.4.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Dispatch functions for HPGL2.
 */

#ifndef __HPGL2DISPATCH_H__
#define __HPGL2DISPATCH_H__

#include "pcl5context.h"

typedef struct HPGL2FunctTable HPGL2FunctTable;

Bool hpgl2_funct_table_create(HPGL2FunctTable **table) ;

void hpgl2_funct_table_destroy(HPGL2FunctTable **table) ;

Bool register_hpgl2_ops(HPGL2FunctTable *table) ;

typedef struct HPGL2FunctEntry HPGL2FunctEntry;

Bool hpgl2_dispatch_op(PCL5Context *pcl5_ctxt, HPGL2FunctEntry* op);

HPGL2FunctEntry* hpgl2_get_op(PCL5Context* pcl5_ctxt, uint8 op_name_ch1, uint8 op_name_ch2);

/* ============================================================================
* Log stripped */
#endif
