/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!src:renderom.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Separation omission logic
 */

#include "core.h"
#include "renderom.h"
#include "swdevice.h"
#include "objects.h"
#include "namedef_.h"
#include "swerrors.h"           /* error_handler */
#include "often.h"

#include "hqmemset.h"
#include "bitblts.h"
#include "display.h"
#include "dlstate.h"
#include "dl_shade.h"
#include "region.h" /* dlregion_mark */

#include "graphics.h"
#include "gstate.h"
#include "gu_chan.h"
#include "preconvert.h" /* preconvert_on_the_fly */
#include "render.h"
#include "blitcolort.h" /* BLIT_MAX_CHANNELS */

#include "color.h" /* ht_applyTransform */
#include "halftone.h" /* ht_checkequivalentchspotids */
#include "swcopyf.h"
#include "ripdebug.h"

#include "hdlPrivate.h"
#include "groupPrivate.h"
#include "imageo.h" /* IMAGEOBJECT */
#include "imexpand.h"
#include "monitor.h"
#include "params.h"
#include "cvcolcvt.h"

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
#include <stdarg.h>

enum { /* Bit set of debug flags */
  /* These bits control detail */
  DEBUG_OMISSION_DLCOLOR = 1,
  DEBUG_OMISSION_IMAGES = 2,
  DEBUG_OMISSION_BACKDROP = 4,
  DEBUG_OMISSION_OBJECT = 8,
  /* These bits control when to report */
  DEBUG_OMISSION_BEGINPAGE = 16,
  DEBUG_OMISSION_ENDPAGE = 32,
  DEBUG_OMISSION_SUPERBLACK = 64,
  DEBUG_OMISSION_REGISTERMARK = 128,
  DEBUG_OMISSION_ALL = 256,
  DEBUG_OMISSION_MARK = 512,
  DEBUG_OMISSION_ERASE = 1024,
  DEBUG_OMISSION_DEVICECOLOR = 2048,
  DEBUG_OMISSION_BLACKTRANSFER = 4096,
  DEBUG_OMISSION_KNOCKOUTS = 8192
} ;

static int32 debug_omission = 0 ;

static uint8 debug_omission_string[1024] ;

#endif

#if defined(DEBUG_BUILD)

void init_sepomit_debug(void)
{
  register_ripvar(NAME_debug_omit, OINTEGER, &debug_omission);
}

static void debug_omit(char *format, ...)
{
  va_list args ;

  va_start(args, format) ;

  vswcopyf(debug_omission_string + strlen((char *)debug_omission_string),
           (uint8 *)format, args) ;

  va_end(args) ;
}

#define DEBUG_OMIT_CLEAR() MACRO_START \
  debug_omission_string[0] = '\0' ; \
MACRO_END

#define DEBUG_OMIT(flags_, parens_) MACRO_START \
  if ( (debug_omission & (flags_)) != 0 ) \
    debug_omit parens_ ; \
MACRO_END

#else /* ! debugging */

#define DEBUG_OMIT_CLEAR() EMPTY_STATEMENT()
#define DEBUG_OMIT(flags_, parens_) EMPTY_STATEMENT()

#endif

enum { /* Flags for erase colorants */
  OMIT_COLORANT_BG = 1,      /* Background separation */
  OMIT_COLORANT_PROCESS = 2, /* Process separation */
  OMIT_COLORANT_MARKED = 4,  /* Colorant marked for omission */
  OMIT_COLORANT_CLEAR = 8    /* Erase color is clear for this colorant */
} ;

typedef uint16 COLORANT_FLAGS ;

struct DL_OMITDATA {
  DL_STATE *page ;              /* The page */
  GUCR_RASTERSTYLE *hr ;        /* Raster style */
  p_ncolor_t erase_color ;      /* Erase color */
  dlc_tint_t eraseTint ;        /* Erase tint */
  SPOTNO erase_spotno;          /* Erase spotno */
  uint32 nalloc ;               /* Size of erase colorant allocations */
  uint32 ncomps ;               /* number of components in erase colour */
  COLORVALUE *erase_qcvs ;      /* Halftone quantised device codes */
  COLORVALUE *erase_cvs ;       /* Expanded erase color */
  COLORANTINDEX *erase_cis ;    /* Colorant indices */
  COLORANT_FLAGS *erase_flags ; /* Colorant flags */
  uint32 nmapalloc ;            /* Size of colorant index map */
  int32 *ci_map ;               /* Map from colorant index into ci/cv array */
  COLORANTINDEX blackIndex ;    /* Index of black colorant */
  HDL *hdl;                     /* HDL being iterated over */
  Group *group;                 /* the nearest enclosing Group */
  int32 override_colortype;     /* like render_info_t.overrideColorType */
  Bool fulltest;                /* i.e. for renderer, not currentseparationorder */
  Bool negprint ;               /* Rendering negative job? */
  Bool allsep ;                 /* /All separation marked */
  Bool multisep ;               /* Superblack or registermarks */
  Bool othersep ;               /* Non-black separation marked */
  Bool success ;                /* Are we proceeding without error? */
  Bool walked ;                 /* Has DL ever been walked? */
  Bool superblacks ;            /* Ignoring superblacks? */
  Bool registermarks ;          /* Ignoring registermarks? */
  Bool knockouts ;              /* Ignoring knockouts? */
  Bool imagecontents ;          /* Not ignoring image contents? */
  Bool beginpage ;              /* Ignoring beginpage? */
  Bool endpage ;                /* Ignoring endpage? */
  Bool otf_possible;            /* SystemParams.TransparencyStrategy == 1? */
  Bool convert_otf;             /* Converting colors on the fly? */
} ;

/* Unpack the relevant raster style details into a DL_OMITDATA structure. */
void omit_unpack_rasterstyle(const DL_STATE *page)
{
  DL_OMITDATA *omit_data = page->omit_data;
  GUCR_RASTERSTYLE *rasterStyle = page->hr;
  const OMIT_DETAILS *pOmitDetails ;

  HQASSERT(page->omit_data != NULL, "omitdata must exist");
  HQASSERT(rasterStyle != NULL, "No rasterstyle to unpack for omission") ;

  if ( rasterStyle != omit_data->hr ) {
    if ( omit_data->hr != NULL )
      guc_discardRasterStyle(&omit_data->hr) ;

    guc_reserveRasterStyle(rasterStyle) ;
    omit_data->hr = rasterStyle ;
  }

  omit_data->blackIndex = guc_getBlackColorantIndex(rasterStyle) ;
  HQASSERT(omit_data->blackIndex != COLORANTINDEX_UNKNOWN,
           "guc_findBlackColorantIndex not setup by gs_applyEraseColor") ;

  pOmitDetails = guc_omitDetails(rasterStyle) ;
  HQASSERT(pOmitDetails, "No omission details in raster style") ;

  omit_data->superblacks = pOmitDetails->superblacks ;
  omit_data->registermarks = pOmitDetails->registermarks ;
  omit_data->knockouts = pOmitDetails->knockouts ;
  omit_data->imagecontents = pOmitDetails->imagecontents ;
  omit_data->beginpage = pOmitDetails->beginpage ;
  omit_data->endpage = pOmitDetails->endpage ;
  omit_data->erase_color = NULL;
}

/* Unpack an the erase colour (either a pseudo-erase, or a real erase colour)
   into the erase colour workspace. We store the erase colorant indices, the
   device independent color values and the halftone quantised color values */
static Bool omit_unpack_erase(DL_OMITDATA *omit_data, LISTOBJECT *erase)
{
  HQASSERT(omit_data->hr, "Rasterstyle not unpacked before erase colour") ;

  if ( erase->p_ncolor != omit_data->erase_color ) {
    GUCR_CHANNEL* hf ;
    uint32 nalloc, i ;
    dl_color_t dlc_erase ;

    /* Count the colorants to find out how much space to allocate. This only
       returns the fully-fledged colorants. DL colors may also contain colors
       which do not appear in this list, because the operator
       removefromseparationcolornames was used. When we iterate DL colours we
       have to check for indices beyond the end of our mapping array, and
       holes in the mapping array. Backdrop colours should already have
       non-existent colorants removed (which is why we only test
       fully-fledged colorants, otherwise we could not determine
       registermarks). */
    gucr_colorantCount(omit_data->hr, &nalloc) ;

    omit_data->ncomps = 0 ;
    omit_data->erase_color = NULL ;

    if ( nalloc > omit_data->nalloc ) {
      void *workspace ;

      if ( omit_data->nalloc > 0 ) {
        mm_free(mm_pool_temp, (mm_addr_t)omit_data->erase_qcvs,
                omit_data->nalloc * (sizeof(COLORVALUE) +         /* erase_qcvs */
                                     sizeof(COLORVALUE) +         /* erase_cis */
                                     sizeof(COLORANTINDEX) +      /* erase_cvs */
                                     sizeof(COLORANT_FLAGS))) ;   /* erase_flags */
        omit_data->nalloc = 0 ;
        omit_data->erase_qcvs = NULL ;
        omit_data->erase_cvs = NULL ;
        omit_data->erase_cis = NULL ;
        omit_data->erase_flags = NULL ;
      }

      workspace = mm_alloc(mm_pool_temp,
                           nalloc * (sizeof(COLORVALUE) +         /* erase_qcvs */
                                     sizeof(COLORVALUE) +         /* erase_cis */
                                     sizeof(COLORANTINDEX) +      /* erase_cvs */
                                     sizeof(COLORANT_FLAGS)),     /* erase_flags */
                           MM_ALLOC_CLASS_SEP_OMIT) ;
      if ( workspace == NULL )
        return error_handler(VMERROR) ;

      omit_data->erase_qcvs = workspace ;
      omit_data->erase_cis = (COLORANTINDEX *)(omit_data->erase_qcvs + nalloc);
      /* The two above are 4-byte aligned, the rest 2-byte. */
      omit_data->erase_cvs = (COLORVALUE *)(omit_data->erase_cis + nalloc);
      omit_data->erase_flags = (COLORANT_FLAGS *)(omit_data->erase_cvs + nalloc) ;
      omit_data->nalloc = nalloc ;
    }

    /* Now we've got space for the colorant indices, get them, and find out
       how many there really were excluding duplicates. */
    gucr_colorantIndices(omit_data->hr, omit_data->erase_cis,
                         &omit_data->ncomps) ;

    HQASSERT(omit_data->ncomps > 0, "No colorants in output rasterstyle") ;
    HQASSERT(omit_data->erase_cis[0] >= 0,
             "Special colorants fully-fledged in output rastersyle") ;

    /* Build a map from colorant index to CV index. The colorant indices are
       returned sorted, so we can determine the size of the map
       immediately. Note that the allocation is one greater than the last
       colorant index, to include it in the map. */
    nalloc = omit_data->erase_cis[omit_data->ncomps - 1] + 1 ;
    if ( nalloc > omit_data->nmapalloc ) {
      void *workspace ;

      if ( omit_data->nmapalloc > 0 ) {
        mm_free(mm_pool_temp, (mm_addr_t)omit_data->ci_map,
                omit_data->nmapalloc * sizeof(int32)) ;
        omit_data->nmapalloc = 0 ;
        omit_data->ci_map = NULL ;
      }

      workspace = mm_alloc(mm_pool_temp, nalloc * sizeof(int32),
                           MM_ALLOC_CLASS_SEP_OMIT) ;
      if ( workspace == NULL )
        return error_handler(VMERROR) ;

      omit_data->ci_map = workspace ;
      omit_data->nmapalloc = nalloc ;
    }

    /* Initialise to no mapping */
    HqMemSet32((uint32 *)(omit_data->ci_map), (uint32)(-1),
               omit_data->nmapalloc) ;

    /* Build the map */
    for ( i = 0 ; i < omit_data->ncomps ; ++i )
      omit_data->ci_map[omit_data->erase_cis[i]] = i ;

    /* Now expand erase colour from argument into multiple separations */

    dlc_from_lobj_weak(erase, &dlc_erase);
    /* Pseudo-erases have not been preconverted in otf; also, this can
       be called through currentseparations, before preconversion */
    if ( (erase->marker & MARKER_DEVICECOLOR) == 0 ) {
      int32 colorType = (omit_data->override_colortype != GSC_UNDEFINED
                         ? omit_data->override_colortype
                         : DISPOSITION_COLORTYPE(erase->disposition));

      if ( !preconvert_on_the_fly(omit_data->group,
                                  erase, colorType, &dlc_erase,
                                  dlc_currentcolor(omit_data->page->dlc_context)) )
        return FALSE;
      dlc_copy_weak(&dlc_erase, dlc_currentcolor(omit_data->page->dlc_context));
      /* NB: Must do NOTHING to dlc_currentcolor until done with dlc_erase */
    }

    switch ( dlc_check_black_white(&dlc_erase) ) {
      dlc_iter_result_t ires ;
      dl_color_iter_t iter ;
      COLORANTINDEX ci ;
      COLORVALUE cv, maxcv, mincv, allcv ;
    case DLC_TINT_BLACK:
      HQASSERT(sizeof(int16) == sizeof(COLORVALUE),
               "HqMemSet16 not appropriate to set COLORVALUEs") ;
      HqMemSet16((uint16 *)omit_data->erase_cvs, 0, (uint32)omit_data->ncomps) ;
      omit_data->eraseTint = DLC_TINT_BLACK ;
      omit_data->negprint = TRUE ;
      break ;
    case DLC_TINT_WHITE: ;
      HQASSERT(sizeof(int16) == sizeof(COLORVALUE),
               "HqMemSet16 not appropriate to set COLORVALUEs") ;
      HqMemSet16((uint16 *)omit_data->erase_cvs, (uint16)COLORVALUE_MAX,
                 (uint32)omit_data->ncomps) ;
      omit_data->eraseTint = DLC_TINT_WHITE ;
      omit_data->negprint = FALSE ;
      break ;
    default:
      HQFAIL("Unknown return value from dlc_check_black_white()");
      /* FALLTHROUGH */
    case DLC_TINT_OTHER:
      omit_data->eraseTint = DLC_TINT_OTHER ;
      omit_data->negprint = TRUE ;

      /* We determine a -ve print if _all_ the erasepage comes out as
         -ve. Iterate over colorants, checking if color is closest to solid
         or clear. */
      ires = dlc_first_colorant(&dlc_erase, &iter, &ci, &cv) ;
      HQASSERT(ires == DLC_ITER_COLORANT || ires == DLC_ITER_ALLSEP,
               "Expected colorant or /All in erase colour") ;

      maxcv = mincv = cv ;

      /* The erase color may not cover the entire set of colorants. In this
         case, render_erase_of_band looks at the /All colorant (or the first
         colorant which does exist, if there is no /All), and chooses white
         or black depending on whether it is greater or less than half. */
      if ( !dlc_get_indexed_colorant(&dlc_erase, COLORANTINDEX_ALL, &allcv) ) {
        allcv = cv ;
      } else {
        HQASSERT(allcv == cv,
                 "/All separation has different value to expanded colorant") ;
      }

      if ( allcv >= COLORVALUE_HALF ) {
        omit_data->negprint = FALSE ;
        allcv = COLORVALUE_MAX ;
      } else {
        allcv = 0 ;
      }

      /* Scan the DL colorants. If the DL colour contains colorants not
         included in the fully-fledged set, ignore them. Note that we will
         consider them for negative print (so that we get consistent
         behaviour when omitting a single separation), but we don't change
         the cv limits, because those will be used to determine if the
         rendered colorants can be treated as all black/all white. The
         scanning loop will terminate when both the DL color iterator and the
         erase colorants run out. This must happen because each branch either
         increments the erase color index or steps the DL colour iterator. */
      for ( i = 0 ;; ) {
        if ( ires == DLC_ITER_COLORANT ) {
          if ( i < omit_data->ncomps ) {
            if ( ci == omit_data->erase_cis[i] ) {
              /* DL colorant matches erase color */
              if ( cv > maxcv )
                maxcv = cv ;
              if ( cv < mincv )
                mincv = cv ;

              omit_data->erase_cvs[i++] = cv ;
            } else if ( ci > omit_data->erase_cis[i] ) {
              /* Erase color has colorants that DL color doesn't */
              omit_data->erase_cvs[i++] = allcv ;
              continue ;
            } /* else DL color has colorants that erase color doesn't */
          } /* else have DL colorant, but no erase colors left */

          /* Consider colorants for negative print even if not fully-fledged. */
          if ( cv >= COLORVALUE_HALF )
            omit_data->negprint = FALSE ;

          ires = dlc_next_colorant(&dlc_erase, &iter, &ci, &cv) ;
        } else if ( i < omit_data->ncomps ) {
          /* DL colour finished, but erase colorants left */
          if ( allcv > maxcv )
            maxcv = allcv ;
          if ( allcv < mincv )
            mincv = allcv ;
          omit_data->erase_cvs[i++] = allcv ;
        } else {
          /* Neither DL colorant nor erase colorant left, we're done. */
          break ;
        }
      }

      HQASSERT(ires == DLC_ITER_ALLSEP || ires == DLC_ITER_NOMORE,
               "Unexpected final return from DL color iterator") ;

      /* If all channels were B/W, mark it B/W anyway */
      if ( maxcv == 0 )
        omit_data->eraseTint = DLC_TINT_BLACK ;
      else if ( mincv == COLORVALUE_MAX )
        omit_data->eraseTint = DLC_TINT_WHITE ;

      break ;
    }

    /* Quantise colourvalues for halftone/contone levels */
    ht_applyTransform(erase->objectstate->spotno, REPRO_TYPE_OTHER,
                      omit_data->ncomps,
                      omit_data->erase_cis,
                      omit_data->erase_cvs,
                      omit_data->erase_qcvs,
                      gucr_halftoning(omit_data->hr),
                      gucr_valuesPerComponent(omit_data->hr));
    omit_data->erase_spotno = erase->objectstate->spotno;

    /* Fill in the colorant flags */
    HQASSERT(sizeof(int16) == sizeof(COLORANT_FLAGS),
             "HqMemSet16 not appropriate to set COLORANT_FLAGs") ;
    HqMemSet16((uint16 *)omit_data->erase_flags, 0, (uint32)omit_data->ncomps) ;

    /* Set up the background and process flags for the fully-fledged
       colorants. */
    for ( hf = gucr_framesStart(omit_data->hr) ;
          gucr_framesMore(hf) ;
          gucr_framesNext(&hf) ) {
      GUCR_COLORANT* hc ;

      for ( hc = gucr_colorantsStart(hf) ;
            gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED) ;
            gucr_colorantsNext(&hc) ) {
        const GUCR_COLORANT_INFO *colorantInfo;

        if ( gucr_colorantDescription(hc, &colorantInfo) ) {
          COLORANTINDEX ci = colorantInfo->colorantIndex ;
          int32 index ;

          HQASSERT(ci >= 0 && (uint32)ci < omit_data->nmapalloc,
                   "Colorant index out of range") ;
          index = omit_data->ci_map[ci] ;
          HQASSERT(index >= 0 && (uint32)index < omit_data->ncomps,
                   "Colorant mapping out of range") ;
          HQASSERT(omit_data->erase_cis[index] == ci,
                   "Erase colorant index map incorrect") ;
          HQASSERT(omit_data->erase_cvs[index] != COLORVALUE_INVALID,
                   "Erase colorant not initialised") ;

          /* Is the colorant a background separation? */
          if ( colorantInfo->fBackground )
            omit_data->erase_flags[index] |= OMIT_COLORANT_BG ;

          /* Is the colorant a process separation? */
          if ( colorantInfo->colorantType == COLORANTTYPE_PROCESS )
            omit_data->erase_flags[index] |= OMIT_COLORANT_PROCESS ;

          /* Is the erase color clear for this colorant? (Solid is an easy
             test against zero, so is not stored.) */
          if ( omit_data->erase_qcvs[index]
               >= ht_getClear(erase->objectstate->spotno, REPRO_TYPE_OTHER, ci,
                              omit_data->hr) )
            omit_data->erase_flags[index] |= OMIT_COLORANT_CLEAR ;
        }
      }
    }

    omit_data->erase_color = erase->p_ncolor;
  }
  return TRUE ;
}

/* Helper function to test a quantised colour value against the erase colour.
   Function returns a true value if the colorant should be marked. */
static Bool mark_quantised_color(DL_OMITDATA *omit_data, uint32 index,
                                 COLORVALUE cv, SPOTNO spotno, HTTYPE type)
{
  /* Check if the quantised colorvalue is the same as the quantised erase
     colour. The conditions here are a little tricky, but in essence they
     will mark the colorant flags as used if omitting any component would
     change the colour, and mark the whole colour as needed if any colorant
     is different to the erase colour. This is essentially an "all or
     nothing" condition. If a single channel were omitted because it matches
     the erase colour, and the erase colour is not blank, then the overall
     colour would change. */
  COLORVALUE qcv ;
  Bool marked = FALSE ;
  COLORANTINDEX ci ;

  HQASSERT(omit_data, "No omission data") ;
  HQASSERT(index < omit_data->ncomps, "Erase colour index out of range") ;

  ci = omit_data->erase_cis[index] ;
  ht_applyTransform(spotno, type, 1, &ci, &cv, &qcv,
                    gucr_halftoning(omit_data->hr),
                    gucr_valuesPerComponent(omit_data->hr));
  if ( qcv <= 0 ) {
    /* Colorant is solid, it marks if the erase color is not solid. */
    if ( omit_data->erase_qcvs[index] > 0 ) {
      omit_data->erase_flags[index] |= OMIT_COLORANT_MARKED ;
      marked = TRUE ;
    } else if ( !omit_data->negprint ) {
      /* If not doing a negative print, this indicates colorant is present,
         so if the colour as a whole is retained, this colorant is needed. */
      omit_data->erase_flags[index] |= OMIT_COLORANT_MARKED ;
    }
  } else if ( qcv >= ht_getClear(spotno, type, ci, omit_data->hr) ) {
    /* Colorant is clear, it marks if the erase color is not clear. */
    if ( (omit_data->erase_flags[index] & OMIT_COLORANT_CLEAR) == 0 ) {
      omit_data->erase_flags[index] |= OMIT_COLORANT_MARKED ;
      marked = TRUE ;
    } else if ( omit_data->negprint ) {
      /* If doing a negative print, this indicates colorant is present, so if
         the colour is retained as a whole, this colorant is needed. */
      omit_data->erase_flags[index] |= OMIT_COLORANT_MARKED ;
    }
  } else {
    /* Colorant is an intermediate value. It marks if the screens are
       different, or the quantised colorant values are different. The screen
       test allows watermarking with different screens to work, and prevents
       mismatches in the numbers of halftone levels from falsely omitting
       separations. */
    if ( qcv != omit_data->erase_qcvs[index] ||
         (spotno != omit_data->erase_spotno &&
          ht_checkequivalentchspotids(spotno, omit_data->erase_spotno,
                                      FALSE, type, ci)
          < 0) )
      marked = TRUE ;

    /* If the colour is retained, this colorant is needed */
    omit_data->erase_flags[index] |= OMIT_COLORANT_MARKED ;
  }

  DEBUG_OMIT(DEBUG_OMISSION_DLCOLOR, ("%d(%d)%c ", ci, cv, marked ? 'M' : 'E')) ;

  return marked ;
}


/* Test a DL colour to classify it as a registermark/superblack/multi/other
   separation, and mark the affected separations in the rasterstyle. A FALSE
   return value is not a failure, it indicates an early return. */
static Bool omit_dl_color(DL_OMITDATA *omit_data,
                          dl_color_t *dlc, LISTOBJECT *lobj)
{
  Bool superblack = TRUE, registermark = TRUE, other = FALSE, marked = FALSE ;
  COLORVALUE allcv ;
  uint32 index ;
  SPOTNO spotno;
  HTTYPE httype;

  HQASSERT(omit_data->erase_color,
           "Erase colour has not been unpacked") ;

  spotno = lobj->objectstate->spotno;
  httype = DISPOSITION_REPRO_TYPE(lobj->disposition);

  if ( omit_data->convert_otf ) {
    int32 colorType = (omit_data->override_colortype != GSC_UNDEFINED
                       ? omit_data->override_colortype
                       : DISPOSITION_COLORTYPE(lobj->disposition));

    if ( !preconvert_on_the_fly(omit_data->group, lobj, colorType, dlc,
                                dlc_currentcolor(omit_data->page->dlc_context)) ) {
      omit_data->success = FALSE;
      return FALSE;
    }
    dlc_copy_weak(dlc, dlc_currentcolor(omit_data->page->dlc_context));
    /* NB: Must do NOTHING to dlc_currentcolor until done with dlc */
  }

  switch ( dlc_check_black_white(dlc) ) {
    dlc_iter_result_t ires ;
    dl_color_iter_t iter ;
    COLORANTINDEX ci ;
    COLORVALUE cv, maxcv, mincv, blackcv ;
  case DLC_TINT_BLACK:
    /* Ignore object if drawn in background color. Don't need to test
       background against All etc, because All black/white is normalised in
       the erase colour code anyway */
    if ( omit_data->eraseTint == DLC_TINT_BLACK ) {
      HQTRACE((debug_omission & DEBUG_OMISSION_ERASE) != 0,
              ("%s ignored (erase black)", debug_omission_string)) ;
      return TRUE;
    }

    /* If there is no black, this cannot be a registermark or superblack */
    if ( omit_data->blackIndex == COLORANTINDEX_NONE ) {
      registermark = FALSE ;
      superblack = FALSE ;
    }

    /* It could be a superblack if we're not looking for registermarks */
    allcv = 0 ;
    break ;
  case DLC_TINT_WHITE:
    /* Ignore object if drawn in background color */
    if ( omit_data->eraseTint == DLC_TINT_WHITE ) {
      HQTRACE((debug_omission & DEBUG_OMISSION_ERASE) != 0,
              ("%s ignored (erase white)", debug_omission_string)) ;
      return TRUE;
    }

    /* If there is no black, this cannot be a registermark or superblack */
    if ( omit_data->blackIndex == COLORANTINDEX_NONE ) {
      registermark = FALSE ;
      superblack = FALSE ;
    }

    /* It could be a (negative print) superblack if we're not looking for
       registermarks */
    allcv = COLORVALUE_MAX ;
    break ;
  default:
    HQFAIL("Unknown return value from dlc_check_black_white()");
    /* FALLTHROUGH */
  case DLC_TINT_OTHER:
    DEBUG_OMIT(DEBUG_OMISSION_DLCOLOR, (" [ ")) ;

    blackcv = COLORVALUE_INVALID ;

    /* Colorant indices arrive from the DL color iterators in order. We
       walk the entire colorant array in order, so that we can detect process
       colorants that were not present in the entire colorant set (this defines
       registermarks). */
    ires = dlc_first_colorant(dlc, &iter, &ci, &cv) ;
    HQASSERT(ires == DLC_ITER_COLORANT || ires == DLC_ITER_ALLSEP,
             "Expected colorant or /All in DL colour") ;

    maxcv = mincv = cv ;

    if ( !dlc_get_indexed_colorant(dlc, COLORANTINDEX_ALL, &allcv) )
      allcv = COLORVALUE_INVALID ;

   /* Scan the DL colorants. If the DL colour contains colorants not
       included in the fully-fledged set, check them to see if they whether
       they make up a registermark or superblack. The scanning loop will
       terminate when both the DL color iterator and the erase colorants run
       out. This must happen because each branch either increments the erase
       color index or steps the DL colour iterator. */
    for ( index = 0 ; ; ) {
      if ( ires == DLC_ITER_COLORANT ) {
        if ( index < omit_data->ncomps ) {
          if ( ci == omit_data->erase_cis[index] ) {
            /* The colorant exists in both the DL and the erase color. */
            omit_data->erase_flags[index] &= ~OMIT_COLORANT_MARKED ;

            if ( (omit_data->erase_flags[index] & OMIT_COLORANT_BG) == 0 ) {
              if ( mark_quantised_color(omit_data, index, cv, spotno, httype) ) {
                marked = TRUE ;

                /* If this is not the black separation, but is marked, then
                   note that other seps are used in the job to prevent black
                   transfer. */
                if ( ci != omit_data->blackIndex )
                  other = TRUE ;
              }
            } else {
              /* Background separation appears if other colours are marked,
                 but does not force marking itself. This probably cannot
                 happen. Background separations should probably not be
                 omitted. */
              omit_data->erase_flags[index] |= OMIT_COLORANT_MARKED ;
              DEBUG_OMIT(DEBUG_OMISSION_DLCOLOR, ("%d(%d)B ", ci, cv)) ;
            }

            ++index ; /* Iterate both erase color and DL color (latter below) */
          } else if ( ci > omit_data->erase_cis[index] ) {
            /* Erase color has colorants that DL color doesn't. If the DL
               color has an /All color, use that for halftone quantised
               checking. */
            omit_data->erase_flags[index] &= ~OMIT_COLORANT_MARKED ;

            if ( allcv != COLORVALUE_INVALID ) {
              /* Check if quantised /All colorant is same as erase color. */
              if ( mark_quantised_color(omit_data, index, allcv, spotno, httype) )
                marked = TRUE ;

              /* Take a copy of the black colour value, for testing
                 superblacks.  */
              if ( omit_data->erase_cis[index] == omit_data->blackIndex )
                blackcv = allcv ;
            } else if ( (omit_data->erase_flags[index] & OMIT_COLORANT_PROCESS) != 0 ) {
              /* If the erase colorant is a process colorant and there is no
                 /All DL colorant, this is not a registermark. */
              registermark = FALSE ;
            }

            ++index ;
            continue ;
          } /* else DL color has colorants that erase color doesn't */
        } /* else have DL colorant, but no erase colors left */

        /* Consider all DL colorants when determining registermark/superblack */
        if ( cv > maxcv )
          maxcv = cv ;
        if ( cv < mincv )
          mincv = cv ;

        /* Take a copy of the black colour value, for testing superblacks. */
        if ( ci == omit_data->blackIndex )
          blackcv = cv ;

        ires = dlc_next_colorant(dlc, &iter, &ci, &cv) ;
      } else if ( index < omit_data->ncomps ) {
        /* DL colour finished, but erase colorants left */
        omit_data->erase_flags[index] &= ~OMIT_COLORANT_MARKED ;

        /* If the erase colorant is a process colorant and there is no /All
           DL colorant, this is not a registermark. */
        if ( allcv == COLORVALUE_INVALID &&
             (omit_data->erase_flags[index] & OMIT_COLORANT_PROCESS) != 0 )
          registermark = FALSE ;

        ++index ;
      } else {
        /* Neither DL colorant nor erase colorant left, we're done. */
        break ;
      }
    }

    HQASSERT(ires == DLC_ITER_ALLSEP || ires == DLC_ITER_NOMORE,
             "Unexpected final return from DL color iterator") ;

    DEBUG_OMIT(DEBUG_OMISSION_DLCOLOR, ("]")) ;

    /* If nothing was marked, all the colorvalues are the same as the
       erase colour, and we can ignore this colour. */
    if ( !marked ) {
      HQTRACE((debug_omission & DEBUG_OMISSION_ERASE) != 0,
              ("%s ignored (erase color)", debug_omission_string)) ;
      return TRUE;
    }

    if ( blackcv == COLORVALUE_INVALID ) {
      superblack = FALSE ;
    } else {
      /* Superblacks must have more black than anything else (duh!). A higher
         value indicates less colorant, unless we are doing a negative
         print. */
      if ( omit_data->negprint ? maxcv > blackcv : mincv < blackcv )
        superblack = FALSE ;
    }

    /* Registermarks must either be painted in the All sep, or have the same
       amount of colorant in all channels (the former isn't guaranteed because
       of transfers/calibration) and they must have a black colorant. */
    if ( allcv == COLORVALUE_INVALID && mincv != maxcv )
      registermark = FALSE ;

    break ;
  }

  /* We classify the colour as the most restrictive of registermark,
     superblack, or neither, but not more than one. If we did not want to
     omit registermarks, this object cannot classify as a registermark, but
     we will not prevent it being treated as a superblack. (If we did want to
     omit registermarks, and it was detected as such, then it should not be
     treated as a superblack.) */
  if ( !omit_data->registermarks )
    registermark = FALSE ;
  else if ( registermark )
    superblack = FALSE ;

  if ( !omit_data->superblacks )
    superblack = FALSE ;

  HQASSERT(!registermark || !superblack,
           "Colour should be only one of registermark or superblack") ;

  if ( allcv != COLORVALUE_INVALID ) {
    /* If /All separation is used, and suitable registermarks and superblacks
       ignores are set up, then mark all sep used and return. If registermarks
       and superblacks are not being ignored, then mark all separations for
       rendering and terminate */
    if ( registermark || superblack ) {
      omit_data->allsep = TRUE ;
      HQTRACE(((debug_omission & DEBUG_OMISSION_REGISTERMARK) && registermark) ||
              ((debug_omission & DEBUG_OMISSION_SUPERBLACK) && superblack),
              ("%s ignored (RegisterMark/SuperBlack All)", debug_omission_string)) ;
      return TRUE ;
    }

    /* Not ignoring registermarks/superblacks, so render all seps */
    (void)guc_dontOmitSeparation(omit_data->hr, COLORANTINDEX_ALL) ;
    omit_data->othersep = TRUE ;
    HQTRACE((debug_omission & DEBUG_OMISSION_ALL) != 0,
            ("%s /All separations marked", debug_omission_string)) ;
    return FALSE ;
  }

  /* Is this an ignored superblack or a registermark without an /All
     separation? */
  if ( registermark || superblack ) {
    omit_data->multisep = TRUE ;
    HQTRACE(((debug_omission & DEBUG_OMISSION_REGISTERMARK) && registermark) ||
            ((debug_omission & DEBUG_OMISSION_SUPERBLACK) && superblack),
            ("%s ignored (RegisterMark/SuperBlack)", debug_omission_string)) ;
  } else {
    /* Note that something other than /Black, superblack, and registermarks
       was used, so we cannot do black transfer. */
    if ( other )
      omit_data->othersep = TRUE ;

    /* Now reset the omit marks on the separations marked by this object */
    for ( index = 0 ; index < omit_data->ncomps ; ++index ) {
      if ( (omit_data->erase_flags[index] & OMIT_COLORANT_MARKED) != 0 ) {
        if ( !guc_dontOmitSeparation(omit_data->hr,
                                     omit_data->erase_cis[index]) ) {
          HQTRACE((debug_omission & DEBUG_OMISSION_ALL) != 0,
                  ("%s complete (all separations marked)", debug_omission_string)) ;
          return FALSE ; /* Done them all, no point in continuing */
        }

        HQTRACE((debug_omission & DEBUG_OMISSION_MARK) != 0,
                ("%s marked %d", debug_omission_string,
                 omit_data->erase_cis[index])) ;
      }
    }
  }
  return TRUE;
}

/* Iterator function for gouraud DL colours. All colours are traversed, so
   gourauds can theoretically contain superblacks or registermarks. */
typedef struct {
  DL_OMITDATA *omit_data ;
  LISTOBJECT *lobj;
} omit_gouraud_t ;

static Bool omit_iterate_gouraud(p_ncolor_t *color, void *callbackdata)
{
  omit_gouraud_t *omit_gouraud_data = callbackdata ;
  dl_color_t dlc ;

  HQASSERT(omit_gouraud_data, "No data for gouraud omission") ;

  dlc_from_dl_weak(*color, &dlc) ;

  return omit_dl_color(omit_gouraud_data->omit_data, &dlc,
                       omit_gouraud_data->lobj);
}


/* Prepare for backdrop separation omission. */
Bool omit_backdrop_prepare(DL_STATE *page)
{
  DL_OMITDATA *omit_data = page->omit_data;
  DLREF *dl ;
  LISTOBJECT *erase ;

  omit_data->convert_otf = FALSE;

  /* Unpack the erase colour from the display list */
  dl = dl_get_orderlist(page) ;
  HQASSERT(dl, "No objects on display list") ;

  erase = dlref_lobj(dl);
  HQASSERT(erase && erase->opcode == RENDER_erase,
           "No erase link on display list") ;
  HQASSERT((erase->marker & MARKER_DEVICECOLOR) != 0,
           "Erase object has not been converted to device color") ;

  if ( !omit_unpack_erase(omit_data, erase) )
    return FALSE ;

  /* Mark that we've looked at the colorants */
  omit_data->walked = TRUE ;

  return TRUE ;
}

/* Set backdrop flags for omission before quantising colours. This is similar
   to omit_dl_color, except it only sets flags, and does not perform
   quantisation itself. */
Bool omit_backdrop_color(DL_OMITDATA *omit_data,
                         int32 nComps,
                         const COLORANTINDEX *cis,
                         const COLORVALUE *cvs,
                         const COLORINFO *info,
                         Bool *maybe_marked)
{
  uint32 index ;
  Bool superblack = TRUE, registermark = TRUE ;
  Bool all = TRUE ;
  COLORVALUE firstcv, cv, mincv, maxcv, blackcv ;
  COLORANTINDEX ci ;

  HQASSERT(nComps > 0, "Should have at least one colorant in backdrop color") ;
  HQASSERT(omit_data->erase_color,
           "Erase colour has not been unpacked") ;

  DEBUG_OMIT_CLEAR();

  *maybe_marked = FALSE ;

  /* No way to determine BeginPage/EndPage yet */
  if ( omit_data->beginpage &&
       (info->spflags & RENDER_BEGINPAGE) != 0 ) {
    HQTRACE((debug_omission & DEBUG_OMISSION_BEGINPAGE) != 0,
            ("%s ignored (BeginPage)", debug_omission_string)) ;
    return TRUE ;
  }

  if ( omit_data->endpage &&
       (info->spflags & RENDER_ENDPAGE) != 0 ) {
    HQTRACE((debug_omission & DEBUG_OMISSION_ENDPAGE) != 0,
            ("%s ignored (EndPage)", debug_omission_string)) ;
    return TRUE ;
  }

  /* By the time we see the backdrop data, colours have been expanded to the
     number of channels in the backdrop, and composited against the page
     colour. We have no way of knowing which colours were originally in the
     object, so we will assume that any channel that exactly matches the erase
     colour was not in the object. The existing definitions of superblacks and
     erase colours are used after removal of these channels. */
  maxcv = 0 ;
  mincv = COLORVALUE_MAX ;
  blackcv = COLORVALUE_INVALID ;

  /* Get initial color from argument array. We assume colorant indices are
     sorted, and traverse them as they are encountered in the output
     colorants. */
  ci = *cis++ ;
  cv = *cvs++ ;
  firstcv = cv ;
  /* nComps is the number of backdrop colorants left to process. */

  DEBUG_OMIT(DEBUG_OMISSION_BACKDROP,
             (" %s%s %02x [ ",
              (info->spflags & RENDER_BEGINPAGE) ? "B" : "",
              (info->spflags & RENDER_ENDPAGE) ? "E" : "",
              info->reproType)) ;

  for ( index = 0 ; ; ) {
    if ( nComps > 0 ) {
      /* All colorants need to be the same, regardless of background or erase
         status, to make it an all colorant. */
      if ( cv != firstcv )
        all = FALSE ;

      if ( index < omit_data->ncomps ) {
        if ( ci == omit_data->erase_cis[index] ) {
          /* The colorant exists in both the backdrop and the erase color */
          omit_data->erase_flags[index] &= ~OMIT_COLORANT_MARKED ;

          if ( cv != omit_data->erase_cvs[index] ) {
            if ( ci == omit_data->blackIndex )
              blackcv = cv ;

            if ( (omit_data->erase_flags[index] & OMIT_COLORANT_BG) == 0 ) {
              Bool marked = FALSE ;

              if ( cv == 0 ) {
                if ( !omit_data->negprint )
                  marked = TRUE ;
              } else if ( cv == COLORVALUE_MAX ) {
                if ( omit_data->negprint )
                  marked = TRUE ;
              } else {
                marked = TRUE ;
              }

              if ( marked ) {
                *maybe_marked = TRUE ;
                omit_data->erase_flags[index] |= OMIT_COLORANT_MARKED ;
                DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)? ", ci, cv)) ;
              } else {
                DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)E ", ci, cv)) ;
              }

              /* We would really like to see the quantised colourvalues before
                 deciding if any other seps are really marked. */
            } else {
              /* Background separation appears if other colours are marked */
              omit_data->erase_flags[index] |= OMIT_COLORANT_MARKED ;
              DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)B ", ci, cv)) ;
            }

            ++index ; /* Iterate both erase color and backdrop color (latter below) */
          } else {
            /* Colorant matches erase colour exactly. Assume it came from the
               erase colour and not the object. */
            if ( (omit_data->erase_flags[index] & OMIT_COLORANT_PROCESS) != 0 )
              registermark = FALSE ;

            DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)E ", ci, cv)) ;

            /* Dealt with this colour, get the next one (but don't read off
               the end of the arrays). We can't just fallthrough to the
               increment like omit_dl_color does, because we would set the
               superblack/registermark detection incorrectly. */
            if ( --nComps > 0 ) {
              HQASSERT(cis[0] >= ci, "Backdrop colorant indices are not sorted") ;
              ci = *cis++ ;
              cv = *cvs++ ;
            }

            ++index ; /* Done with this erase color too */
            continue ;
          }
        } else if ( ci > omit_data->erase_cis[index] ) {
          /* Erase color has colorants that backdrop color doesn't. */
          omit_data->erase_flags[index] &= ~OMIT_COLORANT_MARKED ;

          /* Cannot be all, a separation was not in the backdrop color. */
          all = FALSE ;

          if ( (omit_data->erase_flags[index] & OMIT_COLORANT_PROCESS) != 0 ) {
            /* If the erase colorant is a process colorant, this is not a
               registermark. */
            registermark = FALSE ;
          }

          ++index ;
          continue ;
        } /* else backdrop colorant has colorants that erase color doesn't */
      } /* else have backdrop colorant, but no erase colors left */

      /* Count all used colorants in superblack/registermark definition. */
      if ( cv > maxcv )
        maxcv = cv ;
      if ( cv < mincv )
        mincv = cv ;

      /* Dealt with this colour, get the next one (but don't read off the end
         of the arrays) */
      if ( --nComps > 0 ) {
        HQASSERT(cis[0] >= ci, "Backdrop colorant indices are not sorted") ;
        ci = *cis++ ;
        cv = *cvs++ ;
      }
    } else if ( index < omit_data->ncomps ) {
      /* The colorant is not in the backdrop color. If this colorant is a
         process colour, this is not a registermark (unless it has an all
         separation, in which case it should have expanded to all
         colorants). */
      all = FALSE ;

      if ( (omit_data->erase_flags[index] & OMIT_COLORANT_PROCESS) != 0 )
        registermark = FALSE ;

      ++index ;
    } else {
      /* Neither DL colorant nor erase colorant left, we're done. */
      break ;
    }
  }

  HQASSERT(nComps == 0, "Backdrop colorant did not appear in erase colour") ;

  if ( !*maybe_marked ) {
    HQTRACE((debug_omission & DEBUG_OMISSION_ERASE) != 0,
            ("%s] ignored (erase color)", debug_omission_string)) ;
    return TRUE ;
  }

  if ( blackcv == COLORVALUE_INVALID ) {
    registermark = FALSE ;
    superblack = FALSE ;
  } else {
    /* Registermarks must have the same amount of colorant in all channels,
       and they must have a black colorant. We classify the colour as the
       most restrictive of registermark, superblack, or neither, but not more
       than one. If we did not want to omit registermarks, this object cannot
       classify as a registermark, but we will not prevent it being treated
       as a superblack. (If we did want to omit registermarks, and it was
       detected as such, then it should not be treated as a superblack.) */
    if ( mincv != maxcv )
      registermark = FALSE ;
    else if ( !omit_data->registermarks )
      registermark = FALSE ;
    else if ( registermark )
      superblack = FALSE ;

    /* Superblacks must have more black than anything else (duh!). A
       higher value indicates less colorant, unless we are doing a
       negative print. */
    if ( !omit_data->superblacks )
      superblack = FALSE ;
    else if ( omit_data->negprint ? maxcv > blackcv : mincv < blackcv )
      superblack = FALSE ;
  }

  HQASSERT(!registermark || !superblack,
           "Colour should be only one of registermark or superblack") ;

  if ( all ) {
    if ( registermark || superblack ) {
      omit_data->allsep = TRUE ;

      *maybe_marked = FALSE ;

      HQTRACE(((debug_omission & DEBUG_OMISSION_REGISTERMARK) && registermark) ||
              ((debug_omission & DEBUG_OMISSION_SUPERBLACK) && superblack),
              ("%s] ignored (RegisterMark/SuperBlack All)", debug_omission_string)) ;
      return TRUE ;
    }

    /* Not ignoring registermarks/superblacks, so render all seps */
    (void)guc_dontOmitSeparation(omit_data->hr, COLORANTINDEX_ALL) ;
    omit_data->othersep = TRUE ;
    HQTRACE((debug_omission & DEBUG_OMISSION_ALL) != 0,
            ("%s] /All separations marked", debug_omission_string)) ;
    return FALSE ;
  }

  /* Is this an ignored superblack or a registermark without an /All
     separation? */
  if ( registermark || superblack ) {
    omit_data->multisep = TRUE ;

    *maybe_marked = FALSE ;

    HQTRACE(((debug_omission & DEBUG_OMISSION_REGISTERMARK) && registermark) ||
            ((debug_omission & DEBUG_OMISSION_SUPERBLACK) && superblack),
            ("%s] ignored (RegisterMark/SuperBlack)", debug_omission_string)) ;
  }

  /* Leave marked flags for omit_backdrop_devicecolor to act on. */
  return TRUE ;
}

/* Set omission flags for backdrop colorant post quantisation, and transfer
   to rasterstyle. This routine inherits the flags set by
   omit_backdrop_color, and checks whether they should have been set. */
Bool omit_backdrop_devicecolor(DL_OMITDATA *omit_data,
                               int32 nComps,
                               const COLORANTINDEX *cis,
                               const COLORVALUE *cvs,
                               SPOTNO spotno,
                               HTTYPE type)
{
  uint32 index ;
  COLORVALUE qcv ;
  Bool other = FALSE, marked_some = FALSE ;
  COLORANTINDEX ci ;

  HQASSERT(nComps > 0, "Should have at least one colorant in backdrop color") ;
  HQASSERT(omit_data->erase_color,
           "Erase colour has not been unpacked") ;

  /* Get initial color from argument array. We assume colorant indices are
     sorted, and traverse them as they are encoutered in the output
     colorants. These color values are now quantised to device levels */
  ci = *cis++ ;
  qcv = *cvs++ ;
  /* nComps is the number of backdrop colorants left to process. */

  DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, (": ")) ;

  for ( index = 0 ; ; ) {
    if ( nComps > 0 ) {
      if ( index < omit_data->ncomps ) {
        if ( ci == omit_data->erase_cis[index] ) {
          /* Examine the separations that we thought might be marked. We may
             reset the marks if we decide it shouldn't have been marked in
             the first place. */
          if ( (omit_data->erase_flags[index] & OMIT_COLORANT_MARKED) != 0 &&
               (omit_data->erase_flags[index] & OMIT_COLORANT_BG) == 0 ) {
            Bool marked = FALSE ;

            if ( qcv == 0 ) {
              /* Colorant is solid, it marks if the erase color is not
                 solid. */
              if ( omit_data->erase_qcvs[index] > 0 ) {
                marked = TRUE ;
                DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)M ", ci, qcv)) ;
              } else if ( omit_data->negprint ) {
                /* Both color and erase color are solid, colorant is not
                   marked if negative print. */
                omit_data->erase_flags[index] &= ~OMIT_COLORANT_MARKED ;
                DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)N ", ci, qcv)) ;
              }
            } else if ( qcv >= ht_getClear(spotno, type, ci, omit_data->hr) ) {
              /* Colorant is clear, it marks if the erase color is not
                 clear. */
              if ( (omit_data->erase_flags[index] & OMIT_COLORANT_CLEAR) == 0 ) {
                marked = TRUE ;
                DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)M ", ci, qcv)) ;
              } else if ( !omit_data->negprint ) {
                /* Both color and erase color are clear, colorant is not
                   marked if positive print. */
                omit_data->erase_flags[index] &= ~OMIT_COLORANT_MARKED ;
                DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)N ", ci, qcv)) ;
              }
            } else {
              /* Colorant is an intermediate value. It marks if the screens
                 are different, or the quantised colorant values are
                 different. The screen test allows watermarking with
                 different screens to work, and prevents mismatches in the
                 numbers of halftone levels from falsely omitting
                 separations. */
              if ( qcv != omit_data->erase_qcvs[index] ||
                   (spotno != omit_data->erase_spotno &&
                    ht_checkequivalentchspotids(spotno,
                                                omit_data->erase_spotno,
                                                FALSE, REPRO_TYPE_OTHER, ci)
                    < 0) ) {
                marked = TRUE ;
                DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("%d(%d)M ", ci, qcv)) ;
              }
            }

            if ( marked ) {
              marked_some = TRUE ;
              if ( ci != omit_data->blackIndex )
                other = TRUE ;
            }
          }

          ++index ; /* Iterate both erase color and backdrop color (latter below) */
        } else if ( ci > omit_data->erase_cis[index] ) {
          /* Erase color has colorants that backdrop color doesn't. We have
             already checked for registermark stuff. */
          HQASSERT((omit_data->erase_flags[index] & OMIT_COLORANT_MARKED) == 0,
                   "Colorant should not be marked, it is not in backdrop");

          ++index ;
          continue ;
        } /* else backdrop color has colorants that erase color doesn't */
      } /* else have backdrop color, but no erase colors left */

      /* Dealt with this colour, get the next one (but don't read off the end
         of the arrays) */
      if ( --nComps > 0 ) {
        HQASSERT(cis[0] > ci, "Backdrop colorant indices are not sorted") ;
        ci = *cis++ ;
        qcv = *cvs++ ;
      }
    } else if ( index < omit_data->ncomps ) {
      /* Backdrop colour finished, but erase colorants left */
      HQASSERT((omit_data->erase_flags[index] & OMIT_COLORANT_MARKED) == 0,
               "Colorant should not be marked, it is not in backdrop");
      ++index ;
    } else {
      /* Neither backdrop colorant nor erase colorant left, we're done. */
      break ;
    }
  }

  DEBUG_OMIT(DEBUG_OMISSION_BACKDROP, ("]")) ;

  HQASSERT(nComps == 0, "Backdrop colorant did not appear in erase colour") ;

  /* Note that something other than /Black, superblack, and registermarks
     was used, so we cannot do black transfer. */
  if ( other )
    omit_data->othersep = TRUE ;

  /* Traverse separations looking for omission marks, and propagate the
     colorant flag marks to the rasterstyle. Returning FALSE from this routine
     is not a failure, it indicates there are no more colorants to omit. */
  for ( index = 0 ; index < omit_data->ncomps ; ++index ) {
    if ( (omit_data->erase_flags[index] & OMIT_COLORANT_MARKED) != 0 ) {
      if ( !guc_dontOmitSeparation(omit_data->hr, omit_data->erase_cis[index]) ) {
        HQTRACE((debug_omission & DEBUG_OMISSION_ALL) != 0,
                ("%s complete (all separations marked)", debug_omission_string)) ;
        return FALSE ; /* Done them all, no point in continuing */
      }

      HQTRACE((debug_omission & DEBUG_OMISSION_MARK) != 0,
              ("%s marked %d", debug_omission_string,
               omit_data->erase_cis[index])) ;
    }
  }

  return TRUE ;
}


/* Tests for image omission. Images are not considered registermarks or
   superblacks unless in /All separation. */
static Bool dl_omitblankimages(DL_OMITDATA *omit_data, LISTOBJECT *lobj,
                               dl_color_t *dlc)
{
  dlc_iter_result_t ires ;
  dl_color_iter_t iter ;
  COLORANTINDEX ci ;
  COLORVALUE cv ;
  IMAGEOBJECT *theimage ;

  HQASSERT(lobj, "lobj NULL in dl_omitblankimages") ;
  HQASSERT(lobj->opcode == RENDER_image,
           "lobj type should be RENDER_image in dl_omitblankimages") ;
  HQASSERT(omit_data , "dl_omitdata NULL in dl_omitblankimages") ;

  theimage = lobj->dldata.image;
  HQASSERT(theimage, "somehow lost IMAGEOBJECT") ;

  DEBUG_OMIT(DEBUG_OMISSION_IMAGES, (" [ ")) ;

  for ( ires = dlc_first_colorant(dlc, &iter, &ci, &cv) ;
        ires == DLC_ITER_COLORANT ;
        ires = dlc_next_colorant(dlc, &iter, &ci, &cv) ) {
    int32 index ;

    /* If this separation was removed after the image was created, ignore
       the colorant. */
    if ( (uint32)ci < omit_data->nmapalloc &&
         (index = omit_data->ci_map[ci]) >= 0 ) {
      HQASSERT((uint32)index < omit_data->ncomps,
               "Colorant mapping out of range") ;
      HQASSERT(omit_data->erase_cis[index] == ci,
               "Erase colorant index map incorrect") ;

      /* Expensive tests, only do if possibly omitting separation */
      if ( guc_fOmittingSeparation(omit_data->hr, ci) ) {
        int32 colorstate ;
        COLORVALUE qcv ;

        if ( lobj->objectstate->spotno == omit_data->erase_spotno ) {
          qcv = CAST_TO_COLORVALUE(omit_data->erase_qcvs[index]) ;
        } else {
          qcv = ht_getClear(lobj->objectstate->spotno,
                            DISPOSITION_REPRO_TYPE(lobj->disposition),
                            ci, omit_data->hr) ;
        }

        colorstate = im_expandluttest(theimage->ime, ci, qcv) ;
        if ( omit_data->imagecontents &&
             colorstate != imcolor_same &&
             colorstate != imcolor_error )
          colorstate = im_expanddatatest(theimage->ime, theimage->ims, ci, qcv,
                                         &theimage->imsbbox);

        switch ( colorstate ) {
        case imcolor_same: /* All data was the same as clear color. This marks
                              if the erase color was not clear. */
          if ( (omit_data->erase_flags[index] & OMIT_COLORANT_CLEAR) != 0 ) {
            DEBUG_OMIT(DEBUG_OMISSION_IMAGES, ("%d(%d)E ", ci, qcv)) ;
            break ;
          }
          /*@fallthrough@*/
        case imcolor_different:
        case imcolor_unknown: /* Assume marked, since we don't know any better */
          DEBUG_OMIT(DEBUG_OMISSION_IMAGES, ("%d(%d)M ", ci, qcv)) ;

          if ( ci != omit_data->blackIndex )
            omit_data->othersep = TRUE ;

          if ( !guc_dontOmitSeparation(omit_data->hr, ci) ) {
            HQTRACE((debug_omission & DEBUG_OMISSION_IMAGES) != 0,
                    ("%s... ] (marked all seps)", debug_omission_string)) ;
            return FALSE ;

          }
          HQTRACE((debug_omission & DEBUG_OMISSION_MARK) != 0,
                  ("%s marked %d", debug_omission_string, ci)) ;
          break ;
        default:
          HQFAIL("Invalid return type from im_expandluttype/im_expanddatatype") ;
        case imcolor_error:
          /* Bail out! Bail out! */
          omit_data->success = FALSE ;
          return FALSE ;
        }
      } else {
        DEBUG_OMIT(DEBUG_OMISSION_IMAGES, ("%dN ", ci)) ;

        if ( ci != omit_data->blackIndex )
          omit_data->othersep = TRUE ; /* Assume marked if not omitted */
      }
    }
  }
  switch ( ires ) {
  case DLC_ITER_ALLSEP: /* All sep match, assume content exists */
    omit_data->allsep = TRUE ;
    break;
  case DLC_ITER_ALL01: /* dlc_Black or dlc_White, assume content exists */
    HQFAIL("ALL0 or ALL1 separation not expected in image") ;
    break;
  case DLC_ITER_NONE: /* Ignore NONE sep */
    HQFAIL("/None separation not expected in image") ;
    break;
  case DLC_ITER_NOMORE: /* Normal exit */
    break;
  default:
    HQFAIL("Invalid return code from DL color iterators") ;
  }

  return TRUE ;
}

static Bool dl_omitblankimages_singlepass(DL_OMITDATA *omit_data,
                                          LISTOBJECT *lobj,
                                          dl_color_t *dlc)
{
  IMAGEOBJECT *theimage ;
  int32 yindex, rw;
  void *values;
  int expanded_to_plane[BLIT_MAX_CHANNELS];
  unsigned int expanded_comps, converted_comps;
  size_t converted_bits;
  COLORANTINDEX *output_colorants;
  channel_index_t i ;
  COLORVALUE erase[BLIT_MAX_CHANNELS];
  Bool marked[BLIT_MAX_CHANNELS]; /* Track colorants that are found to mark */
  size_t colorants_to_check;
  dlc_context_t *dlc_context = omit_data->page->dlc_context;
  int32 colorType = (omit_data->override_colortype != GSC_UNDEFINED
                     ? omit_data->override_colortype
                     : DISPOSITION_COLORTYPE(lobj->disposition));

  HQASSERT((lobj->marker & MARKER_DEVICECOLOR) == 0, "Image already converted");
  theimage = lobj->dldata.image;
  HQASSERT(theimage, "somehow lost IMAGEOBJECT") ;

  DEBUG_OMIT(DEBUG_OMISSION_IMAGES, (" [ ")) ;

  /* Call preconvert to set up the on-the-fly conversion */
  if ( !preconvert_on_the_fly(omit_data->group, lobj, colorType, dlc, dlc) ) {
    omit_data->success = FALSE;
    return FALSE;
  }

  /* Single-pass might not need to convert on-the-fly if there's an alternate
     LUT that can be used. In that case all we need to do is pass in the
     preconverted dl color to dl_omitblankimages() and the alternate LUT will be
     used automatically. */
  if ( !im_converting_on_the_fly(theimage->ime) ) {
    Bool result = dl_omitblankimages(omit_data, lobj, dlc);
    dlc_release(dlc_context, dlc);
    return result;
  }

  dlc_release(dlc_context, dlc);
  im_extract_blit_mapping(theimage->ime, expanded_to_plane,
                          &expanded_comps, &converted_comps, &converted_bits,
                          &output_colorants);

  /* Now fetch the quantised erase color for all the colorants from the
     image, and init marked[]. */
  colorants_to_check = 0;
  for ( i = 0 ; i < (size_t)converted_comps ; ++ i ) {
    COLORANTINDEX ci = output_colorants[i];
    int32 index;

    if ( ci == COLORANTINDEX_ALL )
      omit_data->allsep = TRUE; /* All sep match, assume content exists */

    if ( (uint32)ci < omit_data->nmapalloc
         && (index = omit_data->ci_map[ci]) >= 0 ) {
      HQASSERT((uint32)index < omit_data->ncomps,
               "Colorant mapping out of range");
      HQASSERT(omit_data->erase_cis[index] == ci,
               "Erase colorant index map incorrect");

      if ( guc_fOmittingSeparation(omit_data->hr, ci) ) {
        if ( omit_data->imagecontents ) {
           erase[i] = omit_data->erase_qcvs[index];
           marked[i] = FALSE; ++colorants_to_check;
        } else { /* Don't examine image, so have to assume it marks */
          DEBUG_OMIT(DEBUG_OMISSION_IMAGES,
                     ("%d(%d)M ",
                      (int32)ci, (int32)omit_data->erase_qcvs[index]));
          if ( ci != omit_data->blackIndex )
            omit_data->othersep = TRUE ;
          if ( !guc_dontOmitSeparation(omit_data->hr, ci) ) {
            HQTRACE((debug_omission & DEBUG_OMISSION_IMAGES) != 0,
                    ("%s... ] (marked all seps)", debug_omission_string));
            return FALSE;
          }
        }
      } else {
        DEBUG_OMIT(DEBUG_OMISSION_IMAGES,
                   ("%d(%d)N ", (int32)ci, (int32)omit_data->erase_qcvs[index]));
        if ( ci != omit_data->blackIndex )
          omit_data->othersep = TRUE ; /* Assume marked if not omitted */
        marked[i] = TRUE;
      }
    } else
      /* If there's no mapping, this separation was removed after the
         image was created */
      marked[i] = TRUE;
  }
  if ( colorants_to_check == 0 )
    return TRUE; /* image didn't contain any colorants that needed checking */

  /* Expand each line of the image (this does the on-the-fly
     conversion), and check that each pixel matches the erase colour. */
  rw = theimage->imsbbox.x2 - theimage->imsbbox.x1 + 1 ;
  for ( yindex = theimage->imsbbox.y1 ; yindex <= theimage->imsbbox.y2 ; ++yindex ) {
    size_t bytes = converted_bits / 8;
    uint8 *pcolor;
    size_t pixels;
    int32 nrows;

    HQASSERT(converted_bits % 8 == 0, "Can't handle image bit depth");

    SwOftenUnsafe();
    values = im_expandread( theimage->ime, theimage->ims, FALSE,
                            theimage->imsbbox.x1, yindex, rw, &nrows,
                            expanded_to_plane, expanded_comps );
    pcolor = (uint8 *)values;
    for ( pixels = rw ; pixels > 0 && colorants_to_check > 0 ; --pixels ) {
      for ( i = 0 ; i < (size_t)converted_comps ; ++ i ) {
        if ( !marked[i] )
          if ( erase[i] != (bytes == 1 ? (COLORVALUE)*pcolor
                                       : *(COLORVALUE*)pcolor) ) {
            COLORANTINDEX ci = output_colorants[i];
            DEBUG_OMIT(DEBUG_OMISSION_IMAGES,
                       ("%d(%d)M ", (int32)ci, (int32)erase[i]));
            if ( ci != omit_data->blackIndex )
              omit_data->othersep = TRUE ;
            if ( !guc_dontOmitSeparation(omit_data->hr, ci) ) {
              HQTRACE((debug_omission & DEBUG_OMISSION_IMAGES) != 0,
                      ("%s... ] (marked all seps)", debug_omission_string));
              return FALSE;
            }
            /* Even if there are still seps to check, they might not
               occur in this image, so track colorants_to_check. */
            if ( --colorants_to_check == 0 )
              return TRUE;
            marked[i] = TRUE;
            HQTRACE((debug_omission & DEBUG_OMISSION_MARK) != 0,
                    ("%s marked %d", debug_omission_string, (int32)ci));
          }
        pcolor += bytes;
      }
    }
  }
  return TRUE;
}

/* The mega main routine to analyze a display list object to see if it
 * contributes any color to the current set of output separations.
 * Returning FALSE from this routine is just a short-circuit of the dl
 * iteration, not a failure.
 */
static Bool dl_omitblankseps(DL_OMITDATA *omit_data, LISTOBJECT *lobj)
{
  dl_color_t dlc;
  uint8 opcode;

  /* If it's a pattern mask, ignore it. */
  if ( (lobj->spflags & RENDER_PATTERN) != 0 )
    return TRUE;

  DEBUG_OMIT_CLEAR() ;

  DEBUG_OMIT(DEBUG_OMISSION_OBJECT,
             ("SO: %s (%p)", debug_opcode_names[lobj->opcode], lobj)) ;

  /* Ignore marks drawn in BeginPage/EndPage. */
  if ( omit_data->beginpage &&
       (lobj->spflags & RENDER_BEGINPAGE) != 0 ) {
    HQTRACE((debug_omission & DEBUG_OMISSION_BEGINPAGE) != 0,
            ("%s ignored (BeginPage)", debug_omission_string)) ;
    return TRUE ;
  }
  if ( omit_data->endpage &&
       (lobj->spflags & RENDER_ENDPAGE) != 0 ) {
    HQTRACE((debug_omission & DEBUG_OMISSION_ENDPAGE) != 0,
            ("%s ignored (EndPage)", debug_omission_string)) ;
    return TRUE ;
  }

  /* Check for non-device colors.  These objects will either be composited or
     color-converted on-the-fly here or in a another pass. */
  if ( (lobj->marker & MARKER_DEVICECOLOR) == 0 && !omit_data->convert_otf ) {
    Bool two_pass = !omit_data->otf_possible;

    if ( omit_data->fulltest && two_pass ) {
      /* Omits for composited objects are updated from the backdrop.  If the
         object isn't fully composited it will preconverted between the 2 passes
         and separation omission will be called again.  In both cases, the
         object can just be ignored now. */
      HQTRACE((debug_omission & DEBUG_OMISSION_DEVICECOLOR) != 0,
              ("%s not a device color", debug_omission_string));
      return TRUE;
    } else {
      /* Compositing and preconversion for 2-pass are only possible when
         omit_blank_separations is being called from the renderer.  If this is
         not the case (e.g. currentseparationorder), separation omission testing
         is incomplete and ignoring the object now may result in false positive
         omits.  Therefore, to be safe, mark all the separations.If we're
         doing single-pass and can't convert the object then also mark seps. */
      (void)guc_dontOmitSeparation(omit_data->hr, COLORANTINDEX_ALL) ;
      omit_data->othersep = TRUE ;
      HQTRACE((debug_omission & DEBUG_OMISSION_DEVICECOLOR) != 0,
              ("%s all separations marked because object cannot be tested",
               debug_omission_string)) ;
      return FALSE;
    }
  }

  /* Normally knockouts are ignored, but if not ignoring, objects with the
     knockout flag set marks all separations, except for groups and hdls where
     it's their contents that matter.  This case is required when knockouts are
     drawn in the ContoneMask value for HVD. */
  if ( !omit_data->knockouts && (lobj->spflags & RENDER_KNOCKOUT) != 0 &&
       (lobj->opcode != RENDER_erase || (lobj->spflags & RENDER_PSEUDOERASE) != 0) &&
       lobj->opcode != RENDER_group && lobj->opcode != RENDER_hdl ) {
    (void)guc_dontOmitSeparation(omit_data->hr, COLORANTINDEX_ALL) ;
    omit_data->othersep = TRUE ;
    HQTRACE((debug_omission & DEBUG_OMISSION_KNOCKOUTS) != 0,
            ("%s] All separations marked for a knockout", debug_omission_string)) ;
    return FALSE ;
  }

  opcode =
    (lobj->spflags & RENDER_PSEUDOERASE) != 0 ? RENDER_erase : lobj->opcode;
  switch ( opcode ) {
  case RENDER_erase: {
    if ( !omit_unpack_erase(omit_data, lobj) ) {
      omit_data->success = FALSE ;
      return FALSE ;
    }
    return TRUE ;
  }
  case RENDER_gouraud: {
    omit_gouraud_t omit_gouraud_data ;

    /* We now iterate all colours in a gouraud object, so gourauds can be
       superblacks, or can have non black/white erase color channels. A FALSE
       return value is not a failure, it is an early exit from the
       iteration. We do not check the amalgamated list object colour, because
       it has artificial values in which may mismatch the erase colour. */
    omit_gouraud_data.omit_data = omit_data ;
    omit_gouraud_data.lobj = lobj;
    return gouraud_iterate_dlcolors(lobj, omit_iterate_gouraud,
                                    &omit_gouraud_data) ;
  }
  case RENDER_vignette: case RENDER_shfill:
  case RENDER_hdl: case RENDER_group:
    return TRUE; /* handled by the HDL iteration */
  case RENDER_image:
    dlc_from_lobj_weak(lobj, &dlc) ;
    return omit_data->convert_otf
      ? dl_omitblankimages_singlepass(omit_data, lobj, &dlc)
      : dl_omitblankimages(omit_data, lobj, &dlc);
  default: {
    dlc_from_lobj_weak(lobj, &dlc) ;
    return omit_dl_color(omit_data, &dlc, lobj);
  }
  }
}


/* The following routine is the anchor point for analyzing a DL to determine
 * if we can omit any separations due to either no marks being on the page,
 * or extraneous marks that we can ignore.
 * It should be the last thing called before rendering.
 */
Bool omit_blank_separations(const DL_STATE *page)
{
  DL_OMITDATA *omit_data = page->omit_data;

  HQASSERT(page != NULL, "Need a page to check for omit_blank_separations");
  HQASSERT(omit_data->page == page, "Page should be set already");
  HQASSERT(omit_data->hr == page->hr, "Rasterstyle should be set already");

  if ( guc_fOmitBlankSeparations(omit_data->hr) && gucr_framesStart(omit_data->hr) ) {
    Bool otf = omit_data->otf_possible;
    DLREF *erase = dl_get_orderlist(page);
    LISTOBJECT *lobj = dlref_lobj(erase);
    HDL_LIST *hlist;
    Bool fulltest = !IS_INTERPRETER();
    Bool more; /* keep looking for a make of some kind */

    omit_unpack_rasterstyle(page); /* omit details may have changed */
    omit_data->success = TRUE;
    omit_data->hdl = dlPageHDL(page);
    HQASSERT(hdlEnclosingGroup(omit_data->hdl) == NULL,
             "Base HDL should not have an enclosing group");
    omit_data->group = NULL;
    omit_data->override_colortype = GSC_UNDEFINED;
    /* A full test happens when rendering, but possibly not for
       currentseparationorder.  Omit testing may be incomplete for
       currentseparationorder because there may be no way to convert blend space
       dl colors to device space.  In this case objects are deemed to mark all
       seps for safety.  There's a reset before omit testing for rendering, when
       everything can be tested. */
    if ( fulltest && !omit_data->fulltest ) {
      guc_resetOmitSeparations(omit_data->hr);
      omit_data->othersep = FALSE;
    }
    omit_data->fulltest = fulltest;
    omit_data->convert_otf = FALSE; /* Never need OTF in base HDL */
    HQASSERT(lobj->opcode == RENDER_erase, "No erase at start of DL");
    HQASSERT((lobj->marker & MARKER_DEVICECOLOR) != 0,
             "Erase should be in device space already");
    /* must ensure erase object fed first to omit blank seps code */
    more = dl_omitblankseps(omit_data, lobj);

    /* Ensure preconvert workspace is up-to-date. Needs to be done in a separate
       pass since preconverting works down the group stack until device space is
       reached. */
    for ( hlist = page->all_hdls; more && otf && hlist != NULL;
          hlist = hlist->next ) {
      Group *group = hdlGroup(hlist->hdl);
      if ( group != NULL ) {
        Preconvert *preconvert = groupPreconvert(group);
        if ( preconvert != NULL &&
             !preconvert_update(page, preconvert) )
          return FALSE;
      }
    }

    /* Iterate over all active HDLs on the page, and then for each element
       within each HDL ... */
    for ( hlist = page->all_hdls; more && hlist != NULL; hlist = hlist->next ) {
      omit_data->group = hdlEnclosingGroup(hlist->hdl);
      if ( omit_data->group == NULL || !groupMustComposite(omit_data->group) ) {
        DLRANGE dlrange;
        int32 purpose = hdlPurpose(hlist->hdl);

        omit_data->hdl = hlist->hdl;
        omit_data->override_colortype =
          purpose == HDL_SHFILL ? GSC_SHFILL
          : (purpose == HDL_VIGNETTE ? GSC_VIGNETTE : GSC_UNDEFINED);

        hdlDlrange(hlist->hdl, &dlrange);
        for ( dlrange_start(&dlrange); more && !dlrange_done(&dlrange);
              dlrange_next(&dlrange) ) {
          lobj = dlrange_lobj(&dlrange);

          if ( lobj->p_ncolor ) {
            if ( !dl_is_none(lobj->p_ncolor) ) {
              omit_data->convert_otf = (otf && omit_data->group != NULL &&
                                        (lobj->marker & MARKER_DEVICECOLOR) == 0);
              more = dl_omitblankseps(omit_data, lobj);
            }
          }
        }
      }
    }
    if ( !omit_data->success )
      return FALSE;

    omit_data->walked = TRUE; /* Mark that we've looked at DL */
  }
  return TRUE;
}


Bool omit_black_transfer(DL_OMITDATA *omit_data)
{
  GUCR_COLORANT *black = NULL ;

  /* If the black colorant is not fully fledged, the black index may still be
     a known colorant index because of the ProcessColorModel. We need to
     avoid black transfer if it is not fully-fledged and present. */
  if ( omit_data->blackIndex != COLORANTINDEX_NONE ) {
    const GUCR_COLORANT_INFO *colorantInfo;

    gucr_colorantHandle(omit_data->hr, omit_data->blackIndex, &black) ;

    if ( black && !gucr_colorantDescription(black, &colorantInfo) )
      black = NULL ; /* Not actually present */
  }

 /* If /All, superblacks, or registermarks are the only non-black marks, and
    the black colorant is fully-fledged, then enable only the Black
    separation. If the All separation requires rendering properly, enable all
    separations. Note that registermarks and superblacks are not actually
    transferred onto the /All separation, they retain their own original
    colorant set. */
  if ( omit_data->walked && !omit_data->othersep && black ) {
    if ( omit_data->allsep || omit_data->multisep ) {
      /* Reset all omits, then enable black */
      if (! guc_resetOmitSeparations(omit_data->hr))
        return FALSE;
      (void)guc_dontOmitSeparation(omit_data->hr, omit_data->blackIndex) ;

      HQTRACE((debug_omission & DEBUG_OMISSION_BLACKTRANSFER) != 0,
              ("Transferred /All separations to /Black")) ;
    }
  }

  return TRUE ;
}

/* Initialise separation omission private data */
Bool start_separation_omission(DL_STATE *page)
{
  DL_OMITDATA omit_init = {
    NULL,                 /* page */
    NULL,                 /* raster style */
    NULL,                 /* erase_color */
    DLC_TINT_WHITE,       /* eraseTint */
    -1,                   /* erase_spotno */
    0,                    /* nalloc */
    0,                    /* ncomps */
    NULL,                 /* quantised erase colorvalues */
    NULL,                 /* erase color values */
    NULL,                 /* erase colorant indices */
    NULL,                 /* colorant flags */
    0,                    /* nmapalloc */
    NULL,                 /* colorant index map */
    COLORANTINDEX_NONE,   /* blackIndex */
    NULL,                 /* hdl */
    NULL,                 /* group */
    GSC_UNDEFINED,        /* override_colortype */
    TRUE,                 /* fulltest */
    FALSE,                /* negprint */
    FALSE,                /* allsep */
    FALSE,                /* multisep */
    FALSE,                /* othersep */
    FALSE,                /* success */
    FALSE,                /* walked */
    FALSE,                /* superblacks */
    FALSE,                /* registermarks */
    FALSE,                /* knockouts */
    FALSE,                /* imagecontents */
    FALSE,                /* beginpage */
    FALSE,                /* endpage */
    FALSE,                /* otf_possible */
    FALSE                 /* convert_otf */
  };

  HQASSERT(page->omit_data == NULL, "omitdata already exists");

  page->omit_data = mm_alloc(mm_pool_temp, sizeof(DL_OMITDATA),
                             MM_ALLOC_CLASS_SEP_OMIT);
  if ( page->omit_data == NULL )
    return error_handler(VMERROR);

  omit_init.otf_possible = (SystemParams.TransparencyStrategy == 1);
  *page->omit_data = omit_init;

  page->omit_data->page = page;
  omit_unpack_rasterstyle(page);

  return TRUE;
}

void finish_separation_omission(DL_STATE *page)
{
  DL_OMITDATA *omit_data = page->omit_data;

  if ( omit_data != NULL ) {
    if ( omit_data->hr != NULL )
      guc_discardRasterStyle(&omit_data->hr);

    if ( omit_data->nalloc > 0 )
      mm_free(mm_pool_temp, omit_data->erase_qcvs,
              omit_data->nalloc * (sizeof(COLORVALUE) +      /* erase_qcvs */
                                   sizeof(COLORVALUE) +      /* erase_cis */
                                   sizeof(COLORANTINDEX) +   /* erase_cvs */
                                   sizeof(COLORANT_FLAGS))); /* erase_flags */

    if ( omit_data->nmapalloc > 0 )
      mm_free(mm_pool_temp, omit_data->ci_map,
              omit_data->nmapalloc * sizeof(int32));

    mm_free(mm_pool_temp, omit_data, sizeof(DL_OMITDATA));

    page->omit_data = NULL;
  }
}

void init_C_globals_renderom(void)
{
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  debug_omission = 0 ;
  DEBUG_OMIT_CLEAR() ;
#endif
}

/* Log stripped */
