/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_page_handler.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief OIL Job Handler header
 *
 * This file declares the interface to the OIL's page handling functionaliy.
 */

#ifndef _OIL_PAGE_HANDLER_H_
#define _OIL_PAGE_HANDLER_H_

/* page handler interface */
extern OIL_TyPage* CreateOILPage(struct rasterDescription * ptRasterDescription);
extern void ProcessPageDone(unsigned int JobID, unsigned int PageID);
extern void DeleteOILPage(OIL_TyPage *ptOILPage);
extern OIL_TyPage* CreateOILBlankPage(OIL_TyPage *ptOILPage, struct rasterDescription *pstRasterDescription);
extern void CreateOILBlankPlane(OIL_TyPage *ptOILPage, int colorant, OIL_TyPlane *ptOILSamplePlane);
PMS_TyBandPacket *CreateBandPacket(int nColorants, int nColorFamilyOffset, int nSeparations, int nReqBytesPerLine, int nReqLinesPerBand, short Map[]);

extern void CreateConfigTestPage(unsigned char *sz);
extern void CreatePSTestPage(unsigned char *sz);
extern void CreatePCLTestPage(unsigned char *sz);
extern void CreateErrorPage(unsigned char *sz);
#ifdef USE_FF
extern void CreatePCLTestPage(unsigned char *sz);
#endif
#ifdef USE_UFST
extern void CreatePCLTestPage(unsigned char *sz);
#endif

#endif /* _OIL_PAGE_HANDLER_H_ */
