#ifndef __PWDINFO_H__
#define __PWDINFO_H__

/*
 * $HopeName: DPPpwdinfo!export:pwdinfo.h(EBDSDK_P.1) $
 * Passwords dialog header.
 */
#include "std.h"

/* fwos */
#include "fwstring.h"
#include "fwfile.h"

/* -------- Macros -------- */

#define MAX_INCLUSIVE_PASSWORDS  16

#if defined ( SIMULATE ) && defined( RELEASE_BUILD )
#define NO_PASSWORD_GENERATION
#endif

/* -------- Data -------- */

#define PSWD_BASE            0
#define PSWD_IN_LOG          (PSWD_BASE)
#define PSWD_TYPE_OS         (PSWD_BASE + 1)
#define PSWD_TYPE_PLATFORM   (PSWD_BASE + 2)
#define PSWD_TYPE_RIPLEVEL   (PSWD_BASE + 3)
#define PSWD_ARRAY_SIZE      (PSWD_BASE + 4)
extern int32	afPswdSelection[PSWD_ARRAY_SIZE]; 


/* -------- Types -------- */

/* In order to write out password files with plugin passwords, the following details
 * are required.
 */


typedef struct _PluginPasswordDetails
{
  char * pszFeatureName; /* used by PLEX only */
  char * pszOEMName;     /* used by PLEX only */
  char * pszPluginID;    /* used by DEVICE only */
  char * pszPluginName;  /* used by both */
  char * pszDeviceName;  /* used by both */
} PluginPasswordDetails;


typedef struct _PasswordInfo
{
  char*                   pszShortName;
  char*                   pszLongName;
  char*                   pszProductCode;
  uint8*                  paTable;
  uint32                  nFeature;
  uint32                  nKeyFeature;
  char*                   pszSubFeatureString;
  char*                   postscriptKey;
  uint8*                  aMemorySegment;
  PluginPasswordDetails * pluginDetails;
  char                  * apszInclusivePasswords[MAX_INCLUSIVE_PASSWORDS];
  FwID_Text               abzKeyString;
} PasswordInfo;

/* In ascending order of quality :) */
/*
enum PlatformCode
{
  UNINITIALISED, MAC, WIN, UNIX
};
*/

typedef struct ProductList
{
  int32 nKey;
  uint32 nFeature;
  FwTextString ptbzProduct;
  int32 password;
  struct ProductList* next;
} ProductList;

/* -------- Exported functions -------- */

char * pwdinfoVersion(void);

int32 readNumPasswordInfoEntries(void);

PasswordInfo* readNthPasswordInfoEntry(int32 n);

PasswordInfo* readPasswordInfoGivenProduct(char *product);

char* readHIPPLowResCode(void);

char* readHIPPCode(void);

void freeProductChain(ProductList * pProductList);

void writePasswordFile(FwFile file, ProductList * pProductList);


void PSWDGetPassword(const int32          nKey,
                     const PasswordInfo * pInfo,
                     ProductList       ** ppProductList);

void PSWDGenerateKey(int32          nCustomerNo,
                     int32          fDemo,
                     int32          nPlatformID,
                     int32          RipLevel,
                     int32          nSecurityNo,
                     PasswordInfo * pInfo,
                     int32           afPswdType[PSWD_ARRAY_SIZE],
                     ProductList ** ppProductList);

extern FwFile InitialisePasswordFile( int32 nSecurityNo );

/* Write out the product list, close the password file, and then free the product list */
extern void WriteAndClosePasswordFile(FwFile filehandle,
                                      ProductList ** ppProductList);
int32 UseMagicKey(
  int32                 nCustomerNo,
  int32                 fDemo,
  uint32                nFeature
  );


#endif

/*
* Log stripped */
