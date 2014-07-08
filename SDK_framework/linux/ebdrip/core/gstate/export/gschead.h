/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!export:gschead.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS level 3 color processing operators
 */

#ifndef __GS_HEAD_H__
#define __GS_HEAD_H__

#include "objecth.h"
#include "displayt.h"
#include "gs_color.h"
#include "gscdevci.h"         /* GS_BLOCKoverprint */
#include "stacks.h"           /* STACK */
#include "mps.h"              /* mps_res_t */

struct GSC_SIMPLE_TRANSFORM;

/* For 100% black preservation. Only 6 kinds that fit in 3 bits in BackdropInfo. */
enum {
  BLACK_TYPE_UNKNOWN,
  BLACK_TYPE_100_PC,
  BLACK_TYPE_TINT,
  BLACK_TYPE_ZERO,
  BLACK_TYPE_NONE,
  BLACK_TYPE_MODIFIED,
  BLACK_N_TYPES
};
typedef uint8 GSC_BLACK_TYPE;

/*******************************************************************************
 * These functions provide a common interface for the graphics state operators *
 * in PostScript and PDF.                                                      *
 *                                                                             *
 *******************************************************************************/

Bool gsc_getcolorspacetype(OBJECT        *theo,
                           COLORSPACE_ID *colorspaceID);

Bool gsc_getcolorspacesizeandtype(GS_COLORinfo  *colorInfo,
                                  OBJECT        *theo,
                                  COLORSPACE_ID *colorspaceID,
                                  int32         *dimension);

Bool gsc_sameColorSpaceObject(GS_COLORinfo *colorInfo,
                              OBJECT       *colorSpace_1,
                              OBJECT       *colorSpace_2);

Bool gsc_setcolorspace( GS_COLORinfo *colorInfo,
                        STACK *stack,
                        int32 colorType );

Bool gsc_setgray( GS_COLORinfo *colorInfo,
                  STACK *stack,
                  int32 colorType );

Bool gsc_sethsbcolor( GS_COLORinfo *colorInfo,
                      STACK *stack,
                      int32 colorType );

Bool gsc_setrgbcolor( GS_COLORinfo *colorInfo,
                      STACK *stack,
                      int32 colorType );

Bool gsc_setcmykcolor( GS_COLORinfo *colorInfo,
                       STACK *stack,
                       int32 colorType );

Bool gsc_setcolor( GS_COLORinfo *colorInfo,
                   STACK *stack,
                   int32 colorType );

Bool gsc_setpattern( GS_COLORinfo *colorInfo,
                     STACK *stack,
                     int32 colorType );

Bool gsc_getpattern( GS_COLORinfo *colorInfo ,
                     int32 colorType ,
                     OBJECT **pattern );

Bool gsc_currentcolorspace( GS_COLORinfo *colorInfo, int32 colorType,
                            OBJECT *colorSpace );
Bool gsc_getInternalColorSpace( COLORSPACE_ID colorSpaceId,
                                OBJECT **colorSpaceObj );

Bool gsc_currentcolor(GS_COLORinfo *colorInfo, STACK *stack, int32 colorType) ;

/* and these functions provide more direct access */

void gsc_initgray( GS_COLORinfo *colorInfo );

Bool gsc_setcolordirect( GS_COLORinfo *colorInfo,
                         int32 colorType,
                         USERVALUE *pvalues );

Bool gsc_setpatternmaskdirect( GS_COLORinfo *colorInfo,
                               int32 colorType,
                               OBJECT *pattern,
                               USERVALUE *iColorValues );

Bool gsc_setcolorspacedirect( GS_COLORinfo *colorInfo,
                              int32 colorType,
                              COLORSPACE_ID colorspace_id );

Bool gsc_setcolorspacedirectforcompositing( GS_COLORinfo  *colorInfo ,
                                            int32         colorType ,
                                            COLORSPACE_ID colorspace_id);

Bool gsc_setcustomcolorspacedirect( GS_COLORinfo  *colorInfo ,
                                    int32         colorType ,
                                    OBJECT        *colorSpace,
                                    Bool          fCompositing);

Bool gsc_fCompositing(GS_COLORinfo *colorInfo, int32 colorType);

COLORSPACE_ID gsc_getcolorspace( GS_COLORinfo *colorInfo, int32 colorType ) ;
OBJECT *gsc_getcolorspaceobject( GS_COLORinfo *colorInfo, int32 colorType ) ;

Bool gsc_getbasecolorspace( GS_COLORinfo *colorInfo, int32 colorType ,
                            COLORSPACE_ID *piColorSpace );
OBJECT *gsc_getbasecolorspaceobject( GS_COLORinfo *colorInfo , int32 colorType ) ;

/* gsc_dimensions returns the number of color components in the input space. */
int32 gsc_dimensions( GS_COLORinfo *colorInfo, int32 colorType );

void gsc_getcolorvalues( GS_COLORinfo *colorInfo , int32 colorType ,
                         USERVALUE **pColorValues , int32 *pnDimensions ) ;

Bool gsc_setoverprintprocess( GS_COLORinfo *colorInfo, int32 colorType,
                              uint8 processoverprint ) ;
uint8 gsc_getoverprintprocess( GS_COLORinfo *colorInfo , int32 colorType ) ;

Bool gsc_setBlackType(GS_COLORinfo *colorInfo, int32 colorType, GSC_BLACK_TYPE blackType);
GSC_BLACK_TYPE gsc_getBlackType(GS_COLORinfo *colorInfo, int32 colorType);

Bool gsc_setPrevIndependentChannels(GS_COLORinfo *colorInfo, int32 colorType,
                                    Bool prevIndependentChannels);
Bool gsc_getPrevIndependentChannels(GS_COLORinfo *colorInfo, int32 colorType);

Bool gsc_setLuminosityChain(GS_COLORinfo *colorInfo, int32 colorType, Bool on);
Bool gsc_getfSoftMaskLuminosityChain(GS_COLORinfo *colorInfo, int32 colorType);


/* Given an indexed color space (typically the GSC_SHFILL element in
   chainInfo in a graphics state, and specifically not
   GSC_SHFILL_INDEXED_BASE), gsc_baseColor returns a pointer to
   *pnDimensions input color values in *ppColor, that number also
   being set. This is the same number that would be returned by
   gsc_dimensions on the equivalent GSC_SHFILL_INDEXED_BASE chain.

   If the colorchain does not exist it will be created and the first
   link will be invoked. This means that this function can be used
   without invoking the complete chain. The function returns TRUE
   if the chain has been successfully invoked, FALSE otherwise.
*/
Bool gsc_baseColor(GS_COLORinfo *colorInfo,
                   int32 colorType,
                   USERVALUE ** ppColor,
                   int32 * pnDimensions);

/* gsc_range writes the minimum and maximum values that component "index"
   of the input color space indexed from colorType in colorInfo may accept
   into range[0] and range[1].
 */
Bool gsc_range(GS_COLORinfo *colorInfo, int32 colorType, int32 index,
               SYSTEMVALUE range[2]);
Bool gsc_baseRange(GS_COLORinfo *colorInfo, int32 colorType, int32 index,
                   SYSTEMVALUE range[2]);
Bool gsc_getcurrentcolorspacerange(GS_COLORinfo *colorInfo, STACK *stack);

Bool gsc_rgb_to_hsv( USERVALUE rgb[ 3 ] , USERVALUE hsv[ 3 ] );

#endif /* __GS_HEAD_H__ */

/* Log stripped */
