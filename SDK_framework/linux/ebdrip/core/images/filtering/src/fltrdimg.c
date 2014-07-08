/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:fltrdimg.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Entrypoint into the image filtering system
 */

#include "core.h"
#include "fltrdimg.h"

#include "mm.h"
#include "objects.h"
#include "swerrors.h"
#include "swpdfout.h"
#include "objnamer.h"
#include "datatrns.h"
#include "pixlpckr.h"
#include "imfltchn.h"
#include "dataprpr.h"
#include "graphics.h"
#include "gstate.h"
#include "dl_color.h"
#include "dl_image.h"
#include "images.h"
#include "dlstate.h" /* inputpage */
#include "psvm.h"
#include "params.h"
#include "color.h" /* ht_is16bitLevels */
#include "gschtone.h"
#include "hqbitops.h"
#include "metrics.h"
#include "swstart.h"


/* --Private macros-- */

#define FILTEREDIMAGE_NAME "Imagefilter entrypoint"

/* --Private datatypes-- */

typedef enum {
  ProducingImage = 1,
  ProducingMask
} ProductionState;

struct FilteredImage_s {
  uint32 bytesInRepackLine;
  uint32 pureMask;
  uint32 productionState;
  size_t swapBufferSize;
  uint8 *byteSwapBuffer;
  DataTransaction *imageLine;
  DataTransaction *maskLine;
  Bool imagePresent;
  Bool imageFiltered;
  Bool maskPresent;
  Bool maskFiltered;
  FilteredImageArgs imageArgs;
  FilteredImageArgs filteredImageArgs;
  FilteredImageArgs maskArgs;
  FilteredImageArgs filteredMaskArgs;
  DataPreparer *dataPreparer;
  PixelPacker *pixelPacker;
  PixelPackFunction repackFunc;
  OBJECT_NAME_MEMBER
};

/* --Private prototypes-- */

static Bool shouldDisableInterpolation(Bool coerceToFillFlag);
static Bool checkForEarlyExit(IMAGEARGS* source,
                              uint32 deviceAccuracy,
                              Bool* exitEarly);
static Bool checkForBadSizes(IMAGEARGS* arguments);
static uint32 getDeviceAccuracy(DL_STATE *page);
static Bool allocateByteSwapBuffer(FilteredImage* self);
static void commitFilterChainParameterChanges(FilteredImageArgs* preparedArgs,
                                              IMAGEARGS* arguments);
static Bool passDataThruFilters(FilteredImage* self,
                                uint8** result,
                                uint32* resultLength);

#ifdef METRICS_BUILD
/**
 * Image sampling stats; records the number of images which within a rangle of
 * sampling densities. The upper range for each is exclusive.
 * The notSquare count is used for all images which have differing horizontal
 * and vertical sampling.
 */
static struct fltrdimg_metrics_t {
  uint32 sampled0To95Percent;
  uint32 sampled95To105Percent;
  uint32 sampled105To200Percent;
  uint32 sampledAtOrAbove200Percent;
  uint32 notSquare;
} fltrdimg_metrics ;

static Bool fltrdimg_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Images")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Sampling")) )
    return FALSE ;
  SW_METRIC_INTEGER("Ratio0to95Percent", fltrdimg_metrics.sampled0To95Percent);
  SW_METRIC_INTEGER("Ratio95to105Percent", fltrdimg_metrics.sampled95To105Percent);
  SW_METRIC_INTEGER("Ratio105to200Percent", fltrdimg_metrics.sampled105To200Percent);
  SW_METRIC_INTEGER("Ratio200PercentOrMore", fltrdimg_metrics.sampledAtOrAbove200Percent);
  SW_METRIC_INTEGER("notSquare", fltrdimg_metrics.notSquare);
  sw_metrics_close_group(&metrics) ; /*Sampling*/
  sw_metrics_close_group(&metrics) ; /*Images*/

  return TRUE ;
}

static void fltrdimg_metrics_reset(int reason)
{
  struct fltrdimg_metrics_t init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  fltrdimg_metrics = init ;
}

static sw_metrics_callbacks fltrdimg_metrics_hook = {
  fltrdimg_metrics_update,
  fltrdimg_metrics_reset,
  NULL
} ;

typedef enum {
  Sampled0To95Percent,
  Sampled95To105Percent,
  Sampled105To200Percent,
  SampledAtOrAbove200Percent,
} SamplingRange;

/**
 * Return a value indicating the sampling range for the passed size and size
 * on the device.
 */
static SamplingRange getSamplingRange(int32 size, int32 sizeOnDevice)
{
  double percentage;

  if (sizeOnDevice == 0) {
    sizeOnDevice = 1;
  }

  percentage = (size / (double)sizeOnDevice) * 100;

  if (percentage < 95) {
    return Sampled0To95Percent;
  }
  else {
    /* Greater than or equal to 99% */
    if (percentage < 105) {
      return Sampled95To105Percent;
    }
    else {
      /* Greater than or equal to 101% */
      if (percentage < 200) {
        return Sampled105To200Percent;
      }
      else {
        /* Greater than or equal to 200% */
        return SampledAtOrAbove200Percent;
      }
    }
  }
}

/* See header for doc. */
static void updateImageSamplingMetrics(int32 width, int32 height,
                                       int32 widthOnDevice, int32 heightOnDevice)
{
  SamplingRange x = getSamplingRange(width, widthOnDevice);
  SamplingRange y = getSamplingRange(height, heightOnDevice);

  if (x != y) {
    fltrdimg_metrics.notSquare ++;
  }
  else {
    switch (x) {
    case Sampled0To95Percent:
      fltrdimg_metrics.sampled0To95Percent ++;
      break;

    case Sampled95To105Percent:
      fltrdimg_metrics.sampled95To105Percent ++;
      break;

    case Sampled105To200Percent:
      fltrdimg_metrics.sampled105To200Percent ++;
      break;

    case SampledAtOrAbove200Percent:
      fltrdimg_metrics.sampledAtOrAbove200Percent ++;
      break;
    }
  }
}

#endif

/* --Public methods-- */

/* Constructor. See fltrdimg.h for client usage documentation.
 */
Bool filteredImageNew(DL_STATE *page, IMAGEARGS *arguments)
{
  Bool success = TRUE;
  Bool exitEarly;
  uint32 deviceAccuracy;
  uint32 totalFilters;
  DataPreparer *dataPreparer;
  FilteredImage *self;
  FilteredImageArgs *iA;
  FilteredImageArgs *mA;
  ImageFilterChain *imageChain;
  ImageFilterChain *maskChain;

  HQASSERT(arguments != NULL, "filteredImageNew - Passed IMAGEARGS is NULL");

  /* Initialise FilteredImage element of args to NULL */
  arguments->filteredImage = NULL;

  /* If the image is a bad size or not supported by the data preparer,
  abort (but not erroneously - we'll just not filter this image). */
  if (checkForBadSizes(arguments) || !dataPreparerSupported(arguments))
    return TRUE;

  if (!imageFilterChainImage(&imageChain))
    return FALSE;
  if (!imageFilterChainMask(&maskChain))
    return FALSE;

  deviceAccuracy = getDeviceAccuracy(page);
  if (! checkForEarlyExit(arguments, deviceAccuracy, &exitEarly))
    return FALSE;
  if (exitEarly)
    return TRUE;

  /* Create the data preparer */
  dataPreparer = dataPreparerNew(arguments, deviceAccuracy);
  if (dataPreparer == NULL) {
    return FALSE;
  }

  self = (FilteredImage*) mm_alloc(mm_pool_temp, sizeof(FilteredImage),
                                   MM_ALLOC_CLASS_FILTERED_IMAGE);
  if (self == NULL) {
    dataPreparerDelete(dataPreparer);
    (void)error_handler(VMERROR);
    return FALSE;
  }
  NAME_OBJECT(self, FILTEREDIMAGE_NAME);

  /* NULL pointer members */
  self->byteSwapBuffer = NULL;
  self->imageLine = NULL;
  self->maskLine = NULL;
  self->dataPreparer = NULL;
  self->pixelPacker = NULL;

  self->imageFiltered = FALSE;
  self->maskFiltered = FALSE;

  self->dataPreparer = dataPreparer;
  self->pureMask = dataPreparerPureMask(self->dataPreparer);

  self->imagePresent = dataPreparerGetPreparedImageArgs(self->dataPreparer,
                                                        &self->imageArgs);
  self->maskPresent = dataPreparerGetPreparedMaskArgs(self->dataPreparer,
                                                      &self->maskArgs);

  /* Copy prepared args into filtered args - this arg set will be modified
     as it passed through the filter chain */
  self->filteredImageArgs = self->imageArgs;
  self->filteredMaskArgs = self->maskArgs;
  iA = &self->filteredImageArgs;
  mA = &self->filteredMaskArgs;

  HQASSERT(self->imagePresent || self->maskPresent,
           "filteredImageNew - neither image or mask is present");

  /* Get hold of the prepared data transactions */
  self->imageLine = dataPreparerGetImage(self->dataPreparer);
  self->maskLine = dataPreparerGetMask(self->dataPreparer);

  /* Present the image to the image filter chain.
  NOTE: Order is important - the FilteredImage object documentation (in
  fltrdimg.h) states that images will always be presented to the filter chain
  before any associated masks. */
  if (self->imagePresent) {
    if (imageChain != NULL) {
      if (! imageFilterChainNewImage(imageChain, iA,
              shouldDisableInterpolation(iA->coerceToFillFlag), &totalFilters))
        success = FALSE;
      else
        self->imageFiltered = (totalFilters > 0);
    }
  }

  /* Present the mask to the mask filter chain */
  if (success && self->maskPresent) {
    if (maskChain != NULL) {
      if (! imageFilterChainNewImage(maskChain, mA,
              shouldDisableInterpolation(mA->coerceToFillFlag), &totalFilters))
        success = FALSE;
      else
        self->maskFiltered = (totalFilters > 0);
    }
  }

  /* Is any filtering going to take place? */
  if (success && (self->imageFiltered || self->maskFiltered)) {
    if (allocateByteSwapBuffer(self)) {
      /*Allocation succeeded - final commit phase*/

      /* Commit data preparer changes (changes to the presented data format).
         This may fail, so need to do it first of all, to avoid partial
         commit */
      if (dataPreparerCommitParameterChanges(self->dataPreparer)) {
        /* Commit Image parameter changes */
        if (self->imageFiltered) {
          commitFilterChainParameterChanges(iA, arguments);
          imageFilterChainSetDataSource(imageChain, self->imageLine);
        }

        /* Commit Mask parameter changes */
        if (self->maskFiltered) {
          IMAGEARGS *maskArgs;

          /* If this image is mask only, make the parameter changes to
             "arguments", rather than "arguments->maskargs" */
          if (self->pureMask) {
            maskArgs = arguments;
          }
          else {
            maskArgs = arguments->maskargs;
          }

          commitFilterChainParameterChanges(mA, maskArgs);
          imageFilterChainSetDataSource(maskChain, self->maskLine);
        }

        arguments->filteredImage = self;
        return TRUE;
      }
    }
    /* If we get here something failed */
    success = FALSE;
  }

  filteredImageDelete(self);
  return success;
}

/* Destructor. See fltrdimg.h for client usage documentation.
 */
void filteredImageDelete(FilteredImage* self)
{
  ImageFilterChain *imageChain;
  ImageFilterChain *maskChain;

  (void)imageFilterChainImage(&imageChain);
  (void)imageFilterChainMask(&maskChain);
  /* It's hard to imagine how control could reach here without the
   * filter chains having been constructed, but if they aren't and can't
   * be, we lose the error.  No matter. */

  /* It's not an error to try free a null object */
  if (self == NULL) {
    return;
  }

  VERIFY_OBJECT(self, FILTEREDIMAGE_NAME);
  UNNAME_OBJECT(self);

  /* Tell the filter chain this image/mask is finished (it's ok to do this
     even if an image was not started) */
  if (imageChain != NULL) {
    imageFilterChainEndImage(imageChain);
  }
  if (maskChain != NULL) {
    imageFilterChainEndImage(maskChain);
  }

  /* Free member objects */
  pixelPackerDelete(self->pixelPacker);
  dataPreparerDelete(self->dataPreparer);

  /* Free the repack buffer */
  if (self->byteSwapBuffer != NULL)
    mm_free(mm_pool_temp, self->byteSwapBuffer, self->swapBufferSize);

  /* Free self */
  mm_free(mm_pool_temp, self, sizeof(FilteredImage));
}

/* See fltrdimg.h for client usage documentation.
*/
Bool filteredImageGetString(FilteredImage* self,
                            uint8** target,
                            uint32* dataLength,
                            uint32* imageOrMask)
{
  VERIFY_OBJECT(self, FILTEREDIMAGE_NAME);
  HQASSERT((target != NULL) || (dataLength != NULL) || (imageOrMask != NULL),
           "filteredImageGetString - NULL argument(s)");

  do {
    /* Immediate abort on error or premature data end */
    switch (dataPreparerGetLine(self->dataPreparer)) {
      case DPNormal:
        break;

      case DPError:
        return FALSE;

      case DPPrematureEnd:
        *target = NULL;
        *dataLength = 0;
        return TRUE;

      default:
        HQFAIL("filteredImageGetString - bad result from dataPreparerGetLine()");
        break;
    }

    /* Pass the normalised data through the filter chain */
    if (!passDataThruFilters(self, target, dataLength))
      return FALSE;
  }while (*dataLength == 0);

  /* Some data has been produced. If the caller is interested, tell them what
     they are getting */
  if (imageOrMask != NULL) {
    if ((self->productionState == ProducingImage) || self->pureMask) {
      /* When dealing with a pure image mask, trick the caller into thinking
         it's got image data (because thats how the image system works) */
      *imageOrMask = ImageDataSelector;
    }
    else {
      *imageOrMask = MaskDataSelector;
    }
  }
  return TRUE;
}

/* --Private methods-- */

/* Returns TRUE if interpolation-based filtering should be disabled, for reasons
of output device type/unusual image parameters.
*/
static Bool shouldDisableInterpolation(Bool coerceToFillFlag)
{
  /* Reasons not to interpolate:
    1. If PDF out is enabled, the interpolate flag will be passed out to the
      pdf file, so there's no need to interpolate in the RIP.
    2. If the image is being coerced to rectfills, its very unlikely that the
      client would want the results from interpolation to be converted to
      rectfills. */
  if ((pdfout_enabled()) || coerceToFillFlag)
    return TRUE;
  else
    return FALSE;
}


/* Check for quick exit, setting 'exitEarly' to TRUE if the passed image/mask
does not require filtering. Returns FALSE on error.

There is no simple way of determining if an image is going to be filtered or
not; each filter in the image filter chains has its own reasons for filtering an
image, and there is no way to second-guess them.

This method obtains and presents a prepared set of filtered image arguments
to the image filtering chains without allocating any structures, using query
methods rather than construction methods, and thus for most images (which will
not be filtered) we will exit as quickly as possible. When an image is filtered,
there is some duplication of work, but in general this is insignificant.
*/
static Bool checkForEarlyExit(IMAGEARGS* source,
                              uint32 deviceAccuracy,
                              Bool* exitEarly)
{
  Bool imagePresent, maskPresent;
  FilteredImageArgs image, mask;

  HQASSERT(source != NULL && exitEarly != NULL,
           "checkForEarlyExit - 'arguments' parameter is NULL");

  *exitEarly = TRUE;

  if (! dataPreparerGetProcessedArgs(source, deviceAccuracy, &image, &mask,
                                     &imagePresent, &maskPresent))
    return FALSE;

  if (imagePresent) {
    Bool imageFilteringActive;
    ImageFilterChain *imageChain;

#if defined(METRICS_BUILD)
    updateImageSamplingMetrics(image.width, image.height,
                               image.widthOnDevice, image.heightOnDevice);
#endif

    if (!imageFilterChainImage(&imageChain))
      return FALSE;
    if (imageChain == NULL)
      return TRUE; /* Exit early */
    if (! imageFilterChainCheckImage(imageChain, &image,
            shouldDisableInterpolation(image.coerceToFillFlag),
            &imageFilteringActive))
      return FALSE;

    if (imageFilteringActive) {
      *exitEarly = FALSE;
      return TRUE;
    }
  }

  if (maskPresent) {
    Bool maskFilteringActive;
    ImageFilterChain *maskChain;

    if (!imageFilterChainMask(&maskChain))
      return FALSE;
    if (maskChain == NULL)
      return TRUE;
    if (! imageFilterChainCheckImage(maskChain, &mask,
            shouldDisableInterpolation(mask.coerceToFillFlag),
            &maskFilteringActive))
      return FALSE;

    if (maskFilteringActive) {
      *exitEarly = FALSE;
      return TRUE;
    }
  }

  /* If we got here then this image does not require filtering. */
  return TRUE;
}

/* Are any of the image/mask dimensions unusable (ie. zero)
 */
static Bool checkForBadSizes(IMAGEARGS* arguments)
{
  if ((arguments->width <= 0) || (arguments->height <= 0)) {
    return TRUE; /* Bad image size */
  }

  if (arguments->maskargs != NULL) {
    if ((arguments->maskargs->width <= 0) ||
        (arguments->maskargs->height <= 0)) {
      return TRUE; /* Bad mask size */
    }
  }

  return FALSE; /* No bad sizes present */
}

/* Is the target device 8 or 16 bit?
 */
static uint32 getDeviceAccuracy(DL_STATE *page)
{
  int32 nColorants;
  COLORANTINDEX *iColorants;

  /* Figure out if the device supports more than 256 levels.

    The following code is basically copied from im_bitmapsize() in
    dl_image.c, with one major difference - the colortype specified by
    the image has not been setup at this point, so I'm just using the
    GSC_FILL colortype, and the REPRO_TYPE_PICTURE reprotype. Since the
    underlying device is the same, and the halftone capabilities are
    probably similar, this should work correctly most of the time, and
    when it doesn't, the results will still look fine. */

  if (gsc_getDeviceColorColorants(gstateptr->colorInfo, GSC_FILL, &nColorants,
                                  &iColorants)) {
    if (DOING_RUNLENGTH(page)) {
      /* Run length output is always expanded/converted in 16 bpp for images */
      return 16;
    } else {
      /* Must check for this here - it can happen in Pattern colorspaces */
      if ((nColorants != 0) && (iColorants != NULL)) {
        if (ht_is16bitLevels(gsc_getSpotno(gstateptr->colorInfo),
                             REPRO_TYPE_PICTURE,
                             nColorants, iColorants,
                             gsc_getRS(gstateptr->colorInfo))) {
          return 16;
        }
      }
    }
  }
  return 8;
}

/* If the native byte order is not big-endian (and the rip expects all 16-bit
image data to be high-byte first), allocate a byte swap buffer, to convert the
uint16's used during filtering back into a byte stream.

The swapped data must be placed into a dedicated buffer as it's not safe to
modify the contents of the buffer returned by the filter chain directly.
*/
static Bool allocateByteSwapBuffer(FilteredImage* self)
{
  VERIFY_OBJECT(self, FILTEREDIMAGE_NAME);

#ifndef highbytefirst
  if (self->imageArgs.bpc == 16) {
    uint32 width, colorantCount;

    if (self->imageFiltered) {
      /* If filtering is active, we'll need use the filtered width and colorant
      count. */
      width = self->filteredImageArgs.width;
      colorantCount = self->filteredImageArgs.colorantCount;
    }
    else {
      /* If filtering is not active, use the source width and colorant count */
      width = self->imageArgs.width;
      colorantCount = self->imageArgs.colorantCount;
    }

    self->swapBufferSize = width * colorantCount * 2;
    self->byteSwapBuffer = mm_alloc(mm_pool_temp, self->swapBufferSize,
                                    MM_ALLOC_CLASS_FILTERED_IMAGE);
    if (self->byteSwapBuffer == NULL)
      return error_handler(VMERROR);
  }
#endif

  return TRUE;
}

/* Swap pairs of bytes in 'data', storing into 'result'.
*/
static void swapBytes(uint8* data, uint8* result, uint32 count)
{
  HQASSERT(data != NULL && result != NULL, "swapBytes - bad parameters.");
  HQASSERT((count & 1) == 0, "swapBytes - 'count' should be an even number.");
  BYTE_SWAP16_BUFFER(result, data, count) ;
}

/* Change the image arguments to reflect the changes applied by the image
 * filtering chain.
 */
static void commitFilterChainParameterChanges(FilteredImageArgs* preparedArgs,
                                              IMAGEARGS* arguments)
{
  HQASSERT(preparedArgs != NULL, "commitFilterChainParameterChanges - "
           "'preparedArgs' parameter is NULL");
  HQASSERT(arguments != NULL, "commitFilterChainParameterChanges - "
           "'arguments' parameter is NULL");

  arguments->width = preparedArgs->width;
  arguments->height = preparedArgs->height;
  arguments->bits_per_comp = preparedArgs->bpc;
  arguments->omatrix = preparedArgs->imageTransform;
}

/* Pass data through the filters and try to get some out
 */
static Bool passDataThruFilters(FilteredImage* self,
                                uint8** result,
                                uint32* resultLength)
{
  uint32 i;
  uint32 present;
  uint32 filteringActive;
  uint32 produceType;
  DataTransaction* source;
  ImageFilterChain *filterChain, *imageFilterChain, *maskFilterChain;

  VERIFY_OBJECT(self, FILTEREDIMAGE_NAME);
  HQASSERT(result != NULL && resultLength != NULL,
           "passDataThruFilters - NULL argument(s)");

  if (!imageFilterChainImage(&imageFilterChain))
    return FALSE;
  if (!imageFilterChainMask(&maskFilterChain))
    return FALSE;
  do {
    for (i = 0; i < 2; i ++) {
      if (i == 0) { /* Try to get image data first */
        present = self->imagePresent;
        filteringActive = self->imageFiltered;
        source = self->imageLine;
        filterChain = imageFilterChain;
        produceType = ProducingImage;
      }
      else {
        present = self->maskPresent;
        filteringActive = self->maskFiltered;
        source = self->maskLine;
        filterChain = maskFilterChain;
        produceType = ProducingMask;
      }

      if (present) { /* Data is present */
        if (!filteringActive) {
          /* No filtering active - try to read from source */
          *resultLength = dataTransactionAvailable(source);
          if (*resultLength > 0) {
            *result = dataTransactionRead(source, *resultLength);
          }
        }
        else {
          /* Image filtering active, so cycle the filter chain */
          imageFilterChainPush(filterChain);
          *resultLength = imageFilterChainPull(filterChain, result);
        }

        /* Was any data produced? */
        if (*resultLength > 0) {
          self->productionState = produceType;
          break;
        }
      }
    }
    /* Keep trying to get output while none is forthcomming, and there is input
       data left */
  } while ((*resultLength == 0) &&
           (dataTransactionAvailable(self->imageLine) > 0) &&
           (dataTransactionAvailable(self->maskLine) > 0));

  /* If we are producing image data, and the byte swap buffer is not NULL, swap
  the output bytes to mimic bytes from the job. */
  if ((*resultLength > 0) && (self->productionState == ProducingImage) &&
      self->byteSwapBuffer != NULL) {
    swapBytes(*result, self->byteSwapBuffer, *resultLength);
    *result = self->byteSwapBuffer;
  }
  return TRUE;
}

static void init_C_globals_fltrdimg(void)
{
#ifdef METRICS_BUILD
  fltrdimg_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&fltrdimg_metrics_hook) ;
#endif
}

IMPORT_INIT_C_GLOBALS( imfltchn )
IMPORT_INIT_C_GLOBALS( mtchptrn )

void filtering_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  init_C_globals_fltrdimg() ;
  init_C_globals_imfltchn() ;
  init_C_globals_mtchptrn() ;
}

/* Log stripped */
