/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:std_file.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Declares and initialises a table of standard files.
 */

#include "core.h"
#include "coreinit.h"
#include "swoften.h" /* public file */
#include "swctype.h"
#include "swerrors.h"
#include "swdevice.h"
#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"

#include "psvm.h"
#include "devices.h"  /* nulldevice */
#include "std_file.h"

/* Edit file statuses */

#define DOEDIT    1
#define DONEEDIT  2


#define controlU        ( 'u' - 'a' + 1 )
#define controlR        ( 'r' - 'a' + 1 )
#define controlC        ( 'c' - 'a' + 1 )
#define controlH        ( 'h' - 'a' + 1 )


/* Buff sizes for stdin, stdout, stderr, lineedit, and statementedit */
#define BUFFSIZE         1024        /* for std files, and edit files */
#define LINEMAX          2048        /* length of lineedit buffer */
#define STATEMAX         4096        /* length of statementedit buffer */
/* Total buffer size for stdfiles */
#define SUM_STDFILE_BUFFSIZE (LINEMAX + STATEMAX + 3*BUFFSIZE)

FILELIST* std_files;
static uint8* std_files_mem;

Bool stream_echo_flag = FALSE ;

/* Standard file, lineedit, statementedit functions */
static int32 StdFileFillBuff( register FILELIST *flptr ) ;
static int32 StdFileFlushBuff( int32 c, register FILELIST *flptr ) ;
static Bool StdFileInit( FILELIST *flptr ,
                         OBJECT *args ,
                         STACK *stack ) ;
static int32 StdFileClose( register FILELIST *flptr, int32 flag ) ;
static int32 StdFileBytes( register FILELIST *flptr, Hq32x2* avail ) ;
static int32 StdFileReset( register FILELIST *flptr ) ;
static int32 StdFilePos( register FILELIST *flptr, Hq32x2* file_pos ) ;
static int32 StdFileSetPos(register FILELIST *flptr, const Hq32x2 *offset) ;
static int32 StdFileFlushFile( register FILELIST *flptr ) ;
static Bool StdFileLastError( register FILELIST *flptr ) ;
static void StdFileDispose(register FILELIST *flptr) ;

static int32 LineEditFileFillBuff( register FILELIST *flptr ) ;
static int32 StatementEditFileFillBuff( register FILELIST *flptr ) ;
static Bool LineEditFileInit( FILELIST *flptr ,
                              OBJECT *args ,
                              STACK *stack ) ;
static Bool StatementEditFileInit( FILELIST *flptr ,
                                   OBJECT *args ,
                                   STACK *stack ) ;
static int32 EditFileClose( register FILELIST *flptr, int32 flag ) ;
static int32 EditFileBytes( register FILELIST *flptr, Hq32x2* avail ) ;
static int32 EditFileReset( register FILELIST *flptr ) ;
static int32 EditFilePos( register FILELIST *flptr, Hq32x2* file_pos ) ;
static int32 EditFileSetPos(register FILELIST *flptr, const Hq32x2 *offset) ;
static int32 EditFileFlushFile( register FILELIST *flptr ) ;

void init_C_globals_std_file(void)
{
  stream_echo_flag = FALSE ;
  std_files = NULL ;
  std_files_mem = NULL ;
}

void init_std_files_table( void )
{
  uint8* buff;

  HQASSERT((nulldevice != NULL),
           "init_std_files_table: No null device; devices not initialised?") ;
  HQASSERT(std_files != NULL, "Standard files memory not allocated") ;

  /* invalid file */
  init_filelist_struct(&std_files[ INVALIDFILE ] ,
                       NAME_AND_LENGTH("%invalidfile") ,
                       0 , -1 , NULL , 0 ,
                       FileError,                            /* fillbuff */
                       FileFlushBufError,                    /* flushbuff */
                       FileInitError,                        /* initfile */
                       FileCloseError,                       /* closefile */
                       StdFileDispose,                       /* disposefile */
                       FileError2,                           /* bytesavail */
                       FileError,                            /* resetfile */
                       FileError2,                           /* filepos */
                       FileError2Const,                      /* setfilepos */
                       FileError,                            /* flushfile */
                       FileEncodeError,                      /* filterencode */
                       FileDecodeError,                      /* filterdecode */
                       StdFileLastError,                     /* lasterror */
                       0 , NULL ,
                       NULL , &std_files[ LINEEDIT ]) ;

  /* lineedit */
  buff = std_files_mem;
  init_filelist_struct(&std_files[ LINEEDIT ] ,
                       NAME_AND_LENGTH("%lineedit") ,
                       READ_FLAG | EDITFILE_FLAG , -1 ,
                       buff , LINEMAX ,
                       LineEditFileFillBuff ,                /* fillbuff */
                       FileFlushBufError ,                   /* flushbuff */
                       LineEditFileInit,                     /* initfile */
                       EditFileClose ,                       /* closefile */
                       StdFileDispose ,                      /* disposefile */
                       EditFileBytes ,                       /* bytesavail */
                       EditFileReset ,                       /* resetfile */
                       EditFilePos ,                         /* filepos */
                       EditFileSetPos ,                      /* setfilepos */
                       EditFileFlushFile ,                   /* flushfile */
                       FileEncodeError,                      /* filterencode */
                       FileDecodeError ,                     /* filterdecode */
                       StdFileLastError ,                    /* lasterror */
                       0 , NULL ,
                       &std_files[ STDIN ] , &std_files[ STATEMENTEDIT ] ) ;

  /* statement edit */
  buff += LINEMAX;
  init_filelist_struct(&std_files[ STATEMENTEDIT ],
                       NAME_AND_LENGTH("%statementedit") ,
                       READ_FLAG | EDITFILE_FLAG ,  -1 ,
                       buff , STATEMAX ,
                       StatementEditFileFillBuff ,           /* fillbuff */
                       FileFlushBufError ,                   /* flushbuff */
                       StatementEditFileInit ,               /* initfile */
                       EditFileClose ,                       /* closefile */
                       StdFileDispose ,                      /* disposefile */
                       EditFileBytes ,                       /* bytesavail */
                       EditFileReset ,                       /* resetfile */
                       EditFilePos ,                         /* filepos */
                       EditFileSetPos ,                      /* setfilepos */
                       EditFileFlushFile ,                   /* flushfile */
                       FileEncodeError,                      /* filterencode */
                       FileDecodeError ,                     /* filterdecode */
                       StdFileLastError ,                    /* lasterror */
                       0 , NULL ,
                       &std_files[ LINEEDIT ] , &std_files[ STDIN ] ) ;

  /* stdin */
  buff += STATEMAX;
  init_filelist_struct(&std_files[ STDIN ] ,
                       NAME_AND_LENGTH("%stdin") ,
                       READ_FLAG | OPEN_FLAG | STDFILE_FLAG ,
                       0 , buff , BUFFSIZE ,
                       StdFileFillBuff,                      /* fillbuff */
                       FileFlushBufError ,                   /* flushbuff */
                       StdFileInit,                          /* initfile */
                       StdFileClose,                         /* closefile */
                       StdFileDispose ,                      /* disposefile */
                       StdFileBytes,                         /* bytesavail */
                       StdFileReset,                         /* resetfile */
                       StdFilePos,                           /* filepos */
                       StdFileSetPos,                        /* setfilepos */
                       StdFileFlushFile,                     /* flushfile */
                       FileEncodeError,                      /* filterencode */
                       FileDecodeError ,                     /* filterdecode */
                       StdFileLastError,                     /* lasterror */
                       0,  nulldevice , NULL ,
                       & std_files[ STDOUT ] );

  /* stdout */
  buff += BUFFSIZE;
  init_filelist_struct(&std_files[ STDOUT ] ,
                       NAME_AND_LENGTH("%stdout") ,
                       WRITE_FLAG | OPEN_FLAG | STDFILE_FLAG | LB_FLAG ,
                       1 , buff , BUFFSIZE,
                       FileError ,                           /* fillbuff */
                       StdFileFlushBuff,                     /* flushbuff */
                       StdFileInit,                          /* initfile */
                       StdFileClose,                         /* closefile */
                       StdFileDispose ,                      /* disposefile */
                       FileError2,                           /* bytesavail */
                       StdFileReset,                         /* resetfile */
                       StdFilePos,                           /* filepos */
                       StdFileSetPos,                        /* setfilepos */
                       StdFileFlushFile,                     /* flushfile */
                       FileEncodeError,                      /* filterencode */
                       FileDecodeError ,                     /* filterdecode */
                       StdFileLastError,                     /* lasterror */
                       0 , nulldevice , NULL ,
                       & std_files[ STDERR ] ) ;

  /* stderr */
  buff += BUFFSIZE;
  init_filelist_struct(&std_files[ STDERR ] ,
                       NAME_AND_LENGTH("%stderr") ,
                       WRITE_FLAG | OPEN_FLAG | STDFILE_FLAG | LB_FLAG ,
                       2 , buff , BUFFSIZE ,
                       FileError ,                           /* fillbuff */
                       StdFileFlushBuff,                     /* fillbuff */
                       StdFileInit,                          /* initfile */
                       StdFileClose,                         /* closefile */
                       StdFileDispose ,                      /* disposefile */
                       FileError2,                           /* bytesavail */
                       StdFileReset,                         /* resetfile */
                       StdFilePos,                           /* filepos */
                       StdFileSetPos,                        /* setfilepos */
                       StdFileFlushFile,                     /* flushfile */
                       FileEncodeError,                      /* filterencode */
                       FileDecodeError ,                     /* filterdecode */
                       StdFileLastError,                     /* lasterror */
                       0 , nulldevice , NULL , NULL ) ;

  HQASSERT(buff + BUFFSIZE - std_files_mem == SUM_STDFILE_BUFFSIZE,
           "init_std_files_table: using more buffer than allocated");
}

static Bool std_files_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  /** \todo ajcd 2009-11-26: These should be in the PostScript context. */
  std_files = mm_alloc_static(NUMBER_OF_STANDARD_FILES*sizeof(FILELIST));
  std_files_mem = mm_alloc_static(SUM_STDFILE_BUFFSIZE*sizeof(uint8));
  if ( std_files == NULL || std_files_mem == NULL )
    return FALSE ;

  init_std_files_table() ;

  return TRUE ;
}


void std_files_C_globals(core_init_fns *fns)
{
  fns->swstart = std_files_swstart ;
}

/*-------------------------------------------------
 *
 * Routines for FILELIST structure for standard files.
 * (stdin, stdout, stderr at startup for non-mac versions)
 *
 *-------------------------------------------------*/

static int32 StdFileFillBuff( register FILELIST *flptr )
{
  register DEVICELIST *dev ;

  HQASSERT( ! isIFilter(flptr), "StdFileFillBuff called on filter");
  if ( isIEof( flptr ))
    return EOF ;
  dev = theIDeviceList(flptr);
  HQASSERT(dev, "encountered bad file");
  if ( ( theICount( flptr ) =
        (*theIReadFile( dev ))( dev , theIDescriptor( flptr ) ,
                               theIBuffer( flptr ) ,
                               theIBufferSize( flptr ))) <= 0 )
    {
      theIReadSize(flptr) = 0;
      return ioerror_handler( flptr ) ;
    }

  theIReadSize(flptr) = theICount(flptr);
  theICount( flptr )-- ;
  theIPtr( flptr ) = theIBuffer( flptr ) + 1;
  return ( int32 ) *theIBuffer( flptr ) ;
}


static int32 StdFileFlushBuff( int32 c, register FILELIST *flptr )
{
  register DEVICELIST *dev ;

  HQASSERT( ! isIFilter(flptr), "StdFileFlushBuff called on filter");
  dev = theIDeviceList(flptr);
  HQASSERT(dev, "encountered bad file");
  *theIPtr( flptr )++ = ( uint8 ) c ;
  if ( (*theIWriteFile( dev ))( dev , theIDescriptor( flptr ) ,
                               theIBuffer( flptr ),
                               theICount( flptr )) != theICount( flptr ))
    return EOF ;
  theICount( flptr ) = 0 ;
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  return 0 ;
}


static Bool StdFileInit( FILELIST *flptr ,
                          OBJECT *args ,
                          STACK *stack )
{
  UNUSED_PARAM( OBJECT * , args );
  UNUSED_PARAM( STACK * , stack );

  HQASSERT( ! isIFilter(flptr), "StdFileInitf called on filter");
  SetIOpenFlag( flptr ) ;
  ClearIEofFlag( flptr ) ;
  ClearIIOErrFlag( flptr ) ;
  ClearITimeOutFlag( flptr ) ;
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount( flptr ) = 0 ;
  return TRUE ;
}



static int32 StdFileClose( register FILELIST *flptr, int32 flag )
{
  UNUSED_PARAM(int32, flag);

  HQASSERT( ! isIFilter(flptr), "StdFileClose called on filter");
  ClearIOpenFlag( flptr ) ;
  return ( StdFileReset( flptr )) ;
}

static int32 StdFileBytes( FILELIST *flptr, Hq32x2* avail )
/* must be an input file */
{
  /* return the number of bytes in the buffer */
  HQASSERT( ! isIFilter(flptr), "StdFileBytes called on filter");
  HQASSERT((avail != NULL),
           "StdFileBytes: NULL bytecount pointer");
  if ( theICount(flptr) != EOF ) {
    Hq32x2FromInt32(avail, theICount(flptr));
    return(0);
  } else {
    /** \todo Can count be -ve for other than EOF? */
    return(EOF);
  }
}


static int32 StdFileReset( register FILELIST *flptr )
{
  HQASSERT( ! isIFilter(flptr), "StdFileReset called on filter");
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount(flptr ) = 0 ;
  return 0 ;
}


static int32 StdFilePos( register FILELIST *flptr, Hq32x2* file_pos )
{
  /* return the offset from start of buffer */
  HQASSERT( ! isIFilter(flptr), "StdFilePos called on filter");
  HQASSERT((file_pos != NULL),
           "StdFilePos: NULL position pointer");
  Hq32x2FromInt32(file_pos, (theIPtr(flptr) - theIBuffer(flptr)));
  return(0);
}


static int32 StdFileSetPos(register FILELIST *flptr, const Hq32x2* offset)
{
  int32 off;
  int32 res;
  /* set the ptr to offset bytes within the buffer */
  HQASSERT( ! isIFilter(flptr), "StdFileFSetPos called on filter");
  HQASSERT((offset != NULL),
           "StdFileSetPos: NULL offset pointer");
  res = Hq32x2ToInt32(offset, &off);
  if ( !res || (off > theICount(flptr)) )
    return(EOF);
  theIPtr(flptr) = theIBuffer(flptr) + off;
  theICount(flptr) -= off;
  return(0);
}


static int32 StdFileFlushFile( register FILELIST *flptr )
{
  register DEVICELIST *dev ;

  HQASSERT( ! isIFilter(flptr), "StdFileFlushFile called on filter");
  dev = theIDeviceList(flptr);
  HQASSERT(dev, "encountered bad file");
  if ( isIOutputFile( flptr )) {
    if ( theICount( flptr )) {
      if (  (*theIWriteFile( dev ))( dev , theIDescriptor( flptr ) ,
                                    theIBuffer( flptr ) ,
                                    theICount( flptr )) != theICount( flptr ))
        return EOF ;
    }
  } else {
    /* read until end of file found */
    for (;;) {
      if ((*theIReadFile( dev ))( dev , theIDescriptor( flptr ) ,
                                   theIBuffer( flptr ) ,
                                   theIBufferSize( flptr )) < 0 )
        break;
    }
    SetIEofFlag( flptr ) ;
  }
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount( flptr ) = theIReadSize(flptr) = 0 ;
  return ( 0 ) ;
}

static Bool StdFileLastError( register FILELIST *flptr )
{
  UNUSED_PARAM( FILELIST * , flptr ) ;
  return error_handler( IOERROR ) ;
}

static void StdFileDispose( FILELIST *flptr )
{
  UNUSED_PARAM( FILELIST *,flptr );

  HQASSERT( ! isIFilter(flptr), "StdFileDispose called on filter" );
}

/*-------------------------------------------------
 *
 * Routines for FILELIST structure for EDIT files.
 * (lineedit and statement)
 *
 *-------------------------------------------------
*/

/* ----------------------------------------------------------------------------
   function:            mygetline()          author:              Andrew Cave
   creation date:       19-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure reads a complete line into the file "%lineedit". It
   reads characters from mystdin, and echoes to mystdout if echoing
   is set. It also decodes some control chars - assumes mystdin is not
   line buffering.
   This procedure should only be called from the lineedit fillbuff routine.

---------------------------------------------------------------------------- */


static Bool mygetline(FILELIST *flptr)
{
  register int32    i ;
  register int32    temp ;
  register FILELIST *flinptr , *floutptr ;
  register int32    count ;
  register uint8    *ptr ;

  flinptr  = theIStdin ( workingsave ) ;
  floutptr = theIStdout( workingsave ) ;

  if (! isIOpenFileFilterById( theISaveStdinFilterId( workingsave ), flinptr)) {
    SetIIOErrFlag( flptr ) ;
    return error_handler(IOERROR);
  }
  if ( stream_echo_flag &&
      ! isIOpenFileFilterById( theISaveStdoutFilterId( workingsave ), floutptr)) {
    SetIIOErrFlag( flptr ) ;
    return error_handler(IOERROR);
  }

  count = 0 ;
  ptr = theIPtr( flptr ) = theIBuffer( flptr ) ;

  if (( temp = Getc( flinptr )) == EOF ) {
    SetIEofFlag( flptr ) ;
    if ( isIIOError( flinptr )) {
      SetIIOErrFlag( flptr ) ;
      return (*theIFileLastError( flinptr ))( flinptr ) ;
    }
    else
      return FALSE ;
  }
  UnGetc( temp , flinptr ) ;

  do {
    if (( temp = Getc( flinptr )) == EOF ) {
      SetIEofFlag( flptr ) ;
      if ( isIIOError( flinptr )) {
        SetIIOErrFlag( flptr ) ;
        return (*theIFileLastError( flinptr ))( flinptr ) ;
      }
      else
        return FALSE ;
    }

    switch ( temp ) {
    case controlU :
      if ( stream_echo_flag ) {
        for ( i = 0 ; i < count ; ++i )
          if ( Putc( controlH , floutptr ) == EOF )
            return (*theIFileLastError( flptr ))( floutptr ) ;

        if ((*theIMyFlushFile( floutptr ))( floutptr ) == EOF )
          return (*theIFileLastError( flptr ))( floutptr ) ;
      }
      count = 0 ;
      break ;
    case controlR :
      if ( stream_echo_flag ) {
        if ( Putc( '\r' , floutptr ) == EOF )
          return (*theIFileLastError( flptr ))( floutptr ) ;

        for ( i = 0 ; i < count ; ++i )
          if ( Putc( theIBuffer( flptr )[ i ] , floutptr ) == EOF )
            return (*theIFileLastError( flptr ))( floutptr ) ;

        if ((*theIMyFlushFile( floutptr ))( floutptr ) == EOF )
          return (*theIFileLastError( flptr ))( floutptr ) ;
      }
      break ;
    case controlC :
      if ( stream_echo_flag ) {
        if ( Putc( '\r' , floutptr ) == EOF )
          return (*theIFileLastError( flptr ))( floutptr ) ;

        if ((*theIMyFlushFile( floutptr ))( floutptr ) == EOF )
          return (*theIFileLastError( flptr ))( floutptr ) ;
      }
      count = 0 ;
      break ;
    case controlH :
      if ( count > 0 ) {
        count-- ;
        ptr-- ;
        if ( stream_echo_flag ) {
          if ( Putc( controlH , floutptr ) == EOF )
            return (*theIFileLastError( flptr ))( floutptr ) ;

          if ((*theIMyFlushFile( floutptr ))( floutptr ) == EOF )
            return (*theIFileLastError( flptr ))( floutptr ) ;
        }
      }
      break ;
    default:
      if ( count == LINEMAX ) {
        SetIIOErrFlag( flptr ) ;
        return error_handler( IOERROR ) ;
      }
      *ptr = (uint8) temp ; count++ ; ptr++ ;

      if ( stream_echo_flag ) {
        if ( Putc( temp , floutptr ) == EOF )
          return (*theIFileLastError( flptr ))( floutptr ) ;
        if ((*theIMyFlushFile( floutptr ))( floutptr ) == EOF )
          return (*theIFileLastError( flptr ))( floutptr ) ;
      }
    }
  } while ( ! (( temp == LF ) || ( temp == CR ))) ;

  theICount( flptr ) = count ;
  return TRUE ;
}

static int32 LineEditFileFillBuff( register FILELIST *flptr )
{
  if ( isIEof( flptr ))
    return EOF ;
  if ( theIFilterState( flptr )) { /* flagged as having read line already */
    SetIEofFlag( flptr ) ;
    ClearIOpenFlag( flptr ) ;
    return EOF ;
  } else {
    theIPtr( flptr ) = theIBuffer( flptr ) ;
    theICount( flptr ) = 0 ;
    if (! mygetline( flptr ))
      return EOF;
    theICount( flptr )-- ;
    theIPtr( flptr ) = theIBuffer( flptr ) + 1;
    theIFilterState( flptr ) = TRUE ; /* flag that line's been read */
    return ( int32 ) *theIBuffer( flptr ) ;
  }
}


/* ----------------------------------------------------------------------------
   function:            getstatement()     author:              Andrew Cave
   creation date:       19-Oct-1987        last modification:   ##-###-####
   arguments:           flptr .
   description:

   This procedure reads the characters from the underlying file "%lineedit"
   until a complete statement has been read in.  A complete
   statement  is  defined as one or more P.S. tokens terminated by a newline,
   such that  no  '{'  or  '(' is  left unmatched. This procedure should
   only be called from the %statementedit fillbuff routine. After reading
   a newline character, the lineedit file must be reopened if further
   characters are required to complete the statement.

   errors:
   IOERROR, SYNTAXERROR

---------------------------------------------------------------------------- */
static Bool getstatement(FILELIST *flptr)
{
  register int32 c , last ;
  register int32 curl_count ;   /* Used to check for correct number of '{'. */
  register int32 paren_count ;  /* Used to check for correct number of '('. */
  register FILELIST *lflptr ;   /* lineedit file */
  register uint8    *ptr ;
  register int32    count ;

  count = curl_count = paren_count = 0 ;
  ptr = theIPtr( flptr ) = theIBuffer( flptr ) ;
  lflptr = theIUnderFile( flptr ) ;

  if (! isIOpenFileFilterById( theIUnderFilterId( flptr ), lflptr)) {
    SetIIOErrFlag( flptr ) ;
    return error_handler(IOERROR);
  }

  if (( c = Getc( lflptr )) == EOF ) {
    SetIEofFlag( flptr ) ;
    if ( isIIOError( lflptr )) {
      SetIIOErrFlag( flptr ) ;
      return (*theIFileLastError( lflptr ))( lflptr ) ;
    }
    else
      return FALSE ;
  }
  UnGetc( c , lflptr ) ;

  for (;;) {

    if (( c = Getc( lflptr )) == EOF ) {
      SetIEofFlag( flptr ) ;
      if ( isIIOError( lflptr )) {
        SetIIOErrFlag( flptr ) ;
        return (*theIFileLastError( lflptr ))( lflptr ) ;
      }
      else
        return FALSE ;
    }

    if ( count == STATEMAX ) {
      SetIIOErrFlag( flptr ) ;
      return error_handler( IOERROR ) ;
    }
    *ptr = (uint8) c ; ++count ; ++ptr ;

    switch ( c ) {

/* Case 1 - reading a comment */
    case '%' :
      do {
        if (( c = Getc( lflptr )) == EOF )
          return (*theIFileLastError( lflptr ))( lflptr ) ;
        if ( count == STATEMAX ) {
          SetIIOErrFlag( flptr ) ;
          return error_handler( IOERROR ) ;
        }
        *ptr = (uint8) c ;
        ++count ; ptr++ ;
      } while ( ! (( c == LF ) || ( c == CR ) || ( c == FF ))) ;
      if ( c == LF || c == CR ) {
        if (( ! paren_count ) && ( ! curl_count )) {
          theICount( flptr ) = count ;
          return TRUE ;
        }
        /* re-open lineedit file */
        if ( ! ( *theIMyInitFile( lflptr ))( lflptr , NULL , NULL ))
          return ( *theIFileLastError( lflptr ))( lflptr ) ;
      }
      break ;

/* Case 2 - reading a string */
    case '(' :
      ++paren_count ;
      last = ' ' ;
      do {
        if (( c = Getc( lflptr )) == EOF )
          return (*theIFileLastError( lflptr ))( lflptr ) ;
        if ( count == STATEMAX ) {
          SetIIOErrFlag( flptr ) ;
          return error_handler( IOERROR ) ;
        }
        *ptr = (uint8) c ;
        ++count ; ++ptr ;
        switch ( c ) {

        case '(' :
          if ( last != '\\' )
            ++paren_count;
          break ;

        case ')' :
          if ( last != '\\' )
            --paren_count;
          break ;

        case '\\' :
          if ( last == '\\' )
            c = (uint8) ' ' ;
          break ;

        case LF :
        case CR :
          /* re-open lineedit */
          if ( ! ( *theIMyInitFile( lflptr ))( lflptr , NULL , NULL ))
            return ( *theIFileLastError( lflptr ))( lflptr ) ;
        }
        last = c ;
      } while ( paren_count > 0 ) ;
      break ;

    case '{' :
      ++curl_count ;
      break ;

    case '<' :
      if (( c = Getc( lflptr )) == EOF )
        return (*theIFileLastError( lflptr ))( lflptr ) ;
      if ( count == STATEMAX ) {
        SetIIOErrFlag( flptr ) ;
        return error_handler( IOERROR ) ;
      }
      *ptr = (uint8) c ; count++ ; ptr++ ;

      if ( c != '<' ) {
        if ( c == '~' ) {
          do {
            if ( c == LF || c == CR ) { /* re-open lineedit */
              if ( ! ( *theIMyInitFile( lflptr ))( lflptr , NULL , NULL ))
                return ( *theIFileLastError( lflptr ))( lflptr ) ;
            }

            if (( c = Getc( lflptr )) == EOF )
              return (*theIFileLastError( lflptr ))( lflptr ) ;
            if ( count == STATEMAX ) {
              SetIIOErrFlag( flptr ) ;
              return error_handler( IOERROR ) ;
            }
            *ptr = (uint8) c ; count++ ; ptr++ ;
          } while ( c != '~' ) ;
        }
        else {
          while ( c != '>' ) {
            if ( ! ( isxdigit( c ) || isspace( c )))
              break ;

            if ( c == LF || c == CR ) { /* re-open lineedit */
              if ( ! ( *theIMyInitFile( lflptr ))( lflptr , NULL , NULL ))
                return ( *theIFileLastError( lflptr ))( lflptr ) ;
            }

            if (( c = Getc( lflptr )) == EOF )
              return (*theIFileLastError( lflptr ))( lflptr ) ;
            if ( count == STATEMAX ) {
              SetIIOErrFlag( flptr ) ;
              return error_handler( IOERROR ) ;
            }
            *ptr = (uint8) c ; count++ ; ptr++ ;
          }
        }
      }
      break ;

    case '}' :
      --curl_count ;
      if ( curl_count < 0 )
        curl_count = 0 ;
      break ;

    case LF :
    case CR :
      if (( ! paren_count ) && ( ! curl_count )) {
        theICount( flptr ) = count ;
        return TRUE ;
      }
      /* re-open lineedit */
      if ( ! ( *theIMyInitFile( lflptr ))( lflptr , NULL , NULL ))
        return ( *theIFileLastError( lflptr ))( lflptr ) ;
      break ;
    }
  }
}

static int32 StatementEditFileFillBuff( register FILELIST *flptr )
{
  if ( isIEof( flptr ))
    return EOF ;
  if ( theIFilterState( flptr )) { /* have read statement */
    SetIEofFlag( flptr ) ;
    ClearIOpenFlag( flptr ) ;
    return EOF ;
  } else {
    theIPtr( flptr ) = theIBuffer( flptr ) ;
    theICount( flptr ) = 0 ;
    if (! getstatement( flptr ))
      return EOF;
    theIFilterState( flptr ) = TRUE ; /* flag that line's been read */
    theICount( flptr )-- ;
    theIPtr( flptr ) = theIBuffer( flptr ) + 1;
    return ( int32 ) *theIBuffer( flptr ) ;
  }
}

static Bool LineEditFileInit( FILELIST *flptr ,
                              OBJECT *args ,
                              STACK *stack )
{
  UNUSED_PARAM( OBJECT * , args );
  UNUSED_PARAM( STACK * , stack );

  /* called when the lineedit file is opened */

  theIFilterState( flptr ) = FALSE ;
  /* set the underlying file to be the current stdin */
  theIUnderFile( flptr ) = theIStdin( workingsave ) ;
  theIUnderFilterId( flptr ) = theISaveStdinFilterId( workingsave ) ;

/*
  SetIOpenFlag( theIStdin( workingsave )) ;
  ClearIEofFlag( theIStdin( workingsave )) ;
  ClearIIOErrFlag( theIStdin( workingsave )) ;
*/
  SetIOpenFlag( flptr ) ;
  ClearIEofFlag( flptr ) ;
  ClearIIOErrFlag( flptr ) ;
  ClearITimeOutFlag( flptr ) ;
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount( flptr ) = 0 ;
  return TRUE ;
}

static Bool StatementEditFileInit( FILELIST *filter,
                                   OBJECT *args ,
                                   STACK *stack )
{
  register FILELIST *lflptr ;

  UNUSED_PARAM( OBJECT * , args );
  UNUSED_PARAM( STACK * , stack );

  /* called when the statementedit files is opened */
  theIFilterState( filter ) = FALSE ;
  lflptr = theIUnderFile( filter ) ;
  if ( lflptr ) {
    /* re-init the underlying lineedit file */
    if ( ! ( *theIMyInitFile( lflptr ))( lflptr , NULL , NULL ))
      return ( *theIFileLastError( lflptr ))( lflptr ) ;
  }
  SetIOpenFlag( filter ) ;
  ClearIEofFlag( filter ) ;
  ClearIIOErrFlag( filter ) ;
  ClearITimeOutFlag( filter ) ;
  theIPtr( filter ) = theIBuffer( filter ) ;
  theICount( filter ) = 0 ;
  return TRUE ;
}


static int32 EditFileClose( register FILELIST *flptr, int32 flag )
{
  FILELIST *lflptr ;

  UNUSED_PARAM(int32, flag);

  ClearIOpenFlag( flptr ) ;
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount( flptr ) = 0 ;
  lflptr = theIUnderFile( flptr ) ;
  if ( lflptr ) {
    if ( isIOpenFileFilterById( theIUnderFilterId(flptr) , lflptr ) &&
        isIEof( lflptr ))
      /* My reading of top of pg82 pf PLRM LL3 is that the source file for
       * %statementedit and %lineedit is explicitly closed */
      return (*theIMyCloseFile( lflptr ))( lflptr, CLOSE_EXPLICIT ) ;
  }
  return 0 ;
}


static int32 EditFileBytes( register FILELIST *flptr, Hq32x2* avail )
{
  HQASSERT((avail != NULL),
           "EditFileBytes: NULL bytecount pointer");
  if ( theICount(flptr) != EOF ) {
    Hq32x2FromInt32(avail, theICount(flptr));
    return(0);
  } else {
    /** \todo Can count be -ve for other than EOF? */
    return(EOF);
  }
}


static int32 EditFileReset( register FILELIST *flptr )
{
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount( flptr ) = 0 ;
  return ( 0 ) ;
}

static int32 EditFilePos( register FILELIST *flptr, Hq32x2* file_pos )
{
  HQASSERT((file_pos != NULL),
           "EditFilePos: NULL position pointer");
  Hq32x2FromInt32(file_pos, (theIPtr(flptr) - theIBuffer(flptr)));
  return(0);
}

static int32 EditFileSetPos(register FILELIST *flptr, const Hq32x2* offset)
{
  int32 res;
  int32 off;

  HQASSERT((offset != NULL),
           "EditFileSetPos: NULL offset pointer");
  res = Hq32x2ToInt32(offset, &off);
  if ( !res || (off > theICount(flptr)) )
    return(EOF);
  theIPtr(flptr) = theIBuffer(flptr) + off;
  return(0);
}

static int32 EditFileFlushFile( register FILELIST *flptr )
{
  SetIEofFlag( flptr ) ;
  theIPtr( flptr ) = theIBuffer( flptr ) ;
  theICount( flptr ) = 0 ;
  return ( 0 ) ;
}


/* Log stripped */
