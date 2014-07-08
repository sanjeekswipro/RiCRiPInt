/** \file
 * \ingroup hdlt
 *
 * $HopeName: COREhdlt!src:nohdlt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for HDLT functions when compiled out.
 */


#include "core.h"
#include "swerrors.h"
#include "swstart.h"
#include "objecth.h"
#include "pathops.h"
#include "graphict.h"
#include "images.h"
#include "shadex.h"
#include "idlom.h"
#include "namedef_.h"

void hdlt_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  /* Nothing to do */
}

/* idlom_beginPage() is implicit with first object */
Bool idlom_lookupPattern(int32 swPatId, int32 *idlomCacheId)
{
  UNUSED_PARAM(int32, swPatId) ;
  UNUSED_PARAM(int32 *, idlomCacheId) ;
  return FALSE ;
}

int32 idlom_beginPattern(OBJECT *pattern, int32 swPatId)
{
  UNUSED_PARAM(OBJECT *, pattern) ;
  UNUSED_PARAM(int32, swPatId) ;
  return IB_CacheHit ;
}


Bool idlom_lookupForm(int32 formId, int32 *idlomCacheId)
{
  UNUSED_PARAM(int32, formId) ;
  UNUSED_PARAM(int32 *, idlomCacheId) ;
  return FALSE ;
}

int32 idlom_beginForm(OBJECT *form, int32 swFormId)
{
  UNUSED_PARAM(OBJECT *, form) ;
  UNUSED_PARAM(int32, swFormId) ;
  return IB_CacheHit ;
}


Bool idlom_lookupGroup(uint32 groupId, int32 *idlomCacheId)
{
  UNUSED_PARAM(uint32, groupId) ;
  UNUSED_PARAM(int32 *, idlomCacheId) ;
  return FALSE ;
}

Bool idlom_beginGroup(uint32 groupId, Group *group)
{
  UNUSED_PARAM(Group *, group) ;
  UNUSED_PARAM(uint32, groupId) ;
  return TRUE ;
}


Bool idlom_beginText(int32 opname)
{
  UNUSED_PARAM(int32, opname) ;
  return TRUE ;
}


int32 idlom_markBeginCharacter(int32 ch, OBJECT *glyph, FONTinfo *font,
                               int32 *cacheId)
{
  UNUSED_PARAM(int32, ch) ;
  UNUSED_PARAM(OBJECT *, glyph) ;
  UNUSED_PARAM(FONTinfo *, font) ;
  UNUSED_PARAM(int32 *, cacheId) ;
  return IB_CacheHit ;
}

int32 idlom_doBeginCharacter(int32 op)
{
  UNUSED_PARAM(int32, op) ;
  return IB_CacheHit ;
}

int32 idlom_newUserPath(STROKE_PARAMS *pen, OBJECT *theo, int32 rule,
                        OMATRIX *matrix, int32 *cached, int32 *cacheId)
{
  UNUSED_PARAM(STROKE_PARAMS *, pen) ;
  UNUSED_PARAM(OBJECT *, theo) ;
  UNUSED_PARAM(int32, rule) ;
  UNUSED_PARAM(OMATRIX *, matrix) ;
  UNUSED_PARAM(int32 *, cached) ;
  UNUSED_PARAM(int32 *, cacheId) ;
  return NAME_Add ;
}

int32 idlom_newClipPath(PATHINFO *path, int32 fillrule, int32 *cacheOk,
                        int32 *cacheId)
{
  UNUSED_PARAM(PATHINFO *, path) ;
  UNUSED_PARAM(int32, fillrule) ;
  UNUSED_PARAM(int32 *, cacheOk) ;
  UNUSED_PARAM(int32 *, cacheId) ;
  return NAME_Add ;
}

Bool idlom_cacheClip(PATHINFO *path, int32 cacheId)
{
  UNUSED_PARAM(PATHINFO *, path) ;
  UNUSED_PARAM(int32, cacheId) ;
  return TRUE ;
}

void idlom_unCacheClip(PATHINFO *path)
{
  UNUSED_PARAM(PATHINFO *, path) ;
}

void idlom_unCacheChar(int32 fontid,
                       const OMATRIX *fontmatrix,
                       const OBJECT *glyph)
{
  UNUSED_PARAM(int32, fontid) ;
  UNUSED_PARAM(const OMATRIX *, fontmatrix) ;
  UNUSED_PARAM(const OBJECT *, glyph) ;
}

void idlom_maybeFlushAll(void)
{
  /* Nothing to do */
}


Bool idlom_endTarget(int32 target, int32 nameOp, Bool success)
{
  UNUSED_PARAM(int32, target) ;
  UNUSED_PARAM(int32, nameOp) ;
  return success ;
}


int32 idlom_objectCB(int32 colorType, int32 firstClass, int32 thisClass,
                     PATHINFO *path, STROKE_PARAMS *pen, int32 rule,
                     Bool isCached, int32 cacheId,
                     OBJECT *charName, int32 charCode,
                     HDLTinfo *hdltInfo,
                     SHADINGinfo *shadingInfo, OBJECT *shadingDict,
                     OMATRIX *convert, OMATRIX *pathmatrix)
{
  UNUSED_PARAM(int32, colorType) ;
  UNUSED_PARAM(int32, firstClass) ;
  UNUSED_PARAM(int32, thisClass) ;
  UNUSED_PARAM(PATHINFO *, path) ;
  UNUSED_PARAM(STROKE_PARAMS *, pen) ;
  UNUSED_PARAM(int32, rule) ;
  UNUSED_PARAM(Bool, isCached) ;
  UNUSED_PARAM(int32, cacheId) ;
  UNUSED_PARAM(OBJECT *, charName) ;
  UNUSED_PARAM(int32, charCode) ;
  UNUSED_PARAM(HDLTinfo *, hdltInfo) ;
  UNUSED_PARAM(SHADINGinfo *, shadingInfo) ;
  UNUSED_PARAM(OBJECT *, shadingDict) ;
  UNUSED_PARAM(OMATRIX *, convert) ;
  UNUSED_PARAM(OMATRIX *, pathmatrix) ;
  return NAME_Add ;
}

IDLOMARGS *idlom_latchCB(int32 colorType, int32 firstClass, int32 thisClass,
                         PATHINFO *path, STROKE_PARAMS *pen, int32 rule,
                         HDLTinfo *hdltInfo, IDLOMARGS *subObjs)
{
  UNUSED_PARAM(int32, colorType) ;
  UNUSED_PARAM(int32, firstClass) ;
  UNUSED_PARAM(int32, thisClass) ;
  UNUSED_PARAM(PATHINFO *, path) ;
  UNUSED_PARAM(STROKE_PARAMS *, pen) ;
  UNUSED_PARAM(int32, rule) ;
  UNUSED_PARAM(HDLTinfo *, hdltInfo) ;
  UNUSED_PARAM(IDLOMARGS *, subObjs) ;
  return NULL ;
}


int32 idlom_doCB(IDLOMARGS *args)
{
  UNUSED_PARAM(IDLOMARGS *, args) ;
  return NAME_Add ;
}

int32 idlom_vignette(int32 colorType, int8 kind, int8 curve,
                     IDLOMARGS *subArgs, CLIPPATH *clippath)
{
  UNUSED_PARAM(int32, colorType) ;
  UNUSED_PARAM(int8, kind) ;
  UNUSED_PARAM(int8, curve) ;
  UNUSED_PARAM(IDLOMARGS *, subArgs) ;
  UNUSED_PARAM(CLIPPATH *, clippath) ;
  return NAME_Add ;
}


int32 idlom_gouraud(SHADINGvertex *v0, SHADINGvertex *v1,
                    SHADINGvertex *v2, SHADINGinfo *sinfo)
{
  UNUSED_PARAM(SHADINGvertex *, v0) ;
  UNUSED_PARAM(SHADINGvertex *, v1) ;
  UNUSED_PARAM(SHADINGvertex *, v2) ;
  UNUSED_PARAM(SHADINGinfo *, sinfo) ;
  return NAME_Add ;
}


void freeIdlomArgs(IDLOMARGS *fill)
{
  UNUSED_PARAM(IDLOMARGS *, fill) ;
}


Bool idlom_beginImage(int32 colorType,
                      IMAGEARGS *imageargs,
                      int32 colorconverted,
                      int32 oncomps,
                      Bool out16,
                      int32 **idecodes)
{
  UNUSED_PARAM(int32, colorType) ;
  UNUSED_PARAM(IMAGEARGS *, imageargs) ;
  UNUSED_PARAM(int32, colorconverted) ;
  UNUSED_PARAM(int32, oncomps) ;
  UNUSED_PARAM(Bool, out16) ;
  UNUSED_PARAM(int32 **, idecodes) ;
  return TRUE ;
}

int32 idlom_endImage(int32 colorType, struct IMAGEARGS *imageargs)
{
  UNUSED_PARAM(int32, colorType) ;
  UNUSED_PARAM(IMAGEARGS *, imageargs) ;
  return NAME_Add ;
}

Bool idlom_imageData(int32 colorType, struct IMAGEARGS *imageargs,
                     int32 nInSrcs, uint8 *inbuf[], int32 inlen,
                     int32 nOutSrcs, uint8 *outbuf, int32 outlen,
                     Bool out16Bit)
{
  UNUSED_PARAM(int32, colorType) ;
  UNUSED_PARAM(IMAGEARGS *, imageargs) ;
  UNUSED_PARAM(int32, nInSrcs) ;
  UNUSED_PARAM(uint8 **, inbuf) ;
  UNUSED_PARAM(int32, inlen) ;
  UNUSED_PARAM(int32, nOutSrcs) ;
  UNUSED_PARAM(uint8 *, outbuf) ;
  UNUSED_PARAM(int32, outlen) ;
  UNUSED_PARAM(Bool, out16Bit) ;
  return TRUE ;
}

/* Stub for compressed startup PostScript. First two characters are the
   length of the following string. */
uint8 idlompss[2] = {
  0x00, 0x00,
} ;

/* Log stripped */
