/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:epsexec.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS execution operators
 */

#include "core.h"
#include "pscontext.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "namedef_.h"

#include "constant.h"
#include "fileops.h"
#include "stacks.h"
#include "control.h"
#include "params.h"

#include "dicthash.h"

static Bool eps_execop(ps_context_t *context, int32 opnumber) ;
static Bool eps_setupsubfiledict( OBJECT *dict , int32 pslen ) ;
static Bool eps_setuprsddict( OBJECT *dict , OBJECT *realsource ) ;
static Bool eps_readheader( FILELIST *uflptr , int32 *offset ,
                             int32 *pslen ) ;
static Bool eps_makefilter( uint8 *name , OBJECT *fileobj ) ;

/* ---------------------------------------------------------------------- */
Bool epsexec_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  int32 roffset = -1 ;
  int32 pslen = 0 ;
  Bool encapsulated ;
  OBJECT *o1 ;
  OBJECT *o2 ;
  FILELIST *uflptr ;
  OBJECT args = OBJECT_NOTVM_NOTHING ;
  OBJECT fileobj = OBJECT_NOTVM_NOTHING ;
  OBJECT realsource = OBJECT_NOTVM_NOTHING ;
  OBJECT basesource = OBJECT_NOTVM_NOTHING ;
  corecontext_t *corecontext = pscontext->corecontext ;

  enum {
    eps_Encapsulated, eps_dummy
  } ;
  static NAMETYPEMATCH thematch[eps_dummy + 1] = {
    { NAME_Encapsulated  | OOPTIONAL , 1, { OBOOLEAN }},
    DUMMY_END_MATCH
  } ;

  /* Arguments: -file- << ... >> epsexec */

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  type = oType(*o1) ;
  if ( type != OFILE )
    return error_handler( TYPECHECK ) ;

  type = oType(*o2) ;
  if ( type != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  uflptr = oFile(*o1) ;
  if ( ! isIOpenFileFilter( o1 , uflptr ) ||
       ! isIInputFile( uflptr ) ||
       isIEof( uflptr ))
    return error_handler( IOERROR ) ;

  if ( corecontext->glallocmode )
    if ( illegalLocalIntoGlobal(o1, corecontext) )
      return error_handler( INVALIDACCESS ) ;

  if ( ! dictmatch( o2 , thematch ))
    return FALSE ;

  /* If /Encapsulated is true, showpage is suppressed at the end of each page.
   * If missing, then this is assumed to be an encapsulated page.
   */

  encapsulated = TRUE ;
  if ( thematch[eps_Encapsulated].result )
    encapsulated = oBool(*thematch[eps_Encapsulated].result) ;

  /* Make an RSD on top of the whole thing, so that if this PCEPS is actually
   * a DCS2 single file, the DCS procset has something to seek around in.
   */

  OCopy( basesource , *o1 ) ;

  if ( ! push( o1 , & operandstack ) ||
       ! eps_setuprsddict( & args , & basesource ) ||
       ! push(&args, &operandstack) ||
       ! eps_makefilter(( uint8 * )"ReusableStreamDecode" , & fileobj ))
    return FALSE ;

  OCopy( realsource , fileobj ) ;

  uflptr = oFile(fileobj) ;
  if ( ! isIOpenFileFilter( & fileobj , uflptr ) ||
       ! isIInputFile( uflptr ) ||
       isIEof( uflptr ))
    return error_handler( IOERROR ) ;

  if ( ! eps_readheader( uflptr , & roffset , & pslen ))
    return FALSE ;

  /* roffset contains the relative offset from where we start reading the (E)PS.
   * Therefore skip over that many bytes.
   */

  HQASSERT( roffset >= 0 ,  "roffset should be +ve" ) ;
  while ( roffset > 0 ) {
    int32 bytes = 0 ;
    uint8 *ptr = NULL ;

    if ( ! GetFileBuff( uflptr , roffset , & ptr , & bytes ))
      return error_handler( IOERROR ) ;

    HQASSERT( bytes > 0 , "GetFileBuff should return +ve bytes" ) ;
    HQASSERT( bytes <= roffset , "GetFileBuff should return max roffset bytes" ) ;
    roffset -= bytes ;
  }

  /* Set-up a SubFileDecode filter to create a "window" on the file
   * containing just the PS to be interpreted.
   */

  if ( ! push(&realsource, &operandstack) ||
       ! eps_setupsubfiledict( & args , pslen ) ||
       ! push(&args, &operandstack) ||
       ! eps_makefilter(( uint8 * )"SubFileDecode" , & fileobj ))
    return FALSE ;

  /* Add an RSD on top to make sure it's seekable. */

  if ( ! push( & fileobj , & operandstack ) ||
       ! eps_setuprsddict( & args , & realsource ) ||
       ! push(&args, &operandstack) ||
       ! eps_makefilter(( uint8 * )"ReusableStreamDecode" , & fileobj ))
    return FALSE ;

  npop( 2 , & operandstack ) ;

  if ( ! encapsulated ) {
    /* Supress showpage since we add our own. */
    OBJECT value = OBJECT_NOTVM_NOTHING ;

    theTags( value ) = OARRAY | EXECUTABLE | UNLIMITED ;
    theLen( value ) = ( uint16 )0 ;
    oArray(value) = NULL ;

    if ( ! fast_insert_hash_name( & userdict , NAME_showpage , & value ))
      return FALSE ;
  }

  /* Call interpreter on the PS. */

  currfileCache = NULL ;
  if ( ! push( & fileobj , & executionstack ))
    return FALSE ;

  if ( ! interpreter( 1 , NULL ))
    return FALSE;

  return (encapsulated || eps_execop(pscontext, NAME_showpage)) ;
}

/* ---------------------------------------------------------------------- */
static Bool eps_execop(ps_context_t *pscontext, int32 opnumber)
{
  OPERATOR *op ;
  OBJECT tmp_errobject = OBJECT_NOTVM_NOTHING ;

  HQASSERT( opnumber >= 0 , "ps_execop: invalid opnumber" ) ;
  op = & system_ops[ opnumber ] ;

  /* Setup the error object for this operator. */
  OCopy( tmp_errobject , errobject ) ;
  theTags( errobject ) = OOPERATOR | EXECUTABLE | UNLIMITED ;
  oOp(errobject) = op ;

  if ( ! (op->opcall)(pscontext))
    return FALSE ;

  /* Reset the error object back to whatever it was. */
  OCopy( errobject , tmp_errobject ) ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
#define EPS_HEADER 0xc5d0d3c6
#define EPS_HEADER_SIZE 30
#define EPS_IGNOREABLE_CHECKSUM 0xffff

static Bool eps_readheader( FILELIST *uflptr , int32 *roffset , int32 *pslen )
{
  int32 ch , i ;
  int32 offset ;
  uint32 header ;
  uint8 pchecksum ;
  uint16 checksum ;

  pchecksum = 0 ;

  /* Read header (Predefined 32 bit word, all the rest are low byte
   * first). */
  header = 0 ;
  for ( i = 24 ; i >= 0 ; i -= 8 ) {
    if (( ch = Getc( uflptr )) == EOF )
      return error_handler( IOERROR ) ;
    header |= ch << i ;
    pchecksum ^= ( uint8 ) ch ;
  }
  if ( header != EPS_HEADER )
    return error_handler( UNDEFINED ) ;

  /* Read PS byte offset. */
  offset = 0 ;
  for ( i = 0 ; i <= 24 ; i += 8 ) {
    if (( ch = Getc( uflptr )) == EOF )
      return error_handler( IOERROR ) ;
    offset |= ch << i ;
    pchecksum ^= ( uint8 ) ch ;
  }
  if ( offset < EPS_HEADER_SIZE )
    /* offset points somewhere in the header. */
    return error_handler( SYNTAXERROR ) ;

  /* Read length of the PS section. */
  *pslen = 0 ;
  for ( i = 0 ; i <= 24 ; i += 8 ) {
    if (( ch = Getc( uflptr )) == EOF )
      return error_handler( IOERROR ) ;
    *pslen |= ch << i ;
    pchecksum ^= ( uint8 ) ch ;
  }

  /* The next 16 bytes are for the TIFF and Metafile screen rep offset
   * and length. Only need to read them to xor into pchecksum. */
  for ( i = 0 ; i < 16 ; ++i ) {
    if (( ch = Getc( uflptr )) == EOF )
      return error_handler( IOERROR ) ;
    pchecksum ^= ( uint8 ) ch ;
  }

  /* Read the checksum. */
  checksum = 0 ;
  for ( i = 0 ; i <= 8 ; i += 8 ) {
    if (( ch = Getc( uflptr )) == EOF )
      return error_handler( IOERROR ) ;
    checksum |= ch << i ;
  }

  /* Not yet found a test job which tests this checksum code. */
  HQTRACE( checksum != EPS_IGNOREABLE_CHECKSUM ,
           ( "Found a PC EPS file with a checksum! Email markj" )) ;

  /* Compare it with pchecksum (if its not ignoreable). */
  if ( checksum != EPS_IGNOREABLE_CHECKSUM &&
       ( ( uint8 ) checksum ) != pchecksum )
    return error_handler( SYNTAXERROR ) ;

  (*roffset) = offset - EPS_HEADER_SIZE ;

  return TRUE ;
}

static Bool eps_makefilter( uint8 *name , OBJECT *fileobj )
{
  FILELIST *nflptr = filter_standard_find( name , strlen_int32(( char * )name )) ;

  HQASSERT( nflptr , "filter isn't in the list of standard filters!_" ) ;
  HQASSERT( isIFilter( nflptr ) , "not a filter" ) ;

  if ( ! filter_create_object(nflptr, fileobj, NULL, &operandstack) )
    return FALSE ;

  /* Make filter executable */
  theTags(*fileobj) = ( uint8 )(theTags(*fileobj) | EXECUTABLE) ;

  return TRUE ;
}

static Bool eps_setupsubfiledict( OBJECT *dict , int32 pslen )
{
  OBJECT theval = OBJECT_NOTVM_NOTHING ;

  if ( ! ps_dictionary(dict, 2) )
    return FALSE ;

  object_store_integer(&theval, pslen) ;

  if ( ! fast_insert_hash_name(dict, NAME_EODCount, &theval) )
    return FALSE ;

  theTags( theval ) = OSTRING | LITERAL ;
  theLen( theval ) = 0 ;
  oString(theval) = NULL ;

  return fast_insert_hash_name(dict, NAME_EODString, &theval) ;
}

static Bool eps_setuprsddict( OBJECT *dict , OBJECT *realsource  )
{
  OBJECT theval = OBJECT_NOTVM_INTEGER(2) ;

  if ( ! ps_dictionary(dict, realsource ? 2 : 1) )
    return FALSE ;

  /* PC eps files I reckon look most like sequentially accessed
   * lookup table data, for what it's worth.
   */

  if ( ! fast_insert_hash_name(dict, NAME_Intent, &theval) )
    return FALSE ;

  if ( realsource )
    return fast_insert_hash_name(dict, NAME_RealSource, realsource) ;

  return TRUE ;
}

Bool getrealsource_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *o1 ;
  OBJECT *theo ;
  FILELIST *flptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = TopStack( operandstack , ssize ) ;

  type = oType(*o1) ;
  if ( type != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*o1) ;

  if ( oType(theIParamDict(flptr)) == ODICTIONARY ) {
    theo = fast_extract_hash_name(&theIParamDict(flptr), NAME_RealSource) ;

    if ( theo )
      Copy( o1 , theo ) ;
  }

  return TRUE ;
}

/* end of file epsexec.c */

/* Log stripped */
