/* Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_interface_oil2pms.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for OIL TO PMS Interface.
 *
 */

typedef void (***PMS_API_FNS)(); /**< Array of PMS function pointers. */

/* \brief Initialise and return array of PMS API functions.
 *
 */
extern PMS_API_FNS PMS_InitAPI();

extern int PMS_GetPaperInfo(PMS_ePaperSize ePaperSize, PMS_TyPaperInfo** ppPaperInfo);
extern void FreePMSFramebuffer(void);

