/* Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:ripthread.c(EBDSDK_P.1) $
 */

/** \file
 * \ingroup skinkit
 * \brief Example implementation of corerip integration: Starting the RIP.
 *
 * The StartRip() function sets up the environment for SwDllStart by
 * allocating memory for the rip and gathering together all the
 * devices it requires.
 */

/* ----------------------------------------------------------------------
   Include files and miscellaneous declarations
*/

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#if defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> /* Sleep */
#define sleep(x) Sleep((x) * 1000)
#else
#ifndef __CC_ARM
#include <unistd.h>  /* sleep */
#endif
#endif

#include "hqcstass.h"

#include "hqstr.h"
#include "ripthread.h"

#include "dlliface.h"
#include "swstart.h"
#include "swdevice.h"
#include "swtrace.h"
#include "mem.h"
#include "kit.h"  /* KCallMonitorCallback, KSwLeStop */
#include "skinkit.h"
#include "probelog.h"
#include "rdrapi.h"    /* rdr_api */

#ifdef METRO
#include "ptdev.h"
#endif

/* We include zip SW when using a zip-hybrid setup (where the SW */
/* folder is zipped and all writes go to RAM).                   */
#if defined(USE_ZIP_HYBRID_SW_FOLDER)
#define SW_ZIP

/* When using a hybrid SW ZIP folder, this is the name of the SW archive. */
#define SWZIP_ARCHIVE_NAME  "BootFile.bin"
#endif

int32 interrupt_signal_seen = 0; /* extern for use in config.c */

static SwTraceHandlerFn *skinkit_trace_handler = NULL ;

/* ----- Static function declarations ----------------*/

static void RIPCALL RipExit( int32 n, uint8 *text ) ;
static void RIPCALL RipReboot(void) ;
static void RIPCALL RipWarn( uint8 * buffer ) ;

/*
 * The following are only required for a debug version of the core rip library.
 */
void HQNCALL HqCustomAssert(const char * pszFilename, int nLine,
                            const char * pszFormat, va_list vlist,
                            int assertflag)
{
  UNUSED_PARAM(int, assertflag);
#ifdef EMBEDDED
  /* Never use C stdio streams in embedded contexts. */
  SkinMonitorf( "Assert failed in file %s at line %d: ", pszFilename, nLine);
  SkinVMonitorf( pszFormat, vlist );
  SkinMonitorf( "\n");
#else
  /* Output must go directly to stderr here. */
  fprintf(stderr, "Assert failed in file %s at line %d: ", pszFilename, nLine);
  vfprintf(stderr, pszFormat, vlist);
  fprintf(stderr, "\n");
  fflush(stderr);
#endif
#ifdef EXIT_ON_ASSERT
  exit(1) ;
#endif
}

void HQNCALL HqCustomTrace(const char * pszFilename, int nLine,
                           const char * pszFormat, va_list vlist)
{
#ifdef EMBEDDED
  /* Never use C stdio streams in embedded contexts. */
  SkinMonitorf( "HQTRACE(%s:%d): ", pszFilename, nLine );
  SkinVMonitorf( pszFormat, vlist );
  SkinMonitorf( "\n");
#else
  /* Output must go directly to stderr here. */
  fprintf(stderr, "HQTRACE(%s:%d): ", pszFilename, nLine);
  vfprintf(stderr, pszFormat, vlist);
  fputc('\n', stderr);
  fflush(stderr);
#endif
}

/**
 * Structures required for passing to SwDllInit
 */
SWSTART memory[] = {
  { 0, { 0 } }, /* memory tag */
  { SWSwStartReturnsTag, { 0 } }, /* This can be on either SwInit or SwStart */
  { SWNThreadsTag, { 0 } },
  { SWRDRAPITag, { 0 } },
  { SWEndTag, { 0 } }, /* All but the last of these may be re-configured */
  { SWEndTag, { 0 } } /* This tag should stay as SWEndTag */
};

/**
 * Indexes into \c memory structure
 */
enum {
  TagIndex_Memory,
  TagIndex_SWSwStartReturns,
  TagIndex_NThreads,
  TagIndex_RDRAPI,
  TagIndex_Spare
} ;

void SwLeSetTraceHandler(SwTraceHandlerFn *handler)
{
  int i ;

  for ( i = TagIndex_Spare ; i < sizeof(memory) / sizeof(memory[0]) - 1 ; ++i ) {
    if ( memory[i].tag == SWTraceHandlerTag ||
         memory[i].tag == SWEndTag ) {
      memory[i].tag = SWTraceHandlerTag ;
      memory[i].value.probe_function = handler ;
      skinkit_trace_handler = handler ;
      break ;
    }
  }
}

void SwLeTraceEnable(int32 trace, int32 enable)
{
  if ( skinkit_trace_handler != NULL )
    (*skinkit_trace_handler)(trace,
                             enable ? SW_TRACETYPE_ENABLE : SW_TRACETYPE_DISABLE,
                             FALSE /* Indicates called from skin rather than user */) ;
}

void RIPFASTCALL SwLeProbe(int trace_id, int trace_type, intptr_t designator)
{
  if ( skinkit_trace_handler != NULL ) {
    HQASSERT(trace_id >= 0, "Invalid trace ID") ;
    HQASSERT(trace_type >= 0, "Invalid trace type") ;
    PKProbeLog(trace_id, trace_type, designator) ;
  }
}

/**
 * Structures required for passing to SwDllStart
 */
static SWSTART starters[] = {
  { SWDevTypesTag, { 0 } },
#ifdef SW_ZIP
  { SWZipArchiveNameTag, { 0 } },
#endif
  { SWEndTag, { 0 } }
};

/**
 * Indexes into \c starters structure
 */
enum {
  TagIndex_Devices,
#ifdef SW_ZIP
  TagIndex_ZipArchiveName,
#endif
  TagIndex_Starters_End
} ;

/** Functions provided by app for use by corerip library */
static DllFuncs dllFuncs =
{
  RipExit,
  RipReboot,
  HqCustomAssert,
  HqCustomTrace,
  RipWarn,
  NULL, /* Provided optionally by setTickleTimerFunctions() */
  NULL, /* Provided optionally by setTickleTimerFunctions() */
};

/** Device type list */
extern DEVICETYPE
  Fs_Device_Type,
  Monitor_Device_Type,
  PageBuffer_Device_Type,
#ifndef EMBEDDED
  Screening_Device_Type,
#endif
  Config_Device_Type,
  CALENDAR_Device_Type,
#ifdef USE_FONT_DECRYPT
  FontNDcrypt_Device_Type,
#endif
#ifdef SW_ZIP
  SwZip_Device_Type,
#endif
#ifdef METRO
  XpsInput_Device_Type,
#endif
#ifndef EMBEDDED
  Socket_Device_Type,
#endif
  Stream_Device_Type,
#ifdef USE_HYBRID_SW_FOLDER
  Hybrid_Device_Type,
#endif
#ifdef USE_BYPASS_PRINTER
  Printer_Device_Type,
#endif
  Progress_Device_Type,
  RAM_Device_Type ;

static DEVICETYPE *Device_Type_List[] = {
  &Fs_Device_Type,   /* %os% etc */
  &Monitor_Device_Type,
  &PageBuffer_Device_Type,
#ifndef EMBEDDED
  &Screening_Device_Type,
#endif
  &Config_Device_Type,
  &CALENDAR_Device_Type,
#ifdef USE_FONT_DECRYPT
  &FontNDcrypt_Device_Type,
#endif
#ifdef SW_ZIP
  &SwZip_Device_Type,
#endif
#ifdef METRO
  &XpsPrintTicket_Device_Type,
  &XpsInput_Device_Type,
#endif
#ifndef EMBEDDED
  &Socket_Device_Type,
#endif
  &Stream_Device_Type,
#ifdef USE_HYBRID_SW_FOLDER
  &Hybrid_Device_Type,
#endif
#ifndef EMBEDDED
  &Progress_Device_Type,
#endif
  &RAM_Device_Type,
#ifdef USE_BYPASS_PRINTER
  &Printer_Device_Type,
#endif
  NULL    /* terminate this list */
};

static DEVICETYPE ** ppCustomDeviceList = NULL;

/* ---------------------------------------------------------------------- */

static int32 multi_renderers = 1 ;
static HqBool initripmemory_called = FALSE ;


void InitRipMemory( size_t RIP_maxAddressSpaceInBytes, size_t RIP_workingSizeInBytes,
                    size_t RIP_emergencySizeInBytes,
                    void *pMemory )
{
  if ( !initripmemory_called ) {
    if (pMemory) {
      /*
       * If a block of memory has been supplied by the caller, then use the
       * client arena running in that fixed block.
       */
      static SWSTART_MEMORY start_memory;

      start_memory.sizeA = RIP_workingSizeInBytes;
      start_memory.arena = MemGetArena();
      memory[TagIndex_Memory].tag = SWMemoryTag;
      memory[TagIndex_Memory].value.pointer_value = &start_memory;
    } else {
      /*
       * Otherwise, configure the RIP to manage its own memory allocation.
       */
      static SWSTART_MEMCFG start_memcfg;

      /* Address space: we estimate a max address space size, with a
       * deduction of DEFAULT_SKIN_ADDRESS_SPACE_SIZE for skin.
       */
      start_memcfg.maxAddrSpaceSize = RIP_maxAddressSpaceInBytes ;
      start_memcfg.workingSize      = RIP_workingSizeInBytes - RIP_emergencySizeInBytes;
      start_memcfg.emergencySize    = RIP_emergencySizeInBytes;
      start_memcfg.allowUseAllMem   = FALSE;

      start_memcfg.arena = MemGetArena();
      memory[TagIndex_Memory].tag = SWMemCfgTag;
      memory[TagIndex_Memory].value.pointer_value = &start_memcfg;
    }

    memory[TagIndex_NThreads].value.int_value = multi_renderers ;

    /* Ask the RIP to return from the SwInit() and SwStart() functions rather
       than exit the application. */
    memory[TagIndex_SWSwStartReturns].value.int_value = TRUE;

    /* Pass the RDR API */
    memory[TagIndex_RDRAPI].value.pointer_value = rdr_api ;

    (void)SwDllInit( memory, &dllFuncs );

    initripmemory_called = TRUE ;
  }
}


/**
 * \brief Adds DEVICETYPE* objects to the array passed to the RIP
 * during startup.
*/
void SwLeAddCustomDevices(int32 nCustomDevices, DEVICETYPE ** ppCustomDevices)
{
  int32 nStandardDevices;
  int32 i;

  nStandardDevices = ( sizeof( Device_Type_List ) / sizeof( DEVICETYPE * ) ) - 1;   /* Exclude terminating NULL */

  ppCustomDeviceList = MemAlloc( (nStandardDevices + nCustomDevices + 1) * sizeof( DEVICETYPE * ), FALSE, TRUE );

  for ( i = 0; i < nStandardDevices; i++ )
  {
    ppCustomDeviceList[ i ] = Device_Type_List[ i ];
  }

  for ( i = 0; i < nCustomDevices; i++ )
  {
    ppCustomDeviceList[ nStandardDevices + i ] = ppCustomDevices[ i ];
  }

  ppCustomDeviceList[ nStandardDevices + nCustomDevices ] = NULL;
}


/**
 * \brief Sets the number of renderer threads.  Takes effect when the RIP
 * is next started.
*/
void SwLeSetRipRendererThreads(int32 nThreads)
{
  multi_renderers = nThreads;
}


void StartRip(void)
{
  if ( ppCustomDeviceList == NULL )
  {
    starters[TagIndex_Devices].value.pointer_value = Device_Type_List;
  }
  else
  {
    starters[TagIndex_Devices].value.pointer_value = ppCustomDeviceList;
  }

#ifdef SW_ZIP
  /* Configure the name of the zipped SW folder archive. */
  starters[TagIndex_ZipArchiveName].value.pointer_value = SWZIP_ARCHIVE_NAME;
#endif

  /* Leaving us just to call the interpreter */
#ifdef DYLIB
  SwDllStart( starters, &dllFuncs );
#else
  SwLibStart( starters, &dllFuncs );
#endif

  /* Free any memory allocated in SwLeAddCustomDevices() */
  if ( ppCustomDeviceList != NULL )
  {
    MemFree( ppCustomDeviceList );
    ppCustomDeviceList = NULL;
  }
}

void setTickleTimerFunctions(SwStartTickleTimerFn * pfnSwStartTickleTimer,
                             SwStopTickleTimerFn * pfnSwStopTickleTimer)
{
  dllFuncs.pfnSwStartTickleTimer = pfnSwStartTickleTimer;
  dllFuncs.pfnSwStopTickleTimer = pfnSwStopTickleTimer;
}

/* ---------------------------------------------------------------------- */
/** \brief RipExit will be called if for some reason the rip exits.
*/

static void RIPCALL RipExit( int32 n, uint8 *text )
{
  SkinExit( n, text );
  KSwLeStop();
  initripmemory_called = FALSE ;
}

/* ---------------------------------------------------------------------- */

/**
 * \brief The RIP will call back on this function if the interpreter
 * reboots.
 *
 * A reboot of the interpreter is a "soft" reboot. This callback allows
 * the host application to react to the situation where the RIP's
 * job server loop has reset itself. This can happen due to a serious
 * error during processing of a job, or because the job itself has
 * called the PostScript \c quit operator explicitly. The RIP restarts
 * its server loop automatically and continues running.
 *
 * The application may wish to react by soft-rebooting one or more of
 * its own modules, but no specific actions are required by the RIP.
 * Any modules that are shutdown by this function should also be
 * re-started. A reboot of the interpreter does not necessarily mean
 * that the host application is shutting down, although it can, of course,
 * choose to do so.
 *
 * A client reboot callback can be registed via SwLeSetRipRebootFunction().
 */
static void RIPCALL RipReboot( void )
{
  SkinReboot();
}

/** \brief RipWarn
 */
static void RIPCALL RipWarn( uint8 * buffer )
{
  register int32 length ;

  length = strlen_int32( ( char * ) buffer );

  if (length != 0)
    SkinMonitorl( length, buffer ) ;
}


void init_C_globals_ripthread(void)
{
  ppCustomDeviceList = NULL;
  multi_renderers = 1 ;
  initripmemory_called = FALSE ;
}


