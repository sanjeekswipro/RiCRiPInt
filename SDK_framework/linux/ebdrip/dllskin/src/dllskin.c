/* $HopeName: SWdllskin!src:dllskin.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * Wrapper layer to turn core RIP into a DLL.
 *
* Log stripped */

#include "std.h"
#include "dlliface.h"
#include "pdllskin.h"
#include "swdevice.h"
#include "tickle.h"
#include "hqunicode.h"
#include "hqmemcpy.h"
#include "mps.h"
#include "mpscmvff.h"
#include "mpsavm.h"
#include "calibration.h"

#ifdef HAS_FWOS
#define FWSTR_ALLOW_DEPRECATED 1    /* For vsprintf */

#include "fwboot.h"     /* FwBoot */
#include "fwstring.h"   /* FwTextString */
#include "fwfile.h"
#include "fwmem.h"
#include "signdev.h"
#endif


#include "extscrty.h"
#include "dongle.h"

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

#include <time.h>
#include <locale.h>
#include <string.h>
#ifndef HAS_FWOS
#include <stdio.h>
#endif

#ifdef HAS_FWOS
static void BootFrameWork( void );

static FwTextString ptbzProductName = FWSTR_TEXTSTRING( "CoreRIP" );
#endif

static SwExitFn         * pfnSwExit;
static SwRebootFn       * pfnSwReboot;
static SwWarnFn         * pfnSwWarn;

static SwStartTickleTimerFn * pfnSwStartTickleTimer = NULL;
static SwStopTickleTimerFn * pfnSwStopTickleTimer = NULL;

static void startTickleTimerWrap();
static void stopTickleTimerWrap();

static uint8 cvt_buffer[64];

/* Pluggable memory handling for DLL skin. */

static mps_pool_t dllskin_pool = NULL ;

/* header_s -- Block header
 *
 * .header: Each block allocated needs to remember its size, so we add
 * a header to keep it in.  HEADER_BLOCK and BLOCK_HEADER translate
 * between a pointer to the header and a pointer to the block (what
 * the caller of malloc gets).
 */

typedef struct header_s {
  size_t size;
} header_s;

#define MPS_PF_ALIGN    8 /* .hack.align */

/* To emulate EPDR-like behaviour using mvff */
#define EPDR_LIKE ( mps_bool_t )1, ( mps_bool_t )1, ( mps_bool_t )1

#define HEADER_SIZE \
  ((sizeof(header_s) + MPS_PF_ALIGN - 1) & ~(MPS_PF_ALIGN - 1))
#define HEADER_BLOCK(header) \
  ((void *)((char *)(header) + HEADER_SIZE))
#define BLOCK_HEADER(block) \
  ((header_s *)((char *)(block) - HEADER_SIZE))

void HQNCALL SecurityExit( int32 n, uint8 *text )
{
  SwTerminating(n, text) ;
}

static void startTickleTimerWrap()
{
  if ( pfnSwStartTickleTimer == NULL )
    /* Use default tickle timer for the platform. */
    startTickleTimer();
  else
    /* Use client's tickle timer. */
    pfnSwStartTickleTimer();
}

static void stopTickleTimerWrap()
{
  if ( pfnSwStopTickleTimer == NULL )
    /* Use default tickle timer for the platform. */
    stopTickleTimer();
  else
    /* Use client's tickle timer. */
    pfnSwStopTickleTimer();
}

static void *dll_u_alloc(const void *context, size_t size)
{
  mps_pool_t *pool_ptr = (mps_pool_t *)context ;

  HQASSERT(pool_ptr != NULL, "ICU alloc context invalid") ;

  if (*pool_ptr != NULL && size != 0 && (size + HEADER_SIZE > size))
  {
    void * p;
    mps_res_t res;
    res = mps_alloc(&p, *pool_ptr, size + HEADER_SIZE);
    if (!res)
    {
      header_s *header;
      header = p;
      header->size = size + HEADER_SIZE;
      return HEADER_BLOCK(header) ;
    }
  }

  return NULL ;
}

static void *dll_u_realloc(const void *context, void *mem, size_t size)
{
  mps_pool_t *pool_ptr = (mps_pool_t *)context ;
  header_s *header;
  size_t oldSize;
  void *newBlock = NULL ;

  HQASSERT(pool_ptr != NULL, "ICU realloc context invalid") ;

  if (mem == NULL)
    return dll_u_alloc(context, size);

  /* Get the size and check that it's not a free block. */
  header = BLOCK_HEADER(mem);
  oldSize = header->size;
  HQASSERT(oldSize != 0, "ICU Realloc block was zero");

  if (size == oldSize - HEADER_SIZE) /* Same size, return existing */
    return mem;

  if (size != 0) {
    newBlock = dll_u_alloc(context, size);
    if (newBlock != NULL)
      HqMemCpy(newBlock, mem, min(oldSize - HEADER_SIZE, size));
  }

  header->size = 0;
  mps_free(*pool_ptr, header, oldSize);
  return newBlock;
}

static void dll_u_free(const void *context, void *mem)
{
  mps_pool_t *pool_ptr = (mps_pool_t *)context ;
  size_t size;
  header_s *header;

  HQASSERT(pool_ptr != NULL, "ICU realloc context invalid") ;

  if ( *pool_ptr == NULL || mem == NULL )
    return ;

  header = BLOCK_HEADER(mem);
  size = header->size;

  /* detect double frees */
  HQASSERT(size != 0, "Double free of ICU data");
  header->size = 0;

  mps_free(*pool_ptr, header, size);
}

static unicode_memory_t unicode_memory = {
  &dllskin_pool,
  dll_u_alloc,
  dll_u_realloc,
  dll_u_free
} ;

static int dllskin_meminit(SWSTART *start)
{
  int32 i ;
  mps_arena_t dllskin_arena = NULL ;

  /* Spy on the memory configuration tags to find the arena */
  for ( i = 0 ; dllskin_arena == NULL && start[i].tag != SWEndTag ; ++i ) {
    switch ( start[i].tag ) {
    case SWMemoryTag:
      dllskin_arena = ((SWSTART_MEMORY *)start[i].value.pointer_value)->arena;
      break ;
    case SWMemCfgTag:
      dllskin_arena = ((SWSTART_MEMCFG *)start[i].value.pointer_value)->arena;
      break ;
    }
  }

  if ( dllskin_arena != NULL ) {
    mps_res_t res = mps_pool_create(&dllskin_pool, dllskin_arena, mps_class_mvff(),
                                    (size_t)65536, (size_t)32, (size_t)8,
                                    EPDR_LIKE);
    if ( res == MPS_RES_OK ) {
      mps_word_t label = mps_telemetry_intern("DLLskin pool");
      mps_telemetry_label((mps_addr_t)dllskin_pool, label);
    } else {
      SecurityExit(1, (uint8 *)"Unicode memory pool initialisation failed");
      return FALSE ;
    }
  }
  return TRUE ;
}

#ifdef HAS_FWOS

static void *dllskin_alloc(size_t cbSize)
{
  return dll_u_alloc(&dllskin_pool, cbSize) ;
}

static void dllskin_free(void *mem)
{
  dll_u_free(&dllskin_pool, mem) ;
}

#endif

/* Pluggable I/O for Unicode compound. */

/* We cause all file accesses to fail before SwStart is called, because the
   (%os%) device is not initialised. ICU initialisation files should be
   compiled into the RIP anyway. */
static DEVICELIST *os_device_ptr = NULL ;
static HqBool swstart_called = FALSE ;

/* Zero is a valid handle, which would translate to a NULL pointer, so
   we add one to make sure the value is not NULL. This is an awful
   hack. */
#define Dll_PtrToHandle(p) ((VOIDPTR_TO_DEVICE_FILEDESCRIPTOR(p)) - 1)
#define Dll_HandleToPtr(h) (DEVICE_FILEDESCRIPTOR_TO_VOIDPTR((h + 1)))

/* Cache OS device for file handling functions */
static int32 os_device(DEVICELIST **devicelist, DEVICETYPE **devicetype)
{
  if ( !swstart_called )
    return FALSE ;

  if ( os_device_ptr == NULL )
    os_device_ptr = SwFindDevice((uint8 *)"os") ;

  if ( (*devicelist = os_device_ptr) == NULL ||
       (*devicetype = os_device_ptr->devicetype) == NULL )
    return FALSE ;

  return TRUE ;
}

static void *dll_u_fopen(const void *context, const char *filename, const char *mode)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;
  int32 openflags = 0 ;
  DEVICE_FILEDESCRIPTOR handle ;

  HQTRACE(!swstart_called,
          ("ICU fopen('%s','%s') called before SwStart()", filename, mode)) ;

  if ( !os_device(&os, &dev) || filename == NULL || mode == NULL ) {
    *last_error = TRUE ;
    return NULL ;
  }

  switch ( mode[0] ) {
  case 'r':
    openflags |= SW_RDONLY ;
    break ;
  case 'w':
    openflags |= SW_WRONLY|SW_CREAT|SW_TRUNC ;
    break ;
  default:
    *last_error = TRUE ;
    return NULL ;
  }

  if ( (handle = (*dev->open_file)(os, (uint8 *)filename, openflags)) < 0 ) {
    *last_error = ((*dev->last_error)(os) != DeviceNoError) ;
    return NULL ;
  }

  *last_error = FALSE ;

  return Dll_HandleToPtr(handle) ;
}

static void dll_u_fclose(const void *context, void *filestream)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;
  DEVICE_FILEDESCRIPTOR handle = Dll_PtrToHandle(filestream) ;

  HQTRACE(!swstart_called,
          ("ICU fclose() called before SwStart()")) ;

  if ( !os_device(&os, &dev) || handle < 0 ) {
    *last_error = TRUE ;
    return ;
  }

  if ( (*dev->close_file)(os, handle) < 0 ) {
    *last_error = ((*dev->last_error)(os) != DeviceNoError) ;
    return ;
  }

  *last_error = FALSE ;
}

static int32 dll_u_fread(const void *context, void *addr, int32 bytes, void *filestream)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;
  DEVICE_FILEDESCRIPTOR handle = Dll_PtrToHandle(filestream) ;
  int32 size ;

  HQTRACE(!swstart_called,
          ("ICU fread() called before SwStart()")) ;

  if ( !os_device(&os, &dev) || addr == NULL || bytes < 0 || handle < 0 ) {
    *last_error = TRUE ;
    return 0 ;
  }

  if ( (size = (*dev->read_file)(os, handle, addr, bytes)) < 0 ) {
    *last_error = ((*dev->last_error)(os) != DeviceNoError) ;
    return 0 ;
  }

  *last_error = FALSE ;

  return size ;
}

static int32 dll_u_fwrite(const void *context, const void *addr,
                          int32 bytes, void *filestream)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;
  DEVICE_FILEDESCRIPTOR handle = Dll_PtrToHandle(filestream) ;
  int32 size ;

  HQTRACE(!swstart_called,
          ("ICU fwrite() called before SwStart()")) ;

  if ( !os_device(&os, &dev) || addr == NULL || bytes < 0 || handle < 0 ) {
    *last_error = TRUE ;
    return 0 ;
  }

  if ( (size = (*dev->write_file)(os, handle, (uint8 *)addr, bytes)) < 0 ) {
    *last_error = ((*dev->last_error)(os) != DeviceNoError) ;
    return 0 ;
  }

  *last_error = FALSE ;

  return size ;
}

static void dll_u_frewind(const void *context, void *filestream)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;
  DEVICE_FILEDESCRIPTOR handle = Dll_PtrToHandle(filestream) ;
  Hq32x2 where ;

  HQTRACE(!swstart_called,
          ("ICU frewind() called before SwStart()")) ;

  if ( !os_device(&os, &dev) || handle < 0 ) {
    *last_error = TRUE ;
    return ;
  }

  Hq32x2FromUint32(&where, 0) ;
  if ( (*dev->seek_file)(os, handle, &where, SW_SET) < 0 ) {
    *last_error = ((*dev->last_error)(os) != DeviceNoError) ;
    return ;
  }

  *last_error = FALSE ;
}

static int32 dll_u_fextent(const void *context, void *filestream)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;
  DEVICE_FILEDESCRIPTOR handle = Dll_PtrToHandle(filestream) ;
  Hq32x2 size ;
  int32 result ;

  HQTRACE(!swstart_called,
          ("ICU fextent() called before SwStart()")) ;

  if ( !os_device(&os, &dev) || handle < 0 ) {
    *last_error = TRUE ;
    return 0 ;
  }

  if ( (*dev->bytes_file)(os, handle, &size, SW_BYTES_TOTAL_ABS) < 0 ) {
    *last_error = ((*dev->last_error)(os) != DeviceNoError) ;
    return 0 ;
  }

  if ( !Hq32x2ToInt32(&size, &result) ) {
    *last_error = TRUE ;
    return 0 ;
  }

  *last_error = FALSE ;

  return result ;
}

static int dll_u_feof(const void *context, void *filestream)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;
  DEVICE_FILEDESCRIPTOR handle = Dll_PtrToHandle(filestream) ;
  Hq32x2 size ;

  HQTRACE(!swstart_called,
          ("ICU feof() called before SwStart()")) ;

  if ( !os_device(&os, &dev) || handle < 0 ) {
    *last_error = TRUE ;
    return TRUE ;
  }

  if ( (*dev->bytes_file)(os, handle, &size, SW_BYTES_AVAIL_REL) < 0 ) {
    *last_error = ((*dev->last_error)(os) != DeviceNoError) ;
    return TRUE ;
  }

  *last_error = FALSE ;

  return FALSE ;
}

static int dll_u_fremove(const void *context, const char *filename)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;

  HQTRACE(!swstart_called,
          ("ICU fremove() called before SwStart()")) ;

  if ( !os_device(&os, &dev) || filename == NULL ) {
    *last_error = TRUE ;
    return -1 ;
  }

  if ( (*dev->delete_file)(os, (uint8 *)filename) < 0 ) {
    *last_error = ((*dev->last_error)(os) != DeviceNoError) ;
    return -1 ;
  }

  *last_error = FALSE ;

  return 0 ;
}

static int dll_u_ferror(const void *context, void *filestream)
{
  DEVICELIST *os ;
  DEVICETYPE *dev ;
  int32 *last_error = (int32 *)context ;
  DEVICE_FILEDESCRIPTOR handle = Dll_PtrToHandle(filestream) ;

  HQTRACE(!swstart_called,
          ("ICU ferror() called before SwStart()")) ;

  if ( !os_device(&os, &dev) || handle < 0 || *last_error )
    return TRUE ;

  return FALSE ;
}

static int32 unicode_error = FALSE ;

static unicode_fileio_t unicode_fileio = {
  &unicode_error,
  dll_u_fopen,
  dll_u_fclose,
  dll_u_fread,
  dll_u_fwrite,
  dll_u_frewind,
  dll_u_fextent,
  dll_u_feof,
  dll_u_fremove,
  dll_u_ferror
} ;

void init_C_globals_dllskin(void)
{
  dllskin_pool = NULL ;
  os_device_ptr = NULL ;
  swstart_called = FALSE ;
  unicode_error = FALSE ;
  pfnSwStartTickleTimer = NULL;
  pfnSwStopTickleTimer = NULL;
}

/* Declare global init functions here to avoid header inclusion
   nightmare. */
void init_C_globals_ptickle(void) ;

int32 init_C_runtime_dllskin(void* context)
{
  context = NULL ; /* purely to remove compiler warnings. */
  init_C_globals_ptickle() ;
  init_C_globals_dllskin() ;
  return TRUE ;
}

static void set_assert_handlers(DllFuncs *pDllFuncs)
{
  HqAssertHandlers_t handlers ;

  handlers.assert_handler = pDllFuncs->pfnHqCustomAssert ;
  handlers.trace_handler = pDllFuncs->pfnHqCustomTrace ;
  SetHqAssertHandlers(&handlers) ;
}

HqBool RIPCALL SwDllInit( SWSTART * start, DllFuncs * pDllFuncs )
{
  pfnSwExit = pDllFuncs->pfnSwExit;
  pfnSwReboot = pDllFuncs->pfnSwReboot;
  pfnSwWarn = pDllFuncs->pfnSwWarn;
  pfnSwStartTickleTimer = pDllFuncs->pfnSwStartTickleTimer;
  pfnSwStopTickleTimer = pDllFuncs->pfnSwStopTickleTimer;
  set_assert_handlers(pDllFuncs) ;

  if (! init_C_runtime_dllskin(NULL))
    return FALSE ;

  if (! dllskin_meminit( start ))
    return FALSE ;

  return SwInit( start );
}

#ifdef DYLIB

/* Wrapper to SwStart */
void RIPCALL SwDllStart( SWSTART * start, DllFuncs * pDllFuncs )
{
  int32 fSecurityPassed;

  if( pDllFuncs )
  {
    pfnSwExit = pDllFuncs->pfnSwExit;
    pfnSwReboot = pDllFuncs->pfnSwReboot;
    pfnSwWarn = pDllFuncs->pfnSwWarn;
    pfnSwStartTickleTimer = pDllFuncs->pfnSwStartTickleTimer;
    pfnSwStopTickleTimer = pDllFuncs->pfnSwStopTickleTimer;
    set_assert_handlers(pDllFuncs) ;
  }

  startTickleTimerWrap () ;

  if (! dllskin_meminit(start) )
    return ;

  BootFrameWork();

  if ( !unicode_init("Unicode",
                     &unicode_fileio, /*file handler*/
                     &unicode_memory /*memory handler*/) ) {
    SecurityExit(1, (uint8 *)"Unicode initialisation failed") ;
    return ;
  }

  if ( !calibration_init() ) {
    SecurityExit(1, (uint8 *)"Calibration initialisation failed") ;
    return ;
  }

  secDevDetermineMethod();
  fSecurityPassed = startupDongleTestSilently();

  start_sig_check();

  fSecurityPassed = startupDongleTestReport(fSecurityPassed);

  if (fSecurityPassed)
  {
    /* Allow ICU file functions to succeed from now on. */
    swstart_called = TRUE ;
    (void)SwStart( start );
  }
}

#else

/* Alternative wrapper to SwStart, for use when the core is in a statically-
   linked library rather than a DLL. See request 50940. Code duplication with
   SwDllStart can be solved with some refactoring, but that can wait until
   after this approach to supporting the static-link model has been
   evaluated. This method might well disappear altogether. The prototype
   for this function is not part of the public LE interface - it is not
   seen by customers. */
void RIPCALL SwLibStart( SWSTART * start, DllFuncs * pDllFuncs )
{
  int32 fSecurityPassed;

  if( pDllFuncs )
  {
    pfnSwExit = pDllFuncs->pfnSwExit;
    pfnSwReboot = pDllFuncs->pfnSwReboot;
    pfnSwWarn = pDllFuncs->pfnSwWarn;
    pfnSwStartTickleTimer = pDllFuncs->pfnSwStartTickleTimer;
    pfnSwStopTickleTimer = pDllFuncs->pfnSwStopTickleTimer;
    set_assert_handlers(pDllFuncs) ;
  }

  startTickleTimerWrap () ;

  if (!dllskin_meminit(start) )
    return ;

#ifdef HAS_FWOS
  BootFrameWork();
#endif

  if ( !unicode_init("Unicode",
                     &unicode_fileio, /*file handler*/
                     &unicode_memory /*memory handler*/) ) {
    SecurityExit(1, (uint8 *)"Unicode initialisation failed") ;
    return ;
  }

  if ( !calibration_init() ) {
    SecurityExit(1, (uint8 *)"Calibration initialisation failed") ;
    return ;
  }

  secDevDetermineMethod();
  fSecurityPassed = startupDongleTestSilently();

  fSecurityPassed = startupDongleTestReport(fSecurityPassed);

  if (fSecurityPassed)
  {
    /* Allow ICU file functions to succeed from now on. */
    swstart_called = TRUE ;
    (void)SwStart( start );
  }
}

#endif

void RIPCALL SwDllShutdown( void )
{
}

/* External functions provided by user of DLL */
void RIPCALL SwTerminating( int32 n, uint8 *text )
{
#ifdef DYLIB
  end_sig_check();
#endif

  endSecurityDevice();

  calibration_finish() ;

  unicode_finish() ;

#ifdef HAS_FWOS
  FwShutdown();
#endif

  if ( dllskin_pool != NULL ) {
    mps_pool_destroy(dllskin_pool) ;
    dllskin_pool = NULL ;
  }

  stopTickleTimerWrap();

  (pfnSwExit) ( n, text );
}

void RIPCALL SwReboot( void )
{
  stopTickleTimerWrap();

  (pfnSwReboot) ();

  startTickleTimerWrap();
}

void RIPCALL SwDllWarn( uint8 * buffer )
{
  (pfnSwWarn) ( buffer );
}

/* External support functions for RIP provided by DLL skin layer */
int32 get_utime( void )
{
  return platformGetUserMilliSeconds();
}

int32 get_rtime( void )
{
  return platformGetRealMilliSeconds();
}

/* Returns a pointer to a static string giving the operating system */
uint8 * get_operatingsystem( void )
{
  return platformGetOperatingSystem() ;
}

uint8 *cvt_real( double value, char type, int precision)
{
#ifdef HAS_FWOS

  FwStrRecord  displayRecord;

  if ( precision < 0 )
    precision = 6; /* default if it was not set */
  cvt_buffer[0] = '\0';
  FwStrRecordOpenOn( &displayRecord, cvt_buffer, sizeof(cvt_buffer) );
  FwStrPrintReal( &displayRecord,         /* Record to output to */
                  (promoted_real)value,   /* Value to output */
                  type,                   /* %e,f,g,E,F,G conversion */
                  0,                      /* No minimum width */
                  precision,              /* Precision */
                  FALSE,                  /* Not left justified */
                  FALSE,                  /* No padding */
                  '\0',                   /* No positive value indicator */
                  TRUE,                   /* "C" locale */
                  FALSE );                /* Not optional form */
  FwStrRecordClose( &displayRecord );

#else

  char * prevLocale;
  char fmt[ 10 ];
  HqBool restore_locale = FALSE;

  /* Force C locale */
  prevLocale = setlocale( LC_NUMERIC, NULL );
  if ( strcmp(prevLocale, "C") != 0 )
  {
    setlocale( LC_NUMERIC, "C" );
    restore_locale = TRUE;
  }

  if ( precision < 0 )
    sprintf( fmt, "%%%c", type );
  else
    sprintf( fmt, "%%.%d%c", precision, type );
  sprintf( (char *) cvt_buffer, fmt, value );

  if ( restore_locale )
    setlocale( LC_NUMERIC, prevLocale );

#endif

  return cvt_buffer;
}


uint8 *get_date(int32 fLocal)
{
  time_t now;
  int32 len ;
  char * prevLocale = NULL; /* suppress compiler warning */

  if (!fLocal)
  {
    /* Force C locale */
    prevLocale = setlocale( LC_TIME, NULL );
    setlocale( LC_TIME, "C" );
  }

  now = time((time_t *)NULL);
  strcpy((char *) cvt_buffer, ctime(&now));
  len = strlen_int32((char *) cvt_buffer) - 1 ;
  cvt_buffer[ len ] = '\0';

  if (!fLocal)
  {
    /* Reset locale */
    setlocale( LC_TIME, prevLocale );
  }
  return cvt_buffer ;
}


uint8 *get_guilocale( void )
{
  /* Undetermined */
  return (uint8 *) "und";
}


uint8 *get_oslocale( void )
{
  /* Undetermined */
  return (uint8 *) "und";
}

#ifdef HAS_FWOS

void fwExitErrorf ( FwTextByte * format, ... )
{
  if(FwBootDone())
  {
    FwStrRecord rec;

    FwStrRecordOpen(&rec);
    if (format != NULL)
    {
      va_list     vlist;

      va_start(vlist, format);
      FwStrVPrintf(&rec, FALSE, format, vlist);
      va_end(vlist);
    }
    SecurityExit(1, FwStrRecordClose(&rec));
    exit(1);
  }
  else
  {
    char acMessage[256];

    if( format != NULL )
    {
      va_list vlist;

      va_start(vlist, format);
      vsprintf(acMessage, (char *) format, vlist);
      va_end(vlist);
    }
    else
    {
      acMessage[0] = '\0';
    }
    SecurityExit(1, (uint8 *) acMessage);
    exit(1);
  }
}

static void BootFrameWork( void )
{
  FwBootContext bootContext = { 0 };

  FwBootGetDefaultContext( &bootContext );

  /* Set up control context */
  bootContext.control.ptbzAppName = ptbzProductName;
  bootContext.control.exiterrorf = fwExitErrorf;

  /* Set up memory context - minimal memory debugging */
  bootContext.memory.debug_level = FWMEM_DBGLEV_NONE;
  bootContext.memory.alloc = dllskin_alloc ;
  bootContext.memory.free = dllskin_free ;

  FwBoot( &bootContext );
}

#endif
