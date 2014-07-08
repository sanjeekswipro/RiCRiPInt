/** \file
 * \ingroup fonts
 *
 * $HopeName: SWv20!src:fontpriv.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Private font data implementation
 */

#include "core.h"
#include "coreinit.h"
#include "swctype.h"
#include "swerrors.h"
#include "swdevice.h"
#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "mps.h"
#include "mmcompat.h"
#include "gcscan.h"
#include "namedef_.h"
#include "tables.h"
#include "hqmemset.h"

#include "control.h"
#include "fcache.h"
#include "fileops.h"
#include "stacks.h"
#include "dicthash.h"
#include "miscops.h"
#include "typeops.h"

#define FONTOPS 9

/* --- Internal Functions --- */

static Bool fontprivatedict_(ps_context_t *pscontext);
static Bool readeerom_(ps_context_t *pscontext);
static Bool readkey_(ps_context_t *pscontext);
static Bool fget_(ps_context_t *pscontext);
static Bool enter_(ps_context_t *pscontext);
static Bool excheck_(ps_context_t *pscontext);
static Bool checkfont_(ps_context_t *pscontext);
static Bool processstring_(ps_context_t *pscontext);
static Bool initwrite_(ps_context_t *pscontext);
static Bool writedata_(ps_context_t *pscontext);

OPFUNCTION fsdopptr[FONTOPS + 1] = {
  readeerom_ ,
  readkey_ ,
  fget_ ,
  enter_ ,
  excheck_ ,
  checkfont_ ,
  processstring_ ,
  initwrite_ ,
  writedata_ ,
  NULL
} ;

uint8 *fsdnames[ FONTOPS ] = {
  ( uint8 * )"readeerom" ,
  ( uint8 * )"readkey" ,
  ( uint8 * )"fget" ,
  ( uint8 * )"enter" ,
  ( uint8 * )"excheck" ,
  ( uint8 * )"checkfont" ,
  ( uint8 * )"processstring" ,
  ( uint8 * )"initwrite" ,
  ( uint8 * )"writedata"
  } ;

OBJECT fontprivatedict ;
static OPERATOR thefpdop ;
static OPERATOR *allthefsdops = NULL ;
static mps_root_t fontprivatedictroot;


/* scanFontPrivateDict - scanning function for fontprivatedict */
static mps_res_t MPS_CALL scanFontPrivateDict(mps_ss_t ss, void *p, size_t s)
{
  UNUSED_PARAM( void *, p ); UNUSED_PARAM( size_t, s );
  return ps_scan_field( ss, &fontprivatedict );
}


static void init_C_globals_fontpriv(void)
{
  OBJECT oinit = OBJECT_NOTVM_NOTHING ;
  OPERATOR opinit = { 0 } ;

  fontprivatedict = oinit ;
  thefpdop = opinit ;
  allthefsdops = NULL ;
  fontprivatedictroot = NULL ;
}

/* ----------------------------------------------------------------------------
   function:            initfontprivatedict() author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Initialises the contents of fontprivatedict, setting up fontprivatedict
   in userdict.  Makes it a GC root as well.

---------------------------------------------------------------------------- */
static Bool ps_fontprivate_swstart(struct SWSTART *params)
{
  register int32 loop ;
  OBJECT key = OBJECT_NOTVM_NOTHING, object = OBJECT_NOTVM_NOTHING ;
  Bool ok ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  allthefsdops = mm_alloc_static(FONTOPS * sizeof(OPERATOR)) ;
  if ( allthefsdops == NULL )
    return FALSE ;

  /* Create the dictionary. */
  if ( ! ps_dictionary(&fontprivatedict, FONTPRIVATE_SIZE) )
    return FALSE ;

  /* Insert all the operators into 'fontprivatedict'. */
  theTags( key ) = ONAME | EXECUTABLE ;
  theTags( object ) = OOPERATOR | EXECUTABLE ;
  for ( loop = 0 ; fsdopptr[ loop ] ; ++loop ) {
    theIOpName(&allthefsdops[loop]) =
      oName(key) = cachename(fsdnames[ loop ] ,
                             ( uint32 )strlen(( char * )fsdnames[ loop ])) ;
    if ( oName(key) == NULL )
      return FALSE;
    theIOpCall(&allthefsdops[loop]) = fsdopptr[ loop ] ;
    oOp(object) = &allthefsdops[loop] ;
    ok = insert_hash( & fontprivatedict , & key , & object ) ;
    HQASSERT(ok, "Font private dict insert failed") ;
  }

  /* Insert 'fontprivatedict' into 'userdict'. */
  theIOpName(&thefpdop) = oName(key) =
    cachename(STRING_AND_LENGTH("fontprivatedict")) ;
  if ( oName(key) == NULL )
    return FALSE;
  theIOpCall(&thefpdop) = fontprivatedict_ ;
  oOp(object) = &thefpdop ;
  ok = insert_hash( & userdict , & key , & object ) ;
  HQASSERT(ok, "Font private dict insert failed") ;

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &fontprivatedictroot, mm_arena, mps_rank_exact(),
                        0, scanFontPrivateDict, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}


/* finishFontPrivateDict - finish fontprivatedict */
static void ps_fontprivate_finish(void)
{
  mps_root_destroy( fontprivatedictroot );
  /* Make sure we don't use it after it's been finished. */
  theTags( fontprivatedict ) = ONULL;
}


void ps_fontprivate_C_globals(core_init_fns *fns)
{
  init_C_globals_fontpriv() ;

  fns->swstart = ps_fontprivate_swstart ;
  fns->finish = ps_fontprivate_finish ;
}


/* ----------------------------------------------------------------------------
   function:            fontprivatedict()    author:              Andrew Cave
   creation date:       22-Oct-1987          last modification:   ##-###-####
   arguments:           none .
   description:

   If passwd correct, return fontprivatedict.

---------------------------------------------------------------------------- */
static Bool fontprivatedict_(ps_context_t *pscontext)
{
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

/*  The passwd must be an integer.  */
  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  if ( oInteger(*theo) != 378066215 )
    return error_handler( INVALIDACCESS ) ;

  Copy( theo , (& fontprivatedict)) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            readeerom_()      author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Returns the byte (as an int) at the given index in the EEROM scratch array.

---------------------------------------------------------------------------- */
static Bool readeerom_(ps_context_t *pscontext)
{
  OBJECT * theo;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  theo = fast_extract_hash_name(& systemdict, NAME_statusdict);
  if (! theo)
    return error_handler(UNREGISTERED);
  if ( (oName(nnewobj) = cachename(NAME_AND_LENGTH("eescratch"))) == NULL )
    return FALSE;
  theo = fast_extract_hash(theo, &nnewobj);
  if (! theo)
    return error_handler(UNREGISTERED);
  if (! push(theo, & executionstack))
    return FALSE;
  return interpreter(1, NULL);
}

/* ----------------------------------------------------------------------------
   function:            readkey_()            author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Returns the key to check on for font protection(?).

---------------------------------------------------------------------------- */
static Bool readkey_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_integer( 1234, &operandstack );
}

/* ----------------------------------------------------------------------------
   function:            fget_()               author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Similar to get, but no permission checks, and only for dictionaries.

---------------------------------------------------------------------------- */
static Bool fget_(ps_context_t *pscontext)
{
  int32 access ;
  register OBJECT *thed ;
  register OBJECT *theo ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  thed = stackindex( 1 , & operandstack ) ;
  if ( oType(*thed) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  error_clear_newerror_context(ps_core_context(pscontext)->error);
  access = oAccess(*oDict(*thed)) ;
  SET_DICT_ACCESS(thed, UNLIMITED) ;
  theo = extract_hash( thed , theTop( operandstack )) ;
  SET_DICT_ACCESS(thed, access) ;

  if ( ! theo )
    return ( newerror ? FALSE : error_handler( UNDEFINED )) ;

  pop( & operandstack ) ;
  Copy( thed , theo ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            enter_()              author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Similar to put, but no permission checks, and only for dictionaries.

---------------------------------------------------------------------------- */
static Bool enter_(ps_context_t *pscontext)
{
  OBJECT *thed ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  thed = stackindex( 2 , & operandstack ) ;
  if ( oType(*thed) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( !insert_hash_even_if_readonly(thed ,
                                     stackindex(1, &operandstack),
                                     theTop(operandstack)) )
    return FALSE ;

  npop( 3 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            excheck_()            author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Returns a boolean indicating if the given string, procedure or file is
   executeable, (c.f. rcheck/wcheck.

---------------------------------------------------------------------------- */
static Bool excheck_(ps_context_t *pscontext)
{
  return xcheck_(pscontext) ;
}

/* ----------------------------------------------------------------------------
   function:            checkfont_()          author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Decrypts the given string from an eexec format.

---------------------------------------------------------------------------- */
static Bool checkfont_(ps_context_t *pscontext)
{
  register uint8 *str , *res, inbyte, outbyte ;
  register int32 i ;
  register uint16 state ;

  int32 len ;
  OBJECT *o1 , *o2 , *o3 , *o4 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 3 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 3 , & operandstack ) ;
  o2 = stackindex( 2 , & operandstack ) ;
  o3 = stackindex( 1 , & operandstack ) ;
  o4 = theTop( operandstack ) ;

  if (( oType(*o1) != OSTRING ) ||
      ( oType(*o2) != OINTEGER ) ||
      ( oType(*o3) != OINTEGER ) ||
      ( oType(*o4) != OSTRING ))
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanWrite(*o4) && !object_access_override(o4)) )
    return error_handler( INVALIDACCESS ) ;

  len = oInteger(*o2) ;
  if (( len > (int32) theLen(*o1)) || (( len - 4 ) > (int32) theLen(*o4))
       || (len < 4))
    return error_handler( RANGECHECK ) ;

/*  seed = oInteger(*o3) ; */
  str = oString(*o1) ;
  res = oString(*o4) ;
  theLen(*o4) = (uint16)(len - 4) ;

  state = 0xd971 ;
  i = 0 ;
  while ((--len) >= 0 ) {
    inbyte = (*str++) ;
    outbyte = DECRYPT_BYTE( inbyte , state ) ;
    if ( i >= 4 )
      (*res++) = outbyte ;

    DECRYPT_CHANGE_STATE( inbyte , state , DECRYPT_ADD , DECRYPT_MULT ) ;
    ++i ;
  }
  Copy( o1 , o4 ) ;
  npop( 3 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            processstring_()      author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Encrypts the given string into an eexec format.

---------------------------------------------------------------------------- */
static Bool processstring_(ps_context_t *pscontext)
{
  int32 len ;
  uint8 *str , *res ;
  OBJECT *o1 , *o2 , *o3 , *o4 , *o5 , *o6 ;

  register int32 seed ;
  register int32 i ;
  register uint8 inbyte , outbyte ;
  register uint16 state ;

  register uint8 *lhex_table ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 5 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 5 , & operandstack ) ;
  o2 = stackindex( 4 , & operandstack ) ;
  o3 = stackindex( 3 , & operandstack ) ;
  o4 = stackindex( 2 , & operandstack ) ;
  o5 = stackindex( 1 , & operandstack ) ;
  o6 = theTop( operandstack ) ;

  if (( oType(*o1) != OSTRING ) ||
      ( oType(*o2) != OINTEGER ) ||
      ( oType(*o3) != OINTEGER ) ||
      ( oType(*o4) != OINTEGER ) ||
      ( oType(*o5) != OSTRING ) ||
      ( oType(*o6) != OBOOLEAN ))
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanWrite(*o5) && !object_access_override(o5)) )
    return error_handler( INVALIDACCESS ) ;

  len = oInteger(*o2) ;
  if (( len > (int32) theLen(*o1)) || (( 2 * len + 8 ) > (int32) theLen(*o5)))
    return error_handler( RANGECHECK ) ;
  seed = oInteger(*o3) ;
  str = oString(*o1) ;
  res = oString(*o5) ;
  theLen(*o5) = (uint16)( 2 * len + 8 ) ;

  lhex_table = nibble_to_hex_char ;
  state = 0xd971 ;
  for ( i = 0 ; i < 4 ; ++i ) {
    inbyte = (uint8)( seed >> (( 3 - i ) * 8 )) ;
    outbyte = ENCRYPT_BYTE( inbyte , state ) ;

    (*res++) = lhex_table[ outbyte >> 4 ] ;
    (*res++) = lhex_table[ outbyte & 15 ] ;

    ENCRYPT_CHANGE_STATE( outbyte , state , DECRYPT_ADD , DECRYPT_MULT ) ;
  }
  while ((--len) >= 0 ) {
    inbyte = (*str++) ;
    outbyte = ENCRYPT_BYTE( inbyte , state ) ;

    (*res++) = lhex_table[ outbyte >> 4 ] ;
    (*res++) = lhex_table[ outbyte & 15 ] ;

    ENCRYPT_CHANGE_STATE( outbyte , state , DECRYPT_ADD , DECRYPT_MULT ) ;
  }

  Copy( o1 , o5 ) ;
  npop( 5 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            initwrite_()          author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Initialises an eexec write to the given stream, with the given seed.

---------------------------------------------------------------------------- */
static Bool initwrite_(ps_context_t *pscontext)
{
  register int32 seed ;
  register int32 i ;
  uint8 inbyte , outbyte ;
  uint16 state ;

  register uint8 *lhex_table ;
  register FILELIST *flptr ;

  OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = stackindex( 1 , & operandstack ) ;
  if ( oType(*o2) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  if ( oType(*o1) != OFILE )
    return error_handler( TYPECHECK ) ;

  seed = oInteger(*o2) ;

  flptr = oFile(*o1) ;
  if ( ! isIOpenFileFilter( o1 , flptr ))
    return error_handler( IOERROR ) ;
  if ( ! isIOutputFile( flptr ))
    return error_handler( IOERROR ) ;

  lhex_table = nibble_to_hex_char ;

  state = 0xd971 ;
  for ( i = 0 ; i < 4 ; ++i ) {
    inbyte = (uint8)( seed >> (( 3 - i ) * 8 )) ;
    outbyte = ENCRYPT_BYTE( inbyte , state ) ;

    if ( Putc( lhex_table[ outbyte >> 4 ] , flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;
    if ( Putc( lhex_table[ outbyte & 15 ] , flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

    ENCRYPT_CHANGE_STATE( outbyte , state , DECRYPT_ADD , DECRYPT_MULT ) ;
  }
  theIFilterState( flptr ) = state ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            writedata_()          author:              Andrew Cave
   creation date:       01-Sep-1990           last modification:   ##-###-####
   arguments:           none .
   description:

   Writes given data to the output stream, doing an eexec encryption.

---------------------------------------------------------------------------- */
static Bool writedata_(ps_context_t *pscontext)
{
  register uint8 inbyte , outbyte ;
  register uint16 state ;

  register uint8 *clist , *limit ;

  register uint8 *lhex_table ;
  register FILELIST *flptr ;

  uint8 *tempclist ;
  uint16 templen ;
  OBJECT *tempo ;
  int8 glmode ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( NULL == ( tempo = get1filestring( & tempclist , & templen , &glmode,
                                         CANWRITE , CANREAD )))
    return FALSE ;

  flptr = oFile(*tempo) ;
  if ( ! isIOpenFileFilter( tempo , flptr ))
    return error_handler( IOERROR ) ;

  lhex_table = nibble_to_hex_char ;
  state = (uint16)theIFilterState( flptr ) ;

  clist = tempclist ;
  for ( limit = clist + templen ; clist < limit ; ++clist ) {
    inbyte = clist[ 0 ] ;
    outbyte = ENCRYPT_BYTE( inbyte , state ) ;

    if ( Putc( lhex_table[ outbyte >> 4 ] , flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;
    if ( Putc( lhex_table[ outbyte & 15 ] , flptr ) == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;

    ENCRYPT_CHANGE_STATE( outbyte , state , DECRYPT_ADD , DECRYPT_MULT ) ;
  }
  theIFilterState( flptr ) = state ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* Log stripped */
