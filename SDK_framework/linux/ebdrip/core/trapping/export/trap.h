/** \file
 * \ingroup trapping
 *
 * $HopeName: COREtrapping!export:trap.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This header contains the exported API for the core trapping
 * implementation: every hook needed by the rest of ScriptWorks is
 * here. The private stuff for internal use in the trapping code is in
 * trapprv.h.
 */

#ifndef __TRAP_H__
#define __TRAP_H__

#include "surface.h"            /* surface_handle_t */

struct DL_STATE ;         /* SWv20 */
struct render_info_t ;    /* CORErender */
struct core_init_fns ;    /* SWcore */
struct LISTOBJECT ;       /* SWv20 */
struct task_t ;           /* SWmulti */

typedef struct TRAP_SYSTEM_PARAMS {
  /*! \brief The upper limit on the size of each trapping cell in
             kibibytes.

  We aim to allocate this much memory to each cell, although it's pegged
  at three times the maximum mask size as a minimum to avoid too much
  redundancy through overlaps.
  */
  int32 trap_cell_size ;
} TRAP_SYSTEM_PARAMS ;

void trapping_C_globals(struct core_init_fns *fns);

/** \defgroup trapping Trapping.
 * \ingroup core
 */
/** \{ */

/* The identifiers for the known types of trapping engine. */

#define TRAPENGINE_TRAPPRO      0
#define TRAPENGINE_TRAPMASTER   1

/** Overall trapping execution context: private to the trapping
   module. */
typedef struct trap_context_t trap_context_t ;

/** The context used to generate a trap shape: private to the trapping
   module. */
typedef struct trap_RLE_context_t trap_RLE_context_t ;

/** The data which hangs off a RENDER_trap DL object: private to the
   trapping module. */
typedef struct trap_RLE_shape_s trap_RLE_shape_t ;

/** Returns true if the indicated job will be trapped with TrapPro.
 *  \param page The page to query for its trapping status.
 * */
Bool isTrappingActive( /*@notnull@*/ /*@in@*/struct DL_STATE *page ) ;

/** When the pagedevice is deactivated, this gets called with
   pageZonesOnly FALSE to tear down all trapping zones currently in
   effect. In do_beginpage it gets called with pageZonesOnly TRUE to
   get rid of just those zones defined on the page (i.e. it keeps
   those defined in /Install - see RB3 section 6.3.2: "Trapping
   Zones"). */
Bool trapZoneReset(struct DL_STATE *page, Bool page_zones_only) ;

/** The first main entry point for TrapPro: given the final page
   display list, go and prepare it for trapping. The intermediate
   results hang off the private context returned in *tc. */
Bool trapPrepare(
  /*@notnull@*/ /*@in@*/        ps_context_t *pscontext,
  /*@notnull@*/ /*@in@*/        struct DL_STATE *dl_state,
  /*@notnull@*/ /*@out@*/       trap_context_t **ptc,
  /*@in@*/                      struct task_t *trap_complete_task) ;

/** The second entry point for trapping: compositing is complete, so
   now trap the page. */
Bool trapGenerate(
  /*@null@*/ /*@in@*/        trap_context_t *tc ) ;

/** Squirrel away the color values from the given backdrop.
   Assumptions can be made that the image doesn't have any of
   the general case complications: it will always be orthogonal
   and 1-to-1, for instance. */

int32 trapBackdropColorValues( /*@in@*/ /*@dependent@*/ struct render_info_t *ri ,
                               /*@in@*/ struct LISTOBJECT *lobj,
                               /*@in@*/ const dbbox_t *bounds ) ;

/* Called to dispose of the context tc: must be called once
   trapPrepare has succeeded in order to free the considerable
   resources (including an entire memory pool) tied up in the trapping
   context.
   trapContexts are reference counted, and are only freed once all
   references are removed. After calling trapDispose \c *tc is set
   to NULL. */

void trapDispose(
  /*@notnull@*/ /*@in@*/ /*@only@*/        trap_context_t **tc ) ;

/* The PostScript Operators defined in COREtrapping. These prototypes
   aren't strictly necessary for the compiler, but they do stop Splint
   warnings when +export-header is set, and I don't want to turn that
   off because it provides useful information about identifiers which
   should be static. This is especially true since Splint can't check
   the whole program at once in our build environment, so it can't
   check for namespacetightening opportunities the normal way. */

Bool settrapintent_(ps_context_t *pscontext) ;
Bool currenttrapintent_(ps_context_t *pscontext) ;
Bool settrapparams_(ps_context_t *pscontext) ;
Bool currenttrapparams_(ps_context_t *pscontext) ;
Bool settrapzone_(ps_context_t *pscontext) ;

void trapShapeBoundingBox( /*@in@*/ struct trap_RLE_shape_s *shape ,
                           /*@out@*/ dbbox_t *bbox ) ;


Bool trapRequirements(struct DL_STATE *page) ;

/** \} */

/*
* Log stripped */

#endif /* __TRAP_H__ */
