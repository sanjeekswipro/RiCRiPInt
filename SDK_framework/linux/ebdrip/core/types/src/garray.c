/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!src:garray.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Growable array implementation.
 *
 * Thoughts for future development:
 * 1. Allocating memory and copying on every extension is not big and is not
 * clever.  Make the unit allocate in fixed size chunks (2, 4, 8, or 16k say)
 * and only reallocate when that is exceeded.  Can you say buddy allocator?
 * 2. Make the chunk size a property of each growable array so it can be tuned
 * based on its actual use.
 * 3. When chunks are implemented should it be possible to free off unused
 * space?  It shouldn't prevent it being extended again, but it will guarantee
 * a new allocation and copy.
 * 4. Should emptying a growable release all the memory, or shrink back to a
 * single chunk's worth?  Perhaps this should be an option - free all, free to
 * chunk, keep memory.
 */

#include "core.h"
#include "hqmemcpy.h"

#include "garray.h"


/* Object name */
#define GROWABLE_ARRAY_NAME  "Growable Array"


/* Allocate and initialise a new growable array. */
GROWABLE_ARRAY* garr_alloc(
  mm_pool_t mm_pool)
{
  GROWABLE_ARRAY* p_garr;

  p_garr = mm_alloc(mm_pool, sizeof(GROWABLE_ARRAY), MM_ALLOC_CLASS_GROWABLE_ARRAY);
  if ( p_garr != NULL ) {
    garr_init(p_garr, mm_pool);
  }
  return(p_garr);

} /* garr_alloc */


/* Set growable array memory pointer and length.
 *
 * For internal use only, bypasses null pointer and 0 length asserts in the
 * public interfaces.
 */
static
void garr_setmem(
  GROWABLE_ARRAY* p_garr,
  uint8*          p_mem,
  size_t          len)
{
  HQASSERT((p_garr != NULL),
           "garr_setmem: NULL growable pointer");
  HQASSERT((p_mem == NULL ? len == 0 : len > 0),
           "garr_setmem: invalid length for memory pointer");

  p_garr->p_memory = p_mem;
  p_garr->length = len;

} /* garr_setmem */


/* Initialise a growable array. */
void garr_init(
  GROWABLE_ARRAY* p_garr,
  mm_pool_t       mm_pool)
{
  HQASSERT((p_garr != NULL),
           "garr_init: NULL growable array pointer");

  NAME_OBJECT(p_garr, GROWABLE_ARRAY_NAME);

  garr_setmem(p_garr, NULL, 0);
  p_garr->mm_pool = mm_pool;

} /* garr_init */


/* Give memory to growable array as initial memory. */
void garr_takeover(
  GROWABLE_ARRAY* p_garr,
  uint8*          p_mem,
  size_t          len)
{
  HQASSERT((p_garr != NULL),
           "garr_takeover: NULL growable array pointer");
  HQASSERT((len > 0),
           "garr_takeover: invalid memory size");
  HQASSERT((p_mem != NULL),
           "garr_takeover: NULL memory pointer");

  VERIFY_OBJECT(p_garr, GROWABLE_ARRAY_NAME);

  garr_empty(p_garr);

  garr_setmem(p_garr, p_mem, len);

} /* garr_takeover */

/* See header for doc. */
Bool garr_extend(
  GROWABLE_ARRAY* p_garr,
  size_t          len)
{
  size_t  orig_length;
  size_t  new_length;
  uint8*  p_memory;

  HQASSERT((p_garr != NULL),
           "NULL growable array pointer");
  HQASSERT((len > 0),
           "invalid memory size");

  VERIFY_OBJECT(p_garr, GROWABLE_ARRAY_NAME);

  /* MPS does not currently have a realloc function.  Allocate larger block and
   * copy old and new content into it.
   */
  orig_length = garr_length(p_garr);
  new_length = orig_length + len;
  p_memory = mm_alloc(p_garr->mm_pool, new_length, MM_ALLOC_CLASS_GROWABLE_ARRAY);
  if ( p_memory == NULL ) {
    return(FALSE);
  }
  if ( orig_length > 0 ) {
    HqMemCpy(p_memory, garr_data(p_garr), orig_length);
  }
  garr_empty(p_garr);
  garr_setmem(p_garr, p_memory, new_length);

  return(TRUE);
}

/* See header for doc. */
Bool garr_append(
  GROWABLE_ARRAY* p_garr,
  uint8*          p_mem,
  size_t          len)
{
  size_t  orig_length;

  HQASSERT((p_garr != NULL),
           "NULL growable array pointer");
  HQASSERT((len > 0),
           "invalid memory size");
  HQASSERT((p_mem != NULL),
           "NULL memory pointer");

  VERIFY_OBJECT(p_garr, GROWABLE_ARRAY_NAME);

  orig_length = garr_length(p_garr);

  if (! garr_extend(p_garr, len))
    return(FALSE);

  HqMemCpy(garr_data(p_garr) + orig_length, p_mem, len);

  return(TRUE);

} /* garr_extend */


/* Get current length of growable array.
 */
size_t garr_length(
  GROWABLE_ARRAY* p_garr)
{
  HQASSERT((p_garr != NULL),
           "garr_length: NULL growable array pointer");

  VERIFY_OBJECT(p_garr, GROWABLE_ARRAY_NAME);

  return(p_garr->length);

} /* garr_length */


/* Get pointer to start of array data. */
uint8* garr_data(
  GROWABLE_ARRAY* p_garr)
{
  HQASSERT((p_garr != NULL),
           "garr_data: NULL growable array pointer");

  VERIFY_OBJECT(p_garr, GROWABLE_ARRAY_NAME);

  return(p_garr->p_memory);

} /* garr_data */


/* Empty a growable array of all its data.
 */
void garr_empty(
  GROWABLE_ARRAY* p_garr)
{
  uint8*  p_mem;

  HQASSERT((p_garr != NULL),
           "garr_empty: NULL growable array pointer");

  VERIFY_OBJECT(p_garr, GROWABLE_ARRAY_NAME);

  p_mem = garr_data(p_garr);
  if ( p_mem != NULL ) {
    mm_free(p_garr->mm_pool, p_mem, garr_length(p_garr));
    garr_setmem(p_garr, NULL, 0);
  }

} /* garr_empty */


/* Free an allocated growable array.
 */
void garr_free(
  GROWABLE_ARRAY* p_garr)
{
  HQASSERT((p_garr != NULL),
           "garr_free: NULL growable array pointer");

  VERIFY_OBJECT(p_garr, GROWABLE_ARRAY_NAME);

  garr_empty(p_garr);

  mm_free(p_garr->mm_pool, p_garr, sizeof(GROWABLE_ARRAY));

} /* garr_free */

/*
* Log stripped */
/* EOF */
