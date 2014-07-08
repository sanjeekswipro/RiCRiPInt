/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_store.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interfaces for DlStateStore object, and for each state object
 * (e.g. STATEOBJECT, CLIPOBJECT, LateColorAttrib, etc).
 */

#ifndef __DL_STORE_H__
#define __DL_STORE_H__

#include "dl_storet.h"
#include "dl_color.h"
#include "displayt.h"
#include "ndisplay.h"

/* --Methods-- */

/** Create a new DL store. */
DlStateStore* dlSSNew(mm_pool_t *pools,
                      uint32 size,
                      DlSSSet* set,
                      DlSSEntryCopy* copier,
                      DlSSEntryDelete* destructor,
                      DlSSEntryHash* hash,
                      DlSSEntrySame* same,
                      DlSSEntryPreserveChildren* preserve);

/** Destroy a whole DL store. This is not usually called, since DL entries
    are allocated from the DL pool, which is destroyed en-masse. */
void dlSSDelete(DlStateStore* self);

/** Remove all of the regular entries from a DL store and free them. After
    freeing the regular entries, any preserved entries in the DL store are
    inserted into the store. */
void dlSSFree(DlStateStore* self);

/** Insert an entry into a DL store. If an existing entry matches, return
    that entry. If insertCopy is TRUE, then a copy of the entry is made and
    inserted, and the pointer to the new copy is returned. */
DlSSEntry* dlSSInsert(DlStateStore* self, DlSSEntry* entry, Bool insertCopy);

/** Look for a store entry which is the same as the given one, though
    not necessarily the same copy. */
DlSSEntry* dlSSLookup(DlStateStore* self, DlSSEntry* entry);

/** Remove an entry from a DL store. Returns TRUE if the entry was found in
    the DL store and removed. */
Bool dlSSRemove(DlStateStore* self, DlSSEntry* entry);

/** Preserve an entry in a DL store, so dlSSFree will not free it. */
void dlSSPreserve(DlStateStore* self, DlSSEntry* entry, Bool preserveChildren);

/** Iterate over all entries in a DL store. The entry is passed to a callback
    function as a void pointer, because it is most likely to be cast to
    another type anyway. Other DL store functions (remove, preserve, etc)
    should not be called on other DL store entries within the callback. */
Bool dlSSForall(DlStateStore *self,
                Bool (*callback)(void *entry, void *data),
                void *data) ;

#ifdef DEBUG_BUILD
enum {
  DEBUG_DLSTORE_METRICS = 1
} ;

extern int32 debug_dlstore ;

void dlSSMetrics(DlStateStore *self, char *storename, uint32 reportmin) ;
#endif

/* --Store methods for each object type-- */

/* NFILLCACHE */
DlSSEntry* nfillCacheCopy(DlSSEntry* entry, mm_pool_t *pools);
void nfillCacheDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t nfillCacheHash(DlSSEntry* entry);
Bool nfillCacheSame(DlSSEntry* entryA, DlSSEntry* entryB);

/* STATEOBJECT */
DlSSEntry* stateObjectCopy(DlSSEntry* entry, mm_pool_t *pools);
void stateObjectDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t stateObjectHash(DlSSEntry* entry);
Bool stateObjectSame(DlSSEntry* entryA, DlSSEntry* entryB);
void stateObjectPreserveChildren(DlSSEntry* entry, DlSSSet* set);

/* CLIPOBJECT */
DlSSEntry* clipObjectCopy(DlSSEntry* entry, mm_pool_t *pools);
void clipObjectDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t clipObjectHash(DlSSEntry* entry);
Bool clipObjectSame(DlSSEntry* entryA, DlSSEntry* entryB);
void clipObjectPreserveChildren(DlSSEntry* entry, DlSSSet* set);

/* Find a clip object from its ID. */
CLIPOBJECT *clipObjectLookup(DlStateStore *store, int32 clipno, int32 matrixid);

/* GSTAGSTRUCTUREOBJECT */
void gsTagDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t gsTagHash(DlSSEntry* entry);
Bool gsTagSame(DlSSEntry* entryA, DlSSEntry* entryB);

/* SoftMaskAttrib */
DlSSEntry* smAttribCopy(DlSSEntry* entry, mm_pool_t *pools);
void smAttribDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t smAttribHash(DlSSEntry* entry);
Bool smAttribSame(DlSSEntry* entryA, DlSSEntry* entryB);
void smAttribPreserveChildren(DlSSEntry* entry, DlSSSet* set);

/* TranAttrib */
DlSSEntry* tranAttribCopy(DlSSEntry* entry, mm_pool_t *pools);
void tranAttribDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t tranAttribHash(DlSSEntry* entry);
Bool tranAttribSame(DlSSEntry* entryA, DlSSEntry* entryB);
void tranAttribPreserveChildren(DlSSEntry* entry, DlSSSet* set);

/* LateColorAttrib */
DlSSEntry* lateColorAttribCopy(DlSSEntry* entry, mm_pool_t *pools);
void lateColorAttribDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t lateColorAttribHash(DlSSEntry* entry);
Bool lateColorAttribSame(DlSSEntry* entryA, DlSSEntry* entryB);
void lateColorAttribPreserveChildren(DlSSEntry* entry, DlSSSet* set);
LateColorAttrib lateColorAttribNew(void);

/* PATTERNOBJECT */
DlSSEntry* patternObjectCopy(DlSSEntry* entry, mm_pool_t *pools);
void patternObjectDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t patternObjectHash(DlSSEntry* entry);
Bool patternObjectSame(DlSSEntry* entryA, DlSSEntry* entryB);
void patternObjectPreserveChildren(DlSSEntry* entry, DlSSSet* set);

/* Find an existing pattern object from its ID (patterns are costly to
   regenerate, so lookup before trying to insert into cache). The matrix ID is
   used to determine when the page base matrix changes; pattern phase is
   relative to the matrix origin. The group object is the parent group into
   which the pattern is being composited. */
PATTERNOBJECT *patternObjectLookup(DlStateStore *store,
                                   int32 patternid, int32 parent_patternid,
                                   int32 matrixid, Group *context,
                                   dl_color_t *dlcolor,
                                   Bool overprinting[2], TranAttrib *ta);

/* HDL. HDLs are stored in the DL state store when opening/closing
   transparency groups. */
DlSSEntry* hdlStoreCopy(DlSSEntry* entry, mm_pool_t *pools);
void hdlStoreDelete(DlSSEntry* entry, mm_pool_t *pools);
uintptr_t hdlStoreHash(DlSSEntry* entry);
Bool hdlStoreSame(DlSSEntry* entryA, DlSSEntry* entryB);
void hdlStorePreserveChildren(DlSSEntry* entry, DlSSSet* set);

/* Find an HDL from its ID and opcode. Sub-classes of an HDL will share the
   same ID as the base HDL, but have different opcodes. HDLs are costly or
   impossible to regenerate, so lookup before trying to insert into cache. */
HDL *hdlStoreLookup(DlStateStore *store, uint32 hdlid) ;

/* --Misc utility methods-- */
Bool same_clip_objects(
  /*@null@*/ /*@in@*/           CLIPOBJECT *cl1 ,
  /*@null@*/ /*@in@*/           CLIPOBJECT *cl2 ,
                                Bool fAllowFuzzy ,
                                int32 clipRectEpsilon ) ;

Bool same_nfill_objects(
  /*@null@*/ /*@in@*/           NFILLOBJECT *f1 ,
  /*@null@*/ /*@in@*/           NFILLOBJECT *f2 ) ;

/* Get the intersection bounding box of a CLIPOBJECT, and determine if
   it has an nfill object in it. */
void get_cliprect_intersectbbox(
  /*@notnull@*/ /*@in@*/        CLIPOBJECT *cl ,
  /*@notnull@*/ /*@out@*/       dbbox_t *intersect_bbox ,
  /*@notnull@*/ /*@out@*/       Bool *has_nfill ) ;

typedef Bool ( *CLIPCACHE_FORALL_FN )(
  /*@notnull@*/ /*@in@*/        CLIPOBJECT *clip ,
  /*@notnull@*/ /*@in@*/        dbbox_t *bbox ,
                                Bool has_nfill ,
  /*@null@*/                    void *args ) ;

Bool clipcache_forall(
  /*@notnull@*/                 DlStateStore* store,
  /*@notnull@*/                 CLIPCACHE_FORALL_FN fn ,
  /*@null@*/                    void *args ) ;

#endif

/* --Description--

For a description of the DlSSEntry base class, refer to dl_storet.h.

The DlStateStore object provides methods to manage objects derived from the
DlSSEntry base class. The store is a content-addressable store of object
instances; to obtain a pointer to a stored object one generally initializes
a local variable to the required values, and a call to dlSSInsert() will
return a stored object with the same values. If the requested object is not
present in the store, a copy of it is inserted. If the 'insertCopy' parameter
false, then the parameter entry will itself be inserted; note that the caller
is responsible for allocating the entry in this case.

The store will always deallocate all the objects it contains on a call to
dlSSFree().

dlSSRemove() does not deallocate the stored entry - it is simply removed from
store.

The dlSSPreserve() method allows objects to survive a single call to
dlSSFree(). It's acceptable to preserve the same object multiple times (without
calling dlSSFree()) - subsequent calls on the same object have no effect.
The 'preserveChildren' parameter allows all sub-objects which are held in the
state store to be preserved too.
*/


/* Log stripped */
