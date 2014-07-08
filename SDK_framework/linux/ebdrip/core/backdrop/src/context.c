/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:context.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * For creating and managing the compositing contexts.
 */

#include "core.h"
#include "backdroppriv.h"
#include "composite.h"
#include "swerrors.h"

static resource_source_t contextSource;

static Bool bd_contextMakePool(resource_source_t *source,
                               resource_pool_t *pool);

void bd_contextGlobals(void)
{
  resource_source_t init = {0};

  contextSource = init;
  contextSource.refcount = 1; /* Never release this source */
  contextSource.make_pool = bd_contextMakePool;
  /* No mm_pool method, so contexts will never be released in low memory. */
  contextSource.mm_pool = NULL;
  NAME_OBJECT(&contextSource, RESOURCE_SOURCE_NAME);
}

Bool bd_contextInit(void)
{
  return resource_source_low_mem_register(&contextSource);
}

void bd_contextFinish(void)
{
  resource_source_low_mem_deregister(&contextSource);
}

static void bd_contextDestroy(resource_pool_t *pool,
                              resource_entry_t *entry)
{
  CompositeContext *context;
  size_t bytes;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME);
  UNUSED_PARAM(resource_pool_t*, pool);
  HQASSERT(entry != NULL, "No resource entry");

  context = entry->resource;
  bytes = context->inCompsMax * sizeof(COLORVALUE);

  if ( context->fixedResourceIds != NULL )
    bd_resourceFree(context->fixedResourceIds,
                    context->nMaxResources * sizeof(*context->fixedResourceIds));
  if ( context->fixedResources != NULL )
    bd_resourceFree(context->fixedResources,
                    context->nMaxResources * sizeof(*context->fixedResources));
  if ( context->result.colorBuf != NULL )
    bd_resourceFree(context->result.colorBuf, bytes);
  if ( context->background.colorBuf != NULL )
    bd_resourceFree(context->background.colorBuf, bytes);
  if ( context->backgroundForShape.colorBuf != NULL )
    bd_resourceFree(context->backgroundForShape.colorBuf, bytes);
  if ( context->source.colorBuf != NULL )
    bd_resourceFree(context->source.colorBuf, bytes);
  if ( context->source.overprintFlagsBuf != NULL )
    bd_resourceFree(context->source.overprintFlagsBuf,
                    context->inCompsMax * sizeof(blit_channel_state_t));

  bd_coalesceFree(&context->coalesce);

  cceDelete(&context->source.cce);

  pcl_stateFree(&context->pcl);

  bd_freeBackdropReserve(&context->backdropReserve);

  bd_resourceFree(context, sizeof(CompositeContext));

  entry->resource = NULL;
}

#define COMPOSITE_CONTEXT_KEY_NAME "Composite Context Key"

typedef struct {
  uint32 nMaxResources;
  uint32 inCompsMax;
  uint32 width;
  size_t backdropReserveSize;
  OBJECT_NAME_MEMBER
} CompositeContextKey;

/**
 * CompositeContext encompasses all the workspace and state required to
 * composite the backdrops.  It is passed into the backdrop compositing
 * functions and one CompositeContext is used per compositing thread.
 */
static Bool bd_contextNew(resource_pool_t *pool,
                          resource_entry_t *entry,
                          mm_cost_t cost)
{
  CompositeContextKey *key;
  CompositeContext *context, init = {0};
  size_t bytes, i;

  UNUSED_PARAM(mm_cost_t, cost) ;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);
  VERIFY_OBJECT(pool->source, RESOURCE_SOURCE_NAME);
  HQASSERT(entry != NULL, "No resource entry");

  key = (CompositeContextKey*)pool->key;
  VERIFY_OBJECT(key, COMPOSITE_CONTEXT_KEY_NAME);

  context = bd_resourceAllocCost(sizeof(CompositeContext), cost);
  if ( context == NULL )
    return error_handler(VMERROR);

  *context = init;
  entry->resource = context;

  context->nMaxResources = key->nMaxResources;
  context->nFixedResources = 0;
  context->fixedResourceIds = bd_resourceAllocCost(context->nMaxResources *
                                                   sizeof(*context->fixedResourceIds), cost);
  context->fixedResources = bd_resourceAllocCost(context->nMaxResources *
                                                 sizeof(*context->fixedResources), cost);
  bbox_clear(&context->requestBounds);
  context->doCompositing = FALSE;
  context->inCompsMax = key->inCompsMax;
  bbox_clear(&context->bandBounds);
  bytes = key->inCompsMax * sizeof(COLORVALUE);
  context->result.colorBuf = bd_resourceAllocCost(bytes, cost);
  context->background.colorBuf = bd_resourceAllocCost(bytes, cost);
  context->backgroundForShape.colorBuf = bd_resourceAllocCost(bytes, cost);
  context->source.colorBuf = bd_resourceAllocCost(bytes, cost);
  context->source.overprintFlagsBuf =
    bd_resourceAllocCost(key->inCompsMax * sizeof(blit_channel_state_t), cost);

  if ( context->fixedResourceIds == NULL ||
       context->fixedResources == NULL ||
       context->result.colorBuf == NULL ||
       context->background.colorBuf == NULL ||
       context->backgroundForShape.colorBuf == NULL ||
       context->source.colorBuf == NULL ||
       context->source.overprintFlagsBuf == NULL ) {
    bd_contextDestroy(pool, entry);
    return error_handler(VMERROR);
  }

  for ( i = 0; i < context->nMaxResources; ++i ) {
    context->fixedResourceIds[i] = TASK_RESOURCE_ID_INVALID;
    context->fixedResources[i] = NULL;
  }

  if ( !bd_coalesceNew(key->width, cost, &context->coalesce) ) {
    bd_contextDestroy(pool, entry);
    return FALSE;
  }

  context->source.cce = cceNew(resourcePool);
  if ( context->source.cce == NULL ) {
    bd_contextDestroy(pool, entry);
    return FALSE;
  }

  if ( pclGstateIsEnabled() &&
       !pcl_stateNew(key->inCompsMax, cost, &context->pcl) ) {
    bd_contextDestroy(pool, entry);
    return FALSE;
  }

  bd_allocBackdropReserve(key->backdropReserveSize, TRUE,
                          &context->backdropReserve);

  return TRUE;
}

static Bool bd_contextCompare(resource_pool_t *pool,
                              resource_key_t key)
{
  CompositeContextKey *key1, *key2;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);

  key1 = (CompositeContextKey*)pool->key;
  VERIFY_OBJECT(key1, COMPOSITE_CONTEXT_KEY_NAME);

  key2 = (CompositeContextKey*)key;
  VERIFY_OBJECT(key2, COMPOSITE_CONTEXT_KEY_NAME);

  return (key1->nMaxResources == key2->nMaxResources &&
          key1->inCompsMax == key2->inCompsMax &&
          key1->width == key2->width &&
          key1->backdropReserveSize == key2->backdropReserveSize);
}

static void bd_contextDestroyKey(resource_pool_t *pool)
{
  CompositeContextKey *key;

  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);

  key = (CompositeContextKey*)pool->key;
  VERIFY_OBJECT(key, COMPOSITE_CONTEXT_KEY_NAME);

  bd_resourceFree(key, sizeof(CompositeContextKey));
  pool->key = (resource_key_t)NULL;
}

static Bool bd_contextMakePool(resource_source_t *source,
                               resource_pool_t *pool)
{
  CompositeContextKey *key, *keyCopy;

  UNUSED_PARAM(resource_source_t *, source);
  VERIFY_OBJECT(pool, RESOURCE_POOL_NAME);

  pool->alloc = bd_contextNew;
  pool->free = bd_contextDestroy;
  pool->compare = bd_contextCompare;
  /* No low-memory reclaimation at the moment. */
  pool->finish = bd_contextDestroyKey;

  key = (CompositeContextKey*)pool->key;
  VERIFY_OBJECT(key, COMPOSITE_CONTEXT_KEY_NAME);

  /* Key is initialised to the value passed into resource_pool_get(), which is
     off the C stack.  Make a copy of the key which will last the lifetime of
     the pool.  This key is freed by bd_contextDestroyKey. */
  if ( (keyCopy = bd_resourceAlloc(sizeof(CompositeContextKey))) == NULL )
    return error_handler(VMERROR);

  keyCopy->nMaxResources = key->nMaxResources;
  keyCopy->inCompsMax = key->inCompsMax;
  keyCopy->width = key->width;
  keyCopy->backdropReserveSize = key->backdropReserveSize;
  NAME_OBJECT(keyCopy, COMPOSITE_CONTEXT_KEY_NAME);

  pool->key = (resource_id_t)keyCopy;

  return TRUE;
}

Bool bd_contextPoolGet(resource_requirement_t *req, uint32 nMaxResources,
                       uint32 inCompsMax, uint32 width,
                       size_t backdropReserveSize)
{
  resource_pool_t *pool;
  requirement_node_t *expr;
  CompositeContextKey key;

  HQASSERT(IS_INTERPRETER(), "Setting up backdrop context pool too late");

  key.nMaxResources = nMaxResources;
  key.inCompsMax = inCompsMax;
  key.width = width;
  key.backdropReserveSize = backdropReserveSize;
  NAME_OBJECT(&key, COMPOSITE_CONTEXT_KEY_NAME);

  if ( (pool = resource_pool_get(&contextSource,
                                 TASK_RESOURCE_COMPOSITE_CONTEXT,
                                 (resource_key_t)&key)) == NULL )
    return FALSE;

  expr = requirement_node_find(req, REQUIREMENTS_BAND_GROUP);
  HQASSERT(expr != NULL, "No band values resource expression");

  /* The new pool reference acquired here is transferred to request. */
  if ( !resource_requirement_set_pool(req, TASK_RESOURCE_COMPOSITE_CONTEXT,
                                      pool) ||
       !requirement_node_setmin(expr, TASK_RESOURCE_COMPOSITE_CONTEXT, 1) )
    return FALSE;

  return TRUE;
}

/* Log stripped */
