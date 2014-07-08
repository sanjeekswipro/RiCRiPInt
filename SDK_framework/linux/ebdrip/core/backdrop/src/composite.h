/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:composite.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Backdrop store code pertaining to the compositing of incoming spans and
 * blocks.
 */

#ifndef __COMPOSITE_H__
#define __COMPOSITE_H__

#include "coalesce.h"
#include "pcl.h"
#include "block.h"
#include "iterator.h"

/**
 * Properties of the source object and the source inputs to compositeColor.
 */
typedef struct Source {
  /* Properties of the source object. */
  int8            alphaIsShape;
  int8            opaque;
  int8            overprint;
  int8            forceProcessKOs;
  uint8           opcode;
  int8            mappedSourceColor;
  int8            drawSource;      /**< True unless source completely transparent and safe to ignore. */
  int8            whiteSourceColor;
  int8            coalescable;     /**< Can we coalesce spans back into blocks? */
  const blit_channel_state_t *overprintFlags; /**< Current set of overprint flags in use. */
  blit_channel_state_t *overprintFlagsBuf; /**< A buffer used when overprint flags need copying. */

  /* Properties set when compositing a backdrop into another. */
  const Backdrop *backdrop;        /**< The source backdrop. */
  int8            canPoach;        /**< Blocks can be 'poached' between backdrops to save work. */

  /* Source inputs to compositeColor. */
  COLORVALUE     *color;           /**< Pointer to the mapped source color ready for compositing. */
  COLORVALUE     *colorBuf;        /**< A buffer used when source color needs padding. */
  COLORVALUE      alpha;           /**< Alpha implicit with source (alpha = opacity * shape). */
  COLORVALUE      opacity;         /**< Opacity is used rather than alpha for fractional shape. */
  COLORVALUE      shape;           /**< For knockout groups with fractional shape. */
  COLORVALUE      constantAlpha;   /**< Constant alpha excluding softmask. */
  COLORINFO      *info;            /**< Pointer into backdrop source table or infoBuf. */
  COLORINFO       infoBuf;         /**< Used for non-backdrop source objects. */
  CCEBlendMode    blendMode;
  CCE            *cce;
} Source;

/**
 * State used to track the mask as spans are composited, and the mask alpha
 * input to compositeColor.
 */
typedef struct Mask {
  /* Mask tracking state. */
  const Backdrop *backdrop;
  BackdropBlock  *block;
  BackdropLine   *line;
  uint32          xi;              /**< Mask xi expected to be loaded next. */
  uint32          runIndex;        /**< Only used when line is RLE. */
  uint8          *data;

  /** Mask alpha input to compositeColor. */
  COLORVALUE      alpha;
} Mask;

/**
 * State used to track the background as spans are composited, and the
 * background inputs to compositeColor.
 */
typedef struct Background {
  /* Background tracking state. */
  BackdropBlock  *block;
  BackdropLine   *line;
  int32           xi;              /**< Background xi currently loaded. */
  uint8          *data;

  /* Background inputs to compositeColor. */
  COLORVALUE     *color;
  COLORVALUE     *colorBuf;
  COLORVALUE      alpha;
  COLORVALUE      groupAlpha;
  COLORVALUE      shape;
  COLORINFO      *info;
  COLORINFO       infoBuf;
} Background;

/**
 * Destination pointers into a table for the compositeColor result.
 */
typedef struct CompositeResult {
  COLORVALUE      *color;
  COLORVALUE      *colorBuf;       /**< Workspace buffer for compositeColor. */
  COLORVALUE      *alpha;
  COLORVALUE      *groupAlpha;
  COLORVALUE      *shape;
  COLORINFO       *info;
} CompositeResult;

/**
 * The final composited backdrop is read block-by-block using the reader.
 */
typedef struct BackdropReader {
  /** The bounds requested for reading (which must be a subset of the
      current region set), and the read bounds for the current block. */
  dbbox_t          readBounds, readBoundsForBlock;

  /** Fields for block iteration. */
  BlockIterator    blockIterator;
  BackdropBlock   *block;
  Bool             moreBlocks;

  /** Flags indicating the start or finish of a read block.  Blocks need to be
      taken off the purgeableBlocks list whilst being read, and then added back
      after the read.  Blocks loaded from disk need to be freed after the read. */
  Bool             blockStart, blockFinish, wasPurgeable, wasLoaded;

  /** xTopLeft and yTopLeft are the x and y coords of the first pixel in the
      current block. */
  dcoord           xTopLeft, yTopLeft;

  /** Current line and data being read. */
  BackdropLine    *line;
  uint8           *data;

  /** The result bounds for the current area being read. */
  dbbox_t          bounds;
} BackdropReader;

/**
 * Stats to monitor compression rates etc.
 */
#if defined(METRICS_BUILD)
typedef struct CompositeMetrics {
  size_t           nUniformBlocks;
  size_t           nRLELines;
  size_t           nRuns;
  size_t           nMapLines;
  size_t           nPixels;
  size_t           nDuplicateEntries;
  size_t           nPoachCandidates;
  size_t           nPoachedBlocks;
} CompositeMetrics;
#endif

typedef struct BackdropReserve BackdropReserve;

/**
 * CompositeContext encompasses all the workspace and state required to
 * composite the backdrops.  It is passed into the backdrop compositing
 * functions and one CompositeContext is used per compositing thread.
 */
struct CompositeContext {
  /** Resources and ids used in bd_requestRegions and bd_regionReleaseAll. */
  uint32           nMaxResources, nFixedResources;
  resource_id_t   *fixedResourceIds;
  void           **fixedResources;

  /** For checking region requests are consistent. */
  dbbox_t          requestBounds;
  Bool             doCompositing;

  /** Base block X, Y for current region set, used for ID generation. */
  uint32           baseX, baseY ;

  /** Width of current region set in blocks, used for ID generation. */
  uint32           xblock ;

  /** Depth of current region set, used for ID generation. */
  uint32           backdropDepth ;

  /** Max number of input colorants in any backdrop. */
  uint32           inCompsMax;

  /** The current page backdrop (there may be several page backdrops if imposing
      pages). */
  Backdrop        *currentPageBackdrop;

  /** Coalesces fill and quad spans back into blocks to make use of line
      repeats. */
  SpanCoalesce    *coalesce;

  /** The union of the region bounds for this composite context and task. */
  dbbox_t          bandBounds;

  /** X and Y page coordinates for the current span.  They are only set by
      bd_compositeSpan and bd_compositeBlock, not bd_compositeBackdrop. */
  dcoord           spanX, spanY;

  /** Anticipated xi and yi values for the current block to reduce setup work
      per span. Values of -1 force the setup work. */
  int32            xiNext, yiNext;

  /** Source, mask, background and PCL pattern state to track inputs to
      compositeColor. */
  Source           source;
  Mask             mask;
  Background       background; /**< initial background */
  Background       backgroundForShape; /**< immediate background for fractional shape */
  PclBDState      *pcl;

  /** Destination pointers into a table for the compositeColor result. */
  CompositeResult  result;

  /** Specialised functions to load inputs to compositeColor. */
  uint32         (*loadRun)(CompositeContext*, const Backdrop*, uint32, uint32);

  /** Specialised compositeColor functions to avoid tests. */
  Bool           (*compositeColor)(CompositeContext*, const Backdrop*);

  /** The final composited backdrop is read block-by-block using the reader. */
  BackdropReader   reader;

  /** Temporary grab of memory to ensure there will be enough to composite. */
  BackdropReserve   *backdropReserve;

#if defined(METRICS_BUILD)
  /** Stats to monitor compression rates etc. */
  CompositeMetrics   metrics;
#endif
};

void bd_contextGlobals(void);
Bool bd_contextInit(void);
void bd_contextFinish(void);
Bool bd_contextPoolGet(resource_requirement_t *req, uint32 nMaxResources,
                       uint32 inCompsMax, uint32 width,
                       size_t backdropReserveSize);
uint32 bd_loadRunNoOp(CompositeContext *context,
                      const Backdrop *backdrop,
                      uint32 xi, uint32 runLen);
uint32 bd_loadRunBackground(CompositeContext *context,
                            const Backdrop *backdrop,
                            uint32 xi, uint32 runLen);
uint32 bd_loadRunMaskAndBackground(CompositeContext *context,
                                   const Backdrop *backdrop,
                                   uint32 xi, uint32 runLen);
uint32 bd_loadRunFractionalShape(CompositeContext *context,
                                 const Backdrop *backdrop,
                                 uint32 xi, uint32 runLen);
uint32 bd_loadRunPCLPatternAndBackground(CompositeContext *context,
                                         const Backdrop *backdrop,
                                         uint32 xi, uint32 runLen);

#endif /* protection for multiple inclusion */

/* Log stripped */
