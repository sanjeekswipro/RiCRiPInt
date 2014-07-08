/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:block.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#ifndef __BLOCK_H__
#define __BLOCK_H__

#include "table.h"

/* Defined here to save a somewhat pointless api for something accessed and
   modified fairly generally. */
typedef struct BackdropLine {
  BackdropTable *table;
  uint16         offset;
  uint8          nRuns;
  uint8          repeat;
} BackdropLine;

void bd_blockInit(const Backdrop *backdrop, const CompositeContext *context,
                  uint32 bx, uint32 by);

size_t bd_blockSize(void);
uint32 bd_blockWidth(BackdropBlock *block);
uint32 bd_blockHeight(BackdropBlock *block);

uint8 *bd_blockData(BackdropBlock *block, uint32 yi);
void bd_setDataBytes(BackdropBlock *block, uint32 dataBytes);

BackdropLine *bd_blockLine(BackdropBlock *block, uint32 yi);
BackdropResource *bd_blockResource(BackdropBlock *block);

Bool bd_blockLoad(const Backdrop *backdrop, const CompositeContext *context,
                  uint32 bx, uint32 by, BackdropBlock *block);
size_t bd_blockPurgeSize(BackdropBlock *block);
Bool bd_blockPurge(const BackdropShared *shared, BackdropBlock **blockRef);
Bool bd_blockReclaim(const Backdrop *backdrop, BackdropBlock **pblock,
                     Bool bandclose, Bool result);
void bd_blockSwap(const Backdrop *backdrop1, const Backdrop *backdrop2,
                  uint32 bx, uint32 by);
void bd_blockCheck(BackdropBlock *block);
void bd_blockDebug(CompositeContext *context, Backdrop *backdrop,
                   BackdropBlock *block);

Bool bd_retainedBlockNew(Backdrop *backdrop, BackdropBlock *block,
                         int32 tableType, BackdropBlock **pblock);

Bool bd_isTouched(BackdropBlock *block);
void bd_setTouched(BackdropBlock *block, Bool isTouched);

Bool bd_isComplete(BackdropBlock *block);
void bd_setComplete(BackdropBlock *block, Bool isCompleted);

void bd_setPurgeable(const BackdropShared *shared, BackdropBlock *block,
                     Bool isPurgeable, Bool *before);

#define bd_isRLE(_line) ((_line)->nRuns > 0)
#define bd_setMap(_line) (_line)->nRuns = 0;
#define bd_setRLE(_line, _nRuns) (_line)->nRuns = CAST_UNSIGNED_TO_UINT8(_nRuns);

Bool bd_isRepeat(BackdropLine *line);
void bd_setRepeat(BackdropLine *line, Bool repeat);
int32 bd_findRepeatSource(BackdropLine *lines, uint32 yi);
void bd_lineAndDataRepeatSrc(BackdropBlock *block, uint32 yi,
                             BackdropLine **line, uint8 **data);

Bool bd_isStorageMemory(BackdropBlock *block);
Bool bd_isUniform(BackdropBlock *block);
void bd_setUniform(BackdropBlock *block);
BackdropTable *bd_uniformTable(BackdropBlock *block);

#endif /* protection for multiple inclusion */

/* Log stripped */
