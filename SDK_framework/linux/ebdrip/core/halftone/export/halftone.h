/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!export:halftone.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone external interface.
 */

#ifndef __HALFTONE_H__
#define __HALFTONE_H__ 1

#include "objectt.h"  /* OBJECT, NAMECACHE */
#include "graphict.h" /* GUCR_RASTERSTYLE, GUCR_CHANNEL */
#include "gu_htm.h" /* MODHTONE_REF */
#include "bitbltt.h" /* FORM */
#include "gschcms.h" /* REPRO_TYPE_ */
#include "displayt.h" /* dl_erase_nr */

struct core_init_fns ; /* from SWcore */
struct DL_STATE ; /* from SWv20 */
struct RleColorantMap ; /* from SWv20 */


/** \defgroup halftone Halftoning.
    \ingroup gstate
    \{ */

void halftone_C_globals(struct core_init_fns *fns) ;

/** Used to indicate a screen for all types in cacheType args. */
#define HTTYPE_ALL (HTTYPE_DEFAULT+1)


/** \brief Announce a new input DL to the halftone system.

  \param[in] erasenr  The erase number of the new DL.
  \param[in] preload  Whether screens for the first sheet should be
                      preloaded as they are encountered.
  \return  Success indication.

  This may be called repeatedly with new erase numbers, without calling any
  other \c ht_*_dl() function in between. Until a DL has been handed off, it
  will only be accessed by the interpreter thread.
*/
Bool ht_introduce_dl(dl_erase_nr erasenr, Bool preload);


/** Announce handing off a DL to the backend.

    \param[in] erasenr  The erase number of the DL handed off.
    \param[in] separations  Indicates if there are multiple separations.

    This announces that the DL is ready for output, and may henceforth be
    accessed from threads other than the interpreter thread.
*/
void ht_handoff_dl(dl_erase_nr erasenr, Bool separations);


/** Announce the end of processing a DL to the halftone system.

    \param[in] erasenr  The erase number of the DL being finished.

    Note that this function does not imply anything about previous DLs, which
    may still be in processing.
*/
void ht_retire_dl(dl_erase_nr erasenr);


/** Announce that prior DLs have completed processing.

    \param[in] erasenr  The erase number of the DL being flushed.

    This function is used to indicate that all this DL and all DLs older than
    it have completed processing. All halftone forms and data not used by
    younger pages can be purged.
*/
void ht_flush_dl(dl_erase_nr erasenr);


#ifdef DEBUG_BUILD
void ht_print_screen(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci);
#endif


Bool ht_export_screens(struct DL_STATE *page, GUCR_RASTERSTYLE* hRasterStyle,
                       GUCR_CHANNEL* hf, struct RleColorantMap *map,
                       Bool use_screen_cache_name,
                       Bool screen_dot_centered);

void ht_delete_export_screens(void) ;


Bool ht_duplicatechentry(
  SPOTNO        newspotno,
  HTTYPE        newtype,
  COLORANTINDEX newci,
  NAMECACHE*    newcolorname,
  NAMECACHE*    newhtname,
  SPOTNO        oldspotno,
  HTTYPE        oldtype,
  COLORANTINDEX oldci,
  int32         phasex,
  int32         phasey);

Bool ht_checkifchentry(
  SPOTNO        spotno,
  HTTYPE        type,
  COLORANTINDEX ci,
  int32         phasex,
  int32         phasey);

Bool ht_getExternalScreenName( uint8       *externalName,
                               NAMECACHE   *name,
                               int32       externalNameLength );


/**
 * Looks for an exact matching screen to newspotid starting with
 * guessspotid before trying all others. It returns the matching spotid,
 * else -1.
 */
SPOTNO ht_equivalentchspotid(
  SPOTNO newspotid,
  SPOTNO guessspotid);

/* Similar to ht_equivalentchspotid, but goes not search other colorants.
   The return value is guessspotid, or -1 if not matching. */
SPOTNO ht_checkequivalentchspotids(
  SPOTNO        newspotid,
  SPOTNO        guessspotid,
  Bool          fAny,
  HTTYPE        type,
  COLORANTINDEX ci);


Bool ht_isSpotFuncScreen(
  SPOTNO        spotno,
  HTTYPE        type,
  COLORANTINDEX ci,
  int32         phasex,
  int32         phasey);

SPOTNO ht_mergespotnoentry(
  SPOTNO        oldspotno,
  SPOTNO        newspotno,
  HTTYPE        newtype,
  COLORANTINDEX newci,
  dl_erase_nr   eraseno);

void ht_defer_allocation(
  void);

void ht_resume_allocation(
  SPOTNO            spotno,
  Bool              success);

FORM* ht_patternscreenform(
  SPOTNO        spotno,
  int32*        xx,
  int32*        xy,
  int32*        yx,
  int32*        yy,
  int32*        px,
  int32*        py);


Bool ht_isPattern(
  SPOTNO spotno);


/** Mark a screen used (in at least one tint).

  \param[in] erasenr  The erase number of the page to mark for.
  \param[in] spotno  The spotno of the screen to mark.
  \param[in] type  The object type of the screen to mark.
  \param[in] ci  The colorant index of the screen to mark.

  This marks the screen used for the current input page (given by the
  last \ref ht_introduce_dl).
 */
void ht_setUsed(
  dl_erase_nr erasenr,
  SPOTNO spotno,
  HTTYPE type,
  COLORANTINDEX ci);


/** Mark screens for all colorants of a rasterstyle used (in at least one tint).

  \param[in] erasenr  The erase number of the page to mark for.
  \param[in] spotno  The spotno of the screen to mark.
  \param[in] type  The object type of the screen to mark.
  \param[in] deviceRS  A device rasterstyle to get the colorants from.

  This marks the screens used for the current input page (given by the
  last \ref ht_introduce_dl).
 */
void ht_setUsedDeviceRS(dl_erase_nr       erasenr,
                        SPOTNO            spotno,
                        HTTYPE            type,
                        GUCR_RASTERSTYLE *deviceRS);


/** Mark all tints of a screen used.

  \param[in] erasenr  The erase number of the page to mark for.
  \param[in] spotno  The spotno of the screen to mark.
  \param[in] type  The object type of the screen to mark.
  \param[in] ci  The colorant index of the screen to mark.

  This marks the screen used for the current input page (given by the
  last \ref ht_introduce_dl).
 */
void ht_set_all_levels_used(
  dl_erase_nr       erasenr,
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci);


/** Marks a screen used if the given tints require it.

  \param[in] erasenr  The erase number of the page to mark for.
  \param[in] spotno  The spotno of the screen to mark.
  \param[in] type  The object type of the screen to mark.
  \param[in] ci  The colorant index of the screen to mark.
  \param[in] ntints  Number of tints in \a tints.
  \param[in] tints  An array of tints.
  \param[in] stride  The stride between successive tints in \a tints.
  \param[in] white  The clear tint for this screen.

  This marks the screens used for the current input page (given by the
  last \ref ht_introduce_dl).
*/
Bool ht_keep_screen(
  dl_erase_nr   erasenr,
  SPOTNO        spotno,
  HTTYPE        type,
  COLORANTINDEX ci,
  int32         ntints,
  COLORVALUE   *tints,
  int32         stride,
  COLORVALUE    white);


/** Preallocates forms for the given tints in the given screen.

  \param[in] erasenr  The erase number of the page to allocate for.
  \param[in] spotno  The spotno of the screen to allocate for.
  \param[in] type  The object type of the screen to allocate for.
  \param[in] ci  The colorant index of the screen to allocate for.
  \param[in] ntints  Number of tints in \a tints.
  \param[in] tints  An array of tints.
  \param[in] stride  The stride between successive tints in \a tints.
  \return Whether all tints are now used.

\a stride is typically used by callers who have a component-interleaved
array of colors. E.g., a caller with a CMYK array would specify 4 and call this four times, advancing the tint pointer by one slot each time. */
Bool ht_allocateForm(
  dl_erase_nr       erasenr,
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci,
  int32             ntints,
  COLORVALUE        *tints,
  int32             stride,
  corecontext_t*    context);


/** An iterator for information pertinent to uncalibrated screen detection.
 *
 * To begin, \c handle is set to \c NULL and \c startSpotno is set to the
 * desired value (0 to enumerate all screens) by the client. Thereafter,
 * \c handle should be set to the return of the previous call. \c NULL is
 * returned on termination. N.B. The caller must keep calling until \c NULL is
 * returned (or the cache won't be unlocked).
 *
 * The enumeration returns all screens of the first spotno found, and
 * terminates on the next spotno (only spotno is updated in this case)
 * or at the end of the cache (spotno is set to \c SPOT_NO_INVALID).
 *
 * A side effect is that the reported flag in the physical screen is
 * set to TRUE (as returned in \c screenReported).
 */
void *ht_calibration_screen_info(void *handle,
                                 SPOTNO         startSpotno,
                                 dl_erase_nr    erasenr,
                                 SPOTNO        *spotno,
                                 HTTYPE        *type,
                                 COLORANTINDEX *ci,
                                 Bool          *calibrationWarning,
                                 int32         *levelsUsed,
                                 NAMECACHE    **sfName,
                                 SYSTEMVALUE   *frequency,
                                 NAMECACHE    **sfColor,
                                 uint8         *reportName,
                                 uint32         reportNameLength,
                                 Bool          *screenReported);

void ht_clear_all_screen_reported_flags(dl_erase_nr erasenr);


void ht_setCalibrationWarning(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci,
                              Bool b);
Bool ht_getCalibrationWarning(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci);


Bool updateScreenProgress( void );


/** If names referred to by the screens are about to be restored away,
    remove them (these entries will not be used again). */
void htcache_restore_names(int32 slevel);


/** Change the current screen protected from purging.

  \param[in] old  The spotno to release to be purgeable.
  \param[in] new  The spotno to mark unpurgeable.

  This is called when the screen in the current gstate changes. (When the RIP
boots, \c old is allowed to be invalid, and is ignored.)
 */
void ht_change_non_purgable_screen(SPOTNO old, SPOTNO new);


/** Purges the given screens if unused, or records when they were last used.
 *
 * \param[in] spotno  Spotno of screens to purge.
 * \param[in] dopurge  If FALSE, don't purge, record only.
 * \return  TRUE if the spot was purged from the cache, FALSE otherwise.
 *
 * If dopurge is FALSE, no purge is done, only the recording. This is used when
 * a spot is deinstalled from a gstate, where it was protected by
 * MAX_DL_ERASE_NR. The page default screen is never modified here.
 */
Bool purgehalftones(
  SPOTNO      spotno,       /* I */
  Bool        dopurge);     /* I */


/** Sets the page default screen to spotno and purges the previous default. */
void ht_set_page_default(SPOTNO spotno);


/** Link a given colorant to the screens of another colorant.
 *
 * \param[in] ciOriginal  Colorant index to get screens from.
 * \param[in] ciLink  Colorant index to link up to original screens.
 * \param[in] pncLink  Colorant name for \a ciLink.
 * \param[in] usedLink  Indicates objects in DL used the link colorant;
 *                      retain its usage marks.
 * \return Success indication.
 *
 * \a usedLink is used when not recombining, so colorant use was not
 * intercepted. When \c TRUE, the tint usage data from both screens is
 * combined in the linked screens (approximately, if the numbers of grey
 * levels were different). The usage data affected is that of the
 * current input page (given by the last \ref ht_introduce_dl).
 *
 * This function is for recombine to let it link the screen
 * details of its pseudoseparation (used during recombine) to the final
 * actual colorant, for each spotno in the cache. Also used in separated
 * output, to patch screens created before the real separation has been
 * detected. Returns TRUE if colorants linked in all spotnos containing
 * both, else FALSE.
 *
 * \note Since this modifies the spotnos to differ from their original
 * definitions, these spotnos should not be reused. Callers should call
 * invalidate_gstate_screens() and redo the current screen.
 */
Bool ht_patchSpotnos(
  COLORANTINDEX     ciOriginal,
  COLORANTINDEX     ciLink,
  NAMECACHE         *pncLink,
  Bool              usedLink);


#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
extern Bool installing_hexhds;
#endif

#if defined( ASSERT_BUILD )
/* If \c debug_halftonecache is set, output all screens that have been
   used in the given dl, or later. */
void debug_display_ht_table(dl_erase_nr dl);
#else
#define debug_display_ht_table(dl) EMPTY_STATEMENT()
#endif

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
