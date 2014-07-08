/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_bres.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Scan-conversion API
 */

#ifndef __DL_BRES_H__
#define __DL_BRES_H__


#include "ndisplay.h"
#include "displayt.h"
#include "graphict.h"
#include "mm.h"

struct render_info_t;
struct DL_STATE ;

/*
 * External things defined in dl_bres.c
 */

/** Opaque type definition for edge table variables. */
typedef struct nfill_builder_t nfill_builder_t;

size_t nfill_builder_size(void);

/*
 * Two APIs for creating NFILL objects from paths.
 * The first is make_nfill() which does all the work atomically.
 * Second is the series of calls
 *   start_nfill()
 *   addpath2_nfill()
 *   addpath2_nfill()
 *   ...
 *   complete_nfill()
 *   end_nfill()
 * which can be used for creating a single NFILL form any number of paths.
 */
Bool make_nfill(DL_STATE *page, PATHLIST *thepath, uint32 flags,
                NFILLOBJECT **nfillptr);

void start_nfill(DL_STATE *page, nfill_builder_t *nbuilder,
                 size_t size, uint32 flags);
Bool addpath2_nfill(nfill_builder_t *nbuild, PATHLIST *path);
Bool complete_nfill(nfill_builder_t *nbuild, NFILLOBJECT **nf);
void end_nfill(nfill_builder_t *nbuilder);

size_t sizeof_nfill(const NFILLOBJECT *nfill);
void free_nfill(NFILLOBJECT *nfill, mm_pool_t *pools);

Bool add2dl_nfill(DL_STATE *page, int32 type, NFILLOBJECT *nfill);

NFILLOBJECT *nfill_preallocate(DL_STATE *page, size_t nbresssize,
                               int32 nthreads, int32 type, uint8 converter);

/*
 * Utility functions for querying and manipulating NFILL objects
 */
void bbox_nfill(NFILLOBJECT *nfill, const dbbox_t *clip, dbbox_t *bbox,
                Bool *clipped);
void swapxy_nfill(NFILLOBJECT *nfill);
void preset_nfill(NFILLOBJECT *nfill);
void repair_nfill(NFILLOBJECT *nfill, dcoord y1clip);

/** \brief Copy an NFILL, usually from stack memory, into DL memory. */
NFILLOBJECT *nfill_copy(struct DL_STATE *page, const NFILLOBJECT *nfill) ;

size_t sizeof_nbress(const NBRESS *nbress) ;

/** \brief Append the chains from an NFILL, usually from stack memory, into
    preallocated DL memory. */
void nfill_append(struct DL_STATE *page, NFILLOBJECT *out,
                  const NFILLOBJECT *in) ;

/** \brief Convert a quad DL object to an nfill, using the NBRESS workspace
    provided. */
void quad_to_nfill(LISTOBJECT *lobj, NFILLOBJECT *nfill, NBRESS t1[4]);

/** \brief Convert a rectangle (represented by a bbox) to an nfill, using the
    NBRESS workspace provided. */
void rect_to_nfill(const dbbox_t *bbox, NFILLOBJECT *nfill, NBRESS t1[2]);

/** \brief Predicate to test if a quad represents a rectangle. */
Bool quad_is_rectangle(uint32 quad) ;

/** \brief Predicate to test if a quad represents a single point. */
Bool quad_is_point(uint32 quad) ;

/** \brief Predicate to test if a quad represents a hairline (two-point
    closed figure). */
Bool quad_is_line(uint32 quad) ;

/** \brief Predicate to test if a quad represents a triangle. */
Bool quad_is_triangle(uint32 quad) ;

/** Repair NFILL structure to continue rendering. If a previous band has
   used the NFILL, we may be able to continue from where it left off, or at
   least do a shorter update. If replicating in a pattern, we generally want
   to restart the thread (to start at the top of the pattern cell), but we
   have to be careful about replicated clipping, which may need adjusting. */
#define REPAIR_NFILL( _nfill, _y1clip ) MACRO_START             \
  if ( (_nfill)->nexty != (_y1clip) ) {                         \
    if ( (_nfill)->nexty > (_y1clip) ) {                        \
      /* Thread may have stepped beyond current y clip coord */ \
      /* or may simply start below it */                        \
      if ( (_nfill)->nexty != (_nfill)->y1clip ) {              \
        /* Yes, the thread has been stepped */                  \
        /* so preset and repair if necessary */                 \
        preset_nfill( _nfill ) ;                               \
        if ( (_nfill)->nexty < (_y1clip) )                      \
          repair_nfill( _nfill, (_y1clip) );              \
      }                                                         \
    }                                                           \
    else {                                                      \
      repair_nfill( _nfill, (_y1clip) );                  \
    }                                                           \
  }                                                             \
MACRO_END

/** Reset an NFILL structure by setting to its initial limits, then updating
    to the current limits. This is used for replicated fills because they will
    need resetting for each band. */
#define REPAIR_REPLICATED_NFILL( _nfill, _y1clip ) MACRO_START \
  preset_nfill( _nfill );                                     \
  if ( (_nfill)->nexty < (_y1clip) )                           \
    repair_nfill( (_nfill), (_y1clip) );                 \
MACRO_END

/**
 * The NFILL_IS*** bits are mutually exclusive, but a ISCHAR may also have
 * the XYSWAP flag set. If you update these flags check the assert at the
 * start of make_nfill().
 */
enum {
  NFILL_ISSTRK = 0x01,    /**< nfill originated from a stroke */
  NFILL_ISRECT = 0x02,    /**< nfill originated from a rectangle */
  NFILL_ISCHAR = 0x04,    /**< nfill originated from a char */
  NFILL_ISFILL = 0x08,    /**< nfill originated from a fill */
  NFILL_ISCLIP = 0x10,    /**< nfill originated from a clip */
  NFILL_XYSWAP = 0x40     /**< char X/Y may be swapped */
} ;

#endif /* protection for multiple inclusion */

/* Log stripped */
