/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gs_table.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Tom's Table API
 */

#ifndef __GS_TABLE_H__
#define __GS_TABLE_H__


typedef struct GSC_TOMSTABLES GSC_TOMSTABLES;

typedef struct gsc_table GSC_TABLE;


/* Returns a scaling factor to be applied to images data by the client before
 * passing it to Tom's Tables. The tables work in integer maths, so the scaling
 * allows an efficient way of deriving an index into the mini-cube within the
 * tables and a fractional component.
 */
int32 gsc_scaledColor(DL_STATE *page);

#endif /* __GS_TABLE_H__ */

/* Log stripped */
