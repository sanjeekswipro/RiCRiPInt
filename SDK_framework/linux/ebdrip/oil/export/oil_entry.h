/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!export:oil_entry.h(EBDSDK_P.1) $
 *
 */

/*! \mainpage
 *  \ingroup OIL
 *  \brief OIL
 
  <b>Overview of OIL (OEM Interface Layer)</b>

  The OIL provides an interface between the RIP and the printer
  dependent PMS (Print Management System).

  Entry points are defined by the PMS. This OIL implementation supports
  the following functions:\n
  OIL_Init()\n
  OIL_Start()\n
  OIL_StartRIP()\n
  OIL_StopRIP()\n
  OIL_Exit()\n
  OIL_PageDone()\n
  OIL_JobCancel()\n
  OIL_GetBandData()\n
  OIL_ProbeLogFlush()\n

  Memory must be allocated and freed using the OIL-specific memory management
  functions @c OIL_malloc() and @c OIL_free().  These functions form an
  abstraction to the system functions @c malloc() and @c free(), and also
  support the use of different memory pools.\n
  Care must be taken to ensure that memory is released back to the same
  pool it was allocated from.

  All functions within the OIL project that are prefixed with OIL_ are external interfaces to OIL.

  <b>How to use this document</b>

  You should refer to the File List section for an overview of the
  library file (oil_entry.h) for the OIL interface.  You should then
  refer to the Data Structures section for an overview of
  the associated data structures.

  Note: This document is designed for online viewing, with lots of
  hyperlinks (blue text) allowing you to quickly jump to related
  information. We do not recommend that you print this document.
 */

/*! \file
 *  \ingroup OIL
 *  \brief This file defines the OIL API.
 *
 */

#ifndef _OIL_ENTRY_H_
#define _OIL_ENTRY_H_
extern char    *OILVersion;
extern char    *RIPVersion;

int  OIL_Init_Globals(void);
int  OIL_Init(void (**apfn_funcs[])());
int   OIL_Start(PMS_TyJob *pms_ptJob, int * pbSubmittedJob);
int  OIL_StartRIP();
void  OIL_StopRIP(int bForcedShutdown);
void  OIL_Exit(void);
void   OIL_PageDone(PMS_TyPage *ptPMSPage);
int   OIL_JobDone(void);
void  OIL_JobCancel(void);
int OIL_GetBandData(unsigned int uJobID, unsigned int uPageID, int nColorant, int nBandNo, PMS_TyBand *tBand);
void OIL_ProbeLogFlush(void) ;


#endif /* _OIL_ENTRY_H_ */
