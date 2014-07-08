/** \file
 * \ingroup rendering
 *
 * $HopeName$
 *
 * Copyright (C) 2009-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * DDAs for converting image space to device space.
 *
 * Image rendering uses three-part DDAs to get a precise mapping from image
 * space to device space. The corner points of any image source space location
 * can be exactly transformed to device space. The inverse transform from
 * device space to image space is not exact.
 *
 * The macros here are written so that they can be replaced by functions if
 * desired. This means that the output parameters use pass by reference,
 * however DDAs and bases only used for input are passed by value.
 */

#ifndef __IMAGEDDA_H__
#define __IMAGEDDA_H__

/** \ingroup rendering */
/** \{ */

#if defined(HQN_INT64_OP_ADDSUB)

/** Image DDA basis. This structure describes the denominators of the image
    space DDA such that any integral point in image space can be converted
    to device space precisely. */
typedef struct image_dda_basis_t {
  int64 dhl ;      /**< Denominator is width*height */
  int32 maxsteps ; /**< Maximum number of steps in one go. */
  int32 w, h ;     /**< Width, height for initialising DDAs as ratios. */
} image_dda_basis_t ;

/** Image DDA position or delta. */
typedef struct image_dda_t {
  int64 fhl ; /**< Fractional part of DDA 0 <= fhl < dhl */
  int32 i ;  /**< Integral part of DDA */
} image_dda_t ;

#define IMAGE_DDA_ASSERT(ddaa_, basis_) MACRO_START \
  HQASSERT(0 <= (ddaa_)->fhl && (ddaa_)->fhl < (basis_).dhl && \
           (basis_).dhl == (int64)(basis_).w * (int64)(basis_).h,       \
           "Image DDA fraction invalid") ; \
MACRO_END

/** Initialise an image DDA basis, calculating the maximum number of steps
    that can be done using multiplication.

    Since 0 <= fhl < dhl, we have maxsteps * fhl <= MAXINT64, so
    max steps in fhl is MAXINT64/dhl.

    The odd case of w = h = 1 would result in this calculation returning 0,
    so we also limit the intermediate result such that the number of steps
    must remain within the image boundary. The maximum number of steps that
    may be taken is max(w, h).
*/
#define IMAGE_DDA_BASIS(basis_, w_, h_) MACRO_START \
  image_dda_basis_t *_basis_ = (basis_) ; \
  int32 _maxwh_ ; \
  int64 _maxsteps_ ; \
  HQASSERT((w_) > 0 && ((int32)(w_) << 1) > 0, \
           "Image DDA high denominator invalid") ; \
  HQASSERT((h_) > 0 && ((int32)(h_) << 1) > 0, \
           "Image DDA low denominator invalid") ; \
  _basis_->w = (w_) ; \
  _basis_->h = (h_) ; \
  _basis_->dhl = (int64)_basis_->w * (int64)_basis_->h; \
  INLINE_MAX32(_maxwh_, _basis_->w, _basis_->h) ; \
  _maxsteps_ = MAXINT64 / _basis_->dhl ; \
  _maxsteps_ = min(_maxsteps_, _maxwh_) ; \
  _basis_->maxsteps = CAST_SIGNED_TO_INT32(_maxsteps_) ; \
  HQASSERT(_basis_->maxsteps > 0, "Image DDA maxsteps is zero") ; \
MACRO_END

/** Initialise an image DDA as a ratio to the image width. */
#define IMAGE_DDA_INITIALISE_W(ddai_, basis_, dxy_) MACRO_START \
  register int64 _dxy_ = (int64)(dxy_) * (basis_).h ; \
  image_dda_t *_dda_ = (ddai_) ; \
  int64 _dhl_ = (basis_).dhl ; \
  HQASSERT(_dhl_ > 0, "DDA basis must be positive") ; \
  if ( _dxy_ >= _dhl_ ) { /* [dhl,... */ \
    _dda_->i = CAST_SIGNED_TO_INT32(_dxy_ / _dhl_) ; \
    _dda_->fhl = _dxy_ - _dda_->i * _dhl_ ; \
  } else if ( -_dxy_ > _dhl_ ) { /* ...,-dhl) */ \
    _dda_->i = CAST_SIGNED_TO_INT32((_dxy_ - _dhl_ + 1) / _dhl_) ; /* floor(dxy/dhl) */ \
    _dda_->fhl = _dxy_ - _dda_->i * _dhl_ ; \
  } else if ( _dxy_ >= 0 ) { /* [0,dhl) */ \
    _dda_->i = 0 ; \
    _dda_->fhl = _dxy_ ; \
  } else { /* [-dhl,0) */ \
    _dda_->i = -1 ; \
    _dda_->fhl = _dxy_ + _dhl_ ; \
  } \
  HQASSERT(_dda_->i * _dhl_ + _dda_->fhl == _dxy_, "Initial DDA wrong") ; \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Initialise an image DDA as a ratio to the image height. */
#define IMAGE_DDA_INITIALISE_H(ddai_, basis_, dxy_) MACRO_START \
  register int64 _dxy_ = (int64)(dxy_) * (basis_).w ; \
  image_dda_t *_dda_ = (ddai_) ; \
  int64 _dhl_ = (basis_).dhl ; \
  HQASSERT(_dhl_ > 0, "DDA basis must be positive") ; \
  if ( _dxy_ >= _dhl_ ) { /* [dhl,... */ \
    _dda_->i = CAST_SIGNED_TO_INT32(_dxy_ / _dhl_) ; \
    _dda_->fhl = _dxy_ - _dda_->i * _dhl_ ;       \
  } else if ( -_dxy_ > _dhl_ ) { /* ...,-dhl) */ \
    _dda_->i = CAST_SIGNED_TO_INT32((_dxy_ - _dhl_ + 1) / _dhl_) ; /* floor(dxy/dhl) */ \
    _dda_->fhl = _dxy_ - _dda_->i * _dhl_ ; \
  } else if ( _dxy_ >= 0 ) { /* [0,dhl) */ \
    _dda_->i = 0 ; \
    _dda_->fhl = _dxy_ ; \
  } else { /* [-dhl,0) */ \
    _dda_->i = -1 ; \
    _dda_->fhl = _dxy_ + _dhl_ ; \
  } \
  HQASSERT(_dda_->i * _dhl_ + _dda_->fhl == _dxy_, "Initial DDA wrong") ; \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Initialise an image DDA from a real value. */
#define IMAGE_DDA_INITIALISE_R(ddai_, basis_, dxy_) MACRO_START \
  image_dda_t *_dda_ = (ddai_) ; \
  double _dxy_ = (double)(dxy_) ; \
  _dda_->i = (int32)floor(_dxy_) ; \
  _dxy_ = (_dxy_ - _dda_->i) * (basis_).dhl ; \
  _dda_->fhl = (int64)floor(_dxy_) ; \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Initialise an image DDA to zero. */
#define IMAGE_DDA_INITIALISE_0(ddai_) MACRO_START \
  (ddai_)->i = 0 ; \
  (ddai_)->fhl = 0 ; \
MACRO_END

/** Initialise an image DDA to the minimum value possible. */
#define IMAGE_DDA_INITIALISE_EPSILON(ddai_) MACRO_START \
  (ddai_)->i = 0 ; \
  (ddai_)->fhl = 1 ; \
MACRO_END

/** Return the integer floor of an image DDA. */
#define IMAGE_DDA_FLOOR(ddai_) ((ddai_).i)

/** Return the integer ceiling of an image DDA. */
#define IMAGE_DDA_CEIL(ddai_) ((ddai_).i + ((ddai_).fhl > 0))

/** Initialise an image DDA from a coordinate value. */
#define IMAGE_DDA_INITIALISE_XY(ddai_, basis_, xy_) MACRO_START \
  register int32 _xy_ = (xy_) ; \
  image_dda_t *_dda_ = (ddai_) ; \
  HQASSERT((basis_).dhl > 0, "DDA basis must be positive") ; \
  _dda_->i = _xy_ ; \
  _dda_->fhl = 0 ; \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Initialise an image DDA from a coordinate value, plus 0.5. */
#define IMAGE_DDA_INITIALISE_XY_HALF(ddai_, basis_, xy_) MACRO_START \
  register int32 _xy_ = (xy_) ; \
  image_dda_t *_dda_ = (ddai_) ; \
  HQASSERT((basis_).dhl > 0, "DDA basis must be positive") ; \
  _dda_->i = _xy_ ; \
  _dda_->fhl = (basis_).dhl >> 1 ; \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Normalise an image DDA to the correct range, assuming the fractional part
    has just been stepped. */
#define IMAGE_DDA_NORMALISE_1(ddan_, basis_) MACRO_START \
  int64 _diff_ ; \
  HQASSERT((basis_).dhl > 0, "DDA basis must be positive") ; \
  _diff_ = (ddan_)->fhl - (basis_).dhl ; \
  if ( _diff_ >= 0 ) { \
    (ddan_)->fhl = _diff_ ; \
    (ddan_)->i += 1 ; \
  } \
  IMAGE_DDA_ASSERT((ddan_), (basis_)) ; \
MACRO_END

/** Step an image DDA by one step. The output DDA may be the same as the
    input DDA. */
#define IMAGE_DDA_STEP_1(ddain_, step_, basis_, ddaout_) MACRO_START \
  HQASSERT((basis_).dhl > 0, "DDA basis must be positive") ; \
  (ddaout_)->i = (ddain_).i + (step_).i ; \
  (ddaout_)->fhl = (ddain_).fhl + (step_).fhl ; \
  IMAGE_DDA_NORMALISE_1((ddaout_), (basis_)) ; \
MACRO_END

/** Normalise an image DDA to the correct range, assuming the fractional part
    has just been stepped by multiple steps. */
#define IMAGE_DDA_NORMALISE_N(ddan_, basis_) MACRO_START \
  HQASSERT((basis_).dhl > 0, "DDA basis must be positive") ; \
  if ( (ddan_)->fhl >= (basis_).dhl ) { \
    int64 _diff_ = (ddan_)->fhl / (basis_).dhl ; \
    (ddan_)->fhl -= _diff_ * (basis_).dhl ; \
    (ddan_)->i += CAST_SIGNED_TO_INT32(_diff_) ; \
  } \
  IMAGE_DDA_ASSERT((ddan_), (basis_)) ; \
MACRO_END

/** Step an image DDA by a number of steps, using multiplication by up to the
    DDA's maxsteps and division for normalisation. The output DDA may be the
    same as the input DDA. */
#define IMAGE_DDA_STEP_N(ddain_, step_, basis_, nsteps_, ddaout_) MACRO_START \
  image_dda_t _temp_ = (ddain_) ; \
  register int32 _n_ = (nsteps_) ; \
  HQASSERT(_n_ >= 0, "Cannot step image DDA in reverse") ; \
  while ( _n_ != 0 ) { \
    int32 _mul_ ; \
    HQASSERT(_n_ > 0, "DDA loop ran out of steps") ; \
    INLINE_MIN32(_mul_, _n_, (basis_).maxsteps) ; \
    _temp_.i += (step_).i * _mul_ ; \
    _temp_.fhl += (step_).fhl * _mul_ ; \
    IMAGE_DDA_NORMALISE_N(&_temp_, (basis_)) ; \
    _n_ -= _mul_ ; \
  } \
  IMAGE_DDA_ASSERT(&_temp_, (basis_)) ; \
  *(ddaout_) = _temp_ ; \
MACRO_END

/** Step an image DDA by a number of steps, without multiplication or
    division operations. Tests are arranged so that as few wasted shifts and
    normalisations as possible are performed, and a single step is optimised. */
#define IMAGE_DDA_STEP_N_LG2(dda_, step_, basis_, nsteps_) MACRO_START \
  register int32 _n_ = (nsteps_) ; \
  HQASSERT(_n_ >= 0, "Cannot step image DDA in reverse") ; \
  if ( (_n_ & 1) != 0 ) { \
    IMAGE_DDA_STEP_1((dda_), (step_), (basis_), &(dda_)) ; \
  } \
  if ( _n_ > 1 ) { \
    image_dda_t _power_ ; \
    _power_.i = (step_).i << 1 ; \
    _power_.fhl = (step_).fhl << 1 ; \
    for (;;) { /* Power DDA is not yet normalised */ \
      HQASSERT(_n_ > 1, "Failed to handle previous DDA multiplier") ; \
      IMAGE_DDA_NORMALISE_1(&_power_, (basis_)) ; \
      _n_ >>= 1 ; \
      if ( (_n_ & 1) != 0 ) { \
        IMAGE_DDA_STEP_1((dda_), _power_, (basis_), &(dda_)) ; \
        if ( _n_ == 1 ) \
          break ; \
      } \
      _power_.i <<= 1 ; \
      _power_.fhl <<= 1 ; \
    } \
  } \
MACRO_END

/** Negate an image DDA. Floor the negated value if there is a fractional
    part, with appropriate carries to keep the fractional part positive. */
#define IMAGE_DDA_NEGATE(ddain_, basis_, ddaout_) MACRO_START \
  register int64 _frac_ ; \
  register int32 _carry_ = 0 ; \
  HQASSERT((basis_).dhl > 0, "DDA basis must be positive") ; \
  _frac_ = (ddain_).fhl ; \
  if ( _frac_ != 0 ) { \
    _frac_ = (basis_).dhl - _frac_ ; \
    _carry_ = 1 ; \
  } \
  (ddaout_)->fhl = _frac_ ; \
  (ddaout_)->i = -(ddain_).i - _carry_ ; \
  IMAGE_DDA_ASSERT((ddaout_), (basis_)) ; \
MACRO_END

#else /* !HQN_INT64_OP_ADDSUB */

/** Image DDA basis. This structure describes the denominators of the image
    space DDA such that any integral point in image space can be converted
    to device space precisely. */
typedef struct image_dda_basis_t {
  int32 dh ;       /**< High fractional denominator is width */
  int32 dl ;       /**< Low fractional denominator is height */
  int32 maxsteps ; /**< Maximum number of steps in one go. */
} image_dda_basis_t ;

/** Image DDA position or delta. */
typedef struct image_dda_t {
  int32 fh ; /**< High fractional part of DDA 0 <= fh < dh */
  int32 fl ; /**< Low fractional part of DDA 0 <= fl < dl */
  int32 i ;  /**< Integral part of DDA */
} image_dda_t ;

/** Range checking asserts for image DDA. */
#define IMAGE_DDA_ASSERT(ddaa_, basis_) MACRO_START \
  HQASSERT(0 <= (ddaa_)->fh && (ddaa_)->fh < (basis_).dh, \
           "Image DDA high fraction invalid") ; \
  HQASSERT(0 <= (ddaa_)->fl && (ddaa_)->fl < (basis_).dl, \
           "Image DDA low fraction invalid") ; \
MACRO_END

/** Initialise an image DDA basis, calculating the maximum number of steps
    that can be done using multiplication.

    Since 0 <= fl < h, we have maxsteps * fl < maxsteps * h <= MAXINT32, so
    max steps in fl is MAXINT32/h.

    Similarly 0 <= fh < w, we have maxsteps * fh < maxsteps * w <= MAXINT32, so
    max steps in fh is MAXINT32/w. However, the IMAGE_DDA_NORMALISE_N()
    that follows IMAGE_DDA_STEP_N() may also add up to maxsteps to fh
    before it is brought back into range, so we use (MAXINT32-maxsteps_fl)/w.

    The odd case of w = h = 1 would result in this calculation returning 0,
    so we also limit the intermediate result such that the number of steps
    must remain within the image boundary. The maximum number of steps that
    may be taken is max(w, h).
*/
#define IMAGE_DDA_BASIS(basis_, w_, h_) MACRO_START \
  image_dda_basis_t *_basis_ = (basis_) ; \
  int32 _maxstepsl_,  _maxstepsh_, _maxwh_ ; \
  HQASSERT((w_) > 0 && ((int32)(w_) << 1) > 0, \
           "Image DDA high denominator invalid") ; \
  _basis_->dh = (w_) ; \
  HQASSERT((h_) > 0 && ((int32)(h_) << 1) > 0, \
           "Image DDA low denominator invalid") ; \
  _basis_->dl = (h_) ; \
  INLINE_MAX32(_maxwh_, _basis_->dl, _basis_->dh) ; \
  _maxstepsl_ = MAXINT32 / _basis_->dl ; \
  _maxstepsl_ = min(_maxstepsl_, _maxwh_) ; \
  _maxstepsh_ = (MAXINT32 - _maxstepsl_) / _basis_->dh ; \
  _basis_->maxsteps = min(_maxstepsl_, _maxstepsh_) ; \
  HQASSERT(_basis_->maxsteps > 0, "Image DDA maxsteps is zero") ; \
MACRO_END

/** Initialise an image DDA as a ratio to the image width. */
#define IMAGE_DDA_INITIALISE_W(ddai_, basis_, dxy_) MACRO_START \
  register int32 _dxy_ = (dxy_) ; \
  image_dda_t *_dda_ = (ddai_) ; \
  int32 _w_ = (basis_).dh ; \
  HQASSERT(_w_ > 0 && (basis_).dl > 0, "DDA basis must be positive") ; \
  if ( _dxy_ >= _w_ ) { /* [w,... */ \
    _dda_->i = _dxy_ / _w_ ; \
    _dda_->fh = _dxy_ - _dda_->i * _w_ ; \
  } else if ( -_dxy_ > _w_ ) { /* ...,-w) */ \
    _dda_->i = (_dxy_ - _w_ + 1) / _w_ ; /* floor(dxy/w) */  \
    _dda_->fh = _dxy_ - _dda_->i * _w_ ; \
  } else if ( _dxy_ >= 0 ) { /* [0,w) */ \
    _dda_->i = 0 ; \
    _dda_->fh = _dxy_ ; \
  } else { /* [-w,0) */ \
    _dda_->i = -1 ; \
    _dda_->fh = _dxy_ + _w_ ; \
  } \
  _dda_->fl = 0 ; \
  HQASSERT(_dda_->i * _w_ + _dda_->fh == _dxy_, "Initial DDA wrong") ; \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Initialise an image DDA as a ratio to the image height. */
#define IMAGE_DDA_INITIALISE_H(ddai_, basis_, dxy_) MACRO_START \
  register int32 _dxy_ = (dxy_) ; \
  image_dda_t _base_ = {0, 0, 0} ; \
  image_dda_t _step_ = {0, 0, 0} ; \
  int32 _h_ = (basis_).dl ; \
  HQASSERT((basis_).dh > 0 && _h_ > 0, "DDA basis must be positive") ; \
  if ( _dxy_ >= _h_ ) { /* [h,... */ \
    _base_.i = _dxy_ / _h_ ; \
    _step_.fl = _dxy_ - _base_.i * _h_ ; \
  } else if ( -_dxy_ > _h_ ) { /* ...,-h) */ \
    _base_.i = (_dxy_ - _h_ + 1) / _h_ ; /* floor(dxy/h) */  \
    _step_.fl = _dxy_ - _base_.i * _h_ ; \
  } else if ( _dxy_ >= 0 ) { /* [0,h) */ \
    _base_.i = 0 ; \
    _step_.fl = _dxy_ ; \
  } else { /* [-h,0) */ \
    _base_.i = -1 ; \
    _step_.fl = _dxy_ + _h_ ; \
  } \
  IMAGE_DDA_STEP_N(_base_, _step_, (basis_), (basis_).dh, (ddai_)) ; \
MACRO_END

/** Initialise an image DDA from a real value. */
#define IMAGE_DDA_INITIALISE_R(ddai_, basis_, dxy_) MACRO_START \
  image_dda_t *_dda_ = (ddai_) ; \
  double _dxy_ = (double)(dxy_) ; \
  _dda_->i = (int32)floor(_dxy_) ; \
  _dxy_ = (_dxy_ - _dda_->i) * (basis_).dh ; \
  _dda_->fh = (int32)floor(_dxy_) ; \
  _dxy_ = (_dxy_ - _dda_->fh) * (basis_).dl ; \
  _dda_->fl = (int32)floor(_dxy_) ; \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Initialise an image DDA to zero. */
#define IMAGE_DDA_INITIALISE_0(ddai_) MACRO_START \
  (ddai_)->i = 0 ; \
  (ddai_)->fh = 0 ; \
  (ddai_)->fl = 0 ; \
MACRO_END

/** Initialise an image DDA to the minimum value possible. */
#define IMAGE_DDA_INITIALISE_EPSILON(ddai_) MACRO_START \
  (ddai_)->i = 0 ; \
  (ddai_)->fh = 0 ; \
  (ddai_)->fl = 1 ; \
MACRO_END

/** Return the integer floor of an image DDA. */
#define IMAGE_DDA_FLOOR(ddai_) ((ddai_).i)

/** Return the integer ceiling of an image DDA. */
#define IMAGE_DDA_CEIL(ddai_) ((ddai_).i + ((ddai_).fh > 0 || (ddai_).fl > 0)))

/** Initialise an image DDA from a coordinate value. */
#define IMAGE_DDA_INITIALISE_XY(ddai_, basis_, xy_) MACRO_START \
  register int32 _xy_ = (xy_) ; \
  image_dda_t *_dda_ = (ddai_) ; \
  HQASSERT((basis_).dh > 0 && (basis_).dl > 0, "DDA basis must be positive") ; \
  _dda_->i = _xy_ ; \
  _dda_->fh = 0 ; \
  _dda_->fl = 0 ; \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Initialise an image DDA from a coordinate value, plus 0.5. */
#define IMAGE_DDA_INITIALISE_XY_HALF(ddai_, basis_, xy_) MACRO_START \
  register int32 _xy_ = (xy_) ; \
  image_dda_t *_dda_ = (ddai_) ; \
  HQASSERT((basis_).dh > 0 && (basis_).dl > 0, "DDA basis must be positive") ; \
  _dda_->i = _xy_ ; \
  _dda_->fh = (basis_).dh >> 1 ; \
  if ( (basis_).dh & 1 ) { \
    _dda_->fl = (basis_).dl >> 1 ; \
  } else { \
    _dda_->fl = 0 ; \
  } \
  IMAGE_DDA_ASSERT(_dda_, (basis_)) ; \
MACRO_END

/** Normalise an image DDA to the correct range, assuming the fractional part
    has just been stepped. */
#define IMAGE_DDA_NORMALISE_1(ddan_, basis_) MACRO_START \
  int32 _diff_ ; \
  HQASSERT((basis_).dh > 0 && (basis_).dl > 0, "DDA basis must be positive") ; \
  _diff_ = (ddan_)->fl - (basis_).dl ; \
  if ( _diff_ >= 0 ) { \
    (ddan_)->fl = _diff_ ; \
    (ddan_)->fh += 1 ; \
  } \
  _diff_ = (ddan_)->fh - (basis_).dh ; \
  if ( _diff_ >= 0 ) { \
    (ddan_)->fh = _diff_ ; \
    (ddan_)->i += 1 ; \
  } \
  IMAGE_DDA_ASSERT((ddan_), (basis_)) ; \
MACRO_END

/** Step an image DDA by one step. The output DDA may be the same as the
    input DDA. */
#define IMAGE_DDA_STEP_1(ddain_, step_, basis_, ddaout_) MACRO_START \
  HQASSERT((basis_).dh > 0 && (basis_).dl > 0, "DDA basis must be positive") ; \
  (ddaout_)->i = (ddain_).i + (step_).i ; \
  (ddaout_)->fh = (ddain_).fh + (step_).fh ; \
  (ddaout_)->fl = (ddain_).fl + (step_).fl ; \
  IMAGE_DDA_NORMALISE_1((ddaout_), (basis_)) ; \
MACRO_END

/** Normalise an image DDA to the correct range, assuming the fractional part
    has just been stepped by multiple steps. */
#define IMAGE_DDA_NORMALISE_N(ddan_, basis_) MACRO_START \
  HQASSERT((basis_).dh > 0 && (basis_).dl > 0, "DDA basis must be positive") ; \
  if ( (ddan_)->fl >= (basis_).dl ) { \
    int32 _diff_ = (ddan_)->fl / (basis_).dl ; \
    (ddan_)->fl -= _diff_ * (basis_).dl ; \
    (ddan_)->fh += _diff_ ; \
  } \
  if ( (ddan_)->fh >= (basis_).dh ) { \
    int32 _diff_ = (ddan_)->fh / (basis_).dh ; \
    (ddan_)->fh -= _diff_ * (basis_).dh ; \
    (ddan_)->i += _diff_ ; \
  } \
  IMAGE_DDA_ASSERT((ddan_), (basis_)) ; \
MACRO_END

/** Step an image DDA by a number of steps, using multiplication by up to the
    DDA's maxsteps and division for normalisation. The output DDA may be the
    same as the input DDA. */
#define IMAGE_DDA_STEP_N(ddain_, step_, basis_, nsteps_, ddaout_) MACRO_START \
  image_dda_t _temp_ = (ddain_) ; \
  register int32 _n_ = (nsteps_) ; \
  HQASSERT(_n_ >= 0, "Cannot step image DDA in reverse") ; \
  while ( _n_ != 0 ) { \
    int32 _mul_ ; \
    HQASSERT(_n_ > 0, "DDA loop ran out of steps") ; \
    INLINE_MIN32(_mul_, _n_, (basis_).maxsteps) ; \
    _temp_.i += (step_).i * _mul_ ; \
    _temp_.fh += (step_).fh * _mul_ ; \
    _temp_.fl += (step_).fl * _mul_ ; \
    IMAGE_DDA_NORMALISE_N(&_temp_, (basis_)) ; \
    _n_ -= _mul_ ; \
  } \
  IMAGE_DDA_ASSERT(&_temp_, (basis_)) ; \
  *(ddaout_) = _temp_ ; \
MACRO_END

/** Step an image DDA by a number of steps, without multiplication or
    division operations. Tests are arranged so that as few wasted shifts and
    normalisations as possible are performed, and a single step is optimised. */
#define IMAGE_DDA_STEP_N_LG2(dda_, step_, basis_, nsteps_) MACRO_START \
  register int32 _n_ = (nsteps_) ; \
  HQASSERT(_n_ >= 0, "Cannot step image DDA in reverse") ; \
  if ( (_n_ & 1) != 0 ) { \
    IMAGE_DDA_STEP_1((dda_), (step_), (basis_), &(dda_)) ; \
  } \
  if ( _n_ > 1 ) { \
    image_dda_t _power_ ; \
    _power_.i = (step_).i << 1 ; \
    _power_.fh = (step_).fh << 1 ; \
    _power_.fl = (step_).fl << 1 ; \
    for (;;) { /* Power DDA is not yet normalised */ \
      HQASSERT(_n_ > 1, "Failed to handle previous DDA multiplier") ; \
      IMAGE_DDA_NORMALISE_1(&_power_, (basis_)) ; \
      _n_ >>= 1 ; \
      if ( (_n_ & 1) != 0 ) { \
        IMAGE_DDA_STEP_1((dda_), _power_, (basis_), &(dda_)) ; \
        if ( _n_ == 1 ) \
          break ; \
      } \
      _power_.i <<= 1 ; \
      _power_.fh <<= 1 ; \
      _power_.fl <<= 1 ; \
    } \
  } \
MACRO_END

/** Negate an image DDA. Floor the negated value if there is a fractional
    part, with appropriate carries to keep the fractional part positive. */
#define IMAGE_DDA_NEGATE(ddain_, basis_, ddaout_) MACRO_START \
  register int32 _frac_, _carry_ = 0 ; \
  HQASSERT((basis_).dh > 0 && (basis_).dl > 0, "DDA basis must be positive") ; \
  _frac_ = (ddain_).fl ; \
  if ( _frac_ != 0 ) { \
    _frac_ = (basis_).dl - _frac_ ; \
    _carry_ = 1 ; \
  } \
  (ddaout_)->fl = _frac_ ; \
  _frac_ = (ddain_).fh ; \
  if ( (_frac_ | _carry_) != 0 ) { \
    _frac_ = (basis_).dh - _frac_ - _carry_ ; \
    _carry_ = 1 ; \
  } \
  (ddaout_)->fh = _frac_ ; \
  (ddaout_)->i = -(ddain_).i - _carry_ ; \
  IMAGE_DDA_ASSERT((ddaout_), (basis_)) ; \
MACRO_END

#endif /* !HQN_INT64_OP_ADDSUB */

/** Step an image DDA by a number of steps, allowing negative steps. The
    output DDA may be the same as the input DDA. */
#define IMAGE_DDA_STEP_NN(ddain_, step_, basis_, nsteps_, ddaout_) MACRO_START \
  image_dda_t _step_ ; \
  register int32 _nsteps_ = (nsteps_) ; \
  if ( _nsteps_ < 0 ) { \
    IMAGE_DDA_NEGATE((step_), (basis_), &_step_) ; \
    _nsteps_ = -_nsteps_ ; \
  } else { \
    _step_ = (step_) ; \
  } \
  IMAGE_DDA_STEP_N((ddain_), _step_, (basis_), _nsteps_, (ddaout_)) ; \
MACRO_END

/** \} */

#endif /* __IMAGEDDA_H__ */

/* $Log$
 */
