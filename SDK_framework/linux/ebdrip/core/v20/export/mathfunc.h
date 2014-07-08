/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:mathfunc.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Mathematic operators API
 */

#ifndef __MATHFUNC_H__
#define __MATHFUNC_H__


#define SIN0	 0.0
#define SIN90	 1.0
#define SIN180	 0.0
#define SIN270	-1.0

#define COS0	 1.0
#define COS90	 0.0
#define COS180	-1.0
#define COS270	 0.0

/* Lifted straight from gu_misc.c */
#define NORMALISE_ANGLE( _angle ) \
MACRO_START \
  if ((_angle) < 0.0 )  /* Common case; angle just negative */ \
    (_angle) += 360.0 ; \
  if ((_angle) < 0.0 || (_angle) >= 360.0 ) { \
    if ((_angle) > BIGGEST_INTEGER || (_angle) < -BIGGEST_INTEGER ) \
      (_angle) = 45.0; /* ! */ \
    else \
      (_angle) -= 360.0 * (int32) ((_angle) / 360.0) ; \
    if ((_angle) < 0.0 )  /* For negative angles */ \
      (_angle) += 360.0 ; \
  } \
MACRO_END

#define SIN_ANGLE( _angle , _sin_angle ) \
MACRO_START \
  HQASSERT( (_angle) >= 0.0 && (_angle) < 360.0 , \
                 "angle non-normalised in SIN_ANGLE") ; \
  if ((_angle) == 0.0 ) { \
    (_sin_angle) = SIN0 ; \
  } \
  else if ((_angle) ==  90.0 ) { \
    (_sin_angle) = SIN90 ; \
  } \
  else if ((_angle) == 180.0 ) { \
    (_sin_angle) = SIN180 ; \
  } \
  else if ((_angle) == 270.0 ) { \
    (_sin_angle) = SIN270 ; \
  } \
  else { \
    SYSTEMVALUE tmpangle = (_angle) * DEG_TO_RAD ; \
    (_sin_angle) = sin( tmpangle ) ; \
  } \
MACRO_END

#define COS_ANGLE( _angle , _cos_angle ) \
MACRO_START \
  HQASSERT( (_angle) >= 0.0 && (_angle) < 360.0 , \
                 "angle non-normalised in COS_ANGLE") ; \
  if ((_angle) == 0.0 ) { \
    (_cos_angle) = COS0 ; \
  } \
  else if ((_angle) ==  90.0 ) { \
    (_cos_angle) = COS90 ; \
  } \
  else if ((_angle) == 180.0 ) { \
    (_cos_angle) = COS180 ; \
  } \
  else if ((_angle) == 270.0 ) { \
    (_cos_angle) = COS270 ; \
  } \
  else { \
    SYSTEMVALUE tmpangle = (_angle) * DEG_TO_RAD ; \
    (_cos_angle) = cos( tmpangle ) ; \
  } \
MACRO_END

#define SINCOS_ANGLE( _angle , _sin_angle , _cos_angle ) \
MACRO_START \
  HQASSERT( (_angle) >= 0.0 && (_angle) < 360.0 , \
                 "angle non-normalised in SINCOS_ANGLE") ; \
  if ((_angle) == 0.0 ) { \
    (_sin_angle) = SIN0 ; \
    (_cos_angle) = COS0 ; \
  } \
  else if ((_angle) ==  90.0 ) { \
    (_sin_angle) = SIN90 ; \
    (_cos_angle) = COS90 ; \
  } \
  else if ((_angle) == 180.0 ) { \
    (_sin_angle) = SIN180 ; \
    (_cos_angle) = COS180 ; \
  } \
  else if ((_angle) == 270.0 ) { \
    (_sin_angle) = SIN270 ; \
    (_cos_angle) = COS270 ; \
  } \
  else { \
    SYSTEMVALUE tmpangle = (_angle) * DEG_TO_RAD ; \
    (_sin_angle) = sin( tmpangle ) ; \
    (_cos_angle) = cos( tmpangle ) ; \
  } \
MACRO_END


#if defined(CHECK_ATAN2_ARGS)
#define myatan2(dy,dx) (((dy) == 0.0 && (dx) == 0.0) ? 0.0 : (atan2((dy),(dx))))
#else
#define myatan2(dy,dx) atan2((dy),(dx))
#endif


#endif /* protection for multiple inclusion */

/* Log stripped */
