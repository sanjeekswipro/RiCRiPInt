/** \file
 * \ingroup trapping
 *
 * $HopeName: COREtrapping!src:notrap.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for trapping functions when compiled out.
 */

#include "core.h"
#include "swerrors.h"
#include "displayt.h"
#include "trap.h"
#include "taskh.h"

void trapping_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

void trapSetTrappingEffort(int32 effort)
{
  UNUSED_PARAM(int32, effort) ;
}

Bool isTrappingActive(DL_STATE *page)
{
  UNUSED_PARAM(DL_STATE *, page);
  return FALSE ;
}

void trapShapeBoundingBox(trap_RLE_shape_t *shape, dbbox_t *bbox)
{
  UNUSED_PARAM(trap_RLE_shape_t *, shape) ;
  UNUSED_PARAM(dbbox_t *, bbox) ;
}

Bool trapZoneReset(DL_STATE *page, Bool page_zones_only)
{
  UNUSED_PARAM(DL_STATE *,page);
  UNUSED_PARAM(Bool, page_zones_only) ;
  return TRUE ;
}

Bool trapPrepare(ps_context_t *pscontext,
                 DL_STATE *dl_state,
                 trap_context_t **ptc,
                 task_t *trap_complete_task)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;
  UNUSED_PARAM(DL_STATE *, dl_state) ;
  UNUSED_PARAM(task_t *, trap_complete_task) ;
  *ptc = NULL ;
  return TRUE ;
}

Bool trapGenerate(trap_context_t *tc)
{
  UNUSED_PARAM(trap_context_t *, tc) ;
  return TRUE ;
}

Bool trapRequirements(DL_STATE *page)
{
 UNUSED_PARAM(DL_STATE *, page) ;
  return FALSE ;
}

void trapDispose(trap_context_t **tc)
{
  UNUSED_PARAM(trap_context_t **, tc) ;
}

Bool trapBackdropColorValues( render_info_t *ri ,
                              LISTOBJECT *lobj,
                              const dbbox_t *bounds )
{
  UNUSED_PARAM(render_info_t *, ri);
  UNUSED_PARAM(LISTOBJECT *, lobj);
  UNUSED_PARAM(const dbbox_t *, bounds);
  return TRUE;
}

void surface_set_trap_builtin(surface_set_t *set, const surface_t *index[])
{
  UNUSED_PARAM(surface_set_t *, set);
  UNUSED_PARAM(const surface_t **, index);
  return;
}

/* Log stripped */
