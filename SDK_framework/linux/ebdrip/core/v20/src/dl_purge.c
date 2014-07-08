/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_purge.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to purge Display List (DL) data to disk and load it on
 * demand later.
 *
 * The RIP Display List is the major user of memory during the ripping process.
 * DL Memory usage can increase drastically as job complexity increases. It is
 * therefore important for there to be the possibility of purging this data
 * to disk in low memory situations. This file contains routines that cause DL
 * data to be purged to disk when low memory actions occur, and then re-load
 * the data on demand later on.
 *
 * The RIP Display List is a complex tagged hierarchical structure. Each item
 * in the DL consists of a number of headers (DLREF, LISTOBJECT) and then
 * the body of the actual data (listobject->dldata.xxxx). Memory can become
 * exhausted due to a number of possible reasons. The two most typical are 1)
 * The sum of the DL body data (referenced by the dldata pointer) becomes too
 * large, and 2) the total number of DL objects becomes so large that the
 * memory used for the headers fills memory. In case 1), the data for any
 * object type could fill memory, but the two most common are NFILL and GOURAUD
 * objects. Therefore the purge actions are divided into two phases. The first
 * phase writes the body of NFILL and GOURAUD data to disk, and then re-loads
 * it on demand and render time. The second phase reduces the number of DL
 * object headers by aggregating them into a lump of data on disk, and then
 * replacing the object on the DL with a single reference to this aggregate.
 * Performance will fall of as o(N) as a result of the first phase of purging,
 * but o(N^2) as a result of the second. Thus all first phase purging is
 * completed before the necessity of phase 2 work kicks in.
 *
 * Each level in the DL hierarchy is represented by an abstraction called a
 * "Hierarchical Display List" or "HDL". The top level HDL can contain further
 * HDLs within. This results in a tree structure for the whole DL. The
 * purge actions need to step through the entire DL tree in search of data that
 * can be purged. However, new branches of the tree are only added to the
 * parent on completion. There may be considerable amounts of data in such
 * branches prior to their addition to the DL root. HDLs which are open for
 * object addition are stored in a stack. It is therefore necessary for the
 * purge process to go through this entire stack looking for purge candidates.
 * This ensures partially completed branches are scanned even if they are not
 * yet connected to the root. But low-memory actions can occur quite
 * frequently. A purge over all open HDLs may require quite some time stepping
 * through all the elements, even if there is nothing to do. (It is possible
 * to have DLs with millions of elements.) Therefore an ideal solution would
 * record the point reached in an HDL to allow a purge to continue from this
 * point next time.
 */

#include "core.h"
#include "coreinit.h"
#include "display.h"
#include "dl_bres.h"
#include "dlstate.h"
#include "hdl.h"
#include "hdlPrivate.h"
#include "group.h"
#include "devices.h"
#include "dl_purge.h"
#include "dl_free.h"
#include "shadex.h"
#include "monitor.h"
#include "params.h"
#include "gs_tag.h"
#include "lowmem.h"
#include "mlock.h"
#include "swerrors.h" /* FAILURE */
#include "dl_ref.h"


/**
 * Need to maintain a private free list of DLREFs to avoid fragmenting
 * and thrashing the main memory system. This is the size in elements of that
 * local free list.
 */
#define DLPURGE_FLS 100

/**
 * 2nd phase DL purging involves aggregating a number of DL objects into
 * a single object. This define controls the maximum number of objects
 * that can be so aggregated.
 */
#define MAX_DL_PACK 256

/**
 * All the information about DL elements that have been purged to disk.
 */
typedef struct DL_ON_DISK {
  Bool enabled;                /**< Is DL purging enabled */
  int max_level; /**< Maximum level to purge, from SystemParams.DLBanding */
  DEVICELIST *tmpdev;          /**< disk device to purge DL data to */
  DEVICE_FILEDESCRIPTOR fdesc; /**< file handle for DL disk purging */
  size_t purge_size;           /**< current bytes purged to disk */
  unsigned npurged;            /**< number of objects purged to disk */
  size_t max_size;             /**< maximum size of any purged DL object */
  size_t load_size;            /**< Amount of memory allocated fo DL loading */
  void *load_space;            /**< Pointer to buffer for loading DL objects */
  struct {
    Bool fill;                 /**< if free list being filled or emptied */
    size_t index;              /**< next available space in free list */
    DLREF *list[DLPURGE_FLS];  /**< elements of free list */
  } free;                      /**< private DLREF freelist */
  struct {
    DLREF *dlref;
    Bool writeBack;
    LISTOBJECT lobj[MAX_DL_PACK];
  } dcache;                    /**< dl purge disk cache */
} DL_ON_DISK;

/**
 * Single structure holding all DL on disk information.
 */
static DL_ON_DISK dld;

/**
 * Initialise the DL on disk structure.
 */
static void init_dld_common(void)
{
  dld.enabled = FALSE;
  dld.max_level = 0;
  dld.tmpdev = NULL;
  dld.fdesc = 0;
  dld.purge_size = 0;
  dld.npurged = 0;
  dld.max_size = 0;
  dld.free.index = 0;
  dld.free.fill = TRUE; /** \todo BMJ 22-Oct-08 :  free this ? */
  dld.dcache.dlref = NULL;
  dld.dcache.writeBack = FALSE;
}


/**
 * Reset the DL purging subsystem as a new DL is being started.
 */
void dlpurge_reset(void)
{
  if ( dld.tmpdev != NULL )
    (void)(*theICloseFile(dld.tmpdev))(dld.tmpdev, dld.fdesc);

  init_dld_common();
  /* Can't access this from the handlers, so store it. */
  dld.max_level = (int)get_core_context_interp()->systemparams->DLBanding;
  dld.enabled = !dlRandomAccess(); /* Accesses SystemParams, UserParams,
                                      needs multi_nthreads set. */
}


/**
 * Write a single lump of DL data to disk.
 */
static Bool dl2disk(uint8 *head, size_t *info, size_t hbytes, LISTOBJECT *lobj)
{
  Hq32x2 filepos;

  HQASSERT(hbytes <= MAXINT32, "Write size overflow");
  HQASSERT(info[0] <= MAXINT32, "Write size overflow");
  Hq32x2FromSize_t(&filepos, dld.purge_size);
  if ( (*theISeekFile(dld.tmpdev))(dld.tmpdev, dld.fdesc,
                                    &filepos, SW_SET) == 0 ||
       (*theIWriteFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, (uint8 *)info,
                                    (int32)hbytes) != (int32)hbytes ||
       (*theIWriteFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, head,
                                    (int32)info[0]) != (int32)info[0] )
    return device_error_handler(dld.tmpdev);
  lobj->dldata.diskoffset = dld.purge_size;
  lobj->marker |= MARKER_ONDISK;
  dld.purge_size += hbytes + info[0];
  if ( dld.max_size < hbytes + info[0] )
    dld.max_size = hbytes + info[0];
  HQASSERT(dld.max_size <= dld.load_size, "DL Object too big to send to disk");
  dld.npurged++;
  return TRUE;
}

/**
 * Purge the DL data for a single gouraud object.
 */
static size_t purge_gouraud(DL_STATE *page, LISTOBJECT *lobj)
{
  GOURAUDOBJECT *g = lobj->dldata.gouraud;
  size_t info[1];

  info[0] = g->gsize;
  if ( info[0] + sizeof(info) > dld.load_size )
    return 0;
  if ( !dl2disk((uint8 *)g, info, sizeof(info), lobj) )
    return 0;
  dl_free(page->dlpools, g->base, g->gsize, MM_ALLOC_CLASS_GOURAUD);
  return info[0] + sizeof(info);
}

/**
 * Purge an NFILL record to disk.
 */
static size_t purge_nfill(DL_STATE *page, LISTOBJECT *lobj)
{
  NFILLOBJECT *nfill = lobj->dldata.nfill;
  size_t info[2], thd;
  uint8 *head = (uint8 *)nfill;

  info[0] = sizeof_nfill(nfill);
  if ( info[0] + sizeof(info) > dld.load_size )
    return 0;
  for ( thd = 0; thd < (size_t)nfill->nthreads; thd++ ) {
    if ( head > (uint8 *)nfill->thread[thd] )
      head = (uint8 *)nfill->thread[thd];
    nfill->thread[thd] = (NBRESS *)((char *)nfill - (char *)nfill->thread[thd]);
  }
  info[1] = (size_t)((char *)nfill - (char *)head);
  if ( !dl2disk(head, info, sizeof(info), lobj) )
    return 0;
  for ( thd = 0; thd < (size_t)nfill->nthreads; thd++ )
    nfill->thread[thd] = (NBRESS *)((char *)nfill-(char *)nfill->thread[thd]);
  free_nfill(nfill, page->dlpools);
  return info[0] + sizeof(info);
}


/**
 * Carry out a phase 1 DL purge.
 */
static size_t purge_phase1(DL_STATE *page, HDL *hdl)
{
  DLREF *dl;
  size_t pbytes = 0;

  for ( dl = hdlOrderList(hdl); dl != NULL ; dl = dl->next ) {
    if ( dl->inMemory ) {
      LISTOBJECT *lobj = dlref_lobj(dl);

      switch ( lobj->opcode ) {
        case RENDER_fill:
          if ( (lobj->marker & MARKER_ONDISK) == 0 )
            pbytes += purge_nfill(page, lobj);
          break;
        case RENDER_gouraud:
          if ( (lobj->marker & MARKER_ONDISK) == 0 )
            pbytes += purge_gouraud(page, lobj);
          break;
        default:
          break;
      }
    }
  }
  return pbytes;
}


/**
 * 2nd phase DL purge. This involves aggregating a number of objects into a
 * single lump of data, writing it to disk, and creating a new single object
 * that references that lump. i.e. MAX_DL_PACK DLREFs and LISTOBJECTs get
 * converted into a single DLREF.
 */
static size_t purge_phase2(DL_STATE *page, HDL *hdl)
{
  DLREF *dl = hdlOrderList(hdl), *next, *before = NULL;
  LISTOBJECT pack[MAX_DL_PACK];
  uint16 npacked;
  size_t pbytes = 0;

  /*
   * Step through all DL objects but don't process the first and the last,
   * as this makes DL management easier and only very slightly reduces
   * memory savings. Because of this the top level HDL will always start with
   * an un-aggregated erase object, and the tail of the DL does not need
   * to be updated.
   */
  if ( dl == NULL || dlref_next(dl) == NULL )
    return 0;
  before = dl;
  dl = dlref_next(dl);

  for ( npacked = 0, next = dl->next; next != NULL;
        dl = next, next = dl->next ) {
    Bool inMemory = dl->inMemory;

    if ( inMemory ) {
      LISTOBJECT *lobj = dlref_lobj(dl);

      pack[npacked++] = *lobj;
      /* dl_forall can now rely that MARKER_DL_FORALL is unset on disk. */
      pack[npacked-1].marker &= (~MARKER_DL_FORALL);
      /** \todo BMJ 15-Oct-08 :  freeing and tag bytes ! */
      pbytes += sizeof(LISTOBJECT);
      dl_free(page->dlpools, (uint8 *)lobj, sizeof(LISTOBJECT),
          MM_ALLOC_CLASS_LIST_OBJECT);
      /* Fill up local free list before we actually start freeing them */
      if ( dld.free.fill && dld.free.index < DLPURGE_FLS )
        dld.free.list[dld.free.index++] = dl;
      else {
        dld.free.fill = FALSE;
        free_n_dlrefs(dl, 1, page->dlpools);
        pbytes += sizeof(DLREF);
      }
    }
    if ( npacked > 0 && ( npacked >= MAX_DL_PACK || dlref_next(next) == NULL ||
         !inMemory ) ) {
      size_t info[1];
      LISTOBJECT lobj;
      DLREF *newdl;

      HQASSERT(dld.free.index > 0, "No dl purge memory !");
      newdl = dld.free.list[--dld.free.index];
      if ( dld.free.index == 0 )
        dld.free.fill = TRUE;

      info[0] = npacked * sizeof(LISTOBJECT);
      pbytes += info[0];
      if ( !dl2disk((uint8 *)pack, info, sizeof(info), &lobj) )
        return 0;
      newdl->inMemory = FALSE;
      newdl->dl.diskoffset = lobj.dldata.diskoffset;
      newdl->next  = next;
      before->next = newdl;
      newdl->nobjs = npacked;
      npacked = 0;
      before = newdl;
    }
    if ( !inMemory )
      before = dl;
  }
  return pbytes;
}

/**
 * Carry out a DL purge at the given level.
 */
static size_t do_purge_dl(DL_STATE *page, int32 level)
{
  HDL_LIST *hlist;
  size_t pbytes = 0;

  dld.enabled = FALSE; /* Stop getting in here recursively */

  if ( dld.tmpdev == NULL ) {
    dld.tmpdev = find_device((uint8 *)"tmp");
    dld.fdesc = (*theIOpenFile(dld.tmpdev))(dld.tmpdev, (uint8 *)"dl.dat",
                                            SW_RDWR|SW_CREAT|SW_TRUNC);
  }
  /* Step through each HDL associated with this page and attempt a
   * purge. Record the level of the purge to ensure the purging
   * attempts stop when there's nothing to purge anymore. */
  for ( hlist = page->all_hdls; hlist != NULL; hlist = hlist->next ) {
    int curr_level = hdlPurgeLevel(hlist->hdl);

    if ( curr_level < level ) {
      hdlSetPurgeLevel(hlist->hdl, level);
      /* Set the level first, because other threads may be adding items
         just missed by these purges. It might be too late already, but
         it's fairly harmless: the added item will not be purged until
         another is added. */
      if ( curr_level < 1 )
        pbytes += purge_phase1(page, hlist->hdl);
      if ( level >= 2 && curr_level < 2 )
        pbytes += purge_phase2(page, hlist->hdl);
    }
  }
  report_track_dl("dl_purge(?)", pbytes);
  dld.enabled = TRUE;
  return pbytes;
}

/**
 * Return whether anything is currently stored on disk as a result
 * of DL purging.
 */
Bool dlpurge_inuse()
{
  return (dld.enabled && (dld.npurged > 0));
}


/** Shared solicit method of the DL purging low-memory handlers. */
static low_mem_offer_t *dl_purge_solicit(low_mem_handler_t *handler,
                                         low_mem_offer_t *offer,
                                         int level,
                                         corecontext_t *context,
                                         size_t count,
                                         memory_requirement_t* requests)
{
  HDL_LIST *hlist;
  size_t pbytes = 0;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  HQASSERT(context->page->currentdlstate == NULL
           || TAG_BYTES(context->page->currentdlstate) == 0,
           "gstag and dl purging are incompatible");
  if ( !dld.enabled || dld.max_level < level
       || context->page->currentdlstate == NULL /* no current DL */
       /** \todo BMJ 10-Nov-08: sort out gstag and dl purging. */
       /* 2nd level purging is incompatible with gstate tagging because of
          the "two_way_alloc" which makes the LISTOBJECT a variable size.
          Following is not a sufficient test but should act as a reminder
          for now. */
       || (level == 2 && TAG_BYTES(context->page->currentdlstate) != 0) )
    return NULL;

  for ( hlist = context->page->all_hdls; hlist != NULL; hlist = hlist->next ) {
    int curr_level = hdlPurgeLevel(hlist->hdl);

    if ( curr_level < level ) {
      if ( curr_level < 1 )
        pbytes += 512; /* @@@@ HDLs should have size estimates */
      if ( level >= 2 && curr_level < 2 )
        pbytes += 512;
    }
  }
  if ( pbytes == 0 )
    return NULL;
  offer->pool =
    /* Assumes all objs on same level use same pool, but that's just how
       the pools are supposed to be used. */
    dl_choosepool(context->page->dlpools,
                  level == 1 ? MM_ALLOC_CLASS_NFILL : MM_ALLOC_CLASS_DLREF);
  offer->offer_size = pbytes;
  offer->offer_cost = level * 1.5f;
  offer->next = NULL;

  return offer;
}


/** Solicit method of the phase 1 DL purge low-memory handler. */
static low_mem_offer_t *dl_purge1_solicit(low_mem_handler_t *handler,
                                          corecontext_t *context,
                                          size_t count,
                                          memory_requirement_t* requests)
{
  static low_mem_offer_t offer;

  return dl_purge_solicit(handler, &offer, 1, context, count, requests);
}


/** Solicit method of the phase 2 DL purge low-memory handler. */
static low_mem_offer_t *dl_purge2_solicit(low_mem_handler_t *handler,
                                          corecontext_t *context,
                                          size_t count,
                                          memory_requirement_t* requests)
{
  static low_mem_offer_t offer;

  return dl_purge_solicit(handler, &offer, 2, context, count, requests);
}


/** Release method of the phase 1 DL purge low-memory handler. */
static Bool dl_purge1_release(low_mem_handler_t *handler,
                              corecontext_t *context, low_mem_offer_t *offer)
{
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  /* @@@@ could limit the purge according to taken_size */
  (void)do_purge_dl(context->page, 1);
  return TRUE;
}


/** Release method of the phase 2 DL purge low-memory handler. */
static Bool dl_purge2_release(low_mem_handler_t *handler,
                              corecontext_t *context, low_mem_offer_t *offer)
{
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  /* @@@@ could limit the purge according to taken_size */
  (void)do_purge_dl(context->page, 2);
  return TRUE;
}


/** The phase 1 DL purge low-memory handler. */
static low_mem_handler_t dl_purge1_handler = {
  "DL purge phase 1",
  memory_tier_disk, dl_purge1_solicit, dl_purge1_release, FALSE, 0, FALSE };


/** The phase 2 DL purge low-memory handler. */
static low_mem_handler_t dl_purge2_handler = {
  "DL purge phase 2",
  memory_tier_disk, dl_purge2_solicit, dl_purge2_release, FALSE, 0, FALSE };

/* These handlers are thread-safe, because they are disabled during
   rendering and are marked as non-mt-safe, so we only run them in
   single-threaded mode during interpretation, where they worked before
   (or not). To really make them mt-safe,
     1. Most of the dld structure has to be made thread-specific,
     2. dl_load, rewrite_dldata, and sync_disk_cache have to synchronize
        against purging, or use a separate tmpdev and fdesc.
     3. All DL iterations must be changed to lock the HDL against
        purging, and unlock it at the end (we currently have no
        end-iteration interface).
     4. NFILL and Gouraud access has to disable phase 1 purges somehow,
        so their data is not deallocated by it. */


/**
 * Load the body of data that is on disk for the specified DL object.
 */
static uint8 *dl_load(size_t *info, size_t hbytes, LISTOBJECT *lobj)
{
  Hq32x2 filepos;
  uint8 *head = (uint8 *)dld.load_space;

  HQASSERT(dld.tmpdev, "DL on disk but no file open ?");
  HQASSERT(hbytes <= MAXINT32, "Write size overflow");
  Hq32x2FromSize_t(&filepos, lobj->dldata.diskoffset);
  if ( (*theISeekFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, &filepos,
                                   SW_SET) == 0 ||
       (*theIReadFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, (uint8 *)info,
                                   (int32)hbytes) != (int32)hbytes ||
       (*theIReadFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, head,
                                   (int32)info[0]) != (int32)info[0]) {
    (void)device_error_handler(dld.tmpdev);
    return NULL;
  }
  HQASSERT(info[0] <= dld.load_size,"Overflowed DL purge buffer");
  return head;
}

/**
 * Extract the pointer to the NFILL/Gouraud record from the given DL object,
 * loading it from disk if needed.
 */
void *load_dldata(LISTOBJECT *lobj)
{
  switch ( lobj->opcode ) {
    case RENDER_fill:
      if ( (lobj->marker & MARKER_ONDISK) == 0 )
        return (void *)(lobj->dldata.nfill);
      else {
        size_t thd, info[2];
        uint8 *head = dl_load(info, sizeof(info), lobj);
        NFILLOBJECT *nfill;

        nfill = (NFILLOBJECT *)(head + info[1]);
        for ( thd = 0; thd < (size_t)nfill->nthreads; thd++ )
          nfill->thread[thd] = (NBRESS *)((char *)nfill -
                                          (char *)nfill->thread[thd]);
        return (void *)nfill;
      }
    case RENDER_gouraud:
      if ( (lobj->marker & MARKER_ONDISK) == 0 )
        return (void *)(lobj->dldata.gouraud);
      else {
        size_t info[1];
        uint8 *head = dl_load(info, sizeof(info), lobj);

        return (void *)head;
      }
    default:
      /* HQFAIL("Unexpected DL opcode to load from disk"); */
      /* return NULL; */
      return (void *)(lobj->dldata.nfill);
  }
}

/**
 * Re-write purged object to data to disk as it has been changed.
 */
void rewrite_dldata(LISTOBJECT *lobj)
{
  HQASSERT(lobj->opcode == RENDER_gouraud, "Only rewrite gouraud data !\n");
  if ( lobj->marker & MARKER_ONDISK ) {
    uint8 *head = (uint8 *)dld.load_space;
    GOURAUDOBJECT *g = (GOURAUDOBJECT *)head;
    Hq32x2 filepos;

    HQASSERT(dld.tmpdev, "DL on disk but no file open ?");
    Hq32x2FromSize_t(&filepos, lobj->dldata.diskoffset + sizeof(size_t));

    if ( (*theISeekFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, &filepos, SW_SET)
         == 0
         || (*theIWriteFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, head, g->gsize)
            != g->gsize)
      (void)device_error_handler(dld.tmpdev);
    /** \todo This needs some error handling! */
  }
}

/**
 * We have a cache of a block of DL objects read from disk. Flush any
 * modification to a previous block and read in a new one if necessary.
 */
static Bool sync_disk_cache(DLREF *dlref)
{
  if ( dld.dcache.dlref != dlref ) {
    uint8 *cache = (uint8 *)dld.dcache.lobj;
    size_t off;
    size_t bytes;
    Hq32x2 filepos;

    if ( dld.dcache.writeBack ) {
      DLREF *old = dld.dcache.dlref;

      bytes = sizeof(LISTOBJECT) * old->nobjs;
      off = old->dl.diskoffset + sizeof(size_t);

      Hq32x2FromSize_t(&filepos, off);
      if ( (*theISeekFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, &filepos,
                                       SW_SET)
           == 0
           || (*theIWriteFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, cache,
                                           (int32)bytes)
              != (int32)bytes ) {
        (void)device_error_handler(dld.tmpdev);
        return FALSE;
      }
    }
    dld.dcache.writeBack = FALSE;
    dld.dcache.dlref = dlref;
    off = dlref->dl.diskoffset + sizeof(size_t);
    bytes = dlref->nobjs * sizeof(LISTOBJECT);
    Hq32x2FromSize_t(&filepos, off);

    if ( (*theISeekFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, &filepos, SW_SET)
         == 0
         || (*theIReadFile(dld.tmpdev))(dld.tmpdev, dld.fdesc, cache,
                                        (int32)bytes)
            != (int32)bytes ) {
      (void)device_error_handler(dld.tmpdev);
      return FALSE;
    }
  }
  return TRUE;
}

/**
 * Read the n'th LISTOBJECT from the given DL container
 * from the purge file into memory.
 */
Bool dlref_readfromdisk(DLREF *dlref, uint32 index, LISTOBJECT *lobj)
{
  HQASSERT(index < dlref->nobjs, "Fallen off end of DL object");

  if ( !sync_disk_cache(dlref) )
    return FALSE;

  *lobj = dld.dcache.lobj[index];
  return TRUE;
}

/**
 * Write the n'th LISTOBJECT from the given DL container
 * back to disk as it has changed.
 */
Bool dlref_rewrite(DLREF *dlref, uint32 index, LISTOBJECT *lobj)
{
  if ( !sync_disk_cache(dlref) )
    return FALSE;

  dld.dcache.lobj[index] = *lobj;
  dld.dcache.writeBack = TRUE;
  return TRUE;
}

/**
 * Initialise the DL purging subsystem.
 */
static Bool dlpurge_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  /* Have to put control structures in a non-DL pool as the DL pool gets
   * freed as a single entity at the end of the page. */
  dld.load_size = 128*1024;
  dld.load_space = (void *)mm_alloc(mm_pool_fixed, dld.load_size,
                                    MM_ALLOC_CLASS_LIST_OBJECT);
  if ( dld.load_space == NULL )
    return FAILURE(FALSE);
  if ( !low_mem_handler_register(&dl_purge1_handler) )
    FAILURE_GOTO(fail1);
  if ( !low_mem_handler_register(&dl_purge2_handler) )
    FAILURE_GOTO(fail2);
  return TRUE;

 fail2:
  low_mem_handler_deregister(&dl_purge1_handler);
 fail1:
  mm_free(mm_pool_fixed, dld.load_space, dld.load_size);
  return FALSE;
}

/**
 * Terminate the DL purging subsystem.
 */
static void dlpurge_finish(void)
{
  low_mem_handler_deregister(&dl_purge2_handler);
  low_mem_handler_deregister(&dl_purge1_handler);
  if ( dld.load_space )
    mm_free(mm_pool_fixed, dld.load_space, dld.load_size);
}


static void init_C_globals_dl_purge(void)
{
  init_dld_common() ;
}

void dlpurge_C_globals(core_init_fns *fns)
{
  init_C_globals_dl_purge() ;

  fns->swstart = dlpurge_swstart ;
  fns->finish = dlpurge_finish ;
}

/*
* Log stripped */
