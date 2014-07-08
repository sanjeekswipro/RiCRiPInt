#ifndef __CROPDATA_H__
#define __CROPDATA_H__
/*
 * $HopeName: SWcoreskin!export:cropdata.h(EBDSDK_P.1) $
 *
 * Header for RIP configuration provider.
 *
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

#include "coreskin.h"

/* pic */
#include "gdevstio.h"   /* DICTSTRUCTION */

/* ggcore */
#include "fwstring.h"      /* FwTextByte */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* ----------------------- Constants --------------------------------------- */

#if defined(PLATFORM_IS_64BIT)
/* We limit 64 bit machines to 2 TB because these memory limits get
   stored in int32's which ultimately get passed to PS. Limiting to
   2 TB avoids the need for us to change large amounts of historical
   code and is more than large enough. As it happens, this is the
   current limit of maximum memory one can install on a Windows
   machine. */
#define CROP_MAX_MEMORY_IN_K MAXINT32 /* 2^32-1, 2 TB, units are kB */
#else
#define CROP_MAX_MEMORY_IN_K (4 * 1024 * 1024) /* 4 GB, units are kB */
#endif

#define CROP_MIN_BAND_SIZE_IN_K         64
#if defined(CAN_FIND_MEMORY_SIZE) && CAN_FIND_MEMORY_SIZE == 2
#define CROP_MIN_MEMORY_FOR_SYS_IN_K    0
#else
#define CROP_MIN_MEMORY_FOR_SYS_IN_K    512
#endif


/* ----------------- DICTSTRUCTIONs --------------------- */


#ifdef PLATFORM_HAS_VM
#define CROP_DS_MAX_ADDRESS_SPACE_SIZE_PSIO 0
#define CROP_DS_MEMORY_RESERVE_PSIO     1
#define CROP_DS_PSIO_ONLY0              2
#else
#define CROP_DS_PSIO_ONLY0              0
#endif

#ifdef CAN_FIND_MEMORY_SIZE
#define CROP_DS_MIN_MEMORY_FOR_SYS_PSIO CROP_DS_PSIO_ONLY0
#define CROP_DS_PSIO_ONLY1             CROP_DS_PSIO_ONLY0 + 1
#else
#define CROP_DS_PSIO_ONLY1             CROP_DS_PSIO_ONLY0
#endif

#ifdef  NEEDS_SWMEMORY
#define CROP_DS_SW_MEMORY_PSIO          CROP_DS_PSIO_ONLY1
#define CROP_DS_PSIO_ONLY2              CROP_DS_PSIO_ONLY1 + 1
#else
#define CROP_DS_PSIO_ONLY2              CROP_DS_PSIO_ONLY1
#endif

#define CROP_DS_PSIO_ONLY               CROP_DS_PSIO_ONLY2

#define CROP_DS_ALLOW_STOP_START        CROP_DS_PSIO_ONLY
#define CROP_DS_AUTO_PREP_LOAD          CROP_DS_PSIO_ONLY + 1
#define CROP_DS_DISABLE_SOUNDS          CROP_DS_PSIO_ONLY + 2
#define CROP_DS_DISK_FREE               CROP_DS_PSIO_ONLY + 3
#define CROP_DS_STARTUP_PREP_FLAG       CROP_DS_PSIO_ONLY + 4
#define CROP_DS_MIN_BAND_COMPRESS_RATIO CROP_DS_PSIO_ONLY + 5
#define CROP_DS_PER_RENDERER_MEMORY     CROP_DS_PSIO_ONLY + 6
#define CROP_DS_BAND_FACT               CROP_DS_PSIO_ONLY + 7
#define CROP_DS_STARTUP_PREP_FILE       CROP_DS_PSIO_ONLY + 8

#ifdef PLATFORM_HAS_VM
#define CROP_DS_ALL_MEMORY              CROP_DS_PSIO_ONLY + 9
#define CROP_DS_TOTAL0                  CROP_DS_PSIO_ONLY + 10
#else
#define CROP_DS_TOTAL0                  CROP_DS_PSIO_ONLY + 9
#endif

#ifdef  NEEDS_SWMEMORY

#ifdef  CAN_FIND_MEMORY_SIZE
#define CROP_DS_LIMIT_MEMORY            CROP_DS_TOTAL0
#define CROP_DS_TOTAL1                  CROP_DS_TOTAL0 + 1
#else   /* ! CAN_FIND_MEMORY_SIZE */
#define CROP_DS_TOTAL1                  CROP_DS_TOTAL0
#endif  /* ! CAN_FIND_MEMORY_SIZE */

#else   /* ! NEEDS_SWMEMORY */
#define CROP_DS_TOTAL1                  CROP_DS_TOTAL0
#endif  /* ! NEEDS_SWMEMORY */

#define CROP_DS_PSIO_TOTAL              CROP_DS_TOTAL1

/* ----------------------- Types ------------------------------------------- */

/* Combined dialog state */
typedef struct ConfigureRIPOptions
{
  /* data in form required by coregui */

  /* Put smaller items near the beginning for speed
   * Each size group ordered alphabetically.
   */
  int32         allowStopStart;         /* Allow stop start of devices */
  int32         autoPrepLoad;           /* Auto load preps */
  /* setting this to TRUE will change the default for output plugins which dont
   * support the D_GET_RASTER_FORMAT selector from just monochromeBlack to all
   * formats (thus allowing the ues of old color output plugins).
   */
  int32         disableSounds;          /* Disable the sounds */
  int32         diskFree;               /* Disk space to leave free */
  int32         loadPrepOnStartup;      /* Load specified prep on startup */
  float         minBandCompressRatio;   /* for %pagebuffer% */
#ifdef PLATFORM_HAS_VM
  int32         maxAddressSpaceSize;    /* The maximum memory in kB */
  int32         memoryReserve;          /* Extra memory for RIP when needed */
  int32         allowUseAllMemory;      /* Allow RIP to grep all memory */
#endif

#if defined(CAN_FIND_MEMORY_SIZE)
  int32         minMemoryForSys;        /* Min memory to leave for the system */
#endif
  float         perRendererMemory;      /* Band memory per renderer in Mb */
  float         nBandFact;              /* band fact; reduced DLs */
  FwID_Text     startupPrep;            /* Startup prep */

#ifdef NEEDS_SWMEMORY
  int32         SWMemory;               /* Memory available to ScriptWorks */
#ifdef CAN_FIND_MEMORY_SIZE
  int32         limitMemory;            /* Limit to SWMemory? */
#endif
#endif /* NEEDS_SWMEMORY */
} ConfigureRIPOptions;

/* ----------------------- Data -------------------------------------------- */

extern DICTSTRUCTION cropPSIODictStructions[];
extern struct DictPSIONode configureRIPOptionsPSIONode;

/* ----------------------- Functions --------------------------------------- */

extern void cropUpdateMinSWMemory PROTO (( uint32 ckbMinMemory ));

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __CROPDATA_H__ */
/* eof cropdata.h */
