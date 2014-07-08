/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:garray.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Growable array interface.
 */

#ifndef __GARRAY_H__
#define __GARRAY_H__  (1)

#include "mm.h"
#include "objnamer.h"

/* Innards of a growable array.
 *
 * This is public so that instances can easily be embedded in structs or on the
 * heap.  Operations on the growable array should still be through the
 * functional interface.  You have been warned.
 */
typedef struct GROWABLE_ARRAY {
  uint8*    p_memory;   /* Array memory */
  size_t    length;     /* Current length of array */
  mm_pool_t mm_pool;    /* MM pool holding growable array */
  OBJECT_NAME_MEMBER
} GROWABLE_ARRAY;

/* Allocate and initialise a new growable array.
 */
extern
GROWABLE_ARRAY* garr_alloc(
  mm_pool_t mm_pool);

/* Initialise a growable array.
 *
 * This should not be called for an allocated growable array.
 */
extern
void garr_init(
  GROWABLE_ARRAY* p_garr,
  mm_pool_t       mm_pool);

/* Give memory to growable array as initial memory.
 *
 * The growable array will be emptied of any prior data.  The memory must have
 * at least 1 byte.  The memory must be from the same memory pool as the
 * growable array is set up to use.
 */
extern
void garr_takeover(
  GROWABLE_ARRAY* p_garr,
  uint8*          p_mem,
  size_t          len);

/* Extend the length of a growable array. The new data is uninitialised.
 */
extern
Bool garr_extend(
  GROWABLE_ARRAY* p_garr,
  size_t          len);

/* Add data to a growable array.
 *
 * Returns true if array has been extended and the extra data added to it.
 * If the data cannot be added the original data is still valid.
 * The memory must have at least 1 byte.
 */
extern
Bool garr_append(
  GROWABLE_ARRAY* p_garr,
  uint8*          p_mem,
  size_t          len);

/* Get the current length of growable array.
 *
 * The length will valid until the growable array is extended or emptied.  Zero
 * will be returned for an empty array.
 */
extern
size_t garr_length(
  GROWABLE_ARRAY* p_garr);

/* Get pointer to start of array data.
 *
 * The pointer will valid until the growable array is extended or emptied.  NULL
 * will be returned for an empty array.
 */
extern
uint8* garr_data(
  GROWABLE_ARRAY* p_garr);

/* Empty a growable array of all its data.
 */
extern
void garr_empty(
  GROWABLE_ARRAY* p_garr);

/* Free an allocated growable array.
 *
 * Any data will be freed as well.
 */
extern
void garr_free(
  GROWABLE_ARRAY* p_garr);

#endif /* __GARRAY_H__ */

/*
* Log stripped */
