/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imblist.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image blist implementation
 */

#include "core.h"

#include "hqmemset.h"
#include "mm.h"                 /* mm_alloc */
#include "mmcompat.h"           /* mm_alloc_static */
#include "monitor.h"            /* monitorf */
#include "often.h"              /* SwOftenUnsafe */
#include "render.h"             /* inputpage */
#include "display.h"
#include "ripmulti.h"           /* NUM_THREADS */
#include "swerrors.h"           /* error_handler */

#include "imblock.h"            /* im_blocksizeof */
#include "imstore_priv.h"       /* IM_PLANE */

#include "imblist.h"


/**
 * Definition of an image block, private to this module
 *
 * \todo BMJ 28-Aug-07 : Work in Progress
 * IM_BLIST structure was shared amongst all of the modules, and was
 * modifed in various ways by various bits of code.
 * Taken the definition private, and added a somewhat random API on top.
 * Current API evolved from original usage, and needs to be cleaned-up into
 * a much simpler interface.
 */
struct IM_BLIST {
  int32           bx;
  uint8           global;
  uint8           wasGlobal;
  int16           abytes;
  uint8          *data;
  IM_BLOCK       *block;
  IM_BLIST       *next;
  IM_BLIST       *prev;
};


/** Descriptor of a band and the widest image on it. */
typedef struct {
  bandnum_t bandno;
  size_t width;
} blist_wide_band;


/**
 * Structure defining context information stored about blists.
 */
typedef struct BLIST_CTX {
  size_t im_blistc; /**< Total number of blists on global list. */
  size_t nthreads; /**< Number of image rendering threads. */
  /** Collection of the bands with the widest images on them. */
  blist_wide_band wide_bands[1 /* really nthreads */];
} BLIST_CTX;


/**
 * Start the image blist module for this page.
 */
Bool im_blist_start(IM_SHARED *im_shared)
{
  BLIST_CTX *ctx;
  size_t i, nthreads = NUM_THREADS();

  HQASSERT(im_shared->blist_ctx == NULL, "blist_ctx already exists");

  ctx = dl_alloc(im_shared->page->dlpools,
                 sizeof(BLIST_CTX) + sizeof(blist_wide_band) * (nthreads-1),
                 MM_ALLOC_CLASS_IMAGE);
  if ( ctx == NULL )
    return error_handler(VMERROR);

  ctx->im_blistc = 0;
  ctx->nthreads = nthreads;
  for ( i = 0 ; i < nthreads ; ++i ) {
    ctx->wide_bands[i].width = 0;
    ctx->wide_bands[i].bandno = 0;
  }

  im_shared->blist_ctx = ctx;
  return TRUE;
}


void im_blist_finish(IM_SHARED *im_shared)
{
  /* The dl pool is about to be dropped, so only need to null the ptr. */
  im_shared->blist_ctx = NULL;
}


/**
 * Create a new blist
 *
 * There are three different ways we may want to create a blist
 *   a) block != NULL && abytes == 0
 *      This means create a blist and copy its characteristics from the
 *      specified block
 *   b) block == NULL && abytes != 0
 *      This means create a blist and all zero all its characteristics
 *      except for setting abytes to the given value
 *   c) block == NULL && abytes == 0
 *      This means create the blist and the block in one chunk of memory
 *      and zero all the blist and block characteristics.
 *
 * \param[in]     page        DL on which the image blist is being used.
 * \param[in]     bx          Column index
 * \param[in]     global      Is it a global or local blist ?
 * \param[in,out] block       Image block template
 * \param[in]     abytes      block size
 * \param[in]     cost        MM cost
 * \return                    The created blist, or NULL on error
 */
IM_BLIST *blist_create(IM_SHARED *im_shared, int32 bx, Bool global,
                       IM_BLOCK *block, int32 abytes, mm_cost_t cost)
{
  IM_BLIST *blist;
  size_t size = sizeof(IM_BLIST);

  HQASSERT(block == NULL || abytes == 0,"blist creation specified twice");

  if ( abytes || block ) {
    if ( (blist = dl_alloc(im_shared->page->dlpools, size,
                           MM_ALLOC_CLASS_IMAGE_DATA)) == NULL )
      return NULL;
  } else {
    size += im_blocksizeof();
    if ( (blist = im_dataalloc(im_shared, size, cost)) == NULL )
      return NULL;
    block = (IM_BLOCK *)(blist + 1);
    HqMemZero(block, im_blocksizeof());
  }

  blist->bx         = bx;
  blist->global     = (uint8)global;
  blist->wasGlobal  = (uint8)global;
  blist->next       = NULL;
  blist->prev       = NULL;
  blist->block      = block;
  if ( block )
    im_block2blist(blist, TRUE);
  else {
    blist->wasGlobal = (uint8)(!global); /* Not sure why this is done */
    blist->abytes    = CAST_SIGNED_TO_INT16(abytes);
    blist->data      = NULL;
  }
  return blist;
}

/**
 * Destroy the given blist. May also want to free the block data following it.
 * \param[in]     blist           The blist to be destroyed
 * \param[in]     followingBlock  Is there a block to be freed as well
 */
void blist_destroy(IM_SHARED *im_shared, IM_BLIST *blist, Bool followingBlock)
{
  if ( followingBlock )
    im_datafree(im_shared, blist, sizeof(IM_BLIST) + im_blocksizeof());
  else
    dl_free(im_shared->page->dlpools, (mm_addr_t)blist, sizeof(IM_BLIST),
            MM_ALLOC_CLASS_IMAGE_DATA);
}


static Bool blist_usable(IM_BLIST *blist)
{
  return blist->block == NULL || im_blockisusable(blist->block);
}


/**
 * Insert a blist into the global blist list for normal images.
 * The list of blists hangs off a store node for the appropriate value
 * of abytes, this store node should already exist because it should have
 * been created when the store was created prior to the blists.
 * A blist may be added to either the head or the tail of the list. This is
 * important in rendering when blists are borrowed by a store for one band at
 * a time and then handed back to the global list. If blists are put on the
 * head when they are completely finished with they are the first candidate
 * to be used by another store. If they are put on the tail when there are
 * more bands to process for an image then the blist shouldn't end up
 * getting reused if the number of global blists has been calculated
 * correctly => the blist data is still there and available when the blist
 * is reclaimed on the next band. Failure to do this will mean data has to
 * be reloaded from the compressed form or from disk for each band => a
 * large performance drop in rendering.
 *
 * \param[in]     blist           The blist to be be linked in
 * \param[in]     linkToHead      Link to head or tail of global list?
 */
void blist_linkGlobal(IM_SHARED *im_shared, IM_BLIST *blist, Bool linkToHead)
{
  IM_STORE_NODE *node;
  BLIST_CTX *ctx = im_shared->blist_ctx;

  HQASSERT(blist != NULL, "blist is NULL");
  HQASSERT(blist->next == NULL && blist->prev == NULL,
           "blist already in a list");
  HQASSERT(blist_usable(blist), "Moving a block in use to global");
  blist_checkGlobal(im_shared);

  ctx->im_blistc++;
  blist->global    = TRUE;
  blist->wasGlobal = TRUE;

  /* Find the store node for size abytes */
  node = im_shared->im_list;
  while (node != NULL && node->abytes != blist->abytes)
    node = node->next;
  HQASSERT(node != NULL && node->abytes == blist->abytes,
           "Should have found node for this blist");

  /* Insert new blist at head or tail of list for this node */
  if (linkToHead) {
    blist->next = node->blistHead;
    blist->prev = NULL;

    if (blist->next != NULL)
      blist->next->prev = blist;
    else
      node->blistTail = blist;

    node->blistHead = blist;
  } else {
    blist->next = NULL;
    blist->prev = node->blistTail;

    if (blist->prev != NULL)
      blist->prev->next = blist;
    else
      node->blistHead = blist;

    node->blistTail = blist;
  }
  node->nBlists++;
}

/**
 * Remove a blist from the global list.
 *
 * \param[in]     blist           The blist to be be un-linked
 */
void blist_unlinkGlobal(IM_SHARED *im_shared, IM_BLIST *blist)
{
  IM_STORE_NODE *node;
  BLIST_CTX *ctx = im_shared->blist_ctx;

  HQASSERT(blist != NULL, "blist is NULL");

  ctx->im_blistc--;
  blist->global = FALSE;

  /* Find the store node for size abytes */
  node = im_shared->im_list;
  while (node != NULL && blist->abytes != node->abytes)
    node = node->next;
  HQASSERT(node != NULL, "Blist should be linked to a node");

  /* Do the unlinking */
  if (node->blistHead == blist)
    node->blistHead = blist->next;

  if (node->blistTail == blist)
    node->blistTail = blist->prev;

  node->nBlists--;

  if (blist->next != NULL)
    blist->next->prev = blist->prev;
  if (blist->prev != NULL)
    blist->prev->next = blist->next;

  blist->next = NULL;
  blist->prev = NULL;

  blist_checkGlobal(im_shared);
}

/**
 * ASSERT function to check the validity of the global list of blists
 */
void blist_checkGlobal(IM_SHARED *im_shared)
{
#if defined( ASSERT_BUILD )
  size_t nBlists = 0, nIMBlists = 0;
  IM_STORE_NODE *node;
  static int check_count = 0;
  BLIST_CTX *ctx = im_shared->blist_ctx;
  size_t im_blistc = ctx->im_blistc;

  /*
   * This is an expensive function for huge lists,
   * reduce the testing for large lists.
   * It can also be very epensive for smaller lists, so need to throttle
   * these back to 1% of calls as well.
   */
  if ( (im_blistc > 1000   && im_blistc % 1000   != 0) ||
       (im_blistc > 10000  && im_blistc % 10000  != 0) ||
       (im_blistc > 100000 && im_blistc % 100000 != 0))
    return;
  else if (++check_count < 100 )
    return;
  else
    check_count = 0;

  for ( node = im_shared->im_list; node != NULL; node = node->next ) {
    int32 nBlistsForNode = 0;

    if ( node->blistHead != NULL ) {
      IM_BLIST *blist = node->blistHead;

      HQASSERT(blist->prev == NULL, "Head blist->prev not NULL");

      for ( ; blist != NULL; blist = blist->next ) {
        nBlists++;
        nBlistsForNode++;
        nIMBlists++;
        HQASSERT(blist_usable(blist), "Global blist has an unusable block");
        HQASSERT(blist->next != NULL || node->blistTail == blist,
                 "Tail blist->next not NULL");
      }
    } else {
      HQASSERT(node->blistTail == NULL, "Tail blist not NULL");
    }
    HQASSERT(nBlistsForNode == node->nBlists,
             "Inconsistent blist count for node");

    HQASSERT(node->next == NULL || node->abytes > node->next->abytes,
             "nodes not in size order");
  }
  HQASSERT(nBlists == im_blistc, "Inconsistent global blist count");
  HQASSERT(nBlists == nIMBlists,
           "nBlists is not the required total");
#else
  UNUSED_PARAM(IM_SHARED*, im_shared);
#endif /* ASSERT_BUILD */
}

/**
 * ASSERT function to check the validity of a local list of blists
 *
 * \param[in]     blist   The head of the local blist store to check
 * \param[in]     nblists The expected number of blist's in the chain
 */
void blist_checkLocal(IM_BLIST* blist, int32 nblists)
{
#if defined( ASSERT_BUILD )
  int32 n = 0;

  if ( blist != NULL ) {
    HQASSERT(blist->prev == NULL, "Head blist->prev not NULL");
    for ( ; blist != NULL; blist = blist->next )
      ++n;
  }
  HQASSERT(n == nblists, "Inconsistent local blist count");
#else
  UNUSED_PARAM(IM_BLIST *, blist);
  UNUSED_PARAM(int32, nblists);
#endif /* ASSERT_BUILD */
}

/**
 * Insert a blist into a list of blists local to this store
 *
 * \param[in]     blist   The blist to be inserted
 * \param[in,out] plane   The image plane structure in which to add the blist
 */
void blist_link(IM_BLIST *blist, IM_PLANE *plane)
{
  /* Check that blist (to be added) is not already on the chain. */
  HQASSERT(blist != NULL, "blist is NULL");
  HQASSERT(plane != NULL, "plane is NULL");
  HQASSERT(blist->next == NULL && blist->prev == NULL,
           "blist already in a list");

  /* Adding to a local list */
  blist_checkLocal(plane->blisth, plane->nBlists);

  plane->nBlists += 1;

  if ( plane->blisth == NULL )
    plane->blisth = blist;
  else {
    IM_BLIST *blist_next = plane->blisth;
    IM_BLIST *blist_prev = NULL;

    blist->next = blist_next;
    blist->prev = blist_prev;

    if (blist->next != NULL)
      blist->next->prev = blist;
    if (blist->prev != NULL)
      blist->prev->next = blist;

    if ( plane->blisth == blist_next )
      plane->blisth = blist;
  }
  blist_checkLocal(plane->blisth, plane->nBlists);
}

/**
 * Remove a blist from a list of blists local to this store
 *
 * \param[in]     blist   The blist to be removed
 * \param[in,out] plane   The image plane structure from which to remove the
 *                        blist
 */
void blist_unlink(IM_BLIST *blist, IM_PLANE *plane)
{
#if defined( ASSERT_BUILD )
  HQASSERT(blist != NULL, "blist is NULL");
  HQASSERT(plane != NULL, "plane is NULL");

  /* Check blist to be freed is on the chain. */
  {
    IM_BLIST *tblist = plane->blisth;
    while (tblist != NULL) {
      if (tblist == blist)
        break;
      tblist = tblist->next;
    }
    HQASSERT(tblist == blist, "didn't find blist in list to remove");
  }
#endif

  blist_checkLocal(plane->blisth, plane->nBlists);

  /* removing from a local list */
  plane->nBlists -= 1;

  if ( plane->blisth == blist )
    plane->blisth = blist->next;

  if (blist->next != NULL)
    blist->next->prev = blist->prev;
  if (blist->prev != NULL)
    blist->prev->next = blist->next;

  blist->next = NULL;
  blist->prev = NULL;

  blist_checkLocal(plane->blisth, plane->nBlists);
}

/**
 * ASSERT function to check the validity of teh blists in an image store
 *
 * \param[in]     ims   The image store to check
 */
static void checkstorerelease(IM_STORE *ims)
{
#ifdef ASSERT_BUILD
  int32 planei;

  HQASSERT(ims, "ims NULL");
  HQASSERT(!ims->openForWriting, "Store should be non-writeable");

  for (planei = 0; planei < ims->nplanes; planei++) {
    IM_PLANE *plane = ims->planes[planei];

    if ( plane != NULL ) {
      IM_BLIST *blist, *blist_next;

      blist_checkLocal(plane->blisth, plane->nBlists);
      blist_next = plane->blisth;
      while (blist_next != NULL) {
        blist = blist_next;
        blist_next = blist->next;

        if ( blist->wasGlobal )
          im_blockcheckrelease(blist->block);
      }
    }
  }
#else  /* !ASSERT_BUILD */
  UNUSED_PARAM(IM_STORE *, ims);
#endif /* ASSERT_BUILD */
}

/**
 * Release all
 *
 * \param[in]     ims          The image store to use
 * \param[in]     band         band index (or DL_LASTBAND)
 * \param[in]     nullblock    Whether blocks are nulled?
 */
void blist_release(IM_STORE *ims, int32 band, Bool nullblock)
{
  int32 planei;

  for (planei = 0; planei < ims->nplanes; planei++) {
    IM_PLANE *plane = ims->planes[planei];

    if ( plane != NULL ) {
      IM_BLIST *blist, *bnext;

      blist_checkLocal(plane->blisth, plane->nBlists);

      for ( blist = plane->blisth; blist != NULL; blist = bnext ) {
        bnext = blist->next;
        im_blockrelease(ims, blist, nullblock, plane, band);
      }
    }
  }
  blist_checkGlobal(ims->im_shared);
}

/**
 * Walk over all stores that have had their data either compressed or purged
 * to disk and whose blists may now be moved onto the global blist store.
 * Once on the global blist store, purge it down to the minimum required for
 * rendering the set of images on the display list.
 */
void blist_purgeGlobal(IM_SHARED *im_shared)
{
  IM_STORE_NODE *node, *nodenext;

  for ( node = im_shared->im_list; node != NULL; node = node->next ) {
    HQASSERT(node->ims[IM_ACTION_SHAREBLISTS_2] == NULL &&
             node->nStores[IM_ACTION_SHAREBLISTS_2] == 0,
             "Shouldn't be any stores on blist2 - we don't purge them");
  }

  /* All available images should be on the IM_ACTION_SHAREBLISTS_1 list of
   * each store node. Walk over all such stores looking for blists to move
   * to the global list.
   */
  for ( node = im_shared->im_list; node != NULL; node = nodenext ) {
    IM_STORE *ims, *imsnext;

    nodenext = node->next;

    for ( ims = node->ims[IM_ACTION_SHAREBLISTS_1]; ims != NULL;
          ims = imsnext ) {
      int32 planei;

      HQASSERT(!ims->openForWriting, "Store should be non-writeable");
      HQASSERT(ims->action == IM_ACTION_SHAREBLISTS_1 ||
               ims->action == IM_ACTION_SHAREBLISTS_2,
               "Expected an action of type shareblists");

      imsnext = ims->next;
      checkstorerelease(ims);
      for ( planei = 0; planei < ims->nplanes; ++planei )
        if ( ims->planes[planei] != NULL )
          (void)blist_purge(im_shared, ims->planes[planei], 0);

      /*
       * Once all blists have been dealt with, the store action should
       * be updated
       */
      if (ims->action == IM_ACTION_SHAREBLISTS_1 && ims_can_write2disk(ims) ) {
        im_relinkims(ims, IM_ACTION_WRITETODISK);
        ims->action = IM_ACTION_WRITETODISK;
      } else {
        im_relinkims(ims, IM_ACTION_NOTHINGMORE);
        ims->action = IM_ACTION_NOTHINGMORE;
      }
    }
  }
}

/*
 * Failed to find a blist on the image; go take one from the head of
 * global list.
 *
 * \param[in]     abytes       Size of image block
 * \param[in]     bx           Column index
 * \return                     The blist we found or NULL
 */
IM_BLIST *blist_findGlobal(IM_SHARED *im_shared, int32 abytes, int32 bx)
{
  IM_STORE_NODE *node;
  IM_BLIST *blist = NULL;
  BLIST_CTX *ctx = im_shared->blist_ctx;

  blist_checkGlobal(im_shared);

  /* Find the blist node with smallest size sufficient for the job */
  node = im_shared->im_list;
  while (node != NULL && node->abytes != abytes)
    node = node->next;

  HQASSERT(node != NULL && node->abytes == abytes,
           "Should have found node for this blist");

  /* Now search through the nodes backwards until a blist is found */
  for ( ; blist == NULL && node != NULL; node = node->prev ) {
    if ( node->blistHead != NULL ) {
      size_t blistcount = ctx->im_blistc;

      /* First look for a suitable blist of the same type */
      if ( blistcount > 0 ) {
        blist = node->blistHead;
      }

      /* Couldn't find a blist of the required type.  Look for a blist of any of
       * image/filter. We will allow a downgrade from filter to image type.
       */
      if ( blist == NULL )
        blist = node->blistHead;

      if ( blist != NULL ) /* Take off the global list and return it. */ {
        blist->bx = bx;
        blist_unlinkGlobal(im_shared, blist);
      }
    }
  }
  if ( blist != NULL )
    im_blockclear(blist, abytes, FALSE);
  blist_checkGlobal(im_shared);
  return blist;
}

/**
 * Search for a blist in the given local image store
 *
 * \param[in]     ims          Image store to search
 * \param[in]     abytes       Size of image block
 * \param[in]     planei       Index of plane
 * \param[in]     bx           Column index
 * \param[in]     lookInGlobal Should we also look in the global list ?
 * \param[in]     desperate    Steal image blocks in low memory and rendering
 * \return  The blist found or NULL.
 */
IM_BLIST *blist_find(IM_STORE *ims, int32 abytes, int32 planei, int32 bx,
                     Bool lookInGlobal, Bool desperate)
{
  IM_SHARED *im_shared = ims->im_shared;
  IM_PLANE *plane = ims->planes[planei];
  IM_BLIST *blist = NULL;
  int32 pass;

  /*
   * Two passes looking for the blist.
   * First of all see if we can find a cached blist for this column.
   * If we fail, then look for a blist with bx == -1
   */
  for ( pass = 0; pass < 2 && blist == NULL; pass++ ) {
    for ( blist = plane->blisth; blist != NULL; blist = blist->next ) {
      if ( blist->bx == ((pass == 0) ? bx : -1 ) && blist_usable(blist) ) {
        blist->bx = bx; /* Just in case it was -1 */
        break;
      }
    }
  }

  if ( blist == NULL && lookInGlobal ) {
    /* Failed to find a blist on the image; go take one from the head of
     * global list.
     */
    blist = blist_findGlobal(im_shared, abytes, bx);
    if ( blist ) {
      HQASSERT(blist_usable(blist), "Global blist has an unusable block");
      blist->bx = bx;
      blist_link(blist, ims->planes[planei]);
    }

    /* Last gasps for rendering - grab a blist that's already being used */
    if ( blist == NULL && desperate ) {
#ifdef DEBUG_BUILD
      /*
       * There used to be an assert here of
       * "THIS IS RECOVERABLE - but let us know because it makes rendering slow"
       * i.e. really a warning rather than an assert, as it was not always
       * true depending on the number of threads that were trying to use blists
       * at the same time. So it was converted to a debug warning instead.
       */
      if ( debug_imstore & IMSDBG_WARN )
        monitorf((uint8*)"Warning : reusing blists : slows rendering\n");
#endif
      /*
       * Two more passes at trying to find a blist
       *   1) grab one marked as global from any plane in the image list
       *   2) grab any usable one from any plane in the image list
       */
      for ( pass = 0; pass < 2 && blist == NULL; pass++ ) {
        for (planei = 0; planei < ims->nplanes && blist == NULL; planei++) {
          plane = ims->planes[planei];

          if ( plane != NULL && plane->blisth != NULL ) {
            for (blist = plane->blisth; blist != NULL; blist = blist->next ) {
              if ( pass > 0 || blist->wasGlobal ) {
                if ( blist_usable(blist) ) {
                  blist->bx = bx;
                  break;
                }
              }
            }
          }
        }
      }

      /* grab one marked as global from any plane in other images.
       */
      if ( blist == NULL ) {
        /** \todo JJ Grab one. Probably need a new list of wasGlobal
         * blists to enable this efficiently.
         */
      }
    }
  }

  /* Either we've found a floating blist, or a blist which needs to be
   * compressed or go to disk, or we can just take it from its existing block.
   */
  if ( blist != NULL ) {
    im_blockclear(blist, abytes, TRUE);
    HQASSERT(blist->bx == bx, "Got blist but bx incorrect");
    if ( !lookInGlobal ) {
      HQASSERT(blist->abytes == abytes, "got blist but incorrect abytes");
      HQASSERT(blist->data != NULL, "block data got lost");
    }
  }
  blist_checkGlobal(im_shared);

  return blist;
}

/**
 * Free all of the data structures hanging of the blist, and the
 * blist itself, returning the next one in the list.
 *
 * \param[in,out] blist    The blist to free
 * \return                 The next blist in the chain
 */
IM_BLIST *blist_freeall(IM_SHARED *im_shared, IM_BLIST *blist)
{
  IM_BLIST *next = blist->next;

  blist_unlinkGlobal(im_shared, blist);

  im_datafree(im_shared, blist->data, blist->abytes);
  if ( blist->block )
    im_blocknull(blist->block);
  blist_destroy(im_shared, blist, FALSE);

  return next;
}

/**
 * Add the specified blist to the given blist chain
 *
 * \param[in]     blist    The blist to add
 * \param[in,out] chain    The chain of blists's to add to
 */
void blist_add2chain(IM_BLIST *blist, IM_BLIST **chain)
{
  if (*chain == NULL )
    *chain = blist;
  else {
    blist->next = *chain;
    blist->prev = (*chain)->prev;
    (*chain)->prev = blist;
    *chain =  blist;
  }
}

/**
 * Remove the specified blist (or all of them if blist == NULL )
 * from the given blist chain
 *
 * \param[in]     blist    The blist to remove (NULL means all of them)
 * \param[in,out] chain    The chain of blists's to remove it from
 */
void blist_unchain(IM_SHARED *im_shared, IM_BLIST *blist, IM_BLIST **chain)
{
  IM_BLIST *bl, *next;

  for ( bl = *chain; bl != NULL && bl->prev != NULL; bl = bl->prev)
    EMPTY_STATEMENT(); /* walk to the head of the chain */

  for ( ; bl != NULL; bl = next ) {
    next = bl->next;
    if ( blist == NULL || bl == blist ) {
      if ( bl->prev != NULL)
        bl->prev->next = bl->next;
      if ( bl->next != NULL)
        bl->next->prev = bl->prev;
      if (bl == (*chain))
        *chain = bl->next;
      bl->next = NULL;
      bl->prev = NULL;
      if ( blist != NULL )
        return;
      else
        blist_destroy(im_shared, bl, FALSE);
    }
  }
  if ( blist != NULL )
    HQFAIL("failed to find blist in list");
}

/**
 * Purge some blists associated with the given image plane
 *
 * \param[in,out]  plane      The plane from which to purge the blists's
 * \param[in]      minblists  The minimum number to leave
 * \return                    The number of purged blist's
 */
int32 blist_purge(IM_SHARED *im_shared, IM_PLANE *plane, int32 minblists)
{
  IM_BLIST *blist, *next;
  int32 purged = 0;

  if ( plane->blisth == NULL )
    return 0;

  for ( blist = plane->blisth; blist->prev != NULL; blist = blist->prev)
    EMPTY_STATEMENT();

  for ( ; blist != NULL && plane->nBlists > minblists; blist = next ) {
    IM_BLOCK *block = blist->block;

    next = blist->next;

    HQASSERT(block == NULL || im_blockcomplete(block),
             "blist block should be complete");
    HQASSERT(block == NULL || im_blockstorage(block) != IM_STORAGE_NONE,
             "blist block should be allocated");

    if ( block == NULL || im_blockisusable(block) ) {
      /* The action of moving a blist onto the global list is often
       * enough to allow interpretation of images to continue. If we
       * are not interpreting an image at the moment then we will
       * simply carry on with other iterations of the low memory
       * handler to get more memory.
       */
      blist_unlink(blist , plane);
      blist_linkGlobal(im_shared, blist, TRUE);
      purged++;
    }
  }
  return purged;
}

/**
 * Sets data pointer and abytes fields for the given blist
 *
 * \param[in,out] blist   The blist having its data pointer changed
 * \param[in]     data    The new data value
 * \param[in]     abytes  The new abytes field
 */
void blist_setdata(IM_BLIST *blist, uint8 *data, int16 abytes)
{
  blist->data   = data;
  blist->abytes = abytes;
}

/**
 * Return the data pointer from the given blist
 *
 * \param[in]      blist   The blist being queried
 * \param[out]     abytes  If non-NULL, return the blist abytes
 * \return                 The data pointer of the given blist
 */
uint8 *blist_getdata(IM_BLIST *blist, int16 *abytes)
{
  if ( abytes != NULL )
    *abytes = blist->abytes;
  return blist->data;
}

/**
 * Return the sizeof a IM_BLIST structure
 *
 * \return             sizeof(IM_BLIST)
 */
size_t blist_sizeof()
{
  return sizeof(IM_BLIST);
}

/**
 * Set a new value for the image block stored in this blist
 *
 * \param[in,out]  blist  The blist whose block is to be set
 * \param[in]      block  The new value for the blist block
 */
void blist_setblock(IM_BLIST *blist, IM_BLOCK *block)
{
  blist->block = block;
}

/**
 * Return the image block for the given blist
 *
 * \param[in]  blist   The blist being queried
 * \return             The image block for that blist
 */
IM_BLOCK *blist_getblock(IM_BLIST *blist)
{
  return blist->block;
}

/**
 * Adjust blists for the given plane to allow for the fact that it is
 * an xyswapped image.
 *
 * \param[in]      ims    The image store
 * \param[in]      plane  The plane for which the blists need to be adjusted
 */
void blist_xyswap(IM_STORE *ims, IM_PLANE *plane)
{
  IM_BLIST *blist, *next;

  /* Assign all blist's to an unallocated column because xyswapped
   * stores have bx assignments that are invalid for rendering.
   * The blist will be allocated to a particular bx during rendering
   * or when the store is purged.
   */
  for ( blist = plane->blisth; blist != NULL ; blist = blist->next )
    blist->bx = -1;

  /* Remove excessive blists for rendering requirements */
  for ( blist = plane->blisth; blist != NULL &&
        plane->nBlists > NUM_RENDERING_BLISTS(ims); blist = next ) {
    next = blist->next;

    /* Only free the non-global blist's. The global ones will be put
     * back into the global list below (in im_storerelease) and will
     * be available for reuse at that point.
     */
    if ( !blist->wasGlobal ) {
      IM_BLOCK *block = blist->block;

      if ( block != NULL ) {
        HQASSERT(ims->abytes == blist->abytes, "blist not expected size");
        im_block_null(block, 0 ); /* blist */
      }
      if ( block == NULL || im_blockisusable(block) ) {
        /* If a block is moveable, it can only point to data if it has
         * a blist. So free off any data memory.
         */
        im_datafree(ims->im_shared, blist->data, blist->abytes);
        if ( block != NULL )
          im_block_null(block, 1); /* data */
      }
      blist_unlink(blist, plane);
      blist_destroy(ims->im_shared, blist, FALSE);
    }
  }
}

/**
 * Reassign any blist blocks if the current associations aren't usable.
 * This may happen particularly when compressing images in low memory and
 * for xyswapped images.
 *
 * \param[in]      ims    The image store
 * \param[in,out]  plane  The plane for which the blists are to be reassigned
 */
void blist_reassign(IM_STORE *ims, IM_PLANE *plane)
{
  IM_BLIST *blist;
  int32 blockIdx = 0;

  for ( blist = plane->blisth; blist != NULL && blockIdx < ims->nblocks;
        blist = blist->next ) {
    if ( (blist->block == NULL || !im_blockisusable(blist->block)) &&
         !blist->wasGlobal ) {
      /* We've got a block that isn't usable.  Try to associate it with a
       * block that is usable.  Choose the first block found in the block
       * array that isn't already associated with a blist.
       */

      for ( ; blockIdx < ims->nblocks; blockIdx++ ) {
        IM_BLOCK *block = plane->blocks[blockIdx];

        if ( block != NULL && im_blockisusable(block) &&
             im_blockblist(block) == NULL && im_blockdata(block, 0) != NULL ) {
          if ( blist->block != NULL )
            im_block_null(blist->block, 0 /*blist*/);

          blist->block = block;
          im_block2blist(blist, FALSE);
          break;
        }
      }
    }
  }
}

/**
 * Purge blists of the given plane in preparation for rendering.
 *
 * \param[in]      ims    The image store
 * \param[in]      plane  The plane for which the blists are to be purged.
 */
void blist_purge2disk(IM_STORE *ims, IM_PLANE *plane)
{
  IM_BLIST *blist;

  /* Purge the blists if appropriate:
   * - if any blocks have been purged, we will need enough blists available
   *   during rendering. NB. We're using a simple strategy of purging all
   *   blist blocks if *any* blocks have been purged, regardless of how many
   *   blist blocks are actually required.
   * - if we allocated a global blist to an image and we never purged it
   *   while processing the image, then give it a chance to purge now.
   *   This condition is most likely to happen for xyswapped images which
   *   we try to purge after processing because it gives better locality
   *   for rendering.
   * If we fail to purge blist's (either global or image local) they will not
   * be available for rendering (including partial painting). With enough such
   * losses, rendering may fail with VMError.
   */
  for ( blist = plane->blisth; blist != NULL; blist = blist->next ) {
    if ( ims->purged || blist->wasGlobal )
      (void)im_blockpurge(ims, blist->block, TRUE);
  }
}

/**
 * ASSERT function to check the validity of the given blist in the
 * pre-render phase
 *
 * \param[in]      blist    Head of blist chain to check
 */
void blist_pre_render_assertions(IM_BLIST *blist)
{
  for ( ; blist != NULL; blist = blist->next ) {
    IM_BLOCK *block = blist_getblock(blist);

    HQASSERT(blist->global, "Block global mark is wrong");
    if (block != NULL)
      im_block_blist_check(block, blist);
  }
}

/**
 * Check column validity for given blist
 *
 * \param[in]      blist    Head of blist chain to check
 * \param[out]     ccheck   Array to write colum results to
 * \param[in]      maxbx    Length of ccheck array (with -1 index as well)
 */
void blist_ccheck(IM_BLIST *blist, int32 *ccheck, int32 maxbx)
{
  for ( ; blist != NULL; blist = blist->next ) {
    IM_BLOCK *block = blist->block;

    HQASSERT(!blist->global || blist->wasGlobal, "global mark is wrong");
    if ( block != NULL )
      im_block_acheck(block, blist);
    if ( blist->bx == -1)
      ccheck[blist->bx]++;
    else if (  blist->bx < maxbx )
      ccheck[blist->bx] |= 2;
  }
}

/**
 * Return whether the blist is global or not
 *
 * \param[in]   blist  blist being queried
 * \return             global status of the given blist
 */
Bool blist_global(IM_BLIST *blist)
{
  return blist->global;
}

/**
 * Return whether the blist was global or not
 *
 * \param[in]   blist  blist being queried
 * \return             wasGlobal status of the given blist
 */
Bool blist_wasGlobal(IM_BLIST *blist)
{
  return blist->wasGlobal;
}

/**
 * Set the given blists bx value
 *
 * \param[in,out] blist  blist being changed
 * \param[in]     bx     new bx value
 */
void blist_setbx(IM_BLIST *blist, int32 bx)
{
  blist->bx = bx;
}

/**
 * Get the given blists bx value
 *
 * \param[in]  blist  blist being queried
 * \return            bx value
 */
int32 blist_getbx(IM_BLIST *blist)
{
  return blist->bx;
}

/**
 * Check to see if we have exceeded the required number of blists,
 * with a tolerance of 'margin'.
 *
 * \param[in] im_shared  The shared image state for this page.
 * \param[in] margin  The margin we are allowed to exceed limit by.
 * \return            If there are too many blists.
 */
Bool blist_toomany(IM_SHARED *im_shared, size_t margin)
{
  return im_shared->blist_ctx->im_blistc > blist_required(im_shared) + margin;
}


/**
 * Return the number of global blists required.
 *
 * \param[in] im_shared  The shared image state for this page.
 * \return  Number of global blists required.
 */
size_t blist_required(IM_SHARED *im_shared)
{
  BLIST_CTX *ctx = im_shared->blist_ctx;
  size_t req = 0;
  size_t i;

  for ( i = 0 ; i < ctx->nthreads ; ++i )
    req += ctx->wide_bands[i].width;
  return req;
}


/** Add the image store to the estimate of required global blists.
 *
 * \param[in] ims  The image store added.
 * \param[in] band1  The first band of the image.
 * \param[in] band2  The last band of the image.
 */
void blist_add_extent(IM_STORE *ims, bandnum_t band1, bandnum_t band2)
{
  BLIST_CTX *ctx = ims->im_shared->blist_ctx;
  bandnum_t band;
  size_t num_blists, thinnest = (size_t)-1;
  size_t i, thinnest_index = (size_t)-1;

  HQASSERT(ims, "ims somehow NULL");

  if ( !ims_can_compress(ims) && !ims_can_write2disk(ims) )
    return;
  num_blists = ims->nplanes * (size_t)ims->xblock;

  /* This basically keeps track of the NUM_THREADS() widest bands. Because there
     are only a few, we don't use a heap or other clever structure. */
  for ( band = band1 ; band <= band2 ; band++ ) {
    /* Scan the array, looking for the same band or finding the thinnest. */
    for ( i = 0 ; i < ctx->nthreads ; ++i ) {
      if ( ctx->wide_bands[i].bandno == band )
        break;
      else
        if ( ctx->wide_bands[i].width < thinnest ) {
          thinnest_index = i; thinnest = ctx->wide_bands[i].width;
        }
    }
    if ( i == ctx->nthreads ) /* This band wasn't in the array. */ {
      HQASSERT( thinnest_index != (size_t)-1, "Didn't find thinnest in the array.");
      if ( num_blists <= thinnest )
        return;
      ctx->wide_bands[thinnest_index].bandno = band;
      ctx->wide_bands[thinnest_index].width = num_blists;
      thinnest = num_blists; /* Not true, just a start for recalculation. */
    } else /* This band was in the array, update if necessary. */
      if ( ctx->wide_bands[i].width < num_blists ) {
        if ( ctx->wide_bands[i].width == thinnest )
          thinnest = num_blists; /* Not true, but it'll be recalculated. */
        ctx->wide_bands[i].width = num_blists;
      }
  }
}


/**
 * Create a pretty debug printout on the state of the global blists.
 */
void blist_globalreport(IM_SHARED *im_shared)
{
  IM_STORE_NODE *node;
  BLIST_CTX *ctx = im_shared->blist_ctx;

  monitorf((uint8*)"== Global blist : %d ==\n", ctx->im_blistc);
  for ( node = im_shared->im_list; node != NULL; node = node->next )
    monitorf((uint8*)"  list[%d] = %d\n", node->abytes, node->nBlists);
}

/* Log stripped */
