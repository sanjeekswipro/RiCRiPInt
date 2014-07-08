/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!export:gs_color.h(EBDSDK_P.1) $

 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for front-end color representation.
 */

#ifndef __GS_COLOR_H__
#define __GS_COLOR_H__

#include "displayt.h"           /* DL_STATE */
#include "graphict.h"           /* GS_COLORinfo */
#include "mm.h"                 /* mm_size_t */
#include "mps.h"                /* mps_res_t */
#include "objectt.h"            /* OBJECT */

/** \defgroup color Color representation and functions.
    \ingroup gstate
    \{ */

/* forward typedefs */
typedef uint8 COLORSPACE_ID ;
typedef uintptr_t CLID;

enum {
  /* These are colorspaces that may be used in jobs.
   * Be careful if changing them because a few arrays in gschead.c depend on
   * the order.  Also update SWcore!testsrc:swaddin:swaddin.cpp.
   */
  SPACE_notset = 0,
  SPACE_CIETableA,
  SPACE_CIETableABC,
  SPACE_CIETableABCD,
  SPACE_CIEBasedA,
  SPACE_CIEBasedABC,
  SPACE_CIEBasedDEF,
  SPACE_CIEBasedDEFG,
  SPACE_DeviceGray,
  SPACE_DeviceRGB,
  SPACE_DeviceCMY,
  SPACE_DeviceCMYK,
  SPACE_Pattern,
  SPACE_Indexed,
  SPACE_Separation,
  SPACE_DeviceN,
  SPACE_Lab,
  SPACE_CalGray,
  SPACE_CalRGB,
  SPACE_ICCBased,
  SPACE_CMM, /* Custom Color Spaces implemented in alternate CMMs */

  /* MARKS THE BOUNDARY BETWEEN EXTERNAL & INTERNAL SPACES */
  SPACE_Preseparation,

  /* These are colorspaces used for internal conversions */
  SPACE_DeviceRGBK,
  SPACE_DeviceK,
  SPACE_FinalDeviceN,
  SPACE_CIEXYZ,
  SPACE_CIELab,
  SPACE_ICCXYZ,
  SPACE_ICCLab,
  SPACE_HqnPCS,
  SPACE_HqnProfile,
  SPACE_SoftMaskXYZ,
  SPACE_InterceptCMYK,
  SPACE_InterceptRGB,
  SPACE_InterceptGray,
  SPACE_PatternMask,
  SPACE_Recombination,
  SPACE_TrapDeviceN
};


#define ColorspaceIsCIEBased(cs) ( cs == SPACE_CIETableA    || \
                                   cs == SPACE_CIETableABC  || \
                                   cs == SPACE_CIETableABCD || \
                                   cs == SPACE_CIEBasedA    || \
                                   cs == SPACE_CIEBasedABC  || \
                                   cs == SPACE_CIEBasedDEF  || \
                                   cs == SPACE_CIEBasedDEFG || \
                                   cs == SPACE_Lab          || \
                                   cs == SPACE_CalGray      || \
                                   cs == SPACE_CalRGB       || \
                                   cs == SPACE_ICCBased )

#define ColorspaceIsSubtractive(cs) ( cs == SPACE_DeviceCMY       || \
                                      cs == SPACE_DeviceCMYK      || \
                                      cs == SPACE_Separation      || \
                                      cs == SPACE_DeviceN         || \
                                      cs == SPACE_DeviceK         || \
                                      cs == SPACE_TrapDeviceN)

#define DeviceColorspaceIsAdditive(cs) ( cs == SPACE_DeviceGray       || \
                                         cs == SPACE_DeviceRGB        || \
                                         cs == SPACE_DeviceRGBK )


/* Indexes into arrays within GS_COLORinfo.
 * NB. The total number of values, including GSC_UNDEFINED, is limited to 8
 *     because there are only 3 bits available to colorType in the DL object. We
 *     are currently at the limit.
 */
enum {
  GSC_ILLEGAL   = -2,
  GSC_UNDEFINED = -1,
  GSC_FILL      = 0,
  GSC_STROKE,
  GSC_IMAGE,
  GSC_SHFILL,
  GSC_SHFILL_INDEXED_BASE,
  GSC_VIGNETTE,
  GSC_BACKDROP,
  GSC_N_COLOR_TYPES
};

#define COLORTYPE_ASSERT(_colorType_, _where_) \
  HQASSERT( _colorType_ == GSC_FILL     ||  \
            _colorType_ == GSC_STROKE   ||  \
            _colorType_ == GSC_IMAGE    ||  \
            _colorType_ == GSC_SHFILL   ||  \
            _colorType_ == GSC_SHFILL_INDEXED_BASE || \
            _colorType_ == GSC_VIGNETTE ||  \
            _colorType_ == GSC_BACKDROP,    \
            "Invalid colorType in " _where_ ) ;


/* values for convertAllSeparation */
#define GSC_CONVERTALLSEPARATION_ALL   0
#define GSC_CONVERTALLSEPARATION_BLACK 1
#define GSC_CONVERTALLSEPARATION_CMYK  2

/* The maximum number of image pixels to be converted in one call to
 * gsc_invokeChainBlock */
#define GSC_BLOCK_MAXCOLORS 256

/* ---------------------------------------------------------------------------
  Some macros to narrow a color value(s) to a range.
 */
#define NARROW_01(_color) MACRO_START \
  if ( (_color) < 0.0f )  (_color) = 0.0f ;            \
  else if ( (_color) > 1.0f ) (_color) = 1.0f ;        \
MACRO_END

#define NARROW1(_color, _range) MACRO_START \
  register SYSTEMVALUE * r = (_range) ;                \
  if ((_color) [0] < r [0]) (_color) [0] = r [0];      \
  else if ((_color) [0] > r [1]) (_color) [0] = r [1]; \
MACRO_END

#define NARROW3(_color, _range) MACRO_START \
  register SYSTEMVALUE * r = (_range) ;                \
  if ((_color) [0] < r [0]) (_color) [0] = r [0];      \
  else if ((_color) [0] > r [1]) (_color) [0] = r [1]; \
  if ((_color) [1] < r [2]) (_color) [1] = r [2];      \
  else if ((_color) [1] > r [3]) (_color) [1] = r [3]; \
  if ((_color) [2] < r [4]) _color [2] = r [4];        \
  else if ((_color) [2] > r [5]) (_color) [2] = r [5]; \
MACRO_END

#define NARROW4(_color, _range) MACRO_START \
  register SYSTEMVALUE * r = (_range) ;                \
  if ((_color) [0] < r [0]) (_color) [0] = r [0];      \
  else if ((_color) [0] > r [1]) (_color) [0] = r [1]; \
  if ((_color) [1] < r [2]) (_color) [1] = r [2];      \
  else if ((_color) [1] > r [3]) (_color) [1] = r [3]; \
  if ((_color) [2] < r [4]) (_color) [2] = r [4];      \
  else if ((_color) [2] > r [5]) (_color) [2] = r [5]; \
  if ((_color) [3] < r [6]) (_color) [3] = r [6];      \
  else if ((_color) [3] > r [7]) (_color) [3] = r [7]; \
MACRO_END

#define COLOR_01_ASSERT(_color, _location_text) MACRO_START \
  HQASSERT( (_color) >= 0.0f, "Color < 0 in " _location_text ); \
  HQASSERT( (_color) <= 1.0f, "Color > 1 in " _location_text ); \
MACRO_END


/********************************************************************************
 *                                                                              *
 * The following functions provide assess to graphics state color data.         *
 *                                                                              *
 ********************************************************************************/

typedef struct COLOR_STATE COLOR_STATE;

extern COLOR_STATE *frontEndColorState;

Bool gsc_colorStateCreate(COLOR_STATE **colorStateRef);
void gsc_colorStateDestroy(COLOR_STATE **colorStateRef);
Bool gsc_colorStateTransfer(DL_STATE *page,
                            COLOR_STATE *srcColorState,
                            COLOR_STATE *dstColorState);

/** The following three functions apply to the frontend color state only.
    They are used to reset the state between pages or partial paints. */
Bool gsc_colorStateStart(void);
void gsc_colorStateFinish(Bool changing_page);
void gsc_colorStatePartialReset(void);

/* Sets up initial graphics state color structure pointers (possibly
 * just to NULL).
 */
void gsc_finish( GS_COLORinfo *colorInfo );

/* gsc_copycolorinfo copies a colorinfo structure, and reserves claims on the
 * color chains and info structures referenced by the original.
 */
void gsc_copycolorinfo( GS_COLORinfo *dst, GS_COLORinfo *src );

/* colorInfos belong to colorStates and the first state is frontEndColorState.
 * A new colorState is created for backend color transforms and all the backend
 * colorInfos need copying over to this state.
 */
Bool gsc_copycolorinfo_withstate(GS_COLORinfo *dst, GS_COLORinfo *src,
                                 COLOR_STATE *colorState);

/* gsc_freecolorinfo frees (removes claims on, freeing if necessary) color
 * chains and info structures.  Used when discarding a gstate, or HDLT idlomArgs
 * structure ('mini-gstate').
 */
void gsc_freecolorinfo( GS_COLORinfo *colorInfo );

/* gsc_areobjectsglobal returns TRUE if all graphics state color
 * objects are in global VM.
 */
Bool gsc_areobjectsglobal(corecontext_t *context, GS_COLORinfo *colorInfo );

/* gsc_scan scans gstate color info
 */
mps_res_t gsc_scan( mps_ss_t ss, GS_COLORinfo *colorInfo );

/* gsc_initgraphics sets up the graphics state color data as required
 * by the initgraphics operator.
 */
Bool gsc_initgraphics( GS_COLORinfo *colorInfo );


/* gsc_colorspaceNamesToIndex populates a colorant index array for colorants
 * in the named Separation or DeviceN colorspace.
 */
Bool gsc_colorspaceNamesToIndex(GUCR_RASTERSTYLE   *hRasterStyle,
                                OBJECT             *PSColorSpace,
                                Bool               allowAutoSeparation,
                                Bool               f_do_nci,
                                COLORANTINDEX      *pcolorants,
                                int32              n_colorants,
                                GS_COLORinfo       *colorInfo,
                                Bool               *allColorantsMatch);

/* gsc_invokeChainSingle, gsc_invokeChainBlock and gsc_invokeChainBlockViaTable.
 * Check for a valid colorchain and creates one if necessary and then calls
 * the invoke function (which performs the color transform) for each link
 * in the color chain.
 */
Bool gsc_invokeChainSingle( GS_COLORinfo *colorInfo, int32 colorType );

Bool gsc_invokeChainTransform(GS_COLORinfo *colorInfo,
                              int32 colorType,
                              COLORSPACE_ID oColorSpace,
                              Bool forCurrentCMYKColor,
                              USERVALUE *oColorValues);

Bool gsc_invokeChainBlock( GS_COLORinfo *colorInfo, int32 colorType,
                           USERVALUE *piColorValues, COLORVALUE *poColorValues,
                           int32 nColors );

Bool gsc_invokeChainBlockViaTable(GS_COLORinfo *colorInfo,
                                  int32 colorType,
                                  int32 *piColorValues,
                                  COLORVALUE *poColorValues,
                                  int32 nColors);

Bool gsc_populateHalftoneCache( GS_COLORinfo *colorInfo , int32 colorType ,
                                COLORVALUE *poColorValues ,
                                int32 nColors ) ;

Bool gsc_getChainOutputColors( GS_COLORinfo *colorInfo, int32 colorType,
                               COLORVALUE **oColorValues,
                               COLORANTINDEX **oColorants,
                               int32 *nColors,
                               Bool *fOverprinting );

Bool gsc_findBlackColorantIndex( GUCR_RASTERSTYLE* hRasterStyle );

Bool gsc_getChainOutputColorSpace( GS_COLORinfo *colorInfo, int32 colorType,
                                    COLORSPACE_ID *colorSpaceId,
                                    OBJECT **colorSpaceObj );

Bool gsc_getNSeparationColorants( GS_COLORinfo *colorInfo ,
                                  int32 colorType ,
                                  int32 *nColorants ,
                                  COLORANTINDEX **oColorants );

Bool gsc_getBaseColorListDetails( GS_COLORinfo *colorInfo ,
                                  int32 colorType ,
                                  int32* colorCount ,
                                  int32* colorantCount );

Bool gsc_getBaseColorList( GS_COLORinfo *colorInfo ,
                           int32 colorType ,
                           int32 expectedColorCount,
                           int32 expecedColorantCount,
                           USERVALUE *targetList );

Bool gsc_hasIndependentChannels( GS_COLORinfo *colorInfo ,
                                 int32 colorType ,
                                 Bool exactChannelMatch,
                                 Bool *independent );

Bool gsc_getDeviceColorColorants( GS_COLORinfo *colorInfo , int32 colorType ,
                                  int32 *pnColorants ,
                                  COLORANTINDEX **piColorants ) ;

Bool gsc_updateHTCacheForShfillDecomposition(GS_COLORinfo *colorInfo, int32 colorType);

Bool gsc_isPreseparationChain( GS_COLORinfo *colorInfo ,
                               int32 colorType ,
                               int32 *preseparation );

Bool gsc_colorChainIsComplex( GS_COLORinfo *colorInfo ,
                              int32 colorType ,
                              Bool *iscomplex ,
                              Bool *fast_rgb_gray_candidate,
                              Bool *fast_rgb_cmyk_candidate);

void gsc_setConvertAllSeparation(GS_COLORinfo *colorInfo, int convertAllSeparation);


Bool gsc_generationNumber(GS_COLORinfo *colorInfo, int32 colorType,
                          uint32* pGeneratioNumber);

Bool gsc_constructChain( GS_COLORinfo *colorInfo, int32 colorType );

Bool gsc_chainCanBeInvoked( GS_COLORinfo *colorInfo , int32 colorType ) ;

void gsc_markChainsInvalid(GS_COLORinfo *colorInfo);

int32 gsc_colorInfoSize(void);

void gsc_setOpaque(GS_COLORinfo *colorInfo,
                   Bool opaqueNonStroke, Bool opaqueStroke);
#if defined( ASSERT_BUILD )
void gsc_getOpaque(GS_COLORinfo *colorInfo,
                   Bool *opaqueNonStroke, Bool *opaqueStroke);
#endif

void gsc_setHalftonePhase(GS_COLORinfo *colorInfo, int32 phaseX, int32 phaseY) ;
int32 gsc_getHalftonePhaseX( GS_COLORinfo *colorInfo ) ;
int32 gsc_getHalftonePhaseY( GS_COLORinfo *colorInfo ) ;

void gsc_setScreenRotate(GS_COLORinfo *colorInfo, USERVALUE screenRotate) ;
USERVALUE gsc_getScreenRotate(GS_COLORinfo *colorInfo) ;

/*@dependent@*/ /*@notnull@*/
GUCR_RASTERSTYLE *gsc_getRS(
  /*@notnull@*/ /*@in@*/    const GS_COLORinfo *colorInfo);

void gsc_replaceRasterStyle(GS_COLORinfo *colorInfo, GUCR_RASTERSTYLE *rasterStyle);
void gsc_setDeviceRS(GS_COLORinfo *colorInfo, GUCR_RASTERSTYLE *deviceRS);
GUCR_RASTERSTYLE *gsc_getTargetRS(GS_COLORinfo *colorInfo);
void gsc_setTargetRS(GS_COLORinfo *colorInfo, GUCR_RASTERSTYLE *targetRS);


Bool gsc_promoteColor( GS_COLORinfo *colorInfo,
                       USERVALUE **colorValue, int32 *nColors,
                       COLORSPACE_ID *oldSpace, COLORSPACE_ID newSpace ) ;

Bool gsc_convertRGBtoCMYK( GS_COLORinfo *colorInfo,
                           USERVALUE rgb[ 3 ] , USERVALUE cmyk[ 4 ] ) ;

/* gsc_CIEToICC:
   Creates an ICC input profile from a CIE A or ABC color space.
   f16Bit indicates whether 8 or 16 bit precision is required.
   Converting a CIEBasedA color space to an RGB input profile
   should only be done when at least one of the following is
   non-trivial: MatrixA, RangeLMN, DecodeLMN, MatrixLMN.
   Otherwise a gray tone reproduction curve can be used more
   efficiently instead (or CalGray)
 */

/* An iccStream_t must be setup by the client of gsc_CIEToICC; It is an
   abstract way of specifying the output stream for the ICC profile */
typedef struct {
  Bool     (*iccPutc)(uint8 unByte, void* iccStream);
  void*     iccState;
#ifdef ASSERT_BUILD
  uint32    nStreamPosition;
#endif
} iccStream_t;

Bool gsc_CIEToICC(GS_COLORinfo *colorInfo, int32 colorType,
                  int32 f16Bit, iccStream_t* pStream);

Bool gsc_loadEraseColorInfo(GS_COLORinfo *colorInfo);
GS_COLORinfo *gsc_eraseColorInfo(void);

void gsc_save(int32 saveLevel);
Bool gsc_restore(int32 saveLevel);

Bool gsc_use_fast_rgb2cmyk(
  GS_COLORinfo  *color_info);

/** \} */

/* Log stripped */

#endif /* __GS_COLOR_H__ */
