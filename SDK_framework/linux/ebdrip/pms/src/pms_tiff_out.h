/* Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_tiff_out.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for TIFF Output Handler.
 *
 */

#ifndef _PMS_TIFF_OUT_H_
#define _PMS_TIFF_OUT_H_

enum {
  TIFF_NoError,
  TIFF_Error_bppNotSupported,
  TIFF_Error_GGETiff,
  TIFF_Error_Memory,
  TIFF_Error_FileOpen,
  TIFF_Error_FileIO,
};
typedef int PMS_eTIFF_Errors;

int TIFF_PageHandler( PMS_TyPage *ptPMSPage );

#endif /* _PMS_TIFF_OUT_H_ */
