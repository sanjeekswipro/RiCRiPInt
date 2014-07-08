/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:resource.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * For creating and managing the full-sized backdrop block resources.
 */

#include "core.h"
#include "backdroppriv.h"
#include "composite.h"
#include "resource.h"
#include "swerrors.h"

struct BackdropResource {
  size_t            nTables, tableSize;
  uint16            dataBytes, linesBytes, tablesBytes;

  BackdropBlock    *block;
  uint8            *data;
  BackdropLine     *lines;
  BackdropTable   **tables;
};

static resource_source_t resourceSource;

static Bool bd_resourceMakePool(resource_source_t *source,
                                resource_pool_t *pool);

void bd_resourceGlobals(void)
{
  resource_source_t init = {0};

  resourceSource = init;
  resourceSource.refcount = 1; /* Never release this source */
  resourceSource.make_pool = bd_resourceMakePool;
  resourceSource.mm_pool = bd_resourcePool;
  NAME_OBJECT(&resourceSource, RESOURCE_SOURCE_NAME);
}

Bool bd_resourceInit(void)
{
  return resource_source_low_mem_register(&resourceSource);
}

void bd_resourceFinish(void)
{
  resource_source_low_mem_deregister(&resourceSource);
}

static void bd_resourceDestroy(resource_pool_t *pool,
                               resource_entry_t *entry)
{
  BackdropResource *resource;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME);
  UNUSED_PARAM(resource_pool_t*, pool);
  HQASSERT(entry != NULL, "No resource entry");

  resource = entry->resource;

  if ( resource->tables != NULL ) {
    uint32 i;
    for ( i = 0; i < resource->nTables; ++i ) {
      if ( resource->tables[i] != NULL )
        bd_resourceFree(resource->tables[i], resource->tableSize);
    }
    bd_resourceFree(resource->tables, resource->tablesBytes);
  }

  if ( resource->lines != NULL )
    bd_resourceFree(resource->lines, resource->linesBytes);

  if ( resource->data != NULL )
    bd_resourceFree(resource->data, resource->dataBytes);

  if ( resource->block != NULL )
    bd_resourceFree(resource->block, (int32)bd_blockSize());

  bd_resourceFree(resource, sizeof(BackdropResource));

  entry->resource = NULL;
}

#define BACKDROP_RESOURCE_KEY_NAME "Backdrop Resource Key"

typedef struct {
  uint32 nTables;
  size_t tableSize;
  uint32 blockHeight;
  OBJECT_NAME_MEMBER
} BackdropResourceKey;

static Bool bd_resourceNew(resource_pool_t *pool,
                           resource_entry_t *entry,
                           mm_cost_t cost)
{
  BackdropResourceKey *key;
  BackdropResource *resource, init = {0};
  uint32 i;

  UNUSED_PARAM(mm_cost_t, cost) ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME);
  HQASSERT(entry != NULL, "No resource entry");

  key = (BackdropResourceKey*)pool->key;
  VERIFY_OBJECT(key, BACKDROP_RESOURCE_KEY_NAME);

  resource = bd_resourceAllocCost(sizeof(BackdropResource), cost);
  if ( resource == NULL )
    return FALSE;

  *resource = init;
  entry->resource = resource;

  resource->block = bd_resourceAllocCost((int32)bd_blockSize(), cost);
  if ( resource->block == NULL ) {
    bd_resourceDestroy(pool, entry);
    return FALSE;
  }

  resource->nTables = key->nTables;
  resource->tableSize = key->tableSize;

  resource->dataBytes = CAST_SIGNED_TO_UINT16(BLOCK_DEFAULT_WIDTH *
                                              key->blockHeight);
  resource->data = bd_resourceAllocCost(resource->dataBytes, cost);
  if ( resource->data == NULL ) {
    bd_resourceDestroy(pool, entry);
    return FALSE;
  }

  resource->linesBytes = CAST_SIGNED_TO_UINT16(sizeof(BackdropLine) *
                                               key->blockHeight);
  resource->lines = bd_resourceAllocCost(resource->linesBytes, cost);
  if ( resource->lines == NULL ) {
    bd_resourceDestroy(pool, entry);
    return FALSE;
  }

  resource->tablesBytes = CAST_SIGNED_TO_UINT16(resource->nTables *
                                                sizeof(BackdropTable*));
  resource->tables = bd_resourceAllocCost(resource->tablesBytes, cost);
  if ( resource->tables == NULL ) {
    bd_resourceDestroy(pool, entry);
    return FALSE;
  }
  for ( i = 0; i < resource->nTables; ++i ) {
    resource->tables[i] = NULL;
  }
  for ( i = 0; i < resource->nTables; ++i ) {
    resource->tables[i] = bd_resourceAllocCost(resource->tableSize, cost);
    if ( resource->tables[i] == NULL ) {
      bd_resourceDestroy(pool, entry);
      return FALSE;
    }
  }

  return TRUE;
}

static Bool bd_resourcesCompare(resource_pool_t *pool,
                                resource_key_t key)
{
  BackdropResourceKey *key1, *key2;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);

  key1 = (BackdropResourceKey*)pool->key;
  VERIFY_OBJECT(key1, BACKDROP_RESOURCE_KEY_NAME);

  key2 = (BackdropResourceKey*)key;
  VERIFY_OBJECT(key2, BACKDROP_RESOURCE_KEY_NAME);

  return (key1->nTables == key2->nTables &&
          key1->tableSize == key2->tableSize &&
          key1->blockHeight == key2->blockHeight);
}

static size_t bd_resourceEntrySize(resource_pool_t *pool,
                                   const resource_entry_t *entry)
{
  BackdropResourceKey *key;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);
  UNUSED_PARAM(const resource_entry_t*, entry);

  key = (BackdropResourceKey*)pool->key;
  VERIFY_OBJECT(key, BACKDROP_RESOURCE_KEY_NAME);

  return bd_resourceSize(key->nTables, key->tableSize, key->blockHeight);
}

static void bd_resourceDestroyKey(resource_pool_t *pool)
{
  BackdropResourceKey *key;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);

  key = (BackdropResourceKey*)pool->key;
  VERIFY_OBJECT(key, BACKDROP_RESOURCE_KEY_NAME);

  bd_resourceFree(key, sizeof(BackdropResourceKey));
  pool->key = (resource_key_t)NULL;
}

static Bool bd_resourceMakePool(resource_source_t *source,
                                resource_pool_t *pool)
{
  BackdropResourceKey *key, *keyCopy;

  UNUSED_PARAM(resource_source_t *, source);
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);

  pool->alloc = bd_resourceNew;
  pool->free = bd_resourceDestroy;
  pool->compare = bd_resourcesCompare;
  pool->entry_size = bd_resourceEntrySize;
  pool->finish = bd_resourceDestroyKey;

  key = (BackdropResourceKey*)pool->key;
  VERIFY_OBJECT(key, BACKDROP_RESOURCE_KEY_NAME);

  /* Key is initialised to the value passed into resource_pool_get(), which is
     off the C stack.  Make a copy of the key which will last the lifetime of
     the pool.  This key is freed by bd_resourceDestroyKey. */
  if ( (keyCopy = bd_resourceAlloc(sizeof(BackdropResourceKey))) == NULL )
    return error_handler(VMERROR);

  keyCopy->nTables = key->nTables;
  keyCopy->tableSize = key->tableSize;
  keyCopy->blockHeight = key->blockHeight;
  NAME_OBJECT(keyCopy, BACKDROP_RESOURCE_KEY_NAME);

  pool->key = (resource_id_t)keyCopy;

  return TRUE;
}

Bool bd_resourcePoolGet(resource_requirement_t *req,
                        uint32 nTables, size_t tableSize,
                        uint32 blockHeight)
{
  resource_pool_t *pool;
  BackdropResourceKey key;

  HQASSERT(IS_INTERPRETER(), "Setting up backdrop resource pool too late");

  key.nTables = nTables;
  key.tableSize = tableSize;
  key.blockHeight = blockHeight;
  NAME_OBJECT(&key, BACKDROP_RESOURCE_KEY_NAME);

  if ( (pool = resource_pool_get(&resourceSource,
                                 TASK_RESOURCE_BACKDROP_RESOURCE,
                                 (resource_key_t)&key)) == NULL )
    return FALSE;

  /* The new pool reference acquired here is transferred to request. */
  if ( !resource_requirement_set_pool(req, TASK_RESOURCE_BACKDROP_RESOURCE,
                                      pool) )
    return FALSE;

  return TRUE;
}

Bool bd_resourcePoolSetMinMax(resource_requirement_t *req,
                              uint32 nMinResources, uint32 nMaxResources)
{
  requirement_node_t *expr;

  HQASSERT(IS_INTERPRETER(), "Setting up backdrop resource pool too late");

  expr = requirement_node_find(req, REQUIREMENTS_BAND_GROUP);
  HQASSERT(expr != NULL, "No band values resource expression");

  if ( !requirement_node_setmin(expr, TASK_RESOURCE_BACKDROP_RESOURCE,
                                nMinResources) ||
       !requirement_node_setmax(expr, TASK_RESOURCE_BACKDROP_RESOURCE,
                                nMaxResources) )
    return FALSE;

  return TRUE;
}

size_t bd_resourceSize(uint32 nTables, size_t tableSize, uint32 blockHeight)
{
  return (sizeof(BackdropResource) + bd_blockSize() +
          BLOCK_DEFAULT_WIDTH * blockHeight +
          sizeof(BackdropLine) * blockHeight +
          nTables * sizeof(BackdropTable*) +
          nTables * tableSize);
}

BackdropResource *bd_resourceGet(const Backdrop *backdrop,
                                 const CompositeContext *context,
                                 uint32 bx, uint32 by)
{
  BackdropResource *resource;
  uint32 index ;

  /* Re-base the index from absolute block space to relative to the current
     region set. */
  HQASSERT(bx >= context->baseX && by >= context->baseY,
           "Block is not within current region set") ;
  bx -= context->baseX ;
  by -= context->baseY ;

  HQASSERT(bx < context->xblock, "Block is not within current region set") ;
  index = bx + by * context->xblock ;

  HQASSERT(backdrop->depth < context->backdropDepth,
           "Backdrop depth more than current region set") ;
  index = backdrop->depth + context->backdropDepth * index;
  HQASSERT((resource_id_t)index == context->fixedResourceIds[index],
           "Resource index doesn't match resource ID") ;

  resource = context->fixedResources[index];
  HQASSERT(resource != NULL, "Failed to find a backdrop resource");

  return resource;
}

void bd_resourceRelease(BackdropResource *resource, resource_id_t id)
{
  task_resource_unfix(TASK_RESOURCE_BACKDROP_RESOURCE, id, resource);
}

void bd_resourceSwap(BackdropResource *resource1, BackdropResource *resource2)
{
  BackdropResource tmp = *resource1;

  HQASSERT(resource1->nTables == resource2->nTables &&
           resource1->tableSize == resource2->tableSize &&
           resource1->dataBytes == resource2->dataBytes &&
           resource1->linesBytes == resource2->linesBytes &&
           resource1->tablesBytes == resource2->tablesBytes,
           "Can't swap different sized resources");

  resource1->block = resource2->block;
  resource1->data = resource2->data;
  resource1->lines = resource2->lines;
  resource1->tables = resource2->tables;

  resource2->block = tmp.block;
  resource2->data = tmp.data;
  resource2->lines = tmp.lines;
  resource2->tables = tmp.tables;
}

BackdropBlock *bd_resourceBlock(BackdropResource *resource)
{
  return resource->block;
}

uint8 *bd_resourceData(BackdropResource *resource, uint16 *dataBytes)
{
  if ( dataBytes != NULL )
    *dataBytes = resource->dataBytes;
  return resource->data;
}

BackdropLine *bd_resourceLines(BackdropResource *resource, uint16 *linesBytes)
{
  if ( linesBytes != NULL )
    *linesBytes = resource->linesBytes;
  return resource->lines;
}

BackdropTable **bd_resourceTables(BackdropResource *resource)
{
  return resource->tables;
}

/* Log stripped */
