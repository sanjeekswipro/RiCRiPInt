/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:fileops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * File operations at PostScript level; opening, closing, reading, writing
 */

#include "core.h"
#include "swdevice.h"
#include "swoften.h"
#include "swerrors.h"
#include "swctype.h"
#include "objects.h"
#include "hqmemcmp.h"
#include "fonts.h" /* charcontext_t */
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "tables.h"
#include "rsd.h"
#include "devices.h"
#include "devparam.h"

#include "params.h"
#include "psvm.h"
#include "stacks.h"
#include "dicthash.h"
#include "display.h"
#include "progress.h"
#include "control.h"
#include "miscops.h"
#include "fileops.h"
#include "swmemory.h"
#include "chartype.h"
#include "std_file.h"
#include "filename.h"

/* ----------------------------------------------------------------------------
   function:            file_()            author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 155.

---------------------------------------------------------------------------- */


Bool file_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 ;
  int32 openflags , psflags;
  Bool append_mode = FALSE ;
  int32 i ;
  int8 access[ 5 ] ;
  int32 baseflag ;
  OBJECT fileo = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 1 , & operandstack ) ;
  o2 = theTop( operandstack ) ;

  if ( oType(*o1) != OSTRING || oType(*o2) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanRead(*o2) && !object_access_override(o2)) )
    return error_handler( INVALIDACCESS ) ;

  if ( theLen(*o2) == 0 )
    return error_handler( INVALIDFILEACCESS );

  /* Expect: (r{+}{@}{&}{!}) in that order, where {} indicates optional. */
  i = 0 ;
  access[ i ] = oString(*o2)[i] ;
  i++ ;
  switch ( theLen(*o2)) {
  case 5 :
    access[ i ] = oString(*o2)[i] ;
    i++ ;
  case 4 :
    access[ i ] = oString(*o2)[i] ;
    i++ ;
  case 3 :
    access[ i ] = oString(*o2)[i] ;
    i++ ;
  case 2 :
    access[ i ] = oString(*o2)[i] ;
    i++ ;
  case 1 :
    access[ i ] = '\0' ;
    break ;
  default :
    return error_handler( INVALIDFILEACCESS ) ;
  }

  baseflag = 0 ;

  i = 0 ;
  switch ( access[ i++ ] ) {
  case 'r' :
    if ( access[ i ] == '+' ) {
      openflags = SW_RDWR | SW_FROMPS ;
      psflags = READ_FLAG | WRITE_FLAG ;
      i++ ;
    }
    else {
      openflags = SW_RDONLY | SW_FROMPS ;
      psflags = READ_FLAG ;
    }
    break ;
  case 'w' :
    if ( access[ i ] == '+' ) {
      openflags = SW_RDWR | SW_CREAT | SW_TRUNC | SW_FROMPS ;
      psflags = READ_FLAG | WRITE_FLAG;
      i++ ;
    }
    else {
      openflags = SW_WRONLY | SW_CREAT | SW_TRUNC | SW_FROMPS ;
      psflags = WRITE_FLAG ;
    }
    break ;
  case 'a' :
    if ( access[ i ] == '+' ) {
      openflags = SW_RDWR | SW_CREAT | SW_FROMPS ;
      psflags = READ_FLAG | WRITE_FLAG ;
      i++ ;
    }
    else {
      openflags = SW_WRONLY | SW_CREAT | SW_FROMPS ;
      psflags = WRITE_FLAG ;
    }
    append_mode = TRUE ;
    break ;
  default :
    return error_handler( INVALIDFILEACCESS ) ;
  }
  if ( access[ i ] == '@' ) {
    openflags = openflags | SW_FONT ;
    i++ ;
  }
  if ( access[ i ] == '&' ) {
    baseflag = BASE_FILE_FLAG ;
    i++ ;
  }
  if ( access[ i ] == '!' ) {
    /* Overkill? */
    if ( (psflags & READ_FLAG) == 0 ) {
      return error_handler(INVALIDFILEACCESS) ;
    }
    psflags |= CTRLD_FLAG;
    i++;
  }

  if ( !file_open(o1, openflags, psflags, append_mode, baseflag, &fileo) )
    return FALSE;

  npop( 2 , & operandstack ) ;
  return push(&fileo, &operandstack) ;
}

Bool ps_file_standard(uint8 *name, int32 len, int32 flags, OBJECT *file)
{
  Bool glmode = TRUE ;
  FILELIST *flptr = NULL;
  uint16 filter_id = 0 ;
  int32 psflags = 0 ;

  HQASSERT(name, "No standard file name") ;
  HQASSERT(file, "No standard file return object") ;

  if ( (flags & (SW_RDWR|SW_RDONLY)) != 0 )
    psflags |= READ_FLAG ;
  if ( (flags & (SW_RDWR|SW_WRONLY)) != 0 )
    psflags |= WRITE_FLAG ;

  /* special case handling for stdin etc */
  if ( HqMemCmp(name, len, NAME_AND_LENGTH("stdin")) == 0 ) {
    flptr = theIStdin( workingsave ) ;
    filter_id = theISaveStdinFilterId( workingsave ) ;
  } else if ( HqMemCmp(name, len, NAME_AND_LENGTH("stdout")) == 0 ) {
    flptr = theIStdout( workingsave ) ;
    filter_id = theISaveStdoutFilterId( workingsave ) ;
  } else if ( HqMemCmp(name, len, NAME_AND_LENGTH("stderr")) == 0 ) {
    flptr = theIStderr( workingsave ) ;
    filter_id = theISaveStderrFilterId( workingsave ) ;
  }

  if (flptr) {
    FILELIST *tflptr ;
    filelist_iterator_t iter ;

    /* In case stdin/stdout are local not global - inefficient (AC).*/
    for ( tflptr = filelist_first(&iter, TRUE, FALSE) ; /* Local only */
          tflptr ;
          tflptr = filelist_next(&iter) ) {
      if ( tflptr == flptr ) {
        glmode = FALSE ;
        break ;
      }
    }
  } else if ( HqMemCmp (name, len, NAME_AND_LENGTH("lineedit")) == 0 ) {
    flptr = &std_files[ LINEEDIT ] ;
    /* call the file's init routine */
    if ( ! ( *theIMyInitFile( flptr ))( flptr , NULL , NULL ))
      return ( *theIFileLastError( flptr ))( flptr ) ;
    filter_id = theIFilterId( flptr ) ;
  } else if ( HqMemCmp (name, len, NAME_AND_LENGTH("statementedit")) == 0 ) {
    flptr = &std_files[ STATEMENTEDIT ] ;
    /* call the file's init routine */
    if ( ! ( *theIMyInitFile( flptr ))( flptr , NULL , NULL ))
      return ( *theIFileLastError( flptr ))( flptr ) ;
    filter_id = theIFilterId( flptr ) ;
  } else if ( HqMemCmp (name, len, NAME_AND_LENGTH("invalidfile")) == 0 ) {
    flptr = &std_files[ INVALIDFILE ] ;
    filter_id = theIFilterId( flptr ) ;
  } else {
    return error_handler( UNDEFINEDFILENAME ) ;
  }

  if ( flptr != &std_files[ INVALIDFILE ] )
    if ( psflags != (int32)( theIFlags( flptr ) & ACCESS_BITS ))
      return error_handler( INVALIDFILEACCESS ) ;

  /* Setup the file object */
  theTags(*file) = (uint8)(OFILE | LITERAL |
                           ((psflags & WRITE_FLAG) ? UNLIMITED : READ_ONLY)) ;
  SETGLOBJECTTO(*file, glmode) ;
  theLen(*file) = filter_id ;
  oFile(*file) = flptr ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            closefile_()       author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 129.

---------------------------------------------------------------------------- */


Bool closefile_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  if ( !file_close(theo) )
    return FALSE ;

  pop( & operandstack ) ;

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            read_()            author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 198.

---------------------------------------------------------------------------- */
Bool read_(ps_context_t *pscontext)
{
  register int32 temp ;
  register FILELIST *flptr ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  temp = theStackSize( operandstack ) ;
  if ( EmptyStack( temp ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , temp ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK) ;

  if ( ! oCanRead(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  flptr = oFile(*theo) ;

  /* it valid to look at isIInputFile before checking if it
     is a dead filter, because the direction of any reused filter
     is guaranteed to be the same */
  if ( ! isIInputFile( flptr ))
    return error_handler( INVALIDACCESS ) ;

  if ( ! isIOpenFileFilter( theo , flptr )) { /* Implies that associated file is closed. */
    Copy( theo , (& fnewobj)) ;
    return TRUE ;
  }

  if (theIFileLineNo(flptr) < 0) {
    theIFileLineNo(flptr) = - theIFileLineNo(flptr);
  }

  /* if the last character was a CR, and we it was not consumed by
   * a binary read then read ahead to see if there is a LF
   */
  if ( isISkipLF( flptr )) {
    if (( temp = Getc( flptr )) != EOF ) {
      if ( temp != LF ) {
        UnGetc( temp , flptr ) ;
      }
    }
    ClearICRFlags( flptr ) ;
  }

  if (( temp = Getc( flptr )) == EOF ) {

    if ( ! setReadFileProgress( flptr ))
      return FALSE ;

    if ( isIIOError( flptr )) {
      return (*theIFileLastError( flptr ))( flptr ) ;
    } else {
      if ( (*theIMyCloseFile( flptr ))( flptr, CLOSE_IMPLICIT ) == EOF )
        return error_handler( IOERROR ) ;
      Copy( theo , &fnewobj ) ;
      return TRUE ;
    }
  }

  /* check for CR-LF */
  if ( temp == CR ) {
    SetIGotCRFlag( flptr ) ;
    theIFileLineNo( flptr )++;
  } else if ( temp == LF ) {
    theIFileLineNo( flptr )++;
  }

  if ( ! setReadFileProgress( flptr ))
    return FALSE ;

  oInteger(inewobj) = temp ;
  Copy( theo , (& inewobj)) ;
  return push( & tnewobj , & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            ungetc_()          author:              John Sturdy
   creation date:       23-Sep-1998        last modification:   ##-###-####
   arguments:           none .
   description:

---------------------------------------------------------------------------- */
Bool ungetc_(ps_context_t *pscontext)
{
  register int32 temp ;
  register FILELIST *flptr ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  temp = theStackSize( operandstack ) ;
  if ( EmptyStack( temp ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , temp ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK) ;

  if ( !oCanRead(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  flptr = oFile(*theo) ;

  UnGetc( 0 , flptr );

  pop(&operandstack);

  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            write_()           author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 239.

---------------------------------------------------------------------------- */
Bool write_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register FILELIST *flptr ;

  uint8 val ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  val = ( uint8 )oInteger(*theo) ;

  theo = stackindex( 1 , & operandstack ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK ) ;
  if ( !oCanWrite(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  flptr = oFile(*theo) ;
  /* it valid to look at isIOutputFile before checking if it
     is a dead filter, because the direction of any reused filter
     is guaranteed to be the same */
  if ( !isIOutputFile( flptr ))
    return error_handler( IOERROR ) ;
  if ( ! isIOpenFileFilter( theo , flptr ))
    return error_handler( IOERROR ) ;

  if ( Putc( val , flptr ) == EOF )
    return (*theIFileLastError( flptr ))( flptr ) ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            status_()          author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 226.

---------------------------------------------------------------------------- */
Bool status_(ps_context_t *pscontext)
{
  register int32 len ;
  register int32 ssize ;
  register OBJECT *o1 , *o2 ;
  STAT statusbuffer;
  Bool failed = TRUE ;
  uint8 *device_name , *file_name ;
  DEVICELIST *dev = NULL;
  device_iterator_t iter ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = TopStack( operandstack , ssize ) ;
  switch ( oType(*o1) ) {
  case OFILE :
    o2 = isIOpenFileFilter(o1 , oFile(*o1)) ? &tnewobj : &fnewobj ;
    Copy( o1 , o2 ) ;
    break ;
  case OSTRING :
    if ( ! oCanRead(*o1) && !object_access_override(o1) )
      return error_handler( INVALIDACCESS ) ;

    len = theLen(*o1) ;
    if ( len >= LONGESTFILENAME )
      return error_handler( LIMITCHECK ) ;

    switch ( parse_filename(oString(*o1), len, &device_name, &file_name) ) {
    case DEVICEANDFILE :
      if ( NULL == ( dev = find_device( device_name )))
        break ;
      if (( ! isDeviceRelative( dev )) && ( ! isDeviceEnabled( dev )))
        break ;
      if ((*theIStatusFile( dev ))( dev , file_name , &statusbuffer ) == 0 )
        failed = FALSE ;
      break ;

    case JUSTFILE :
      {
        DEVICESPARAMS *devparams = ps_core_context(pscontext)->devicesparams;
        for ( dev = device_first(&iter, DEVICEENABLED|DEVICERELATIVE) ;
              dev ;
              dev = device_next(&iter) ) {
          if ( AllowSearch(devparams, dev) ) {
            if ((*theIStatusFile( dev ))( dev , file_name ,
                                          &statusbuffer ) == 0 ) {
              failed = FALSE ;
              break ;
            }
          }
        }
        break ;
      }

    case NODEVICEORFILE :
    case JUSTDEVICE : /* device used to return error_handler( INVALIDFILEACCESS ) [#10022] */
      failed = TRUE ;
      break ;

    default:
      return error_handler( LIMITCHECK ) ;
    }

    if ( failed ) {
      /* can't get at file - doesn't exist or protection etc */
      o2 = & fnewobj ;
      Copy( o1 , o2 );
    } else {
      DEVSTAT statdev;
      double  val;

      HQASSERT(dev != NULL, "did not find device");

      /* get the block size for this device */
      if ((*theIStatusDevice(dev))(dev, &statdev) == EOF) {
        o2 = & fnewobj ;
        Copy( o1 , o2 );
      } else {

        oInteger(inewobj) = HqU32x2BoundToInt32(&statusbuffer.bytes);
        if ( ! push( & inewobj , & operandstack ))
          return FALSE;

        oInteger(inewobj) = (int32)statusbuffer.referenced;
        if ( ! push( & inewobj , & operandstack )) {
          pop(&operandstack);
          return FALSE;
        }

        oInteger(inewobj) = (int32)statusbuffer.created;
        if ( ! push( & inewobj , & operandstack )) {
          npop ( 2, & operandstack );
          return FALSE;
        }

        if ( ! push( & tnewobj , & operandstack )) {
          npop ( 3, & operandstack );
          return FALSE;
        }

        val = HqU32x2ToDouble(&statusbuffer.bytes);
        val = (val + (double)(statdev.block_size - 1)) / (double)statdev.block_size;
        oInteger(inewobj) = (int32)val;
        o2 = & inewobj ;
        Copy ( o1, o2 ) ;
      }
    }
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            resetfile_()       author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 202.

---------------------------------------------------------------------------- */
Bool resetfile_(ps_context_t *pscontext)
{
  register int32 ssize ;
  OBJECT *theo;
  register FILELIST *flptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*theo) ;

  if ( isIOpenFileFilter( theo , flptr ))
    if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            bytesavailable_()  author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 125.

---------------------------------------------------------------------------- */
Bool bytesavailable_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  register FILELIST *flptr ;
  int32  bytes = -1;
  Hq32x2 avail;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK ) ;

  /** \todo @@@ TODO FIXME ajcd 2005-09-20: This looks wrong. A
      non-executable but readable file should surely not throw an error? */
  if ( !oCanRead(*theo) || !oCanExec(*theo) )
    if ( !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

  flptr = oFile(*theo) ;
  /* it is valid to look at isIInputFile before checking if it
     is a dead filter, because the direction of any reused filter
     is guaranteed to be the same */
  if ( ! isIInputFile( flptr ))
    /* its an output file */
    return error_handler( INVALIDACCESS ) ;

  if ( isIOpenFileFilter(theo, flptr) &&
       ((*theIMyBytesAvailable(flptr))(flptr, &avail) == 0) ) {
    /* If cannot fit count in bytes, bytes stays -1 */
    (void)Hq32x2ToInt32(&avail, &bytes);
  }
  oInteger(inewobj) = bytes;

  Copy( theo , (&inewobj)) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            currfile_()        author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 135.

---------------------------------------------------------------------------- */
static OBJECT currfileCacheObject ;
OBJECT *currfileCache = NULL ;

Bool currfile_(ps_context_t *pscontext)
{
  OBJECT fileo = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( currfileCache || currfile_cache() ) {
#if defined( ASSERT_BUILD )
    /* Verify currfileCache is correct. */
    int32 loop ;
    int32 esize ;
    OBJECT *theo ;

    esize = theStackSize( executionstack ) ;
    for ( loop = 0 ; loop < esize ; ++loop ) {
      theo = stackindex( loop , & executionstack ) ;
      if ( oType(*theo) == OFILE ) {
        HQASSERT(oFile(*theo) == oFile(*currfileCache),
                 "currfileCache does not contain the file at the "
                 "top of the execution stack.") ;
        break ;
      }
    }
#endif
    return  push( currfileCache , & operandstack ) ;
  }

/* No file object on exec stack, so set up invalid file object. */
  theTags(fileo) = OFILE | LITERAL | READ_ONLY ;
  oFile(fileo) = std_files ;

  return push(&fileo, &operandstack) ;
}

/** Cache the current file (top file on execution stack) */
Bool currfile_cache(void)
{
  register int32 loop, esize ;
  register OBJECT *theo ;

  esize = theStackSize( executionstack ) ;
  for ( loop = 0 ; loop < esize ; ++loop ) {
    theo = stackindex( loop , & executionstack ) ;
    if ( oType(* theo) == OFILE ) {
      currfileCache = ( & currfileCacheObject ) ;
      Copy( currfileCache , theo ) ;
      theTags(*currfileCache) &= (~EXECUTABLE) ;
      /* Must clear the run'd flag so that we don't close the file on a
         stop/exit. */
      ClearRunFile( currfileCache ) ;
      return TRUE ;
    }
  }
  return FALSE ;
}

/* ----------------------------------------------------------------------------
   function:            echo_()            author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 149.

---------------------------------------------------------------------------- */

Bool echo_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OBOOLEAN )
    return error_handler( TYPECHECK ) ;

  stream_echo_flag = oBool(*theo) ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            flushfile_()       author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 158.

---------------------------------------------------------------------------- */
Bool flushfile_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register FILELIST *flptr ;

  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*theo) ;
  if ( ! isIOpenFileFilter( theo , flptr )) {
    goto no_flush_return;
  }
  if ( ! (oCanWrite(*theo) || oCanExec(*theo)) )
    if ( !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

  if ((*theIMyFlushFile( flptr ))( flptr ) == EOF )
    return (*theIFileLastError( flptr ))( flptr ) ;

no_flush_return:
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            flush_()           author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 158.

---------------------------------------------------------------------------- */
Bool flush_(ps_context_t *pscontext)
{
  FILELIST *flptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  flptr = theIStdout( workingsave ) ;
  if ( isIOpenFileFilterById( theISaveStdoutFilterId( workingsave ) , flptr ))
    if ((*theIMyFlushFile( flptr ))( flptr ) == EOF )
       return (*theIFileLastError( flptr ))( flptr ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            print_()           author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 193.

---------------------------------------------------------------------------- */
Bool print_(ps_context_t *pscontext)
{
  register uint8    *clist , *limit ;
  register int32    ssize ;
  register OBJECT   *theo ;
  register FILELIST *flptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OSTRING && oType(*theo) != OLONGSTRING )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  if ( oType(*theo) == OLONGSTRING) {
    clist = theLSCList(*oLongStr(*theo)) ;
    limit = clist + theLSLen(*oLongStr(*theo)) ;
  }
  else { /* OSTRING */
    clist = oString(*theo) ;
    limit = clist + (int32)theLen(*theo) ;
  }
  flptr = theIStdout( workingsave ) ;
  if ( ! isIOpenFileFilterById( theISaveStdoutFilterId( workingsave ) , flptr ))
    return error_handler( IOERROR ) ;

  for ( /* limit alread set */ ; clist < limit ; clist++ )
    if ( Putc( * (( int8 * ) clist ) , flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            writestring_()     author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 240.

---------------------------------------------------------------------------- */
Bool writestring_(ps_context_t *pscontext)
{
  register uint8    *clist , *limit ;
  register FILELIST *flptr ;
  uint8             *tempclist ;
  int32             templen ;
  OBJECT            *tempo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( NULL == ( tempo = get1filelongstring( & tempclist , & templen ,
                                             CANWRITE , CANREAD )))
    return FALSE ;

  flptr = oFile(*tempo) ;
  if ( ! isIOpenFileFilter( tempo , flptr ))
    return error_handler( IOERROR ) ;

  clist = tempclist ;
  for ( limit = clist + templen ; clist < limit ; clist++ )
    if ( Putc( *( int8 * )clist , flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            writehexstring_()  author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 240.

---------------------------------------------------------------------------- */
Bool writehexstring_(ps_context_t *pscontext)
{
  register int32    c ;
  register uint8    *lhex_table ;
  register uint8    *clist , *limit ;
  register FILELIST *flptr ;
  uint8             *tempclist ;
  int32             templen ;
  OBJECT            *tempo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( NULL == ( tempo = get1filelongstring( & tempclist , & templen ,
                                             CANWRITE , CANREAD )))
    return FALSE ;

  flptr = oFile(*tempo) ;
  if ( ! isIOpenFileFilter( tempo , flptr ))
    return error_handler( IOERROR ) ;

  lhex_table = nibble_to_hex_char ;
  clist = tempclist ;

  for ( limit = clist + templen ; clist < limit ; ++clist ) {
    c = lhex_table[ (int32)(*clist) >> 4 ] ;
    if ( Putc( c , flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;
    c = lhex_table[ (int32)*clist & 15 ] ;
    if ( Putc( c , flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;
  }
  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            readstring_()      author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 201.

---------------------------------------------------------------------------- */
Bool readstring_(ps_context_t *pscontext)
{
  register int32 len , bytes , temp, eof ;
  register uint8 *clist ;
  uint16         templen ;
  uint8          *tempclist ;
  OBJECT         *theo, *topo ;
  FILELIST       *flptr ;
  int8           glmode ;

  if ( NULL == ( theo = get1filestring( & tempclist , & templen , &glmode,
                                        CANREAD , CANWRITE )))
    return FALSE ;

  flptr = oFile(*theo) ;
  topo = theTop(operandstack) ;

  if ( isIEof( flptr ) || ! isIOpenFileFilter( theo , flptr )) {
    theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
    SETGLOBJECTTO(*theo, glmode) ;
    theLen(*theo)  = 0 ;
    oString(*theo) = NULL ;

    Copy(topo, &fnewobj) ;

    return TRUE ;
  }

  if (theIFileLineNo(flptr) < 0) {
    theIFileLineNo(flptr) = - theIFileLineNo(flptr);
  }

  clist = tempclist ; len = templen ;

  /* if the last character was a CR which was not read as binary data,
   * read ahead to see if there is a LF
   */
  if ( isISkipLF( flptr )) {
    if (( temp = Getc( flptr )) != EOF ) {
      if ( temp != LF ) {
        UnGetc( temp , flptr ) ;
      }
    }
  }
  ClearICRFlags( flptr ) ;

  /* If we're doing a character (especially a type 4 char), tell skin not to
     bother with lookahead. This test used to also check if the font type in
     the current gstate was 4, but I have removed this because it is generally
     applicable to all BuildChar fonts, and the current gstate in a buildchar
     does not necessarily reflect the type of the font being built (a setfont
     may have been done). */
  if ( char_doing_buildchar() ) {
    DEVICELIST *  dev = theIDeviceList(flptr);

    if (dev && theIIoctl(dev)) {
      (void) ( *theIIoctl(dev) )(dev, theIDescriptor(flptr),
                                 DeviceIOCtl_ShortRead, len);
    }
  }

  HQASSERT( theICount( flptr ) >= 0 , "theICount( flptr ) < 0 in readstring" ) ;
  eof = FALSE ;

  while ( len ) {
    bytes = len ;
    if ( theICount( flptr ) < len )
      bytes = theICount( flptr ) ;
    if ( bytes > 0 ) {
      HqMemCpy( clist , theIPtr( flptr ) , bytes ) ;
      theICount( flptr ) -= bytes ;
      theIPtr( flptr ) += bytes ;
      len -= bytes ;
      clist += bytes ;
    }
    if ( len ) {
      if (( temp = Getc( flptr )) == EOF ) {
        eof = TRUE ;
        break ;
      }
      *(clist++) = (uint8) temp ;
      len-- ;
    }
  }

  /* now run through the string checking for CR and/or LF */
  len = templen - len ;
  clist = tempclist ;

  templen = CAST_TO_UINT16(len) ;

  /* Speed optimisations for loop below: */
  /* 1) use PRE-decrement of loop counter to save value copy and test! */
  /* 2) use post-increment fetch */
  /* 3) explicitly cache value in ch to save re-fetching from memory */
  /* Normal case of this loop is now 5 insns for GCC 68K compared */
  /* with 9 for previous version -> nearly 2 * faster */

  if ( ps_core_context(pscontext)->systemparams->CountLines ) {
    while ( --len >= 0 ) {
      register int32 ch;

      if ( (ch = *clist++) <= CR && ch >= LF) {
        if ( ch == CR ) {
          if ( len > 0 ) {
            if ( *clist != LF )
              theIFileLineNo( flptr )++ ;
          } else {
            SetIGotCRFlag( flptr ) ;
            theIFileLineNo( flptr )++ ;
          }
        } else if ( ch == LF ) {
          theIFileLineNo( flptr )++ ;
        }
      }
    }
  }

  if ( ! setReadFileProgress( flptr ))
    return FALSE ;

  if ( eof ) {
    if ( isIIOError( flptr ))
      return (*theIFileLastError( flptr ))( flptr ) ;
    if ( (*theIMyCloseFile( flptr ))( flptr, CLOSE_IMPLICIT ) == EOF )
      return error_handler( IOERROR ) ;
    Copy(topo, &fnewobj) ;
  } else {
    Copy(topo, &tnewobj) ;
  }

  theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
  SETGLOBJECTTO(*theo, glmode) ;
  theLen(*theo)  = templen ;
  oString(*theo) = templen > 0 ? tempclist : NULL ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            peekreadstring_(..) author:            Richard Kistruck
   creation date:       12-Jan-1999        last modification:   ##-###-####
   arguments:           none .
   description:

   Mostly similar to readstring_(), without advancing file position or progress.

   Reads only what happens to be in the buffer currently (unless the buffer
   is _completely empty_, in which case it fills it).  Treats buffer-end
   like EOF, but does not close file.  Treats all errors like EOF.

---------------------------------------------------------------------------- */
Bool peekreadstring_(ps_context_t *pscontext)
{
  uint8    *pbStr ;  /* string */
  uint16    cbStr ;  /* count of bytes in string */
  int32     cbBuf ;  /* count of bytes available in buffer */
  int32     cbCopy ;  /* count of bytes to copy */
  int32     fFileNA , skiplf ;
  OBJECT   *theo , *topo ;
  FILELIST *flptr ;
  int8      glmode ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  theo = get1filestring( & pbStr , & cbStr , &glmode, CANREAD , CANWRITE );
  if ( theo == NULL )
    return FALSE ;

  flptr = oFile(*theo) ;
  fFileNA = isIIOError( flptr ) || ! isIOpenFileFilter( theo , flptr ) ;

  /* By default, push empty substring and false */
  theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
  SETGLOBJECTTO(*theo, glmode) ;
  theLen(*theo)  = 0 ;
  oString(*theo) = NULL ;
  topo = theTop( operandstack ) ;
  Copy( topo , &fnewobj ) ;

  /* Report any failure, or empty string, as though it were EOF */
  if ( fFileNA || cbStr == 0 )
    return TRUE ;
  /* Re-fill buff if empty */
  if ( ! EnsureNotEmptyFileBuff( flptr ) )
    return TRUE ;

  /* If the preceding read was non-binary (eg. readline) and the last char
   * read was CR (ie. isISkipLF is true), and the next char is LF, skip it.
   */
  HQASSERT( theICount( flptr ) >= 1 , "peekreadstring_: unexpected empty buffer" ) ;
  skiplf = ( isISkipLF( flptr ) && *( theIPtr( flptr ) ) == LF ) ;

  /* How much to copy? */
  cbBuf = theICount( flptr ) - skiplf ;
  if ( cbBuf < cbStr ) {
    /* too few bytes available; copy all we can */
    cbCopy = cbBuf ;
  } else {
    /* enough bytes available; copy only what's needed, and push true */
    cbCopy = cbStr ;
    Copy( topo , &tnewobj ) ;
  }

  /* Copy bytes and set string length */
  if ( cbCopy > 0 ) {
    HqMemCpy( pbStr , theIPtr( flptr ) + skiplf ,  cbCopy ) ;
    theLen(*theo) = CAST_TO_UINT16(cbCopy) ;
    oString(*theo) = pbStr ;
  }

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            readhexstring_()   author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 199.

---------------------------------------------------------------------------- */
Bool readhexstring_(ps_context_t *pscontext)
{
  register int32    len ;
  register int32    loop , temp1 , temp2 , res ;
  register uint8    *clist ;
  register int8     *lhex_table ;
  register FILELIST *flptr ;
  register OBJECT   *theo , *topo ;
  uint8             *tclist ;
  uint16            tlen, filter_id ;
  int8              glmode ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( (theo = get1filestring(&tclist, &tlen, &glmode, CANREAD, CANWRITE)) == NULL )
    return FALSE ;

  topo = theTop(operandstack) ;
  clist = tclist ;
  len = tlen ;
  flptr = oFile(*theo) ;

  if ( ! isIOpenFileFilter(theo, flptr) ) {
    theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
    SETGLOBJECTTO(*theo, glmode) ;
    theLen(*theo)  = 0 ;
    oString(*theo) = NULL ;

    Copy(topo, &fnewobj) ;

    return TRUE ;
  }
  filter_id = theLen(*theo);

  /* if the last character was a CR, read ahead to see if there is a LF */
  if ( isICRFlags( flptr )) {
    if (( temp1 = Getc( flptr )) != EOF ) {
      if ( temp1 != LF ) {
        UnGetc( temp1 , flptr ) ;
      }
    }
    ClearICRFlags( flptr ) ;
  }
  if ( theIFileLineNo( flptr ) < 0 )
    theIFileLineNo( flptr ) = - theIFileLineNo( flptr ) ;

  lhex_table = char_to_hex_nibble ;
  for ( loop = 0 ; loop < len ; ++loop ) {
    temp1 = Getc( flptr ) ;
    temp2 = Getc( flptr ) ;
    res = ( lhex_table[ temp1 ] << 4 ) | lhex_table[ temp2 ] ;
    /* Negative res implies problems with reading character. */
    if ( res < 0 ) {
      while ( lhex_table[ temp1 ] < 0 ) {
        if ( temp1 < 0 ) {

          if ( isIOpenFileFilterById( filter_id, flptr ) && ! setReadFileProgress( flptr ))
            return FALSE ;

          if ( isIIOError( flptr ))
            return (*theIFileLastError( flptr ))( flptr ) ;
          if ( (isIOpenFileFilterById( filter_id, flptr )) &&
               (*theIMyCloseFile( flptr ))( flptr, CLOSE_IMPLICIT ) == EOF )
            return error_handler( IOERROR ) ;

          theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
          SETGLOBJECTTO(*theo, glmode) ;
          theLen(*theo)  = CAST_TO_UINT16(loop) ;
          oString(*theo) = loop > 0 ? tclist : NULL ;

          Copy(topo, &fnewobj) ;

          return TRUE ;
        }
        if ( IsEndOfLine( temp1 )) {
          theIFileLineNo(flptr)++;
          if ( temp1 == CR ) {
            SetICRFlags( flptr ) ;
          } else if ( isICRFlags( flptr )) {
            ClearICRFlags( flptr ) ;
            theIFileLineNo(flptr)--;
          }
        } else {
          ClearICRFlags( flptr ) ;
        }
        temp1 = temp2 ;
        temp2 = Getc( flptr ) ;
      }
      while ( lhex_table[ temp2 ] < 0 ) {
        if ( temp2 < 0 ) {

          if ( isIOpenFileFilterById( filter_id, flptr ) && ! setReadFileProgress( flptr ))
            return FALSE ;

          if ( isIIOError( flptr ))
            return (*theIFileLastError( flptr ))( flptr ) ;
          if ( (isIOpenFileFilterById( filter_id, flptr )) &&
               (*theIMyCloseFile( flptr ))( flptr, CLOSE_IMPLICIT ) == EOF )
            return error_handler( IOERROR ) ;
          temp2 = '0';
          break ;
        }
        if ( IsEndOfLine( temp2 )) {
          theIFileLineNo(flptr)++;
          if ( temp2 == CR ) {
            SetICRFlags( flptr ) ;
          } else if ( isICRFlags( flptr )) {
            ClearICRFlags( flptr ) ;
            theIFileLineNo(flptr)--;
          }
        } else {
          ClearICRFlags( flptr ) ;
        }
        temp2 = Getc( flptr ) ;
      }
      res = ( lhex_table[ temp1 ] << 4 ) | lhex_table[ temp2 ] ;
    }
    (*clist++) = (uint8)res ;
  }

  if ( isIOpenFileFilterById( filter_id, flptr ) && ! setReadFileProgress( flptr ))
    return FALSE ;

  theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
  SETGLOBJECTTO(*theo, glmode) ;
  theLen(*theo)  = CAST_TO_UINT16(len) ;
  oString(*theo) = len > 0 ? tclist : NULL ;

  Copy(topo, &tnewobj) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            readline_()        author:              Andrew Cave
   creation date:       20-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 200.

---------------------------------------------------------------------------- */
Bool readline_(ps_context_t *pscontext)
{
  register int32    len , loop , temp ;
  register uint8    *clist ;
  register OBJECT   *theo , *topo ;
  register FILELIST *flptr ;
  uint8             *tempclist ;
  uint16             templen ;
  int8               glmode ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( NULL == (theo = get1filestring( & tempclist , & templen , &glmode,
                                       CANREAD , CANWRITE )))
    return FALSE ;

  topo = theTop(operandstack) ;
  clist = tempclist ; len = templen ;
  flptr = oFile(*theo) ;

  if ( ! isIOpenFileFilter(theo , flptr )) {
    theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
    SETGLOBJECTTO(*theo, glmode) ;
    theLen(*theo)  = 0 ;
    oString(*theo) = NULL ;

    Copy(topo, &fnewobj) ;

    return TRUE ;
  }

  if ( theIFileLineNo( flptr ) < 0 )
    theIFileLineNo( flptr ) = - theIFileLineNo( flptr ) ;

  /* if the last character was a CR, read ahead to see if there is a LF */
  if ( isICRFlags( flptr )) {
    if (( temp = Getc( flptr )) != EOF ) {
      if ( temp != LF ) {
        UnGetc( temp , flptr ) ;
      }
    }
    ClearICRFlags( flptr ) ;
  }

  for ( loop = 0 ; loop <= len ; ++loop ) {
    if (( temp = Getc( flptr )) == EOF ) {

      if ( ! setReadFileProgress( flptr ))
        return FALSE ;

      if ( isIIOError( flptr ))
        return (*theIFileLastError( flptr ))( flptr ) ;
      if ( (*theIMyCloseFile( flptr ))( flptr, CLOSE_IMPLICIT ) == EOF )
        return error_handler( IOERROR ) ;

      theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
      SETGLOBJECTTO(*theo, glmode) ;
      theLen(*theo)  = CAST_TO_UINT16(loop) ;
      oString(*theo) = loop > 0 ? tempclist : NULL ;

      Copy(topo, &fnewobj) ;

      return TRUE ;
    }

    if ( IsEndOfLine( temp )) {
      if ( temp == CR ) {
        SetICRFlags( flptr ) ;
      }
      theIFileLineNo(flptr)++;

      if ( ! setReadFileProgress( flptr ))
        return FALSE ;

      theTags(*theo) = OSTRING | LITERAL | UNLIMITED ;
      SETGLOBJECTTO(*theo, glmode) ;
      theLen(*theo)  = CAST_TO_UINT16(loop) ;
      oString(*theo) = loop > 0 ? tempclist : NULL ;

      Copy(topo, &tnewobj) ;

      return TRUE ;
    }

    if ( loop == len ) {
      UnGetc( temp , flptr ) ;
    }
    else
      (*clist++) = (uint8)temp ;
  }

  if ( ! setReadFileProgress( flptr ))
    return FALSE ;

  /* String completely filled up before end of line implies error. */
  return error_handler( RANGECHECK ) ;
}

/* ----------------------------------------------------------------------------
   function:            fileposition_()    author:              Andrew Cave
   creation date:       31-Aug-1990        last modification:   ##-###-####
   arguments:           none .
   description:

   See LaserWriter Reference page 48.

---------------------------------------------------------------------------- */
Bool fileposition_(ps_context_t *pscontext)
{
  OBJECT   fpos = OBJECT_NOTVM_INTEGER(0) ;
  FILELIST *flptr ;
  OBJECT   *theo ;
  int32    temp ;
  Bool     ok = TRUE ;
  Hq32x2   file_pos ;

  temp = theStackSize( operandstack ) ;
  if ( EmptyStack( temp ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , temp ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*theo) ;
  if ( ! isIOpenFileFilter( theo , flptr ))
    return error_handler( IOERROR ) ;

  if ( (flptr == theIStdin(workingsave) &&
        ps_core_context(pscontext)->userparams->AdobeFilePosition) ||
       flptr == theIStdout(workingsave) ||
       flptr == theIStderr(workingsave) )
    return error_handler( IOERROR ) ;

  if ( (*theIMyFilePos(flptr))(flptr, &file_pos) == EOF ) {
    return (*theIFileLastError(flptr))(flptr) ;
  }

  /* if the last character was a CR, read ahead to see if there is a LF */
  if ( isICRFlags( flptr )) {
    if (( temp = Getc( flptr )) != EOF ) {
      if ( temp == LF ) {
        /* fpos++ */
        Hq32x2AddUint32(&file_pos, &file_pos, 1u) ;
      }
      UnGetc( temp , flptr ) ;
    }
  }

  /* returns !ok if >2GB */
  ok = Hq32x2ToInt32(&file_pos, &oInteger(fpos));

  if (!ok) {
    /* > 2GB - encode as an XPF (never negative) */
    static OBJECT a_real = OBJECT_NOTVM_REAL(OBJECT_0_0F) ;
    SYSTEMVALUE r = Hq32x2ToDouble(&file_pos) ;

    fpos = a_real ;
    object_store_XPF(&fpos, r) ;

    /* Check for >1TB - object_store_XPF truncates too-big values */
    ok = (theLen(fpos) != 0 || r == (SYSTEMVALUE)oReal(fpos)) ;
  }

  if ( !ok ) {
    /* too long but no underlying file error - tidy up and throw IOERROR */
    (void)(*theIFileLastError(flptr))(flptr);
    return error_handler(IOERROR) ;
  }

  pop( & operandstack ) ;
  return push(&fpos, &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            setfileposition_() author:              Andrew Cave
   creation date:       31-Aug-1990        last modification:   ##-###-####
   arguments:           none .
   description:

   See LaserWriter Reference page 48.

---------------------------------------------------------------------------- */
Bool setfileposition_(ps_context_t *pscontext)
{
  FILELIST *flptr ;
  OBJECT   *o1 , *o2 ;
  Hq32x2   file_pos;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o1) != OFILE )
    return error_handler( TYPECHECK ) ;

  switch (oType(*o2)) {
    SYSTEMVALUE r ;
    int type ;

    case OINTEGER:  /* The Red Book case */
      if (oInteger(*o2) < 0)
        return error_handler( RANGECHECK ) ;
      Hq32x2FromInt32(&file_pos, oInteger(*o2)) ;
      break ;

    case OREAL: /* Our XPF extension */
      type = object_get_XPF(o2, &r) ;
      if (type == XPF_EXTENDED ||
          (type == XPF_STANDARD && r > XPF_MINIMUM / 2)) {
        /* We allow "large" standard reals because they may have been XPFs
           before being manipulated. We still TYPECHECK small ones which are
           likely to be GENOA tests. */
        Hq32x2FromDouble(&file_pos, r) ;
        break ;
      }
      /* otherwise drop through into standard Red Book error */

    default:
      return error_handler( TYPECHECK ) ;
  }

  flptr = oFile(*o1) ;
  if ( ! isIOpenFileFilter( o1 , flptr ))
    return error_handler( IOERROR ) ;

  if ( ( flptr == theIStdin(  workingsave ) &&
        ps_core_context(pscontext)->userparams->AdobeFilePosition ) ||
       flptr == theIStdout( workingsave ) ||
       flptr == theIStderr( workingsave ) )
    return error_handler( IOERROR ) ;

  if ( isIOutputFile( flptr )) {
    if ((*theIMyFlushFile( flptr ))( flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;
  } else {
    if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;
  }

  if ((*theIMySetFilePos( flptr ))( flptr , &file_pos ) == EOF )
    return (*theIFileLastError( flptr ))( flptr ) ;

  if ( isIInputFile(flptr) && !setReadFileProgress(flptr) )
    return FALSE ;
  ClearICRFlags( flptr ) ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            deletefile_()      author:              Andrew Cave
   creation date:       03-Jan-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   See LaserWriter Reference page 48.

---------------------------------------------------------------------------- */
Bool deletefile_(ps_context_t *pscontext)
{
  register int32  len ;
  register OBJECT *theo ;
  uint8           *device_name , *file_name ;
  DEVICELIST      *dev ;
  device_iterator_t iter ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  len = theLen(*theo) ;
  if ( len >= LONGESTFILENAME )
    return error_handler( UNDEFINEDFILENAME ) ;

  switch ( parse_filename(oString(*theo), len, &device_name, &file_name) ) {
  case JUSTDEVICE :
    return error_handler( UNDEFINEDFILENAME ) ;
  case JUSTFILE :
    {
      DEVICESPARAMS *devparams = ps_core_context(pscontext)->devicesparams;
      /* try all devices until one works */
      for ( dev = device_first(&iter, DEVICEENABLED|DEVICERELATIVE) ;
            dev ;
            dev = device_next(&iter) ) {
        if ( AllowSearch(devparams, dev) ) {
          if ( (*theIDeleteFile( dev ))( dev, file_name ) != EOF )
            break ;
        }
      }
      if ( ! dev )
        return error_handler( UNDEFINEDFILENAME ) ;
      break ;
    }
  case DEVICEANDFILE :
    if ( NULL == ( dev = find_device( device_name )))
      return error_handler( UNDEFINEDFILENAME ) ;
    if ( ! isDeviceEnabled( dev ))
      return error_handler( INVALIDFILEACCESS ) ;
    if ( (*theIDeleteFile( dev ))( dev , file_name ) == EOF )
      return device_error_handler( dev ) ;
    break ;
  case NODEVICEORFILE :
    return error_handler( UNDEFINEDFILENAME ) ;
  default:
    return error_handler( LIMITCHECK ) ;
  }

  pop( & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            renamefile_()      author:              Andrew Cave
   creation date:       03-Jan-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   See LaserWriter Reference page 48.

---------------------------------------------------------------------------- */
Bool renamefile_(ps_context_t *pscontext)
{
  register int32  len1 , len2 ;
  register OBJECT *o1 , *o2 ;
  DEVICE_FILEDESCRIPTOR d ;
  uint8           file1[ LONGESTFILENAME + 1 ] ;
  uint8           *device_name , *file_name ;
  DEVICELIST      *dev1 , *dev2 ;
  device_iterator_t iter ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  d = 0;                        /* init to keep compiler quiet */

  o1 = stackindex( 1 , & operandstack ) ;
  o2 = theTop( operandstack ) ;
  if ( oType(*o1) != OSTRING || oType(*o2) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanRead(*o2) && !object_access_override(o2)) )
    return error_handler( INVALIDACCESS ) ;

  len1 = theLen(*o1) ;
  len2 = theLen(*o2) ;
  if (( len1 >= LONGESTFILENAME ) || ( len2 >= LONGESTFILENAME ))
    return error_handler( LIMITCHECK ) ;

  /* parse the first file name, and try to find which devices it is on */
  switch ( parse_filename(oString(*o1), len1, &device_name, &file_name) ) {
  case JUSTDEVICE :
    return error_handler( INVALIDFILEACCESS ) ;
  case JUSTFILE :
    {
      DEVICESPARAMS *devparams = ps_core_context(pscontext)->devicesparams;
      /* find device which has this file */
      for ( dev1 = device_first(&iter, DEVICEENABLED|DEVICERELATIVE) ;
            dev1 ;
            dev1 = device_next(&iter) ) {
        if ( AllowSearch(devparams, dev1) ) {
          if (( d = (*theIOpenFile( dev1 ))( dev1, file_name, SW_RDONLY )) >= 0 )
            break ;
        }
      }
      if ( ! dev1 )
        return error_handler( UNDEFINEDFILENAME ) ;
      break ;
    }
  case DEVICEANDFILE :
    if ( NULL == ( dev1 = find_device( device_name )))
      return error_handler( UNDEFINEDFILENAME ) ;
    if ( ( ! isDeviceEnabled( dev1 )) || ( ! isDeviceRelative( dev1 )))
      return error_handler( UNDEFINEDFILENAME ) ;
    if (( d = (*theIOpenFile( dev1 ))( dev1 , file_name , SW_RDONLY )) < 0 )
      return device_error_handler( dev1 ) ;
    break ;
  case NODEVICEORFILE :
    return error_handler( UNDEFINEDFILENAME ) ;
  default:
    return error_handler( LIMITCHECK ) ;
  }
  /* close the file */
  if ((*theICloseFile( dev1 ))( dev1 , d ) < 0 )
    return device_error_handler( dev1 ) ;
  (void)strcpy( (char *)file1 , (char *)file_name ) ;

  /* parse the second file name */
  switch ( parse_filename(oString(*o2), len2, &device_name, &file_name) ) {
  case JUSTDEVICE :
    return error_handler( INVALIDFILEACCESS ) ;
  case JUSTFILE :

    /* try it on the same device as the first file */
    if ((*theIRenameFile( dev1 ))( dev1 , file1 , file_name ) < 0 )
      return device_error_handler( dev1 ) ;
    break ;

  case DEVICEANDFILE :
    if ( NULL == ( dev2 = find_device( device_name )))
      return error_handler( UNDEFINEDFILENAME ) ;
    if ( ( ! isDeviceEnabled( dev2 )) || ( ! isDeviceRelative( dev2 )))
      return error_handler( UNDEFINEDFILENAME ) ;
    if ( dev1 == dev2 ) {
      if ((*theIRenameFile( dev2 ))( dev2 , file1 , file_name ) < 0 )
        return device_error_handler (dev2);
    } else {
      /* have to do the copy between devices */
#define FOP_BUFSIZ (4*1024) /* ie 4K */
      uint8 buff[ FOP_BUFSIZ ] ;
      DEVICE_FILEDESCRIPTOR f1 , f2 ;
      int32 numbytes ;

      if (( f1 = (*theIOpenFile( dev1 ))( dev1 , file1 , SW_RDONLY )) < 0 )
        return device_error_handler( dev1 ) ;

      if (( f2 = (*theIOpenFile( dev2 ))( dev2 , file_name ,
                                        SW_WRONLY | SW_CREAT | SW_TRUNC )) < 0) {
        ( void )(*theICloseFile( dev1 ))( dev1 , f1 ) ;
        return device_error_handler( dev1 ) ;
      }
      while (( numbytes = (*theIReadFile( dev1 ))( dev1, f1 , buff , FOP_BUFSIZ )) > 0)
        if ((*theIWriteFile( dev2 ))( dev2 , f2 , buff , numbytes ) < 0)
          return device_error_handler (dev2);
      if (numbytes < 0)
        return device_error_handler (dev1);

      if ((*theICloseFile( dev1 ))( dev1 , f1 ) == EOF)
        return device_error_handler (dev1);
      if ((*theICloseFile( dev2 ))( dev2 , f2 ) == EOF)
        return device_error_handler (dev2);
      if ( (*theIDeleteFile( dev1 ))( dev1 , file1 ) == EOF )
        return device_error_handler( dev1 ) ;
    }
    break ;
  case NODEVICEORFILE :
    return error_handler( UNDEFINEDFILENAME ) ;
  default:
    return error_handler( RANGECHECK ) ;
  }

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

void free_flist( SLIST *flist )
{
  SLIST *next_flist;

  while (flist) {
    next_flist = flist->next;
    mm_free_with_header(mm_pool_temp,(mm_addr_t)flist->text);
    mm_free(mm_pool_temp,(mm_addr_t)flist, sizeof(SLIST) );
    flist=next_flist;
  }
}

/* ----------------------------------------------------------------------------
   function:            filenameforall_()  author:              Andrew Cave
   creation date:       03-Jan-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   Notes:
     A copy of the pattern must be cached because a recursive filenameforall
     would otherwise overrwrite the pattern in its parse_filename call.
     execute_filenameforall added for the list based version
   See LaserWriter Reference page 50.

---------------------------------------------------------------------------- */
Bool execute_filenameforall(OBJECT *scratch, SLIST *head_flist, OBJECT *proc)
{
  Bool anexit ;
  SLIST *flist;

  while (head_flist) {
    OBJECT *topo;

    flist = head_flist;
    if ( ! push( scratch , & operandstack )) {
      free_flist(flist);
      return FALSE ;
    }
    topo = theTop(operandstack);
    HqMemCpy(oString(*topo) , head_flist->text,
             strlen((char *)head_flist->text));
    theLen(*topo) = (uint16)strlen((char *)head_flist->text);
    head_flist = head_flist->next;

    if ( ! push( proc , & executionstack )) {
      free_flist(flist);
      pop( & operandstack ) ;
      return FALSE ;
    }

    anexit = TRUE ;
    if ( ! interpreter( 1 , &anexit )) {
      if ( !anexit ) {
        free_flist(flist);
        return FALSE ;
      }
      else
        error_clear();
    }

    mm_free_with_header(mm_pool_temp,(mm_addr_t)flist->text);
    mm_free(mm_pool_temp,(mm_addr_t)flist, sizeof(SLIST) );
  }
  return TRUE ;
}

Bool filenameforall_(ps_context_t *pscontext)
{
  uint8 *dev_pattern , *file_pattern ;
  int32 stat ;
  OBJECT *pat , *proc , *scratch ;
  DEVICELIST *dev ;
  SLIST *head_flist;
  Bool ok = TRUE;

  head_flist = NULL;
  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  pat = stackindex( 2 , & operandstack ) ;
  proc = stackindex( 1 , & operandstack ) ;
  scratch = theTop( operandstack ) ;

  switch ( oType(*proc) ) {
  case OARRAY :
  case OPACKEDARRAY :
  case OOPERATOR :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  if ( oType(*pat) != OSTRING || oType(*scratch) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*pat) && !object_access_override(pat)) ||
       (!oCanExec(*proc) && !object_access_override(proc)) ||
       (!oCanWrite(*scratch) && !object_access_override(scratch)) )
    return error_handler( INVALIDACCESS ) ;

  if ( ! push3( pat , proc , scratch , &temporarystack ))
    return FALSE ;
  scratch = theTop( temporarystack ) ;
  proc = stackindex( 1 , &temporarystack ) ;
  pat = stackindex( 2 , &temporarystack ) ;

  npop( 3 , & operandstack ) ;

  switch ( stat = parse_filename(oString(*pat), theLen(*pat),
                                 &dev_pattern , &file_pattern )) {
  case JUSTDEVICE :
    {
      uint8 dev_cache[ LONGESTDEVICENAME ] ;
      device_iterator_t iter ;

      /* cache the device pattern */
      (void)strcpy( (char *)dev_cache , (char *)dev_pattern ) ;

      for ( dev = device_first(&iter, DEVICEENABLED) ;
            ok && dev ;
            dev = device_next(&iter) ) {
        if ( SwPatternMatch( dev_cache , theIDevName( dev ))) {
          if ( match_on_device( dev , &head_flist)) {
            if ( !execute_filenameforall(scratch, head_flist, proc) )
              ok = FALSE;
            head_flist = NULL;
          } else {
            ok = FALSE;
          }
        }
      }
      break ;
    }

  case JUSTFILE :
    {
      DEVICESPARAMS *devparams = ps_core_context(pscontext)->devicesparams;
      uint8 file_cache[ LONGESTFILENAME ] ;
      device_iterator_t iter ;

      /* cache just the file pattern */
      (void)strcpy( (char *)file_cache , (char *)file_pattern ) ;

      /* try pattern match on all devices */
      for ( dev = device_first(&iter, DEVICEENABLED|DEVICERELATIVE) ;
            ok && dev ;
            dev = device_next(&iter) ) {
        if ( AllowFilenameforall(devparams, dev ) ) {
          /* try to match files on that device */
          if ( match_files_on_device( dev , file_cache ,
                                       scratch , stat , &head_flist)) {
            if ( !execute_filenameforall(scratch, head_flist, proc) )
              ok = FALSE;
            head_flist = NULL;
          } else {
            ok = FALSE ;
          }
        }
      }
      break ;
    }

  case DEVICEANDFILE :
    {
      uint8 dev_cache[ LONGESTDEVICENAME ] ;
      uint8 file_cache[ LONGESTFILENAME ] ;
      device_iterator_t iter ;

      /* cache both the file pattern and the device pattern */
      (void)strcpy( (char *)dev_cache , (char *)dev_pattern ) ;
      (void)strcpy( (char *)file_cache , (char *)file_pattern ) ;

      /* pattern match each device and on all files on that device */
      for ( dev = device_first(&iter, DEVICEENABLED|DEVICERELATIVE) ;
            ok && dev ;
            dev = device_next(&iter) ) {
        if ( SwPatternMatch( dev_cache , theIDevName( dev ))) {
          /* try to match files on that device */
          if ( match_files_on_device( dev , file_cache ,
                                        scratch , stat , &head_flist)) {
            if ( !execute_filenameforall(scratch, head_flist, proc) )
              ok = FALSE;
            head_flist = NULL;
          } else {
            ok = FALSE;
          }
        }
      }
      break ;
    }

  case NODEVICEORFILE :
    /* Empty string given as pattern; nothing can match. */
    break;

  default:
    ok = error_handler( LIMITCHECK ) ;
    break;
  }
  npop( 3 , &temporarystack ) ;

  return ok;
}



/* ----------------------------------------------------------------------------
   function:            system_()          author:              Andrew Cave
   creation date:       03-Jan-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   system call.

---------------------------------------------------------------------------- */
Bool system_(ps_context_t *pscontext)
{
  register int32  len ;
  register OBJECT *theo ;

#ifdef NEEDS_SYSTEM_PROTOTYPE
extern int system( char * );
#endif

#define LOCALBUFFER 256
  uint8 commd[ LOCALBUFFER + 1 ] ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( !oCanRead(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  len = theLen(*theo) ;
  if ( ! len )
    return error_handler( RANGECHECK )  ;

  if ( len > LOCALBUFFER )
    return error_handler( LIMITCHECK ) ;

  HqMemCpy(commd, oString(*theo), len) ;
  commd[ len ] = '\0' ;

  if ( system(( char * )commd ) == EOF ) {
    Copy( theo , ( & fnewobj )) ;
  }
  else {
    Copy( theo , ( & tnewobj )) ;
  }
  return TRUE ;
}

/** Utility function for read functions in this module. Basically checks for a
    string & file on the top of the operand stack and checks their attributes,
    returning these. */
OBJECT *get1filestring( uint8 **thec, uint16 *thelen, int8 *glmode,
                        uint8 c1, uint8 c2 )
{
  register FILELIST *flptr ;
  register OBJECT *o1 , *o2 ;

  if ( theStackSize( operandstack ) < 1 ) {
    ( void )error_handler( STACKUNDERFLOW ) ;
    return NULL ;
  }

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o2) != OSTRING ) {
    ( void ) error_handler( TYPECHECK ) ;
    return NULL ;
  }

  (*thec) = oString(*o2) ;
  (*thelen) = theLen(*o2) ;
  (*glmode) = (int8)oGlobalValue(*o2) ;

  if ( oType(*o1) != OFILE ) {
    ( void )error_handler( TYPECHECK ) ;
    return NULL ;
  }

  if ( (oAccess(*o1) < c1 && !object_access_override(o1)) ||
       (oAccess(*o2) < c2 && !object_access_override(o2)) ) {
    ( void )error_handler( INVALIDACCESS ) ;
    return NULL ;
  }

  flptr = oFile(*o1) ;
  /* it is valid to look at isIInputFile and isIOutputFile before checking if it
     is a dead filter, because the direction of any reused filter
     is guaranteed to be the same */
  if ( (c1 ==  CANREAD && ! isIInputFile(flptr)) ||
       (c1 == CANWRITE && ! isIOutputFile(flptr)) ) {
    ( void )error_handler( IOERROR ) ;
    return NULL ;
  }
  return  o1  ;
}


/** Same as above \c get1filestring but allows a longstring or a string. */
OBJECT *get1filelongstring( uint8 **thec, int32 *thelen, uint8 c1, uint8 c2 )
{
  register FILELIST *flptr ;
  register OBJECT *o1 , *o2 ;

  if ( theStackSize( operandstack ) < 1 ) {
    ( void )error_handler( STACKUNDERFLOW ) ;
    return NULL ;
  }

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o2) == OSTRING ) {
    (*thec) = oString(*o2) ;
    (*thelen) = theLen(*o2) ;
  }
  else if ( oType(*o2) == OLONGSTRING ) {
    (*thec) = theLSCList(*oLongStr(*o2)) ;
    (*thelen) = theLSLen(*oLongStr(*o2)) ;
  }
  else {
    ( void ) error_handler( TYPECHECK ) ;
    return NULL ;
  }

  if ( oType(*o1) != OFILE ) {
    ( void )error_handler( TYPECHECK ) ;
    return NULL ;
  }

  if ( (oAccess(*o1) < c1 && !object_access_override(o1)) ||
       (oAccess(*o2) < c2 && !object_access_override(o2)) ) {
    ( void )error_handler( INVALIDACCESS ) ;
    return NULL ;
  }

  flptr = oFile(*o1) ;
  /* it is valid to look at isIInputFile and isIOutputFile before checking if it
     is a dead filter, because the direction of any reused filter
     is guaranteed to be the same */
  if ( (c1 ==  CANREAD && !isIInputFile(flptr)) ||
       (c1 == CANWRITE && !isIOutputFile(flptr)) ) {
    ( void )error_handler( IOERROR ) ;
    return NULL ;
  }
  return  o1  ;
}

/* ----------------------------------------------------------------------------
   function:            fileseekable_()
   usage:               fileobj fileseekable_ bool
   description:

     Determines whether the given file object is seekable.
     The operator is an HQN extension and defined in internaldict.
     See also gsc_fileseekable.

---------------------------------------------------------------------------- */
Bool fileseekable_(ps_context_t *pscontext)
{
  OBJECT   *theo ;
  FILELIST *flptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ) )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*theo) ;
  HQASSERT( flptr != NULL , "flptr is null" ) ;
  if ( ! isIOpenFileFilter( theo , flptr ))
    return error_handler( IOERROR ) ;

  if ( file_seekable( flptr ) )
    Copy(theo, &tnewobj) ;
  else
    Copy(theo, &fnewobj) ;

  return TRUE ;
}

void init_C_globals_fileops(void)
{
  currfileCache = NULL ;
}

/* ---------------------------------------------------------------------- */

/* Log stripped */
