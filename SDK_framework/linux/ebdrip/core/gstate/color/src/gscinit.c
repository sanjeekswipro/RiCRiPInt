/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscinit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation for gstate color compound.
 */

#include "core.h"
#include "gscinitpriv.h"

#include "coreinit.h"
#include "graphics.h"
#include "gstate.h"
#include "gscheadpriv.h"        /* cc_initChains */
#include "gs_chaincachepriv.h"  /* cc_initChainCache */
#include "gscicc.h"             /* cc_iccbased_finish */
#include "gs_tablepriv.h" /* gst_swstart */


void cc_colorInfo_init(GS_COLORinfo * colorInfo)
{
  int32 colorType;

  colorInfo->deviceRS = NULL;
  colorInfo->targetRS = NULL;

  for (colorType = 0; colorType < GSC_N_COLOR_TYPES; colorType++) {
    colorInfo->chainInfo[colorType] = NULL;
    colorInfo->constructionDepth[colorType] = 0;
    colorInfo->chainCache[colorType] = NULL;
  }

  colorInfo->crdInfo = NULL ;
  colorInfo->rgbtocmykInfo = NULL ;
  colorInfo->transferInfo = NULL ;
  colorInfo->calibrationInfo = NULL ;
  colorInfo->halftoneInfo = NULL ;
  colorInfo->hcmsInfo = NULL ;
  colorInfo->devicecodeInfo = NULL ;

  colorInfo->fInvalidateColorChains = FALSE;
  colorInfo->params.convertAllSeparation = GSC_CONVERTALLSEPARATION_ALL;
  colorInfo->params.enableColorCache = GSC_ENABLE_ALL_COLOR_CACHES;
  colorInfo->params.excludedSeparations = onull;   /* struct copy for slot properties */
  colorInfo->params.photoshopInput = FALSE;
  colorInfo->params.adobeProcessSeparations = FALSE;
  colorInfo->params.useFastRGBToCMYK = TRUE;
  colorInfo->params.rgbToCMYKMethod = 0;

  colorInfo->gstate.opaqueNonStroke = FALSE;
  colorInfo->gstate.opaqueStroke = FALSE;
  colorInfo->gstate.halftonePhaseX = 0;
  colorInfo->gstate.halftonePhaseY = 0;
  colorInfo->gstate.screenRotate = 0.0;

  colorInfo->colorState = NULL;

  colorInfo->next = NULL;
}


static Bool color_swinit(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if (!gsc_paramsInit())
    return FALSE;

  return TRUE;
}


static Bool color_swstart(struct SWSTART *params)
{
  GUCR_RASTERSTYLE *rasterStyle;

  if (!coc_swstart(params))
    return FALSE;
  if ( !cc_chainCacheSWStart() )
    return FALSE;
  cc_colorInfo_init(gstateptr->colorInfo);

  if ( !gsc_swstart(gstateptr->colorInfo) ||
       !gst_swstart() ||
       !cc_initChains(gstateptr->colorInfo) ||
       !cc_initChainCache(gstateptr->colorInfo) ||
       !cc_iccbased_init() )
    return FALSE;

  /* The gstate transparency state setup must happen after initialising
     the colour parameters. */
  tsDefault(gsTranState(gstateptr), gstateptr->colorInfo);

  if (! guc_init(&rasterStyle)) { /* creates root */
    HQFAIL("guc_init failed");
    return FALSE;
  }
  gsc_replaceRasterStyle(gstateptr->colorInfo, rasterStyle);

  return TRUE ;
}


static void color_swfinish(void)
{
  guc_finish();
  cc_chainCacheSWFinish();
  gst_finish();
  if (gstateptr != NULL) {
    gsc_finish(gstateptr->colorInfo);
  }
  coc_swfinish();
}


IMPORT_INIT_C_GLOBALS( gs_color )
IMPORT_INIT_C_GLOBALS( gs_chaincache )
IMPORT_INIT_C_GLOBALS( gs_table )
IMPORT_INIT_C_GLOBALS( gschcms )
IMPORT_INIT_C_GLOBALS( gschtone )
IMPORT_INIT_C_GLOBALS( gscicc )
IMPORT_INIT_C_GLOBALS( gscparams )


void color_C_globals(struct core_init_fns *fns)
{
  /*****************************************************************************

    Globals are only allowed for frontend color transforms. If an item needs to
    be used for both frontend and backend transforms then it should be put into
    COLOR_STATE.

  *****************************************************************************/

  init_C_globals_gs_color() ;
  init_C_globals_gs_chaincache() ;
  init_C_globals_gs_table() ;
  init_C_globals_gschcms() ;
  init_C_globals_gschtone() ;
  init_C_globals_gscicc() ;
  init_C_globals_gscparams() ;

  fns->swinit = color_swinit ;
  fns->swstart = color_swstart ;
  fns->finish = color_swfinish ;
}

/* Log stripped */
