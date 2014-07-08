/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_foral.h(EBDSDK_P.1) $
 * $Id: export:dl_foral.h,v 1.22.2.1.1.1 2013/12/19 11:25:19 anon Exp $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Display-list enumeration API
 */

#ifndef __DL_FORAL_H__
#define __DL_FORAL_H__

#include "display.h" /* DLRANGE definition */

/*
 * The DL forall flag set doubles both to control whether dl_forall recurses
 * into sub-objects, and to report to the callback function whether the DL
 * walk is inside a sub-object. enums are usually preferable for manifest
 * constants, but enums are always int, and these flags should be unsigned,
 * so defines are used here instead.
 */
#define DL_FORALL_USEMARKER  1u /* Control: use marker to visit objects once */
#define DL_FORALL_DLRANGE    2u /* Control: iterate dlrange not hdl */
#define DL_FORALL_PATTERN    4u /* Control, report: recurse into patterns */
#define DL_FORALL_SOFTMASK   8u /* Control, report: recurse into softmasks */
#define DL_FORALL_SHFILL    16u /* Control, report: recurse into shfill/vign */
#define DL_FORALL_GROUP     32u /* Control, report: recurse into groups */
#define DL_FORALL_NONE      64u /* Control, report: /None coloured objects */

/**
 * Structure used both as input to specify the dl_forall() request, and
 * output passed to the dl_forall() callback function.
 */
typedef struct DL_FORALL_INFO {
  LISTOBJECT *lobj;       /**< DL object enumeration has reached */
  DL_STATE *page;         /**< Input page */
  HDL *hdl;               /**< Input hdl */
  struct DLRANGE dlrange; /**< Input DL range / Current DL position */
  uint32 inflags;         /**< Enumeration request, set of DL_FORALL_XXX */
  uint32 reason;          /**< Why client callback made, set of DL_FORALL_XXX */
  uint32 depth;           /**< Within DL tree */
  void *data;             /**< Client data pointer */
} DL_FORALL_INFO;

/**
 * client supplied forall callback function
 */
typedef Bool (*dl_forall_fn)(DL_FORALL_INFO *info);

Bool dl_forall(/*@notnull@*/ /*@in@*/ DL_FORALL_INFO *info,
               /*@notnull@*/          dl_forall_fn func);

#endif /* protection for multiple inclusion */

/* Log stripped */
