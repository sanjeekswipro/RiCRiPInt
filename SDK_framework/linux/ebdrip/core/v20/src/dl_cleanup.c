/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_cleanup.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Methods to cleanup a DL object after a partial paint.
 *
 * Post partial-paint cleanup will often simply result in the destruction of
 * the DL object, but some objects (e.g. HDL's and their derivatives) will
 * often need to take some memory-liberating action, but not actually destroy
 * themselves. Thus each cleanup method returns a boolean value indicating if
 * the object was destroyed.
 */

#include "core.h"
#include "dl_cleanup.h"

#include "dl_store.h"
#include "dl_free.h"
#include "display.h"
#include "hdlPrivate.h"
#include "groupPrivate.h"
#include "vnobj.h"
#include "shadex.h"

/* --Private prototypes-- */

static Bool cleanupSimple(LISTOBJECT* object, DL_STATE *page);
static Bool cleanupErase(LISTOBJECT* object, DL_STATE *page);
static Bool cleanupVignette(LISTOBJECT* object, DL_STATE *page);
static Bool cleanupShfill(LISTOBJECT* object, DL_STATE *page);
static Bool cleanupHDL(LISTOBJECT* object, DL_STATE *page);
static Bool cleanupGroup(LISTOBJECT* object, DL_STATE *page);

/* --Private types-- */

/* Cleanup function interface. Cleanup functions return TRUE if the object
was completely destroyed (and thus should be removed from the display list). */
typedef Bool (*dlCleanupFunc)(LISTOBJECT* object, DL_STATE *page);

/* --Private variables-- */

/* This table maps display list object render codes to cleanup functions.
The render code for an object is really mis-named now, and is generally accepted
to be the identifier for an object on the display list. The render code is
listed beside each function to illustrate how the cleanup functions map to each
object type.

!!NOTE!! There must be an entry in this table for each render opcode. */
static dlCleanupFunc cleanupFuncs[N_RENDER_OPCODES] = {
  cleanupSimple,  /* - RENDER_void */
  cleanupErase,   /* - RENDER_erase */
  cleanupSimple,  /* - RENDER_char */
  cleanupSimple,  /* - RENDER_rect */
  cleanupSimple,  /* - RENDER_quad */
  cleanupSimple,  /* - RENDER_fill */
  cleanupSimple,  /* - RENDER_mask */
  cleanupSimple,  /* - RENDER_image */
  cleanupVignette,/* - RENDER_vignette */
  cleanupSimple,  /* - RENDER_gouraud */
  cleanupShfill,  /* - RENDER_shfill */
  cleanupSimple,  /* - RENDER_shfill_patch */
  cleanupHDL,     /* - RENDER_hdl */
  cleanupGroup,   /* - RENDER_group */
  cleanupSimple,  /* - RENDER_backdrop */
  cleanupSimple   /* - RENDER_cell */
};


/* --Public methods-- */

/* DL object cleanup entry point. Dispatch the object to the associated cleanup
function.
*/
Bool cleanupDLObjectAfterPartialPaint(LISTOBJECT* object, DL_STATE *page)
{
  HQASSERT(object != NULL,
           "cleanupDLObjectAfterPartialPaint - 'object' cannot be NULL");

  return cleanupFuncs[object->opcode](object, page);
}

/* --Private methods-- */

/* Simple cleanup method - just destroy the object and return TRUE.
*/
static Bool cleanupSimple(LISTOBJECT* object, DL_STATE *page)
{
  free_dl_object(object, page);
  return TRUE;
}

/* Erases are considered indestructable; they simple change their state from a
true erase to a 'read in partially painted band' object.
*/
static Bool cleanupErase(LISTOBJECT *dlobj, DL_STATE *page)
{
  HQASSERT(dlobj != NULL, "cleanupErase - 'object' cannot be NULL");
  HQASSERT(page != NULL, "cleanupErase - 'page' cannot be NULL");

  /* This changes the erase to be a 'load previously painted band' indicator
     in the renderer if partial painting. */
  dlobj->dldata.erase.newpage = (uint8)FALSE;

  /* Unless explicitly preserved, state objects do not survive partial
     paints. */
  dlSSPreserve(page->stores.state, &dlobj->objectstate->storeEntry, TRUE);

  return FALSE;
}

static Bool cleanupVignette(LISTOBJECT *lobj, DL_STATE *page)
{
  HQASSERT(lobj != NULL, "object cannot be NULL");
  HQASSERT(lobj->opcode == RENDER_vignette, "Expected a vignette");
  HQASSERT(lobj->dldata.vignette->vhdl != NULL, "HDL cannot be NULL");

  if ( !hdlCleanupAfterPartialPaint(&lobj->dldata.vignette->vhdl) )
    return FALSE;

  free_dl_object(lobj, page);
  return TRUE;
}

static Bool cleanupShfill(LISTOBJECT *lobj, DL_STATE *page)
{
  HQASSERT(lobj != NULL, "object cannot be NULL");
  HQASSERT(lobj->opcode == RENDER_shfill, "Expected a shfill");
  HQASSERT(lobj->dldata.shade->hdl != NULL, "HDL cannot be NULL");

  if ( !hdlCleanupAfterPartialPaint(&lobj->dldata.shade->hdl) )
    return FALSE;

  free_dl_object(lobj, page);
  return TRUE;
}

static Bool cleanupHDL(LISTOBJECT *lobj, DL_STATE *page)
{
  HQASSERT(lobj != NULL, "object cannot be NULL");
  HQASSERT(lobj->opcode == RENDER_hdl, "Expected an HDL");
  HQASSERT(lobj->dldata.hdl != NULL, "HDL cannot be NULL");

  if ( !hdlCleanupAfterPartialPaint(&lobj->dldata.hdl) )
    return FALSE;

  free_dl_object(lobj, page);
  return TRUE;
}

static Bool cleanupGroup(LISTOBJECT *lobj, DL_STATE *page)
{
  HQASSERT(lobj != NULL, "object cannot be NULL");
  HQASSERT(lobj->opcode == RENDER_group, "Expected a group");
  HQASSERT(lobj->dldata.hdl != NULL, "HDL cannot be NULL");

  if ( !groupCleanupAfterPartialPaint(&lobj->dldata.group) )
    return FALSE;

  free_dl_object(lobj, page);
  return TRUE;
}

/* Log stripped */
