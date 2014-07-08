/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:pclAttrib.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#include "core.h"
#include "pclAttrib.h"
#include "pclAttribTypes.h"
#include "pclGstate.h"

#include "dl_store.h"
#include "dl_bres.h"
#include "dl_purge.h"
#include "swerrors.h"
#include "display.h"
#include "graphics.h"
#include "gstate.h"
#include "hqmemcpy.h"
#include "mathfunc.h"
#include "jobmetrics.h"
#include "gu_chan.h"
#include "plotops.h"
#include "imageo.h"
#include "dl_image.h"
#include "imexpand.h"
#include "render.h" /* rippedtodisk */
#include "cvdecodes.h"
#include "params.h"
#include "hdl.h"
#include "groupPrivate.h"
#include "monitor.h"
#include "swcopyf.h"
#include "backdrop.h"
#include "ndisplay.h"  /* NFILLOBJECT */
#include "imstore.h"

#define PCL_PACKED_FG_NA 0xdeadu

/** This flag controls the application of PCL graphics state. */
static Bool pclGstateActive;

/** Controls optimisation of color processing for PCL 5e jobs. */
static Bool pcl5eModeEnabled;

/** This structure contains the current core-side PCL graphics state. */
static PclAttrib pclGstate;

/**
 * A matrix used within the pclGstate global. VxWorks requires doubles to be
 * 8-byte aligned; the compiler will align this structure to 8-bytes for us.
 */
static OMATRIX alignedMatrix;

/** Special cache entries which invoke specific behavior. */
pcl5_pattern erasePattern, whitePattern, invalidPattern;

static uint8 erasePatternData[1] = {0};

static int pcl_xor_count ;
static int pcl_xor_state ;
static int next_xor_state ;

static Bool set_attrib_rendering(DL_STATE *page, PclAttrib *attrib,
                                 const dl_color_t *dlc) ;

#ifdef DEBUG_BUILD
static int32 objnum ;
int32 debug_pcl_idiom ;
int32 debug_pcl_idiom_control ;
#endif

#ifdef METRICS_BUILD
/* Reasons for image merge differences */
enum {
  GAP_BITS = 1,
  GAP_MAX = (1 << GAP_BITS),
  GAP_MASK = GAP_MAX - 1,
  OVERLAP_BITS = 1,
  OVERLAP_MAX = (1 << OVERLAP_BITS),
  OVERLAP_MASK = OVERLAP_MAX - 1
} ;

enum {
  REASON_RATE_DIFF = 1, /* xperw or yperh too different. */
  REASON_NOT_ADJACENT = REASON_RATE_DIFF << 1, /* Not adjacent. */
  REASON_ADJACENT_GAP = REASON_NOT_ADJACENT << 1, /* Gap */
  REASON_ADJACENT_OVERLAP = REASON_ADJACENT_GAP << GAP_BITS, /* Overlaps other amount */
  REASON_ADJACENT_OVERLAP_SP = REASON_ADJACENT_OVERLAP << OVERLAP_BITS, /* Overlaps one source pixel */
  REASON_ADJACENT_SUBSET = REASON_ADJACENT_OVERLAP_SP << 1, /* Not full edge */
  REASON_MAX = REASON_ADJACENT_SUBSET << 1
} ;

enum {
  XOR_BLACK = 1,
  XOR_WHITE = 2,
  XOR_COLOR = 4,
  XOR_SHIFT = 3
} ;

static int32 xor_metrics ;

typedef struct pclattrib_metrics_t {
  int32 unmerged_images ;
  int32 successive_images ;
  int32 merged_images ;
  int32 failed_merges[REASON_MAX] ;
  int32 xor_idioms[1 << (XOR_SHIFT + XOR_SHIFT)] ;
  int32 failed_objectstate ;
  int32 gridsize ;
} pclattrib_metrics_t ;

static pclattrib_metrics_t pclattrib_metrics = { 0 } ;

static void fail_reason(int32 reason, char *buffer)
{
  int32 tmp ;

  strcpy(buffer, "Fail") ;
  buffer += strlen(buffer) ;

  if ( reason & REASON_RATE_DIFF ) {
    strcpy(buffer, "_Rate") ;
    buffer += strlen(buffer) ;
  }

  if ( reason & REASON_NOT_ADJACENT ) {
    strcpy(buffer, "_NAdj") ;
    buffer += strlen(buffer) ;
  }

  if ( (tmp = (reason & (GAP_MASK * REASON_ADJACENT_GAP))) != 0 ) {
    swcopyf((uint8 *)buffer, (uint8 *)"_Gap%d", tmp / REASON_ADJACENT_GAP) ;
    buffer += strlen(buffer) ;
  }

  if ( (tmp = (reason & (OVERLAP_MASK * REASON_ADJACENT_OVERLAP_SP))) != 0 ) {
    strcpy(buffer, "_OverlapSrc") ;
    buffer += strlen(buffer) ;
  }

  if ( (tmp = (reason & (OVERLAP_MASK * REASON_ADJACENT_OVERLAP))) != 0 ) {
    swcopyf((uint8 *)buffer, (uint8 *)"_Overlap%d", tmp / REASON_ADJACENT_OVERLAP) ;
    buffer += strlen(buffer) ;
  }
}

static void xor_reason(int32 idiom, char *buffer)
{
  strcpy(buffer, "Xor") ;
  buffer += strlen(buffer) ;

  if ( idiom & XOR_BLACK ) {
    strcpy(buffer, "_Black") ;
    buffer += strlen(buffer) ;
  }

  if ( idiom & XOR_WHITE ) {
    strcpy(buffer, "_White") ;
    buffer += strlen(buffer) ;
  }

  if ( idiom & XOR_COLOR ) {
    strcpy(buffer, "_Other") ;
    buffer += strlen(buffer) ;
  }

  strcpy(buffer, "_Inside") ;
  buffer += strlen(buffer) ;

  idiom >>= XOR_SHIFT ;

  if ( idiom & XOR_BLACK ) {
    strcpy(buffer, "_Black") ;
    buffer += strlen(buffer) ;
  }

  if ( idiom & XOR_WHITE ) {
    strcpy(buffer, "_White") ;
    buffer += strlen(buffer) ;
  }

  if ( idiom & XOR_COLOR ) {
    strcpy(buffer, "_Other") ;
    buffer += strlen(buffer) ;
  }
}

static Bool pclattrib_metrics_update(sw_metrics_group *metrics)
{
  int32 i ;

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("PCL")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("ImagesMerge")) )
    return FALSE ;

  SW_METRIC_INTEGER("ImagesBeforeMerge", pclattrib_metrics.unmerged_images) ;
  SW_METRIC_INTEGER("ImagePairs", pclattrib_metrics.successive_images) ;
  SW_METRIC_INTEGER("ImagesAfterMerge", pclattrib_metrics.merged_images) ;

  for ( i = 1 ; i < REASON_MAX ; ++i ) {
    if ( pclattrib_metrics.failed_merges[i] != 0 ) {
      char buffer[100] ;

      fail_reason(i, buffer) ;

      if ( !sw_metric_integer(metrics, buffer, strlen(buffer),
                              pclattrib_metrics.failed_merges[i]) )
        return FALSE ;
    }
  }

  if ( pclattrib_metrics.failed_objectstate > 0 ) {
    SW_METRIC_INTEGER("Fail_ObjectState", pclattrib_metrics.failed_objectstate) ;
  }

  sw_metrics_close_group(&metrics) ;

  if (!sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("XORIdioms")) )
    return FALSE ;

  for ( i = 1 ; i < (1 << (XOR_SHIFT + XOR_SHIFT)) ; ++i ) {
    if ( pclattrib_metrics.xor_idioms[i] != 0 ) {
      char buffer[100] ;

      xor_reason(i, buffer) ;

      if ( !sw_metric_integer(metrics, buffer, strlen(buffer),
                              pclattrib_metrics.xor_idioms[i]) )
        return FALSE ;
    }
  }

  sw_metrics_close_group(&metrics) ;
  sw_metrics_close_group(&metrics) ;

  return TRUE ;
}

static void pclattrib_metrics_reset(int reason)
{
  pclattrib_metrics_t init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  pclattrib_metrics = init ;
  xor_metrics = 0 ;
}

static sw_metrics_callbacks pclattrib_metrics_hook = {
  pclattrib_metrics_update,
  pclattrib_metrics_reset,
  NULL
} ;

static void pcl_metrics_count_rop(int usage, PclAttrib *attrib,
                                  const DL_STATE *page)
{
  uintptr_t hashkey = (uintptr_t)(attrib->rop | usage) ;
  if ( foregroundIsWhite(page, attrib) )
    hashkey |= ROP_METRIC_F_WHITE ;
  else if ( foregroundIsBlack(page, attrib) )
    hashkey |= ROP_METRIC_F_BLACK ;
  if ( attrib->sourceTransparent )
    hashkey |= ROP_METRIC_S_TRANS ;
  if ( attrib->patternTransparent )
    hashkey |= ROP_METRIC_P_TRANS ;

  if ( !sw_metric_hashtable_increment_key_counter(dl_metrics()->pcl.rops, hashkey) )
    HQFAIL("Can't increment ROP count") ;
}
#else /* !METRICS_BUILD */
#define pcl_metrics_count_rop(u_, a_, p_) EMPTY_STATEMENT()
#endif /* !METRICS_BUILD */

/**
 * Returns true if the passed color is black.
 */
static Bool packedColorIsBlack(COLORSPACE_ID virtualDeviceSpace,
                               PclPackedColor packedColor)
{
  if (virtualDeviceSpace != SPACE_DeviceCMYK) {
    return packedColor == PCL_PACKED_RGB_BLACK;
  }
  else {
    /* Black is either full K, or full CMY, or both (but obviously we don't need
     * to explicitly check for that). Packed colors are additive. */
    return (packedColor >> 24) == 0 || (packedColor & 0xffffff) == 0 ;
  }
}

/**
 * Returns true if the passed color is black.
 */
static Bool packedColorIsWhite(COLORSPACE_ID virtualDeviceSpace,
                               PclPackedColor packedColor)
{
  if (virtualDeviceSpace != SPACE_DeviceCMYK) {
    return packedColor == PCL_PACKED_RGB_WHITE;
  }
  else {
    return packedColor == PCL_PACKED_CMYK_WHITE;
  }
}

#define SWAP(_a, _b, _temp) { \
  (_temp) = (_a); \
  (_a) = (_b); \
  (_b) = (_temp); \
}

static Bool ncolor_from_packed_color(DL_STATE *page,
                                     p_ncolor_t *ncolor,
                                     PclPackedColor packed)
{
  dlc_context_t *dlc_context = page->dlc_context ;
  dl_color_t dlc ;

  dlc_clear(&dlc) ;

  /* Use the special white for the preconverted white to make it easier
     to detect in the pattern blitters. */
  if ( packedColorIsWhite(page->virtualDeviceSpace, packed) ) {
    dlc_get_white(dlc_context, &dlc) ;
  } else {
    USERVALUE floats[4] ;

    /* If virtual colorspace is subtractive, negate additive packed CMYK
       values. */
    if ( page->virtualDeviceSpace == SPACE_DeviceCMYK ) {
      /* Store special black (0) or non-special black (1) DL color when
         optimising PCL attributes. */
      if ( packedColorIsBlack(SPACE_DeviceCMYK, packed) ) {
        packed = 0xff000000u ; /* K channel only. */
      } else {
        packed = ~packed ;
      }
    }

    /* We don't actually know if this is Gray, RGB, or CMYK, but we can
       unpack all of the channels from the packed color into the floats,
       and set the color directly. */
    floats[0] = (uint8)packed / 255.0f ;
    floats[1] = (uint8)(packed >> 8) / 255.0f ;
    floats[2] = (uint8)(packed >> 16) / 255.0f ;
    floats[3] = (uint8)(packed >> 24) / 255.0f ;

    if ( !gsc_setcolordirect(gstateptr->colorInfo, GSC_FILL, floats) ||
         !gsc_invokeChainSingle(gstateptr->colorInfo, GSC_FILL) )
      return FALSE ;

    dlc_copy_release(dlc_context, &dlc, dlc_currentcolor(dlc_context)) ;
  }

  return dlc_to_dl(dlc_context, ncolor, &dlc) ;
}

/**
 * Walk over DL pattern pixels, reading through the transform from the source
 * pattern. Note that the transform does not include the DPI scaling factor,
 * so this is applied separately.
 */
static Bool transformPattern(DL_STATE *page, PclAttrib* self, void* sourceData,
                             uint32 sourceBitsPerComponent, Bool direct,
                             uint32 sourceStride,
                             uint32 sourceWidth, uint32 sourceHeight,
                             double xScale, double yScale)
{
  uint32 x, y, ox, oy;
  OMATRIX transform;
  Bool transformValid = self->constructionState.transformValid;
  PclDLPattern* dlPattern = self->dlPattern;
  uint8* sourceBytes = NULL;
  PclPackedColor* sourceColors = NULL;
  Bool preconvert ;
  dlc_context_t *dlc_context = page->dlc_context ;
  dl_color_t saveddlcolor ;
  Bool result = FALSE ;
  uint32 offset ;
  DEVICESPACEID dspace ;

  guc_deviceColorSpace(page->hr, &dspace, NULL) ;
  /* Preconvert if virtualDeviceSpace is the same as the output device space,
     and not PCL5e (which uses direct tests on the 0/1 pattern index
     values). */
  /* Assume that all PCL patterns can be handled through idioms or direct
     rendering, and convert them to DL colors during interpretation if the
     virtual device space matches the final output space. */
  preconvert = (!pcl5eModeIsEnabled() &&
                ((page->virtualDeviceSpace == SPACE_DeviceCMYK &&
                  dspace == DEVICESPACE_CMYK) ||
                 (page->virtualDeviceSpace == SPACE_DeviceRGB &&
                  dspace == DEVICESPACE_RGB) ||
                 (page->virtualDeviceSpace == SPACE_DeviceGray &&
                  dspace == DEVICESPACE_Gray)) ) ;

  UNUSED_PARAM(uint32, sourceWidth);
  UNUSED_PARAM(uint32, sourceHeight);

  HQASSERT((direct && sourceBitsPerComponent == 8) ||
           (! direct &&
            (sourceBitsPerComponent == 1 || sourceBitsPerComponent == 8)),
           "Unsupported pattern pixel format.");

#define return DO_NOT_return_GO_TO_failed_INSTEAD!
  if ( preconvert ) {
    dlc_clear(&saveddlcolor) ;
    dlc_copy_release(dlc_context, &saveddlcolor, dlc_currentcolor(dlc_context)) ;
    dlPattern->preconverted = PCL_PATTERN_PRECONVERT_DEVICE ;
    if ( !gsc_setcolorspacedirect(gstateptr->colorInfo, GSC_FILL, page->virtualDeviceSpace) )
      goto failed ;
  }

  if (direct) {
    sourceColors = (PclPackedColor*)sourceData;
  }
  else {
    sourceBytes = (uint8*)sourceData;
    if ( preconvert ) { /* convert palette from packed color to device color */
      uint32 i ;
      for ( i = 0 ; i < dlPattern->paletteSize ; ++i ) {
        if ( !ncolor_from_packed_color(page,
                                       &dlPattern->palette[i].ncolor,
                                       dlPattern->palette[i].packed) )
          goto failed ;
      }
    }
  }

  if (transformValid) {
    /* Invert the matrix so it maps from device pattern to source pattern
     * space. */
    if ( !matrix_inverse(self->constructionState.transform, &transform) )
      HQFAIL("Non-invertible PCL pattern transform matrix") ;
  }

  /* Normalise the pattern reference point; this may be negative. */
  HQASSERT(dlPattern->width <= 0xFFFF && dlPattern->height <= 0xFFFF,
           "Unexpected pattern width or height") ;

  ox = abs(self->constructionState.origin.x) % dlPattern->width;
  oy = abs(self->constructionState.origin.y) % dlPattern->height;
  if (self->constructionState.origin.x < 0)
    ox = dlPattern->width - ox;
  if (self->constructionState.origin.y < 0)
    oy = dlPattern->height - oy;

  offset = 0 ;
  for (y = 0; y < dlPattern->height; ++y) {
    PclDLPatternLine *line = &dlPattern->lines[(y + oy) % dlPattern->height] ;
    uint8 *outByte = NULL;
    pcl_color_union *outColor = NULL;
    uint32 w = dlPattern->width ;
    /* The line limit is set to the highest offset where we can guarantee
       to complete the pattern using full lines. This means that the first
       few lines stored have less tolerance for bad compression, later lines
       may compress to RLE inefficiently. */
    uint32 linelimit = w * (y + 1) ;

    line->type = PCL_PATTERNLINE_FULL ;
    line->repeats = 0 ;
    line->offset = CAST_UNSIGNED_TO_UINT16(offset) ;

    if (direct) {
      outColor = &dlPattern->data.pixels[offset];
    } else {
      outByte = &dlPattern->data.indices[offset];
    }

    for (x = 0; x < w; x ++) {
      uint32 writeX = (x + ox) % w;
      int32 readX, readY;

      SC_C2D_INT(readX, (x / xScale));
      SC_C2D_INT(readY, (y / yScale));
      if (transformValid) {
        double pX, pY;
        MATRIX_TRANSFORM_XY(readX, readY, pX, pY, &transform);
        SC_C2D_INT(readX, pX);
        SC_C2D_INT(readY, pY);
      }

      HQASSERT(readX >= 0 && readX < (int32)sourceWidth &&
               readY >= 0 && readY < (int32)sourceHeight,
               "Invalid read point.");

      if (sourceBitsPerComponent == 1) {
        uint8 byte = sourceBytes[(readY * sourceStride) + (readX >> 3)];
        outByte[writeX] = (uint8)((byte >> (7 - (readX & 7))) & 1);
      } else if (!direct) {
        outByte[writeX] = sourceBytes[(readY * sourceStride) + readX];
      } else if ( !preconvert ) {
        outColor[writeX].i = 0 ; /* In case sizeof(intptr) > sizeof(packed) */
        outColor[writeX].packed = sourceColors[(readY * sourceStride) + readX];
      } else {
        if ( !ncolor_from_packed_color(page,
                                       &outColor[writeX].ncolor,
                                       sourceColors[(readY * sourceStride) + readX]) )
          goto failed ;
      }
    }

    /* Now try to RLE compress the line, using whatever space is
       available up to the total pattern space as temporary storage. */
    {
      uint32 i = 0, r = w ; /* Start indices of full line, RLE data. */

      /* rlast is the index of the last possible location we can store RLE
         data into, relative to the start of the full line data. */
      uint32 rlast = w * dlPattern->height - offset - 2 ;

      while ( i < w && r <= rlast ) {
        intptr_t val = direct ? outColor[i].i : outByte[i] ;
        uint32 start = i ;
        do {
          ++i ;
        } while ( i < w && (i - start) < 255 &&
                  val == (direct ? outColor[i].i : outByte[i]) ) ;

        if ( direct ) {
          outColor[r++].i = i - start ;
          outColor[r++].i = val ;
        } else {
          outByte[r++] = (uint8)(i - start) ;
          outByte[r++] = (uint8)val ;
        }
      }

      r -= w ; /* Width of RLE'd data */

      if ( i == w && offset + r < linelimit && r <= PCL_PATTERNLINE_RLEMAX * 2 ) {
        HQASSERT(r > 0, "No RLE spans in pattern line") ;
        /* Successfully RLE'd line, and line fits in available space. */
        line->type = CAST_UNSIGNED_TO_UINT8(PCL_PATTERNLINE_RLE0 + r / 2) ;
        /* Copy RLE line data over top of full line data. */
        for ( i = 0 ; i < r ; ++i ) {
          if ( direct ) {
            outColor[i].i = outColor[i + w].i ;
          } else {
            outByte[i] = outByte[i + w] ;
          }
        }
      } else {
        /* Use full line data. */
        r = w ;
      }

      /* Now check whether line is the same as previous line. */
      if ( y > 0 && line > dlPattern->lines && line->type == line[-1].type ) {
        /* Potential match. Check if the data matches. */
        uint8 *lastByte = NULL;
        pcl_color_union *lastColor = NULL;

        if (direct) {
          lastColor = &dlPattern->data.pixels[line[-1].offset];
        } else {
          lastByte = &dlPattern->data.indices[line[-1].offset];
        }

        for ( i = 0 ; i < r ; ++i ) {
          if ( direct
               ? outColor[i].i != lastColor[i].i
               : outByte[i] != lastByte[i] )
            break ;
        }

        if ( i == r ) { /* Same as previous */
          line->offset = line[-1].offset ;
          do {
            --line ;
            line->repeats += 1 ;
          } while ( line > dlPattern->lines &&
                    line->offset == line[-1].offset ) ;
          r = 0 ;
        }
      }

      offset += r ;
    }
  }

  result = TRUE ;

 failed:
  if ( preconvert ) {
    dlc_release(dlc_context, dlc_currentcolor(dlc_context)) ;
    dlc_copy_release(dlc_context, dlc_currentcolor(dlc_context), &saveddlcolor) ;
  }

#undef return
  return result ;
}

static void setBlackAndWhiteForDirect(PclPackedColor *data,
                                      uint32 sourceWidth, uint32 sourceHeight,
                                      COLORSPACE_ID virtualDeviceSpace,
                                      PclAttrib *attrib,
                                      PclPackedColor *patternColor)
{
  uint32 w, h;
  PclPackedColor otherColor = 0u ;

  for ( h = 0; h < sourceHeight; ++h ) {
    for ( w = 0; w < sourceWidth; ++w ) {
      PclPackedColor color = data[0] ;
      if ( packedColorIsWhite(virtualDeviceSpace, color) ) {
        attrib->patternColors |= PCL_PATTERN_WHITE ;
      } else {
        if ( (attrib->patternColors & ~PCL_PATTERN_WHITE) != 0 &&
             otherColor != color ) { /* Different non-white. */
          attrib->patternColors |= PCL_PATTERN_MANY ;
        }
        if ( packedColorIsBlack(virtualDeviceSpace, color) ) {
          attrib->patternColors |= PCL_PATTERN_BLACK ;
        } else {
          attrib->patternColors |= PCL_PATTERN_OTHER ;
        }
        otherColor = color ;
      }
      if ( attrib->patternColors == PCL_PATTERN_ALL )
        return ;
      ++data;
    }
  }
  *patternColor = otherColor ;
}

static void setBlackAndWhiteForPalette(uint8 *data,
                                       uint32 sourceWidth, uint32 sourceHeight,
                                       uint32 sourceBitsPerComponent,
                                       uint32 sourceStride,
                                       COLORSPACE_ID virtualDeviceSpace,
                                       PclAttrib *attrib,
                                       PclPackedColor *patternColor)
{
  uint32 w, h;
  PclPackedColor otherColor = 0u ;
  Pcl5CachedPalette *palette = attrib->constructionState.palette;

  /* Extensive test to determine if all of the values actually used by
     the pattern data are black or white. */
  for ( h = 0; h < sourceHeight; ++h ) {
    for ( w = 0; w < sourceWidth; ++w ) {
      PclPackedColor color ;
      uint32 index ;

      if ( sourceBitsPerComponent == 1 ) {
        index = (data[w >> 3] >> (7 - (w & 7))) & 1 ;
      } else {
        HQASSERT(sourceBitsPerComponent == 8,
                 "Invalid PCL pattern bits per component") ;
        index = data[w] ;
      }

      if ( index >= palette->size )
        index %= palette->size ;

      color = palette->colors[index] ;

      if ( packedColorIsWhite(virtualDeviceSpace, color) ) {
        attrib->patternColors |= PCL_PATTERN_WHITE ;
      } else {
        if ( (attrib->patternColors & ~PCL_PATTERN_WHITE) != 0 &&
             otherColor != color ) { /* Different non-white. */
          attrib->patternColors |= PCL_PATTERN_MANY ;
        }
        if ( packedColorIsBlack(virtualDeviceSpace, color) ) {
          attrib->patternColors |= PCL_PATTERN_BLACK ;
        } else {
          attrib->patternColors |= PCL_PATTERN_OTHER ;
        }
        otherColor = color ;
      }
      if ( attrib->patternColors == PCL_PATTERN_ALL )
        return ;
    }
    data += sourceStride ;
  }
  *patternColor = otherColor ;
}

/** Determine what colors the pattern contains. */
static void getPclPatternColor(COLORSPACE_ID virtualDeviceSpace,
                               PclAttrib *attrib,
                               PclPatternConstructionState *cState)
{
  PclCachedPattern* cacheEntry = &attrib->cacheEntry;
  void* sourceData;
  uint32 sourceWidth, sourceHeight;
  uint32 sourceBitsPerComponent, sourceStride;
  uint32 width, height;
  double xScale, yScale;
  Bool colorPattern, direct;

  if ( cState->cached ) {
    attrib->patternColors = cState->patternColors ;
    return ;
  }

  /* Get PCL5/PCLXL specific data. */
  switch (cacheEntry->type) {
  default:
    HQFAIL("Unknown pattern type.");
    return;

  case PCL5_PATTERN:
    {
      pcl5_pattern* p = cacheEntry->pointer.pcl5;
      colorPattern = p->color && cState->palette != NULL;
      direct = FALSE;
      sourceData = p->data;
      sourceBitsPerComponent = p->bits_per_pixel;
      sourceStride = p->stride;
      sourceWidth = p->width;
      sourceHeight = p->height;
      xScale = cState->deviceDpi / (double)p->x_dpi;
      yScale = cState->deviceDpi / (double)p->y_dpi;
      break;
    }

  case PCLXL_PATTERN:
    {
      PclXLPattern* p = cacheEntry->pointer.pclxl;
      colorPattern = TRUE;
      direct = p->direct;
      if (direct)
        sourceData = p->data.pixels;
      else {
        HQASSERT(cState->palette != NULL,
                 "Null palette in indexed PCLXL pattern");
        sourceData = p->data.indices;
      }
      sourceBitsPerComponent = p->bits_per_pixel;
      sourceStride = p->stride;
      sourceWidth = p->size.x;
      sourceHeight = p->size.y;
      xScale = cState->targetSize.x / (double)p->size.x;
      yScale = cState->targetSize.y / (double)p->size.y;
      break;
    }
  }

  cState->useForeground = !colorPattern ;

  width = (uint32)(sourceWidth * xScale);
  height = (uint32)(sourceHeight * yScale);

  if (width == 0 || height == 0) {
    /* Not much we can do here; just use the default solid black pattern. */
    attrib->patternColors = PCL_PATTERN_NONE ;
  } else if (direct) {
    setBlackAndWhiteForDirect(sourceData, sourceWidth, sourceHeight,
                              virtualDeviceSpace, attrib,
                              &cState->patternColor);
  } else if (colorPattern) {
    setBlackAndWhiteForPalette(sourceData, sourceWidth, sourceHeight,
                               sourceBitsPerComponent, sourceStride,
                               virtualDeviceSpace, attrib,
                               &cState->patternColor);
  } else {
    attrib->patternColors = PCL_PATTERN_BLACK_AND_WHITE;
  }

  HQASSERT(colorPattern ||
           (attrib->patternColors & PCL_PATTERN_OTHER) == 0,
           "Black and white pattern contained other color") ;

  cState->patternColors = attrib->patternColors ;
  cState->cached = TRUE ;
}

/**
 * Create the DL pattern for the passed attribute.
 */
static Bool makeDlPattern(PclAttrib* self, mm_pool_t *pools)
{
  PclDLPattern* dlPattern = NULL;
  PclDLPattern init = {0};
  PclCachedPattern* cacheEntry = &self->cacheEntry;
  PclPatternConstructionState* cState = &self->constructionState;
  void* sourceData;
  uint32 sourceWidth, sourceHeight;
  uint32 sourceBitsPerComponent, sourceStride;
  uint32 width, height;
  double xScale, yScale;
  size_t dataBytes;
  Bool colorPattern, direct;
  DL_STATE *page = get_core_context_interp()->page ;
  uint32 i ;

  /* If the pattern has been determined to make no difference, then don't
     build a DL pattern. */
  if (self->patternColors == PCL_PATTERN_NONE)
    return TRUE ;

  /* Get PCL5/PCLXL specific data. */
  switch (cacheEntry->type) {
    default:
      HQFAIL("Unknown pattern type.");
      return FALSE;

    case PCL5_PATTERN: {
      pcl5_pattern* p = cacheEntry->pointer.pcl5;
      colorPattern = p->color && cState->palette != NULL;
      direct = FALSE;
      sourceData = p->data;
      sourceBitsPerComponent = p->bits_per_pixel;
      sourceStride = p->stride;
      sourceWidth = p->width;
      sourceHeight = p->height;
      xScale = cState->deviceDpi / (double)p->x_dpi;
      yScale = cState->deviceDpi / (double)p->y_dpi;
      break;
    }

    case PCLXL_PATTERN: {
      PclXLPattern* p = cacheEntry->pointer.pclxl;
      colorPattern = TRUE;
      direct = p->direct;
      if (direct)
        sourceData = p->data.pixels;
      else {
        HQASSERT(cState->palette != NULL,
                 "Null palette in indexed PCLXL pattern");
        sourceData = p->data.indices;
      }
      sourceBitsPerComponent = p->bits_per_pixel;
      sourceStride = p->stride;
      sourceWidth = p->size.x;
      sourceHeight = p->size.y;
      xScale = cState->targetSize.x / (double)p->size.x;
      yScale = cState->targetSize.y / (double)p->size.y;
      break;
    }
  }

  width = (uint32)(sourceWidth * xScale);
  height = (uint32)(sourceHeight * yScale);

  if (width == 0 || height == 0) {
    /* Not much we can do here; just use the default solid black pattern. */
    HQFAIL("Should have filtered out degenerate patterns") ;
    return TRUE;
  }

  /* The pattern transformation may swap the x and y axes (e.g. a 90 degree
   * rotation). */
  if (cState->transposed) {
    uint32 tempInt;
    double tempDouble;
    SWAP(width, height, tempInt);
    SWAP(xScale, yScale, tempDouble);
  }

  if ( direct ) {
    dataBytes = sizeof(pcl_color_union) * width * height;
  } else {
    dataBytes = sizeof(uint8) * width * height;
    if ( colorPattern ) {
      dataBytes += sizeof(pcl_color_union) * cState->palette->size ;
    } else {
      dataBytes += sizeof(pcl_color_union) * 2 ; /* Shared palette copy */
    }
  }
  dataBytes += height * sizeof(PclDLPatternLine) ;
  dlPattern = dl_alloc(pools, sizeof(PclDLPattern) + dataBytes,
                       MM_ALLOC_CLASS_PCL_ATTRIB_PATTERN);
  if (dlPattern == NULL)
    return error_handler(VMERROR);

  *dlPattern = init;

  self->dlPattern = dlPattern;

  dlPattern->width = width;
  dlPattern->height = height;
  dlPattern->dataBytes = dataBytes;
  dlPattern->preconverted = PCL_PATTERN_PRECONVERT_NONE ;
  dlPattern->lines = (void *)(dlPattern + 1) ;

  if (direct) {
    dlPattern->palette = NULL;
    dlPattern->paletteSize = 0;
    dlPattern->data.pixels = (void*)(dlPattern->lines + height);
  } else if (colorPattern) {
    Pcl5CachedPalette *palette = cState->palette ;
    dlPattern->palette = (void*)(dlPattern->lines + height);
    dlPattern->paletteSize = palette->size ;
    dlPattern->data.indices = (void *)(dlPattern->palette + palette->size) ;
    for ( i = 0 ; i < palette->size ; ++i ) {
      dlPattern->palette[i].packed = palette->colors[i] ;
    }
  } else {
    dlPattern->palette = (void*)(dlPattern->lines + height);
    dlPattern->paletteSize = 2 ;
    dlPattern->data.indices = (void *)(dlPattern->palette + 2) ;
    if ( pcl5eModeIsEnabled() ) {
      /* Palette is not needed in mono rendering, however black/white data is
         packed into indices storage. To make it easy to use the pattern
         iterator, we set up a palette with 0 and 1. */
      dlPattern->palette[0].packed = 0;
      dlPattern->palette[1].packed = 1;
    } else if (page->virtualDeviceSpace != SPACE_DeviceCMYK) {
      dlPattern->palette[0].packed = PCL_PACKED_RGB_WHITE;
      dlPattern->palette[1].packed = PCL_PACKED_RGB_BLACK;
    } else {
      dlPattern->palette[0].packed = PCL_PACKED_CMYK_WHITE;
      dlPattern->palette[1].packed = PCL_PACKED_CMYK_BLACK;
    }
  }

  return transformPattern(page, self, sourceData, sourceBitsPerComponent,
                          direct, sourceStride, sourceWidth, sourceHeight,
                          xScale, yScale);
}

/**
 * Returns true if the passed two patterns are the same.
 */
static Bool patternSame(int32 patternType,
                        PclPatternConstructionState* a,
                        PclPatternConstructionState* b)
{
  Bool transformsSame = FALSE;

  /* Check simple elements first. */
  if (a->paletteUid != b->paletteUid ||
      ! POINTS_EQUAL(a->origin, b->origin))
    return FALSE;

  /* Check pattern-type specific state. */
  switch (patternType) {
    default:
      HQFAIL("Invalid pattern type.");
      return FALSE;

    case PCL5_PATTERN:
      if (a->deviceDpi != b->deviceDpi)
        return FALSE;
      break;

    case PCLXL_PATTERN:
      if (! POINTS_EQUAL(a->targetSize, b->targetSize))
        return FALSE;
      break;
  }

  if (a->transformValid && b->transformValid)
    transformsSame = matrix_equal(a->transform, b->transform);
  else
    transformsSame = (a->transformValid == b->transformValid);

  return transformsSame;
}

static PclAttrib* attribCopy(PclAttrib* original, mm_pool_t *pools)
{
  PclAttrib *copy;

  /* We allocate additional memory to hold an 8-byte aligned OMATRIX, which
   * is used within the constructionState member. */
  copy = dl_alloc(pools, sizeof(PclAttrib) + sizeof(OMATRIX) + 4,
                  MM_ALLOC_CLASS_PCL_ATTRIB_MATRIX);
  if (copy == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
#ifdef METRICS_BUILD
  dl_metrics()->store.pclCount++;
#endif

  *copy = *original;

  /* Patch up the transform pointer. The mess is required by VxWorks, which
   * insists on all double's being 8-byte aligned. */
  copy->constructionState.transform = PTR_ALIGN_UP(OMATRIX*, copy + 1, sizeof(SYSTEMVALUE));
  *copy->constructionState.transform = *original->constructionState.transform;

  return copy;
}

/**
 * DLStore Copy method.
 */
DlSSEntry* pclAttribCopy(DlSSEntry* entry, mm_pool_t *pools)
{
  PclAttrib* original = (PclAttrib*)entry;
  PclAttrib* copy;

  HQASSERT(original != NULL, "'original' cannot be NULL");

  copy = attribCopy(original, pools);
  if ( !copy )
    return NULL;

  copy->dlPattern = NULL;
  if (! makeDlPattern(copy, pools)) {
    pclAttribDelete(&copy->storeEntry, pools);
    return NULL;
  }
  /* We can't hold onto a palette reference beyond construction. */
  copy->constructionState.palette = NULL;

  return &copy->storeEntry;
}

/* attribModify is required because pclAttribCopy is not a pure copy.
   pclAttribCopy does some construction, calling makeDlPattern, which requires
   the constructionState to be around and this is only present in the
   special-cased static attrib pclGstate.  I think pclAttribCopy was written
   this way to make use of DL store caching thus avoiding calling makeDlPattern
   more than necessary.  attribModify is used to change the rop in any PCL
   attrib on the DL and it doesn't require constructionState to exist.  It
   allocs its own PCL attrib which is inserted directly into the DL store cache
   (or an existing cached version is used instead), and this avoids
   pclAttribCopy being called. */
static PclAttrib* attribModify(DL_STATE *page,
                               PclAttrib *original,
                               uint8 rop,
                               uint8 foregroundSource,
                               uint8 sourceTransparent,
                               uint8 patternTransparent,
                               uint8 patternColors,
                               uint8 xorstate,
                               const dl_color_t *dlc)
{
  PclAttrib* copy, *cached;

  HQASSERT(original != NULL, "'original' cannot be NULL");
  HQASSERT(original->constructionState.palette == NULL,
           "Shouldn't be holding on to a constructionState");

  copy = attribCopy(original, page->dlpools);
  if ( !copy )
    return NULL;

  copy->rop = rop;
  copy->foregroundSource = foregroundSource;
  copy->patternColors = patternColors;
  copy->xorstate = xorstate;

  if ( rop == PCL_ROP_D ) {
    copy->sourceTransparent = FALSE;
    copy->patternTransparent = FALSE;
    copy->backdrop = FALSE ;
    copy->patternBlit = FALSE ;
  } else {
    copy->sourceTransparent = sourceTransparent;
    copy->patternTransparent = patternTransparent;

    if ( !set_attrib_rendering(page, copy, dlc) )
      return NULL ;
  }

  /* Need to insert, NOT copy, because there is no constructionState
     for this attrib, therefore we cannot call makeDlPattern. */
  cached = (PclAttrib*)dlSSInsert(page->stores.pcl, &copy->storeEntry,
                                  FALSE /* insert, don't copy */);
  if ( !cached )
    return NULL;

  if ( cached != copy ) {
    /* Already have a cached PCL attrib - free the copy (but not any shared
     * DL pattern) and return the cached attrib. */
    copy->dlPattern = NULL;
    pclAttribDelete(&copy->storeEntry, page->dlpools);
  }

  return cached;
}

/**
 * DLStore destructor.
 */
void pclAttribDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  if (entry != NULL) {
    PclAttrib* self = (PclAttrib*)entry;
    PclDLPattern* dlPattern = self->dlPattern;
    if (dlPattern != NULL) {
      dl_free(pools, dlPattern, sizeof(PclDLPattern) + dlPattern->dataBytes,
              MM_ALLOC_CLASS_PCL_ATTRIB_PATTERN);
    }
    dl_free(pools, entry, sizeof(PclAttrib) + sizeof(OMATRIX) + 4,
            MM_ALLOC_CLASS_PCL_ATTRIB_MATRIX);
  }
}

/**
 * DLStore Hash function.
 */
uintptr_t pclAttribHash(DlSSEntry* entry)
{
  PclAttrib* self = (PclAttrib*)entry;
  uint32 transparency;
  uintptr_t hh;

  HQASSERT(self != NULL, "'self' cannot be NULL");

  transparency = self->sourceTransparent | (self->patternTransparent << 1);

  hh = ((intptr_t)self->cacheEntry.pointer.pointer) + self->rop +
    (transparency << 8) + (self->patternColors << 10) +
    (self->xorstate << 13) ;
  if ( self->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB )
    hh += (intptr_t)self->foreground;
  return hh;
}

/**
 * DLStore comparison method.
 */
Bool pclAttribSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  PclAttrib* a = (PclAttrib*)entryA;
  PclAttrib* b = (PclAttrib*)entryB;

  HQASSERT((a != NULL) && (b != NULL), "Parameters cannot be NULL");

  /* Check simple elements first. */
  if (a->cacheEntry.type != b->cacheEntry.type ||
      a->cacheEntry.pointer.pointer != b->cacheEntry.pointer.pointer ||
      a->sourceTransparent != b->sourceTransparent ||
      a->patternTransparent != b->patternTransparent ||
      a->rop != b->rop ||
      /** \todo ajcd 2013-07-10: Is this implied by the same pattern and ROP? */
      a->patternColors != b->patternColors ||
      a->xorstate != b->xorstate ||
      a->backdrop != b->backdrop ||
      a->patternBlit != b->patternBlit ||
      /* Are both using the same foreground color settings? */
      a->foregroundSource != b->foregroundSource ||
      /* Foreground sources are the same; if the color is within this
         structure check the two match. */
      (a->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB &&
       a->foreground != b->foreground) )
    return FALSE ;

  return patternSame(a->cacheEntry.type, &a->constructionState,
                     &b->constructionState);
}

/**
 * Preserve any stored children.
 */
void pclAttribPreserveChildren(DlSSEntry* entry, DlSSSet* set)
{
  UNUSED_PARAM(DlSSEntry*, entry);
  UNUSED_PARAM(DlSSSet*, set);

  /* nothing to do as yet. */
}

/**
 * Initialise the solid white and Invalid pattern cache entries.
 */
static void initSpecialPatterns(void)
{
  /** \todo ajcd 2009-11-22: If these can't have changed since C runtime,
      init remove this function because init_C_globals_pclAttrib() will
      have done the job. */
  pcl5_pattern blank = {0};
  pcl5_pattern *e = &erasePattern;

  invalidPattern = erasePattern = whitePattern = blank;

  e->width = 8;
  e->height = 1;
  e->x_dpi = e->y_dpi = 300;
  e->color = FALSE;
  e->bits_per_pixel = 1;
  e->stride = 1;
  e->data = erasePatternData;
  whitePattern = erasePattern;
}

void pclGstateInit(corecontext_t* corecontext)
{
  if ( pclGstateActive ) {
    PclAttrib init = {0};
    IPOINT offset = {0, 0};

    /* Zero all members. */
    pclGstate = init;

    /* The transform member of the construction state is normally allocated during
     * construction; for the global instance we'll point it at a static OMATRIX. */
    pclGstate.constructionState.transform = &alignedMatrix;

    initSpecialPatterns();
    setPcl5Pattern(NULL, 300, 0, &offset, NULL);
    setPclSourceTransparent(TRUE);
    setPclPatternTransparent(TRUE);
    setPclRop(PCL_ROP_TSo);
    setPclForegroundSource(corecontext->page, PCL_FOREGROUND_NOT_SET);

    HQASSERT(pcl_xor_count == 0 && pcl_xor_state == PCL_XOR_OUTSIDE,
             "Lost track of PCL XOR idioms") ;
    pcl_xor_count = 0 ;
    pcl_xor_state = next_xor_state = PCL_XOR_OUTSIDE ;
  }
}

void pclGstateEnable(corecontext_t* corecontext,
                     Bool enable, Bool blackAndWhiteOnly)
{
  pclGstateActive = enable;    /* Must be enabled before pclGstateInit call */
  pcl5eModeEnabled = blackAndWhiteOnly;
  pclGstateInit(corecontext);

  if (enable) {
    /* Set the current backdrop's opaqueOnly mode, though we expect bd_sharedNew
       to get called soon anyway, which will call pcl5eModeIsEnabled. */
    corecontext->page->opaqueOnly = TRUE;
    /* Set current page's 5e mode. dl_begin_page will call pcl5eModeIsEnabled
       for subsequent pages. */
    corecontext->page->pcl5eModeEnabled = blackAndWhiteOnly;
  }
}

Bool pclGstateIsEnabled(void)
{
  HQASSERT(IS_INTERPRETER(), "pclGstateIsEnabled called from back end") ;

  return pclGstateActive;
}

Bool pcl5eModeIsEnabled(void)
{
  HQASSERT(IS_INTERPRETER(), "pcl5eModeIsEnabled called from back end") ;

  return pclGstateActive && pcl5eModeEnabled;
}

/** Replace a template DL color with the values from a packed PCL color. */
Bool dlc_from_packed_color(dlc_context_t *dlc_context, dl_color_t *dlc,
                           PclPackedColor packedColor)
{
  dl_color_t dlc_new;
  COLORVALUE cvs[4];
  int32 ncomps;

  dlc_clear(&dlc_new);

  ncomps = DLC_NUM_CHANNELS(dlc);
  HQASSERT(ncomps == 1 || ncomps == 3 || ncomps == 4,
           "Expected PCL to be 1, 3 or 4 colorants");
  /* PCL_UNPACK_RGB is a subset of PCL_UNPACK_CMYK. We're OK unpacking the
     fourth channel, we know we have space. This also works for gray values. */
  PCL_UNPACK_CMYK(packedColor, cvs) ;

  if ( !dlc_alloc_fillin_template(dlc_context, &dlc_new, dlc, cvs, ncomps) )
    return FALSE ;

  /* Release template reference and transfer new color to output dlc
     wrapper. */
  dlc_release(dlc_context, dlc);
  dlc_copy_release(dlc_context, dlc, &dlc_new);

  return TRUE ;
}

/** Force the foreground color to black. */
static void set_fg_black(PclAttrib *attrib, DL_STATE *page, dl_color_t *dlc)
{
  if ( attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
    dlc_context_t *dlc_context = page->dlc_context ;
    /* Store special black (0) or non-special black (1) DL color when
       optimising PCL attributes. */
    if ( !dlc_from_packed_color(dlc_context, dlc,
                                (!page->pcl5eModeEnabled &&
                                 page->virtualDeviceSpace == SPACE_DeviceCMYK)
                                ? 0x00ffffffu /*black channel only*/
                                : 0u /*generic additive black*/) ) {
      HQFAIL("Couldn't reallocate packed color") ;
    }
  } else if ( attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB ) {
    attrib->foreground = 0u ;
    HQASSERT(PCL_PACKED_RGB_BLACK == attrib->foreground &&
             PCL_PACKED_CMYK_BLACK == attrib->foreground,
             "PCL Packed black values differ") ;
  }
}

/** Force the foreground color to white. */
static void set_fg_white(PclAttrib *attrib, DL_STATE *page, dl_color_t *dlc)
{
  if ( attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
    dlc_context_t *dlc_context = page->dlc_context ;
    dlc_release(dlc_context, dlc) ;
    dlc_get_white(dlc_context, dlc) ;
  } else if ( attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB ) {
    if (page->virtualDeviceSpace == SPACE_DeviceCMYK)
      attrib->foreground = PCL_PACKED_CMYK_WHITE;
    else
      attrib->foreground = PCL_PACKED_RGB_WHITE;
  }
}

Bool getPclAttrib(DL_STATE* page, Bool is_image, Bool first_try,
                  PclAttrib** result)
{
  PclAttrib attrib = pclGstate; /* From now on use attrib, not pclGstate */
  dlc_context_t *dlc_context;
  dl_color_t *dlc_current ;

  HQASSERT(page != NULL && result != NULL, "Parameters cannot be NULL");

  *result = NULL;
  /* Abort early if we're not applying PCL gstate. */
  if (! pclGstateActive)
    return TRUE;

  dlc_context = page->dlc_context;
  dlc_current = dlc_currentcolor(dlc_context) ;

  pcl_metrics_count_rop(ROP_METRIC_ORIGINAL, &attrib, page) ;

  /* Source color canonicalisation/optimisation. */
  if ( attrib.foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
    /* DL color is foreground is only used for cases where the source color
       is black. We therefore know that source transparency is irrelevant
       (it only matters if the source color is white), so we can force source
       transparency to FALSE, and simplify the ROP. */
    attrib.sourceTransparent = FALSE ;
    attrib.rop = (attrib.rop & 0x33) | ((attrib.rop & 0x33) << 2);

    /* Foreground in structure isn't used. */
    attrib.foreground = PCL_PACKED_FG_NA ;

    /** \todo ajcd 2013-07-16: Optimise Tn ROP to T and flip foreground
        color if FG black or white. Is there a generic test we can use to find
        Tn subsections in the ROPs in general? */
  } else if ( attrib.foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB ) {
    /* Foreground in PCL attrib is used for images (where the source is
       variable), and when we are rendering the white spans of a character
       cell. The latter case only happens when source transparency is FALSE
       (otherwise the spans are ignored). We can simplify ROPs for this
       case, because the source color is constant. */
    if ( !is_image &&
         (attrib.rop != PCL_ROP_S) ) {
      HQASSERT(!attrib.sourceTransparent,
               "Source white transparent object with FG in attrib") ;
      /*
       * Evolution of optimisations means this code path is no longer
       * being taken for any of the jobs in the performance suite, but it is
       * for jobs in the ISO suite. And the optimisation assumptions made
       * at the time are not valid for this suite. So turn an assert into
       * a proper test. This presumably just means that the optimisation is
       * no longer done for the particular ISO job which has non-white text.
       */
      if ( dlc_is_white(dlc_current) ) {
        /* Source is white (FF,FF,FF). */
        attrib.rop = (attrib.rop & 0xcc) | ((attrib.rop & 0xcc) >> 2);
      }
    }

    /* If the foreground color is not used, normalise its value for
       debugging and ease of comparison. */
    if ( !pclROPRequiresTexture(attrib.rop) )
      attrib.foreground = PCL_PACKED_FG_NA ;
  }

  if ( first_try ) {
    pcl_xor_state = next_xor_state ;

    /* Detect XOR idioms before ROP simplification, and store the state in the
       PCL attrib, so the back-end knows when to flip interior colors. */
    switch ( attrib.rop ) {
    case PCL_ROP_DTx:
    case PCL_ROP_DSx:
    case PCL_ROP_Dn:
      switch ( pcl_xor_state ) {
      case PCL_XOR_OUTSIDE:
        HQASSERT(pcl_xor_count == 0, "Lost track of PCL XOR state") ;
        next_xor_state = pcl_xor_state = PCL_XOR_STARTING ;
        /*@fallthrough@*/
      case PCL_XOR_STARTING:
        ++pcl_xor_count ;
        break ;
      case PCL_XOR_INSIDE:
        next_xor_state = pcl_xor_state = PCL_XOR_ENDING ;
        /*@fallthrough@*/
      case PCL_XOR_ENDING:
        HQASSERT(pcl_xor_count > 0, "Lost track of PCL XOR state") ;
        if ( --pcl_xor_count == 0 )
          next_xor_state = PCL_XOR_OUTSIDE ;
        break ;
      }
      break ;
    default:
      if ( pcl_xor_state == PCL_XOR_STARTING ) {
        HQASSERT(pcl_xor_count > 0, "Lost track of PCL XOR state") ;
        next_xor_state = pcl_xor_state = PCL_XOR_INSIDE ;
      } else {
        HQASSERT((pcl_xor_state == PCL_XOR_INSIDE && pcl_xor_count > 0) ||
                 (pcl_xor_state == PCL_XOR_OUTSIDE && pcl_xor_count == 0),
                 "Lost track of PCL XOR state") ;
      }
      break ;
    }
  }
  attrib.xorstate = CAST_UNSIGNED_TO_UINT8(pcl_xor_state) ;

  /* Texture canonicalisation/optimisation. */
  if ( attrib.cacheEntry.pointer.pointer == NULL ) {
    /* If the pattern transparency cannot matter (because there is no
       pattern), then force it to FALSE, and simplify the ROP. */
    attrib.patternTransparent = FALSE ;

    /* Avoid re-packing color if the ROP is already T-agnostic. */
    if ( (attrib.rop >> 4) != (attrib.rop & 0xF) ) {
      if (attrib.foregroundSource != PCL_DL_COLOR_IS_FOREGROUND ||
          (/* Don't simplify past T unless forced to. */
           (attrib.rop != PCL_ROP_T) &&
           /* Don't simplify the ROP if it is used for tracking XOR idioms. */
           (attrib.rop != PCL_ROP_DTx)) ) {
        if ( foregroundIsWhite(page, &attrib) ) {
          attrib.rop = (attrib.rop & 0xF0) | ((attrib.rop & 0xF0) >> 4);
        } else if ( foregroundIsBlack(page, &attrib) ) {
          attrib.rop = (attrib.rop & 0x0F) | ((attrib.rop & 0x0F) << 4);
        }
      }
    }

    HQASSERT(attrib.patternColors == PCL_PATTERN_NONE,
             "Pattern colors set inappropriately") ;
  } else if ( attrib.cacheEntry.pointer.pointer == &erasePattern ) {
    /* The erase pattern is implemented as opaque white with transparency
       settings ignored. */
    attrib.sourceTransparent = FALSE ;
    attrib.patternTransparent = FALSE ;
    attrib.patternColors = PCL_PATTERN_NONE ;
    set_fg_white(&attrib, page, dlc_current) ;
    attrib.rop = (attrib.rop & 0xF0) | ((attrib.rop & 0xF0) >> 4);
  } else if ( attrib.cacheEntry.pointer.pointer == &invalidPattern ) {
    /* Objects drawn in the invalid pattern can be discarded. Setting the ROP
       to D will cause the DL color to be set to none, in turn this will drop
       the object before it is added to the DL. */
    attrib.rop = PCL_ROP_D ;
  } else if ( attrib.patternTransparent || pclROPRequiresTexture(attrib.rop) ) {
    /* There is a pattern, and it is required. Work out the colors used by
       the pattern. */
    PclPatternConstructionState *cstate = &pclGstate.constructionState ;

    getPclPatternColor(page->virtualDeviceSpace, &attrib, cstate) ;

    switch ( attrib.patternColors ) {
    case PCL_PATTERN_WHITE:
      if ( attrib.patternTransparent ) {
        /* If the pattern is completely white, and pattern transparency is
           on, then the object is completely transparent and can be
           ignored. */
        /** \todo ajcd 2013-07-10: What about the special (!sourceTransparent
            && sourceWhite) case in PCL 5 Color Technical Reference page
            5-10? */
        attrib.rop = PCL_ROP_D ;
      } else {
        /* The pattern is completely white, but non-transparent. Use a solid
           white foreground instead. */
        set_fg_white(&attrib, page, dlc_current) ;
        if (attrib.rop != PCL_ROP_T ||
            attrib.foregroundSource != PCL_DL_COLOR_IS_FOREGROUND)
          attrib.rop = (attrib.rop & 0xF0) | ((attrib.rop & 0xF0) >> 4);
      }
      attrib.patternColors = PCL_PATTERN_NONE ; /* Solid color */
      break ;
    case PCL_PATTERN_BLACK:
      /* If the pattern is all black, then transparency is irrelevant, and
         we can use a solid color instead of a pattern. */
      attrib.patternTransparent = FALSE ;
      attrib.patternColors = PCL_PATTERN_NONE ; /* Solid color */
      /* Simplify the ROP if possible. If this is not a color pattern, we
         use the foreground color instead of black. */
      if ( !cstate->useForeground ) {
        set_fg_black(&attrib, page, dlc_current) ;
        if (attrib.foregroundSource != PCL_DL_COLOR_IS_FOREGROUND ||
            (/* Don't simplify past T unless forced to. */
             (attrib.rop != PCL_ROP_T) &&
             /* Don't simplify the ROP if it is used for tracking XOR idioms. */
             (attrib.rop != PCL_ROP_DTx)) )
          attrib.rop = (attrib.rop & 0x0F) | ((attrib.rop & 0x0F) << 4);
      } else {
        if (attrib.foregroundSource != PCL_DL_COLOR_IS_FOREGROUND ||
            (/* Don't simplify past T unless forced to. */
             (attrib.rop != PCL_ROP_T) &&
             /* Don't simplify the ROP if it is used for tracking XOR idioms. */
             (attrib.rop != PCL_ROP_DTx)) ) {
          if ( foregroundIsWhite(page, &attrib) ) {
            attrib.rop = (attrib.rop & 0xF0) | ((attrib.rop & 0xF0) >> 4);
          } else if ( foregroundIsBlack(page, &attrib) ) {
            attrib.rop = (attrib.rop & 0x0F) | ((attrib.rop & 0x0F) << 4);
          }
        }
      }
      break ;
    case PCL_PATTERN_BLACK_AND_WHITE:
      if ( !cstate->useForeground ) {
        /* A color black and white pattern has been used. The foreground color
           may not have been set to black, do this now so we can use the
           pattern blit layer if the pattern is transparent. */
        set_fg_black(&attrib, page, dlc_current) ;
      } else {
        /* The pattern inherits the color of the foreground. If the foreground
           is not black, convert this to other and white. */
        if ( !foregroundIsBlack(page, &attrib) )
          attrib.patternColors = PCL_PATTERN_OTHER_AND_WHITE ;

        /** \todo ajcd 2013-07-11: What if the foreground is white? The
            algorithm on page 5-10 of the PCL 5 Color Technical Reference
            refers to "pattern == white" in the description of which spans to
            omit, rather than "texture == white". Texture is only used in the
            actual ROP operation input. Do we need a value for
            PCL_PATTERN_WHITE_AND_WHITE, or is it sufficient to test for FG
            white iff PCL_PATTERN_OTHER_AND_WHITE? */
      }

      /* If we are using ROP DTa, then the white sections of the pattern will
         not have any effect, and we can treat the pattern as transparent.
         Furthermore, if the foreground color shown through the pattern
         sections is black, then we can change the ROP to T, because DTa with
         T=0 is 0. */
      if ( attrib.rop == PCL_ROP_DTa) {
        attrib.patternTransparent = TRUE ;
        if ( attrib.patternColors == PCL_PATTERN_BLACK_AND_WHITE )
          attrib.rop = PCL_ROP_T ;
      }

      break ;
    case PCL_PATTERN_OTHER:
      /* If the pattern is all a single non-black, non-white color, then
         transparency is irrelevant, and we can use a solid color instead of
         a pattern. */
      attrib.patternTransparent = FALSE ;
      attrib.patternColors = PCL_PATTERN_NONE ; /* Solid color */
      /*@fallthrough@*/
    case PCL_PATTERN_OTHER_AND_WHITE:
      HQASSERT(!cstate->useForeground, "Black and white pattern has other color") ;
      /* We have an all-one-color pattern. Set this color as the foreground.
         We cannot simplify ROPs in this case. */
      if ( attrib.foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
        if ( !dlc_from_packed_color(page->dlc_context, dlc_current, cstate->patternColor) )
          return FALSE ;
      } else if ( attrib.foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB ) {
        attrib.foreground = cstate->patternColor;
      }
      break ;
    case PCL_PATTERN_NONE:
      /* No pattern. This must mean the pattern scaling is degenerate. Use
         solid black instead. */
      /** \todo ajcd 2013-07-11: Should we use FG instead if useForeground is
          true? */
      attrib.patternTransparent = FALSE ;
      set_fg_black(&attrib, page, dlc_current) ;
      if (attrib.foregroundSource != PCL_DL_COLOR_IS_FOREGROUND ||
          (/* Don't simplify past T unless forced to. */
           (attrib.rop != PCL_ROP_T) &&
           /* Don't simplify the ROP if it is used for tracking XOR idioms. */
           (attrib.rop != PCL_ROP_DTx)) )
        attrib.rop = (attrib.rop & 0x0F) | ((attrib.rop & 0x0F) << 4);
      break ;
    case PCL_PATTERN_MANY:
      /* The pattern does not contain white, we can force transparency to
         false because it won't make a difference.  */
      attrib.patternTransparent = FALSE ;
      /*@fallthrough@*/
    case PCL_PATTERN_ALL:
      /* Colored patterns with too many colors to optimise. */
      if ( !attrib.patternTransparent && !pclROPRequiresTexture(attrib.rop) ) {
        /* If the ROP doesn't require the texture and is not transparent,
           then we can just set it to a solid color. We choose black so that
           the DL color won't affect separation omission. */
        attrib.patternColors = PCL_PATTERN_NONE ;
        set_fg_black(&attrib, page, dlc_current) ;
      }
      break ;
    default:
      HQFAIL("Invalid pattern color setting") ;
      return error_handler(UNREGISTERED) ;
    }
  } else {
    /* There is a pattern, but it is not required because of the ROP. */
    HQASSERT(attrib.patternColors == PCL_PATTERN_NONE,
             "Pattern colors set inappropriately") ;
    HQASSERT(!attrib.patternTransparent, "Pattern should not be transparent") ;
    /* The texture is not used, however the DL color may contribute to
       separation omission. Normalise the foreground color to black for
       consistency. */
    set_fg_black(&attrib, page, dlc_current) ;
  }

  /* We have now simplified and normalised the attribute settings as much as
     we can, based on our current state of knowledge about the source and
     texture. We don't yet know anything about the destination. We do know
     that ROP D cannot affect the output in any way, so prepare for it to be
     discarded as soon as possible. */
  if ( attrib.rop == PCL_ROP_D ) {
    dlc_release(dlc_context, dlc_current);
    dlc_get_none(dlc_context, dlc_current);
    attrib.patternTransparent = FALSE ;
    attrib.sourceTransparent = FALSE ;
    attrib.backdrop = FALSE ;
    attrib.patternBlit = FALSE ;
  } else {
    if ( attrib.foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
      /* If the DL color is foreground, then the blit color will be set from
         it. If we're ROPping black or white, then we can set the DL color to
         that color, and let the normal opaque rendering (possibly plus the
         pattern filter) take care of rendering the right color. It doesn't
         matter if we do go via backdrop, the ROP will ensure that black or
         white is chosen regardless of us changing the foreground. If we're
         inside an XOR idiom, use rich black rather than process black for
         the color, we want to clear all channels. */
      if ( attrib.rop == PCL_ROP_BLACK ) {
        set_fg_black(&attrib, page, dlc_current) ;
      } else if ( attrib.rop == PCL_ROP_WHITE  ) {
        set_fg_white(&attrib, page, dlc_current) ;
      }
    }


#ifdef METRICS_BUILD
    if ( pcl_xor_state == PCL_XOR_STARTING ||
         pcl_xor_state == PCL_XOR_INSIDE ) {
      int32 flags = 0 ;

      switch ( attrib.patternColors ) {
      case PCL_PATTERN_BLACK_AND_WHITE:
        flags |= XOR_BLACK ;
        if ( !attrib.patternTransparent )
          flags |= XOR_WHITE ;
        break ;
      case PCL_PATTERN_OTHER_AND_WHITE:
        flags |= XOR_COLOR ;
        if ( !attrib.patternTransparent )
          flags |= XOR_WHITE ;
        break ;
      case PCL_PATTERN_MANY:
        flags |= XOR_BLACK|XOR_COLOR ;
        break ;
      case PCL_PATTERN_ALL:
        flags |= XOR_BLACK|XOR_WHITE|XOR_COLOR ;
        break ;
      }

      if ( attrib.foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ) {
        if ( foregroundIsWhite(page, &attrib) )
          flags |= XOR_WHITE ;
        else if ( foregroundIsBlack(page, &attrib) )
          flags |= XOR_BLACK ;
        else
          flags |= XOR_COLOR ;
      } else { /* Can't easily tell if image is B&W. Assume all colors. */
        flags = XOR_BLACK|XOR_COLOR ;
        if ( !attrib.sourceTransparent )
          flags |= XOR_WHITE ;
      }

      xor_metrics |= flags << (pcl_xor_state == PCL_XOR_INSIDE ? XOR_SHIFT : 0) ;
    } else if ( xor_metrics != 0 ) {
      pclattrib_metrics.xor_idioms[xor_metrics]++ ;
      xor_metrics = 0 ;
    }
#endif

    if ( !set_attrib_rendering(page, &attrib, dlc_current) )
      return FALSE ;
  }

  /* The insert copy constructs the DL pattern, which is rather late. */
  *result = (PclAttrib*)dlSSInsert(page->stores.pcl, &attrib.storeEntry, TRUE);
  if (*result == NULL)
    return FALSE;

#if METRICS_BUILD
  if ((*result)->patternColors != PCL_PATTERN_NONE)
    dl_metrics()->pcl.patternedObjects++;
#endif

  return TRUE;
}

/** Final stage of PCL attribute construction, to check how the surface
    wishes to render objects using an attribute. This is used both for
    initial construction of the PCL state, but also when updating the
    attributes as a result of idiom recognition. The attrib structure
    must be writable (i.e., not shared by objects on the DL). */
static Bool set_attrib_rendering(DL_STATE *page, PclAttrib *attrib,
                                 const dl_color_t *dlc)
{
  /* If the blit color comes from the DL color, can a normal opaque render
     be expected to get it right? */
  Bool blit_dlc = ((attrib->rop == PCL_ROP_T ||
                    attrib->rop == PCL_ROP_BLACK ||
                    attrib->rop == PCL_ROP_WHITE) &&
                   attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND);
  /* If the blit color comes from the object, can a normal opaque render be
     expected to get it right? */
  Bool blit_obj = (attrib->rop == PCL_ROP_S &&
                   attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB) ;

  /* We're going to store a flag in attrib indicating whether the blits are
     handled by the backdrop surface or by the output surface. The default
     case is that we can handle any case that can be set up to use a "normal"
     blit chain using the output surface. This is any blit that doesn't have
     source transparency (each source span would need examined), that doesn't
     need the destination in the ROP, and that needs only one of the texture
     or source in the ROP, and doesn't modify the color value. For these
     cases, we can set up a single color in the blit_color_t when rendering,
     and call the blit chain. */
  if ( attrib->sourceTransparent ||
       (attrib->rop != PCL_ROP_D && !blit_obj && !blit_dlc) ||
       (attrib->rop != PCL_ROP_D && pclROPRequiresDestination(attrib->rop)) ) {
    attrib->backdrop = TRUE ;
  } else {
    attrib->backdrop = FALSE ;
  }

  /* Set the default decision for whether we can use the pattern blit layer.
     The default pattern blit layer just filters out white spans from the
     pattern, so it can only be used when the pattern is transparent, when
     the pattern has one non-white color, and when the color we want to blit
     will be in the blit_color_t. The latter case indicates that either the
     source or the texture must be irrelevant: the other one should have been
     installed in the blit color. The backdrop doesn't have a pattern blit
     layer, so this won't take effect in that case. */
  if ( (blit_dlc || blit_obj) &&
       attrib->patternColors != PCL_PATTERN_NONE ) {
    attrib->patternBlit = TRUE ;
  } else {
    attrib->patternBlit = FALSE ;
  }

  /* Now check if the surface can override the backdrop or the pattern blit
     decisions. */
  if ( page->deviceROP && page->surfaces->rop_support != NULL ) {
#ifdef DEBUG_BUILD
    if ( (debug_pcl_idiom & DEBUG_PCL_FORCE_BACKDROP) == 0 )
#endif
      if ( !(*page->surfaces->rop_support)(page->sfc_page, page, dlc, attrib) )
        return error_handler(RANGECHECK) ; /* What should this error be? */
  }

  return TRUE ;
}

Bool foregroundIsBlack(const DL_STATE *page, const PclAttrib *self)
{
  HQASSERT(IS_INTERPRETER(), "Not testing correct foreground color") ;
  switch ( self->foregroundSource ) {
  case PCL_DL_COLOR_IS_FOREGROUND:
    return dlc_is_black(dlc_currentcolor(page->dlc_context),
                        page->virtualBlackIndex) ;
  case PCL_FOREGROUND_IN_PCL_ATTRIB:
    return packedColorIsBlack(page->virtualDeviceSpace,
                              self->foreground);
  default:
    HQFAIL("Unknown foregroundSource value");
    return FALSE;
  }
}

Bool foregroundIsWhite(const DL_STATE *page, const PclAttrib *self)
{
  HQASSERT(IS_INTERPRETER(), "Not testing correct foreground color") ;
  switch ( self->foregroundSource ) {
  case PCL_DL_COLOR_IS_FOREGROUND:
    return dlc_is_white(dlc_currentcolor(page->dlc_context)) ;
  case PCL_FOREGROUND_IN_PCL_ATTRIB:
    return packedColorIsWhite(page->virtualDeviceSpace,
                              self->foreground);
  default:
    HQFAIL("Unknown foregroundSource value");
    return FALSE;
  }
}

static Bool packedColorToObjectColor(DL_STATE *page, PclPackedColor packedColor,
                                     LISTOBJECT *object)
{
  dl_color_t dlColor;

  if ( !dlc_from_lobj(page->dlc_context, object, &dlColor) ||
       !dlc_from_packed_color(page->dlc_context, &dlColor, packedColor) )
    return FALSE;

  dl_release(page->dlc_context, &object->p_ncolor);
  return dlc_to_lobj(page->dlc_context, object, &dlColor);
}

static uint8 simplifyRopForWhiteDest(PclAttrib* attrib)
{
  uint8 rop = attrib->rop ;

  /* Cancelling out one input may result in other inputs being
     cancelled out too.  So before anything is cancelled make sure
     sourceTransparent and patternTransparent are such that any
     cancelling is allowed. */

  if ( attrib->sourceTransparent &&
       attrib->foregroundSource != PCL_DL_COLOR_IS_FOREGROUND )
    return rop;

  /** \todo should be able to relax this test */
  if ( attrib->patternTransparent )
    return rop;

  /* Can the ROP be simplified by cancelling out destination?
     The object's background is always RGB white (FF,FF,FF).
     Don't simplify the ROP if it is used for tracking XOR idioms. */
  if ( (rop != PCL_ROP_Dn &&
       (attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND
        ? rop != PCL_ROP_DTx
        : rop != PCL_ROP_DSx)) )
    rop = (rop & 0xAA) | ((rop & 0xAA) >> 1);

  return rop;
}

static LateColorAttrib *lcmAttribModify(DL_STATE *page, LISTOBJECT *object)
{
  LateColorAttrib newLCMAttrib;

  newLCMAttrib = *object->objectstate->lateColorAttrib;
  newLCMAttrib.blackType = BLACK_TYPE_MODIFIED;

  return (LateColorAttrib*)dlSSInsert(page->stores.latecolor,
                                      &newLCMAttrib.storeEntry,
                                      TRUE /* copy */);
}

static Bool updateAttrib(DL_STATE *page,
                         LISTOBJECT *object,
                         uint8 rop,
                         uint8 foregroundSource,
                         uint8 sourceTransparent,
                         uint8 patternTransparent,
                         uint8 patternColors,
                         uint8 xorstate,
                         Bool colorModified)
{
  STATEOBJECT *state = object->objectstate;
  STATEOBJECT newState = *state;
  dl_color_t dlc ;

  dlc_from_lobj_weak(object, &dlc) ;

  newState.pclAttrib = attribModify(page, state->pclAttrib, rop,
                                    foregroundSource, sourceTransparent,
                                    patternTransparent, patternColors,
                                    xorstate, &dlc);
  if ( !newState.pclAttrib )
    return FALSE;

  if ( colorModified ) {
    newState.lateColorAttrib = lcmAttribModify(page, object);
    if ( !newState.lateColorAttrib )
      return FALSE;
  }

  state = (STATEOBJECT*)dlSSInsert(page->stores.state, &newState.storeEntry, TRUE);
  if ( !state )
    return FALSE;

  object->objectstate = state;
  if ( rop == PCL_ROP_D )
    dl_to_none(page->dlc_context, &object->p_ncolor) ;

  return TRUE;
}

/**
 * Attempt to optimise the PclAttrib of the passed dlref, e.g. by simplifying
 * the rop if destination is white.  General rop simplification has already
 * been done when the pcl attribute was created in getPclAttrib.
 */
static void pclAttribOptimiseForWhiteDest(DL_STATE* page,
                                          LISTOBJECT* object,
                                          BitGrid* destinationRegions)
{
  PclAttrib* attrib = object->objectstate->pclAttrib;
  uint8 rop;

  HQASSERT(attrib != NULL, "Object has no PclAttrib.");

  if (destinationRegions != NULL &&
      bitGridGetBoxMapped(destinationRegions, &object->bbox) == BGAllClear) {
    /* Try to simplify the ROP by cancelling out operations
       based on a white destination. */
    rop = simplifyRopForWhiteDest(attrib);

    /* If the ROP could be simplified we'll need a new STATEOBJECT. */
    if (rop != attrib->rop) {
      /* We don't care if this fails - it's only an optimisation. */
      (void)updateAttrib(page, object, rop, attrib->foregroundSource,
                         attrib->sourceTransparent, attrib->patternTransparent,
                         attrib->patternColors, attrib->xorstate,
                         FALSE /*colorModified*/) ;
    }
  }
}

/* #define BACKDROP_RENDER_REASONS */
#if defined(DEBUG_BUILD)
#include "monitor.h"

const char *debug_string_pclPatternColors(int pc)
{
  static char colorstring[100] ;
  char *curr = colorstring ;

  if ( pc == PCL_PATTERN_NONE ) {
    strcpy(curr, "none") ;
    curr += strlen(curr) ;
  } else if ( pc & PCL_PATTERN_WHITE ) {
    strcpy(curr, "white") ;
    curr += strlen(curr) ;
  }

  if ( pc & ~PCL_PATTERN_WHITE ) {
    if ( curr != colorstring )
      *curr++ = '+' ;

    switch ( pc & ~PCL_PATTERN_WHITE ) {
    case PCL_PATTERN_BLACK:
      strcpy(curr, "black") ;
      break ;
    case PCL_PATTERN_OTHER:
      strcpy(curr, "other") ;
      break ;
    case PCL_PATTERN_MANY:
      strcpy(curr, "many") ;
      break ;
    default:
      strcpy(curr, "invalid") ;
      break ;
    }

    curr += strlen(curr) ;
  }

  *curr = '\0' ;
  return colorstring ;
}

const char *debug_string_pclXORState(int xs)
{
  const static char *strings[] = { "outside", "starting", "inside", "ending" };

  if ( xs >= NUM_ARRAY_ITEMS(strings) )
    return "invalid" ;
  return strings[xs] ;
}

#ifdef BACKDROP_RENDER_REASONS
void debugBackdropRenderReason(LISTOBJECT *object, BitGrid* destinationRegions)
{
  Bool whiteDest = (bitGridGetBoxMapped(destinationRegions, &object->bbox) == BGAllClear);
  dl_color_t dlc;

  dlc_from_dl_weak(object->p_ncolor, &dlc);

  /* This is useful for determining if more Rops can be special cased
     to avoid backdrop rendering. */
  monitorf((uint8*)"backdrop rendering (0x%X): opcode %d, rop %d, whiteDest %d, "
           "dlc (%d, %d, %d), ",
           object, object->opcode, object->objectstate->pclAttrib->rop,
           whiteDest, dlc.ce.pcv[0], dlc.ce.pcv[1], dlc.ce.pcv[2]);

  if ( object->objectstate->pclAttrib->dlPattern )
    PclDLPattern *dlPattern = object->objectstate->pclAttrib->dlPattern ;
    monitorf((uint8*)"patterned (%s), ",
             debug_string_pclPatternColors(object->objectstate->pclAttrib->patternColors));
  else
    monitorf((uint8*)"not patterned, ");
  monitorf((uint8*)"bbox (%d, %d, %d, %d)\n",
           object->bbox.x1, object->bbox.y1, object->bbox.x2, object->bbox.y2);
}
#endif
#endif /* DEBUG_BUILD */

#ifndef BACKDROP_RENDER_REASONS
#define debugBackdropRenderReason(_object, _destinationRegions) \
  EMPTY_STATEMENT()
#endif

static void updateRegionMap(DL_STATE *page, LISTOBJECT *object,
                            BitGrid *destinationRegions,
                            BitGrid *backdropRegions)
{
  PclAttrib *attrib = object->objectstate->pclAttrib;
  dl_color_t dlc;
#ifdef DEBUG_BUILD
  static Bool printing = FALSE ;
#endif

  /* An early return for 5e which doesn't enable OverprintPreview and therefore
     doesn't have any region maps. */
  if ( page->pcl5eModeEnabled ) {
    pcl_metrics_count_rop(ROP_METRIC_DIRECT, attrib, page) ;
    return;
  }

  /* As a last resort, if destination is white it may still be possible to
     simplify the rop and avoid compositing.  If we've partial painted then
     we don't really know what the destination is. */
  if ( attrib->backdrop && !page->rippedtodisk )
    pclAttribOptimiseForWhiteDest(page, object, destinationRegions);

#ifdef DEBUG_BUILD
# ifdef DEBUG_TRACK_SETG
  objnum = object->setg_count ;
# else
  ++objnum ;
# endif

  if ( attrib->backdrop && (debug_pcl_idiom & DEBUG_PCL_IDIOM_PRINT_BD) != 0 )
    printing = TRUE ;

  if ( printing ) {
    monitorf((uint8 *)"OBJ %d op=%s rop=%d [%d,%d %d,%d] ST=%s PT=%s FS=%s FC=%s PAT=%s PC=%s XS=%s BD=%s PB=%s\n",
             objnum, debug_opcode_names[object->opcode], attrib->rop,
             object->bbox.x1, object->bbox.y1, object->bbox.x2, object->bbox.y2,
             attrib->sourceTransparent ? "true" : "false",
             attrib->patternTransparent ? "true" : "false",
             attrib->foregroundSource == PCL_FOREGROUND_NOT_SET ? "not_set" :
             attrib->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND ? "dl_color" :
             attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB ? "pcl_attrib" : "invalid",
             foregroundIsBlack(page, attrib) ? "black" :
             foregroundIsWhite(page, attrib) ? "white" : "other",
             attrib->dlPattern ? "true" : "false",
             debug_string_pclPatternColors(attrib->patternColors),
             debug_string_pclXORState(attrib->xorstate),
             attrib->backdrop ? "true" : "false",
             attrib->patternBlit ? "true" : "false"
             ) ;
    if ( !attrib->backdrop ) {
      /* Turn off idiom printing when we reach a "normal" ROP. */
      switch ( attrib->rop ) {
      case 204: /* S */
      case 240: /* T */
      case 252: /* TSo */
        printing = FALSE ;
        break ;
      }
    }
  }
#endif

  if ( attrib->backdrop ) {
#ifdef DEBUG_BUILD
    if ( (debug_pcl_idiom & DEBUG_PCL_SKIP_ROP_BD) != 0 ) {
      dl_to_none(page->dlc_context, &object->p_ncolor) ;
      return ;
    }
#endif

    HQASSERT(!(page->rippedtodisk && pclROPRequiresDestination(attrib->rop)),
             "ROP requires destination but already partial-painted destination"
             " - output may be wrong");

    /* No alternative - need to backdrop render the object with the original ROP. */
    bitGridSetBoxMapped(backdropRegions, &object->bbox, TRUE);

    /* Destination will probably be non-white. */
    bitGridSetBoxMapped(destinationRegions, &object->bbox, TRUE);

    debugBackdropRenderReason(object, destinationRegions);

    pcl_metrics_count_rop(ROP_METRIC_BACKDROP, attrib, page) ;
  } else { /* Direct render */
#ifdef DEBUG_BUILD
    if ( (debug_pcl_idiom & DEBUG_PCL_SKIP_ROP_DIRECT) != 0 ) {
      dl_to_none(page->dlc_context, &object->p_ncolor) ;
      return ;
    }
#endif

    /* A none colour means the object is completely ignored - nothing more to do. */
    if ( dl_is_none(object->p_ncolor) )
      return;

    /* Check for a non-white object and update the destination regions. */
    dlc_from_lobj_weak(object, &dlc);
    if ( attrib->foregroundSource != PCL_DL_COLOR_IS_FOREGROUND ||
         attrib->rop != PCL_ROP_T ||
         !dlc_is_white(&dlc) )
      bitGridSetBoxMapped(destinationRegions, &object->bbox, TRUE);
    pcl_metrics_count_rop(ROP_METRIC_DIRECT, attrib, page) ;
  }
}

static void changeImageTo(DL_STATE *page, LISTOBJECT *object, uint8 opcode)
{
  IMAGEOBJECT *image = object->dldata.image ;

  HQASSERT(opcode == RENDER_void || opcode == RENDER_rect,
           "Can't change image to anything but rect or void") ;

  im_freeobject(image, page);
  object->dldata.image = NULL;

  object->opcode = opcode;
}

/**
 * Check for a uniform image and convert it to a rect.  Don't do uniform image
 * replacement for ropped images that may match an idiom.  If it's a masked
 * image then it's better to leave it alone.  The purpose is to save memory.
 */
static void checkForUniformImage(DL_STATE *page, LISTOBJECT *object)
{
#define MAX_NCOMPS 4
  COLORVALUE uniformcvs[MAX_NCOMPS] = { 0 } ;
  PclAttrib *attrib = object->objectstate->pclAttrib ;
  uint8 rop = attrib->rop;
  dl_color_t dlc_new;
  IMAGEOBJECT *image = object->dldata.image ;

  /* If the source color (from the image data) is not required by the ROP, then
     simplify the image. */
  if ( image->mask ||
       (pclROPRequiresSource(rop) &&
        !im_expanduniform(image->ime, image->ims, uniformcvs, MAX_NCOMPS)) )
    return;

  /* Image is uniform so replace with a rectfill. Replace dl color with a new
     one containing the uniform color contained in the image store. Object
     state is mostly unchanged so correct repro intent and spotno is still
     used. Only the black state info has to be marked as possibly being
     modified. Because we're changing the assumptions under which the
     back-end works (that the blit color comes from the image data and that
     the DL color is not the foreground color), we have to swap the source
     and texture in the object and the ROP. Since a rect has source black by
     definition, if the texture matters, the foreground must be black to make
     the swap valid. */
  HQASSERT(attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB,
           "Foreground is not in PCL attrib") ;

  /* We either need a solid texture, or a transparent black and white
     pattern. */
  if ( attrib->patternColors != PCL_PATTERN_NONE &&
       !(attrib->patternTransparent &&
         attrib->patternColors == PCL_PATTERN_BLACK_AND_WHITE) )
    return ;

  /* If the texture is required, then the foreground color must be black. (If
     not, it could be anything, but should have been simplified to black by
     getPclAttrib() anyway.) */
  if ( pclROPRequiresTexture(attrib->rop) &&
       !packedColorIsBlack(page->virtualDeviceSpace,
                           attrib->foreground) )
    return ;

  dlc_clear(&dlc_new);

  if ( pclROPRequiresSource(rop) ) {
    int32 ncomps ;
    dl_color_t dlc_old ;

    dlc_from_dl_weak(object->p_ncolor, &dlc_old);
    ncomps = DLC_NUM_CHANNELS(&dlc_old);
    HQASSERT(ncomps <= MAX_NCOMPS, "PCL should be max 4 ncomps");

    if ( !dlc_alloc_fillin_template(page->dlc_context, &dlc_new, &dlc_old,
                                    uniformcvs, ncomps) )
      return;
  } else {
    dlc_get_black(page->dlc_context, &dlc_new) ;
  }

  /* If the uniform color is white, and source transparency is on, this
     object is totally transparent. */
  if ( attrib->sourceTransparent && dlc_is_white(&dlc_new) ) {
    dl_to_none(page->dlc_context, &object->p_ncolor) ;
  } else {
    /* We're changing from foreground in PCL attrib to foreground in DL color,
       and from source in object to source black. Change the ROP to match.
       The ROP number is the solution to the truth table:

        T Texture      1 1 1 1 0 0 0 0
        S Source       1 1 0 0 1 1 0 0
        D Destination  1 0 1 0 1 0 1 0
        ROP Code       ? ? ? ? ? ? ? ?

       So changing S and T can be achieved by swapping the middle two pairs
       of bits. However, having done the swap, we know that S will be black,
       so we can further simplify and combine the operations in one. */
    rop = (rop & 0x03) | ((rop << 2) & 0x3c) | ((rop << 4) & 0xc0) ;

    /* Now check if we can simplify the ROP with the new texture color. */
    if (/* Don't simplify past T unless forced to. */
        (rop != PCL_ROP_T) &&
        /* Don't simplify the ROP if it is used for tracking XOR idioms. */
        (rop != PCL_ROP_DTx) ) {
      if ( dlc_is_white(&dlc_new) ) {
        rop = (rop & 0xF0) | ((rop & 0xF0) >> 4);
      } else if ( dlc_is_black(&dlc_new, page->virtualBlackIndex) ) {
        rop = (rop & 0x0F) | ((rop & 0x0F) << 4);
      }
    }

    dl_release(page->dlc_context, &object->p_ncolor);
    dlc_to_lobj_release(object, &dlc_new);

    /* The color has changed. Update the black state in LCM attribs of the
       objectstate. */
    if ( !updateAttrib(page, object, rop,
                       PCL_DL_COLOR_IS_FOREGROUND /*foreground source*/,
                       FALSE /*sourceTransparent*/,
                       attrib->patternTransparent,
                       attrib->patternColors,
                       attrib->xorstate,
                       TRUE /*colorModified*/))
      return;

    /* It looks like a rect on the artwork, so looks best with that screen and
       color management. Therefore, change the type to Fill. Also means this
       idiom doesn't need to mark the halftone cache, because either (if not
       compositing) it's been done already identically for images and linework,
       or it will be done by compositing/preconversion (but the latter wouldn't
       if this was left as GSC_IMAGE). */
    DISPOSITION_STORE(object->disposition, REPRO_TYPE_OTHER, GSC_FILL,
                      (object->disposition & DISPOSITION_FLAG_USER) == 0 ? 0 : 1);
  }

  changeImageTo(page, object, RENDER_rect) ;
}

static void flushHead(DL_STATE *page)
{
  HQASSERT(page->pclIdiomQueue.nparts > 0, "No parts on idiom queue");
  page->pclIdiomQueue.nparts-- ;
  page->pclIdiomQueue.first = (page->pclIdiomQueue.first + 1) % NUM_ARRAY_ITEMS(page->pclIdiomQueue.parts) ;
}

static void flushQueue(DL_STATE *page, uint32 numParts)
{
  HQASSERT(page->pclIdiomQueue.nparts >= numParts,
           "Flushing more parts than on the queue");

  while ( numParts > 0 ) {
    flushHead(page);
    --numParts;
  }
}

static Bool checkForIdioms(DL_STATE *page, uint32 *numParts);

void pclIdiomFlush(DL_STATE *page)
{
  while ( page->pclIdiomQueue.nparts > 0 ) {
    uint32 numParts;

    if ( checkForIdioms(page, &numParts) )
      flushQueue(page, numParts);
    else
      flushHead(page);
  }
}

static Bool bbox_is_adjacent(dbbox_t *bb1, dbbox_t *bb2)
{
  if ( bb1->x2 + 1 == bb2->x1 || bb2->x2 + 1 == bb1->x1 ) {
    return (bb1->y1 == bb2->y1 && bb1->y2 == bb2->y2) ;
  } else if ( bb1->y2 + 1 == bb2->y1 || bb2->y2 + 1 == bb1->y1 ) {
    return (bb1->x1 == bb2->x1 && bb1->x2 == bb2->x2) ;
  }
  return FALSE ;
}

static inline Bool opcode_is_fill_variant(uint8 opcode)
{
  return opcode == RENDER_rect || opcode == RENDER_fill ||
         opcode == RENDER_quad;
}

/** Add a new object to the PCL idiom queue.
    \param page  The DL to which the object was added.
    \param dlref The DL reference of the object.
    \retval TRUE The object extended the PCL idiom queue.
    \retval FALSE The object combined with previous idiom part. */
static Bool addToQueue(DL_STATE *page, DLREF *dlref)
{
  LISTOBJECT *object = dlref_lobj(dlref) ;
  LISTOBJECT *last = page->pclIdiomQueue.last ;

  page->pclIdiomQueue.last = object ;
  if ( page->pclIdiomQueue.nparts == 0 ) {
    page->pclIdiomQueue.first = 0 ;
    page->pclIdiomQueue.parts[0].head = dlref;
    page->pclIdiomQueue.parts[0].bbox = object->bbox;
    page->pclIdiomQueue.parts[0].nobj = 1;
  } else {
    /** \todo ajcd 2013-08-05: For now, we're not combining objects, so
        just add the object as a new part. */
    uint32 index = (page->pclIdiomQueue.first + page->pclIdiomQueue.nparts - 1)
      % NUM_ARRAY_ITEMS(page->pclIdiomQueue.parts) ;
    PclAttrib *attrib = object->objectstate->pclAttrib ;

    if ( last != NULL &&
         last->objectstate->pclAttrib->xorstate == attrib->xorstate ) {
      if ( attrib->xorstate == PCL_XOR_STARTING ||
           attrib->xorstate == PCL_XOR_ENDING ) {
        pcl_idiom_part *part = &page->pclIdiomQueue.parts[index] ;
        if ( (object->opcode == RENDER_image ||
              object->opcode == RENDER_rect) &&
             bbox_is_adjacent(&part->bbox, &object->bbox) ) {
          bbox_union(&part->bbox, &object->bbox, &part->bbox);
          part->nobj += 1;
          return FALSE ;
        }
      } else if ( attrib->xorstate == PCL_XOR_INSIDE ) {
        pcl_idiom_part *part = &page->pclIdiomQueue.parts[index] ;
        /* We can only construct a unified NFILLOBJECT for the clipping
           polygons if they don't overlap. */
        if ( opcode_is_fill_variant(object->opcode) &&
             !bbox_intersects(&part->bbox, &object->bbox) ) {
          bbox_union(&part->bbox, &object->bbox, &part->bbox);
          part->nobj += 1;
          return FALSE ;
        }
      }
    }

    /* Not combining with previous idiom part, so start a new part. */
    index = (index + 1) % NUM_ARRAY_ITEMS(page->pclIdiomQueue.parts) ;
    HQASSERT(page->pclIdiomQueue.nparts < NUM_ARRAY_ITEMS(page->pclIdiomQueue.parts),
             "Overflowed idiom queue") ;
    page->pclIdiomQueue.parts[index].head = dlref;
    page->pclIdiomQueue.parts[index].bbox = object->bbox;
    page->pclIdiomQueue.parts[index].nobj = 1;
  }

  page->pclIdiomQueue.nparts++ ;
  return TRUE ;
}

/* Loads the "objects" array from the DL linked-list.  Note it deliberately
   loads starting from the head of the list (i.e.  it chooses the older DL
   objects in preference to the newest).  This has the effect of giving the
   idioms involving multiple objects a chance to match before those idioms
   involving fewer objects. */
static void getNParts(DL_STATE *page, uint32 numParts, pcl_idiom_part *parts[])
{
  uint32 index = page->pclIdiomQueue.first ;

  HQASSERT(numParts > 0, "No objects requested for idiom") ;
  do {
    *parts = &page->pclIdiomQueue.parts[index] ;
    HQASSERT(!dl_is_none(dlref_lobj(page->pclIdiomQueue.parts[index].head)->p_ncolor),
             "None color object passed to idiom") ;
    index = (index + 1) % NUM_ARRAY_ITEMS(page->pclIdiomQueue.parts) ;
    ++parts ;
  } while ( --numParts > 0 ) ;
}

/**
 * Check for following idiom:
 * 0: rect/fill with rop DTx/Dn and desired colour
 * 1: another rect/fill with black and white pattern and rop DTa
 * 2: same rect/fill as first with rop DTx/Dn and desired colour
 */
static Bool checkForTextureClipIdiom(DL_STATE *page, pcl_idiom_part *parts[])
{
  dl_color_t dlc;
  PclAttrib *attr0, *attr1, *attr2 ;
  LISTOBJECT *objects[3] ;

  if ( parts[0]->nobj != 1 || parts[1]->nobj != 1 || parts[2]->nobj != 1 )
    return FALSE ;

  objects[0] = dlref_lobj(parts[0]->head) ;
  objects[1] = dlref_lobj(parts[1]->head) ;
  objects[2] = dlref_lobj(parts[2]->head) ;

  /* First and third objects are supposed to match. */
  if ( !(objects[0]->opcode == objects[2]->opcode &&
         opcode_is_fill_variant(objects[0]->opcode)) )
    return FALSE;

  if ( !bbox_equal(&parts[0]->bbox, &parts[2]->bbox) )
    return FALSE;

  /* Second object is allowed to be a fill (in which case
     we can do augment_clipping below), otherwise the second
     object must match the first and third objects. */
  if ( !(objects[0]->opcode == objects[1]->opcode &&
         bbox_equal(&parts[0]->bbox, &parts[1]->bbox)) &&
       objects[1]->opcode != RENDER_fill )
    return FALSE;

  attr0 = objects[0]->objectstate->pclAttrib ;
  attr1 = objects[1]->objectstate->pclAttrib ;
  attr2 = objects[2]->objectstate->pclAttrib ;

  HQASSERT(attr1->foregroundSource == PCL_DL_COLOR_IS_FOREGROUND,
           "DL color not in foreground of rect/fill object") ;

  /* First and last objects must use the same ROP */
  if ( attr0->rop != attr2->rop )
    return FALSE ;

  /* First and last object ROP must be DTx or Dn. */
  if ( attr0->rop != PCL_ROP_DTx && attr0->rop != PCL_ROP_Dn )
    return FALSE ;

  /* First and last objects must not be patterned */
  if ( attr0->patternColors != PCL_PATTERN_NONE ||
       attr2->patternColors != PCL_PATTERN_NONE )
    return FALSE;

  /* First and last object (texture) color must match. */
  if ( !dl_equal(objects[0]->p_ncolor, objects[2]->p_ncolor) )
    return FALSE;

  /* The middle object must be drawn with a black and white pattern. The DTa
     ROP will clear the black bits in the pattern. The subsequent DTx will
     show the texture through the bits that were cleared. */
  if ( attr1->patternColors != PCL_PATTERN_BLACK_AND_WHITE )
    return FALSE;

  /* Middle object must be drawn using the DTa ROP, or must be T and
     transparent. */
  if ( attr1->rop != PCL_ROP_DTa &&
       (attr1->rop != PCL_ROP_T || !attr1->patternTransparent) )
    return FALSE ;

  /* We could also check that the second object is drawn using a
     transparent black pattern with ROP 0 (black), or with a transparent
     black pattern and ROP T with a black DL color, but these cases are
     unlikely. If the creator had the sophistication to generate them, it
     would have generated the pattern with the right foreground color in
     the first place. */

  /* If the replaced object color would be white we can't apply this idiom;
     since we have to set pattern transparency to true the entire object will
     be transparent if backdrop rendered (remember that some other object may
     cause this object to be backdrop rendered after replacement). */
  dlc_from_dl_weak(objects[0]->p_ncolor, &dlc);
  if (dlc_is_white(&dlc))
    return FALSE;

  /* Matches idiom so don't need to backdrop render!
   *
   * Patch the rop of the second object to T and copy the DL colour from the
   * first object to the second (replacing the second's black colour).  This
   * means we will paint the source colour wherever the pattern is black.  Then
   * make the first and third objects invisible.
   */

  /** \todo ajcd 2013-07-29: NOTE that we have seen cases where the middle
      object extends beyond the extent of the outer objects. In this case,
      this transformation is not equivalent, but is almost certainly what the
      driver writer intended. */
  HQTRACE(!bbox_contains(&parts[0]->bbox, &parts[1]->bbox),
          ("PCL Clip fill idiom applied to overextended pattern clip:"
           "  object [%d %d %d %d] pattern [%d %d %d %d]",
           parts[0]->bbox.x1, parts[0]->bbox.y1,
           parts[0]->bbox.x2, parts[0]->bbox.y2,
           parts[1]->bbox.x1, parts[1]->bbox.y1,
           parts[1]->bbox.x2, parts[1]->bbox.y2)) ;

  {
    Bool tblack = dlc_is_black(&dlc, page->virtualBlackIndex) ;

    /* Swap colors of first and middle objects directly, so we don't have to
       allocate another reference. */
    if ( tblack ) {
      dl_to_black(page->dlc_context, &objects[1]->p_ncolor) ;
    } else {
      p_ncolor_t ncolor = objects[1]->p_ncolor ;
      objects[1]->p_ncolor = objects[0]->p_ncolor ;
      objects[0]->p_ncolor = ncolor ;
    }

    dl_to_none(page->dlc_context, &objects[0]->p_ncolor);
    dl_to_none(page->dlc_context, &objects[2]->p_ncolor);

    /* We'll assign a color to the patterned object, so we need to treat its
       pattern as being other and white. We need to say that the object is
       outside the XOR idiom to avoid flipping black to white in subtractive
       rendering. */
    if ( !updateAttrib(page, objects[1], PCL_ROP_T,
                       PCL_DL_COLOR_IS_FOREGROUND /*no change*/,
                       FALSE /*sourceTransparent*/,
                       TRUE /*patternTransparent*/,
                       tblack ? PCL_PATTERN_BLACK_AND_WHITE
                       : PCL_PATTERN_OTHER_AND_WHITE,
                       PCL_XOR_OUTSIDE,
                       TRUE /*colorModified*/) )
      return FALSE;
  }

  return TRUE;
}

/**
 * Image or fill clipping idiom.
 *
 * Check for the following objects:
 * 0: image with rop 102 (DSx) or rect with rop 90 (DTx)
 * 1: unpatterned polygons in black with rop 0 (black)
 * 2: same image with rop 102 (DSx) or same rect with rop 90 (DTx)
 *
 * The driver is trying to draw an image or rect and clip it to the polygon.
 * The destination is irrelevant because if you print the same image/rect
 * twice with DSx/DYx then you always get back the destination. If you draw a
 * black object between the images/rects then you get back the destination
 * where there is no black and the image/rect where there is black.
 */
static Bool checkForClipIdiom(DL_STATE *page, pcl_idiom_part *parts[])
{
  NFILLOBJECT *nfill ;
  DLREF *ref_0, *ref_1, *ref_2 ;
  uint32 i ;
  int32 nthreads ;
  size_t nbresssize ;
  int32 nfilltype ;
  uint8 converter ;

  /* Same number of objects in first and last part. */
  if ( parts[0]->nobj != parts[2]->nobj )
    return FALSE ;

  /* First and last part have same bbox. */
  if ( !bbox_equal(&parts[0]->bbox, &parts[2]->bbox) )
    return FALSE;

  for ( i = parts[0]->nobj, ref_0 = parts[0]->head, ref_2 = parts[2]->head ;
        i > 0 ;
        --i, ref_0 = dlref_next(ref_0), ref_2 = dlref_next(ref_2) ) {
    LISTOBJECT *obj0, *obj2 ;
    PclAttrib *attr0, *attr2 ;

    HQASSERT(ref_0 != NULL && ref_2 != NULL , "No DL reference in PCL idiom") ;

    obj0 = dlref_lobj(ref_0) ;
    obj2 = dlref_lobj(ref_2) ;

    /* First and last objects must be the same type */
    if ( obj0->opcode != obj2->opcode )
      return FALSE;

    /* Each object in part must have same bbox. */
    if ( !bbox_equal(&obj0->bbox, &obj2->bbox) )
      return FALSE;

    attr0 = obj0->objectstate->pclAttrib ;
    attr2 = obj2->objectstate->pclAttrib ;

    /* First and last objects must use the same ROP */
    if ( attr0->rop != attr2->rop )
      return FALSE ;

    /* First and third objects are either DTx/Dn rect, or DSx/Dn image. */
    if ( obj0->opcode == RENDER_rect ) {
      /* Rects must be DTx or Dn. */
      if ( attr0->rop != PCL_ROP_DTx && attr0->rop != PCL_ROP_Dn )
        return FALSE ;

      /* First and last object (texture) color must match. */
      if ( !dl_equal(obj0->p_ncolor, obj2->p_ncolor) )
        return FALSE;
    } else if ( obj0->opcode == RENDER_image ) {
      /* Images must be DSx. */
      if ( attr0->rop != PCL_ROP_DSx )
        return FALSE ;

    } else
      return FALSE ;
  }

  /* Count the size of the NFILLOBJECT we'll need to construct for the
     unified clipping polygon, and extract the object type and converter. */
  nthreads = 0 ;
  nbresssize = 0 ;
  nfilltype = 0 ;
  converter = 0 ;
  for ( i = parts[1]->nobj, ref_1 = parts[1]->head ;
        i > 0 ;
        --i, ref_1 = dlref_next(ref_1) ) {
    LISTOBJECT *obj1 ;
    PclAttrib *attr1 ;
    int32 j ;

    HQASSERT(ref_1 != NULL, "No DL reference in PCL idiom") ;

    obj1 = dlref_lobj(ref_1) ;
    attr1 = obj1->objectstate->pclAttrib ;

    /* Middle objects cannot be patterned. */
    if ( attr1->patternColors != PCL_PATTERN_NONE )
      return FALSE;

    /* Middle objects must be black. */
    if ( attr1->rop == PCL_ROP_T ) {
      dl_color_t dlc ;
      dlc_from_dl_weak(obj1->p_ncolor, &dlc);
      if (!dlc_is_black(&dlc, page->virtualBlackIndex))
        return FALSE;
    } else if ( attr1->rop != PCL_ROP_BLACK ) {
      return FALSE ;
    }

    /* Middle objects must be variants of fill. */
    switch ( obj1->opcode ) {
    case RENDER_rect:
      if ( parts[1]->nobj > 1 ) {
        /* Convert rect to NFILL to count the thread sizes. */
        NFILLOBJECT rectfill ;
        NBRESS rectthreads[2] ;
        rect_to_nfill(&obj1->bbox, &rectfill, &rectthreads[0]) ;
        for ( j = 0; j < rectfill.nthreads ; ++j )
          nbresssize += sizeof_nbress(&rectthreads[j]) ;
        nthreads += 2 ; /* Left and right thread. */
      }
      break ;
    case RENDER_fill:
      if ( parts[1]->nobj > 1 ) {
        NFILLOBJECT *oldnfill = obj1->dldata.nfill ;
        for ( j = 0; j < oldnfill->nthreads ; ++j )
          nbresssize += sizeof_nbress(oldnfill->thread[j]) ;
        nthreads += oldnfill->nthreads ;
        if ( nfilltype != 0 ) {
          /* The fill rules and scan conversion rules must match, or we can't
             match the objects. */
          if ( nfilltype != oldnfill->type ||
               converter != oldnfill->converter )
            return FALSE ;
        }
        nfilltype = oldnfill->type ;
        converter = oldnfill->converter ;
      }
      break ;
    case RENDER_quad:
      { /* Convert quad to NFILL to count the threads. */
        NFILLOBJECT quadfill ;
        NBRESS quadthreads[4] ;
        quad_to_nfill(obj1, &quadfill, &quadthreads[0]) ;
        for ( j = 0; j < quadfill.nthreads ; ++j )
          nbresssize += sizeof_nbress(&quadthreads[j]) ;
        nthreads += quadfill.nthreads ;
      }
      break ;
    default:
      return FALSE ;
    }
  }

  /* Matches idiom so don't need to backdrop render!
   *
   * The idiom is just trying to clip the images/rects to the polygons, so
   * augment the second image/rect's clipping with the polygons and turn off
   * ropping. The first image/rect's dl colour can be set to none so it will
   * be completely ignored. The black polygons still needs to be drawn in case
   * the images/rects don't cover the polygon clip completely. In these areas
   * the black polygon shows through.
   *
   * We start by collecting the polygons together into one NFILLOBJECT
   * representing the union of the clipping.
   */

#ifdef DEBUG_BUILD
  if ( (debug_pcl_idiom & DEBUG_PCL_IDIOM_EXTENDED_PRINT) != 0 &&
       (parts[0]->nobj != 1 || parts[1]->nobj != 1) ) {
    monitorf((uint8 *)("Multiple objects inside PCL idiom: "
                       "%d x %d [%d %d %d %d] %d [%d %d %d %d]\n"),
             parts[0]->nobj, dlref_lobj(parts[0]->head)->opcode,
             parts[0]->bbox.x1, parts[0]->bbox.y1,
             parts[0]->bbox.x2, parts[0]->bbox.y2,
             parts[1]->nobj,
             parts[1]->bbox.x1, parts[1]->bbox.y1,
             parts[1]->bbox.x2, parts[1]->bbox.y2) ;
  }
#endif

  if ( nthreads > 0 ) {
    /* If the clip is solely quads and rects, set the default type and
       conversion rules. */
    if ( nfilltype == 0 ) {
      nfilltype = NZFILL_TYPE ;
      converter = SC_RULE_HARLEQUIN ;
    }

    if ( (nfill = nfill_preallocate(page, nbresssize, nthreads,
                                    nfilltype, converter)) == NULL )
      return FALSE ;
  } else {
    nfill = NULL ;
  }

  for ( i = parts[1]->nobj, ref_1 = parts[1]->head ;
        i > 0 ;
        --i, ref_1 = dlref_next(ref_1) ) {
    LISTOBJECT *obj1 = dlref_lobj(ref_1) ;

    switch ( obj1->opcode ) {
    case RENDER_rect:
      if ( nthreads > 0 ) {
        NFILLOBJECT rectfill ;
        NBRESS rectthreads[2] ;
        dbbox_t bbox = obj1->bbox ;
        if ( converter == SC_RULE_TESSELATE ) {
          bbox.x2 += 1 ;
          bbox.y2 += 1 ;
        }
        rect_to_nfill(&bbox, &rectfill, &rectthreads[0]) ;
        nfill_append(page, nfill, &rectfill) ;
      } /* else augment_clipping will handle NULL fill for rect */
      break ;
    case RENDER_fill:
      if ( nthreads > 0 ) {
        nfill_append(page, nfill, obj1->dldata.nfill) ;
      } else {
        nfill = obj1->dldata.nfill ;
      }
      break ;
    case RENDER_quad:
      { /* Build an NFILL, and copy it. */
        NFILLOBJECT quadfill ;
        NBRESS quadthreads[4] ;
        quad_to_nfill(obj1, &quadfill, &quadthreads[0]) ;
        nfill_append(page, nfill, &quadfill) ;
      }
      break ;
    default:
      HQFAIL("Should have already filtered out non-fill objects") ;
      return FALSE ;
    }
  }

  /* We're going to return TRUE even if clipping or attribute updates fail
     from here on, because we want to consume the objects from the idiom
     queue. We won't bother freeing the nfill we constructed, it'll get
     released with the page anyway. */
  for ( i = parts[0]->nobj, ref_0 = parts[0]->head, ref_2 = parts[2]->head ;
        i > 0 ;
        --i, ref_0 = dlref_next(ref_0), ref_2 = dlref_next(ref_2) ) {
    LISTOBJECT *obj0 = dlref_lobj(ref_0) ;
    LISTOBJECT *obj2 = dlref_lobj(ref_2) ;
    PclAttrib *attr2 = obj2->objectstate->pclAttrib ;

    if ( !augment_clipping(page, obj2, nfill, &parts[1]->bbox) ||
         !updateAttrib(page, obj2,
                       obj2->opcode == RENDER_image ? PCL_ROP_S : PCL_ROP_T,
                       attr2->foregroundSource,
                       attr2->sourceTransparent,
                       attr2->patternTransparent,
                       attr2->patternColors,
                       attr2->xorstate,
                       FALSE /*colorModified*/) )
      return TRUE;

    /* Successfully updated last object, so dispose of first. */
    if ( obj0->opcode == RENDER_image )
      changeImageTo(page, obj0, RENDER_void) ;
    dl_to_none(page->dlc_context, &obj0->p_ncolor);
  }

  for ( i = parts[1]->nobj, ref_1 = parts[1]->head ;
        i > 0 ;
        --i, ref_1 = dlref_next(ref_1) ) {
    LISTOBJECT *obj1 = dlref_lobj(ref_1) ;
    /* The clipping polygons may be larger than the extent of the final part,
       in which case we don't want to set them to None, because bits of them
       will show around the edges. */
    if ( bbox_contains(&parts[2]->bbox, &obj1->bbox) )
      dl_to_none(page->dlc_context, &obj1->p_ncolor) ;
  }

  return TRUE;
}

/**
 * Image masking idiom.
 *
 * Check for the following objects:
 * 0: Image with rop DSx
 * 1: 1 bit image with rop DSa
 * 2: Image (same as Image 0) with rop DSx
 *
 * This is a variation of the image clipping idiom.  Instead of a black fill the
 * image is clipped to an imagemask.  Since the imagemask contains white and
 * black bits the driver applies rop DSa to the imagemask, but the principle is
 * the same as the clip.
 */
static Bool checkForImageMaskIdiom(DL_STATE *page, pcl_idiom_part *parts[])
{
  Bool polarity;
  IMAGEOBJECT *im0, *im2 ;
  LISTOBJECT *objects[3] ;

  if ( parts[0]->nobj != 1 || parts[1]->nobj != 1 || parts[2]->nobj != 1 )
    return FALSE ;

  objects[0] = dlref_lobj(parts[0]->head) ;
  objects[1] = dlref_lobj(parts[1]->head) ;
  objects[2] = dlref_lobj(parts[2]->head) ;

  if ( !(objects[0]->opcode == RENDER_image &&
         objects[1]->opcode == RENDER_image &&
         objects[2]->opcode == RENDER_image) )
    return FALSE;

  if ( !(objects[0]->objectstate->pclAttrib->rop == PCL_ROP_DSx &&
         objects[1]->objectstate->pclAttrib->rop == PCL_ROP_DSa &&
         objects[2]->objectstate->pclAttrib->rop == PCL_ROP_DSx) )
    return FALSE;

  /* Same pattern used for all (if any). */
  if ( !(objects[0]->objectstate->pclAttrib->cacheEntry.pointer.pointer
         == objects[1]->objectstate->pclAttrib->cacheEntry.pointer.pointer &&
         objects[1]->objectstate->pclAttrib->cacheEntry.pointer.pointer
         == objects[2]->objectstate->pclAttrib->cacheEntry.pointer.pointer) )
    return FALSE;

  im0 = objects[0]->dldata.image ;
  im2 = objects[2]->dldata.image ;

  if ( im0->ims != im2->ims ) {
    /** \todo ajcd 2013-07-25: some cases have different image contents for
        first and third image. This doesn't seem right, find out why and fix
        it.*/

    /* Check bboxes of images match. Only the first and third boxes must
       match. The middle (mask) must be contained inside the first and third,
       otherwise it would scribble outside of the area we're going to mask,
       but it can be smaller. The area outside of the mask but inside the
       images won't be affected, it would just XOR twice for no net
       change. */
    if ( !bbox_equal(&objects[0]->bbox, &objects[2]->bbox) ||
         !bbox_contains(&objects[0]->bbox, &objects[1]->bbox) )
      return FALSE;

    /* First and third images must be equally sized but allow the mask to differ. */
    if ( !bbox_equal(im_storebbox_original(objects[0]->dldata.image->ims),
                     im_storebbox_original(objects[2]->dldata.image->ims)) )
      return FALSE;
  }

  /* Check the second image is suitable for use as a mask (1 or 8 bit etc). */
  if ( !im_expandmasktest(page, objects[1]->dldata.image->ime, &polarity) )
    return FALSE;

  /* Matches idiom so don't need to backdrop render!
   *
   * Convert the first image to a masked image using the second image
   * as a 1 bit mask.
   */
  {
    IMAGEOBJECT *imageobj = objects[0]->dldata.image;
    IMAGEOBJECT *maskobj = objects[1]->dldata.image;
    IM_EXPAND *mask_ime;
    int32 ibpp;
    PclAttrib* oldPcl = objects[0]->objectstate->pclAttrib;
    int32 rw, rh ;

    if ( !updateAttrib(page, objects[0], PCL_ROP_S,
                       oldPcl->foregroundSource, oldPcl->sourceTransparent,
                       oldPcl->patternTransparent, oldPcl->patternColors,
                       oldPcl->xorstate,
                       FALSE /*colorModified*/) )
      return FALSE;

    /* It's a mask, but the driver may generate 8 bit RGB data... */
    ibpp = im_expandbpp(maskobj->ime);

    /* Make a new image expander suitable for rendering the second image
       as a 1 bit mask for the first image. */
    rw = maskobj->imsbbox.x2 - maskobj->imsbbox.x1 + 1 ;
    rh = maskobj->imsbbox.y2 - maskobj->imsbbox.y1 + 1 ;
    mask_ime = im_expandopenimask(page, rw, rh, ibpp, polarity);
    if ( !mask_ime )
      return FALSE;

    im_expandfree(page, maskobj->ime);
    maskobj->ime = mask_ime;

    imageobj->mask = maskobj;
    if ( !dl_reserve_band(page, RESERVED_BAND_MASKED_IMAGE) )
      return FALSE ;

    /* Turn off the DSx ropping in the first object.  The second image's
       imageobj has been grabbed so need to change the opcode.  Neither the
       second and third objects need rendering now. */
    objects[1]->dldata.image = NULL;
    objects[1]->opcode = RENDER_void;

    dl_to_none(page->dlc_context, &objects[1]->p_ncolor);
    changeImageTo(page, objects[2], RENDER_void) ;
    dl_to_none(page->dlc_context, &objects[2]->p_ncolor);

    return TRUE;
  }
}

/* Check for an object that is obscured by the immediately preceding
   object. */
static Bool checkForObscuredIdiom(DL_STATE *page, pcl_idiom_part *parts[])
{
  PclAttrib *attr0, *attr1 ;
  LISTOBJECT *objects[2] ;

  if ( parts[0]->nobj != 1 || parts[1]->nobj != 1 )
    return FALSE ;

  objects[0] = dlref_lobj(parts[0]->head) ;
  objects[1] = dlref_lobj(parts[1]->head) ;

  /** \todo ajcd 2013-07-30: This idiom is currently not working. Disable via
      a prototest until it can be made to work. Differences appear in job
      AC8Z51CC_PCL6clr_1200-1bit.prn amongst other. */
  if ( ! FALSE /* Feature(obscured_idiom) */ )
    return FALSE ;

  attr0 = objects[0]->objectstate->pclAttrib ;
  attr1 = objects[1]->objectstate->pclAttrib ;

  /* Both objects must either be outside of an XOR idiom, or inside of an XOR
     idiom. */
  if ( attr0->xorstate != attr1->xorstate ||
       attr0->xorstate == PCL_XOR_STARTING ||
       attr0->xorstate == PCL_XOR_ENDING )
    return FALSE ;

  /* The second object must not depend on the destination. */
  if ( pclROPRequiresDestination(attr1->rop) )
    return FALSE ;

  /* The second object may not be transparent. */
  if ( attr1->sourceTransparent || attr1->patternTransparent )
    return FALSE ;

  /* The second object must cover the first object entirely. */
  if ( !bbox_contains(&objects[1]->bbox, &objects[0]->bbox) )
    return FALSE ;

  /* The second object must be either a rect or an image to guarantee
     coverage of the entire bbox. */
  if ( objects[1]->opcode == RENDER_image ) {
    /* If the second object is an image, it cannot have a mask. */
    if ( objects[1]->dldata.image->mask != NULL )
      return FALSE ;
  } else if ( objects[1]->opcode != RENDER_rect )
    return FALSE ;

  /* Matched idiom. We can dispose of the first object to the best of our
     ability. */
  if ( objects[0]->opcode == RENDER_image )
    changeImageTo(page, objects[0], RENDER_void) ;

  dl_to_none(page->dlc_context, &objects[0]->p_ncolor);

  return TRUE;
}

/**
 * Check for following idiom:
 * Black and white image object with ROP 136 or 184.
 * ROP 136 is DSa.  ROP 184 "D xor T, and S, xor T" is another variation on the
 * clipping idiom.  Both idioms can be handled by converting the image into an
 * imagemask with a black DL colour or the foreground colour.
 */
static Bool checkForSingleImageMaskIdiom(DL_STATE *page,
                                         pcl_idiom_part *parts[])
{
  LISTOBJECT *object ;
  PclAttrib *attrib ;
  Bool polarity;

  if ( parts[0]->nobj != 1 )
    return FALSE ;

  object = dlref_lobj(parts[0]->head);

  if ( object->opcode != RENDER_image )
    return FALSE;

  attrib = object->objectstate->pclAttrib ;
  HQASSERT(attrib->foregroundSource == PCL_FOREGROUND_IN_PCL_ATTRIB,
           "Image should have foreground in PCL attribs") ;

  if ( attrib->rop != PCL_ROP_DSa && attrib->rop != 184 /*TSDTxax*/ )
    return FALSE;

  /* TSDTxax cannot be patterned, because we're going to change the
     foreground color. */
  if ( attrib->rop == 184 && attrib->patternColors != PCL_PATTERN_NONE )
    return FALSE;

  /* Check the image is suitable for use as a mask (1 or 8 bit etc). */
  if ( !im_expandmasktest(page, object->dldata.image->ime, &polarity) )
    return FALSE;

  /** \todo ajcd 2013-07-25: The polarity of the mask is wrong for use inside
      a direct ROP XOR idiom. */

  /* Matches idiom so don't need to backdrop render!
   *
   * Patch the rop of the object to S.  Make a new expander suitable for use as
   * mask.  Set the foreground colour (in the pclAttrib) as the dl colour.  Then
   * we have a simple imagemask!
   */
  {
    IMAGEOBJECT *maskobj = object->dldata.image;
    IM_EXPAND *mask_ime;
    PclPackedColor packedColor;
    uint8 reproType = DISPOSITION_REPRO_TYPE(object->disposition);
    uint8 userLabel = object->disposition & DISPOSITION_FLAG_USER;
    int32 ibpp, rw, rh;

    if ( page->pcl5eModeEnabled || attrib->rop == PCL_ROP_DSa ) {
      packedColor = (page->virtualDeviceSpace == SPACE_DeviceCMYK)
        ? 0x00ffffff /*black channel only*/
        : 0u /*generic additive black*/;
    } else
      packedColor = attrib->foreground;

    if ( !packedColorToObjectColor(page, packedColor, object) )
      return FALSE;

    if ( !updateAttrib(page, object, PCL_ROP_S,
                       attrib->foregroundSource, attrib->sourceTransparent,
                       attrib->patternTransparent, attrib->patternColors,
                       attrib->xorstate,
                       TRUE /*colorModified*/) )
      return FALSE;

    /* It's a mask, but the driver may generate 8 bit RGB data... */
    ibpp = im_expandbpp(object->dldata.image->ime);

    /* Make a new image expander suitable for rendering the image
       as a 1 bit imagemask. */
    rw = maskobj->imsbbox.x2 - maskobj->imsbbox.x1 + 1 ;
    rh = maskobj->imsbbox.y2 - maskobj->imsbbox.y1 + 1 ;
    mask_ime = im_expandopenimask(page, rw, rh, ibpp, polarity);
    if ( !mask_ime )
      return FALSE;

    im_expandfree(page, maskobj->ime);
    maskobj->ime = mask_ime;
    theIOptimize(maskobj) &= ~IMAGE_OPTIMISE_1TO1; /* n/a to imagemasks */

    object->opcode = RENDER_mask;
    DISPOSITION_STORE(object->disposition, reproType, GSC_FILL, userLabel);
  }

  return TRUE;
}

/**
 * PCL drivers generate certain combinations of objects with ROPs to represent
 * common idioms like clipped images.  Recognising these idioms and converting
 * them to more efficient internal representations not involving ROPping means
 * means a significant reduction in the amount of compositing required and a
 * substantial performance gain.
 */
static Bool checkForIdioms(DL_STATE *page, uint32 *numParts)
{
  uint32 index ;

  typedef struct IdiomTable {
    Bool (*handler)(DL_STATE *page, pcl_idiom_part *parts[]);
    uint32 numPartsIn;
    uint32 numPartsConsumed;
  } IdiomTable;

  const static IdiomTable idiomTable[] = {
    {checkForTextureClipIdiom, 3, 3},
    {checkForClipIdiom, 3, 3},
    {checkForImageMaskIdiom, 3, 3},
    {checkForObscuredIdiom, 2, 1},
    {checkForSingleImageMaskIdiom, 1, 1},
  };

  pcl_idiom_part *parts[PCL_IDIOM_MAX_LEN] ;

  *numParts = 0;

  getNParts(page, min(page->pclIdiomQueue.nparts, PCL_IDIOM_MAX_LEN), parts) ;

  /* Now try to match the objects on the queue with idioms that
     can be handled without backdrop rendering. */
  for ( index = 0; index < NUM_ARRAY_ITEMS(idiomTable); ++index ) {
    const IdiomTable *idiom = &idiomTable[index] ;
    HQASSERT(PCL_IDIOM_MAX_LEN >= idiom->numPartsIn,
             "PCL_IDIOM_MAX_LEN needs updating");
    if (
#ifdef DEBUG_BUILD
        (debug_pcl_idiom_control & (1 << index)) == 0 &&
#endif
         page->pclIdiomQueue.nparts >= idiom->numPartsIn &&
         idiom->handler(page, parts) ) {
      *numParts = idiom->numPartsConsumed;
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * Don't allow partial painting if we're in the middle of a potential
 * PCL idiom (a sequence of ropped objects that can be manipulated to
 * avoid compositing).
 */
Bool pclPartialPaintSafe(DL_STATE *page)
{
  DLREF *dlref;

  /* If there's nothing in the PCL idiom queue, we're safe. */
  if ( page->pclIdiomQueue.nparts == 0 )
    return TRUE;

  /* We can always partial paint if we're not backdrop rendering; unlike
   * backdrops, bands are repopulated after a partial paint and therefore rops
   * which use the destination are not a problem. */
  /* PCL now fixed, and the code crashes. Not sure what the correct
   * architectural fix, but the following at least prevents the crashes for now
   */
  if ( page->currentGroup == NULL || page->pcl5eModeEnabled )
    return TRUE;

  /* If any of the lobjs in the PCL idiom queue involve the destination then
     partial painting should be avoided unless the surface supports the rop in
     the blit stack. The rop_support() method of the surface has previously been
     queried by set_attrib_rendering(), and pclAttrib->backdrop will be set if
     the rop isn't supported by the output surface. */
  for ( dlref = page->pclIdiomQueue.parts[page->pclIdiomQueue.first].head ;
        dlref != NULL;
        dlref = dlref_next(dlref) ) {
    LISTOBJECT *lobj = dlref_lobj(dlref);
    if ( pclROPRequiresDestination(lobj->objectstate->pclAttrib->rop) &&
         lobj->objectstate->pclAttrib->backdrop)
      return FALSE;
  }

  return TRUE;
}

/**
 * Mark the region map to indicate the areas of the page that require
 * compositing.  Various ROPs need compositing because they involve the
 * destination and this must be held in RGB space without screening.
 *
 * The code tracks the last several DL objects and tries to match them
 * to a range of PCL driver idioms.  If the DL objects match an idiom
 * the objects can be optimised to avoid compositing.
 */
void pclUpdateRegionMap(DL_STATE *page, DLREF *dlref,
                        BitGrid *destinationRegions, BitGrid *backdropRegions)
{
  LISTOBJECT *object = dlref_lobj(dlref);

  /* The forall flags should exclude None objects. */
  HQASSERT(!dl_is_none(object->p_ncolor),
           "Shouldn't be marking None color objects") ;

  updateRegionMap(page, object, destinationRegions, backdropRegions);
}

void pclIdiomAdd(DL_STATE *page, DLREF *dlref)
{
  uint32 numParts;
  LISTOBJECT *object = dlref_lobj(dlref);

  /* Check for a uniform image and replace it with a rect to save memory. */
  if ( object->opcode == RENDER_image ) {
#ifdef METRICS_BUILD
    ++pclattrib_metrics.unmerged_images ;
    ++pclattrib_metrics.merged_images ;
#endif
    checkForUniformImage(page, object);
  }

  /* Add the new object to the queue. Ignore any objects already simplified
     to None color. */
  if ( dl_is_none(object->p_ncolor) )
    return ;

  if ( !addToQueue(page, dlref) )
    return ;

  /* If the added object didn't combine with the last part and was temporarily
     added to the overflow slot, flush the first part as it can no longer be
     part of an idiom and release the overflow slot. */
  if ( page->pclIdiomQueue.nparts == NUM_ARRAY_ITEMS(page->pclIdiomQueue.parts) )
    flushHead(page);

  /* Wait until the idiom queue is long enough to match any idiom before
     checking, so longer idioms get matched first. If DL purging is enabled, we
     can't handle multiple objects, so check for single-object idioms before
     flushing. */
  if ( page->pclIdiomQueue.nparts == PCL_IDIOM_MAX_LEN || dlpurge_inuse() ) {
    if ( checkForIdioms(page, &numParts) )
      flushQueue(page, numParts);
    else if ( dlpurge_inuse() )
      flushHead(page);
  }
}

Bool pclROPRequiresDestination(uint8 rop)
{
  /* The ROP number is the solution to the truth table:

        T Texture      1 1 1 1 0 0 0 0
        S Source       1 1 0 0 1 1 0 0
        D Destination  1 0 1 0 1 0 1 0
        ROP Code       ? ? ? ? ? ? ? ?

     The destination is irrelevant if the ROP returns the same regardless
     whether D is 0 or 1 for the same T and S settings. */
  return ((rop >> 1) & 0x55) != (rop & 0x55) ;
}

Bool pclROPRequiresSource(uint8 rop)
{
  /* The ROP number is the solution to the truth table:

        T Texture      1 1 1 1 0 0 0 0
        S Source       1 1 0 0 1 1 0 0
        D Destination  1 0 1 0 1 0 1 0
        ROP Code       ? ? ? ? ? ? ? ?

     The source is irrelevant if the ROP returns the same regardless
     whether S is 0 or 1 for the same T and D settings. */
  return ((rop >> 2) & 0x33) != (rop & 0x33) ;
}

Bool pclROPRequiresTexture(uint8 rop)
{
  /* The ROP number is the solution to the truth table:

        T Texture      1 1 1 1 0 0 0 0
        S Source       1 1 0 0 1 1 0 0
        D Destination  1 0 1 0 1 0 1 0
        ROP Code       ? ? ? ? ? ? ? ?

     The texture is irrelevant if the ROP returns the same regardless
     whether T is 0 or 1 for the same S and D settings. */
  return ((rop >> 4) & 0x0f) != (rop & 0x0f) ;
}

/**
 * Return a matrix which maps from the coordinate space (0, 0, width, height)
 * to one which includes the specified rotation, which should be a multiple of
 * 90 degrees.
 *
 * \return true if the transform transposes the coorinate space.
 */
static Bool getPrintDirectionMatrix(uint32 width, uint32 height,
                                    uint32 angle, OMATRIX* matrix)
{
  OMATRIX rotation;
  Bool transposed = FALSE;

  *matrix = identity_matrix;
  matrix_set_rotation(&rotation, 0 - (double)angle);

  /* Offset the rotation to ensure that the coordinate space is in the positive
   * domain. */
  switch (angle) {
  case 0:
    /* matrix is already identity. */
    break;

  case 90:
    transposed = TRUE;
    matrix_translate(matrix, 0, width, matrix);
    matrix_mult(&rotation, matrix, matrix);
    break;

  case 180:
    matrix_translate(matrix, width, height, matrix);
    matrix_mult(&rotation, matrix, matrix);
    break;

  case 270:
    transposed = TRUE;
    matrix_translate(matrix, height, 0, matrix);
    matrix_mult(&rotation, matrix, matrix);
    break;

  default:
    HQFAIL("Invalid angle.");
    break;
  }
  return transposed;
}

Bool setPclForegroundSource(DL_STATE *page, uint8 source)
{
  pclGstate.foregroundSource = source;

  if (source == PCL_FOREGROUND_IN_PCL_ATTRIB) {
    if ( !pclPackCurrentColor(page, &pclGstate.foreground) )
      return FALSE;
  }

  return TRUE;
}

uint8 getPclForegroundSource(void)
{
  return pclGstate.foregroundSource;
}

void setPcl5Pattern(pcl5_pattern* pattern,
                    uint32 deviceDpi,
                    uint32 angle,
                    IPOINT* origin,
                    Pcl5CachedPalette* palette)
{
  PclPatternConstructionState* cState = &pclGstate.constructionState;

  pclGstate.cacheEntry.type = PCL5_PATTERN;
  pclGstate.cacheEntry.pointer.pcl5 = pattern;

  cState->cached = FALSE ;
  cState->deviceDpi = deviceDpi;

  /* Hang on to the palette if the pattern is color and the palette is not null.
   * Color patterns will be down-graded to black and white if no palette is
   * provided. */
  if (pattern != NULL && pattern->color && palette != NULL) {
    cState->paletteUid = palette->uid;
    cState->palette = palette;
  }
  else {
    cState->paletteUid = 0;
    cState->palette = NULL;
  }

  if (pattern == NULL || angle == 0) {
    /* No transform required if the angle is zero. */
    cState->transposed = FALSE;
    cState->transformValid = FALSE;
  }
  else {
    cState->transformValid = TRUE;
    cState->transposed = getPrintDirectionMatrix(pattern->width - 1,
                                                 pattern->height - 1,
                                                 angle, cState->transform);
  }
  cState->origin = *origin;
}

/**
 * Set the current pattern to a PCLXL pattern.
 * \param angle The angle of rotation for the pattern. Must be multiple of 90.
 * \param scale The scaling aspect of the XL ctm for the pattern.
 * \param origin The origin of the pattern. If null, (0, 0) will be used.
 * \param targetSize The size of the pattern; this may be null or (0,0) if
 *        the default size should be used.
 * \param palette Palette for indexed patterns. The passed palette should be
 *        valid until the patterned object has been added to the display list
 *        (at which point a copy is made if required). This may be NULL, in
 *        which case color patterns are treated as black and white.
 */
void setPclXLPattern(PclXLPattern* pattern, int32 angle, FPOINT *scale,
                     FPOINT* origin, IPOINT* targetSize, PclXLCachedPalette *palette)
{
  PclPatternConstructionState* cState = &pclGstate.constructionState;
  FPOINT targetSizeFloat;

  pclGstate.cacheEntry.type = PCLXL_PATTERN;
  pclGstate.cacheEntry.pointer.pclxl = pattern;

  cState->cached = FALSE ;

  /* Hang on to the palette if it is not null */
  if (pattern != NULL && palette != NULL) {
    cState->paletteUid = palette->uid;
    cState->palette = palette;
  }
  else {
    cState->paletteUid = 0;
    cState->palette = NULL;
  }

  cState->transformValid = FALSE;
  *cState->transform = identity_matrix;
  cState->transposed = FALSE;

  if ( !pattern )
    return; /* Pattern has been cleared */

  if (targetSize == NULL || targetSize->x == 0 || targetSize->y == 0)
    cState->targetSize = pattern->targetSize;
  else
    cState->targetSize = *targetSize;

  if ( angle != 0 ) {
    SYSTEMVALUE norm_angle = angle;

    NORMALISE_ANGLE(norm_angle);
    angle = (int32)norm_angle;

    cState->transformValid = TRUE;
    cState->transposed = getPrintDirectionMatrix(pattern->size.x - 1,
                                                 pattern->size.y - 1,
                                                 angle, cState->transform);
  }

  /* Scale targetSize up to device space. */
  targetSizeFloat.x = cState->targetSize.x * scale->x;
  targetSizeFloat.y = cState->targetSize.y * scale->y;
  SC_C2D_INT(cState->targetSize.x, targetSizeFloat.x);
  SC_C2D_INT(cState->targetSize.y, targetSizeFloat.y);

  /* Round up to one pixel minimum. */
  if ( cState->targetSize.x <= 0 )
    cState->targetSize.x = 1;
  if ( cState->targetSize.y <= 0 )
    cState->targetSize.y = 1;

  if (origin == NULL) {
    SETXY(cState->origin, 0, 0);
  } else {
    SC_C2D_INT(cState->origin.x, origin->x) ;
    SC_C2D_INT(cState->origin.y, origin->y) ;
  }
}

void setPclSourceTransparent(Bool transparent)
{
  pclGstate.sourceTransparent = (uint8)transparent;
}

void setPclPatternTransparent(Bool transparent)
{
  pclGstate.patternTransparent = (uint8)transparent;
}

Bool isPclSourceTransparent(void)
{
  return (Bool) pclGstate.sourceTransparent;
}

Bool isPclPatternTransparent(void)
{
  return (Bool) pclGstate.patternTransparent;
}

void setPclRop(uint8 rop)
{
  pclGstate.rop = rop;
}

uint8 getPclRop(void)
{
  return pclGstate.rop;
}

Bool pclPackCurrentColor(DL_STATE *page, PclPackedColor* color)
{
  dl_color_iter_t iterator;
  COLORANTINDEX index;
  COLORVALUE c[4];
  int32 i = 0;
  dl_color_t *dlc_current = dlc_currentcolor(page->dlc_context) ;
  /* DeviceGray is treated as RGB by replicating the single channel into 3. */
  int32 nColorants = page->virtualDeviceSpace == SPACE_DeviceCMYK ? 4 : 3 ;

  /* Invoke the color change to ensure the current color is correct (setting the
   * current color doesn't actually invoke the chain). */
  if (! gsc_invokeChainSingle(gstateptr->colorInfo, GSC_FILL))
    return FALSE;

  /* PCL RGB packing is a subset of CMYK packing. Initialising the last
     channel allows us to pack the color with the same macro. */
  c[3] = 0 ;

  switch (dlc_first_colorant(dlc_current, &iterator, &index, &c[0])) {
  default:
    HQFAIL("Unhandled iterator constant.");
    return FALSE;

  case DLC_ITER_ALL01:
    c[0] = dlc_is_white(dlc_current) ? COLORVALUE_ONE : 0 ;
    /*@fallthrough@*/
  case DLC_ITER_ALLSEP:
    i = 1 ;
    break;

  case DLC_ITER_COLORANT:
    do {
      ++i ;
      HQASSERT(i <= nColorants, "Too many colorants") ;
    } while ( dlc_next_colorant(dlc_current, &iterator, &index, &c[i]) == DLC_ITER_COLORANT ) ;
    HQASSERT(i == nColorants ||
             (i == 1 && page->pcl5eModeEnabled) ||
             (i == 1 && page->virtualDeviceSpace == SPACE_DeviceGray),
             "Unexpected number of colorants") ;
    break;
  }

  /* If given just one channel, replicate it up to the number of channels. */
  while (i < nColorants) {
    c[i++] = c[0];
  }

  /* Pack color. RGB packed colors are a subset of CMYK colors. Since we
     initialised the last channel to 0, we can use the CMYK pack. */
  *color = PCL_PACK_CMYK(c) ;
  HQASSERT(page->virtualDeviceSpace == SPACE_DeviceCMYK ||
           PCL_PACK_RGB(c) == *color, "RGB/Gray color packing incorrect") ;
  if ( page->virtualDeviceSpace == SPACE_DeviceCMYK &&
       packedColorIsBlack(SPACE_DeviceCMYK, *color)) {
    /* Override any form of black with rop-friendly rich-black. */
    *color = PCL_PACKED_CMYK_BLACK;
  }

  return TRUE;
}

/**
 */
static inline int32 patternWrap(int32 val, uint32 size)
{
  int32 mask = (int32)size - 1 ;
  if ((mask & (int32)size) == 0) /* width is a power of 2 */
    return val & mask;
  else
    return val % (int32)size;
}

void pclDLPatternIteratorStart(PclDLPatternIterator *iter,
                               PclDLPattern *pattern,
                               int32 x, int32 y, int32 runLength)
{
  PclDLPatternLine *line ;
  int32 maxwidth ;

  x = patternWrap(x, pattern->width) ;
  y = patternWrap(y, pattern->height) ;

  line = &pattern->lines[y] ;
  iter->palette = pattern->palette ;
  iter->paletteSize = pattern->paletteSize ;

  if ( line->type == PCL_PATTERNLINE_FULL ) {
    iter->rle = FALSE ;
    iter->nlines = line->repeats + 1 ;
    if ( iter->paletteSize == 0 ) {
      iter->start.pixels = pattern->data.pixels + line->offset ;
      iter->curr.pixels = iter->start.pixels + x ;
      iter->end.pixels = iter->start.pixels + pattern->width ;
    } else {
      iter->start.indices = pattern->data.indices + line->offset ;
      iter->curr.indices = iter->start.indices + x ;
      iter->end.indices = iter->start.indices + pattern->width ;
    }
    x = 0 ; /* Done the phase adjustment by adding x to curr. */
  } else {
    int32 runs = line->type - PCL_PATTERNLINE_RLE0 ;

    iter->rle = TRUE ;
    iter->nlines = line->repeats + 1 ;
    if ( iter->paletteSize == 0 ) {
      iter->start.pixels = pattern->data.pixels + line->offset ;
      iter->curr.pixels = iter->start.pixels ;
      iter->end.pixels = iter->start.pixels + runs * 2 ;
    } else {
      iter->start.indices = pattern->data.indices + line->offset ;
      iter->curr.indices = iter->start.indices ;
      iter->end.indices = iter->start.indices + runs * 2 ;
    }
  }

  INLINE_MIN32(maxwidth, x + runLength, (int32)pattern->width) ;
  pclDLPatternIteratorNext(iter, maxwidth) ;
  if ( iter->cspan == (int32)pattern->width ) {
    /* If the line covers the entire width of the pattern, then it will
       repeat to the entire run length. */
    iter->cspan = runLength ;
  } else {
    /* Phase adjustment for first run. */
    while ( iter->cspan <= x ) {
      x -= iter->cspan ;
      pclDLPatternIteratorNext(iter, x + runLength) ;
    }
    iter->cspan -= x ;
  }
  HQASSERT(iter->cspan <= runLength, "Initial run length too long") ;
}

void pclDLPatternIteratorNext(PclDLPatternIterator *iter, int32 runLength)
{
  HQASSERT(iter->curr.indices >= iter->start.indices &&
           iter->curr.indices < iter->end.indices,
           "PCL pattern iterator out of range") ;

  if ( iter->rle ) {
    /* RLE spans are stored as (span,color) pairs. */
    if ( iter->paletteSize == 0 ) {
      pcl_color_union *before = iter->curr.pixels ;
      iter->cspan = before[0].i ;
      iter->color = before[1] ;
      iter->curr.pixels += 2 ;
      /* Wrap around and test if first pixel is same as last */
      if ( iter->curr.pixels == iter->end.pixels ) {
        iter->curr = iter->start ;
        if ( iter->cspan < runLength &&
             iter->curr.pixels != before &&
             iter->color.i == iter->curr.pixels[1].i ) {
          iter->cspan += iter->curr.pixels[0].i ;
          iter->curr.pixels += 2 ;
        }
      }
    } else {
      uint8 *before = iter->curr.indices ;
      uint8 index ;
      iter->cspan = before[0] ;
      index = before[1] ;
      iter->color = iter->palette[patternWrap(index, iter->paletteSize)] ;
      iter->curr.indices += 2 ;
      /* Wrap around and test if first pixel is same as last */
      if ( iter->curr.indices == iter->end.indices ) {
        iter->curr = iter->start ;
        if ( iter->cspan < runLength &&
             iter->curr.indices != before &&
             index == iter->curr.indices[1] ) {
          iter->cspan += iter->curr.indices[0] ;
          iter->curr.indices += 2 ;
        }
      }
    }
  } else {
    iter->cspan = 0 ;
    if ( iter->paletteSize == 0 ) {
      pcl_color_union *before = iter->curr.pixels ;
      iter->color = *before ;
      do {
        ++iter->cspan ;
        if ( ++iter->curr.pixels == iter->end.pixels )
          iter->curr = iter->start ;
      } while ( iter->cspan < runLength &&
                iter->curr.pixels != before &&
                iter->color.i == iter->curr.pixels->i ) ;
    } else {
      uint8 *before = iter->curr.indices ;
      uint8 index = *before ;
      iter->color = iter->palette[patternWrap(index, iter->paletteSize)] ;
      do {
        ++iter->cspan ;
        if ( ++iter->curr.indices == iter->end.indices )
          iter->curr = iter->start ;
      } while ( iter->cspan < runLength &&
                iter->curr.indices != before &&
                index == *iter->curr.indices ) ;
    }
  }

  INLINE_MIN32(iter->cspan, runLength, iter->cspan) ;
}

void init_C_globals_pclAttrib(void)
{
  PclAttrib pclGstateInit = {0};
  OMATRIX alignedMatrixInit = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0, MATRIX_OPT_BOTH} ;
  pcl5_pattern pclPatternInit = {0} ;

  pclGstateActive = FALSE;
  pcl5eModeEnabled = FALSE;
  pclGstate = pclGstateInit;
  alignedMatrix = alignedMatrixInit;
  invalidPattern = erasePattern = pclPatternInit ;

  erasePattern.width = 8;
  erasePattern.height = 1;
  erasePattern.x_dpi = erasePattern.y_dpi = 300;
  erasePattern.color = FALSE;
  erasePattern.bits_per_pixel = 1;
  erasePattern.stride = 1;
  erasePattern.data = erasePatternData;

  whitePattern = erasePattern ;

#ifdef DEBUG_BUILD
  objnum = 0 ;
  debug_pcl_idiom = debug_pcl_idiom_control = 0 ;
#endif
#ifdef METRICS_BUILD
  pclattrib_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&pclattrib_metrics_hook) ;
#endif
}

/* Log stripped */

