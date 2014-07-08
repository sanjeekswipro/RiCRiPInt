/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!export:tranState.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to transparency-specific graphics state.
 */

#ifndef __TRANSTATE_H__
#define __TRANSTATE_H__

#include "objects.h"
#include "graphict.h"

/* --Public types-- */

typedef uint32 TsStrokeSelector;

/* Possible values of TsStrokeSelector. */
enum {
  TsStroke = 1,
  TsNonStroke,
  TsStrokeAndNonStroke
};

/* --Public methods-- */

/* Initialise TranState to a default state. This method must be called before
any other ts method. */
void tsDefault(TranState* self, GS_COLORinfo *colorInfo);

/* Create a copy of 'self' in 'copy'. This function may allocate memory, and
thus may fail. Be sure to discard (tsDiscard()) any objects produced by this
method to ensure memory is not leaked. */
void tsCopy(TranState* self, TranState* copy);
void tsDiscard(TranState* self);

/* Simple access and query methods */
void tsSetAlphaIsShape(TranState* self, Bool value);
Bool tsAlphaIsShape(TranState* self);

void tsSetTextKnockout(TranState* self, Bool value);
Bool tsTextKnockout(TranState* self);

void tsSetConstantAlpha(TranState* self,
                        Bool setStrokingAlpha,
                        USERVALUE value,
                        GS_COLORinfo *colorInfo);
USERVALUE tsConstantAlpha(TranState* self, Bool strokingAlpha);

/* Soft mask accessor. See graphict.h for a list of valid values for the
SoftMaskType.

This method keeps an internal copy of the passed backdrop color. The transfer
function is ignored currently, but ultimately it too will be copied. The passed
'maskGroup' is not copied, and will simply be adoped as is (the Group provides
no ownship transference methods). */
Bool tsSetSoftMask(TranState* self,
                   SoftMaskType type,
                   uint32 groupId,
                   GS_COLORinfo *colorInfo);

/* Blend mode accessor. Note that 'mode' can be a name or an array of names. In
the latter case the first supported mode is used.

If all the specified modes are unsupported, the mode is silently defaulted to
the 'Normal' blend mode. Use the query method tsBlendMode() to determin the
blend mode actually installed. */
void tsSetBlendMode(TranState* self, OBJECT mode, GS_COLORinfo *colorInfo);

/* Blend mode query - return the name of the current blend mode. */
OBJECT tsBlendMode(TranState* self);

/* Will an object drawn with the current settings be opaque? Strokes use
a different constant alpha from all other objects, so the 'type' parameter
allows you to determin if strokes/nonstrokes or both object types are opaque.

Factors not related to the transparency state (such as the current group
                                               hierarchy) are not consulted, and so only the opaqueness of an object in it's
immediately containing group is returned. The object could still be transparent
on the final output page.
*/
Bool tsOpaque(TranState* self, TsStrokeSelector type, GS_COLORinfo *colorInfo);

#endif

/* --Description--

The TranState structure is used to hold graphics state required by transparency.

The structure is not intended to be allocated (thus there is not constructor
method); rather it should be held directly as a structure (the type is not
opaque - it's defined in graphics.h). However, the structure is named in the
usual way (via the macros in objnamer.h), and so before any ts methods can be
called on a structure, it must first be initialised via tsDefault().

The TranState structure can contain allocated sub-objects, and should thus be
disposed of via tsDiscard().
*/

/* Log stripped */
