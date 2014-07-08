/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscparams.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color module params.
 */


#include "core.h"

#include "coreparams.h"         /* module_params_t */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "dlstate.h"            /* DL_STATE */
#include "gcscan.h"
#include "graphics.h"           /* GSTATE */
#include "gstate.h"             /* gs_forall */
#include "namedef_.h"
#include "swoften.h"            /* public file */
#include "swerrors.h"
#include "swdevice.h"
#include "swctype.h"
#include "swstart.h"
#include "coreinit.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"

#include "gs_colorpriv.h"       /* PAGEDEVICE_MIRROR */
#include "gsccalib.h"           /* gsc_validateCalibrationArray */
#include "gs_cache.h"           /* GSC_ENABLE_ALL_COLOR_CACHES */
#include "dicthash.h"           /* CopyDictionary */
#include "gschtone.h"           /* gsc_redo_setscreen */

#include "gscparamspriv.h"
#include "functns.h"
#include "gs_callps.h"

typedef struct {
  OBJECT  *object;
  int32   saveLevel;
  int32   paramName;
} paramValue_t;


/* Modular color system/userparams */
static Bool gsc_set_systemparam(corecontext_t *context,
                                uint16 name, OBJECT *theo);
static Bool gsc_get_systemparam(corecontext_t *context,
                                uint16 name, OBJECT *result);

static NAMETYPEMATCH gsc_system_match[] = {
  { NAME_Overprint | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ImmediateRepro | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_UseAllSetScreen | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AdobeCurrentHalftone | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_TableBasedColor | OOPTIONAL, 1, {OBOOLEAN}},
  { NAME_ForcePositive | OOPTIONAL, 1 , {OBOOLEAN}},
  DUMMY_END_MATCH
};

static module_params_t color_system_params = {
  gsc_system_match,
  gsc_set_systemparam,
  gsc_get_systemparam,
  NULL
};


static Bool gsc_set_userparam(corecontext_t *context,
                              uint16 name, OBJECT *theo);
static Bool gsc_get_userparam(corecontext_t *context,
                              uint16 name, OBJECT *result);
static Bool setParamForSaveLevel(GSTATE * gs, void *arg);
static Bool checkKeyIsName(OBJECT * key, OBJECT * value, void *arg);


static NAMETYPEMATCH gsc_user_match[] = {
  { NAME_OverprintProcess   | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OverprintBlack     | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OverprintGray      | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OverprintGrayImages| OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OverprintWhite     | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_IgnoreOverprintMode    | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_TransformedSpotOverprint | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OverprintICCBased | OOPTIONAL, 1, { OBOOLEAN }},

  { NAME_EnableColorCache   | OOPTIONAL, 1, { OINTEGER }},
  { NAME_ExcludedSeparations    | OOPTIONAL, 2, { ONULL, ODICTIONARY }},
  { NAME_PhotoshopInput         | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AdobeProcessSeparations | OOPTIONAL, 1, { OBOOLEAN }},

  { NAME_AbortOnBadICCProfile   | OOPTIONAL, 1, { OBOOLEAN }},

  { NAME_DuplicateColorants     | OOPTIONAL, 1, { ONAME }},
  { NAME_NegativeJob              | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_IgnoreSetTransfer        | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_IgnoreSetBlackGeneration | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_TransferFunction       | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
  { NAME_UseAllSetTransfer      | OOPTIONAL, 1, { OBOOLEAN }},

  { NAME_FasterColorMethod      | OOPTIONAL, 1, { ONAME }},
  { NAME_FasterColorGridPoints  | OOPTIONAL, 1, { OINTEGER }},
  { NAME_FasterColorSmoothness  | OOPTIONAL, 1, { OREAL }},
  { NAME_HalftoneColorantMapping | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_UseFastRGBToCMYK       | OOPTIONAL, 1, {OBOOLEAN}},
  { NAME_RGBToCMYKMethod        | OOPTIONAL, 1, {OINTEGER}},
DUMMY_END_MATCH
};

static module_params_t color_user_params = {
  gsc_user_match,
  gsc_set_userparam,
  gsc_get_userparam,
  NULL
};


/** Initialise the color system params and insert in system param list */
static Bool colorSystemParamsInit(corecontext_t *context)
{
  COLOR_SYSTEM_PARAMS *color_systemparams;

  /* Initialise color configuration parameters */
  color_systemparams = mm_alloc_static(sizeof(COLOR_SYSTEM_PARAMS));
  if (color_systemparams == NULL)
    return FALSE;
  context->color_systemparams = color_systemparams;

  color_systemparams->Overprint = TRUE;
  color_systemparams->ImmediateRepro = FALSE;
  color_systemparams->UseAllSetScreen = TRUE;
  color_systemparams->AdobeCurrentHalftone = FALSE;
  color_systemparams->TableBasedColor = TRUE;
  color_systemparams->ForcePositive = FALSE;

  HQASSERT(color_system_params.next == NULL,
           "Already linked system params accessor");

  /* Link accessors into global list */
  color_system_params.next = context->systemparamlist;
  context->systemparamlist = &color_system_params;

  return TRUE;
}

/** Initialise the color user params and insert in user param list */
static Bool colorUserParamsInit(corecontext_t *context)
{
  COLOR_USER_PARAMS *colorParams;

  /* Initialise color configuration parameters */
  colorParams = mm_alloc_static(sizeof(COLOR_USER_PARAMS));
  if (colorParams == NULL)
    return FALSE;
  context->color_userparams = colorParams;

  colorParams->OverprintProcess = INITIAL_OVERPRINT_MODE;
  colorParams->OverprintBlack = INITIAL_OVERPRINT_BLACK;
  colorParams->OverprintGray = INITIAL_OVERPRINT_GRAY;
  colorParams->OverprintGrayImages = INITIAL_OVERPRINT_GRAYIMAGES;
  colorParams->OverprintWhite = INITIAL_OVERPRINT_WHITE;
  colorParams->IgnoreOverprintMode = INITIAL_IGNORE_OVERPRINT_MODE;
  colorParams->TransformedSpotOverprint = INITIAL_TRANSFORMED_SPOT_OVERPRINT;
  colorParams->OverprintICCBased = INITIAL_OVERPRINT_ICCBASED;

  colorParams->EnableColorCache = GSC_ENABLE_ALL_COLOR_CACHES;
  colorParams->ExcludedSeparations = onull; /* Struct copy to set slot properties */
  colorParams->PhotoshopInput = FALSE;
  colorParams->AdobeProcessSeparations = FALSE;

  colorParams->AbortOnBadICCProfile = FALSE;
  colorParams->DuplicateColorants = NAME_Last;

  colorParams->NegativeJob = FALSE;
  colorParams->IgnoreSetTransfer = FALSE;
  colorParams->IgnoreSetBlackGeneration = FALSE;
  (void)object_slot_notvm(&colorParams->TransferFunction);
  theTags(colorParams->TransferFunction) = OARRAY | LITERAL | UNLIMITED;
  theLen(colorParams->TransferFunction) = 0;
  oArray(colorParams->TransferFunction) = NULL;
  colorParams->TransferFunctionId = 0;
  colorParams->UseAllSetTransfer = TRUE;

  colorParams->FasterColorMethod = NAME_Tetrahedral;
  colorParams->FasterColorGridPoints = 16;
  colorParams->FasterColorSmoothness = 1.0f;
  colorParams->HalftoneColorantMapping = TRUE;

  colorParams->UseFastRGBToCMYK = TRUE;
  colorParams->RGBToCMYKMethod = 0;

  /* Link accessors into global list */
  color_user_params.next = context->userparamlist;
  context->userparamlist = &color_user_params;

  return TRUE;
}

/* Routine to initialise the color subsystem. This routine is called before any
 * other routines in this module via swstart(). */
Bool gsc_paramsInit(void)
{
  corecontext_t *context = get_core_context();

  if (!colorSystemParamsInit(context) ||
      !colorUserParamsInit(context))
    return FAILURE(FALSE);

  return TRUE;
}

/** finish colorUserParams - deinitialization for colorUserParams */
void gsc_params_finish(void)
{
  COLOR_USER_PARAMS *color_userparams = get_core_context()->color_userparams;
  color_userparams->TransferFunction = onull; /* Struct copy to set slot properties */
  color_userparams->ExcludedSeparations = onull; /* Struct copy to set slot properties */
}

/* Called from swinit() */
void init_C_globals_gscparams(void)
{
  color_system_params.next = NULL;
  color_user_params.next = NULL;
}

/** Setter for a color system param.
 * NB. System params are a front end construct, while most of these params
 * are also referenced in color conversions at the back end. Therefore, the
 * color system params are only accessed directly in currentsystemparams; all
 * other references use working copies in the DL_STATE.
 * The value of the params used in the back end is the one pertaining at the
 * hand-over from the front to the back end.
 */
static Bool gsc_set_systemparam(corecontext_t *context,
                                uint16 name, OBJECT *theo)
{
  COLOR_SYSTEM_PARAMS *colorSystemParams;
  COLOR_PAGE_PARAMS *colorPageParams;

  colorSystemParams = context->color_systemparams;
  colorPageParams = &context->page->colorPageParams;

  switch (name) {
  case NAME_Overprint:
    if ( colorSystemParams->Overprint != oBool(*theo) ) {
      gs_invalidateAllColorChains();
      colorSystemParams->Overprint = (int8)oBool(*theo);
    }
    break;

  case NAME_ImmediateRepro:
    if ( colorSystemParams->ImmediateRepro != oBool(*theo) ) {
      gs_invalidateAllColorChains();
      colorSystemParams->ImmediateRepro = (uint8)oBool(*theo);
    }
    break;

  case NAME_UseAllSetScreen:
    colorSystemParams->UseAllSetScreen = (uint8)oBool(*theo);
    if ( ! gsc_redo_setscreen( gstateptr->colorInfo ))
      return FALSE;
    break;

  case NAME_AdobeCurrentHalftone:
    colorSystemParams->AdobeCurrentHalftone = (int8)oBool(*theo);
    break;

  case NAME_TableBasedColor: /* Lazy-tables plus interpolation for CIEBased spaces */
    if ( colorSystemParams->TableBasedColor != oBool(*theo) ) {
      gs_invalidateAllColorChains();
      colorSystemParams->TableBasedColor = (uint8)oBool(*theo);
    }
    break;

  case NAME_ForcePositive:
    if ( colorSystemParams->ForcePositive != oBool(*theo) ) {
      gs_invalidateAllColorChains();
      colorSystemParams->ForcePositive = (uint8)oBool(*theo);
    }
    break;

  case NAMES_COUNTED: /* Finaliser */
      /* Copy all values into the DL_STATE. It is all values because this also
       * serves as the initialiser - I don't want to put knowledge of the
       * these values in the DL_STATE initialiser.
       */
      colorPageParams->overprintsEnabled = colorSystemParams->Overprint;
      colorPageParams->immediateRepro = colorSystemParams->ImmediateRepro;
      colorPageParams->useAllSetScreen = colorSystemParams->UseAllSetScreen;
      colorPageParams->adobeCurrentHalftone = colorSystemParams->AdobeCurrentHalftone;
      colorPageParams->tableBasedColor = colorSystemParams->TableBasedColor;
      colorPageParams->forcePositive = colorSystemParams->ForcePositive;
    break;
  }

  return TRUE;
}

/** Getter for a color system param */
static Bool gsc_get_systemparam(corecontext_t *context,
                                uint16 name, OBJECT *result)
{
  COLOR_SYSTEM_PARAMS   *colorSystemParams;

  colorSystemParams = context->color_systemparams;

  switch (name) {
  case NAME_Overprint:
    object_store_bool(result, colorSystemParams->Overprint);
    break;

  case NAME_ImmediateRepro:
    object_store_bool(result, colorSystemParams->ImmediateRepro);
    break;

  case NAME_UseAllSetScreen:
    object_store_bool(result, colorSystemParams->UseAllSetScreen);
    break;

  case NAME_AdobeCurrentHalftone:
    object_store_bool(result, colorSystemParams->AdobeCurrentHalftone);
    break;

  case NAME_TableBasedColor:
    /* TableBasedColor: Lazy-tables plus interpolation for CIEBased spaces */
    object_store_bool(result, colorSystemParams->TableBasedColor);
    break;

  case NAME_ForcePositive:
    object_store_bool(result, colorSystemParams->ForcePositive);
    break;
  }

  return TRUE;
}

/** Setter for a color user param of the gstate type.
 * Transfer param values to all gstates at the current save level.
 * See gsc_set_userparam().
 */
static Bool gsc_set_gstateparam(corecontext_t *context,
                                uint16 name, OBJECT *theo)
{
  COLOR_USER_PARAMS *colorUserParams;
  paramValue_t paramData;
  Bool updateGstate = FALSE;

  colorUserParams = context->color_userparams;

  switch (name) {
  case NAME_OverprintProcess:
  case NAME_OverprintBlack:
  case NAME_OverprintGray:
  case NAME_OverprintGrayImages:
  case NAME_OverprintWhite:
  case NAME_IgnoreOverprintMode:
  case NAME_TransformedSpotOverprint:
  case NAME_OverprintICCBased:
    {
      Bool *param = NULL;

      switch (name) {
      case NAME_OverprintProcess:         param = &colorUserParams->OverprintProcess;         break;
      case NAME_OverprintBlack:           param = &colorUserParams->OverprintBlack;           break;
      case NAME_OverprintGray:            param = &colorUserParams->OverprintGray;            break;
      case NAME_OverprintGrayImages:      param = &colorUserParams->OverprintGrayImages;      break;
      case NAME_OverprintWhite:           param = &colorUserParams->OverprintWhite;           break;
      case NAME_IgnoreOverprintMode:      param = &colorUserParams->IgnoreOverprintMode;      break;
      case NAME_TransformedSpotOverprint: param = &colorUserParams->TransformedSpotOverprint; break;
      case NAME_OverprintICCBased:        param = &colorUserParams->OverprintICCBased;        break;
      }
      *param = (int8)oBool(*theo);

      /* There is no need to call gs_InvalidColorForSaveLevel() for these params
       * because setting the overprint state in the gstate via the callee
       * functions will invalidate all chains which have actually changed value.
       */
      updateGstate = TRUE;
    }
    break;

  case NAME_EnableColorCache:
    if ( colorUserParams->EnableColorCache != oInteger(*theo) ) {
      colorUserParams->EnableColorCache = oInteger(*theo);
      gs_InvalidColorForSaveLevel();
      updateGstate = TRUE;
    }
    break;

  case NAME_ExcludedSeparations:
    /* If there was a non-trivial value before, just invalidate the chains
     * without attempting to compare dictionary values.
     */
    if (oType(colorUserParams->ExcludedSeparations) != ONULL) {
      gs_InvalidColorForSaveLevel();
      updateGstate = TRUE;
    }

    colorUserParams->ExcludedSeparations = *theo;
    if ( oType( *theo ) == ODICTIONARY ) {
      int length;

      getDictLength(length, theo);
      if (!walk_dictionary(theo, checkKeyIsName, NULL))
        return FALSE;
      gs_InvalidColorForSaveLevel();
      updateGstate = TRUE;
    }
    break;

  case NAME_PhotoshopInput:
    if ( colorUserParams->PhotoshopInput != oBool(*theo) ) {
      colorUserParams->PhotoshopInput = (int8)oBool(*theo);
      gs_InvalidColorForSaveLevel();
      updateGstate = TRUE;
    }
    break;

  case NAME_AdobeProcessSeparations:
    if ( colorUserParams->AdobeProcessSeparations != oBool(*theo) ) {
      colorUserParams->AdobeProcessSeparations = (int8)oBool(*theo);
      gs_InvalidColorForSaveLevel();
      updateGstate = TRUE;
    }
    break;

  case NAME_UseFastRGBToCMYK:
    if (colorUserParams->UseFastRGBToCMYK != oBool(*theo)) {
      colorUserParams->UseFastRGBToCMYK = (int8)oBool(*theo);
      updateGstate = TRUE;
    }
    break;

  case NAME_RGBToCMYKMethod:
    if (colorUserParams->RGBToCMYKMethod != oInteger(*theo)) {
      colorUserParams->RGBToCMYKMethod = oInteger(*theo);
      updateGstate = TRUE;
    }
    break;
  }

  /* Transfer the param value to the working copy in all gstates in the current
   * save level.
   */
  if (updateGstate) {
    paramData.object = theo;
    paramData.saveLevel = context->savelevel;
    paramData.paramName = name;
    if (!gs_forall(setParamForSaveLevel, (void *) &paramData, TRUE, TRUE))
      return FALSE;
  }

  return TRUE;
}

/** Setter for a color user param of the pagedevice type.
 * Store the value, which doesn't become effective until setpagedevice, via
 * gsc_pagedevice().
 * See gsc_set_userparam().
 */
static Bool gsc_set_pageparam(COLOR_USER_PARAMS *colorUserParams,
                              uint16 name, OBJECT *theo)
{
  switch (name) {
  case NAME_AbortOnBadICCProfile:
    colorUserParams->AbortOnBadICCProfile = (int8)oBool(*theo);
    break;

  case NAME_DuplicateColorants: {
    NAMECACHE* dupcolorants = oName(*theo);

    if (dupcolorants->namenumber != NAME_First &&
        dupcolorants->namenumber != NAME_Last)
      return error_handler(RANGECHECK);

    colorUserParams->DuplicateColorants = dupcolorants->namenumber;
    break;
  }

  case NAME_NegativeJob:
    colorUserParams->NegativeJob = (int8)oBool(*theo);
    break;

  case NAME_IgnoreSetTransfer:
    colorUserParams->IgnoreSetTransfer = (int8)oBool(*theo);
    break;

  case NAME_IgnoreSetBlackGeneration:
    colorUserParams->IgnoreSetBlackGeneration = (int8)oBool(*theo);
    break;

  case NAME_TransferFunction:
    {
      static int32 TransferFunctionId = 0;
      /* If not executable, the transfer must be an array of number pairs */
      if ( !oExecutable(*theo))
        if ( !gsc_validateCalibrationArray(theo) )
          return FALSE;
      OCopy(colorUserParams->TransferFunction, *theo);
      if (theLen(*theo) == 0)
        colorUserParams->TransferFunctionId = 0;
      else
        colorUserParams->TransferFunctionId = ++TransferFunctionId;
    }
    break;

  case NAME_UseAllSetTransfer:
    colorUserParams->UseAllSetTransfer = (int8)oBool(*theo);
    break;

  case NAME_FasterColorMethod:
    switch (oNameNumber(*theo)) {
    case NAME_Cubic:
    case NAME_Tetrahedral:
       colorUserParams->FasterColorMethod = oNameNumber(*theo);
       break;
    default:
      return detail_error_handler(RANGECHECK, "Invalid FasterColorMethod");
    }
    break;

  case NAME_FasterColorGridPoints:
    if (oInteger(*theo) < 2 || oInteger(*theo) > 64)
      return detail_error_handler(RANGECHECK, "FasterColorGridPoints out of range");
    colorUserParams->FasterColorGridPoints = (uint8) oInteger(*theo);
    break;

  case NAME_FasterColorSmoothness:
    colorUserParams->FasterColorSmoothness = oReal(*theo);
    break;

  case NAME_HalftoneColorantMapping:
    colorUserParams->HalftoneColorantMapping = (int8)oBool(*theo);
    break;

  case NAME_RGBToCMYKMethod:
    if (oInteger(*theo) < 0 || oInteger(*theo) > 2) {
      return (error_handler(RANGECHECK));
    }
    break;
  }

  return TRUE;
}

/** Setter for a color user param.
 * NB. User params are a front end construct, while most of these params
 * are also referenced in color conversions at the back end. Therefore, the
 * color user params are only accessed directly in currentuserparams; all
 * other references use working copies.
 * Color user params are all treated internally as though they were either
 * pagedevice or gstate params:
 * - the pagedevice params have working copies in the DL_STATE and are only
 *   updated during setpagedevice. Any changes to the user param after the
 *   setpagedevice is stored, but doesn't update the working copy.
 * - the gstate params have working copies in the gstate. In the front end,
 *   changes to these user params are reflected in the gstate. In the back end,
 *   the values are taken from the gstate pertaining after BeginPage.
 */
static Bool gsc_set_userparam(corecontext_t *context,
                              uint16 name, OBJECT *theo)
{
  COLOR_USER_PARAMS *colorUserParams;

  colorUserParams = context->color_userparams;

  if (!gsc_set_gstateparam(context, name, theo))
    return FALSE;

  if (!gsc_set_pageparam(colorUserParams, name, theo))
    return FALSE;

  return TRUE;
}

/** Getter for a color user param.
 * See gsc_set_userparam().
 */
static Bool gsc_get_userparam(corecontext_t *context,
                              uint16 name, OBJECT *result)
{
  COLOR_USER_PARAMS *colorUserParams;

  colorUserParams = context->color_userparams;

  switch (name) {
  case NAME_OverprintProcess:
    object_store_bool(result, colorUserParams->OverprintProcess);
    break;

  case NAME_OverprintBlack:
    object_store_bool(result, colorUserParams->OverprintBlack);
    break;

  case NAME_OverprintGray:
    object_store_bool(result, colorUserParams->OverprintGray);
    break;

  case NAME_OverprintGrayImages:
    object_store_bool(result, colorUserParams->OverprintGrayImages);
    break;

  case NAME_OverprintWhite:
    object_store_bool(result, colorUserParams->OverprintWhite);
    break;

  case NAME_IgnoreOverprintMode:
    object_store_bool(result, colorUserParams->IgnoreOverprintMode);
    break;

  case NAME_TransformedSpotOverprint:
    object_store_bool(result, colorUserParams->TransformedSpotOverprint);
    break;

  case NAME_OverprintICCBased:
    object_store_bool(result, colorUserParams->OverprintICCBased);
    break;

  case NAME_EnableColorCache:
    object_store_integer(result, colorUserParams->EnableColorCache);
    break;

  case NAME_ExcludedSeparations:
    if (oType(colorUserParams->ExcludedSeparations) == ODICTIONARY) {
    /* This dictionary is only allowed to be one level deep so a shallow copy is fine */
      if (!ps_dictionary(result, 4))
        return FALSE;
      if (!CopyDictionary(&colorUserParams->ExcludedSeparations, result, NULL, NULL))
        return FALSE;
    }
    else
      object_store_null(result);
    break;

  case NAME_PhotoshopInput:
    object_store_bool(result, colorUserParams->PhotoshopInput);
    break;

  case NAME_AdobeProcessSeparations:
    object_store_bool(result, colorUserParams->AdobeProcessSeparations);
    break;

  case NAME_AbortOnBadICCProfile:
    object_store_bool(result, colorUserParams->AbortOnBadICCProfile);
    break;

  case NAME_DuplicateColorants:
    object_store_name(result, colorUserParams->DuplicateColorants, LITERAL);
    break;

  case NAME_NegativeJob:
    object_store_bool(result, colorUserParams->NegativeJob);
    break;

  case NAME_IgnoreSetTransfer:
    object_store_bool(result, colorUserParams->IgnoreSetTransfer);
    break;

  case NAME_IgnoreSetBlackGeneration:
    object_store_bool(result, colorUserParams->IgnoreSetBlackGeneration);
    break;

  case NAME_TransferFunction:
    {
      uint32  count;
      OBJECT  oarray = OBJECT_NOTVM_NOTHING;
      OBJECT* olist;
      OBJECT* olisttf;

      OCopy(oarray, colorUserParams->TransferFunction);
      SETGLOBJECT(oarray, context);
      count = theLen(oarray);
      if ( count > 0 ) {
        olist = get_omemory(count);
        if ( olist == NULL ) {
          return error_handler(VMERROR);
        }
        oArray(oarray) = olist;
        for ( olisttf = oArray(colorUserParams->TransferFunction);
              count-- > 0;
              olist++, olisttf++ ) {
          Copy(olist, olisttf);
        }
      }
      OCopy(*result, oarray);
    }
    break;

  case NAME_UseAllSetTransfer:
    object_store_bool(result, colorUserParams->UseAllSetTransfer);
    break;

  case NAME_FasterColorMethod:
    object_store_name(result, colorUserParams->FasterColorMethod, LITERAL);
    break;

  case NAME_FasterColorGridPoints:
    object_store_integer(result, colorUserParams->FasterColorGridPoints);
    break;

  case NAME_FasterColorSmoothness:
    object_store_real(result, colorUserParams->FasterColorSmoothness);
    break;

  case NAME_HalftoneColorantMapping:
    object_store_bool(result, colorUserParams->HalftoneColorantMapping);
    break;

  case NAME_UseFastRGBToCMYK:
    object_store_bool(result, colorUserParams->UseFastRGBToCMYK);
    break;

  case NAME_RGBToCMYKMethod:
    object_store_integer(result, colorUserParams->RGBToCMYKMethod);
    break;
  }

  return TRUE;
}

/** Transfer the value of gstate type color user params to a working copy in one
 * gstate. All gstates at the current save level will ultimately be called.
 * See gsc_set_userparam().
 */
static Bool setParamForSaveLevel(GSTATE * gs, void *arg)
{
  paramValue_t *param = arg;

  if (gs->slevel == param->saveLevel) {
    switch (param->paramName) {
    case NAME_OverprintProcess:
      if (!gsc_setoverprintmode(gs->colorInfo, oBool(*param->object)))
        return FALSE;
      break;
    case NAME_OverprintBlack:
      if (!gsc_setoverprintblack(gs->colorInfo, oBool(*param->object)))
        return FALSE;
      break;
    case NAME_OverprintGray:
      if (!gsc_setoverprintgray(gs->colorInfo, oBool(*param->object)))
        return FALSE;
      break;
    case NAME_OverprintGrayImages:
      if (!gsc_setoverprintgrayimages(gs->colorInfo, oBool(*param->object)))
        return FALSE;
      break;
    case NAME_OverprintWhite:
      if (!gsc_setoverprintwhite(gs->colorInfo, oBool(*param->object)))
        return FALSE;
      break;
    case NAME_IgnoreOverprintMode:
      if (!gsc_setignoreoverprintmode(gs->colorInfo, oBool(*param->object)))
        return FALSE;
      break;
    case NAME_TransformedSpotOverprint:
      if (!gsc_settransformedspotoverprint(gs->colorInfo, oBool(*param->object)))
        return FALSE;
      break;
    case NAME_OverprintICCBased:
      if (!gsc_setoverprinticcbased(gs->colorInfo, oBool(*param->object)))
        return FALSE;
      break;
    case NAME_EnableColorCache:
      gstateptr->colorInfo->params.enableColorCache = oInteger(*param->object);
      break;
    case NAME_ExcludedSeparations:
      Copy(&gstateptr->colorInfo->params.excludedSeparations, param->object);
      break;
    case NAME_PhotoshopInput:
      gstateptr->colorInfo->params.photoshopInput = oBool(*param->object);
      break;
    case NAME_AdobeProcessSeparations:
      gstateptr->colorInfo->params.adobeProcessSeparations = oBool(*param->object);
      break;
    case NAME_UseFastRGBToCMYK:
      gstateptr->colorInfo->params.useFastRGBToCMYK = oBool(*param->object);
      break;
    case NAME_RGBToCMYKMethod:
      gstateptr->colorInfo->params.rgbToCMYKMethod = oInteger(*param->object);
      break;
    default:
      HQFAIL("Unexpected overprint param");
      break;
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------------*/

static Bool checkKeyIsName(OBJECT * key, OBJECT * value, void *arg)
{
  UNUSED_PARAM(OBJECT *, value);
  UNUSED_PARAM(void *, arg);

  if (oType(*key) != ONAME)
    return error_handler(TYPECHECK);

  return TRUE;
}

/** GC scanner for color user params. */
mps_res_t MPS_CALL gsc_scanColorUserParams(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res;
  COLOR_USER_PARAMS *colorUserParams = p;

  UNUSED_PARAM( size_t, s );
  res = ps_scan_field( ss, &colorUserParams->TransferFunction );
  if (res != MPS_RES_OK) return res;
  res = ps_scan_field( ss, &colorUserParams->ExcludedSeparations );
  if (res != MPS_RES_OK) return res;

  return MPS_RES_OK;
}

/* ---------------------------------------------------------------------- */

/*
 * Setup the color page params from the color user params
 */
static Bool pagedevice_transfer(COLOR_USER_PARAMS *colorUserParams,
                                COLOR_PAGE_PARAMS *colorPageParams)
{
  OBJECT *xfer = &colorUserParams->TransferFunction; /* PS user transfer */

  /* If we had a previous cached func that we are over-writing, then free it */
  if ( colorPageParams->transfer.cached &&
       colorPageParams->transfer.func.cpsc ) {
    destroy_callpscache(&colorPageParams->transfer.func.cpsc);
  }

  colorPageParams->transfer.id = colorUserParams->TransferFunctionId;
  /*
   * 3 cases to deal with
   *   1) Special case an empty PS func as this is the typical case
   *      and is a no-op
   *   2) If the PS func is an executable procedure, then need to
   *      pre-execute and cache the results to ensure the interpreter
   *      will not need to be called during rendering.
   *   3) If it is a literal array used as a lookup, then it can just
   *      be passed to the back-end as reading PS objects (as opposed to
   *      executing them) is OK at render time.
   */
  if ( theLen(*xfer) == 0 ) {
    colorPageParams->transfer.cached = TRUE;
    colorPageParams->transfer.func.cpsc = NULL;
  } else if ( oExecutable(*xfer) ) {
    colorPageParams->transfer.cached = TRUE;
    colorPageParams->transfer.func.cpsc = create_callpscache(FN_TRANSFER, 1, 0,
                                                             NULL, xfer);
    if ( colorPageParams->transfer.func.cpsc == NULL )
      return FALSE;
  } else {
    colorPageParams->transfer.cached = FALSE;
    colorPageParams->transfer.func.psfunc = *xfer;
  }
  return TRUE;
}

/** During setpagedevice, transfer necessary color params to working copies that
 * are safe to use in both the front and back ends of the rip.
 * All of these params are either pagedevice keys or color user params that are
 * treated as pagedevice keys.
 * The working copies are mostly in the DL_STATE, with some in the gstate.
 * Also see gsc_set_userparam().
 */
Bool gsc_pagedevice(corecontext_t *context,
                    DL_STATE      *page,
                    GS_COLORinfo  *colorInfo,
                    OBJECT        *pagedeviceDict)
{
  OBJECT *theo;
  COLOR_PAGE_PARAMS *colorPageParams;
  COLOR_USER_PARAMS *colorUserParams;

  enum {
    pdevmatch_HWResolution,
    pdevmatch_ConvertAllSeparation,
    pdevmatch_AlternateCMM,
    pdevmatch_Exposure,
    pdevmatch_NegativePrint,
    pdevmatch_ContoneMask,
    pdevmatch_Halftone,
    pdevmatch_dummy
  };

  static NAMETYPEMATCH pdevmatch[pdevmatch_dummy + 1] = {
    /* Use the enum above to index this match */
    { NAME_HWResolution, 2, { OARRAY, OPACKEDARRAY }},
    { NAME_ConvertAllSeparation | OOPTIONAL, 2, { ONAME, ONULL }},
    { NAME_AlternateCMM, 2, { OSTRING, ONULL }},
    { NAME_Exposure | OOPTIONAL, 1, { OINTEGER }},
    { NAME_NegativePrint | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_ContoneMask | OOPTIONAL, 1, { OINTEGER }},
    { NAME_Halftone, 1, { OBOOLEAN }},
    DUMMY_END_MATCH
  };

  HQASSERT(colorInfo != NULL, "gsc_setLateColorManagement: colorInfo is NULL");

  if (! dictmatch(pagedeviceDict, pdevmatch))
    return FALSE;

  colorPageParams = &page->colorPageParams;

  /* ConvertAllSeparation. This is a pagedevice key that is treated internally
   * as a gstate attribute, so the working value is stored in the gstate.
   */
  theo = pdevmatch[pdevmatch_ConvertAllSeparation].result;
  if ( theo != NULL ) {
    int convertAllSeparation = GSC_CONVERTALLSEPARATION_ALL;
    if (oType(*theo) == ONAME) {
      switch (oName(*theo) - system_names) {
      case NAME_Black:
        convertAllSeparation = GSC_CONVERTALLSEPARATION_BLACK;
        break;
      case NAME_DeviceCMYK:
        convertAllSeparation = GSC_CONVERTALLSEPARATION_CMYK;
        break;
      case NAME_All:
        /* the default */
        break;
      default:
        return error_handler(RANGECHECK);
      }
    }

    gsc_setConvertAllSeparation(colorInfo, convertAllSeparation);
  }

  /* AlternateCMM. A pagedevice key that is stored in the gstate for
   * historical reasons.
   */
  if (!gsc_setAlternateCMM(colorInfo, pdevmatch[pdevmatch_AlternateCMM].result))
    return FALSE;

  /* Some pagedevice keys for storing in the DL_STATE which will be the values
   * used elsewhere in the rip. Some of these keys are optional.
   */
  colorPageParams->exposure = pdevmatch[pdevmatch_Exposure].result == NULL ?
                0 : oInteger(*pdevmatch[pdevmatch_Exposure].result);
  colorPageParams->negativePrint = pdevmatch[pdevmatch_NegativePrint].result == NULL ?
                FALSE : oInteger(*pdevmatch[pdevmatch_NegativePrint].result);
  colorPageParams->contoneMask = pdevmatch[pdevmatch_ContoneMask].result == NULL ?
                0 : oInteger(*pdevmatch[pdevmatch_ContoneMask].result);
  colorPageParams->halftoning = oBool(*pdevmatch[pdevmatch_Halftone].result);

  /* Store working copies of these userparams in the DL_STATE because they are
   * treated as though they are pagedevice keys. And because most of them are
   * referenced in the back end where the userparams don't exist.
   */
  colorUserParams = context->color_userparams;
  colorPageParams->abortOnBadICCProfile = colorUserParams->AbortOnBadICCProfile;
  colorPageParams->duplicateColorants = colorUserParams->DuplicateColorants;
  colorPageParams->negativeJob = colorUserParams->NegativeJob;
  colorPageParams->ignoreSetTransfer = colorUserParams->IgnoreSetTransfer;
  colorPageParams->ignoreSetBlackGeneration = colorUserParams->IgnoreSetBlackGeneration;
  if ( !pagedevice_transfer(colorUserParams, colorPageParams) )
    return FALSE;
  colorPageParams->useAllSetTransfer = colorUserParams->UseAllSetTransfer;

  colorPageParams->fasterColorMethod = colorUserParams->FasterColorMethod;
  colorPageParams->fasterColorGridPoints = colorUserParams->FasterColorGridPoints;
  colorPageParams->fasterColorSmoothness = colorUserParams->FasterColorSmoothness;
  colorPageParams->halftoneColorantMapping = colorUserParams->HalftoneColorantMapping;

  return TRUE;
}

/** Setter for halftonephase */
void gsc_setHalftonePhase(GS_COLORinfo *colorInfo, int32 phaseX, int32 phaseY)
{
  colorInfo->gstate.halftonePhaseX = phaseX;
  colorInfo->gstate.halftonePhaseY = phaseY;
}

/** Getter for halftonephaseX */
int32 gsc_getHalftonePhaseX(GS_COLORinfo *colorInfo)
{
  return colorInfo->gstate.halftonePhaseX;
}

/** Getter for halftonephaseY */
int32 gsc_getHalftonePhaseY(GS_COLORinfo *colorInfo)
{
  return colorInfo->gstate.halftonePhaseY;
}

/** Setter for ScreenRotate */
void gsc_setScreenRotate(GS_COLORinfo *colorInfo, USERVALUE screenRotate)
{
  colorInfo->gstate.screenRotate = screenRotate;
}

/** Getter for ScreenRotate */
USERVALUE gsc_getScreenRotate(GS_COLORinfo *colorInfo)
{
  return colorInfo->gstate.screenRotate;
}

/* Log stripped */
