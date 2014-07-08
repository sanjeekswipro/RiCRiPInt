/** \file
 * \ingroup funcs
 *
 * $HopeName: COREfunctions!src:fnpriv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Function implementation definitions
 */

#ifndef __FNPRIV_H__
#define __FNPRIV_H__

#include "objecth.h"            /* OBJECT */

typedef struct FUNCTIONCACHE FUNCTIONCACHE ;

typedef void  (*fn_uncache_proc)( FUNCTIONCACHE * ) ;
typedef Bool (*fn_evaluate_proc)( FUNCTIONCACHE * , int32 ,
                                  SYSTEMVALUE * , SYSTEMVALUE * ) ;
typedef Bool (*fn_validate_proc)( FUNCTIONCACHE * , void * ) ;
typedef Bool (*fn_finddiscont_proc)( FUNCTIONCACHE * ,
                                     int32 , SYSTEMVALUE [] ,
                                     SYSTEMVALUE * , int32 * ) ;

/* Pointer type for type specific function info. This is an opaque pointer:
   the forward reference to a non-existent struct type is deliberate */
typedef struct fn_type_specific_opaque* fn_type_specific ;

/* Fields in this structure MUST only be accessed from functns.c and
   by the type specific function units (eg fntype0), or there will be
   trouble :-) */
struct FUNCTIONCACHE {
  int32                lock ;          /* cache not purged if this is set */

  int32                firstgen ;      /* identify if function has changed */
  int32                secondgen ;

  int32                fntype ;        /* sample table, exponential, etc */

  int32                spreadmethod ;  /* NAME_None or NAME_Repeat or NAME_Reflect */
  int32                spreadfactor ;  /* number of times function is run across the domain */

  int32                usage ;         /* the client module */

  int32                in_dim ;        /* number of parameters on input */
  int32                out_dim ;       /* number of parameters on output */

  SYSTEMVALUE          s_domain[ 8 ] ; /* inlined domain for in dim <= 4 */
  SYSTEMVALUE          s_range[ 8 ] ;  /* inlined range for out dim <= 4 */
  OBJECT*              domain ;        /* domain for in dim > 4 */
  OBJECT*              range ;         /* range for out dim > 4 */

  fn_type_specific     specific ;      /* ptr to type specific function data */

  fn_evaluate_proc     evalproc ;      /* type specific procs for evaluation, */
  fn_uncache_proc      freeproc ;      /* freeing specific data and finding */
  fn_finddiscont_proc  discontproc ;   /* a discontinuity in a sepcific type */

  SYSTEMVALUE*         input ;         /* input buf for in_dim > 4 */
  SYSTEMVALUE*         output ;        /* output buf for out_dim > 4 */
} ;

/** Unpack a function onto the cache. Can be called recursively for type 3
   stitching functions. */
Bool fn_unpack(OBJECT *theo, FUNCTIONCACHE *fn, int32 safe_recurse, int32 usage) ;

/** Checks arrayobj contains only integers or reals. */
Bool fn_typecheck_array( OBJECT *arrayobj ) ;

Bool fn_extract_from_alternate( OBJECT *longerone ,
                                SYSTEMVALUE *shorterone ,
                                int32 shorterlen , int len ,
                                int32 index,
                                SYSTEMVALUE *result ) ;

Bool fn_check_bounds( SYSTEMVALUE bounds[],
                      SYSTEMVALUE domain[],
                      SYSTEMVALUE *discontinuity,
                      int32 *order ) ;

/** Are values equal when converted to USERVALUE? */
#define MAPS_TO_SAME_USERVALUE(res, a, b) \
  MACRO_START \
    /* volatile forces compiler to do the conversions. */ \
    volatile SYSTEMVALUE _mtsu_SYSTEMVALUE_a = (SYSTEMVALUE)(a), \
                         _mtsu_SYSTEMVALUE_b = (SYSTEMVALUE)(b); \
    /* Two conversions are needed, because the input could be \
     * (internally represented as) an extended real , and we want to \
     * compute exactly as will happen when a value is returned in a \
     * SYSTEMVALUE and later converted to USERVALUE. */ \
    volatile USERVALUE _mtsu_USERVALUE_a = (USERVALUE)_mtsu_SYSTEMVALUE_a, \
                       _mtsu_USERVALUE_b = (USERVALUE)_mtsu_SYSTEMVALUE_b; \
    res = ( _mtsu_USERVALUE_a == _mtsu_USERVALUE_b ); \
  MACRO_END

/** Explicitly invalidate a cache slot, as an alternative to relying on
    firstgen and secondgen (usually colorgen and reprogen respectively) */
#define fn_invalidate_entry( _fn ) MACRO_START \
  HQASSERT( (_fn) != NULL , "fn_invalidate_entry: _fn null" ) ; \
  (_fn)->firstgen  = FN_GEN_INVALID ; \
  (_fn)->secondgen = FN_GEN_INVALID ; \
MACRO_END

#define fn_cliptointerval( _arg , _lower , _upper ) MACRO_START  \
  if ( (_lower) > (_upper) ) \
    return error_handler( RANGECHECK ) ; \
  if ( (_arg) < (_lower) ) \
    (_arg) = (_lower) ; \
  else if ( (_arg) > (_upper) ) \
    (_arg) = (_upper) ; \
MACRO_END

/** Adjust \a input by an epsilon in the direction given by \a upwards. The
    epsilon is scaled to match the exponent of input to avoid underflow. Biasing
    the input value upwards or downwards helps to avoid discontinuities. */
#define fn_bias_input(input, upwards) MACRO_START \
  SYSTEMVALUE epsilon = input == 0 ? FLT_EPSILON : fabs(input) * FLT_EPSILON ; \
  input += upwards ? epsilon : -epsilon ; \
MACRO_END

/* Log stripped */
#endif
