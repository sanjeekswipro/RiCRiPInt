/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:mskscalr.c(EBDSDK_P.1) $
 * $Id: src:mskscalr.c,v 1.10.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * An image filter that enlarges image masks
 */

#include "core.h"
#include "mskscalr.h"

#include "objnamer.h"
#include "imagfltrPrivate.h"
#include "mtchptrn.h"
#include "mm.h"
#include "objects.h"
#include "namedef_.h"
#include "swerrors.h"
#include "imfltchn.h"

/* --Private Macros-- */

#define MASKSCALER_NAME "Imagefilter mask scaler"
#define CENTRE_PIXEL (1 << 12)
#define INNER_KERNAL ((14 << 5) | (14 << 10) | (14 << 15))

/* --Private datatypes-- */

typedef struct MaskScaler_s {  
  uint32 width;
  uint32 height;
  uint32 readY;
  uint32 writeY;
  uint32 inputSize;
  uint32 outputLineSize;
  uint32 linesRequired;
  uint32 outputReady;
  uint32 finished;
  uint32 addCount; 
  uint32 removeCount;
  DataTransaction *inputs[5];
  DataTransaction *output;
  MatchPattern *addCandidates;
  MatchPattern *removeCandidates;
  OBJECT_NAME_MEMBER
}MaskScaler;

/* --Private prototypes-- */

/* These methods are not static because the are called from outside of this 
   file, but not directly (through an interface setup with 
   maskScalerExposeImageFilter()), and they should never be called directly, 
   so they are absent from the header */
LocalImageFilter maskScalerNew(FilteredImageArgs* args,
                               FilteredImageResult* target);
void maskScalerDelete(LocalImageFilter localFilter);
Bool maskScalerPresent(FilteredImageArgs* args, 
                       Bool* accepted,
                       FilteredImageResult* changes);
void maskScalerPush(LocalImageFilter localFilter, DataTransaction* data);
uint32 maskScalerPull(LocalImageFilter localFilter, uint8** dataPointer);

STATIC void rotateInputs(MaskScaler* self);
STATIC void produceOutput(MaskScaler* self);

/* --Interface exposers-- */

/* ImageFilter exposer
 */
ImageFilter* maskScalerExposeImageFilter(void)
{
  ImageFilter* filter = imageFilterNew();

  if (filter == NULL)
    return NULL;

  filter->type = ImageFilterTypeLocal;
  filter->instance = NULL;

  filter->localFilter.presenter = maskScalerPresent;
  filter->localFilter.constructor = maskScalerNew;
  filter->localFilter.destructor = maskScalerDelete;
  filter->localFilter.push = maskScalerPush;
  filter->localFilter.pull = maskScalerPull;

  return filter;
}

/* --Private methods-- */

/* Constructor
 */
LocalImageFilter maskScalerNew(FilteredImageArgs* args, 
                               FilteredImageResult* target)
{
  uint32 i;
  MaskScaler *self;

  UNUSED_PARAM(FilteredImageResult*, target);
  HQASSERT(args != NULL, "maskScalerNew - 'args' parameter is NULL");
  HQASSERT((target->width == (args->width * 4)) && 
           (target->height == (args->height * 4)), "maskScalerNew - I/O "
           "dimension mismatch");

  self = (MaskScaler*) mm_alloc(mm_pool_temp, sizeof(MaskScaler), 
                                MM_ALLOC_CLASS_MASK_SCALER);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  NAME_OBJECT(self, MASKSCALER_NAME);
  
  /* NULL pointer memebers */
  self->inputs[0] = self->inputs[1] = self->inputs[2] = NULL;
  self->inputs[3] = self->inputs[4] = NULL;
  self->output = NULL;
  self->addCandidates = NULL;
  self->removeCandidates = NULL;

  self->width = args->width;
  self->height = args->height;

  self->inputSize = (self->width + 7) / 8;
  self->outputLineSize = ((self->width * 4) + 7) / 8;
  self->readY = self->writeY = 0;
  self->linesRequired = 3; /* Need 3 lines of data initially */
  self->outputReady = 0;
  self->finished = 0;

  /* Get hold of the add and remove patterns */
  self->addCandidates = matchPatternAddCandidates(&self->addCount);
  self->removeCandidates = matchPatternRemoveCandidates(&self->removeCount);
  
  /* Did we get valid candidate lists? */
  if ((self->addCandidates != NULL) && (self->removeCandidates != NULL)) {
    uint32 allocError = 0;

    /* Allocate storage for the input and output lines that we'll need to 
       buffer */
    for (i = 0; (i < 5) && (allocError == 0); i ++) {
      self->inputs[i] = dataTransactionNewWithBuffer(self->inputSize);
      
      if (self->inputs[i] != NULL) {
        /* Zero each input buffer, so any output that doesn't have a full 
           input set isn't interfered with */
        dataTransactionClear(self->inputs[i]); 
      }
      else {
        allocError = 1;
      }
    }
        
    if (allocError == 0) { /* Were all input buffer allocations successful? */
      /* We need 4x the output size because we always output 4 lines */
      self->output = dataTransactionNewWithBuffer(self->outputLineSize * 4); 
      
      if (self->output != NULL) {
        return (LocalImageFilter*)self;
      }
    }
  }

  /* One or more members couldn't be allocated */
  maskScalerDelete(self);
  return NULL;
}

/* Destructor
 */
void maskScalerDelete(LocalImageFilter localFilter)
{
  uint32 i;
  MaskScaler *self = (MaskScaler*)localFilter;

  if (self == NULL)
    return;

  VERIFY_OBJECT(self, MASKSCALER_NAME);
  UNNAME_OBJECT(self);

  /* Release the add and remove patterns */
  if (self->addCandidates != NULL)
    matchPatternReleaseAddCandidates();
  
  if (self->removeCandidates != NULL)
    matchPatternReleaseRemoveCandidates();

  /* Free the data transactions */
  dataTransactionDelete(self->output);
  
  for (i = 0; i < 5; i ++)
    dataTransactionDelete(self->inputs[i]);
  
  /* Free self */
  mm_free(mm_pool_temp, self, sizeof(MaskScaler));
}

/* Manipulate the passed FilteredImageResult structure to reflect what would
happen during processing, but don't actually do any work.
As specified by the local image filter interface (in imagfltrPrivate.h), the
'accepted' parameter is set to FALSE by default, and the 'changes' parameter is
initially set to match the input image.
*/
Bool maskScalerPresent(FilteredImageArgs* args, 
                       Bool* accepted,
                       FilteredImageResult* changes)
{ 
  uint32 targetWidth;
  uint32 targetHeight;
  Bool disableFilter = FALSE;
  Bool changesMade = FALSE;
  Bool interpolate;
  FilteredImageResult controllerChanges;

  HQASSERT(args != NULL, "maskScalerDryRun - 'args' parameter is NULL");

  controllerChanges = *changes;
  if (runFilterController(args, NAME_MaskScaleController, &disableFilter,
                          &controllerChanges, &changesMade) == FALSE)
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

  /* We do nothing if interpolate is false. */
  if (! interpolate)
    return TRUE;

  /* These must be true for the scaler to function correctly */
  if ((args->bpc == 1) && (args->width > 1) && (args->height > 1)) {
    if ((targetWidth > args->width) || (targetHeight > args->height)) {
      /* When handling rectangular masks, the major length may become huge as 
         the minor length tries to get to target resolution. Reduce this by 
         rejecting any mask where the resolution of either axis is greater than
         150% of target resolution */
      if ((args->width < (targetWidth * 1.5)) &&
          (args->height < (targetHeight * 1.5))) {
        
        changes->width = args->width * 4;
        changes->height = args->height * 4;
        *accepted = TRUE;
      }
    }
  }

  return TRUE;
}

/* Push some data at the mask scaler
 */
void maskScalerPush(LocalImageFilter localFilter, DataTransaction* data)
{
  MaskScaler *self = (MaskScaler*)localFilter;

  VERIFY_OBJECT(self, MASKSCALER_NAME);
  HQASSERT(data != NULL, "maskScalerPush - 'data' parameter is NULL");
  
  /* If we need data, try to read as many lines of it as we need */
  if ((self->finished == 0) && (self->linesRequired > 0)) {
    /* If there is no more input data, rotate inputs and clear the line, 
       for as many lines are as needed (generally 1) */
    if (self->readY >= self->height) {
      for (; self->linesRequired > 0; self->linesRequired --) {
        rotateInputs(self);
        dataTransactionReset(self->inputs[4]);
        dataTransactionClear(self->inputs[4]);
        /* Still increment readY, so that we'd don't need to worry about the 
           last few lines as a special case */
        self->readY ++; 
      }
    }
    else {  
      uint32 amountAvailable, readSuccessful;
    
      do {
        amountAvailable = dataTransactionAvailable(data);
        
        if (amountAvailable >= self->inputSize) {
          rotateInputs(self);
          dataTransactionReset(self->inputs[4]);
          dataTransactionWrite(self->inputs[4], 
                               dataTransactionRead(data, self->inputSize), 
                               self->inputSize);
          self->readY ++;
          self->linesRequired --;
          readSuccessful = 1;
        }
        else {
          readSuccessful = 0;
        }

      }while(readSuccessful && (self->linesRequired > 0));
    }
  }
}
  
/* Try to pull some data out of the mask scaler. The amount avaiable is 
 * returned
 */
uint32 maskScalerPull(LocalImageFilter localFilter, uint8** dataPointer)
{
  MaskScaler *self = (MaskScaler*)localFilter;

  VERIFY_OBJECT(self, MASKSCALER_NAME);
  HQASSERT(dataPointer != NULL, 
           "maskScalerPull - 'dataPointer' parameter is NULL");

  /* This is true when there are lines of output ready */
  if (self->outputReady > 0) {
    self->outputReady --;
    dataPointer[0] = dataTransactionRead(self->output, self->outputLineSize);
    return self->outputLineSize;
  }

  /* If we've not finished and we have enough data, try to produce some 
     output */
  if ((self->finished == 0) && (self->readY > 2)) {
    if (self->writeY <= (self->readY - 3)) {
      produceOutput(self);
      self->linesRequired = 1;
    }
    
    if (self->writeY >= self->height) { /* Have we finished? */
      self->finished = 1;
    }
  }

  if (self->outputReady) {
    self->outputReady --;
    dataPointer[0] = dataTransactionRead(self->output, self->outputLineSize);
    return self->outputLineSize;
  }
  else {
    dataPointer[0] = NULL;
    return 0;
  }
}

/* Rotate the input buffers
 */
STATIC void rotateInputs(MaskScaler* self)
{   
  uint32 i;
  DataTransaction *temp;

  VERIFY_OBJECT(self, MASKSCALER_NAME);

  temp = self->inputs[0];
  for (i = 0; i < 4; i ++) {
    self->inputs[i] = self->inputs[i + 1];
  }

  self->inputs[4] = temp;
}

/* Produce two lines of output
 */

/* Ugly macro that relies on calling context - it happens for every PIXEL 
 * though, so it needs to be fast, thus it's not a function, but it does 
 * happen in a few places, so.... it's an ugly macro
 */
#define FillColumn() \
  MACRO_START \
    kernal >>= 5; \
    /* Only get some new data if there is some left */ \
    if (((bytesConsumed * 8) + bitsConsumed) < self->width) { \
 \
      kernal |= (values[0] & 128) << 13; \
      kernal |= (values[1] & 128) << 14; \
      kernal |= (values[2] & 128) << 15; \
      kernal |= (values[3] & 128) << 16; \
      kernal |= (values[4] & 128) << 17; \
 \
      values[0] <<= 1; \
      values[1] <<= 1; \
      values[2] <<= 1; \
      values[3] <<= 1; \
      values[4] <<= 1; \
 \
      bitsConsumed ++; \
      if (bitsConsumed >= 8) { \
        for (i = 0; i < 5; i ++) { \
          values[i] = lists[i][0]; \
          lists[i] ++; \
        } \
        bitsConsumed = 0; \
        bytesConsumed ++; \
      } \
    } \
  MACRO_END

STATIC void produceOutput(MaskScaler* self)
{
  uint8 *outputs[5];
  uint8 *lists[5];
  uint8 values[5];
  int32 outputShift = 4;
  uint32 i;
  uint32 x;
  uint32 kernal = 0;
  uint32 bytesConsumed = 0;
  uint32 bitsConsumed = 0;
  uint32 candidateCount;
  uint32 initValue;
  MatchPattern *candidates;
  MatchPattern *cC;
   
  /* Get pointers to the input data, and get hold of the first value for 
     each */
  for (i = 0; i < 5; i ++) {
    lists[i] = dataTransactionRead(self->inputs[i], 0);
    values[i] = lists[i][0];
    lists[i] ++;
  }

  /* Get pointers to the output lines, and init the first output bytes to 
     zero */
  dataTransactionReset(self->output);
  outputs[0] = dataTransactionRead(self->output, 0);
  outputs[0][0] = 0;
  for (i = 1; i < 4; i ++) {
    outputs[i] = outputs[0] + (i * self->outputLineSize);
    outputs[i][0] = 0;
  }

  /* Fill the source kernal with the initial amount of data */
  FillColumn();
  FillColumn();
  FillColumn();

  for (x = 0; x < self->width; x ++) {
    /* Match the current kernal with the add/remove kernals */
    /* Check against remove candidates if the centre is set, else compare 
       against add candidates */
    if ((kernal & CENTRE_PIXEL) > 0) {
      candidates = self->removeCandidates;
      candidateCount = self->removeCount;
      initValue = 1;
    }
    else {
      candidates = self->addCandidates;
      candidateCount = self->addCount;
      initValue = 0;
    }
    
    /* Init loop counter so the no-match detection will work if the pattern 
       match doesn't occur */
    i = candidateCount; 

    /* No point doing any checks when the inner kernal is empty or full */
    if (!(((kernal & INNER_KERNAL) == 0) || 
          ((kernal & INNER_KERNAL) == INNER_KERNAL))) {
      /* Search for match */
      for (i = 0; i < candidateCount; i ++) {
        cC = &candidates[i];
        
        if ((kernal & cC->dontCareMask) == cC->setPattern) {
          outputs[0][0] |= ((cC->targetData >> 12) & 15) << outputShift;
          outputs[1][0] |= ((cC->targetData >> 8) & 15) << outputShift;
          outputs[2][0] |= ((cC->targetData >> 4) & 15) << outputShift;
          outputs[3][0] |= (cC->targetData & 15) << outputShift;
          break;
        }
      }
    }

    /* Set output if no match */
    /* Pointless to set if output is blank (zero) */
    if ((i == candidateCount) && (initValue != 0)) {        
      outputs[0][0] |= 15 << outputShift;
      outputs[1][0] |= 15 << outputShift;
      outputs[2][0] |= 15 << outputShift;
      outputs[3][0] |= 15 << outputShift;
    }

    /* Get new column of data into the kernal */
    FillColumn();

    /* Manage the output */
    outputShift -= 4;
    if ((outputShift < 0) && (x < (self->width - 1))) {
      /* Increment and clear the output */
      for (i = 0; i < 4; i ++) {
        outputs[i] ++;
        outputs[i][0] = 0;
      }
      outputShift = 4;
    }
  }

  self->writeY ++;
  /* Perform a fake write - we've already written into this buffer (not using 
     the write methods though) so we can read from it in stages */
  dataTransactionFakeWrite(self->output, self->outputLineSize * 4); 
  self->outputReady = 4; /* Four lines of output ready */
}

/* Log stripped */
