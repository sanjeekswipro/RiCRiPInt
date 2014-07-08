/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:intrpltr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * An image filter that produces interpolated output when enlarging images
 */

#include "core.h"
#include "intrpltr.h"

#include "objnamer.h"
#include "imagfltrPrivate.h"
#include "datatrns.h"
#include "mm.h"
#include "hqmemcpy.h"
#include "swerrors.h"
#include "imfltchn.h"
#include "objects.h"
#include "namedef_.h"
#include "dictscan.h"

#include <math.h>

/* --Private macros-- */

#define INTERPOLATOR_NAME "Imagefilter interpolator"

#define HALF (0x80000000u)
#define ACCURACY_SHIFT (14)
#define REDUCTION_SHIFT (32 - ACCURACY_SHIFT)

/* --Private datatypes-- */

typedef struct Interpolator {
  uint8 *buffer;
  uint8 *intermediate;
  uint8 *bufferStart;
  uint8 *writePointer;
  uint8 *readPointer;
  uint8 *lines[2];
  uint32 dataRequired;
  uint32 sourceLineSize;
  uint32 targetLineSize;
  uint32 linesBuffered;
  uint32 linesProduced;
  HqU32x2 y;
  HqU32x2 yInc;
  HqU32x2 leadingLine;
  size_t bufferSize;
  size_t intermediateSize;
  FilteredImageArgs source;
  FilteredImageResult target;
  OBJECT_NAME_MEMBER
} Interpolator;

/* --Private prototypes-- */

/* These methods are not static because the are called from outside of this
   file, but not directly (through an interface setup with
   interpolatorExposeImageFilter()), and they should never be called directly,
   so they are absent from the header */
Bool interpolatorPresent(FilteredImageArgs* args,
                         Bool* accepted,
                         FilteredImageResult* changes);
LocalImageFilter interpolatorNew(FilteredImageArgs* args,
                                 FilteredImageResult* target);
void interpolatorDelete(LocalImageFilter localFilter);
void interpolatorPush(LocalImageFilter localFilter,
                      DataTransaction* transaction);
uint32 interpolatorPull(LocalImageFilter localFilter, uint8** dataPointer);

static uint8* interpolate(Interpolator* self);
static void readComplete(Interpolator* self);
static void newLineRequired(Interpolator* self);
static void expandLine8Bit(Interpolator* self, uint8* targetBuffer);
static void expandLine16Bit(Interpolator* self, uint8* targetBuffer);
static void produceTweenLine8Bit(Interpolator* self, uint32 tween);
static void produceTweenLine16Bit(Interpolator* self, uint32 tween);

/* --Interface exposers-- */

/* Obtain an ImageFilter interface for this filter.
 */
ImageFilter* interpolatorExposeImageFilter(void)
{
  ImageFilter* filter = imageFilterNew();

  if (filter == NULL)
    return NULL;

  filter->type = ImageFilterTypeLocal;
  filter->instance = NULL;

  filter->localFilter.presenter = interpolatorPresent;
  filter->localFilter.constructor = interpolatorNew;
  filter->localFilter.destructor = interpolatorDelete;
  filter->localFilter.push = interpolatorPush;
  filter->localFilter.pull = interpolatorPull;

  return filter;
}

/* --Private methods-- */

/* Constructor - will return null if not able to process image
 */
LocalImageFilter interpolatorNew(FilteredImageArgs* args,
                                 FilteredImageResult* target)
{
  uint32 bytesPerPixel;
  SYSTEMVALUE yIncReal;
  Interpolator *self;

  HQASSERT(args != NULL, "interpolatorNew - 'args' parameter is NULL");

  self = (Interpolator*) mm_alloc(mm_pool_temp, sizeof(Interpolator),
                                  MM_ALLOC_CLASS_INTERPOLATOR);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  NAME_OBJECT(self, INTERPOLATOR_NAME);

  /* NULL pointer members */
  self->buffer = NULL;
  self->intermediate = NULL;
  self->bufferStart = NULL;
  self->writePointer = NULL;
  self->readPointer = NULL;
  self->lines[0] = self->lines[1] = NULL;

  self->linesBuffered = 0;
  self->linesProduced = 0;
  self->source = *args;
  self->target = *target;
  bytesPerPixel = self->source.colorantCount * self->source.containerSize;
  self->sourceLineSize = self->source.width * bytesPerPixel;
  self->targetLineSize = self->target.width * bytesPerPixel;

#define ONE_SHIFT_LEFT_32  ((double)(1u<<31) * 2.0)
  if (self->source.height >= (self->target.height - 1)) {
    /* Note we have a special case for when source and target height are the
     * same, so this value does not matter in that case. */
    yIncReal = ONE_SHIFT_LEFT_32;
  }
  else {
    yIncReal = (SYSTEMVALUE)self->source.height * ONE_SHIFT_LEFT_32 / (target->height - 1);
  }
  HqU32x2FromDouble(&self->yInc, yIncReal);

  HqU32x2FromUint32(&self->y, 0);
  /* When y becomes more than leadingLine, we need to read in a new source
     line */
  self->leadingLine.high = 1;
  self->leadingLine.low = HALF;

  /* Allocate storage for buffer and intermediate lines */
  /* The bytesPerPixel * 2 deal is for the buffer space at either end used in
     expansion. Generally the output line would be sufficiently larger than
     the input to make this unnecessary, but it's there just to be sure */
  self->bufferSize = self->targetLineSize + (bytesPerPixel * 2);
  self->intermediateSize = self->targetLineSize * 2;

  self->buffer = (uint8*) mm_alloc(mm_pool_temp, self->bufferSize,
                                   MM_ALLOC_CLASS_INTERPOLATOR);
  self->intermediate = (uint8*) mm_alloc(mm_pool_temp, self->intermediateSize,
                                         MM_ALLOC_CLASS_INTERPOLATOR);
  if ((self->buffer == NULL) || (self->intermediate == NULL)) {
    interpolatorDelete(self);
    (void)error_handler(VMERROR);
    return NULL;
  }
  /* Initialise pointers into the two buffers just allocated - we don't use
     their base addresses directly */
  /* + bytesPerPixel allows a buffer pixels to be inserted at the start (the
     end will have them too) */
  self->bufferStart = self->buffer + bytesPerPixel;
  self->lines[0] = self->intermediate;
  self->lines[1] = self->intermediate + self->targetLineSize;

  /* Setup the current read (one line into the input buffer) */
  self->dataRequired = self->sourceLineSize;
  self->writePointer = self->bufferStart;

  return (LocalImageFilter*)self;
}

/* Destructor
 */
void interpolatorDelete(LocalImageFilter localFilter)
{
  Interpolator *self = (Interpolator*)localFilter;

  /* It's not an error to try free a null object */
  if (self == NULL) {
    return;
  }
  VERIFY_OBJECT(self, INTERPOLATOR_NAME);
  UNNAME_OBJECT(self);

  /* Free the expansion buffers */
  if (self->buffer != NULL) {
    mm_free(mm_pool_temp, self->buffer, self->bufferSize);
  }

  if (self->intermediate != NULL) {
    mm_free(mm_pool_temp, self->intermediate, self->intermediateSize);
  }

  mm_free(mm_pool_temp, self, sizeof(Interpolator));
}

/* Manipulate the passed FilteredImageResult structure to reflect what would
happen during processing, but don't actually do any work.
As specified by the local image filter interface (in imagfltrPrivate.h), the
'accepted' parameter is set to FALSE by default, and the 'changes'
parameter is initially set to match the input image.
*/
Bool interpolatorPresent(FilteredImageArgs* args,
                         Bool* accepted,
                         FilteredImageResult* changes)
{
  uint32 targetWidth;
  uint32 targetHeight;
  Bool disableFilter = FALSE;
  Bool changesMade = FALSE;
  Bool interpolate;
  FilteredImageResult controllerChanges;

  HQASSERT(args != NULL && accepted != NULL && changes != NULL,
           "interpolatorPresent - parameters cannot be NULL");

  controllerChanges = *changes;
  if (!runFilterController(args, NAME_ImageInterpolateController,
                           &disableFilter, &controllerChanges, &changesMade))
    return FALSE;

  if (disableFilter)
    /* Abort immediately. */
    return TRUE;

  /* If the controller made any changes, use those specified. Otherwise
  default to device resolution. */
  if (changesMade) {
    targetWidth = controllerChanges.width;
    targetHeight = controllerChanges.height;
    interpolate = controllerChanges.interpolate;
  }
  else {
    targetWidth = args->widthOnDevice;
    targetHeight = args->heightOnDevice;
    interpolate = args->interpolateFlag;
  }

  if (args->dictionary != NULL) {
    NAMETYPEMATCH match[] = {
      {NAME_GG_TargetInterpWidth | OOPTIONAL, 1, {OINTEGER}},
      {NAME_GG_TargetInterpHeight | OOPTIONAL, 1, {OINTEGER}},
      DUMMY_END_MATCH};

    if (! dictmatch(args->dictionary, match))
      return FALSE;

    if (match[0].result != NULL && match[1].result != NULL) {
      targetWidth = oInteger(*match[0].result);
      targetHeight = oInteger(*match[1].result);
      interpolate = TRUE;
    }
  }

  /* We do nothing if interpolate is false. */
  if (! interpolate)
    return TRUE;

  /* If the source or target dimensions are not degenerate, and the bitdepth
     is 8 or 16 bit... */
  if ((args->width > 0) && (args->height > 0) &&
      (targetWidth > 0) && (targetHeight > 0) &&
      ((args->bpc == 8) || (args->bpc == 16))) {

    /* Atleast one dimension of the output must be greater than that of the
       source, and neither can be any smaller... */
    if ((targetWidth >= args->width) &&
        (targetHeight >= args->height) &&
        ((targetWidth != args->width) ||
         (targetHeight != args->height))) {

      changes->width = targetWidth;
      changes->height = targetHeight;
      *accepted = TRUE;
    }
  }
  return TRUE;
}

/* Push some data to the filter. The amount of data consumed is returned
 */
void interpolatorPush(LocalImageFilter localFilter,
                      DataTransaction* transaction)
{
  Interpolator *self = (Interpolator*)localFilter;

  VERIFY_OBJECT(self, INTERPOLATOR_NAME);
  HQASSERT(transaction != NULL,
           "interpolatorPush - 'transaction' parameter is NULL");

  if ((self->dataRequired > 0) &&
      (dataTransactionAvailable(transaction) >= self->dataRequired)) {
    uint32 readAmount;
    uint32 amountAvailable;

    do {
      /* Read as much data as we can without going over the amount required */
      amountAvailable = dataTransactionAvailable(transaction);
      if (amountAvailable >= self->dataRequired) {
        readAmount = self->dataRequired;
      }
      else {
        readAmount = amountAvailable;
      }

      HqMemCpy(self->writePointer,
               dataTransactionRead(transaction, readAmount), readAmount);
      self->writePointer += readAmount;
      self->dataRequired -= readAmount;

      /* We've read as much data as we needed - go and expand it */
      if (self->dataRequired == 0) {
        readComplete(self); /* This can change dataRequired */
      }
    } while ((self->dataRequired > 0) &&
             (dataTransactionAvailable(transaction) > 0));
  }
}

/* Try to pull some data out of the filter. The amount avaiable is returned
 */
uint32 interpolatorPull(LocalImageFilter localFilter, uint8** dataPointer)
{
  Interpolator *self = (Interpolator*)localFilter;

  VERIFY_OBJECT(self, INTERPOLATOR_NAME);
  HQASSERT(dataPointer != NULL,
           "interpolatorPull - 'dataPointer' paramter is NULL");

  if ((self->linesProduced < self->target.height) &&
      (self->dataRequired == 0)) {
    /* This may return null, if the interpolate isn't actually valid */
    dataPointer[0] = interpolate(self);

    if (dataPointer[0] != NULL) {
      return self->targetLineSize;
    }
    else {
      return 0;
    }
  }
  else {
    dataPointer[0] = NULL;
    return 0;
  }
}

/* We've read one line of data - expand it, and set any further data requests
 */
static void readComplete(Interpolator* self)
{
  uint8* target;
  VERIFY_OBJECT(self, INTERPOLATOR_NAME);

  HQASSERT(self->linesBuffered >= 0 && self->linesBuffered <= 1,
           "Invalid number of lines buffered.");

  self->linesBuffered ++;
  target = self->lines[self->linesBuffered - 1];

  if (self->source.bpc == 8) {
    expandLine8Bit(self, target);
  }
  else {
    expandLine16Bit(self, target);
  }

  /* Only read another line if required. */
  if (self->source.height > 1 &&
      self->source.height < self->target.height &&
      self->linesBuffered < 2 &&
      self->linesProduced < self->target.height) {
    self->dataRequired = self->sourceLineSize;
    self->writePointer = self->bufferStart;
  }
}

/* Produce a tween line from the two buffered, or enter a need-data state
 */
static uint8* interpolate(Interpolator* self)
{
  uint32 tween;
  HqU32x2 y;
  HqU32x2 upperLimit;
  uint8* lines[2];

  VERIFY_OBJECT(self, INTERPOLATOR_NAME);

  y = self->y;
  lines[0] = self->lines[0];
  lines[1] = self->lines[1];

  /* Do this first to ensure it happens - lots of returns below, and one
   * rollback case */
  HqU32x2Add(&self->y, &self->y, &self->yInc);
  self->linesProduced ++;

  /* Special case for images with no vertical scaling. */
  if (self->source.height == self->target.height) {
    newLineRequired(self);
    return lines[0];
  }

  /* Special cases for top and bottom of image, and for 1 high images */
  if (self->source.height == 1 || (y.high == 0 && y.low <= HALF)) {
    return lines[0];
  }

  upperLimit.high = self->source.height - 1;
  upperLimit.low = HALF;
  if (HqU32x2Compare(&y, &upperLimit) >= 0) {
    return lines[1];
  }

  /* Normal operation */
  HQASSERT(self->linesBuffered == 2, "Not enough lines buffered.");

  if (HqU32x2Compare(&y, &self->leadingLine) >= 0) {
    /* We've moved past the data we have buffered. Need to read in a new line */
    newLineRequired(self);
    self->leadingLine.high ++;

    /* Rollback the increment to y and linesProduced, since no output has been
       produced */
    self->y = y;
    self->linesProduced --;
    return NULL;
  }
  else {
    /* The required line is somewhere between the two we have buffered. */
    HqU32x2SubtractUint32(&y, &y, HALF);

    /* Need to use reduced accuracy because at worst case we could be
       modulating a 16-bit color value, and we don't want overflow */
    tween = y.low >> REDUCTION_SHIFT;

    /* Produce a new output line */
    if (self->source.bpc == 8) {
      produceTweenLine8Bit(self, tween);
    }
    else {
      produceTweenLine16Bit(self, tween);
    }

    return self->buffer;
  }
}

/**
 * Call this method when we need to read another source line. This will swap the
 * internal line buffers, allowing self->lines[1] to be used for the next
 * horizontally interpolated line.
 */
static void newLineRequired(Interpolator* self)
{
  uint8 *temp;

  VERIFY_OBJECT(self, INTERPOLATOR_NAME);

  temp = self->lines[0];
  self->lines[0] = self->lines[1];
  self->lines[1] = temp;

  self->linesBuffered --;
  self->writePointer = self->bufferStart;
  self->dataRequired = self->sourceLineSize;
}

/**
 * Horizontally interpolate the line of pixels buffered in interpolator->source
 * to the target width, storing the result in interpolator->target.
 */
#define EXPAND_LINE_BODY(_targetType, _interpolator, _target) \
  MACRO_START \
    _targetType *source = (_targetType*)(_interpolator)->bufferStart; \
    _targetType *target = (_targetType*)(_target); \
    /* Use signed int for these because some calculated indices are -ve: */ \
    int32 i;                                      \
    int32 inLineSize; \
    int32 outLineSize; \
    int32 colorantCount = (int32)(_interpolator)->source.colorantCount; \
    SYSTEMVALUE step; \
    SYSTEMVALUE value; \
   \
    inLineSize = (int32)(_interpolator)->source.width * colorantCount; \
    outLineSize = (int32)(_interpolator)->target.width * colorantCount; \
   \
    /* Buffer ends */ \
    HQASSERT(inLineSize >= colorantCount, \
             "EXPAND_LINE_BODY macro - extraordinary circumstance"); \
    for (i = 0; i < colorantCount; i ++) { \
      source[i - colorantCount] = source[i];            \
      source[inLineSize + i] = source[inLineSize - colorantCount + i]; \
    } \
   \
    /* Calculate mapping factor */ \
    /* Why -1? Because we want to iterate 0 - width over the source, but */ \
    /* we only interate 0 - (width - 1) on the target */ \
    step = (_interpolator)->source.width / \
           (SYSTEMVALUE)((_interpolator)->target.width - 1); \
    value = 0; \
   \
    /* Start and end values are always same as source - all others are */ \
    /* interpolated */ \
    for (i = 0; i < outLineSize; i += colorantCount) { \
      int32 j;                                        \
      int32 ivalue = (int32)value;                    \
      SYSTEMVALUE fraction = value - ivalue;           \
      ivalue *= colorantCount; \
   \
      for (j = 0; j < colorantCount; j ++) { \
        if (fraction < 0.5) { \
          target[i + j] = (_targetType)(source[ivalue + j] * \
                          (fraction + 0.5) + \
                          source[ivalue + j - colorantCount] * \
                          (0.5 - fraction) + 0.5); \
        } \
        else { \
          target[i + j] = (_targetType)(source[ivalue + j] * \
                          (1.5 - fraction) + \
                          source[ivalue + j + colorantCount] * \
                          (fraction - 0.5) + 0.5); \
        } \
      } \
      value += step; \
    } \
  MACRO_END

/**
 * Vertically interpolate between the two lines buffered in the passed
 * interpolator (in the 'lines' member).
 *
 * \param _targetType  The integer type of the interpolator
 * \param _interpolator The interpolator object.
 * \param _tween A value in the range 0 <= tween < 1, controlling the blend
 * between the two horizontally-interpolated source lines; a value of zero will
 * simply return the first buffered line.
 */
#define PRODUCE_TWEEN_LINE_BODY(_targetType, _interpolator, _tween) \
  MACRO_START \
    _targetType *above = (_targetType*)(_interpolator)->lines[0]; \
    _targetType *below = (_targetType*)(_interpolator)->lines[1]; \
    _targetType *target = (_targetType*)(_interpolator)->buffer; \
    _targetType *targetEnd; \
    uint32 lineSize; \
    uint32 omTween; \
   \
    lineSize = (_interpolator)->target.width * \
               (_interpolator)->source.colorantCount; \
   \
    /* Check for special case where we just need to copy */ \
    if ((_tween) == 0) { \
      HqMemCpy(target, above, lineSize * sizeof(_targetType)); \
      return; \
    } \
   \
    omTween = (1 << ACCURACY_SHIFT) - (_tween); \
    targetEnd = target + lineSize; \
    /* Calculate new line */ \
    while (target < targetEnd) { \
      /*Notice we add 0.5 in shifted form, to cause "round" rather than */ \
      /* "truncate" behaviour */ \
      *target = (_targetType)(((*above * omTween) + (*below * (_tween)) + \
                (1 << (ACCURACY_SHIFT - 1))) >> ACCURACY_SHIFT); \
      target ++; \
      above ++; \
      below ++; \
    } \
  MACRO_END

/**
 * 8-bit wrapper for EXPAND_LINE_BODY().
 */
static void expandLine8Bit(Interpolator* self, uint8* targetBuffer)
{
  VERIFY_OBJECT(self, INTERPOLATOR_NAME);
  EXPAND_LINE_BODY(uint8, self, targetBuffer);
}

/**
 * 16-bit wrapper for EXPAND_LINE_BODY().
 */
static void expandLine16Bit(Interpolator* self, uint8* targetBuffer)
{
  VERIFY_OBJECT(self, INTERPOLATOR_NAME);
  EXPAND_LINE_BODY(uint16, self, targetBuffer);
}

/**
 * 8-bit wrapper for PRODUCE_TWEEN_LINE_BODY().
 */
static void produceTweenLine8Bit(Interpolator* self, uint32 tween)
{
  VERIFY_OBJECT(self, INTERPOLATOR_NAME);
  PRODUCE_TWEEN_LINE_BODY(uint8, self, tween);
}

/**
 * 16-bit wrapper for PRODUCE_TWEEN_LINE_BODY().
 */
static void produceTweenLine16Bit(Interpolator* self, uint32 tween)
{
  VERIFY_OBJECT(self, INTERPOLATOR_NAME);
  PRODUCE_TWEEN_LINE_BODY(uint16, self, tween);
}

/* Log stripped */
