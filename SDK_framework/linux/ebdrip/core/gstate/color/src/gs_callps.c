/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gs_callps.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Color conversions call back to the PS interpreter
 *
 * Following code supports a cache of single PS procedures and their
 * associated return single value numerical values. These can be used for
 * things like Under Color Removal, BlackGeneration et al. For now these are
 * in this file since UCR & BG is all that use them. Later on if used
 * elsewhere can be moved.
 */

#include "core.h"
#include "hqmemcpy.h"           /* HqMemCpy */
#include "swerrors.h"           /* TYPECHECK */
#include "mps.h"                /* mps_res_t */
#include "gcscan.h"             /* ps_scan_field */
#include "control.h"            /* interpreter - *** Remove this *** */
#include "fileio.h"             /* FILELIST */
#include "functns.h"            /* fn_evaluate */
#include "namedef_.h"           /* NAME_pop */
#include "params.h"             /* SystemParams */
#include "render.h"             /* inputpage */
#include "routedev.h"           /* DEVICE_INVALID_CONTEXT */
#include "spdetect.h"           /* detect_setcolor_separation */
#include "stacks.h"             /* operandstack - *** Remove this *** */
#include "gs_colorpriv.h"       /* CLINK */
#include "gscdevcipriv.h"       /* OP_CMYK_OVERPRINT_MASK */
#include "constant.h"           /* EPSILON */
#include "lowmem.h" /* mm_set_reserves */
#include "context.h" /* get_core_context */
#include "gs_callps.h"


/**
 * Cache for calls to the PS interpreter by color chains.
 * Cached procedures take one real argument and return 'nOut' reals.
 */
struct CALLPSCACHE {
  cc_counter_t refCnt;
  int32 uniqueID;
  int32 fnType;
  int32 nOut, nVals;
  Bool emptyFunc;
  struct {
    USERVALUE min, max;
  } range;
  USERVALUE vCache[1]; /* Actually variable, with nVals*nOut entries */
};

/**
 * Call the PS interpreter to evaluate a given procedure.
 *
 * Procedure takes one real input value and returns 'n'.
 */
Bool call_psproc(OBJECT *psfunc, SYSTEMVALUE in, SYSTEMVALUE *out, int32 nOut)
{
  int32 saved_gId = gstateptr->gId;
  corecontext_t *context = get_core_context_interp();

#ifdef DEBUG_COLOR
  static int32 trace_gId;
  HQTRACE(trace_gId != saved_gId, ("Callout to PS from color operator"));
  trace_gId = saved_gId;
#endif

  HQASSERT((oType(*psfunc) == OARRAY || oType(*psfunc) == OPACKEDARRAY),
      "PS function is not a procedure");

  if ( theLen(*psfunc) == 0) {
    if ( nOut != 1 )
      return error_handler(STACKUNDERFLOW);
    out[0] = in;
  } else {
    Bool res;

    if ( !stack_push_real(in, &operandstack) )
      return FALSE;
    if ( !push(psfunc, &executionstack) )
      return FALSE;

    mm_set_reserves(context->mm_context, FALSE);
    res = interpreter(1, NULL);
    mm_set_reserves(context->mm_context, TRUE);
    if ( !res )
      return FALSE;

    if ( !stack_get_numeric(&operandstack, out, nOut) )
      return FALSE;
    npop(nOut, &operandstack);

    if ( gstateptr->gId != saved_gId )
      return detail_error_handler(UNDEFINED, "Unsafe use of gsave/grestore.");
  }
  return TRUE;
}

/*
 * Color chain call to the PS interpreter, cached to avoid need to invoke
 * the interpreter during multi-threaded compositing.
 *
 * Note that this routine could end up going recursive if one creates
 * a BR or UCR proc that installs a new one. However, this will be
 * picked up by the check in control.c on MaxInterpreterLevel.
 */
static Bool do_callps(CALLPSCACHE *cpsc, OBJECT *psfunc, USERVALUE in,
                      USERVALUE *out)
{
  switch ( oType(*psfunc) ) {
    case OFILE:
    case ODICTIONARY:
      HQASSERT(cpsc->nOut == 1, "unexpected nOut");
      if ( !fn_evaluate(psfunc, &in, out, cpsc->fnType, 0, cpsc->uniqueID,
                        FN_GEN_NA, NULL) )
        return FALSE;
      break;

    case OARRAY:
    case OPACKEDARRAY: {
      /** \todo BMJ 21-Jul-10 :  This code should really be a call to the
       * above function, but mess between USERVALUEs and SYSTEMVALUEs means
       * this is not possible.
       */
      corecontext_t *context = get_core_context_interp();
      int32 saved_gId = gstateptr->gId;
      Bool res;

      if ( !stack_push_real((SYSTEMVALUE)in, &operandstack) )
        return FALSE;
      if ( !push(psfunc, &executionstack) )
        return FALSE;

      mm_set_reserves(context->mm_context, FALSE);
      res = interpreter(1, NULL);
      mm_set_reserves(context->mm_context, TRUE);
      if ( !res )
        return FALSE;

      if ( !stack_get_reals(&operandstack, out, cpsc->nOut) )
        return FALSE;
      npop(cpsc->nOut, &operandstack);

      if ( gstateptr->gId != saved_gId )
        return detail_error_handler(UNDEFINED, "Unsafe use of gsave/grestore.");
    } break;
    default:
      return error_handler(TYPECHECK);
  }

  if ( cpsc->fnType == FN_BLACK_GEN ) {
    HQASSERT(cpsc->nOut == 1, "unexpected nOut");
    NARROW_01(out[0]);
  }
  return TRUE;
}

/**
 * Create a cache for color chain calls to the PS interpreter
 */
CALLPSCACHE *create_callpscache(int32 fnType, int32 nOut, int32 uniqueID,
                                SYSTEMVALUE *range, OBJECT *psfunc)
{
  mm_size_t size;
  CALLPSCACHE *cpsc;
  USERVALUE r_min, r_max;
  Bool emptyFunc = TRUE;
  int i, nVals = 256;

  /*
   * By default we use 256 interpolation steps and limit the function
   * to the range 0 -> 1. But this does not work for some classes of function.
   * If we are bumping the range, need to increase the number of interpolation
   * points in proportion, to get the error limits consistent.
   */
  if ( range == NULL ) { /* NULL range => default [0, 1] */
    nVals = 256;
    r_min = 0.0f;
    r_max = 1.0f;
  } else {
    r_min = (USERVALUE)range[0];
    r_max = (USERVALUE)range[1];
    if ( r_max - r_min <= 1.0f )
      nVals = 256;
    else
      nVals = (int32)(256 * (r_max - r_min));
  }

  size = sizeof(CALLPSCACHE);
  if ( oType(*psfunc) == ONULL ) {
    emptyFunc = TRUE;
  } else if ((oType(*psfunc) != OARRAY && oType(*psfunc) != OPACKEDARRAY) ||
           theLen(*psfunc) > 0 ) {
    emptyFunc = FALSE;
    size += nVals * nOut * sizeof(cpsc->vCache[0]);
  }

  cpsc = (CALLPSCACHE *)mm_sac_alloc(mm_pool_color, size,
                                     MM_ALLOC_CLASS_NCOLOR);

  if (cpsc == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  cpsc->refCnt = 1;
  cpsc->uniqueID = uniqueID;
  cpsc->fnType = fnType;
  cpsc->emptyFunc = emptyFunc;
  cpsc->nOut = nOut;
  cpsc->nVals = nVals;
  cpsc->range.min = r_min;
  cpsc->range.max = r_max;

  /* Provided its not an empty function, fill the cache by calling
   * the interpreter...
   */
  if ( !emptyFunc ) {
    for ( i = 0; i < cpsc->nVals; i++ ) {
      USERVALUE in = (i/(cpsc->nVals - 1.0f))*
        (cpsc->range.max - cpsc->range.min) + cpsc->range.min;

      if ( !do_callps(cpsc, psfunc, in, &(cpsc->vCache[i*cpsc->nOut])) ) {
        mm_sac_free(mm_pool_color, (mm_addr_t)cpsc, size);
        return NULL;
      }
    }
  }
  return cpsc;
}

/**
 * Increase reference count on PS call cache
 */
void reserve_callpscache(CALLPSCACHE *cpsc)
{
  HQASSERT(cpsc, "CALLPSCACHE NULL");
  CLINK_RESERVE(cpsc);
}

static void free_callpscache(CALLPSCACHE *cpsc)
{
  mm_size_t size;
  size = sizeof(CALLPSCACHE);
  if ( !cpsc->emptyFunc )
    size += cpsc->nVals * cpsc->nOut * sizeof(cpsc->vCache[0]);
  mm_sac_free(mm_pool_color, cpsc, size);
}


/**
 * Free structure maintaing PS call cache
 */
void destroy_callpscache(CALLPSCACHE **cpsc)
{
  HQASSERT(*cpsc, "CALLPSCACHE NULL");
  CLINK_RELEASE(cpsc, free_callpscache);
}

/**
 * Lookup an entry in the color chain PS call cache.
 */
void lookup_callpscache(CALLPSCACHE *cpsc, USERVALUE in, USERVALUE *out)
{
  USERVALUE in01, in_f, in_if;
  int32 i, index, in_i;

  HQASSERT(cpsc && out, "Corrupt callpscache");

  /*
   * An empty PS procedure is an identity function, so can always do this
   * without caching values or calling the interpreter.
   */
  if ( cpsc->emptyFunc ) {
    *out = in;
    return;
  }

  /* Cummulative rounding errors may cause the input to be a tiny bit outside
   * the stated range. If so, then wobble it back into range
   */
  if ( in < cpsc->range.min && in >= cpsc->range.min - EPSILON )
    in = cpsc->range.min;
  if ( in > cpsc->range.max && in <= cpsc->range.max + EPSILON )
    in = cpsc->range.max;

  if ( in < cpsc->range.min || in > cpsc->range.max ) {
    /* pre-cached PS calls over what we thought would be the entire range,
     * but now have been called with a value outside that range. And its not
     * just a rounding error, its well outside the given range.
     * Far too late to do anything about it, as it is not safe to call the
     * interpreter at this point.
     */
    HQFAIL("Color value outside pre-cached range");
    if ( in < cpsc->range.min )
      in01 = 0.0f;
    else
      in01 = 1.0f;
  } else {
    in01 = (in - cpsc->range.min)/(cpsc->range.max - cpsc->range.min);
  }
  in_f  = in01 * (cpsc->nVals - 1.0f);
  in_i  = (int32)in_f;
  in_if = (USERVALUE)in_i;
  index   = in_i*cpsc->nOut;

  for ( i = 0; i < cpsc->nOut; i++ )
    out[i] = cpsc->vCache[index+i];

  if ( in_f != in_if ) /* fraction : need to interpolate */ {
    USERVALUE *out2 = &(cpsc->vCache[index+cpsc->nOut]);
    for ( i = 0; i < cpsc->nOut; i++ )
      out[i] += (in_f - in_if) * (out2[i] - out[i]);
  }
}

/**
 * Return the UniqueID for the given PS call cache.
 */
int32 id_callpscache(CALLPSCACHE *cpsc)
{
  HQASSERT(cpsc, "cpsc NULL");
  return cpsc->uniqueID;
}

/* Log stripped */
