/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_foral.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display-list enumeration
 */

#include "core.h"
#include "dl_foral.h"

#include "often.h"
#include "bitblts.h"
#include "display.h"
#include "trap.h"
#include "hdlPrivate.h"
#include "group.h"
#include "vndetect.h"
#include "dl_purge.h"
#include "vnobj.h"
#include "shadex.h"
#include "pattern.h"

static Bool dl_forall_dlrange(DL_FORALL_INFO *info, dl_forall_fn func);

/**
 * Private routine used by dl_forall to recurse into a sub-hdl
 *
 * Take a copy of the forall info structure on the stack so as to maintain
 * the data recursively at the right depth.
 */
static Bool dl_forall_recurse(DL_FORALL_INFO *info, dl_forall_fn func,
                              uint32 reason, HDL *hdl)
{
  DL_FORALL_INFO rinfo = *info; /* copy for recursive call */

  HQASSERT(hdl, "Lost HDL during dl recursive enumeration");
  rinfo.depth++;
  rinfo.hdl = hdl;
  rinfo.reason |= reason;
  hdlDlrange(rinfo.hdl, &(rinfo.dlrange));

  return dl_forall_dlrange(&rinfo, func);
}

/**
 * Private routine used by dl_forall in enumerating DL elements, to deal
 * with a single object.
 */
static Bool dl_forall_one(DL_FORALL_INFO *info, dl_forall_fn func)
{
  LISTOBJECT *lobj = info->lobj;
  STATEOBJECT *dlstate = lobj->objectstate;
  uint32 reason;

  if ( dl_is_none(lobj->p_ncolor) ) {
    if ( (info->inflags & DL_FORALL_NONE) == 0 )
      return TRUE; /* Ignore this object */

    info->reason |= DL_FORALL_NONE;
  }

  /* now callback to the client */
  if ( ! (*func)(info) )
    return FALSE;

  /* Recheck for a none color in case callback has changed it. */
  if ( dl_is_none(lobj->p_ncolor) ) {
    if ( (info->inflags & DL_FORALL_NONE) == 0 )
      return TRUE; /* Ignore this object */

    info->reason |= DL_FORALL_NONE;
  }

  /*
   * Now check for recursive elements
   * First look for HDLs in pattern or transparency
   */
  if ( dlstate != NULL ) {
    HDL *patHdl = patternHdl(dlstate->patternstate);

    if ( patHdl != NULL && (info->inflags & DL_FORALL_PATTERN) != 0 )
      if ( !dl_forall_recurse(info, func, DL_FORALL_PATTERN, patHdl) )
        return FALSE;

    if ( dlstate->tranAttrib != NULL ) {
      TranAttrib *ta = dlstate->tranAttrib;

      if ( ta->softMask != NULL && ta->softMask->group != NULL &&
          (info->inflags & DL_FORALL_SOFTMASK) != 0 ) {
        if ( !dl_forall_recurse(info, func, DL_FORALL_SOFTMASK, 
                                groupHdl(ta->softMask->group)) )
          return FALSE;
      }
    }
  }

  /* Now Check for objects which contain sub-display lists. */
  reason = DL_FORALL_USEMARKER; /* mark as invalid */
  if ( lobj->opcode == RENDER_vignette &&
       (info->inflags & DL_FORALL_SHFILL) != 0 ) {
    reason = DL_FORALL_SHFILL;
    if ( !dl_forall_recurse(info, func, reason, lobj->dldata.vignette->vhdl) )
      return FALSE;
  } else if ( lobj->opcode == RENDER_shfill &&
              (info->inflags & DL_FORALL_SHFILL) != 0 ) {
    reason = DL_FORALL_SHFILL;
    if ( !dl_forall_recurse(info, func, reason, lobj->dldata.shade->hdl) )
      return FALSE;
  } else if ( lobj->opcode == RENDER_hdl ) {
    reason = 0; /* No flag/reason for going int HDLs ? */
    if ( !dl_forall_recurse(info, func, reason, lobj->dldata.hdl) )
      return FALSE;
  } else if ( lobj->opcode == RENDER_group &&
              (info->inflags & DL_FORALL_GROUP) != 0 ) {
    reason = DL_FORALL_GROUP;
    if ( !dl_forall_recurse(info, func, reason, groupHdl(lobj->dldata.group)) )
      return FALSE;
  }

  return TRUE;
}

/**
 * Private routine used by dl_forall to enumerate all the DL objects.
 *
 * Key point is that it recurses for all dl elements in a vignette chain,
 * and for all dl elements in any pattern states.
 *
 * N.B. Recombine code uses dl forall and has a callback function which may
 * end up deleting the current object being enumerated. Therefore have to
 * save the next pointer before calling back to the client. Conversely, when
 * DL has been purged to disk we cannot save the next pointer before the
 * client call as it may cause the current object to be unloaded from memory.
 * Hence we have to make the stepping of the DL to next dependent on whether
 * we are purging or possibly recombining. Yuck.
 */
static Bool dl_forall_dlrange(DL_FORALL_INFO *info, dl_forall_fn func)
{
  struct DLRANGE *dlrange = &(info->dlrange);
  Bool purge = dlpurge_inuse();

  for ( dlrange_start(dlrange); !dlrange_done(dlrange); ) {
    DLREF *next = purge ? NULL : dlref_next(dlrange->current.dlref);

    info->lobj = dlrange_lobj(dlrange);
    HQASSERT(info->lobj, "Lost DL object during enumeration");

    /* If we are not use markers, or if the marker shows this object has
       not been visited, then mark it as visited and call the
       iteration function */
    if ( (info->inflags & DL_FORALL_USEMARKER) == 0 ||
         (info->lobj->marker & MARKER_DL_FORALL) == 0 ) {
      info->lobj->marker |= MARKER_DL_FORALL;

      if ( !dl_forall_one(info, func) )
        return FALSE;
    }
    if ( purge )
      dlrange_next(dlrange);
    else
      dlrange->current.dlref = next;
  }
  return TRUE;
}

/**
 * Callback used by dl_forall when we first clear the DL markers
 *
 * Need this object again on the next band; so need to clear the bit
 */
static Bool dl_clearmarkers(DL_FORALL_INFO *info)
{
  info->lobj->marker &= (~MARKER_DL_FORALL);
  return TRUE;
}

/*
 * Generic routine for iterating over sections of the display list.
 *
 * It can be requested to iterate either an entire hdl (info->hdl) or
 * when (info->flags & DL_FORALL_DLRANGE) is true, it instead iterates
 * over the section specified by info->dlrange
 * It can also be requested to descend into pattern/vignette/shfill
 * sub-dls by use of the appropriate DL_FORALL_XX flags
 */
Bool dl_forall(DL_FORALL_INFO *client_info, dl_forall_fn func)
{
  Bool result = TRUE;
  DL_FORALL_INFO info;

  HQASSERT(client_info, "Invalid argument for DL forall");
  info = *client_info; /* copy so we don't trample on user input request */
  HQASSERT(func, "No callbackfunc for DL forall");
  /* Add to the following assert if more option flags are added */
  HQASSERT((info.inflags & ~(DL_FORALL_USEMARKER|DL_FORALL_DLRANGE|
           DL_FORALL_PATTERN|DL_FORALL_SOFTMASK|DL_FORALL_SHFILL|
           DL_FORALL_GROUP|DL_FORALL_NONE)) == 0, "invalid dl forall flags");
  info.depth  = 0; /* Start at the top level and descend */
  info.reason = 0; /* no reason for calling back to client yet */
  if ( info.inflags & DL_FORALL_DLRANGE ) {
    info.hdl = NULL;
  } else {
    HQASSERT(info.hdl, "No display list to walk");
    hdlDlrange(info.hdl, &(info.dlrange));
  }

  /*
   * If we have been asked to use markers, turn marker use off and iterate
   * to clear all the marker bits, then turn marker use back on 
   */
  if ( (info.inflags & DL_FORALL_USEMARKER) != 0 ) {
    info.inflags &= ~DL_FORALL_USEMARKER;
    result = dl_forall_dlrange(&info, dl_clearmarkers);
    info.inflags |=  DL_FORALL_USEMARKER;
  }
  if ( result )
    result = dl_forall_dlrange(&info, func);
  return result;
}

/* Log stripped */
