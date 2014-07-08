/** \file
 * \ingroup jbig
 *
 * $HopeName: COREjbig!src:jbig2.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to implement the public API for JBIG2, plus the platform
 * specific library calls and wrapper code that it needs.
 *
 * The JBIG2Decode filter can only be used from PDF, not from PostScript.
 * This means that the requirement that the filter be able to cope with
 * non-blocking reads and restart anywhere in the input is relaxed.
 * However, for the sake of consistency, the fileread() routine itself
 * retains the basic inside-out state machine coding style, and can probably
 * be restarted.  However, the routines which handle the individual segment
 * types (jbig2sd() etc.) definitely cannot be restarted once they have got
 * past the header bytes and into the MMR or arithemtic coded portions of
 * the segment.
 *
 * --------------------------------------------------------------------------
 *
 * This code is a port of the JBIG2 code from the JAWS codebase, but has been
 * restructured to isolate the buffered fileio dependencies from the main
 * algorithm. The restructuring was done whilst in the jaws codebase, so the
 * module could still be used there if required. (see file history in hope
 * for more information).
 *
 * The code has been split into two source files.
 * The core JBIG2 implementation is in "jbig2i.c", and has no direct
 * dependencies on the Scriptworks codebase. This code can therefore
 * be used easily in both ScriptWorks and JAWS.
 * This file "jbig2.c" implements an API wrapper layer on top of the
 * core module, handling all the dependencies of the Scriptworks codebase.
 *
 * This is a first pass at providing some basic JBIG2 functionality, and a
 * number of issues need further investigation :
 *
 */

/**
 * \todo
 * JBIG2 in PostScript does not buffer properly
 *
 * \todo
 * Address issues in the list below : BMJ 03/5/07
 *   + Unsure about memory usage in general, is the code using too much to be
 *     usable in Scriptworks. Further study/optimisations needed.
 *   + Error handling needs further checking/testing to ensure invalid
 *     input is handled nicely.
 *   + Original jaws code was designed to have code and tests inline to avoid
 *     subroutine overhead. This resulted in large functions with repeated
 *     code, and error handling with much break/continue/goto logic. I have
 *     done some restructuring for clarity, but more would probably be useful.
 *   + There are a number of comments in the original code indicating some
 *     areas which need fixing or further testing.
 */

#include "core.h"
#include "coreinit.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swctype.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "tables.h"
#include "jbig2.h"
#include "jbig2i.h"
#include "monitor.h"
#include "ripdebug.h"
#include "namedef_.h"
#include "ccittfax.h"
#include "swcopyf.h"

/*
 * ****************************************************************************
 *
 * First provide implementations of the basic memory and I/O and functions
 * that are needed by the core JBIG2 code.
 *
 * ****************************************************************************
 */

#if defined(DEBUG_BUILD)

int32 debug_jbig2 = 0;

#define J2MEM_UNIT 1024

typedef struct J2MEM
{
  Bool full;
  struct
  {
    char *mem;
    unsigned len;
  } d[J2MEM_UNIT];
  struct J2MEM *next;
} J2MEM;

/*
 * Track JBIG2 memory usage.
 */
static void j2mem_record(char *ptr,unsigned len, int stage)
{
  static J2MEM *j2mem = NULL;
  static int jbig2nfilters = 0;
  static int jbig2nmallocs = 0;
  static int jbig2msize = 0;

  J2MEM *m, *next;
  int i;
  Bool found = FALSE;

  if ( stage == 1 ) /* free */
  {
    for ( m = j2mem; m != NULL ; m = m->next )
    {
      for ( i= 0; i < J2MEM_UNIT; i++ )
      {
        if ( m->d[i].mem == ptr )
        {
          monitorf((uint8 *)"JBIG2 free(0x%08x,%d)\n",ptr,m->d[i].len);
          m->d[i].mem = NULL;
          jbig2nmallocs--;
          jbig2msize -= m->d[i].len;
          found = TRUE;
          break;
        }
      }
      if ( found )
        break;
    }
    HQASSERT(found, "jbig2 bad memory free");
  }
  else if ( stage == 0 ) /* malloc */
  {
    monitorf((uint8 *)"JBIG2 malloc(0x%08x,%d)\n",ptr,len);
    if ( ptr == NULL )
      return;
    jbig2nmallocs++;
    jbig2msize += len;
    for ( m = j2mem; m != NULL ; m = m->next )
    {
      if ( !m->full )
      {
        for ( i= 0; i < J2MEM_UNIT; i++ )
        {
          if ( m->d[i].mem == NULL )
          {
            if ( found )
            {
              m->full = FALSE;
              break;
            }
            else
            {
              m->d[i].mem = ptr;
              m->d[i].len = len;
              found = TRUE;
              m->full = TRUE;
            }
          }
        }
      }
    }
    if ( !found )
    {
      m = (J2MEM *)malloc(sizeof(J2MEM));
      HQASSERT(m,"Run out of memory in jbig2 logging");
      HqMemZero(m, sizeof(J2MEM));
      m->next = j2mem;
      j2mem = m;
      m->d[0].mem = ptr;
      m->d[0].len = len;
      m->full = FALSE;
    }
  }
  else if ( stage == 2 ) /* new filter */
  {
    jbig2nfilters++;
  }
  else /* stage == 3, close filter */
  {
    HQASSERT(jbig2nfilters > 0,"closing more JBIG2 filters than open");
    jbig2nfilters--;
    if ( jbig2nfilters == 0 )
    {
      if ( jbig2nmallocs != 0 )
      {
        monitorf((uint8 *)"Leaking some jbig2 memory\n");
        for ( m = j2mem; m != NULL ; m = m->next )
        {
          for ( i= 0; i < J2MEM_UNIT; i++ )
          {
            if ( m->d[i].mem != NULL )
            {
              monitorf((uint8 *)"JBIG2 un-freed 0x%08x,%d\n",
                  m->d[i].mem, m->d[i].len);
            }
          }
        }
      }
      for ( m = j2mem; m != NULL ; m = next )
      {
        next = m->next;
        free(m);
      }
      j2mem = NULL;
    }
  }
}

/*
 * JBIG2 assert has failed.
 */
void jb2fail(char *message)
{
  monitorf((uint8 *) message);
}

/*
 * JBIG2 printf() style debug message.
 */
void jb2dbg(char *message, ...)
{
  va_list vlist;
  uint8 buff[1024];

  va_start( vlist, message ) ;
  vswcopyf(buff, (uint8 *)message, vlist);
  monitorf(buff);
  va_end( vlist );
}

#endif /* DEBUG_BUILD */

/*
 * JBIG2 filter needs to read a bitmap encoded using MMR (Group 4) compression.
 * Open a stream to do the decoding.
 */
J2STREAM *jb2openMMR(J2STREAM *f, int columns, int rows, int endofblock)
{
  return (J2STREAM*)ccitt_open((FILELIST *)f, columns, rows, endofblock);
}

/*
 * Finished with external MMR decoder, so close it.
 */
void jb2closeMMR(J2STREAM *f)
{
  ccitt_close((FILELIST *)f);
}

/*
 * JBIG2 memory alloc (and zero memory too, if requested)
 */
char *jb2malloc(unsigned len, int zero)
{
  char *ptr = mm_alloc_with_header(mm_pool_temp, len, MM_ALLOC_CLASS_JBIG2);
  if (ptr == NULL)
    (void) error_handler(VMERROR);
  else if ( zero )
    HqMemZero(ptr, len);

  DEBUG_JB2(J2DBG_MEM,j2mem_record(ptr, len, 0);)
  return ptr;
}

/*
 * JBIG2 memory free
 */
void jb2free(char *ptr)
{
  if ( ptr != 0 )
  {
    DEBUG_JB2(J2DBG_MEM,j2mem_record(ptr, 0, 1);)
    mm_free_with_header(mm_pool_temp, ptr);
  }
}

/*
 * Configurable parameter for non-standard Acrobat support ?
 */
int32 jbig2refagg1_val = 1;

/*
 * Return UI config telling us whether to support non-standard
 * Acrobat JBIG2 encoding.
 */
int jb2refagg1()
{
  return jbig2refagg1_val;
}

/*
 * Return a single character from the given JBIG2 stream, or -1 on EOF.
 */
int jb2getc(J2STREAM *f)
{
  FILELIST *filter = (FILELIST *)f;
  int ch;

  if ( (ch = Getc(filter)) == EOF )
    return -1;
  else
    return ch;
}

/*
 * Error has occured reading the given JBIG2 stream.
 * Set error conditions as appropriate, and report the problem.
 */
int jb2error(J2STREAM *f, char *cp, int n)
{
  FILELIST *filter = (FILELIST *)f;

  if (n == -1)
    SetIEofFlag(filter);
  if (n == -2)
    SetIIOErrFlag(filter);
  if (cp)
    monitorf((uint8 *)"JBIG2 error '%s'\n",cp);

  return (n);
}

/*
 * Return the private storage abstraction from the given JBIG2 stream.
 */
void *jb2get_private(J2STREAM *f)
{
  FILELIST *filter = (FILELIST *)f;

  return (void *)(theIFilterPrivate(filter));
}

/*
 * Return the underlying input stream structure from the given
 * JBIG2 I/O stream.
 */
J2STREAM *jb2infile(J2STREAM *f)
{
  FILELIST *filter = (FILELIST *)f;

  return (J2STREAM *)(theIUnderFile(filter));
}

/*
 * Deal with any output chars that are present in the JBIG2 I/O stream.
 */
void jb2chars_in_outbuf(J2STREAM *f)
{
  UNUSED_PARAM(J2STREAM *, f);
  /* doesn't seem to necessary in Scriptworks */
}

/*
 * Write the passed buffer of data into the JBIG2 output stream.
 */
int jb2write(J2STREAM *f, char *ptr, int n)
{
  FILELIST *filter = (FILELIST *)f;

  if ( n > filter->buffersize )
    n = filter->buffersize;

  bcopy(ptr,(char *)filter->buffer,n);
  filter->count = n;
  return n;
}

/*
 * zero the specified block of memory.
 */
void jb2zero(char *ptr, int bytes)
{
  HqMemZero(ptr, bytes);
}

/*
 * compare the two blocks of memory.
 */
int jb2cmp(char *p1, char *p2, int bytes)
{
  return HqMemCmp((const uint8 *)p1, bytes, (const uint8 *)p2, bytes);
}

/*
 * copy the given block of memory.
 */
void jb2copy(char *src, char *dest, int bytes)
{
  HqMemCpy(dest, src, bytes);
}

/*
 * Increase reference count in the given JBIG2 stream
 */
void jb2countup(J2STREAM *f)
{
  UNUSED_PARAM(J2STREAM *, f);
}

/*
 * Decrease reference count in the given JBIG2 stream
 */
void jb2countdown(J2STREAM *f)
{
  UNUSED_PARAM(J2STREAM *, f);
}


/*
 * ****************************************************************************
 *
 * Now implement the public API.
 *
 * ****************************************************************************
 */

#define JBIG2BUFFSIZE (1024*32)

/*
 * Parse the arguments and create/initialise a JBIG2 filter.
 */
static Bool jbig2FilterInit(FILELIST *filter , OBJECT *args , STACK *stack )
{
  J2STREAM *globals = NULL;
  int32 pop_args = 0 ;
  enum {
    match_jbig2globals, match_n_entries
  };
  static NAMETYPEMATCH thematch[match_n_entries + 1] = {
      { NAME_JBIG2Globals | OOPTIONAL, 1, { OFILE }},
        DUMMY_END_MATCH
  };

  HQASSERT(filter , "filter NULL in jbig2FilterInit.");

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty");

  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! dictmatch(args, thematch ) )
      return FALSE ;
    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;
    OCopy( theIParamDict( filter ), *args ) ;
  } else
    args = NULL ;

  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {
    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  theIBuffer(filter) = mm_alloc(mm_pool_temp, JBIG2BUFFSIZE + 1,
                                              MM_ALLOC_CLASS_JBIG2);

  if (theIBuffer(filter) == NULL)
    return error_handler(VMERROR);

  theIBuffer(filter)++ ;
  theIPtr(filter) = theIBuffer(filter) ;
  theICount(filter) = 0 ;
  theIBufferSize(filter) = JBIG2BUFFSIZE ;
  theIFilterState(filter) = FILTER_INIT_STATE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  if ( args && thematch[match_jbig2globals].result != NULL )
  {
    OBJECT *theo = thematch[match_jbig2globals].result;
    FILELIST *flptr = oFile(*theo);

    HQASSERT(oType(*theo) == OFILE, "JBIG2Globals not a stream");

    if ( isIRewindable(flptr) ) {
      /* Now make sure we're at the start of the data */
      Hq32x2 filepos;
      Hq32x2FromUint32(&filepos, 0u);
      if ( (*theIMyResetFile(flptr))(flptr) == EOF ||
           (*theIMySetFilePos(flptr))(flptr, &filepos) == EOF )
        return (*theIFileLastError(flptr))(flptr);
    }

    globals = (J2STREAM *)flptr;
  }
  if ( (theIFilterPrivate(filter) = jbig2open(globals)) == NULL )
    return FALSE;

  DEBUG_JB2(J2DBG_MEM,j2mem_record(NULL, 0, 2);)
  return TRUE ;
}

/*
 * Finished with the JBIG2 filter, free asscoiated resources.
 */
static void jbig2FilterDispose(FILELIST *filter)
{
  J2STREAM *f = (J2STREAM *)filter;

  HQASSERT(filter , "filter NULL in jbig2FilterDispose.");

  if ( theIBuffer(filter) )
  {
    mm_free(mm_pool_temp, (mm_addr_t)(theIBuffer(filter) - 1),
                          JBIG2BUFFSIZE + 1);
    theIBuffer(filter) = NULL;
  }
  if ( jbig2close(f) > 0 )
  {
    theIFilterPrivate(filter) = NULL;
    DEBUG_JB2(J2DBG_MEM,j2mem_record(NULL, 0, 3);)
  }
}

/*
 * Decode some bytes in the JBIG2 stream.
 */
static Bool jbig2DecodeBuffer(FILELIST *filter, int32 *ret_bytes )
{
  int32 n;
  J2STREAM *f = (J2STREAM *)filter;

  HQASSERT(filter , "filter NULL in jbig2DecodeBuffer.");

  DEBUG_JB2(J2DBG_PARSE,monitorf((uint8 *)"JBIG2 Decode Buffer\n");)
  n = jbig2read(f);
  DEBUG_JB2(J2DBG_PARSE,monitorf((uint8 *)"JBIG2 buffer returns %d %d\n",
                          n,filter->count);)
  *ret_bytes = filter->count;
  return TRUE;
}

static void init_C_globals_jbig2(void)
{
#ifdef DEBUG_BUILD
  debug_jbig2 = 0 ;
#endif
}

/*
 * External public API for JBIG2 module.
 */
static Bool jbig2_swstart(struct SWSTART *params)
{
  FILELIST *flptr ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( (flptr = mm_alloc_static(sizeof(FILELIST))) == NULL )
    return FALSE ;

#if defined(DEBUG_BUILD)
  register_ripvar(NAME_debug_jbig2, OINTEGER, &debug_jbig2);
#endif

  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("JBIG2Decode"),
                       FILTER_FLAG | READ_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                /* fillbuff */
                       FilterFlushBufError,           /* flushbuff */
                       jbig2FilterInit,               /* initfile */
                       FilterCloseFile,               /* closefile */
                       jbig2FilterDispose,            /* disposefile */
                       FilterBytes,                   /* bytesavail */
                       FilterReset,                   /* resetfile */
                       FilterPos,                     /* filepos */
                       FilterSetPos,                  /* setfilepos */
                       FilterFlushFile,               /* flushfile */
                       FilterEncodeError,             /* filterencode */
                       jbig2DecodeBuffer ,            /* filterdecode */
                       FilterLastError,               /* lasterror */
                       -1, NULL, NULL, NULL ) ;
  filter_standard_add(&flptr[0]) ;

  return TRUE ;
}

void jbig2_C_globals(core_init_fns *fns)
{
  init_C_globals_jbig2() ;
  fns->swstart = jbig2_swstart ;
}

/*
 * Restarted log with new port of code from Jaws.
 *
* Log stripped */

/* end of jbig2.c */
