/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:group.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Transparency Group object.
 */

#include "core.h"
#include "groupPrivate.h"

#include "cce.h"                /* CCEModeNormal */
#include "compositers.h"        /* cceMultiplyAlpha */
#include "devops.h"             /* xdpi, ydpi */
#include "dl_foral.h"           /* dl_forall */
#include "dl_store.h"           /* dlSSInsert */
#include "functns.h"            /* FN_SOFTMASK_TFR */
#include "gs_color.h"           /* NARROW_01 */
#include "gsc_icc.h"            /* gsc_safeBackendColorSpace */
#include "gscdevci.h"           /* gsc_setoverprint */
#include "gschcms.h"            /* REPRO_TYPE_* */
#include "gschead.h"            /* gsc_setcolordirect */
#include "gscsmpxform.h"        /* gsc_spacecache_setcolorspace */
#include "gu_htm.h"             /* htm_GetFirstHalftoneRef */
#include "hdlPrivate.h"         /* hdlFromListobject */
#include "idlom.h"              /* IDLOM_BEGINGROUP */
#include "imexpand.h"           /* im_expand* */
#include "imstore.h"
#include "imageo.h"             /* IMAGEOBJECT */
#include "jobmetrics.h"         /* dl_metrics() */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_Black */
#include "params.h"             /* SystemParams */
#include "pattern.h"            /* UNCOLOURED_PATTERN */
#include "patternrender.h"      /* pattern_finish */
#include "plotops.h"            /* degenerateClipping */
#include "rcbcntrl.h"           /* rcbn_disable_interception */
#include "renderom.h"           /* omit_backdrop_prepare */
#include "rleColorantMapping.h" /* RleColorantMap */
#include "routedev.h"           /* DEVICE_SETG */
#include "surface.h"
#include "spdetect.h"           /* disable_separation_detection */
#include "swerrors.h"           /* VMERROR */
#include "rleblt.h"             /* rleSet... */
#include "timing.h"             /* probe_begin */
#include "tranState.h"          /* tsDefault */
#include "trap.h"               /* trapCaptureBackdropColorValues */
#include "vndetect.h"           /* flush_vignette */
#include "backdrop.h"
#include "pixelLabels.h"
#include "gu_chan.h"
#include "blitcolorh.h"
#include "blitcolors.h"
#include "spotlist.h"
#include "cvcolcvt.h"
#include "graphict.h"           /* GS_COLORinfo */
#include "mm.h"
#include "ripmulti.h"
#include "preconvert.h"
#include "region.h"
#include "gscxfer.h"            /* gsc_setTransfersPreapplied */

#ifdef METRICS_BUILD
/* Parked here for want of a better place to put it: */
void updateBlendModeMetrics(TranAttrib* tranAttrib, dl_metrics_t *dlmetrics)
{
  if ( tranAttrib == NULL )
    return ;

  switch (tranAttrib->blendMode) {
  case CCEModeNormal:
    if (tranAttrib->alpha == COLORVALUE_ONE)
      dlmetrics->blendmodes.opaqueNormal++ ;
    else
      dlmetrics->blendmodes.normal++ ;
    break;
  case CCEModeMultiply:
    dlmetrics->blendmodes.multiply++ ;
    break;
  case CCEModeScreen:
    dlmetrics->blendmodes.screen++ ;
    break;
  case CCEModeOverlay:
    dlmetrics->blendmodes.overlay++ ;
    break;
  case CCEModeSoftLight:
    dlmetrics->blendmodes.softLight++ ;
    break;
  case CCEModeHardLight:
    dlmetrics->blendmodes.hardLight++ ;
    break;
  case CCEModeColorDodge:
    dlmetrics->blendmodes.colorDodge++ ;
    break;
  case CCEModeColorBurn:
    dlmetrics->blendmodes.colorBurn++ ;
    break;
  case CCEModeDarken:
    dlmetrics->blendmodes.darken++ ;
    break;
  case CCEModeLighten:
    dlmetrics->blendmodes.lighten++ ;
    break;
  case CCEModeDifference:
    dlmetrics->blendmodes.difference++ ;
    break;
  case CCEModeExclusion:
    dlmetrics->blendmodes.exclusion++ ;
    break;
  case CCEModeHue:
    dlmetrics->blendmodes.hue++ ;
    break;
  case CCEModeSaturation:
    dlmetrics->blendmodes.saturation++ ;
    break;
  case CCEModeColor:
    dlmetrics->blendmodes.color++ ;
    break;
  case CCEModeLuminosity:
    dlmetrics->blendmodes.luminosity++ ;
    break;
  }
}
#endif /* METRICS_BUILD */

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
#include "ripdebug.h"       /* register_ripvar */

int32 backdrop_render_debug = 0;
int32 backdrop_debug_left = 0;
int32 backdrop_debug_right = MAXINT32 ;

char* groupusagelabels[] = {"page",
                            "sub",
                            "implicit",
                            "pattern",
                            "alpha softmask",
                            "luminosity softmask"};
#endif

#ifdef DEBUG_BUILD
static unsigned group_elim_new = 0;
static unsigned group_elim_hits = 0;
static unsigned group_elim_total = 0;
#endif


/* --Private macros-- */

#define GROUP_NAME "Group"


/* --Private datatypes-- */

struct Group {
  HDL *hdl; /* The Hierarchical Display List object for this Group. */
  DL_STATE *page ;  /* DL page which this instance is associated with */

  /* A pointer to the parent group.
  NOTE: There may be other constructs between groups (e.g. group->hdl->group
  nesting is possible), but this pointer will always point to the nearest
  containing Group object, therefore skipping any intervening constructs. */
  Group *parent ;

  Group *initialGroup ; /* Group used for initialising this group, or null */
  OBJECT colorspace;
  OBJECT colorSpaceArray[NUM_CSA_SIZE];
  COLORSPACE_ID processSpace;
  Bool processSubtractive;
  uint32 nProcessComps;
  uint32 nCurrentComps;

  GroupAttrs attrs; /**< essential state like isolated, knockout etc. Made
                         public through groupGetAttrs. */

  unsigned numNonIsolatedGroups; /**< Valid in page groups only. */

  Bool degenerate ;
  Bool insidePattern;
  Bool mustComposite; /**< The Group or a parent applies transparency to its elements */
  Bool blendModeWarning; /**< Controls non-separable blend mode warning. */
  Bool savedOverprintBlack;
  Bool disabledRecombineInterception;
  Bool insideSoftmask; /**< Is there a softmask among the ancestors? */
  GroupUsage groupusage;
  USERVALUE *softMaskBColor;
  uint32 nSoftMaskBCComps;
  FN_INTERNAL *softMaskTransfer;
  OBJECT *hdltTransferObj;
  int32 hdltTransferId;

  Preconvert *preconvert; /**< Preconvert object used to convert DL from input
                               blend space to group's output space. */
  CV_COLCVT *converter; /**< Converter object used to convert backdrop from
                             input blend space to group's output space. */
  Backdrop *backdrop; /**< Backdrop for the parts of the group requiring
                           compositing. */

  GUCR_RASTERSTYLE *inputRasterStyle;
  GUCR_RASTERSTYLE *outputRasterStyle;
  GS_COLORinfo *colorInfo;
  blit_colormap_t *blitmap ;
  blit_color_t erase_color;

  /* This structure maps DL colorant indices used in this group onto the lists
     of colorants output to the PGB header; this is only required for RLE
     output.  Note that we require two maps, one for the input raster style and
     one for the output raster style; the output map is only required for the
     page group, since composited output from all other groups is never sent to
     RLE. */
  RleColorantMap *inputRleColorantMap;

  unsigned depth;

  dbbox_t softMaskArea; /**< Bbox of the objects using this softmask group (or empty). */
  Bool fWillRetainSoftmask; /**< Is a softmask used for some object in the group? */
  Range extentOnPage; /**< Range of bands used, clipped to softMaskArea */
  /* The number of backdrops needed on a band affects the max size of the
     region set within a band that can be composited in one go, given
     limited resources. */
  unsigned *bandBackdrops; /**< Array of counts of backdrops needed for
                                each band of this group. */
  unsigned *bandBackdropsPage; /**< Array of counts of backdrops needed for
                                    each band of this page group. */

  LISTOBJECT *lobj; /**< Set if applicable to the group, but nulled once group
                         has been added to parent. */
  p_ncolor_t lobjColor; /**< Copy of lobj->p_ncolor for use after lobj is
                             nulled. */
  uint8 renderingIntent; /**< Copy of group's rendering intent for use after
                             lobj is nulled. */

  dl_color_t dlcErase; /**< The erase color converted to blend space. Used for
                            the composite-to-page operation and for the blit
                            color map's erase color. */

  OBJECT_NAME_MEMBER
};

struct group_tracker_t {
  Group *group ;            /**< Group currently being rendered. */
  group_tracker_t *parent ; /**< Parent group context. */
  Group *retainedSoftMask ; /**< For rendering, only one softmask backdrop retained at each group level. */
  dbbox_t retainedSoftMaskBounds ; /**< The part of the softmask retained. */
} ;

/**
 * Check for a standard PDF blend space.  CalGray, CalRGB and ICCBased have been
 * mapped to their device equivalent value.
 */
#define BLENDSPACE_IS_STANDARD(processSpace) \
  (processSpace == SPACE_DeviceGray || \
   processSpace == SPACE_DeviceRGB || \
   processSpace == SPACE_DeviceCMYK)

/**
 * Max number of components in a standard process blend space (excluding spots).
 */
#define MAX_BLENDSPACE_COMPS (4)

/* --Private prototypes-- */

static Bool groupRasterStyles(Group *group, GSTATE *gs);
static Bool renderRegions(Group *group,
                          render_info_t* renderInfo,
                          const dbbox_t *bandlimits,
                          Bool backdropRender,
                          DLRANGE *dlrange,
                          TranAttrib *toParent);
static void groupCommonTermination(Group *group);
static void groupCommonDestruction(Group *group);
static Bool groupMakeLobj(Group *group);
static unsigned groupBackdropsHere(Group *group);

Bool groupInsideSoftMask(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->insideSoftmask;
}

HDL *groupHdl(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->hdl;
}

/** Return a group's UID. */
uint32 groupId(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return hdlId(group->hdl);
}

/** Detach the HDL from the group, returning it. This is used to optimise groups
    when they have no effect, turning them into plain HDLs.

    Any immediate child groups will have the 'parent' pointer updated to point
    to the parent of this group, as this instance is no longer a real group. */
HDL *groupHdlDetach(Group *detach)
{
  HDL_LIST *hlist;
  HDL *hdl;

  VERIFY_OBJECT(detach, GROUP_NAME);

  for ( hlist = detach->page->all_hdls; hlist != NULL; hlist = hlist->next ) {
    Group *child = hdlGroup(hlist->hdl);
    if ( child != NULL ) {
      VERIFY_OBJECT(child, GROUP_NAME) ;
      if ( child->parent == detach )
        child->parent = detach->parent ;
      if ( child->initialGroup == detach ) {
        child->initialGroup = detach->initialGroup;
        /* This is right for complicated reasons. */
        if ( child->initialGroup == NULL && !child->attrs.isolated )
          /* If detach was isolated, the child is now isolated. */
          child->attrs.isolated = TRUE;
      }
    }
  }

  if ( detach->parent != NULL ) {
    detach->parent->attrs.hasShape |= detach->attrs.hasShape;
    detach->parent->fWillRetainSoftmask |= detach->fWillRetainSoftmask;
  }
#ifdef METRICS_BUILD
  dl_metrics()->groups.groupsEliminated++;
#endif
  hdl = detach->hdl;
  detach->hdl = NULL;
  hdlSetGroup(hdl, NULL);
  return hdl;
}

/** Return the page the group is associated with. */
DL_STATE *groupPage(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->page;
}


/** Find the bottom of the group stack. */
Group *groupBase(Group *group)
{
  for (;;) {
    VERIFY_OBJECT(group, GROUP_NAME);

    if ( group->parent == NULL )
      break ;

    group = group->parent ;
  }
  return group ;
}


/** Announce that there's an object with non-trivial shape in the group */
void groupAnnounceShapedObj(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  if ( group->attrs.knockoutDescendant )
    group->attrs.hasShape = TRUE;
}

static Bool groupLoadSoftMaskBC(Group *group, OBJECT *bcolor)
{
  HQASSERT(group->groupusage == GroupLuminositySoftMask,
           "Softmask BC applies to luminosity softmasks only");
  HQASSERT(group->nProcessComps > 0, "nProcessComps is zero");
  HQASSERT(BLENDSPACE_IS_STANDARD(group->processSpace),
           "Softmask should use a standard blend space");

  group->nSoftMaskBCComps = group->nProcessComps;
  group->softMaskBColor = dl_alloc(group->page->dlpools,
                                   sizeof(USERVALUE) * group->nSoftMaskBCComps,
                                   MM_ALLOC_CLASS_GROUP);
  if ( group->softMaskBColor == NULL )
    return error_handler(VMERROR);

  if ( bcolor != NULL ) {
    uint32 iComp;

    HQASSERT(oType(*bcolor) == OARRAY || oType(*bcolor) == OPACKEDARRAY,
             "Background color is not an array") ;
    HQASSERT(theLen(*bcolor) == group->nSoftMaskBCComps,
             "Background color has wrong number of components") ;
    for ( iComp = 0; iComp < group->nSoftMaskBCComps; ++iComp ) {
      if ( !object_get_real(&oArray(*bcolor)[iComp],
                            &group->softMaskBColor[iComp]) )
        return FALSE;
    }
  } else {
    /* Default backdrop color is black. */
    switch ( group->processSpace ) {
    case SPACE_DeviceGray:
      group->softMaskBColor[0] = 0;
      break;
    case SPACE_DeviceRGB:
      group->softMaskBColor[0] = group->softMaskBColor[1] =
        group->softMaskBColor[2] = 0;
      break;
    case SPACE_DeviceCMYK:
      group->softMaskBColor[0] = group->softMaskBColor[1] =
        group->softMaskBColor[2] = 0;
      group->softMaskBColor[3] = 1;
      break;
    default:
      HQFAIL("Invalid softmask process space");
      break;
    }
  }
  return TRUE;
}

/** Constructor.
*/
Bool groupOpen(DL_STATE *page,
               OBJECT colorspace,
               Bool isolated, Bool knockout, Bool banded,
               OBJECT *bcolor,
               OBJECT *xferfn,
               TranAttrib *patternTA,
               GroupUsage groupusage,
               Group **newGroup)
{
  Group *group;
  int i;

  static int32 softMaskTransferId = 0 ;

  HQASSERT(page != NULL, "page is null");
  HQASSERT(oType(colorspace) == ONULL ||
           oType(colorspace) == ONAME ||
           oType(colorspace) == OARRAY,
           "Colourspace for group is not valid");
  HQASSERT(groupusage < GroupNumUses, "groupusage out of range");
  HQASSERT(groupusage < NUM_ARRAY_ITEMS(groupusagelabels),
           "New GroupUsage needs adding to groupusagelabels");
  HQASSERT(newGroup != NULL, "newGroup is null");
  HQASSERT(bcolor == NULL || groupusage == GroupLuminositySoftMask,
           "Background color specified with non Luminosity softmask group") ;
  HQASSERT(xferfn == NULL || groupusage == GroupLuminositySoftMask ||
           groupusage == GroupAlphaSoftMask,
           "Transfer function specified with non softmask group") ;
  /* patternTA relates to the patterned object, which is not necessarily the
     same as the current tranAttrib. */
  HQASSERT((groupusage == GroupPattern) == (patternTA != NULL),
           "Pattern groups must supply the tranAttrib from a patterned object");
  HQASSERT(groupusage != GroupPage || page->currentGroup == NULL,
           "Trying to create a page group whilst other groups are still open");
  HQASSERT(groupusage == GroupPage || page->currentGroup != NULL,
           "Must have the page group in place before creating other groups");

  *newGroup = NULL;

  if ( groupusage != GroupPage ) {
    /* Ensure clipping is valid, but don't do any colour setup. This is not done
       for the page group, since the gstate may not have been set up yet. */
    if ( !DEVICE_SETG(page, GSC_UNDEFINED, DEVICE_SETG_GROUP) )
      return FALSE ;

#if defined( METRICS_BUILD )
    updateBlendModeMetrics(page->currentdlstate->tranAttrib, dl_metrics());
#endif
  }

  group = dl_alloc(page->dlpools, sizeof(Group), MM_ALLOC_CLASS_GROUP);
  if (group == NULL)
    return error_handler(VMERROR);

  /* Clear structure (NULL any pointers). */
  HqMemZero(group, sizeof(Group));

  /* Setup private data */
  group->hdl = NULL;
  group->page = page;
  group->parent = page->currentGroup ;

  /* The group color space is internalised into a form that is safe in the
     back end. Only device spaces and ICCBased are allowed. */
  group->colorspace = onull;           /* Struct copy to initialise slot properties */
  for (i = 0; i < NUM_CSA_SIZE; i++)
    group->colorSpaceArray[i] = onull; /* Struct copy to initialise slot properties */
  gsc_safeBackendColorSpace(&group->colorspace, &colorspace,
                            group->colorSpaceArray);

  group->processSpace = SPACE_notset;
  group->processSubtractive = FALSE;
  group->nProcessComps = 0;
  group->nCurrentComps = 0;

  group->insideSoftmask =
    groupusage == GroupAlphaSoftMask || groupusage == GroupLuminositySoftMask
    || (group->parent != NULL && group->parent->insideSoftmask) ;
  bbox_clear(&group->softMaskArea) ;

  group->fWillRetainSoftmask = FALSE;
  if ( groupusage == GroupAlphaSoftMask || groupusage == GroupLuminositySoftMask )
    group->parent->fWillRetainSoftmask = TRUE;

  group->attrs.isolated = isolated ;
  group->attrs.knockout = knockout;
  group->attrs.knockoutDescendant = knockout
    || (!(groupusage == GroupLuminositySoftMask
          || groupusage == GroupAlphaSoftMask)
        && group->parent != NULL && group->parent->attrs.knockoutDescendant);
  group->attrs.hasShape = FALSE;
  if ( groupusage == GroupLuminositySoftMask )
    group->attrs.softMaskType = LuminositySoftMask;
  else if ( groupusage == GroupAlphaSoftMask )
    group->attrs.softMaskType = AlphaSoftMask;
  else
    group->attrs.softMaskType = EmptySoftMask;
  group->attrs.compositeToPage = groupusage == GroupPage;
  group->attrs.lobjLCA = NULL;

  /* degenerateClipping only valid when setg above has been done (i.e., not for page group). */
  group->degenerate = ( degenerateClipping && groupusage != GroupPage ) ;
  HQASSERT(isolated ||
           (groupusage != GroupAlphaSoftMask &&
            groupusage != GroupLuminositySoftMask),
           "Alpha/Luminosity softmask group must be isolated") ;
  group->insidePattern = groupusage == GroupPattern
    || ( group->parent != NULL && group->parent->insidePattern );
  group->blendModeWarning = FALSE;
  group->savedOverprintBlack = gsc_getoverprintblack(gstateptr->colorInfo);
  group->disabledRecombineInterception = FALSE;
  group->groupusage = groupusage;
  group->nSoftMaskBCComps = 0;
  group->softMaskTransfer = NULL;
  group->hdltTransferId = 0;
  group->hdltTransferObj = NULL;
  group->lobj = NULL;
  group->lobjColor = NULL;
  dlc_clear(&group->dlcErase);
  group->blitmap = NULL ;

  /* Track group's depth, excluding group elimination.  page->groupDepth is the
     max group depth and is recalculated at the end of interpretation to include
     the effects of group elimination. */
  group->depth = groupBackdropsHere(group);
  if ( group->parent != NULL )
    group->depth += group->parent->depth;
  if ( group->depth > page->groupDepth )
    page->groupDepth = group->depth;

  group->initialGroup = NULL;
  if ( !isolated ) {
    /* For this non-isolated group, determine which
       group the initial backdrop is obtained from. */
    Group *parent = group->parent;

    HQASSERT(parent != NULL, "Non-isolated group must have parent");

    if ( parent->attrs.knockout )
      group->initialGroup = parent->initialGroup;
    else
      group->initialGroup = parent;

    /* A null initialGroup implies the group
       is equivalent to an isolated group. */
    if ( group->initialGroup == NULL )
      group->attrs.isolated = isolated = TRUE;
  }

  NAME_OBJECT(group, GROUP_NAME);

  /* Track the number of non-isolated groups, later adjusted for group
     elimination, to help with resource provisioning for compositing. */
  if ( !group->attrs.isolated ) {
    Group *base = groupBase(group);
    ++base->numNonIsolatedGroups;
  }

  /* Internalise the softmask transfer for later use in rendering.
     A value of /Identity or /Default gets translated by PDF code to an empty
     array which we should ignore */
  if (xferfn != NULL &&
      (oType(*xferfn) != OARRAY || theLen(*xferfn) != 0)) {
    mm_pool_t pool = dl_choosepool(page->dlpools, MM_ALLOC_CLASS_GROUP);
    if (!fn_create(xferfn, ++softMaskTransferId,
                   FN_SOFTMASK_TFR, pool,
                   &group->softMaskTransfer)) {
      groupDestroy(&group);
      return FALSE;
    }
    group->hdltTransferObj = xferfn;
    group->hdltTransferId = softMaskTransferId;
    HQASSERT(softMaskTransferId >= 0, "Ran out of soft mask transfer IDs") ;
  }

  /* Create HDL. */
  if ( !hdlOpen(page, banded, groupusage == GroupPage ? HDL_PAGE : HDL_GROUP,
                &group->hdl) ) {
    groupDestroy(&group);
    return FALSE;
  }
  hdlSetGroup(group->hdl, group);

  /* Group object valid from now on */

  if ( !groupRasterStyles(group, gstateptr) ) {
    groupDestroy(&group);
    return FALSE;
  }

  /* Disable color management for soft masks to ensure the mask is setup independent.
   * This will affect color in both the front and back ends.
   * We will rely on a grestore in the client to restore color management. */
  if ( groupusage == GroupAlphaSoftMask ||
       groupusage == GroupLuminositySoftMask ) {
    /* Force the overriding of device independent colour to the nearest device
     * space for alpha masks, don't override for luminosity masks */
    if ( !gsc_configPaintingSoftMask(gstateptr->colorInfo,
         groupusage == GroupAlphaSoftMask)) {
      groupDestroy(&group);
      return FALSE;
    }
  }

  /* Make a colorinfo for this group which converts objects from blend space to
     group output space. */
  if ( !groupResetColorInfo(group, gstateptr->colorInfo, page->colorState) ) {
    groupDestroy(&group);
    return FALSE;
  }

  /* Don't go through recombine interception for anything other than page groups
     or opaque patterns because:
     - recombine assumes the blend space of the page group;
     - we won't ever see a preseparated transparency job.
     Also the blend space must have a gray channel (see rcba_prepare_dl). */
  if ( rcbn_enabled() &&
       ((groupusage != GroupPage && groupusage != GroupPattern) ||
        (group->processSpace != SPACE_DeviceCMYK && group->processSpace != SPACE_DeviceGray) ||
        (group->parent != NULL && group->parent->disabledRecombineInterception)) ) {
    rcbn_disable_interception(gstateptr->colorInfo);
    group->disabledRecombineInterception = TRUE;
  }

  if ( groupusage == GroupLuminositySoftMask &&
       !groupLoadSoftMaskBC(group, bcolor) ) {
    groupDestroy(&group);
    return FALSE;
  }
  group->bandBackdrops = NULL;
  group->bandBackdropsPage = NULL;

  /* If asked, create a listobject for the group now, to get the stateobject
     correct before modifying the default transparency values. */
  if ( (groupusage == GroupPage || groupusage == GroupSubGroup ||
        groupusage == GroupImplicit) &&
       !groupMakeLobj(group) ) {
    groupDestroy(&group);
    return FALSE;
  }

  if ( groupusage != GroupPage &&
       !IDLOM_BEGINGROUP(groupId(group), group) ) {
    groupDestroy(&group) ;
    return FALSE ;
  }

  /* Make the new blend space the target for the gstate color chains. AFTER the
     setg for the group. */
  gsc_setTargetRS(gstateptr->colorInfo, group->inputRasterStyle);

  group->mustComposite =
    /* If the group's lobj applies transparency then the group's elements will
       need compositing. */
    (group->lobj != NULL && (group->lobj->marker & MARKER_TRANSPARENT) != 0) ||
    /* For pattern groups test the tranAttrib applied to the patterned object
       (there's no group->lobj). */
    (groupusage == GroupPattern && !tranAttribIsOpaque(patternTA)) ||
    /* Inherit mustComposite from the parent group. */
    (group->parent != NULL && group->parent->mustComposite) ||
    /* With this option, any object in any group can be preconverted to device
       space (not when single-phase compositing, though); without, preconversion
       is done only in groups which have a blend space that matches all parent
       groups down to the virtual device. */
    ((
#ifdef DEBUG_BUILD
      (backdrop_render_debug & BR_DISABLE_PRECONVERT_ALL) != 0 ||
#endif
      SystemParams.TransparencyStrategy == 1) &&
     group->parent != NULL &&
     group->inputRasterStyle != group->outputRasterStyle);

  /* Reset the transparency related attributes to their default values.
     Note, this is done _after_ creating the group's listobject to allow
     the current attributes to be associated with the group rather than
     the group's contents (PDF 1.4, 7.5.5, p452). */
  tsDefault(&gstateptr->tranState, gstateptr->colorInfo);

#ifdef METRICS_BUILD
  dl_metrics()->groups.groups++;
#endif

  probe_begin(SW_TRACE_DL_GROUP, (intptr_t)group) ;

  page->currentGroup = group;

  *newGroup = group;
  return TRUE;
}

/**
 * Make a listobject for the group to be able to add the group to its parent
 * group or HDL.  Note the dl color in the listobject is not actually used and
 * will be replaced later in groupSetLobjColor.  However, we're going to set the
 * color to white to avoid confusion with the black preservation code.
 */
static Bool groupMakeLobj(Group *group)
{
  dbbox_t bbox;

  VERIFY_OBJECT(group, GROUP_NAME);

  /* Set a default bbox for the group listobject.  The bbox is overridden later
     in groupClose when the group's contents is known. */
  bbox_store(&bbox, 0, 0, group->page->page_w - 1, group->page->page_h - 1);

  /* Page groups require only a basic stateobject, just to add the group to the
     base HDL, and the gstate may not yet be valid so setg isn't possible. */
  if ( group->groupusage == GroupPage ) {
    STATEOBJECT state = stateobject_new(group->page->default_spot_no);
    dlc_context_t *dlc_context = group->page->dlc_context;
    dl_color_t *dlc_current = dlc_currentcolor(dlc_context);
    COLORSPACE_ID groupColorSpace;

    if ( !setup_rect_clipping(group->page, &state.clipstate, &bbox) )
      return FALSE;

    /* ICCBased page groups need the rendering intent & color model */
    if ( !gsc_getcolorspacetype(&group->colorspace, &groupColorSpace) )
      HQFAIL("group->colorspace should be valid");
    if ( groupColorSpace == SPACE_ICCBased ) {
      LateColorAttrib lca = lateColorAttribNew();
      OBJECT *cs = &group->colorspace;
      if ( oType(*cs) == ONULL &&
           !gsc_getInternalColorSpace(group->processSpace, &cs) )
        return FALSE;
      if ( !gsc_colorModel(group->colorInfo, cs, &lca.origColorModel) )
        return FALSE;
      lca.renderingIntent = gsc_getICCrenderingintent(group->colorInfo);
      state.lateColorAttrib =
        (LateColorAttrib *)dlSSInsert(group->page->stores.state, &lca.storeEntry, TRUE);
      if ( state.lateColorAttrib == NULL )
        return FALSE;
    }

    group->page->currentdlstate =
      (STATEOBJECT *)dlSSInsert(group->page->stores.state, &state.storeEntry, TRUE);
    if ( group->page->currentdlstate == NULL )
      return FALSE;

    dl_currentexflags = 0;
    dl_currentdisposition = 0;
    dlc_release(dlc_context, dlc_current);
    dlc_get_white(dlc_context, dlc_current);
  } else {
    USERVALUE inputValues[MAX_BLENDSPACE_COMPS];
    COLORSPACE_ID processSpace = group->processSpace;
    uint32 nProcessComps = group->nProcessComps, iComp;

    if ( !BLENDSPACE_IS_STANDARD(group->processSpace) ) {
      /* Using a non-standard blend space, presumably device space, and
         gsc_setcolorspacedirect may not support it.  Just set a gray colorspace
         for now and groupSetLobjColor will change it later. */
      HQASSERT(!guc_backdropRasterStyle(group->inputRasterStyle),
               "All backdrop raster styles should have a standard blend space PCM");
      processSpace = SPACE_DeviceGray;
      nProcessComps = 1;
    }

    HQASSERT(nProcessComps <= MAX_BLENDSPACE_COMPS,
             "MAX_BLENDSPACE_COMPS too small");
    for ( iComp = 0; iComp < nProcessComps; ++iComp )
      inputValues[iComp] = group->processSubtractive ? 0.0f : 1.0f;

    if ( !gsc_setcolorspacedirect(gstateptr->colorInfo, GSC_BACKDROP,
                                  processSpace) ||
         !gsc_setcolordirect(gstateptr->colorInfo, GSC_BACKDROP,
                             inputValues) ||
         !DEVICE_SETG(group->page, GSC_BACKDROP, DEVICE_SETG_GROUP) )
      return FALSE;
  }

  if ( !make_listobject(group->page, RENDER_group, &bbox, &group->lobj) )
    return FALSE;

  /* The knockout flag is required when compositing a group into its parent,
     and to ensure any knockouts on the objects inside are drawn. */
  group->lobj->spflags |= RENDER_KNOCKOUT;

  group->attrs.lobjLCA = group->lobj->objectstate->lateColorAttrib;

  group->lobj->dldata.group = group;
  return TRUE;
}

/**
 * Make a dl color from the HDL's merged color and color-convert to parent blend
 * space if necessary.  The lobjColor is used in renderBackdrop() and also
 * optionally copied to the group lobj.
 */
static Bool groupSetLobjColor(Group *group)
{
  LISTOBJECT *lobj = group->lobj;
  dlc_context_t *dlc_context = group->page->dlc_context;
  dl_color_t dlc;

  if ( group->groupusage == GroupLuminositySoftMask ||
       group->groupusage == GroupAlphaSoftMask )
    return TRUE; /* lobjColor not required */

  dl_release(dlc_context, &group->lobjColor);

  if ( group->degenerate )
    /* Setting a none color means the renderer ignores the group. */
    dl_to_none(dlc_context, &group->lobjColor);
  else if ( group->preconvert == NULL ) {
    /* Group has same blend space as parent. */
    if ( !dlc_to_dl(dlc_context, &group->lobjColor,
                    hdlColor(group->hdl)) )
      return FALSE;
  } else {
    /* Convert merged color to parent's blend space. */
    LateColorAttrib dummyLCA = lateColorAttribNew();

    /* Use GSC_BACKDROP to avoid problems with implicit overprints. */
    if ( !preconvert_dlcolor(group, GSC_BACKDROP,
                             group->page->default_spot_no,
                             REPRO_TYPE_OTHER, &dummyLCA, FALSE, FALSE,
                             hdlColor(group->hdl)) ||
         !dlc_to_dl(dlc_context, &group->lobjColor,
                    dlc_currentcolor(dlc_context)) )
      return FALSE;
  }

  dlc_from_dl_weak(group->lobjColor, &dlc);
  if ( dlc_doing_maxblt_overprints(&dlc) ) {
    /* Need to remove max-blt overprint flags. The group itself doesn't do
       max-blting, only its elements. */
    if ( !dlc_clear_overprints(dlc_context, &dlc) )
      return FALSE;
    dlc_to_dl_weak(&group->lobjColor, &dlc);
  }

  /* If this page group has been partial painted then lobj will have been added
     to the DL already, and we need to find it and update its DL color in case
     more colorants have been added since. */
  if ( lobj == NULL && group->groupusage == GroupPage ) {
    DLRANGE dlrange;

    hdlDlrange(dlPageHDL(group->page), &dlrange);

    for ( dlrange_start(&dlrange); !dlrange_done(&dlrange) && lobj == NULL;
          dlrange_next(&dlrange) ) {
      LISTOBJECT *tlobj = dlrange_lobj(&dlrange);
      if ( tlobj->opcode == RENDER_group && tlobj->dldata.group == group )
        lobj = tlobj;
    }
    HQASSERT(lobj != NULL, "Failed to find page group's DL object");
  }

  /* Update lobj if present. */
  if ( lobj != NULL ) {
    dl_release(dlc_context, &lobj->p_ncolor);
    if ( !dl_copy(dlc_context, &lobj->p_ncolor,
                  &group->lobjColor) )
      return FALSE;

    if ( group->lobj == NULL ) {
      /* lobj already on the dl, so hdlAdd won't be called. */
      if ( !hdlMergeColorUpdate(group->hdl, lobj) )
        return FALSE;
    }
  }

  return TRUE;
}

/**
 * Close the group.  If it's appropriate, a listobject is created and then added
 * to the containing HDL or Group.  The group's parent becomes currentGroup
 * again.  The partialClose flag is set when partial painting in a page group.
 * In this case the group (and its hdl) must be left open ready to continue
 * adding more DL objects after the partial render.
 */
Bool groupCloseAny(Group **groupPointer, Bool partial, Bool success)
{
  DL_STATE *page;
  Group *group;
  HDL *hdl;
  dbbox_t *bbox = NULL;

  HQASSERT(groupPointer != NULL, "groupClose - 'groupPointer' cannot be NULL");
  group = *groupPointer;
  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(group->groupusage == GroupPage || !partial,
           "Only the page group can be partially closed");

  hdl = group->hdl;
  page = group->page;

  if ( group->groupusage != GroupPage && !IDLOM_ENDGROUP(success) )
    success = FALSE ;

  if ( !finishaddchardisplay(page, 1) )
    success = error_handler( VMERROR ) ;

  if ( success ) {
    if ( !flush_vignette(VD_Default) )
      success = FALSE ;
  } else
    abort_vignette(page) ;

  /* Check if recombine was enabled on this page group. */
  if ( success && group->groupusage == GroupPage && !hdlIsEmpty(hdl) &&
       rcbn_enabled() && !group->disabledRecombineInterception )
    /* Turn off recombine interception until the next rcbn_beginpage and finish
       recombine processing on the DL (the "Recombining separations" phase). */
    if ( !rcbn_endpage(page, group, hdlRecombined(hdl),
                       group->savedOverprintBlack) )
      success = FALSE;

  /* Determine dlcErase before the input rasterstyle is cleared in
     groupCommonTermination. Use the erase color proc when the blend space is
     device space, and plain white otherwise. */
  dlc_release(page->dlc_context, &group->dlcErase);
  if ( success && !DEVICE_INVALID_CONTEXT() &&
       (group->groupusage == GroupPage ||
        !guc_backdropRasterStyle(group->inputRasterStyle)) )
    success = gs_applyEraseColor(&CoreContext, TRUE, &group->dlcErase, NULL, FALSE);
  else
    dlc_get_white(page->dlc_context, &group->dlcErase);

  if ( !partial ) {
    /* Remove the group from the current group stack. */
    groupCommonTermination(group) ;
    if ( *groupPointer == NULL )
      groupPointer = &group;

    if ( !hdlClose(&group->hdl, success) )
      success = FALSE ;

    /* Don't calc bbox if already degenerate (also clipstate will be null).  If
       partial painting, leave the bbox as default and add the lobj to all
       bands. */
    if ( success && group->lobj != NULL && !group->degenerate ) {
      bbox = &group->lobj->bbox;
      hdlBBox(hdl, bbox);
      bbox_intersection(bbox, &group->lobj->objectstate->clipstate->bounds, bbox);
    }
  }

  probe_end(SW_TRACE_DL_GROUP, (intptr_t)group) ;

  if ( !success ) {
    groupDestroy(groupPointer);
    return FALSE ;
  }

  /* Look for degenerate groups, i.e. those that are clipped out entirely, and
     destroy them (after all the HDLT group callbacks).  Must retain softmask
     groups even if they are degenerate to ensure the default alpha (possibly
     derived from the background color) is setup correctly.  Can't destroy an
     empty group if doing a partial close, more objects may be added later. */
  if ( !partial && !groupIsSoftMask(group) &&
       (group->degenerate || hdlIsEmpty(hdl) ||
        (bbox != NULL && bbox_is_empty(bbox))) ) {
    groupDestroy(groupPointer) ;
#ifdef METRICS_BUILD
    dl_metrics()->groups.groupsEliminated++;
#endif
    return TRUE ;
  }

  /* Now the group is closing the set of colorants in the HDL is fixed. */
  if ( group->preconvert != NULL &&
       !preconvert_update(page, group->preconvert) ) {
    groupDestroy(groupPointer);
    return FALSE ;
  }

  /* Make lobjColor from the HDL merge color and convert to parent blend space.
     Used to render the backdrop and for the optional lobj added to the DL. */
  if ( !groupSetLobjColor(group) ) {
    groupDestroy(groupPointer);
    return FALSE ;
  }

  /* If a DL object exists then add it to the DL.  If the group can be optimised
     away then add an HDL object instead of Group object. */
  if ( group->lobj != NULL ) {
    Bool eliminate = FALSE, destroyGroup = FALSE, added = FALSE;
    LISTOBJECT *lobj;
    HDL *savedTargetHdl = NULL;

    if ( !partial &&
         !groupEliminate(group, group->lobj->objectstate->tranAttrib, &eliminate) ) {
      groupDestroy(groupPointer);
      return FALSE;
    }
    if ( eliminate ) {
      /* Patch the listobject from a Group to a normal HDL. */
      HDL *detachedHdl = groupHdlDetach(group);
      HQASSERT(detachedHdl == hdl, "Detached the wrong HDL");
      HQASSERT(group->lobj->opcode == RENDER_group, "group not a RENDER_group");
      group->lobj->opcode = RENDER_hdl;
      group->lobj->dldata.hdl = detachedHdl;
      destroyGroup = TRUE;
    }

    /* We are about to attempt to attach the listobject to the parent
       HDL. Whether it succeeds or not, we shouldn't try to free it
       ourselves. */
    lobj = group->lobj;
    group->lobj = NULL;

    if ( partial ) {
      /* Want to add the page group lobj to the base hdl without closing the
         group.  Since hdlClose hasn't been called we need to temporarily change
         the targetHdl. */
      savedTargetHdl = page->targetHdl;
      page->targetHdl = hdlBase(page->targetHdl);
    }

    /* Add the Group or HDL object to the display list. */
    success = add_listobject(page, lobj, &added);

    if ( savedTargetHdl )
      page->targetHdl = savedTargetHdl;

    if ( !success ) {
      groupDestroy(groupPointer);
      return FALSE;
    }

    if ( !added )
      /* This can happen if the page target HDL is null: the group has already
         been destroyed by the free_dl_object call in add_listobject. */
      return TRUE;

    if ( destroyGroup ) {
      /* Group not needed for transparency and is now freed. */
      groupDestroy(groupPointer);
      return TRUE;
    }
  }

  if ( group->attrs.hasShape ) {
#ifdef METRICS_BUILD
    dl_metrics()->groups.groupsStoringShape++;
#endif
    if ( group->parent != NULL && group->parent->attrs.knockoutDescendant )
      group->parent->attrs.hasShape = TRUE;
  }

  /* PS objects are not allowed in the back-end. */
  group->hdltTransferObj = NULL;

  HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
          ("%sisolated, %sknockout group depth %u %s",
           (group->attrs.isolated ? "" : "non-"),
           (group->attrs.knockout ? "" : "non-"),
           group->depth,
           (groupIsSoftMask(group) ? "for softmask" : "")));

#ifdef DEBUG_BUILD
  if ( group->groupusage == GroupPage &&
       (backdrop_render_debug & BR_DEBUG_TRACE) != 0 )
    monitorf((uint8*)"Group elimination totals: %u/%u/%u",
             group_elim_new, group_elim_hits, group_elim_total);
#endif

  return TRUE ;
}

/**
 * Callback for DL iteration that checks that the object is suitable
 * for rendering in self-intersect mode.
 */
static Bool dlIterSelfIntersectable(DL_FORALL_INFO *info)
{
  STATEOBJECT **pState = info->data;
  STATEOBJECT *objState = info->lobj->objectstate;

  /* If any object is patterned, the group must have been eliminated,
     and it can't be made transparent anymore. */
  if (objState->patternstate != NULL)
    return FALSE;

  if (*pState == NULL) { /* no state seen yet */
    *pState = objState;
    return TRUE;
  } else
      /* Can't have different complex clips; rectangular ones are OK
         (this is slightly too tight, but we'll probably improve the
         self-intersection code, so no point in honing this). */
      return *pState == objState
        || (*pState)->clipstate == objState->clipstate
        || (clipIsRectangular((*pState)->clipstate)
            && clipIsRectangular(objState->clipstate));
}

/** Callback for DL iteration that puts the given transparency state on
    all objects.
    Returns FALSE on error, TRUE otherwise. */
static Bool dlIterPutTransparency(DL_FORALL_INFO *info)
{
  LISTOBJECT *lobj = info->lobj;
  STATEOBJECT new_state;

  HQASSERT(lobj->objectstate->patternstate == NULL
           || lobj->objectstate->patternstate->opcode == RENDER_group,
           "Can't change tranAttrib on this patterned object");
  if (lobj->opcode == RENDER_hdl)
    return TRUE;
  /* Modify a copy of the state, insert it in the state store, and
     install the stored state in the object. */
  new_state = *lobj->objectstate;
  new_state.tranAttrib = (TranAttrib *)(info->data);
  lobj->objectstate = (STATEOBJECT *)dlSSInsert(info->page->stores.state,
                              &new_state.storeEntry, TRUE);
  if ( lobj->objectstate == NULL )
    return FALSE;
  lobj->marker |= MARKER_TRANSPARENT | MARKER_OMNITRANSPARENT;
  hdlSetTransparent(info->hdl);
  /* If lobj has acquired a softmask then update the softmask area used.  This
     is normally done when the object is added to the DL, but that's already
     happened. */
  if ( new_state.tranAttrib->softMask != NULL )
    groupSoftMaskAreaExpand(new_state.tranAttrib->softMask->group, &lobj->bbox);
  return TRUE;
}

/** The tranAttrib applied to the group has been move to the sub-elements and
    therefore should be cleared from the group's lobj.
    Returns FALSE on error, TRUE otherwise. */
static Bool clearTransparency(Group *group)
{
  if ( group->lobj != NULL &&
       !tranAttribIsOpaque(group->lobj->objectstate->tranAttrib) ) {
    TranAttrib tranDefault;
    STATEOBJECT new_state;

    tranDefault.storeEntry.next = NULL ;
    tranDefault.blendMode = CCEModeNormal ;
    tranDefault.alphaIsShape = FALSE ;
    tranDefault.alpha = COLORVALUE_ONE ;
    tranDefault.softMask = NULL ;

    new_state = *group->lobj->objectstate;
    new_state.tranAttrib =
      (TranAttrib*)dlSSInsert(group->page->stores.transparency,
                              &tranDefault.storeEntry, TRUE);
    if ( new_state.tranAttrib == NULL )
      return FALSE;
    group->lobj->objectstate =
      (STATEOBJECT *)dlSSInsert(group->page->stores.state,
                                &new_state.storeEntry, TRUE);
    if ( group->lobj->objectstate == NULL )
      return FALSE;
    group->lobj->marker &= ~(MARKER_TRANSPARENT | MARKER_OMNITRANSPARENT);
  }

  return TRUE;
}

/** Combine transparency attributes from a group and its object into one,
    if possible. */
static Bool combineTransparency(TranAttrib *newTA, Group *group,
                                TranAttrib *groupTA, TranAttrib *objTA)
{
  HQASSERT(groupTA != NULL, "Can't get here with non-transparent group");
  if ( objTA == NULL )
    *newTA = *groupTA; /* Only one ta to combine -- that was simple. */
  else {
    if ( (group->attrs.isolated || objTA->blendMode == CCEModeNormal) &&
         groupTA->alphaIsShape == objTA->alphaIsShape ) {
      /* In these cases, the group's internal composition results in
         just the object's color and alpha, which are then composited to
         the parent according to the group's ta. */
      newTA->blendMode = groupTA->blendMode;
      newTA->alphaIsShape = groupTA->alphaIsShape;
      cceMultiplyAlpha(1, &objTA->alpha, groupTA->alpha, &newTA->alpha);
    } else
      return FALSE;

    /* Softmask works by multiplying into the alpha, which the above
       clause has decided can be combined.  Can't be bothered to write a
       procedure for multiplying softmasks; instead, if there's one
       softmask between them, take that; give up on two. */
    if ( objTA->softMask != NULL )
      if ( groupTA->softMask != NULL )
        return FALSE;
      else
        newTA->softMask = objTA->softMask;
    else
      newTA->softMask = groupTA->softMask;
  }
  return TRUE;
}


/** Callback for DL iteration that checks if the latecolor state of objects are
    different from the group, preventing the group from being eliminated.
    Returns TRUE for matching latecolor. */
static Bool dlIterMatchingLateColor(DL_FORALL_INFO *info)
{
  LISTOBJECT *lobj = info->lobj;
  LateColorAttrib *objLCA = lobj->objectstate->lateColorAttrib;
  LateColorAttrib *groupLCA = info->data;

  return lobj->opcode == RENDER_hdl ||
         (objLCA->renderingIntent == groupLCA->renderingIntent &&
          objLCA->origColorModel == groupLCA->origColorModel);
}

/**
 * Called at group closing time to determine if the group is really
 * needed.  If it doesn't actually affect anything, the caller will
 * change it to an HDL, which will save a lot of compositing.
 *
 * Group elimination requires updating parent and initialGroup
 * pointers.  For the latter, each elimination below must justify why
 * updating to group->initialGroup is right.
 *
 * Returns FALSE on error, TRUE otherwise.
 * 'eliminate' is returned as TRUE if the group can be eliminated.
 */
Bool groupEliminate(Group *group, TranAttrib *groupTA, Bool *eliminate)
{
  LISTOBJECT *singleObj = NULL;
  Bool groupIsOpaque = tranAttribIsOpaque(groupTA);
  DL_FORALL_INFO info;

  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(!groupIsSoftMask(group), "Can't be used on softmask groups");

  *eliminate = FALSE;

#ifdef DEBUG_BUILD
  ++group_elim_total;

  if ( (backdrop_render_debug & BR_DISABLE_GROUP_ELIMINATION) != 0 )
    return TRUE;
#endif

  /* To eliminate the page group we'd need to be sure it's not needed for
     overprinting, PCL ropping etc.  Different rasterstyles mean any group is
     required. */
  if ( group->groupusage == GroupPage ||
       group->inputRasterStyle != group->outputRasterStyle )
    return TRUE;

  /* Objects normally inherit the rendering intent from the group which we deal
     with elsewhere. An exception is made for groups painted directly into the
     page group, which we can't eliminate if the rendering intent of a contained
     object is different to the group. If we did eliminate the group, we could
     use the wrong intent when color managing the output colors. */
   if ( group->lobj != NULL && group->parent != NULL &&
        group->parent->groupusage == GroupPage ) {
     DL_FORALL_INFO info;
     info.page    = group->page;
     info.hdl     = group->hdl;
     info.data    = group->lobj->objectstate->lateColorAttrib;
     info.inflags = DL_FORALL_SHFILL|DL_FORALL_PATTERN;

     if ( !dl_forall(&info, dlIterMatchingLateColor) )
       return TRUE;
   }

  /* If the group doesn't contribute any transparency and none of the
     objects in it do, it can be eliminated.  Justification for
     initialGroup: No transparency, no initialGroup ptrs. */
  if ( groupIsOpaque && !hdlTransparent(group->hdl) ) {
#ifdef DEBUG_BUILD
    ++group_elim_hits;
#endif
    HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
            ("Eliminated non-transparent group"));
    *eliminate = TRUE;
    return TRUE;
  }

  /* Unless the whole group hierarchy is completely opaque the page group
     cannot be eliminate. */
  if ( group->groupusage == GroupPage )
    return TRUE;

  /* The end of section 7.3.3 in the PDFRM states the effect of compositing
     objects as a group is the same as that of compositing them separately
     (without grouping) when this set of conditions hold:
     - The group is non-isolated and has the same knockout attribute as its
       parent group. (This rule doesn't need to be applied for groups
       containing a single non-self-intersecting object.)
     - When compositing the group's results with the group backdrop, the Normal
       blend mode is used, and the shape and opacity inputs are always 1.0.

     Justification for initialGroup: The conditions above cause the
     parent to have the same state this group would have had.  Turns out
     if this group is an initial backdrop for some group, then
     group->initialGroup = the parent, because an initial backdrop
     must be a non-knockout group, so the parent must also be. */
  if ( groupIsOpaque && !group->attrs.isolated &&
       group->attrs.knockout == group->parent->attrs.knockout ) {
#ifdef DEBUG_BUILD
    ++group_elim_hits;
#endif
    HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
            ("Eliminated ineffectual group"));
    *eliminate = TRUE;
    return TRUE;
  }

  /* If the group contains a single non-self-intersecting object, the
     knockout and isolated flags don't actually matter.  Recognising
     this is particularly beneficial for pattern type 2 shfills which
     create knockout groups.  Justification for initialGroup: During
     the only obj, the group's state is still its initial background. */
  singleObj = hdlSingleObject(group->hdl);
  if ( groupIsOpaque && singleObj != NULL ) {
#ifdef DEBUG_BUILD
    ++group_elim_hits;
#endif
    HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
            ("Eliminated single-object group"));
    *eliminate = TRUE;
    return TRUE;
  }

  /* If there's a single non-self-intersecting object in the group, try
     to create a single transparency state that combines those of the
     object and the group, attach it to the object, and eliminate the
     group.  However, if the object is patterned but its group has been
     eliminated, it can't be made transparent anymore.  Justification
     for initialGroup: As above. */
  if ( singleObj != NULL
       && !(singleObj->objectstate->patternstate != NULL
            && singleObj->objectstate->patternstate->opcode == RENDER_hdl) ) {
    TranAttrib newTA;

    if ( combineTransparency(&newTA, group, groupTA,
                             singleObj->objectstate->tranAttrib) ) {
      /* Insert the new attributes in the state store, and install it
         in the object (and in its sub-DL for shfills). */
      info.page    = group->page;
      info.hdl     = group->hdl;
      info.data    = dlSSInsert(group->page->stores.transparency,
                                &newTA.storeEntry, TRUE);
      if ( info.data == NULL)
        return FALSE;
      info.inflags = DL_FORALL_SHFILL|DL_FORALL_NONE;

      if ( !dl_forall(&info, dlIterPutTransparency) )
        return FALSE;
#ifdef DEBUG_BUILD
      ++group_elim_hits;
#endif
      if ( !clearTransparency(group) )
        return FALSE;
      HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
              ("Eliminated simple group, combining with its only object"));
      *eliminate = TRUE;
      return TRUE;
    }
  }

  /* If all objects are opaque (but the group isn't or it would have
     been eliminated above), they don't really composite against each
     other.  By rendering the DL in self-intersect mode, they can't, so
     they handle like a single object.  The group can be eliminated and
     the transparency can be moved to each obj, as above.  Justification
     for initialGroup: No transparency, no initialGroup ptrs. */
  if (
#ifdef DEBUG_BUILD
       (backdrop_render_debug & BR_DISABLE_GROUP_ELIM_INTERSECT) == 0 &&
#endif
       !hdlTransparent(group->hdl) && !hdlOverprint(group->hdl) ) {
    STATEOBJECT *state = NULL;
    HDL *hdl = group->hdl;

    info.page    = group->page;
    info.hdl     = hdl;
    info.data    = &state;
    info.inflags = DL_FORALL_SHFILL|DL_FORALL_NONE;

    if ( dl_forall(&info, dlIterSelfIntersectable) ) {
      info.data  = groupTA;

      /* Note don't call clearTransparency() on the HDL.  If there's a softmask
         in groupTA it needs to be composited at the HDL level instead of on the
         sub-elements, otherwise there's a risk of attempting to use
         intersectingclipform recursively when the softmask contains a shfill
         with self-intersecting also enabled.  The softmask backdrop will be
         composited and retained for the sub-elements. */
      if ( !dl_forall(&info, dlIterPutTransparency) )
        return FALSE;
      if ( !hdlSetSelfIntersect(hdl) )
        return FALSE;
#ifdef DEBUG_BUILD
      ++group_elim_new;
#endif
      HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
              ("Eliminated group, pushing transparency onto opaque objects"));
      *eliminate = TRUE;
      return TRUE;
    }
  }
  return TRUE;
}

/**
 * Returns true if the Group must be composited.
 */
Bool groupMustComposite(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->mustComposite;
}

/**
 * Can only allow non-separable blend modes when compositing in a standard blend
 * space; if compositing in device space with a non-standard PCM (eg DeviceN)
 * then override the blend mode with normal. This overriding can be avoided by
 * enabling OverprintPreview.
 */
uint32 groupOverrideBlendMode(Group *group, uint32 blendMode)
{
  switch ( group->processSpace ) {
  case SPACE_DeviceGray:
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGB:
    return blendMode;
  default : HQFAIL("Unknown process space");
  case SPACE_Lab:
  case SPACE_DeviceCMY:
  case SPACE_DeviceRGBK:
  case SPACE_DeviceN: {
    Group *base = groupBase(group);

    /* Can only have a non-standard blend space when OverprintProcess is false. */
    HQASSERT(!gsc_getOverprintPreview(gstateptr->colorInfo),
             "Expected OverprintPreview to be disabled");

    if ( cceBlendModeIsSeparable(blendMode) )
      return blendMode;
    else {
      if ( !base->blendModeWarning ) {
        monitorf(UVS("%%%%[ Warning: Overriding non-separable blend mode with Normal - "
                     "OverprintPreview is disabled ]%%%%\n"));
        base->blendModeWarning = TRUE;
      }
      return CCEModeNormal;
    }
   }
  }
}

/** Special light destructor to remove permanent memory allocated for groups.
   This is called just before destroying a whole DL pool. If you do want to
   destroy a group and free its DL memory, call groupDestroy(). */
void groupAbandon(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);

  /* Destroy any objects in permanent memory. This also removes the
     rasterstyle from the gstate stack if necessary. */
  groupCommonDestruction(group);
}

/**
 * Abandon groups after compositing.
 */
void groupAbandonAll(DL_STATE *page)
{
  HDL_LIST *hlist;

  HQASSERT(page->currentGroup == NULL ||
           page->currentGroup->groupusage == GroupPage,
           "Expected sub-groups to have been closed");

  for ( hlist = page->all_hdls; hlist != NULL; hlist = hlist->next ) {
    Group *group = hdlGroup(hlist->hdl);

    if ( group != NULL )
      groupAbandon(group);
  }
}

/** Destructor.
*/
void groupDestroy(Group **groupPointer)
{
  Group *group;

  HQASSERT(groupPointer != NULL, "groupDestroy - 'groupPointer' cannot be NULL");
  group = *groupPointer;
  if ( group == NULL )
    return;

  VERIFY_OBJECT(group, GROUP_NAME);

  if ( !group->attrs.isolated ) {
    Group *base = groupBase(group);
    HQASSERT(base->numNonIsolatedGroups > 0, "numNonIsolatedGroups gone wrong");
    --base->numNonIsolatedGroups;
  }

  groupCommonTermination(group);

  *groupPointer = NULL; /* Must be done after groupCommonTermination in case
                          groupPointer refers to page->currentGroup */

  /* Destroy any objects in permanent memory. This also removes the
     rasterstyle from the gstate stack if necessary. */
  groupCommonDestruction(group) ;

  /* Destroy objects in DL memory. */
  dl_release(group->page->dlc_context, &group->lobjColor);
  dlc_release(group->page->dlc_context, &group->dlcErase);

  if ( group->inputRleColorantMap != NULL )
    rleColorantMapDestroy(&group->inputRleColorantMap);

  HQASSERT(group->blitmap == NULL, "blitmap freed in groupCommonDestruction");

  if ( group->bandBackdrops != NULL )
    dl_free(group->page->dlpools, group->bandBackdrops,
            sizeof(unsigned) * group->extentOnPage.length, MM_ALLOC_CLASS_GROUP);
  if ( group->bandBackdropsPage != NULL )
    dl_free(group->page->dlpools, group->bandBackdropsPage,
            sizeof(unsigned) * group->extentOnPage.length, MM_ALLOC_CLASS_GROUP);

  hdlDestroy(&group->hdl);

  HQASSERT(group->converter == NULL, "converter freed in groupCommonDestruction");

  group->backdrop = NULL; /* backdrops are freed by bd_sharedFree. */

  if ( group->colorInfo != NULL )
    dl_free(group->page->dlpools, group->colorInfo, gsc_colorInfoSize(),
            MM_ALLOC_CLASS_GROUP);

  if ( group->softMaskTransfer != NULL )
    fn_destroy(group->softMaskTransfer);

  if ( group->lobj != NULL )
    free_listobject(group->lobj, group->page) ;

  UNNAME_OBJECT(group);

  dl_free(group->page->dlpools, group, sizeof(Group), MM_ALLOC_CLASS_GROUP);
}

/** Device rasterstyle has changed. Reset all the references to the device
 *  rasterstyle in this group.
 */
Bool groupResetDeviceRS(Group *group)
{
  DL_STATE *page;

  VERIFY_OBJECT(group, GROUP_NAME);
  page = group->page;

  /* Update input and output device rasterstyles in the group. */
  if ( !guc_backdropRasterStyle(group->inputRasterStyle) ) {
    guc_reserveRasterStyle(page->hr);
    guc_discardRasterStyle(&group->inputRasterStyle);
    group->inputRasterStyle = page->hr;
  }
  if ( !guc_backdropRasterStyle(group->outputRasterStyle) ) {
    guc_reserveRasterStyle(page->hr);
    guc_discardRasterStyle(&group->outputRasterStyle);
    group->outputRasterStyle = page->hr;
  }

  /* Update target and device rasterstyles. */
  gsc_setTargetRS(group->colorInfo, group->outputRasterStyle);
  gsc_setDeviceRS(group->colorInfo, page->hr);

  HQASSERT(group->converter == NULL, "converter may be out of date now");

  if ( group->inputRasterStyle != group->outputRasterStyle ) {
    if ( group->preconvert != NULL )
      preconvert_free(page, &group->preconvert);
    if ( !preconvert_new(page, group->colorInfo, group->colorspace,
                         group->processSpace, group->nProcessComps,
                         group->inputRasterStyle, &group->preconvert) )
      return FALSE;
  }

  return TRUE;
}

/**
 * - Replace the existing colorInfo with the new one which may include changes
 *   following a setinterceptcolorspace in the BeginPage hook.
 * - Replace all the frontend colorInfos with backend colorInfos attached to the
 *   given colorState.
 * - Update any device rasterstyle references in case the device rs has been
 *   copied ready for the backend.  The rs copy is necessary for pipelining of
 *   interpretation and rendering.
 */
Bool groupResetColorInfo(Group *group, GS_COLORinfo *newColorInfo,
                         COLOR_STATE *colorState)
{
  GS_COLORinfo *copyColorInfo;
  DL_STATE *page;

  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(colorState != NULL, "Color state missing");
  page = group->page;

  copyColorInfo = dl_alloc(page->dlpools, gsc_colorInfoSize(),
                           MM_ALLOC_CLASS_GROUP);
  if ( copyColorInfo == NULL)
    return error_handler(VMERROR);

  /* Frontend and backend transforms have separate colorStates to handle
     pipelining.  Add the copied colorInfo to the given colorState.  If
     colorState is null the copy is attached to the same colorState as
     before. */
  if ( !gsc_copycolorinfo_withstate(copyColorInfo, newColorInfo, colorState) )
    return FALSE;
  if ( group->colorInfo != NULL ) {
    gsc_freecolorinfo(group->colorInfo);
    dl_free(page->dlpools, group->colorInfo, gsc_colorInfoSize(),
            MM_ALLOC_CLASS_GROUP);
  }
  group->colorInfo = copyColorInfo;

  /* Set the transparency state to default when doing preconverting to ensure
     devicecode c-link and other places which test on this do not have
     unexpected behaviour (it's unsafe to leave it floating at whatever it was
     set to last).  The preconvert stage uses the colorInfo in the group as
     preconversion does conversion to group->outputRasterStyle. */
  gsc_setOpaque(group->colorInfo, TRUE, TRUE);

  /* OverprintMode & OverprintBlack is not done on back-end color chains, it's a
     front-end thing. */
  if ( !gsc_setignoreoverprintmode(group->colorInfo, FALSE) ||
       !gsc_setoverprintmode(group->colorInfo, FALSE) ||
       !gsc_setoverprintblack(group->colorInfo, FALSE) )
    return FALSE;

  /* Transfer functions are applied in the frontend and therefore we don't want
     to apply them again in the backend when doing recombine adjustment or
     compositing. */
  if ( !gsc_setTransfersPreapplied(group->colorInfo, TRUE) )
    return FALSE;

  /* Device RS may have changed. */
  if ( !groupResetDeviceRS(group) )
    return FALSE;

  return TRUE;
}

/**
 * Free as much memory as possible after a partial paint.
 * Returns TRUE if the group was completely destroyed.
 */
Bool groupCleanupAfterPartialPaint(Group **groupPointer)
{
  Group *group;

  HQASSERT(groupPointer != NULL, "groupPointer cannot be NULL");
  group = *groupPointer;

  VERIFY_OBJECT(group, GROUP_NAME);

  if ( hdlCleanupAfterPartialPaint(&group->hdl) &&
       group->groupusage != GroupPage ) {
    /* HDL removed completely - finish with the group. */
    HQASSERT(group->groupusage != GroupPage,
             "Page group is open and cannot be completely removed");
    groupDestroy(groupPointer);
    return TRUE;
  }

  /* Partial paint is restricted to the page group. Cannot partial paint when in
     a sub-group and any sub-groups already on the DL should have been closed
     and removed completely above. */
  HQASSERT(group->groupusage == GroupPage,
           "Only the page group can continue after a partial paint");

  /* Free everything used for the partial paint. */

  group->backdrop = NULL; /* backdrops are freed by bd_sharedFree. */

  if ( group->converter != NULL )
    cv_colcvtfree(group->page, &group->converter);

  if ( group->blitmap != NULL )
    blit_colormap_destroy(&group->blitmap,
                          dl_choosepool(group->page->dlpools,
                                        MM_ALLOC_CLASS_BLIT_COLORMAP)) ;

  /* group->preconvert is still valid, if present. */
  if  ( group->inputRasterStyle != group->outputRasterStyle )
    HQASSERT(group->preconvert != NULL, "group->preconvert is missing");

  dl_release(group->page->dlc_context, &group->lobjColor);
  dlc_release(group->page->dlc_context, &group->dlcErase);

  HQASSERT(gsc_getTargetRS(gstateptr->colorInfo) == group->inputRasterStyle,
           "Target RS should still be set to the page group's blend space RS");

  /* This Group was not completely removed, so return FALSE */
  return FALSE ;
}

Preconvert *groupPreconvert(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(group->preconvert ||
           group->inputRasterStyle == group->outputRasterStyle,
           "Expected to have a preconvert object ready");
  return group->preconvert;
}

Group *groupParent(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->parent;
}

/**
 * Setup the input colorspace for the backdrop.
 */
static Bool groupSetBackdropColorSpace(Group *group,
                                       COLORANTINDEX *inColorants, uint32 inComps)
{
  GS_COLORinfo *colorInfo = group->colorInfo;

  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(group->blitmap->ncolors == inComps,
           "Blitmap and inComps must map the same set of colorants");
  HQASSERT(group->outputRasterStyle == gsc_getTargetRS(colorInfo),
           "Group's colorInfo not set up correctly");

  /* First set the associated profile which comes from the colorspace for the
     source group. */
  if ( !gsc_setAssociatedProfile(colorInfo, group->processSpace,
                                 (gsc_isInvertible(colorInfo, &group->colorspace)
                                  ? &group->colorspace : &onull)) )
    return FALSE;

  if ( inComps != group->nProcessComps ) {
    /* Converting spots; or both process and spot colorants;
       or a subset of the process */
    if ( !gsc_spacecache_setcolorspace(colorInfo,
                                       group->inputRasterStyle,
                                       GSC_BACKDROP,
                                       inComps, inColorants,
                                       TRUE, /* fCompositing */
                                       guc_getCMYKEquivalents,
                                       NULL) )
      return FALSE;
  } else {
    if ( !gsc_setcolorspacedirectforcompositing(colorInfo, GSC_BACKDROP,
                                                group->processSpace) )
      return FALSE;
  }

  /** \todo @@@ TODO FIXME The luminosity flag must be set AFTER the color
      space since the color space will reset the flag to default when it
      builds a new chain head... */
  return gsc_setoverprint(colorInfo, GSC_BACKDROP, FALSE) &&
         gsc_setLuminosityChain(colorInfo, GSC_BACKDROP,
                                group->groupusage == GroupLuminositySoftMask) ;
}

/**
 * Set the color management attributes appropriately for a color chain that is
 * about to be invoked for a group's colorInfo.
 */
Bool groupSetColorAttribs(GS_COLORinfo *colorInfo, int32 colorType,
                          uint8 reproType, LateColorAttrib *lca)
{
  OBJECT ri = OBJECT_NOTVM_NOTHING;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");
  HQASSERT(reproType < REPRO_N_TYPES, "reproType out of range");
  HQASSERT(lca != NULL, "LCA NULL");

  if ( !gsc_setRequiredReproType(colorInfo, colorType, reproType) )
    return FALSE;

  if ( !gsc_setColorModel(colorInfo, colorType,
                          lca->origColorModel) )
    return FALSE;

  object_store_namecache(&ri, gsc_convertIntentToName(lca->renderingIntent), LITERAL);
  if ( !gsc_setrenderingintent(colorInfo, &ri) )
    return FALSE;

  if ( !gsc_setBlackType(colorInfo, colorType, lca->blackType) )
    return FALSE;

  if ( !gsc_setPrevIndependentChannels(colorInfo, colorType, lca->independentChannels) )
    return FALSE;

  return TRUE;
}

/**
 * Set the page color in the backdrop for the composite-to-page operation.
 * Can't use page->dlc_erase since that is a device color and the backdrop wants
 * the erase color in this page group's blend space.  This function is called
 * when the page group is being closed, when the list of input colorants is
 * fixed.
 */
static Bool groupSetPageColor(Group *group,
                              uint32 inComps, COLORANTINDEX *inColorants)
{
  Bool result = FALSE;
  COLORVALUE *pageColor = NULL;
  COLORVALUE dcv;
  uint32 i;

  HQASSERT(group->groupusage == GroupPage,
           "pageColor only relevant for page groups");

  pageColor = mm_alloc(mm_pool_temp, inComps * sizeof(COLORVALUE),
                       MM_ALLOC_CLASS_GROUP);
  if ( pageColor == NULL )
    return error_handler(VMERROR);
#define return USE_goto_cleanup

  switch ( dlc_check_black_white(&group->dlcErase) ) {
  default:
    HQFAIL("Invalid DL tint color");
    /*@fallthrough@*/
  case DLC_TINT_WHITE:
    dcv = COLORVALUE_ONE;
    break;
  case DLC_TINT_BLACK:
    dcv = COLORVALUE_ZERO;
    break;
  case DLC_TINT_OTHER:
    dcv = COLORVALUE_INVALID;
    break;
  }

  for ( i = 0; i < inComps; ++i ) {
    COLORVALUE cv = dcv;

    /* Test if we have a colorant value for this channel. Note that the
       dlc_get_indexed_colorant call checks for either the colorant index,
       or colorant /All existing. */
    if ( cv != COLORVALUE_INVALID /* cv already extracted */ ||
         (dlc_get_indexed_colorant(&group->dlcErase, inColorants[i], &cv) &&
          cv != COLORVALUE_TRANSPARENT) )
      /* pageColor[i] set below */
      EMPTY_STATEMENT();
    else {
      /* This is the erase color that we're unpacking. We can't leave any
         rendered channels marked as overprinted, but there is no
         colorant, and no /All separation. Use solid or clear according
         to whether the first colorant is closest to solid or clear. */
      dl_color_iter_t dliter;
      COLORANTINDEX ci;

      switch ( dlc_first_colorant(&group->dlcErase, &dliter, &ci, &cv) ) {
      case DLC_ITER_COLORANT:
      case DLC_ITER_ALLSEP:
        HQASSERT(cv != COLORVALUE_TRANSPARENT,
                 "NYI: knockout color with first channel transparent");
        cv = cv >= COLORVALUE_HALF ? COLORVALUE_ONE : COLORVALUE_ZERO;
        break;
      case DLC_ITER_ALL01:
        HQFAIL("Erase color should have been handled through tint");
        break;
      default:
        HQFAIL("Erase color has no colorants");
        break;
      }
    }

    pageColor[i] = cv;
  }

  if ( !bd_setPageColor(group->backdrop, inComps, pageColor) )
    goto cleanup;

  result = TRUE;
 cleanup:
  if ( pageColor != NULL )
    mm_free(mm_pool_temp, pageColor, inComps * sizeof(COLORVALUE));
#undef return
  return result;
}

/**
 * Set the initial color (applies to luminosity softmasks only) and any softmask
 * transfer function.
 */
static Bool groupSetSoftMaskBC(Group *group)
{
  COLORVALUE *softMaskInitColor = NULL, colorBuf[MAX_BLENDSPACE_COMPS];

  /* Backdrop expects additive values normalised to COLORVALUE type. */
  if ( group->softMaskBColor != NULL ) {
    uint32 i;

    HQASSERT(group->nSoftMaskBCComps <= MAX_BLENDSPACE_COMPS,
             "MAX_BLENDSPACE_COMPS too small");
    HQASSERT(group->nProcessComps == group->nSoftMaskBCComps,
             "SoftMask should only have process colorants");

    for ( i = 0; i < group->nSoftMaskBCComps; ++i ) {
      USERVALUE cvf = group->softMaskBColor[i];
      NARROW_01(cvf);
      if ( group->processSubtractive )
        cvf = 1.0f - cvf;
      colorBuf[i] = FLOAT_TO_COLORVALUE(cvf);
    }
    softMaskInitColor = colorBuf;
  }

  return bd_setSoftMask(group->backdrop, softMaskInitColor,
                        group->softMaskTransfer);
}


static unsigned groupBackdropsHere(Group *group)
{
  unsigned backdropsHere;

  backdropsHere = (group->attrs.softMaskType != EmptySoftMask ? 0
                   : (DOING_TRANSPARENT_RLE(group->page)
                      && (group->page->rle_flags & RLE_NO_COMPOSITE) != 0
                      ? (group->insideSoftmask ? 1 : 0)
                      : 1))
                  + (group->fWillRetainSoftmask ? 1 : 0);
  /* A softmask's backdrop is counted through the parent's
     fWillRetainSoftmask, because it may be retained beyond the extent
     of the softmask. */
  return backdropsHere;
}


/** Calculate the number of backdrops needed to render each band */
static Bool groupCountBackdrops(Group *group, unsigned *depth)
{
  unsigned backdropsHere, backdropsTotal;
  size_t i;
  size_t nBands;
  DL_STATE *page = group->page;
  Group *pageGroup = groupBase(group);

  /* NB. This code relies on a recursive descent, using ancestor's data. */

  *depth = 0;

  /* Work out the band extent of the group. */
  group->extentOnPage = hdlExtentOnPage(group->hdl);
  if ( groupIsSoftMask(group) && !group->insidePattern ) {
    /* clip to softMaskArea (not in a pattern, as it's not in page coords) */
    Range softmask_range;

    hdlBandRangeOnPage(group->page, &group->softMaskArea, &softmask_range);
    /* The softmask extends beyond its extentOnPage, but those bits are
       empty, so only need the backdrop of the softmask itself, which
       we're counting over the entire parent group, anyway. */
    group->extentOnPage = rangeIntersection(group->extentOnPage,
                                            softmask_range);
  }
  if ( group->parent != NULL )
    /* Clip to parent's extent, so softmask subgroups inherit the clip above */
    group->extentOnPage = rangeIntersection(group->extentOnPage,
                                            group->parent->extentOnPage);
  nBands = (size_t)group->extentOnPage.length;
  if ( nBands == 0 )
    return TRUE;

  group->bandBackdrops =
    dl_alloc(page->dlpools, sizeof(unsigned) * nBands, MM_ALLOC_CLASS_GROUP);
  if ( group->bandBackdrops == NULL )
    return error_handler(VMERROR);

  backdropsHere = groupBackdropsHere(group);

  if ( group->groupusage == GroupPage ) {
    group->bandBackdropsPage =
      dl_alloc(page->dlpools, sizeof(unsigned) * nBands, MM_ALLOC_CLASS_GROUP);
    if ( group->bandBackdropsPage == NULL )
      return error_handler(VMERROR);
    for ( i = 0 ; i < nBands ; ++i )
      group->bandBackdropsPage[i] = group->bandBackdrops[i] = backdropsHere;
    if ( page->groupDepth < backdropsHere )
      page->groupDepth = backdropsHere;
    backdropsTotal = backdropsHere;
  } else {
    size_t offsetToParent = group->extentOnPage.origin
                            - group->parent->extentOnPage.origin;
    size_t offsetToPageGroup = group->extentOnPage.origin
                               - pageGroup->extentOnPage.origin;

    HQASSERT(group->parent->bandBackdrops != NULL,
             "Parent group is missing its backdrop counts");
    HQASSERT(pageGroup->bandBackdropsPage != NULL,
             "Page group is missing its backdrop counts");

    backdropsTotal =
      group->parent->bandBackdrops[offsetToParent] + backdropsHere;

    for ( i = 0 ; i < nBands ; ++i ) {
      HQASSERT(backdropsTotal ==
               group->parent->bandBackdrops[i + offsetToParent] + backdropsHere,
               "backdropsTotal must be the same across the bands");
      group->bandBackdrops[i] = backdropsTotal;
      if ( pageGroup->bandBackdropsPage[i + offsetToPageGroup] < backdropsTotal ) {
        pageGroup->bandBackdropsPage[i + offsetToPageGroup] = backdropsTotal;
        if ( page->groupDepth < backdropsTotal )
          page->groupDepth = backdropsTotal;
      }
    }
  }
  /* The depth of this group's backdrop (if any), is equal to the number of
     backdrops used by the ancestors, except that softmasks had their backdrop
     counted in the parent, so subtract one. */
  *depth = backdropsTotal - backdropsHere
    - (group->attrs.softMaskType != EmptySoftMask ? 1 : 0);
  return TRUE;
}


/**
 * Create the backdrop which eventually holds the composited results of this
 * group in a near raster form.  Optionally make a converter if we need to
 * convert the backdrop from the group's blend space to another space.
 */
static Bool groupNewBackdrop(Group *group)
{
  Bool result = FALSE;
  unsigned depth;
  Backdrop *initialBackdrop, *parentBackdrop;
  COLORANTINDEX *inColorants = NULL, *outColorants = NULL;
  uint32 inComps = 0, inProcessComps = 0, outComps, i;
  Bool out16;
  LateColorAttrib *groupPageLCA = NULL;
  Bool need_rle_type =
    (group->page->rle_flags & (RLE_TRANSPARENCY | RLE_OBJECT_TYPE))
    == (RLE_TRANSPARENCY | RLE_OBJECT_TYPE);

  /* DL_FORALL_USEMARKER doesn't work for groups in STATEOBJECT (eg softmasks),
     hence check on group->backdrop != NULL */
  if ( group == NULL || group->degenerate || group->backdrop != NULL )
    return TRUE;

  HQASSERT(group->backdrop == NULL, "Already got a backdrop");
  HQASSERT(group->converter == NULL, "Already got a converter");
  HQASSERT(group->blitmap == NULL, "Already got a blitmap");

  /* Create the blitmap used to render the group's DL into the backdrop.  May be
     compositing in device space and therefore will need to ensure the blitmap
     is setup for COLORVALUEs and pixel-interleaved output. */
  if ( !blit_colormap_create(group->page, &group->blitmap,
                             dl_choosepool(group->page->dlpools,
                                           MM_ALLOC_CLASS_BLIT_COLORMAP),
                             surface_find(group->page->surfaces,
                                          SURFACE_TRANSPARENCY),
                             gucr_colorantsStart(gucr_framesStart(group->inputRasterStyle)),
                             65536, /* override depth */
                             need_rle_type, /* type */
                             FALSE, /* don't force masking */
                             TRUE, /* For compositing */
                             FALSE /* force pixel interleaved */) )
    return FALSE;

  /** \todo MJ 02/06/10 could use the merge color as not all groups may need the
      full set of colorants, and fewer spots may simplify the backend color
      conversion. */
  inComps = group->blitmap->ncolors;
#define return USE_goto_cleanup
  inColorants = mm_alloc(mm_pool_temp, inComps * sizeof(COLORANTINDEX),
                         MM_ALLOC_CLASS_GROUP);
  if ( inColorants == NULL ) {
    (void)error_handler(VMERROR);
    goto cleanup;
  }

  for ( i = 0; i < inComps; ++i ) {
    const GUCR_COLORANT_INFO *info = group->blitmap->channel[i].colorant_info;

    /* The blend space is required to have the full set of colorants in the PCM.
       Colorant info is null if a process colorant has been forced, but info is
       not used for backdrop rendering.  */
    if ( info == NULL || info->colorantType == COLORANTTYPE_PROCESS )
      ++inProcessComps;

    inColorants[i] = group->blitmap->channel[i].ci;
  }

  if ( group->inputRasterStyle != group->outputRasterStyle ) {
    /* Set input colorspace and obtain output colorant set. */
    if ( !groupSetBackdropColorSpace(group, inColorants, inComps) ||
         !gsc_getDeviceColorColorants(group->colorInfo, GSC_BACKDROP,
                                      (int32*)&outComps, &outColorants) )
      goto cleanup;
  } else {
    /* Input and output blend spaces are identical. */
    outColorants = inColorants;
    outComps = inComps;
  }

  /* Determine bit depth for quantised colors in the page backdrop.
     Non-page backdrops always use 16 bit color values. */
  out16 = (group->groupusage != GroupPage ||
           spotlist_out16(group->page, outComps, outColorants));

  if ( group->groupusage == GroupPage ||
       group->inputRasterStyle != group->outputRasterStyle ) {
    /* Create object to convert backdrop to parent blend space or device space,
       or just for quantisation to device codes. */
    group->converter = cv_colcvtopen(group->page,
                                     group->preconvert != NULL
                                     ? preconvert_method(group->preconvert)
                                     : GSC_QUANTISE_ONLY,
                                     group->colorInfo, GSC_BACKDROP,
                                     TRUE, out16, inComps,
                                     outComps, outColorants,
                                     group->groupusage == GroupPage,
                                     group->attrs.lobjLCA);
    if ( group->converter == NULL )
      goto cleanup;
  }
  if ( group->parent != NULL && group->parent->groupusage == GroupPage &&
       group->parent->attrs.lobjLCA == NULL )
    groupPageLCA = group->attrs.lobjLCA;

  blit_color_init(&group->erase_color, group->blitmap);
  blit_color_unpack(&group->erase_color, &group->dlcErase,
                    0, /*type*/
                    group->attrs.lobjLCA,
                    TRUE, /*knockout this color*/
                    FALSE, /*selected*/
                    TRUE, /*This is the erase color*/
                    FALSE);

  initialBackdrop = (!group->attrs.isolated && group->initialGroup != NULL
                     ? group->initialGroup->backdrop : NULL);
  parentBackdrop = (group->parent != NULL ? group->parent->backdrop : NULL);

  if ( !groupCountBackdrops(group, &depth) )
    goto cleanup;

  if ( !bd_backdropNew(group->page->backdropShared, depth,
                       initialBackdrop, parentBackdrop, group, out16,
                       inComps, inProcessComps, inColorants,
                       outComps, outColorants,
                       group->converter, groupPageLCA,
                       &group->backdrop) )
    goto cleanup;

  /* Set group specific additional attributes in the backdrop. */
  switch ( group->groupusage ) {
  case GroupPage:
    if ( !groupSetPageColor(group, inComps, inColorants) )
      goto cleanup;
    break;
  case GroupLuminositySoftMask:
  case GroupAlphaSoftMask:
    if ( !groupSetSoftMaskBC(group) )
      goto cleanup;
    break;
  }

  result = TRUE;
 cleanup:
  if ( inColorants != NULL )
    mm_free(mm_pool_temp, inColorants, inComps * sizeof(COLORANTINDEX));
#undef return
  return result;
}

static Bool groupNewBackdropCallback(DL_FORALL_INFO *info)
{
  LISTOBJECT *lobj = info->lobj;
  STATEOBJECT *state = lobj->objectstate;

  if ( lobj->opcode == RENDER_group )
    if ( !groupNewBackdrop(lobj->dldata.group) )
      return FALSE;

  if ( state->tranAttrib != NULL &&
       state->tranAttrib->softMask != NULL &&
       state->tranAttrib->softMask->group != NULL )
    if ( !groupNewBackdrop(state->tranAttrib->softMask->group) )
      return FALSE;

  if ( state->patternstate != NULL &&
       state->patternstate->opcode == RENDER_group &&
       state->patternstate->dldata.group != NULL )
    if ( !groupNewBackdrop(state->patternstate->dldata.group) )
      return FALSE;

  return TRUE;
}

/**
 * Create the backdrops and the backdrop shared state.  Note, cannot just
 * iterate over just the groups in the dl store (i.e. using dlSSForall) because
 * some groups for patterns may have been created but not used on the DL (and
 * their parent ptrs may be out of date).  Doing a dl_forall ensures backdrops
 * are created in the right order to ensure initial backdrop and parent backdrop
 * are available if appropriate.
 */
Bool groupNewBackdrops(DL_STATE *page)
{
  DL_FORALL_INFO info;
  uint32 retention = BD_RETAINNOTHING;

  HQASSERT(page->backdropShared != NULL, "Backdrop shared state missing");
  HQASSERT((page->band_lines % page->region_height) == 0 || page->sizedisplaylist == 1,
           "Region height must be an exact factor of band height, unless only one band");

  /* Create a backdrop for each group.  Use dl_forall to create backdrops in the
     right order to ensure initial backdrop and parent backdrop are available.
     groupDepth is recalculated as a by-product of creating backdrops and then
     includes the result of any group elimination. */
  page->groupDepth = 0;
  info.hdl = dlPageHDL(page);
  info.data = NULL;
  info.inflags = (DL_FORALL_PATTERN|DL_FORALL_SHFILL|
                  DL_FORALL_GROUP|DL_FORALL_NONE|DL_FORALL_SOFTMASK);
  if ( !dl_forall(&info, groupNewBackdropCallback) )
    return FALSE;

  /* Compositing works on all the colorants together.  For pixel and mono output
     modes, each colorant is rendered immediately and the composited regions are
     no longer required.  For band, frame and separations output modes, the
     composited results are stored until each colorant has been rendered (this
     saves re-compositing on every band/frame/separation). */
  switch ( gucr_separationStyle(page->hr) ) {
  case GUCR_SEPARATIONSTYLE_SEPARATIONS:
  case GUCR_SEPARATIONSTYLE_COLORED_SEPARATIONS:
  case GUCR_SEPARATIONSTYLE_PROGRESSIVES:
    retention = BD_RETAINPAGE;
    break;
  case GUCR_SEPARATIONSTYLE_MONOCHROME:
    retention = BD_RETAINNOTHING;
    break;
  case GUCR_SEPARATIONSTYLE_COMPOSITE:
    switch ( gucr_interleavingStyle(page->hr) ) {
    case GUCR_INTERLEAVINGSTYLE_MONO:
    case GUCR_INTERLEAVINGSTYLE_PIXEL:
      retention = BD_RETAINNOTHING;
      break;
    case GUCR_INTERLEAVINGSTYLE_BAND:
      retention = BD_RETAINBAND;
      break;
    case GUCR_INTERLEAVINGSTYLE_FRAME:
      retention = BD_RETAINPAGE;
      break;
    default:
      HQFAIL("Unexpected interleaving style");
      return error_handler(UNREGISTERED);
    }
    break;
  default:
    HQFAIL("Unexpected separations style");
    return error_handler(UNREGISTERED);
  }

  /* For modular halftoning, retain bd for multiple renderings of a band. */
  if ( retention == BD_RETAINNOTHING && gucr_halftoning(page->hr)
       && htm_first_halftone_ref(page->eraseno) != NULL )
    retention = BD_RETAINBAND;

  /* bd_resourceUpdate to finalise resource provision after backdrop creation. */
  if ( !bd_resourceUpdate(page->backdropShared, 0 /* N/A */, FALSE /* N/A */,
                          page->groupDepth) )
    return FALSE;

  /* Create compositing contexts, backdrop resource pools.  If there are
     multiple spots then the backdrop may need to merge spots for overprinted
     objects. */
  return bd_backdropPrepare(page->backdropShared, retention,
                            spotlist_multi_spots(page));
}

/**
 * Determine if the softmask has already been composited and the results
 * retained.  We only allow one softmask at a time at each level in the group
 * stack to save backdrop resources.  A retained softmask is referenced by an
 * ancestor group and when this ancestor has finished rendering the softmask
 * backdrop blocks are released.
 */
Bool groupRetainedSoftMask(const render_info_t *ri, const Group *group)
{
  group_tracker_t *tracker;

  VERIFY_OBJECT(group, GROUP_NAME);

  /* Look for this softmask in any ancestor group, if it has already
     been retained we don't want to retain it again. */
  for ( tracker = ri->group_tracker; tracker != NULL; tracker = tracker->parent ) {
    if ( tracker->retainedSoftMask == group )
      return TRUE; /* already retained and composited */
  }

  return FALSE; /* need to composite softmask */
}

/**
 * Find the DL for the given band for a group object
 */
static void groupDLRange(HDL *hdl, int32 bandi, DLRANGE *dlrange)
{
  DLREF *dl = hdlOrderList(hdl);

  if ( bandi != DL_LASTBAND ) {
    HQASSERT(bandi >= 0,  "band cannot be negative.");

    /* Convert the absolute band index into a relative one. */
    bandi = bandi - (int32)hdlOffsetIntoPageDL(hdl);

    /* Obtain the DL band, may be null for soft masks */
    if ( bandi >= 0 && bandi < (int32)rangeTop(hdlUsedBands(hdl)) ) {
      DLREF **bands = hdlBands(hdl);

      if ( bands[bandi] != NULL )
        dl = bands[bandi];
    } else
      dl = NULL;
  }
  dlrange_init(dlrange);
  dlrange->start.dlref = dl;
}

Group *groupRendering(const render_info_t *ri)
{
  return ri->group_tracker != NULL ? ri->group_tracker->group : NULL;
}

Bool groupRenderingSubgroup(const render_info_t *ri)
{
  return ri->group_tracker != NULL
    ? ri->group_tracker->group->parent != NULL
    : FALSE ;
}

/** Render the group. The transparency attributes passed into this routine
   are those used to composite the backdrop with its parent. It is passed in
   because pattern cells need to inherit this information from the patterned
   object. This routine takes an HDL pointer because it is used as a virtual
   method. */
Bool groupRender(Group *group, render_info_t* renderInfo, TranAttrib *toParent)
{
  const render_state_t* rs;
  DLRANGE dlrange;
  TranAttrib tranDefault ;
  group_tracker_t group_tracker = { 0 } ;
  render_info_t ri ;

  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(renderInfo != NULL, "renderInfo is null");

  rs = renderInfo->p_rs;
  HQASSERT(rs != NULL, "No render state.");

  HQASSERT(toParent == NULL || tranAttribIsOpaque(toParent) ||
           renderInfo->rb.p_painting_pattern == NULL ||
           renderInfo->rb.p_painting_pattern->pPattern->painttype != UNCOLOURED_PATTERN,
           "Uncoloured pattern should have been transformed into UNCOLOURED_TRANSPARENT_PATTERN") ;

  if ( toParent == NULL ) {
    /* If no transparency attributes were supplied, use the default values */
    tranDefault.storeEntry.next = NULL ;
    tranDefault.blendMode = CCEModeNormal ;
    tranDefault.alphaIsShape = FALSE ;
    tranDefault.alpha = COLORVALUE_ONE ;
    tranDefault.softMask = NULL ;
    toParent = &tranDefault ;
  }

  groupDLRange(group->hdl, rs->band, &dlrange);

  /* Take a copy of the renderInfo, because we'll change the group tracker,
     and at the base level, dlRegionIterator will modify the bounds. */
  RI_COPY_FROM_RI(&ri, renderInfo) ;

  group_tracker.group = group ;
  group_tracker.parent = renderInfo->group_tracker ;
  ri.group_tracker = &group_tracker ;

  if ( groupIsSoftMask(group) ) {
    /* We only allow one softmask at a time at each level in the group stack to
       save backdrop resources.  A retained softmask is referenced by an
       ancestor group tracker rather than current one, because the softmask may
       be re-used within the parent context, but this context will be discarded
       immediately.  When this ancestor has finished rendering, the softmask
       backdrop blocks are released. */
    HQASSERT(render_state_backdrop(rs) != NULL,
             "Should always have a parent backdrop when rendering softmask") ;
    HQASSERT(!groupRetainedSoftMask(renderInfo, group),
             "Already retained and composited the softmask");

    if ( group_tracker.parent->retainedSoftMask != NULL ) {
      /* Changed softmasks within the same level: recycle the backdrop blocks
         for the new softmask.  If we switch back again, the first softmask will
         require re-compositing. */
      if ( !bd_regionRelease(groupGetBackdrop(group_tracker.parent->retainedSoftMask),
                             &group_tracker.parent->retainedSoftMaskBounds) )
        return FALSE ;

      /* Any retained soft mask will have just been released, so make sure
         there are no references to it. */
      group_tracker.parent->retainedSoftMask = NULL ;
      bbox_clear(&group_tracker.parent->retainedSoftMaskBounds) ;
    }

    /* Render the softmask into a backdrop. */
    if ( !renderRegions(group, &ri, &rs->cs.bandlimits,
                        TRUE, &dlrange, toParent) )
      return FALSE;

    group_tracker.parent->retainedSoftMask = group ;
    group_tracker.parent->retainedSoftMaskBounds = rs->cs.bandlimits ;

  } else if ( group->parent != NULL || rs->band == DL_LASTBAND ) {
    /* ordinary subgroup or pattern group */
    Bool backdropRender = (render_state_backdrop(rs) != NULL);

    /* Direct rendering a group that must be composited is a no-op. */
    if ( backdropRender || !group->mustComposite ) {
#if defined(DEBUG_BUILD)
      dlregion_clip(rs->page, &ri.bounds, backdrop_debug_left,
                    backdrop_debug_right) ;
#endif

      /* Render the pattern or group into a backdrop object. */
      if ( !renderRegions(group, &ri, &rs->cs.bandlimits,
                          backdropRender, &dlrange, toParent) )
        return FALSE;

      /* Trim or re-use the blocks in the backdrop for the current regions,
         depending on whether we are doing a pattern and may need to run the DL
         again. Everything has been flattened into the top-level group's
         backdrop which is not trimmed until all its channels have been
         rendered. */
      HQASSERT(!groupIsSoftMask(group), "Shouldn't be trimming softmask");
      if ( backdropRender && group->backdrop )
        if ( !bd_regionRelease(group->backdrop, &rs->cs.bandlimits) )
          return FALSE;
    }
  } else if ( dlrange.start.dlref != NULL ) {
    DLRegionIterator iterator = DL_REGION_ITERATOR_INIT;
    Bool more;
    uint32 band = rs->band - hdlOffsetIntoPageDL(group->hdl);
    Bool need_backdrop = (rs->pass_region_types & RENDER_REGIONS_BACKDROP) != 0
      && group->page->regionMap != NULL;

    /* This must be the top-level region render. We shouldn't be in pattern
       replication, because ri.bounds would be relative to the pattern DL,
       rather than the base DL. */
    HQASSERT(ri.pattern_state != PATTERN_PAINTING,
             "Should not be in pattern replication") ;
    HQASSERT(group->groupusage == GroupPage,
             "Should only direct render the page group");
    HQASSERT(!need_backdrop || group->bandBackdropsPage != NULL,
             "bandBackdropsPage is missing");
    HQASSERT(!group->insideSoftmask, "Page group shouldn't be in softmask");
    HQASSERT(dlrange.start.dlref != NULL, "null DLRANGE");

    HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
            ("band %u:", (unsigned)band));

    /* There may be multiple page groups if doing imposition. It's useful to
       know which page backdrop we'll be using. */
    if ( need_backdrop )
      bd_setCurrentPagebackdrop(rs->composite_context, group->backdrop);

    do {
      Bool backdropRender, result;
      int32 regions_needed = rs->pass_region_types;
      DLRANGE *do_range = &dlrange;

      more = dlregion_iterator(group->page, rs->surface_handle,
                               rs->composite_context,
                               &renderInfo->bounds,
                               need_backdrop ? group->bandBackdropsPage[band] : 0,
                               regions_needed,
                               &iterator, &ri.bounds,
                               &backdropRender);

      /* Clip the x extent of the returned bounds to the page, taking account of
      the x separation offset. This is necessary because the region iterator
      works in terms of backdrop blocks, and a backdrop block may extend beyond
      a clip region even if its contents do not (because a backdrop block is a
      fixed size, thus there may be some excess introduced). */
      if ( ri.bounds.x2 + ri.rb.x_sep_position >= ri.rb.outputform->w )
        ri.bounds.x2 = ri.rb.outputform->w - ri.rb.x_sep_position - 1;
      HQASSERT(!bbox_is_empty(&ri.bounds), "Must have a non-empty bbox");
      HQASSERT(bbox_contains(&rs->cs.bandlimits, &ri.bounds),
               "Region iteration bounds are not contained in band limits") ;

      if ( backdropRender ) { /* Region requires backdrop rendering */
        if ( (rs->pass_region_types & RENDER_REGIONS_BACKDROP) == 0 ) {
          /* But we're not doing backdrop rendering. */
          continue;
        }
      } else /* Region requires direct rendering */
        if ( (rs->pass_region_types & RENDER_REGIONS_DIRECT) == 0 )
          /* But we're not doing direct rendering. */
          continue;

      if ( !clip_context_begin(&ri) )
        return FALSE ;

#define return DO_NOT_return!
      /* Strictly speaking, it's not necessary to update the clip like this,
         because they will be reset, however it is useful to retain sanity
         when debugging. */
      ri.clip = ri.bounds ;

      HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
              ("x1 %d, y1 %d, x2 %d, y2 %d, %s rendering...",
               ri.bounds.x1, ri.bounds.y1,
               ri.bounds.x2, ri.bounds.y2,
               (backdropRender ? "Backdrop" : "Direct")));

      /* Render the DL via a backdrop object or directly */
      result = renderRegions(group, &ri, &ri.bounds, backdropRender, do_range,
                             toParent) ;

      /* Release the resources used to composite this region set.  Do this even
         if an error has occurred, because task cancellation may not happen
         immediately. */
      if ( backdropRender && group->page->backdropShared != NULL )
        if ( ! bd_regionReleaseAll(rs->composite_context, group->page->backdropShared,
                                   &ri.bounds, result) )
          result = FALSE ;

      /* Having composited and rendered all of the regions for a particular set
         of bounds, we will have possibly restricted the clipping form contents
         to the region bounds, and may have set the tracking variables to
         stack-frame local state and clip values. We need to reset these
         variables so that the next region can regenerate the clip
         correctly. */
      clip_context_end(&ri) ;
#undef return
      /* Any retained soft mask will have just been released, so make sure
         there are no references to it. */
      group_tracker.retainedSoftMask = NULL ;
      bbox_clear(&group_tracker.retainedSoftMaskBounds) ;

      if ( !result )
        return FALSE ;
    } while (more);
  }

  if ( group_tracker.retainedSoftMask ) {
    /* We've finished at this level in the group stack and need
       to release any softmask retained in this level. */
    HQASSERT(groupGetBackdrop(group_tracker.retainedSoftMask),
             "Expected backdrop for softmask");
    HQASSERT(render_state_backdrop(rs) != NULL,
             "Retained softmask but not backdrop rendering");
    if ( !bd_regionRelease(groupGetBackdrop(group_tracker.retainedSoftMask),
                           &group_tracker.retainedSoftMaskBounds) )
      return FALSE;
  }
  return TRUE ;
}

Bool groupColorantToEquivalentRealColorant(Group *group,
                                           COLORANTINDEX ci,
                                           COLORANTINDEX **cimap)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return guc_equivalentRealColorantIndex(group->inputRasterStyle, ci, cimap);
}

dbbox_t groupSoftMaskArea(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(groupIsSoftMask(group),
           "groupSoftMaskArea called with a non-softmask group") ;
  return group->softMaskArea ;
}

/* Optimise softmasks by compositing only the area covered by the objects
   that use the softmask.  This routine expands the softmask area to the
   given bbox. */
void groupSoftMaskAreaExpand(Group *group, dbbox_t *bbox)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(groupIsSoftMask(group),
           "groupSoftMaskAreaExpand called with a non-softmask group") ;

  if ( pattern_executingpaintproc(NULL) ) {
    /* The DL object referring to the softmask is inside a pattern, so
       softMaskArea is no use and we'll have to composite the softmask up to
       the pattern's replication limits instead. */
    bbox_store(&group->softMaskArea, MINDCOORD, MINDCOORD, MAXDCOORD, MAXDCOORD) ;
  } else {
    if ( bbox ) {
      /* Expand the softmask area required to include the current object. */
      bbox_union(&group->softMaskArea, bbox, &group->softMaskArea) ;
    }
  }
}

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
void init_backdroprender_debug(void)
{
  register_ripvar(NAME_debug_br, OINTEGER, & backdrop_render_debug);
  register_ripvar(NAME_debug_br_firstregion, OINTEGER, &backdrop_debug_left);
  register_ripvar(NAME_debug_br_lastregion, OINTEGER, &backdrop_debug_right);
}
#endif

/* Return values for the group attributes */
const GroupAttrs *groupGetAttrs(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return &group->attrs;
}

Bool groupNonIsolatedGroups(Group *group)
{
  group = groupBase(group);
  return group->numNonIsolatedGroups > 0;
}

Bool groupInsidePattern(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->insidePattern;
}

static Bool groupRasterStyles(Group *group, GSTATE *gs)
{
  Bool groupIsVirtualDevice = FALSE, pageBlendSpaceIsDeviceSpace = FALSE;

  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(oType(group->colorspace) != ONULL ||
           group->groupusage != GroupLuminositySoftMask,
           "Must have a colorspace for luminosity softmask");

  if ( oType(group->colorspace) == ONULL ) {
    if ( group->groupusage == GroupPage ) {
      /* Choose the PCM for the default page group's blend space.
         PDF page groups specify their own. */
      if ( gsc_getOverprintPreview(gs->colorInfo) ) {
        /* Convert virtual device space ID to a name. */
        COLORSPACE_ID spaceId;
        int32 csName;
        dlVirtualDeviceSpace(group->page, &csName, &spaceId);
        object_store_name(&group->colorspace, csName, LITERAL);
        groupIsVirtualDevice = TRUE;
      } else {
        /* Do compositing in device space. */
        guc_colorSpace(gsc_getRS(gs->colorInfo), &group->colorspace);
        pageBlendSpaceIsDeviceSpace = TRUE;
      }
    } else { /* group->groupusage != GroupPage */
      /* Inherit color space from parent. */
      Copy(&group->colorspace, &group->parent->colorspace);
      if ( !guc_backdropRasterStyle(group->parent->inputRasterStyle) )
        pageBlendSpaceIsDeviceSpace = TRUE;
    }
    HQASSERT(oType(group->colorspace) != ONULL, "Colorspace still empty");
  }

  /* Target space for converting the composited results in the backdrop. */
  if (groupIsSoftMask(group)) {
    OBJECT *colorSpace;

    /* Building a soft mask group. Note that the parent is set to be the
       device raster style, to make sure real/virtual colorant maps in
       contained groups are correct. This is counter intuitive, we would
       expect it to have no parent, but is used to simplify implementation of
       in the presence of HDLT callbacks which may change mappings. */
    if ( !gsc_getInternalColorSpace(SPACE_DeviceGray, &colorSpace) ||
         !guc_setupBackdropRasterStyle(&group->outputRasterStyle,
                                       gsc_getRS(gs->colorInfo),
                                       gs->colorInfo,
                                       colorSpace, FALSE, FALSE, FALSE) )
      return FALSE;
  } else {
    /* Target space for composited group.  After a PDF page group has been
       closed, there's a restore back to the gstate from when the original
       default page group was around, which means the target RS might not be
       what we want when creating the next page group. So for page groups use
       the device RS explicitly. */
    group->outputRasterStyle = group->groupusage == GroupPage
                              ? gsc_getRS(gs->colorInfo) /* device RS */
                              : gsc_getTargetRS(gs->colorInfo);
    guc_reserveRasterStyle(group->outputRasterStyle);
  }

  /* Target space for the DL objects going into the group.
     Softmask groups never re-use their parent's inputRasterStyle because
     autoseps must be disabled for softmasks. */
  if ( pageBlendSpaceIsDeviceSpace ) {
    /* Making an implicit page group with OverprintPreview set to false.  Do
       compositing in device space. */
    group->inputRasterStyle = group->outputRasterStyle;
    guc_reserveRasterStyle(group->inputRasterStyle);
  } else if ( !group->attrs.isolated ||
              (!groupIsSoftMask(group) && group->parent != NULL &&
               BLENDSPACE_IS_STANDARD(group->parent->processSpace) &&
               gsc_sameColorSpaceObject(gs->colorInfo,
                                        &group->colorspace,
                                        &group->parent->colorspace)) ) {
    /* Non-isolated groups, and those that do not specify a color space,
       inherit input space from parent group. */
    HQASSERT(group->attrs.isolated || !groupIsSoftMask(group),
             "Only non-soft mask groups can be non-isolated");
    HQASSERT(group->parent != NULL,
             "Non-isolated group must have a parent");

    group->inputRasterStyle = group->parent->inputRasterStyle;
    guc_reserveRasterStyle(group->inputRasterStyle);
  } else {
    Bool autoSeps = !groupIsSoftMask(group) &&
                    group->page->backdropAutoSeparations;
    Bool inheritSeps = !groupIsSoftMask(group);

    /* We always link the parent to the current target, even though it may
       seem inappropriate for softmasks, because an HDLT callback in a
       softmask that changes the colorant mapping needs to be able to find the
       real device rasterstyle. */
    if ( !guc_setupBackdropRasterStyle(&group->inputRasterStyle,
                                       gsc_getTargetRS(gs->colorInfo),
                                       gs->colorInfo,
                                       &group->colorspace,
                                       groupIsVirtualDevice,
                                       autoSeps, inheritSeps) )
      return FALSE;
  }

  guc_deviceToColorSpaceId(group->inputRasterStyle, &group->processSpace);
  guc_deviceColorSpace(group->inputRasterStyle, NULL, (int32*)&group->nProcessComps);
  if ( !guc_deviceColorSpaceSubtractive(group->inputRasterStyle,
                                        &group->processSubtractive) )
    return FALSE;

  HQASSERT(group->inputRasterStyle != NULL, "inputRasterStyle is null");
  HQASSERT(group->outputRasterStyle != NULL, "outputRasterStyle is null");

  return TRUE;
}

#if defined(DEBUG_BUILD)
Bool dbg_mark(LISTOBJECT *lobj, render_info_t *ri, dcoord x1, dcoord y1,
              uint16 w, uint16 h)
{
  bbox_store(&(lobj->bbox), x1, y1, x1 + w, y1 + h);
  return render_single_listobj(ri, lobj);
}
#endif

/**
 * Render the backdrop into the enclosing HDL or into the output form.
 */
static Bool renderBackdrop(Group *group, render_info_t *renderInfo,
                           const dbbox_t *bbox, TranAttrib *toParent)

{
  LISTOBJECT lobj;
  STATEOBJECT state;
  CLIPOBJECT clip;
  LateColorAttrib lca;
  render_info_t ri;
  blit_chain_t blits;
  Bool result = FALSE ;
  const render_state_t *p_rs = renderInfo->p_rs ;

  /* Render the region back to its parent. The region bounds passed in are
     used to override the render info bounds, which may be set up for a
     pattern cell's device space. */
  RI_COPY_FROM_RI(&ri, renderInfo) ;
  blits = *ri.rb.blits ;
  ri.rb.blits = &blits ;

  ri.bounds = *bbox ;

  /* Don't want to replicate the backdrop compositing, even if we're in a
     pattern. Replication will have been dealt with already. This also
     prevents uncoloured transparent patterns from inappropriately overriding
     the DL colour. */
  pattern_finish(&ri) ;
  HQASSERT(p_rs->dopatterns,
           "Final group output when regenerating pattern forms") ;

  clip.storeEntry.next = NULL ;
  clip.bounds = *bbox ;
  if ( group->page->ScanConversion == SC_RULE_TESSELATE ) {
    /* To compensate for the render loop taking it off later.
       Tesselating scan conversion happened inside the backdrop. */
    ++clip.bounds.y2 ;
    ++clip.bounds.x2 ;
  }
  clip.fill = NULL ;
  clip.context = NULL ;
  clip.ncomplex = 0 ;
  clip.rule = NZFILL_TYPE ;
  clip.clipno = CLIPID_INVALID - 1 ; /* Pick a very unlikely clip ID */
  clip.pagebasematrixid = PAGEBASEID_INVALID ;

  lca = lateColorAttribNew() ;

  state = stateobject_new(p_rs->lobjErase->objectstate->spotno);
  state.clipstate = &clip ;
  state.lateColorAttrib = &lca ;
  state.tranAttrib = toParent ;

  init_listobject(&lobj, RENDER_backdrop, (dbbox_t*)bbox) ;
  lobj.dldata.backdrop = group->backdrop ;
  lobj.objectstate = &state ;
  /* PDF 1.6 7.6.3 Overprinting and Transparency (final para) states when
     painting an object that is a transparency group rather than an elementary
     object ... a group is considered to paint all color components, both
     process and spot. */
  lobj.spflags = RENDER_KNOCKOUT ;
  /* Render the image on all planes. The backdrop expander will deal with
     pixels appropriately. */
  DISPOSITION_STORE(lobj.disposition, REPRO_DISPOSITION_MIXED,
                    GSC_UNDEFINED, 0) ;
  HQASSERT(group->lobjColor != NULL, "Should have called groupSetLobjColor by now") ;
  dl_copy_weak(&lobj.p_ncolor, group->lobjColor) ;
  if ( group->parent == NULL )
    lobj.marker |= MARKER_DEVICECOLOR ;

  if ( !clip_context_begin(&ri) )
    return FALSE ;
#define return DO_NOT_return!

  if ( group->groupusage == GroupPage) {
    if ( !trapBackdropColorValues(&ri, &lobj, bbox) )
      goto cleanup ;
  }
  /* Render the backdrop image with the appropriate transparency attributes
     to the parent backdrop. We should not try to use render_backdrop here,
     because might be rendering to the real device, and we need to take
     account of the rendering properties for each separation. */
  if ( !render_single_listobj(&ri, &lobj) )
    goto cleanup ;

#if defined(DEBUG_BUILD)
  /* Draw corners of the region being rendered as white corner marks with
     smaller black ticks over them. */
  if (group->groupusage == GroupPage &&
      (backdrop_render_debug & BR_DEBUG_RENDER_REGION_CORNERS) != 0 ) {
    dl_color_t dlcBlack, dlcWhite ;
    uint16 ytick, xtick;
    int32 tmp;

    tmp = (bbox->x2 - bbox->x1)/3;
    if ( tmp > (int32)(group->page->xdpi/10) )
      tmp  = (int32)(group->page->xdpi/10);
    xtick = (uint16)tmp;

    tmp = (bbox->y2 - bbox->y1)/3;
    if ( tmp > (int32)(group->page->ydpi/10) )
      tmp  = (int32)(group->page->ydpi/10);
    ytick = (uint16)tmp;

    dlc_clear(&dlcBlack) ;
    dlc_get_black(group->page->dlc_context, &dlcBlack) ;

    dlc_clear(&dlcWhite) ;
    dlc_get_white(group->page->dlc_context, &dlcWhite) ;

    /* Create LISTOBJECT to draw four ticks at the corners of the region.
     * Inherit most of the parameters from the backdrop object, then reset
     * to create rects of the appropriate size.
     */
    init_listobject(&lobj, RENDER_rect, NULL);
    lobj.objectstate = &state;
    DISPOSITION_STORE(lobj.disposition, REPRO_DISPOSITION_RENDER,
                      GSC_FILL, 0) ; /* Debug marks appear everywhere */
    (void)dlc_to_lobj(group->page->dlc_context, &lobj, &dlcWhite);
    if ( !dbg_mark(&lobj, &ri, bbox->x1, bbox->y1, 1, ytick + 1) )
      goto cleanup;
    if ( !dbg_mark(&lobj, &ri, bbox->x1, bbox->y1, xtick + 1, 1) )
      goto cleanup;
    if ( !dbg_mark(&lobj, &ri, bbox->x2 - xtick - 1,
                               bbox->y2 - 1, xtick + 1, 1) )
      goto cleanup;
    if ( !dbg_mark(&lobj, &ri, bbox->x2 - 1, bbox->y2 - ytick - 1,
                               1, ytick + 1) )
      goto cleanup;
    (void)dlc_to_lobj(group->page->dlc_context, &lobj, &dlcBlack);
    if ( !dbg_mark(&lobj, &ri, bbox->x1, bbox->y1, 0, ytick) )
      goto cleanup;
    if ( !dbg_mark(&lobj, &ri, bbox->x1, bbox->y1, xtick, 0) )
      goto cleanup;
    if ( !dbg_mark(&lobj, &ri, bbox->x2 - xtick, bbox->y2, xtick, 0) )
      goto cleanup;
    if ( !dbg_mark(&lobj, &ri, bbox->x2, bbox->y2 - ytick, 0, ytick) )
      goto cleanup;
  }
#endif

  result = TRUE ;

 cleanup:
  /* Having composited and rendered all of the regions for a particular set
     of bounds, we will have possibly restricted the clipping form contents
     to the region bounds, and may have set the tracking variables to
     stack-frame local state and clip values. We need to reset these
     variables so that the next region can regenerate the clip
     correctly. */
  clip_context_end(&ri) ;

  /* It's possible that the render tracker is holding on to the 'state' stack
   * variable. It needs to be cleared now. */
  if (ri.p_rs->cs.renderTracker->oldstate == &state)
    ri.p_rs->cs.renderTracker->oldstate = NULL;

#undef return

  return result;
}

/* Region callbacks for surface localisers.
 *
 * We go through this bother so that the surface can have a stack frame
 * interpolated into the rendering stack, as a convenient place to put
 * thread-local region data. The callback typedefs and callback system allows
 * us to alter the parameters of without affecting the surface definitions
 * (or even having to recompile them).
 */

/** \brief Render region callback data name. */
#define RENDER_REGION_CALLBACK_NAME "Render region callback"

/** \brief Parameters for render region callback function. */
struct render_region_callback_t {
  Group *group ;        /**< Group object. */
  DLRANGE *dlrange ;    /**< DL object range in group. */
  const surface_t *surface ; /**< Transparency surface. */
  render_info_t *ri ;   /**< Render info state. */
  Backdrop *backdrop ;  /**< Backdrop of group. */
  const dbbox_t *bbox ; /**< Region bounds */
  int called ;          /**< Number of times the callback was called. */
  OBJECT_NAME_MEMBER
} ;

static Bool render_region_callback(render_region_callback_t *data)
{
  Group *group ;
  const surface_t *surface ;
  Bool dummy_white_on_white = FALSE ;
  render_state_t bdstate ;
  Bool result = TRUE ;

  VERIFY_OBJECT(data, RENDER_REGION_CALLBACK_NAME) ;

  HQASSERT(data->called == 0, "Render region callback has been called already") ;
  ++data->called ;

  group = data->group ;
  VERIFY_OBJECT(group, GROUP_NAME) ;

  HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
          ("compositing %s group 0x%X",
           groupusagelabels[group->groupusage], group));

  if ( data->dlrange->start.dlref == NULL )
    return TRUE ;

  RS_COPY_FROM_RI(&bdstate, data->ri) ;

  /* Render DL band into the backdrop */
  /* keep p_rs->forms */
  /* keep p_rs->clipbase */
  /* keep p_rs->surface_handle */
  bdstate.hr = group->inputRasterStyle ;
  bdstate.hf = gucr_framesStart(bdstate.hr) ;
  bdstate.nChannel = 0 ;
  /* keep p_rs->lobjErase */
  HQASSERT(bdstate.lobjErase == dlref_lobj(dl_get_orderlist(group->page)),
           "Erase object unexpectedly changed during backdrop render") ;
  /* keep p_rs->composite_context */
  bdstate.backdrop = data->backdrop ;
  gucr_colorantCount(bdstate.hr, &bdstate.nCi);
  /* keep p_rs->band */
  /* keep pass_region_types */
  bdstate.fPixelInterleaved = TRUE ;
  bdstate.maxmode = BLT_MAX_NONE;
  bdstate.fMergePatterns = !group->attrs.knockout;
  bdstate.dopatterns = TRUE;
  bdstate.htm_info = NULL;
  dlc_clear(&bdstate.dlc_watermark) ;
  bdstate.spotno_watermark = SPOT_NO_INVALID ;

  bdstate.cs.blitmap = group->blitmap ;
  bdstate.cs.erase_color = &group->erase_color;
  bdstate.cs.knockout_color = &group->erase_color;
  /* keep p_rs->cs.renderTracker */
  bdstate.cs.hc = gucr_colorantsStart(bdstate.hf) ;
  bdstate.cs.p_white_on_white = &dummy_white_on_white ;
  bdstate.cs.bandlimits = *data->bbox;
  bdstate.cs.fIsHalftone = FALSE ;
  bdstate.cs.bg_separation = FALSE ;
  bdstate.cs.fSelfIntersect = FALSE ;
  bdstate.cs.selected_mht = NULL ;
  bdstate.cs.selected_mht_is_erase = FALSE ;

  bdstate.ri.generate_object_map = FALSE;
  bdstate.ri.surface = surface = data->surface ;

  bdstate.ri.rb.color = NULL ;

  if ( !clip_context_begin(&bdstate.ri) )
    return FALSE ;
#define return DO_NOT_return!

  if ( surface->assign != NULL )
    result = (*surface->assign)(bdstate.surface_handle, &bdstate) ;

  if ( result )
    result = render_object_list_of_band(&bdstate.ri, data->dlrange) ;

  clip_context_end(&bdstate.ri) ;
#undef return

  return result ;
}

RleColorantMap *region_rle_colorant_map(const render_region_callback_t *region)
{
  VERIFY_OBJECT(region, RENDER_REGION_CALLBACK_NAME) ;
  VERIFY_OBJECT(region->group, GROUP_NAME) ;
  return region->group->inputRleColorantMap ;
}

static Bool renderRegions(Group *group,
                          render_info_t *renderInfo,
                          const dbbox_t *bandlimits,
                          Bool backdropRender,
                          DLRANGE *dlrange,
                          TranAttrib *toParent)
{
  Backdrop *backdrop, *initial, *target;
  Bool result;
  DEVICESPACEID colorspace ;
  const render_state_t *p_rs ;
  const transparency_surface_t *transurf ;
  render_region_callback_t callback_data ;

  VERIFY_OBJECT(group, GROUP_NAME);
  HQASSERT(renderInfo != NULL, "renderInfo is null");

  p_rs = renderInfo->p_rs ;
  HQASSERT(p_rs != NULL, "No render state");
  HQASSERT(toParent != NULL, "No transparency attributes");

  renderInfo->region_set_type =
    backdropRender ? RENDER_REGIONS_BACKDROP : RENDER_REGIONS_DIRECT;

  if (! backdropRender) {
    /* Directly render the DL to output or outer group */
#if defined( DEBUG_BUILD )
    if ((backdrop_render_debug & BR_DEBUG_SKIP_DIRECT_REGIONS) != 0) {
      HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
              ("skipping region for direct rendering"));
      return TRUE;
    }
#endif

    /* Only the page group, and those pattern or sub-groups which inherit
       the page group's color space can be direct rendered.  Other groups
       can get this far because their bbox is not the same as the union of
       the bboxes of the group's sub-dl (i.e. this is an optimisation). */
    if (group->groupusage != GroupPage &&
        group->groupusage != GroupSubGroup &&
        group->groupusage != GroupPattern &&
        group->groupusage != GroupImplicit)
      return TRUE;

    return render_object_list_of_band(renderInfo, dlrange);
  }

  /* Render the DL via a backdrop object */

#if defined( DEBUG_BUILD )
  if ((backdrop_render_debug & BR_DEBUG_SKIP_BACKDROP_REGIONS) != 0) {
    HQTRACE((backdrop_render_debug & BR_DEBUG_TRACE) != 0,
            ("skipping region for backdrop rendering"));
    return TRUE;
  }
#endif

  backdrop = group->backdrop;
  HQASSERT(backdrop != NULL, "backdrop is null");

  transurf = surface_find_transparency(group->page->surfaces);
  HQASSERT(transurf != NULL, "No transparency surface for output") ;

  initial = NULL ;
  if ( group->initialGroup )
    initial = group->initialGroup->backdrop ;

  target = NULL ;
  if ( !groupIsSoftMask(group) )
    target = p_rs->backdrop ;

  guc_deviceColorSpace(group->inputRasterStyle, &colorspace, NULL) ;

  /* Have we already composited this group before? We may be called multiple
     times on the same region in frame interleaved mode. We will re-composite
     patterns (when called with DL_LASTBAND) so that overlapping objects
     using the same pattern can composite against themselves properly.

     Softmasks are only ever composited once (even within patterns). This
     only works at the moment because softmasks are not shared between
     contexts. The transparency state is re-set within groups and patterns,
     ensuring different softmask instances. When the softmask group pointer
     is moved out of the gstate, the same softmask instance might be
     shareable in different contexts. If this happens, we will need to add an
     iterator to composite just the blocks that have not been done before.
     This should be true for all isolated groups; each region should only
     ever be composited once. */
  callback_data.group = group ;
  callback_data.dlrange = dlrange ;
  callback_data.surface = &transurf->base ;
  callback_data.ri = renderInfo ;
  callback_data.backdrop = group->backdrop ;
  HQASSERT(callback_data.backdrop != NULL, "backdropStore is null");
  callback_data.bbox = bandlimits ;
  callback_data.called = 0 ;
  NAME_OBJECT(&callback_data, RENDER_REGION_CALLBACK_NAME) ;

  HQASSERT(transurf->region_create, "No region create method") ;
  result = (*transurf->region_create)(p_rs->surface_handle,
                                      p_rs->composite_context,
                                      colorspace,
                                      groupIsSoftMask(group),
                                      p_rs->band == DL_LASTBAND,
                                      group->attrs.knockout,
                                      callback_data.backdrop,
                                      initial, target, toParent, bandlimits,
                                      &render_region_callback,
                                      &callback_data) ;

  /* We may optimise out the callback. We shouldn't call it more than once
     (well, maybe we should, if trapping wants to replay?) */
  HQASSERT(callback_data.called == 0 || callback_data.called == 1,
           "Render region callback was not called back correctly") ;
  UNNAME_OBJECT(&callback_data) ;

  /* Unless this is a soft mask render the backdrop into the enclosing
     HDL or into the output form.  Composited softmask backdrops are read
     when rendering other objects which reference the softmask. We might want
     to skip this when dopatterns is false, because that indicates we're
     regenerating pattern forms. */
  if (result && !groupIsSoftMask(group)
      /** \todo It's not nice that this needs to know about RLE_NO_COMPOSITE. */
      && ((group->page->rle_flags & RLE_NO_COMPOSITE) == 0
          || group->insideSoftmask) )
    result = renderBackdrop(group, renderInfo, bandlimits, toParent);

  return result ;
}

/** Common action required when a group is terminated, either by being closed or
   destroyed while active.
*/
static void groupCommonTermination(Group *group)
{
  DL_STATE *page ;

  VERIFY_OBJECT(group, GROUP_NAME);

  if (group->disabledRecombineInterception) {
    /** \todo @@@ TODO FIXME ajcd 2005-06-19: This behaviour should not be
       part of the group object. It is front-end, and should be enabled and
       according to the group target. */
    /* Re-enable recombine interception after this soft mask.  Disabled
       recombine interception for soft masks since it simplifies dealing
       with composite jobs going through a recombine setup.  Recombine
       interception is not required since we are making an alpha channel. */
    rcbn_enable_interception(gstateptr->colorInfo);
    group->disabledRecombineInterception = FALSE;
  }

  page = group->page ;

  /* Destruction may happen part way through construction of a group or other
     HDL subclass. Make sure the Group is removed from the stack in this
     case. */
  if ( page->currentGroup == group )
    /* Reinstate the parent Group as current on the page DL. */
    page->currentGroup = group->parent;

  /* Terminating the group so the target RS in the gstate needs to be reset. */
  gsc_setTargetRS(gstateptr->colorInfo, group->outputRasterStyle);

  /* After removing the page group reset recombine. */
  if ( group->groupusage == GroupPage )
    (void)rcbn_reset();
}


/** Frees anything in the Group which was not allocated from dl memory.
 */
static void groupCommonDestruction(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);

  if ( group->colorInfo != NULL ) {
    gsc_freecolorinfo(group->colorInfo);
    /* Need to free colorInfo itself even though it's dl memory;
       groupCommonDestruction may be called again. */
    dl_free(group->page->dlpools, group->colorInfo, gsc_colorInfoSize(),
            MM_ALLOC_CLASS_GROUP);
    group->colorInfo = NULL;
  }
  if ( group->converter != NULL )
    cv_colcvtfree(group->page, &group->converter);
  if ( group->preconvert != NULL )
    preconvert_free(group->page, &group->preconvert);

  if ( group->blitmap != NULL )
    blit_colormap_destroy(&group->blitmap,
                          dl_choosepool(group->page->dlpools,
                                        MM_ALLOC_CLASS_BLIT_COLORMAP));

  if ( group->inputRasterStyle != NULL )
    guc_discardRasterStyle(& group->inputRasterStyle);
  if ( group->outputRasterStyle != NULL )
    guc_discardRasterStyle(& group->outputRasterStyle);
}

GUCR_RASTERSTYLE *groupInputRasterStyle(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->inputRasterStyle;
}

GUCR_RASTERSTYLE *groupOutputRasterStyle(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->outputRasterStyle;
}

/** Return the usage of this group.
*/
GroupUsage groupGetUsage(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->groupusage;
}

void groupGetHDLT(Group *group, Bool *isolated, Bool *knockout,
                  OBJECT *colorspace, USERVALUE **bgcolor, int32 *bgcolordim,
                  OBJECT *transferfn, int32 *transferfnid)
{
  VERIFY_OBJECT(group, GROUP_NAME) ;

  *isolated = group->attrs.isolated ;
  *knockout = group->attrs.knockout ;
  Copy(colorspace, &group->colorspace) ;
  *bgcolor = group->softMaskBColor ;
  *bgcolordim = (int32)group->nSoftMaskBCComps ;
  if ( group->softMaskTransfer )
    Copy(transferfn, group->hdltTransferObj) ;
  else
    Copy(transferfn, &onull) ;
  *transferfnid = group->hdltTransferId ;
}

/** Return the Group's backdrop image.
*/
Backdrop *groupGetBackdrop(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->backdrop;
}

OBJECT *groupColorSpace(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return &group->colorspace;
}

GS_COLORinfo *groupColorInfo(Group *group)
{
  VERIFY_OBJECT(group, GROUP_NAME);
  return group->colorInfo;
}

Bool groupRLEColorants(ColorantList *colorants, DL_STATE *page)
{
  HDL_LIST *hlist;

  HQASSERT(DOING_TRANSPARENT_RLE(page), "Not doing transparent RLE") ;

  colorantListTerminateDeviceColorants(colorants) ;

  for ( hlist = page->all_hdls; hlist != NULL; hlist = hlist->next ) {
    Group *group = hdlGroup(hlist->hdl);
    if ( group != NULL ) {
      VERIFY_OBJECT(group, GROUP_NAME) ;
      if ( !groupInsideSoftMask(group) ) {
        HQASSERT(groupGetUsage(group) != GroupLuminositySoftMask &&
                 groupGetUsage(group) != GroupAlphaSoftMask,
                 "Colorant list should not be added from softmask");

        if ( !colorantListAddFromRasterStyle(colorants,
                                             gucr_framesStart(group->inputRasterStyle),
                                             FALSE /*all colorants*/) )
          return FALSE ;

        /* When outputting transparent RLE, we need to create a mapping from
           the colorant indices used in this group's raster styles to those
           listed in the PGB header, so that the contents of this group can
           be passed to the RLE system. We've just added all of the group
           colorants to the PGB list, so we can now construct the map. */
        HQASSERT(group->inputRleColorantMap == NULL,
                 "Group already has an RLE colorant map") ;
        group->inputRleColorantMap = rleColorantMapNew(page->dlpools,
                                                       group->inputRasterStyle,
                                                       colorants);
        if (group->inputRleColorantMap == NULL)
          return FALSE;
      }
    }
  }

  return TRUE;
}

void init_C_globals_group(void)
{
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  backdrop_render_debug = 0;
  backdrop_debug_left = 0;
  backdrop_debug_right = MAXINT32 ;
#endif
#ifdef DEBUG_BUILD
  group_elim_new = 0;
  group_elim_hits = 0;
  group_elim_total = 0;
#endif
}

/* Log stripped */
