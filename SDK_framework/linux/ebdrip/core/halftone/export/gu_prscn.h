/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!export:gu_prscn.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Precision screening API
 */

#ifndef __GU_PRSCN_H__
#define __GU_PRSCN_H__

#include "objectt.h"  /* OBJECT, NAMECACHE */


/** Search for an HPS screen set, returning the number of cell repeats
    required to achieve the optimised angles.

    \return -1 on memory exhaustion, 0 if out of tolerance, or the number
      of cell repeats. Note that \c error_handler() is NOT called on error,
      the caller must do so.
 */
int32 accurateCellMultiple(corecontext_t *context,
                           SYSTEMVALUE uFreq, SYSTEMVALUE uAngle,
                           SYSTEMVALUE *rifreq, SYSTEMVALUE *riangle,
                           SYSTEMVALUE *rofreq, SYSTEMVALUE *roangle,
                           SYSTEMVALUE *rdfreq, Bool *optimized_angle,
                           Bool singlecell,
                           int32 willzeroadjust,
                           NAMECACHE *spotfun);

/** Start reporting HPS screen generation. */
void report_screen_start( corecontext_t *context,
                          NAMECACHE *htname ,
                          NAMECACHE *sfname ,
                          SYSTEMVALUE freqv ,
                          SYSTEMVALUE anglv ) ;

/** Report searching for HPS screen set. */
void report_screen_search(corecontext_t *context, SYSTEMVALUE freqv, NAMECACHE *spotfun);

/** End reporting HPS screen generation. */
void report_screen_end(corecontext_t *context,
                       NAMECACHE *htname,
                       NAMECACHE *sfname,
                       SYSTEMVALUE freqv,
                       SYSTEMVALUE anglv,
                       SYSTEMVALUE devfreq,
                       SYSTEMVALUE freqerr,
                       SYSTEMVALUE anglerr);

#endif /* protection for multiple inclusion */


/* Log stripped */
