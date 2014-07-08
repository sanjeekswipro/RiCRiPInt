/* Copyright (C) 2007-2010 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: SWsecurity!src:tbub.c(EBDSDK_P.1) $
 * Try-before-you-buy checking functionality
 *
* Log stripped */

#ifdef CORESKIN
/* Only available if CORESKIN defined */

#include "tbub.h"     /* includes product.h ... */


/* ------------------- Public interface ------------------ */

void startTBUB( void )
{
}

void endTBUB(void)
{
}

/* ------------------- Private ------------------------- */


int32 IsTryBeforeYouBuyEnabled( void )
{
  return FALSE;
}

int32 IsTryBeforeYouBuyPossible( void )
{
  return FALSE;
}

int32 localLicenceServerRunning( void )
{
  return FALSE;
}

int32 resetLocalLicenceServer( void )
{
  return FALSE;
}

int32 installTBYB( FwTextString ptbzPermitFile )
{
  /* Do nothing */
  UNUSED_PARAM( FwTextString, ptbzPermitFile );
  return FALSE;
}

int32 getTbubUniqueID( uint32 * pid )
{
  UNUSED_PARAM( uint32 *, pid );
  return FALSE;
}

int32 getTbubDaysToRun( void )
{
  int32    nDays = -1;

  return nDays;
}

int32 tbyb_license_expiring( int32 * pfExpiring, uint32 * pDaysLeft )
{
  int32 fSuccess = FALSE;

  UNUSED_PARAM( int32 *, pfExpiring );
  UNUSED_PARAM( uint32 *, pDaysLeft );

  return fSuccess;
}

#endif /* CORESKIN */

char tbub_stopwarn;
