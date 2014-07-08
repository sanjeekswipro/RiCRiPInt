/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:renderfn.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rendering functions for display list objects.
 */

#include "core.h"
#include "swerrors.h"
#include "renderfn.h"

#include "swdevice.h"
#include "scanconv.h"
#include "spanlist.h"

#include "hqbitvector.h"
#include "timing.h"
#include "bitblts.h"
#include "bitblth.h"
#include "blttables.h"
#include "blitcolors.h"
#include "blitcolorh.h"
#include "pixelLabels.h"
#include "display.h"
#include "clipblts.h"
#include "dlstate.h"
#include "ndisplay.h"
#include "toneblt.h"
#include "rleblt.h"
#include "backdropblt.h"
#include "graphics.h" /* CHARCACHE */
#include "gu_chan.h" /* guc_allowMaxBlts */
#include "color.h" /* ht_getColorState */
#include "htrender.h" /* SLOWGENERAL, et. al. */
#include "rlecache.h"
#include "devops.h"
#include "dl_bres.h"
#include "dl_purge.h"
#include "patternrender.h" /* REPLICATING */
#include "render.h"
#include "renderim.h"
#include "rendersh.h"
#include "gscxfer.h"
#include "hdl.h"
#include "group.h"
#include "cce.h"       /* CCEModeNormal */
#include "trap.h"      /* trapRenderTrap */
#include "vnobj.h"      /* VIGNETTEOBJECT */
#include "pclAttrib.h"
#include "gu_htm.h" /* MODHTONE_REF */
#include "backdrop.h" /* Backdrop */
#include "spotlist.h" /* spotlist_iterate */
#include "dl_cell.h"
#include "shadex.h"
#include "imexpand.h" /* im_expand_ims_alpha et. al. */
#include "imstore.h"

/* Reduce maskclip to span sizes */
static void maskedimage_reduce(render_blit_t *rb,
                               dcoord y,
                               dcoord xs, dcoord xe)
{
  render_info_t *p_ri = (render_info_t *)rb->p_ri ; /* Cast away constness */

  UNUSED_PARAM(dcoord, y) ;

  if ( xs > p_ri->x1maskclip )
    p_ri->x1maskclip = xs ;
  if ( xe < p_ri->x2maskclip )
    p_ri->x2maskclip = xe ;
}

/* Renders the mask into maskedimageform - if there is a complex clip
   the mask is rendered through the clip. If maskedimageform is still
   rlencoded, check the spans to determine if it is possible to have
   an x1/2maskclip that allows optimisations like 3partimageandgo. */
static Bool render_maskedimage_mask(IMAGEOBJECT *mask, render_info_t *p_ri)
{
  render_blit_t rb_copy = p_ri->rb ;
  Bool result = TRUE ;
  blit_chain_t mask_blits = *(p_ri->rb.blits) ;
  FORM *maskedimageform = &p_ri->p_rs->forms->maskedimageform;
  blit_colormap_t blitmap ;
  blit_color_t color ;

  HQASSERT( mask != NULL ,
            "mask is null in render_maskedimage_mask" ) ;
  HQASSERT(p_ri->rb.maxmode != BLT_MAX_NONE ||
           !DOING_BLITS(&mask_blits, MAXBLT_BLIT_INDEX),
           "Maxblit functions set when not maxblitting") ;

  /* Set the color to black or white for consistency; this probably isn't
     absolutely needed, but is nice to do. The black/white color setting is
     inside each of the branches. */
  blit_colormap_mask(&blitmap) ;
  blit_color_init(&color, &blitmap) ;
  rb_copy.color = &color ;

  /* Turn off maxblits while producing mask */
  rb_copy.blits = &mask_blits ;
  CLEAR_BLITS(&mask_blits, MAXBLT_BLIT_INDEX) ;

  theFormHOff(*maskedimageform) = theFormHOff(*rb_copy.outputform) ;
  rb_copy.outputform = maskedimageform;
  rb_copy.depth_shift = 0; /* don't do multibit in the mask */

  if ( (rb_copy.clipmode == BLT_CLP_COMPLEX &&
        theFormT(*rb_copy.clipform) == FORMTYPE_BANDBITMAP) ||
       !rleclip_initform(rb_copy.outputform) ) {
    /* No chance that xmaskclip optimisations will work because the complex
       clip contains too many spans (or not enough room for even one span)
       for it to be represented by rle. */
    theFormT(*rb_copy.outputform) = FORMTYPE_BANDBITMAP ;

    rb_copy.ylineaddr = BLIT_ADDRESS(theFormA(*rb_copy.outputform),
      (( p_ri->clip.y1 - theFormHOff(*rb_copy.outputform)
         - rb_copy.y_sep_position )
       * theFormL(*rb_copy.outputform)));

    blit_color_mask(&color, TRUE /*White*/) ;

    /* Force use of unclipped blit to clear mask area regardless of actual
       clip type. */
    SET_BLITS(&mask_blits, BASE_BLIT_INDEX,
              &blitslice0[BLT_CLP_NONE],
              &blitslice0[BLT_CLP_NONE],
              &blitslice0[BLT_CLP_NONE]) ;

    DO_BLOCK(&rb_copy, p_ri->clip.y1, p_ri->clip.y2,
             p_ri->clip.x1, p_ri->clip.x2);

    blit_color_mask(&color, FALSE /*Black*/) ;

    SET_BLITS(&mask_blits, BASE_BLIT_INDEX,
              &blitslice1[BLT_CLP_NONE],
              &blitslice1[BLT_CLP_RECT],
              &blitslice1[BLT_CLP_COMPLEX]) ;

    if ( ( theIOptimize( mask ) & IMAGE_OPTIMISE_ROTATED ) != 0 ) {
      HQASSERT((theIOptimize(mask) & IMAGE_OPTIMISE_1TO1) == 0,
               "Cannot be both rotated and 1 to 1 mask") ;
      result = setuprotatedmaskandgo(mask , &rb_copy);
    } else {
      result = setupmaskandgo(mask, &rb_copy) ;
    }

    if ( result ) {
      /* Switch off xclipmask optimisation. */
      p_ri->x1maskclip = MAXDCOORD;
      p_ri->x2maskclip = MINDCOORD;
    }
  } else {
    blit_color_mask(&color, FALSE /*Black*/) ;

    /* Either there is no complex clip or it is RLE-encoded, therefore
       xclipmask optimisations may still be possible. */

    /* Use charbltn to call the mask span function */
    SET_BLITS(&mask_blits, BASE_BLIT_INDEX,
              &maskimageslice[BLT_CLP_NONE],
              &maskimageslice[BLT_CLP_RECT],
              &maskimageslice[BLT_CLP_COMPLEX]) ;

    if ( ( theIOptimize( mask ) & IMAGE_OPTIMISE_ROTATED ) != 0 )
      result = setuprotatedmaskandgo(mask, &rb_copy);
    else
      result = setupmaskandgo(mask, &rb_copy) ;

    if ( result ) {
      if ( theFormT(*rb_copy.outputform) == FORMTYPE_BANDRLEENCODED ) {
        /* outputform is still rleencoded therefore look for new
           maskclips. */

        int32 l = theFormL(*rb_copy.outputform) ;
        int32 rh = theFormRH(*rb_copy.outputform) ;
        blit_t *llineaddr = theFormA(*rb_copy.outputform) ;
        dcoord x1 = p_ri->clip.x1 + rb_copy.x_sep_position ;
        dcoord x2 = p_ri->clip.x2 + rb_copy.x_sep_position ;

        /* Reset the x1, x2 maskclip limits to the largest unclipped area
           within the mask band. */
        while ( --rh >= 0 ) {
          spanlist_t *spans = (spanlist_t *)llineaddr ;
          if ( !spanlist_merge(spans) || spanlist_count(spans) != 1 ) {
            p_ri->x1maskclip = MAXDCOORD;
            p_ri->x2maskclip = MINDCOORD;
            break ;
          } else {
            spanlist_intersecting(spans, maskedimage_reduce, NULL,
                                  &rb_copy, 0 /* y not used */, x1, x2,
                                  rb_copy.x_sep_position) ;
            if ( p_ri->x1maskclip > p_ri->x2maskclip )
              break ;
          }
          llineaddr = BLIT_ADDRESS(llineaddr, l) ;
        }

        if ( p_ri->rb.depth_shift == 0 )
          /* 1-bit screened, therefore must convert to bitmapped. */
          bandrleencoded_to_bandbitmap(rb_copy.outputform,
                                       p_ri->p_rs->forms->halftonebase,
                                       x1, x2) ;
      } else {
        /* Too many spans (or not enough room for even one span) to
           represent in rle, therefore cannot do xclipmask optimisations. */
        p_ri->x1maskclip = MAXDCOORD ;
        p_ri->x2maskclip = MINDCOORD ;
      }
    }
  }
  return result ;
}

/* ====================================================================== */

static void do_render_rect(render_info_t *ri,
                           LISTOBJECT* lobj)
{
  int32 x1 , y1;
  int32 x2 , y2;
  render_blit_t *rb = &ri->rb ;

  x1 = lobj->bbox.x1;
  y1 = lobj->bbox.y1;
  x2 = lobj->bbox.x2;
  y2 = lobj->bbox.y2;

  bbox_clip_y(&ri->clip, y1, y2);
  if ( y1 > y2 )
    return;
  bbox_clip_x(&ri->clip, x1, x2);
  if ( x1 > x2 )
    return;

  rb->ylineaddr = BLIT_ADDRESS(theFormA(*rb->outputform),
                               theFormL(*rb->outputform)*(y1 - theFormHOff(*rb->outputform) - rb->y_sep_position));
  rb->ymaskaddr = BLIT_ADDRESS(theFormA(*rb->clipform),
                               theFormL(*rb->clipform)*(y1 - theFormHOff(*rb->clipform) - rb->y_sep_position));

  DO_BLOCK(rb, y1, y2, x1, x2);
}

/* ====================================================================== */

/* Pattern replication: pattern replication is either done by char replicator
   function (setup by begin_pattern()), or by block replicator if rlechar() is
   called. */

/* Macro to save adding very ugly repetitive asserts to the cases below that */
/* deal with non blank forms */
#define ASSERT_NONBLANK_FORM(form, text) \
  HQASSERT(theFormH(*(form)) != 0 && theFormW(*(form)) != 0 && \
           theFormL(*(form)) != 0 && theFormS(*(form)) != 0, (text))

static void do_render_char(render_info_t *ri, LISTOBJECT* lobj)
{
  /* common code for all the render_char_... routines */
  DL_CHARS *dlchars = lobj->dldata.text;
  int32 i;

  for ( i = 0; i < dlchars->nchars; i++ )
  {
    CHARCACHE *thechar;
    FORM *tform = dlchars->ch[i].form;
    dcoord sx   = dlchars->ch[i].x;
    dcoord sy   = dlchars->ch[i].y;

    /* optimise by ignoring completely clipped characters */
    if ( sy <= ri->clip.y2 ) {
    TRYAGAIN:
      switch ( theFormT(*tform) ) {
      case FORMTYPE_CHARCACHE:
        thechar = (CHARCACHE*)tform;
        tform = theForm(*thechar);
        goto TRYAGAIN;

      case FORMTYPE_CACHERLE1:
      case FORMTYPE_CACHERLE2:
      case FORMTYPE_CACHERLE3:
      case FORMTYPE_CACHERLE4:
      case FORMTYPE_CACHERLE5:
      case FORMTYPE_CACHERLE6:
      case FORMTYPE_CACHERLE7:
      case FORMTYPE_CACHERLE8:
        ASSERT_NONBLANK_FORM(tform,
                             "do_render_char: zeros in CACHERLEX case");
        /* Pattern replication is taken care of by block routines */
        rlechar(&ri->rb, tform, sx, sy);
        break;

        /* This unlikely variant is needed for handling setscreen patterns in contone */
      case FORMTYPE_HALFTONEBITMAP:
        HQASSERT(dlchars->nchars == 1,
                 "FORMTYPE_HALFTONEBITMAP should be only form in char object") ;
        HQASSERT(!ri->p_rs->cs.fIsHalftone,
                 "form type FORMTYPE_HALFTONEBITMAP should only be used in contone");
        ASSERT_NONBLANK_FORM(tform,
                             "do_render_char: zeros in HALFTONEBITMAP case");
        /* FALL THROUGH */
      case FORMTYPE_CACHEBITMAP:
        ASSERT_NONBLANK_FORM(tform,
                             "do_render_char: zeros in CACHEBITMAP case");
        DO_CHAR(&ri->rb, tform, sx, sy);
        break;

      case FORMTYPE_BLANK:      /* empty form */
        HQASSERT( theFormS(*tform) == 0,
                  "Blank form has size != 0 in do_render_char");
        HQASSERT( theFormA(*tform) == NULL,
                  "Blank form isn't empty in do_render_char");
        HQASSERT( theFormH(*tform) == 0 && theFormW(*tform) == 0,
                  "do_render_char: Blank form must have zero w & h");
        HQASSERT( theFormL(*tform),
                  "do_render_char: Blank form must have zero lbyte");
        break;

      default:
        HQFAIL("Invalid form type in do_render_char");
      }
    }
  }
}


static Bool white_on_white_check(render_info_t *p_ri)
{
  const render_state_t *p_rs = p_ri->p_rs ;
  blit_color_t *color, *erase ;
  const blit_colormap_t *map ;
  channel_index_t index ;

  if ( ! *p_rs->cs.p_white_on_white )
    return FALSE ;

  color = p_ri->rb.color ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_quantised, "Current color isn't quantised") ;

  erase = p_rs->cs.erase_color ;
  VERIFY_OBJECT(erase, BLIT_COLOR_NAME) ;
  HQASSERT(erase->valid & blit_color_quantised, "Erase color isn't quantised") ;

  HQASSERT(color->map == erase->map,
           "Current color and erase color use different channel sets") ;

  /* - Self-intersection blitting invalidates the white-on-white optimisation,
       because the objects are appearing out of order.
     - PCL ROPing with anything but 252 (the default) mixes with colors in
       awkward ways, so invalidate the optimisation.
     - Screening with a different screen from the erase color invalidates the
       optimisation, because the bit pattern will probably be different.

     In contrast to previous versions of this function, we don't disable for
     maxblitting (because if the colors are the same, the max/min of both of
     them is the same). It is not usable on backdrops, and *p_white_on_white is
     initialised to FALSE for them, so any alpha here is not for compositing,
     just an alpha output. */
  if ( p_rs->cs.fSelfIntersect ||
       (p_ri->lobj->objectstate != NULL &&
        p_ri->lobj->objectstate->pclAttrib != NULL &&
        p_ri->lobj->objectstate->pclAttrib->rop != 252) ||
       (p_rs->cs.fIsHalftone &&
        (color->quantised.spotno != erase->quantised.spotno ||
         color->quantised.type != erase->quantised.type)) ) {
    *p_rs->cs.p_white_on_white = FALSE ;
    return FALSE ;
  }

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  for ( index = 0 ; index < map->nchannels ; ++index ) {
    if ( (color->state[index] & blit_channel_present) != 0 ) {
      /* The current color channels must be a subset of the erase color, and
         all channels must have the same quantised value. */
      if ( (erase->state[index] & blit_channel_present) == 0 ||
           color->quantised.qcv[index] != erase->quantised.qcv[index] ) {
        *p_rs->cs.p_white_on_white = FALSE ;
        return FALSE ;
      }
    }
  }
  return TRUE ;
}

/** Predicate to determine if a PCL pattern is used with intermediate colors
    that may require screening. */
static Bool pclStateScreened(const STATEOBJECT *state)
{
  const PclAttrib *attrib ;

  HQASSERT(state, "No state object") ;
  if ( (attrib = state->pclAttrib) == NULL )
    return FALSE ;

  /** \todo ajcd 2014-06-19: Should this just be _MANY and _ALL? Most
      single-color cases or single and white cases will be converted to _NONE
      cases by getPclAttrib(), if the ROP allows it. */
  return attrib->patternColors >= PCL_PATTERN_OTHER ;
}

/* ---------------------------------------------------------------------- */
/* renderfuncs array indexed according to the constants defined in display.h */

static Bool render_void(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                        Bool screened) ;
static Bool render_vignette(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                            Bool screened) ;
static Bool render_image(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                         Bool screened) ;
static Bool render_shfill(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                          Bool screened) ;
static Bool render_hdl(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                       Bool screened) ;
static Bool render_group(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                         Bool screened) ;
static Bool render_erase(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                         Bool screened) ;
static Bool render_char(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                        Bool screened) ;
static Bool render_rect(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                        Bool screened) ;
static Bool render_quad(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                        Bool screened) ;
static Bool render_fill(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                        Bool screened) ;
static Bool render_mask(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                        Bool screened) ;
static Bool render_backdrop(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                        Bool screened) ;
static Bool render_cell(/*@notnull@*/ /*@in@*/ render_info_t *p_ri,
                        Bool screened) ;

RENDER_FUNCTION renderfuncs[ N_RENDER_OPCODES ] = {
  render_void,
  render_erase,
  render_char,
  render_rect,
  render_quad,
  render_fill,
  render_mask,
  render_image,
  render_vignette,
  render_gouraud,
  render_shfill,
  render_void,
  render_hdl,
  render_group,
  render_backdrop,
  render_cell
};

/* ---------------------------------------------------------------------- */
static Bool render_void(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                        Bool screened)
{
  UNUSED_PARAM(render_info_t*, p_ri);
  UNUSED_PARAM(Bool, screened);

  HQFAIL("render_void should never be called") ;

  return FALSE;
}

/* ---------------------------------------------------------------------- */
static Bool render_erase(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                         Bool screened)
{
  const surface_t *surface ;
  Bool locked = FALSE ;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");

  if ( screened &&
       (blit_quantise_state(p_ri->rb.color) & blit_quantise_mid) != 0 ) {
    /* Halftoning */
    HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
             "Using a degenerate screen");
    LOCK_HALFTONE(p_ri->ht_params);
    locked = TRUE ;
  }

  surface = p_ri->surface ;
  HQASSERT(surface != NULL, "No output surface") ;

  /* Get unclipped base blits for erasing with, if necessary */
  SET_BLITS(p_ri->rb.blits, BASE_BLIT_INDEX,
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_NONE]) ;

  *p_ri->p_rs->cs.p_white_on_white = TRUE;

  (*surface->areafill)(&p_ri->rb, p_ri->rb.outputform);

  if ( locked )
    UNLOCK_HALFTONE(p_ri->ht_params);

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool render_char(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                        Bool screened)
{
  const surface_t *surface ;
  Bool locked = FALSE ;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");
  HQASSERT(p_ri->rb.clipmode < BLT_CLP_N, "clip mode is invalid" );

  if (white_on_white_check(p_ri))
    return TRUE;

  if ( screened ) {
    if ( (blit_quantise_state(p_ri->rb.color) & blit_quantise_mid) != 0 ||
         pclStateScreened(p_ri->lobj->objectstate) ) {
      /* Halftoning */
      HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
               "Using a degenerate screen");
      LOCK_HALFTONE(p_ri->ht_params);
      locked = TRUE ;
    }
  }

  surface = p_ri->surface ;
  HQASSERT(surface != NULL, "No output surface") ;

  SET_BLITS(p_ri->rb.blits, BASE_BLIT_INDEX,
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_RECT],
            &surface->baseblits[BLT_CLP_COMPLEX]) ;

  do_render_char(p_ri, p_ri->lobj);

  if ( locked )
    UNLOCK_HALFTONE(p_ri->ht_params);

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool render_rect(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                        Bool screened)
{
  const surface_t *surface ;
  Bool locked = FALSE ;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");
  HQASSERT(p_ri->rb.clipmode < BLT_CLP_N, "clip mode is invalid" );

  if (white_on_white_check(p_ri))
    return TRUE;

  if ( screened ) {
    if ( (blit_quantise_state(p_ri->rb.color) & blit_quantise_mid) != 0 ||
         pclStateScreened(p_ri->lobj->objectstate) ) {
      /* Halftoning */
      HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
               "Using a degenerate screen");
      LOCK_HALFTONE(p_ri->ht_params);
      locked = TRUE ;
    }
  }

  surface = p_ri->surface ;
  HQASSERT(surface != NULL, "No output surface") ;

  SET_BLITS(p_ri->rb.blits, BASE_BLIT_INDEX,
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_RECT],
            &surface->baseblits[BLT_CLP_COMPLEX]) ;

  do_render_rect(p_ri, p_ri->lobj);

  if ( locked )
    UNLOCK_HALFTONE(p_ri->ht_params);

  return TRUE;
}

static Bool render_nfill(render_info_t *p_ri, NFILLOBJECT *nfill, Bool screened)
{
  const surface_t *surface ;
  Bool locked = FALSE ;
  int32 nfill_flags = 0 ;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");
  HQASSERT(p_ri->rb.clipmode < BLT_CLP_N, "clip mode is invalid" );

  if ( !screened ) {
    nfill_flags = MERGE_SPANS ;
  } else {
    PclAttrib *pclAttrib = p_ri->lobj->objectstate->pclAttrib ;

    /* PCL ROPs are handled in the PCL blitter for screened output, rather than
       the backdrop for contone.  The PCL blitter requires non-overlapping spans
       for ROPs to avoid double-compositing artifacts. */
    if ( pclAttrib != NULL ) {
      if ( pclROPRequiresDestination(pclAttrib->rop) )
        nfill_flags = MERGE_SPANS ;
    }

    if ( (blit_quantise_state(p_ri->rb.color) & blit_quantise_mid) != 0 ||
         pclStateScreened(p_ri->lobj->objectstate) ) {
      /* Halftoning */
      HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
               "Using a degenerate screen");
      LOCK_HALFTONE(p_ri->ht_params);
      locked = TRUE ;
    }
  }

  surface = p_ri->surface ;
  HQASSERT(surface != NULL, "No output surface") ;

  SET_BLITS(p_ri->rb.blits, BASE_BLIT_INDEX,
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_RECT],
            &surface->baseblits[BLT_CLP_COMPLEX]) ;

  if ( REPLICATING(p_ri) ) {
    REPAIR_REPLICATED_NFILL(nfill, p_ri->clip.y1);
  } else {
    REPAIR_NFILL(nfill, p_ri->clip.y1);
  }
  scanconvert_band(&p_ri->rb, nfill, nfill_flags|nfill->type);

  if ( locked )
    UNLOCK_HALFTONE(p_ri->ht_params);

  return TRUE ;
}

/**
 * Convert simplified fill into a nfill on the fly and call the normal
 * NFILL rendering
 */
static Bool render_quad(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                        Bool screened)
{
  NFILLOBJECT nfill;
  NBRESS threads[4];

  if (white_on_white_check(p_ri))
    return TRUE;

  quad_to_nfill(p_ri->lobj, &nfill, threads);

  /* Initialise nfill. Note we do this every time which means we do not get
   * the advantages of avoiding re-calculating state. But this is very minimal
   * with something this simple.
   */
  return render_nfill(p_ri, &nfill, screened);
}

/* ---------------------------------------------------------------------- */
static Bool render_fill(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                        Bool screened)
{
  NFILLOBJECT *nfill;
  Bool result;

  if (white_on_white_check(p_ri))
    return TRUE;

  nfill = (NFILLOBJECT *)load_dldata(p_ri->lobj);
  NFILL_LOCK_WR_CLAIM(nfill);
  result = render_nfill(p_ri, nfill, screened);
  NFILL_LOCK_RELEASE();
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool render_mask(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                        Bool screened)
{
  const surface_t *surface ;
  Bool locked = FALSE ;
  IMAGEOBJECT *themask ;
  Bool result ;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");
  HQASSERT(p_ri->rb.clipmode < BLT_CLP_N, "clip mode is invalid" );

  if (white_on_white_check(p_ri))
    return TRUE;

  if ( screened ) {
    if ( (blit_quantise_state(p_ri->rb.color) & blit_quantise_mid) != 0 ||
         pclStateScreened(p_ri->lobj->objectstate) ) {
      HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
               "Using a degenerate screen");
      LOCK_HALFTONE(p_ri->ht_params);
      locked = TRUE ;
    }
  }

  surface = p_ri->surface ;
  HQASSERT(surface != NULL, "No output surface") ;

  SET_BLITS(p_ri->rb.blits, BASE_BLIT_INDEX,
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_RECT],
            &surface->baseblits[BLT_CLP_COMPLEX]) ;

  themask = p_ri->lobj->dldata.image;

  if ( (theIOptimize(themask) & IMAGE_OPTIMISE_ROTATED) != 0 ) {
    HQASSERT((theIOptimize(themask) & IMAGE_OPTIMISE_1TO1) == 0,
             "Cannot be both rotated and 1 to 1 mask") ;
    result = setuprotatedmaskandgo(themask, &p_ri->rb);
  } else {
    result = setupmaskandgo(themask, &p_ri->rb);
  }
  im_storerelease( themask->ims , p_ri->p_rs->band , FALSE ) ;

  if ( locked )
    UNLOCK_HALFTONE(p_ri->ht_params);

  return result ;
}

/* ---------------------------------------------------------------------- */
static Bool render_image(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                         Bool screened)
{
  IMAGEOBJECT*  theimage;
  IMAGEOBJECT*  themask;
  Bool locked = FALSE ;
  Bool result = TRUE ;
  render_info_t ri_copy ;
  blit_color_t *color ;
  const surface_t *surface ;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");
  HQASSERT(p_ri->rb.clipmode < BLT_CLP_N, "clip mode is invalid" );
  HQASSERT(screened == p_ri->p_rs->cs.fIsHalftone,
           "Screened parameter is inconsistent with render state") ;

  color = p_ri->rb.color ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  surface = p_ri->surface ;
  HQASSERT(surface != NULL, "No output surface") ;

  /* If all of the colors came from knockouts, then there is no variation in
     the color. We can use a fast knockout method, especially for orthogonal
     images. */
  if ( color->noverrides == color->map->ncolors ) {
    /* We can do white on white checks, because the color is constant. */
    if (white_on_white_check(p_ri))
      return TRUE;

    if ( screened &&
         (blit_quantise_state(color) & blit_quantise_mid) != 0 ) {
      /* Halftoning */
      HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
               "Using a degenerate screen");
      LOCK_HALFTONE(p_ri->ht_params);
      locked = TRUE ;
    } else {
      HQASSERT(!screened || !pclStateScreened(p_ri->lobj->objectstate),
               "Overriding PCL patterned image with single color") ;
    }
  } else if ( screened ) {
    /* Halftoning */
    HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
             "Using a degenerate screen");
    LOCK_HALFTONE(p_ri->ht_params);
    locked = TRUE ;
  }

  RI_COPY_FROM_RI(&ri_copy, p_ri) ;

  /* The image blit is accessed through the blit chain, so we always need
     this. */
  SET_BLITS(ri_copy.rb.blits, BASE_BLIT_INDEX,
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_RECT],
            &surface->baseblits[BLT_CLP_COMPLEX]) ;

  theimage = ri_copy.lobj->dldata.image;

  /* white_on_white handled by setup...andgo */

  themask = theimage->mask;
  if ( themask != NULL ) {
    /* Image type 3 and 4, masked images, change the clipform. */
    FORM *clipform = &p_ri->p_rs->forms->maskedimageform;
    /* Shrink the clip area to the extent of the image. This will help
       restrict the length of runs blitted into the mask form when
       converting RLE-encoded masks into bitmap masks, avoiding clearing
       areas of the mask form that won't be touched. */
    bbox_intersection(&ri_copy.clip, &theimage->bbox, &ri_copy.clip) ;
    result = render_maskedimage_mask(themask, &ri_copy) ;
    im_storerelease( themask->ims , ri_copy.p_rs->band , FALSE ) ;
    ri_copy.rb.clipmode = BLT_CLP_COMPLEX;
    theFormHOff(*clipform) = theFormHOff(*ri_copy.rb.clipform) ;
    ri_copy.rb.clipform = clipform;
  }

  if ( result ) {
    if ( (theIOptimize(theimage) & IMAGE_OPTIMISE_ROTATED) != 0 ) {
      result = setuprotatedimageandgo(theimage, &ri_copy.rb, screened);
    } else if ( ri_copy.rb.clipmode == BLT_CLP_COMPLEX &&
                ri_copy.x1maskclip < ri_copy.x2maskclip &&
                ri_copy.pattern_state == PATTERN_OFF ) {
      /** \todo ajcd 2008-09-30: Should make maskclip test such that a
          reasonable area is between the maskclip limits. At the moment, it
          could be as little as 2 pixels horizontally, and the extra setup will
          hurt performance in that case. The tricky bit is deciding what a
          "reasonable area" is. */
      result = setup3partimageandgo(theimage, &ri_copy.rb, screened);
    } else {
      result = setupimageandgo(theimage, &ri_copy.rb, screened);
    }
  }

  if ( theimage->ims ) {
    im_storerelease( theimage->ims , ri_copy.p_rs->band , FALSE ) ;
    if ( theimage->ime && im_expand_ims_alpha(theimage->ime) )
      im_storerelease(im_expand_ims_alpha(theimage->ime), ri_copy.p_rs->band, FALSE) ;
  }

  if ( locked )
    UNLOCK_HALFTONE(p_ri->ht_params);

  return result;
}

/* ====================================================================== */
static Bool render_vignette(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                            Bool screened)
{
  VIGNETTEOBJECT *vignette;
  render_info_t ri;
  DLRANGE dlrange;

  UNUSED_PARAM(Bool, screened) ;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");
  RI_COPY_FROM_RI(&ri, p_ri) ;
  ri.overrideColorType = GSC_VIGNETTE;

  HQASSERT(ri.lobj != NULL, "NULL object in p_ri");
  vignette = ri.lobj->dldata.vignette ;
  HQASSERT(vignette != NULL, "No vignette to render");
  hdlDlrange(ri.lobj->dldata.vignette->vhdl, &dlrange);

  /* If 'recurse' is true, each vignette element needs to be handled
   * independently because it is clipped separately. So just process the
   * subordinate display list like any other. Otherwise we know the objects
   * share enough common renderering properties that we can scoot through
   * them in a special optimised loop.
   */
  if ( !vignette->recurse ) {
    dlrange.common_render = TRUE;
    dlrange.forwards = !vignette->rollover;
    if ( dlpurge_inuse() ) /* no rollover optimisation if DL on disk */
      dlrange.forwards = TRUE;
  }
  return render_object_list_of_band(&ri, &dlrange);
}

/* ---------------------------------------------------------------------- */
static Bool render_shfill(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                          Bool screened)
{
  Bool transparency = FALSE ;
  LISTOBJECT *lobj ;
  blit_color_t *color ;
  TranAttrib *tranAttrib ;
  render_info_t ri_copy ;
  HDL *hdl ;
  channel_index_t n_varying_channels;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");
  HQASSERT(p_ri->rb.clipmode < BLT_CLP_N, "clip mode is invalid" );

  /* Force overriding of the colour type for background fills, etc. */
  RI_COPY_FROM_RI(&ri_copy, p_ri) ;
  ri_copy.overrideColorType = GSC_SHFILL;

  color = ri_copy.rb.color ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  n_varying_channels =
    color->nchannels - color->noverrides
    /* The type channel is unvarying, but not counted in noverrides. */
    - (color->map->type_index >= color->map->nchannels ? 0 : 1)
    /* The alpha channel is unvarying, if the merged color is opaque. */
    - (color->map->alpha_index >= color->map->nchannels ? 0
       : color->alpha == COLORVALUE_ONE);
  if ( n_varying_channels == 0 ) {
    /* We can do white-on-white checks, because the color is constant. */
    if ( white_on_white_check(&ri_copy) )
      return TRUE;
  }

  lobj = ri_copy.lobj ;
  HQASSERT(lobj, "No shfill LISTOBJECT") ;
  HQASSERT(lobj->objectstate, "No shfill STATEOBJECT") ;

  hdl = lobj->dldata.shade->hdl ;
  HQASSERT(hdl, "No shfill HDL") ;

  /* If this shfill is in transparency, and is not using normal blend mode with
     a constant alpha of 1 and no softmask, then painter's algorithm will not
     work. I suspect some other blend modes could be allowed with appropriate
     constant alphas (Darken and Lighten, for instance). */
  tranAttrib = lobj->objectstate->tranAttrib ;
  if ( color->alpha < COLORVALUE_ONE ||
       (tranAttrib &&
        (tranAttrib->blendMode != CCEModeNormal ||
         tranAttrib->alpha != COLORVALUE_ONE ||
         tranAttrib->softMask != NULL)) )
    transparency = TRUE ;

  return render_shfill_all(&ri_copy, screened, hdl,
                           n_varying_channels, transparency);
}

/* ---------------------------------------------------------------------- */
static Bool render_hdl(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                       Bool screened)
{
  Bool ok ;
  HDL *hdl = p_ri->lobj->dldata.hdl ;

  UNUSED_PARAM(Bool, screened);

  PROBE(SW_TRACE_RENDER_HDL, (intptr_t)hdl,
        ok = hdlRender(hdl, p_ri,
                       NULL /*transparency*/, FALSE /*self-intersect*/)) ;
  return ok ;
}

/* ---------------------------------------------------------------------- */
static Bool render_group(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                         Bool screened)
{
  Group *group = p_ri->lobj->dldata.group ;
  HDL *hdl = groupHdl(group) ;
  render_state_t rs ;
  dbbox_t bbox ;
  Bool ok ;

  UNUSED_PARAM(Bool, screened);

  HQASSERT(group != NULL, "Group is missing") ;
  HQASSERT(hdl != NULL, "Group has no HDL") ;

  /* Set up a local renderstate, so that we can restrict the group rendering
     limits to the minimum size possible (the intersection of the clip,
     the group's bbox, and the current render limits). */
  RS_COPY_FROM_RI(&rs, p_ri) ;

  /* Restrict to clip limit */
  bbox_intersection(&rs.ri.bounds, &rs.ri.clip, &rs.ri.bounds);

  /* Restrict to group's bounding box */
  hdlBBox(hdl, &bbox);
  bbox_intersection(&rs.ri.bounds, &bbox, &rs.ri.bounds) ;

  /* Since we restricted bounds to the clip above, they are now the
     intersection of the clip, the bounds, and the HDL bbox. Resetting the
     clip here is not strictly speaking necessary, but helps preserve
     sanity when debugging, so the clip doesn't ever appear to be larger
     than the bounds. */
  rs.ri.clip = rs.ri.bounds ;

  /* Test for degenerate region */
  if ( bbox_is_empty(&rs.ri.bounds) )
    return TRUE ;

  if ( !REPLICATING(&rs.ri) ) {
    HQASSERT(bbox_contains(&rs.cs.bandlimits, &rs.ri.bounds),
             "Bounds should be subset of bandlimits outside of pattern");
    rs.cs.bandlimits = rs.ri.bounds ;
  }

  /* Bounds have changed, so we need a new clip context. */
  if ( !clip_context_begin(&rs.ri) )
    return FALSE ;
#define return DO_NOT_return!

  HQASSERT(rs.ri.lobj != NULL &&
           rs.ri.lobj->objectstate != NULL, "No object state") ;

  PROBE(SW_TRACE_RENDER_GROUP, (intptr_t)hdl,
        ok = groupRender(group, &rs.ri, rs.ri.lobj->objectstate->tranAttrib));

  clip_context_end(&rs.ri) ;
#undef return

  return ok ;
}

/* ---------------------------------------------------------------------- */
static Bool render_backdrop(/*@notnull@*/ /*@in@*/ render_info_t*  p_ri,
                            Bool screened)
{
  const render_state_t *p_rs ;
  Backdrop *backdrop ;

  HQASSERT(RENDER_INFO_CONSISTENT(p_ri), "Render context is not consistent");

  /* Setup for backdrop rendering should have taken into account tesselated
     clip reduction. */
  HQASSERT(bbox_equal(&p_ri->bounds, &p_ri->clip),
           "Clip is not same as bounds");

  /* If all the colors come from knockouts (noverrides == ncolors), and not
     doing render properties, then the knockout is a no-op because only the
     erase has been rendered so far. Knockouts with render properties must go
     through the normal route as pixels may differ according to render
     properties. */
  if ( p_ri->rb.color->noverrides == p_ri->rb.color->map->ncolors &&
       !p_ri->rb.color->map->apply_properties )
    return TRUE; /* KO is a no-op (band contains erase only) */

  p_rs = p_ri->p_rs;
  backdrop = p_ri->lobj->dldata.backdrop;

  *p_rs->cs.p_white_on_white = FALSE;

  if ( p_ri->surface->backdropblit == NULL ) {
    HQFAIL("No surface backdrop blit") ;
    return detail_error_handler(UNREGISTERED, "Output surface does not support transparency") ;
  }

  if ( screened &&
       (spotlist_multi_spots(p_rs->page) ||
        ht_is_object_based_screen(p_ri->rb.color->quantised.spotno)) ) {
    /* Iterate over the backdrop for each spot number, and also object type
       if doing object-based screening. This reduces excessive screen
       switching at the expense of reading the backdrop multiple times. */
    SPOTNO_ITER iter;
    SPOTNO spotno;
    HTTYPE objtype;

    for ( spotlist_iterate_init(&iter, p_rs->page->spotlist);
          spotlist_iterate(&iter, &spotno, &objtype); ) {
      if ( !(*p_ri->surface->backdropblit)(p_rs->surface_handle,
                                           &p_ri->rb,
                                           backdrop,
                                           render_state_backdrop(p_rs),
                                           spotno, objtype) )
        return FALSE;
    }
  } else {
    /* Rendering a backdrop to contone or to halftone with just one screen.
       render_object_list_of_band has done modular skipping and gethalftone.
       Doesn't matter what object type was used, as it's not an o-b screen */
    HQASSERT(p_ri->rb.color->quantised.spotno == p_rs->lobjErase->objectstate->spotno,
             "Not using default halftone for backdrop");
    if ( !(*p_ri->surface->backdropblit)(p_rs->surface_handle,
                                         &p_ri->rb,
                                         backdrop,
                                         render_state_backdrop(p_rs),
                                         SPOT_NO_INVALID, HTTYPE_DEFAULT) )
      return FALSE;
  }
  return TRUE;
}


/** Render the spans of a multi-colored cell to halftoned output. */
static void render_cell_multi_screened( /*@in@*/ render_info_t *p_ri )
{
  dbbox_t bbox ;
  uint32 cn ;
  dcoord y ;
  CELL *cell ;
  const blit_slice_t *slice ;
  render_blit_t *rb = &p_ri->rb;
  FORM *outputform = rb->outputform;
  FORM *clipform = rb->clipform;
  COLORANTINDEX ci = blit_map_sole_index(rb->color->map);

  HQASSERT( p_ri != NULL , "NULL pointer to render info" ) ;

  cell = p_ri->lobj->dldata.cell ;
  slice = &p_ri->surface->baseblits[0];
  HQASSERT( ! cellismono( cell ) ,
            "Shouldn't be called with a monochrome cell." ) ;

  cellbbox( cell , & bbox ) ;
  if ( ! cellmapcolorant( cell , p_ri->ht_params->ci , & cn )) {
    return ;
  }

  for ( y = bbox.y1 ; y <= bbox.y2 ; y++ ) {
    dcoord x = bbox.x1 ;
    uint32 bitcount ;

    cellgetrowstart( cell , y - bbox.y1 , & bitcount ) ;

    rb->ylineaddr = BLIT_ADDRESS( theFormA(*outputform) ,
                                  ( theFormL(*outputform) *
                                    ( y - theFormHOff(*outputform) -
                                      rb->y_sep_position ))) ;
    rb->ymaskaddr = BLIT_ADDRESS( theFormA(*clipform) ,
                                  ( theFormL(*clipform) *
                                    ( y - theFormHOff(*clipform) -
                                      rb->y_sep_position ))) ;

    while ( x <= bbox.x2 ) {
      Bool transparent ;
      uint8 length ;
      dcoord x1, x2;

      celldecodespan( cell, &transparent, &length,
                      y - bbox.y1, &bitcount,
                      p_ri->p_rs->cs.selected_mht, rb->color );

      if ( ! transparent && y >= p_ri->clip.y1 && y <= p_ri->clip.y2 ) {
        x1 = x; x2 = x1 + length;
        bbox_clip_x( & p_ri->clip , x1 , x2 );
        if ( x1 <= x2 ) {
          Bool unlock = FALSE ;

          blit_color_quantise(rb->color);
          render_gethalftone(p_ri->ht_params, rb->color->quantised.spotno,
                             rb->color->quantised.type, ci, NULL);
          if ( ( rb->color->quantised.state & blit_quantise_mid) != 0 ) {
            /* Halftoning */
            HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
                     "Using a degenerate screen");
            LOCK_HALFTONE(p_ri->ht_params);
            unlock = TRUE ;
          }
          SET_BLITS_CURRENT(rb->blits, BASE_BLIT_INDEX, &slice[BLT_CLP_NONE],
                            &slice[BLT_CLP_RECT], &slice[BLT_CLP_COMPLEX]);
          DO_SPAN( rb , y , x1 , x2 ) ;
          if ( unlock )
            UNLOCK_HALFTONE( p_ri->ht_params ) ;
        }
      }
      x += length + 1 ;
    }
  }
}


static void render_cell_multi_contone( /*@in@*/ render_info_t *p_ri )
{
  CELL *cell ;
  render_blit_t *rb = & p_ri->rb ;
  dbbox_t bbox ;
  dcoord y ;
  dcoord min_y ;
  dcoord max_y ;

  HQASSERT(p_ri, "render info is null");

  cell = p_ri->lobj->dldata.cell ;

  HQASSERT( ! cellismono( cell ) ,
            "Shouldn't be called with a monochrome cell." ) ;

  cellbbox( cell , & bbox ) ;
  min_y = max( bbox.y1 , p_ri->clip.y1 ) ;
  max_y = min( bbox.y2 , p_ri->clip.y2 ) ;

  (void)blit_quantise_state(p_ri->rb.color);

  for ( y = min_y ; y <= max_y ; y++ ) {
    FORM *outputform = rb->outputform ;
    FORM *clipform = rb->clipform ;
    dcoord x = bbox.x1 ;
    uint32 bitcount ;

    cellgetrowstart( cell , y - bbox.y1 , & bitcount ) ;

    rb->ylineaddr = BLIT_ADDRESS( theFormA(*outputform) ,
                                  ( theFormL(*outputform) *
                                    ( y - theFormHOff(*outputform) -
                                      rb->y_sep_position ))) ;
    rb->ymaskaddr = BLIT_ADDRESS( theFormA(*clipform) ,
                                  ( theFormL(*clipform) *
                                    ( y - theFormHOff(*clipform) -
                                      rb->y_sep_position ))) ;

    while ( x <= bbox.x2 ) {
      Bool transparent ;
      uint8 length ;

      celldecodespan( cell, &transparent, &length,
                      y - bbox.y1, &bitcount,
                      p_ri->p_rs->cs.selected_mht, rb->color );

      if ( ! transparent ) {
        dcoord x1 ;
        dcoord x2 ;

        x1 = x ;
        x2 = x1 + length ;
        bbox_clip_x( & p_ri->clip , x1 , x2 ) ;
        if ( x1 <= x2 ) {
          blit_color_quantise(rb->color);
          blit_color_pack(rb->color);
          DO_SPAN( rb , y , x1 , x2 ) ;
        }
      }
      x += length + 1 ;
    }
  }

#if defined( DEBUG_CELL_OUTLINES )
  {
    FORM *outputform = rb->outputform ;
    FORM *clipform = rb->clipform ;
    dcoord min_x = max( bbox.x1 , p_ri->clip.x1 ) ;
    dcoord max_x = min( bbox.x2 , p_ri->clip.x2 ) ;

    celldecodedebugcolor( cell , rb->color ) ;
    blit_color_quantise( rb->color ) ;
    blit_color_pack( rb->color ) ;

    if ( bbox.y1 >= p_ri->clip.y1 && bbox.y1 <= p_ri->clip.y2 ) {
      rb->ylineaddr = BLIT_ADDRESS( theFormA(*outputform) ,
                                    ( theFormL(*outputform) *
                                      ( bbox.y1 - theFormHOff(*outputform) -
                                        rb->y_sep_position ))) ;
      rb->ymaskaddr = BLIT_ADDRESS( theFormA(*clipform) ,
                                    ( theFormL(*clipform) *
                                      ( bbox.y1 - theFormHOff(*clipform) -
                                        rb->y_sep_position ))) ;

      DO_SPAN( rb , bbox.y1 , min_x , max_x ) ;
    }
    if ( bbox.y2 >= p_ri->clip.y1 && bbox.y2 <= p_ri->clip.y2 ) {
      rb->ylineaddr = BLIT_ADDRESS( theFormA(*outputform) ,
                                    ( theFormL(*outputform) *
                                      ( bbox.y2 - theFormHOff(*outputform) -
                                        rb->y_sep_position ))) ;
      rb->ymaskaddr = BLIT_ADDRESS( theFormA(*clipform) ,
                                    ( theFormL(*clipform) *
                                      ( bbox.y2 - theFormHOff(*clipform) -
                                        rb->y_sep_position ))) ;

      DO_SPAN( rb , bbox.y2 , min_x , max_x ) ;
    }

    if ( bbox.x1 >= p_ri->clip.x1 && bbox.x1 <= p_ri->clip.x2 ) {
      for ( y = min_y ; y <= max_y ; y++ ) {
        rb->ylineaddr = BLIT_ADDRESS( theFormA(*outputform) ,
                                      ( theFormL(*outputform) *
                                        ( y - theFormHOff(*outputform) -
                                          rb->y_sep_position ))) ;
        rb->ymaskaddr = BLIT_ADDRESS( theFormA(*clipform) ,
                                      ( theFormL(*clipform) *
                                        ( y - theFormHOff(*clipform) -
                                          rb->y_sep_position ))) ;

        DO_SPAN( rb , y , bbox.x1 , bbox.x1 ) ;
      }
    }

    if ( bbox.x2 >= p_ri->clip.x1 && bbox.x2 <= p_ri->clip.x2 ) {
      for ( y = min_y ; y <= max_y ; y++ ) {
        rb->ylineaddr = BLIT_ADDRESS( theFormA(*outputform) ,
                                      ( theFormL(*outputform) *
                                        ( y - theFormHOff(*outputform) -
                                          rb->y_sep_position ))) ;
        rb->ymaskaddr = BLIT_ADDRESS( theFormA(*clipform) ,
                                      ( theFormL(*clipform) *
                                        ( y - theFormHOff(*clipform) -
                                          rb->y_sep_position ))) ;

        DO_SPAN( rb , y , bbox.x2 , bbox.x2 ) ;
      }
    }
  }
#endif /* DEBUG_CELL_OUTLINES */
}

/** This is a monochrome cell: the color comes purely from the
    dl_color so we only need a single bit for each span "color" just
    to denote whether it's transparent or not. */

static void render_cell_single( /*@in@*/ render_info_t *ri )
{
  dbbox_t bbox ;
  dcoord y ;
  CELL *cell ;

  HQASSERT( ri != NULL , "NULL pointer to render info" ) ;

  cell = ri->lobj->dldata.cell ;

  cellbbox( cell , & bbox ) ;

  for ( y = bbox.y1 ; y <= bbox.y2 ; y++ ) {
    render_blit_t *rb = & ri->rb ;
    FORM *outputform = rb->outputform ;
    FORM *clipform = rb->clipform ;
    dcoord x = bbox.x1 ;
    uint32 bitcount ;

    cellgetrowstart( cell , y - bbox.y1 , & bitcount ) ;

    rb->ylineaddr = BLIT_ADDRESS( theFormA(*outputform) ,
                                  ( theFormL(*outputform) *
                                    ( y - theFormHOff(*outputform) -
                                      rb->y_sep_position ))) ;
    rb->ymaskaddr = BLIT_ADDRESS( theFormA(*clipform) ,
                                  ( theFormL(*clipform) *
                                    ( y - theFormHOff(*clipform) -
                                      rb->y_sep_position ))) ;

    while ( x <= bbox.x2 ) {
      uint8 length ;
      Bool transparent = TRUE ;

      celldecodespan( cell, &transparent, &length,
                      y - bbox.y1, &bitcount,
                      ri->p_rs->cs.selected_mht, NULL );

      if ( ! transparent &&  y >= ri->clip.y1 && y <= ri->clip.y2 ) {
        dcoord x1 = x ;
        dcoord x2 = x1 + length ;

        bbox_clip_x( & ri->clip , x1 , x2 ) ;
        if ( x1 <= x2 ) {
          DO_SPAN( rb , y , x1 , x2 ) ;
        }
      }

      x += length + 1 ;
    }
  }
}


/* ---------------------------------------------------------------------- */

static Bool render_cell(render_info_t* p_ri, Bool screened)
{
  const surface_t *surface;
  Bool locked = FALSE;
  CELL *cell;

  HQASSERT(p_ri != NULL, "NULL pointer to render info");
  HQASSERT(p_ri->rb.clipmode < BLT_CLP_N, "clip mode is invalid");

  cell = p_ri->lobj->dldata.cell;
  HQASSERT(cell != NULL, "No cell data to render");

  bbox_intersection(&p_ri->clip, &p_ri->lobj->bbox, &p_ri->clip);

  if (bbox_is_empty(&p_ri->clip)) {
    return TRUE;
  }

  if (emptycell(cell))
    return TRUE ;

  surface = p_ri->surface;
  HQASSERT(surface != NULL, "No output surface");

  if (cellismono(cell)) {

    if (white_on_white_check(p_ri))
      return TRUE;

    if ( screened &&
         (blit_quantise_state(p_ri->rb.color) & blit_quantise_mid) != 0) {
      /* Halftoning */
      HQASSERT(!HT_PARAMS_DEGENERATE(p_ri->ht_params),
               "Using a degenerate screen");

      LOCK_HALFTONE(p_ri->ht_params);
      locked = TRUE;
    }


    SET_BLITS(p_ri->rb.blits, BASE_BLIT_INDEX,
              &surface->baseblits[BLT_CLP_NONE],
              &surface->baseblits[BLT_CLP_RECT],
              &surface->baseblits[BLT_CLP_COMPLEX]);

    render_cell_single(p_ri);

    if (locked)
      UNLOCK_HALFTONE(p_ri->ht_params);

  }
  else { /* colored cell. */
    if (screened)
      render_cell_multi_screened(p_ri);
    else {

      SET_BLITS(p_ri->rb.blits, BASE_BLIT_INDEX,
                &surface->baseblits[BLT_CLP_NONE],
                &surface->baseblits[BLT_CLP_RECT],
                &surface->baseblits[BLT_CLP_COMPLEX]);

      render_cell_multi_contone(p_ri);

    }
  }
  return TRUE;
}

/* Log stripped */
