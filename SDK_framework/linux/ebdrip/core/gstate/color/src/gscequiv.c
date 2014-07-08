/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscequiv.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines determining separation name -> CMYK & sRGB mappings.
 */

#include "core.h"

#include "dicthash.h"           /* systemdict */
#include "gschead.h"            /* gsc_setcolorspace */
#include "miscops.h"            /* run_ps_string */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "rcbcntrl.h"           /* rcbn_current_equiv_details */
#include "stackops.h"           /* STACK_POSITIONS */
#include "swerrors.h"           /* UNDEFINED */

#include "gs_colorpriv.h"       /* GS_COLORinfo */
#include "gsccrd.h"             /* gsc_setcolorrendering */
#include "gscequiv.h"

/*----------------------------------------------------------------------------*/
#if defined( DEBUG_BUILD )
#define DEBUG_SHOW_EQUIV(_name, _level, _equivs) \
  debug_show_equiv_((_name), (_level), (_equivs))

static void debug_show_equiv_(NAMECACHE *sep, int32 level, EQUIVCOLOR equivs);

/* Run time flag to control debugging output */
static int32 gsc_equiv_debug = 0;
#else
#define DEBUG_SHOW_EQUIV(_name, _level, _equivs) EMPTY_STATEMENT()
#endif

/*----------------------------------------------------------------------------*/
#define GSC_EQUIV_FILL(_res, _c, _m, _y, _k) MACRO_START                \
  ((_res))[0] = (_c);                                                   \
  ((_res))[1] = (_m);                                                   \
  ((_res))[2] = (_y);                                                   \
  ((_res))[3] = (_k);                                                   \
MACRO_END

typedef struct {
  OBJECT          *psColorSpace;
  Bool            usePSTintTransform;
  int32           sepPosition;
  COLORSPACE_ID   outputSpaceId;
  int32           equivColorType;
} EXTRADATA;

/*----------------------------------------------------------------------------*/
static Bool try_process_names(GS_COLORinfo *colorInfo,
                              NAMECACHE *sepname,
                              EQUIVCOLOR equivs,
                              Bool *foundEquivs,
                              EXTRADATA *extraData);
static Bool try_std_named_colors(GS_COLORinfo *colorInfo,
                                 NAMECACHE *sepname,
                                 EQUIVCOLOR equivs,
                                 Bool *foundEquivs,
                                 EXTRADATA *extraData);
static Bool try_rcb_named_colors(GS_COLORinfo *colorInfo,
                                 NAMECACHE *sepname,
                                 EQUIVCOLOR equivs,
                                 Bool *foundEquivs,
                                 EXTRADATA *extraData);
static Bool try_roam_named_colors(GS_COLORinfo *colorInfo,
                                  NAMECACHE *sepname,
                                  EQUIVCOLOR equivs,
                                  Bool *foundEquivs,
                                  EXTRADATA *extraData);
static Bool try_cmykcustom_color_comments(GS_COLORinfo *colorInfo,
                                          NAMECACHE *sepname,
                                          EQUIVCOLOR equivs,
                                          Bool *foundEquivs,
                                          EXTRADATA *extraData);
static Bool try_CMYKEquivalents(GS_COLORinfo *colorInfo,
                                NAMECACHE *sepname,
                                EQUIVCOLOR equivs,
                                Bool *foundEquivs,
                                EXTRADATA *extraData);
static Bool try_tint_transform(GS_COLORinfo *colorInfo,
                               NAMECACHE *sepname,
                               EQUIVCOLOR equivs,
                               Bool *foundEquivs,
                               EXTRADATA *extraData);
static Bool try_colorants_dict(GS_COLORinfo *colorInfo,
                               NAMECACHE *sepname,
                               EQUIVCOLOR equivs,
                               Bool *foundEquivs,
                               EXTRADATA *extraData);
static Bool read_colors_array(OBJECT *arrayo, EQUIVCOLOR equivs);
static Bool check_colorants_dictionary(const OBJECT* PSColorSpace,
                                       OBJECT** colorantsDict);
static Bool lookup_colorants_dictionary(const OBJECT* colorantsDict,
                                        NAMECACHE *sepname,
                                        OBJECT **separationCSA);

/*----------------------------------------------------------------------------*/
typedef Bool (*GSC_EQUIV_METHOD)(GS_COLORinfo *colorInfo,
                                 NAMECACHE *sepname,
                                 EQUIVCOLOR equivs,
                                 Bool *foundEquivs,
                                 EXTRADATA *extraData);

typedef struct {
  GSC_EQUIV_METHOD methodcall;
  int32 prioritylevel;
} GSC_EQUIV_INFO;

static GSC_EQUIV_INFO rcbCMYKMethods[] = {
  { try_process_names,             GSC_EQUIV_LVL_PROCESS },
  { try_std_named_colors,          GSC_EQUIV_LVL_STD_NAMEDCOLOR },
  { try_CMYKEquivalents,           GSC_EQUIV_LVL_TINT },
  { try_cmykcustom_color_comments, GSC_EQUIV_LVL_CUSTOMCOLOR },
  { try_rcb_named_colors,          GSC_EQUIV_LVL_RCB_NAMEDCOLOR }
};

static GSC_EQUIV_INFO stdCMYKMethods[] = {
  { try_std_named_colors,          GSC_EQUIV_LVL_STD_NAMEDCOLOR },
  { try_colorants_dict,            GSC_EQUIV_LVL_COLORANTS_DICT },
  { try_tint_transform,            GSC_EQUIV_LVL_TINT },
  { try_cmykcustom_color_comments, GSC_EQUIV_LVL_CUSTOMCOLOR }
};

static GSC_EQUIV_INFO roamMethods[] = {
  { try_roam_named_colors,         GSC_EQUIV_LVL_STD_NAMEDCOLOR },
  { try_colorants_dict,            GSC_EQUIV_LVL_COLORANTS_DICT },
  { try_tint_transform,            GSC_EQUIV_LVL_TINT },
  { try_cmykcustom_color_comments, GSC_EQUIV_LVL_CUSTOMCOLOR }
};

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/** Obtain CMYK equivalent color values for named colarants for use in recombine.
 * The color should be as accurate as possible to provide quality output. To that
 * end, a number of conversion methods are tried with a priority assigned to
 * each. Several may succeed, with the highest prioriry method being the one
 * used.
 * Whereever appropriate, the equivalent values should be compatible with Hqn
 * named color management. Hence, this is the highest priority method.
 * One feature of this method is the Recombine NamedColorOrder resource. For the
 * purpose of recombining pre-separated jobs, it's possible to add any an entry
 * for any non-standard colorants in one of the NamedColor databases named in
 * the Recombine resource.
 * It's possible that a given pre-separated job hasn't provided good equivalent
 * values for all the named colorants in the job and that we do not have one
 * in any of the available named color databases. The rip will fail such jobs,
 * but they can be made to succeed if an entry is added to one of the named color
 * databases, usually in one speicifc to the Recombine NamedColorOrder resource.
 */
Bool gsc_rcbequiv_lookup(GS_COLORinfo *colorInfo,
                         NAMECACHE *sepname,
                         EQUIVCOLOR equivs,
                         int32 *level)
{
  int32 len = sizeof(rcbCMYKMethods) / sizeof(GSC_EQUIV_INFO);
  int32 i = 0;
  EXTRADATA extraData;
  GS_COLORinfo localColorInfo;

  HQASSERT(sepname, "sepname NULL");
  HQASSERT(equivs, "equivs NULL");

  extraData.psColorSpace = NULL;      /* Unused */
  extraData.usePSTintTransform = -1;  /* Unused */
  extraData.sepPosition = -1;         /* Unused */
  extraData.outputSpaceId = SPACE_DeviceCMYK;
  extraData.equivColorType = GSC_FILL;

  for (i = 0; i < len; i++) {
    Bool foundEquivs = FALSE;

    if (rcbCMYKMethods[i].prioritylevel <= *level)
      continue;

    /* Wrap the function in a color context that will revert any colorspaces,
     * named color order resources, or anything else that might be put in the
     * gstate as a side effect.
     */
    gsc_copycolorinfo(&localColorInfo, colorInfo);
    localColorInfo.params.enableColorCache = 0;

    if (!rcbCMYKMethods[i].methodcall(&localColorInfo, sepname, equivs,
                                      &foundEquivs, &extraData)) {
      gsc_freecolorinfo(&localColorInfo);
      return FALSE;
    }

    if (foundEquivs)
      *level = rcbCMYKMethods[i].prioritylevel;

    gsc_freecolorinfo(&localColorInfo);
  }

  DEBUG_SHOW_EQUIV(sepname, *level, equivs);

  return TRUE;
}

/** Obtain CMYK equivalent color values for named colarants for use in converting
 * named colorants to process on devices that can't handle the colorant.
 * The color should be as accurate as possible to provide quality output. To that
 * end, a number of conversion methods are tried with a priority assigned to
 * each. Several may succeed, with the highest prioriry method being the one
 * used.
 * Hqn named color management via the Intercept NamedColorOrder resource is the
 * highest priority method as that typically provides device independent color.
 * While it's possible for one of the methods to throw an error, we should always
 * get equivalent colors from one of the methods because composite jobs must
 * always provide an alternate space and tint transform. The only exception being
 * TIFF images.
 */
Bool gsc_stdCMYKequiv_lookup(GS_COLORinfo *colorInfo,
                             NAMECACHE *sepname,
                             EQUIVCOLOR equivs,
                             OBJECT *psColorSpace,
                             Bool usePSTintTransform,
                             int32 sepPosition)
{
  int32 len = sizeof(stdCMYKMethods) / sizeof(GSC_EQUIV_INFO);
  int32 i = 0;
  int32 level = GSC_EQUIV_LVL_NONEKNOWN;
  EXTRADATA extraData;
  GS_COLORinfo localColorInfo;

  HQASSERT(sepname, "sepname NULL");
  HQASSERT(equivs, "equivs NULL");

  extraData.psColorSpace = psColorSpace;
  extraData.usePSTintTransform = usePSTintTransform;
  extraData.sepPosition = sepPosition;
  extraData.outputSpaceId = SPACE_DeviceCMYK;
  extraData.equivColorType = GSC_FILL;

  for (i = 0; i < len; i++) {
    Bool foundEquivs = FALSE;

    if (stdCMYKMethods[i].prioritylevel < level)
      continue;

    /* Wrap the function in a color context that will revert any colorspaces,
     * named color order resources, or anything else that might be put in the
     * gstate as a side effect.
     */
    gsc_copycolorinfo(&localColorInfo, colorInfo);
    localColorInfo.params.enableColorCache = 0;

    if (!stdCMYKMethods[i].methodcall(&localColorInfo, sepname, equivs,
                                      &foundEquivs, &extraData)) {
      gsc_freecolorinfo(&localColorInfo);
      return FALSE;
    }

    if (foundEquivs)
      level = stdCMYKMethods[i].prioritylevel;

    gsc_freecolorinfo(&localColorInfo);
  }

  DEBUG_SHOW_EQUIV(sepname, level, equivs);

  return TRUE;
}

/** Obtain sRGB equivalent color values for named colarants for use in
 * previewing applications such as Roam in an Hqn gui rip where separations are
 * produced.
 * Ideally, the Roam NamedColorOrder resource would provide a device independent
 * equivalent for the named colorant that could be rendered through the sRGB CRD.
 * If that isn't possible, other methods are attempted with a priority order.
 * If none of the methods succeed in providing an sRGB equivalent, it isn't
 * treated as an error because the plates are still produced - it's only the
 * previewing application that won't give a plausible representation of the plate.
 */
Bool gsc_roamRGBequiv_lookup(GS_COLORinfo *colorInfo,
                             NAMECACHE *sepname,
                             EQUIVCOLOR equivs,
                             OBJECT *psColorSpace,
                             Bool usePSTintTransform,
                             int32 sepPosition)
{
  int32 len = sizeof(roamMethods) / sizeof(GSC_EQUIV_INFO);
  int32 i = 0;
  int32 level = GSC_EQUIV_LVL_NONEKNOWN;
  EXTRADATA extraData;
  GS_COLORinfo localColorInfo;

  HQASSERT(sepname, "sepname NULL");
  HQASSERT(equivs, "equivs NULL");

  extraData.psColorSpace = psColorSpace;
  extraData.usePSTintTransform = usePSTintTransform;
  extraData.sepPosition = sepPosition;
  extraData.outputSpaceId = SPACE_DeviceRGB;
  extraData.equivColorType = GSC_FILL;

  for (i = 0; i < len; i++) {
    Bool foundEquivs = FALSE;
    STACK_POSITIONS stackPositions;

    if (roamMethods[i].prioritylevel < level)
      continue;

    /* Wrap the function in a color context that will revert any colorspaces,
     * named color order resources, or anything else that might be put in the
     * gstate as a side effect.
     */
    gsc_copycolorinfo(&localColorInfo, colorInfo);
    localColorInfo.params.enableColorCache = 0;

    /* Install sRGB CRD as the correct CRD for sRGB colour. Not all of the
     * methods in roamMethods will make use of a CRD, but that's ok because the
     * purpose of gsc_roamRGBequiv_lookup() is to create plausible sRGB colors
     * for roaming in previewing applications such as roam in a gui rip. For
     * the same reason, we won't throw an error if the CRD can't be set.
     * NB. The resource can be obtained via a PS callout, but it must be installed
     *     into the correct colorInfo.
     */
    saveStackPositions(&stackPositions);
    if (!run_ps_string((uint8 *) "/sRGB /ColorRendering findresource") ||
        !gsc_setcolorrendering(&localColorInfo, &operandstack))
    {
      HQFAIL("Setting the sRGB CRD failed.");
      error_clear();
      if ( !restoreStackPositions(&stackPositions, FALSE) )
        return error_handler(STACKUNDERFLOW) ;

      /* Don't treat this error as fatal because sRGB equivalents aren't
       * essential. */
    }

    if (!roamMethods[i].methodcall(&localColorInfo, sepname, equivs,
                                   &foundEquivs, &extraData)) {
      error_clear();
      if ( !restoreStackPositions(&stackPositions, FALSE) )
        return error_handler(STACKUNDERFLOW) ;

      /* Don't treat this error as fatal because sRGB equivalents aren't
       * essential. */
    }

    if (foundEquivs)
      level = roamMethods[i].prioritylevel;

    gsc_freecolorinfo(&localColorInfo);
  }

  DEBUG_SHOW_EQUIV(sepname, level, equivs);

  return TRUE;
}

/*----------------------------------------------------------------------------*/
void gsc_rcbequiv_handle_detectop(OBJECT *key, OBJECT *value)
{
  int32 *equivlevel;
  EQUIVCOLOR *equivs;
  int32 len;
  OBJECT *olist;
  NAMECACHE *sepname;

  HQASSERT(key, "key NULL");
  HQASSERT(value, "value NULL");
  HQASSERT(oType(*key) == ONAME, "name expected");
  /* Currently should only get /spots from freehand */
  HQASSERT(oName(*key) == system_names + NAME_spots, "unexpected name");

  if ( oType(*value) != OARRAY &&
       oType(*value) != OPACKEDARRAY )
    return;

  len = theILen(value);

  if ( ! len )
    return;

  /* Got an array, does it look like we expect: contain arrays of length 6 */
  olist = oArray(*value);
  for ( key = olist + len ; olist < key; ++olist ) {
    int32 type = oType(*olist);
    if ( type != OARRAY && type != OPACKEDARRAY )
      return;
    if ( theILen(olist) != 6 )
      return;
  }
  olist -= len;

  /* The array looks like it might be a freehand spots array, so lets risk it
     as long as a higher priority method hasn't set these values already */
  rcbn_current_equiv_details(&equivlevel, &equivs);
  if ( (*equivlevel) >= GSC_EQUIV_LVL_FREEHANDSPOTS )
    return;

  /* Get real name of current sep */
  sepname = rcbn_sepnmActual(rcbn_iterate(NULL));
  if ( ! sepname )
    return;

  for ( /* key set above */ ; olist < key; ++olist ) {
    OBJECT *sublist = oArray(*olist);
    if ( oType(sublist[4]) != OSTRING )
      return;
    if ( sepname == cachename(oString(sublist[4]),
                              theLen(sublist[4])) &&
         read_colors_array(olist, *equivs) ) {
      *equivlevel = GSC_EQUIV_LVL_FREEHANDSPOTS;
      return;
    }
  }
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
static Bool try_process_names(GS_COLORinfo *colorInfo,
                              NAMECACHE *sepname,
                              EQUIVCOLOR equivs,
                              Bool *foundEquivs,
                              EXTRADATA *extraData)
{
  UNUSED_PARAM(GS_COLORinfo *, colorInfo);
  UNUSED_PARAM(void *,extraData);

  switch ( theINameNumber(sepname) ) {
  case NAME_Cyan:    GSC_EQUIV_FILL(equivs, 1.0f, 0.0f, 0.0f, 0.0f); break;
  case NAME_Magenta: GSC_EQUIV_FILL(equivs, 0.0f, 1.0f, 0.0f, 0.0f); break;
  case NAME_Yellow:  GSC_EQUIV_FILL(equivs, 0.0f, 0.0f, 1.0f, 0.0f); break;
  case NAME_Black:   GSC_EQUIV_FILL(equivs, 0.0f, 0.0f, 0.0f, 1.0f); break;
  case NAME_Gray :   GSC_EQUIV_FILL(equivs, 0.0f, 0.0f, 0.0f, 1.0f); break;

  default: /* Not found */
    return TRUE;
  }

  *foundEquivs = TRUE;
  return TRUE;
}


/*----------------------------------------------------------------------------*/
static Bool try_cmykcustom_color_comments(GS_COLORinfo *colorInfo,
                                          NAMECACHE *sepname,
                                          EQUIVCOLOR equivs,
                                          Bool *foundEquivs,
                                          EXTRADATA *extraData)
{
  OBJECT *dict;
  OBJECT *convert;
  OBJECT nameobj = OBJECT_NOTVM_NOTHING;

  /* Get statusdict from systemdict */
  dict = fast_extract_hash_name(&systemdict, NAME_statusdict);
  if (dict == NULL)
    return FALSE;

  /* Get CMYKCustomColors dictionary from statusdict */
  dict = fast_extract_hash_name(dict, NAME_CMYKCustomColors);
  if (dict == NULL)
    return TRUE;    /* Not an error */

  if ( oType(*dict) != ODICTIONARY )
    return FALSE;

  /* Look up the separation name in CMYKCustomColors. If the colorant doesn't
   * exist it's not an error.
   */
  theTags(nameobj) = ONAME | LITERAL;
  oName(nameobj) = sepname;
  convert = fast_extract_hash(dict, &nameobj);
  if (convert == NULL)
    return TRUE;    /* Not an error */

  if (!read_colors_array(convert, equivs))
    return FALSE;


  if (extraData->outputSpaceId != SPACE_DeviceCMYK) {
    /* Install as the current colorspace and invoke it to RGB.
     * This call should always work, so any error are treated as fatal.
     */
    if (!gsc_setcolorspacedirect(colorInfo, extraData->equivColorType, SPACE_DeviceCMYK) ||
        !gsc_setcolordirect(colorInfo, extraData->equivColorType, equivs) ||
        !gsc_invokeChainTransform(colorInfo, extraData->equivColorType,
                                  extraData->outputSpaceId, FALSE,
                                  equivs)) {
      return FALSE;
    }
  }

  *foundEquivs = TRUE;
  return TRUE;
}

/*----------------------------------------------------------------------------*/
static Bool try_CMYKEquivalents(GS_COLORinfo *colorInfo,
                                NAMECACHE *sepname,
                                EQUIVCOLOR equivs,
                                Bool *foundEquivs,
                                EXTRADATA *extraData)
{
  OBJECT *dict;
  OBJECT *convert;
  OBJECT nameobj = OBJECT_NOTVM_NOTHING;

  UNUSED_PARAM(GS_COLORinfo *, colorInfo);
  UNUSED_PARAM(void *, extraData);

  /* Get statusdict from systemdict */
  dict = fast_extract_hash_name(&systemdict, NAME_statusdict);
  if (dict == NULL)
    return FALSE;

  /* Get CMYKEquivalents dictionary from statusdict */
  dict = fast_extract_hash_name(dict, NAME_CMYKEquivalents);
  if (dict == NULL)
    return TRUE;    /* Not an error */

  if (oType(*dict) != ODICTIONARY)
    return FALSE;

  /* Look up the separation name in CMYKEquivalents */
  theTags(nameobj) = ONAME | LITERAL;
  oName(nameobj) = sepname;
  convert = fast_extract_hash(dict, &nameobj);
  if (convert == NULL)
    return TRUE;    /* Not an error */

  if (!read_colors_array(convert, equivs))
    return FALSE;

  *foundEquivs = TRUE;
  return TRUE;
}

/*----------------------------------------------------------------------------*/
static Bool named_colors_common(GS_COLORinfo *colorInfo,
                                NAMECACHE *sepname,
                                EQUIVCOLOR equivs,
                                Bool *foundEquivs,
                                COLORSPACE_ID outputSpaceId,
                                int32 equivColorType)
{
  OBJECT colorSpaceObj = OBJECT_NOTVM_NOTHING;
  OBJECT separationSpace[4];
  OBJECT tintTransform[1];
  STACK_POSITIONS stackPositions;

  /* Create a Separation colorspace for sepname that can be invoked to get
   * equivalent cmyk values from invoking the color chain. We only want to know
   * about equivalents from the current named color database, so deliberately
   * set the tint transform to an illegal procedure that will cause an error
   * if an attempt were made to execute it in the event of sepname not being
   * found in the named color databases.
   * NB. This is a temporary color space that only exists for the duration of
   * this function. The color space MUST be restored in the client.
   */
  theTags(colorSpaceObj) = OARRAY | LITERAL | UNLIMITED;
  theLen(colorSpaceObj) = 4;
  oArray(colorSpaceObj) = separationSpace;

  object_store_name(object_slot_notvm(&separationSpace[0]), NAME_Separation, LITERAL);
  separationSpace[1] = onothing;         /* Struct copy to initialise slot properties */
  theTags(separationSpace[1]) = ONAME | LITERAL;
  oName(separationSpace[1]) = sepname;
  object_store_name(object_slot_notvm(&separationSpace[2]), NAME_DeviceGray, LITERAL);
  separationSpace[3] = onothing;         /* Struct copy to initialise slot properties */
  theTags(separationSpace[3]) = OARRAY | EXECUTABLE | EXECUTE_ONLY;
  theLen(separationSpace[3]) = 1;
  oArray(separationSpace[3]) = tintTransform;

  object_store_null(object_slot_notvm(&tintTransform[0]));  /* The illegal procedure */

  saveStackPositions(&stackPositions);

  /* Install as the current colorspace and invoke it. The colorspace will be
   * restored away by the client.
   * The color value required are the default solid for a Separation space.
   */
  if (!push(&colorSpaceObj, &operandstack) ||
      !gsc_setcolorspace(colorInfo, &operandstack, equivColorType))
    return FALSE;

  /* The color value required are the default solid for a Separation space. */
  if (!gsc_invokeChainTransform(colorInfo, equivColorType,
                                outputSpaceId, FALSE,
                                equivs)) {
    /* We didn't find sepname in these databases, so clear any error and roll the
     * stacks back to what they were.
     */
    error_clear();
    if ( !restoreStackPositions(&stackPositions, FALSE) )
      return error_handler(STACKUNDERFLOW) ;

    return TRUE ;
  }

  *foundEquivs = TRUE;
  return TRUE;
}

static Bool try_std_named_colors(GS_COLORinfo *colorInfo,
                                 NAMECACHE *sepname,
                                 EQUIVCOLOR equivs,
                                 Bool *foundEquivs,
                                 EXTRADATA *extraData)
{
  /* Get the named color equivalent (if present) that would be used in
   * non-recombine setups. This should be the highest priority method after
   * process colors.
   */
  return named_colors_common(colorInfo, sepname, equivs,
                             foundEquivs, extraData->outputSpaceId,
                             extraData->equivColorType);
}

static Bool try_rcb_named_colors(GS_COLORinfo *colorInfo,
                                 NAMECACHE *sepname,
                                 EQUIVCOLOR equivs,
                                 Bool *foundEquivs,
                                 EXTRADATA *extraData)
{
  OBJECT namedColorIntercept;

  /* Get the Recombine NamedColor resource and set it in the gstate. Then
   * attempt to get equivalent colors using the same methods as for non-recombine.
   * This is a low priority method if all else fails. NB. The NamedColor
   * databases used in this method can be quite different to the ones used in
   * the standard method above, often a superset just to allow the job to output.
   * NB. The resource can be obtained via a PS callout, but it must be installed
   *     into the correct colorInfo.
   */
  if (!run_ps_string((uint8*)"<< /NamedColor /Recombine /NamedColorOrder findresource >>")) {
    HQFAIL("Internal setting of Recombine NamedColorOrder resource failed");
    return error_handler(UNDEFINED);
  }

  Copy(&namedColorIntercept, theTop(operandstack));
  pop(&operandstack);

  if (!gsc_setinterceptcolorspace(colorInfo, &namedColorIntercept))
    return FALSE;

  return named_colors_common(colorInfo, sepname, equivs,
                             foundEquivs, extraData->outputSpaceId,
                             extraData->equivColorType);
}

static Bool try_roam_named_colors(GS_COLORinfo *colorInfo,
                                  NAMECACHE *sepname,
                                  EQUIVCOLOR equivs,
                                  Bool *foundEquivs,
                                  EXTRADATA *extraData)
{
  OBJECT namedColorIntercept;
  Bool foundResource;

  /* Find out if the Roam NamedColor resource exists; it is an optional resource.
   * If it doesn't exist, just return success with foundEquivs unset.
   */
  if (!run_ps_string((uint8*)"/Roam /NamedColorOrder resourcestatus")) {
    HQFAIL("Internal resourcestatus failed for Roam NamedColorOrder");
    /* If this fails then the tidyup is handled by the client. */
    return FALSE;
  }
  foundResource = oBool(*theTop(operandstack));
  pop(&operandstack);
  if (!foundResource)
    return TRUE;

  /* pop the other 2 items left by a successful resourcestatus */
  npop(2, &operandstack);

  /* The Roam resource exists so and set it in the gstate. Then attempt to
   * get equivalent colors using the named color interception.
   * NB1. The NamedColor databases used in this method can be quite different
   *      to the ones used in the standard method.
   * NB2. The resource can be obtained via a PS callout and placed into a dict
   *      ready for passing to setinterceptcolorspace, but it must be installed
   *      into the correct colorInfo.
   */
  if (!run_ps_string((uint8*)"<< /NamedColor  /Roam /NamedColorOrder findresource >>")) {
    HQFAIL("Internal findresource failed for Roam NamedColorOrder");
    /* If this fails then the tidyup is handled by the client. */
    return FALSE;
  }

  Copy(&namedColorIntercept, theTop(operandstack));
  pop(&operandstack);

  if (!gsc_setinterceptcolorspace(colorInfo, &namedColorIntercept))
    return FALSE;

  return named_colors_common(colorInfo, sepname, equivs,
                             foundEquivs, extraData->outputSpaceId,
                             extraData->equivColorType);
}

/*----------------------------------------------------------------------------*/
static Bool try_tint_transform(GS_COLORinfo *colorInfo,
                               NAMECACHE *sepname,
                               EQUIVCOLOR equivs,
                               Bool *foundEquivs,
                               EXTRADATA *extraData)
{
  USERVALUE *iColorValues;
  mm_size_t mem_size;
  int32 i;
  STACK_POSITIONS stackPositions;
  COLORSPACE_ID dummyColorSpaceId;
  int32 nColorants;

  /* It would be nice to get assert that sepname really is in the colorspace at
   * the right position.
   */
  UNUSED_PARAM(NAMECACHE *, sepname);

  HQASSERT(extraData->psColorSpace != NULL, "psColorSpace NULL");
  HQASSERT(extraData->sepPosition >= 0, "sepPosition negative");
  HQASSERT((extraData->usePSTintTransform & ~1) == 0, "usePSTintTransform not boolean");

  /* Don't use psColorSpace if the client doesn't think it contains a useful
   * tint transform.
   */
  if (!extraData->usePSTintTransform)
    return TRUE;

  saveStackPositions(&stackPositions);

  /* Install as the current colorspace and invoke it. The colorspace will be
   * restored away by the client */
  if (!push(extraData->psColorSpace, &operandstack) ||
      !gsc_setcolorspace(colorInfo, &operandstack, extraData->equivColorType))
    return FALSE;

  if ( !gsc_getcolorspacesizeandtype(colorInfo, extraData->psColorSpace,
                                     &dummyColorSpaceId, &nColorants))
    return FALSE;

  /* Allocate memory for input values */
  mem_size = nColorants * sizeof(USERVALUE);
  iColorValues = mm_alloc(mm_pool_color, mem_size, MM_ALLOC_CLASS_NCOLOR);
  if (iColorValues == NULL)
    return error_handler(VMERROR);

  /* And set just one input value to solid with the others clear */
  for (i = 0; i < nColorants; i++)
    iColorValues[i] = 0.0;
  iColorValues[extraData->sepPosition] = 1.0f;

  if (!gsc_setcolordirect(colorInfo, extraData->equivColorType, iColorValues))
    return FALSE;

  if (!gsc_invokeChainTransform(colorInfo, extraData->equivColorType,
                                extraData->outputSpaceId, FALSE,
                                equivs)) {
    /* Invoking the chain was unsuccessful. This won't be considered a fatal
     * error because there's a small chance that the color values we've put in
     * may be inappropriate .
     */
    mm_free(mm_pool_color, iColorValues, mem_size);
    error_clear();
    if ( !restoreStackPositions(&stackPositions, FALSE) )
      return error_handler(STACKUNDERFLOW) ;

    return TRUE;
  }

  mm_free(mm_pool_color, iColorValues, mem_size);

  *foundEquivs = TRUE;
  return TRUE;
}

/*----------------------------------------------------------------------------*/
static Bool try_colorants_dict(GS_COLORinfo *colorInfo,
                               NAMECACHE *sepname,
                               EQUIVCOLOR equivs,
                               Bool *foundEquivs,
                               EXTRADATA *extraData)
{
  STACK_POSITIONS stackPositions;
  OBJECT *colorantsDict = NULL;
  OBJECT *separationObj;

  HQASSERT(extraData->psColorSpace != NULL, "psColorSpace NULL");


  /* Get the Separation colorspace for sepname from the Colorants dict which is
   * an optional 5th element of a CSA. Use it to obtain equivalent values.
   */

  saveStackPositions(&stackPositions);

  /* See if there is a Colorants dictionary in a DeviceN PSColorSpace */
  if (!check_colorants_dictionary(extraData->psColorSpace, &colorantsDict))
    return FALSE;
  if (colorantsDict == NULL)
    return TRUE;    /* Not an error */

  if (!lookup_colorants_dictionary(colorantsDict, sepname, &separationObj)) {
    /* Creating the chain was unsuccessful. This won't be considered a fatal
     * error because the Colorants dict is optional so we'll continue as
     * though it weren't there.
     */
    error_clear();
    if ( !restoreStackPositions(&stackPositions, FALSE) )
      return error_handler(STACKUNDERFLOW) ;

    return TRUE;
  }

  /* Install as the current colorspace and invoke it. The colorspace will be
   * restored away by the client */
  if (!push(separationObj, &operandstack) ||
      !gsc_setcolorspace(colorInfo, &operandstack, extraData->equivColorType)) {
    /* Creating the chain was unsuccessful. This won't be considered a fatal
     * error because the Colorants dict is optional so we'll continue as
     * though it weren't there.
     */
    error_clear();
    if ( !restoreStackPositions(&stackPositions, FALSE) )
      return error_handler(STACKUNDERFLOW) ;

    return TRUE;
  }

  /* The default color value will be for the solid - which is what we want */

  if (!gsc_invokeChainTransform(colorInfo, extraData->equivColorType,
                                extraData->outputSpaceId, FALSE,
                                equivs)) {
    /* Invoking the chain was unsuccessful. This won't be considered a fatal
     * error because the Colorants dict is optional so we'll continue as
     * though it weren't there.
     */
    error_clear();
    if ( !restoreStackPositions(&stackPositions, FALSE) )
      return error_handler(STACKUNDERFLOW) ;

    return TRUE;
  }

  *foundEquivs = TRUE;
  return TRUE;
}

/*----------------------------------------------------------------------------*/
static Bool read_colors_array(OBJECT *arrayo, EQUIVCOLOR equivs)
{
  int32 i;

  if ( oType(*arrayo) != OARRAY &&
       oType(*arrayo) != OPACKEDARRAY )
    return FALSE;
  if ( theILen(arrayo) < 4 )
    return FALSE;

  arrayo = oArray(*arrayo);
  for ( i = 0 ; i < 4; ++i )
    if ( oType(arrayo[i]) != OINTEGER &&
         oType(arrayo[i]) != OREAL )
      return FALSE;

  for ( i = 0 ; i < 4; ++i ) {
    if ( oType(arrayo[i]) == OREAL )
      equivs[i] = oReal(arrayo[i]);
    else
      equivs[i] = (USERVALUE)oInteger(arrayo[i]);
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/** check_colorants_dictionary takes a DeviceN colorspace and
    returns a pointer to the Colorants dictionary if there is one. */
static Bool check_colorants_dictionary(
  /*@notnull@*/                        const OBJECT *PSColorSpace,
  /*@notnull@*/ /*@out@*/              OBJECT **colorantsDict)
{
  OBJECT* theo;
  int32 len;

  *colorantsDict = NULL;

  theo = oArray(*PSColorSpace) ;  /* DeviceN array */

  /* Ignore non-DeviceN color spaces */
  if (oName(*theo) != system_names + NAME_DeviceN)
    return TRUE;

  HQASSERT(colorantsDict != NULL,
           "null colorants dictionary pointer" );

  *colorantsDict = NULL;

  len = theILen( PSColorSpace );

  if (len == 5) {
    /* we should have an attributes dictionary */

    theo += 4 ;         /* attributes dict */

    if ( oType(*theo) != ODICTIONARY ) {
      return error_handler( TYPECHECK );
    }

    *colorantsDict = fast_extract_hash_name(theo, NAME_Colorants);

    /* if simply not found, ignore the fact */
    if (*colorantsDict != NULL) {
      if ( oType(**colorantsDict) != ODICTIONARY ) {
        return error_handler( TYPECHECK );
      }
    }
  }
  return TRUE;
}

/*----------------------------------------------------------------------------*/
/** lookup_colorants_dictionary looks up the given separation name in the given
    Colorants dictionary */
static Bool lookup_colorants_dictionary(
  /*@notnull@*/                       const OBJECT *colorantsDict,
  /*@notnull@*/                       NAMECACHE *sepname,
  /*@notnull@*/ /*@out@*/             OBJECT **separationCSA)
{
  OBJECT colorant;

  HQASSERT( sepname != NULL, "null separation name");
  HQASSERT( separationCSA != NULL, "null separationCSA flag");
  HQASSERT(oType(*colorantsDict) == ODICTIONARY,
           "colorantsDict is not a dictionary");

  theTags(colorant) = ONAME | LITERAL;
  oName(colorant) = sepname;

  /* separation should be an array, e.g. [/Separation /MyPink /DeviceCMYK {tinttransform}] */
  *separationCSA = fast_extract_hash(colorantsDict, &colorant);

  return *separationCSA != NULL;
}

/*----------------------------------------------------------------------------*/
#if defined( DEBUG_BUILD )

static void debug_show_equiv_(NAMECACHE *sepname, int32 level, EQUIVCOLOR equivs)
{
  int32 i;
  char *det_str;

  if ( ! gsc_equiv_debug )
    return;

  switch (level) {
  case GSC_EQUIV_LVL_PROCESS:        det_str = "Process Color";    break;
  case GSC_EQUIV_LVL_STD_NAMEDCOLOR: det_str = "STD Named Color";  break;
  case GSC_EQUIV_LVL_COLORANTS_DICT: det_str = "Colorants dict";   break;
  case GSC_EQUIV_LVL_TINT:           det_str = "Tint transform";   break;
  case GSC_EQUIV_LVL_CUSTOMCOLOR:    det_str = "CMYKCustomColor";  break;
  case GSC_EQUIV_LVL_FREEHANDSPOTS:  det_str = "Spots Array";      break;
  case GSC_EQUIV_LVL_RCB_NAMEDCOLOR: det_str = "RCB Named Color";  break;
  case GSC_EQUIV_LVL_NONEKNOWN:      det_str = "Not Found";        break;
  default:  HQFAIL("Unknown recombine detection method (ignorable)");
                                    det_str = "Unknown";          break;
  }
  monitorf((uint8*)"Equiv Detection Method %d: %-15s CMYK: ", level, det_str);

  /* vswcopyf ignores precision format specifiers for floats, so jump through
     some hoops to make it look nice.  Not very efficient though. */
  for ( i = 0; i < 4; ++i ) {
    USERVALUE val = equivs[i];
    if ( val == 0.0f )
      monitorf((uint8*)"0.000 ");
    else if ( val == 1.0f )
      monitorf((uint8*)"1.000 ");
    else {
      int32 sf;
      monitorf((uint8*)"0.");
      for ( sf = 0; sf < 3 ; ++sf ) {
        val = (val - (int32)val) * 10;
        monitorf((uint8*)"%d", (int32)val);
      }
      monitorf((uint8*)" ");
    }
  }

  monitorf((uint8*)"PlateColor: '%.*s'\n", sepname->len, sepname->clist);

}
#endif

/* Log stripped */
