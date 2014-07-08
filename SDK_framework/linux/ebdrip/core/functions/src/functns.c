/** \file
 * \ingroup funcs
 *
 * $HopeName: COREfunctions!src:functns.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Function evaluation for PS and PDF function dictionaries.
 */

#include <float.h>        /* FLT_EPSILON */

#include "core.h"

#include "coreinit.h"     /* core_init_fns */
#include "dictscan.h"     /* dictmatch */
#include "fileio.h"       /* isIInputFile */
#include "graphics.h"     /* GSTATE (for gstateptr->colorInfo) */
#include "gstate.h"       /* gstateptr */
#include "gschtone.h"     /* gsc_gethalftonedict */
#include "gscsmplk.h"     /* gsc_getblackgenerationid */
#include "gscxfer.h"      /* gsc_gettransferid */
#include "mm.h"           /* mm_alloc */
#include "lowmem.h"       /* low_mem_handler_t */
#include "namedef_.h"     /* NAME_HalftoneType */
#include "objectt.h"      /* OBJECT */
#include "stream.h"       /* streamLookupDict */
#include "swerrors.h"     /* TYPECHECK */

#include "functns.h"

#include "fnpriv.h"
#include "fntype0.h"
#include "fntype2.h"
#include "fntype3.h"
#include "fntype4.h"

/* -------------------------------------------------------------------------- */
/* Global variable used to turn function tracing on */
#if defined( ASSERT_BUILD )
Bool trace_fn = FALSE;
#endif

/* Stitching functions may go recursive. */
#define FN_RECURSE_DEPTH 32

/* The number of slots for each usage. New uses must be included in
 * the cacheinfo structure in functns.c. */
#define FN_HT_SIZE       20
#define FN_BG_SIZE        1
#define FN_UCR_SIZE       1
#define FN_SPOT_SIZE      1
#define FN_TR_SIZE        4
#define FN_SHADING_SIZE  10
#define FN_SHOPACITY_SIZE 1
#define FN_CIE_TINT_SIZE  1
#define FN_TINT_SIZE      2 /* one for tints one for recombine sep detection */
#define FN_SOFTMASK_SIZE  1
#define FN_EVALFUNC_SIZE  1

/* Index into the functioncache + functioninfo arrays. */
enum {
  FN_HT_BASE = 0,
  FN_BG_BASE = FN_HT_BASE + FN_HT_SIZE,
  FN_UCR_BASE = FN_BG_BASE + FN_BG_SIZE,
  FN_TR_BASE = FN_UCR_BASE + FN_UCR_SIZE,
  FN_SPOT_BASE = FN_TR_BASE + FN_TR_SIZE,
  FN_SHADING_BASE = FN_SPOT_BASE + FN_SPOT_SIZE,
  FN_SHOPACITY_BASE = FN_SHADING_BASE + FN_SHADING_SIZE,
  FN_CIE_TINT_BASE = FN_SHOPACITY_BASE + FN_SHOPACITY_SIZE,
  FN_TINT_BASE = FN_CIE_TINT_BASE + FN_CIE_TINT_SIZE,
  FN_SOFTMASK_BASE = FN_TINT_BASE + FN_TINT_SIZE,
  FN_EVALFUNC_BASE = FN_SOFTMASK_BASE + FN_SOFTMASK_SIZE,
  /* FN_CACHE_SIZE must be the last entry */
  FN_CACHE_SIZE = FN_EVALFUNC_BASE + FN_EVALFUNC_SIZE
} ;

/* -------------------------------------------------------------------------- */
typedef struct FN_CACHE_INFO {
  int32 num_slots ;
  int32 cache_offset ;
  fn_validate_proc validateproc ;
} FN_CACHE_INFO ;

/* -------------------------------------------------------------------------- */
static FUNCTIONCACHE * fn_cache_entry( OBJECT *function ,
                                       int32 usage , int32 offset ,
                                       int32 firstgen , int32 secondgen ,
                                       int32 *ptruncated_offset ,
                                       void *data ) ;

static Bool fn_validate_transfer( FUNCTIONCACHE *fn , void *data ) ;
static Bool fn_validate_bg( FUNCTIONCACHE *fn , void *data ) ;
static Bool fn_validate_ucr( FUNCTIONCACHE *fn , void *data ) ;
static Bool fn_validate_spot( FUNCTIONCACHE *fn , void *data ) ;
static Bool fn_validate_shading( FUNCTIONCACHE *fn , void *data ) ;
static Bool fn_validate_shopacity( FUNCTIONCACHE *fn , void *data ) ;
static Bool fn_validate_tint( FUNCTIONCACHE *fn , void *data ) ;
static Bool fn_validate_cie_tint( FUNCTIONCACHE *fn , void *data ) ;
static Bool fn_validate_evalfunc( FUNCTIONCACHE *fn , void *data ) ;

static void fn_spread_encode_input( FUNCTIONCACHE *fn, Bool upwards, SYSTEMVALUE *value ) ;
static void fn_spread_decode_discontinuity( FUNCTIONCACHE *fn, SYSTEMVALUE* discont, int32 *order, SYSTEMVALUE bounds[ 2 ] ) ;

/* -------------------------------------------------------------------------- */
/* cacheinfo table contains the number of slots in the function cache
 * allocated for a particular use, the offset into the function cache,
 * and the routine used to validate the function (eg in/out dimension,
 * domain and range).
 * Table only has to go as far as FN_NUMBER_OF_PDF_USES in enumeration.
 */
static FN_CACHE_INFO gfn_cacheinfo[] = {
  /* 0 */ { FN_HT_SIZE       , FN_HT_BASE       , fn_validate_transfer } ,
  /* 1 */ { FN_BG_SIZE       , FN_BG_BASE       , fn_validate_bg       } ,
  /* 2 */ { FN_UCR_SIZE      , FN_UCR_BASE      , fn_validate_ucr      } ,
  /* 3 */ { FN_TR_SIZE       , FN_TR_BASE       , fn_validate_transfer } ,
  /* 4 */ { FN_SPOT_SIZE     , FN_SPOT_BASE     , fn_validate_spot     } ,
  /* 5 */ { FN_SHADING_SIZE  , FN_SHADING_BASE  , fn_validate_shading  } ,
  /* 6 */ { FN_SHOPACITY_SIZE, FN_SHOPACITY_BASE, fn_validate_shopacity } ,
  /* 7 */ { FN_CIE_TINT_SIZE , FN_CIE_TINT_BASE , fn_validate_cie_tint } ,
  /* 8 */ { FN_TINT_SIZE     , FN_TINT_BASE     , fn_validate_tint     } ,
  /* 9 */ { FN_SOFTMASK_SIZE , FN_SOFTMASK_BASE , fn_validate_transfer } ,
  /*10 */ { FN_EVALFUNC_SIZE , FN_EVALFUNC_BASE , fn_validate_evalfunc }
} ;

/* Function cache contains all the unpacked functions */
static FUNCTIONCACHE gfn_functioncache[ FN_CACHE_SIZE ] = { 0 } ;


static low_mem_handler_t fn_cache_handler;


/** Function cache initialization */
static Bool fn_cache_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  return low_mem_handler_register(&fn_cache_handler);
}


/** Function cache finishing */
static void fn_cache_finish(void)
{
  low_mem_handler_deregister(&fn_cache_handler);
}


static void init_C_globals_functns(void)
{
  FUNCTIONCACHE functioncache_init = { 0 } ;
  int i ;

#if defined(ASSERT_BUILD)
  trace_fn = FALSE ;
#endif

  for ( i = 0 ; i < FN_CACHE_SIZE ; ++i )
    gfn_functioncache[i] = functioncache_init ;
}


void functions_C_globals(core_init_fns *fns)
{
  init_C_globals_functns() ;
  fns->swstart = fn_cache_swstart;
  fns->finish = fn_cache_finish;
}


/* -------------------------------------------------------------------------- */
/* evalfunc_()       A ScriptWorks special PostScript operator (i.e. not PLRM)
 * Operands (top of stack first):-
 *  o   function dictionary;
 *  o ..  n numbers (must equal number of pairs in Domain in function dict.)
 *
 * If successful, removes all input operands off the stack and pushes the
 * output numbers on to the stack.  The number of outputs pushed on depends
 * entirely on the function dictionary itself.
 */
Bool evalfunc_(ps_context_t *pscontext)
{
  FUNCTIONCACHE *fn;
  SYSTEMVALUE s_input[4], s_output[4];
  SYSTEMVALUE *in_ptr, *out_ptr;
  Bool truncated_offset = FALSE;

  OBJECT *pDict;
  OBJECT *pObj;
  Bool ok = TRUE;
  int32 inx;
  int32 size;

  int32 usage = FN_EVALFUNC_OP;
  int32 offset = 0;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* The top object on the operand stack must be the function dictionary. */
  size = theStackSize( operandstack );
  if (size < 0)
    return error_handler( STACKUNDERFLOW );

  pDict = theTop( operandstack );
  if (oType(*pDict) != ODICTIONARY)
    return error_handler( TYPECHECK );

  /* Stop our cache slot from being purged while we're using it */
  fn_lock( usage, offset );

  /* Validate the function dictionary, unpack it, and cache it.  This
     will then enable us to allocate memory for output results before
     actually evaluating the function. */
  fn = fn_cache_entry( pDict, usage, offset,
                       FN_GEN_DEFAULT, FN_GEN_NA,
                       &truncated_offset,
                       NULL );
  if (fn == NULL) {
    ok = FALSE;
  } else {

    /* Type 4 functions are only expected from PDF*/
    if (fn->fntype == 4) {
      ok = error_handler( TYPECHECK );

      /* Now that we know the number of input parameters, check the stack size. */
    } else if ( size < fn->in_dim ) {
      ok = error_handler( STACKUNDERFLOW );

    } else {
      /* If the number of inputs/outputs is 4 or less, we can use local
         arrays, otherwise use the arrays malloc'd by fn_cache_entry(). */
      in_ptr  = (fn->in_dim  <= 4) ? s_input  : fn->input;
      out_ptr = (fn->out_dim <= 4) ? s_output : fn->output;

      /* Load up the input values from operands on the stack. */
      ok = TRUE;
      for (inx = 0; inx < fn->in_dim && ok; inx++) {
        pObj = stackindex( inx + 1, &operandstack );
        ok = object_get_numeric( pObj, &in_ptr[inx] );
      }

      /* Perform the evaluation. */
      if (ok) {
        ok = (*fn->evalproc)( fn, TRUE, in_ptr, out_ptr );
        if (ok) {

          /* Remove all input parameters from the stack, including the dictionary. */
          npop( fn->in_dim + 1, &operandstack );

          /* Push output results onto the stack */
          for (inx = 0; inx < fn->out_dim && ok; inx++) {
            ok = stack_push_real(out_ptr[inx], &operandstack);
          }
        }
      }
    }

    /* Unless we can find a way to ensure the function dictionary is
       exactly the same in between successive calls to this operator, we
       have to release the cached entry since a key-by-key comparison is
       just as good as re-unpacking it, etc. */
    fn_invalidate_entry( fn );
  }

  fn_unlock( usage, offset );

  return ok ;
}

/* -------------------------------------------------------------------------- */
/* Description of arguments to fn_evaluate:
 *
 * function                dictionary for PS and a file for PDF
 * input                   input values (function arguments)
 * output                  output values, allocated by caller
 * usage                   spot function, ucr, bg (see header for full list)
 * offset                  0+, select cache slot when num of slots > 1 for this use
 * firstgen                typically color generation
 * secondgen               typically repro generation
 * data                    used for validating the Function:
 *                          expected output dimension for tint transform
 *                          shfill domain - function domain must be a superset
 */
Bool fn_evaluate( OBJECT *function ,
                  USERVALUE *input , USERVALUE *output ,
                  int32 usage , int32 offset ,
                  int32 firstgen , int32 secondgen ,
                  void *data )
{
  return fn_evaluate_with_direction( function ,
                                     input , output ,
                                     TRUE /* upwards */ ,
                                     usage , offset ,
                                     firstgen , secondgen ,
                                     data ) ;
}

/* -------------------------------------------------------------------------- */
/* Description of arguments to fn_evaluate_with_direction:
 *
 * function                dictionary for PS and a file for PDF
 * upwards                 Evaluates x + epsilon at a discontinuity
 * input                   input values (function arguments)
 * output                  output values, allocated by caller
 * usage                   spot function, ucr, bg (see header for full list)
 * offset                  0+, select cache slot when num of slots > 1 for this use
 * firstgen                typically color generation
 * secondgen               typically repro generation
 * data                    used for validating the Function:
 *                          expected output dimension for tint transform
 *                          shfill domain - function domain must be a superset
 */
Bool fn_evaluate_with_direction( OBJECT *function ,
                                 USERVALUE *input , USERVALUE *output ,
                                 Bool upwards ,
                                 int32 usage , int32 offset ,
                                 int32 firstgen , int32 secondgen ,
                                 void *data )
{
  FUNCTIONCACHE *fn ;
  SYSTEMVALUE s_input[ 4 ] , s_output[ 4 ] ;
  SYSTEMVALUE *in_ptr , *out_ptr , *s_ptr ;
  Bool truncated_offset = FALSE ;
  int32 i ;

  HQASSERT( function , "function is null in fn_evaluate" ) ;
  HQASSERT( input , "input is null in fn_evaluate" ) ;
  HQASSERT( output , "output is null in fn_evaluate" ) ;
  HQASSERT( 0 <= usage && usage < FN_NUMBER_OF_PDF_USES ,
            "usage out of range in fn_evaluate" ) ;
  HQASSERT( FN_NUMBER_OF_PDF_USES ==
            sizeof(gfn_cacheinfo)/sizeof(*gfn_cacheinfo) ,
            "uses enum does not match gfn_cacheinfo length" ) ;
  HQASSERT( offset >= 0 , "-ve offset in fn_evaluate" ) ;
  HQASSERT( firstgen == FN_GEN_NA ||
            ( 0 < firstgen  && firstgen <= MAXINT32 ) ,
            "Not a valid color generation numbers in fn_evaluate" ) ;
  HQASSERT( secondgen == FN_GEN_NA ||
            ( 0 < secondgen  && secondgen <= MAXINT32 ) ,
            "Not a valid repro generation numbers in fn_evaluate" ) ;
  HQASSERT( firstgen != FN_GEN_NA || secondgen != FN_GEN_NA ,
            "Neither generation numbers are used in fn_evaluate" ) ;

  /* Stop this function from being purged whilst we're using it */
  fn_lock( usage , offset ) ;

  fn = fn_cache_entry( function ,
                       usage , offset ,
                       firstgen , secondgen ,
                       & truncated_offset ,
                       data ) ;
  if ( fn == NULL ) {
    fn_unlock( usage , offset ) ;
    return FALSE ;
  }

  in_ptr =  ( fn->in_dim <= 4 )  ? s_input  : fn->input ;
  out_ptr = ( fn->out_dim <= 4 ) ? s_output : fn->output ;

  s_ptr = in_ptr ;
  for ( i = 0 ; i < fn->in_dim ; ++i )
    *s_ptr++ = ( SYSTEMVALUE ) *input++ ;

  if ( fn->spreadmethod != NAME_None && fn->spreadfactor != 1 )
    fn_spread_encode_input( fn , upwards , in_ptr ) ;

  if ( ! (*fn->evalproc)( fn , upwards , in_ptr , out_ptr )) {
    fn_unlock( usage , offset ) ;
    return FALSE ;
  }

  for ( i = 0 ; i < fn->out_dim ; ++i )
    *output++ = ( USERVALUE ) *out_ptr++ ;

  if ( truncated_offset )
    /* Cache slot use only temporary, force a cache next time */
    fn_invalidate_entry( fn ) ;

  fn_unlock( usage , offset ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool fn_find_discontinuity( OBJECT *function ,
                            int32 index ,
                            USERVALUE bounds[],
                            USERVALUE *discontinuity , int32 *order ,
                            int32 usage , int32 offset ,
                            int32 firstgen , int32 secondgen ,
                            void *data )
{
  FUNCTIONCACHE *fn ;
  SYSTEMVALUE sbounds[2], sdiscontinuity ;
  int32 truncated_offset = FALSE ;

  HQASSERT( function , "function is null in fn_find_discontinuity." ) ;
  HQASSERT( discontinuity , "discontinuity is null in fn_find_discontinuity." ) ;
  HQASSERT( 0 <= usage && usage < FN_NUMBER_OF_PDF_USES ,
            "usage out of range in fn_find_discontinuity" ) ;
  HQASSERT( FN_NUMBER_OF_PDF_USES ==
            sizeof(gfn_cacheinfo)/sizeof(*gfn_cacheinfo) ,
            "uses enum does not match gfn_cacheinfo length" ) ;
  HQASSERT( offset >= 0 , "-ve offset in fn_find_discontinuity" ) ;
  HQASSERT( firstgen == FN_GEN_NA ||
            ( 0 < firstgen  && firstgen <= MAXINT32 ) ,
            "Not a valid color generation number in fn_find_discontinuity." ) ;
  HQASSERT( secondgen == FN_GEN_NA ||
            ( 0 < secondgen  && secondgen <= MAXINT32 ) ,
            "Not a valid repro generation number in fn_find_discontinuity." ) ;
  HQASSERT( firstgen != FN_GEN_NA || secondgen != FN_GEN_NA ,
            "Neither generation numbers are used in fn_find_discontinuity." ) ;
  HQASSERT( bounds[ 0 ] != bounds[ 1 ] , "Interval is empty in fn_find_discontinuity." ) ;

  fn = fn_cache_entry( function ,
                       usage , offset ,
                       firstgen , secondgen ,
                       & truncated_offset ,
                       data ) ;
  if ( fn == NULL )
    return FALSE ;

  sbounds[ 0 ] = bounds[ 0 ] ;
  sbounds[ 1 ] = bounds[ 1 ] ;

  /* This is what we return if we don't find a discontinuity */
  sdiscontinuity = bounds[ 0 ] ;
  *order = -1 ;
  if ( ! (*fn->discontproc)( fn, index, sbounds, &sdiscontinuity, order ))
    return FALSE ;

  if ( fn->spreadmethod != NAME_None && fn->spreadfactor != 1 )
    fn_spread_decode_discontinuity( fn, &sdiscontinuity, order, sbounds ) ;

  *discontinuity = ( USERVALUE )sdiscontinuity ;

  if ( truncated_offset )
    /* Cache slot use only temporary, force a cache next time. */
    fn_invalidate_entry( fn ) ;

  return TRUE ;
}

/* ------------------------------------------------------------------------=- */
enum {
  fcd_FunctionType, fcd_Domain, fcd_Range,
  fcd_HqnSpreadMethod, fcd_HqnSpreadFactor,
  fcd_n_entries
} ;
static NAMETYPEMATCH function_common_dict[fcd_n_entries + 1] = {
  { NAME_FunctionType,        1, { OINTEGER }},
  { NAME_Domain ,             2, { OARRAY, OPACKEDARRAY }},
  { NAME_Range  | OOPTIONAL , 2, { OARRAY, OPACKEDARRAY }},
  /* Range optional, but required for type 0 and 4 functions */
  { NAME_HqnSpreadMethod | OOPTIONAL, 1, { ONAME } },
  { NAME_HqnSpreadFactor | OOPTIONAL, 1, { OINTEGER } },
  DUMMY_END_MATCH
} ;

Bool fn_unpack( OBJECT *theo , FUNCTIONCACHE *fn , int32 safe_recurse, int32 usage )
{
  FILELIST *flptr ;
  OBJECT *thes ;
  int32 len ;
  int32 fn_type ;
  int32 do_alloc ;

  HQASSERT( theo != NULL , "fn_unpack: theo null" ) ;
  HQASSERT( fn != NULL , "fn_unpack: fn null" ) ;

  if ( oType( *theo ) != OFILE && oType( *theo ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  /* Stitching functions may go recursive. */
  if ( safe_recurse == 0 )
    return error_handler( UNDEFINEDRESULT ) ;
  --safe_recurse ;

  thes = NULL ;
  flptr = NULL ;

  if ( oType( *theo ) == OFILE ) {
    flptr = oFile( *theo ) ;
    HQASSERT( flptr , "flptr is null in fn_unpack" ) ;

    if ( ! isIInputFile( flptr ) ||
         ! isIOpenFileFilter( theo , flptr ) ||
         ! isIRewindable( flptr ))
      return error_handler( IOERROR ) ;

    thes = theo ;
    /* Get dictionary associated with the stream and fill-in entries. */
    theo = streamLookupDict( theo ) ;
    if ( theo == NULL )
      return error_handler(UNDEFINED) ;
    HQASSERT( oType( *theo ) == ODICTIONARY , "Must be an ODICTIONARY in fn_unpack" ) ;
  }

  if ( !oCanRead(*oDict(*theo)) && !object_access_override(oDict(*theo)) )
    return error_handler(INVALIDACCESS) ;

  if ( ! dictmatch( theo , function_common_dict ))
    return FALSE ;

  fn_type = oInteger(*function_common_dict[fcd_FunctionType].result) ;

  /* May be able to re-use existing function type specific data structure. */
  do_alloc = ( fn_type != fn->fntype || fn->specific == 0 ) ;

  if ( fn_type != fn->fntype && fn->specific != 0 )
    /* New function different from old, therefore free old before
     * allocating new. */
    (*fn->freeproc)( fn ) ;

  fn->fntype = fn_type ;

  /* A function can be repeated or reflected a number of times across (the
     number is controlled by the integer spreadfactor).  This allows efficient
     implementation of spread methods in radial gradients. */
  if ( function_common_dict[fcd_HqnSpreadMethod].result ) {
    fn->spreadmethod = oNameNumber(*function_common_dict[fcd_HqnSpreadMethod].result) ;
    switch ( fn->spreadmethod ) {
    case NAME_Repeat :
    case NAME_Reflect :
      break ;
    default :
      return error_handler( RANGECHECK ) ;
    }
  } else {
    fn->spreadmethod = NAME_None ;
  }

  /* The number of times the function is run across the domain. */
  if ( function_common_dict[fcd_HqnSpreadFactor].result ) {
    fn->spreadfactor = oInteger(*function_common_dict[fcd_HqnSpreadFactor].result) ;
    /* A value of zero doesn't make sense - the default is one. */
    if (fn->spreadfactor == 0)
      return error_handler(RANGECHECK);
  } else {
    fn->spreadfactor = 1 ;
  }

  fn->usage = usage ;

  /* Calculate the dimension of the domain. */
  len = theLen(*function_common_dict[fcd_Domain].result) ;
  if ( ( len & 1 ) != 0 )
    return error_handler( RANGECHECK ) ;
  if ( len == 0 )
    return error_handler( RANGECHECK ) ;
  fn->in_dim =  len >> 1 ;

  if ( fn->in_dim > 4 ) {
    if ( ! fn_typecheck_array(function_common_dict[fcd_Domain].result) )
      return FALSE ;
    fn->domain = function_common_dict[fcd_Domain].result ;
  } else {
    if ( ! object_get_numeric_array(function_common_dict[fcd_Domain].result,
                                    fn->s_domain, len) )
      return FALSE ;
  }

  /* Calculate the dimension of the range. */
  if ( function_common_dict[fcd_Range].result ) {
    len = theLen(*function_common_dict[fcd_Range].result) ;
    if ( ( len & 1 ) != 0 )
      return error_handler( RANGECHECK ) ;
    if ( len == 0 )
      return error_handler( RANGECHECK ) ;
    fn->out_dim =  len >> 1 ;
    if ( fn->out_dim > 4 ) {
      if ( ! fn_typecheck_array(function_common_dict[fcd_Range].result) )
        return FALSE ;
    } else {
      if ( ! object_get_numeric_array(function_common_dict[fcd_Range].result,
                                      fn->s_range, len) )
        return FALSE ;
    }
    fn->range = function_common_dict[fcd_Range].result ;
  } else {
    /* Range required for type 0 and 4 functions */
    if ( fn_type == 0 || fn_type == 4 )
      return error_handler( TYPECHECK ) ;
    fn->out_dim = 0 ;
    fn->range = NULL ;
  }

  /* Now allocate and initialise the function type specific data
   * structure and unpack the type specific information into it.
   */

  switch ( fn_type ) {
  case 0 :
    {
      if ( do_alloc && ! fntype0_initcache( fn ))
        return FALSE ;
      return fntype0_unpack( fn , theo , thes , flptr ) ;
    }
  case 2 :
    {
      if ( do_alloc && ! fntype2_initcache( fn ))
        return FALSE ;
      return fntype2_unpack( fn , theo ) ;
    }
  case 3 :
    {
      if ( do_alloc && ! fntype3_initcache( fn ))
        return FALSE ;
      return fntype3_unpack( fn , theo , safe_recurse ) ;
    }
  case 4 :
    {
      if ( do_alloc && ! fntype4_initcache( fn ))
        return FALSE ;
      return fntype4_unpack( fn , thes ) ;
    }
  default :
    return error_handler( RANGECHECK ) ;
  }
  /* NOT REACHED */
}

/* -------------------------------------------------------------------------- */
static FUNCTIONCACHE * fn_cache_entry( OBJECT *function ,
                                       int32 usage , int32 offset ,
                                       int32 firstgen , int32 secondgen ,
                                       Bool *ptruncated_offset ,
                                       void *data )
{
  FUNCTIONCACHE *fn ;
  Bool truncated_offset = FALSE ;

  HQASSERT( function != NULL , "fn_cache_entry: function null" ) ;

  if ( offset >= gfn_cacheinfo[ usage ].num_slots ) {
    /* Run out of slots, therefore re-use last slot (N-Color) */
    offset = gfn_cacheinfo[ usage ].num_slots - 1 ;
    fn_invalidate( usage , offset ) ;
    truncated_offset = TRUE ;
  }

  fn = & gfn_functioncache[ gfn_cacheinfo[ usage ].cache_offset + offset ] ;

  if ( fn->firstgen != firstgen || fn->secondgen != secondgen ) {
    int32 in_dim = fn->in_dim ;
    int32 out_dim = fn->out_dim ;

    HQASSERT( gfn_cacheinfo[ usage ].validateproc != NULL ,
              "fn_cache_entry: validateproc null" ) ;
    if ( ! fn_unpack( function , fn , FN_RECURSE_DEPTH, usage ) ||
         ! (*(gfn_cacheinfo[ usage ].validateproc))( fn , data )) {
      fn_invalidate_entry( fn ) ;
      /* Restore consistency to the memory allocations below */
      fn->in_dim = in_dim ;
      fn->out_dim = out_dim ;
      return NULL ;
    }

    fn->firstgen = firstgen ;
    fn->secondgen = secondgen ;

    /* Allocate input/output double precision arrays if in/out
     * dimension is greater than 4.
     */
    if ( in_dim > 4 && in_dim != fn->in_dim && fn->input != NULL ) {
      mm_free( mm_pool_temp ,
               ( mm_addr_t ) fn->input ,
               in_dim * sizeof( SYSTEMVALUE )) ;
      fn->input = NULL ;
    }
    if ( out_dim > 4 && out_dim != fn->out_dim && fn->output != NULL ) {
      mm_free( mm_pool_temp ,
               ( mm_addr_t ) fn->output ,
               out_dim * sizeof( SYSTEMVALUE )) ;
      fn->output = NULL ;
    }
    if ( fn->in_dim > 4 && fn->input == NULL ) {
      fn->input = ( SYSTEMVALUE * ) mm_alloc( mm_pool_temp ,
                                              fn->in_dim * sizeof( SYSTEMVALUE ) ,
                                              MM_ALLOC_CLASS_FUNCTIONS ) ;
      if ( fn->input == NULL ) {
        fn_invalidate_entry( fn ) ;
        ( void )error_handler( VMERROR ) ;
        return NULL ;
      }
    }
    if ( fn->out_dim > 4 && fn->output == NULL ) {
      fn->output = ( SYSTEMVALUE * ) mm_alloc( mm_pool_temp ,
                                               fn->out_dim * sizeof( SYSTEMVALUE ) ,
                                               MM_ALLOC_CLASS_FUNCTIONS ) ;
      if ( fn->output == NULL ) {
        mm_free( mm_pool_temp ,
                 ( mm_addr_t ) fn->input ,
                 fn->in_dim * sizeof( SYSTEMVALUE )) ;
        fn->input = NULL ;
        fn_invalidate_entry( fn ) ;
        ( void )error_handler( VMERROR ) ;
        return NULL ;
      }
    }
  }

  *ptruncated_offset = truncated_offset ;

  return fn ;
}

/* -------------------------------------------------------------------------- */
Bool fn_extract_from_alternate( OBJECT *longerone ,
                                SYSTEMVALUE *shorterone ,
                                int32 shorterlen , int len ,
                                int32 index,
                                SYSTEMVALUE *result )
{
  HQASSERT( shorterone , "shorterone NULL in fn_extract_from_alternate" ) ;
  HQASSERT( result , "result NULL in fn_extract_from_alternate" ) ;
  HQASSERT( index < len , "index out of bounds in fn_extract_from_alternate" ) ;

  if ( len <= shorterlen ) {
    *result = shorterone[index] ;
  } else {
    HQASSERT( longerone , "longerone NULL in fn_extract_from_alternate" ) ;
    HQASSERT( oType( *longerone ) == OARRAY || oType( *longerone ) == OPACKEDARRAY ,
              "longerone is not an array in fn_extract_from_alternate" ) ;
    HQASSERT( theLen(*longerone) == len ,
              "longerone not expected length in fn_extract_from_alternate" ) ;
    if ( !object_get_numeric(oArray(*longerone) + index, result) )
      return FALSE ;
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool fn_check_bounds( SYSTEMVALUE bounds[],
                      SYSTEMVALUE domain[],
                      SYSTEMVALUE *discontinuity,
                      int32 *order )
{
  if ( domain[0] > bounds[0] ) {
    if ( domain[0] < bounds[1] ) {
      /* Got one. */
      *order = 0 ;
      *discontinuity = domain[0] ;
    }
    /* else the bounds are outside the domain, so there are no discontinuities. */
    return TRUE ;
  } else if ( domain[1] < bounds[1] ) {
    if ( domain[1] > bounds[0] ) {
      /* We've found one */
      *order = 0 ;
      *discontinuity = domain[1] ;
    }
    /* else the bounds are outside the domain, so there are no discontinuities. */
    return TRUE ;
  }
  return FALSE ;
}


/* -------------------------------------------------------------------------- */
/* Checks arrayobj contains only integers or reals */
Bool fn_typecheck_array( OBJECT *arrayobj )
{
  OBJECT *theo ;
  int32 len ;
  int32 i ;

  HQASSERT( arrayobj , "fn_typecheck_array: arrayobj null" ) ;
  HQASSERT( oType( *arrayobj ) == OARRAY || oType( *arrayobj ) == OPACKEDARRAY ,
            "fn_typecheck_array: arrayobj is not an array" ) ;
  HQASSERT( theLen(*arrayobj) > 0 , "fn_typecheck_array: arrayobj length <= 0" ) ;

  len = theLen(*arrayobj) ;
  theo = oArray( *arrayobj ) ;

  for ( i = 0 ; i < len ; ++i ) {
    if ( oType( *theo ) != OINTEGER && oType( *theo ) != OREAL )
      return error_handler( TYPECHECK ) ;
    ++theo ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Returns the unpacked function entry associated with the usage and offset.
   Aasumes the entry is valid */
static FUNCTIONCACHE * fn_select_entry( int32 usage , int32 offset )
{
  FUNCTIONCACHE *fn ;

  HQASSERT( 0 <= usage && usage < FN_NUMBER_OF_PDF_USES ,
            "fn_select_entry: usage out of range" ) ;
  HQASSERT( offset >= 0 , "fn_select_entry: -ve offset" ) ;

  if ( offset >= gfn_cacheinfo[ usage ].num_slots )
    /* Run out of slots, therefore re-use last slot (N-Color). */
    offset = gfn_cacheinfo[ usage ].num_slots - 1 ;

  fn = & gfn_functioncache[ gfn_cacheinfo[ usage ].cache_offset + offset ] ;
  HQASSERT( fn , "fn_select_entry: fn null" ) ;

  return fn ;
}

/* -------------------------------------------------------------------------- */
/* Explicitly invalidate a cache slot, as an alternative to relying on
   firstgen and secondgen (usually colorgen and reprogen respectively) */
void fn_invalidate( int32 usage , int32 offset )
{
  FUNCTIONCACHE *fn ;

  /* Forces the next call to fn_evaluate to load the function
   * into the cache */
  fn = fn_select_entry( usage , offset ) ;
  HQASSERT( fn , "fn_invalidate: fn null" ) ;

  fn_invalidate_entry( fn ) ;
}

/* -------------------------------------------------------------------------- */
/* fn_lock clears the flag in the cache struct to control the
   behaviour of fn_purge (called by the low memory handler).
   For use by spot functions and shading functions only  */
void fn_lock( int32 usage , int32 offset )
{
  FUNCTIONCACHE *fn ;

  fn = fn_select_entry( usage , offset ) ;
  HQASSERT( fn , "fn_lock: fn null" ) ;

  fn->lock = TRUE ;
}

/* -------------------------------------------------------------------------- */
/* fn_unlock sets the flag in the cache struct to control the
   behaviour of fn_purge (called by the low memory handler).
   For use by spot functions and shading functions only  */
void fn_unlock( int32 usage , int32 offset )
{
  FUNCTIONCACHE *fn ;

  fn = fn_select_entry( usage , offset ) ;
  HQASSERT( fn , "fn_unlock: fn null" ) ;

  fn->lock = FALSE ;
}

/* -------------------------------------------------------------------------- */
/* Free a complete function cache. This is only used when purging an entry
  in low memory conditions. Normally function caches are freed lazily when
  a new function is unpacked */
static void fn_freecache( FUNCTIONCACHE *fn )
{
  HQASSERT( fn != NULL , "fn_freecache: fn null" ) ;

  /* Free the function type specific data */
  HQASSERT( fn->freeproc != NULL ,
    "fn_freecache: freeproc null, yet specific is not null" ) ;
  (*(fn->freeproc))( fn ) ;

  /* Free the working doubles for in/out */
  if ( fn->in_dim > 4 ) {
    HQASSERT( fn->input , "fn_freecache: fn->input null" ) ;
    mm_free( mm_pool_temp ,
             ( mm_addr_t ) fn->input ,
             fn->in_dim * sizeof( SYSTEMVALUE )) ;
    fn->input = NULL ;
  }
  if ( fn->out_dim > 4 ) {
    HQASSERT( fn->output , "fn_freecache: fn->output null" ) ;
    mm_free( mm_pool_temp ,
             ( mm_addr_t ) fn->output ,
             fn->out_dim * sizeof( SYSTEMVALUE )) ;
    fn->output = NULL ;
  }
  fn_invalidate_entry( fn ) ;
}

/* -------------------------------------------------------------------------- */
/* Purges the function cache of the unpacked functions. Purging action
   depends on what the function is used for. Eg in the case of spot
   function it does not make sense to purge. */
static unsigned fn_purge(Bool do_free)
{
  FUNCTIONCACHE *fn = gfn_functioncache ;
  OBJECT *theo ;
  unsigned removed = 0, i;
  int32 gen;
  Bool transfer_needed = FALSE;

  /* Purge halftone transfer functions if current halftone is not type 5 */
  theo = gsc_gethalftonedict( gstateptr->colorInfo ) ;
  if ( oType( *theo ) == ODICTIONARY ) {
    theo = fast_extract_hash_name( theo , NAME_HalftoneType ) ;
    HQASSERT( theo != NULL , "halftonetype not defined in halftone dict" ) ;
    transfer_needed = oInteger(*theo) == 5 || oInteger(*theo) == 195;
  }
  gen = gsc_gethalftoneid( gstateptr->colorInfo ) ;
  for ( i = FN_HT_BASE ; i < FN_BG_BASE ; ++i ) {
    if ( fn->specific != 0 && ! fn->lock
         && ( !transfer_needed || fn->firstgen != gen )) {
      if ( do_free )
        fn_freecache(fn);
      ++removed ;
    }
    ++fn ;
  }
  /* Black generataion and under-color removal */
  gen = gsc_getblackgenerationid( gstateptr->colorInfo ) ;
  for ( i = FN_BG_BASE ; i < FN_UCR_BASE ; ++i ) {
    if ( fn->specific != 0 && ! fn->lock && fn->firstgen != gen ) {
      if ( do_free )
        fn_freecache(fn);
      ++removed ;
    }
    ++fn ;
  }
  gen = gsc_getundercolorremovalid( gstateptr->colorInfo ) ;
  for ( i = FN_UCR_BASE ; i < FN_TR_BASE ; ++i ) {
    if ( fn->specific != 0 && ! fn->lock && fn->firstgen != gen ) {
      if ( do_free )
        fn_freecache(fn);
      ++removed ;
    }
    ++fn ;
  }
  /* Transfer functions */
  gen = gsc_gettransferid( gstateptr->colorInfo ) ;
  for ( i = FN_TR_BASE ; i < FN_SPOT_BASE ; ++i ) {
    if ( fn->specific != 0 && ! fn->lock && fn->firstgen != gen ) {
      if ( do_free )
        fn_freecache(fn);
      ++removed ;
    }
    ++fn ;
  }
  /* For spot functions and shading functions, only free the slots
   * which are not locked (ie not in current use) */
  for ( i = FN_SPOT_BASE ; i < FN_CIE_TINT_BASE ; ++i ) {
    if ( fn->specific != 0 && ! fn->lock ) {
      if ( do_free )
        fn_freecache(fn);
      ++removed ;
    }
    ++fn ;
  }
  /* Always purge the tint transforms (for now) */
  for ( i = FN_CIE_TINT_BASE ; i < FN_SOFTMASK_BASE ; ++i ) {
    if ( fn->specific != 0 && ! fn->lock ) {
      if ( do_free )
        fn_freecache(fn);
      ++removed ;
    }
    ++fn ;
  }
  /* Always purge the soft mask alpha transfer function */
  for ( i = FN_SOFTMASK_BASE ; i < FN_EVALFUNC_SIZE ; ++i ) {
    if ( fn->specific != 0 && ! fn->lock ) {
      if ( do_free )
        fn_freecache(fn);
      ++removed ;
    }
    ++fn ;
  }
  /* Always purge the evalfunc operator function */
  for ( i = FN_EVALFUNC_BASE ; i < FN_CACHE_SIZE ; ++i ) {
    if ( fn->specific != 0 && ! fn->lock ) {
      if ( do_free )
        fn_freecache(fn);
      ++removed ;
    }
    ++fn ;
  }
  return removed ;
}


/** Solicit method of the function cache low-memory handler. */
static low_mem_offer_t *fn_cache_solicit(low_mem_handler_t *handler,
                                         corecontext_t *context,
                                         size_t count,
                                         memory_requirement_t* requests)
{
  static low_mem_offer_t offer;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !context->between_operators || fn_purge(FALSE) == 0 )
    return NULL;
  offer.pool = NULL; /* depends */
  offer.offer_size = 64 * 1024; /* @@@@ */
  offer.offer_cost = 1.0; /* @@@@ */
  offer.next = NULL;
  return &offer;
}


/** Release method of the function cache low-memory handler. */
static Bool fn_cache_release(low_mem_handler_t *handler,
                             corecontext_t *context,
                             low_mem_offer_t *offer)
{
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  (void)fn_purge(TRUE);
  return TRUE;
}


static low_mem_handler_t fn_cache_handler = {
  "Function cache",
  memory_tier_ram, fn_cache_solicit, fn_cache_release, FALSE,
  0, FALSE };


/* -------------------------------------------------------------------------- */
static Bool fn_validate_transfer( FUNCTIONCACHE *fn , void *data )
{
  HQASSERT( data == NULL , "expected data is non-null in fn_validate_transfer" ) ;
  UNUSED_PARAM( void * , data ) ;
  if ( fn->in_dim != 1 || fn->out_dim != 1 ||
       fn->s_domain[ 0 ] != 0.0 || fn->s_domain[ 1 ] != 1.0 ||
       ( fn->range != NULL &&
         (fn->s_range[ 0 ] != 0.0 || fn->s_range[ 1 ] != 1.0) ))
    return error_handler( RANGECHECK ) ;
  return TRUE ;
}

/* ------------------------------------------------------------------------- */
static Bool fn_validate_bg( FUNCTIONCACHE *fn , void *data )
{
  HQASSERT( data == NULL , "expected data is non-null in fn_validate_bg" ) ;
  UNUSED_PARAM( void * , data ) ;
  if ( fn->in_dim != 1 || fn->out_dim != 1 ||
       fn->s_domain[ 0 ] != 0.0 || fn->s_domain[ 1 ] != 1.0 ||
       ( fn->range != NULL &&
         (fn->s_range[ 0 ] != 0.0 || fn->s_range[ 1 ] != 1.0) ))
    return error_handler( RANGECHECK ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool fn_validate_ucr( FUNCTIONCACHE *fn , void *data )
{
  HQASSERT( data == NULL , "expected data is non-null in fn_validate_ucr" ) ;
  UNUSED_PARAM( void * , data ) ;
  if ( fn->in_dim != 1 || fn->out_dim != 1 ||
       fn->s_domain[ 0 ] != 0.0 || fn->s_domain[ 1 ] != 1.0 ||
       ( fn->range != NULL &&
         (fn->s_range[ 0 ] != -1.0 || fn->s_range[ 1 ] != 1.0) ))
    return error_handler( RANGECHECK ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool fn_validate_spot( FUNCTIONCACHE *fn , void *data )
{
  HQASSERT( data == NULL , "expected data is non-null in fn_validate_spot" ) ;
  UNUSED_PARAM( void * , data ) ;
  if ( fn->in_dim != 2 || fn->out_dim != 1 ||
       fn->s_domain[ 0 ] != -1.0 || fn->s_domain[ 1 ] != 1.0 ||
       fn->s_domain[ 2 ] != -1.0 || fn->s_domain[ 3 ] != 1.0 ||
       ( fn->range != NULL &&
         (fn->s_range[ 0 ] != -1.0 || fn->s_range[ 1 ] != 1.0) ))
    return error_handler( RANGECHECK ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool fn_validate_shading( FUNCTIONCACHE *fn , void *data )
{
  OBJECT *domain ;

  if ( data == NULL )
    return TRUE ;

  /* function domain must be a super-set of the domain or decode in
   * the shading dictionary.
   */

  domain = ( OBJECT * ) data ;

  if ( fn->in_dim * 2 != theLen(*domain))
    return error_handler( RANGECHECK ) ;

  domain = oArray( *domain ) ;

  if ( fn->in_dim <= 4 ) {
    SYSTEMVALUE *fn_domain = fn->s_domain ;
    int32 i , in_dim = fn->in_dim ;
    for ( i = 0 ; i < in_dim ; ++i ) {
      SYSTEMVALUE d0 , d1 ;
      if ( !object_get_numeric(domain++, &d0) ||
           !object_get_numeric(domain++, &d1) )
        return FALSE ;
      if ( fn_domain[ 0 ] > d0 || fn_domain[ 1 ] < d1 )
        return error_handler( RANGECHECK ) ;
      fn_domain += 2 ;
    }
  }
  else {
    OBJECT *fn_domain = fn->domain ;
    int32 i , in_dim = fn->in_dim ;

    if ( theLen(*fn_domain) != in_dim )
      return error_handler( RANGECHECK ) ;

    fn_domain = oArray( *fn_domain ) ;

    for ( i = 0 ; i < in_dim ; ++i ) {
      SYSTEMVALUE d0 , d1 , fn_d0 , fn_d1 ;
      if ( !object_get_numeric(domain++, &d0) ||
           !object_get_numeric(domain++, &d1) ||
           !object_get_numeric(fn_domain++, &fn_d0) ||
           !object_get_numeric(fn_domain++, &fn_d1) )
        return FALSE ;

      if ( fn_d0 > d0 || fn_d1 < d1 )
        return error_handler( RANGECHECK ) ;
    }
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Shading opacity function is a 1-input, 1-ouput function. */
static Bool fn_validate_shopacity( FUNCTIONCACHE *fn , void *data )
{
  HQASSERT( data == NULL , "expected data is non-null in fn_validate_shopacity" ) ;
  UNUSED_PARAM( void * , data ) ;
  if ( fn->in_dim != 1 || fn->out_dim != 1 ||
       fn->s_domain[ 0 ] != 0.0 || fn->s_domain[ 1 ] != 1.0 ||
       ( fn->range != NULL &&
         (fn->s_range[ 0 ] != 0.0 || fn->s_range[ 1 ] != 1.0) ))
    return error_handler( RANGECHECK ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool fn_validate_tint( FUNCTIONCACHE *fn , void *data )
{
  intptr_t tint_out_dim ;
  int32 i;

  HQASSERT( data != NULL , "expected data is null in fn_validate_spot" ) ;
  tint_out_dim = (intptr_t) data ;
  if ( fn->out_dim != tint_out_dim)
    return error_handler( RANGECHECK ) ;

  if ( fn->range == NULL )
    return TRUE ;

  if (fn->in_dim <= 4) {
    for ( i = 0 ; i != fn->in_dim * 2 ; i += 2 ) {
      if ( fn->s_domain[ i ] >= fn->s_domain[ i+1 ] )   /* The domain cannot be zero */
        return error_handler( RANGECHECK ) ;
    }
  }
  else {
    OBJECT *domain ;
    domain = oArray( *(fn->domain) ) ;
    for ( i = 0 ; i != fn->in_dim * 2 ; i += 2 ) {
      SYSTEMVALUE d1 , d2 ;
      if ( !object_get_numeric(domain++, &d1) ||
           !object_get_numeric(domain++, &d2) )
        return FALSE ;
      if ( d1 >= d2 )                                   /* The domain cannot be zero */
        return error_handler( RANGECHECK ) ;
    }
  }

  if ( tint_out_dim <= 4 ) {
    int32 i ;
    for ( i = 0 ; i < tint_out_dim * 2 ; i += 2 )
      if ( fn->s_range[ i ] > fn->s_range[ i + 1 ] )    /* The range is allowed to be zero */
        return error_handler( RANGECHECK ) ;
  }
  else {
    OBJECT *range ;
    int32 i ;
    HQASSERT( theLen(*fn->range) == tint_out_dim * 2 ,
              "fn->range does not match tint_out_dim" ) ;
    range = oArray( *(fn->range) ) ;
    for ( i = 0 ; i < tint_out_dim * 2 ; i += 2 ) {
      SYSTEMVALUE r1 , r2 ;
      if ( !object_get_numeric(range++, &r1) ||
           !object_get_numeric(range++, &r2) )
        return FALSE ;
      if ( r1 > r2 )                                    /* The range is allowed to be zero */
        return error_handler( RANGECHECK ) ;
    }
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool fn_validate_cie_tint( FUNCTIONCACHE *fn , void *data )
{
  /* no checking is done in this case. */
  UNUSED_PARAM( FUNCTIONCACHE * , fn ) ;
  UNUSED_PARAM( void * , data ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool fn_validate_evalfunc( FUNCTIONCACHE *fn , void *data )
{
  /* no checking is done in this case. */
  UNUSED_PARAM( FUNCTIONCACHE * , fn ) ;
  UNUSED_PARAM( void * , data ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* This routine is used by pdfout uses a table containing expected
   in/out dimensions, domains and ranges of functions to validate
   given values. Cannot validate the range for a tint transform into
   CIE based space since the range is not fixed */
int32 get_function_info( int32 index , int32 tint_out_dim ,
                         FUNCTIONINFO **info )
{
  static FUNCTIONINFO functioninfo[] = {
    /* 0 */ { 1, 1, {0,1},       {0,1}             }, /* HT           */
    /* 1 */ { 1, 1, {0,1},       {0,1}             }, /* BG           */
    /* 2 */ { 1, 1, {0,1},       {-1,1}            }, /* UCR          */
    /* 3 */ { 2, 1, {-1,1,-1,1}, {-1,1}            }, /* Spot fn      */
    /* 4 */ { 1, 1, {0,1},       {0,1}             }, /* TR           */
    /* 5 */ { 0, 0, {0,0,0,0},   {0,0,0,0,0,0,0,0} }, /* Shading      */
    /* 6 */ { 1, 3, {0,1},       {0,0,0,0,0,0}     }, /* Tint for CIE */
    /* 7 */ { 1, 1, {0,1},       {0,1}             }, /* Tint o/dim 1 */
    /* 8 */ { 1, 3, {0,1},       {0,1,0,1,0,1}     }, /* Tint o/dim 3 */
    /* 9 */ { 1, 4, {0,1},       {0,1,0,1,0,1,0,1} }  /* Tint o/dim 4 */
  } ;

  HQASSERT( info , "Info is a null pointer in get_function_info.");
  HQASSERT( index >= 0 && index < FN_CACHE_SIZE ,
            "index out of range in get_function_info." ) ;
  if ( index == FN_TINT_BASE ) {
    /* Tint separation is a special case, with range dimensions of 1, 3 or 4. */
    switch ( tint_out_dim ) {
    case 1 :
      index = FN_TINT_BASE ;
      break ;
    case 3 :
      index = FN_TINT_BASE + 1 ;
      break ;
    case 4 :
      index = FN_TINT_BASE + 2 ;
      break ;
    default :
      HQTRACE( trace_fn , ("tint_out_dim must be either 1, 3 or 4 in get_function_info.")) ;
      return error_handler( RANGECHECK ) ;
    }
  }
  *info = &functioninfo[ index ];
  return index;
}

/* -------------------------------------------------------------------------- */
Bool fn_get_info(OBJECT *function, int32 usage, int32 offset,
                 int32 firstgen, int32 secondgen, void *data,
                 FUNCTIONINFO *info)
{
  FUNCTIONCACHE *fn ;
  Bool truncated_offset = FALSE ;

  HQASSERT( function , "function is null in fn_evaluate" ) ;
  HQASSERT( 0 <= usage && usage < FN_NUMBER_OF_PDF_USES ,
            "usage out of range" ) ;
  HQASSERT( FN_NUMBER_OF_PDF_USES ==
            sizeof(gfn_cacheinfo)/sizeof(*gfn_cacheinfo) ,
            "uses enum does not match gfn_cacheinfo length" ) ;
  HQASSERT( offset >= 0 , "-ve offset" ) ;
  HQASSERT( firstgen == FN_GEN_NA ||
            ( 0 < firstgen  && firstgen <= MAXINT32 ) ,
            "Not a valid color generation number" ) ;
  HQASSERT( secondgen == FN_GEN_NA ||
            ( 0 < secondgen  && secondgen <= MAXINT32 ) ,
            "Not a valid repro generation number" ) ;
  HQASSERT( firstgen != FN_GEN_NA || secondgen != FN_GEN_NA ,
            "Neither generation numbers are used" ) ;

  fn = fn_cache_entry( function ,
                       usage , offset ,
                       firstgen , secondgen ,
                       & truncated_offset ,
                       data ) ;
  if ( fn == NULL )
    return FALSE ;

  info->in_dim = fn->in_dim ;
  info->out_dim = fn->out_dim ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
Bool fn_PSCalculatorCompatible(OBJECT* poProcedure, int32 recLevel)
{
  OBJECT* poThis ;
  int32 count ;

  HQASSERT( poProcedure != NULL, "poProcedure null");
  HQASSERT(( oType( *poProcedure ) == OARRAY ||
             oType( *poProcedure ) == OPACKEDARRAY ) &&
           oExecutable( *poProcedure ),
           "poProcedure is not a valid proc" ) ;

  if ( recLevel < 0 )
    return FALSE ; /* Fn is too deep => not valid */

  poThis = oArray( *poProcedure ) ;
  for ( count = ( int32 )theLen(*poProcedure) ; count > 0 ; count-- ) {
    switch ( oType( *poThis )) {
    case OINTEGER:
    case OREAL:
    case OBOOLEAN:
      break ; /* All valid */

    case OOPERATOR:
    {
      NAMECACHE *opname ;

      if ( ! oExecutable( *poThis ))
        return FALSE ; /* Not valid if not executable */
      opname = theIOpName( oOp( *poThis )) ;
      if (( theIOpClass( opname ) & FUNCTIONOP ) == 0 )
        return FALSE ;
      break ;
    }

    case ONAME:
      if ( ! oExecutable( *poThis ))
        return FALSE ; /* Not valid if not executable */
      if (( theIOpClass( oName( *poThis )) & FUNCTIONOP ) == 0 )
        return FALSE ;
      /* Alternatively for executable name, could do fastextracthash on
       * dicts on dictstack to see what name evals to, and then do
       * fn_PSCalculatorCompatible( nobj, recLevel ) on that. But it's
       * not really enforcing the idea of encapsulated (ie standalone) fns.
       */
      break ;

    case OARRAY:
    case OPACKEDARRAY:
      if ( ! oExecutable( *poThis ))
        return FALSE ; /* Only (sub-)procedures are valid */
      if ( ! fn_PSCalculatorCompatible( poThis, recLevel - 1 ))
        return FALSE ; /* Fail if sub-proc isn't valid */
      break ;

    default:
      return FALSE ; /* No other objects are valid */
    }
    poThis++ ;
  }
  return TRUE ;
}

/* The function is actually being repeated or reflected a number of times
   (spreadfactor times!).  The input value needs to be mapped to a new input
   value taking into account all the repeats/reflections. */
static void fn_spread_encode_input( FUNCTIONCACHE *fn, Bool upwards, SYSTEMVALUE *value )
{
  SYSTEMVALUE tmp, fractional_part ;
  int32 integral_part ;

  tmp = *value * fn->spreadfactor ;

  /* Adjust the input value by an epsilon depending on evaluation direction.
     This is to make sure that rounding errors in discontinuity encoding and
     decoding still cause the value to fall within the correct function. When
     evaluating upwards, we prefer to be in the function above a discontinuity,
     when evaluating downwards, we prefer to be in the function below a
     discontinuity. */
  fn_bias_input(tmp, upwards) ;

  integral_part = ( int32 ) tmp ;
  fractional_part = tmp - ( SYSTEMVALUE ) integral_part ;

  if ( fractional_part > 0.0 )
    tmp = fractional_part ;
  else if ( integral_part > 0 && !upwards )
    tmp = 1.0 ;
  else
    tmp = 0.0 ;

  if ( fn->spreadmethod == NAME_Reflect && ( integral_part & 1 ) ) {
    Bool maps_to_same;
    MAPS_TO_SAME_USERVALUE( maps_to_same, fractional_part, 0.0 ) ;
    if ( !maps_to_same )
      tmp = 1.0 - tmp ;
  }

  *value = tmp ;
}

/* The function is actually being repeated or reflected a number of times
   (spreadfactor times!).  The discontinuity already found needs to be mapped to
   a new discontinuity taking into account all the repeats/reflected. */
static void fn_spread_decode_discontinuity( FUNCTIONCACHE *fn,
                                            SYSTEMVALUE* discont,
                                            int32 *order,
                                            SYSTEMVALUE bounds[ 2 ] )
{
  SYSTEMVALUE tmp, spreadfactor_inv ;
  SYSTEMVALUE spread_discont = 0.0 ;
  int32 i ;

  tmp = *discont / fn->spreadfactor ;
  spreadfactor_inv = 1.0 / (SYSTEMVALUE)fn->spreadfactor ;

  /* Since the function is actually going to be repeated or reflected
     spreadfactor times across the function domain, there are additional
     discontinuities at the boundary of each repeat/reflect. */
  spread_discont = 0.0 ;
  for ( i = 0 ; i < fn->spreadfactor ; ++i ) {
    Bool maps_to_same;
    MAPS_TO_SAME_USERVALUE( maps_to_same, spread_discont, bounds[ 0 ] ) ;
    if ( ! maps_to_same && spread_discont > bounds[ 0 ] ) {
      MAPS_TO_SAME_USERVALUE( maps_to_same, spread_discont, bounds[ 1 ] ) ;
      if ( ! maps_to_same && spread_discont < bounds[ 1 ] ) {
        if (*order == -1) {
          *order = 0;
          *discont = spread_discont ;
          return ;
        }
        break ;
      }
    }
    spread_discont += spreadfactor_inv ;
  }

  /* Map the discontinuity found in the normal function range to the
     repeated/reflected range and pick the smaller value between this
     discontinuity and the one calculated above on a spread boundary. */
  if ( *order != -1 ) {
    for ( i = 0 ; i < fn->spreadfactor ; ++i ) {
      Bool maps_to_same;
      MAPS_TO_SAME_USERVALUE( maps_to_same, tmp, bounds[ 0 ] ) ;
      if ( ! maps_to_same && tmp > bounds[ 0 ] ) {
        MAPS_TO_SAME_USERVALUE( maps_to_same, tmp, bounds[ 1 ] ) ;
        if ( ! maps_to_same && tmp < bounds[ 1 ] ) {
          if ( spread_discont < tmp )
            *discont = spread_discont ;
          else
            *discont = tmp ;
          return ;
        }
      }
      tmp += spreadfactor_inv ;
    }
    /* There is no discontinuity in the mapped range.
       This is what we return if we don't find a discontinuity. */
    *order = -1 ;
    *discont = bounds[ 0 ] ;
  }
}

/* ---------------------------------------------------------------------- */

/** Functions for the internalisation of a 1d function to avoid using an object
 * with an unknown lifetime.
 * This was specifically added for the use of soft mask transfers in the renderer
 * which requires COLORVALUE data, so the current implementation returns that
 * type as an optimisation.
 */

#define FUNCTION_TABLE_SIZE   (256)

/** Internal representaion of a 1d function as a table. */
struct FN_INTERNAL {
  int32             id;
  int32             usage;
  mm_pool_t         pool;
  COLORVALUE        table[FUNCTION_TABLE_SIZE];
};


/** Create an internal representation of a 1d function. */
Bool fn_create(OBJECT          *functionObj,
               int32           functionId,
               int32           usage,
               mm_pool_t       pool,
               FN_INTERNAL     **pfunction)
{
  uint32 i;
  FN_INTERNAL *function;

  HQASSERT(functionObj != NULL, "functionObj NULL");
  HQASSERT(functionId > 0, "Invalid function id");

  function = mm_alloc(pool, sizeof(FN_INTERNAL),
                      MM_ALLOC_CLASS_FUNCTIONS);
  if (function == NULL)
    return error_handler(VMERROR);

  function->id = functionId;
  function->usage = usage;
  function->pool = pool;

  /* Populate a table of transfer values for use at the back end */
  for (i = 0; i < FUNCTION_TABLE_SIZE; i++) {
    USERVALUE value;

    /* Pass the mask alpha through a 1 to 1 transfer function */
    value = (1.0f * i) / (FUNCTION_TABLE_SIZE - 1);

    if (!fn_evaluate(functionObj,
                     &value, &value,
                     usage, 0,
                     functionId, FN_GEN_NA, NULL)) {
      mm_free(pool, function, sizeof(FN_INTERNAL));
      return FALSE;
    }

    function->table[i] = FLOAT_TO_COLORVALUE(value);
  }

  *pfunction = function;

  return TRUE;
}

/** Destroy an internal representation of a 1d function. */
void fn_destroy(FN_INTERNAL *function)
{
  HQASSERT(function != NULL, "function NULL");

  if (function != NULL)
    mm_free(function->pool, function, sizeof(FN_INTERNAL));
}

/** Evaluate an internal representation of a 1d function. */
COLORVALUE fn_evaluateColorValue(FN_INTERNAL   *function,
                                 COLORVALUE    inValue)
{
  uint32 lowIndex;
  uint32 highIndex;
  COLORVALUE fraction;

  HQASSERT(COLORVALUE_MAX / (FUNCTION_TABLE_SIZE - 1) == 1 << 8,
           "Expected a range of 256 per table value");

  lowIndex = inValue >> 8;
  highIndex = lowIndex + 1;
  if (highIndex == FUNCTION_TABLE_SIZE)
    highIndex = lowIndex;

  fraction = inValue & 0xFF;

  if (fraction == 0)
    return function->table[lowIndex];
  else {
    uint32 lo;
    uint32 hi;

    /* Interpolate between the high and low table values. */
    hi = function->table[highIndex] * fraction;
    lo = function->table[lowIndex] * (256 - fraction);

    return CAST_TO_COLORVALUE(( lo + hi + 128 ) >> 8);
  }
}


/* Log stripped */
