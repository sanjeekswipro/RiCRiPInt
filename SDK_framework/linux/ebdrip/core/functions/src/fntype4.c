/** \file
 * \ingroup psfuncs
 *
 * $HopeName: COREfunctions!src:fntype4.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Function Type 4 Implementation
 */

#include "core.h"
#include "swerrors.h"

#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "fileops.h"
#include "control.h"
#include "stacks.h"
#include "dictscan.h"
#include "namedef_.h"
#include "functns.h"
#include "pathops.h"
#include "execops.h"

#include "fnpriv.h"
#include "fntype4.h"

/*----------------------------------------------------------------------------*/
typedef struct fntype4 {
  OBJECT ps_proc ;
} FNTYPE4 ;

/*----------------------------------------------------------------------------*/
static void  fntype4_freecache( FUNCTIONCACHE *fn ) ;
static Bool fntype4_evaluate_function( FUNCTIONCACHE *fn , Bool upwards ,
                                       SYSTEMVALUE *input , SYSTEMVALUE *output ) ;
static Bool fntype4_find_discontinuity( FUNCTIONCACHE *fn ,
                                        int32 index ,
                                        SYSTEMVALUE bounds[],
                                        SYSTEMVALUE *discontinuity , int32 *order ) ;


/*----------------------------------------------------------------------------*/
Bool fntype4_initcache( FUNCTIONCACHE *fn )
{
  FNTYPE4 *t4 ;

  HQASSERT( fn , "fn is null in fntype4_initcache" ) ;
  HQASSERT( fn->specific == NULL ,
            "specific not null in fntype4_initcache" ) ;
  t4 = ( FNTYPE4 * )mm_alloc( mm_pool_temp ,
                              sizeof( FNTYPE4 ) ,
                              MM_ALLOC_CLASS_FUNCTIONS ) ;
  if ( t4 == NULL )
    return error_handler( VMERROR ) ;

  fn->specific = ( fn_type_specific )t4 ;
  fn->evalproc = fntype4_evaluate_function ;
  fn->discontproc = fntype4_find_discontinuity ;
  fn->freeproc = fntype4_freecache ;

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
enum { fntype4_Length, fntype4_DataSource, fntype4_dummy } ;
static NAMETYPEMATCH fntype4_dict[fntype4_dummy + 1] = {
  { NAME_Length,                  1, { OINTEGER }},
  { NAME_DataSource,              1, { OFILE }},
  DUMMY_END_MATCH
} ;

Bool fntype4_unpack( FUNCTIONCACHE *fn , OBJECT *thes )
{
  corecontext_t *context = get_core_context_interp();
  FNTYPE4 *t4 ;
  OBJECT *ps_stream ;
  Hq32x2 filepos;
  Bool glmode;
  Bool result;

  HQASSERT( fn , "fn is null in fntype4_unpack" ) ;

  t4 = ( FNTYPE4 * )fn->specific ;
  HQASSERT( t4 , "t4 is null in fntype4_unpack" ) ;

  t4->ps_proc = onothing;   /* Struct copy to set slot properties */

  /* Enforce presence of Range */
  if ( fn->out_dim == 0 )
    return error_handler( TYPECHECK ) ;

  if ( oType(*thes) == ODICTIONARY ) {
    if ( ! dictmatch( thes , fntype4_dict ))
      return FALSE ;
    ps_stream = fntype4_dict[fntype4_DataSource].result ;
  }
  else {
    HQASSERT( oType(*thes) == OFILE ,
              "thes is file or dict in fntype4_unpack" );
    ps_stream = thes ;
  }

  /* When the array is created below, make it a global array. This is a hack to
   * allow one usage (PDF soft masks) to be retained after the restore at the
   * end of the page and available during compositing.
   */
  glmode = setglallocmode(context, TRUE) ;

  /* This is expected to convert the stream into an executable aarray */
  result = setup_pending_exec( ps_stream , TRUE );

  setglallocmode(context, glmode);

  if ( ! result )
    return FALSE ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  if ( ! oCanExec(*theTop(operandstack)) ||
       ! oExecutable(*theTop(operandstack)) )
    return error_handler( INVALIDACCESS ) ;

  Copy( &t4->ps_proc , theTop( operandstack ) ) ;
  pop( &operandstack ) ;

  if ( oType(t4->ps_proc) != OARRAY &&
       oType(t4->ps_proc) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  Hq32x2FromInt32(&filepos, 0);
  if ( (FilterSetPos(oFile(*ps_stream), &filepos) == EOF) ||
       (Hq32x2CompareInt32(&filepos, 0) != 0) )
    return error_handler(IOERROR);

  return TRUE ;
}



/*----------------------------------------------------------------------------*/
static void fntype4_freecache( FUNCTIONCACHE *fn )
{
  FNTYPE4 *t4 ;
  HQASSERT( fn , "fn is null in fntype4_freecache" ) ;
  t4 = ( FNTYPE4 * )fn->specific ;
  mm_free( mm_pool_temp , ( mm_addr_t ) t4 , sizeof( FNTYPE4 )) ;
  fn->specific = NULL ;
  fn_invalidate_entry( fn ) ;
}

/*----------------------------------------------------------------------------*/

static Bool fntype4_evaluate_function( FUNCTIONCACHE *fn , Bool upwards ,
                                       SYSTEMVALUE *input , SYSTEMVALUE *output )
{
  OBJECT *theo ;
  FNTYPE4 *t4 ;
  SYSTEMVALUE tmp, lb , ub ;
  SYSTEMVALUE *out_ptr ;
  int32 n , i ;
  int32 oldstacksize, newstacksize;

  UNUSED_PARAM(Bool, upwards) ;

  HQASSERT( fn , "fn is null in fntype4_evaluate_function" ) ;
  HQASSERT( input , "input is null in fntype4_evaluate_function" ) ;
  HQASSERT( output , "output is null in fntype4_evaluate_function" ) ;

  t4 = ( FNTYPE4 * )fn->specific ;
  HQASSERT( t4 , "t4 is null in fntype4_evaluate_function" ) ;

  /* need to setup the postscript dictionaries:
   * input variables -> initial operand stack
   * output variables -> items remaining on operand stack after execution
   * It is an error for # remaining operands to different from # output
   * variables.
   */

  currfileCache = NULL ;
  oldstacksize = theStackSize( operandstack );

  if ( fn->in_dim <= 4 ) {
    SYSTEMVALUE *domain = fn->s_domain ;

    for ( i = 0 ; i < fn->in_dim ; ++i ) {
      tmp = input[i] ;
      lb = domain[0] ;
      ub = domain[1] ;
      domain += 2 ;

      /* Clip input values to the domain of the function. */
      fn_cliptointerval( tmp , lb , ub ) ;

      if ( ! stack_push_real(tmp, &operandstack))
        return FALSE ;
    }
  } else {
    OBJECT *domain = oArray(*fn->domain) ;

    HQASSERT( fn->domain , "fn->domain is null." ) ;
    HQASSERT( oType(*fn->domain) == OARRAY ||
              oType(*fn->domain) == OPACKEDARRAY ,
              "fn->domain is not an array." ) ;

    for ( i = 0 ; i < fn->in_dim ; ++i ) {
      tmp = input[i] ;
      if ( !object_get_numeric(domain++, &lb) ||
           !object_get_numeric(domain++, &ub) )
        return FALSE ;

      /* Clip input values to the domain of the function. */
      fn_cliptointerval( tmp , lb , ub ) ;

      if ( ! stack_push_real(tmp, &operandstack))
        return FALSE ;
    }
  }

  if ( ! push( &t4->ps_proc , & executionstack ))
    return FALSE ;

  if ( ! interpreter( 1 , NULL ))
    return FALSE;

  newstacksize = theStackSize( operandstack ) ;
  HQASSERT( newstacksize > oldstacksize, "no results in fntype4_evaluate") ;

  n = newstacksize - oldstacksize;
  if ( n != fn->out_dim )
    return error_handler( RANGECHECK ) ;

  /*
   * extract the outputs from the operand stack and insert them into
   * the output array.  Need to do this in reverse order: the top of the
   * stack is the last output value.
   */

  out_ptr = output + fn->out_dim ;
  if ( fn->out_dim <= 4 ) {
    SYSTEMVALUE *rng_ptr = fn->s_range ;
    HQASSERT( rng_ptr , "fn->s_range null in fntype4_evaluate_function" );
    for ( i = 0 ; i < n ; ++i ) {
      theo = TopStack( operandstack , newstacksize-- ) ;
      if ( !object_get_numeric(theo, &tmp) )
        return FALSE ;
      lb = rng_ptr[0] ;
      ub = rng_ptr[1] ;
      rng_ptr += 2 ;
      fn_cliptointerval(tmp, lb, ub) ;
      *--out_ptr = tmp ;
      pop( & operandstack ) ;
    }
  }
  else {
    OBJECT *rng_ptr = fn->range ;
    HQASSERT( rng_ptr, "fn->range null in fntype4_evaluate_function" );
    for ( i = 0 ; i < n ; ++i ) {
      theo = TopStack( operandstack , newstacksize-- ) ;
      if ( !object_get_numeric(theo, &tmp) ||
           !object_get_numeric(rng_ptr++, &lb) ||
           !object_get_numeric(rng_ptr++, &ub) )
        return FALSE ;
      fn_cliptointerval(tmp, lb, ub) ;
      *--out_ptr = tmp ;
      pop ( & operandstack ) ;
    }
  }

  return TRUE ;
}




/*----------------------------------------------------------------------------*/
static Bool fntype4_find_discontinuity( FUNCTIONCACHE *fn ,
                                       int32 index ,
                                       SYSTEMVALUE bounds[],
                                       SYSTEMVALUE *discontinuity , int32 *order )
{
  UNUSED_PARAM(FUNCTIONCACHE *, fn) ;
  UNUSED_PARAM(int32, index) ;
  UNUSED_PARAM(SYSTEMVALUE *, bounds) ;
  UNUSED_PARAM(SYSTEMVALUE *, discontinuity) ;
  UNUSED_PARAM(int32 *, order) ;
  /* don't know what to put here, so return TRUE for now. */
  return TRUE ;
}

/*----------------------------------------------------------------------------*/


/* Log stripped */
