/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:decimate.c(EBDSDK_P.1) $
 * $Id: src:decimate.c,v 1.3.4.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2009-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image filter that halves image dimensions.
 */

#include "core.h"
#include "decimate.h"

#include "objnamer.h"
#include "imagfltrPrivate.h"
#include "datatrns.h"
#include "mm.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "swerrors.h"
#include "namedef_.h"
#include "imfltchn.h"
#include "dictscan.h"
#include "params.h"

/* --Private macros-- */

#define DECIMATE_IF_NAME "Imagefilter decimate"

/* --Private datatypes-- */

typedef struct DecimateIF_s {
  FilteredImageResult target;
  uint32 inputLineSize;
  uint32 outputLineSize;
  uint32 y;
  Bool consuming;
  Bool waitingForLastOddRow;
  FilteredImageArgs args;
  DataTransaction *line;
  OBJECT_NAME_MEMBER
} DecimateIF;

/* --Private prototypes-- */

/* These methods are not static because the are called from outside of this
   file, but not directly (through an interface setup with
   decimateIFExposeImageFilter()), and they should never be called directly,
   so they are absent from the header */

LocalImageFilter decimateIFNew(FilteredImageArgs* args,
                              FilteredImageResult* target);
void decimateIFDelete(LocalImageFilter localFilter);
Bool decimateIFPresent(FilteredImageArgs* args,
                      Bool* accepted,
                      FilteredImageResult* changes);
void decimateIFPush(LocalImageFilter localFilter, DataTransaction* transaction);
uint32 decimateIFPull(LocalImageFilter localFilter, uint8** dataPointer);


/* --Interface exposers-- */

/**
 * ImageFilter exposer
 */
ImageFilter* decimateIFExposeImageFilter(void)
{
  ImageFilter* filter = imageFilterNew();

  if (filter == NULL)
    return NULL;

  filter->type = ImageFilterTypeLocal;
  filter->instance = NULL;

  filter->localFilter.presenter = decimateIFPresent;
  filter->localFilter.constructor = decimateIFNew;
  filter->localFilter.destructor = decimateIFDelete;
  filter->localFilter.push = decimateIFPush;
  filter->localFilter.pull = decimateIFPull;

  return filter;
}

/* --Private methods-- */

/**
 * Constructor - will return null if not able to process image
 */
LocalImageFilter decimateIFNew(FilteredImageArgs* args,
                               FilteredImageResult* target)
{
  DecimateIF *self = NULL;

  HQASSERT(args != NULL, "decimateIFNew - 'args' parameter is NULL");

  self = (DecimateIF*) mm_alloc(mm_pool_temp, sizeof(DecimateIF),
                               MM_ALLOC_CLASS_AVERAGE_IF);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  NAME_OBJECT(self, DECIMATE_IF_NAME);

  /* Setup self */
  self->args = args[0];
  self->target = *target;
  self->y = 0;
  self->consuming = TRUE;
  self->waitingForLastOddRow = FALSE;

  self->inputLineSize = self->args.colorantCount * self->args.width;
  self->outputLineSize = self->args.colorantCount * self->target.width;

  HQASSERT(self->target.height == self->args.height / 2,
           "decimateIFNew - output height must be less than input");
  HQASSERT(self->target.height > 0, "decimateIFNew - output height is zero");

  /* Allocate average buffer */
  self->line = dataTransactionNewWithBuffer(self->outputLineSize);

  if (self->line != NULL)
    return (LocalImageFilter*)self;
  else {
    /* Unsuccessful allocation */
    decimateIFDelete(self);
    return NULL;
  }
}

/**
 * Destructor.
 */
void decimateIFDelete(LocalImageFilter localFilter)
{
  DecimateIF *self = (DecimateIF*)localFilter;

  if (self == NULL)
    return;

  VERIFY_OBJECT(self, DECIMATE_IF_NAME);
  UNNAME_OBJECT(self);

  dataTransactionDelete(self->line);

  mm_free(mm_pool_temp, self, sizeof(DecimateIF));
}

/**
 * Manipulate the passed FilteredImageResult structure to reflect what would
 * happen during processing, but don't actually do any work.
 * As specified by the local image filter interface (in imagfltrPrivate.h), the
 * 'accepted' parameter is set to FALSE by default, and the 'changes' parameter
 * is initially set to match the input image.
 */
Bool decimateIFPresent(FilteredImageArgs* args,
                       Bool* accepted,
                       FilteredImageResult* changes)
{
  USERPARAMS *userparams = get_core_context()->userparams;
  uint32 targetWidth;
  uint32 targetHeight;

  if (! userparams->decimation.enabled)
    return TRUE;

  HQASSERT(args != NULL && accepted != NULL && changes != NULL,
           "decimateIFPresent - parameters cannot be NULL");

  targetWidth = args->width / 2;
  targetHeight = args->height / 2;

  /* If the source or target dimensions are not degenerate, and the bitdepth
   * is 8 bit... */
  if (args->width > 0 && args->height > 0 &&
      targetWidth > 0 && targetHeight > 0 &&
      args->bpc == 8) {
    uint32 resolutionPercent = userparams->decimation.minimumResolutionPercentage;

    /* Don't resample small images. */
    if ((args->width * args->height) < userparams->decimation.minimumArea)
      return TRUE;

    /* Don't resample images whose resolution is too low. */
    if (((args->width * 100) / args->widthOnDevice) < resolutionPercent ||
        ((args->height * 100) / args->heightOnDevice) < resolutionPercent)
      return TRUE;

    changes->width = targetWidth;
    changes->height = targetHeight;
    *accepted = TRUE;
  }

  return TRUE;
}

/**
 * Push 3 channels of data at this filter.
 *
 * Optimsed version of decimateIFPush for the typical case of 3 channels.
 */
void decimate_push3(DecimateIF *self, uint8 *targetLine, uint8 *inputLine)
{
  uint32 x;

  if ( self->y % 2 == 0 ) {
    for (x = 0; x < self->target.width; x ++) {
      targetLine[0] = (inputLine[0] + inputLine[0 + 3]) >> 1;
      targetLine[1] = (inputLine[1] + inputLine[1 + 3]) >> 1;
      targetLine[2] = (inputLine[2] + inputLine[2 + 3]) >> 1;
      targetLine += 3;
      inputLine += 3 * 2;
    }
  } else { /* Second of a pair of lines. */
    uint8 average;
    for (x = 0; x < self->target.width; x ++) {
      average = (inputLine[0] + inputLine[0 + 3]) >> 1;
      targetLine[0] = (targetLine[0] + average) >> 1;
      average = (inputLine[1] + inputLine[1 + 3]) >> 1;
      targetLine[1] = (targetLine[1] + average) >> 1;
      average = (inputLine[2] + inputLine[2 + 3]) >> 1;
      targetLine[2] = (targetLine[2] + average) >> 1;
      targetLine += 3;
      inputLine += 3 * 2;
    }
    self->consuming = FALSE;
  }
}

/**
 * Push some data at this filter.
 */
void decimateIFPush(LocalImageFilter localFilter, DataTransaction* transaction)
{
  DecimateIF *self = (DecimateIF*)localFilter;
  uint32 colorantCount = self->args.colorantCount;
  uint32 x, c;
  uint8* inputLine;
  uint8* targetLine;

  VERIFY_OBJECT(self, DECIMATE_IF_NAME);
  HQASSERT(transaction != NULL,
           "decimateIFPush - 'transaction' parameter is NULL");

  if (! self->consuming || dataTransactionAvailable(transaction) == 0) {
    return;
  }

  inputLine = dataTransactionRead(transaction, self->inputLineSize);

  /* Abort now if we were just waiting to consume the last odd-numbered row. */
  if (self->waitingForLastOddRow) {
    self->waitingForLastOddRow = FALSE;
    self->consuming = FALSE;
    return;
  }

  targetLine = dataTransactionRead(self->line, 0);

  /* Note below that we ignore any trailing odd pixel in the source. */

  if ( colorantCount == 3 ) {
    decimate_push3(self, targetLine, inputLine);
  } else {
    if (self->y % 2 == 0) {
      /* First of a pair of lines. */
      switch (colorantCount) {
        case 3:
          for (x = 0; x < self->target.width; x ++) {
            targetLine[0] = (inputLine[0] + inputLine[0 + colorantCount]) >> 1;
            targetLine[1] = (inputLine[1] + inputLine[1 + colorantCount]) >> 1;
            targetLine[2] = (inputLine[2] + inputLine[2 + colorantCount]) >> 1;
            targetLine += 3;
            inputLine += 3 * 2;
          }
          break;

        default:
          for (x = 0; x < self->target.width; x ++) {
            for (c = 0; c < colorantCount; c ++) {
              targetLine[c] = (inputLine[c]+inputLine[c + colorantCount]) >> 1;
            }
            targetLine += colorantCount;
            inputLine += colorantCount * 2;
          }
          break;
      }
    }
    else {
      /* Second of a pair of lines. */
      uint8 average;
      switch (colorantCount) {
        case 3:
          for (x = 0; x < self->target.width; x ++) {
            average = (inputLine[0] + inputLine[0 + colorantCount]) >> 1;
            targetLine[0] = (targetLine[0] + average) >> 1;
            average = (inputLine[1] + inputLine[1 + colorantCount]) >> 1;
            targetLine[1] = (targetLine[1] + average) >> 1;
            average = (inputLine[2] + inputLine[2 + colorantCount]) >> 1;
            targetLine[2] = (targetLine[2] + average) >> 1;
            targetLine += 3;
            inputLine += 3 * 2;
          }
          break;


        default:
          for (x = 0; x < self->target.width; x ++) {
            for (c = 0; c < colorantCount; c ++) {
              average = (inputLine[c] + inputLine[c + colorantCount]) >> 1;
              targetLine[c] = (targetLine[c] + average) >> 1;
            }
            targetLine += colorantCount;
            inputLine += colorantCount * 2;
          }
          break;
      }
      self->consuming = FALSE;
    }
  }
  self->y ++;

  /* If the image has an odd number of rows, we need to discard the final row
   * (since it won't be included in the filtered output). */
  if (self->y == self->args.height - 1 && self->args.height % 2 == 1) {
    self->consuming = TRUE;
    self->waitingForLastOddRow = TRUE;
  }
}

/**
 * Pull some data from this filter.
 */
uint32 decimateIFPull(LocalImageFilter localFilter, uint8** dataPointer)
{
  DecimateIF *self = (DecimateIF*)localFilter;

  VERIFY_OBJECT(self, DECIMATE_IF_NAME);
  HQASSERT(dataPointer != NULL,
           "decimateIFPull - 'dataPointer' parameter is NULL");

  if (! self->consuming) {
    self->consuming = TRUE;

    dataPointer[0] = (uint8*)dataTransactionRead(self->line, 0);
    return self->outputLineSize;
  }
  else {
    dataPointer[0] = NULL;
    return 0;
  }
}

/* Log stripped */

