/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfstrobj.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Handling of PDF stream objects.
 */

#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "dictscan.h"
#include "mm.h"
#include "md5.h"
#include "fileio.h"
#include "rsd.h"
#include "namedef_.h"
#include "dicthash.h"

#include "stacks.h"
#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "monitor.h"

#include "swpdf.h"
#include "stream.h"
#include "pdfmatch.h"
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdfxref.h"

#include "pdfexec.h"
#include "pdfin.h"
#include "pdffs.h"
#include "pdfdefs.h"
#include "pdfncryp.h"
#include "pdfscan.h"
#include "pdfstrobj.h"
#include "pdfx.h"

/* ----------------------------------------------------------------------------
 * pdf_streamobject()
 * ------------------
 * Call this to change a stream-parameters dictionary object into a stream
 * object.  (The dictionary survives, getting saved in the stream's
 * theIParamDict slot).
 *
 * The stream's data will come from some underlying file, starting at the
 * underlying file's current file-position.  First, a StreamDecode filter
 * is created on the file.  Then any other filters specified in the
 * stream-parameters dictionary are layered on top.
 *
 * Pass the (correctly positioned) underlying file in "flptr".
 *
 * Pass the stream-parameters dictionary in "pdfobj".
 * (This gets changed to be the new stream object).
 *
 * Pass object number and generation in "objnum" and "objgen".
 *
 * Returns:
 * The "*pdfobj" object is overwritten with the new stream object.
 */
Bool pdf_streamobject( PDFCONTEXT *pdfc , FILELIST *flptr , OBJECT *pdfobj ,
                       int32 objnum , int32 objgen , PDF_STREAM_INFO *info )
{
  OBJECT name = OBJECT_NOTVM_NOTHING ;
  OBJECT flag = OBJECT_NOTVM_NOTHING ;
  OBJECT argsobj = OBJECT_NOTVM_NOTHING ;
  OBJECT *filter ;
  OBJECT *decodeparams ;
  OBJECT *streamLength ;
  OBJECT psfilename = OBJECT_NOTVM_NULL;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  Hq32x2 file_pos;
  Bool is_xref_stream = FALSE ;

  enum {
    e_pdfstream_length,
    e_pdfstream_filter,
    e_pdfstream_decodeparams,
    e_pdfstream_f,
    e_pdfstream_ffilter,
    e_pdfstream_fdecodeparams,
    e_pdfstream_xref,
    e_pdfstream_hqncacheslot,
    e_pdfstream_max
  };
  static NAMETYPEMATCH stream_dict[e_pdfstream_max + 1] = {
    { NAME_Length                  , 2, { OINTEGER, OINDIRECT }},
    { NAME_Filter       | OOPTIONAL, 4, { ONAME, OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_DecodeParms  | OOPTIONAL, 5, { ONULL, ODICTIONARY, OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_F            | OOPTIONAL, 3, { OSTRING, ODICTIONARY, OINDIRECT }},
    { NAME_FFilter      | OOPTIONAL, 4, { ONAME, OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_FDecodeParms | OOPTIONAL, 5, { ONULL, ODICTIONARY, OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_XRef         | OOPTIONAL, 0, { ONOTHING }},  /* ie, completely ignore the type */
    { NAME_HqnCacheSlot | OOPTIONAL, 0, { ONOTHING }},  /* ie, completely ignore the type */
    DUMMY_END_MATCH
  } ;

  enum {
    pdf15dict_Type,
    pdf15dict_max
  } ;
  static NAMETYPEMATCH pdf15dictmatch[pdf15dict_max + 1] = {
    { NAME_Type  , 2, { ONAME, OINDIRECT }},
    DUMMY_END_MATCH
  };

  enum {
    e_xrefstreams_size,
    e_xrefstreams_index,
    e_xrefstreams_prev,
    e_xrefstreams_w,
    e_xrefstreams_max
  };
  static NAMETYPEMATCH xrefdictmatch[e_xrefstreams_max + 1] = {
    { NAME_Size             , 1, { OINTEGER }},
    { NAME_Index | OOPTIONAL, 1, { OARRAY }},
    { NAME_Prev  | OOPTIONAL, 2, { OINTEGER, OFILEOFFSET }},
    { NAME_W                , 1, { OARRAY }},
    DUMMY_END_MATCH
  } ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( flptr , "flptr NULL in pdf_streamobject" ) ;
  HQASSERT( pdfobj , "pdfobj NULL in pdf_streamobject" ) ;

  if ( oType( *pdfobj ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  /* Note that we want to capture the current stream position before we
   * call pdf_dictmatch. That's because this will/may go recursive on.
   * reading new objects. This is then passed in the dictionary arguments
   * to the StreamDecode filter.
   */
  if ( (*theIMyFilePos(flptr))(flptr, &file_pos) == EOF )
    return (*theIFileLastError(flptr))(flptr) ;

  if ( ! pdf_dictmatch( pdfc , pdfobj , stream_dict ))
    return FALSE ;

  /* Retain the Length object */
  streamLength = stream_dict[ e_pdfstream_length ].result;

  /* Set the default state for when there's no /F key
     - i.e. use the present object's stream directly. */
  filter = stream_dict[ e_pdfstream_filter ].result;       /* Set to use standard filters */
  decodeparams = stream_dict[ e_pdfstream_decodeparams ].result; /* and their params. */

  /* If the 'F' key is specified then use FFilters and FDecodeParms.
     Construct a PS filename out of this (the 'F' object) to pass to the
     StreamDecode filter.  */
  if (stream_dict[ e_pdfstream_f ].result) {
    OBJECT * ID;
    Bool isVolatile;

    if (! pdfxFKeyInStreamDictionaryDetected(pdfc))
      return FALSE;

    filter = stream_dict[ e_pdfstream_ffilter ].result;
    decodeparams = stream_dict[ e_pdfstream_fdecodeparams ].result;

    /* The /F key could provide (a) a string directly naming a file, (b) a
       dictionary which could either (b1) name a file or (b2) specify
       another pdf object stream for the embedded file. pdf_filespec()
       resolves all this and returns the file's name or embedded file stream
       via the 'psfilename' parameter. IMPORTANT:- pdf_filespec() may result in
       this function (pdf_streamobject()) being called recursively; hence this
       should be the last use of 'stream_dict' which may now get overwritten.
    */
    if ( ! pdf_filespec( pdfc,
                         stream_dict[ e_pdfstream_f ].result,   /* /F value (string or dict) */
                         &psfilename,               /* Returned file name or other stream */
                         &ID,                       /* Returned file ID (not used) */
                         &isVolatile ))             /* Returned flag (not used)  */
      return FALSE;

    if (oType(psfilename) == OSTRING) {
      OBJECT RetFName = OBJECT_NOTVM_NOTHING;
      /* pdf_filespec() has resolved the /F reference to a file name string.
         Ensure the file can be found (including a search through the OPI folders
         if necessary).
         Note that for PDF/X jobs, we only look for external files using the OPI
         system. */
      if (!pdf_locate_file( pdfc, &psfilename, &RetFName, NULL,
                            pdfxExternalFilesAllowed(pdfc) ))
        return FALSE;

      if (oType(RetFName) == OSTRING)
        Copy( &psfilename, &RetFName );   /* file found ok. */
      else  {         /* File not found is not an "error". */
        monitorf(UVM("External file '%.*s' not found, using direct stream contents.\n"),
                 theLen(psfilename), oString(psfilename));
        object_store_null(&psfilename);
      }
    }
  }
  else {
    /* Now we know we are not dealing with an external file stream, can safely
     * add the "uniqueID" /XRef, as required for ICC profile caching.  The
     * uniqueID is generated from object number (32bits) and generation
     * (16bits) by reversing the bit order of objgen and XORing it into
     * the top of objnum - the chances two seperate streams generating
     * the same uniqueID are therefore *hugely* reduced.
     */
    int32 id;
    id = (objgen & 0xFF00) | (objgen &   0xFF)<<16 ; /* swap bytes */
    id = (id &   0xF0F000) | (id &   0x0F0F00)<<8 ;  /* swap nibbles */
    id = (id &  0xCCCC000) | (id &  0x3333000)<<4 ;  /* swap nibblets */
    id = (id & 0x2AAA8000) | (id & 0x15554000)<<2 ;  /* swap bits */
    id = objnum ^ id<<1 ;

    HQASSERT(!stream_dict[e_pdfstream_xref].result,
             "Stream dictionary already has an XRef") ;
    object_store_name(&name, NAME_XRef, LITERAL) ;
    object_store_integer(&argsobj, id) ;
    if ( !pdf_fast_insert_hash( pdfc, pdfobj, &name, &argsobj ) ) {
      HQFAIL( "Unable to add XRef to stream dictionary" ) ;
    }
  }

  /* Store the xref cache slot identifier in the args dictionary so we
     can map the OBJECT structure back onto the xref cache slot which
     owns it. That means we can manage stream object cache lifetimes
     correctly. */

  {
    OBJECT ind = OBJECT_NOTVM_NOTHING ;

    theTags( ind ) = OINTEGER | LITERAL ;
    oInteger( ind ) = objnum ;

    HQASSERT(!stream_dict[e_pdfstream_hqncacheslot].result,
             "Stream dictionary already has an HqnCacheSlot") ;
    object_store_name(&name, NAME_HqnCacheSlot, LITERAL) ;
    if ( !pdf_fast_insert_hash( pdfc, pdfobj, &name, &ind ) ) {
      HQFAIL( "Unable to add HqnCacheSlot to stream dictionary" ) ;
    }
  }

  if (info != NULL) {
    /* Expecting a PDF1.5 xref stream */
    OBJECT * Wobj;

    if ( ! pdf_dictmatch( pdfc , pdfobj , pdf15dictmatch ))
      return FALSE ;

    if ( oNameNumber(*pdf15dictmatch[pdf15dict_Type].result) != NAME_XRef )
      return error_handler(TYPECHECK);

    if ( oType( ixc->pdfroot ) != OINDIRECT ) {
      if (!pdf_extract_trailer_dict(pdfc,pdfobj) )
        return FALSE;

      /* Shallow copy xref dict to trailer dict so existing PS
         procsets can access trailer dict members.*/
      ixc->pdftrailer = *pdfobj;
    } else {
      if (!pdf_extract_prevtrailer_dict(pdfc,pdfobj))
        return FALSE;
    }

    if ( ! pdf_dictmatch( pdfc , pdfobj , xrefdictmatch ))
      return FALSE ;

    info->size = oInteger( *(xrefdictmatch[ e_xrefstreams_size ].result) );
    if (info->size < 1) {
      return (error_handler(RANGECHECK));
    }

    if ( theLen(*xrefdictmatch[e_xrefstreams_w].result) < 3 )
      return error_handler(RANGECHECK) ;

    Wobj = oArray( *(xrefdictmatch[ e_xrefstreams_w ].result) );

    if ( oType(Wobj[0]) != OINTEGER ||
         oType(Wobj[1]) != OINTEGER ||
         oType(Wobj[2]) != OINTEGER )
      return error_handler(TYPECHECK) ;

    /* Only field 2 can be up to 8 bytes long to cope with offsets > 4GiB */
    if (oInteger(Wobj[0]) < 0 || oInteger(Wobj[0]) > 4 ||
        oInteger(Wobj[1]) < 1 || oInteger(Wobj[1]) > 8 ||
        oInteger(Wobj[2]) < 0 || oInteger(Wobj[2]) > 4) {
      return (error_handler(RANGECHECK));
    }

    info->Wsize[0] = oInteger(Wobj[0]);
    info->Wsize[1] = oInteger(Wobj[1]);
    info->Wsize[2] = oInteger(Wobj[2]);

    info->index = NULL;
    if (xrefdictmatch[ e_xrefstreams_index ].result) {
      if (theILen(xrefdictmatch[e_xrefstreams_index].result) == 0 ||
          theILen(xrefdictmatch[e_xrefstreams_index].result)%2 != 0) {
        return (error_handler(RANGECHECK));
      }
      info->index = xrefdictmatch[ e_xrefstreams_index ].result;
    }

    if (xrefdictmatch[ e_xrefstreams_prev ].result) {
      OBJECT * obj = xrefdictmatch[ e_xrefstreams_prev ].result;

      if (oType(*obj) == OINTEGER)
        Hq32x2FromInt32(&info->prev, oInteger( *obj) );
      else
        FileOffsetToHq32x2(info->prev, *obj );

      HQASSERT( Hq32x2Compare( &ixc->trailer_prev, &info->prev) == 0,
                "unexpected offset for previous xrefs");
    } else
      Hq32x2FromInt32(&info->prev, -1); /* don't leave it unset */

    is_xref_stream = TRUE ;
  }

  if (oType(psfilename) == OFILE) {
    /* The 'pdf_filespec' function has resolved the /F key to an
       _embedded_ file stream.  That stream has already been resolved by
       the scanner for another PDF object - so here link to it by
       putting an HqEmbeddedStream key in the parameters dictionary.
       This will be picked up by the streamDecode filter. */
    object_store_name(&name, NAME_HqEmbeddedStream, FALSE) ;
    if ( ! pdf_fast_insert_hash( pdfc, pdfobj , & name , & psfilename ))
      return FALSE ;
  } else if (oType(psfilename) == OSTRING) {
    /* Put the PostScript filename into the dictionary. The streamDecode
       filter uses it to explicitly open the file. */
    object_store_name(&name, NAME_filename, FALSE) ;
    if ( ! pdf_fast_insert_hash( pdfc, pdfobj , & name , & psfilename ))
      return FALSE ;
  }

  /* Put the Length object into the dictionary. */
  object_store_name(&name, NAME_Length, FALSE) ;
  if ( ! pdf_fast_insert_hash( pdfc, pdfobj, &name, streamLength ))
    return FALSE ;

  /* Put a Position object into the dictionary (indicates start of stream). */
  object_store_name(&name, NAME_Position, FALSE) ;
  Hq32x2ToFileOffset(fonewobj,file_pos);
  if ( ! pdf_fast_insert_hash( pdfc, pdfobj, &name, &fonewobj ))
    return FALSE ;

  /* Put a Strict key into the dictionary (for correct end of stream checking). */
  object_store_name(&name, NAME_Strict, FALSE) ;
  object_store_bool(&flag, ixc->strictpdf) ;
  if ( ! pdf_fast_insert_hash( pdfc, pdfobj, &name, &flag ))
    return FALSE ;

  /* Turn the dictionary into a stream
   * ---------------------------------
   * 1) Re-set 'pdfobj' to point either to the current source file.
   *    This becomes the 'underlying' file.
   *
   * 2) Create a StreamDecode filter on the underlying file.
   *    The StreamDecode filter's "init" routine saves the dictionary
   *    in theIParamDict of the newly created filter;
   */
  OCopy( argsobj, *pdfobj );

  file_store_object(pdfobj, flptr, EXECUTABLE) ;
  object_store_name(&name, NAME_StreamDecode, FALSE) ;

  if ( !pdf_createfilter( pdfc, pdfobj, &name, &argsobj, FALSE ) )
    return FALSE ;

  /* We need an implicit decrypt filter inbetween the StreamDecode
     and the filter list (if any). */
  if ( pdfxc->crypt_info != NULL && info == NULL ) {

    if (! pdf_insert_decrypt_filter(pdfc, pdfobj, filter, decodeparams,
                (uint32)(objnum), (uint32)(objgen),
                 oType(psfilename) == ONULL /* Flag - stream or embedded file? */ ))
      return FALSE ;

  } else if ( ! is_xref_stream &&
              ( oType(ixc->trailer_encrypt) == OINDIRECT ||
                oType(ixc->trailer_encrypt) == ODICTIONARY ) ) {

    if (! pdf_begin_decryption(pdfc, &ixc->trailer_encrypt, &ixc->trailer_id))
      return FALSE ;

    if (! pdf_insert_decrypt_filter(pdfc, pdfobj, filter, decodeparams,
                (uint32)(objnum), (uint32)(objgen),
                 oType(psfilename) == ONULL /* Flag - stream or embedded file? */ ))
      return FALSE ;
  }

  /* Layer additional filters onto the file, as specified by the "Filter"
     or "FFilter" entry in the stream-parameters dictionary. */

  /* pdf_createfilterlist() ought to ignore /Crypt filters because
     they are handled by pdf_insert_decrypt_filter(). */
  if (! pdf_createfilterlist( pdfc, pdfobj, filter, decodeparams, TRUE, TRUE ))
    return FALSE;

  return TRUE;
}

Bool pdf_seek_to_compressedxrefobj(PDFCONTEXT *pdfc,
                                   FILELIST **pflptr,
                                   XREFOBJ *objectStream,
                                   int32 targetObjNum)
{
  OBJECT *stream, *streamDict, *extends;
  FILELIST *flptr;
  int32 i, totalObjects, firstOffset, targetOffset = -1;

  enum {
    e_objstream_n,
    e_objstream_first,
    e_objstream_extends,
    e_objstream_max
  };
  static NAMETYPEMATCH match[e_objstream_max + 1] = {
    {NAME_N, 2, {OINTEGER, OINDIRECT}},
    {NAME_First, 2, {OINTEGER, OINDIRECT}},
    {NAME_Extends | OOPTIONAL, 1, {OINDIRECT}},
    DUMMY_END_MATCH
  };

  HQASSERT(pflptr != NULL, "Nowhere to store result.");

  if ( !pdfxObjectStreamDetected(pdfc) )
    return FALSE;

  /* Object streams have an implicit generation of 0. */
  if ( !pdf_lookupxref(pdfc, &stream, objectStream->d.c.objnum, 0, FALSE) )
    return FALSE;

  if ( oType(*stream) != OFILE )
    return error_handler(TYPECHECK);

  flptr = oFile(*stream);
  streamDict = streamLookupDict(stream);
  if ( !pdf_dictmatch(pdfc, streamDict, match) )
    return FALSE;

  totalObjects = oInteger(*match[e_objstream_n].result);
  firstOffset = oInteger(*match[e_objstream_first].result);
  extends = match[e_objstream_extends].result;

  /* We can't seek on the object stream (who knows what filters are in place),
   * so we have to read the whole index, then consume bytes to skip to the
   * requested object, if we find it. */
  for ( i = 0; i < totalObjects; ++i ) {
    int32 objNum, offset, objNumBytes, offsetBytes;

    /* The stream starts with (object number, stream offset) pairs identifying
     * the objects in the stream. */
    if ( !pdf_scan_next_integer_with_bytescount(flptr, &objNum, NULL, NULL,
                                                &objNumBytes) ||
         !pdf_scan_next_integer_with_bytescount(flptr, &offset, NULL, NULL,
                                                &offsetBytes) )
      return FALSE;

    firstOffset -= objNumBytes + offsetBytes; /* subtract bytes just read */

    if ( objNum == targetObjNum ) {
      /* Found the object we're after - save it's offset, relative to
         firstOffset, after some sanity checks. */
      if ( firstOffset < 0 || offset < 0 )
        return error_handler(RANGECHECK);
      targetOffset = firstOffset + offset;
      break;
    }
  }

  if ( targetOffset == -1 ) {
    /* The object was not found but could appear in the stream that this
     * stream extends. */
    if ( extends != NULL ) {
      XREFOBJ nextStream = *objectStream;
      nextStream.d.c.objnum = oXRefID(*extends);
      return pdf_seek_to_compressedxrefobj(pdfc, pflptr, &nextStream, targetObjNum);
    } else
      return error_handler(UNDEFINED);
  }
  else {
    /* Found the object; consume any bytes preceeding it. */
    if ( targetOffset > 0 && file_skip(flptr, targetOffset, NULL) <= 0 )
      return isIIOError(flptr) ? FALSE : error_handler(UNDEFINED);

    *pflptr = flptr;
    return TRUE;
  }
}

/*
* Log stripped */

