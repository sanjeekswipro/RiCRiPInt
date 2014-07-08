/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_store.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of DlStateStore object, and required store methods for each
 * state type (e.g. STATEOBJECT, CLIPOBJECT, LateColorAttrib, etc).
 */

#include "core.h"
#include "swerrors.h"
#include "dl_store.h"

#include "dl_color.h"
#include "hdlPrivate.h"
#include "groupPrivate.h"
#include "display.h"
#include "dl_bres.h"
#include "gs_tag.h"
#include "objnamer.h"
#include "graphics.h"
#include "rcbtrap.h"
#include "params.h"
#include "mps.h"
#include "cce.h"
#include "ndisplay.h"
#include "monitor.h"
#include "jobmetrics.h"
#include "pattern.h"
#include "often.h"
#include "vnobj.h"
#include "shadex.h"


/* --Private macros-- */

#define DLSTATESTORE_NAME "DL state store"


/* --Private datatypes--*/

/* Display List State Store */
struct DlStateStore {
  uint32 tableSize;
  DlSSEntry** hashTable;
  DlSSEntry* preserved;
  DlSSEntry* lastlookup;
  DlSSSet* set;

  DlSSEntryCopy* copyEntry;
  DlSSEntryDelete* deleteEntry;
  DlSSEntryHash* hashEntry;
  DlSSEntrySame* sameEntry;
  DlSSEntryPreserveChildren* preserveChildrenForEntry;

  mm_pool_t *pools;

#ifdef DEBUG_BUILD
  uint32 nentries ;
  uint32 npreserved ;
  uint32 nlookups ;
  uint32 nlasthits ;
  uint32 nchainmisses ;
  uint32 maxchain ;
#endif

  OBJECT_NAME_MEMBER
};

#ifdef DEBUG_BUILD
int32 debug_dlstore = 0 ;
#endif

/* --DlStateStore methods-- */

/* Constructor.
*/
DlStateStore* dlSSNew(mm_pool_t *pools,
                      uint32 size,
                      DlSSSet* set,
                      DlSSEntryCopy* copier,
                      DlSSEntryDelete* destructor,
                      DlSSEntryHash* hash,
                      DlSSEntrySame* same,
                      DlSSEntryPreserveChildren* preserve)
{
  uint32 i;
  DlSSEntry** table;
  DlStateStore* self;

  HQASSERT((destructor != NULL) && (hash != NULL) && (same != NULL),
           "Required methods cannot be NULL");

  self = dl_alloc(pools, sizeof(DlStateStore), MM_ALLOC_CLASS_DLSTATESTORE);
  table = dl_alloc(pools, sizeof(DlSSEntry*) * size,
                   MM_ALLOC_CLASS_DLSTATESTORE);

  /* Check for allocation failure. */
  if ((self == NULL) || (table == NULL)) {
    /* This error causes the DL pools to be cleared, freeing self and table. */
    (void)error_handler(VMERROR);
    return NULL;
  }

  self->pools = pools;
  NAME_OBJECT(self, DLSTATESTORE_NAME);

  self->tableSize = size;
  self->hashTable = table;
  self->set = set;
  /* Initialise the hash table and preserved list to be empty. */
  for (i = 0; i < size; i ++) {
    table[i] = NULL;
  }
  self->preserved = NULL;
  self->lastlookup = NULL; /* Fast lookup */

  /* Assign the methods. */
  self->copyEntry = copier;
  self->deleteEntry = destructor;
  self->hashEntry = hash;
  self->sameEntry = same;
  self->preserveChildrenForEntry = preserve;

#ifdef DEBUG_BUILD
  self->nentries = 0 ;
  self->npreserved = 0 ;
  self->nlookups = 0 ;
  self->nlasthits = 0 ;
  self->nchainmisses = 0 ;
  self->maxchain = 0 ;
#endif

  return self;
}

/* Destructor.
In general this is never called, since the stores are allocated from a
display pool, which is cleared regularly.
*/
void dlSSDelete(DlStateStore* self)
{
  if (self == NULL) {
    return;
  }

  VERIFY_OBJECT(self, DLSTATESTORE_NAME);

  dl_free(self->pools, self->hashTable, sizeof(DlSSEntry*) * self->tableSize,
          MM_ALLOC_CLASS_DLSTATESTORE);
  dl_free(self->pools, self, sizeof(DlStateStore), MM_ALLOC_CLASS_DLSTATESTORE);
}

/* Insert the passed entry into the store, returning a pointer to the
entry in the store.  If the entry is already present, a pointer to the
previously stored entry will be returned.

If insertCopy is TRUE, and the entry is not already in the store, a copy
will be inserted into it.  Note that there must be a valid copier method
in this case.

On error NULL is returned.  */
DlSSEntry* dlSSInsert(DlStateStore* self, DlSSEntry* entry, Bool insertCopy)
{
  uintptr_t hash;
  DlSSEntry* stored;
#ifdef DEBUG_BUILD
  uint32 chainlength = 0 ;
#endif

  VERIFY_OBJECT(self, DLSTATESTORE_NAME);
  HQASSERT(entry != NULL, "dlSSInsert - 'entry' cannot be NULL");

  hash = self->hashEntry(entry) % self->tableSize;

#ifdef DEBUG_BUILD
  self->nlookups++ ;
#endif

  /* Test fast lookup first */
  if ( (stored = self->lastlookup) == NULL || !self->sameEntry(entry, stored) ) {
    /* No fast lookup match, so scan the hash bin for a matching entry. */
    stored = self->hashTable[hash];
    while ( stored != NULL && !self->sameEntry(entry, stored) ) {
      stored = stored->next;
#ifdef DEBUG_BUILD
      chainlength++ ;
#endif
    }
#ifdef DEBUG_BUILD
    self->nchainmisses += chainlength ;
#endif
  }
#ifdef DEBUG_BUILD
  else {
    self->nlasthits++ ;
  }
#endif

  /* If the entry was not matched in the list, add a new entry. */
  if (stored == NULL) {
    /* If we should insert a copy, perform the copy, otherwise we'll insert the
    parameter entry directly. */
    if (insertCopy) {
      HQASSERT(self->copyEntry != NULL,
               "dlSSInsert - copyEntry method cannot be NULL");
      stored = self->copyEntry(entry, self->pools);
    }
    else {
      stored = entry;
    }

    /* The copy may have failed. */
    if (stored == NULL) {
      (void)error_handler(VMERROR);
    }
    else {
      uintptr_t hash = self->hashEntry(entry) % self->tableSize;

      /* Insert into the hash bin list. */
      stored->next = self->hashTable[hash];
      self->lastlookup = self->hashTable[hash] = stored;
#ifdef DEBUG_BUILD
      self->nentries++ ;
      if ( chainlength >= self->maxchain )
        self->maxchain = chainlength + 1 ;
#endif
    }
  } else /* Found entry, so set last lookup to match this entry */
    self->lastlookup = stored ;

  return stored;
}

/* Remove the specified entry from the store.  If the entry was present,
TRUE is returned, otherwise FALSE is returned.  */
Bool dlSSRemove(DlStateStore* self, DlSSEntry* entry)
{
  DlSSEntry* lookup;
  DlSSEntry** previous;

  VERIFY_OBJECT(self, DLSTATESTORE_NAME);
  HQASSERT(entry != NULL, "dlSSRemove - 'entry' cannot be NULL");

  self->lastlookup = NULL; /* Clear the fast lookup entry */

  previous = &self->hashTable[self->hashEntry(entry) % self->tableSize];
  while ((lookup = *previous) != NULL) {
    if (entry == lookup) {
      /* We've found the entry - unlink it and return. */
      *previous = lookup->next;
      lookup->next = NULL;
#ifdef DEBUG_BUILD
      self->nentries-- ;
#endif
      return TRUE;
    }
    previous = &lookup->next;
  }

  return FALSE;
}

/* Preserve the passed object. This works by removing the object from
the store, and then adding it to a list of preserved objects, which will
be reinstated in dlSSFree().  */
void dlSSPreserve(DlStateStore* self, DlSSEntry* entry, Bool preserveChildren)
{
  VERIFY_OBJECT(self, DLSTATESTORE_NAME);
  HQASSERT(entry != NULL, "dlSSPreserve - 'object' cannot be NULL");

  /* Try to remove the object from the store.  If the object was in the
  store, then add it to the preservation list. If it was not in the
  store, then we can assume that this object has already been removed,
  and is already on the preservation list. */
  if (dlSSRemove(self, entry)) {
    entry->next = self->preserved;
    self->preserved = entry;

    if ( preserveChildren && self->preserveChildrenForEntry != NULL )
      self->preserveChildrenForEntry(entry, self->set);

#ifdef DEBUG_BUILD
    self->npreserved++ ;
#endif
  }
}

/* Free (i.e., call the destructor for) all entries in the store's hash
table.  Preserved entries will be re-inserted into it after it has been
cleared.  The preserve lists are then cleared too.  */
void dlSSFree(DlStateStore* self)
{
  uint32 i;

  VERIFY_OBJECT(self, DLSTATESTORE_NAME);

  /* Clear the fast lookup pointer */
  self->lastlookup = NULL ;

  /* Delete all entries in the table. */
  for (i = 0; i < self->tableSize; i ++) {
    DlSSEntry *entry, **hashbin = &self->hashTable[i];

    /* Destroy all entries in this hash bin. */
    while ( (entry = *hashbin) != NULL ) {
      /* Unlink entry from chain before calling delete function; we don't
         hold onto *any* entry pointers during this process in case
         recursive calls are made on dlSSRemove() or dlSSPreserve(). */
      *hashbin = entry->next ;

      self->deleteEntry(entry, self->pools);
    }

    HQASSERT(self->hashTable[i] == NULL, "DL store entry list not freed") ;

#ifdef DEBUG_BUILD
    self->nentries = 0 ;
    self->maxchain = 0 ;
#endif
  }

  /* Insert all preserved entries. */
  while (self->preserved != NULL) {
    DlSSEntry* current = self->preserved;

    /* Move to the next entry before we insert the current one (as the
    insertion will overwrite the 'next' pointer). */
    self->preserved = self->preserved->next;
    (void)dlSSInsert(self, current, FALSE);

#ifdef DEBUG_BUILD
    self->npreserved-- ;
#endif
  }
  /* Note that the preserved list is now clear (NULL). */
}


Bool dlSSForall(DlStateStore *self,
                Bool (*callback)(void *entry, void *data),
                void *data)
{
  uint32 i;

  VERIFY_OBJECT(self, DLSTATESTORE_NAME);

  /* Iterate over all entries in the table. */
  for (i = 0; i < self->tableSize; i ++) {
    DlSSEntry* entry = self->hashTable[i];

    while (entry != NULL) {
      DlSSEntry *next = entry->next ; /* Save next in case remove done */

      if ( !(*callback)(entry, data) )
        return FALSE ;

      entry = next;
    }
  }

  return TRUE ;
}

#ifdef DEBUG_BUILD
void dlSSMetrics(DlStateStore *self, char *storename, uint32 reportmin)
{
  if ( self->nentries >= reportmin ) {
    monitorf((uint8 *)"DL store %s:\n", storename) ;
    monitorf((uint8 *)"  entries=%d\n", self->nentries) ;
    monitorf((uint8 *)"  hash table loading=%f%%\n",
             (double)self->nentries * 100.0 / (double)self->tableSize) ;
    monitorf((uint8 *)"  preserved=%d\n", self->npreserved) ;
    monitorf((uint8 *)"  inserts=%d\n", self->nlookups) ;
    monitorf((uint8 *)"  fast hits=%d\n", self->nlasthits) ;
    monitorf((uint8 *)"  chain misses=%d\n", self->nchainmisses) ;
    monitorf((uint8 *)"  max chain length=%d\n", self->maxchain) ;
  }
}
#endif

/* --NFILLCACHE store methods-- */

/* Copier.
*/
DlSSEntry* nfillCacheCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  NFILLCACHE* self = (NFILLCACHE*)entry;
  NFILLCACHE* copy;

#ifdef METRICS_BUILD
  dl_metrics()->store.nfillCount++;
#endif

  HQASSERT(self != NULL, "nfillCacheCopy - 'self' cannot be NULL");

  copy = (NFILLCACHE*)dl_alloc(pools, sizeof(NFILLCACHE),
                               MM_ALLOC_CLASS_NFILL_CACHE);
  if (copy == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  *copy = *self;
  return &copy->storeEntry;
}

/* Destructor.
*/
void nfillCacheDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  NFILLCACHE* self = (NFILLCACHE*)entry;

  HQASSERT(self != NULL, "nfillCacheDelete - 'self' cannot be NULL");

  /* We are responsible for deallocating the fill (even though we didn't
  allocate it). */
  free_nfill(self->nfill, pools);
  dl_free(pools, self, sizeof(NFILLCACHE), MM_ALLOC_CLASS_NFILL_CACHE);
}

/* Hash function.
*/
uintptr_t nfillCacheHash(DlSSEntry* entry)
{
  int32 i;
  int32 limit ;
  int32 hash;
  NFILLOBJECT* nfill;
  NFILLCACHE* self = (NFILLCACHE*)entry;

  HQASSERT(self != NULL, "nfillCacheHash - 'self' cannot be NULL");

  nfill = self->nfill;
  HQASSERT(nfill != NULL, "nfillCacheHash - 'nfill' cannot be NULL");

  limit = nfill->nthreads ;

  /* Compute hash function on NFILL. There isn't much information at the
     top level of the NFILL structure, so we combine the thread start and
     ends as well. */
  hash = limit + nfill->y1clip ;

  for ( i = 0 ; i < limit ; i++ ) {
    NBRESS *nbress= nfill->thread[i] ;

    hash ^= nbress->nx1 + nbress->nx2 ;
    hash ^= nbress->ny1 + nbress->ny2 ;
  }

  return hash;
}

/* Are the passed NFILLCACHE objects identical?
*/
Bool nfillCacheSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  NFILLOBJECT *a;
  NFILLOBJECT *b;

  HQASSERT((entryA != NULL) && (entryB != NULL),
           "nfillSame - parameters cannot be NULL");

  a = ((NFILLCACHE*)entryA)->nfill;
  b = ((NFILLCACHE*)entryB)->nfill;

  HQASSERT((a != NULL) && (b != NULL), "nfillSame - fills cannot be NULL");

  /* same_nfill_objects() is a well-used API and could not be changed, thus
  it's called from here rather than inlined. */
  return same_nfill_objects(a, b);
}


/* --STATEOBJECT store methods-- */

/* Copier.
*/
DlSSEntry* stateObjectCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  STATEOBJECT* self = (STATEOBJECT*)entry;
  STATEOBJECT* copy;

#ifdef METRICS_BUILD
  dl_metrics()->store.stateCount ++;
#endif

  HQASSERT(self != NULL, "stateObjectCopy - 'self' cannot be NULL");

  copy = (STATEOBJECT*)dl_alloc(pools, sizeof(STATEOBJECT),
                                MM_ALLOC_CLASS_STATE_OBJECT) ;
  if (copy == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  *copy = *self;
  return &copy->storeEntry;
}

/* Destructor.
*/
void stateObjectDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  STATEOBJECT* self = (STATEOBJECT*)entry;

  HQASSERT(self != NULL, "stateObjectDelete - 'self' cannot be NULL");

  dl_free(pools, self, sizeof(STATEOBJECT), MM_ALLOC_CLASS_STATE_OBJECT);
}

/* Compute hash function on state object. Pointer comparison is OK, because
clips and patterns will have been stored by this time, and so must be the same
for the same clips/patterns, on the same page.  A multiplication-based mixer
ensures that the value returned depends on all the bits of the STATEOBJECT.
*/
uintptr_t stateObjectHash(DlSSEntry* entry)
{
  uintptr_t hash;
  STATEOBJECT* self = (STATEOBJECT*)entry;

  HQASSERT(self != NULL, "stateObjectHash - 'self' cannot be NULL");

  hash = (uintptr_t)self->clipstate ^ (uintptr_t)self->patternstate;
  hash += self->spotno;
  hash += (uintptr_t)self->gstagstructure;
  hash += (uintptr_t)self->tranAttrib;
  hash += (uintptr_t)self->lateColorAttrib;
  hash += (uintptr_t)self->pclAttrib;

  /* Make high bits depend on all bits */
  hash *= hash * 2 + 1;

  return hash;
}

/* Are the passed STATEOBJECTs identical?
*/
Bool stateObjectSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  STATEOBJECT* a = (STATEOBJECT*)entryA;
  STATEOBJECT* b = (STATEOBJECT*)entryB;

  HQASSERT((a != NULL) && (b != NULL),
           "stateObjectSame - parameters cannot be NULL");

  if (a->clipstate == b->clipstate &&
      a->spotno == b->spotno &&
      a->patternstate == b->patternstate &&
      a->patternshape == b->patternshape &&
      a->gstagstructure == b->gstagstructure &&
      a->tranAttrib == b->tranAttrib &&
      a->lateColorAttrib == b->lateColorAttrib &&
      a->pclAttrib == b->pclAttrib) {
    return TRUE;
  }

  return FALSE;
}

/* Preserve any stored children.
*/
void stateObjectPreserveChildren(DlSSEntry* entry, DlSSSet* set)
{
  STATEOBJECT* self = (STATEOBJECT*)entry;
  CLIPOBJECT* clip;

  HQASSERT(self != NULL,
           "stateObjectPreserveChildren - 'self' cannot be NULL");

  /* Preserve clips. */
  clip = self->clipstate;
  while (clip != NULL) {
    dlSSPreserve(set->clip, &clip->storeEntry, TRUE);
    clip = clip->context;
  }

  if (self->patternstate != NULL)
    dlSSPreserve(set->pattern, &self->patternstate->storeEntry, TRUE);

  if (self->gstagstructure != NULL)
    dlSSPreserve(set->gstag, &self->gstagstructure->storeEntry, TRUE);

  if (self->tranAttrib != NULL)
    dlSSPreserve(set->transparency, &self->tranAttrib->storeEntry, TRUE);

  if (self->lateColorAttrib != NULL)
    dlSSPreserve(set->latecolor, &self->lateColorAttrib->storeEntry, TRUE);

  if (self->pclAttrib != NULL)
    dlSSPreserve(set->pcl, &self->pclAttrib->storeEntry, TRUE);
}


/* --CLIPOBJECT store methods-- */

/* Copier.
*/
DlSSEntry* clipObjectCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  CLIPOBJECT* self = (CLIPOBJECT*)entry;
  CLIPOBJECT* copy;

#ifdef METRICS_BUILD
  dl_metrics()->store.clipCount++;
#endif

  HQASSERT(self != NULL, "clipObjectCopy - 'self' cannot be NULL");

  copy = (CLIPOBJECT*)dl_alloc(pools, sizeof(CLIPOBJECT),
                               MM_ALLOC_CLASS_CLIP_OBJECT) ;
  if (copy == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  *copy = *self;
  return &copy->storeEntry;
}

/* Destructor.
*/
void clipObjectDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  CLIPOBJECT* self = (CLIPOBJECT*)entry;

  HQASSERT(self != NULL, "clipObjectDelete - 'self' cannot be NULL");

  dl_free(pools, self, sizeof(CLIPOBJECT), MM_ALLOC_CLASS_CLIP_OBJECT) ;
}

/* Hash function.
*/
uintptr_t clipObjectHash(DlSSEntry* entry)
{
  CLIPOBJECT* self = (CLIPOBJECT*)entry;

  HQASSERT(self != NULL, "clipObjectHash - 'self' cannot be NULL");

  return self->clipno ;
}

/* Are the passed CLIPOBJECTs identical?
*/
Bool clipObjectSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  CLIPOBJECT* a = (CLIPOBJECT*)entryA;
  CLIPOBJECT* b = (CLIPOBJECT*)entryB;

  HQASSERT((a != NULL) && (b != NULL),
           "clipObjectSame - parameters cannot be NULL");

  if ( a->clipno == b->clipno && a->pagebasematrixid == b->pagebasematrixid ) {
    return TRUE;
  }

  return FALSE;
}

/* Preserve any stored children.
*/

/* Clip objects are an irregular case; CLIPOBJECTS reference NFILL objects,
 * but DL store holds NFILLCACHE objects that are indirections to the NFILLs.
 * To preserve DL objects properly, the NFILL objects in clips must be related
 *  to NFILLCACHE objects in the DL store. This is a clunky but inobstrusive
 * way of doing this; iterate over the appropriate DL store looking for
 * NFILLCACHE objects associated to a particular NFILL. Each such cache
 * object must be preserved, to preserve the nfill.
 */

/* this type is only need for the function that preserves the nfill objects.
 * A convience to pass enough data to match NFILL and NFILLCACHE objects.
 */
typedef struct NFillPreserveInfo {
  DlStateStore* store ;
  NFILLOBJECT* nfill ;
} NFillPreserveInfo ;

static Bool PreserveNFillCacheForClip(void * a, void* b )
{
  NFILLCACHE* entry = (NFILLCACHE*)a ;
  NFillPreserveInfo * info = (NFillPreserveInfo*)b ;

  if ( entry->nfill == info->nfill )
    dlSSPreserve(info->store, &entry->storeEntry, FALSE) ;

  return TRUE;
}

void clipObjectPreserveChildren(DlSSEntry* entry, DlSSSet* set)
{
  CLIPOBJECT* self = (CLIPOBJECT*)entry;

  HQASSERT(self != NULL, "clipObjectPreserveChildren - 'self' cannot be NULL");

  if (self->fill != NULL) {
    /* Seek out all and preserve all NFILLCACHE objects associated to
     * this nfill.
     */
    NFillPreserveInfo info;

    info.nfill = self->fill;
    info.store = set->nfill;
    (void) dlSSForall(set->nfill, PreserveNFillCacheForClip, &info ) ;
  }
}

/* The clip object lookup must be fast, since it is called a lot. It uses the
   same tests as clipObjectSame() and clipObjectHash(), but does not call
   those functions. If you change those functions, you need to change this
   function. */
CLIPOBJECT *clipObjectLookup(DlStateStore *store, int32 clipno, int32 matrixid)
{
  CLIPOBJECT *clipobj ;

  HQASSERT(store != NULL, "store should not be NULL");

  VERIFY_OBJECT(store, DLSTATESTORE_NAME);

  /* Check fast lookup first */
  if ( (clipobj = (CLIPOBJECT *)store->lastlookup) != NULL &&
       clipobj->clipno == clipno &&
       clipobj->pagebasematrixid == matrixid )
    return clipobj ;

  /* Check all entries in table */
  clipobj = (CLIPOBJECT *)store->hashTable[clipno % store->tableSize] ;

  while ( clipobj ) {
    if ( clipobj->clipno == clipno && clipobj->pagebasematrixid == matrixid ) {
      store->lastlookup = &clipobj->storeEntry ;
      return clipobj ;
    }

    clipobj = (CLIPOBJECT *)clipobj->storeEntry.next ;
  }

  return NULL ;
}


/* --SoftMaskAttrib store methods-- */

/* Copy method.
*/
DlSSEntry* smAttribCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  SoftMaskAttrib* self = (SoftMaskAttrib*)entry;
  SoftMaskAttrib* copy;

#ifdef METRICS_BUILD
  dl_metrics()->store.softMaskCount++;
#endif

  HQASSERT(self != NULL, "smAttribCopy - 'self' cannot be NULL");

  copy = dl_alloc(pools, sizeof(SoftMaskAttrib), MM_ALLOC_CLASS_SOFTMASKATTRIB);
  if (copy == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  *copy = *self;
  return &copy->storeEntry;
}

/* Destructor.
*/
void smAttribDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  if (entry != NULL) /* Free self. */
    dl_free(pools, entry, sizeof(SoftMaskAttrib),
            MM_ALLOC_CLASS_SOFTMASKATTRIB);
}

/* Hash function.
*/
uintptr_t smAttribHash(DlSSEntry* entry)
{
  SoftMaskAttrib* self = (SoftMaskAttrib*)entry;

  HQASSERT(self != NULL, "smAttribHash - 'self' cannot be NULL");

  return (uintptr_t)self->type ^ (uintptr_t)self->group;
}

/* Are the passed SoftMaskAttribs identical?
*/
Bool smAttribSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  SoftMaskAttrib* a = (SoftMaskAttrib*)entryA;
  SoftMaskAttrib* b = (SoftMaskAttrib*)entryB;

  HQASSERT((a != NULL) && (b != NULL),
           "smAttribSame - parameters cannot be NULL");

  return a->type == b->type && a->group == b->group ;
}

/**
 * Recursively preserve any children.
 */
void smAttribPreserveChildren(DlSSEntry *entry, DlSSSet *set)
{
  SoftMaskAttrib *self = (SoftMaskAttrib *)entry;

  HQASSERT(self != NULL, "smAttribPreserveChildren - 'self' cannot be NULL");

  if (self->group != NULL) {
    HDL *hdl = groupHdl(self->group);

    dlSSPreserve(set->hdl, hdlStoreEntry(hdl), TRUE);
  }
}

/* --TranAttrib store methods-- */

/* Copy method.
*/
DlSSEntry* tranAttribCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  TranAttrib* self = (TranAttrib*)entry;
  TranAttrib* copy;

#ifdef METRICS_BUILD
  dl_metrics()->store.transparencyCount++;
#endif

  HQASSERT(self != NULL, "tranAttribCopy - 'self' cannot be NULL");

  copy = dl_alloc(pools, sizeof(TranAttrib), MM_ALLOC_CLASS_TRANATTRIB);
  if (copy == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  *copy = *self;
  return &copy->storeEntry;
}

/* Destructor.
*/
void tranAttribDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  if (entry != NULL)
    dl_free(pools, entry, sizeof(TranAttrib), MM_ALLOC_CLASS_TRANATTRIB);
}

/* Hash function.
*/
uintptr_t tranAttribHash(DlSSEntry* entry)
{
  TranAttrib* self = (TranAttrib*)entry;

  HQASSERT(self != NULL, "tranAttribHash - 'self' cannot be NULL");

  return self->blendMode + self->alpha + self->alphaIsShape +
         (uintptr_t)self->softMask;
}

/* Are the passed TranAttribs identical?
*/
Bool tranAttribSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  TranAttrib* a = (TranAttrib*)entryA;
  TranAttrib* b = (TranAttrib*)entryB;

  HQASSERT((a != NULL) && (b != NULL),
           "tranAttribSame - parameters cannot be NULL");

  /* Simple element-wise comparison. */
  if ((a->blendMode != b->blendMode) ||
      (a->softMask != b->softMask) ||
      (a->alpha != b->alpha) ||
      (a->alphaIsShape != b->alphaIsShape)) {
    return FALSE;
  }

  return TRUE;
}

/* Preserve any stored children.
*/
void tranAttribPreserveChildren(DlSSEntry* entry, DlSSSet* set)
{
  TranAttrib* self = (TranAttrib*)entry;

  HQASSERT(self != NULL, "tranAttribPreserveChildren - 'self' cannot be NULL");

  if (self->softMask != NULL)
    dlSSPreserve(set->softMask, &self->softMask->storeEntry, TRUE);
}


/* --LateColorAttrib store methods-- */

/* Copy method.
*/
DlSSEntry* lateColorAttribCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  LateColorAttrib* self = (LateColorAttrib*)entry;
  LateColorAttrib* copy;

#ifdef METRICS_BUILD
  dl_metrics()->store.latecolorCount++;
#endif

  copy = dl_alloc(pools, sizeof(LateColorAttrib), MM_ALLOC_CLASS_BDATTRIB);
  if (copy == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  *copy = *self;
  return &copy->storeEntry;
}

/* Destructor.
*/
void lateColorAttribDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  if (entry != NULL)
    dl_free(pools, entry, sizeof(LateColorAttrib), MM_ALLOC_CLASS_BDATTRIB);
}

/* Hash function.
*/
uintptr_t lateColorAttribHash(DlSSEntry* entry)
{
  LateColorAttrib* self = (LateColorAttrib*)entry;

  HQASSERT(self != NULL, "lateColorAttribHash - 'self' cannot be NULL");

  HQASSERT(self->origColorModel < (1 << 3), "Too many color models");
  HQASSERT(self->renderingIntent < (1 << 3), "Too many intents");
  HQASSERT(self->blackType < (1 << 3), "Too many black types");

  return (uintptr_t)(self->origColorModel |
                     (self->overprintMode << 4) |
                     (self->renderingIntent << 5) |
                     (self->blackType << 8) |
#ifdef METRICS_BUILD
                     (self->is_icc << 11) |
#endif
                     (self->independentChannels << 12));
}

/* Are the passed LateColorAttribs identical?
*/
Bool lateColorAttribSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  LateColorAttrib* a = (LateColorAttrib*)entryA;
  LateColorAttrib* b = (LateColorAttrib*)entryB;

  HQASSERT((a != NULL) && (b != NULL),
           "lateColorAttribSame - parameters cannot be NULL");

  return    a->origColorModel == b->origColorModel
         && a->overprintMode == b->overprintMode
         && a->renderingIntent == b->renderingIntent
         && a->blackType == b->blackType
         && a->independentChannels == b->independentChannels
#ifdef METRICS_BUILD
         && a->is_icc == b->is_icc
#endif
         ;
}

/* Recursively preserve any children.
*/
void lateColorAttribPreserveChildren(DlSSEntry* entry, DlSSSet* set)
{
  HQASSERT(entry != NULL, "lateColorAttribPreserveChildren - 'self' cannot be NULL");

  UNUSED_PARAM(DlSSEntry *, entry) ;
  UNUSED_PARAM(DlSSSet *, set) ;
}

/* Assign initial values, primarily for the erase color in a special case.
*/
LateColorAttrib lateColorAttribNew(void)
{
  LateColorAttrib a;

  a.storeEntry.next = NULL;
  a.origColorModel = REPRO_COLOR_MODEL_GRAY;
  a.overprintMode = INITIAL_OVERPRINT_MODE;
  a.renderingIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
  a.blackType = BLACK_TYPE_NONE;
  a.independentChannels = TRUE;
#ifdef METRICS_BUILD
  a.is_icc = FALSE;
#endif

 return a;
}


/* --PATTERNOBJECT store methods-- */


/* Copier.
*/
DlSSEntry* patternObjectCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  PATTERNOBJECT* self = (PATTERNOBJECT*)entry;
  PATTERNOBJECT* copy;

#ifdef METRICS_BUILD
  dl_metrics()->store.patternCount++;
#endif

  HQASSERT(self != NULL, "patternObjectCopy - 'self' cannot be NULL");

  copy = (PATTERNOBJECT*)dl_alloc(pools, sizeof(PATTERNOBJECT),
                                  MM_ALLOC_CLASS_PATTERN_OBJECT);
  if (copy == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  *copy = *self;
  return &copy->storeEntry;
}

/* Destructor.
*/
void patternObjectDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  PATTERNOBJECT *patobj = (PATTERNOBJECT*)entry;

  HQASSERT(patobj != NULL, "patternObjectDelete - 'patobj' cannot be NULL");

  if ( patobj->opcode == RENDER_hdl )
    hdlDestroy(&patobj->dldata.hdl);
  else if ( patobj->opcode == RENDER_group )
    groupDestroy(&patobj->dldata.group);
  patobj->opcode = RENDER_void;

  dl_free(pools, patobj, sizeof(PATTERNOBJECT), MM_ALLOC_CLASS_PATTERN_OBJECT);
}

/* Hash function.
*/
uintptr_t patternObjectHash(DlSSEntry* entry)
{
  PATTERNOBJECT *patobj = (PATTERNOBJECT*)entry;

  HQASSERT(patobj != NULL, "patternObjectHash - 'patobj' cannot be NULL");

  return (uint32)patobj->patternid;
}


/** Are the passed PATTERNOBJECTs identical? */
Bool patternObjectSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  PATTERNOBJECT* a = (PATTERNOBJECT*)entryA;
  PATTERNOBJECT* b = (PATTERNOBJECT*)entryB;

  HQASSERT((a != NULL) && (b != NULL),
           "patternObjectSame - parameters cannot be NULL");

  /* These tests must be the same as patternObjectLookup. */
  if ( a->patternid == b->patternid &&
       a->parent_patternid == b->parent_patternid &&
       a->pageBaseMatrixId == b->pageBaseMatrixId &&
       ((a->opcode == RENDER_group && b->opcode == RENDER_group)
        /* if there's a group, any transparency will work */
        || tranAttribEqual(a->ta, b->ta)) &&
       a->groupid == b->groupid &&
       dl_equal(a->ncolor, b->ncolor) &&
       a->overprinting[GSC_FILL] == b->overprinting[GSC_FILL] &&
       a->overprinting[GSC_STROKE] == b->overprinting[GSC_STROKE] ) {
    HQASSERT(a->painttype == b->painttype &&
             a->tilingtype == b->tilingtype &&
             a->xx == b->xx && a->xy == b->xy &&
             a->yx == b->yx && a->yy == b->yy &&
             a->bbx == b->bbx && a->bby == b->bby &&
             a->bsizex == b->bsizex && a->bsizey == b->bsizey &&
             patternHdl(a) == patternHdl(b),
             "Pattern IDs match, but other details do not") ;
    return TRUE;
  }
  return FALSE;
}


/** Preserve any stored children. */
void patternObjectPreserveChildren(DlSSEntry* entry, DlSSSet* set)
{
  PATTERNOBJECT *patobj = (PATTERNOBJECT*)entry;
  HDL *hdl = NULL;

  HQASSERT(patobj != NULL, "patternObjectPreserveChildren - 'patobj' cannot be NULL");

  hdl = patternHdl(patobj);

  if ( hdl != NULL )
    dlSSPreserve(set->hdl, hdlStoreEntry(hdl), TRUE);
}


/* Quick check if a group will certainly not be needed.
   groupCertainlyNotNeeded must imply groupNotNeededForTransparency. */
static Bool groupCertainlyNotNeeded(TranAttrib *tranattrib, Bool opaqueHdl)
{
  /* Without duplicating all the logic, we can determine if this is the
     non-transparent case, which is the one important for efficiency. */
  return
#ifdef DEBUG_BUILD
    (backdrop_render_debug & BR_DISABLE_GROUP_ELIMINATION) == 0 &&
#endif
    tranAttribIsOpaque(tranattrib) && opaqueHdl;
}


/* The pattern object lookup must be fast, since it is called a lot.  It
   uses the same tests as patternObjectSame(), but does not call it.  If
   you change one, you have to change the other. */
PATTERNOBJECT *patternObjectLookup(DlStateStore *store,
                                   int32 patternid, int32 parent_patternid,
                                   int32 matrixid, Group *context,
                                   dl_color_t *dlcolor, Bool overprinting[2],
                                   TranAttrib *tranattrib )
{
  PATTERNOBJECT *pattern ;
  p_ncolor_t ncolor ;

  HQASSERT(store != NULL, "store should not be NULL");
  HQASSERT(dlcolor != NULL, "No DL colour");

  VERIFY_OBJECT(store, DLSTATESTORE_NAME);

  /* Get an ncolor to compare with the pattern's one */
  dlc_to_dl_weak(&ncolor, dlcolor) ;

  /* Check fast lookup first */
  if ( (pattern = (PATTERNOBJECT *)store->lastlookup) != NULL &&
       pattern->patternid == patternid &&
       pattern->parent_patternid == parent_patternid &&
       pattern->pageBaseMatrixId == matrixid &&
       (pattern->opcode == RENDER_group
        /* if there's a group, don't match the non-transparent case */
        ? !groupCertainlyNotNeeded(tranattrib, !pattern->backdrop)
        /* if there's no group, transparency must match */
        : tranAttribEqual(pattern->ta, tranattrib)) &&
       (context == NULL || pattern->groupid == groupId(context)) &&
       dl_equal(pattern->ncolor, ncolor) &&
       pattern->overprinting[GSC_FILL] == overprinting[GSC_FILL] &&
       pattern->overprinting[GSC_STROKE] == overprinting[GSC_STROKE] )
    return pattern ;

  /* Check all entries in table */
  pattern = (PATTERNOBJECT *)store->hashTable[(uint32)patternid % store->tableSize] ;

  while ( pattern ) {
    if ( pattern->patternid == patternid &&
         pattern->parent_patternid == parent_patternid &&
         pattern->pageBaseMatrixId == matrixid &&
         (pattern->opcode == RENDER_group
          ? !groupCertainlyNotNeeded(tranattrib, !pattern->backdrop)
          : tranAttribEqual(pattern->ta, tranattrib)) &&
         (context == NULL || pattern->groupid == groupId(context)) &&
         dl_equal(pattern->ncolor, ncolor) &&
         pattern->overprinting[GSC_FILL] == overprinting[GSC_FILL] &&
         pattern->overprinting[GSC_STROKE] == overprinting[GSC_STROKE] ) {
      store->lastlookup = &pattern->storeEntry ;
      return pattern ;
    }

    pattern = (PATTERNOBJECT *)pattern->storeEntry.next ;
  }

  return NULL ;
}


/* --HDL store methods-- */

/* Copier.
*/
DlSSEntry* hdlStoreCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  UNUSED_PARAM(DlSSEntry *, entry) ;
  UNUSED_PARAM(mm_pool_t *, pools) ;

  HQFAIL("hdlStoreCopy should never be called; insert directly") ;

  (void)error_handler(UNREGISTERED);
  return NULL;
}

/* Destructor.
*/
void hdlStoreDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  HDL *hdl = (HDL*)entry;
  Group *group;

  UNUSED_PARAM(mm_pool_t *, pools);

  HQASSERT(hdl != NULL, "hdlStoreDelete - 'hdl' cannot be NULL");
  group = hdlGroup(hdl);

  if ( group != NULL )
    groupDestroy(&group);
  else
    hdlDestroy(&hdl);
}

/* Hash function.
*/
uintptr_t hdlStoreHash(DlSSEntry* entry)
{
  HDL *hdl = (HDL*)entry;

  HQASSERT(hdl != NULL, "hdl cannot be NULL");
  return hdlId(hdl);
}

/* Are the passed HDLs identical?
*/
Bool hdlStoreSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  HDL* a = (HDL*)entryA;
  HDL* b = (HDL*)entryB;

  HQASSERT((a != NULL) && (b != NULL),
           "hdlStoreSame - parameters cannot be NULL");

  /* Referential identity is used for HDLs. */
  return (a == b) ;
}

/**
 * Traverse a sub-display list linked list, preserving all stateobjects and
 * their children.
 */
void hdlStorePreserveChildren(DlSSEntry *entry, DlSSSet *stores)
{
  DLREF *dl;

  HQASSERT(entry != NULL, "Missing entry");
  HQASSERT(stores != NULL, "No DL stores");

  for ( dl = hdlOrderList((HDL*)entry); dl != NULL; dl = dlref_next(dl) ) {
    LISTOBJECT *lobj = dlref_lobj(dl);

    /* Preserving a large vignette could take a while. */
    SwOftenUnsafe();

    HQASSERT(lobj, "No listobject on display list");
    HQASSERT(lobj->objectstate, "No listobject on display list");

    /* Always preserve the state of the sub-object */
    dlSSPreserve(stores->state, &lobj->objectstate->storeEntry, TRUE);

    /* The object may have a sub-display list. Preserve its children. */
    switch ( lobj->opcode ) {
    case RENDER_shfill:
      dlSSPreserve(stores->hdl, hdlStoreEntry(lobj->dldata.shade->hdl), TRUE);
      break;
    case RENDER_hdl:
      dlSSPreserve(stores->hdl, hdlStoreEntry(lobj->dldata.hdl), TRUE);
      break;
    case RENDER_group:
      dlSSPreserve(stores->hdl, hdlStoreEntry(groupHdl(lobj->dldata.group)), TRUE);
      break;
    case RENDER_vignette:
      dlSSPreserve(stores->hdl, hdlStoreEntry(lobj->dldata.vignette->vhdl), TRUE);
      break;
    }
  }
}

/* Look up an HDL using its ID. This is used to associate the front and
   back-end representations of HDLs for groups, patterns, etc. */
HDL *hdlStoreLookup(DlStateStore *store, uint32 id)
{
  DlSSEntry *entry;
  HDL *hdl;

  VERIFY_OBJECT(store, DLSTATESTORE_NAME);

  entry = store->lastlookup;
  hdl = (HDL *)entry;

  /* Check fast lookup first */
  if ( hdl != NULL && hdlId(hdl) == id )
    return hdl;

  /* Check all entries in table */
  entry = store->hashTable[id % store->tableSize];

  while ( entry != NULL ) {
    hdl = (HDL *)entry;

    if ( hdlId(hdl) == id ) {
      store->lastlookup = entry;
      return hdl;
    }
    entry = entry->next;
  }
  return NULL;
}

/* --Utility methods-- */

/*
 * Get the intersection bounding box of a CLIPOBJECT, and determine
 * if it has an nfill object in it.
 */
void get_cliprect_intersectbbox(CLIPOBJECT *cl,
                                dbbox_t *intersect_bbox,
                                Bool *has_nfill)
{
  CLIPOBJECT *clip = cl ;
  dcoord intersect_X1 = MINDCOORD ;
  dcoord intersect_Y1 = MINDCOORD ;
  dcoord intersect_X2 = MAXDCOORD ;
  dcoord intersect_Y2 = MAXDCOORD ;
  Bool nfill_found = FALSE ;

  HQASSERT( clip, "NULL clipobject passed to get_cliprect_intersectbbox");
  HQASSERT( intersect_bbox,
            "NULL intersect_bbox passed to get_cliprect_intersectbbox");
  HQASSERT( has_nfill, "NULL has_nfill passed to get_cliprect_intersectbbox");


  while ( clip ) {
    if ( theX1Clip(*clip) > intersect_X1 )
      intersect_X1 = theX1Clip(*clip) ;

    if ( theY1Clip(*clip) > intersect_Y1 )
      intersect_Y1 = theY1Clip(*clip) ;

    if ( theX2Clip(*clip) < intersect_X2 )
      intersect_X2 = theX2Clip(*clip) ;

    if ( theY2Clip(*clip) < intersect_Y2 )
      intersect_Y2 = theY2Clip(*clip) ;

    if ( clip->fill && !nfill_found )
      nfill_found = TRUE ;

    clip = clip->context;
  }

  bbox_store(intersect_bbox,
             intersect_X1, intersect_Y1, intersect_X2, intersect_Y2) ;
  *has_nfill = nfill_found ;
}

/* Enumerate all the clips in the cache, calling the given function with each
 * one and its intersection bounding box and whether it has an nfill. Private
 * opaque args pointer for convenience.
 */

Bool clipcache_forall( DlStateStore* store,
                       Bool (*fn)(CLIPOBJECT *clip, dbbox_t *bbox,
                                  Bool has_nfill, void *args),
                       void *args )
{
  uint32 i ;
  CLIPOBJECT *clip ;

  HQASSERT( fn , "fn NULL in clipcache_forall" ) ;

  for ( i = 0 ; i < store->tableSize ; i++ ) {
    clip = (CLIPOBJECT*)store->hashTable[ i ] ;

    while ( clip ) {
      dbbox_t bbox ;
      int32 has_nfill ;

      get_cliprect_intersectbbox( clip , & bbox , & has_nfill ) ;

      if ( ! ( *fn )( clip , & bbox , has_nfill , args ))
        return FALSE ;

      clip = (CLIPOBJECT*)clip->storeEntry.next ;
    }
  }

  return TRUE ;
}

/*
 * Return TRUE if the two clipobjects are the same
 */

Bool same_clip_objects(CLIPOBJECT *cl1, CLIPOBJECT *cl2,
                       Bool fAllowFuzzy, int32 clipRectEpsilon)
{
  dbbox_t c1_bbox_intersect, c2_bbox_intersect ;
  Bool c1_has_nfill = FALSE ;
  Bool c2_has_nfill = FALSE ;

  /* If the pointers are the same, then the objects are. If either is NULL,
   * then the objects are not.
   */
  if ( cl1 == cl2 )
    return TRUE ;
  if ( cl1 == NULL || cl2 == NULL )
    return FALSE ;

  /*
   * Find the intersection rectangle for each clip, and find out if
   * it has an nfill or not.
   */
  get_cliprect_intersectbbox(cl1, &c1_bbox_intersect, &c1_has_nfill ) ;
  get_cliprect_intersectbbox(cl2, &c2_bbox_intersect, &c2_has_nfill ) ;

  /* Intersection tests */
  if ( !bbox_contains_epsilon(&c1_bbox_intersect, &c2_bbox_intersect,
                              clipRectEpsilon, clipRectEpsilon) ||
       !bbox_contains_epsilon(&c2_bbox_intersect, &c1_bbox_intersect,
                              clipRectEpsilon, clipRectEpsilon) ) {
    if ( !fAllowFuzzy )
      return FALSE ;

    /* If neither is wholly inside the other, they overlap */
    if ( !bbox_contains(&c1_bbox_intersect, &c2_bbox_intersect) &&
         !bbox_contains(&c2_bbox_intersect, &c1_bbox_intersect) )
      return FALSE ;
  }

  /* Passed the intersection tests and nfills present must be the same */
  if ( c1_has_nfill != c2_has_nfill )
    return FALSE ;

  /*
   * Loop over the contexts containing fills looking at the rule and the
   * fill contents to make sure they match.
   */

  while ( cl1 != NULL && cl2 != NULL ) {
    Bool fCompare = TRUE;

    /* Skip over clips without nfills, they have already been checked */
    if ( cl1->fill == NULL ) {
      cl1 = cl1->context ;
      fCompare = FALSE ;
    }
    if ( cl2->fill == NULL ) {
      cl2 = cl2->context ;
      fCompare = FALSE ;
    }

    if ( fCompare ) {
      NFILLOBJECT *nfill1 , *nfill2 ;

      if ( cl1->rule != cl2->rule )
        return FALSE ;

      /* Now comparing the fill objects in each context
       * to make sure that they have the same start points.
       */
      nfill1 = cl1->fill ;
      nfill2 = cl2->fill ;

      if ( ! same_nfill_objects( nfill1 , nfill2 )) {
        if ( ! fAllowFuzzy ||
             (( /* Normal match has failed, try to trap match
                   (if both have trap info) */
               nfill1->rcbtrap == NULL ||
               nfill2->rcbtrap == NULL ||
               ! rcbt_comparetrap( nfill1->rcbtrap , nfill2->rcbtrap ,
                                   FALSE /* disallow donuts */,
                                   TRUE /* do center check */ )) &&
              ( /* Ignore clip test completely if both are complex clips
                   (no trap info) and the user param is set */
               nfill1->rcbtrap != NULL ||
               nfill2->rcbtrap != NULL ||
               ! UserParams.RecombineWeakMergeTest )))
          return FALSE;
      }
      cl1 = cl1->context ;
      cl2 = cl2->context ;
    }
  }

  /* Must both end together or allow straggling cliprecs that contain
     only a bbox (since already checked bboxes are similar) */
  while ( cl1 != NULL ) {
    if ( cl1->fill != NULL )
      return FALSE ;
    cl1 = cl1->context ;
  }
  while ( cl2 != NULL ) {
    if ( cl2->fill != NULL )
      return FALSE ;
    cl2 = cl2->context ;
  }

  /* At this point through exhaustion they are the same, or a trap match. */
  return TRUE ;
}

/* Are the passed nfill objects the same?
*/
Bool same_nfill_objects(NFILLOBJECT *nfill1, NFILLOBJECT *nfill2)
{
  int32 i, limit;
  NBRESS *p1, *p2;

  /* if the fill pointers are the same then it has to be the same! */
  if (nfill1 == nfill2)
    return TRUE;

  /* if the number of threads are different, or the starting Y clip or scan
   * conversion rule are different for the two fill objects then they can not
   * be the same. The clippedout test is possibly a little harsh, we really
   * only care about the right hand side, but there is not much probability
   * of matching everything else and having the clipping discards differ. */
  if (!nfill1 || !nfill2 ||
      nfill1->type != nfill2->type ||
      nfill1->nthreads != nfill2->nthreads ||
      nfill1->y1clip != nfill2->y1clip ||
      nfill1->converter != nfill2->converter ||
      nfill1->clippedout != nfill2->clippedout )
    return FALSE;

  limit = nfill1->nthreads;
  /* loop through verifying that the starting points for each
   * thread are the same */
  for (i=0; i<limit; ++i)
  {
    p1 = nfill1->thread[i];
    p2 = nfill2->thread[i];
    if (p1->nx1 != p2->nx1 || p1->ny1 != p2->ny1 || p1->nx2 != p2->nx2
        || p1->ny2 != p2->ny2 || p1->norient != p2->norient)
      return FALSE;
  }

  /* if the dx/dy pairs are different then they must be different */
  for (i=0; i<limit;++i)
  {
    p1 = nfill1->thread[i];
    p2 = nfill2->thread[i];

    if ( !dxylist_equal(&(p1->dxy), &(p2->dxy)) )
      return FALSE;
  }
  /* at this point the two fills have to be the same since they mark
   * the same pixels */
  return TRUE;
}

/* Log stripped */
