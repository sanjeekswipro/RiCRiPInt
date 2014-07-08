/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:htpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone module private interfaces.
 */

#ifndef __HTPRIV_H__
#define __HTPRIV_H__ 1

#include "color.h"    /* HT_TRANSFORM_INFO */
#include "htrender.h" /* ht_params_t */
#include "mps.h"
#include "objecth.h"  /* NAMECACHE */
#include "gu_chan.h"  /* GUCR_RASTERSTYLE */
#include "gu_htm.h"   /* MODHTONE_REF */
#include "mlock.h"
#include "displayt.h" /* dl_erase_nr */


#define LONGESTSCREENNAME 256

/* Defs for various caches of info for colorant indices for one spotno.
 * The offset is there to cope with COLORANTINDEX_ALL & COLORANTINDEX_NONE.
 */
#define HT_MAX_COLORANT_CACHE     (30)
#define HT_OFFSET_COLORANT_CACHE  (2)

/* Test for valid colorant indices in some functions. This will include
 * COLORANTINDEX_ALL, COLORANTINDEX_NONE, and a set of real indices. Each of
 * these indices must have a slot in the LAST_SPOT_CACHE arrays.
 */
#define HT_CI_VALID(ci)   (ci >= -HT_OFFSET_COLORANT_CACHE && \
                           COLORANTINDEX_NONE >= -HT_OFFSET_COLORANT_CACHE && \
                           COLORANTINDEX_ALL >= -HT_OFFSET_COLORANT_CACHE)


typedef struct LISTCHALFTONE LISTCHALFTONE;
typedef struct CHALFTONE CHALFTONE;


extern Bool debug_halftonecache;


/** Oldest DL that halftone information is retained for. */
extern dl_erase_nr oldest_dl;

/** The erase number of the DL being built. */
extern dl_erase_nr input_dl;


/** The erase number of the DL the screen was last used on. */
dl_erase_nr chalftone_last_used(CHALFTONE *pch);


extern multi_mutex_t ht_cache_mutex;


/* cache access functions from htcache.c */

HT_TRANSFORM_INFO *ht_getCachedTransformInfo(SPOTNO spotno, HTTYPE type,
                                             Bool halftoning,
                                             int32 contone_levels);

void ht_invalidate_transformInfo(HT_TRANSFORM_INFO *transformInfo);

LISTCHALFTONE* ht_getlistch(SPOTNO nSpotId, HTTYPE type, COLORANTINDEX ci,
                            corecontext_t *corecontext);

LISTCHALFTONE* ht_getlistch_exact(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci);

CHALFTONE* ht_getch(SPOTNO nSpotId, HTTYPE type, COLORANTINDEX ci);

void ht_cacheInsert(LISTCHALFTONE* plistch);

void ht_cacheRemove(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci);


typedef struct ht_cacheIterator {
  LISTCHALFTONE *curr;
  size_t slot;
  LISTCHALFTONE *last_none[HTTYPE_DEFAULT+1];
  uint8 next_none_type;
  SPOTNO last_spotno;
  SPOTNO start_spotno;
  COLORANTINDEX select_ci; /* COLORANTINDEX_UNKNOWN means all cis */
  dl_erase_nr oldest_dl;
} ht_cacheIterator;


/** \brief Initialize an iteration over halftone cache entries.

 \param[in,out] iter The iteration state to be initialized.

 \param[in] oldest_dl  Erase number of oldest DL to iterate over. Use
    INVALID_DL to iterate over the whole cache (even the ones not used
    on any page).

 \param[in] start_spotno Spotno to start from.  Must be present in the
   cache, or equal SPOT_NO_INVALID, meaning don't care.

 \param[in] select_ci If not \c COLORANTINDEX_UNKNOWN, select only
   entries for that colorant index, or the default (COLORANTINDEX_NONE)
   if no exact match.

  Once this has been called, ht_iterChentryEnd() must be called.
 */
void ht_iterChentryBegin(ht_cacheIterator *iter,
                         dl_erase_nr oldest_dl,
                         SPOTNO start_spotno,
                         COLORANTINDEX select_ci);

Bool ht_iterChentryNext(ht_cacheIterator *iter,
                        LISTCHALFTONE **listch_out,
                        CHALFTONE **ch_out,
                        MODHTONE_REF **mhtref_out,
                        SPOTNO *spotno_out,
                        HTTYPE *type_out,
                        COLORANTINDEX *ci_out);

void ht_iterChentryEnd(ht_cacheIterator *iter);


LISTCHALFTONE *ht_listchAlloc(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci,
                              CHALFTONE *pch,
                              NAMECACHE *sfcolor, NAMECACHE *htname,
                              MODHTONE_REF *mhtref,
                              int32 phasex, int32 phasey);

void safe_free_listchalftone(
  LISTCHALFTONE*  plistch,   /* I */
  Bool            tidyrefs   /* I */ );


CHALFTONE *ht_listchChalftone(LISTCHALFTONE *listch);

/** Return the modular screen from a LISTCHALFTONE. */
MODHTONE_REF *ht_listchMhtref(LISTCHALFTONE *listch);

Bool ht_listchIsModular(LISTCHALFTONE* listch);
const uint8 *ht_listchModularName(LISTCHALFTONE* listch);

NAMECACHE *ht_listchHalfToneName(LISTCHALFTONE* listch);

NAMECACHE *ht_listchSFColor(LISTCHALFTONE* listch);

Bool ht_listchCalibrationWarning(LISTCHALFTONE* listch);

dl_erase_nr ht_listch_last_used_dl(LISTCHALFTONE* listch);

void ht_listchCXY(LISTCHALFTONE* listch, int32* cx, int32* cy);

void ht_listchPhaseXY(LISTCHALFTONE *listch, int32 *px, int32 *py);


Bool initHalfToneCache(void);

void finishHalfToneCache(void);

/* CHALFTONE utilities from halftone.c */

void addRefChPtr(CHALFTONE *chptr);
void releaseChPtr(CHALFTONE *chptr);


/** \brief Transfer usage levels from one screen to another.

  \param[in] chSrc  The screen to transfer from.
  \param[in] chDst  The screen to transfer to.

  Only the count, since notones and formsize could be different. Because
  notones could be different, the count is scaled to approximate what it
  would have been on the destination screen. Because this is only used
  by patching, it's implicitly a front-end operation. */
void ht_transferLevels(CHALFTONE *chSrc, CHALFTONE *chDst);


Bool ht_chIsThreshold(CHALFTONE* pch);
Bool ht_chIsNormalThreshold(CHALFTONE* pch);
Bool ht_chHasFormclass(CHALFTONE* pch);

/** Used after preload, indicates if any level of the screen is used.

  \param[in] pch  \c CHALFTONE of the screen to test.
  \return Is the screen used.

  Note that bi-level screens never have any levels used. */
Bool ht_chIsUsedForRendering(CHALFTONE* pch);

uint16 ht_chNoTones(CHALFTONE* pch);
Bool ht_chAccurate(CHALFTONE* pch);
SYSTEMVALUE ht_chAngle(CHALFTONE* pch);

void chalftone_scan(mps_ss_t ss, CHALFTONE *chptr);

void chalftone_restore_names(CHALFTONE *chptr, int32 slevel);


/* Various halftone utilities from halftone.c */

void rawstorechglobals(ht_params_t *ht_params, CHALFTONE *chptr);

void ht_dot_shape_string(NAMECACHE *htname,
                         NAMECACHE *sfname,
                         Bool intToExt ,
                         uint8 nambuf[LONGESTSCREENNAME * 2]) ;

void ht_converge_phase(CHALFTONE *chptr, int32 *cx_io, int32 *cy_io,
                       int32 *px_out, int32 *py_out);

void mknamestr(uint8 *buff, NAMECACHE *name);

int32 ht_bit_depth_shift(GUCR_RASTERSTYLE *rs);


/** Test if screens match in all fields that affect dot positions,
   cf. \c init_form().  \a ch2 is adjusted by the \a depth_shift given. */
Bool ht_equivalent_render_params(CHALFTONE *ch1, CHALFTONE *ch2,
                                 uint8 depth_shift, SYSTEMVALUE orientation,
                                 Bool adjustableRatio);

/* Checks if there's an equivalent entry in the cache (memory or disk).
   Not used for modular halftones.
   Returns 0 if none found, -1 on error, 1 on success. */
int ht_equivalent_ch_pre_cacheentry(
  corecontext_t *context,
  SPOTNO        spotno,
  HTTYPE        type,
  COLORANTINDEX ci,
  CHALFTONE*    chptr,
  uint8         depth_shift,
  uint8         default_depth_shift,
  SYSTEMVALUE   orientation,
  NAMECACHE*    htname,
  NAMECACHE*    sfcolor,
  NAMECACHE*    alternativeName,
  NAMECACHE*    alternativeColor,
  HTTYPE        cacheType,
  int32         detail_name,
  int32         detail_index,
  int32         phasex,
  int32         phasey);


Bool ht_insertchentry(
  corecontext_t *context,
  SPOTNO        spotno,
  HTTYPE        objtype,
  COLORANTINDEX ci,
  CHALFTONE*    chptr,
  uint8         depth_shift,
  uint8         default_depth_shift,
  Bool          needs_depth_adjustment,
  SYSTEMVALUE   orientation,
  int32         patterngraylevel,
  NAMECACHE*    htname,
  NAMECACHE*    sfcolor,
  int32         detail_name,
  int32         detail_index,
  Bool          cachetodisk,
  NAMECACHE *   nmAlternativeName,
  NAMECACHE *   nmAlternativeColor,
  HTTYPE        cacheType,
  int32         phasex,
  int32         phasey);


/* Halftone disk cache interface from htdisk.c */

int32 loadHalftoneFromDisk(corecontext_t *context,
                           SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                           CHALFTONE *ch_template,
                           uint8 depth_shift, uint8 default_depth_shift,
                           SYSTEMVALUE orientation,
                           NAMECACHE *htname,
                           NAMECACHE *sfcolor,
                           NAMECACHE *alternativeName,
                           NAMECACHE *alternativeColor,
                           HTTYPE cacheType,
                           int32 detail_name, int32 detail_index,
                           int32 phasex, int32 phasey);

Bool saveHalftoneToDisk( corecontext_t *context,
                         CHALFTONE *chptr,
                         NAMECACHE *cacheName, NAMECACHE *sfcolor, uint8 type,
                         int32 detail_name, int32 detail_index );


#endif /* protection for multiple inclusion */

/* Log stripped */
