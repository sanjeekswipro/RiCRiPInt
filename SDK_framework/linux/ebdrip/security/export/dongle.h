/* $HopeName: SWsecurity!export:dongle.h(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __DONGLE_H__
#define __DONGLE_H__

/*
Log stripped */


/* -------- Includes -------- */

#include "std.h"    /* MACRO_START */

#if defined(NON_RIP_SECURITY) || defined(CORESKIN)
#include "fwstring.h"   /* FwStrRecord */
#endif

/* -------- Macros -------- */

/* Utility macros for those below. */
/* Get the values in bits FirstBit..LastBit */
#define GET_BITS_N(InputValue, FirstBit, LastBit, BitsInValue, OutputValue) MACRO_START \
  (OutputValue) = getBitsN( (InputValue), (FirstBit), (LastBit), (BitsInValue) ); \
MACRO_END

/* Set the value in FirstBit..LastBit */
#define SET_BITS_N(OutputValue, FirstBit, LastBit, BitsInValue, InputValue) MACRO_START \
  (OutputValue) = setBitsN( (OutputValue), (FirstBit), (LastBit), (BitsInValue), (InputValue) ); \
MACRO_END


#define GET_BITS(InputValue, FirstBit, LastBit, OutputValue) MACRO_START \
  GET_BITS_N(InputValue, FirstBit, LastBit, 16, OutputValue); \
MACRO_END

#define SET_BITS(OutputValue, FirstBit, LastBit, InputValue) MACRO_START \
  SET_BITS_N(OutputValue, FirstBit, LastBit, 16, InputValue); \
MACRO_END


/* --- Macros to read/write the values stored in the dongle */

/* Traditional bit usage
 *
 * byte \ bit    7   6   5   4   3   2   1   0
 *  0          <----------- flags ------------->
 *  1          <---------- product ------------>   aka platform ID
 *  2          <- FL -> <------ version ------->   FL = feature level
 *  3          <------ resolution limit ------->
 *  4          Demo <------ customer no ------->
 *  5          <----- security no (high) ------>
 *  6          <------ security no (lo) ------->
 */


/* Extended bit usage
 *
 * Although traditionally only 8 bits per value have been used the values
 * passed to the dongle test functions are 32-bit.  Many of the dongle
 * types have 16-bit cells and we can be sure that the top 8 bits will
 * have set to 0, so it is possible to start using the high byte in those
 * dongles.
 *
 * However setting bits in the high byte would cause asserts to fire when
 * running old rips, so the strategy is to use a different 7 cells in the
 * dongle for the 16-bit values.  Both the 8-bit and 16-bit values will
 * be programmed.  If the dongle should not enable rips currently in the
 * field for some reason then the "BREAK_OLD_RIPS" flag should be set in
 * the 8-bit data.
 * 
 * word \ bit   15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
 *  0          <---------------------------- flags ---------------------------->
 *  1          <----------- unused -----------> <---------- product ----------->   aka platform ID
 *      or     <----------------------- platform flags ------------------------>   if PRODUCT_CODE_USES_BITS
 *  2          <----- unused -----> Dongle type <- FL -> <------ version ------>      FL = feature level
 *  3          <-- customer no (16 bit, hi) --> <------ resolution limit ------>
 *  4          <-- customer no (16 bit, lo) --> Demo <-- customer no (7 bit) -->
 *  5          <---------------------- security no (high) --------------------->
 *  6          <------ res limit (high) ------> <------ security no (lo) ------>
 */


/* Byte 0 */
#define DONGLE_GET_PROTECTED_PLUGINS(aDongle, Value)    GET_BITS((aDongle)[0], 0, 0, (Value))
#define DONGLE_SET_PROTECTED_PLUGINS(aDongle, Value)    SET_BITS((aDongle)[0], 0, 0, (Value))
#define DONGLE_GET_POSTSCRIPT_DENIED(aDongle, Value)    GET_BITS((aDongle)[0], 1, 1, (Value))
#define DONGLE_SET_POSTSCRIPT_DENIED(aDongle, Value)    SET_BITS((aDongle)[0], 1, 1, (Value))
#define DONGLE_GET_PDF_DENIED(aDongle, Value)           GET_BITS((aDongle)[0], 2, 2, (Value))
#define DONGLE_SET_PDF_DENIED(aDongle, Value)           SET_BITS((aDongle)[0], 2, 2, (Value))
#define DONGLE_GET_GUI_DENIED(aDongle, Value)           GET_BITS((aDongle)[0], 3, 3, (Value))
#define DONGLE_SET_GUI_DENIED(aDongle, Value)           SET_BITS((aDongle)[0], 3, 3, (Value))
/* #define DONGLE_BIT_4_CANT_BE_REUSED(aDongle, Value)  GET_BITS((aDongle)[0], 4, 4, (Value))   Can't reuse bit 4 */
/* #define DONGLE_BIT_4_CANT_BE_REUSED(aDongle, Value)  SET_BITS((aDongle)[0], 4, 4, (Value))   Can't reuse bit 4 */
#define DONGLE_GET_WATERMARK(aDongle, Value)            GET_BITS((aDongle)[0], 5, 5, (Value))
#define DONGLE_SET_WATERMARK(aDongle, Value)            SET_BITS((aDongle)[0], 5, 5, (Value))
#define DONGLE_SET_USE_TIMER(aDongle, Value)            SET_BITS((aDongle)[0], 6, 6, (Value))
#define DONGLE_GET_USE_TIMER(aDongle, Value)            GET_BITS((aDongle)[0], 6, 6, (Value))
#define DONGLE_SET_BREAK_OLD_RIPS(aDongle, Value)       SET_BITS((aDongle)[0], 7, 7, (Value))
#define DONGLE_GET_BREAK_OLD_RIPS(aDongle, Value)       GET_BITS((aDongle)[0], 7, 7, (Value))
#define DONGLE_GET_PRODUCT_CODE_USES_BITS(aDongle, Value) GET_BITS((aDongle)[0], 8, 8, (Value))
#define DONGLE_SET_PRODUCT_CODE_USES_BITS(aDongle, Value) SET_BITS((aDongle)[0], 8, 8, (Value))
#define DONGLE_GET_LLV(aDongle, Value)                  GET_BITS((aDongle)[0], 9, 9, (Value))
#define DONGLE_SET_LLV(aDongle, Value)                  SET_BITS((aDongle)[0], 9, 9, (Value))
#define DONGLE_GET_HPS_DENIED(aDongle, Value)           GET_BITS((aDongle)[0], 10, 10, (Value))
#define DONGLE_SET_HPS_DENIED(aDongle, Value)           SET_BITS((aDongle)[0], 10, 10, (Value))
#define DONGLE_GET_XPS_DENIED(aDongle, Value)           GET_BITS((aDongle)[0], 11, 11, (Value))
#define DONGLE_SET_XPS_DENIED(aDongle, Value)           SET_BITS((aDongle)[0], 11, 11, (Value))
#define DONGLE_GET_APPLY_WATERMARK(aDongle, Value)      GET_BITS((aDongle)[0], 12, 12, (Value))
#define DONGLE_SET_APPLY_WATERMARK(aDongle, Value)      SET_BITS((aDongle)[0], 12, 12, (Value))
#define DONGLE_GET_TEAMWORK(aDongle, Value)             GET_BITS((aDongle)[0], 13, 13, (Value))
#define DONGLE_SET_TEAMWORK(aDongle, Value)             SET_BITS((aDongle)[0], 13, 13, (Value))
#define DONGLE_GET_HI_RES_LIMIT(aDongle, Value)         GET_BITS((aDongle)[0], 14, 14, (Value))
#define DONGLE_SET_HI_RES_LIMIT(aDongle, Value)         SET_BITS((aDongle)[0], 14, 14, (Value))

#define DONGLE_GET_UNUSED_BITS(aDongle, Value)          GET_BITS((aDongle)[0], 15, 15, (Value))
#define DONGLE_SET_UNUSED_BITS(aDongle, Value)          SET_BITS((aDongle)[0], 15, 15, (Value))


/* Previous usage of Byte 0 - all deprecated
 * The assumption is that all these bits haven't been set for years
 * Mac and PC dongles are still being programmed with bit 4 set, so we can't reuse it
 */
#define DONGLE_GET_ULTRE_FLAG(aDongle, Value)       GET_BITS((aDongle)[0], 0, 0, (Value))      /* deprecated */
#define DONGLE_SET_ULTRE_FLAG(aDongle, Value)       SET_BITS((aDongle)[0], 0, 0, (Value))      /* deprecated */
#define DONGLE_GET_PELBOX_FLAG(aDongle, Value)      GET_BITS((aDongle)[0], 1, 1, (Value))      /* deprecated */
#define DONGLE_SET_PELBOX_FLAG(aDongle, Value)      SET_BITS((aDongle)[0], 1, 1, (Value))      /* deprecated */
#define DONGLE_GET_LASERBUS_FLAG(aDongle, Value)    GET_BITS((aDongle)[0], 2, 2, (Value))      /* deprecated */
#define DONGLE_SET_LASERBUS_FLAG(aDongle, Value)    SET_BITS((aDongle)[0], 2, 2, (Value))      /* deprecated */
#define DONGLE_GET_PCDISKFILE_FLAG(aDongle, Value)  GET_BITS((aDongle)[0], 3, 3, (Value))      /* deprecated */
#define DONGLE_SET_PCDISKFILE_FLAG(aDongle, Value)  SET_BITS((aDongle)[0], 3, 3, (Value))      /* deprecated */
#define DONGLE_GET_PLUGIN_DRIVERS_FLAG(aDongle, Value)  GET_BITS((aDongle)[0], 4, 4, (Value))  /* deprecated */
#define DONGLE_SET_PLUGIN_DRIVERS_FLAG(aDongle, Value)  SET_BITS((aDongle)[0], 4, 4, (Value))  /* deprecated */
#define DONGLE_GET_PROT_SCHEME(aDongle, Value)      GET_BITS((aDongle)[0], 5, 7, (Value))      /* deprecated */
#define DONGLE_SET_PROT_SCHEME(aDongle, Value)      SET_BITS((aDongle)[0], 5, 7, (Value))      /* deprecated */


/* Byte 1 */
#define DONGLE_GET_PRODUCT_CODE(aDongle, Value)     GET_BITS((aDongle)[1], 0, 15, (Value))
#define DONGLE_SET_PRODUCT_CODE(aDongle, Value)     SET_BITS((aDongle)[1], 0, 15, (Value))

/* Bit flags, named for OS_Hardware, used when PLATFORM_CODE_BITS flag is set */
#define DONGLE_PLATFORM_MACOS_POWERMAC                  BIT( 0 )
#define DONGLE_PLATFORM_WINNT_INTEL                     BIT( 1 )
#define DONGLE_PLATFORM_UNIX_SPARC                      BIT( 2 )
#define DONGLE_PLATFORM_UNIX_SGI                        BIT( 3 )
#define DONGLE_PLATFORM_UNIX_INTEL                      BIT( 4 )

#define DONGLE_PLATFORM_MACOS_POWERMAC_PARALLEL         BIT( 8 )
#define DONGLE_PLATFORM_WINNT_INTEL_PARALLEL            BIT( 9 )
#define DONGLE_PLATFORM_UNIX_SPARC_PARALLEL             BIT( 10 )
#define DONGLE_PLATFORM_UNIX_SGI_PARALLEL               BIT( 11 )
#define DONGLE_PLATFORM_UNIX_INTEL_PARALLEL             BIT( 12 )

/* The same information as an array initialiser for bit number -> platform code */
#define DONGLE_PLATFORM_BITS \
  { \
    P_MACOS | P_POWERMAC, \
    P_WINNT | P_INTEL,    \
    P_UNIX  | P_SPARC,    \
    P_UNIX  | P_SGI,      \
    P_UNIX  | P_INTEL,    \
    0,                    \
    0,                    \
    0,                    \
    P_MACOS_PARALLEL | P_POWERMAC, \
    P_WINNT_PARALLEL | P_INTEL,    \
    P_UNIX_PARALLEL  | P_SPARC,    \
    P_UNIX_PARALLEL  | P_SGI,      \
    P_UNIX_PARALLEL  | P_INTEL,    \
    0,                             \
    0,                             \
    0                              \
  }


/* Byte 2 */
#define DONGLE_GET_RIP_VERSION(aDongle, Value)      GET_BITS((aDongle)[2], 0, 5, (Value))
#define DONGLE_SET_RIP_VERSION(aDongle, Value)      SET_BITS((aDongle)[2], 0, 5, (Value))
#define DONGLE_GET_FEATURE_LEVEL(aDongle, Value)    GET_BITS((aDongle)[2], 6, 7, (Value))
#define DONGLE_SET_FEATURE_LEVEL(aDongle, Value)    SET_BITS((aDongle)[2], 6, 7, (Value))
#define DONGLE_GET_DONGLE_TYPE(aDongle, Value)      GET_BITS((aDongle)[2], 8, 10, (Value))
#define DONGLE_SET_DONGLE_TYPE(aDongle, Value)      SET_BITS((aDongle)[2], 8, 10, (Value))

/* Values for dongle type */
#define DONGLE_TYPE_EMPTY                           ( 0 )
#define DONGLE_TYPE_DEMO                            ( 1 )
#define DONGLE_TYPE_LICENSED                        ( 2 )

/* Byte 3 for full RIP
 * If resolution limit is no more than 25500 we divide by 100 and store in byte 3.
 * If resolution is greater than 25500 we divide by 10, store the low 8 bit in
 * byte 3 and the high 8 bits in the top half of byte 6.  This allows a max
 * limit of 655350 dpi.
 */
#define DONGLE_GET_RES_INFO(aDongle, Value) MACRO_START \
  uint32 fHiRes; \
  DONGLE_GET_HI_RES_LIMIT((aDongle), (fHiRes)); \
  if( fHiRes ) { \
    uint32 resLo; \
    uint32 resHi; \
    GET_BITS((aDongle)[3], 0, 7, resLo); \
    GET_BITS((aDongle)[6], 8, 15, resHi); \
    (Value) = ((resHi <<  8) | resLo) * 10; \
  } else { \
    GET_BITS((aDongle)[3], 0, 7, (Value)); \
    (Value) *= 100; \
  } \
MACRO_END
#define DONGLE_SET_RES_INFO(aDongle, Value) MACRO_START \
  if( (Value) > 25500 ) { \
    uint32 resLo = ((Value) / 10) & 0xFF; \
    uint32 resHi = ((Value) / 10) >> 8; \
    DONGLE_SET_HI_RES_LIMIT((aDongle), 1); \
    SET_BITS((aDongle)[3], 0, 7, resLo); \
    SET_BITS((aDongle)[6], 8, 15, resHi); \
  } else { \
    DONGLE_SET_HI_RES_LIMIT((aDongle), 0); \
    SET_BITS((aDongle)[3], 0, 7, (Value) / 100); \
  } \
MACRO_END
/* Byte 3 for lite RIP */
#define DONGLE_GET_MICRORIP_RES_INFO(aDongle, Value) GET_BITS((aDongle)[3], 0, 7, (Value))
#define DONGLE_SET_MICRORIP_RES_INFO(aDongle, resi, widthi) \
   SET_BITS((aDongle)[3], 0, 7, ((resi) | ((widthi) << 4)))


/* Byte 4 */
/* Customer number 
 * Getting:
 * - When the 7-bit customer number == DONGLE_CUSTOMER_NO_IS_16_BIT return the 16-bit value,
 * otherwise return the 7-bit value.
 * Setting:
 * - When the customer number is less than DONGLE_CUSTOMER_NO_IS_16_BIT set the 7-bit customer
 * number to the true value.
 * - When the customer number exceeds DONGLE_CUSTOMER_NO_IS_16_BIT set the 7-bit customer
 * number to DONGLE_CUSTOMER_NO_IS_16_BIT.
 * - In either case set the 16-bit customer number to the true value.
 * - Assert that we never try to use DONGLE_CUSTOMER_NO_IS_16_BIT as the customer number.
 */
#define DONGLE_CUSTOMER_NO_IS_16_BIT 0x7f

#define DONGLE_GET_CUSTOMER_NO(aDongle, Value) MACRO_START \
  GET_BITS((aDongle)[4], 0, 6, (Value)); \
  if( (Value) == DONGLE_CUSTOMER_NO_IS_16_BIT ) \
  { \
    uint32 _lo_, _hi_; \
    GET_BITS((aDongle)[4], 8, 15, (_lo_)); \
    GET_BITS((aDongle)[3], 8, 15, (_hi_)); \
    (Value) = (_hi_ << 8) | _lo_; \
  } \
MACRO_END
#define DONGLE_SET_CUSTOMER_NO(aDongle, Value) MACRO_START \
  HQASSERT( (Value) != DONGLE_CUSTOMER_NO_IS_16_BIT, \
    "Should not be setting customer number to DONGLE_CUSTOMER_NO_IS_16_BIT sentinel value" ); \
  if( (Value) < DONGLE_CUSTOMER_NO_IS_16_BIT ) \
  { \
    SET_BITS((aDongle)[4], 0, 6, (Value)); \
  } \
  else \
  { \
    SET_BITS((aDongle)[4], 0, 6, DONGLE_CUSTOMER_NO_IS_16_BIT); \
  } \
  SET_BITS((aDongle)[4], 8, 15, (Value) & 0xff); \
  SET_BITS((aDongle)[3], 8, 15, (Value) >> 8); \
MACRO_END

#define DONGLE_GET_DEMO_FLAG(aDongle, Value)        GET_BITS((aDongle)[4], 7, 7, (Value))
#define DONGLE_SET_DEMO_FLAG(aDongle, Value)        SET_BITS((aDongle)[4], 7, 7, (Value))


/* Bytes 5 and 6 */
#define DONGLE_GET_SECURITY_NO(aDongle, Value) MACRO_START \
  uint32 _lo_, _hi_; \
  GET_BITS((aDongle)[6], 0, 7, (_lo_)); \
  GET_BITS((aDongle)[5], 0, 15, (_hi_)); \
  (Value) = (_hi_ << 8) | _lo_; \
MACRO_END
#define DONGLE_SET_SECURITY_NO(aDongle, Value) MACRO_START \
  SET_BITS((aDongle)[6], 0, 7, (Value) & 0xff); \
  SET_BITS((aDongle)[5], 0, 15, (Value) >> 8); \
MACRO_END


/* Interpret feature level value */
#define LITE_RIP 1
#define FULL_RIP 0

#define RESULT_ARRAY_SIZE 7


/* LLv locale field bits
 * Dongle will contain a separate field which specifies which
 * LLv locale(s) it enables.  These defines map bits in that
 * field to the possible locales.
 */
#define LLV_LOCALE_SIMPLIFIED_CHINESE               BIT( 0 )
#define LLV_LOCALE_TRADITIONAL_CHINESE              BIT( 1 )
#define LLV_LOCALE_INVERTED_CASE                    BIT( 2 )

#define LLV_ALL_LOCALES                             BITS_BELOW( 3 )
#define LLV_LOCALE_NONE                             0


/* -------------------------------- Functions ------------------------------ */

#define DoCustomerNumberCheck  concatToEOL
int32 DoCustomerNumberCheck(int32 * dongleValues, int32 * customerID, int32 fUpdate);

#define DoPlatformCheck  copyToEOL
int32 DoPlatformCheck(int32 * dongleValues, int32 platform_id, int32 * pRequiresFeature);

/* Get the serial no, returning TRUE <=> success */
#define getDongleSerialNo truncAtEOL
extern int32 getDongleSerialNo( uint32 * pSerialNo );

/* Accessor for resultArray */
extern int32 * getResultArray( void );

/* Some dongles can hold an application set date: YYYYMMDD */
extern int32 getDongleDate( uint32 * pDate );
extern int32 setDongleDate( uint32 date );

/* Accessor for fGUIDenied */
extern int32 getGUIDenied( void );

/* Is this RIP is compiled as an LLv rip
 * For use in non-llv varianted compounds
 */
extern int32 isLLvRip( void );

/* Returns the LLv locale the rip has been built for
 * Value is one of the LLV_LOCALE_XXX defines above
 */
extern uint32 getRipLLvLocale( void );

/* Returns the compiled checksum of the dedicated message file.
 * 0 otherwise. 
 */
#define getLLvMsgChecksum ripIncImageSize
extern uint32 getLLvMsgChecksum(void);

/*
 * Returns TRUE if the attached dongle is of time-expiry type.
 */ 
extern int32 timeExpiryDongle( void );

/*
 * Decrements the count of time remaining in the dongle.  Doesn't
 * return anything because the consequences of the decrement are
 * the subsequent success / failure of dongle tests
 */
extern void decrementTimeExpiringDongle( void );

/*
 * Returns a suitable time in seconds to allow between security
 * check calls
 */
extern uint32 getSecurityCheckPeriod( void );

/*
 * Returns TRUE if dongle provides a license via HLS to run a RIP
 */
extern int32 dongleGrantsHLSLicense( int32 * pResultArray );

/*
 * Returns TRUE if dongle allows use of an HLS permit of the given type for given product
 */
extern int32 donglePermitsPermitType( int32 * pResultArray, char * pProduct, int32 cbProdLen, uint32 permitType );

#ifdef NON_RIP_SECURITY
/* Generates a report in *prInfo about the connected dongle(s) */
extern void getDongleInfo( FwStrRecord * prInfo );
#endif

/* For GET_BITS_N, SET_BITS_N macros */
extern uint32 getBitsN( uint32 InputValue, uint32 FirstBit, uint32 LastBit, uint32 BitsInValue );
extern uint32 setBitsN( uint32 OutputValue, uint32 FirstBit, uint32 LastBit, uint32 BitsInValue, uint32 InputValue );

/* Returns TRUE if secDevEnablesFeature() / secDevEnablesSubfeature()
 * should be used to check for the specified (sub)feature, rather
 * than using the traditional password check.
 */
extern HqBool secDevCanEnableFeature( uint32 nFeature );

/* Reads the password for the specified feature from
 * the current security device, returning TRUE if
 * successful, in which case *pfEnabled is set to
 * TRUE if the feature is enabled.
 * If the feature is enabled for a trial *pfTrial is
 * set to TRUE (pfTrial may be NULL).
 */
extern HqBool secDevEnablesFeature( uint32 nFeature, HqBool * pfEnabled, int32 * pfTrial );

/* Reads the password for the specified subfeature from
 * the current security device, returning TRUE if
 * successful, in which case *pfEnabled is set to
 * TRUE if the subfeature is enabled.
 * If the subfeature is enabled for a trial *pfTrial is
 * set to TRUE (pfTrial may be NULL).
 */
extern HqBool secDevEnablesSubfeature( uint32 nFeature, char * pbzFeatureStr, HqBool * pfEnabled, int32 * pfTrial );

/* Returns TRUE if SafeNet LDK is providing the license for the RIP */
extern HqBool ripLicenseIsFromLDK( void );

/* Determine user's preferred security method(s) */
extern void secDevDetermineMethod( void );

/* Check if a security method should be used */
extern HqBool secDevUseLDK( void );
extern HqBool secDevUseHLS( void );

#ifdef CORESKIN

/* Get ID of security device: key ID, serial no */
extern HqBool secDevGetId( FwStrRecord * prId );

/* Provides the feature number to use for the specified
 * subfeature.
 * Returns TRUE if successful.
 */
extern HqBool secDevFeatureForSubFeature( uint32 nFeature, char * pbzFeatureStr, uint32 * pnKeyfeature );

/* Get any identity string from the security device */
extern HqBool secDevGetRIPIdentity( FwStrRecord * prIdentity );

/* Pass in the name to use in the UI for a feature */
extern void secDevSetFeatureName( uint32 nFeature, FwTextString ptbzName );

/* Returns TRUE if HLS is providing the license for the RIP */
extern HqBool ripLicenseIsFromHLS( void );

/* Install any ptasks for the security device */
extern void installSecurityTasks( void );

/* Does product have "Expiring Licenses" menu item */
extern int32 hasExpiringLicensesMenuItem( void );

#endif

#endif /* protection for multiple inclusion */

/* end of security.h */
