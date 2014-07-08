/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mpslibep.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief This file implements the MPSLib interface for EP-core.
 */

#include "core.h"
#include "mpsio.h"
#include "mpslib.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "monitor.h"
#include "timing.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* "streams". We provide two streams: stdout and stderr. In theory,
   these could point to different places. For now, they both go to
   the monitor window and log file. */

enum mps_lib_stream_e {
  MPS_LIB_STDOUT,
  MPS_LIB_STDERR
};

#define MPSLibStreamSig         ((uint32)0x519523ea)
#define MPSLibStreamEOF         ((int)-0xe0f)

static struct mps_lib_stream_s {
  uint32 sig;
  int stream;
}

mps_stdout = {MPSLibStreamSig, MPS_LIB_STDOUT},
mps_stderr = {MPSLibStreamSig, MPS_LIB_STDERR};

int MPS_CALL mps_lib_get_EOF(void)
{
  return MPSLibStreamEOF;
}

mps_lib_FILE * MPS_CALL mps_lib_get_stderr(void)
{
  return &mps_stderr;
}

mps_lib_FILE * MPS_CALL mps_lib_get_stdout(void)
{
  return &mps_stdout;
}

int MPS_CALL mps_lib_fputc(int c, mps_lib_FILE *stream)
{
#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM( mps_lib_FILE *, stream ) ;
#endif
  HQASSERT(stream->sig == MPSLibStreamSig,
           "bogus stream signature in mps_lib_fputc");
  monitorf((uint8*)"%c",c);
  return c;     /* success */
}

int MPS_CALL mps_lib_fputs(const char *s, mps_lib_FILE *stream)
{
#if ! defined( ASSERT_BUILD )
  UNUSED_PARAM( mps_lib_FILE *, stream ) ;
#endif
  HQASSERT(stream->sig == MPSLibStreamSig,
           "bogus stream signature in mps_lib_fputs");
  monitorf((uint8*)"%s", s);
  return 0;     /* success */
}


/* mps_lib_assert_fail - called when an assert fails
 *
 * This implementation needs to know the internals of HQASSERT. */

#define MM_MAX_ASSERT_LENGTH (1000)

void MPS_CALL mps_lib_assert_fail(const char *data)
{
#if defined( ASSERT_BUILD )
  char buffer[ MM_MAX_ASSERT_LENGTH ];
  char *textend; char *fileend;
  size_t copylen;

  /* data consists of text, file, and line separated by newlines */
  textend = strchr( data, '\n' );
  HQASSERT( textend != NULL, "Malformed MM assert" );
  fileend = strchr( textend+1, '\n' );
  HQASSERT( fileend != NULL, "Malformed MM assert" );
  copylen = min(MM_MAX_ASSERT_LENGTH - 1, (size_t)(fileend - data));
  HQASSERT( (size_t)(fileend - data) == copylen,
            "Memory management assert too long" );
  HqMemCpy(buffer, data, copylen);
  buffer[ copylen ] = '\0';
  if ( (size_t)(textend - data) < copylen ) {
    /* If there was space for the file, separate it from the text, and
       pass it to HqAssert. */
    buffer[textend - data] = '\0';
    HqTraceSetFileAndLine(&buffer[textend - data + 1], atoi(fileend+1)) ;
  } else {
    /* If couldn't copy file, pass pointer to the original (with the
       line number stuck to the end, alas). */
    HqTraceSetFileAndLine(textend+1, atoi(fileend+1)) ;
  }
  HqAssert("Memory Management: %s", buffer);
  HqAssertPhonyExit();
#else
  monitorf((uint8*)"Memory management assert failed: %s\n", data);
  dispatch_SwExit(swexit_error_mps_lib_assert, "Memory management assert failed.") ;
#endif /* ASSERT_BUILD */
}


void * MPS_CALL mps_lib_memset(void *s, int c, size_t n)
{
  HqMemSet8(s, (uint8)c, (uint32)n);
  return s;
}


void * MPS_CALL mps_lib_memcpy(void *t, const void *f, size_t n)
{
  HqMemCpy(t, f, n);
  return t;
}


int MPS_CALL mps_lib_memcmp(const void *s1, const void *s2, size_t n)
{
  int32 i = CAST_SIZET_TO_INT32(n) ;
  return HqMemCmp(s1, i, s2, i) ;
}


static mps_clock_t event_clock = (mps_clock_t)0;

mps_clock_t MPS_CALL mps_clock(void)
{
  return event_clock++;
}

unsigned long mm_telemetry_control = 0x63 /* User, Alloc, Pool, Arena */;

unsigned long MPS_CALL mps_lib_telemetry_control(void)
{
  return mm_telemetry_control;
}

static FILE *ioFile = NULL;
static const char *ioFilename = NULL;

mps_res_t MPS_CALL mps_io_create(mps_io_t *mps_io_r)
{
  FILE *f = NULL ;

  if(ioFile != NULL) /* See impl.c.event.trans.log */
    return MPS_RES_LIMITATION; /* Cannot currently open more than one log */

  if ( ioFilename != NULL ) {
    f = fopen(ioFilename, "wb");
    if(f == NULL)
      return MPS_RES_IO;

    *mps_io_r = (mps_io_t)f;
    ioFile = f;
  }

  return MPS_RES_OK;
}


void MPS_CALL mps_io_destroy(mps_io_t mps_io)
{
  FILE *f = (FILE *)mps_io;
  if ( f != NULL ) {
    HQASSERT(f == ioFile, "Destroying different file than opened") ;
    ioFile = NULL;
    (void)fclose(f);
  }
}


mps_res_t MPS_CALL mps_io_write(mps_io_t mps_io, void *buf, size_t size)
{
  FILE *f = (FILE *)mps_io;

  if ( f != NULL ) {
    size_t n;

    HQASSERT(f == ioFile, "Writing to different file than opened") ;

    n = fwrite(buf, size, 1, f);
    if(n != 1)
      return MPS_RES_IO;
  }

  return MPS_RES_OK;
}


mps_res_t MPS_CALL mps_io_flush(mps_io_t mps_io)
{
  FILE *f = (FILE *)mps_io;

  if ( f != NULL ) {
    int e;

    HQASSERT(f == ioFile, "Flushing different file than opened") ;

    e = fflush(f);
    if(e == EOF)
      return MPS_RES_IO;
  }

  return MPS_RES_OK;
}

/* MPS Events use internal MPS types, which are not exportable from MPS. We
   overlay these re-definitions on top of the event pointer sent to
   mps_lib_event_filter() to determine if we want to process the event. */
struct mps_lib_event_pw {
  mps_clock_t codeAndClock ;
  void *p0 ;
  uintptr_t w1 ;
} ;

mps_bool_t MPS_CALL mps_lib_event_filter(const void *event, size_t length)
{
#ifdef PROBE_BUILD
  /* Probe for commit size changes. */
  const uintptr_t *evtype = event ;
  HQASSERT(length >= sizeof(*evtype), "MPS event too short for code") ;
  switch (*evtype & 0xff) {
  case 0x6A: /* CommitSet */
    HQASSERT(length == sizeof(struct mps_lib_event_pw),
             "MPS event length doesn't match CommitSet") ;
    probe_other(SW_TRACE_MPS_COMMITTED, SW_TRACETYPE_AMOUNT,
                ((const struct mps_lib_event_pw *)event)->w1) ;
    break ;
  }
#endif

  UNUSED_PARAM(size_t, length) ; /* asserts only */
  UNUSED_PARAM(const void *, event) ; /* probes only */

  /* Only add event to buffer if writing log file. */
  return ioFile != NULL ;
}

void MPS_CALL mps_lib_telemetry_defaults(const char *filename,
                                         unsigned long options)
{
  ioFilename = filename ;
  mm_telemetry_control = options ;
}

void init_C_globals_mpslibep(void)
{
  /* MPS is initialised before core, so we have to rely on static
     initialisations. */
}

/* Log stripped */
