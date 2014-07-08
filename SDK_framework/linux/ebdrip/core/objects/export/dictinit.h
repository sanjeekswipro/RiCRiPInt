/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!export:dictinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Dictionary creation.
 */

#ifndef __DICTINIT_H__
#define __DICTINIT_H__ (1)

#include "objectt.h"

/* Dictionary allocator context */
typedef struct DICT_ALLOCATOR {
  void *(*alloc_mem)(size_t size, uintptr_t data);
  uintptr_t data;
} DICT_ALLOCATOR;

/** \brief Allocate and initialise a new dictionary.
 *
 * All created dictionaries have unlimited access. Note that the global memory
 * setting used for the dictionary contents is also applied to the dictionary
 * object.
 *
 * \param[out] dict The dictionary object.
 * \param[in] allocator The allocator context to use for the dictionary storage.
 * \param[in] length The initial number of entries in the dictionary.
 * \param[in] mark The object mark applied to the dictionary.
 *
 * \returns True if the dictionary was created, else false and raises VMERROR.
 */
Bool dict_create(
  OBJECT  *dict,
  DICT_ALLOCATOR *allocator,
  int32   length,
  uint8   mark);

#endif /* !__DICTINIT_H__ */

/* Log stripped */
