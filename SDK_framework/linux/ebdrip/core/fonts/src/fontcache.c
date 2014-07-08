/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:fontcache.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to manage the font cache, font info, encoding lookups and font
 * objects.
 */

#include "core.h"
#include "fontcache.h"

#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "namedef_.h"
#include "coreinit.h"

#include "fonts.h"
#include "fontparam.h"
#include "uniqueid.h" /* UniqueID ranges */

#include "matrix.h"
#include "graphics.h" /* FONTinfo */
#include "often.h"    /* SwOften */
#include "rlecache.h" /* form_to_rle */
#include "dlstate.h"  /* DL_STATE */
#include "render.h"   /* outputpage */
#include "gstate.h"   /* clear_gstate */
#include "showops.h"  /* theCDevProc */
#include "formOps.h"
#include "lowmem.h"


/* --- Internal variables --- */
int32 no_purge = 0 ; /* This global indicates that the font cache should not
                        be purged. It is used during interpreter callbacks
                        while caching characters. */
static int32 last_purge = 0;

/* Pointer to the cached fonts */
static FONTCACHE *thefontcache = NULL ;

/*---------------------------------------------------------------------------*/
/* Type definitions for font cache structures */
struct MATRIXCACHE {
  OMATRIX omatrix ;
  CHARCACHE *link[ 32 ] ;
  MATRIXCACHE *next ;
} ;

struct FONTCACHE {
  int32 fontid ;
  int32 uniqueid ;
  int32 sid ;
  uint8 fonttype , painttype , cdevproc , unused ;
  USERVALUE strokewidth ;
  MATRIXCACHE *link ;
  FONTCACHE *next ;
} ;

/* cdevproc types for font cache */
enum { CDEVPROC_none = 0, CDEVPROC_std, CDEVPROC_custom } ;


#define fc_has_temp_UID(fc) \
  ((theUniqueID(fc) & 0xFF000000) == UID_RANGE_temp << 24)


/*---------------------------------------------------------------------------*/
/* Create a new char cache form, inserting into the font cache. */
CHARCACHE *fontcache_new_char(FONTinfo *fontInfo, OBJECT *glyphname)
{
  register CHARCACHE *newchar ;
  register MATRIXCACHE *newmatrix ;
  register FONTCACHE *fcptr ;
  int32 linkindex ;
  corecontext_t *context = get_core_context_interp() ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(glyphname, "No font cache key") ;
  HQASSERT(oType(*glyphname) != ONULL, "Font cache key should not be ONULL") ;
  HQASSERT(IS_INTERPRETER(), "Creating new character but not interpreter") ;

  /* Insert new font if necessary. */
  if (( fcptr = theLookupFont( *fontInfo )) == NULL ) {
    if (( fcptr = (FONTCACHE *)mm_alloc(mm_pool_temp,
                                        sizeof(FONTCACHE),
                                        MM_ALLOC_CLASS_FONT_CACHE)) == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    if ( oType(theCDevProc(*fontInfo)) == OOPERATOR &&
         oExecutable(theCDevProc(*fontInfo)) &&
         oOp(theCDevProc(*fontInfo)) == &system_ops[NAME_stdCDevProc] )
      fcptr->cdevproc = CDEVPROC_std ;
    else if ( oType(theCDevProc(*fontInfo)) != ONULL )
      fcptr->cdevproc = CDEVPROC_custom ;
    else
      fcptr->cdevproc = CDEVPROC_none ;

    theUniqueID(*fcptr) = theUniqueID(*fontInfo) ;
    theFontType(*fcptr) = theFontType(*fontInfo) ;
    thePaintType(*fcptr) = thePaintType(*fontInfo) ;
    theStrokeWidth(*fcptr) = theStrokeWidth(*fontInfo) ;

    theFontId(*fcptr) = theCurrFid(*fontInfo) ;
    theISaveLevel(fcptr) = context->savelevel ;

    fcptr->link = NULL ;
    fcptr->next = thefontcache ;
    thefontcache = fcptr ;
    theLookupFont( *fontInfo ) = fcptr ;
    context->fontsparams->CurCacheFonts += 1 ;
  }

  /* Insert new matrix if necessary. */
  if (( newmatrix = theLookupMatrix( *fontInfo )) == NULL ) {
    if (( newmatrix = (MATRIXCACHE *)mm_alloc(mm_pool_temp,
                                              sizeof(MATRIXCACHE),
                                              MM_ALLOC_CLASS_MATRIX_CACHE)) == NULL ) {
      (void)error_handler( VMERROR ) ;
      return NULL ;
    }

    theLookupMatrix( *fontInfo ) = newmatrix ;
    newmatrix->next = theLookupFont( *fontInfo )->link;
    theLookupFont( *fontInfo )->link = newmatrix ;
    for ( linkindex = 0 ; linkindex < 32 ; ++linkindex )
      newmatrix->link[linkindex] = NULL ;
    MATRIX_COPY( & newmatrix->omatrix , & theFontMatrix( *fontInfo )) ;
    context->fontsparams->CurCacheMatrix += 1;
  }

  /* Insert new character. */
  if (( newchar = alloc_ccache()) == NULL ) {
    (void)error_handler( VMERROR ) ;
    return NULL ;
  }

  linkindex = oInteger(*glyphname) & 31 ;
  newchar->next = newmatrix->link[ linkindex ] ;
  newmatrix->link[ linkindex ] = newchar ;

  Copy(&theGlyphName(*newchar), glyphname) ;

  theFormT(*newchar ) = FORMTYPE_CHARCACHE ;
  newchar->pageno = newchar->baseno = context->page->eraseno ;
  theICharWMode( newchar ) = theWMode( *fontInfo ) ;

  context->fontsparams->CurCacheChars += 1 ;

  /* Matrix comparison pointer for trapping */
  newchar->matrix = newmatrix ;

  return newchar ;
}

/*---------------------------------------------------------------------------*/

/* Now try to matchup UniqueID's (and other necessary stuff). The fontcache
   list is re-linked to be an MRU (most recently used) list. */
Bool fontcache_lookup_font(FONTinfo *fontInfo)
{
  corecontext_t *context = get_core_context_interp();

  HQASSERT(fontInfo, "No font info") ;

  /* Can't match new font against old one with Metrics dictionaries */
  if ( theUniqueID(*fontInfo) != -1 &&
       !theMetrics(*fontInfo) && !theMetrics2(*fontInfo) ) {
    FONTCACHE *fcptr, **fprev ;
    uint8 cdevproc = CDEVPROC_none ;

    if ( oType(theCDevProc(*fontInfo)) == OOPERATOR &&
         oExecutable(theCDevProc(*fontInfo)) &&
         oOp(theCDevProc(*fontInfo)) == &system_ops[NAME_stdCDevProc] )
      cdevproc = CDEVPROC_std ;
    else if ( oType(theCDevProc(*fontInfo)) != ONULL )
      return TRUE ;

    for ( fprev = &thefontcache ;
          (fcptr = *fprev) != NULL ;
          fprev = &fcptr->next) {
      if ( theISaveLevel( fcptr ) < 0 ) {
        if ( theUniqueID(*fcptr) == theUniqueID(*fontInfo) &&
             theFontType(*fcptr) == theFontType(*fontInfo) &&
             thePaintType(*fcptr) == thePaintType(*fontInfo) &&
             theStrokeWidth(*fcptr) == theStrokeWidth(*fontInfo) &&
             fcptr->cdevproc == cdevproc ) {
          /* Re-link MRU list*/
          *fprev = fcptr->next ;
          fcptr->next = thefontcache ;
          thefontcache = fcptr ;

          theFontId(*fcptr) = theCurrFid(*fontInfo) ;
          theISaveLevel( fcptr ) = context->savelevel ;
          theLookupFont( *fontInfo) = fcptr ;
          return TRUE ;
        }
      }
    }
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            get_font()         author:              Andrew Cave
   creation date:       06-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   Looks up the ( possibly ) new font in the font list.
   The fontcache list is re-linked to be an MRU (most recently used) list.
---------------------------------------------------------------------------- */
Bool fontcache_lookup_fid(FONTinfo *fontInfo)
{
  register FONTCACHE *fcptr, **fprev ;

  HQASSERT(fontInfo, "No font info") ;

/* Lookup font - straight id match */
  for ( fprev = &thefontcache ;
        (fcptr = *fprev) != NULL ;
        fprev = &fcptr->next)
    if ( theFontId(*fcptr) == theCurrFid(*fontInfo) ) {
      /* Re-link MRU list*/
      *fprev = fcptr->next ;
      fcptr->next = thefontcache ;
      thefontcache = fcptr ;

      theLookupFont(*fontInfo) = fcptr ;

      return TRUE ;
    }

  return FALSE ;
}

/* ----------------------------------------------------------------------------
   function:            get_matrix(..)     author:              Andrew Cave
   creation date:       06-Oct-1987        last modification:   12-Jan-1995
   arguments:           none .
   description:

   Looks up the ( possibly ) new matrix in the matrix list.

   modifications:

   12-Jan-1995 dstrauss (Dave Strauss)
       Replace compareSysVals calls with straight "==" - this is the
       only way to guarantee that character bitmaps will be pixel-for-
       pixel equivalent.

   11-Jan-1995 dstrauss (Dave Strauss)
       Replace (fabs(a - b) < EPSILON) comparisons with call to
       new function compareSysVals, which compares SYSTEMVALUES to
       24 bits.

---------------------------------------------------------------------------- */
Bool fontcache_lookup_matrix(FONTinfo *fontInfo)
{
  OMATRIX *mptr ;
  MATRIXCACHE *amatrix, **mprev ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(theLookupFont(*fontInfo), "No lookup font") ;

/* Lookup matrix */
  mptr = & theFontMatrix(*fontInfo) ;

  for ( mprev = &(theLookupFont(*fontInfo)->link);
        (amatrix = *mprev) != NULL ;
        mprev = &amatrix->next)
    if ( MATRIX_EQ( & amatrix->omatrix, mptr )) {
      /* Re-link MRU list*/
      *mprev = amatrix->next ;
      amatrix->next = theLookupFont(*fontInfo)->link;
      theLookupFont(*fontInfo)->link = amatrix ;

      theLookupMatrix(*fontInfo) = amatrix ;
      return TRUE ;
    }

  return FALSE ;
}

Bool fontcache_lookup_matrix_t32(FONTinfo *fontInfo)
{
  OMATRIX *mptr ;
  MATRIXCACHE *amatrix, **mprev ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(theLookupFont(*fontInfo), "No lookup font") ;

/* Lookup matrix */
  mptr = & theFontMatrix(*fontInfo) ;

  for ( mprev = &(theLookupFont(*fontInfo)->link) ;
        (amatrix = *mprev) != NULL ;
        mprev = &amatrix->next)
    if ( MATRIX_REQ( & amatrix->omatrix, mptr )) {
      /* Re-link MRU list*/
      *mprev = amatrix->next ;
      amatrix->next = theLookupFont(*fontInfo)->link;
      theLookupFont(*fontInfo)->link = amatrix ;

      theLookupMatrix(*fontInfo) = amatrix ;
      return TRUE ;
    }

  return FALSE ;
}

/* Function for HDLT to get at matrix used for lookup */
OMATRIX *fontcache_current_matrix(FONTinfo *fontInfo)
{
  HQASSERT(fontInfo, "No font info") ;

  if ( theLookupMatrix(*fontInfo) )
    return &theLookupMatrix(*fontInfo)->omatrix ;

  return NULL ;
}

/* ----------------------------------------------------------------------------
   function:            get_char()         author:              Andrew Cave
   creation date:       06-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   Looks up the ( possibly ) new character in the character list.
   Returns FALSE if doesn't exist, otherwise TRUE.

---------------------------------------------------------------------------- */
CHARCACHE *fontcache_lookup_char(FONTinfo *fontInfo, OBJECT *glyphname)
{
  int32 linkindex ;
  CHARCACHE *cptr ;
  MATRIXCACHE *mptr ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(glyphname, "No font cache key") ;

  linkindex = oInteger(*glyphname) & 31 ;

  mptr = theLookupMatrix(*fontInfo) ;
  HQASSERT( mptr , "looking up character but NULL mptr" ) ;

  for ( cptr = mptr->link[ linkindex ] ;
        cptr != NULL ;
        cptr = cptr->next)
    if ( oInteger(*glyphname) == oInteger(theGlyphName(*cptr)) &&
         oType(*glyphname) == oType(theGlyphName(*cptr)) &&
         theWMode(*fontInfo) == theICharWMode( cptr ))
      return cptr ;

  return NULL ;
}

/* Type 32 variant lookup for missing WMode; use other WMode's cache,
   if present. */
CHARCACHE *fontcache_lookup_char_wmode(FONTinfo *fontInfo, OBJECT *glyphname)
{
  int32 linkindex ;
  CHARCACHE *cptr, *found = NULL ;
  MATRIXCACHE *mptr;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(glyphname, "No font cache key") ;

  linkindex = oInteger(*glyphname) & 31 ;

  mptr = theLookupMatrix(*fontInfo) ;
  HQASSERT( mptr , "looking up character but NULL mptr" ) ;

  for ( cptr = mptr->link[ linkindex ] ;
        cptr != NULL ;
        cptr = cptr->next) {
    if ( oInteger(*glyphname) == oInteger(theGlyphName(*cptr)) &&
         oType(*glyphname) == oType(theGlyphName(*cptr)) ) {
      if ( theWMode(*fontInfo) == theICharWMode( cptr ) )
        return cptr;
      else /* Everything matches except the wmode */
        found = cptr;
    }
  }

  return found ;
}

/* Type 32 fonts have a master character definition inserted into the font
   cache with the identity matrix, created by addglyph. This is used both as
   a character definition, and to generate image data for other scalings of
   the glyph. The Type 32 master font cache lookup is indexed by an integer.
   The non-master caches are indexed by the master caches' address. */
CHARCACHE *fontcache_lookup_char_t32(FONTinfo *fontInfo, OBJECT *glyphname)
{
  int32 linkindex ;
  CHARCACHE *cptr ;
  MATRIXCACHE *mptr ;
  FONTCACHE *fcptr ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(glyphname, "No font cache key") ;

  linkindex = oInteger(*glyphname) & 31 ;

  fcptr = theLookupFont(*fontInfo);
  HQASSERT(fcptr, "No lookup font set");

  /* Find the entry for the identity cache */
  for ( mptr = fcptr->link ; mptr ; mptr = mptr->next)
    if ( MATRIX_EQ(&mptr->omatrix, &identity_matrix) )
      break;

  /* No characters defined if we can't find this matrix */
  if ( ! mptr )
    return NULL;

  /* Now have a go at getting the master definintion */
  for ( cptr = mptr->link[ linkindex ] ;
        cptr != NULL ;
        cptr = cptr->next)
    if ( oInteger(*glyphname) == oInteger(theGlyphName(*cptr)) &&
         oType(*glyphname) == oType(theGlyphName(*cptr)) &&
         isIT32Master(cptr) )
      return cptr ;

  return NULL ;
}


/* ----------------------------------------------------------------------------
   This function returns a flag indicating whether the two chars given
   come from the same MATRIXCACHE. It does so by comparing the opaque
   matrix pointers in the CHARCACHE struct. THIS IS ITS ONLY
   PURPOSE. Don't go casting it back to a MATRIXCACHE and using it for
   anything else! When this code gets rewritten, this will be done
   more nicely.
---------------------------------------------------------------------------- */

Bool chars_have_same_matrix(CHARCACHE *char1 , CHARCACHE *char2)
{
  return ( char1->matrix == char2->matrix ) ;
}

/* See header for doc. */
CHARCACHE* alloc_ccache( void )
{
  CHARCACHE* cptr = (CHARCACHE *)mm_alloc(mm_pool_temp, sizeof(CHARCACHE),
                                          MM_ALLOC_CLASS_CHAR_CACHE);
  if (cptr != NULL) {
    CHARCACHE template = {0};
    *cptr = template;
  }
  return cptr;
}

/* Low-level cache free. Used to be a macro in macros.h, no longer appropriate
 * since the trap info became a sub-structure. Type 32 data now freed here too.
 */
void free_ccache( CHARCACHE *cptr )
{
  if ( cptr->thebmapForm != NULL ) {
    destroy_Form( cptr->thebmapForm ) ;
  }

  if ( cptr->inverse != NULL ) {
    offsetFormDelete( cptr->inverse ) ;
  }

  /* Free any type 32 data structures. */

  if ( cptr->t32data ) {
    HQASSERT( isIT32Master( cptr ) ,
              "free_ccache: CHARCACHE T32 data suspect.");
    mm_free( mm_pool_temp , ( mm_addr_t )cptr->t32data ,
             sizeof( T32_DATA )) ;
    cptr->t32data = NULL ;
  }

  mm_free( mm_pool_temp , ( mm_addr_t )cptr , sizeof( CHARCACHE )) ;
}

/*---------------------------------------------------------------------------*/
/* Free a character still under construction, if an error occurs while filling
   it. */
void fontcache_free_char(FONTSPARAMS *fontparams,
                         FONTinfo *fontInfo, CHARCACHE *cptr)
{
  int32 linkindex ;
  MATRIXCACHE *mptr ;

  HQASSERT(fontInfo, "No font info") ;

  if ( ! cptr )
    return ;

  mptr = theLookupMatrix(*fontInfo) ;
  if ( ! mptr )
    return ;

  linkindex = oInteger(theGlyphName(*cptr)) & 31 ;

/* Relink top level cache. */
  HQASSERT( mptr->link[ linkindex ] == cptr , "CHARCACHE got out of sync" ) ;
  mptr->link[ linkindex ] = cptr->next ;
  fontparams->CurCacheChars -= 1 ;

  fontparams->CurFontCache -= ALIGN_FORM_SIZE(theFormS(*theForm(*cptr)));

  free_ccache( cptr ) ;
}

/*---------------------------------------------------------------------------*/
/* Mark everything at the current savelevel as eligible for purging */
void fontcache_restore( int32 slevel )
{
  register FONTCACHE *fcptr ;

  for ( fcptr = thefontcache ; fcptr ; fcptr = fcptr->next)
    if ( theISaveLevel( fcptr ) > slevel )
      theISaveLevel( fcptr ) = -1 ;
}


static Bool fontcache_compressing = FALSE ;

Bool fontcache_is_compressing(void)
{
  return fontcache_compressing ;
}

static void fontcache_compress(FONTSPARAMS *fontsparams)
{
  register int32 curntnumber ;
  register int32 erasenumber ;
  register int32 i , rledsomething ;
  register CHARCACHE *cptr ;
  register MATRIXCACHE *mptr ;
  register FONTCACHE *fptr ;

  /* If we can't purge, have nothing to purge, have compressed all characters
     and new ones, or are currently generating a character, we can't compress
     the fontcache. */
  if ( no_purge ||
       fontsparams->CurFontCache == 0 ||
       char_doing_cached() ||
       fontcache_compressing )
    return ;

  rledsomething = 0 ;
  erasenumber = outputpage_lock()->eraseno ; outputpage_unlock() ;
  curntnumber = inputpage_lock()->eraseno ;
#define return DO_NOT_return_UNLOCK_INPUTPAGE_INSTEAD!

  HQTRACE( debug_lowmemory,
           ( "rlefontcache: (%d)", fontsparams->CurFontCache )) ;

  for ( fptr = thefontcache ; fptr ; fptr = fptr->next) {
    for ( mptr = fptr->link ; mptr ; mptr = mptr->next) {
      CHARCACHE **clistptr = mptr->link;
      for ( i = 0 ; i < 32 ; ++ i )
        for ( cptr = (*clistptr++) ; cptr ; cptr = cptr->next) {

          SwOftenUnsafe() ;

          /* We can compress any character that is older than any DL page, or
             any character on the current input page (because it can't be
             simultaneously rendering and this handler is only called from
             the interpreter at specific points). */
          if ( cptr->pageno < erasenumber || cptr->baseno == curntnumber )
            /* Can't currently rle a master Type 32 definition.
               t32_unpack_form needs changing to support this. */
            if ( theFormT(*theForm(*cptr)) == FORMTYPE_CACHEBITMAP &&
                 cptr->rlesize == 0 &&
                 ! isIT32Master( cptr ) )
              /** \todo ajcd 2011-03-07: This function is not MT-safe, it
                  uses the interpret basemap. */
              if ( form_to_rle( cptr , 0 ))
                ++rledsomething ;
        }
    }
  }

  inputpage_unlock() ;
#undef return

  HQTRACE( debug_lowmemory,
           ( "rlefontcache: (%d,%d)", rledsomething, fontsparams->CurFontCache )) ;

  return ;
}

/* -------------------------------------------------------------------------- */
/* Mark fonts and cached glyphs that can't be reused beyond the end of this page
 *
 * Such glyphs will be discarded by the next fontcache_purge_useless(). This
 * function is used by PFIN if a PFIN module wants to delete or redefine a
 * glyph.
 */
void fontcache_make_useless(int32 UniqueID, OBJECT *glyphname)
{
  FONTCACHE *font, *adoptive ;
  int32     linkindex ;

  /* Avoid silliness */
  if (UniqueID == -1)
    return ;

  if (glyphname == NULL) {
    /* Discard whole font (by making it useless and inaccessible) */

    for (font = thefontcache; font; font = font->next) {
      if (theUniqueID(*font) == UniqueID) {
        theUniqueID(*font) = -1 ;
        theFontId(*font) = -1 ;    /* We don't want the font being used again */
      }
    }
    return ;
  }

  /* Discard a glyph from all instances of the font... */

  /* The link hash within a MATRIXCACHE */
  linkindex = oInteger(*glyphname) & 31 ;

  /* Find an adoptive parent that is to be purged - the glyph is moved to this
   * font if its parent is not a temporary, so that the glyph will be purged
   * at the end of the page. It is also obfuscated so that it can't be
   * matched again.
   */
  for (adoptive = thefontcache ;
       adoptive && !fc_has_temp_UID(*adoptive) ;
       adoptive = adoptive->next) ;  /* if 0 we will create one on demand */

  for (font = thefontcache ; font ; font = font->next) {
    /* Check every font for this ID */
    if (theUniqueID(*font) == UniqueID) {
      MATRIXCACHE *matrix ;
      for (matrix = font->link; matrix; matrix = matrix->next) {
        /* For every size of this font */
        CHARCACHE *chr, *prev ;

        SwOftenUnsafe() ;

        for (prev = NULL, chr = matrix->link[linkindex] ;
             chr ;
             prev = chr, chr = chr->next) {
          /* check every glyph on this hash list */
          if (oType(*glyphname) == oType(chr->glyphname) &&
              ((oType(*glyphname) == OINTEGER &&
                oInteger(*glyphname) == oInteger(chr->glyphname)) ||
               (oType(*glyphname) == ONAME &&
                oName(*glyphname) == oName(chr->glyphname)))) {
            /* This glyph must be "removed" from the fontcache.
             *
             * We can't actually kill it yet because the display list may
             * still need it, so we make it unmatchable and attach it to a
             * font that is to be purged.
             */

            /* Delink from real parent */
            if (prev)
              prev->next = chr->next ;
            else
              matrix->link[linkindex] = chr->next ;

            /* Make it unmatchable */
            theTags(chr->glyphname) = ONOTHING | LITERAL ;

            /* Ensure we have an adoptive parent */
            if (adoptive == NULL) {
              /* Make a sacrificial fontcache/matrixcache parent for
               * discarded glyphs, if we couldn't find a suitable one.
               */
              const static FONTCACHE zerofontcache = {-1,-1,-1} ; /* No fid/uid */
              const static MATRIXCACHE zeromatrixcache = {0} ;

              if ((adoptive = mm_alloc(mm_pool_temp,
                                       sizeof(FONTCACHE),
                                       MM_ALLOC_CLASS_FONT_CACHE)) == NULL ) {
                (void)error_handler(VMERROR) ;
                return ;
              }
              *adoptive = zerofontcache ;
              if ((adoptive->link = mm_alloc(mm_pool_temp,
                                             sizeof(MATRIXCACHE),
                                             MM_ALLOC_CLASS_FONT_CACHE)) == NULL) {
                mm_free(mm_pool_temp, adoptive, sizeof(FONTCACHE)) ;
                (void)error_handler(VMERROR) ;
                return ;
              }
              *adoptive->link = zeromatrixcache ;

              adoptive->next = thefontcache ;
              thefontcache = adoptive ;
            }

            /* adopt by font to be purged (use linkindex to spread the load) */
            chr->next = adoptive->link->link[linkindex] ;
            adoptive->link->link[linkindex] = chr ;

          } /* if chr == glyphname */
        } /* for chr */
      } /* for matrix */
    } /* if UniqueID */
  } /* for font */

  return ;
}

/* -------------------------------------------------------------------------- */
/* fontcache_purge_useless() removes font cache references which can never be
   re-used. These are fonts whose save level is -1 (i.e. they're not
   referenced in PostScript anymore, and their UniqueID is -1 (or was allocated
   by getnewuniqueid and so is of the form 0xFFxxxxxx), so they can't be used by
   subsequent jobs. */
void fontcache_purge_useless(int32 erasenumber)
{
  register FONTCACHE *fcptr ;
  register FONTCACHE **fprev = & thefontcache ;
  FONTSPARAMS *fontparams = get_core_context_interp()->fontsparams;

  while ((fcptr = *fprev) != NULL) {
    if (theISaveLevel(fcptr) < 0 &&
        (fc_has_temp_UID(*fcptr) || fcptr->cdevproc == CDEVPROC_custom)) {
      register MATRIXCACHE *mcptr ;
      MATRIXCACHE **mprev = &(fcptr->link);

      while ((mcptr = *mprev) != NULL) {
        register CHARCACHE *cptr ;
        CHARCACHE **clistptr = mcptr->link;
        register int32 i ;
        int32 anyleft = FALSE ;

        SwOftenUnsafe() ;

        for ( i = 0 ; i < 32 ; ++i ) {
          CHARCACHE **cprev = & clistptr[i] ;

          while ((cptr = *cprev) != NULL) {
            if ( cptr->pageno < erasenumber ) {
              SwOftenUnsafe() ;

              *cprev = cptr->next ; /* remove char from chain */
              fontparams->CurCacheChars -= 1 ;
              fontparams->CurFontCache -=
                ALIGN_FORM_SIZE(theFormS(*theForm(*cptr)));
              free_ccache(cptr) ;
            } else {
              cprev = & cptr->next ;
              anyleft = TRUE ;
            }
          }
        }
        if ( ! anyleft ) {
          *mprev = mcptr->next ; /* remove matrix from chain */
          fontparams->CurCacheMatrix -= 1 ;
          if (gstateptr->theFONTinfo.lmatrix == mcptr) /* tidy up gstate */
            gstateptr->theFONTinfo.lmatrix = NULL ;
          mm_free(mm_pool_temp, (mm_addr_t)mcptr, sizeof(MATRIXCACHE)) ;
        } else
          mprev = & mcptr->next ;
      }
      if (fcptr->link == NULL) {
        *fprev = fcptr->next ; /* remove font from chain */
        fontparams->CurCacheFonts -= 1 ;
        if (gstateptr->theFONTinfo.lfont == fcptr) /* tidy up gstate */
          gstateptr->theFONTinfo.lfont = NULL ;
        mm_free(mm_pool_temp, (mm_addr_t)fcptr, sizeof(FONTCACHE)) ;
      } else
        fprev = & fcptr->next ;
    } else
      fprev = & fcptr->next ;
  }

  fontcache_compressing = FALSE ;
}


static Bool clear_one_lookup_font(GSTATE *gs, void *unused_arg)
{
  UNUSED_PARAM(void *, unused_arg);

  HQASSERT(gs, "gs is null");

  theLookupFont(theFontInfo(*gs)) = NULL;
  theLookupMatrix(theFontInfo(*gs)) = NULL;
  return TRUE; /* continue walk */
}


static void clear_fonts_from_gstates(void)
{
  (void)gs_forall(clear_one_lookup_font, NULL, FALSE, TRUE);
}


static void fontcache_purge(FONTSPARAMS *fontsparams, int32 purge)
{
  register int32 i, level, removedchars, removedfontmatrix;
  Bool anyleft;
  register CHARCACHE *cptr ;
  register MATRIXCACHE *mptr ;
  register FONTCACHE *fptr ;
  register CHARCACHE **clistptr ;
  CHARCACHE **cprev ;
  MATRIXCACHE **mprev ;
  FONTCACHE **fprev ;
  int32 erasenumber ;

  if ( fontcache_compressing )
    return; /* pointless */

  level = fontsparams->CurFontCache - purge;
  HQTRACE( debug_lowmemory,
           ( "purge_fcache: (%d, %d, %d)", purge,
             fontsparams->MaxFontCache, fontsparams->CurFontCache ));

  /* Remove matrices that have a showpage number less than or equal to value, */
  /* and remove any fonts that end up having a null set of matrices. */
  removedchars = 0 ;
  removedfontmatrix = 0 ;
  erasenumber = outputpage_lock()->eraseno ; outputpage_unlock() ;

  while (( fontsparams->CurFontCache > level ) &&
         ( last_purge < erasenumber )) {
    int32 next_purge ;

    /* Remember the latest page to purge next iteration */
    next_purge = erasenumber ;

    fprev = ( & thefontcache ) ;
    fptr = (*fprev) ;

    while ( fptr ) {

      if (gstateptr->theFONTinfo.lfont == fptr) {
        /* Don't purge the font that the gstate is currently
           using. Even if we're being called from the low memory
           handler, we should be between operators. */
        HQASSERT(IS_INTERPRETER(),
                 "The gstate check is only safe for the interpreter thread") ;
        fprev = & fptr->next ;
        fptr = (*fprev) ;
        continue ;
      }

      mprev = &(fptr->link);
      mptr = (*mprev) ;

      while ( mptr ) {

        SwOftenUnsafe() ;

        if ( fontsparams->CurFontCache <= level )
          goto quickout ;  /* Really require a break 3 */

        /* Free all the characters hanging off the matrix. */
        anyleft = FALSE ;
        clistptr = mptr->link;
        for ( i = 0 ; i < 32 ; ++ i ) {
          cprev = ( & clistptr[ i ] ) ;
          cptr = (*cprev) ;
          while ( cptr )
            if ( cptr->pageno <= last_purge ) {
              if ( isIT32Master(cptr) ) {
                /* Can't flush this character out of the cache as it's the
                   master definition of a type 32 glyph. */
                anyleft = TRUE;
                cprev = ( & cptr->next) ;
                cptr = (*cprev) ;
              } else {
                SwOftenUnsafe() ;

                ++removedchars ;

                (*cprev) = cptr->next ;
                fontsparams->CurCacheChars -= 1 ;
                fontsparams->CurFontCache -=
                  ALIGN_FORM_SIZE(theFormS(*theForm(*cptr)));
                free_ccache( cptr ) ;
                cptr = (*cprev) ;
              }
            }
            else {
              /* Update which page to purge next iteration */
              if ( cptr->pageno < next_purge )
                next_purge = cptr->pageno ;

              anyleft = TRUE ;

              cprev = ( & cptr->next) ;
              cptr = (*cprev) ;
            }
        }
        if ( ! anyleft ) {
          fontsparams->CurCacheMatrix -= 1 ;
          ++removedfontmatrix ;
          (*mprev) = mptr->next ;
          mm_free(mm_pool_temp, (mm_addr_t)mptr, sizeof(MATRIXCACHE)) ;
          mptr = (*mprev) ;
        }
        else {
          mprev = ( & mptr->next) ;
          mptr = (*mprev) ;
        }
      }
      if ( ! fptr->link ) {
        fontsparams->CurCacheFonts -= 1 ;
        ++removedfontmatrix ;
        (*fprev) = fptr->next ;
        mm_free(mm_pool_temp, (mm_addr_t)fptr, sizeof(FONTCACHE)) ;
        fptr = (*fprev) ;
      }
      else {
        fprev = & fptr->next ;
        fptr = (*fprev) ;
      }
    }
    HQASSERT( next_purge > last_purge, "purge_fcache: bad next_purge" );
    last_purge = next_purge ;
  }

quickout: /* efficiency exit from above - done enough */

  HQTRACE( debug_lowmemory,
           ( "purge_fcache: (%d,%d)", removedchars, fontsparams->CurFontCache )) ;

  if ( removedfontmatrix )
    clear_fonts_from_gstates();

  if ( removedchars ) {
    /* If we successfully removed characters, resume normal
       (non-compressing) service. */
    fontcache_compressing = FALSE ;
  } else {
    if ( !fontcache_compressing )
      fontcache_compress(fontsparams) ;
    fontcache_compressing = TRUE ;
  }
  return;
}


void fontcache_check_limits(FONTSPARAMS *fontsparams)
{
  /* I'm not sure this is good idea, but it's what the code was trying
     to do before. - pekka 2011-02-09 */
  if ( (fontsparams->CurCacheMatrix > fontsparams->MaxCacheMatrix
        || fontsparams->CurCacheChars > fontsparams->MaxCacheChars
        || fontsparams->CurFontCache > fontsparams->MaxFontCache)
       /* If building a character, can't purge. */
       && !no_purge && !char_doing_cached() )
    fontcache_purge(fontsparams,
                    max(fontsparams->CurFontCache - fontsparams->MaxFontCache,
                        1));
}


void fontcache_clear(FONTSPARAMS *fontsparams)
{
  HQASSERT(fontsparams != NULL, "NULL fontsparams pointer");

  /* If building a character, can't allow a purge. */
  if ( no_purge || char_doing_cached() )
    HQFAIL("Must clear font cache, but not allowed.");
  else
    fontcache_purge(fontsparams, fontsparams->CurFontCache);
}


/** Solicit method of the font cache low-memory handler. */
static low_mem_offer_t *fontcache_solicit(low_mem_handler_t *handler,
                                          corecontext_t *context,
                                          size_t count,
                                          memory_requirement_t* requests)
{
  static low_mem_offer_t offer;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !context->between_operators
       /* between_operators implies FontParams is available */
       /* If not allowed, there is nothing to free, building a
          character, or already tried everything and compressed it all,
          it won't get any more blood from this stone. */
       || no_purge || context->fontsparams->CurFontCache == 0 ||
       char_doing_cached() || fontcache_compressing )
    return NULL;

  offer.pool = mm_pool_temp;
  offer.offer_size = (size_t)context->fontsparams->CurFontCache;
  offer.offer_cost = 1.0f;
  offer.next = NULL;
  return &offer;
}


/** Release method of the font cache low-memory handler. */
static Bool fontcache_release(low_mem_handler_t *handler,
                              corecontext_t *context, low_mem_offer_t *offer)
{
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);

  fontcache_purge(context->fontsparams, CAST_SIZET_TO_INT32(offer->taken_size));
  return TRUE;
}


/** The font cache low-memory handler. */
static low_mem_handler_t fontcache_handler = {
  "font glyph cache",
  memory_tier_ram, fontcache_solicit, fontcache_release, FALSE,
  0, FALSE };


/** Font cache initialization. */
static Bool fontcache_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  return low_mem_handler_register(&fontcache_handler);
}


/** Font cache finishing. */
static void fontcache_finish(void)
{
  low_mem_handler_deregister(&fontcache_handler);
}


/* Names are about to be restored (exitserver context). Remove all references
   to names from the font cache, the pointers may be different if the same
   name is used in future. */
void fontcache_restore_names( int32 slevel )
{
  FONTCACHE *fptr ;
  for ( fptr = thefontcache ; fptr ; fptr = fptr->next) {
    MATRIXCACHE *mptr ;
    for ( mptr = fptr->link ; mptr ; mptr = mptr->next) {
      int32 i ;
      for ( i = 0 ; i < 32 ; ++i ) {
        CHARCACHE *cptr ;
        for ( cptr = mptr->link[ i ] ; cptr ; cptr = cptr->next) {
          OBJECT *glyphname = &theGlyphName(*cptr) ;
          if ( oType(*glyphname) == ONAME ) {
            NAMECACHE *nptr = oName(*glyphname) ;
            if ( theISaveLevel( nptr ) > slevel )
              theTags(theGlyphName(*cptr)) = ONULL ;
          }
        }
      }
    }
  }
}

/*---------------------------------------------------------------------------*/
/* Type 32/CID Type 4 fonts manipulate the font cache directly. This loop
   removes a range of characters for a font from the cache. */
void fontcache_remove_chars(int32 fid, int32 firstcid, int32 lastcid)
{
  FONTCACHE    *fptr;
  FONTCACHE   **fprev;
  MATRIXCACHE  *mptr;
  MATRIXCACHE **mprev;
  CHARCACHE    *cptr;
  CHARCACHE   **cprev;
  CHARCACHE   **clistptr ;
  int32 erasenumber ;
  int32 i ;
  int32 looplast = lastcid ;
  int32 removedfontmatrix = 0 ;
  FONTSPARAMS *fontparams = get_core_context_interp()->fontsparams;

  HQASSERT(firstcid >= 0, "CID should not be negative");
  HQASSERT(firstcid <= lastcid, "first and last CID out of order");

  /* First find the font in the cache */
  fprev = &thefontcache;
  fptr = *fprev;
  while ( fptr ) {
    if ( theFontId(*fptr) == fid )
      break;
    fprev = & fptr->next;
    fptr = (*fprev);
  }

  if ( ! fptr )
    return;

  if ( lastcid - firstcid > 31 )
    looplast = firstcid + 31;

  erasenumber = outputpage_lock()->eraseno ; outputpage_unlock() ;
  mprev = &(fptr->link);
  mptr = (*mprev);
  while ( mptr ) {
    /* Free all the characters hanging off the matrix. */
    Bool anyleft = FALSE;
    clistptr = mptr->link;
    for ( i = firstcid; i <= looplast; ++i ) {
      SwOftenUnsafe();
      cprev = ( &clistptr[ i & 31 ] );
      cptr = (*cprev);
      while ( cptr ) {
        if ( oInteger(theGlyphName(*cptr)) >= firstcid &&
             oInteger(theGlyphName(*cptr)) <= lastcid ) {
          /* Always remove the t32data field and clear the master flag,
             as this will allow a standard purge to remove the character */

          if ( cptr->t32data ) {
            HQASSERT(isIT32Master(cptr),
                     "t32_remove_gl: CHARCACHE T32 data suspect");
            mm_free(mm_pool_temp, (mm_addr_t)cptr->t32data,
                    sizeof(T32_DATA));
            cptr->t32data = NULL;
          }
          ClearIT32MasterFlag(cptr);

          /* Now really delete the cache entry if we can */
          if ( cptr->pageno < erasenumber ) {
            (*cprev) = cptr->next;
            fontparams->CurCacheChars -= 1;
            fontparams->CurFontCache -=
              ALIGN_FORM_SIZE(theFormS(*theForm(*cptr)));
            free_ccache( cptr );
            cptr = (*cprev);
          } else {
            anyleft = TRUE;

            cprev = ( & cptr->next);
            cptr = (*cprev);
          }
        } else {
          anyleft = TRUE;

          cprev = ( & cptr->next);
          cptr = (*cprev);
        }
      }
    }
    /* Reset cache pointer. */
    if ( ! anyleft ) {
      /* Reset cache information. */
      fontparams->CurCacheMatrix -= 1;
      ++removedfontmatrix;
      /* Reset cache pointer. */
      (*mprev) = mptr->next;
      mm_free(mm_pool_temp, (mm_addr_t)mptr, sizeof(MATRIXCACHE)) ;
      mptr = (*mprev);
    }
    else {
      mprev = ( & mptr->next);
      mptr = (*mprev);
    }
  }

  if ( ! fptr->link ) {
    fontparams->CurCacheFonts -= 1;
    ++removedfontmatrix;
    (*fprev) = fptr->next;
    mm_free(mm_pool_temp, (mm_addr_t)fptr, sizeof(FONTCACHE)) ;
  }

  if ( removedfontmatrix )
    clear_fonts_from_gstates();
}

/*---------------------------------------------------------------------------*/

size_t fontcache_available_memory(void)
{
  size_t avail = 0 ;
  FONTSPARAMS *fontparams = get_core_context_interp()->fontsparams;

  avail += fontparams->CurCacheFonts
           * SIZE_ALIGN_UP(sizeof(FONTCACHE), MM_TEMP_POOL_ALIGN);
  avail += fontparams->CurCacheMatrix
           * SIZE_ALIGN_UP(sizeof(MATRIXCACHE), MM_TEMP_POOL_ALIGN);
  avail += fontparams->CurCacheChars
           * SIZE_ALIGN_UP(sizeof(CHARCACHE), MM_TEMP_POOL_ALIGN);
  /* The alignment for FORMs is accounted for in CurFontCache. */
  avail += fontparams->CurCacheChars * sizeof(FORM) + fontparams->CurFontCache;
  return avail ;
}

/* Return length of fontcache linked list.  If this becomes significant then an
 * active length count will have to be maintained to eliminate the scan.
 */
static
int32 fontcachesize(void)
{
  FONTCACHE*  fptr;
  int32       count = 0;

  for (fptr = thefontcache; fptr != NULL; fptr = fptr->next) {
    count++;
  }

  return (count);
}

/* Return an array of unique ids for all fontcache entries.  There may be
 * duplicates.  Limited to 64k uids but we should never get there.
 * If we want to remove duplicates, allocate a temporary array to collect all
 * the uids, qsort them into order and then walk to find duplicates and copy
 * down from next unique uid.
 */
Bool currentfontcacheuids_(
  ps_context_t* pscontext)
{
  OBJECT      arr = OBJECT_NOTVM_NOTHING;
  FONTCACHE*  fptr;
  int32       i;

  UNUSED_PARAM(ps_context_t*, pscontext) ;

  HQTRACE(fontcachesize() > MAXUINT16, ("fontcache has more than 64k entries"));

  if (!ps_array(&arr, fontcachesize())) {
    return (FALSE);
  }
  for (i = 0, fptr = thefontcache; fptr != NULL; fptr = fptr->next, i++) {
    object_store_integer(&oArray(arr)[i], fptr->uniqueid);
  }

  return (push(&arr, &operandstack));
}

static void init_C_globals_fontcache(void)
{
  no_purge = 0 ;
  last_purge = 0 ;
  thefontcache = NULL ;
  fontcache_compressing = FALSE ;
}


void fontcache_C_globals(core_init_fns *fns)
{
  init_C_globals_fontcache() ;

  fns->swstart = fontcache_swstart ;
  fns->finish = fontcache_finish ;
}


/*
Log stripped */
