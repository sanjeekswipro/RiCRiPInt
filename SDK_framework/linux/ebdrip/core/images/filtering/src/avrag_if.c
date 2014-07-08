/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:avrag_if.c(EBDSDK_P.1) $
 * $Id: src:avrag_if.c,v 1.13.2.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2001-2010, 2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image filter that averages large images into smaller images
 */

#include "core.h"
#include "avrag_if.h"

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

/* --Private macros-- */

#define AVERAGE_IF_NAME "Imagefilter average"
#define MAX_8_BIT_TINT (255)
#define MAX_16_BIT_TINT (65535)

/* Anything fractional blend value less that this wouldn't contribute to
   the output */
#define FRAC_THRESHOLD (1.0 / MAX_16_BIT_TINT) 

/* --Private datatypes-- */

typedef enum AIFStates_e {
  ReadingStartFractional,
  ReadingIntegral,
  ReadingEndFractional,
  Outputting
}AIFStates;

typedef struct AveragerState_s {
  uint32 outputPos;
  uint32 scanPos;
  uint32 start;
  uint32 end;
  SYSTEMVALUE step;
  SYSTEMVALUE total;
  SYSTEMVALUE inputPos;
}AveragerState;

typedef struct AverageIF_s {
  FilteredImageResult target;
  uint32 inputLineSize;
  uint32 outputLineSize;
  uint32 state;
  AveragerState aS;
  FilteredImageArgs args;
  DataTransaction *line;
  OBJECT_NAME_MEMBER
}AverageIF;

/* --Private prototypes-- */

/* These methods are not static because the are called from outside of this 
   file, but not directly (through an interface setup with 
   averageIFExposeImageFilter()), and they should never be called directly, 
   so they are absent from the header */

LocalImageFilter averageIFNew(FilteredImageArgs* args, 
                              FilteredImageResult* target);
void averageIFDelete(LocalImageFilter localFilter);
Bool averageIFPresent(FilteredImageArgs* args, 
                      Bool* accepted,
                      FilteredImageResult* changes);
void averageIFPush(LocalImageFilter localFilter, DataTransaction* transaction);
uint32 averageIFPull(LocalImageFilter localFilter, uint8** dataPointer);

static void averageLine(uint32 inputWidth, 
                        uint32 outputWidth,
                        uint32 interleaveStep,
                        uint8* input, 
                        uint32* output,
                        uint32 overwriteSource,
                        SYSTEMVALUE inputBlendFactor,
                        uint32 inputSize);

static void normaliseLine(uint32* line, 
                          uint32 width, 
                          SYSTEMVALUE total, 
                          uint32 outputSize);

/* --Interface exposers-- */

/* ImageFilter exposer
 */
ImageFilter* averageIFExposeImageFilter(void)
{
  ImageFilter* filter = imageFilterNew();

  if (filter == NULL)
    return NULL;

  filter->type = ImageFilterTypeLocal;
  filter->instance = NULL;

  filter->localFilter.presenter = averageIFPresent;
  filter->localFilter.constructor = averageIFNew;
  filter->localFilter.destructor = averageIFDelete;
  filter->localFilter.push = averageIFPush;
  filter->localFilter.pull = averageIFPull;

  return filter;
}

/* --Private methods-- */

/* Constructor - will return null if not able to process image
 */
LocalImageFilter averageIFNew(FilteredImageArgs* args, 
                              FilteredImageResult* target)
{
  AverageIF *self = NULL;
  
  HQASSERT(args != NULL, "averageIFNew - 'args' parameter is NULL");
  
  self = (AverageIF*) mm_alloc(mm_pool_temp, sizeof(AverageIF), 
                               MM_ALLOC_CLASS_AVERAGE_IF);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
    
  NAME_OBJECT(self, AVERAGE_IF_NAME);

  /* Setup self */
  self->args = args[0];
  self->target = *target;
  
  self->inputLineSize = self->args.colorantCount * self->args.containerSize *
                        self->args.width ;
  self->outputLineSize = self->args.colorantCount * self->args.containerSize * 
                         self->target.width;
                         
  self->state = ReadingStartFractional;
  self->aS.inputPos = 0;
  self->aS.outputPos = 0;
  
  HQASSERT(self->target.height <= self->args.height, 
           "averageIFNew - output height must be less than input");
  HQASSERT(self->target.height > 0, "averageIFNew - output height is zero");
  
  self->aS.step = self->args.height / (SYSTEMVALUE)self->target.height;

  /* Allocate average buffer */
  {
    uint32 averageBufferSize;

    /* The average buffer has a full word for each pixel - this allows the 
       averaging to work properly (and accurately) */
    averageBufferSize = self->args.colorantCount * sizeof(int32) * 
                        self->target.width;
    self->line = dataTransactionNewWithBuffer(averageBufferSize);
  }
  
  if (self->line != NULL)
    return (LocalImageFilter*)self;
  else {
    /* Unsuccessful allocation */
    averageIFDelete(self);
    return NULL;
  }
}

/* Destructor
 */
void averageIFDelete(LocalImageFilter localFilter)
{
  AverageIF *self = (AverageIF*)localFilter;

  if (self == NULL)
    return;

  VERIFY_OBJECT(self, AVERAGE_IF_NAME);
  UNNAME_OBJECT(self);

  dataTransactionDelete(self->line);
  
  mm_free(mm_pool_temp, self, sizeof(AverageIF));
}

/* Manipulate the passed FilteredImageResult structure to reflect what would
happen during processing, but don't actually do any work.
As specified by the local image filter interface (in imagfltrPrivate.h), the
'accepted' parameter is set to FALSE by default, and the 'changes' parameter is
initially set to match the input image.
*/
Bool averageIFPresent(FilteredImageArgs* args, 
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
           "averageIFPresent - parameters cannot be NULL");

  controllerChanges = *changes;
  if (runFilterController(args, NAME_ImageResampleController, &disableFilter,
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

  if (args->dictionary != NULL) {
    NAMETYPEMATCH match[] = {
      {NAME_GG_TargetAverageWidth | OOPTIONAL, 1, {OINTEGER}},
      {NAME_GG_TargetAverageHeight | OOPTIONAL, 1, {OINTEGER}},
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

    /* Atleast one dimension of the output must be less than that of the 
       source, and neither can be any larger... */
    if ((targetWidth <= args->width) && (targetHeight <= args->height) && 
        ((targetWidth != args->width) || (targetHeight != args->height))) {
      changes->width = targetWidth;
      changes->height = targetHeight;
      *accepted = TRUE;
    }
  }

  return TRUE;
}

/* Push some data at this filter
 */ 
void averageIFPush(LocalImageFilter localFilter, DataTransaction* transaction)
{
  uint8 *inputData;
  uint32 *outputData;
  uint32 i;
  uint32 containerSize;
  SYSTEMVALUE fractional;
  AverageIF *self = (AverageIF*)localFilter;
  AveragerState *aS;

  VERIFY_OBJECT(self, AVERAGE_IF_NAME);
  HQASSERT(transaction != NULL, 
           "averageIFPush - 'transaction' parameter is NULL");
  
  if (dataTransactionAvailable(transaction) > 0) {
    /* We perform a dummy read on the input because not all of the cases below
      want new data after processing - notably the EndFractional case leaves 
      the input in the buffer so the StartFraction case can reuse it. Cases
      that do require new data perform the real read themselves */
    inputData = dataTransactionRead(transaction, 0);
  }
  else {
    inputData = NULL;
  }
  containerSize = self->args.containerSize;
  outputData = (uint32*)dataTransactionRead(self->line, 0);
  
  aS = &self->aS;
  
  /* Act according to our internal state */
  switch (self->state)
  {
    case ReadingStartFractional:
      if (inputData == NULL) {
        break;
      }
      
      /* Perform a real read on the input to show we've consumed it */
      (void)dataTransactionRead(transaction, self->inputLineSize);

      fractional = 1 - (aS->inputPos - (int32)aS->inputPos);
      aS->total = fractional;
          
      for (i = 0; i < self->args.colorantCount; i ++) {
        averageLine(self->args.width, self->target.width, 
                    self->args.colorantCount,
                    &inputData[i * containerSize], 
                    &outputData[i], 1, fractional, containerSize);
      }
 
      aS->start = (uint32)(aS->inputPos + 1);
      aS->end = (uint32)(aS->inputPos + aS->step);
      aS->scanPos = aS->start;
      self->state = ReadingIntegral;
      break;
    
    case ReadingIntegral:
      if (aS->scanPos < aS->end) {
        if (inputData == NULL) {
          break;
        }      

        /* Perform a real read on the input to show we've consumed it */
        (void)dataTransactionRead(transaction, self->inputLineSize);
        
        for (i = 0; i < self->args.colorantCount; i ++) {
          averageLine(self->args.width, self->target.width, 
                      self->args.colorantCount,
                      &inputData[i * containerSize], 
                      &outputData[i], 0, 1.0, containerSize);
        }
        aS->total += 1.0;
        aS->scanPos ++;
      }
      else {
        aS->inputPos += aS->step;
        self->state = ReadingEndFractional;
      }
      break;
      
    case ReadingEndFractional:
      {
        uint32 blendNeeded = 1;
        
        fractional = aS->inputPos - (int32)aS->inputPos;
        /* Is the blend fraction large enough to bother doing? */
        if (fractional < FRAC_THRESHOLD) {
          blendNeeded = 0;
        }

        if ((inputData != NULL) || (blendNeeded == 0)) {
          if (blendNeeded == 1) {
            for (i = 0; i < self->args.colorantCount; i ++) {
              averageLine(self->args.width, self->target.width, 
                          self->args.colorantCount,
                          &inputData[i * containerSize], 
                          &outputData[i], 0, fractional, containerSize);
            }
            aS->total += fractional;
          }
          
          /* container size is in bytes - multiply by 8 to get size in bits */
          normaliseLine(outputData, 
                        self->target.width * self->args.colorantCount, 
                        aS->total, self->args.containerSize);
          aS->outputPos ++;
          self->state = Outputting;
        }
        break;
      }
  }
}

/* Pull some data from this filter
 */
uint32 averageIFPull(LocalImageFilter localFilter, uint8** dataPointer)
{
  AverageIF *self = (AverageIF*)localFilter;

  VERIFY_OBJECT(self, AVERAGE_IF_NAME);
  HQASSERT(dataPointer != NULL, 
           "averageIFPull - 'dataPointer' parameter is NULL");

  if (self->state == Outputting) {
    /* Move back into reading state */
    self->state = ReadingStartFractional;
    
    dataPointer[0] = (uint8*)dataTransactionRead(self->line, 0);
    return self->outputLineSize;
  }
  else {
    dataPointer[0] = NULL;
    return 0;
  }
}

/* Average a line of input into the output buffer. If 'overwriteSource' is 1,
 * the contents of the output buffer will not be included in the averaging.
 * 'inputBlendFactor' will be applied to the input as a whole - this supports
 * fractional contributions of input lines.
 * Support for multiple colorants is via by the 'interleaveStep' parameter,
 * which is the number of pixels to step over for each iteration. Thus to 
 * process a three-colorant line, this function would be called three times, 
 * with a value of '3' for 'interleaveStep'.
 */
static void averageLine(uint32 inputWidth, 
                        uint32 outputWidth,
                        uint32 interleaveStep,
                        uint8* inputPointer, 
                        uint32* output,
                        uint32 overwriteSource,
                        SYSTEMVALUE inputBlendFactor,
                        uint32 inputSize)
{
  uint8* input8 = inputPointer;
  uint16* input16 = (uint16*)inputPointer;
  uint32 outputPos;
  uint32 x;
  uint32 start;
  uint32 end;
  SYSTEMVALUE step;
  SYSTEMVALUE average;
  SYSTEMVALUE total;
  SYSTEMVALUE inputPos = 0;
  SYSTEMVALUE fractional = 0;

  HQASSERT(outputWidth <= inputWidth, 
           "averageLine - output must be smaller than input");

  step = inputWidth / (SYSTEMVALUE)outputWidth;

  for (outputPos = 0; outputPos < outputWidth; outputPos ++) {

    /* Add start partial pixel */
    total = 1.0 - fractional;
    if (inputSize == 1) {
      average = input8[((int32)inputPos) * interleaveStep] * total;
    }
    else {
      average = input16[((int32)inputPos) * interleaveStep] * total;
    }

    /* Add all the whole pixels. If inputPos laid on an integer, we would have 
       already added it, so the +1 always works */
    start = (uint32)(inputPos + 1);
    end = (uint32)(inputPos + step);

    if (inputSize == 1) {
      for (x = start; x < end; x ++) {
        average += input8[x * interleaveStep];
      }
    }
    else {
      for (x = start; x < end; x ++) {
        average += input16[x * interleaveStep];
      }
    }

    /* Add the number of whole pixels we just added to the average to 
       'total' */
    total += end - start;
    inputPos += step;
    
    /* Add end partial pixel */
    fractional = inputPos - (int32)inputPos;
    total += fractional;

    if ((fractional > 0) && (x < inputWidth)) {
      if (inputSize == 1) {
        average += input8[x * interleaveStep] * fractional;
      }
      else {
        average += input16[x * interleaveStep] * fractional;
      }
    }

    /* Write output */
    average = (average / total) * inputBlendFactor;

    /* Limit output (Shouldn't be necessary...) */
    if (inputSize == 1) {
      if (average > MAX_8_BIT_TINT) {
        average = MAX_8_BIT_TINT;
      }
    }
    else {
      if (average > MAX_16_BIT_TINT) {
        average = MAX_16_BIT_TINT;
      }
    }

    if (overwriteSource == 1) {
      output[outputPos * interleaveStep] = (uint32)average;
    }
    else {
      output[outputPos * interleaveStep] = output[outputPos * interleaveStep] + 
                                           (uint32)average;
    }     
  }
}

/* Normalise the input line, ie. divide each uint32 in 'input' by 'total'.
 * 'outputSize' can be set to 8 or 16, to support 8 or 16 bit images
 */
static void normaliseLine(uint32* line, 
                          uint32 width, 
                          SYSTEMVALUE total, 
                          uint32 outputSize)
{
  uint32 x;
  uint32 result;
  SYSTEMVALUE scale;
  
  HQASSERT(line != NULL, "");
  HQASSERT(total != 0, "");
  
  scale = 1.0 / total;

  switch (outputSize)
  {
    case 1:
      {	
        uint8* target = (uint8*)line;
        
        for (x = 0; x < width; x ++) {
          result = (uint32)(line[x] * scale);
          
          if (result <= MAX_8_BIT_TINT) {
            target[x] = (uint8)result;
          }
          else {
            HQFAIL("normaliseLine - strange value");
            target[x] = MAX_8_BIT_TINT;
          }
        }
      }
      break;
    
    case 2:
      {	
        uint16* target = (uint16*)line;

        for (x = 0; x < width; x ++) {
          result = (uint32)(line[x] * scale);
          
          if (result <= MAX_16_BIT_TINT) {
            target[x] = (uint16)result;
          }
          else {
            HQFAIL("normaliseLine - strange value");
            target[x] = MAX_16_BIT_TINT;
          }
        }
      }
      break;
      
    default:
      HQFAIL("normaliseLine - Unsupported outputSize");
  }
}

/* EOF avrag_if.c */

/* Log stripped */
