/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:smooth.c(EBDSDK_P.1) $
 * $Id: src:smooth.c,v 1.2.4.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image filter that smooths images.
 */

#include "core.h"
#include "smooth.h"

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
#include "pclGstate.h"
#include "hqmemset.h"

/* --Private macros-- */

#define SMOOTH_IF_NAME "Imagefilter smooth"
#define MAX_RUN_LENGTH 8
#define TOLERANCE 3

/* At least one RGB component must be darker (i.e. less than) this value for
 * span merging to be enabled. We use 255 - TOLERANCE to avoid cross-over
 * artifacts in faint shades. */
#define FAINTEST 255 - TOLERANCE

#define abs(_x) (((_x) >= 0) ? (_x) : 0 - (_x))

/* --Private datatypes-- */

typedef struct {
  FilteredImageResult target;
  FilteredImageArgs args;
  uint32 lineSize;
  uint32 linesConsumed;

  /* True if spans should grow along the images vertical axis, false for the
   * horizontal axis. */
  Bool vertical;

  /* Each line written to the filter is buffered in this line before processing
   * and production to the consumer. */
  DataTransaction* line;

  /* This line contains the base colors used in vertical run detection. If no
   * run is in progress for a given pixel, the matching pixel in this line is
   * just the previous line's data, otherwise it is the first color in the
   * run. */
  DataTransaction* base;

  /* Run lengths; we don't allow runs to grow too long as nasty stripes can
   * appear. This buffer  */
  uint8* runLengths;

  OBJECT_NAME_MEMBER
} SmoothIF;

/* --Private prototypes-- */

/* These methods are not static because the are called from outside of this
 * file, but not directly (through an interface setup with
 * smoothIFExposeImageFilter()), and they should never be called directly,
 * so they are absent from the header */

LocalImageFilter smoothIFNew(FilteredImageArgs* args,
                             FilteredImageResult* target);
void smoothIFDelete(LocalImageFilter localFilter);
Bool smoothIFPresent(FilteredImageArgs* args,
                     Bool* accepted,
                     FilteredImageResult* changes);
void smoothIFPush(LocalImageFilter localFilter, DataTransaction* transaction);
uint32 smoothIFPull(LocalImageFilter localFilter, uint8** dataPointer);


/* --Interface exposers-- */

/**
 * ImageFilter exposer
 */
ImageFilter* smoothIFExposeImageFilter(void)
{
  ImageFilter* filter = imageFilterNew();

  if (filter == NULL)
    return NULL;

  filter->type = ImageFilterTypeLocal;
  filter->instance = NULL;

  filter->localFilter.presenter = smoothIFPresent;
  filter->localFilter.constructor = smoothIFNew;
  filter->localFilter.destructor = smoothIFDelete;
  filter->localFilter.push = smoothIFPush;
  filter->localFilter.pull = smoothIFPull;

  return filter;
}

/* --Private methods-- */

/**
 * Returns TRUE if the image specified by args is orthogonal or very close
 * to orthogonal (enough that it's worth smoothing the image).
 *
 * @param swapped If not null, this will be TRUE if the image has its x and y
 * axes swapped.
 */
static Bool isOrthogonal(FilteredImageArgs* args, Bool* swapped) {
  double rx[2], ry[2];

  /* This tolerance accepts an off-axis 1 pixel in 100, given the x values
   * used below and assuming a 1 to 1 scaling. It doesn't really matter if
   * the scaling is not 1 to 1, the figure is still indicative. */
  double tolerance = 1;

  MATRIX_TRANSFORM_DXY(0, 0, rx[0], ry[0], &args->imageToDevice);
  MATRIX_TRANSFORM_DXY(100, 0, rx[1], ry[1], &args->imageToDevice);
  /* Note that the image may be rotated by a multiple of 90 degrees. */
  if (abs(ry[0] - ry[1]) <= tolerance ||
      abs(rx[0] - rx[1]) <= tolerance) {
    if (swapped != NULL)
      *swapped = abs(rx[0] - rx[1]) <= tolerance;
    return TRUE;
  }

  return FALSE;
}

/**
 * Constructor - will return null if not able to process image
 */
LocalImageFilter smoothIFNew(FilteredImageArgs* args,
                             FilteredImageResult* target)
{
  SmoothIF *self = NULL;

  HQASSERT(args != NULL, "smoothIFNew - 'args' parameter is NULL");

  self = (SmoothIF*) mm_alloc(mm_pool_temp, sizeof(SmoothIF),
                              MM_ALLOC_CLASS_SMOOTH_IF);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  NAME_OBJECT(self, SMOOTH_IF_NAME);

  /* Setup self */
  self->args = args[0];
  self->target = *target;
  self->lineSize = self->args.colorantCount * self->args.width;
  self->linesConsumed = 0;
  isOrthogonal(args, &self->vertical);

  /* Allocate line buffers. */
  self->line = dataTransactionNewWithBuffer(self->lineSize);

  if (self->vertical) {
    self->base = dataTransactionNewWithBuffer(self->lineSize);
    self->runLengths = (uint8*)mm_alloc(mm_pool_temp,
                                        sizeof(uint8) * self->args.width,
                                        MM_ALLOC_CLASS_SMOOTH_IF);
    if (self->runLengths != NULL) {
      HqMemSet8_f(self->runLengths, 0, self->args.width);
    }
  }
  else {
    self->base = NULL;
    self->runLengths = NULL;
  }

  if (self->line == NULL ||
      (self->vertical && (self->base == NULL || self->runLengths == NULL))) {
    /* Unsuccessful allocation */
    smoothIFDelete(self);
    return NULL;
  }
  return (LocalImageFilter*)self;
}

/**
 * Destructor.
 */
void smoothIFDelete(LocalImageFilter localFilter)
{
  SmoothIF *self = (SmoothIF*)localFilter;

  if (self == NULL)
    return;

  VERIFY_OBJECT(self, SMOOTH_IF_NAME);
  UNNAME_OBJECT(self);

  dataTransactionDelete(self->line);
  if (self->vertical) {
    dataTransactionDelete(self->base);
    mm_free(mm_pool_temp, self->runLengths, sizeof(uint8) * self->args.width);
  }

  mm_free(mm_pool_temp, self, sizeof(SmoothIF));
}

/**
 * Manipulate the passed FilteredImageResult structure to reflect what would
 * happen during processing, but don't actually do any work.
 * As specified by the local image filter interface (in imagfltrPrivate.h), the
 * 'accepted' parameter is set to FALSE by default, and the 'changes' parameter
 * is initially set to match the input image.
 */
Bool smoothIFPresent(FilteredImageArgs* args,
                     Bool* accepted,
                     FilteredImageResult* changes)
{
  UNUSED_PARAM(FilteredImageResult*, changes);

  if (pclGstateIsEnabled() &&
      args->width > 0 && args->height > 0 &&
      args->colorantCount == 3 && args->bpc == 8) {
    /* Don't resample small images. */
    if ((args->width * args->height) < 500 * 500)
      return TRUE;

    /* Don't smooth images whose resolution is too low. */
    if (((args->width * 100) / args->widthOnDevice) < 40 ||
        ((args->height * 100) / args->heightOnDevice) < 40) {
      return TRUE;
    }

    if (isOrthogonal(args, NULL)) {
      *accepted = TRUE;
    }
  }

  return TRUE;
}

static void verticalGrow(SmoothIF *self, DataTransaction* transaction) {
  uint8* line = dataTransactionRead(transaction, self->lineSize);

  if (self->linesConsumed == 0) {
    dataTransactionWrite(self->base, line, self->lineSize);
    dataTransactionWrite(self->line, line, self->lineSize);
  }
  else {
    uint8* base = dataTransactionRead(self->base, 0);
    uint8* out = dataTransactionRead(self->line, 0);
    uint32 x;

    dataTransactionFakeWrite(self->line, self->lineSize);
    for (x = 0; x < self->args.width; x ++) {
      /* Note that we don't try to grow spans into nearly-white areas -
       * artifacts can show up in halftoned output. */
      if (self->runLengths[x] < MAX_RUN_LENGTH &&
          (line[0] < FAINTEST || line[1] < FAINTEST || line[2] < FAINTEST) &&
          abs((int32)base[0] - line[0]) <= TOLERANCE &&
          abs((int32)base[1] - line[1]) <= TOLERANCE &&
          abs((int32)base[2] - line[2]) <= TOLERANCE) {
        /* A match; copy the base color into the output. */
        out[0] = base[0];
        out[1] = base[1];
        out[2] = base[2];
        self->runLengths[x] ++;
      }
      else {
        /* No match; copy the line color into the base and the output. */
        out[0] = base[0] = line[0];
        out[1] = base[1] = line[1];
        out[2] = base[2] = line[2];
        self->runLengths[x] = 0;
      }
      base += 3;
      line += 3;
      out += 3;
    }
  }
}

static void horizontalGrow(SmoothIF *self, DataTransaction* transaction) {
  uint8* line = dataTransactionRead(transaction, self->lineSize);
  uint8* out = dataTransactionRead(self->line, 0);
  uint8* base = line;
  uint32 x, runLength = MAX_RUN_LENGTH;

  dataTransactionFakeWrite(self->line, self->lineSize);
  for (x = 0; x < self->args.width; x ++) {
    /* Note that we don't try to grow spans into white areas - nasty mess
     * can show up in halftoned output. */
    if (runLength < MAX_RUN_LENGTH &&
        (line[0] < FAINTEST || line[1] < FAINTEST || line[2] < FAINTEST) &&
        abs((int32)base[0] - line[0]) <= TOLERANCE &&
        abs((int32)base[1] - line[1]) <= TOLERANCE &&
        abs((int32)base[2] - line[2]) <= TOLERANCE) {
      /* A match; copy the base color into the output. */
      out[0] = base[0];
      out[1] = base[1];
      out[2] = base[2];
      runLength ++;
    }
    else {
      /* No match; copy the line color into the output and set the base color
       * to the current color. */
      out[0] = line[0];
      out[1] = line[1];
      out[2] = line[2];
      base = line;
      runLength = 0;
    }
    line += 3;
    out += 3;
  }
}

/**
 * Push some data at this filter.
 */
void smoothIFPush(LocalImageFilter localFilter, DataTransaction* transaction)
{
  SmoothIF *self = (SmoothIF*)localFilter;

  VERIFY_OBJECT(self, SMOOTH_IF_NAME);
  HQASSERT(transaction != NULL,
           "smoothIFPush - 'transaction' parameter is NULL");

  if (dataTransactionAvailable(transaction) == 0) {
    return;
  }

  if (self->vertical) {
    verticalGrow(self, transaction);
  }
  else {
    horizontalGrow(self, transaction);
  }

  self->linesConsumed ++;
}

/**
 * Pull some data from this filter.
 */
uint32 smoothIFPull(LocalImageFilter localFilter, uint8** dataPointer)
{
  SmoothIF *self = (SmoothIF*)localFilter;

  VERIFY_OBJECT(self, SMOOTH_IF_NAME);
  HQASSERT(dataPointer != NULL,
           "smoothIFPull - 'dataPointer' parameter is NULL");

  *dataPointer = (uint8*)dataTransactionRead(self->line, self->lineSize);
  dataTransactionReset(self->line);
  return self->lineSize;
}

/*
Log stripped */

