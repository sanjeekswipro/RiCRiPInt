/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:implicitgroup.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS implicit group for display-list creation
 */

#include "core.h"
#include "implicitgroup.h"
#include "group.h"
#include "gstack.h"
#include "graphics.h"
#include "tranState.h"
#include "dl_color.h"
#include "gscdevci.h"
#include "graphict.h"
#include "gs_color.h"
#include "swmemory.h"
#include "render.h"
#include "hdl.h"
#include "groupPrivate.h"
#include "namedef_.h"

#define TOLERANCE 0.001

/** Open an implicit group if required. Calls to this function must be matched
   with a call to closeImplicitGroup().

   In the context of transparency, the B (combined fill and stroke) operator
   requires an implicit group to be created in order to avoid compositing the
   overlapping sections of the stroke and path against each other.

   The PDF 1.4 specification complicates this by trying to preserve overprint
   behavior when possible.
*/
Bool openImplicitGroup(DL_STATE *page, Group** group, int32 *gid,
                       IMPLICIT_GROUP_USAGE usage,
                       Bool transparent_pattern)
{
  TranState* ts = &gstateptr->tranState;
  USERVALUE strokingAlpha = tsConstantAlpha(ts, TRUE);
  USERVALUE nonstrokingAlpha = tsConstantAlpha(ts, FALSE);

  HQASSERT(group != NULL || gid != NULL,
           "openImplicitGroup - parameters cannot be NULL");
  HQASSERT(usage == IMPLICIT_GROUP_STROKE ||
           usage == IMPLICIT_GROUP_PDF ||
           usage == IMPLICIT_GROUP_XPS ||
           usage == IMPLICIT_GROUP_TEXT_KO,
           "Unexpected usage in openImplicitGroup");

  *group = NULL ;
  *gid = GS_INVALID_GID ;

  /* No need to create a group if the stroke and fill are both opaque. */
  if ( !transparent_pattern && tsOpaque(ts, TsStrokeAndNonStroke, gstateptr->colorInfo) )
    return TRUE;

  /* Opening a group changes the current gstate - save now, and we'll restore
     when we close the implicit group. */
  if ( gs_gpush(GST_GROUP) ) {
    int32 groupgid = gstackptr->gId ;

    /* Update reference into gstate after gsave. */
    ts = &gstateptr->tranState;

    /* For PDF, if the stroke and non-stroking alpha are the same and
       overprinting is enabled, we can make some effort to preserve
       overprint behavior.  Otherwise a knockout group is used.
       (PDF 1.4 p. 462)

       If this implicit group is required as workaround for stroke
       self-intersection artifacts, jump straight to the knockout group.

       XPS never uses a knockout group in case there are separate alphas
       which must be applied inside the the group.  The alpha already in
       force applies on the group, cumulatively.
    */
    if (usage == IMPLICIT_GROUP_XPS ||
        (usage == IMPLICIT_GROUP_PDF &&
         fabs(strokingAlpha - nonstrokingAlpha) < TOLERANCE &&
         (gsc_getoverprint(gstateptr->colorInfo, GSC_FILL) ||
          gsc_getoverprint(gstateptr->colorInfo, GSC_STROKE)))) {
      /* To preserve overprinting, we print into a non-knockout, non-isolated
         group, with stroke and non-stroking alpha set to 1, and the Normal
         blend mode (overprinting is handled automatically). The group itself
         is drawn with the stroke/nonstroke alpha and the normal blend
         mode. */

      /* Opening a group initialises the transparency graphics state. This is
         fine because we want to draw using an alpha of 1 using Normal
         anyway. We'll let the soft mask apply to the group as a whole. */
      if ( groupOpen(page, onull /*colorspace*/, FALSE /*I*/, FALSE /*K*/,
                     TRUE /*Banded*/, NULL /*bgcolor*/, NULL /*xferfn*/,
                     NULL /*patternTA*/, GroupImplicit, group)) {
        if ( gs_gpush(GST_GSAVE) ) {
          *gid = groupgid ;
          return TRUE ;
        }
        (void)groupClose(group, FALSE) ;
      }
    } else {
      /* Create a non-isolated knockout group. The fill and stroke use their
         prevailing alpha and blend mode, and the group as a whole uses
         Normal with an alpha of 1. */
      OBJECT blendMode = OBJECT_NOTVM_NOTHING;
      OBJECT normal = OBJECT_NOTVM_NAME(NAME_Normal, LITERAL) ;
      Bool alphaIsShape;

      HQASSERT(usage == IMPLICIT_GROUP_STROKE ||
               usage == IMPLICIT_GROUP_PDF ||
               usage == IMPLICIT_GROUP_TEXT_KO,
               "Implicit knockout group should only be created for Text KO, strokes and PDF");

      /* Install 'Normal' blend mode. */
      blendMode = tsBlendMode(ts);
      tsSetBlendMode(ts, normal, gstateptr->colorInfo);

      /* Set the alpha to 1.0 for the group. */
      tsSetConstantAlpha(ts, FALSE, 1.0f, gstateptr->colorInfo);
      alphaIsShape = tsAlphaIsShape(ts); tsSetAlphaIsShape(ts, FALSE);

      /* Opening a group initialises the transparency graphics state. We will
         reinstate the blend mode and alpha manually, and let the soft mask
         apply to the group as a whole. */
      if ( groupOpen(page, onull /*colorspace*/, FALSE /*I*/, TRUE /*K*/,
                     TRUE /*Banded*/, NULL /*bgcolor*/, NULL /*xferfn*/,
                     NULL /*patternTA*/, GroupImplicit, group) ) {
        /* Restore blend mode and alpha. */
        tsSetBlendMode(ts, blendMode, gstateptr->colorInfo);
        tsSetConstantAlpha(ts, TRUE, strokingAlpha, gstateptr->colorInfo);
        tsSetConstantAlpha(ts, FALSE, nonstrokingAlpha, gstateptr->colorInfo);
        tsSetAlphaIsShape(ts, alphaIsShape);

        if ( gs_gpush(GST_GSAVE) ) {
          *gid = groupgid ;
          return TRUE ;
        }
        (void)groupClose(group, FALSE) ;
      }
    }

    (void)gs_cleargstates(groupgid, GST_GROUP, NULL) ;
  }

  return FALSE;
}

/** Close an implicit group (if it was required), restoring and changes made
   to the transparency state.
*/
Bool closeImplicitGroup(Group **group, int32 gid, Bool result)
{
  HQASSERT(group != NULL, "closeImplicitGroup - 'group' cannot be NULL.");

  /* Close the group. This may be empty if the object was completely clipped
     out. */
  if ( *group != NULL ) {
    if ( hdlIsEmpty(groupHdl(*group)) ) {
      groupDestroy(group) ;
    } else {
      if ( !groupClose(group, result) )
        result = FALSE ;
    }
  }

  /* openImplicitGroup() gsaved to preserve any soft mask. */
  if (gid != GS_INVALID_GID && !gs_cleargstates(gid, GST_GROUP, NULL))
    result = FALSE;

  return result;
}

/* Log stripped */
