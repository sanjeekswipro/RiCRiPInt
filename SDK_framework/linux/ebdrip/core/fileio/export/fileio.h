/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!export:fileio.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions of file handling structs and macro accessors. Include fileioh.h
 * if you just need access to file functions and blind pointers.
 */

#ifndef __FILEIO_H__
#define __FILEIO_H__


/* ----------------------------------------------------------------------------
   header file:         filestructs        author:              Dave Earl
   creation date:       9-Apr-1990
   description:

   This file describes the internal file structures used by ScriptWorks
   and the access macros for them

---------------------------------------------------------------------------- */

#include "objects.h"  /* Need full definition of OBJECT */
#include "fileioh.h"  /* Get typedefs and functions. */
#include "ascii.h"

/**
 * \defgroup filters Filters
 * \ingroup fileio
 */

/**
 * \defgroup fileio File I/O
 * \ingroup core
 */
/** \{ */

/* .....Looking for Documentation?  See block comments at end of file...... */

typedef int32 (*FILELIST_FILLBUFF)(/*@notnull@*/ /*@in@*/ FILELIST *flptr);
     /* fillbuff: for input */
typedef int32 (*FILELIST_FLUSHBUFF)(int32 c,
                                    /*@notnull@*/ /*@in@*/ FILELIST *flptr) ;
     /* flushbuff: for output */

/** FILELIST_INITFILE (described from caller's point of view)
 * -----------------
 * This routine initialised a file or filter, allocating any buffers necessary.
 * File initialisation may be passed NULL for both "args" and "stack", but
 * filters will have at least one of these parameters set.
 *
 * Normal initialisation is performed by passing "stack", but not "args"
 * through; the filter is responsible for removing all of the arguments from
 * the stack, *including* the underlying source/target object (which should
 * be passed to filter_target_or_source to set up the underlying file). The
 * filter initialisation should save its primary argument (usually a
 * dictionary) in theIParamDict(flptr).
 *
 * If "args" is provided, it takes the place of the primary argument from the
 * stack. In this case, there should not be a primary argument on the stack.
 *
 * If "args" but no "stack" is provided, the filter's underlying file should
 * not be altered, but buffers and state should be allocated and initialised
 * as usual. This case indicates that the filter is being re-opened, and can
 * happen when a PDF input stream is re-wound to its start.
 *
 * WARNING: For some other (non-filter) file types, FILELIST_INITFILE
 * ignores args and does not affect the stack. */
typedef Bool (*FILELIST_INITFILE)(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                                  /*@null@*/ /*@in@*/ OBJECT *args,
                                  /*@null@*/ /*@in@*/ STACK *stack) ;
typedef int32 (*FILELIST_CLOSEFILE)(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                                    int32 flag) ;
typedef void (*FILELIST_DISPOSEFILE)(/*@notnull@*/ /*@in@*/ FILELIST *flptr) ;
typedef int32 (*FILELIST_BYTESAVAILABLE)(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                                         /*@notnull@*/ /*@in@*/ Hq32x2* avail) ;
typedef int32 (*FILELIST_RESETFILE)(/*@notnull@*/ /*@in@*/ FILELIST *flptr) ;
typedef int32 (*FILELIST_FILEPOSITION)(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                                       /*@notnull@*/ /*@in@*/ Hq32x2* filepos);
typedef int32 (*FILELIST_SETFILEPOSITION)(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                                          /*@notnull@*/ /*@in@*/ const Hq32x2* offset);
typedef int32 (*FILELIST_FLUSHFILE)(/*@notnull@*/ /*@in@*/ FILELIST *flptr);
typedef Bool (*FILELIST_FILTERENCODE)(/*@notnull@*/ /*@in@*/ FILELIST *flptr ) ;
typedef Bool (*FILELIST_FILTERDECODE)(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                                      /*@notnull@*/ /*@out@*/ int32 *bytes ) ;
typedef Bool  (*FILELIST_DECODEINFO)(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                                     /*@null@*/ /*@in@*/ imagefilter_match_t *match) ;
typedef Bool (*FILELIST_LAST_ERROR)(/*@notnull@*/ /*@in@*/ FILELIST *flptr) ;

/* .....Looking for Documentation?  See block comments at end of file...... */
/** \brief The Harlequin RIP file type. */
struct FILELIST { /* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
  TypeTag typetag ;            /**< GC type tag. MUST be first field. */
  int8 unused1 ;
  uint16 len ;                 /**< Length of file name. */
  /*@owned@*/ uint8 *clist ;   /**< File name storage. */
  uint16 filter_id ;           /**< Re-use count for filters. */
  int16 error ;     /**< Deferred PS error (only applicable to filters). */
  uint32 flags ;               /**< Flags. */
  union {
    /*@owned@*/ void *filterprivate ;   /**< Private filter state structure. */
    DEVICE_FILEDESCRIPTOR descriptor ;  /**< Descriptor of real files. */
  } u ;
  /*@owned@*/ uint8 *buffer ;  /**< Buffer into which bytes are put. */
  int32 count ;                /**< The number of bytes left in the buffer. */
  /*@dependent@*/ uint8 *ptr ; /**< Current position in buffer. */
  int32 buffersize ;           /**< The size of this buffer. */
  int32 readsize ;        /**< Number of bytes read in last decode call. */
  FILELIST_FILLBUFF fillbuff ;
  FILELIST_FLUSHBUFF flushbuff ;
  FILELIST_INITFILE myinitfile ;
  FILELIST_CLOSEFILE myclosefile ;
  FILELIST_DISPOSEFILE mydisposefile ;
  FILELIST_BYTESAVAILABLE mybytesavailable ;
  FILELIST_RESETFILE myresetfile ;
  FILELIST_FILEPOSITION myfileposition ;
  FILELIST_SETFILEPOSITION mysetfileposition ;
  FILELIST_FLUSHFILE myflushfile ;
  FILELIST_FILTERENCODE filterencode ;
  FILELIST_FILTERDECODE filterdecode ;
  FILELIST_DECODEINFO decodeinfo ;
  FILELIST_LAST_ERROR last_error ;
  int32 filter_state ;
  int32 lineno ;
  /*@dependent@*/ DEVICELIST *device ;
  /*@dependent@*/ FILELIST *underlying_file ;
  /*@dependent@*/ FILELIST *next ;
  uint16 underlying_filter_id ;
  uint8 last_char ;
  uint8 sid ;
  OBJECT param_dict ;
  int32 pdf_context_id ;
} ;

/** Initialisation function for FILELISTs. This function is here because it
    requires intimate knowledge of the FILELIST struct. */
void init_filelist_struct(/*@notnull@*/ /*@out@*/ register FILELIST *flptr,
                          uint8    *name,
                          int32    len,
                          int32    flags,
                          DEVICE_FILEDESCRIPTOR descriptor,
                          uint8    *buffer,
                          int32    buffsize,
                          FILELIST_FILLBUFF fillbuff,
                          FILELIST_FLUSHBUFF flushbuff,
                          FILELIST_INITFILE initfile,
                          FILELIST_CLOSEFILE closefile,
                          FILELIST_DISPOSEFILE disposefile,
                          FILELIST_BYTESAVAILABLE bytesavail,
                          FILELIST_RESETFILE resetfile,
                          FILELIST_FILEPOSITION filepos,
                          FILELIST_SETFILEPOSITION setfilepos,
                          FILELIST_FLUSHFILE flushfile,
                          FILELIST_FILTERENCODE filterencode,
                          FILELIST_FILTERDECODE filterdecode,
                          FILELIST_LAST_ERROR lasterror,
                          int32    filterstate,
                          DEVICELIST *dev,
                          FILELIST *underfile,
                          FILELIST *next );

/** List of strings struct for new filenameforall */
struct SLIST {
  /*@owned@*/ uint8 *text;
  /*@dependent@*/ struct SLIST *next;
};

#define FILEBUFFSIZE     (16 * 1024) /* for real files */
#define FONTFILEBUFFSIZE (4 * 1024)  /* for font files */

/* flag values used in the length field of a file object to indicate
   run file and filter id: access via macros below */
#define ISRUNFILE        ((uint16)0x8000)
#define FIRSTFILTERID    ((uint16)1)
#define LASTFILTERID     ((uint16)(ISRUNFILE - 1))

#define GetNextBuf( f ) ((*(f)->fillbuff)(f))

#define Getc( f ) \
  (--(f)->count >= 0? ((int32)*(f)->ptr++) : GetNextBuf(f) )

#define UnGetc( c , f ) \
  ((isIFilter(f))?(FilterUnGetc(f)):((f)->count++ , (f)->ptr--))

#define Putc( c , f ) \
  (++(f)->count >= (f)->buffersize ?\
       (*(f)->flushbuff)((int32)(c), f) :\
       (((int32)c == LF) && ((int32)((f)->flags) & LB_FLAG) ?\
            (*(f)->flushbuff)((int32)(c), f) :\
            (int32) (*((f)->ptr++) = (uint8) (c))))

/** Transparent Putc for filters - no check made for LB_FLAG */
#define TPutc( c , f )\
  (++(f)->count >= (f)->buffersize ?\
       (*(f)->flushbuff)((int32)(c), (f)) :\
       (int32) (*((f)->ptr++) = (uint8) (c)))


/* .....Looking for Documentation?  See block comments at end of file...... */
#define EOF_FLAG       0x0001
#define READ_FLAG      0x0002
#define WRITE_FLAG     0x0004
#define OPEN_FLAG      0x0008
#define STDFILE_FLAG   0x0010
#define EDITFILE_FLAG  0x0020
#define REALFILE_FLAG  0x0040
#define FILTER_FLAG    0x0080
#define LB_FLAG        0x0100
#define IOERR_FLAG     0x0200
#define TIMEOUT_FLAG   0x0400
#define REWINDABLE     0x0800
#define DONEFILL       0x1000
#define GOTCR          0x2000
#define SKIPLF         0x4000
#define BASE_FILE_FLAG 0x8000
#define RSD_FLAG       0x10000
#define DELIMITS_FLAG  0x20000
#define EXPANDS_FLAG   0x40000
#define THRESHOLD_FLAG 0x80000
#define CLOSING_FLAG   0x100000
#define CST_FLAG       0x200000
#define CTRLD_FLAG     0x400000
#define PURGE_NOTIFY_FLAG 0x800000
#define FGCFLAG        0x80000000
#define RW_FLAG        (WRITE_FLAG | READ_FLAG)
#define ACCESS_BITS    (WRITE_FLAG | READ_FLAG)

/* clear/set file flags */

#define ClearIEofFlag(f)         (f)->flags &= ~EOF_FLAG
#define SetIEofFlag(f)           (f)->flags |= EOF_FLAG
#define ClearIOpenFlag(f)        (f)->flags &= ~OPEN_FLAG
#define SetIOpenFlag(f)          (f)->flags |= OPEN_FLAG
#define ClearILineBuffFlag(f)    (f)->flags &= ~LB_FLAG
#define SetILineBuffFlag(f)      (f)->flags |= LB_FLAG
#define ClearIIOErrFlag(f)       (f)->flags &= ~IOERR_FLAG
#define SetIIOErrFlag( f)        (f)->flags |= IOERR_FLAG
#define ClearITimeOutFlag(f)     (f)->flags &= ~TIMEOUT_FLAG
#define SetITimeOutFlag(f)       (f)->flags |= TIMEOUT_FLAG
#define ClearIRewindableFlag(f)  (f)->flags &= ~REWINDABLE
#define SetIRewindableFlag(f)    (f)->flags |= REWINDABLE
#define ClearIRSDFlag(f)         (f)->flags &= ~RSD_FLAG
#define SetIRSDFlag(f)           (f)->flags |= RSD_FLAG
#define ClearIThresholdFlag(f)   (f)->flags &= ~THRESHOLD_FLAG
#define SetIThresholdFlag(f)     (f)->flags |= THRESHOLD_FLAG
#define ClearIClosingFlag(f)     (f)->flags &= ~CLOSING_FLAG
#define SetIClosingFlag(f)       (f)->flags |= CLOSING_FLAG
#define ClearICSTFlag(f)         (f)->flags &= ~CST_FLAG
#define SetICSTFlag(f)           (f)->flags |= CST_FLAG
#define ClearICTRLDFlag(f)       (f)->flags &= ~CTRLD_FLAG
#define SetICTRLDFlag(f)         (f)->flags |= CTRLD_FLAG

#define SetIDoneFillFlag(f)      (f)->flags |= DONEFILL
#define ClearIDoneFillFlag(f)    (f)->flags &= ~DONEFILL
#define SetICRFlags(f)           (f)->flags |= ( GOTCR | SKIPLF)
#define ClearICRFlags(f)         (f)->flags &= ~(GOTCR | SKIPLF)
#define SetIGotCRFlag(f)         (f)->flags |= GOTCR

/* File flag tests */

#define isIEof( f )              ((f)->flags & EOF_FLAG)
#define isIInputFile( f )        ((f)->flags & READ_FLAG)
#define isIOutputFile( f )       ((f)->flags & WRITE_FLAG)
#define isIReadWriteFile( f )    (((f)->flags & RW_FLAG) == RW_FLAG)
#define isIOpenFile( f )         ((f)->flags & OPEN_FLAG)
#define isIStdFile( f )          ((f)->flags & STDFILE_FLAG)
#define isIEditFile( f )         ((f)->flags & EDITFILE_FLAG)
#define isIRealFile( f )         ((f)->flags & REALFILE_FLAG)
#define isIFilter( f )           ((f)->flags & FILTER_FLAG)
#define isILineBuff( f )         ((f)->flags & LB_FLAG)
#define isIIOError( f )          ((f)->flags & IOERR_FLAG)
#define isITimeOutError( f )     ((f)->flags & TIMEOUT_FLAG)
#define isIRewindable( f )       ((f)->flags & REWINDABLE)
#define isIDoneFill( f )         ((f)->flags & DONEFILL)
#define isIGotCR( f )            ((f)->flags & GOTCR )
#define isISkipLF( f )           ((f)->flags & SKIPLF )
#define isIBaseFile( f )         ((f)->flags & BASE_FILE_FLAG )
#define isIRSDFilter( f )        ((f)->flags & RSD_FLAG)
#define isIDelimitsData( f )     ((f)->flags & DELIMITS_FLAG)
#define isIExpandsData( f )      ((f)->flags & EXPANDS_FLAG)
#define isIThreshold( f )        ((f)->flags & THRESHOLD_FLAG)
#define isIClosing( f )          ((f)->flags & CLOSING_FLAG)
#define isICST( f )              ((f)->flags & CST_FLAG)
#define isICTRLD( f )            ((f)->flags & CTRLD_FLAG)

#define isICRFlags( f )          ((f)->flags & (SKIPLF | GOTCR))

/** \page crlf CR-LF flags:
 *
 * There are two flags associated with CR's.
 *  GOTCR
 *  SKIPLF
 * In the scanner, if a CR is encountered, both flags are set.
 * In read and readstring, an initial LF is skipped only if SKIPLF is set, and
 * only GOTCR is set at the end if the last character consumed is a CR.
 * In readline, an initial LF is skipped if either SKIPLF or GOTCR is set.
 * Both flags are set if the last character read is a CR.
 * Therefore GOTCR is used for line counting (if the system parameter
 * CountLines is true), and SKIPLF is used to indicate to the binary routines
 * that the previous reading-operation was *not* a binary one.
 */


/* isIOpenFileFilter and isIOpenFileFilterById cross check between filter and
   referencing object or specified filter id as well as checking the openness
   of the underlying file. This is because a filter whose id is out of date is
   by definition closed and it is invalid to access the filter pointed at - it
   is being reused. The same test is (now) valid for files because the filter id
   part of the length field is always zero for files (well, should be) */
#define theFilterIdPart( n_ ) ( (n_) & ~ ISRUNFILE )

#define isIOpenFileFilterById(id_, flptr_) \
   (theFilterIdPart(id_) == theIFilterId(flptr_) && isIOpenFile(flptr_))

#define isIOpenFileFilter(filtero_, flptr_) \
   (isIOpenFileFilterById(theLen(*(filtero_)), (flptr_)))

#define IsRunFile( theo_ ) ((theLen(*(theo_)) & ISRUNFILE) == ISRUNFILE)

#define SetRunFile( theo_ ) MACRO_START \
  theLen(*(theo_)) = (uint16) (theLen(*(theo_)) | ISRUNFILE); \
MACRO_END

#define ClearRunFile( theo_ ) MACRO_START \
  theLen(*(theo_)) = (uint16) theFilterIdPart(theLen(*(theo_))); \
MACRO_END


/* Macros on FILE objects. */
/* .....Looking for Documentation?  See block comments at end of file...... */
#define theIFlags(val)            ((val)->flags)
#define theIFillBuffer(val)       ((val)->fillbuff)
#define theIFlushBuffer(val)      ((val)->flushbuff)
#define theIDescriptor(val)       ((val)->u.descriptor)
#define theIFilterPrivate(val)    ((val)->u.filterprivate)
#define theBufferSize(val)        ((val).buffersize)
#define theIBufferSize(val)       ((val)->buffersize)
#define theBuffer(val)            ((val).buffer)
#define theIBuffer(val)           ((val)->buffer)
#define thePtr(val)               ((val).ptr)
#define theIPtr(val)              ((val)->ptr)
#define theISaveLevel(val)        ((val)->sid)
#define theIFilterId(val)         ((val)->filter_id)
#define theIError(val)            ((val)->error)
#define theIMyBytesAvailable(val) ((val)->mybytesavailable)
#define theIMyInitFile(val)       ((val)->myinitfile)
#define theIMyCloseFile(val)      ((val)->myclosefile)
#define theIMyDisposeFile(val)    ((val)->mydisposefile)
#define theIMyResetFile(val)      ((val)->myresetfile)
#define theIMyFlushFile(val)      ((val)->myflushfile)
#define theIFilterEncode(val)     ((val)->filterencode)
#define theIFilterDecode(val)     ((val)->filterdecode)
#define theIFilterDecodeInfo(val) ((val)->decodeinfo)
#define theIFilterGCMark(val)     ((val)->gc_mark)
#define theIFileLastError(val)    ((val)->last_error)
#define theIFilterState(val)      ((val)->filter_state)
#define theIUnderFile(val)        ((val)->underlying_file)
#define theIUnderFilterId(val)    ((val)->underlying_filter_id)
#define theFileLineNo(val)        ((val).lineno)
#define theIFileLineNo(val)       ((val)->lineno)
#define theIMyFilePos(val)        ((val)->myfileposition)
#define theIMySetFilePos(val)     ((val)->mysetfileposition)
#define theDeviceList(val)        ((val).device)
#define theIDeviceList(val)       ((val)->device)
#define theCount(val)             ((val).count)
#define theICount(val)            ((val)->count)
#define theILastChar(val)         ((val)->last_char)
#define theIReadSize(val)         ((val)->readsize)
#define theIParamDict(val)        ((val)->param_dict)
#define theIPDFContextID(val)     ((val)->pdf_context_id)

/* Filter macros */

/** Filter state definitions */
enum {
  FILTER_INIT_STATE,     /**< Filter has not read any data yet. */
  FILTER_EMPTY_STATE,    /**< Filter has not hit EOF yet. */
  FILTER_LASTCHAR_STATE, /**< Filter has found EOF, but still has data. */
  FILTER_EOF_STATE,      /**< Filter has found EOF, has no data. */
  FILTER_ERR_STATE       /**< Filter has encountered an error, has no data. */
} ;

/** \page flags File closing reason flags
 *
 * The second argument for theIMyCloseFile should be one of the following 3
 * values.  CLOSE_EXPLICIT and CLOSE_IMPLICIT are to distinguish between PS
 * style 'close when reach EOF' and closefile type operations.  CLOSE_FORCE is
 * to handle implicit closing at restore or garbage collection time and is
 * needed to handle circular RSDs (a Hqn extension) used with PS halftone
 * threshold files.
 *
 * Normal PS and PDF work should only need to pass CLOSE_EXPLICIT.
 * CLOSE_IMPLICIT is only used with PS file reading operators and all required
 * occurences should now be present.  CLOSE_FORCE should not be used outside of
 * restore or garbage collection since the only time it is needed is from PS for
 * ht threshold files from currenthalftone - not relevant for PDF.
 *
 * Note: Starts at 2 as 0 and 1 were used in a earlier version and this catches
 *       any calls missed when changing over.
 */
#define CLOSE_EXPLICIT  (2)
#define CLOSE_IMPLICIT  (3)
#define CLOSE_FORCE     (4)

/** \struct FILELIST

Description of the FILELIST Structural and Procedural Interface

See fileio.c for comments on the buffering scheme.

 The types of files are:
 - Bottom level files: (no underlying files)
   - Standard files (%stdin, %stdout, %stderr at boot up)
   - Other files connecting to real files
 - High Level files: (sits on top of other high level files or bottom files)
   - Edit files %lineedit, %statementedit
   - Filters

 All bottom level files have a pointer to the "device" on which the file
 was created. The device structure provides the calls which provide the
 interface between SW and the OS. (See swdevice.h)


typetag:
  Type tag used by the typed pool GC, see COREobjects!src:objects.c.

clist, len:
  The name of the file, and its length. (It is usually NULL terminated
  as well). The length will be 0 for a file created on a non-device
  relative device (eg. %config%).

filter_id:
  ID of a file or filter. This number is incremented when a FILELIST structure
  is re-used, so file objects and FILELISTs retaining pointers to it will be
  able to tell if it is the same file that they had referred to previously.

error:
  Deferred PS error (only used by filters).

flags:
  A whole bunch of flags:
  - EOF_FLAG      Set when the file/filter has reached end-of-file
  - READ_FLAG     set if the file is an input file / decode filter
  - WRITE_FLAG    set if the file is an output file / encode filter
  - OPEN_FLAG     set if the file is still open
  - STDFILE_FLAG  set if the file is the initial built-in default
                  %stdin, %stdout or %stderr.
  - EDITFILE_FLAG set if the file is %linedit or %statementedit.
  - REALFILE_FLAG set if the file is a file opened on a device.
  - FILTER_FLAG   set if the file is a filter.
  - LB_FLAG       set if the output file must be line-buffered.
  - IOERR_FLAG    set by the file/filter fillbuff routine if it encounters
                  an error. (i.e., it is only checked after a Getc returns EOF)
  - REWINDABLE    set if the filter is rewindable, as in the case of pdf thresholds.
  - RSD_FLAG      set if the filter is a ReusableStreamDecode filter.
                  Next 2 flags are only for decode filters (used by RSD Filter).
  - DELIMITS_FLAG set if filter does no data translation, only delimits data.
  - EXPANDS_FLAG  set if filter expands the data, eg LZW.
  - THRESHOLD_FLAG set when we internally layer an rsd on top of a threshold screen.
  - CLOSING_FLAG  set when a filter is closing. For output filters, this signals to
                  the encode routine that it must write any state bytes such as
                  checksums and EOD markers as well as any pending data.
  - CST_FLAG      reflects the value of the CloseSource or CloseTarget flag passed
                  to a filter on initialisation. When the filter closes, if this
                  flag is set, the underlying file is also closed.
  - PURGE_NOTIFY_FLAG set if fileio_secure_pdf_filters should be called
                  when the filter is purged
  - CTRLD_FLAG    set if the PS interpreter should detect ^D EOF marker.
  - TIMEOUT_FLAG  not used yet.
  - DONEFILL      internal flag for read/write files
  - GOTCR         see description below
  - SKIPLF        see description below


  CR-LF flags:
    There are two flags associated with end-of-lines.

  -   GOTCR
  -   SKIPLF

    In the scanner, if a CR is encountered, both flags are set.
    In read and readstring, an initial LF is skipped only if SKIPLF is set, and
    only GOTCR is set at the end if the last character consumed is a CR.
    In readline, an initial LF is skipped if either SKIPLF or GOTCR is set.
    Both flags are set if the last character read is a CR.
    Therefore GOTCR is used for line counting (if the system parameter
    CountLines is true), and SKIPLF is used to indicate to the binary routines
    that the previous reading-operation was *not* a binary one.


descriptor/filterprivate:
  - for a real file: the descriptor returned by the open_file call on the
                    device.

  - for a filter: points to a structure which is used to maintain state
                 for the filter.

buffer:
  This is the pointer to the buffer into which bytes are:
     - inserted by the fillbuff routine and extracted by the Getc macro for an
       input file.
     - inserted by the Putc macro and extracted by the flushbuff routine for
       an output file.

count:
  The number of bytes left in the buffer.

ptr:
  The point in the buffer for the next byte:
    - to be extracted by a Getc macro for an input file.
    - to be inserted by a Putc macro for an output file.

buffersize:
  The size of this buffer.

fillbuff:
  When the buffer is empty for an input file, the next Getc macro will
  call the fillbuff routine to get some more data. A filter implementation
  of this routine is responsible for reading from its underlying file
  (using Getc) and performing the appropriate decoding, and inserting the
  results into the buffer.

  This routine must return with the first byte in the buffer, and advance the
  ptr and decrement the count. It should return EOF if there is no more data
  left in that file.

  If an error occurs getting the next buffer of data, the routine must
  set the IOERR_FLAG and return EOF. It is the responsibility of the RIP to
  call the last_error routine for the file soon after this happens.

flushbuff:
  When the buffer is full less 1 byte for an output file, the next
  Putc macro will insert the byte into the last slot of the buffer, and
  call the flushbuff routine. A filter implementation of this routine
  is responsible for encoding the buffer, and writing the results out to the
  underlying file (using Putc).

  If something goes wrong with flushing the buffer, then this routine
  must return EOF.

myinitfile:
  This routine is called when a real file is opened by the file operator, or
  when a filter is opened by the filter operator. It is resposible for
  allocating the buffer space and, for filters, initialising the state
  of the filter. Callers should make sure they call the last_error routine
  (see below) if this fails to make sure any buffers allocated before the
  failure are not leaked.

myclosefile:
  A real file routine must call the close on the underlying device, then free
  the buffer.

  A filter close routine must free the buffer and any other structures
  allocated to maintain filter state. If the underlying file is a procedure
  source or the close source/target flag is set then this must be closed
  as well.

  Finally the routine should clear the OPEN_FLAG except for RSD filters that are
  being implicitly closed (i.e. when PS reaches EOF).

  A flag is passed in indicating how the file/filter is being closed -
  explicitly from something like closefile, implicitly by reaching EOF, or a
  forced close due to a restore or garbage collection.

  If anything goes wrong, this routine should return -1. The RIP must not
  call last_error - it must simply assume an IOERROR.

mydisposefile:
  The routine called by the close and last error routines to dispose of
  the buffer and any other private state structures. Must do the right
  thing with respect to pointers: in particular it must work with partially
  constructed filters or files and with multiple invocations. Consequently
  it must set pointers to NULL after freeing buffers.

mybytesavailable:

  Not defined for filters.
  For real files, it must call the devices bytes_file routine.

myresetfile:
  Simply reset the ptr and count variables

myfileposition:
  Not defined for filters.
  For real files, call the seek on the device.

mysetfileposition:
  Not defined for filters.
  For real files, call the seek on the device.

myflushfile:
  - Real file:
    - Input file - seek to the end of the file, setting EOF_FLAG.
    - Output file - flush the current buffer.

  - Filter:
    - Encoding:
      Call the encodebuffer routine for the filter to flush as much as possible
      from the filters buffer.
    - Decoding:
      Repeatedly call the decodebuffer routine to flush until the underlying
      file reaches EOF. Throw away the result. Close the filter.

filterencode:
  The filter-specific routine to encode the filters buffer. Called by the
  generic filter fillbuff. If CLOSINGFLAG is set, the bytes written should
  include any arising from the filter state such as EODs, checksums etc.

filterdecode:
  The filter-specific routine to fill the filters buffer with decoded
  data. Called by the generic filter fillbuff. May change filter.buffer, but
  filter.ptr and filter.count are managed by fillbuff.

decodeinfo:
  Defined for image filters only. Matches a list of requests for
  information about the image and calls functions to provide the
  information from the image.

last_error:
  If the file/filter is still open, free up buffers and other memory allocated
  to it, and return the appropriate error. Filter routines should have set up
  an error before returning abnormally, rather than relying on the last error
  routine. A caller of file/filter routines which gets an error should ensure
  it calls the last_error routine to clean things up and prevent leaks. The
  last_error routine should also set/reset appropriate flags to reflect the
  dead status of the FILELIST slot.

filter_state:
  Used by the generic filter fillbuff routine in order to implement the
  close-encoding-filters-on-last-character-returned-although-Adobe-do-not-
  implement-it.

lineno:
  Count of lines for an input file. The number is set to a negative number
  when the scanner moves to a new line, but the last token on the previous
  line has not been consumed yet.

device:
  Pointer to the device on which the real file exists.

underlying_file:
  The FILELIST struct of the underlying file/filter for a filter.

next:
  Linked list of FILELIST structs.

underlying_filter_id:
  If the underlying file is a filter, maintains the id so we can tell if
  the filelist to which underlying_file points has been reused.

last_char:
  Last character in filter AFTER buffer has gone away.

sid:
  The save level at which the file was created. When the RIP restores to a save
  level less than this number, the file/filter needs to be closed. See the
  myclosefile description.

param_dict:
  The params used to create a filter; null for files.

pdfcontext_id:
  The id of the PDF context in which the file object was created, 0 for PS.

*/

/** \} */

#endif /* protection for multiple inclusion */


/* Log stripped */
