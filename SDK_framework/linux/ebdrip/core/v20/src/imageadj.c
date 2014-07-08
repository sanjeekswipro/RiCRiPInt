/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imageadj.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image adjustment code.
 */

#include "core.h"

#include "imageadj.h"
#include "images.h"
#include "imexpand.h"
#include "imageo.h"
#include "imstore.h"
#include "dl_image.h"
#include "cvdecodes.h"

#include "control.h"
#include "swerrors.h"
#include "display.h"
#include "gschead.h"
#include "cmpprog.h"
#include "often.h"
#include "gu_chan.h"
#include "pixelLabels.h"
#include "rcbcntrl.h"
#include "namedef_.h"
#include "rcbadjst.h"
#include "spdetect.h"
#include "interrupts.h"
#include "gscfastrgb2gray.h"

/* ---------------------------------------------------------------------- */

/** Container structure for all the state need to do the image adjustment. */
typedef struct IM_ADJ_DATA {
  IMAGEARGS imageargs ;       /* imageargs structure */
  IMAGEDATA imagedata ;       /* imagedata structure */
  IM_BUFFER imagebuf ;        /* buffering structure.*/

  Bool replace ;              /**< Replace image if only needed for direct
                                 regions, or for both direct and backdrop
                                 regions set up an alternate image LUT or
                                 on-the-fly conversion */
  Bool independent ;          /**< Do we have independent channels? */

  Bool update_progress ;      /* Update the progress dial */

  IM_EXPAND *image_ime ;      /* Pointer to new expander for image */
  Bool useluts ;              /**< Can use a LUT to convert this image */

  uint32 ims_flags;           /**< Flipping and swapping is disabled whilst
                                   doing image adjustment.  It is re-enabled
                                   in the new or re-used image at the end. */

  Bool (*im_adj_func)(IMAGEOBJECT *imageobj, LISTOBJECT *image_lobj,
                      COLORANTINDEX *inputcolorants,
                      struct IM_ADJ_DATA *im_adj_data) ;
                              /* Callback to image adj function variant */
} IM_ADJ_DATA ;

static Bool im_adj_interleave(IMAGEOBJECT *imageobj,
                              LISTOBJECT *image_lobj,
                              COLORANTINDEX *inputcolorants,
                              IM_ADJ_DATA *im_adj_data);

/* ---------------------------------------------------------------------- */
/* Fill in the bare minimum of imageargs we can; this is the INPUT data. */
static void im_adj_imageargs( IMAGEARGS *imageargs ,
                              IMAGEOBJECT *imageobj ,
                              GS_COLORinfo *colorInfo ,
                              int32 colorType )
{
  const ibbox_t *bbox = im_storebbox_trimmed(imageobj->ims);

  imageargs->width  = bbox->x2 - bbox->x1 + 1;
  imageargs->height = bbox->y2 - bbox->y1 + 1;

  imageargs->bits_per_comp = im_expandbpp( imageobj->ime );

  imageargs->ncomps = gsc_dimensions( colorInfo , colorType );
  imageargs->nprocs = imageargs->ncomps;

  imageargs->image_color_space = gsc_getcolorspace( colorInfo , colorType );
  imageargs->image_color_object = *gsc_getcolorspaceobject(colorInfo,
                                                           colorType);

  imageargs->data_src = NULL;
  imageargs->stack_args = 0;

  imageargs->imageop = NAME_Preseparation;
  if ( im_expand_ims_alpha(imageobj->ime) )
    imageargs->imagetype = TypeImageAlphaImage ;
  else
    imageargs->imagetype = TypeImageImage;

  imageargs->polarity = FALSE;
  imageargs->interpolate = FALSE;
  imageargs->interleave = INTERLEAVE_NONE;

  imageargs->lines_per_block = 1;
  imageargs->decode = NULL;

  imageargs->maskgen = NULL;
  imageargs->maskargs = NULL;

  imageargs->fit_edges = FALSE;
  imageargs->coerce_to_fill = FALSE;
  imageargs->colorType = ( int8 ) colorType;
}

/* ---------------------------------------------------------------------- */
static Bool im_adj_early_imagedata(IMAGEDATA *imagedata,
                                   IMAGEARGS *imageargs,
                                   IMAGEOBJECT *imageobj,
                                   Bool fixedpt16,
                                   USERVALUE *colorvalues,
                                   COLORANTINDEX *inputcolorants)
{
  Bool fsubtractive = ColorspaceIsSubtractive(imageargs->image_color_space);

  /* Generate the decodes to go from image data to input color values. */
  switch ( im_expandformat(imageobj->ime) ) {
  case ime_planar:
    if ( !im_alloc_adjdecodes(imageargs, imagedata, imageobj,
                              colorvalues, inputcolorants, fsubtractive) )
      return FALSE;
    break;
  case ime_colorconverted:
    if ( !cv_alloc_decodes(imagedata->page, imageargs->ncomps, fsubtractive,
                           &imageobj->cv_fdecodes, &imageobj->cv_ndecodes,
                           &imageobj->alloc_ncomps) )
      return FALSE;
    imagedata->u.fdecodes = imageobj->cv_fdecodes;
    break;
  default:
    /* Can't currently adjust images which have an interleaved lut therefore
       they are not allowed in the first place.  Image adjustment exists to
       prepare images for direct rendering so we should never see a backdrop
       image expander here. */
    HQFAIL("Unexpected or unrecognised image expansion format");
    return error_handler(UNREGISTERED);
  }

  if ( imagedata->method == GSC_USE_INTERPOLATION ) {
    /* Table-based color conversion. */
    if ( fixedpt16 ) {
      HQASSERT(/* imageobj->decodesobj == NULL && */
               imagedata->u.fdecodes == imageobj->cv_fdecodes,
               "Expected to be using cv_fdecodes");
      imagedata->ndecodes = imageobj->cv_ndecodes ;
    } else {
      HQASSERT(/* imageobj->decodesobj != NULL && */
               imagedata->u.fdecodes != imageobj->cv_fdecodes,
               "Expected not to be using cv_fdecodes");
      if ( ! im_alloc_ndecodes(imageargs, imagedata))
        return FALSE ;
    }
  }
  else if ( imagedata->method == GSC_USE_FASTRGB2GRAY ) {
    if ( !gsc_fastrgb2gray_prep(imagedata->colorInfo, imageargs->colorType,
                                imageargs->bits_per_comp,
                                imagedata->u.fdecodes[0],
                                imagedata->u.fdecodes[1],
                                imagedata->u.fdecodes[2]) )
      return FALSE;

  } else if (imagedata->method == GSC_USE_FASTRGB2CMYK) {
    /** \todo Directly access the image luts because we want stay with ints not floats. */
    COLORVALUE **luts = im_expand_decode_array(imageobj->ime);
    if (!gsc_fastrgb2cmyk_prep(imagedata->colorInfo, imageargs->bits_per_comp,
                               luts[0], luts[1], luts[2])) {
      return (FALSE);
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool im_adj_late_imagedata( IMAGEDATA *imagedata ,
                                   IMAGEARGS *imageargs ,
                                   IMAGEOBJECT *imageobj ,
                                   IM_BUFFER *imagebuf ,
                                   IM_ADJ_DATA *im_adj_data ,
                                   int32 colortype )
{
  IM_EXPAND *imexpand = im_adj_data->image_ime;
  Bool independent = im_adj_data->independent;
  int32 decodesize;
  int32 **decodes = NULL;

  /* no need to pass decode arrays to color converter in this case */
  if ( imagedata->method != GSC_USE_FASTRGB2GRAY &&
       imagedata->method != GSC_USE_FASTRGB2CMYK)
    decodes = (imagedata->ndecodes ? imagedata->ndecodes : imagedata->u.idecodes);

  imagedata->imb = imagebuf ;
  imagedata->expandformat = im_expandformat( imexpand );
  decodesize = imageargs->width;

  /* Decide whether we are colour converting */
  if ( imagedata->expandformat == ime_colorconverted ) {
    imagedata->imc =
      im_colcvtopen(imagedata->colorInfo, imagedata->pools, decodes,
                    imageargs->ncomps, decodesize, imagedata->out16,
                    imagedata->method, colortype, FALSE /* justDecode */);
    if ( imagedata->imc == NULL )
      return error_handler( VMERROR ) ;
  }

  /* Recombine of normal images requires the bufptrs, but not the buffers
   * because it reuses other buffers
   */
  if ( ! im_alloc_bufptrs( imageargs , imagedata ))
    return FALSE;

  imagedata->pd_use = 0 ;

  if ( imagedata->expandformat == ime_colorconverted ) {
    /* Color converting. */
    imagedata->pd_use = PD_UNPACK | PD_CLEAR | PD_NATIVE ;
  }
  else if ( (! independent && imagedata->expandformat == ime_planar) ||
             imagedata->expandformat == ime_interleaved2 ||
             imagedata->expandformat == ime_interleaved4 ||
             imagedata->expandformat == ime_interleaved8 ||
             imagedata->expandformat == ime_interleaved16) {
    /* Single plane or interleaved output that may be padded. */
    imagedata->pd_use = PD_UNPACK | PD_CLEAR | PD_PACK | PD_NATIVE ;
  }

  if ( imagedata->pd_use != 0 ) {
    imagedata->pd =
      pd_packdataopen(imagedata->pools, imageargs->ncomps ,
                      decodesize , im_storebpp( imageobj->ims ) ,
                      FALSE ,                    /* Interleaved */
                      im_expandpad( imexpand ) , /* Pad Data */
                      FALSE ,                    /* Don't pack 12bpp to 16 */
                      imagedata->pd_use);
    if ( imagedata->pd == NULL )
      return FALSE ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** Determines if all the planes of the image exist for the dl color. */
static Bool im_adj_allstoreplanesexist( IMAGEOBJECT *imageobj,
                                        COLORANTINDEX *inputcolorants,
                                        int32 ncolorants )
{
  int32 i;
  for ( i = 0 ; i < ncolorants ; ++i ) {
    COLORANTINDEX ci = inputcolorants[ i ] ;
    if ( ci == COLORANTINDEX_NONE ||
         !im_storeplaneexists( imageobj->ims , i ) )
      return FALSE ;
  }
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool im_adj_fillpackdata( PACKDATA *pd ,
                                 IM_STORE *ims , IM_EXPAND* ime , int32 bpp ,
                                 COLORANTINDEX *colorants , int32 incomps ,
                                 int32 x1 , int32 x2 , int32 y ,
                                 int32 **ibuf ,
                                 uint8 **blankBufs,
                                 corecontext_t *context )
{
  int32 x , n = 0 ;

  HQASSERT( pd != NULL , "im_adj_fillpackdata: pd NULL" ) ;
  HQASSERT( ims != NULL , "im_adj_fillpackdata: ims NULL" ) ;
  HQASSERT( ime != NULL , "im_adj_fillpackdata: ime NULL" ) ;
  HQASSERT( bpp > 0 , "im_adj_fillpackdata: bpp should be > 0" ) ;
  HQASSERT( colorants != NULL , "im_adj_fillpackdata: colorants NULL" ) ;
  HQASSERT( incomps > 0 , "im_adj_fillpackdata: incomps should be > 0" ) ;
  HQASSERT( x1 >= 0 , "im_adj_fillpackdata: x1 should be >= 0" ) ;
  HQASSERT( x2 >= 0 , "im_adj_fillpackdata: x2 should be >= 0" ) ;
  HQASSERT( y >= 0 , "im_adj_fillpackdata: y should be >= 0" ) ;
  HQASSERT( ibuf != NULL , "im_adj_fillpackdata: ibuf NULL" ) ;

  for ( x = x1 ; x <= x2 ; x += n ) {
    int32 i , bytes  = 0 ;
#if defined( ASSERT_BUILD )
    int32 tbytes = -1 ;
#endif

    for ( i = 0 ; i < incomps ; i++ ) {
      int32 plane = im_expandadjustplane(ime, i) ;
      Bool release = FALSE ;
      uint8 *buf = NULL ;

      if ( colorants[ i ] != COLORANTINDEX_NONE &&
           im_storeplaneexists( ims , plane )) {
        /* Have image data for this plane/colorant */
        if ( ! im_storeread( ims , x , y , plane , & buf , & bytes ))
          return FALSE ;
#if defined( ASSERT_BUILD )
        HQASSERT( tbytes == -1 || tbytes == bytes ,
                  "Expect to get same number of bytes for each plane" ) ;
        tbytes = bytes ;
#endif
        release = TRUE ;
      } else  if ( blankBufs != NULL ) {
        /* Only need to do this if *all* the planes are blank */
        bytes = x2 - x1 + 1 ;
        buf = blankBufs[ i ] ;
      }

      if ( buf != NULL ) {
        /* Work out number of pixels returned by the image store,
         * ignoring any pixels that are clipped out on the left.
         */
        n = (( bytes * 8 ) / bpp ) ;
        if ( n + x > x2 + 1 )
          n = x2 + 1 - x ; /* Can truncate - clipped on rhs */

        /* Unpack into int32 and interleave */
        pd_unpack_icomp( pd , buf , i , ibuf , n ) ;
      }

      /* Now the image data has been unpacked and buffered we can release the
         image store to make image resources available to the next read. */
      if ( release )
        im_storereadrelease(context) ;
    }
    HQASSERT( bytes != 0 && n > 0 , "Should have read from at least one image store" ) ;

    SwOftenUnsafe() ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
#if defined( ASSERT_BUILD )
static Bool trace_adjImages = FALSE ;
#endif

static Bool im_adj_adjustdata( IM_ADJ_DATA*    im_adj_data ,
                               IMAGEDATA*      imagedata ,
                               IMAGEARGS*      imageargs ,
                               IMAGEOBJECT*    imageobj ,
                               IM_STORE*       image_ims ,
                               COLORANTINDEX*  inputcolorants )
{
  Bool fBlankImage ;
  int32 i, width ;
  int32 bpp , incomps ;
  int32 y_exact , x1_exact , y1_exact , x2_exact , y2_exact ;
  int32 *ubuf ;
  Bool interrupt = FALSE, result = TRUE ;
  int32 image_ncolorants = 0;
  uint8 *tbuf = NULL;
  uint8 **blankBufs ;
  COLORANTINDEX *image_map = NULL ;
  const ibbox_t *bbox;
  ibbox_t trimbbox ;
  corecontext_t *context = get_core_context();

  /* The image store bbox is the area of the image store that remains
     after trimming.  This is the area to be converted. */
  bbox = im_storebbox_trimmed(imageobj->ims);
  bbox_load(bbox, x1_exact, y1_exact, x2_exact, y2_exact);

  incomps = imageargs->ncomps;
  bpp = im_storebpp( imageobj->ims ) ;

  /* Check for an image with only blank planes, unlikely but possible */
  fBlankImage = TRUE ;
  for ( i = 0 ; i < incomps ; i++ ) {
    COLORANTINDEX ci = inputcolorants[ i ] ;
    if ( ci != COLORANTINDEX_NONE &&
         im_storeplaneexists( imageobj->ims , i )) {
      fBlankImage = FALSE ;
      break ;
    }
  }
  blankBufs = NULL ;
  if ( fBlankImage ) {
    /* Have an image that contains *all* blank planes. This really should not
       happen, and if it does, it implies that the image has been recombined
       incorrectly. Since it is rare, we'll just create a line of blank image
       data and use that instead of reading from the image store. Note, this
       is not required when only *some* of the image planes are blank
    */
    HQTRACE( trace_adjImages ,
             ( "im_adj_adjustdata: Have an image with only blank planes, "
               "implying the image has been incorrectly recombined" )) ;
    if ( ! im_alloc_buffers( imageargs , imagedata ))
      return FALSE ;
    for ( i = 0 ; i < incomps ; ++i ) {
      imagedata->imb->ibuf[ i ] = imagedata->imb->tbuf[ i ] ;
      HqMemZero(imagedata->imb->ibuf[i],
                imagedata->bytes_in_input_line ) ;
    }
    blankBufs = imagedata->imb->ibuf ;
  }

  trimbbox = *bbox ;
  width = x2_exact - x1_exact + 1 ;
  for ( y_exact = y1_exact ; y_exact <= y2_exact && result; ++y_exact ) {
    if ( tbuf == NULL || !im_is_row_repeat(imageobj->ims, y_exact) ) {
      result = im_adj_fillpackdata( imagedata->pd ,
                                    imageobj->ims , imageobj->ime , bpp ,
                                    inputcolorants , incomps ,
                                    x1_exact , x2_exact ,
                                    y_exact ,
                                    & ubuf ,
                                    blankBufs, context ) ;
      if ( ! result )
        break ;

      /* Progressively trim the original image store to release blocks that
         can be re-used by the adjusted image store. */
      if ( y_exact < y2_exact && imageobj->ims != image_ims ) {
        trimbbox.y1 = y_exact + 1 ;
        im_storetrim(imageobj->ims, &trimbbox) ;
      }

      if ( imagedata->expandformat == ime_colorconverted ) {
        /* Color conversion */
        result = im_colcvt( imagedata->imc, ubuf, &tbuf ) ;
      }
      else {
        HQASSERT( ( imagedata->pd_use & PD_PACK ) != 0,
                  "PD_PACK not set but expecting packing" ) ;
        pd_pack( imagedata->pd , ubuf , & tbuf, width ) ;
      }

      /* Check for error */
      if ( ! result )
        break ;
    }
    /* Write image data into the new store(s) */
    result = im_storewrite(image_ims, x1_exact, y_exact,
                           IMS_ALLPLANES, tbuf, width) ;

    /* Check for error */
    if ( ! result )
      break ;

    /* See if the user tried to interrupt us */
    if ( ! interrupts_clear(allow_interrupt) ) {
      interrupt = TRUE ;
      break ;
    }

    if ( im_adj_data->update_progress ) {
      int32 rw = imageobj->imsbbox.x2 - imageobj->imsbbox.x1 + 1 ;
      updateDLProgressTotal((double)rw, PRECONVERT_PROGRESS) ;
    }

    SwOftenUnsafe() ;
  }

  /* Tidy up */
  if  ( image_map != NULL )
    mm_free( mm_pool_temp ,
            ( mm_addr_t ) image_map ,
            image_ncolorants * sizeof( COLORANTINDEX ) ) ;

  return ( interrupt ? report_interrupt(allow_interrupt) : result ) ;
}

/* ---------------------------------------------------------------------- */
static Bool im_adj_setup_new_imstore(IMAGEOBJECT *imageobj,
                                     IM_ADJ_DATA *im_adj_data,
                                     int32 bpp ,
                                     int32 planes ,
                                     IM_STORE **image_ims )
{
  IMAGEDATA *imagedata = &im_adj_data->imagedata ;
  const ibbox_t *bbox ;

  im_adj_data->ims_flags = im_storegetflags(imageobj->ims);
  im_storesetflags(imageobj->ims,
                   im_adj_data->ims_flags & ~(IMS_XFLIP|IMS_YFLIP|IMS_XYSWAP));

  /* The existing image store can be reused as the destination image store for
     the adjusted data providing all the right conditions are met.  By simply
     allocating an extra plane RGB to CMYK conversion can be handled more
     efficiently.  Interleaved LUTs are not included, because testing for the
     right conditions is not obvious and it is unlikely most will be suitable. */
  if ( im_adj_data->im_adj_func != im_adj_interleave &&
       bpp == im_storebpp(imageobj->ims) ) {
    Bool recycled;

    if ( !im_storerecycle(imageobj->ims, planes, &recycled) )
      return FALSE;

    if ( recycled ) {
      *image_ims = imageobj->ims ;
      return TRUE;
    }
  }

  /* We're not recycling the existing image store, so we can set up a new
     store restricted to the trimmed size of the original store. */
  bbox = im_storebbox_trimmed(imageobj->ims) ;

  /* Create new store for the image color adjusted, recombined, data */
  *image_ims = im_storeopen(imagedata->page->im_shared, bbox, planes, bpp, 0);
  if ( *image_ims == NULL )
    return error_handler( VMERROR ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool im_adj_finish_imstore(IM_ADJ_DATA *im_adj_data,
                                  IMAGEOBJECT *imageobj,
                                  IM_STORE *image_ims, Bool result)
{
  result = result && im_storeclose(image_ims);

  if ( !result ) {
    if ( imageobj->ims != image_ims )
      im_storefree(image_ims);
    return FALSE;
  }

  /* Update image with new image store and expander */

  if ( imageobj->ims != image_ims ) {
    im_storefree(imageobj->ims);
    imageobj->ims = image_ims;
  }

  /* Reinstate x/yflip and xyflip which needed to be temporarily disabled whilst
     image adjusting. */
  im_storesetflags(imageobj->ims, im_adj_data->ims_flags);
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Adjust image using an independent LUT or on-the-fly. */
static Bool im_adj_noop(IMAGEOBJECT *imageobj,
                        LISTOBJECT *image_lobj,
                        COLORANTINDEX *inputcolorants,
                        IM_ADJ_DATA *im_adj_data)
{
  UNUSED_PARAM(COLORANTINDEX *, inputcolorants);
  UNUSED_PARAM(LISTOBJECT*, image_lobj);

  /* This is function is nearly a no-op, because the work has either been done
     creating a new LUT in im_expandopenimage, or because the image will be
     converted on-the-fly. Either way the image store is left untouched. */
  if ( im_adj_data->update_progress ) {
    int32 rw = imageobj->imsbbox.x2 - imageobj->imsbbox.x1 + 1 ;
    int32 rh = imageobj->imsbbox.y2 - imageobj->imsbbox.y1 + 1 ;
    updateDLProgressTotal((double)rw * (double)rh, PRECONVERT_PROGRESS);
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Adjust image using an interleaved LUT. */
static Bool im_adj_interleave(IMAGEOBJECT *imageobj,
                              LISTOBJECT *image_lobj,
                              COLORANTINDEX *inputcolorants,
                              IM_ADJ_DATA *im_adj_data)
{
  IMAGEDATA *imagedata = &im_adj_data->imagedata;
  IMAGEARGS *imageargs = &im_adj_data->imageargs;
  IM_STORE *image_ims;
  int32 bpp = 8;
  Bool result;

  HQASSERT(!im_adj_data->independent, "Should NOT have independent channels");
  HQASSERT(im_adj_data->image_ime, "New expander for image null");
  HQASSERT(im_adj_data->replace, "Can only interleave the store if replacing");

  UNUSED_PARAM(LISTOBJECT* , image_lobj );

  /* single component or ime_interleaved2/4/8/16  */
  switch ( imagedata->expandformat ) {
  case ime_planar :
    bpp = imageargs->bits_per_comp;
    break;

  case ime_interleaved2:
  case ime_interleaved4:
  case ime_interleaved8:
  case ime_interleaved16:
    bpp = imagedata->expandformat;
    break;

  default:
    HQFAIL( "im_adj_interleavedLUT: unexpected  expandformat" ) ;
    break;
  }

  /* Create a new store to hold the interleaved image data. */
  if ( !im_adj_setup_new_imstore(imageobj,
                                 im_adj_data, bpp, 1, &image_ims) )
    return FALSE;

  /* Interleave the image data and write into the new image store. */
  result = im_adj_adjustdata(im_adj_data, imagedata, imageargs, imageobj,
                             image_ims, inputcolorants);

  return im_adj_finish_imstore(im_adj_data, imageobj, image_ims, result);
}

/* ---------------------------------------------------------------------- */
/** Adjust image using color conversion method. */
static Bool im_adj_colcvt(IMAGEOBJECT *imageobj,
                          LISTOBJECT *image_lobj,
                          COLORANTINDEX *inputcolorants,
                          IM_ADJ_DATA *im_adj_data)
{
  IMAGEDATA *imagedata = &im_adj_data->imagedata;
  IMAGEARGS *imageargs = &im_adj_data->imageargs;
  IM_STORE *image_ims = NULL;
  int32 bpp = imagedata->out16 ? 16 : 8;
  int32 planes = dl_num_channels(image_lobj->p_ncolor);
  Bool result;

  HQASSERT(im_adj_data->replace, "Can only color-convert the store if replacing");

  /* Image required in direct regions only.
     Replace the imstore with a new color-converted imstore. */

  if ( !im_adj_setup_new_imstore(imageobj, im_adj_data, bpp, planes, &image_ims) )
    return FALSE;

  result = im_adj_adjustdata(im_adj_data, imagedata, imageargs, imageobj,
                             image_ims, inputcolorants);

  return im_adj_finish_imstore(im_adj_data, imageobj, image_ims, result);
}

/* ---------------------------------------------------------------------- */
static Bool im_adj_adjustimage_begin( IMAGEOBJECT *imageobj ,
                                      LISTOBJECT *image_lobj ,
                                      USERVALUE *colorvalues ,
                                      COLORANTINDEX* inputcolorants ,
                                      int32 colortype ,
                                      IM_ADJ_DATA *im_adj_data )
{
  IMAGEARGS *imageargs = &im_adj_data->imageargs ;
  IMAGEDATA *imagedata = &im_adj_data->imagedata ;
  IM_BUFFER *imagebuf  = &im_adj_data->imagebuf ;
  IM_EXPAND *image_ime ;
  int32 interleaving;
  Bool independent;
  int32 bpp = 8 ;
  Bool fixedpt16 ;
  Bool allowLuts = TRUE, allowInterleavedLuts = FALSE ;
  int32 nplanes;
  int32 expandformat = im_expandformat(imageobj->ime);
  int32 decodes_incomps;
  void *decode_for_adjust;

  /* Determine if the colorspace has independent channels */
  if ( ! gsc_hasIndependentChannels( imagedata->colorInfo , colortype , TRUE ,
                                     & im_adj_data->independent ) )
    return FALSE ;

  /* If we are going to use an independent LUT, then all the images
   * planes of the data must exist since the expander requires this.
   */
  if ( im_adj_data->independent &&
       !im_adj_allstoreplanesexist( imageobj, inputcolorants,
                                    dl_num_channels(image_lobj->p_ncolor) ) )
    allowLuts = FALSE ;

  /* Fill in the imagearg object */
  im_adj_imageargs( imageargs , imageobj , imagedata->colorInfo , colortype ) ;

  /* Set up output components and plane */
  if ( ! im_prepareimageoutput(imageargs, imagedata, TRUE, FALSE) )
    return FALSE ;

  /* Determine the size of the image */
  if ( ! im_bitmapsize( imageargs , imagedata ) )
    return FALSE ;

  imagedata->lines_left_in_block = imageargs->lines_per_block ;
  HQASSERT(imagedata->lines_left_in_block > 0, "No lines per block") ;

  bpp = im_storebpp( imageobj->ims ) ;

  interleaving =
    (gucr_interleavingStyle(gsc_getTargetRS(imagedata->colorInfo))
     == GUCR_INTERLEAVINGSTYLE_PIXEL);

  /* All presep stores and some composite stores contain the original image
   * data with a planar lut.  Composite stores may additionally be color
   * converted at interpretation time and will therefore contain 16-bit
   * colors in the range
     [ 0 COLORVALUE_ONE ] (a subset of the full 16-bits). */
  fixedpt16 = ((imageobj->flags & IM_FLAG_PRESEP) == 0 &&
               imageargs->bits_per_comp == 16 &&
               im_expandformat(imageobj->ime) == ime_colorconverted);

  /* The image adjustment code can't handle interleaved LUTs so they must be
     disabled if the image is being converted to anything other than device
     space.  The image may be image-adjusted several times to finally reach
     device space. */
  allowInterleavedLuts =
    allowLuts &&
    !guc_backdropRasterStyle(gsc_getTargetRS(imagedata->colorInfo));

  if ( expandformat == ime_planar && im_expandiplanes(imageobj->ime) == 1 ) {
    /* The current colour chain may or may not be independent, but if the
       original image's colour space was indexed or mono then we can pretend it
       is always independent.  For example, this allows an indexed to RGB LUT to
       be converted to an indexed to (RGB to) CMYK LUT without having to colour
       convert the image.  The original indexed to RGB LUT is included in the
       decodes array (created by im_adj_alloc_decodes earlier).  The image store
       contains a single plane of data, and the new decodes array maps this
       plane to the input space of the current colour chain. */
    independent = TRUE;
    decodes_incomps = 1;
  } else {
    independent = im_adj_data->independent;
    decodes_incomps = imageargs->ncomps;
  }

  /* Setup the decodes arrays to map image data to input color values */
  if ( !im_adj_early_imagedata(imagedata, imageargs, imageobj,
                               fixedpt16, colorvalues, inputcolorants) )
    return FALSE;

  /* decode_for_adjust is used as a key in the image LUT cache, and the
     reference must be valid for the duration of the page. */
  if ( im_expandformat(imageobj->ime) == ime_colorconverted )
    /* Image decodes are the standard COLORVALUE decodes and all components
       refer to the same decode array. */
    decode_for_adjust = imagedata->u.fdecodes[0];
  else
    /* Image decodes are derived from the previous conversion's LUT. */
    decode_for_adjust = im_expand_decode_array(imageobj->ime);
  HQASSERT(decode_for_adjust != NULL, "Image LUT may not be cached");

  /* Create the new expand object for the transformed image. */
  image_ime = im_expandopenimage(imagedata->page,
                                 imagedata->u.fdecodes,
                                 imagedata->ndecodes,
                                 NULL, decode_for_adjust,
                                 decodes_incomps, independent,
                                 imageargs->width , imageargs->height ,
                                 imageargs->ncomps ,
                                 imageargs->bits_per_comp ,
                                 fixedpt16 ,
                                 imagedata->oncomps ,
                                 imagedata->out16 ,
                                 interleaving ,
                                 IME_ALLPLANES ,
                                 imagedata->colorInfo, colortype ,
                                 imageargs->imagetype , imagedata->method,
                                 imageargs->isSoftMask,
                                 allowLuts, allowInterleavedLuts,
                                 ime_colorconverted);
  if ( image_ime == NULL )
    return FALSE ;

  im_adj_data->image_ime = image_ime ;

  /* Check the previous image expander for an alpha channel and if present
     transfer it to the new image expander. */
  if ( im_adj_data->replace ) {
    IM_STORE *ims_alpha = im_expand_detach_alphachannel(imageobj->ime) ;
    if ( ims_alpha ) {
      if ( !im_expandalphachannel(image_ime, ims_alpha) ) {
        im_storefree(ims_alpha) ;
        return FALSE ;
      }
    }
  }

  /* Fill in the remaining fields of the imagedata object */
  if ( ! im_adj_late_imagedata( imagedata , imageargs , imageobj , imagebuf ,
                                im_adj_data , colortype) )
    return FALSE ;

  nplanes = im_storenplanes(imageobj->ims);

  /* Decide which function callback to use to adjust the image. The decision is
   * based on the expandformat setup by calling im_expandopenimage, whether the
   * colorspace has independent channels, and whether the image store is allowed
   * to be replaced.
   */
  if ( im_adj_data->replace ) {
    switch ( imagedata->expandformat ) {
    case ime_planar :
      im_adj_data->useluts = TRUE;
      im_adj_data->im_adj_func = im_adj_data->independent || nplanes == 1
        ? im_adj_noop : im_adj_interleave;
      break;

    case ime_interleaved2 : case ime_interleaved4 :
    case ime_interleaved8 : case ime_interleaved16 :
      /* Use interleaved LUT. */
      im_adj_data->useluts = TRUE;
      im_adj_data->im_adj_func = im_adj_interleave;
      break ;

    case ime_colorconverted :
      /* Color Convert image */
      im_adj_data->useluts = FALSE;
      im_adj_data->im_adj_func = im_adj_colcvt;
      break ;

    default:
      HQFAIL("Expand format not supported") ;
      return FALSE ;
    }
  } else {
    /* Allow an independent LUT. */
    im_adj_data->useluts = ( imagedata->expandformat == ime_planar &&
                             (im_adj_data->independent || nplanes == 1) );
    im_adj_data->im_adj_func = im_adj_noop;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool im_adj_adjustimage_end(IM_ADJ_DATA *im_adj_data,
                                   IMAGEOBJECT *imageobj, Bool result)
{
  IMAGEDATA *imagedata = &im_adj_data->imagedata;
  IMAGEARGS *imageargs = &im_adj_data->imageargs;

  if ( result ) {
    if ( im_adj_data->replace ) {
      /* Image required in direct regions only.
         Replace the image expander with the new one. */
      im_expandfree(imagedata->page, imageobj->ime);
      imageobj->ime = im_adj_data->image_ime;
      im_adj_data->image_ime = NULL;
      imageobj->sixteenbit_output = (uint8)imagedata->out16;
    } else {
      /* Image required in both direct and backdrop regions. */
      if ( im_adj_data->useluts ) {
        /* Attach the alternate image expander (with the LUT for the direct
           regions) to the existing image expander. */
        im_expand_attach_alternate(imageobj->ime, im_adj_data->image_ime);
        im_adj_data->image_ime = NULL;
      } else {
        /* Can't use a LUT so set up on-the-fly conversion.
           New image expander not required and is freed later. */
        if ( !im_expand_setup_on_the_fly(imageobj, imagedata->page,
                                         imagedata->method,
                                         imagedata->colorInfo,
                                         imageargs->colorType) )
          result = FALSE;
      }
    }
  }

  im_free_data(imageargs, imagedata);
  if ( im_adj_data->image_ime != NULL )
    im_expandfree(imagedata->page, im_adj_data->image_ime);
  return result;
}

/* ---------------------------------------------------------------------- */
/**
 * \todo BMJ 19-Sep-08 :  stop using temp pools for this
 */
static mm_pool_t *use_temp_pools(void)
{
  static mm_pool_t tmp_pools[3];

  tmp_pools[0] = tmp_pools[1] = tmp_pools[2] = mm_pool_temp;
  return tmp_pools;
}

/* ---------------------------------------------------------------------- */
static Bool im_adj_adjustimage_internal(DL_STATE *page, Bool replace,
                                        LISTOBJECT *image_lobj,
                                        USERVALUE *colorvalues,
                                        COLORANTINDEX *inputcolorants,
                                        GS_COLORinfo *colorInfo,
                                        int32 colortype, int32 method)
{
  IM_ADJ_DATA im_adj_data = { 0 };
  IMAGEDATA *imagedata = &im_adj_data.imagedata;
  IMAGEOBJECT *imageobj = image_lobj->dldata.image;
  Bool result;
  int32 ncomps;

  HQASSERT(imageobj->ims , "ims null");
  HQASSERT(imageobj->ime , "ime null");
  HQASSERT(colorvalues , "colorvalues null");
  HQASSERT(inputcolorants , "inputcolorants null" ) ;

  if ( (imageobj->flags & IM_FLAG_PRESEP) != 0 ) {
    if ( theINPlanes( imageobj ) == IM_GOTALLPLANES )
      return TRUE ;
    theINPlanes( imageobj ) = IM_GOTALLPLANES ;
  }

  im_adj_data.replace = replace;
  imagedata->page = page;
  imagedata->colorInfo = colorInfo;
  imagedata->pools = use_temp_pools();
  imagedata->method = method;

  ncomps = gsc_dimensions(colorInfo, colortype);

  /* Reorder preseparated image store and expander planes from ascending
     numerical inputcolorant order to given inputcolorant order (which then
     matches the non-preseparated case). */
  if ( (imageobj->flags & IM_FLAG_PRESEP) != 0 ) {
    const COLORANTINDEX *cis = im_getcis(imageobj->ime) ;
    int32 i;

    HQASSERT(im_expandformat(imageobj->ime) == ime_planar,
             "Recombined images should be planar") ;

    /* Check if already ordered. */
    for (i = 0; i < ncomps && inputcolorants[i] == cis[i]; ++i)
      EMPTY_STATEMENT();

    if (i < ncomps) {
      if ( !im_storereorder(imageobj->ims, cis, inputcolorants, ncomps) ||
           !im_expandreorder(page, imageobj->ime, inputcolorants, ncomps) )
        return FALSE;
    }
  }

  /* The image may require multiple conversions if it is inside a sub-group
   * with a different blend space to the page group. In this case we only
   * update the progress dial when doing the last colour conversion to device
   * space.
   */
  im_adj_data.update_progress =
    !guc_backdropRasterStyle(gsc_getTargetRS(colorInfo));

  /* There are three methods by which the image can be adjusted.
   * (1) We have independent channels ( e.g. CMYK -> CMYK ). Use an
   *     independent LUT. No new image stores are created. Instead,
   *     the original image stores are used and new image expanders are
   *     created to render the image.
   * (2) We do NOT have independent channels ( e.g. RGB -> CMYK ) but
   *     the size of the image is small enough to make it worth while
   *     using an interleaved LUT. A new image store is created by
   *     merging the image into a single plane of interleaved data
   *     and a new image expander is created to render the updated image
   *     object.
   * (3) Color convert the image. This method can be split into two cases.
   *     (a) We have independent channels. Separate image stores are created
   *         for the image objects containing the color converted
   *         planarized data and new image expanders are created to render
   *         the image objects.
   *     (b) We do NOT have independent channels. A single planarized image
   *         store is created by merging the color converted data for
   *         the image and a new image expander is created to render
   *         the updated image object.
   */

  /* Setup data structures needed to recombine the image. This also
   * decides which function callback to use.
   */
  if ( !im_adj_adjustimage_begin(imageobj, image_lobj, colorvalues,
                                 inputcolorants, colortype, &im_adj_data) )
    return im_adj_adjustimage_end(&im_adj_data, imageobj, FALSE);

  page->im_imagesleft += 1;

  /* Convert the image or set up the new LUT. */
  result = im_adj_data.im_adj_func(imageobj, image_lobj, inputcolorants,
                                   &im_adj_data);

  page->im_imagesleft -= 1;

  return im_adj_adjustimage_end(&im_adj_data, imageobj, result);
}

/* ---------------------------------------------------------------------- */
Bool im_adj_adjustimage(DL_STATE *page, Bool replace,
                        LISTOBJECT *image_lobj, USERVALUE *colorvalues,
                        COLORANTINDEX *inputcolorants, GS_COLORinfo *colorInfo,
                        int32 colortype, int32 method)
{
  IMAGEOBJECT *imageobj;

  HQASSERT(image_lobj != NULL, "image_lobj null");
  HQASSERT(colorvalues != NULL, "colorvalues null");
  HQASSERT(inputcolorants != NULL, "inputcolorants null");
  HQASSERT(!rcbn_intercepting(), "Recombine interception must be disabled");

  imageobj = image_lobj->dldata.image;

  HQASSERT(imageobj != NULL, "imageobj null");
  HQASSERT(imageobj->ims != NULL, "ims null");
  HQASSERT(imageobj->ime != NULL, "ime null");

  return im_adj_adjustimage_internal(page, replace, image_lobj,
                                     colorvalues, inputcolorants, colorInfo,
                                     colortype, method);
}

/* ---------------------------------------------------------------------- */

void init_C_globals_imageadj(void)
{
#if defined( ASSERT_BUILD )
  trace_adjImages = FALSE ;
#endif
}

/* Log stripped */
