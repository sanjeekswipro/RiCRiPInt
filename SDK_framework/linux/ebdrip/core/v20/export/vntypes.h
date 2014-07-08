/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:vntypes.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Vignette detection forward type definitions
 */

#ifndef __VNTYPES_H__
#define __VNTYPES_H__

/* Level of Vignette Detection. */
enum { VDL_None = 0, VDL_Simple = 1, VDL_Complex = 1 /* 3 later on. */ } ;

enum {
  VK_Unknown = 0 ,
  VK_Rectangle , VK_Circle ,
  VK_Linear , VK_Logarithmic
};

typedef struct VIGNETTEARGS VIGNETTEARGS ;

#endif /* protection for multiple inclusion */


/* Log stripped */
