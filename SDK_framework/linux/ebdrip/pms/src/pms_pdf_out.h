/* Copyright (C) 2005-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_pdf_out.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup PMS
 *  \brief Header file for PDF Output Handler.
 *
 */

#ifndef _PMS_PDF_OUT_H_
#define _PMS_PDF_OUT_H_

enum {
  PDF_NoError,
  PDF_Error_Memory,
  PDF_Error_FileOpen,
  PDF_Error_FileIO,
};
typedef int PMS_ePDF_Errors;

int PDF_PageHandler( PMS_TyPage *ptPMSPage );

#endif /* _PMS_PDF_OUT_H_ */
