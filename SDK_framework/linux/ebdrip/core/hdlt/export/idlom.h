/** \file
 * \ingroup hdlt
 *
 * $HopeName: COREhdlt!export:idlom.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains headers for Harlequin's "Postscript-Editable Display
 * List" extensions, whereby users (or, more likely, OEMs) can register
 * postscript callbacks to be called when various objects are added to a
 * display list.
 */

#ifndef __IDLOM_H__
#define __IDLOM_H__

#include "objecth.h"            /* OBJECT */
#include "graphict.h"           /* LINELIST, etc. */
#include "matrix.h"             /* OMATRIX */
#include "tranState.h"          /* TranState */
#include "pathops.h"            /* STROKE_PARAMS */
#include "fonts.h"              /* NO_CHARCODE */
#include "gs_color.h"           /* COLORSPACE_ID */
#include "displayt.h"           /* Group */
#include "imaget.h"             /* IMAGEARGS */
#include "shadex.h"             /* SHADING* */

struct core_init_fns ; /* from SWcore */

/**
 * \defgroup hdlt HDLT - Harlequin Display List Technology.
 * \ingroup core
 */
/** \{ */

/** This enum lists of devices in IDLOM, with extra values to guarantee
 * that (1) we can make arrays with IT_NumTargets, (2) we can do loops
 * from IT_First to IT_NumTargets, and (3) IT_NoTarget is defined and
 * not in the above range, as an explicit "invalid" value.  Macros to
 * convert to and from name numbers are also here; the easy conversion
 * is saved for idlomClasses below.
 */
enum {
  IT_First=0, IT_Page=0,
  IT_Character,
  IT_UserPath,
  IT_Clip,
  IT_Pattern,
  IT_Form,
  IT_Group,
  IT_Text,
  IT_NumTargets,
  IT_NoTarget
} ;
typedef int32 idlomTargets ;

#define cvtNameToTarget( _name, _target ) MACRO_START       \
  switch ( _name ) {                                        \
  case NAME_Page:      ( _target ) = IT_Page      ; break ; \
  case NAME_Character: ( _target ) = IT_Character ; break ; \
  case NAME_UserPath:  ( _target ) = IT_UserPath  ; break ; \
  case NAME_Clip:      ( _target ) = IT_Clip      ; break ; \
  case NAME_Pattern:   ( _target ) = IT_Pattern   ; break ; \
  case NAME_Form:      ( _target ) = IT_Form      ; break ; \
  case NAME_Group:     ( _target ) = IT_Group     ; break ; \
  case NAME_Text:      ( _target ) = IT_Text      ; break ; \
  default:                                                  \
    HQTRACE((debug_idlom & IDLOM_DEBUG_PSERR) != 0, ( "invalid name in cvtNameToTarget" )) ; \
    ( _target ) = IT_NoTarget ;                             \
  }                                                         \
MACRO_END

#define cvtTargetToName( _target, _name ) MACRO_START         \
  int32 _nidx_ ;                                              \
  switch ( _target ) {                                        \
  case IT_Page :     _nidx_ = NAME_Page      ; break ;        \
  case IT_Character: _nidx_ = NAME_Character ; break ;        \
  case IT_UserPath:  _nidx_ = NAME_UserPath  ; break ;        \
  case IT_Clip:      _nidx_ = NAME_Clip      ; break ;        \
  case IT_Pattern:   _nidx_ = NAME_Pattern   ; break ;        \
  case IT_Form:      _nidx_ = NAME_Form      ; break ;        \
  case IT_Group:     _nidx_ = NAME_Group     ; break ;        \
  case IT_Text:      _nidx_ = NAME_Text      ; break ;        \
  default:                                                    \
    HQTRACE((debug_idlom & IDLOM_DEBUG_PSERR) != 0, ( "invalid target in cvtTargetToName" )) ; \
    _nidx_ = NAME_undefined ;                                 \
  }                                                           \
  (_name) = _nidx_ + system_names ;                           \
MACRO_END

/** This enum lists the classes available in IDLOM; like the above, it
 * it guarantees that IC_First and IC_NumClasses can be used for loops,
 * and IC_NoClass can be used as a "valid invalid value."  Note that
 * the conversion macros rely on the order here and in names.nam being
 * the same.
 */
enum {
  IC_First=0, IC_Fill=0,
  IC_Character,
  IC_Image,
  IC_Clip,
  IC_Rectangle,
  IC_Stroke,
  IC_Vignette,
  IC_Path,
  IC_Shfill,
  IC_Gouraud,
  IC_NumClasses,
  IC_NoClass
} ;
typedef int32 idlomClasses ;

#define cvtNameToClass( _name, class_ ) MACRO_START                   \
  int32 _class_ ;                                                     \
  _class_ = ( idlomClasses )( theINameNumber( _name ) - NAME_Fill ) ; \
  if ( _class_ < IC_First || _class_ >= IC_NumClasses ) {             \
    _class_ = IC_NoClass ;                                            \
    HQTRACE((debug_idlom & IDLOM_DEBUG_PSERR) != 0, ( "invalid name in cvtNameToClass" )) ;            \
  }                                                                   \
  ( class_ ) = _class_ ;                                              \
MACRO_END

#define cvtClassToName( _class, name_ ) MACRO_START                   \
  int32 _class_ = ( _class ) ;                                        \
  HQASSERT(_class_ >= IC_First && _class_ < IC_NumClasses,            \
           "invalid name in cvtClassToName") ;                        \
  ( name_ ) = ( NAMECACHE * )( system_names + NAME_Fill + _class_ ) ; \
MACRO_END


/** This enum identifies everything explicitly, as EOFILL_TYPE and
 * NZFILL_TYPE come from outside, but we need other values, too, and
 * need them to not conflict.
 */
enum {
  IR_Irrelevent = -1,
  IR_EOFill     = EOFILL_TYPE,
  IR_Fill       = NZFILL_TYPE,
  IR_Stroke     = 2,
  IR_IClip      = NZFILL_TYPE | CLIPINVERT
} ;
typedef int32 idlomRules ;

/** This enum indicates what cache devices want: CacheHit is either a
 * previously-known or an equivalence; CacheMiss is a "never before
 * seen" or a failure to take the character object,
 */
enum {
  IB_PSErr = FALSE,
  IB_CacheHit,
  IB_CacheMiss,
  IB_NoCache,
  IB_CacheEquiv,
  IB_MarkedNotBegun
} ;
typedef int32 idlomBeginCodes ;

enum { IH_Nothing, IH_Pending, IH_AddChar, IH_AddFill };
typedef int32 idlomHeldObjType ;

/** This structure declares a holding area for objects going to the
 * display list, but held up pending an IDLOM callback.  It's used
 * for characters, to both add the first one and extend it for later
 * ones, which accounts for the union.
 */
typedef struct {
   int32 tag ;                  /* what kind of add is held? */
   SYSTEMVALUE x, y ;
   SYSTEMVALUE xtrans, ytrans ; /* Font matrix translation */
   union {
     struct {                   /* args to addchardisplay */
       FORM *form ;
       SYSTEMVALUE xbear, ybear ;
      } addchar ;
     struct {                   /* args to addnbressdisplay */
       int32 rule ;
       NFILLOBJECT *nfill ;
      } addnbress ;
   } args ;
} heldAddToDL ;


typedef struct idlomArgs IDLOMARGS ;

/** Args given to HDLT calls. */
struct idlomArgs {
  int32 firstClass ;              /**< actual class of object. */
  int32 thisClass ;               /**< coerced class of object. */
  int32 rule ;                    /**< fill rule (for fill, stroke and clip). */
  PATHINFO path ;                 /**< path, included in pen below. */
  STROKE_PARAMS pen ;             /**< pen.path = path, maybe only field used. */
  OMATRIX pathmatrix ;            /**< matrix for a stroke adjustment. */
  uint8 pathmatUsed ;             /**< whether the pathmatrix entry is relevant. */
  FPOINT coord ;                  /**< a single coordinate. */
  Bool isCached ;                 /**< bit for whether char, upath in cache. */
  int32 charCode ;                /**< character ID codes, or NO_CHARCODE (-1). */
  OBJECT *charName ;              /**< name of character object. */
  int32 cacheId ;                 /**< cache entry if isCached is TRUE. */
  OMATRIX convert ;               /**< font, image, upath adjustments. */
  int8 vignKind ;                 /**< kind of vignette (if firstClass=vignette). */
  int8 vignCurve ;                /**< kind of vignette (if firstClass=vignette). */

  IDLOMARGS *subObjs ;            /**< list of subobjects. */
  int32 numSubObjs ;              /**< number of subobjects. */

  /* args extracted from gstate */
  OMATRIX origctm ;               /**< Original target's CTM. */
  OMATRIX origdevctm ;            /**< Original target's default CTM. */
  OMATRIX proxyctm ;              /**< Proxy target's CTM. */
  OMATRIX proxydevctm ;           /**< Proxy target's default CTM. */
  SYSTEMVALUE torig[2] ;          /**< Translation from first to absolute. */
  SYSTEMVALUE tproxy[2] ;         /**< Translation from proxy to absolute. */
  SYSTEMVALUE offset[2] ;         /**< Offset to reported proxy position. */
  SYSTEMVALUE position[2] ;       /**< Position of character. */
  int32 protectFont ;
  OBJECT fontdict;
  int32 cacheOp ;
  int32 showCount ;
  TranState transparency ;        /**< Transparency state. */

  CLIPRECORD *cliprec ;

  /* Shaded fill-specific stuff */
  SHADINGinfo *shadingInfo ;
  OBJECT *shadingDict ;

  /* General color stuff */
  GS_COLORinfo *colorInfo ;       /**< our copy of the current color info. */
  int32 colorType ;               /**< what color chain do we use? */

  /* Input color */
  int32 n_iColorants ;            /**< number of colorants. */
  USERVALUE *iColorValues ;       /**< array of color values. */
  COLORSPACE_ID iColorSpace ;     /**< color space. */
  Bool fPromotedSpace ;           /**< have we promoted color space. */
  Bool fExplicitOverprint ;       /**< did input color use explicit overprints? */

  /* Output color */
  int32 n_oColorants ;            /**< number of colorants. */
  USERVALUE *oColorValues ;       /**< array of color values. */
  COLORANTINDEX *oColorants ;     /**< array of colorants. */
  Bool fOverprinting ;            /**< are we currently overprinting? */
} ;

void hdlt_C_globals(struct core_init_fns *fns) ;

/* idlom_beginPage() is implicit with first object */
Bool idlom_lookupPattern( int32 swPatId, int32 *idlomCacheId ) ;
int32 idlom_beginPattern( OBJECT *pattern, int32 swPatId ) ;

Bool idlom_lookupForm(int32 formId, int32 *idlomCacheId) ;
int32 idlom_beginForm(OBJECT *form, int32 swFormId) ;

Bool idlom_lookupGroup(uint32 groupId, int32 *idlomCacheId) ;
Bool idlom_beginGroup(uint32 groupId, Group *group) ;

Bool idlom_beginText(int32 opname) ;

int32 idlom_markBeginCharacter(int32 ch, OBJECT *glyph, FONTinfo *font,
                               int32 *cacheId) ;
int32 idlom_doBeginCharacter( int32 op ) ;
int32 idlom_newUserPath( STROKE_PARAMS *pen, OBJECT *theo, int32 rule,
                         OMATRIX *matrix, int32 *cached, int32 *cacheId ) ;
int32 idlom_newClipPath( PATHINFO *path, int32 fillrule, int32 *cacheOk,
                         int32 *cacheId ) ;
Bool idlom_cacheClip(PATHINFO *path, int32 cacheId) ;
void  idlom_unCacheClip( PATHINFO *path ) ;
void  idlom_unCacheChar(int32 fontid,
                        /*@notnull@*/ const OMATRIX *fontmatrix,
                        /*@notnull@*/ const OBJECT *glyph) ;
void  idlom_maybeFlushAll( void ) ;

Bool idlom_endTarget(int32 target, int32 nameOp, Bool success) ;

int32 idlom_objectCB( int32 colorType, int32 firstClass, int32 thisClass,
                      PATHINFO *path, STROKE_PARAMS *pen, int32 rule,
                      Bool isCached, int32 cacheId,
                      OBJECT *charName, int32 charCode,
                      HDLTinfo *hdltInfo,
                      SHADINGinfo *shadingInfo, OBJECT *shadingDict,
                      OMATRIX *convert, OMATRIX *pathmatrix) ;

/** idlom_latchCB is used to cache the args and gstate into an idlomArgs struct;
 * it replaces idlom_objectCB, and the actual call is made later, with
 * idlom_doCB.
 */
IDLOMARGS *idlom_latchCB( int32 colorType, int32 firstClass, int32 thisClass,
                          PATHINFO *path, STROKE_PARAMS *pen, int32 rule,
                          HDLTinfo *hdltInfo, IDLOMARGS *subObjs) ;

int32 idlom_doCB( IDLOMARGS *args ) ;
int32 idlom_vignette( int32 colorType, int8 kind, int8 curve,
                      IDLOMARGS *subArgs, CLIPPATH *clippath ) ;

int32 idlom_gouraud( SHADINGvertex *v0, SHADINGvertex *v1,
                     SHADINGvertex *v2, SHADINGinfo *sinfo) ;

void freeIdlomArgs( IDLOMARGS *fill ) ;

int32 idlom_beginImage(int32 colorType ,
                       IMAGEARGS *imageargs ,
                       Bool colorconverted ,
                       int32 oncomps ,
                       Bool out16 ,
                       int32 **idecodes) ;
int32 idlom_endImage( int32 colorType , IMAGEARGS *imageargs ) ;
Bool idlom_imageData(int32 colorType , IMAGEARGS *imageargs ,
                     int32 nInSrcs , uint8 *inbuf[] , int32 inlen ,
                     int32 nOutSrcs , uint8 *outbuf , int32 outlen ,
                     Bool out16Bit ) ;

/* performance-opt. macros for the entry points */
#define IDLOM_MARKBEGINCHARACTER(ch,name,font,cache)                    \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_markBeginCharacter((ch),(name),(font),(cache)) :               \
   IB_CacheHit)

#define IDLOM_DOBEGINCHARACTER(op) \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_doBeginCharacter(op) : \
    IB_CacheHit)

#define IDLOM_ENDCHARACTER(success,cache) \
   (isHDLTEnabled(*gstateptr) ? \
    *(cache) = gstateptr->theHDLTinfo.cacheID, /* note comma */\
    idlom_endTarget(IT_Character,gstateptr->theHDLTinfo.cacheOp,(success)) : \
    TRUE)

#define IDLOM_ENDPAGE(op) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_endTarget(IT_Page,(op),TRUE)) : \
    (TRUE))

#define IDLOM_PATTERNLOOKUP(swid) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_lookupPattern((swid), NULL)) : \
    (IB_CacheHit))

#define IDLOM_BEGINPATTERN(pat, id) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_beginPattern((pat), (id))) : \
    (IB_CacheHit))

#define IDLOM_ENDPATTERN(success) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_endTarget(IT_Pattern,NAME_undefined,(success))) : \
    (TRUE))

#define IDLOM_FORMLOOKUP(swid) \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_lookupForm((swid), NULL) : \
    IB_CacheHit)

#define IDLOM_BEGINFORM(form, id) \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_beginForm((form), (id)) : \
    IB_CacheHit)

#define IDLOM_ENDFORM(success) \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_endTarget(IT_Form, NAME_undefined, (success)) : \
    TRUE)

#define IDLOM_BEGINGROUP(id, group) \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_beginGroup(id, group) : \
    TRUE)

#define IDLOM_ENDGROUP(success) \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_endTarget(IT_Group, NAME_undefined, (success)) : \
    TRUE)

#define IDLOM_BEGINTEXT(op) \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_beginText(op) : \
    TRUE)

#define IDLOM_ENDTEXT(op, success) \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_endTarget(IT_Text, op, (success)) : \
    TRUE)

#define IDLOM_DO_LATCHED(args) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_doCB((args))) : \
    (NAME_Add))

#define IDLOM_FILL(colorType,rule,path,matrix)                          \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_objectCB((colorType), IC_Fill, IC_Fill,                        \
                  (path), NULL /*pen*/, (rule),                         \
                  FALSE /*isCached*/, 0 /*cacheId*/,                    \
                  NULL /*charName*/, NO_CHARCODE,                       \
                  &gstateptr->theHDLTinfo,                              \
                  NULL /*shadingInfo*/, NULL /*shadingDict*/,           \
                  (matrix), NULL /*pathMatrix*/) :     \
    NAME_Add)

#define IDLOM_LATCH_FILL(colorType,rule,path,subObjs)                   \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_latchCB((colorType), IC_Fill, IC_Fill,                         \
                 (path), NULL /*pen*/, (rule),                          \
                 &gstateptr->theHDLTinfo, (subObjs)) :                  \
   NULL)

#define IDLOM_CACHED_FILL(colorType,rule,path,obj,cid)                  \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_objectCB((colorType), IC_Fill, IC_Fill,                        \
                  (path), NULL /*pen*/, (rule),                         \
                  TRUE /*isCached*/, (cid),                             \
                  (obj), NO_CHARCODE,                                   \
                  &gstateptr->theHDLTinfo,                              \
                  NULL /*shadingInfo*/, NULL /*shadingDict*/,           \
                  NULL /*convert*/, NULL /*subObjs*/) :                 \
    NAME_Add)

#define IDLOM_STROKE(colorType,params,matrix)                           \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_objectCB((colorType), IC_Stroke, IC_Stroke,                    \
                  thePathInfo(*(params)), (params), IR_Stroke,          \
                  FALSE /*isCached*/, 0 /*cacheId*/,                    \
                  NULL /*charName*/, NO_CHARCODE,                       \
                  &gstateptr->theHDLTinfo,                              \
                  NULL /*shadingInfo*/, NULL /*shadingDict*/,           \
                  (matrix), NULL /*pathMatrix*/) :                      \
    NAME_Add)

#define IDLOM_LATCH_STROKE(colorType,params,subObjs)                    \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_latchCB((colorType), IC_Stroke, IC_Stroke,                     \
                 thePathInfo(*(params)), (params), IR_Stroke,           \
                 &gstateptr->theHDLTinfo, (subObjs)) :                  \
   NULL)

#define IDLOM_CACHED_STROKE(colorType,params,matrix,obj,cid)            \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_objectCB((colorType), IC_Stroke, IC_Stroke,                    \
                  thePathInfo(*(params)), (params), IR_Stroke,          \
                  TRUE /*isCached*/, (cid),                             \
                  (obj), NO_CHARCODE,                                   \
                  &gstateptr->theHDLTinfo,                              \
                  NULL /*shadingInfo*/, NULL /*shadingDict*/,           \
                  NULL /*convert*/, (matrix)) :                         \
    NAME_Add)

#define IDLOM_PATH(params,rule,matrix)                                  \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_objectCB(GSC_UNDEFINED, IC_Path, IC_Path,                      \
                  thePathInfo(*(params)), (params), (rule),             \
                  FALSE /*isCached*/, 0 /*cacheId*/,                    \
                  NULL /*charName*/, NO_CHARCODE,                       \
                  &gstateptr->theHDLTinfo,                              \
                  NULL /*shadingInfo*/, NULL /*shadingDict*/,           \
                  (matrix), NULL /*pathMatrix*/) :                      \
   NAME_Add)

#define IDLOM_CHARACTER(cid,chname,chcode)                              \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_objectCB(GSC_FILL, IC_Character, IC_Character,                 \
                  NULL /*path*/, NULL /*pen*/, IR_Irrelevent,           \
                  TRUE /*isCached*/, (cid),                             \
                  (chname), (chcode),                                   \
                  &gstateptr->theHDLTinfo,                              \
                  NULL /*shadingInfo*/, NULL /*shadingDict*/,           \
                  NULL /*convert*/, NULL /*pathmatrix*/) :              \
    NAME_Add)

#define IDLOM_VIGNETTE(colorType,kind,curve,sublist,clippath) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_vignette((colorType),(kind),(curve),(sublist),(clippath))) : \
    (NAME_Add))

#define IDLOM_RECTANGLE(colorType, path, rule)                          \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_objectCB((colorType), IC_Fill, IC_Rectangle,                   \
                  (path), NULL /*pen*/, (rule),                         \
                  FALSE /*isCached*/, 0 /*cacheId*/,                    \
                  NULL /*charName*/, NO_CHARCODE,                       \
                  &gstateptr->theHDLTinfo,                              \
                  NULL /*shadingInfo*/, NULL /*shadingDict*/,           \
                  NULL /*convert*/, NULL /*pathMatrix*/) :              \
   NAME_Add)


#define IDLOM_SHFILL(colorType, shadinginfo, shadingdict)               \
  (isHDLTEnabled(*gstateptr) ?                                          \
   idlom_objectCB((colorType), IC_Shfill, IC_Shfill,                    \
                  NULL /*path*/, NULL /*pen*/, IR_Irrelevent,           \
                  FALSE /*isCached*/, 0 /*cacheId*/,                    \
                  NULL /*charName*/, NO_CHARCODE,                       \
                  &gstateptr->theHDLTinfo,                              \
                  (shadinginfo), (shadingdict),                         \
                  NULL /*convert*/, NULL /*pathMatrix*/) :              \
   NAME_Add)

#define IDLOM_GOURAUD(_v0, _v1, _v2, _sinfo) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_gouraud((_v0), (_v1), (_v2), (_sinfo))) : \
    (NAME_Add))

#define IDLOM_NEWCLIP(newclip,cached,cacheId)                           \
  (isHDLTEnabled(*gstateptr) ?                                          \
    idlom_objectCB(GSC_UNDEFINED, IC_Clip, IC_Clip,                     \
                   &theClipPath(*(newclip)), NULL /*pen*/, theClipType(*(newclip)), \
                   (cached), (cacheId),                                 \
                   NULL /*charName*/, NO_CHARCODE,                      \
                   &gstateptr->theHDLTinfo,                             \
                   NULL /*shadingInfo*/, NULL /*shadingDict*/,          \
                   NULL /*convert*/, NULL /*pathmatrix*/) :             \
   NAME_Add)

#define IDLOM_CLIP() \
   (isHDLTEnabled(*gstateptr) ? \
    idlom_objectCB(GSC_UNDEFINED, IC_Clip, IC_Clip,                     \
                   NULL /*path*/, NULL /*pen*/, IR_Irrelevent,          \
                   FALSE /*isCached*/, 0 /*cacheId*/,                   \
                   NULL /*charName*/, NO_CHARCODE,                      \
                   &gstateptr->theHDLTinfo,                             \
                   NULL /*shadingInfo*/, NULL /*shadingDict*/,          \
                   NULL /*convert*/, NULL /*pathMatrix*/) :             \
    NAME_Add)

/* Clips have some special handling, too, for the clip target... */
#define IDLOM_NEWCLIPPATH(path,rule,cacheFlag,id) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_newClipPath((path), (rule), (cacheFlag), (id))) : \
    (NAME_Add))

#define IDLOM_NEWUSERPATH(path,theo,rule,matrix,cacheFlag,id) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_newUserPath((path), (theo), (rule), (matrix), (cacheFlag), (id))) : \
    (NAME_Add))

#define IDLOM_CACHECLIP(path,id) \
   ((isHDLTEnabled( *gstateptr )) ? (idlom_cacheClip((path),(id))) : TRUE)

#define IDLOM_UNCACHECLIP(path) MACRO_START \
   if (isHDLTEnabled( *gstateptr )) \
     idlom_unCacheClip((path)); \
   MACRO_END


/* Images, unfortunately, are also special, and need several entry points (and
 * might as well have their own entry points for real, then).
 */
#define IDLOM_BEGINIMAGE(colorType, imageargs, colorconverted, oncomps, \
                         out16, idecodes ) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_beginImage((colorType),(imageargs),(colorconverted), \
                      (oncomps),(out16),(idecodes))) : \
    NAME_Add)

#define IDLOM_ENDIMAGE(colorType, image) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_endImage((colorType),(image))) : \
    NAME_Add)

#define IDLOM_IMAGEDATA(colorType, imageargs, incomps, in, inlen, outcomps, \
                        out, outlen, out16) \
   ((isHDLTEnabled( *gstateptr )) ? \
    (idlom_imageData((colorType),(imageargs), \
                     (incomps),(in),(inlen),(outcomps),(out),(outlen), \
                     (out16))) : \
    TRUE)

/** \} */

#endif    /* ! __IDLOM_H__ */


/* Log stripped */
