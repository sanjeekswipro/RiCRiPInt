/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:mtchptrn.c(EBDSDK_P.1) $
 * $Id: src:mtchptrn.c,v 1.9.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Helper object for the MaskScaler object - produces the set of match 
 * patterns used by the MaskScaler, given a set of easy-to-edit templates
 */

#include "core.h"
#include "mtchptrn.h"

#include "swerrors.h"
#include "mm.h"

/* --Private macros-- */

#define ADD_TEMPLATE_COUNT 14
#define REMOVE_TEMPLATE_COUNT 8

/* --Private datatypes-- */

typedef struct PatternTemplate_s {
  uint8 setPattern[25];
  uint8 dontCareMask[25];
  uint8 targetData[16];
  uint32 createRotations;
  uint32 createMirror;
}PatternTemplate;

/* --Private data-- */

STATIC uint32 addPatternCount;
STATIC uint32 addPatternRefCount = 0; 
STATIC uint32 removePatternCount;
STATIC uint32 removePatternRefCount = 0;

STATIC MatchPattern *addCandidates = NULL;
STATIC MatchPattern *removeCandidates = NULL;

/* --Private data prototypes-- */

extern PatternTemplate addTemplates[ADD_TEMPLATE_COUNT];
extern PatternTemplate removeTemplates[REMOVE_TEMPLATE_COUNT];

/* --Private prototypes-- */

STATIC void freeAddPatterns(void);
STATIC void freeRemovePatterns(void);
STATIC MatchPattern* produce(PatternTemplate* templates, 
                             uint32 count, 
                             uint32* patternsProduced);
STATIC MatchPattern* createPatternFromTemplate(PatternTemplate* tplate, 
                                               MatchPattern* target);
STATIC void initializePattern(MatchPattern* target, PatternTemplate* tplate);

/* --Public methods-- */

/* Return a list of the add-candidate patterns
 */
MatchPattern* matchPatternAddCandidates(uint32* count)
{
  HQASSERT(count != NULL, 
           "matchPatternAddCandidates - 'count' parameter is NULL");

  addPatternRefCount ++;
  
  if (addCandidates == NULL) {
    addCandidates = produce(addTemplates, ADD_TEMPLATE_COUNT, 
                            &addPatternCount);
  }
  
  if (addCandidates == NULL) {
    addPatternCount = 0;
  }

  count[0] = addPatternCount;
  return addCandidates;
}

/* Release the addCandidates. The internal cache will be freed when there are 
 * no references to it
 */
void matchPatternReleaseAddCandidates(void)
{
  HQASSERT(addPatternRefCount > 0, 
           "matchPatternReleaseAddCandidates - addCandidate refcount is "
           "already zero");

  addPatternRefCount --;

  if (addPatternRefCount == 0) {
    freeAddPatterns();
  }
}

/* Return a list of the remove-candidate patterns
 */
MatchPattern* matchPatternRemoveCandidates(uint32* count)
{
  HQASSERT(count != NULL, 
           "matchPatternRemoveCandidates - 'count' parameter is NULL");

  removePatternRefCount ++;
  
  if (removeCandidates == NULL) {
    removeCandidates = produce(removeTemplates, REMOVE_TEMPLATE_COUNT, 
                               &removePatternCount);
  }

  if (removeCandidates == NULL) {
    removePatternCount = 0;
  }

  count[0] = removePatternCount;
  return removeCandidates;
}

/* Release the addCandidates. The internal cache will be freed when there are
 * no references to it
 */
void matchPatternReleaseRemoveCandidates(void)
{
  HQASSERT(removePatternRefCount > 0, 
           "matchPatternReleaseRemoveCandidates - removeCandidate refcount is "
           "already zero");

  removePatternRefCount --;

  if (removePatternRefCount == 0) {
    freeRemovePatterns();
  }
}

/* --Private methods-- */

/* Free the addPattern list
 */
STATIC void freeAddPatterns(void)
{
  HQASSERT(addPatternRefCount == 0, "freeAddPatterns - refcount is non-zero");

  if (addCandidates != NULL) {
    mm_free(mm_pool_temp, addCandidates, 
            sizeof(MatchPattern) * addPatternCount);
  }

  addCandidates = NULL;
  addPatternCount = 0;
}

/* Free the removePattern list
 */
STATIC void freeRemovePatterns(void)
{
  HQASSERT(removePatternRefCount == 0, 
           "freeRemovePatterns - refcount is non-zero");

  if (removeCandidates != NULL) {
    mm_free(mm_pool_temp, removeCandidates, 
            sizeof(MatchPattern) * removePatternCount);
  }

  removeCandidates = NULL;
  removePatternCount = 0;
}

/* Given a set of PatternTemplates, produce a full set of MatchPatterns
 */
STATIC MatchPattern* produce(PatternTemplate* templates, 
                             uint32 count, 
                             uint32* patternsProduced)
{
  uint32 i;
  uint32 angleCount;
  MatchPattern *list;
  MatchPattern *listPointer;

  HQASSERT(templates != NULL, "produce - 'templates' parameter is NULL");
  HQASSERT(patternsProduced != NULL, 
           "produce - 'patternsProduced parameter is NULL");

  /* Count how many patterns we are going to produce so we can reserve 
  storage for them in one go */
  patternsProduced[0] = 0;
  for (i = 0; i < count; i ++) {
    if (templates[i].createRotations == 1) {
      angleCount = 4;
    }
    else {
      angleCount = 1;
    }

    if (templates[i].createMirror == 1) {
      patternsProduced[0] += angleCount * 2;
    }
    else {
      patternsProduced[0] += angleCount;
    }
  }

  /* Allocate pattern list */
  list = (MatchPattern*)mm_alloc(mm_pool_temp, 
                                 sizeof(MatchPattern) * patternsProduced[0], 
                                 MM_ALLOC_CLASS_MATCH_PATTERN);
  listPointer = list;
  if (list == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  /* Create each pattern */
  for (i = 0; i < count; i ++) {
    listPointer = createPatternFromTemplate(&templates[i], listPointer);
  }

  HQASSERT(listPointer == (&list[patternsProduced[0]]), 
           "MatchPattern::produce - didn't allocate enough storage for the "
           "patterns produced");
  
  return list;
}

/* Create a pattern, and possibly rotated and mirrored versions of it, from 
 * the passed template, into the passed MatchPattern pointer, returning a new
 * pointer incremented past the produced patterns
 */
STATIC MatchPattern* createPatternFromTemplate(PatternTemplate* tplate, 
                                               MatchPattern* target)
{
  int32 rotationMatrices[] = { 
          /* 0 degrees */1, 0, 0, 1, /* 90 degrees */0, 1, -1, 0, 
          /* 180 degrees */-1, 0, 0, -1, /* 240 degrees */0, -1, 1, 0
        };
  int32 *matrix;
  int32 x;
  int32 y;
  int32 tX;
  int32 tY;
  int32 xOff;
  int32 yOff;
  uint32 angle;
  uint32 angleCount;
  PatternTemplate temp;
  PatternTemplate mirror;

  HQASSERT(tplate != NULL, 
           "createPatternFromTemplate - 'tplate' parameter is NULL");
  HQASSERT(target != NULL, 
           "createPatternFromTemplate - 'target' parameter is NULL");

  if (tplate->createRotations == 1) {
    angleCount = 4;
  }
  else {
    angleCount = 1;
  }

  for (angle = 0; angle < angleCount; angle ++) {
    matrix = &rotationMatrices[4 * angle];

    /* Determin the offset required to translate the rotated coords back into
    the bounds of the pattern */
    if ((matrix[0] + matrix[1]) < 0) {
      xOff = 1;
    }
    else {
      xOff = 0;
    }
    
    if ((matrix[2] + matrix[3]) < 0) {
      yOff = 1;
    }
    else {
      yOff = 0;
    }

    /* Transform the patterns */
    for (y = 0; y < 5; y ++) {
      for (x = 0; x < 5; x ++) {
        tX = (x * matrix[0] + y * matrix[1]) + (4 * xOff);
        tY = (x * matrix[2] + y * matrix[3]) + (4 * yOff);
        
        temp.setPattern[tX + (tY * 5)] = tplate->setPattern[x + (y * 5)];
        temp.dontCareMask[tX + (tY * 5)] = tplate->dontCareMask[x + (y * 5)];
      }
    }
    
    /* Transform the output image pattern */
    for (y = 0; y < 4; y ++) {
      for (x = 0; x < 4; x ++) {
        tX = (x * matrix[0] + y * matrix[1]) + (3 * xOff);
        tY = (x * matrix[2] + y * matrix[3]) + (3 * yOff);
        
        temp.targetData[tX + (tY * 4)] = tplate->targetData[x + (y * 4)];
      }
    }
    
    initializePattern(target, &temp);
    target ++;

    /* Create the mirrored pattern if required */
    if (tplate->createMirror == 1) {
      for (y = 0; y < 5; y ++) {
        for (x = 0; x < 5; x ++) {
          mirror.setPattern[(4 - x) + (y * 5)] = temp.setPattern[x + (y * 5)];
          mirror.dontCareMask[(4 - x) + (y * 5)] = temp.dontCareMask[x + (y * 5)];
          if ((x < 4) && (y < 4)) {
            mirror.targetData[(3 - x) + (y * 4)] = temp.targetData[x + (y * 4)];
          }
        }
      }

      initializePattern(target, &mirror);
      target ++;
    }
  }

  return target;  
}

/* Initialize the passed MatchPattern using the passed PatternTemplate
 */
STATIC void initializePattern(MatchPattern* target, PatternTemplate* tplate)
{
  uint8 *sP = tplate->setPattern;
  uint8 *dCM = tplate->dontCareMask;
  uint32 i;
  uint32 set = 0;
  uint32 mask = 0;
  uint32 data = 0;

  HQASSERT(tplate != NULL, "initializePattern - 'tplate' parameter is NULL");
  HQASSERT(target != NULL, "initializePattern - 'target' parameter is NULL");

  /*
  The mapping of the bits in the match pattern and don't care mask int's to 
  the source pixels is thus:
  
  | 0| 5|10|15|20|
  | 1| 6|11|16|21|  Where bit 12 is super-pixel currently being tested for 
  | 2| 7|12|17|22|  addition or removal of pixels. This pattern allows for 
  | 3| 8|13|18|23|  nice optimisiation of pattern construction - the pattern 
  | 4| 9|14|19|24|  of source pixels only needs to be fully built once each 
  
  line, across the span it can be shifted to the right, and have the upper 
  bits (20-24) read from the image. */

  /* Init set pattern and the don't care mask */
  for (i = 0; i < 5; i ++) {
    set |= (sP[0] | (sP[1] << 5) | (sP[2] << 10) | (sP[3] << 15) | 
           (sP[4] << 20)) << i;
    mask |= (dCM[0] | (dCM[1] << 5) | (dCM[2] << 10) | (dCM[3] << 15) | 
            (dCM[4] << 20)) << i;
    sP += 5;
    dCM += 5;
  }

  /*
  The target data is a simple bit to pixel mapping:

  | 0| 1| 2| 3|
  | 4| 5| 6| 7|
  | 8| 9|10|11|
  |12|13|14|15|   
  
  This layout allows rows of data to be easily copied into the output buffer */

  /* Copy the target data */
  for (i = 0; i < 16; i ++) {
    data |= tplate->targetData[i] << (15 - i);
  }
   
  target->setPattern = set;
  target->dontCareMask = mask;
  target->targetData = data;
}

/* --Private data-- */

/* See image filter doc in notes for explanation what these things are */
PatternTemplate addTemplates[ADD_TEMPLATE_COUNT] = {
  { /* 1a */
    {
      0,0,0,0,0,
      0,1,0,0,1,
      0,1,0,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,1,1,1,
      0,1,1,1,1,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      0,0,0,1,
      0,0,1,1
    },
    1, 1
  },
  { /* 1b */
    {
      0,1,0,0,1,
      0,1,0,1,0,
      0,1,0,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,1,1,1,1,
      0,1,1,1,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,1,
      0,0,0,1,
      0,0,1,1
    },
    1, 1
  },
  { /* 1c */
    {
      0,0,0,0,1,
      0,1,0,1,0,
      0,1,0,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      1,1,1,1,1,
      0,1,1,1,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,1,
      0,0,0,1,
      0,0,1,1
    },
    1, 1
  },
  { /* 2a */
    {
      0,0,0,0,0,
      0,1,0,1,0,
      0,1,0,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      0,0,0,0,
      0,0,0,0
    },
    1, 0
  },
  { /* 2b */
    {
      0,0,0,0,0,
      0,1,0,1,0,
      0,1,0,1,0,
      0,1,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      0,0,0,1,
      0,0,1,1
    },
    1, 1
  },
  { /* 3 */
    {
      0,0,0,0,0,
      0,0,0,0,0,
      0,1,0,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      1,1,1,1,1,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      0,0,0,0,
      0,0,0,0
    },
    1, 0
  },
  { /* 4 */
    {
      0,0,0,1,0,
      0,0,0,1,0,
      0,0,0,1,0,
      1,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,1,1,0,
      0,0,1,1,0,
      1,1,1,1,0,
      1,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      0,0,0,0,
      0,0,0,0
    },
    1, 0
  },
  { /* 4.5a */
    {
      0,0,0,0,0,
      0,0,0,1,0,
      0,0,0,1,0,
      0,1,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,0,1,1,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,1,
      0,0,0,1,
      0,0,1,1,
      1,1,1,1
    },
    1, 0
  },
  { /* 4.5b */
    {
      0,0,0,0,0,
      0,0,0,1,0,
      0,0,0,1,0,
      0,1,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,1,1,0,
      0,0,1,1,0,
      1,1,1,1,0,
      1,1,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,1,
      0,0,0,1,
      0,0,1,1,
      1,1,1,1
    },
    1, 0
  },
  { /* 4.5c */
    {
      0,0,0,0,0,
      0,0,0,1,0,
      0,0,0,1,0,
      1,1,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,1,1,0,
      0,0,1,1,0,
      1,1,1,1,0,
      1,1,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,1,
      0,0,0,1,
      0,0,1,1
    },
    1, 1
  },
  { /* 5 */
    {
      0,0,0,0,0,
      0,0,1,0,0,
      0,1,0,1,0,
      0,0,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,0,1,0,0,
      0,1,1,1,0,
      0,0,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      0,0,0,0,
      0,0,0,0
    },
    0, 0
  },
  { /* 6 */
    {
      0,0,0,0,0,
      0,0,0,0,0,
      0,1,0,0,0,
      0,0,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,1,1,0,0,
      0,1,1,1,0,
      0,0,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      1,0,0,0,
      1,1,1,0
    },
    1, 1
  },
  { /* 7 */
    {
      0,0,0,0,0,
      0,0,0,0,0,
      0,1,0,1,0,
      0,0,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,0,1,0,0,
      0,1,1,1,0,
      0,0,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      1,0,0,1,
      1,1,1,1
    },
    1, 0
  },
  { /* 8 */
    {
      0,0,0,0,0,
      0,0,0,0,0,
      0,1,0,0,0,
      0,0,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,1,1,0,0,
      0,1,1,1,0,
      0,0,1,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,
      0,0,0,0,
      1,0,0,0,
      1,1,0,0
    },
    1, 0
  }
};

PatternTemplate removeTemplates[REMOVE_TEMPLATE_COUNT] =
{
  { /* 0.2 */
    {
      0,1,0,0,0,
      0,0,1,0,0,
      0,0,1,1,0,
      0,0,0,0,1,
      0,0,0,0,0
    },
    {
      0,1,0,0,0,
      0,1,1,0,0,
      0,1,1,1,0,
      0,1,1,1,1,
      0,0,0,0,0
    },
    {
      1,1,1,1,
      0,1,1,1,
      0,1,1,1,
      0,0,0,1
    },
    1, 0
  },
  { /* 0.2a */
    {
      0,1,0,0,0,
      0,0,1,0,0,
      0,0,1,1,0,
      0,0,0,0,0,
      0,0,0,0,0
    },
    {
      0,1,0,0,0,
      0,1,1,0,0,
      0,1,1,1,0,
      0,1,1,1,1,
      0,0,0,0,0
    },
    {
      1,1,1,1,
      0,1,1,1,
      0,1,1,1,
      0,0,1,1
    },
    1, 1
  },
  { /* 0.5 */
    {
      0,0,0,0,0,
      0,0,1,0,0,
      0,0,1,1,0,
      0,0,0,1,0,
      0,0,0,0,0
    },
    {
      0,1,1,0,0,
      0,1,1,0,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      1,1,1,1,
      1,1,1,1,
      1,1,1,1,
      0,1,1,1
    },
    1, 1
  },
  { /* 0 */
    {
      0,0,0,0,0,
      0,0,0,0,0,
      0,0,1,0,0,
      1,1,1,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,1,1,1,0,
      1,1,1,1,0,
      1,1,1,1,0,
      0,0,0,0,0
    },
    {
      1,1,1,1,
      1,1,1,1,
      1,1,1,1,
      1,1,1,1
    },
    1, 1
  },
  { /* 1 */
    {
      0,0,0,0,0,
      0,1,0,1,0,
      0,0,1,0,0,
      0,0,0,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,1,0,1,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      1,1,1,1,
      1,1,1,1,
      1,1,1,1,
      0,1,1,0
    },
    1, 0
  },
  { /* 2 */
    {
      0,0,0,0,0,
      0,0,0,0,0,
      0,0,1,0,0,
      0,0,0,1,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,0,1,1,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      1,1,1,0,
      1,1,1,1,
      1,1,1,1,
      0,1,1,1
    },
    1, 0
  },
  { /* 4 */
    {
      0,0,0,0,0,
      0,0,0,1,0,
      0,1,1,0,0,
      0,0,0,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,0,0,1,0,
      0,1,1,1,0,
      0,1,1,1,0,
      0,0,0,0,0
    },
    {
      1,1,1,1,
      1,1,1,1,
      1,1,1,0,
      1,0,0,0
    },
    1, 1
  },
  { /* 5 */
    {
      0,0,0,0,0,
      0,0,0,0,0,
      0,0,1,0,0,
      0,1,0,0,0,
      0,0,0,0,0
    },
    {
      0,0,0,0,0,
      0,1,1,0,0,
      0,1,1,0,0,
      0,1,0,0,0,
      0,0,0,0,0
    },
    {
      0,1,1,1,
      1,1,1,1,
      1,1,1,1,
      1,1,1,1
    },
    1, 1
  }
};

void init_C_globals_mtchptrn(void)
{
  addPatternCount = 0;
  addPatternRefCount = 0; 
  removePatternCount = 0;
  removePatternRefCount = 0;
  addCandidates = NULL;
  removeCandidates = NULL;
}

/* Log stripped */
