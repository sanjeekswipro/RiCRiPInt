/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscluminosity.c(EBDSDK_P.1) $
 * $Id: color:src:gscluminosity.c,v 1.22.2.1.1.1 2013/12/19 11:24:53 anon Exp $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Luminsoity functions
 */

#include "core.h"

#include "caching.h"            /* PENTIUM_CACHE_LOAD */
#include "swerrors.h"           /* error_handler */
#include "dlstate.h"            /* dlc_context */

#include "gs_colorpriv.h"       /* cc_common_create */

#include "gscluminosity.h"      /* extern's */


struct CLINKLUMINOSITYinfo {
  /* The page containing the dl_colors produced by this chain */
  DL_STATE  *page ;
} ;

#define CLID_SIZEnoslotsneeded (0)

/* ---------------------------------------------------------------------- */
static Bool luminosity_invokeSingle(CLINK* pLink, USERVALUE* dummy);
static Bool luminosity_invokeBlock(CLINK* pLink, CLINKblock* block);
static uint32 luminosityStructSize(void);
static void luminosityUpdatePtrs(CLINK *pLink);
#if defined( ASSERT_BUILD )
static void luminosityAssertions(CLINK *pLink);
#else
#define luminosityAssertions(pLink)   EMPTY_STATEMENT()
#endif
static inline USERVALUE rgbLuminosity(USERVALUE *iColorValues);
static inline USERVALUE cmykLuminosity(USERVALUE *iColorValues);

static CLINKfunctions CLINKluminosity_functions = {
  cc_common_destroy,
  luminosity_invokeSingle,
  luminosity_invokeBlock,
  NULL
};

/* Not a proper colorant index, just something to stick in a dl color. */
#define LUMINOSITY_NCOMPS 1
COLORANTINDEX luminosity_colorants[LUMINOSITY_NCOMPS] = {0};

int32 cc_luminosity_ncomps(void)
{
  return LUMINOSITY_NCOMPS;
}

COLORANTINDEX* cc_luminosity_colorants(void)
{
  return luminosity_colorants;
}

/* ---------------------------------------------------------------------- */
CLINK* cc_luminosity_create(GS_CONSTRUCT_CONTEXT  *context)
{
  CLINK* pLink;
  int32 ncomps;
  COLORSPACE_ID colorspace;

  HQASSERT(context != NULL, "context NULL");

  colorspace = context->chainColorSpace;

  switch (colorspace) {
  case SPACE_DeviceGray:
    ncomps = 1;
    break;
  case SPACE_DeviceRGB:
    ncomps = 3;
    break;
  case SPACE_DeviceCMYK:
    ncomps = 4;
    break;
  case SPACE_CIEXYZ:
    ncomps = NUMBER_XYZ_COMPONENTS;
    break;
  default:
    HQFAIL("Unsuitable color space for extracting luminosity");
    (void)error_handler(UNDEFINED);
    return NULL;
  }

  pLink = cc_common_create(ncomps, NULL,
                           colorspace,
                           SPACE_DeviceGray /* N/A, chosen arbitrarily */,
                           CL_TYPEluminosity,
                           luminosityStructSize(),
                           &CLINKluminosity_functions,
                           CLID_SIZEnoslotsneeded);
  if (pLink == NULL)
    return NULL;

  luminosityUpdatePtrs(pLink);

  /* This page context will be used by dl_colors from invoke functions */
  pLink->p.luminosity->page = context->page;

  luminosityAssertions(pLink);

  return pLink;
}

/* ---------------------------------------------------------------------- */
static Bool luminosity_invokeSingle(CLINK* pLink, USERVALUE* dummy)
{
  USERVALUE inColorValue;
  COLORVALUE outColorValue;
  USERVALUE *iColorValues = pLink->iColorValues;
  dlc_context_t *dlc_context;
  dl_color_t *dlc_current;


  UNUSED_PARAM(USERVALUE*, dummy);
  luminosityAssertions(pLink);

  dlc_context = pLink->p.luminosity->page->dlc_context;
  dlc_current = dlc_currentcolor(dlc_context);

  /* The method of deriving the soft mask value from the input color space is
   * as stated in the spec.
   */
  switch (pLink->iColorSpace) {
  case SPACE_DeviceGray:
    inColorValue = pLink->iColorValues[0];
    break;
  case SPACE_DeviceRGB:
    inColorValue = rgbLuminosity(iColorValues);
    break;
  case SPACE_DeviceCMYK:
    inColorValue = cmykLuminosity(iColorValues);
    break;
  case SPACE_CIEXYZ:
    /* Luminosity is the Y component for XYZ space */
    inColorValue = pLink->iColorValues[CC_CIEXYZ_Y];
    NARROW_01(inColorValue);
    break;
  default:
    HQFAIL("Unsuitable color space for extracting luminosity");
    return FALSE;
  }

  outColorValue = FLOAT_TO_COLORVALUE(inColorValue);

  dlc_release(dlc_context, dlc_current);

  return dlc_alloc_fillin(dlc_context,
                          cc_luminosity_ncomps(),
                          cc_luminosity_colorants(),
                          &outColorValue,
                          dlc_current);
}

/* ---------------------------------------------------------------------- */
static Bool luminosity_invokeBlock(CLINK* pLink, CLINKblock* block)
{
  int32 i;
  int32 nColors;
  USERVALUE* inColorValues;
  COLORVALUE* outColorValues;

  luminosityAssertions(pLink);
  HQASSERT(block != NULL, "block is null");

  nColors = block->nColors;
  inColorValues = block->iColorValues;
  outColorValues = block->deviceCodes;

  switch (pLink->iColorSpace) {
  case SPACE_DeviceGray:
    for (i = 0; i < nColors; i++)
      outColorValues[i] = FLOAT_TO_COLORVALUE(inColorValues[i]);
    break;
  case SPACE_DeviceRGB:
    for (i = 0; i < nColors; i++) {
      outColorValues[i] = FLOAT_TO_COLORVALUE(rgbLuminosity(inColorValues));
      inColorValues += 3;
    }
    break;
  case SPACE_DeviceCMYK:
    for (i = 0; i < nColors; i++) {
      outColorValues[i] = FLOAT_TO_COLORVALUE(cmykLuminosity(inColorValues));
      inColorValues += 4;
    }
    break;
  case SPACE_CIEXYZ:
    /* Luminosity is the Y component for XYZ space */
    for (i = 0; i < nColors; i++) {
      NARROW_01(inColorValues[CC_CIEXYZ_Y]);
      outColorValues[i] = FLOAT_TO_COLORVALUE(inColorValues[CC_CIEXYZ_Y]);
      inColorValues += NUMBER_XYZ_COMPONENTS;
    }
    break;
  default:
    HQFAIL("Unsuitable color space for extracting luminosity");
    return FALSE;
  }

  return TRUE;
}

static uint32 luminosityStructSize(void)
{
  return sizeof(CLINKLUMINOSITYinfo);
}

static void luminosityUpdatePtrs(CLINK *pLink)
{
  pLink->p.luminosity = (CLINKLUMINOSITYinfo *)
    ((uint8 *) pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
static void luminosityAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEluminosity,
                      luminosityStructSize(),
                      &CLINKluminosity_functions);
}
#endif

static inline USERVALUE rgbLuminosity(USERVALUE *iColorValues)
{
  return 0.3f  * iColorValues[0] +
         0.59f * iColorValues[1] +
         0.11f * iColorValues[2];
}

static inline USERVALUE cmykLuminosity(USERVALUE *iColorValues)
{
  return 0.3f  * (1.0f - iColorValues[0]) * (1.0f - iColorValues[3]) +
         0.59f * (1.0f - iColorValues[1]) * (1.0f - iColorValues[3]) +
         0.11f * (1.0f - iColorValues[2]) * (1.0f - iColorValues[3]);
}

/* Log stripped */
