/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!export:graphict.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Simple and incomplete typedefs for graphics state module.
 */

#ifndef __GRAPHICT_H__
#define __GRAPHICT_H__

/* Do NOT include any headers, or make this file depend on any other
   headers */

/* Main gstate typedef */
typedef struct GSTATE GSTATE;

#define GS_INVALID_GID    -1
#define GS_INVALID_MID    0
#define GS_INVALID_SLEVEL -1

/*---------------------------------------------------------------------------*/
/* In paths.h */
typedef struct LINELIST LINELIST ;
typedef struct PATHLIST PATHLIST ;
typedef struct PATHINFO PATHINFO ;

/*---------------------------------------------------------------------------*/
/* In graphics.h */
typedef struct CLIPRECORD CLIPRECORD ;
typedef struct CLIPPATH CLIPPATH ;
typedef struct HALFTONE HALFTONE ;
typedef struct GS_COLORinfo GS_COLORinfo;

/* Rasterstyle modelling */
typedef struct GUCR_CHANNEL               GUCR_CHANNEL;
typedef struct GUCR_COLORANT              GUCR_COLORANT;
typedef struct GUCR_RASTERSTYLE           GUCR_RASTERSTYLE;
typedef struct GUCR_PHOTOINK_INFO         GUCR_PHOTOINK_INFO;

/* Line styles; dashes, caps, joins, mitres, width, etc */
typedef struct LINESTYLE LINESTYLE ;

/* HDLT info; targets, proxies, offsets, etc. */
typedef struct HDLTinfo HDLTinfo ;

/* Font info; dictionary, sub-dictionary, type, etc. */
typedef struct FONTinfo FONTinfo ;

/* Transparency information */
typedef struct SoftMask SoftMask;
typedef struct TranState TranState;

typedef int32 SoftMaskType;

/* Possible values for SoftMaskType. */
enum {
  EmptySoftMask = 1,
  AlphaSoftMask,
  LuminositySoftMask
};

#define validSoftMaskType(type_) \
  (((type_) == EmptySoftMask) || \
   ((type_) == AlphaSoftMask) || \
   ((type_) == LuminositySoftMask))

/* Charpaths */
typedef struct CHARPATHS CHARPATHS ;

/*---------------------------------------------------------------------------*/
/* Defines for the two fill rules.  It is assumed in various places that
 *   X & CLIPRULE is either NZFILL_TYPE or EOFILL_TYPE
 */

/* Layout of filltype or cliptype byte: [hugo, January 31, 1996, task #7219]
 *
 *      Bit  |   7   |   6   |   5   |   4   |   3   |   2   |   1   |   0   |
 *           +-------+-------+-------+-------+-------+-------+-------+-------+
 * cliptype: |   0   |   1   |   EO  |  NZ   | Degn. | Rect. | Invert| Normal|
 *           +-------+-------+-------+-------+-------+-------+-------+-------+
 * filltype: |   1   |   0   |   EO  |  NZ   |   0   |   0   |   0   |   0   |
 *           +-------+-------+-------+-------+-------+-------+-------+-------+
 * recttype: |   0   |   0   |   EO  |  NZ   |   0   |   0   |   0   |   1   | << NEW
 *           +-------+-------+-------+-------+-------+-------+-------+-------+
 * (other) : |   0   |   0   |   EO  |  NZ   |   0   |   0   |   0   |   0   |
 *           +-------+-------+-------+-------+-------+-------+-------+-------+
 */

#define NZFILL_TYPE 0x10        /* default; was 1, changed to find old bits */
#define EOFILL_TYPE 0x20        /* was 0, changed to flush out old bits */
#define FILLRULE    (NZFILL_TYPE|EOFILL_TYPE)
#define CLIPRULE    FILLRULE

/* fields for object style - none means a char or stroke "(other)" above */
#define ISCLIP      0x40
#define ISFILL      0x80        /* maybe add at some stage ISRECT & ISCHAR */
#define ISRECT      0x01        /* (See just below) */
#define ISCHAR    0x0100        /* extend into 16bits */
#define ISSTRK    0x0200

/* fields for cliptype */
#define CLIPNORMALISED 0x01
#define CLIPINVERT     0x02
#define CLIPISRECT     0x04     /* Indicates if clip path is rectangular */
#define CLIPISDEGN     0x08

#define CLIPPED_RHS 0x0400      /* for nfills clipped on the rhs; req for recombine */

/* Hint that the fill code will need to run local span merging in order to
   avoid double strikig pixels */
#define MERGE_SPANS 0x0800

/**
 * Hint that the NFILL has low pixel coverage and is probably not worth
 * trying to render via rollovers.
 */
#define SPARSE_NFILL 0x1000

/* Log stripped */
#endif /* __GRAPHICT_H__ */
