/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:tstream.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The T-stream filter allows splitting a file stream so that it is sent to a
 * file and to the underlying filter. This was used for EP2000 spooling, to
 * capture filestreams while analysing them. It is now used for debugging.
 * Only one T-stream can be active at a time.
 */

#include "core.h"
#include "swoften.h"
#include "swerrors.h"
#include "swstart.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "fileio.h"

#include "tstream.h"

/* ---------------------------------------------------------------------- */
typedef struct {
  FILELIST_FILLBUFF original_fillbuff;
  FILELIST_CLOSEFILE original_closefile;
  FILELIST_RESETFILE original_resetfile;
  FILELIST_SETFILEPOSITION original_setfileposition;
  FILELIST_FLUSHFILE original_flushfile;

  FILELIST_CLOSEFILE t_original_closefile;
  FILELIST * flptr_diversion;
  FILELIST * flptr_original;
  uint8 * buffer_original;
  uint8 defer_write_diversion;
} T_DIVERSION;

static T_DIVERSION t_diversion = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };
static T_DIVERSION empty_t_diversion = { NULL, NULL, NULL, NULL, NULL, NULL, NULL };

void init_C_globals_tstream(void)
{
  t_diversion = empty_t_diversion ;
}

/* --------------------------------------------------
 *
 *  Substitute routines for FILELIST structure for
 *  T-Stream files
 *
 *-------------------------------------------------*/

static Bool t_writedeferbuff(FILELIST *flptr)
{
  HQASSERT(flptr, "flptr NULL in t_writedeferbuff");

  if (t_diversion.defer_write_diversion)
  {
    /* Flush the number of bytes read from the original buffer */
    int32 size = CAST_PTRDIFFT_TO_INT32(theIPtr(flptr) - t_diversion.buffer_original);
    int32 lastchar = 0;

    if ( t_diversion.original_fillbuff == FilterFillBuff &&
        theIFilterState(flptr) == FILTER_LASTCHAR_STATE)
      lastchar = 1;

    if ( !file_write(t_diversion.flptr_diversion,
                     t_diversion.buffer_original,
                     size+lastchar) )
      return FALSE;

    t_diversion.buffer_original = theIPtr(flptr) + lastchar;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

static int32 t_fillbuff(register FILELIST *flptr)
{
  /* This is the normal situation for a T Stream, and the heart of its
     functionality: data demanded by this routine is diverted to the T.  */

  int32 filter_state, result_byte, readsize = 0 ;

  HQASSERT(t_diversion.flptr_diversion, "NULL diversion file");
  HQASSERT(t_diversion.original_fillbuff != NULL, "fillbuff not diverted");
  HQASSERT(flptr == t_diversion.flptr_original, "file does not match original");
  HQASSERT(isIOpenFile(flptr), "file to fill no longer open");

  filter_state = theIFilterState(flptr);

  /* Flush the number of bytes read from the original buffer */
  if (! t_writedeferbuff(flptr))
    return EOF;

  /* The first time into the filter routine, we shorten the return count */
  if ( t_diversion.original_fillbuff == FilterFillBuff &&
       filter_state == FILTER_INIT_STATE )
    readsize = 1 ;

  result_byte = (* t_diversion.original_fillbuff)(flptr);
    /* note that though we call the original's fill routine, we give
       it the file on which we are called. */

  readsize += theIReadSize(flptr) ;

  if (result_byte >= 0 && /* not therefore EOF */
      readsize > 0 &&
      t_diversion.flptr_diversion &&
      isIOpenFile(t_diversion.flptr_diversion))
  {
    if (!t_diversion.defer_write_diversion) {
      if ( !file_write(t_diversion.flptr_diversion,
                       theIBuffer(flptr),
                       readsize) )
        return EOF;
    }
    else {
      /* Store the start of the buffer for the next write.
       * If the filter is in an empty state then pick up the last
       * character from the previous read.
       */
      t_diversion.buffer_original = theIBuffer(flptr);
      if (t_diversion.original_fillbuff == FilterFillBuff &&
          filter_state == FILTER_EMPTY_STATE)
        t_diversion.buffer_original--;
    }
  }

  return result_byte;
}

/* ---------------------------------------------------------------------- */
static Bool t_copycurrentbuffer(FILELIST *flptr_o, FILELIST *flptr_d)
{
  HQASSERT(isIOpenFile(flptr_d), "diversion file not open");

  /* Copy the current contents, if any, of the original's FILELIST buffer
     to the diversion file. */
  if (!t_diversion.defer_write_diversion && theICount(flptr_o) > 0) {
    /* Copy the file using the diversion file's buffering */
    if ( !file_write(flptr_d,
                     theIPtr(flptr_o),
                     theICount(flptr_o)) )
      return error_handler( IOERROR );
  }

  return TRUE ;
}


/* ---------------------------------------------------------------------- */
static int32 t_closefile(register FILELIST *flptr, int32 flag)
{
  /* This is the substitution for the original file (the diverison
     file also has a substitute close function - see below).
     This function needs to close both the original and diversion files, but
     also it should restore the file structures back to their normal
     state - we are done with diverting */
  int32 result_original;

  HQASSERT(t_diversion.flptr_diversion, "NULL diversion file");
  HQASSERT(t_diversion.original_closefile != NULL, "closefile not diverted");
  HQASSERT(flptr == t_diversion.flptr_original, "file does not match original");

  /* Flush the number of bytes read from the original buffer */
  if (! t_writedeferbuff(flptr))
    return EOF;

  result_original = (* t_diversion.original_closefile)(flptr, flag);
  /* note that though we call the original's fill routine, we give
     it the file on which we are called. */

  /* set everything back to normal. This closes the diversion file. */
  if (! terminate_tstream(flptr))
    return EOF;

  return result_original; /* the normal case */
}

/* ---------------------------------------------------------------------- */
static int32 t_resetfile(FILELIST *flptr)
{
  UNUSED_PARAM(FILELIST *, flptr);
  HQFAIL("reset attempted on diverted stream");
  return EOF;
}

/* ---------------------------------------------------------------------- */
static int32 t_setfilepos(FILELIST *flptr, const Hq32x2 *offset)
{
  /* you could actually provoke this from PostScript, so strictly
     speaking it should not assert, but I'd really like to know about
     it if this occurs in a debug version */
  UNUSED_PARAM(FILELIST *, flptr);
  UNUSED_PARAM(const Hq32x2 *, offset);
  HQFAIL("setfilepos attempted on diverted stream");
  return EOF;
}

/* ---------------------------------------------------------------------- */
static int32 t_flushfile(register FILELIST *flptr)
{
  /* This version of flush (for read only tee-d streams remember)
     actively reads the file to the end, but discards the data once it
     has written it to the diversion file. We should also make sure
     the buffer of the underlying file is in a safe condition, liek
     the original flush - not that this matters too much, because the
     file is now closed. */

  int32 result_byte;

  HQASSERT(t_diversion.flptr_diversion, "NULL diversion file");
  HQASSERT(flptr == t_diversion.flptr_original, "file does not match original");
  HQASSERT(t_diversion.original_fillbuff != NULL, "fillbuff not diverted");
  HQASSERT(isIOpenFile(flptr), "file to fill no longer open");

  /* Flush the number of bytes read from the original buffer */
  if (! t_writedeferbuff(flptr))
    return EOF;

  for (;;) {
    if (! isIOpenFile(t_diversion.flptr_diversion))
      break;

    result_byte = (* t_diversion.original_fillbuff) (flptr);

    if (result_byte < 0 || /* eof already set */
        theIReadSize(flptr) <= 0)
      break;

    if ( !file_write(t_diversion.flptr_diversion,
                     theIBuffer(flptr),
                     theIReadSize(flptr)) )
      return EOF;
  }

  ClearIDoneFillFlag(flptr);
  theIPtr(flptr) = theIBuffer(flptr);
  theICount(flptr) = 0;
  return 0;
}

/* ---------------------------------------------------------------------- */
static int32 t_diversion_closefile(register FILELIST * flptr, int32 flag)
{
  /* This is the substitute function to catch close on the diversion
     file. We need to terminate teeing here also */

  int32 result_original;

  HQASSERT (flptr == t_diversion.flptr_diversion, "different diversion file detected");
  HQASSERT (t_diversion.t_original_closefile != NULL,
            "closefile not diverted on diversion file");


  /* Flush the number of bytes read from the original buffer */
  if (! t_writedeferbuff(flptr))
    return EOF;

  result_original = (* t_diversion.t_original_closefile)(flptr, flag);

  if (! terminate_tstream(t_diversion.flptr_original))
    return EOF;

  return result_original; /* the normal case */

}

/* ====================================================================== */
/* control routines for T-streams */

/* ---------------------------------------------------------------------- */
Bool start_tstream(FILELIST * flptr_original, FILELIST * flptr_diversion,
                   uint8 defer_write)
{
  /* This routine does the substitution of file handling routines
     above in order to achieve write data incoming on flptr_original
     to flptr_diversion */

  HQASSERT(t_diversion.flptr_original == NULL, "tee original already set");
  HQASSERT(t_diversion.flptr_diversion == NULL, "tee diversion already set");

  /* Flag if deferred writing is enabled */
  t_diversion.defer_write_diversion = defer_write;

  /* Store a pointer to the start of the buffer */
  t_diversion.buffer_original = theIPtr(flptr_original);

  /* copy the current buffer... */
  if (! t_copycurrentbuffer( flptr_original, flptr_diversion ) )
    return FALSE ;

  /* ...and tee all subsequent FillBuffs */
  t_diversion.original_fillbuff = theIFillBuffer(flptr_original);
  theIFillBuffer(flptr_original) = t_fillbuff;

  /* flushbuff (write) unchanged - this should never be called, because we
     should only use this on files only open for read */
  HQASSERT(! isIOutputFile(flptr_original), "Tee file is not open only for read");

  /* init unchanged - original should already be init'ed anyway */

  t_diversion.original_closefile = theIMyCloseFile(flptr_original);
  theIMyCloseFile(flptr_original) = t_closefile;

  /* bytesavailable unchanged - this is only a query on the original
     file, so not relevant to the diversion file */

  t_diversion.original_resetfile = theIMyResetFile(flptr_original);
  theIMyResetFile(flptr_original) = t_resetfile;
  /* which is an error function - implies a seek coming */

  /* filepos unchanged. Though it does a seek it is at the current
     position simply to return the current position, and therefore
     does not affect the diversion file */

  t_diversion.original_setfileposition = theIMySetFilePos(flptr_original);
  theIMySetFilePos(flptr_original) = t_setfilepos;
  /* which is an error function because seeking on the stdin will
     disrupt the diverted file */

  t_diversion.original_flushfile = theIMyFlushFile(flptr_original);
  theIMyFlushFile(flptr_original) = t_flushfile;
  /* we need a new flushfile routine because the native one just
     seeks to end of file, rather than reading the contents - and to
     divert the contents, we have to read them */

  /* filter routines, and last_error not substituted */

  /* also substitute the diversion file's close routine */
  t_diversion.t_original_closefile = theIMyCloseFile(flptr_diversion);
  theIMyCloseFile(flptr_diversion) = t_diversion_closefile;

  /* record the files in our static structure */
  t_diversion.flptr_original = flptr_original;
  t_diversion.flptr_diversion = flptr_diversion;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool terminate_tstream(FILELIST * flptr)
{
  /* flptr is the file on which we are currently reading for stdin
     whose functions have been substituted; the originals and the
     diverted stream are in the TStream positions in the save
     structure. Returns the diversion file if all OK, otherwise NULL */

  Bool result = TRUE;

  HQASSERT(t_diversion.flptr_original != NULL, "tee original not set");
  HQASSERT(t_diversion.flptr_diversion != NULL, "tee diversion not set");
  HQASSERT(flptr == t_diversion.flptr_original,
           "terminate not given original stream");
  HQASSERT(theIFillBuffer(flptr) == t_fillbuff,
           "standard input does not appear to have been tee-ed");

  /* Flush the number of bytes read from the original buffer */
  if (! t_writedeferbuff(flptr))
    return FALSE;

  theIFillBuffer(flptr)   = t_diversion.original_fillbuff;
  theIMyCloseFile(flptr)  = t_diversion.original_closefile;
  theIMyResetFile(flptr)  = t_diversion.original_resetfile;
  theIMySetFilePos(flptr) = t_diversion.original_setfileposition;
  theIMyFlushFile(flptr)  = t_diversion.original_flushfile;

  theIMyCloseFile(t_diversion.flptr_diversion) = t_diversion.t_original_closefile;
  /* note: as well as resetting things, that ensures the close below
     only operates as normal, otherwise we would be called back into
     here again */

  t_diversion.original_fillbuff = NULL;
  t_diversion.original_closefile = NULL;
  t_diversion.original_resetfile = NULL;
  t_diversion.original_setfileposition = NULL;
  t_diversion.original_flushfile = NULL;
  t_diversion.t_original_closefile = NULL;

  if (isIOpenFile(t_diversion.flptr_diversion)) {
    OBJECT ofile = OBJECT_NOTVM_NOTHING ;

    file_store_object(&ofile, t_diversion.flptr_diversion, LITERAL) ;

    if (! file_close(&ofile) )
      result = FALSE;
  }

  t_diversion.flptr_original = NULL;
  t_diversion.flptr_diversion = NULL;

  return result;
}

/* ---------------------------------------------------------------------- */
FILELIST * diverted_tstream(void)
{
  /* returns the flptr of the currently diverted file if any */
  return t_diversion.flptr_diversion;
}


/* Log stripped */
