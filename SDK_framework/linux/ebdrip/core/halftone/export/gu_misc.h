/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!export:gu_misc.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone creation functions for various types of halftones
 */

#ifndef __GU_MISC_H__
#define __GU_MISC_H__

#include "dl_color.h"     /* COLORANTINDEX */
#include "graphict.h"     /* GS_COLORinfo */
#include "objectt.h"      /* OBJECT */

/** Return codes for \c getdotshapeoverride. */
enum {
  SCREEN_OVERRIDE_NONE,
  SCREEN_OVERRIDE_SETHALFTONE,
  SCREEN_OVERRIDE_SPOTFUNCTION
} ;

/** Get the override type, name, and data. */
int32 getdotshapeoverride(corecontext_t *context,
                          NAMECACHE **ret_override_name , OBJECT **ret_override_data ) ;


/** Set up XY tables for type 3 and 6 halftones, and insert in the cache. */
Bool newthresholds(corecontext_t *context,
                   int32 w, int32 h, int32 depth,
                   OBJECT *stro, Bool invert,
                   SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                   NAMECACHE *htname ,
                   NAMECACHE *htcolor ,
                   NAMECACHE *alternativeName,
                   NAMECACHE *alternativeColor,
                   HTTYPE cacheType,
                   Bool complete,
                   Bool extragrays,
                   OBJECT *newfileo,
                   int32 encryptType,
                   int32 encryptLength,
                   GS_COLORinfo *colorInfo);

/** Set up XY tables for type 10 halftones (supplement 2015). */
Bool newXYthresholds(corecontext_t *context,
                     int32 x, int32 y, int32 depth,
                     OBJECT *stro, Bool invert,
                     SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                     NAMECACHE *htname ,
                     NAMECACHE *htcolor ,
                     NAMECACHE *alternativeName,
                     NAMECACHE *alternativeColor,
                     HTTYPE cacheType,
                     Bool complete,
                     Bool extragrays,
                     OBJECT *newfileo,
                     int32 encryptType,
                     int32 encryptLength,
                     GS_COLORinfo *colorInfo);

/** Set up XY tables for type 16 halftones (supplement 3010). */
Bool new16thresholds(corecontext_t *context,
                     int32 width, int32 height, int32 depth,
                     OBJECT *stro, Bool invert,
                     SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                     NAMECACHE *htname ,
                     NAMECACHE *htcolor ,
                     NAMECACHE *alternativeName,
                     NAMECACHE *alternativeColor,
                     HTTYPE cacheType,
                     Bool complete,
                     Bool extragrays,
                     Bool limitlevels,
                     OBJECT *newfileo,
                     int32 width2, int32 height2,
                     int32 encryptType,
                     int32 encryptLength,
                     GS_COLORinfo *colorInfo);

/** Set up XY tables for normal (freq, angle, spotfunction) screens. */
Bool newhalftones(corecontext_t *context,
                  SYSTEMVALUE *usersfreqv ,
                  SYSTEMVALUE *usersanglv ,
                  OBJECT *proco ,
                  SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                  NAMECACHE *htname ,
                  NAMECACHE *sfcolor ,
                  NAMECACHE *alternativeName,
                  NAMECACHE *alternativeColor,
                  HTTYPE cacheType,
                  Bool accurate ,
                  Bool accurateInHalftoneDict ,
                  Bool doubleScreens ,
                  Bool requireActualAngleFreq ,
                  Bool *tellMeIfPatternScreen,
                  Bool docolordetection,
                  Bool overridefreq,
                  Bool overrideangle,
                  Bool complete,
                  int32 phasex,
                  int32 phasey,
                  OBJECT * poAdjustScreen,
                  GS_COLORinfo *colorInfo);

/** Set up a modular screen.
  * Also sets up XY tables for a ghost screen to underlay it.
  */
Bool newModularHalftone(SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                        OBJECT    *modname ,
                        NAMECACHE *htname ,
                        NAMECACHE *htcolor ,
                        OBJECT    *htdict , /*< Can be the subordinate to a type 5 */
                        GS_COLORinfo *colorInfo ) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
