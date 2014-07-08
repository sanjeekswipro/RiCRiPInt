#ifndef __JOBMETRICS_H__
#define __JOBMETRICS_H__

/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:jobmetrics.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to core job metrics.
 */

#include "metrics.h"
#include "displayt.h" /* N_RENDER_OPCODES */
#include "objects.h"  /* OBJECT */

struct core_init_fns ;

/* The boot functions are available in all builds; in non-metrics variants
   they are stubbed out. */

/** \brief Initialise the metrics system. */
void sw_jobmetrics_C_globals(struct core_init_fns *fns) ;

#ifdef METRICS_BUILD
/** Statistics captured for a display list. */
typedef struct dl_metrics_t {
  uint32 opcodes[N_RENDER_OPCODES] ; /**< Object types on DL. */

  /** Statistics for the DL State Store. */
  struct {
    uint32 setgCount;
    uint32 stateCount;

    uint32 nfillCount;
    uint32 clipCount;
    uint32 gstagCount;
    uint32 patternCount;
    uint32 patternshapeCount;
    uint32 softMaskCount;
    uint32 latecolorCount;
    uint32 transparencyCount;
    uint32 hdlCount;
    uint32 pclCount;
  } store;

  /** Blend modes; the values are the number of objects added to the display
   * list using that blend mode. */
  struct {
    /* A special blend mode; 'Normal' with a source alpha of 1.0. */
    uint32 opaqueNormal;
    uint32 normal;
    uint32 multiply;
    uint32 screen;
    uint32 overlay;
    uint32 softLight;
    uint32 hardLight;
    uint32 colorDodge;
    uint32 colorBurn;
    uint32 darken;
    uint32 lighten;
    uint32 difference;
    uint32 exclusion;
    uint32 hue;
    uint32 saturation;
    uint32 color;
    uint32 luminosity;
  } blendmodes;

  struct {
    struct {
      uint32 rgb;
      uint32 cmyk;
      uint32 gray;
      uint32 other;
    } colorspaces ;
    uint32 orthogonal ;
    uint32 rotated ;
    uint32 degenerate ;
  } images ;

  struct {
    uint32 tiles_cached ;
    uint32 tile_memory ;
    uint32 possible_shapes ;
    uint32 actual_shapes ;
    sw_metric_histogram_t(25) times_used ;
  } imagetiles ;

  struct {
    uint32 groups;
    uint32 groupsEliminated;
    uint32 groupsStoringShape;
  } groups ;

  struct {
    uint32 patternedObjects;
    /** PCL ROPs used on DL. */
    sw_metric_hashtable *rops ;
  } pcl;

  /** Compositing regions for this DL. */
  struct {
    uint32 total;
    uint32 backdropRendered;
    uint32 directRendered;
  } regions ;

  /** DL objects and their transparency. */
  sw_metric_hashtable *trans_attributes;
} dl_metrics_t ;

/** \brief Accessor for DL store stats for interpreter.
 *
 * \todo ajcd 2010-11-30: We may want to make this referent to the core
 * context, and put a current stats pointer into the context, so tasks can
 * update different stats simultaneously.
 */
dl_metrics_t *dl_metrics(void) ;

/** Update transparency metrics. This function is implemented in the
    transparency state file, but exported here, because it's only useful
    in metrics counting. */
void updateBlendModeMetrics(TranAttrib *tranAttrib, dl_metrics_t *dlmetrics) ;

Bool populate_dl_transparency_metrics_hashtable(void) ;

/* Utility function to check if the color space is ICC based. */
Bool dl_transparency_is_icc_colorspace(OBJECT *colorSpace) ;

/** ROP metrics bits, ORed into the hashtable key. */
enum {
  ROP_METRIC_ORIGINAL = 0,
  ROP_METRIC_BACKDROP = 256,
  ROP_METRIC_DIRECT = 512,
  ROP_METRIC_S_TRANS = 1024,
  ROP_METRIC_P_TRANS = 2048,
  ROP_METRIC_F_WHITE = 4096,
  ROP_METRIC_F_BLACK = 8192
} ;

#endif /* METRICS_BUILD */

/* ============================================================================
* Log stripped */
#endif
