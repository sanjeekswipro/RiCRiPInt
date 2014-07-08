/** \file
 *
 * $HopeName: SWle-security!src:lesec.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

/* ----------------------------- Includes ---------------------------------- */

#include "lesec.h"

#include "lesecint.h"

#include "dongle.h"
#include "security.h"
#include "timebomb.h"

#include "genkey.h"
#include "swstart.h"  /* SwExit */
#include "hqmd5.h"    /* MD5 */
#include "lesecgen.h" /* lesec_decode */

#include "mpscmvff.h"

#include <stdio.h>


/* ----------------------------- Types ------------------------------------- */

typedef struct InitData
{
  uint32 oemID;
  uint32 securityNo;
  uint32 resLimit;
  uint32 productVersion;
  uint32 platform;

  uint32  cbHandshakeLen;
  uint8 * pHandshake;

} InitData;


/* ---------------------------- Defines ------------------------------------ */

#define FULL_TEST_HANDSHAKE_LEN (MAX_HANDSHAKE_LEN)
#define TEST_HANDSHAKE_LEN      ((FULL_TEST_HANDSHAKE_LEN) - 24)

/* To emulate EPDR-like behaviour using mvff */
#define EPDR_LIKE ( mps_bool_t )1, ( mps_bool_t )1, ( mps_bool_t )1


/* ------------------------------ Data ---------------------------------- */

static InitData gInitData = { 0 };

static uint32 gPasswords[ GNKEY_MAX ] = { 0 };

static SwSecHandshakeFn * handshakeFn = NULL;


/* ------------------------ Forward function declarations ------------------ */

static int32 doHandshake( SwSecHandshakeStruct * pHandshake );


/* ---------------------------- Exported functions ------------------------- */

void RIPCALL SwSecInit( SwSecInitStruct * pParam, mps_arena_t arena )
{
  mps_pool_t lesec_pool = NULL;
  mps_res_t  res;

  res = mps_pool_create( &lesec_pool, arena, mps_class_mvff(),
                          (size_t)1024, (size_t)32, (size_t)8, EPDR_LIKE );

  if( res == MPS_RES_OK )
  {
    /* Decode pParam->pData */
    mps_addr_t p;
#ifdef MM_DEBUG_MPSTAG
    mps_word_t label = mps_telemetry_intern("LE security pool");
    mps_telemetry_label((mps_addr_t)lesec_pool, label);
#endif
    res = mps_alloc( &p, lesec_pool, pParam->dataLen );

    if( res == MPS_RES_OK )
    {
      uint8    * pDataCopy;
      uint32     i;
      uint32     nPasswords;
      LeSecPassword * pPasswords;

      pDataCopy = (uint8 *) p;
      lesecDecode( pParam->dataLen, pParam->pData, pDataCopy );

      if( lesecValidateData( pParam->oemID, pParam->dataLen, pDataCopy ) )
      {
        handshakeFn = pParam->handshakeFn;

        gInitData.oemID = getOemID( pDataCopy );
        gInitData.securityNo = getSecurityNo( pDataCopy );
        gInitData.resLimit = getResLimit( pDataCopy );
        gInitData.productVersion = getProductVersion( pDataCopy );
        gInitData.platform = getPlatform( pDataCopy );

        gInitData.pHandshake = copyHandshake( pDataCopy, &gInitData.cbHandshakeLen );

        pPasswords = copyPasswords( pDataCopy, &nPasswords );

        if( pPasswords != NULL )
        {
          for( i = 0; i < nPasswords; i++ )
          {
            gPasswords[ pPasswords[ i ].id ] = pPasswords[ i ].value;
          }

          freePasswords( pPasswords );
        }
      }

      mps_free( lesec_pool, p, pParam->dataLen );
    }

    mps_pool_destroy( lesec_pool );
  }
}

void RIPCALL SwSecShutdown( void )
{
  if( gInitData.pHandshake )
  {
    freeHandshake( gInitData.pHandshake );
    gInitData.pHandshake = NULL;
  }
}

void RIPCALL SwSecGetConfig( uint8 * buff, int32 len )
{
  char * ptr = (char *) buff;

  UNUSED_PARAM( int32, len );

  ptr += sprintf( ptr, "<< " );

  if( gPasswords[ GNKEY_FEATURE_HDS ] != 0 )
  {
    ptr += sprintf( ptr, "/HDS %d ", gPasswords[ GNKEY_FEATURE_HDS ] );
  }

  if( gPasswords[ GNKEY_FEATURE_HDSLOWRES ] != 0 )
  {
    ptr += sprintf( ptr, "/HDSLOWRES %d ", gPasswords[ GNKEY_FEATURE_HDSLOWRES ] );
  }

  if( gPasswords[ GNKEY_FEATURE_HMS ] != 0 )
  {
    ptr += sprintf( ptr, "/HMS %d ", gPasswords[ GNKEY_FEATURE_HMS ] );
  }

  if( gPasswords[ GNKEY_FEATURE_HCS ] != 0 )
  {
    ptr += sprintf( ptr, "/HCS %d ", gPasswords[ GNKEY_FEATURE_HCS ] );
  }

  if( gPasswords[ GNKEY_FEATURE_HPS ] != 0 )
  {
    ptr += sprintf( ptr, "/HPS %d ", gPasswords[ GNKEY_FEATURE_HPS ] );
  }

  if( gPasswords[ GNKEY_FEATURE_HXM ] != 0 )
  {
    ptr += sprintf( ptr, "/HXM %d ", gPasswords[ GNKEY_FEATURE_HXM ] );
  }

  if( gPasswords[ GNKEY_FEATURE_HXMLOWRES ] != 0 )
  {
    ptr += sprintf( ptr, "/HXMLOWRES %d ", gPasswords[ GNKEY_FEATURE_HXMLOWRES ] );
  }

  if( gPasswords[ GNKEY_FEATURE_IDLOM ] != 0 )
  {
    ptr += sprintf( ptr, "/DLMS %d ", gPasswords[ GNKEY_FEATURE_IDLOM ] );
  }

  if( gPasswords[ GNKEY_FEATURE_TRAP_PRO ] != 0 )
  {
    ptr += sprintf( ptr, "/TrapPro %d ", gPasswords[ GNKEY_FEATURE_TRAP_PRO ] );
  }

  if( gPasswords[ GNKEY_FEATURE_TIFF_IT ] != 0 )
  {
    ptr += sprintf( ptr, "/TIFFIT %d ", gPasswords[ GNKEY_FEATURE_TIFF_IT ] );
  }

  if( gPasswords[ GNKEY_FEATURE_ICC ] != 0 )
  {
    ptr += sprintf( ptr, "/ICC %d ", gPasswords[ GNKEY_FEATURE_ICC ] );
  }

  if( gPasswords[ GNKEY_FEATURE_2THREAD ] != 0 )
  {
    ptr += sprintf( ptr, "/TwoThreads %d ", gPasswords[ GNKEY_FEATURE_2THREAD ] );
  }

  if( gPasswords[ GNKEY_FEATURE_SIMPLE_IMPOSITION ] != 0 )
  {
    ptr += sprintf( ptr, "/SimpleImposition %d ", gPasswords[ GNKEY_FEATURE_SIMPLE_IMPOSITION ] );
  }

  if( gPasswords[ GNKEY_FEATURE_POSTSCRIPT ] != 0 )
  {
    ptr += sprintf( ptr, "/PostScriptPassword %d ", gPasswords[ GNKEY_FEATURE_POSTSCRIPT ] );
  }

  if( gPasswords[ GNKEY_FEATURE_PDF ] != 0 )
  {
    ptr += sprintf( ptr, "/PDFPassword %d ", gPasswords[ GNKEY_FEATURE_PDF ] );
  }

  if( gPasswords[ GNKEY_FEATURE_XPS ] != 0 )
  {
    ptr += sprintf( ptr, "/XPSPassword %d ", gPasswords[ GNKEY_FEATURE_XPS ] );
  }

  if( gPasswords[ GNKEY_FEATURE_APPLY_WATERMARK ] != 0 )
  {
    ptr += sprintf( ptr, "/ApplyWatermarkPassword %d ", gPasswords[ GNKEY_FEATURE_APPLY_WATERMARK ] );
  }

  if( gPasswords[ GNKEY_FEATURE_MTC ] != 0 )
  {
    HQFAIL("MTC is obsolete");
  }

  if( gPasswords[ GNKEY_FEATURE_MAX_THREADS_LIMIT ] != 0 )
  {
    ptr += sprintf( ptr, "/MaxThreadsLimit %d ", gPasswords[ GNKEY_FEATURE_MAX_THREADS_LIMIT ] );
  }

  if( gPasswords[ GNKEY_FEATURE_PIPELINING ] != 0 )
  {
    ptr += sprintf( ptr, "/Pipelining %d ", gPasswords[ GNKEY_FEATURE_PIPELINING ] );
  }

  if( gPasswords[ GNKEY_FEATURE_HVD_EXTERNAL ] != 0 )
  {
    ptr += sprintf( ptr, "/HVDExternal %d ", gPasswords[ GNKEY_FEATURE_HVD_EXTERNAL ] );
  }

  if( gPasswords[ GNKEY_FEATURE_HVD_INTERNAL ] != 0 )
  {
    ptr += sprintf( ptr, "/HVDInternal %d ", gPasswords[ GNKEY_FEATURE_HVD_INTERNAL ] );
  }

  /* Always need to set these, even if they are zero */
  ptr += sprintf( ptr, "/RevisionPassword %d ", gPasswords[ GNKEY_REVISION_PASSWORD ] );
  ptr += sprintf( ptr, "/PlatformPassword %d ", gPasswords[ GNKEY_PLATFORM_PASSWORD ] );

  ptr += sprintf( ptr, "/Password 0 >> setsystemparams " );
}

int32 fullTestLeSec( int32 * resultArray )
{
  uint8 handshakeTx[ FULL_TEST_HANDSHAKE_LEN ];
  uint8 handshakeRx[ MD5_DIGEST_LENGTH ];
  SwSecHandshakeStruct handshake;

  handshake.nLength = sizeof( handshakeTx );
  handshake.pIn = handshakeTx;
  handshake.pOut = handshakeRx;

  if( ! doHandshake( &handshake ) )
  {
    return FALSE;
  }

  DONGLE_SET_PROTECTED_PLUGINS( resultArray, 0 );
  DONGLE_SET_GUI_DENIED( resultArray, 1 );
  DONGLE_SET_WATERMARK( resultArray, 0 );
  DONGLE_SET_USE_TIMER( resultArray, 0 );
  DONGLE_SET_BREAK_OLD_RIPS( resultArray, 1 );
  DONGLE_SET_PRODUCT_CODE_USES_BITS( resultArray, 0 );
  DONGLE_SET_LLV( resultArray, 0 );

  DONGLE_SET_PRODUCT_CODE( resultArray, gInitData.platform );

  DONGLE_SET_RES_INFO( resultArray, gInitData.resLimit );

  DONGLE_SET_RIP_VERSION( resultArray, gInitData.productVersion );
  DONGLE_SET_FEATURE_LEVEL( resultArray, FULL_RIP );

  DONGLE_SET_CUSTOMER_NO( resultArray, gInitData.oemID );
  DONGLE_SET_DEMO_FLAG( resultArray, 0 );

  DONGLE_SET_SECURITY_NO( resultArray, gInitData.securityNo );

  DONGLE_SET_TEAMWORK( resultArray, 1 );

  /* Disable all PDLs.  One or more will be enabled by passwords. */
  DONGLE_SET_POSTSCRIPT_DENIED( resultArray, 1 );
  DONGLE_SET_PDF_DENIED( resultArray, 1 );
  DONGLE_SET_HPS_DENIED( resultArray, 1 );
  DONGLE_SET_XPS_DENIED( resultArray, 1 );
  DONGLE_SET_APPLY_WATERMARK( resultArray, 1 );

  return TRUE;
}

int32 testLeSec( void )
{
  uint8 handshakeTx[ TEST_HANDSHAKE_LEN ];
  uint8 handshakeRx[ MD5_DIGEST_LENGTH ];
  SwSecHandshakeStruct handshake;

  handshake.nLength = sizeof( handshakeTx );
  handshake.pIn = handshakeTx;
  handshake.pOut = handshakeRx;

  return doHandshake( &handshake );
}

void reportLeSecError( void )
{
  printf( "Fatal security device failure\n" );
}


/* ------------------------- Internal  functions  -------------------------- */

static int32 doHandshake( SwSecHandshakeStruct * pHandshake )
{
  uint32 i;
  uint8  scratch[ MAX_HANDSHAKE_LEN ];
  uint8  md5out[ MD5_DIGEST_LENGTH ];
  uint8  r = 23;

  HQASSERT(pHandshake->nLength <= sizeof(scratch), "scratch buffer too small");

  if( handshakeFn == NULL || gInitData.pHandshake == NULL )
  {
    return FALSE;
  }

  fillBuffer( pHandshake->pIn, pHandshake->nLength );

  for( i = 0; i < pHandshake->nLength; i++ )
  {
    scratch[ i ] = pHandshake->pIn[ i ] ^ gInitData.pHandshake[ i %  gInitData.cbHandshakeLen ];
  }

  MD5( scratch, pHandshake->nLength, md5out );

  (*handshakeFn)( pHandshake );

  for( i = 0; i < MD5_DIGEST_LENGTH; i++ )
  {
    if( ( r = md5out[ i ] ^ pHandshake->pOut[ i ] ) != 0 )
    {
      break;
    }
  }

  /* r == 0 if handshake is valid */
  return r == 0;
}


/*
* Log stripped */

/* end of lesec.c */
