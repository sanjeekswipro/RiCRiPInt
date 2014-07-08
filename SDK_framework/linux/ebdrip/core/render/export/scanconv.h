/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!export:scanconv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions and structures for scan conversion. This is a part of the
 * CORErender interface.
 */

#ifndef __SCANCONV_H__
#define __SCANCONV_H__

/** \defgroup scanconvert Scan Conversion
    \ingroup rendering */
/** \{ */

struct render_blit_t ; /* from SWv20 */
struct NFILLOBJECT ; /* from SWv20 */

/*---------------------------------------------------------------------------*/
/* DDA scan conversion macros. These are exported because the NFILLOBJECT
   structure preset initialisation and band/clip update uses them. */

/** Initialise a scan-conversion DDA from the dx,dy steps. */
#define DDA_SCAN_INITIALISE(nbress_, dx_, dy_) MACRO_START \
  register NBRESS *_nbress_ = (nbress_) ; \
  register dcoord _dx_ = (dx_), _dy_ = (dy_) ; \
  register dcoord _denom_ = _dy_  + _dy_ ; \
  HQASSERT(_dy_ >= 0, "Scan conversion line segment inverted") ; \
  _nbress_->denom = _denom_ ; \
  _nbress_->xe = _dy_ ; \
  _nbress_->nmindy = _denom_ ; \
  _nbress_->ntype = FALSE ; \
  if ( _dy_ > 0 ) { /* Initialise step to dx/dy, the gradient */ \
    if ( _dx_ >= _denom_ ) { /* [2dy,... */ \
      _nbress_->si = _dx_ / _dy_ ; \
      _nbress_->sf = (_dx_ - _nbress_->si * _dy_) << 1 ; \
    } else if ( -_dx_ > _denom_ ) { /* ...,-2dy) */ \
      _nbress_->si = (_dx_ - _dy_ + 1) / _dy_ ; /* floor(dx/denom) */ \
      _nbress_->sf = (_dx_ - _nbress_->si * _dy_) << 1 ; \
    } else if ( _dx_ >= _dy_ ) { /* [dy,2dy) */ \
      _nbress_->si = 1 ; \
      _nbress_->sf = (_dx_ - _dy_) << 1 ; \
    } else if ( -_dx_ > _dy_ ) { /* [-2dy,-dy) */ \
      _nbress_->si = -2 ; \
      _nbress_->sf = (_dx_ + _denom_) << 1 ; \
    } else if ( _dx_ >= 0 ) { /* [0,dy) */ \
      _nbress_->si = 0 ; \
      _nbress_->sf = _dx_ << 1 ; \
      _nbress_->ntype = TRUE ; \
    } else { /* [-dy,0) */ \
      _nbress_->si = -1 ; \
      _nbress_->sf = (_dx_ + _dy_) << 1 ; \
      _nbress_->ntype = TRUE ; \
    } \
    HQASSERT(_nbress_->si * _denom_ + _nbress_->sf == _dx_ + _dx_, \
             "Initial DDA wrong") ; \
    HQASSERT(0 <= _nbress_->sf && _nbress_->sf < _denom_, \
             "Initial DDA gradient fraction invalid") ; \
  } else { /* Horizontal line segment */ \
    _nbress_->si = _dx_ ; \
    _nbress_->sf = 0 ; \
  } \
MACRO_END

/** Normalise a scan-conversion DDA to the correct range, assuming the
    fractional part has just been stepped. */
#define DDA_SCAN_NORMALISE(denom_, ddai_, ddaf_) MACRO_START \
  HQASSERT((denom_) > 0 && ((denom_) << 1) > 0, \
           "Scan conversion DDA denominator invalid") ; \
  if ( (ddaf_) >= (denom_) ) { \
    (ddaf_) -= (denom_) ; \
    (ddai_) += 1 ; \
  } \
  HQASSERT((ddaf_) >= 0 && (ddaf_) < (denom_), \
           "Scan conversion DDA normalise fraction invalid") ; \
MACRO_END

/** Step a scan-conversion DDA by a number of steps. Tests are arranged so that
    as few wasted shifts and normalisations as possible are performed. */
#define DDA_SCAN_STEP_N(nbress_, nsteps_) MACRO_START \
  register NBRESS *_nbress_ = (nbress_) ; \
  register dcoord _si_, _sf_ ; \
  register dcoord _n_ = (nsteps_) ; \
  dcoord _denom_ = _nbress_->denom ; \
  _nbress_->nmindy -= _n_ + _n_ ; \
  HQASSERT(_nbress_->nmindy >= 0, "DDA stepped too many lines") ; \
  _si_ = _nbress_->si ; \
  _sf_ = _nbress_->sf ; \
  do { \
    HQASSERT(_n_ > 0, "DDA loop ran out of scanlines") ; \
    while ( (_n_ & 1) == 0 ) { \
      _si_ += _si_ ; \
      _sf_ += _sf_ ; \
      DDA_SCAN_NORMALISE(_denom_, _si_, _sf_) ; \
      _n_ >>= 1 ; \
    } \
    _nbress_->u1.ncx += _si_ ; \
    _nbress_->xe += _sf_ ; \
    DDA_SCAN_NORMALISE(_denom_, _nbress_->u1.ncx, _nbress_->xe) ; \
  } while ( --_n_ != 0 ) ; \
MACRO_END

/*---------------------------------------------------------------------------*/
/* Sub-pixel resolution centre-pixel scan conversion for characters. */

/** Defines the scale for character subpixel scan conversion. Six bits means
    curves are flattened at 64 times the normal resolution. */
#define AR_BITS      6
#define AR_FACTOR    (1 << AR_BITS)
#define AR_FRACBITS  (AR_FACTOR - 1)

/** This is the max pixel size we allow accurate rendering with. Values above
   this indicate accurate rendering is always on. */
#define AR_MAXSIZE (MAXINT32)

/* Scan conversion for small characters, using whatever blits are set up on
   the blit stack. Currently assumes that bitfill1 will be used, outputting
   to a bitmap form (outputform), and does not permit rendering of clipping
   mask. */
enum { /* Flags for scan conversion */
  SC_FLAG_ACCURATE = 1, /**< Use sub-pixel rendering algorithm. */
  SC_FLAG_TWOPASS = 2,  /**< Use two-pass rendering algorithm. */
  SC_FLAG_SWAPXY = 4    /**< X/Y swapped in NFILLOBJECT for vertical render. */
} ;
void scanconvert_char(struct render_blit_t *rb,
                      struct NFILLOBJECT *nfill,
                      int32 therule, uint32 flags);

/*---------------------------------------------------------------------------*/
/** Identity scale pixel rendering with dispatch to rule stored in
    NFILLOBJECT.. */
void scanconvert_band(struct render_blit_t *rb,
                      struct NFILLOBJECT *nfill,
                      int32 therule);

/** \} */

#endif /* __SCANCONV_H__ */

/* Log stripped */
