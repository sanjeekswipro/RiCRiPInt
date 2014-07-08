/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!src:tranState.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Transparency-specific graphics state.
 */

#include "core.h"
#include "tranState.h"
#include "graphics.h"
#include "gs_color.h"     /* gsc_markChainsInvalid */
#include "objects.h"
#include "cce.h"
#include "mm.h"
#include "namedef_.h"
#include "swerrors.h"
#include "displayt.h" /* HDL_ID_INVALID */
#include "groupPrivate.h"

/* --Private macros-- */

#define TRANSSTATE_NAME "Transparency state"


/* --Private prototypes-- */

static void updateOpaque(TranState* self, GS_COLORinfo *colorInfo);
static CCEBlendMode nameToCCEBlendMode(OBJECT name);
static OBJECT CCEBlendModeToName(CCEBlendMode mode);


/* --Public methods-- */

/* Install the default transparency state.
*/
void tsDefault(TranState* self, GS_COLORinfo *colorInfo)
{
  OBJECT normal = OBJECT_NOTVM_NAME(NAME_Normal, LITERAL) ;

  HQASSERT(self != NULL, "tsDefault - self cannot be NULL");

  /* Name ourselves - all other methods expect a valid name */
  NAME_OBJECT(self, TRANSSTATE_NAME);

  tsSetAlphaIsShape(self, FALSE);
  tsSetTextKnockout(self, TRUE);
  tsSetConstantAlpha(self, TRUE, 1, colorInfo);
  tsSetConstantAlpha(self, FALSE, 1, colorInfo);
  tsSetBlendMode(self, normal, colorInfo);

  /* Initialize the softmask. This must be done specifically rather than
  just calling tsSetSoftMask(), as that function assumes that there is a valid
  mask already installed. */
  self->softMask.type = EmptySoftMask;
  self->softMask.groupId = HDL_ID_INVALID;

  updateOpaque(self, colorInfo);
}

/* Create a copy of self.
*/
void tsCopy(TranState* self, TranState* copy)
{
  HQASSERT((self != NULL) && (copy != NULL),
           "tsCopy - parameters cannot not be NULL");

  /* Simple structure copy. */
  *copy = *self;
}

/* Destroy any allocated objects, and unname the object. This is basically a
destructor, but recall that TranState objects are not allocated dynamically,
so 'self' is not freed.
*/
void tsDiscard(TranState* self)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);
  UNNAME_OBJECT(self);
  UNUSED_PARAM(TranState*, self);
}

/* AlphaIsShape accessor.
*/
void tsSetAlphaIsShape(TranState* self, Bool value)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);
  HQASSERT(BOOL_IS_VALID(value), "tsSetAlphaIsShape - 'value' is invalid");

  self->alphaIsShape = (uint8)value;
}

/* AlphaIsShape query.
*/
Bool tsAlphaIsShape(TranState* self)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);

  return self->alphaIsShape;
}

/* Text knockout accessor.
*/
void tsSetTextKnockout(TranState* self, Bool value)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);
  HQASSERT(BOOL_IS_VALID(value), "tsSetTextKnockout - 'value' is invalid");

  self->textKnockout = (uint8)value;
}

/* Text knockout query.
*/
Bool tsTextKnockout(TranState* self)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);

  return self->textKnockout;
}

/* Set the constant alpha. There are two values for this, one for stokes, and
one for all other objects; the value being set is controlled by the
setStrokingAlpha paramter (pass TRUE to set stroking constant alpha). Values
outside of the 0 - 1 range will be clipped.
*/
void tsSetConstantAlpha(TranState* self,
                        Bool setStrokingAlpha,
                        USERVALUE value,
                        GS_COLORinfo *colorInfo)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);
  HQASSERT(BOOL_IS_VALID(setStrokingAlpha),
           "tsSetConstantAlpha - 'setStrokingAlpha' is invalid");

  /* Limit the value */
  if (value < 0.0f) {
    value = 0.0f;
  }
  if (value > 1.0f) {
    value = 1.0f;
  }

  /* Set appropriate value */
  if (setStrokingAlpha) {
    self->strokingAlpha = value;
  }
  else {
    self->nonstrokingAlpha = value;
  }

  updateOpaque(self, colorInfo);
}

/* Constant alpha query - 'strokingAlpha' controls the value accessed
(pass TRUE to obtain the stroking constant alpha).
*/
USERVALUE tsConstantAlpha(TranState* self, Bool strokingAlpha)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);
  HQASSERT(BOOL_IS_VALID(strokingAlpha),
           "tsConstantAlpha - 'strokingAlpha is invalid");

  /* Return requested value */
  if (strokingAlpha) {
    return self->strokingAlpha;
  }
  else {
    return self->nonstrokingAlpha;
  }
}

/* SoftMask accessor. This function will not hold onto the passed OBJECT's
for any longer than the duration of this function.
*/
Bool tsSetSoftMask(TranState* self,
                   SoftMaskType type,
                   uint32 groupId,
                   GS_COLORinfo *colorInfo)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);
  HQASSERT(validSoftMaskType(type), "tsSetSoftMask - 'type' is invalid");

  HQASSERT(type == EmptySoftMask ?
           groupId == HDL_ID_INVALID :
           groupId != HDL_ID_INVALID, "Invalid softmask ID") ;

  /* Setup new mask details. */
  self->softMask.type = type;
  self->softMask.groupId = groupId;

  updateOpaque(self, colorInfo);

  return TRUE;
}

/* BlendMode accessor.
*/
void tsSetBlendMode(TranState* self, OBJECT mode, GS_COLORinfo *colorInfo)
{
  CCEBlendMode cceMode = CCEModeNormal; /* Init to default mode. */
  CCEBlendMode testMode;

  VERIFY_OBJECT(self, TRANSSTATE_NAME);
  HQASSERT((oType(mode) == ONAME) || (oType(mode) == OARRAY) ||
           (oType(mode) == OPACKEDARRAY),
           "tsSetBlendMode - invalid type for 'mode'");

  if (oType(mode) == ONAME) {
    /* If the mode is selected, we'll use it as the current mode. */
    testMode = nameToCCEBlendMode(mode);
    if (cceBlendModeSupported(testMode)) {
      cceMode = testMode;
    }
  }
  else {
    int32 i;

    /* We must have an array of names - check each in turn. */
    for (i = 0; i < theLen(mode); i ++) {
      testMode = nameToCCEBlendMode(oArray(mode)[i]);
      if (cceBlendModeSupported(testMode)) {
        cceMode = testMode;
        break;
      }
    }
  }

  /* Commit the mode. Recall that cceMode was initialized to the default
  mode, and this will be the value of cceMode if no valid modes were found
  in the code above. */
  self->blendMode = cceMode;

  updateOpaque(self, colorInfo);
}

/* Blend mode query - this may return a mode different to that specified in a
call to tsSetBlendMode(), if the requested mode was not supported.
*/
OBJECT tsBlendMode(TranState* self)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);

  return CCEBlendModeToName(self->blendMode);
}

/* Will an object drawn with the current settings be opaque? Strokes use
a different constant alpha from all other objects, so the 'type' parameter
allows you to determin if strokes/nonstrokes or both object types are opaque.

Factors not related to the transparency state (such as the current group
hierarchy) are not consulted, and so only the opaqueness of an object in it's
immediately containing group is returned. The object could still be transparent
on the final output page.
*/
Bool tsOpaque(TranState* self, TsStrokeSelector type, GS_COLORinfo *colorInfo)
{
  VERIFY_OBJECT(self, TRANSSTATE_NAME);

  UNUSED_PARAM(GS_COLORinfo *, colorInfo);

#if defined( ASSERT_BUILD )
  { /* check opaque flags are up-to-date */
    uint8 opaqueStroke = self->opaqueStroke;
    uint8 opaqueNonStroke = self->opaqueNonStroke;
    updateOpaque(self, colorInfo);
    HQASSERT(opaqueStroke == self->opaqueStroke &&
             opaqueNonStroke == self->opaqueNonStroke,
             "opaque flags in transparency state are out-of-date");
  }
#endif

  switch (type) {
  case TsStroke:
    return (Bool)self->opaqueStroke;
  case TsNonStroke:
    return (Bool)self->opaqueNonStroke;
  case TsStrokeAndNonStroke:
    return (Bool)(self->opaqueStroke && self->opaqueNonStroke);
  default:
    HQFAIL("Unrecognised value for TsStrokeSelector");
    return FALSE;
  }
  /* NOT REACHED */
}

/* --Private methods-- */

static void updateOpaque(TranState* self, GS_COLORinfo *colorInfo)
{
  uint8 opaqueStroke = TRUE;
  uint8 opaqueNonStroke = TRUE;

#if defined( ASSERT_BUILD )
  { /* check opaque flags are up-to-date */
    Bool tmpOpaqueStroke;
    Bool tmpOpaqueNonStroke;
    gsc_getOpaque(colorInfo, &tmpOpaqueNonStroke, &tmpOpaqueStroke);

    /* We will fire this assert if the opaque flags in the mirror within colorInfo
     * are now different to the canonical data.  A possible reason why is that
     * tsCopy is being used differently. At the time of writing, both the
     * colorInfo and the tranState within the gstate are updated together. And
     * that is required.
     */
    HQASSERT(tmpOpaqueStroke == self->opaqueStroke &&
             tmpOpaqueNonStroke == self->opaqueNonStroke,
             "opaque flags in colorInfo are out-of-date");
  }
#endif

  if (self->blendMode != CCEModeNormal ||
      self->softMask.type != EmptySoftMask) {
    opaqueStroke = FALSE;
    opaqueNonStroke = FALSE;
  } else {
    if (self->strokingAlpha < 1)
      opaqueStroke = FALSE;
    if (self->nonstrokingAlpha < 1)
      opaqueNonStroke = FALSE;
  }

  self->opaqueStroke = opaqueStroke;
  self->opaqueNonStroke = opaqueNonStroke;

  /* The opaqueness or transparency of an object determines whether
     the current halftone or default screen is used (according to
     PDF 1.4 rules).  Reset chains to ensure correct properties are
     used in subsequent color chain invoke. */
  gsc_setOpaque(colorInfo, self->opaqueNonStroke, self->opaqueStroke);
}

/* Convert a PS name into a CCEBlendMode
*/
static CCEBlendMode nameToCCEBlendMode(OBJECT name)
{
  CCEBlendMode mode;

  HQASSERT(oType(name) == ONAME,
           "nameToCCEBlendMode - 'name' must be of ONAME type");

  switch ( oNameNumber(name) ) {

  case NAME_Normal:
    mode = CCEModeNormal;
    break;
  case NAME_Multiply:
    mode = CCEModeMultiply;
    break;
  case NAME_Screen:
    mode = CCEModeScreen;
    break;
  case NAME_Overlay:
    mode = CCEModeOverlay;
    break;
  case NAME_SoftLight:
    mode = CCEModeSoftLight;
    break;
  case NAME_HardLight:
    mode = CCEModeHardLight;
    break;
  case NAME_ColorDodge:
    mode = CCEModeColorDodge;
    break;
  case NAME_ColorBurn:
    mode = CCEModeColorBurn;
    break;
  case NAME_Darken:
    mode = CCEModeDarken;
    break;
  case NAME_Lighten:
    mode = CCEModeLighten;
    break;
  case NAME_Difference:
    mode = CCEModeDifference;
    break;
  case NAME_Exclusion:
    mode = CCEModeExclusion;
    break;
  case NAME_Hue:
    mode = CCEModeHue;
    break;
  case NAME_Saturation:
    mode = CCEModeSaturation;
    break;
  case NAME_Color:
    mode = CCEModeColor;
    break;
  case NAME_Luminosity:
    mode = CCEModeLuminosity;
    break;

  default:
    /* Unrecognized names are defaulted to Normal. */
    mode = CCEModeNormal;
    break;
  }

  return mode;
}

/* Convert a CCEBlendMode into a name.
*/
static OBJECT CCEBlendModeToName(CCEBlendMode mode)
{
  OBJECT name = OBJECT_NOTVM_NOTHING;

  switch (mode) {

  case CCEModeNormal:
    object_store_name(&name, NAME_Normal, LITERAL);
    break;
  case CCEModeMultiply:
    object_store_name(&name, NAME_Multiply, LITERAL);
    break;
  case CCEModeScreen:
    object_store_name(&name, NAME_Screen, LITERAL);
    break;
  case CCEModeOverlay:
    object_store_name(&name, NAME_Overlay, LITERAL);
    break;
  case CCEModeSoftLight:
    object_store_name(&name, NAME_SoftLight, LITERAL);
    break;
  case CCEModeHardLight:
    object_store_name(&name, NAME_HardLight, LITERAL);
    break;
  case CCEModeColorDodge:
    object_store_name(&name, NAME_ColorDodge, LITERAL);
    break;
  case CCEModeColorBurn:
    object_store_name(&name, NAME_ColorBurn, LITERAL);
    break;
  case CCEModeDarken:
    object_store_name(&name, NAME_Darken, LITERAL);
    break;
  case CCEModeLighten:
    object_store_name(&name, NAME_Lighten, LITERAL);
    break;
  case CCEModeDifference:
    object_store_name(&name, NAME_Difference, LITERAL);
    break;
  case CCEModeExclusion:
    object_store_name(&name, NAME_Exclusion, LITERAL);
    break;
  case CCEModeHue:
    object_store_name(&name, NAME_Hue, LITERAL);
    break;
  case CCEModeSaturation:
    object_store_name(&name, NAME_Saturation, LITERAL);
    break;
  case CCEModeColor:
    object_store_name(&name, NAME_Color, LITERAL);
    break;
  case CCEModeLuminosity:
    object_store_name(&name, NAME_Luminosity, LITERAL);
    break;

  case CCEModeUnspecified:
  default:
    /* Unrecognized modes mean trouble. */
    HQFAIL("CCEBlendModeToName - name is not a valid blend mode");
    object_store_null(&name);
    break;
  }

  return name;
}

/* Log stripped */
