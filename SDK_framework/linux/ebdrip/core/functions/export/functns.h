/** \file
 * \ingroup funcs
 *
 * $HopeName: COREfunctions!export:functns.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Function API for PS/PDF
 */

#ifndef __FUNCTNS_H__
#define __FUNCTNS_H__

#include "mm.h"                 /* mm_pool_t */
#include "objecth.h"            /* OBJECT */

struct core_init_fns ; /* swstart */

/**
 * \defgroup funcs Function API for PS/PDF
 * \ingroup core
 */
/** \{ */

/* --------------------------- external constants --------------------------- */

/**
 * Used to specify what a function will be used for. Used for both function
 * lookup and caching from PDF, and as a general enumeration of function types
 * in the PostScript code.
 *
 * In the PDF case, required as an argument to fn_evaluate.
 * As well as determining which slot in the cache will be used, the usage
 * value affects the purging characteristics. In this case only values up to
 * \c FN_NUMBER_OF_PDF_USES are used.
 *
 * When used in the PS code, any value may be used, including those beyond
 * \c FN_NUMBER_OF_PDF_USES.
 */
enum {
  FN_HALFTONE_TFR = 0 ,
  FN_BLACK_GEN ,
  FN_UNDER_COL_REM ,
  FN_TRANSFER ,
  FN_SPOT_FUNCTION ,
  FN_SHADING ,
  FN_SHOPACITY ,
  FN_CIE_TINT_TFM ,
  FN_TINT_TFM ,
  FN_SOFTMASK_TFR ,
  FN_EVALFUNC_OP ,
  FN_NUMBER_OF_PDF_USES,
  FN_CRD_LMNFUNC,
  FN_CRD_ABCFUNC,
  FN_CRD_TIFUNC,
  FN_CRD_PQRFUNC
} ;

enum {
  /** FN_INVALID_GEN is a number that a color or reproduction generation
      number can never equal. Its purpose is to cause the function cache,
      for a particular function, to be reloaded in the next call to
      evaluate_function. */
  FN_GEN_INVALID = 0,

  /** Default to any number not equal to \c FN_INVALID_GEN. */
  FN_GEN_DEFAULT = 1,

  /** Two generation numbers are used for a halftone, color and
      reproduction, all the other uses use one; for these cases the
      generation number not used is masked off with \c FN_GEN_NA (function
      generation number not applicable). */
  FN_GEN_NA = -1
} ;

/* Only to be used for pdfout  */
enum {
  FN_TINT_DIM_NA = 0
} ;

/* --------------------------- external structures -------------------------- */

/* Hangover for pdf out. Description of function input/output
   arguments. Whole of pdfout and functions needs reviewing */
typedef struct FUNCTIONINFO {
  int32 in_dim ;
  int32 out_dim ;
  int32 domain[ 4 ] ;
  int32 range[ 8 ] ;
} FUNCTIONINFO ;

/* --------------------------- external functions --------------------------- */

void functions_C_globals(struct core_init_fns *fns) ;

/* Routines to apply a function to a set of input values and produce the
   corresponding output values. fn_evaluate_with_direction is the same as
   fn_evaluate except for the addition of the upwards parameter. With stitching
   functions it is possible to have discontinuties at the stitching points. In
   this case, the result of an evaluation on a boundary depends on the function
   used (choice of two). The upwards flag biases the evaluation towards a
   particular direction. fn_evaluate_with_direction with upwards set to true is
   equivalent to fn_evaluate */
Bool fn_evaluate( OBJECT *function ,
                  USERVALUE *input , USERVALUE *output ,
                  int32 usage , int32 offset ,
                  int32 firstgen , int32 secondgen ,
                  void *data ) ;
Bool fn_evaluate_with_direction( OBJECT *function ,
                                 USERVALUE *input , USERVALUE *output ,
                                 Bool upwards ,
                                 int32 usage , int32 offset ,
                                 int32 firstgen , int32 secondgen ,
                                 void *data ) ;

Bool fn_find_discontinuity( OBJECT *function ,
                            int32 index ,
                            USERVALUE bounds[],
                            USERVALUE *discontinuity , int32 *order ,
                            int32 usage , int32 offset ,
                            int32 firstgen , int32 secondgen ,
                            void *data ) ;

/** Explicitly invalidate a cache slot, as an alternative to relying on
    firstgen and secondgen (usually colorgen and reprogen respectively) */
void fn_invalidate( int32 usage , int32 offset ) ;

/** Clears the flag in the cache struct to control the
    behaviour of fn_purge (called by the low memory handler).
    For use by spot functions and shading functions only. */
void fn_lock( int32 usage , int32 offset ) ;

/** Set the flag in the cache struct to control the
    behaviour of fn_purge (called by the low memory handler).
    For use by spot functions and shading functions only  */
void fn_unlock( int32 usage , int32 offset ) ;

/** Determines if the PS procedure is compatible with PS Calculator. */
Bool fn_PSCalculatorCompatible(OBJECT* poProcedure, int32 recLevel);

/** \todo To be removed, for old PDF Out only */
int32 get_function_info( int32 index , int32 tint_out_dim ,
			 FUNCTIONINFO **info ) ;

/** Unpacks the function object and returns information about it.
   This function should probably supercede get_function_info. Note that it
   DOES NOT currently fill in the range and domain arrays, just the in_dim
   and out_dim members. Knowing the function's input and output dimensionality
   is important for typechecking shaded fills. Other information can be
   added to FUNCTIONINFO as needed. */
Bool fn_get_info(OBJECT *function, int32 usage, int32 offset,
                  int32 firstgen, int32 secondgen, void *data,
                  FUNCTIONINFO *info) ;

/* --------------------------- external variables --------------------------- */

/* Global variable used to turn function tracing on */
#if defined( ASSERT_BUILD )
extern Bool trace_fn ;
#endif

/*----------------------------------------------------------------------------*/

/** Functions for the internalisation of a 1d function to avoid using an object
 * with an unknown lifetime.
 * This was specifically added for the use of soft mask transfers in the renderer
 * which requires COLORVALUE data, so an optimisation of specifying the data
 * format has been built-in.
 */

typedef struct FN_INTERNAL FN_INTERNAL;

Bool fn_create(OBJECT          *functionObj,
               int32           functionId,
               int32           usage,
               mm_pool_t       pool,
               FN_INTERNAL     **pfunction);

void fn_destroy(FN_INTERNAL *function);

COLORVALUE fn_evaluateColorValue(FN_INTERNAL *function,
                                 COLORVALUE inValue);

/** \} */

#endif

/* Log stripped */
