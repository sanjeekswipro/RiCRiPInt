/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:cvdecodes.c(EBDSDK_P.1) $
 * $Id: src:cvdecodes.c,v 1.2.2.1.1.1 2013/12/19 11:25:18 anon Exp $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Decode arrays for mapping COLORVALUE values to input color values;
 * used in the backdrop and for preconverting objects from blend spaces.
 */

#include "core.h"
#include "dlstate.h"
#include "cvdecodes.h"
#include "gs_table.h"
#include "display.h"
#include "swerrors.h"
#include "control.h"

/*
 * Problems with partial paints failing in low-memory because of too much
 * memory being allocated. Biggest alloc is often the cvdecodes array.
 * So add code to allow it to be pre-allocated, avoiding a big alloc during
 * Partial Paint. Pre-alloc is in temp_pool, but not an issue as there is no
 * free for cvdecodes as its just in dl-pool, which normally gets thrown
 * away in total.
 */

static struct {
  void *ptr;
  Bool in_use;
} dec_savs[2];

void fndecodes_init(void)
{
  int i;
  size_t bytes = COLORVALUE_MAX + 2;

  if ( sizeof(float) > sizeof(int32) )
    bytes *= sizeof(float);
  else
    bytes *= sizeof(int32);

  for ( i = 0; i < 2; i++ ) {
    dec_savs[i].in_use = FALSE;
    dec_savs[i].ptr = mm_alloc(mm_pool_temp, bytes,
                               MM_ALLOC_CLASS_IMAGE_DECODES);
  }
}

void fndecodes_term(void)
{
  int i;
  size_t bytes = COLORVALUE_MAX + 2;

  if ( sizeof(float) > sizeof(int32) )
    bytes *= sizeof(float);
  else
    bytes *= sizeof(int32);

  for ( i = 0; i < 2; i++ ) {
    if ( dec_savs[i].ptr ) {
      mm_free(mm_pool_temp, dec_savs[i].ptr, bytes);
      dec_savs[i].ptr = NULL;
    }
  }
}

static void *dec_alloc(DL_STATE *page, long one_size)
{
  int i;

  for ( i = 0; i < 2; i++ ) {
    if ( !dec_savs[i].in_use && dec_savs[i].ptr ) {
      dec_savs[i].in_use = TRUE;
      return dec_savs[i].ptr;
    }
  }
  return dl_alloc(page->dlpools, (COLORVALUE_MAX + 2) * one_size,
                  MM_ALLOC_CLASS_IMAGE_DECODES);
}

/* cvdecodes array no longer being used, so if its in our saved list we can
 * mark it as available for future use
 */
static void dec_not_used(void *ptr)
{
  int i;

  if ( ptr == NULL )
    return;
  for ( i = 0; i < 2; i++ ) {
    if ( dec_savs[i].ptr == ptr ) {
      dec_savs[i].in_use = FALSE;
    }
  }
}

static Bool alloc_fndecodes(DL_STATE *page, Bool subtractive)
{
  HQASSERT((page->cv_fdecodes[subtractive] != NULL)
           == (page->cv_ndecodes[subtractive] != NULL),
           "cv_fdecodes and cv_ndecodes should be both allocated or both null");

  if ( page->cv_fdecodes[subtractive] == NULL ) {
    float *cv_fdecodes;
    int32 *cv_ndecodes;
    float scaledcolor = (float)gsc_scaledColor(page);
    int32 i;

    cv_fdecodes = dec_alloc(page, sizeof(float));
    if ( !cv_fdecodes )
      return error_handler(VMERROR);

    cv_ndecodes = dec_alloc(page, sizeof(int32));
    if ( !cv_ndecodes ) {
      dl_free(page->dlpools, cv_fdecodes, (COLORVALUE_MAX + 2) * sizeof(int32),
              MM_ALLOC_CLASS_IMAGE_DECODES);
      return error_handler(VMERROR);
    }

    page->cv_fdecodes[subtractive] = cv_fdecodes;
    page->cv_ndecodes[subtractive] = cv_ndecodes;

    if ( subtractive ) {
      for ( i = COLORVALUE_MAX; i >= 0; --i ) {
        float decode = COLORVALUE_TO_USERVALUE(i);
        *cv_fdecodes++ = decode;
        *cv_ndecodes++ = ( int32 )( scaledcolor * decode + 0.5f );
      }
    } else {
      for ( i = 0; i <= COLORVALUE_MAX; ++i ) {
        float decode = COLORVALUE_TO_USERVALUE(i);
        *cv_fdecodes++ = decode;
        *cv_ndecodes++ = ( int32 )( scaledcolor * decode + 0.5f );
      }
    }

    /* Store the single value at the end that guarantees to map to white. */
    *cv_fdecodes = 0.0f;
    *cv_ndecodes = 0;
  }

  return TRUE;
}

/** Decodes for backdrops can be shared, because they are an identity
   transformation. I haven't removed them altogether for identity transforms,
   because it's too much disruption. Instead, we create a single look-up, and
   share it between all colorants of all backdrops. */
Bool cv_alloc_decodes(DL_STATE *page, uint32 ncomps, Bool subtractive,
                      float ***cv_fdecodes, int32 ***cv_ndecodes,
                      uint32 *alloc_ncomps)
{
  uint32 i;
  float **fdecodes;
  int32 **ndecodes;

  HQASSERT(page, "No page for backdrop decodes");

  if ( *cv_fdecodes ) {
    if ( *alloc_ncomps == ncomps ) {
      if ( (*cv_fdecodes)[0] != page->cv_fdecodes[subtractive] ) {
        for ( i = 0; i < ncomps; ++i ) {
          (*cv_fdecodes)[i] = page->cv_fdecodes[subtractive];
          (*cv_ndecodes)[i] = page->cv_ndecodes[subtractive];
        }
      }
      return TRUE;
    }
    /* cv_[fn]decodes allocated but ncomps is wrong. */
    cv_free_decodes(page, cv_fdecodes, cv_ndecodes, alloc_ncomps);
  }

  if ( !alloc_fndecodes(page, subtractive) )
    return FALSE;

  fdecodes = dl_alloc(page->dlpools, ncomps * sizeof(float *),
                     MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( !fdecodes )
    return error_handler(VMERROR);

  ndecodes = dl_alloc(page->dlpools, ncomps * sizeof(int32 *),
                      MM_ALLOC_CLASS_IMAGE_CONVERT);
  if ( !ndecodes ) {
    dl_free(page->dlpools, fdecodes, ncomps * sizeof(float *),
            MM_ALLOC_CLASS_IMAGE_CONVERT);
    return error_handler(VMERROR);
  }

  for ( i = 0; i < ncomps; ++i ) {
    fdecodes[i] = page->cv_fdecodes[subtractive];
    ndecodes[i] = page->cv_ndecodes[subtractive];
  }

  *cv_fdecodes = fdecodes;
  *cv_ndecodes = ndecodes;
  *alloc_ncomps = ncomps;

  return TRUE;
}

void cv_free_decodes(DL_STATE *page,
                     float ***cv_fdecodes, int32 ***cv_ndecodes,
                     uint32 *alloc_ncomps)
{
  /* Note, this just frees ncomps array.  The fdecodes and ndecodes arrays are
     cached in DL_STATE. */
  if ( *cv_fdecodes ) {
    dl_free(page->dlpools, (mm_addr_t)(*cv_fdecodes),
            (*alloc_ncomps) * sizeof(float *),
            MM_ALLOC_CLASS_IMAGE_CONVERT);
    (*cv_fdecodes) = NULL;
  }
  if ( *cv_ndecodes ) {
    dl_free(page->dlpools, (mm_addr_t)(*cv_ndecodes),
            (*alloc_ncomps) * sizeof(int32 *),
            MM_ALLOC_CLASS_IMAGE_CONVERT);
    (*cv_ndecodes) = NULL;
  }
  *alloc_ncomps = 0;
}

/* The decodes memory is from the dl pool.  So all we need to do for a reset
   after the dl pool has been destroyed is null the ptrs.  The ncomps arrays
   stored in the imageobject that refer to the page->cv_[fn]decodes are freed by
   the image code. */
void cv_reset_decodes(DL_STATE *page)
{
  int i;

  for ( i = 0; i < 2; i++ ) {
    dec_not_used(page->cv_fdecodes[i]);
    page->cv_fdecodes[i] = NULL;
    dec_not_used(page->cv_ndecodes[i]);
    page->cv_ndecodes[i] = NULL;
  }
}

/* Memory requirement for one set of fdecodes and one set of ndecodes.
   Would need to double this if both additive and subtractive variants
   are required. */
size_t cv_decodes_size(void)
{
  return (COLORVALUE_MAX + 2) * (sizeof(float) + sizeof(int32));
}

/* =============================================================================
* Log stripped */
