/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:rendersh.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rendering functions for smooth shading gouraud objects.
 */

#include "core.h"
#include "rendersh.h"
#include "swtrace.h" /* SW_TRACE_INVALID */
#include "hqbitops.h" /* BIT_SHIFT32_* */
#include "hqbitvector.h"
#include "objnamer.h"

#include "often.h"

#include "display.h"  /* LISTOBJECT */
#include "ndisplay.h" /* NBRESS, NFILLOBJECT */
#include "dl_color.h" /* COLORVALUE */
#include "bitblts.h"  /* SET_BLIT_SLICE */
#include "blitcolorh.h"
#include "blitcolors.h"
#include "surface.h"
#include "blttables.h"
#include "shadex.h"  /* SHADING_DEBUG_FLAG */
#include "dl_bres.h" /* REPAIR_NFILL */
#include "dl_purge.h"
#include "control.h"  /* interrupts_clear */
#include "interrupts.h"
#include "mlock.h"    /* multi_rwlock_t */
#include "color.h" /* ht_getStateColor */
#include "htrender.h" /* LOCK_HALFTONE */
#include "hdl.h" /* hdlRender */
#include "patternrender.h" /* REPLICATING */
#include "scanconv.h" /* scanconvert_band */
#include "pixelLabels.h"
#include "gu_htm.h" /* MODHTONE_REF */
#include "preconvert.h"
#include "group.h"
#include "renderfn.h" /* mht_selected */


static multi_rwlock_t gouraud_lock;
/* The fill lock initialised global does not need initialising in a init
 * function since it is only read in the finish function which can only be called
 * if the start function has been called which does the initialisation.
 */
static Bool gouraud_lock_init = FALSE;

#define GOURAUD_LOCK_WR_CLAIM(item) \
  multi_rwlock_lock(&gouraud_lock, (void *)item, MULTI_RWLOCK_WRITE)
#define GOURAUD_LOCK_RELEASE() multi_rwlock_unlock(&gouraud_lock)


/** Bisection for Hq32x2 fixed-point coordinate values prevents unnecessary
   float to long conversions, that can add up to a substantial amount of
   time. The bisection leaves the fractional part with one bit more precision
   each time. Division truncates towards zero, so if the sum of the integral
   (high) coordinate parts is negative and odd, we need to take into account
   the implicit -0.5 from truncation. If the sum of the integral parts before
   division is odd, we will add 0.5 to the fractional part. Since we are
   increasing the precision of the fractional part by one bit, this is the
   same as adding the denominator to the fractional part. If this addition
   would cause the fractional part to overflow, we will increase the integral
   part instead. */
#define GOURAUD_BISECT(_r, _a, _b, _d) MACRO_START                      \
  Hq32x2 *_a_ = (_a), *_b_ = (_b), *_r_ = (_r) ;                        \
  register int32 _h_ = _a_->high + _b_->high ;                          \
  register uint32 _l_ = _a_->low + _b_->low ;                           \
  HQASSERT((_d) != 0 && ((_d) & ((_d) - 1)) == 0,                       \
           "Fraction denominator is not a positive power of two") ;     \
  HQASSERT(_a_->low < (_d) && _b_->low < (_d),                          \
           "Low fraction larger than denominator") ;                    \
  if ( (_h_ & 1) != 0 ) { /* Add 0.5 to fractional part */              \
    if ( _h_ < 0 )                                                      \
      _h_ -= 1 ; /* Round down to avoid truncate towards zero */        \
    if ( _l_ >= (_d) ) {                                                \
      _l_ -= (_d) ;                                                     \
      _h_ += 2 ; /* Compensate for fraction overflow, division by 2 */  \
    } else                                                              \
      _l_ += (_d) ;                                                     \
  }                                                                     \
  _r_->high = _h_ / 2 ; /* Not >> 1, may be signed */                   \
  _r_->low = _l_ ;                                                      \
  HQASSERT(_l_ < ((_d) << 1), "Fractional part out of range") ;         \
MACRO_END


#if 0
#if defined( ASSERT_BUILD )
/* Please note; expensive tests may not work at high-resolution, due to
   overflow of int32 range in the tests. Care has been taken to ensure that
   this does not happen in the code, by use of maximum range testing for
   multipliers to DDAs. */
#define EXPENSIVE_TESTS
#endif
#endif

/** \page rendersh Storage of gouraud span globals
   Storage for all gouraud span globals in struct. Each channel has a struct
   describing the DDAs used to find the span lengths and colors.
   Usually only channel zero is used; for pixel interleaved,
   multiple DDAs are run in parallel, with the DDAs leapfrogging each other
   as the spans are drawn.

   render_gouraud_blit initialises this structure prior to calling a fill
   function which will in turn call the span functions.  It can be set up in
   three different ways for each channel:

   horiz:       if dz / dx == 0 , there is no change in that channel along
                a scanline. In this case color.colorfrac are the color, and
                dy.dyfrac are the color change per scanline.
   slow:        if | dz / dx | < 1, the color change horizontally is less
                than one halftone level (device code) per pixel. ex.exfrac
                tracks the end position of the span, lx.lxfrac is the length
                of each span, ps.psfrac is the phase shift between lines, and
                dycolor is the color normalisation of the phase shift between
                lines (the phase shift is normalised to be the smallest
                magnitude change to ex.exfrac from one scanline to another).
                For fast X an Y convergence for hi-res band clipping, there are
                pre-multiplied versions of ps.psfrac and lx.lxfrac available.
                If a convergence step exceeds maxmul steps, it is done by
                stepping by a multiple of maxmul first, and then by the
                remainder.
   fast:        if | dz / dx | >= 1, there is a change in device code every
                pixel. color.colorfrac is the color, dy.dyfrac is the change
                in color per scanline, dx.dxfrac is the change in color per
                horizontal pixel.

   Each vertex of the triangle is a point in three-dimensional (x, y, z) space.
   The Z dimension represents the color value of the point. We want to
   describe the plane that they all lie in.  We take the cross product of two
   edges; since this is perpendicular to the plane, all three points should
   have the same dot product with this vector.  We use this for assertions in
   the debug rip:

        x * cross_x + y * cross_y + z * cross_z = plane_const.

   The x and y components of the plane normal define a vector in which the
   color is changing most rapidly. The size of the z component relative to the
   x and y components defines how fast the colour changes in those directions.
   The expressions (adx / adz) and (ady / adz) give the color change
   per pixel in the X and Y directions respectively, and (adz / adx) and
   (adz / ady) give the span length per color change in the X and Y
   directions respectively.
*/

/** New typedef for rational colourvalue DDA representation. Because of the
   possibility for overflow of 32-bit ints, this representation uses a 3-part
   fraction:

       cv = ci + cfh/crossc + cfl/(crossc * maxband)
            where cfh < crossc && cfl < maxband

   The integral part is represented by a signed integers, but the fractional
   parts are unsigned positive integers. Negative values are created by
   making the integral part the floor of the real value, and adding the
   positive fractional parts. */
typedef struct {
  int32 ci ;        /* integral part */
  uint32 cfh, cfl ; /* fractional parts */
} shfill_dda_t ;

#define ASSERT_DDA_CONSISTENT(_dda, _dhi, _dlo) MACRO_START \
  HQASSERT((_dda).cfh < (_dhi), "DDA fraction cfh out of range") ; \
  HQASSERT((_dda).cfl < (_dlo), "DDA fraction cfl out of range") ; \
MACRO_END

/** Re-normalise a shfill dda which is at most one step out. */
#define NORMALISE_DDA_1(_dda, _dhi, _dlo) MACRO_START \
  register int32 _diff_ = (_dda).cfl - (int32)(_dlo) ; \
  if ( _diff_ >= 0 ) { \
    (_dda).cfl = (uint32)_diff_ ; \
    ++(_dda).cfh ; \
  } \
  _diff_ = (_dda).cfh - (_dhi) ; \
  if ( _diff_ >= 0 ) { \
    (_dda).cfh = (uint32)_diff_ ; \
    ++(_dda).ci ; \
  } \
  ASSERT_DDA_CONSISTENT(_dda, _dhi, _dlo) ; \
MACRO_END

/** Get a double value from a DDA. Note that this LOSES PRECISION, and so
   should only be used for approximate results. */
#define DOUBLE_FROM_DDA(_d, _dda, _dhi, _dlo) MACRO_START \
  register double _d_ = (double)(_dda).cfl + (double)(_dda).cfh * (double)(_dlo) ; \
  (_d) = (double)(_dda).ci + _d_ / ((double)(_dhi) * (double)(_dlo)) ; \
MACRO_END

/** Multiply a DDA by 2. */
#define MULTIPLY_DDA_2(_dda, _dhi, _dlo) MACRO_START \
  HQASSERT(((_dda).ci ^ ((_dda).ci << 1)) >= 0, \
           "DDA multiply exceeded representable range") ; \
  (_dda).ci <<= 1 ; \
  (_dda).cfh <<= 1 ; \
  (_dda).cfl <<= 1 ; \
  NORMALISE_DDA_1(_dda, _dhi, _dlo) ; \
  ASSERT_DDA_CONSISTENT(_dda, _dhi, _dlo) ; \
MACRO_END

/** Divide a DDA by 2, truncating. */
#define DIVIDE_DDA_2(_dda, _dhi, _dlo) MACRO_START \
  HQASSERT((_dda.ci) >= 0, "Divide negative DDA not allowed") ; \
  if ( ((_dda).ci & 1) != 0 ) { \
    HQASSERT(MAXUINT32 - (_dhi) >= (_dda).cfh, \
             "Overflow in DDA fraction high divide") ; \
    (_dda).cfh += (_dhi) ; \
  } \
  if ( ((_dda).cfh & 1) != 0 ) { \
    HQASSERT(MAXUINT32 - (_dlo) >= (_dda).cfl, \
             "Overflow in DDA fraction low divide") ; \
    (_dda).cfl += (_dlo) ; \
  } \
  (_dda).ci >>= 1 ; \
  (_dda).cfh >>= 1 ; \
  (_dda).cfl >>= 1 ; \
  ASSERT_DDA_CONSISTENT(_dda, _dhi, _dlo) ; \
MACRO_END

/** Add DDA2 to DDA1. */
#define ADD_DDA_1(_dda1, _dda2, _dhi, _dlo) MACRO_START \
  (_dda1).ci += (_dda2).ci ; \
  (_dda1).cfh += (_dda2).cfh ; \
  (_dda1).cfl += (_dda2).cfl ; \
  NORMALISE_DDA_1(_dda1, _dhi, _dlo) ; \
  ASSERT_DDA_CONSISTENT(_dda1, _dhi, _dlo) ; \
MACRO_END

/** Subtract DDA2 from DDA1. */
#define SUBTRACT_DDA_1(_dda1, _dda2, _dhi, _dlo) MACRO_START \
  register uint32 _carry_ = 0 ; \
  register int32 _diff_ = (_dda1).cfl - (_dda2).cfl ; \
  if ( _diff_ < 0 ) { \
    _diff_ += (int32)(_dlo) ; \
    _carry_ = 1 ; \
  } \
  (_dda1).cfl = _diff_ ; \
  _diff_ = (_dda1).cfh - (_dda2).cfh - _carry_ ; \
  _carry_ = 0 ; \
  if ( _diff_ < 0 ) { \
    _diff_ += (int32)(_dhi) ; \
    _carry_ = 1 ; \
  } \
  (_dda1).cfh = _diff_ ; \
  (_dda1).ci -= (_dda2).ci + _carry_ ; \
  ASSERT_DDA_CONSISTENT(_dda1, _dhi, _dlo) ; \
MACRO_END

/** Add DDA2 scaled by N steps to DDA1, for arbitrary N. Used for fast
   convergence of values in O(log(N)). */
#define ADD_DDA_N(_dda1, _dda2, _n, _dhi, _dlo) MACRO_START \
  register uint32 _n_ = (uint32)(_n) ; \
  shfill_dda_t _dda_add_n_ = (_dda2) ; \
  HQASSERT(MAXINT32 / (abs(_dda_add_n_.ci) + 1) >= (int32)(_n), \
           "Overflow in scaled DDA addition") ; \
  for (;;) { \
    if ( (_n_ & 1) != 0 ) { \
      ADD_DDA_1(_dda1, _dda_add_n_, _dhi, _dlo) ; \
    } \
    _n_ >>= 1 ; \
    if ( _n_ == 0 ) \
      break ; \
    MULTIPLY_DDA_2(_dda_add_n_, _dhi, _dlo) ; \
  } \
  ASSERT_DDA_CONSISTENT(_dda1, _dhi, _dlo) ; \
MACRO_END

/** Return negated DDA value. Floor the negated value if there is a
   fractional part, with appropriate carrys to keep fractional part
   positive. */
#define NEGATE_DDA(_dda, _dhi, _dlo) MACRO_START \
  register uint32 _carry_ = 0 ; \
  if ( (_dda).cfl != 0 ) { \
    (_dda).cfl = (_dlo) - (_dda).cfl ; \
    _carry_ = 1 ; \
  } \
  if ( (_dda).cfh != 0 || _carry_ ) { \
    (_dda).cfh = (_dhi) - (_dda).cfh - _carry_ ; \
    _carry_ = 1 ; \
  } \
  (_dda).ci = -(_dda).ci - _carry_ ; \
MACRO_END

/** Initialise a DDA to a constant integral value. */
#define INITIALISE_DDA_I(_dda, _i) MACRO_START \
  HQASSERT((double)(_i) >= MININT32 && (double)(_i) <= MAXINT32, \
           "DDA integral initialiser out of range") ; \
  (_dda).ci = (int32)(_i) ; \
  (_dda).cfh = 0 ; \
  (_dda).cfl = 0 ; \
MACRO_END

/** Initialise DDA value to value divided by _dlo. This is used in
   quantisation of the initial colour value and the colour step per band. The
   main reason it is a macro is to prevent the muldiv from obscuring the
   caller. If the initialisation is not calculable without overflow directly,
   then the expansion of INITIALISE_DDA_HILO(_dda, _v, _dhi, _dhi, _dlo) is
   used for the fractional parts, multiplying the numerator and denominator
   by _dhi. Since only the fractional part is calculated, some optimisations
   can be applied to ensure that the result never overflows. In particular,
   the case of _dhi < _dlo means that the result will be directly calculable,
   since _dlo will be in the range of colour values. */
#define INITIALISE_DDA_DLO(_dda, _v, _dhi, _dlo) MACRO_START \
  register uint32 _cbf_ ; \
  /* Always true as _v is unsigned: HQASSERT((_v) >= 0, "DDA initialisation value negative") ; */ \
  (_dda).ci = (_v) / (_dlo) ; \
  _cbf_ = (uint32)(_v) - (uint32)(_dda).ci * (_dlo) ; \
  if ( MAXUINT32 / (_dhi) >= _cbf_ ) { /* Calculable without overflow */ \
    _cbf_ *= (_dhi) ; \
    (_dda).cfh = _cbf_ / (_dlo) ; \
    (_dda).cfl = _cbf_ - (_dda).cfh * (_dlo) ; \
  } else { /* trunc(_nhi/_dhi) expansion terms zero */ \
    shfill_dda_t _dda_dlo_ ; \
    register uint32 _hilo_ ; \
    HQASSERT((_dhi) >= (_dlo), "Denominators indicate overflow possible") ; \
    HQASSERT(_cbf_ < (_dhi), "DDA high fraction out of range") ; \
    (_dda).cfh = (_dda).cfl = 0 ; \
    _dda_dlo_.ci = 0 ; \
    _dda_dlo_.cfh = _cbf_ ; \
    _dda_dlo_.cfl = 0 ; \
    _hilo_ = (_dhi) / (_dlo) ; \
    ADD_DDA_N(_dda, _dda_dlo_, _hilo_, _dhi, _dlo) ; \
    _dda_dlo_.cfh = 0 ; \
    _dda_dlo_.cfl = (_dhi) - _hilo_ * (_dlo) ; \
    ADD_DDA_N(_dda, _dda_dlo_, _cbf_, _dhi, _dlo) ; \
  } \
  ASSERT_DDA_CONSISTENT(_dda, _dhi, _dlo) ; \
MACRO_END

/** Initialise DDA value to (_nhi * _nlo) / (_dhi * _dlo) without overflowing
   integers. This is used in initialisation of the step size per colour band
   and other DDAs. The method used is derived from representing _nhi and _nlo
   as integral and fractional parts:

   (_nhi / _dhi) * (_nlo / _dlo)
   = (trunc(_nhi/_dhi)+(_nhi%_dhi)/_dhi) * (trunc(_nlo/_dlo)+(_nlo%_dlo)/_dlo)
   = trunc(_nhi/_dhi)*trunc(_nlo/_dlo) + trunc(_nhi/_dhi)*(_nlo%_dlo)/_dlo +
     trunc(_nlo/_dlo)*(_nhi%_dhi)/_dhi + ((_nlo%_dlo)/_dlo)*((_nhi%_dhi)/_dhi)

   The fractional expressions are directly translated to DDAs, and multiples
   are added to the initial DDA value.
*/
#define INITIALISE_DDA_HILO(_dda, _nhi, _nlo, _dhi, _dlo) MACRO_START \
  register uint32 _hid_, _hir_, _lod_, _lor_ ; \
  shfill_dda_t _dda_hilo_ ; \
  /* Always true as _nhi, _nlo are unsigned: HQASSERT((_nhi) >= 0 && (_nlo) >= 0, "DDA initialisation value negative") ; */ \
  HQASSERT((_dhi) > 0 && (_dlo) > 0, "DDA denominators invalid") ; \
  _hid_ = (uint32)(_nhi) / (uint32)(_dhi) ; \
  _hir_ = (uint32)(_nhi) - _hid_ * (uint32)(_dhi) ; \
  HQASSERT(_hir_ < (_dhi), "High-part remainder out of range") ; \
  _lod_ = (uint32)(_nlo) / (uint32)(_dlo) ; \
  _lor_ = (uint32)(_nlo) - _lod_ * (uint32)(_dlo) ; \
  HQASSERT(_lor_ < (_dlo), "Low-part remainder out of range") ; \
  /* trunc(_nhi/_dhi)*trunc(_nlo/_dlo) */ \
  INITIALISE_DDA_I(_dda, _hid_ * _lod_) ; \
  _dda_hilo_.ci = 0 ; \
  /* trunc(_nlo/_dlo)*(_nhi%_dhi)/_dhi */ \
  if ( _lod_ > 0 ) { \
    _dda_hilo_.cfh = _hir_ ; \
    _dda_hilo_.cfl = 0 ; \
    ADD_DDA_N(_dda, _dda_hilo_, _lod_, _dhi, _dlo) ; \
  } \
  if ( _lor_ > 0 ) { \
    /* ((_nlo%_dlo)/_dlo)*((_nhi%_dhi)/_dhi) */ \
    if ( _hir_ > 0 ) { \
      _dda_hilo_.cfh = 0 ; \
      _dda_hilo_.cfl = _lor_ ; \
      ADD_DDA_N(_dda, _dda_hilo_, _hir_, _dhi, _dlo) ; \
    } \
    /* trunc(_nhi/_dhi)*(_nlo%_dlo)/_dlo */ \
    if ( _hid_ > 0 ) { \
      INITIALISE_DDA_DLO(_dda_hilo_, _lor_, _dhi, _dlo) ; \
      ADD_DDA_N(_dda, _dda_hilo_, _hid_, _dhi, _dlo) ; \
    } \
  } \
  ASSERT_DDA_CONSISTENT(_dda, _dhi, _dlo) ; \
MACRO_END

/** This macro is used to verify the result of the integral DDA initialisation
   above; since IEEE doubles have a 48 bit mantissa, and the values of _nhi and
   _nlo (or _dhi and _dlo) fit in 32 and 16 bits respectively, we should be
   able to do the multiplications without overflow. */
#define INITIALISE_DDA_HILO_DOUBLE(_dda, _nhi, _nlo, _dhi, _dlo) MACRO_START \
  register double _n_ = (double)(_nhi) * (double)(_nlo) ; \
  register double _d_ = (double)(_dhi) * (double)(_dlo) ; \
  HQASSERT(_n_ / _d_ <= MAXINT32, "DDA double initialisation out of range") ; \
  (_dda).ci = (int32)(_n_ / _d_) ; \
  _n_ -= (_dda).ci * _d_ ; \
  HQASSERT(_n_ / (double)(_dlo) < (double)(_dhi), \
           "DDA double initialisation high fraction out of range") ; \
  (_dda).cfh = (uint32)(_n_ / (double)(_dlo)) ; \
  HQASSERT(_n_ - (_dda).cfh * (double)(_dlo) < (double)(_dlo), \
           "DDA double initialisation low fraction out of range") ; \
  _n_ -= (_dda).cfh * (double)(_dlo) ; \
  HQASSERT(_n_ == (uint32)_n_, \
           "DDA double initialisation low fraction not integral") ; \
  (_dda).cfl = (uint32)_n_ ; \
MACRO_END

/** Convert a DDA based on one denominator to another. The DDA is assumed to
   be on basis (dhi1,dlo), and is converted to (dhi2,dlo). This is done by
   multiplying the DDA by dhi1/dhi2:

   x = ci * dhi1 / dhi2 + cfh / dhi2 + cfl / (dhi2 * dlo)
     = ci * (trunc(dhi1/dhi2) + (dhi1%dhi2)/dhi2) +
             trunc(cfh/dhi2) + (cfh%dhi2)/dhi2 + cfl/(dhi2 * dlo)

   which can be converted to a DDA on (dhi2,dlo) basis by distributing the
   terms into separate DDAs and adding them. Note that the cfl term can be
   copied directly, since cfl < dlo it is already valid. We put the cfh and
   cfl terms into the result DDA directly, and then add the ci term DDA. */
#define CHANGE_DDA_BASIS(_dda, _dhi1, _dhi2, _dlo) MACRO_START \
  int32 _ci_basis_ = (_dda).ci ; \
  shfill_dda_t _dda_basis_ ; \
  _dda_basis_.ci = (int32)((_dhi1) / (_dhi2)) ; \
  _dda_basis_.cfh = (_dhi1) - _dda_basis_.ci * (_dhi2) ; \
  _dda_basis_.cfl = 0 ; \
  (_dda).ci = (int32)((_dda).cfh / (_dhi2)) ; \
  (_dda).cfh = (_dda).cfh - (_dda).ci * (_dhi2) ; \
  ADD_DDA_N(_dda, _dda_basis_, _ci_basis_, _dhi2, _dlo) ; \
MACRO_END


/** Typedef for per-channel information. */
typedef struct {
#if defined( EXPENSIVE_TESTS )
  int32 cross_x, cross_y, cross_z, plane_const ;
#endif
  channel_index_t bci ;  /**< Blit color index for this channel */
  uint16 maxband ;       /**< Maximum band index required for smoothness. */
  uint32 adx ;           /**< Denominator for position DDAs. */

  /* All colour DDAs held in same denominator basis (adz,maxband) */
  shfill_dda_t cband ;   /**< Colour step per band, 0 < cband <= COLORVALUE_MAX. */
  shfill_dda_t cquant ;  /**< Colour quantised to multiple of cband. */
  shfill_dda_t cerror ;  /**< Error from exact colour, 0 <= cerror < cband. */
  shfill_dda_t cqx ;     /**< Quantised colour per x step, cqx = n * cband. */
  shfill_dda_t cex ;     /**< Error from exact colour per x step, 0 >= cex > -cband. */
  shfill_dda_t cqy ;     /**< Quantised colour per y, cqy = n * cband. */
  shfill_dda_t cey ;     /**< Error from exact colour per y, 0 >= cey > -cband. */

  /* All position DDAs held in same denominator basis (adx,maxband) */
  shfill_dda_t xps ;     /**< X per step (>= 1). */
  shfill_dda_t nxs ;     /**< Next X step (X phase), 1 <= nxs < xps + 1. */
  shfill_dda_t xpy ;     /**< X phase shift per Y, 0 <= xpy < xps. */

  /* Noise addition factors */
  int32 noiseadded ;     /**< Amount of noise in cquant as a multiple of cband. */
#ifdef ASSERT_BUILD
  COLORVALUE htmax ;     /**< Maximum halftone level. */
#endif
} GOURAUD_DDA;

#define GOURAUD_CONTEXT_NAME "Gouraud context"

/** Structure holding information needed by span routines. */
typedef struct GOURAUD_CONTEXT {
  GOURAUD_DDA *channel;  /**< ptr to channel array. */
  void *colorWorkspace;
  channel_index_t nchannels;
  int32 last_x, last_y ;
  uint32 adz ;           /**< Denominator for colour DDAs. */
  USERVALUE noise ;      /**< Factor of cband FM noise to add. */
  uint16 noiseshift ;    /**< Grid size for noise addition. */
  uint16 noisemask ;     /**< Grid mask for noise addition. */
  uint16 mbands ;        /**< Max bands in this Gouraud fill. */
  uint8 object_label ;   /**< Object type label. */
  LateColorAttrib *lca;  /**< Fill late-color attributes. */
  Bool knockout ;        /**< Does this fill knock out? */
  Bool selected ;        /**< Modular halftone selection. */
  Bool varying_alpha;    /**< Alpha varies across the fill. */
  blit_color_t colors[3] ; /**< Three blit colors for corners. */

  OBJECT_NAME_MEMBER
} GOURAUD_CONTEXT ;


/** Structure for holding invariant Gouraud base fill callback info, and
   color source pointers. Do not put values in here which must be preserved
   across recursive calls to render_gouraud_*. */
typedef struct {
  render_info_t *p_ri ;
  p_ncolor_t *nextcolor ; /* Next colorvalue in decomposition */
  Bool screened ;
  uintptr_t flags ;     /* Current flags */
  int32 bits ;          /* Number of bits left in current flags */
  int32 linear_triangles ; /* Number of triangles rendered */
  LISTOBJECT tlobj ; /* Listobject and NFILL for gouraud triangle */
  NFILLOBJECT tnfill ;
  NBRESS tnbress[6]; /* only 3 used normally, 3 extra for debug stroking */
} GOURAUD_FILL ;


size_t gouraud_dda_size(uint32 nchannels)
{
  return sizeof(GOURAUD_DDA) * nchannels;
}


#if defined(ASSERT_BUILD)
static void dda_assertions(GOURAUD_CONTEXT *gspanc,
                           register GOURAUD_DDA *channel)
{
  uint32 maxband, adz, adx ;
  int32 nq ;
  double dcb, dcq ;
  shfill_dda_t cquant, cband, cex ;

  VERIFY_OBJECT(gspanc, GOURAUD_CONTEXT_NAME) ;

  adz = gspanc->adz ;
  HQASSERT(adz > 0 && MAXUINT32 - adz >= adz,
           "DDA colour denominator out of valid range") ;
  HQASSERT(channel, "No channel for DDA assertions") ;

  adx = channel->adx ;
  HQASSERT(MAXUINT32 - adx >= adx,
           "DDA position denominator out of valid range") ;

  maxband = channel->maxband ;
  HQASSERT(maxband > 0 && maxband <= COLORVALUE_MAX,
           "Number of bands out of valid range") ;
  HQASSERT(maxband <= (uint32)channel->htmax,
           "More bands than halftone levels") ;

  /* Assert that cband is positive and in range */
  HQASSERT(channel->cband.ci > 0 ||
           (channel->cband.ci == 0 &&
            (channel->cband.cfh > 0 || channel->cband.cfl > 0)),
           "Colour step per band not positive") ;

  HQASSERT(channel->cband.ci < COLORVALUE_MAX ||
           (channel->cband.ci == COLORVALUE_MAX &&
            channel->cband.cfh == 0 &&
            channel->cband.cfl == 0),
           "Colour step per band larger than colour range") ;

  DOUBLE_FROM_DDA(dcb, channel->cband, adz, maxband) ;
  HQASSERT(dcb > 0.0, "Colour per band not positive as double") ;

  /* Assert that cquant is exact multiple of cband */
  DOUBLE_FROM_DDA(dcq, channel->cquant, adz, maxband) ;
  nq = (int32)(dcq / dcb + (dcq < 0 ? -0.5 : 0.5)) ;
  cband = channel->cband ;
  INITIALISE_DDA_I(cquant, 0) ;

  if ( nq < 0 ) {
    NEGATE_DDA(cband, adz, maxband) ;
    nq = -nq ;
  }

  ADD_DDA_N(cquant, cband, nq, adz, maxband) ;
  HQASSERT(cquant.ci == channel->cquant.ci &&
           cquant.cfh == channel->cquant.cfh &&
           cquant.cfl == channel->cquant.cfl,
           "Quantised colour is not exact multiple of band step") ;

  /* Assert that cerror is in range 0 <= cerror < cband */
  HQASSERT(channel->cerror.ci >= 0,
           "Colour error negative") ;
  HQASSERT(channel->cerror.ci < channel->cband.ci ||
           (channel->cerror.ci == channel->cband.ci &&
            (channel->cerror.cfh < channel->cband.cfh ||
             (channel->cerror.cfh == channel->cband.cfh &&
              channel->cerror.cfl < channel->cband.cfl))),
           "Colour error not less than colour step") ;

  /* Assert that cqx is an exact multiple of cband */
  DOUBLE_FROM_DDA(dcq, channel->cqx, adz, maxband) ;
  nq = (int32)(dcq / dcb + (dcq < 0 ? -0.5 : 0.5)) ;
  cband = channel->cband ;
  INITIALISE_DDA_I(cquant, 0) ;

  if ( nq < 0 ) {
    NEGATE_DDA(cband, adz, maxband) ;
    nq = -nq ;
  }

  ADD_DDA_N(cquant, cband, nq, adz, maxband) ;
  HQASSERT(cquant.ci == channel->cqx.ci &&
           cquant.cfh == channel->cqx.cfh &&
           cquant.cfl == channel->cqx.cfl,
           "Quantised colour is not exact multiple of band step") ;

  /* Assert that cex is in range 0 >= cex > -cband */
  HQASSERT(channel->cex.ci < 0 ||
           (channel->cex.ci == 0 &&
            channel->cex.cfh == 0 &&
            channel->cex.cfl == 0),
           "Colour error per X positive") ;
  cband = channel->cband ;
  NEGATE_DDA(cband, adz, maxband) ;
  HQASSERT(channel->cex.ci > cband.ci ||
           (channel->cex.ci == cband.ci &&
            (channel->cex.cfh > cband.cfh ||
             (channel->cex.cfh == cband.cfh &&
              channel->cex.cfl > cband.cfl))),
           "Colour error per X not greater than negative colour step") ;

  /* Assert that cqy is an exact multiple of cband */
  DOUBLE_FROM_DDA(dcq, channel->cqy, adz, maxband) ;
  nq = (int32)(dcq / dcb + (dcq < 0 ? -0.5 : 0.5)) ;
  cband = channel->cband ;
  INITIALISE_DDA_I(cquant, 0) ;

  if ( nq < 0 ) {
    NEGATE_DDA(cband, adz, maxband) ;
    nq = -nq ;
  }

  ADD_DDA_N(cquant, cband, nq, adz, maxband) ;
  HQASSERT(cquant.ci == channel->cqy.ci &&
           cquant.cfh == channel->cqy.cfh &&
           cquant.cfl == channel->cqy.cfl,
           "Quantised colour is not exact multiple of band step") ;

  /* Assert that cey is in range 0 >= cey > -cband */
  HQASSERT(channel->cey.ci < 0 ||
           (channel->cey.ci == 0 &&
            channel->cey.cfh == 0 &&
            channel->cey.cfl == 0),
           "Colour error per Y positive") ;
  cband = channel->cband ;
  NEGATE_DDA(cband, adz, maxband) ;
  HQASSERT(channel->cey.ci > cband.ci ||
           (channel->cey.ci == cband.ci &&
            (channel->cey.cfh > cband.cfh ||
             (channel->cey.cfh == cband.cfh &&
              channel->cey.cfl > cband.cfl))),
           "Colour error per Y not greater than negative colour step") ;

  /* Assert that xps is greater than or equal to one */
  HQASSERT(channel->xps.ci >= 1,
           "Position step size less than one") ;

  if ( gspanc->noise == 0.0f || adx != 0 ) {
    /* Assert that nxs is in range 1 <= nxs < xps + 1 */
    HQASSERT(channel->nxs.ci >= 1,
             "Phase error not greater than one") ;
    HQASSERT(channel->nxs.ci < channel->xps.ci + 1 ||
             (channel->nxs.ci == channel->xps.ci + 1 &&
              (channel->nxs.cfh < channel->xps.cfh ||
               (channel->nxs.cfh == channel->xps.cfh &&
                channel->nxs.cfl < channel->xps.cfl))),
             "Phase error greater than step size plus one") ;
  }

  /* Assert that xpy is in range 0 <= xpy < xps */
  HQASSERT(channel->xpy.ci >= 0,
           "Phase shift per Y negative") ;
  HQASSERT(channel->xpy.ci < channel->xps.ci ||
           (channel->xpy.ci == channel->xps.ci &&
            (channel->xpy.cfh < channel->xps.cfh ||
             (channel->xpy.cfh == channel->xps.cfh &&
              channel->xpy.cfl < channel->xps.cfl))),
           "Phase shift per Y not less than step size") ;

  /* Only one of the error control methods should be used at once */
  cex = channel->cex;
  NEGATE_DDA(cex, adz, maxband) ;
  HQASSERT((channel->xpy.ci == 0 &&
            channel->xpy.cfh == 0 &&
            channel->xpy.cfl == 0) ||
           (channel->cey.ci == 0 &&
            channel->cey.cfh == 0 &&
            channel->cey.cfl == 0) ||
           (channel->cey.cfh == 0 &&
            channel->cey.cfl == 0 &&
            channel->cqx.cfh == 0 &&
            channel->cqx.cfl == 0 &&
            channel->cex.cfh == 0 &&
            channel->cex.cfl == 0 &&
            abs(channel->cqx.ci + channel->cex.ci) == 1) ||
           (abs(channel->cqx.ci - cex.ci) == 1 &&
            channel->cqx.cfh == cex.cfh &&
            channel->cqx.cfl == cex.cfl),
           "Only one of cey, xpy should be non-zero, or should be stepping one fractional value") ;
}
#else /* ! ASSERT_BUILD */
#define dda_assertions(gspanc, channel) EMPTY_STATEMENT()
#endif /* ! ASSERT_BUILD */

/** Re-normalise X phase of DDA by adding or subtracting enough X steps
   to return the range to bias <= x < xps + bias, and return the number and
   direction of steps. Bias is either 0 or 1, depending on whether the
   phase shift or phase error are being normalised. */
static int32 renormalise_gspan_x(register GOURAUD_DDA *channel,
                                 shfill_dda_t *xdda, int32 bias)
{
  uint32 maxband = channel->maxband, adx = channel->adx ;
  int32 xsteps = 0 ;
  shfill_dda_t nxs ;

  HQASSERT(adx > 0, "Can't renormalise phase for horizontal") ;

  nxs.ci = xdda->ci - bias ;
  nxs.cfh = xdda->cfh ;
  nxs.cfl = xdda->cfl ;

  /* For small normalisations, we don't want to go to the expense of the
     division, so detect these cases and add or subtract one step. The
     fractional components can be ignored in the quick test because
     xps >= 1. */
  if ( nxs.ci >= channel->xps.ci + channel->xps.ci ||
       nxs.ci < -channel->xps.ci ) {
    /* More than one convergence step may be required */
    register double dnxs, dxps ;
    shfill_dda_t xps = channel->xps ;
    int32 n ;

    DOUBLE_FROM_DDA(dnxs, nxs, adx, maxband) ;
    DOUBLE_FROM_DDA(dxps, xps, adx, maxband) ;

    xsteps = (int32)(dnxs / dxps) ;

    if ( xsteps < 0 ) {
      n = -xsteps ;
    } else {
      n = xsteps ;
      NEGATE_DDA(xps, adx, maxband) ;
    }

    ADD_DDA_N(nxs, xps, n, adx, maxband) ;
  }

  /* Perform single final convergence step if required */
  if ( nxs.ci < 0 ) {
    --xsteps ;
    ADD_DDA_1(nxs, channel->xps, adx, maxband) ;
  } else if ( nxs.ci > channel->xps.ci ||
              (nxs.ci == channel->xps.ci &&
               (nxs.cfh > channel->xps.cfh ||
                (nxs.cfh == channel->xps.cfh &&
                 nxs.cfl >= channel->xps.cfl))) ) {
    ++xsteps ;
    SUBTRACT_DDA_1(nxs, channel->xps, adx, maxband) ;
  }

  xdda->ci = nxs.ci + bias ;
  xdda->cfh = nxs.cfh ;
  xdda->cfl = nxs.cfl ;

  return xsteps ;
}

/** Update a channel colour (cquant+cerror) by a number of steps. Note the
   struct passing in arguments for cqs and ces; this is because we modify
   these values. */
static void update_channel_nsteps(register GOURAUD_DDA *channel,
                                  shfill_dda_t cqs, /* Quantum per step */
                                  shfill_dda_t ces, /* Error per step */
                                  int32 nsteps,
                                  uint32 adz)
{
  uint32 maxband = channel->maxband ;

  HQASSERT(ces.ci < 0 ||
           (ces.ci == 0 && ces.cfh == 0 && ces.cfl == 0),
           "Colour error per step should be negative or zero") ;

  /* If there is no colour change in this direction, don't waste time
     looping. We do not need to test the fractional parts of ces, because
     they will be zero if the integral part is zero. */
  if ( (ces.ci|cqs.ci|cqs.cfh|cqs.cfl) == 0 )
    return ;

  if ( nsteps < 0 ) {
    NEGATE_DDA(cqs, adz, maxband) ;
    if ( ces.ci < 0 ) { /* Maintain invariant ces <= 0 */
      ADD_DDA_1(cqs, channel->cband, adz, maxband) ;
      ADD_DDA_1(ces, channel->cband, adz, maxband) ;
      NEGATE_DDA(ces, adz, maxband) ;
    }
    nsteps = -nsteps ;
  }

  /* This is the logic of ADD_DDA_N, but overflowing the error and error per
     step into the quantum or quantum per step respectively as we go along.
     This prevents overflow in the intermediate results of scanline
     convergence. It does not prevent the DDA from being stepped out of its
     representable range. */
  for (;;) {
    shfill_dda_t nces ;

    if ( (nsteps & 1) != 0 ) {
      ADD_DDA_1(channel->cquant, cqs, adz, maxband) ;
      ADD_DDA_1(channel->cerror, ces, adz, maxband) ;

      /* If the colour error overflowed, adjust the colour quantum. */
      if ( channel->cerror.ci < 0 ) {
        ADD_DDA_1(channel->cerror, channel->cband, adz, maxband) ;
        SUBTRACT_DDA_1(channel->cquant, channel->cband, adz, maxband) ;
      }
    }

    nsteps >>= 1 ;
    if ( nsteps == 0 )
      break ;

    MULTIPLY_DDA_2(cqs, adz, maxband) ;
    MULTIPLY_DDA_2(ces, adz, maxband) ;

    /* If the colour error per step overflowed, adjust the colour quantum per
       step. The test is performed by adding cband to ces and determining if
       the result is negative; the partial result can be tested before the
       addition is complete to avoid the adjustment in most cases. */
    nces.ci = ces.ci + channel->cband.ci ;
    if ( nces.ci <= -1 ) {
      /* If the integral part is -1 exactly, we need to test after
         normalisation. If it is less than -1, the result will be less than
         zero after normalisation, but it's not worth adding a special case
         for that because we have to normalise anyway. */
      nces.cfh = ces.cfh + channel->cband.cfh ;
      nces.cfl = ces.cfl + channel->cband.cfl ;
      NORMALISE_DDA_1(nces, adz, maxband) ;
      if ( nces.ci < 0 ) {
        ces = nces ;
        SUBTRACT_DDA_1(cqs, channel->cband, adz, maxband) ;
      }
    }
  }
}

static void update_gspan_scanline(register GOURAUD_DDA *channel,
                                  int32 ydiff, int32 xdiff, uint32 adz)
{
  uint32 adx = channel->adx ;

  HQASSERT(ydiff != 0 || xdiff != 0,
           "DDA scanline update called, but no position difference") ;

  /* It is possible that the DDA convergence step will overflow the
     representable range of the quantised colour DDA. This can occur if the
     convergence step goes outside of the triangle by more than about 2^15
     times the maximum colour change length along the colour change vector.
     This potential problem has not yet been dealt with. This is pretty
     unlikely, but might be provoked by a triangle such as (66000,-66000,0),
     (0,0,0), (1,0,65280). */
  if ( ydiff != 0 ) {
    /* Note that the Y convergence can be negative. This is permitted because
       multi-processor RIPs can conceivably render the bands out of order,
       sharing the workspace between processors. If this happens, we can
       avoid re-calculating the cross products and DDA components by
       re-converging the DDAs vertically. */
    update_channel_nsteps(channel, channel->cqy, channel->cey, ydiff, adz) ;

    /* Calculate position phase change for Y difference. */
    if ( adx != 0 ) {
      uint32 maxband = channel->maxband ;
      shfill_dda_t xpy = channel->xpy ;

      if ( ydiff < 0 ) {
        NEGATE_DDA(xpy, adx, maxband) ;
        ydiff = -ydiff ;
      }

      ADD_DDA_N(channel->nxs, xpy, ydiff, adx, maxband) ;
    }
  }

  /* X convergence only matters if the bands are not horizontal. The
     renormalisation is always done in this case, because the Y convergence
     may have altered nxs. The next span length is always updated, even if
     horizontal. In the horizontal case, the span length is initialised such
     that a single span covers the whole of the triangle's coordinate range. */
  channel->nxs.ci -= xdiff ;
  if ( adx != 0 ) {
    int32 xsteps = renormalise_gspan_x(channel, &channel->nxs, 1) ;

    /* We use the negated value from the renormalisation because it the
       number of colour band changes required is the opposite way from the
       number of steps added. */
    if ( xsteps != 0 )
      update_channel_nsteps(channel, channel->cqx, channel->cex, -xsteps, adz) ;
  }
}

void gspan_1(render_blit_t *rb,
             register dcoord y, register dcoord xs, register dcoord xe)
{
  register GOURAUD_CONTEXT *gspanc ;
  register GOURAUD_DDA *channel ;
  int32 xdiff, ydiff ;
  uint32 maxband, adz ;
  shfill_dda_t cquant, cerror, nxs ;

  VERIFY_OBJECT(rb->color, BLIT_COLOR_NAME) ;
  HQASSERT(rb->color->valid & blit_color_unpacked, "Output color has never been unpacked") ;

  GET_BLIT_DATA(rb->blits, GOURAUD_BLIT_INDEX, gspanc) ;
  VERIFY_OBJECT(gspanc, GOURAUD_CONTEXT_NAME) ;

  channel = &gspanc->channel[0] ;
  xdiff = xs - gspanc->last_x ;
  ydiff = y - gspanc->last_y ;
  maxband = channel->maxband ;
  adz = gspanc->adz ;

  HQASSERT(channel->bci < BLIT_MAX_CHANNELS, "Invalid channel index");
  HQASSERT((rb->color->state[channel->bci] & (blit_channel_present|blit_channel_override)) == blit_channel_present,
           "Sole channel is overprinted, transparent, missing or overridden") ;

  if ( ydiff != 0 || xdiff != 0 ) {
    update_gspan_scanline(channel, ydiff, xdiff, adz) ;
    dda_assertions(gspanc, channel) ;

    /* Leave X and Y converged at start of span; next line will probably start
       close to this point, reducing convergence computation. */
    gspanc->last_y = y ;
    gspanc->last_x = xs ;
  }

  cquant = channel->cquant ;
  cerror = channel->cerror ;
  nxs = channel->nxs ;

  for ( ;; ) {
    register dcoord ex = xs + channel->nxs.ci - 1 ;

    HQASSERT(xs <= ex, "ex is now too small") ;
    if ( ex > xe )
      ex = xe ;

    /* This check is too expensive and should't appear
     * in normal debug builds.
     */
#ifdef EXPENSIVE_TESTS
    {
      int32 vc = channel->cross_z ; /* +ve */
      int32 vcv = channel->plane_const -  channel->cross_x * xs - channel->cross_y * y ;

      HQASSERT( (vcv + 100 * vc) / vc == color[i] + 100, "Start pixel is wrong color");
      vcv = channel->plane_const -  channel->cross_x * min_ex - channel->cross_y * y ;
      HQASSERT( (vcv + 100 * vc) / vc == color[i] + 100, "End pixel is wrong color");
    }
#endif

    DO_SPAN(rb, y, xs, ex) ;
    if (ex == xe) {
      channel->cquant = cquant ;
      channel->cerror = cerror ;
      channel->nxs = nxs ;
      return;
    }
    xs = ex + 1 ;

    /* Step for new colour */
    ADD_DDA_1(channel->cquant, channel->cqx, adz, maxband) ;
    ADD_DDA_1(channel->cerror, channel->cex, adz, maxband) ;

    /* If colour error exceeds a band, shift quantised colour by one */
    if ( channel->cerror.ci < 0 ) {
      ADD_DDA_1(channel->cerror, channel->cband, adz, maxband) ;
      SUBTRACT_DDA_1(channel->cquant, channel->cband, adz, maxband) ;
    }
    HQASSERT(channel->cerror.ci >= 0,
             "Colour error exceeds one band after single normalisation") ;

    /* Get next span length */
    channel->nxs.ci = 0 ;
    ADD_DDA_1(channel->nxs, channel->xps, channel->adx, maxband) ;
    HQASSERT(channel->nxs.ci > 0, "Next X value degenerate") ;
  }
}

void gspan_n(render_blit_t *rb,
             register dcoord y, register dcoord xs, register dcoord xe)
{
  register int i;
  GOURAUD_CONTEXT *gspanc ;
  dcoord min_nx = xe - xs + 1 ;
  int32 xdiff = xs, ydiff ;
  uint32 adz ;
  blit_color_t *color = rb->color ;
  const blit_colormap_t *map ;
  struct {
    shfill_dda_t cquant, cerror, nxs ;
  } savedda[BLIT_MAX_CHANNELS] ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_unpacked, "Output color has never been unpacked") ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  GET_BLIT_DATA(rb->blits, GOURAUD_BLIT_INDEX, gspanc) ;
  VERIFY_OBJECT(gspanc, GOURAUD_CONTEXT_NAME) ;

  xdiff = xs - gspanc->last_x ;
  ydiff = y - gspanc->last_y ;
  adz = gspanc->adz ;

  for ( i = gspanc->nchannels ; --i >= 0 ; ) {
    register GOURAUD_DDA *channel = &gspanc->channel[i] ;

    HQASSERT(channel->bci < BLIT_MAX_CHANNELS, "Invalid channel index");
    HQASSERT((color->state[channel->bci] & (blit_channel_present|blit_channel_override)) == blit_channel_present,
             "Colorant for channel should be present and not overridden") ;

    if ( ydiff != 0 || xdiff != 0 ) {
      update_gspan_scanline(channel, ydiff, xdiff, adz) ;
      dda_assertions(gspanc, channel) ;
    }

    HQASSERT(channel->nxs.ci > 0, "Next X span is degenerate after update") ;
    if ( channel->nxs.ci < min_nx )
      min_nx = channel->nxs.ci ;

    savedda[i].cquant = channel->cquant ;
    savedda[i].cerror = channel->cerror ;
    savedda[i].nxs = channel->nxs ;
  }

  /* Leave colours converged on start of span; next line will probably start
     close to this point, reducing convergence computation. */
  gspanc->last_y = y ;
  gspanc->last_x = xs ;

  for ( ;; ) {
    register dcoord min_ex = xs + min_nx - 1 ; /* This is now the smallest end position of any channel */

    HQASSERT(xs <= min_ex, "ex is now too small") ;
    HQASSERT(min_ex <= xe, "ex is now too large") ;

    /* This check is too expensive and should't appear
     * in normal debug builds.
     */
#ifdef EXPENSIVE_TESTS
    for ( i = gspanc->nchannels ; --i >= 0 ; ) {
      int32 vc = channel->cross_z ; /* +ve */
      int32 vcv = channel->plane_const -  channel->cross_x * xs - channel->cross_y * y ;

      HQASSERT( (vcv + 100 * vc) / vc == color[i] + 100, "Start pixel is wrong color");
      vcv = channel->plane_const -  channel->cross_x * min_ex - channel->cross_y * y ;
      HQASSERT( (vcv + 100 * vc) / vc == color[i] + 100, "End pixel is wrong color");
    }
#endif

    DO_SPAN(rb, y, xs, min_ex) ;
    if (min_ex == xe) {
      for ( i = gspanc->nchannels ; --i >= 0 ; ) {
        /* Restore values of colour and colour/phase error at convergence */
        register GOURAUD_DDA *channel = &gspanc->channel[i] ;

        channel->cquant = savedda[i].cquant ;
        channel->cerror = savedda[i].cerror ;
        channel->nxs = savedda[i].nxs ;
      }
      return;
    }
    xs = min_ex + 1 ;
    /* Temporarily use min_ex for next min_nx. min_nx remains at old span
       length throughout loop. */
    min_ex = xe - xs + 1 ;

    for ( i = gspanc->nchannels ; --i >= 0 ; ) {
      register GOURAUD_DDA *channel = &gspanc->channel[i] ;
      uint32 maxband = channel->maxband ;

      /* Get next span length */
      channel->nxs.ci -= min_nx ;
      if ( channel->nxs.ci <= 0 ) {
        ADD_DDA_1(channel->nxs, channel->xps, channel->adx, maxband) ;
        HQASSERT(channel->nxs.ci > 0, "Next X value degenerate after update") ;

        /* Step for new colour */
        ADD_DDA_1(channel->cquant, channel->cqx, adz, maxband) ;
        ADD_DDA_1(channel->cerror, channel->cex, adz, maxband) ;

        /* If colour error exceeds a band, shift quantised colour by one */
        if ( channel->cerror.ci < 0 ) {
          ADD_DDA_1(channel->cerror, channel->cband, adz, maxband) ;
          SUBTRACT_DDA_1(channel->cquant, channel->cband, adz, maxband) ;
        }
        HQASSERT(channel->cerror.ci >= 0,
                 "Colour error exceeds one band after single normalisation") ;
      }

      if ( channel->nxs.ci < min_ex )
        min_ex = channel->nxs.ci ;
    }

    min_nx = min_ex ;
  }
}

/* Macro to generate noise from various inputs. The PNRG used has a 2^32
   modulus, using the position and colorant index as seeds. The PNRG comes
   from p.284 of Numerical Recipes in C. We run two iterations of the PNRG
   because one will not sufficiently randomise the high-order bits; seed
   values along a line are a small power of two apart, so the randomised
   values will be different that small powers of two multiplied by 0x19660d.
   The top bits of this are likely to be the same, providing approximately
   the same noise. The low bits are not sufficiently randomised either,
   because of the masking used to create the seed. */
#define NOISE_TABLE_MAX 255

static uint32 gspan_noise_table_x[NOISE_TABLE_MAX + 1] = { 0u },
              gspan_noise_table_y[NOISE_TABLE_MAX + 1] = { 0u } ;

#define GENERATE_NOISE(_x, _y, _channel, _index) MACRO_START \
  register GOURAUD_DDA *_channel_ = (_channel) ; \
  register int32 _noisewanted_ = 0, _noisediff_ = 0 ; \
  register int32 _x_ = (_x) >> gspanc->noiseshift ; \
  register int32 _y_ = (_y) >> gspanc->noiseshift ; \
  register uint32 _seed_ = gspan_noise_table_x[_x_ & NOISE_TABLE_MAX] + \
                           _x_ * _index + \
                           gspan_noise_table_y[_y_ & NOISE_TABLE_MAX] + \
                           _y_ ; \
  _seed_ = _seed_ * 1664525ul + 1013904223ul ; \
  /* This calculation must be signed, so that the noise is balanced. \
     We ignore the fractional components of cerror. This tends to \
     underestimate the error, but that is OK, since noise addition is a \
     random process anyway. Normalise the error to the range 0..1 by \
     dividing by cband. */ \
  HQASSERT(_channel_->cerror.ci * _channel_->maxband <= COLORVALUE_MAX, \
           "Colour error overflow in noise generation") ; \
  _noisewanted_ = _channel_->cerror.ci * (int32)_channel_->maxband + \
                  (int32)((int16)(_seed_ >> 16) * gspanc->noise) ; \
  if ( _noisewanted_ < 0 ) \
    _noisewanted_ -= (int32)COLORVALUE_MAX - 1 ; /* floor() */ \
  _noisewanted_ /= (int32)COLORVALUE_MAX ; \
  _noisediff_ = _noisewanted_ - _channel_->noiseadded ; \
  if ( _noisediff_ != 0 ) { \
    shfill_dda_t _cband_ = _channel_->cband ; \
    uint32 _maxband_ = _channel_->maxband, _adz_ = gspanc->adz ; \
    if ( _noisediff_ < 0 ) { \
      _noisediff_ = -_noisediff_ ; \
      NEGATE_DDA(_cband_, _adz_, _maxband_) ; \
    } \
    ADD_DDA_N(_channel_->cquant, _cband_, _noisediff_, _adz_, _maxband_) ; \
    _channel_->noiseadded = _noisewanted_ ; \
  } \
MACRO_END

#define REMOVE_NOISE(_channel) MACRO_START \
  register GOURAUD_DDA *_channel_ = (_channel) ; \
  int32 _noiseadded_ = _channel_->noiseadded ; \
  if ( _noiseadded_ != 0 ) { \
    shfill_dda_t _cband_ = _channel_->cband ; \
    uint32 _maxband_ = _channel_->maxband, _adz_ = gspanc->adz ; \
    if ( _noiseadded_ > 0 ) \
      NEGATE_DDA(_cband_, _adz_, _maxband_) ; \
    else \
      _noiseadded_ = -_noiseadded_ ; \
    ADD_DDA_N(_channel_->cquant, _cband_, _noiseadded_, _adz_, _maxband_) ; \
    _channel_->noiseadded = 0 ; \
  } \
MACRO_END

/* This version of the 1-channel span function adds noise to cquant according
   to the ShadingAntiAliasSize and ShadingAntiAliasFactor userparams. */
void gspan_noise_1(render_blit_t *rb,
                   register dcoord y, register dcoord xs, register dcoord xe)
{
  GOURAUD_CONTEXT *gspanc ;
  register GOURAUD_DDA *channel ;
  int32 xdiff, ydiff, nxl ;
  uint32 maxband, adz ;
  shfill_dda_t cquant, cerror ;
  blit_color_t *color = rb->color ;
  COLORANTINDEX ci ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_unpacked, "Output color has never been unpacked") ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;

  GET_BLIT_DATA(rb->blits, GOURAUD_BLIT_INDEX, gspanc) ;
  VERIFY_OBJECT(gspanc, GOURAUD_CONTEXT_NAME) ;

  channel = &gspanc->channel[0] ;
  HQASSERT(channel->bci < BLIT_MAX_CHANNELS, "Invalid channel index");
  HQASSERT((color->state[channel->bci] & (blit_channel_present|blit_channel_override)) == blit_channel_present,
           "Sole channel is overprinted, transparent, missing or overridden") ;
  ci = color->map->channel[channel->bci].ci ;
  xdiff = (xs >> gspanc->noiseshift) - gspanc->last_x ;
  ydiff = (y >> gspanc->noiseshift) - gspanc->last_y ;
  nxl = (xs | gspanc->noisemask) - xs ;
  maxband = channel->maxband ;
  adz = gspanc->adz ;

  if ( ydiff != 0 || xdiff != 0 ) {
    update_gspan_scanline(channel, ydiff, xdiff, adz) ;
    dda_assertions(gspanc, channel) ;

    /* Leave X and Y converged at start of span; next line will probably start
       close to this point, reducing convergence computation. */
    gspanc->last_y += ydiff ;
    gspanc->last_x += xdiff ;
  }

  cquant = channel->cquant ;
  cerror = channel->cerror ;

  /* We're only updating the scanline colour and error DDAs once per
     block, but we still need to update the next span length. */
  for ( ;; ) {
    register dcoord ex = xs + nxl ;

    HQASSERT(xs <= ex, "ex is now too small") ;
    if ( ex > xe )
      ex = xe ;

    /* This check is too expensive and should't appear
     * in normal debug builds.
     */
#ifdef EXPENSIVE_TESTS
    {
      int32 vc = channel->cross_z ; /* +ve */
      int32 vcv = channel->plane_const -  channel->cross_x * xs - channel->cross_y * y ;

      HQASSERT( (vcv + 100 * vc) / vc == color[i] + 100, "Start pixel is wrong color");
      vcv = channel->plane_const -  channel->cross_x * min_ex - channel->cross_y * y ;
      HQASSERT( (vcv + 100 * vc) / vc == color[i] + 100, "End pixel is wrong color");
    }
#endif

    GENERATE_NOISE(xs, y, channel, ci) ;

    DO_SPAN(rb, y, xs, ex) ;
    if ( ex == xe ) {
      /* Don't need to remove noise from cquant because it is overwritten. */
      channel->noiseadded = 0 ;
      channel->cquant = cquant ;
      channel->cerror = cerror ;
      return;
    }

    xs = ex + 1 ;
    nxl = gspanc->noisemask ;

    /* Step for new colour */
    ADD_DDA_1(channel->cquant, channel->cqx, adz, maxband) ;
    ADD_DDA_1(channel->cerror, channel->cex, adz, maxband) ;

    /* If colour error exceeds a band, shift quantised colour by one */
    if ( channel->cerror.ci < 0 ) {
      ADD_DDA_1(channel->cerror, channel->cband, adz, maxband) ;
      SUBTRACT_DDA_1(channel->cquant, channel->cband, adz, maxband) ;
    }
    HQASSERT(channel->cerror.ci >= 0,
             "Colour error exceeds one band after single normalisation") ;
  }
}

void gspan_noise_n(render_blit_t *rb,
                   register dcoord y, register dcoord xs, register dcoord xe)
{
  register int i;
  GOURAUD_CONTEXT *gspanc ;
  int32 nxl, xdiff, ydiff ;
  uint32 adz ;
  blit_color_t *color = rb->color ;
  const blit_colormap_t *map ;
  struct {
    shfill_dda_t cquant, cerror ;
  } savedda[BLIT_MAX_CHANNELS] ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_unpacked, "Output color has never been unpacked") ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  GET_BLIT_DATA(rb->blits, GOURAUD_BLIT_INDEX, gspanc) ;
  VERIFY_OBJECT(gspanc, GOURAUD_CONTEXT_NAME) ;

  nxl = (xs | gspanc->noisemask) - xs ;
  adz = gspanc->adz ;
  xdiff = (xs >> gspanc->noiseshift) - gspanc->last_x ;
  ydiff = (y >> gspanc->noiseshift) - gspanc->last_y ;

  for ( i = gspanc->nchannels ; --i >= 0 ; ) {
    register GOURAUD_DDA *channel = &gspanc->channel[i] ;
    channel_index_t bci = channel->bci ;

    HQASSERT(channel->bci < BLIT_MAX_CHANNELS, "Invalid channel index");
    HQASSERT((color->state[bci] & (blit_channel_present|blit_channel_override)) == blit_channel_present,
             "Colorant for channel should be present and not overridden") ;

    /* Update error convergence. Check if noise convergence block convergence
       is the same; this will normally change less rapidly than the start
       coordinate. */
    if ( xdiff != 0 || ydiff != 0 ) {
      update_gspan_scanline(channel, ydiff, xdiff, adz) ;
      dda_assertions(gspanc, channel) ;
    }

    savedda[i].cquant = channel->cquant ;
    savedda[i].cerror = channel->cerror ;

    GENERATE_NOISE(xs, y, channel, map->channel[bci].ci) ;
  }

  /* Leave colours converged on start of span; next line will probably start
     close to this point, reducing convergence computation. */
  gspanc->last_y += ydiff ;
  gspanc->last_x += xdiff ;

  for ( ;; ) {
    register dcoord ex = xs + nxl ; /* This is now the smallest end position of any channel */

    HQASSERT(xs <= ex, "ex is now too small") ;
    if ( ex > xe )
      ex = xe  ;

    /* This check is too expensive and should't appear
     * in normal debug builds.
     */
#ifdef EXPENSIVE_TESTS
    for ( i = gspanc->nchannels ; --i >= 0 ; ) {
      int32 vc = channel->cross_z ; /* +ve */
      int32 vcv = channel->plane_const -  channel->cross_x * xs - channel->cross_y * y ;

      HQASSERT( (vcv + 100 * vc) / vc == color[i] + 100, "Start pixel is wrong color");
      vcv = channel->plane_const -  channel->cross_x * ex - channel->cross_y * y ;
      HQASSERT( (vcv + 100 * vc) / vc == color[i] + 100, "End pixel is wrong color");
    }
#endif

    DO_SPAN(rb, y, xs, ex) ;

    if ( ex == xe ) {
      for ( i = gspanc->nchannels ; --i >= 0 ; ) {
        /* Restore values of colour and colour/phase error at convergence.
           Don't need to remove noise from cquant because it is
           overwritten. */
        register GOURAUD_DDA *channel = &gspanc->channel[i] ;

        channel->cquant = savedda[i].cquant ;
        channel->cerror = savedda[i].cerror ;
        channel->noiseadded = 0 ;
      }

      return;
    }
    xs = ex + 1 ;
    nxl = gspanc->noisemask ;

    for ( i = gspanc->nchannels ; --i >= 0 ; ) {
      register GOURAUD_DDA *channel = &gspanc->channel[i] ;
      channel_index_t bci = channel->bci ;
      uint32 maxband = channel->maxband ;

      /* Step for new colour */
      ADD_DDA_1(channel->cquant, channel->cqx, adz, maxband) ;
      ADD_DDA_1(channel->cerror, channel->cex, adz, maxband) ;

      /* If colour error exceeds a band, shift quantised colour by one */
      if ( channel->cerror.ci < 0 ) {
        ADD_DDA_1(channel->cerror, channel->cband, adz, maxband) ;
        SUBTRACT_DDA_1(channel->cquant, channel->cband, adz, maxband) ;
      }
      HQASSERT(channel->cerror.ci >= 0,
               "Colour error exceeds one band after single normalisation") ;

      GENERATE_NOISE(xs, y, channel, map->channel[bci].ci) ;
    }
  }
}

/** Gouraud base function for one screened channel. The gouraud base
    functions trim the interpolated channel data to a colorvalue, which is
    then quantised or packed suitably for the underlying blits. This function
    is optimised for a single screened output channel. */
static void gspan_base_screened_1(render_blit_t *rb,
                                  register dcoord y, register dcoord xs, register dcoord xe)
{
  register int32 cv ;
  GOURAUD_CONTEXT *gspanc ;
  blit_color_t *color = rb->color ;
  const surface_t *surface ;
  register GOURAUD_DDA *channel ;
  channel_index_t bci ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_unpacked, "Output color has never been unpacked") ;
  HQASSERT(color->ncolors == 1, "Should only be one channel");

  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;

  GET_BLIT_DATA(rb->blits, GOURAUD_BASE_BLIT_INDEX, gspanc) ;
  VERIFY_OBJECT(gspanc, GOURAUD_CONTEXT_NAME) ;
  HQASSERT(gspanc->nchannels == 1, "Should only be one channel");

  channel = &gspanc->channel[0] ;
  bci = channel->bci ;
  HQASSERT(bci < BLIT_MAX_CHANNELS, "Invalid channel index");
  HQASSERT((color->state[bci] & (blit_channel_present|blit_channel_override)) == blit_channel_present,
           "Sole channel is overprinted, transparent, missing or overridden") ;
  HQASSERT(color->map->channel[bci].type == channel_is_color,
           "Screening alpha or type channel") ;

  INLINE_RANGE32(cv, channel->cquant.ci, (int32)COLORVALUE_MIN, (int32)COLORVALUE_MAX) ;
  color->unpacked.channel[bci].cv = CAST_TO_COLORVALUE(cv) ;
  COLORVALUE_MULTIPLY(cv, color->quantised.htmax[bci], color->quantised.qcv[bci]) ;
  color->quantised.state = blit_quantise_unknown ;

  surface = rb->p_ri->surface ;
  HQASSERT(surface != NULL, "No output surface") ;
  HQASSERT(surface->screened, "Not a halftoned blit") ;

  /* Reset the blit slice for the base blit, because it may have self-modified
     to use specific functions for the tone value. */
  SET_BLIT_SLICE(rb->blits, BASE_BLIT_INDEX, rb->clipmode,
                 &surface->baseblits[rb->clipmode]) ;

  DO_SPAN(rb, y, xs, xe) ;
}

/** Gouraud base function for one contone channel. The gouraud base
    functions trim the interpolated channel data to a colorvalue, which is
    then quantised or packed suitably for the underlying blits. This function
    is optimised for a single contone output channel. */
static void gspan_base_contone_1(render_blit_t *rb,
                                 register dcoord y, register dcoord xs, register dcoord xe)
{
  register int32 cv ;
  GOURAUD_CONTEXT *gspanc ;
  blit_color_t *color = rb->color ;
  register GOURAUD_DDA *channel ;
  channel_index_t bci ;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_unpacked, "Output color has never been unpacked") ;
  VERIFY_OBJECT(color->map, BLIT_MAP_NAME) ;
  HQASSERT((color->ncolors == 1 && color->map->alpha_index >= color->map->nchannels)
           || (color->ncolors == 0 && color->map->alpha_index < color->map->nchannels),
           "Should only be one channel"); /* Either color or alpha */

  GET_BLIT_DATA(rb->blits, GOURAUD_BASE_BLIT_INDEX, gspanc) ;
  VERIFY_OBJECT(gspanc, GOURAUD_CONTEXT_NAME) ;
  HQASSERT(gspanc->nchannels == 1, "Should only be one channel");

  channel = &gspanc->channel[0] ;
  bci = channel->bci ;
  HQASSERT(bci < BLIT_MAX_CHANNELS, "Invalid channel index");

  HQASSERT((color->state[bci] & (blit_channel_present|blit_channel_override)) == blit_channel_present,
           "Sole channel is overprinted, transparent, missing or overridden") ;
  HQASSERT(color->map->channel[bci].type != channel_is_type,
           "Interpolating type channel") ;

  INLINE_RANGE32(cv, channel->cquant.ci, (int32)COLORVALUE_MIN, (int32)COLORVALUE_MAX) ;
  color->unpacked.channel[bci].cv = CAST_TO_COLORVALUE(cv) ;
  color->alpha = color->unpacked.channel[color->map->alpha_index].cv ;
#ifdef ASSERT_BUILD
  color->valid = blit_color_unpacked;
#endif

  blit_color_quantise(color) ;
  blit_color_pack(color) ;

  /* We don't reset the base blits here, because they might use
     self-modifying blit slices for overprint optimisation. We don't want to
     re-do that decision for every span. This does mean that self-modifying
     blit slices cannot be used for tone optimisation. */

  DO_SPAN(rb, y, xs, xe) ;
}

/** Gouraud base function for many contone channels. The gouraud base
    functions trim the interpolated channel data to a colorvalue, which is
    then quantised or packed suitably for the underlying blits. This function
    is optimised for multiple contone output channels. */
static void gspan_base_contone_n(render_blit_t *rb,
                                 register dcoord y, register dcoord xs, register dcoord xe)
{
  GOURAUD_CONTEXT *gspanc ;
  blit_color_t *color = rb->color ;
  const blit_colormap_t *map ;
  int i;

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_unpacked, "Output color has never been unpacked") ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  GET_BLIT_DATA(rb->blits, GOURAUD_BASE_BLIT_INDEX, gspanc) ;
  VERIFY_OBJECT(gspanc, GOURAUD_CONTEXT_NAME) ;

  for ( i = gspanc->nchannels ; --i >= 0 ; ) {
    register GOURAUD_DDA *channel = &gspanc->channel[i] ;
    channel_index_t bci = channel->bci ;
    register int32 cv ;

    HQASSERT(bci < BLIT_MAX_CHANNELS, "Invalid channel index");
    /* Leave omitted or overridden colorants alone. */
    HQASSERT((color->state[bci] & (blit_channel_present|blit_channel_override)) == blit_channel_present,
             "Colorant for channel should be present and not overridden") ;
    HQASSERT(map->channel[bci].type != channel_is_type, "Interpolating type channel") ;
    INLINE_RANGE32(cv, channel->cquant.ci, (int32)COLORVALUE_MIN, (int32)COLORVALUE_MAX) ;
    color->unpacked.channel[bci].cv = CAST_TO_COLORVALUE(cv) ;
  }

  color->alpha = color->unpacked.channel[map->alpha_index].cv ;
#ifdef ASSERT_BUILD
  color->valid = blit_color_unpacked;
#endif

  blit_color_quantise(color) ;
  blit_color_pack(color) ;

  /* We don't reset the base blits here, because they might use
     self-modifying blit slices for overprint optimisation. We don't want to
     re-do that decision for every span. This does mean that self-modifying
     blit slices cannot be used for tone optimisation. */

  DO_SPAN(rb, y, xs, xe) ;
}

/* next_block is in the gouraud slices because the background is rendered
   using a rectfill after the blits have been initialised. */
void surface_gouraud_builtin_screened(surface_t *surface)
{
  HQASSERT(surface != NULL, "No surface to hook up") ;

  /* next_block is in the gouraud slices because the background is rendered
     using a rectfill after the blits have been initialised. */
  surface->gouraudbaseblits.spanfn = gspan_base_screened_1 ;
  surface->gouraudbaseblits.blockfn = next_block ;

  surface->gouraudinterpolateblits[BLT_GOUR_SMOOTH].spanfn = gspan_1 ;
  surface->gouraudinterpolateblits[BLT_GOUR_SMOOTH].blockfn = next_block ;
  surface->gouraudinterpolateblits[BLT_GOUR_NOISE].spanfn = gspan_noise_1 ;
  surface->gouraudinterpolateblits[BLT_GOUR_NOISE].blockfn = next_block ;
}

void surface_gouraud_builtin_tone(surface_t *surface)
{
  HQASSERT(surface != NULL, "No surface to hook up") ;

  /* next_block is in the gouraud slices because the background is rendered
     using a rectfill after the blits have been initialised. */
  surface->gouraudbaseblits.spanfn = gspan_base_contone_1 ;
  surface->gouraudbaseblits.blockfn = next_block ;

  surface->gouraudinterpolateblits[BLT_GOUR_SMOOTH].spanfn = gspan_1 ;
  surface->gouraudinterpolateblits[BLT_GOUR_SMOOTH].blockfn = next_block ;
  surface->gouraudinterpolateblits[BLT_GOUR_NOISE].spanfn = gspan_noise_1 ;
  surface->gouraudinterpolateblits[BLT_GOUR_NOISE].blockfn = next_block ;
}

void surface_gouraud_builtin_tone_multi(surface_t *surface)
{
  HQASSERT(surface != NULL, "No surface to hook up") ;

  /* next_block is in the gouraud slices because the background is rendered
     using a rectfill after the blits have been initialised. */
  surface->gouraudbaseblits.spanfn = gspan_base_contone_n ;
  surface->gouraudbaseblits.blockfn = next_block ;

  surface->gouraudinterpolateblits[BLT_GOUR_SMOOTH].spanfn = gspan_n ;
  surface->gouraudinterpolateblits[BLT_GOUR_SMOOTH].blockfn = next_block ;
  surface->gouraudinterpolateblits[BLT_GOUR_NOISE].spanfn = gspan_noise_n ;
  surface->gouraudinterpolateblits[BLT_GOUR_NOISE].blockfn = next_block ;
}

static Bool render_gouraud_blit(Hq32x2 coords[6],
                                p_ncolor_t colors[3],
                                GOURAUD_FILL *gfill)
{
  dcoord icoords[6], xmin, xmax ;
  size_t index, nthreads = 0 ;
  render_info_t rinfo ;
  GOURAUD_CONTEXT *gspan;
  Bool halftone_locked = FALSE ;
  Bool workspace_locked = FALSE ;
  blit_color_t *color ;
  const surface_t *surface ;
  dlc_context_t *dlc_context ;

  /* The gspan->channel normally points at this, since most of the time
     there's only one channel; pixel-interleaved contone has a maximum of 4
     channels at present; but pixel-interleaved for color rle has an
     arbitrary number of channels, but rarely more than 4 or 6 at a time.
     When this is exceeded, we'll allocate memory and point channel at it. */
  GOURAUD_DDA fixed_channels[RENDERSH_MAX_FIXED_CHANNELS];

  RI_COPY_FROM_RI(&rinfo, gfill->p_ri) ;
  GET_BLIT_DATA(rinfo.rb.blits, GOURAUD_BLIT_INDEX, gspan) ;
  VERIFY_OBJECT(gspan, GOURAUD_CONTEXT_NAME) ;

  color = rinfo.rb.color ;
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;

  dlc_context = rinfo.p_rs->page->dlc_context ;

  ++gfill->linear_triangles ;

  /* Maintain points in highest-lowest y order */
#if 0
  /* I'd like to use this assert, to reduce sorting when the linear
     interpolated bitblit is implemented, but it doesn't work because of the
     integer subdivision; for example, point 2 can be slightly above and
     right of point 3, but when subdivided the new points 2 and 3 are
     truncated to the same line, with the x-coordinates now out of order. */
  HQASSERT((coords[1] < coords[3] ||
            (coords[1] == coords[3] && coords[0] <= coords[2])) &&
           (coords[3] < coords[5] ||
            (coords[3] == coords[5] && coords[2] <= coords[4])),
           "Triangle points out of order in render_gouraud_blit") ;
  /* Weaker assert follows: */
#endif
  HQASSERT((coords[1].high < coords[3].high ||
            (coords[1].high == coords[3].high &&
             coords[1].low <= coords[3].low)) &&
           (coords[3].high < coords[5].high ||
            (coords[3].high == coords[5].high &&
             coords[3].low <= coords[5].low)),
           "Triangle points out of order in render_gouraud_blit") ;

  /* Unpack coordinates. Coordinates are pre-rounded; 0.5 was added to each
     coordinate when they were converted to Hq32x2. */
  for ( index = 0 ; index < 6 ; ++index )
    icoords[index] = coords[index].high ;

  /* Find horizontal extent of triangle */
  xmin = xmax = icoords[0] ;
  if ( icoords[2] < xmin )
    xmin = icoords[2] ;
  else if ( icoords[2] > xmax )
    xmax = icoords[2] ;
  if ( icoords[4] < xmin )
    xmin = icoords[4] ;
  else if ( icoords[4] > xmax )
    xmax = icoords[4] ;

  /* Clip out whole triangle? */
  /** \todo Could test for intersection with the actual triangle lines,
     rather than its bounding box. */
  if ( !bbox_intersects_coordinates(&rinfo.clip, xmin, icoords[1], xmax, icoords[5]) )
    return TRUE ;

  /* single fill, with span set to special function. */
  for ( index = 0 ; index < 3 ; index++ ) {
    NBRESS *nbress = gfill->tnfill.thread[nthreads];
    int32 next = (index + 1) % 3 ;
    dcoord x1 = icoords[2*index], y1 = icoords[2*index + 1] ;
    dcoord x2 = icoords[2*next], y2 = icoords[2*next + 1] ;

    /* Clip out thread? Clip to right but not left. */
    if ( x1 > rinfo.clip.x2 && x2 > rinfo.clip.x2 )
      continue ;

    if ( y1 < y2 ) {
      if ( y1 > rinfo.clip.y2 || y2 < rinfo.clip.y1 )
        continue ;
      nbress->nx1 = x1 ;
      nbress->ny1 = y1 ;
      nbress->nx2 = x2 ;
      nbress->ny2 = y2 ;
      nbress->norient = NORIENTDOWN ;
    } else { /* Reverse thread */
      if ( y2 > rinfo.clip.y2 || y1 < rinfo.clip.y1 )
        continue ;
      nbress->nx1 = x2 ;
      nbress->ny1 = y2 ;
      nbress->nx2 = x1 ;
      nbress->ny2 = y1 ;
      nbress->norient = -NORIENTDOWN ;
    }
    ++nthreads;
#if defined( DEBUG_BUILD )
    /*
     * If outline debug enabled, double up vectors but with reverse
     * orientation so that we get the 0-width stroked outline.
     */
    if ( (shading_debug_flag & SHADING_DEBUG_OUTLINE) != 0 ) {
      NBRESS *nbress2 = gfill->tnfill.thread[nthreads];
      *nbress2 = *nbress;
      nbress2->norient = -nbress->norient;
      nthreads++;
    }
#endif
  }

  /* Clip out if there are no threads. */
  if ( nthreads == 0 )
    return TRUE ;

  gfill->tnfill.type = NZFILL_TYPE;
  gfill->tnfill.nexty = MAXDCOORD ; /* Force initialisation */
  gfill->tnfill.y1clip = rinfo.clip.y1 ; /* Not preset */
  gfill->tnfill.nthreads = (int32)nthreads;
  gfill->tnfill.converter = SC_RULE_HARLEQUIN ;

  /* Optimise out clipping if within x1maskclip/x2maskclip area. */
  if ( xmin >= rinfo.x1maskclip && xmax <= rinfo.x2maskclip )
    rinfo.rb.clipmode = BLT_CLP_RECT;

  rinfo.lobj = &gfill->tlobj ;

  if ( REPLICATING(&rinfo) )
    REPAIR_REPLICATED_NFILL(&gfill->tnfill, rinfo.clip.y1);
  else
    REPAIR_NFILL(&gfill->tnfill, rinfo.clip.y1);

  surface = rinfo.surface ;
  HQASSERT(surface != NULL, "No output surface") ;

  SET_BLITS(rinfo.rb.blits, BASE_BLIT_INDEX,
            &surface->baseblits[BLT_CLP_NONE],
            &surface->baseblits[BLT_CLP_RECT],
            &surface->baseblits[BLT_CLP_COMPLEX]);

  if ( gspan->nchannels != 0 ) { /* Not rendering a knock-out */
    /* Initialize this triangle.  IWBNI we cached the values between bands. */
    const blit_colormap_t *map = color->map ;
    int32 cross, adz_sign ;
    uint32 adz ;
    dl_color_t dcolor ;
    const blit_slice_t *gblit ;
    blit_color_t *bcolors = &gspan->colors[0] ;
    channel_index_t bci ;
    Bool otf_convert = ri_converting_on_the_fly(&rinfo)
                       && (rinfo.lobj->marker & MARKER_DEVICECOLOR) == 0;

    VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

    /* Assume we'll draw something that isn't the background. For knocking out,
       this was checked in render_shfill. */
    *rinfo.p_rs->cs.p_white_on_white = FALSE;

    if (gspan->colorWorkspace) {
      GOURAUD_LOCK_WR_CLAIM(gspan->colorWorkspace);
      workspace_locked = TRUE ;
      gspan->channel = (GOURAUD_DDA*)gspan->colorWorkspace;
    } else {
      HQASSERT(gspan->nchannels <= RENDERSH_MAX_FIXED_CHANNELS,
               "Not enough channels for shfill");
      gspan->channel = fixed_channels;
    }

#ifdef EXPENSIVE_TESTS
    HQFAIL( "Expensive tests shouldn't be enabled in a normal debug RIP." ) ;
#endif

    dlc_from_dl_weak(colors[0], &dcolor) ;

    if ( otf_convert ) {
      /* The DL object has a device-independent color and so must overlap at
         least one region which is to be backdrop rendered (for transparency,
         spot overprinting etc.).  However for the current region, the object
         can be rendered out directly once the color has been converted to a
         device color.  This device color is not stored and must be freed
         after the blit color has been set up.
      */
      if ( !preconvert_on_the_fly(groupRendering(&rinfo), rinfo.lobj,
                                  GSC_SHFILL, &dcolor, &dcolor) )
        return FALSE;
    }
    blit_color_unpack(&bcolors[0], &dcolor,
                      gspan->object_label, gspan->lca,
                      gspan->knockout, gspan->selected, FALSE, FALSE);

    /* Z component of plane normal (cross product) */
    HQASSERT(cross_check(icoords[0], icoords[1],
                         icoords[2], icoords[3],
                         icoords[4], icoords[5]),
             "Z component cross product would overflow") ;
    cross = ((icoords[4] - icoords[0]) * (icoords[3] - icoords[1]) -
             (icoords[2] - icoords[0]) * (icoords[5] - icoords[1])) ;

    if ( cross == 0 ) {
      /* Degenerate - convert to something sensible. */
      if ( icoords[1] == icoords[5] ) {
        HQASSERT( icoords[3] == icoords[5], "Point ordering assertion wrong" ) ;
        /* Sort X coordinates; Y are already the same */
        if ( icoords[0] > icoords[4] ) {
          int32 tmp = icoords[0] ; icoords[0] = icoords[4] ; icoords[4] = tmp ;
        }
        if ( icoords[0] > icoords[2] ) {
          int32 tmp = icoords[0] ; icoords[0] = icoords[2] ; icoords[2] = tmp ;
        } else if ( icoords[2] > icoords[4] ) {
          int32 tmp = icoords[2] ; icoords[2] = icoords[4] ; icoords[4] = tmp ;
        }
        if ( icoords[0] == icoords[4] ) {
          /* point degeneracy */
          icoords[2] = icoords[0] + 1 ;
          icoords[3] = icoords[1] ;
          icoords[4] = icoords[0] ;
          icoords[5] = icoords[1] + 1 ;
          /* dcolor is still set to colors[0]. */
          blit_color_unpack(&bcolors[1], &dcolor,
                            gspan->object_label, gspan->lca,
                            gspan->knockout, gspan->selected, FALSE, FALSE);
          blit_color_unpack(&bcolors[2], &dcolor,
                            gspan->object_label, gspan->lca,
                            gspan->knockout, gspan->selected, FALSE, FALSE);
          /* Equivalent of:
             cross = (icoords[4] - icoords[0]) * (icoords[3] - icoords[1]) -
                     (icoords[2] - icoords[0]) * (icoords[5] - icoords[1]) ;
          */
          cross = -1 ;
        }
      }

      if ( cross == 0 ) {
        /* Wasn't point degenerate. Replace middle point with another, such
           that colour change is directly between first and last points.
           Ordering doesn't matter anymore, these are not used for drawing,
           only for calculating plane normals. */
        icoords[2] = icoords[0] + (icoords[5] - icoords[1]);
        icoords[3] = icoords[1] - (icoords[4] - icoords[0]);

        /* Copy of point[0]: */
        blit_color_unpack(&bcolors[1], &dcolor,
                          gspan->object_label, gspan->lca,
                          gspan->knockout, gspan->selected, FALSE, FALSE);

        if ( otf_convert )
          dlc_release(dlc_context, &dcolor);
        dlc_from_dl_weak(colors[2], &dcolor) ;
        if ( otf_convert ) {
          if ( !preconvert_on_the_fly(groupRendering(&rinfo), rinfo.lobj,
                                      GSC_SHFILL, &dcolor, &dcolor) )
            return FALSE;
        }
        blit_color_unpack(&bcolors[2], &dcolor,
                          gspan->object_label, gspan->lca,
                          gspan->knockout, gspan->selected, FALSE, FALSE);

        HQASSERT(cross_check(icoords[0], icoords[1],
                             icoords[2], icoords[3],
                             icoords[4], icoords[5]),
                 "Degenerate Z component cross product would overflow");
        cross = ((icoords[4] - icoords[0]) * (icoords[3] - icoords[1]) -
                 (icoords[2] - icoords[0]) * (icoords[5] - icoords[1])) ;
      }
    } else {
      if ( otf_convert )
        dlc_release(dlc_context, &dcolor);
      dlc_from_dl_weak(colors[1], &dcolor) ;
      if ( otf_convert ) {
        if ( !preconvert_on_the_fly(groupRendering(&rinfo), rinfo.lobj,
                                    GSC_SHFILL, &dcolor, &dcolor) )
            return FALSE;
      }
      blit_color_unpack(&bcolors[1], &dcolor,
                        gspan->object_label, gspan->lca,
                        gspan->knockout, gspan->selected, FALSE, FALSE);

      if ( otf_convert )
        dlc_release(dlc_context, &dcolor);
      dlc_from_dl_weak(colors[2], &dcolor) ;
      if ( otf_convert ) {
        if ( !preconvert_on_the_fly(groupRendering(&rinfo), rinfo.lobj,
                                    GSC_SHFILL, &dcolor, &dcolor) )
          return FALSE;
      }
      blit_color_unpack(&bcolors[2], &dcolor,
                        gspan->object_label, gspan->lca,
                        gspan->knockout, gspan->selected, FALSE, FALSE);
    }
    if ( otf_convert )
      dlc_release(dlc_context, &dcolor);

    gspan->last_x = icoords[0] ; /* Setup colors for top line */
    gspan->last_y = icoords[1] ;

    /* Note that all of the calculations using the cross product signs use
       greater than zero rather than less than zero, because the vector
       (adx,ady,adz) is normal to the colour change vector, so the ratios needs
       to be rotated by 90 degrees for the colour change vector. */
    HQASSERT(cross != 0, "Degeneracy handling failed") ;
    if ( cross < 0 ) {
      cross = -cross ;
      adz_sign = -1 ;
    } else
      adz_sign = 1 ;
    gspan->adz = adz = (uint32)cross ;

    HQASSERT(bcolors[0].nchannels == bcolors[1].nchannels &&
             bcolors[1].nchannels == bcolors[2].nchannels,
             "Number of colors should match for all corners") ;

    /* Set up DDAs for each colorant */
    for ( index = 0, bci = 0 ; bci < map->nchannels ; ++bci ) {

      /* We don't need to calculate DDAs for channels that are missing, or are
         overridden (e.g. knocked out). */
      if ( (color->state[bci] & (blit_channel_present|blit_channel_override)) == blit_channel_present
           && map->channel[bci].type != channel_is_type
           && (map->channel[bci].type != channel_is_alpha
               || gspan->varying_alpha) ) {
        register GOURAUD_DDA *channel = &gspan->channel[index++] ;
        uint32 adx, ady ;
        int32 adx_sign = 0, ady_sign = 0 ;
        COLORVALUE c0, c1, c2, htmax ;
        uint32 maxband, factor ;
        Bool same_color ;

        /* The channels present should be the same between all corners. */
        HQASSERT(bcolors[0].state[bci] == bcolors[1].state[bci] &&
                 bcolors[1].state[bci] == bcolors[2].state[bci],
                 "Overprint and override channels should be the same for all corners") ;
        HQASSERT(color->state[bci] == bcolors[0].state[bci],
                 "Overprint and override channels should be the same for corner and object color") ;

        channel->bci = bci ;

        /* Clear noise factors and phase */
        channel->noiseadded = 0 ;

        /* The number of bands used is the lowest exact factor of htmax greater
           than or equal to the minimum number of bands (in sobj->mbands). */
        htmax = color->quantised.htmax[bci] ;
#ifdef ASSERT_BUILD
        channel->htmax = htmax ;
#endif

        for ( maxband = htmax, factor = (uint32)htmax / gspan->mbands ; factor > 1 ; --factor ) {
          uint32 cofactor = htmax / factor ;

          if ( factor * cofactor == htmax ) {
            maxband = cofactor ;
            break ;
          }
        }
        channel->maxband = CAST_UNSIGNED_TO_UINT16(maxband) ;

        /* Calculate colour step per band. Note this is unsigned; adz sign
           should be applied later, to cqx/cqy. */
        INITIALISE_DDA_DLO(channel->cband, COLORVALUE_MAX, adz, maxband) ;
#if defined(ASSERT_BUILD)
        { /* Verify cquant calculation using different arithmetic */
          shfill_dda_t cbd ;
          INITIALISE_DDA_HILO_DOUBLE(cbd, adz, COLORVALUE_MAX, adz, maxband) ;
          HQASSERT(channel->cband.ci == cbd.ci &&
                   channel->cband.cfh == cbd.cfh &&
                   channel->cband.cfl == cbd.cfl,
                   "Colour band computations do not agree") ;
        }
#endif

        c0 = bcolors[0].unpacked.channel[bci].cv ;
        c1 = bcolors[1].unpacked.channel[bci].cv ;
        c2 = bcolors[2].unpacked.channel[bci].cv ;

        { /* Set initial quantised colour (c0), and error from quantised colour.
             Band quantisation rounds to cband step:

             cq = trunc((c0 + cband/2)/cband) * cband
             = trunc((c0 + cmax/(2*maxband)) * (maxband/cmax)) * (cmax/maxband)
             = trunc(c0*maxband/cmax + (cmax * maxband)/(2*maxband*cmax)) * cmax / maxband
             = trunc(c0*maxband/cmax + 1/2) * cmax / maxband
             = trunc((c0*maxband + cmax/2)/cmax) * cmax / maxband
             cq + ce = c0
          */
#define QUANTISE_BAND(_c, _mb)                                          \
          ((((_c) * (_mb) + COLORVALUE_HALF) / COLORVALUE_MAX) * COLORVALUE_MAX)

          uint32 bq = QUANTISE_BAND(c0, maxband) ;

          /* If all of the corners quantise to the same value, note that the
             colour across the triangle is flat. This will be used in some cases
             to optimise the rendering. */
          same_color = (gspan->noise == 0.0f &&
                        bq == QUANTISE_BAND(c1, maxband) &&
                        bq == QUANTISE_BAND(c2, maxband)) ;

          INITIALISE_DDA_DLO(channel->cquant, bq, adz, maxband) ;
#if defined(ASSERT_BUILD)
          { /* Verify cquant calculation using different arithmetic */
            shfill_dda_t cqd ;
            INITIALISE_DDA_HILO_DOUBLE(cqd, adz, bq, adz, maxband) ;
            HQASSERT(channel->cquant.ci == cqd.ci &&
                     channel->cquant.cfh == cqd.cfh &&
                     channel->cquant.cfl == cqd.cfl,
                     "Colour quantisation computations do not agree") ;
          }
#endif

          /* The colour error is biased by cband/2 so that we do not need to
             test if it is less than -cband/2 or greater than cband/2 when
             updating a span. The test becomes less than zero or greater than
             cband, which is then further optimised to less than zero by
             ensuring the error per Y and error per span are negative
             (adjusting the quanta per Y or band appropriately). */
          channel->cerror = channel->cband ;
          DIVIDE_DDA_2(channel->cerror, adz, maxband) ;
          SUBTRACT_DDA_1(channel->cerror, channel->cquant, adz, maxband) ;
          channel->cerror.ci += c0 ;
        }

        /* Work out colour change vector DDA increments */

        /* Y component of 3d cross-product (plane normal) */
        HQASSERT(cross_check(icoords[0], c0, icoords[4], c2, icoords[2], c1),
                 "Y component cross product would overflow") ;
        cross = ((icoords[2] - icoords[0]) * (c2 - c0) -
                 (icoords[4] - icoords[0]) * (c1 - c0)) ;
        if ( cross < 0 ) {
          cross = -cross ;
          ady_sign = -1 ;
        } else if ( cross > 0 )
          ady_sign = 1 ;
        ady = (uint32)cross ;

        if ( same_color ) {
          /* Corners are all the same color */
          INITIALISE_DDA_I(channel->cqy, 0) ;
          INITIALISE_DDA_I(channel->cey, 0) ;
          /** \todo ajcd 2008-10-28: We could mark this colorant as an
              override, which will make rendering faster, but would not allow
              the noise addition to operate on this sub-triangle. That might
              cause visible artifacts in blends. */
        } else {
          /* Set the colour change per Y coordinate. The colour change per Y is
             ady/adz, but needs to be divided into the quantised change cqy and
             the remainder cey (cqy quantised to the truncated closest multiple
             of cband). The quantisation is found by finding the whole number of
             bands per Y, and multiplying by cband.

             bpy = trunc((crossy/crossc) * (maxband/cmax))

             Since we're only interested in the integral part, we take colour
             change per Y, multiply by maxband and divide by cmax, ignoring the
             fractional round-off.

             cpy = [trunc(crossy/crossc), crossy - trunc(crossy/crossc) * crossc, 0]

             Since cpy should not exceed cmax, the multiplication by maxband is
             safe from overflow:

             bpy' = NORMALISE(cpy * maxband)

             Fractional parts do not affect integral result in following:

             bpy = bpy'.cvi / cmax

             cqy = cband * bpy
             cey + cqy = cpy, 0 >= cey > -cband
          */
          uint32 bpy ; /* Bands per Y */
          shfill_dda_t cpy, tmp ;

          cpy.ci = ady/adz ;
          cpy.cfh = ady - cpy.ci * adz ;
          cpy.cfl = 0 ;

          HQASSERT(MAXINT32 / (cpy.ci + 1) >= (int32)maxband,
                   "Overflow in Y colour change quantisation") ;

          INITIALISE_DDA_I(tmp, 0) ;
          ADD_DDA_N(tmp, cpy, maxband, adz, maxband) ;

          bpy = tmp.ci / COLORVALUE_MAX ;
          HQASSERT(bpy <= COLORVALUE_MAX, "More bands per Y than colour values") ;

          INITIALISE_DDA_I(channel->cqy, 0) ;
          ADD_DDA_N(channel->cqy, channel->cband, bpy, adz, maxband) ;

          /* Work out negated error rather than positive error. This has the
             advantage that it is quick to test if the error is zero (the
             integral part will be zero), and it's what we want for one case
             anyway. */
          channel->cey = channel->cqy ;
          SUBTRACT_DDA_1(channel->cey, cpy, adz, maxband) ;

          /* Now restore invariant cqy + cey = cpy */
          if ( ady_sign * adz_sign > 0 ) {
            NEGATE_DDA(channel->cqy, adz, maxband) ;
          } else if ( channel->cey.ci < 0 ) {
            ADD_DDA_1(channel->cqy, channel->cband, adz, maxband) ;
            ADD_DDA_1(channel->cey, channel->cband, adz, maxband) ;
            NEGATE_DDA(channel->cey, adz, maxband) ;
          }

          HQASSERT(channel->cey.ci < 0 ||
                   (channel->cey.ci == 0 &&
                    channel->cey.cfh == 0 &&
                    channel->cey.cfl == 0),
                   "Y colour error is positive") ;
        }

        /* X component of 3d cross-product (plane normal) */
        HQASSERT(cross_check(icoords[1], c0, icoords[3], c1, icoords[5], c2),
                 "X component cross product would overflow") ;
        cross = ((icoords[5] - icoords[1]) * (c1 - c0) -
                 (icoords[3] - icoords[1]) * (c2 - c0)) ;
        if ( cross < 0 ) {
          cross = -cross ;
          adx_sign = -1 ;
        } else if ( cross > 0 )
          adx_sign = 1 ;
        channel->adx = adx = (uint32)cross ;

        if ( adx == 0 || same_color ) {
          /* No colour change in horizontal */
          INITIALISE_DDA_I(channel->cqx, 0) ;
          INITIALISE_DDA_I(channel->cex, 0) ;
          INITIALISE_DDA_I(channel->nxs, xmax - icoords[0] + 1) ;
          INITIALISE_DDA_I(channel->xps, xmax - xmin + 1) ;
          INITIALISE_DDA_I(channel->xpy, 0) ;
        } else {
          /* Set the colour change per X coordinate. This is done in the same
             way as the colour change per Y. If the number of bands per X is
             zero, then the expressions will be inverted and the span per
             band is used instead. */
          uint32 bpx ; /* Bands per X */
          shfill_dda_t cpx, tmp ;

          cpx.ci = adx/adz ;
          cpx.cfh = adx - cpx.ci * adz ;
          cpx.cfl = 0 ;

          HQASSERT(MAXINT32 / (cpx.ci + 1) >= (int32)maxband,
                   "Overflow in X colour change quantisation") ;

          INITIALISE_DDA_I(tmp, 0) ;
          ADD_DDA_N(tmp, cpx, maxband, adz, maxband) ;

          bpx = tmp.ci / COLORVALUE_MAX ;
          HQASSERT(bpx <= COLORVALUE_MAX, "More bands per X than colour values") ;

          if ( bpx > 0 || gspan->noise > 0.0f ) {
            /* Fast colour change shifts a pixel at a time. Noise addition uses
               same principle, but a different block size. */
            INITIALISE_DDA_I(channel->cqx, 0) ;
            ADD_DDA_N(channel->cqx, channel->cband, bpx, adz, maxband) ;

            /* Work out negated error rather than positive error. This has the
               advantage that it is quick to test if the error is zero (the
               integral part will be zero), and it's what we want for one case
               anyway. */
            channel->cex = channel->cqx ;
            SUBTRACT_DDA_1(channel->cex, cpx, adz, maxband) ;

            /* Now restore invariant cqx + cex = cpx */
            if ( adx_sign * adz_sign > 0 ) {
              NEGATE_DDA(channel->cqx, adz, maxband) ;
            } else if ( channel->cex.ci < 0 ) {
              ADD_DDA_1(channel->cqx, channel->cband, adz, maxband) ;
              ADD_DDA_1(channel->cex, channel->cband, adz, maxband) ;
              NEGATE_DDA(channel->cex, adz, maxband) ;
            }

            HQASSERT(channel->cex.ci < 0 ||
                     (channel->cex.ci == 0 &&
                      channel->cex.cfh == 0 &&
                      channel->cex.cfl == 0),
                     "X colour error is positive") ;

            /* Shift exactly one pixel at a time */
            INITIALISE_DDA_I(channel->xps, 1) ;
            INITIALISE_DDA_I(channel->nxs, 1) ;
            INITIALISE_DDA_I(channel->xpy, 0) ;
          } else if ( (double)adz * (double)(icoords[5] - icoords[1]) <
                      (double)adx * (double)maxband *
                      0.5 * MAXINT32 / COLORVALUE_MAX ) {
            /* The test above determines whether the maximum convergence step
               possible can overflow the DDAs (the maximum step being the
               maximum height times the maximum x per y). The test uses doubles
               to avoid overflowing int32/uint32 intermediate results, and as a
               result is not exact. To compensate for the test not being exact,
               we use a binary order of magnitude less than we require. The
               exact test is:

               (adz / adx) * (COLORVALUE_MAX / maxband) * ymax >= MAXINT

               If this does not overflow, we shift one colour band at a time */
            channel->cqx = channel->cband ;
            INITIALISE_DDA_I(channel->cex, 0) ;

            if ( adx_sign * adz_sign > 0 )
              NEGATE_DDA(channel->cqx, adz, maxband) ;

            /* Span length per colour band is xpc * cband */
            INITIALISE_DDA_HILO(channel->xps, adz, COLORVALUE_MAX, adx, maxband) ;
#if defined(ASSERT_BUILD)
            { /* Verify xps calculation using different arithmetic */
              shfill_dda_t xpsd ;
              INITIALISE_DDA_HILO_DOUBLE(xpsd, adz, COLORVALUE_MAX, adx, maxband) ;
              HQASSERT(channel->xps.ci == xpsd.ci &&
                       channel->xps.cfh == xpsd.cfh &&
                       channel->xps.cfl == xpsd.cfl,
                       "Span length per colour band computations do not agree") ;
            }
#endif

            /* Initial phase depends on colour error and direction of colour
               change. It is either cerror * xpc or (cband - cerror) * xpc.
               Since cerror is biased to 0 <= cerror < cband, this will always
               be positive. It may be close to zero, in which case we step to
               the next band. Changing the basis from (adz,maxband) to
               (adx,maxband) is the same as multiplying by xpc, since
               xpc = adz/adx. */
            channel->nxs = channel->cerror ;
            if ( adx_sign * adz_sign < 0 ) {
              SUBTRACT_DDA_1(channel->nxs, channel->cband, adz, maxband) ;
              NEGATE_DDA(channel->nxs, adz, maxband) ;
            }

            CHANGE_DDA_BASIS(channel->nxs, adz, adx, maxband) ;

            if ( channel->nxs.ci < 1 ) {
              ADD_DDA_1(channel->nxs, channel->xps, adx, maxband) ;
              ADD_DDA_1(channel->cquant, channel->cqx, adz, maxband) ;
            }

            /* Calculate X phase shift per Y. There are two methods for
               tracking the current error; one uses the colour error fraction,
               the other uses the phase difference from the next band. Only one
               at a time should be used to adjust the convergence in Y. We have
               calculated the colour error version above, for slow colour
               change we want to change it to a phase error. This calculation
               can also be done directly, so in an assert build we verify both
               calculations give the same result. The phase tracking method
               ensures that 0 <= xpy < xps, so cqy may need to be adjusted if
               xpy would be negative. */
            if ( channel->cey.ci < 0 ) {
              if ( channel->cqx.ci < 0 ) {
                ADD_DDA_1(channel->cey, channel->cband, adz, maxband) ;
                SUBTRACT_DDA_1(channel->cqy, channel->cband, adz, maxband) ;
              } else
                NEGATE_DDA(channel->cey, adz, maxband) ;

              /* Changing basis from (adz,maxband) to (adx,maxband) is the same
                 as multiplying by adz/adx, which is x per colour. Since cey
                 was already normalised to less than cband, this result must be
                 less than x per step. */
              CHANGE_DDA_BASIS(channel->cey, adz, adx, maxband) ;
            }

#if defined(ASSERT_BUILD)
            {
              int32 xsteps ;
              shfill_dda_t cquant, cqx ;

              channel->xpy.ci = ady/adx ;
              channel->xpy.cfh = ady - channel->xpy.ci * adx ;
              channel->xpy.cfl = 0 ;

              if ( adx_sign * ady_sign > 0 )
                NEGATE_DDA(channel->xpy, adx, maxband) ;

              cqx = channel->cqx ;
              INITIALISE_DDA_I(cquant, 0) ;

              /* X phase shift per Y is normalised to 0 <= xpy < xps. This is
                 very similar to phase error renormalisation that xpy is not
                 biased by 1. */
              xsteps = renormalise_gspan_x(channel, &channel->xpy, 0) ;

              if ( xsteps > 0 )
                NEGATE_DDA(cqx, adz, maxband) ;
              else
                xsteps = -xsteps ;

              ADD_DDA_N(cquant, cqx, xsteps, adz, maxband) ;

              HQASSERT(channel->xpy.ci == channel->cey.ci &&
                       channel->xpy.cfh == channel->cey.cfh &&
                       channel->xpy.cfl == channel->cey.cfl &&
                       cquant.ci == channel->cqy.ci &&
                       cquant.cfh == channel->cqy.cfh &&
                       cquant.cfl == channel->cqy.cfl,
                       "Phase per Y calculations do not match") ;
            }
#endif

            /* We're tracking phase error, not colour error in Y. */
            channel->xpy = channel->cey ;
            INITIALISE_DDA_I(channel->cey, 0) ;
          } else if ( icoords[5] - icoords[1] <= MAXINT32 / (int32)(adz/adx + 1) ) {
            /* Slow colour change shifts one colour band at a time. If the
               largest convergence step could overflow the arithmetic used, we
               shift one fractional colour value at a time. Any colour shift
               lower than this is both extremely unlikely, and can effectively
               be treated as if there is no horizontal colour change. */
            INITIALISE_DDA_I(channel->cqx, 0) ;
            INITIALISE_DDA_I(channel->cex, -1) ;

            if ( adx_sign * adz_sign < 0 ) {
              ADD_DDA_1(channel->cqx, channel->cband, adz, maxband) ;
              ADD_DDA_1(channel->cex, channel->cband, adz, maxband) ;
              NEGATE_DDA(channel->cex, adz, maxband) ;
            }

            /* Span length per step is xpc */
            channel->xps.ci = adz/adx ;
            channel->xps.cfh = adz - channel->xps.ci * adx ;
            channel->xps.cfl = 0 ;

            /* The colour error already calculated is adjusted for steps of
               size cband, and is inappropriate for a single fractional colour
               step. The principle is that the colour value falls at the centre
               of the band. In the case that we are altering just one colour
               value at a time, we should set the initial phase difference to
               be half of the length per step. */
            channel->nxs = channel->xps ;
            DIVIDE_DDA_2(channel->nxs, adx, maxband) ;

            /* Calculate X phase shift per Y. There are two methods for
               tracking the current error; one uses the colour error fraction,
               the other uses the phase difference from the next band. In this
               case we use a combination of both to adjust the convergence in
               Y. We have calculated the colour error version above, we now
               split it into two components, an integral part and a fractional
               phase error. The phase tracking method ensures that
               0 <= xpy < xps, so cey may need to be adjusted if xpy would be
               negative. */
            if ( channel->cey.cfh != 0 || channel->cey.cfl != 0 ) {
              /* X per Y is the fraction of a colourvalue changed per Y */
              channel->xpy.ci = 0 ;
              channel->xpy.cfh = channel->cey.cfh ;
              channel->xpy.cfl = channel->cey.cfl ;

              if ( adx_sign * adz_sign < 0 ) {
                channel->xpy.ci = -1 ;
                NEGATE_DDA(channel->xpy, adz, maxband) ;
                channel->cey.ci += 1 ;
                HQASSERT(channel->cey.ci <= 0, "Y colour error out of range") ;
              }

              /* Changing basis from (adz,maxband) to (adx,maxband) is the same
                 as multiplying by adz/adx, which is x per colour. Since xpy
                 was already normalised to less than 1, this result must be
                 less than x per step. */
              CHANGE_DDA_BASIS(channel->xpy, adz, adx, maxband) ;

              /* Colour change per Y is integral number of colourvalues */
              channel->cey.cfh = 0 ;
              channel->cey.cfl = 0 ;

              /* If either removing the positive fractional part or adjusting
                 the Y error for the step gradient moved it out of range,
                 re-normalise the quantised step and error. We only need to test
                 on the integral component of the error because its fractional
                 components are zero. */
              if ( channel->cey.ci < -channel->cband.ci ||
                   (channel->cey.ci == -channel->cband.ci &&
                    (channel->cband.cfh|channel->cband.cfl) == 0) ) {
                SUBTRACT_DDA_1(channel->cqy, channel->cband, adz, maxband) ;
                ADD_DDA_1(channel->cey, channel->cband, adz, maxband) ;
              }
            } else {
              INITIALISE_DDA_I(channel->xpy, 0) ;
            }
          } else {
            /* Oops, this shading is so flat that we can't represent it even
               using just one fractional colour step at a time. Pretend there
               was no colour change in horizontal. */
            INITIALISE_DDA_I(channel->cqx, 0) ;
            INITIALISE_DDA_I(channel->cex, 0) ;
            INITIALISE_DDA_I(channel->nxs, xmax - icoords[0] + 1) ;
            INITIALISE_DDA_I(channel->xps, xmax - xmin + 1) ;
            INITIALISE_DDA_I(channel->xpy, 0) ;
          }
        }

        dda_assertions(gspan, channel) ;

        if ( gspan->noise > 0.0f ) {
          /* If antialias noise addition is in effect, use square pixel blocks
             of an appropriate size for the ShadingAntiAliasSize. Update
             DDAs to centre of start coordinate's rounding block, and then
             multiply X and Y error fractions by size of block. */
          int32 j ;
          int32 xn = (gspan->noisemask >> 1) - (gspan->last_x & gspan->noisemask) ;
          int32 yn = (gspan->noisemask >> 1) - (gspan->last_y & gspan->noisemask) ;

          if ( xn != 0 || yn != 0 ) {
            update_gspan_scanline(channel, yn, xn, gspan->adz) ;
            dda_assertions(gspan, channel) ;
          }

          INITIALISE_DDA_I(channel->nxs, 1) ;
          INITIALISE_DDA_I(channel->xps, 1) ;
          INITIALISE_DDA_I(channel->xpy, 0) ;

          for ( j = 0 ; j < gspan->noiseshift ; ++j ) {
            shfill_dda_t ceq ;

            ceq = channel->cband ;
            MULTIPLY_DDA_2(channel->cqx, adz, maxband) ;
            MULTIPLY_DDA_2(channel->cex, adz, maxband) ;
            ADD_DDA_1(ceq, channel->cex, adz, maxband) ;
            if ( ceq.ci < 0 || (ceq.ci|ceq.cfh|ceq.cfl) == 0 ) {
              channel->cex = ceq ;
              SUBTRACT_DDA_1(channel->cqx, channel->cband, adz, maxband) ;
            }

            ceq = channel->cband ;
            MULTIPLY_DDA_2(channel->cqy, adz, maxband) ;
            MULTIPLY_DDA_2(channel->cey, adz, maxband) ;
            ADD_DDA_1(ceq, channel->cey, adz, maxband) ;
            if ( ceq.ci < 0 || (ceq.ci|ceq.cfh|ceq.cfl) == 0 ) {
              channel->cey = ceq ;
              SUBTRACT_DDA_1(channel->cqy, channel->cband, adz, maxband) ;
            }
          }

          dda_assertions(gspan, channel) ;
        }

        /*
         * adx_sign * adx * x + ady_sign * ady * y + adz_sign * adz * color
         * = constant.
         */
#ifdef EXPENSIVE_TESTS
        channel->cross_x = 2 * adz_sign * adx_sign * adx ;
        channel->cross_y = 2 * adz_sign * ady_sign * ady ;
        channel->cross_z = 2 * adz ;
        channel->plane_const = channel->cross_x * icoords[0]
          + channel->cross_y * icoords[1]
          + adz * (2 * c0 + 1) ;

        HQASSERT( channel->cross_x * icoords[2]
                  + channel->cross_y * icoords[3]
                  + adz * (2 * c1 + 1)
                  == channel->plane_const
                  &&
                  channel->cross_x * icoords[4]
                  + channel->cross_y * icoords[5]
                  + adz * (2 * c2 + 1)
                  == channel->plane_const,
                  "Calculated wrong plane equation" ) ;
#endif
      } /* if channel present */
    } /* for channels */
    HQASSERT(index == gspan->nchannels, "Number of channels mismatched") ;

    if ( gspan->noise > 0.0f ) {
      gspan->last_x >>= gspan->noiseshift ;
      gspan->last_y >>= gspan->noiseshift ;
    }

    /* Push appropriate span functions on the blit stack. */

    gblit = &surface->gouraudinterpolateblits[BLT_GOUR_SMOOTH] ;
    if ( gspan->noise > 0.0f )
      gblit = &surface->gouraudinterpolateblits[BLT_GOUR_NOISE] ;

    SET_BLITS(rinfo.rb.blits, GOURAUD_BASE_BLIT_INDEX,
              &surface->gouraudbaseblits,
              &surface->gouraudbaseblits,
              &surface->gouraudbaseblits) ;
    SET_BLITS(rinfo.rb.blits, GOURAUD_BLIT_INDEX,
              gblit, gblit, gblit) ;
  } /* End of not-knocking-out condition */

  if ( gfill->screened ) {
    if ( gspan->nchannels != 0 ||
         (blit_quantise_state(color) & blit_quantise_mid) != 0 ) {
      /* Either halftone knockout or varying shade; lock the halftone */
      HQASSERT(!HT_PARAMS_DEGENERATE(rinfo.ht_params),
               "Using a degenerate screen");
      LOCK_HALFTONE(rinfo.ht_params);
      halftone_locked = TRUE ;
    }
  }

  /* Do the fill. */
  scanconvert_band(&rinfo.rb, &gfill->tnfill, NZFILL_TYPE);

  /* Reset gouraud blits to next, so background rectfill will work. This
     won't be so nasty when the gouraud object is reworked to not use an
     HDL, the background will be blitted as an separate rectfill. */
  SET_BLITS(rinfo.rb.blits, GOURAUD_BASE_BLIT_INDEX,
            &next_slice, &next_slice, &next_slice) ;
  SET_BLITS(rinfo.rb.blits, GOURAUD_BLIT_INDEX,
            &next_slice, &next_slice, &next_slice) ;

  if (halftone_locked)
    UNLOCK_HALFTONE(rinfo.ht_params);

  if (workspace_locked)
    GOURAUD_LOCK_RELEASE();

  return TRUE;
}


/* Render a gouraud shaded triangle object, by extracting points from
   non-linear subdivisions to find linear sub-triangles */
static Bool render_gouraud_sub(Hq32x2 coords[6], uint32 fraction,
                               p_ncolor_t colors[3],
                               GOURAUD_FILL *gfill,
                               GOURAUDOBJECT *gour)
{
  uintptr_t mask ;

  HQASSERT(gour, "No gour argument to render_gouraud_sub") ;
  HQASSERT(gfill, "No gfill argument to render_gouraud_sub") ;
  HQASSERT(gfill->bits >= 0 && gfill->bits <= sizeof(uintptr_t) * 8,
           "Strange number of bits left in flagptr") ;
  HQASSERT(fraction != 0 && (fraction & (fraction - 1)) == 0,
           "Fraction denominator is not a positive power of two") ;

  /* Maintain points in highest-lowest y order */
#if 0
  /* I'd like to use this assert, to reduce sorting when the linear
     interpolated bitblit is implemented, but it doesn't work because of the
     integer subdivision; for example, point 2 can be slightly above and
     right of point 3, but when subdivided the new points 2 and 3 are
     truncated to the same line, with the x-coordinates now out of order. */
  HQASSERT((coords[1] < coords[3] ||
            (coords[1] == coords[3] && coords[0] <= coords[2])) &&
           (coords[3] < coords[5] ||
            (coords[3] == coords[5] && coords[2] <= coords[4])),
           "Triangle points out of order in render_gouraud_sub") ;
  /* Weaker assert follows: */
#endif
  HQASSERT((coords[1].high < coords[3].high ||
            (coords[1].high == coords[3].high &&
             coords[1].low <= coords[3].low)) &&
           (coords[3].high < coords[5].high ||
            (coords[3].high == coords[5].high &&
             coords[3].low <= coords[5].low)),
           "Triangle points out of order in render_gouraud_sub") ;

  SwOftenUnsafe();
  if ( !interrupts_clear(allow_interrupt) )
    return report_interrupt(allow_interrupt);

  if ( --gfill->bits < 0 ) { /* Get a new flag byte if no more bits */
    gfill->bits = sizeof(uintptr_t) * 8 - 1 ;
    gfill->flags = *((uintptr_t *)gfill->nextcolor) ;
    HQASSERT(sizeof(uintptr_t) == sizeof(p_ncolor_t),
             "p_ncolor_t not the same as flags") ;
    gfill->nextcolor += 1 ;
  }
  mask = (uintptr_t)1 << gfill->bits ;

  if ( gfill->flags & mask ) { /* Subdivide and try again */
    Hq32x2 scoords[6] ;
    p_ncolor_t vcolors[3], scolors[3] ;
    int32 i, j ;

    /* Extract in-between colours into array. These are extracted in such an
       order that adding the vertex indices and exclusive-oring with 3 gives
       the right colour index. i.e., v01 => vcolors[2], v12 => vcolors[0],
       and v20 => vcolors[1] */
    dl_copy_weak(&vcolors[2], gfill->nextcolor[0]);
    dl_copy_weak(&vcolors[0], gfill->nextcolor[1]);
    dl_copy_weak(&vcolors[1], gfill->nextcolor[2]);

    gfill->nextcolor += 3 ;

    /* Preserve Y-ordering when subdividing. This loop does the first three
       triangles. Note that (v0 + v0)/2 == v0 is used to set the v0, v1, and
       v2, elements. */
    /* v0, v01, v20 */
    /* v01, v1, v12 */
    /* v20, v12, v2 */
    for ( j = 0 ; j < 3 ; ++j ) {
      int32 j2 = j + j ;
      scoords[j2].high = coords[j2].high ;
      scoords[j2].low = coords[j2].low << 1 ;
      scoords[j2 + 1].high = coords[j2 + 1].high ;
      scoords[j2 + 1].low = coords[j2 + 1].low << 1 ;
      dl_copy_weak(&scolors[j], colors[j]);
      for ( i = (j + 1) % 3 ; i != j ; i = (i + 1) % 3 ) {
        int32 i2 = i + i ;
        GOURAUD_BISECT(&scoords[i2], &coords[i2], &coords[j2], fraction) ;
        GOURAUD_BISECT(&scoords[i2 + 1], &coords[i2 + 1], &coords[j2 + 1], fraction) ;
        dl_copy_weak(&scolors[i], vcolors[(j + i) ^ 3]) ;
      }
      if ( !render_gouraud_sub(scoords, fraction << 1, scolors, gfill, gour) )
        return FALSE ;
    }
    /* v01, v20, v12 */
    for ( i = j = 0 ; i < 3 ; ++i, j = (j + 2) % 3 ) {
      int32 k = (j + 1) % 3, i2 = i + i, j2 = j + j, k2 = k + k ;
      GOURAUD_BISECT(&scoords[i2], &coords[j2], &coords[k2], fraction) ;
      GOURAUD_BISECT(&scoords[i2 + 1], &coords[j2 + 1], &coords[k2 + 1], fraction) ;
      dl_copy_weak(&scolors[i], vcolors[(j + k) ^ 3]);
    }
    if ( !render_gouraud_sub(scoords, fraction << 1, scolors, gfill, gour) )
      return FALSE ;
  } else { /* Linear interpolation */
    if ( !render_gouraud_blit(coords, colors, gfill) )
      return FALSE ;
  }
  return TRUE ;
}

Bool render_gouraud(render_info_t *p_ri, Bool screened)
{
  LISTOBJECT *lobj = p_ri->lobj ;
  GOURAUDOBJECT *gour = (GOURAUDOBJECT *)load_dldata(lobj);
  GOURAUD_FILL gfill ;
  p_ncolor_t *p_ncolors ;
  int32 index ;
  Hq32x2 coords[6] ;

  p_ncolors = (p_ncolor_t *)(gour + 1) ;

  init_listobject(&(gfill.tlobj), RENDER_gouraud, NULL);
  gfill.tlobj = *lobj ; /* Copy all fields of Gouraud, then override some. */
  gfill.tlobj.dldata.nfill = &(gfill.tnfill);

  HQASSERT(ST_NTHREADS >= 3, "Nfill on stack not big enough");
  for ( index = 0 ; index < 6 ; index++ ) {
    NBRESS *nbress = &(gfill.tnbress[index]);

    gfill.tnfill.thread[index] = nbress;
    dxylist_init(&(nbress->dxy));
    nbress->u1.next = NULL ;
    nbress->norient = (uint8)(index==0);
  }
  gfill.tnfill.rcbtrap = NULL ;

  /* Invariant fill and non-recursively preserved information, passed as
     struct to reduce arg passing. */
  gfill.p_ri = p_ri ;
  gfill.screened = screened ;
  gfill.flags = gour->flags ;
  gfill.bits = sizeof(uintptr_t) * 8 ;
  gfill.nextcolor = p_ncolors + 3 ;
  gfill.linear_triangles = 0 ;

  /* Add 0.5 rounding fraction to the initial coordinate. Bisection preserves
     the magnitude of the rounding fraction, since it is added to both
     coordinates involved in the bisection. The initial denominator is 2, so
     the initial rounding fraction is 1. */
  for ( index = 0 ; index < 6 ; ++index ) {
    coords[index].high = gour->coords[index] ;
    coords[index].low = 1 ;
  }

  if ( !render_gouraud_sub(coords, 2u, p_ncolors, &gfill, gour) )
    return FALSE ;

#if defined( ASSERT_BUILD )
  HQASSERT(gfill.linear_triangles == *((int32 *)gfill.nextcolor),
           "Mismatch in number of triangles decomposed and rendered") ;
#endif
  return TRUE ;
} /* Function render_gouraud_all */


/* Prepare to render, and then render an HDL. */
Bool render_shfill_all(render_info_t *p_ri, Bool screened,
                       HDL *hdl, channel_index_t nchannels, Bool transparency)
{
  SHADINGOBJECT *sobj = p_ri->lobj->dldata.shade;
  Bool result ;
  GOURAUD_CONTEXT gspan ;
  Bool intersecting = FALSE ;
  blit_color_t *color = p_ri->rb.color ;

  UNUSED_PARAM(Bool, screened);
  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT(color->valid & blit_color_unpacked, "Color not unpacked yet") ;
  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Screen has not been set for color") ;

  /* If transparency is present, we need to do something about it. Determine
     if we're going to use the self-intersecting blits and reversed
     Z-order for the sub-DL. */
  if ( transparency ) {
    DLREF *subdl = hdlOrderList(hdl) ;

    HQASSERT(subdl, "No objects on shfill DL") ;

    /* Self-intersection tests are very conservative at the moment. We know
       we cannot allow overlaps, so we will allow painter's algorithm if
       there is a single non-decomposed Gouraud on the sub-dl.  Also allow
       painter's if just the rect background object is present (no Gourauds). */
    if ( dlref_next(subdl) ||
         ( dlref_lobj(subdl)->opcode == RENDER_gouraud &&
           dlref_lobj(subdl)->dldata.gouraud->flags != 0 ) )
      intersecting = TRUE ;
  }

  gspan.colorWorkspace = sobj->colorWorkspace;
  gspan.mbands = sobj->mbands;
  gspan.noise = sobj->noise * COLORVALUE_MAX / 65536.0f ;
  gspan.noiseshift = sobj->noisesize;
  gspan.noisemask = (uint16)((1u << sobj->noisesize) - 1) ;
  gspan.nchannels = nchannels;
  HQASSERT(nchannels <= (channel_index_t)sobj->nchannels, "colorWorkspace buffer overrun");

  /* Object label and rendering intent should be the same for all shfill sub-
     objects. */
  gspan.object_label = pixelLabelFromDisposition(p_ri->lobj->disposition) ;
  gspan.lca = p_ri->lobj->objectstate->lateColorAttrib;
  HQASSERT(DISPOSITION_REPRO_TYPE(p_ri->lobj->disposition) == REPRO_TYPE_VIGNETTE,
           "Non-vignette type for shill");
  gspan.knockout = ((p_ri->lobj->spflags & RENDER_KNOCKOUT) != 0) ;

  /* The screen of the shading doesn't change, so save the modular halftone
     selection state for use across all blit unpacking. */
  gspan.selected = mht_selected(p_ri,
                                color->quantised.spotno, REPRO_TYPE_VIGNETTE);

  gspan.varying_alpha = color->alpha != COLORVALUE_ONE;

  /* Initialise the corner color storage, based on the current color. These
     will be re-used for each triangle. We'll re-use the current color
     as the output color for the unpacked color values for each span. */
  blit_color_init(&gspan.colors[0], color->map) ;
  blit_quantise_set_screen(&gspan.colors[0],
                           color->quantised.spotno, REPRO_TYPE_VIGNETTE);
  blit_color_init(&gspan.colors[1], color->map) ;
  blit_quantise_set_screen(&gspan.colors[1],
                           color->quantised.spotno, REPRO_TYPE_VIGNETTE);
  blit_color_init(&gspan.colors[2], color->map) ;
  blit_quantise_set_screen(&gspan.colors[2],
                           color->quantised.spotno, REPRO_TYPE_VIGNETTE);

  NAME_OBJECT(&gspan, GOURAUD_CONTEXT_NAME) ;

  /* Set up pass-throughs for the gouraud blits, until such point as we set
     the real interpolations for each channel. This also allows us to set
     the blit data for the gouraud blits to the context structure. */
  SET_BLITS(p_ri->rb.blits, GOURAUD_BASE_BLIT_INDEX,
            &next_slice, &next_slice, &next_slice) ;
  SET_BLIT_DATA(p_ri->rb.blits, GOURAUD_BASE_BLIT_INDEX, &gspan) ;

  SET_BLITS(p_ri->rb.blits, GOURAUD_BLIT_INDEX,
            &next_slice, &next_slice, &next_slice) ;
  SET_BLIT_DATA(p_ri->rb.blits, GOURAUD_BLIT_INDEX, &gspan) ;

  result = hdlRender(hdl, p_ri, NULL, intersecting);

  CLEAR_BLITS(p_ri->rb.blits, GOURAUD_BLIT_INDEX) ;
  CLEAR_BLITS(p_ri->rb.blits, GOURAUD_BASE_BLIT_INDEX) ;

  UNNAME_OBJECT(&gspan) ;

  return result ;
}


void gouraud_init(void)
{
  /* Don't instrument the gouraud lock, it's rather too much high-frequency
     noise. */
  multi_rwlock_init(&gouraud_lock, GOURAUD_LOCK_INDEX,
                    SW_TRACE_GOURAUD_ACQUIRE, SW_TRACE_GOURAUD_READ_HOLD,
                    SW_TRACE_GOURAUD_WRITE_HOLD);
  gouraud_lock_init = TRUE;

  if ( gspan_noise_table_x[0] == 0 ) {
    /* Initialise noise table with a different PNRG than the one used in
       GENERATE_NOISE */
    uint32 i, seed = 1183615869 ;
    for ( i = 0 ; i <= NOISE_TABLE_MAX ; ++i ) {
      seed = seed * 1103515245 + 12345 ;
      gspan_noise_table_x[i] = seed ;
    }
    for ( i = 0 ; i <= NOISE_TABLE_MAX ; ++i ) {
      seed = seed * 1103515245 + 12345 ;
      gspan_noise_table_y[i] = seed ;
    }
    HQASSERT(gspan_noise_table_x[0] != 0, "Noise table initialisation failed") ;
  }
}


void gouraud_finish(void)
{
  if (gouraud_lock_init) {
    multi_rwlock_finish(&gouraud_lock);
    gouraud_lock_init = FALSE;
  }
}


/* Log stripped */
