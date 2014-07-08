/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!src:pdfstrm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF stream creation.
 */

#include "core.h"
#include "pdfstrm.h"

#include "swdevice.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "objects.h"
#include "dictscan.h"
#include "mm.h"
#include "md5.h"
#include "fileio.h"
#include "stream.h"
#include "rsd.h"
#include "namedef_.h"

#include "chartype.h" /* IsWhiteSpace */

#include "swpdf.h"
#include "pdfmatch.h"
#include "pdfmem.h"
#include "pdfxref.h"

/* ---------------------------------------------------------------------- */

/** pdf_createfilterlist
 *
 * Call this to layer filters on top of an existing file or filter.
 *
 * Pass the existing file or filter in "file".
 * (This gets changed to be the topmost newly-added filter).
 *
 * Pass the name or array of names of the desired filter-types in "theo".
 *
 * Pass the filter-parameters or array of filter-parameters in "parms".
 *
 * Pass flag indicating if each filter-parameter dictionary needs
 * a deep copy to a new object before creating the filter in.
 * "copyparms".
 *
 * Pass whether the close source/target flag should be set (for
 * non-StreamDecode filters) in the "cst" flag.
 *
 * Returns:
 * The "*file" object is overwritten with the topmost newly-added filter.
 *
 * **** Note: Used for PDF Out as well as PDF input ***
 */
Bool pdf_createfilterlist( PDFCONTEXT *pdfc , OBJECT *file , OBJECT *theo ,
                           OBJECT *parms, Bool copyparms, int32 cst )
{
  HQASSERT( pdfc , "pdfc NULL in pdf_createfilterlist" ) ;
  HQASSERT( file , "file NULL in pdf_createfilterlist" ) ;
  HQASSERT( oType(*file) == OFILE ,
            "file not a file in pdf_createfilterlist" ) ;

  if ( theo != NULL ) {
    OBJECT null = OBJECT_NOTVM_NULL ;
    OBJECT dict = OBJECT_NOTVM_NOTHING ;

    if ( oType(*theo) == ONAME ) {
      if ( ! parms )
        parms = ( & null ) ;
      else {
        if ( oType(*parms) == OARRAY || oType(*parms) == OPACKEDARRAY ) {
          if ( theLen(*parms) != 1 )
            return error_handler( TYPECHECK ) ;
          parms = oArray(*parms) ;
        }
      }

      Copy( & dict, parms ) ;
      if ( copyparms ) {
        if ( ! pdf_copyobject( pdfc, parms, & dict ) )
          return FALSE ;
        if ( ! pdf_resolvexrefs( pdfc , &dict) )
          return FALSE ;
      }

      if ( !pdf_createfilter( pdfc , file , theo , & dict , cst ) )
        return FALSE ;
    }
    else {
      int32 i ;
      int32 len ;
      int32 isarray ;
      OBJECT *olistn , *olista ;

      HQASSERT(oType(*theo) == OARRAY || oType(*theo) == OPACKEDARRAY,
               "only know about names & arrays" ) ;
      if ( ! parms )
        parms = ( & null ) ;
      else {
        if ( oType(*parms) == ODICTIONARY )
          return error_handler( TYPECHECK ) ;
        if ( (oType(*parms) == OARRAY || oType(*parms) == OPACKEDARRAY)
             && theLen(*theo) != theLen(*parms) )
          return error_handler( RANGECHECK ) ;
      }
      olistn = oArray(*theo) ;
      isarray = FALSE ;
      olista = parms ;
      if ( oType(*parms) == OARRAY || oType(*parms) == OPACKEDARRAY ) {
        isarray = TRUE ;
        olista = oArray(*parms) ;
      }
      len = theLen(*theo) ;
      for ( i = 0 ; i < len ; ++i ) {
        theo = olistn ;
        if ( oType(*theo) == OINDIRECT ) {
          if ( ! pdf_lookupxref(pdfc, &theo, oXRefID(*theo),
                                theGen(*theo), FALSE) )
            return FALSE ;
          if ( theo == NULL )
            return error_handler( UNDEFINEDRESOURCE ) ;
        }
        if ( oType(*theo) != ONAME )
          return error_handler( TYPECHECK ) ;

        parms = olista ;
        if ( oType(*parms) == OINDIRECT ) {
          if ( ! pdf_lookupxref(pdfc, &parms, oXRefID(*parms),
                                theGen(*parms), FALSE) )
            return FALSE ;
          if ( parms == NULL )
            return error_handler( UNDEFINEDRESOURCE ) ;
        }
        if ( oType(*parms) != ONULL && oType(*parms) != ODICTIONARY )
          return error_handler( TYPECHECK ) ;

        Copy( & dict, parms ) ;
        if ( copyparms ) {
          if ( ! pdf_copyobject( pdfc, parms, & dict ) )
            return FALSE ;
          if ( ! pdf_resolvexrefs( pdfc , &dict) )
            return FALSE ;
        }

        if ( !pdf_createfilter( pdfc , file , theo , & dict , cst ) )
          return FALSE ;

        ++olistn ;
        if ( isarray )
          ++olista ;
      }
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

static FILELIST *pdf_createfilter_alloc(mm_pool_t pool)
{
  FILELIST *filter;

  HQASSERT(pool != NULL, "No PDF structure pool for alloc") ;

  filter = mm_alloc(pool, sizeof(FILELIST), MM_ALLOC_CLASS_PDF_FILELIST) ;
  if (filter == NULL)
    (void) error_handler(VMERROR);

  return filter;
}

/* pdf_createfilter
 * ----------------
 * Call this to create a new filter (of a certain type), on top of an
 * underlying file.
 *
 * Pass the underlying file object in "file".
 *
 * Pass the name of the desired filter-type in "name".
 *
 * Pass filter-creation arguments in the "args" dictionary.
 * This dictionary gets saved in theIParamDict of the returned flptr.
 *
 * Pass whether the close source/target flag should be set (for
 * non-StreamDecode filters) in the "cst" flag.
 *
 * Returns:
 * The return value is a boolean indicating success/failure. "file" is updated
 * with the new filter id and filelist.
 *
 * The filter created is in the pdf world, attached to the pdf context
 * with a corresponding lifetime -- it is not affected by save/restore.
 *
 * **** Note: Used for PDF Out as well as PDF input ***
 */
Bool pdf_createfilter( PDFCONTEXT *pdfc , OBJECT *file ,
                       OBJECT *name , OBJECT *args , int32 cst )
{
  int32 name_length ;
  int32 find_error ;
  uint8 *filter_name ;
  NAMECACHE *nptr ;
  FILELIST *flptr ;
  FILELIST *nflptr ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT params_dict = OBJECT_NOTVM_NOTHING ;
  SFRAME myframe ;
  STACK mystack = { EMPTY_STACK, NULL, FRAMESIZE, STACK_TYPE_OPERAND } ;

  mystack.fptr = &myframe ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT(file, "no file object") ;
  HQASSERT( name , "name NULL in pdf_createfilter" ) ;
  HQASSERT( args , "args NULL in pdf_createfilter" ) ;

  HQASSERT( oType(*name) == ONAME , "bad OBJECT type in pdf_createfilter" ) ;
  nptr = oName(*name) ;

  /* handle possible abbreviations */
  switch ( theINameNumber(nptr) ) {
  case NAME_A85:
    nptr = system_names + NAME_ASCII85Decode;
    break;
  case NAME_AHx:
    nptr = system_names + NAME_ASCIIHexDecode;
    break;
  case NAME_CCF:
    nptr = system_names + NAME_CCITTFaxDecode;
    break;
  case NAME_DCT:
    nptr = system_names + NAME_DCTDecode;
    break;
  case NAME_Fl:
    nptr = system_names + NAME_FlateDecode;
    break;
  case NAME_LZW:
    nptr = system_names + NAME_LZWDecode;
    break;
  case NAME_RL:
    nptr = system_names + NAME_RunLengthDecode;
    break;
  }

  name_length = theINLen( nptr ) ;
  filter_name = theICList( nptr ) ;

  /* find the filter name in the external or standard filter table */
  nflptr = filter_external_find( filter_name, name_length,
                                  & find_error, TRUE ) ;
  if ( nflptr == NULL ) {
    if ( find_error != NOT_AN_ERROR)
      return error_handler( find_error );

    if ( NULL == ( nflptr = filter_standard_find( filter_name , name_length )))
      return error_handler( UNDEFINED ) ;
  }

  /* Sanity check. */
  HQASSERT(isIFilter(nflptr), "Not a filter") ;

  if ( isIInputFile( nflptr )) {
    if ( theINameNumber(nptr) == NAME_JPXDecode ) {
      /* JPXDecode can automatically layer an RSD underneath itself, but that
         RSD would be allocated from PostScript memory. To make sure that the
         RSD is allocated from PDF memory, we allocate it here. We force the
         cst flag TRUE on the JPXDecode, because the caller may not be
         expecting the RSD in the chain (this will make JPXDecode close the
         RSD, the RSD will close the underlying file if necessary). */
      OBJECT rsdname = OBJECT_NOTVM_NAME(NAME_ReusableStreamDecode, LITERAL) ;
      OBJECT rsdargs = OBJECT_NOTVM_NULL ;
      if ( !pdf_createfilter(pdfc, file, &rsdname, &rsdargs, cst) )
        return FALSE ;

      cst = TRUE ;
    } else if ( theINameNumber( nptr ) == NAME_FlateDecode ) {
      /* If we're creating a FlateDecode filter, add the
       * ErrorOnFlateChecksumFailure flag to the params.
       */
      OBJECT paramName = OBJECT_NOTVM_NAME(NAME_ErrorOnChecksumFailure, LITERAL) ;

      if ( oType(*args) != ODICTIONARY ) {
        /* The args object wasn't a dictionary - must be an ONULL
         * otherwise it doesn't make sense. Make a new dict - a length
         * of 4 should be enough to make sure it never needs to grow.
         */

        HQASSERT(oType(*args) == ONULL , "Expecting null args." ) ;

        if ( ! pdf_create_dictionary( pdfc , 4 , & params_dict ))
          return FALSE ;

        args = & params_dict ;
      }

      if ( ! pdf_fast_insert_hash( pdfc , args , & paramName ,
                                   pdfxc->ErrorOnFlateChecksumFailure ?
                                   & tnewobj : & fnewobj  ))
        return FALSE ;
    }
  }

  /* If we have an underlying file, put it on the stack for the init routine,
     and set the new filter id and file pointer for the return object. */
  if ( !push(file, &mystack) )
    return FALSE ;

  /* Provide an empty list to prevent the function scanning for a closed file -
   * for jobs with a very large number of pages the linear search can become
   * significant.
   */
  flptr = NULL;
  if ( !filter_create_with_alloc(nflptr, &flptr,
                                 args, &mystack, pdfxc->id,
                                 pdf_createfilter_alloc,
                                 pdfxc->mm_structure_pool) )
    return FALSE ;

  /* Add new filter to head of chain */
  flptr->next = pdfxc->streams;
  pdfxc->streams = flptr;

  /* promote streams to save level of pdf context */
  flptr->sid = CAST_UNSIGNED_TO_UINT8(pdfxc->savelevel) ;

  /* Only PDF Input filters are (initially at least) marked as rewindable */
  if ( isIInputFile( nflptr ))
    SetIRewindableFlag( flptr ) ;

  /* If cst is true set the "close source/target" flag. */
  if ( cst ) {
    HQASSERT( theINameNumber(nptr) != NAME_StreamDecode ,
              "Setting CST on a stream would mean the real file gets closed." ) ;
    SetICSTFlag( flptr ) ;
  }

  /* Prepare the return file object. The input file object's executability is
     retained. */
  file_store_object(file, flptr, CAST_UNSIGNED_TO_UINT8(oExec(*file))) ;

  /* We should be setting this on file close, but we don't have easy access to
     that, so use this file creation as a proxy. */
  pdfxc->lowmemRedoStreams = TRUE;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
void pdf_flushstreams( PDFCONTEXT *pdfc )
{
  FILELIST *flptr ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  flptr = pdfxc->streams ;
  while ( flptr ) {
    FILELIST *tmp = flptr ;
    flptr = flptr->next ;

    ClearIRewindableFlag( tmp ) ;
    if ( isIOpenFile( tmp )) {
      ( void )(*theIMyCloseFile( tmp ))( tmp, CLOSE_EXPLICIT ) ;
    }

    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )tmp ,
             sizeof( FILELIST )) ;
  }

  pdfxc->streams = NULL ;
}

/* ----------------------------------------------------------------------
 * Frees the memory for any streams in the current execution context
 * which are closed and have been flagged as non-rewindable.
 */
Bool pdf_purgestreams( PDFCONTEXT *pdfc )
{
  FILELIST *flptr, *next, *prev = NULL ;
  Bool result = FALSE;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  flptr = pdfxc->streams ;

  while ( flptr ) {
    next = flptr->next ;

    if ( ! isIOpenFile( flptr ) &&
         ! isIRewindable( flptr ) &&
           theIFilterId( flptr ) < LASTFILTERID ) {
#if defined( ASSERT_BUILD )
      {
        /* Sanity checks */
        HQASSERT( flptr != pdfxc->flptr,
                  "Cannot purge underlying file in pdf_purgestreams" ) ;
        if ( pdfc->contentsStream )
          HQASSERT( flptr != oFile(*pdfc->contentsStream),
                    "Cannot purge contents stream in pdf_purgestreams" ) ;

        HQASSERT( isIFilter( flptr ),
                  "Attempting to close non-filter file in pdf_purgestreams") ;
      }
#endif
      mm_free( pdfxc->mm_structure_pool ,
               ( mm_addr_t ) flptr , sizeof( FILELIST ) );
      if ( prev )
        prev->next = next ;
      else
        pdfxc->streams = next ;

      result = TRUE;
    }
    else
      prev = flptr ;

    flptr = next ;
  }

  pdfc->pdfxc->lowmemStreamCount = 0;

  return result ;
}


size_t pdf_measure_purgeable_streams(PDFCONTEXT *pdfc)
{
  size_t count = 0;
  FILELIST *flptr;
  PDFXCONTEXT *pdfxc;

  PDF_CHECK_MC( pdfc );
  PDF_GET_XC( pdfxc );

  /* There is no point in measuring streams unless more streams have been closed
     since the last call. However, we don't have easy access to streams closing,
     and this measurement is potentially expensive. So, I've used a file open
     call as a proxy (in pdf_createfilter), which seems to work ok. */
  if (!pdfxc->lowmemRedoStreams)
    return pdfxc->lowmemStreamCount;
  pdfc->pdfxc->lowmemRedoStreams = FALSE;

  flptr = pdfxc->streams;

  while ( flptr != NULL ) {
    if ( ! isIOpenFile( flptr ) && ! isIRewindable( flptr )
         && theIFilterId( flptr ) < LASTFILTERID )
      count += sizeof(FILELIST);
    flptr = flptr->next;
  }
  pdfxc->lowmemStreamCount = count;
  return count;
}


/** Attempt to rewind the passed stream; 'rewound', if not NULL, will be set to
true if the the stream actually was rewound (closed streams, for example cannot
be rewound).

If the stream can be positioned, and has useful data waiting to be read, it's
position will be restored to that before the rewind at the end of the current
marking context. Note that when the same file is so marked more than once in any
single marking context, the file position will be that before the FIRST call to
to this method when we return to the enclosing marking context.

Returns false on error.
*/
Bool pdf_rewindstream(PDFCONTEXT *pdfc, OBJECT* stream, Bool* rewound)
{
  FILELIST *flptr ;

  if (rewound != NULL)
    *rewound = FALSE ;

  if ( oType( *stream ) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile( *stream ) ;
  HQASSERT(!isIOutputFile(flptr),
           "pdf_rewindstream - 'stream' should be an input stream.") ;

  if ( isIOpenFileFilter( stream , flptr )) {
    Hq32x2 filepos ;

    /* If the filter supports positioning, and has useful stuff left to read, we
    save a file position restore list entry, which will reset the position at
    the end of the current marking stream. */
    if ( (*theIMyFilePos(flptr))(flptr, &filepos) != EOF ) {
      int32 ch ;

      do {
        ch = Getc(flptr) ;
      } while ( ch >= 0 && IsWhiteSpace(ch) ) ;

      if ( ch != EOF ) {
        PDF_FILERESTORE_LIST *restore ;
        /* Add new restorelist entry of filepos, flptr. */

        if ( (restore = mm_alloc(mm_pool_temp,
                                 sizeof(PDF_FILERESTORE_LIST),
                                 MM_ALLOC_CLASS_PDF_FILERESTORE)) == NULL )
          return error_handler(VMERROR) ;

        /* Note that we are adding this to the start of the restore list; if
        this stream already has restore entries, they will be honored after this
        one, meaning that the file position after all restores will be set to
        that before the first call to this method, in this marking context. */
        restore->position = filepos ;
        restore->fileobj = *stream ;
        restore->next = pdfc->restorefiles ;
        pdfc->restorefiles = restore ;
      }
    }

    Hq32x2FromInt32(&filepos, 0);
    if ((*theIMyResetFile( flptr ))( flptr ) == EOF ||
        (*theIMySetFilePos( flptr ))( flptr , &filepos ) == EOF )
      return ( *theIFileLastError( flptr ))( flptr ) ;

    if (rewound != NULL)
      *rewound = TRUE ;
  }

  return TRUE ;
}

/** Restores streams to the positions given by the PDF_FILERESTORE_LIST entries,
  * if result passed in as TRUE.  Frees the entries.
  */
Bool pdf_restorestreams( PDFCONTEXT *pdfc, Bool result )
{
  PDF_FILERESTORE_LIST *restore ;
  FILELIST *flptr ;
  Bool res = result ;

  while ( (restore = pdfc->restorefiles) != NULL ) {

    HQASSERT(oType(restore->fileobj) == OFILE, "Saved file not a file") ;
    flptr = oFile(restore->fileobj) ;

    if ( res && isIOpenFileFilter(&restore->fileobj, flptr) ) {
      if ( (*theIMyResetFile(flptr))(flptr) == EOF ||
           (*theIMySetFilePos(flptr))(flptr, &restore->position) == EOF )
        res = (*theIFileLastError(flptr))(flptr) ;
    }

    pdfc->restorefiles = restore->next ;
    mm_free(mm_pool_temp, (mm_pool_t)restore, sizeof(PDF_FILERESTORE_LIST)) ;
  }

  return res ;
}


/* end of file pdfstrm.c */

/* Log stripped */
