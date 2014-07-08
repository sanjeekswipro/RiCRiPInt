/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!marking:src:userpath.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Userpath caching functions.
 */

#include "core.h"
#include "swstart.h"
#include "upcache.h"

#include "swerrors.h"
#include "ripdebug.h"
#include "swdevice.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "fontcache.h"    /* free_ccache */
#include "scanconv.h"
#include "swpdfout.h"
#include "idlom.h"
#include "timing.h"
#include "namedef_.h"
#include "hqmemset.h"

#include "bitbltt.h" /* FORM */
#include "matrix.h"
#include "display.h"
#include "graphics.h"
#include "gstate.h"
#include "pathcons.h"
#include "pathops.h"
#include "clippath.h"
#include "system.h"
#include "stacks.h"
#include "gu_path.h"
#include "gu_cons.h"
#include "gu_ctm.h"
#include "devops.h"
#include "ndisplay.h"
#include "routedev.h"
#include "rlecache.h"
#include "dlstate.h"
#include "render.h"
#include "plotops.h"
#include "dl_bres.h"
#include "dl_free.h"
#include "clipops.h"
#include "fcache.h"
#include "gs_color.h"     /* GSC_ILLEGAL */
#include "gu_fills.h"
#include "vndetect.h"
#include "utils.h"
#include "lowmem.h"
#include "params.h"
#include "formOps.h"

#include "trap.h"         /* isTrapEnabled() */

#include "upath.h"
#include "monitor.h"

/* Polygon Caching definitions */

#ifdef DEBUG_BUILD
static int32 debug_userpath = 0;
#endif

#ifdef DEBUG_BUILD
# define CACHE_DEBUG if ( debug_userpath > 0 ) monitorf
#else
# define CACHE_DEBUG if (FALSE) monitorf
#endif

/* Header for cached ufill and ueofill information */
typedef struct ufillcache {
  struct ufillcache *next ;
  USERVALUE flatness ;
  CHARCACHE *cacheinfo ;
} UFILLCACHE ;

/* Header for cached ustroke information */
typedef struct ustrokecache {
  struct ustrokecache *next ;
  OMATRIX *adjustctm ;
  LINESTYLE linestyle ;
  uint8 strokeadjust ; /* adjust points before stroking? */
  uint8 spare1, spare2, spare3 ;
  CHARCACHE *cacheinfo ;
} USTROKECACHE ;

/* cached matrix and pointers */
typedef struct umatrixcache {
  OMATRIX partmatrix ;
  struct umatrixcache *next ; /* next in this upath list */
#ifdef UPATH_GLOBAL_MATRIX
  struct umatrixcache *global ; /* next in global upath list */
#endif
  USTROKECACHE *strokes ;
  UFILLCACHE *fills, *eofills ;
} UMATRIXCACHE ;

/* top-level userpath cache structure */
typedef struct upathcache {
  struct upathcache *next ; /* next in cache list */
  uint32 checksum ; /* short check for path inequality */
  uint32 size ; /* path storage in bytes */
  PATHINFO thepath ;
  UMATRIXCACHE *matrixlist ; /* matrix cache list */
} UPATHCACHE ;

/* indices into cache array */
#define UCACHE_NZFILL 0
#define UCACHE_EOFILL 1
#define UCACHE_STROKE 2

static UPATHCACHE *get_upathcache(void) ;
static void free_upathcache(UPATHCACHE *upcache) ;

static CHARCACHE *ucachehit(UPATHCACHE *upcache, int32 type,
                            OMATRIX *adjustmat, Bool *cacheit);
static Bool ucacheinsert(corecontext_t *context, UPATHCACHE *upcache, int32 type,
                         OMATRIX *adjustmat, CHARCACHE **ccacheptr,
                         PATHINFO *upath);

static Bool bitblt_ucache(DL_STATE *page, int32 colorType, CHARCACHE *cptr,
                          SYSTEMVALUE cx, SYSTEMVALUE cy) ;

static Bool userpath_fill(corecontext_t *context, int32 therule, int32 type) ;

enum internal_to_upath_flags { UPATH_INTERNAL_COPY = 0x00,
                               UPATH_INTERNAL_DISPOSE = 0x01 } ;

static Bool internal_to_path(UPATHCACHE *upcache, int32 action,
                             PATHINFO *ppath) ;

static Bool upath_to_internal(OBJECT *theo, UPATHCACHE **upcacheptr,
                              Bool *cachedptr) ;


/* Userpath marking operators */

Bool ufill_(ps_context_t *pscontext)
{
  return userpath_fill(ps_core_context(pscontext),
                       NZFILL_TYPE, UCACHE_NZFILL);
}

Bool ueofill_(ps_context_t *pscontext)
{
  return userpath_fill(ps_core_context(pscontext),
                       EOFILL_TYPE, UCACHE_EOFILL);
}

/* Last Modified:       02-Jun-1995
 * Modification history:
 *      24-May-95 (N.Speciner); call DEVICE_SETG before calling IDLOM callback,
 *              since we want to make sure that the device clipping boundaries
 *              have been properly set up to avoid incorrectly looking clipped
 *              out.
 *      30-May-95 (N.Speciner); add IDLOM support for UserPath target. Call
 *              IDLOM_FILL or IDLOM_CACHED_FILL, depending on whether UserPath
 *              was cached by IDLOM. If IDLOM cached it, but SW didn't, then
 *              call IDLOM_CACHED_FILL directly, but disable IDLOM before
 *              calling dofill.
 *      02-Jun-95 (N.Speciner); pass the userpath object to IDLOM_CACHED_FILL,
 *              so that IDLOM has a way of identifying the appropriate cache
 *              entry unambiguously later (for example, at vignette flush
 *              time). The user id is not sufficient due to the possibility of
 *              IDLOM callbacks returning /Equivalent. This change allows
 *              /Recache to work from the IDLOM object callback.
 */

/* Y 16Sep97 add pdfout hook. For the sake of simplicity and minimal change,
   duplicate the work of internal_to_path and translate_path. I do that
   before rounding the translation components, so don't lose accuracy.
*/

static Bool userpath_fill_internal(corecontext_t *context, int32 therule, int32 type,
                                   UPATHCACHE *upcache, Bool cacheit,
                                   int32 colorType, FILL_OPTIONS foptions)
{
  DL_STATE *page = context->page;
  Bool idlomCacheHit = FALSE;
  int32 idlomId = 0;
  SYSTEMVALUE cx = thegsPageCTM(*gstateptr).matrix[2][0];
  SYSTEMVALUE cy = thegsPageCTM(*gstateptr).matrix[2][1];
  PATHINFO upath;
  Bool result;

  path_init(&upath);

  if ( pdfout_enabled() ) {
    Bool res;
    if ( !internal_to_path(upcache, 0 , &upath) )
      return FALSE;
    path_translate(&upath, cx, cy);
    res = pdfout_dofill(context->pdfout_h, &upath, therule,0, colorType);
    path_free_list(upath.firstpath, mm_pool_temp);
    if ( !res )
      return FALSE;
  }

  /* Set up current translation rounding; may not be used if cached, but it's
     not much work anyway. round to nearest value; doesn't check for
     overflowing integer limits */
  SC_RINTF(cx, cx);
  SC_RINTF(cy, cy);

  if ( cacheit && !char_doing_charpath() ) {
    CHARCACHE *ccache;       /* try to cache userpath fill */

    if ( isHDLTEnabled( *gstateptr )) {
      STROKE_PARAMS params;

      /* Do this work just so we can do IDLOM caching */
      set_gstate_stroke(&params, &thePathInfo(*upcache), NULL, FALSE);

      /* IDLOM's internal cache should be based on the same parameters as SW's,
       * which is pretty all-inclusive. An IDLOM client may have looser re-
       * quirements, making use of /Equivalent returns, but if the internal
       * cache requirements are too loose, then we may not let the IDLOM client
       * know about a difference that it needs to know about. So the IDLOM
       * cache must discriminate based on: rule (stroke, fill, or eofill),
       * stroke parameters (in the case of stroke), additional matrix (in the
       * case of stroke), path, and gstate parameters (CTM).
       * This is much more restrictive than an abstract path would require, but
       * since we don't know what an IDLOM client will use to distinguish one
       * user path from another, we must do this.
       */
      switch (IDLOM_NEWUSERPATH(&params, theTop(operandstack),
                                therule, NULL,
                                &idlomCacheHit, &idlomId)) {
      case NAME_false:
        free_upathcache(upcache);
        return FALSE;
      default: ;      /* no effect */
      }
    }

    /* Test to see if already cached */
    if ( NULL == (ccache = ucachehit(upcache, type, NULL, &cacheit)) ) {
      /* Either tried to cache before & failed, or caching turned off? */
      if ( !cacheit || context->userparams->MaxUPathItem <= 0 ) {
        if ( !internal_to_path(upcache, UPATH_INTERNAL_DISPOSE, &upath) )
          return FALSE;
      } else {
        if ( !ucacheinsert(context, upcache, type, NULL, &ccache, &upath) )
          return FALSE;
      }
    } else    /* cache hit, but may need path for IDLOM */
      if ( !internal_to_path(upcache, UPATH_INTERNAL_DISPOSE, &upath) )
        return FALSE;

    if ( ccache ) {   /* if we've got a charcache form, blit it! */
      if ( !DEVICE_SETG(page, colorType, DEVICE_SETG_NORMAL) ) {
        path_free_list(upath.firstpath, mm_pool_temp);
        return FALSE;
      }
      if ( isHDLTEnabled( *gstateptr )) {
        int32 rval;

        /* translate existing path to current position */
        path_translate(&upath, cx, cy);

        if ( idlomCacheHit == 0 )
          rval = IDLOM_FILL(colorType, therule, &upath, NULL);
        else
          rval = IDLOM_CACHED_FILL(colorType, therule, &upath,
                                   theTop(operandstack), idlomId);
        path_free_list(upath.firstpath, mm_pool_temp);
        switch ( rval ) {
        case NAME_false: /* PS error in IDLOM callbacks */
          /*pop(&operandstack); <<<< Why this here? */
          return FALSE;
        case NAME_Discard: /* just pretending */
          return TRUE;
        default: /* only add, for now */
          ;
        }
      } else  /* IDLOM not enabled, just chuck away path */
        path_free_list(upath.firstpath, mm_pool_temp);

      if ( !bitblt_ucache(page, colorType, ccache, cx, cy) )
        return FALSE;
      CACHE_DEBUG((uint8 *)"C");

      return TRUE;
    }
  } else {    /* not caching, so get path from current internal form */
    if ( !internal_to_path(upcache, UPATH_INTERNAL_DISPOSE, &upath) )
      return FALSE;
  }

  /* fallback to filling path directly */
  HQASSERT( upath.firstpath != NULL,
            "degenerate path filled in userpath_fill" );

  /* translate path to current position */
  path_translate(&upath, cx, cy);

  if ( idlomCacheHit != 0 ) {   /* Only happens if idlom enabled & cache hit */
    if ( !DEVICE_SETG(page, colorType, DEVICE_SETG_NORMAL) )  {
      path_free_list(upath.firstpath, mm_pool_temp);
      return FALSE;
    }
    switch ( IDLOM_CACHED_FILL(colorType, therule, &upath,
                               theTop(operandstack), idlomId) ) {
    case NAME_false:/* PS error in IDLOM callbacks */
      path_free_list(upath.firstpath, mm_pool_temp);
      /*pop(&operandstack); <<<< Why was this here? */
      return FALSE;
    case NAME_Discard:/* just pretending */
      path_free_list(upath.firstpath, mm_pool_temp);
      return TRUE;
    default:/* only add, for now */
      ;
    }
    foptions |= FILL_NO_HDLT|FILL_NO_SETG;
  }
  /* If idlom enabled, and no cache hit, callback happens inside dofill */

  result = dofill(&upath, therule, colorType, foptions);
  CACHE_DEBUG((uint8 *)"F");

  path_free_list(upath.firstpath, mm_pool_temp);

  return result;
}

static Bool userpath_fill(corecontext_t *context, int32 therule, int32 type)
{
  UPATHCACHE *upcache;
  Bool result = TRUE;
  Bool cacheit;

  if ( !flush_vignette(VD_Default) )
    return FALSE;

  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW);

  if ( !upath_to_internal(theTop(operandstack), &upcache, &cacheit) )
    return FALSE;

  if ( upcache != NULL ) {        /* Ignore fill if it's degenerate */
    result = userpath_fill_internal(
      context, therule, type, upcache, cacheit, GSC_FILL,
      FILL_NOT_VIGNETTE | FILL_NOT_ERASE | FILL_NO_PDFOUT);
  }

  if ( result )
    pop(&operandstack);

  return result;
}

/* ustroke and ustrokepath do not share common code like ufill and ueofill,
   because ustrokepath never needs to go via the internal representation of
   the userpath; it goes directly to the final path

 * Last Modified:       02-Jun-1995
 * Modification history:
 *      19-May-95 (N.Speciner); don't adjust the path by the cache bearings for
 *              IDLOM, since the bearings are not adjusted for strokes (they
 *              are adjusted in the stroked path, not in the path).
 *      24-May-95 (N.Speciner); use npop, rather than pop, to make sure that
 *              all operands are popped off the stack before normal return.
 *      24-May-95 (N.Speciner); adjust the CTM before calling IDLOM_STROKE if
 *              a matrix operand was given to the ustroke operator.
 *              Also, call DEVICE_SETG before calling IDLOM callback, since we
 *              want to make sure that the device clipping boundaries have been
 *              properly set up to avoid incorrectly looking clipped out.
 *      30-May-95 (N.Speciner); add IDLOM support for UserPath target. Call
 *              IDLOM_STROKE or IDLOM_CACHED_STROKE, depending on whether
 *              UserPath was cached by IDLOM. If IDLOM cached it, but SW didn't
 *              then call IDLOM_CACHED_STROKE directly, but disable IDLOM
 *              before calling dostroke.
 *      02-Jun-95 (N.Speciner); pass the userpath obj. to IDLOM_CACHED_STROKE,
 *              so that IDLOM has a way of identifying the appropriate cache
 *              entry unambiguously later (for example, at vignette flush
 *              time). The user id is not sufficient due to the possibility of
 *              IDLOM callbacks returning /Equivalent. This change allows
 *              /Recache to work from the IDLOM object callback.
 */
Bool ustroke_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  Bool has_matrix = FALSE ;
  int32 nargs = 1 ;
  OBJECT *theo ;
  OMATRIX matrix ;
  UPATHCACHE *upcache ;
  PATHINFO upath ;
  Bool result = TRUE ;
  Bool cacheit;
  STROKE_PARAMS params ;
  Bool idlomCacheHit = FALSE ;
  int32 idlomId = 0 ;
  STROKE_OPTIONS soptions = STROKE_NOT_VIGNETTE  | STROKE_NO_PDFOUT ;
  DL_STATE *page = context->page;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  path_init( & upath ) ;

  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  theo = theTop(operandstack) ;
  if ( is_matrix_noerror(theo, & matrix) ) {
    if ( theStackSize(operandstack) < 1 )       /* 2 argument form */
      return error_handler( STACKUNDERFLOW ) ;
    if ( ! oCanRead(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
    nargs++ ;
    theo = stackindex( 1, &operandstack );

    /* If it's a pure translation matrix, ignore it...believe me, I've tried */
    if ( matrix.matrix[0][0] != 1.0 || matrix.matrix[0][1] != 0.0 ||
         matrix.matrix[1][0] != 0.0 || matrix.matrix[1][1] != 1.0 )
      has_matrix = TRUE ;
  }

  if ( ! upath_to_internal(theo, &upcache, &cacheit) )
    return FALSE ;

  if ( upcache != NULL ) {        /* Ignore stroke if it's degenerate */
    SYSTEMVALUE cx = thegsPageCTM(*gstateptr).matrix[2][0] ;
    SYSTEMVALUE cy = thegsPageCTM(*gstateptr).matrix[2][1] ;

    if ( pdfout_enabled() ) {
      Bool res ;
      if ( ! internal_to_path(upcache, 0 , &upath) )
        return FALSE ;
      path_translate(&upath, cx, cy) ;
      set_gstate_stroke(&params, &upath, NULL, FALSE) ;
      if ( has_matrix ) {
        params.usematrix = TRUE ;
        matrix_mult( & matrix, & thegsPageCTM(*gstateptr), & params.orig_ctm );
      }

      res = pdfout_dostroke( context->pdfout_h , & params , GSC_FILL ) ;
      path_free_list(upath.firstpath, mm_pool_temp) ;
      if (! res)
        return FALSE ;
    }

    /* Set up current translation rounding; may not be used if cached, but it's
       not much work anyway. round to nearest value; doesn't check for
       overflowing integer limits */
    SC_RINTF( cx , cx ) ;
    SC_RINTF( cy , cy ) ;

    if ( cacheit && !char_doing_charpath() ) {
      CHARCACHE *ccache ;       /* try to cache userpath stroke */

      if ( isHDLTEnabled( *gstateptr )) {
        /* Do this work just so we can do IDLOM caching */
        set_gstate_stroke(&params, &thePathInfo(*upcache), NULL, FALSE) ;

        /* IDLOM's internal cache should be based on the same parms as SW's,
         * which is pretty all-inclusive. An IDLOM client may have looser re-
         * quirements, making use of /Equivalent returns, but if the internal
         * cache requirements are too loose, then we may not let the IDLOM
         * client know about a difference that it needs to know about. So the
         * IDLOM cache must discriminate based on: rule (stroke, fill, or
         * eofill), stroke parameters (in the case of stroke), additional
         * matrix (in the case of stroke), path, and gstate parameters (CTM).
         * This is much more restrictive than an abstract path would require,
         * but since we don't know what an IDLOM client will use to distinguish
         * one user path from another, we must do this.
         */
        switch (IDLOM_NEWUSERPATH(&params, theo, IR_Stroke,
                                  has_matrix ? & matrix : NULL,
                                  &idlomCacheHit, &idlomId)) {
        case NAME_false:
          free_upathcache(upcache);
          return FALSE;
        default: ;      /* no effect */
        }
      }

      /* Test to see if already cached */
      if ( NULL == (ccache = ucachehit(upcache, UCACHE_STROKE,
                                       has_matrix ? & matrix : NULL,
                                       &cacheit)) ) {
        /* Either tried to cache before & failed, or caching turned off? */
        if ( ! cacheit || context->userparams->MaxUPathItem <= 0 ) {
          if ( ! internal_to_path(upcache, UPATH_INTERNAL_DISPOSE, &upath) )
            return FALSE ;
        } else {
          if ( ! ucacheinsert(context, upcache, UCACHE_STROKE,
                              has_matrix ? & matrix : NULL,
                              &ccache, &upath) )
            return FALSE ;
        }
      } else    /* cache hit, but may need path for IDLOM */
        if ( ! internal_to_path(upcache, UPATH_INTERNAL_DISPOSE, &upath) )
          return FALSE ;

      if ( ccache ) {   /* if we've got a charcache form, blit it! */
        if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) )  {
          path_free_list(upath.firstpath, mm_pool_temp) ;
          return FALSE ;
        }

        if ( isHDLTEnabled( *gstateptr )) {
          int32 rval ;

          /* for IDLOM, we need to build a "normal" path.  This might have been
           * done already (with internal_to_path) above, if this is the first
           * time for a given userpath, but it might not.
           */
          set_gstate_stroke(&params, &upath, NULL, FALSE) ;
          /* translate path to current position */
          path_translate(&upath, cx, cy) ;

          if (idlomCacheHit == 0)
            rval = IDLOM_STROKE(GSC_FILL, &params, has_matrix ? &matrix : NULL);
          else
            rval = IDLOM_CACHED_STROKE(GSC_FILL, &params,
                                       has_matrix ? &matrix : NULL,
                                       theo, idlomId) ;
          path_free_list(upath.firstpath, mm_pool_temp) ;
          switch (rval) {
          case NAME_false:/* PS error in IDLOM callbacks */
            npop(nargs, &operandstack);
            return FALSE ;
          case NAME_Discard:/* just pretending */
            npop(nargs, &operandstack);
            return TRUE ;
          default:/* only add, for now */
            ;
          }
        } else
          path_free_list(upath.firstpath, mm_pool_temp) ;

        if ( !bitblt_ucache(page, GSC_FILL, ccache, cx, cy) )
          return FALSE ;

        npop(nargs, &operandstack) ;

        return TRUE ;
      }
    } else {    /* not caching, so translate path to current transform */
      if ( ! internal_to_path(upcache, UPATH_INTERNAL_DISPOSE, &upath) )
        return FALSE ;
    }

    /* fallback to filling path directly */
    HQASSERT( upath.firstpath != NULL,
              "degenerate path stroke in ustroke_" ) ;

    /* translate path to current position */
    path_translate(&upath, cx, cy) ;

    {
      set_gstate_stroke(&params, &upath, NULL, FALSE) ;

      if ( has_matrix ) {
        params.usematrix = TRUE ;
        matrix_mult( & matrix, & thegsPageCTM(*gstateptr), & params.orig_ctm );
      }

      if (idlomCacheHit != 0) { /* only happens if IDLOM enabled & cache hit */
        if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) )  {
          path_free_list(upath.firstpath, mm_pool_temp) ;
          return FALSE ;
        }

        switch (IDLOM_CACHED_STROKE(GSC_FILL, &params,
                                    has_matrix ? &matrix : NULL,
                                    theo, idlomId)) {
        case NAME_false:/* PS error in IDLOM callbacks */
          path_free_list(upath.firstpath, mm_pool_temp) ;
          npop(nargs,&operandstack);
          return FALSE ;
        case NAME_Discard:/* just pretending */
          path_free_list(upath.firstpath, mm_pool_temp) ;
          npop(nargs,&operandstack);
          return TRUE ;
        default:/* only add, for now */
          ;
        }

        soptions |= STROKE_NO_SETG|STROKE_NO_HDLT ;
      }
      /* If idlom enabled, and no cache hit, callback happens inside dostroke */
      result = dostroke( & params , GSC_FILL , soptions) ;

      path_free_list(upath.firstpath, mm_pool_temp) ;    /* dispose of path */
    }
  }

  if ( result )
    npop(nargs, &operandstack) ;

  return result ;
}

/* Userpath memory allocation and de-allocation routines. Userpaths tend to be
   long-lived objects.
*/

#ifdef SYSTEM_STATS_USERPATHS
static int32 theuserpaths_stats = 0 ;
static int32 theuserpaths_stats_reported = 0 ;
#endif

static void free_upathcache( UPATHCACHE *upcache )
{
  HQASSERT(upcache != NULL, "free_upathcache called with NULL cache pointer") ;

#ifdef SYSTEM_STATS_USERPATHS
  theuserpaths_stats-- ;
#endif

  if (thePathInfo(*upcache).firstpath)
    path_free_list(thePathInfo(*upcache).firstpath, mm_pool_temp) ;

  mm_free(mm_pool_temp, (mm_addr_t)upcache , sizeof(UPATHCACHE)) ;
}

static UPATHCACHE *get_upathcache( void )
{
  UPATHCACHE *upathcache;

#ifdef SYSTEM_STATS_USERPATHS
  theuserpaths_stats++ ;
  if ( theuserpaths_stats > theuserpaths_stats_reported ) {
    theuserpaths_stats_reported = theuserpaths_stats ;
    monitorf((uint8*)"theuserpaths_stats: %d\n",theuserpaths_stats);
  }
#endif

  upathcache = mm_alloc(mm_pool_temp, sizeof(UPATHCACHE),
                        MM_ALLOC_CLASS_UPATH) ;
  if (upathcache == NULL)
    (void) error_handler(VMERROR);

  return upathcache;
}

/** upath_to_internal parses a userpath object and returns a cache structure
   containing the normalised internal representation of the userpath, and a
   checksum on it which can be used for checking for cache misses quickly.
   If the userpath is degenerate, no cache structure is returned; this
   routine is only called in situations where this approach is safe. */
static Bool upath_to_internal(OBJECT *theo, UPATHCACHE **upcacheptr,
                              Bool *cachedptr)
{
  Bool result ;
  SYSTEMVALUE oldtx = thegsPageCTM(*gstateptr).matrix[2][0] ;
  SYSTEMVALUE oldty = thegsPageCTM(*gstateptr).matrix[2][1] ;
  UPATHCACHE *upcache = NULL ;
  Bool oldcheckbbox = checkbbox ;
  PATHINFO userpath ;
  Bool uparsecached ;

  HQASSERT(theo != NULL, "Object pointer NULL in upath_to_internal") ;
  HQASSERT(upcacheptr != NULL, "Cache pointer NULL in upath_to_internal") ;
  HQASSERT(cachedptr != NULL, "Cached flag pointer NULL in upath_to_internal");

  checkbbox = TRUE ;
  thegsPageCTM(*gstateptr).matrix[2][0] = 0.0 ;
  thegsPageCTM(*gstateptr).matrix[2][1] = 0.0 ;
  newctmin |= NEWCTM_TCOMPONENTS ;

  path_init(&userpath) ;

  result = parse_userpath(theo, &userpath, &uparsecached) ;

  /* restore original state */
  thegsPageCTM(*gstateptr).matrix[2][0] = oldtx ;
  thegsPageCTM(*gstateptr).matrix[2][1] = oldty ;
  newctmin |= NEWCTM_TCOMPONENTS ;
  checkbbox = oldcheckbbox ;

  if ( result ) {
    if ( !userpath.lastline ||
         (theLineType(*userpath.lastline) == MOVETO &&
          userpath.firstpath == userpath.lastpath) ) {
      path_free_list(userpath.firstpath, mm_pool_temp) ;
      *upcacheptr = NULL ;      /* degenerate userpath */

      return TRUE ;
    }

    upcache = get_upathcache();
  }

  if ( upcache == NULL ) {     /* tidy up internal representation */
    path_free_list(userpath.firstpath, mm_pool_temp) ;

    return FALSE ;
  }

  path_checksum(&userpath, &upcache->checksum, &upcache->size);
  thePathInfo(*upcache) = userpath ;    /* struct copy */
  upcache->matrixlist = NULL;

  *cachedptr = uparsecached ;
  *upcacheptr = upcache ;

  return TRUE ;
}

/* Similar to the above, for use by polygon caching */
Bool polygon_to_internal(PATHINFO * polygon, UPATHCACHE **upcacheptr)
{
  UPATHCACHE * upcache = get_upathcache();

  path_checksum(polygon, &upcache->checksum, &upcache->size);
  thePathInfo(*upcache) = *polygon;    /* struct copy */
  upcache->matrixlist = NULL;

  *upcacheptr = upcache;

  return TRUE;
}

/* internal_to_path returns a path corresponding to the internal
   representation of a userpath. The path returned is closed with MYCLOSE if
   necessary, suitable for intersecting with clips or stroking directly.
   This routine frees the internal cache structure argument if the action
   UPATH_INTERNAL_DISPOSE is set. */
static Bool internal_to_path(UPATHCACHE *upcache, int32 action,
                             PATHINFO *ppath)
{
  HQASSERT(upcache != NULL, "Cache pointer NULL in internal_to_path") ;
  HQASSERT(ppath != NULL, "Pathinfo pointer NULL in internal_to_path") ;
  HQASSERT(action == UPATH_INTERNAL_DISPOSE || action == UPATH_INTERNAL_COPY,
           "Action value invalid in internal_to_path") ;

  if ( action == UPATH_INTERNAL_DISPOSE ) {
    *ppath = thePathInfo(*upcache) ;    /* extract path to avoid copying it */
    path_init(&thePathInfo(*upcache)) ;
    free_upathcache(upcache) ;          /* dispose of internal structure */
  } else if ( !path_copy(ppath, &thePathInfo(*upcache), mm_pool_temp) )
    return FALSE ;

  if ( ! path_close(MYCLOSE, ppath) ) {
    path_free_list(ppath->firstpath, mm_pool_temp) ;
    return FALSE ;
  }

  return TRUE ;
}

/* Userpath caching.
   Userpaths are cached as RLE forms, using whichever RLE storage method is
   most efficient. The userpath cache is a multi-level structure:

   At the top level is an array of pointers to UPATHCACHE structures. This
   array is indexed by masking the low order bits of the userpath checksum.
   Each pointer points to a list of UPATHCACHES; the first step is to check if
   the checksums match.

   Each UPATHCACHE has a list of UMATRIXCACHE structures hanging off it; these
   contain the first four components of the matrix which the userpaths
   under it relate to (i.e. not the translation, which is irrelevant). The
   UMATRIXCACHE structure has a pointer to a list of USTROKECACHE structures,
   and two lists of UFILLCACHE structures, one for fills and one for eofills.

   The USTROKECACHE and UFILLCACHE structures contain all of the gstate
   information relevant to the userpath. For fills, this is just the
   flatness, but for strokes it is all of the line style parameters as well.
   Each USTROKECACHE or UFILLCACHE structure contains a pointer to a CHARCACHE
   structure; if this pointer is NULL, this indicates that an attempt has been
   made to cache the userpath, but it couldn't be done for some reason. This is
   used to indicate when it is a waste of time trying to cache the userpath.

   The CHARCACHE structure contains the information needed to render the
   userpath; the RLE form, the offsets of the form, and the page number
   information which is used to tell if the userpath can be flushed from the
   cache.

   The RLE data is generated directly by the Bresenham filling routines, called
   with the span function set to rle_dospan. This means that only one fill can
   be used to generate the points, because the dospan function relies on the
   scanlines being done in turn. This in turn means that strokes are generated
   by a strokepath/fill combination, to make them happen as one fill.

   Note that the RLE form data is allocated separately from the form header,
   and so must be freed separately.

   The userpath cache is a separate cache in its own right. If a userpath
   overflows the size of the cache, it will not be cached. In contrast, if
   caching a character in a font overflows the font cache limit, the
   character is cached and the purge flag is set.
 */

#define UCACHE_BINS 32  /* Must be a power of two */
#define UCACHE_INDEX(c) ((c) & (UCACHE_BINS - 1))

static UPATHCACHE *userpath_cache[UCACHE_BINS] ;

/* User path caching parameters. These parameters are (Red Book pp. 517, 536)
 *      maximum number of bytes for a single cached userpath (userparam)
 *      maximum number of bytes in whole userpath cache (systemparam)
 *      number of bytes in userpath cache at the moment (systemparam)
 *      maximum number of userpaths cached (MAXINT)
 *      current number of userpaths cached (curupathnum)
 */

static Bool ucache_full(int32 size)
{
  SYSTEMPARAMS *systemparams = get_core_context_interp()->systemparams;

  return systemparams->CurUPathCache + size > systemparams->MaxUPathCache;
}


static int32 curupathnum = 0 ;

int32 upaths_cached(void)
{
  return curupathnum ;
}

/* Userpath caching implementation. Userpaths are cached as RLE character forms
   using one of the short, medium, or long RLE cache formats. Cached userpaths
   can be blitted directly to the display list.
*/

/** Auxiliary cache hit functions. These functions are used when searching the
   cache for userpaths and when inserting new userpaths. */
static UPATHCACHE *find_ucache(UPATHCACHE *upcache)
{
  UPATHCACHE *ucptr ;
  uint32 checksum ;

  HQASSERT( upcache != NULL, "Null userpath parameter in find_ucache" ) ;

  checksum = upcache->checksum;

  for ( ucptr = userpath_cache[UCACHE_INDEX(checksum)] ;
        ucptr ;
        ucptr = ucptr->next ) {
    if ( checksum == ucptr->checksum ) { /* now check for full equality */
      if ( path_compare(&thePathInfo(*upcache),
                        &thePathInfo(*ucptr),
                        PATH_COMPARE_NORMAL, 0.0) )
        return ucptr ;
    }
  }

  return NULL ;
}

static UMATRIXCACHE *find_umatrix(UPATHCACHE *upcache)
{
  OMATRIX *mptr = & thegsPageCTM(*gstateptr) ;
  UMATRIXCACHE *mcptr ;

  HQASSERT( upcache != NULL, "Null userpath parameter in find_umatrix" ) ;

  /* Check if matrix matches CTM. This test is similar to font caching tests,
     and so should probably use the same comparison method. Test checks
     [0][0] and [1][1] first, because it's more likely that the matrix is
     orthogonal than not. */
  for ( mcptr = upcache->matrixlist; mcptr ; mcptr = mcptr->next )
    if ( MATRIX_REQ( &mcptr->partmatrix, mptr ))
      return mcptr ;

  return NULL ;
}


/** Test if internal representation is in internal cache. Uses elements of the
   current gstate, depending on the userpath type. Only the elements that
   can affect a path are tested; in particular, if the flat parameter is set,
   the path is flattened already so the flatness isn't checked. */
static CHARCACHE *ucachehit(UPATHCACHE *upcache, int32 type,
                            OMATRIX *adjustmat, Bool *cacheit)
{
  UPATHCACHE *ucptr = find_ucache(upcache) ;

  if ( ucptr != NULL ) {
    UMATRIXCACHE *mcptr = find_umatrix(ucptr) ;
    int32 curved = thePathInfo(*ucptr).curved ;

    if ( mcptr != NULL ) {
      if ( type == UCACHE_STROKE ) {    /* Search stroke list */
        USTROKECACHE *ustrokep ;
        USERVALUE flatness = theFlatness(theLineStyle(*gstateptr)) ;
        USERVALUE linewidth = theLineWidth(theLineStyle(*gstateptr)) ;
        USERVALUE miterlimit = theMiterLimit(theLineStyle(*gstateptr)) ;
        USERVALUE dashoffset = theDashOffset(theLineStyle(*gstateptr)) ;
        uint8 startlinecap = theStartLineCap(theLineStyle(*gstateptr)) ;
        uint8 endlinecap = theEndLineCap(theLineStyle(*gstateptr)) ;
        uint8 dashlinecap = theDashLineCap(theLineStyle(*gstateptr)) ;
        uint8 linejoin = theLineJoin(theLineStyle(*gstateptr)) ;
        uint8 strokeadjust = (uint8) ForceStrokeAdjustApply(FALSE) ;

        for ( ustrokep = mcptr->strokes; ustrokep;
              ustrokep = ustrokep->next ) {
          uint16 dashlen = theDashListLen(theLineStyle(*gstateptr)) ;

          if ( (!curved || flatness == theFlatness(ustrokep->linestyle)) &&
               linewidth == theLineWidth(ustrokep->linestyle) &&
               linejoin == theLineJoin(ustrokep->linestyle) && (linejoin != 0
               || miterlimit == theMiterLimit(ustrokep->linestyle)) &&
               dashoffset == theDashOffset(ustrokep->linestyle) &&
               dashlen == theDashListLen(ustrokep->linestyle) &&
               startlinecap == theStartLineCap(ustrokep->linestyle) &&
               endlinecap == theEndLineCap(ustrokep->linestyle) &&
               dashlinecap == theDashLineCap(ustrokep->linestyle) &&
               strokeadjust == ustrokep->strokeadjust &&
               (adjustmat != NULL) == (ustrokep->adjustctm != NULL) )
            {   /* Matched so far - now check dash pattern and adjust matrix */
              SYSTEMVALUE *dashlist = theDashList(theLineStyle(*gstateptr)) ;
              SYSTEMVALUE *udash = theDashList(ustrokep->linestyle) ;

              while ( dashlen > 0 ) {
                if ( *udash != *dashlist )
                  goto NEXT_STROKE ;
                dashlen-- ;
                dashlist++ ;
                udash++ ;
              }

              if ( adjustmat != NULL ) {
                HQASSERT( ustrokep->adjustctm != NULL,
                          "Adjustment matrix not set in ucachehit" ) ;
                if ( ! MATRIX_EQ( adjustmat, ustrokep->adjustctm))
                  goto NEXT_STROKE ;
              }

              /* We've now matched everything, so it's the same */

              *cacheit = FALSE ;        /* indicate that we've hit cache */
              return ustrokep->cacheinfo;
            }
        NEXT_STROKE: ;
        }
      } else {  /* Search fill lists for cached userpath */
        UFILLCACHE *ufillp ;
        USERVALUE flatness = theFlatness(theLineStyle(*gstateptr)) ;

        if ( type == UCACHE_NZFILL )
          ufillp = mcptr->fills;
        else
          ufillp = mcptr->eofills;

        for ( ; ufillp ; ufillp = ufillp->next )
          if ( (!curved || flatness == theFlatness(*ufillp)) ) {
            *cacheit = FALSE ;          /* indicate that we've hit cache */
            return ufillp->cacheinfo;
          }
      }
    }
  }

  return NULL ;                 /* failed to find userpath */
}

/** Fill a userpath into an RLE cache form, and return a charcache structure
   describing it. */
static Bool ucachefill(corecontext_t *context, PATHINFO *upath, int32 therule,
                       CHARCACHE **ccacheptr)
{
  DL_STATE *page = context->page;
  CHARCACHE *cptr ;
  NFILLOBJECT *nfill ;
  PATHINFO ucpath ;
  sbbox_t bbox ;

  HQASSERT( upath, "Pathinfo null" ) ;

  HQASSERT( upath->lastline != NULL, "Path degenerate" ) ;
  HQASSERT( theLineType(*upath->lastline) == MYCLOSE ||
            theLineType(*upath->lastline) == CLOSEPATH,
            "Path not closed" ) ;

  /* Copy pathinfo, and make bounding box minimal, for smallest imaging area */
  ucpath = *upath ;

  /* Find out size of path, to set up form width, height, and offsets */
  ucpath.bboxtype = BBOX_NOT_SET ; /* Need this to override setbbox */
  (void)path_bbox(&ucpath, & bbox, BBOX_IGNORE_ALL|BBOX_SAVE) ;

  if ( (cptr = alloc_ccache()) == NULL )
    return error_handler(VMERROR);

  theFormT(*cptr) = FORMTYPE_CHARCACHE ;

  cptr->pageno = cptr->baseno = page->eraseno ;
  theXBearing(*cptr) = bbox.x1 < 0 ? (int32)bbox.x1 - 1 : (int32)bbox.x1 ;
  theYBearing(*cptr) = bbox.y1 < 0 ? (int32)bbox.y1 - 1 : (int32)bbox.y1 ;

  /* Using the charcache form wastes certain fields. When I've demonstrated
   * it working with charcache forms, I'll set up a userpath cache header which
   * contains the fields above and the form inline. The render functions are
   * all capable of taking normal forms, so this shouldn't cause problems.
   */

  /* Set required state for fill */
  fl_setflat( theFlatness( theLineStyle(*gstateptr))) ;

  HQASSERT(bbox.x2 - theXBearing(*cptr) + 1 > 0 &&
           bbox.y2 - theYBearing(*cptr) + 1 > 0, "Invalid ucache bbox");
  /* get size of form and clipping boundaries from bounding box */
  bbox_store(&cclip_bbox, 0, 0,  /* bbox should be normalised to 0,0 */
             (dcoord)(bbox.x2 - theXBearing(*cptr) + 1),
             (dcoord)(bbox.y2 - theYBearing(*cptr) + 1));

  /* Now prepare the path and fill it, using the direct-to-rle functions.
     Normalise the path so that the top left coord is near 0,0 in device
     space. It isn't put exactly at 0,0 because we want to retain the
     phase information. */
  path_translate(&ucpath, -theXBearing(*cptr), -theYBearing(*cptr)) ;

  /* Can't do rectangles direct to RLE at the moment, so always flatten */
  if ( make_nfill(page, ucpath.firstpath, NFILL_ISFILL, &nfill)) {
    if ( nfill != NULL ) { /* Check for degenerate. */
      render_state_t ucache_rs ;

      probe_begin(SW_TRACE_USERPATH_CACHE, (intptr_t)cptr) ;

      if ( setup_rle_render(&ucache_rs,
                            nfill->nthreads, cclip_bbox.x2 + 1,
                            cclip_bbox.y2 + 1,
                            RLE_UPATH_SPAN_GUESS(cclip_bbox.x2 + 1)) ) {
        FORM *form ;

        preset_nfill( nfill ) ;
        scanconvert_band(&ucache_rs.ri.rb, nfill, therule ) ;
        free_fill( nfill, page ) ;

        if ( (form = finish_rle_render()) != NULL ) { /* is it too big? */
          int32 size = theFormS(*form) +
            (int32)(sizeof(CHARCACHE) + sizeof(FORM)) ;
          if ( size > context->userparams->MaxUPathItem || ucache_full(size) ) {
            destroy_Form(form) ;
          } else {              /* Yahoo! We've managed to cache it! */
            context->systemparams->CurUPathCache += size ;
            curupathnum++ ;
            theForm(*cptr) = form ;
            *ccacheptr = cptr ;

            /* Undo translation of path, since path may be linked into cache */
            path_translate(&ucpath, theXBearing(*cptr), theYBearing(*cptr)) ;

            probe_end(SW_TRACE_USERPATH_CACHE, (intptr_t)cptr) ;

            return TRUE ;
          }
        }
      } else                    /* setup_rle_render failed */
        free_fill( nfill, page ) ;

      probe_end(SW_TRACE_USERPATH_CACHE, (intptr_t)cptr) ;
    }
  }

  /* We didn't manage to cache it, so we need to undo the translation
     we made for cache coordinates. */
  path_translate(&ucpath, theXBearing(*cptr), theYBearing(*cptr)) ;

  free_ccache( cptr ) ;

  *ccacheptr = NULL ;
  return TRUE ;
}

/** Try to insert internal representation into cache. This routine consumes
   (free or link into cache) the first argument (upathcache structure).
   It returns a CHARCACHE structure and a PATHINFO, both of which must be freed
   by the caller. The path returned is closed with a MYCLOSE if necessary. */
static Bool ucacheinsert(corecontext_t *context, UPATHCACHE *upcache, int32 type,
                         OMATRIX *adjustmat, CHARCACHE **ccacheptr,
                         PATHINFO *upath)
{
  SYSTEMPARAMS *systemparams = context->systemparams;
  UPATHCACHE *ucptr ;
  UMATRIXCACHE *mcptr ;
  CHARCACHE *cptr ;

  HQASSERT(!char_doing_charpath(), "Userpath caching inside a charpath!" ) ;

  /* Insert into various caches, starting with representation cache */
  if ( (ucptr = find_ucache(upcache)) == NULL ) {
    int32 index = UCACHE_INDEX(upcache->checksum);
    int32 size = upcache->size + (int32)sizeof(UPATHCACHE) ;

    if ( ! internal_to_path(upcache, UPATH_INTERNAL_COPY, upath) )
      return FALSE ;

    if ( ucache_full(size) ) {
      *ccacheptr = NULL ;
      return TRUE ;
    }
    systemparams->CurUPathCache += size ;

    upcache->next = userpath_cache[index] ;
    userpath_cache[index] = upcache ;
    ucptr = upcache ;
    CACHE_DEBUG((uint8 *)"+");
  } else if ( ! internal_to_path(upcache, UPATH_INTERNAL_DISPOSE, upath) )
      return FALSE ;

  /* Next insert into matrix cache */
  if ( (mcptr = find_umatrix(ucptr)) == NULL ) {
    int32 size = (int32)sizeof(UMATRIXCACHE) ;

    if ( ucache_full(size) ) {
      *ccacheptr = NULL ;       /* return appropriate bits to user */
      return TRUE ;
    }

    if ( (mcptr = ((UMATRIXCACHE *)mm_alloc(mm_pool_temp, sizeof(UMATRIXCACHE),
                    MM_ALLOC_CLASS_UPATH_MATRIX))) == NULL ) {
      path_free_list(upath->firstpath, mm_pool_temp) ;
      return error_handler(VMERROR) ;
    }

    systemparams->CurUPathCache += size ;

    MATRIX_COPY( &mcptr->partmatrix, & thegsPageCTM(*gstateptr)) ;
    mcptr->strokes = NULL;
    mcptr->fills = NULL;
    mcptr->eofills = NULL;

    mcptr->next = ucptr->matrixlist;
    ucptr->matrixlist = mcptr;
  }

  /* Next insert into fill or stroke list, and prepare to render it */
  if ( type == UCACHE_STROKE ) {
    STROKE_PARAMS params ;
    PATHINFO spath ;
    USTROKECACHE *sptr ;
    OMATRIX * adjustptr = NULL ;
    int32 size = (int32)(sizeof(USTROKECACHE)
                 + theDashListLen(gstateptr->thestyle) * sizeof(SYSTEMVALUE));

    /* Stroked userpaths are cached by doing a strokepath fill combination
       so that a single fill is used to generate the RLE. This makes it much
       easier to generate the RLE because all of the spans for a single
       scanline are generated before the next scanline is started. */
    if ( ucache_full(size) ) {
      *ccacheptr = NULL ;       /* return appropriate bits to user */
      return TRUE ;
    }

    set_gstate_stroke(&params, &thePathInfo(*ucptr), &spath, FALSE) ;

    if ( adjustmat != NULL ) {
      params.usematrix = TRUE ;
      matrix_mult( adjustmat, & thegsPageCTM(*gstateptr), & params.orig_ctm ) ;
    }

    if ( ! dostroke( & params , GSC_ILLEGAL , STROKE_NOT_VIGNETTE )) {
      path_free_list(upath->firstpath, mm_pool_temp) ;
      return FALSE ;
    }

    HQASSERT(spath.firstpath != NULL,
             "Stroked path is degenerate in ucacheinsert") ;

    if ( adjustmat != NULL ) {
      adjustptr = (OMATRIX *)
        mm_alloc(mm_pool_temp, sizeof(OMATRIX), MM_ALLOC_CLASS_MATRIX) ;
      if ( adjustptr == NULL ) {
        path_free_list(upath->firstpath, mm_pool_temp) ;
        path_free_list(spath.firstpath, mm_pool_temp) ;
        return error_handler(VMERROR);
      }

      MATRIX_COPY(adjustptr, adjustmat) ;
    }

    if ( !ucachefill(context, &spath, NZFILL_TYPE, &cptr) || (sptr =
          ((USTROKECACHE *)mm_alloc(mm_pool_temp, sizeof(USTROKECACHE),
            MM_ALLOC_CLASS_UPATH_STROKE))) == NULL ) {
      path_free_list(upath->firstpath, mm_pool_temp) ;
      path_free_list(spath.firstpath, mm_pool_temp) ;
      if ( adjustptr != NULL )
        mm_free(mm_pool_temp, (mm_addr_t) adjustptr, sizeof(OMATRIX)) ;
      return error_handler(VMERROR) ;
    }

    sptr->cacheinfo = cptr;

    sptr->linestyle = theLineStyle(*gstateptr) ;
    theDashListLen(sptr->linestyle) = 0 ;
    if ( !gs_storedashlist(&sptr->linestyle, theDashList(gstateptr->thestyle),
                           theDashListLen(gstateptr->thestyle)) ) {
      path_free_list(upath->firstpath, mm_pool_temp) ;
      path_free_list(spath.firstpath, mm_pool_temp) ;
      if ( adjustptr != NULL )
        mm_free(mm_pool_temp, (mm_addr_t) adjustptr, sizeof(OMATRIX)) ;
      mm_free(mm_pool_temp, (mm_addr_t)(sptr), sizeof(USTROKECACHE));
      return FALSE ;
    }
    object_store_null(&theDashPattern(sptr->linestyle)) ;

    sptr->strokeadjust = (uint8) ForceStrokeAdjustApply(FALSE) ;

    sptr->adjustctm = adjustptr ;

    sptr->next = mcptr->strokes;
    mcptr->strokes = sptr;

    systemparams->CurUPathCache += size ;

    path_free_list(spath.firstpath, mm_pool_temp) ;
  } else {
    UFILLCACHE *fptr ;
    int32 size = (int32)sizeof(UFILLCACHE) ;

    if ( ucache_full(size) ) {
      *ccacheptr = NULL ;       /* return appropriate bits to user */
      return TRUE ;
    }

    HQASSERT(type == UCACHE_EOFILL || type == UCACHE_NZFILL,
             "Invalid cache type in ucacheinsert" ) ;

    if ( !ucachefill(context, upath, type == UCACHE_NZFILL ? NZFILL_TYPE :
                     EOFILL_TYPE, &cptr) ||
        (fptr = ((UFILLCACHE *)mm_alloc(mm_pool_temp, sizeof(UFILLCACHE),
                                   MM_ALLOC_CLASS_UPATH_FILL))) == NULL ) {
      path_free_list(upath->firstpath, mm_pool_temp) ;
      return error_handler(VMERROR) ;
    }

    if ( type == UCACHE_NZFILL ) {
      fptr->next = mcptr->fills;
      mcptr->fills = fptr;
    } else {
      fptr->next = mcptr->eofills;
      mcptr->eofills = fptr;
    }

    fptr->cacheinfo = cptr;
    theFlatness(*fptr) = theFlatness(theLineStyle(*gstateptr)) ;

    systemparams->CurUPathCache += size ;
  }

  *ccacheptr = cptr ;

  return TRUE ;
}

/** bitblt_ucache is used to output the userpath form to the display list or
   device. It is based on bitblt_char, but is a lot simpler. */
static Bool bitblt_ucache(DL_STATE *page, int32 colorType, CHARCACHE *cptr,
                          SYSTEMVALUE cx, SYSTEMVALUE cy)
{
  int32 sx , sy ;
  FORM *tempf ;

  tempf = theForm(*cptr) ;

  /* Mark the userpath (in the cache) as used in current page. */
  if ( cptr->pageno < page->eraseno )
    cptr->baseno = page->eraseno ;
  cptr->pageno = page->eraseno ;

  if ( !DEVICE_SETG(page, colorType, DEVICE_SETG_NORMAL) )
    return FALSE ;

  if ( degenerateClipping )
    return TRUE ;

#if 0
  if ( cptr->pageno != page->eraseno)
    cptr->usagecnt = 0 ;
#endif
  ++( cptr->usagecnt ) ;

/* Rasterop result to character destination. */
  SC_C2D_INT(sx, cx + theXBearing(*cptr)) ;
  SC_C2D_INT(sy, cy + theYBearing(*cptr)) ;

  tempf = ( FORM * )cptr ;      /* Undo extra level of indirection */

  return DEVICE_DOCHAR(page, tempf, sx, sy);
}

/*
 * Attempt to convert the given path to a character-cache structure, if we
 * have not already done so, then blit the resulting charcache object.
 *
 * This is the API used from the path polygon caching code. It shares common
 * code with the rest of the userpath module, but is not actually using the
 * usepath cache itself. In common with the userpath_fill case, it takes the
 * CTM x,y to be an offset to apply to the path.
 */
Bool fill_using_charcache(DL_STATE *page, PATHINFO *path, int32 filltype,
                          CHARCACHE **ccptr, SYSTEMVALUE cx, SYSTEMVALUE cy)
{
  HQASSERT(page && path && ccptr, "Bad usepath-cache call");
  HQASSERT(filltype == NZFILL_TYPE || filltype == EOFILL_TYPE,
           "Unexpected fill type") ;

  if ( *ccptr == NULL ) {
    if ( !ucachefill(get_core_context_interp(), path, filltype, ccptr) )
      return FALSE;
  }
  if ( *ccptr )
    return bitblt_ucache(page, GSC_FILL, *ccptr, cx, cy);
  return TRUE;
}

/* Userpath cache purging.
   This is very simple at the moment; it just chucks away all caches entries
   which aren't used by objects on the display lists. If a phased purge is
   deemed desirable, this could be done by throwing away userpaths that
   aren't referred to which have different matrices or gstate elements from
   the current gstate, on the assumption that they're less likely to be used,
   then throwing away all headers for "don't cache" entries, then throwing
   away everything which isn't used by the DLs. This could also be done by
   proportion of the memory used, like the font cache purge.

   purge_ucache returns the amount of memory reclaimed.
   */

/** Throw away the char form, if it's obsolete. */
static Bool purge_uform(corecontext_t *context, CHARCACHE *cptr,
                        size_t *reclaimed)
{
  int32 erasenumber;
  if ( cptr == NULL )
    return TRUE ;

  erasenumber = outputpage_lock()->eraseno ; outputpage_unlock() ;
  if ( cptr->pageno < erasenumber ) {
    FORM *form = theForm(*cptr) ;
    size_t size;

    HQASSERT(form != NULL, "No character form in purge_uform" ) ;

    size = (size_t)theFormS(*form) + sizeof(CHARCACHE) + sizeof(FORM);
    context->systemparams->CurUPathCache -= CAST_SIZET_TO_INT32(size);
    *reclaimed += size ;
    curupathnum-- ;

    free_ccache(cptr) ;

    return TRUE ;
  }

  return FALSE ;
}


static size_t last_ucachesize = 0; /* size of usercache at last purge */
static int32 last_eraseno = 0 ;    /* erase number at last purge */


void purge_ucache(corecontext_t *context)
{
  size_t index;
  size_t reclaimed = 0;

  if ( context->systemparams->CurUPathCache == 0 )
    return;

  for ( index = 0 ; index < UCACHE_BINS ; index++ ) {
    UPATHCACHE *ucptr, **ucpptr = &(userpath_cache[index]) ;

    /* Search down internal representations, freeing bits as we go */
    while ( (ucptr = *ucpptr) != NULL ) {
      UPATHCACHE *unext = ucptr->next ;
      UMATRIXCACHE *mcptr, **mcpptr = &ucptr->matrixlist;

      while ( (mcptr = *mcpptr) != NULL ) {
        UMATRIXCACHE *mnext = mcptr->next ;
        USTROKECACHE *sptr, **spptr = &mcptr->strokes;
        UFILLCACHE *fptr, **pfpptr, **fpptr = &mcptr->fills;

        /* check if we can remove strokes */
        while ( (sptr = *spptr) != NULL ) {
          USTROKECACHE *snext = sptr->next ;

          /* If the char wasn't there, or was thrown away, dispose of stroke */
          if ( purge_uform(context, sptr->cacheinfo, &reclaimed) ) {
            size_t size = sizeof(USTROKECACHE);

            if ( sptr->adjustctm != NULL )
              mm_free(mm_pool_temp, (mm_addr_t)sptr->adjustctm,
                      sizeof(OMATRIX)) ;

            (void)gs_storedashlist(&sptr->linestyle, NULL, 0) ;

            mm_free(mm_pool_temp, (mm_addr_t)(sptr), sizeof(USTROKECACHE));
            context->systemparams->CurUPathCache -= CAST_SIZET_TO_INT32(size);
            reclaimed += size ;

            *spptr = snext ;
          } else
            spptr = & sptr->next ;
        }


        /* Check if we can remove fills and eofills */
        do {
          pfpptr = fpptr ;

          while ( (fptr = *fpptr) != NULL ) {
            UFILLCACHE *fnext = fptr->next ;

            /* If the char wasn't there, or was thrown away, dispose of fill */
            if ( purge_uform(context, fptr->cacheinfo, &reclaimed) ) {
              size_t size = sizeof(UFILLCACHE);

              mm_free(mm_pool_temp, (mm_addr_t)(fptr), sizeof(UFILLCACHE));
              context->systemparams->CurUPathCache -= CAST_SIZET_TO_INT32(size);
              reclaimed += size ;

              *fpptr = fnext ;
            } else
              fpptr = & fptr->next ;
          }
          fpptr = &mcptr->eofills;
        } while ( fpptr != pfpptr ) ;

        /* If we've removed all of the fills & strokes, then remove matrix */
        if ( mcptr->fills == NULL && mcptr->eofills == NULL &&
             mcptr->strokes == NULL ) {
          size_t size = sizeof(UMATRIXCACHE);

          mm_free(mm_pool_temp, (mm_addr_t)(mcptr), sizeof(UMATRIXCACHE));

          context->systemparams->CurUPathCache -= CAST_SIZET_TO_INT32(size);
          reclaimed += size ;

          *mcpptr = mnext ;
        } else
          mcpptr = & mcptr->next ;
      }

      /* Did we remove all of the matrices? If so, remove internal rep. */
      if ( ucptr->matrixlist == NULL ) {
        size_t size = sizeof(UPATHCACHE) + ucptr->size;

        free_upathcache(ucptr) ;
        context->systemparams->CurUPathCache -= CAST_SIZET_TO_INT32(size);
        reclaimed += size ;

        *ucpptr = unext ;
      } else
        ucpptr = & ucptr->next ;
    }
  }
  last_ucachesize = context->systemparams->CurUPathCache;
  last_eraseno = context->page->eraseno ;
  purge_polygon_cache(last_eraseno);
}


/** Solicit method of the userpath cache low-memory handler. */
static low_mem_offer_t *userpath_solicit(low_mem_handler_t *handler,
                                         corecontext_t *context,
                                         size_t count,
                                         memory_requirement_t* requests)
{
  static low_mem_offer_t offer;
  size_t uc = context->systemparams != NULL
    ? (size_t)context->systemparams->CurUPathCache : 0;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !context->between_operators
       /* between_operators implies SystemParams is available */
       || uc == 0
       || (uc == last_ucachesize && context->page->eraseno == last_eraseno) )
    return NULL;

  offer.pool = mm_pool_temp;
  offer.offer_size = uc/4; /* @@@@ guess */
  offer.offer_cost = 1.0f;
  offer.next = NULL;
  return &offer;
}


/** Release method of the userpath cache low-memory handler */
static Bool userpath_release(low_mem_handler_t *handler,
                             corecontext_t *context, low_mem_offer_t *offer)
{
  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  purge_ucache(context);
  return TRUE;
}

/** The userpath swinit function. */
static Bool userpath_swstart(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params);
#ifdef DEBUG_BUILD
  register_ripvar(NAME_debug_userpath, OINTEGER, &debug_userpath);
#endif
  return TRUE;
}

/** The userpath cache low-memory handler. */
static low_mem_handler_t userpath_handler = {
  "Userpath cache",
  memory_tier_ram, userpath_solicit, userpath_release, FALSE,
  0, FALSE };

/** Userpath cache initialization */
Bool userpath_init(void)
{
  return low_mem_handler_register(&userpath_handler);
}


/** Userpath cache finishing */
static void userpath_finish(void)
{
  low_mem_handler_deregister(&userpath_handler);
}


void init_C_globals_userpath(core_init_fns *fns)
{
#ifdef SYSTEM_STATS_USERPATHS
  theuserpaths_stats = 0 ;
  theuserpaths_stats_reported = 0 ;
#endif
  curupathnum = 0 ;
  HqMemZero(userpath_cache, sizeof(userpath_cache));
  last_ucachesize = 0 ;
  last_eraseno = 0 ;
#ifdef DEBUG_BUILD
  debug_userpath = 0;
#endif

  fns->swstart = userpath_swstart;
  fns->postboot = userpath_init;
  fns->finish = userpath_finish;
}

/* Log stripped */
