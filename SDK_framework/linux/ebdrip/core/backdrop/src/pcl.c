/** \file
 * \ingroup backdrop
 *
 * $HopeName: COREbackdrop!src:pcl.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL uses backdrop to handle PCL ROPs that involve destination.
 */

#include "core.h"
#include "backdroppriv.h"
#include "pcl.h"
#include "composite.h"
#include "compositecolor.h"
#include "gschead.h"
#include "rop.h"
#include "display.h"
#include "swerrors.h"

/**
 * Pack a single Gray COLORVALUE into a single RGB word.
 */
#define PCL_PACK_GRAY(cv_) \
  ((PclPackedColor)(((cv_)[0] >> 8) | ((cv_)[0] & 0xff00) | (((cv_)[0] & 0xff00) << 8)))

/**
 * Unpack a RGB word representing a gray value into a COLORVALUE array. We're
 * arbitrarily picking one channel, they should be all the same.
 */
#define PCL_UNPACK_GRAY(gray_, cv_) MACRO_START \
  (cv_)[0] = (COLORVALUE)((gray_) & 0xff00); \
MACRO_END

struct PclBDState {
  /* The Pcl5Attrib structure for the current object. */
  PclAttrib* attrib;

  /* Copied from attrib for convenience. */
  PclDLPattern* dlPattern;

  /* Source/pattern transparency. */
  Bool sourceTransparent, patternTransparent;

  /* Backdrop for PCL can be DeviceGray (1 channel), DeviceRGB (3 channels),
     or CMYK (4 channels). */
  uint32 nComps;

  /* Packed versions of black and white, will be in RGB or CMYK as required. */
  PclPackedColor black, white;

  /* The packed RGB foreground color. */
  PclPackedColor packedForeground;

  /* True if a pattern should be applied. */
  Bool patterned;

  /* True if the pattern is a color pattern. */
  Bool colorPattern;

  /* The packed RGB colors. */
  uint32 packedSource, packedPattern, packedTexture;

  /* Pattern iterator; used to obtain pattern data. */
  PclDLPatternIterator patternIterator;

  /* Refers to the next xi to be loaded - used in pcl_loadRun. */
  uint32 xi;
};

Bool pcl_stateNew(uint32 inCompsMax, mm_cost_t cost, PclBDState **newPcl)
{
  PclBDState *pcl;

  if ( !pclGstateIsEnabled() ) {
    *newPcl = NULL;
    return TRUE;
  }

  pcl = bd_resourceAllocCost(sizeof(PclBDState), cost);
  if ( pcl == NULL )
    return error_handler(VMERROR);

  pcl->attrib = NULL;
  pcl->patterned = FALSE;

  HQASSERT(inCompsMax == 1 || inCompsMax == 3 || inCompsMax == 4,
           "Unexpected PCL VirtualDeviceSpace") ;
  pcl->nComps = inCompsMax ;

  if ( inCompsMax == 4 ) {
    /* CMYK */
    pcl->black = PCL_PACKED_CMYK_BLACK;
    pcl->white = PCL_PACKED_CMYK_WHITE;
  } else {
    /* Gray or RGB */
    pcl->black = PCL_PACKED_RGB_BLACK;
    pcl->white = PCL_PACKED_RGB_WHITE;
  }

  *newPcl = pcl;
  return TRUE;
}

void pcl_stateFree(PclBDState **freePcl)
{
  if ( *freePcl != NULL ) {
    PclBDState *pcl = *freePcl;

    bd_resourceFree(pcl, sizeof(PclBDState));

    *freePcl = NULL;
  }
}

/**
 * Configure PCL run information.
 */
void pcl_runInfo(PclBDState *pcl, PclAttrib *attrib)
{
  pcl->attrib = attrib;
  pcl->patterned = FALSE;

  if ( attrib ) {
    HQASSERT(attrib->cacheEntry.pointer.pointer != &invalidPattern,
             "An invalidPattern object should never be seen here");
    pcl->sourceTransparent = attrib->sourceTransparent;
    pcl->patternTransparent = attrib->patternTransparent;

    /* Pack the foreground color now if it's not provided by the object color. */
    if ( attrib->foregroundSource != PCL_DL_COLOR_IS_FOREGROUND ) {
      HQASSERT(attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB,
               "Bad value for foregroundSource.");
      pcl->packedForeground = attrib->foreground;
    }

    if ( attrib->patternColors != PCL_PATTERN_NONE ) {
      pcl->patterned = TRUE;
      pcl->dlPattern = attrib->dlPattern ;
      pcl->colorPattern =
          (attrib->patternColors != PCL_PATTERN_BLACK_AND_WHITE &&
           attrib->patternColors != PCL_PATTERN_OTHER_AND_WHITE) ;
    }
  }
}

Bool pcl_hasAttrib(PclBDState *pcl)
{
  return pcl != NULL && pcl->attrib != NULL;
}

Bool pcl_patternActive(PclBDState *pcl)
{
  return pcl != NULL && pcl->patterned;
}

void pcl_sourceColor(CompositeContext *context, Bool *opaque)
{
  PclBDState *pcl = context->pcl;
  uint32 bdsrc ;

  bd_checkColor(context->source.color, context->source.alpha,
                pcl->nComps, FALSE);

  switch ( pcl->nComps ) {
  default:
    HQFAIL("Unexpected number of components in PCL VirtualDeviceSpace") ;
    /*@fallthrough@*/
  case 1:
    bdsrc = PCL_PACK_GRAY(context->source.color);
    break ;
  case 3:
    bdsrc = PCL_PACK_RGB(context->source.color);
    break ;
  case 4:
    bdsrc = PCL_PACK_CMYK(context->source.color);
    break ;
  }

  if ( pcl->attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
    /* If the source color is actually foreground, the source color is black. */
    pcl->packedForeground = bdsrc ;
    pcl->packedSource = pcl->black;
  }
  else {
    /* Foreground already set in bd_runInfo(). */
    pcl->packedSource = bdsrc ;
  }
  if ( !pcl->patterned )
    pcl->packedTexture = pcl->packedForeground;

  /* Considered opaque if the ROP doesn't involve the destination and the source
     and pattern are opaque. */
  *opaque = (!pclROPRequiresDestination(pcl->attrib->rop) &&
             !pcl->attrib->sourceTransparent &&
             !pcl->attrib->patternTransparent);
}

void pcl_setPattern(PclBDState *pcl, dcoord spanX, dcoord spanY)
{
  pclDLPatternIteratorStart(&pcl->patternIterator, pcl->dlPattern,
                            spanX, spanY, BLOCK_DEFAULT_WIDTH);
  pcl->xi = 0;
}

uint32 pcl_loadRun(PclBDState *pcl, uint32 xi, uint32 runLen)
{
  PclDLPatternIterator *iterator = &pcl->patternIterator;
  uint32 run;

  /* This assumes xi goes left to right and if this isn't the case bd_setLine is
     called to restart. */
  if ( xi >= pcl->xi ) {
    for (;;) {
      int32 skip = xi - pcl->xi ;
      if ( skip >= iterator->cspan ) {
        pcl->xi += iterator->cspan ;
        pclDLPatternIteratorNext(iterator, xi + runLen - pcl->xi) ;
      } else {
        pcl->xi += skip ;
        iterator->cspan -= skip ;
        break ;
      }
    }

    pcl->packedPattern = iterator->color.packed;
    if ( pcl->colorPattern )
      pcl->packedTexture = pcl->packedPattern;
    else
      pcl->packedTexture = pcl->packedPattern | pcl->packedForeground;
  }

  run = (uint32)iterator->cspan;
  HQASSERT(run > 0, "Can't have a zero length run");
  if ( run > runLen )
    run = runLen;

  return run;
}

/**
 * Composite the source and background color using the PCL rendering model.
 * \return true if the result color is to be drawn and false if to be ignored.
 */
Bool pcl_compositeColor(CompositeContext *context, const Backdrop *backdrop)
{
  PclBDState *pcl = context->pcl;
  Bool sourceWhite = (pcl->packedSource == pcl->white);
  uint32 result;
  GSC_BLACK_TYPE oldBlackType;

  UNUSED_PARAM(const Backdrop*, backdrop);

  if ( pcl->sourceTransparent && sourceWhite )
    return FALSE; /* ignore */

  if ( pcl->patternTransparent &&
       pcl->packedTexture == pcl->white && !sourceWhite )
    return FALSE; /* ignore */

  switch ( pcl->nComps ) {
  default:
    HQFAIL("Unexpected number of components in PCL VirtualDeviceSpace") ;
    /*@fallthrough@*/
  case 1:
    result = rop(pcl->packedSource, pcl->packedTexture,
                 PCL_PACK_GRAY(context->background.color),
                 pcl->attrib->rop);
    PCL_UNPACK_GRAY(result, context->result.color);
    break ;
  case 3:
    result = rop(pcl->packedSource, pcl->packedTexture,
                 PCL_PACK_RGB(context->background.color),
                 pcl->attrib->rop);
    PCL_UNPACK_RGB(result, context->result.color);
    break ;
  case 4:
    result = rop(pcl->packedSource, pcl->packedTexture,
                 PCL_PACK_CMYK(context->background.color),
                 pcl->attrib->rop);
    PCL_UNPACK_CMYK(result, context->result.color);
    break ;
  }

  HQASSERT(context->source.alpha == COLORVALUE_ONE,
           "Expected PCL source alpha to be one");
  *context->result.alpha = COLORVALUE_ONE;
  *context->result.info = *context->source.info;

  /* When ropping, we defer black preservation */
  oldBlackType = COLORINFO_BLACK_TYPE(context->result.info->lcmAttribs);
  if (oldBlackType == BLACK_TYPE_100_PC || oldBlackType == BLACK_TYPE_TINT)
    COLORINFO_SET_BLACK_TYPE(context->result.info->lcmAttribs, BLACK_TYPE_MODIFIED);

  bd_checkColor(context->result.color, *context->result.alpha,
                pcl->nComps, TRUE);
  COLORTYPE_ASSERT(context->result.info->colorType, "pcl_compositeColor");
  HQASSERT(context->result.info->spotNo > 0, "Invalid spotNo");

  return TRUE; /* draw */
}

/* Log stripped */
