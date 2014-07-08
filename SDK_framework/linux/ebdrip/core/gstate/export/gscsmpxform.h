/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gscsmpxform.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to create and cache colorspace objects and custom tinttransform data
 * for backend color transformations.
 */

#ifndef __GSCSMPXFORM_H__
#define __GSCSMPXFORM_H__

#include "graphict.h"     /* GS_COLORinfo */

#define NUM_EQUIV_COLORS    (4)

typedef USERVALUE EQUIVCOLOR[NUM_EQUIV_COLORS];

/* A raster style style is optional (use NULL) */
typedef NAMECACHE *(*CMYK_EQUIV_CALLBACK)(GUCR_RASTERSTYLE *inputRS,
                                          COLORANTINDEX ciPseudo,
                                          EQUIVCOLOR **equiv,
                                          void *private_callback_data);

Bool gsc_spacecache_setcolorspace(GS_COLORinfo *colorInfo,
                                  GUCR_RASTERSTYLE *inputRS,
                                  int32 colorType,
                                  int32 nColorants,
                                  COLORANTINDEX *colorantIndices,
                                  Bool fCompositing,
                                  CMYK_EQUIV_CALLBACK cmyk_equiv_callback,
                                  void *private_callback_data);

OBJECT *gsc_spacecache_getcolorspace(GS_COLORinfo *colorInfo,
                                     GUCR_RASTERSTYLE *inputRS,
                                     int32 nSpots, COLORANTINDEX *spotcolorants,
                                     CMYK_EQUIV_CALLBACK cmyk_equiv_callback,
                                     void *private_callback_data);

#endif /* __GSCSMPXFORM_H__ */

/* Log stripped */
