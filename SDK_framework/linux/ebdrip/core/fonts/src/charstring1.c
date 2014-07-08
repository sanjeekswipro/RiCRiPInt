/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:charstring1.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interpretation of Adobe Type 1 font format
 */

#include "core.h"
#include "hqmemcpy.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swstart.h"
#include "fileio.h"
#include "objects.h"
#include "dictscan.h"
#include "mm.h"
#include "mmcompat.h"
#include "monitor.h"
#include "strfilt.h"
#include "fonts.h"
#include "morisawa.h"
#include "namedef_.h"

#include "fontparam.h"
#include "matrix.h"
#include "stacks.h"
#include "dicthash.h"
#include "graphics.h"
#include "gstate.h"
#include "showops.h"
#include "gu_path.h"
#include "control.h"
#include "pathcons.h"
#include "dictops.h"
#include "system.h"

#include "fcache.h"
#include "adobe1.h"

#include "t1hint.h"
#include "encoding.h"
#include "chbuild.h"
#include "adobe2.h"  /* decode_adobe2_outline */
#include "charsel.h"
#include "charstring12.h" /* This file ONLY implements Type 1 charstrings */
#include "tt_xps.h"        /* tt_xps_metrics */

enum { FDR_Success, FDR_Fail, FDR_NoFilter, FDR_BadError };
/* --- Internal Functions --- */
static Bool decode_adobe1_outline(corecontext_t *context,
                                  charstring_methods_t *t1fns,
                                  OBJECT *stringo,
                                  charstring_build_t *buildfns);
static Bool decode_adobe1_outline_recursive(corecontext_t *context,
                                            charstring_methods_t *t1fns,
                                            uint8 *slist, uint32 slen,
                                            charstring_build_t *buildfns,
                                            int32 recurse_level, Bool *closed) ;
static void seed_font_decryption(uint8 *slist, int32 lenIV,
                                 uint16 *font_decrypt_state);
static uint8 char_font_decryption(uint8 *slist, int32 lenIV,
                                  uint16 *font_decrypt_state);
static Bool handle_othersubrs(corecontext_t *context,
                              charstring_methods_t *t1fns,
                              int32 othersubr_index, int32 numargs ,
                              double *arg_stack);

static Bool eCC_decrypt(uint8 *from, int32 len, uint8 *to) ;

/* --- Internal Debugging --- */
#if defined( ASSERT_BUILD )
int32 debug_hint_decode    = 0 ;
int32 debug_othersubr      = 0 ;
int32 debug_path_type      = 0 ;
int32 debug_path_values    = 0 ;
int32 debug_check_absolute = 0 ;
#endif

/* Type1 definitions */
#define HSTEM         1
#define VSTEM         3
#define VMOVETO       4
#define RLINETO       5
#define HLINETO       6
#define VLINETO       7
#define RRCURVETO     8
#define CLSPATH       9
#define CALLSUBR     10
#define RETURN       11
#define T1ESCAPE     12
#define HSBW         13
#define ENDCHAR      14
#define RMOVETO      21
#define HMOVETO      22
#define VHCURVETO    30
#define HVCURVETO    31
#define EDOTSECTION   0
#define EVSTEM3       1
#define EHSTEM3       2
#define ESEAC         6
#define ESBW          7
#define EDIV         12
#define ECALLOTHER   16
#define EPOP         17
#define ESETCPOINT   33
/* End of Type1 definitions */

#define ADD_FLEX_MOVETO(info, px , py ) MACRO_START \
 if ( (info)->F_index + 2 > TYPE1_FSLIMIT ) \
   return FAILURE(FALSE) ; \
 (info)->F_stack[ (info)->F_index++ ] = px ; \
 (info)->F_stack[ (info)->F_index++ ] = py ; \
 MACRO_END

#define CHECK_FOR_INITIAL_MOVE(info, buildfns) MACRO_START \
  if (!(info)->initial_point_set) {     \
    (info)->initial_point_set = TRUE;   \
    if ( !(*(buildfns)->moveto)((buildfns)->data, 0, 0) ) \
      return FALSE ; \
  }  \
  *closed = FALSE ; \
  MACRO_END

enum {
  fdm_Strategy,
  fdm_n_entries
} ;
static NAMETYPEMATCH fontdecodematch[fdm_n_entries + 1] = {
  { NAME_Strategy, 1, { OINTEGER }},
  DUMMY_END_MATCH
};

#define FONT_DECODE_FILTER "FONTDecode"

static int32 execFontDecodeFilter( OBJECT *stringold , OBJECT *stringnew , int32 strategy )
{
  int32  bytes , bytessofar ;
  int32  result ;

  uint8 *new_dstring = NULL ;
  uint8 *dstring ;
  int32 ldstring ;

  OBJECT *param_dict ;

  FILELIST *flptr ;
  FILELIST *nflptr ;
  FILELIST string_filter ;
  FILELIST decode_filter ;

  HQASSERT( stringold != NULL , "stringold NULL" ) ;
  HQASSERT( stringnew != NULL , "stringnew NULL" ) ;
  HQASSERT( strategy >= MINPOSSIBLESTRATEGY && strategy <= MAXPOSSIBLESTRATEGY , "strategy out of range" ) ;

  /* Firstly set up underlying string file that SwReadFilterBytes uses */
  flptr = &string_filter ;
  string_decode_filter(flptr) ;
  result = (*theIMyInitFile(flptr))(flptr, stringold, NULL) ;
  HQASSERT(result, "Should not have failed initialising string filter") ;

  /* Secondly find the device type... */
  nflptr = filter_external_find(NAME_AND_LENGTH(FONT_DECODE_FILTER),
                                &result,
                                FALSE ) ;
  if ( ! nflptr ) {
    (void)error_handler( INVALIDFONT );
    return FDR_NoFilter ;
  }

  /* check that such a filter is not accessible to PS, i.e. has right device number */
  if ( FONT_ND_CRYPT_DEVICE_TYPE !=
      theIDevTypeNumber( theIDevType( theIDeviceList( nflptr ))) ) {
    (void)error_handler( INVALIDACCESS );
    return FDR_BadError ;
  }

  flptr = (&decode_filter) ;
  *flptr = *nflptr ;
  theIUnderFile( flptr ) = (&string_filter) ;

  /* Init the filter; this checks the top of the stack for an optional dict */
  param_dict = fast_extract_hash_name( &internaldict, NAME_FONTDecodeParams );
  if ( param_dict == NULL ||
       oType(*param_dict) != ODICTIONARY ||
       !dictmatch(param_dict, fontdecodematch) ) {
    (void)error_handler( INVALIDFONT );
    return FDR_BadError ;
  }
  oInteger(*fontdecodematch[fdm_Strategy].result) = strategy ;

  if ( !filter_create_hook(flptr, param_dict, NULL) )
    return FDR_BadError ;

  if ( ! (*theIMyInitFile( flptr ))( flptr , param_dict , NULL ))  {
    ( void )( *theIFileLastError( flptr ))( flptr ) ;
    return FDR_BadError ;
  }

  /* Now decode the string... */
  dstring = oString(*stringnew) ;
  ldstring = theLen(*stringnew) ;
  bytessofar = 0 ;
  do {
    if ( ! (*theIFilterDecode( flptr ))( flptr , & bytes )) {
      if ( new_dstring ) mm_free_with_header(mm_pool_temp, new_dstring) ;
      ( void )(*theIMyCloseFile( flptr ))( flptr, CLOSE_EXPLICIT ) ;
      if ( isIIOError( flptr ))
        return FDR_Fail ;      /* maybe just not that strategy/encoding */
      else {
        ( void )( *theIFileLastError( flptr ))( flptr );
        return FDR_BadError ;
      }
    }
    if ( bytes ) {
      if ( bytes + bytessofar > ldstring ) {
        int32 sizemem ;
        uint8 *newmem ;
        sizemem = bytes + bytessofar < 2 * ldstring ? 2 * ldstring : bytes + bytessofar ;
        if (( newmem = mm_alloc_with_header(mm_pool_temp, (mm_size_t) sizemem,
                                            MM_ALLOC_CLASS_ADOBE_DECODE)) == NULL ) {
          if ( new_dstring ) mm_free_with_header(mm_pool_temp, new_dstring ) ;
          ( void )(*theIMyCloseFile( flptr ))( flptr, CLOSE_EXPLICIT ) ;
          (void)error_handler( VMERROR );
          return FDR_BadError ;
        }
        HqMemCpy( newmem , dstring , bytessofar ) ;
        ldstring = sizemem ;
        if ( new_dstring ) mm_free_with_header(mm_pool_temp, new_dstring ) ;
        new_dstring = dstring = newmem ;
      }
      HqMemCpy( dstring + bytessofar , theIBuffer( flptr ) , bytes ) ;
      bytessofar += bytes ;
    }
  } while ( bytes != 0 ) ;

  /* Close the filter and return the decoded string */
  ( void )(*theIMyCloseFile( flptr ))( flptr, CLOSE_EXPLICIT ) ;

  theLen(*stringnew) = CAST_TO_UINT16( bytessofar );
  oString(*stringnew) = dstring ;

  return FDR_Success ;
}

Bool adobe_cache_encrypted(corecontext_t *context,
                           charcontext_t *charcontext,
                           int32 protection,
                           LINELIST *currpt,
                           Bool xps)
{
  int32 strategy, nextstrategy ;
  Bool result = TRUE ;
#define TYPE1_TYPICAL_STRING_LEN 1024
  uint8 tmpstring[ TYPE1_TYPICAL_STRING_LEN ] ;
  uint8 *newslist = tmpstring ;
  OBJECT decrypt_string = OBJECT_NOTVM_NOTHING,
    original_string = OBJECT_NOTVM_NOTHING ;

  static int32 LastFontDecodeStrategy = 1;

  /* We save the current character context, and replace some fields
     temporarily. We don't redirect the char context to the local context
     copy, because this will become impossible to discover from nested
     routines when targets (12145) are complete. We can't restore the whole
     of the old context because the cptr, xwidth, and ywidth will have been
     updated. */
  HQASSERT(charcontext, "No character context") ;
  Copy(&original_string, &charcontext->definition) ;

  HQASSERT(protection == PROTECTED_NONE || protection == PROTECTED_MRSWA ||
           protection == PROTECTED_HQXRUN || protection == PROTECTED_ATL,
           "Not protected in expected way") ;

  if ( oType(original_string) != OSTRING )
    return error_handler( INVALIDFONT ) ;

  Copy(&decrypt_string, &original_string) ;

  if ( protection == PROTECTED_MRSWA ) {
    int32 slen = theLen(original_string) ;

    if ( slen > TYPE1_TYPICAL_STRING_LEN &&
         NULL == (newslist = (uint8 *)mm_alloc(mm_pool_temp, (mm_size_t) slen,
                                               MM_ALLOC_CLASS_ADOBE_DECODE)) )
      return error_handler( VMERROR );

    result = MPS_decrypt(oString(original_string), slen, newslist) ;

    oString(decrypt_string) = newslist ;

    if ( context->fontsparams->MPSOutlines )
      protection = FALSE ;
  }
  else if ( protection == PROTECTED_ATL ) {
    int32 slen = theLen(original_string) ;

    if ( slen > TYPE1_TYPICAL_STRING_LEN &&
         NULL == (newslist = (uint8 *)mm_alloc(mm_pool_temp, (mm_size_t) slen,
                                               MM_ALLOC_CLASS_ADOBE_DECODE)) )
      return error_handler( VMERROR );

    result = eCC_decrypt(oString(original_string), slen, newslist) ;

    oString(decrypt_string) = newslist ;
  }

  if ( result ) {
    OCopy(charcontext->definition, decrypt_string) ;
    result = adobe_cache(context, charcontext, protection, currpt, xps) ;
    OCopy(charcontext->definition, original_string) ;
  }

  if ( newslist != tmpstring )
    mm_free(mm_pool_temp, (mm_addr_t)newslist, theLen(original_string)) ;

  if ( result )
    return TRUE ;

  strategy = LastFontDecodeStrategy;
  nextstrategy = MINPOSSIBLESTRATEGY ;

  do {
    HQASSERT( PROTECTION(strategy) >= PROTECTED_MINSTRAT,
              "Last Strategy underflow wrt protn" );
    HQASSERT( PROTECTION(strategy) <= PROTECTED_MAXSTRAT,
              "Last Strategy overflow wrt protn" );
    HQASSERT( PROTECTION(strategy), "Last Strategy not true for protection" );

    theLen(decrypt_string) = TYPE1_TYPICAL_STRING_LEN ;
    oString(decrypt_string) = tmpstring ;

    result = execFontDecodeFilter(&original_string, &decrypt_string, strategy) ;
    switch (result) {
    case FDR_Success:          /* go on to try to decode it (might fail) */
      OCopy(charcontext->definition, decrypt_string) ;
      result = adobe_cache(context, charcontext, PROTECTION(strategy), currpt, xps) ;
      OCopy(charcontext->definition, original_string) ;
      break;
    case FDR_Fail:             /* try next strategy instead */
      result = FALSE;
      break;
    case FDR_NoFilter: /* we can't decode this string, so return */
    case FDR_BadError: /* the error on up... */
      return FALSE;
    }

    /* Alloc by font decode filter */
    if (oString(decrypt_string) != tmpstring )
      mm_free_with_header(mm_pool_temp, oString(decrypt_string) ) ;

    if ( result ) {
      LastFontDecodeStrategy = strategy ;
      return TRUE ;
    }

    strategy = nextstrategy++ ;
    if ( strategy == LastFontDecodeStrategy )
      strategy = nextstrategy++ ;
  } while ( strategy <= MAXPOSSIBLESTRATEGY ) ;

  return error_handler(INVALIDFONT) ;
}

/** CCRun, eCCRun, and MPSRun need to interpret Type 1 charstrings inside Type
   4 fonts. The rest of the font structure is the same as a Type 1. At the
   moment, there is a special case in decode_adobe1_outline to cope with
   SEAC in a Type 4. It would be nice to get rid of this by using the SEAC
   methods, but Type 4 needs to do a recursive plotchar rather than access
   a sub-charstring. Make a copy of the PS type 1 methods, and use these
   to decrypt a string passed to the CCRun procedure. */
Bool adobe_cache_type4(corecontext_t *context,
                       charcontext_t *charcontext,
                       FONTinfo *fontInfo,
                       OBJECT *stringo,
                       int32 protection,
                       LINELIST *currpt)
{
  Bool result ;
  charcontext_t ocharcontext ;
  charstring_methods_t t4_methods ;

  /* We save the current character context, and replace some fields
     temporarily. We don't redirect the char context to the local context
     copy, because this will become impossible to discover from nested
     routines when targets (12145) are complete. We can't restore the whole
     of the old context because the cptr, xwidth, and ywidth will have been
     updated. */
  HQASSERT(charcontext, "No character context") ;
  ocharcontext = *charcontext ;

  HQASSERT(stringo, "No Type 1 character string") ;

  /* Make a copy of the Type 1 method functions and set the data pointer */
  t4_methods = pstype1_charstring_fns ;
  t4_methods.data = fontInfo ;
  charcontext->methods = &t4_methods ;
  charcontext->chartype = CHAR_Type1 ;
  OCopy(charcontext->definition, *stringo) ;

  result = adobe_cache_encrypted(context, charcontext, protection, currpt, FALSE) ;

  charcontext->methods = ocharcontext.methods ;
  charcontext->chartype = ocharcontext.chartype ;
  charcontext->definition = ocharcontext.definition ;

  return result ;
}

/* ----------------------------------------------------------------------------
   function:            eCC_decrypt(..)    author:              Angus Duggan
   creation date:       18 Jul 1994        last modification:   ##-###-####
   arguments:           type .
   description:

   Decrypts an Adobe-Morisawa encrypted string, using eexec-like method

---------------------------------------------------------------------------- */
static Bool eCC_decrypt(uint8 *from, int32 len, uint8 *to)
{
  int32 eCC_decrypt_state = 54261 ; /* Decryption seed. */

  while ( len-- ) {
    register int32 in ;
    register int32 out ;

    in = ( int32 )( *from++ ) ;
    out = DECRYPT_BYTE( in , eCC_decrypt_state ) ;
    DECRYPT_CHANGE_STATE( in ,
                          eCC_decrypt_state ,
                          21483 , /* Decryption adder */
                          16477 ) ; /* Decryption multiplier */
    *to++ = ( uint8 ) out ;
  }

  return TRUE ; /* Charstring is now decrypted. */
}


/* -------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
   function:            adobe_cache(..)    author:              Andrew Cave
   creation date:       06-Oct-1987        last modification:   ##-###-####
   arguments:           type .
   description:

   Decodes an Adobe Type 1 font, and caches it for ScriptWorks.

---------------------------------------------------------------------------- */
static charstring_build_t *ch_build = NULL ;

Bool adobe_cache(corecontext_t *context,
                 charcontext_t *charcontext,
                 int32 protection,
                 LINELIST *currpt,
                 Bool xps)
{
  int32 mflags;
  SYSTEMVALUE metrics[ 10 ] ;
  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;
  int32 plottype ;
  OBJECT *stringo ;

  HQASSERT(charcontext, "No character context") ;
  HQASSERT(charcontext == char_current_context(),
           "Character context is not current") ;
  plottype = charcontext->modtype ;
  stringo = &charcontext->definition ;

  if ( isEncrypted(*fontInfo) &&
       (isEncrypted(*fontInfo) != PROTECTED_MRSWA ||
        context->fontsparams->MPSOutlines) ) {
    if ( ! protection )
      protection = isEncrypted(*fontInfo) ;
    else if ( isEncrypted(*fontInfo) != (uint8) protection )
      protection = PROTECTED_BLANKET;
  }

  if (oType(*stringo) != OSTRING)
    return error_handler( INVALIDFONT ) ;

  /* Extract any metric information. */
  mflags = 0;
  if ( plottype != DOTYPE4SHOW ) {
    if ( ! get_metrics(&charcontext->glyphname, metrics, & mflags) )
      return error_handler( INVALIDFONT ) ;

    /* [Bug #4546] at this point, metrics contains:
     * [ 0 ] : MF_W0 => x-width                         else unset thoughout
     * [ 1 ] : MF_W0 => y-width
     * [ 2 ] : MF_LL => new lhs (x) of bbox
     * [ 3 ] : MF_UR => translation (y) of bbox
     * [ 4 ] : unset
     * [ 5 ] : unset
     * [ 6 ] : MF_W1 => x-width-v
     * [ 7 ] : MF_W1 => y-width-v
     * [ 8 ] : MF_VO => x-vec-oH-oV
     * [ 9 ] : MF_VO => y-vec-oH-oV
     *
     * also, UR => LL => W0
     *       W1 <=> VO
     */

    /* If metrics (width) & stringwidth, and no cdevproc, done enough. */
    if ( plottype == DOSTRINGWIDTH ) {
      if (oType( theCDevProc( *fontInfo ) ) == ONULL ) {
        /* then NO CDevProc, so no new info will be forthcoming */
        SYSTEMVALUE *wxy = NULL ;

        if ( theWMode(*fontInfo) != 0 && (mflags & MF_W1) != 0 )
          wxy = &metrics[6] ; /* wmode 1, W1 is defined */
        else if ( (mflags & MF_W0) != 0 )
          wxy = &metrics[0] ; /* wmode 0 or W1 is undefined; use W0 widths */

        if ( wxy != NULL ) {
          COMPUTE_STRINGWIDTH(wxy, charcontext) ;
          return TRUE ;
        }
      }
    }
  }

  /* Initialise the hinting and char builder of the adobe outline.
     DOTYPE4SHOW is used for Type 4 SEAC characters. In this case, we need to
     go out to the interpreter to get the sub-charstring. We do not want to
     destroy the existing path created before this SEAC character, so mark
     the start of the current SEAC sub-path, and keep the path intact. */
  if ( plottype != DOTYPE4SHOW ) {
    charstring_build_t ch_data, *ch_save = ch_build ;
    PATHINFO adobe_path ;
    ch_float axwidth, aywidth ;
    int32 hint ;
    Bool result ;
    charstring_decode_fn decoder ;
    double maxglyphsize, thisglyphsize;
    uint32 accurate = 0;

    if ( !char_accurate(&thisglyphsize, &accurate) )
      return FALSE ;

    hint = (int32)context->fontsparams->HintedFonts;
    if ( hint != 0 && hint != 255 ) {
      /* then we must do arithmetic */
      maxglyphsize = hint * 10.0;
      maxglyphsize *= maxglyphsize;

      hint = thisglyphsize <= maxglyphsize;
    }

    if ( charcontext->chartype == CHAR_Type2 ) {
      decoder = decode_adobe2_outline ;
    } else {
      HQASSERT(charcontext->chartype == CHAR_Type1, "Not a Type 1 charstring") ;
      decoder = decode_adobe1_outline ;
    }

    ch_build = &ch_data ;
    path_init(&adobe_path) ;
    axwidth = aywidth = 0 ;

    if ( hint ) {
      result = t1hint_build_path(context,
                                 charcontext->methods, stringo, decoder,
                                 &ch_data, &ch_data,
                                 &adobe_path, &axwidth, &aywidth) ;
    } else {
      result = ch_build_path(context,
                             charcontext->methods, stringo, decoder,
                             &ch_data, &ch_data,
                             &adobe_path, &axwidth, &aywidth) ;
    }

    ch_build = ch_save ;

    if ( result ) {
      sbbox_t abbox ;
      Bool t1_marked ;
      PATHLIST *thepath ;
      LINELIST *theline ;
      SYSTEMVALUE *bbindexed ;

      /* Decode string's width. */
      if ( ! ( mflags & MF_W0 )) {
        metrics[ 0 ] = (SYSTEMVALUE)axwidth ;
        metrics[ 1 ] = (SYSTEMVALUE)aywidth ;
        mflags |= MF_W0 ;
      }

      /* Detect blank characters and set the bbox to zero. Blank characters
         have either no path, or MOVETO followed by a CLOSEPATH or
         MYCLOSE. */
      if ( (thepath = adobe_path.firstpath) != NULL &&
           (theline = theSubPath(*thepath)) != NULL &&
           (theline = theline->next) != NULL &&
           theLineType(*theline) != CLOSEPATH &&
           theLineType(*theline) != MYCLOSE ) {
        t1_marked = TRUE ;
        (void)path_bbox(&adobe_path, &abbox, BBOX_IGNORE_NONE|BBOX_LOAD) ;
      } else {
        t1_marked = FALSE ;
        bbox_store(&abbox, 0, 0, 0, 0) ;
      }

      bbox_as_indexed(bbindexed, &abbox) ;

      /* XPS vertical metric calculation */
      if ( xps &&
           theWMode(*fontInfo) != 0 &&
           !tt_xps_metrics( fontInfo, &charcontext->glyphname,
                            bbindexed, metrics, &mflags) )
        return error_handler( INVALIDFONT ) ;

      /* Adjust for CDevProc, and calculate set width */
      result = char_metrics(charcontext, metrics, &mflags, bbindexed) ;

      if ( result && plottype != DOSTRINGWIDTH ) {
        SYSTEMVALUE xoff = 0, yoff = 0 ;
        CHARCACHE *cptr = NULL ;

        /* See if can cache it - insert into top & second level cache. */
        if ( plottype != DOCHARPATH ) {
          SYSTEMVALUE bearings[ 4 ] ;
          char_bearings(charcontext, bearings, bbindexed, &theFontMatrix(*fontInfo)) ;
          cptr = char_cache(charcontext, metrics, mflags, bearings, !t1_marked) ;
          xoff = bearings[0] ;
          yoff = bearings[1] ;
        }

        adobe_path.protection = CAST_TO_UINT8(protection); /* can be FALSE == PROTECTED_NONE */

        result = char_draw(charcontext, currpt, cptr, metrics, mflags,
                           xoff, yoff, !t1_marked, accurate,
                           &adobe_path, &theFontMatrix(*fontInfo)) ;
      }
    }

    path_free_list(adobe_path.firstpath, mm_pool_temp) ;

    return result ;
  } else {
    Bool closed ;
    HQASSERT(charcontext->chartype == CHAR_Type1,
             "Not a Type 1 charstring in a Type 4 SEAC") ;
    HQASSERT(ch_build, "No char builder for Type 4 SEAC") ;
    /* Must have a pre-existing char builder from the enclosing plotchar. */
    return decode_adobe1_outline_recursive(context, charcontext->methods,
                                           oString(*stringo), theLen(*stringo),
                                           ch_build, 0, &closed) ;
  }
  /* NOTREACHED */
}

/* ----------------------------------------------------------------------------
   function:          decode_adobe_outline(..) author:              Andrew Cave
   creation date:     08-Mar-1989              last modification:   ##-###-####
   arguments:         slist , slen , recursive , ax , ay .
   description:

   Decodes a single byte stream of an Adobe Type 1 font outline.

---------------------------------------------------------------------------- */
#define TYPE1_VSLIMIT 24
#define TYPE1_CSLIMIT 24
#define TYPE1_FSLIMIT 14

/* Collected information required to decode a Type 1 charstring. */
typedef struct {
  int32 R_value ;
  int32 V_index, C_index, F_index ;

  double V_stack[ TYPE1_VSLIMIT ] ;
  double C_stack[ TYPE1_CSLIMIT ] ;
  double F_stack[ TYPE1_FSLIMIT ] ;

  Bool flexing ;
  Bool initial_point_set ;

  double axlsbpn, aylsbpn ;
  double axpostn, aypostn ;

  Bool axwidthbear ;
  int32 numstems ;
  uint32 cntrgroup ;
} adobe1_info_t ;

static adobe1_info_t *adobe1_info = NULL ;

/* Decode a single Adobe 1 charstring, including recursive calls to Type 4
   SEACs and other subrs/SEACs. Recursive calls enter through
   decode_adobe1_outline_recursive(). */
static Bool decode_adobe1_outline(corecontext_t *context,
                                  charstring_methods_t *t1fns,
                                  OBJECT *stringo,
                                  charstring_build_t *buildfns)
{
  adobe1_info_t adobe1_build = {0}, *adobe1_save = adobe1_info ;
  Bool result ;
  Bool closed = TRUE ;

  adobe1_info = &adobe1_build ;
  result = decode_adobe1_outline_recursive(context, t1fns,
                                           oString(*stringo), theLen(*stringo),
                                           buildfns, 0, &closed) ;
  adobe1_info = adobe1_save ;

  return result ;
}


/* Ensure that PaintType 1 paths are fully closed */
#define AUTOCLOSE(c) MACRO_START \
  if (autoclose && !*closed) {\
    HQTRACE( debug_hint_decode, ( "Automatic CLSPATH" )) ;\
    if ( ! (*buildfns->closepath)(buildfns->data))\
      return FALSE ;\
    *closed = c ;\
  } MACRO_END


static Bool decode_adobe1_outline_recursive(corecontext_t *context,
                                            charstring_methods_t *t1fns,
                                            uint8 *slist, uint32 slen,
                                            charstring_build_t *buildfns,
                                            int32 recurse_level,
                                            Bool *closed)
{
  Bool res , autoclose ;
  int32 temp , subno ;
  int32 charno , rcharno ;
  double rcharx , rchary , rcxlsb ;
  ch_float curve[ 12 ] ;
#define FONT_SEED   4330
  uint16 font_decrypt_state = FONT_SEED ;

  int32 i, numargs ;
  int32 result ;
  int32 nextbyte ;
  uint8 *slimit ;

  register double *stack ;
  register adobe1_info_t *info = adobe1_info ;

  double *C_stack, *F_stack ;

  ch_float txlsbpn = 0 , tylsbpn = 0 ;

  uint8 *subrstr ;
  uint32 subrlen ;
  int32 lenIV = 4 ; /* Default lenIV specified by Type 1 font format */

  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;
  autoclose = (fontInfo->painttype == 0) ;

  HQASSERT(t1fns, "No charstring methods") ;

  HQASSERT(info, "Not decoding a Type 1 charstring") ;
  stack = info->V_stack ;
  C_stack = info->C_stack ;
  F_stack = info->F_stack ;

  if ( recurse_level == 0 ) {
    /* Always reset the initial point for characters; if a SEAC is called
       with a character that uses an implicit moveto, we'd like to catch that
       too. */
    info->initial_point_set = FALSE ;
    *closed = TRUE ;
  } else if (recurse_level >= 50) {
    /* If the recursion level has gotten up to 50, then the SEAC/SUBRS
       recursion limit has been exceeded. Quietly return */
    return TRUE;
  }

  /* The initialisation vector length defaults to 4 for Type 1 charstrings. */
  {
    OBJECT info = OBJECT_NOTVM_NOTHING ;

    if ( ! (*t1fns->get_info)(t1fns->data, NAME_lenIV, -1, &info) )
      return FALSE ;

    if ( oType(info) == OINTEGER )
      lenIV = oInteger(info) ;
  }

  /* The string length won't be smaller than lenIV if lenIV is negative
     (ignored). */
  if ( (int32)slen < lenIV )
    return error_handler( INVALIDFONT ) ;

  if ( lenIV >= 0 ) {
    /* Start off decryption, seeding with first lenIV bytes. */
    seed_font_decryption(slist, lenIV, &font_decrypt_state) ;
    slist += lenIV ;
    slen  -= lenIV ;
  }

  slimit = slist + slen ;

  /* Deal with next byte. */
  while ( slist < slimit ) {
    nextbyte = char_font_decryption(slist++, lenIV, &font_decrypt_state) ;
    if ( nextbyte >= 32 ) {
      if ( nextbyte > 250 ) {
        if ( nextbyte == 255 ) {
          /* Check that four bytes left. */
          if (( slimit - slist ) < 4 )
            return error_handler( INVALIDFONT ) ;
          /* result := (( 256 * byte1 + byte2 ) * 256 + byte3 ) * 256 + byte4 */
          result = 0 ;
          for ( i = 4 ; i > 0 ; --i ) {
            result <<= 8 ;
            result |= char_font_decryption(slist++, lenIV, &font_decrypt_state) ;
          }
        }
        else {
          /* result := 64148 - (( 256 * byte0 ) + byte1 ) */
          result = ( nextbyte << 8 ) ;
          result |= char_font_decryption(slist++, lenIV, &font_decrypt_state) ;
          result -= 64148 ;
          result = -result ;
        }
      }
      /* result := ( 256 * byte0 ) + byte1 - 63124 */
      else if ( nextbyte > 246 ) {
        result = ( nextbyte << 8 ) ;
        result |= char_font_decryption(slist++, lenIV, &font_decrypt_state) ;
        result -= 63124 ;
      }
      else {
        /* result := byte0 - 139 */
        result = nextbyte - 139 ;
      }
      if ( info->V_index >= TYPE1_VSLIMIT )
        return FAILURE(FALSE) ;
      HQTRACE( debug_path_values, ( "result: %d", result )) ;
      stack[ info->V_index++ ] = result ;
    }
    else { /* Byte selector */
      /* Check correct number of args. */
      HQTRACE( debug_hint_decode, ( "Byte selector: %d (%d)", nextbyte, info->V_index )) ;
      HQTRACE( debug_hint_decode,
              ("Stack %f, %f, %f, %f, %f, %f",
                stack[0], stack[1], stack[2], stack[3], stack[4], stack[5] )) ;

      switch ( nextbyte ) {
        int32 edgestem ;
      case HSTEM :
        HQTRACE( debug_hint_decode, ( "HSTEM y %f + %f", stack[0], stack[1] )) ;
        /* Type 1 edge stems are not specifically distinguished as top or
           bottom stems, as Type 2 stems are. They must fall within the
           coordinates of the character, but that is too hard to check here.
           We'll mark it as both a top and a bottom stem; the hinter can then
           decide whether to limit the effect to one edge. */
        edgestem = (stack[1] == 20 || stack[1] == 21) ;
        if ( !(*buildfns->hstem)(buildfns->data, stack[0], stack[0] + stack[1],
                                 edgestem, edgestem, info->numstems++) )
          return FALSE ;

        info->V_index = 0 ;
        break ;

      case VSTEM :
        HQTRACE( debug_hint_decode, ( "VSTEM x %f + %f", stack[0], stack[1] )) ;
        /* Type 1 edge stems are not specifically distinguished as left or
           right stems, as Type 2 stems are. They must fall within the
           coordinates of the character, but that is too hard to check here.
           We'll mark it as both a top and a bottom stem; the hinter can then
           decide whether to limit the effect to one edge. */
        edgestem = (stack[1] == 20 || stack[1] == 21) ;
        if ( !(*buildfns->vstem)(buildfns->data, stack[0], stack[0] + stack[1],
                                 edgestem, edgestem, info->numstems++) )
          return FALSE ;

        info->V_index = 0 ;
        break ;

      case VMOVETO :
        info->aypostn += stack[ 0 ] ;
        HQTRACE( debug_hint_decode, ( "VMOVETO" )) ;
        if ( info->flexing ) {
          HQTRACE( debug_hint_decode, ( "Flex VMOVETO" )) ;
          ADD_FLEX_MOVETO(info, info->axpostn , info->aypostn ) ;
        } else {
          AUTOCLOSE(FALSE) ;
          if ( !(*buildfns->moveto)(buildfns->data, info->axpostn, info->aypostn) )
            return FALSE ;
        }

        info->V_index = 0 ;
        info->initial_point_set = TRUE;
        break ;

      case RLINETO :
        CHECK_FOR_INITIAL_MOVE(info, buildfns) ;

        info->axpostn += stack[ 0 ] ;
        info->aypostn += stack[ 1 ] ;
        HQTRACE( debug_hint_decode, ( "RLINETO" )) ;

        if ( !(*buildfns->lineto)(buildfns->data, info->axpostn, info->aypostn) )
          return FALSE ;

        info->V_index = 0 ;
        break ;

      case HLINETO :
        CHECK_FOR_INITIAL_MOVE(info, buildfns) ;

        info->axpostn += stack[ 0 ] ;
        HQTRACE( debug_hint_decode, ( "HLINETO" )) ;

        if ( !(*buildfns->lineto)(buildfns->data, info->axpostn, info->aypostn) )
          return FALSE ;

        info->V_index = 0 ;
        break ;

      case VLINETO :
        CHECK_FOR_INITIAL_MOVE(info, buildfns) ;

        info->aypostn += stack[ 0 ] ;
        HQTRACE( debug_hint_decode, ( "VLINETO" )) ;

        if ( !(*buildfns->lineto)(buildfns->data, info->axpostn, info->aypostn) )
          return FALSE ;

        info->V_index = 0 ;
        break ;

      case RRCURVETO :
        CHECK_FOR_INITIAL_MOVE(info, buildfns) ;

        curve[ 0 ] = info->axpostn + stack[ 0 ] ;
        curve[ 1 ] = info->aypostn + stack[ 1 ] ;
        curve[ 2 ] = curve[ 0 ] + stack[ 2 ] ;
        curve[ 3 ] = curve[ 1 ] + stack[ 3 ] ;
        curve[ 4 ] = curve[ 2 ] + stack[ 4 ] ;
        curve[ 5 ] = curve[ 3 ] + stack[ 5 ] ;
        HQTRACE( debug_hint_decode, ( "RRCURVETO" )) ;

        if ( !(*buildfns->curveto)(buildfns->data, curve) )
          return FALSE ;

        info->axpostn = curve[ 4 ] ;
        info->aypostn = curve[ 5 ] ;

        info->V_index = 0 ;
        break ;

      case CLSPATH :
        HQTRACE( debug_hint_decode, ( "CLSPATH" )) ;

        if ( ! (*buildfns->closepath)(buildfns->data))
          return FALSE ;

        info->V_index = 0 ;
        *closed = TRUE ;

        /* [64805] closepath should preserve the currentpoint. Ensure that the
         * rip path being built reflects this.
         */
        if ( ! (buildfns->moveto)(buildfns->data, info->axpostn, info->aypostn))
          return FALSE;

        break ;

      case CALLSUBR :
        HQTRACE( debug_hint_decode, ( "CALLSUBR" )) ;
        if ( info->V_index == 0 )
          return FAILURE(FALSE) ;
        subno = (int32)stack[ --info->V_index ] ;
        HQTRACE( debug_path_type || debug_hint_decode, ( "subroutine %d", subno )) ;

        if ( !(*t1fns->begin_subr)(t1fns->data, subno, FALSE, &subrstr, &subrlen) )
          return FALSE ;

        res = decode_adobe1_outline_recursive(context, t1fns, subrstr, subrlen, buildfns,
                                              recurse_level + 1, closed) ;

        (*t1fns->end_subr)(t1fns->data, &subrstr, &subrlen) ;

        if ( !res )
          return FAILURE(FALSE) ;

        if ( info->R_value )
          return TRUE ;

        break ;

      case RETURN :
        HQTRACE( debug_hint_decode, ( "RETURN" )) ;
        return TRUE ;
        /*break;*/

      case T1ESCAPE :
        HQTRACE( debug_hint_decode, ( "T1ESCAPE" )) ;
        /* Check that at least one byte left in string. */
        if ( slimit == slist )
          return FAILURE(FALSE) ;
        temp = char_font_decryption(slist++, lenIV, &font_decrypt_state) ;
        HQTRACE( debug_path_type || debug_hint_decode,
                ( "Recursive byte selector %d", temp )) ;
        switch ( temp ) {
          uint32 group ;
        case EDOTSECTION :
          /* These should appear in pairs, shrouding the charstring
           * that defines the dot.
           */
          HQTRACE( debug_hint_decode, ( "EDOTSECTION" )) ;

          if ( !(*buildfns->dotsection)(buildfns->data) )
            return FALSE ;

          info->V_index = 0 ;
          break ;

        case EVSTEM3 :
          HQTRACE( debug_hint_decode, ( "EVSTEM3" )) ;
          group = ++info->cntrgroup ;
          for ( i = 0 ; i < 6 ; i += 2, ++info->numstems ) {
            if ( !(*buildfns->vstem)(buildfns->data,
                                     stack[i], stack[i] + stack[i + 1],
                                     FALSE, FALSE, info->numstems) ||
                 !(*buildfns->cntrmask)(buildfns->data,
                                        info->numstems, group) )
              return FALSE ;
          }
          if ( !(*buildfns->cntrmask)(buildfns->data, -1, group) )
            return FALSE ;

          info->V_index = 0 ;
          break ;

        case EHSTEM3 :
          HQTRACE( debug_hint_decode, ( "EHSTEM3" )) ;
          group = ++info->cntrgroup ;
          for ( i = 0 ; i < 6 ; i += 2, ++info->numstems ) {
            if ( !(*buildfns->hstem)(buildfns->data,
                                     stack[i], stack[i] + stack[i + 1],
                                     FALSE, FALSE, info->numstems) ||
                 !(*buildfns->cntrmask)(buildfns->data,
                                        info->numstems, group) )
              return FALSE ;
          }
          if ( !(*buildfns->cntrmask)(buildfns->data, -1, group) )
            return FALSE ;

          info->V_index = 0 ;
          break ;

        case ESEAC :
          HQTRACE( debug_hint_decode, ( "ESEAC" )) ;
          rcxlsb = stack[ 0 ] ;
          rcharx = stack[ 1 ] ;
          rchary = stack[ 2 ] ;
          charno = (int32)stack[ 3 ] ;
          rcharno = (int32)stack[ 4 ] ;
          info->V_index = 0 ;

          txlsbpn = info->axlsbpn ;
          tylsbpn = info->aylsbpn ;
          info->axlsbpn = 0 ;
          info->aylsbpn = 0 ;

          if ( theIFontType(fontInfo) == FONTTYPE_4 ) {
            /* remove current path and move to character origin before
               calling plotcharglyph */

            (void)gs_newpath() ;

            if ( (res = path_moveto(theIgsPageCTM( gstateptr ).matrix[2][0],
                                    theIgsPageCTM( gstateptr ).matrix[2][1],
                                    MOVETO, &thePathInfo(*gstateptr))) != 0) {
              FVECTOR advance;
              char_selector_t seacselect = {0} ;
              /* Save the current char methods around the call to plotchar;
                 a recursive character may alter the private data field. This
                 is ugly, it would be nicer to have a unique structure for
                 each recursive char's methods, but the begin_char/end_char
                 mechanism doesn't lend itself to that without memory
                 allocation. */
              charstring_methods_t saved_methods = *t1fns ;

              seacselect.cid = -1 ;
              seacselect.name = StandardEncoding[charno] ;
              seacselect.string = seacselect.complete =
                seacselect.font = seacselect.cmap =
                seacselect.parent = onull ; /* Struct copy to set slot properties */
              seacselect.index = -1 ;
              seacselect.type3cid = -1 ;

              if ( (res = plotchar(&seacselect, DOTYPE4SHOW, 1, NULL, NULL,
                                   &advance, CHAR_NORMAL)) != 0) {

                info->R_value = FALSE ;

                info->axlsbpn = txlsbpn + ( rcharx - rcxlsb ) ;
                info->aylsbpn = tylsbpn + ( rchary          ) ;

                /* remove current path and move to character origin before
                   calling plotcharglyph */

                (void)gs_newpath() ;

                if ( (res = path_moveto(theIgsPageCTM( gstateptr ).matrix[2][0],
                                        theIgsPageCTM( gstateptr ).matrix[2][1],
                                        MOVETO, &thePathInfo(*gstateptr))) != 0) {
                  /* Restore old methods, because private pointer may have
                     been changed by plotchar */
                  *t1fns = saved_methods;

                  seacselect.cid = -1 ;
                  seacselect.name = StandardEncoding[rcharno] ;
                  res = plotchar(&seacselect, DOTYPE4SHOW, 1, NULL, NULL,
                                 &advance, CHAR_NORMAL) ;
                }
              }
              *t1fns = saved_methods;
            }

            info->R_value = FALSE ;
            *closed = TRUE ;
            return ( res ) ;
          }
          HQTRACE( debug_path_type,
                  ( "recursive char %d (%d @ %f,%f)",charno,rcharno,rcharx,rchary )) ;

          /* Look up first char name in StandardEncoding. */
          if ( !(*t1fns->begin_seac)(t1fns->data, charno,
                                     &subrstr, &subrlen))
            return FALSE ;

          res = decode_adobe1_outline_recursive(context, t1fns, subrstr, subrlen, buildfns,
                                                recurse_level + 1, closed) ;

          (*t1fns->end_seac)(t1fns->data, &subrstr, &subrlen) ;

          if ( !res )
            return FAILURE(FALSE) ;

          info->R_value = FALSE ;

          info->axlsbpn = txlsbpn + ( rcharx - rcxlsb ) ;
          info->aylsbpn = tylsbpn + ( rchary          ) ;

          /* Look up second char name in StandardEncoding. */
          if ( !(*t1fns->begin_seac)(t1fns->data, rcharno,
                                     &subrstr, &subrlen))
            return FALSE ;

          res = decode_adobe1_outline_recursive(context, t1fns, subrstr, subrlen, buildfns,
                                                recurse_level + 1, closed) ;

          (*t1fns->end_seac)(t1fns->data, &subrstr, &subrlen) ;

          if ( !res )
            return FAILURE(FALSE) ;

          info->R_value = FALSE ;

          return TRUE ;
          /*break;*/

        case ESBW :
          HQTRACE( debug_hint_decode, ( "ESBW" )) ;
          if ( !info->axwidthbear ) {
            info->axwidthbear = TRUE ;
            if ( !(*buildfns->setwidth)(buildfns->data, stack[2], stack[3]) )
              return FALSE ;
          }

          info->axlsbpn += stack[0] ;
          info->aylsbpn += stack[1] ;
          if ( !(*buildfns->setbearing)(buildfns->data, info->axlsbpn, info->aylsbpn) )
            return FALSE ;

          info->axpostn = info->aypostn = 0 ;

          info->V_index = 0 ;
          break ;

        case EDIV :
          HQTRACE( debug_hint_decode, ( "EDIV" )) ;
          if ( info->V_index < 2 )
            return FAILURE(FALSE) ;
          i = --info->V_index ;
          if ( stack[i] == 0 )
            return FAILURE(FALSE) ;
          stack[i - 1] /= stack[i] ;
          break ;

        case ECALLOTHER :
          /* If it is using the proc in OtherSubrs[0..2] then
           * flexing is being done.
           * If OtherSubrs[3] is being used then Hints are being changed
           * Anything above this is a generic call to the interpreter
           */
          HQTRACE( debug_hint_decode, ( "ECALLOTHER" )) ;
          /* Push all the args onto our temp stack. */
          if ( info->V_index < 2 )
            return FAILURE(FALSE) ;

          /* Hack: coordinates going out to the othersubr should really be in
             origin-relative space, but they are not, since this code was
             written using sidebearing-relative space. This may need changing
             if we run into problems with it. */
          info->C_index = 0;

          subno = (int32)stack[ --info->V_index ] ;
          numargs = i = (int32)stack[ --info->V_index ] ;
          if (( info->C_index + i ) > TYPE1_CSLIMIT )
            return FAILURE(FALSE) ;
          if ( info->V_index < i )
            return FAILURE(FALSE) ;
          while ( i-- > 0 )
            C_stack[ info->C_index++ ] = stack[ --info->V_index ] ;

          HQTRACE( debug_hint_decode, ( "Call OtherSubr[%i]", subno )) ;

          switch ( subno ) {
            SYSTEMVALUE refx, refy ;
          case 0 :
            HQTRACE(debug_othersubr, ( "case 0 - Flex" )) ;

            /* Build up the two curves from Flex information. */
            if ( info->F_index < 14 )
              return FAILURE(FALSE) ;

            for ( i = 11 ; i >= 0 ; --i )
              curve[ i ] = F_stack[ --info->F_index ] ;

            refy = F_stack[ --info->F_index ] ;
            refx = F_stack[ --info->F_index ] ;
            info->F_index = 0 ;

            /* now get the feature height */
            if ( (refy - curve[ 11 ]) == 0 ) {
              /* then it is a horizontal flex feature */
              if ( !(*buildfns->flex)(buildfns->data,
                                      &curve[0], &curve[6],
                                      refy - curve[ 5 ], stack[0],
                                      TRUE) )
                return FALSE ;
            } else if ( (refx - curve[ 10 ]) == 0 ) {
              /* it should be a vertical flex feature, but check anyway */
              if ( !(*buildfns->flex)(buildfns->data,
                                      &curve[0], &curve[6],
                                      refx - curve[ 4 ], stack[0],
                                      FALSE) )
                return FALSE ;
            } else {
              /* Not horizontal or vertical flex, use two curves */
              if ( !(*buildfns->curveto)(buildfns->data, &curve[0]) ||
                   !(*buildfns->curveto)(buildfns->data, &curve[6]) )
                return FALSE ;
            }
            *closed = FALSE ;

            /* This is a hack. The coordinates given to setcurrentpoint are
               origin-relative rather than sidebearing-relative. Since this
               pair of numbers should only be used by setcurrentpoint, we
               convert them into origin-relative coordinates before putting
               them on the externally-visible PS stack. */
            info->C_index = 2;
            info->axpostn = curve[ 10 ] ;
            C_stack[ 1 ] = info->axpostn + info->axlsbpn ;
            info->aypostn = curve[ 11 ] ;
            C_stack[ 0 ] = info->aypostn + info->aylsbpn ;

            info->flexing = FALSE ;
            break ;

          case 1 :
            HQTRACE(debug_othersubr, ( "case 1 - Flex" )) ;
            info->F_index = 0;
            info->flexing = TRUE ;
            /* Initialise the flex stack - that is all. */
            break ;

          case 2 :
            HQTRACE(debug_othersubr, ( "case 2 - Flex" )) ;
            /* This case is a no-op because flex parameters are collected by
               RMOVETO. This isn't very nice, but it is necessary to stop
               startpathposition being set by the flex rmovetos. */
            break ;

          case 3 :
            HQTRACE(debug_othersubr, ( "case 3 - Change Character hints" )) ;
            if ( !(*buildfns->change)(buildfns->data) )
              return FALSE ;
            break ;

          case 12:
          case 13:
            /* OtherSubrs 12 and 13 are for counter control hints. See
               TN5015, "Type 1 Font Format Supplement". The data format for
               them is:

               #H HG1 HG2 ... HGn #V VG1 VG2 ... VGn m 12/13 callothersubr

               The unpacking loop above reverses the values on C_stack and
               hence the operand stack. Each group has a set of stems. The
               last stem of each group has a negative width. */
            HQTRACE(debug_othersubr, ("case 12/13 - Counter control hints")) ;

            for ( i = 0 ; i < numargs ; ++i ) {
              if ( !stack_push_numeric(C_stack[i], &operandstack) )
                return FALSE ;
            }

            /* The default implementation in TN5015 to ignore these hints are:

               OtherSubrs 12 (pushed on stack):
               {}

               OtherSubrs 13 (removes H groups, V groups):
               { 2 { cvi { { pop 0 lt { exit } if } loop } repeat } repeat } */
            if ( subno == 13 ) {
              Bool done = FALSE ;
              Bool (*stemfn)(void *data, ch_float y1, ch_float y2,
                             Bool tedge, Bool bedge, int32 index) ;

              /* The stem group sets are similar, so we will share code for
                 them, changing only the stem function from H to V. */
              for ( stemfn = buildfns->hstem ; ; stemfn = buildfns->vstem ) {
                int32 ngroups ;

                if ( isEmpty(operandstack) )
                  return error_handler(STACKUNDERFLOW) ;

                /* If the number of groups is a fractional number, this will
                   throw an error where the default OtherSubr replacement
                   would not. I don't think this is a problem, because there
                   are likely to be the wrong number of groups anyway in this
                   case. */
                if ( !object_get_integer(theTop(operandstack), &ngroups) )
                  return FALSE ;

                pop(&operandstack) ;

                while ( --ngroups >= 0 ) {
                  SYSTEMVALUE stem_lo, stem_hi = 0.0 ;
                  uint32 group = ++info->cntrgroup ;
                  int32 index = 0 ;

                  do {
                    if ( theStackSize(operandstack) <= index )
                      return error_handler(STACKUNDERFLOW) ;

                    if ( !object_get_numeric(stackindex(index++, &operandstack),
                                             &stem_lo) )
                      return FALSE ;

                    stem_lo += stem_hi ;

                    if ( !object_get_numeric(stackindex(index++, &operandstack),
                                             &stem_hi) )
                      return FALSE ;

                    stem_hi += stem_lo ;

                    if ( !(*stemfn)(buildfns->data, stem_lo, stem_hi,
                                    FALSE, FALSE, info->numstems) ||
                         !(*buildfns->cntrmask)(buildfns->data,
                                                info->numstems++, group) )
                      return FALSE ;
                  } while ( stem_hi >= stem_lo ) ;

                  if ( !(*buildfns->cntrmask)(buildfns->data, -1, group) )
                    return FALSE ;

                  npop(index, &operandstack) ;
                }

                /* If we've done both H and V, we're done. Otherwise second
                   pass uses V stem function. */
                if ( done )
                  break ;

                done = TRUE ;
              }

              /* OtherSubrs 12/13 must come immediately after hsbw or sbw
                 (TN5015). Since the groups may create overlapping stem
                 hints, we will change the hints to deactivate them all once
                 they have been created. The snapped hints will be
                 re-activated when the new hstem/vstem commands are
                 issued. */
              if ( !(*buildfns->change)(buildfns->data) )
                return FALSE ;
            }

            info->C_index = 0 ;
            break ;

          case 14:
          case 15:
          case 16:
          case 17:
          case 18:
            HQTRACE(debug_othersubr, ( "case 14-18 - MM hints" )) ;
            /** \todo @@@ TODO FIXME ajcd 2003-04-04: OtherSubrs 14-18 are for MM.
               See TN5015, "Type 1 Font Format Supplement". These should be
               implemented in C code. */

            /* FALLTHROUGH */
          default :
            /* Neither Flexing or Hint Changing.
             * So, call the interpreter
             */
            HQTRACE( debug_othersubr,
                     ( "other subr %d: args %d", subno, numargs )) ;
            if ( !handle_othersubrs(context, t1fns, subno, numargs, C_stack) )
              return FALSE ;

            info->C_index = 0 ;
            break;

          }
          break ;

        case EPOP :
          if ( info->C_index == 0 ) {
            SYSTEMVALUE number ;
            HQTRACE( debug_othersubr || debug_hint_decode, ( "epop real stack: " )) ;
            if ( ! stack_get_numeric(&operandstack, &number, 1) )
              return FALSE ;

            pop( & operandstack ) ;

            stack[ info->V_index++ ] = number ;
            HQTRACE( debug_othersubr || debug_hint_decode,
                    ( " result %f (%g)", stack[ info->V_index - 1 ], number )) ;
          }
          else {
            HQTRACE( debug_othersubr || debug_hint_decode,
                    ( "epop index %d value %f", info->C_index, C_stack[ info->C_index - 1 ] )) ;
            if ( info->C_index <= 0 )
              return FAILURE(FALSE) ;
            stack[ info->V_index++ ] = C_stack[ --info->C_index ] ;
          }
          break ;

        case 32 :
          HQTRACE( debug_hint_decode, ( "case 32 - What ???" )) ;
          /* Ignore setcurrentpoint's - hinting only. */
          /* What does this do ? gn */

          info->V_index = 0 ;
          break ;

        case ESETCPOINT :
          /* The coordinates given to setcurrentpoint are origin-relative,
             rather than corner of the character-relative, so remove the
             sidebearings to convert them into internal coordinates. */
          info->axpostn = stack[ 0 ] - info->axlsbpn ;
          info->aypostn = stack[ 1 ] - info->aylsbpn ;

          info->V_index = 0 ;
          break ;

        default:
          HQTRACE( debug_hint_decode, ( "unknown escape code 12 %d", temp )) ;
          break;
        }
        break ;

      case HSBW :
        HQTRACE( debug_hint_decode, ( "HSBW" )) ;
        if ( !info->axwidthbear ) {
          info->axwidthbear = TRUE ;
          if ( !(*buildfns->setwidth)(buildfns->data, stack[1], 0) )
            return FALSE ;
        }

        info->axlsbpn += stack[0] ;
        if ( !(*buildfns->setbearing)(buildfns->data, info->axlsbpn, info->aylsbpn) )
          return FALSE ;

        info->axpostn = info->aypostn = 0 ;

        info->V_index = 0 ;
        break ;

      case ENDCHAR :
        HQTRACE( debug_hint_decode, ( "ENDCHAR" )) ;

        info->V_index = 0 ;
        info->R_value = TRUE ;

        if ( recurse_level == 0 )
          AUTOCLOSE(TRUE) ;

        return TRUE ;
        /*break;*/

      case 15 :
        HQTRACE( debug_check_absolute || debug_hint_decode, ( "Check 15" )) ;
        info->axpostn = stack[ 0 ] ;
        info->aypostn = stack[ 1 ] ;

        if ( info->flexing ) {
          HQTRACE( debug_hint_decode, ( "Flex 15" )) ;
          ADD_FLEX_MOVETO(info, info->axpostn , info->aypostn ) ;
        } else {
          AUTOCLOSE(FALSE) ;
          if ( !(*buildfns->moveto)(buildfns->data, info->axpostn, info->aypostn) )
            return FALSE ;
        }

        info->V_index = 0 ;
        info->initial_point_set = TRUE;
        break ;

      case 16 :
        HQTRACE( debug_check_absolute || debug_hint_decode, ( "Check 16" )) ;
        CHECK_FOR_INITIAL_MOVE(info, buildfns) ;

        info->axpostn = stack[ 0 ] ;
        info->aypostn = stack[ 1 ] ;

        if ( !(*buildfns->lineto)(buildfns->data, info->axpostn, info->aypostn) )
          return FALSE ;

        info->V_index = 0 ;
        break ;

      case 17 :
        HQTRACE( debug_check_absolute || debug_hint_decode, ( "Check 17" )) ;
        CHECK_FOR_INITIAL_MOVE(info, buildfns) ;

        if ( !(*buildfns->curveto)(buildfns->data, stack) )
          return FALSE ;

        info->axpostn = stack[ 4 ] ;
        info->aypostn = stack[ 5 ] ;

        info->V_index = 0 ;
        break ;

      case RMOVETO :
        HQTRACE( debug_hint_decode, ( "RMOVETO" )) ;
        info->axpostn += stack[ 0 ] ;
        info->aypostn += stack[ 1 ] ;
        if ( info->flexing ) {
          HQTRACE( debug_hint_decode, ( "Flex RMOVETO" )) ;
          ADD_FLEX_MOVETO(info, info->axpostn , info->aypostn ) ;
        } else {
          AUTOCLOSE(FALSE) ;
          if ( !(*buildfns->moveto)(buildfns->data, info->axpostn, info->aypostn) )
            return FALSE ;
        }

        info->V_index = 0 ;
        info->initial_point_set = TRUE;
        break ;

      case HMOVETO :
        info->axpostn += stack[ 0 ] ;
        HQTRACE( debug_hint_decode, ( "HMOVETO" )) ;
        if ( info->flexing ) {
          HQTRACE( debug_hint_decode, ( "Flex HMOVETO" )) ;
          ADD_FLEX_MOVETO(info, info->axpostn , info->aypostn ) ;
        } else {
          AUTOCLOSE(FALSE) ;
          if ( !(*buildfns->moveto)(buildfns->data, info->axpostn, info->aypostn) )
            return FALSE ;
        }

        info->V_index = 0 ;
        info->initial_point_set = TRUE;
        break ;

      case VHCURVETO :
        HQTRACE( debug_hint_decode, ( "VHCURVETO" )) ;
        CHECK_FOR_INITIAL_MOVE(info, buildfns) ;

        curve[ 0 ] = info->axpostn ;
        curve[ 1 ] = info->aypostn + stack[ 0 ] ;
        curve[ 2 ] = info->axpostn + stack[ 1 ] ;
        curve[ 3 ] = curve[ 1 ] + stack[ 2 ] ;
        curve[ 4 ] = curve[ 2 ] + stack[ 3 ] ;
        curve[ 5 ] = curve[ 3 ] ;

        if ( !(*buildfns->curveto)(buildfns->data, curve) )
          return FALSE ;

        info->axpostn = curve[ 4 ] ;
        info->aypostn = curve[ 5 ] ;

        info->V_index = 0 ;
        break ;

      case HVCURVETO :
        HQTRACE( debug_hint_decode, ( "HVCURVETO" )) ;
        CHECK_FOR_INITIAL_MOVE(info, buildfns) ;

        curve[ 0 ] = info->axpostn + stack[ 0 ] ;
        curve[ 1 ] = info->aypostn ;
        curve[ 3 ] = info->aypostn + stack[ 2 ] ;
        curve[ 2 ] = curve[ 0 ] + stack[ 1 ] ;
        curve[ 4 ] = curve[ 2 ] ;
        curve[ 5 ] = curve[ 3 ] + stack[ 3 ] ;

        if ( !(*buildfns->curveto)(buildfns->data, curve) )
          return FALSE ;

        info->axpostn = curve[ 4 ] ;
        info->aypostn = curve[ 5 ] ;

        info->V_index = 0 ;
        break ;

      default:
        HQTRACE( debug_hint_decode, ( "unknown byte selector '%d'", nextbyte )) ;
        return FAILURE(FALSE) ;
      }
    }
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:          seed_font_decryption(..) author:              Andrew Cave
   creation date:     08-Mar-1989              last modification:   ##-###-####
   arguments:
   description:

   Seeds the decryption routine for an Adobe Type 1 font.

---------------------------------------------------------------------------- */
static void seed_font_decryption(uint8 *slist, int32 lenIV,
                                 uint16 *font_decrypt_state )
{
  for ( ; lenIV > 0 ; --lenIV ) {
    uint8 temp = *slist++ ;
    DECRYPT_CHANGE_STATE( temp ,
                          *font_decrypt_state ,
                          DECRYPT_ADD ,
                          DECRYPT_MULT ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:          char_font_decryption(..) author:              Andrew Cave
   creation date:     08-Mar-1989              last modification:   ##-###-####
   arguments:
   description:

   Returns the next character in the Adobe Type 1 font stream, and modifies
   the state of the random number encryption technique.

---------------------------------------------------------------------------- */
static uint8 char_font_decryption(uint8 *slist, int32 lenIV,
                                  uint16 *font_decrypt_state)
{
  register uint8 in, out ;

  in = *slist ;
  if ( lenIV >= 0 ) {
    out = DECRYPT_BYTE( in , *font_decrypt_state ) ;
    DECRYPT_CHANGE_STATE( in ,
                          *font_decrypt_state ,
                          DECRYPT_ADD ,
                          DECRYPT_MULT ) ;
  }
  else {
    out = in ;
  }
  return ( out ) ;
}


static Bool handle_othersubrs(corecontext_t *context,
                              charstring_methods_t *t1fns,
                              int32 othersubr_index, int32 numargs ,
                              double *arg_stack)
{
  OBJECT subr = OBJECT_NOTVM_NOTHING;
  int32 i;
  ps_context_t *pscontext = context->pscontext ;

  HQASSERT(t1fns, "No charstring methods") ;

  /* Called from CALLOTHERSUBR for anything other
   * than OtherSubrs entries 0 - 3 .
   */

  /* The plan is to do this :-
   * gsave
   * push systemdict onto dict stack (HMT-23.3.93) - T1 book V 1.1
   * push fontdict onto dict stack (AC-5.6.92)
   * push arguments onto stack
   * push procedure onto execution stack (OtherSubrs[othersubrs_index])
   * exec
   * pop  fontdict from dict stack (AC-5.6.92)
   * pop  systemdict off dict stack (HMT-23.3.93)
   * grestore
   */
  if ( !(*t1fns->get_info)(t1fns->data, NAME_OtherSubrs, -1, &subr) )
    return FALSE ;

  if ( oType(subr) == ONULL ) /* No OtherSubrs exist, ignore it */
    return TRUE ;

  HQASSERT(oType(subr) == OINTEGER, "Length of OtherSubrs not an integer") ;
  if ( !(*t1fns->get_info)(t1fns->data, NAME_OtherSubrs,
                           othersubr_index, &subr) )
    return FALSE ;

  /* If the subr is not an array, ignore it */
  if ( oType(subr) != OARRAY && oType(subr) != OPACKEDARRAY )
    return TRUE ;

  for (i=0; i< numargs; i++) {
    if ( !stack_push_numeric(arg_stack[i], &operandstack) )
      return FALSE ;
  }

  if (!gsave_(pscontext) ||
      !begininternal( & systemdict ) ||
      !begininternal(&theFontInfo(*gstateptr).subfont) ||
      !push(&subr, &executionstack))
    return FALSE;

  NO_PURGE_INTERPRETER();

  return ( end_(pscontext) && end_(pscontext) && grestore_(pscontext)) ;
}

void init_C_globals_charstring1(void)
{
#if defined( ASSERT_BUILD )
  debug_hint_decode    = 0 ;
  debug_othersubr      = 0 ;
  debug_path_type      = 0 ;
  debug_path_values    = 0 ;
  debug_check_absolute = 0 ;
#endif

  ch_build = NULL ;
  adobe1_info = NULL ;
}

/*
Log stripped */
