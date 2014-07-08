/* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: SWsecurity!src:dongle.c(EBDSDK_P.1) $

Log stripped */
/* dongle.c */

/*  code for all dongles.  Here are indirect calls, enabling
  the placement of other checks in with them, or distraction code  */

#include "product.h"  /* includes std.h ... */

#ifdef CORESKIN
#include "cfileutl.h"   /* PlatformSWDir */
#include "fwevent.h"    /* FwEventExecuteOnBootThread */
#include "skinmain.h"   /* coreskinexiterrorc */
#include "skinpfns.h"   /* gui_tickcount */
#include "ptask.h"      /* PeriodicTaskCreate, seconds2period, etc. */
#else
#ifdef CORERIP
#include "swstart.h"    /* SwExit */
#else
#include "swexit.h"     /* SwExit */
#endif
#endif

#if defined(LDK) && defined(HQNLIC)
#include "fwconfig.h"   /* FwConfigReadFile */
#include "fwmem.h"      /* FwMemFree */
#include "fwvector.h"   /* FwVectorOpen */
#endif

#ifndef NON_RIP_SECURITY
#include "custlib.h"    /* Header files unneeded in non-rip application */
#endif /* NON_RIP_SECURITY */

#include "fwstring.h"   /* FwStrRecord */
#ifdef NON_RIP_SECURITY
#include "fwtransl.h"   /* FwTranslStripMarkers */
#endif /* NON_RIP_SECURITY */

#include "mphardng.h"
#include "obfusc.h"
#include "proto.h"
#include "swoften.h"
#include "swenv.h"    /* get_rtime */

#include "checksum.h"
#include "genkey.h"
#include "security.h"
#include "dongle.h"

#include "extscrty.h"
#include "dllsec.h"
#include "swexit.h"

#ifdef CORESKIN
#ifdef PRODUCT_HAS_API
#include "riporb.h"   /* riporbNotifyRIPIsReady */
#endif
#endif


/*
 * Security device headers
 */

#if defined (HQNLIC) || defined(LDK)

/* Licence server and/or LDK variants */


/* Undefine all the possible dongle defines */
#undef EVE_DONGLE
#undef MACTIVATOR
#undef TIMEBOMB
#undef CPLUS
#undef SENTINEL
#undef MICROPHAR

/* Undefine DLL security define */
#undef DLLSEC

#else /* HQNLIC */

#ifdef LESEC

/* LE security variant */
#include "lesecint.h"

/* Undefine all the possible dongle defines */
#undef EVE_DONGLE
#undef MACTIVATOR
#undef TIMEBOMB
#undef CPLUS
#undef SENTINEL
#undef MICROPHAR

/* Undefine DLL security define */
#undef DLLSEC

#else /* LESEC */

/* Dongle or watermark variant */
#ifdef EVE_DONGLE
#include "eve3dong.h"
#endif
#include "rnbwdong.h"

/* Define DLL security define, unless NON_RIP_SECURITY */
#ifdef NON_RIP_SECURITY
#undef DLLSEC
#else
#define DLLSEC 1
#endif

#endif /* LESEC */
#endif /* HQNLIC */

#ifdef ACTIVATOR_M
#  error ACTIVATOR_M symbol is obsolete. Activator no longer supported.
#endif

/* Dongle Defines - used to be in header file, but only accessed here anyway */

#define noSecurityDevice              0
#define eveSecurityDevice             1
#define mactivatorSecurityDevice      2
#define timebombSecurityDevice        4
#define superproSecurityDevice        5
#define micropharSecurityDevice       6
#define cplusSecurityDevice           7
#define eve3SecurityDevice            8
#define dllSecurityDevice             9
#define licSecurityDevice            10
#define leSecurityDevice             11
#define ldkSecurityDevice            12

static int32 securityDevice = noSecurityDevice;
static int32 securityDeviceUsed = noSecurityDevice;
static int32 corestart = TRUE;
static int32 fGUIDenied = FALSE;

#if defined(LDK) && defined(HQNLIC)
static int32 preferredDevice = noSecurityDevice;
#endif

#ifdef ASSERT_BUILD
static int32 fDoingStartupTest = FALSE;
#endif

static int32 resultArray[ RESULT_ARRAY_SIZE ] = { 0 } ;

/* Macro for clearing this array (prior to asking the
 * dongle to fill it in).
 */
#define ZERO_RESULTARRAY( _array_ ) MACRO_START \
  int32 i ; \
  for( i = 0; i < RESULT_ARRAY_SIZE; ++i ) { \
    _array_[i] = 0; \
  } \
MACRO_END

/* Permit types */
enum
{
  LSS_TYPE_NORMAL              = 0,   /* Normal permit */
  LSS_TYPE_RIP_DEMO            = 1,   /* RIP demo permit, for use with a demo dongle */
  LSS_TYPE_RIP_HIRE_PURCHASE   = 2,   /* RIP hire purchase permit, for when the customer pays in installments */
};

#ifdef CORESKIN

/* Data for pTask to check time-expiry dongles. */
static PeriodicTaskCallback task_CheckTimeExpiryDongle;
static PeriodicTask * pTaskCheckTimeExpiryDongle = NULL;
static int32          taskSemaCheckTimeExpiryDongle = 0;


static int32 task_CheckTimeExpiryDongle( void )
{
  /*
   * Consequences of counter reaching 0 are not considered here.  That
   * code is separate, in the periodic dongle presence check.
   */
  decrementTimeExpiringDongle();

  return FALSE;
}

#endif /* CORESKIN */

/*
 * Security device prototypes
 */

#ifdef SENTINEL
#ifdef MACOSX
static uint32 macSuperProMinRipVersion( void );
#endif
#endif

#ifdef EVE_DONGLE
#ifndef MACOSX
extern fullTestEveDonglex3(int32 *resultArray);
extern testEveDonglex3();
extern reportEveDongleError();
#endif
#endif

#ifdef TIMEBOMB
extern int32 fullTestTimebomb ( int32*  resultArray );
extern int32 testTimebomb( void );
extern void reportTimebombError ( void );
#endif


/* Having SECURITY_DEVICE, SECURITY_DEVICE2, 3, 4... is a bit
 * inflexible, and leads to a large number of #ifdefs.  Instead, have an
 *  array of the devices we are going to test. */
typedef struct SecurityDevice {
  int     securityDevice;     /* See "Dongle Defines" above */
  char *  deviceType;
  int32  (*fullTest)(int32*);
  int32  (*test)(void);
  void   (*end)(void);         /* May be NULL */
  int32  (*isTimed)(void);     /* May be NULL */
  void   (*decTimer)(void);    /* May be NULL */
  int32  (*serialNo)(uint32 *);    /* May be NULL */
  int32  (*hasDate)(int32 *);      /* May be NULL */
  int32  (*getDate)(uint32 *);     /* May be NULL */
  int32  (*setDate)(uint32);       /* May be NULL */
  int32  (*llvLocales)(uint32 *);  /* May be NULL */
  uint32 (*minRipVersion)(void);   /* May be NULL */
#ifdef CORESKIN
  HqBool (*secDevId)(FwStrRecord *);    /* May be NULL */
#endif
} SecurityDevice;


/* Current devices */
static SecurityDevice devices[] = {
#ifdef DLLSEC
  { dllSecurityDevice, "DLL", fullTestDLL, testDLL, 0, 0, 0, 0 },
#endif
#ifdef SENTINEL
  { superproSecurityDevice, "SuperPro", fullTestSuperpro, testSuperpro, endSuperpro,
    dongleIsTimedSuperPro, decrementSuperProTimer, getSuperproSerialNo,
    superproDongleHoldsDate, getSuperproDongleDate, setSuperproDongleDate,
    getSuperproLLvLocales,
#ifdef MACOSX
    macSuperProMinRipVersion
#else
    NULL
#endif
  },
#endif
#ifdef EVE_DONGLE
  { eve3SecurityDevice, "EvE3", fEVE3FullTestDongle, fEVE3TestDongle, eve3EndDongle,
    dongleIsTimedEve3, decrementEve3Timer,
    getEve3SerialNo
  },
#ifndef MACOSX
  { eveSecurityDevice, "EvE2", fullTestEveDonglex3, testEveDonglex3, 0, 0, 0, 0 },
#endif
#endif
#ifdef MACTIVATOR
  { mactivatorSecurityDevice, "Mactivator", fullTestMactivator, testMactivator, 0, 0, 0, 0 },
#endif
#ifdef TIMEBOMB
  { timebombSecurityDevice, "Timebomb", fullTestTimebomb, testTimebomb, 0, 0, 0, 0 },
#endif
#if defined(MICROPHAR) || ( defined(SENTINEL) && defined(_PPC_) )
  { micropharSecurityDevice, "Microphar", fullTestMicroPharx3, testMicroPhar, 0,
    0, 0, getMicroPharSerialNo,
    0 },
#endif
#ifdef LESEC
  { leSecurityDevice, "LE security", fullTestLeSec, testLeSec, 0, 0, 0, 0 },
#endif
  { noSecurityDevice, NULL, 0, 0, 0, 0, 0, 0 }
};


static SecurityDevice * findSecurityDevice( int32 secDev );

#ifdef LLV
static int32 getLLvLocales( uint32 * pLLvLocales );
#endif


#ifdef NON_RIP_SECURITY
#define REPORT_DONGLE_ERROR() { } /* R_D_E isn't for use in non-SW prods */
#else

#ifdef EVE_DONGLE
#ifdef MACOSX
#define REPORT_DONGLE_ERROR() reportEve3DongleError()
#else
#define REPORT_DONGLE_ERROR() reportEveDongleError()
#endif /* MACOSX */
#endif /* EVE_DONGLE */
#ifdef MACTIVATOR
#define REPORT_DONGLE_ERROR() reportMactivatorError()
#endif /* MACTIVATOR */
#ifdef TIMEBOMB
#define REPORT_DONGLE_ERROR() reportTimebombError()
#endif /* TIMEBOMB */
#ifdef MICROPHAR
#define REPORT_DONGLE_ERROR() { }
#endif /* MICROPHAR */
#if defined (HQNLIC) || defined(LDK)
#define REPORT_DONGLE_ERROR() reportLICError()
#endif /* HQNLIC || LDK */
#ifdef LESEC
#define REPORT_DONGLE_ERROR() reportLeSecError()
#endif /* LESEC */

#if defined(CPLUS) || defined(SENTINEL)
#ifndef REPORT_DONGLE_ERROR
#define REPORT_DONGLE_ERROR() reportSentinelError()
#endif
#endif /* defined(CPLUS) || defined(SENTINEL) */

#ifndef REPORT_DONGLE_ERROR
#define REPORT_DONGLE_ERROR() { } /* No security device.  Dummy define to allow compilation. */
#endif

#endif /* NON_RIP_SECURITY */



/* ************************************************************************ */
/* The following checks dissallow certain combinations of security devices. */
/* ************************************************************************ */
/*                                                                          */
/*    IMPORTANT NOTE:  IMPORTANT NOTE:  IMPORTANT NOTE:  IMPORTANT NOTE:    */
/* ****        If a version is built with a Timebomb                   **** */
/* ****       this should be the only security mechanism.              **** */
/* ****      ADD ANY NEW SECURITY MECHANISM INTO THE FOLLOWING ifdef   **** */
/*                                                                          */
/*  NOTE: 2002-07-11: Timeout dongles and "timebomb" verion are different & */
/*  deliberately cannot be combined.  The timebomb is a timeout specially   */
/*  built into the RIP as in Watermark.  ANY RIP can use a timeout dongle,  */
/*  and will become time-limited only as a result of having that dongle     */
/*  plugged in                                                              */

#if defined(TIMEBOMB) && ( defined(EVE_DONGLE) || defined(MACTIVATOR) || defined(MICROPHAR) || defined (SENTINEL) )
this combination of security devices is not allowed -
        timebomb should not be used with any other mechanism
#endif /* defined(TIMEBOMB) && ( defined(EVE_DONGLE) || defined(MACTIVATOR) || defined(MICROPHAR) || defined(SENTINEL) ) */

/*                                                                          */
/* ************************************************************************ */
/* End of the security device combination checking.                         */
/* ************************************************************************ */

static int32 deviceMatchesPlatform( int32 * resultArray, int32 platform_id )
{
  /* Platform matches if:
   *         the dongle is a Harlequin Eval dongle
   *    or   the dongle is a global dongle
   *    or   the platform_id matches.
   */
  int32 customerNo;

  DONGLE_GET_CUSTOMER_NO( resultArray, customerNo );

  if( customerNo == 0x0B || customerNo == 0 )
  {
    /* Eval and global dongles work on any platform
     * Overwrite dongle value with current platform code
     */
    DONGLE_SET_PRODUCT_CODE( resultArray, platform_id );

    return TRUE;
  }
  else
  {
    int32 fProductCodeUsesBits;

    DONGLE_GET_PRODUCT_CODE_USES_BITS( resultArray, fProductCodeUsesBits );

    if( fProductCodeUsesBits )
    {
      /* Dongle has a product code which has one bit
       * per platform supported, allowing one dongle to
       * support multiple platforms.
       * Check if the appropriate bit is set for platform_id.
       */
      int32 platformCodes[ 16 ] = DONGLE_PLATFORM_BITS;
      int32 i;

      for( i = 0; i < 16; i++ )
      {
        if( platformCodes[ i ] == platform_id )
        {
          int32 platformFlags;

          DONGLE_GET_PRODUCT_CODE( resultArray, platformFlags );

          if( platformFlags & ( 1 << i ) )
          {
            /* Overwrite flags values with platform code */
            DONGLE_SET_PRODUCT_CODE_USES_BITS( resultArray, 0 );  /* Clear flag */
            DONGLE_SET_PRODUCT_CODE( resultArray, platform_id );

            return TRUE;
          }
          else
          {
            /* Overwrite flags values with 0xFF (i.e. an invalid platform code) */
            DONGLE_SET_PRODUCT_CODE_USES_BITS( resultArray, 0 );  /* Clear flag */
            DONGLE_SET_PRODUCT_CODE( resultArray, 0xFF );

            return FALSE;
          }
        }
      }

      HQFAIL( "Unknown platform_id - not in platformCodes[]" );

      return FALSE;
    }
    else
    {
      /* Dongle has a product code which specifies
       * the single platform supported by the dongle.
       * Require platform IDs to match
       */
      int32 platformID;

      DONGLE_GET_PRODUCT_CODE( resultArray, platformID );

      return ( platformID == platform_id );
    }
  }
}

static int32 deviceSupportsPlatform( int32 * resultArray, int32 platform_id )
{
#ifdef NON_RIP_SECURITY

  UNUSED_PARAM(int32 *, resultArray);
  UNUSED_PARAM(int32, platform_id);

  /* Platform supported if:
   *         always
   */

  return TRUE;

#else

  return deviceMatchesPlatform( resultArray, platform_id );

#endif
}

static int32 os_without_mp( int32 id )
{
  int32 os = id & PLATFORM_OS_MASK;
  int32 nonmp_os;

  switch( os )
  {
  case P_MACOS_PARALLEL:
    nonmp_os = P_MACOS;
    break;
  case P_UNIX_PARALLEL:
    nonmp_os = P_UNIX;
    break;
  case P_WINNT_PARALLEL:
    nonmp_os = P_WINNT;
    break;
  default:
    nonmp_os = os;
  }

  return nonmp_os;
}

static int32 id_without_mp( int32 id )
{
  int32 nonmp_os = os_without_mp( id );

  return nonmp_os | (id & PLATFORM_MACHINE_MASK);
}

static int32 machine_with_64bit( int32 id )
{
  int32 machine = id & PLATFORM_MACHINE_MASK;
  int32 machine_64bit;

  switch( machine )
  {
  case P_INTEL:
    machine_64bit = P_INTEL64;
    break;
  default:
    machine_64bit = machine;
  }

  return machine_64bit;
}

static int32 id_32_to_64bit( int32 id )
{
  int32 machine_64bit = machine_with_64bit( id );

  return machine_64bit | (id & PLATFORM_OS_MASK);
}

static int32 machine_without_64bit( int32 id )
{
  int32 machine = id & PLATFORM_MACHINE_MASK;
  int32 machine_32bit;

  switch( machine )
  {
  case P_INTEL64:
    machine_32bit = P_INTEL;
    break;
  default:
    machine_32bit = machine;
  }

  return machine_32bit;
}

static int32 id_64_to_32bit( int32 id )
{
  int32 machine_32bit = machine_without_64bit( id );

  return machine_32bit | (id & PLATFORM_OS_MASK);
}

int32 DoPlatformCheck(int32 * dongleValues, int32 platform_id, int32 * pRequiresFeature)
{
  int32 nonmp_id;
  int32 id_32bit;
  int32 id_64bit;

  *pRequiresFeature = 0;

  if( deviceSupportsPlatform(dongleValues, platform_id) )
  {
    return TRUE;
  }

  /* Allow a multi-process rip to run with a non-multi-process dongle */
  nonmp_id = id_without_mp(platform_id);
  if (nonmp_id != platform_id && deviceSupportsPlatform(dongleValues, nonmp_id) )
  {
    return TRUE;
  }

  /* Allow a 64-bit rip to run if there is a 32-bit dongle
    * (password required, checking elsewhere)
    */
  id_32bit = id_64_to_32bit(platform_id);
  if (id_32bit != platform_id)
  {
    if( deviceSupportsPlatform(dongleValues, id_32bit) )
    {
      *pRequiresFeature = GNKEY_FEATURE_64_BIT;
      return TRUE;
    }
    else
    {
      /* ... or a non-multi-process 32-bit dongle */
      int32 nonmp32_id = id_without_mp(id_32bit);
      if (nonmp32_id != id_32bit && deviceSupportsPlatform(dongleValues, nonmp32_id) )
      {
        *pRequiresFeature = GNKEY_FEATURE_64_BIT;
        return TRUE;
      }
    }
  }

  /* Allow a 32-bit rip to run if there is a 64-bit dongle
    * (password required if dongle is not for one version
    * newer than RIP, checking elsewhere)
    */
  id_64bit = id_32_to_64bit(platform_id);
  if (id_64bit != platform_id)
  {
    if( deviceSupportsPlatform(dongleValues, id_64bit) )
    {
      *pRequiresFeature = GNKEY_FEATURE_32_BIT;
      return TRUE;
    }
    else
    {
      /* ... or a non-multi-process 64-bit dongle */
      int32 nonmp64_id = id_without_mp(id_64bit);
      if (nonmp64_id != id_64bit && deviceSupportsPlatform(dongleValues, nonmp64_id) )
      {
        *pRequiresFeature = GNKEY_FEATURE_32_BIT;
        return TRUE;
      }
    }
  }

  /* Allow a rip to run on a "professional" OS if there is a dongle
    * for a "consumer" OS, specifically to allow Win98 dongles to work
    * with XP (password required, checking elsewhere)
    */
  if (platform_id == (P_INTEL|P_WINNT))
  {
    int32 consumer_id = (P_INTEL|P_WIN31);
    if( deviceSupportsPlatform(dongleValues, consumer_id) )
    {
      *pRequiresFeature = GNKEY_PLATFORM_PASSWORD;
      return TRUE;
    }
  }

  /* Allow any Windows RIP to run with a Mac dongle, so as to
    * allow crossgrades.
    */
  if (os_without_mp(platform_id) == P_WINNT)
  {
    int32 mac_id = (P_POWERMAC|P_MACOS);
    if( deviceSupportsPlatform(dongleValues, mac_id) )
    {
      return TRUE;
    }
  }

  return FALSE;
}

static int32 CheckSecurityDevice(SecurityDevice *dev, int32 platform_id)
{
  ZERO_RESULTARRAY( resultArray );

  if( dev != NULL )
  {
    if( dev->fullTest(resultArray) )
    {
      int32 dummy;

      if( DoPlatformCheck(resultArray, platform_id, &dummy) )
      {
        return TRUE;
      }
    }
  }

  return FALSE;
}

static int32 CheckAllSecurityDevices(int32 platform_id)
{
  if( securityDevice != noSecurityDevice )
  {
    /* Only look for known security device */
    if( CheckSecurityDevice(findSecurityDevice(securityDevice), platform_id) )
    {
      return securityDevice;
    }
  }
  else if( securityDeviceUsed != noSecurityDevice )
  {
    /* Only look for the security device which worked before */
    if( CheckSecurityDevice(findSecurityDevice(securityDeviceUsed), platform_id) )
    {
      return securityDeviceUsed;
    }
  }
  else
  {
    /* Search for all possible security devices */
    SecurityDevice *dev;

    for (dev = devices; dev->securityDevice != noSecurityDevice; dev++)
    {
      if( CheckSecurityDevice(dev, platform_id) )
      {
        return dev->securityDevice;
      }
    }
  }

  return noSecurityDevice;
}


/* Don't put startupDongleTestSilently right next to
 * startupDongleTestReport; granted it doesn't make much difference but
 * might as well try to separate them.
 */
int32 RIPCALL startupDongleTestSilently()
{
  int32 fResult;

#ifdef ASSERT_BUILD
  fDoingStartupTest = TRUE;
#endif

  corestart = FALSE;
  fResult = forcedFullTestSecurityDevice();

#ifdef ASSERT_BUILD
  fDoingStartupTest = FALSE;
#endif

  return fResult;
}

HqBool secDevCanEnableFeature( uint32 nFeature )
{
  UNUSED_PARAM(uint32, nFeature);

  return FALSE;
}

HqBool secDevEnablesFeature( uint32 nFeature, HqBool * pfEnabled, int32 * pfTrial )
{
  UNUSED_PARAM(uint32, nFeature);
  UNUSED_PARAM(HqBool *, pfEnabled);
  UNUSED_PARAM(int32 *, pfTrial);

  return FALSE;
}

HqBool secDevEnablesSubfeature( uint32 nFeature, char * pbzFeatureStr, HqBool * pfEnabled, int32 * pfTrial )
{
  UNUSED_PARAM(uint32, nFeature);
  UNUSED_PARAM(char *, pbzFeatureStr);
  UNUSED_PARAM(HqBool *, pfEnabled);
  UNUSED_PARAM(int32 *, pfTrial);

  return FALSE;
}

HqBool ripLicenseIsFromLDK( void )
{

  return FALSE;
}

void secDevDetermineMethod( void )
{
#if defined(LDK) && defined(HQNLIC)
  FwTextString ptbzSWDir = NULL;

#ifdef CORESKIN
  ptbzSWDir = PlatformSWDir();
#else
  /* Similar to PKSWDir()/findSWDir() in SWskinkit!src:file.c,
   * but using FrameWork functions.  No way that we can pick
   * up any SW folder specified as a command line arg though.
   * Assumes not Embedded, which won't be using HLS or LDK, will it?!
   */
  FwErrorState errState = FW_ERROR_STATE_INIT(FALSE);
  FwStrRecord  rPath;
  int32        i;

  FwStrRecordOpen(&rPath);

  for (i = 0; i < 2; i++)
  {
    FwTextString ptbzSearchRoot = (i == 0) ? FwCurrentDir() : FwAppDir();

    if(ptbzSearchRoot != NULL)
    {
      FwTextString ptbzSearchRootParent = NULL;

      /* <root>/SW */
      FwStrRecordShorten(&rPath, 0);
      FwStrPutTextString(&rPath, ptbzSearchRoot);
      FwFilenameConcatenate(&rPath, FWSTR_TEXTSTRING("SW"));
      if (FwFileGetInfo(&errState, FwStrRecordGetBuffer(&rPath), NULL, 0))
      {
        /* found it */
        FwFileEnsureTrailingSeparator(&rPath);
        ptbzSWDir = FwStrRecordClose(&rPath);
        break;
      }

      /* <root>/SW-clrip */
      FwStrRecordShorten(&rPath, 0);
      FwStrPutTextString(&rPath, ptbzSearchRoot);
      FwFilenameConcatenate(&rPath, FWSTR_TEXTSTRING("SW-clrip"));
      if (FwFileGetInfo(&errState, FwStrRecordGetBuffer(&rPath), NULL, 0))
      {
        /* found it */
        FwFileEnsureTrailingSeparator(&rPath);
        ptbzSWDir = FwStrRecordClose(&rPath);
        break;
      }

      /* <root>/../SW */
      ptbzSearchRootParent = FwStrDuplicate(ptbzSearchRoot);
      if(FwFileRemoveTrailingSeparator(ptbzSearchRootParent))
      {
        ptbzSearchRootParent = FwFileRemoveLeafname(ptbzSearchRootParent);

        FwStrRecordShorten(&rPath, 0);
        FwStrPutTextString(&rPath, ptbzSearchRootParent);
        FwFilenameConcatenate(&rPath, FWSTR_TEXTSTRING("SW"));
        if (FwFileGetInfo(&errState, FwStrRecordGetBuffer(&rPath), NULL, 0))
        {
          /* found it */
          FwFileEnsureTrailingSeparator(&rPath);
          ptbzSWDir = FwStrRecordClose(&rPath);
          FwMemFree(ptbzSearchRootParent);
          break;
        }
      }
      else
      {
        FwMemFree(ptbzSearchRootParent);
        ptbzSearchRootParent = NULL;
      }

      /* <root>/../SW-clrip */
      if( ptbzSearchRootParent != NULL )
      {
        FwStrRecordShorten(&rPath, 0);
        FwStrPutTextString(&rPath, ptbzSearchRootParent);
        FwFilenameConcatenate(&rPath, FWSTR_TEXTSTRING("SW-clrip"));
        if (FwFileGetInfo(&errState, FwStrRecordGetBuffer(&rPath), NULL, 0))
        {
          /* found it */
          FwFileEnsureTrailingSeparator(&rPath);
          ptbzSWDir = FwStrRecordClose(&rPath);
          FwMemFree(ptbzSearchRootParent);
          break;
        }
      }
      else
      {
        FwMemFree(ptbzSearchRootParent);
        ptbzSearchRootParent = NULL;
      }

      FwMemFreeSafely(&ptbzSearchRootParent);
    }
  }
#endif

  if( ptbzSWDir != NULL )
  {
    FwStrRecord  rPrefsFile;
    FwVector     vPrefs = { FW_VECTOR_NOT_INIT };
    FwErrorState errState = FW_ERROR_STATE_INIT(FALSE);

    FwStrRecordOpen(&rPrefsFile);
    FwStrPutTextString(&rPrefsFile, ptbzSWDir);
    FwFilenameConcatenate(&rPrefsFile, FWSTR_TEXTSTRING("secprefs"));

    FwVectorOpen(&vPrefs, 10);

    if( FwConfigReadFile(&errState, FwStrRecordGetBuffer(&rPrefsFile), &vPrefs)
      && vPrefs.nCurrentSize >= 2 )
    {
      FwTextString ptbzSetting = (FwTextString) FwVectorGetElement(&vPrefs, 0);

      if( FwStrEqual(ptbzSetting, FWSTR_TEXTSTRING("method")) )
      {
        FwTextString ptbzMethod = (FwTextString) FwVectorGetElement(&vPrefs, 1);

        if( FwStrEqual(ptbzMethod, FWSTR_TEXTSTRING("ldk")) )
        {
          preferredDevice = ldkSecurityDevice;
        }
        else if( FwStrEqual(ptbzMethod, FWSTR_TEXTSTRING("hls")) )
        {
          preferredDevice = licSecurityDevice;
        }
        else
        {
          HQFAIL("Unexpected method in secprefs files");
        }
      }
      else
      {
        HQFAIL("Unexpected setting in secprefs files");
      }
    }

    FwVectorAbandon(&vPrefs, TRUE);
    FwStrRecordAbandon(&rPrefsFile);

#ifndef CORESKIN
    FwMemFree(ptbzSWDir);
#endif
  }
#endif
}

HqBool secDevUseLDK( void)
{
#if defined(LDK)
  HqBool fUseLDK = TRUE;

#if defined(HQNLIC)
  if( preferredDevice == licSecurityDevice )
  {
    fUseLDK = FALSE;
  }
#endif

  return fUseLDK;
#else
  return FALSE;
#endif
}

HqBool secDevUseHLS( void)
{
#if defined(HQNLIC)
  HqBool fUseHLS = TRUE;

#if defined(LDK)
  if( preferredDevice == ldkSecurityDevice )
  {
    fUseHLS = FALSE;
  }
#endif

  return fUseHLS;
#else
  return FALSE;
#endif
}

#ifdef CORESKIN

/*
 * Get serial no of attached dongle.
 */
HqBool secDevGetId( FwStrRecord * prId )
{
  HqBool fSuccess = FALSE;

  SecurityDevice * dev = findSecurityDevice( securityDevice );

  if( dev != NULL )
  {
    if( dev->secDevId )
    {
      fSuccess = (dev->secDevId)( prId );
    }
    else
    {
      uint32 serialNo;

      if( getDongleSerialNo( &serialNo ) )
      {
        FwStrPrintf( prId, FALSE, FWSTR_TEXTSTRING( "%u" ), serialNo );
        fSuccess = TRUE;
      }
    }
  }

  return fSuccess;
}

HqBool secDevFeatureForSubFeature( uint32 nFeature, char * pbzFeatureStr, uint32 * pnKeyfeature )
{
  UNUSED_PARAM(uint32, nFeature);
  UNUSED_PARAM(char *, pbzFeatureStr);
  UNUSED_PARAM(uint32 *, pnKeyfeature);

  return FALSE;
}

static void doTimeExpiryDongleTaskCreate( void * pUnused )
{
  UNUSED_PARAM(void *, pUnused);

  PeriodicTaskCreate
   (
    &pTaskCheckTimeExpiryDongle,
    &taskSemaCheckTimeExpiryDongle,
    seconds2period( 60 ),                   /* Every 60 secs */
    task_CheckTimeExpiryDongle
#ifdef TRACE_PTASKS
    , "DongleExpiryCheck"
#endif
   );
}

HqBool secDevGetRIPIdentity( FwStrRecord * prIdentity )
{
  UNUSED_PARAM(FwStrRecord *, prIdentity);

  return FALSE;
}

void secDevSetFeatureName( uint32 nFeature, FwTextString ptbzName )
{
  UNUSED_PARAM(uint32, nFeature);
  UNUSED_PARAM(FwTextString, ptbzName);
}

HqBool ripLicenseIsFromHLS( void )
{

  return FALSE;
}

void installSecurityTasks( void )
{
}

int32 hasExpiringLicensesMenuItem( void )
{
  return FALSE;
}

#endif

int32 RIPCALL fullTestSecurityDevice(int32 *checkCode,
  int32 * aParams[ nTestSecDevParams ] )
{
  int32 platform_id = 0;
  uint32 RipFeatureLevel = 0;
  int32  fMustHaveWaterMark = FALSE;

  int32  secDev = securityDevice;

  HQASSERT( !corestart, "startupDongleTestSilently not called" );

#ifdef NON_RIP_SECURITY
  platform_id = PLATFORM_ID;
#else
  platform_id = RipPlatformID();
#endif

  securityDevice = CheckAllSecurityDevices(platform_id);


  if (securityDevice != noSecurityDevice) {
    int32 savedCheckCode = *checkCode;
    int32 fDemoFlag;
    int32 fProtectedPluginsFlag;
#ifdef CORESKIN
    static int32 fTimerDonglePTaskInstalled = FALSE;
#endif /* CORESKIN */

    securityDeviceUsed = securityDevice;

    DONGLE_GET_PRODUCT_CODE(resultArray, *aParams[ iProductCode ]);
    DONGLE_GET_RIP_VERSION(resultArray, *aParams[ iProductVersion ]);
    DONGLE_GET_DEMO_FLAG(resultArray, fDemoFlag);
    *aParams[ iDemonstrationOnly ] = (fDemoFlag) ? DEMONSTRATIONPRODUCT : FULLPRODUCT;

    DONGLE_GET_FEATURE_LEVEL(resultArray, RipFeatureLevel);
    if( LITE_RIP == RipFeatureLevel ) {
      /* Strictly speaking this isn't a res limit, but the RIP will
       * extract the res and width indexes from this data.
       */
      DONGLE_GET_MICRORIP_RES_INFO(resultArray, *aParams[ iResolutionLimit ]);
    } else {
      DONGLE_GET_RES_INFO(resultArray, *aParams[ iResolutionLimit ]);
    }

#ifdef CORESKIN
    /* Install ptask for timer dongles if not already done */
    if ( ! fTimerDonglePTaskInstalled )
    {
      if ( timeExpiryDongle() )
      {
        FwEventExecuteOnBootThread( doTimeExpiryDongleTaskCreate, NULL );
      }

      fTimerDonglePTaskInstalled = TRUE;
    }
#endif /* CORESKIN */

    /* Ignore the result of check, the full test is
     * done later - we call it for the side-effect of
     * updating the value of *customerID, and potentially
     * updating the RIP's customer number.
     */
    DoCustomerNumberCheck(resultArray, aParams[ iCustomerID ], TRUE);

    DONGLE_GET_SECURITY_NO(resultArray, *aParams[ iSecurityNumber ]);
    *aParams[ iFeatureLevel ] = RipFeatureLevel;
    /* from version 25 dongles */
    DONGLE_GET_PROTECTED_PLUGINS(resultArray, fProtectedPluginsFlag);
    *aParams[ iProtectedPlugins ] = (fProtectedPluginsFlag) ? PROTECTED_PLUGINS : UNPROTECTED_PLUGINS;
    DONGLE_GET_POSTSCRIPT_DENIED(resultArray, *aParams[ ifPostScriptDenied ]);
    DONGLE_GET_PDF_DENIED(resultArray, *aParams[ ifPDFDenied ]);
    DONGLE_GET_GUI_DENIED(resultArray, fGUIDenied);

#ifndef NON_RIP_SECURITY
#ifndef CORESKIN
    /* An HHR rip will only run if the GUI denied flag is set */
    if (!fGUIDenied)
    {
      /* unless its a global dongle */
      if (*aParams[ iCustomerID ] != 0)
      {
        goto SECURITY_ERROR;
      }
    }
#endif
#endif

    /* from version 29 dongles */
    if (*aParams[ iProductVersion ] >= 29)
    {
      int32 unusedBits;

      DONGLE_GET_WATERMARK(resultArray, fMustHaveWaterMark);
#ifndef NON_RIP_SECURITY
      if (fMustHaveWaterMark)
        goto SECURITY_ERROR;
#endif
      /* If the unused bits are set, give up. This is so that
       * we can use these bits in the future to deny other
       * features, and existing versions of the RIP will
       * then stop working.
       */
      DONGLE_GET_UNUSED_BITS(resultArray, unusedBits);
      if (unusedBits != 0)
        goto SECURITY_ERROR;
    }

    /* from version 30 dongles */
    if (*aParams[ iProductVersion ] >= 30)
    {
      int32  llvFlag;
#ifdef LLV
      uint32 llvLocales;
#endif

      DONGLE_GET_LLV(resultArray, llvFlag);

#ifdef LLV
      /* An LLv rip requires that llvFlag be set */
      if( ! llvFlag )
        goto SECURITY_ERROR;

      /* Check that this LLv locale is supported */
      if( getLLvLocales( &llvLocales ) )
      {
        if( llvLocales & ~LLV_ALL_LOCALES )
        {
          /* Unknown bits set */
          goto SECURITY_ERROR;
        }

#if defined (LLV_SC)
        if( ! ( llvLocales & LLV_LOCALE_SIMPLIFIED_CHINESE ) )
          goto SECURITY_ERROR;
#elif defined (LLV_TC)
        if( ! ( llvLocales & LLV_LOCALE_TRADITIONAL_CHINESE ) )
          goto SECURITY_ERROR;
#elif defined (LLV_INV)
        if( ! ( llvLocales & LLV_LOCALE_INVERTED_CASE ) )
          goto SECURITY_ERROR;
#else
        /* Unknown LLv define */
        HQFAIL_AT_COMPILE_TIME()
#endif
      }
      else
      {
        /* Failed to get LLV locale flags - fail */
        goto SECURITY_ERROR;
      }
#else
#ifndef NON_RIP_SECURITY
      /* A non-LLv rip requires that llvFlag be clear */
      if( llvFlag )
        goto SECURITY_ERROR;
#endif
#endif

      DONGLE_GET_HPS_DENIED(resultArray, *aParams[ ifHPSDenied ]);
    }
    else
    {
#ifdef LLV
      /* An LLv rip will only run with v30 or higher */
      goto SECURITY_ERROR;
#endif
    }

    /* from version 31 dongles */
    /* nothing new */

    /* from version 32 dongles */
    if (*aParams[ iProductVersion ] >= 32)
    {
      DONGLE_GET_XPS_DENIED(resultArray, *aParams[ ifXPSDenied ]);
      DONGLE_GET_APPLY_WATERMARK(resultArray, *aParams[ ifApplyWatermark ]);
    }

    /* from version 33 dongles */
    if (*aParams[ iProductVersion ] >= 33)
    {
      DONGLE_GET_TEAMWORK(resultArray, *aParams[ ifTeamWork ]);
    }

#ifdef CORESKIN
#endif

    *checkCode = ( *aParams[ iCustomerID ] ^
                   *aParams[ iProductCode ] ^
                   *aParams[ iProductVersion ] ^
                   *aParams[ iDemonstrationOnly ] ^
                   *aParams[ iResolutionLimit ] ^
                   *aParams[ iSecurityNumber ] ^
                   *aParams[ iFeatureLevel ] ^
                   *aParams[ iProtectedPlugins ] ^
                   *aParams[ ifPostScriptDenied ] ^
                   *aParams[ ifPDFDenied ] ^
                   *aParams[ ifHPSDenied ] ^
                   *aParams[ ifXPSDenied ] ^
                   *aParams[ ifApplyWatermark ] ^
                   *aParams[ ifTeamWork ] );
    return ( savedCheckCode ^ fullTestSecurityMask );
  }
  else
  {
    SecurityDevice * dev;

    /* Call end function for previously known device(s) */
    if( secDev != noSecurityDevice )
    {
      dev = findSecurityDevice( secDev );

      if( dev != NULL && dev->end )
      {
        dev->end();
      }
    }

  SECURITY_ERROR:
    HQASSERT( fDoingStartupTest, "Full test of security device failed\n" );

#ifdef CORESKIN
#ifdef PRODUCT_HAS_API
    riporbNotifyRIPIsReady( FALSE );
#endif
#endif

    *aParams[ iCustomerID ] = -2;
    *aParams[ iProductCode ] = -3;
    *aParams[ iProductVersion ] = -13;
    *aParams[ iDemonstrationOnly ] = -1;
    *aParams[ iResolutionLimit ] = -1;
    *aParams[ iSecurityNumber ] = -1;
    *aParams[ iFeatureLevel ] = -1;
    *aParams[ iProtectedPlugins ] = -1;
    *aParams[ ifPostScriptDenied ] = 0;
    *aParams[ ifPDFDenied ] = 0;
    *aParams[ ifHPSDenied ] = 0;
    *aParams[ ifXPSDenied ] = 0;
    *aParams[ ifApplyWatermark ] = 0;
    *aParams[ ifTeamWork ] = 0;

    return ( 0x287DAC56 );
  }
}

int32 RIPCALL testSecurityDevice(int32 *checkCode,
  int32 * aParams[ nTestSecDevParams ] )
{
#ifndef TEST_EVERY_CALL
  /* Use unsigned numbers for the time test. This makes the comparison
   * test very easy. When the number does wrap (every 50 days), one extra
   * dongle check will be performed.
   */
  static uint32 lastTimeTested = 0;
  uint32 currentTime;

  currentTime = (uint32)get_rtime();

  /* Carry out the security device test no more frequently than the device wants */
  if (lastTimeTested == 0 ||
      (currentTime - lastTimeTested) > getSecurityCheckPeriod() * 1000)
  {
#endif /* TEST_EVERY_CALL */
    SecurityDevice * dev;

#ifndef TEST_EVERY_CALL
    lastTimeTested = currentTime;
#endif /* TEST_EVERY_CALL */

    if( (dev = findSecurityDevice( securityDevice )) != NULL && dev->test() )
    {
      return ((*checkCode) ^ testSecurityMask);
    }

    HQFAIL( "Quick test of security device failed\n" );

#ifdef CORESKIN
#ifdef PRODUCT_HAS_API
    riporbNotifyRIPIsReady( FALSE );
#endif
#endif

    *aParams[ iCustomerID ] = -2;
    *aParams[ iProductCode ] = -3;
    *aParams[ iProductVersion ] = -13;
    *aParams[ iDemonstrationOnly ] = -1;
    *aParams[ iResolutionLimit ] = -1;
    *aParams[ iSecurityNumber ] = -1;
    *aParams[ iFeatureLevel ] = -1;
    *aParams[ iProtectedPlugins ] = -1;
    *aParams[ ifPostScriptDenied ] = 0;
    *aParams[ ifPDFDenied ] = 0;
    *aParams[ ifHPSDenied ] = 0;
    *aParams[ ifXPSDenied ] = 0;
    *aParams[ ifApplyWatermark ] = 0;
    *aParams[ ifTeamWork ] = 0;

    *checkCode = 0;
#ifndef TEST_EVERY_CALL
    lastTimeTested = 0;
#endif /* TEST_EVERY_CALL */

    return ( 0x90adcf43 );
#ifndef TEST_EVERY_CALL
  } else {
    return ( (*checkCode) ^ testSecurityMask );
  }
#endif /* TEST_EVERY_CALL */
}


void RIPCALL endSecurityDevice()
{
  SecurityDevice *dev;

  /* Not all security devices need shutting down. For example the
   * license server does need to be called so the license can be freed.
   */

  if( securityDevice != noSecurityDevice )
  {
    dev = findSecurityDevice( securityDevice );

    if( dev != NULL )
    {
      if (dev->end) dev->end();
    }

    securityDevice = noSecurityDevice;
  }
}


/* IF YOU CHANGE THE RIP VERSION TESTING IN THIS, YOU MUST CHANGE THE
 * SAME CHECK IN THE MACRO SECNeedRevisionPasswordCheck in
 * SWcore_interface!export:security.h (in core/interface/export "currently")*/
int32 RIPCALL startupDongleTestReport(int32 fTestPassed)
{
#ifndef NON_RIP_SECURITY
  int32 nDongleRipVersion;
#endif /* NON_RIP_SECURITY */

  int32 exitCode = 0;

  HQASSERT( !corestart, "startupDongleTestSilently not called" );

  if (corestart || !fTestPassed)
  {
    REPORT_DONGLE_ERROR();
    /* Give up with a bad error after reporting the various dongle errors. */
#ifdef CORESKIN
    coreskinexiterrorc(2, (uint8 *)"Fatal security device failure");
    /* Does not return */
#else
    SecurityExit(2, (uint8 *)"Fatal security device failure");
    return FALSE;
#endif
  }

#ifndef NON_RIP_SECURITY  /* Can ignore rip-specific code when defined */
  /* Don't allow RIP's that are more than 2 versions older than the
   * dongle to run.
   */
  nDongleRipVersion = DongleMaxRipVersion();
  SECMapDongleVersion(nDongleRipVersion);
  if (nDongleRipVersion - 2 > CustomerRipVersionNumber()) {
    exitCode = 3;
  }
#endif  /* NON_RIP_SECURITY */

  /* Check that the customer number in the dongle will allow this
   * customization of the RIP to run. The update of the RIP's
   * customer number should have been done already.
   */
  if (! DoCustomerNumberCheck(resultArray, NULL, FALSE))
    exitCode = 4;

  if (exitCode > 0)
  {
#ifdef CORESKIN
    coreskinexiterrorc(exitCode, (uint8 *)"Fatal security device failure");
    /* Does not return */
#else
    SecurityExit(exitCode, (uint8 *)"Fatal security device failure");
    return FALSE;
#endif
  }

  return TRUE;
}


int32 DoCustomerNumberCheck(int32 * dongleValues, int32 * customerID, int32 fUpdate)
{
  int32 DongleCustomerNo;
#ifndef NON_RIP_SECURITY
  int32 RIPCustomerNo=0;
#endif

#ifdef NON_RIP_SECURITY
  UNUSED_PARAM(int32, fUpdate);
#endif

  DONGLE_GET_CUSTOMER_NO( dongleValues, DongleCustomerNo );

  if (customerID != NULL)
    *customerID = DongleCustomerNo;

#ifdef NON_RIP_SECURITY  /* When NON_RIP_SECURITY defined just update customerID */
  return TRUE;
#else
    RIPCustomerNo = RipCustomerNumber();

    if (RIPCustomerNo == 0x06 &&
        DongleCustomerNo == 0x31)
    {
      RIPCustomerNo = DongleCustomerNo;
      if (fUpdate)
        SetRipCustomerNumber(RIPCustomerNo);
    }

#if defined(IBMPC) || defined(WIN32)
    if (RIPCustomerNo == 0x40 &&
        DongleCustomerNo == 0x07)
    {
      RIPCustomerNo = DongleCustomerNo;
      if (fUpdate)
        SetRipCustomerNumber(RIPCustomerNo);
    }
#endif /* defined(IBMPC) || defined(WIN32) */

    if ((RIPCustomerNo == 0x0f && DongleCustomerNo == 0x21) ||
        (RIPCustomerNo == 0x21 && DongleCustomerNo == 0x0f))
    {
      RIPCustomerNo = DongleCustomerNo;
    }

    if ((RIPCustomerNo == 0x1b && DongleCustomerNo == 0x07) ||
        (RIPCustomerNo == 0x07 && DongleCustomerNo == 0x1b))
    {
      RIPCustomerNo = DongleCustomerNo;
    }

    if (RIPCustomerNo == 0x04)
    {
      if (DongleCustomerNo == 0x0f)
      {
        RIPCustomerNo = DongleCustomerNo;
        if (fUpdate)
          SetRipCustomerNumber(RIPCustomerNo);
      }
      else if (DongleCustomerNo == 0x21)
      {
        RIPCustomerNo = DongleCustomerNo;
        if (fUpdate)
          SetRipCustomerNumber(0x0f);
      }
    }

    if ((RIPCustomerNo == 0x07 && DongleCustomerNo == 0x33) ||
        (RIPCustomerNo == 0x33 && DongleCustomerNo == 0x07))
    {
      RIPCustomerNo = DongleCustomerNo;
    }

    /* now actually test whether the dongle customer number will
     * enable this RIP.
     */
    return
      RIPCustomerNo != 0 &&
      (
        DongleCustomerNo == 0  ||
        DongleCustomerNo == RIPCustomerNo
      ) &&
      DongleLevel() == 0;
#endif /* NON_RIP_SECURITY */
}


/*
 * Return TRUE if a time-expiry dongle is attached.
 */
int32 timeExpiryDongle( void )
{
  SecurityDevice * dev = findSecurityDevice( securityDevice );

  if( dev != NULL )
  {
    if( dev->isTimed )
    {
      return (dev->isTimed)();
    }
  }

  return FALSE;
}

/*
 * Decrements the count of time remaining in the dongle.  Doesn't
 * return anything because the consequences of the decrement are not
 * handled by the clients of this function.
 */
void decrementTimeExpiringDongle( void )
{
  SecurityDevice * dev = findSecurityDevice( securityDevice );

  if( dev != NULL )
  {
    if( dev->decTimer )
    {
      (dev->decTimer)();
    }
    else
    {
      HQFAIL( "securityDevice is timeout device but has no decrement timer fn" );
    }
  }
}


static SecurityDevice * findSecurityDevice( int32 secDev )
{
  SecurityDevice * dev = devices;

  while( dev->securityDevice != noSecurityDevice )
  {
    if( secDev == dev->securityDevice )
    {
      return dev;
    }

    dev++;
  }

  return NULL;
}


/*
 * Get serial no of attached dongle.
 */
int32 getDongleSerialNo( uint32 * pSerialNo )
{
  SecurityDevice * dev = findSecurityDevice( securityDevice );
  uint32           serialNo;

  if( dev != NULL )
  {
    if( dev->serialNo )
    {
      if( (dev->serialNo)( &serialNo ) )
      {
        *pSerialNo = serialNo;
        return TRUE;
      }
    }
    else
    {
      HQFAIL( "Cannot get serial number of SecurityDevice" );
    }
  }

  return FALSE;
}


int32 getDongleDate( uint32 * pDate )
{
  SecurityDevice * dev = findSecurityDevice( securityDevice );
  int32            fHasDate;

  if( dev != NULL )
  {
    if( dev->hasDate && dev->hasDate( &fHasDate ) && fHasDate )
    {
      if( (dev->getDate)( pDate ) )
      {
        return TRUE;
      }
    }
  }

  return FALSE;
}


int32 setDongleDate( uint32 date )
{
  SecurityDevice * dev = findSecurityDevice( securityDevice );
  int32            fHasDate;

  if( dev != NULL )
  {
    if( dev->hasDate && dev->hasDate( &fHasDate ) && fHasDate )
    {
      if( (dev->setDate)( date ) )
      {
        return TRUE;
      }
    }
  }

  return FALSE;
}

#ifdef LLV

static int32 getLLvLocales( uint32 * pLLvLocales )
{
  SecurityDevice * dev = findSecurityDevice( securityDevice );
  int32            fSuccess = FALSE;

  if( dev != NULL )
  {
    if( dev->llvLocales != NULL )
    {
      /* Security device supports LLv, get locales */
      if( (dev->llvLocales)( pLLvLocales ) )
      {
        fSuccess = TRUE;
      }
    }
    else
    {
      /* Security device does not support LLv, so fail */
    }
  }

  return fSuccess;
}

#endif

int32 * getResultArray( void )
{
  return resultArray;
}


int32 getGUIDenied( void )
{
  return fGUIDenied;
}


int32 isLLvRip( void )
{
#ifdef LLV
  return TRUE;
#else
  return FALSE;
#endif
}

uint32 getLLvMsgChecksum(void)
{
#ifdef LLV
  extern uint32 ripIncrementImageSize; /* checksum, that is */
  return ripIncrementImageSize;
#else
  return 0;
#endif
}

uint32 getRipLLvLocale( void )
{
  uint32 locale = LLV_LOCALE_NONE;

#ifdef LLV
#if defined (LLV_SC)
  locale = LLV_LOCALE_SIMPLIFIED_CHINESE;
#elif defined (LLV_TC)
  locale = LLV_LOCALE_TRADITIONAL_CHINESE;
#elif defined (LLV_INV)
  locale = LLV_LOCALE_INVERTED_CASE;
#else
  /* Unknown LLv define */
  HQFAIL_AT_COMPILE_TIME()
#endif
#endif

  return locale;
}

uint32 getSecurityCheckPeriod( void )
{
  if( securityDevice == ldkSecurityDevice )
  {
    /* SafeNet LDK */
    return 60;
  }
  else if( securityDevice == licSecurityDevice )
  {
  }
  else if( securityDevice == eveSecurityDevice || securityDevice == eve3SecurityDevice )
  {
    /* For the EvE dongles the checking is state-machine based and we
     * need to make relatively frequent calls so as to complete a full
     * test in a reasonable time
     */
    return 15;
  }

  /* Other dongles do one big test less frequently
   */
  return 2 * 60;
}

int32 dongleGrantsHLSLicense( int32 * pResultArray )
{
  int32   fGrantsLicense = FALSE;

  int32   fDemo;
  int32   customerNo;
  int32   resolution;
  int32   featureLevel;

  DONGLE_GET_DEMO_FLAG( pResultArray, fDemo );
  DONGLE_GET_CUSTOMER_NO( pResultArray, customerNo );
  DONGLE_GET_RES_INFO( pResultArray, resolution );
  DONGLE_GET_FEATURE_LEVEL( pResultArray, featureLevel );

  /* Non-global demo dongles no longer grant a RIP license */
  if( !fDemo || customerNo == 0 )
  {
    /* Otherwise get license if:
     * - resolution != 0, i.e. not a ripless dongle, or
     * - a lite rip (not that we expect to see any of those these days,
     *               but it could have a 0 resolution value)
     */
    if( resolution != 0 || featureLevel == LITE_RIP )
    {
      fGrantsLicense = TRUE;
    }
  }

  return fGrantsLicense;
}

int32 donglePermitsPermitType( int32 * pResultArray, char * pProduct, int32 cbProdLen, uint32 permitType )
{
  int32   fDongleAllowsPermit = FALSE;

  char  * ripProduct = "Harlequin RIP - PostScript Interpreter";
  int32  cbRipProduct = strlen_int32( ripProduct );

  char  * tbybProduct = "Harlequin RIP - Try Before You Buy";
  int32  cbTbybProduct = strlen_int32( tbybProduct );

  int32   customerNo;
  int32   dongleType;

  int32   fNormalDongle;
  int32   fDemoDongle;
  int32   fEmptyDongle;

  DONGLE_GET_DEMO_FLAG( pResultArray, fDemoDongle );
  DONGLE_GET_CUSTOMER_NO( pResultArray, customerNo );
  DONGLE_GET_DONGLE_TYPE( pResultArray, dongleType );

  fNormalDongle = dongleGrantsHLSLicense( pResultArray );
  fEmptyDongle = !fNormalDongle && !fDemoDongle && dongleType == DONGLE_TYPE_EMPTY;

  if( customerNo == 0 )
  {
    /* Global dongle allows all permits */
    fDongleAllowsPermit = TRUE;
  }
  else if( cbRipProduct <= cbProdLen && FwStrNEqual( pProduct, ripProduct, cbRipProduct ) )
  {
    /* RIP permit */
    if( permitType == LSS_TYPE_NORMAL )
    {
      /* Normal permit works with normal and empty dongles */
      if( fNormalDongle || fEmptyDongle )
      {
        fDongleAllowsPermit = TRUE;
      }
    }
    else if( permitType == LSS_TYPE_RIP_DEMO )
    {
      /* Demo permit works with demo dongles */
      if( fDemoDongle )
      {
        fDongleAllowsPermit = TRUE;
      }
    }
    else if( permitType == LSS_TYPE_RIP_HIRE_PURCHASE )
    {
      /* Hire purchase permit works with empty dongles */
      if( fEmptyDongle )
      {
        fDongleAllowsPermit = TRUE;
      }
    }
  }
  else if( cbTbybProduct <= cbProdLen && FwStrNEqual( pProduct, tbybProduct, cbTbybProduct ) )
  {
    /* Try Before You Buy permit */
    if( permitType == LSS_TYPE_NORMAL )
    {
      /* Normal permit works with normal and empty dongles (NB not demo dongles) */
      if( fNormalDongle || fEmptyDongle )
      {
        fDongleAllowsPermit = TRUE;
      }
    }
    /* Demo and hire purchase permits are not expected for non-RIP product */
  }
  else
  {
    /* Permit for some other product */
    if( permitType == LSS_TYPE_NORMAL )
    {
      /* Normal permit works with normal, demo and empty dongles */
      if( fNormalDongle || fDemoDongle || fEmptyDongle )
      {
        fDongleAllowsPermit = TRUE;
      }
    }
    /* Demo and hire purchase permits are not expected for non-RIP product */
  }

  return fDongleAllowsPermit;
}

#ifdef SENTINEL
#ifdef MACOSX
static uint32 macSuperProMinRipVersion( void )
{
  /* SuperPro not supported on Mac OS X until v7 */
  return 7;
}
#endif
#endif

#ifdef NON_RIP_SECURITY

/* Map RIP version number stored in dongle to RIP
 * version number as seen by user.
 */
static uint32 adjustRipVersion( uint32 version )
{
  /*   13    v4 RIP
   *   14    v5 RIP
   *   15    v6 RIP
   *   16    v7 RIP
   *   17    v8 RIP
   *   18    v9 RIP
   *   25    v4 Restricted RIP
   *   26    v5 Restricted RIP
   *   27    v6 Restricted RIP
   *   29    v5 Further Restricted RIP
   *   30    v6 Further Restricted RIP
   *   31    v7 Restricted RIP
   *   32    v8 Restricted RIP
   *   33    v9 Restricted / MultiRIP 2 RIP
   *   34    MultiRIP 3 RIP
   *   35    MultiRIP 10 RIP
   */
  SECMapDongleVersion(version);

  /* Now adjust to version seen by user */
  version -= 9;

  return version;
}

static int32 dongleSupportsPlatform( int32 * resultArray, int32 platform_id )
{
  /* Determine if the dongle can support platform_id, possibly with
   * appropriate passwords.
   * This isn't an exact science, because the LM and RIP might be
   * compiled differently (32-bit vs 64-bit, SMP vs not-SMP, etc).
   */
  int32 fSupportsPlatform;

  /* First check for full OS + architecture match, including effects of global/eval dongles */
  fSupportsPlatform = deviceMatchesPlatform( resultArray, platform_id );

  if ( !fSupportsPlatform )
  {
    /* Not a simple match.
     * Check for a match, ignoring SMP and 32/64-bit
     */
    int32 nonmp_os = os_without_mp( platform_id );
    int32 non64_machine = machine_without_64bit( platform_id );
    int32 match_id = nonmp_os | non64_machine;
    int32 fProductCodeUsesBits;

    DONGLE_GET_PRODUCT_CODE_USES_BITS( resultArray, fProductCodeUsesBits );

    if( fProductCodeUsesBits )
    {
      /* Dongle has a product code which has one bit
       * per platform supported, allowing one dongle to
       * support multiple platforms.
       * Check if the non-MP OS of any of the supported
       * platforms matches nonmp_os.
       */
      int32 platformCodes[ 16 ] = DONGLE_PLATFORM_BITS;
      int32 platformFlags;
      int32 i;

      DONGLE_GET_PRODUCT_CODE( resultArray, platformFlags );

      for( i = 0; i < 16; i++ )
      {
        if( platformFlags & ( 1 << i ) )
        {
          int32 dongle_nonmp_os = os_without_mp( platformCodes[ i ] );
          int32 dongle_non64_machine = machine_without_64bit( platformCodes[ i ] );
          int32 dongle_id = dongle_nonmp_os | dongle_non64_machine;

          if( dongle_id == match_id )
          {
            fSupportsPlatform = TRUE;
            break;
          }
        }
      }
    }
    else
    {
      /* Dongle has a product code which specifies
       * the single platform supported by the dongle.
       * Require platform IDs to match
       */
      int32 product_code;
      int32 dongle_nonmp_os;
      int32 dongle_non64_machine;
      int32 dongle_id;

      DONGLE_GET_PRODUCT_CODE( resultArray, product_code );
      dongle_nonmp_os = os_without_mp( product_code );
      dongle_non64_machine = machine_without_64bit( product_code );
      dongle_id = dongle_nonmp_os | dongle_non64_machine;

      if ( dongle_id == match_id )
      {
        fSupportsPlatform = TRUE;
      }
    }
  }

  return fSupportsPlatform;
}

static void appendPlatformName( FwStrRecord * prName, int32 platform_id )
{
  int32 machine = platform_id & PLATFORM_MACHINE_MASK;
  int32 os = platform_id & PLATFORM_OS_MASK;

  switch( os )
  {
  case P_MACOS:
  case P_MACOS_PARALLEL:
    FwStrPutRawString( prName, FWSTR_TEXTSTRING("Mac OS X") );
    break;
  case P_UNIX:
  case P_UNIX_PARALLEL:
    FwStrPutRawString( prName, FWSTR_TEXTSTRING("Linux") );

    switch( machine )
    {
    case P_INTEL:
      FwStrPutRawString( prName, FWSTR_TEXTSTRING(" 32-bit") );
      break;
    case P_INTEL64:
      FwStrPutRawString( prName, FWSTR_TEXTSTRING(" 64-bit") );
      break;
    default:
      break;
    }
    break;
  case P_WINNT:
  case P_WINNT_PARALLEL:
    FwStrPutRawString( prName, FWSTR_TEXTSTRING("Windows") );

    switch( machine )
    {
    case P_INTEL:
      FwStrPutRawString( prName, FWSTR_TEXTSTRING(" 32-bit") );
      break;
    case P_INTEL64:
      FwStrPutRawString( prName, FWSTR_TEXTSTRING(" 64-bit") );
      break;
    default:
      break;
    }
    break;
  case P_WIN31:
    FwStrPutRawString( prName, FWSTR_TEXTSTRING("Windows 95") );
    break;
  case P_CUSTOM:
    FwStrPutRawString( prName, FWSTR_TEXTSTRING("Custom") );
    break;
  default:
    HQFAIL("Unknown OS");
    break;
  }
}

static void getDonglePlatforms( int32 * resultArray, FwStrRecord * prPlatforms )
{
  uint32 customerNo;

  DONGLE_GET_CUSTOMER_NO( resultArray, customerNo );

  if( customerNo == 0 )
  {
    FwStrPutRawString( prPlatforms, FWSTR_TEXTSTRING( "Any" ) );
  }
  else
  {
    int32 fProductCodeUsesBits;

    DONGLE_GET_PRODUCT_CODE_USES_BITS( resultArray, fProductCodeUsesBits );

    if( fProductCodeUsesBits )
    {
      /* Dongle has a product code which has one bit
       * per platform supported, allowing one dongle to
       * support multiple platforms.
       */
      int32 platformCodes[ 16 ] = DONGLE_PLATFORM_BITS;
      int32 platformFlags;
      int32 i;
      int32 fFirst = TRUE;

      DONGLE_GET_PRODUCT_CODE( resultArray, platformFlags );

      for( i = 0; i < 16; i++ )
      {
        if( platformFlags & ( 1 << i ) )
        {
          if( !fFirst )
          {
            FwStrPutRawString( prPlatforms, FWSTR_TEXTSTRING( ", " ) );
          }
          appendPlatformName( prPlatforms, platformCodes[ i ] );
          fFirst = FALSE;
        }
      }
    }
    else
    {
      /* Dongle has a product code which specifies
       * the single platform supported by the dongle.
       */
      int32 product_code;

      DONGLE_GET_PRODUCT_CODE( resultArray, product_code );

      appendPlatformName( prPlatforms, product_code );
    }
  }
}

static int32 donglePlatformIs64bit( int32 * resultArray )
{
  int32 fIs64Bit = FALSE;
  int32 fProductCodeUsesBits;

  DONGLE_GET_PRODUCT_CODE_USES_BITS( resultArray, fProductCodeUsesBits );

  if( fProductCodeUsesBits )
  {
    /* Dongle has a product code which has one bit
     * per platform supported, allowing one dongle to
     * support multiple platforms.
     */
    HQFAIL("TODO: donglePlatformIs64bit() is not implemented for fProductCodeUsesBits");
    /* Will need to revise caller too, as a dongle might be valid for 32-bit and 64-bit */
  }
  else
  {
    /* Dongle has a product code which specifies
     * the single platform supported by the dongle.
     */
    int32 product_code;
    int32 machine;
    int32 non64_machine;

    DONGLE_GET_PRODUCT_CODE( resultArray, product_code );

    machine = product_code & PLATFORM_MACHINE_MASK;
    non64_machine = machine_without_64bit( product_code );

    if( machine != non64_machine )
    {
      fIs64Bit = TRUE;
    }
  }

  return fIs64Bit;
}

static int32 donglePlatformHas64BitRips( int32 * resultArray )
{
  int32 fHas64BitRips = FALSE;
  int32 fProductCodeUsesBits;

  DONGLE_GET_PRODUCT_CODE_USES_BITS( resultArray, fProductCodeUsesBits );

  if( fProductCodeUsesBits )
  {
    /* Dongle has a product code which has one bit
     * per platform supported, allowing one dongle to
     * support multiple platforms.
     */
    HQFAIL("TODO: donglePlatformHas64BitRips() is not implemented for fProductCodeUsesBits");
    /* and what if some platforms do and some don't ??? */
  }
  else
  {
    /* Dongle has a product code which specifies
     * the single platform supported by the dongle.
     */
    int32 product_code;
    int32 dongle_nonmp_os;

    DONGLE_GET_PRODUCT_CODE( resultArray, product_code );

    dongle_nonmp_os = os_without_mp( product_code );

    if( dongle_nonmp_os == P_UNIX || dongle_nonmp_os == P_WINNT )
    {
      fHas64BitRips = TRUE;
    }
  }

  return fHas64BitRips;
}

static int32 donglePlatformIsMac( int32 * resultArray )
{
  int32 fPlatformIsMac = FALSE;
  int32 fProductCodeUsesBits;

  DONGLE_GET_PRODUCT_CODE_USES_BITS( resultArray, fProductCodeUsesBits );

  if( fProductCodeUsesBits )
  {
    /* Dongle has a product code which has one bit
     * per platform supported, allowing one dongle to
     * support multiple platforms.
     */
    HQFAIL("TODO: donglePlatformIsMac() is not implemented for fProductCodeUsesBits");
    /* and what if some platforms do and some don't ??? */
  }
  else
  {
    /* Dongle has a product code which specifies
     * the single platform supported by the dongle.
     */
    int32 product_code;
    int32 dongle_nonmp_os;

    DONGLE_GET_PRODUCT_CODE( resultArray, product_code );

    dongle_nonmp_os = os_without_mp( product_code );

    if( dongle_nonmp_os == P_MACOS )
    {
      fPlatformIsMac = TRUE;
    }
  }

  return fPlatformIsMac;
}

/* Generates a report in *prInfo about the connected dongle(s)
 */
void getDongleInfo( FwStrRecord * prInfo )
{
  FwStrRecord      rInfo;

  int32            deviceData[ RESULT_ARRAY_SIZE ];
  SecurityDevice * pDev;

  int32            platform_id;
  int32            fFoundDongle = FALSE;

  int32            securityNo;
  int32            checksum;
  uint32           serialNo = 0;
  int32            fGotSerialNo = FALSE;
  int32            fPlatformMatches = FALSE;
  int32            fGrantsHLSLicense;
  int32            fMultiRIP;
  uint32           customerNo;
  uint32           maxRipVersion = 0;
  uint32           minRipVersion = 0;

  platform_id = PLATFORM_ID;

  FwStrRecordOpenSize( &rInfo, 2048 );

  /* Search for a current security device */
  for( pDev = devices; pDev->securityDevice != noSecurityDevice; pDev++ )
  {
    ZERO_RESULTARRAY( deviceData );

    if( pDev->fullTest( deviceData ) )
    {
      fPlatformMatches = dongleSupportsPlatform( deviceData, platform_id );
      fGrantsHLSLicense = dongleGrantsHLSLicense( deviceData );

      DONGLE_GET_SECURITY_NO( deviceData, securityNo );
      CHECKSUM( securityNo, checksum, 0 );
      DONGLE_GET_CUSTOMER_NO( deviceData, customerNo );
      DONGLE_GET_TEAMWORK( deviceData, fMultiRIP );

      if( pDev->serialNo )
      {
        fGotSerialNo = pDev->serialNo( &serialNo );
      }

      if( fGrantsHLSLicense )
      {
        FwStrRecord rPlatforms;

        FwStrRecordOpen( &rPlatforms );
        getDonglePlatforms( deviceData, &rPlatforms );

        if( fPlatformMatches )
        {
          if( fGotSerialNo )
          {
            FwStrPrintf( &rInfo, TRUE, FWSTR_TEXTSTRING( "Found supported %s dongle:\n  Customer no: %u:  Security no: %d-%02d:  Serial no: %u:  Platform: %s\n" ),
              pDev->deviceType, customerNo, securityNo, checksum, serialNo, FwStrRecordGetBuffer( &rPlatforms ) );
          }
          else
          {
            FwStrPrintf( &rInfo, TRUE, FWSTR_TEXTSTRING( "Found supported %s dongle:\n  Customer no: %u:  Security no: %d-%02d:  Unknown serial no:  Platform: %s\n" ),
              pDev->deviceType, customerNo, securityNo, checksum, FwStrRecordGetBuffer( &rPlatforms ) );
          }

          if( pDev->minRipVersion )
          {
            /* Dongle type is not supported by earlier RIPs */
            minRipVersion = pDev->minRipVersion();
          }

          if( customerNo == 0 )
          {
            /* Global dongles enable any version, including MultiRIP */
            if( minRipVersion <= 6)
            {
              FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP Eclipse Release SP4, v7, v8, v9 and MultiRIP v2, v3, v10\n\n" ) );
            }
            else
            {
              HQASSERT( minRipVersion == 7, "Unexpected minRipVersion value" );

              FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP v7, v8, v9 and MultiRIP v2, v3, v10\n\n" ) );
            }
          }
          else if( customerNo == 0x0B )
          {
            /* Eval dongles no longer grant a RIP license, so we should never get to here */
            HQFAIL( "Eval dongle which grants a RIP license???" );
          }
          else
          {
            /* Customer dongle enables previous 2 versions.  Also need to check MultiRIP flag. */
            DONGLE_GET_RIP_VERSION( deviceData, maxRipVersion );
            maxRipVersion = adjustRipVersion( maxRipVersion );

            if( fMultiRIP )
            {
              HQASSERT( maxRipVersion >= 9, "MultiRIP dongles should be at least v9" );

              if( maxRipVersion == 9 )
              {
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP v9 and MultiRIP v2\n" ) );
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin MultiRIP v3 or v10\n\n" ) );
              }
              else if( maxRipVersion == 10 )
              {
                int32 fDonglePlatformHas64BitRips = donglePlatformHas64BitRips( deviceData );
 
                if( fDonglePlatformHas64BitRips )
                {
                  int32 fDongleIs64Bit = donglePlatformIs64bit( deviceData );

                  if( fDongleIs64Bit )
                  {
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP v9 and MultiRIP v2 and v3 (64-bit)\n" ) );
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin MultiRIP v10\n\n" ) );
                  }
                  else
                  {
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin MultiRIP v2 and v3 (32-bit)\n" ) );
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin MultiRIP v10\n" ) );
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "A 64-bit password is required to enable Harlequin MultiRIP v3 (64-bit)\n\n" ) );
                  }
                }
                else
                {
                  /* Platform only has 32-bit RIPs */
                  FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin MultiRIP v2 and v3\n" ) );
                  FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin MultiRIP v10\n\n" ) );
                }
              }
              else if( maxRipVersion == 11 )
              {
                int32 fDonglePlatformHas64BitRips = donglePlatformHas64BitRips( deviceData );
 
                if( fDonglePlatformHas64BitRips )
                {
                  int32 fDongleIs64Bit = donglePlatformIs64bit( deviceData );

                  if( fDongleIs64Bit )
                  {
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable MultiRIP v2, v3 and v10 (64-bit)\n" ) );
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "A 32-bit password is required to enable Harlequin MultiRIP v10 (32-bit)\n\n" ) );
                  }
                  else
                  {
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin MultiRIP v2, v3 and v10 (32-bit)\n" ) );
                    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "A 64-bit password is required to enable Harlequin MultiRIP v3 or v10 (64-bit)\n\n" ) );
                  }
                }
                else
                {
                  /* Platform only has 32-bit RIPs */
                  FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin MultiRIP v2, v3 and v10\n" ) );
                }
              }
              else
              {
                HQFAIL( "Unhandled MultiRIP version" );
              }
            }
            else if( minRipVersion <= 6)
            {
              if( maxRipVersion <= 6 )
              {
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP Eclipse Release SP4\n" ) );
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin RIP v7, v8, v9 or MultiRIP v2, v3, v10\n\n" ) );
              }
              else if( maxRipVersion == 7 )
              {
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP Eclipse Release SP4 and v7\n" ) );
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin RIP v8, v9 or MultiRIP v2, v3, v10\n\n" ) );
              }
              else if( maxRipVersion == 8 )
              {
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP Eclipse Release SP4, v7 and v8\n" ) );
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin RIP v9 or MultiRIP v2, v3, v10\n\n" ) );
              }
              else if( maxRipVersion == 9 )
              {
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP v7, v8 and v9\n" ) );
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin MultiRIP v2, v3, v10\n\n" ) );
              }
              else
              {
                HQFAIL( "Unhandled maxRipVersion" );
              }
            }
            else
            {
              HQASSERT( minRipVersion == 7, "Unexpected minRipVersion value" );

              if( maxRipVersion == 7 )
              {
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP v7\n" ) );
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin RIP v8, v9 or MultiRIP v2, v3, v10\n\n" ) );
              }
              else if( maxRipVersion == 8 )
              {
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP v7 and v8\n" ) );
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin RIP v9 or MultiRIP v2, v3, v10\n\n" ) );
              }
              else if( maxRipVersion == 9 )
              {
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "This dongle will enable Harlequin RIP v7, v8 and v9\n" ) );
                FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin MultiRIP v2, v3, v10\n\n" ) );
              }
              else
              {
                HQFAIL( "Unhandled maxRipVersion" );
              }
            }
          }
        }
        else
        {
          if( fGotSerialNo )
          {
            FwStrPrintf( &rInfo, TRUE, FWSTR_TEXTSTRING( "Found %s dongle for a different platform (%s):\n  Customer no: %u:  Security no: %d-%02d:  Serial no: %u\n" ),
              pDev->deviceType, FwStrRecordGetBuffer( &rPlatforms ), customerNo, securityNo, checksum, serialNo );
          }
          else
          {
            FwStrPrintf( &rInfo, TRUE, FWSTR_TEXTSTRING( "Found %s dongle for a different platform (%s):\n  Customer no: %u:  Security no: %d-%02d:  Unknown serial no\n" ),
              pDev->deviceType, FwStrRecordGetBuffer( &rPlatforms ), customerNo, securityNo, checksum );
          }

#ifdef WIN32
          if( donglePlatformIsMac( deviceData ) )
          {
            FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "An upgrade password is required to enable Harlequin MultiRIP v10\n\n" ) );
          }
#endif
        }

        FwStrRecordAbandon( &rPlatforms );
      }
      else
      {
        if( fGotSerialNo )
        {
          FwStrPrintf( &rInfo, TRUE, FWSTR_TEXTSTRING( "Found %s dongle which provides HLS unique ID:\n  Customer no: %u:  Security no: %d-%02d:  Serial no: %u\n" ),
            pDev->deviceType, customerNo, securityNo, checksum, serialNo );
        }
        else
        {
          FwStrPrintf( &rInfo, TRUE, FWSTR_TEXTSTRING( "Found %s dongle which provides HLS unique ID:\n  Customer no: %u:  Security no: %d-%02d:  Unknown serial no\n" ),
            pDev->deviceType, customerNo, securityNo, checksum );
        }
      }

      fFoundDongle = TRUE;
      break;
    }
  }

  if( !fFoundDongle )
  {
    FwStrPutTextString( &rInfo, FWSTR_TEXTSTRING( "No dongles found\n" ) );
  }

  FwTranslStripMarkers( FwStrRecordGetBuffer( &rInfo ), prInfo );
  FwStrRecordAbandon( &rInfo );
}

#endif


/* eof dongle.c */
