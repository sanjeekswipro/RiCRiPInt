/** \file
 * \ingroup stitchfuncs
 *
 * $HopeName: COREfunctions!src:fntype3.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Function Type 3 Implementation
 */

#include "core.h"
#include "swerrors.h"

#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "control.h"
#include "dictscan.h"
#include "namedef_.h"
#include "functns.h"

#include "fnpriv.h"
#include "fntype3.h"
#include <float.h>  /* ANSI standard macro FLT_EPSILON */

/*----------------------------------------------------------------------------*/
typedef struct fntype3 {
  int32 functions_len ;
  FUNCTIONCACHE *functions ;
  SYSTEMVALUE *bounds ;
  SYSTEMVALUE *encode ;
} FNTYPE3 ;

/*----------------------------------------------------------------------------*/
static void  fntype3_free_functions( FNTYPE3 *t3 ) ;
static void  fntype3_freecache( FUNCTIONCACHE *fn ) ;
static Bool fntype3_evaluate_function( FUNCTIONCACHE *fn , Bool upwards ,
                                       SYSTEMVALUE *input , SYSTEMVALUE *output ) ;
static Bool fntype3_find_discontinuity( FUNCTIONCACHE *fn ,
                                        int32 index ,
                                        SYSTEMVALUE bounds[],
                                        SYSTEMVALUE *discontinuity , int32 *order ) ;

/*----------------------------------------------------------------------------*/
Bool fntype3_initcache( FUNCTIONCACHE *fn )
{
  FNTYPE3 *t3 ;

  HQASSERT( fn , "fn is null in fntype3_initcache" ) ;
  HQASSERT( fn->specific == NULL ,
            "specific not null in fntype3_initcache" ) ;
  t3 = mm_alloc( mm_pool_temp ,
                 sizeof( FNTYPE3 ) ,
                 MM_ALLOC_CLASS_FUNCTIONS ) ;
  if ( t3 == NULL )
    return error_handler( VMERROR ) ;

  fn->specific = ( fn_type_specific )t3 ;

  t3->functions_len = 0 ;
  t3->functions = NULL ;
  t3->bounds = NULL ;
  t3->encode = NULL ;
  fn->evalproc = fntype3_evaluate_function ;
  fn->discontproc = fntype3_find_discontinuity ;
  fn->freeproc = fntype3_freecache ;

  return TRUE ;
}

/*----------------------------------------------------------------------------*/

enum {fntype3_Functions, fntype3_Bounds, fntype3_Encode};

static NAMETYPEMATCH fntype3_dict[] = {
  { NAME_Functions, 1, { OARRAY }},
  { NAME_Bounds,    1, { OARRAY }},
  { NAME_Encode,    1, { OARRAY }},
  DUMMY_END_MATCH
} ;

Bool fntype3_unpack( FUNCTIONCACHE *fn , OBJECT *thed , int32 safe_recurse )
{
  FNTYPE3 *t3;
  OBJECT *theo;
  int32 n, i, out_dim;

  HQASSERT( fn , "fn is null in fntype3_unpack" ) ;
  HQASSERT( thed , "thed is null in fntype3_unpack" ) ;

  t3 = ( FNTYPE3 * )fn->specific ;
  HQASSERT( t3 , "t3 is null in fntype3_unpack" ) ;

  /* Must be a 1 - input function. */
  if ( fn->in_dim != 1 )
    return error_handler( RANGECHECK ) ;

  if ( ! dictmatch( thed , fntype3_dict ))
    return FALSE ;

  theo = fntype3_dict[fntype3_Functions].result ;
  n = theLen( *theo ) ;

  if ( n == 0 )
    return error_handler( RANGECHECK ) ;

  /* We (almost) always need to free any existing sub-functions. The only time
   * we can avoid it is when the sub-functions are also the same but that's
   * too complicated a check.
   */
  if ( t3->functions_len != 0 )
    fntype3_free_functions(t3);

  t3->functions = mm_alloc(mm_pool_temp, n * sizeof(FUNCTIONCACHE),
                           MM_ALLOC_CLASS_FUNCTIONS);
  if ( t3->functions != NULL ) {
    for ( i = 0; i < n; ++i ) {
      t3->functions[i].fntype = -1;
      t3->functions[i].specific = NULL;
    }
  }

  t3->functions_len = n;

  t3->encode = mm_alloc(mm_pool_temp, 2 * n * sizeof(SYSTEMVALUE),
                        MM_ALLOC_CLASS_FUNCTIONS);
  if ( n > 1 )
    t3->bounds = mm_alloc(mm_pool_temp, (n - 1) * sizeof(SYSTEMVALUE),
                          MM_ALLOC_CLASS_FUNCTIONS);

  if ( t3->functions == NULL || t3->encode == NULL ||
       (n > 1 && t3->bounds == NULL) ) {
    fntype3_free_functions(t3);
    return error_handler(VMERROR);
  }

  /* Bounds */
  theo = fntype3_dict[fntype3_Bounds].result ;
  if ( theLen( *theo ) != n - 1 )
    return error_handler( RANGECHECK ) ;

  if ( n > 1 ) {
    if ( ! object_get_numeric_array(theo, t3->bounds, n - 1) )
      return FALSE ;

    for ( i = 0 ; i < n - 1 ; ++i ) {
      SYSTEMVALUE b = t3->bounds[ i ] ;
      /* Each bound must be within domain. */
      if ( b < fn->s_domain[ 0 ] || b > fn->s_domain[ 1 ] )
        return error_handler( RANGECHECK ) ;

      /* Bounds must be in order of increasing magnitude. */
      if ( i != 0 && b < t3->bounds[ i - 1 ] )
        return error_handler( RANGECHECK ) ;
    }
  }

  /* Encode */
  theo = fntype3_dict[fntype3_Encode].result ;
  if ( theLen( *theo ) != 2 * n )
    return error_handler( RANGECHECK ) ;

  if ( ! object_get_numeric_array(theo, t3->encode, theLen(*theo)) )
    return FALSE ;

  /* Functions */
  theo = oArray(*fntype3_dict[fntype3_Functions].result) ;

  for ( i = 0; i < t3->functions_len; ++i ) {
    if ( !fn_unpack(&theo[i], &t3->functions[i], safe_recurse, fn->usage) )
      return FALSE;
  }

  /* Check out dim is consistent across the functions. */
  out_dim = fn->out_dim ;

  for ( i = 0 ; i < t3->functions_len ; ++i ) {
    if ( i == 0 && fn->range == NULL )
      out_dim = t3->functions[i].out_dim ;
    else
      if ( out_dim != t3->functions[i].out_dim )
        return error_handler( RANGECHECK ) ;
  }

  fn->out_dim = out_dim ;

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
static void fntype3_free_functions( FNTYPE3 *t3 )
{
  HQASSERT( t3 , "t3 is null in fntype3_free_functions" ) ;

  if (t3->bounds != NULL)
    mm_free(mm_pool_temp, t3->bounds,(t3->functions_len - 1) * sizeof(SYSTEMVALUE));
  if (t3->encode != NULL)
    mm_free(mm_pool_temp, t3->encode,2 * t3->functions_len * sizeof(SYSTEMVALUE));

  if ( t3->functions != NULL ) {
    FUNCTIONCACHE *fn_ptr = t3->functions ;
    int32 i ;
    for ( i = 0 ; i < t3->functions_len ; ++i ) {
      if ( fn_ptr->specific != NULL )
        (*fn_ptr->freeproc)( fn_ptr ) ;
      ++fn_ptr ;
    }
    mm_free( mm_pool_temp , t3->functions ,
             t3->functions_len * sizeof( FUNCTIONCACHE )) ;
  }
  t3->functions = NULL ;
  t3->bounds = NULL ;
  t3->encode = NULL ;
  t3->functions_len = 0 ;
}

/*----------------------------------------------------------------------------*/
static void fntype3_freecache( FUNCTIONCACHE *fn )
{
  FNTYPE3 *t3 ;

  t3 = ( FNTYPE3 * )fn->specific ;
  HQASSERT( t3 , "t3 is null in fntype3_freecache" ) ;

  /* May need to call the freeproc for each function and then free the
   * functioncache list itself.
   */
  if ( t3->functions_len != 0 )
    fntype3_free_functions( t3 ) ;

  HQASSERT( t3->functions == NULL ,
            "t3->functions is not null in fntype3_freecache" ) ;
  mm_free( mm_pool_temp , t3 , sizeof( FNTYPE3 )) ;
  fn->specific = NULL ;
  fn_invalidate_entry( fn ) ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype3_evaluate_function( FUNCTIONCACHE *fn , Bool upwards ,
                                       SYSTEMVALUE *input , SYSTEMVALUE *output )
{
  FNTYPE3 *t3 ;
  SYSTEMVALUE et , d0 , d1 , lb , ub , elb , eub ;
  FUNCTIONCACHE *fn_ptr ;
  int32 n , i ;

  HQASSERT( input , "input is null in fntype3_evaluate_function" ) ;
  HQASSERT( output , "output is null in fntype3_evaluate_function" ) ;

  et = *input ;

  t3 = ( FNTYPE3 * )fn->specific ;
  HQASSERT( t3 , "t3 is null in fntype3_evaluate_function" ) ;

  /* Adjust the input value by an epsilon depending on evaluation direction.
     This is to make sure that rounding errors in discontinuity encoding and
     decoding still cause the value to fall within the correct function. When
     evaluating upwards, we prefer to be in the function above a discontinuity,
     when evaluating downwards, we prefer to be in the function below a
     discontinuity. This modified input value is used for choosing the sub
     function only, and not for function evaluation. */
  fn_bias_input(et, upwards) ;

  /* Clip input values to the domain of the function. */
  d0 = fn->s_domain[ 0 ] ;
  d1 = fn->s_domain[ 1 ] ;
  fn_cliptointerval( et , d0 , d1 ) ;

  /* Select the appropriate function by determining which interval the
   * input belongs. Obtain the bounds/domain interval and encoding
   * interval.
   */
  n = t3->functions_len ;
  lb = d0 ;
  ub = d1 ;

  if ( upwards ) {
    for ( i = n - 1 ; i >= 0 ; --i ) {
      lb = i == 0 ? d0 : t3->bounds[i-1] ;
      if ( lb <= et && (et < ub || lb == et || i == n - 1) )
        break ;
      ub = lb ;
    }
  } else {
    for ( i = 0 ; i < n ; ++i ) {
      ub = i == n - 1 ? d1 : t3->bounds[i] ;
      if ( lb <= et && (et < ub || lb == et || i == n - 1) )
        break ;
      lb = ub ;
    }
  }

  elb = t3->encode[ 2 * i ];
  eub = t3->encode[ 2 * i + 1 ];

  if ( lb == ub ) {
    /* Strangely enough, Adobe don't generate an error here (as we initially
       did). They appear to evaluate the function as either the lower or upper
       bound. It's difficult to say why they choose one or the other but always
       choosing elb seems to work better most of the time. */
    et = elb ;
  } else {
    /* Clip the original input value to interval of function range found, in
       case rounding errors caused it to drop into an adjacent function range. */
    fn_cliptointerval( *input , lb , ub ) ;

    /* Encode the input value according to bounds and encode intervals. */
    et = (( *input - lb ) * ( eub - elb )) / ( ub - lb ) + elb ;
  }

  /* Reverse sense of the "upwards" flag if necessary */
  if ( eub < elb )
    upwards = !upwards ;

  fn_ptr = &t3->functions[i] ;

  if ( ! (*fn_ptr->evalproc)( fn_ptr , upwards , & et , output ))
    return FALSE ;

  if ( fn->range != NULL ) {
    /* Clip output to Range interval. */
    if ( fn->out_dim <= 4 ) {
      SYSTEMVALUE *range_ptr = fn->s_range ;
      for ( i = 0 ; i < fn->out_dim ; ++i ) {
        fn_cliptointerval( *output , range_ptr[ 0 ] , range_ptr[ 1 ] ) ;
        ++output ;
        range_ptr += 2 ;
      }
    }
    else {
      OBJECT *range_ptr = oArray(*fn->range) ;
      for ( i = 0 ; i < fn->out_dim ; ++i ) {
        if ( !object_get_numeric(range_ptr++, &lb) ||
             !object_get_numeric(range_ptr++, &ub) )
          return FALSE ;
        fn_cliptointerval( *output , lb , ub ) ;
        ++output ;
      }
    }
  }

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype3_find_discontinuity( FUNCTIONCACHE *fn ,
                                        int32 index ,
                                        SYSTEMVALUE bounds[],
                                        SYSTEMVALUE *discontinuity , int32 *order )
{
  FNTYPE3 *t3 ;
  SYSTEMVALUE fun_bounds[2], encode[2], domain[2] , encoded_bounds[2] ;
  SYSTEMVALUE boundary_val, stmp;
  FUNCTIONCACHE *sub_fn ;
  int32 first_unknown, last_unknown, b_sample, i, sub_order ;

  if ( bounds[0] > bounds[1] ) { /* Type 3 can encode in reverse order */
    SYSTEMVALUE rbounds[2] ;

    rbounds[0] = bounds[1] ;
    rbounds[1] = bounds[0] ;

    return fntype3_find_discontinuity(fn, index, rbounds, discontinuity, order) ;
  }

  HQASSERT( fn , "fn is null in fntype3_find_discontinuity" ) ;

  t3 = ( FNTYPE3 * )fn->specific ;
  HQASSERT( t3 , "t3 is null in fntype3_find_discontinuity" ) ;

  HQASSERT( fn->in_dim == 1, "input dim not 1 in fntype3_find_discontinuity" ) ;
  HQASSERT( index == 0, "index not 0 in fntype3_find_discontinuity" ) ;
  HQASSERT( t3->functions_len > 0, "Badly formed stiched function in fntype3_find_discontinuity" ) ;

  first_unknown = 0;
  last_unknown = t3->functions_len -2;
  while ( first_unknown <= last_unknown ) {
    b_sample = (first_unknown + last_unknown) /2;
    boundary_val = t3->bounds[b_sample];
    if ( boundary_val <= bounds[0] ) {
      first_unknown = b_sample +1 ;
    } else if ( boundary_val >= bounds[1] ) {
      last_unknown = b_sample -1 ;
    } else {
      /* We've found one. */
      *discontinuity = boundary_val ;
      *order = 0;
      return TRUE ;
    }
  }
  HQASSERT( first_unknown - last_unknown == 1,
    "Screwy internal logic, gap found not 1 in fntype3_find_discontinuity" ) ;

  /* so, if there's a discontinuity it's the domain or within function
     first_unknown */
  if ( !fn_extract_from_alternate( fn->domain, fn->s_domain, 8,
                                   2 * fn->in_dim, 2 * index, &domain[0] ) )
    return FALSE ;
  if ( !fn_extract_from_alternate( fn->domain, fn->s_domain, 8,
                                   2 * fn->in_dim, 2 * index +1, &domain[1] ) )
    return FALSE ;
  if ( domain[0] >= domain[1] )
    return error_handler( UNDEFINED ) ; /** \todo What should this be? */

  /* There'll be no discontinuity if we're right outside the domain. */
  if ( domain[0] >= bounds[1] || domain[1] <= bounds[0] )
    return TRUE ;

  /* OK, let's start preparing to check function first_unknown */
  if ( first_unknown == 0 )
    fun_bounds[0] = domain[0] ;
  else
    fun_bounds[0] = t3->bounds[first_unknown - 1];

  if ( first_unknown == t3->functions_len -1 )
    fun_bounds[1] = domain[1] ;
  else
    fun_bounds[1] = t3->bounds[first_unknown] ;

  encode[0] = t3->encode[2 * first_unknown];
  encode[1] = t3->encode[2 * first_unknown + 1];

  if ( encode[1] == encode[0] ) {
    /* The entire domain maps onto one value, so there are no discontinuities
     * - even if one of the boundaries is outside the domain, the domain
     * doesn't mark a discontinuity! */
    return TRUE ;
  }
  for ( i = 0 ; i < 2 ; i ++ ) {
    /* We don't take the common factors out of the loop, because we want to
       make this as close to the reverse encoding as possible. There will
       still be floating point errors that require the use of an epsilon
       test. */
    encoded_bounds[i] = ((bounds[i] - fun_bounds[0]) *
                         (encode[1] - encode[0])) /
                        (fun_bounds[1] - fun_bounds[0]) + encode[0] ;
  }

  sub_fn = & t3->functions[ first_unknown ] ;
  for ( ;; ) {
    sub_order = -1 ;
    if ( ! (*sub_fn->discontproc)( sub_fn, index, encoded_bounds, &stmp, &sub_order ))
      return FALSE ;

    if ( sub_order >= 0 ) {      /* decode stmp to get the true position */
      SYSTEMVALUE result = ((stmp - encode[0]) *
                            (fun_bounds[1] - fun_bounds[0])) /
                           (encode[1] - encode[0]) + fun_bounds[0] ;
      int maps_to_same;

      /* If we will map back to the same value, then try again with reduced
         range */
      MAPS_TO_SAME_USERVALUE( maps_to_same, result, bounds[0] );
      if ( maps_to_same ) {
        encoded_bounds[0] = stmp ;
        continue ;
      } else {
        MAPS_TO_SAME_USERVALUE( maps_to_same, result, bounds[1] );
        if ( maps_to_same ) {
          encoded_bounds[1] = stmp ;
          continue ;
        }
      }

      *discontinuity = result ;
      *order = sub_order ;
      return TRUE ;
    }
    break ;
  }

  if ( first_unknown == 0 || first_unknown == t3->functions_len -1 ) {
    /* If it's the first or last function, check the domain */
    if ( fn_check_bounds( bounds , domain , discontinuity , order ) )
      return TRUE ;
  }

  /* Nope, no discontinuity found. */
  return TRUE ;
}

/*----------------------------------------------------------------------------*/


/* Log stripped */
