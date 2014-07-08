/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!export:graphics.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This header file defines the graphic data types for the PostScript/PDF/XPS
 * imaging model.
 */

#ifndef __GRAPHICS_H__
#define __GRAPHICS_H__

#include "graphict.h" /* typedefs */
#include "paths.h"    /* PATHINFO, etc */
#include "objecth.h"  /* OBJECT, NAMECACHE */
#include "ndisplay.h" /* NFILLOBJECT */
#include "matrix.h"   /* OMATRIX */
#include "bitbltt.h"  /* FORM */
#include "objects.h"
#include "objnamer.h"
#include "group.h"

struct font_methods_t ; /* from COREfonts */
struct FONTCACHE ;      /* from COREfonts */
struct MATRIXCACHE ;    /* from COREfonts */

/**
 * \defgroup gstate Graphics State.
 * \ingroup core
 * \{
 */

/* Graphics State Macros. */

#define theCharXWidth(val)   ((val).c_width)
#define theCharYWidth(val)   ((val).c_height)
#define theXBearing(val)     ((val).xbear)
#define theYBearing(val)     ((val).ybear)
#define theCount(val)        ((val).count)
#define theICount(val)       ((val)->count) /*deprecated*/
#define theForm(val)         ((val).thebmapForm)

#define theFontId(val)       ((val).fontid)
#define theStdMapping(val)   ((val).stdmapping)

#define doStrokeIt(val)      ((val).strokeit)
#define thePathCTM(val)      ((val).pathctm)
#define thePathOf(val)       ((val).apath)

/* Graphic object's macros. */
#define theSubPath(val)      ((val).subpath)
#define theISubPath(val)     ((val)->subpath) /*deprecated*/
#define theWindingNo(val)    ((val).winding)
#define theLineType(val)     ((val).type)
#define theILineType(val)    ((val)->type) /*deprecated*/
#define theLineOrder(val)    ((val).order)
#define theILineOrder(val)   ((val)->order) /*deprecated*/
#define thePoint(val)        ((val).point)
#define theIPoint(val)       ((val)->point) /*deprecated*/
#define theX(val)            ((val).x)
#define theY(val)            ((val).y)

/* Linelist bit flags for orientation (used in clippath), IsStroked and
   dash continuation (PCL). */
#define LINELIST_ORIENT_PLUS  (0x1)
#define LINELIST_ORIENT_MINUS (0x2)
#define LINELIST_UNSTROKED    (0x4)
#define LINELIST_CONT_DASH    (0x8)

#define LINELIST_ORIENT_MASK  (LINELIST_ORIENT_PLUS|LINELIST_ORIENT_MINUS)

#define SET_LINELIST_ORIENT(_line, _orient) MACRO_START \
  HQASSERT((_orient) == -1 || (_orient) == 0 || (_orient) == 1, \
           "orient value must be -1, 0 or 1"); \
  (_line)->flags &= ~LINELIST_ORIENT_MASK; \
  if ((_orient) == -1) \
    (_line)->flags |= LINELIST_ORIENT_MINUS; \
  else if ((_orient) == 1) \
    (_line)->flags |= LINELIST_ORIENT_PLUS; \
MACRO_END

#define LINELIST_ORIENT(_line) \
  (((_line)->flags & LINELIST_ORIENT_PLUS) ? 1 : \
     ((_line)->flags & LINELIST_ORIENT_MINUS) ? -1 : 0)

#define SET_LINELIST_STROKED(_line, _stroked) MACRO_START \
  if (_stroked) \
    (_line)->flags &= ~LINELIST_UNSTROKED; \
  else \
    (_line)->flags |= LINELIST_UNSTROKED; \
MACRO_END

#define SET_LINELIST_CONT_DASH(_line, _cont_dash) MACRO_START \
  if (_cont_dash) \
    (_line)->flags |= LINELIST_CONT_DASH; \
  else \
    (_line)->flags &= ~LINELIST_CONT_DASH; \
MACRO_END

/* The default is orientation zero, stroked true and reset dashing (per-sub-path). */
#define INIT_LINELIST_FLAGS(_line) (_line)->flags = 0;

/* scalefont macros. */
#define theFontPtr(val)   ((val).fontdptr)

struct CHARPATHS {
  int32 strokeit ;
  PATHINFO *chpath ;
  PATHLIST **apath ;
  CHARPATHS *next ;
} ;
#define thePathInfoOf(val)     ((val).chpath)


/*----------------------------------------------------------------------------
   These structure definitions define the font cache data structures.
----------------------------------------------------------------------------*/
typedef struct {
  int32 sid ;
  OBJECT *fontdptr ;
  OMATRIX omatrix ;
  OBJECT thefont ;
} MFONTLOOK ;

/* Extra information if a type 32 font is defined with metrics for wmode 1 as
   well as wmode 0. */
typedef struct {
  SYSTEMVALUE w;
  SYSTEMVALUE h;
  SYSTEMVALUE xoffset;
  SYSTEMVALUE yoffset;
} T32_DATA;

#define theXOffset(val)        ((val).xoffset)
#define theYOffset(val)        ((val).yoffset)
#define theWidth(val)          ((val).w)
#define theHeight(val)         ((val).h)

typedef struct CHARCACHE {
  int32  type ;                    /* MUST be first element - see FORM data type */
  int32  usagecnt ;                /* How many times char used on this page */
  OBJECT glyphname ;
  uint16 wmode ;                   /* Does_Composite_Fonts */
  uint8  t32flags ;
  uint8  waste ;
  int32  pageno ;                  /* Last page number on which char is on DL */
  int32  rlesize ;                 /* Size if we did RLE it */
  FORM *thebmapForm ;

  /* The inverse of 'thebmapForm'; used in PCL glyph rendering. This may be NULL
   * even if inverse_generated is true; indicating that thebmapForm was empty. */
  OFFSETFORM *inverse ;

  /* TRUE if we have attempted to generate the inverse form. If this is TRUE and
   * the inverse form is NULL, thebmapForm is empty. */
  Bool inverse_generated ;

  SYSTEMVALUE xbear , ybear ;
  SYSTEMVALUE c_width , c_height ;
  T32_DATA *t32data ;
  struct CHARCACHE *next ;
  void  *matrix ;                  /* Opaque pointer to matrix, for trapping */
  int32  baseno ;                  /* First page number on which char is on DL */
} CHARCACHE ;

#define T32_MASTER_FLAG    (0x01)
#define T32_BOTHMODES_FLAG (0x02)

#define SetIT32MasterFlag(val)      ((val)->t32flags |= T32_MASTER_FLAG)
#define ClearIT32MasterFlag(val)    ((val)->t32flags &= ~T32_MASTER_FLAG)
#define SetIT32BothModesFlag(val)   ((val)->t32flags |= T32_BOTHMODES_FLAG)
#define ClearIT32BothModesFlag(val) ((val)->t32flags &= ~T32_BOTHMODES_FLAG)

#define isIT32Master(val)        ((val)->t32flags & T32_MASTER_FLAG)
#define isIT32BothModes(val)     ((val)->t32flags & T32_BOTHMODES_FLAG)

/*----------------------------------------------------------------------------
   These structure definitions define the graphics state data structure.
----------------------------------------------------------------------------*/
enum {
  CLIPID_INVALID = 0
} ;

struct CLIPRECORD {
  uint32 refcount ;
  int32 clipno ;
  int32 pagebaseid ;
  USERVALUE clipflat ;  /* 0.0 to use gstate's flatness, < 0.0 indicates fill rather
                          than path in u below */
  union {
    PATHINFO clippath ;
    NFILLOBJECT * nfill;
  } u;
  dbbox_t bounds ;
  struct CLIPRECORD *next ;
  uint8 systemalloc ;
  uint8 cliptype ;
} ;

#define theClipType(val)        ((val).cliptype)
#define theClipRefCount(val)    ((val).refcount)
#define theClipNo(val)          ((val).clipno)
#define theClipFlat(val)        ((val).clipflat)
#define theClipPath(val)        ((val).u.clippath)
#define theX1Clip(val)          ((val).bounds.x1)
#define theY1Clip(val)          ((val).bounds.y1)
#define theX2Clip(val)          ((val).bounds.x2)
#define theY2Clip(val)          ((val).bounds.y2)

#define theISystemAlloc(val)    ((val)->systemalloc)

struct CLIPPATH {
  sbbox_t rbounds ; /* Fractional (real) bounds */
  dbbox_t bounds ;  /* Device space coordinate bounds */
  CLIPRECORD *cliprec ;
  uint32 refcount ;
  int32 pagebaseid ;
  struct CLIPPATH *next ;
} ;

#define theXd1Clip(val)      ((val).rbounds.x1)
#define theYd1Clip(val)      ((val).rbounds.y1)
#define theXd2Clip(val)      ((val).rbounds.x2)
#define theYd2Clip(val)      ((val).rbounds.y2)
#define theClipRecord(val)   ((val).cliprec)
#define theClipStack(val)    ((val).next)

#define thegsPageBaseID(_v)  ((_v).pagebaseid)

#define COLOR_MODE_MONOCHROME             1
#define COLOR_MODE_GRAYTONE               2
#define COLOR_MODE_RGB_PIXEL_INTERLEAVED  3
#define COLOR_MODE_CMYK_PIXEL_INTERLEAVED 4
#define COLOR_MODE_RGB_BAND_INTERLEAVED   5
#define COLOR_MODE_CMYK_BAND_INTERLEAVED  6
#define COLOR_MODE_RGB_FRAME_INTERLEAVED  7
#define COLOR_MODE_CMYK_FRAME_INTERLEAVED 8

/* This never used in isolation from the gstate, so typedef left in this
   file */
typedef struct PDEVinfo {
  /* These bits are set wrt the page device. */
  int32 devicebandid ;
  USERVALUE devicescreenrotate ;
  int32 devicehalftonephasex, devicehalftonephasey ;
  OBJECT deviceshowproc ;

  OBJECT pagedevicedict ;
  OMATRIX pagedeviceCTM ;
  OMATRIX inversepagedeviceCTM ; /* From device space to default user space */

  uint8 devicestrokeadjust ; /* May be overridden by ForceStrokeAdjust SystemParam */
  uint8 scanconversion ; /* SC_RULE_TOUCHING, SC_RULE_TESSELATE */

  uint8 spare1[2] ; /* PADDING. USE ME FIRST. */

  /* These bits get set with sub-devices (e.g. characters & patterns. */
  int32 devicew , deviceh ;
  int32 devicetype ;
  OMATRIX devicectm ;

  int32 did;  /* Distill ID: to finalise things before VM disappears */
  USERVALUE smoothness ; /* Shaded fill smoothness */
  int32 pagebaseid ;
  CLIPRECORD *initcliprec ; /* The device clip record (set by clip_device_new) */
} PDEVinfo ;

#define thegsDeviceStrokeAdjust(_v) ((_v).thePDEVinfo.devicestrokeadjust)
#define thegsDevicePageDict(_v) ((_v).thePDEVinfo.pagedevicedict)
#define thegsDevicePageCTM(_v) ((_v).thePDEVinfo.pagedeviceCTM)
#define thegsDeviceInversePageCTM(_v) ((_v).thePDEVinfo.inversepagedeviceCTM)
#define thegsDeviceHalftonePhaseX(_v) ((_v).thePDEVinfo.devicehalftonephasex)
#define thegsDeviceHalftonePhaseY(_v) ((_v).thePDEVinfo.devicehalftonephasey)
#define thegsDeviceType(_v) ((_v).thePDEVinfo.devicetype)
#define thegsDeviceCTM(_v) ((_v).thePDEVinfo.devicectm)
#define thegsDeviceBandId(_v) ((_v).thePDEVinfo.devicebandid)
#define thegsDeviceShowProc(_v) ((_v).thePDEVinfo.deviceshowproc)
#define thegsDeviceW(_v) ((_v).thePDEVinfo.devicew)
#define thegsDeviceH(_v) ((_v).thePDEVinfo.deviceh)
#define thegsDeviceScreenRotate(_v) ((_v).thePDEVinfo.devicescreenrotate)
#define thegsDistillID(_v) ((_v).thePDEVinfo.did)
#define thegsSmoothness(_v) ((_v).thePDEVinfo.smoothness)

#define theIgsDevicePageDict(_v) thegsDevicePageDict(*(_v)) /*deprecated*/
#define theIgsDevicePageCTM(_v) thegsDevicePageCTM(*(_v)) /*deprecated*/
#define theIgsDeviceInversePageCTM(_v) thegsDeviceInversePageCTM(*(_v)) /*deprecated*/
#define theIgsDeviceCTM(_v) thegsDeviceCTM(*(_v)) /*deprecated*/
#define theIgsDistillID(_v) thegsDistillID(*(_v)) /*deprecated*/

/* This never used in isolation from the gstate, so typedef left in this
   file */
typedef struct PAGEinfo {
  OMATRIX  thectm ;     /* Working CTM of gstate. */
  CLIPPATH theclip ;    /* Clipping related to the current page. */
} PAGEinfo;

#define thegsPageCTM(_v)     ((_v).thePAGEinfo.thectm)
#define thegsPageClip(_v)    ((_v).thePAGEinfo.theclip)

#define theIgsPageCTM(_v)    thegsPageCTM(*(_v)) /*deprecated*/

struct HALFTONE {
  USERVALUE angl ;
  USERVALUE freq ;
  OBJECT spotfn ;
} ;


#define ST_SETHALFTONE        0
#define ST_SETSCREEN          1 /* set(color)screen +ve, sethalftone 0. */
#define ST_SETPATTERN         2 /* pattern screens derived from setscreen. */
#define ST_SETCOLORSCREEN     3
#define ST_SETCOLORPATTERN    4 /* pattern screens derived from setcolorscreen. */
#define ST_SETMISSING         5 /* used by setup_implicit_screens for missing screens. */

/* ---------------------------------------------------------------- */

enum {
  /* Normal case for PS, PDF, XPS etc */
  DASHMODE_FIXED,

  /* Adjusts pattern length to fill a segment with an integer multiple
     number of patterns (for HPGL2). */
  DASHMODE_ADAPTIVE,

  /* Treat dash list values as percentages of each line segment (for HPGL2). */
  DASHMODE_PERCENTAGE
} ;

struct LINESTYLE {
  USERVALUE linewidth ;
  USERVALUE flatness ;
  USERVALUE miterlimit ;
  USERVALUE dashoffset ;
  OBJECT dashpattern ;
  SYSTEMVALUE *dashlist ;
  uint16 dashmode ;
  uint16 dashlistlen ;
  uint8 startlinecap ;
  uint8 endlinecap ;
  uint8 dashlinecap ;
  uint8 linejoin ;
} ;

#define theLineWidth(val)       ((val).linewidth)
#define theFlatness(val)        ((val).flatness)
#define theMiterLimit(val)      ((val).miterlimit)
#define theDashPattern(val)     ((val).dashpattern)
#define theDashList(val)        ((val).dashlist)
#define theDashListLen(val)     ((val).dashlistlen)
#define theDashOffset(val)      ((val).dashoffset)
#define theStartLineCap(val)    ((val).startlinecap)
#define theEndLineCap(val)      ((val).endlinecap)
#define theDashLineCap(val)     ((val).dashlinecap)
#define theLineJoin(val)        ((val).linejoin)


/* Types of show operations. Must fit in uint8. */
enum { DOSHOW = 0, DOSTRINGWIDTH, DOCHARPATH, DOTYPE4SHOW, NOSHOWTYPE,
       DOXPS = 1<<7 } ;

/* The FONTinfo structure would probably be more logically defined in the
   COREfonts compound, but it is kept here for now so that all compounds
   using the gstate do not need to include the fonts compound. */
/* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
struct FONTinfo {
  /* Internal cached font PS variables. */
  OBJECT thefont ;
  OBJECT subfont ;
  OBJECT rootfont ;
  OBJECT theencoding ;
  OBJECT *thecharstrings ;
  OBJECT *themetrics ;
  OBJECT *themetrics2 ;
  OBJECT *fontbbox ;

  OBJECT fdepvector ;
  OBJECT prefenc ;
  OBJECT cdevproc ;

  /* Internal cached font C variables. */
  uint8 fonttype , painttype , encrypted ;
  uint8 unused1 ; /* USE ME FIRST */
  int32 currfid ;
  int32 uniqueid ;
  USERVALUE strokewidth ;

  uint16 subscount ;
  uint8 subsbytes, fmaptype ;
  union {
    uint32 *subsvector ;
    OBJECT *cmap;
    uint16 fdindex ;     /* current index into CID font's FDArray */
    struct {
      uint8 escchar , shiftin , shiftout , unused2 ;
    } mapthings ;
  } umapthings ;

  /* Internal C variables, used for things like font cache lookup,... */
  uint8 gotmatrix, wmode , wmodeneeded, cancache ;

  OMATRIX fontmatrix ;           /* Font space to device space */
  OMATRIX fontATMTRM ;           /* PDF font scaling factor */
  OMATRIX fontcompositematrix ;  /* Font space to user space (no CTM) */
  OMATRIX scalematrix ;          /* Font expansion factor */
  /* Relationships between font matrices:

     FCM = fontcompositematrix (font units to userspace)
     UFM = unscaled FontMatrix (font units to em units)
     FM = fontmatrix (font units to device space)
     SM = scalematrix (concatenated from multiple scalefont/makefonts)

     SM = SM(n) * ... SM(1) * SM(0) * ATMTRM
     FCM = UFM * SM
     FM = FCM * CTM

     Composite fonts preserve these relationships. Each hierarchical fontmatrix
     adjusts SM by its ScaleMatrix, and FM/FCM by its FontMatrix.
   */

  struct FONTCACHE   *lfont ;   /* Entry in fontcache for current font */
  struct MATRIXCACHE *lmatrix ; /* Entry in fontcache for current fontmatrix */

  /* I'm a bit dubious about putting callback functions in the gstate, but it
     seems to be the cleanest way of getting the various font containers and
     charstring formats correct. set_font() and set_cid_subfont() install a
     pointer to an appropriate set of font methods.  */
  struct font_methods_t *fontfns ;
} ;

#define theGlyphChar(val)     ((val).glyphchar)
#define theGlyphName(val)     ((val).glyphname)
#define theFontCompositeMatrix(val) ((val).fontcompositematrix)
#define theCurrFid(val)       ((val).currfid)
#define theFontMatrix(val)    ((val).fontmatrix)
#define theFontATMTRM(val)    ((val).theFONTinfo.fontATMTRM)
#define theFontType(val)      ((val).fonttype)
#define theIFontType(val)     ((val)->fonttype) /*deprecated*/
#define thePaintType(val)     ((val).painttype)
#define isEncrypted(val)      ((val).encrypted)
#define gotFontMatrix(val)    ((val).gotmatrix)
#define theStrokeWidth(val)   ((val).strokewidth)
#define theLookupFont(val)    ((val).lfont)
#define theLookupMatrix(val)  ((val).lmatrix)
#define theMyFont(val)        ((val).thefont)
#define theEncoding(val)      ((val).theencoding)
#define theCharStrings(val)   ((val).thecharstrings)
#define theMetrics(val)       ((val).themetrics)
#define theUniqueID(val)      ((val).uniqueid)
#define theFDIndex(val)       ((val).umapthings.fdindex)

/** \ingroup hdlt \brief HDLT information stored in gstate. */
struct HDLTinfo {
  int8        state ;           /**< see HDLT_* pseudoliterals. */
  int8        target ;          /**< intended target; redundant to gtype? */
  int8        proxy ;           /**< actually-handling target (enum). */
  int8        frameBegun ;      /**< return from begin callback, or FALSE. */
  int32       cacheID ;         /**< cache ID of whatever's in this frame. */
  SYSTEMVALUE trans[2] ;        /**< offset of device coords from absolute. */
  SYSTEMVALUE offset[2] ;       /**< offset of reported coords from absolute. */
  SYSTEMVALUE position[2] ;     /**< position of containing character. */
  OBJECT      hooksDict ;       /**< Current callback state. */
  OBJECT      hooksOrig ;       /**< Original callback state dictionaries. */
  OBJECT     *object ;          /**< what's being done (e.g. char name, upath). */
  void       *void1 ;           /**< extra bit (e.g. font dict, ...). */
  int32       showCount ;       /**< character number in "this" show. */
  int32       cacheOp ;         /**< NAME_null or NAME_setcachedevice, etc. */
  FONTinfo   *baseFontInfo ;    /**< fontinfo for current font. */
  struct HDLTinfo *next ;       /**< Enclosing target. */
} ;

/** \ingroup hdlt \brief Values of the HDLT state variable. */
enum {
  HDLT_DISABLED,
  HDLT_NEWDICT,
  HDLT_NORMAL
} ;

#define isHDLTEnabled(val)      ((val).theHDLTinfo.state  != HDLT_DISABLED)
#define isHDLTNormal(val)       ((val).theHDLTinfo.state  == HDLT_NORMAL)
#define isHDLTDictChanged(val)  ((val).theHDLTinfo.state  == HDLT_NEWDICT)

#define theIdlomState(val)      ((val).theHDLTinfo.state)

/* This never used in isolation from the gstate, so typedef left in this
   file */
typedef struct trapinfo {
  int32 trapIntent ;                /* Boolean: DL objects added while this is
                                     * true will be trapped at showpage time.
                                     */
} TRAPinfo ;

#define theTRAPInfo(val)                ((val).theTRAPinfo)
#define theTrapIntent(val)              (theTRAPInfo(val).trapIntent)
#define theITrapIntent(val)             (theTRAPInfo(*(val)).trapIntent)

/* This never used in isolation from the gstate, so typedef left in this
   file */
typedef struct pdffinfo {
  SYSTEMVALUE Tc ;      /* Text Character spacing. */
  SYSTEMVALUE Tfs ;     /* Text Font Size. */
  SYSTEMVALUE TL ;      /* Text Leading. */
  SYSTEMVALUE Ts ;      /* Text riSe. */
  SYSTEMVALUE Tw ;      /* Text Word spacing. */
  SYSTEMVALUE Tz ;      /* Text horiZontal scaling. */
  OBJECT      Tf ;      /* Text Font. */
  int32       Tr ;      /* Text Rendering mode. */
} PDFFinfo ;

#define thePDFFInfo(val)                ((val).thePDFFinfo)
#define thePDFFCharSpace(val)           (thePDFFInfo(val).Tc)
#define theIPDFFCharSpace(val)          (thePDFFInfo(*(val)).Tc) /*deprecated*/
#define thePDFFFontSize(val)            (thePDFFInfo(val).Tfs)
#define theIPDFFFontSize(val)           (thePDFFInfo(*(val)).Tfs) /*deprecated*/
#define thePDFFLeading(val)             (thePDFFInfo(val).TL)
#define theIPDFFLeading(val)            (thePDFFInfo(*(val)).TL) /*deprecated*/
#define thePDFFRise(val)                (thePDFFInfo(val).Ts)
#define theIPDFFRise(val)               (thePDFFInfo(*(val)).Ts) /*deprecated*/
#define thePDFFWordSpace(val)           (thePDFFInfo(val).Tw)
#define theIPDFFWordSpace(val)          (thePDFFInfo(*(val)).Tw) /*deprecated*/
#define thePDFFHorizScale(val)          (thePDFFInfo(val).Tz)
#define theIPDFFHorizScale(val)         (thePDFFInfo(*(val)).Tz) /*deprecated*/
#define thePDFFFont(val)                (thePDFFInfo(val).Tf)
#define theIPDFFFont(val)               (thePDFFInfo(*(val)).Tf) /*deprecated*/
#define thePDFFRenderMode(val)          (thePDFFInfo(val).Tr)
#define theIPDFFRenderMode(val)         (thePDFFInfo(*(val)).Tr) /*deprecated*/

#define PDFRENDERMASK_FILL              0x01
#define PDFRENDERMASK_STROKE            0x02
#define PDFRENDERMASK_CLIP              0x04
#define PDFRENDERMODE_MIN               0
#define PDFRENDERMODE_MAX               7
#define PDFRENDERMODE_TABLESIZE         8
/* Lookup table defined in SWpdf!src:pdfshow.c */
extern const int8 pdfRenderModeTable[PDFRENDERMODE_TABLESIZE];

#define isPDFRenderMode(val, flag)     \
  HQASSERT_EXPR((val) >= 0 && \
                (val) < PDFRENDERMODE_TABLESIZE, \
                "Invalid PDF font render mode", \
                (pdfRenderModeTable[(val)] & (flag)) != 0)
#define isPDFRenderModeFill(val)      (isPDFRenderMode((val), PDFRENDERMASK_FILL))
#define isPDFRenderModeStroke(val)    (isPDFRenderMode((val), PDFRENDERMASK_STROKE))
#define isPDFRenderModeClip(val)      (isPDFRenderMode((val), PDFRENDERMASK_CLIP))
#define isPDFRenderModeNone(val)      \
  (!isPDFRenderMode((val), PDFRENDERMASK_FILL | PDFRENDERMASK_STROKE | PDFRENDERMASK_CLIP))

/* path epsilons */
typedef struct pa_epsilon_struct {
  /* Standard error margin. */
  SYSTEMVALUE ex ;
  SYSTEMVALUE ey ;
  /* Double error margin. */
  SYSTEMVALUE e2x ;
  SYSTEMVALUE e2y ;

  /* Error for pathsaresimilar displacement test. */
  /* Standard error margin. */
  SYSTEMVALUE epx ;
  SYSTEMVALUE epy ;
  /* Double error margin. */
  SYSTEMVALUE e2px ;
  SYSTEMVALUE e2py ;

  /* Error for part h) displacement test. */
  /* Standard error margin. */
  SYSTEMVALUE edx ;
  SYSTEMVALUE edy ;
  /* Double error margin. */
  SYSTEMVALUE e2dx ;
  SYSTEMVALUE e2dy ;

  /* Error for exact match */
  /* Standard error margin. */
  SYSTEMVALUE eex ;
  SYSTEMVALUE eey ;
  /* Double error margin. */
  SYSTEMVALUE ee2x ;
  SYSTEMVALUE ee2y ;

  /* Error for part d) bbox check of dl object. */
  int32 eix ;
  int32 eiy ;
} pa_epsilon_t ;


/* ---------------------------------------------------------------- */

/* TranState structures. */

struct SoftMask {
  SoftMaskType type;
  uint32 groupId;
};

struct TranState {
  uint8 opaqueStroke;
  uint8 opaqueNonStroke;

  uint8 alphaIsShape;
  uint8 textKnockout;
  USERVALUE strokingAlpha;
  USERVALUE nonstrokingAlpha;

  uint32 blendMode;
  SoftMask softMask;

  OBJECT_NAME_MEMBER
};

/* ---------------------------------------------------------------- */

/* GSTATE tagging info. */

/* This never used in isolation from the gstate, so typedef left in this
   file */
typedef struct GSTAGinfo {
  OBJECT dict ;
  GSTAGSTRUCTUREOBJECT *structure ;
  uint32 *data ;
} GSTAGinfo ;

/* ---------------------------------------------------------------- */

struct GSTATE {
  TypeTag typetag ; /* must be first field */
  uint8 user_label; /* Boolean for user labels */
  int16 unused2 ;
  int32     gId ;               /* ID for this particular gstate. */
  int32     gType ;             /* Type of gsave operation used on this gstate. */
  int32     slevel ;            /* Save level used for this particular gstate. */
  int32     saved ;             /* This gstate has been subject to gstate/currentgstate/copy. */
  GSTATE    *next ;             /* Graphics stack is linked list of gstates. */

  PATHINFO  thepath ;           /* Path to which fill/stroke to be applied. */
  LINESTYLE thestyle ;          /* Line style to be applied for a stroke. */

  PDEVinfo  thePDEVinfo ;       /* Device information. */
  PAGEinfo  thePAGEinfo ;       /* Page information; e.g. clipping, ctm. */

  GS_COLORinfo *colorInfo ;     /* The new gstate color variables and chains allocated at end of structure */
  FONTinfo  theFONTinfo ;       /* Font details loaded into the RIPs 'ATM'. */

  HDLTinfo  theHDLTinfo;
  TRAPinfo  theTRAPinfo;
  PDFFinfo  thePDFFinfo;        /* Font info for PDF. */

  TranState tranState;          /* Transparency State. */

  GSTAGinfo theGSTAGinfo  ;     /* GSTATE tag info */

  pa_epsilon_t pa_eps ;       /* path epsilons */
} ;

#define thePathInfo(val)       ((val).thepath)
#define theIPathInfo(val)      ((val)->thepath) /*deprecated*/
#define theLineStyle(val)      ((val).thestyle)
#define theILineStyle(val)     ((val)->thestyle) /*deprecated*/

#define theFontInfo(val)       ((val).theFONTinfo)
#define theIFontInfo(val)      ((val)->theFONTinfo) /*deprecated*/

#define CurrentPoint           ((thePathInfo(*gstateptr)).lastline)

enum {
  GST_NOTYPE ,                  /* arbitrary gstate eg PS gstate object */
  GST_CURRENT ,                 /* marks the current gstate */
  GST_GSAVE ,                   /* OLD 'g' */
  GST_SAVE ,                    /* OLD 's' */
  GST_FORM ,                    /* OLD 'f' */
  GST_PATTERN ,                 /* OLD 'p' */
  GST_SHADING ,                 /* entirely new type */
  GST_GROUP ,                   /* entirely new type */
  GST_SETCHARDEVICE ,           /* OLD 'b' */
  GST_SETPAGEDEVICE ,           /* OLD 'h' */
  GST_SHOWPAGE ,                /* new type for recombine forcing of showpage */
  GST_PDF ,                     /* called from pdf Q only, same as GST_GSAVE except path isn't saved */
  GST_PCL5 ,                    /* The pushed PCL5 gstate when in HPGL */
  GST_HPGL                      /* The pushed HPGL gstate when in PCL5 */
} ;

/** \} */

/*
Log stripped */
#endif /* protection for multiple inclusion */
