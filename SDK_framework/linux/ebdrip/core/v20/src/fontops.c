/** \file
 * \ingroup fonts
 *
 * $HopeName: SWv20!src:fontops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font operations; define, scale, select, compose, blend, matrix manipulation.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "mm.h"
#include "mps.h"
#include "mmcompat.h"
#include "gcscan.h"
#include "objects.h"
#include "dicthash.h"
#include "fileio.h"
#include "fonth.h"
#include "namedef_.h"

#include "bitblts.h"
#include "matrix.h"
#include "constant.h" /* EPSILON */
#include "params.h"
#include "psvm.h"
#include "stacks.h"
#include "display.h"
#include "graphics.h"
#include "gstate.h"
#include "showops.h"
#include "adobe1.h"  /* DLD1_CASE */
#include "control.h"
#include "miscops.h"
#include "utils.h"
#include "swmemory.h"
#include "execops.h"
#include "fontops.h"
#include "swdevice.h"
#include "hqxfonts.h"
#include "dictscan.h"
#include "dictops.h"
#include "fileops.h"
#include "cidfont.h"
#include "encoding.h"

#define REMEMBER 16

/* --- Internal Functions --- */

static Bool definefont_internal(ps_context_t *pscontext, Bool cid_caller);
static Bool cmap_setup_usematrix(OBJECT *fdict, OBJECT *cmap);
static Bool font_checkdict(OBJECT *fonto);
static Bool CID_checkdict(OBJECT *fonto, OBJECT *cidfonto);
static Bool CID_check_fdarray(OBJECT * fdarray, OMATRIX * cidmatrix,
                              Bool apply, int32 fonttype, OBJECT **painto);
static void  cacheFontDirectory(void);
static Bool font_derivation_store(OBJECT *fonto, OBJECT *keyo) ;
static mps_res_t MPS_CALL scanMakeScaleCaches(mps_ss_t ss, void *p, size_t s);
static Bool makethefont(OBJECT *olddict, OMATRIX *matrixptr,
                        OBJECT **ret_dict) ;


/* --- Exported Variables --- */

int32 mflookpos;
MFONTLOOK *oldmfonts;
MFONTLOOK **poldmfonts;

static mps_root_t oldMFontsRoot;

OBJECT  lfontdirobj ;
OBJECT  gfontdirobj ;
static OBJECT fontinfoobj ;

OBJECT *lfontdirptr;
OBJECT *gfontdirptr;
OBJECT * fontdirptr;

static void init_C_globals_fontops(void)
{
  OBJECT oinit = OBJECT_NOTVM_NOTHING ;
  mflookpos = 0 ;
  oldmfonts = NULL ;
  poldmfonts = NULL ;
  oldMFontsRoot = NULL ;
  lfontdirobj = gfontdirobj = fontinfoobj = oinit ;
  lfontdirptr = NULL ;
  gfontdirptr = NULL ;
  fontdirptr = NULL ;
}

static Bool ps_fontops_swstart(struct SWSTART *params)
{
  register int32 i ;
  MFONTLOOK init = { 0 } ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  init.thefont = onothing ;

  oldmfonts = mm_alloc_static(REMEMBER * sizeof(MFONTLOOK)) ;
  poldmfonts = mm_alloc_static(REMEMBER * sizeof(MFONTLOOK *)) ;
  if ( oldmfonts == NULL || poldmfonts == NULL )
    return FALSE ;

  for ( i = 0 ; i < REMEMBER ; ++i ) {
    oldmfonts[ i ] = init ;
    poldmfonts[ i ] = ( & oldmfonts[ i ] ) ;
  }

  /* Encodings need to be initialised after systemdict is available,
     but before the %boot% file sequence. There isn't a suitable place in
     COREfonts to hook that in, so we initialise them here. */
  if ( !initEncodings() )
    return FALSE ;

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &oldMFontsRoot, mm_arena, mps_rank_exact(),
                        0, scanMakeScaleCaches, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}


/* finishMakeScaleCaches - finish the scale font caches */
static void ps_fontops_finish(void)
{
  /* Make sure we don't use them after they've been finished. */
  mflookpos = 0;
  mps_root_destroy( oldMFontsRoot );
}

void ps_fontops_C_globals(core_init_fns *fns)
{
  init_C_globals_fontops() ;

  fns->swstart = ps_fontops_swstart ;
  fns->finish = ps_fontops_finish ;
}


/* ----------------------------------------------------------------------------
   function:            definefont_()      author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 146.
---------------------------------------------------------------------------- */

Bool definefont_(ps_context_t *pscontext)
{
  return definefont_internal(pscontext, FALSE);
}

/* This is where 'defineresource' on a CIDFont instance winds up. */

Bool defineCIDfont(ps_context_t *pscontext)
{
  return definefont_internal(pscontext, TRUE);
}


/* ---------------------------------------------------------------------------
   The cid_caller flag indicates if we're called 'cos of definefont or
   defineCIDfont. If it's cidfont, then only cidfonts are allowed.  For
   definefont, anything goes.
 */
static Bool definefont_internal(ps_context_t *pscontext, Bool cid_caller)
{
  Bool check_dict ;
  OBJECT *o1 , *fonto , *thed , *theo ;
  OBJECT lerrobj = OBJECT_NOTVM_NOTHING;
  OBJECT fido = OBJECT_NOTVM_NOTHING;

  OCopy(lerrobj, errobject);
  theTags(errobject) = OOPERATOR | EXECUTABLE | UNLIMITED;
  oOp(errobject) = &system_ops[ NAME_defineresource ];

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  fonto = theTop( operandstack ) ;
  if ( oType( *fonto ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  o1 = stackindex( 1 , & operandstack ) ;

  if ( oType( *o1 ) == ONULL )
    return error_handler( TYPECHECK ) ;

  thed = oDict( *fonto ) ;

  if ( theISaveLangLevel( workingsave ) >= 2 ) {
    check_dict = TRUE ;
    if (( theTags(*thed) & ISAFONT ) != 0 )
      check_dict = FALSE ;
  } else {
    /* level 1 cannot redefine a font dictionary */
    if ( fast_extract_hash_name( fonto , NAME_FID ))
      return error_handler( INVALIDFONT ) ;
    check_dict = TRUE ;
  }

  if ( check_dict ) {
    if ( ! oCanWrite( *thed ))
      if ( ! object_access_override(thed))
        return error_handler( INVALIDACCESS ) ;

    /* As far as I can tell from testing on 3010, CIDFontType has precedence
       over FontType. */
    if ( NULL != ( theo = fast_extract_hash_name( fonto , NAME_CIDFontType ))) {
      if ( ! CID_checkdict(fonto, theo) )
        return FALSE;
    } else { /* Non CID font */
      if ( cid_caller )
        return error_handler(INVALIDFONT);

      if ( ! font_checkdict(fonto) )
        return FALSE;
    }

    /*  Add additional fontID value */
    oName( nnewobj ) = &system_names[ NAME_FID ] ;
    theTags(fido) = OFONTID | LITERAL ;
    oFid(fido) = ++fid_count ;
    theLen(fido) = 0 ;
    if (HqxDepth > 0) {
      HQASSERT (fid_count < FID_PROTECTED,
                "overflow of FID and bit-flags in definefont_");
      oFid(fido) |= FID_PROTECTED ;
    }
    if ( ! insert_hash(fonto, &nnewobj, &fido) )
      return FALSE ;
  }

  /* In bootstrap.ps, we call definefont (guaranteed), so were here can cache
     the globals FontDirectory & GlobalFontDirectory. */
  if ( ! fontdirptr )
    cacheFontDirectory() ;

  /* Determine derivation of the font. */
  if ( !font_derivation_store(fonto, o1) )
    return FALSE ;

  if ( ! cid_caller ) {
    /* Store the resulting font in the FontDirectory. */
    if ( !insert_hash_even_if_readonly(fontdirptr, o1, fonto) )
      return FALSE ;

    /* if the vm mode is Global, add to the local FontDirectory as well */
    if ( ps_core_context(pscontext)->glallocmode ) {
      if ( !insert_hash_even_if_readonly(&lfontdirobj, o1, fonto) )
        return FALSE ;
    }
  }

  theTags(*thed) = OMARK | READ_ONLY | ISAFONT ;
  Copy( o1 , fonto ) ;

  pop( & operandstack ) ;
  OCopy(errobject, lerrobj);

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            undefinefont_     author:              Luke Tunmer
   creation date:       21-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool undefinefont_(ps_context_t *pscontext)
{
  OBJECT *theo ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;
  theo = theTop( operandstack ) ;
  if ( oType( *theo ) != ONAME )
    return error_handler( TYPECHECK ) ;

  if ( ! remove_hash( fontdirptr , theo , FALSE ))
    return FALSE ;

  oName( nnewobj ) = &system_names[ NAME_Font ] ;
  if ( ! push( &nnewobj , &operandstack ))
    return FALSE ;

  oName( nnewobj ) = &system_names[ NAME_undefineresource ] ;

  if ( NULL == ( theo = extract_hash( &internaldict , &nnewobj )))
    return error_handler( UNREGISTERED ) ;

  if ( ! push( theo, &operandstack))
    return FALSE ;

  return myexec_(pscontext) ;
}


/* ----------------------------------------------------------------------------
   function:            scalefont_()       author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 210.

---------------------------------------------------------------------------- */
Bool scalefont_(ps_context_t *pscontext)
{
  SYSTEMVALUE scale ;
  register OBJECT *fontdict ;
  OBJECT *newfontdict ;
  register OBJECT *o1 ;

  OMATRIX matrix ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  fontdict = stackindex( 1 , & operandstack ) ;
  if ( oType( *fontdict ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! stack_get_numeric(&operandstack, &scale, 1) )
    return FALSE ;

  o1 = oDict( *fontdict ) ;
  if ( ! oCanRead( *o1 ))
    if ( ! object_access_override(o1))
      return error_handler( INVALIDACCESS ) ;
  if ( ! ( (int32)theTags(*o1) & ISAFONT ))
    return error_handler( INVALIDFONT ) ;

  matrix.matrix[ 0 ][ 0 ] = scale ;
  matrix.matrix[ 0 ][ 1 ] = 0.0 ;
  matrix.matrix[ 1 ][ 0 ] = 0.0 ;
  matrix.matrix[ 1 ][ 1 ] = scale ;
  matrix.matrix[ 2 ][ 0 ] = 0.0 ;
  matrix.matrix[ 2 ][ 1 ] = 0.0 ;
  matrix.opt = 0 ;
  if ( scale != 0.0 )
    matrix.opt = MATRIX_OPT_0011 ;
  if ( ! makethefont( fontdict , & matrix , &newfontdict ))
    return FALSE ;

  Copy( fontdict , newfontdict ) ;
  pop( &operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            setfont_()         author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 215.

---------------------------------------------------------------------------- */
Bool setfont_(ps_context_t *pscontext)
{
  register OBJECT *o1 ;

  OBJECT *thed ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = theTop( operandstack ) ;
  if ( oType( *o1 ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  thed = oDict( *o1 ) ;
  if ( ! oCanExec( *thed ))
    if ( ! object_access_override(thed))
      return error_handler( INVALIDACCESS ) ;
  if ( theISaveLangLevel( workingsave ) >= 2 ) {
    /* Level 1 allows any dictionary. Level 2 requires a dictionary
     * made from definefont.
     */
    if ( ! ( (int32)theTags(*thed) & ISAFONT ))
      return error_handler( INVALIDFONT ) ;
  }

  if ( ! gs_setfont( o1 ))
    return FALSE ;

  pop( &operandstack ) ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            gs_setfont        author:              Luke Tunmer
   creation date:       16-Aug-1991       last modification:   ##-###-####
   arguments:
   description:

   The guts of setfont_
---------------------------------------------------------------------------- */
Bool gs_setfont( OBJECT *fontdict )
{
  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;

  if ( !OBJECTS_IDENTICAL(*fontdict, theMyFont(*fontInfo)) ) {
    OBJECT *theo;

    Copy(&theMyFont(*fontInfo), fontdict) ;

    theo = fast_extract_hash_name(fontdict, NAME_FID);
    if (!theo)
       return FALSE;            /* we can't find an FID?! */
    if ( oType(*theo) != OFONTID )
      return error_handler( TYPECHECK ) ;

    isEncrypted(*fontInfo) =
       (uint8) ((oFid(*theo) & FID_PROTECTED) ? PROTECTED_HQXRUN : PROTECTED_NONE);
    gotFontMatrix(*fontInfo) = FALSE ;
    theLookupFont(*fontInfo) = NULL ;
    theLookupMatrix(*fontInfo) = NULL ;
    fontInfo->cancache = TRUE ;
    /* Does_Composite_Fonts */
    theWModeNeeded(*fontInfo) = TRUE; /* not set */

    /* And set the fonttype */
    error_clear_newerror();
    theo = fast_extract_hash_name(fontdict, NAME_FontType);
    if ( theo == NULL )
      return newerror ? FALSE : error_handler( INVALIDFONT );

    if ( oType(*theo) != OINTEGER )
      return error_handler( INVALIDFONT ) ;

    theFontType(*fontInfo) = (uint8)oInteger(*theo);

    /* Set the location from which Private information will be extracted */
    Copy(&fontInfo->subfont, fontdict) ;

    /* For safety, set font methods to routines that throw errors. */
    fontInfo->fontfns = &font_invalid_fns ;
  }

  /* Root font is set unconditionally because it is possible you could
     do setfont on a font which is part of a composite font heirarchy. */
  Copy(&fontInfo->rootfont, fontdict) ;

  return TRUE ;
}

void gs_setfontctm( OMATRIX *fontmatrix )
{
  MATRIX_COPY( & theFontATMTRM( *gstateptr ) , fontmatrix ) ;
  gotFontMatrix( theFontInfo( *gstateptr )) = FALSE ;
  theLookupFont( theFontInfo( *gstateptr )) = NULL ;
  theLookupMatrix( theFontInfo( *gstateptr )) = NULL ;
}

static void cacheFontDirectory(void)
{
  OBJECT *fontinfoptr ;

  oName( nnewobj ) = &system_names[NAME_FontDirectory] ;
  lfontdirptr = fast_sys_extract_hash( & nnewobj ) ;
  HQASSERT(lfontdirptr, "FontDirectory not present at bootup") ;
  HQASSERT(oType(*lfontdirptr) == ODICTIONARY,
           "FontDirectory is not a directory") ;

  oName( nnewobj ) = &system_names[NAME_GlobalFontDirectory] ;
  gfontdirptr = fast_sys_extract_hash( & nnewobj ) ;
  HQASSERT(gfontdirptr, "GlobalFontDirectory not present at bootup") ;
  HQASSERT(oType(*gfontdirptr) == ODICTIONARY,
           "GlobalFontDirectory is not a directory") ;

  fontinfoptr = fast_extract_hash_name( &internaldict, NAME_FontInfo ) ;
  HQASSERT(fontinfoptr, "Internal FontInfo directory not present at bootup") ;
  HQASSERT(oType(*fontinfoptr) == ODICTIONARY,
           "Internal FontInfo is not a directory") ;

  Copy( &lfontdirobj , lfontdirptr ) ;
  Copy( &gfontdirobj , gfontdirptr ) ;
  Copy( &fontinfoobj , fontinfoptr ) ;

  fontdirptr = lfontdirptr ;            /* Set to local for now. */
}

/* ----------------------------------------------------------------------------
   function:            makefont_()        author:              Andrew Cave
   creation date:       16-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   See PostScript reference manual page 183.

---------------------------------------------------------------------------- */
Bool makefont_(ps_context_t *pscontext)
{
  register OBJECT *olddict ;
  OBJECT *newdict ;
  register OBJECT *o1 , *matrixo ;

  OMATRIX matrix ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  olddict = stackindex( 1 , & operandstack ) ;
  if ( oType( *olddict ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  matrixo = theTop( operandstack ) ;
  if ( ! is_matrix( matrixo , & matrix ))
    return FALSE ;
  if ( ! oCanRead(*matrixo) )
    if ( ! object_access_override(matrixo))
      return error_handler( INVALIDACCESS ) ;

  o1 = oDict( *olddict ) ;
  if ( ! oCanRead( *o1 ))
    if ( ! object_access_override(o1))
      return error_handler( INVALIDACCESS ) ;
  if ( ! ( (int32)theTags(*o1) & ISAFONT ))
    return error_handler( INVALIDFONT ) ;

  if ( ! makethefont( olddict , & matrix , &newdict ))
    return FALSE ;

  Copy( olddict , newdict ) ;
  pop( &operandstack ) ;

  return TRUE ;
}


/* Required VM allocation must be set before calling. */
static Bool makefontdict(
  corecontext_t *context,
  OBJECT *olddict,
  OMATRIX *matrixptr,
  OBJECT **ret_dict)
{
  OMATRIX scalem ;
  OMATRIX fontm ;
  OBJECT fonto = OBJECT_NOTVM_NOTHING ;
  OBJECT matrixo = OBJECT_NOTVM_NOTHING ;
  MFONTLOOK *noldmfont ;
  OBJECT *o1 ;
  OBJECT *o2 ;
  int32 newDictSize ;
  int32 i ;

  getDictLength(newDictSize, olddict) ;
  if ( ! fast_extract_hash_name( olddict , NAME_OrigFont ))
    newDictSize += 1 ;

  if ( (o1 = fast_extract_hash_name( olddict , NAME_ScaleMatrix )) != NULL) {
    if ( ! is_matrix( o1 , & scalem ))
      return FALSE ;

    matrix_mult( & scalem , matrixptr , & scalem ) ;
  }
  else {
    MATRIX_COPY( & scalem , matrixptr ) ;
    newDictSize += 1 ;
  }

  /* Get memory for new dictionary & it's new FontMatrix */
  if ( ! ps_dictionary(&fonto, newDictSize) ||
       ! CopyDictionary(olddict, &fonto, NULL, NULL) )
    return FALSE ;

  /* Set up new FontMatrix */
  if ( NULL == (o2 = fast_extract_hash_name(&fonto, NAME_FontMatrix) ))
    return error_handler( UNREGISTERED ) ;

  if ( ! is_matrix( o2 , & fontm ))
    return FALSE ;

  matrix_mult( & fontm , matrixptr , & fontm ) ;

  /* Note that o2 points into the dictionary, so insert_hash is unnecessary. */
  if ( !ps_array(o2, 6) ||
       !from_matrix(oArray(*o2), & fontm , oGlobalValue(*o2)) )
    return FALSE ;

  /* add OrigFont entry */
  oName( nnewobj ) = &system_names[ NAME_OrigFont ] ;
  if ( ! insert_hash(&fonto, &nnewobj, olddict) )
    return FALSE ;

  /* add ScaleMatrix entry - take a copy of the matrix */
  if ( !ps_array(&matrixo, 6) ||
       !from_matrix(oArray(matrixo), &scalem, oGlobalValue(matrixo)) )
    return FALSE ;

  oName( nnewobj ) = &system_names[ NAME_ScaleMatrix ] ;
  if ( ! insert_hash(&fonto, &nnewobj, &matrixo) )
    return FALSE ;

  /* limit access to the new font dictionary */
  theTags(*oDict(fonto)) = OMARK | READ_ONLY | ISAFONT ;

  if ( mflookpos != REMEMBER )
    ++mflookpos ;

  noldmfont = poldmfonts[ mflookpos - 1 ] ;
  for ( i = ( mflookpos - 1 ) ; i > 0 ; --i )
    poldmfonts[ i ] = poldmfonts[ i - 1 ] ;
  poldmfonts[ 0 ] = noldmfont ;

  /* Insert font for later use. */
  theISaveLevel( noldmfont ) = context->savelevel ;
  MATRIX_COPY( & noldmfont->omatrix , matrixptr ) ;
  /* Must re-get this, since may have garbage-collected. */
  theFontPtr(*noldmfont) = oDict(*olddict) ;
  o1 = &theMyFont(*noldmfont) ;
  OCopy(*o1, fonto) ;

  *ret_dict = o1;

  return (TRUE);
}

/* ----------------------------------------------------------------------------
   function:            makethefont       author:              Luke Tunmer
   creation date:       16-Aug-1991       last modification:   ##-###-####
   arguments:
   description:

   The guts of makefont_
---------------------------------------------------------------------------- */
static Bool makethefont(OBJECT *olddict, OMATRIX *matrixptr, OBJECT **ret_dict)
{
  corecontext_t *context = get_core_context_interp();
  MFONTLOOK *noldmfont ;
  int32 i , j ;
  OBJECT *o1 ;
  Bool vmmode;
  Bool result;

  HQASSERT( olddict && matrixptr && ret_dict, "illegal NULL pointers to fn" ) ;
  HQASSERT( matrix_assert( matrixptr ) , "matrix not a proper optimised matrix" ) ;

/* See if already had a similar font. */
  o1 = oDict( *olddict ) ;
  for ( i = 0 ; i < mflookpos ; ++i ) {
    noldmfont = poldmfonts[ i ] ;
    if ( o1 == theFontPtr(*noldmfont) )
      if ( MATRIX_EQ( & noldmfont->omatrix, matrixptr )) {
        for ( j = i ; j > 0 ; --j )
          poldmfonts[ j ] = poldmfonts[ j - 1 ] ;
        poldmfonts[ 0 ] = noldmfont ;

        *ret_dict = &theMyFont(*noldmfont) ;
        return TRUE ;
      }
  }

  /* make the VM alloc mode the same as the old dictionary */
  vmmode = setglallocmode(context, oGlobalValue(*o1));

  result = makefontdict(context, olddict, matrixptr, ret_dict);

  /* restore the VM allocation mode */
  setglallocmode(context, vmmode ) ;

  return (result);
}



/* ----------------------------------------------------------------------------
   function:            selectfont        author:              Luke Tunmer
   creation date:       16-Aug-1991       last modification:   ##-###-####
   arguments:
   description:

   See page 490 Level-2 book.
---------------------------------------------------------------------------- */
Bool selectfont_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 , *thefont ;
  int32 matrix_arg ;
  SYSTEMVALUE scale = 0.0 ; /* Silence stupid compiler */
  OMATRIX matrix ;
  OBJECT tempobj = OBJECT_NOTVM_NOTHING , *newfont ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 1 , &operandstack ) ;
  if ( oType( *o1 ) == ONULL )
    return error_handler( TYPECHECK ) ;

  o2 = theTop( operandstack ) ;
  if (oType(*o2) == OARRAY  ||  oType(*o2) == OPACKEDARRAY ) {
    if ( ! is_matrix( o2 , & matrix ))
      return FALSE ;
    matrix_arg = TRUE ;
  } else {
    if ( ! stack_get_numeric(&operandstack, &scale, 1) )
      return FALSE ;
    matrix_arg = FALSE ;
  }

  /* try to find the font */
  if ( NULL == ( thefont = extract_hash( fontdirptr , o1 ))) {
    /* must use findfont to get it */

    if ( ! push( o1 , &operandstack ))
      return FALSE ;
    oName( nnewobje ) = &system_names[ NAME_findfont ] ;

    if ( ! push( &nnewobje , & executionstack ))
      return FALSE ;
    if ( ! interpreter( 1 , NULL ))
      return FALSE ;

    /* check that findfont returned a real font dictionary */
    if ( isEmpty( operandstack ))
      return error_handler( STACKUNDERFLOW ) ;
    thefont = theTop( operandstack ) ;
    OCopy( tempobj , *thefont ) ;
    thefont = &tempobj ;
    pop( &operandstack ) ; /* remove the font from the stack */
    if (oType(tempobj) != ODICTIONARY )
      return error_handler( TYPECHECK ) ;
    o1 = oDict( tempobj ) ;
    if ( ! oCanRead(*o1))
      if ( ! object_access_override(o1))
        return error_handler( INVALIDACCESS ) ;
    if ( ! ( (int32)theTags(*o1) & ISAFONT ))
      return error_handler( INVALIDFONT ) ;
  }

  if ( ! matrix_arg ) {
    matrix.matrix[ 0 ][ 0 ] = scale ;
    matrix.matrix[ 0 ][ 1 ] = 0.0 ;
    matrix.matrix[ 1 ][ 0 ] = 0.0 ;
    matrix.matrix[ 1 ][ 1 ] = scale ;
    matrix.matrix[ 2 ][ 0 ] = 0.0 ;
    matrix.matrix[ 2 ][ 1 ] = 0.0 ;
    matrix.opt = 0 ;
    if ( scale != 0.0 )
      matrix.opt = MATRIX_OPT_0011 ;
  }
  if ( ! makethefont( thefont , & matrix , &newfont ))
    return FALSE ;

  if ( ! gs_setfont( newfont ))
    return FALSE ;

  npop( 2 , &operandstack ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------------
   composefont.
   In:  Name CMap [ Fonts ] composefont
   Out: Type0 Font Dictionary

   See "PostScript Language Extensions for CID-Keyed Fonts" p.24

   ------------------------------------------------------------------------ */

Bool composefont_(ps_context_t *pscontext)
{
  OBJECT *fname, *cmap, *fdeps;
  OBJECT *olist, *theo ;
  OBJECT newfontdict = OBJECT_NOTVM_NOTHING ;
  OBJECT oarray = OBJECT_NOTVM_NOTHING ;
  int32 i, numfonts ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  fdeps = theTop(operandstack);
  if ( oType(*fdeps) != OARRAY &&
       oType(*fdeps) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  cmap = stackindex(1, &operandstack);
  if ( oType(*cmap) != ONAME &&
       oType(*cmap) != OSTRING &&
       oType(*cmap) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  fname = stackindex(2, &operandstack);
  if ( oType(*fname) != ONAME &&
       oType(*fname) != OSTRING )
    return error_handler( TYPECHECK ) ;

  /* Create space for the new font dictionary */
  if ( !ps_dictionary(&newfontdict, 9) )
    return FALSE;

  /* FontName */
  oName(nnewobj) = &system_names[NAME_FontName];
  if ( !insert_hash(&newfontdict, &nnewobj, fname) )
    return FALSE;

  /* FontType */
  oName(nnewobj) = &system_names[NAME_FontType];
  oInteger(inewobj) = 0;
  if ( !insert_hash(&newfontdict, &nnewobj, &inewobj) )
    return FALSE;

  /* FMapType */
  oName(nnewobj) = &system_names[NAME_FMapType];
  oInteger(inewobj) = MAP_CMAP;
  if ( !insert_hash(&newfontdict, &nnewobj, &inewobj) )
    return FALSE;

  /* CMap */
  /* If the CMap given on the stack is a name (or string), do a findresource
   * to get a dictionary
   */
  if ( oType(*cmap) == ONAME || oType(*cmap) == OSTRING ) {
    OBJECT *cmapdict;
    if ( !push(cmap, &operandstack) )
      return FALSE;
    cmapdict = theTop(operandstack); /* This is where we expect the result */
    oName(nnewobj) = &system_names[NAME_CMap];
    if ( !push(&nnewobj, &operandstack) )
      return FALSE;
    oName(nnewobj) = &system_names[NAME_findresource];
    if ( !push(&nnewobj, &executionstack) )
      return FALSE;
    if ( !interpreter(1, NULL) ) {
      /* Tidy up after ourselves */
      npop(2, &operandstack);
      return FALSE;
    }

    if ( theTop(operandstack) != cmapdict ) /* Something wrong */
      return FALSE;
    if ( oType(*cmapdict) != ODICTIONARY ) {
      pop(&operandstack);
      return error_handler(TYPECHECK);
    }
    Copy(cmap, cmapdict);
    pop(&operandstack);
  }

  oName(nnewobj) = &system_names[NAME_CMap];
  if ( !insert_hash(&newfontdict, &nnewobj, cmap) )
    return FALSE;

  /* FDepVector */
  /* For each font in the array, convert names to dictionaries */
  /* Adobe appear to allow a null array - I suppose the user can poke it later
     in the dictionary */
  numfonts = theLen(*fdeps);
  olist = oArray(*fdeps);
  for ( i = 0; i < numfonts; ++i ) {
    switch ( oType( olist[i] ) ) {
    case ODICTIONARY:
      break;
    case OSTRING:
    case ONAME:
      /* Need to try and find the font: could be regular or CID flavour */
      {
        OBJECT tempobj = OBJECT_NOTVM_NOTHING;

        if ( NULL == ( theo = extract_hash(fontdirptr, olist + i ))) {
          /* Can't just use findresource, as it'll give us Courier/CIDBullet
             when the font doesn't exist.  Use resourcestatus on CIDFont
             category first to check existence.  If this works do a
             findresource, if it fails, just do a findfont. */
          oName(nnewobj) = &system_names[NAME_CIDFont];
          if ( !push2(olist + i, &nnewobj, &operandstack) )
            return FALSE;
          oName(nnewobj) = &system_names[NAME_resourcestatus];
          if ( !push(&nnewobj, &executionstack) ||
               !interpreter(1, NULL) )
            return FALSE ;
          /* Stack now either false if not found, or blah blah true if it
             exists. */
          if ( oBool(*theTop(operandstack)) ) {
            npop(3, &operandstack);
            oName(nnewobj) = &system_names[NAME_CIDFont];
            if ( !push2(olist + i, &nnewobj, &operandstack) )
              return FALSE;
            oName(nnewobj) = &system_names[NAME_findresource];
            if ( !push(&nnewobj, &executionstack) ||
                 !interpreter(1, NULL) )
              return FALSE ;
            /* check that findresource returned a real font dictionary */
            if ( isEmpty( operandstack ))
              return error_handler( STACKUNDERFLOW ) ;
            theo = theTop(operandstack);
            Copy(&tempobj, theo);
            theo = &tempobj;
            pop(&operandstack); /* remove the font from the stack */
            if (oType(tempobj) != ODICTIONARY )
              return error_handler( TYPECHECK );
            Copy(olist + i, theo);
          }
          else { /* resourcestatus failed, just do a findfont */
            Copy(theTop(operandstack), olist + i);
            oName(nnewobj) = &system_names[NAME_findfont];
            if ( !push(&nnewobj, &executionstack) ||
                 !interpreter(1, NULL) )
              return FALSE ;
            /* check that findresource returned a real font dictionary */
            if ( isEmpty( operandstack ))
              return error_handler( STACKUNDERFLOW ) ;
            theo = theTop(operandstack);
            Copy(&tempobj, theo);
            theo = &tempobj;
            pop(&operandstack); /* remove the font from the stack */
            if (oType(tempobj) != ODICTIONARY )
              return error_handler( TYPECHECK );
            Copy(olist + i, theo);
          }
        } else
          Copy(olist + i, theo);
      }
      break;
    default:
      return error_handler(TYPECHECK);
    }
  }

  oName(nnewobj) = &system_names[NAME_FDepVector];
  if ( !insert_hash(&newfontdict, &nnewobj, fdeps) )
    return FALSE;

  /* Encoding */
  /* Set up the Encoding array to be the identity mapping, same length as the
     FDepVector */
  if ( !ps_array(&oarray, numfonts) )
    return FALSE ;

  olist = oArray(oarray) ;

  for ( i = 0; i < numfonts; ++i ) {
    object_store_integer(&olist[i], i) ;
  }

  oName(nnewobj) = &system_names[NAME_Encoding];
  if ( !insert_hash(&newfontdict, &nnewobj, &oarray) )
    return FALSE;

  /* WMode */
  /* If defined in the CMap, use that value, else 0 */
  oName(nnewobj) = &system_names[NAME_WMode];
  theo = fast_extract_hash(cmap, &nnewobj);
  if ( theo ) {
    if ( oType(*theo) != OINTEGER )
      return error_handler(TYPECHECK);
    oInteger(inewobj) = oInteger(*theo);
  } else {
    oInteger(inewobj) = 0;
  }
  if ( !insert_hash(&newfontdict, &nnewobj, &inewobj) )
    return FALSE;

  /* FontMatrix */
  /* Use the identity matrix */
  HQASSERT(matrix_assert(&identity_matrix),
           "composefont_: not a proper optimised matrix");

  /* now create a PS matrix, and insert into the font dictionary */
  if ( !ps_array(&oarray, 6) ||
       !from_matrix(oArray(oarray), &identity_matrix, oGlobalValue(oarray)) )
    return FALSE;

  oName(nnewobj) = &system_names[NAME_FontMatrix];
  if ( !insert_hash(&newfontdict, &nnewobj , &oarray) )
    return FALSE;

  /* If the CMap program contained a beginusematrix/endusematrix, the cidinit
    procset will have added a special "/CodeMap" dictionary to the CMap
    dictionary.  The CodeMap contains the matrix (potentially one for each
    font indicated by the FDepVector array).  These matrices are now to
    be applied to their respective font. */
  if ( !cmap_setup_usematrix( &newfontdict, cmap ) )
    return FALSE;

  /* Copy our dictionary onto the stack */
  Copy(cmap, &newfontdict);
  pop(&operandstack);
  /* Stack now: Name Dict */
  oName(nnewobj) = &system_names[NAME_definefont];
  if ( !push(&nnewobj, &executionstack) )
    return FALSE;
  if ( !interpreter(1, NULL) ) {
    return FALSE;
  }
  return TRUE;
}

/* ---------------------------------------------------------------------------
   Apply any matrixes specified with beginusematrix in the cmap, to the
   descendent fonts.
   Note: If a usematrix specifies a font index outside the range of the
   FDepVector, a HQASSERT is fired but otherwise ignored in a release build.
*/

static Bool cmap_setup_usematrix(OBJECT *fdict, OBJECT *cmap)
{
  OBJECT *codemap;
  OBJECT *matrixlist;
  OBJECT *fdvect;
  OMATRIX matrix;
  int32 i;

  codemap = fast_extract_hash_name( cmap, NAME_CodeMap );
  if ( !codemap )
    return error_handler(INVALIDFONT);
  if ( oType(*codemap) != ODICTIONARY )
    return error_handler(INVALIDFONT);

  matrixlist = fast_extract_hash_name( codemap, NAME_usematrixblock );
  if ( !matrixlist )
    return TRUE;  /* No matrixes specified */
  if ( oType(*matrixlist) != OARRAY &&
       oType(*matrixlist) != OPACKEDARRAY )
    return error_handler(INVALIDFONT);

  fdvect = fast_extract_hash_name( fdict, NAME_FDepVector );
  /* Existence has been checked in definefont_(), type hasn't */
  if ( oType(*fdvect) != OARRAY &&
       oType(*fdvect) != OPACKEDARRAY )
    /* Given that this isn't normally checked by definefont_(), should this
       really be an error? */
    return error_handler(INVALIDFONT);


  HQASSERT( theLen(*matrixlist) <= theLen(*fdvect),
            "usematrix font id(s) outside FDepVector range." );

  /* Matrix for font i is stored at position i in the vector */
  for ( i = 0; i < theLen(*matrixlist) && i < theLen(*fdvect); ++i ) {
    OBJECT *matrixo = oArray(*matrixlist) + i;
    OBJECT *fonto = oArray(*fdvect) + i;
    OBJECT *newfdict;

    /* NULL entry indicates no matrix mapping for this font */
    if ( oType(*matrixo) != ONULL ) {
      if ( !is_matrix(matrixo, &matrix) )
        return FALSE;
      if ( !makethefont(fonto, &matrix, &newfdict) )
        return FALSE;
      Copy(fonto, newfdict);
    }

  }
  return TRUE;
}


static Bool font_checkdict(OBJECT *fonto)
{
  OMATRIX matrix ;
  OBJECT *theo;
  int32 fmaptype;

  /* Keys common to all non cid fonts */
  enum { ReqmFontType = 0, ReqmEncoding, ReqmFontMatrix, ReqmFont_numEntries };
  static NAMETYPEMATCH required_m[ReqmFont_numEntries + 1] = {
    { NAME_FontType,                1, { OINTEGER }},
    { NAME_Encoding,                2, { OARRAY, OPACKEDARRAY }},
    { NAME_FontMatrix,              2, { OARRAY, OPACKEDARRAY }},
    DUMMY_END_MATCH
  };

  /* Checks for all required key-value pairs */
  if ( ! dictmatch(fonto, required_m) )
    return error_handler( INVALIDFONT );

      /* test the matrix is a real matrix */
  if ( ! is_matrix( required_m[ReqmFontMatrix].result, & matrix ))
    return FALSE ;

  if ( oInteger(*(required_m[ReqmFontType].result)) == 0 ) {
    /* is a composite font, so check that user has allowed them */
    if ( ! theISaveCompFonts( workingsave ))
      return error_handler( INVALIDFONT ) ;

    /* Check for required entries in composite fonts */
    if ( NULL == ( theo = fast_extract_hash_name( fonto , NAME_FMapType )))
      return error_handler( INVALIDFONT ) ;
    if ( oType( *theo ) != OINTEGER )
      return error_handler( INVALIDFONT ) ;
    fmaptype = oInteger( *theo ) ;

    if ( ! fast_extract_hash_name( fonto , NAME_FDepVector ))
      return error_handler( INVALIDFONT ) ;

    if ( ! fast_extract_hash_name( fonto , NAME_PrefEnc )) {
      /* the spec says "if not present, a PS NULL will be created
       * by definefont".
       */
    }

    switch ( fmaptype ) {
    case MAP_ESC:
    case MAP_DESC:
      oName(nnewobj) = &system_names[ NAME_EscChar ] ;
      if ( ! fast_extract_hash( fonto , & nnewobj )) {
        /* we're to add one: 8#377 (spec, page 7) */
        oInteger( inewobj ) = 255 ;
        if ( ! insert_hash( fonto , & nnewobj , & inewobj ))
          return FALSE ;
      }
      break;

    case MAP_SUBS:
      if ( ! fast_extract_hash_name( fonto , NAME_SubsVector ))
        return error_handler( INVALIDFONT ) ;
      break;

    case MAP_SHIFT:
      oName(nnewobj) = &system_names[ NAME_ShiftIn ] ;
      if ( ! fast_extract_hash( fonto , & nnewobj )) {
        /* we're to add one: 15 (Red Book 2 sec 5.9) */
        oInteger(inewobj) = 15 ;
        if ( ! insert_hash( fonto , & nnewobj , & inewobj ))
          return FALSE ;
      }
      oName(nnewobj) = &system_names[ NAME_ShiftOut ] ;
      if ( ! fast_extract_hash( fonto , & nnewobj )) {
        /* we're to add one: 14 (Red Book 2 sec 5.9) */
        oInteger(inewobj) = 14 ;
        if ( ! insert_hash( fonto , & nnewobj , & inewobj ))
          return FALSE ;
      }
      break;

    case MAP_CMAP:
      theo = fast_extract_hash_name( fonto, NAME_CMap );
      if ( theo == NULL )
        return error_handler(INVALIDFONT);
      if ( oType(*theo) != ODICTIONARY )
        return error_handler(INVALIDFONT);
      break;

    default:
      ;
    }
  }

  return TRUE;
}


static Bool CID_checkdict(OBJECT *fonto, OBJECT *cidfonto)
{
  OBJECT *theo;
  int32 fdbytes, gdbytes, cidcount;
  int32 cidfonttype;
  int32 fonttype = -1;
  OMATRIX matrix = { 0.001, 0.0, 0.0, 0.001, 0.0, 0.0, MATRIX_OPT_0011 };
  OBJECT * fdarray = NULL;
  Bool nofontmatrix = FALSE;

  /* NOTE: the spec says that CIDSystemInfo and CIDFontName are also required.
     The 3010 implementation on hippo doesn't appear to support this, so I've
     loosened our tests to match

     Keys common to all cid font flavours */

  enum { CAmFontBBox = 0, CAmFontMatrix, CAm_numEntries };
  static NAMETYPEMATCH cidall_m[CAm_numEntries + 1] = {
    { NAME_FontBBox,               2, { OARRAY, OPACKEDARRAY }},
    { NAME_FontMatrix | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
    DUMMY_END_MATCH
  };


  /* Keys for CIDFontType 0 */
  enum { C0mCIDCount = 0, C0mCIDMapOffset, C0mFDArray, C0mFDBytes,
         C0mGDBytes, C0mGlyphData, C0mGlyphDirectory, C0mPaintType,
         C0mFontType, C0m_numEntries};
  static NAMETYPEMATCH cidtype0_m[C0m_numEntries + 1] = {
    /* Use the enum below to index entries */
    { NAME_CIDCount,                  1, { OINTEGER }},
    { NAME_CIDMapOffset,              1, { OINTEGER }},
    { NAME_FDArray,                   2, { OARRAY, OPACKEDARRAY }},
    { NAME_FDBytes,                   1, { OINTEGER }},
    { NAME_GDBytes,                   1, { OINTEGER }},
    { NAME_GlyphData,                 4, { OINTEGER, OSTRING,
                                           OARRAY, OPACKEDARRAY }},
    { NAME_GlyphDirectory|OOPTIONAL,  3, { ODICTIONARY,
                                           OARRAY, OPACKEDARRAY }},
    { NAME_PaintType|OOPTIONAL,       1, { OINTEGER }},
    { NAME_FontType|OOPTIONAL,        1, { OINTEGER }},
    DUMMY_END_MATCH
  };


  /* Keys for CIDFontType 1 */
  enum { C1mBuildGlyph = 0, C1m_numEntries };
  static NAMETYPEMATCH cidtype1_m[C1m_numEntries + 1] = {
    { NAME_BuildGlyph,           2, { OARRAY, OPACKEDARRAY }},
    DUMMY_END_MATCH
  };

  /* Keys for CIDFontType 2
     Note that CIDTYPE2 with dictionary and integer form CIDMaps have no need
     for GDBytes. TN3011 which introduced these forms does not explicitly
     say it is not needed, but we allow its omission in these cases. */
  enum { C2mCharStrings = 0, C2mCIDCount, C2mCIDMap, C2mEncoding,
         C2mFontType, C2mGDBytes, C2msfnts, C2m_numEntries };
  static NAMETYPEMATCH cidtype2_m[C2m_numEntries + 1] = {
    /* Use the enum below to index entries */
    { NAME_CharStrings,          1, { ODICTIONARY }},
    { NAME_CIDCount,             1, { OINTEGER }},
    { NAME_CIDMap,               5, { OSTRING, OARRAY, OPACKEDARRAY, ODICTIONARY, OINTEGER }},
    { NAME_Encoding,             2, { OARRAY, OPACKEDARRAY }},
    { NAME_FontType,             1, { OINTEGER }},
    { NAME_GDBytes | OOPTIONAL,   1, { OINTEGER }},
    { NAME_sfnts,                3, { OARRAY, OPACKEDARRAY, OFILE }},
    DUMMY_END_MATCH
  };

  /* No CIDFontType 3 exists */
  /* No additional keys for CIDFontType 4 */

  /* First check the cidfonttype object we were passed */
  if ( oType(*cidfonto) != OINTEGER )
    return error_handler(INVALIDFONT);
  cidfonttype = oInteger(*cidfonto);

  /* Check for all required key-value pairs for all types of CID Font */
  if ( !dictmatch(fonto, cidall_m) )
    return error_handler(INVALIDFONT);

  /* Extract the matrix - for CIDFontType 0, we have to set one up if not
       present.  For 1,2,4, it's required.  Deal with T0 case in the T0
       specific code later if required */
  if ( cidall_m[CAmFontMatrix].result != NULL ) {
    if ( ! is_matrix(cidall_m[CAmFontMatrix].result, & matrix) )
      return FALSE ;
  } else {
    if ( cidfonttype == 0 )
      nofontmatrix = TRUE;
    else
      return error_handler( INVALIDFONT );
  }

  /* Validate the individual entries for cidfonttype and derive the fonttype
       whilst we're at it.
       Page 137-8 of 3010 gives the following mappings:
       CIDFontType FontType Description
       0           9        Type 1 glyph procedures
       1           10       Type 3 like BuildGlyph procedure
       2           11       TrueType glyph procedure
       4           32       BitmapFont
       0C          102      Type 1 or 2 CFF font (Hqn extension type)
     */

  switch (cidfonttype) {
  case 0:
    if ( !dictmatch(fonto, cidtype0_m) )
      return error_handler(INVALIDFONT);

    fonttype = CIDFONTTYPE0 ;
    if ( cidtype0_m[C0mFontType].result &&
         (oInteger(*cidtype0_m[C0mFontType].result) == 2 ||
          oInteger(*cidtype0_m[C0mFontType].result) == CIDFONTTYPE0C ) )
      fonttype = CIDFONTTYPE0C ;

    cidcount = oInteger(*(cidtype0_m[C0mCIDCount].result));
    fdarray = cidtype0_m[C0mFDArray].result;

    fdbytes = oInteger(*(cidtype0_m[C0mFDBytes].result));
    if ( fdbytes  < 0 || fdbytes > 4 )
      return error_handler( INVALIDFONT );

    gdbytes = oInteger(*(cidtype0_m[C0mGDBytes].result));
    if ( gdbytes  < 1 || gdbytes > 4 ) {
      /* If there is a GlyphDirectory, then the charstring offset table isn't
         going to be used, so what is the point of sanity checking GDBytes? */
      if ( !cidtype0_m[C0mGlyphDirectory].result )
        return error_handler( INVALIDFONT );
    }

    /* If we're accessing the font from disk, GlyphData will be an int,
       holding the size of the data segment.
       If we've loaded the font into VM, it's a string or (more likely) an
       array of strings.

       This comment used to be followed by some typchecking code before I
       switched to dictmatch for all the dictionary lookup. Left in as
       possible useful
       */

    /* Need to loop through the entries in the FDArray and make sure
       they're defined properly.  Also need to define a matix if one isn't
       already (nofontmatrix) and in this case, mangle the matrix of each font
       in the FDArray */

    if ( nofontmatrix ) { /* Define a default matrix if needed */
      OBJECT arrayo = OBJECT_NOTVM_NOTHING;

      HQASSERT(matrix_assert(&matrix),
               "defineCIDfont: not a proper optimised matrix");
      HQASSERT(cidall_m[CAmFontMatrix].result == NULL,
               "defCIDfont: creating a matrix when one already exists");

      /* now create a PS matrix, and insert into the font dictionary */
      if ( !ps_array(&arrayo, 6) ||
           !from_matrix(oArray(arrayo), &matrix, oGlobalValue(arrayo)) )
        return FALSE;

      oName(nnewobj) = &system_names[ NAME_FontMatrix ];
      if ( ! insert_hash(fonto, &nnewobj, &arrayo) )
        return FALSE;
    }

    if ( !CID_check_fdarray(fdarray, &matrix, nofontmatrix, fonttype, &theo) )
      return FALSE;

    /* Check if GlyphDirectory is long enough */
    if ( cidtype0_m[C0mGlyphDirectory].result &&
         oType(*(cidtype0_m[C0mGlyphDirectory].result)) != ODICTIONARY ) {
      HQASSERT(oType(*(cidtype0_m[C0mGlyphDirectory].result)) == OARRAY ||
               oType(*(cidtype0_m[C0mGlyphDirectory].result)) == OPACKEDARRAY,
               "GlyphDirectory type wrong; dictmatch problem?") ;
      if ( theLen(*cidtype0_m[C0mGlyphDirectory].result) < cidcount )
        return error_handler(INVALIDFONT) ;
    }

    if ( theo && !cidtype0_m[C0mPaintType].result ) {
      /* Copy PaintType from FDArray dictionary to top-level dictionary. */
      oName(nnewobj) = &system_names[ NAME_PaintType ];
      if ( !insert_hash(fonto, &nnewobj, theo) )
        return FALSE ;
    }

    break; /* type 0 */

  case 1:
    fonttype = CIDFONTTYPE1;
    if ( !dictmatch(fonto, cidtype1_m) )
      return error_handler(INVALIDFONT);

    break; /* type 1 */

  case 2:
    fonttype = CIDFONTTYPE2;
    if ( !dictmatch(fonto, cidtype2_m) )
      return error_handler(INVALIDFONT);

    cidcount = oInteger(*(cidtype2_m[C2mCIDCount].result));

    /* Check length of the CID to TT Glyph map.
       This can either be a single string CIDCount * GDBytes long, or an
       array containing strings which are multiples of GDBytes long, totalling
       CIDCount * GDBytes long. */
    theo = cidtype2_m[C2mCIDMap].result;
    switch ( oType( *theo ) ) {
    case OSTRING:
      if ( cidtype2_m[C2mGDBytes].result == NULL )
        return error_handler( INVALIDFONT );

      gdbytes = oInteger(*(cidtype2_m[C2mGDBytes].result));
      if ( gdbytes  < 1 || gdbytes > 4 ||
           theLen(*theo) < cidcount * gdbytes )    /* [12460] */
        return error_handler( INVALIDFONT );
      break;
    case OARRAY:
    case OPACKEDARRAY:
      {
        int32 i, total = 0 ;
        OBJECT *slist = oArray(*theo) ;

        if ( cidtype2_m[C2mGDBytes].result == NULL )
          return error_handler( INVALIDFONT );

        gdbytes = oInteger(*(cidtype2_m[C2mGDBytes].result));
        if ( gdbytes  < 1 || gdbytes > 4 )
          return error_handler( INVALIDFONT );

        for ( i = 0 ; i < theLen(*theo) ; ++i, ++slist ) {
          if ( oType(*slist) != OSTRING ||
               theLen(*slist) % gdbytes != 0 )
            return error_handler( INVALIDFONT );
          total += theLen(*slist) ;
        }

        if ( total < cidcount * gdbytes )  /* [66181] repeating [12460] above */
          return error_handler( INVALIDFONT );

        break;
      }
    case OINTEGER: /* Offset for identity mapping */
      /* I can't see any reason to rangecheck the offset. It maps CID to
         glyph index, and there is no reason that a negative glyph index
         could not be used (apart from the possibility that it would be a
         stupid, confusing thing to do). */
    case ODICTIONARY:
      break ;
    default:
      HQFAIL("defCIDfont: should not get here.  Dictmatch problem?");
      return error_handler( INVALIDFONT );
    } /* switch */

    break;

  case 4:
    fonttype = CIDFONTTYPE4;
    /* No additional keys to check */
    break;

  default:  /* Unknown CIDFontType */
    return error_handler(INVALIDFONT);

  } /* switch ( cidfonttype ) */

  /* Insert FontType key calculated from cidfonttype above */
  HQASSERT(fonttype != -1, "defCIDfont: fontype has not been set");
  oName(nnewobj) = &system_names[ NAME_FontType ];
  oInteger( inewobj ) = fonttype;
  if ( ! insert_hash( fonto , & nnewobj , & inewobj ))
    return FALSE;

  return TRUE;
}


/* Verify that the members of the fdarray of the CID font have got the required
   dictionary entries to make them valid.  Also, if the apply parameter is
   true, concatenate the FontMatrix of each font in the fdarray with the
   inverse of the cidmatrix. */

static Bool CID_check_fdarray(OBJECT * fdarray, OMATRIX * cidmatrix,
                              Bool apply, int32 fonttype, OBJECT **painto)
{
  OMATRIX inv_cidmatrix;
  OBJECT * theo;
  int32 fdarraylen;
  int32 i;
  OBJECT * olist;

  HQASSERT(fdarray, "CID_check_fdarray: Null FDArray");
  HQASSERT(cidmatrix, "CID_check_fdarray: Null cidmatrix");
  HQASSERT(painto, "CID_check_fdarray: No object for PaintType");

  *painto = NULL ;

  fdarraylen = theLen(*fdarray);
  if ( fdarraylen < 1 )
    return error_handler( INVALIDFONT );

  olist = oArray(*fdarray);
  HQASSERT(olist, "CID_check_fdarray: length > 0 but olist null");

  if ( apply )
    if ( !matrix_inverse(cidmatrix, &inv_cidmatrix) )
      return FALSE;

  /* Loop over all the font dictionaries in the array */
  for ( i = 0; i < fdarraylen; ++i ) {
    OMATRIX matrix;

    if ( oType( olist[i] ) != ODICTIONARY )
      return error_handler( INVALIDFONT );

    /* Extract the font matrix */
    if ( NULL == (theo = fast_extract_hash_name( &olist[i], NAME_FontMatrix )) )
      return error_handler( INVALIDFONT ) ;
    /*   test the matrix is a real matrix */
    if ( !is_matrix(theo , &matrix) )
      return FALSE ;
    /* and transform it if required: */
    if ( apply )
      matrix_mult(&matrix, &inv_cidmatrix, &matrix);

    if ( !from_matrix(oArray(*theo), &matrix, oGlobalValue(*theo)) )
      return FALSE;

    /* Check there is a Private Dictionary, but only if not a CFF font */
    if ( fonttype != CIDFONTTYPE0C ) {
      OBJECT * pdict;

      if ( NULL == (pdict = fast_extract_hash_name(&olist[i], NAME_Private)) ||
           oType(*pdict) != ODICTIONARY )
        return error_handler( INVALIDFONT );
    }

    /* Check if PaintType exists. Some font vendors mistakenly put the
       PaintType in the FDArray dictionaries rather than the top-level
       dictionary. */
    if ( NULL != (theo = fast_extract_hash_name( &olist[i], NAME_PaintType )) ) {
      if ( oType(*theo) == OINTEGER ) { /* Defensive programming */
        if ( *painto == NULL ) {
          *painto = theo ;
        } else {
          HQASSERT( oInteger(**painto) == oInteger(*theo),
                   "Bogus FDArray PaintTypes do not match") ;
        }
      } else {
        HQFAIL("Bogus FDArray PaintType is not an integer") ;
      }
    }
  } /* for */

  return TRUE;
}

/* Find out where font came from; embedded in job, derived from existing font,
   file on disc. Store this information in font information dict in
   internaldict. The fontinfodict is keyed by a font dictionary, giving a
   dictionary value. The dictionary value contains the entries:

   /FontName
     The name under which the font was defined.
   /BaseFontName
     The name of the base font from which this font was derived.
   /BaseFont
     The dictionary of the base font from which this font was derived.
   /Embedded
     Boolean value indicating whether the base font was found in the job, or
     accessed from an external resource.
*/

enum { FDERIV_NO_CONFIDENCE = 0,
       FDERIV_TOTAL_CONFIDENCE = 65536 } ;

typedef struct fderiv_compare {
  OBJECT *fonto, *matchkey, *matchval ;
  int32 confidence ;
} FDERIV_COMPARE ;

static Bool font_derivation_subdictwalk(OBJECT *key, OBJECT *val, void *data)
{
  OBJECT *fonto = data ;
  OBJECT *compare ;

  /* Ignore these keys when trying a more general match. */
  static int32 match_ignore[] = { NAME_FID,
                                  NAME_Encoding,
                                  NAME_FontMatrix,
                                  NAME_Metrics,
                                  NAME_Metrics2,
                                  NAME_OrigFont,
                                  NAME_ScaleMatrix,
                                  NAME_WMode,
                                  NAME_VMode } ;

  HQASSERT(fonto, "No comparison font in derivation test") ;

  if ( oType(*key) == ONAME ) {
    int32 i, namenum = theINameNumber(oName(*key)) ;

    for ( i = 0 ; i < NUM_ARRAY_ITEMS(match_ignore) ; ++i )
      if ( namenum == match_ignore[i] )
        return TRUE ;
  }

  /* Fail if key is not present */
  if ( (compare = extract_hash(fonto, key)) == NULL )
    return FALSE ;

  return OBJECTS_IDENTICAL(*val, *compare) ;
}

static Bool font_derivation_dictwalk(OBJECT *key, OBJECT *val, void *data)
{
  FDERIV_COMPARE *fcomp = data ;
  OBJECT *xf, *xk ;
  int32 i, fonttype, confidence = FDERIV_NO_CONFIDENCE ;

  /* If any of these exist in both fonts and have the same value, then the
     font is a derivative. */
  static int32 match_if_any[] = { NAME_UniqueID,
                                  NAME_XUID } ;
  /* If all of these keys are the same in both fonts (either they both exist
     and are the same, or neither exist), and the font is of one of the
     relevant types (1, 2, 4, 42, 111) the font is a derivative. */
  static int32 match_same_both[] = { NAME_sfnts,
                                     NAME_CharStrings,
                                     NAME_FontBBox,
                                     NAME_Private } ;

  HQASSERT(fcomp, "No comparison struct for font derivation test") ;
  HQASSERT(oType(*fcomp->fonto) == ODICTIONARY &&
           oType(*key) == ODICTIONARY,
           "Should be comparing dictionaries") ;

  /* FontTypes must match. We need this later anyway, so extract it now. */
  oName(nnewobj) = &system_names[NAME_FontType] ;
  if ( (xk = fast_extract_hash(key, &nnewobj)) == NULL ||
       oType(*xk) != OINTEGER ||
       (xf = fast_extract_hash(fcomp->fonto, &nnewobj)) == NULL ||
       oType(*xf) != OINTEGER ||
       oInteger(*xf) != oInteger(*xk) )
    return TRUE ; /* No FontType, wrong type, or different, so can't match. */

  fonttype = oInteger(*xf) ;

  /* Do any of the elements in match_if_any match? */
  for ( i = 0 ; i < NUM_ARRAY_ITEMS(match_if_any) ; ++i ) {
    oName(nnewobj) = &system_names[match_if_any[i]] ;
    if ( (xk = fast_extract_hash(key, &nnewobj)) != NULL &&
         (xf = fast_extract_hash(fcomp->fonto, &nnewobj)) != NULL &&
         OBJECTS_IDENTICAL(*xf, *xk) ) {
      confidence = FDERIV_TOTAL_CONFIDENCE ;
      break ;
    }
  }

  if ( confidence != FDERIV_TOTAL_CONFIDENCE ) {
    switch ( fonttype ) {
    case FONTTYPE_1:
    case FONTTYPE_CFF:
    case FONTTYPE_4:
    case FONTTYPE_TT:
    case DLD1_CASE:
      /* Do all of the elements in match_same_both match? */
      confidence = FDERIV_TOTAL_CONFIDENCE ;
      for ( i = 0 ; i < NUM_ARRAY_ITEMS(match_same_both) ; ++i ) {
        oName(nnewobj) = &system_names[match_same_both[i]] ;
        xf = fast_extract_hash(key, &nnewobj) ;
        xk = fast_extract_hash(fcomp->fonto, &nnewobj) ;
        if ( !(xf == NULL && xk == NULL) &&
             !(xf != NULL && xk != NULL && OBJECTS_IDENTICAL(*xf, *xk)) ) {
          confidence = FDERIV_NO_CONFIDENCE ;
          break ;
        }
      }
      break ;
    default:
      /* We will try a shallow compare of all elements in the dicts to see
         how many are the same; this is likely to tell us if a copy of the
         top-level dict for re-encoding has been done. If all but the ignored
         entries match, then we can say it's a derived font. The match is
         from the potential base to the potential derived font, so that extra
         keys in the derived font are ignored. */
      confidence = FDERIV_TOTAL_CONFIDENCE ;
      if ( !walk_dictionary(key, font_derivation_subdictwalk, fcomp->fonto) )
        confidence = FDERIV_NO_CONFIDENCE ;
      break ;
    }
  }

  if ( confidence > fcomp->confidence ) {
    fcomp->confidence = confidence ;
    fcomp->matchkey = key ;
    fcomp->matchval = val ;
  }

  /* Keep trying if we're not sure we've found it */
  return (confidence != FDERIV_TOTAL_CONFIDENCE) ;
}

#define FDERIV_DICT_SIZE (4) /* Number of entries in font info dict */

static Bool font_derivation_store(OBJECT *fonto, OBJECT *keyo)
{
  FDERIV_COMPARE fcomp ;
  OBJECT infod = OBJECT_NOTVM_NOTHING ;
  OBJECT *basename, *basedict, *embedo ;

  HQASSERT(fonto, "No font object pointer") ;
  HQASSERT(oType(fontinfoobj) == ODICTIONARY,
           "No font info dictionary") ;

  /* If it is in this dict already under a different name, short-circuit
     the match. */
  if ( extract_hash(&fontinfoobj, fonto) != NULL )
    return TRUE ;

  /* Look over whole of font dictionary to find font from which this was
     derived. */

  fcomp.fonto = fonto ;
  fcomp.matchkey = NULL ;
  fcomp.matchval = NULL ;
  fcomp.confidence = FDERIV_NO_CONFIDENCE ;

  (void)walk_dictionary(&fontinfoobj, font_derivation_dictwalk, &fcomp) ;

  /* Create (local) dictionary for info, and add to font info dict */
  {
    corecontext_t *context = get_core_context_interp();
    Bool result ;
    Bool glmode;

    glmode = setglallocmode(context, FALSE) ;
    result = ps_dictionary(&infod, FDERIV_DICT_SIZE) ;
    setglallocmode(context, glmode) ;

    if ( !result )
      return error_handler(VMERROR) ;
  }

  if ( fcomp.confidence == FDERIV_TOTAL_CONFIDENCE ) { /* Derived font */
    HQASSERT(fcomp.matchkey && fcomp.matchval,
             "Total confidence in match, but no matched font") ;

    /* Look in matched font's info, and see where it was derived from. */
    basename = fast_extract_hash_name( fcomp.matchval, NAME_BaseFontName ) ;
    basedict = fast_extract_hash_name( fcomp.matchval, NAME_BaseFont ) ;

    /* Derived fonts are by definition embedded. */
    embedo = &tnewobj ;
  } else { /* Base font */
    OBJECT *fidict ;

    /* This is not a derived font, set the BaseFontName to the font name. */
    basename = keyo ;
    basedict = fonto ;

    /* Determine if this font is embedded; getfontfromdisk in resource.pss
       saves the font file in a sub-dictionary of internaldict. Compare this
       to the currentfile to determine if the definefont is using the same
       file as findfont. If so, it's not embedded. */
    if ( (currfileCache != NULL || currfile_cache()) &&
         (fidict = fast_extract_hash_name(&internaldict, NAME_FontDescriptor)) != NULL &&
         oType(*fidict) == ODICTIONARY &&
         (embedo = fast_extract_hash_name(fidict, NAME_FontFile)) != NULL &&
         oType(*embedo) == oType(*currfileCache) &&
         OBJECT_SET_D1(*embedo, OBJECT_GET_D1(*currfileCache)) &&
         theFilterIdPart(theLen(*embedo)) == theFilterIdPart(theLen(*currfileCache)) ) {
      embedo = &fnewobj ;
    } else {
      embedo = &tnewobj ;
    }
  }

  /* Insert the base name we found into the infodict */
  HQASSERT(basename, "No BaseFontName found for font") ;
  if ( !fast_insert_hash_name(&infod, NAME_BaseFontName, basename) )
    return FALSE ;

  HQASSERT(basedict, "No BaseFont found for font") ;
  if ( !fast_insert_hash_name(&infod, NAME_BaseFont, basedict) )
    return FALSE ;

  if ( !fast_insert_hash_name(&infod, NAME_Embedded, embedo) )
    return FALSE ;

  /* Store new name with which this font was associated */
  if ( !fast_insert_hash_name(&infod, NAME_FontName, keyo) )
    return FALSE ;

  return insert_hash_even_if_readonly(&fontinfoobj, fonto, &infod) ;
}

/* Return font derivation info. Returns FALSE if info could not be found */
Bool font_derivation(OBJECT *fonto, OBJECT *basename, OBJECT *basedict,
                     int32 *embedded)
{
  OBJECT *infod, *theo ;

  HQASSERT(fonto, "No font object") ;
  HQASSERT(basename, "No return pointer for font derivation name") ;
  HQASSERT(basedict, "No return pointer for font derivation dict") ;
  HQASSERT(embedded, "No return pointer for font embedding flag") ;
  HQASSERT(oType(*fonto) == ODICTIONARY,
           "font object is not a dictionary") ;

  /* Find the original font from makefont/scalefont/selectfont */
  while ( (theo = fast_extract_hash_name( fonto, NAME_OrigFont )) != NULL )
    fonto = theo ;

  theTags(*basename) = ONULL ;
  theTags(*basedict) = ONULL ;
  *embedded = FALSE ;

  /* Get info from font info dict */
  if ( (infod = extract_hash(&fontinfoobj, fonto)) == NULL )
    return FALSE ;

  if ( (theo = fast_extract_hash_name( infod, NAME_BaseFontName )) == NULL )
    return FALSE ;

  Copy(basename, theo) ;

  if ( (theo = fast_extract_hash_name( infod, NAME_BaseFont )) == NULL )
    return FALSE ;

  Copy(basedict, theo) ;

  if ( (theo = fast_extract_hash_name( infod, NAME_Embedded )) == NULL ||
       oType(*theo) != OBOOLEAN )
    return FALSE ;

  *embedded = oBool(*theo) ;

  return TRUE ;
}


/* Calculate the height of the current font. This is 'right' most of the time;
 * the value obtained assumes that the font is mapped to unit height by the
 * font matrix specified in the original font dictionary. Shear transforms in
 * both X and Y axes will reduce the accuracy of the obtained height
 */

Bool calc_font_height( SYSTEMVALUE *height )
{
  OMATRIX page_tm ;

  /* Get the inverse of the DTM and premultiply by CTM getting the
   * page transformation matrix in points (userspace to default userspace).
   */

  if ( ! matrix_inverse(&thegsDevicePageCTM(*gstateptr), &page_tm) )
    return error_handler( INVALIDFONT ) ;
  matrix_mult(&thegsPageCTM(*gstateptr), &page_tm, &page_tm) ;

  /* Add scale factor of the current font. */
  matrix_mult( &theFontInfo(*gstateptr).scalematrix , & page_tm , & page_tm ) ;

  {
    /* Transform ( 0 , 1 ) through the matrix onto ( x1 , y1 ). */

    SYSTEMVALUE x1 = page_tm.matrix[ 1 ][ 0 ] ;
    SYSTEMVALUE y1 = page_tm.matrix[ 1 ][ 1 ] ;

    /* ...and ( 1 , 0 ) onto ( x2 , y2 ). */

    SYSTEMVALUE x2 = page_tm.matrix[ 0 ][ 0 ] ;
    SYSTEMVALUE y2 = page_tm.matrix[ 0 ][ 1 ] ;

    /* Now find the angles of the lines to these points from the origin. */

    SYSTEMVALUE a1 = ( fabs( x1 ) > EPSILON ) ? atan( y1 / x1 ) :
                                                DEG_TO_RAD * 90.0 ;
    SYSTEMVALUE a2 = ( fabs( x2 ) > EPSILON ) ? atan( y2 / x2 ) :
                                                DEG_TO_RAD * 90.0 ;

    /* The effective point size is the perpendicular distance from the
     * transformed x axis to the transformed ( 0 , 1 ). In particular
     * this means we get the size "right" for a poor man's oblique
     * font which has been constructed using a matrix with an x shear
     * as a parameter to makefont (see 21816).
     */

    height[ 0 ] = fabs( sqrt( pow( x1 , 2.0 ) + pow( y1 , 2.0 )) *
                        sin( a1 - a2 )) ;
  }

  return TRUE ;
}


/* scanMakeScaleCaches - scanning function for the font caches */
static mps_res_t MPS_CALL scanMakeScaleCaches(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res;
  size_t i;

  UNUSED_PARAM( void *, p ); UNUSED_PARAM( size_t, s );
  MPS_SCAN_BEGIN( ss )
    for ( i = 0 ; (int32)i < mflookpos ; ++i ) {
      MPS_RETAIN( &poldmfonts[i]->fontdptr, TRUE );
      MPS_SCAN_CALL( res = ps_scan_field( ss, &poldmfonts[i]->thefont ));
      if ( res != MPS_RES_OK ) return res;
    }
  MPS_SCAN_END( ss );
  return MPS_RES_OK;
}

/* Log stripped */
