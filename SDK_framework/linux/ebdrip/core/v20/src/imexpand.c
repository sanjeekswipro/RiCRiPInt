/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imexpand.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image expanders.
 */

#include "core.h"
#include "coreinit.h"
#include "imexpand.h"
#include "swerrors.h"
#include "swstart.h" /* SwThreadIndex() */
#include "mm.h"
#include "caching.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "hqbitvector.h"
#include "objnamer.h"
#include "often.h"
#include "gstate.h"
#include "render.h" /* RENDER_REGIONS_DIRECT */
#include "bitbltt.h" /* BLIT_ALIGN_UP etc */
#include "bitblts.h" /* GET_BLIT_DATA */
#include "blitcolors.h"
#include "blitcolorh.h"
#include "display.h" /* LISTOBJECT */
#include "params.h"
#include "ripmulti.h" /* NUM_THREADS */
#include "imstore.h"
#include "imageo.h" /* IMAGEOBJECT */
#include "imlut.h"
#include "cvcolcvt.h"
#include "surface.h" /* surface_set_t */
#include "gu_chan.h" /* guc_backdropRasterStyle */
#include "gschtone.h" /* gsc_getSpotno */
#include "color.h" /* ht_applyTransform */
#include "control.h" /* interrupts_clear */
#include "halftone.h"  /* ht_allocateForm */
#include "imcontext.h"
#include "interrupts.h"
#include "trap.h" /* isTrappingActive */
#include "metrics.h"
#include "gscfastrgb2gray.h"


/* The maximum number of bits in a normal image LUT == 4096 entries */
#define MAX_LUT_BITS      (12)

/* -------------------------------------------------------------------------- */
#define IM_EXPBUF_NAME "Image expansion buffer"

#define EXPBUF_N_COMPS (4)

struct IM_EXPBUF {
  uint8 *buf ;           /**< Buffer in which to store expanded data */
  int32 size ;           /**< Size of buffer */
  int32 refcnt ;         /**< Number of images using this expansion buffer */
  IM_EXPAND *ime ;       /**< Image currently using this expansion buffer */
  int32 x , y , n ;
  Bool otf;              /**< on-the-fly converted? */
  unsigned int expanded_comps ; /**< Last mapping for expanded comps. */
  int expanded_to_plane[EXPBUF_N_COMPS] ; /**< Last mapping for expanded to planes */

  OBJECT_NAME_MEMBER
} ;

/* On-the-fly conversion data */
typedef struct {
  IM_EXPAND *ime;      /**< Alternate ime containing a LUT for on-the-fly conversion */
  CV_COLCVT *converter; /**< Color conversion object for on-the-fly conversion (non-LUT case) */
  COLORANTINDEX *output_colorants; /**< Output colors from an on-the-fly conversion */
  uint16 planes;       /**< Number of planes from an on-the-fly conversion */
  uint8 obpp;          /**< On-the-fly converted output bits per pixel */
  int32 method;        /**< Conversion method; color transform or just quantising */
} IM_EXPAND_OTF;

#define IM_EXPAND_NAME "Image expander"

struct IM_EXPAND {
  uint8 ibpp ;           /**< Input bits per pixel (12 maybe packed into 16). */
  uint8 orig_ibpp ;      /**< Original input bits per pixel (12 is stored as 12). */
  uint8 obpp ;           /**< Output bits per pixel. */
  uint8 expibpp ;        /**< Expanded input bits per pixel. */
  uint8 expobpp ;        /**< Expanded output bits per pixel. */

  uint16 iplanes ;       /**< Input number of planes. */
  uint16 oplanes ;       /**< Output number of planes. */

  uint16 lplanes ;       /**< Number of planes in LUT and cis arrays. */
  uint16 alloc_lplanes ; /**< Number of planes allocated for LUT and cis (an extra plane for alpha). */
  uint8 presepExpbuf ;   /**< Indicates if a recombined expbuf needs enlarging prior to rendering. */
  uint8 format ;         /**< Format of expansion data. */

  dl_erase_nr dl;        /**< Erase number of the DL. */
  COLORANTINDEX *cis ;   /**< Array of colorant indices. */

  uint8 paddata ;        /**< May need to pad components into a container. */

  uint8 is_mask ;        /**< Flag set to true iff this is an expander for an imagemask */

  IM_EXPAND *ime_direct ; /**< An alternate ime for direct regions. */
  IM_EXPAND_OTF otf ;    /**< Or, use on-the-fly conversion for direct regions instead. */

  IM_STORE *ims_alpha ;  /**< alpha channel image store */
  mm_size_t lutsize ;    /**< Size of single lut. */
  Bool own_expluts ;     /**< Image LUT memory owned by this ime - needs to be freed. */
  uint8 **expluts ;      /**< LUT to be used in expansion (may or may not be owned by ime). */
  IM_EXPBUF *expbuf ;    /**< Expansion buffer in which to store expanded data. */

  OBJECT_NAME_MEMBER
} ;

/* -------------------------------------------------------------------------- */
/** \todo ajcd 2008-10-31: These tables should move to core/tables. They are
    endian-versions of nibble-to-uint32 expansion tables, which are useful
    elsewhere. */
static uint32 im_elutP[ 16 ] = {
#ifdef highbytefirst
  0x00000000 , 0x00000001 , 0x00000100 , 0x00000101 ,
  0x00010000 , 0x00010001 , 0x00010100 , 0x00010101 ,
  0x01000000 , 0x01000001 , 0x01000100 , 0x01000101 ,
  0x01010000 , 0x01010001 , 0x01010100 , 0x01010101
#else
  0x00000000 , 0x01000000 , 0x00010000 , 0x01010000 ,
  0x00000100 , 0x01000100 , 0x00010100 , 0x01010100 ,
  0x00000001 , 0x01000001 , 0x00010001 , 0x01010001 ,
  0x00000101 , 0x01000101 , 0x00010101 , 0x01010101
#endif
} ;

static uint32 *im_elutsP[] =
{
  im_elutP
} ;

/** \todo ajcd 2008-10-31: These tables should move to core/tables. They are
    endian-versions of nibble-to-uint32 expansion tables, which are useful
    elsewhere. */
static uint32 im_elutN[ 16 ] = {
#ifdef highbytefirst
  0x01010101 , 0x01010100 , 0x01010001 , 0x01010000 ,
  0x01000101 , 0x01000100 , 0x01000001 , 0x01000000 ,
  0x00010101 , 0x00010100 , 0x00010001 , 0x00010000 ,
  0x00000101 , 0x00000100 , 0x00000001 , 0x00000000
#else
  0x01010101 , 0x00010101 , 0x01000101 , 0x00000101 ,
  0x01010001 , 0x00010001 , 0x01000001 , 0x00000001 ,
  0x01010100 , 0x00010100 , 0x01000100 , 0x00000100 ,
  0x01010000 , 0x00010000 , 0x01000000 , 0x00000000
#endif
} ;

static uint32 *im_elutsN[] =
{
  im_elutN
} ;

/* 8 bit imagemasks are created by PCL.
   The only interesting values are index 0 and 255. */
static uint8 im_imask8_reverse_polarity_lut[ 256 ] = {
  0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static uint8 *im_imask8_reverse_polarity[] =
{
  im_imask8_reverse_polarity_lut
} ;

/* -------------------------------------------------------------------------- */
static Bool im_expand_alloc_expluts(DL_STATE *page, IM_EXPAND *ime,
                                    int32 splanes);

static void  im_lutexpand( uint8 **expluts , int32 oncomps , Bool out16 ,
                           int32 expibpp , int32 ebpp , int32 lbpp ) ;

static Bool im_lutgen(DL_STATE *page, IM_EXPAND *ime,
                      float *fdecodes[], int32 *ndecodes[],
                      float *decode_array, void *decode_for_adjust,
                      Bool independent,
                      int32 incomps, int32 ibpp , Bool fixedpt16,
                      int32 method, int32 oncomps, Bool out16,
                      int32 splanes, int32 plane, Bool isSoftMask,
                      int32 expibpp, int32 ebpp, int32 lbpp,
                      GS_COLORinfo *colorInfo, int32 colorType);

static Bool im_lutgenpresep(IM_EXPAND *ime,
                            GS_COLORinfo *colorInfo ,
                            int32 colorType ,
                            float *decodes[] ,
                            int32 ibpp ,
                            uint8 **expluts ,
                            int32 plane ) ;

static Bool im_lutgennormal(DL_STATE *page, IM_EXPAND *ime,
                            GS_COLORinfo *colorInfo, int32 colorType,
                            float *fdecodes[], int32 *ndecodes[],
                            int32 incomps, int32 ibpp, Bool fixedpt16,
                            int32 method, int32 ebpp, Bool out16,
                            Bool independent, uint8 **expluts, int32 expibpp);

static int32 im_getci(const IM_EXPAND *ime , COLORANTINDEX ci) ;

static Bool im_expand1to1b(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x , int32 y , int32 n ,
                            int plane) ;
static Bool im_expand1to1f(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x , int32 y , int32 n ,
                            int plane) ;

static Bool im_expand1to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                          int32 x, int32 y, int32 n,
                          int plane, unsigned int offset,
                          unsigned int expanded_comps, corecontext_t *context) ;
static Bool im_expand1to8x4(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x, int32 y, int32 n,
                            int plane, unsigned int offset,
                            unsigned int expanded_comps, corecontext_t *context) ;
static Bool im_expand1to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x, int32 y, int32 n,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps) ;
static Bool im_expand1to16x2(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                             int32 x, int32 y, int32 n,
                             int plane, unsigned int offset,
                             unsigned int expanded_comps) ;

static Bool im_expand2to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                          int32 x, int32 y, int32 n,
                          int plane, unsigned int offset,
                          unsigned int expanded_comps) ;
static Bool im_expand2to8x4(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x, int32 y, int32 n,
                            int plane, unsigned int offset,
                            unsigned int expanded_comps) ;
static Bool im_expand2to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x, int32 y, int32 n,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps) ;
static Bool im_expand2to16x2(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                             int32 x, int32 y, int32 n,
                             int plane, unsigned int offset,
                             unsigned int expanded_comps) ;

static Bool im_expand4to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                          int32 x, int32 y, int32 n,
                          int plane, unsigned int offset,
                          unsigned int expanded_comps) ;
static Bool im_expand4to8x2(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x, int32 y, int32 n,
                            int plane, unsigned int offset,
                            unsigned int expanded_comps) ;
static Bool im_expand4to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x, int32 y, int32 n,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps) ;
static Bool im_expand4to16x2(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                             int32 x, int32 y, int32 n,
                             int plane, unsigned int offset,
                             unsigned int expanded_comps) ;

static Bool im_expand8to8nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                               int32 x, int32 y, int32 n,
                               int plane, unsigned int offset,
                               unsigned int expanded_comps, corecontext_t *context) ;
static Bool im_expand8to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                          int32 x, int32 y, int32 n,
                          int plane, unsigned int offset,
                          unsigned int expanded_comps, corecontext_t *context) ;

static Bool im_expand8to16nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                int32 x, int32 y, int32 n,
                                int plane, unsigned int offset,
                                unsigned int expanded_comps) ;
static Bool im_expand8to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x, int32 y, int32 n,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps) ;

static Bool im_expand16to8nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                int32 x, int32 y, int32 n,
                                int plane, unsigned int offset,
                                unsigned int expanded_comps) ;
static Bool im_expand16to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x, int32 y, int32 n,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps) ;
static Bool im_expand16to16nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                 int32 x, int32 y, int32 n,
                                 int plane, unsigned int offset,
                                 unsigned int expanded_comps) ;
static Bool im_expand16to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x, int32 y, int32 n,
                            int plane, unsigned int offset,
                            unsigned int expanded_comps) ;

static Bool im_expand32to8nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                int32 x, int32 y, int32 n,
                                int plane, unsigned int offset,
                                unsigned int expanded_comps) ;
static Bool im_expand32to16nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                 int32 x, int32 y, int32 n,
                                 int plane, unsigned int offset,
                                 unsigned int expanded_comps) ;


#define IM_EXPAND_INVALID       (-1)


#if defined(METRICS_BUILD)


/** Total size of reused expansion buffers, in bytes. */
static size_t reused_expansions;


static Bool im_expand_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Images")) )
    return FALSE;
  SW_METRIC_INTEGER("reused_expansions",
                    CAST_SIZET_TO_INT32(reused_expansions));
  sw_metrics_close_group(&metrics);
  return TRUE;
}

static void im_expand_metrics_reset(int reason)
{
  UNUSED_PARAM(int, reason);

  reused_expansions = 0;
}

static sw_metrics_callbacks im_expand_metrics_hook = {
  im_expand_metrics_update, im_expand_metrics_reset, NULL
};


#endif /* METRICS_BUILD */


/*---------------------------------------------------------------------------*/

static inline IM_EXPBUF *expand_expbuf(IM_EXPAND *ime, corecontext_t *context)
{
  IM_EXPBUF *imb ;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);

  imb = context->im_context->expbuf ;
  if ( imb == NULL )
    imb = ime->expbuf ;

  VERIFY_OBJECT(imb, IM_EXPBUF_NAME);

  return imb ;
}

static inline void claim_expand_buffer(IM_EXPAND *ime, IM_EXPBUF *imb)
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  VERIFY_OBJECT(imb, IM_EXPBUF_NAME);

  imb->ime = ime ;
}

static Bool reuse_expand_buffer(IM_EXPAND *ime, IM_EXPBUF *imb,
                                int32 x, int32 y, int32 n, Bool otf,
                                const int expanded_to_plane[],
                                unsigned int comps)
{
  size_t i;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  VERIFY_OBJECT(imb, IM_EXPBUF_NAME);

  HQASSERT(imb->buf != NULL &&
           imb->refcnt > 0 &&
           (!IS_INTERPRETER() || imb->refcnt > 1 || imb->ime == ime) &&
           (imb->size >=
             (((((n * ime->ibpp + 7 ) / 8 ) * 8 ) /
               ime->expibpp ) * ime->expobpp ) / 8) &&
           (IS_INTERPRETER() || CoreContext.page->im_bufsize >= n),
           "Expand buffer error");
  if ( imb->ime == ime
       && imb->expanded_comps == comps && comps <= EXPBUF_N_COMPS
       && imb->x  == x && imb->y  == y
       && imb->n  == n && imb->otf == otf ) {
    for ( i = 0 ; i < comps ; ++i )
      if ( expanded_to_plane[i] >= 0
           && imb->expanded_to_plane[i] != expanded_to_plane[i] )
        break;
    if ( i == comps ) { /* all match, reuse current buffer */
#if defined(METRICS_BUILD)
      reused_expansions += imb->size;
#endif
      return TRUE;
    }
  }
  imb->x = x; imb->y = y;
  imb->n = n;
  imb->otf = otf;
  imb->expanded_comps = comps;
  if ( comps <= EXPBUF_N_COMPS )
    for ( i = 0 ; i < comps ; ++i )
      imb->expanded_to_plane[i] = expanded_to_plane[i];
  /* Note: don't store ime here, as we need it to see if we do the clear. */
  return FALSE;
}


/* -------------------------------------------------------------------------- */

/** Split a 32-bit LUT value into two 16-bit values, and store in the
    destination locations separated by stride. */
static inline void split_32_to_16x2(uint32 val, uint16 *dst, unsigned int stride)
{
#ifdef highbytefirst
  dst[0] = (uint16)(val >> 16) ;
  dst[stride] = (uint16)val ;
#else
  dst[0] = (uint16)val ;
  dst[stride] = (uint16)(val >> 16) ;
#endif
}

/** Split a 32-bit LUT value into four 8-bit values, and store in the
    destination locations separated by stride. */
static inline void split_32_to_8x4(uint32 val, uint8 *dst, unsigned int stride)
{
#ifdef highbytefirst
  dst[0] = (uint8)(val >> 24) ;
  dst += stride ;
  dst[0] = (uint8)(val >> 16) ;
  dst += stride ;
  dst[0] = (uint8)(val >> 8) ;
  dst += stride ;
  dst[0] = (uint16)val ;
#else
  dst[0] = (uint8)val ;
  dst += stride ;
  dst[0] = (uint8)(val >> 8) ;
  dst += stride ;
  dst[0] = (uint8)(val >> 16) ;
  dst += stride ;
  dst[0] = (uint8)(val >> 24) ;
#endif
}

/** Split a 16-bit LUT value into two 8-bit values, and store in the
    destination locations separated by stride. */
static inline void split_16_to_8x2(uint16 val, uint8 *dst, unsigned int stride)
{
#ifdef highbytefirst
  dst[0] = (uint8)(val >> 8) ;
  dst[stride] = (uint8)val ;
#else
  dst[0] = (uint8)val ;
  dst[stride] = (uint8)(val >> 8) ;
#endif
}

/* -------------------------------------------------------------------------- */

void im_expand_colorinfo_free(DL_STATE *page, Bool otf_possible)
{
  IMAGEOBJECT *image;

  /* Early bail-out if there aren't any colorInfos to free. */
  if (!otf_possible)
    return;

  /* The on-the-fly converter object contains a colorInfo copy and this must be
     removed from the colorInfo global list before the dl pool is destroyed. */
  for ( image = page->im_list; image; image = image->next ) {
    IM_EXPAND *ime = image->ime;
    if ( ime && ime->otf.converter )
      cv_colcvtfree(page, &ime->otf.converter);
  }
}


void im_expanderase(DL_STATE *page)
{
  /* It's not possible to know if on-the-fly is on from some clients. */
  Bool otf_possible = TRUE;
  im_expand_colorinfo_free(page, otf_possible);
  page->im_bufsize = 0;
  page->im_expbuf_shared = NULL;
}


IM_EXPBUF **im_createexpbuf(DL_STATE *page, Bool multi_thread)
{
  int32 bufsize ;
  unsigned int t;
  IM_EXPBUF **expbufs;

  bufsize = page->im_bufsize ;

  expbufs = ( IM_EXPBUF** )mm_alloc( mm_pool_temp,
                                     sizeof(IM_EXPBUF*) * NUM_THREADS(),
                                     MM_ALLOC_CLASS_IMAGE_EXPBUF ) ;
  if ( expbufs == NULL ) {
    (void)error_handler( VMERROR );
    return NULL;
  }

  /* N.B. Clear all possible expanders. Testing whether these are NULL
     determines whether the image's expander buffer is used. */
  for ( t = 0; t < NUM_THREADS(); ++t )
    expbufs[t] = NULL;

  if ( multi_thread && bufsize > 0 ) {
    for ( t = 1; t < NUM_THREADS(); ++t ) {
      IM_EXPBUF *expbuf;
      size_t i;

      expbuf = ( IM_EXPBUF * )mm_alloc( mm_pool_temp,
                                        sizeof(IM_EXPBUF) + bufsize,
                                        MM_ALLOC_CLASS_IMAGE_EXPBUF);
      if ( expbuf == NULL ) {
        im_freeexpbuf(page, expbufs);
        (void)error_handler( VMERROR );
        return NULL;
      }
      expbuf->buf    = (uint8 *)BLIT_ALIGN_UP(expbuf + 1);
      expbuf->size   = bufsize;
      expbuf->refcnt = 1;
      expbuf->ime    = NULL;
      expbuf->x      = IM_EXPAND_INVALID;
      expbuf->y      = IM_EXPAND_INVALID;
      expbuf->n      = IM_EXPAND_INVALID;
      expbuf->otf    = IM_EXPAND_INVALID;
      expbuf->expanded_comps = 0;
      for ( i = 0 ; i < EXPBUF_N_COMPS ; ++i )
        expbuf->expanded_to_plane[i] = -1;
      NAME_OBJECT(expbuf, IM_EXPBUF_NAME) ;
      expbufs[t] = expbuf;
    }
  }

  return expbufs;
}


void im_freeexpbuf(DL_STATE *page, IM_EXPBUF **expbufs)
{
  HQASSERT( expbufs, "expbufs is NULL in im_freeexpbuf" ) ;

  if ( page->im_bufsize > 0 ) {
    unsigned int t;

    for ( t = 1; t < NUM_THREADS(); ++t ) {
      if (expbufs[t] != NULL) {
        VERIFY_OBJECT(expbufs[t], IM_EXPBUF_NAME);
        HQASSERT(expbufs[t]->size == page->im_bufsize,
                 "expbuf is different to size required in im_freeexpbuf" ) ;
        UNNAME_OBJECT(expbufs[t]) ;
        mm_free(mm_pool_temp, expbufs[t],
                sizeof(IM_EXPBUF) + expbufs[t]->size);
      }
    }
  }
  mm_free(mm_pool_temp, expbufs, sizeof(IM_EXPBUF *) * NUM_THREADS());
}


static Bool im_expbuf_enlarge(IM_EXPAND* ime, DL_STATE *page, int32 bufsize)
{
  IM_EXPBUF* expbuf;
  size_t i;

  VERIFY_OBJECT(ime->expbuf, IM_EXPBUF_NAME); /* because it will be freed */

  expbuf = (IM_EXPBUF*)dl_alloc(page->dlpools,
                                sizeof(IM_EXPBUF) + bufsize,
                                MM_ALLOC_CLASS_IMAGE_EXPBUF);
  if (expbuf == NULL)
    return error_handler(VMERROR);

  expbuf->buf    = (uint8 *)BLIT_ALIGN_UP(expbuf + 1) ;
  expbuf->size   = bufsize ;
  expbuf->refcnt = ime->expbuf->refcnt ;
  expbuf->ime    = ime ;

  /* Clear the "duplicate line" expansion cache. */
  expbuf->x = expbuf->y = expbuf->n = IM_EXPAND_INVALID ;
  for ( i = 0 ; i < EXPBUF_N_COMPS ; ++i )
    expbuf->expanded_to_plane[i] = -1;

  NAME_OBJECT(expbuf, IM_EXPBUF_NAME) ;

  /* Accumulate the size of the largest expansion buffer. */
  if ( bufsize > page->im_bufsize )
    page->im_bufsize = bufsize ;

  if ( ime->expbuf->refcnt > 1 ) {
    /* Old expbuf was shared, fix up the shared references
       to point to the new larger expbuf. */
    IMAGEOBJECT *image;
    for ( image = page->im_list ; image != NULL ; image = image->next ) {
      /* image->ime can be null for backdrops */
      if ( image->ime != NULL && image->ime != ime
           && image->ime->expbuf == ime->expbuf )
        image->ime->expbuf = expbuf;
    }
  }

  dl_free(page->dlpools, ime->expbuf,
          sizeof(IM_EXPBUF) + ime->expbuf->size, MM_ALLOC_CLASS_IMAGE_EXPBUF);
  ime->expbuf = expbuf;
  return TRUE;
}


/* -------------------------------------------------------------------------- */
/* Imagemasks are 1 bpp, but we allow an 8 bit mask option for daft PCL
   imagemask-like images. */
IM_EXPAND *im_expandopenimask(DL_STATE *page, int32 width, int32 height,
                              int32 ibpp, Bool polarity)
{
  int32 bufsize ;
  IM_EXPBUF *expbuf ;
  IM_EXPAND *ime ;
  IM_EXPAND_OTF otf_init = {0};
  size_t i;

  UNUSED_PARAM( int32 , height ) ;

  HQASSERT( width > 0 , "width should be > 0" ) ;
  HQASSERT( height > 0 , "height should be > 0" ) ;
  HQASSERT(ibpp == 1 || ibpp == 8, "ibpp must be 0 or 8 only");

  ime = dl_alloc(page->dlpools, sizeof(IM_EXPAND),
                 MM_ALLOC_CLASS_IMAGE_EXPAND) ;
  if ( ime == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  bufsize = ( width + 7 ) & ~7 ;
  /* Ensure extra byte for imgfillh/rle/rlecolor algorithms. */
  bufsize += 1 ;
  /* Ensure enough extra bytes to blit align the expander buffer, and to have
     a full complement of blit_t words in it. */
  bufsize = BLIT_ALIGN_SIZE(bufsize) + BLIT_MASK_BYTES ;

  expbuf = dl_alloc(page->dlpools, sizeof(IM_EXPBUF) + bufsize,
                    MM_ALLOC_CLASS_IMAGE_EXPBUF) ;
  if ( expbuf == NULL ) {
    dl_free(page->dlpools, ime, sizeof(IM_EXPAND),
            MM_ALLOC_CLASS_IMAGE_EXPAND);
    (void)error_handler(VMERROR);
    return NULL;
  }

  /* Now fill in all of our structure. */
  ime->ibpp      = CAST_SIGNED_TO_UINT8(ibpp) ;
  ime->orig_ibpp = CAST_SIGNED_TO_UINT8(ibpp) ;
  ime->obpp      = 8 ;
  if ( ibpp == 1 ) {
    ime->expibpp = 4 ; /* Since we want to expand optimally. */
    ime->expobpp = 32 ;
  } else {
    /* Image store data is already in the right format - don't expand. */
    ime->expibpp = 8 ;
    ime->expobpp = 8 ;
  }

  ime->iplanes = 1 ;
  ime->oplanes = 1 ;

  ime->lplanes = 1 ;
  ime->alloc_lplanes = 1 ;

  ime->presepExpbuf = FALSE ;
  ime->dl = page->eraseno;

  /* We always set the cis array, for masks it is a single channel with
     colorant index /All. */
  ime->cis = dl_alloc(page->dlpools, sizeof(COLORANTINDEX),
                      MM_ALLOC_CLASS_IMAGE_TABLES);
  if (ime->cis == NULL) {
    dl_free(page->dlpools, expbuf, sizeof(IM_EXPBUF) + bufsize,
            MM_ALLOC_CLASS_IMAGE_EXPBUF) ;
    dl_free(page->dlpools, ime, sizeof(IM_EXPAND),
            MM_ALLOC_CLASS_IMAGE_EXPAND);
    (void)error_handler(VMERROR);
    return NULL ;
  }

  ime->cis[0] = COLORANTINDEX_ALL ;

  ime->format  = ime_planar ;
  ime->paddata = FALSE ;

  ime->is_mask = TRUE ;

  ime->ims_alpha = NULL ;

  if ( ibpp == 1 ) {
    ime->lutsize = sizeof( im_elutP ) ;
    ime->own_expluts = FALSE;
    ime->expluts = ( uint8 ** )( polarity ? im_elutsP : im_elutsN ) ;
  } else {
    /* 8 bit image mask case for PCL.  If polarity is false a LUT
       is required to flip the polarity. */
    if ( polarity ) {
      ime->lutsize = 0 ;
      ime->own_expluts = FALSE ;
      ime->expluts = NULL ;
    } else {
      ime->lutsize = sizeof(im_imask8_reverse_polarity_lut) ;
      ime->own_expluts = FALSE ;
      ime->expluts = im_imask8_reverse_polarity ;
    }
  }

  ime->ime_direct = NULL;
  ime->otf = otf_init;

  ime->expbuf    = expbuf ;

  NAME_OBJECT(ime, IM_EXPAND_NAME) ;

  expbuf->buf    = (uint8 *)BLIT_ALIGN_UP(expbuf + 1) ;
  expbuf->size   = bufsize ;
  expbuf->refcnt = 1 ;
  expbuf->ime    = ime ;

  /* Clear the "duplicate line" expansion cache. */
  expbuf->x = expbuf->y = expbuf->n = IM_EXPAND_INVALID ;
  for ( i = 0 ; i < EXPBUF_N_COMPS ; ++i )
    expbuf->expanded_to_plane[i] = -1;

  NAME_OBJECT(expbuf, IM_EXPBUF_NAME) ;

  if ( bufsize > page->im_bufsize )
    page->im_bufsize = bufsize ;
  return ime ;
}

/* Create an image expander for the specified source image data.

The passed label will be used to label pixels in those expanders which
produce labelled data when the source data is not itself labelled.
*/
IM_EXPAND *im_expandopenimage(DL_STATE *page, float *fdecodes[], int32 *ndecodes[] ,
                              float *decode_array, void *decode_for_adjust,
                              int32 decodes_incomps, Bool independent,
                              int32 width , int32 height ,
                              int32 incomps , int32 ibpp , Bool fixedpt16,
                              int32 oncomps , Bool out16,
                              Bool interleaved ,  /* pixel interleaved output */
                              int32 plane ,
                              GS_COLORinfo *colorInfo, int32 colorType ,
                              int32 imageType , int32 method ,
                              Bool isSoftMask ,
                              Bool allowLuts, Bool allowInterleavedLuts,
                              uint8 defaultFormat)
{
  GUCR_RASTERSTYLE *hRasterStyle = gsc_getTargetRS(colorInfo);
  int32 ebpp ;  /* input bpp (all colorants) iff pixel interleaved LUT */
  int32 lbpp ;
  int32 obpp ;
  int32 expibpp ;
  int32 expobpp ;
  int32 encomps ;
  mm_size_t lutsize ;
  int32 bufsize ;
  int32 splanes ;
  int32 lplanes ;
  Bool useluts ;
  uint8 format ;
  uint8 paddata ;
  IM_EXPAND *ime  ;
  IM_EXPAND_OTF otf_init = {0};

  UNUSED_PARAM(int32, height);

  HQASSERT(decodes_incomps == 1 || decodes_incomps == incomps,
           "Unexpected decodes_incomps value");
  HQASSERT(width > 0 , "width should be > 0");
  HQASSERT(height > 0 , "height should be > 0");
  HQASSERT(incomps > 0 , "incomps should be > 0");
  /* Now allow bpp==32 as a debugging aid */
  HQASSERT(ibpp == 1 || ibpp == 2 || ibpp == 4 || ibpp == 8 || ibpp == 12 ||
           ibpp == 16 || ibpp == 32, "ibpp is an unrecognized value");
  HQASSERT( oncomps > 0 , "oncomps should be > 0" ) ;
  HQASSERT(allowLuts || !allowInterleavedLuts,
           "allowInterleaved LUTs makes no sense without allowLuts") ;
  HQASSERT(fdecodes != NULL || !allowLuts,
           "allowLuts makes no sense without decode array(s)") ;
  HQASSERT(defaultFormat == ime_colorconverted ||
           defaultFormat == ime_as_is_decoded,
           "Unexpected default image expander format") ;

  ebpp = ibpp ;
  encomps = incomps ;

  lbpp = 0 ;

  paddata = FALSE ;

  if ( plane != IME_ALLPLANES ) {
    /* For recombine we need to override some of the inputs:
     * a) Output is always 1 plane (before we color convert).
     * b) We always expand to 16 bit LUTs.
     * c) We don't care about interleaving.
     */
    oncomps = 1 ;
    out16 = TRUE ;
    obpp = 16 ;

    format = ime_planar ;
    useluts = TRUE ;
    HQASSERT(fdecodes != NULL, "No decodes for recombined LUT") ;

    /* Pretend that lots of input planes. */
    encomps = plane + 1 ;

    /* Generate a sparse LUT since these will get merged later on. */
    lplanes = plane + 1 ;

    /* Store lut at index 'plane'. */
    splanes = plane ;
  } else if ( imageType == TypeImageAlphaAlpha ) {
    /* Always convert the alpha channel to simplify merging the alpha
       channel with the rest of the image data. */
    useluts = FALSE;
    HQASSERT(encomps == 1, "Should only be one input component for alpha channel") ;
    format = ime_colorconverted ;
    obpp = out16 ? 16 : 8 ;
    lplanes = 1 ;
    splanes = 0 ;
  } else {
    obpp = out16 ? 16 : 8 ;

    /* Can deal with either single channel inputs, multi channel inputs where
     * the total number of combinations are small (so use an interleaved LUT)
     * or multi channel inputs where the input space is equal to the output
     * space, which means that all the LUTs are independent.
     */
    useluts = FALSE ;

    format = defaultFormat; /* ime_colorconverted or ime_as_is_decoded */
    /* Debug option to process 32bit input data. Let it go through untouched */
    if ( ibpp == 32 )
      format = ime_as_is;

    /** \todo ajcd 2008-09-08: GSC_SHFILL won't appear without PoorShading,
        so remove it. */
    if ( colorType == GSC_SHFILL ) {
#ifdef POOR_SHADING
      HQFAIL("Obsolete image expansion color type") ;
#endif
      useluts = FALSE ;
    } else { /* Normal image. */
      if ( incomps == 1 ) {
        useluts = TRUE ; /* Use LUT if only one channel in. */
      } else {
        if ( independent ) {
          useluts = TRUE ; /* Use LUTs if channels are independent. */
        } else {
          /* If the number of combinations can be represented as a small LUT,
           * then we interleave the data and use an interleaved LUT to do the
           * color conversions. Choose that the total number of bits is less
           * than 8 bpp.
           */
          if ( ibpp * incomps <= MAX_LUT_BITS )
            useluts = TRUE ;
        }
      }
    }

    /* Image adjustment code cannot cope with interleaved luts. */
    if ( ! allowInterleavedLuts &&
         format == ime_colorconverted && ! independent && incomps != 1 )
      useluts = FALSE ;

    if (isSoftMask) {
      /* Always use a LUT when this is a soft mask - the LUT in this case
         contains final output colors. */
      useluts = TRUE;
      HQASSERT(incomps == 1,
               "Soft mask cannot have more than one component.");
      HQASSERT(fdecodes != NULL, "No decodes for softmask LUT") ;
    }

    /* As-is is a debug format; as-is decoded is used in image filtering. */
    HQASSERT(format == ime_colorconverted ||
             format == ime_as_is ||
             format == ime_as_is_decoded, "Unexpected format");

    if ( format != ime_colorconverted || !allowLuts )
      useluts = FALSE;

    if ( useluts ) {
      format = ime_planar ;
      if ( ! independent && incomps != 1 ) { /* Multi-channel LUT */
        encomps = 1 ;
        ebpp = ibpp * incomps ;
        /* This assert follows from the logic above; since we're not
           independent or single component, and the format is
           ime_colorconverted, the only way we can have got here is to have
           passed the remaining useluts=TRUE tests, which are that ibpp *
           incomps <= MAX_LUT_BITS.

           Multi-channel LUTs still have planar LUTs, and still produce only
           one output sample per input sample.
        */
        HQASSERT(ebpp <= MAX_LUT_BITS, "Multi-channel LUT size is too large") ;
        HQASSERT(ebpp > 1, "Multi-channel LUT size is too small.") ;
        if ( ebpp == 2 ) {
          format = ime_interleaved2  , lbpp =  2 - ebpp , ebpp =  2 ;
        } else if ( ebpp <= 4 ) {
          format = ime_interleaved4  , lbpp =  4 - ebpp , ebpp =  4 ;
        } else if ( ebpp <= 8 ) {
          format = ime_interleaved8  , lbpp =  8 - ebpp , ebpp =  8 ;
        } else if ( ebpp <= 16 ) {
          format = ime_interleaved16 , lbpp = 16 - ebpp , ebpp = 16 ;
        }
        paddata = ( uint8 )(lbpp != 0) ;
      }
    }

    /* Generate full set of planar LUTs. */
    lplanes = oncomps ;

    /* Start storing luts at index 0 onwards. */
    splanes = 0 ;
  }

  /* Allocate the IM_EXPAND structure. */
  ime = dl_alloc(page->dlpools, sizeof(IM_EXPAND),
                 MM_ALLOC_CLASS_IMAGE_EXPAND) ;
  if ( ime == NULL ) {
    (void)error_handler(VMERROR);
    return NULL ;
  }

  /* Clear pointers that might need freeing */
  ime->own_expluts = FALSE ;
  ime->expluts = NULL ;
  ime->cis = NULL ;
  ime->expbuf = NULL ;
  ime->ims_alpha = NULL ;

  if ( useluts && decodes_incomps != incomps )
    /* Must be preconverting the image and the image store contains a single
       plane of data and the decodes array maps this plane to the input space of
       the current colour chain.  This allows, for example, an indexed to RGB
       image to be adjusted by simply making a new indexed to CMYK LUT.  The
       original indexed to RGB LUT is included in the decodes array - hence it
       maps from one component (decodes_incomps == 1) to three components
       (incomps == 3, RGB).  The decodes array is now used to make an indexed to
       CMYK LUT in im_lutgennormal. */
    ime->iplanes = CAST_SIGNED_TO_INT16(decodes_incomps) ;
  else
    ime->iplanes = CAST_SIGNED_TO_INT16(encomps) ;

  ime->oplanes = CAST_SIGNED_TO_UINT16(oncomps) ;
  ime->lplanes = CAST_SIGNED_TO_UINT16(lplanes) ;

  /* Always allocate an extra LUT and cis array slot for a potential
     alpha channel to be added later.  Even if this image never has an
     alpha channel another image sharing the same LUT may. */
  ime->alloc_lplanes = CAST_SIGNED_TO_UINT16(lplanes + 1) ;

  /* If we're not using luts, then the data as stored in the image store, i.e.
   * as we read it, is already in the pixel size of the output. Hence the
   * first two following lines of code.
   *
   * Added 32bit image data as a debugging option which will be stored as
   * 32bits in the image store, so make sure this test does not trip in that
   * case.
   */
  if ( !useluts && ibpp != 32 )
    ebpp = ibpp = obpp ;

  /* Check if we can use an expanded LUT. That depends on the number of bits
   * per output pixel and the size of an expanded LUT.
   */
  expibpp = ebpp ;
  expobpp = obpp ;

  lutsize = 0 ;
  if ( useluts ) {
    /* We cannot expand LUTs for use in backdrops, because they rely upon the
       LUT packing when adjusting colours for the next blend space or device
       space. */
    if ( plane == IME_ALLPLANES && !guc_backdropRasterStyle(hRasterStyle) ) {
      /* As long as the LUT doesn't get too large (defined here as 2^8) and
       * the pixel size of the expanded data is one that we can cope with
       * (currently 8, 16 or 32) then we can expand the LUT to map more than
       * one sample simultaneously.
       */
      while ( expobpp < 32 && expibpp < 8 ) {
        expibpp <<= 1 ;
        expobpp <<= 1 ;
        HQASSERT((expibpp & (expibpp - 1)) == 0 && expibpp <= 16,
                 "Expanded input bits was not a power of 2") ;
        HQASSERT((expobpp & (expobpp - 1)) == 0 && expobpp <= 32,
                 "Expanded output bits was not a power of 2") ;
      }
    }

    /* The LUT size can be trimmed slightly if the input bits times input
       components were not a power of 2. We can omit the padding bits from
       the component mapped to the highest input bit position. */
    lutsize = ( 1u << ( expibpp - lbpp )) * expobpp / 8 ;
  }
  ime->lutsize = lutsize ;

  /* Now fill in all of our structure. If the data size is defined as 12 bits,
   * then we need to actually store 16 bits, since this is what we will read
   * from the image store.
   *
   * If, however, we're dealing with a pre-separated image then we don't have
   * an expand buffer, and the lut is basically used to hold the decode array,
   * so we really need to remember if the data size is 12 bpp
   */
  if ( plane == IME_ALLPLANES ) {
    ime->ibpp    = CAST_SIGNED_TO_UINT8( ebpp == 12 ? 16 : ebpp ) ;
    ime->expibpp = CAST_SIGNED_TO_UINT8( expibpp == 12 ? 16 : expibpp ) ;
  }
  else {
    ime->ibpp    = CAST_SIGNED_TO_UINT8(ebpp) ;
    ime->expibpp = CAST_SIGNED_TO_UINT8(expibpp) ;
  }
  ime->orig_ibpp = CAST_SIGNED_TO_UINT8(ebpp) ;

  ime->obpp    = CAST_SIGNED_TO_UINT8(obpp) ;
  ime->expobpp = CAST_SIGNED_TO_UINT8(expobpp) ;

  ime->presepExpbuf = (uint8)(plane != IME_ALLPLANES);

  ime->dl = page->eraseno;
  ime->format  = format ;

  ime->paddata = paddata ;

  ime->is_mask = FALSE ;

  ime->ime_direct = NULL;
  ime->otf = otf_init;

  NAME_OBJECT(ime, IM_EXPAND_NAME) ;
  /* From here on in the code we CAN call im_expandfree to clean up on error. */
  /* Allocate the COLORANTINDEX mapping array. We need ime->cis filled in
     before generating LUTs. */
  {
    int32 i ;
    COLORANTINDEX *cis = dl_alloc(page->dlpools,
                                  ime->alloc_lplanes * sizeof(COLORANTINDEX),
                                  MM_ALLOC_CLASS_IMAGE_TABLES) ;

    if ( cis == NULL ) {
      im_expandfree(page, ime) ;
      (void)error_handler(VMERROR);
      return NULL ;
    }

    for ( i = 0 ; i < ime->alloc_lplanes ; ++i )
      cis[i] = COLORANTINDEX_UNKNOWN ;
    ime->cis = cis ;

    /* There used to be an optimisation that made ime->cis NULL if the
       colorants were in order. I don't think it's worth saving the small
       amount of memory (usually 16 bytes per image) for this, at the expense
       of the complexity of the conditions that are required to set it up and
       handle it. */
    if ( imageType == TypeImageAlphaAlpha ) {
      /* Alpha masks don't need a CIS array, because they have their store
         stolen and attached to the parent expander. But, we've allocated one
         anyway, so fill it in. */
      HQASSERT(lplanes > 0, "Not enough channels for alpha channel") ;
      ime->cis[0] = COLORANTINDEX_ALPHA ;
    } else if ( plane == IME_ALLPLANES ) {
      int32 nColorants ;
      COLORANTINDEX *iColorants ;

      /* Obtain the list of colorants for the device */
      if ( ! gsc_getDeviceColorColorants(colorInfo, colorType,
                                         &nColorants, &iColorants) ) {
        im_expandfree(page, ime) ;
        return NULL ;
      }

      HQASSERT(nColorants <= ime->lplanes, "Differing nColorants vs lplanes") ;

      for ( i = 0 ; i < nColorants ; ++i )
        ime->cis[i] = iColorants[i] ;
    } else {
      HQASSERT(plane >= 0 && plane < lplanes, "Preseparation plane out of range") ;
      ime->cis[plane] = plane ;
    }
  }

  HQASSERT(!isSoftMask || useluts, "im_expandopenimage - should be using "
           "a LUT for a soft mask.");

  /* If LUTs to be used, generate the LUT or take it from the LUT cache. */
  if ( useluts ) {
    if ( !im_lutgen(page, ime, fdecodes, ndecodes, decode_array, decode_for_adjust,
                    independent, incomps, ibpp, fixedpt16, method,
                    oncomps, out16, splanes, plane, isSoftMask,
                    expibpp, ebpp, lbpp,
                    colorInfo, colorType) ) {
      im_expandfree(page, ime);
      return NULL;
    }
  }

  /* Calculate the size of the expansion buffer that we need. */
  /** \todo ajcd 2008-09-08: This will need changing when rendering operates
      per image store block, instead of per line. */

  /* Start with the width of the input, rounded up to a whole number of
     input units. */
  bufsize = (width * ebpp + expibpp - 1) / expibpp ;

  /* If the expanded size is less than a byte, round up to the next byte
     worth of input units. */
  bufsize = (bufsize + 7 / expibpp) & ~(7 / expibpp) ;

  /* Multiply by the output size per input unit. */
  bufsize *= expobpp / 8 ;

  /* If the image may require compositing (i.e. it's a front end color chain,
     !gsc_fCompositing()), or if outputting to pixel interleaved, leave space
     for the output components, potentially with alpha and runlength/type
     channels. */
  /** \todo jonw 64747 The "isTrappingActive()" is a hack I was hoping
      to remove, but must stay in for HMR3 beta because of doubt about
      where im_force_interleave etc. are reset: the comments in
      dlstate.h say erasepage but in fact they are reset as part of
      beginpage. That means trapResetZone and settrapzone_'s setting
      of this value gets reset by dl_image_start before it has chance
      to take effect here. */
  if ( !gsc_fCompositing(colorInfo, colorType) ||
       interleaved || isTrappingActive(page) || page->im_force_interleave ) {
    int32 comps = oncomps ;

    /* Because image buffers are created early based on output
     * surface, we have to have this dl_state parameter to ensure that
     * if a trapping or capture surface is selected later, the
     * expansion buffer meets requirement of the trapping or capture
     * surface (which may be pixel interleaved). */
    /** \todo jonw 64747 This will need changing if we ever support
        interleaving of more than 4 colorants. What about RLE?
        (currently making a minimal change on the run-up to HMR3
        beta) */
    if (page->im_force_interleave && oncomps < 4)
      comps = 4;

    /* Are we going to expand the alpha channel? */
    /** \todo ajcd 2008-10-01: This won't be right when we allow /Alpha as
        an output channel. */
    if ( imageType == TypeImageAlphaImage )
      ++comps ;

    bufsize *= comps;
  }

  /* Allocate the expansion buffer structure (IM_EXPBUF) plus the extra bytes
     for the buffer itself. Ensure enough extra bytes to blit align the
     expander buffer, and to have a full complement of blit_t words in it. */
  bufsize = BLIT_ALIGN_SIZE(bufsize) + BLIT_MASK_BYTES ;
  {
    size_t i;
    IM_EXPBUF *expbuf = dl_alloc(page->dlpools,
                                 sizeof(IM_EXPBUF) + bufsize,
                                 MM_ALLOC_CLASS_IMAGE_EXPBUF) ;
    if ( expbuf == NULL ) {
      im_expandfree(page, ime) ;
      (void)error_handler(VMERROR);
      return NULL;
    }

    expbuf->buf    = (uint8 *)BLIT_ALIGN_UP(expbuf + 1) ;
    expbuf->size   = bufsize ;
    expbuf->refcnt = 1 ;
    expbuf->ime    = ime ;
    expbuf->otf    = IM_EXPAND_INVALID;
    expbuf->expanded_comps = 0;

    /* Clear the "duplicate line" expansion cache. */
    expbuf->x = expbuf->y = expbuf->n = IM_EXPAND_INVALID ;
    for ( i = 0 ; i < EXPBUF_N_COMPS ; ++i )
      expbuf->expanded_to_plane[i] = -1;

    NAME_OBJECT(expbuf, IM_EXPBUF_NAME) ;

    /* Accumulate the size of the largest expansion buffer. */
    if ( bufsize > page->im_bufsize )
      page->im_bufsize = bufsize ;

    ime->expbuf = expbuf ;
  }
  return ime ;
}

/* -------------------------------------------------------------------------- */
static Bool im_lutgen(DL_STATE *page, IM_EXPAND *ime,
                      float *fdecodes[], int32 *ndecodes[],
                      float *decode_array, void *decode_for_adjust,
                      Bool independent,
                      int32 incomps, int32 ibpp , Bool fixedpt16,
                      int32 method, int32 oncomps, Bool out16,
                      int32 splanes, int32 plane, Bool isSoftMask,
                      int32 expibpp, int32 ebpp, int32 lbpp,
                      GS_COLORinfo *colorInfo, int32 colorType)
{
  uint8 **expluts = NULL ;

  /* Have we made a LUT already?  Image LUTs are not cached for recombine
     because recombine needs to merge LUTs.  It is not worth caching soft
     mask image LUTs. */
  if ( plane == IME_ALLPLANES && !isSoftMask ) {
    if ( imlut_lookup(colorInfo, colorType,
                      decode_array, decode_for_adjust,
                      incomps, ibpp, (expibpp != ebpp),
                      &ime->expluts) )
      return TRUE;
  }

  /* Allocate all the image LUT memory. */
  if ( !im_expand_alloc_expluts(page, ime, splanes) )
    return FALSE;

  expluts = ime->expluts;

  /* Now fill in all the LUTs. */
  if ( plane != IME_ALLPLANES ) {
    if ( ! im_lutgenpresep( ime, colorInfo , colorType , fdecodes , ibpp ,
                            expluts , plane ))
      return FALSE ;
  } else {
    if ( isSoftMask ) {
      float* src = fdecodes[0];
      COLORVALUE* result = (COLORVALUE*)expluts[0];
      uint32 count = 1 << ibpp;
      uint32 i;

      HQASSERT(incomps == 1, "im_expandopenimage - soft mask cannot have more "
               "than one component.");
      for (i = 0; i < count; i ++) {
        result[i] = FLOAT_TO_COLORVALUE(src[i]);
      }
    } else {
      if ( ! im_lutgennormal( page, ime, colorInfo , colorType ,
                              fdecodes , ndecodes ,
                              incomps , ibpp , fixedpt16 , method,
                              ebpp , out16 , independent , expluts , expibpp ))
        return FALSE ;
    }

    /* If we're generating an expanded LUT, then expand it. */
    if ( expibpp != ebpp )
      im_lutexpand( expluts , oncomps , out16 , expibpp , ebpp , lbpp ) ;
  }

  if ( plane == IME_ALLPLANES && !isSoftMask ) {
    Bool added;

    /* Created a new image LUT, now make it available for sharing. */
    imlut_add(page, colorInfo, colorType,
              decode_array, decode_for_adjust,
              incomps, ibpp, (expibpp != ebpp),
              ime->expluts, &added);

    /* Ownership of the image LUT has been handed over to the imlut. */
    if ( added )
      ime->own_expluts = FALSE;
  }

  return TRUE;
}


/** Setup structures for on-the-fly color conversion with the image expander */
Bool im_expand_setup_on_the_fly(const IMAGEOBJECT *imageobj, DL_STATE *page,
                                int32 method, GS_COLORinfo *colorInfo,
                                int32 colorType)
{
  IM_EXPAND *ime = imageobj->ime;
  int32 oncomps;
  Bool out16, fromPageGroup;
  int32 out_size;

  if ( ime->ime_direct != NULL )
    return TRUE;

  if (!gsc_getDeviceColorColorants(colorInfo, colorType,
                                   &oncomps, &ime->otf.output_colorants))
    return FALSE;
  out16 = ht_is16bitLevels(gsc_getSpotno(colorInfo),
                           gsc_getRequiredReproType(colorInfo, colorType),
                           oncomps, ime->otf.output_colorants,
                           gsc_getTargetRS(colorInfo));

  fromPageGroup = !guc_backdropRasterStyle(gsc_getTargetRS(colorInfo));

  ime->otf.converter = cv_colcvtopen(page, method, colorInfo, colorType,
                                     ime->obpp == 16, out16,
                                     gsc_dimensions(colorInfo, colorType),
                                     oncomps, ime->otf.output_colorants,
                                     fromPageGroup, NULL);
  if ( ime->otf.converter == NULL )
    return FALSE;

  /* Enlarge expand buffer if necessary. */
  out_size = ( out16 ? 2 : 1 ) * (size_t)oncomps *
    (imageobj->imsbbox.x2 - imageobj->imsbbox.x1 + 1) ;
  if ( out_size > ime->expbuf->size )
    if ( !im_expbuf_enlarge(ime, page, out_size) ) {
      cv_colcvtfree(page, &ime->otf.converter);
      return FALSE;
    }
  ime->otf.planes = CAST_UNSIGNED_TO_UINT16(oncomps);
  ime->otf.obpp = out16 ? 16 : 8;
  ime->otf.method = method;
  return TRUE;
}

static Bool im_convert_otf(IM_EXPAND *ime, IM_EXPBUF *imb, size_t iconv,
                           unsigned int expanded_comps,
                           unsigned int converted_comps)
{
  uint8 *in = imb->buf, *out = imb->buf;

  HQASSERT(ime->otf.method != GSC_QUANTISE_ONLY, "Expected full color conversion");

  if ( expanded_comps * ime->obpp < converted_comps * ime->otf.obpp ) {
    /* Conversion expands, can't convert in place; move input to end. */
    size_t in_bytes = iconv * expanded_comps * (ime->obpp / 8);
    in += (size_t)imb->size - in_bytes;
    HqMemMove(in, imb->buf, (int32)in_bytes);
  }
  imb->otf = TRUE;
  /* Go through the scanline's worth of data, limited to processing only
     GSC_BLOCK_MAXCOLORS values at a time. */
  while ( iconv > 0 ) {
    size_t tconv = min(GSC_BLOCK_MAXCOLORS, iconv);

    if ( !cv_colcvt(ime->otf.converter, (uint32)tconv, NULL,
                    (COLORVALUE *)in, (COLORVALUE *)out, out) )
      return FALSE;
    in  += tconv * expanded_comps  * (ime->obpp / 8);
    out += tconv * ime->otf.planes * (ime->otf.obpp / 8);
    iconv -= tconv;
  }
  return TRUE;
}

static void im_quantise_otf(IM_EXPAND *ime, IM_EXPBUF *imb, size_t iconv,
                            unsigned int expanded_comps,
                            int plane, unsigned int eindex)
{
  uint8 *in = imb->buf, *out = imb->buf;

  HQASSERT(ime->otf.method == GSC_QUANTISE_ONLY, "Expected quantisation only");
  HQASSERT(ime->obpp >= ime->otf.obpp,
           "Quantisation should always be possible in situ");

  imb->otf = TRUE;
  /* Go through the scanline's worth of data, limited to processing only
     GSC_BLOCK_MAXCOLORS values at a time. */
  while ( iconv > 0 ) {
    size_t tconv = min(GSC_BLOCK_MAXCOLORS, iconv);

    cv_quantiseplane(ime->otf.converter, (uint32)tconv,
                     plane, eindex, expanded_comps,
                     (COLORVALUE *)in, (COLORVALUE *)out, out);
    in  += tconv * expanded_comps * (ime->obpp / 8);
    out += tconv * expanded_comps * (ime->otf.obpp / 8);
    iconv -= tconv;
  }
}

/** Attach an alternate ime for the direct regions (whilst still being
    able to use the original for the backdrop regions). */
void im_expand_attach_alternate(IM_EXPAND *ime, IM_EXPAND *ime_alternate)
{
  /* May have set up on-the-fly early for separation omission, but use
     the alternate ime from now on. */
  if ( ime->otf.converter != NULL )
    cv_colcvtfree(CoreContext.page, &ime->otf.converter);
  ime->ime_direct = ime_alternate;
}


Bool im_converting_on_the_fly(const IM_EXPAND *ime)
{
  return ime->otf.converter != NULL && ime->otf.method != GSC_QUANTISE_ONLY;
}


/** Are we getting 16 bpc from the image? */
Bool im_16bit_output(const render_info_t* p_ri, const IM_EXPAND *ime)
{
  if ( p_ri->region_set_type == RENDER_REGIONS_DIRECT ) {
    if ( ime->otf.converter != NULL )
      /* Use on-the-fly value. */
      return ime->otf.obpp == 16;
    if ( ime->ime_direct != NULL )
      /* Switch to the alternate ime. */
      ime = ime->ime_direct;
  }
  return ime->obpp == 16;
}


/* -------------------------------------------------------------------------- */
static Bool im_lutgenpresep( IM_EXPAND *ime,
                             GS_COLORinfo *colorInfo ,
                             int32 colorType ,
                             float *decodes[] ,
                             int32 ibpp ,
                             uint8 **expluts ,
                             int32 plane )
{
  int32      ncolors;
  USERVALUE* iColorValues;
  USERVALUE  iColorValue;
  COLORVALUE oColorCode;
  int32      iColor;
  uint16     *explut;

  UNUSED_PARAM( IM_EXPAND * , ime ) ;

  ncolors = ( 1 << ibpp );
  iColorValues = decodes[0];
  explut = ( uint16 * )expluts[ plane ];

  for ( iColor = 0; iColor < ncolors; ++iColor ) {

    /* Convert one input value from gray to subtractive, fixed point, form */
    iColorValue = iColorValues[iColor];
    if ( !gsc_invokeChainBlock(colorInfo, colorType,
                               &iColorValue, &oColorCode, 1))
      return FALSE;

    explut[iColor] = oColorCode;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
#define COLOR_ARRAY_SIZE 32
static Bool im_lutgennormal(DL_STATE *page, IM_EXPAND *ime,
                            GS_COLORinfo *colorInfo, int32 colorType,
                            float *fdecodes[], int32 *ndecodes[],
                            int32 incomps, int32 ibpp, Bool fixedpt16,
                            int32 method, int32 ebpp, Bool out16,
                            Bool independent, uint8 **expluts, int32 expibpp)
{
  int32 i , j , ncolors , offset = 0 ;
  int32 n = expibpp / ebpp ;  /* 'n' bytes per number */
  int32 adjust = n * ( 1 << ( expibpp - ebpp )) ;
  int32 ibppmask = (( 1 << ibpp ) - 1 ) ;
  int32 ncolorsPerComp ;
  int32 **idecodes ;
  int32 icolorArray[COLOR_ARRAY_SIZE] ;
  int32 *icolor = icolorArray ;
  SPOTNO spotno = gsc_getSpotno(colorInfo) ;
  HTTYPE reprotype = gsc_getRequiredReproType(colorInfo, colorType) ;
  corecontext_t *context = get_core_context();

  COLORVALUE ocolorArray[COLOR_ARRAY_SIZE] ;
  COLORVALUE *ocolor = ocolorArray ;
  COLORANTINDEX *ocolorants;
  HT_TRANSFORM_INFO *transformInfo = NULL;
  GUCR_RASTERSTYLE *targetRS = gsc_getTargetRS(colorInfo);
  Bool fCompositing = gsc_fCompositing(colorInfo, colorType);

  /* 1bit images require invoke single to intercept 100% black, but back-end
     chains don't invoke single to avoid direct-/backdrop-render anomalies. */
  Bool doInvokeSingle = ibpp == 1 && !fCompositing ;
  Bool doQuantise = !guc_backdropRasterStyle(targetRS) && fCompositing ;
  Bool *htDoForms = NULL ;
  Bool result = FALSE ;

  if ( !gsc_getDeviceColorColorants(colorInfo, colorType, &i, &ocolorants) )
    return FALSE ;
  HQASSERT(i == ime->lplanes, "lplanes mismatch with color chain");

  if (incomps > COLOR_ARRAY_SIZE) {
    icolor = mm_alloc( mm_pool_temp ,
                       incomps * sizeof( int32 ) ,
                       MM_ALLOC_CLASS_IMAGE_TABLES ) ;
    if ( icolor == NULL ) {
      (void)error_handler(VMERROR) ;
      goto cleanup ;
    }
  }
#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!

  if (ime->lplanes > COLOR_ARRAY_SIZE) {
    ocolor = mm_alloc( mm_pool_temp ,
                       ime->lplanes * sizeof( COLORVALUE ) ,
                       MM_ALLOC_CLASS_IMAGE_TABLES ) ;
    if ( ocolor == NULL ) {
      (void)error_handler(VMERROR) ;
      goto cleanup ;
    }
  }

  if ( doQuantise && gucr_halftoning(targetRS) ) {
    htDoForms = mm_alloc( mm_pool_temp ,
                          ime->lplanes * sizeof( Bool ) ,
                          MM_ALLOC_CLASS_IMAGE_TABLES ) ;
    if ( htDoForms == NULL ) {
      (void)error_handler(VMERROR) ;
      goto cleanup ;
    }
    for ( j = 0; j < ime->lplanes; ++j ) {
      GUCR_COLORANT *colorant;
      const GUCR_COLORANT_INFO *info;
      gucr_colorantHandle(targetRS, ocolorants[j], &colorant);
      htDoForms[j] = (colorant != NULL &&
                      gucr_colorantDescription(colorant, &info));
    }
  }

  ncolorsPerComp = ( fixedpt16 ? COLORVALUE_MAX + 1 : 1 << ibpp ) ;
  ncolors = ncolorsPerComp ;  /* ie. num values per _colorant_ */
  if ( ! independent )
    for ( j = 1 ; j < incomps ; ++j )
      ncolors *= ncolorsPerComp ;   /* now, num values per _color_ */

  /* Prepare to quantise colors to halftone levels or device codes
     (if not done here, quantisation is done in the color chain). */
  if ( doQuantise ) {
    transformInfo = mm_alloc( mm_pool_temp ,
                              ime->lplanes * sizeof( HT_TRANSFORM_INFO ) ,
                              MM_ALLOC_CLASS_IMAGE_TABLES ) ;
    if ( transformInfo == NULL ) {
      (void)error_handler(VMERROR) ;
      goto cleanup ;
    }
    ht_setupTransforms(spotno, reprotype, ime->lplanes, ime->cis,
                       gsc_getRS(colorInfo), transformInfo) ;
  }

  if ( method == GSC_USE_PS_PROC || colorType != GSC_IMAGE || doInvokeSingle ) {
    HQASSERT(sizeof(int32) == sizeof(float),
             "Casting decodes array will no longer work");
    idecodes = (int32**)fdecodes;
  } else
    idecodes = ndecodes;

  /* For color value; i.e. for each entry in the LUT,... */
  for ( i = 0 ; i < ncolors ; ++i ) {
    /* Take each colorant's value from the Decode array being careful of
       pixel interleaved (non-independent) sample values, and put them
       in the 'icolors[]' array ready for color conversion. */
    if ( independent ) {
      if (method == GSC_USE_FASTRGB2GRAY || method == GSC_USE_FASTRGB2CMYK) {
        for ( j = 0 ; j < incomps ; ++j )
          icolor[j] = i;
      } else {
        for ( j = 0 ; j < incomps ; ++j )
          icolor[ j ] = idecodes[ j ][ i ] ;
      }
    }
    else {
      /* For non-independent LUTs we have to generate the table backwards. */
      int32 tmpi = i ;
      for ( j = incomps - 1 ; j >= 0 ; --j ) {
        icolor[ j ] = idecodes[ j ][ tmpi & ibppmask ] ;
        tmpi >>= ibpp ;
      }
      HQASSERT( tmpi < ( 1 << ibpp ) , "underun when generating LUT" ) ;
    }

    /* Perform color conversion on the input color (icolor). */
    if ( !doInvokeSingle ) {
      if ( method == GSC_USE_PS_PROC || colorType != GSC_IMAGE ) {
        if ( ! gsc_invokeChainBlock( colorInfo , colorType ,
                                     (float*)icolor , ocolor , 1 ))
          goto cleanup ;
      } else {
        if (method == GSC_USE_FASTRGB2CMYK) {
          if (!gsc_fastrgb2cmyk_do(colorInfo, colorType, icolor, ocolor, 1))
            goto cleanup;
        } else if (method == GSC_USE_FASTRGB2GRAY) {
          if (!gsc_fastrgb2gray_do(colorInfo, colorType, icolor, ocolor, 1))
            goto cleanup;

        } else {
          if ( ! gsc_invokeChainBlockViaTable( colorInfo , colorType ,
                                               icolor , ocolor , 1 ))
            goto cleanup ;
        }
      }
    }
    else {
      /* Invoke single because we may need to intercept 100% black, and block is
         optimised not to do that. There should only be two colors in the loop
         in this case. Can only do this in front-end color chains or it may lead
         to differences between direct- and backdrop-rendered regions. */
      if ( ! gsc_setcolordirect( colorInfo , colorType , (float*)icolor ) ||
           ! gsc_invokeChainSingle( colorInfo , colorType ))
        goto cleanup ;

      for ( j = 0 ; j < ime->lplanes ; ++j ) {
        /* Find the color values for each output colorant of the image. This
           also works for the /All colorant. */
        if ( !dlc_get_indexed_colorant(dlc_currentcolor(page->dlc_context),
                                       ime->cis[j], &ocolor[j]) )
          goto cleanup ;
      }
    }

    /* If quantisation didn't happen in the color chain then do it now.  Doing
       quantisation outside of Tom's Tables ensures consistency between direct-
       and backdrop-rendered regions. */
    if ( doQuantise ) {
      ht_doTransforms(ime->lplanes, ocolor, transformInfo, ocolor) ;
      for ( j = 0; htDoForms != NULL && j < ime->lplanes; ++j )
        if ( htDoForms[j] &&
             ht_allocateForm(ime->dl, spotno, reprotype, ocolorants[j],
                             1, &ocolor[j], ime->lplanes, context) )
          htDoForms[j] = FALSE; /* all forms now allocated */
    }

    /* Converted color values now in the 'ocolor[]' array.  Put them into the LUT.
       Remember, it's one whole LUT per output colorant (i.e. 'expluts' is an array
       of pointers to LUTs). */
    for ( j = 0 ; j < ime->lplanes ; ++j ) {
      if ( out16 ) {
        uint16 *explut = ( uint16 * )expluts[ j ] ;
        explut[ offset ] = ( uint16 )ocolor[ j ] ;
      } else if ( !doQuantise ) {
        /* Color chain produced 16-bit color values.
           This is 8-bit output, so use the top 8 bits. */
        uint8 *explut = ( uint8 * )expluts[j] ;
        explut[offset] = COLORVALUE_TO_UINT8(ocolor[j]) ;
      } else {
        uint8 *explut = ( uint8 * )expluts[ j ] ;
        explut[ offset ] = ( uint8 )ocolor[ j ] ;
      }
    }
    offset += adjust ;    /* Increment index to leave room if LUT is an expanded LUT. */

  } /* next LUT entry */

  if ( fixedpt16 ) {
    /* If the source is fixed-pt 16-bit colorvalues then we only use a subset of
       the 16-bits.  Since the testing for blank separations may test the whole
       lut, the remaining lut entries filled by copying the first entry.
       Creating a full 16-bit lut saves on special case code in other areas. */
    ncolorsPerComp = 1 << ibpp ;
    ncolors = ncolorsPerComp ;
    if ( ! independent )
      for ( j = 1 ; j < incomps ; ++j )
        ncolors *= ncolorsPerComp ;
    HQASSERT( i < ncolors, "Should still have lut entries to pad" ) ;

    for ( ; i < ncolors ; ++i ) {
      for ( j = 0 ; j < ime->lplanes ; ++j ) {
        if ( out16 ) {
          uint16 *explut = ( uint16 * )expluts[ j ] ;
          explut[ offset ] = explut[ 0 ] ;
        }
        else {
          uint8 *explut = ( uint8 * )expluts[ j ] ;
          explut[ offset ] = explut[ 0 ] ;
        }
      }
      offset += adjust ;    /* Increment index to leave room if LUT is an expanded LUT. */
    }
  }

  result = TRUE;
 cleanup:
  if (transformInfo != NULL)
    mm_free( mm_pool_temp , transformInfo ,
             ime->lplanes * sizeof( HT_TRANSFORM_INFO ));
  if (htDoForms != NULL)
    mm_free( mm_pool_temp , htDoForms , ime->lplanes * sizeof( Bool )) ;
  if (ocolor != ocolorArray)
    mm_free( mm_pool_temp , ocolor , ime->lplanes * sizeof( COLORVALUE )) ;
  if (icolor != icolorArray)
    mm_free( mm_pool_temp , icolor , incomps * sizeof( int32 )) ;
#undef return
  return result ;
}

/* -------------------------------------------------------------------------- */
static void im_lutexpand( uint8 **expluts , int32 oncomps , Bool out16 ,
                          int32 expibpp , int32 ebpp , int32 lbpp )
{
  int32 i , j , k ;
  int32 colors = ( 1 << ( expibpp - lbpp )) ;
  int32 n = expibpp / ebpp ;
  int32 adjust = n * ( 1 << ( expibpp - ebpp )) ;
  int32 ibppmask = (( 1 << ( ebpp - lbpp )) - 1 ) ;

  for ( j = 0 ; j < oncomps ; ++j ) {
    uint8 *bexplut = ( uint8 * )expluts[ j ] ;
    uint8 *explut = bexplut ;
    for ( i = 0 ; i < colors ; ++i ) {
      int32 tmpi = i ;
      explut += ( out16 ? n << 1 : n << 0 ) ;
      for ( k = n - 1 ; k >= 0 ; --k ) {
        if ( out16 ) {
          uint16 *explut16 = ( uint16 * )explut ;
          uint16 *bexplut16 = ( uint16 * )bexplut ;
          explut16[ -1 ] = bexplut16[ ( tmpi & ibppmask ) * adjust ] ;
          explut -= 2 ;
        }
        else {
          explut[ -1 ] = bexplut[ ( tmpi & ibppmask ) * adjust ] ;
          explut -= 1 ;
        }
        tmpi >>= ebpp ;
      }
      explut += ( out16 ? n << 1 : n << 0 ) ;
    }
  }
}

/* ----------------------------------------------------------------------------
   Update the image expander to reflect that an alpha channel has been added. */
Bool im_expandalphachannel(IM_EXPAND* ime,
                           IM_STORE* ims_alpha)
{
  HQASSERT(ime, "ime is null");
  HQASSERT(ims_alpha, "ims_alpha is null");
  HQASSERT(ime->ims_alpha == NULL, "Alpha channel already attached to expander");
  HQASSERT((ime->lplanes + 1) == ime->alloc_lplanes, "No space for alpha in LUT and cis");
  HQASSERT(ime->cis != NULL,
           "Attaching alpha channel to preseparated or alpha channel image") ;
  HQASSERT(!ime->expluts || !ime->expluts[ime->lplanes],
           "Shouldn't have a LUT allocated for alpha channel");

  ime->cis[ime->lplanes] = COLORANTINDEX_ALPHA;

  ime->lplanes += 1;
  ime->oplanes += 1;

  ime->ims_alpha = ims_alpha;

  return TRUE;
}

IM_STORE* im_expand_ims_alpha(IM_EXPAND* ime)
{
  return ime->ims_alpha;
}

IM_STORE* im_expand_detach_alphachannel(IM_EXPAND *ime)
{
  IM_STORE *ims_alpha = ime->ims_alpha ;

  ime->ims_alpha = NULL ;

  return ims_alpha ;
}

/* ---------------------------------------------------------------------- */
void im_expandplanefree(DL_STATE *page, IM_EXPAND* ime, int32 plane)
{
  HQASSERT(ime->format == ime_planar, "Must be planar format");
  HQASSERT(ime->expluts, "ime->expluts is null");
  HQASSERT(plane >= 0 && plane < ime->lplanes, "plane is out of range");
  HQASSERT(ime->expluts[plane], "Lut is null");

  if ( ime->own_expluts )
    dl_free(page->dlpools, ime->expluts[plane], ime->lutsize,
            MM_ALLOC_CLASS_IMAGE_TABLES);
  ime->expluts[plane] = NULL;
  ime->cis[plane] = COLORANTINDEX_UNKNOWN;
  ime->oplanes -= 1;
}

/* ---------------------------------------------------------------------- */
Bool im_expandreorder(DL_STATE *page, IM_EXPAND *ime,
                      const COLORANTINDEX order[], int32 nplanes)
{
  int32 i;
  COLORANTINDEX *cis ;
  uint8 **expluts ;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME) ;

  HQASSERT(ime->own_expluts, "Shouldn't be trying to reorder expluts if it's cached");
  HQASSERT(nplanes == ime->oplanes, "unexpected number of oplanes");
  HQASSERT(nplanes <= ime->lplanes, "unexpected number of lplanes");
  HQASSERT(ime->format == ime_planar, "Should only reorder planar images") ;

  cis = ime->cis ;
  HQASSERT(cis != NULL, "No colorant indices for planar image expander") ;

  expluts = ime->expluts ;
  HQASSERT(expluts != NULL, "No LUTs for planar image expander") ;

  /* For each output colorant, find the current slot that contains its
     colorant, and swap it for the desired slot. */
  for (i = 0; i < nplanes; ++i) {
    int32 j;
    /* It's tempting to try to speed the following loop up by starting it at
       j=i+1, but please don't, because it'll break a useful assert. If the
       first desired colorant isn't found, the plane in the first slot won't
       be moved; it may then be found to match a later colorant. Also, the
       assert that we actually found a plane for the colorant relies on
       starting at less than ime->lplanes. */
    for (j = 0; j < ime->lplanes; ++j) {
      if (cis[j] == order[i]) {
        /* Found colorant for this slot. */
        uint8 *tmplut = expluts[i] ;
        COLORANTINDEX tmpci = cis[i] ;
        expluts[i] = expluts[j] ;
        cis[i] = cis[j] ;
        expluts[j] = tmplut ;
        cis[j] = tmpci ;
        break ;
      }
    }
    HQASSERT(j < ime->lplanes, "No colorant for image expander reordering") ;
  }

  /* The rest of the slots should be empty. */
  for ( ; i < ime->lplanes ; ++i ) {
    if ( ime->expluts[i] != NULL ) {
      HQFAIL("Image expander plane not used after reordering") ;
      im_expandplanefree(page, ime, i) ;
    }
    HQASSERT(ime->cis[i] == COLORANTINDEX_UNKNOWN,
             "Image expander plane has colorant but no lut after reordering") ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool im_expandrecombined(IM_EXPAND* ime, DL_STATE *page,
                         const COLORANTINDEX rcbmap[], int32 maplength)
{
  int32 i ;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);

  /* Map pseudo colorant indices to virtual colorant indices. */
  for ( i = 0 ; i < ime->lplanes && i < maplength ; ++i ) {
    COLORANTINDEX ci = ime->cis[i] ;
    if ( ci != COLORANTINDEX_UNKNOWN )
      ime->cis[i] = rcbmap[ci] ;
  }

  /* Since we didn't know how many planes this object would
     contain when the ime was originally created, we may need
     to now enlarge the expansion buffer to deal with oplanes. */
  if (ime->expbuf != NULL && ime->oplanes > 1 && ime->presepExpbuf) {
    if ( !im_expbuf_enlarge(ime, page, ime->expbuf->size * ime->oplanes) )
      return FALSE;
    ime->presepExpbuf = FALSE;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

void *im_expand1to1(IM_EXPAND *ime, IM_STORE *ims,
                    Bool xflip, int32 x , int32 y , int32 n , int32 *nrows,
                    const int expanded_to_plane[],
                    unsigned int nexpanded)
{
  Bool result ;
  IM_EXPBUF *imb ;

  *nrows = 1;
  imb = expand_expbuf(ime, get_core_context()) ;

  HQASSERT(ims != NULL, "ims is missing");
  HQASSERT( x >= 0 , "x should be >= 0" ) ;
  HQASSERT( y >= 0 , "y should be >= 0" ) ;
  HQASSERT( n >  0 , "y should be >  0" ) ;
  HQASSERT(nexpanded == 1u && expanded_to_plane[0] >= 0,
           "Should only be one expanded channel for 1 to 1") ;

  SwOftenUnsafe() ;

  /* If this image was the last to use this expansion buffer, and the
   * expansion buffer holds the correct part of the image then we don't
   * need to refill the expansion buffer. */
  if ( reuse_expand_buffer(ime, imb, x, y, n, FALSE,
                           expanded_to_plane, nexpanded) )
    return imb->buf ;

  /* Need to calculate adjustment. Adjust to word boundary lower than
     x position, since expanders expand a word at a time and clipping on the
     left of the image store can result in number of bytes in the first image
     block that is not a multiple of the word size. */
  {
    const ibbox_t *oibbox = im_storebbox_original(ims) ;
    int32 adjust = (x - oibbox->x1) & BLIT_MASK_BITS ;
    x -= adjust ;
    n += adjust ;
  }

  /* Adjust number of samples to number of bytes */
  n = (n + 7) / 8 ;

  if ( xflip )
    result = im_expand1to1b(ime, imb, ims, x, y, n, expanded_to_plane[0]) ;
  else
    result = im_expand1to1f(ime, imb, ims, x, y, n, expanded_to_plane[0]) ;

  if ( !result )
    return NULL ;

  claim_expand_buffer(ime, imb) ;

  return imb->buf ;
}

/* -------------------------------------------------------------------------- */
void *im_expandknockout(IM_EXPAND *ime,
                        IM_STORE *ims,
                        Bool compositing,
                        int32 x, int32 y, int32 n, int32 *nrows,
                        const int expanded_to_plane[],
                        unsigned int nexpanded)
{
  static uint32 dummy_data[1] = { 0xdeadbeef } ;

  UNUSED_PARAM(IM_EXPAND *, ime);
  UNUSED_PARAM(IM_STORE *, ims);
  UNUSED_PARAM(Bool, compositing);
  UNUSED_PARAM(int32, x);
  UNUSED_PARAM(int32, y);
  UNUSED_PARAM(int32, n);
  UNUSED_PARAM(const int *, expanded_to_plane);
  UNUSED_PARAM(unsigned int, nexpanded);

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  HQASSERT(nexpanded == 0, "Should have no expanded data") ;

  *nrows = 1;
  return dummy_data ;
} /* Function im_expandknockout */


/* Read and expand some image data. */
void *im_expandread(IM_EXPAND *ime , IM_STORE *ims ,
                    Bool compositing,
                    int32 x, int32 y, int32 nvals, int32 *nrows,
                    const int expanded_to_plane[],
                    unsigned int expanded_comps)
{
  unsigned int eindex;
  int32 offset ;
  Bool fdoexpand;
  Bool funmapped;
  IM_EXPBUF *imb ;
  int32 n, nvals_adj;
  /* If on-the-fly conversion has been set up and not rendering to backdrop,
   * do color conversion to device colors. */
  Bool cc_on_the_fly, quantise_otf ;
  unsigned int converted_comps ;
  IM_STORE *ims_alpha = ime->ims_alpha ;
  corecontext_t *context = get_core_context();

  HQASSERT(ims != NULL, "ims missing");
  HQASSERT( x >= 0 , "x should be >= 0" ) ;
  HQASSERT( y >= 0 , "y should be >= 0" ) ;
  HQASSERT( nvals >  0, "nvals should be > 0" );
  HQASSERT( expanded_to_plane , "No plane mapping array" ) ;
  HQASSERT( expanded_comps > 0, "No expanded components" );

  *nrows = im_storeread_nrows(ims, y);

  cc_on_the_fly = im_converting_on_the_fly(ime) && !compositing;
  quantise_otf = ime->otf.converter != NULL &&
    ime->otf.method == GSC_QUANTISE_ONLY && !compositing;
  HQASSERT(!cc_on_the_fly || !quantise_otf, "Can't use both on-the-fly methods");
  converted_comps = cc_on_the_fly ? ime->otf.planes : expanded_comps;
  if ( ime->ime_direct != NULL && !compositing ) {
    /* Switch to the alternate image expander for direct regions. */
    HQASSERT(!cc_on_the_fly, "Can't use both conversion methods at the same time");
    ime = ime->ime_direct;
  }

  imb = expand_expbuf(ime, context);

  SwOftenUnsafe() ;

  /* If not starting at a byte boundary, adjust starting point and length */
  offset = 0; nvals_adj = nvals;
  if ( ime->ibpp < 8 && ime->expluts != NULL ) {
    /* Round start of expansion to a byte boundary. */
    const ibbox_t *oibbox = im_storebbox_original(ims) ;
    int32 adjust = (x - oibbox->x1) & (7 / ime->ibpp); /* 4->1, 2->3, 1->7 */
    if ( adjust > 0 ) {
      x -= adjust;
      nvals_adj += adjust;
      offset = adjust * converted_comps * ((cc_on_the_fly || quantise_otf
                                            ? ime->otf.obpp : ime->obpp) / 8);
    }
  }

  /* If this image was the last to use this expansion buffer, and the
   * expansion buffer holds the correct part of the image then we don't
   * need to refill the expansion buffer. */
  if ( reuse_expand_buffer(ime, imb, x, y, nvals_adj,
                           cc_on_the_fly || quantise_otf,
                           expanded_to_plane, expanded_comps) )
    return imb->buf + offset;

  /* Total number of bytes required. */
  n = ( nvals_adj * ime->ibpp + 7 ) / 8 ;

  /* Determine if there is anything to do, and whether the mapping can be used
     with interleaved LUTs. */
  fdoexpand = funmapped = FALSE ;
  for ( eindex = 0 ; eindex < expanded_comps ; ++eindex ) {
    HQASSERT(expanded_to_plane[eindex] < ime->lplanes,
             "Requesting non-existing plane from image expander") ;
    if ( expanded_to_plane[eindex] >= 0 ) {
      fdoexpand = TRUE ;
    } else { /* Unmapped expander channel; first time round, clear this data. */
      funmapped = TRUE ;
    }
  }

  if ( !fdoexpand ) {
    /** \todo ajcd 2008-09-17: what should we do if there are no mapped
        channels? The code returned FALSE/NULL before. */
    HQFAIL("Nothing for image expander to do") ;
    return NULL ;
  }

  /* Clear out buffer if for new image or first use of buffer, and unmapped
     expander channels exist, so we get easy and consistent merging of
     adjacent pixels. We don't care what the values of the unmapped channels
     actually are. */
  if ( funmapped ) {
    IM_EXPAND *ime_ofbuf = imb->ime ;
    if ( ime_ofbuf == NULL || ime_ofbuf != ime || imb->n < 0 ) {
      void* expbuf = imb->buf;
      HqMemZero(expbuf, imb->size) ;
    }
  }

  /* Expand the planar stores one at a time, interleaving into the
     expander buffer. */
  for ( eindex = 0 ; eindex < expanded_comps ; ++eindex ) {
    Bool result = TRUE ;
    int plane = expanded_to_plane[eindex] ;

    /* If no plane should be filled in, ignore channel. */
    if ( plane < 0 )
      continue ;

    HQASSERT(plane < ime->lplanes, "No such plane in expander") ;
    HQASSERT(ime->cis != NULL, "No colorant index array in expander") ;
    HQASSERT(COLORANTINDEX_VALID(ime->cis[plane]),
             "Colorant index for plane is not valid") ;

    if ( ime->cis[plane] == COLORANTINDEX_ALPHA ) {
      int32 depth = im_storebpp(ims_alpha) ;
      int32 nalpha = nvals_adj * depth/8;
      /* If we're getting the alpha channel, divert it to the alpha store. */
      if ( depth == 16 ) {
        if ( ime->obpp == 16 ) {
          result = im_expand16to16nolut(ime, imb, ims_alpha, x, y, nalpha,
                                        0 /*plane*/, eindex, expanded_comps) ;
        } else {
          HQASSERT(ime->obpp == 8, "Output depth should be 8 or 16") ;
          result = im_expand16to8nolut(ime, imb, ims_alpha, x, y, nalpha,
                                       0 /*plane*/, eindex, expanded_comps) ;
        }
      } else {
        HQASSERT(depth == 8, "Alpha depth should be 8 or 16") ;
        if ( ime->obpp == 16 ) {
          result = im_expand8to16nolut(ime, imb, ims_alpha, x, y, nalpha,
                                       0 /*plane*/, eindex, expanded_comps) ;
        } else {
          HQASSERT(ime->obpp == 8, "Output depth should be 8 or 16") ;
          result = im_expand8to8nolut(ime, imb, ims_alpha, x, y, nalpha,
                                      0 /*plane*/, eindex, expanded_comps, context) ;
        }
      }
    } else {
      switch ( ime->expibpp ) {
      case  1: /* 1->? */
        switch ( ime->expobpp ) {
        case 8: /* 1->8 LUT */
          /* This case covers:
             1 x 1-bit -> 8-bit planar/interleaved output
          */
          result = im_expand1to8(ime, imb, ims, x, y, n,
                                 plane, eindex, expanded_comps, context) ;
          break ;
        case 16: /* 1->16 LUT */
          /* This case covers:
             1 x 1-bit -> 16-bit planar/interleaved output
          */
          result = im_expand1to16(ime, imb, ims, x, y, n,
                                  plane, eindex, expanded_comps) ;
          break ;
        default:
          HQFAIL( "Unsupported/unknown output bits per pixel" ) ;
          return NULL ;
        }
        break ;
      case  2: /* 2->? */
        switch ( ime->expobpp ) {
        case 8: /* 2->8 LUT */
          /* This case covers:
             1 x 2-bit -> 8-bit planar/interleaved output
             1 x 1-bit x 2-color -> 8-bit planar/interleaved output
          */
          HQASSERT(ime->ibpp == ime->expibpp,
                   "Shouldn't have expanded input with 8-bit output") ;
          result = im_expand2to8(ime, imb, ims, x, y, n,
                                 plane, eindex, expanded_comps) ;
          break ;
        case 16: /* 2->16 LUT */
          /* We shouldn't have expanded input because the expansion should
             have continued to give 4->32. */
          HQASSERT(ime->ibpp == ime->expibpp,
                   "Shouldn't have expanded input for 2->16") ;
          /* This case covers:
             1 x 2-bit -> 16-bit interleaved/planar output
             1 x 1-bit x 2-color -> 16-bit planar/interleaved output
          */
          result = im_expand2to16(ime, imb, ims, x, y, n,
                                  plane, eindex, expanded_comps) ;
          break ;
        case 32: /* 2->32, only 2x1->2x16 LUT */
          HQASSERT(ime->ibpp != ime->expibpp,
                   "Should have expanded input with 32-bit output") ;
          /* This case covers:
             2 x 1-bit -> 2 x 16-bit planar/interleaved output
          */
          result = im_expand1to16x2(ime, imb, ims, x, y, n,
                                    plane, eindex, expanded_comps) ;
          break ;
        default:
          HQFAIL( "Unsupported/unknown output bits per pixel" ) ;
          return NULL ;
        }
        break ;
      case  4: /* 4->? */
        switch ( ime->expobpp ) {
        case 8: /* 4->8 LUT */
          HQASSERT(ime->ibpp == ime->expibpp,
                   "Shouldn't have expanded input with 8-bit output") ;
          /* This case covers:
             1 x 4-bit -> 8-bit interleaved/planar output
             1 x 2-bit x 2-color -> 8-bit interleaved/planar output
             1 x 1-bit x 3-color+pad -> 8-bit interleaved/planar output
             1 x 1-bit x 4-color -> 8-bit interleaved/planar output
          */
          result = im_expand4to8(ime, imb, ims, x, y, n,
                                 plane, eindex, expanded_comps) ;
          break ;
        case 16: /* 4->16 */
          /* We shouldn't have expanded input because the expansion should
             have continued to give 8->32. */
          HQASSERT(ime->ibpp == ime->expibpp,
                   "Shouldn't have expanded input with 4->16") ;
          /* This case covers:
             1 x 4-bit -> 16-bit interleaved/planar output
             1 x 2-bit x 2-color -> 16-bit interleaved/planar output
             1 x 1-bit x 3-color+pad -> 16-bit interleaved/planar output
             1 x 1-bit x 4-color -> 16-bit interleaved/planar output
          */
          result = im_expand4to16(ime, imb, ims, x, y, n,
                                  plane, eindex, expanded_comps) ;
          break ;
        case 32: /* 4->32 LUT */
          HQASSERT(ime->ibpp != ime->expibpp,
                   "Should have expanded input with 32-bit output") ;
          switch ( ime->ibpp ) {
          case 2: /* 2x2->2x16 LUT */
            /* This case covers:
               2 x 2-bit -> 2 x 16-bit interleaved output
               2 x 1-bit x 2-color -> 2 x 16-bit planar/interleaved output
            */
            result = im_expand2to16x2(ime, imb, ims, x, y, n,
                                      plane, eindex, expanded_comps) ;
            break ;
          case 1: /* 4x1->4x8 LUT */
            /* This case covers:
               4 x 1-bit -> 4 x 8-bit planar/interleaved output
            */
            result = im_expand1to8x4(ime, imb, ims, x, y, n,
                                     plane, eindex, expanded_comps, context) ;
            break ;
          default:
            HQFAIL("Unrecognised combination of expanded and input bits") ;
            return NULL ;
          }
          break ;
        default:
          HQFAIL( "Unsupported/unknown output bits per pixel" ) ;
          return NULL ;
        }
        break ;
      case  8: /* 8->? */
        if ( ime->expluts == NULL ) {
          HQASSERT(ime->expobpp == 8, "Output isn't same as input depth") ;
          HQASSERT(ime->ibpp == ime->expibpp,
                   "Cannot use expanded input without LUT") ;
          result = im_expand8to8nolut(ime, imb, ims, x, y, n,
                                      plane, eindex, expanded_comps, context) ;
        } else {
          switch ( ime->expobpp ) {
          case 8: /* 8->8 LUT */
            HQASSERT(ime->ibpp == ime->expibpp,
                     "Shouldn't have expanded input with 8-bit output") ;
            /* This case covers:
               1 x 8-bit -> 8-bit interleaved/planar output
               1 x 4-bit x 2-color -> 8-bit interleaved/planar output
               1 x 2-bit x 3-color+pad -> 8-bit interleaved/planar output
               1 x 2-bit x 4-color -> 8-bit interleaved/planar output
               1 x 1-bit x 5-color+pad -> 8-bit interleaved/planar output
               1 x 1-bit x 6-color+pad -> 8-bit interleaved/planar output
               1 x 1-bit x 7-color+pad -> 8-bit interleaved/planar output
               1 x 1-bit x 8-color -> 8-bit interleaved/planar output
            */
            result = im_expand8to8(ime, imb, ims, x, y, n,
                                   plane, eindex, expanded_comps, context) ;
            break ;
          case 16: /* 8->16 LUT */
            if ( expanded_comps == 1 || ime->ibpp == ime->expibpp ) {
              /* This case covers:
                 1 x 8-bit -> 16-bit interleaved/planar output
                 1 x 4-bit x 2-color -> 16-bit interleaved/planar output
                 1 x 2-bit x 3-color+pad -> 16-bit interleaved/planar output
                 1 x 2-bit x 4-color -> 16-bit interleaved/planar output
                 1 x 1-bit x 5-color+pad -> 16-bit interleaved/planar output
                 1 x 1-bit x 6-color+pad -> 16-bit interleaved/planar output
                 1 x 1-bit x 7-color+pad -> 16-bit interleaved/planar output
                 1 x 1-bit x 8-color -> 16-bit interleaved/planar output
                 2 x 4-bit -> 2 x 8-bit planar output
                 2 x 2-bit x 2-color -> 2 x 8-bit planar output
              */
              result = im_expand8to16(ime, imb, ims, x, y, n,
                                      plane, eindex, expanded_comps) ;
            } else {
              /* This case covers:
                 2 x 4-bit -> 2 x 8-bit interleaved output
                 2 x 2-bit x 2-color -> 2 x 8-bit interleaved output
              */
              result = im_expand4to8x2(ime, imb, ims, x, y, n,
                                       plane, eindex, expanded_comps) ;
            }
            break ;
          case 32: /* 8->32 LUT */
            HQASSERT(ime->ibpp != ime->expibpp,
                     "Should have expanded input with 32-bit output") ;
            switch ( ime->ibpp ) {
            case 4: /* 2x4->2x16 LUT */
              /* This case covers:
                 2 x 4-bit -> 2 x 16-bit planar/interleaved output
                 2 x 2-bit x 2-color -> 2 x 16-bit planar/interleaved output
                 2 x 1-bit x 3-color+pad -> 2 x 16-bit planar/interleaved output
                 2 x 1-bit x 4-color -> 2 x 16-bit planar/interleaved output
              */
              result = im_expand4to16x2(ime, imb, ims, x, y, n,
                                        plane, eindex, expanded_comps) ;
              break ;
            case 2: /* 4x2->4x8 LUT */
              /* This case covers:
                 4 x 2-bit -> 4 x 8-bit planar/interleaved output
                 4 x 1-bit x 2-color -> 4 x 8-bit planar/interleaved output
              */
              result = im_expand2to8x4(ime, imb, ims, x, y, n,
                                       plane, eindex, expanded_comps) ;
              break ;
            default:
              HQFAIL("Unrecognised combination of expanded and input bits") ;
              return NULL ;
            }
            break ;
          default:
            HQFAIL( "Unsupported/unknown output bits per pixel" ) ;
            return NULL ;
          }
        }
        break ;
      case 12:
        HQASSERT( ime->expluts != NULL , "explut must be there for recombine" ) ;
        HQASSERT( ime->expobpp == 16 , "expobpp should be 16 for recombine") ;
        /* This case covers:
           1 x 12-bit -> 1 x 16-bit planar/interleaved output
        */
        result = im_expand16to16(ime, imb, ims, x, y, n,
                                 plane, eindex, expanded_comps) ;
        break ;
      case 16: /* 16->? */
        if ( ime->expluts == NULL ) {
          HQASSERT(ime->expobpp == 16, "Output isn't same as input depth") ;
          result = im_expand16to16nolut(ime, imb, ims, x, y, n,
                                        plane, eindex, expanded_comps) ;
        } else {
          switch ( ime->expobpp ) {
          case 8: /* 16->8 LUT */
            /* This case covers:
               1 x 12-bit -> 1 x 8-bit planar/interleaved output
               1 x 16-bit -> 1 x 8-bit planar/interleaved output
               1 x 1-bit x 9-color -> 8-bit interleaved/planar output
               1 x 1-bit x 10-color -> 8-bit interleaved/planar output
               1 x 1-bit x 11-color -> 8-bit interleaved/planar output
               1 x 1-bit x 12-color -> 8-bit interleaved/planar output
            */
            result = im_expand16to8(ime, imb, ims, x, y, n,
                                    plane, eindex, expanded_comps) ;
            break ;
          case 16: /* 16->16 LUT */
            HQASSERT(ime->ibpp == ime->expibpp,
                     "Shouldn't have expanded input to 16 bits") ;
            /* This case covers:
               1 x 16-bit -> 16-bit interleaved/planar output
               1 x 1-bit x 9-color -> 16-bit interleaved/planar output
               1 x 1-bit x 10-color -> 16-bit interleaved/planar output
               1 x 1-bit x 11-color -> 16-bit interleaved/planar output
               1 x 1-bit x 12-color -> 16-bit interleaved/planar output
            */
            result = im_expand16to16(ime, imb, ims, x, y, n,
                                     plane, eindex, expanded_comps) ;
            break ;
          default:
            HQFAIL( "Unsupported/unknown output bits per pixel" ) ;
            return NULL ;
          }
        }
        break ;
      case 32: /* 32->? */
        /*
         * For testing 32bit data in the image store, add a very simple
         * testing option of processing 32bit image data. Only works in
         * a very limited number of setups.
         */
        HQASSERT(ime->ibpp == ime->expibpp,
                 "Shouldn't have expanded input with 32-bit input") ;
        switch ( ime->expobpp ) {
        case 8: /* 32->8 */
          /* This case covers:
             1 x 32-bit -> 8-bit interleaved/planar output
          */
          result = im_expand32to8nolut(ime, imb, ims, x, y, n,
                                       plane, eindex, expanded_comps);
          break ;
        case 16: /* 32->16 */
          /* This case covers:
             1 x 32-bit -> 16-bit interleaved/planar output
          */
          result = im_expand32to16nolut(ime, imb, ims, x, y, n,
                                        plane, eindex, expanded_comps);
          break ;
        default:
          HQFAIL( "Unsupported/unknown output bits per pixel" ) ;
          return NULL ;
        }
        break;
      default:
        HQFAIL( "Unsupported/unknown input bits per pixel" ) ;
        return NULL ;
      }
    }

    if ( !result )
      return NULL ;

    /* On-the-fly quantisation to device codes (already in device space). */
    if ( quantise_otf )
      im_quantise_otf(ime, imb, (size_t)nvals_adj, expanded_comps, plane, eindex);
  }

  /* On-the-fly color conversion to device space and device codes. */
  if ( cc_on_the_fly ) {
    if ( !im_convert_otf(ime, imb, (size_t)nvals_adj, expanded_comps,
                         converted_comps) )
      return NULL;
  }

  claim_expand_buffer(ime, imb) ;

  return imb->buf + offset ;
}

/* -------------------------------------------------------------------------- */
static Bool im_expand_alloc_expluts(DL_STATE *page, IM_EXPAND *ime,
                                    int32 splanes)
{
  int32 i;

  HQASSERT(!ime->own_expluts && !ime->expluts, "expluts already allocated");
  HQASSERT(ime->lutsize > 0, "image explander lutsize <= 0");

  /* Allocate array of pointers (one pointer for each plane). */
  ime->expluts = (uint8**)dl_alloc(page->dlpools,
                                   ime->alloc_lplanes * sizeof(uint8*),
                                   MM_ALLOC_CLASS_IMAGE_TABLES);
  if ( !ime->expluts )
    return error_handler(VMERROR);

  for ( i = 0; i < ime->alloc_lplanes; ++i ) {
    ime->expluts[i] = NULL;
  }

  ime->own_expluts = TRUE;

  /* Now allocate all the LUT memory (don't allocate LUT for alpha channel in
     the final slot, when lplanes < alloc_lplanes). */
  for ( i = splanes ; i < ime->lplanes ; ++i ) {
    ime->expluts[i] = (uint8*)dl_alloc(page->dlpools, ime->lutsize,
                                       MM_ALLOC_CLASS_IMAGE_TABLES);
    if ( !ime->expluts[i] )
      return error_handler(VMERROR);
  }

  return TRUE;
}


static void im_expand_free_expluts(DL_STATE *page, IM_EXPAND *ime)
{
  if ( ime->own_expluts && ime->expluts ) {
    int32 i;
    for ( i = 0; i < ime->alloc_lplanes; ++i ) {
      if ( ime->expluts[i] )
        dl_free(page->dlpools, ime->expluts[i], ime->lutsize,
                MM_ALLOC_CLASS_IMAGE_TABLES);
    }
    dl_free(page->dlpools, ime->expluts, ime->alloc_lplanes * sizeof(uint8*),
            MM_ALLOC_CLASS_IMAGE_TABLES);
    ime->expluts = NULL;
  }
}

void im_expandfree(DL_STATE *page, IM_EXPAND *ime)
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);

  if ( ime->ime_direct != NULL )
    im_expandfree(page, ime->ime_direct);
  if ( ime->otf.converter != NULL )
    cv_colcvtfree(page, &ime->otf.converter);
  if ( ime->own_expluts )
    im_expand_free_expluts(page, ime);
  if ( ime->cis )
    dl_free(page->dlpools, ime->cis,
            ime->alloc_lplanes * sizeof(COLORANTINDEX),
            MM_ALLOC_CLASS_IMAGE_TABLES);
  if ( ime->expbuf ) {
    IM_EXPBUF *expbuf = ime->expbuf ;

    VERIFY_OBJECT(expbuf, IM_EXPBUF_NAME);

    if (( --expbuf->refcnt ) == 0 ) {
      /* Clear the pointer to the shared expansion buffer, it's just an
       * optimization for purge. */
      if ( page->im_expbuf_shared == expbuf )
        page->im_expbuf_shared = NULL ;
      /* This could have been the largest buffer, but can't (re)set the size,
       * since we don't know it. The code (especially in purge code) is safe
       * even so. */

      UNNAME_OBJECT(expbuf) ;
      dl_free(page->dlpools, expbuf , sizeof(IM_EXPBUF) + expbuf->size,
              MM_ALLOC_CLASS_IMAGE_EXPBUF);
    } else {
      /* The expansion buffer is referenced more than once. Make sure the
       * expansion buffer points to an expander which references the
       * expansion buffer and NOT the expander being freed.
       */
      if ( expbuf->ime == ime ) {
        IMAGEOBJECT *image ;

        for ( image = page->im_list ;
              image != NULL ;
              image = image->next ) {
          if ( image->ime != ime
               && image->ime != NULL && image->ime->expbuf == expbuf ) {
            expbuf->ime = image->ime ;
            break ;
          }
        }
      }
    }
  }

  if (ime->ims_alpha)
    im_storefree(ime->ims_alpha);

  UNNAME_OBJECT(ime) ;
  dl_free(page->dlpools, ime , sizeof(IM_EXPAND),
          MM_ALLOC_CLASS_IMAGE_EXPAND);
}

/* -------------------------------------------------------------------------- */
Bool im_expandmerge(DL_STATE *page, IM_EXPAND *ime_src, IM_EXPAND *ime_dst)
{
  int32 i ;
  int32 alloc_lplanes ;

  VERIFY_OBJECT(ime_src, IM_EXPAND_NAME);
  VERIFY_OBJECT(ime_dst, IM_EXPAND_NAME);

  HQASSERT( ime_src->ibpp == ime_dst->ibpp , "src ibpp does not match dst ibpp" ) ;
  HQASSERT( ime_src->obpp == ime_dst->obpp , "src obpp does not match dst obpp" ) ;
  HQASSERT( ime_src->expibpp == ime_dst->expibpp , "src expibpp does not match dst expibpp" ) ;
  HQASSERT( ime_src->expobpp == ime_dst->expobpp , "src expobpp does not match dst expobpp" ) ;
  HQASSERT( ime_src->lutsize == ime_dst->lutsize , "src lutsize does not match dst lutsize" ) ;
  HQASSERT( ime_src->expbuf != NULL && ime_dst->expbuf != NULL ,
            "im_expandmerge: src and dst expbuf must not be null" ) ;
  HQASSERT( (ime_src->own_expluts && ime_dst->own_expluts) ||
            (!ime_src->expluts && !ime_dst->expluts) ,
            "Shouldn't be trying to merge cached expluts");

  HQASSERT(ime_src->expluts != NULL && ime_dst->expluts != NULL,
           "Inconsistent useage of lut when recombining");

  /* Enlarge the number of planes if we need to. */
  alloc_lplanes = ime_src->alloc_lplanes ;
  if ( alloc_lplanes > ime_dst->alloc_lplanes ) {
    COLORANTINDEX *cis ;
    uint8 **expluts = NULL;

    if ( (cis = dl_alloc(page->dlpools, alloc_lplanes * sizeof(COLORANTINDEX),
                         MM_ALLOC_CLASS_IMAGE_TABLES)) == NULL )
      return error_handler(VMERROR);

    for ( i = 0 ; i < ime_dst->alloc_lplanes ; ++i )
      cis[i] = ime_dst->cis[i] ;
    for ( ; i < alloc_lplanes ; ++i )
      cis[i] = COLORANTINDEX_UNKNOWN ;

    if (ime_src->expluts != NULL) {
      expluts = dl_alloc(page->dlpools, alloc_lplanes * sizeof( uint8 *),
                         MM_ALLOC_CLASS_IMAGE_TABLES) ;
      if ( expluts == NULL ) {
        dl_free(page->dlpools, cis, alloc_lplanes * sizeof(COLORANTINDEX),
                MM_ALLOC_CLASS_IMAGE_TABLES);
        return error_handler(VMERROR);
      }

      for ( i = 0 ; i < ime_dst->alloc_lplanes ; ++i )
        expluts[i] = ime_dst->expluts[i] ;
      for ( ; i < alloc_lplanes ; ++i )
        expluts[i] = NULL ;

      dl_free(page->dlpools, ime_dst->expluts,
              ime_dst->alloc_lplanes * sizeof(uint8 *), MM_ALLOC_CLASS_IMAGE_TABLES);
    }

    dl_free(page->dlpools, ime_dst->cis,
            ime_dst->alloc_lplanes * sizeof(COLORANTINDEX),
            MM_ALLOC_CLASS_IMAGE_TABLES);

    ime_dst->cis = cis ;
    ime_dst->expluts = expluts ;
    ime_dst->lplanes = ime_src->lplanes ;
    ime_dst->alloc_lplanes = ime_src->alloc_lplanes ;
  }

  ime_dst->iplanes = ime_dst->lplanes ;
  HQASSERT(ime_src->oplanes == 1, "Merging source plane with more than one output") ;
  ime_dst->oplanes += 1 ;

  /* Merge planes from src into dst. */
  for ( i = 0 ; i < ime_src->lplanes ; ++i ) {
    HQASSERT(ime_src->cis[i] == COLORANTINDEX_UNKNOWN ||
             ime_dst->cis[i] == COLORANTINDEX_UNKNOWN,
             "Only one of merged channels should be valid") ;
    if ( ime_src->cis[i] != COLORANTINDEX_UNKNOWN )
      ime_dst->cis[i] = ime_src->cis[i] ;
  }

  if (ime_src->expluts != NULL) {
    for ( i = 0 ; i < ime_src->lplanes ; ++i ) {
      HQASSERT(ime_src->expluts[i] == NULL ||
               ime_dst->expluts[i] == NULL,
               "Only one of merged channels should have a LUT") ;
      if ( ime_src->expluts[ i ] != NULL ) {
        ime_dst->expluts[ i ] = ime_src->expluts[ i ] ;
        ime_src->expluts[ i ] = NULL ;
      }
    }
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
int32 im_ci2plane(const IM_EXPAND *ime, COLORANTINDEX ci)
{
  int32 plane ;
  COLORANTINDEX *cis ;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);

  cis = ime->cis ;
  HQASSERT(cis != NULL, "Should have colorant indices for image expander") ;
  plane = ime->lplanes ;
  HQASSERT(plane > 0, "No planes in image expander") ;
  while ( --plane >= 0 )
    if ( cis[plane] == ci )
      break ;

  return plane ; /* -1 if ci not found */
}

static int32 im_getci(const IM_EXPAND *ime , COLORANTINDEX ci)
{
  int32 plane ;

  plane = im_ci2plane(ime, ci) ;
  if ( plane < 0 ) /* Didn't find colorant, see if there is an /All colorant */
    plane = im_ci2plane(ime, COLORANTINDEX_ALL) ;

  return plane ;
}

/* -------------------------------------------------------------------------- */
const COLORANTINDEX *im_getcis(const IM_EXPAND *ime)
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);

  return ime->cis ;
}

/* -------------------------------------------------------------------------- */
Bool im_expand1bit(const blit_color_t *color, const IM_EXPAND *ime,
                   const int expanded_to_plane[],
                   unsigned int expanded_comps,
                   const int blit_to_expanded[])
{
  int32 n, adjust ;
  channel_index_t index ;
  channel_output_t color0, color1, htmax ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Screen information not set up for blit color") ;
  HQASSERT(expanded_to_plane != NULL, "No expanded to plane mapping") ;
  HQASSERT(blit_to_expanded != NULL, "No blit to expanded mapping") ;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);

  /* Must be expanding one component. */
  if ( expanded_comps != 1 )
    return FALSE ;

  /* Expansion must map to a real plane */
  if ( expanded_to_plane[0] < 0 )
    return FALSE ;

  /* Must have planar LUTs. */
  if ( ime->format != ime_planar )
    return FALSE ;

  /* Check that only 1 bpp; could include ibpp > 1 but that would be a stupid image. */
  if ( ime->ibpp != 1 )
    return FALSE ;

  n = ime->expibpp / ime->ibpp ;
  adjust = n * (1 << (ime->expibpp - ime->ibpp)) ;

  HQASSERT(expanded_to_plane[0] < ime->lplanes,
           "Planar LUT doesn't have enough source components") ;

  if ( ime->obpp == 8 ) {
    uint8 *explut8 = ( uint8 * )ime->expluts[expanded_to_plane[0]] ;
    color0 = explut8[ 0 ] ;
    color1 = explut8[ adjust ] ;
  } else { /* ime->obpp == 16 */
    uint16 *explut16 = ( uint16 * )ime->expluts[expanded_to_plane[0]] ;
    color0 = explut16[ 0 ] ;
    color1 = explut16[ adjust ] ;
    HQASSERT(ime->obpp == 16, "Expander output is not 8 or 16") ;
  }

  /* We don't care which way round the mapping is, just that one end is
     clear and the other is solid. */
  if ( color0 == 0 )
    htmax = color1 ;
  else if ( color1 == 0 )
    htmax = color0 ;
  else /* Neither color is zero. */
    return FALSE ;

  for ( index = 0 ; index < color->map->nchannels ; ++index ) {
    /* All blit channels should map to either -1 (no mapping) or expanded data
       index 0 (the sole expanded channel). For those blit channels that map
       to this channel, verify that they have the correct max value. */
    if ( blit_to_expanded[index] == 0 ) {
      if ( htmax != color->quantised.htmax[index] )
        return FALSE ;
    } else {
      HQASSERT(blit_to_expanded[index] < 0,
               "Blit channel maps to non-existent expander data") ;
    }
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
int32 im_expandformat(const IM_EXPAND *ime)
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  HQASSERT( ime->expluts != NULL ||
            ime->format == ime_colorconverted ||
            ime->format == ime_as_is ||
            ime->format == ime_as_is_decoded , "explut disagrees with format" ) ;
  return ime->format ;
}

/* -------------------------------------------------------------------------- */
Bool im_expandpad(const IM_EXPAND *ime)
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  /* padding color components is required when:
   * a) doing RGB interleaved -> RGB_ (1,2,3 bits per component).
   */
  return ime->paddata ;
}

/* -------------------------------------------------------------------------- */
int32 im_expandbpp(const IM_EXPAND *ime)
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  /* What is the input bit depth? */
  HQASSERT( ime->orig_ibpp ==  1 ||
            ime->orig_ibpp ==  2 ||
            ime->orig_ibpp ==  4 ||
            ime->orig_ibpp ==  8 ||
            ime->orig_ibpp == 12 ||
            ime->orig_ibpp == 16 ||
            ime->orig_ibpp == 32, "im_expandbpp: orig_ibpp not one of 1,2,4,8,12,16,32") ;
  return ime->orig_ibpp ;
}

/* -------------------------------------------------------------------------- */
int32 im_expandobpp(const IM_EXPAND *ime)
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  /* What is the output bit depth? */
  HQASSERT(ime->obpp ==  8 ||
           ime->obpp == 16, "im_expandobpp: obpp not one of 8,16") ;
  return ime->obpp ;
}

/* -------------------------------------------------------------------------- */
int32 im_expandluttest(const IM_EXPAND *ime, COLORANTINDEX ci, COLORVALUE ecv)
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);

  /* If planar LUT exists, test if all same as erase color */
  if ( ime->expluts != NULL ) {
    int32 adjust = 1 ;
    int32 li = im_getci(ime, ci) ;

    if ( li < 0 )
      return imcolor_error ;

    HQASSERT(li < ime->lplanes, "LUT index not in range") ;

    /* For expanded LUT, take only relevant entries */
    if ( ime->ibpp != ime->expibpp ) {
      adjust = (ime->expibpp / ime->ibpp) * ( 1 << ( ime->expibpp - ime->ibpp )) ;
    }

    switch ( ime->obpp ) {
    case 8: {   /* !out16 */
      uint8 *lut = (uint8 *)ime->expluts[li] ;
      size_t ncolors = ime->lutsize ;
      size_t i ;

      for ( i = 0 ; i < ncolors ; i += adjust ) {
        uint8 ocv = lut[i] ;

        if ( ime->otf.converter != NULL && ime->otf.method == GSC_QUANTISE_ONLY ) {
          COLORVALUE icv = ocv ;
          cv_quantiseplane(ime->otf.converter, 1, li, 0 /* offset */, 1 /* stride */,
                           &icv, NULL, &ocv) ;
        }

        if ( ocv != CAST_UNSIGNED_TO_UINT8(ecv) )
          return imcolor_different ;
      }
      break ;
    }
    case 16: {  /* out16 */
      uint16 *lut = (uint16 *)ime->expluts[li] ;
      size_t ncolors = ( ime->lutsize >> 1 ) ;
      size_t i ;

      for ( i = 0 ; i < ncolors ; i += adjust ) {
        COLORVALUE ocv = lut[i] ;

        if ( ime->otf.converter != NULL && ime->otf.method == GSC_QUANTISE_ONLY )
          cv_quantiseplane(ime->otf.converter, 1, li, 0 /* offset */, 1 /* stride */,
                         &ocv, &ocv, NULL) ;

        if ( ocv != ecv )
          return imcolor_different ;
      }
      break ;
    }
    default:
      HQFAIL( "Unsupported/unknown output bits per pixel" ) ;
      return imcolor_error ;
    }

    return imcolor_same ;
  }

  return imcolor_unknown ;
}

/* -------------------------------------------------------------------------- */
int32 im_expanddatatest( IM_EXPAND *ime, IM_STORE *ims,
                         COLORANTINDEX ci, COLORVALUE ecv,
                         const ibbox_t *ibbox)
{
  int32 rw, rh, yindex, plane ;
  int expanded_to_plane[1] ;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  HQASSERT(ims != NULL, "ims is missing");

  /* Expand data from image */
  plane = im_getci(ime, ci) ;
  if ( plane < 0 ) { /* Plane does not exist */
    return imcolor_error ;
  }

  expanded_to_plane[0] = plane ;

  rw = ibbox->x2 - ibbox->x1 + 1 ;
  rh = ibbox->y2 - ibbox->y1 + 1 ;

  switch ( ime->obpp ) {
    uint32 ecv4 ;
  case 8:
    /* 8-bit values. */
    HQASSERT(ecv <= 255, "Erase color value out of range.") ;

    /* Set up 4-pixel at a time value */
    ecv4 = (ecv | (ecv << 8)) ;
    ecv4 = (ecv4 | (ecv4 << 16)) ;

    for ( yindex = ibbox->y1 ; yindex <= ibbox->y2 ; ++yindex ) {
      uint8 *values ;
      int32 nPixels = rw, nrows;

      SwOftenUnsafe() ;
      if ( !interrupts_clear(allow_interrupt) ) {
        ( void ) report_interrupt(allow_interrupt) ;
        return imcolor_error ;
      }

      values = im_expandread(ime, ims, FALSE, ibbox->x1, yindex, rw, &nrows,
                             expanded_to_plane, 1u) ;
      if ( values == NULL )
        return imcolor_error ;

      /* Check groups of four, treating each group as a single word. Initial
         and trailing entries that are not word-aligned are processed separately.
         This method is only used if there will be enough pixels to align the
         address and then get more than one word of comparison. */
      if ( nPixels >= 11 ) {
        uint32 *values32 ;
        int32 nWords ;

        /* Consume any initial values to get 'values' onto a word boundary. */
        switch ((uintptr_t)values & 3) {
        case 1:
          if ( ecv != (COLORVALUE)*values )
            return imcolor_different ;
          --nPixels ;
          ++values ;
          /* FALLTHRU */
        case 2:
          if ( ecv != (COLORVALUE)*values )
            return imcolor_different ;
          --nPixels ;
          ++values ;
          /* FALLTHRU */
        case 3:
          if ( ecv != (COLORVALUE)*values )
            return imcolor_different ;
          --nPixels ;
          ++values ;
        }

        nWords = nPixels >> 2 ;
        nPixels -= nWords << 2 ;
        HQASSERT(nWords > 0, "Address alignment left too few words") ;

        HQASSERT(((uintptr_t)values & 3) == 0,
                 "Address alignment failed to align to uint32 boundary") ;

        values32 = (uint32 *)values ;
        do {
          if ( ecv4 != *values32 )
            return imcolor_different ;

          values32 ++ ;
          nWords -- ;
        } while ( nWords > 0 ) ;

        values = (uint8 *)values32 ;
      }

      /* Check the remaining values. */
      for ( ; nPixels > 0 ; nPixels -- ) {
        if ( ecv != (COLORVALUE)*values )
          return imcolor_different ;

        values ++ ;
      }
    }

    break ;

  case 16:

    /* Set up 4-pixel at a time value */
    ecv4 = (ecv | (ecv << 16)) ;

    for ( yindex = ibbox->y1 ; yindex <= ibbox->y2 ; ++yindex ) {
      uint16 *values ;
      int32 nPixels = rw, nrows;

      SwOftenUnsafe() ;
      if ( !interrupts_clear(allow_interrupt) ) {
        ( void ) report_interrupt(allow_interrupt) ;
        return imcolor_error ;
      }

      values = im_expandread(ime, ims, FALSE, ibbox->x1, yindex, rw, &nrows,
                             expanded_to_plane, 1u);
      if ( values == NULL )
        return imcolor_error ;

      HQASSERT(((uintptr_t)values & 0x1) == 0,
               "Expansion buffer not 16-bit aligned") ;

      /* Check groups of two, treating each group as a single word. Initial
         and trailing entries that are not word-aligned are processed separately.
         This method is only used if there will be enough pixels to align the
         address and then get more than one word of comparison. */
      if ( nPixels >= 5 ) {
        uint32 *values32 ;
        int32 nWords ;

        /* Consume any initial values to get 'values' onto a word boundary. */
        if ( ((uintptr_t)values & 2) != 0 ) {
          if ( ecv != (COLORVALUE)*values )
            return imcolor_different ;
          --nPixels ;
          ++values ;
        }
        HQASSERT(((uintptr_t)values & 3) == 0,
                 "Address alignment failed to align to uint32 boundary") ;

        nWords = nPixels >> 1 ;
        nPixels -= nWords << 1 ;
        HQASSERT(nWords > 0, "Address alignment left too few words") ;

        values32 = (uint32 *)values ;
        do {
          if ( ecv4 != *values32 )
            return imcolor_different ;

          values32 ++ ;
          nWords -- ;
        } while ( nWords > 0 ) ;

        values = (uint16 *)values32 ;
      }

      /* Check the remaining values. */
      for ( ; nPixels > 0 ; nPixels -- ) {
        if ( ecv != (COLORVALUE)*values )
          return imcolor_different ;

        values ++ ;
      }
    }

    break ;
  default:
    HQFAIL("Unsupported/unknown output bits per pixel") ;
    return imcolor_different ;
  }

  return imcolor_same ;
}

/* -------------------------------------------------------------------------- */
static inline Bool pcl_lut_test(const IM_EXPAND *ime, int index, uint16 *color)
{
  HQASSERT(ime->oplanes == 1 || ime->oplanes == 3 || ime->oplanes == 4,
           "Unexpected number of image planes") ;

  if ( ime->ibpp == ime->expibpp ) {
    if ( ime->obpp == 16 ) {
      uint16 *lut ;

      lut = (uint16 *)ime->expluts[0] ;
      *color = lut[index] ;
      if ( ime->oplanes >= 3 ) {
        /* The first three color planes should be the same */
        lut = (uint16 *)ime->expluts[1] ;
        if ( *color != lut[index] )
          return FALSE ;
        lut = (uint16 *)ime->expluts[2] ;
        if ( *color != lut[index] )
          return FALSE ;
        /** \todo ajcd 2013-05-13: Crude hack to determine if we're doing RGB or
            CMYK. Use the fourth (K) plane for the colour, it doesn't need to
            match the first three planes. We should be taking into account the
            cis ordering when doing this. */
        if ( ime->oplanes == 4 ) {
          lut = (uint16 *)ime->expluts[3] ;
          *color = lut[index] ;
        }
      }
    } else {
      uint8 *lut ;
      HQASSERT(ime->obpp == 8, "Unexpected LUT output depth") ;

      lut = (uint8 *)ime->expluts[0] ;
      *color = lut[index] ;
      if ( ime->oplanes >= 3 ) {
        /* The first three color planes should be the same */
        lut = (uint8 *)ime->expluts[1] ;
        if ( *color != lut[index] )
          return FALSE ;
        lut = (uint8 *)ime->expluts[2] ;
        if ( *color != lut[index] )
          return FALSE ;
        /** \todo ajcd 2013-05-13: Crude hack to determine if we're doing RGB or
            CMYK. Use the fourth (K) plane for the colour, it doesn't need to
            match the first three planes. We should be taking into account the
            cis ordering when doing this. */
        if ( ime->oplanes == 4 ) {
          lut = (uint8 *)ime->expluts[3] ;
          *color = lut[index] ;
        }
      }
      *color <<= 8 ;
    }
  } else if ( ime->oplanes == 1 ) {
    uint32 *lut = (uint32 *)ime->expluts[0] ;
    /* Expanded LUTs allowed:
         2x1->2x16
         4x1->4x8
       Since this is a single bit input, we're either picking entry 0 or 1.
       The LUT is packed such that this corresponds to the high bits.
    */
    HQASSERT(ime->ibpp == 1, "Expanded LUT has unexpected input depth") ;
    HQASSERT(ime->expobpp == 32, "Expanded LUT has unexpected output depth") ;
    switch ( ime->expibpp ) {
    case 2: /* 2x1->2x16 */
      *color = (COLORVALUE)(lut[index] >> 16) ;
      break ;
    case 4: /* 4x1->4x8 */
      *color = (COLORVALUE)((lut[index] >> 16) & 0xff00) ;
      break ;
    default:
      HQFAIL("Unexpected expanded input depth") ;
      return FALSE ;
    }
  } else if ( ime->oplanes == 4 ) {
    uint32 *lut ;
    /* Expanded LUTs allowed:
         4x1->4x8
       Since this is a single bit input, we're either picking entry 0 or 1.
       The LUT is packed such that this corresponds to the high bits.
    */
    HQASSERT(ime->ibpp == 1, "Expanded LUT has unexpected input depth") ;
    HQASSERT(ime->expobpp == 32, "Expanded LUT has unexpected output depth") ;
    HQASSERT(ime->expibpp == 4, "Expanded LUT has unexpected input depth") ;

    lut = (uint32 *)ime->expluts[0] ;
    *color = (COLORVALUE)((lut[index] >> 16) & 0xff00) ;
    /* The first three color planes should be the same */
    lut = (uint32 *)ime->expluts[1] ;
    if ( *color != (COLORVALUE)((lut[index] >> 16) & 0xff00) )
      return FALSE ;
    lut = (uint32 *)ime->expluts[2] ;
    if ( *color != (COLORVALUE)((lut[index] >> 16) & 0xff00) )
      return FALSE ;
    /** \todo ajcd 2013-05-13: Crude hack to determine if we're doing RGB or
        CMYK. Use the fourth (K) plane for the colour, it doesn't need to
        match the first three planes. */
    lut = (uint32 *)ime->expluts[3] ;
    *color = (COLORVALUE)((lut[index] >> 16) & 0xff00) ;
  } else
    return FALSE ;

  if ( *color != COLORVALUE_ZERO && *color != COLORVALUE_ONE )
    return FALSE ;

  return TRUE ;
}

/* im_expandmasktest determines if an image and expander is suitable for use as
   a 1 bit mask and also determines the polarity of the mask.  The return value
   indicates whether the expander makes a suitable mask or not.  This routine is
   used by PCL to convert a common ropping idiom over to a masked image. */
Bool im_expandmasktest(DL_STATE *page, const IM_EXPAND *ime, Bool *polarity)
{
  uint16 color0, color1 ;
  int ncolors ;

  UNUSED_PARAM(DL_STATE*, page);

  *polarity = TRUE ; /* initialised arbitrarily */

  /* Check the LUT can be considered suitable for use as a mask.
     Besides 1 bit masks, allow 8 bits as seen in PCL. */
  if ( ime->orig_ibpp != 1 && ime->orig_ibpp != 8 )
    return FALSE ;

  if ( ime->expluts == NULL )
    return FALSE ;

  ncolors = 1 << ime->orig_ibpp ;

  /* There are only two colors in the LUT which need checking and each of these
     colors must have identical components.  This only works because the PCL
     jobs use an indexed space with each color containing identical components
     and PCL uses LCM so the LUT contains the original RGB data. */

  if ( ime->oplanes != 3 && ime->oplanes != 4 )
    if ( ime->oplanes != 1 )
      return FALSE ;

  /* Some PCL images use a LUT that has black and white in the first two
     entries, even though they use 8 bits per pixel. */
  if ( !pcl_lut_test(ime, 0, &color0) ||
       !pcl_lut_test(ime, ncolors - 1, &color1) ||
       (color0 == color1 && ncolors > 2 && !pcl_lut_test(ime, 1, &color1)) ||
       color0 == color1 )
    return FALSE ;

  /* We've checked that the color entries are (0,0,0) or (1,1,1). We just
     need to know which way round they are to determine the mask's
     polarity. */
  if ( color0 == COLORVALUE_ZERO && color1 == COLORVALUE_ONE )
    *polarity = FALSE ;
  else if ( color0 == COLORVALUE_ONE && color1 == COLORVALUE_ZERO )
    *polarity = TRUE ;
  else
    return FALSE ;

  return TRUE ; /* suitable for use as a mask */
}

/* -------------------------------------------------------------------------- */
#if BLIT_WIDTH_BYTES > 4
/** Load a blit word from ordered bytes, reversing it. */
#define BLIT_LOAD_REVERSE(ptr_)                              \
  (((blit_t)(reversed_bits_in_byte[(ptr_)[0]]) << SHIFT8) |   \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[1]]) << SHIFT7) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[2]]) << SHIFT6) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[3]]) << SHIFT5) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[4]]) << SHIFT4) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[5]]) << SHIFT3) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[6]]) << SHIFT2) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[7]]) << SHIFT1))
#else /* BLIT_WIDTH_BYTES <= 4 */
/** Load a blit word from ordered bytes, reversing it. */
#define BLIT_LOAD_REVERSE(ptr_)                              \
  (((blit_t)(reversed_bits_in_byte[(ptr_)[0]]) << SHIFT4) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[1]]) << SHIFT3) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[2]]) << SHIFT2) |    \
   ((blit_t)(reversed_bits_in_byte[(ptr_)[3]]) << SHIFT1))
#endif /* BLIT_WIDTH_BYTES <= 4 */

/* Used for:
 * a) 1-1 images (backwards).
 */
static Bool im_expand1to1b(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x , int32 y , int32 n ,
                            int plane)
{
  blit_t invert, *dst ;
  uint8 *lut ;

  HQASSERT(ime->expluts != NULL, "No LUT for 1 to 1 expansion") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 1, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (blit_t *)expbuf->buf ;
  lut = (uint8 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & BLIT_MASK_BYTES) == 0,
           "Destination isn't blit aligned") ;

  /* Test LUT entries to determine if inverting polarity. The sense of the
     LUT entries in masks is opposite to images. 1-bit input masks always use
     a 4 bit -> 32 bit LUT. */
  HQASSERT(!ime->is_mask || ime->expobpp == 32, "Mask LUT is not 32 bits out") ;
  invert = (ime->is_mask
            ? *(uint32 *)lut != 0 /* 1-bit masks use a 4->32 bit lookup. */
            : ime->obpp == 8
            ? *lut == 0            /* 8-bit image LUT */
            : *(uint16 *)lut == 0) /* 16-bit image LUT */
    ? ALLONES : 0 ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  /* Write the data backwards here. */
  dst += (n + BLIT_MASK_BYTES) >> BLIT_SHIFT_BYTES ;
  HQASSERT((uint8 *)dst <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;
  do {
    int32 sbytes, bytes ;
    uint8 *src ;

    if ( ! im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;
    HQASSERT(bytes % BLIT_WIDTH_BYTES == 0 || n - bytes <= 0,
             "Assuming that tiles are always multiples of blit width.") ;

    if ( bytes > n )
      bytes = n ;

    sbytes = bytes ;

    while ( (bytes -= BLIT_WIDTH_BYTES * 4) >= 0 ) {
      dst -= 4 ;
      PENTIUM_CACHE_LOAD(dst + 3) ;
      dst[3] = invert ^ BLIT_LOAD_REVERSE(&src[0 * BLIT_WIDTH_BYTES]) ;
      dst[2] = invert ^ BLIT_LOAD_REVERSE(&src[1 * BLIT_WIDTH_BYTES]) ;
      dst[1] = invert ^ BLIT_LOAD_REVERSE(&src[2 * BLIT_WIDTH_BYTES]) ;
      dst[0] = invert ^ BLIT_LOAD_REVERSE(&src[3 * BLIT_WIDTH_BYTES]) ;
      src += BLIT_WIDTH_BYTES * 4 ;
    }
    bytes += BLIT_WIDTH_BYTES * 4 ;
    while ( (bytes -= BLIT_WIDTH_BYTES) >= 0 ) {
      dst -= 1 ;
      dst[0] = invert ^ BLIT_LOAD_REVERSE(&src[0 * BLIT_WIDTH_BYTES]) ;
      src += BLIT_WIDTH_BYTES ;
    }
    bytes += BLIT_WIDTH_BYTES ;
    if ( bytes > 0 ) {
      blit_t value = 0 ;
      HQASSERT(n - sbytes == 0, "Should only be doing partial bytes at end of line") ;
      switch ( bytes ) {
#if BLIT_WIDTH_BYTES > 4
      case 7:
        value |= (blit_t)(reversed_bits_in_byte[src[6]]) << SHIFT2 ;
        /*@fallthrough@*/
      case 6:
        value |= (blit_t)(reversed_bits_in_byte[src[5]]) << SHIFT3 ;
        /*@fallthrough@*/
      case 5:
        value |= (blit_t)(reversed_bits_in_byte[src[4]]) << SHIFT4 ;
        /*@fallthrough@*/
      case 4:
        value |= (blit_t)(reversed_bits_in_byte[src[3]]) << SHIFT5 ;
        /*@fallthrough@*/
      case 3:
        value |= (blit_t)(reversed_bits_in_byte[src[2]]) << SHIFT6 ;
        /*@fallthrough@*/
      case 2:
        value |= (blit_t)(reversed_bits_in_byte[src[1]]) << SHIFT7 ;
        /*@fallthrough@*/
      case 1:
        value |= (blit_t)(reversed_bits_in_byte[src[0]]) << SHIFT8 ;
        /*@fallthrough@*/
#else /* BLIT_WIDTH_BYTES <= 4 */
      case 3:
        value |= (blit_t)(reversed_bits_in_byte[src[2]]) << SHIFT2 ;
        /*@fallthrough@*/
      case 2:
        value |= (blit_t)(reversed_bits_in_byte[src[1]]) << SHIFT3 ;
        /*@fallthrough@*/
      case 1:
        value |= (blit_t)(reversed_bits_in_byte[src[0]]) << SHIFT4 ;
        /*@fallthrough@*/
#endif /* BLIT_WIDTH_BYTES <= 4 */
      }
      dst[-1] = invert ^ value ;
    }

    im_storereadrelease(NULL) ;
    x += 8 * sbytes ;
    n -= sbytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)dst >= expbuf->buf,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 1-1 images (forwards).
 */
static Bool im_expand1to1f(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x , int32 y , int32 n ,
                            int plane)
{
  blit_t invert, *dst ;
  uint8 *lut ;

  HQASSERT(ime->expluts != NULL, "No LUT for 1 to 1 expansion") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 1, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (blit_t *)expbuf->buf ;
  lut = (uint8 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & BLIT_MASK_BYTES) == 0,
           "Destination isn't blit aligned") ;

  /* Test LUT entries to determine if inverting polarity. The sense of the
     LUT entries in masks is opposite to images. 1-bit input masks always use
     a 4 bit -> 32 bit LUT. */
  HQASSERT(!ime->is_mask || ime->expobpp == 32, "Mask LUT is not 32 bits out") ;
  invert = (ime->is_mask
            ? *(uint32 *)lut != 0 /* 1-bit masks use a 4->32 bit lookup. */
            : ime->obpp == 8
            ? *lut == 0            /* 8-bit image LUT */
            : *(uint16 *)lut == 0) /* 16-bit image LUT */
    ? ALLONES : 0 ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  /* Write the data forwards here. */

  do {
    int32 sbytes, bytes ;
    uint8 *src ;

    if ( ! im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;
    HQASSERT(bytes % BLIT_WIDTH_BYTES == 0 || n - bytes <= 0,
              "Assuming that tiles are always multiples of blit width.") ;

    if ( bytes > n )
      bytes = n ;

    sbytes = bytes ;

    while ( (bytes -= BLIT_WIDTH_BYTES * 4) >= 0 ) {
      PENTIUM_CACHE_LOAD( dst + 3 ) ;
      dst[0] = invert ^ BLIT_LOAD(&src[0 * BLIT_WIDTH_BYTES]) ;
      dst[1] = invert ^ BLIT_LOAD(&src[1 * BLIT_WIDTH_BYTES]) ;
      dst[2] = invert ^ BLIT_LOAD(&src[2 * BLIT_WIDTH_BYTES]) ;
      dst[3] = invert ^ BLIT_LOAD(&src[3 * BLIT_WIDTH_BYTES]) ;
      dst += 4 ;
      src += BLIT_WIDTH_BYTES * 4 ;
    }
    bytes += BLIT_WIDTH_BYTES * 4 ;
    while ( (bytes -= BLIT_WIDTH_BYTES) >= 0 ) {
      dst[0] = invert ^ BLIT_LOAD(&src[0 * BLIT_WIDTH_BYTES]) ;
      dst += 1 ;
      src += BLIT_WIDTH_BYTES ;
    }
    bytes += BLIT_WIDTH_BYTES ;
    if ( bytes > 0 ) {
      blit_t value = 0 ;
      HQASSERT(n - sbytes == 0, "Should only be doing partial bytes at end of line") ;
      switch ( bytes ) {
#if BLIT_WIDTH_BYTES > 4
      case 7:
        value |= (blit_t)src[6] << SHIFT7 ;
        /*@fallthrough@*/
      case 6:
        value |= (blit_t)src[5] << SHIFT6 ;
        /*@fallthrough@*/
      case 5:
        value |= (blit_t)src[4] << SHIFT5 ;
        /*@fallthrough@*/
      case 4:
        value |= (blit_t)src[3] << SHIFT4 ;
        /*@fallthrough@*/
#endif
      case 3:
        value |= (blit_t)src[2] << SHIFT3 ;
        /*@fallthrough@*/
      case 2:
        value |= (blit_t)src[1] << SHIFT2 ;
        /*@fallthrough@*/
      case 1:
        value |= (blit_t)src[0] << SHIFT1 ;
        /*@fallthrough@*/
      }
      dst[0] = invert ^ value ;
    }

    im_storereadrelease(NULL) ;
    x += 8 * sbytes ;
    n -= sbytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)dst <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 1->8.
 */
static Bool im_expand1to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x , int32 y , int32 n ,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps, corecontext_t *context)
{
  uint8 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 1, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;
  lut = (uint8 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        PENTIUM_CACHE_LOAD( dst + 7 ) ;
        dst[ 0 ] = lut[ ( cval >> 7 ) & 0x01 ] ;
        dst[ 1 ] = lut[ ( cval >> 6 ) & 0x01 ] ;
        dst[ 2 ] = lut[ ( cval >> 5 ) & 0x01 ] ;
        dst[ 3 ] = lut[ ( cval >> 4 ) & 0x01 ] ;
        dst[ 4 ] = lut[ ( cval >> 3 ) & 0x01 ] ;
        dst[ 5 ] = lut[ ( cval >> 2 ) & 0x01 ] ;
        dst[ 6 ] = lut[ ( cval >> 1 ) & 0x01 ] ;
        dst[ 7 ] = lut[ ( cval >> 0 ) & 0x01 ] ;
        dst += 8 ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        dst[ 0 ] = lut[ ( cval >> 7 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 6 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 5 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 4 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 3 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 2 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 1 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 0 ) & 0x01 ] ;
        dst += expanded_comps ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(context) ;
    x += 8 * bytes ; /* 1 sample per input * 8 inputs per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 1->8 four times, expanded LUT to interleaved output.
 */
static Bool im_expand1to8x4(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                             int32 x , int32 y , int32 n ,
                             int plane, unsigned int offset,
                             unsigned int expanded_comps, corecontext_t *context)
{
  uint8 *dst ;
  uint32 *lut ;
  unsigned int expanded_stride ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 1, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;
  lut = (uint32 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)lut & 3) == 0, "LUT not 32-bit aligned") ;

  expanded_stride = expanded_comps * 4 ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;

      HQASSERT(((uintptr_t)dst & 3) == 0,
               "Destination pointer is not 32-bit aligned") ;
      do {
        uint8 cval = src[0] ;
        uint32 *dst32 = (uint32 *)dst ;
        dst32[0] = lut[(cval >> 4) & 0x0f] ;
        dst32[1] = lut[(cval >> 0) & 0x0f] ;
        dst = (uint8 *)(dst32 + 2) ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;
      do {
        uint8 cval = src[ 0 ] ;
        split_32_to_8x4(lut[ ( cval >> 4 ) & 0x0f ], dst, expanded_comps) ;
        dst += expanded_stride ;
        split_32_to_8x4(lut[ ( cval >> 0 ) & 0x0f ], dst, expanded_comps) ;
        dst += expanded_stride ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(context) ;
    x += 8 * bytes ; /* 4 samples per input * 2 inputs per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 1->16.
 */
static Bool im_expand1to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x , int32 y , int32 n ,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps)
{
  uint16 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 1, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  lut = (uint16 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;
  HQASSERT(((uintptr_t)lut & 1) == 0, "LUT is not 16-bit aligned") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        PENTIUM_CACHE_LOAD( dst + 7 ) ;
        dst[ 0 ] = lut[ ( cval >> 7 ) & 0x01 ] ;
        dst[ 1 ] = lut[ ( cval >> 6 ) & 0x01 ] ;
        dst[ 2 ] = lut[ ( cval >> 5 ) & 0x01 ] ;
        dst[ 3 ] = lut[ ( cval >> 4 ) & 0x01 ] ;
        dst[ 4 ] = lut[ ( cval >> 3 ) & 0x01 ] ;
        dst[ 5 ] = lut[ ( cval >> 2 ) & 0x01 ] ;
        dst[ 6 ] = lut[ ( cval >> 1 ) & 0x01 ] ;
        dst[ 7 ] = lut[ ( cval >> 0 ) & 0x01 ] ;
        dst += 8 ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        dst[ 0 ] = lut[ ( cval >> 7 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 6 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 5 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 4 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 3 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 2 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 1 ) & 0x01 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 0 ) & 0x01 ] ;
        dst += expanded_comps ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 8 * bytes ; /* 1 sample per input * 8 inputs per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 1->16 twice, expanded LUT to interleaved output.
 */
static Bool im_expand1to16x2(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                             int32 x , int32 y , int32 n ,
                             int plane, unsigned int offset,
                             unsigned int expanded_comps)
{
  uint16 *dst ;
  uint32 *lut ;
  unsigned int expanded_stride ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 1, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  lut = (uint32 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;
  HQASSERT(((uintptr_t)lut & 3) == 0, "LUT is not 32-bit aligned") ;

  expanded_stride = expanded_comps * 2 ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;

      HQASSERT(((uintptr_t)dst & 3) == 0,
               "Destination pointer is not 32-bit aligned") ;
      do {
        uint8 cval = src[0] ;
        uint32 *dst32 = (uint32 *)dst ;
        dst32[0] = lut[(cval >> 6) & 0x03] ;
        dst32[1] = lut[(cval >> 4) & 0x03] ;
        dst32[2] = lut[(cval >> 2) & 0x03] ;
        dst32[3] = lut[(cval >> 0) & 0x03] ;
        dst = (uint16 *)(dst32 + 4) ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;
      do {
        uint8 cval = src[ 0 ] ;
        split_32_to_16x2(lut[ ( cval >> 6 ) & 0x03 ], dst, expanded_comps) ;
        dst += expanded_stride ;
        split_32_to_16x2(lut[ ( cval >> 4 ) & 0x03 ], dst, expanded_comps) ;
        dst += expanded_stride ;
        split_32_to_16x2(lut[ ( cval >> 2 ) & 0x03 ], dst, expanded_comps) ;
        dst += expanded_stride ;
        split_32_to_16x2(lut[ ( cval >> 0 ) & 0x03 ], dst, expanded_comps) ;
        dst += expanded_stride ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 8 * bytes ; /* 2 samples per input * 4 inputs per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 2->8.
 */
static Bool im_expand2to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x , int32 y , int32 n ,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps)
{
  uint8 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 2, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;
  lut = (uint8 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        PENTIUM_CACHE_LOAD( dst + 3 ) ;
        dst[ 0 ] = lut[ ( cval >> 6 ) & 0x03 ] ;
        dst[ 1 ] = lut[ ( cval >> 4 ) & 0x03 ] ;
        dst[ 2 ] = lut[ ( cval >> 2 ) & 0x03 ] ;
        dst[ 3 ] = lut[ ( cval >> 0 ) & 0x03 ] ;
        dst += 4 ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        dst[ 0 ] = lut[ ( cval >> 6 ) & 0x03 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 4 ) & 0x03 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 2 ) & 0x03 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 0 ) & 0x03 ] ;
        dst += expanded_comps ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 4 * bytes ; /* 1 sample per input * 4 inputs per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 2->8 four times, expanded LUT to interleaved output.
 */
static Bool im_expand2to8x4(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                             int32 x , int32 y , int32 n ,
                             int plane, unsigned int offset,
                             unsigned int expanded_comps)
{
  uint8 *dst ;
  uint32 *lut ;
  unsigned int expanded_stride ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 2, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;
  lut = (uint32 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)lut & 3) == 0, "LUT is not 32-bit aligned") ;

  expanded_stride = expanded_comps * 4 ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;
      HQASSERT(((uintptr_t)dst & 3) == 0,
               "Destination pointer is not 32-bit aligned") ;
      do {
        uint32 *dst32 = (uint32 *)dst ;
        dst32[0] = lut[src[0]] ;
        dst = (uint8 *)(dst32 + 1) ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;
      do {
        uint8 cval = src[ 0 ] ;
        split_32_to_8x4(lut[ cval ], dst, expanded_comps) ;
        dst += expanded_stride ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 4 * bytes ; /* 4 samples per input * 1 input per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 2->16.
 */
static Bool im_expand2to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x , int32 y , int32 n ,
                            int plane, unsigned int offset,
                            unsigned int expanded_comps)
{
  uint16 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 2,
           "Expander depth used with multi-bit input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  lut = (uint16 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;
  HQASSERT(((uintptr_t)lut & 1) == 0, "LUT is not 16-bit aligned") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        PENTIUM_CACHE_LOAD( dst + 3 ) ;
        dst[ 0 ] = lut[ ( cval >> 6 ) & 0x03 ] ;
        dst[ 1 ] = lut[ ( cval >> 4 ) & 0x03 ] ;
        dst[ 2 ] = lut[ ( cval >> 2 ) & 0x03 ] ;
        dst[ 3 ] = lut[ ( cval >> 0 ) & 0x03 ] ;
        dst += 4 ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        dst[ 0 ] = lut[ ( cval >> 6 ) & 0x03 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 4 ) & 0x03 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 2 ) & 0x03 ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 0 ) & 0x03 ] ;
        dst += expanded_comps ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 4 * bytes ; /* 1 sample per input * 4 inputs per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 2x2->2x16.
 */
static Bool im_expand2to16x2(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                              int32 x , int32 y , int32 n ,
                              int plane, unsigned int offset,
                              unsigned int expanded_comps)
{
  uint16 *dst ;
  uint32 *lut ;
  unsigned int expanded_stride ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 2,
           "Expander depth used with multi-bit input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  lut = (uint32 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;
  HQASSERT(((uintptr_t)lut & 3) == 0, "LUT is not 32-bit aligned") ;

  expanded_stride = expanded_comps * 2 ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;
      HQASSERT(((uintptr_t)dst & 3) == 0,
               "Destination pointer is not 32-bit aligned") ;
      do {
        uint8 cval = src[ 0 ] ;
        uint32 *dst32 = (uint32 *)dst ;
        dst32[0] = lut[(cval >> 4) & 0x0f] ;
        dst32[1] = lut[(cval >> 0) & 0x0f] ;
        dst = (uint16 *)(dst32 + 2) ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;
      do {
        uint8 cval = src[ 0 ] ;
        split_32_to_16x2(lut[ ( cval >> 4 ) & 0x0f ], dst, expanded_comps) ;
        dst += expanded_stride ;
        split_32_to_16x2(lut[ ( cval >> 0 ) & 0x0f ], dst, expanded_comps) ;
        dst += expanded_stride ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 4 * bytes ; /* 2 samples per input * 2 inputs per bytes */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 4->8.
 */
static Bool im_expand4to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x , int32 y , int32 n ,
                           int plane, unsigned int offset,
                           unsigned int expanded_comps)
{
  uint8 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 4, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;
  lut = (uint8 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        dst[ 0 ] = lut[ ( cval >> 4 ) & 0x0f ] ;
        dst[ 1 ] = lut[ ( cval >> 0 ) & 0x0f ] ;
        dst += 2 ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        dst[ 0 ] = lut[ ( cval >> 4 ) & 0x0f ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 0 ) & 0x0f ] ;
        dst += expanded_comps ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 2 * bytes ; /* 1 sample per input * 2 input per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 4->8 two samples together.
 */
static Bool im_expand4to8x2(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x , int32 y , int32 n ,
                            int plane, unsigned int offset,
                            unsigned int expanded_comps)
{
  uint8 *dst ;
  uint16 *lut ;
  unsigned int expanded_stride ;

  HQASSERT(expanded_comps > 1, "Should not be used for planar expansion") ;
  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 4, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;
  lut = (uint16 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)lut & 1) == 0, "LUT not 16-bit aligned") ;

  expanded_stride = expanded_comps * 2 ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes, nbytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    nbytes = bytes ;
    do {
      split_16_to_8x2(lut[src[0]], dst, expanded_comps) ;
      dst += expanded_stride ;
      src += 1 ;
    } while ( --nbytes > 0 ) ;

    im_storereadrelease(NULL) ;
    x += 2 * bytes ; /* 2 samples per input / 1 bytes per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 4->16.
 */
static Bool im_expand4to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x , int32 y , int32 n ,
                            int plane, unsigned int offset,
                            unsigned int expanded_comps)
{
  uint16 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 4, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  lut = (uint16 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;
  HQASSERT(((uintptr_t)lut & 1) == 0, "LUT is not 16-bit aligned") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        dst[ 0 ] = lut[ ( cval >> 4 ) & 0x0f ] ;
        dst[ 1 ] = lut[ ( cval >> 0 ) & 0x0f ] ;
        dst += 2 ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;

      do {
        uint8 cval = src[ 0 ] ;
        dst[ 0 ] = lut[ ( cval >> 4 ) & 0x0f ] ;
        dst += expanded_comps ;
        dst[ 0 ] = lut[ ( cval >> 0 ) & 0x0f ] ;
        dst += expanded_comps ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 2 * bytes ; /* 1 sample per input * 2 inputs per byte */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 4->16 two samples together.
 */
static Bool im_expand4to16x2(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                              int32 x , int32 y , int32 n ,
                              int plane, unsigned int offset,
                              unsigned int expanded_comps)
{
  uint16 *dst ;
  uint32 *lut ;
  unsigned int expanded_stride ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 4, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  lut = (uint32 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;
  HQASSERT(((uintptr_t)lut & 3) == 0, "LUT is not 32-bit aligned") ;

  expanded_stride = expanded_comps * 2 ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Planar output */
      int32 nbytes = bytes ;
      HQASSERT(((uintptr_t)dst & 3) == 0,
               "Destination pointer is not 32-bit aligned") ;
      do {
        uint32 *dst32 = (uint32 *)dst ;
        dst32[0] = lut[src[0]] ;
        dst = (uint16 *)(dst32 + 1) ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    } else { /* Interleaved output */
      int32 nbytes = bytes ;
      do {
        split_32_to_16x2(lut[src[0]], dst, expanded_comps) ;
        dst += expanded_stride ;
        src += 1 ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += 2 * bytes ; /* 2 samples per input * 1 byte per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 8->8 color converted data.
 */
static Bool im_expand8to8nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                               int32 x, int32 y, int32 n,
                               int plane,
                               unsigned int offset,
                               unsigned int expanded_comps, corecontext_t *context)
{
  uint8 *dst ;

  UNUSED_PARAM(IM_EXPAND *, ime) ;

  HQASSERT(ime->expluts == NULL || ims == ime->ims_alpha,
           "lut should be NULL") ;
  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Invalid plane") ;
  HQASSERT(ime->ibpp == 8, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Direct mapping input to output. */
      HqMemCpy(dst, src, bytes) ;
      dst += bytes ;
    } else { /* Distribute to output channels */
      int32 nbytes = bytes ;
      do { /* Let compiler unroll this loop unless proven not good enough. */
        dst[0] = src[0] ;
        src += 1 ;
        dst += expanded_comps ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(context) ;
    x += bytes ; /* 1 samples per input * 1 byte per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 8->8 independent data.
 */
static Bool im_expand8to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                          int32 x, int32 y, int32 n,
                          int plane,
                          unsigned int offset,
                          unsigned int expanded_comps, corecontext_t *context)
{
  uint8 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 8, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;
  lut = (uint8 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Direct mapping input to output. */
      int32 nbytes = bytes ;

      while ( (nbytes -= 8) >= 0 ) {
        PENTIUM_CACHE_LOAD(dst + 7) ;
        dst[0] = lut[src[0]] ;
        dst[1] = lut[src[1]] ;
        dst[2] = lut[src[2]] ;
        dst[3] = lut[src[3]] ;
        dst[4] = lut[src[4]] ;
        dst[5] = lut[src[5]] ;
        dst[6] = lut[src[6]] ;
        dst[7] = lut[src[7]] ;
        dst += 8 ;
        src += 8 ;
      }
      nbytes += 8 ;
      while ( --nbytes >= 0 ) {
        dst[0] = lut[src[0]] ;
        dst += 1 ;
        src += 1 ;
      }
    } else { /* Distribute to output channels */
      int32 nbytes = bytes ;
      do { /* Let compiler unroll this loop unless proven not good enough. */
        dst[0] = lut[src[0]] ;
        src += 1 ;
        dst += expanded_comps ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(context) ;
    x += bytes ; /* 1 sample per input * 1 byte per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 8->16 color converted data.
 */
static Bool im_expand8to16nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                int32 x, int32 y, int32 n,
                                int plane,
                                unsigned int offset,
                                unsigned int expanded_comps)
{
  uint16 *dst ;

  UNUSED_PARAM(IM_EXPAND *, ime) ;

  HQASSERT(ime->expluts == NULL || ims == ime->ims_alpha,
           "lut should be NULL") ;
  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Invalid plane") ;
  HQASSERT(ime->ibpp == 8, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;

  do {
    int32 bytes, nbytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    nbytes = bytes ;
    do { /* Let compiler unroll this loop unless proven not good enough. */
      dst[0] = (COLORVALUE)(src[0] << 8) ;
      src += 1 ;
      dst += expanded_comps ;
    } while ( --nbytes > 0 ) ;

    im_storereadrelease(NULL) ;
    x += bytes ;
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 8->16.
 * b) 2x4->2x8
 */
static Bool im_expand8to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x, int32 y, int32 n,
                           int plane,
                           unsigned int offset,
                           unsigned int expanded_comps)
{
  uint16 *dst, *lut ;
  int32 samplesperbyte ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 8 || ime->ibpp == 4,
           "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  lut = (uint16 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  samplesperbyte = 8 / ime->ibpp ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;
  HQASSERT(((uintptr_t)lut & 1) == 0, "LUT is not 16-bit aligned") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *src ;

    if ( !im_storeread(ims, x, y, plane, &src, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    if ( expanded_comps == 1 ) { /* Direct mapping input to output. */
      int32 nbytes = bytes ;

      while ( (nbytes -= 8) >= 0 ) {
        PENTIUM_CACHE_LOAD(dst + 7) ;
        dst[0] = lut[src[0]] ;
        dst[1] = lut[src[1]] ;
        dst[2] = lut[src[2]] ;
        dst[3] = lut[src[3]] ;
        dst[4] = lut[src[4]] ;
        dst[5] = lut[src[5]] ;
        dst[6] = lut[src[6]] ;
        dst[7] = lut[src[7]] ;
        dst += 8 ;
        src += 8 ;
      }
      nbytes += 8 ;
      while ( --nbytes >= 0 ) {
        dst[0] = lut[src[0]] ;
        dst += 1 ;
        src += 1 ;
      }
    } else { /* Distribute to output channels */
      int32 nbytes = bytes ;
      do { /* Let compiler unroll this loop unless proven not good enough. */
        dst[0] = lut[src[0]] ;
        src += 1 ;
        dst += expanded_comps ;
      } while ( --nbytes > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += bytes * samplesperbyte ; /* samples per input * bytes per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 16->8 alpha channel.
 */
static Bool im_expand16to8nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                int32 x, int32 y, int32 n,
                                int plane,
                                unsigned int offset,
                                unsigned int expanded_comps)
{
  uint8 *dst ;

  UNUSED_PARAM(IM_EXPAND *, ime) ;

  HQASSERT(ime->expluts == NULL || ims == ime->ims_alpha,
           "lut should be NULL") ;
  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Invalid plane") ;
  HQASSERT(16 == im_storebpp(ims), "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;

  do {
    int32 bytes, nbytes ;
    uint8 *tsrc ;
    uint16 *src ;

    if ( !im_storeread(ims, x, y, plane, &tsrc, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    HQASSERT(((uintptr_t)tsrc & 1) == 0,
             "Source data is not 16-bit aligned") ;
    HQASSERT((bytes & 1) == 0, "Input bytes not a multiple of 2") ;
    src = (uint16 *)tsrc ;

    nbytes = bytes ;
    do { /* Let compiler unroll this loop unless proven not good enough. */
      dst[0] = COLORVALUE_TO_UINT8(src[0]) ;
      src += 1 ;
      dst += expanded_comps ;
    } while ( (nbytes -= 2) > 0 ) ;

    im_storereadrelease(NULL) ;
    x += bytes >> 1 ; /* 1 samples per input / 2 bytes per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Used for:
 * a) 12->8.
 */
static Bool im_expand16to8(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                           int32 x, int32 y, int32 n,
                           int plane,
                           unsigned int offset,
                           unsigned int expanded_comps)
{
  uint8 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 12 || ime->ibpp == 16,
           "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;
  lut = (uint8 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *tsrc ;
    uint16 *src ;

    if ( !im_storeread(ims, x, y, plane, &tsrc, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    HQASSERT(((uintptr_t)tsrc & 1) == 0,
             "Source data is not 16-bit aligned") ;
    HQASSERT((bytes & 1) == 0, "Input bytes not a multiple of 2") ;
    src = (uint16 *)tsrc ;

    if ( expanded_comps == 1 ) { /* Direct mapping input to output. */
      int32 nbytes = bytes ;

      while ( (nbytes -= 16) >= 0 ) {
        PENTIUM_CACHE_LOAD(dst + 7) ;
        dst[0] = lut[src[0]] ;
        dst[1] = lut[src[1]] ;
        dst[2] = lut[src[2]] ;
        dst[3] = lut[src[3]] ;
        dst[4] = lut[src[4]] ;
        dst[5] = lut[src[5]] ;
        dst[6] = lut[src[6]] ;
        dst[7] = lut[src[7]] ;
        dst += 8 ;
        src += 8 ;
      }
      nbytes += 16 ;
      while ( (nbytes -= 2) >= 0 ) {
        dst[0] = lut[src[0]] ;
        dst += 1 ;
        src += 1 ;
      }
    } else { /* Distribute to output channels */
      int32 nbytes = bytes ;
      do { /* Let compiler unroll this loop unless proven not good enough. */
        dst[0] = lut[src[0]] ;
        src += 1 ;
        dst += expanded_comps ;
      } while ( (nbytes -= 2) > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += bytes >> 1 ; /* 1 samples per input / 2 bytes per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 16->16 color converted data.
 */
static Bool im_expand16to16nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                 int32 x, int32 y, int32 n,
                                 int plane,
                                 unsigned int offset,
                                 unsigned int expanded_comps)
{
  uint16 *dst ;

  UNUSED_PARAM(IM_EXPAND *, ime) ;

  HQASSERT(ime->expluts == NULL || ims == ime->ims_alpha,
           "lut should be NULL") ;
  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Invalid plane") ;
  HQASSERT(16 == im_storebpp(ims), "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;

  do {
    int32 bytes ;
    uint8 *tsrc ;
    uint16 *src ;

    if ( !im_storeread(ims, x, y, plane, &tsrc, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    HQASSERT(((uintptr_t)tsrc & 1) == 0,
             "Source data is not 16-bit aligned") ;
    HQASSERT((bytes & 1) == 0, "Input bytes not a multiple of 2") ;
    src = (uint16 *)tsrc ;

    if ( expanded_comps == 1 ) { /* Direct mapping input to output. */
      HqMemCpy(dst, src, bytes) ;
      dst += bytes >> 1 ;
    } else { /* Distribute to output channels */
      int32 nbytes = bytes ;
      do { /* Let compiler unroll this loop unless proven not good enough. */
        dst[0] = src[0] ;
        src += 1 ;
        dst += expanded_comps ;
      } while ( (nbytes -= 2) > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += bytes >> 1 ; /* 1 samples per input / 2 bytes per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 16->16 independent data.
 */
static Bool im_expand16to16(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                            int32 x, int32 y, int32 n,
                            int plane,
                            unsigned int offset,
                            unsigned int expanded_comps)
{
  uint16 *dst, *lut ;

  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Plane doesn't have LUT") ;
  HQASSERT(ime->ibpp == 16 || ime->ibpp == 12,
           "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  lut = (uint16 *)ime->expluts[plane] ;
  HQASSERT(lut != NULL, "Plane doesn't have LUT") ;

  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination is not 16-bit aligned") ;
  HQASSERT(((uintptr_t)lut & 1) == 0, "LUT is not 16-bit aligned") ;

  if ( ime->iplanes == 1 )
    plane = 0 ;

  do {
    int32 bytes ;
    uint8 *tsrc ;
    uint16 *src ;

    if ( !im_storeread(ims, x, y, plane, &tsrc, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    HQASSERT(((uintptr_t)tsrc & 1) == 0,
             "Source data is not 16-bit aligned") ;
    HQASSERT((bytes & 1) == 0, "Input bytes not a multiple of 2") ;
    src = (uint16 *)tsrc ;

    if ( expanded_comps == 1 ) { /* Direct mapping input to output. */
      int32 nbytes = bytes ;

      while ( (nbytes -= 16) >= 0 ) {
        PENTIUM_CACHE_LOAD(dst + 7) ;
        dst[0] = lut[src[0]] ;
        dst[1] = lut[src[1]] ;
        dst[2] = lut[src[2]] ;
        dst[3] = lut[src[3]] ;
        dst[4] = lut[src[4]] ;
        dst[5] = lut[src[5]] ;
        dst[6] = lut[src[6]] ;
        dst[7] = lut[src[7]] ;
        dst += 8 ;
        src += 8 ;
      }
      nbytes += 16 ;
      while ( (nbytes -= 2) >= 0 ) {
        dst[0] = lut[src[0]] ;
        dst += 1 ;
        src += 1 ;
      }
    } else { /* Distribute to output channels */
      int32 nbytes = bytes ;
      do { /* Let compiler unroll this loop unless proven not good enough. */
        dst[0] = lut[src[0]] ;
        src += 1 ;
        dst += expanded_comps ;
      } while ( (nbytes -= 2) > 0 ) ;
    }

    im_storereadrelease(NULL) ;
    x += bytes >> 1 ; /* 1 samples per input / 2 bytes per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 32->8 color converted data.
 */
static Bool im_expand32to8nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                int32 x, int32 y, int32 n,
                                int plane,
                                unsigned int offset,
                                unsigned int expanded_comps)
{
  uint8 *dst ;

  UNUSED_PARAM(IM_EXPAND *, ime) ;

  HQASSERT(ime->expluts == NULL, "lut should be NULL") ;
  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Invalid plane") ;
  HQASSERT(ime->ibpp == 32, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint8 *)expbuf->buf + offset ;

  do {
    int32 bytes, nbytes ;
    uint8 *tsrc ;
    float *src ;

    if ( !im_storeread(ims, x, y, plane, &tsrc, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    HQASSERT(((uintptr_t)tsrc & 3) == 0,
             "Source data is not 32-bit aligned") ;
    HQASSERT((bytes & 3) == 0, "Input bytes not a multiple of 4") ;
    src = (float *)tsrc ;

    nbytes = bytes ;
    do { /* Let compiler unroll this loop unless proven not good enough. */
      dst[0] = (uint8)(src[0] * 255 + 0.5f) ;
      src += 1 ;
      dst += expanded_comps ;
    } while ( (nbytes -= 4) > 0 ) ;

    im_storereadrelease(NULL) ;
    x += bytes >> 2 ; /* 1 samples per input / 4 bytes per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* Used for:
 * a) 32->16 color converted data.
 */
static Bool im_expand32to16nolut(IM_EXPAND *ime, IM_EXPBUF *expbuf, IM_STORE *ims,
                                 int32 x, int32 y, int32 n,
                                 int plane,
                                 unsigned int offset,
                                 unsigned int expanded_comps)
{
  uint16 *dst ;

  UNUSED_PARAM(IM_EXPAND *, ime) ;

  HQASSERT(ime->expluts == NULL, "lut should be NULL") ;
  HQASSERT(offset < expanded_comps, "Expanded channel out of range") ;
  HQASSERT(plane >= 0 && plane < ime->lplanes, "Invalid plane") ;
  HQASSERT(ime->ibpp == 32, "Expander used with wrong depth input") ;
  HQASSERT(n > 0, "No expansion required") ;

  dst = (uint16 *)expbuf->buf + offset ;
  HQASSERT(((uintptr_t)dst & 1) == 0, "Destination not 16-bit aligned") ;

  do {
    int32 bytes, nbytes ;
    uint8 *tsrc ;
    float *src ;

    if ( !im_storeread(ims, x, y, plane, &tsrc, &bytes) )
      return FALSE ;

    HQASSERT(bytes > 0, "Should have read something from store") ;

    if ( bytes > n )
      bytes = n ;

    HQASSERT(((uintptr_t)tsrc & 3) == 0,
             "Source data is not 32-bit aligned") ;
    HQASSERT((bytes & 3) == 0, "Input bytes not a multiple of 4") ;
    src = (float *)tsrc ;

    nbytes = bytes ;
    do { /* Let compiler unroll this loop unless proven not good enough. */
      dst[0] = FLOAT_TO_COLORVALUE(src[0]) ;
      src += 1 ;
      dst += expanded_comps ;
    } while ( (nbytes -= 4) > 0 ) ;

    im_storereadrelease(NULL) ;
    x += bytes >> 2 ; /* 1 samples per input / 4 bytes per input */
    n -= bytes ;
  } while ( n > 0 ) ;

  HQASSERT((uint8 *)(dst - expanded_comps + 1) <= expbuf->buf + expbuf->size,
           "Overran image expansion buffer") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
void *im_expand_decode_array(const IM_EXPAND *ime)
{
  return (void*)ime->expluts;
}

/* -------------------------------------------------------------------------- */
Bool im_expandlutexists( IM_EXPAND *ime , int32 plane )
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  HQASSERT( plane >= 0 , "im_expandlutexists: plane < 0" ) ;
  return plane < ime->lplanes && ime->expluts[ plane ] != NULL;
}

/* -------------------------------------------------------------------------- */
/* Map an expand plane index to a store plane index. */
int32 im_expandadjustplane(IM_EXPAND* ime, int32 i)
{
  int32 plane;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);

  if (ime->expluts && ime->iplanes == 1)
    /* Four luts but only one image plane.  Each lut plane uses
       the same image plane data.  This is used for gray->cmyk. */
    plane = 0;
  else
    plane = i;

  return plane;
}

/* -------------------------------------------------------------------------- */
int32 im_expandiplanes( IM_EXPAND *ime )
{
  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  return ime->iplanes;
}

/* -------------------------------------------------------------------------- */
void im_expandpresepread( IM_EXPAND *ime ,
                          Bool fSubtractive ,
                          float *buf ,
                          int32 plane )
{
  uint16 *lut ;
  int32 ncolors ;
  int32 j ;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  HQASSERT( buf != NULL , "im_expandpresepread: buf NULL" ) ;
  HQASSERT( plane >= 0 && plane < ime->lplanes ,
            "im_expandpresepread: plane out of range" ) ;
  HQASSERT( ime->expobpp == 16 , "Expected 16 bit lut" ) ;

  lut = (uint16 *)ime->expluts[ plane ];
  HQASSERT( lut != NULL , "im_expandpresepread: lut NULL" ) ;

  ncolors = 1 << ime->orig_ibpp ;
  HQASSERT( ncolors >= 1 , "im_expandpresepread: ncolors < 1" ) ;

  for ( j = 0 ; j < ncolors ; ++j ) {
    HQASSERT(lut[j] <= COLORVALUE_MAX, "lut out of bounds");
    /* Presep colors are additive. */
    if ( fSubtractive )
      buf[ j ] = 1.0f - ( float )((SYSTEMVALUE) lut[ j ] * COLORVALUE_INVERSE) ;
    else
      buf[ j ] = ( float )((SYSTEMVALUE) lut[ j ] * COLORVALUE_INVERSE) ;
  }
}

/* -------------------------------------------------------------------------- */
/* im_expanduniform determines if a store contains uniform data (i.e. every
   pixel is the same).  It only tests images whose width and height are <=
   to maxwidth and maxheight.  The resulting uniform color is returned in
   the colorvalue buffer providing the buffer is sufficiently long. */
Bool im_expanduniform(IM_EXPAND *ime, IM_STORE *ims,
                      COLORVALUE *buf, int32 buflen)
{
  int32 plane;
  uint16 uniformcolor;

  if ( buflen < ime->oplanes ) {
    HQFAIL("buf too short - cannot attempt uniform expansion");
    return FALSE;
  }

  if ( ime->expluts && ime->obpp != 16 )
    return FALSE;

  if ( !ime->expluts ) {
    for ( plane = 0; plane < ime->oplanes; ++plane ) {
      if ( !im_store_is_uniform(ims, plane, &uniformcolor) )
        return FALSE;

      if ( ime->obpp == 8 )
        uniformcolor <<= 8 ;

      buf[plane] = (COLORVALUE)uniformcolor;
    }
  }
  else {
    for ( plane = 0; plane < ime->oplanes; ++plane ) {
      if ( !im_store_is_uniform(ims, (ime->iplanes == 1 ? 0 : plane),
                                &uniformcolor) )
        return FALSE;

      if ( ime->obpp == 16 && ime->expobpp == 16 ) {
        buf[plane] = ((COLORVALUE*)ime->expluts[plane])[uniformcolor];
      } else if ( ime->obpp == 8 && ime->expobpp == 8 ) {
        buf[plane] = ((uint8*)ime->expluts[plane])[uniformcolor] << 8;
      } else {
#ifdef ASSERT_BUILD
        static int32 count = 0 ;
#endif
        HQTRACE(count++ == 0, ("NYI uniform test ibpp %d expibpp %d obpp %d expobpp %d",
                               ime->ibpp, ime->expibpp, ime->obpp, ime->expobpp)) ;
      }
    }
  }

  return TRUE;
}

/*---------------------------------------------------------------------------*/

Bool im_expandtolut(DL_STATE *page, IM_EXPAND *ime, GS_COLORinfo *colorInfo,
                    COLORANTINDEX ci)
{
  int32 i, j ;
  Bool added;

  /* This static is not used, except to generate a unique address to
     differentiate this modified LUTs set from all normal LUTs. The
     decode address is just used as a cookie in the LUT comparisons, it is
     never dereferenced. */
  static int magic_decode_cookie ;

  HQASSERT(ime->expluts == NULL, "Image already has LUTs") ;
  HQASSERT(ime->format == ime_colorconverted ||
           ime->format == ime_as_is_decoded,
           "Converting wrong image expander type to LUTs") ;
  HQASSERT(ime->oplanes <= ime->lplanes, "Not enough planes in expander") ;

  ime->lutsize = (1u << ime->orig_ibpp) * ime->obpp / 8 ;
  ime->iplanes = 1 ; /* We've already shuffled down the store plane. */
  ime->format = ime_planar ;

  if ( imlut_lookup(colorInfo, GSC_IMAGE, NULL /*decode_array*/,
                    &magic_decode_cookie, ime->oplanes, ime->orig_ibpp,
                    FALSE /*expanded*/, &ime->expluts) )
    return TRUE ;

  /* Allocate all the image LUT memory. */
  if ( !im_expand_alloc_expluts(page, ime, 0) )
    return FALSE;

  for ( j = 0 ; j < ime->lplanes ; ++j ) {
    if ( ime->cis[j] == ci ) { /* Identity LUT */
      if ( ime->obpp == 8 ) {
        uint8 *lut = (uint8 *)ime->expluts[j] ;
        for ( i = 0 ; i < 1 << ime->orig_ibpp ; ++i )
          lut[i] = CAST_SIGNED_TO_UINT8(i) ;
      } else {
        uint16 *lut = (uint16 *)ime->expluts[j] ;
        HQASSERT(ime->obpp == 16, "Unexpected expander output depth") ;
        for ( i = 0 ; i < 1 << ime->orig_ibpp ; ++i )
          lut[i] = CAST_SIGNED_TO_UINT16(i) ;
      }
    } else {
      /* Set the LUT to no colorant (additive). */
      HqMemSet8(ime->expluts[j], 0xff, ime->lutsize) ;
    }
  }

  /* Created a new image LUT, now make it available for sharing. */
  imlut_add(page, colorInfo, GSC_IMAGE, NULL /*decode_array*/,
            &magic_decode_cookie, ime->oplanes, ime->orig_ibpp,
            FALSE /*expanded*/, ime->expluts, &added);

  /* Ownership of the image LUT has been handed over to the imlut. */
  if ( added )
    ime->own_expluts = FALSE;

  return TRUE ;
}

/*---------------------------------------------------------------------------*/

/* Return a blit mapping for extracting the on-the-fly converted data
   (for separation omission) and some ancillary data. */
void im_extract_blit_mapping(IM_EXPAND *ime,
                             int expanded_to_plane[],
                             unsigned int *expanded_comps,
                             unsigned int *converted_comps,
                             size_t *converted_bits,
                             COLORANTINDEX **output_colorants)
{
  channel_index_t i ;

  /* Expand all channels from the image for the color conversion, except
     alpha channels */
  *expanded_comps = ime->lplanes;
  /** \todo ppp 2009-12-22: This won't be right when we allow /Alpha as
      an output channel. */
  if ( ime->cis[ime->lplanes-1] == COLORANTINDEX_ALPHA )
    --*expanded_comps;
  for ( i = 0 ; i < *expanded_comps ; ++i )
    expanded_to_plane[i] = (int)i;
  *converted_comps = ime->otf.planes;
  *converted_bits = ime->otf.obpp;
  *output_colorants = ime->otf.output_colorants;
  return;
}


/* Prepare template color for image expansion, and create mappings. */
void im_expand_blit_mapping(const render_blit_t *rb,
                            IM_EXPAND *ime,
                            int expanded_to_plane[],
                            unsigned int *expanded_comps,
                            unsigned int *converted_comps,
                            int blit_to_expanded[])
{
  const blit_colormap_t *map ;
  channel_index_t i ;
  blit_color_t *color ;

  VERIFY_OBJECT(ime, IM_EXPAND_NAME);
  HQASSERT(ime->cis, "Image expander needs colorant indices") ;
  HQASSERT(rb != NULL, "No render state") ;
  HQASSERT(expanded_to_plane != NULL, "Nowhere to put plane mappings") ;
  HQASSERT(blit_to_expanded != NULL, "Nowhere to put blit mappings") ;
  HQASSERT(expanded_comps != NULL, "Nowhere to put expanded size") ;

  /* Prepare the blit color in the same way that blit_color_unpack() does.
     We're going to stuff in values from the expanders, but these values are
     already quantised. It's not clear whether we should always reverse the
     quantisation to generate a valid unpacked color, or whether we should
     just create a dummy unpacked color, or even leave it invalid and assume
     that the lower levels never need to look at it (this probably isn't a
     good assumption for trapping). */

  color = rb->color ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  if ( rb->p_ri->region_set_type == RENDER_REGIONS_DIRECT ) {
    if ( ime->ime_direct != NULL ) {
      /* Switch to the alternate ime for direct regions. */
      ime = ime->ime_direct;
    } else if ( im_converting_on_the_fly(ime) ) {
      int output_channels[BLIT_MAX_CHANNELS];
      unsigned int nexpanded_after_conversion;

      HQASSERT(ri_converting_on_the_fly(rb->p_ri),
               "Setting up on-the-fly conversion, but not converting?");
      /* Expand all channels from the image for the color conversion, except
         alpha channels */
      *expanded_comps = ime->lplanes;
      /** \todo ppp 2009-12-22: This won't be right when we allow /Alpha as
          an output channel. */
      if ( ime->cis[ime->lplanes-1] == COLORANTINDEX_ALPHA )
        --*expanded_comps;
      for ( i = 0 ; i < *expanded_comps ; ++i )
        expanded_to_plane[i] = (int)i;
      *converted_comps = ime->otf.planes;
      /* Set up the mapping from imc output to blit color. */
      blit_expand_mapping(color,
                          ime->otf.output_colorants, ime->otf.planes,
                          TRUE,
                          output_channels, &nexpanded_after_conversion,
                          blit_to_expanded);
      if ( nexpanded_after_conversion == 0 ) { /* knockout */
        *expanded_comps = 0; return; /* skip all conversion */
      }
      /* Collapse the mapping into blit_to_expanded, as the output will be
         processed by the pixel extractor directly. */
      for ( i = 0 ; i < map->nchannels ; ++i ) {
        int eindex = blit_to_expanded[i];
        if ( eindex >= 0 ) {
          HQASSERT((unsigned int)eindex < nexpanded_after_conversion,
                   "Expanded index out of range");
          blit_to_expanded[i] = output_channels[eindex];
        }
      }
      return;
    }
  }

  blit_expand_mapping(color, ime->cis, ime->lplanes, TRUE,
                      expanded_to_plane, expanded_comps, blit_to_expanded);
  *converted_comps = *expanded_comps;
  HQASSERT(*expanded_comps <= (unsigned)ime->lplanes,
           "Expanded data larger than space allocated for it") ;
}


/** Last image in the current offer. */
static IMAGEOBJECT *last_image_in_offer;


/** Solicit method of the imexpand low-memory handler.
 *
 * The actual release mechanism is quite involved and involves two passes
 * through the list of images and relying on reference counts. So the solicit
 * is a simplified approximation. It assumes that a reference count of n
 * occurs because there are n-1 other uses of the same buffer in the list.
 * So it gets each one to have a 1/n effect on the freed total. It also tracks
 * the max buffer size as it goes, and assumes this is the one that will not
 * be freed but shared amongst the other ones.
 *
 * Note : This is marked as thread-safe because low-mem imexpand purges are not
 * allowed at render time. They are prevented by the im_purge_allowed flag.
 * This means we can only ever have to deal with a page being interpreted,
 * and this can only happen from one thread at the moment. Thus no mutex
 * locking is required. If this restriction is to be lifted, a mutex will be
 * required to ensure there is no threading contention.
 */
static low_mem_offer_t *im_expand_lowmem_solicit(low_mem_handler_t *handler,
                                                 corecontext_t *context,
                                                 size_t count,
                                                 memory_requirement_t* requests)
{
  static low_mem_offer_t offer;
  DL_STATE *page;
  IM_EXPBUF *cipbuf;
  IMAGEOBJECT *image;
  size_t bytes2free = 0, max2free = 0;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count);
  UNUSED_PARAM(memory_requirement_t*, requests);

  page = context->page;
  HQASSERT(page, "No DL page");
  if ( !page->im_purge_allowed )
    return NULL;
  /* If nothing has changed since the last solicit, remake the offer. */
  if ( last_image_in_offer == page->im_list && last_image_in_offer != NULL )
    return &offer;
  cipbuf = page->im_expbuf_shared;

  for ( image = page->im_list ; image != NULL ; image = image->next ) {
    IM_EXPAND *ime = image->ime ;
    if ( ime != NULL ) {
      IM_EXPBUF *tmpbuf = ime->expbuf ;
      if ( tmpbuf != NULL ) {
        VERIFY_OBJECT(tmpbuf, IM_EXPBUF_NAME);
        bytes2free += (sizeof(IM_EXPBUF) + tmpbuf->size)/tmpbuf->refcnt;
        if ( max2free < sizeof(IM_EXPBUF) + tmpbuf->size )
          max2free = sizeof(IM_EXPBUF) + tmpbuf->size;
      }
      if ( tmpbuf == cipbuf )
        break; /* all shared from here on */
    }
  }
  if ( bytes2free > max2free )
    bytes2free -= max2free;
  else
    bytes2free = 0;

  if ( bytes2free > 0 ) {
    last_image_in_offer = page->im_list;
    offer.pool = dl_choosepool(page->dlpools, MM_ALLOC_CLASS_IMAGE_EXPBUF);
    offer.offer_size = bytes2free;
    offer.offer_cost = 0.1f;
    offer.next = NULL;
    return &offer;
  } else
    return NULL;
}

/**
 * Release method of the imstore low-memory handler.
 *
 * Ignore the offer_taken request size but just free as much as we can.
 * This is because the existing algorithm relies on there not being partial
 * purges. This means it can stop as soon as it finds a buffer equal to the
 * shared buffer.
 */
static Bool im_expand_lowmem_release(low_mem_handler_t *handler,
                                     corecontext_t *context,
                                     low_mem_offer_t *offer)
{
  DL_STATE *page;
  int32 bufsize;
  IM_EXPBUF *expbuf;
  IM_EXPBUF *cipbuf;
  IMAGEOBJECT *image;
  size_t freed = 0;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  page = context->page;
  HQASSERT(page, "No DL page");
  if ( !page->im_purge_allowed )
    return TRUE;

  last_image_in_offer = NULL; /* stop solicit from remaking this offer */

  cipbuf = page->im_expbuf_shared;
  if ( cipbuf ) {
    expbuf  = cipbuf;
    bufsize = cipbuf->size;
  } else {
    expbuf  = NULL;
    bufsize = 0;
  }

  /* Find the largest expansion buffer amongst the new images, stopping as
   * soon as we find one that we know to be the largest. page->im_bufsize might
   * be too large, because the largest buffer got deleted. */
  if ( bufsize < page->im_bufsize ) {
    for ( image = page->im_list; image != NULL; image = image->next ) {
      IM_EXPAND *ime = image->ime;
      if ( ime != NULL ) {
        IM_EXPBUF *tmpbuf = ime->expbuf;
        if ( tmpbuf != NULL ) {
          int32 tmpsize = tmpbuf->size;

          if ( tmpsize > bufsize ) {
            expbuf  = tmpbuf;
            bufsize = tmpsize;
            if ( bufsize >= page->im_bufsize )
              break;
          } else if ( tmpbuf == cipbuf )
            /* If we've reached the previously purged images, they're all the
               same size. */
            break;
        }
      }
    }
  }
  /* Can't set page->im_bufsize, because there might be an image of that size
     under construction, not yet on im_list. */

  /* Didn't find an expansion buffer; should only be hit with recombine.
   * Can't assert anything though since could be in the middle of color
   * converting the recombined images, so recombine could be turned off(!).
   */
  if ( expbuf == NULL )
    return TRUE;

  for ( image = page->im_list; image != NULL; image = image->next ) {
    IM_EXPAND *ime = image->ime;
    if ( ime != NULL ) {
      IM_EXPBUF *tmpbuf = ime->expbuf;
      if ( tmpbuf != NULL ) {
        /* Free current expansion buffer if not already done so (and not the
         * one we're about to share from) and then share the largest.
         */
        VERIFY_OBJECT(tmpbuf, IM_EXPBUF_NAME);
        if ( tmpbuf != expbuf ) {
          /* If we have several images sharing a smaller expansion buffer then
           * make sure we only free it once!
           */
          if ((--tmpbuf->refcnt) == 0 ) {
            UNNAME_OBJECT(tmpbuf);
            freed += sizeof(IM_EXPBUF) + tmpbuf->size;
            dl_free(page->dlpools, tmpbuf, sizeof(IM_EXPBUF) + tmpbuf->size,
                    MM_ALLOC_CLASS_IMAGE_EXPBUF);
            SwOftenUnsafe();
          }
          ime->expbuf = expbuf;
          expbuf->refcnt++;
        }
        else if ( tmpbuf == cipbuf ) {
          /* These remaining images are already shared by the largest */
          break;
        }
      }
    }
  }
  page->im_expbuf_shared = expbuf;
  return TRUE;
}

/** The im_expand low-memory handler. */
static low_mem_handler_t im_expand_lowmem_handler = {
  "Image expand",
  memory_tier_ram, im_expand_lowmem_solicit, im_expand_lowmem_release, TRUE,
  0, FALSE
};


static Bool im_expand_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  return low_mem_handler_register(&im_expand_lowmem_handler);
}

static void im_expand_finish(void)
{
  low_mem_handler_deregister(&im_expand_lowmem_handler);
}

void im_expand_C_globals(core_init_fns *fns)
{
  fns->swstart = im_expand_swstart;
  fns->finish  = im_expand_finish;
#if defined(METRICS_BUILD)
  sw_metrics_register(&im_expand_metrics_hook);
#endif
}

/* Log stripped */
