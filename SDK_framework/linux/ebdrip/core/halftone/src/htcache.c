/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:htcache.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Manages the halftone cache (not the internals of the screens).
 */

#include "core.h"
#include "htpriv.h"
#include "halftone.h" /* external interface of this file */

#include "mm.h"
#include "mmcompat.h" /* mm_alloc_static */
#include "lowmem.h"
#include "mps.h"
#include "swstart.h"
#include "hqassert.h"
#include "gu_htm.h" /* htm_AddRefHalftoneRef */
#include "lists.h"
#include "objecth.h"  /* NAMECACHE */
#include "monitor.h"
#include "color.h"    /* HT_TRANSFORM_INFO */
#include "swerrors.h" /* error_handler */
#include "params.h"
#include "hqmemset.h"
#include "objnamer.h"
#include "mlock.h"
#include "swtrace.h"


/* Note this must be a power of two. */
#define HALFTONECACHESIZE ( 1 << 5 )


#if defined( ASSERT_BUILD )
Bool debug_halftonecache = FALSE ;
#endif


#define RESET_CLONED_SPOTID 0xFFFF
static SPOTNO clonedspotid = RESET_CLONED_SPOTID; /* the id of the last 'cloned' spotno */


struct LISTCHALFTONE {
  SPOTNO spotno ;
  COLORANTINDEX ci;
  HTTYPE type;
  int8 calibration_warning; /* really Bool */
  NAMECACHE *sfcolor ;
  NAMECACHE *htname ; /* E.g. Agfa, SimpleDot. */
  dl_erase_nr last_used_dl; /* DL this screen was last used in */
  int32 phasex , phasey ;
  int32 halfcx , halfcy ;
  struct CHALFTONE *chptr ;
  struct MODHTONE_REF *mhtref ; /* only for modular screens */
  struct LISTCHALFTONE *next ;
};


#define theISpotNo(val) ((val)->spotno)
#define theIHalfColor(val) ((val)->ci)

#define theIHalfCx(val) ((val)->halfcx)
#define theIHalfCy(val) ((val)->halfcy)
#define theIChPtr(val) ((val)->chptr)
#define theIMhtRef(val) ((val)->mhtref)

#define theIHalfPx(val) ((val)->phasex)
#define theIHalfPy(val) ((val)->phasey)


multi_mutex_t ht_cache_mutex;


/*
 * Cache of last spot id and colorant index to optimise cache searching.
 * This cache should only be accessed in:
 *  invalidate_current_halftone
 *  safe_free_listchalftone
 *  ht_getlistch
 *  ht_getCachedTransformInfo
 *
 * There are actually two almost independent caches in here. They are together
 * because they both get invalidated when the spotno changes. Both are caches of
 * info for (practically) all colorant indices with a given spotno.
 *
 * One cache is for listch which gives fast access for a given colorant index.
 * The other cache is for transform info required by ht_applyTransform().
 *
 * The set of colorant indices include COLORANTINDEX_ALL and COLORANTINDEX_NONE,
 * along with a number of indices up to and including HT_MAX_COLORANT_CACHE that
 * is rarely exceeded in practice. Since the special colorants have negative
 * numbers, we use 'listch' to point to an element of 'listchArray' which allows
 * 'listch' to have a colorant index as its subscript. Similarly for
 * 'transformInfo' and 'transformInfoArray'.
 *
 * The 'listch' cache is only valid while the 'nSpotIdLast' remains unchanged.
 * As soon as it has changed, the client of the cache is expected to invalidate
 * the whole of the cache. Likewise for the transform info, which also requires
 * the client invalidate the whole cache whenever levelsLast changes.
 */
typedef struct HT_COLOR_INFO {
  int32 levelsLast;
  HT_TRANSFORM_INFO transformInfoArray[HTTYPE_DEFAULT+1][HT_MAX_COLORANT_CACHE + 1 + HT_OFFSET_COLORANT_CACHE];
  HT_TRANSFORM_INFO *transformInfo[HTTYPE_DEFAULT+1];
} HT_COLOR_INFO;

typedef struct {
  SPOTNO            nSpotIdLast;
  LISTCHALFTONE     *listchArray[HTTYPE_DEFAULT+1][HT_MAX_COLORANT_CACHE + 1 + HT_OFFSET_COLORANT_CACHE];
  LISTCHALFTONE     **listch[HTTYPE_DEFAULT+1];
  HT_COLOR_INFO     colInfo;

  OBJECT_NAME_MEMBER
} LAST_SPOT_CACHE;

#define LAST_SPOT_CACHE_NAME "Last spot cache"

/** The per-thread halftone cache context. */
typedef struct ht_context_t {
  LAST_SPOT_CACHE last_spot_cache ;
  /** Global list of all thread contexts. The interpreter's context appears
      first on the list. This list is only used by
      ht_invalidate_all_current_halftones(). It would be nice to get rid of
      it. */
  struct ht_context_t *next ;
} ht_context_t ;

static ht_context_t interpreter_ht_context ;


static LAST_SPOT_CACHE *fetch_last_spot(
  corecontext_t *context)
{
  LAST_SPOT_CACHE *lsc;

  if (!context) {
    context = get_core_context();
  }
  lsc = &context->ht_context->last_spot_cache;
  VERIFY_OBJECT(lsc, LAST_SPOT_CACHE_NAME);
  return (lsc);
}


static LISTCHALFTONE **hctable = NULL ; /* the halftone cache */
static mps_root_t hctable_root;
static Bool seen_obj_based_screen = FALSE; /* have we ever seen an obj screen */

static mps_res_t MPS_CALL hctable_scan(mps_ss_t ss, void *p, size_t s);


#if defined(DEBUG_BUILD)

LAST_SPOT_CACHE *ht_getLastSpotCache(void)
{
  return fetch_last_spot(NULL);
}

#endif /* DEBUG_BUILD */


/* Invalidate halftone cache state for the current thread when, e.g. a spotno
 * changes. */
static void invalidate_current_halftone(LAST_SPOT_CACHE *spot_cache)
{
  COLORANTINDEX i;
  HTTYPE t;

  VERIFY_OBJECT(spot_cache, LAST_SPOT_CACHE_NAME) ;

  spot_cache->nSpotIdLast = SPOT_NO_INVALID;
  for ( t = 0 ; t <= HTTYPE_DEFAULT ; ++t ) {
    spot_cache->listch[t] = &spot_cache->listchArray[t][HT_OFFSET_COLORANT_CACHE];
    for (i = -HT_OFFSET_COLORANT_CACHE; i <= HT_MAX_COLORANT_CACHE; i++)
      spot_cache->listch[t][i] = NULL;
  }
  spot_cache->colInfo.levelsLast = -1;
  for ( t = 0 ; t <= HTTYPE_DEFAULT ; ++t ) {
    spot_cache->colInfo.transformInfo[t] =
      &spot_cache->colInfo.transformInfoArray[t][HT_OFFSET_COLORANT_CACHE];
    for ( i = -HT_OFFSET_COLORANT_CACHE ; i <= HT_MAX_COLORANT_CACHE ; i++ )
      ht_invalidate_transformInfo(&spot_cache->colInfo.transformInfo[t][i]);
  }
}

#if 0
/** \brief Invalidate halftone cache state for all threads.

  This must be called holding \c ht_cache_mutex.
 */
static void ht_invalidate_all_current_halftones(void)
{
  ht_context_t *ht_context ;

  for (ht_context = &interpreter_ht_context ;
       ht_context != NULL ;
       ht_context = ht_context->next ) {
    invalidate_current_halftone(&ht_context->last_spot_cache);
  }
}
#endif

/* Return a pointer to a transformInfo to be populated */
HT_TRANSFORM_INFO *ht_getCachedTransformInfo(SPOTNO spotno,
                                             HTTYPE type,
                                             Bool halftoning,
                                             int32 contone_levels)
{
  LAST_SPOT_CACHE *last_spot;
  int32 levels = halftoning ? 0 : contone_levels;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");

  last_spot = fetch_last_spot(NULL);

  if ( spotno != last_spot->nSpotIdLast
       || levels != last_spot->colInfo.levelsLast ) {
    invalidate_current_halftone(last_spot);

    last_spot->nSpotIdLast = spotno;
    last_spot->colInfo.levelsLast = levels;
  }

  return last_spot->colInfo.transformInfo[type];
}


CHALFTONE *ht_listchChalftone(LISTCHALFTONE *listch)
{
  return listch->chptr;
}


MODHTONE_REF *ht_listchMhtref(LISTCHALFTONE *listch)
{
  return listch->mhtref;
}


Bool ht_listchIsModular(LISTCHALFTONE *listch)
{
  return listch->mhtref != NULL;
}


const uint8 *ht_listchModularName(LISTCHALFTONE *listch)
{
  HQASSERT(listch->mhtref != NULL, "Halftone is not modular") ;
  return listch->mhtref->mod_entry->impl->info.name ;
}


NAMECACHE *ht_listchHalfToneName(LISTCHALFTONE *listch)
{
  return listch->htname;
}


NAMECACHE *ht_listchSFColor(LISTCHALFTONE *listch)
{
  return listch->sfcolor;
}


Bool ht_listchCalibrationWarning(LISTCHALFTONE *listch)
{
  return listch->calibration_warning;
}


dl_erase_nr ht_listch_last_used_dl(LISTCHALFTONE *listch)
{
  return listch->last_used_dl;
}


void ht_listchCXY(LISTCHALFTONE* listch, int32* cx, int32* cy)
{
  *cx = listch->halfcx; *cy = listch->halfcy;
}

void ht_listchPhaseXY(LISTCHALFTONE *listch, int32 *px, int32 *py)
{
  *px = listch->phasex; *py = listch->phasey;
}


/* ---------------------------------------------------------------------- */
/* ---- Cache iteration ---- */


void ht_iterChentryBegin(ht_cacheIterator *iter,
                         dl_erase_nr oldest_dl,
                         SPOTNO start_spotno, COLORANTINDEX select_ci)
{
  LISTCHALFTONE *plistch;
  uint8 type;

  HQASSERT(iter != NULL, "No iterator");
  HQASSERT(select_ci == COLORANTINDEX_UNKNOWN || HT_CI_VALID(select_ci),
           "Invalid colorant index");

  multi_mutex_lock(&ht_cache_mutex);
  if (start_spotno == SPOT_NO_INVALID) {
    /*  start_spotno == SPOT_NO_INVALID (meaning 'don't care') iterates
        from first slot to last. */
    iter->slot = 0; plistch = hctable[0];
  } else {
    iter->slot = (size_t)start_spotno & (HALFTONECACHESIZE - 1);
    plistch = hctable[iter->slot];
    while ( plistch != NULL && plistch->spotno != start_spotno
            && plistch->last_used_dl >= oldest_dl )
      plistch = plistch->next;
    HQASSERT(plistch != NULL, "Iteration start spotno didn't exist");
  }
  iter->curr = plistch;
  iter->last_spotno = iter->start_spotno = SPOT_NO_INVALID;
  for ( type = 0 ; type <= HTTYPE_DEFAULT ; ++type )
    iter->last_none[type] = NULL;
  iter->next_none_type = HTTYPE_DEFAULT + 1; /* don't go round the loop */
  iter->select_ci = select_ci; iter->oldest_dl = oldest_dl;
}



Bool ht_iterChentryNext(ht_cacheIterator *iter,
                        LISTCHALFTONE **listch_out,
                        CHALFTONE **ch_out,
                        MODHTONE_REF **mhtref_out,
                        SPOTNO *spotno_out,
                        HTTYPE *type_out,
                        COLORANTINDEX *ci_out)
{
  LISTCHALFTONE *plistch;
  uint8 type;
  Bool goneRound = FALSE;
  size_t slot;

  HQASSERT(iter != NULL, "No iterator");
  /* When iterating a selected ci, the state of this iterator alternates
     between its normal entries and any defaults (COLORANTINDEX_NONE).
     The latter are produced by the type loop in the middle (look for
     'goto found'); the former from the two outer loops.  The default
     state is entered when the spotno changes.  In it, iter->curr points
     to the next entry for the outer loops, but as long as it doesn't
     match last_spotno, control goes to the type loop. */
  /** \todo This code will process HTTYPE_DEFAULT entries, even if all
     the four specific type entries are present.  This does not match
     the lookup, but is fairly harmless. */
  plistch =
    iter->start_spotno != SPOT_NO_INVALID ? iter->curr->next : iter->curr;
  slot = iter->slot;
  for (;;) { /* loop over slots */
    while ( plistch != NULL ) { /* loop over chain */
      if ( plistch->spotno != iter->last_spotno ) { /* new spot */
        if ( iter->select_ci != COLORANTINDEX_UNKNOWN ) {
          /* See if any defaults need to be produced */
          for ( type = iter->next_none_type ; type <= HTTYPE_DEFAULT ; ++type )
            if ( iter->last_none[type] != NULL ) { /* No entry for ci in the */
              ++iter->next_none_type;              /* last spot, so give. */
              plistch = iter->last_none[type];
              iter->last_none[type] = NULL;
              goto found;
            }
          iter->next_none_type = 0;
        }
        if ( plistch->spotno == iter->start_spotno )
          /* Reached start_spotno again, end iteration. */
          return FALSE;
        if ( iter->start_spotno == SPOT_NO_INVALID )
          /* First new spot seen, remember it */
          iter->start_spotno = plistch->spotno;
        iter->last_spotno = plistch->spotno;
      }

      iter->curr = plistch;
      iter->slot = slot;
      if ( plistch->last_used_dl >= iter->oldest_dl ) {
        if ( iter->select_ci == COLORANTINDEX_UNKNOWN )
          /* if not selecting on ci, any entry will do */
          goto found;
        else if ( iter->select_ci == plistch->ci ) {
          iter->last_none[plistch->type] = NULL; /* stop default being used */
          goto found;
        } else if ( plistch->ci == COLORANTINDEX_NONE )
          iter->last_none[plistch->type] = plistch; /* remember default */
      }
      plistch = plistch->next;
    } /* loop over chain */

    /* Force spot end, in case there's only one spot in the cache */
    iter->last_spotno = SPOT_NO_INVALID;

    if (++slot == HALFTONECACHESIZE) {
      if ( goneRound )
        return FALSE; /* cache was empty, quit */
      goneRound = TRUE; slot = 0;
    }
    plistch = hctable[slot];
  }

 found:
  if (listch_out != NULL)
    *listch_out = plistch;
  if (ch_out != NULL)
    *ch_out = plistch->chptr;
  if (mhtref_out != NULL)
    *mhtref_out = plistch->mhtref;
  if (spotno_out != NULL)
    *spotno_out = plistch->spotno;
  if (type_out != NULL)
    *type_out = plistch->type;
  if (ci_out != NULL)
    *ci_out = plistch->ci;
  return TRUE;
}


void ht_iterChentryEnd(ht_cacheIterator *iter)
{
  UNUSED_PARAM(ht_cacheIterator *, iter);
  multi_mutex_unlock(&ht_cache_mutex);
}


/** Create a new LISTCHALFTONE which is a duplicate of an existing one,
 * bumping ref-counts of the underlying CHALFTONE and MODHTONE_REF etc.
 * Note that the chain pointer in the new structure is always NULL.
 * Returns NULL if unable to allocate the memory for the new structure.
 */
static LISTCHALFTONE *duplicate_listchalftone( LISTCHALFTONE *listchptr )
{
  LISTCHALFTONE *pnewlistch;

  pnewlistch = (LISTCHALFTONE*)mm_alloc(mm_pool_temp, sizeof(LISTCHALFTONE),
                                        MM_ALLOC_CLASS_LISTCHALFTONE);
  if ( NULL != pnewlistch ) {
    *pnewlistch = *listchptr ;

    /* Ensure the chain pointer isn't duplicated */
    pnewlistch->next = NULL ;

    /* Bump reference counts */
    if ( pnewlistch->chptr != NULL )
      addRefChPtr( pnewlistch->chptr );
    if ( theIMhtRef( pnewlistch ) )
      htm_AddRefHalftoneRef( theIMhtRef( pnewlistch ));
  }

  return pnewlistch ;
}


LISTCHALFTONE *ht_listchAlloc(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci,
                              CHALFTONE *pch,
                              NAMECACHE *sfcolor, NAMECACHE *htname,
                              MODHTONE_REF *mhtref,
                              int32 phasex, int32 phasey)
{
  LISTCHALFTONE *listchptr = mm_alloc(mm_pool_temp, sizeof(LISTCHALFTONE),
                                      MM_ALLOC_CLASS_LISTCHALFTONE);
  if ( listchptr == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  listchptr->spotno = spotno; listchptr->type = type; listchptr->ci = ci;
  listchptr->sfcolor = sfcolor; listchptr->htname = htname;
  listchptr->last_used_dl = MAX_DL_ERASE_NR;
  ht_converge_phase(pch, &phasex, &phasey,
                    &listchptr->phasex, &listchptr->phasey);
  listchptr->halfcx = phasex; listchptr->halfcy = phasey;
  listchptr->calibration_warning = FALSE;
  listchptr->chptr = pch;
  listchptr->mhtref = mhtref;
  return listchptr;
}


/* Free a LISTCHALFTONE structure.
 *
 * Also releases references to underlying CHALFTONE and MODHTONE_REF
 * if the tidyrefs argument is TRUE.
 */
void safe_free_listchalftone(
  LISTCHALFTONE*  plistch,   /* I */
  Bool            tidyrefs   /* I */ )
{
  HQASSERT((plistch != NULL),
           "safe_free_listchalftone: NULL pointer to listch to be freed");
  HQASSERT(HT_CI_VALID(plistch->ci),
           "safe_free_listchalftone: invalid colorant index");

  if ( tidyrefs ) {
    if ( NULL != theIChPtr( plistch ) )
      releaseChPtr( theIChPtr( plistch ));
    if ( NULL != theIMhtRef( plistch ) )
      htm_ReleaseHalftoneRef( theIMhtRef( plistch ));
  }

  mm_free(mm_pool_temp, (mm_addr_t)plistch, sizeof(LISTCHALFTONE));
}


#if defined( ASSERT_BUILD )
static Bool debug_equivalentchspotid = FALSE ;
#endif


/* ----------------------------------------------------------------------
 *   When merging preseparations (and overprinting to a backdrop), two
 *   objects may coincide with different spot numbers. In this case, what is
 *   needed is a NEW spotnumber (starting from clonedspotid & decrement)
 *   which has all of the old screens, plus the new screen.
 */
typedef struct ht_clonespots {
  dll_link_t    dll;          /* Link to other clones in list */
  SPOTNO        oldspotno;
  SPOTNO        newspotno;
  HTTYPE        newtype;
  COLORANTINDEX newci;
  SPOTNO        clonespotno;
} HT_CLONES;

static dll_list_t gdlsCloneFree;
static dll_list_t gdlsCloneActive;

#define HT_MAXCLONES  (16)
static HT_CLONES  grgClone[HT_MAXCLONES];

#if defined( ASSERT_BUILD )
static Bool gfCloneInit = FALSE;
#endif

static void ht_clone_init(void)
{
  int32 iClone;

  /* Reset lists to be empty */
  DLL_RESET_LIST(&gdlsCloneActive);
  DLL_RESET_LIST(&gdlsCloneFree);

  /* Add all cache entries to free list */
  for ( iClone = 0; iClone < HT_MAXCLONES; iClone++ ) {
    DLL_RESET_LINK(&grgClone[iClone], dll);
    DLL_ADD_TAIL(&gdlsCloneFree, &grgClone[iClone], dll);
  }

#if defined( ASSERT_BUILD )
  gfCloneInit = TRUE;
#endif
} /* Function ht_clone_init */


static void ht_clone_reset(void)
{
  HQASSERT((gfCloneInit),
           "ht_clone_reset: ht clone cache not initialised");

  /* Move all active entries back to free list */
  DLL_LIST_APPEND(&gdlsCloneFree, &gdlsCloneActive);

  HQASSERT((DLL_LIST_IS_EMPTY(&gdlsCloneActive)),
           "ht_clone_reset: failed to empty active list");

}


/* Keep track in the page default spot here for modularity reasons. */
static SPOTNO page_default_spot = SPOT_NO_INVALID;


void ht_set_page_default(SPOTNO spotno)
{
  if ( spotno != page_default_spot ) {
    SPOTNO old_default = page_default_spot;

    page_default_spot = spotno;
    if ( old_default != SPOT_NO_INVALID )
      (void) purgehalftones(old_default, FALSE);
  }
}


/** If names referred to by the screens are about to be restored away,
    remove them (these entries will not be used again). */
void htcache_restore_names(int32 slevel)
{
  size_t i;
  LISTCHALFTONE *listchptr;

  multi_mutex_lock(&ht_cache_mutex);
  for ( i = 0; i < HALFTONECACHESIZE; ++i ) {
    listchptr = hctable[i];
    while ( listchptr != NULL ) {
      if ( listchptr->sfcolor != NULL && listchptr->sfcolor->sid > slevel )
        listchptr->sfcolor = NULL;
      if ( listchptr->htname != NULL && listchptr->htname->sid > slevel )
        listchptr->htname = NULL;
      if ( listchptr->chptr != NULL )
        chalftone_restore_names(listchptr->chptr, slevel);
      listchptr = listchptr->next;
    }
  }
  multi_mutex_unlock(&ht_cache_mutex);
}


#define UPDATE_LAST_USED(plistch, eraseno) MACRO_START \
  if ( (plistch)->last_used_dl != MAX_DL_ERASE_NR ) \
    (plistch)->last_used_dl = (eraseno); \
MACRO_END


void ht_change_non_purgable_screen(SPOTNO oldspot, SPOTNO newspot)
{
  LISTCHALFTONE*  plistch;

  HQASSERT(newspot > 0, "invalid spot number");

  if ( oldspot == newspot )
    return;

  if ( oldspot > 0 )
    /* Make old screen purgeable in low-memory handling on the next page. */
    (void)purgehalftones(oldspot, FALSE);

  /* Ensure the new screen does not get freed. */
  multi_mutex_lock(&ht_cache_mutex);
  for ( plistch = hctable[newspot & (HALFTONECACHESIZE-1)] ; plistch != NULL ;
        plistch = plistch->next )
    if ( plistch->spotno == newspot )
      plistch->last_used_dl = MAX_DL_ERASE_NR;
  multi_mutex_unlock(&ht_cache_mutex);
}


Bool purgehalftones(
  SPOTNO  spotno,
  Bool    dopurge)
{
  LISTCHALFTONE* listchptr, *previous, *thisone;

  HQASSERT(spotno > 0, "invalid spot number to purge");

  if ( spotno == page_default_spot )
    return FALSE;

  multi_mutex_lock(&ht_cache_mutex);

  listchptr = hctable[spotno&(HALFTONECACHESIZE - 1)];

  if ( !dopurge ) {

    /* Now that this spotno is not in active use, update last_used, and
       leave it for low memory to purge. */
    while ( listchptr != NULL ) {
      if ( listchptr->spotno == spotno ) {
        /* Reset ht erase count to the latest dl it was used on */
        listchptr->last_used_dl =
          listchptr->chptr ? chalftone_last_used(listchptr->chptr)
                           : mhtref_last_used(listchptr->mhtref);
      }
      listchptr = listchptr->next;
    }

  } else { /* Remove spotno from cache, it was never used. */

    /* It's not necessary to invalidate the last spots, because no one
       should have any references to purgeable spotnos. */

    previous = NULL;
    while ( listchptr != NULL ) {
      if ( theISpotNo(listchptr) == spotno ) {
        /* Found our spotno - remove it */
        HQTRACE(debug_halftonecache,
                ("purgehalftones: %d %d", spotno, theIHalfColor(listchptr)));

        /* Link up the chain around this listch, handling start of ht cache chain */
        if ( previous != NULL ) {
          previous->next = listchptr->next;
        } else {
          hctable[spotno&(HALFTONECACHESIZE - 1)] = listchptr->next;
        }

        /* Move to next listch */
        thisone = listchptr;
        listchptr = listchptr->next;

        /* Now safely free off this listch */
        safe_free_listchalftone( thisone, TRUE );
      } else { /* Not our spotno, move onto next in chain */
        previous = listchptr;
        listchptr = listchptr->next;
      }
    }

  } /* if !dopurge */
  multi_mutex_unlock(&ht_cache_mutex);
  return dopurge;
} /* Function purgehalftones */


#if defined( ASSERT_BUILD )

/*
 * Function:    ht_checkCache
 *
 * Purpose:     To check that screen entries are in expected order
 *
 * Arguments:   spotno    - screen family to check
 *
 * Returns:     TRUE if in expected order, else FALSE
 *
 * Notes:       The expected order is NONE followed by colorants in increasing
 *              index order, some possibly omitted
 */
static Bool ht_checkCache(
  SPOTNO spotno)
{
  COLORANTINDEX   ciPrev;
  HTTYPE          typePrev;
  LISTCHALFTONE*  plistch;

  HQASSERT((spotno > 0),
           "ht_checkCache: invalid spot number");

  plistch = hctable[spotno&(HALFTONECACHESIZE-1)];
  if ( plistch == NULL )
    return FALSE; /* couldn't find it */

  /* Find first spotno entry */
  while ( plistch != NULL && plistch->spotno != spotno )
    plistch = plistch->next;
  if ( plistch == NULL)
    return FALSE; /* couldn't find it */

  for (;;) {
    ciPrev = plistch->ci; typePrev = plistch->type;
    plistch = plistch->next;
    /* Loop over chain until end or different spotno */
    if ( plistch == NULL || plistch->spotno != spotno )
      break;
    if ( ciPrev > plistch->ci
         || (ciPrev == plistch->ci && typePrev >= plistch->type) )
      return FALSE; /* wrong order */
  }
  while ( plistch != NULL ) {
    if ( plistch->spotno == spotno )
      return FALSE; /* additional entry after some different spotno */
    plistch = plistch->next;
  }
  return TRUE;
} /* Function ht_checkCache */

#endif /* defined( ASSERT_BUILD ) */


/*
 * Function:    ht_cacheInsert
 *
 * Purpose:     To insert a LISTCHALFTONE into the cache in predefined order
 *
 * Arguments:   plistch     - pointer to list ch struct to insert in cache
 *
 * Returns:     Nothing
 */
void ht_cacheInsert(
  LISTCHALFTONE*  plistch)
{
  LISTCHALFTONE*  plistchNext;
  LISTCHALFTONE** pplistchPrev;

  HQASSERT((plistch != NULL),
           "ht_cacheInsert: NULL pointer to list ch struct");
  HQASSERT(HTTYPE_VALID(plistch->type), "ht_cacheInsert: invalid type");
  HQASSERT(COLORANTINDEX_VALID(plistch->ci), "ht_cacheInsert: invalid ci");

  if ( plistch->type != HTTYPE_DEFAULT )
    seen_obj_based_screen = TRUE;

  multi_mutex_lock(&ht_cache_mutex);

  /* Find the one to insert before (list ordered by ascending spotno, ci, type). */
  for (pplistchPrev = & hctable[theISpotNo(plistch)&(HALFTONECACHESIZE-1)],
         plistchNext = * pplistchPrev;
       plistchNext != NULL;
       pplistchPrev = & (plistchNext->next), plistchNext = plistchNext->next)
    if ( plistch->spotno < plistchNext->spotno
         || (plistch->spotno == plistchNext->spotno
             && (plistch->ci < plistchNext->ci
                 || (plistch->ci == plistchNext->ci
                     && plistch->type < plistchNext->type))) )
      break;

  HQTRACE(debug_halftonecache,
          ("ht_cacheInsert: %d %d %d",
           theISpotNo(plistch), plistch->type, theIHalfColor(plistch)));

  /* Invalidate last_spot (only interpreter thread, as inserts only
     happens during interpretation) */
  if ( interpreter_ht_context.last_spot_cache.nSpotIdLast == plistch->spotno )
    interpreter_ht_context.last_spot_cache.nSpotIdLast = SPOT_NO_INVALID;

  /* and insert, and check */

  * pplistchPrev = plistch;
  plistch->next = plistchNext;
  HQASSERT((ht_checkCache(theISpotNo(plistch))),
           "ht_cacheInsert: cache in invalid state after insertion");
  multi_mutex_unlock(&ht_cache_mutex);
} /* Function ht_cacheInsert */


/* Given a /Default screen, check if any non-/Default of the same spot are used. */
static Bool any_non_default_screens_used(LISTCHALFTONE *listch)
{
  SPOTNO spot = listch->spotno;

  while ( (listch = listch->next) != NULL && listch->spotno == spot )
    if ( listch->ci != COLORANTINDEX_NONE
         && listch->last_used_dl >= oldest_dl )
      /* Strickly speaking, should check types are compatible, but we can afford
         to keep all types of /Default screens. */
      return TRUE;
  return FALSE;
}


/** Solicit method of the halftone cache low-memory handler. */
static low_mem_offer_t *ht_cache_solicit(low_mem_handler_t *handler,
                                         corecontext_t *context,
                                         size_t count,
                                         memory_requirement_t* requests)
{
  static low_mem_offer_t offer;
  size_t i;
  LISTCHALFTONE *listchptr;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(corecontext_t *, context);
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  offer.offer_size = 0;
  if ( !multi_mutex_trylock(&ht_cache_mutex) )
    return NULL;
  for ( i = 0; i < HALFTONECACHESIZE; ++i ) {
    listchptr = hctable[i];
    while ( listchptr != NULL ) {
      if ( listchptr->last_used_dl < oldest_dl
           /* Only purge /Default, if there are no others in the spot. */
           && (listchptr->ci != COLORANTINDEX_NONE
               || !any_non_default_screens_used(listchptr)) )
        offer.offer_size += sizeof(LISTCHALFTONE); /* @@@@ */
      listchptr = listchptr->next;
    }
  }
  multi_mutex_unlock(&ht_cache_mutex);
  if ( offer.offer_size == 0 )
    return NULL;
  offer.pool = mm_pool_temp;
  offer.offer_cost = 1.0; /* @@@@ */
  offer.next = NULL;
  return &offer;
}


/** Release method of the halftone cache low-memory handler. */
static Bool ht_cache_release(low_mem_handler_t *handler,
                             corecontext_t *context,
                             low_mem_offer_t *offer)
{
  size_t i;
  LISTCHALFTONE *listchptr, *next_listchptr;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  if ( !multi_mutex_trylock(&ht_cache_mutex) )
    return TRUE; /* give up if can't get the lock */
  /* It's not necessary to invalidate the last spots, because no one
     should have any references to purgeable spotnos. */
  ht_clone_reset();
  for ( i = 0; i < HALFTONECACHESIZE; ++i ) {
    listchptr = hctable[i];
    while ( listchptr != NULL ) {
      next_listchptr = listchptr->next;
      if ( listchptr->last_used_dl < oldest_dl
           /* Only purge /Default, if there are no others in the spot. */
           && (listchptr->ci != COLORANTINDEX_NONE
               || !any_non_default_screens_used(listchptr)) )
        ht_cacheRemove(listchptr->spotno, listchptr->type, listchptr->ci);
      listchptr = next_listchptr;
    }
  }
  multi_mutex_unlock(&ht_cache_mutex);
  return TRUE;
}


static low_mem_handler_t ht_cache_handler = {
  "Halftone screen cache",
  memory_tier_disk, ht_cache_solicit, ht_cache_release, TRUE,
  0, FALSE };


static mps_res_t MPS_CALL hctable_scan(mps_ss_t ss, void *p, size_t s)
{
  size_t i;

  UNUSED_PARAM( void *, p ); UNUSED_PARAM( size_t, s );
  MPS_SCAN_BEGIN( ss )
    /* map over the entire table */
    for ( i = 0; i < HALFTONECACHESIZE; ++i ) {
      register LISTCHALFTONE *listchptr = hctable[i];

      while ( listchptr != NULL ) {
        MPS_RETAIN( &listchptr->sfcolor, TRUE );
        if ( listchptr->htname != NULL)
          MPS_RETAIN( &listchptr->htname, TRUE );
        if ( listchptr->chptr != NULL )
          MPS_SCAN_CALL(chalftone_scan(ss, listchptr->chptr));
        listchptr = listchptr->next;
      }
    }
  MPS_SCAN_END( ss );
  return MPS_RES_OK;
}


/** Mark all screens of given spotno used on given DL. */
static void ht_mark_spot_used(SPOTNO spotno, dl_erase_nr eraseno)
{
  LISTCHALFTONE *plistch = hctable[spotno & (HALFTONECACHESIZE - 1)];
  for ( plistch = hctable[spotno & (HALFTONECACHESIZE-1)] ; plistch != NULL ;
        plistch = plistch->next )
    if ( plistch->spotno == spotno )
      UPDATE_LAST_USED(plistch, eraseno);
}


#if defined( ASSERT_BUILD )
static Bool debug_mergespotnoentry = FALSE ;
#endif


SPOTNO ht_mergespotnoentry(
  SPOTNO        oldspotno,
  SPOTNO        newspotno,
  HTTYPE        newtype,
  COLORANTINDEX newci,
  dl_erase_nr eraseno)
{
  SPOTNO          clonespotno;
  HTTYPE          copiedtype;
  LISTCHALFTONE*  newlistchptr;
  LISTCHALFTONE*  oldlistchptr;
  LISTCHALFTONE*  plistch;
  LISTCHALFTONE*  listchptr;
  HT_CLONES*  pClone;

  HQASSERT((oldspotno > 0),
           "ht_mergespotnoentry: invalid old spotno");
  HQASSERT((newspotno > 0),
           "ht_mergespotnoentry: invalid new spotno");
  HQASSERT((oldspotno != newspotno),
           "ht_mergespotnoentry: old and new spotnos are the same");
  HQASSERT(HTTYPE_VALID(newtype), "invalid type");
  HQASSERT(HT_CI_VALID(newci),
           "ht_mergespotnoentry: invalid colorant index");

  multi_mutex_lock(&ht_cache_mutex);

  /* Loop through cache for existing merge entry */
  pClone = DLL_GET_HEAD(&gdlsCloneActive, HT_CLONES, dll);
  while ( pClone != NULL ) {
    if ( (pClone->oldspotno == oldspotno) &&
         (pClone->newspotno == newspotno) &&
         (pClone->newtype   == newtype) &&
         (pClone->newci     == newci) ) {
      /* We have seen this combination before - nothing to do */
      HQTRACE(debug_mergespotnoentry,
              ("Hit cache: %d == %d(%d, %u)", oldspotno,
               newspotno, newci, newtype));

      if ( !DLL_FIRST_IN_LIST(pClone, dll) ) {
        /* Move matching entry to start of list */
        DLL_REMOVE(pClone, dll);
        DLL_ADD_HEAD(&gdlsCloneActive, pClone, dll);
      }
      ht_mark_spot_used(pClone->clonespotno, eraseno);

      multi_mutex_unlock(&ht_cache_mutex);
      return pClone->clonespotno;
    }

    pClone = DLL_GET_NEXT(pClone, HT_CLONES, dll);
  }

  /* Pick up either free or last used clone entry ... */
  pClone = !DLL_LIST_IS_EMPTY(&gdlsCloneFree)
              ? DLL_GET_TAIL(&gdlsCloneFree, HT_CLONES, dll)
              : DLL_GET_TAIL(&gdlsCloneActive, HT_CLONES, dll);

  /* ... and add to head of active list */
  DLL_REMOVE(pClone, dll);
  DLL_ADD_HEAD(&gdlsCloneActive, pClone, dll);

  /* Fill in cache matching info */
  pClone->oldspotno = oldspotno;
  pClone->newspotno = newspotno;
  pClone->newtype   = newtype;
  pClone->newci     = newci;

  /* Extract newci from old and new spotnos */
  newlistchptr = ht_getlistch_exact(newspotno, newtype, newci);
  if ( newlistchptr == NULL && newtype != HTTYPE_DEFAULT )
    newlistchptr = ht_getlistch_exact(newspotno, HTTYPE_DEFAULT, newci);

  if (newlistchptr == NULL) {
    /* Ended up using the default entry for newci (as newci is not produced
       on the output device). Do not need to merge this with oldspotno as it
       should already have a default. (Note: the default in oldspotno may
       not be the same as the default in newspotno, but for now, use the
       oldspotno default in any case. If this turns out to be a problem then
       spots can be compared before deciding to merge the two DL objects.) */
#if defined( ASSERT_BUILD )
    if (debug_mergespotnoentry) {
      newlistchptr = ht_getlistch(newspotno, newtype, newci, NULL);
      HQTRACE(newlistchptr != NULL && newlistchptr->ci == COLORANTINDEX_NONE,
              ("ht_mergespotnoentry: expected default from newspotno"));
      oldlistchptr = ht_getlistch(oldspotno, newtype, newci, NULL);
      HQTRACE(oldlistchptr != NULL && oldlistchptr->ci == COLORANTINDEX_NONE,
              ("ht_mergespotnoentry: expected default from oldspotno"));
      HQTRACE(newlistchptr != NULL && oldlistchptr != NULL
              && newlistchptr->chptr == oldlistchptr->chptr
              && newlistchptr->mhtref == oldlistchptr->mhtref
              && newlistchptr->phasex == oldlistchptr->phasex
              && newlistchptr->phasey == oldlistchptr->phasey,
              ("ht_mergespotnoentry: expected defaults to match"));
    }
#endif
    /* Remember the spotno we can use for this combination */
    pClone->clonespotno = oldspotno;
    multi_mutex_unlock(&ht_cache_mutex);
    return 0; /* Flag to continue using oldspotno - yuck */
  }

  /* Preserve the type of the copied entry for clarity.  The merged spot
     will not be used for another type of object in this colorant. */
  copiedtype = newlistchptr->type;
  oldlistchptr = ht_getlistch_exact(oldspotno, copiedtype, newci);

  if ( oldlistchptr == NULL ) {
    /* Newci is not in old spotno - add newci to it */
    plistch = duplicate_listchalftone( newlistchptr ) ;
    if ( plistch == NULL ) {
      multi_mutex_unlock(&ht_cache_mutex);
      (void)error_handler(VMERROR);
      return SPOT_NO_INVALID;
    }

    /* Override pertinent fields */
    theISpotNo(plistch) = oldspotno;
    UPDATE_LAST_USED(plistch, eraseno);

    ht_cacheInsert(plistch);

    /* Check if the old spot with the newly added entry for newci is now a
       duplicate of an existing spot. If it is a duplicate then use the spot
       already in existence and delete the one just created. */
    {
      SPOTNO eqlspotno;
      eqlspotno = ht_equivalentchspotid(oldspotno, 0 /* doesn't matter */);
      if ( eqlspotno >= 0 ) {
        /* Free only the newci spot for oldspotno */
        ht_cacheRemove(oldspotno, newtype, newci);
        /* ht_equivalentchspotid does lookups, so clear the cache. */
        invalidate_current_halftone(fetch_last_spot(NULL));

        /* Remember the spotno we can use for this combination */
        pClone->clonespotno = eqlspotno;
        ht_mark_spot_used(eqlspotno, eraseno);
        multi_mutex_unlock(&ht_cache_mutex);
        return eqlspotno;
      }
    }
    /* Remember the spotno we can use for this combination */
    pClone->clonespotno = oldspotno;
    multi_mutex_unlock(&ht_cache_mutex);
    return 0; /* Flag to continue using oldspotno - yuck */
  }

  /* Newci exists in old spotno - check if they are different */
  if ( (theIChPtr(oldlistchptr) != theIChPtr(newlistchptr)) ||
       (theIHalfPx(oldlistchptr) != theIHalfPx(newlistchptr)) ||
       (theIHalfPy(oldlistchptr) != theIHalfPy(newlistchptr)) ||
       (theIMhtRef(oldlistchptr) != theIMhtRef(newlistchptr)) ) {
    /* Need to build new spotno to be used - allocate a cloned spotno */
    clonespotno = --clonedspotid;

    /* Copy newci into oldspotno */
    listchptr = duplicate_listchalftone( newlistchptr ) ;
    if ( NULL == listchptr ) {
      multi_mutex_unlock(&ht_cache_mutex);
      (void)error_handler(VMERROR);
      return SPOT_NO_INVALID;
    }

    HQTRACE(debug_halftonecache,
            ("ht_mergespotnoentry: %d(%d, %u)", clonespotno, newci, copiedtype));

    /* Override some pertinent fields */
    theISpotNo(listchptr) = clonespotno;
    UPDATE_LAST_USED(listchptr, eraseno);

    /* Start chain of clonespotno colorants */
    listchptr->next = NULL;
    newlistchptr = listchptr;

    HQTRACE(debug_mergespotnoentry,
            ("Creating ht entry: %d = %d + %d(%d, %u))",
             clonespotno, oldspotno, newspotno, newci, copiedtype));

    for ( oldlistchptr = hctable[oldspotno&(HALFTONECACHESIZE - 1)];
          oldlistchptr != NULL;
          oldlistchptr = oldlistchptr->next ) {

      if ( (theISpotNo(oldlistchptr) == oldspotno) &&
           (theIHalfColor(oldlistchptr) != newci ||
            oldlistchptr->type != copiedtype) ) {
        /* Got old screen not newci, copiedtype: copy into clonespotno */
        listchptr = duplicate_listchalftone( oldlistchptr ) ;
        if ( NULL == listchptr ) {
          multi_mutex_unlock(&ht_cache_mutex);
          /* Undo what we have done so far*/
          while ( newlistchptr != NULL) {
            listchptr = newlistchptr;
            newlistchptr = newlistchptr->next;
            safe_free_listchalftone( listchptr, TRUE );
          }
          (void)error_handler(VMERROR);
          return SPOT_NO_INVALID;
        }

        HQTRACE(debug_halftonecache,
                ("ht_mergespotnoentry: %d %d", clonespotno, theIHalfColor(oldlistchptr)));

        /* Override pertinent fields */
        theISpotNo(listchptr) = clonespotno;
        UPDATE_LAST_USED(listchptr, eraseno);

        /* Link up colorant at front of list */
        listchptr->next = newlistchptr;
        newlistchptr = listchptr;
      }
    }

    /* Add list of spotno colorants to ht cache */
    while ( newlistchptr != NULL ) {
      plistch = newlistchptr->next;
      ht_cacheInsert(newlistchptr);
      newlistchptr = plistch;
    }

    /* Check if the new merged spot is in fact a duplicate of an existing
       spot. If it is a duplicate then use the spot already in existence
       and delete the one just created. */
    {
      SPOTNO eqlspotno;
      eqlspotno = ht_equivalentchspotid(clonespotno, newspotno);
      if ( eqlspotno >= 0 ) {
        Bool purged;
        /* Safe to purge, as iterators have been locked out since creation. */
        purged = purgehalftones(clonespotno, TRUE);
        clonespotno = eqlspotno;
        if ( purged )
          clonedspotid++; /* reclaim clone spot no (counts downwards) */
        ht_mark_spot_used(eqlspotno, eraseno);
      }
    }

  } else { /* Newci same in both spotno - might as well use old */

    HQTRACE(debug_mergespotnoentry,
            ("Existing ht entry: %d == %d(%d)", oldspotno, newspotno, newci));
    pClone->clonespotno = oldspotno;
    multi_mutex_unlock(&ht_cache_mutex);
    return 0; /* Flag to continue using oldspotno - yuck */
  }

  pClone->clonespotno = clonespotno;
  multi_mutex_unlock(&ht_cache_mutex);
  return clonespotno;
} /* Function ht_mergespotnoentry */



/*
 * Function:    ht_patchColorant
 *
 * Purpose:     To link together two colorants within a spot set
 *
 * Returns:     TRUE if linked up ok, else FALSE
 */
static Bool ht_patchColorant(
  COLORANTINDEX     ciLink,         /* I */
  NAMECACHE         *pncLink,       /* I */
  LISTCHALFTONE     *plistchOrig,   /* I */
  Bool              usedLink)
{
  LISTCHALFTONE*  plistchLink;

  HQASSERT(HT_CI_VALID(ciLink),
           "ht_patchColorant: invalid colorant index");
  HQASSERT((pncLink != NULL),
           "ht_patchColorant: NULL pointer to link colorant name");
  HQASSERT((plistchOrig != NULL),
           "ht_patchColorant: NULL pointer to original listch");

  /* Look for colorant to link in original spotno set */
  plistchLink = ht_getlistch_exact(plistchOrig->spotno, plistchOrig->type, ciLink);

  if ( plistchLink == NULL ) {
    plistchLink = duplicate_listchalftone( plistchOrig );
    if ( plistchLink == NULL )
      return error_handler(VMERROR);
    /* Override pertinent fields */
    theIHalfColor(plistchLink) = ciLink;
    plistchLink->sfcolor = pncLink;
    /* Add to ht cache */
    ht_cacheInsert(plistchLink);
    return TRUE;
  }

  /* Colorant to link up exists - update colorant screen info  */
  if ( theIChPtr(plistchLink) != theIChPtr(plistchOrig) ) {
    /* Screens are not the same - link them up! */
    if ( usedLink )
      /* Objs in DL have used the link screen (not recombining, so not
         intercepted); transfer those levels to the real screen. */
      ht_transferLevels(plistchLink->chptr, plistchOrig->chptr);
    addRefChPtr( theIChPtr(plistchOrig) ) ;
    releaseChPtr( theIChPtr(plistchLink) ) ;
    theIChPtr(plistchLink) = theIChPtr(plistchOrig) ;
  }
  /* Ditto for modular screen ref */
  if ( theIMhtRef( plistchLink ) != theIMhtRef( plistchOrig ) ) {
    if ( usedLink ) /* Objs in DL might have used the link screen */
      /* If it has been used, transfer that state. */
      if ( htm_is_used( plistchLink->mhtref, input_dl ) )
        if ( ! htm_set_used( plistchOrig->mhtref, input_dl ) )
          return FALSE;
    if ( theIMhtRef( plistchOrig ) )
      htm_AddRefHalftoneRef( theIMhtRef( plistchOrig ));
    if ( theIMhtRef( plistchLink ) )
      htm_ReleaseHalftoneRef( theIMhtRef( plistchLink ));
    theIMhtRef( plistchLink ) = theIMhtRef( plistchOrig ) ;
  }
  /* Copy over phase info */
  theIHalfCx(plistchLink) = theIHalfCx(plistchOrig);
  theIHalfCy(plistchLink) = theIHalfCy(plistchOrig);
  theIHalfPx(plistchLink) = theIHalfPx(plistchOrig);
  theIHalfPy(plistchLink) = theIHalfPy(plistchOrig);

  /* This should stop the linked up screen from disappearing */
  if ( !usedLink )
    UPDATE_LAST_USED(plistchLink, plistchOrig->last_used_dl);
  return TRUE;
} /* Function ht_patchColorant */


Bool ht_patchSpotnos(
  COLORANTINDEX     ciOriginal,     /* I */
  COLORANTINDEX     ciLink,         /* I */
  NAMECACHE         *pncLink,       /* I */
  Bool              usedLink)
{
  int32           i;
  Bool            fPatchOk;
  LISTCHALFTONE*  plistch;

  HQASSERT(HT_CI_VALID(ciOriginal),
           "ht_patchSpotnos: invalid colorant index");
  HQASSERT(HT_CI_VALID(ciLink),
           "ht_patchSpotnos: invalid colorant index");
  HQASSERT((ciOriginal != ciLink),
           "ht_patchSpotnos: original and link colorant indexes are the same");
  HQASSERT((pncLink != NULL),
           "ht_patchSpotnos: NULL link colorant name pointer");

  multi_mutex_lock(&ht_cache_mutex);

  ht_clone_reset();
  fPatchOk = TRUE;
  /* Loop over the whole ht cache */
  for ( i = 0; fPatchOk && (i < HALFTONECACHESIZE); i++ ) {
    for ( plistch = hctable[i]; fPatchOk && (plistch != NULL);
          plistch = plistch->next ) {
      if ( plistch->ci == ciOriginal && plistch->last_used_dl >= input_dl )
        /* Found a listch using the screen - update actual screen to use this */
        fPatchOk = ht_patchColorant(ciLink, pncLink, plistch, usedLink);
    }
  }
  multi_mutex_unlock(&ht_cache_mutex);
  return fPatchOk;
} /* Function ht_patchSpotnos */


/*
 * Function:    ht_checkequivalentchspotid
 *
 * Purpose: Check if the existing spotid screen set is an equivalent
 *   subset of the new spotid, or the specific colorant is equivalent.
 *
 * Arguments:   newspotid     - spotno for new screen to check equivalence of
 *              guessspotid   - spotno of existing screen to check against
 *              fAny - check all colorants and types in existing spotno
 *              type          - specific type to check in existing spotno
 *              ci            - specific colorant to check in existing spotno
 *
 * Returns:     TRUE, if it is a subset of the new spot id or the
 *              specific colorant is the same
 */
static Bool ht_checkequivalentchspotid(
  SPOTNO        newspotid,
  SPOTNO        guessspotid,
  Bool          fAny,
  HTTYPE        type,
  COLORANTINDEX ci)
{
  LISTCHALFTONE *newlistchptr;
  LISTCHALFTONE *oldlistchptr;

  HQASSERT(newspotid > 0, "invalid new spot id");
  HQASSERT(guessspotid > 0, "invalid guessed spot id");
  HQASSERT((newspotid != guessspotid),
           "ht_checkequivalentchspotid: equal spot ids - doh!");
  HQASSERT(HTTYPE_VALID(type), "ht_checkequivalentchspotid: invalid type");
  HQASSERT((fAny || HT_CI_VALID(ci)),
           "ht_checkequivalentchspotid: invalid specific colorant index");

  /* Loop over all screens for existing spotid */
  for ( oldlistchptr = hctable[guessspotid&(HALFTONECACHESIZE-1)];
        oldlistchptr != NULL;
        oldlistchptr = oldlistchptr->next ) {

    if ( (guessspotid == theISpotNo(oldlistchptr)) &&
         (fAny || (ci == oldlistchptr->ci && type == oldlistchptr->type)) ) {
      /* Look up matching colorant in new spotid screens */
      newlistchptr =
        ht_getlistch_exact(newspotid, oldlistchptr->type, oldlistchptr->ci);
      if ( newlistchptr == NULL )
        /* New spotid screen does not exist - spotids not equivalent */
        return FALSE;
      if ( (theIChPtr(newlistchptr) != theIChPtr(oldlistchptr)) ||
           (theIMhtRef(newlistchptr) != theIMhtRef(oldlistchptr)) ||
           (theIHalfPx(newlistchptr) != theIHalfPx(oldlistchptr)) ||
           (theIHalfPy(newlistchptr) != theIHalfPy(oldlistchptr)) )
        /* Failed colorant detail match - spotids not equivalent */
        return FALSE;
    }
  }
  return TRUE;
} /* Function ht_checkequivalentchspotid */


/*
 * Function:    ht_checkequivalentchspotids
 *
 * Purpose:     To see if for one or all colorant screens are the same for
 *              two given spotnos.
 *
 * Arguments:   newspotid     - spotid for a new screen set
 *              guessspotid   - spotid for existing screen set
 *              fAny          - match all colorants and types
 *              ci            - specific colorant to match if fAny is FALSE
 *
 * Returns: The second spotno if screen(s) are equivalent, else SPOT_NO_INVALID
 *
 * Notes:       The two-way check is to ensure both spots have the same set of
 * colorant screens, i.e., one is not the proper subset of the other.
 */
SPOTNO ht_checkequivalentchspotids(
  SPOTNO        newspotid,
  SPOTNO        guessspotid,
  Bool          fAny,
  HTTYPE        type,
  COLORANTINDEX ci )
{
  if ( !ht_checkequivalentchspotid(newspotid, guessspotid,
                                   fAny, type, ci) )
    return SPOT_NO_INVALID;
  if ( fAny && !ht_checkequivalentchspotid(guessspotid, newspotid,
                                           fAny, type, ci) )
    return SPOT_NO_INVALID;
  return guessspotid;
}


/*
 * Function:    ht_equivalentchspotid
 *
 * Purpose:     To look for an exact matching screen set to a new one, starting
 *              with a given one.
 *
 * Arguments:   newspotid     - spotid of new screen set
 *              guessspotid   - spotid of previous screen set
 *
 * Returns:     Spot id for exact matching screen set, else SPOT_NO_INVALID
 */
SPOTNO ht_equivalentchspotid(
  SPOTNO newspotid,
  SPOTNO guessspotid)
{
  int32 i;
  SPOTNO existingspotid;

  HQASSERT(newspotid > 0, "invalid spot number");

  multi_mutex_lock(&ht_cache_mutex);

  if ( guessspotid > 0 ) { /* allow for no previous set */
    /* First of all, check for new and existing screen sets matching */
    existingspotid = ht_checkequivalentchspotids(newspotid, guessspotid, TRUE,
                                                 HTTYPE_DEFAULT,
                                                 COLORANTINDEX_UNKNOWN);
    if ( existingspotid >= 0 ) {
      /* Found exact matching screen set */
      HQTRACE(debug_equivalentchspotid,
              ("ht_checkequivalentchspotids(guess): (%d)", existingspotid));
      multi_mutex_unlock(&ht_cache_mutex);
      return existingspotid;
    }
  }

  /* Loop over any other screens in cache looking for exact set match */
  for ( i = 0; i < HALFTONECACHESIZE; ++i ) {
    LISTCHALFTONE* listchptr;

    for ( listchptr = hctable[i];
          listchptr != NULL;
          listchptr = listchptr->next ) {
      SPOTNO existingspotid = theISpotNo(listchptr);

      if ( (existingspotid != newspotid) &&
           (existingspotid != guessspotid) &&
           (theIHalfColor(listchptr) == COLORANTINDEX_NONE) ) {
        /* Use default screen to check each spotno only once. */

        /* Check for new and other existing screen sets matching */
        existingspotid = ht_checkequivalentchspotids(newspotid, existingspotid,
                                                     TRUE, HTTYPE_DEFAULT,
                                                     COLORANTINDEX_NONE);
        if ( existingspotid >= 0 ) {
          /* Found exact matching screen set */
          HQTRACE(debug_equivalentchspotid,
                  ("ht_checkequivalentchspotids(found): (%d)", existingspotid));
          multi_mutex_unlock(&ht_cache_mutex);
          return existingspotid;
        }
      }
    }
  }
  multi_mutex_unlock(&ht_cache_mutex);
  /* No matching screen set found */
  return SPOT_NO_INVALID;
} /* Function ht_equivalentchspotid */


LISTCHALFTONE* ht_getlistch_exact(
  SPOTNO          spotno,
  HTTYPE          type,
  COLORANTINDEX   ci)
{
  LISTCHALFTONE*  plistch;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "ht_getlistch_exact: invalid type");
  HQASSERT(HT_CI_VALID(ci),
           "ht_getlistch_exact: invalid colorant index");

  /* Find a screen for the spotno */
  plistch = ht_getlistch(spotno, type, ci, NULL);

  /* Only return screen if the one requested -- not default screen */
  return (plistch != NULL && plistch->ci == ci && plistch->type == type)
         ? plistch : NULL;
}


/* Finds the cached halftone given by spotno, type and colorant, and unpacks the
   params which are used during rendering. */
void render_gethalftone( ht_params_t* ht_params,
  SPOTNO spotno,
  HTTYPE type,
  COLORANTINDEX ci,
  corecontext_t *context)
{
  LISTCHALFTONE*  plistch;
  CHALFTONE*      pch;
  static FORM*    two_nulls[2] = {NULL, NULL};

  HQASSERT(spotno > 0, "invalid spotno");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(HT_CI_VALID(ci), "invalid colorant index");

  if ( ht_params->spotno == spotno && ht_params->objtype == type
       && ht_params->ci == ci )
    return;

  plistch = ht_getlistch(spotno, type, ci, context);
  ht_params->spotno = spotno; ht_params->objtype = type; ht_params->ci = ci;

  if ( plistch == ht_params->listchptr )
    return;

  if ( plistch != NULL ) {
    /* Got a screen for the spotno and colorant (or NONE) */
    pch = theIChPtr(plistch);
    if ( pch == NULL ) { /* modular, will not be used */
      /** \todo Render loop should not allow this. */
      ht_params->cachedforms = two_nulls; /* so it asserts in getnearest */
      ht_params->listchptr = plistch;
      ht_params->type = NHALFTONETYPES; /* so it asserts */
    } else if ( ht_chIsUsedForRendering(pch) ) {
      /* Non-degenerate screen used - extract bits */
      ht_params->listchptr = plistch;
      rawstorechglobals(ht_params, pch);
      /* get current halfxy and halfcy values */
      ht_params->cx = theIHalfCx(plistch); ht_params->cy = theIHalfCy(plistch);
      ht_params->px = theIHalfPx(plistch); ht_params->py = theIHalfPy(plistch);
      /* We found screen for given colorant - all done */
    } else { /* found but not really used */
      /* This may happen for non-rendering objects (e.g., HDLs) or
         multi-coloured ones that only paint solid and clear. */
      ht_params->cachedforms = two_nulls;
      ht_params->listchptr = NULL;
      ht_params->lockid = NULL;
      ht_params->type = GENERAL; /* not really used, but asserted */
    }
  } else { /* No screens for spotno */
    /* May happen as for previous clause. */
    ht_params->cachedforms = two_nulls;
    ht_params->listchptr = NULL;
    ht_params->lockid = NULL;
    ht_params->type = GENERAL; /* not really used, but asserted */
  }
} /* Function render_gethalftone */


/** Checks to see if a certain halftone exists in the cache.
 *
 * Looks either for a given ci & type, or all screens of a spotno (ci =
 * COLORANTINDEX_UNKNOWN).  In any case, the phase must match. */
Bool ht_checkifchentry(
  SPOTNO        spotno,
  HTTYPE        type,
  COLORANTINDEX ci,
  int32         phasex,
  int32         phasey)
{
  Bool found = FALSE;
  LISTCHALFTONE *listchptr ;

  HQASSERT(spotno > 0, "invalid spotno");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(ci == COLORANTINDEX_UNKNOWN || HT_CI_VALID(ci),
           "invalid colorant index");
  for ( listchptr = hctable[spotno&(HALFTONECACHESIZE-1)];
        listchptr != NULL;
        listchptr = listchptr->next ) {

    if ( theISpotNo(listchptr) == spotno ) {
      if ( ci == COLORANTINDEX_UNKNOWN
           || (ci == listchptr->ci && type == listchptr->type) ) {
        int32 lphasex = phasex;
        int32 lphasey = phasey;
        int32 px, py;

        ht_converge_phase(listchptr->chptr, &lphasex, &lphasey, &px, &py);
        if ( theIHalfPx(listchptr) == px && theIHalfPy(listchptr) == py ) {
          if ( ci != COLORANTINDEX_UNKNOWN ) /* must be an exact match */
            return TRUE;
          found = TRUE;
        } else
          return FALSE;
      }
    }
  }
  return found;
} /* Function ht_checkifchentry */


/* Return the modular halftone instance reference for a given
 * spotno and colorant index.
 * Returns NULL if the entry isn't found or isn't a modular one.
 */
MODHTONE_REF *ht_getModularHalftoneRef(
  SPOTNO spotno,
  HTTYPE type,
  COLORANTINDEX ci )
{
  LISTCHALFTONE *listchptr = ht_getlistch(spotno, type, ci, NULL);

  if (listchptr != NULL)
    return theIMhtRef(listchptr);
  else
    return NULL;
} /* Function ht_getModularHalftoneRef */


void ht_setCalibrationWarning(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci,
                              Bool b)
{
  LISTCHALFTONE *listchptr = ht_getlistch(spotno, type, ci, NULL);

  HQASSERT(listchptr != NULL, "Invalid screen in ht_setCalibrationWarning");
  listchptr->calibration_warning = (int8)b;
}


Bool ht_getCalibrationWarning(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci)
{
  LISTCHALFTONE *listchptr = ht_getlistch(spotno, type, ci, NULL);

  HQASSERT(listchptr != NULL, "Invalid screen in ht_getCalibrationWarning");
  return listchptr->calibration_warning;
}


Bool ht_modularHalftoneUsedForColorant(const MODHTONE_REF *mht,
                                       dl_erase_nr erasenr,
                                       COLORANTINDEX ci)
{
  register size_t i ;
  register LISTCHALFTONE *listchptr ;
  uint8 type;
  Bool lastNoneUsesIt[HTTYPE_DEFAULT+1]; /* Naughty: knows type order */
  SPOTNO lastSpot;

  UNUSED_PARAM(dl_erase_nr, erasenr); /** \todo Implement RSN */

  for ( i = 0; i < HALFTONECACHESIZE; ++i ) {
    listchptr = hctable[i]; lastSpot = SPOT_NO_INVALID;
    for ( type = 0 ; type <= HTTYPE_DEFAULT ; ++type )
      lastNoneUsesIt[type] = FALSE;
    while ( listchptr != NULL ) {
      if ( listchptr->spotno != lastSpot ) { /* new spot */
        for ( type = 0 ; type <= HTTYPE_DEFAULT ; ++type ) {
          if ( lastNoneUsesIt[type] ) /* There wasn't an entry for ci in */
            return TRUE;              /* the last spot, but None used mht. */
          lastNoneUsesIt[type] = FALSE;
        }
        lastSpot = listchptr->spotno;
      }
      if ( listchptr->ci == ci ) {
        if ( listchptr->mhtref == mht )
          return TRUE;
        else /* the colorant had an entry, ignore None */
          lastNoneUsesIt[listchptr->type] = FALSE;
      } else if ( listchptr->ci == COLORANTINDEX_NONE )
        if ( listchptr->mhtref == mht )
          lastNoneUsesIt[listchptr->type] = TRUE;
      listchptr = listchptr->next;
    }
    for ( type = 0 ; type <= HTTYPE_DEFAULT ; ++type )
      if ( lastNoneUsesIt[type] ) /* As above, consider None in the last spot */
        return TRUE;
  }
  return FALSE ;
}


/* Returns TRUE if the halftone cache entry for this spot and color is a
 * spot-function (frequency & angle) screen. FALSE otherwise.
 * Should not be called if there is no such entry (checkifchentry should
 * be called first to make sure).
 */
Bool ht_isSpotFuncScreen(
  SPOTNO        spotno,
  HTTYPE        type,
  COLORANTINDEX ci,
  int32         phasex,
  int32         phasey)
{
  LISTCHALFTONE *listchptr;
  int32 lphasex = phasex;
  int32 lphasey = phasey;
  int32 px, py;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "Must check valid type");
  HQASSERT(COLORANTINDEX_VALID(ci), "Must check a valid colorant");
  HQASSERT(ht_checkifchentry(spotno, type, ci, phasex, phasey),
            "ht_isSpotFuncScreen called when no chentry exists at all") ;

  listchptr = ht_getlistch(spotno, type, ci, NULL);
  if ( listchptr->mhtref || ht_chIsThreshold(listchptr->chptr))
    return FALSE;
  ht_converge_phase(listchptr->chptr, &lphasex, &lphasey, &px, &py);
  return listchptr->phasex == px && listchptr->phasey == py;
} /* Function ht_isSpotFuncScreen */



Bool ht_is_object_based_screen(SPOTNO spotno)
{
  LISTCHALFTONE *listchptr;
  Bool res = FALSE;

  HQASSERT(spotno > 0, "invalid spot number");

  /* If we have never seen an object based screen, then this one cannot be one.
     Crude performance optimisation for workflows where there is absolutely
     no object based screening ever. */
  if ( !seen_obj_based_screen )
    return FALSE;

  multi_mutex_lock(&ht_cache_mutex);
  for ( listchptr = hctable[spotno&(HALFTONECACHESIZE-1)];
        listchptr != NULL;
        listchptr = listchptr->next )
    if ( listchptr->spotno == spotno )
      if ( listchptr->type != HTTYPE_DEFAULT ) {
        res = TRUE; break;
      }
  multi_mutex_unlock(&ht_cache_mutex);
  return res;
}


/** ht_duplicatechentry()
 *
 *  This function creates a new LISTCHALFTONE entry, largely as a copy of an
 *  existing one (identified by the 'oldspotno' & 'oldci' parameters), with
 *  both the new and the old one pointing to the same CHALFTONE structure
 *  (thus sharing common data).
*/
Bool ht_duplicatechentry(
  SPOTNO          newspotno,
  HTTYPE          newtype,
  COLORANTINDEX   newci,
  NAMECACHE*      newcolorname,
  NAMECACHE*      newhtname,
  SPOTNO          oldspotno,
  HTTYPE          oldtype,
  COLORANTINDEX   oldci,
  int32           phasex,
  int32           phasey)
{
  register CHALFTONE *oldchptr ;
  register LISTCHALFTONE *oldlistchptr ;
  register LISTCHALFTONE *newlistchptr ;

  HQASSERT(newcolorname != NULL,
           "ht_duplicatechentry: no color name given");

  for ( oldlistchptr = hctable[oldspotno&(HALFTONECACHESIZE-1)] ;
        oldlistchptr != NULL ;
        oldlistchptr = oldlistchptr->next ) {

    if ( theISpotNo(oldlistchptr) == oldspotno
         && oldlistchptr->type == oldtype
         && theIHalfColor(oldlistchptr) == oldci ) {

      oldchptr = theIChPtr( oldlistchptr);

      /* Now that found equivalent halftone, insert duplicate. */
      newlistchptr = duplicate_listchalftone( oldlistchptr ) ;
      if ( NULL == newlistchptr ) {
        return error_handler(VMERROR);
      }

      /* Fill in replacement details. */
      theISpotNo(newlistchptr) = newspotno;
      newlistchptr->type = newtype;
      theIHalfColor(newlistchptr) = newci;
      newlistchptr->htname = newhtname;
      newlistchptr->sfcolor = newcolorname;
      newlistchptr->last_used_dl = MAX_DL_ERASE_NR;
      newlistchptr->calibration_warning = FALSE;

      ht_converge_phase(oldchptr, &phasex, &phasey,
                        &newlistchptr->phasex, &newlistchptr->phasey);
      newlistchptr->halfcx = phasex; newlistchptr->halfcy = phasey;

      /* Put into table. */
      ht_cacheInsert(newlistchptr);

      return TRUE ;
    }
  }

  /* Not found usually means that degenerate spot function of zero size:
     - I don't believe it. In any case if this so, the routine shouldn't be
     called so it is a valid assert failure. We shouldn't just ignore
     failure to duplicate - it is a source of errors which leads to
     degenerate screens being used to print because a screen is missing
     which should be there */
  HQFAIL("ht_duplicatechentry didn't find screen to duplicate");
  return TRUE ;
} /* Function ht_duplicatechentry */


/** Remove an entry from the cache.

  This should be called holding \c ht_cache_mutex. The caller is
  responsible for any cache invalidation.
 */
void ht_cacheRemove(
  SPOTNO        spotno,
  HTTYPE        type,
  COLORANTINDEX ci)
{
  LISTCHALFTONE*  listchptr;
  LISTCHALFTONE*  pchptr;

  pchptr = NULL;
  for ( listchptr = hctable[spotno&(HALFTONECACHESIZE-1)];
        listchptr != NULL;
        listchptr = listchptr->next ) {

    if ( theISpotNo(listchptr) == spotno && listchptr->type == type
         && theIHalfColor(listchptr) == ci ) {
      /* Found it, so free if we have not just done a partial paint,
       * or if we have just done a partial paint, but this screen
       * was not used in it.
       */
      HQTRACE(debug_halftonecache, ("freechentry: %d %d", spotno, ci));
      if ( pchptr != NULL )
        pchptr->next = listchptr->next;
      else
        hctable[spotno&(HALFTONECACHESIZE-1)] = listchptr->next;
      safe_free_listchalftone( listchptr, TRUE );
      return;
    }
    pchptr = listchptr;
  }
}


/*
 * Function:    ht_getlistch
 *
 * Purpose:     To get list ch for given spotno, type and colorant index.
 *              As a side effect, maintains the listch entries for reuse in the
 *              current LAST_SPOT_CACHE.
 *
 * Arguments:   nSpotId     - spot number
 *              type        - object type
 *              ci          - colorant index
 *
 * Returns: Pointer to listch struct for either the colorant or the
 *          NONE separation for the spot no, or NULL if spotno not in ht
 *          table (i.e., not even a NONE screen!)
 *
 * Notes: Colorants are in increasing index order, e.g. NONE(-2), 0, 1,
 * 3, 6...  Types are in increasing order within each colorant. Cached
 * lookup doesn't need to lock, because interleaving of inserts and
 * lookup doesn't matter, and there shouldn't be any references to
 * purgeable spots so screens about to be removed can't be looked
 * up. Also, invalidation will just cause a lookup in the actual table,
 * and if done by another thread, will be synchronized.
 */
LISTCHALFTONE* ht_getlistch(SPOTNO nSpotId, HTTYPE type, COLORANTINDEX ci,
                            corecontext_t *context)
{
  LISTCHALFTONE *plistch = NULL;
  LAST_SPOT_CACHE *last_spot;

  HQASSERT(nSpotId > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "ht_getlistch: invalid type");
  HQASSERT(HT_CI_VALID(ci),
           "ht_getlistch: invalid colorant index");

  ci = (ci != COLORANTINDEX_ALL) ? ci : COLORANTINDEX_NONE;

  last_spot = fetch_last_spot(context);

  if ( nSpotId == last_spot->nSpotIdLast ) {
    /* Same spot id: see if value is cached for this colorant */
    if ( ci <= HT_MAX_COLORANT_CACHE )
      plistch = last_spot->listch[type][ci];
  } else {
    /* New spot id - invalidate cached info */
    invalidate_current_halftone(last_spot);
  }

  if ( plistch == NULL ) { /* Not cached, search */
    multi_mutex_lock(&ht_cache_mutex);
    /* Have to set nSpotIdLast here, to protect against invalidate_all. */
    last_spot->nSpotIdLast = nSpotId;

    if (last_spot->listch[0][COLORANTINDEX_NONE] != NULL)
      /* Got the first screen: start looking from there */
      plistch = last_spot->listch[0][COLORANTINDEX_NONE];
    else {
      /* Didn't find a 0/None screen before: locate first screen */
      plistch = hctable[nSpotId&(HALFTONECACHESIZE-1)];

      /* Find first for spotno in chain */
      while ( plistch != NULL && plistch->spotno != nSpotId )
        plistch = plistch->next;
    }
    /* This is not the optimal place to start the search, but it is the
       most efficient place to start to just cache everything. */

    /* Now look for colorant/type in list */
    while ( plistch != NULL ) {
      COLORANTINDEX listchCi = plistch->ci;

      HQASSERT(plistch->spotno == nSpotId, "Got lost in the cache");
      if ( listchCi <= HT_MAX_COLORANT_CACHE ) {
        /* Saw a cacheable colorant: update cache */
        if ( last_spot->listch[plistch->type][listchCi] == NULL)
          last_spot->listch[plistch->type][listchCi] = plistch;
        else
          HQASSERT(last_spot->listch[plistch->type][listchCi] == plistch,
                   "Inconsistent listch entries");
      }

      if (plistch->spotno == nSpotId && plistch->type == type && listchCi == ci)
        /* Found entry for spot, colorant, and type we're looking for */
        break;

      plistch = plistch->next;

      /* Don't walk beyond the requested spotno because the list is sorted. [More
       * complex tests on ci & type aren't needed because of caching.] */
      if ( plistch != NULL && plistch->spotno > nSpotId )
        plistch = NULL;
    }

    /* try defaults, in order type/NONE, DEFAULT/ci, DEFAULT/NONE */
    if ( plistch == NULL ) {
      plistch = last_spot->listch[type][COLORANTINDEX_NONE];
      if ( type != HTTYPE_DEFAULT
           /* At this point, plistch could be a DEFAULT/NONE, but that's no
              good, because DEFAULT/ci should take precedence. */
           && (plistch == NULL || plistch->type == HTTYPE_DEFAULT) )
        plistch = ht_getlistch(nSpotId, HTTYPE_DEFAULT, ci, context);

      /* Whatever we find, cache it. */
      if ( plistch != NULL && ci <= HT_MAX_COLORANT_CACHE )
        last_spot->listch[type][ci] = plistch;
    }
    multi_mutex_unlock(&ht_cache_mutex);
  }

  HQASSERT((plistch == NULL) ||
           ((plistch->spotno == nSpotId) &&
            (plistch->type == type || plistch->type == HTTYPE_DEFAULT) &&
            (plistch->ci == COLORANTINDEX_NONE || plistch->ci == ci)),
           "ht_getlistch: returning invalid ht listch pointer");
  return plistch;
} /* Function ht_getlistch */


#ifdef DEBUG_BUILD
void ht_print_screen(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci)
{
  LISTCHALFTONE* plistch;
  plistch = ht_getlistch(spotno, type, ci, NULL);
  if (plistch == NULL) {
    monitorf((uint8*)"ci:%d type:%u ht:NULL", ci, type);
  } else if (plistch->chptr == NULL) {
    monitorf((uint8*)"ci:%d type:%u ht:modular", ci, type);
  } else {
    monitorf((uint8*)"ci:%d type:%u sfcolor:%.*s angle:%f",
             ci, type,
             plistch->sfcolor->len, plistch->sfcolor->clist,
             ht_chAngle(plistch->chptr));
  }
}
#endif /* DEBUG_BUILD */


/*
 * Function:    ht_getch
 *
 * Purpose:     To get ch struct for given spotno and colorant index
 *
 * Arguments:   nSpotId     - spot number
 *              type        - object type
 *              ci          - colorant index
 *
 * Returns: Pointer to ch struct for type and colorant, or appropriate
 *          default, or NULL if no match.
 *
 * Notes:
 */
CHALFTONE* ht_getch(
  SPOTNO nSpotId,
  HTTYPE type,
  COLORANTINDEX ci)
{
  LISTCHALFTONE*  plistch;
  CHALFTONE*      pch;

  HQASSERT(nSpotId > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "ht_getch: invalid type");
  HQASSERT(HT_CI_VALID(ci),
           "ht_getch: invalid colorant index");

  plistch = ht_getlistch(nSpotId, type, ci, NULL);
  if ( plistch != NULL )
    /* Found list ch - return ch struct */
    pch = plistch->chptr;
  else
    pch = NULL;

  return pch;
} /* Function ht_getch */


/** Context specialiser for the HT cache context. */
static void ht_cache_specialise(corecontext_t *context,
                                context_specialise_private *data)
{
  ht_context_t ht_context = { 0 } ;

  /* Link into global list. */
  ht_context.next = interpreter_ht_context.next ;
  interpreter_ht_context.next = &ht_context ;

  NAME_OBJECT(&ht_context.last_spot_cache, LAST_SPOT_CACHE_NAME) ;
  invalidate_current_halftone(&ht_context.last_spot_cache) ;

  context->ht_context = &ht_context;
  context_specialise_next(context, data);

  UNNAME_OBJECT(&ht_context.last_spot_cache) ;
}


/** Structure for registering the HT context specialiser. */
static context_specialiser_t ht_context_specialiser = {
  ht_cache_specialise, NULL
};

Bool initHalfToneCache( void )
{
  HQASSERT( ( 2 * HALFTONECACHESIZE - 1 ) ==
            ( HALFTONECACHESIZE ^ ( HALFTONECACHESIZE - 1 )) ,
            "HALFTONECACHESIZE is not a power of 2" ) ;

  hctable = (LISTCHALFTONE **)
    mm_alloc_static(sizeof(LISTCHALFTONE *) * HALFTONECACHESIZE);
  if ( hctable == NULL )
    return FAILURE(FALSE) ;
  HqMemSetPtr(hctable, NULL, HALFTONECACHESIZE) ;

  if ( !low_mem_handler_register(&ht_cache_handler) )
    return FAILURE(FALSE);

  if ( mps_root_create( &hctable_root, mm_arena, mps_rank_exact(),
                        0, hctable_scan, NULL, 0 ) != MPS_RES_OK ) {
    low_mem_handler_deregister(&ht_cache_handler);
    return FAILURE(FALSE) ;
  }

  interpreter_ht_context.next = NULL ;
  NAME_OBJECT(&interpreter_ht_context.last_spot_cache, LAST_SPOT_CACHE_NAME) ;
  CoreContext.ht_context = &interpreter_ht_context ;
  invalidate_current_halftone(&interpreter_ht_context.last_spot_cache);

  context_specialise_add(&ht_context_specialiser);

  multi_mutex_init(&ht_cache_mutex, HT_CACHE_LOCK_INDEX, TRUE,
                   SW_TRACE_HT_CACHE_ACQUIRE, SW_TRACE_HT_CACHE_HOLD);
  return TRUE ;
}


/* ---------------------------------------------------------------------- */
void finishHalfToneCache( void )
{
  multi_mutex_finish(&ht_cache_mutex);
  interpreter_ht_context.next = NULL ; /* Disconnect global list */
  UNNAME_OBJECT(&interpreter_ht_context.last_spot_cache) ;
  mps_root_destroy( hctable_root );
  low_mem_handler_deregister(&ht_cache_handler);
}


void init_C_globals_htcache(void)
{
  clonedspotid = RESET_CLONED_SPOTID;
  page_default_spot = SPOT_NO_INVALID;
  hctable = NULL ;
  hctable_root = NULL ;
  seen_obj_based_screen = FALSE;
  HqMemZero(grgClone, sizeof(grgClone)) ;

  interpreter_ht_context.next = NULL ;
  ht_context_specialiser.next = NULL ;

#if defined( ASSERT_BUILD )
  debug_halftonecache = FALSE ;
  debug_equivalentchspotid = FALSE ;
  debug_mergespotnoentry = FALSE ;
#endif

  ht_clone_init() ;
}


/*
Log stripped */
