/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:renderinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation and finalisation for CORErender module.
 */

#include "core.h"
#include "coreinit.h"
#include "objects.h"
#include "swstart.h"
#include "swtrace.h"            /* SW_TRACE_INVALID */
#include "namedef_.h"
#include "ripdebug.h"

#include "bandtablepriv.h"
#include "bitblth.h"
#include "toneblt1.h"
#include "toneblt8.h"
#include "toneblt16.h"
#include "toneblt24.h"
#include "toneblt32.h"
#include "tonebltn.h"
#include "rleblt.h"
#include "halftoneinit.h"
#include "clipblts.h"
#include "shadex.h"
#include "bandtable.h"
#include "surface.h"
#include "renderfn.h"
#include "backdropblt.h"
#include "builtin.h"
#include "spanlist.h"
#include "render.h" /* rendering_prefers_bitmaps */
#include "gu_htm.h"


Bool rendering_prefers_bitmaps(DL_STATE *page)
{
  /* The output is a 1-bit raster, and we're rendering directly to it. */
  return gucr_valuesPerComponent(page->hr) == 2
    && htm_first_halftone_ref(page->eraseno) == NULL;
}


#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
uint32 debug_scanconv = 0 ;
int32 debug_surface = 0 ;
int32 debug_pattern_first = 0 ;
int32 debug_pattern_last = MAXINT32 ;
#endif

static void init_C_globals_renderinit(void)
{
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  debug_scanconv = 0 ;
  debug_surface = 0 ;
  debug_pattern_first = 0 ;
  debug_pattern_last = MAXINT32 ;
#endif
}

/* Clip surfaces must be initialized early, so other surfaces that depend on
 * them can be initialised (e.g trapping). Any such ordering dependency is
 * determined by the order in which core_init_fns for various modules are call.
 */
static Bool render_misc_swinit(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params);
  init_clip_surface();
  init_mask_1();
#ifdef BLIT_BACKDROP
  init_backdropblt() ;
#endif
  return TRUE;
}

/* The fill lock initialised global does not need initialising in a init
 * function since it is only read in the finish function which can only be called
 * if the start function has been called which does the initialisation.
 */
static Bool nfill_lock_init = FALSE;

static Bool render_misc_swstart(SWSTART *params)
{
  register int32 i ;

  for (i = 0; params[i].tag != SWEndTag; i++) {
    if ( params[i].tag == SWBandFactTag ) {
      band_factor = params[i].value.float_value ;
      if ( band_factor < 0.0f )
        band_factor = 0.0f ;
      break;
    }
  }

  multi_rwlock_init(&nfill_lock, NFILL_LOCK_INDEX, SW_TRACE_NFILL_ACQUIRE,
                    SW_TRACE_NFILL_READ_HOLD, SW_TRACE_NFILL_WRITE_HOLD);
  nfill_lock_init = TRUE;

  /* Note that there may be other surfaces initialized in other parts of the rip
   * e.g. trapping. */

#ifdef BLIT_CONTONE_N
  /* Initialise generic pixel-interleaved blitter before any other surfaces,
     because the surface set register adds surfaces to the front of the
     list. The generic surface will be matched after more specific matches
     have failed. */
  init_toneblt_N() ;
#endif
#ifdef BLIT_HALFTONE_MODULAR
  init_toneblt_1() ;
#endif
#if defined(BLIT_CONTONE_8) || defined(BLIT_HALFTONE_MODULAR)
  init_toneblt_8() ;
#endif
#if defined(BLIT_CONTONE_16) || defined(BLIT_HALFTONE_MODULAR)
  init_toneblt_16() ;
#endif
#ifdef BLIT_CONTONE_24
  init_toneblt_24() ;
#endif
#ifdef BLIT_CONTONE_32
  init_toneblt_32() ;
#endif
#if defined(BLIT_HALFTONE_1) || defined(BLIT_HALFTONE_MODULAR)
  init_halftone_1() ;
#endif
#ifdef BLIT_HALFTONE_2
  init_halftone_2() ;
#endif
#ifdef BLIT_HALFTONE_4
  init_halftone_4() ;
#endif

  gouraud_init() ;

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  register_ripvar(NAME_debug_scanconv, OINTEGER, &debug_scanconv);
  register_ripvar(NAME_debug_surface, OINTEGER, &debug_surface);
  register_ripvar(NAME_debug_pattern_first, OINTEGER, &debug_pattern_first);
  register_ripvar(NAME_debug_pattern_last, OINTEGER, &debug_pattern_last);
#endif
  spanlist_unit_test() ;

  return TRUE ;
}

static void render_misc_finish(void)
{
  gouraud_finish() ;

  if (nfill_lock_init) {
    multi_rwlock_finish(&nfill_lock);
    nfill_lock_init = FALSE;
  }
}

/** Initialisation sub-table for render compound. */
static core_init_t render_init[] = {
  CORE_INIT_LOCAL("Render misc", render_misc_swinit, render_misc_swstart, NULL,
                  render_misc_finish),
  CORE_INIT("Band table", bandtable_C_globals),
#if defined(BLIT_RLE_MONO) || defined(BLIT_RLE_COLOR)
  CORE_INIT("RLE resources", rleblt_C_globals),
#endif
  CORE_INIT("Surface list", surface_C_globals)
} ;

static Bool render_swinit(SWSTART *params)
{
  return core_swinit_run(render_init, NUM_ARRAY_ITEMS(render_init), params) ;
}

static Bool render_swstart(SWSTART *params)
{
  return core_swstart_run(render_init, NUM_ARRAY_ITEMS(render_init), params) ;
}

static Bool render_postboot(void)
{
  return core_postboot_run(render_init, NUM_ARRAY_ITEMS(render_init)) ;
}

static void render_finish(void)
{
  core_finish_run(render_init, NUM_ARRAY_ITEMS(render_init)) ;
}


IMPORT_INIT_C_GLOBALS( backdropblt )
IMPORT_INIT_C_GLOBALS( bitblts )
IMPORT_INIT_C_GLOBALS( renderloop )

/** Compound runtime initialisation. */
void render_C_globals(core_init_fns *fns)
{
#ifdef BLIT_BACKDROP
  init_C_globals_backdropblt() ;
#endif
  init_C_globals_bitblts() ;
  init_C_globals_renderinit() ;
  init_C_globals_renderloop();

  fns->swinit = render_swinit ;
  fns->swstart = render_swstart ;
  fns->postboot = render_postboot ;
  fns->finish = render_finish ;

  core_C_globals_run(render_init, NUM_ARRAY_ITEMS(render_init)) ;
}

/* Log stripped */
