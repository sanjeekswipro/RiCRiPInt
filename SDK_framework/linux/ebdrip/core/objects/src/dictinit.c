/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:dictinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Dictionary creation.
 */

#include "core.h"
#include "objects.h"
#include "swerrors.h"
#include "dictinit.h"

/* Allocate and initialise a new dictionary */
Bool dict_create(
  OBJECT  *dict,
  DICT_ALLOCATOR *allocator,
  int32   length,
  uint8   mark)
{
  void *storage;

  HQASSERT((dict != NULL), "NULL PS dict pointer");
  HQASSERT((length >= 0), "Invalid dict length");

  /* For phase 1 dict API work we continue with the implementation of dicts as
   * being a hash array of OBJECTs
   */
  storage = allocator->alloc_mem(NDICTOBJECTS(length)*sizeof(OBJECT), allocator->data);
  if (storage == NULL) {
    return (error_handler(VMERROR));
  }
  init_dictionary(dict, length, UNLIMITED, storage, mark);

  return (TRUE);
}

/* Log stripped */
