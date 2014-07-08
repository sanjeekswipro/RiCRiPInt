/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:alphaDiv.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A somewhat unusual image filter that uses a single instance to process
 * image and mask data simulataneously, in order to undo a convolution of the
 * mask and image data (the matte color computation for soft masked images,
 * as described in the PDF 1.4 manual).
 */

#include "core.h"
#include "alphaDiv.h"

#include "fltrdimg.h"
#include "datatrns.h"
#include "objnamer.h"
#include "dictscan.h"
#include "namedef_.h"
#include "swerrors.h"
#include "hqmemset.h"

/* --Private types-- */

/* AlphaDiv image filter. */
typedef struct {
  Bool imagePrepared;
  Bool maskPrepared;
  Bool needImage;
  Bool needMask;
  Bool destroyed;

  uint32 width;
  uint32 height;
  uint32 colorantCount;
  uint32 imageSampleSize; /* 1 = 8-bit, 2 = 16-bit. */
  uint32 maskSampleSize;
  uint32 imageLineSize; /* Input line sizes in bytes. */
  uint32 maskLineSize;
  uint32 outputMaskLineSize;
  uint32 imageLinesProduced;
  uint32 maskLinesProduced;

  uint16* matteColor;
  DataTransaction* imageBuffer;
  DataTransaction* maskBuffer;
  DataTransaction* outputMask;
  OBJECT_NAME_MEMBER
} AlphaDiv;


/* --Private macros-- */

#define ALPHADIV_NAME "Imagefilter alpha div"

/* Min and max values of a color, as defined by the 8-bit or 16-bit containers
used to hold pixel data. */
#define COLOR_MIN 0u
#define COLOR_8_MAX 255u
#define COLOR_16_MAX 65535u

/* Limit the passed value to the range min <= value <= max. */
#define Limit(x_, min_, max_) \
  MACRO_START \
    if ((x_) > max_) \
      (x_) = max_; \
    if ((x_) < min_) \
      (x_) = min_; \
  MACRO_END

/* Divide two color values. The values are assumed to be fixed point
representations of values in the range 0 < x < max, where max is a fixed point
representation of 1.
*/
#define FixedDivide(a_, b_, max_) \
  (((b_) > 0) ? (((a_) * max_) / (b_)) : max_)

/* --Static data-- */

static AlphaDiv globalAlphaDiv = {0};

/* --Private prototypes-- */

Bool adPresent(FilteredImageArgs* image,
               Bool* accept,
               FilteredImageResult* changes);
LocalImageFilter adNew(FilteredImageArgs* image, FilteredImageResult* target);
void adDelete(LocalImageFilter pointer);
void adPushImage(LocalImageFilter pointer, DataTransaction* data);
void adPushMask(LocalImageFilter pointer, DataTransaction* data);
uint32 adPullImage(LocalImageFilter pointer, uint8** result);
uint32 adPullMask(LocalImageFilter pointer, uint8** result);


/* --Interface exposers-- */

/* Obtain an ImageFilter interface for the image filter.
*/
ImageFilter* alphaDivExposeImageFilter(void)
{
  ImageFilter* filter = imageFilterNew();

  if (filter == NULL)
    return NULL;

  filter->type = ImageFilterTypeLocal;
  filter->instance = NULL;

  filter->localFilter.presenter = adPresent;
  filter->localFilter.constructor = adNew;
  filter->localFilter.destructor = adDelete;
  filter->localFilter.push = adPushImage;
  filter->localFilter.pull = adPullImage;

  return filter;
}

/* Obtain an ImageFilter interface for the mask filter.
*/
ImageFilter* alphaDivExposeMaskFilter(void)
{
  ImageFilter* filter = imageFilterNew();

  if (filter == NULL)
    return NULL;

  filter->type = ImageFilterTypeLocal;
  filter->instance = NULL;

  filter->localFilter.presenter = adPresent;
  filter->localFilter.constructor = adNew;
  filter->localFilter.destructor = adDelete;
  filter->localFilter.push = adPushMask;
  filter->localFilter.pull = adPullMask;

  return filter;
}

/* --Private methods-- */

/* Presenter.
*/
Bool adPresent(FilteredImageArgs* image,
               Bool* accept,
               FilteredImageResult* changes)
{
  NAMETYPEMATCH match[] = {
    {NAME_Matted | OOPTIONAL, 1, {OBOOLEAN}}, DUMMY_END_MATCH};

  HQASSERT(image != NULL && accept != NULL,
           "adPresent - parameters cannot be NULL");
  UNUSED_PARAM(FilteredImageResult*, changes);

  /* Reject any image which does not have a dictionary (any such image cannot
  possibly be matted). */
  if (image->dictionary == NULL)
    return TRUE;

  if (image->bpc != 8 && image->bpc != 16)
    return TRUE;

  if (! dictmatch(image->dictionary, match))
    return FALSE;

  /* We accept all matte images/masks. */
  if (match[0].result != NULL)
    *accept = oBool(*match[0].result);
  else
    *accept = FALSE;

  return TRUE;
}

/* Constructor. Because this is a dual-filter (processes both image and mask
data using a single instance), this constructor will be called twice for any
given masked image, firstly for the image data, and then for the mask.
*/
LocalImageFilter adNew(FilteredImageArgs* image, FilteredImageResult* target)
{
  uint32 i;
  uint32 sampleSize;
  AlphaDiv* self = &globalAlphaDiv;

  HQASSERT(image->bpc == 8 || image->bpc == 16,
           "adNew - image does not match that passed to present method.");
  UNUSED_PARAM(FilteredImageResult*, target);

  self->destroyed = FALSE;
  sampleSize = image->bpc == 16 ? 2 : 1;

  switch (image->imageOrMask) {

    /* The FilteredImage object will always create image filters before mask
       filters, so we'll construct the global instance when the image is specified,
       and just make sure that the mask matches our expectations when it is
       specified. */
  case ImageDataSelector:
    HQASSERT(! self->imagePrepared,
             "adNew - image has already been prepared.");
    NAME_OBJECT(self, ALPHADIV_NAME);
    self->imagePrepared = TRUE;
    self->maskPrepared = FALSE;
    self->width = image->width;
    self->height = image->height;
    self->colorantCount = image->colorantCount;
    self->imageSampleSize = sampleSize;
    self->needImage = self->needMask = TRUE;
    self->imageLinesProduced = self->maskLinesProduced = 0;

    self->imageLineSize = self->width * self->colorantCount * sampleSize;
    self->imageBuffer = dataTransactionNewWithBuffer(self->imageLineSize);
    self->matteColor = NULL;
    break;

  case MaskDataSelector:
    HQASSERT(self->imagePrepared, "adNew - image not prepared.");
    HQASSERT(! self->maskPrepared, "adNew - mask has already been prepared.");
    HQASSERT(self->width == image->width && self->height == image->height,
             "adNew - mask does not match image dimensions.");

    self->maskPrepared = TRUE;
    self->maskSampleSize = sampleSize;
    /* The mask data will be resampled to match the image sample size. */
    self->maskLineSize = self->width * self->imageSampleSize;
    self->maskBuffer = dataTransactionNewWithBuffer(self->maskLineSize);

    /* The outputMask is the original mask data. */
    self->outputMaskLineSize = self->width * sampleSize;
    self->outputMask = dataTransactionNewWithBuffer(self->outputMaskLineSize);

    /* Create a list to hold a copy of the matte color. */
    self->matteColor = mm_alloc(mm_pool_temp,
                                sizeof(uint16) * self->colorantCount,
                                MM_ALLOC_CLASS_MATTE_COLOR);
    if ( self->matteColor == NULL ) {
      (void)error_handler(VMERROR) ;
      adDelete(self);
      return NULL ;
    }

    {
      OBJECT* list;
      NAMETYPEMATCH match[] = {
        {NAME_Matte | OOPTIONAL, 2, {OARRAY, OPACKEDARRAY}}, DUMMY_END_MATCH};

      if (! dictmatch(image->dictionary, match)) {
        adDelete(self);
        return NULL;
      }
      list = match[0].result;

      /* Does this list contain an entry for each colorant. */
      if (theLen(*list) < self->colorantCount) {
        (void)error_handler(RANGECHECK);
        adDelete(self);
        return NULL;
      }

      /* Read the matte color from the postscript object list, and convert to
         8-bit format. */
      for (i = 0; i < self->colorantCount; i ++) {
        double value ;
        if ( !object_get_numeric(&oArray(*list)[i], &value) ) {
          adDelete(self);
          return NULL;
        }

        /* Limit the color value to the range 0 <= value <= 1 */
        if (value > 1)
          value = 1;
        if (value < 0)
          value = 0;

        /* Convert the color value to a 16-bit value. */
        self->matteColor[i] = (uint16)(value * COLOR_16_MAX);
      }
    }
    break;

  default:
    HQFAIL("adNew - unrecognised data type.");
    return NULL;
  }

  return self;
}

/* Destructor.
*/
void adDelete(LocalImageFilter pointer)
{
  AlphaDiv* self = (AlphaDiv*)pointer;

  /* It's not an error to try free a null object */
  if (self == NULL)
    return;

  VERIFY_OBJECT(self, ALPHADIV_NAME);

  /* Because we are using a single instance for mask and image, this destructor
  will get called twice. Abort now if this is the second time (don't try to
  delete everything a second time). */
  if (self->destroyed)
    return;

  dataTransactionDelete(self->imageBuffer);
  dataTransactionDelete(self->maskBuffer);
  dataTransactionDelete(self->outputMask);
  if ( self->matteColor != NULL ) {
    mm_free(mm_pool_temp, self->matteColor, sizeof(uint16) * self->colorantCount) ;
    self->matteColor = NULL ;
  }

  /* Reset structure. */
  HqMemZero((uint8 *)self, sizeof(AlphaDiv));
  NAME_OBJECT(self, ALPHADIV_NAME);
  self->destroyed = TRUE;
}

/* Push a line of image data.
*/
void adPushImage(LocalImageFilter pointer, DataTransaction* data)
{
  AlphaDiv* self = (AlphaDiv*)pointer;

  VERIFY_OBJECT(self, ALPHADIV_NAME);
  HQASSERT(self->imagePrepared && self->maskPrepared,
           "adPushImage - filter not configured.");
  HQASSERT(data != NULL, "adPushImage - 'data' cannot be NULL.");

  if (! self->needImage ||
      dataTransactionAvailable(data) < self->imageLineSize)
    return;

  dataTransactionWrite(self->imageBuffer,
                       dataTransactionRead(data, self->imageLineSize),
                       self->imageLineSize);
  self->needImage = FALSE;
}

/* Push a line of mask data. Resample the mask data to match the image sample
size.
*/
void adPushMask(LocalImageFilter pointer, DataTransaction* data)
{
  AlphaDiv* self = (AlphaDiv*)pointer;
  uint32 inputSize;

  VERIFY_OBJECT(self, ALPHADIV_NAME);
  HQASSERT(data != NULL, "adPushMask - 'data' cannot be NULL.");
  HQASSERT(self->imagePrepared && self->maskPrepared,
           "adPushMask - filter not configured.");

  inputSize = self->width * self->maskSampleSize;
  if (! self->needMask || dataTransactionAvailable(data) < inputSize)
    return;

  /* The mask data may be resampled and therefore if preserving the mask the
     original data needs buffering for adPullMask. The read of '0' means the
     read head isn't advanced. */
  dataTransactionWrite(self->outputMask, dataTransactionRead(data, 0), inputSize);

  /* Resample the mask data to match the image data. */
  if (self->imageSampleSize == self->maskSampleSize) {
    HQASSERT(inputSize == self->maskLineSize, "adPushMask - size mismatch.");
    dataTransactionWrite(self->maskBuffer, dataTransactionRead(data, inputSize),
                         self->maskLineSize);
  }
  else {
    uint32 i;
    /* Note that at this point we know that the sample sizes are different. */
    if (self->imageSampleSize == 1) {
      uint16* maskData = (uint16*)dataTransactionRead(data, inputSize);
      uint8* maskBuffer = dataTransactionRead(self->maskBuffer, 0);

      HQASSERT(self->maskSampleSize == 2, "adPushMask - unexpected sample size.");
      /* Truncate the samples to 8-bit. */
      for (i = 0; i < self->width; i ++)
        maskBuffer[i] = (uint8)(maskData[i] >> 8);
    }
    else {
      uint8* maskData = dataTransactionRead(data, inputSize);
      uint16* maskBuffer = (uint16*)dataTransactionRead(self->maskBuffer, 0);

      HQASSERT(self->maskSampleSize == 1, "adPushMask - unexpected sample size.");
      /* Boost the samples up to 16-bit. */
      for (i = 0; i < self->width; i ++)
        maskBuffer[i] = (uint16)((maskData[i] << 8) | maskData[i]);
    }
    dataTransactionFakeWrite(self->maskBuffer, self->maskLineSize);
  }
  self->needMask = FALSE;
}

/* Extract a line of image data. This data will have had the matte color
operation undone (i.e. the data will be pure image data).
*/
uint32 adPullImage(LocalImageFilter pointer, uint8** result)
{
  uint32 colorant;
  uint32 i;
  double color;
  AlphaDiv* self = (AlphaDiv*)pointer;

  VERIFY_OBJECT(self, ALPHADIV_NAME);
  HQASSERT(result != NULL, "adPullImage - 'result' cannot be NULL");

  /* We can only return data if we have the image and mask ready, and there
  is data left to produce. */
  if (self->needImage || self->needMask ||
      self->imageLinesProduced >= self->height) {
    *result = NULL;
    return 0;
  }

  self->imageLinesProduced ++;

  /* Iterate over all colorants. */
  if (self->imageSampleSize == 1) {
    /* 8-bit image/mask data. */
    uint8 *pixelBase, *pixels, *mask;
    double matteShade;

    /* Get hold of the pixel and mask color lists. */
    *result = pixelBase = dataTransactionRead(self->imageBuffer,
                                              self->imageLineSize);
    mask = dataTransactionRead(self->maskBuffer, self->maskLineSize);

    for (colorant = 0; colorant < self->colorantCount; colorant ++) {
      pixels = pixelBase + colorant;
      matteShade = (uint8)(self->matteColor[colorant] >> 8);

      /* Iterate over each pixel of this colorant. */
      for (i = 0; i < self->width; i ++) {
        /* Undo the matte color computation as defined by the PDF 1.4 spec. */
        color = matteShade + FixedDivide((*pixels - matteShade), mask[i],
                                         COLOR_8_MAX);
        Limit(color, COLOR_MIN, COLOR_8_MAX);
        *pixels = (uint8)color;

        /* Skip on to the next pixel in this colorant. */
        pixels += self->colorantCount;
      }
    }
  }
  else {
    /* 16-bit image/mask data. */
    uint16 *pixelBase, *pixels, *mask;
    double matteShade;

    /* Get hold of the pixel and mask color lists. */
    pixelBase = (uint16*)dataTransactionRead(self->imageBuffer,
                                             self->imageLineSize);
    *result = (uint8*)pixelBase;
    mask = (uint16*)dataTransactionRead(self->maskBuffer, self->maskLineSize);

    for (colorant = 0; colorant < self->colorantCount; colorant ++) {
      pixels = pixelBase + colorant;
      matteShade = self->matteColor[colorant];

      /* Iterate over each pixel of this colorant. */
      for (i = 0; i < self->width; i ++) {
        /* Undo the matte color computation as defined by the PDF 1.4 spec. */
        color = matteShade + FixedDivide((*pixels - matteShade), mask[i],
                                         COLOR_16_MAX);
        Limit(color, COLOR_MIN, COLOR_16_MAX);
        *pixels = (uint16)color;

        /* Skip on to the next pixel in this colorant. */
        pixels += self->colorantCount;
      }
    }
  }

  dataTransactionReset(self->imageBuffer);
  dataTransactionReset(self->maskBuffer);

  self->needImage = self->needMask = TRUE;
  return self->imageLineSize;
}

/* Just return a line of solid 1-bit mask.
*/
uint32 adPullMask(LocalImageFilter pointer, uint8** result)
{
  AlphaDiv* self = (AlphaDiv*)pointer;

  VERIFY_OBJECT(self, ALPHADIV_NAME);
  HQASSERT(result != NULL, "adPullMask - result cannot be NULL");

  if (self->maskLinesProduced < self->height) {
    self->maskLinesProduced ++;
    /* The original mask data was copied into outputMask, and now it's just
       passed on. */
    *result = dataTransactionRead(self->outputMask, self->outputMaskLineSize);
    dataTransactionReset(self->outputMask);
    return self->outputMaskLineSize;
  }
  else {
    *result = NULL;
    return 0;
  }
}

/* Log stripped */
