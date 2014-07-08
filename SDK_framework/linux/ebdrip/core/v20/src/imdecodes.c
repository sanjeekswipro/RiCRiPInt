/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:imdecodes.c(EBDSDK_P.1) $
 * $Id: src:imdecodes.c,v 1.9.2.1.1.1 2013/12/19 11:25:28 anon Exp $
 *
 * Copyright (C) 2009-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image decodes arrays map image data to input color values.
 *
 * Image decodes are cached (only most recently used) for a particular
 * set of criteria (e.g. bits per component and number of components).
 * It's easy to add cache slots for more combinations if a particular
 * jobs demands it, just add to the enum and matching slot macro.
 *
 * Note, the cached entries are only freed when they are replaced by another
 * entry.
 */

#include "core.h"
#include "imdecodes.h"
#include "dlstate.h"
#include "gschead.h"
#include "display.h"
#include "swerrors.h"
#include "control.h"
#include "constant.h"
#include "gs_table.h"
#include "imageo.h"
#include "imexpand.h" /* im_expand_decode_array et. al. */

typedef struct im_decodesobj_t {
  /* For both front and back-end conversion. */
  int32 bpp, ncomps, scaledcolor;
  float *range;

  /* For front-end conversion. */
  float *decode, *dmin, *dfac;

  /* For back-end conversion. */
  void *lut;

  /* Cached fdecodes and normalised decodes arrays. */
  float **fdecodes;
  int32 **ndecodes;

} im_decodesobj_t;

static int32 im_decodesobj_cache_slot(IMAGEARGS *imageargs)
{
  switch ( imageargs->imagetype ) {
  case TypeImageMask :
  case TypeImageMaskedMask :
    return DECODESOBJ_SLOT_MASK;

  case TypeImageAlphaAlpha :
    return DECODESOBJ_SLOT_ALPHA;

  case TypeImageImage :
  case TypeImageMaskedImage :
  case TypeImageAlphaImage :
    switch ( imageargs->bits_per_comp ) {
    case 1 :
      return DECODESOBJ_SLOT_1BPP;
    case 8 :
      if ( imageargs->ncomps == 3 )
        return DECODESOBJ_SLOT_8BPP_3NCOMPS;
      else
        return DECODESOBJ_SLOT_8BPP;
    }
    return DECODESOBJ_SLOT_DEFAULT;

  default :
    HQFAIL("Unexpected image type in image decode cache");
    return DECODESOBJ_SLOT_DEFAULT;
  }
}

static Bool fdecodes_range(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                           int32 i, float range[2])
{
  /* Get the range of values (via range[0] & range[1]) that colors in the
     current color space may accept (nb: usually this range is 0..1.) */
  if ( imageargs->imagetype != TypeImageAlphaAlpha ) {
    SYSTEMVALUE s_range[2];

    if ( !gsc_range(imagedata->colorInfo, imageargs->colorType, i,
                    s_range) )
      return FALSE;

    range[0] = (float)s_range[0];
    range[1] = (float)s_range[1];
  }
  else {
    /* Alpha channel always uses 0-1 range. */
    range[0] = 0;
    range[1] = 1;
  }

  return TRUE;
}

/* Looks for a suitable decodes object in the cache. */
static im_decodesobj_t* im_lookup_decodesobj(IMAGEARGS *imageargs,
                                             IMAGEDATA *imagedata,
                                             void *lut,
                                             int32 *slot_addr)
{
  im_decodesobj_t *decodesobj;
  int32 slot, i, ncomps = imageargs->ncomps;
  float range[2];

  slot = im_decodesobj_cache_slot(imageargs);
  if ( slot_addr ) *slot_addr = slot;

  decodesobj = imagedata->page->decodesobj[slot];

  if ( !decodesobj ||
       !decodesobj->fdecodes ||
       decodesobj->bpp != imageargs->bits_per_comp ||
       decodesobj->ncomps != imageargs->ncomps ||
       decodesobj->scaledcolor != gsc_scaledColor(imagedata->page) ||
       (decodesobj->decode != NULL) ^ (imageargs->decode != NULL) ||
       decodesobj->lut != lut )
    return NULL;

  for ( i = 0; i < ncomps; ++i ) {
    if ( (decodesobj->decode &&
          (decodesobj->decode[i*2] != imageargs->decode[i*2] ||
           decodesobj->decode[i*2+1] != imageargs->decode[i*2+1])) ||
         !fdecodes_range(imageargs, imagedata, i, range) ||
         decodesobj->range[i*2] != range[0] ||
         decodesobj->range[i*2+1] != range[1] )
      return NULL;
  }

  return decodesobj;
}

static void im_free_fdecodes(im_decodesobj_t *decodesobj)
{
  float **fdecodes = decodesobj->fdecodes;

  if ( fdecodes ) {
    int32 i, ncomps = decodesobj->ncomps;
    int32 bytes = ((1 << decodesobj->bpp) + 1) * sizeof(float);

    for ( i = 0; i < ncomps; ++i ) {
      float *fdecode = fdecodes[i];

      if ( fdecode ) {
        int32 j;

        /* Check to see if we've got any duplicates. */
        for ( j = i + 1; j < ncomps; ++j )
          if ( fdecode == fdecodes[j] )
            fdecodes[j] = NULL;

        mm_free(mm_pool_temp, (mm_addr_t)fdecode, bytes);
      }
    }
    mm_free(mm_pool_temp, (mm_addr_t)decodesobj->fdecodes,
            ncomps * sizeof(float *));
  }
}

static void im_free_ndecodes(im_decodesobj_t *decodesobj)
{
  int32 **ndecodes = decodesobj->ndecodes;

  if ( ndecodes ) {
    int32 i, ncomps = decodesobj->ncomps, s = (1 << decodesobj->bpp);

    for ( i = 0; i < ncomps; ++i ) {
      int32 *ndecode = ndecodes[i];

      if ( ndecode ) {
        int32 j;

        /* Check to see if we've got any duplicates. */
        for ( j = i + 1; j < ncomps; ++j )
          if ( ndecode == ndecodes[j] )
            ndecodes[j] = NULL;

        mm_free(mm_pool_temp, (mm_addr_t)ndecode, (s + 1) * sizeof(int32));
      }
    }
    mm_free(mm_pool_temp, (mm_addr_t)ndecodes,
            decodesobj->ncomps * sizeof(int32 *));
  }
}

static void im_free_decodesobj(im_decodesobj_t **decodesobj_addr)
{
  im_decodesobj_t *decodesobj = *decodesobj_addr;

  if ( decodesobj ) {
    if ( decodesobj->decode )
      mm_free(mm_pool_temp, (mm_addr_t)decodesobj->decode,
              4 * decodesobj->ncomps * sizeof(float));

    if ( decodesobj->range )
      mm_free(mm_pool_temp, (mm_addr_t)decodesobj->range,
              2 * decodesobj->ncomps * sizeof(float));

    im_free_fdecodes(decodesobj);
    im_free_ndecodes(decodesobj);

    mm_free(mm_pool_temp, (mm_addr_t)decodesobj,
            sizeof(im_decodesobj_t));

    *decodesobj_addr = NULL;
  }
}

static Bool im_alloc_decodesobj(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                                void *lut,
                                im_decodesobj_t **decodesobj_addr)
{
  im_decodesobj_t *decodesobj;
  static im_decodesobj_t decodesobj_init = {0};
  int32 i, ncomps = imageargs->ncomps;
  float bitfac;

  *decodesobj_addr = NULL;

  decodesobj = mm_alloc(mm_pool_temp, sizeof(im_decodesobj_t),
                        MM_ALLOC_CLASS_IMAGE_DECODES);
  if ( !decodesobj )
    return error_handler(VMERROR);

  *decodesobj = decodesobj_init;
  decodesobj->bpp = imageargs->bits_per_comp;
  decodesobj->ncomps = imageargs->ncomps;
  decodesobj->scaledcolor = gsc_scaledColor(imagedata->page);

  decodesobj->range = mm_alloc(mm_pool_temp,
                               2 * ncomps * sizeof(float),
                               MM_ALLOC_CLASS_IMAGE_DECODES);
  if ( !decodesobj->range ) {
    im_free_decodesobj(&decodesobj);
    return error_handler(VMERROR);
  }

  for ( i = 0 ; i < ncomps; ++i ) {
    if ( !fdecodes_range(imageargs, imagedata, i, &decodesobj->range[i*2]) ) {
      im_free_decodesobj(&decodesobj);
      return FALSE;
    }
  }

  if ( imageargs->decode ) {
    /* A front-end image decodes array object must store the image dictionary
       decode array and make dmin/dfac arrays.  These are used to generate the
       fdecodes array and also as a key for the cache. */
    decodesobj->decode = mm_alloc(mm_pool_temp,
                                  4 * ncomps * sizeof(float),
                                  MM_ALLOC_CLASS_IMAGE_DECODES);
    if ( !decodesobj->decode ) {
      im_free_decodesobj(&decodesobj);
      return error_handler(VMERROR);
    }

    decodesobj->dmin = decodesobj->decode + (2 * ncomps);
    decodesobj->dfac = decodesobj->dmin + ncomps;

    /* Allow bpp 32 as a debuging option : stop this calculation blowing-up */
    HQASSERT(decodesobj->bpp > 0 && decodesobj->bpp <= 32,
             "unexpected bits/pixel value");
    if ( decodesobj->bpp == 32 )
      bitfac = 1.0f / 4294967295.0f;
    else
      bitfac = 1.0f / (( 1 << decodesobj->bpp ) - 1.0f);

    for ( i = 0 ; i < ncomps; ++i ) {
      float dmin = imageargs->decode[i*2];

      decodesobj->decode[i*2] = imageargs->decode[i*2];
      decodesobj->decode[i*2+1] = imageargs->decode[i*2+1];

      /* For each colorant, provides values for the dmin and dfac arrays used to
         perform the calculations required by the Decode elements of the image
         dictionary.  See PLRM 3, section 4.10.5 for more info. */
      decodesobj->dmin[i] = dmin;
      decodesobj->dfac[i] = bitfac * ( imageargs->decode[i*2+1] - dmin );
    }
  }
  else {
    /* A back-end image decodes array object.  The image LUT is used to
       generate the fdecodes array and also as a key for the cache. */
    decodesobj->lut = lut;
  }

  *decodesobj_addr = decodesobj;

  return TRUE;
}

/**
   The image dictionary's source /Decode [] arrays provide a linear mapping from
   image sample values to current color space's color values.  To speed up this
   translation, a look-up table is created to map each possible sample value to
   an output value described in the PLRM.3.  Similar routine below for the
   recombine case.
 */
Bool im_alloc_fdecodes(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  int32 i, s, ncomps;
  float **fdecodes;
  im_decodesobj_t *decodesobj;
  int32 slot;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");
  HQASSERT(imagedata->u.fdecodes == NULL,
           "Should not have already allocated fdecodes");

  /* Check the fdecodes cache for a matching fdecodes. */
  decodesobj = im_lookup_decodesobj(imageargs, imagedata, NULL, &slot);
  if ( decodesobj ) {
    /* Transfer ownership from cache to IMAGEDATA.  This allows more
       decodesobj to be created if we end up partial painting whilst
       interpreting this image. */
    imagedata->page->decodesobj[slot] = NULL;
    imagedata->decodesobj = decodesobj;
    imagedata->u.fdecodes = decodesobj->fdecodes;
    return TRUE;
  }

  if ( !im_alloc_decodesobj(imageargs, imagedata, NULL, &decodesobj) )
    return FALSE;

  /* Now generate the fdecodes array itself. */

  ncomps = imageargs->ncomps;
  s = 1 << imageargs->bits_per_comp;

  fdecodes = decodesobj->fdecodes =
    mm_alloc(mm_pool_temp, ncomps * sizeof(float *),
             MM_ALLOC_CLASS_IMAGE_DECODES);
  if ( !fdecodes )
    return error_handler(VMERROR);

  /* Zero these so that the free will work correctly. */
  for ( i = 0; i < ncomps; ++i )
    fdecodes[i] = NULL;

  for ( i = 0; i < ncomps; ++i ) {   /* for each colorant */
    float *range = &decodesobj->range[i*2];
    float dmin = decodesobj->dmin[i];
    float dfac = decodesobj->dfac[i];
    int32 j;

    /* Check to see if we can find a duplicate decode array already
       allocated for one of the other colorants. If so, share it. */
    for ( j = 0; j < i; ++j )
      if ( dmin == decodesobj->dmin[j] && dfac == decodesobj->dfac[j] ) {
        fdecodes[i] = fdecodes[j];
        break;
      }

    /* If we didn't find one then create a new one. Note size is 1 larger since
       we store a value at the end that guarantees to map to white. */
    if ( fdecodes[i] == NULL ) {
      float *fdecode;

      fdecode = fdecodes[i] =
        mm_alloc(mm_pool_temp, (s + 1) * sizeof(float),
                 MM_ALLOC_CLASS_IMAGE_DECODES);
      if ( !fdecode ) {
        im_free_decodesobj(&imagedata->page->decodesobj[slot]);
        return error_handler(VMERROR);
      }

      for ( j = 0; j < s; ++j ) {
        float tmp = dmin + j * dfac;
        if ( tmp < range[0] )
          tmp = range[0];
        else if ( tmp > range[1] )
          tmp = range[1];
        *fdecode++ = tmp;
      }
      /* Store the single value at the end that guarantees to map to white. */
      *fdecode = 0.0f;
    }
  }

  imagedata->decodesobj = decodesobj;
  imagedata->u.fdecodes = fdecodes;

  return TRUE;
}

/**
   This routine creates a set of arrays (one for each colorant) parallel to the
   Decode arrays. They are, apparently, "normalised" and used for color
   conversion.  The normalised decodes arrays are generated from the fdecodes
   arrays therefore a decodes object must exist already.
 */
Bool im_alloc_ndecodes(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  im_decodesobj_t *decodesobj;
  int32 i, s, ncomps;
  int32 **ndecodes;
  float **fdecodes;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");

  decodesobj = imagedata->decodesobj;
  if ( !decodesobj )
    return error_handler(UNREGISTERED);

  if ( decodesobj->ndecodes ) {
    imagedata->ndecodes = decodesobj->ndecodes;
    return TRUE;
  }

  HQASSERT(imagedata->u.fdecodes != NULL,
           "Should have already allocated fdecodes");
  HQASSERT(imagedata->u.fdecodes == decodesobj->fdecodes,
           "fdecodes should match the one in the decodesobj");
  fdecodes = imagedata->u.fdecodes;

  ncomps = imageargs->ncomps;
  s = 1 << imageargs->bits_per_comp;

  ndecodes = decodesobj->ndecodes =
    mm_alloc(mm_pool_temp, ncomps * sizeof(int32 *),
             MM_ALLOC_CLASS_IMAGE_DECODES);
  if ( !ndecodes )
    return error_handler(VMERROR);

  /* Zero these so that the free will work correctly. */
  for ( i = 0; i < ncomps; ++i )
    ndecodes[i] = NULL;

  for ( i = 0; i < ncomps; ++i ) {
    float *range = &decodesobj->range[i*2];
    int32 j;
    float *fdecode = fdecodes[i];

    /* Check to see if we can find a duplicate. */
    for ( j = 0; j < i; ++j ) {
      if ( fdecode == fdecodes[j] ) {
        float *trange = &decodesobj->range[j*2];
        if ( range[0] == trange[0] &&
             range[1] == trange[1] ) {
          ndecodes[i] = ndecodes[j];
          break;
        }
      }
    }
    /* If we didn't find one then create a new one. */
    if ( ndecodes[i] == NULL ) {
      float range0;
      float scalef;
      int32 *ndecode;

      ndecode = ndecodes[i] =
        mm_alloc(mm_pool_temp, (s + 1) * sizeof(int32),
                 MM_ALLOC_CLASS_IMAGE_DECODES);
      if ( !ndecode ) {
        im_free_ndecodes(decodesobj);
        return error_handler(VMERROR);
      }

      range0 = range[0];
      if (fabs(range[1] - range[0]) <= EPSILON * fabs(range[1] + range[0])) {
        /* This can happen for Indexed colorspaces with just one entry */
        HQASSERT(range[1] == range[0], "Colorspace range suspicious");
        scalef = 0.0;
      }
      else
        scalef = decodesobj->scaledcolor / (range[1] - range0);

      /* Don't forget to modify the value at the end of the decode array. */
      for ( j = 0; j < s + 1; ++j ) {
        float tmp = fdecode[j];
        int32 val = ( int32 )(scalef * ( tmp - range0 ) + 0.5f);
        HQASSERT(val >= 0 && val <= decodesobj->scaledcolor, "val out of range");
        ndecode[j] = val;
      }
    }
  }

  imagedata->ndecodes = ndecodes;

  return TRUE;
}

Bool im_alloc_adjdecodes(IMAGEARGS *imageargs,
                         IMAGEDATA *imagedata,
                         IMAGEOBJECT *imageobj,
                         USERVALUE *colorvalues,
                         COLORANTINDEX *inputcolorants,
                         Bool subtractive)
{
  int32 i, s, ncomps;
  void *lut;
  float **fdecodes;
  im_decodesobj_t *decodesobj;
  int32 slot;

  HQASSERT(imageargs, "imageargs NULL");
  HQASSERT(imagedata, "imagedata NULL");
  HQASSERT(imagedata->u.fdecodes == NULL,
           "Should not have already allocated fdecodes");
  HQASSERT(imageobj, "imageobj NULL");
  HQASSERT(colorvalues, "colorvalues NULL");
  HQASSERT(inputcolorants, "inputcolorants NULL");

  /* Non-LUT backend images use the backdrop decodes which are
     handled separately. */
  lut = im_expand_decode_array(imageobj->ime);
  HQASSERT(lut, "Must have a LUT-type image in this case");

  /* Check the fdecodes cache for a matching fdecodes. */
  decodesobj = im_lookup_decodesobj(imageargs, imagedata, lut, &slot);
  if ( decodesobj ) {
    /* Transfer ownership from cache to IMAGEDATA.  This allows more
       decodesobj to be created if we end up partial painting whilst
       interpreting this image. */
    imagedata->page->decodesobj[slot] = NULL;
    imagedata->decodesobj = decodesobj;
    imagedata->u.fdecodes = decodesobj->fdecodes;
    return TRUE;
  }

  if ( !im_alloc_decodesobj(imageargs, imagedata, lut, &decodesobj) )
    return FALSE;

  /* Now generate the fdecodes array itself. */

  ncomps = imageargs->ncomps;
  s = 1 << imageargs->bits_per_comp;

  fdecodes = decodesobj->fdecodes =
    mm_alloc(mm_pool_temp, ncomps * sizeof(float *),
             MM_ALLOC_CLASS_IMAGE_DECODES);
  if ( !fdecodes )
    return error_handler(VMERROR);

  /* Zero these so that the free will work correctly. */
  for ( i = 0; i < ncomps; ++i )
    fdecodes[ i ] = NULL;

  for ( i = 0; i < ncomps; ++i ) {
    COLORANTINDEX ci = inputcolorants[i];
    float *fdecode;

    /* Note that size is 1 larger since we store a value at the end that
       guarantees to map to white. */
    fdecode = fdecodes[i] =
      mm_alloc(mm_pool_temp, (s + 1) * sizeof(float),
               MM_ALLOC_CLASS_IMAGE_DECODES);
    if ( !fdecode ) {
      im_free_decodesobj(&imagedata->page->decodesobj[slot]);
      return error_handler(VMERROR);
    }

    if ( ci != COLORANTINDEX_NONE &&
         im_expandlutexists(imageobj->ime, i) ) {
      /* Read in the decodes array from the lut table. */
      im_expandpresepread(imageobj->ime, subtractive, fdecode, i);
      fdecode += s;
    }
    else {
      int32 j;
      float decval = colorvalues[i];
      for ( j = 0; j < s; ++j )
        *fdecode++ = decval;
    }
    /* Store the single value at the end that guarantees to map to white. */
    *fdecode = 1.0f; /* additive */
  }

  imagedata->decodesobj = decodesobj;
  imagedata->u.fdecodes = fdecodes;

  return TRUE;
}

float* im_fdecodes_dfac(IMAGEDATA *imagedata)
{
  return imagedata->decodesobj ? imagedata->decodesobj->dfac : NULL;
}

void im_free_decodes(IMAGEARGS *imageargs, IMAGEDATA *imagedata)
{
  if ( imagedata->decodesobj ) {
    int32 slot = im_decodesobj_cache_slot(imageargs);

    if ( imagedata->page->decodesobj[slot] != imagedata->decodesobj ) {
      /* Put the decodes object in the cache, replacing the existing entry. */
      im_free_decodesobj(&imagedata->page->decodesobj[slot]);
      imagedata->page->decodesobj[slot] = imagedata->decodesobj;
    }

    imagedata->decodesobj = NULL;
  }

  imagedata->u.fdecodes = NULL;
  imagedata->ndecodes = NULL;
}

void im_reset_decodes(DL_STATE *page)
{
  int32 i;

  for ( i = 0; i < DECODESOBJ_NUM_SLOTS; ++i ) {
    im_free_decodesobj(&page->decodesobj[i]);
  }
}

/* =============================================================================
* Log stripped */
