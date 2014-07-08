/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imlut.c(EBDSDK_P.1) $
 * $Id: src:imlut.c,v 1.9.1.1.1.1 2013/12/19 11:25:26 anon Exp $
 *
 * Copyright (C) 2009-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 *
 * imlut - the image LUT cache
 *
 * Image LUTs are tables that map image data from one colour space to another.
 * They are created and used by the image expander (imexpand.c).  Once a LUT has
 * been created by the image expander, the expander hands over ownership of the
 * LUT (i.e. it doesn't need to worry about freeing the LUT) to the imlut cache,
 * allowing the LUT to be reused for other images later on.
 *
 * The imlut entries are chained together by two linked-lists.  A complete list
 * of entries hangs of the DL_STATE, but this is currently used only for debug
 * info.
 *
 * The second list hangs of a colour cache entry.  A colour chain has a set
 * colour IDs (CLIDs) associated with it.  Having the colour cache refer to a
 * sub-set of image LUTs means that all of those image LUTs are applicable to
 * one colour chain (i.e. they have the same c-links on the chain etc.).  This
 * means most of the work of finding an imlut entry is done by the colour cache.
 * In addition, images can have different bits per pixel and different decode
 * arrays, so a colour cache may refer to more than one image LUT.
 *
 * The decode array is the original decode array from the image dictionary.  The
 * decode array is copied and compared against for an imlut match.  For images
 * that are being adjusted there is no decode array dictionary entry.  Either
 * the decodes are derived from the input colour space (and that must match by
 * virtue of the CLIDs matching) or the decodes are derived from the image LUT
 * that we are about to adjust (make a new LUT).  In these cases a simple ptr
 * equality test is sufficient.
 *
 * Image LUTs are allocated in dl pool memory and are preserved across partial
 * paints.  At the end of the page the dl pool is dropped and the image LUTs are
 * gone.  We don't need to run through the main list freeing imlut entries.
 */

#include "core.h"
#include "imlut.h"
#include "swerrors.h"
#include "mm.h"
#include "dlstate.h"
#include "display.h"
#include "gs_cache.h"
#include "hqmemcmp.h"
#include "gschead.h"
#include "params.h"             /* UserParams */

#if defined( DEBUG_BUILD )
#include "monitor.h"
#endif

struct imlut_t {
  /** Bits per pixel and image decodes can vary independently from CLIDs,
      and therefore must be checked before making a match. */
  int32   bpp;                /** Bits per pixel. */
  int32   ncomps;             /** Number of input components. */
  float   *decode_array;      /** 2 * ncomps length, or NULL. */
  void    *decode_for_adjust; /** decode for image adjustment
                                  (just compare ptrs), or NULL. */
  Bool    expanded;           /** An expanded LUT variant. */

  /** A chunk of memory known as a LUT for mapping image data from one colour
      space to another.  The LUT is created and used elsewhere (imexpand.c). */
  uint8   **lut;

#if defined( DEBUG_BUILD )
  uint32  hits;
#endif

  /** A linked-list of imluts with the same CLIDs, but different bpp, decodes etc. */
  imlut_t *next_in_group;

  /** A linked-list of all imluts. */
  imlut_t *next;
};

static void imlut_free(DL_STATE *page, imlut_t *imlut)
{
  if ( imlut->decode_array ) {
    dl_free(page->dlpools, imlut->decode_array, 2 * imlut->ncomps * sizeof(float),
            MM_ALLOC_CLASS_IMAGE_TABLES);
  }
  dl_free(page->dlpools, imlut, sizeof(imlut_t),
          MM_ALLOC_CLASS_IMAGE_TABLES);
}

void imlut_destroy(DL_STATE *page)
{
#if defined( DEBUG_BUILD )
  static Bool print_stats = FALSE;

  if ( print_stats ) {
    imlut_t *imlut;

    for ( imlut = page->image_lut_list; imlut; imlut = imlut->next ) {
      monitorf((uint8*)"imlut 0x%X, hits %d, bpp %d, ncomps %d\n",
               imlut, imlut->hits, imlut->bpp, imlut->ncomps);
    }
  }
#endif

  /* imluts are retained across partial paints and at the
     end of the page the pool is cleared - so no free required! */
  page->image_lut_list = NULL;
}

static void imlut_new(DL_STATE *page,
                      float *decode_array, void *decode_for_adjust,
                      int32 ncomps, int32 bpp, Bool expanded,
                      uint8 **lut, imlut_t **new_imlut)
{
  Bool result = FALSE;
  const imlut_t blank = { 0 };
  imlut_t *imlut;

  *new_imlut = NULL;

  imlut = dl_alloc(page->dlpools, sizeof(imlut_t),
                   MM_ALLOC_CLASS_IMAGE_TABLES);
  if ( imlut == NULL )
    return;

  *imlut = blank;
  imlut->bpp = bpp;
  imlut->ncomps = ncomps;
  imlut->decode_for_adjust = decode_for_adjust;
  imlut->expanded = expanded;
  imlut->lut = lut;
#if defined( DEBUG_BUILD )
  imlut->hits = 1;
#endif

#define return DO_NOT_return_goto_cleanup_INSTEAD!

  if ( decode_array ) {
    int32 i;

    imlut->decode_array = dl_alloc(page->dlpools, 2 * ncomps * sizeof(float),
                                   MM_ALLOC_CLASS_IMAGE_TABLES);
    if ( !imlut->decode_array )
      goto cleanup;

    for ( i = 2 * ncomps - 1; i >= 0; --i ) {
      imlut->decode_array[i] = decode_array[i];
    }
  }

  *new_imlut = imlut;

  result = TRUE;

 cleanup:
  if ( !result )
    /* Failure is not an error, just tidy up */
    imlut_free(page, imlut);
#undef return
  return;
}

static Bool imlut_match(imlut_t *imlut,
                        float *decode_array, void *decode_for_adjust,
                        int32 ncomps, int32 bpp, Bool expanded)
{
  /* We know CLIDs match already, so it's only bpp and decode_array that
     actually need testing. */

  if ( imlut->bpp != bpp ||
       imlut->ncomps != ncomps ||
       (imlut->decode_array != NULL) != (decode_array != NULL) ||
       imlut->decode_for_adjust != decode_for_adjust ||
       imlut->expanded != expanded )
    return FALSE;

  if ( decode_array ) {
    int32 i;

    for ( i = 2 * ncomps - 1; i >= 0; --i ) {
      if ( fabs(imlut->decode_array[i] - decode_array[i]) > 0.00001 )
        return FALSE;
    }
  }

  return TRUE;
}

Bool imlut_lookup(GS_COLORinfo *colorInfo, int32 colorType,
                  float *decode_array, void *decode_for_adjust,
                  int32 ncomps, int32 bpp, Bool expanded,
                  uint8 ***lut)
{
  imlut_t *imlut = NULL;

  /* If there is no decodes array then we can't be sure it's
     safe to re-use a lut. */
  if ( decode_array || decode_for_adjust ) {

    /* Look for an existing imlut with matching CLIDs, decode, bpp etc.
       All the imluts on the 'next_in_group' list have matching CLIDs. */
    for ( imlut = coc_imlut_get(colorInfo, colorType);
          imlut && !imlut_match(imlut, decode_array, decode_for_adjust,
                                ncomps, bpp, expanded);
          imlut = imlut->next_in_group ) {
      EMPTY_STATEMENT();
    }
  }

  if ( lut )
    *lut = imlut ? imlut->lut : NULL;

#if defined( DEBUG_BUILD )
  if ( imlut && lut )
    imlut->hits += 1;
#endif

  return imlut ? TRUE : FALSE;
}

void imlut_add(DL_STATE *page,
               GS_COLORinfo *colorInfo, int32 colorType,
               float *decode_array, void *decode_for_adjust,
               int32 ncomps, int32 bpp, Bool expanded,
               uint8 **lut, Bool *added)
{
  imlut_t *imlut;
  imlut_t *orig_group_imlut;

  *added = FALSE;

  HQASSERT(!imlut_lookup(colorInfo, colorType, decode_array, decode_for_adjust,
                         ncomps, bpp, expanded, NULL),
           "Shouldn't be trying to add an image LUT that has already been stored");

  orig_group_imlut = coc_imlut_get(colorInfo, colorType);

  imlut_new(page, decode_array, decode_for_adjust,
            ncomps, bpp, expanded, lut, &imlut);
  /* Failure to allocate an imlut is not an error */
  if ( imlut == NULL )
    return;

  if ( !coc_imlut_set(colorInfo, colorType, imlut) ) {
    /* Failure to allocate an a color cache is not an error */
    imlut_free(page, imlut);
    return;
  }

  /* This imlut is put at the head of the page list */
  imlut->next = page->image_lut_list;
  page->image_lut_list = imlut;

  /* And at the head of imlut group for the color chain */
  imlut->next_in_group = orig_group_imlut;

  *added = TRUE;

  return;
}

/* Log stripped */
