/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:fonts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font subsystem implementation file. See COREfonts export file for class
 * definitions.
 */

#include "core.h"
#include "swerrors.h" /* error_handler, INVALIDFONT */
#include "objects.h"  /* object_store_* */
#include "dictscan.h" /* NAMETYPEMATCH */
#include "graphics.h" /* NOSHOWTYPE */
#include "mm.h"
#include "mmcompat.h"
#include "coreparams.h"
#include "security.h" /* SWpermission */
#include "genkey.h"   /* GNKEY_FEATURE_MPSOUTLINE */
#include "blobdata.h"
#include "swstart.h"
#include "coreinit.h"
#include "namedef_.h"

#include "fonts.h"
#include "fontcache.h"
#include "fontdata.h"
#include "fontparam.h"
#include "fcache.h"   /* no_purge */
#include "ttf.h"      /* tt_restore */
#include "cff.h"      /* cff_* */
#include "cidmap.h"   /* cidmap_* */
#include "cidfont0.h" /* cid0_* */
#include "t1hint.h"   /* t1hint_debug_init */
#include "dloader.h"  /* dld_* */
#include "pfin.h"     /* pfin_* */
#include "tt_sw.h"    /* tt_C_globals */
#include "morisawa.h" /* MPS_* */

/*---------------------------------------------------------------------------*/
/* Modular fonts system/userparams */
static Bool fonts_set_systemparam(corecontext_t *context, uint16 name, OBJECT *theo) ;
static Bool fonts_get_systemparam(corecontext_t *context, uint16 name, OBJECT *result) ;

static NAMETYPEMATCH fonts_system_match[] = {
  { NAME_FontFillRule | OOPTIONAL, 1, { ONAME }},
#if defined(DEBUG_BUILD)
  { NAME_FontHintingMethod | OOPTIONAL, 1, { OINTEGER }},
#endif
  { NAME_HintedFonts | OOPTIONAL, 3, { OBOOLEAN, OINTEGER, ONULL }},
  { NAME_MaxFontData | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MorisawaFonts | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_MorisawaCIDFonts | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_MorisawaFontOutlines | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_MaxFontCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CurFontCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_TrueTypeHints | OOPTIONAL, 3, { OBOOLEAN, ONAME, ONULL }},
  { NAME_ReportFontRepairs | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ForceNullMapping | OOPTIONAL, 1, { OBOOLEAN }},
  DUMMY_END_MATCH
} ;

/*---------------------------------------------------------------------------*/
static module_params_t fonts_system_params = {
  fonts_system_match,
  fonts_set_systemparam,
  fonts_get_systemparam,
  NULL
} ;

/** Initialisation table for COREfonts. */
static core_init_t fonts_init[] = {
  CORE_INIT("fontdata", fontdata_C_globals),
  CORE_INIT("fontcache", fontcache_C_globals),
  CORE_INIT("CID map", cidmap_C_globals),
#ifndef NO_CFF_FONTS
  CORE_INIT("CFF fonts", cff_C_globals),
#endif
#ifndef NO_TT_FONTS
  CORE_INIT("TT fonts", ttfont_C_globals),
  /* fonts compound takes responsibility for SWTrueType initialisation,
     because export header is in fonts. */
  CORE_INIT("TT fonts", tt_C_globals),
#endif
#ifndef NO_CID0_FONTS
  CORE_INIT("CID0 fonts", cid0_C_globals),
#endif
#ifndef NO_DLD1_FONTS
  CORE_INIT("DLD1 fonts", dld_C_globals),
#endif
  CORE_INIT("PFIN", pfin_C_globals),
} ;

static Bool fonts_swinit(SWSTART *params)
{
  return core_swinit_run(fonts_init, NUM_ARRAY_ITEMS(fonts_init), params) ;
}

static Bool fonts_swstart(SWSTART *params)
{
  FONTSPARAMS *fontsparams ;
  corecontext_t *context = get_core_context() ;

  /* Initialise devices configuration parameters */
  if ( (fontsparams = mm_alloc_static(sizeof(FONTSPARAMS))) == NULL )
    return FALSE ;

#define HINTEDFONTS (30) /* means 300 pixels */
  fontsparams->HintedFonts = HINTEDFONTS ;
  fontsparams->fontfillrule = NZFILL_TYPE ;
  fontsparams->MPSOutlines = FALSE ;
  fontsparams->MaxFontCache = fontsparams->CurFontCache = 0 ;
  fontsparams->MaxCacheMatrix = fontsparams->CurCacheMatrix = 0 ;
  fontsparams->MaxCacheChars = fontsparams->CurCacheChars = 0 ;
  fontsparams->CurCacheFonts = 0 ;
  fontsparams->TrueTypeHints = TrueTypeHints_SafeFaults ;
  fontsparams->ReportFontRepairs = FALSE ;
  fontsparams->ForceNullMapping = FALSE ;

  context->fontsparams = fontsparams ;

  HQASSERT(fonts_system_params.next == NULL,
           "Already linked system params accessor") ;

  /* Link accessors into global list */
  fonts_system_params.next = context->systemparamlist ;
  context->systemparamlist = &fonts_system_params ;

  /* Initial character context is NULL. */
  context->charcontext = NULL ;

#if defined(DEBUG_BUILD)
  t1hint_debug_init() ;
#endif

  return core_swstart_run(fonts_init, NUM_ARRAY_ITEMS(fonts_init), params) ;
}

static Bool fonts_postboot(void)
{
  return core_postboot_run(fonts_init, NUM_ARRAY_ITEMS(fonts_init)) ;
}

static void fonts_finish(void)
{
  corecontext_t *context = get_core_context() ;

  /* Clean out caches */
  font_caches_clear(context);
  core_finish_run(fonts_init, NUM_ARRAY_ITEMS(fonts_init)) ;
  if (context) {
    context->fontsparams = NULL ;
  }
}


void font_caches_clear(corecontext_t *context)
{
#ifndef NO_DLD1_FONTS
  dld_cache_clear();
#endif
#ifndef NO_CID0_FONTS
  cid0_cache_clear();
#endif
  cidmap_cache_clear();
#ifndef NO_CFF_FONTS
  cff_cache_clear();
#endif
#ifndef NO_TT_FONTS
  tt_cache_clear();
#endif
  if (context && context->fontsparams) {
    fontcache_clear(context->fontsparams);
  }
}


/*---------------------------------------------------------------------------*/
/* Interaction with PostScript VM */
void fonts_restore_commit(int32 savelevel)
{
  /* When modularisation is complete, purge_fnames will also be called from
     here. */
#ifndef NO_DLD1_FONTS
  dld_restore(savelevel);
#endif
#ifndef NO_CID0_FONTS
  cid0_restore(savelevel);
#endif
  cidmap_restore(savelevel);
#ifndef NO_TT_FONTS
  tt_restore(savelevel);
#endif
#ifndef NO_CFF_FONTS
  cff_restore(savelevel) ;
#endif
  fontcache_restore(savelevel) ;
}

/*---------------------------------------------------------------------------*/
static Bool fonts_set_systemparam(corecontext_t *context, uint16 name, OBJECT *theo)
{
  FONTSPARAMS *fontparams = context->fontsparams ;

  HQASSERT((theo && name < NAMES_COUNTED) ||
           (!theo && name == NAMES_COUNTED),
           "name and parameter object inconsistent") ;

  switch ( name ) {
  case NAME_FontFillRule:
    {
      uint8 fontfillrule ;

      if ( oName(*theo) == &system_names[NAME_fill] )
        fontfillrule = NZFILL_TYPE ;
      else if ( oName(*theo) == &system_names[NAME_eofill] )
        fontfillrule = EOFILL_TYPE ;
      else
        return error_handler(RANGECHECK);

      if ( fontfillrule != fontparams->fontfillrule ) {
        fontcache_clear(fontparams);
        fontparams->fontfillrule = fontfillrule ;
      }
    }
    break ;

#if defined(DEBUG_BUILD)
  case NAME_FontHintingMethod:
    t1hint_method(oInteger(*theo)) ;
    break ;
#endif

  case NAME_HintedFonts:
    {
      /* integer means hint below that font "size" in pixels.
       * false means never, true means always.
       * null means reset to default of 300 pixels.
       * Integer range is 10..2540, rounded to mul of 10.
       * If it changes, we must purge the font cache.  If chars have
       * already been used in this page, however the cached ones'll be used.
       */
      int32 old = fontparams->HintedFonts;
      if ( oType(*theo) == OINTEGER ) {
        int32 i = oInteger(*theo) ;
        i = ( i + 9 ) / 10;
        if ( i < 0 ) i = 0;
        if ( i > 255 ) i = 255;
        fontparams->HintedFonts = (uint8)i;
      }
      else if ( oType(*theo) == ONULL )
        fontparams->HintedFonts = (uint8)HINTEDFONTS; /* the default */
      else
        fontparams->HintedFonts = (uint8) (oBool(*theo) ? 255 : 0 );

      if ( old != fontparams->HintedFonts )
        /* Purge the whole glyph cache, but not any headers or charstring
           data. */
        fontcache_clear(fontparams);
    }
    break ;

  case NAME_MaxFontData:
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK) ;
    blob_cache_set_limit(font_data_cache, (size_t)oInteger(*theo)) ;
    break ;

  /* Cannot set MorisawaFonts */

  /* Cannot set MorisawaCIDFonts */

  case NAME_MorisawaFontOutlines: /* Outline de-restriction (extra feature) */
    fontparams->MPSOutlines = (int8)SWpermission(oInteger(*theo),
                                                   GNKEY_FEATURE_MPSOUTLINE);
    break ;

  case NAME_MaxFontCache:
    fontparams->MaxFontCache = oInteger(*theo) ;
    /* Should we purge the cache now? */
    break ;

  /* Cannot set CurFontCache */

  case NAME_TrueTypeHints:
    {
      /* /Check or null = run TT instructions, but throw errors on problems
       * /Safe or true  = run TT instructions, but fallback to un-instructed
       *                  characters on error, reporting the guilty font
       * /Silent        = as above, but without the report
       * /None or false = never run TrueType instructions
       */
      int32 old = fontparams->TrueTypeHints;
      if ( oType(*theo) == ONAME ) {
        switch ( oName(*theo)->namenumber ) {
        case NAME_None:
          fontparams->TrueTypeHints = TrueTypeHints_None ;
          break ;
        case NAME_Check:
          fontparams->TrueTypeHints = TrueTypeHints_CheckFaults ;
          break ;
        case NAME_Safe:
          fontparams->TrueTypeHints = TrueTypeHints_SafeFaults ;
          break ;
        case NAME_Silent:
          fontparams->TrueTypeHints = TrueTypeHints_SilentFaults ;
          break ;
        default:
          return error_handler(RANGECHECK) ;
        }
      } else if ( oType(*theo) == ONULL )
        fontparams->TrueTypeHints = TrueTypeHints_CheckFaults; /* the default */
      else
        fontparams->TrueTypeHints =
          (uint8)(oBool(*theo) ? TrueTypeHints_SafeFaults : TrueTypeHints_None);

      if ( old != fontparams->TrueTypeHints )
        /* Purge the whole glyph cache, but not any headers or charstring
           data. */
        fontcache_clear(fontparams);
    }
    break ;

  case NAME_ReportFontRepairs:
    fontparams->ReportFontRepairs = (uint8)oBool(*theo) ;
    break;

  case NAME_ForceNullMapping:
    {
      /* true  = CMap mapping from char 0 to .notdef is forced to .null instead
       * false = Character mapping unaffected
       * Controls CMap handling in cid2_lookup_char [300510] */
      uint8 old = fontparams->ForceNullMapping ;
      fontparams->ForceNullMapping = (uint8)oBool(*theo) ;
      if (old != fontparams->ForceNullMapping)
        fontcache_clear(fontparams) ;
    }
    break ;
  }

  return TRUE ;
}

static Bool fonts_get_systemparam(corecontext_t *context, uint16 name, OBJECT *result)
{
  FONTSPARAMS *fontparams = context->fontsparams ;

  HQASSERT(result, "No object for systemparam result") ;

  switch ( name ) {
  case NAME_FontFillRule:
    HQASSERT(fontparams->fontfillrule == NZFILL_TYPE ||
             fontparams->fontfillrule == EOFILL_TYPE,
             "Invalid font fill rule") ;
    object_store_name(result, fontparams->fontfillrule == NZFILL_TYPE ?
                      NAME_fill : NAME_eofill, LITERAL) ;
    break ;

  /* FontHintingMethod not returned deliberately */

  case NAME_HintedFonts:
    if ( fontparams->HintedFonts == 0) {
      OCopy(*result, fnewobj) ;
    } else if ( fontparams->HintedFonts == 255) {
      OCopy(*result, tnewobj) ;
    } else {
      object_store_integer(result, fontparams->HintedFonts * 10);
    }
    break;

  case NAME_MaxFontData:
    object_store_integer(result, (int32)blob_cache_get_limit(font_data_cache)) ;
    break ;

  case NAME_MorisawaFonts:
    object_store_bool(result, MPS_supported()) ;
    break;

  case NAME_MorisawaCIDFonts:
    object_store_bool(result, MPS_supported()) ;
    break;

  /* MorisawaFontOutlines not returned deliberately */

  case NAME_MaxFontCache:
    object_store_integer(result, fontparams->MaxFontCache) ;
    break;

  case NAME_CurFontCache:
    object_store_integer(result, fontparams->CurFontCache) ;
    break;

  case NAME_TrueTypeHints:
    switch ( fontparams->TrueTypeHints ) {
    case TrueTypeHints_None:
      *result = fnewobj ;
      break ;
    case TrueTypeHints_SafeFaults:
      /* Will be tnewobj in future */
      object_store_name(result, NAME_Safe, LITERAL);
      break ;
    case TrueTypeHints_SilentFaults:
      object_store_name(result, NAME_Silent, LITERAL);
      break ;
    /*case TrueTypeHints_CheckFaults:*/
    default:
      object_store_name(result, NAME_Check, LITERAL);
      break ;
    }
    break;

  case NAME_ReportFontRepairs:
    object_store_bool(result, fontparams->ReportFontRepairs) ;
    break;

  case NAME_ForceNullMapping:
    object_store_bool(result, fontparams->ForceNullMapping) ;
    break ;
  }

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/* This function is used by the rest of the RIP to determine if protected
   font definitions can be provided to the user. */
Bool font_protection_override(uint8 protection, uint32 reason)
{
  UNUSED_PARAM(uint32, reason) ;

  HQASSERT(reason == FONT_OVERRIDE_CHARPATH ||
           reason == FONT_OVERRIDE_HDLT,
           "Unknown reason for font protection override") ;

  return (protection == PROTECTED_MRSWA &&
          get_core_context_interp()->fontsparams->MPSOutlines) ;
}

/*---------------------------------------------------------------------------*/
charcontext_t *char_current_context(void)
{
  return get_core_context_interp()->charcontext ;
}

Bool char_doing_charpath(void)
{
  charcontext_t *context = char_current_context() ;
  return (context != NULL && context->modtype == DOCHARPATH) ;
}

Bool char_doing_cached(void)
{
  charcontext_t *context = char_current_context() ;
  return (context != NULL && context->cptr != NULL) ;
}

Bool char_doing_buildchar(void)
{
  charcontext_t *context = char_current_context() ;
  return (context != NULL && context->buildchar) ;
}


static void init_C_globals_fonts(void)
{
  fonts_system_params.next = NULL ;
}

/* Declare global init functions here to avoid header inclusion
   nightmare. */
IMPORT_INIT_C_GLOBALS( charstring1 )
IMPORT_INIT_C_GLOBALS( charstring2 )
IMPORT_INIT_C_GLOBALS( t1hint )
IMPORT_INIT_C_GLOBALS( t32font )

void fonts_C_globals(core_init_fns *fns)
{
  init_C_globals_charstring1() ;
  init_C_globals_charstring2() ;
  init_C_globals_fonts() ;
  /* pfin has its own entry point */
  init_C_globals_t1hint() ;
  init_C_globals_t32font() ;

  fns->swinit = fonts_swinit ;
  fns->swstart = fonts_swstart ;
  fns->postboot = fonts_postboot ;
  fns->finish = fonts_finish ;

  core_C_globals_run(fonts_init, NUM_ARRAY_ITEMS(fonts_init)) ;
}

/*
Log stripped */
