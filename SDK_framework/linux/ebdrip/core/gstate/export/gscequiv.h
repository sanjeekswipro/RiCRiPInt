/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gscequiv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to determine separation name -> CMYK mappings.
 */

#ifndef __GSCEQUIV_H__
#define __GSCEQUIV_H__

#include "gscsmpxform.h"  /* EQUIVCOLOR */

Bool gsc_rcbequiv_lookup(GS_COLORinfo *colorInfo,
                         NAMECACHE *sepname,
                         EQUIVCOLOR equivs,
                         int32 *level);

Bool gsc_stdCMYKequiv_lookup(GS_COLORinfo *colorInfo,
                             NAMECACHE *sepname,
                             EQUIVCOLOR equivs,
                             OBJECT *psColorSpace,
                             Bool usePSTintTransform,
                             int32 sepPosition);

Bool gsc_roamRGBequiv_lookup(GS_COLORinfo *colorInfo,
                             NAMECACHE *sepname,
                             EQUIVCOLOR equivs,
                             OBJECT *psColorSpace,
                             Bool usePSTintTransform,
                             int32 sepPosition);

void gsc_rcbequiv_handle_detectop(OBJECT *key, OBJECT *value);

/* CMYK & sRGB equivalent color values are obtained in three scenarios:
 * - recombine requires CMYK values for merging named colorants.
 * - conversion of named colors to process in composite jobs requiring backdrop
 *   rendering (as in PDF transparency, late color management, etc.) requires
 *   CMYK values for merging named colorants at the back end because any tint
 *   transform that was available in interpretation is not available at the back
 *   end.
 * - roam would like sRGB equivalent values for displaying separations of named
 *   colorants. This is not a critical function but desirable.
 *
 * Implement a simple priority level scheme that works for all three scenarios.
 * For recombine, the special feature is that equivalent values may not be
 * available until the end of the page, we have to allow for higher priority
 * methods to supercede the current values.
 
 * This is the desired priority order. The effectiveness of the named color
 * databases is controlled by the order of databases in the various
 * NamedColorOrder resources. 
 *  ---
 * 7. CMYK process colors
 * 6. With HCMS and if available, use Named color database from the Intercept
 *    (or Roam) NamedColorOrder resources. These usually return either XYZ or
 *    CMYK values.
 * 5. Colorants dictionary as the 5th element of a PDF CSA.
 * 4. Transforms from tint transforms in CSAs for composite jobs or PDF's
 *    /SeparationInfo for pre-separated jobs.
 * 3. CMYKCustomColor comments.
 * 2. Freehand spots array.
 * 1. Named color databases from the Recombine NamedColorOrder resource. These
 *    often define, e.g. Red, Green, Blue, and common Illustrator colors.
 */

#define GSC_EQUIV_LVL_PROCESS         (7)
#define GSC_EQUIV_LVL_STD_NAMEDCOLOR  (6)
#define GSC_EQUIV_LVL_COLORANTS_DICT  (5)
#define GSC_EQUIV_LVL_TINT            (4)
#define GSC_EQUIV_LVL_CUSTOMCOLOR     (3)
#define GSC_EQUIV_LVL_FREEHANDSPOTS   (2)
#define GSC_EQUIV_LVL_RCB_NAMEDCOLOR  (1)
#define GSC_EQUIV_LVL_NONEKNOWN       (0)

#endif /* __GSCEQUIV_H__ */


/* Log stripped */
