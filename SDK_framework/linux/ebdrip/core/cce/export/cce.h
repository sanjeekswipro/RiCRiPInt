/** \file
 * \ingroup cce
 *
 * $HopeName: COREcce!export:cce.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Initialisation and setting of CCE objects
 */

/** \defgroup cce Color Compositing Engine
    \ingroup color */
/** \{ */

#ifndef __CCE_H__
#define __CCE_H__

#include "mm.h" /* mm_pool_t */

/* --Public datatypes-- */

/** Blend mode type */
typedef uint32 CCEBlendMode;

/** Possible values of blend mode */
enum {
  CCEModeUnspecified,
  CCEModeNormal,
  CCEModeMultiply,
  CCEModeScreen,
  CCEModeOverlay,
  CCEModeSoftLight,
  CCEModeHardLight,
  CCEModeColorDodge,
  CCEModeColorBurn,
  CCEModeDarken,
  CCEModeLighten,
  CCEModeDifference,
  CCEModeExclusion,
  CCEModeHue,
  CCEModeSaturation,
  CCEModeColor,
  CCEModeLuminosity
};

typedef void (*CCEInterface)(uint32 count,
                             const COLORVALUE *src,
                             COLORVALUE srcAlpha,
                             const COLORVALUE *bd,
                             COLORVALUE bdAlpha,
                             COLORVALUE* result);

/** Color Compositing Engine object */
typedef struct CCE {
  /* Process color compositers */
  CCEInterface composite;
  CCEInterface compositePreMult;

  /* Spot color compositers */
  CCEInterface compositeSpot;
  CCEInterface compositeSpotPreMult;

  /* Pool that this structure came from */
  mm_pool_t pool ;
} CCE;


/** \brief Determine if a blend mode is supported by the CCE.

    \param mode The blend mode to test.

    \retval TRUE if the blend mode is supported.
    \retval FALSE if the blend mode is not supported.
*/
Bool cceBlendModeSupported(CCEBlendMode mode);

/** \brief Determine if a blend mode is separable.

    \param mode The blend mode to test.

    \retval TRUE if the blend mode is separable.
    \retval FALSE if the blend mode is not non-separable.

    This function should only be used if the blend mode is known to be
    supported. If the blend mode is not supported, it will return FALSE.
*/
Bool cceBlendModeIsSeparable(CCEBlendMode mode);

/** \brief Create a new color compositing engine structure.

    \param memoryPool The memory pool from which allocations will be made.

    \returns A reference to the new CCE object, or NULL if there was an
    allocation failure.

    The CCE compositer methods should not be used until the blend mode has
    been set with \c cceSetBlendMode().
*/
CCE* cceNew(mm_pool_t memoryPool);

/** \brief Free a color compositing engine structure.

    \param cce A pointer where the CCE structure to free is found.

    This routine frees the CCE structure if it exists, and sets the pointer to
    it to NULL.
 */
void cceDelete(CCE **cce);

/** \brief Set the compositer methods for a color compositing engine
    structure for a blend mode.

    \param[out] self  The CCE structure to set.
    \param[in] modes  An array of blend modes, which will be tried in order.
                      The first supported blend mode will be used, or
                      \c CCEModeNormal if none are supported.
    \param modesCount The number of blend modes in the \a modes array.

    \returns The blend mode actually chosen.
*/
CCEBlendMode cceSetBlendMode(/*@notnull@*/ /*@out@*/ CCE* self,
                             /*@notnull@*/ /*@in@*/ const CCEBlendMode* modes,
                             uint32 modesCount);
/** \} */

#endif

/* Log stripped */
