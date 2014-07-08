/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:devops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Page device operations.
 */

#ifndef __DEVOPS_H__
#define __DEVOPS_H__


#include "bitbltt.h"  /* FORM */
#include "display.h"  /* LateColorAttrib */
#include "matrix.h"   /* OMATRIX */
#include "objecth.h"  /* OBJECT */
#include "graphict.h" /* GSTATE */

/* exported types */
typedef struct {
  Bool new_is_pagedevice ;
  Bool old_is_pagedevice ;
  Bool different_devices ;
  enum {
    PAGEDEVICE_NO_ACTION,
    PAGEDEVICE_REACTIVATING,
    PAGEDEVICE_DEFERRED_DEACTIVATE,
    PAGEDEVICE_FORCED_DEACTIVATE
  } action ;
} deactivate_pagedevice_t ;

/* exported variables */
extern uint32 securityArray[];

extern Bool   doing_imposition;
extern Bool   doing_mirrorprint;

enum { PAGEBASEID_INVALID = -1, PAGEBASEID_INITIAL = 0 } ;
extern int32   pageBaseMatrixId;
extern OMATRIX pageBaseMatrix;

extern int32 showno ;
extern int32 bandid ;


/* exported functions */
Bool securityTickle(void);

struct core_init_fns;
void ea_checks_C_globals(
  struct core_init_fns* fns);

Bool is_pseudo_erasepage(void);

Bool gs_applyEraseColor(corecontext_t *context,
                        Bool use_current_gs,
                        dl_color_t *eraseColor,
                        LateColorAttrib **lateColorAttribs,
                        Bool knockout);

Bool call_resetpagedevice(void);
void deactivate_pagedevice(GSTATE *gs_dst ,
                           GSTATE *gs_src ,
                           OBJECT *pagedevice ,
                           deactivate_pagedevice_t *dpd_res);
Bool do_pagedevice_reactivate(ps_context_t *pscontext, deactivate_pagedevice_t *dpd);
Bool do_pagedevice_deactivate(deactivate_pagedevice_t *dpd);
Bool do_pagedevice_showpage(Bool forced);

/**
 * These numbers are the circumstances in which EndPage is being called. The
 * numbers are not arbitrary: they are defined on page 252 of RB2; the
 * reactivate, partial and not active codes are not part of that definition.
 */
enum {
  PAGEDEVICE_DO_SHOWPAGE   =  0, /* Required value. */
  PAGEDEVICE_DO_COPYPAGE   =  1, /* Required value. */
  PAGEDEVICE_DO_DEACTIVATE =  2, /* Required value. */
  PAGEDEVICE_DO_REACTIVATE,
  PAGEDEVICE_DO_PARTIAL,
  PAGEDEVICE_NOT_ACTIVE
} ;

/** \brief Flag indicating type of render handoff in progress.

     This is one of the PAGEDEVICE_* enumeration values. */
extern int rendering_in_progress;

Bool do_sethalftonephase( STACK *stack );

#endif /* protection for multiple inclusion */

/*
Log stripped */
