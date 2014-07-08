/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_ref.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to manage references to the DL.
 *
 * Start work on modularising access to display list.
 * Create an opaque 'Display List Reference' object as a
 * replacement for the old public 'LINKOBJECT' structure.
 * As a first cut, the new DLREF is exactly the same as
 * the structure it replces, and the accessor methods are
 * the simplest atomic operations possible.
 *
 * \todo BMJ 14-Oct-08 :  Work in progress - Carry on developing
 * concept of a DL reference.
 */

#include "core.h"
#include "dl_ref.h"
#include "display.h"
#include "dl_bres.h"
#include "dlstate.h"
#include "hdl.h"
#include "devices.h"
#include "dl_purge.h"
#include "dl_free.h"
#include "shadex.h"
#include "monitor.h"
#include "params.h"
#include "swerrors.h"


/**
 * Return actual DL data referenced by the given dlref
 */
LISTOBJECT *dlref_lobj(DLREF *dlref)
{
  HQASSERT(dlref->inMemory && dlref->nobjs == 1, "Corrupt DLREF");
  return dlref->dl.lobj;
}

/**
 * Set the LISTOBJECT pointer inside the given dlref
 */
void dlref_assign(DLREF *dlref, LISTOBJECT *lobj)
{
  HQASSERT(dlref->inMemory && dlref->nobjs == 1, "Corrupt DLREF");
  dlref->dl.lobj = lobj;
}

/**
 * Return the pointer to the next DL reference in the chain
 */
DLREF *dlref_next(DLREF *dlref)
{
  HQASSERT(dlref->inMemory && dlref->nobjs == 1, "Corrupt DLREF");
  return dlref->next;
}

/**
 * Set the pointer to the next DL reference in the chain
 */
void dlref_setnext(DLREF *dlref, DLREF *next)
{
  HQASSERT(dlref->inMemory && dlref->nobjs == 1, "Corrupt DLREF");
  dlref->next = next;
}

/**
 * Allocate a set of dlrefs, joining them all together.
 */
DLREF *alloc_n_dlrefs(size_t n, DL_STATE *page)
{
  DLREF *head;
  void *p;
  size_t size = n * sizeof(DLREF);
  size_t i;

  MM_AP_ALLOC(p, page->dl_ap, size);
  if ( p == NULL ) {
    /* Hacky way of getting low-memory handling. */
    p = dl_alloc(page->dlpools, size, MM_ALLOC_CLASS_DLREF);
    if ( p == NULL )
      return FAILURE(NULL);
  }
  head = p;

  for ( i = 0; i < n; i++ ) {
    head[i].inMemory = (uint16)TRUE;
    head[i].nobjs    = 1;
    head[i].dl.lobj  = NULL;
    head[i].next = NULL;
    if ( i != n - 1 )
      head[i].next = &head[i+1];
  }
  return head;
}

/**
 * free a block of dlrefs
 */
void free_n_dlrefs(DLREF *head, int32 n, mm_pool_t dlpools[N_DL_POOLS])
{
  dl_free(dlpools, head, sizeof(DLREF) * n, MM_ALLOC_CLASS_DLREF);
}

/*
* Log stripped */
