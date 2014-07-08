/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:spotlist.c(EBDSDK_P.1) $
 * $Id: src:spotlist.c,v 1.7.1.1.1.1 2013/12/19 11:25:21 anon Exp $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Maintain a list of spot numbers and object types on the DL to help the final
 * backdrop render handle screen switching efficiently.
 */

#include "core.h"
#include "display.h"
#include "spotlist.h"
#include "dlstate.h"
#include "mm.h"
#include "gu_chan.h"
#include "swerrors.h"
#include "htrender.h"
#include "color.h"

/* HTTYPE goes from zero to HTTYPE_DEFAULT inclusively.
   However this code should never see HTTYPE_DEFAULT. */
#define HTTYPE_NUM (4)
#define HTTYPE_MASK_ALL (15) /* HTTYPE_NUM bits set */

struct SPOTNO_LINK {
  SPOTNO       spotno;
  uint8        objtypes;
  SPOTNO_LINK *next;
};

void spotlist_init(DL_STATE *page, Bool partial)
{
  HQASSERT((pow(2, HTTYPE_NUM) - 1) == HTTYPE_MASK_ALL,
           "HTTYPE_NUM and HTTYPE_MASK_ALL are out of step");

  if ( partial ) {
    /* Pool only partially freed; free spot list manually. */
    while ( page->spotlist != NULL ) {
      SPOTNO_LINK *link = page->spotlist;
      page->spotlist = link->next;
      dl_free(page->dlpools, link, sizeof(SPOTNO_LINK),
              MM_ALLOC_CLASS_SPOTLIST);
    }
  } else
    /* Whole dl pool has been destroyed; just need to clear the list. */
    page->spotlist = NULL;
}

/**
 * Add spot number to the list of known spots to help final backdrop render
 * handle screen switching efficiently.
 */
Bool spotlist_add(DL_STATE *page, SPOTNO spotno, HTTYPE objtype)
{
  SPOTNO_LINK **insert;

  HQASSERT(objtype < HTTYPE_NUM, "Invalid objtype");

  if ( !gucr_halftoning(page->hr) )
    return TRUE;

  /* Look for the spotno already existing on the list or for an insertion point.
     The list is in descending numerical order so it will probably be at the head. */
  for ( insert = &page->spotlist;
        *insert && (*insert)->spotno > spotno;
        insert = &((*insert)->next) ) {
    EMPTY_STATEMENT();
  }

  if ( !(*insert) || (*insert)->spotno != spotno ) {
    SPOTNO_LINK *link = dl_alloc(page->dlpools, sizeof(SPOTNO_LINK),
                                 MM_ALLOC_CLASS_SPOTLIST);
    if ( link == NULL )
      return error_handler(VMERROR);

    link->spotno = spotno;
    link->objtypes = 0;
    link->next = *insert;
    *insert = link;

#if defined( ASSERT_BUILD )
    for ( link = page->spotlist; link->next; link = link->next ) {
      HQASSERT(link->spotno > link->next->spotno,
               "Spot list should be in descending numerical order");
    }
#endif
  }

  /* Track which object types are used for this spot. */
  HQASSERT((*insert)->spotno == spotno, "Must have a spot link now");
  (*insert)->objtypes |= BIT(objtype);

  return TRUE;
}

/** A thread safe version of spotlist_add() for use when adding spots in the
    backend (eg. when compositing). */
Bool spotlist_add_safe(DL_STATE *page, SPOTNO spotno, HTTYPE objtype)
{
  Bool result;
  (void)outputpage_lock();
  result = spotlist_add(page, spotno, objtype);
  outputpage_unlock();
  return result;
}

/**
 * Is there more than one spot being tracked?
 */
Bool spotlist_multi_spots(const DL_STATE *page)
{
  HQASSERT(!gucr_halftoning(page->hr) || page->spotlist,
           "Must have at least one spot number if halftoning");

  return page->spotlist != NULL && page->spotlist->next != NULL;
}

/**
 * Do any of the screens require 16 bit output?
 */
Bool spotlist_out16(const DL_STATE *page, uint32 nComps,
                    COLORANTINDEX *colorants)
{
  if ( gucr_halftoning(page->hr) ) {
    SPOTNO_ITER iter;
    SPOTNO spotno;
    HTTYPE objtype;

    for ( spotlist_iterate_init(&iter, page->spotlist);
          spotlist_iterate(&iter, &spotno, &objtype); ) {
      if ( ht_is16bitLevels(spotno, objtype, nComps, colorants, page->hr) )
        return TRUE;
    }
  } else {
    /* Contone. */
    if ( gucr_valuesPerComponent(page->hr) > 256 )
      return TRUE;
  }

  return FALSE;
}

void spotlist_iterate_init(SPOTNO_ITER *iter, SPOTNO_LINK *spotlist)
{
  HQASSERT(spotlist, "Must have at least one spot number");
  iter->current = spotlist;
  iter->objtype = 0;
}

Bool spotlist_iterate(SPOTNO_ITER *iter, SPOTNO *spotno, HTTYPE *objtype)
{
  if ( !iter->current )
    /* End of spot numbers and object types. */
    return FALSE;

  if ( ht_is_object_based_screen(iter->current->spotno) ) {
    /* Object based screening requires an iteration over objtypes. */
    for ( ; iter->objtype < HTTYPE_NUM; ++iter->objtype ) {
      if ( (iter->current->objtypes & BIT(iter->objtype)) != 0 ) {
        *spotno = iter->current->spotno;
        *objtype = iter->objtype++;
        return TRUE;
      }
    }
    /* Ran out of object types; go to next spot number and recurse. */
    iter->current = iter->current->next;
    iter->objtype = 0;
    return spotlist_iterate(iter, spotno, objtype);
  } else {
    /* Not object based screening: just iterate over spot numbers and
       HTTYPE_DEFAULT means do all object types at once. */
    *spotno = iter->current->spotno;
    *objtype = HTTYPE_DEFAULT;
    iter->current = iter->current->next;
    iter->objtype = 0;
    return TRUE;
  }
}

#if defined( DEBUG_BUILD )
#include "monitor.h"
#include "group.h"
void spotlist_trace(const DL_STATE *page)
{
  if ( (backdrop_render_debug & BR_DEBUG_TRACE) != 0 ) {
    SPOTNO_LINK *link;
    monitorf((uint8*)"List of tracked spot numbers:\n");
    for ( link = page->spotlist; link; link = link->next ) {
      monitorf((uint8*)"  spotno %d (%s screen); objtypes (", link->spotno,
               (link->spotno == page->default_spot_no ? "default" : "job"));
      if ( link->objtypes == HTTYPE_MASK_ALL )
        monitorf((uint8*)"all");
      else {
        uint8 objtype;
        Bool separator = FALSE;
        for ( objtype = 0; objtype < HTTYPE_NUM; ++objtype ) {
          if ( (link->objtypes & BIT(objtype)) != 0 ) {
            monitorf((uint8*)"%s%d", separator ? " " : "", objtype);
            separator = TRUE;
          }
        }
      }
      monitorf((uint8*)")\n");
    }
    monitorf((uint8*)"End of list.\n");
  }
}
#endif

/* Log stripped */
