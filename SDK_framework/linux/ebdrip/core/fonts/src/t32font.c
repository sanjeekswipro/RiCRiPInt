/** \file
 * \ingroup fonttype32
 *
 * $HopeName: COREfonts!src:t32font.c(EBDSDK_P.1) $:
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Support for Type 32 fonts (aka CIDFontType 4). These are device-resolution
 * bitmap fonts that may be inserted directly into the font cache.
 *
 * Debugging:
 * At compile time, define T32_DEBUG to include extra debug code.
 * At run time, code is switched on the value of the t32_debug variable.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "monitor.h"
#include "fonts.h"
#include "fontcache.h"
#include "hqmemset.h"
#include "timing.h"
#include "namedef_.h"

#include "matrix.h"
#include "stacks.h"
#include "stackops.h"
#include "graphics.h"
#include "gstack.h"
#include "gstate.h"
#include "showops.h"
#include "swmemory.h" /* gs_cleargstates */
#include "utils.h"    /* from_matrix */
#include "gu_ctm.h"   /* gs_setctm */
#include "routedev.h" /* CURRENT_DEVICE */
#include "images.h"   /* gs_imagemask */
#include "fontops.h"  /* gs_setfont */
#include "render.h"   /* render_state_t */
#include "bitblts.h" /* blit_chain_t */
#include "formOps.h"

#include "fcache.h"
#include "cidfont.h"
#include "t32font.h"


/* Exported definition of the font methods for Type 32 (CID Type 4) bitmap
   fonts */
static Bool t32_lookup_char(FONTinfo *fontInfo,
                            charcontext_t *context) ;
static Bool t32_begin_char(FONTinfo *fontInfo,
                           charcontext_t *context) ;
static void t32_end_char(FONTinfo *fontInfo,
                         charcontext_t *context) ;

font_methods_t font_cid4_fns = {
  fontcache_cid_key,
  t32_lookup_char,
  NULL, /* No subfont lookup */
  t32_begin_char,
  t32_end_char
} ;

/* ------------------------------------------------------------------------- */
/* ----------------------------  DEBUG SETUP  ------------------------------ */
/* ------------------------------------------------------------------------- */

/* Run time debug control */
#if defined( DEBUG_BUILD )
int32 t32_debug = 0;
#endif

/* ------------------------------------------------------------------------- */
/* -------------------------  TYPES AND DEFINES  --------------------------- */
/* ------------------------------------------------------------------------- */

enum { Q32mFontType, Q32m_n_entries };

static NAMETYPEMATCH quick32_m[Q32m_n_entries + 1] = {
  { NAME_FontType,               1, { OINTEGER }},
  DUMMY_END_MATCH
};



/* Pass a pointer to one of these structures around between routines, rather
   than passing zillions of params. */
typedef struct {
  /* Metrics */
  SYSTEMVALUE w0x, w0y, w1x, w1y;
  SYSTEMVALUE vx, vy;
  int32 llx, lly, urx, ury;
  /* Other derived useful values */
  int32 width, height;
  int32 bytewidth;
  int32 num_metrics;
} T32METRICS;


/* ------------------------------------------------------------------------- */
/* ------------------------  INTERNAL PROTOTYPES  -------------------------- */
/* ------------------------------------------------------------------------- */

static Bool t32_checktype(OBJECT *fonto);

static Bool t32_scale_and_cache(corecontext_t *context,
                                charcontext_t *charcontext,
                                T32METRICS *met, OBJECT *bitmapo,
                                LINELIST *currpt,
                                int32 gid);
static Bool t32_scale_and_cache_guts(ps_context_t *pscontext,
                                     charcontext_t *charcontext,
                                     T32METRICS *met, OBJECT *bitmapo);

static Bool addglyph_internal(ps_context_t *pscontext, OBJECT *metrico,
                              OBJECT *bitmapo, int32 cid, OBJECT *fonto);

static Bool t32_cache_bitmap(ps_context_t *pscontext,
                             int32 width, int32 height, OBJECT *bitmap,
                             int32 llx, int32 lly);
static void t32_derive_metrics(CHARCACHE *cptr, T32METRICS *met);
static void t32_unpack_form(CHARCACHE *cptr, T32METRICS *met, uint8 *decode);
static Bool t32_remove_glyphs(OBJECT *fonto, int32 firstcid, int32 lastcid);

#if defined( DEBUG_BUILD )
static void t32_test_decode(CHARCACHE *cptr);
static void t32debug_dump_metrics(T32METRICS *m, CHARCACHE *c);
static void skg_dump_form_binary(CHARCACHE *cptr);
static void skg_dump_string(uint8 *data_string, int32 width, int32 height);
#endif


/* ----------------------------------------------------------------------- */
/* Font lookup and top-level charstring routines for Type 32 (CID Type 4)
   bitmap fonts */
static Bool t32_lookup_char(FONTinfo *fontInfo,
                            charcontext_t *context)
{
  CHARCACHE *master ;

  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(fontInfo, "No selector") ;
  HQASSERT(context, "No  context") ;

  HQASSERT(theFontType(*fontInfo) == CIDFONTTYPE4, "Not in a Type 32") ;

  /* It is possible that theLookupFont is not set by this time; if a
     CID Font Type 4 is added but does not have any characters added, or
     the font dictionary is copied and redefined, the FIDs will not match
     and there will not be any characters. */
  if ( !theLookupFont(*fontInfo) )
    return error_handler(INVALIDFONT) ;

  /* Type 32 lookup matrix can ignore translations when looking for cache
     entry. */
  if ( !theLookupMatrix(*fontInfo) )
    (void)fontcache_lookup_matrix_t32(fontInfo) ;

  HQASSERT(oType(context->glyphname) == OINTEGER, "Not indexed by CID") ;
  if ( (master = fontcache_lookup_char_t32(fontInfo,
                                           &context->glyphname)) == NULL ) {
    context->chartype = CHAR_Undefined ;
    return TRUE ;
  }

  theTags(context->definition) = OCPOINTER | LITERAL ;
  oCPointer(context->definition) = master ;

  context->chartype = CHAR_Bitmap ;

  return TRUE ;
}

/* Determine if the named char exists in the font, and whether it has been
   replaced by a procedure. */
static Bool t32_begin_char(FONTinfo *fontInfo,
                            charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;
  UNUSED_PARAM(charcontext_t *, context) ;

  HQASSERT(theFontType(*fontInfo) == CIDFONTTYPE4, "Not in a Type 32") ;
  HQASSERT(context->chartype == CHAR_Bitmap, "Not a bitmap char") ;

  return TRUE ;
}

static void t32_end_char(FONTinfo *fontInfo,
                         charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;
  UNUSED_PARAM(charcontext_t *, context) ;
}

/*---------------------------------------------------------------------------*/
Bool addglyph_(ps_context_t *pscontext)
{
  OBJECT *theo, *fonto, *bitmapo, *metrico;
  int32 cid;

  if ( theStackSize(operandstack) < 3 )
    return error_handler(STACKUNDERFLOW);

  fonto = theTop(operandstack);
  if ( oType(*fonto) != ODICTIONARY )
    return error_handler(INVALIDFONT);

  if ( ! oCanRead(*oDict(*fonto)) )
    if ( ! object_access_override(oDict(*fonto)))
      return error_handler( INVALIDACCESS );
  if ( ! (theTags(*oDict(*fonto)) & ISAFONT) )
    return error_handler(INVALIDFONT);

  if ( ! t32_checktype(fonto) )
    return error_handler(INVALIDFONT);

  theo = stackindex(1, &operandstack);
  if ( oType(*theo) != OINTEGER )
    return error_handler(TYPECHECK);
  cid = oInteger(*theo);

  bitmapo = stackindex(2, &operandstack);
  if ( oType(*bitmapo) != OSTRING )
    return error_handler(TYPECHECK);

  metrico = stackindex(3, &operandstack);
  if ( oType(*metrico) != OARRAY &&
       oType(*metrico) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  if ( theLen(*metrico) != 6 &&
       theLen(*metrico) != 10 )
    return error_handler(TYPECHECK);

  if ( ! addglyph_internal(pscontext, metrico, bitmapo, cid, fonto) )
    return FALSE;

  npop(4, &operandstack);
  return TRUE;
}


Bool removeglyphs_(ps_context_t *pscontext)
{
  OBJECT *theo, *fonto;
  int32 firstcid, lastcid;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize(operandstack) < 2 )
    return error_handler(STACKUNDERFLOW);

  fonto = theTop(operandstack);
  if ( oType(*fonto) != ODICTIONARY )
    return error_handler(INVALIDFONT);

  if ( !oCanRead(*oDict(*fonto)) )
    if ( ! object_access_override(oDict(*fonto)))
      return error_handler( INVALIDACCESS );
  if ( ! (theTags(*oDict(*fonto)) & ISAFONT) )
    return error_handler(INVALIDFONT);

  if ( ! t32_checktype(fonto) )
    return error_handler(INVALIDFONT);

  theo = stackindex(1, &operandstack);
  if ( oType(*theo) != OINTEGER )
    return error_handler(TYPECHECK);
  lastcid  = oInteger(*theo);

  theo = stackindex(2, &operandstack);
  if ( oType(*theo) != OINTEGER )
    return error_handler(TYPECHECK);
  firstcid = oInteger(*theo);

  if ( firstcid < 0 ||
       firstcid > lastcid )
    return error_handler(RANGECHECK);

  if ( ! t32_remove_glyphs(fonto, firstcid, lastcid) )
    return FALSE;

  npop(3, &operandstack);
  return TRUE;
}


Bool removeall_(ps_context_t *pscontext)
{
  OBJECT *fonto;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW);

  fonto = theTop(operandstack);
  if ( oType(*fonto) != ODICTIONARY )
    return error_handler(INVALIDFONT);

  if ( !oCanRead(*oDict(*fonto)) )
    if ( ! object_access_override(oDict(*fonto)))
      return error_handler( INVALIDACCESS );
  if ( ! (theTags(*oDict(*fonto)) & ISAFONT) )
    return error_handler(INVALIDFONT);

  if ( ! t32_checktype(fonto) )
    return error_handler(INVALIDFONT);

  if ( ! t32_remove_glyphs(fonto, 0, MAXINT32) )
    return FALSE;

  pop(&operandstack);
  return TRUE;
}


Bool t32_plot(corecontext_t *context,
              charcontext_t *charcontext, LINELIST *currpt, int32 gid)
{
  CHARCACHE *master = NULL;
  int32 bothmodes;
  OBJECT bitmapo = OBJECT_NOTVM_NOTHING;
  uint8 *bitmap = NULL;
  T32METRICS metrics;
  int32 result;
  CHARCACHE *cptr = NULL ;
  OBJECT *glyphname ;

  HQASSERT(charcontext, "No character context") ;
  glyphname = &charcontext->definition ;
  HQASSERT(oType(*glyphname) == OCPOINTER,
           "Type 32/CID 4 glyphname not a pointer");

  master = oCPointer(*glyphname) ;
  HQASSERT(isIT32Master(master), "t32_pl: invalid master cache entry");

  bothmodes = isIT32BothModes(master);

  if ( theLookupMatrix(theFontInfo(*gstateptr)) ) {
    if ( !bothmodes ) {
      /* If we had a matrix hit, but not a character hit, and we know that the
         character had only one set of metrics, it's possible we've got the
         character from the 'other' wmode cached, in which case we can just use
         that */
      if ( (cptr = fontcache_lookup_char_t32(&theFontInfo(*gstateptr),
                                             glyphname)) != NULL ) {
        charcontext->cptr = cptr ;
        charcontext->cachelevel = CacheLevel_Found ;
        HQASSERT(theWMode(theFontInfo(*gstateptr)) != theICharWMode(cptr),
                 "Should have found character with correct WMode before") ;
        return TRUE ;
      }
    }
  }

  t32_derive_metrics(master, &metrics);
  if ( theFormT(*theForm(*master)) != FORMTYPE_BLANK ) {
    bitmap = (uint8*)mm_alloc(mm_pool_temp,
                              metrics.bytewidth * metrics.height,
                              MM_ALLOC_CLASS_T32_IMAGESRC);
    if ( ! bitmap )
      return error_handler( VMERROR );
    t32_unpack_form(master, &metrics, bitmap);
  }

  theTags(bitmapo) = OSTRING | UNLIMITED | LITERAL ;
  SETGLOBJECTTO(bitmapo, TRUE) ;
  theLen(bitmapo) = (uint16)(metrics.bytewidth * metrics.height);
  oString(bitmapo) = bitmap;

  /* Okay, now we've got all the information we need to do the character */

  result = t32_scale_and_cache(context, charcontext, &metrics, &bitmapo, currpt, gid);

  if ( bitmap != NULL )
    mm_free(mm_pool_temp,
            (mm_addr_t)bitmap,
            metrics.bytewidth * metrics.height);

  return result;
}


/* ----------------------------------------------------------------------- */
/* -----------------------   INTERNAL FUNCTIONS   ------------------------ */
/* ----------------------------------------------------------------------- */

static Bool t32_checktype(OBJECT *fonto)
{
  /* Check it's a type 32 font */
  return (dictmatch(fonto, quick32_m) &&
          oInteger(*quick32_m[Q32mFontType].result) == CIDFONTTYPE4) ;
}


static Bool t32_remove_glyphs(OBJECT *fonto, int32 firstcid, int32 lastcid)
{
  int32 fid;
  OBJECT *theo;

  HQASSERT(firstcid <= lastcid, "t32_remove_gl: first and last order");
  HQASSERT(oType(*fonto) == ODICTIONARY,
           "t32_remove_gl: font is not a dictionary");

  if ( NULL == ( theo = fast_extract_hash_name(fonto, NAME_FID)) ) {
    return error_handler( INVALIDFONT );
  }
  fid = oFid(*theo);

  fontcache_remove_chars(fid, firstcid, lastcid) ;

  return TRUE ;
}


static Bool addglyph_internal(ps_context_t *pscontext, OBJECT *metrico,
                              OBJECT *bitmapo, int32 cid, OBJECT *fonto)
{
  T32METRICS met;

  int32 i;
  Bool result = FALSE;
  OBJECT *olist;

  HQASSERT(metrico, "t32_addglyph_int: no metrics");

  met.num_metrics = theLen(*metrico);
  olist = oArray(*metrico);

  HQASSERT(olist != NULL && (met.num_metrics == 6 || met.num_metrics == 10),
           "t32_addglyph_int: got dodgy metrics array");

  /* Extract width & height */
  if ( ! object_get_numeric(olist + 0, &met.w0x) ||
       ! object_get_numeric(olist + 1, &met.w0y) )
    return FALSE;

  /* next four co-ords for lower left and upper right, should all be ints */
  for ( i = 2; i < 6; ++i )
    if ( oType(olist[i]) != OINTEGER )
      return error_handler(TYPECHECK);

  met.llx = oInteger(olist[2]);
  met.lly = oInteger(olist[3]);
  met.urx = oInteger(olist[4]);
  met.ury = oInteger(olist[5]);

  if ( met.num_metrics == 10 ) {
    /* Get wmode 1 stuff too */
    if ( ! object_get_numeric(olist + 6, &met.w1x) ||
         ! object_get_numeric(olist + 7, &met.w1y) ||
         ! object_get_numeric(olist + 8, &met.vx)  ||
         ! object_get_numeric(olist + 9, &met.vy) )
      return FALSE;
  }

  met.width  = met.urx - met.llx;
  met.height = met.ury - met.lly;

  if ( met.width < 0 || met.height < 0 )
    return error_handler(RANGECHECK);

  met.bytewidth = (met.width + 7) >> 3;

  if ( (met.bytewidth * met.height) > theLen(*bitmapo) ) /* bytes to bits */
    return error_handler(RANGECHECK);

  /* We add a gsave around the bracket_plot() because we need to reset the
     font; gs_setcachedevice looks for the GST_SETCACHEDEVICE gstack frame
     to extract the font information from. */
#define return DO_NOT_RETURN_GO_TO_cleanup_gsave_OR_cleanup_bracket_plot_INSTEAD!
  if ( gsave_(pscontext) ) {
    /* Disabled HDLT since it shouldn't be told about characters until we
     * actually use them - in which case, although Scriptworks will (should!)
     * have cached them, the HDLT Character callback won't have done (hasn't
     * seen them yet) and so the guts will be 're-run' later.
     */
    gstateptr->theHDLTinfo.state = HDLT_DISABLED ;

    if ( gs_setfont(fonto) && set_font() ) {
      int32 gid ;
      corecontext_t *context = ps_core_context(pscontext) ;
      charcontext_t *ocharcontext, lcharcontext ;
      render_state_t char_rs ;
      blit_chain_t char_blits ;
      render_forms_t char_forms ;
      Bool check ;

      /* Fake a character context for gs_setcachedevice et. al. */
      ocharcontext = context->charcontext ;
      context->charcontext = &lcharcontext ;

#if defined( DEBUG_BUILD ) ||  defined( ASSERT_BUILD )
      HqMemSet8((uint8 *)&char_rs, 0xff, sizeof(render_state_t));
      HqMemSet8((uint8 *)&char_forms, 0xff, sizeof(FORM));
#endif

      lcharcontext.cptr = NULL ;
      lcharcontext.xwidth = 0.0 ; /* Make metrics problems obvious */
      lcharcontext.ywidth = 0.0 ;
      lcharcontext.cachelevel = CacheLevel_Unset ;
      lcharcontext.buildchar = TRUE ;
      lcharcontext.buildthischar = TRUE ;
      lcharcontext.bracketed = FALSE ;
      lcharcontext.showtype = DOSHOW ;
      lcharcontext.modtype = DOSHOW ;
      lcharcontext.chartype = CHAR_Bitmap ;
      object_store_integer(object_slot_notvm(&lcharcontext.glyphname), cid) ;
      lcharcontext.glyphchar = cid;
      lcharcontext.methods = NULL;
      lcharcontext.rs = &char_rs ;
      char_rs.forms = &char_forms ;
      char_rs.ri.rb.clipform = &char_forms.clippingform ;
      char_rs.ri.rb.blits = &char_blits ;

      /* Initialise the definition to null, just in case of errors. */
      lcharcontext.definition = onull ; /* Struct copy to set slot properties */

      /* When we're called through addglyph, we have to set up the font
         ourselves, and force the identity transformation */
      MATRIX_COPY(&theFontMatrix(theFontInfo(*gstateptr)), &identity_matrix);
      MATRIX_COPY(&thegsDeviceCTM(*gstateptr), &identity_matrix) ;
      check = gs_setctm(&identity_matrix, FALSE) ;
      HQASSERT(check, "gs_setctm should not fail with FALSE argument") ;

      if ( (!theLookupFont(theFontInfo(*gstateptr)) ||
            fontcache_lookup_matrix(&theFontInfo(*gstateptr))) &&
           bracket_plot(&lcharcontext, &gid) ) {
        if ( t32_scale_and_cache_guts(pscontext, &lcharcontext, &met, bitmapo) ) {
          CHARCACHE *cptr = lcharcontext.cptr ;

          /* Now set any T32 specific cache entries */

          /* This is the 'main' definition of the glyph - this flag lets us
             avoid it on cache purge */
          SetIT32MasterFlag(cptr);
          if ( met.num_metrics == 10 ) {
            /* store enough information to draw the character in the 'other'
               wmode, should we need to. */
            T32_DATA *t32data;

            t32data = (T32_DATA*)mm_alloc(mm_pool_temp, sizeof(T32_DATA),
                                          MM_ALLOC_CLASS_T32_DATA);
            if ( !t32data ) {
              (void)error_handler( VMERROR );
              goto cleanup_bracket_plot ;
            }

            cptr->t32data = t32data;

            theXOffset(*t32data) = met.vx;
            theYOffset(*t32data) = met.vy;

            if ( theWMode(theFontInfo(*gstateptr)) ) {
              theWidth(*t32data) = met.w0x;
              theHeight(*t32data) = met.w0y;
            } else {
              theWidth(*t32data) = met.w1x;
              theHeight(*t32data) = met.w1y;
            }
          }

          result = TRUE ; /* Yippee! It all worked! */

#if defined( DEBUG_BUILD )
          if ( t32_debug ) {
            t32_test_decode(cptr);
          }
#endif
        }
      cleanup_bracket_plot:
        result = gs_cleargstates(gid, GST_SETCHARDEVICE, NULL) && result;
      }

      if ( lcharcontext.cachelevel == CacheLevel_Cached ) {
        /* We may have failed after adding the character to the cache. */
        probe_end(SW_TRACE_FONT_CACHE, (intptr_t)lcharcontext.cptr) ;

        if ( !result ) {
          fontcache_free_char(context->fontsparams,
                              &theFontInfo(*gstateptr), lcharcontext.cptr) ;
          lcharcontext.cachelevel = CacheLevel_Error ;
          lcharcontext.cptr = NULL ;
        }
      }

      context->charcontext = ocharcontext ;
    }
    result = grestore_(pscontext) && result;
  }

#undef return
  return result ;
}

/* Wrapper round the guts of the routine, to aid cleanup in error cases */
static Bool t32_scale_and_cache(corecontext_t *context,
                                charcontext_t *charcontext,
                                T32METRICS *met, OBJECT *bitmapo,
                                LINELIST *currpt,
                                int32 gid )
{
  int32 lgid = GS_INVALID_GID;
  Bool result = TRUE;

  /* Only do a bracket_plot if we haven't done one already - don't want two */
  if ( gid == GS_INVALID_GID )
    result = bracket_plot(charcontext, &lgid);

  /* We're called via plotCIDcharglyph, so the font is there already */

   if ( result ) {
     OMATRIX m1 ;
     Bool check ;
     ps_context_t *pscontext = context->pscontext ;

     /* Otherwise, we're called via plotCIDcharglyph, so the font is there
        already */

     /* Set CTM - default CTM set in setcachedevice(2) */
     MATRIX_COPY(&m1, &theFontMatrix(theFontInfo(*gstateptr))) ;
     if ( charcontext->modtype == DOSTRINGWIDTH ) {
       m1.matrix[ 2 ][ 0 ] = 0.0 ;
       m1.matrix[ 2 ][ 1 ] = 0.0 ;
       if ( ! nulldevice_(pscontext))
         result = FALSE ;
     }
     else {
       HQASSERT( currpt != NULL, "No currentpoint in t32_scale_guts" );

       m1.matrix[ 2 ][ 0 ] += theX(thePoint(*currpt)) ;
       m1.matrix[ 2 ][ 1 ] += theY(thePoint(*currpt)) ;
     }

     MATRIX_COPY(&thegsDeviceCTM(*gstateptr), &m1) ;
     check = gs_setctm(&m1, FALSE) ;
     HQASSERT(check, "gs_setctm should not fail with FALSE argument") ;

     result = result && t32_scale_and_cache_guts(pscontext, charcontext, met, bitmapo);
   }

   if ( lgid != GS_INVALID_GID ) {
     charcontext->bracketed = FALSE;
     result = gs_cleargstates( lgid, GST_SETCHARDEVICE, NULL ) && result;
   }

  return result;
}


static Bool t32_scale_and_cache_guts(ps_context_t *pscontext,
                                     charcontext_t *charcontext,
                                     T32METRICS *met, OBJECT *bitmapo)
{
  Bool result ;
  CHARCACHE *cptr ;

  if ( !mark_(pscontext) )
    return FALSE;

  /* Push metrics and call setcachedevice */
  result = (stack_push_real(met->w0x, &operandstack) &&
            stack_push_real(met->w0y, &operandstack) &&
            stack_push_integer(met->llx, &operandstack) &&
            stack_push_integer(met->lly, &operandstack) &&
            stack_push_integer(met->urx, &operandstack) &&
            stack_push_integer(met->ury, &operandstack)) ;

  if ( result && met->num_metrics == 10 )
    result = (stack_push_real(met->w1x, &operandstack) &&
              stack_push_real(met->w1y, &operandstack) &&
              stack_push_real(met->vx, &operandstack) &&
              stack_push_real(met->vy, &operandstack)) ;

  if ( result )
    result = gs_setcachedevice(&operandstack,
                               met->num_metrics == 10 ? TRUE : FALSE) ;

  if ( !cleartomark_(pscontext) || !result )
    return FALSE ;

  /* It's possible for setcachedevice to return true, but not have a cptr if
     the size of the bitmap is too large, or the cache allocation calls in
     char_cache (called by gs_setcachedevice) fail. */
  if ( charcontext->cachelevel != CacheLevel_Cached )
    return FAILURE(FALSE);

  cptr = charcontext->cptr ;
  HQASSERT(cptr, "No char cache set");

  /* Check for a blank form */
  if ( theLen(*bitmapo) == 0 ) {
    destroy_Form(theForm(*cptr));
    if ( NULL == (theForm(*cptr) = MAKE_BLANK_FORM()) )
      return FAILURE(FALSE);
  } else {
    if ( ! t32_cache_bitmap(pscontext, met->width, met->height, bitmapo,
                            met->llx, met->lly) )
      return FALSE;
  }

  if ( theFormT(*theForm(*cptr)) == FORMTYPE_CACHEBITMAPTORLE )
    /* This next line should be:
       ( void )form_to_rle( cptr , 0 ) ;
       But until t32_unpack_form understands rle, don't do it. */
    theFormT(*theForm(*cptr)) = FORMTYPE_CACHEBITMAP;

  if ( met->num_metrics == 10 )
    SetIT32BothModesFlag(cptr);

  return TRUE;
}


/* Render a type32 bitmap into the current cache char */
static Bool t32_cache_bitmap(ps_context_t *pscontext,
                             int32 width, int32 height, OBJECT *bitmap,
                             int32 llx, int32 lly)
{
  corecontext_t *context = ps_core_context(pscontext) ;
  Bool result = FALSE;
  OBJECT newmatrix = OBJECT_NOTVM_NOTHING;
  OBJECT olist[6];
  OMATRIX imatrix;
  size_t i;

  HQASSERT(CURRENT_DEVICE() == DEVICE_CHAR,
           "t32_cache_bmap: DEVICE_CHAR should be set");

  MATRIX_COPY(&imatrix, &identity_matrix);
  MATRIX_20(&imatrix) = (SYSTEMVALUE)-llx;
  MATRIX_21(&imatrix) = (SYSTEMVALUE)-lly;

  theTags(newmatrix) = OARRAY | LITERAL | UNLIMITED;
  theLen(newmatrix) = 6;
  oArray(newmatrix) = olist;
  for ( i = 0; i < 6; ++i ) {
    object_store_null(object_slot_notvm(&olist[i])) ;
  }

  if ( ! from_matrix(olist, &imatrix, context->glallocmode) )
    return FALSE;

  if ( !mark_(pscontext) )
    return FALSE ;

  result = ( stack_push_integer(width, &operandstack) &&
             stack_push_integer(height, &operandstack) &&
             push3(&tnewobj, &newmatrix, bitmap, &operandstack) &&
             gs_imagemask(context, &operandstack) );

  return cleartomark_(pscontext) && result;
}


static void t32_derive_metrics(CHARCACHE *cptr, T32METRICS *met)
{
  FORM *f = theForm(*cptr);
  int32 width, height;
  SYSTEMVALUE tmp_xb, tmp_yb;

  HQASSERT(isIT32Master(cptr), "t32_derive_m: expected master cache");
  HQASSERT(f, "t32_derive_m: no form data");

  width  = theFormW(*f) - 5;
  height = theFormH(*f) - 5;

  HQASSERT((width > 0 && height > 0) ||
           (width == -5 && height == -5 && theFormT(*f) == FORMTYPE_BLANK),
           "t32_derive_m: dubious width/height in form");

  met->width  = width;
  met->height = height;
  met->bytewidth = (width + 7) >> 3;

  if ( isIT32BothModes(cptr) ) {
    T32_DATA *t32d = cptr->t32data;
    HQASSERT(t32d, "t32_derive_m: no data pointer?");

    met->num_metrics = 10;
    met->vx = theXOffset(*t32d);
    met->vy = theYOffset(*t32d);

    if ( theICharWMode(cptr) ) {
      /* Defined wmode 1 */
      tmp_xb = theXBearing(*cptr) + met->vx;
      tmp_yb = theYBearing(*cptr) + met->vy;

      met->w0x = theWidth(*t32d);
      met->w0y = theHeight(*t32d);
      met->w1x = theCharXWidth(*cptr);
      met->w1y = theCharYWidth(*cptr);
    } else {
      /* Defined wmode 0 */
      tmp_xb = theXBearing(*cptr);
      tmp_yb = theYBearing(*cptr);

      met->w0x = theCharXWidth(*cptr);
      met->w0y = theCharYWidth(*cptr);
      met->w1x = theWidth(*t32d);
      met->w1y = theHeight(*t32d);
    }
  } else { /* Only one set of metrics */
    met->num_metrics = 6;
    tmp_xb = theXBearing(*cptr);
    tmp_yb = theYBearing(*cptr);

    met->w0x = theCharXWidth(*cptr);
    met->w0y = theCharYWidth(*cptr);
  }

  met->llx = (int32)(tmp_xb + 2.0);
  met->lly = (int32)(tmp_yb + 2.0);
  met->urx = (int32)(met->llx) + width;
  met->ury = (int32)(met->lly) + height;
}


static void t32_unpack_form(CHARCACHE *cptr, T32METRICS *met, uint8 *decode)
{
  FORM *f = theForm(*cptr);
  int32 lbytes = theFormL(*f);
  int32 words_per_line = lbytes >> BLIT_SHIFT_BYTES;

  int32 inlines = 2;
  int32 outlines = 0;
  int32 formheight;
  int32 bytewidth;

  HQASSERT(decode, "t32_up_form: no decode array");
  HQASSERT(met, "t32_up_form: no metrics");
  HQASSERT(theFormT(*f) ==  FORMTYPE_CACHEBITMAP,
           "t32_up_form: can only handle uncompressed forms");

  formheight = met->height;
  bytewidth = met->bytewidth;

  HQASSERT(theFormH(*f) == formheight + 5 && theFormW(*f) == met->width + 5,
           "t32_up_form: suspicious dimensions");

  /* Copy data out of the form */
  for ( outlines = 0 ; outlines < formheight; ++outlines ) {
    int32 wordsleft = words_per_line;
    int32 outbytes = bytewidth;
    blit_t *data = BLIT_ADDRESS(theFormA(*f), inlines * lbytes) ;
    blit_t ping = *data++;
    blit_t srcword;

    while ( wordsleft >= 2 ) {
      blit_t pong = *data++;
      HQASSERT(outbytes >= BLIT_WIDTH_BYTES, "t32_up_form: Invalid assumption");

      /** \todo @@@ TODO FIXME ajcd 2005-06-26: This is probably wrong. It
          should probably take into account bitsgoleft/bitsgoright, and use
          SHIFTLEFT/SHIFTRIGHT. */
      srcword = ping << 2 | pong >> (BLIT_WIDTH_BITS - 2);

      BLIT_STORE(decode, srcword) ;
      decode += BLIT_WIDTH_BYTES ;

      outbytes -= BLIT_WIDTH_BYTES;
      ping = pong;
      --wordsleft;
    }
    srcword = ping << 2;
    if ( outbytes > 0 ) {
      HQASSERT(outbytes <= BLIT_WIDTH_BYTES, "t32_up_form: need more bytes than expected");

      switch ( outbytes ) {
#if BLIT_WIDTH_BYTES > 4
      case 8:
        *decode++ = (uint8)(srcword >> (BLIT_WIDTH_BITS - 8));  srcword <<= 8;
      case 7:
        *decode++ = (uint8)(srcword >> (BLIT_WIDTH_BITS - 8));  srcword <<= 8;
      case 6:
        *decode++ = (uint8)(srcword >> (BLIT_WIDTH_BITS - 8));  srcword <<= 8;
      case 5:
        *decode++ = (uint8)(srcword >> (BLIT_WIDTH_BITS - 8));  srcword <<= 8;
#endif
      case 4:
        *decode++ = (uint8)(srcword >> (BLIT_WIDTH_BITS - 8));  srcword <<= 8;
      case 3:
        *decode++ = (uint8)(srcword >> (BLIT_WIDTH_BITS - 8));  srcword <<= 8;
      case 2:
        *decode++ = (uint8)(srcword >> (BLIT_WIDTH_BITS - 8));  srcword <<= 8;
      case 1:
        *decode++ = (uint8)(srcword >> (BLIT_WIDTH_BITS - 8));
        break ;
      default:
        HQFAIL("Invalid number of bytes in a blit_t word") ;
        break ;
      }
    }

    ++inlines;
    HQASSERT(data == theFormA(*f) + inlines * words_per_line,
             "t32_up_form: pointer sanity check failed");
  }
}



/* ------------------------------------------------------------------------- */
/* -----------------  DEBUG ONLY ROUTINES BELOW THIS POINT ----------------- */
/* ------------------------------------------------------------------------- */

#if defined( DEBUG_BUILD )

static void t32_test_decode(CHARCACHE *cptr)
{
  T32METRICS metrics;
  uint8 *bitmap;

  t32_derive_metrics(cptr, &metrics);
  t32debug_dump_metrics(&metrics, cptr);

  skg_dump_form_binary(cptr);

  bitmap = (uint8*)mm_alloc(mm_pool_temp,
                            metrics.bytewidth * metrics.height,
                            MM_ALLOC_CLASS_T32_IMAGESRC);
  if ( ! bitmap ) {
    monitorf((uint8*)"t32_test_decode: failed to alloc bitmap store");
    return;
  }

  t32_unpack_form(cptr, &metrics, bitmap);
  skg_dump_string(bitmap, metrics.width, metrics.height);

  mm_free(mm_pool_temp, (mm_addr_t)bitmap, metrics.bytewidth * metrics.height);
}

static void t32debug_dump_metrics(T32METRICS *m, CHARCACHE *c)
{
  monitorf((uint8*)"\n\nDumping CID %d\n", oInteger(theGlyphName(*c)));
  monitorf((uint8*)"[ %2.1lf %2.1lf    %2d %2d %2d %2d ",
           m->w0x, m->w0y, m->llx, m->lly, m->urx, m->ury);

  if ( m->num_metrics == 10 )
    monitorf((uint8*)"   %2.1lf %2.1lf %2.1lf %2.1lf ",
             m->w1x, m->w1y, m->vx, m->vy);

  monitorf((uint8*)"]\nWidth : %d  Height : %d\n\n", m->width, m->height);
}

static void skg_dump_form_binary(CHARCACHE *cptr)
{
  FORM *f = theForm(*cptr);
  int32 w = theFormW(*f);
  int32 h = theFormH(*f);
  int32 lbytes = theFormL(*f);
  int32 tbytes = theFormT(*f);
  int32 rh = theFormRH(*f);
  int32 hoff = theFormHOff(*f);

  int32 line;
  int32 topbit = w - 1;

  monitorf((uint8*)"Dumping cid: %d\n", oInteger(theGlyphName(*cptr)));
  monitorf((uint8*)"  Width : %4d  Height: %4d\n", w, h);
  monitorf((uint8*)"  LBytes: %4d  TBytes: %4d\n", lbytes, tbytes);
  monitorf((uint8*)"  RH    : %4d  HOff  : %4d\n", rh, hoff);
  monitorf((uint8*)"  XBear : %4f  YBear : %4f\n\n",
           theXBearing(*cptr), theYBearing(*cptr));

  for ( line = 0 ; line < h; ++line ) {
    int32 bit;
    int32 bitindex = BLIT_WIDTH_BITS - 1;
    blit_t bitval;
    uint8 out[255];
    uint8* outpos = out;
    blit_t *data = BLIT_ADDRESS(theFormA(*f), line * lbytes) ;

    for ( bit = 0 ; bit <= topbit; ++bit ) {

      /** \todo @@@ TODO FIXME ajcd 2005-06-26: This should be conditional on
          bitsgoright/bitsgoleft. */
      bitval = ((*data) >> bitindex) & 1 ;

      if ( bit == 2 || ((bit - 2) & 3) == 0 )
        *outpos++ = (uint8)' ';

      *outpos++ = (uint8)(bitval + '0');

      if ( (--bitindex) < 0 ) {
        *outpos++ = '\0';
        monitorf(out);
        outpos = out;
        ++data;
        bitindex = BLIT_WIDTH_BITS - 1;
      }
    } /* for bit */

    if ( outpos != out ) {
      *outpos++ = '\0';
      monitorf(out);
    }
    monitorf((uint8*)"\n");
  } /* for line */

  monitorf((uint8*)"\n");
}

static void skg_dump_string(uint8 *data_string, int32 width, int32 height)
{
  int32 bytes_per_line = (width + 7) >> 3;
  int32 line, byte;

  HQASSERT(data_string, "skg_dump_string: no data string");

  for ( line = 0; line < height ; ++line ) {
    for ( byte = 0 ; byte < bytes_per_line; ++byte)
      monitorf((uint8*)"%.2x", *data_string++);
    monitorf((uint8*)"\n");
  }
}

#endif /* defined( DEBUG_BUILD ) */

void init_C_globals_t32font(void)
{
#if defined( DEBUG_BUILD )
  t32_debug = 0;
#endif
}

/* Log stripped */
