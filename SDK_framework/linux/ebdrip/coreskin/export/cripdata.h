#ifndef __CRIPDATA_H__
#define __CRIPDATA_H__
/* $HopeName: SWcoreskin!export:cripdata.h(EBDSDK_P.1) $
 *
 * Header for RIP configuration provider.
 * Copyright (C) 2002-2010 Global Graphics Software.  All rights reserved.
 *
* Log stripped */

#include "coreskin.h"

/* coreskin */
#include "cropdata.h"   /* ConfigureRIPOptions */
#include "crexdata.h"   /* ConfigureRIPExtras */

/* ggcore */
#include "fwstring.h"   /* FwID_Text */

/* pic */
#include "gdevstio.h"   /* DICTSTRUCTION */
#include "gdevtype.h"   /* PluginID */

/* psio */
#include "psioutil.h"   /* dictResult */

/* v20key */
#include "genkey.h"     /* GNKEY_FEATURE_* etc, use with coreguiIsExtrasFeatureAllowed */

#define CRIP_MIN_PRINTER_BUFFER_BANDS   2

#define CRIP_MAX_THREADS                MAXINT
#define CRIP_THREADS_UNLIMITED          0
#define CRIP_MAX_PIPELINING_DEPTH       5

/* Limits on max threads limit, based on knowledge of the passwords we are likely to generate */
#define CRIP_MIN_PASSWORD_LIMIT         0       /* == unlimited */
#define CRIP_MAX_PASSWORD_LIMIT         32

#define CRIP_MIN_NETWORK_BUFFER_IN_K    64

#define CRIP_DEFAULT_BAND_SIZE_IN_K 128
#define CRIP_MIN_PRINTER_BUFFER_IN_K \
 ( CRIP_DEFAULT_BAND_SIZE_IN_K * CRIP_MIN_PRINTER_BUFFER_BANDS )

#define CRIP_MAX_REDUCED_ROAM_FACTOR    65535   /* somewhat arbitrary */
#define CRIP_MAX_REDUCED_ROAM_DIMENSION 65535   /* again, somewhat arbitrary */

#define SECS_PER_MIN 60 /* in case we ever need longer/shorter minutes */

#define RIP_MODE_STRING_DEFAULT UVSA("Multiple (Parallel)")
#define RIP_MODE_SEPARATOR              -1
#define LITE_RIP_MODE_INDEX             3
#define NUM_MODES                       5


/* Dictstructions defined in cripdata.c need be known to gui */

#define CRIP_DS_NONGUI_TOTAL            7

#define CRIP_DS_JOB_TIMEOUT             CRIP_DS_NONGUI_TOTAL + 0
#define CRIP_DS_NETWORK_BUFFER          CRIP_DS_NONGUI_TOTAL + 1
#define CRIP_DS_PAGE_BUFFERING          CRIP_DS_NONGUI_TOTAL + 2
#define CRIP_DS_PGB_DIR                 CRIP_DS_NONGUI_TOTAL + 3
#define CRIP_DS_TMP_DIR                 CRIP_DS_NONGUI_TOTAL + 4
#define CRIP_DS_MAX_THREADS             CRIP_DS_NONGUI_TOTAL + 5
#define CRIP_DS_MAX_THREADS_LIMIT       CRIP_DS_NONGUI_TOTAL + 6
#define CRIP_DS_MAX_PIPELINING_DEPTH    CRIP_DS_NONGUI_TOTAL + 7
#define CRIP_DS_PSIO_TOTAL              CRIP_DS_NONGUI_TOTAL + 8


/* Defines for HCPS allowed options - used with isHCPSOptionAllowed() */

#define HCPS_ANY                        0x0000
#define HCPS_HCPIMPORT          0x0001
#define HCPS_HCPEXPORT          0x0002
#define HCPS_ICCIMPORT          0x0004
#define HCPS_CRDGEN                     0x0008
#define HCPS_CRIMGR                     0x0010
#define HCPS_CSUMGR                     0x0020
#define HCPS_FULLTYPE           0x0040
#define HCPS_LITETYPE           0x0080
#define HCPS_ICCTYPE            0x0100
#define HCPS_CSUSEL         0x0200

/* 'New' color setup types, replacing HCPS_FULLTYPE, HCPS_LITETYPE, HCPS_ICCTYPE */
#define HCPS_NCM_TYPE       0x0400
#define HCPS_FULL_TYPE      0x0800

/* Result codes for cripSetRipMode() */
#define CRIP_SET_RIP_MODE_SUCCESS    0
#define CRIP_SET_RIP_MODE_BAD_MODE   -1
#define CRIP_SET_RIP_MODE_SAVE_ERROR -2

#ifdef __cplusplus
extern "C" {
#endif

/* Entry in the list of protected devices */
typedef struct ConfigureRIPProtectedDevice
{
  listStruct    Entry;
  PluginID      pluginID;
  FwID_Text     atbzPluginName;
  FwID_Text     atbzSecurityName;
  int32         nPassword;
  int32         fTrial;
  int32         nKeyFeature;
} ConfigureRIPProtectedDevice;


/* Combined dialog data */
typedef struct ConfigureRIP
{
  /* data in form required by coregui */

  /* Put smaller items near the beginning for speed
   * Each size group ordered alphabetically.
   */
#define CRIP_COMPRESSION_DEFAULT        7
#define CRIP_COMPRESSION_NONE           -1
  int32         compressionMode;                /* The compression mode */
  int32         jobTimeout;                     /* Job timeout value */
  int32         networkBuffer;                  /* Network buffer size */
  int32         printerBuffer;                  /* Printer buffer size */
  int32         maxThreads;                     /* Maximum # threads in use in core */
  int32         maxThreadsLimit;                /* Limit allowed by password for maxThreads */
  int32         maxPipeliningDepth;             /* Maximum depth allowed for pipelining */
  int32         throughputperiod;               /* TP system polling rate */
  int32         reducedRoamFactor;              /* scaling factor for RR */
  int32         minReducedRoamWidth;            /* min width and height for */
  int32         minReducedRoamHeight;           /* reduced roam window */
  int32         canPrintFileWhenOutputting;     /* allow outputting and printing */

  FwID_Text     pageBuffering;                  /* Rip to disk mode */

  FwTextByte    pgbDirectory[ LONGESTFILENAME ];/* The Page Buffer folder */
  FwTextByte    tmpDirectory[ LONGESTFILENAME ];/* The workspace folder */

  /* a structure for each of the dialogs children */
  ConfigureRIPExtras            extras;
  ConfigureRIPOptions           options;
  ConfigureRIPPluginExtras    * ppluginextras;
  ConfigureRIPProtectedDevice * pProtectedDevices;
  ConfigureRIPProtectedDevice * pProtectedCoreModules;
} ConfigureRIP;

typedef struct RIPMode
{
  int32         number;
  FwTextString  name;
} RIPMode;

typedef struct ImportedRIPpasswords
{
  ConfigureRIPExtras            pwd_extras;
  ConfigureRIPPluginExtras    * pwd_ppluginextras;
  ConfigureRIPProtectedDevice * pwd_pProtectedDevices;
  ConfigureRIPProtectedDevice * pwd_pProtectedCoreModules;
} ImportedRIPpasswords;

extern DictPSIONode     configureRIP_PSIONode;
extern DICTSTRUCTION    cripPSIODictStructions[];
extern FwTextByte       atbzConfigureRIPName[];
extern ConfigureRIP     currentConfigureRIP;
extern FwTextString     ptbzPrepsDirectory;
extern RIPMode          ripModes[];

extern dictResult cripSave( int32 fWarnIfSaveFails );
extern int32 lookupRipMode( ConfigureRIP * pCRIP );
extern int32 cripSetRipMode( int32 ripMode );
extern void coreguiInitConfigureRIP(void);
extern int32 coreguiResetConfigureRIPFactoryDefaults(void);
extern void cripGetNewPluginExtras (ConfigureRIP *pConfRIP);
extern float coreguiGetBandFact(void);

extern void coreguiGetMinDiskFreeInBytes( HqU32x2 * pBytes );
extern uint32 coreguiGetMinMemoryForSysInK(void);
extern uint32 coreguiGetNetworkBufferInK(void);
extern int32 coreguiGetNumThreads(void);
extern uint32 coreguiGetPrinterBufferInK(void);
extern FwTextString coreguiGetStartupPrep(void);
extern int32 coreguiAllowSound(void);

extern uint32 coreguiGetSWMemoryInK(void);
#if defined(MUST_USE_VM_ARENA) || defined(MUST_USE_SHARED_ARENA)
extern uint32 coreguiGetMaxAddrSpaceSizeInK(void);
#endif
#ifdef MUST_USE_VM_ARENA
extern uint32 coreguiGetEmergencySizeInK(void);
extern int32 coreguiGetAllowUseAllMemory(void);
#endif

extern uint32 coreguiGetGrowSizeInK(void);
extern uint32 coreguiGetTotalFreeInK(void);
extern int32 coreguiGetReducedRoamFactor(void);

extern int32 configureRIPChanged(void);

extern int32 coreguiIsStructsExtraFeatureIndirectlyAllowed(
    ConfigureRIPExtras*     pExtras,
    int32                   nFeature,
    int32                 * pfTrial
    );

extern int32 coreguiIsExtrasFeatureAllowed(int32 nFeature);

extern int32 coreguiIsStructsExtrasFeatureAllowed(
    ConfigureRIPExtras*     pExtras,
    int32                   nFeature,
    int32                 * pfTrial
    );

extern int32 coreguiCanPrintFileWhenOutputting(void);

extern void cripUpdateCurrentSWMemory( uint32 ckbSWMemory );

extern int32 coreguiIsPluginExtrasFeatureAvailable(FwTextString atbzDevice);
extern int32 coreguiIsPluginExtrasFeatureAllowed(FwTextString atbzDevice,
                                                 int32 nFeature);

extern int32 isHCPSOptionAllowed(FwTextString atbzDevice, int32 optionRequired);

extern int32 coreguiIsDeviceEnabled( PluginID * pPluginID, FwTextString ptbzPluginName, FwTextString ptbzSecurityName );
extern int32 coreguiCheckProtectedDevice( ConfigureRIPProtectedDevice * pProtectedDevice );
extern int32 coreguiIsProtectedDeviceEnabled( FwTextString ptbzPluginName, FwTextString ptbzSecurityName );

extern int32 coreguiCheckProtectedCoreModule( ConfigureRIPProtectedDevice * pProtectedCM );

extern void copyConfigureRIP( ConfigureRIP *pTo, ConfigureRIP *pFrom );
extern void releaseConfigureRIP( ConfigureRIP * pConfigureRIP );

extern ConfigureRIPPluginExtras * duplicatePluginExtras( ConfigureRIPPluginExtras * pFrom );
extern void releasePluginExtras( ConfigureRIPPluginExtras ** ppPlex );
extern int32 comparePluginExtra( ConfigureRIPPluginExtras* dest, ConfigureRIPPluginExtras* src );
extern void copyPluginExtra( ConfigureRIPPluginExtras* dest, ConfigureRIPPluginExtras* src );

extern ConfigureRIPProtectedDevice * duplicateProtectedDevices( ConfigureRIPProtectedDevice * pFrom );
extern void releaseProtectedDevices( ConfigureRIPProtectedDevice ** ppProtDev );
extern ConfigureRIPProtectedDevice * cripGetProtectedDeviceInfo(ConfigureRIPProtectedDevice * pList, FwTextString ptbzPluginName,
                                     FwTextString ptbzSecurityName, ConfigureRIPProtectedDevice * pInfo );
void cripAddProtectedDeviceToList( ConfigureRIPProtectedDevice ** ppList, ConfigureRIPProtectedDevice * pInfo, int32 pos );
extern int32 cripLoadPasswordsFile( FwTextString ptbzFileName, ImportedRIPpasswords * pPasswords );

#ifdef __cplusplus
}
#endif

#endif /* __CRIPDATA_H__ */
