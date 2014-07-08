/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_image.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display list image argument unpacking, reading, and image object creation
 */

#include "core.h"

#include "swerrors.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "mm.h"
#include "lowmem.h"
#include "asyncps.h"
#include "progupdt.h"

#include "dl_image.h"
#include "blitcolort.h"
#include "mmcompat.h"
#include "objects.h"
#include "fileio.h"
#include "namedef_.h"
#include "swpdfout.h"
#include "often.h"

#include "display.h"
#include "dlstate.h"
#include "control.h"
#include "stacks.h"
#include "graphics.h"
#include "gu_chan.h"
#include "gs_color.h"
#include "gs_table.h"
#include "halftone.h" /* ht_defer_allocation */
#include "color.h" /* ht_getSolid */
#include "render.h"
#include "gstate.h"
#include "routedev.h"
#include "psvm.h"
#include "spdetect.h"
#include "fileops.h"
#include "idlom.h"
#include "packdata.h"
#include "utils.h"
#include "plotops.h"
#include "params.h"
#include "group.h"

#include "gschtone.h"
#include "gschead.h"
#include "gschcms.h"
#include "gscparams.h"

#include "imageo.h" /* IMAGEOBJECT */
#include "imaskgen.h"
#include "imskgen4.h"
#include "images.h"
#include "imtiles.h"
#include "imstore.h"
#include "imcolcvt.h"
#include "imexpand.h"
#include "imdecodes.h"
#include "cvdecodes.h"
#include "imlut.h" /* imlut_destroy */
#include "dl_bres.h"
#include "dl_free.h"
#include "dl_color.h"
#include "gu_path.h"
#include "pathcons.h"
#include "panalyze.h"
#include "gscdevci.h" /* gsc_setblockoverprints et al */
#include "gscfastrgb2gray.h"

#include "rcbadjst.h" /* rcba_doing_color_adjust */
#include "rcbcntrl.h"
#include "rcbtrap.h"
#include "cmpprog.h"
#include "genhook.h"
#include "fltrdimg.h"
#include "trap.h"
#include "tranState.h"
#include "pixelLabels.h"
#include "timing.h"
#include "interrupts.h"
#include "jobmetrics.h"

#define IMBBUF_COOKIE   (0xEC)  /* A magic marker for the end of buffers */

static void  im_free_buffers(IMAGEARGS *imageargs, IMAGEDATA *imagedata);
static void im_optimisematrix(IMAGEARGS *imageargs, IMAGEDATA *imagedata);
static Bool im_setcolorspace(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                             int32 colorType);
static Bool im_setintercepttype(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                int32 colorType );
static Bool im_prepareimagedata(IMAGEARGS *imageargs,
                                IMAGEDATA *imagedata,
                                Bool filtering);
static Bool im_detectseparation(IMAGEARGS *imageargs);
static Bool im_createimageobj( int32 colorType,
                               IMAGEARGS *imageargs,
                               IMAGEDATA *imagedata,
                               IMAGEOBJECT **newimageobj,
                               OMATRIX *matrix );
static Bool im_finishimageobj( IMAGEARGS *imageargs,
                               IMAGEDATA *imagedata,
                               IMAGEOBJECT *imageobj );
static Bool im_expandaddalpha(DL_STATE *page, IMAGEOBJECT* imageobj);

typedef enum {
  NoLines = 0,
  MaskLines = 1,
  ImageLines = 2,
  MaskImageLines = MaskLines|ImageLines
} ImageLineType;

static Bool im_getimageblock(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                              IMAGEARGS *imaskargs, IMAGEDATA *imaskdata,
                              int32 *lines, ImageLineType *read_type);

static Bool im_getfilteredblock(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                IMAGEARGS *imaskargs, IMAGEDATA *imaskdata,
                                int32 *lines, ImageLineType *read_type);

static Bool dl_imagedatafn(IMAGEARGS *imageargs,
                            IMAGEDATA *imagedata,
                            uint8 **tbuf,
                            int32 nprocs,
                            void *data);

Bool im_convertlines(IMAGEARGS *imageargs,
                     IMAGEDATA *imagedata,
                     int32 lines,
                     Bool (*linefn)(IMAGEARGS *imageargs,
                                    IMAGEDATA *imagedata,
                                    uint8 **tbuf,
                                    int32 ncomps,
                                    void *data),
                     void *data);

static Bool im_rcb_blankimage(IMAGEOBJECT* image, COLORANTINDEX ci);


static uint8 im_reproObjectType(IMAGEARGS *imageargs, IMAGEDATA *imagedata);

/**
 * Start the dl image module for this page.
 */
Bool dl_image_start(DL_STATE *page)
{
  size_t i;
  page->im_list = NULL;
  page->im_imagesleft = 0;
  page->im_bufsize = 0;
  page->im_force_interleave = FALSE;
  page->im_expbuf_shared = NULL;
  page->im_shared = NULL;
  page->image_lut_list = NULL;
  page->im_purge_allowed = TRUE;
  for ( i = 0; i < DECODESOBJ_NUM_SLOTS; ++i )
    page->decodesobj[i] = NULL;
  cv_reset_decodes(page);
  return im_shared_start(page);
}

/**
 * Finish the dl image module for this page.
 */
void dl_image_finish(DL_STATE *page)
{
  HQASSERT(page->im_imagesleft == 0, "im_imagesleft must be zero");
  im_expanderase(page);
  im_shared_finish(page);
  imlut_destroy(page);
  page->im_purge_allowed = TRUE;
  im_reset_decodes(page);
  cv_reset_decodes(page);
  page->im_list = NULL; /* do last since im_list is used by above */
}

/**
 * Double-link list management for image objects. The list acts like a stack,
 * the double-link structure is just to make insertion and deletion quicker.
 */
void im_addimagelist(IMAGEOBJECT **im_list, IMAGEOBJECT *imageobj)
{
  HQASSERT(im_list, "No list to remove image from");
  HQASSERT(imageobj, "No image to remove from list");
  HQASSERT(imageobj->next == NULL && imageobj->prev == NULL,
           "Image is already linked in list");

  imageobj->next = *im_list;

  if ( imageobj->next != NULL )
    imageobj->next->prev = imageobj;

  *im_list = imageobj;
}

/**
 * Unlink an image from its list, and remove it if it is attached to this
 * list
 */
void im_removeimagelist(IMAGEOBJECT **im_list, IMAGEOBJECT *imageobj)
{
  HQASSERT(im_list, "No list to remove image from");
  HQASSERT(imageobj, "No image to remove from list");

  if ( *im_list == imageobj )
    *im_list = imageobj->next;

  if ( imageobj->next )
    imageobj->next->prev = imageobj->prev;

  if ( imageobj->prev )
    imageobj->prev->next = imageobj->next;

  imageobj->prev = imageobj->next = NULL;
}

/**
 * Free all associated storage for the given image object.
 */
void im_freeobject(IMAGEOBJECT *imageobj, DL_STATE *page)
{
  HQASSERT(imageobj, "imageobj NULL");
  HQASSERT(page, "page NULL");

  im_removeimagelist(&page->im_list, imageobj);

  if ( imageobj->mask )
    im_freeobject( imageobj->mask, page );

  if ( imageobj->rcb.nfillm )
    free_fill( imageobj->rcb.nfillm, page );

  if ( imageobj->ims )
    im_storefree(imageobj->ims);

  if ( imageobj->ime )
    im_expandfree(page, imageobj->ime);

  cv_free_decodes(page, &imageobj->cv_fdecodes, &imageobj->cv_ndecodes,
                  &imageobj->alloc_ncomps);

  if ( imageobj->tiles )
    dl_free(page->dlpools, (mm_addr_t)(imageobj->tiles),
             (mm_size_t)sizeof(IMAGETILES), MM_ALLOC_CLASS_IMAGE_TILETABLE);

  dl_free(page->dlpools, (mm_addr_t)imageobj->base_addr,
           (mm_size_t)sizeof(IMAGEOBJECT) + 4, MM_ALLOC_CLASS_IMAGE);
}

/**
 * Free all the buffers associated with the given image object
 */
void im_free_bufptrs(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  int32 nprocs;
  IM_BUFFER *imb;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");

  imb = imagedata->imb;
  if ( imb == NULL )
    return;

  nprocs = imageargs->nprocs;
  HQASSERT(nprocs > 0, "nprocs should be > 0");

  if ( imb->ibuf ) {
    mm_free( mm_pool_temp, imb->ibuf, nprocs * sizeof( uint8 * ) );
    imb->ibuf = NULL;
  }
  if ( imb->obuf ) {
    mm_free( mm_pool_temp, imb->obuf, nprocs * sizeof( uint8 * ) );
    imb->obuf = NULL;
  }
  if ( imb->tbuf ) {
    mm_free( mm_pool_temp, imb->tbuf, nprocs * sizeof( uint8 * ) );
    imb->tbuf = NULL;
  }
  if ( imb->dbuf ) {
    mm_free( mm_pool_temp, imb->dbuf, nprocs * sizeof( uint8 * ) );
    imb->dbuf = NULL;
  }
}

/**
 * Free al the buffers associated with the given object
 */
static void im_free_buffers(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  int32 i;
  int32 nprocs;
  IM_BUFFER *imb;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");

  imb = imagedata->imb;
  if ( imb == NULL )
    return;

  if ( imb->tbuf != NULL ) {
    nprocs = imageargs->nprocs;
    HQASSERT(nprocs > 0, "nprocs should be > 0");

    for ( i = 0; i < nprocs; ++i ) {
      if ( imb->tbuf[i] != NULL ) {
        HQASSERT(imb->tbuf[i][imb->tbufsize - 1] == IMBBUF_COOKIE,
                 "Image buffer has overrun");;
        mm_free(mm_pool_temp, imb->tbuf[i], imb->tbufsize);
        imb->tbuf[i] = NULL;
      }
    }
    imb->tbufsize = 0;
  }
  if ( imb->dbuf != NULL ) {
    nprocs = imageargs->nprocs;
    HQASSERT(nprocs > 0, "nprocs should be > 0");

    for ( i = 0; i < nprocs; ++i ) {
      if ( imb->dbuf[i] != NULL ) {
        mm_free(mm_pool_temp, imb->dbuf[i], imb->dbufsize);
        imb->dbuf[i] = NULL;
      }
    }
    imb->dbufsize = 0;
  }
}

static void im_free_early_imagedata(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");

  im_free_buffers(imageargs, imagedata);
  im_free_bufptrs(imageargs, imagedata);
  im_free_decodes(imageargs, imagedata);
}

static void im_free_late_imagedata(IMAGEDATA *imagedata)
{
  HQASSERT(imagedata, "imagedata NULL");

  if ( imagedata->imc != NULL ) {
    im_colcvtfree( imagedata->imc );
    imagedata->imc = NULL;
  }
  if ( imagedata->pd != NULL ) {
    pd_packdatafree( imagedata->pd );
    imagedata->pd = NULL;
  }

  imagedata->expandformat = 0;
}

void im_free_data( IMAGEARGS *imageargs, IMAGEDATA *imagedata )
{
  im_free_early_imagedata( imageargs, imagedata );
  im_free_late_imagedata( imagedata );
}


/**
   im_alloc_bufptrs()
   Allocates arrays of pointers to buffers for initial input processing of the
   image data.  These are the ibuf[], obuf[] and tbuf[] arrays declared in the
   IMAGEDATA->IM_BUFFER structure.  (The buffers themselves are
   allocated/assigned later.)  Note (from im_alloc_buffers()) that only the
   tbuf[] array is allocated some buffers - the ibuf[] array points to the
   original input source buffer (e.g. OSTRING or OFILE) and the obuf[] array
   is used only to point the appropriate output buffer (i.e. either the ibuf[]
   or tbuf[] buffer) depending on the type of processing that's been performed.
*/

Bool im_alloc_bufptrs( IMAGEARGS *imageargs, IMAGEDATA *imagedata )
{
  int32 i;
  int32 nprocs;
  IM_BUFFER *imb;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");

  imb = imagedata->imb;
  HQASSERT(imb != NULL, "imb NULL");

  nprocs = imageargs->nprocs;
  HQASSERT(nprocs > 0, "nprocs should be > 0");

  HQASSERT(imb->ibuf == NULL, "ibuf should be NULL");
  HQASSERT(imb->obuf == NULL, "obuf should be NULL");
  HQASSERT(imb->tbuf == NULL, "tbuf should be NULL");
  HQASSERT(imb->dbuf == NULL, "dbuf should be NULL");

  imb->ibuf = ( uint8 ** )mm_alloc(mm_pool_temp,
                                   nprocs * sizeof( uint8 * ),
                                   MM_ALLOC_CLASS_IMAGE_SCANLINE);
  imb->obuf = ( uint8 ** )mm_alloc(mm_pool_temp,
                                   nprocs * sizeof( uint8 * ),
                                   MM_ALLOC_CLASS_IMAGE_SCANLINE);
  imb->tbuf = ( uint8 ** )mm_alloc(mm_pool_temp,
                                   nprocs * sizeof( uint8 * ),
                                   MM_ALLOC_CLASS_IMAGE_SCANLINE);

  imb->dbuf = ( uint8 ** )mm_alloc(mm_pool_temp,
                                   nprocs * sizeof( uint8 * ),
                                   MM_ALLOC_CLASS_IMAGE_SCANLINE);

  if ( imb->ibuf == NULL || imb->obuf == NULL || imb->tbuf == NULL ||
       imb->dbuf == NULL ) {
    im_free_bufptrs(imageargs, imagedata);
    return error_handler(VMERROR);
  }

  imb->tbufsize = imb->dbufsize = 0;

  for ( i = 0; i < nprocs; ++i )
    imb->obuf[i] = imb->tbuf[i] = imb->dbuf[i] = NULL;
  imb->ibytes = imb->tbytes = 0;

  return TRUE;
}

static int32 pre_downsampled_bytes(IMAGEARGS *imageargs)
{
  int32 nb = imageargs->downsample.w0 * imageargs->bits_per_comp;

  if ( imageargs->nprocs == 1 ) {
    if (imageargs->interleave == INTERLEAVE_PER_SAMPLE)
      nb *= (imageargs->ncomps + 1);
    else
      nb *= imageargs->ncomps;
  }
  nb = ( nb + 7 ) >> 3;
  return nb;
}

/**
   im_alloc_buffers()
   See comment for im_alloc_bufptrs() for some information.
*/
Bool im_alloc_buffers( IMAGEARGS *imageargs, IMAGEDATA *imagedata )
{
  int32 i;
  int32 nprocs;
  IM_BUFFER *imb;
  int32 tbufsize;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");

  imb = imagedata->imb;
  HQASSERT(imb, "imb NULL");
  HQASSERT(imb->tbufsize == 0, "Expected tbufsize == 0");

  nprocs = imageargs->nprocs;
  HQASSERT(nprocs > 0, "nprocs should be > 0");

  /* The normal image case, bufsize == the size of the input line */
  tbufsize = imagedata->bytes_in_input_line;

  if ( imageargs->downsample.x > 1 )
    tbufsize = pre_downsampled_bytes(imageargs);

  /* Add 1 byte for a magic cookie which will be checked in im_free_buffers */
  tbufsize += 1;

  /* Allocate the tbuf's, they are the same size for all procs and they will
   * all be allocated together.
   */
  for ( i = 0; i < nprocs; ++i ) {
    HQASSERT(imb->tbuf[i] == NULL, "Image buffer memory leak");
    imb->tbuf[i] = (uint8 *)mm_alloc(mm_pool_temp, tbufsize,
                                     MM_ALLOC_CLASS_IMAGE_SCANLINE);
    if ( imb->tbuf[i] == NULL )
      return error_handler(VMERROR);
    imb->tbuf[i][tbufsize - 1] = IMBBUF_COOKIE;
  }
  imb->tbufsize = tbufsize;

  if ( imageargs->downsample.enabled ) {
    int32 dbufsize = (imagedata->bytes_in_input_line+1) *
                      imageargs->downsample.x * imageargs->downsample.y;

    for ( i = 0; i < nprocs; ++i ) {
      HQASSERT(imb->dbuf[i] == NULL, "Image buffer memory leak");
      imb->dbuf[i] = (uint8 *)mm_alloc(mm_pool_temp, dbufsize,
                                       MM_ALLOC_CLASS_IMAGE_SCANLINE);
      if ( imb->dbuf[i] == NULL )
        return error_handler(VMERROR);
    }
    imb->dbufsize = dbufsize;
  }

  return TRUE;
}

/**
 * Allocate the imagedata memory whose size can be derived from imageargs.
 */
static Bool im_alloc_early_imagedata(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  int32 imagetype = imageargs->imagetype;
  IMAGEARGS *imaskargs = imageargs->maskargs;
  IMAGEDATA *imaskdata = imagedata->imaskdata;

  HQASSERT((imaskargs == NULL) ^ ((imagetype == TypeImageMaskedImage)||
                                  (imagetype == TypeImageAlphaImage)),
           "Should have imaskargs only if a mask is present");
  HQASSERT((imaskdata == NULL) == (imaskargs == NULL),
           "Should have imaskdata only if a mask is present");

  /* Allocate the buffers for processing the input scanlines from the
   * postscript file. */
  if ( imagedata->sharedIMB ) {
    if (!im_alloc_bufptrs( imageargs, imagedata ) ||
        !im_alloc_buffers(imageargs, imagedata))
      return FALSE;
  }
  if ( imaskdata != NULL && imaskdata->sharedIMB ) {
    if (!im_alloc_bufptrs(imaskargs, imaskdata) ||
        !im_alloc_buffers(imaskargs, imaskdata))
      return FALSE;
  }

  if ( imagetype == TypeImageImage || imagetype == TypeImageMaskedImage ||
       imagetype == TypeImageAlphaImage ) {
    if ( !im_alloc_fdecodes(imageargs, imagedata) )
      return FALSE;

    if ( imagedata->method != GSC_USE_PS_PROC ) {
      if ( imagedata->method == GSC_USE_FASTRGB2GRAY ||
           imagedata->method == GSC_USE_FASTRGB2CMYK) {
        /* fastrgb2gray_prep() or fastrgb2cmyk_prep() are called in
         * im_alloc_late_imagedata(), after we've finished setting up the color
         * chain. */
      }
      else {
        if ( ! im_alloc_ndecodes(imageargs, imagedata))
          return FALSE;
      }
    }

    if ( imagetype == TypeImageAlphaImage )
      if ( !im_alloc_fdecodes(imaskargs, imaskdata) )
        return FALSE;
  }
  return TRUE;
}

/**
   im_alloc_late_imagedata()
   Allocate the imagedata memory whose size isn't known until after the
   expandformat is available in dl_image_begin.
*/
static Bool im_alloc_late_imagedata(IMAGEARGS *imageargs,
                                    IMAGEDATA *imagedata,
                                    IM_EXPAND *ime)
{
  int32 imagetype = imageargs->imagetype;
  int32 colorType = imageargs->colorType;

  imagedata->expandformat = im_expandformat(ime);

  /* Set up the fast rgb2gray LUT which is owned by the color chain and valid
     until the chain is invalidated.  fastrgb2gray_prep() must be done after the
     image chain has been set up or the LUT may disappear. */
  if ( imagedata->method == GSC_USE_FASTRGB2GRAY )
    if ( !gsc_fastrgb2gray_prep(imagedata->colorInfo, imageargs->colorType,
                                imageargs->bits_per_comp,
                                imagedata->u.fdecodes[0],
                                imagedata->u.fdecodes[1],
                                imagedata->u.fdecodes[2]) )
      return FALSE;

  if (imagedata->method == GSC_USE_FASTRGB2CMYK) {
    if (!gsc_fastrgb2cmyk_prep(imagedata->colorInfo, imageargs->bits_per_comp,
                               NULL, NULL, NULL)) {
      return FALSE;
    }
  }

  /* Prepare for color conversion! (Note this means that expansion LUTs
     will not be used.) */
  if ( imagedata->expandformat == ime_colorconverted ||
       imagedata->expandformat == ime_as_is_decoded ) {
    Bool justDecode = (imageargs->imagetype == TypeImageAlphaAlpha ||
                       imagedata->expandformat == ime_as_is_decoded);
    HQASSERT(imagetype != TypeImageMask &&
             imagetype != TypeImageMaskedMask,
             "Doing image mask but expander said allocate col cvt");
    if ( imagedata->imc == NULL ) /* i.e., not striping */
      imagedata->imc = im_colcvtopen(imagedata->colorInfo,
                                     imagedata->pools,
                                     imagedata->ndecodes
                                     ? imagedata->ndecodes
                                     : imagedata->u.idecodes,
                                     imageargs->ncomps,
                                     imageargs->width,
                                     imagedata->out16,
                                     imagedata->method,
                                     colorType, justDecode);
    if ( imagedata->imc == NULL )
      return FALSE;
  }

  /* The following section uses a number of rules to decide just what, and how
     much buffering and re-buffering of various types needs doing.
     The "packdata" package does all sorts depending on the setting of the
     'pd_use' flag.
     pd_pack(), pd_unpack() and pd_planarize() are all called from
     im_putscanlines() to translate the image data before putting it away into
     the image store.
  */
  imagedata->pd_use = 0;

  if ( imagetype != TypeImageMask && imagetype != TypeImageMaskedMask ) {
    switch ( imagedata->expandformat ) {
    case ime_as_is:
      HQASSERT(imageargs->bits_per_comp == 32,
               "Only 32 bpp should be expanded as-is") ;
      /*
       * Support 32bit image data as a debugging option.
       * Ensure it does not get packed/unpacked
       */
      imagedata->pd_use = 0;
      break ;
    case ime_colorconverted:
    case ime_as_is_decoded:
      /* Color converting, not using expansion LUTs. If the original data is
         pixel-interleaved, test to see if all of the components are the
         same, but only if the test is easy. (Easy is defined as a bits depth
         of 8 or 16 for this purpose.) We may be able to simplify to a single
         component plus identity LUTs later. For the moment, we'll just
         restrict this to the RGB to CMYK conversions, so we can throw away
         the R, G, and B channels, retaining the K channel. */
      imagedata->all_components_same = (imagedata->method == GSC_USE_FASTRGB2CMYK &&
                                        imagedata->page->virtualBlackIndex >= 0 &&
                                        imageargs->nprocs == 1 &&
                                        imageargs->ncomps > 1 &&
                                        (imageargs->bits_per_comp & ~7) == imageargs->bits_per_comp) ;
      imagedata->pd_use = PD_UNPACK;
      break ;
    case ime_planar:
      /* Need to expand 12 bits per pixel into 16.
         Need to unpack/re-pack 16-bit data to make native endian. */
      if ( imageargs->bits_per_comp >= 12 )
        imagedata->pd_use = PD_UNPACK | PD_PACK | PD_PLANAR;

      /* Need to planarize data for expand routines if input samples are
         pixel interleaved (i.e. not separate datasources) else we assume
         the input is already planar. */
      if (imageargs->ncomps != imageargs->nprocs)
        imagedata->pd_use = PD_UNPACK | PD_PACK | PD_PLANAR | PD_ALIGN;
      break ;
    default:
      /* use expansion LUTs with non-independent colorants => interleave! */
      /* Interleaved output that may be padded. */
      HQASSERT(imageargs->ncomps > 1, "Only interleave > 1 component");
      HQASSERT(imageargs->bits_per_comp != 32, "Dont interleave 32 bits");
      HQASSERT(imageargs->bits_per_comp != 12, "Dont interleave 12 bits");
      HQASSERT(imageargs->bits_per_comp !=  8, "Dont interleave  8 bits");
      if ( im_expandpad( ime ) ||
           imageargs->ncomps == imageargs->nprocs)
        imagedata->pd_use = PD_UNPACK | PD_PACK;
      break ;
    }

    /* Generating mask for a type 4 image. */
    if ( imageargs->maskgen != NULL )
      imagedata->pd_use |= PD_UNPACK;

    /* Pre-allocate the packdata buffers */
    if ( imagedata->pd_use != 0 ) {
      if ( imagedata->pd == NULL ) /* i.e., not striping */
        imagedata->pd = pd_packdataopen(imagedata->pools,
                                        imageargs->ncomps,
                                        imageargs->width,
                                        imageargs->bits_per_comp,
                                        imageargs->nprocs == 1,
                                        im_expandpad( ime ),
                                        TRUE, /* pack 12 bpp into 16 bits */
                                        imagedata->pd_use);
      if ( imagedata->pd == NULL )
        return FALSE;
    }
  }

  return TRUE;
}

#if 0 /* DEBUG_BUILD */

int32 getenv_method( )
{
  static Bool gotenv = FALSE;
  static int32 env_method = GSC_NO_EXTERNAL_METHOD_CHOICE;

  if ( !gotenv ) {
    char *env;

    gotenv = TRUE;
    env = getenv("RGBFAST"); /* set RGBFAST=xx */
    if ( env != NULL )
      env_method = atol(env);
  }
  return env_method;
}

#endif

int32 im_determine_method(int32 imagetype, int32 ncomps, int32 bits_per_comp,
                          GS_COLORinfo *colorInfo, int32 colorType)
{
  /* Don't use table methods for:
   * - image masks (or masked images masks).
   * - indexed images (can't interpolate, but these will normally use luts)
   * - large component images.
   * - images with bits per pixel less than 4 (code doesn't support it).
   * - vignettes (Mx1 or 1xN images).
   * - and only if we're allowed to.
   */
  if ( imagetype == TypeImageMask ||
       imagetype == TypeImageMaskedMask ||
       imagetype == TypeImageAlphaAlpha ||
       gsc_getcolorspace(colorInfo, colorType) == SPACE_Indexed ||
       ncomps > 16 ||
       bits_per_comp < 4 ||
       !get_core_context_interp()->color_systemparams->TableBasedColor ||
       colorType == GSC_SHFILL )
    return GSC_USE_PS_PROC;
  else {
    Bool iscomplex;
    Bool fast_rgb_gray_candidate = FALSE;
    Bool fast_rgb_cmyk_candidate = FALSE;

    if ( ! gsc_colorChainIsComplex(colorInfo, colorType,
                                   &iscomplex, &fast_rgb_gray_candidate,
                                   &fast_rgb_cmyk_candidate ))
      return GSC_USE_PS_PROC;

    /* fast rgb conversion applies to a cetain subset of images. As its
     * LUT based, don't apply to tiny images, or images whose bits per
     * component value would make tables impractical. */
    /** \todo @@@ TODO rogerg Enable the optimisation for 4 and 12 bit
     * images. The LUT generation should support it; just widen the test
     * below on BPC.
     */
    if (fast_rgb_gray_candidate && bits_per_comp == 8) {
#if 0 /* DEBUG_BUILD */
      int32 force_method = getenv_method();

      if ( force_method != GSC_NO_EXTERNAL_METHOD_CHOICE ) {
        HQTRACE(TRUE, ("OVERRIDE COL CVT"));
        return force_method;
      }
      else
#endif /* DEBUG_BUILD */
        return GSC_USE_FASTRGB2GRAY;
    }

    if (gsc_use_fast_rgb2cmyk(colorInfo) &&
        fast_rgb_cmyk_candidate &&
        (bits_per_comp == 8 || bits_per_comp == 16)) {
      return (GSC_USE_FASTRGB2CMYK);
    }

    /* Only use interpolation for complex color spaces, otherwise evaluation
     * is quicker. */
    if ( ! iscomplex )
      return GSC_USE_PS_PROC;

    return GSC_USE_INTERPOLATION;
  }
  /* NOT REACHED */
}

typedef struct {
  dl_color_t dlc;
  IMAGEOBJECT *imageobj;
  Bool is_mask_data;
  GUCR_RASTERSTYLE *savedTargetRS;
  enum {
    NoStripe, StripeBegun, StripeEnded
  } stripe_state;         /**< What phase of striping are we at? */
} dl_image_state;

/**
 * Have we now got enough memory to allocate a complete row of image
 * blocks per plane? Try pre-allocating row of image blocks (mirroring
 * calls to im_storewrite in im_putscanlines).
 */
static Bool do_imstore_prealloc(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                IMAGEOBJECT *imageobj)
{
  if ( imagedata->expandformat == ime_colorconverted ||
       ( imagedata->pd_use & PD_PACK ) != 0 ) {
    int32 lplane = imagedata->presep ? imagedata->plane : IMS_ALLPLANES;

    if ( !im_storeprealloc(imageobj->ims, lplane, imageargs->width) )
      return FALSE;
  }
  else {
    int32 i;

    for ( i = 0; i < imageargs->nprocs; i++ ) {
      if ( !im_storeprealloc(imageobj->ims, imagedata->plane + i,
                             imageargs->width) )
        return FALSE;
    }
  }
  return TRUE;
}

static Bool dl_image_begin(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                           void *data)
{
  dl_image_state *state = data;
  corecontext_t *context = get_core_context_interp();
  error_context_t *errcontext = context->error ;
  int32 colorType;
  int32 action;
  IMAGEOBJECT *imageobj = NULL;
  int32 imagetype;
  Bool result = FALSE;
  Bool isMask;
  int saved_dl_safe_recursion = dl_safe_recursion;
  Bool striping = FALSE;
  dl_color_t *dlc_current;

  /* In case we add a new DL object we need to flush any partial
   * character one. */
  if ( !finishaddchardisplay(imagedata->page, 1) )
    return FALSE;

  HQASSERT(state, "No DODL image state");
  state->imageobj = NULL;

  /* An image might be started as part of a continuing sequence of
   * sub-image stripes. Any previous stripe must be ended before
   * another can be started.
   */

  HQASSERT(state->stripe_state == NoStripe ||
           state->stripe_state == StripeEnded,
           "Started an image stripe in illegal state");

  /* if we are continuing a sequence of stripes, we avoid begin / end
   * image hooks and related HDLT callbacks.
   */
  striping = (state->stripe_state == StripeEnded);

  /* In striping mode, there may already be "late image data" allocated
   *  for the image data - allocated through a previous call to
   *  im_createimageobj via dl_image_begin. We can delete the memory
   *  here and do im_createimageobj as normal. */
  if ( striping )
    im_free_late_imagedata(imagedata);

  dlc_clear(&state->dlc);

  imagetype = imageargs->imagetype;
  HQASSERT(VALID_IMAGETYPE( imagetype ), "bad image type");

  isMask = ( (imagetype == TypeImageMaskedImage) ||
             (imagetype == TypeImageAlphaImage));

#if defined(METRICS_BUILD)
  /* Count images. */
  if ( !striping )
    switch ( imagetype ) {
      dl_metrics_t *dlmetrics ;
    case TypeImageImage:
    case TypeImageMaskedImage:
    case TypeImageAlphaImage:
      dlmetrics = dl_metrics() ;
      switch (imageargs->image_color_space) {
      default:
        ++dlmetrics->images.colorspaces.other ;
        break;
      case SPACE_DeviceRGB:
      case SPACE_CalRGB:
        ++dlmetrics->images.colorspaces.rgb ;
        break;
      case SPACE_DeviceCMYK:
        ++dlmetrics->images.colorspaces.cmyk ;
        break;
      case SPACE_DeviceGray:
      case SPACE_CalGray:
        ++dlmetrics->images.colorspaces.gray ;
        break;
      }

      if ( imagedata->optimise & IMAGE_OPTIMISE_ROTATED ) {
        ++dlmetrics->images.rotated ;
      } else {
        ++dlmetrics->images.orthogonal ;
      }

      if ( imagedata->degenerate )
        ++dlmetrics->images.degenerate ;

      break ;
    }
#endif

  /* Ignore degenerate image */
  if ( imagedata->degenerate || char_doing_charpath() )
    return TRUE;

#define return DO_NOT_RETURN_goto_ENDIMAGE_INSTEAD!
  probe_begin(SW_TRACE_DL_IMAGE, (intptr_t)0) ;

  /* Say whether the image is a picture, by our definition, to the
     outside world (e.g hooks, callbacks) */
  context->systemparams->Picture = im_reproObjectType(imageargs, imagedata) ==
                         REPRO_TYPE_PICTURE;

  /* Call the image hook if required, before the operands are popped off the
     stack so the hook has access to them if necessary. Rather like the HDLT
     hook really. This used to be inside the DL image only, and was done
     after the setg(). It makes more sense to be before the setg(), so
     modifications to the gstate can be noticed.

     Once in striping mode, do not announce the stripes to the outside world.
     */
  if ( ! striping &&
       ! runHooks(&theIgsDevicePageDict(gstateptr), GENHOOK_StartImage) )
    goto ENDIMAGE;

  colorType = imageargs->colorType;
  HQASSERT(colorType == GSC_FILL || colorType == GSC_IMAGE ||
           colorType == GSC_SHFILL, "Strange colorType for image");

  /* Note that the SETG() must come before any function which assumes that
     the gstate is correct for this image; ie. it needs to come before
     PDFOUT and HDLT callouts. */
  if ( !DEVICE_SETG(imagedata->page, colorType,
                    striping ? DEVICE_SETG_RETRY : DEVICE_SETG_NORMAL) )
    goto ENDIMAGE;

  /* image stripes are a purely internal concept. */
  if (!striping ) {
    if ( pdfout_enabled() &&
         ! pdfout_beginimage( context->pdfout_h, colorType, imageargs,
                              imagedata->u.idecodes ))
      goto ENDIMAGE;

    switch ( IDLOM_BEGINIMAGE(colorType,
                              imageargs,
                              imagedata->expandformat == ime_colorconverted,
                              imagedata->oncomps,
                              imagedata->out16,
                              imagedata->u.idecodes) ) {
    case NAME_false:   /* Error in callback or in setup */
      goto ENDIMAGE;
    case NAME_Discard: /* Single-callback form wants to throw it away */
      /* Pretend image is degenerate, so data and end callback are ignored. */
      imagedata->degenerate = TRUE;
      result = TRUE;
      goto ENDIMAGE;
    case NAME_Add:     /* Single-callback form wants to add it now */
      /* Disable HDLT until the end of im_common, so image data and End
         callbacks won't be called. */
      theIdlomState(*gstateptr) = HDLT_DISABLED;
      break;
    default:
      HQFAIL("HDLT image Begin callback returned unexpected value");
      /*@fallthrough@*/
    case NAME_End:     /* Defer what to do to End callback */
      break;
    }
  }

  /* Do all the one-off setup, ready to create the image object(s) */
  if ( imageargs->imagetype != TypeImageMask &&
       imageargs->imagetype != TypeImageMaskedMask &&
       imageargs->imagetype != TypeImageAlphaAlpha ) {
    if ( ! im_detectseparation(imageargs) )
      goto ENDIMAGE;
  }

  /* OK, all the image is setup correctly for the DL & external use */

  /* Create the actual image object, calling low memory handler as appropriate
     to make space for it. */

  error_clear_newerror_context(errcontext);
  action = 0;

  /* The loop is here to allow a retry in case we didn't have enough memory the
   * first time. We may allow partial paint if called from the single
   * controlled place - but nowhere else within this loop.
   * NB. This currently protects cases where setg hasn't already been called.
   */
  dl_safe_recursion++;

  for (;;) {
    Bool loopResult, got_reserves, no_error;
    int current_dl_safe_recursion = dl_safe_recursion;
    dl_erase_nr eraseno_before = context->page->eraseno;

    HQASSERT(!error_signalled_context(errcontext),
             "Shouldn't be starting image in error state");

    loopResult = im_createimageobj(colorType, imageargs, imagedata,
                                   &imageobj, &imagedata->opt_matrix);
    state->imageobj = imageobj;
    if ( imageobj != NULL )
      im_addimagelist(&context->page->im_list, imageobj);

    if ( loopResult && isMask )
      loopResult = im_createimageobj(colorType, imageargs->maskargs,
                                     imagedata->imaskdata, &imageobj->mask,
                                     NULL);

    /* Unrecoverable error occurred, so abandon the image */
    if ( ! loopResult && newerror_context(errcontext) &&
         newerror_context(errcontext) != VMERROR )
      goto ENDIMAGE;

    if ( loopResult )
      loopResult = do_imstore_prealloc(imageargs, imagedata, imageobj);
    if ( loopResult && !isMask
         /** \todo Assuming guc_backdropRasterStyle() means extra image planes
             are needed. Ideally, use resources, or TransparencyStrategy 1! */
         && guc_backdropRasterStyle(gsc_getTargetRS(gstateptr->colorInfo))
         /* If it's going to be entirely transparent, no preconversion. */
         && !stateobject_transparent(context->page->currentdlstate)
         && !groupMustComposite(context->page->currentGroup)
         && context->systemparams->TransparencyStrategy == 2 )
      loopResult = im_preconvert_prealloc(imageobj->ims);
    if ( loopResult  && isMask ) {
      /* Note, this code could be optimised - since mask should go though
       * the else clause, with theIArgsNumberOfProcs == 1, but I [who??] wanted
       * to keep the code the same as the image case, and mirroring the code in
       * im_putscanlines
       */
      loopResult = do_imstore_prealloc(imageargs->maskargs,
                                       imagedata->imaskdata, imageobj->mask);
    }

    /* Unrecoverable error occurred, so abandon the image */
    if ( !loopResult && newerror_context(errcontext) &&
         newerror_context(errcontext) != VMERROR )
      goto ENDIMAGE;
    /* If the allocations all succeeded, without using any of the reserve
     * etc. then we can skip to the next step, and finish off the image
     */
    if ( loopResult && !mm_memory_is_low )
      break;
    error_clear_context(errcontext); /* clear any VMerror */

    /* Don't have enough memory, do extra low memory handling (even
     * partial-paint), and try again until this succeeds or runs out of low
     * memory actions.
     *
     * im_storerelease is being called because the prealloc phase may have
     * allocated blocks from the global blist, leaving no blocks left
     * to do a partial paint. This step allows partial paint to proceed.
     */
    if (imageobj != NULL && imageobj->ims != NULL)
      im_storerelease( imageobj->ims, 0, FALSE);
    if ( imageobj != NULL
         && imageobj->mask != NULL && imageobj->mask->ims != NULL )
      im_storerelease(imageobj->mask->ims, 0, FALSE);

    /** \todo Have to grab for reserves, because a global blist assigned to the
       image in the image store could have been put back on the global blist in
       im_storerelease and be purged by low-memory action, putting this into an
       infinite loop. We hope the reserves are enough to avoid that. */

    /* Allow partial painting if it was allowed at the start of the loop */
    dl_safe_recursion = saved_dl_safe_recursion;
    no_error = low_mem_regain_reserves_with_pp(&got_reserves, context);
    dl_safe_recursion = current_dl_safe_recursion;

    /* If we did a partial paint, the image object is no longer valid,
     * since the dl pools will have been destroyed under our feet. */
    if ( context->page->eraseno != eraseno_before )
      state->imageobj = imageobj = NULL;

    if ( !no_error )
      goto ENDIMAGE;
    if ( !got_reserves && context->page->eraseno == eraseno_before ) {
      /* Didn't regain all reserves, and couldn't partial paint. */
      if ( loopResult )
        /* Allocations succeeded using reserve. Continue with the image (can
         * survive im_storerelease putting image blists on the global list). */
        break;
      else {
        (void)error_handler(VMERROR);
        goto ENDIMAGE;
      }
    }
    /* If there was a partial paint, currentdlstate and dlc_currentcolor
     * need regenerating with a new setg. */
    if ( context->page->eraseno != eraseno_before )
      if ( !DEVICE_SETG(imagedata->page, colorType, DEVICE_SETG_RETRY) )
        goto ENDIMAGE;

    /* Free up any image DL memory created if there was an error. */
    if ( imageobj != NULL ) {
      im_freeobject( imageobj, imagedata->page);
      state->imageobj = imageobj = NULL;
    }
    if ( !striping )
      im_free_late_imagedata(imagedata);
  }

  /* Remove the increment at the beginning of the above loop. */
  dl_safe_recursion--;

  /* Finish; do all once-only stuff here, and any remaining allocations
   * that we aren't bothered about retrying if they fail, esp. for rotated
   * images in im_generatetiles. NB. We don't worry if the tile allocations
   * succeed because we have rendering functions to cope.
   */

  if ( ! im_finishimageobj( imageargs, imagedata, imageobj ))
    goto ENDIMAGE;

  if ( isMask && !im_finishimageobj(imageargs->maskargs, imagedata->imaskdata,
                                    imageobj->mask) )
    goto ENDIMAGE;

  dlc_current = dlc_currentcolor(imagedata->page->dlc_context);

  if ( degenerateClipping || dlc_is_none(dlc_current) ) {
    /* If we have degenerate clipping the current colour may or may not happen
       to be none.  It's safest to set the image state colour to none. */
    dlc_get_none(imagedata->page->dlc_context, &state->dlc) ;
  } else if ( colorType == GSC_FILL ) {
    /* For image masks, we need to set the image's DL color to the color
       setup by setg(). */
    dlc_copy_release(imagedata->page->dlc_context, &state->dlc, dlc_current) ;
  } else {
    /* For images, build a color with the same channels as the current color
       for the colorspace, but with the colorvalues all set to intermediate
       values. This will cause the color unpacking to prepare halftone
       screens for the image correctly. */
    int32 i ;
    COLORVALUE alpha ;
    COLORVALUE cv_fillin[BLIT_MAX_CHANNELS] ;

    HQASSERT(DLC_NUM_CHANNELS(dlc_current) < BLIT_MAX_CHANNELS,
             "Too many simultaneous channels") ;
    for ( i = 0 ; i < DLC_NUM_CHANNELS(dlc_current) ; ++i )
      cv_fillin[i] = COLORVALUE_HALF ;

    alpha = dlc_color_opacity(dlc_current) ;
    if ( alpha < COLORVALUE_ONE ) {
      /* The final colorvalue is an alpha value; it shouldn't be overridden. */
      HQASSERT(i > 0, "Alpha with no colorvalues") ;
      cv_fillin[i - 1] = alpha ;
    }

    if ( !dlc_alloc_fillin_template(imagedata->page->dlc_context, &state->dlc,
                                    dlc_current, cv_fillin,
                                    DLC_NUM_CHANNELS(dlc_current)) )
      goto ENDIMAGE ;
  }

  /* Accumulate overprint flags into im_dlc if applicable */
  if ( colorType == GSC_SHFILL )
    if ( !gsc_setblockoverprints(imagedata->colorInfo, colorType) )
      goto ENDIMAGE;

  /* Don't allocate halftone forms until the end of the colour conversion,
   * to prevent them from using the colour table reserve memory and trying to
   * leave room for the any VM the callbacks use (eg. RunLength filter).
   */
  ht_defer_allocation();

  imagedata->page->im_imagesleft += 1;

#undef return
  return TRUE;

ENDIMAGE:
  HQASSERT(!result || imagedata->degenerate,
           "Image should have failed or been discarded (marked degenerate)") ;

  if (!result) {
    /* Free up any image DL memory created if there was an error. */
    if ( imageobj != NULL ) {
      im_freeobject(imageobj, imagedata->page);
      state->imageobj = imageobj = NULL;
    }
    im_free_late_imagedata(imagedata);
  }

  probe_end(SW_TRACE_DL_IMAGE, (intptr_t)0) ;

  return result;
}

/**
 Actions to render an image as a series of sub-images. Prepares images data
 accumulated so far for addition to DL by dl_image_end.
 Each stripe is ended by a call to this function.
*/
static void dl_image_early_end(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                               void *data)
{
  dl_image_state *state = data;

  UNUSED_PARAM(IMAGEARGS *, imageargs);
  UNUSED_PARAM(IMAGEDATA *, imagedata);

  HQASSERT(imageargs, "imageargs is NULL");
  HQASSERT(imagedata, "imagedata is NULL");
  HQASSERT(state, "NULL state passed to dl_image_stripe");
  HQASSERT(imageargs->imagetype == TypeImageImage ||
           imageargs->imagetype == TypeImageMask,
          "Invalid image type for striping.");

  state->stripe_state = StripeBegun;
}

/**
 * Actions needed to continue processing an image as series of sub-images
 * (stripes). A counterpart to the actions taken in dl_image_early_end.
 */
static void dl_image_stripe(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                            void *data)
{
  dl_image_state *state = data;

  UNUSED_PARAM(IMAGEARGS *, imageargs) ; /* assert only */
  UNUSED_PARAM(IMAGEDATA *, imagedata) ; /* assert only */

  HQASSERT(imageargs, "imageargs is NULL");
  HQASSERT(imagedata, "imagedata is NULL");
  HQASSERT(state, "dl_image_state is NULL");

  HQASSERT(imageargs->imagetype == TypeImageImage ||
           imageargs->imagetype == TypeImageMask,
          "Invalid image type for striping.");

  HQASSERT(imageargs->height >= 0, "image height must be > 0");

  state->stripe_state = StripeEnded;

  /* Cannot free late imagedata here, because of the virtual striping done in
   * the clipped image optimisation - which does a stripe without adding to DL.
   */
#if 0
  im_free_late_imagedata(imagedata);
#endif

  HQASSERT(imagedata->lines_left_in_block > 0, "No lines per block in stripe");
}

/** Helper function for dl_image_clip_optimize. This is called on the image
    and mask objects separately. */
static void dl_image_clip_helper(IMAGEOBJECT *imageobj, int32 *y1, int32 *y2)
{
  const ibbox_t *imsbbox = &imageobj->imsbbox ;

  if (degenerateClipping || bbox_is_empty(imsbbox) ) {
    *y1 = *y2 + 1 ; /* Omit all lines */
    return ;
  }

  /* This is called pre-swapping, so we need to apply the swap here. We'll
     still use the image space bbox computed during im_finishimageobj(). */
  if ( (imageobj->optimize & IMAGE_OPTIMISE_SWAP) == 0 ) {
    *y1 = imsbbox->y1 ;
    *y2 = imsbbox->y2 ;
  } else {
    *y1 = imsbbox->x1 ;
    *y2 = imsbbox->x2 ;
  }
}

/**
 *  Determine the first and last row limits that may be discarded from the
 *  image due to the clipping.
 */
static void dl_image_clip_optimize(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                   void *data, int32 *iy1, int32 *iy2,
                                   int32 *my1, int32 *my2)
{
  dl_image_state *state = data;
  IMAGEOBJECT *imageobj ;

  UNUSED_PARAM(IMAGEARGS *, imageargs)
  UNUSED_PARAM(IMAGEDATA *, imagedata)

  /* don't try to optimize if we are attempting charpaths. */
  if ( char_doing_charpath() )
    return;

  /* im_finishimageobj() restricts the image space bounds to the area that
     possibly intersects the clip bbox. We need to relate that area back to
     the original image orientation. */
  imageobj = state->imageobj ;
  HQASSERT(imageobj != NULL, "No DL image object") ;

  dl_image_clip_helper(imageobj, iy1, iy2) ;

  if ( imageobj->mask != NULL )
    dl_image_clip_helper(imageobj->mask, my1, my2) ;
}

static Bool dl_image_end(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                         void *data, Bool abort)
{
  dl_image_state *state = data;
  int32 colorType;
  IMAGEOBJECT *imageobj;
  int32 imagetype;
  Bool result = TRUE;
  Bool striping = FALSE;
  dl_color_t *dlc_current;
  dbbox_t dbbox ;

  /* dl_image_end operates in modified way when image striping. HDLT callbacks
   * and end image hooks etc. apply only when not striping or processing the
   * last stripe of an image. The stripe state indicates the striping action.
   */

  /* action == NoStripe -> image not striped. action == StripeBegun ->
   * image is normal stripe. action == StripeEnded -> stripe is last stripe */
  striping = ( state->stripe_state == StripeBegun );

  if ( imagedata->degenerate || /* Ignore degenerate image */
       char_doing_charpath() )
    return TRUE;

#define return DO_NOT_RETURN_goto_ENDIMAGE_INSTEAD!
  imageobj = state->imageobj;
  HQASSERT(imageobj, "Image object missing");

  imagetype = imageargs->imagetype;
  HQASSERT(VALID_IMAGETYPE( imagetype ), "bad image type");

  colorType = imageargs->colorType;
  HQASSERT(colorType == GSC_FILL || colorType == GSC_IMAGE ||
           colorType == GSC_SHFILL, "Strange colorType for image");

  if (!striping ) {
    if ( pdfout_enabled() &&
         ! pdfout_endimage(get_core_context_interp()->pdfout_h,
                           imagedata->image_lines, imageargs)) {
      result = FALSE;
      goto ENDIMAGE;
    }

    switch ( IDLOM_ENDIMAGE( colorType, imageargs )) {
    case NAME_false:             /* PS error in IDLOM callbacks */
      result = FALSE;
      goto ENDIMAGE;
    case NAME_Discard:           /* just pretending */
      goto ENDIMAGE;
    default:                     /* only add, for now */
      HQFAIL("Unexpected return for HDLT image end callback");
      /*@fallthrough@*/
    case NAME_Add:
      break;
    }
  }

  if ( abort ||                       /* Told to throw away image */
       degenerateClipping )           /* Check for degenerate clipping. */
    goto ENDIMAGE;

  /* Adjust the end of the image space bbox to reflect the number of lines
     actually read from the data. */
  if ( (imageobj->optimize & IMAGE_OPTIMISE_SWAP) == 0 ) {
    imageobj->imsbbox.y2 = imagedata->image_lines - 1 ;
  } else {
    imageobj->imsbbox.x2 = imagedata->image_lines - 1 ;
  }

  if ( bbox_is_empty(&imageobj->imsbbox) ) /* No data in stripe */
    goto ENDIMAGE ;

  if ( imageobj->mask ) {
    IMAGEOBJECT *maskobj = imageobj->mask ;
    IMAGEDATA *imaskdata = imagedata->imaskdata ;

    if ( (maskobj->optimize & IMAGE_OPTIMISE_SWAP) == 0 ) {
      maskobj->imsbbox.y2 = imaskdata->image_lines - 1 ;
    } else {
      maskobj->imsbbox.x2 = imaskdata->image_lines - 1 ;
    }

    if ( bbox_is_empty(&maskobj->imsbbox) ) /* No data in stripe */
      goto ENDIMAGE ;
  }

  /* Accumulate overprint flags into state->dlc if applicable. Note that this
     function is always called for shfill images, because HCMS may have set
     up state->dlc with entirely inappropriate maxblits for the image. The
     apply block overprints replaces the overprint information with nothing
     if there are no overprints. */
  if ( colorType == GSC_SHFILL )
    if ( !gsc_applyblockoverprints(imagedata->colorInfo, colorType,
                                   imagedata->page, &state->dlc) ) {
      result = FALSE;
      goto ENDIMAGE;
    }

  gsc_clearblockoverprints(imagedata->colorInfo, colorType);

  SwOftenUnsafe();

  /* End of the image data collection - add to D.L. */
  if ( imageobj->ims )
    if ( !im_storeclose(imageobj->ims) ) {
      result = FALSE;
      goto ENDIMAGE;
    }
  if ( imageobj->mask ) {
    if ( imageobj->mask->ims )
      if ( !im_storeclose(imageobj->mask->ims) ) {
        result = FALSE;
        goto ENDIMAGE;
      }
  }

  if ( imagedata->all_components_same ) {
    IM_EXPAND *ime = imageobj->ime ;
    int32 i ;
    COLORANTINDEX ci, blackIndex ;
    COLORVALUE cv, cv_fillin[BLIT_MAX_CHANNELS] ;
    dl_color_t dlc ;
    dl_color_iter_t iter ;
    dlc_iter_result_t res ;
    /* All of the channels in the image had the same data. For now, we're
       just checking RGB data, and we have color converted the data to CMYK
       using the fast RGB to CMYK method, which uses naive full black
       generation. We can use this knowledge to discard the C, M, and Y
       planes, and put zero LUTs in their place, and an identity LUT in the K
       plane. In future, we probably want to retain one plane of original
       data and swap it into the store if we do this, or discard it if we
       don't. */
    /** \todo ajcd 2013-11-20: Don't use page black index, use
        imagedata->colorInfo's black index. */
    blackIndex = imagedata->page->virtualBlackIndex ;
    HQASSERT(blackIndex >= 0, "No black index") ;
    HQASSERT(imagedata->method == GSC_USE_FASTRGB2CMYK,
             "Unexpected color conversion method") ;
    if ( !im_storereorder(imageobj->ims, im_getcis(ime), &blackIndex, 1) ||
         !im_expandtolut(imagedata->page, imageobj->ime,
                         imagedata->colorInfo, blackIndex) ) {
      result = FALSE ;
      goto ENDIMAGE ;
    }
    /* Change all but the black channel to COLORVALUE_ONE, so they can be
       omitted. */
    HQASSERT(DLC_NUM_CHANNELS(&state->dlc) < BLIT_MAX_CHANNELS,
             "Too many simultaneous channels") ;
    for ( i = 0, res = dlc_first_colorant(&state->dlc, &iter, &ci, &cv) ;
          res == DLC_ITER_COLORANT ;
          ++i, res = dlc_next_colorant(&state->dlc, &iter, &ci, &cv) ) {
      if ( ci != blackIndex )
        cv = COLORVALUE_ONE ;
      cv_fillin[i] = cv ;
    }

    cv = dlc_color_opacity(&state->dlc) ;
    if ( cv < COLORVALUE_ONE ) {
      /* The final colorvalue is an alpha value; it shouldn't be overridden. */
      cv_fillin[i++] = cv ;
    }

    dlc_clear(&dlc) ;
    if ( !dlc_alloc_fillin_template(imagedata->page->dlc_context, &dlc,
                                    &state->dlc, cv_fillin, i) ) {
      result = FALSE ;
      goto ENDIMAGE ;
    }
    dlc_copy_release(imagedata->page->dlc_context, &state->dlc, &dlc) ;
  }

#ifdef DEBUG_MASK_GENERATION
  /* display only the mask of a masked image as if generated by imagemask */
  if ( imagetype == TypeImageMaskedImage ) {
    imageobj = imageobj->mask;
    imageargs->imagetype = imagetype = TypeImageMask;
  }
#endif

#ifdef DEBUG_BUILD
  /* Allow skipping of images based on sequence number, optimisation flags,
     and mask/image type. */
  if ( debug_dl_skipimage(imagedata->optimise |
                          ((imageobj->geometry.wx == 0 &&
                            imageobj->geometry.wy != 0 &&
                            imageobj->geometry.hx != 0 &&
                            imageobj->geometry.hy == 0) << 4) |
                          (imagetype << 8)) )
    goto ENDIMAGE;
#endif

  /* Set the image device space bbox by transforming the image space bbox to
     device space, and intersecting with the clip bbox. The image space bbox
     was originally set by transforming the device space bbox, but may have
     been shrunk by striping the image. */
  if ( !image_dbbox_covering_ibbox(imageobj, &imageobj->imsbbox, &dbbox) )
    goto ENDIMAGE ;

  bbox_intersection(&dbbox, &cclip_bbox, &imageobj->bbox) ;
  if ( bbox_is_empty(&imageobj->bbox) )
    goto ENDIMAGE ;

  if ( imagetype == TypeImageMaskedImage ||
       imagetype == TypeImageAlphaImage ) {
    IMAGEOBJECT *maskobj = imageobj->mask ;

    if ( !image_dbbox_covering_ibbox(maskobj, &maskobj->imsbbox, &dbbox) )
      goto ENDIMAGE ;

    bbox_intersection(&dbbox, &cclip_bbox, &maskobj->bbox) ;
    if ( bbox_is_empty(&maskobj->bbox) )
      goto ENDIMAGE ;
  }

  /* Reinstate previous color to get planes right in lobj */
  dlc_current = dlc_currentcolor(imagedata->page->dlc_context);
  dlc_copy_release(imagedata->page->dlc_context, dlc_current,
                   &state->dlc);

  /* For blank presep images, test the result of putting white thru the
     transfers. This is used to detect knockouts (planes of image data that
     are totally white). Need to tweak the dl color for recombine color
     adjustment later on */
  if ( imagetype == TypeImageImage && imagedata->presep ) {
    COLORVALUE presepColorValue;
    COLORSPACE_ID colorSpace;
    USERVALUE white[4];
    COLORVALUE presepDeviceCode;
    int32 incomps = imageargs->ncomps;
    Bool fTestImagePlane = TRUE;

    HQASSERT(DLC_NUM_CHANNELS(dlc_current) == 1, "expected only one colorant");
    HQASSERT(rcbn_enabled(), "expected recombine to be enabled");

    /* Preseparated color chain ends in additive; choose any non-white color
       (this will only be used for indexed color images).  */
    presepDeviceCode = COLORVALUE_PRESEP_BLACK;

    colorSpace = ( COLORSPACE_ID )imageargs->image_color_space;
    switch ( colorSpace ) {
    case SPACE_DeviceGray :
    case SPACE_CIEBasedA :
    case SPACE_CalGray :
      white[0] = 1.0f;
      HQASSERT(incomps == 1, "incomps disagrees with colorSpace(1)");
      break;
    case SPACE_DeviceRGB :
      white[0] = white[1] = white[2] = 1.0f;
      HQASSERT(incomps == 3, "incomps disagrees with colorSpace(2)");
      break;
    case SPACE_DeviceCMYK :
      white[0] = white[1] = white[2] = white[3] = 0.0f;
      HQASSERT(incomps == 4, "incomps disagrees with colorSpace(3)");
      break;
    case SPACE_Separation :
    case SPACE_DeviceN :
      white[0] = 0.0f;
      HQASSERT(incomps == 1, "incomps disagrees with colorSpace(4)");
      break;
    default :
      HQASSERT(colorSpace == SPACE_Indexed || colorSpace == SPACE_ICCBased,
               "should only be Indexed or ICCBased at this point");
      fTestImagePlane = FALSE;
      break;
    }

    if ( fTestImagePlane ) {
      if ( ! gsc_invokeChainBlock( imagedata->colorInfo, colorType,
                                   white, & presepDeviceCode, incomps )) {
        result = FALSE;
        goto ENDIMAGE;
      }
    }

    if ( presepDeviceCode == COLORVALUE_PRESEP_WHITE && /* is it a knockout? */
         im_rcb_blankimage( imageobj, imagedata->plane )) {
      /* Image plane is all zeroes, so use white in the dl color. */
      presepColorValue = COLORVALUE_PRESEP_WHITE;
    } else {
      /* Not a knockout; use black in the dl color. */
      presepColorValue = COLORVALUE_PRESEP_BLACK;
    }
    if ( !dlc_replace_indexed_colorant(imagedata->page->dlc_context,
                                       dlc_current,
                                       imagedata->plane,
                                       presepColorValue) ) {
      result = FALSE;
      goto ENDIMAGE;
    }
  }

  /* The alpha channel is often completely solid which is wasteful
     (of memory and compositing effort).  Scan the alpha channel and
     if it does turn out to be completely solid it can be deleted. */
  if ( imagetype == TypeImageAlphaImage ) {
    COLORVALUE alpha;

    if ( im_expanduniform(imageobj->mask->ime, imageobj->mask->ims,
                          &alpha, 1) &&
         alpha == COLORVALUE_ONE ) {
      im_freeobject(imageobj->mask, imagedata->page);
      imageobj->mask = NULL;

      /* NOTE: Don't change imageargs->imagetype because that still needs
         to be set to TypeImageAlphaImage so the alpha channel's imageargs
         and imagedata structs are freed.  This routine should use the
         imagetype local from now on. */
      imagetype = TypeImageImage;
    }
  }

  if ( imagetype == TypeImageAlphaImage ) {
    /* TIFF/PNG images with an alpha channel. */
    if ( !im_expandaddalpha(imagedata->page, imageobj) ) {
      result = FALSE;
      goto ENDIMAGE;
    }
  }

  if ( !addimagedisplay(imagedata->page, imageobj, imagetype) ) {
    result = FALSE;
    goto ENDIMAGE;
  }

  HQASSERT(result, "Fell through to image success, but result is FALSE");
  imageobj = NULL; /* Success; clear imageobj so it won't get freed */

 ENDIMAGE: /* Target for error and early exit cases */

  /* Call the end image hook if required */
  if (!striping &&
      !runHooks(& theIgsDevicePageDict(gstateptr), GENHOOK_EndImage))
    result = FALSE;

  ht_resume_allocation(gsc_getSpotno(imagedata->colorInfo), result);

  imagedata->page->im_imagesleft -= 1;

  /* Free up any image DL memory created if there was an error. */
  if ( imageobj != NULL )
    im_freeobject(imageobj, imagedata->page);

  probe_end(SW_TRACE_DL_IMAGE, (intptr_t)state->imageobj) ;

  state->imageobj = NULL;

#undef return
  return result;
}

/**
 * Is image stripping allowed ?
 *
 * Disable striping when there is HDLT enabled. It changes the image data
 * available, and it is not yet clear what should be presented to the HDLT
 * client. The adjustment of the image grids in the striping means that the
 * images reported to HDLT would be translated wrt to the reported matrix. If
 * the HDLT client decided to replace the images, the position of the
 * replacement objects would not necessarily co-incide with the placement of
 * the image stripes.
 *
 * As recombine requires size of the objects be used to decide which objects
 * are related, striping should be disabled for recombine.
 */
static Bool im_stripe_allowed(corecontext_t *context, int32 imagetype)
{
  return context->systemparams->StripeImages
    && !isHDLTEnabled(*gstateptr) && !rcbn_enabled()
    && (imagetype == TypeImageImage || imagetype == TypeImageMask);
}

/* Disable early image clipping when there is HDLT enabled. It changes the
 * image data available, and it is not yet clear what should be presented
 * to the HDLT client.
 * As recombine requires size of the objects be used to decide which objects
 * are related, striping should be disabled for recombine. It is debatable as
 * to whether clipped image optimization is adversely affected by early
 * clipping.
 */
static Bool im_early_clip_allowed(corecontext_t *context,
                                  int32         imagetype,
                                  IMAGEARGS     *imageargs)
{
  return context->systemparams->OptimizeClippedImages &&
         !isHDLTEnabled(*gstateptr) && !rcbn_enabled() &&
         /* no early clip optimisation for filtered images or masked/alpha images. */
         /** \todo ajcd 2014-01-03: Can we allow masked/alpha images? */
         (imagetype == TypeImageImage || imagetype == TypeImageMask) &&
         imageargs->filteredImage == NULL;
}


static Bool image_forcedpp = FALSE; /* For patching in debugger */


static Bool dl_image_data(int32 data_type,
                           IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                           uint32 lines, void *data)
{
  double tb = imageargs->width * imageargs->height *
              imageargs->bits_per_comp * imageargs->ncomps/8.0;
  dl_image_state *state = data;
  Bool ok;

  HQASSERT(state, "No state");

  if ( imagedata->degenerate || /* Ignore degenerate image */
       char_doing_charpath() )
    return TRUE;

  state->is_mask_data = (data_type == NAME_Mask);

  ok = im_convertlines(imageargs, imagedata, lines, dl_imagedatafn, data);
  /*
   * Big images are a problem because what we do is limp on as far as we
   * can, doing low-mem actions, until we finally totally run-out of memory.
   * At that point we can stripe the image and partial-paint. But by then its
   * often too late. What we need to do is force image striping to happen
   * early, and then force a partial-paint, even if we are not 100% out of
   * memory. That way there is a chance for the PP to succeed without running
   * out of memory.
   *
   * Check if free memory is below the threshold to force partial painting.  It
   * may be better to stripe the image and partial paint rather than do lots of
   * other low-mem actions first.  It's difficult to estimate how much memory is
   * actually required to finish the image because of allocs like Tom's Tables
   * so a crude threshold is used.
   *
   * Pekka 16-May-2014: Partial paints succeed well enough now, and this was
   * always a temporary hack. So it's disabled now. Scheduling of partial paints
   * for efficiency purposes will be addressed in a different way.
   *
   * partial_paint_allowed test commented out for now as the logic on
   * dl_safe_recursion is wrong. What we want is not whether PP is allowed now,
   * but whether it would be allowed at the top of the image loop. This needs
   * to be fixed if this is ever used again.
   *
   * todo BMJ 12-Mar-14 : Review logic of forced PP and find better solution
   *                       for hard-coded heuristic thresholds.
   */
  if ( image_forcedpp && tb > 8.0*1024*1024 ) {
    corecontext_t *context = get_core_context_interp();
    if ( im_stripe_allowed(context, imageargs->imagetype) &&
         /* partial_paint_allowed(context) && */
         mm_no_pool_size(FALSE) <  4.0*1024*1024 ) {
      ok = error_handler(VMERROR);
    }
  }
  return ok;
}

Bool dodisplayimage(DL_STATE *page, STACK *stack, IMAGEARGS *imageargs)
{
  dl_image_state state;

  state.stripe_state = NoStripe;
  return im_common(page, stack, imageargs, &state,
                   dl_image_begin, dl_image_end, dl_image_data,
                   dl_image_early_end, dl_image_stripe,
                   dl_image_clip_optimize);
}

/** Null device images still received HDLT callbacks. */
static Bool null_image_begin(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                             void *data)
{
  UNUSED_PARAM(void *, data);

  if ( imagedata->degenerate ||
       char_doing_charpath() )
    return TRUE;

  switch ( IDLOM_BEGINIMAGE(imageargs->colorType,
                            imageargs,
                            imagedata->expandformat == ime_colorconverted,
                            imagedata->oncomps,
                            imagedata->out16,
                            imagedata->u.idecodes) ) {
  case NAME_false:   /* Error in callback or in setup */
    return FALSE;
  case NAME_Discard: /* Single-callback form wants to throw it away */
    /* Pretend image is degenerate, so data and end callback are ignored. */
    imagedata->degenerate = TRUE;
    break;
  case NAME_Add:     /* Single-callback form wants to add it now */
    /* Disable HDLT until the end of im_common, so image data and End
       callbacks won't be called. */
    theIdlomState(*gstateptr) = HDLT_DISABLED;
    break;
  default:
    HQFAIL("HDLT image Begin callback returned unexpected value");
    /*@fallthrough@*/
  case NAME_End:     /* Defer what to do to End callback */
    break;
  }

  return TRUE;
}

static Bool null_image_end(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                           void *data, Bool abort)
{
  UNUSED_PARAM(void *, data);
  UNUSED_PARAM(int32, abort);

  if ( imagedata->degenerate ||
       char_doing_charpath() )
    return TRUE;

  switch ( IDLOM_ENDIMAGE(imageargs->colorType, imageargs) ) {
  case NAME_false:             /* PS error in IDLOM callbacks */
    return FALSE;
  case NAME_Discard:           /* just pretending */
    return TRUE;
  default:                     /* only add, for now */
    HQFAIL("Unexpected return for HDLT image end callback");
    /*@fallthrough@*/
  case NAME_Add:
    break;
  }

  return TRUE;
}

static Bool null_imagedatafn(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                             uint8 **tbuf, int32 nprocs, void *data)
{
  IM_BUFFER *imb;

  UNUSED_PARAM(void *, data);
  UNUSED_PARAM(int32, nprocs);

  HQASSERT(imageargs, "No image args");
  HQASSERT(imagedata, "No image data");

  imb = imagedata->imb;
  HQASSERT(imb, "somehow lost image buffer");

  return IDLOM_IMAGEDATA(imageargs->colorType, imageargs,
                         imageargs->nprocs,
                         imb->obuf, imagedata->bytes_in_image_line,
                         imagedata->oncomps, tbuf[0],
                         imageargs->width * imagedata->oncomps *
                         (imagedata->out16 ? 2 : 1),
                         imagedata->out16);
}

static Bool null_image_data(int32 data_type,
                            IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                            uint32 lines, void *data)
{
  UNUSED_PARAM(int32, data_type);

  if ( imagedata->degenerate || char_doing_charpath() )
    return TRUE;

  return im_convertlines(imageargs, imagedata, lines, null_imagedatafn, data);
}

static void null_image_stripe(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                              void *data)
{
  UNUSED_PARAM(IMAGEARGS *, imageargs);
  UNUSED_PARAM(IMAGEDATA *, imagedata);
  UNUSED_PARAM(void *,data);
}

static void null_image_early_end(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                 void *data)
{
  UNUSED_PARAM(IMAGEARGS *, imageargs);
  UNUSED_PARAM(IMAGEDATA *, imagedata);
  UNUSED_PARAM(void *,data);
}

static void null_image_clip_optimize(IMAGEARGS *imageargs,
                                     IMAGEDATA *imagedata,
                                     void *data, int32 *iy1, int32 *iy2,
                                     int32 *my1, int32 *my2)
{
  UNUSED_PARAM(IMAGEARGS *, imageargs);
  UNUSED_PARAM(IMAGEDATA *, imagedata);
  UNUSED_PARAM(void *, data);
  UNUSED_PARAM(int32 *, iy1);
  UNUSED_PARAM(int32 *, iy2);
  UNUSED_PARAM(int32 *, my1);
  UNUSED_PARAM(int32 *, my2);
}

Bool donullimage(DL_STATE *page, STACK *stack, IMAGEARGS *imageargs)
{
  return im_common(page, stack, imageargs, NULL,
                   null_image_begin, null_image_end, null_image_data,
                   null_image_early_end, null_image_stripe,
                   null_image_clip_optimize);
}

static void im_freeimagedata( IMAGEARGS *imageargs,
                              IMAGEDATA *imagedata )
{
  int32 imagetype = imageargs->imagetype;

  /* Free up any temporary workspace we used. */
  if ( imagetype == TypeImageMaskedImage ||
       imagetype == TypeImageAlphaImage ) {
    IMAGEARGS *imaskargs = imageargs->maskargs;
    IMAGEDATA *imaskdata = imagedata->imaskdata;

    if ( imaskdata ) { /* maybe null if partially constructed */
      if (imageargs->interleave == INTERLEAVE_SCANLINE) {
        /* Buffer was shared. Remove the right one */
        HQASSERT(imagedata->sharedIMB ^ imaskdata->sharedIMB,
                 "Exactly one of image or mask buffer should be allocated");
        if ( imagedata->sharedIMB )
          imaskdata->imb = NULL;
        else
          imagedata->imb = NULL;
      }
      im_free_data(imaskargs, imaskdata);
    }
  }
  im_free_data(imageargs, imagedata);
}

static Bool swap_for_speed(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                           OMATRIX *mptr)
{
  DL_STATE *page = imagedata->page ;

  /** \todo ajcd 2013-06-25: These should take into account the image
      placement and clipping to determine how much of the w/h are actually
      renderable. We haven't yet optimised or quantised the image matrix, so
      we'd have to repeat all of those calculations here. */
  if ( (mptr->opt & MATRIX_OPT_1001) == 0 ) {
    /* The matrix may be non-invertible at this point. */
    if ( mptr->matrix[0][0] != 0.0 && mptr->matrix[1][1] != 0.0 ) {
      int32 w_across_x = min(imageargs->width,
                             (int32)fabs(page->page_w / mptr->matrix[0][0])) ;
      int32 h_across_y = min(imageargs->height,
                             (int32)fabs(page->band_lines / mptr->matrix[1][1])) ;
      /** \todo ajcd 2013-06-25: Add fudge factor for swap time? */
      /* Only swap if h across y is strictly greater than w across x. */
      return (h_across_y > w_across_x) ;
    }
  } else if ( (mptr->opt & MATRIX_OPT_0011) == 0 ) {
    /* The matrix may be non-invertible at this point. */
    if ( mptr->matrix[1][0] != 0.0 && mptr->matrix[0][1] != 0.0 ) {
      int32 h_across_x = min(imageargs->height,
                             (int32)fabs(page->page_w / mptr->matrix[1][0])) ;
      int32 w_across_y = min(imageargs->width,
                             (int32)fabs(page->band_lines / mptr->matrix[0][1])) ;
      /** \todo ajcd 2013-06-25: Add fudge factor for swap time? */
      /* Only swap if h across x is strictly greater than w across y. */
      return (h_across_x > w_across_y) ;
    }
  } else {
    HQFAIL("Not an orthogonal image") ;
  }

  return FALSE ;
}

#define MAYBE_1TO1(m00, rwidth) \
  (fabs(m00) - 1.0 == 0.0 || (rwidth) * fabs(m00) - (rwidth) < 0.5)

static void im_optimisematrix(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  OMATRIX *mptr = &imagedata->opt_matrix;
  int32 optimise = 0;
  int32 rwidth, rheight;
  Bool ismask ;

  HQASSERT(imageargs, "No image args");
  HQASSERT(mptr, "mptr NULL");

  rwidth = imageargs->width;
  rheight = imageargs->height;

  /* If this is the mask for an image, we will already have set the
     optimisation flags for the image, and we need to use the same swap
     matrix modifications. The caller propagates the image's IMAGEDATA
     optimise flags to the mask's IMAGEDATA, so we'll use that as a guide on
     what to do. */
  ismask = (imageargs->imagetype == TypeImageMaskedMask ||
            imageargs->imagetype == TypeImageAlphaAlpha) ;

  if ( mptr->opt != MATRIX_OPT_BOTH ) {
    /* Orthogonal transformation. */

    if (( mptr->opt & MATRIX_OPT_1001 ) == 0 ) {
      /* Swap to improve speed if the image width across the band width is
         sufficiently smaller than the image height across the band height to
         compensate for the time taken to swap. */
      if ( ismask
           ? (imagedata->optimise & IMAGE_OPTIMISE_SWAP) != 0
           : ((imageargs->flipswap & IMAGE_OPTIMISE_SWAP4SPEED) != 0 &&
              swap_for_speed(imageargs, imagedata, mptr)) ) {
        /* Only do the swap if we won't mess up 1:1 copydot optimisations. */
        if ( (imageargs->flipswap & IMAGE_OPTIMISE_1TO1) == 0 ||
             !MAYBE_1TO1(mptr->matrix[0][0], imageargs->width) ) {
          /* Swap major and minor components, so X axis now becomes Y axis. */
          mptr->matrix[1][0] = mptr->matrix[0][0];
          mptr->matrix[0][1] = mptr->matrix[1][1];
          mptr->matrix[0][0] = mptr->matrix[1][1] = 0.0;
          optimise |= IMAGE_OPTIMISE_SWAP;
          rwidth = imageargs->height;
          rheight = imageargs->width;
        }
      }
    }
    else if (( mptr->opt & MATRIX_OPT_0011 ) == 0 ) {
      /* Swap if forcing device row rendering, or to improve speed
         if the image height across the band width is sufficiently smaller
         than the image width across the band height to compensate for the
         time taken to swap. */
      if ( ismask
           ? (imagedata->optimise & IMAGE_OPTIMISE_SWAP) != 0
           : ((imageargs->flipswap & IMAGE_OPTIMISE_SWAP) != 0 ||
              ((imageargs->flipswap & IMAGE_OPTIMISE_SWAP4SPEED) != 0 &&
               swap_for_speed(imageargs, imagedata, mptr)) ||
              ((imageargs->flipswap & IMAGE_OPTIMISE_1TO1) != 0 &&
               MAYBE_1TO1(mptr->matrix[1][0], imageargs->height))) ) {
        /* Swap major and minor components, so X axis now becomes Y axis. */
        mptr->matrix[0][0] = mptr->matrix[1][0];
        mptr->matrix[1][1] = mptr->matrix[0][1];
        mptr->matrix[0][1] = mptr->matrix[1][0] = 0.0;
        optimise |= IMAGE_OPTIMISE_SWAP;
        rwidth = imageargs->height;
        rheight = imageargs->width;
      }
    } else {
      HQFAIL("Image matrix optimisation invalid") ;
    }

    /* If certain conditions are met, then the RIP can blit the source
     * image[mask] without performing any transformations.
     * These conditions are:
     *  a) x axis locked (and obviously therefore not rotated).
     *  b) image or imagemask (not masked image mask).
     *  c) not contone (including run length, i.e. bits per output pixel is 1).
     *  d) not inside a pattern.
     */

    /* We don't care about the image type any more for this optimisation,
       because it's only acted upon by the image blit functions for
       appropriate surfaces. */
    if ( (imageargs->flipswap & IMAGE_OPTIMISE_1TO1) != 0 &&
         MAYBE_1TO1(mptr->matrix[0][0], rwidth) )
      optimise |= IMAGE_OPTIMISE_1TO1;

    if ( imageargs->fit_edges ) { /* Compensate for off-by-one imaging */
      if ( mptr->matrix[0][0] > 0.0 )
        mptr->matrix[0][0] += 1.0 / (SYSTEMVALUE)rwidth;
      else if ( mptr->matrix[0][0] < 0.0 ) {
        mptr->matrix[0][0] -= 1.0 / (SYSTEMVALUE)rwidth;
        mptr->matrix[2][0] += 1.0 ;
      }
      if ( mptr->matrix[1][1] > 0.0 )
        mptr->matrix[1][1] += 1.0 / (SYSTEMVALUE)rheight;
      else if ( mptr->matrix[1][1] < 0.0 ) {
        mptr->matrix[1][1] -= 1.0 / (SYSTEMVALUE)rheight;
        mptr->matrix[2][1] += 1.0 ;
      }
      if ( mptr->matrix[0][1] > 0.0 )
        mptr->matrix[0][1] += 1.0 / (SYSTEMVALUE)rwidth;
      else if ( mptr->matrix[0][1] < 0.0 ) {
        mptr->matrix[0][1] -= 1.0 / (SYSTEMVALUE)rwidth;
        mptr->matrix[2][1] += 1.0 ;
      }
      if ( mptr->matrix[1][0] > 0.0 )
        mptr->matrix[1][0] += 1.0 / (SYSTEMVALUE)rheight;
      else if ( mptr->matrix[1][0] < 0.0 ) {
        mptr->matrix[1][0] -= 1.0 / (SYSTEMVALUE)rheight;
        mptr->matrix[2][0] += 1.0 ;
      }
    }
  } else {
    /* Not an orthogonal image transformation. We include skew matrices in
       the 'rotated' optimisation category. */
    optimise |= IMAGE_OPTIMISE_ROTATED;

    /* Change direction if the X axis is steeper than Y axis. */
    if ( ismask
         ? (imagedata->optimise & IMAGE_OPTIMISE_SWAP) != 0
         : ((imageargs->flipswap & IMAGE_OPTIMISE_SWAP) != 0 &&
            fabs(mptr->matrix[0][0] * mptr->matrix[1][1]) <
            fabs(mptr->matrix[1][0] * mptr->matrix[0][1])) ) {
      SYSTEMVALUE ftemp;
      ftemp = mptr->matrix[0][0];
      mptr->matrix[0][0] = mptr->matrix[1][0];
      mptr->matrix[1][0] = ftemp;
      ftemp = mptr->matrix[1][1];
      mptr->matrix[1][1] = mptr->matrix[0][1];
      mptr->matrix[0][1] = ftemp;

      optimise |= IMAGE_OPTIMISE_SWAP;
      rwidth = imageargs->height;
      rheight = imageargs->width;
    }

    if ( imageargs->fit_edges ) {
      /* Compensate for off-by-one imaging */
      SYSTEMVALUE ax, ay;

      /* Extend minor axis by one pixel */
      if ( fabs(mptr->matrix[0][0]) > fabs(mptr->matrix[0][1]) ) {
        ax = ((mptr->matrix[0][0] > 0.0)-(mptr->matrix[0][0] < 0.0))/rwidth;
        ay = mptr->matrix[0][1] / (fabs(mptr->matrix[0][0]) * rwidth);
      } else if ( fabs(mptr->matrix[0][1]) > 0.0 ) {
        ax = mptr->matrix[0][0] / (fabs(mptr->matrix[0][1]) * rwidth);
        ay = ((mptr->matrix[0][1] > 0.0)-(mptr->matrix[0][1] < 0.0))/rwidth;
      } else { /* Degenerate minor axis */
        ax = ay = 0.0;
      }
      if ( mptr->matrix[0][1] < 0.0 ) {
        mptr->matrix[2][0] -= ax ;
        mptr->matrix[2][1] -= ay ;
      }
      mptr->matrix[0][0] += ax;
      mptr->matrix[0][1] += ay;

      /* Extend major axis by one pixel */
      if ( fabs(mptr->matrix[1][0]) > fabs(mptr->matrix[1][1]) ) {
        ax = ((mptr->matrix[1][0] > 0.0)-(mptr->matrix[1][0] < 0.0))/rheight;
        ay = mptr->matrix[1][1] / (fabs(mptr->matrix[1][0]) * rheight);
      } else if ( fabs(mptr->matrix[1][1]) > 0.0 ) {
        ax = mptr->matrix[1][0] / (fabs(mptr->matrix[1][1]) * rheight);
        ay = ((mptr->matrix[1][1] > 0.0)-(mptr->matrix[1][1] < 0.0))/rheight;
      } else { /* Degenerate major axis */
        ax = ay = 0.0;
      }
      if ( mptr->matrix[1][1] < 0.0 ) {
        mptr->matrix[2][0] -= ax ;
        mptr->matrix[2][1] -= ay ;
      }
      mptr->matrix[1][0] += ax;
      mptr->matrix[1][1] += ay;
    }
  }

  MATRIX_SET_OPT_BOTH(mptr); /* For safety, ensure optimise flags are OK */

  imagedata->optimise = optimise;
  imagedata->rwidth = rwidth;
  imagedata->rheight = rheight;
}

/**
 * im_setcolorspace()
 * Determines the input (PostScript job's) color space, and may also
 * result in the color chain (to output device color space) being created.
*/
static Bool im_setcolorspace(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                             int32 colorType)
{
  COLORSPACE_ID ispace;

  HQASSERT(imageargs, "imageargs NULL");

  ispace = ( COLORSPACE_ID )imageargs->image_color_space;
  if ( ispace == SPACE_DeviceGray ||
       ispace == SPACE_DeviceRGB  ||
       ispace == SPACE_DeviceCMY  ||
       ispace == SPACE_DeviceCMYK )
    return gsc_setcolorspacedirect(imagedata->colorInfo, colorType, ispace);
  else
    return push( & imageargs->image_color_object, &operandstack ) &&
           gsc_setcolorspace(imagedata->colorInfo, &operandstack, colorType);
}

static Bool im_setintercepttype(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                int32 colorType )
{
  uint8 reproType;

  HQASSERT(imageargs, "imageargs NULL");

  if ( imageargs->colorType == GSC_SHFILL )
    reproType = REPRO_TYPE_VIGNETTE;
  else
    reproType = im_reproObjectType(imageargs, imagedata);

  return gsc_setRequiredReproType(imagedata->colorInfo, colorType, reproType) ;
}

static uint8 im_reproObjectType(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  Bool possiblyPictureOrVignette;
  Bool oneBitImage;
  Bool singleRowImage;
  uint8 reproType;

  possiblyPictureOrVignette =
        !imageargs->coerce_to_fill &&
        (imageargs->imagetype == TypeImageImage ||
         imageargs->imagetype == TypeImageAlphaImage ||
         imageargs->imagetype == TypeImageMaskedImage);

  if (!possiblyPictureOrVignette)
    return REPRO_TYPE_OTHER;

  oneBitImage = imageargs->bits_per_comp == 1;
  singleRowImage = (imageargs->width == 1 || imageargs->height == 1);

  /* Pictures are NxM multi-bit images by default. 1-bit or single row images
   * can be treated as Pictures with a configuration option.
   * Vignettes are 1xN or Mx1 multi-bit images by default. 1-bit images can be
   * treated as Vignettes with a configuration option.
   * Single row, 1-bit images get treated as though they are 1-bit to avoid
   * ambiguity.
   */
  if (oneBitImage)
    reproType = gsc_getTreatOneBitImagesAs(imagedata->colorInfo);
  else {
    if (singleRowImage)
      reproType = gsc_getTreatSingleRowImagesAs(imagedata->colorInfo);
    else
      reproType = REPRO_TYPE_PICTURE;
  }

  return reproType;
}

static void im_quantisematrix(IMAGEDATA *imagedata)
{
  im_transform_t *transform ;
  const OMATRIX *mptr ;

  HQASSERT(imagedata, "No image data");
  transform = &imagedata->geometry ;
  mptr = &imagedata->opt_matrix ;

  /* All geometry fields except the width and height are invalid until
     im_quantisematrix. */
  transform->w = imagedata->rwidth ;
  transform->h = imagedata->rheight ;

  /* Convert the exact coordinates of the corner points to integral device
     space coordinates, and then adjust to get the width and height
     axis vectors. */
  SC_C2D_INT(transform->tx, mptr->matrix[2][0]);
  SC_C2D_INT(transform->ty, mptr->matrix[2][1]);
  SC_C2D_INT(transform->wx,
             mptr->matrix[0][0] * transform->w + mptr->matrix[2][0]);
  transform->wx -= transform->tx ;
  SC_C2D_INT(transform->wy,
             mptr->matrix[0][1] * transform->w + mptr->matrix[2][1]);
  transform->wy -= transform->ty ;
  SC_C2D_INT(transform->hx,
             mptr->matrix[1][0] * transform->h + mptr->matrix[2][0]);
  transform->hx -= transform->tx ;
  SC_C2D_INT(transform->hy,
             mptr->matrix[1][1] * transform->h + mptr->matrix[2][1]);
  transform->hy -= transform->ty ;

  /* Set the extent (bounding box) of the image corners */
  transform->extent.y1 = transform->extent.y2 = transform->ty ;

  if ( transform->wy < 0 ) {
    transform->extent.y1 += transform->wy ;
  } else {
    transform->extent.y2 += transform->wy ;
  }

  if ( transform->hy < 0 ) {
    transform->extent.y1 += transform->hy ;
  } else {
    transform->extent.y2 += transform->hy ;
  }

  transform->extent.x1 = transform->extent.x2 = transform->tx ;

  if ( transform->wx < 0 ) {
    transform->extent.x1 += transform->wx ;
  } else {
    transform->extent.x2 += transform->wx ;
  }

  if ( transform->hx < 0 ) {
    transform->extent.x1 += transform->hx ;
  } else {
    transform->extent.x2 += transform->hx ;
  }

  /* Adjust for off-by-one rendering. */
  transform->extent.x2 -= 1 ;
  transform->extent.y2 -= 1 ;

  /* Set the cross product and the sign of the cross product. */
  transform->cross = ((im_dcross_t)transform->wx * (im_dcross_t)transform->hy -
                      (im_dcross_t)transform->wy * (im_dcross_t)transform->hx) ;
  if ( transform->cross < 0 ) {
    transform->cross_sign = -1;
    HQASSERT(mptr->matrix[0][0] * mptr->matrix[1][1] -
             mptr->matrix[0][1] * mptr->matrix[1][0] < 0,
             "Rounded cross product doesn't match original matrix");
  } else if ( transform->cross > 0 ) {
    transform->cross_sign = 1;
    HQASSERT(mptr->matrix[0][0] * mptr->matrix[1][1] -
             mptr->matrix[0][1] * mptr->matrix[1][0] > 0,
             "Rounded cross product doesn't match original matrix");
  } else {
    /* Quantised matrix is non-invertible. */
    transform->cross_sign = 0 ;
  }
}

static Bool im_build_nfill_outline(DL_STATE *page, IMAGEARGS *imageargs,
                                   NFILLOBJECT **pnfill)
{
  Bool result;
  OMATRIX matrix;
  SYSTEMVALUE w, h;
  NFILLOBJECT *nfill;
  SYSTEMVALUE x1, y1, x2, y2, x3, y3, x4, y4;

  if ( ! im_calculatematrix( imageargs, & matrix, FALSE ))
    return FALSE;

  HQASSERT(imageargs != NULL, "imageargs NULL");
  HQASSERT(pnfill != NULL, "pnfill NULL");

  HQASSERT(rcbn_enabled(), "recombine should be enabled");

  w = (SYSTEMVALUE) imageargs->width;
  h = (SYSTEMVALUE) imageargs->height;

  MATRIX_TRANSFORM_XY( 0.0, 0.0, x1, y1, & matrix );
  MATRIX_TRANSFORM_XY( w, 0.0, x2, y2, & matrix );
  MATRIX_TRANSFORM_XY( w, h, x3, y3, & matrix );
  MATRIX_TRANSFORM_XY( 0.0, h, x4, y4, & matrix );

  path_fill_four(x1, y1, x2, y2, x3, y3, x4, y4);

  nfill = NULL;
  result = make_nfill(page, &p4cpath, NFILL_ISFILL, & nfill);
  if ( result  && nfill ) {
    dbbox_t bbox;
    Bool clipped;

    bbox_nfill(nfill, &cclip_bbox, &bbox, &clipped);
    if ( clipped ) {
      free_fill( nfill, page );
      nfill = NULL;
    }
  }

  /* Add recombine trap info. */
  if ( nfill != NULL ) {
    /* Ensure bbox is set. */
    (void)path_bbox(&i4cpath, NULL, BBOX_IGNORE_ALL|BBOX_SAVE);

    if ( !rcbt_addtrap(page->dlpools, nfill, &i4cpath, FALSE /* fDonut */, NULL) )
      result = FALSE;
  }

  (*pnfill) = nfill;

  return result;
}

/**
 * Calculates the 'bytes_in_input_line' and 'bytes_in_image_line' values for
 * the IMAGEDATA. Also sets a number of other miscellaneous items.
 * This routine may be called for either the image or the mask (type 3/4
 * images) data.
*/
Bool im_bitmapsize( IMAGEARGS *imageargs,
                    IMAGEDATA *imagedata )
{
  int32 numBytes;

  HQASSERT(imageargs, "imageargs NULL");

  /* Deal with imagemasks specially. */
  if ( imageargs->imagetype == TypeImageMask ||
       imageargs->imagetype == TypeImageMaskedMask ||
       imageargs->imagetype == TypeImageAlphaAlpha ) {
    int32 numBytes = imageargs->width;

    /* Preserve bit-depth for alpha channel. */
    if ( imageargs->imagetype == TypeImageAlphaAlpha )
      numBytes *= imageargs->bits_per_comp;

    numBytes = ( numBytes + 7 ) >> 3;
    imagedata->bytes_in_image_line = numBytes;
    imagedata->bytes_in_input_line = numBytes;

    return TRUE;
  }

  /* Calculate the size, in bytes, of a complete scanline (i.e. one row) from
     the input datasource of the image. This equals width * bpp *
     num.componants. */
  numBytes = imageargs->width * imageargs->bits_per_comp;
  if ( imageargs->nprocs <= 1 )     /* Separate datasources? */
    numBytes *= imageargs->ncomps; /* No. All comps. in one line. */

  /* Calc. min number of whole bytes from the total number of required bits. */
  imagedata->bytes_in_image_line = ( numBytes + 7 ) >> 3;

  /* Because mask and image data may be pixel interleaved, then an input
     "block" is conceived as comprising either just the image line or else
     both the image and mask lines together. So, if this image data has mask
     data interleaved per-pixel then increase the input size as if we had a
     separate component. */
  numBytes = imageargs->width * imageargs->bits_per_comp;
  if ( imageargs->nprocs == 1 ) {
    if (imageargs->interleave == INTERLEAVE_PER_SAMPLE) /* pixel interleaved */
      numBytes *= (imageargs->ncomps + 1);
    else
      numBytes *= imageargs->ncomps;
  }

  /* from num bits to whole num bytes */
  imagedata->bytes_in_input_line = ( numBytes + 7 ) >> 3;

  HQASSERT(imageargs->interleave == INTERLEAVE_PER_SAMPLE ?
           imagedata->bytes_in_input_line >= imagedata->bytes_in_image_line :
           imagedata->bytes_in_input_line == imagedata->bytes_in_image_line,
           "im_bitmapsize: bad bytes_in_image_line calculation?");

  return TRUE;
}

static Bool im_prepareimagedata(IMAGEARGS *imageargs,
                                IMAGEDATA *imagedata,
                                Bool filtering)
{
  int32 imagetype;

  HQASSERT(imageargs, "No image args");
  HQASSERT(imagedata, "No image data");

  imagetype = imageargs->imagetype;
  HQASSERT(VALID_IMAGETYPE( imagetype ), "bad image type");

  imagedata->degenerate = (imageargs->width == 0 || imageargs->height == 0);

  /* An early bail out, we'll carry on but no processing will be done */
  if ( imagedata->degenerate )
    return TRUE;

  /* calculate the size of the bitmap */
  if ( ! im_bitmapsize( imageargs, imagedata ))
    return FALSE;

  /* Extract matrix, invert, concatenate with CTM (and make the image
     orthogonal if it's nearly so anyway). */
  if ( ! im_calculatematrix( imageargs, &imagedata->orig_matrix, TRUE ))
    return FALSE;

  /* Optimise (rotate, xflip, yflip) the imaging depending on the matrix. */
  MATRIX_COPY(&imagedata->opt_matrix, &imagedata->orig_matrix);
  im_optimisematrix(imageargs, imagedata);

  /* Quantise image corners into device space so we can use DDAs. */
  im_quantisematrix(imagedata) ;

  imagedata->image_lines = 0;
  imagedata->state_line = -1;
  imagedata->state = IMD_raw;
  imagedata->lines_left_in_block = imageargs->lines_per_block;
  HQASSERT(imagedata->lines_left_in_block > 0, "No lines per block");

  if ( !filtering ) {
    int32 colorType = imageargs->colorType;
    HQASSERT(colorType == GSC_FILL ||
             colorType == GSC_IMAGE ||
             colorType == GSC_SHFILL,
             "Strange colorType for image");

    /* Decide which image color conversion route we're going to take. */
    imagedata->method =
      im_determine_method(imageargs->imagetype, imageargs->ncomps,
                          imageargs->bits_per_comp,
                          imagedata->colorInfo, colorType);
  }
  return TRUE;
}


Bool im_prepareimageoutput(IMAGEARGS *imageargs,
                           IMAGEDATA *imagedata,
                           Bool adjusting, Bool filtering)
{
  GUCR_RASTERSTYLE* hRasterStyle;
  int32 imagetype, colorType;
  Bool presep = FALSE;

  HQASSERT(imageargs, "No image args");
  HQASSERT(imagedata, "No image data");

  colorType = imageargs->colorType;
  HQASSERT(colorType == GSC_FILL || colorType == GSC_IMAGE ||
           colorType == GSC_SHFILL, "Strange colorType for image");

  imagetype = imageargs->imagetype;
  HQASSERT(VALID_IMAGETYPE( imagetype ), "bad image type");
  hRasterStyle = gsc_getTargetRS(imagedata->colorInfo);

  /* Deal with imagemasks specially. */
  if ( imagetype == TypeImageMask ||
       imagetype == TypeImageMaskedMask ||
       imagetype == TypeImageAlphaAlpha ) {
    imagedata->plane = 0;
    imagedata->oncomps = 1;

    if ( !gsc_isPreseparationChain(imagedata->colorInfo, colorType, &presep) )
      return FALSE;

    imagedata->out16 = FALSE;

    /** \todo ajcd 2010-02-26: This decision is being made too early.
        Modifying the surface selection after this decision is taken will
        affect the optimality of the surfaces. */
    if (imagetype == TypeImageAlphaAlpha &&
        surfaces_image_depth(imagedata->page->surfaces, TRUE /*transparent*/) > 8 )
      imagedata->out16 = TRUE ;
  } else {
    int32 nColorants;
    COLORANTINDEX *iColorants;

    /* Do not reset the colourspace if in recombine adjust phase. */
    if ( !adjusting ) {

      if ( !filtering ) {
        /* Call this prior to setting the colorspace for efficiency. */
        if ( ! im_setintercepttype( imageargs, imagedata, colorType ))
          return FALSE;
      }

      /* set input color space */
      if ( imagetype == TypeImageImage ||
           imagetype == TypeImageMaskedImage ||
           imagetype == TypeImageAlphaImage ) {
        if ( ! im_setcolorspace( imageargs, imagedata, colorType ))
          return FALSE;
      }

      if ( filtering ) {
        /* This uses iColorSpace set up by im_setcolorspace
         * in the chain head. */
        if ( ! im_setintercepttype( imageargs, imagedata, colorType ))
          return FALSE;
      }

      /* Store pre-separated-ness away for later use */
      if ( !gsc_isPreseparationChain(imagedata->colorInfo, colorType,
                                     &presep) )
        return FALSE;
    }

    if ( ! gsc_getDeviceColorColorants( imagedata->colorInfo, colorType,
                                        & nColorants,
                                        & iColorants ))
      return FALSE;

    imagedata->plane = 0;
    if ( presep ) {
      /* Preseparated images can only be monochromatic */
      HQASSERT(imageargs->ncomps == 1,
                "Preseparated images can only be monochromatic");
      HQASSERT(nColorants == 1,
                "Preseparated images should only have 1 (output) channel");

      imagedata->plane = rcbn_current_colorant();
    }

    imagedata->oncomps = nColorants;

    {
      /** \todo ajcd 2010-02-26: This decision is being made too early.
          Modifying the surface selection after this decision is taken will
          affect the optimality of the surfaces. */
      uint32 depth = surfaces_image_depth(imagedata->page->surfaces,
                                          guc_backdropRasterStyle(hRasterStyle)) ;
      if ( depth == 0 ) {
        /* Let the RIP choose, according to HT levels. */
        imagedata->out16 =
          ht_is16bitLevels( gsc_getSpotno( imagedata->colorInfo ),
                            gsc_getRequiredReproType(imagedata->colorInfo,
                                                     colorType),
                            nColorants, iColorants, hRasterStyle );
      } else if ( depth > 8 ) {
        imagedata->out16 = TRUE ;
      } else {
        imagedata->out16 = FALSE ; /* No point storing 16 bits. */
      }
    }
  }
  imagedata->presep = presep;

  return TRUE;
}

static Bool im_detectseparation(IMAGEARGS *imageargs)
{
  COLORSPACE_ID ispace = ( COLORSPACE_ID )imageargs->image_color_space;

  HQASSERT(imageargs->imagetype != TypeImageMask &&
           imageargs->imagetype != TypeImageMaskedMask &&
           imageargs->imagetype != TypeImageAlphaAlpha,
           "Mask has no relevance for separation detection");
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );

  if ( ispace == SPACE_Indexed ) {
    if ( !gsc_getbasecolorspace(gstateptr->colorInfo, imageargs->colorType,
                                &ispace) )
      return FALSE;
  }

  if ( ispace == SPACE_DeviceGray ) {
    if ( new_screen_detected ) { /* flag possibly screened presep page. */
      if ( !setscreen_separation(TRUE) )
        return FALSE;
    }
  } else {
    if ( new_color_detected ) {
      if ( !detect_setcolor_separation()) /* flag page as composite. */
        return FALSE;
    }
  }

  return TRUE;
}

/**
 * Allocates the IMAGEOBJECT structure and populates it.
 * This involves allocating several other structures including the
 * grid arrays, expansion buffers (used in rendering), and image store
 * structures.  Further, the IMAGEDATA structure is used to hold more values
 * including structures allocated for the Decode arrays, pack/unpack buffers,
 * and color conversion buffers.
 * This function is called once for the image proper, and once more for the
 * mask data in the case of type 3/4 images.
*/
static Bool im_createimageobj( int32 colorType,
                               IMAGEARGS *imageargs,    /* image or mask! */
                               IMAGEDATA *imagedata,    /* image or mask! */
                               IMAGEOBJECT **newimageobj,
                               OMATRIX *pMatrix )        /* NULL for mask! */
{
  IMAGEOBJECT *imageobj;
  int32 imagetype;
  int32 incomps;
  Bool presep;
  void *base;
  Bool allowInterleavedLuts;
  uint32 imflags = 0;
  GUCR_RASTERSTYLE *hRasterStyle = gsc_getTargetRS(imagedata->colorInfo);

  *newimageobj = NULL;   /* Default */

  imagetype = imageargs->imagetype;
  HQASSERT(VALID_IMAGETYPE( imagetype ), "im_createimageobj: bad type");

  incomps = imageargs->ncomps;
  presep  = imagedata->presep;

  /* Create the IMAGEOBJECT struct, + extra so we can int32-word align it. */
  base = ( IMAGEOBJECT * )dl_alloc(imagedata->page->dlpools,
                                   sizeof( IMAGEOBJECT ) + 4,
                                   MM_ALLOC_CLASS_IMAGE );
  if ( base == NULL )
    return error_handler(VMERROR);

  /* Set various fields to NULL to cater for safe freeing. */
  HqMemZero(base, sizeof(IMAGEOBJECT) + 4);

  /* IMAGEOBJECT now contains an OMATRIX which needs double word alignment */
  imageobj = (IMAGEOBJECT *)DWORD_ALIGN_UP(uintptr_t, base);
  HQASSERT(DWORD_IS_ALIGNED(uintptr_t, imageobj),
           "IMAGEOBJECT is not 8-byte aligned");

  /* Store the base address of the structure for freeing */
  imageobj->base_addr = base;

  *newimageobj = imageobj;   /* Return pointer to IMAGEOBJECT to caller */

  /* Image-space bbox is initially the remainder of the image, after any
     previous stripe. */
  bbox_store(&imageobj->imsbbox,
             (imagedata->optimise & IMAGE_OPTIMISE_SWAP) ? imagedata->image_lines : 0,
             (imagedata->optimise & IMAGE_OPTIMISE_SWAP) ? 0 : imagedata->image_lines,
             imagedata->rwidth - 1, imagedata->rheight - 1) ;

  imageobj->sixteenbit_output = ( uint8 )imagedata->out16;

  /* Set initial checksum which helps in recombining pre-sepd image[mask]s */
  if ( imagetype == TypeImageImage ||
       imagetype == TypeImageMaskedImage ||
       imagetype == TypeImageAlphaImage )
    theINPlanes( imageobj ) = ( presep ? 1 : incomps );
  else
    theIAdler32( imageobj ) = 0;

  imageobj->optimize = ( uint8 )imagedata->optimise;

  if (presep)
    imageobj->flags |= IM_FLAG_PRESEP;

  imageobj->ims = NULL;
  imageobj->ime = NULL;

  /* Decode arrays used for image adjustment. */
  imageobj->cv_fdecodes = NULL;
  imageobj->cv_ndecodes = NULL;

  /* Recombine info. */
  if (pMatrix) {
    HQASSERT(matrix_assert( pMatrix ),"bad image matrix (pMatrix)");
    /* save matrix for possible use in recombine */
    imageobj->rcb.omatrix = *pMatrix;
  }

  imageobj->rcb.nfillm = NULL;

  /* Do this before other image object allocations, because
     im_createimageobj() is called inside a low-memory recovery loop. We want
     to allocate tiles for performance: it's strictly not necessary, so we
     won't throw an error, but if something else causes a low-memory
     recovery, we'd like to make sure the rendering performs well. */
  if ( imagedata->optimise & IMAGE_OPTIMISE_ROTATED )
    imageobj->tiles = im_generatetiles(imagedata->page, &imagedata->geometry);

  /* Can't preconvert images that have an interleaved lut (see
     im_adj_adjustdata).  However, if the image is has some transparency then
     the whole image will be backdrop rendered and an interleaved can be
     allowed. */
  allowInterleavedLuts =
    (!guc_backdropRasterStyle(hRasterStyle) ||
     !tsOpaque(gsTranState(gstateptr),
               (colorType == GSC_STROKE ? TsStroke : TsNonStroke),
               imagedata->colorInfo));

  /* Now open/setup the expansion code which involves creating an expansion
     buffer and (optionally) look-up tables (LUTs) for color conversion. The
     case for image masks (but not AlphaMasks) is simpler, and so handled
     separately. */
  if ( imagetype == TypeImageMask || imagetype == TypeImageMaskedMask ) {
    imageobj->ime = im_expandopenimask(imagedata->page,
                                       imagedata->rwidth, imagedata->rheight,
                                       imageargs->bits_per_comp,
                                       imageargs->polarity);
  } else {
    int32 plane = presep ? imagedata->plane : IME_ALLPLANES; /*iff recombine*/
    Bool independent, interleaved = (gucr_interleavingStyle(hRasterStyle) ==
                                     GUCR_INTERLEAVINGSTYLE_PIXEL);

    if ( !gsc_hasIndependentChannels(imagedata->colorInfo, colorType, TRUE,
                                     &independent) )
      return FALSE;

    imageobj->ime = im_expandopenimage(imagedata->page,
                                       imagedata->u.fdecodes,
                                       imagedata->ndecodes,
                                       imageargs->decode, NULL,
                                       incomps, independent,
                                       imagedata->rwidth, imagedata->rheight,
                                       incomps, imageargs->bits_per_comp,
                                       FALSE, /*fixedpt16*/
                                       imagedata->oncomps, imagedata->out16,
                                       interleaved, /*interleaved output*/
                                       plane,
                                       imagedata->colorInfo, colorType,
                                       imagetype, imagedata->method,
                                       imageargs->isSoftMask,
                                       TRUE, /*allowLuts*/
                                       allowInterleavedLuts,
                                       ime_colorconverted);
  }
  if ( imageobj->ime == NULL )
    return FALSE;

  /* If the image is now in device space, no image adjustment is required to
     direct render it, but it will need quantising to device codes. */
  if ( (imagetype == TypeImageImage ||
        imagetype == TypeImageMaskedImage ||
        imagetype == TypeImageAlphaImage) &&
       !guc_backdropRasterStyle(hRasterStyle) &&
       tsOpaque(gsTranState(gstateptr),
                (colorType == GSC_STROKE ? TsStroke : TsNonStroke),
                imagedata->colorInfo) ) {
    /* If image and output are both 8 bit contone, quantisation is a no-op. */
    if ( imagedata->out16 || gucr_valuesPerComponent(hRasterStyle) != 256 ||
         gucr_halftoning(hRasterStyle) ) {
      if ( !im_expand_setup_on_the_fly(imageobj, imagedata->page, GSC_QUANTISE_ONLY,
                                       gstateptr->colorInfo, colorType) )
        return FALSE;
    }
  }

  if (!im_alloc_late_imagedata(imageargs, imagedata, imageobj->ime ))
    return FALSE;

  /* Now to open the image store.  First, prepare the output bpp and number
    of planes. */
  {
    int32 planes;
    int32 bpp;
    int32 oncomps;

    oncomps = imagedata->oncomps;

    if ( imagedata->expandformat == ime_planar ) {
      planes = ( presep ? imagedata->plane + 1 : incomps );
      bpp = imageargs->bits_per_comp;
      if ( bpp == 12 )  /* Must "pack" 12 into 16 bit containers. */
        bpp = 16;
    }
    else if ( imagedata->expandformat == ime_colorconverted ) {
      planes = imagedata->oncomps;
      bpp = ( imagedata->out16 ? 16 : 8 );
    }
    else { /* ime_interleaved2/4/8/16 ? */
      planes = 1;
      bpp = imagedata->expandformat;

      /* Special debug case for processing 32bit input image data */
      if ( imageargs->bits_per_comp == 32 &&
           imagedata->expandformat == ime_as_is )
        bpp = 32;
    }
    if ( imagedata->optimise & IMAGE_OPTIMISE_XFLIP )
      imflags |= IMS_XFLIP;
    if ( imagedata->optimise & IMAGE_OPTIMISE_YFLIP )
      imflags |= IMS_YFLIP;
    if ( imagedata->optimise & IMAGE_OPTIMISE_SWAP )
      imflags |= IMS_XYSWAP;
    if ( !im_stripe_allowed(get_core_context_interp(), imagetype) )
      imflags |= IMS_DESPERATE;
    if ( imageargs->downsample.enabled )
      imflags |= IMS_DOWNSAMPLED;
    if ( imageargs->downsample.rowrepeats_near )
      imflags |= IMS_ROWREPEATS_NEAR;
    if ( imageargs->downsample.rowrepeats_2rows )
      imflags |= IMS_ROWREPEATS_2ROWS;

    imageobj->ims = im_storeopen(imagedata->page->im_shared,
                                 &imageobj->imsbbox, planes, bpp, imflags);
    if ( imageobj->ims == NULL )
      return FALSE;
  }

  return TRUE;
}

/** This fn contains all stuff from im_createimageobj that can either only
 * be executed once (ie must be outside fail/retry low-memory loop around
 * im_createobj).
 */
static Bool im_finishimageobj( IMAGEARGS *imageargs,
                               IMAGEDATA *imagedata,
                               IMAGEOBJECT *imageobj )
{
  im_transform_t *geometry = &imageobj->geometry ;

  HQASSERT(VALID_IMAGETYPE(imageargs->imagetype), "bad imagetype");

  *geometry = imagedata->geometry ;

  /* Initialise the device bbox to the intersection of the front-end clip
     and the image extent. We'll shrink it to the stripe bounds after
     working out the stripe device coordinates. */
  bbox_intersection(&cclip_bbox, &geometry->extent, &imageobj->bbox) ;

  /* Shrink the image space bounding box to the device clip box. This has to
     be done after quantising the matrix and initialising both bboxes. Ignore
     failure, we don't take care of degeneracy until later. */
  (void)image_ibbox_covering_dbbox(imageobj, &imageobj->bbox,
                                   &imageobj->imsbbox) ;

  if ( imagedata->presep ) {
    if ( !im_build_nfill_outline(imagedata->page, imageargs, &imageobj->rcb.nfillm) )
      return FALSE;
  }

  return TRUE;
}

/**
   Given a single buffer of data where image and mask data is sample
   interleaved (in the image's input buffer), this routine splits the
   data so that the image and mask data are returned in separate buffers.
   Note that sample interleaved data can only occur when all data is coming
   in through a single Datasource - hence the direct use of tbuf[0] & obuf[0].
*/
static void im_splitscanline( IMAGEARGS *imageargs,
                              IM_BUFFER *imagebuf,
                              IMAGEARGS *imaskargs,
                              IM_BUFFER *imaskbuf )
{
  int32 mbits, ibits;

  UNUSED_PARAM( IMAGEARGS *, imaskargs );

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagebuf, "image buffer NULL");
  HQASSERT(imaskargs, "imaskargs NULL");
  HQASSERT(imaskbuf, "imask buffer NULL");

  HQASSERT(imageargs->interleave == INTERLEAVE_PER_SAMPLE,
           "im_splitscanline: bad interleave" );
  HQASSERT(imageargs->imagetype == TypeImageMaskedImage ||
           imageargs->imagetype == TypeImageAlphaImage,
           "Expected masked image or alpha image types only");

  /* The tbuf[] buffer for the mask will have been allocated but not yet
     used. */
  imaskbuf->obuf[0] = imaskbuf->tbuf[0];

  /* When image & mask data is sample interleaved, the bit-depth for each
     component including the mask "component" is the same. */
  mbits = imageargs->bits_per_comp;    /* bits in each sample of mask data */
  ibits = mbits * imageargs->ncomps; /* & then in each sample of image data */

  if ( imageargs->imagetype == TypeImageAlphaImage ) {
    /* TIFF or PNG images with an alpha channel. */
    alpha_split_samples(TRUE,  /* alpha_last is true (ie RGBA,RGBA,...) */
        imageargs->premult_alpha,   /* color values premultiplied by alpha */
        imagebuf->obuf[0],        /* interleaved input data     */
        imageargs->width, /* number of samples to split */
        imaskbuf->obuf[0],        /* returned pure mask data    */
        mbits,                      /* num. bits per mask sample  */
        imagebuf->tbuf[0],        /* returned pure image data   */
        ibits );                     /* num. bits per image sample */
  } else {
    /* PSL3 Masked Images. */
    imask_split_samples(imagebuf->obuf[0],  /* interleaved input data     */
        imageargs->width, /* number of samples to split */
        imaskbuf->obuf[0],        /* returned pure mask data    */
        mbits,                      /* num. bits per mask sample  */
        imagebuf->tbuf[0],        /* returned pure image data   */
        ibits  );                    /* num. bits per image sample */
  }

  imagebuf->obuf[0] = imagebuf->tbuf[0];
}

typedef int32 (*LINEFUNC)(IMAGEARGS *, IM_BUFFER *, int32, int32, Bool);

#define DOWN_FUNCTION down_1101
#define DOWN_NCOMPS 1
#define DOWN_DOWNX 1
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"
#define DOWN_FUNCTION down_3101
#define DOWN_NCOMPS 3
#define DOWN_DOWNX 1
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"
#define DOWN_FUNCTION down_4101
#define DOWN_NCOMPS 4
#define DOWN_DOWNX 1
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"
#define DOWN_FUNCTION down_1201
#define DOWN_NCOMPS 1
#define DOWN_DOWNX 2
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"
#define DOWN_FUNCTION down_3201
#define DOWN_NCOMPS 3
#define DOWN_DOWNX 2
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"
#define DOWN_FUNCTION down_4201
#define DOWN_NCOMPS 4
#define DOWN_DOWNX 2
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"
#define DOWN_FUNCTION down_1401
#define DOWN_NCOMPS 1
#define DOWN_DOWNX 4
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"
#define DOWN_FUNCTION down_3401
#define DOWN_NCOMPS 3
#define DOWN_DOWNX 4
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"
#define DOWN_FUNCTION down_4401
#define DOWN_NCOMPS 4
#define DOWN_DOWNX 4
#define DOWN_METHOD 0
#define DOWN_NPROCS 1
#include "downfunc.h"

#define DOWN_FUNCTION downsample_line
#define DOWN_NPROCS nprocs
#define DOWN_NCOMPS ncomps
#define DOWN_DOWNX downx
#define DOWN_METHOD method
#include "downfunc.h"

LINEFUNC choose_linefunc(IMAGEARGS *imageargs) {
  int32 ncomps = imageargs->ncomps;
  int32 x  = imageargs->downsample.x;
  int32 m = imageargs->downsample.method;
  int32 nprocs = imageargs->nprocs;

  if ( ncomps == 1 && x == 1 && m == 0 && nprocs == 1 ) return down_1101;
  if ( ncomps == 3 && x == 1 && m == 0 && nprocs == 1 ) return down_3101;
  if ( ncomps == 4 && x == 1 && m == 0 && nprocs == 1 ) return down_4101;
  if ( ncomps == 1 && x == 2 && m == 0 && nprocs == 1 ) return down_1201;
  if ( ncomps == 3 && x == 2 && m == 0 && nprocs == 1 ) return down_3201;
  if ( ncomps == 4 && x == 2 && m == 0 && nprocs == 1 ) return down_4201;
  if ( ncomps == 1 && x == 4 && m == 0 && nprocs == 1 ) return down_1401;
  if ( ncomps == 3 && x == 4 && m == 0 && nprocs == 1 ) return down_3401;
  if ( ncomps == 4 && x == 4 && m == 0 && nprocs == 1 ) return down_4401;
  return downsample_line;
}


/**
  This routine is expected to return an integral number of scan-lines of image
  (and/or mask) data, up to 'maxlines' (it may return less).
*/
static int32 do_im_getscanlines( IMAGEARGS *imageargs,
                                 IMAGEDATA *imagedata )
{
  int32 lines, rembytes, maxbytes, nlines = 1, lines_to_drop = 0;
  int32 nprocs, bytes_in_input_line;
  int32 maxlines, y0 = 0, downbytes;
  LINEFUNC down = choose_linefunc(imageargs);
  IM_BUFFER *imb;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");

  imb = imagedata->imb;
  HQASSERT(imb, "somehow lost image buffer");

  maxlines = imagedata->lines_left_in_block;
  if ( imageargs->downsample.y > 1 ) {
    int32 tail_lines;

    nlines = imageargs->downsample.y;
    maxlines *= nlines;
    y0 = nlines * (imageargs->height - imagedata->lines_left_in_block);
    tail_lines = imageargs->downsample.h0 - imageargs->height * nlines ;
    if ( maxlines == nlines && tail_lines > 0 ) {
      /* We have just one scanline of data to get, but there are trailing
       * lines we need to throw away because of downsampled height being
       * rounded down.
       */
      lines_to_drop = tail_lines;
    }
    maxlines += tail_lines;
  }
  HQASSERT(maxlines > 0, "maxlines should be > 0");

  nprocs = imageargs->nprocs;
  HQASSERT(nprocs > 0, "nprocs should be > 0");

  downbytes = bytes_in_input_line = imagedata->bytes_in_input_line;
  if ( imageargs->downsample.x > 1 )
    bytes_in_input_line = pre_downsampled_bytes(imageargs);
  HQASSERT(bytes_in_input_line > 0, "bytes_in_input_line should be > 0");

  /* Start by assuming we could read all the scanlines in one go. Unless, of
     course, we will overflow the size of an int32. */
  if ( MAXINT32 / bytes_in_input_line < maxlines )
    maxlines = MAXINT32 / bytes_in_input_line;

  rembytes = maxlines * bytes_in_input_line;
  lines = 0;

  do {
    maxbytes = rembytes ;
    for (;;) {
      int32 i, len = imb->ibytes;

      /* Get data from each Datasource "in parallel" via the calls to
         get_image_string().  Note that it may return the full quota's
         worth of data in one go.  It returns a pointer to the input
         Datasource's buffer via the ibuf[] array. */

      if ( len == 0 ) {
        int32 toget = maxbytes;
        for ( i = 0; i < nprocs; ++i ) {
          if ( !get_image_string(&imageargs->data_src[i], toget,
                                 &imb->ibuf[i], &len) )
            return -1;
          if ( i == 0 )
            imb->ibytes = toget = len;
          else if ( len != imb->ibytes ) {
            /* Multiproc images must get same number of bytes */
            ( void )error_handler( RANGECHECK );
            return -1;
          }
        }
        if ( len == 0 ) { /* No more data, no lines obtained. */
          nlines = -lines_to_drop;
          break;
        }
        rembytes -= len ;
      }

      if ( nlines <= 0 ) {
        /* Just getting the data in order to throw it away as we are on the
         * scanlines which are off the end of the rounded-down downsampled
         * height. Count the number of whole lines including bytes carried
         * over from the previous get_image_string().
         */
        int32 discard = (len + imb->tbytes) / bytes_in_input_line;
        len = discard * bytes_in_input_line;
        imb->tbytes = imb->ibytes + imb->tbytes - len ;
        imb->ibytes = 0 ;
        if ( discard > 0 ) {
          /* Compensate for outer loop condition decrementing nlines. */
          nlines -= discard - 1 ;
          break ;
        }
        continue;
      }

      if ( imb->tbytes == 0 && len >= bytes_in_input_line ) {
        /* Return as we have a contiguous block of at least one scanline. */

        if ( imageargs->downsample.enabled ) {
          /* If we are downsampling, do the down-sampling into the temp
           * buffer (tbuf). There is only space for a single scanline here,
           * so just consume a scanline worth of final image data, which
           * wil be N lines of original input data.
           */
          imb->ibytes -= bytes_in_input_line;
          lines += (*down)(imageargs, imb, y0, downbytes, FALSE);
          for ( i = 0; i < nprocs; ++i )
            imb->ibuf[i] += bytes_in_input_line;
          y0++;
          break;
        } else {
          lines = len / bytes_in_input_line;   /* Number of *whole* lines */
          if ( lines > maxlines )
            lines = maxlines;
          len = lines * bytes_in_input_line;

          for ( i = 0; i < nprocs; ++i ) {
            imb->obuf[i] = imb->ibuf[i];
            imb->ibuf[i] += len;
          }
          imb->ibytes -= len;   /* Remaining bytes after whole num lines */
          break;
        }
      }

      /* Here, not yet got a whole line's worth of data.  For each Datasource,
         copy the data so far obtained into a temporary buffer (tbuf[]) where it
         can be collated until one whole line's worth of data has been obtained.
         In this case, set obuf[] to point to tbuf[]'s buffers instead of ibuf[].
      */
      len = bytes_in_input_line - imb->tbytes;
      if ( len > imb->ibytes )
        len = imb->ibytes;

      for ( i = 0; i < nprocs; ++i ) {
        HQASSERT(imb->tbuf[i],"Writing into tmp buf but not been allocated");
        HqMemCpy( imb->tbuf[i] + imb->tbytes, imb->ibuf[i], len );
        imb->ibuf[i] += len;
      }
      imb->ibytes -= len;
      imb->tbytes += len;

      /* The tbuf[] temporary buffers are only one input line long, so limit
         the max bytes we will read in the next input call to this size. */
      maxbytes = bytes_in_input_line - imb->tbytes;
      if ( maxbytes == 0 ) {
        /* Data accumulated in the temp buffer amounts to a single scanlines
         * worth, so return it.
         */
        imb->tbytes = 0;
        if ( imageargs->downsample.enabled ) {
          lines += (*down)(imageargs, imb, y0, downbytes, TRUE);
          y0++;
          break;
        } else {
          for ( i = 0; i < nprocs; ++i )
            imb->obuf[i] = imb->tbuf[i];
          lines = 1;
          break;
        }
      }
    }
  } while ( --nlines + lines_to_drop > 0 );

  return lines;
}

static int32 im_getscanlines( IMAGEARGS *imageargs,
                              IMAGEDATA *imagedata )
{
  int32 lines = do_im_getscanlines(imageargs, imagedata);

#if 0 /* print all the image data returned by im_getscanlines */
  int32 i, j;

  printf("im_getscanlines: %d\n", lines);
  for ( i = 0; i < imageargs->nprocs; i++ ) {
    uint8 *ptr = imagedata->imb->obuf[i];
    printf("chan[%d]=", i);
    for ( j = 0; j < imagedata->bytes_in_input_line; j++ ) {
      printf("%02x", ptr[j]);
    }
    printf("\n");
  }
#endif
  return lines;
}

Bool im_convertlines(IMAGEARGS *imageargs,
                     IMAGEDATA *imagedata,
                     int32 lines,
                     Bool (*linefn)(IMAGEARGS *imageargs,
                                    IMAGEDATA *imagedata,
                                    uint8 **tbuf,
                                    int32 ncomps,
                                    void *data),
                     void *data)
{
  int32 i, nprocs, image_lines, tconv, bytes_in_image_line, bytes_in_lines;
  IM_BUFFER *imb;

  HQASSERT(imageargs, "No image args");
  HQASSERT(imagedata, "No image data");

  imb = imagedata->imb;
  HQASSERT(imb, "No image buffer");

  nprocs = imageargs->nprocs;
  tconv = imageargs->width;
  image_lines = imagedata->image_lines;
  bytes_in_image_line = imagedata->bytes_in_image_line;
  bytes_in_lines = bytes_in_image_line * lines;

  while ( --lines >= 0 ) {
    if ( imagedata->state_line != imagedata->image_lines ) {
      imagedata->state = IMD_raw;
      imagedata->ubuf = NULL;
      imagedata->tbuf = NULL;
    }

    if ( ( imagedata->pd_use & PD_UNPACK ) != 0 &&
         imagedata->state < IMD_unpacked ) {
      HQASSERT(imagedata->pd, "Pack data NULL");
      /* Unpack into int32, interleaved. */
      pd_unpack( imagedata->pd,
                 imb->obuf, imageargs->ncomps,
                 &imagedata->ubuf, tconv );
      imagedata->state = IMD_unpacked;
    }

    if ( imagedata->expandformat == ime_colorconverted ||
         imagedata->expandformat == ime_as_is_decoded) {
      /* Not using lut, data gets planarized as part of color conversion. */
      if ( imagedata->state < IMD_converted ) {
        if ( ! im_colcvt(imagedata->imc, imagedata->ubuf, &imagedata->tbuf) )
          return FALSE;
        imagedata->state = IMD_converted;
      }
      if ( ! (*linefn)(imageargs, imagedata, &imagedata->tbuf, 0, data) )
        return FALSE;
    } else if  ( (imagedata->pd_use & PD_PACK) != 0 ) {
      HQASSERT(imagedata->ubuf != NULL, "unpacked buffer null");
      if ( imagedata->state < IMD_converted ) {
        if ( ( imagedata->pd_use & PD_PLANAR ) != 0 ) {
          /* Need to planarize for the image store. */
          HQASSERT(imagedata->expandformat == ime_planar ||
                    imagedata->expandformat == ime_as_is,
                    "PD_PLANAR set but not expecting planar");
          pd_planarize( imagedata->pd, imagedata->ubuf, & imagedata->ubuf );
        }

        pd_pack(imagedata->pd, imagedata->ubuf, &imagedata->tbuf, tconv);

        imagedata->state = IMD_converted;
      }

      if ( ! (*linefn)(imageargs, imagedata, &imagedata->tbuf, 0, data) )
        return FALSE;
    } else {
      if ( ! (*linefn)(imageargs, imagedata, imb->obuf, nprocs, data) )
        return FALSE;
    }
    imagedata->state_line = imagedata->image_lines++;

    for ( i = 0; i < nprocs; ++i )
      imb->obuf[i] += bytes_in_image_line;
  }

  /* Reset input buffer pointers for next call */
  for ( i = 0; i < nprocs; ++i )
    imb->obuf[i] -= bytes_in_lines;

  imagedata->image_lines = image_lines;

  return TRUE;
}

/** Image storing function. nprocs indicates if the data is planar; if not,
   it is interleaved and can be written to the image store, pdfout, or HDLT
   in one unit. */
static Bool dl_imagedatafn(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                           uint8 **tbuf, int32 nprocs, void *data)
{
  dl_image_state *state = data;
  IM_BUFFER *imb;
  IMAGEOBJECT *imageobj;
  int32 i;

  HQASSERT(imageargs, "No image args");
  HQASSERT(imagedata, "No image data");
  HQASSERT(state, "No callback data");

  imb = imagedata->imb;
  HQASSERT(imb, "somehow lost image buffer");

  imageobj = state->imageobj;
  HQASSERT(imageobj, "somehow lost image object");

  if ( state->is_mask_data )
    imageobj = imageobj->mask;

  /* If recombing and this is an imagemask then accumulate a checksum
   * to assist with the recombination
   */
  if ( imagedata->presep &&
       imageargs->imagetype == TypeImageMask ) {
    uint16 s1 = (uint16)theIAdler32(imageobj);
    uint16 s2 = (uint16)(theIAdler32(imageobj) >> 16);
    /* Update checksum and seed values (imagemasks only have 1 component). */
    theIAdler32(imageobj) = calculateAdler32(imb->obuf[0],
                                             imagedata->bytes_in_image_line,
                                             &s1, &s2);
  }

  for ( i = 0; i < (nprocs == 0 ? 1 : nprocs); ++i ) {
    int32 planei;

    if ( nprocs == 0 && (!imagedata->presep) )
      planei = IMS_ALLPLANES;
    else
      planei = imagedata->plane + i;

    if ( !im_storewrite(imageobj->ims, 0, imagedata->image_lines,
                        planei, tbuf[i], imageargs->width) )
      return FALSE;
  }


  if (pdfout_enabled()) {
    /* If we are dealing with image or mask data, PDFout needs this hook.  */
    if (! pdfout_imagedata(get_core_context()->pdfout_h, imageargs->nprocs,
                           imb->obuf,
                           imagedata->bytes_in_image_line * imageargs->nprocs,
                           state->is_mask_data))
      return FALSE;
  }

  if ( ! IDLOM_IMAGEDATA(imageargs->colorType, imageargs,
                         imageargs->nprocs,
                         imb->obuf, imagedata->bytes_in_image_line,
                         imagedata->oncomps, tbuf[0],
                         imageargs->width * imagedata->oncomps *
                         (imagedata->out16 ? 2 : 1),
                         imagedata->out16))
    return FALSE; /* PS or I/O error in IDLOM callbacks */

  return TRUE;
}

static void im_genchromamask( IMAGEARGS *imageargs,
                              IMAGEDATA *imagedata,
                              IM_BUFFER *imaskbuf )
{
  IM_BUFFER *imagebuf;

  HQASSERT(imageargs, "No image args");
  HQASSERT(imagedata, "No image data");
  HQASSERT(imaskbuf, "No imask buffer");

  imagebuf = imagedata->imb;
  HQASSERT(imagebuf, "No image buffer");

  HQASSERT(imagedata->state_line != imagedata->image_lines,
           "Image data already unpacked");

  /* There is no point either checking or changing the imagedata->state
     here, as this function just deals with one line at a time, and is
     called before we do the conversions in im_convertlines.  The
     imagedata->state is reset anyhow in im_convertlines and then set up
     appropriately in there.  The imaskbuf is expected to be OK at this
     point.
  */

  if ( ( imagedata->pd_use & PD_UNPACK ) != 0 ) {

    HQASSERT(imagedata->pd, "No pack data");

    /* Unpack into int32, interleaved. */
    pd_unpack(imagedata->pd,
              imagebuf->obuf, imageargs->ncomps,
              &imagedata->ubuf, imageargs->width);

    HQASSERT(imageargs->maskgen, "No mask generation");

    /* Generate mask for chroma-keyed image */
    im_genmask(imageargs->maskgen, imagedata->ubuf, imaskbuf->tbuf[0]);
    imaskbuf->obuf[0] = imaskbuf->tbuf[0];
  }
}

static Bool im_getimageblock(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                             IMAGEARGS *imaskargs, IMAGEDATA *imaskdata,
                             int32 *lines, ImageLineType *read_type)
{
  IM_BUFFER *imagebuf;

  HQASSERT(imageargs, "No image args");
  HQASSERT(imagedata, "No image data");

  imagebuf = imagedata->imb;
  HQASSERT(imagebuf, "No image buffer");

  if ( imageargs->interleave == INTERLEAVE_SCANLINE ||
       imageargs->interleave == INTERLEAVE_SEPARATE ) {
    HQASSERT(imaskargs, "No image mask args");
    HQASSERT(imaskdata, "No image mask data");

    if ( imaskdata->lines_left_in_block > 0 ) {
      *lines = im_getscanlines(imaskargs, imaskdata);
      if ( *lines > 0 ) {
        imaskdata->lines_left_in_block -= *lines;
        *read_type = MaskLines;
      } else if ( *lines == 0 ) {
        *read_type = NoLines;
      } else
        return FALSE;

      return TRUE;
    }
  }

  *lines = im_getscanlines(imageargs, imagedata);

  if ( *lines > 0 ) {
    imagedata->lines_left_in_block -= *lines;

    if ( imageargs->interleave == INTERLEAVE_PER_SAMPLE ) {
      HQASSERT(imaskargs, "No image mask args");
      HQASSERT(*lines == 1, "More than one line read for interleaved image");

      im_splitscanline(imageargs, imagebuf, imaskargs, imaskdata->imb);

      *read_type = MaskImageLines;
    } else if ( imageargs->interleave == INTERLEAVE_CHROMA ) {
      HQASSERT(imaskargs, "No image mask args");
      HQASSERT(*lines == 1, "More than one line read for interleaved image");

      im_genchromamask(imageargs, imagedata, imaskdata->imb);

      *read_type = MaskImageLines;
    } else {
      *read_type = ImageLines;
      if ( imagedata->all_components_same ) {
        /* Test that all components are the same for all samples. */
        int32 bytes_per_component = imageargs->bits_per_comp / 8 ;
        int32 ncomps = imageargs->ncomps ;
        int32 samples = *lines * imagedata->bytes_in_image_line /
          (bytes_per_component * ncomps) ;
        HQASSERT(samples > 0, "No samples on image line") ;
        if ( bytes_per_component == 1 ) {
          const uint8 *data = imagedata->imb->obuf[0] ;
          switch ( ncomps ) {
          case 3:
            do {
              if ( data[0] != data[1] || data[0] != data[2] )
                goto done ;
              data += ncomps ;
            } while ( --samples > 0 ) ;
            break ;
          case 4:
            do {
              if ( data[0] != data[1] || data[0] != data[2] ||
                   data[0] != data[3] )
                goto done ;
              data += ncomps ;
            } while ( --samples > 0 ) ;
            break ;
          default:
            do {
              int32 i ;
              for ( i = 1 ; i < ncomps ; ++i ) {
                if ( data[0] != data[i] )
                  goto done ;
              }
              data += ncomps ;
            } while ( --samples > 0 ) ;
            break ;
          }
        } else {
          const uint16 *data = (const uint16 *)imagedata->imb->obuf[0] ;
          HQASSERT(bytes_per_component == 2, "Unexpected bytes per component") ;
          switch ( ncomps ) {
          case 3:
            do {
              if ( data[0] != data[1] || data[0] != data[2] )
                goto done ;
              data += ncomps ;
            } while ( --samples > 0 ) ;
            break ;
          case 4:
            do {
              if ( data[0] != data[1] || data[0] != data[2] || data[0] != data[3] )
                goto done ;
              data += ncomps ;
            } while ( --samples > 0 ) ;
            break ;
          default:
            do {
              int32 i ;
              for ( i = 1 ; i < ncomps ; ++i ) {
                if ( data[0] != data[i] )
                  goto done ;
              }
              data += ncomps ;
            } while ( --samples > 0 ) ;
            break ;
          }
        }
      done:
        imagedata->all_components_same = (samples == 0) ;
      }
    }

    /* If finished both mask and image data, reset for next block */
    if ( imagedata->lines_left_in_block == 0 ) {
      imagedata->lines_left_in_block = imageargs->lines_per_block;
      if ( imaskdata && imaskargs )
        imaskdata->lines_left_in_block = imaskargs->lines_per_block;
    }
  } else if ( *lines == 0 ) {
    *read_type = NoLines;
  } else
    return FALSE;

  return TRUE;
}

static Bool im_getfilteredblock(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                IMAGEARGS *imaskargs, IMAGEDATA *imaskdata,
                                int32 *lines, ImageLineType *read_type)
{
  uint8 *data;
  uint32 length, imageOrMask;
  IM_BUFFER *imagebuf, *imaskbuf;

  UNUSED_PARAM(IMAGEARGS *, imaskargs);

  HQASSERT(imageargs, "No image args");
  HQASSERT(imagedata, "No image data");

  imagebuf = imagedata->imb;
  HQASSERT(imagebuf, "No image buffer");

  if ( !filteredImageGetString(imageargs->filteredImage,
                               &data, &length, &imageOrMask ) )
    return FALSE;

  if ( length > 0 ) {
    *lines = 1;
    if ( imageOrMask == ImageDataSelector ) {
      imagebuf->obuf[0] = data;
      *read_type = ImageLines;
    } else {
      HQASSERT(imaskdata, "No image mask buffer");

      imaskbuf = imaskdata->imb;
      HQASSERT(imaskbuf, "No image mask buffer");

      HQASSERT(imageOrMask == MaskDataSelector, "Not an image or a mask line");
      imaskbuf->obuf[0] = data;
      *read_type = MaskLines;
    }
  } else /* premature end of data */
    *read_type = NoLines;

  return TRUE;
}

static Bool im_expandaddalpha(DL_STATE *page, IMAGEOBJECT* imageobj)
{
  HQASSERT(imageobj->mask, "image must have a mask image object");
  HQASSERT(bbox_equal(im_storebbox_original(imageobj->ims),
                      im_storebbox_original(imageobj->mask->ims)),
           "A type 12 image requires image stores to have equal dimensions");

  /* Indicate that the alpha channel requires compositing. */
  imageobj->flags |= IM_FLAG_COMPOSITE_ALPHA_CHANNEL;

  /* Update the image expander to store a reference to the alpha store. */
  if ( !im_expandalphachannel(imageobj->ime, imageobj->mask->ims) )
    return FALSE;

  /* Null the reference to the alpha image store (so it won't get freed below).
     The image expander now refers to the alpha image store instead. */
  imageobj->mask->ims = NULL;

  /* Free the alpha channel object (but not the alpha image store). */
  im_freeobject(imageobj->mask, page);
  imageobj->mask = NULL;

  return TRUE;
}

/*
  In low memory, im_common can attempt to process a large image as a of smaller
  sub-images (stripes). Each stripe is rendered as separate image, and partial
  painting can be attempted between the production of each stripe.
  The use of stripes is hidden from external interfaces like HDLT, and
  StartImage / EndImage hooks. So for a particular input image, the existence
  of a low memory will not affect how the input image is presented via those
  interfaces.

  Each stripe will share the same imagedata structure. But as each stripe
  is a separate image, upon starting the stripe some fields of the imagedata
  will have to be reset.

  The striping mechanism also provides a means to avoid color converting data
  that will be clipped out. If the image is not completely contained within the
  page's clip region, treat the clipped in and clipped out data as if they are
  in different stripes. The clipped out stripe is not added to the DL, and its
  data can be disposed of. Data can only be optimised per whole scan line and
  is sensitive to the orientation of the image. We can only discard a entire
  scan line of input, not partial scan lines.

  There are two mechanisms for optimizing clipped images.
  One discards scan lines that are completely clipped out, if the rip
  configuration allows it. This is basically a striping of the image data
  as detailed above.

  Where a scan line is partially clipped out, color conversion can be avoided
  for the clipped out portion.
*/

Bool im_common(DL_STATE *page, STACK *stack, IMAGEARGS *imageargs, void *data,
               Bool (*im_begin)(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                void *data),
               Bool (*im_end)(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                              void *data, Bool abort),
               Bool (*im_data)(int32 data_type,
                               IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                               uint32 lines, void *data),
               void (*im_early_end)(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                               void *data),
               void (*im_stripe)(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                               void *data),
               void (*im_clip_lines)(IMAGEARGS *imageargs,
                                     IMAGEDATA *imagedata,
                                     void *data, int32 *iy1, int32 *iy2,
                                     int32 *my1, int32 *my2))
{
  Bool result = TRUE;
  IMAGEARGS *imaskargs = NULL;
  corecontext_t *corecontext = get_core_context_interp() ;
  error_context_t *errcontext = corecontext->error ;
  static mm_pool_t temp_pools[4];

  /* each stripe (if used) treated at im_common's original dl_safe level.*/
  int initial_dl_safe_recursion = dl_safe_recursion;
  int8 hdltState;

  IMAGEDATA imagedata = { 0 };
  IMAGEDATA imaskdata = { 0 };

  IM_BUFFER imagebuf  = { 0 };
  IM_BUFFER imaskbuf  = { 0 };

  int32 imagetype, colorType;
  Bool stripe_allowed;

  /* early_clip_allowed determines if completely clipped out scan lines can be
   * discarded without processing.
   */
  Bool early_clip_allowed;

  HQASSERT(stack, "No image args stack");
  HQASSERT(imageargs, "No image args");

  imagetype = imageargs->imagetype;
  HQASSERT(VALID_IMAGETYPE( imagetype ), "bad image type");

  colorType = imageargs->colorType;
  HQASSERT(colorType == GSC_FILL || colorType == GSC_IMAGE ||
           colorType == GSC_SHFILL, "Strange colorType for image");

  stripe_allowed = im_stripe_allowed(corecontext, imagetype);
  early_clip_allowed = im_early_clip_allowed(corecontext, imagetype,imageargs);

#define return DO_NOT_RETURN_SET_result_INSTEAD!
  /* Images containing an alpha channel cannot be recombined without more work
   * in the recombine adjust code. It is simpler to disable recombine for such
   * images since a preseparated version doesn't make much sense and not
   * likely to see one.
   */
  if ( imagetype == TypeImageAlphaImage )
    rcbn_disable_interception(gstateptr->colorInfo);

  hdltState = theIdlomState(*gstateptr);

  imagedata.context = corecontext;
  imagedata.page = page;
  imagedata.colorInfo = gstateptr->colorInfo;

  /**
   * \todo BMJ 19-Sep-08 :  Stop using temp pool for this
   * \todo ajcd 2013-06-19: Maybe not. The packdata structure has to survive
   * a partial paint, so that the data not processed by an image stripe will
   * be accessible for the next stripe.
   */
  temp_pools[0] = temp_pools[1] = temp_pools[2] = temp_pools[3] = mm_pool_temp;
  imagedata.pools = temp_pools;
  imagedata.imb = & imagebuf;
  if ( !im_prepareimageoutput(imageargs, &imagedata, FALSE, FALSE) ||
       !im_prepareimagedata(imageargs, &imagedata, FALSE) ) {
    result = FALSE;
  } else {
    imagedata.sharedIMB = TRUE;

    if ( imagetype == TypeImageAlphaImage ||
         imagetype == TypeImageMaskedImage )  {
      imaskargs = imageargs->maskargs;
      HQASSERT(imaskargs != NULL, "no masked image args");

      imagedata.imaskdata = & imaskdata;
      imaskdata.context = corecontext;
      imaskdata.page = page;
      imaskdata.colorInfo = gstateptr->colorInfo;
      imaskdata.pools = temp_pools;
      imaskdata.imb = & imaskbuf;
      /* Need to use same optimisation for the mask as the image used. */
      imaskdata.optimise = imagedata.optimise ;

      if ( !im_prepareimageoutput(imaskargs, &imaskdata, FALSE, FALSE) ||
           !im_prepareimagedata(imaskargs, &imaskdata, FALSE) ) {
        result = FALSE;
      } else {
        /* N.B. Image is degenerate if image OR mask has a zero dimension */
        imagedata.degenerate |= imaskdata.degenerate;

        switch ( imageargs->interleave) {
        case INTERLEAVE_PER_SAMPLE:
          HQASSERT(imageargs->nprocs == 1,
                   "Should be 1 image proc when INTERLEAVE_PER_SAMPLE");
          HQASSERT(imaskargs->nprocs == 1, "Should be 1 mask proc");
          break;
        case INTERLEAVE_CHROMA:
          /* Ensure there is a buffer allocated for the mask. */
          HQASSERT(imagetype != TypeImageAlphaImage,
                   "Alpha masked image cannot be INTERLEAVE_CHROMA" );
          HQASSERT(imaskargs->nprocs == 1, "Should be 1 mask proc");
          break;
        case INTERLEAVE_SCANLINE:
          HQASSERT(imageargs->nprocs == 1 &&
                   imaskargs->nprocs == 1,
                   "Should be 1 image and 1 mask proc");
          break;
        case INTERLEAVE_SEPARATE:
          break;
        default:
          HQFAIL("Inconsistent image interleave type");
        }

        imaskdata.sharedIMB = TRUE;

        /* Scanline interleaved images require the sharing of data source.
         * Also sharing of buffers between the image and the mask. Allocate
         * for the biggest one.
         */
        if (imageargs->interleave == INTERLEAVE_SCANLINE) {
          imaskargs->data_src = imageargs->data_src;
          imaskdata.imb = imagedata.imb;
          if ( imagedata.bytes_in_input_line > imaskdata.bytes_in_input_line )
            imaskdata.sharedIMB = FALSE;
          else
            imagedata.sharedIMB = FALSE;
        }
      }
    } else {
      HQASSERT(imageargs->interleave == INTERLEAVE_NONE,
               "Inconsistent image interleave type");
    }
  }

  if ( result ) {
    int32 action = 0;

    HQASSERT(!error_signalled_context(errcontext),
             "Shouldn't be starting image in error state");
    error_clear_newerror_context(errcontext);

    for (;;) {
      Bool local_ripped_to_disk = FALSE;

      result = im_alloc_early_imagedata(imageargs, &imagedata);

      /* Unrecoverable error occurred, so abandon the image */
      if ( !result && newerror_context(errcontext) &&
           newerror_context(errcontext) != VMERROR )
        break;

      /* If the allocations all succeeded, without using any of the reserve
       * etc. then we can skip to the next step, and finish off the image
       */
      if ( result && !mm_memory_is_low )
        break;
      error_clear_context(errcontext); /* clear any VMerror */

      /* It ran out of memory, so do some low-memory handling, and try again. */
      action = handleLowMemory(action, TRY_NORMAL_METHODS,
                               &local_ripped_to_disk);
      if ( action < 0 ) { /* error */
        result = FALSE; break;
      }
      if ( action == 0 ) { /* nothing more to do */
        if ( result )
          /* Allocations succeeded using reserve. Continue with the image. */
          break;
        else {
          (void)error_handler(VMERROR);
          break;
        }
      }
      /* Start again with a clean slate. */
      im_freeimagedata( imageargs, &imagedata );
    }
  }

  /* (*im_end)() MUST be called if im_begin succeeds. */
  result = result && (*im_begin)(imageargs, &imagedata, data);
  if ( result ) {
    Bool need_im_end = TRUE;

    /* These args were left on the stack during the im_begin call so that
       hooks could look at them. Modifying any of the values is not advised or
       supported. They should probably be moved to temporarystack until after
       the (*im_end)() for GC. These must be removed before any "stripe" images
       are begun. */
    npop( imageargs->stack_args, stack );

    /* If not degenerate image or mask (width or height of 0) get data */
    if ( !imagedata.degenerate ) {
      /* height may be adjusted to reflect clipped image height. */
      int32 lines = 0, height, maskHeight = 0;
      int32 image_y1, image_y2 ; /* image early clip limits */
      int32 mask_y1, mask_y2 ; /* mask early clip limits */
      ImageLineType data_type = NoLines;
      Bool stripeWithNoData = FALSE;
      Bool (*getimageblock)(IMAGEARGS *, IMAGEDATA *,
                            IMAGEARGS *, IMAGEDATA *,
                            int32 *, ImageLineType *);

      if ( imageargs->filteredImage != NULL ) {
        getimageblock = im_getfilteredblock;
        imagedata.all_components_same = FALSE ;
        /* N.B. Reset again after striping. */
      } else {
        getimageblock = im_getimageblock;
      }

      height = imageargs->height;
      HQASSERT(height > 0, "image height should not be zero");

      image_y1 = 0 ;
      image_y2 = height - 1 ;

      if ( imaskargs )
        maskHeight = imaskargs->height;

      mask_y1 = 0 ;
      mask_y2 = maskHeight - 1 ;

      /* After image object created, clip is properly set up. */
      if ( early_clip_allowed ) {
        (*im_clip_lines)(imageargs, &imagedata, data,
                         &image_y1, &image_y2, &mask_y1, &mask_y2) ;
        if ( image_y1 == 0 )
          early_clip_allowed = FALSE ;
      }

      /* do all the callbacks to get the image data */
      do {
        SwOftenSafe();
        if ( !interrupts_clear(allow_interrupt) ) {
          (void)report_interrupt(allow_interrupt);
          result = FALSE;
          break;
        }

        /* lines > 0 at this point if striping, and stripe did not
         * consume entire block of data. */
        if (lines == 0 &&
            !(*getimageblock)(imageargs, &imagedata, imaskargs,
                              &imaskdata, &lines, &data_type) ) {
          /* an error here won't cause striping, just fail. */
          result = FALSE;
          break;
        }

        if ( data_type == NoLines ) {
          break;
        }

        /* no striping for masked images, but we will allow early clipping
           for extra data at the end. */
        if ( (data_type & MaskLines) != 0 ) {
          if ( imaskdata.image_lines > mask_y2 ||
               imaskdata.image_lines + lines <= mask_y1 ) {
            /* Clip off initial or trailing lines. */
            EMPTY_STATEMENT() ;
          } else if ( !(*im_data)(NAME_Mask, imaskargs, &imaskdata,
                                  lines, data) ) {
            /* No attempt at recovery here, just fail. */
            result = FALSE;
            break;
          }
          imaskdata.image_lines += lines;
        }

        if ( (data_type & ImageLines) != 0 ) {
          if ( imagedata.image_lines > image_y2 ||
               imagedata.image_lines + lines <= image_y1 ) {
            /* Clip off initial and trailing lines. */
            imagedata.image_lines += lines;
          } else {
            int32 oldimagelines = imagedata.image_lines;

            if ( !early_clip_allowed &&
                 (*im_data)(NAME_Image, imageargs, &imagedata, lines, data) ) {
              /* Normal case, all lines are processed. */
              imagedata.image_lines += lines;

              /* And reset the infinite loop check */
              stripeWithNoData = FALSE;
            } else {
              result = FALSE; /* failed unless we can successfully stripe. */

              if ( early_clip_allowed || stripe_allowed ) {
                Bool was_vmerror = (error_signalled_context(errcontext) &&
                                    newerror_context(errcontext) == VMERROR) ;

                HQASSERT(imagetype == TypeImageImage ||
                         imagetype == TypeImageMask,
                         "Trying to stripe un-stripeable image type.");

                if ( early_clip_allowed || was_vmerror ) {
                  /* Force out the stripe at this point,
                   * im_early_end preps image so far for entry on DL.
                   * im_stripe ensures state ok to start next stripe.
                   * dl_safe_recursion allows striping to partial paint
                   * if partial was allowed when im_common called.
                   */

                  (*im_early_end)(imageargs, &imagedata, data);

                  /* We're about to call im_end(), don't call it again unless
                     the im_begin() below succeeds. */
                  need_im_end = FALSE;

                  if ((*im_end)(imageargs, &imagedata, data, FALSE)) {
                    (*im_stripe)(imageargs, &imagedata, data);

                    /* Clear the vmerror because im_begin() demands it. */
                    error_clear_context(errcontext);
                    dl_safe_recursion = initial_dl_safe_recursion;

                    /*
                     * At this point we are in-between the im_begin and im_end
                     * of image striping, so it is the one point where it
                     * would be safe to partial-paint if we wanted to. But we
                     * do not normally, as other low-mem actions suffice. This,
                     * however, leads to a build-up of compressed striped
                     * images, and when we eventually do decide to PP we run
                     * out of memory. So force an early PP to avoid this.
                     */
                    if ( image_forcedpp && was_vmerror ) {
                      if ( partial_paint_allowed(corecontext) ) {
                        if ( !rip_to_disk(corecontext) ) {
                          result = FALSE;
                          break;
                        }
                      }
                    }

                    /** \todo @@@ TODO rogerg we are missing a clipped image
                     * optmization trick here. The stripe might cover
                     * the required height, so no need to start a stripe
                     * for the remaining data in the block. */

                    if ( (*im_begin)(imageargs, &imagedata, data) ) {
                      int32 linesProcessed = imagedata.image_lines -
                        oldimagelines;

                      need_im_end = TRUE;

                      /* Adjust for lines consumed up until VM error */
                      lines -= linesProcessed;
                      imagedata.state_line = -1;
                      if ( imageargs->filteredImage != NULL )
                        imagedata.all_components_same = FALSE;

                      /* Allow one more go at a stripe after a vmerror.
                       * We would hope to succeed after partial painting, but if
                       * that doesn't happen and we get back here without making
                       * progress, we'll bail out to avoid an infinite loop.
                       */
                      if (!stripeWithNoData) {
                        result = TRUE;
                        if ( early_clip_allowed ) {
                          /* This stripe was for early clipping. Start a new
                             stripe for real data. */
                          early_clip_allowed = FALSE ;
                        } else {
                          stripeWithNoData = (linesProcessed <= 0);
                        }
                        continue; /* avoid resetting the lines count */
                      } /* failed twice in a row to complete any lines */
                    }  /* else im_begin failed */

                    /* Rethrow the vmerror error we cancelled above. */
                    HQASSERT(!result, "result should be FALSE");
                    (void) error_handler(VMERROR);
                  }  /* else im_end failed */
                }  /* else non-VM error terminates the image. */
              }  /* else striping not allowed */

              HQASSERT(!result, "Bailing out of image striping with result TRUE");
              break;
            }
          }
        } /* image line data */

        lines = 0; /* might have mask and image lines in one block. */
      } while ( imagedata.image_lines < height ||
                imaskdata.image_lines < maskHeight );

      /* If we are going to do image_end, we need to set the final
         image_lines to the last clip line, so the image store is trimmed
         correctly. It's possible that the image did not have enough data to
         fill the clip, so only do this if we saw more data than fills the
         clip. */
      /** \todo ajcd 2014-01-17: This is a little bit crude, but is probably
          a temporary measure until the loop is changed to use lazy image
          object stripe construction. Lazy construction would address the
          todo about ending a stripe after the clip limit above. */
      if ( imagedata.image_lines > image_y2 )
        imagedata.image_lines = image_y2 + 1 ;
      if ( imaskdata.image_lines > mask_y2 )
        imaskdata.image_lines = mask_y2 + 1 ;
    } /* not degenerate data */

    if (need_im_end && !(*im_end)(imageargs, &imagedata, data, !result))
      result = FALSE;

  }

  im_freeimagedata(imageargs, &imagedata);

  /* For scanline interleaved images, we shared the data sources above,
   * so deshare them now.
   */
  if (imageargs->interleave == INTERLEAVE_SCANLINE)
    imaskargs->data_src = NULL;

  /* Restore state */
  theIdlomState(*gstateptr) = hdltState;

  if ( imagetype == TypeImageAlphaImage )
    rcbn_enable_interception(gstateptr->colorInfo);

#undef return
  return result;
}


Bool im_calculatematrix( IMAGEARGS *imageargs, OMATRIX *mptr,
                         Bool islowlevel )
{
  OMATRIX matrix;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(mptr, "mptr NULL");

  /* Extract matrix, calculate inverse and concatenate with CTM. */
  if ( ! matrix_inverse( & imageargs->omatrix, mptr ))
    return error_handler( UNDEFINEDRESULT );

  /* We may be called upon to round the image such that the corners fall on
   * device pixel boundaries. This is a hack that will eventually be cured with
   * 64 possible roundings in image tiles. For now we are stuck with 16
   * possible roundings and the probability that tessellated, rotated images
   * won't perfectly tessellate. The following works around the imperfect
   * tessellation by snapping the image corners to device pixels, the (0, 0)
   * corner is rounded down, while the other corners are rounded up.
   * Not very nice.
   */
  MATRIX_COPY(&matrix, &theIgsPageCTM(gstateptr));
  if ( get_core_context_interp()->userparams->SnapImageCorners ) {
    SYSTEMVALUE tx, ty, m00, m01, m10, m11;
    int32 sx, sy, s00, s01, s10, s11;

    /* Make the snapping sign independent.
     * This is advantageous for 45 degree rotations */
    s00 = (MATRIX_00(&matrix) >= 0) ? 1 : -1;
    s01 = (MATRIX_01(&matrix) >= 0) ? 1 : -1;
    s10 = (MATRIX_10(&matrix) >= 0) ? 1 : -1;
    s11 = (MATRIX_11(&matrix) >= 0) ? 1 : -1;
    sx  = (MATRIX_20(&matrix) >= 0) ? 1 : -1;
    sy  = (MATRIX_21(&matrix) >= 0) ? 1 : -1;

    MATRIX_00(&matrix) = fabs(MATRIX_00(&matrix));
    MATRIX_01(&matrix) = fabs(MATRIX_01(&matrix));
    MATRIX_10(&matrix) = fabs(MATRIX_10(&matrix));
    MATRIX_11(&matrix) = fabs(MATRIX_11(&matrix));
    MATRIX_20(&matrix) = fabs(MATRIX_20(&matrix));
    MATRIX_21(&matrix) = fabs(MATRIX_21(&matrix));

    /* Round translation point and make the snapping translation independent */
    SC_C2D_INTF(tx, MATRIX_20(&matrix));
    SC_C2D_INTF(ty, MATRIX_21(&matrix));

    MATRIX_20(&matrix) = 0;
    MATRIX_21(&matrix) = 0;

    /* Round original (1,0) transformed point, and convert to delta */
    MATRIX_TRANSFORM_XY(1, 0, m00, m01, &matrix);
    SC_C2D_INTF(m00, m00 + 1);
    SC_C2D_INTF(m01, m01 + 1);

    /* Round original (0,1) transformed point, inc and convert to delta */
    MATRIX_TRANSFORM_XY(0, 1, m10, m11, &matrix);
    SC_C2D_INTF(m10, m10 + 1);
    SC_C2D_INTF(m11, m11 + 1);

    MATRIX_00(&matrix) = m00 * s00;
    MATRIX_01(&matrix) = m01 * s01;
    MATRIX_10(&matrix) = m10 * s10;
    MATRIX_11(&matrix) = m11 * s11;
    MATRIX_20(&matrix) = tx * sx;
    MATRIX_21(&matrix) = ty * sy;

    MATRIX_SET_OPT_BOTH(&matrix);

    imageargs->fit_edges = TRUE;
  }

  matrix_mult(mptr, &matrix, mptr);

  if (imageargs->clean_matrix)
    matrix_clean( mptr );

  if ( islowlevel ) { /* Low level matrix required */
    /* If resultant image is almost orthogonal, then make it so. */
    SYSTEMVALUE tmpxx = ( SYSTEMVALUE )imageargs->width *
                        mptr->matrix[0][0];
    SYSTEMVALUE tmpxy = ( SYSTEMVALUE )imageargs->width *
                        mptr->matrix[0][1];
    SYSTEMVALUE tmpyx = ( SYSTEMVALUE )imageargs->height *
                        mptr->matrix[1][0];
    SYSTEMVALUE tmpyy = ( SYSTEMVALUE )imageargs->height *
                        mptr->matrix[1][1];
    /* If gradient is 1/100 close to being orthogonal, snap it. */
#define GRADIENT_TOLERANCE 100.0

    if (( mptr->opt & MATRIX_OPT_0011 ) != 0 ) {
      if ( fabs( tmpxx ) < 0.5 &&
           fabs( tmpxx * GRADIENT_TOLERANCE ) < fabs( tmpxy ) ) {
        /* Gradient of first axis is near vertical. */
        mptr->matrix[0][0] = 0.0;
        mptr->matrix[2][0] += ( 0.5 * tmpxx );
        MATRIX_SET_OPT_BOTH( mptr );
      }
      if ( fabs( tmpyy ) < 0.5 &&
           fabs( tmpyy * GRADIENT_TOLERANCE ) < fabs( tmpyx ) ) {
        /* Gradient of second axis is near horizontal. */
        mptr->matrix[1][1] = 0.0;
        mptr->matrix[2][1] += ( 0.5 * tmpyy );
        MATRIX_SET_OPT_BOTH( mptr );
      }
    }
    if (( mptr->opt & MATRIX_OPT_1001 ) != 0 ) {
      if ( fabs( tmpyx ) < 0.5 &&
           fabs( tmpyx * GRADIENT_TOLERANCE ) < fabs( tmpyy ) ) {
        /* Gradient of second axis is near vertical. */
        mptr->matrix[1][0] = 0.0;
        mptr->matrix[2][0] += ( 0.5 * tmpyx );
        MATRIX_SET_OPT_BOTH( mptr );
      }
      if ( fabs( tmpxy ) < 0.5 &&
           fabs( tmpxy * GRADIENT_TOLERANCE ) < fabs( tmpxx ) ) {
        /* Gradient of first axis is near horizontal. */
        mptr->matrix[0][1] = 0.0;
        mptr->matrix[2][1] += ( 0.5 * tmpxy );
        MATRIX_SET_OPT_BOTH( mptr );
      }
    }

    if ( imageargs->image_pixel_centers && !imageargs->fit_edges ) {
      mptr->matrix[2][0] += 0.5;   /* Use pixel centres, not edges. */
      mptr->matrix[2][1] += 0.5;   /* Use pixel centres, not edges. */
    }
  }

  return TRUE;
}

/**
 * This function returns TRUE if image data exists, and it is all blank
 * (presep white).  This is used to simplify final color conversion and
 * therefore improvement performance.
 */
static Bool im_rcb_blankimage(IMAGEOBJECT* image, COLORANTINDEX ci)
{
  int32 test_result;

  HQASSERT(image, "image is null");
  HQASSERT(ci >= 0, "invalid ci");

  /** \todo @@jonw TODO FIXME Same cop-out for trapping :-> */
  if (isTrappingActive(CoreContext.page))
    return FALSE;

  /* Recognise a transfer function that maps all values to zero
     (white in presep space) */
  test_result = im_expandluttest(image->ime, ci, COLORVALUE_PRESEP_WHITE);
  if (test_result == imcolor_same)
    return TRUE;

  HQASSERT(im_expandobpp(image->ime) == 16, "Expected 16-bit image values");

  /* Laboriously test every pixel. */
  test_result = im_expanddatatest(image->ime, image->ims,
                                  ci, COLORVALUE_PRESEP_WHITE,
                                  &image->imsbbox);
  return test_result == imcolor_same;
}

/* ---------------------------------------------------------------------- */

/* Exact image space to device space and vice-versa transforms. */

/** Multiply two partial device to image space transform results, checking
    against overflow. */
#define IM_DCROSS_MUL(res_, a_, b_) MACRO_START                         \
  im_dcross_t _a = (a_) ;                                               \
  im_dcross_t _b = (b_) ;                                               \
  HQASSERT(_a > 0                                                       \
           ? (_b <= IM_DCROSS_MAX / _a && _b >= IM_DCROSS_MIN / _a)     \
           : _a < 0                                                     \
           ? (_b <= IM_DCROSS_MIN / _a && _b >= IM_DCROSS_MAX / _a)     \
           : TRUE,                                                      \
           "Partial device to image transform overflow (mul)") ;        \
  res_ = _a * _b ;                                                      \
MACRO_END

/** Add two partial device to image space transform results, checking against
    overflow. */
#define IM_DCROSS_ADD(res_, a_, b_) MACRO_START                         \
  im_dcross_t _a = (a_) ;                                               \
  im_dcross_t _b = (b_) ;                                               \
  HQASSERT(_a > 0                                                       \
           ? (_b <= IM_DCROSS_MAX - _a)                                 \
           : _a < 0                                                     \
           ? (_b >= IM_DCROSS_MIN - _a)                                 \
           : TRUE,                                                      \
           "Partial device to image transform overflow (add)") ;        \
  res_ = _a + _b ;                                                      \
MACRO_END

/** Complete a partial image-space to device space transform.

    Uses free variables geometry, ibbox_transform. */
#define IM_I2D_COMPLETE(px_, py_, wh_, ixy_) MACRO_START                \
  im_dcross_t _txy ;                                                    \
  IM_DCROSS_ADD(_txy, (px_), (py_)) ;                                   \
  IM_DCROSS_MUL(_txy, geometry->wh_, _txy) ;                            \
  /* If the vector is heading towards the bottom or right edges in device \
     space (which we've already increased), round the transformed value \
     down in image space. */                                            \
  if ( geometry->wh_ ## x > 0 || geometry->wh_ ## y > 0 )               \
    IM_DCROSS_ADD(_txy, -geometry->cross_sign, _txy) ;                  \
  _txy /= geometry->cross ;                                             \
  HQASSERT(_txy >= MININT32 && _txy <= MAXINT32,                        \
           "Device space to image space overflow") ;                    \
  ixy_ = (int32)_txy ; /* Truncation OK here */                         \
MACRO_END

/** Helper function to project an exterior point to the boundary of image
    space along a device-space orthogonal axis. This is used to shrink a
    transformed device bounding box to a smaller area in image space.

    Projecting along device-space axes, we will encounter the image space
    boundary at two possible locations. For a point outside of the V
    boundary, an X or Y projection can be decomposed into a vector along the
    V axis to reach the boundary, and a vector along the U axis to reach the
    original X or Y position. The ratio of these vectors is based on the
    forward matrix components:

      [XperU YperU XperV YperV] = [a b c d]

      YperV / YperU = UperVx = (X projection)
      XperV / XperU = UperVy = (Y projection)

    geometry->wx, geometry->wy is forward matrix times width (height for hx,
    hy):

      [geometry->wx geometry->wy geometry->hx geometry->hy] = [aw bw ch dh]

    Thus, we have:

      UperVx = (geometry->hy / h) * (w / geometry->wy)
             = (geometry->hy * w) / (geometry->wy * h)
      UperVy = (geometry->hx / h) * (w / geometry->wx)
             = (geometry->hx * w) / (geometry->wx * h)
      VperUx = (geometry->wy * h) / (geometry->hy * w)
      VperUy = (geometry->wx * h) / (geometry->hx * w)

    The magnitude of these ratios are correct, but the sign may not be, if
    both of the X or Y components of the vectors have the same sign. The sign
    is determined by looking at the U/V change in the direction of the
    projection.

    The U/V values passed in do not have fractional information. Since they
    may be just outside the U/V boundary, we round down the computed V/U
    change to avoid over-shrinking the bounding box.
*/
static Bool project_to_boundary(IPOINT uv[4], int32 i, int32 dx, int32 dy,
                                const ibbox_t *imsbbox,
                                const im_transform_t *geometry,
                                ibbox_t *ibbox_transform)
{
  int32 u1, v1, du1, du2, dv1, dv2 ;

  if ( bbox_contains(ibbox_transform, imsbbox) ) /* Nothing more to do. */
    return FALSE ;

  u1 = uv[i].x ; v1 = uv[i].y ;
  du1 = u1 - imsbbox->x1 ; du2 = imsbbox->x2 - u1 ;
  dv1 = v1 - imsbbox->y1 ; dv2 = imsbbox->y2 - v1 ;

  HQASSERT(dx == -1 || dx == 1, "X sign invalid") ;
  HQASSERT(dy == -1 || dy == 1, "Y sign invalid") ;
  HQASSERT(du1 >= 0 || du2 >= 0, "Outside both U boundaries simultaneously") ;
  HQASSERT(dv1 >= 0 || dv2 >= 0, "Outside both V boundaries simultaneously") ;

  if ( (du1 ^ du2) >= 0 && (dv1 ^ dv2) >= 0 ) {
    /* Point is completely inside image space bbox. */
    bbox_union_coordinates(ibbox_transform, u1, v1, u1, v1) ;
  } else if ( (dv1 ^ dv2) >= 0 ) { /* u1 is outside, v1 is inside. */
    int32 u, v, du, dv ;
    im_dcross_t num, den ;
    int32 ux = uv[i + dx].x, uy = uv[i + dy * 2].x ;

    du = du1 < 0 ? -du1 : du2 ;
    u = u1 + du ;

    /* Find V projected along X. If the adjacent device X bbox corner is
       outside the same boundary of U, then the projection won't
       intersect. */
    if ( geometry->hy != 0 &&
         (du1 < 0 ? ux >= imsbbox->x1 : ux <= imsbbox->x2) ) {
      IM_DCROSS_MUL(num, abs(geometry->wy), geometry->h) ;
      IM_DCROSS_MUL(num, num, abs(du) - 1) ;
      IM_DCROSS_MUL(den, abs(geometry->hy), geometry->w) ;
      /* Truncation is OK, we want the bbox to round outwards. */
      dv = CAST_SIGNED_TO_INT32(num / den) ;
      /* Plain multiplication is OK, because direction of V-change along
         dx direction is +/- 1. */
      dv *= SIGN32(uv[i + dx].y - v1) ;
      v = v1 + dv ;
      bbox_union_coordinates(ibbox_transform, u, v, u, v) ;
    }

    /* Find V projected along Y. If the adjacent device Y bbox corner is
       outside the same boundary of U, then the projection won't
       intersect. */
    if ( geometry->hx != 0 &&
         (du1 < 0 ? uy >= imsbbox->x1 : uy <= imsbbox->x2) ) {
      IM_DCROSS_MUL(num, abs(geometry->wx), geometry->h) ;
      IM_DCROSS_MUL(num, num, abs(du) - 1) ;
      IM_DCROSS_MUL(den, abs(geometry->hx), geometry->w) ;
      /* Truncation is OK, we want the bbox to round outwards. */
      dv = CAST_SIGNED_TO_INT32(num / den) ;
      /* Plain multiplication is OK, because direction of V-change along
         dy direction is +/- 1. */
      dv *= SIGN32(uv[i + dy * 2].y - v1) ;
      v = v1 + dv ;
      bbox_union_coordinates(ibbox_transform, u, v, u, v) ;
    }
  } else if ( (du1 ^ du2) >= 0 ) { /* v1 is outside, u1 is inside. */
    int32 u, v, du, dv ;
    im_dcross_t num, den ;
    int32 vx = uv[i + dx].y, vy = uv[i + dy * 2].y ;

    dv = dv1 < 0 ? -dv1 : dv2 ;
    v = v1 + dv ;

    /* Find U projected along X. If the adjacent device X bbox corner is
       outside the same boundary of V, then the projection won't
       intersect. */
    if ( geometry->wy != 0 &&
         (dv1 < 0 ? vx >= imsbbox->y1 : vx <= imsbbox->y2) ) {
      IM_DCROSS_MUL(num, abs(geometry->hy), geometry->w) ;
      IM_DCROSS_MUL(num, num, abs(dv) - 1) ;
      IM_DCROSS_MUL(den, abs(geometry->wy), geometry->h) ;
      /* Truncation is OK, we want the bbox to round outwards. */
      du = CAST_SIGNED_TO_INT32(num / den) ;
      /* Plain multiplication is OK, because direction of U-change along
         dx direction is +/- 1. */
      du *= SIGN32(uv[i + dx].x - u1) ;
      u = u1 + du ;
      bbox_union_coordinates(ibbox_transform, u, v, u, v) ;
    }

    /* Find U projected along Y. If the adjacent device Y bbox corner is
       outside the same boundary of V, then the projection won't
       intersect. */
    if ( geometry->wx != 0 &&
         (dv1 < 0 ? vy >= imsbbox->y1 : vy <= imsbbox->y2) ) {
      IM_DCROSS_MUL(num, abs(geometry->hx), geometry->w) ;
      IM_DCROSS_MUL(num, num, abs(dv) - 1) ;
      IM_DCROSS_MUL(den, abs(geometry->wx), geometry->h) ;
      /* Truncation is OK, we want the bbox to round outwards. */
      du = CAST_SIGNED_TO_INT32(num / den) ;
      /* Plain multiplication is OK, because direction of U-change along dy
         direction is +/- 1. */
      du *= SIGN32(uv[i + dy * 2].x - u1) ;
      u = u1 + du ;
      bbox_union_coordinates(ibbox_transform, u, v, u, v) ;
    }
  } else {
    /* Both u1 and v1 are outside of the image space bbox. This can only
       happen for skew matrices, because the device-space bounding box is
       reduced to the image extent, so for non-skew matrices all four corners
       are on bounding box edges. Snap the point to the closest corner. */
    int32 u = du1 < 0 ? u1 - du1 : u1 + du2 ;
    int32 v = dv1 < 0 ? v1 - dv1 : v1 + dv2 ;
    bbox_union_coordinates(ibbox_transform, u, v, u, v) ;
  }

  return TRUE ;
}

/** \todo ajcd 2014-01-22: Decide whether to just use geometry object, and
    not image object. This would allow use for clipping before the image
    object is created, but would require intersecting with the geometry
    extent rather than the actual image clip box, and would require result or
    post-intersection with the actual image space bbox. */
Bool image_ibbox_covering_dbbox(const IMAGEOBJECT *imageobj,
                                const dbbox_t *dbbox,
                                ibbox_t *ibbox)
{
  dbbox_t dbbox_clipped ;
  ibbox_t ibbox_transform ;
  const ibbox_t *imsbbox ;
  const im_transform_t *geometry ;
  im_dcross_t tx1, tx2, ty1, ty2 ;
  /* Points are arranged around the corners of the clip bbox:
     0: x1,y1  1: x2,y1  2: x1,y2  3: x2,y2 */
  IPOINT uv[4] ;

  HQASSERT(imageobj != NULL, "No image object") ;
  geometry = &imageobj->geometry ;
  IM_TRANSFORM_ASSERTS(geometry) ;
  HQASSERT(dbbox != NULL, "No device space bbox") ;
  HQASSERT(ibbox != NULL, "No image space bbox") ;
  imsbbox = &imageobj->imsbbox ;

  if ( geometry->cross_sign == 0 ) {
    /* Non-invertible matrix implies no visible area, we'll make the result
       equivalent to an empty bbox. */
    return FALSE ;
  }

  HQASSERT(geometry->cross != 0, "Invalid image geometry") ;

  bbox_intersection(&imageobj->bbox, dbbox, &dbbox_clipped) ;
  if ( bbox_is_empty(&dbbox_clipped) )
    return FALSE ;

  bbox_clear(&ibbox_transform) ;

  /* We're going to transform from device space to image space. In the
     general case, the steps are the inverse of the forward matrix multiplied
     by the unit vectors. These reduce to:

     uperx = hy * w / cross
     vperx = -wy * h / cross
     upery = -hx * w / cross
     vpery = wx * h / cross

     To find the 0 <= u < w coordinate, we need to take the device-space
     delta transform (dx,dy) from the device-space geometry origin (tx,ty),
     and compute:

     u = dx * uperx + dy * upery
       = w * (dx * hy - dy * hx) / cross

     Similarly,

     v = dx * vperx + dy * vpery
       = h * (dx * -wy + dy * wx) / cross

     We'll perform these computations using im_dcross_t values, checking for
     overflow along the way. (Overflow is not expected, because the minimum
     range of exact integer values that we can represent with im_dcross_t is
     0..2^53-1.)

     Truncation towards zero is OK when converting im_dcross_t to int32,
     because the final result will be intersected with the image space bbox,
     which is all non-negative. Since we clipped the device-space bbox to the
     image extent before transforming the coordinates, we will assert that we
     don't overflow image space coordinate range rather than clip to range.
  */

  /* Convert absolute device bbox to vectors from image origin. */
  dbbox_clipped.x1 -= geometry->tx ;
  dbbox_clipped.x2 -= geometry->tx ;
  dbbox_clipped.y1 -= geometry->ty ;
  dbbox_clipped.y2 -= geometry->ty ;

  /* The bounding box we got told to draw is inclusive. Device pixels can be
     visualized as half-open ranges, covering (x,x+1] and (y,y+1].

     Image rendering has an off-by-one asymmetry, to make images tesselate.
     We do not render the right (x2) or bottom (y2) edges of each source
     pixel when mapped to device space. This means that any source pixel that
     starts and ends within the same device pixel will be ignored (including
     source pixels that start to the left of the clip boundary and finish in
     the leftmost device pixel). This equates to using the colour of the
     source pixel just to the left/top of the transition between device
     pixels.

     We expand the right and bottom edges of the device bbox so that the
     transforms will include the coordinate of the transition boundary. We
     then compensate by subtracting an epsilon from the transformed value
     before truncating it, but only if the image source row is increasing
     (otherwise the image space floor rounds up in device space,
     corresponding to the increased boundary). */
  dbbox_clipped.x2 += 1 ;
  dbbox_clipped.y2 += 1 ;

  /* Because we render rotated images by quantising the row start position
     and using the delta for columns along the row, we don't add the
     fractional parts of the row start and the column position together, and
     so may miss carries in the DDA. This means we won't necessarily touch
     all of the device pixels we should do based on an image space transform
     that from the device space bbox that does take the fractional carries
     into account. To ensure we cover all rows without the carry, when
     rendering rotated images we enlarge the clip bounds by one pixel before
     converting to device space. The DDAs represent the floor of the device
     coordinates, so we only enlarge the upper bound. */

  /* If either of the U or V vectors is vertical, we can increase the left
     edge of the box we're considering to start at the boundary transition
     between the left edge and the next pixel. The left and right edges of
     the image will be vertical, there will be no partial pixel
     inclusions. */
  if ( geometry->wx == 0 || geometry->hx == 0 )
    dbbox_clipped.x1 += 1 ;
  else
    dbbox_clipped.x2 += 1 ; /* Compensation for rotated image carry. */

  /* If either of the U or V vectors is horizontal, we can increase the top
     edge of the box we're considering to start at the boundary transition
     between the top edge and the next pixel. The top and bottom edges of the
     image will be horizontal, there will be no partial pixel inclusions. */
  if ( geometry->wy == 0 || geometry->hy == 0 )
    dbbox_clipped.y1 += 1 ;
  else
    dbbox_clipped.y2 += 1 ; /* Compensation for rotated image carry. */

  /* U value for x1,y1. */
  IM_DCROSS_MUL(tx1, dbbox_clipped.x1, geometry->hy) ;
  IM_DCROSS_MUL(ty1, dbbox_clipped.y1, -geometry->hx) ;
  IM_I2D_COMPLETE(tx1, ty1, w, uv[0].x) ;

  /* U value for x2,y2. */
  IM_DCROSS_MUL(tx2, dbbox_clipped.x2, geometry->hy) ;
  IM_DCROSS_MUL(ty2, dbbox_clipped.y2, -geometry->hx) ;
  IM_I2D_COMPLETE(tx2, ty2, w, uv[3].x) ;

  if ( tx1 == tx2 ) {
    uv[1].x = uv[0].x ; /* x2,y1 is same as x1,y1 */
    uv[2].x = uv[3].x ; /* x1,y2 is same as x2,y2 */
  } else if ( ty1 == ty2 ) {
    uv[1].x = uv[3].x ; /* x2,y1 is same as x2,y2 */
    uv[2].x = uv[0].x ; /* x1,y2 is same as x1,y1 */
  } else {
    /* U value for x2,y1. */
    IM_I2D_COMPLETE(tx2, ty1, w, uv[1].x) ;

    /* U value for x1,y2. */
    IM_I2D_COMPLETE(tx1, ty2, w, uv[2].x) ;
  }

  /* V value for x1,y1. */
  IM_DCROSS_MUL(tx1, dbbox_clipped.x1, -geometry->wy) ;
  IM_DCROSS_MUL(ty1, dbbox_clipped.y1, geometry->wx) ;
  IM_I2D_COMPLETE(tx1, ty1, h, uv[0].y) ;

  /* V value for x2,y2. */
  IM_DCROSS_MUL(tx2, dbbox_clipped.x2, -geometry->wy) ;
  IM_DCROSS_MUL(ty2, dbbox_clipped.y2, geometry->wx) ;
  IM_I2D_COMPLETE(tx2, ty2, h, uv[3].y) ;

  if ( tx1 == tx2 ) {
    uv[1].y = uv[0].y ; /* x2,y1 is same as x1,y1 */
    uv[2].y = uv[3].y ; /* x1,y2 is same as x2,y2 */
  } else if ( ty1 == ty2 ) {
    uv[1].y = uv[3].y ; /* x2,y1 is same as x2,y2 */
    uv[2].y = uv[0].y ; /* x1,y2 is same as x1,y1 */
  } else {
    /* V value for x2,y1. */
    IM_I2D_COMPLETE(tx2, ty1, h, uv[1].y) ;

    /* V value for x1,y2. */
    IM_I2D_COMPLETE(tx1, ty2, h, uv[2].y) ;
  }

  /* Ensure each image space coordinate is inside image space, projecting to
     the boundary of image space if necessary, and combining with the image
     space bounding box. There is one case that this will fail to construct
     the minimal bounding box, when the entire device space bounding box is
     outside the image space bounding box. In this case, the bounding box
     will be projected onto one edge of image space. */
  (void)(project_to_boundary(uv, 0,  1,  1, imsbbox, geometry, &ibbox_transform) &&
         project_to_boundary(uv, 3, -1, -1, imsbbox, geometry, &ibbox_transform) &&
         project_to_boundary(uv, 1, -1,  1, imsbbox, geometry, &ibbox_transform) &&
         project_to_boundary(uv, 2,  1, -1, imsbbox, geometry, &ibbox_transform)) ;

  /* Intersect with the stored region of the image, and test if there was
     anything left after intersection. */
  bbox_intersection(&ibbox_transform, imsbbox, ibbox) ;

  return !bbox_is_empty(ibbox) ;
}

/*
Log stripped */
