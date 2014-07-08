/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:dataprpr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Normalises data for image filtering.
 */

#include "core.h"
#include "dataprpr.h"
#include "datatrns.h"

#include "fltrdimg.h"
#include "namedef_.h"
#include "objnamer.h"
#include "pixlpckr.h"
#include "objects.h"
#include "graphics.h"
#include "gstate.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "images.h"
#include "dl_image.h"
#include "gschead.h"
#include "hqmemset.h"

/* --Private macros-- */

#define DATAPREPARER_NAME "Imagefilter data prep"
#define SINGLE_BYTE_CONTAINER (1)
#define DOUBLE_BYTE_CONTAINER (2)
#define BITS_IN_BYTE (8)
#define MAX_8_BIT_VALUE (255)
#define MAX_16_BIT_VALUE (65535)

/* --Private datatypes-- */

/* Source data type enumeration */
typedef enum SourceImageType {
  SourceImageMask = 1,
  SourceInterleavedNoMask,
  SourcePlanarNoMask,
  SourceInterleavedWithSampleMaskFirst,
  SourceInterleavedWithSampleMaskLast,
  SourceInterleavedWithScanLineMask,
  SourcePlanarWithSeparateMask,
  SourceInterleavedWithSeparateMask,
  SourceUnsupported
} SourceImageType;

/* Line reading state - used in type 3, interleave 2 images */
typedef enum InterleavedLineReadState {
  LineReadingMajor = 1,
  LineReadingMinor
} InterleavedLineReadState;

/* Data source details */
typedef struct DataSource {
  FilteredImageArgs args;
  uint32 polarity;
  uint32 sourceCount;
  uint32 inputSize;
  uint32 expandedSize;
  uint32 linesRead;
  SourceImageType imageType;
  Bool derivative;
  OBJECT **dataSources;
  IMAGEARGS *sourceArgs;
  DataTransaction **inputs;
  DataTransaction **buffers;
  OBJECT_NAME_MEMBER
} DataSource;

/* Indexed colorspace image details */
typedef struct ICSDetails {
  uint8 *indexedColorList;
  uint8 *indexDecode;
  int32 baseColorCount;
  int32 baseColorantCount;
  Bool  changesCommited;
  int32 outBpc;
  int32 baseRangeSize;
  int32 indexDecodeSize;
  size_t indexedColorListSize;
  SYSTEMVALUE *baseRange;
  OBJECT *baseSpace;
} ICSDetails;

/* Data preparer */
struct DataPreparer {
  uint8 *maskBuffer;
  uint8 *imageBuffer;
  uint8 *lookedupLine;
  Bool maskMajor;
  uint32 lineRatio;
  uint32 linesRead;
  InterleavedLineReadState lineReadState;
  SourceImageType inputType;
  Bool contoneMask;
  size_t lookedupLineSize;
  ICSDetails icsDetails;
  IMAGEARGS *sourceArgs;
  DataSource *iDetails;
  DataSource *mDetails;
  DataTransaction *image;
  DataTransaction *mask;
  PixelPacker *packer;
  PixelPackFunction packFunc;
  PixelPackWithMaskFunction interleavedMaskPackFunc;
  PixelPacker *contoneMaskPacker;
  PixelPackFunction contoneMaskPackFunc;
  OBJECT_NAME_MEMBER
};

/* --DataSource file-public prototypes-- */
Bool dataSourceConvertArgs(IMAGEARGS* source,
                           uint32 imageOrMask,
                           uint32 deviceAccuracy,
                           FilteredImageArgs* args);
static DataSource* dataSourceNew(IMAGEARGS* source,
                                 uint32 imageOrMask,
                                 SourceImageType type,
                                 uint32 deviceAccuracy);
static DataSource* dataSourceNewDerivative(IMAGEARGS* source,
                                           uint32 imageOrMask,
                                           SourceImageType type,
                                           DataSource* base,
                                           uint32 deviceAccuracy);
static void dataSourceDelete(DataSource* self);
static void dataSourceCommitParameterChanges(DataSource* self,
                                             Bool contoneMask);
static DPReturnState dataSourceGetLine(DataSource* self, uint32* amountReady);
static void indexedColorLookup(DataPreparer* self);

/* --DataPreparer Private prototypes-- */
static SourceImageType determineSourceImageType(IMAGEARGS* source);
static Bool prepareInputs(DataPreparer* self, IMAGEARGS* source,
                          uint32 deviceAccuracy);
static void lineInterleavedMaskSetup(DataPreparer* self);
static Bool indexedImageSetup(DataPreparer* self, IMAGEARGS* source,
                              uint32 deviceAccuracy);
static Bool createIndexDecodeTable(DataPreparer* self);
static void invertData(uint8* data, uint32 count);
static DPReturnState readMaskLine(DataPreparer* self);
static DPReturnState readImageLine(DataPreparer* self);
static DPReturnState readSampleInterleavedImageMaskLine(DataPreparer* self);
static DPReturnState readLineInterleavedImageMaskLine(DataPreparer* self);

/* --Public methods-- */

/* Is the passed image supported by the data preparer?
 */
Bool dataPreparerSupported(IMAGEARGS* source)
{
  if (determineSourceImageType(source) == SourceUnsupported) {
    return FALSE;
  }
  return TRUE;
}

/* Process the passed IMAGEARGS to produce a set of FilteredImageArgs, identical
to those that would be created by the DataPreparer constructor.
The processed args are placed in the client-allocated structures passed.
*/
Bool dataPreparerGetProcessedArgs(IMAGEARGS* source,
                                  uint32 deviceAccuracy,
                                  FilteredImageArgs* imageArgs,
                                  FilteredImageArgs* maskArgs,
                                  Bool* imagePresent,
                                  Bool* maskPresent)
{
  uint32 type;

  HQASSERT(source != NULL && imageArgs != NULL && maskArgs != NULL &&
           imagePresent != NULL && maskPresent != NULL,
           "dataPreparerGetProcessedArgs - parameters cannot be NULL");

  *imagePresent = *maskPresent = FALSE;

  type = determineSourceImageType(source);
  if (type == SourceUnsupported)
    return FALSE;

  if (type == SourceImageMask) {
    /* This is a pure mask. */
    if (! dataSourceConvertArgs(source, MaskDataSelector, deviceAccuracy,
                                maskArgs))
      return FALSE;
    *maskPresent = TRUE;
  }
  else {
    if (! dataSourceConvertArgs(source, ImageDataSelector, deviceAccuracy,
                                imageArgs))
       return FALSE;
    *imagePresent = TRUE;

    /* Get mask args if it's present. */
    if (type != SourcePlanarNoMask && type != SourceInterleavedNoMask) {
      if (! dataSourceConvertArgs(source->maskargs, MaskDataSelector,
                                  deviceAccuracy, maskArgs))
        return FALSE;
      *maskPresent = TRUE;
    }
  }

  return TRUE;
}

/* Constructor
 */
DataPreparer* dataPreparerNew(IMAGEARGS* source, uint32 deviceAccuracy)
{
  SourceImageType type;
  DataPreparer *self;

  HQASSERT(source != NULL, "dataPreparerNew - passed IMAGEARGS is NULL");
  HQASSERT((deviceAccuracy == 8) || (deviceAccuracy == 16),
           "dataPreparerNew - passed 'deviceAccuracy' invalid");

  /* Get type first so we can exit cleanly if we need to */
  type = determineSourceImageType(source);

  /* Immediate abort when input image is unsupported */
  if (type == SourceUnsupported) {
    return NULL;
  }

  self = (DataPreparer*) mm_alloc(mm_pool_temp, sizeof(DataPreparer),
                                  MM_ALLOC_CLASS_DATA_PREPARER);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  /* Clear and name structure. */
  HqMemZero((uint8 *)self, (int)sizeof(DataPreparer));
  NAME_OBJECT(self, DATAPREPARER_NAME);

  self->icsDetails.changesCommited = FALSE;
  self->inputType = type;
  self->sourceArgs = source;

  if (prepareInputs(self, source, deviceAccuracy)) {
    Bool allocError = TRUE;

    /* Do image setup if needed */
    if (self->iDetails != NULL) {
      DataSource* image = self->iDetails;

      self->imageBuffer = mm_alloc(mm_pool_temp, image->expandedSize,
                                   MM_ALLOC_CLASS_DATA_PREPARER);
      if (self->imageBuffer != NULL) {
        uint32 containerSize;

        /* How accurate data should we produce? */
        if (deviceAccuracy == 16) {
          containerSize = DOUBLE_BYTE_CONTAINER;
        }
        else {
          containerSize = SINGLE_BYTE_CONTAINER;
        }

        /* Create a indexed image lookup table if needed */
        if (indexedImageSetup(self, source, containerSize)) {
          /* Data normaliser for image data */
          if (self->icsDetails.indexedColorList != NULL) {
            self->packer = pixelPackerNew(image->args.originalBpc,
                                          image->args.bpc, image->sourceCount);

            if (self->packer != NULL) {
              /* We'll also need (yet another!) buffer to put the looked-up
                 color values into */
              self->lookedupLineSize = (self->icsDetails.outBpc / BITS_IN_BYTE) *
                                        self->icsDetails.baseColorantCount *
                                        image->args.width;

              self->lookedupLine = mm_alloc(mm_pool_temp,
                                            self->lookedupLineSize,
                                            MM_ALLOC_CLASS_DATA_PREPARER);

              if (self->lookedupLine != NULL) {
                allocError = FALSE; /* This part of allocation succeeded */
              }
              else
                (void)error_handler(VMERROR);
            }
          }
          else {
            /* Data normaliser for image data */
            self->packer = pixelPackerNew(image->args.originalBpc, image->args.bpc,
                                          image->sourceCount);
            if (self->packer != NULL) {
              allocError = FALSE; /* This part of allocation succeeded */
            }
          }

          if (!allocError) {
            self->packFunc = pixelPackerGetPacker(self->packer);
            self->interleavedMaskPackFunc =
              pixelPackerGetMaskPacker(self->packer);
          }
        }
      } else
        (void)error_handler(VMERROR);
    }
    else {
      allocError = FALSE;
    }

    if (!allocError) { /* Only bother if we're setup ok so far */
      /* Do mask setup if needed */
      if ((self->mDetails != NULL) && (self->inputType != SourceImageMask)) {
        DataSource* mask = self->mDetails;
        /* We only need a mask packers/buffers if there is separate (i.e. not
        interleaved with the image) mask data. */

        if (mask->sourceArgs->contoneMask) {
          self->contoneMask = TRUE;

          /* If we have a contone mask, it will need normalizing as though it
          were an image. */
          self->contoneMaskPacker = pixelPackerNew(mask->args.originalBpc,
                                                   mask->args.bpc, 1);
          if (self->contoneMaskPacker != NULL)
            self->contoneMaskPackFunc =
              pixelPackerGetPacker(self->contoneMaskPacker);
          else
            allocError = TRUE;
        }
        else {
          self->contoneMask = FALSE;
        }

        self->maskBuffer = mm_alloc(mm_pool_temp, mask->expandedSize,
                                    MM_ALLOC_CLASS_DATA_PREPARER);
        if (self->maskBuffer == NULL) {
          (void)error_handler(VMERROR);
          allocError = TRUE;
        }
      }

      if (!allocError) { /* Are we still doing ok? */
        /* Normalised data buffers- create transactions for both, even though
           they may be of zero size (makes it easier for the data consumer -
           they can just check the amounts available for each) */
        self->image = dataTransactionNewWithoutBuffer();
        self->mask = dataTransactionNewWithoutBuffer();

        if ((self->image != NULL) && (self->mask != NULL)) {
          /* If necessary, calculate the mask to image height ratio */
          if (self->inputType == SourceInterleavedWithScanLineMask) {
            lineInterleavedMaskSetup(self);
          }
          return self;
        }
      }
    }
  }

  /* Allocation unsuccessful */
  dataPreparerDelete(self);
  return NULL;
}

/* Destructor
 */
void dataPreparerDelete(DataPreparer* self)
{
  /* It's not an error to try free a null object */
  if (self == NULL) {
    return;
  }

  VERIFY_OBJECT(self, DATAPREPARER_NAME);
  UNNAME_OBJECT(self);

  /* Indexed color space details */
  /* Restore any previous state changes made by the indexed color system */
  if (self->icsDetails.changesCommited) {
    /* If we Gsaved earlier, we need to Grestore now */
    if (!grestore_(get_core_context_interp()->pscontext)) {
      HQFAIL("dataPreparerDelete - grestore failed.");
    }
  }

  if (self->icsDetails.indexedColorList != NULL) {
    mm_free(mm_pool_temp, self->icsDetails.indexedColorList,
            self->icsDetails.indexedColorListSize);
  }

   if (self->icsDetails.indexDecode != NULL) {
    mm_free(mm_pool_temp, self->icsDetails.indexDecode,
            self->icsDetails.indexDecodeSize);
  }

  if (self->icsDetails.baseRange != NULL) {
    mm_free(mm_pool_temp, self->icsDetails.baseRange,
            self->icsDetails.baseRangeSize);
  }

  /* Free image and mask buffers */
  if (self->imageBuffer != NULL) {
    mm_free(mm_pool_temp, self->imageBuffer, self->iDetails->expandedSize);
  }

  if (self->maskBuffer != NULL) {
    mm_free(mm_pool_temp, self->maskBuffer, self->mDetails->expandedSize);
  }

  if (self->lookedupLine != NULL) {
    mm_free(mm_pool_temp, self->lookedupLine, self->lookedupLineSize);
  }

  pixelPackerDelete(self->packer);
  pixelPackerDelete(self->contoneMaskPacker);
  dataSourceDelete(self->iDetails);
  dataSourceDelete(self->mDetails);

  /* Delete output data transactions */
  dataTransactionDelete(self->image);
  dataTransactionDelete(self->mask);

  /* Free self */
  mm_free(mm_pool_temp, self, sizeof(DataPreparer));
}

/* Commit any changes to the input parameter set
 */
Bool dataPreparerCommitParameterChanges(DataPreparer* self)
{
  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  if (self->icsDetails.indexedColorList != NULL) {
    ps_context_t *pscontext = get_core_context_interp()->pscontext ;

    /* Gsave and change to the base color space. The destructor will do
       the Grestore */
    if (gsave_(pscontext)) {
      if (push(self->icsDetails.baseSpace, &operandstack) &&
          gsc_setcolorspace(gstateptr->colorInfo, &operandstack, GSC_FILL)) {
        USERVALUE *oldDecode = NULL;

        /* Change decode array to correct for the changes we've made (Need to
           free old list and alloc new one too).

           Need to do this in the right order. If the alloc_image_decode() were
           to fail, we'd be left with an image that doesn't have a valid decode
           array, and trouble later on.
           So, we keep hold of the old decode array, try to allocate the new
           one, and the free the old one if the allocation was successful, or
           restore it and abort if it wasn't */
        if (self->sourceArgs->decode != NULL) {
          oldDecode = self->sourceArgs->decode;
          self->sourceArgs->decode = NULL;
        }

        if (alloc_image_decode(self->sourceArgs,
                               self->icsDetails.baseColorantCount)) {
          int32 i;

          /* Alloc was successful, free the old decode */
          if (oldDecode != NULL) {
            mm_free_with_header(mm_pool_temp, (mm_addr_t)oldDecode);
          }

          for (i = 0; i < self->icsDetails.baseColorantCount; i ++) {
            self->sourceArgs->decode[i * 2] =
              (USERVALUE)self->icsDetails.baseRange[i * 2];
            self->sourceArgs->decode[(i * 2) + 1] =
              (USERVALUE)self->icsDetails.baseRange[(i * 2) + 1];
          }

          /* Change image args to reflect new colorspace - this
             code is basically copied from get_image_args() */
          self->sourceArgs->image_color_space = gsc_getcolorspace(gstateptr->colorInfo,
                                                                  GSC_FILL);

          self->sourceArgs->image_color_object =
            *gsc_getcolorspaceobject(gstateptr->colorInfo, GSC_FILL);

          self->sourceArgs->ncomps = gsc_dimensions(gstateptr->colorInfo , GSC_FILL);
          HQASSERT(self->sourceArgs->ncomps ==
                   self->icsDetails.baseColorantCount,
                   "dataPreparerCommitParamterChanges - gsc_dimensions()"
                   "returned different colorant count than expected");

          /* We'll change the Bpc later, overriding the DataSource's commit */

          /* We've commited the changes. Let the destructor know */
          self->icsDetails.changesCommited = TRUE;
        }
        else {
          /* Alloc failed - rollback and abort */
          self->sourceArgs->decode = oldDecode;
          if (!grestore_(pscontext)) {
            HQFAIL("dataPreparerCommitParamterChanges - grestore failed.");
          }
          return FALSE;
        }
      }
      else {
        /* setcolorspace failed - rollback and abort */
        if (!grestore_(pscontext)) {
          HQFAIL("dataPreparerCommitParamterChanges - grestore failed.");
        }
        return FALSE;
      }
    }
    else {
      /* gsave failed - abort */
      return FALSE;
    }
  }

  if (self->iDetails != NULL) {
    dataSourceCommitParameterChanges(self->iDetails, self->contoneMask);

    /* If we're got an indexed color image, we (the DataPreparer) need to
       override the bpc, as we control it - the existing Bpc is that of the
       indices themselves */
    if (self->icsDetails.indexedColorList != NULL)
      self->sourceArgs->bits_per_comp = self->icsDetails.outBpc;
  }

  if (self->mDetails != NULL) {
    dataSourceCommitParameterChanges(self->mDetails, self->contoneMask);
  }

  return TRUE; /* Successful commit */
}

/* Get a copy of the prepared image FilteredImageArgs in "target".
Returns FALSE when there is no image present (i.e. pure mask).
 */
Bool dataPreparerGetPreparedImageArgs(DataPreparer* self,
                                      FilteredImageArgs* target)
{
  VERIFY_OBJECT(self, DATAPREPARER_NAME);
  HQASSERT(target != NULL,
           "dataPreparerGetPreparedImageArgs - target is NULL");

  if (self->iDetails != NULL) {
    *target = self->iDetails->args;

    /* If we're doing indexed color lookup, we need to override the colorant
    count, bpc, and container size */
    if (self->lookedupLine != NULL) {
      target->colorantCount = self->icsDetails.baseColorantCount;
      target->bpc = self->icsDetails.outBpc;
      target->containerSize = self->icsDetails.outBpc / BITS_IN_BYTE;
    }
    return TRUE;
  }
  return FALSE;
}

/* Get a copy of the prepared mask FilteredImageArgs in "target".
Returns FALSE when there is no mask present (i.e. pure image).
 */
Bool dataPreparerGetPreparedMaskArgs(DataPreparer* self,
                                     FilteredImageArgs* target)
{
  VERIFY_OBJECT(self, DATAPREPARER_NAME);
  HQASSERT(target != NULL, "dataPreparerGetPreparedMaskArgs - target is NULL");

  if (self->mDetails != NULL) {
    *target = self->mDetails->args;
    return TRUE;
  }
  return FALSE;
}

/* Get hold of the prepared image data transaction
 */
DataTransaction* dataPreparerGetImage(DataPreparer* self)
{
  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  return self->image;
}

/* Get hold of the prepared mask data transaction
 */
DataTransaction* dataPreparerGetMask(DataPreparer* self)
{
  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  return self->mask;
}

/* Is this image a pure mask? (ie mask with no image data)
 */
Bool dataPreparerPureMask(DataPreparer* self)
{
  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  if (self->inputType == SourceImageMask) {
    return TRUE;
  }
  else {
    return FALSE;
  }
}

/* Get a line of normalised data. The caller can access the data by obtaining
 * pointers to the transactions held internally through getImageTransaction()
 * and getMaskTransaction(). Once these pointers have been obtained, the are
 * valid for the lifetime of the dataPreparer they belong to
 */
DPReturnState dataPreparerGetLine(DataPreparer* self)
{
  DPReturnState success;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  /* If there is data currently available, return now and force the caller
     to consume it */
  if (dataTransactionAvailable(self->image) > 0) {
    return DPNormal;
  }

  if (dataTransactionAvailable(self->mask) > 0) {
    return DPNormal;
  }

  /* At this point, any previous lines of data will have been consumed, so
     we can reset their buffers */
  dataTransactionReset(self->image);
  dataTransactionReset(self->mask);

  switch (self->inputType) {

    /* --Simple Mask-- */
    case SourceImageMask:

      return readMaskLine(self);

    /* --Simple image-- */
    case SourceInterleavedNoMask:
    case SourcePlanarNoMask:

      return readImageLine(self);

    /* --Image with separate mask-- */
    case SourceInterleavedWithSeparateMask:
    case SourcePlanarWithSeparateMask:

      success = readMaskLine(self);
      if (success != DPNormal)
        return success;

      success = readImageLine(self);
      if (success != DPNormal)
        return success;

      return DPNormal;

    /* --Image with sample interleaved mask-- */
    case SourceInterleavedWithSampleMaskFirst:
    case SourceInterleavedWithSampleMaskLast:

      return readSampleInterleavedImageMaskLine(self);

    /* --Image with line interleaved mask-- */
    case SourceInterleavedWithScanLineMask:

      return readLineInterleavedImageMaskLine(self);

    default:
      HQFAIL("dataPreparerGetLine - Unknown source type");
      return DPError;
  }
}

/* --Private methods-- */

/* Work out the type of input image
 */
static SourceImageType determineSourceImageType(IMAGEARGS* source)
{
  SourceImageType type = SourceUnsupported;

  HQASSERT(source != NULL,
           "determineSourceImageType - Passed IMAGEARGS is NULL");

  /* Work out the type of this image */
  switch (source->imagetype) {
  case TypeImageMask:
    type = SourceImageMask;
    break;

  case TypeImageImage:
    if (source->nprocs == source->ncomps) {
      type = SourcePlanarNoMask;
    }
    else {
      type = SourceInterleavedNoMask;
    }
    break;

  case TypeImageAlphaImage:
  case TypeImageAlphaAlpha:
    if ( source->interleave == INTERLEAVE_PER_SAMPLE ) {
      type = SourceInterleavedWithSampleMaskLast;
      break;
    }
    /*@fallthrough@*/
  case TypeImageMaskedImage:
  case TypeImageMaskedMask:
    switch (source->interleave) {
    case INTERLEAVE_PER_SAMPLE:
      type = SourceInterleavedWithSampleMaskFirst;
      break;

    case INTERLEAVE_SCANLINE:
      type = SourceInterleavedWithScanLineMask;
      break;

    case INTERLEAVE_SEPARATE:
      if (source->nprocs > 1) {
        type = SourcePlanarWithSeparateMask;
      }
      else {
        type = SourceInterleavedWithSeparateMask;
      }
      break;
    }
    break;
  }
  /* If no matches were found, the source image is unsupported */
  return type;
}

/* Create DataSource to handle the input image
 */
static Bool prepareInputs(DataPreparer* self,
                          IMAGEARGS* source,
                          uint32 deviceAccuracy)
{
  self->iDetails = NULL;
  self->mDetails = NULL;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);
  HQASSERT(source != NULL, "prepareInputs - 'source' parameter is NULL");
  HQASSERT((deviceAccuracy == 8) || (deviceAccuracy == 16),
           "prepareInputs - 'deviceAccuracy' parameter invalid");

  /* Always need to get the image args unless it's a mask only */
  if (self->inputType != SourceImageMask) {
    self->iDetails = dataSourceNew(source, ImageDataSelector,
                                   self->inputType, deviceAccuracy);
    if (self->iDetails == NULL) {
      return FALSE;
    }
  }

  /* Need to get the mask args if mask is present */
  if ((self->inputType != SourcePlanarNoMask) &&
      (self->inputType != SourceInterleavedNoMask)) {
    if (self->inputType == SourceInterleavedWithScanLineMask) {
      /* In this case, we must share the datasources and transactions of the
         image, so create a derivative DataSource instance  */
      self->mDetails = dataSourceNewDerivative(source->maskargs,
                                               MaskDataSelector,
                                               self->inputType,
                                               self->iDetails,
                                               deviceAccuracy);
    }
    else {
      if (self->inputType == SourceImageMask) {
        self->mDetails = dataSourceNew(source, MaskDataSelector,
                                       self->inputType, deviceAccuracy);
      }
      else {
        HQASSERT(source->maskargs != NULL, "prepareInputs - maskargs is NULL");
        self->mDetails = dataSourceNew(source->maskargs, MaskDataSelector,
                                       self->inputType, deviceAccuracy);
      }
    }

    if (self->mDetails == NULL) {
      return FALSE;
    }
  }

  return TRUE; /* Allocation successful  */
}

/* Check for this image being in an indexed color space, and perform setup if
 * needed.
 * NOTE: Indexed colorspace images are converted to 8 or 16 bit images,
 * depending on the value of the parameter "containerSize", which can be 1 or
 * 2 (bytes) */
static Bool indexedImageSetup(DataPreparer* self,
                              IMAGEARGS* source,
                              uint32 containerSize)
{
  int32 i;
  ICSDetails *ics = &self->icsDetails;
  USERVALUE *tempColorList;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);
  HQASSERT(source != NULL, "indexedImageSetup - 'source' parameter is NULL");
  HQASSERT((containerSize == SINGLE_BYTE_CONTAINER) ||
           (containerSize == DOUBLE_BYTE_CONTAINER),
           "indexedImageSetup - containerSize is invalid");

  /* NOTE: The color type used throughout the indexed image handling code is
     GSC_FILL. This works because we only ever do anything if the color space
     specified by the image arguments is indexed, in which event the current
     color space must be indexed, unlike "colorimage" type images, where the
     current colorspace may be different that that of the image */

  /* Is this image in an indexed colorspace? */
  if (source->image_color_space == SPACE_Indexed) {

    /* Get the base space object so we can change to the base space later */
    ics->baseSpace = gsc_getbasecolorspaceobject(gstateptr->colorInfo, GSC_FILL);
    /* We are in an indexed colorspace. Get hold of the base color array */
    if (gsc_getBaseColorListDetails(gstateptr->colorInfo, GSC_FILL,
                                    &ics->baseColorCount,
                                    &ics->baseColorantCount)) {
      Bool allocError = FALSE;

      /* Get hold of the base range arrays */
      ics->baseRangeSize = sizeof(SYSTEMVALUE) * ics->baseColorantCount * 2;
      ics->baseRange = (SYSTEMVALUE*) mm_alloc(mm_pool_temp,
                                                ics->baseRangeSize,
                                                MM_ALLOC_CLASS_DATA_PREPARER);
      if (ics->baseRange != NULL) {
        for (i = 0; (i < ics->baseColorantCount) && !allocError; i++) {
          if (!gsc_baseRange(gstateptr->colorInfo, GSC_FILL, i,
                            &ics->baseRange[i * 2])) {
            allocError = TRUE;
          }
          else {
            HQASSERT((ics->baseRange[(i * 2) + 1] - ics->baseRange[i * 2]) != 0,
                      "indexedImageSetup - Base colorspace range has a width "
                      "of zero");
          }
        }
        if (!allocError) {
          uint32 tempListSize;

          /* Allocate a temporary list of the colors - we'll need to
          convert these floats to ints */
          tempListSize = sizeof(USERVALUE) * ics->baseColorCount *
                          ics->baseColorantCount;
          tempColorList = (USERVALUE*) mm_alloc(mm_pool_temp,
                                                tempListSize,
                                                MM_ALLOC_CLASS_DATA_PREPARER);
          if (tempColorList != NULL) {
            if (gsc_getBaseColorList(gstateptr->colorInfo, GSC_FILL,
                                     ics->baseColorCount,
                                     ics->baseColorantCount,
                                     tempColorList)) {
              uint8 *list8 = NULL;
              uint16 *list16 = NULL;
              int32 color;
              int32 colorant;
              SYSTEMVALUE value;

              /* This is the Bpc of looked-up values */
              ics->outBpc = containerSize * BITS_IN_BYTE;

              ics->indexedColorListSize = containerSize *
                                          ics->baseColorCount *
                                          ics->baseColorantCount;
              ics->indexedColorList =
                (uint8*) mm_alloc(mm_pool_temp, ics->indexedColorListSize,
                                  MM_ALLOC_CLASS_DATA_PREPARER);
              if (containerSize == SINGLE_BYTE_CONTAINER) {
                list8 = ics->indexedColorList;
              }
              else {
                list16 = (uint16*) ics->indexedColorList;
              }

              if (ics->indexedColorList != NULL) {
                for (color = 0; color < ics->baseColorCount; color ++)
                  for (colorant = 0; colorant < ics->baseColorantCount;
                        colorant ++) {
                    value = tempColorList[(color * ics->baseColorantCount) +
                                          colorant];

                    /* Convert value into 0-1 range. The decode array
                        specified in the image will be ajusted to reflect and
                        undo this range change */

                    /* We asserted previously that we don't get zero as a
                        divisor */
                    value = (value - ics->baseRange[colorant * 2]) /
                            (ics->baseRange[(colorant * 2) + 1] -
                            ics->baseRange[colorant * 2]);

                    /* Paranoid clamp to range 0 - 1 */
                    if (value < 0.0) {
                      value = 0;
                    }
                    if (value > 1.0) {
                      value = 1;
                    }

                    if (containerSize == SINGLE_BYTE_CONTAINER) {
                      list8[0] = (uint8)((value * MAX_8_BIT_VALUE) + 0.5);
                      list8 ++;
                    }
                    else {
                      list16[0] = (uint16)((value * MAX_16_BIT_VALUE) + 0.5);
                      list16 ++;
                    }
                  }
              }
              else {
                (void)error_handler(VMERROR);
                allocError = TRUE;
              }
            }
            else {
              allocError = TRUE;
            }

            /* Free the temp list */
            mm_free(mm_pool_temp, tempColorList, tempListSize);

            if (!allocError ) {
              if (createIndexDecodeTable(self)) {
                return TRUE; /* Allocation successul */
              }
            }
          } else
            (void)error_handler(VMERROR);
        }
      } else
        (void)error_handler(VMERROR);
    }
  }
  else {
    return TRUE; /* Allocation unnecessary */
  }

  /* It would seem simpler to place error_handler(VMERROR) here, but I
   * can't be sure all the errors that lead here are VMerrors. */
  return FALSE; /* Allocation unsuccessful */
}

/* Create a decode table for index values.
 */
static Bool createIndexDecodeTable(DataPreparer* self)
{
  uint8 *list8Bit = NULL;
  uint16 *list16Bit = NULL;
  uint32 sourceRange;
  uint32 value = 0;
  uint32 bpc;
  USERVALUE* decode;
  SYSTEMVALUE decodedValue = 0;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  bpc = self->iDetails->args.bpc;
  HQASSERT(bpc == 8 || bpc == 16, "createIndexDecodeTable - unexpected bpc.");

  sourceRange = 1 << bpc;
  decode = self->sourceArgs->decode;

  /* Determin the size of the decode list, depending on the bpc of indices
     comming from the pixel packer, and allocate storage for the decode list. */
  if (bpc == 16) {
    self->icsDetails.indexDecodeSize = sizeof(uint16) * sourceRange;
    self->icsDetails.indexDecode =
      (uint8*) mm_alloc(mm_pool_temp, self->icsDetails.indexDecodeSize,
                        MM_ALLOC_CLASS_DATA_PREPARER);
    list16Bit = (uint16*) self->icsDetails.indexDecode;
  }
  else {
    self->icsDetails.indexDecodeSize = sizeof(uint8) * sourceRange;
    self->icsDetails.indexDecode =
      (uint8*) mm_alloc(mm_pool_temp, self->icsDetails.indexDecodeSize,
                        MM_ALLOC_CLASS_DATA_PREPARER);
    list8Bit = self->icsDetails.indexDecode;
  }

  if (self->icsDetails.indexDecode != NULL) {
    uint32 limit = MAX_8_BIT_VALUE;
    if (bpc == 16)
      limit = MAX_16_BIT_VALUE;

    sourceRange --;
    for (value = 0; value <= sourceRange; value ++) {
      /* This mimics how the rip sets-up it's decode array, so we get the
         same results */
      decodedValue = decode[0] + (((decode[1] - decode[0]) /
                                   sourceRange) * value);

      /* Clamp to range we can hold in decode table */
      if (decodedValue < 0)
        decodedValue = 0;

      if (decodedValue > limit)
        decodedValue = limit;

      if (bpc == 16)
        list16Bit[value] = (uint16)decodedValue;
      else
        list8Bit[value] = (uint8)decodedValue;
    }
    return TRUE; /* Allocation successful */
  }

  return error_handler(VMERROR); /* Allocation unsuccessful */
}

/* Determin the image line/mask line ratio
 */
static void lineInterleavedMaskSetup(DataPreparer* self)
{
  uint32 maskHeight;
  uint32 imageHeight;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);
  HQASSERT((self->iDetails != NULL) && (self->mDetails != NULL),
           "lineInterleavedMaskSetup - don't have valid image and mask");

  imageHeight = self->iDetails->args.height;
  maskHeight = self->mDetails->args.height;
  self->linesRead = 0;

  if (maskHeight > imageHeight) {
    self->maskMajor = TRUE;
    self->lineRatio = maskHeight / imageHeight;
    self->lineReadState = LineReadingMajor;
  }
  else {
    self->maskMajor = FALSE;
    self->lineRatio = imageHeight / maskHeight;
    self->lineReadState = LineReadingMinor;
  }
}

/* Invert (NOT) 'count' items of data
 */
static void invertData(uint8* data, uint32 count)
{
  uint32 *wordPointer = (uint32*)data;

  HQASSERT(data != NULL, "invertData - 'data' parameter is NULL");

  for (; count > 3; count -= 4) {
    *wordPointer = ~(*wordPointer);
    wordPointer ++;
  }
  data = (uint8*)wordPointer;

  for (; count > 0; count --) {
    *data = (uint8)~(*data);
    data ++;
  }
}

/* Get a line of mask data from the separate mask source
 */
static DPReturnState readMaskLine(DataPreparer* self)
{
  uint32 amountReady;
  DPReturnState success;
  DataSource *source = self->mDetails;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  /* Invalidate the output transaction incase of early
     return (two cases below) */
  dataTransactionNewBuffer(self->mask, NULL, 0, 0);

  success = dataSourceGetLine(source, &amountReady);
  if (success != DPNormal) {
    return success; /* Immediate abort on error */
  }

  if (amountReady == 0) {
    /* Immediate abort when data runs out (output transactions are empty
       at this stage) */
    return DPNormal;
  }

  if (self->contoneMask) {
    /* Contone masks need to be normalised. */
    uint8* sourceData = dataTransactionRead(source->buffers[0],
                                            source->inputSize);
    (void)self->contoneMaskPackFunc(self->contoneMaskPacker, sourceData,
                                    self->maskBuffer, source->args.width);

    dataTransactionNewBuffer(self->mask, self->maskBuffer,
                            source->expandedSize, source->expandedSize);
  }
  else {
    /* Standard 1 bit mask. */
    dataTransactionNewBuffer(self->mask, dataTransactionRead(source->buffers[0],
                                                             source->expandedSize),
                             source->expandedSize, source->expandedSize);
    if (source->polarity == 0)
      invertData(dataTransactionRead(self->mask, 0),
                 dataTransactionAvailable(self->mask));
  }

  return DPNormal;
}

/* Get a line of image data from the separate image source, and repack it if
 * necessary
 */
static DPReturnState readImageLine(DataPreparer* self)
{
  uint32 i, amountReady;
  DPReturnState success;
  DataSource *source;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  source = self->iDetails;

  /* Invalidate the output transaction incase of early return (two cases
     below) */
  dataTransactionNewBuffer(self->image, NULL, 0, 0);

  success = dataSourceGetLine(source, &amountReady);
  if (success != DPNormal) {
    return success; /* Immediate abort on error */
  }

  if (amountReady == 0) {
    /* Immediate abort when data runs out (output transactions are empty at
       this stage) */
    return DPNormal;
  }

  /* Normalise the line */
  HQASSERT(source->sourceCount > 0,
           "readImageLine - no sources for image data");
  if (source->sourceCount == 1) {
    /* Need to pack width * colorant count when interleaved source */
    uint8* sourceData = dataTransactionRead(source->buffers[0],
                                            source->inputSize);
    (void)self->packFunc(self->packer, sourceData, self->imageBuffer,
                         source->args.width * source->args.colorantCount);
  }
  else {
    for (i = 0; i < source->sourceCount; i ++) {
      uint8* sourceData = dataTransactionRead(source->buffers[i],
                                              source->inputSize);
      (void)self->packFunc(self->packer, sourceData,
                           self->imageBuffer + (i * source->args.containerSize),
                           source->args.width);
    }
  }

  if (self->lookedupLine != NULL) {
    indexedColorLookup(self);
  }
  else {
    dataTransactionNewBuffer(self->image, self->imageBuffer,
                             source->expandedSize, source->expandedSize);
  }

  return DPNormal;
}

/* Get a line of image data, and a line of mask data, from a single
 * interleaved source.
 * NOTE: The mask extraction and image defragment is not optimal - a faster
 * way would be to do the separation specifically in the pack function, at
 * the cost of a special function for each bit depth
 */
static DPReturnState readSampleInterleavedImageMaskLine(DataPreparer* self)
{
  uint32 amountReady;
  uint32 effectiveColorants;
  uint8* source;
  DPReturnState success;
  DataSource *image;
  DataSource *mask;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  image = self->iDetails;
  mask = self->mDetails;

  /* Invalidate the output transaction incase of early return
     (two cases below) */
  dataTransactionNewBuffer(self->image, NULL, 0, 0);
  dataTransactionNewBuffer(self->mask, NULL, 0, 0);

  success = dataSourceGetLine(image, &amountReady);
  if (success != DPNormal)
    return success; /* Immediate abort on error */

  if (amountReady == 0)
    /* Immediate abort when data runs out (output transactions are empty
       at this stage) */
    return DPNormal;

  effectiveColorants = image->args.colorantCount + 1;

  /* Normalise the line */
  source = dataTransactionRead(image->buffers[0], image->inputSize);

  (void)self->packFunc(self->packer, source, self->imageBuffer,
                       image->args.width * effectiveColorants);

  if (self->contoneMask) {
    /* Extract mask, preserving the bit depth. */
    uint32 count = image->args.width ;
    HQASSERT(count > 0, "No samples to extract") ;
    if (image->args.containerSize == SINGLE_BYTE_CONTAINER) {
      uint8 *to = self->maskBuffer ;
      uint8 *from = self->imageBuffer ;
      if ( self->inputType == SourceInterleavedWithSampleMaskLast )
        from += image->args.colorantCount ;
      do {
        *to = *from ;
        ++to ;
        from += effectiveColorants ;
      } while ( --count > 0 ) ;
    } else {
      uint16 *to = (uint16 *)self->maskBuffer ;
      uint16 *from = (uint16 *)self->imageBuffer ;
      if ( self->inputType == SourceInterleavedWithSampleMaskLast )
        from += image->args.colorantCount ;
      do {
        *to = *from ;
        ++to ;
        from += effectiveColorants ;
      } while ( --count > 0 ) ;
    }
  }
  else {
    /* Extract the mask and convert to 1 bit */
    (void)self->interleavedMaskPackFunc(self->packer, self->imageBuffer,
                                        self->maskBuffer,
                                        image->args.width, effectiveColorants);
  }

  /* Remove the gaps left by the mask in the image data - use 8 / 16 bit
     types */
  {
    uint32 count = image->args.width * effectiveColorants ;
    HQASSERT(count > 0, "No samples to extract") ;

    if (image->args.containerSize == SINGLE_BYTE_CONTAINER) {
      uint8 *to, *from, *mask ;
      mask = from = to = self->imageBuffer ;
      if ( self->inputType == SourceInterleavedWithSampleMaskLast )
        mask += image->args.colorantCount ;
      do {
        if ( from == mask ) {
          mask += effectiveColorants ;
        } else {
          *to++ = *from ;
        }
        ++from ;
      } while ( --count > 0 ) ;
    } else {
      uint16 *to, *from, *mask ;
      mask = from = to = (uint16 *)self->imageBuffer ;
      if ( self->inputType == SourceInterleavedWithSampleMaskLast )
        mask += image->args.colorantCount ;
      do {
        if ( from == mask ) {
          mask += effectiveColorants ;
        } else {
          *to++ = *from ;
        }
        ++from ;
      } while ( --count > 0 ) ;
    }
  }

  /* Setup the image and mask transactions */
  {
    uint32 imageSize = image->args.width * image->args.colorantCount *
                       image->args.containerSize;

    if (self->lookedupLine != NULL)
      indexedColorLookup(self);
    else
      dataTransactionNewBuffer(self->image, self->imageBuffer, imageSize,
                               imageSize);

    dataTransactionNewBuffer(self->mask, self->maskBuffer, mask->expandedSize,
                             mask->expandedSize);

    if (!self->contoneMask && mask->polarity == 0)
      invertData(dataTransactionRead(self->mask, 0),
                 dataTransactionAvailable(self->mask));
  }
  return DPNormal;
}

/* Get a line of image or mask data
 */
static DPReturnState readLineInterleavedImageMaskLine(DataPreparer* self)
{
  DPReturnState result;

  VERIFY_OBJECT(self, DATAPREPARER_NAME);

  if (self->lineReadState == LineReadingMajor) {
    /* Always read atleast one line of major data */
    if (self->maskMajor) {
      result = readMaskLine(self);
    }
    else {
      result = readImageLine(self);
    }

    self->linesRead ++;

    /* We've read the right amount of major lines, move to minor state */
    if (self->linesRead >= self->lineRatio) {
      self->lineReadState = LineReadingMinor;
    }
  }
  else {
    /* We're readying a minor line, which means it's only one line of data.
       Ready it and set the state for the next major read */
    if (!self->maskMajor) {
      result = readMaskLine(self);
    }
    else {
      result = readImageLine(self);
    }

    self->lineReadState = LineReadingMajor;
    self->linesRead = 0;
  }
  return result;
}

/* Lookup the actual color values for the index color data. At this point, we
 * have what would be final output image data, where this not an index color
 * image
 */
#define LINE_LOOKUP_BODY(_preparer, _lineSourceType, _lineTargetType, _indexLimit) \
  MACRO_START \
    _lineSourceType *source = (_lineSourceType*)(_preparer)->imageBuffer; \
    _lineSourceType *decode = (_lineSourceType*)(_preparer)->icsDetails.indexDecode; \
    _lineTargetType *target = (_lineTargetType*)(_preparer)->lookedupLine; \
    _lineTargetType *lookup = (_lineTargetType*)(_preparer)->icsDetails.indexedColorList; \
    _lineTargetType *color; \
    uint32 i; \
    uint32 j; \
    uint32 limit = (_preparer)->icsDetails.baseColorCount - 1; \
    uint32 width = (_preparer)->iDetails->args.width; \
    uint32 colorantCount = (_preparer)->icsDetails.baseColorantCount; \
    uint32 index; \
    uint32 decodedIndex; \
 \
    HQASSERT((source != NULL) && (decode != NULL) && \
             (target != NULL) && (lookup != NULL), \
             "LINE_LOOKUP_BODY macro - object is not properly setup"); \
 \
    for (i = 0; i < width; i ++) { \
      decodedIndex = source[0]; \
      source ++; \
 \
      if (decodedIndex > (_indexLimit)) { \
        decodedIndex = (_indexLimit); \
      } \
 \
      index = decode[decodedIndex]; \
      if (index > limit) { \
        index = limit; \
      } \
 \
      color = &lookup[index * colorantCount]; \
      for (j = 0; j < colorantCount; j ++) { \
        target[0] = color[j]; \
        target ++; \
      } \
    } \
  MACRO_END

/* Optimised version of LINE_LOOKUP_BODY for the typical case of 8bits in,
   8bits out, 3 channels */
void line_lookup_body883(DataPreparer *self)
{
  uint8 *source = (uint8 *)self->imageBuffer;
  uint8 *decode = (uint8 *)self->icsDetails.indexDecode;
  uint8 *target = (uint8 *)self->lookedupLine;
  uint8 *lookup = (uint8 *)self->icsDetails.indexedColorList;
  uint8 *color;
  uint32 i, width = self->iDetails->args.width;

  for ( i = 0; i < width; i++ ) {
    color = &lookup[3*decode[*source++]];
    target[0] = color[0];
    target[1] = color[1];
    target[2] = color[2];
    target += 3;
  }
}

static void indexedColorLookup(DataPreparer* self)
{
  VERIFY_OBJECT(self, DATAPREPARER_NAME);
  HQASSERT(self->iDetails->args.colorantCount == 1,
           "indexedColorLookup - colorant count in source data should be "
           "1 (one)");

  if (self->iDetails->args.containerSize == SINGLE_BYTE_CONTAINER) {
    if (self->icsDetails.outBpc == 8) {
      /*  8 Bit indices, 8 bit color data  */
      if ( self->icsDetails.baseColorantCount == 3 ) {
        line_lookup_body883(self);
      } else {
        LINE_LOOKUP_BODY(self, uint8, uint8, MAX_8_BIT_VALUE);
      }
      LINE_LOOKUP_BODY(self, uint8, uint8, MAX_8_BIT_VALUE);
    }
    else {
      /*  8 Bit indices, 16 bit color data  */
      LINE_LOOKUP_BODY(self, uint8, uint16, MAX_8_BIT_VALUE);
    }
  }
  else {
    if (self->icsDetails.outBpc == 8) {
      /* 16 Bit indices, 8 bit color data  */
      LINE_LOOKUP_BODY(self, uint16, uint8, MAX_16_BIT_VALUE);
    }
    else {
      /* 16 Bit indices, 16 bit color data  */
      LINE_LOOKUP_BODY(self, uint16, uint16, MAX_16_BIT_VALUE);
    }
  }

  /* Set the output transaction to the looked-up data */
  dataTransactionNewBuffer(self->image, self->lookedupLine,
                           CAST_SIZET_TO_UINT32(self->lookedupLineSize),
                           CAST_SIZET_TO_UINT32(self->lookedupLineSize));
}

/* --DataSource-- */

/* --Private macros-- */

#define PREPDETAILS_NAME "Imagefilter data prep details"

/* --Private prototypes-- */

static DataSource* allocateAndSimpleSetup(IMAGEARGS* source,
                                          uint32 imageOrMask,
                                          SourceImageType type,
                                          uint32 deviceAccuracy);
static void getDimensionsOnDevice(OMATRIX* transform,
                                  uint32 width,
                                  uint32 height,
                                  uint32* widthOnDevice,
                                  uint32* heightOnDevice);
static uint32 copyDataSources(DataSource* self);
static void determineSizes(DataSource* self);
static uint32 getAmountReady(DataSource* self,
                             uint32* amountReady,
                             uint32 maxRequired);

/* --File-public methods-- */

/* Convert the passed IMAGEARGS into a set of FilteredImageArgs. The resulting
args will exactly match those args generated for a DataSource constructed using
the same IMAGEARGS. e.g.
        FilteredImageArgs args;
        DataSource data;

        dataSourceConvertArgs(imageargs, imageOrMask, deviceAccuracy, &args);
        data = dataSourceNew(imageArgs, imageOrMask, type, deviceAccuracy);
        // 'data->args' is now identical to 'args'
*/
Bool dataSourceConvertArgs(IMAGEARGS* source,
                           uint32 imageOrMask,
                           uint32 deviceAccuracy,
                           FilteredImageArgs* args)
{
  HQASSERT((imageOrMask == ImageDataSelector) ||
           (imageOrMask == MaskDataSelector),
           "dataSourceConvertArgs - bad image/mask selector");

  /* Calculate the image to device transform, and obtain the transformed width
  and height of the image on the device. */
  if (! im_calculatematrix(source, &args->imageToDevice, TRUE))
    return FALSE;

  args->width = source->width;
  args->height = source->height;
  getDimensionsOnDevice(&args->imageToDevice, args->width, args->height,
                        &args->widthOnDevice, &args->heightOnDevice);

  args->colorantCount = source->ncomps;
  args->interpolateFlag = source->interpolate;
  args->coerceToFillFlag = source->coerce_to_fill;
  args->dictionary = source->dictionary;
  args->imageTransform = source->omatrix;
  args->imageOrMask = imageOrMask;
  args->originalBpc = source->bits_per_comp;

  /* Determin the output bits per colorant and container size for the filtered
  image. */
  if (imageOrMask == MaskDataSelector && ! source->contoneMask) {
    args->bpc = 1;
    args->containerSize = 0;
  }
  else {
    if (source->bits_per_comp > 8) {
      /* We can't downsample indexed color images. */
      if (imageOrMask == ImageDataSelector &&
          source->image_color_space == SPACE_Indexed) {
        args->bpc = 16;
        args->containerSize = DOUBLE_BYTE_CONTAINER;
      }
      else {
        args->bpc = deviceAccuracy;
        args->containerSize = deviceAccuracy / BITS_IN_BYTE;
      }
    }
    else {
      args->bpc = 8;
      args->containerSize = SINGLE_BYTE_CONTAINER;
    }
  }

  return TRUE;
}

/* Constructor; extract useful info (eg width, height, etc) from the source
 * IMAGEARGS into a new instance
 */
static DataSource* dataSourceNew(IMAGEARGS* source,
                                 uint32 imageOrMask,
                                 SourceImageType type,
                                 uint32 deviceAccuracy)
{
  DataSource *self;

  HQASSERT(source != NULL, "dataSourceNew - 'source' parameter is NULL");
  HQASSERT((imageOrMask == ImageDataSelector) ||
           (imageOrMask == MaskDataSelector),
           "dataSourceNew - bad image/mask selector");
  HQASSERT((type == SourceImageMask) ||
           (type == SourceInterleavedNoMask) ||
           (type == SourcePlanarNoMask ) ||
           (type == SourceInterleavedWithSampleMaskFirst) ||
           (type == SourceInterleavedWithSampleMaskLast) ||
           (type == SourceInterleavedWithScanLineMask) ||
           (type == SourcePlanarWithSeparateMask) ||
           (type == SourceInterleavedWithSeparateMask),
           "dataSourceNew - bad image type selector");
  HQASSERT((deviceAccuracy == 8) || (deviceAccuracy == 16),
           "dataSourceNew - 'deviceAccuracy' parameter is invalid");

  self = allocateAndSimpleSetup(source, imageOrMask, type, deviceAccuracy);
  if (self == NULL) {
    return NULL;
  }

  self->derivative = FALSE;

  /* How many data sources? */
  if (imageOrMask == MaskDataSelector &&
      (type == SourceInterleavedWithSampleMaskFirst ||
       type == SourceInterleavedWithSampleMaskLast)) {
    self->sourceCount = 0;
  }
  else {
    if (source->nprocs == 1) {
      self->sourceCount = 1;
    }
    else {
      HQASSERT((source->nprocs == source->ncomps) && (source->nprocs != 0),
               "dataSourceNew - multi data sources specified, "
               "but data source count does not equal colorant count");
      self->sourceCount = source->nprocs;
    }
  }

  /* Fill in the more complex details */
  determineSizes(self);

  if (!copyDataSources(self)) {
    dataSourceDelete(self);
    return NULL;
  }
  else {
    return self;
  }
}

/* Constructor; same as simple contructor for simple elements, but uses an
 * existing instance's transactions and datasource lists. This behaviour is
 * required by type 3, interleave 2 images
 */
static DataSource* dataSourceNewDerivative(IMAGEARGS* source,
                                           uint32 imageOrMask,
                                           SourceImageType type,
                                           DataSource* base,
                                           uint32 deviceAccuracy)

{
  DataSource *self;

  HQASSERT(source != NULL,
           "dataSourceNewDerivative - 'source' parameter is NULL");
  HQASSERT((imageOrMask == ImageDataSelector) ||
           (imageOrMask == MaskDataSelector),
           "dataSourceNewDerivative - bad image/mask selector");
  HQASSERT((type == SourceImageMask) || (type == SourceInterleavedNoMask) ||
           (type == SourcePlanarNoMask ) ||
           (type == SourceInterleavedWithSampleMaskFirst) ||
           (type == SourceInterleavedWithSampleMaskLast) ||
           (type == SourceInterleavedWithScanLineMask) ||
           (type == SourcePlanarWithSeparateMask) ||
           (type == SourceInterleavedWithSeparateMask),
           "dataSourceNewDerivative - bad image type selector");
  HQASSERT((deviceAccuracy == 8) || (deviceAccuracy == 16),
           "dataSourceNewDerivative - 'deviceAccuracy' parameter is invalid");

  VERIFY_OBJECT(base, PREPDETAILS_NAME);

  self = allocateAndSimpleSetup(source, imageOrMask, type, deviceAccuracy);
  if (self == NULL) {
    return NULL;
  }

  self->derivative = TRUE;

  /* Set our data source and transaction list pointers to those of the base.
     Obviously, these will become invalid when the base if deleted */
  self->sourceCount = base->sourceCount;
  self->dataSources = base->dataSources;
  self->inputs = base->inputs;
  self->buffers = base->buffers;

  /* Final setup */
  determineSizes(self);

  return self;
}

/* Destructor
 */
static void dataSourceDelete(DataSource* self)
{
  /* It's not an error to try free a null object */
  if (self == NULL) {
    return;
  }

  VERIFY_OBJECT(self, PREPDETAILS_NAME);
  UNNAME_OBJECT(self);

  /* Free the datasource and transaction lists (unless we are derivative) */
  if (!self->derivative && (self->sourceCount > 0)) {
    uint32 i;

    for (i = 0; i < self->sourceCount; i ++) {
      if (self->inputs != NULL) {
        dataTransactionDelete(self->inputs[i]);
      }

      if (self->buffers != NULL) {
        dataTransactionDelete(self->buffers[i]);
      }
    }

    if (self->inputs != NULL) {
      mm_free(mm_pool_temp, self->inputs,
              sizeof(DataTransaction*) * self->sourceCount);
    }

    if (self->buffers != NULL) {
      mm_free(mm_pool_temp, self->buffers,
              sizeof(DataTransaction*) * self->sourceCount);
    }

    if (self->dataSources != NULL) {
      mm_free(mm_pool_temp, self->dataSources,
              sizeof(OBJECT*) * self->sourceCount);
    }
  }

  /*Free self */
  mm_free(mm_pool_temp, self, sizeof(DataSource));
}

/* Perform any necessary changes to the original parameter set
 */
static void dataSourceCommitParameterChanges(DataSource* self,
                                             Bool contoneMask)
{
  VERIFY_OBJECT(self, PREPDETAILS_NAME);

  if (self->args.imageOrMask == ImageDataSelector) {
    /* If the original image has interleave type 1 or 2, change that to
       type 3. Type 4 should never get this far */
    if (self->imageType == SourceInterleavedWithSampleMaskFirst ||
        self->imageType == SourceInterleavedWithSampleMaskLast ||
        self->imageType == SourceInterleavedWithScanLineMask) {
      self->sourceArgs->interleave = INTERLEAVE_SEPARATE;
    }

    /* This change doesn't stop the original datasources being freed
       correctly - see free_image_args() in images.c */
    self->sourceArgs->nprocs = 1;
    self->sourceArgs->lines_per_block = 1;
  }
  else {
    if (!contoneMask) {
      /* We will normalise 1-bit mask polarity, so prevent the rip doing it too. */
      self->sourceArgs->polarity = 1;
      self->sourceArgs->decode[0] = 1;
      self->sourceArgs->decode[1] = 0;
    }
  }

  /* Common (image and mask) changes. */
  self->sourceArgs->bits_per_comp = self->args.bpc;
}

/* Get one line of data - this may be in one buffer, or in several
 */
static DPReturnState dataSourceGetLine(DataSource* self, uint32* amountReady)
{
  int32 maxRequired;
  uint32 i;

  VERIFY_OBJECT(self, PREPDETAILS_NAME);
  HQASSERT(amountReady != NULL,
           "dataSourceGetLine - 'amountReady' parameter is NULL");

  /* Reset our buffer transactions (all data in them will have been consumed
     at this point) */
  for (i = 0; i < self->sourceCount; i ++) {
    HQASSERT(dataTransactionAvailable(self->buffers[i]) == 0,
             "dataSourceGetLine - data left in buffer transaction");
    dataTransactionReset(self->buffers[i]);
  }

  /* If all the lines have been read, return as normal (obviously no data will
     be available) */
  if (self->linesRead >= self->args.height) {
    *amountReady = 0;
    return DPNormal;
  }

  do {
    /* If we already have some data, be sure to only read the extra we need.
       Since all buffer transactions are sync'd, we can check the amount
       available in the first transaction and know that this is true for all
       of them. */
    maxRequired = self->inputSize - dataTransactionAvailable(self->buffers[0]);
    HQASSERT(maxRequired >= 0,
             "dataSourceGetLine - negative amount of data required");

    if (!getAmountReady(self, amountReady, (uint32)maxRequired)) {
      return DPError; /* Immediate abort on data read error */
    }

    if (*amountReady == 0) {
      /* Immediate abort when data runs out (we know this is premature because
         we previously tested for normal end of data) */
      return DPPrematureEnd;
    }

    /* Possible optimisation - only need to do this write if there isn't a full
       line of input available, otherwise can read straight from input
       transactions */
    for (i = 0; i < self->sourceCount; i ++) {
      dataTransactionWrite(self->buffers[i],
                           dataTransactionRead(self->inputs[i], *amountReady),
                           *amountReady);
    }
  }while (dataTransactionAvailable(self->buffers[0]) < self->inputSize);

  self->linesRead ++;
  return DPNormal;
}

/* --Private methods-- */

/* Allocate an instance, and setup it's simple members
 */
static DataSource* allocateAndSimpleSetup(IMAGEARGS* source,
                                          uint32 imageOrMask,
                                          SourceImageType type,
                                          uint32 deviceAccuracy)
{
  FilteredImageArgs args;
  DataSource *self;

  HQASSERT(source != NULL,
           "allocateAndSimpleSetup - 'source' parameter is NULL");
  HQASSERT((imageOrMask == ImageDataSelector) ||
           (imageOrMask == MaskDataSelector),
           "allocateAndSimpleSetup - bad image/mask selector");
  HQASSERT((type == SourceImageMask) || (type == SourceInterleavedNoMask) ||
           (type == SourcePlanarNoMask ) ||
           (type == SourceInterleavedWithSampleMaskFirst) ||
           (type == SourceInterleavedWithSampleMaskLast) ||
           (type == SourceInterleavedWithScanLineMask) ||
           (type == SourcePlanarWithSeparateMask) ||
           (type == SourceInterleavedWithSeparateMask),
           "allocateAndSimpleSetup - bad image type selector");

  if (! dataSourceConvertArgs(source, imageOrMask, deviceAccuracy, &args))
    return NULL;

  self = (DataSource*) mm_alloc(mm_pool_temp, sizeof(DataSource),
                                MM_ALLOC_CLASS_DATA_SOURCE);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  NAME_OBJECT(self, PREPDETAILS_NAME);

  /* NULL pointer members */
  self->dataSources = NULL;
  self->sourceArgs = NULL;
  self->inputs = NULL;
  self->buffers = NULL;

  self->args = args;
  self->sourceArgs = source;
  self->imageType = type;
  self->linesRead = 0;

  /* We will invert the mask data if necessary to obey the polarity setting.
  We do this because we want the output of the mask scaling filter to always
  look the same, regardless of the polarity of the mask. In other words, the
  mask scalar always works with the data as it will appear on the page. */
  self->polarity = source->polarity; /* Only relevant for masks */

  return self;
}

/* Calculate how large the image is in device pixels, using the passed matrix
 */
static void getDimensionsOnDevice(OMATRIX* transform,
                                  uint32 width,
                                  uint32 height,
                                  uint32* widthOnDevice,
                                  uint32* heightOnDevice)
{
  SYSTEMVALUE widthV[2];
  SYSTEMVALUE heightV[2];
  SYSTEMVALUE finalWidth;
  SYSTEMVALUE finalHeight;

  /* Calculate the size of the image in device pixels */
  MATRIX_TRANSFORM_DXY(width, 0, widthV[0], widthV[1], transform);
  MATRIX_TRANSFORM_DXY(0, height, heightV[0], heightV[1], transform);

  finalWidth = sqrt((widthV[0] * widthV[0]) + (widthV[1] * widthV[1]));
  finalHeight = sqrt((heightV[0] * heightV[0]) + (heightV[1] * heightV[1]));

  widthOnDevice[0] = (int32)(finalWidth + 0.5);
  heightOnDevice[0] = (int32)(finalHeight + 0.5);
}

/* Calculate data size details of image
 */
static void determineSizes(DataSource* self)
{
  VERIFY_OBJECT(self, PREPDETAILS_NAME);

  /* How many bytes in the input and expanded data? */
  if (self->args.imageOrMask == ImageDataSelector) {
    uint32 effectiveColorants;
    uint32 expandedColorants;

    HQASSERT(self->sourceCount > 0,
             "determineSizes - image has source count of 0");
    /* Input size - If the data comes from separate sources, this is the amount
       that needs to be read from each to obtain enough for one full line.
       Otherwise this is the number of bytes that must be read from a single
       source to obtain one full line */
    expandedColorants = self->args.colorantCount;
    if (self->sourceCount == 1) {
      if (self->imageType == SourceInterleavedWithSampleMaskFirst ||
          self->imageType == SourceInterleavedWithSampleMaskLast) {
        /* Nasty special case - effectively there is one extra colorant
           (the mask) in the image data */
        effectiveColorants = self->args.colorantCount + 1;
        expandedColorants ++;
      }
      else {
        effectiveColorants = self->args.colorantCount;
      }
    }
    else {
      effectiveColorants = 1;
    }

    self->inputSize = ((self->args.width * self->args.originalBpc *
                        effectiveColorants) + 7) / BITS_IN_BYTE;
    /* Expanded size - the number of bytes in the expanded (normalised)
       output - includes interleaved mask */
    self->expandedSize = self->args.width * expandedColorants *
                         self->args.containerSize;
  }
  else {
    /* Mask */
    if (self->sourceArgs->contoneMask) {
      /* We have a contone mask - treat as image data. */
      self->inputSize = ((self->args.width * self->args.originalBpc) + 7) /
                        BITS_IN_BYTE;
      self->expandedSize = self->args.width * self->args.containerSize;
    }
    else {
      /* We have a standard binary (1-bit) mask. */
      self->inputSize = (self->args.width + 7) / BITS_IN_BYTE;
      self->expandedSize = (self->args.width + 7) / BITS_IN_BYTE;
    }

    /* Nasty special case - input size should be zero (the image
       input size includes our data) */
    if (self->imageType == SourceInterleavedWithSampleMaskLast ||
        self->imageType == SourceInterleavedWithSampleMaskFirst)
      self->inputSize = 0;
  }
}

/* Copy the datasource list, and create transactions to handle and buffer the
 * input data
 */
static uint32 copyDataSources(DataSource* self)
{
  uint32 i;

  VERIFY_OBJECT(self, PREPDETAILS_NAME);

  if (self->sourceCount > 0) { /* Type 3 mask's may not have a data source */
    /* Create the input and buffer transaction lists */
    self->inputs = (DataTransaction**) mm_alloc(mm_pool_temp,
                                                sizeof(DataTransaction*) *
                                                self->sourceCount,
                                                MM_ALLOC_CLASS_DATA_SOURCE);
    self->buffers = (DataTransaction**) mm_alloc(mm_pool_temp,
                                                 sizeof(DataTransaction*) *
                                                 self->sourceCount,
                                                 MM_ALLOC_CLASS_DATA_SOURCE);

    /* Copy the data source list */
    self->dataSources =
      (OBJECT**) mm_alloc(mm_pool_temp, sizeof(OBJECT*) * self->sourceCount,
                          MM_ALLOC_CLASS_DATA_SOURCE);

    /* Need to try an do initialisation before checking for a fail in the
       above allocations, because some of them may have suceeded, and they
       need to be filled with NULL for deleting to work properly */
    for (i = 0; i < self->sourceCount; i ++) {
      if (self->inputs != NULL) {
        self->inputs[i] = NULL;
      }

      if (self->buffers != NULL) {
        self->buffers[i] = NULL;
      }

      if (self->dataSources != NULL) {
        self->dataSources[i] = NULL;
      }
    }

    /* Did the above allocations succeed? */
    if ((self->inputs != NULL) && (self->buffers != NULL) &&
        (self->dataSources != NULL)) {
      Bool allocError = FALSE;

      for (i = 0; (i < self->sourceCount) && !allocError; i++) {
        if ((self->inputs[i] = dataTransactionNewWithoutBuffer()) == NULL) {
          allocError = TRUE;
        }

        if ((self->buffers[i] =
            dataTransactionNewWithBuffer(self->inputSize)) == NULL) {
          allocError = TRUE;
        }

        self->dataSources[i] = &self->sourceArgs->data_src[i];
      }
      if (!allocError) {
        return TRUE; /* Successful allocation */
      }
    } else
      (void)error_handler(VMERROR);
  }
  else {
    return TRUE; /* Allocation unecessary */
  }

  /* Unsuccessful allocation */
  return FALSE;
}

/* Calculate the amount of data available from the source(s)
 */
static uint32 getAmountReady(DataSource* self,
                             uint32* amountReady,
                             uint32 maxRequired)
{
  uint8 *data;
  int32 available;
  uint32 i;
  uint32 minAvailable = 0;
  Bool firstRun = TRUE;
  DataTransaction *trans;

  VERIFY_OBJECT(self, PREPDETAILS_NAME);
  HQASSERT(amountReady != NULL,
           "getAmountReady - 'amountReady' parameter is NULL");

  /* Read the data from the underlying data source only read as much as the
     caller asked for (we don't want to read off the end of the data stream in
     PDF). It's possible that each data source could return some amount less
     than that required. To avoid having to sync all these possibly differing
     amounts, we offer the caller the minimum available out of all data
     sources, and let the caller buffer it up and call us again. */
  for (i = 0; i < self->sourceCount; i ++)
  {
    trans = self->inputs[i];

    /* Remember that each transaction may have some amount of data available,
       usually because one or more sources stalled in the last read and didn't
       manage to read the required amount. Since we don't own the data held in
       each transaction, we can only read new data if all the old has been
       read. */
    if (dataTransactionAvailable(trans) == 0) {
      if (!get_image_string(self->dataSources[i],
                            maxRequired, &data, &available)) {
        return FALSE;
      }
      HQASSERT(available >= 0, "getAmountReady - negative amount returned by "
               "get_image_string()");

      dataTransactionNewBuffer(trans, data, (uint32)available,
                               (uint32)available);
    }

    if (firstRun || (dataTransactionAvailable(trans) < minAvailable)) {
      firstRun = FALSE;
      minAvailable = dataTransactionAvailable(trans);
    }
  }

  HQASSERT(!firstRun, "getAmountReady - minAvailable has not been set");
  *amountReady = minAvailable;

  /* It is possible for there to be more data ready than we asked for - limit
     to what the caller asked for */
  if (*amountReady > maxRequired) {
    *amountReady = maxRequired;
  }

  return TRUE;
}

/* Log stripped */
