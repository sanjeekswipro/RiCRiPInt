/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_pgb!swpgb.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This header file provides details of PGB format.
 */


#ifndef __SWPGB_H__
#define __SWPGB_H__


/** \brief Type bits in object map rasters */
enum {
  SW_PGB_USER_OBJECT = (1u << 0u),
  SW_PGB_NAMEDCOLOR_OBJECT = (1u << 1u),
  SW_PGB_BLACK_OBJECT = (1u << 2u),
  SW_PGB_LW_OBJECT = (1u << 3u),
  SW_PGB_TEXT_OBJECT = (1u << 4u),
  SW_PGB_VIGNETTE_OBJECT = (1u << 5u),
  SW_PGB_IMAGE_OBJECT = (1u << 6u),
  SW_PGB_COMPOSITED_OBJECT = (1u << 7u)
};


/** \brief Special handling options for colorants
 *
 * Note that any consumer which doesn't understand these will just not
 * apply any special handling, which is correct.  Furthermore any
 * consumer which _is_ aware of the specialHandling field but sees a
 * value it doesn't recognise, should just not treat the colorant
 * specially.
 */
typedef int32 sw_pgb_special_handling;
enum {
  SW_PGB_SPECIALHANDLING_NONE = 0,
  SW_PGB_SPECIALHANDLING_OPAQUE,
  SW_PGB_SPECIALHANDLING_OPAQUEIGNORE,
  SW_PGB_SPECIALHANDLING_TRANSPARENT,
  SW_PGB_SPECIALHANDLING_TRAPZONES,
  SW_PGB_SPECIALHANDLING_TRAPHIGHLIGHTS,
  SW_PGB_SPECIALHANDLING_OBJECTMAP
};


#endif /* __SWPGB_H__ */

