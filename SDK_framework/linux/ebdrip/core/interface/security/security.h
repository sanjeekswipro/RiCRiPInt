/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_security!security.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 */

#ifndef __SECURITY_H__
#define __SECURITY_H__

#include "extscrty.h"       /* obfuscation for CustomerRipNumber */

#ifdef __cplusplus
extern "C" {
#endif

extern int32 RIPCALL forcedFullTestSecurityDevice RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *forcedFullTestSecurityDevice_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL RipPlatformID RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *RipPlatformID_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DonglePlatformID RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DonglePlatformID_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleLevel RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleLevel_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleMaxRipVersion RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleMaxRipVersion_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleMaxResolution RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleMaxResolution_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleMinResolution RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleMinResolution_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleMaxXWidth RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleMaxXWidth_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleCustomerNo RIPPROTO((void));
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleCustomerNo_fn_t) RIPPROTO((void));

extern int32 RIPCALL HqxCryptCustomerNo RIPPROTO((void));
/* Typedef with same signature. */
typedef int32 (RIPCALL *HqxCryptCustomerNo_fn_t) RIPPROTO((void));

extern int32 RIPCALL DongleDemoVersion RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleDemoVersion_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleProtectedPlugins RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleProtectedPlugins_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleSecurityNo RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleSecurityNo_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL HqxCryptSecurityNo RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *HqxCryptSecurityNo_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleFeatureLevel RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleFeatureLevel_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL SECGetMaxYResolution RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *SECGetMaxYResolution_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DonglePostScriptDenied RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DonglePostScriptDenied_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DonglePDFDenied RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DonglePDFDenied_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleHPSDenied RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleHPSDenied_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleXPSDenied RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleXPSDenied_fn_t) RIPPROTO((void)) ;

extern int32 RIPCALL DongleApplyWatermark RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DongleApplyWatermark_fn_t) RIPPROTO((void)) ;

/* This is needed as we can't include custlib.h in this file (as
 * it's included by files that can't access custlib.h
 */
extern int32 RIPCALL DoGetCustomerRipVersionNumber RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *DoGetCustomerRipVersionNumber_fn_t) RIPPROTO((void)) ;

/* Platform password:
 * - allows dongle for one platform to enable RIP for a different platform
 */
#ifdef PLATFORM_IS_64BIT
/* For a 64-bit executable the platform password allows use with a 32-bit dongle */
#define PLATFORM_PASSWORD_KEY GNKEY_FEATURE_64_BIT
#else
/* For a 32-bit executable the platform password allows use with a 64-bit dongle */
#define PLATFORM_PASSWORD_KEY GNKEY_FEATURE_32_BIT
#endif

/* Revision password:
 * - allows dongle for an older version to enable RIP
 * - 32-bit and 64-bit RIPs require different passwords
 * - if you enter the password for the "wrong" architecture
 *   you will also have to enter the PLATFORM_PASSWORD_KEY
 *   password
 */
#ifdef PLATFORM_IS_64BIT
#define REVISION_PASSWORD_KEY  GNKEY_REV_MR4_64_PASSWORD
#define REVISION_PASSWORD_KEY2 GNKEY_REV_MR4_32_PASSWORD
#else
#define REVISION_PASSWORD_KEY  GNKEY_REV_MR4_32_PASSWORD
#define REVISION_PASSWORD_KEY2 GNKEY_REV_MR4_64_PASSWORD
#endif

/* Maps the version number from the dongle to the range
 * used by DoGetCustomerRipVersionNumber().
 * See old versions of dongle.c for the history of this.
 */
#define SECMapDongleVersion(dongleVersion) \
  if(dongleVersion >= 31) \
    dongleVersion -= 15; \
  if(dongleVersion >= 29) \
    dongleVersion -= 3; \
  if(dongleVersion >= 25) \
    dongleVersion -= 12; \

/*
 * SECNeedRevisionPasswordCheck
 *
 * Do we need to check the password for a chargeable revision?
 * Do so unless we have a global or eval dongle, or the RIP's version
 * number is less than the dongle's.
 * This is a macro so that it can't be hacked so easily.
 */
#define SECNeedRevisionPasswordCheck(fNeedsCheck) MACRO_START \
  int32   nCustomerNumber = DongleCustomerNo(); \
  \
  (fNeedsCheck) = TRUE; \
  \
  /* 0 == global dongle, 0x0b == eval dongle. */ \
  if( (0 == nCustomerNumber) || (0x0B == nCustomerNumber) ) { \
    (fNeedsCheck) = FALSE; \
  } else { \
    int32 dongleVersion = DongleMaxRipVersion(); \
    SECMapDongleVersion(dongleVersion); \
    if(DoGetCustomerRipVersionNumber() <= dongleVersion) { \
      (fNeedsCheck) = FALSE; \
    } \
  } \
MACRO_END

/* Returns the key of the revision feature used to enable the RIP,
 * i.e. REVISION_PASSWORD_KEY or REVISION_PASSWORD_KEY2, or 0 if none
 */
extern int32 RIPCALL SecRevisionPasswordKey RIPPROTO((void)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *SecRevisionPasswordKey_fn_t) RIPPROTO((void)) ;

extern void RIPCALL SecSetRevisionPasswordKey RIPPROTO((int32 key)) ;
/* Typedef with same signature. */
typedef void (RIPCALL *SecSetRevisionPasswordKey_fn_t) RIPPROTO((int32 key)) ;

/*
 * SECNeedPlatformUpgradePasswordCheck
 *
 * Do we need to check the password for a chargeable platform upgrade?
 * Do so unless we have:
 * - a global dongle, or
 * - an eval dongle
 * Also unless we have:
 * - a revision password for this platform was used to enable this RIP
 * Also unless we have:
 * - the dongle's platform is the same as the platform we are running on, or
 * - the dongle's platform is the non-MP form of an MP rip's platform, or
 * - the dongle's platform is the 64-bit equivalent of the platform we are running on
 *   and the dongle's version is one newer than the RIP's version, or
 * - the dongle's platform is the non-MP form of the 64-bit equivalent of an MP rip's platform
 *   and the dongle's version is one newer than the RIP's version
 * - the dongle is for the Mac and this is a Windows RIP
 *
 * Re the revision password condition we need to be careful to require a platform upgrade
 * password when the revision password for the "wrong" architecture has been used.
 * We test this by checking the result of SecRevisionPasswordKey():
 * - if it returns 0 then no revision password was needed
 * - if it returns REVISION_PASSWORD_KEY then the password for this architecture was used
 * - if it returns REVISION_PASSWORD_KEY2 then the password for the "wrong" architecture was used
 */

#ifdef PLATFORM_IS_64BIT
#define fSEC64BitRip TRUE
#define PLATFORM_ID_WITH_64BIT            0
#define PLATFORM_ID_WITH_64BIT_WITHOUT_MP 0
#else
#define fSEC64BitRip FALSE
#define PLATFORM_ID_WITHOUT_64BIT        0
#define PLATFORM_ID_WITHOUT_MP_AND_64BIT 0

#ifndef PLATFORM_ID_WITH_64BIT
#define PLATFORM_ID_WITH_64BIT            0
#define PLATFORM_ID_WITH_64BIT_WITHOUT_MP 0
#endif
#endif

#define SECNeedPlatformUpgradePasswordCheck(fNeedsCheck) MACRO_START \
  int32 nCustomerNumber = DongleCustomerNo(); \
  \
  (fNeedsCheck) = TRUE; \
  \
  /* 0 == global dongle, 0x0b == eval dongle. */ \
  if( (0 == nCustomerNumber) || (0x0B == nCustomerNumber) ) { \
    (fNeedsCheck) = FALSE; \
  } else { \
    int32 revisionPasswordKey = SecRevisionPasswordKey(); \
    int32 ripPlatform = RipPlatformID(); \
    int32 donglePlatform = DonglePlatformID(); \
    int32 ripVersion = DoGetCustomerRipVersionNumber(); \
    int32 dongleVersion = DongleMaxRipVersion(); \
    SECMapDongleVersion(dongleVersion); \
    \
    if( dongleVersion >= ripVersion ) { \
      revisionPasswordKey = 0;  /* ignore any upgrade password if not needed */ \
    } \
    \
    if( revisionPasswordKey == REVISION_PASSWORD_KEY ) { \
      (fNeedsCheck) = FALSE; \
    } else if( revisionPasswordKey == 0 ) { \
      if( donglePlatform == ripPlatform ) { \
        (fNeedsCheck) = FALSE; \
      } else { \
        ripPlatform = PLATFORM_ID_WITHOUT_MP; \
        if( donglePlatform == ripPlatform ) { \
          (fNeedsCheck) = FALSE; \
        } else { \
          int32 dongleOS = donglePlatform & PLATFORM_OS_MASK; \
          if( (dongleOS == P_MACOS) && (NORMAL_OS == P_WINNT) ) { \
            (fNeedsCheck) = FALSE; \
          } else { \
            if( ripVersion == dongleVersion - 1 ) { \
              if( !fSEC64BitRip ) { \
                ripPlatform = PLATFORM_ID_WITH_64BIT; \
                if( donglePlatform == ripPlatform ) { \
                  (fNeedsCheck) = FALSE; \
                } else { \
                  ripPlatform = PLATFORM_ID_WITH_64BIT_WITHOUT_MP; \
                  if( donglePlatform == ripPlatform ) { \
                    (fNeedsCheck) = FALSE; \
                  } \
                } \
              } \
            } \
          } \
        } \
      } \
      \
      /* We allow: \
       * - a 64-bit dongle to run a 32-bit rip with a password (password not required if dongle version is one newer than RIP's version) \
       * - a 32-bit dongle to run a 64-bit rip with a password \
       * \
       * (see fullTestSecurityDevice() in SWsecurity!src:dongle.c) \
       * Assert if that is not the situation now */ \
      HQASSERT( (!fNeedsCheck) \
        || (!fSEC64BitRip && (donglePlatform == PLATFORM_ID_WITH_64BIT || donglePlatform == PLATFORM_ID_WITH_64BIT_WITHOUT_MP) && (ripVersion != dongleVersion - 1)) \
        || (fSEC64BitRip && (donglePlatform == PLATFORM_ID_WITHOUT_64BIT || donglePlatform == PLATFORM_ID_WITHOUT_MP_AND_64BIT)), \
        "Revise SECNeedPlatformUpgradePasswordCheck() assert for this platform upgrade"); \
    } \
  } \
MACRO_END


/* Can we use low res HDS?
 */
#define fSECAllowLowResHDS(_ydpi) ((_ydpi) <= 1500)


/* Can we use low res HXM?
 * We can if either resolution is in the range 900-1500 dpi
 */
#define fSECAllowLowResHXM(_xdpi, _ydpi) \
  (((_xdpi) >= 900 && (_xdpi) <= 1500) || ((_ydpi) >= 900 && (_ydpi) <= 1500))


extern int32 RIPCALL SWpermission RIPPROTO((uint32 nSecurity, int32 nFeature)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *SWpermission_fn_t) RIPPROTO((uint32 nSecurity, int32 nFeature)) ;

extern int32 RIPCALL SWext_permission RIPPROTO((uint32 nSecurity, int32 nFeature,
                                                int32 *pfTrial)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *SWext_permission_fn_t) RIPPROTO((uint32 nSecurity, int32 nFeature,
                                                         int32 *pfTrial)) ;

extern int32 RIPCALL SWsubfeature_permission RIPPROTO((uint32 nSecurity, int32 nFeature,
                                                       char *pbzFeatureName)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *SWsubfeature_permission_fn_t) RIPPROTO((uint32 nSecurity, int32 nFeature,
                                                                char *pbzFeatureName)) ;

extern int32 RIPCALL SWext_subfeature_permission RIPPROTO((uint32 nSecurity, int32 nFeature,
                                                           char * pbzFeatureName,
                                                           int32 * pfTrial)) ;
/* Typedef with same signature. */
typedef int32 (RIPCALL *SWext_subfeature_permission_fn_t) RIPPROTO((uint32 nSecurity, int32 nFeature,
                                                                    char * pbzFeatureName,
                                                                    int32 * pfTrial)) ;

extern void RIPCALL setTBUBsecurityNo RIPPROTO((int32 tbubSecNo)) ;
/* Typedef with same signature. */
typedef void (RIPCALL *setTBUBsecurityNo_fn_t) RIPPROTO((int32 tbubSecNo)) ;

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
