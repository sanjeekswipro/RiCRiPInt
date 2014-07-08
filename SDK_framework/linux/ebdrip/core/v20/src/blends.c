/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!src:blends.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interpolation of blends (Shading Types 2 & 3)
 */

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "swdevice.h"
#include "often.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "monitor.h"
#include "namedef_.h"
#include "coremaths.h" /* approx_gcd */

#include "control.h"
#include "interrupts.h"
#include "stackops.h"
#include "gstate.h"
#include "halftone.h"
#include "shadex.h"
#include "params.h"
#include "constant.h" /* EPSILON, CIRCLE_FACTOR */
#include "images.h"
#include "rectops.h"
#include "functns.h"
#include "routedev.h"
#include "matrix.h"
#include "std_file.h"
#include "pathcons.h"
#include "gu_cons.h"
#include "gu_fills.h"
#include "gu_path.h"
#include "pathops.h"
#include "execops.h"
#include "gu_ctm.h"
#include "gschtone.h"
#include "gschead.h"
#include "dl_color.h"
#include "system.h"  /* freepath */
#include "rcbshfil.h"
#include "dlstate.h"

#include "blends.h"
#include "dl_shade.h"
#include "shadecev.h"
#include "tensor.h"
#include "surfacet.h"


/* Helper struct for radial blend arcs, containing calculated unit vectors and
   flags */
typedef struct blend_arc_t {
  SYSTEMVALUE uax, uay ; /* Device-space unit vector along axis */
  SYSTEMVALUE upx, upy ; /* Device-space unit vector perpendicular to axis */
  SYSTEMVALUE tpx, tpy ; /* Tangent vector from r0-r1, on perpendicular side */
  SYSTEMVALUE tqx, tqy ; /* Tangent vector from r0-r1, on opposite side */
  SYSTEMVALUE ptx, pty ; /* Vector to tangent point on perpendicular side */
  SYSTEMVALUE qtx, qty ; /* Vector to tangent point on opposite side */
  SYSTEMVALUE sintheta, costheta ;
} blend_arc_t ;

static Bool radial_blend_quadrant(int32 type,
                                  SYSTEMVALUE cx, SYSTEMVALUE cy,
                                  SYSTEMVALUE ax, SYSTEMVALUE ay,
                                  SYSTEMVALUE px, SYSTEMVALUE py,
                                  PATHINFO *pathinfo) ;
static Bool radial_blend_arc(Bool (*join)(SYSTEMVALUE, SYSTEMVALUE, PATHINFO *),
                             SYSTEMVALUE cx, SYSTEMVALUE cy,
                             SYSTEMVALUE r,
                             blend_arc_t *arc,
                             PATHINFO *pathinfo) ;
static Bool radial_blend_extend(SYSTEMVALUE cx, SYSTEMVALUE cy,
                                SYSTEMVALUE r, SYSTEMVALUE sd,
                                blend_arc_t *arc, int32 dirn,
                                int32 opposite,
                                PATHINFO *pathinfo) ;

static Bool blend_fill( PATHINFO *pathinfo ) ;

static Bool blend_lineto(SYSTEMVALUE x, SYSTEMVALUE y, PATHINFO *path) ;
static Bool blend_moveto(SYSTEMVALUE x, SYSTEMVALUE y, PATHINFO *path) ;


/* All this stuff should now be obsolete, but we do still need it for when the
   PoorShading param is set. We're also keeping it under debug build for
   comparison for now. */
#if defined(DEBUG_BUILD) || ! defined POOR_SHADING

/* Noise addition defines */
#define NOISE_PIXELS_PER_INCH 100 /* Size of noise pixels */
#define NOISE_MIN_STEP_PIXELS 2 /* Minimum number of pixels per step allowed */

/* Analysis of blend functions. The goal of analysing the blend functions is
   to work out an even number of steps that can be applied across the whole
   blend, that will be within the supplied color tolerance, and will sample
   the function such that the turning points between piecewise linear segments
   are not on sample points. Discontinuous functions cause major trouble with
   linearity testing and subdividing if sample points are at function joins. */
static Bool blend_section_gcd(SYSTEMVALUE *gcd, USERVALUE d0, USERVALUE d1,
                              USERVALUE base, SYSTEMVALUE minstep,
                              int32 index, SHADINGinfo *sinfo)
{
  USERVALUE bounds[2], discont ;
  int32 order;

  HQASSERT(d0 <= d1, "Domain out of order in blend_section_gcd") ;
  HQASSERT(gcd, "No GCD pointer in blend_section_gcd") ;
  HQASSERT(*gcd >= 0, "GCD value negative in blend_section_gcd") ;
  HQASSERT(minstep > 0, "minstep value not positive in blend_section_gcd") ;
  HQASSERT(sinfo, "Shading info NULL in blend_section_gcd") ;
  HQASSERT(index >= 0 && index < sinfo->nfuncs,
           "function index invalid in blend_section_gcd") ;

  bounds[0] = d0 ;
  bounds[1] = d1 ;

  if ( !(sinfo->funcs ?
         fn_find_discontinuity(&sinfo->funcs[index], 0, bounds,
                               &discont, &order,
                               FN_SHADING, index,
                               sinfo->base_fuid, FN_GEN_NA,
                               &sinfo->fndecode) :
         rcbs_fn_find_discontinuity(sinfo->rfuncs[index], 0, bounds,
                                    &discont, &order)) )
    return FALSE ;

  if ( order != -1 ) { /* Discontinuity found */
    HQASSERT(discont > bounds[0] && discont < bounds[1],
             "Discontinuity not contained in range") ;

    *gcd = approx_gcd(*gcd, discont - base, minstep) ;

#if defined(DEBUG_BUILD)
  if ( (shading_debug_flag & SHADING_DEBUG_INFO) != 0 ) {
    monitorf((uint8 *)"Blend function UID %d channel %d discontinuity found at %f GCD=%f\n",
             sinfo->base_fuid, index, discont, *gcd) ;
  }
#endif

    return (blend_section_gcd(gcd, d0, discont, base, minstep, index, sinfo) &&
            blend_section_gcd(gcd, discont, d1, base, minstep, index, sinfo)) ;
  }

  return TRUE ;
}

/* Sensible maximum for number of representable colors */
#define MAX_BLEND_COLORS 65535

/* Return number of steps to use for blend, based on smoothness, minimum
   decomposition size, and function information in shading info. Analyses
   function object to try and determine likely boundaries between linear
   regions, and use these to assist in probing function for linearity.
   Assumes that opacity is already linear and therefore does not need
   including. */
static Bool blend_steps(USERVALUE d0, USERVALUE d1, SYSTEMVALUE minstep,
                        SHADINGinfo *sinfo)
{
  int32 i, j ;
  uint32 max_steps, min_sections, samples ;
  int32 blend_lin = UserParams.BlendLinearity + 2 ;
  dlc_context_t *dlc_context = sinfo->page->dlc_context;
  dl_color_t prev_color, this_color, *icolors[2] ;
  dl_color_t dcolor ;

  HQASSERT(sinfo->base_index == GSC_SHFILL, "Blend does not have GSC_SHFILL colorType") ;

  dlc_clear(&prev_color);
  dlc_clear(&this_color);
  dlc_clear(&dcolor);

  if ( d0 > d1 ) { /* ordering makes no difference in working out step size */
    USERVALUE tmp = d0 ; d0 = d1 ; d1 = tmp ;
  }

  {  /* Round minstep to get whole number of quanta over blend */
    SYSTEMVALUE tmp = (d1 - d0) / minstep ;

    max_steps = (int32)tmp ;

    if ( tmp > max_steps ) /* Add one for fractional part */
      ++max_steps ;

    if ( max_steps > 0 ) { /* find number of sections in blend */
      SYSTEMVALUE section_gcd ;

      minstep = (d1 - d0) / max_steps ;

      /* min_sections is size of domain divided by GCD of quantised function
         section boundaries in all channels */
      section_gcd = d1 - d0 ;
      for ( i = 0 ; i < sinfo->nfuncs ; ++i ) {
        if ( !blend_section_gcd(&section_gcd, d0, d1, d0, minstep, i, sinfo) )
          return FALSE ;
      }

      HQASSERT(section_gcd > 0, "GCD incorrectly calculated in bland_steps") ;

      min_sections = (int32)((d1 - d0) / section_gcd + 0.5) ;
    } else { /* No color change over blend */
      min_sections = 1 ;
    }
  }

  /* min_sections is the number of steps into which the blend must be divided
     to ensure that each section boundary falls on a step. The number of
     steps in the blend will be the least multiple greater than one of
     min_sections that either yields the required colour tolerance, has more
     than or equal to COLORVALUE_MAX steps per section, or has step size less
     than or equal to minstep. */

  HQASSERT(blend_lin >= 2, "BlendLinearity less than zero in blend_steps") ;

  icolors[0] = &prev_color ;
  icolors[1] = &this_color ;

#if defined(DEBUG_BUILD)
  if ( (shading_debug_flag & SHADING_DEBUG_INFO) != 0 ) {
    monitorf((uint8 *)"Blend function UID %d minstep=%f max_steps=%d min_sections=%d smoothnessbands=%d\n",
             sinfo->base_fuid, minstep, max_steps, min_sections,
             sinfo->smoothnessbands) ;
  }
#endif

  /* Increase in samples if not linear may not converge fast enough. May want
     to double samples in continuation statement of this for loop: */
  for ( samples = min_sections ; /* multiples of min_sections */
        samples < max_steps ; /* not smaller than minstep */
        /* samples += min_sections */ ) { /* What to do if not linear */
    int32 linear = TRUE ;
    uint32 max_interpolate = 1 ;
    /* We'll use doubles for the iterative calculation to preserve accuracy.
    The functions that use the interated value ('bc') need pointers to floats,
    so we'll have to pass an actual float variable to these functions,
    hence 'bc_param'. */
    SYSTEMVALUE bc_step = (SYSTEMVALUE)((d1 - d0) / samples) ;
    SYSTEMVALUE bc = d0 ;
    USERVALUE bc_param = (USERVALUE)bc ;
    dl_color_t *dlc_current = dlc_currentcolor(dlc_context) ;

    /* Divide color distance between adjacent samples by smoothness to
       get number of levels required if this section were linear. Take
       maximum number of levels between samples and multiply that by
       min_sections. If any section is not linear, increase samples and
       try again. */

    /* Convert startpoint of section to color */
    if ( !shading_color(&bc_param, 1.0f /* opacity is linear */,
                        TRUE, sinfo, TRUE) )
      return FALSE ;

    /* Take copy of color generated */
    dlc_copy_release(dlc_context, &prev_color, dlc_current) ;

    for ( i = 0 ; linear && i < (int32)samples ; ++i ) {
      USERVALUE bs = (USERVALUE)bc ; /* Start of section is previous bc */

      bc += bc_step ;
      bc_param = (USERVALUE)bc;

      /* Convert endpoint of section to color */
      if ( !shading_color(&bc_param, 1.0f /* opacity is linear */,
                          FALSE, sinfo, TRUE) ) {
        dlc_release(dlc_context, &prev_color);
        return FALSE ;
      }

      dlc_copy_release(dlc_context, &this_color, dlc_current) ;

      /* Test for linearity by interpolating interior points of section */
      for ( j = 1 ; j < blend_lin ; ++j ) { /* Interpolate points 1..n-1 */
        SYSTEMVALUE weights[2] ;
        USERVALUE bi ;

        weights[0] = (SYSTEMVALUE)j / (SYSTEMVALUE)blend_lin ;
        weights[1] = (SYSTEMVALUE)(blend_lin - j) / (SYSTEMVALUE)blend_lin ;

        bi = (USERVALUE)(bs * weights[0] + bc * weights[1]) ;

        if ( !shading_color(&bi, 1.0f /* opacity is linear */,
                            TRUE, sinfo, TRUE) ||
             !dlc_alloc_interpolate(dlc_context, 2, weights, &dcolor, icolors) ) {
          /* Release temporary refs to dl colors */
          dlc_release(dlc_context, &prev_color);
          dlc_release(dlc_context, &this_color);
          return FALSE ;
        }

        if ( !shading_color_close_enough(sinfo, dlc_current, &dcolor,
                                         gsc_getRS(gstateptr->colorInfo)) ) {
#if defined(DEBUG_BUILD)
          if ( (shading_debug_flag & SHADING_DEBUG_INFO) != 0 ) {

            monitorf((uint8 *)"Blend function UID %d section %d (%f..%f, sampled at %f) is not linear\n",
                     sinfo->base_fuid, i, bs, (USERVALUE)bc, bi) ;
          }
#endif
          /* Release ref to interpolated color */
          dlc_release(dlc_context, &dcolor);

          linear = FALSE ;
          break ;
        }

        /* Release ref to interpolated color */
        dlc_release(dlc_context, &dcolor);
      }

      if ( linear ) {   /* Find number of steps if linear */
        uint32 nsteps = (dlc_max_difference(&prev_color, &this_color) * sinfo->smoothnessbands + COLORVALUE_MAX - 1u) / COLORVALUE_MAX ;

        if ( nsteps > max_interpolate )
          max_interpolate = nsteps ;
      }

      /* Release previous color, copy current color to prev color */
      dlc_release(dlc_context, &prev_color);
      dlc_copy_release(dlc_context, &prev_color, &this_color) ;
    }

    dlc_release(dlc_context, &this_color);

    /* Adjust samples to number required for color closeness */
    if ( samples * max_interpolate > max_steps ) {
      max_interpolate = (max_steps + samples - 1) / samples ;
      HQASSERT(samples * max_interpolate >= max_steps,
               "Interpolation to max_steps failed") ;
    }

    samples *= max_interpolate ;

    if ( linear ) /* It's linear, that'll do, otherwise try again */
      break ;

    if ( max_interpolate <= 1 ) /* If no section was linear, increase samples */
      samples += min_sections ;
  }


#if defined(DEBUG_BUILD)
  if ( (shading_debug_flag & SHADING_DEBUG_INFO) != 0 ) {
    monitorf((uint8 *)"Blend function UID %d analysis returning %d steps\n",
             sinfo->base_fuid, samples) ;
  }
#endif

  return samples ;
}

/* Callbacks for blend file, used for image data source; interpolates between
   endpoints, evaluates function and returns input colorvalues scaled to 12
   bit range. */
typedef struct {
  USERVALUE d0, d1 ;
  int32 index, nsteps, ncomps ; /* sample index, # steps, # components */
  int32 width, stepsize, changeindex ;
  uint32 seed ;                 /* Random seed for noise addition */
  SHADINGinfo *sinfo ;
  uint32 *cvalues ; /* Encoded result of color function evaluations */
} BLEND_FILESTATE ;

static BLEND_FILESTATE bfstate ;
static FILELIST bflist ;

#define BLEND_FILE_BPC 12 /* Bits per component */
#define BLEND_COLOR_CURRENT(_bs, _i) ((_bs)->cvalues[(_i)])
#define BLEND_COLOR_NEXT(_bs, _i) ((_bs)->cvalues[(_i) + (_bs)->ncomps])

/* Same algorithm as hqnrand in SWv20!src:randops.c, but returns uint32 */
static uint32 blendNoiseRand(BLEND_FILESTATE *bstate)
{
  bstate->seed = 2147001325 * bstate->seed + 715136305 ;

  return bstate->seed ;
}

static int32 blendFileFillBuff(register FILELIST *flptr)
{
  BLEND_FILESTATE *bstate ;
  SHADINGinfo *sinfo ;
  uint8 *buff, *bufend ;
  int32 bits = 0 ; /* number of bits in last byte of buffer */
  int32 width, iw, ih ;
  int32 modulo, chance ;

  HQASSERT( flptr , "flptr NULL in blendFileFillBuff." ) ;

  bstate = (BLEND_FILESTATE *)theIFilterPrivate(flptr) ;
  sinfo = bstate->sinfo ;
  width = bstate->width ;
  modulo = (bstate->stepsize | 1) ; /* pixels per step rounded up to odd */

  ih = bstate->index / width ;

  HQASSERT(bstate->index % width == 0, "blendFileFillBuff not at row start") ;

  if ( ih >= bstate->nsteps * bstate->stepsize ) { /* End of blend? */
    SetIEofFlag(flptr) ;
    ClearIOpenFlag(flptr) ;

    return EOF ;
  }

  buff = theIBuffer(flptr) ;
  bufend = buff + theIBufferSize(flptr) ;

  HQASSERT(buff, "Blend file buffer null") ;

  HQASSERT(sinfo->base_index == GSC_SHFILL, "Blend does not have GSC_SHFILL colorType") ;

  /* Decide when to change next and current colors */
  if ( !sinfo->antialias ) { /* Get this step's color */
    SYSTEMVALUE dstep = (bstate->d1 - bstate->d0) / bstate->nsteps ;
    USERVALUE bcolor = (USERVALUE)(bstate->d0 + dstep * ih + dstep * 0.5) ;
    int32 ci ;

    HQASSERT(bstate->stepsize == 1, "Step size must be 1 when not anti-aliased") ;

    bstate->changeindex += bstate->stepsize ;

    if ( !shading_color_function(&bcolor, TRUE, sinfo) )
      return EOF ;

    for ( ci = 0 ; ci < bstate->ncomps ; ++ci ) {
      SYSTEMVALUE range[2] ;

      if ( !gsc_range(gstateptr->colorInfo, GSC_SHFILL, ci, range) )
        return EOF ;

      BLEND_COLOR_CURRENT(bstate, ci) = (uint32)(((1 << BLEND_FILE_BPC) - 1) * (sinfo->scratch[ci] - range[0]) / (range[1] - range[0])) ;
    }
  } else if ( ih == bstate->changeindex ) { /* Set up current color */
    int32 ci, step = ih / bstate->stepsize + 1 ;

    bstate->changeindex += bstate->stepsize ;

    if ( step >= bstate->nsteps ) { /* End of blend, use d1 evaluated down */
      if ( !shading_color_function(&bstate->d1, FALSE, sinfo) )
        return EOF ;
    } else { /* Get next step's color */
      SYSTEMVALUE dstep = (bstate->d1 - bstate->d0) / bstate->nsteps ;
      USERVALUE bcolor = (USERVALUE)(bstate->d0 + dstep * step + dstep * 0.5) ;

      if ( !shading_color_function(&bcolor, TRUE, sinfo) )
        return EOF ;
    }

    for ( ci = 0 ; ci < bstate->ncomps ; ++ci ) {
      SYSTEMVALUE range[2] ;

      if ( !gsc_range(gstateptr->colorInfo, GSC_SHFILL, ci, range) )
        return EOF ;

      BLEND_COLOR_CURRENT(bstate, ci) = BLEND_COLOR_NEXT(bstate, ci) ;
      BLEND_COLOR_NEXT(bstate, ci) = (uint32)(((1 << BLEND_FILE_BPC) - 1) * (sinfo->scratch[ci] - range[0]) / (range[1] - range[0])) ;
    }
  }

  HQASSERT(ih < bstate->changeindex, "Missed color change") ;

  /* Chance of choosing current color depends how far between colors we are */
  /** \todo TODO: Shouldn't spread noise across function discontinuities */

  chance = bstate->changeindex - ih ;
  HQASSERT(chance >= 0 && chance <= modulo, "Impossible chance of choosing current color") ;

  /* Complete current row */
  for ( iw = bstate->index % width ; iw < width && buff < bufend ; ++iw ) {
    int32 ci = 0 ;
    uint32 *cvalues = &BLEND_COLOR_CURRENT(bstate, 0) ;

    /* For each pixel across the width of the blend, select the current color
       or next color, proportionately to the distance across the blend step.
       */
    if ( sinfo->antialias ) {
      if ( (uint32)blendNoiseRand(bstate) % (uint32)modulo >= (uint32)chance )
        cvalues = &BLEND_COLOR_NEXT(bstate, 0) ;
    }

    /* Pack encoded values into buffer */
    for ( ci = 0 ; ci < bstate->ncomps ; ++ci ) {
      uint32 encoded = cvalues[ci] ;

      HQASSERT((bufend - buff) * 8 >= BLEND_FILE_BPC - bits,
               "Not enough space in blendFileFillBuff output buffer") ;

#if BLEND_FILE_BPC == 12
      HQASSERT(encoded <= 4095, "Too many color values for 12-bit blend image") ;
      if ( bits != 0 ) {
        *buff++ |= (uint8)(encoded >> 8) ; /* Half first byte left */
        *buff++ = (uint8)encoded ; /* Full second byte */
      } else {
        *buff++ = (uint8)(encoded >> 4) ; /* Full first byte */
        *buff = (uint8)(encoded << 4) ; /* Half second byte */
      }
      bits = 4 - bits ;
#else
#if BLEND_FILE_BPC == 8
      HQASSERT(encoded <= 255, "Too many color values for 8-bit blend image") ;

      *buff++ = (uint8)encoded ;
#else
      HQASSERT(BLEND_FILE_BPC == 16, "Unrecognised bits per component in blendFileFillBuff") ;
      HQASSERT(encoded <= 65535, "Too many color values for 16-bit blend image") ;

      *buff++ = (uint8)(encoded >> 8) ;
      *buff++ = (uint8)encoded ;
#endif
#endif
    }
  }

  if ( bits != 0 ) { /* Account for byte containing final bits */
    bits = 0 ;
    ++buff ;
  }

  HQASSERT(buff <= bufend, "Too many bytes filled into blend file") ;

  theIReadSize(flptr) = CAST_PTRDIFFT_TO_INT32(buff - theIBuffer(flptr)) ;
  theICount(flptr) = theIReadSize(flptr) - 1 ;
  theIPtr(flptr) = theIBuffer(flptr) + 1 ;

  bstate->index += (iw - bstate->index % width) ;

  return ( int32 ) *(theIBuffer(flptr)) ;
}

static Bool blendFileInit(FILELIST *flptr, OBJECT* args, STACK* stack)
{
  UNUSED_PARAM(FILELIST *, flptr);
  UNUSED_PARAM(OBJECT *, args);
  UNUSED_PARAM(STACK *, stack);

  return TRUE ;
}

static void blendFileDispose(register FILELIST *flptr)
{
  BLEND_FILESTATE *bstate ;
  uint8 *buff ;

  HQASSERT( flptr , "flptr NULL in blendFileDispose." ) ;

  bstate = (BLEND_FILESTATE *)theIFilterPrivate(flptr) ;
  buff = theIBuffer(flptr) ;

  if ( buff ) {
    mm_free(mm_pool_temp, (mm_addr_t)buff, theIBufferSize(flptr)) ;
    theIBuffer(flptr) = NULL ;
    theIPtr(flptr) = NULL ;
  }

  if ( bfstate.cvalues != NULL ) {
    mm_free(mm_pool_temp, (mm_addr_t)bfstate.cvalues, bfstate.ncomps * 2 * sizeof(uint32)) ;
    bfstate.cvalues = NULL ;
  }
}

static FILELIST *blend_filelist(int32 nsteps, int32 width, int32 height,
                                SHADINGinfo *sinfo,
                                USERVALUE d0, USERVALUE d1)
{
  int32 bufsize ;
  uint8 *buffer ;

  HQASSERT( sinfo , "sinfo NULL in blend_filelist." ) ;

  /* Buffer size is enough for a row of the image */
  bufsize = (BLEND_FILE_BPC * sinfo->ncolors * width + 7) / 8 ;
  buffer = theIBuffer(&bflist) ;

  /* Free old buffers if present and not reusable */

  if ( buffer != NULL && theIBufferSize(&bflist) != bufsize ) {
    mm_free(mm_pool_temp, (mm_addr_t)buffer, theIBufferSize(&bflist)) ;
    buffer = theIBuffer(&bflist) = NULL ;
  }

  if ( bfstate.cvalues != NULL && bfstate.ncomps != sinfo->ncolors ) {
    mm_free(mm_pool_temp, (mm_addr_t)bfstate.cvalues, bfstate.ncomps * 2 * sizeof(uint32)) ;
    bfstate.cvalues = NULL ;
  }

  /* Allocate new buffers for data and for color values */
  if ( bfstate.cvalues == NULL ) {
    bfstate.cvalues = (uint32 *)mm_alloc(mm_pool_temp, sinfo->ncolors * 2 * sizeof(uint32), MM_ALLOC_CLASS_SHADING) ;
    if ( bfstate.cvalues == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }
    bfstate.ncomps = sinfo->ncolors ;
  }

  if ( buffer == NULL ) {
    buffer = (uint8 *)mm_alloc(mm_pool_temp, bufsize, MM_ALLOC_CLASS_SHADING) ;
    if ( buffer == NULL ) {
      if ( bfstate.cvalues != NULL && bfstate.ncomps != sinfo->ncolors ) {
        mm_free(mm_pool_temp, (mm_addr_t)bfstate.cvalues, bfstate.ncomps * 2 * sizeof(uint32)) ;
      }
      (void)error_handler(VMERROR) ;
      return NULL ;
    }
  }

  init_filelist_struct(&bflist,
                       NAME_AND_LENGTH("%blend"),
                       READ_FLAG | OPEN_FLAG | FILTER_FLAG , /* Flags */
                       /* Not entirely happy with this cast. We seem
                          to set the file descriptor (which ought to
                          use the VOIDPTR_TO_DEVICE_FILEDESCRIPTOR
                          macro), but the callbacks use the private
                          data which is a void *. As of 29-Dec-2006,
                          this is the only case in the RIP where this
                          happens. If its not needed, perhaps its
                          usage ought to be removed. --johnk */
                       (DEVICE_FILEDESCRIPTOR)((intptr_t)&bfstate), /* FilterPrivate */
                       buffer, /* Buffer */
                       bufsize, /* bufsize */
                       blendFileFillBuff, /* fillbuff */
                       FilterFlushBufError, /* flushbuff */
                       blendFileInit, /* initfile */
                       FilterCloseFile, /* closefile */
                       blendFileDispose, /* disposefile */
                       FilterBytesAvailNoOp, /* bytesavail */
                       FilterNoOp, /* resetfile */
                       FilterFilePosNoOp, /* filepos */
                       FilterSetFilePosNoOp, /* setfilepos */
                       FilterNoOp, /* flushfile */
                       FilterEncodeError, /* filterencode */
                       FilterDecodeError, /* filterdecode */
                       FilterLastError, /* lasterror */
                       0, /* filterstate */
                       NULL , /* devicelist */
                       NULL, /* underlying file */
                       NULL /*next */
                       ) ;

  bfstate.nsteps = nsteps ;
  bfstate.width = width ;
  bfstate.stepsize = height / nsteps ;
  bfstate.seed = 0 ; /* ? */
  bfstate.index = 0 ;
  bfstate.changeindex = bfstate.stepsize / 2 ;
  bfstate.sinfo = sinfo ;
  bfstate.d0 = d0 ;
  bfstate.d1 = d1 ;

  if ( sinfo->antialias ) { /* Current color is start of domain, next is first step */
    int32 ci ;
    USERVALUE dc ;

    HQASSERT(sinfo->base_index == GSC_SHFILL, "Blend does not have GSC_SHFILL colorType") ;

    if ( !shading_color_function(&d0, TRUE, sinfo) )
      return NULL ;

    for ( ci = 0 ; ci < sinfo->ncolors ; ++ci ) {
      SYSTEMVALUE range[2] ;

      if ( !gsc_range(gstateptr->colorInfo, GSC_SHFILL, ci, range) )
        return NULL ;

      BLEND_COLOR_CURRENT(&bfstate, ci) = (uint32)(((1 << BLEND_FILE_BPC) - 1) * (sinfo->scratch[ci] - range[0]) / (range[1] - range[0])) ;
    }

    dc = (USERVALUE)(d0 + 0.5 * (d1 - d0) / nsteps) ;

    if ( !shading_color_function(&dc, TRUE, sinfo) )
      return NULL ;

    for ( ci = 0 ; ci < sinfo->ncolors ; ++ci ) {
      SYSTEMVALUE range[2] ;

      if ( !gsc_range(gstateptr->colorInfo, GSC_SHFILL, ci, range) )
        return NULL ;

      BLEND_COLOR_NEXT(&bfstate, ci) = (uint32)(((1 << BLEND_FILE_BPC) - 1) * (sinfo->scratch[ci] - range[0]) / (range[1] - range[0])) ;
    }
  }

  return &bflist ;
}

/** Create an image to represent the described axial shaded fill, and add it to
the display list.
*/
static Bool axialAsImage(SHADINGinfo *sinfo,
                         SYSTEMVALUE coords[4],
                         USERVALUE colors[2],
                         SYSTEMVALUE bh,
                         SYSTEMVALUE minstep,
                         sbbox_t *bbox)
{
  /* Fill in imageargs, then call DEVICE_IMAGE */
  IMAGEARGS imageargs ;
  OBJECT blendo = OBJECT_NOTVM_NOTHING ;
  int32 i, result ;
  FILELIST *flptr ;
  int32 nsteps, width, height ;
  SYSTEMVALUE bw = coords[2] - coords[0] ;  /* Blend width in blend space */

  SYSTEMVALUE xdpi = sinfo->page->xdpi, ydpi = sinfo->page->ydpi ;


  nsteps = blend_steps(colors[0], colors[1], minstep, sinfo) ;
  if ( nsteps == 0 )
    return FALSE ;

  width = 1 ;
  height = nsteps ;

  /* The "AntiAliasing" here isn't strictly antialiasing. Anti-aliasing
     can still lead to mach banding, since it preserves the monotonicity
     of colour change, and the transition between bands is at the same
     point. This code sub-divides the blend into smaller units and
     performs noise addition, to allow the eye to visually blend adjacent
     levels. The noise is added by probablistically selecting the
     previous, current, or next step's colour in the blend, depending on
     a psuedo-random factor. Noise addition is only performed if the
     blend can be subdivided into discrete pixels of about 1/100 inch.
     This number is lifted from the threshold at which Tom's "faster"
     image method is enabled. */
  if ( sinfo->antialias ) {
    SYSTEMVALUE vx, vy, len ;
    int32 wfac, hfac ;

    /* Get blend height in device pixels */
    MATRIX_TRANSFORM_DXY(0.0, bh, vx, vy, &thegsPageCTM(*gstateptr)) ;

    /* Convert to blend height in inches */
    len = sqrt((vx * vx) / (xdpi * xdpi) + (vy * vy) / (ydpi * ydpi)) ;

    /* Number of noise pixel divisions in blend height */
    wfac = (int32)ceil(len * NOISE_PIXELS_PER_INCH) ;

    /* If noise pixels are larger than a device pixel and there are
       enough of them, carry on */
    if ( wfac < NOISE_MIN_STEP_PIXELS ) {
      sinfo->antialias = FALSE ;
    } else {
      HQASSERT(wfac > 0, "Width factor not greater than zero") ;
      vx /= wfac ; /* Device pixels per noise pixel */
      vy /= wfac ;
      if ( fabs(vx) < 1.0 && fabs(vy) < 1.0 )
        sinfo->antialias = FALSE ;
    }

    /* Get blend step size in device pixels */
    MATRIX_TRANSFORM_DXY(1.0 / nsteps, 0.0, vx, vy, &thegsPageCTM(*gstateptr)) ;

    /* Convert to blend step size in inches */
    len = sqrt((vx * vx) / (xdpi * xdpi) + (vy * vy) / (ydpi * ydpi)) ;

    /* Number of noise pixel divisions in blend step */
    hfac = (int32)ceil(len * NOISE_PIXELS_PER_INCH) ;

    /* If noise pixels are larger than a device pixel and there are
       enough of them, carry on */
    if ( hfac < NOISE_MIN_STEP_PIXELS ) {
      sinfo->antialias = FALSE ;
    } else {
      HQASSERT(hfac > 0, "Height factor not greater than zero") ;
      vx /= hfac ; /* Device pixels per noise pixel */
      vy /= hfac ;
      if ( fabs(vx) < 1.0 && fabs(vy) < 1.0 )
        sinfo->antialias = FALSE ;
    }

    if ( sinfo->antialias ) {
      width *= wfac ;
      height *= hfac ;
    }
  }

  flptr = blend_filelist(nsteps, width, height, sinfo, colors[0], colors[1]) ;

  if ( flptr == NULL )
    return FALSE ;

  file_store_object(&blendo, flptr, EXECUTABLE) ;

  init_image_args(&imageargs, GSC_SHFILL) ;
  imageargs.width = width;
  imageargs.height = height;
  imageargs.bits_per_comp = BLEND_FILE_BPC;
  imageargs.ncomps = sinfo->ncolors;
  imageargs.nprocs = 1;

  HQASSERT(sinfo->base_index == GSC_SHFILL, "Blend does not have GSC_SHFILL colorType") ;

  imageargs.image_color_space = gsc_getcolorspace(gstateptr->colorInfo, GSC_SHFILL);
  imageargs.image_color_object = *gsc_getcolorspaceobject(gstateptr->colorInfo, GSC_SHFILL);

  /* Map blend space rectangle to imagespace rectangle */
  imageargs.omatrix.matrix[0][0] = 0.0;
  imageargs.omatrix.matrix[1][1] = 0.0;
  if ( bw != 0.0 ) {
    imageargs.omatrix.matrix[0][1] = height / bw;
    imageargs.omatrix.matrix[2][1] = -height * coords[0] / bw;
  } else {
    imageargs.omatrix.matrix[0][1] = height;
    imageargs.omatrix.matrix[2][1] = -height * coords[0];
  }
  if ( bh != 0.0 ) {
    imageargs.omatrix.matrix[1][0] = width / bh;
    imageargs.omatrix.matrix[2][0] = -width * bbox->y1 / bh;
  } else {
    imageargs.omatrix.matrix[1][0] = width;
    imageargs.omatrix.matrix[2][0] = -width * bbox->y1;
  }
  MATRIX_SET_OPT_BOTH(&(imageargs.omatrix));

  /* Dynamic callback to blend space interpolator */
  /* Allocate N-color arrays for image data src. */
  if ( ! alloc_image_datasrc( & imageargs , 1 ))
    return FALSE ;
  OCopy(imageargs.data_src[0], blendo);

  imageargs.stack_args = 0;
  imageargs.imageop = NAME_image;
  imageargs.imagetype = TypeImageImage; /* default */
  imageargs.polarity = FALSE;
  imageargs.interpolate = FALSE;
  imageargs.fit_edges = TRUE;
  imageargs.interleave = INTERLEAVE_NONE; /* default (no mask) */
  imageargs.lines_per_block = imageargs.height;
  imageargs.image_pixel_centers = FALSE ;
  imageargs.clean_matrix = FALSE ;

  /* If doing fill coercion, then image must be directly coerced to fill */
  imageargs.coerce_to_fill = ( uint8 )( sinfo->coercion == NAME_Fill );

  /* Allocate N-color arrays for image decode. */
  if ( ! alloc_image_decode( & imageargs , sinfo->ncolors ))
    return FALSE ;
  for ( i = 0 ; i < sinfo->ncolors * 2 ; i += 2 ) {
    imageargs.decode[i + 0] = 0.0f;
    imageargs.decode[i + 1] = 1.0f;
  }

  imageargs.maskgen = NULL;
  imageargs.maskargs = NULL;

  set_image_order(&imageargs) ;

  /* operandstack not used, since theIArgsStackArgs is 0 */
  result = DEVICE_IMAGE(sinfo->page, &operandstack, &imageargs) ;

  free_image_args( & imageargs ) ;

  return result ;
}

static Bool radialAsFills(USERVALUE colors[2], USERVALUE opacity[2], int32 extend[2],
                          blend_arc_t arc, SHADINGinfo *sinfo,
                          SYSTEMVALUE minstep, SYSTEMVALUE d, SYSTEMVALUE dr,
                          SYSTEMVALUE r0, SYSTEMVALUE r1,
                          SYSTEMVALUE sx, SYSTEMVALUE sy,
                          SYSTEMVALUE x0, SYSTEMVALUE y0)
{
  PATHINFO pathinfo ;
  SYSTEMVALUE x1, y1 ;
  int32 i, numerator, halfi, solid, nsteps ;
  SYSTEMVALUE dhalf, dcentre, dhalf_opacity, dcentre_opacity, sr ;
  blend_arc_t oarc ; /* Final and interior arcs */
  SYSTEMVALUE epsilon = 0.0001 ;

  /* The opposite (or outside) arc is the part of the circle not covered by
     the normal arc. Set up arc vectors to draw it. */
  oarc.uax = -arc.uax, oarc.uay = -arc.uay ;
  oarc.upx = -arc.upx, oarc.upy = -arc.upy ;
  oarc.sintheta = -arc.sintheta, oarc.costheta = arc.costheta ;

  /* Swap tangent points and vectors */
  oarc.ptx = arc.qtx, oarc.pty = arc.qty ;
  oarc.qtx = arc.ptx, oarc.qty = arc.pty ;
  oarc.tpx = -arc.tqx, oarc.tpy = -arc.tqy ;
  oarc.tqx = -arc.tpx, oarc.tqy = -arc.tpy ;

  /* Now work out how many steps to put in the blend. We distribute the
     steps evenly over the whole blend for visual uniformity, using the
     minimum number needed for satisfying the colour closeness and
     decomposition size tests. For the decomposition test, we need the
     minimum step in the function space as calculated earlier. */
  nsteps = blend_steps(colors[0], colors[1], minstep, sinfo) ;

  if ( nsteps == 0 )
    return FALSE ;

  /* A further optimisation. The halfi index is used to optimise out
     drawing the hidden sides of circles that will be painted over anyway.
     These are the circles that do not intersect the final blend circle.
     This formula is derived from the solution of x for the intersection of
     the lines:

        x = -y + d + r1

     and

        y = r0 + x * (r1 - r0) / d

     which represent the centre of a circle abutting the final blend circle
     and the radius of a circle at a distance x from the start of the
     blend. If there is no solution to this equation, then the outer edges
     of the start and end circles touch, and so the circles should be
     treated as contained. */

  if ( d + dr != 0 )
    halfi = (int32)((d - r0 - r1) * nsteps / (d + dr)) ;
  else
    halfi = 0 ;

  sr = dr / nsteps ; /* Difference between radii for each step */
  d = d / nsteps ; /* Distance between centre of each step */
  sx = sx / nsteps ; /* X and Y difference between centres for each step */
  sy = sy / nsteps ;
  epsilon = epsilon / nsteps ;

  dcentre = (colors[1] + colors[0]) * 0.5 ; /* Centre point of domain */
  dhalf = (colors[1] - colors[0]) * 0.5 ; /* Half width of domain */
  dcentre_opacity = (opacity[1] + opacity[0]) * 0.5 ; /* Centre point of opacity domain */
  dhalf_opacity = (opacity[1] - opacity[0]) * 0.5 ; /* Half width of opacity domain */

  /* We can only paint solid shapes if the extend end flag is set, and the
     final radius is less than the initial one or the circles are not
     contained. If the circles are contained and the final radius is larger
     than the initial one, then we are looking down the centre of a
     projected cone or cylinder, and the whole exterior of the outer circle
     will be painted. If the circles are not contained, then the projection
     will either taper to a point, or expand infinitely. */
  solid = (extend[1] && (sr < 0 || d > sr)) ;

  /* For each step in the blend, draw the difference between the start and
     end positions of the step. Depending on the intersection between the
     start and end positions and the solid flag, this contour may be a
     single circle, a torus, the convex hull of two circles with the
     intersection between the circles removed, or the convex hull of the
     two circles. I use EOFILL to draw the difference between the outer
     contour and the inner contour of the step, since it is easier to write
     the intersection point and tangent point to arc conversion to take a
     single orientation. */
  for ( i = 0, numerator = 1 - nsteps ; i < nsteps ; ++i, numerator += 2 ) {
    /* Interpolate color value. Sample points are mid-points of blend steps. */
    USERVALUE bcolor = (USERVALUE)((SYSTEMVALUE)numerator * dhalf / (SYSTEMVALUE)nsteps + dcentre) ;
    USERVALUE bopacity = (USERVALUE)((SYSTEMVALUE)numerator * dhalf_opacity / (SYSTEMVALUE)nsteps + dcentre_opacity) ;

    path_init(&pathinfo) ;

    /* Next centre and radius */
    r1 = r0 + sr ;
    x1 = x0 + sx ;
    y1 = y0 + sy ;

    if ( d <= fabs(sr) + epsilon ) { /* Circles are contained within each other */
      SYSTEMVALUE ax, ay, px, py ;

      /* Draw first circle if it is not degenerate. */
      if ( r0 > 0 ) {
        ax = arc.uax * r0 ;
        ay = arc.uay * r0 ;
        px = arc.upx * r0 ;
        py = arc.upy * r0 ;

        if ( !radial_blend_quadrant(MOVETO, x0, y0, ax, ay, px, py, &pathinfo) ||
             !radial_blend_quadrant(LINETO, x0, y0, px, py, -ax, -ay, &pathinfo) ||
             !radial_blend_quadrant(LINETO, x0, y0, -ax, -ay, -px, -py, &pathinfo) ||
             !radial_blend_quadrant(CLOSEPATH, x0, y0, -px, -py, ax, ay, &pathinfo) )
          return FALSE ;
      }

      /* Draw second circle if it is not degenerate, and it is either the
         outer contour or the inner contour when the final extend flag is not
         set. In the last case, the inside of the final circle will not be
         filled, so the previously drawn objects must show through. */
      if ( r1 > 0 && (!solid || sr > 0) ) {
        ax = arc.uax * r1 ;
        ay = arc.uay * r1 ;
        px = arc.upx * r1 ;
        py = arc.upy * r1 ;

        if ( !radial_blend_quadrant(MOVETO, x1, y1, ax, ay, px, py, &pathinfo) ||
             !radial_blend_quadrant(LINETO, x1, y1, px, py, -ax, -ay, &pathinfo) ||
             !radial_blend_quadrant(LINETO, x1, y1, -ax, -ay, -px, -py, &pathinfo) ||
             !radial_blend_quadrant(CLOSEPATH, x1, y1, -px, -py, ax, ay, &pathinfo) )
          return FALSE ;
      }
    } else {
      /* The outer contour of two non-contained circles is the convex hull
         of the circles. This is drawn by finding the tangent points of the
         lines connecting the circles on each circle, and drawing the
         outside arcs between these tangent points. Explicit lines are not
         needed to join the tangent points between the circles, since the
         arc operator can start with a line to the point on the arc. The
         tangent points are found by rotating the perpendicular axis to the
         axis between the blend centres. The amount to rotate is the same
         as the angle formed by a right angle triangle with the radius step
         as the opposite and the blend centre step as the hypotenuese. This
         is easily shown diagramatically, but harder to explain. */

      /* Curve round outside of r0. Arc covers 180 - 2 * theta degrees. */
      if ( !radial_blend_arc(blend_moveto, x0, y0, r0, &arc, &pathinfo) )
        return FALSE ;

      /* We can optimise out drawing half of the blend circle if we are
         going to paint over it anyway. This will happen if we are drawing
         solid, or the step would not intersect with the final blend
         circle. The latter test is converted into the halfi index before
         starting the loop, giving the last step index which does not
         intersect the final blend circle. */
      if ( solid || i < halfi ) {
        blend_arc_t sarc ;

        /* The other end of a solid step needs to be draws anti-clockwise */
        sarc.uax = arc.uax, sarc.uay = arc.uay ;
        sarc.upx = -arc.upx, sarc.upy = -arc.upy ;
        sarc.sintheta = arc.sintheta, sarc.costheta = arc.costheta ;

        /* Swap tangent points and vectors */
        sarc.ptx = arc.qtx, sarc.pty = arc.qty ;
        sarc.qtx = arc.ptx, sarc.qty = arc.pty ;
        sarc.tpx = arc.tqx, sarc.tpy = arc.tqy ;
        sarc.tqx = arc.tpx, sarc.tqy = arc.tpy ;

        /* Curve round inside of r1. Arc covers 180 - 2 * theta degrees. */
        if ( !radial_blend_arc(blend_lineto, x1, y1, r1, &sarc, &pathinfo) )
          return FALSE ;

        /* closepath to join back to start of first arc */
        if ( !path_close(CLOSEPATH, &pathinfo) )
          return FALSE ;
      } else { /* Have to drawn convex hull and intersection */
        /* Curve round outside of r1. Arc covers 180 + 2 * theta degrees. */
        if ( !radial_blend_arc(blend_lineto, x1, y1, r1, &oarc, &pathinfo) )
          return FALSE ;

        /* closepath to join back to start of first arc */
        if ( !path_close(CLOSEPATH, &pathinfo) )
          return FALSE ;

        /* Draw inner contour, which is arcs between intersection of two
           circles. Only draw the arcs if the circles intersect, are not
           degenerate, and we are not going to draw over the gap by filling
           the final shape anyway. The angles from the centres to the
           intersection points are found by determining the perpendicular
           distance from the blend axis to the intersection points, and the
           distance along the blend axis to closest point to the
           intersection point (which is where the perpendicular distance to
           the intersection point is measured). This yields the opposite
           and adjacent of a right handed triangle, the hypotenuese being
           the radius of the circle, from which the sin and cos of the
           angle between the blend axis and the vectors to the intersection
           points. Having found the angle from the blend axis to the
           intersection point vectors, the angle 90 - theta is correct to
           plug into the arc-drawing routine used to draw the arcs between
           tangent points. We never need to calculate the angle explicitly,
           since we use the sin and cos of it. */
        if ( d < r0 + r1 && r0 > 0 && r1 > 0 ) {
          SYSTEMVALUE yd, tmp, d2 ;
          SYSTEMVALUE s = (d + r0 + r1) * 0.5 ;
          blend_arc_t iarc, ioarc ; /* Interior arcs */

          /* Perpendicular distance from blend axis to intersection. This is
             derived from the formula for the area of a triangle, which is:

             area = sqrt(S * (S - A) * (S - B) * (S - C))

             where

             S = (A + B + C) / 2

             and A, B, and C are the lengths of the triangle sides.

             The area of a triangle with one side A is the same as half of the
             area of a rectangle with one side A and the other side the length
             of the perpendicular distance from A to the point joining B & C.

             From this, the perpendicular distance is:

             p = 2 * area / A

          */
          yd = 2.0 * sqrt(s * (s - d) * (s - r0) * (s - r1)) / d ;

          /* Work out factor of d to intersection of perpendicular and d */
          d2 = d * 0.5 ;
          tmp = (r0 * r0 - r1 * r1) / (d * 2.0) ;

          iarc.costheta = yd / r0 ; /* sin(90 - theta) */
          iarc.sintheta = (d2 + tmp) / r0 ; /* cos(90 - theta) */

          iarc.uax = oarc.uax ;
          iarc.uay = oarc.uay ;
          iarc.upx = oarc.upx ;
          iarc.upy = oarc.upy ;

          iarc.tpx = iarc.uax * iarc.costheta + iarc.upx * iarc.sintheta ;
          iarc.tpy = iarc.uay * iarc.costheta + iarc.upy * iarc.sintheta ;
          iarc.tqx = iarc.uax * iarc.costheta - iarc.upx * iarc.sintheta ;
          iarc.tqy = iarc.uay * iarc.costheta - iarc.upy * iarc.sintheta ;
          iarc.ptx = iarc.upx * iarc.costheta - iarc.uax * iarc.sintheta ;
          iarc.pty = iarc.upy * iarc.costheta - iarc.uay * iarc.sintheta ;
          iarc.qtx = -iarc.upx * iarc.costheta - iarc.uax * iarc.sintheta ;
          iarc.qty = -iarc.upy * iarc.costheta - iarc.uay * iarc.sintheta ;

          /* curve round inside of r0 */
          if ( !radial_blend_arc(blend_moveto, x0, y0, r0, &iarc, &pathinfo) )
            return FALSE ;

          ioarc.costheta = yd / r1 ; /* sin(90 - theta) */
          ioarc.sintheta = (d2 - tmp) / r1 ; /* cos(90 - theta) */

          ioarc.uax = arc.uax ;
          ioarc.uay = arc.uay ;
          ioarc.upx = arc.upx ;
          ioarc.upy = arc.upy ;

          ioarc.tpx = ioarc.uax * ioarc.costheta + ioarc.upx * ioarc.sintheta ;
          ioarc.tpy = ioarc.uay * ioarc.costheta + ioarc.upy * ioarc.sintheta ;
          ioarc.tqx = ioarc.uax * ioarc.costheta - ioarc.upx * ioarc.sintheta ;
          ioarc.tqy = ioarc.uay * ioarc.costheta - ioarc.upy * ioarc.sintheta ;
          ioarc.ptx = ioarc.upx * ioarc.costheta - ioarc.uax * ioarc.sintheta ;
          ioarc.pty = ioarc.upy * ioarc.costheta - ioarc.uay * ioarc.sintheta ;
          ioarc.qtx = -ioarc.upx * ioarc.costheta - ioarc.uax * ioarc.sintheta ;
          ioarc.qty = -ioarc.upy * ioarc.costheta - ioarc.uay * ioarc.sintheta ;

          /* join to curve round inside of r1 */
          if ( !radial_blend_arc(blend_lineto, x1, y1, r1, &ioarc, &pathinfo) )
            return FALSE ;

          /* closepath to join back to start */
          if ( !path_close(CLOSEPATH, &pathinfo) )
            return FALSE ;
        }
      }
    }

    /* fill or stroke path with interpolated color */
    if ( !shading_color(&bcolor, bopacity, TRUE, sinfo, FALSE) ||
         !blend_fill( &pathinfo )) {
      path_free_list(thePath(pathinfo), mm_pool_temp) ;
      return FALSE ;
    }

    path_free_list(thePath(pathinfo), mm_pool_temp) ;

    /* Update current centre and radius */
    r0 = r1 ;
    x0 = x1 ;  y0 = y1 ;
  }

  return TRUE ;
}

#endif /* defined(DEBUG_BUILD) || ! defined POOR_SHADING */


/** \defgroup axialsh Axial shading
    \ingroup shfill */
/** \{ */
/* Implementation of blend as two Gouraud triangles. Decompose for function
   discontinuities and colour before creating triangles. Single component
   functions only. */
static Bool axialblendgouraud(SYSTEMVALUE coords[4],
                              USERVALUE c0, USERVALUE c1,
                              USERVALUE opacity0, USERVALUE opacity1,
                              SYSTEMVALUE minstep,
                              SHADINGinfo *sinfo)
{
  int32 result, i ;
  SHADINGvertex *corners[4] ;
  SYSTEMVALUE *bbindexed ;

  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  /* Do not decompose blend for colour if it is smaller than minstep */
  if ( coords[2] - coords[0] > minstep ) {
    int32 blend_lin = UserParams.BlendLinearity + 2 ;
    dl_color_t dcolor0, dcolor1, dcolori, *icolors[2] ;
    int32 linear = TRUE ;
    dlc_context_t *dlc_context = sinfo->page->dlc_context;
    dl_color_t *dlc_current = dlc_currentcolor(dlc_context) ;

    dlc_clear(&dcolor0);
    dlc_clear(&dcolor1);
    dlc_clear(&dcolori);

    /* Convert startpoint of section to color */
    if ( !shading_color(&c0, opacity0, TRUE, sinfo, TRUE) )
      return FALSE ;

    /* Take copy of color generated */
    dlc_copy_release(dlc_context, &dcolor0, dlc_current) ;

    /* Convert endpoint of section to color */
    if ( !shading_color(&c1, opacity1, FALSE, sinfo, TRUE) ) {
      dlc_release(dlc_context, &dcolor0);
      return FALSE ;
    }

    /* Take copy of color generated */
    dlc_copy_release(dlc_context, &dcolor1, dlc_current) ;

    icolors[0] = &dcolor0 ;
    icolors[1] = &dcolor1 ;

    /* Test for linearity by interpolating interior points of section */
    for ( i = 1 ; i < blend_lin ; ++i ) { /* Interpolate points 1..n-1 */
      SYSTEMVALUE weights[2] ;
      USERVALUE bi, bi_opacity ;

      weights[0] = (SYSTEMVALUE)i / (SYSTEMVALUE)blend_lin ;
      weights[1] = (SYSTEMVALUE)(blend_lin - i) / (SYSTEMVALUE)blend_lin ;

      bi = (USERVALUE)(c0 * weights[0] + c1 * weights[1]) ;
      bi_opacity = (USERVALUE)(opacity0 * weights[0] + opacity1 * weights[1]) ;

      if ( !shading_color(&bi, bi_opacity, TRUE, sinfo, TRUE) ||
           !dlc_alloc_interpolate(dlc_context, 2, weights, &dcolori, icolors) ) {
        /* Release temporary refs to dl colors */
        dlc_release(dlc_context, &dcolor0);
        dlc_release(dlc_context, &dcolor1);
        return FALSE ;
      }

      if ( !shading_color_close_enough(sinfo, dlc_current, &dcolori,
                                       gsc_getRS(gstateptr->colorInfo)) ) {
#if defined(DEBUG_BUILD)
        if ( (shading_debug_flag & SHADING_DEBUG_INFO) != 0 ) {

          monitorf((uint8 *)"Axial blend function UID %d (%f..%f, sampled at %f) is not linear\n",
                   sinfo->base_fuid, c0, c1, bi) ;
        }
#endif
        /* Release ref to interpolated color */
        dlc_release(dlc_context, &dcolori);

        linear = FALSE ;
        break ;
      }

      /* Release ref to interpolated color */
      dlc_release(dlc_context, &dcolori);
    }

    dlc_release(dlc_context, &dcolor0);
    dlc_release(dlc_context, &dcolor1);

    if ( !linear ) {
      SYSTEMVALUE scoords[4] ;
      USERVALUE cmid = (USERVALUE)((c0 + c1) * 0.5) ;
      USERVALUE opacitymid = (USERVALUE)((opacity0 + opacity1) * 0.5);

      scoords[0] = coords[0] ;
      scoords[1] = coords[1] ;
      scoords[2] = (coords[0] + coords[2]) * 0.5 ;
      scoords[3] = coords[3] ;

      if ( !axialblendgouraud(scoords, c0, cmid, opacity0, opacitymid, minstep, sinfo) )
        return FALSE ;

      scoords[0] = scoords[2] ;
      scoords[2] = coords[2] ;

      if ( !axialblendgouraud(scoords, cmid, c1, opacitymid, opacity1, minstep, sinfo) )
        return FALSE ;

      return TRUE ;
    }
  }

  /* Blend section is close enough to linear */

  /* Allocate enough for four corners */
  if ( !vertex_alloc(corners, 4) )
    return error_handler(VMERROR) ;

  bbox_as_indexed(bbindexed, &sinfo->bbox) ;

  for ( i = 0 ; i < 4 ; ++i ) {
    SYSTEMVALUE x = coords[(i & 1) << 1], y = bbindexed[(i & 2) | 1] ;

    MATRIX_TRANSFORM_XY(x, y, corners[i]->x, corners[i]->y,
                        &theIgsPageCTM(gstateptr)) ;
    corners[i]->comps[0] = (i & 1) ? c1 : c0 ;
    corners[i]->opacity = (i & 1) ? opacity1 : opacity0;
  }

  result = (DEVICE_GOURAUD(sinfo->page, corners[0], corners[1], corners[2], sinfo) &&
            DEVICE_GOURAUD(sinfo->page, corners[1], corners[2], corners[3], sinfo)) ;

  vertex_free(sinfo, corners, 4) ;

  return result ;
}

static Bool axialblendfn(SYSTEMVALUE coords[4],
                         USERVALUE c0, USERVALUE c1,
                         USERVALUE opacity0, USERVALUE opacity1,
                         SYSTEMVALUE minstep, SHADINGinfo *sinfo)
{
  /* Use two gouraud triangles for blend sections. Decompose for function and
     colour first. */
  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  HQASSERT( sinfo->nfuncs != 0 && sinfo->ncomps == 1, "Bad functions in axialblendfn") ;

#if defined( DEBUG_BUILD )
  if ( (shading_debug_flag & SHADING_DEBUG_DISCONTINUITY) == 0 )
#endif
  if ( c0 != c1 ) {
    int32 i ;
    USERVALUE bounds[2] ;

    /* It doesn't matter which way round we look, the discontinuity should
       still be there. So sort the bounds. */
    if ( c0 < c1 ) {
      bounds[0] = c0 ;
      bounds[1] = c1 ;
    } else if ( c0 > c1 ) {
      bounds[0] = c1 ;
      bounds[1] = c0 ;
    }

    for ( i = 0 ; i < sinfo->nfuncs ; ++i ) {
      int32 order ;
      USERVALUE discont ;

      if ( !(sinfo->funcs ?
             fn_find_discontinuity(&sinfo->funcs[i], 0, bounds,
                                   &discont, &order,
                                   FN_SHADING, i,
                                   sinfo->base_fuid, FN_GEN_NA,
                                   &sinfo->fndecode) :
             rcbs_fn_find_discontinuity(sinfo->rfuncs[i], 0, bounds,
                                        &discont, &order)) )
        return FALSE ;

      if ( order != -1 ) {
        SYSTEMVALUE scoords[4] ;
        USERVALUE d0 = (discont - c0) / (c1 - c0) ;
        USERVALUE opacityd = opacity0 + d0 * (opacity1 - opacity0);

        HQASSERT(discont > bounds[0] && discont < bounds[1],
                 "Discontinuity not contained in range") ;

        /* Decompose into two Gouraud triangles, interpolating X coordinates */
        scoords[0] = coords[0] ;
        scoords[1] = coords[1] ;
        scoords[2] = discont ; /* Coordinates are normalised to 0..1 */
        scoords[3] = coords[3] ;

        /* This assert attempts to check that the coordinate system matches the
           function domain. The coordinates are calculated in double, the
           function discontinuities in single precision, so we need an epsilon
           to check. */
        HQASSERT(fabs((discont - coords[0]) * (c1 - c0) - (discont - c0) * (coords[2] - coords[0])) < EPSILON,
                 "Interpolation to discontinuity failed") ;

        if ( !axialblendfn(scoords, c0, discont, opacity0, opacityd, minstep, sinfo) )
          return FALSE ;

        scoords[0] = scoords[2] ;
        scoords[2] = coords[2] ;

        if ( !axialblendfn(scoords, discont, c1, opacityd, opacity1, minstep, sinfo) )
          return FALSE ;

        return TRUE ;
      }
    }
  } /* else if bounds are same, there can't be a discontinuity */

  return axialblendgouraud(coords, c0, c1, opacity0, opacity1, minstep, sinfo) ;
}

Bool axialblend(SYSTEMVALUE coords[4], USERVALUE colors[2], USERVALUE opacity[2],
                Bool extend[2], SHADINGinfo *sinfo)
{
  SYSTEMVALUE minstep, minshadesize = UserParams.MinShadingSize ;
  SYSTEMVALUE x0, x1, y0, y1, vx, vy ;
  OMATRIX bmat ;
  sbbox_t *bbox = &sinfo->bbox ;
  SYSTEMVALUE bh ;
  SYSTEMVALUE xdpi = sinfo->page->xdpi, ydpi = sinfo->page->ydpi ;

  /* Limit minshadesize to 1 device pixel minimum. In the case of non-square
     resolutions, it is limited to the larger userspace equivalent of the
     device pixel sides. */
  if ( minshadesize < 72.0 / xdpi )
    minshadesize = 72.0 / xdpi ;
  if ( minshadesize < 72.0 / ydpi )
    minshadesize = 72.0 / ydpi ;

  /* For shading type 2 (axial), transform coordinate space so that (0,0)
     maps to (x0,y0), (1,0) maps to (x1,y1), and lengths are preserved in
     both directions. Then adjust bbox to this new space. In the new space,
     points perpendicular to the x-axis share the same colour, so axial
     blends can be created by rectangles orthogonal to these axes. The
     transformed bbox defines the area that may be filled; the parts of that
     bbox with x coordinates in the range 0-1 will have the shading colour
     applied, parts with x coordinates less than 0 will have the start colour
     filled if the start extend flag is set, parts with x coordinates greater
     than 1 will have the end colour filled if the end extend flag is set. */

  /* Create matrix mapping from blend space into userspace. Matrix is:
     [ (x1-x0) (y1-y0) -(y1-y0) (x1-x0) x0 y0 ] */

  x0 = coords[0] ;
  y0 = coords[1] ;
  x1 = coords[2] ;
  y1 = coords[3] ;

  vx = (x1 - x0) ;
  vy = (y1 - y0) ;

  /* Don't paint anything if axis is degenerate */
  if ( vx*vx + vy*vy == 0.0 )
    return TRUE ;

  bmat.matrix[ 0 ][ 0 ] =  vx ;
  bmat.matrix[ 0 ][ 1 ] =  vy ;
  bmat.matrix[ 1 ][ 0 ] = -vy ;
  bmat.matrix[ 1 ][ 1 ] =  vx ;
  bmat.matrix[ 2 ][ 0 ] =  x0 ;
  bmat.matrix[ 2 ][ 1 ] =  y0 ;
  MATRIX_SET_OPT_BOTH(&bmat) ;

  /* Concat blend space matrix to user space matrix to create single
     transformation from blend space to device space. */
  gs_modifyctm(&bmat) ;

  /* Create inverted matrix to map userspace to blend space. */
  if ( !matrix_inverse(&bmat, &bmat) )
    return error_handler(UNDEFINEDRESULT) ;

  /* Convert BBox and endpoints into blend space. (Endpoints should map
     to (0,0) and (1,0).) */
  bbox_transform(bbox, bbox, &bmat) ;

#if defined( ASSERT_BUILD )
  MATRIX_TRANSFORM_XY(x0, y0, x0, y0, &bmat) ;
  MATRIX_TRANSFORM_XY(x1, y1, x1, y1, &bmat) ;
  HQASSERT(fabs(x0) < EPSILON && fabs(y0) < EPSILON &&
           fabs(x1 - 1.0) < EPSILON && fabs(y1) < EPSILON,
           "Blend space matrix wrong in shading_function") ;
#endif

  coords[0] = 0.0 ;
  coords[1] = 0.0 ;
  coords[2] = 1.0 ;
  coords[3] = 0.0 ;

  /* Now work out minstep. Find length of blend in device space, work out
     factor to get step MinShadingSize long. This is the minimum blend-space
     increment. */

  MATRIX_TRANSFORM_DXY(1.0, 0.0, vx, vy, &thegsPageCTM(*gstateptr)) ;
  vx = vx / xdpi * 72.0 ; /* Convert vx,vy to default userspace */
  vy = vy / ydpi * 72.0 ;
  minstep = sqrt(vx * vx + vy * vy) ; /* Length in default userspace */

  if ( minstep > minshadesize )
    minstep = minshadesize / minstep ;
  else
    minstep = 1.0 ;

  /* minstep is now a factor of the distance between the blend endpoints to
     correspond to the minimum decomposition size. This will be used by
     blend_steps to help determine how many steps should be in the blend.
     Now work out how many steps to put in the blend. We distribute the steps
     evenly over the whole blend for visual uniformity, using the minimum
     number needed for satisfying the colour closeness and decomposition size
     tests. For the decomposition test, we need the minimum step in the
     function space as calculated earlier. */
  bh = bbox->y2 - bbox->y1 ;        /* Blend height */

  if ( extend[0] && bbox->x1 < coords[0] ) { /* Draw initial rectangle */
    /* Acrobat appears to use at most zero for the first extend. */
    USERVALUE extend_color = min(colors[0], 0) ;
    RECTANGLE rect ;

    rect.x = bbox->x1 ;
    rect.y = bbox->y1 ;
    rect.w = bbox->x2 ;
    if ( coords[0] < rect.w )
      rect.w = coords[0] ;
    rect.w -= rect.x ;
    rect.h = bh ;

    /* Set initial colour */
    if ( ! shading_color(&extend_color, opacity[0], FALSE, sinfo, FALSE) ||
         ! dorectfill(1, &rect, GSC_SHFILL, RECT_NOT_VIGNETTE|RECT_NOT_ERASE|RECT_NO_SETG))
      return FALSE ;
  }

  if ( coords[0] < bbox->x2 && coords[2] > bbox->x1 ) {
    Bool useImage = SystemParams.PoorShading ;
#if defined(DEBUG_BUILD)
    useImage = useImage ||
               (shading_debug_flag & SHADING_DEBUG_BLENDGOURAUD) != 0 ;
#endif

    /* The image method cannot be used where transparency is present. */
    if ( useImage && opacity[0] == 1 && opacity[1] == 1 ) {
      if ( !axialAsImage(sinfo, coords, colors, bh, minstep, bbox) )
        return FALSE ;
    } else {
      if ( !axialblendfn(coords, colors[0], colors[1], opacity[0], opacity[1], minstep, sinfo) )
        return FALSE ;
    }
  }

  if ( extend[1] && bbox->x2 > coords[2] ) { /* Draw final rectangle */
    /* Acrobat appears to use at least one for the second extend. */
    USERVALUE extend_color = max(colors[1], 1) ;
    RECTANGLE rect ;

    rect.x = coords[2] ;
    if ( bbox->x1 > rect.x )
      rect.x = bbox->x1 ;
    rect.y = bbox->y1 ;
    rect.w = bbox->x2 - rect.x ;
    rect.h = bh ;

    /* Set final colour */
    if ( ! shading_color(&extend_color, opacity[1], TRUE, sinfo, FALSE) ||
         ! dorectfill(1, &rect, GSC_SHFILL, RECT_NOT_VIGNETTE|RECT_NOT_ERASE|RECT_NO_SETG))
      return FALSE ;
  }

  return TRUE ;
}
/** \} */

/* Implementation of radial blend as four Coons patches. Decompose for
   discontinuities and colour before generating patches. Single component
   functions only. */
/** \defgroup radialsh Radial shading
    \ingroup shfill */
/** \{ */
static Bool radialblendpatch(SYSTEMVALUE coords[6],
                             USERVALUE c0, USERVALUE c1,
                             USERVALUE opacity0, USERVALUE opacity1,
                             SYSTEMVALUE ux, SYSTEMVALUE uy,
                             SYSTEMVALUE vx, SYSTEMVALUE vy,
                             SYSTEMVALUE t0x, SYSTEMVALUE t0y,
                             SYSTEMVALUE t1x, SYSTEMVALUE t1y,
                             SYSTEMVALUE cf,
                             SHADINGinfo *sinfo)
{
  SHADINGvertex *corners[4] ;
  SHADINGtensor tensor = { NULL, NULL, {NULL, NULL},
                           {
                             {{0, 0}, {0, 0}, {0, 0}, {0, 0}},
                             {{0, 0}, {0, 0}, {0, 0}, {0, 0}},
                             {{0, 0}, {0, 0}, {0, 0}, {0, 0}},
                             {{0, 0}, {0, 0}, {0, 0}, {0, 0}},
                           },
                           { 0, 0 }
  } ;
  SHADINGpatch patch ;
  int32 result, up, ut ;

  /* Allocate enough for four corners */
  if ( !vertex_alloc(corners, 4) )
    return error_handler(VMERROR) ;

  tensor.sinfo = sinfo ;
  tensor.patch = &patch ;
  tensor.pos[0] = tensor.pos[1] = 1 ;
  tensor.neighbors[0] = tensor.neighbors[1] = NULL ;

  patch.spos[TENSOR_U] = patch.spos[TENSOR_V] = SPOS_OF_ONE ; /* =find_spos(1) */
  HQASSERT(patch.spos[TENSOR_U] < 0 && (patch.spos[TENSOR_U] << 1) == 0,
           "SPOS_OF_ONE is not just top (sign) bit") ;
  patch.decomp_type = TENSOR_UV_INVALID ;
  patch.children = NULL ;

  patch.corner[0][0] = corners[0] ;
  patch.corner[0][1] = corners[1] ;
  patch.corner[1][0] = corners[2] ;
  patch.corner[1][1] = corners[3] ;

  patch.corner[0][0]->comps[0] = patch.corner[1][0]->comps[0] = c0 ;
  patch.corner[0][1]->comps[0] = patch.corner[1][1]->comps[0] = c1 ;

  patch.corner[0][0]->opacity = patch.corner[1][0]->opacity = opacity0 ;
  patch.corner[0][1]->opacity = patch.corner[1][1]->opacity = opacity1 ;

  patch.corner[0][0]->x = coords[0] + vx * coords[2] ;
  patch.corner[0][0]->y = coords[1] + vy * coords[2] ;

  patch.corner[1][0]->x = coords[0] + ux * coords[2] ;
  patch.corner[1][0]->y = coords[1] + uy * coords[2] ;

  patch.corner[0][1]->x = coords[3] + vx * coords[5] ;
  patch.corner[0][1]->y = coords[4] + vy * coords[5] ;

  patch.corner[1][1]->x = coords[3] + ux * coords[5] ;
  patch.corner[1][1]->y = coords[4] + uy * coords[5] ;

  /* Linearly interpolate constant U edges */
  for ( up = ut = 0 ; up < 2 ; ++up, ut += 3 ) {
    int32 vp, vt ; ;

    for ( vp = vt = 0 ; vp < 2 ; ++vp, vt += 3 ) {
      int32 v1 = vt ^ 1, v2 = vt ^ 2 ; /* One third, two-thirds indices */

      /* Ensure tensor corners are exactly the same as patch corners */
      tensor.coord[ut][vt][0] = patch.corner[up][vp]->x ;
      tensor.coord[ut][vt][1] = patch.corner[up][vp]->y ;

      /* Interpolate other edge points */
      tensor.coord[ut][v1][0] = (patch.corner[up][0]->x * v2 + patch.corner[up][1]->x * v1) / 3.0 ;
      tensor.coord[ut][v1][1] = (patch.corner[up][0]->y * v2 + patch.corner[up][1]->y * v1) / 3.0 ;
    }
  }

  /* Radially interpolate constant V edges */
  tensor.coord[1][0][0] = tensor.coord[0][0][0] + t0x * coords[2] * cf ;
  tensor.coord[1][0][1] = tensor.coord[0][0][1] + t0y * coords[2] * cf ;
  tensor.coord[2][0][0] = tensor.coord[3][0][0] + t1x * coords[2] * cf ;
  tensor.coord[2][0][1] = tensor.coord[3][0][1] + t1y * coords[2] * cf ;
  tensor.coord[1][3][0] = tensor.coord[0][3][0] + t0x * coords[5] * cf ;
  tensor.coord[1][3][1] = tensor.coord[0][3][1] + t0y * coords[5] * cf ;
  tensor.coord[2][3][0] = tensor.coord[3][3][0] + t1x * coords[5] * cf ;
  tensor.coord[2][3][1] = tensor.coord[3][3][1] + t1y * coords[5] * cf ;

  tensor_internal_coords(&tensor) ; /* Convert Coons to Tensor patch */
  result = tensor_draw(&tensor) ;

  vertex_free(sinfo, corners, 4) ;

  return result ;
}

/* Decompose for colour and then create two or four Coons patches. */
static Bool radialblendcoons(SYSTEMVALUE coords[6],
                             USERVALUE c0, USERVALUE c1,
                             USERVALUE opacity0, USERVALUE opacity1,
                             blend_arc_t *arc,
                             uint8 omitback,
                             SYSTEMVALUE minstep,
                             SHADINGinfo *sinfo)
{
  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  /* Do not decompose blend for colour if it is smaller than minstep */
  if ( fabs(c1 - c0) > minstep ) {
    int32 i ;
    int32 blend_lin = UserParams.BlendLinearity + 2 ;
    int32 linear = TRUE ;
    dlc_context_t *dlc_context = sinfo->page->dlc_context ;
    dl_color_t *dlc_current = dlc_currentcolor(dlc_context) ;
    dl_color_t dcolor0, dcolor1, dcolori, *icolors[2] ;

    dlc_clear(&dcolor0);
    dlc_clear(&dcolor1);
    dlc_clear(&dcolori);

    /* Convert startpoint of section to color */
    if ( !shading_color(&c0, opacity0, TRUE, sinfo, TRUE) )
      return FALSE ;

    /* Take copy of color generated */
    dlc_copy_release(dlc_context, &dcolor0, dlc_current) ;

    /* Convert endpoint of section to color */
    if ( !shading_color(&c1, opacity1, FALSE, sinfo, TRUE) ) {
      dlc_release(dlc_context, &dcolor0);
      return FALSE ;
    }

    /* Take copy of color generated */
    dlc_copy_release(dlc_context, &dcolor1, dlc_current) ;

    icolors[0] = &dcolor0 ;
    icolors[1] = &dcolor1 ;

    /* Test for linearity by interpolating interior points of section */
    for ( i = 1 ; i < blend_lin ; ++i ) { /* Interpolate points 1..n-1 */
      SYSTEMVALUE weights[2] ;
      USERVALUE bi, bi_opacity ;

      weights[0] = (SYSTEMVALUE)i / (SYSTEMVALUE)blend_lin ;
      weights[1] = (SYSTEMVALUE)(blend_lin - i) / (SYSTEMVALUE)blend_lin ;

      bi = (USERVALUE)(c0 * weights[0] + c1 * weights[1]) ;
      bi_opacity = (USERVALUE)(opacity0 * weights[0] + opacity1 * weights[1]) ;

      if ( !shading_color(&bi, bi_opacity, TRUE, sinfo, TRUE) ||
           !dlc_alloc_interpolate(dlc_context, 2, weights, &dcolori, icolors) ) {
        /* Release temporary refs to dl colors */
        dlc_release(dlc_context, &dcolor0);
        dlc_release(dlc_context, &dcolor1);
        return FALSE ;
      }

      if ( !shading_color_close_enough(sinfo, dlc_current, &dcolori,
                                       gsc_getRS(gstateptr->colorInfo)) ) {
#if defined(DEBUG_BUILD)
        if ( (shading_debug_flag & SHADING_DEBUG_INFO) != 0 ) {

          monitorf((uint8 *)"Radial blend function UID %d (%f..%f, sampled at %f) is not linear\n",
                   sinfo->base_fuid, c0, c1, bi) ;
        }
#endif
        /* Release ref to interpolated color */
        dlc_release(dlc_context, &dcolori);

        linear = FALSE ;
        break ;
      }

      /* Release ref to interpolated color */
      dlc_release(dlc_context, &dcolori);
    }

    dlc_release(dlc_context, &dcolor0);
    dlc_release(dlc_context, &dcolor1);

    if ( !linear ) {
      SYSTEMVALUE scoords[6] ;
      USERVALUE cmid = (USERVALUE)((c0 + c1) * 0.5) ;
      USERVALUE opacitymid = (USERVALUE)((opacity0 + opacity1) * 0.5);

      /* Decompose into two segments, interpolating coordinates */
      scoords[0] = coords[0] ;
      scoords[1] = coords[1] ;
      scoords[2] = coords[2] ;
      scoords[3] = 0.5 * (coords[0] + coords[3]) ;
      scoords[4] = 0.5 * (coords[1] + coords[4]) ;
      scoords[5] = 0.5 * (coords[2] + coords[5]) ;

      if ( !radialblendcoons(scoords, c0, cmid, opacity0, opacitymid, arc,
                             omitback, minstep, sinfo) )
        return FALSE ;

      scoords[0] = scoords[3] ;
      scoords[1] = scoords[4] ;
      scoords[2] = scoords[5] ;
      scoords[3] = coords[3] ;
      scoords[4] = coords[4] ;
      scoords[5] = coords[5] ;

      if ( !radialblendcoons(scoords, cmid, c1, opacitymid, opacity1, arc, omitback, minstep, sinfo) )
        return FALSE ;

      return TRUE ;
    }
  }

  /* Blend section is close enough to linear */

  /* If the back patches cannot be seen, because the front ones wrap around
     the edges and there is a final extension, don't wasste time drawing
     them. */
  if ( omitback ) {
    /* If the final radius is larger than the initial, the tangent point is
       on the front quadrants, so we can completely omit the rear patches. */
    if ( arc->sintheta < 0.0 ) {
      /* See the comments in radial_blend_extend for details on how to
         calculate the circle factor */
      SYSTEMVALUE cf = ((4 * sqrt(0.5 - 0.5 * arc->costheta)) /
                        (3 + 3 * sqrt(0.5 + 0.5 * arc->costheta))) ;

#if defined(DEBUG_BUILD)
      if ( (shading_debug_flag & SHADING_DEBUG_RADIAL_Q1) == 0 )
#endif
        if ( !radialblendpatch(coords, c0, c1, opacity0, opacity1,
                               arc->upx, arc->upy, arc->ptx, arc->pty,
                               -arc->tpx, -arc->tpy, arc->uax, arc->uay,
                               cf, sinfo) )
          return FALSE ;

#if defined(DEBUG_BUILD)
      if ( (shading_debug_flag & SHADING_DEBUG_RADIAL_Q2) == 0 )
#endif
        if ( !radialblendpatch(coords, c0, c1, opacity0, opacity1,
                               -arc->upx, -arc->upy, arc->qtx, arc->qty,
                               -arc->tqx, -arc->tqy, arc->uax, arc->uay,
                               cf, sinfo) )
          return FALSE ;
    }
  } else {
#if defined(DEBUG_BUILD)
    if ( (shading_debug_flag & SHADING_DEBUG_RADIAL_Q1) == 0 )
#endif
      if ( !radialblendpatch(coords, c0, c1, opacity0, opacity1,
                             arc->upx, arc->upy, arc->uax, arc->uay,
                             arc->upx, arc->upy, arc->uax, arc->uay,
                             CIRCLE_FACTOR, sinfo) )
        return FALSE ;

#if defined(DEBUG_BUILD)
    if ( (shading_debug_flag & SHADING_DEBUG_RADIAL_Q2) == 0 )
#endif
      if ( !radialblendpatch(coords, c0, c1, opacity0, opacity1,
                             -arc->upx, -arc->upy, arc->uax, arc->uay,
                             -arc->upx, -arc->upy, arc->uax, arc->uay,
                             CIRCLE_FACTOR, sinfo) )
        return FALSE ;
  }

#if defined(DEBUG_BUILD)
  if ( (shading_debug_flag & SHADING_DEBUG_RADIAL_Q3) == 0 )
#endif
    if ( !radialblendpatch(coords, c0, c1, opacity0, opacity1,
                           -arc->uax, -arc->uay, arc->upx, arc->upy,
                           -arc->uax, -arc->uay, arc->upx, arc->upy,
                           CIRCLE_FACTOR, sinfo) )
      return FALSE ;

#if defined(DEBUG_BUILD)
  if ( (shading_debug_flag & SHADING_DEBUG_RADIAL_Q4) == 0 )
#endif
    if ( !radialblendpatch(coords, c0, c1, opacity0, opacity1,
                           -arc->uax, -arc->uay, -arc->upx, -arc->upy,
                           -arc->uax, -arc->uay, -arc->upx, -arc->upy,
                           CIRCLE_FACTOR, sinfo) )
      return FALSE ;

  return TRUE ;
}

/* Decompose for function discontinuities and then call colour decomposition */
static Bool radialblendfn(SYSTEMVALUE coords[6],
                          USERVALUE c0, USERVALUE c1,
                          USERVALUE opacity0, USERVALUE opacity1,
                          blend_arc_t *arc,
                          uint8 omitback,
                          SYSTEMVALUE minstep,
                          SHADINGinfo *sinfo)
{
  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  HQASSERT( sinfo->nfuncs != 0 && sinfo->ncomps == 1, "Bad functions in radialblendfn") ;

#if defined( DEBUG_BUILD )
  if ( (shading_debug_flag & SHADING_DEBUG_DISCONTINUITY) == 0 )
#endif
  if ( c0 != c1 ) {
    int32 i ;
    USERVALUE bounds[2] ;

    /* It doesn't matter which way round we look, the discontinuity should
       still be there. So sort the bounds. */
    if ( c0 < c1 ) {
      bounds[0] = c0 ;
      bounds[1] = c1 ;
    } else if ( c0 > c1 ) {
      bounds[0] = c1 ;
      bounds[1] = c0 ;
    }

    for ( i = 0 ; i < sinfo->nfuncs ; ++i ) {
      int32 order ;
      USERVALUE discont ;

      if ( !(sinfo->funcs ?
             fn_find_discontinuity(&sinfo->funcs[i], 0, bounds,
                                   &discont, &order,
                                   FN_SHADING, i,
                                   sinfo->base_fuid, FN_GEN_NA,
                                   &sinfo->fndecode) :
             rcbs_fn_find_discontinuity(sinfo->rfuncs[i], 0, bounds,
                                        &discont, &order)) )
        return FALSE ;

      if ( order != -1 ) {
        SYSTEMVALUE scoords[6] ;
        USERVALUE d0 = (discont - c0) / (c1 - c0) ;
        USERVALUE opacityd = opacity0 + d0 * (opacity1 - opacity0);

        HQASSERT(discont > bounds[0] && discont < bounds[1],
                 "Discontinuity not contained in range") ;

        /* Decompose into two segments, interpolating coordinates */
        scoords[0] = coords[0] ;
        scoords[1] = coords[1] ;
        scoords[2] = coords[2] ;
        scoords[3] = coords[0] + d0 * (coords[3] - coords[0]);
        scoords[4] = coords[1] + d0 * (coords[4] - coords[1]);
        scoords[5] = coords[2] + d0 * (coords[5] - coords[2]);

        if ( !radialblendfn(scoords, c0, discont, opacity0, opacityd, arc, omitback, minstep, sinfo) )
          return FALSE ;

        scoords[0] = scoords[3] ;
        scoords[1] = scoords[4] ;
        scoords[2] = scoords[5] ;
        scoords[3] = coords[3] ;
        scoords[4] = coords[4] ;
        scoords[5] = coords[5] ;

        if ( !radialblendfn(scoords, discont, c1, opacityd, opacity1, arc, omitback, minstep, sinfo) )
          return FALSE ;

        return TRUE ;
      }
    }
  } /* else if bounds are same, there can't be a discontinuity */

  return radialblendcoons(coords, c0, c1, opacity0, opacity1, arc, omitback, minstep, sinfo) ;
}

Bool radialblend(SYSTEMVALUE coords[6], USERVALUE colors[2], USERVALUE opacity[2],
                 Bool extend[2], SHADINGinfo *sinfo)
{
  PATHINFO pathinfo ;
  SYSTEMVALUE minstep, minstepx, minstepy ;
  SYSTEMVALUE minshadesize = UserParams.MinShadingSize ;
  SYSTEMVALUE x0, y0, r0, x1, y1, r1 ;
  SYSTEMVALUE d, dr, dx, dy, sx, sy, sd ;
  blend_arc_t arc ; /* Normalised arc vectors */
  Bool useFills ;

  /* Typecheck Radial blend values */

  /* Error if radii are negative */
  if ( coords[2] < 0.0 || coords[5] < 0.0 )
    return error_handler(RANGECHECK) ;

  /* Don't paint anything if both radii are zero */
  if ( coords[2] == 0.0 && coords[5] == 0.0 )
    return TRUE ;

  MATRIX_TRANSFORM_XY(coords[0], coords[1], x0, y0, &theIgsPageCTM(gstateptr)) ;
  MATRIX_TRANSFORM_XY(coords[3], coords[4], x1, y1, &theIgsPageCTM(gstateptr)) ;
  /* Vector between centres in dspace */
  sx = x1 - x0 ;
  sy = y1 - y0 ;

  /* Distance between blend centres. Optimise out if smaller than one pixel. */
  sd = sx * sx + sy * sy ;
  if ( sd < 1.0 ) {
    sd = d = 0.0 ;
    dx = 1.0 ; /* Unit vector, any old direction */
    dy = 0.0 ;
  } else {
    /* Vector between centres in uspace, used to calculate unit vectors */
    dx = coords[3] - coords[0] ;
    dy = coords[4] - coords[1] ;
    d = sqrt(dx * dx + dy * dy) ;
    dx /= d ; /* normalised vector between blend centres */
    dy /= d ;
    sd = sqrt(sd) / d ; /* Multiplier from uspace to dspace */
  }

  /* Convert unit vectors to device space. up is rotated 90 degrees
     anti-clockwise in userspace, and we normalise the device-space
     equivalent to be clockwise in device space (equivalent to anti-clockwise
     in default userspace). */
  MATRIX_TRANSFORM_DXY(dx, dy, arc.uax, arc.uay, &theIgsPageCTM(gstateptr)) ;
  MATRIX_TRANSFORM_DXY(-dy, dx, arc.upx, arc.upy, &theIgsPageCTM(gstateptr)) ;
  if ( arc.uax * arc.upy - arc.upx * arc.uay > 0 ) {
    arc.upx = -arc.upx ;
    arc.upy = -arc.upy ;
  }

  /* Radii and radius change. */
  r0 = coords[2] ; r1 = coords[5] ;
  dr = r1 - r0 ;

  HQASSERT(r0 >= 0 && r1 >= 0, "Radii must be non-negative in radialblend") ;
  HQASSERT(r0 > 0 || r1 > 0, "At least one radius must be non-zero in radialblend") ;


  /* There is a job which specifies circles such that one circle is
     just outside the other circle and the rip duly recognises this
     and draws the extensions in what we believe to be the correct way
     for the non-contained case. However, it appears that Acrobat
     treats the circles as being contained. See request 64235 for more
     details. */
  if ( d > (fabs(dr) + 0.0001)) { /* Not contained */
    arc.sintheta = dr / d ;
    arc.costheta = sqrt(1.0 - arc.sintheta * arc.sintheta) ;
  } else { /* Contained circles */
    arc.sintheta = (dr < 0) ? -1.0 : 1.0 ; /* +/-90 degrees is not possible */
    arc.costheta = 0.0 ;
  }

  /* Unit tangent vectors are the vectors from the tangent points to the
     start and end circles. These are the axis vector rotated theta and
     -theta degrees anti-clockwise (in default userspace, cw in device space)
     respectively. These expansions are NOT obvious, so don't mistake them
     for a normal single-vector rotation. We're using the combination of the
     axis and perpendicular device-space vectors to get the effect we want.
     Here's how the expansion works for the vector from the centre to the
     tangent point:

     In userspace, rotating the normalised axis vector (dx,dy)
     anti-clockwise by theta degrees is given by:

     txu = dx * costheta - dy * sintheta
     tyu = dx * sintheta + dy * costheta

     Translating this into a device-space distance gives:

     txd = txu * m00 + tyu * m10
     tyd = txu * m01 + tyu * m11

     Expanding and re-arranging the terms to group the costheta and sintheta
     terms, we get:

     txd = dx * m00 * costheta + dy * m10 * costheta +
           dx * m10 * sintheta - dy * m00 * sintheta
     tyd = dx * m01 * costheta + dy * m11 * costheta +
           dx * m11 * sintheta - dy * m01 * sintheta

     Observing that the unit axis vector (uax,uay) and the unit perpendicular
     vector are device-space translations of (dx,dy) and (-dy, dx):

     uax = dx * m00 + dy * m10
     uay = dx * m01 + dy * m11

     upx = dx * m10 - dy * m00
     upy = dx * m11 - dy * m01

     We can substitute these to get:

     tpx = uax * costheta + upx * sintheta
     tpy = uay * costheta + upy * sintheta

     Similar substitutions are performed for the other vectors. */
  arc.tpx = arc.uax * arc.costheta + arc.upx * arc.sintheta ;
  arc.tpy = arc.uay * arc.costheta + arc.upy * arc.sintheta ;

  /* Rotated the other way: */
  arc.tqx = arc.uax * arc.costheta - arc.upx * arc.sintheta ;
  arc.tqy = arc.uay * arc.costheta - arc.upy * arc.sintheta ;

  /* These vectors are the vectors from the centre point to the tangent
     points, rotated +theta and 180-theta degrees. First on the perpendicular
     side: */
  arc.ptx = arc.upx * arc.costheta - arc.uax * arc.sintheta ;
  arc.pty = arc.upy * arc.costheta - arc.uay * arc.sintheta ;

  /* And the opposite side: */
  arc.qtx = -arc.upx * arc.costheta - arc.uax * arc.sintheta ;
  arc.qty = -arc.upy * arc.costheta - arc.uay * arc.sintheta ;

  /* Limit minshadesize to 1 device pixel minimum. In the case of non-square
     resolutions, it is limited to the larger userspace equivalent of the
     device pixel sides. */
  if ( minshadesize < 72.0 / sinfo->page->xdpi )
    minshadesize = 72.0 / sinfo->page->xdpi ;
  if ( minshadesize < 72.0 / sinfo->page->ydpi )
    minshadesize = 72.0 / sinfo->page->ydpi ;

  /* Find out the minimum contour step size as a proportion of the function
     value difference between the start and end. This makes it easier to
     test if a colour decomposition is too small to bother with. The maximum
     contour difference in the each of the axis and perpendicular is tested. */
  minstepx = sx + dr * arc.uax ;
  minstepy = sy + dr * arc.uay ;
  if ( minstepx < 0.0 )
    minstepx = -minstepx ;
  if ( minstepy < 0.0 )
    minstepy = -minstepy ;

  dx = dr * arc.upx ;
  dy = dr * arc.upy ;
  if ( dx < 0.0 )
    dx = -dx ;
  if ( dy < 0.0 )
    dy = -dy ;

  /* Select larger device space contour difference for each axis */
  if ( dx > minstepx )
    minstepx = dx ;
  if ( dy > minstepy )
    minstepy = dy ;

  /* Now change minstepx and minstepy from device space max contour
     differences to default userspace differences. */
  minstepx = minstepx / sinfo->page->xdpi * 72.0 ; /* Default userspace equivalent */
  minstepy = minstepy / sinfo->page->ydpi * 72.0 ;

  /* Select larger user space contour difference and divide minshadesize by
     it to get function space step factor */
  if ( minstepx > minstepy )
    minstep = minstepx ;
  else
    minstep = minstepy ;

  if ( minstep > minshadesize )
    minstep = minshadesize / minstep ;
  else
    minstep = 1.0 ;

  /* minstep is now a factor of the distance between the blend endpoints to
     correspond to the minimum decomposition size. This will be used by
     blend_steps to help determine how many steps should be in the blend. */
  minstep *= fabs(colors[1] - colors[0]) ;

  /* Extend start. If start circle is smaller (i.e. dr > 0), then the cone
     tapers to a point, which may be inside or outside the BBox boundary. If
     the start circle is larger, the cone extends out to the intersection with
     the boundary. */
  if ( extend[0] ) {
    /* Draw full arc if overlapped and not doing a final extension, so the
       initial extension may show through the hole in the blend */
    /* Acrobat appears to use at most zero for the first extend. */
    USERVALUE extend_color = min(colors[0], 0) ;
    int32 opposite = (!extend[1] && r0 + r1 > d);
    path_init(&pathinfo) ;

    if ( !radial_blend_extend(x0, y0, r0, sd, &arc, -1, opposite, &pathinfo) ||
         !shading_color(&extend_color, opacity[0], FALSE, sinfo, FALSE) ||
         !blend_fill(&pathinfo)) {
      path_free_list(thePath(pathinfo), mm_pool_temp) ;
      return FALSE ;
    }

    path_free_list(thePath(pathinfo), mm_pool_temp) ;
  }

  useFills = SystemParams.PoorShading ;
#if defined(DEBUG_BUILD)
  useFills = useFills || (shading_debug_flag & SHADING_DEBUG_RADIALCOONS) != 0 ;
#endif

  if ( useFills ) {
    if ( !radialAsFills(colors, opacity, extend, arc, sinfo, minstep,
                        d, dr, r0, r1, sx, sy, x0, y0) )
      return FALSE ;
  } else {
    SYSTEMVALUE scoords[6] ;   /* Coordinates in device space */
    USERVALUE c2 = colors[0] ; /* Default to entire blend */
    USERVALUE opacity2 = opacity[0] ; /* Default to entire blend */
    uint8 omit = FALSE ;       /* Contained blends need all quadrants */

    scoords[0] = x0 ;
    scoords[1] = y0 ;
    scoords[2] = r0 ;

    if ( arc.costheta != 0.0 ) { /* Not contained */
      SYSTEMVALUE r2, d2 ;

      /* Determine if there is an initial portion of the blend that we can
         do with the back quadrants omitted or trimmed. This is possible if
         the blend is not contained, and either there is a final extension,
         or the back portions won't show. This happens for all values of
         the parameter for which the centre is such that the radius does
         not overlap the final circle. i.e., not within (r1 + r) of the
         final coordinate. The radius for which this happens is found by
         the relationships:

         sin(theta) = (r1 - r)/(r1 + r)

         So,

         (r1 + r) * sin(theta) = r1 - r

         and hence the expression below for r.

         We call the function with omitback TRUE even if the tangent is
         such that more that two quadrants are visible. The lowest-level
         decomposition will either omit the patches entirely, or trim them
         to extend to the visible tangent. */
      r2 = r1 * (1.0 - arc.sintheta) / (1.0 + arc.sintheta) ;
      d2 = (d - r2 - r1) / d ;

      if ( extend[1] || d2 >= 1.0 ) {
        /* If there is a final extension, it will obscure the inside of the
           final circle. Also, the blend may taper to a point, in which case
           the rear patches are obscured. */
        omit = TRUE ; /* Fall through to single final section */
      } else if ( d2 > EPSILON ) {
        HQASSERT(d2 < 1.0, "Shouldn't be interpolating past endpoint") ;

        scoords[3] = x0 + d2 * (x1 - x0) ;
        scoords[4] = y0 + d2 * (y1 - y0) ;
        scoords[5] = r2 ;
        HQASSERT(fabs(r0 + d2 * (r1 - r0) - r2) < EPSILON,
                 "Radius interpolation doesn't match calculation") ;

        c2 = (USERVALUE)(colors[0] + d2 * (colors[1] - colors[0])) ;
        opacity2 = (USERVALUE)(opacity[0] + d2 * (opacity[1] - opacity[0])) ;

        if ( !radialblendfn(scoords, colors[0], c2, opacity[0], opacity2, &arc, TRUE, minstep, sinfo) )
          return FALSE ;

        scoords[0] = scoords[3] ;
        scoords[1] = scoords[4] ;
        scoords[2] = scoords[5] ;
      }
    }

    /* Final section from intermediate point, if any, to final radius */
    scoords[3] = x1 ;
    scoords[4] = y1 ;
    scoords[5] = r1 ;

    if ( !radialblendfn(scoords, c2, colors[1], opacity2, opacity[1], &arc, omit, minstep, sinfo) )
      return FALSE ;
  }

  if ( extend[1] ) { /* Draw final shape */
    /* Acrobat appears to use at least one for the second extend. */
    USERVALUE extend_color = max(colors[1], 1) ;
    path_init(&pathinfo) ;

    /* fill final colour */
    if ( !radial_blend_extend(x1, y1, r1, sd, &arc, 1, FALSE, &pathinfo) ||
         !shading_color(&extend_color, opacity[1], TRUE, sinfo, FALSE) ||
         !blend_fill(&pathinfo)) {
      path_free_list(thePath(pathinfo), mm_pool_temp) ;
      return FALSE ;
    }

    path_free_list(thePath(pathinfo), mm_pool_temp) ;
  }

  return TRUE ;
}

/* Extension of radial blend. There are six possibilities, depending on
   whether the blend is contained or uncontained, and whether
   the radius increases, decreases or stays the same. The extension should
   always be contained within the boundaries of the cone. */
static Bool radial_blend_extend(SYSTEMVALUE cx, SYSTEMVALUE cy,
                                SYSTEMVALUE r, SYSTEMVALUE sd,
                                blend_arc_t *arc, int32 dirn,
                                int32 opposite, PATHINFO *pathinfo)
{
  SYSTEMVALUE d = 0, cross ;
  int32 i ;

  HQASSERT(dirn == 1 || dirn == -1, "Extension direction not +/-1") ;

  if ( arc->costheta == 0.0 ) { /* Contained blend. */
    SYSTEMVALUE ax, ay, px, py ;

    ax = arc->uax * r ;
    ay = arc->uay * r ;
    px = arc->upx * r ;
    py = arc->upy * r ;

    /* Grr. Adobe aren't consistent about the Extend key applied to identical
       circles at start & end of a blend. A true extension would paint values
       of the parameter in the parametric equation which are < 0 or > 1, which
       would just be the points on the same circle. However, they actually
       paint the initial extension as everything outside the circle, and the
       final extension as everything inside the circle. */

    if ( arc->sintheta * dirn > 0 ) { /* Outer path covers whole clipping bbox */
      SYSTEMVALUE x1, y1, x2, y2 ;

      x1 = cclip_bbox.x1 - 1.0 ;
      x2 = cclip_bbox.x2 + 1.0 ;
      y1 = cclip_bbox.y1 - 1.0 ;
      y2 = cclip_bbox.y2 + 1.0 ;

      if (! path_add_four(pathinfo, x1, y1, x1, y2, x2, y2, x2, y1))
        return FALSE;
    }

    /* Possible optimisations: initial extension && solid with sintheta <= 0
       draw clipping box without hole */

    /* Draw circle, either knocking out hole in outer contour if outer
       extension or creating outer contour if inner extension. Explicitly
       draw four quadrants rather than use gs_arcto, because we want to
       make the Beziers to exactly match the shading decomposition. */
    return (radial_blend_quadrant(MOVETO, cx, cy, ax, ay, px, py, pathinfo) &&
            radial_blend_quadrant(LINETO, cx, cy, px, py, -ax, -ay, pathinfo) &&
            radial_blend_quadrant(LINETO, cx, cy, -ax, -ay, -px, -py, pathinfo) &&
            radial_blend_quadrant(CLOSEPATH, cx, cy, -px, -py, ax, ay, pathinfo)) ;
  }

  /* Uncontained blend. If the blend is not disjoint or the final extension is,
     not used there is a hole in the middle of the blend through which the
     initial extension may show through. In this case, we must draw the inside
     of the initial extension arc. In either case, we make a copy of the arc
     so we can swap the order easily. */
  {
    blend_arc_t earc ;

    /* Mirror so that the inside of the arc is drawn in the opposite
       direction from normal so that we end up at the same tangent points we
       would have originally. */
    if ( opposite ) {
      /* Opposite arc needs to be draws anti-clockwise */
      earc.uax = -arc->uax, earc.uay = -arc->uay ;
      earc.upx = arc->upx, earc.upy = arc->upy ;
      earc.sintheta = -arc->sintheta, earc.costheta = arc->costheta ;

      /* Invert tangent vectors */
      earc.ptx = arc->ptx, earc.pty = arc->pty ;
      earc.qtx = arc->qtx, earc.qty = arc->qty ;
      earc.tpx = -arc->tpx, earc.tpy = -arc->tpy ;
      earc.tqx = -arc->tqx, earc.tqy = -arc->tqy ;
    } else
      earc = *arc ;

    if ( earc.sintheta > 0 && fabs(1.0 - earc.sintheta) > EPSILON ) {
      /* Apply a bodge to extensions to compensate for differing approximations
         of the same arc. This is because when the arc of the extension is
         smaller than 180 degrees, the Bezier approximation of the arc may not
         match the first or last circle contour exactly. The bodge is to move
         the centre and radius such that the contour shifts in or out exactly
         one device pixel. */
      sd *= 1.0 - earc.sintheta ;
      HQASSERT(sd != 0.0,
               "Extension distance factor indicates contained circles");
      sd = dirn / sd ;

      /* Only do the adjustment if the radius will remain positive. */
      if ( r > arc->sintheta * sd ) {
        r -= arc->sintheta * sd ; /* N.B. arc, not earc */

        cx -= arc->uax * sd ; /* N.B. arc, not earc */
        cy -= arc->uay * sd ;
      }
    }

    if ( !radial_blend_arc(blend_moveto, cx, cy, r, &earc, pathinfo) )
      return FALSE ;
  }

  /* Done with extension arc; now revert to using parameter arc for tangent
     computations. */

  if ( arc->sintheta * dirn < -EPSILON ) { /* Tangent vectors converge */
    /* Determine if intersection of tangent vectors is within clip box. If so,
       draw to that point and back. We find the userspace distance along the
       axis using the radius and angle, and multiply this by the unit axis
       vector. The sign of the angle automatically compensates for the
       direction of the axis; if doing an initial extension, sintheta will be
       positive but we want to go in the direction opposite the axis vector,
       if doing a final extension, sintheta will be negative but we want to
       go in the direction of the axis. We actually allow the intersection to
       be a short distance outside the clip box, because this method is easier
       than intersecting with the edges. */
    SYSTEMVALUE ud = r / arc->sintheta ;
    SYSTEMVALUE dx, dy ;

    dx = cx - ud * arc->uax ;
    dy = cy - ud * arc->uay ;

#define EXTEND_BBOX 4 /* pixels */
    if ( dx > cclip_bbox.x1 - EXTEND_BBOX &&
         dx < cclip_bbox.x2 + EXTEND_BBOX &&
         dy > cclip_bbox.y1 - EXTEND_BBOX &&
         dy < cclip_bbox.y2 + EXTEND_BBOX )
      return (blend_lineto(dx, dy, pathinfo) &&
              path_close(CLOSEPATH, pathinfo)) ;
  }

  /* Find the perpendicular line intersecting just beyond the furthest corner
     of the clip box. We can draw along the tangent to that line, then to the
     other tangent, and back, and guaranteed to be outside the clip box. */
  cross = arc->upx * arc->uay - arc->upy * arc->uax ;
  if ( cross >= -EPSILON && cross <= EPSILON )
    return error_handler(UNDEFINEDRESULT) ;

  for ( i = 0 ; i < 4 ; ++i )
  {
    SYSTEMVALUE xclip = (i & 1) ? cclip_bbox.x2 + EXTEND_BBOX:
                                  cclip_bbox.x1 - EXTEND_BBOX;
    SYSTEMVALUE yclip = (i & 2) ? cclip_bbox.y2 + EXTEND_BBOX:
                                  cclip_bbox.y1 - EXTEND_BBOX;
    SYSTEMVALUE t ;

    /* Find intersection of axis and perpendicular going through corner. We get
       this as a proportion of the length of the unit axis vector to the
       intersection point. */
    t = (arc->upy * (cx - xclip) - arc->upx * (cy - yclip)) / cross ;
    if ( t * dirn > d * dirn )
      d = t ;
  }

  /* We need to draw to the intersection of the tangents and the furthest
     perpendicular. The distance factor is the userspace multiplier of the
     unit axis vector to the required perpendicular. */
  cx += arc->uax * d ; /* Convert centre to intersection point */
  cy += arc->uay * d ;

  /* Convert distance along axis to distance along perpendicular, and add to
     current radius. */
  r += d * arc->sintheta ;
  r /= arc->costheta ;

  return (blend_lineto(cx - r * arc->upx, cy - r * arc->upy, pathinfo) &&
          blend_lineto(cx + r * arc->upx, cy + r * arc->upy, pathinfo) &&
          path_close(CLOSEPATH, pathinfo)) ;
}

/* Add a quadrant to a path, using scaled unit vectors. The path centre is in
   userspace, the unit vectors are in device space, the radius is a userspace
   distance. */
static Bool radial_blend_quadrant(int32 type,
                                  SYSTEMVALUE cx, SYSTEMVALUE cy,
                                  SYSTEMVALUE ax, SYSTEMVALUE ay,
                                  SYSTEMVALUE px, SYSTEMVALUE py,
                                  PATHINFO *pathinfo)
{
  SYSTEMVALUE args[6] ;
  SYSTEMVALUE cpx, cpy, cax, cay ;

  if ( type == MOVETO && !blend_moveto(cx + ax, cy + ay, pathinfo) )
    return FALSE ;

  cax = ax * CIRCLE_FACTOR, cay = ay * CIRCLE_FACTOR ;
  cpx = px * CIRCLE_FACTOR, cpy = py * CIRCLE_FACTOR ;

  args[0] = cx + ax + cpx ;
  args[1] = cy + ay + cpy ;
  args[2] = cx + px + cax ;
  args[3] = cy + py + cay ;
  args[4] = cx + px ;
  args[5] = cy + py ;

  if ( !path_curveto(args, TRUE, pathinfo) )
    return FALSE ;

  if ( type == CLOSEPATH && !path_close(CLOSEPATH, pathinfo) )
    return FALSE ;

  return TRUE ;
}

/* Draw arc segment between (axis + 90 degrees + theta) and (axis - 90
   degrees - theta), where -90 < theta < 90 degrees. This is used to draw the
   outside arc segments between the tangent points in the convex hull of two
   non-contained circles, and also to draw the inner arcs in the intersection
   between two circles. The arguments are the type of initial join (lineto or
   moveto), the arc vectors, the arc radius, and the pathinfo into which the
   arc segment will go. The arc is drawn clockwise in device space
   (counter-clockwise in default user space). */
static Bool radial_blend_arc(Bool (*join)(SYSTEMVALUE, SYSTEMVALUE, PATHINFO *),
                             SYSTEMVALUE cx, SYSTEMVALUE cy,
                             SYSTEMVALUE r,
                             blend_arc_t *arc,
                             PATHINFO *pathinfo)
{
  SYSTEMVALUE args[ 6 ] ;
  SYSTEMVALUE tx0, ty0, tx1, ty1 ; /* Tangent points */
  SYSTEMVALUE rax, ray, rpx, rpy ; /* Scaled axis and perpendicular vector */

  HQASSERT(r >= -EPSILON, "Radius too negative in radial_blend_arc") ;

  /* Initial tangent point of arc */
  tx0 = cx + r * arc->ptx ;
  ty0 = cy + r * arc->pty ;

  /* Add lineto or moveto to initial point */
  if ( !(*join)(tx0, ty0, pathinfo) )
    return FALSE ;

  if ( r <= 0.0 ) /* Degenerate circle. Test against exactly zero because in userspace */
    return TRUE ;

  rax = r * arc->uax ;
  ray = r * arc->uay ;
  rpx = r * arc->upx ;
  rpy = r * arc->upy ;

  /* Final tangent point of arc */
  tx1 = cx + r * arc->qtx ;
  ty1 = cy + r * arc->qty ;

  if ( arc->sintheta > EPSILON ) {
    /* Divide the arc of less than 180 degrees into two beziers, from the
       start point to the supplied axis, then from there to the end point. */
    SYSTEMVALUE cf = r * ((4 * sqrt(0.5 - 0.5 * arc->sintheta)) /
                          (3 + 3 * sqrt(0.5 + 0.5 * arc->sintheta))) ;

    /* The calculation above works out the factor to multiply the radial
       vectors (axis and vector to first/last point) by to obtain the Bezier
       control point approximations. This is based on the formula for
       CIRCLE_FACTOR given in gu_cons.c:

       cf = 4 * cos(theta/2) / (3 + 3 * cos(theta/2))

       In this case, cos(theta/2) can be found by bisecting the distance
       between the tangent point and the perpendicular vector. Using
       normalised vectors with sintheta and costheta as the tangent point
       coordinates, this distance is:

       |v| = sqrt(sin(theta)^2 + (1 - cos(theta))^2) / 2
           = sqrt(sin(theta)^2 + 1 - 2 * cos(theta) + cos(theta)^2) / 2
           = sqrt(2 - 2 * cos(theta)) / 2
           = sqrt((1 - cos(theta)) / 2)

       The bisected vector can be used to form a right-angle triangle with the
       perpendicular as the hypotenuese, yeilding a formula for sin(theta/2):

       sin(theta/2) = |v| / 1

       (since the length of the normalised perpendicular is 1). This is turn
       yeilds cos(theta/2), which is what we were after:

       cos(theta/2) = sqrt(1 - sin(theta/2)^2)
                    = sqrt(1 - (1 - cos(theta)) / 2)
                    = sqrt((1 + cos(theta)) / 2)

       The circle factor is the proportion of the vector from the
       perpendicular or the tangent point to the intersection of the tangent
       vectors. To scale it to be proportionate to the radius, we need to
       multiply by the length of the vector to the intersection of the tangent
       vectors (we would also divide by the radius, but this is normalised
       to 1). The length of the vector to the intersection point is the
       length of the hypotenuese of the similar triangle with angle theta/2 and
       adjacent |v|. i.e.,

       |w| = |v| / cos(theta/2)
           = sin(theta/2) / cos(theta/2)

       Thus the normalised circle factor is:

       cf = 4 * cos(theta/2) * sin(theta/2) /
            (3 + 3 * cos(theta/2)) * cos(theta/2))
          = 4 * sin(theta/2) / (3 + 3 * cos(theta/2))
          = 4 * sqrt((1 - cos(theta)) / 2) /
            (3 + 3 * sqrt((1 + cos(theta)) / 2))

       In the two-Bezier case, the first Bezier will go from the tangent
       point to the axis and not the perpendicular, and thus should use
       (90-theta). This is conveniently achieved by substituting sintheta for
       costheta and vice-versa in the calculation above. */

    args[0] = tx0 - arc->tpx * cf ;
    args[1] = ty0 - arc->tpy * cf ;
    args[2] = cx - rax + arc->upx * cf ;
    args[3] = cy - ray + arc->upy * cf ;
    args[4] = cx - rax ;
    args[5] = cy - ray ;

    if ( !path_curveto(args, TRUE, pathinfo) )
      return FALSE ;

    args[0] = cx - rax - arc->upx * cf ;
    args[1] = cy - ray - arc->upy * cf ;
    args[2] = tx1 - arc->tqx * cf ;
    args[3] = ty1 - arc->tqy * cf ;
    args[4] = tx1 ;
    args[5] = ty1 ;

    if ( !path_curveto(args, TRUE, pathinfo) )
      return FALSE ;
  } else { /* Semi-circle or more */
    /* Divide the arc of greater than 180 degrees into four beziers, from the
       start point to the perpendicular to the supplied axis, from there to
       the axis, from there to the other side of the perpendicular, and from
       there to the end point. If it is close enough to a semi-circle, we
       don't bother with the start and end portions. See comments above for
       details of how to calculate Bezier approximation control points. */
    SYSTEMVALUE cf = r * ((4 * sqrt(0.5 - 0.5 * arc->costheta)) /
                          (3 + 3 * sqrt(0.5 + 0.5 * arc->costheta))) ;

    if ( arc->sintheta < -EPSILON ) { /* Initial segment to perpendicular */
      args[0] = tx0 - arc->tpx * cf ;
      args[1] = ty0 - arc->tpy * cf ;
      args[2] = cx + rpx + arc->uax * cf ;
      args[3] = cy + rpy + arc->uay * cf ;
      args[4] = cx + rpx ;
      args[5] = cy + rpy ;

      if ( !path_curveto(args, TRUE, pathinfo) )
        return FALSE ;
    }

    /* Semi-circle in centre portion */
    if ( !radial_blend_quadrant(LINETO, cx, cy, rpx, rpy, -rax, -ray, pathinfo) ||
         !radial_blend_quadrant(LINETO, cx, cy, -rax, -ray, -rpx, -rpy, pathinfo) )
      return FALSE ;

    if ( arc->sintheta < -EPSILON ) { /* Final segment from opposite */
      args[0] = cx - rpx + arc->uax * cf ;
      args[1] = cy - rpy + arc->uay * cf ;
      args[2] = tx1 - arc->tqx * cf ;
      args[3] = ty1 - arc->tqy * cf ;
      args[4] = tx1 ;
      args[5] = ty1 ;

      if ( !path_curveto(args, TRUE, pathinfo) )
        return FALSE ;
    }
  }

  return TRUE ;
}
/** \} */

static Bool blend_lineto(SYSTEMVALUE x, SYSTEMVALUE y, PATHINFO *pathinfo)
{
  return path_segment(x, y, LINETO, TRUE, pathinfo) ;
}

static Bool blend_moveto(SYSTEMVALUE x, SYSTEMVALUE y, PATHINFO *pathinfo)
{
  return path_moveto(x, y, MOVETO, pathinfo) ;
}

static Bool blend_fill( PATHINFO *pathinfo )
{
#if defined( DEBUG_BUILD )
  if ( (shading_debug_flag & SHADING_DEBUG_OUTLINE) != 0 ) {
    STROKE_PARAMS sparams ;

    set_gstate_stroke(&sparams, pathinfo, NULL, FALSE) ;

    if ( !dostroke(&sparams, GSC_SHFILL, STROKE_NOT_VIGNETTE|STROKE_NO_SETG) )
      return FALSE ;
  } else
#endif
    {
      if ( !dofill(pathinfo, EOFILL_TYPE, GSC_SHFILL,
                   FILL_NOT_VIGNETTE|FILL_NO_SETG|FILL_NOT_ERASE) )
        return FALSE;
    }

  return TRUE ;
}

void init_C_globals_blends(void)
{
#if defined(DEBUG_BUILD) || ! defined POOR_SHADING
  BLEND_FILESTATE bfstate_init = { 0 } ;
  FILELIST bflist_init = { 0 } ;
  bfstate = bfstate_init ;
  bflist = bflist_init ;
#endif
}

/* Log stripped */
