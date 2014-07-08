/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:cvcolcvt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Converts a buffer of COLORVALUEs between color spaces.  The color chain
 * specified by colorInfo and colorType must be setup with the right input
 * and output spaces.
 */

#ifndef __CVCOLCVT_H__
#define __CVCOLCVT_H__

#include "gschcms.h" /* REPRO_COLOR_MODEL */

/**
 * As part of the effort to keep BackdropInfo small, lcmAttribs is a collection
 * of items munged into one byte. Currently, the renderingIntent, blackType
 * (for 100% black preservation), and independentChannels (for controlling color
 * management into/out of the virtual device).
 */
#define COLORINFO_RI_SHIFT                    (0)
#define COLORINFO_BLACK_TYPE_SHIFT            (3)
#define COLORINFO_INDEPENDENT_CHANNELS_SHIFT  (6)
#define COLORINFO_RI_MASK                     (0x07)
#define COLORINFO_BLACK_TYPE_MASK             (0x07 << COLORINFO_BLACK_TYPE_SHIFT)
#define COLORINFO_INDEPENDENT_CHANNELS_MASK   (0x01 << COLORINFO_INDEPENDENT_CHANNELS_SHIFT)

#define COLORINFO_RENDERING_INTENT(_lcmAttribs) \
    ((_lcmAttribs) & COLORINFO_RI_MASK)
#define COLORINFO_BLACK_TYPE(_lcmAttribs) \
    (((_lcmAttribs) & COLORINFO_BLACK_TYPE_MASK) >> COLORINFO_BLACK_TYPE_SHIFT)
#define COLORINFO_INDEPENDENT_CHANNELS(_lcmAttribs) \
    (((_lcmAttribs) & COLORINFO_INDEPENDENT_CHANNELS_MASK) >> COLORINFO_INDEPENDENT_CHANNELS_SHIFT)

#define COLORINFO_SET_RENDERING_INTENT(_lcmAttribs, _renderingIntent) \
  MACRO_START \
      HQASSERT((((_renderingIntent) << COLORINFO_RI_SHIFT) & ~COLORINFO_RI_MASK) == 0, \
               "rendering intent out of range"); \
      (_lcmAttribs) &= ~COLORINFO_RI_MASK; \
      (_lcmAttribs) |= (_renderingIntent) << COLORINFO_RI_SHIFT; \
  MACRO_END

#define COLORINFO_SET_BLACK_TYPE(_lcmAttribs, _blackType) \
  MACRO_START \
      HQASSERT((((_blackType) << COLORINFO_BLACK_TYPE_SHIFT) & ~COLORINFO_BLACK_TYPE_MASK) == 0, \
               "blackType out of range"); \
      (_lcmAttribs) &= ~COLORINFO_BLACK_TYPE_MASK; \
      (_lcmAttribs) |= (_blackType) << COLORINFO_BLACK_TYPE_SHIFT; \
  MACRO_END

#define COLORINFO_SET_INDEPENDENT_CHANNELS(_lcmAttribs, _independentChannels) \
    MACRO_START \
      HQASSERT((((_independentChannels) << COLORINFO_INDEPENDENT_CHANNELS_SHIFT) & ~COLORINFO_INDEPENDENT_CHANNELS_MASK) == 0, \
               "independentChannels not bool"); \
      (_lcmAttribs) &= ~COLORINFO_INDEPENDENT_CHANNELS_MASK; \
      (_lcmAttribs) |= (_independentChannels) << COLORINFO_INDEPENDENT_CHANNELS_SHIFT; \
    MACRO_END

/**
 * Info required to be passed from interpretation to the back end per pixel.
 * Because this is per pixel, it must be kept as small as possible.
 * Also it needs to be public...
 */
typedef struct COLORINFO {
  int32             spotNo;
  int8              colorType;
  uint8             reproType;
  REPRO_COLOR_MODEL origColorModel;
  uint8             lcmAttribs;
  uint8             spflags; /* A subset of the spflags from the LISTOBJECT,
                                for BeginPage/EndPage omission */
  uint8             label;   /* flags indicating the source of a pixel */
} COLORINFO;

#define COLORINFO_SET(_info, _spotNo, _colorType, _reproType,                  \
                      _origColorModel, _renderingIntent,                       \
                      _blackType, _independentChannels,                        \
                      _spflags, _label)                                        \
MACRO_START                                                                    \
  (_info).spotNo         = (_spotNo);                                          \
  (_info).colorType      = (_colorType);                                       \
  (_info).reproType      = CAST_UNSIGNED_TO_UINT8(_reproType);                 \
  (_info).origColorModel = CAST_UNSIGNED_TO_UINT8(_origColorModel);            \
  (_info).lcmAttribs     = CAST_UNSIGNED_TO_UINT8(_renderingIntent) |          \
            CAST_UNSIGNED_TO_UINT8(_blackType) << COLORINFO_BLACK_TYPE_SHIFT | \
            CAST_UNSIGNED_TO_UINT8(_independentChannels) << COLORINFO_INDEPENDENT_CHANNELS_SHIFT; \
  (_info).spflags        = CAST_UNSIGNED_TO_UINT8(_spflags);                   \
  (_info).label          = (_label);                                           \
MACRO_END

#define COLORINFO_EQ(_info1, _info2) \
  ((_info1).spotNo         == (_info2).spotNo && \
   (_info1).colorType      == (_info2).colorType && \
   (_info1).reproType      == (_info2).reproType && \
   (_info1).origColorModel == (_info2).origColorModel && \
   (_info1).lcmAttribs     == (_info2).lcmAttribs && \
   (_info1).spflags        == (_info2).spflags && \
   (_info1).label          == (_info2).label)

struct core_init_fns ; /* from SWcore */

void cvcolcvt_C_globals(struct core_init_fns *fns);

typedef struct CV_COLCVT CV_COLCVT;

CV_COLCVT *cv_colcvtopen(DL_STATE *page, int32 method,
                         GS_COLORinfo *colorInfo, int32 colorType,
                         Bool in16, Bool out16, uint32 inComps,
                         uint32 outComps, COLORANTINDEX *outColorants,
                         Bool fromPageGroup, LateColorAttrib *lca);
void cv_colcvtfree(DL_STATE *page, CV_COLCVT **freeConverter);

Bool cv_colcvt(CV_COLCVT *converter, uint32 nColors,
               COLORINFO *info /* optional */, COLORVALUE *inColors,
               COLORVALUE *outColors, uint8 *outColors8);

void cv_quantiseplane(CV_COLCVT *converter, uint32 nColors,
                      int plane, unsigned int offset, unsigned int stride,
                      COLORVALUE *inColors,
                      COLORVALUE *outColors, uint8 *outColors8);

/**
 * Require a mutex in cv_colcvt() to lock the use of buffers in CV_COLCVT and
 * also around the color conversion code which is not thread safe.
 *
 * The lock is also used inside preconvert_on_the_fly().
 */
void cvcolcvt_lock(void);
void cvcolcvt_unlock(void);

#endif /* protection for multiple inclusion */

/* Log stripped */
