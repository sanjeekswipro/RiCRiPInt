/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfscan.c(EBDSDK_P.1) $
 * Copyright (C) 2013-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF lexical analysis
 */

#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "swctype.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "hqmemcpy.h"
#include "tables.h"
#include "namedef_.h"

#include "stacks.h"
#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "chartype.h"

#include "pdfexec.h"
#include "pdfin.h"
#include "pdfmem.h"
#include "pdfops.h"
#include "pdfrepr.h"
#include "pdfscan.h"
#include "pdfstrm.h"
#include "pdfstrobj.h"
#include "pdfxref.h"

#include "pdfncryp.h"

/* To avoid having to expand a dictionary later on, add an extra
 * MIN_EXTRA_DPAIRS pairs of objects. */
#define MIN_EXTRA_DPAIRS 9

typedef struct scancontext {
  /* Stack used to store OBJECTs seen so far. */
  int32  scanlen ;
  uint8 *scanbuf ;
#define SCANCACHE_SIZE 64
  uint8  scancache[ SCANCACHE_SIZE ] ;

  /* Set if scanning a contents stream */
  int32 scancontents ;

  /* Set if scanning an executable array. */
  int32 procedure ;

  STACK *pdfstack ;

  int32 objnum, objgen ;

  /* Parent PDF context. */
  PDFCONTEXT *pdfc ;
} SCANCONTEXT ;

#define CHECK_EXTEND_SCANCACHE( _buf , _len , _sc , _checklen ) MACRO_START \
  if ((_len) >= SCANCACHE_SIZE ) { \
    if ((_len) >= (_checklen)) \
      return error_handler( LIMITCHECK ) ; \
    if ((_len) >= sc->scanlen ) { \
      if ( ! pdf_scanextendcache( sc )) \
        return FALSE ; \
      (_buf) = sc->scanbuf ; \
    } \
  } \
MACRO_END


static Bool pdf_scanop( SCANCONTEXT *sc  , FILELIST *flptr ) ;
static Bool pdf_scanboolnull( SCANCONTEXT *sc  , FILELIST *flptr ,
                               int32 ch1 , int32 ch2 , int32 ch3 ,
                               int32 ch4 ) ;
static Bool pdf_scanproctoken( SCANCONTEXT *sc , FILELIST *flptr ,
                                uint8 buffer [] , int32 index ) ;
static Bool pdf_scanobj( SCANCONTEXT *sc  , FILELIST *flptr ) ;
static Bool pdf_scandigits( SCANCONTEXT *sc  , FILELIST *flptr, Bool do_fileoffset ) ;
static Bool pdf_scanname( SCANCONTEXT *sc  , FILELIST *flptr ) ;
static Bool pdf_scanstring( SCANCONTEXT *sc  , FILELIST *flptr ) ;
static Bool pdf_scandict( SCANCONTEXT *sc  , FILELIST **flptr, Bool do_fileoffset ) ;
static Bool pdf_scanhexstring( SCANCONTEXT *sc  , FILELIST *flptr ) ;
static Bool pdf_scanarray( SCANCONTEXT *sc  , FILELIST **flptr ,
                            int32 arrayterminator ) ;

static Bool pdf_scanobject( SCANCONTEXT *sc , FILELIST **flptr, Bool do_fileoffset ) ;
static Bool pdf_scanobject_internal( SCANCONTEXT *sc , FILELIST **flptr, Bool do_fileoffset ) ;

static Bool values_equal(int32 intvalue, SYSTEMVALUE fltvalue, int32 intflt, int32 checkval) ;
static Bool pdf_scannererror( FILELIST *flptr , int32 syntaxerror ) ;

static Bool pdf_is_encrypted_stream( FILELIST *flptr ) ;

/* -------------------------------------------------------------------------- */
/* Skip white space and comments, and return the character */

static Bool skip_whitespace(FILELIST *flptr, int32 *ch)
{
  do {
    if (( *ch = Getc( flptr )) == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
    if ( *ch == '%' ) {
      do {
        if (( *ch = Getc( flptr )) == EOF )
          return pdf_scannererror( flptr , TRUE ) ;
      } while ( ! IsEndOfLine( *ch )) ;
    }
  } while ( IsWhiteSpace( *ch )) ;

  return TRUE ;
}

/* Check end of token and separator. */

static Bool check_terminator(FILELIST *flptr, char *match,
                             Bool strict, int32 *lineend)
{
  int32 ch ;

  while (*match) {
    ch = Getc(flptr) ;
    if (ch == EOF)
      return pdf_scannererror( flptr , TRUE ) ;
    if (ch != *match++)
      return error_handler( UNDEFINED ) ;
  }

  if (( ch = Getc( flptr )) != EOF ) {
    if (strict) {
      if ( ! IsEndMarkerPDF( ch ))
        return pdf_scannererror( flptr , TRUE ) ;
    }
    if ( IsEndOfLine( ch )) {
      if ( ch == CR &&
        (( ch = Getc( flptr )) != EOF ) &&
           ch != LF )
        UnGetc( ch , flptr ) ;
      (*lineend) = TRUE ;
    }
    else if ( ! IsWhiteSpace( ch ))
      UnGetc( ch , flptr ) ;
  }

  return TRUE ;
}

/* Find "obj" token. */

static Bool pdf_obj_start(FILELIST *flptr, Bool strict,
                          int32 *result, int32 *lineend)
{
  int32 ch ;

  *result = -1 ; /* failed */
  *lineend = FALSE ;

  if (!skip_whitespace(flptr, &ch))  /* returns initial ch */
    return FALSE ;

  if (ch != 'o')
    return error_handler( UNDEFINED ) ;

  if (!check_terminator(flptr, "bj", strict, lineend))
    return FALSE ;

  *result = 0 ; /* success (0 == "obj") */
  return TRUE ;
}

/* Find "endobj", "stream" or if not strict "<int> <int> obj" and "xref" tokens.

   Returns result = 0 for endobj or 1 for stream. */

static Bool pdf_obj_end(FILELIST *flptr, Bool strict,
                        int32 *result, int32 *lineend)
{
  char *match ;
  int32 ch, op = 0 ; /* 0 == "endobj" (explicitly, or implicitly if !strict) */

  *result = -1 ; /* failed */
  *lineend = FALSE ;

  if (!skip_whitespace(flptr, &ch))  /* returns initial ch */
    return FALSE ;

  if (ch == 'e') {
    match = "ndobj" ;

  } else if (ch == 's') {
    match = "tream" ;
    op = 1 ; /* 1 == "stream" */

  } else if (!strict && isdigit(ch)) { /* seen in ibs252800.pdf */
    int more ;
    /* Skip <int> <int> then check for "obj" */
    for (more = 1; more >= 0; --more) {
      do {
        ch = Getc(flptr) ;
        if (ch == EOF)
          return pdf_scannererror(flptr, TRUE) ;
      } while (isdigit(ch)) ;
      if (!IsWhiteSpace(ch))
        return error_handler(UNDEFINED) ;
      do {
        ch = Getc( flptr ) ;
        if (ch == EOF)
          return pdf_scannererror( flptr , TRUE ) ;
      } while (IsWhiteSpace( ch )) ;
      if (more && !isdigit( ch ))
        return error_handler( UNDEFINED ) ;
    }
    if (ch != 'o')
      return error_handler( UNDEFINED ) ;
    match = "bj" ;

  } else if (!strict && ch == 'x') { /* seen in ASC1_0308.pdf */
    match = "ref" ;

  } else
    return error_handler( UNDEFINED ) ;

  if (!check_terminator(flptr, match, strict, lineend))
    return FALSE ;

  *result = op ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */

static Bool pdf_is_encrypted_stream( FILELIST *flptr )
{
  FILELIST *next_flptr = flptr ;

  if (! isIFilter(flptr))
    return FALSE ;

  while (next_flptr != NULL) {
    int32 zee_name = filter_standard_name(next_flptr) ;
    switch (zee_name) {
    case NAME_AESDecode:
    case NAME_RC4Decode:
      return TRUE ;
    }
    next_flptr = next_flptr->underlying_file ;
  }

  return FALSE ;
}

/* Call this to read a particular indirect object (specified by its
 * objnum and objgen) into memory, from the PDF file.
 *
 * Before calling, the caller should look up the object in the file's xref
 * table, and set the file-position to the place where the object is supposed
 * to start.
 *
 * Pass the (correctly positioned) file in "flptr".
 *
 * Pass objnum and objgen by setting theXRefID and theIGen in a local
 * object, and pass a pointer to the object in "pdfobj". Set the
 * "streamDictOnly" flag if you want to skip construction of the
 * stream and its filter chain if the object being read is in fact a
 * stream.
 *
 * Returns:
 * The "*pdfobj" object is overwritten with the newly read-in object.
 * The flag "*streamDict" is set if the object scanned was a stream
 * and only the dict was scanned.
 */
Bool pdf_xrefobject( PDFCONTEXT *pdfc , FILELIST *flptr , OBJECT *pdfobj ,
                     PDF_STREAM_INFO *info,
                     Bool streamDictOnly , int8 *streamDict )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  int32  opres = 0 ; /* Initialise to keep compiler warnings free */
  Bool  lineend = FALSE ;
  int32  result ;
  int32  ssize ;
  OBJECT *theo ;
  STACK  pdfstack ;
  SFRAME pdfstackframe ;
  SCANCONTEXT scancontext ;
  int32 objuse = XREF_Used;

  /* Set these to something to avoid compiler warning: warning C4701:
     potentially uninitialized local variable 'objgen' used */
  int32 objnum = 1, objgen = 1 ;
  Bool is_xref_stream = (info != NULL) ;

  HQASSERT( SCANCACHE_SIZE < MAXPSNAME   , "This code won't work unless this is TRUE" ) ;
  HQASSERT( SCANCACHE_SIZE < MAXPSSTRING , "This code won't work unless this is TRUE" ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( flptr , "flptr NULL in pdf_xrefobject" ) ;
  HQASSERT( pdfobj , "pdfobj NULL in pdf_xrefobject" ) ;

  /* Initialise the pdf OBJECT stack. */
  pdfstack.size = EMPTY_STACK ;
  pdfstack.fptr = ( & pdfstackframe ) ;
  pdfstack.limit = PDFIN_MAX_STACK_SIZE ;
  pdfstack.type = STACK_TYPE_OPERAND ;
  pdfstackframe.link = NULL ;

  scancontext.scancontents = FALSE ;
  scancontext.procedure = FALSE ;
  scancontext.pdfstack = & pdfstack ;

  scancontext.scanlen = SCANCACHE_SIZE ;
  scancontext.scanbuf = scancontext.scancache ;

  scancontext.pdfc = pdfc ;

  if (! is_xref_stream) {
    objgen = theIGen( pdfobj ) ;
    objnum = oXRefID(*pdfobj) ;
  }
  HQASSERT( is_xref_stream || objgen >= 0, "objgen out of range" ) ;
  HQASSERT( is_xref_stream || objnum > 0, "objnum out of range" ) ;

  do {
    int32 intvalue, intflt ;
    SYSTEMVALUE fltvalue = -1.0 ;

    result = FALSE ;
    if (pdf_scan_next_integer(flptr, &intvalue, &fltvalue, &intflt)) { /* object number */
      if (is_xref_stream || values_equal(intvalue, fltvalue, intflt, objnum)) {
        objnum = intflt ? intvalue : (int32)fltvalue ;
        if (pdf_scan_next_integer(flptr, &intvalue, &fltvalue, &intflt)) { /* generation number */
          if (is_xref_stream || values_equal(intvalue, fltvalue, intflt, objgen)) {
            objgen = intflt ? intvalue : (int32)fltvalue ;
            if (pdf_obj_start(flptr, ixc->strictpdf, &opres, &lineend)) { /* "obj" token */
              result = TRUE ;
              break ;
            }
          }
        }
      }
    }

    /* When we are dealing with a normal stream try and repair,
       otherwise repair is attempted latter on. */
    if (isIOpenFile(flptr) && !is_xref_stream) {
      /* Clear any error from trying to read object */
      error_clear_context(pdfc->corecontext->error);
      /* Bad object byte offset, try to repair the job and go again. */
      result = pdf_repair( pdfc , flptr ) && /* Fails if already repaired. */
               pdf_seek_to_xrefobj( pdfc , flptr , objnum , objgen , & objuse, NULL ) ;
    }
  } while ( result && objuse != XREF_Uninitialised ) ;

  if (result) {
    if (objuse == XREF_Uninitialised) {
      OBJECT pdfobj = OBJECT_NOTVM_NULL;
      result = push(&pdfobj, scancontext.pdfstack);

    } else {
      HQASSERT(objuse == XREF_Used, "Got compressed or free object");
      scancontext.objgen = objgen;
      scancontext.objnum = objnum;

      /* Scan in one object, which will be followed by a token:
       * either "endobj" (setting opres to 0)
       * -OR-   "stream" (setting opres to 1)
       */
      result = pdf_scanobject( &scancontext , &flptr, FALSE ) &&
               pdf_obj_end( flptr , ixc->strictpdf , &opres , &lineend ) ;
    }
  }

  if ( scancontext.scanbuf != scancontext.scancache )
    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )scancontext.scanbuf ,
             scancontext.scanlen ) ;

  if ( result ) {
    HQASSERT( opres >= 0 , "somehow succeeded & opres got left -ve" ) ;
    ssize = theStackSize( pdfstack ) ;
    HQASSERT( ! EmptyStack( ssize ) , "somehow ended up with empty stack" ) ;

    /* Copy scanned object into *pdfobj */
    theo = TopStack( pdfstack , ssize ) ;
    Copy( pdfobj, theo );

    if ( opres == 1 ) {
      /* The token after the object was "stream".  Create the stream
       * now.
       *
       * pdfobj is the stream-parameters dictionary we have just
       * scanned.  We pass it to pdf_streamobject(), which creates a
       * new stream, saves the dictionary in theIParamDict slot, and
       * changes *pdfobj to be the new stream.
       *
       * Note that the spec says stream should be terminated with a
       * CRLF pair, otherwise it's impossible to tell whether the file
       * came from a PC and the CRLF is a line end or the file came
       * from a Mac and actually the LF is the first byte of encoded
       * data. Guess what? Barco generated PDF has stream followed by
       * a space then an LF.
       */

      if ( ! streamDictOnly ) {
        if ( ! lineend ) {
          if ( ixc->strictpdf ) {
            return error_handler( SYNTAXERROR ) ;
          }
          else {
            int32 ch ;

            /* We've got the bad Barco token. All we can do is consume
             * a line-end however it appears. If the LF was meant to
             * be the first char of encoded data, we have absolutely
             * no way of knowing.
             */

            if (( ch = Getc( flptr )) != EOF ) {
              if ( IsEndOfLine( ch )) {
                if ( ch == CR &&
                     (( ch = Getc( flptr )) != EOF ) &&
                     ch != LF )
                  UnGetc( ch , flptr ) ;
              }
              else {
                /* The stream token was terminated by something other
                 * than a line end, but there's no line end following
                 * that. Definitely an error.
                 */
                return error_handler( SYNTAXERROR ) ;
              }
            }
          }
        }

        *streamDict = XREF_FullStream ;

        result = pdf_streamobject( pdfc , flptr , pdfobj , objnum , objgen, info ) ;
      }
      else {
        *streamDict = XREF_StreamDict ;
      }
    }
    else {
      *streamDict = XREF_NotStream ;
    }

    if ( result )
      pop( & pdfstack ) ;
  }

  /* Free up any composite objects scanned. */
  pdf_clearstack( pdfc , & pdfstack , EMPTY_STACK ) ;

  return result ;
}

/* Call this to read a particular indirect stream object (specified by
 * its objnum and objgen) into memory, from the PDF file (>=PDF1.5).
 *
 * Before calling, the caller should look up the object in the file's
 * xref table, and set the file-position to the place where the object
 * is supposed to start.
 *
 * Pass the (correctly positioned) file in "flptr".
 *
 * Pass objnum and objgen by setting theXRefID and theIGen in a local
 * object, and pass a pointer to the object in "pdfobj".  (This gets
 * changed to be the newly read-in object).
 *
 * Returns:
 * The "*pdfobj" object is overwritten with the newly read-in object.
 */
Bool pdf_xrefstreamobj( PDFCONTEXT *pdfc , FILELIST *flptr , OBJECT *pdfobj )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  int32  result ;
  int32  ssize ;
  OBJECT *theo ;
  STACK  pdfstack ;
  SFRAME pdfstackframe ;
  SCANCONTEXT scancontext ;
  int32 objnum , objgen ;

  HQASSERT( SCANCACHE_SIZE < MAXPSNAME   , "This code won't work unless this is TRUE" ) ;
  HQASSERT( SCANCACHE_SIZE < MAXPSSTRING , "This code won't work unless this is TRUE" ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( flptr , "flptr NULL in pdf_xrefobject" ) ;
  HQASSERT( pdfobj , "pdfobj NULL in pdf_xrefobject" ) ;

  objgen = theIGen( pdfobj ) ;
  objnum = oXRefID(*pdfobj) ;

  HQASSERT( objgen >= 0 , "objgen out of range" ) ;
  HQASSERT( objnum >  0 , "objnum out of range" ) ;

  scancontext.objgen = objgen;
  scancontext.objnum = objnum;

  /* Initialise the pdf OBJECT stack. */
  pdfstack.size = EMPTY_STACK ;
  pdfstack.fptr = ( & pdfstackframe ) ;
  pdfstack.limit = PDFIN_MAX_STACK_SIZE ;
  pdfstack.type = STACK_TYPE_OPERAND ;
  pdfstackframe.link = NULL ;

  scancontext.scancontents = FALSE ;
  scancontext.procedure = FALSE ;
  scancontext.pdfstack = & pdfstack ;

  scancontext.scanlen = SCANCACHE_SIZE ;
  scancontext.scanbuf = scancontext.scancache ;

  scancontext.pdfc = pdfc ;

  result = pdf_scanobject( & scancontext , & flptr, FALSE );

  if ( scancontext.scanbuf != scancontext.scancache )
    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )scancontext.scanbuf ,
             scancontext.scanlen ) ;
  if ( result ) {
    ssize = theStackSize( pdfstack ) ;
    HQASSERT( ! EmptyStack( ssize ) , "somehow ended up with empty stack" ) ;

    /* Copy scanned object into *pdfobj */
    theo = TopStack( pdfstack , ssize ) ;
    Copy( pdfobj, theo );

    if ( result )
      pop( & pdfstack ) ;
  }

  /* Free up any composite objects scanned. */
  pdf_clearstack( pdfc , & pdfstack , EMPTY_STACK ) ;

  return result ;
}

/* ---------------------------------------------------------------------- */
/* pdf_readobject
 * --------------
 * Apparently only used for the PDF Trailer.
 */
Bool pdf_readobject( PDFCONTEXT *pdfc , FILELIST *flptr , OBJECT *pdfobj )
{
  PDFXCONTEXT *pdfxc ;
  int32  result ;
  int32  ssize ;
  OBJECT *theo ;
  STACK  pdfstack ;
  SFRAME pdfstackframe ;
  SCANCONTEXT scancontext ;

  HQASSERT( SCANCACHE_SIZE < MAXPSNAME   , "This code won't work unless this is TRUE" ) ;
  HQASSERT( SCANCACHE_SIZE < MAXPSSTRING , "This code won't work unless this is TRUE" ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  HQASSERT( flptr , "flptr NULL in pdf_readobject" ) ;
  HQASSERT( pdfobj , "pdfobj NULL in pdf_readobject" ) ;

  scancontext.objgen = 0;
  scancontext.objnum = 0;

  /* Initialise the pdf OBJECT stack. */
  pdfstack.size = EMPTY_STACK ;
  pdfstack.fptr = ( & pdfstackframe ) ;
  pdfstack.limit = PDFIN_MAX_STACK_SIZE ;
  pdfstack.type = STACK_TYPE_OPERAND ;
  pdfstackframe.link = NULL ;

  scancontext.scancontents = FALSE ;
  scancontext.procedure = FALSE ;
  scancontext.pdfstack = & pdfstack ;

  scancontext.scanlen = SCANCACHE_SIZE ;
  scancontext.scanbuf = scancontext.scancache ;

  scancontext.pdfc = pdfc ;

  result = pdf_scanobject_internal( & scancontext , & flptr, TRUE ) ;

  if ( scancontext.scanbuf != scancontext.scancache )
    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )scancontext.scanbuf ,
             scancontext.scanlen ) ;

  if ( result ) {
    ssize = theStackSize( pdfstack ) ;
    if ( EmptyStack( ssize )) {
      theITags( pdfobj ) = ONOTHING ;
      return result ;
    }
    theo = TopStack( pdfstack , ssize ) ;

    /* Copy scanned object into *pdfobj */
    theo = TopStack( pdfstack , ssize ) ;
    Copy( pdfobj, theo );

    pop( & pdfstack ) ;
  }

  pdf_clearstack( pdfc , & pdfstack , EMPTY_STACK ) ;
  return result ;
}

/* ---------------------------------------------------------------------- */
Bool pdf_scancontent( PDFCONTEXT *pdfc , FILELIST **flptr , OBJECT *pdfobj )
{
  PDFXCONTEXT *pdfxc ;
  Bool  result ;
  int32  ssize ;
  OBJECT *theo ;
  STACK  pdfstack ;
  SFRAME pdfstackframe ;
  SCANCONTEXT scancontext ;

  HQASSERT( SCANCACHE_SIZE < MAXPSNAME   , "This code won't work unless this is TRUE" ) ;
  HQASSERT( SCANCACHE_SIZE < MAXPSSTRING , "This code won't work unless this is TRUE" ) ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  HQASSERT( pdfc->contents , "contents NULL in pdf_scancontent" ) ;
  HQASSERT( flptr , "flptr NULL in pdf_scancontent" ) ;
  HQASSERT( pdfobj , "pdfobj NULL in pdf_scancontent" ) ;

  /* Initialise the pdf OBJECT stack. */
  pdfstack.size = EMPTY_STACK ;
  pdfstack.fptr = ( & pdfstackframe ) ;
  pdfstack.limit = PDFIN_MAX_STACK_SIZE ;
  pdfstack.type = STACK_TYPE_OPERAND ;
  pdfstackframe.link = NULL ;

  scancontext.scancontents = TRUE ;
  scancontext.procedure = FALSE ;
  scancontext.pdfstack = & pdfstack ;

  scancontext.scanlen = SCANCACHE_SIZE ;
  scancontext.scanbuf = scancontext.scancache ;

  scancontext.pdfc = pdfc ;

  result = pdf_scanobject( & scancontext , flptr, FALSE ) ;

  if ( scancontext.scanbuf != scancontext.scancache )
    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )scancontext.scanbuf ,
             scancontext.scanlen ) ;

  if ( result ) {
    Bool glmode ;

    ssize = theStackSize( pdfstack ) ;
    if ( EmptyStack( ssize )) {
      theITags( pdfobj ) = ONOTHING ;
      return result ;
    }
    theo = TopStack( pdfstack , ssize ) ;

    /* Create the object using local PS VM */

    glmode = setglallocmode(pdfc->corecontext, FALSE ) ;

    result = result &&
             pdf_copyobject( NULL, theo, pdfobj ) ;

    setglallocmode(pdfc->corecontext, glmode ) ;
  }

  /* Free up any composite objects scanned. */
  pdf_clearstack( pdfc , & pdfstack , EMPTY_STACK ) ;

  return result ;
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scanextendcache( SCANCONTEXT *sc )
{
  int32 oldlen ;
  int32 newlen ;
  uint8 *oldmem ;
  uint8 *newmem ;
  PDFCONTEXT *pdfc ;
  PDFXCONTEXT *pdfxc ;

  HQASSERT( sc , "sc NULL in pdf_scanextendcache" ) ;

  pdfc = sc->pdfc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  oldlen = sc->scanlen ;
  newlen = oldlen * 2 ;

  oldmem = sc->scanbuf ;
  newmem = mm_alloc( pdfxc->mm_structure_pool ,
                     newlen ,
                     MM_ALLOC_CLASS_PDF_SCANCACHE ) ;
  if ( ! newmem )
    return error_handler( VMERROR ) ;

  HqMemCpy( newmem , oldmem , oldlen ) ;
  if ( oldmem != sc->scancache )
    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )oldmem ,
             oldlen ) ;

  sc->scanlen = newlen ;
  sc->scanbuf = newmem ;

  return TRUE ;
}

static Bool pdf_scanobject( SCANCONTEXT *sc , FILELIST **flptr, Bool do_fileoffset )
{
  return pdf_scanobject_internal( sc , flptr, do_fileoffset );
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scanobject_internal( SCANCONTEXT *sc , FILELIST **flptr, Bool do_fileoffset )
{
  int32 ch ;

  HQASSERT( sc , "sc NULL in pdf_scanobject_internal" ) ;
  HQASSERT( flptr && *flptr , "flptr NULL in pdf_scanobject_internal" ) ;

  /* Skip white space (and comments)
   * If we reach the end of the file at this point then that's ok - it just
   * means that it had trailing whitespace and/or comments; nothing to
   * complain about
   */
  do {
    if (( ch = Getc( *flptr )) == EOF )
      return pdf_scannererror( *flptr , FALSE ) ;
    else if ( ch == '%' ) {
      do {
        if (( ch = Getc( *flptr )) == EOF )
          return pdf_scannererror( *flptr , FALSE ) ;
      } while ( ! IsEndOfLine( ch )) ;
    }
  } while ( IsWhiteSpace( ch )) ;

  /* Determine potential object type. */
  if ( isdigit( ch )) {
    UnGetc( ch , *flptr ) ;
    return pdf_scandigits( sc , *flptr, do_fileoffset ) ;
  }
  else if ( isalpha( ch )) {
    UnGetc( ch , *flptr ) ;
    if ( sc->scancontents )
      return pdf_scanop( sc , *flptr ) ;
    else
      return pdf_scanobj( sc , *flptr ) ;
  }
  else if ( IsSpecialChar( ch )) {
    switch ( ch ) {
    case '/':
      return pdf_scanname( sc , *flptr ) ;
    case '(':
      return pdf_scanstring( sc , *flptr ) ;
    case '<':
      if (( ch = Getc( *flptr )) == EOF )
        return pdf_scannererror( *flptr , TRUE ) ;
      if ( ch == '<' )
        return pdf_scandict( sc , flptr, do_fileoffset ) ;
      else {
        UnGetc( ch , *flptr ) ;
        return pdf_scanhexstring( sc , *flptr ) ;
      }
    case '{':
      return pdf_scanarray( sc , flptr , '}' ) ;
    case '[':
      return pdf_scanarray( sc , flptr , ']' ) ;
    default:
      return error_handler( SYNTAXERROR ) ;
    }
  }
  else {
    switch ( ch ) {
    case '\'':
    case '\"':
      if ( sc->scancontents ) {
        UnGetc( ch , *flptr ) ;
        return pdf_scanop( sc , *flptr ) ;
      }
      else
        return error_handler( SYNTAXERROR ) ;
    case '-':
    case '.':
    case '+':
      UnGetc( ch , *flptr ) ;
      return pdf_scandigits( sc , *flptr, do_fileoffset ) ;
    default:
      return error_handler( SYNTAXERROR ) ;
    }
  }
  /* Not reached. */
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scanop( SCANCONTEXT *sc , FILELIST *flptr )
{
  /* Allowed operators are { false, true , null } */

  int32 ch ;
  int32 ch1 , ch2 , ch3 ;
  OBJECT pdfobj = OBJECT_NOTVM_NOTHING ;
  STACK *stack ;

  HQASSERT( sc , "sc NULL in pdf_scanop" ) ;
  HQASSERT( flptr , "flptr NULL in pdf_scanop" ) ;

  stack = sc->pdfstack ;

  ch1 = Getc( flptr ) ;
  HQASSERT( ch1 != EOF , "always UnGetc before calling pdf_scanop" ) ;

  /* Get at most 3 characters */
  ch2 = ch3 = 0 ;
  if (( ch = Getc( flptr )) != EOF ) {
    if ( ! IsEndMarkerPDF( ch )) {
      ch2 = ch ;
      if (( ch = Getc( flptr )) != EOF ) {
        if ( ! IsEndMarkerPDF( ch )) {
          ch3 = ch ;
          if (( ch = Getc( flptr )) != EOF ) {
            if ( ! IsEndMarkerPDF( ch )) {
              /* It can't be an operator.
               * Could still be true or false or null, though..
               */
              return pdf_scanboolnull( sc, flptr, ch1 , ch2 , ch3 , ch ) ;
            }
          }
        }
      }
    }
  }
  /* And check correctly terminated (dealing with CR/LF pairs). */
  HQASSERT( ch == EOF || IsEndMarkerPDF( ch ) , "don't know what char this is" ) ;
  if ( ch != EOF ) {
    if ( IsEndOfLine( ch )) {
      if ( ch == CR &&
        (( ch = Getc( flptr )) != EOF ) &&
           ch != LF )
        UnGetc( ch , flptr ) ;
    }
    else if ( ! IsWhiteSpace( ch ))
      UnGetc( ch , flptr ) ;
  }

  /* Determine the operator; note there is some overloading here. */
  theTags( pdfobj ) = OOPERATOR ;
  oInteger(pdfobj) = pdf_whichop( ch1 , ch2 , ch3 ) ;

  return push( & pdfobj , stack ) ;
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scanboolnull( SCANCONTEXT *sc , FILELIST *flptr ,
                               int32 ch1 , int32 ch2 , int32 ch3 ,
                               int32 ch4 )
{
  /* Allowed operators are { false, true , null } */

  OBJECT pdfobj = OBJECT_NOTVM_NOTHING ;
  STACK *stack ;
  int32 ch ;

  HQASSERT( sc , "sc NULL in pdf_scancsobj" ) ;
  HQASSERT( flptr , "flptr NULL in pdf_scancsobj" ) ;

  stack = sc->pdfstack ;

  if ( ch1 == 'n' && ch2 == 'u' && ch3 == 'l' && ch4 == 'l' ) {
    if (( ch = Getc( flptr )) != EOF ) {
      if ( ! IsEndMarkerPDF( ch ))
        return error_handler( UNDEFINED ) ;
    }
    theTags( pdfobj ) = ONULL ;
  }
  else if ( ch1 == 't' && ch2 == 'r' && ch3 == 'u' && ch4 == 'e' ) {
    if (( ch = Getc( flptr )) != EOF ) {
      if ( ! IsEndMarkerPDF( ch ))
        return error_handler( UNDEFINED ) ;
    }
    theTags( pdfobj ) = OBOOLEAN ;
    oBool(pdfobj) = TRUE ;
  }
  else if ( ch1 == 'f' && ch2 == 'a' && ch3 == 'l' && ch4 == 's' ) {
    if (( ch = Getc( flptr )) != EOF ) {
      if ( ch == 'e' ) {
        if (( ch = Getc( flptr )) != EOF ) {
          if ( ! IsEndMarkerPDF( ch ))
            return error_handler( UNDEFINED ) ;
        }
        theTags( pdfobj ) = OBOOLEAN ;
        oBool(pdfobj) = FALSE ;
      }
      else
        return error_handler( UNDEFINED ) ;
    }
    else
      return error_handler( UNDEFINED ) ;
  }
  else
    return error_handler( UNDEFINED ) ;

  /* And check correctly terminated (dealing with CR/LF pairs). */
  HQASSERT( ch == EOF || IsEndMarkerPDF( ch ) , "don't know what char this is" ) ;
  if ( ch != EOF ) {
    if ( IsEndOfLine( ch )) {
      if ( ch == CR &&
        (( ch = Getc( flptr )) != EOF ) &&
           ch != LF )
        UnGetc( ch , flptr ) ;
    }
    else if ( ! IsWhiteSpace( ch ))
      UnGetc( ch , flptr ) ;
  }

  return push( & pdfobj , stack ) ;
}

/* ---------------------------------------------------------------------- */

static Bool pdf_scanproctoken( SCANCONTEXT *sc , FILELIST *flptr ,
                                uint8 buffer [] , int32 index )
{
  STACK *stack ;
  int32 ch ;
  NAMECACHE * opname ;

  HQASSERT( sc , "sc is null in pdf_scanproctoken" ) ;
  HQASSERT( flptr , "flptr is null in pdf_scanproctoken" ) ;
  HQASSERT( buffer , "buffer is null in pdf_scanproctoken" ) ;

  if ( ! sc->procedure )
    /* Not scanning an execuable array therefore disallow tokens from
     * this set. */
    return error_handler( UNDEFINED ) ;

  stack = sc->pdfstack ;
  /* Read rest of token. If token is over buffer limit then signal
   * syntax error. If token hits EOF then signal scanner error.
   */
  if ( ( ch = Getc( flptr )) == EOF )
    return pdf_scannererror( flptr , TRUE ) ;

  while ( ! IsEndMarkerPDF( ch )) {
    if ( index >= MAXPSNAME )
      return error_handler( SYNTAXERROR ) ;
    buffer[ index++ ] = (uint8) ch ;
    if ( ( ch = Getc( flptr )) == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
  }

  opname = cachename(( index > 0 ) ? buffer : NULL , ( uint32 )index ) ;
  if ( ( NULL == opname ) ||
       ( theIOpClass( opname ) & FUNCTIONOP ) == 0 )
    return error_handler( UNDEFINED ) ;

  HQASSERT( theINameNumber( opname ) >= 0 &&
            theINameNumber( opname ) < OPS_COUNTED ,
            "operator number out of bounds in pdf_scanproctoken" ) ;

  oName(nnewobje) = opname;
  return push( & nnewobje , stack ) ;
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scanobj( SCANCONTEXT *sc , FILELIST *flptr )
{
  /* Allowed operators are { R, false, true , null } */

  int32 ch ;
  int32 op ;
  OBJECT pdfobj = OBJECT_NOTVM_NOTHING ;
  uint8 *match ;
  uint8 *tokens[] = {
    ( uint8 * )"R" ,
    ( uint8 * )"false" ,
    ( uint8 * )"true" ,
    ( uint8 * )"null" ,
    NULL
  } ;
  int32 ssize ;
  int32 iobj , igen ;
  OBJECT *oobj , *ogen ;
  STACK *stack ;
  uint8 buffer[MAXPSNAME + 1]; /* 1 for is end marker */
  int32 index = 0;

  HQASSERT( sc , "sc NULL in pdf_scanobj" ) ;
  HQASSERT( flptr , "flptr NULL in pdf_scanobj" ) ;

  stack = sc->pdfstack ;

  ch = Getc( flptr ) ;
  buffer[index++] = (uint8) ch;
  HQASSERT( ch != EOF , "always UnGetc before calling pdf_scanobj" ) ;

  /* Find which token we're matching. */
  for ( op = 0 ; ( match = tokens[ op ] ) != NULL ; ++op )
    if ((*match++) == ch )
      break ;
  if ( ! match )
    return pdf_scanproctoken( sc , flptr , buffer , index ) ;

  /* Read rest of token. */
  while ( *match != '\0' ) {
    ch = Getc( flptr );
    if ( ch == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
    buffer[index++] = (uint8) ch;
    if ((*match++) != ch )
      return pdf_scanproctoken( sc , flptr , buffer , index ) ;
  }

  /* Check token separated properly. */
  if (( ch = Getc( flptr )) != EOF ) {
    if ( ! IsEndMarkerPDF( ch )) {
      buffer[index++] = (uint8) ch;
      return pdf_scanproctoken( sc , flptr , buffer , index ) ;
    }
    if ( IsEndOfLine( ch )) {
      if ( ch == CR &&
           (( ch = Getc( flptr )) != EOF ) &&
              ch != LF )
        UnGetc( ch , flptr ) ;
    }
    else if ( ! IsWhiteSpace( ch ))
      UnGetc( ch , flptr ) ;
  }

  /* Setup object. */
  switch ( op ) {
  case 0:
    ssize = theIStackSize( stack ) ;
    if ( ssize < 1 )
      return error_handler( STACKUNDERFLOW ) ;
    ogen = theITop( stack ) ;
    oobj = ( & ogen[ -1 ] ) ;
    if ( ! fastIStackAccess( stack ))
      oobj = stackindex( 1 , stack ) ;
    if ( oType(*oobj) != OINTEGER ||
         oType(*ogen) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    iobj = oInteger(*oobj) ;
    igen = oInteger(*ogen) ;
    if ( igen < 0x0000 || igen > 0xFFFF )
      return error_handler( RANGECHECK ) ;
    theTags( pdfobj ) = OINDIRECT | LITERAL ;
    theGen( pdfobj ) = ( uint16 )igen ;
    oXRefID(pdfobj) = iobj ;
    Copy( oobj , & pdfobj ) ;
    pop( stack ) ;
    return TRUE ;

  case 1:
  case 2:
    theTags( pdfobj ) = OBOOLEAN | LITERAL ;
    oBool(pdfobj) = (op-1) /* Yuck */ ;
    return push( & pdfobj , stack ) ;

  case 3:
    theTags( pdfobj ) = ONULL | LITERAL ;
    return push( & pdfobj , stack ) ;

  default:
    HQFAIL( "Unknown operator" ) ;
  }
  return error_handler( UNREGISTERED ) ;
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scandigits( SCANCONTEXT *sc  , FILELIST *flptr, Bool do_fileoffset )
{
  int32 ch ;
  OBJECT pdfobj = OBJECT_NOTVM_NOTHING ;
  STACK *stack ;

  int32 sign ;
  int32 ntotal ;
  int32 nleading ;
  int32 ileading ;
  SYSTEMVALUE fleading ;
  PDFCONTEXT *pdfc ;
  PDFXCONTEXT *pdfxc ;

  HQASSERT( sc , "sc NULL in pdf_scandigits" ) ;
  HQASSERT( flptr , "flptr NULL in pdf_scandigits" ) ;

  pdfc = sc->pdfc ;
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  stack = sc->pdfstack ;

  sign = 1 ;
  ch = Getc( flptr ) ;
  HQASSERT( ch != EOF , "always UnGetc before calling pdf_scandigits" ) ;
  if ( ch == '-' || ch == '+' ) {
    if ( ch == '-' )
      sign = -1 ;
    if (( ch = Getc( flptr )) == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
  }

  nleading = 0 ;
  ileading = 0 ;
  fleading = 0.0 ;
  /* Scan the m part of a (m.n) number. */
  do {
    if ( ! isdigit( ch ))
      break ;
    ch = ( ch - '0' ) ;
    ++nleading ;
    if ( nleading < 10 )
      ileading = 10 * ileading + ch ;
    else if ( nleading > 10 )
      fleading = 10.0 * fleading + ch ;
    else
      fleading = 10.0 * ( SYSTEMVALUE )ileading + ch ;
  } while (( ch = Getc( flptr )) != EOF ) ;

  ntotal = nleading ;
  /* Scan the . part of a (m.n) number. */
  if ( ch == '.' ) {
    /* Scan the n part of a (m.n) number. */
    while (( ch = Getc( flptr )) != EOF ) {
      if ( ! isdigit( ch ))
        break ;
      ch = ( ch - '0' ) ;
      ++ntotal ;
      if ( ntotal < 10 )
        ileading = 10 * ileading + ch ;
      else if ( ntotal > 10 )
        fleading = 10.0 * fleading + ch ;
      else
        fleading = 10.0 * ( SYSTEMVALUE )ileading + ch ;
    }
  }

  /* Bad number; probably just a {+,-,.,+.,-.}. */
  if ( ntotal == 0 )
    return error_handler( SYNTAXERROR ) ;

  if ( ch != EOF ) {
    if ( ! IsEndMarkerPDF( ch ))
      return pdf_scannererror( flptr , TRUE ) ;
    if ( IsEndOfLine( ch )) {
      if ( ch == CR &&
           (( ch = Getc( flptr )) != EOF ) &&
              ch != LF )
        UnGetc( ch , flptr ) ;
    }
    else if ( ! IsWhiteSpace( ch ))
      UnGetc( ch , flptr ) ;
  }

  /* Convert number to integer or real. */
  if ( ntotal == nleading && nleading > 0 ) {
    /* We scanned (m) or (m.). */
    if ( nleading < 10 ) {
      /* Less than 10 digits, so definitely an integer. */
      if ( sign < 0 )
        ileading = -ileading ;
      theTags( pdfobj ) = OINTEGER | LITERAL ;
      oInteger(pdfobj) = ileading ;
    }
    else {
      /* More than 10 digits, so may be a real or too large (i.e. overflow). */
      if ( sign < 0 )
        fleading = -fleading ;
      if ( intrange( fleading )) {
        theTags( pdfobj ) = OINTEGER | LITERAL ;
        oInteger(pdfobj) = ( int32 )fleading ;
      }
      else {

        /* faced with a large integer we approximate this with a real
           unless we are in the trailer dict where we create a rare
           OFILEOFFSET value. */
        if (do_fileoffset) {
          Hq32x2 t_big;

          HQASSERT(floor(fleading) == fleading,"out of range fileoffset.");
          if (fabs(fleading) > 10000000000.0)
            return error_handler( RANGECHECK ) ;

          Hq32x2FromDouble(&t_big, fleading );
          theTags( pdfobj ) = OFILEOFFSET | LITERAL ;
          Hq32x2ToFileOffset(pdfobj,t_big);
        } else {
          if ( ! realrange( fleading ))
            return error_handler( RANGECHECK ) ;
          theTags( pdfobj ) = OREAL | LITERAL ;
          oReal( pdfobj ) = ( USERVALUE )fleading ;
        }
      }
    }
  }
  else {
    /* We scanned (.n) or (m.n). */
    int32 ntrailing = ntotal - nleading ;
    SYSTEMVALUE fdivs[ 11 ] = { 0.0 , 0.1 , 0.01 , 0.001 , 0.0001 , 0.00001 , 0.000001 ,
                                0.0000001 , 0.00000001 , 0.000000001 , 0.0000000001 } ;
    if ( ntotal < 10 ) {
      if ( sign < 0 )
        ileading = -ileading ;
      fleading = ( SYSTEMVALUE )ileading * fdivs[ ntrailing ] ;
    }
    else {
      if ( sign < 0 )
        fleading = -fleading ;
      if ( ntrailing < 10 )
        fleading = fleading * fdivs[ ntrailing ] ;
      else
        fleading = fleading / pow( 10.0 , ( SYSTEMVALUE )( ntrailing )) ;

      if ( nleading > 10 && ! realrange( fleading ))
        return error_handler( RANGECHECK ) ;
      if ( ntrailing > 10 && ! realprecision( fleading ))
        fleading = 0.0 ;
    }
    theTags( pdfobj ) = OREAL | LITERAL ;
    oReal(pdfobj) = ( USERVALUE )fleading ;
  }

  return push( & pdfobj , stack ) ;
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scanname( SCANCONTEXT *sc , FILELIST *flptr )
{
  int32 ch ;
  int32 len ;
  uint8 *namebuf ;
  NAMECACHE *name ;
  OBJECT pdfobj = OBJECT_NOTVM_NOTHING ;
  STACK *stack ;
  PDFCONTEXT *pdfc ;
  PDFXCONTEXT *pdfxc ;

  HQASSERT( sc , "sc NULL in pdf_scanname" ) ;
  HQASSERT( flptr , "flptr NULL in pdf_scanname" ) ;

  pdfc = sc->pdfc ;
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  stack = sc->pdfstack ;
  namebuf = sc->scanbuf ;

  len = 0 ;
  while (( ch = Getc( flptr )) != EOF ) {
    if ( ch == '#' ) {
      int32 ch1 , ch2 ;

      if (( ch1 = Getc( flptr )) == EOF )
        return pdf_scannererror( flptr , TRUE ) ;
      ch1 = char_to_hex_nibble[ ch1 ] ;
      if ( ch1 < 0 )
        return error_handler( SYNTAXERROR ) ;
      if (( ch2 = Getc( flptr )) == EOF )
        return pdf_scannererror( flptr , TRUE ) ;
      ch2 = char_to_hex_nibble[ ch2 ] ;
      if ( ch2 < 0 )
        return error_handler( SYNTAXERROR ) ;
      ch = ( ch1 << 4 ) + ch2 ;
    }
    else {
      if ( IsEndMarkerPDF( ch )) {
        if ( IsEndOfLine( ch )) {
          if ( ch == CR &&
            (( ch = Getc( flptr )) != EOF ) &&
               ch != LF )
            UnGetc( ch , flptr ) ;
        }
        else if ( ! IsWhiteSpace( ch ))
          UnGetc( ch , flptr ) ;
        break ;
      }
      /* The spec says we should do this, but Acrobat doesn't:
       * else
       *   if ( ch < 0x21 || ch > 0x7E )
       *     return error_handler( UNDEFINED ) ;
       */
    }

    CHECK_EXTEND_SCANCACHE( namebuf , len , sc , MAXPSNAME ) ;
    namebuf[ len++ ] = ( uint8 )ch ;
  }

  name = cachename(( len > 0 ) ? namebuf : NULL , ( uint32 )len ) ;
  if ( ! name )
    return error_handler( VMERROR ) ;

  theTags( pdfobj ) = ONAME | LITERAL ;
  oName(pdfobj) = name ;

  return push( & pdfobj , stack ) ;
}

/* ---------------------------------------------------------------------- */

/* Helper fn for scanstring and scanhexstring. */
static Bool pdf_push_scanbuf_as_obj(SCANCONTEXT *sc, FILELIST *flptr, int32 len)
{
  uint8 *stringbuf = sc->scanbuf;
  uint8 *string;
  PDFCONTEXT *pdfc = sc->pdfc;
  PDFXCONTEXT *pdfxc;
  OBJECT pdfobj = OBJECT_NOTVM_NOTHING;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  if ( len == 0 )
    string = NULL ;
  else {
    string = PDF_ALLOCSTRING( pdfxc, len ) ;
    if ( string == NULL )
      return error_handler( VMERROR ) ;

    /* Copy the stringbuf into string. If it is encrypted, decrypt it. */
    if ( ! sc->scancontents && pdfxc->crypt_info != NULL
         && ! pdf_is_encrypted_stream(flptr) ) {
      uint8 *tempstring;
      int32 newlen;

      /* Note that pdf_decrypt_string raises PS error on failure. */
      if (! pdf_decrypt_string(pdfxc, stringbuf, string, len, &newlen,
                               (uint32)(sc->objnum), (uint32)(sc->objgen)) )
        return FALSE ;

      if ( newlen != len ) {
        if ( newlen == 0 )
          tempstring = NULL;
        else {
          HQASSERT(newlen < len, "decrypted string is not shorter than original") ;

          tempstring = PDF_ALLOCSTRING( pdfxc, newlen ) ;
          if ( tempstring == NULL ) {
            PDF_FREESTRING( pdfxc, string, len ) ;
            return error_handler( VMERROR ) ;
          }
          HqMemCpy(tempstring, string, newlen) ;
        }
        PDF_FREESTRING( pdfxc, string, len ) ;
        string = tempstring; len = newlen;
      }
    } else
      HqMemCpy(string, stringbuf, len);
  }

  theTags(pdfobj) = OSTRING | UNLIMITED | LITERAL;
  theLen(pdfobj) = (uint16)len;
  oString(pdfobj) = string;

  return push(&pdfobj, sc->pdfstack);
}


static Bool pdf_scanstring( SCANCONTEXT *sc , FILELIST *flptr )
{
  int32 ch ;
  int32 len;
  int32 paren ;
  uint8 *stringbuf ;

  HQASSERT( sc , "sc NULL in pdf_scanstring" ) ;
  HQASSERT( flptr , "flptr NULL in pdf_scanstring" ) ;

  stringbuf = sc->scanbuf ;

  len = 0 ;
  paren = 1 ;
  for (;;) {
    if (( ch = Getc( flptr )) == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
    if ( isalpha( ch )) {
      /* common case */
      EMPTY_STATEMENT() ;
    }
    else if ( ch == ')' ) {
      --paren ;
      if ( paren == 0 )
        break ;
    }
    else if ( ch == '(' )
      ++paren ;
    else if ( ch == '\\' ) {
      if (( ch = Getc( flptr )) == EOF )
        return pdf_scannererror( flptr , TRUE ) ;
      if (( ch >= '0' ) && ( ch <= '7' )) {
        int32 chval = ( ch - '0' ) ;
        if (( ch = Getc( flptr )) == EOF )
          return pdf_scannererror( flptr , TRUE ) ;
        if (( ch >= '0' ) && ( ch <= '7' )) {
          chval = ( chval << 3 ) + ( ch - '0' ) ;
          if (( ch = Getc( flptr )) == EOF )
            return pdf_scannererror( flptr , TRUE ) ;
          if (( ch >= '0' ) && ( ch <= '7' )) {
            chval = ( chval << 3 ) + ( ch - '0' ) ;
            if (( ch = Getc( flptr )) == EOF )
              return pdf_scannererror( flptr , TRUE ) ;
          }
        }
        UnGetc( ch , flptr ) ;
        ch = chval ;
      }
      else {
        switch ( ch ) {
        case 'n':  ch = LF ;  break ;  /* linefeed           */
        case 'r':  ch = CR ;  break ;  /* carriage return    */
        case 't':  ch = '\t'; break ;  /* horizontal tab     */
        case 'b':  ch = '\b'; break ;  /* backspace          */
        case 'f':  ch = '\f'; break ;  /* formfeed           */
        case '\\': ch = '\\'; break ;  /* backslash          */
        case '(':  ch = '(' ; break ;  /* left parenthesis   */
        case ')':  ch = ')' ; break ;  /* right parenthesis  */
        case CR : /* no character - newline ignored */
          if (( ch = Getc( flptr )) == EOF )
            return pdf_scannererror( flptr , TRUE ) ;
          if ( ch != LF )
            UnGetc( ch , flptr ) ;
        case LF:
          ch = stringbuf[ --len ] ;
          break ;
        }
      }
    }
    else if ( ch == CR ) {
      if (( ch = Getc( flptr )) == EOF )
        return pdf_scannererror( flptr , TRUE ) ;
      if ( ch != LF )
        UnGetc( ch , flptr ) ;
      ch = LF ;
    }
    CHECK_EXTEND_SCANCACHE( stringbuf , len , sc , MAXPSSTRING ) ;
    stringbuf[ len++ ] = ( uint8 )ch ;
  }
  return pdf_push_scanbuf_as_obj(sc, flptr, len);
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scandict( SCANCONTEXT *sc , FILELIST **flptr, Bool do_fileoffset )
{
  int32 ch ;
  int32 len , newlen ;
  OBJECT pdfobj = OBJECT_NOTVM_NOTHING ;
  OBJECT *dict ;
  STACK *stack ;
  Bool mustscanR = FALSE ;
  PDFCONTEXT *pdfc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  HQASSERT( sc , "sc NULL in pdf_scandict" ) ;

  pdfc = sc->pdfc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( flptr && *flptr, "flptr NULL in pdf_scandict" ) ;

  stack = sc->pdfstack ;

  len = 0 ;
  for (;;) {

    /* Skip white space (and comments) */
    do {
      /* If we reach the end of this stream then we may, if its a contents
       * stream, continue onto the next (contents) stream; if not then abort
       * with a (syntax) error. If there are no more contents streams then
       * we'll behave as Adobe and allow the unterminated dict, unless
       * we're being strict (in which case we'll report a sytex error).
       */
      while (( ch = Getc( *flptr )) == EOF ) {
        if ( ! sc->scancontents || ! pdf_next_content( pdfc , flptr ))
          return pdf_scannererror( *flptr ,
                                   ! sc->scancontents || ixc->strictpdf ) ;
        if ( *flptr == NULL )
          return ( ixc->strictpdf ? error_handler( SYNTAXERROR ) : TRUE ) ;
      }

      if ( ch == '%' ) {
        do {
          /* Skip comments, with same handle-eof code as above - making sure
           * that we end the comment if we do go onto a new stream
           */
          if (( ch = Getc( *flptr )) == EOF ) {
            do {
              if ( ! sc->scancontents || ! pdf_next_content( pdfc , flptr ))
                return pdf_scannererror( *flptr ,
                                         ! sc->scancontents || ixc->strictpdf ) ;
              if ( *flptr == NULL )
                return ( ixc->strictpdf ? error_handler( SYNTAXERROR ) : TRUE ) ;
            } while (( ch = Getc( *flptr )) == EOF ) ;
            break ; /* End comment since using next stream */
          }
        } while ( ! IsEndOfLine( ch )) ;
      }
    } while ( IsWhiteSpace( ch )) ;

    /* Check for end of dictionary. */
    if ( ch == '>' ) {
      if (( ch = Getc( *flptr )) !=  '>' )
        return error_handler( SYNTAXERROR ) ;
      break ;
    }

    /* Put back character to read next object. */
    UnGetc( ch , *flptr ) ;

    /* Check we haven't got too large. */
    if ( len > 2 * MAXPSDICT )
      return error_handler( LIMITCHECK ) ;

    /* Scan next argument. */
    if ( ! pdf_scanobject( sc , flptr, do_fileoffset ))
      return FALSE ;

    /* Check the key is a name. */
    if (( len & 1 ) == 0 ) {
      OBJECT *key = theITop( stack ) ;
      if ( mustscanR ) {
        if ( oType(*key) != OINDIRECT )
          return error_handler( TYPECHECK ) ;
        mustscanR = FALSE ;
        --len ;
      }
      else if ( oType(*key) != ONAME ) {
        if ( oType(*key) != OINTEGER )
          return error_handler( TYPECHECK ) ;
        /* Got an integer as a key; therefore must be an indirect reference. */
        mustscanR = TRUE ;
        --len ;
      }
    }
    ++len ;
  }

  /* Check we haven't got too large. */
  if ( len > 2 * MAXPSDICT )
    return error_handler( LIMITCHECK ) ;

  /* Add either 5% extra key-value pairs (or an even number just
   * below) or MIN_EXTRA_DPAIRS, whichever is greater, to avoid
   * expanding the dictionary later on. */
  newlen = min( MAXPSDICT , len / 2 + max( len/10, MIN_EXTRA_DPAIRS ));

  /* Didn't get indirect reference so badly formed dictionary. */
  if ( mustscanR )
    return error_handler( TYPECHECK ) ;

  /* Check we've got correct number of key values pairs. */
  if (( len & 1 ) != 0 )
    return error_handler( UNDEFINEDRESULT ) ;

  /* Allocate memory for object and initialise. */
  dict = PDF_ALLOCOBJECT( pdfxc, (int32)NDICTOBJECTS(newlen) );
  if ( ! dict )
    return error_handler( VMERROR ) ;

  init_dictionary(&pdfobj, newlen, UNLIMITED,
                  dict, ISNOTVMDICTMARK(pdfxc->savelevel)) ;

  /* Setup object. */
  while ( len > 0 ) {
    OBJECT *key , *val ;
    val = theITop( stack ) ;
    key = ( & val[ -1 ] ) ;
    if ( ! fastIStackAccess( stack ))
      key = stackindex( 1 , stack ) ;

    if ( ! pdf_fast_insert_hash( pdfc , & pdfobj , key , val )) {
      HQFAIL( "should never fail" ) ;
      return FALSE ;
    }
    npop( 2 , stack ) ;
    len -= 2 ;
  }

  return push( & pdfobj , stack ) ;
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scanhexstring( SCANCONTEXT *sc , FILELIST *flptr )
{
  int32 ch ;
  int32 rh ;
  int32 len;
  int32 hexcount ;
  int32 hexvalue = 0 ; /* Initialise to keep compiler warnings free */
  uint8 *stringbuf ;

  HQASSERT( sc , "sc NULL in pdf_scanhexstring" ) ;
  HQASSERT( flptr , "flptr NULL in pdf_scanhexstring" ) ;

  stringbuf = sc->scanbuf ;

  len = 0 ;
  hexcount = 0 ;
  for (;;) {
    if (( ch = Getc( flptr )) == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
    rh = char_to_hex_nibble[ ch ] ;
    if ( rh >= 0 ) {
      if ( hexcount == 0 ) {
        CHECK_EXTEND_SCANCACHE( stringbuf , len , sc , MAXPSSTRING ) ;
        hexcount = 1 ;
        hexvalue = ( rh << 4 ) ;
      }
      else {
        hexcount = 0 ;
        stringbuf[ len++ ] = ( uint8 )( hexvalue | rh ) ;
      }
    }
    else if ( ch == '>' ) {
      if ( hexcount != 0 )
        stringbuf[ len++ ] = ( uint8 )hexvalue ;
      break ;
    }
    else if ( ! IsWhiteSpace( ch ))
      return error_handler( SYNTAXERROR ) ;
  }
  return pdf_push_scanbuf_as_obj(sc, flptr, len);
}

/* ---------------------------------------------------------------------- */
static Bool pdf_scanarray( SCANCONTEXT *sc , FILELIST **flptr ,
                            int32 arrayterminator )
{
  int32 ch ;
  int32 len ;
  OBJECT *array ;
  OBJECT pdfobj = OBJECT_NOTVM_NOTHING ;
  STACK *stack ;
  PDFCONTEXT *pdfc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  int32 scanningproc ;

  HQASSERT( sc , "sc NULL in pdf_scanarray" ) ;

  pdfc = sc->pdfc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;


  HQASSERT( flptr && *flptr , "flptr NULL in pdf_scanarray" ) ;
  HQASSERT( arrayterminator == ']' || arrayterminator == '}' ,
            "arrayterminator must be either ']' or '}' in pdf_scanarray" ) ;

  stack = sc->pdfstack ;
  scanningproc = sc->procedure ;
  sc->procedure = ( arrayterminator == '}' ) ;
  len = 0 ;
  for (;;) {

    /* Skip white space (and comments) */
    do {
      /* If we reach the end of this stream then we may, if its a contents
       * stream, continue onto the next (contents) stream; if not then abort
       * with a (syntax) error. If there are no more contents streams then
       * we'll behave as Adobe and allow the unterminated array, unless
       * we're being strict (in which case we'll report a sytex error).
       */
      while (( ch = Getc( *flptr )) == EOF ) {
        if ( ! sc->scancontents || ! pdf_next_content( pdfc , flptr ))
          return pdf_scannererror( *flptr ,
                                   ! sc->scancontents || ixc->strictpdf ) ;
        if ( *flptr == NULL )
          return ( ixc->strictpdf ? error_handler( SYNTAXERROR ) : TRUE ) ;
      }

      if ( ch == '%' ) {
        do {
          /* Skip comments, with same handle-eof code as above - making sure
           * that we end the comment if we do go onto a new stream
           */
          if (( ch = Getc( *flptr )) == EOF ) {
            do {
              if ( ! sc->scancontents || ! pdf_next_content( pdfc , flptr ))
                return pdf_scannererror( *flptr ,
                                         ! sc->scancontents || ixc->strictpdf ) ;
              if ( *flptr == NULL )
                return ( ixc->strictpdf ? error_handler( SYNTAXERROR ) : TRUE ) ;
            } while (( ch = Getc( *flptr )) == EOF ) ;
            break ; /* End comment since using next stream */
          }
        } while ( ! IsEndOfLine( ch )) ;
      }
    } while ( IsWhiteSpace( ch )) ;

    /* Check for end of array. */
    if ( ch == arrayterminator )
      break ;

    /* Put back character to read next object. */
    UnGetc( ch , *flptr ) ;

    /* Scan next argument. */
    if ( ! pdf_scanobject( sc , flptr, FALSE ))
      return FALSE ;

    {
      OBJECT *val = theITop( stack ) ;
      if ( oType(*val) == OINDIRECT )
        len -= 2 ;
    }

    ++len ;
  }

  theTags( pdfobj ) = OARRAY | UNLIMITED ;
  theTags( pdfobj ) |= ( arrayterminator == ']' ? LITERAL : EXECUTABLE ) ;

  /* Check we haven't got too large. */
  if ( len > MAXPSARRAY ) {
    OBJECT *astore ;
    array = PDF_ALLOCOBJECT( pdfxc, len + 2 ) ;
    if ( ! array )
      return error_handler( VMERROR ) ;
    /* Make trampoline */
    theTags( *array ) = OINTEGER | UNLIMITED ;
    theLen( *array ) = 0 ;
    oInteger( *array ) = len ;
    ++array ;
    theTags( *array ) = OARRAY | UNLIMITED ;
    theLen( *array ) = 0 ;
    oArray( *array ) = array+1 ;
    ++array ;
    /* Populate array */
    for ( astore = array + (len-1) ; astore >= array ; --astore ) {
      OBJECT *val = theITop( stack ) ;
      OCopy(*astore, *val) ;
      pop( stack ) ;
    }
    /* Make extended object - convert OARRAY into OEXTENDED, keeping flags */
    theTags( pdfobj ) ^= OARRAY ^ OEXTENDED ;
    array -= 2 ;
    len = OLONGARRAY ;

  } else if ( len > 0 ) {
    OBJECT *astore ;
    array = PDF_ALLOCOBJECT( pdfxc, len ) ;
    if ( ! array )
      return error_handler( VMERROR ) ;
    for ( astore = array + (len-1) ; astore >= array ; --astore ) {
      OBJECT *val = theITop( stack ) ;
      OCopy(*astore, *val) ;
      pop( stack ) ;
    }

  } else
    array = NULL ;

  theLen( pdfobj ) = ( uint16 )len ;
  oArray( pdfobj ) = array ;

  sc->procedure = scanningproc ;

  return push( & pdfobj , stack ) ;
}

/* ---------------------------------------------------------------------- */
static Bool values_equal(int32 intvalue, SYSTEMVALUE fltvalue, int32 intflt, int32 checkval)
{
  if (intflt) {
    if (intvalue == checkval)
      return TRUE ;
    else
      return error_handler(UNDEFINEDRESOURCE) ;
  } else {
    if (fltvalue == (SYSTEMVALUE)checkval)
      return TRUE ;
    else
      return error_handler(UNDEFINEDRESOURCE) ;
  }
  /* NOT REACHED */
}

/* ---------------------------------------------------------------------- */
Bool pdf_readdata( FILELIST *flptr , uint8 *lineptr , uint8 *lineend )
{
  int32 ch ;

  HQASSERT( flptr   , "flptr   NULL in pdf_readdata" ) ;
  HQASSERT( lineptr , "lineptr NULL in pdf_readdata" ) ;
  HQASSERT( lineend , "lineend NULL in pdf_readdata" ) ;

  while ( lineptr < lineend ) {
    ch = Getc( flptr ) ;
    if ( ch == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
    (*lineptr++) = ( uint8 )ch ;
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
** Reads in a line of text, but if a '<' is encountered, stops
there. It can return EOF.
*/
int32 pdf_readdata_delimited( FILELIST *flptr , uint8 *lineptr , uint8 *lineend )
{
  int32 ch ;
  uint8 *linebuf ;

  HQASSERT( lineptr , "lineptr NULL in pdf_readline" ) ;
  HQASSERT( lineend , "lineend NULL in pdf_readline" ) ;
  HQASSERT( flptr   , "flptr   NULL in pdf_readline" ) ;

  linebuf = lineptr ;
  for (;;) {
    ch = Getc( flptr ) ;
    if ( ch == EOF )
      return ( CAST_PTRDIFFT_TO_INT32(lineptr - linebuf) ) ;
    if ( ch == LF || ch == CR ) {
      if ( ch == CR ) {
        ch = Getc( flptr ) ;
        if ( ch == EOF )
          return ( CAST_PTRDIFFT_TO_INT32(lineptr - linebuf) ) ;
        if ( ch != LF )
          UnGetc( ch , flptr ) ;
      }
      return ( CAST_PTRDIFFT_TO_INT32(lineptr - linebuf) ) ;
    }
    if (ch == '<' && lineptr != linebuf) {
      UnGetc( ch , flptr ) ;  /* Don't consume the '<' */
      return ( CAST_PTRDIFFT_TO_INT32(lineptr - linebuf) ) ;
    }
    if ( lineptr == lineend )
      return EOF ;
    (*lineptr++) = ( uint8 )ch ;
  }
  /* NOT REACHED */
}


/* Consume consequtive space characters. Returns TRUE if at least one
   space character was found, otherwise FALSE. Raises an error if no
   whitespace was found at all. */
Bool pdf_scan_xref_required_whitespace( FILELIST *flptr )
{
  int32 ch ;
  Bool found_one_space = FALSE ;

  for (;;) {
    if ( (ch = Getc( flptr )) == EOF )
      break ;

    if (! IsWhiteSpace((uint8)ch)) {
      UnGetc(ch, flptr) ;
      break ;
    }
    found_one_space = TRUE ;
  }
  if (found_one_space) {
    return TRUE ;
  } else {
    return pdf_scannererror( flptr , TRUE ) ;
  }
}

/* Attempt to consume the specified string including any preceeding
   white space. Does not raise an error. */
Bool pdf_scan_xref_optional_string( FILELIST *flptr, uint8 *token, int32 token_len )
{
  int32 ch ;
  uint8 *ptr = token ;

  HQASSERT(flptr, "flptr is NULL" ) ;
  HQASSERT(token, "token is NULL" ) ;

  /* Remove preceeding white space. */
  for (;;) {
    if ( (ch = Getc( flptr )) == EOF )
      return FALSE ;
    if (! IsWhiteSpace((uint8)ch))
      break ;
  }

  for (;;) {
    if (*ptr == (uint8)ch) {
      token_len-- ;
      if (token_len > 0) {
        if ( (ch = Getc( flptr )) == EOF )
          return FALSE ;
        ptr++ ;
      } else {
        return TRUE ;
      }
    } else {
      UnGetc( ch , flptr ) ;
      return FALSE ;
    }
  }
  /* NOT REACHED */
}

/* Consume a required integer including any preceeding white
   space. Raises an error if unable to read an integer. */
Bool pdf_scan_xref_required_integer( FILELIST *flptr, int32 *integer )
{
  int32 ch ;
  int32 n ;
  int32 i = 0 ;

  HQASSERT(flptr, "flptr is NULL" ) ;
  HQASSERT(integer, "integer is NULL" ) ;

  /* Remove preceeding white space. */
  for (;;) {
    if ( (ch = Getc( flptr )) == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
    if (! IsWhiteSpace((uint8)ch))
      break ;
  }

  for (n=0; n < 10; n++) {
    if (! isdigit( ch )) {
      UnGetc( ch , flptr ) ;
      break ;
    }
    ch = ch - '0' ;

    if ( n < 9 ) {
      i = 10 * i + ch ;
    } else {
      int32 itmp ;
      Hq32x2 tmp , tmp1 , tmp2 , tmp4 , tmp8 , tmpA ;

      Hq32x2FromInt32( & tmp1 , i ) ;
      Hq32x2Add( & tmp2 , & tmp1 , & tmp1 ) ;
      Hq32x2Add( & tmp4 , & tmp2 , & tmp2 ) ;
      Hq32x2Add( & tmp8 , & tmp4 , & tmp4 ) ;
      Hq32x2Add( & tmpA , & tmp8 , & tmp2 ) ;
      Hq32x2AddInt32( & tmp , & tmpA , ch ) ;
      if ( ! Hq32x2ToInt32( & tmp , & itmp ))
        return error_handler( RANGECHECK ) ;
      i = itmp ;
    }

    if ( (ch = Getc( flptr )) == EOF )
      break ;
  }

  /* Must be at least one numeric. */
  if (n == 0)
    return pdf_scannererror( flptr , TRUE ) ;

  (*integer) = i ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
Bool pdf_scan_next_integer_with_bytescount(FILELIST *flptr, int32 *intvalue,
                                           SYSTEMVALUE *fltvalue, int32 *intflt,
                                           int32 *bytescount)
{
  int32 ch, nleading, ileading ;
  SYSTEMVALUE fleading ;

#define ScanIntGetc(_f) (++(*bytescount), Getc(_f))
#define ScanIntUnGetc(_c, _f) (UnGetc(_c, _f), --(*bytescount))

  *bytescount = 0;

  HQASSERT( flptr , "flptr is NULL" ) ;

  /* Skip white space (and comments) */
  do {
    if (( ch = ScanIntGetc( flptr )) == EOF )
      return pdf_scannererror( flptr , TRUE ) ;
    if ( ch == '%' ) {
      do {
        if (( ch = ScanIntGetc( flptr )) == EOF )
          return pdf_scannererror( flptr , TRUE ) ;
      } while ( ! IsEndOfLine( ch )) ;
    }
  } while ( IsWhiteSpace( ch )) ;

  nleading = 0 ;
  ileading = 0 ;
  fleading = 0.0 ;
  /* Scan the m part of an integer. */
  for (;;) {
    if ( ! isdigit( ch ))
      break ;
    ch = ( ch - '0' ) ;
    ++nleading ;
    if ( nleading < 10 )
      ileading = 10 * ileading + ch ;
    else if ( nleading == 10 )
      fleading = 10.0 * ( SYSTEMVALUE )ileading + ch ;
    else
      return error_handler( SYNTAXERROR ) ;
    ch = ScanIntGetc( flptr );

    if (ch == EOF)
      break;
  }

  if ( ch == EOF )
    return pdf_scannererror( flptr , TRUE ) ;

  /* Bad integer. */
  if ( nleading == 0 )
    return error_handler( SYNTAXERROR ) ;

  if ( ! IsEndMarkerPDF( ch ))
    return error_handler( SYNTAXERROR ) ;
  if ( IsEndOfLine( ch )) {

    if ( ch == CR ) {
      if (( ch = ScanIntGetc( flptr )) != EOF ) {
        if ( ch != LF )
          ScanIntUnGetc( ch , flptr ) ;
      }
    }
  }
  else if ( ! IsWhiteSpace( ch )) {
    ScanIntUnGetc( ch , flptr ) ;
  }

  if ( nleading < 10 ) {
    *intvalue = ileading ;
    if (intflt)
      *intflt = TRUE;
  }
  else {
    if (fltvalue) {
      *fltvalue = fleading ;
      if (intflt)
        *intflt = FALSE;
    } else {
      return error_handler( RANGECHECK ) ;
    }
  }

  return TRUE;
}

Bool pdf_scan_next_integer(FILELIST *flptr, int32 *intvalue,
                           SYSTEMVALUE *fltvalue, int32 *intflt)
{
  int32 bytescount = 0;
  return pdf_scan_next_integer_with_bytescount(flptr, intvalue,
                                               fltvalue, intflt, &bytescount);
}

/* ---------------------------------------------------------------------- */
int32 pdf_scan_integer_allowing_spaces( uint8 *lineptr , uint8 *lineend , int32 *integer )
{
  HQASSERT( lineptr , "lineptr is NULL" ) ;
  HQASSERT( lineend , "lineend is NULL" ) ;
  HQASSERT( integer , "integer is NULL" ) ;
  HQASSERT( lineptr <= lineend , "lineptr > lineend" ) ;

  /* Skip over leading/trailing whitespace */
  while ( lineptr < lineend && IsWhiteSpace(( int32 )*lineptr ))
    ++lineptr ;
  while ( lineptr < lineend && IsWhiteSpace(( int32 )*( lineend - 1 )))
    --lineend ;

  /* Now get the integer */
  return pdf_scan_integer( lineptr, lineend, integer ) ;
}

/* As long as the accumulator is less than this value then the code can keep to
 * 32bit math only switching to 64bit on overflow */
#define MAXINT_DIV10ISH ((MAXINT32 - 9)/10)

/* Scan integers that may be larger than 2^32 - 1.  It is the responsibility of
 * the caller to ensure any limits on the input or the resulting value */
Bool pdf_scan_large_integer( uint8 *lineptr , uint8 *lineend , Hq32x2 *integer )
{
  int32 c ;
  int32 i = 0 ;

  HQASSERT( lineptr , "lineptr NULL" ) ;
  HQASSERT( lineend , "lineend NULL" ) ;
  HQASSERT( integer , "integer NULL" ) ;
  HQASSERT( lineptr <= lineend , "lineptr > lineend" ) ;

  /* Skip over leading/trailing whitespace */
  while ( lineptr < lineend && IsWhiteSpace(( int32 )*lineptr ))
    ++lineptr ;
  while ( lineptr < lineend && IsWhiteSpace(( int32 )*( lineend - 1 )))
    --lineend ;

  if ( lineend <= lineptr )
    return error_handler( UNDEFINEDRESULT ) ;

  HQASSERT( ! IsWhiteSpace(( int32 )*lineptr) ,
            "whitespace found before integer" ) ;
  HQASSERT( ! IsWhiteSpace(( int32 )*(lineend - 1)) ,
            "whitespace found after integer" ) ;

  Hq32x2FromInt32( integer , 0 ) ;

  while ( lineptr < lineend ) {
    c = ( int32 )(*lineptr++) ;
    if ( ! isdigit( c ))
      return error_handler( SYNTAXERROR ) ;
    c = c - '0' ;
    if ( i < MAXINT_DIV10ISH ) {
      i = 10 * i + c ;
    } else {
      Hq32x2 tmp2 , tmp4 , tmp8 , tmpA ;
      if (Hq32x2IsZero(integer)) {
        Hq32x2FromInt32( integer , i ) ;
      }
      Hq32x2Add( & tmp2 , integer , integer ) ;
      Hq32x2Add( & tmp4 , & tmp2 , & tmp2 ) ;
      Hq32x2Add( & tmp8 , & tmp4 , & tmp4 ) ;
      Hq32x2Add( & tmpA , & tmp8 , & tmp2 ) ;
      Hq32x2AddInt32( integer , & tmpA , c ) ;
    }
  }

  if (Hq32x2IsZero(integer)) {
    Hq32x2FromInt32( integer, i );
  }

  return TRUE ;
}

Bool pdf_scan_integer( uint8 *lineptr , uint8 *lineend , int32 *integer )
{
  int32 c ;
  int32 n = 0 ;
  int32 i = 0 ;

  HQASSERT( lineptr , "lineptr NULL in pdf_scan_integer" ) ;
  HQASSERT( lineend , "lineend NULL in pdf_scan_integer" ) ;
  HQASSERT( integer , "integer NULL in pdf_scan_integer" ) ;
  HQASSERT( lineptr <= lineend , "lineptr > lineend in pdf_scan_integer" ) ;

  if ( lineend <= lineptr )
    return error_handler( UNDEFINEDRESULT ) ;

  HQASSERT( ! IsWhiteSpace(( int32 )*lineptr) ,
            "whitespace found before integer in pdf_scan_integer" ) ;
  HQASSERT( ! IsWhiteSpace(( int32 )*(lineend - 1)) ,
            "whitespace found after integer in pdf_scan_integer" ) ;

  if ( lineend - lineptr > 10 )
    return error_handler( UNDEFINEDRESULT ) ;

  while ( lineptr < lineend ) {
    c = ( int32 )(*lineptr++) ;
    if ( ! isdigit( c ))
      return error_handler( SYNTAXERROR ) ;
    c = c - '0' ;
    ++n ;
    if ( n < 10 )
      i = 10 * i + c ;
    else {
      int32 itmp ;
      Hq32x2 tmp , tmp1 , tmp2 , tmp4 , tmp8 , tmpA ;
      HQASSERT( n == 10 , "can't scan int32 larger than 10 digits" ) ;
      Hq32x2FromInt32( & tmp1 , i ) ;
      Hq32x2Add( & tmp2 , & tmp1 , & tmp1 ) ;
      Hq32x2Add( & tmp4 , & tmp2 , & tmp2 ) ;
      Hq32x2Add( & tmp8 , & tmp4 , & tmp4 ) ;
      Hq32x2Add( & tmpA , & tmp8 , & tmp2 ) ;
      Hq32x2AddInt32( & tmp , & tmpA , c ) ;
      if ( ! Hq32x2ToInt32( & tmp , & itmp ))
        return error_handler( RANGECHECK ) ;
      i = itmp ;
    }
  }
  (*integer) = i ;
  return TRUE ;
}

/* Check for an error in the given FILELIST, from which the scanner has been
 * reading. If there was no error there, either return TRUE or raise a
 * syntax error, depending on the value of syntaxerror.
 */

static Bool pdf_scannererror( FILELIST *flptr , int32 syntaxerror )
{
  if ( isIIOError( flptr ))
    return (*theIFileLastError( flptr ))( flptr ) ;
  else
    return syntaxerror ? error_handler( SYNTAXERROR ) : TRUE ;
}

/* Log stripped */
