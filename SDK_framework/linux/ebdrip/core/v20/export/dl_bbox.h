/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_bbox.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exported functions for display list BBox code
 */

#ifndef __DL_BBOX_H__
#define __DL_BBOX_H__

#include "displayt.h"  /* LISTOBJECT */

void dlobj_bbox(
  /*@notnull@*/ /*@in@*/        LISTOBJECT *lobj ,
  /*@notnull@*/ /*@out@*/       dbbox_t *bbox ) ;

/* Given an x1 and x2 determines whether the lobj intersects this area.  */
Bool dlobj_intersects(LISTOBJECT *lobj, dcoord x1, dcoord x2, Bool *intersects);

/* Tries to find the largest rectangular area covered by the object without any
   holes.  Returns false if such an area could not be found for the object.
 */
Bool dlobj_interior_box(
  /*@notnull@*/ /*@in@*/        LISTOBJECT* lobj,
  /*@notnull@*/ /*@out@*/       dbbox_t *interior_box ) ;

/*
Log stripped */
#endif /* __DL_BBOX_H__ */
