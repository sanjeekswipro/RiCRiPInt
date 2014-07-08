/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:hdl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Public functional interface to the Hierarchical Display List.
 */

#ifndef __HDL_H__
#define __HDL_H__

#include "displayt.h"

struct render_info_t ; /* from render.h */

/**
 * Different kinds of HDLs supported
 */
enum {
  HDL_BASE,     /**< Top level DL */
  HDL_PAGE,     /**< Page level HDL inside base (multiple page HDLs if imposing) */
  HDL_FORM,     /**< Representing PDL form constructs */
  HDL_GROUP,    /**< For Transparency groups */
  HDL_PATTERN,  /**< For sub-DL patterns */
  HDL_SHFILL,   /**< For elements of a smooth shade */
  HDL_VIGNETTE, /**< For elements of a vignette */
  N_HDL_TYPES   /**< Used for array initialisation sizes etc */
};

Bool hdlOpen(/*@notnull@*/ /*@in@*/ DL_STATE* root,
             Bool banded, int32 purpose,
             /*@notnull@*/ /*@out@*/ HDL** hdl);
Bool hdlClose(/*@notnull@*/ /*@in@*/ HDL** hdlPointer,
              Bool success);
void hdlDestroy(/*@notnull@*/ /*@in@*/ HDL** hdlPointer);

uint32 hdlId(/*@in@*/ /*@notnull@*/ /*@observer@*/ HDL *hdl);

/** Render an HDL. If the boolean interset parameter is true, then
    self-intersection will be prevented by using a clipping mask; later
    objects in the Z-order will knockout earlier objects. */
Bool hdlRender(/*@notnull@*/ /*@in@*/ HDL* hdl,
               /*@notnull@*/ /*@in@*/ struct render_info_t* renderInfo,
               /*@null@*/ /*@in@*/ TranAttrib *transparency,
               Bool intersect);

Bool hdlIsEmpty(/*@in@*/ /*@notnull@*/ /*@observer@*/ HDL* hdl);
void hdlBBox(HDL *hdl, dbbox_t *bbox);

/*@dependent@*/
HDL *hdlParent(/*@in@*/ /*@notnull@*/ HDL* hdl);

/*@dependent@*/
DLREF* hdlOrderList(/*@notnull@*/ /*@in@*/ HDL* hdl);

void hdlDlrange(HDL *hdl, DLRANGE *dlrange);

void hdlDlrangeNoErase(HDL *hdl, DLRANGE *dlrange);

void hdlDlrangeBackwards(HDL *hdl, DLRANGE *dlrange);

/** Get band from HDL by (page-relative) band number. */
DLREF *hdlGetHead(HDL *hdl, uint32 bandi);

Group *hdlGroup(/*@notnull@*/ /*@in@*/ HDL *hdl);

Group *hdlEnclosingGroup(/*@notnull@*/ /*@in@*/ HDL *hdl);

/* HDL Support for recombine */

void hdlRemoveFirstObject(HDL *hdl);

Range hdlExtentOnParent(HDL *hdl);

DLREF **hdlBands(HDL *hdl);

DLREF **hdlBandTails(HDL *hdl);

DLREF **hdlBandTailSnapshot(HDL *hdl);

Bool hdlAdjustBandRange(HDL *hdl, LISTOBJECT *lobj, Range oldr, Range newr);

void hdlSetOverprint(HDL *hdl);

dl_color_t* hdlColor(HDL *hdl);

DLREF *hdlOrderListLast(HDL *hdl);

Bool hdlTransparent(HDL *hdl);

Bool hdlPatterned(HDL *hdl);

#endif


/* --Description--

Note that src:hdlPrivate.h contains further documentation regarding low-level
customization issues.

--Basic concepts--
The display list is notionally a list of bands, where each band covers some
horizontal region (whose height is defined by the 'band height') of the output
page.

Objects added to this notional display list are only added those bands which
their vertical extent overlaps. This spacial organization of graphical objects
is an optimisation that greatly reduces the amount of time required to render
the page in sections - which is useful as there is rarely enough memory to
render a whole page in its entirety.

The HDL object implements this behavior, with support for recursive display
lists. There is always at least one HDL (the 'base' HDL) which is the root of
the HDL hierarchy, and there is always a 'current' HDL, which is initially
set to the base HDL.

Any objects added to the display list are added to the current HDL.

--Controlling the current HDL--
A new current HDL is created by a call to one of the hdlOpen...() methods. This
new HDL will have the previous current HDL as its parent in the HDL hierarchy.

All objects added to the display list will now be added to this new HDL, until
a call to hdlClose() is made, at which point the HDL will be closed and
permenantly added to the parent HDL (this is not always the case - see
'Floating HDLs'). The parent HDL will then be reinstated as the current HDL.

Note that this process is fully recursive - any number of HDL's may be opened
before any are closed. Note that order of closure is important - only the
current HDL can ever be closed.

--Aborting HDLs--
Should you wish to abandon the contents of the current HDL, you can set the
'abort' flag to TRUE when calling hdlClose(). This will cause the HDL, and all
of its contents, to be destroyed.

This is the only way to destroy open HDLs.

--Destroying HDLs--
hdlDestroy() causes a HDL and its contents to be deallocated. This method can
only be called on closed HDLs - should you wish to destroy an open HDL, call
hdlClose() with the 'abort' flag set to true.

Note that any references to the HDL being destroyed must be cleared by the
client (for example - the HDL may be referenced by its parent in the HDL
hierarchy). However, it should not generally be necessary to explicitly destroy
HDLs referenced in such a way.

--Floating HDLs--
A 'floating' HDL is one that is not actually added to the HDL hierarchy. This
is useful when you have captured a set of objects that should not be directly
rendered on the page, but is instead used later in some other way. An example
of this is a soft mask in PDF.

--Empty HDLs--
If a HDL is empty when it is closed, it will be silently destroyed (hdlClose()
will return TRUE as normal). In this instance, the HDL instance pointer, as
pointed by 'hdlPointer' in hdlClose() will be set to NULL, as it will in any
situation that causes the hdl to be destroyed (e.g. through error or if the
'abort' flag is TRUE).

--Updating the region map--
The region map is a page-level structure which controls application of backdrop
rendering. The hdlClose() function takes a flag which controls if this structure
if updated, using the bounds of the finished HDL.

This level of control is probably unnecessary.

--DL iteration--
It is often required to be able iterate over all elements in all HDLs
associated with a given page. This used to be done by a laborious descent
through the hierachy starting from the root, and marking visited objects to
avoid processing them twice. This method has been replaced by keeping a list
of all HDLs associated with a given page to allow for easy enumeration.

*/

/* Log stripped */
