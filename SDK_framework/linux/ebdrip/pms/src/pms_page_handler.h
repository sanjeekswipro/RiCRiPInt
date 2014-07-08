/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_page_handler.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for Page Handler.
 *
 */

#ifndef _PMS_PAGE_HANDLER_H_
#define _PMS_PAGE_HANDLER_H_

#define MAX_PAGE_IN_PAGEBUFFER  3
void AppendToPageQueue(PMS_TyPage *ptPMSPage);
void ShufflePageList();
void PrintPageList();
void PrintPage(PMS_TyPage * pstPageToPrint);
void RemovePage(PMS_TyPage *pstPageToDelete);
int IsEngineIdle();

#endif /* _PMS_PAGE_HANDLER_H_ */
