/* Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */

/*
 * $HopeName: SWsecurity!src:dllsec.c(EBDSDK_P.1) $
 * Interface to a DLL (shared library or code fragment) to provide
 * security for ScriptWorks.
 */

/* ----------------------- Includes ---------------------------------------- */
#include "product.h"
#ifndef NON_RIP_SECURITY  /* Ignore file contents for non-RIP apps */
#include "dllsec.h"
#include "custlib.h"
#include "dongle.h"
#include "ripversn.h"   /* PRODUCT_VERSION_MAJOR */



#ifdef WIN32
#include "custlibp.h"     /* CustomerAlternativeSecurity() */
#include "fxcommon.h"     /* FxWin32Platform() */
#endif
#define HQN
#include "timebomb.h"

#ifdef CORESKIN
#include "fwstring.h" /* UVS */
#include "pdsfns.h"   /* pds_warning */
#define DONGLE_WARNING pds_warning
#else
#ifdef CORERIP
#define UVS( _s_ ) ( (uint8 *)( _s_ ) )
#include "dlliface.h" 
#define DONGLE_WARNING SwDllWarn
#endif /* CORERIP */
#endif /* CORESKIN */

/* ----------------------- Macros ------------------------------------------ */

#define LICENCE_VERSION   2
#define LICENCE_TIMELIMIT 30

/* DayNo from epoch date 1/1/1970, so this number corresponds to 30/09/2014.
 * There are 44 years from the beginning of 1970 to the beginning of 2014, of 
 * which 11 are leap years, plus another (365-92) days.
 */
#define FINAL_EXPIRY_DAY  ((44 * 365) + 11 + 273)

/* WARNING WARNING WARNING WARNING WARNING
 * The logic of LICENCE_SERIAL_NO is duplicated in PluginHostIsWaterMarkedRIP().
 * See SWcommon!src:pluglib.c.
 */
#define LICENCE_SERIAL_NO (0xFF00 | ((RipCustomerNumber()) & 0xFF))

/* Obfuscation defines */
#define GetLicenceData      overwriteSettings
#define CreateLicence       getPosition
#define UpdateLicence       initialiseSettings
#define ValidateLicence     confirmOverwrite
#define GetLicence          savePosition
#define PutLicence          updateWindowTitle

/* ----------------------- Types ------------------------------------------- */

/* ----------------------- Data -------------------------------------------- */

/* ----------------------- Function Prototypes ----------------------------- */


/* ----------------------- Functions --------------------------------------- */

int32 fullTestDLL(int32 * resultArray)
{


  UNUSED_PARAM(int32*, resultArray)


  return FALSE;
}

int32 testDLL(void)
{



  return FALSE;
}

/* Check if the licence is valid, if it isn't, renew it.
 * return TRUE<->actually renewed license
 */
int32 renewDLL(void)
{
  return FALSE; /* fail if not a watermark RIP */
}

/* Returns the number of days remaining, or zero if demonstration has expired
 */
int32 GetDaysRemaining()
{

  return 0;
}

/* Returns boolean TRUE <-> this is a demonstration RIP. 
 * ONLY USE THIS IN COMPOUNDS WHICH ARE NOT CUSTOMISED: 
 * otherwise use #ifdef WATERMARK.
 */
int32 IsWaterMarkRIP()
{
  return FALSE;
}

/* ----------------------- Internal Functions ---------------------------------- */


/* Licence routines */


#endif /* NON_RIP_SECURITY */

/* Log stripped */
