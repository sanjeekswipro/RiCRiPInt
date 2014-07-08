/** \file
 * \ingroup cce
 *
 * $HopeName: COREcce!src:cce.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * The Color Compositing Engine (CCE)
 */

#include "core.h"
#include "mmcompat.h"

#include "compositers.h"

/* --Private prototypes-- */

static Bool linkBlendMode(CCE* self, CCEBlendMode mode);


/* --Public functions-- */

/* returns TRUE if the passed blend mode is supported.
*/
Bool cceBlendModeSupported(CCEBlendMode mode)
{
  return linkBlendMode(NULL, mode);
}

/* Returns TRUE if the passed blend mode is separable.
*/
Bool cceBlendModeIsSeparable(CCEBlendMode mode)
{
  switch (mode) {
    /* Separable blend modes. */
  case CCEModeNormal:
  case CCEModeMultiply:
  case CCEModeScreen:
  case CCEModeOverlay:
  case CCEModeSoftLight:
  case CCEModeHardLight:
  case CCEModeColorDodge:
  case CCEModeColorBurn:
  case CCEModeDarken:
  case CCEModeLighten:
  case CCEModeDifference:
  case CCEModeExclusion:
    return TRUE;

    /* Non-separable blend modes. */
  case CCEModeHue:
  case CCEModeSaturation:
  case CCEModeColor:
  case CCEModeLuminosity:
    return FALSE;

  default:
    HQFAIL("cceBlendModeIsSeparable - unsupported blend mode.");
    break;
  }

  return FALSE;
}

static void cceCompositeError(uint32 count,
                              const COLORVALUE *src, COLORVALUE srcAlpha,
                              const COLORVALUE *bdPremult, COLORVALUE bdAlpha,
                              COLORVALUE* result)
{
  UNUSED_PARAM(uint32, count) ;
  UNUSED_PARAM(const COLORVALUE *, src) ;
  UNUSED_PARAM(COLORVALUE, srcAlpha) ;
  UNUSED_PARAM(const COLORVALUE *, bdPremult) ;
  UNUSED_PARAM(COLORVALUE, bdAlpha) ;
  UNUSED_PARAM(COLORVALUE *, result) ;

  HQFAIL("CCE object used before methods set up") ;
}

/* --Public methods-- */

/* Constructor.
*/
CCE* cceNew(mm_pool_t memoryPool)
{
  CCE* self;

  /* Allocate self and state object memory in one go. */
  self = (CCE*) mm_alloc_with_header(memoryPool, sizeof(CCE),
                                     MM_ALLOC_CLASS_CCE);
  if (self == NULL)
    return NULL;

  /* Null function interfaces. */
  self->composite = cceCompositeError;
  self->compositePreMult = cceCompositeError;
  self->compositeSpot = cceCompositeError;
  self->compositeSpotPreMult = cceCompositeError;

  self->pool = memoryPool ;

  return self;
}

/* Destructor.
*/
void cceDelete(CCE **cce)
{
  HQASSERT(cce, "Nowhere to find CCE") ;

  if (*cce == NULL)
    return;

  mm_free_with_header((*cce)->pool, *cce);
  *cce = NULL ;
}

/* Set current blend mode.
*/
CCEBlendMode cceSetBlendMode(CCE* self,
                             const CCEBlendMode* modes,
                             uint32 modesCount)
{
  uint32 i;

  /* Try to link the methods for the specified blend mode. */
  for (i = 0; i < modesCount; i ++) {
    if ( linkBlendMode(self, modes[i]) ) {
      /* Requested blend mode supported. */
      return modes[i];
    }
  }

  /* If we have tried each blend mode in the passed list and failed, default
     to Normal. */
  if ( !linkBlendMode(self, CCEModeNormal) )
    HQFAIL("cceSetBlendMode - unable to install default for unsupported mode");

  return CCEModeNormal;
}

/* --Private methods-- */

/* If the passed blend mode is valid, set the blend function interfaces
to point to the appropriate functions. If the blend mode is not supported,
no action is taken.
Returns 'TRUE' if blend mode was linked successfully.
*/
#define SET_PROCESS_COMPOSITER(self_, NAME_) \
  if ((self_) != NULL) { \
    (self_)->composite = cce##NAME_; \
    (self_)->compositePreMult = cce##NAME_##PreMult; \
  }

#define SET_SPOT_COMPOSITER(self_, NAME_) \
  if ((self_) != NULL) { \
    (self_)->compositeSpot = cce##NAME_; \
    (self_)->compositeSpotPreMult = cce##NAME_##PreMult; \
  }

static Bool linkBlendMode(CCE* self, CCEBlendMode mode)
{
  Bool supported = TRUE;

  switch (mode) {
    /* Separable blend modes. */
  case CCEModeNormal:
    SET_PROCESS_COMPOSITER(self, Normal);
    SET_SPOT_COMPOSITER(self, Normal);
    break;

  case CCEModeMultiply:
    SET_PROCESS_COMPOSITER(self, Multiply);
    SET_SPOT_COMPOSITER(self, Multiply);
    break;

  case CCEModeScreen:
    SET_PROCESS_COMPOSITER(self, Screen);
    SET_SPOT_COMPOSITER(self, Screen);
    break;

  case CCEModeOverlay:
    SET_PROCESS_COMPOSITER(self, Overlay);
    SET_SPOT_COMPOSITER(self, Overlay);
    break;

  case CCEModeSoftLight:
    SET_PROCESS_COMPOSITER(self, SoftLight);
    SET_SPOT_COMPOSITER(self, SoftLight);
    break;

  case CCEModeHardLight:
    SET_PROCESS_COMPOSITER(self, HardLight);
    SET_SPOT_COMPOSITER(self, HardLight);
    break;

  case CCEModeColorDodge:
    SET_PROCESS_COMPOSITER(self, ColorDodge);
    SET_SPOT_COMPOSITER(self, ColorDodge);
    break;

  case CCEModeColorBurn:
    SET_PROCESS_COMPOSITER(self, ColorBurn);
    SET_SPOT_COMPOSITER(self, ColorBurn);
    break;

  case CCEModeDarken:
    SET_PROCESS_COMPOSITER(self, Darken);
    SET_SPOT_COMPOSITER(self, Darken);
    break;

  case CCEModeLighten:
    SET_PROCESS_COMPOSITER(self, Lighten);
    SET_SPOT_COMPOSITER(self, Lighten);
    break;

  case CCEModeDifference:
    SET_PROCESS_COMPOSITER(self, Difference);
    SET_SPOT_COMPOSITER(self, Normal);
    break;

  case CCEModeExclusion:
    SET_PROCESS_COMPOSITER(self, Exclusion);
    SET_SPOT_COMPOSITER(self, Normal);
    break;


    /* Non-separable blend modes. */
  case CCEModeHue:
    SET_PROCESS_COMPOSITER(self, Hue);
    SET_SPOT_COMPOSITER(self, Normal);
    break;

  case CCEModeSaturation:
    SET_PROCESS_COMPOSITER(self, Saturation);
    SET_SPOT_COMPOSITER(self, Normal);
    break;

  case CCEModeColor:
    SET_PROCESS_COMPOSITER(self, Color);
    SET_SPOT_COMPOSITER(self, Normal);
    break;

  case CCEModeLuminosity:
    SET_PROCESS_COMPOSITER(self, Luminosity);
    SET_SPOT_COMPOSITER(self, Normal);
    break;

  default:
    supported = FALSE;
    break;
  }

  return supported;
}

/* Log stripped */
