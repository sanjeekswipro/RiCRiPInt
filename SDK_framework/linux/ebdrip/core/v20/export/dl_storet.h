/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_storet.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Public types for the DlStateStore object.
 */

#ifndef __dl_storet_H__
#define __dl_storet_H__

#include "mm.h"

/* --Datatypes-- */

/* Display List State Store */
typedef struct DlStateStore DlStateStore;

/* Display List State Store Set */
typedef struct DlSSSet {
  DlStateStore* state;
  DlStateStore* nfill;
  DlStateStore* clip;
  DlStateStore* gstag;
  DlStateStore* pattern;
  DlStateStore* patternshape;
  DlStateStore* softMask;
  DlStateStore* latecolor;
  DlStateStore* transparency;
  DlStateStore* hdl;
  DlStateStore* pcl;
} DlSSSet;

/* State Store entry base class - must be the first member of any subclass. */
typedef struct DlSSEntry {
  struct DlSSEntry* next;
} DlSSEntry;

/* Methods which must be implemented by a subclass. */
typedef void (DlSSEntryDelete)(DlSSEntry* entry, mm_pool_t *pools);
typedef uintptr_t (DlSSEntryHash)(DlSSEntry* entry);
typedef Bool (DlSSEntrySame)(DlSSEntry* a, DlSSEntry* b);

/* Methods which can optionally be implementated by a subclass. */
typedef DlSSEntry* (DlSSEntryCopy)(DlSSEntry* entry, mm_pool_t *pools);
typedef void (DlSSEntryPreserveChildren)(DlSSEntry* entry, DlSSSet* set);


/* --Description--

--DlStateStore--

Please refer to the description in dl_store.h.

--DlSSSet--

The DlSSSet structure is designed to allow a particular set of stores to
be grouped together. This is more than a aesthetic grouping; each DlSSEntry
object may contain references to other stored objects. These other objects
must be stored in the same set of DlStateStores in order for recursive
preservation to work (via the DlSSEntryPreserveChildren interface) - each
entry must have access to the stores which hold its members in order to
preserve them.

--DlSSEntry--

The DlSSEntry type and associated method interfaces define a base class
which allows any object to be managed by a DLStateStore object.

To subclass DlSSEntry, one must define a datatype where the DlSSEntry is
the first member, thus:

struct Subclass {
  DlSSEntry storeEntry;
  <other members>
}

Methods must then be defined to perform the support functions required by
the DlStateStore object. dl_store.c contains many implementations of
such methods.

If possible, the most efficient way to insert a new object into a store is
to setup a local variable instance of your object, and pass a pointer to it
into dlSSInsert(), with the insertCopy parameter set to TRUE. In order for
this to work, you must implement the DlSSEntryCopy() method.

If your subclass contains members that are also held by a DlStateStore, you
should implement the DlSSEntryPreserveChildren() method, in which you should
preserve each member (be sure to pass TRUE for the preserveChildren
parameter to ensure that any recursive preservation takes place).
*/

#endif

/* Log stripped */
