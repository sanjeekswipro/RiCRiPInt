/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:resource.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#ifndef __BACKDROPRESOURCE_H__
#define __BACKDROPRESOURCE_H__

#include "block.h"

void bd_resourceGlobals(void);
Bool bd_resourceInit(void);
void bd_resourceFinish(void);
Bool bd_resourcePoolGet(resource_requirement_t *req,
                        uint32 nTables, size_t tableSize,
                        uint32 blockHeight);
Bool bd_resourcePoolSetMinMax(resource_requirement_t *req,
                              uint32 nMinResources, uint32 nMaxResources);
size_t bd_resourceSize(uint32 nTables, size_t tableSize, uint32 blockHeight);
BackdropResource *bd_resourceGet(const Backdrop *shared,
                                 const CompositeContext *context,
                                 uint32 bx, uint32 by);
void bd_resourceRelease(BackdropResource *resource, resource_id_t id);
void bd_resourceSwap(BackdropResource *resource1, BackdropResource *resource2);
BackdropBlock *bd_resourceBlock(BackdropResource *resource);
uint8 *bd_resourceData(BackdropResource *resource, uint16 *dataBytes);
BackdropLine *bd_resourceLines(BackdropResource *resource, uint16 *linesBytes);
BackdropTable **bd_resourceTables(BackdropResource *resource);

#endif /* protection for multiple inclusion */

/* Log stripped */
