/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:security.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS security/dongle code
 */

#include "core.h"
#include "hqmemset.h"
#include "proto.h"
#include "objects.h"
#include "fileio.h"
#include "swerrors.h"
#include "chartype.h"
#include "intscrty.h"
#include "extscrty.h"
#include "security.h"
#include "swstart.h"
#include "custlib.h"
#include "ripversn.h"    /* Give's RIP version. */
#include "params.h"

/* Exclude toothpick security table until we're sure we can remove it. */
#define NO_TOOTHPICK_TABLE
#include "gentab.h"      /* for aHds and so on */
#include "checksum.h"    /* for CHECKSUM */
#include "genkey.h"      /* GNKEY_XXX numbers */
#include "dongle.h"      /* secDevCanEnableFeature() */

#include "hqxcrypt.h"

#if PLATFORM_OS == P_WINNT || PLATFORM_OS == P_WIN31
#include <windows.h>
#endif

#define CODESCRAMBLE 0x489F4834
#define randomiseInScrty incrementFileList
#define FULLCHECKPERIOD 1000 * 60 * 5 /* 5 minutes */

#define INITIAL_CHECK_SEED   0x9A28D409

/* obfuscation */
#define performSecurityCheck   mapCharSets

STATIC uint32 randomiseInScrty (uint32 val, uint32 flag) ;
STATIC int32 performSecurityCheck(int32 fFull, uint32 seed);
STATIC int32 permission (uint32 nSecurity, uint8 aTable[16], int32 nFeature, int32 * pfTrial);

/* for LITE_RIP - use large number for 'unlimited' so not to break existing interface */
STATIC int32 res_limit[2][16] =
{
  /* resolution limit - indexed by bits 0-3 of resolutionLimit */
  {72, 100, 300, 400, 600, 800, 900, 1000, 1200, 990, 0, 0, 0, 0, 0, 99999999},
  /* x width pixel limit - indexed by bits 4-7 of resolutionLimit */
  {1000, 3000, 5000, 7000, 9000, 11000, 13000, 15000, 18000, 22000, 36000, 44000, 0, 0, 0, 99999999}
};

STATIC int32  customerID = -1;
STATIC int32  productCode = -1;
STATIC int32  productVersion = -1;
STATIC int32  demonstrationOnly = (int32)0xFFFFFFFF;
STATIC int32  resolutionLimit = (int32)0xFFFFFFFF;
STATIC int32  xPixelWidth = 0;
STATIC int32  upperResolutionLimit = 0;
STATIC int32  lowerResolutionLimit = 0;
STATIC int32  securityNumber = (int32)0xFFFFFFFF;
STATIC int32  featureLevel = -1;
STATIC int32  protectedPlugins = (int32)0xFFFFFFFF;
static int32  fPostScriptDenied = FALSE;
static int32  fPDFDenied = FALSE;
static int32  fHPSDenied = FALSE;
static int32  fXPSDenied = FALSE;
static int32  fApplyWatermark = FALSE;
static int32  fTeamWork = FALSE;
static int32  tbubSecurityNumber = -1;
static int32  revisionPasswordKey = 0;

static int32 * aTestSecDevParams[ nTestSecDevParams ] =
{
  &customerID,
  &productCode,
  &productVersion,
  &demonstrationOnly,
  &resolutionLimit,
  &securityNumber,
  &featureLevel,
  &protectedPlugins,
  &fPostScriptDenied,
  &fPDFDenied,
  &fHPSDenied,
  &fXPSDenied,
  &fApplyWatermark,
  &fTeamWork
};

int32 RIPCALL RipPlatformID( void )
{
  /* Returns the platform that the rip was compiled for. */
  return PLATFORM_ID;
}

int32 RIPCALL DonglePlatformID(void)
{
  return ( productCode );
}

int32 RIPCALL DongleLevel(void)
{
  return (0);  /* Return zero to remain compatible with older dongles */
}

int32 RIPCALL DongleMaxRipVersion(void)
{
  return ( productVersion );
}

#define IS_LITE_RIP  ( featureLevel == 1 )

/*
 * SECGetCustomerMaxYResolution
 *
 * Get the maximum y-resolution, which is dependent on the Lite-ness of the RIP and
 * the customer.
 */
int32 RIPCALL SECGetMaxYResolution(void)
{
  int32     nMaxForLiteRip = ( (RipCustomerNumber() == 0x16) ? 1200 : 999);

  /* If this is a Lite RIP, use the figure calculated above.
   * If this is a full RIP, use the maximum resolution from the dongle.
   */
  int32     nMaxCustRes = (IS_LITE_RIP ? nMaxForLiteRip : DongleMaxResolution() );

  return nMaxCustRes;
}


/*
 * DoGetCustomerRipVersionNumber
 *
 * This function is needed as we can't directly call CustomerRipVersionNumber
 * from the macro SECGetRipVersionNumber in security.h
 * I'd prefer to leave SECGetRipVersionNumber as a macro as it's less easy
 * to hack that way.
 */
int32 RIPCALL DoGetCustomerRipVersionNumber(void)
{
  return CustomerRipVersionNumber();
}

int32 RIPCALL DongleMaxResolution(void)
{
  return ( upperResolutionLimit );
}

int32 RIPCALL DongleMinResolution(void)
{
  return ( lowerResolutionLimit );
}

int32 RIPCALL DongleMaxXWidth( void )
{
  return ( xPixelWidth ) ;
}

int32 RIPCALL DongleCustomerNo(void)
{
  return ( customerID );
}

int32 RIPCALL HqxCryptCustomerNo(void)
{
  return HqxCryptCustomerNo_from_DongleCustomerNo( DongleCustomerNo() );
}

int32 RIPCALL DongleDemoVersion(void)
{
  return ( demonstrationOnly != FULLPRODUCT );
}

int32 RIPCALL DongleProtectedPlugins(void)
{
  return ( protectedPlugins != UNPROTECTED_PLUGINS );
}

int32 RIPCALL DongleSecurityNo()
{
  return ( securityNumber );
}

int32 RIPCALL HqxCryptSecurityNo()
{
  return HqxCryptSecurityNo_from_DongleSecurityNo( DongleSecurityNo() );
}

int32 RIPCALL DongleFeatureLevel(void)
{
  return ( featureLevel );
}

int32 RIPCALL DonglePostScriptDenied(void)
{
  return fPostScriptDenied;
}

int32 RIPCALL DonglePDFDenied(void)
{
  return fPDFDenied;
}

int32 RIPCALL DongleHPSDenied(void)
{
  return fHPSDenied;
}

int32 RIPCALL DongleXPSDenied(void)
{
  return fXPSDenied;
}

int32 RIPCALL DongleApplyWatermark(void)
{
  return fApplyWatermark;
}

void RIPCALL setTBUBsecurityNo( int32 tbubSecNo )
{
  tbubSecurityNumber = tbubSecNo;
}

int32 RIPCALL SecRevisionPasswordKey(void)
{
  return revisionPasswordKey;
}

void RIPCALL SecSetRevisionPasswordKey(int32 key)
{
  revisionPasswordKey = key;
}

static int32  lastTimeCalled = 0;
static uint32 lastCode = CODEINITIALISER;
/* Check the current security mechanism for a valid security mechanism. */
uint32 doSecurityCheck (uint32 code, uint32 val[3])
{
  uint32        currentTime;
  uint32        tempval;
  int32         fFull;

  /* check code is same as code'' returned last time */
  tempval = randomiseInScrty(lastCode, FALSE);
  /* order of evaluation not defined for ^ */
  if ( tempval ^ (lastCode = randomiseInScrty(code, TRUE)) )
  {
    /* Code is not the same as last time */
    lastCode ^= CODESCRAMBLE;
    return ( code ^ CODESCRAMBLE );
  }
  else
  {
    /*
       At this point check the security device and put the
       results into val[0] and val[1].

       val[0] is made up of four bytes, [1-4] from dongle_array,
       see Dongle protection mechanism document for details.

       val[1] should have the value 1 if the test of the security
       mechanism indicated that the device was present and
       one of ours. No test of the dongle/licence server
       programming is done only that it is a real valid device
       (i.e. one of our dongles).
    */
    /* If we have a real dongle, check it here. */

    currentTime = (uint32)get_rtime();

    /* Work out whether we need to do a full dongle check or not.
     * A full check is done if a time of FULLCHECKPERIOD in
     * get_rtime units (milliseconds) has elapsed since the last one (and
     * obviously a full check is done first time round).
     *
     * The return of get_rtime is signed, and wraps around every
     * 50 days. By treating the number as unsigned, the test becomes
     * simple. When the number does wrap, one extra security test will
     * be performed. This is not a real problem.
     */
    fFull = lastTimeCalled == 0 || (currentTime - lastTimeCalled > FULLCHECKPERIOD);

    if (performSecurityCheck(fFull, currentTime))
    {
      if (fFull)
      {
        lastTimeCalled = currentTime;
      }
      /* set up the returned array with the appropriate stuff */
      val[0] = ( productCode & 0xFF ) |
           ( (productVersion & 0xFF) << 8 ) |
           ( ( ( upperResolutionLimit + 99 ) / 100 ) << 16 ) |
           ( ( (customerID & 0x7F) |
               ( ( demonstrationOnly != FULLPRODUCT ) ? 0x80 : 0x00 ) ) << 24 );
      val[1] = 1;
      val[2] = securityNumber ;
    }
    else
    {
      /* security check failed - put garbage into the returned array */
      val[0] = val[1] = (uint32)fullTestSecurityMask;
    }
    val[0] ^= lastCode;
    val[1] ^= lastCode;
    val[2] ^= lastCode;
    return ( lastCode = randomiseInScrty(lastCode, TRUE) );
  }
}

/*
 * This function _might_ be called by the RIP's skin code to force an early
 * security check before the RIP itself boots, however this cannot be relied upon.
 */
int32 RIPCALL forcedFullTestSecurityDevice (void)
{
  return performSecurityCheck(TRUE, INITIAL_CHECK_SEED);
}

static uint32  Rb = (0x80000000 & 0);
/* Randomise function called from within securityCheck in security.c only. */
STATIC uint32 randomiseInScrty (uint32 val, uint32 flag)
{
  uint32 Rc;

  Rc = (val >> 1) | Rb;
  if (flag)
  {
    Rb = (val & 1) << 31;
  }
  Rc = Rc ^ (val << 12);
  val = Rc ^ (Rc >> 20);

  return (val);
}

/* Update matching function in ETmars-licence-c!src:dngfns.c
 * when changing this function.
 */
static int32 fFirstTime = TRUE;
static uint32 savedCheckCode = 0;
STATIC int32 performSecurityCheck(int32 fFull, uint32 seed)
{
  uint32 result;
  uint32 mask;

  if (fFirstTime)
  {
    fFull = TRUE; /* force a full test whatever has been requested */
  }

  if (fFull)
  {
    uint32 code = seed;

    result = fullTestSecurityDevice((int32 *)&code,
                                    aTestSecDevParams);
    /* The full test modifies the value of 'code' to be the result of the
     * expression below. Test if the return values have been tweaked, and also test if the
     * result of this expression is different from the first time it was calculated.
     */
    if (code != (uint32)(customerID ^
                         productCode ^
                         productVersion ^
                         demonstrationOnly ^
                         resolutionLimit ^
                         securityNumber ^
                         featureLevel ^
                         protectedPlugins ^
                         fPostScriptDenied ^
                         fPDFDenied ^
                         fHPSDenied ^
                         fXPSDenied ^
                         fApplyWatermark ^
                         fTeamWork))
      return FALSE;

    if (fFirstTime)
    {
      savedCheckCode = code;
      fFirstTime = FALSE;
    }
    if (savedCheckCode != code)
      return FALSE;
  }
  else
  {
    result = testSecurityDevice((int32 *)&seed,
                                aTestSecDevParams);
  }
  mask = (uint32)(fFull ? fullTestSecurityMask : testSecurityMask);
  result = mask ^ result;

  /* Test if either the return from the test function has been messed with  */
  if (seed != result)
    return FALSE;

  /* Test if the TeamWork-ness of the security device is correct */
  if (customerID != 0)
  {
    /* Not global */
    int32 ripVersion = CustomerRipVersionNumber();
    HqBool fOldDongle = (productVersion < ripVersion || (productVersion >= 25 && productVersion < (ripVersion + 15)));

    if (customerID == 0x0B || !fOldDongle)
    {
      /* Check TeamWork flag for:
       * - any Eval (old Eval without TW flag must not enable RIP)
       * - a dongle for the current RIP version or newer (for older dongles TeamWork-ness is handled by the revision password)
       */
      if (!fTeamWork)
      {
        /* Require TeamWork flag to be set */
        return FALSE;
      }
    }
  }

  lowerResolutionLimit = 0;
  if ( IS_LITE_RIP )
  {
    upperResolutionLimit = res_limit[0][(unsigned)resolutionLimit & 0xf];
    xPixelWidth = res_limit[1][((unsigned)resolutionLimit & 0xF0) >> 4];
  }
  else
  {
    upperResolutionLimit = resolutionLimit & 0xFFFF ;
  }
  return TRUE;
}


/* --- Functions to check permissions for various RIP features --- */

/* ---------------------------------------------------------------------- */
/* Attempts to crack security by brute force search are prevented by forcing
 * failure if the key is being set too often to different wrong values.
 */





int32 RIPCALL SWpermission( uint32 nSecurity, int32 nFeature)
{
  return SWext_permission( nSecurity, nFeature, NULL );
}

int32 RIPCALL SWext_permission( uint32 nSecurity, int32 nFeature, int32 * pfTrial)
{
  int32   result = FALSE;

  /* Give permission for the revision password feature if we don't need
   * to check it.
   */
  if( REVISION_PASSWORD_KEY == nFeature || REVISION_PASSWORD_KEY2 == nFeature ) {
    int32         fRevisionNeedsCheck = FALSE;

    SECNeedRevisionPasswordCheck(fRevisionNeedsCheck);
    if( !fRevisionNeedsCheck ) {
      return TRUE;
    }
  }

  /* Give permission for the platform upgrade password feature if we don't need
   * to check it.
   */
  if( PLATFORM_PASSWORD_KEY == nFeature ) {
    int32         fPlatformNeedsCheck = FALSE;

    SECNeedPlatformUpgradePasswordCheck(fPlatformNeedsCheck);
    if( !fPlatformNeedsCheck ) {
      return TRUE;
    }
  }

  /* Try to put loads of code in here so that it is hard to disassemble. */
  if (secDevCanEnableFeature(nFeature))
  {
    HqBool fEnabled = FALSE;

    if (secDevEnablesFeature(nFeature, &fEnabled, pfTrial))
    {
      result = fEnabled;
    }
    else
    {
      HQFAIL("Failed to check feature enabledness");
    }
  }
  else
  {
    uint8 * pTable = NULL;

    switch ( nFeature ) {
    case GNKEY_FEATURE_HDS:
      pTable = aHds;
      break ;
    case GNKEY_FEATURE_HMS:
      pTable = aHms;
      break;
    case GNKEY_FEATURE_HCS:
      pTable = aHcs;
      break ;
    case GNKEY_FEATURE_HCMS:
      pTable = aHcms;
      break ;
    case GNKEY_FEATURE_CAL:
      pTable = aCalibrationSecTable;
      break ;
    case GNKEY_REVISION_PASSWORD:
      HQFAIL( "GNKEY_REVISION_PASSWORD should not be used any more" );
      pTable = aMajorRevisionSecTable;
      break;
    case GNKEY_FEATURE_IDLOM:
      pTable = aIDLOMSecurityTable;
      break;
    case GNKEY_FEATURE_TRAP_PRO_LITE:  /* Was GNKEY_FEATURE_EASYTRAP */
      pTable = aEasyTrapSecTable;
      break;
    case GNKEY_FEATURE_TIFF_IT:
      pTable = aTIFF_ITSecTable;
      break;
    case GNKEY_FEATURE_HDSLOWRES:
      pTable = aHDSLOWSecTable;
      break;
    case GNKEY_FEATURE_COLOR_PRO:  /* was GNKEY_FEATURE_ICC */
      pTable = aICCSecurityTable;
      break;
    case GNKEY_FEATURE_HCMS_LITE:
      pTable = aHCMSLiteSecurityTable;
      break;
    case GNKEY_FEATURE_HCEP:
      pTable = aHCEPSecurityTable;
      break;
    case GNKEY_FEATURE_2THREAD:
      pTable = aTwoThreadSecTable;
      break;
    case GNKEY_FEATURE_TIFF6:
      pTable = aTIFF6SecTable;
      break;
    case GNKEY_FEATURE_PDFOUT:
      pTable = aPDFOutSecTable;
      break;
    case GNKEY_FEATURE_POSTSCRIPT:
      pTable = aPostScriptSecTable;
      break;
    case GNKEY_FEATURE_PDF:
      pTable = aPDFSecTable;
      break;
    case GNKEY_FEATURE_MEDIASAVING:
      pTable = aMediaSavingSecTable;
      break;
    case GNKEY_FEATURE_MPSOUTLINE:
      pTable = aMPSOutlineSecTable;
      break ;
    case GNKEY_REV_6_PASSWORD:
      pTable = aRev6SecTable;
      break ;
    case GNKEY_FEATURE_TRAP_PRO:
      pTable = aTrapProSecTable;
      break ;
    case GNKEY_PLATFORM_PASSWORD:
  #ifdef PLATFORM_IS_64BIT
      HQFAIL("Should not be checking GNKEY_PLATFORM_PASSWORD in 64-bit executable");
  #endif
      pTable = aPlatformUpgradeSecTable;
      break ;
    case GNKEY_FEATURE_HPS:
      pTable = aHpsSecTable;
      break;
    case GNKEY_FEATURE_SIMPLE_IMPOSITION:
      pTable = aSimpleImpositionSecTable;
      break;
    case GNKEY_REV_7_PASSWORD:
      pTable = aRev7SecTable;
      break;
    case GNKEY_FEATURE_HXM:
      pTable = aHxmSecTable;
      break;
    case GNKEY_FEATURE_XPS:
      pTable = aXPSSecTable;
      break;
    case GNKEY_FEATURE_APPLY_WATERMARK:
      pTable = aApplyWatermarkSecTable;
      break;
    case GNKEY_REV_8_PASSWORD:
      pTable = aRev8SecTable;
      break;
    case GNKEY_REV_9_PASSWORD:
      pTable = aRev9SecTable;
      break;
    case GNKEY_REV_TW2_PASSWORD:
      pTable = aRevTW2SecTable;
      break;
    case GNKEY_FEATURE_MTC:
      HQFAIL("MTC is obsolete");
      pTable = aMTCSecTable;
      break;
    case GNKEY_FEATURE_HXMLOWRES:
      pTable = aHxmLowSecTable;
      break;
    case GNKEY_FEATURE_PIPELINING:
      pTable = aPipeliningSecTable;
      break;
    case GNKEY_FEATURE_64_BIT:
  #ifndef PLATFORM_IS_64BIT
      HQFAIL("Should not be checking GNKEY_FEATURE_64_BIT in 32-bit executable");
  #endif
      pTable = a64BitSecTable;
      break;
    case GNKEY_REV_MR3_PASSWORD:
      pTable = aRevMR3SecTable;
      break;
    case GNKEY_FEATURE_HVD_EXTERNAL:
      pTable = aHvdExternalSecTable;
      break;
    case GNKEY_FEATURE_HVD_INTERNAL:
      pTable = aHvdInternalSecTable;
      break;
    case GNKEY_FEATURE_32_BIT:
      pTable = a32BitSecTable;
      break;
    case GNKEY_REV_MR4_32_PASSWORD:
      pTable = aRev32MR4SecTable;
      break;
    case GNKEY_REV_MR4_64_PASSWORD:
      pTable = aRev64MR4SecTable;
      break;
    case GNKEY_FEATURE_TOOTHPICK:
    default:
      HQFAIL("SWpermission: unknown security key.");
      result = FALSE ;
      break ;
    }
    if (pTable != NULL) {
      HQASSERT(pTable[0] != 15,
               "Invalid security table");
      result = permission(nSecurity, pTable, nFeature, pfTrial);
    }
  }

  return ( result ) ;
}

int32 RIPCALL SWsubfeature_permission(uint32 nSecurity,
                                      int32  nFeature,
                                      char * pbzFeatureStr)
{
  return SWext_subfeature_permission( nSecurity, nFeature, pbzFeatureStr, NULL );
}

int32 RIPCALL SWext_subfeature_permission(uint32 nSecurity,
                                          int32  nFeature,
                                          char * pbzFeatureStr,
                                          int32 * pfTrial)
{
  int32        result = FALSE;
  uint8      * pTable = NULL;
  uint8        aTable [16];

  HQASSERT(strlen(pbzFeatureStr) >= 2,
           "Sub-feature name too short");

  if (secDevCanEnableFeature(nFeature))
  {
    HqBool fEnabled = FALSE;

    if (secDevEnablesSubfeature(nFeature, pbzFeatureStr, &fEnabled, pfTrial))
    {
      result = fEnabled;
    }
    else
    {
      HQFAIL("Failed to check subfeature enabledness");
    }
  }
  else
  {
    switch (nFeature) {
    case GNKEY_FEATURE_SUBFEATURE:
      pTable = aSubFeatureSecTable;
      break;
    case GNKEY_FEATURE_PLEX:
      pTable = aHCPSecTable;
      break;
    case GNKEY_FEATURE_CORE_MODULE:
      pTable = aCoreModuleSecTable;
      break;
    case GNKEY_FEATURE_MAX_THREADS_LIMIT:
      pTable = aMaxThreadsLimitSecTable;
      break;
    case GNKEY_FEATURE_TOOTHPICK:
    default:
      HQFAIL("SWsubfeature_permission: unknown security key.");
      break ;
    }
    if (pTable != NULL) {
      HQASSERT(pTable[0] == 15,
                "Invalid table for this feature");
      /* Now perturb the table by the string given. Note that this table
        * must begin with 15, and no other features should have 15 as their
        * first number. This prevents sub-features from accidently assuming
        * the same table as a regular feature.
        */
      PERTURBKEYTABLE(pTable, aTable, pbzFeatureStr);
      result = permission(nSecurity, aTable, nFeature, pfTrial);
    }
  }

  return result;
}

typedef struct attemptcheck_tt {

  /* credit rating, permission() will fail change of keys if -ve */
  int32    Credit;

  /* previous non zero values set to for each FEATURE */
  int32    lastTime;

  uint32   lastSecurity;
  uint32   oldSecurity;
}attemptcheck_t;



static attemptcheck_t attempt[GNKEY_MAX];
static int32  initialised = FALSE; /* wince */
STATIC int32 permission (uint32 nSecurity, uint8 aTable [16], int32 nFeature, int32 * pfTrial)
{
  /* Average period required between settin key to different wrong values */
  #define ATTEMPT_PERIOD          (60 * 1000)     /* 1 minute */

  /* Extra initial allowance of attempts */
  #define INITIAL_ATTEMPTS        10

  /* Credit is kept in terms of (attempts left * ATTEMPT_PERIOD)
   * and bounded to the range [ MIN_CREDIT, MAX_CREDIT ]
   * ie attempts left is in the range -1 to INITIAL_ATTEMPTS.
   * Credit is only used up when the trying to set the key to a wrong value,
   * and one that is different from the previous attempt.
   */
  #define INITIAL_CREDIT          (INITIAL_ATTEMPTS * ATTEMPT_PERIOD)
  #define MAX_CREDIT              INITIAL_CREDIT
  #define MIN_CREDIT              (-ATTEMPT_PERIOD)

  attemptcheck_t* p_attempt;
  uint32        nDongle;
  uint32        nSecurityMode;
  uint32        nTestAgainst;
  int32         i;
  int32         oldCredit;

  HQASSERT(nFeature >= 0 && nFeature < GNKEY_MAX && aTable != NULL,
           "permission: bad args" );

  if(initialised == FALSE) {
    for (i = 0,p_attempt = attempt; i < GNKEY_MAX; i++,p_attempt++ ) {
      p_attempt->lastSecurity = 0;
      p_attempt->Credit = INITIAL_CREDIT;
      p_attempt->lastTime = 0;
      p_attempt->oldSecurity = 0;
    }
    initialised = TRUE;
  }

  p_attempt = attempt + nFeature ;
  oldCredit = p_attempt->Credit;

  /* Top 8 bits are used as security method. */
  /* Bottom 24 bits are used as security number (& checksum). */
  nSecurityMode = ( nSecurity >> 24 ) & 0xFF ;
  nSecurity &= 0x00FFFFFF ;

  /* turn off with no for special case zero */
  if (nSecurity == 0 && nSecurityMode == 0)
    return FALSE;

  /* if trying to change key adjust and check attempt credit */
  if ( nSecurity != p_attempt->lastSecurity )
  {
    int32     now;
    int32     diff;
    /* last time attempt was made to change key to different value */

    p_attempt->oldSecurity = p_attempt->lastSecurity;
    p_attempt->lastSecurity = nSecurity;
    now = get_rtime();
    diff = now - p_attempt->lastTime;
    p_attempt->lastTime = now;

    /* prevent credit wraparounds */
    if ( diff < 0 || diff > INITIAL_CREDIT )
      diff = MAX_CREDIT;

    /* credit caller for delay since previous change call */
    p_attempt->Credit += diff;

    /* levy standard charge for key change */
    p_attempt->Credit -= ATTEMPT_PERIOD;

    /* enforce credit lower bound */
    if ( p_attempt->Credit < MIN_CREDIT )
      p_attempt->Credit = MIN_CREDIT;

    /* enforce credit upper bound */
    if ( p_attempt->Credit > MAX_CREDIT )
      p_attempt->Credit = MAX_CREDIT;

    /* fail if credit negative */
    if ( p_attempt->Credit < 0 )
      return FALSE;
  }

  nDongle = 0 ;

  if( nSecurityMode == 0x00 ) {
    nDongle = DongleSecurityNo() ;

  } else {

    if (( nSecurityMode & 0x08 ) == 0x08 ) {
      /* This is the OS; MacOS, WinNT, Win3.1, Unix,... */
      /* It is inverted because unfortunately, MacOS == 0x00 */
      nDongle |= ((( DonglePlatformID()  & 0xE0 ) ^ 0xE0 ) << 8 ) ;  /* 3 bits worth. */
    }

    if (( nSecurityMode & 0x04 ) == 0x04 ) {
      /* This is the platform; Mac, PowerMac, Intel, Alpha, Sparc,... */
      nDongle |= (( DonglePlatformID()   & 0x1F ) << 8 ) ;  /* 5 bits worth. */
    }

    if (( nSecurityMode & 0x01 ) == 0x01 ) {
      /* This is the Lite or Full'ness of the RIP */
      nDongle |= (( DongleFeatureLevel() & 0x01 ) << 0 );  /* 1 bits worth. */
    }

    /* This is the (unique) customer number: always check this */
    nDongle |= (( DongleCustomerNo()   & 0x7F ) << 1 ) ;  /* 7 bits worth. */
  }

  for (i = 0; i < 3; i++) {

    /* the loop avoids including the big macro twice */
    GENERATEKEY(nTestAgainst, nDongle, aTable, nFeature);

    if (nSecurity == nTestAgainst)
    {
      /* Dont use up credit for setting correct key */
      p_attempt->Credit = oldCredit;

      if( pfTrial )
      {
        *pfTrial = ( i == 1 );  /* TRUE if feature enabled by TBUB security no */
      }

      return TRUE;
    }

    if( i == 0 )
    {
      /* Try next with any try-before-you-buy security no */
      if( tbubSecurityNumber != -1 )
      {
        nDongle = tbubSecurityNumber;
      }
    }
    else if( i == 1 )
    {
      /* Finally try with magic number */

      /* Only global dongles can run HCMS with a magic number. */
      if( (GNKEY_FEATURE_HCMS == nFeature) && (0 != DongleCustomerNo()) ) {
        return FALSE;
      }

      /* Demo, eval and global dongles are allowed to run with a suitable magic number,
      */
      if (DongleDemoVersion () || DongleCustomerNo() == 0 ||
          DongleCustomerNo() == 0x0b)
        nDongle = 60961;
    }
  }

  return FALSE;
}

/* --- END OF "Functions to check permissions for various RIP features" --- */

/*
 * Horrible hack to remove warning about static defined vars not used.
 * Security data is defined in v20key!export:gentab.h as static char arrays,
 * so they are provate to any file that includes them.  Any arrays not used
 * then give the above warning.  Willing to accept better ways of supressing
 * the warnings.
 */
void J8_k27qxy(
  uint8**       pcp2,
  uint8**       pmst,
  uint8**       pchc)
{
  *pcp2 = aCp2rshSecurityTable;
  *pmst = aMiramarSecTable;
  *pchc = aChcS8452SecurityTable;
}

void init_C_globals_security(void)
{
  /* These are set by the skin before RIP startup - so don't reset
     them at RIP startup.

  customerID = -1;
  productCode = -1;
  productVersion = -1;
  demonstrationOnly = (int32)0xFFFFFFFF;
  resolutionLimit = (int32)0xFFFFFFFF;
  xPixelWidth = 0;
  upperResolutionLimit = 0;
  lowerResolutionLimit = 0;
  securityNumber = (int32)0xFFFFFFFF;
  featureLevel = -1;
  protectedPlugins = (int32)0xFFFFFFFF;
  fPostScriptDenied = FALSE;
  fPDFDenied = FALSE;
  fHPSDenied = FALSE;
  fXPSDenied = FALSE;
  fApplyWatermark = FALSE;
  fTeamWork = FALSE;
  tbubSecurityNumber = -1;
  revisionPasswordKey = 0;
  */

  /* C statics which have been pulled from functions and need to be
     reset. */
  lastTimeCalled = 0;
  lastCode = CODEINITIALISER;
  Rb = (0x80000000 & 0);
  fFirstTime = TRUE;
  savedCheckCode = 0;
  HqMemZero(attempt, GNKEY_MAX);
  initialised = FALSE;
}

/* Log stripped */
