/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:group.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to the Group DL object. This object primarily supports the
 * transparency group object in PDF 1.4 and the Canvas object in XPS.
 */

#ifndef __GROUP_H__
#define __GROUP_H__

#include "objects.h"
#include "graphict.h"
#include "displayt.h"
#include "imaget.h"       /* IMAGEOBJECT */
#include "dl_color.h"     /* dl_color_t */

struct render_info_t ; /* from render.h */
struct ColorantList ; /* from rleColorantMappingTypes.h */

/* --Public datatypes-- */

typedef uint32 GroupUsage;
enum {
  GroupPage = 0, /* For main page group */
  GroupSubGroup = 1, /* For sub-groups */
  GroupImplicit = 2, /* For combined stroke and fill ops etc */
  GroupPattern = 3, /* For patterns */
  GroupAlphaSoftMask = 4, /* For soft masks derived from alpha channel */
  GroupLuminositySoftMask = 5, /* For soft mask derived from luminosity */
  GroupNumUses /* ALWAYS LAST */
};

/** Return the UID number for a group. */
uint32 groupId(/*@notnull@*/ /*@in@*/ Group *group) ;

Bool groupOpen(DL_STATE *page,
               OBJECT colorspace,
               Bool isolated, Bool knockout, Bool banded,
               OBJECT *background,
               OBJECT *xferfn,
               TranAttrib *patternTA,
               GroupUsage groupusage,
               Group **newGroup);

/** The page group allows for a partial close to handle partial painting.
    Partial painting within subgroups is not allowed. */
#define groupClose(groupPtr, success) \
  groupCloseAny(groupPtr, FALSE, success)
#define groupClosePageGroup(groupPtr, partial, success) \
  groupCloseAny(groupPtr, partial, success)

Bool groupCloseAny(Group** groupPointer, Bool partial, Bool success);

HDL *groupHdl(/*@notnull@*/ /*@in@*/ Group *group);

DL_STATE *groupPage(/*@notnull@*/ /*@in@*/ Group *group);

Group *groupBase(/*@notnull@*/ /*@in@*/ Group *group) ;

Bool groupColorantToEquivalentRealColorant(/*@notnull@*/ /*@in@*/ Group *group,
                                           COLORANTINDEX ci,
                                           COLORANTINDEX **cimap);

/** Return the group's input raster style. */
GUCR_RASTERSTYLE *groupInputRasterStyle(Group *group);

/** Return the group's output raster style. */
GUCR_RASTERSTYLE *groupOutputRasterStyle(Group *group);

/** Return the usage of this group. */
GroupUsage groupGetUsage(Group *group);

/** Get the details required for HDLT Group callbacks. */
void groupGetHDLT(Group *group, Bool *isolated, Bool *knockout,
                  OBJECT *colorspace, USERVALUE **bgcolor, int32 *bgcolordim,
                  OBJECT *transferfn, int32 *transferfnid) ;

/* Public struct for essential attributes. */
typedef struct {
  Bool isolated, knockout, knockoutDescendant, hasShape, compositeToPage;
  SoftMaskType softMaskType;
  LateColorAttrib *lobjLCA; /**< Copy of lobj->objectstate->lateColorAttrib for
                                 use after Group.lobj is nulled. */
} GroupAttrs;

/** Get the values for the group attributes. */
const GroupAttrs *groupGetAttrs(Group *group);

Bool groupNonIsolatedGroups(Group *group);

#define groupIsSoftMask(_group) (groupGetAttrs(_group)->softMaskType != EmptySoftMask)

/* Find out if the group is inside a pattern. */
Bool groupInsidePattern(Group *group);

/** Return the Group's backdrop. */
struct Backdrop;
struct Backdrop *groupGetBackdrop(Group *group);

dbbox_t groupSoftMaskArea(Group *group) ;

Bool groupMustComposite(Group *group);

uint32 groupOverrideBlendMode(Group *group, uint32 blendMode);

/**
 * The group currently being rendered by the given render info.
 */
Group *groupRendering(/*@notnull@*/ /*@in@*/ const struct render_info_t *ri);

/**
 * Determine if currently rendering a sub-group.
 */
Bool groupRenderingSubgroup(/*@notnull@*/ /*@in@*/ const struct render_info_t *ri);

Bool groupRender(/*@notnull@*/ /*@in@*/ Group *group,
                 /*@notnull@*/ /*@in@*/ struct render_info_t *renderInfo,
                 /*@null@*/ /*@in@*/ TranAttrib *toParent);
struct Preconvert;
struct Preconvert *groupPreconvert(/*@notnull@*/ /*@in@*/ Group *group);

Group *groupParent(/*@notnull@*/ /*@in@*/ Group *group);

Bool groupSetColorAttribs(/*@notnull@*/ /*@in@*/ GS_COLORinfo *colorInfo,
                          int32 colorType, uint8 reproType,
                          /*@notnull@*/ /*@in@*/ LateColorAttrib *lca);

Bool groupRetainedSoftMask(const struct render_info_t *ri, const Group *group);

OBJECT *groupColorSpace(Group *group);

GS_COLORinfo *groupColorInfo(Group *group);

Bool groupRLEColorants(struct ColorantList *colorants, DL_STATE *page) ;

/** Debug/trace control for backdrop rendering */
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
extern int32 backdrop_render_debug;

enum {
  BR_DEBUG_TRACE                   = BIT(0),
  BR_DEBUG_ALL_TO_BACKDROP         = BIT(1),
  BR_DEBUG_SKIP_BACKDROP_REGIONS   = BIT(2),
  BR_DEBUG_SKIP_DIRECT_REGIONS     = BIT(3),
  BR_DEBUG_RENDER_REGION_CORNERS   = BIT(4),
  BR_DEBUG_COLORANTS               = BIT(5),
  BR_DEBUG_RENDER_INDIVIDUALLY     = BIT(6),
  BR_DISABLE_PRECONVERT_ALL        = BIT(7),
  BR_DISABLE_SOFTMASK_PER_BLOCK    = BIT(8),
  BR_DISABLE_GROUP_ELIMINATION     = BIT(9),
  BR_DISABLE_GROUP_ELIM_INTERSECT  = BIT(10),
  BR_IGNORE_NON_KNOCKOUT_SHAPE     = BIT(11)
};

void init_backdroprender_debug(void);
#endif

#endif /* __GROUP_H__ */

/* --Description--

The Group object allows all objects added to the display list between calls to
groupOpen() and groupClose() to be captured and stored within a Group object.
This collection of objects can then be rendered atomically as required by the
transparency specification.

This object is effectively a sub-class of a HDL, which it employs to capture
the display list objects. Rendering and partial paint handling are 'overridden'
by transparency-specific methods.
*/

/* Log stripped */
