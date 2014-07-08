/** \file
 * \ingroup expfuncs
 *
 * $HopeName: COREfunctions!src:fntype2.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Function Type 2 Implementation
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
#include "fntype2.h"

/*----------------------------------------------------------------------------*/
typedef struct fntype2 {
  SYSTEMVALUE s_c0[ 4 ] ;
  SYSTEMVALUE s_c1[ 4 ] ;
  OBJECT *c0 ;
  OBJECT *c1 ;
  SYSTEMVALUE n ;
} FNTYPE2 ;

/*----------------------------------------------------------------------------*/
static void  fntype2_freecache( FUNCTIONCACHE *fn ) ;
static Bool fntype2_evaluate_function( FUNCTIONCACHE *fn , Bool upwards ,
                                       SYSTEMVALUE *input , SYSTEMVALUE *output ) ;
static Bool fntype2_find_discontinuity( FUNCTIONCACHE *fn ,
                                        int32 index ,
                                        SYSTEMVALUE bounds[],
                                        SYSTEMVALUE *discontinuity , int32 *order ) ;

/*----------------------------------------------------------------------------*/
Bool fntype2_initcache( FUNCTIONCACHE *fn )
{
  FNTYPE2 *t2 ;

  HQASSERT( fn , "fn is null in fntype2_initcache" ) ;
  HQASSERT( fn->specific == NULL ,
            "specific not null in fntype2_initcache" ) ;
  t2 = ( FNTYPE2 * )mm_alloc( mm_pool_temp ,
                              sizeof( FNTYPE2 ) ,
                              MM_ALLOC_CLASS_FUNCTIONS ) ;
  if ( t2 == NULL )
    return error_handler( VMERROR ) ;

  fn->specific = ( fn_type_specific )t2 ;

  t2->c0 = NULL ;
  t2->c1 = NULL ;
  t2->n = 0 ;
  fn->evalproc = fntype2_evaluate_function ;
  fn->discontproc = fntype2_find_discontinuity ;
  fn->freeproc = fntype2_freecache ;

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
static NAMETYPEMATCH fntype2_dict[] = {
/* 0 */ { NAME_C0 | OOPTIONAL, 3, { OINTEGER, OREAL, OARRAY }},
/* 1 */ { NAME_C1 | OOPTIONAL, 3, { OINTEGER, OREAL, OARRAY }},
/* 2 */ { NAME_N,              2, { OINTEGER, OREAL }},
        DUMMY_END_MATCH
} ;

Bool fntype2_unpack( FUNCTIONCACHE *fn , OBJECT *thed )
{
  FNTYPE2 *t2 ;
  OBJECT *theo ;

  HQASSERT( fn , "fn is null in fntype2_unpack" ) ;
  HQASSERT( thed , "thed is null in fntype2_unpack" ) ;

  t2 = ( FNTYPE2 * )fn->specific ;
  HQASSERT( t2 , "t2 is null in fntype2_unpack" ) ;

  /* Must be a 1 - input function. */
  if ( fn->in_dim != 1 )
    return error_handler( RANGECHECK ) ;

  if ( ! dictmatch( thed , fntype2_dict ))
    return FALSE ;

  /* Range is optional, and therefore out_dim may not have been
   * determind yet; if not it assumed to be the length of C0/C1.
   */

  /* C0 */
  theo = fntype2_dict[ 0 ].result ;
  if ( theo != NULL && oType(*theo) == OARRAY ) {

    if ( theLen(*theo) != fn->out_dim ) {
      if ( fn->range == NULL && theLen(*theo) > 0 )
        fn->out_dim = theLen(*theo) ;
      else
        return error_handler( RANGECHECK ) ;
    }

    if ( theLen(*theo) > 4 ) {
      if ( ! fn_typecheck_array( theo ))
        return FALSE ;
      t2->c0 = theo ;
    } else {
      if ( ! object_get_numeric_array(theo, t2->s_c0, theLen(*theo)) )
        return FALSE ;
    }
  }
  else {
    if ( fn->out_dim != 1 ) {
      if ( fn->range == NULL )
        fn->out_dim = 1 ;
      else
        return error_handler( RANGECHECK ) ;
    }

    if ( theo != NULL ) {
      if ( !object_get_numeric(theo, &t2->s_c0[0]) )
        return FALSE ;
    } else    /* The default. */
      t2->s_c0[ 0 ] = 0 ;
  }

  /* C1 */
  theo = fntype2_dict[ 1 ].result ;
  if ( theo != NULL && oType(*theo) == OARRAY ) {

    if ( theLen(*theo) != fn->out_dim )
      return error_handler( RANGECHECK ) ;

    if ( theLen(*theo) > 4 ) {
      if ( ! fn_typecheck_array( theo ))
        return FALSE ;
      t2->c1 = theo ;
    } else {
      if ( ! object_get_numeric_array(theo, t2->s_c1, theLen(*theo)) )
        return FALSE ;
    }
  }
  else {
    if ( fn->out_dim != 1 )
      return error_handler( RANGECHECK ) ;

    if ( theo != NULL ) {
      if ( !object_get_numeric(theo, &t2->s_c1[0]) )
        return FALSE ;
    } else    /* The default. */
      t2->s_c1[ 0 ] = 1 ;
  }

  /* N */
  if ( !object_get_numeric( fntype2_dict[ 2 ].result, &t2->n ) )
    return FALSE ;

  /* Check domain constrains function to defined results.
   */

  /* Check for division by 0. */
  if ( t2->n < 0 && fn->s_domain[ 0 ] <= 0 && fn->s_domain[ 1 ] >= 0 )
    return error_handler( RANGECHECK ) ;

  /* Check for sqrt of -1. */
  if ( t2->n - ( uint32 ) t2->n > 0 && fn->s_domain[ 0 ] < 0 )
    return error_handler( RANGECHECK ) ;

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
static void fntype2_freecache( FUNCTIONCACHE *fn )
{
  HQASSERT( fn , "fn is null in fntype2_freecache" ) ;
  mm_free( mm_pool_temp , ( mm_addr_t )( fn->specific ) , sizeof( FNTYPE2 )) ;
  fn->specific = NULL ;
  fn_invalidate_entry( fn ) ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype2_evaluate_function( FUNCTIONCACHE *fn , Bool upwards ,
                                       SYSTEMVALUE *input , SYSTEMVALUE *output )
{
  FNTYPE2 *t2 ;
  SYSTEMVALUE t ;
  SYSTEMVALUE lb , ub ;
  SYSTEMVALUE t_to_N ;
  int32 n ;

  UNUSED_PARAM(Bool, upwards) ;

  /* C0 + t^N * ( C1 - C0 ) */

  HQASSERT( input , "input is null in fntype2_evaluate_function" ) ;
  HQASSERT( output , "output is null in fntype2_evaluate_function" ) ;
  t = ( SYSTEMVALUE ) *input ;

  t2 = ( FNTYPE2 * )fn->specific ;
  HQASSERT( t2 , "t2 is null in fntype2_evaluate_function" ) ;

  /* Clip input values to the domain of the function. */
  lb = fn->s_domain[ 0 ] ;
  ub = fn->s_domain[ 1 ] ;
  fn_cliptointerval( t , lb , ub ) ;

  t_to_N = pow( t , t2->n ) ;

  if ( fn->out_dim <= 4 ) {

    SYSTEMVALUE *c0_ptr = t2->s_c0 ;
    SYSTEMVALUE *c1_ptr = t2->s_c1 ;
    SYSTEMVALUE *range_ptr = fn->s_range ;

    for ( n = 0 ; n < fn->out_dim ; ++n ) {

      SYSTEMVALUE out ;

      out = *c0_ptr + t_to_N * ( *c1_ptr - *c0_ptr ) ;

      if ( fn->range != NULL ) {

        fn_cliptointerval( out , range_ptr[ 0 ] , range_ptr[ 1 ] ) ;
        range_ptr += 2 ;

      }

      *output = out ;

      ++output ; ++c0_ptr ; ++c1_ptr ;

    }
  }
  else {

    OBJECT *c0_ptr = oArray(*t2->c0) ;
    OBJECT *c1_ptr = oArray(*t2->c1) ;
    OBJECT *range_ptr = NULL ;

    if ( fn->range != NULL )
      range_ptr = oArray(*fn->range) ;

    for ( n = 0 ; n < fn->out_dim ; ++n ) {

      SYSTEMVALUE out , c0 , c1 ;

      if ( !object_get_numeric(c0_ptr, &c0) ||
           !object_get_numeric(c1_ptr, &c1) )
        return FALSE ;

      out = c0 + t_to_N * ( c1 - c0 ) ;

      if ( fn->range != NULL ) {

        if ( !object_get_numeric(range_ptr++, &lb) ||
             !object_get_numeric(range_ptr++, &ub) )
          return FALSE ;

        fn_cliptointerval( out , lb , ub ) ;
      }

      *output = out ;

      ++output ; ++c0_ptr ; ++c1_ptr ;

    }
  }

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
static Bool fntype2_find_discontinuity( FUNCTIONCACHE *fn ,
                                        int32 index ,
                                        SYSTEMVALUE bounds[],
                                        SYSTEMVALUE *discontinuity , int32 *order )
{
  FNTYPE2 *t2 ;
  SYSTEMVALUE domain[2] ;

  if ( bounds[0] > bounds[1] ) { /* Type 3 can encode in reverse order */
    SYSTEMVALUE rbounds[2] ;

    rbounds[0] = bounds[1] ;
    rbounds[1] = bounds[0] ;

    return fntype2_find_discontinuity(fn, index, rbounds, discontinuity, order) ;
  }

  HQASSERT( fn , "fn is null in fntype2_find_discontinuity" ) ;

  t2 = ( FNTYPE2 * )fn->specific ;
  HQASSERT( t2 , "t2 is null in fntype2_find_discontinuity" ) ;

  HQASSERT( fn->in_dim == 1, "input dim not 1 in fntype2_find_discontinuity" ) ;
  HQASSERT( index == 0, "index not 0 in fntype2_find_discontinuity" ) ;

  if ( !fn_extract_from_alternate( fn->domain, fn->s_domain, 8, 2 * fn->in_dim,
    2 * index, &domain[0] ) )
    return FALSE ;
  if ( !fn_extract_from_alternate( fn->domain, fn->s_domain, 8, 2 * fn->in_dim,
    2 * index +1, &domain[1] ) )
    return FALSE ;

  if ( domain[0] >= domain[1] ) {
    return error_handler( UNDEFINED ) ; /** \todo What should this be? */
  }

  if ( fn_check_bounds( bounds , domain , discontinuity , order ) )
    return TRUE ;
  /* The interval is within the domain, which is continuous throughout. */
  return TRUE ;
}

/*----------------------------------------------------------------------------*/


/* Log stripped */
