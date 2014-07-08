/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swtimelines.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Defines the core's externally visible timelines.
 */

#ifndef __SWTIMELINES_H__
#define __SWTIMELINES_H__ (1)

#ifdef __cplusplus
extern "C" {
#endif

#include "timelineapi.h"

#define SWTLT(x_) (TL_CORE + (x_))

/** \brief Core timeline types.

    These are used as indices in the core, so we keep the range tight. However,
    they are part of the core interface, so the numbering may not be changed.
 */
enum {
  SWTLT_CORE               = SWTLT(0),  /* A core RIP instance */
  SWTLT_FILE_INTERPRET     = SWTLT(1),  /* Reading a file */
  SWTLT_JOB_STREAM         = SWTLT(2),  /* A single job stream */
  SWTLT_JOB_CONFIG         = SWTLT(3),  /* Job configuration */
  SWTLT_JOB                = SWTLT(4),  /* A running job */
  SWTLT_INTERPRET_PAGE     = SWTLT(5),  /* The interpretation stage */
  SWTLT_COUNTING_PAGES     = SWTLT(6),  /* Counting pages */
  SWTLT_SCANNING_PAGES     = SWTLT(7),  /* Analysing HVD pages. */
  SWTLT_HALFTONE_USAGE     = SWTLT(8),  /* Reporting halftone tiles */
  SWTLT_HALFTONE_CACHING   = SWTLT(9),  /* Pre-generating halftone tiles */
  SWTLT_RECOMBINE_PAGE     = SWTLT(10), /* The page recombining stage */
  SWTLT_TRAP_PREPARATION   = SWTLT(11), /* Preparing for trapping */
  SWTLT_TRAP_GENERATION    = SWTLT(12), /* Generating trapped objects */
  SWTLT_TRAP_IMAGES        = SWTLT(13), /* Internal image trapping */
  SWTLT_RENDER_PREPARE     = SWTLT(14), /* The rendering preparation stage */
  SWTLT_COMPOSITE_PAGE     = SWTLT(15), /* The transparency compositing stage */
  SWTLT_RENDER_PAGE        = SWTLT(16), /* Rendering a separation */
  SWTLT_RENDER_PARTIAL     = SWTLT(17), /* Partial painting a page */
  SWTLT_RENDER_SEPARATION  = SWTLT(18), /* Rendering a separation */
  /** Pagebuffer device timeline. This is actually owned by the PGB device
      outside the core, but the core is a consumer of it, so needs to know
      the type. Progress is measured in lines, continuous across all frames
      in a separation, in the original (untrimmed) line coordinate space. The
      core requires the progress update event to arrive in order to release
      bands. The PGB device is responsible for creating it, updating
      progress, disposing of it, and providing debug information about it. */
  SWTLT_PGB                = SWTLT(19),
  SWTLT_PRESCANNING_PAGES  = SWTLT(20), /* Pre-scanning HVD pages. */
  SWTLT_POSTSCANNING_PAGES = SWTLT(21), /* Post-scanning HVD pages. */
  SWTLT_RENDER_CACHE       = SWTLT(22), /* Rendering a page into the
                                           RIP's cache (i.e. not for
                                           immediate output) */
  TL_CORE_END               /* End marker for core timeline types */
};

/** \brief Context id to find render page number from page render timeline.
 *
 * The render page number starts at 1 and increments with each page rendered.
 * With Retained Raster the render page number may be repeated, and some render
 * page numbers may not be present if a page has no unique content.
 */
#define SW_RENDER_PAGE_NUMBER_CTXT  (1)

#undef SWTLT

#ifdef __cplusplus
}
#endif

#endif /* !__SWTIMELINES_H__ */

