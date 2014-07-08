/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!src:gstate.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Gstate implementation.
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "pscontext.h"
#include "gcscan.h"
#include "swoften.h"
#include "swdevice.h"
#include "often.h"
#include "objects.h"
#include "fileio.h"
#include "namedef_.h"
#include "swpdfin.h" /* pdf_walk_gframe. */
#include "basemap.h"
#include "hqmemcpy.h"

#include "matrix.h"
#include "params.h"
#include "stacks.h"
#include "miscops.h"
#include "dicthash.h"
#include "display.h"
#include "ndisplay.h"
#include "graphics.h"
#include "chartype.h"
#include "binscan.h"
#include "showops.h"
#include "progress.h"
#include "dlstate.h"
#include "render.h"
#include "plotops.h"
#include "gstate.h"
#include "devops.h"
#include "gstack.h"
#include "halftone.h"
#include "startup.h"
#include "control.h"
#include "swmemory.h"
#include "fcache.h"
#include "system.h"
#include "gu_path.h"
#include "gu_chan.h"
#include "pathcons.h"
#include "clipops.h"

#include "trap.h"
#include "routedev.h"

#include "gs_cache.h"           /* gsc_initColorSpaces */
#include "gs_color.h"
#include "gschead.h"
#include "gschtone.h"
#include "tranState.h"

static void apply_pagebasematrix_to_one_path(OMATRIX *diffmatrix,
                                             PATHINFO *path);
static Bool apply_pagebasematrix_to_one_gstate(GSTATE *gs, void *arg) ;


static Bool gs_locked; /**< Lock gstate against modification */


Bool gs_lock(Bool state)
{
  Bool previous = gs_locked;
  gs_locked = state;
  return previous;
}


/* ----------------------------------------------------------------------------
   function:            initgraphics_()    author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 173.

---------------------------------------------------------------------------- */
Bool initgraphics_(ps_context_t *pscontext)
{
  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  ( void )initmatrix_(pscontext) ;
  ( void )newpath_(pscontext) ;
  ( void )initclip_(pscontext) ;

  tsDiscard(gsTranState( gstateptr ));
  tsDefault(gsTranState( gstateptr ), gstateptr->colorInfo);

  theLineWidth(theLineStyle(*gstateptr)) = 1.0f ;
  theStartLineCap(theLineStyle(*gstateptr)) = 0 ;
  theEndLineCap(theLineStyle(*gstateptr)) = 0 ;
  theDashLineCap(theLineStyle(*gstateptr)) = 0 ;
  theLineJoin(theLineStyle(*gstateptr)) = 0 ;
  theDashPattern(theLineStyle(*gstateptr)) = onull ; /* Struct copy to set slot properties */
  oArray(theDashPattern(theLineStyle(*gstateptr))) = NULL ;
  theDashOffset(theLineStyle(*gstateptr)) = 0.0f ;
  (void)gs_storedashlist(&theLineStyle(*gstateptr), NULL, 0) ;
  gstateptr->thestyle.dashmode = DASHMODE_FIXED ;
  theMiterLimit(theLineStyle(*gstateptr)) = 10.0f ;

  return gsc_initgraphics( gstateptr->colorInfo ) ;
}


/* ----------------------------------------------------------------------------
   function:            setlinewidth_()    author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 219.

---------------------------------------------------------------------------- */
Bool gs_setlinewidth( STACK *stack )
{
  int32 stacksize ;
  OBJECT *theo ;
  USERVALUE arg ;

  HQASSERT( stack , "stack is null in gs_setlinewidth" ) ;

  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( *stack , stacksize ) ;
  if ( !object_get_real(theo, &arg) )
    return FALSE ;

  /* Check the validity of line width parameter. */
  if ( arg < 0.0f )
    arg = -arg ;

  theLineWidth(theLineStyle(*gstateptr)) = arg ;

  pop(stack) ;

  return TRUE ;
}

Bool setlinewidth_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  return gs_setlinewidth( & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            currlinewidth_()   author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 137.

---------------------------------------------------------------------------- */
Bool currlinewidth_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_real( theLineWidth(theLineStyle(*gstateptr)),
                          &operandstack) ;
}


/* ----------------------------------------------------------------------------
   function:            setlinecap_()      author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 217.

---------------------------------------------------------------------------- */
Bool gs_setlinecap( STACK *stack )
{
  int32 stacksize ;
  OBJECT *theo ;
  int32 arg ;

  HQASSERT( stack , "stack is null in gs_setlinecap" ) ;

  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( *stack , stacksize ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  arg = oInteger(*theo) ;

  /* 0 - butt cap; 1 - round cap; 2 - projecting square cap */
  if ( arg < 0 || arg > 2 )
    return error_handler( RANGECHECK ) ;

  theStartLineCap(theLineStyle(*gstateptr)) = ( uint8 )arg ;
  theEndLineCap(theLineStyle(*gstateptr)) = ( uint8 )arg ;
  theDashLineCap(theLineStyle(*gstateptr)) = ( uint8 )arg ;

  pop(stack) ;

  return TRUE ;
}

Bool setlinecap_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  return gs_setlinecap( & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            currlinecap_()     author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 136.

---------------------------------------------------------------------------- */
Bool currlinecap_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_integer(theStartLineCap(theLineStyle(*gstateptr)),
                            &operandstack) ;
}

/* currentendlinecap and currentdashcap are extra operators for handling line
   end and dash caps arising from XPS jobs which specify start, end and dash
   caps independently (start line cap is returned by currentlinecap). In PS and
   PDF jobs, all three line caps will be the same and so currentlinecap gives
   the expected result. */

Bool currendlinecap_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_integer(theEndLineCap(theLineStyle(*gstateptr)),
                            &operandstack) ;
}

Bool currdashcap_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_integer(theDashLineCap(theLineStyle(*gstateptr)),
                            &operandstack) ;
}


/* ----------------------------------------------------------------------------
   function:            setlinejoin_()     author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 218.

---------------------------------------------------------------------------- */
Bool gs_setlinejoin( STACK *stack )
{
  int32 stacksize ;
  OBJECT *theo ;
  int32 arg ;

  HQASSERT( stack , "stack is null in gs_setlinejoin" ) ;

  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( *stack , stacksize ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  arg = oInteger(*theo) ;

  /* 0 - miter join; 1 - round join; 2 - bevel join */
  if ( arg < 0 || arg > 2 )
    return error_handler( RANGECHECK ) ;

  theLineJoin(theLineStyle(*gstateptr)) = ( uint8 )arg ;

  pop(stack) ;

  return TRUE ;
}

Bool setlinejoin_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  return gs_setlinejoin( & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            currlinejoin_()    author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 137.

---------------------------------------------------------------------------- */
Bool currlinejoin_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_integer(theLineJoin(theLineStyle(*gstateptr)),
                            &operandstack) ;
}


/* ----------------------------------------------------------------------------
   function:            setmiterlimit_()   author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 220.

---------------------------------------------------------------------------- */
Bool gs_setmiterlimit( STACK *stack )
{
  int32 stacksize ;
  OBJECT *theo ;
  USERVALUE arg ;

  HQASSERT( stack , "stack is null in gs_setmiterlimit" ) ;

  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( *stack , stacksize ) ;

  if ( !object_get_real(theo, &arg) )
    return FALSE ;

  /* Check the validity of mitre limit parameter. */
  if ( arg < 1.0f )
    return error_handler( RANGECHECK ) ;

  theMiterLimit(theLineStyle(*gstateptr)) = arg ;

  pop(stack) ;

  return TRUE ;
}

Bool setmiterlimit_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  return gs_setmiterlimit( & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            currmiterlimit_()  author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 137.

---------------------------------------------------------------------------- */
Bool currmiterlimit_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_real( theMiterLimit(theLineStyle(*gstateptr)),
                          &operandstack) ;
}


/* ----------------------------------------------------------------------------
   function:            setdash_()         author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 214.

---------------------------------------------------------------------------- */
Bool gs_setdash( STACK *stack , Bool retainobj )
{
  Bool success = FALSE ;
  uint16 len ;
  uint32 i ;
  OBJECT *theo , *alist ;
  SYSTEMVALUE offset , sum ;
  SYSTEMVALUE *temppattern ;
  void *map ;
  uint32 size, sema ;

  if ( theIStackSize( stack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( !object_get_numeric(theTop(*stack), &offset) )
    return FALSE ;

  theo = stackindex( 1 , stack ) ;
  switch ( oType(*theo) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  sema = get_basemap_semaphore(&map, &size) ;
  if ( sema == 0 )
    return error_handler( VMERROR ) ;

#define return DO_NOT_RETURN_use_goto_setdash_cleanup_INSTEAD!!

  temppattern = map ;
  size = size / sizeof( SYSTEMVALUE ) ;
  /* dash list length can't exceed a uint16 */
  if ( size > 65535 )
    size = 65535 ;

  len = theLen(*theo) ;
  if ( len > size ) {
    ( void )error_handler( LIMITCHECK ) ;
    goto setdash_cleanup ;
  }

  /* Build the temporary array, checking elements as I go */
  sum = 0.0 ;
  alist = oArray(*theo) ;

  for ( i = 0; i < len ; i++ ) {
    SYSTEMVALUE tempvalue ;

    if ( !object_get_numeric(alist, &tempvalue) )
      goto setdash_cleanup ;

    if ( tempvalue < 0.0 ) {
      ( void ) error_handler( RANGECHECK ) ;
      goto setdash_cleanup ;
    }
    sum += tempvalue ;
    temppattern[ i ] = tempvalue ;
    ++alist ;
  }
  if (( len != 0 ) && ( sum == 0.0 )) {
    ( void ) error_handler( RANGECHECK ) ;
    goto setdash_cleanup ;
  }

  if ( !gs_storedashlist(&theLineStyle(*gstateptr), temppattern, len) )
    goto setdash_cleanup ;

  theDashOffset(theLineStyle(*gstateptr)) = (USERVALUE)offset ;

  if ( retainobj )
    Copy( & theDashPattern(theLineStyle(*gstateptr)) , theo ) ;
  else
    theTags( theDashPattern(theLineStyle(*gstateptr))) = ONULL | LITERAL ;

  success = TRUE ;
 setdash_cleanup:
  free_basemap_semaphore(sema) ;

  /* Client must pop values from the stack because of the way
   * PDF operator d works - see pdfgstat.c
   */

#undef return
  return success ;
}

/* Allocates the precise amount of memory to store the dash list.  The
   'dashlist' argument remains the responsibility of the caller, it is just
   copied over.  gs_storedashlist(linestyle, NULL, 0) can't fail and can be
   used to free the dash list.
 */
Bool gs_storedashlist( LINESTYLE *linestyle, SYSTEMVALUE *dashlist, uint16 dashlistlen )
{
  HQASSERT( linestyle, "linestyle is missing in gsc_storedashlist" ) ;

  if ( theDashListLen(*linestyle) > 0 &&
       theDashList(*linestyle) == dashlist &&
       theDashListLen(*linestyle) == dashlistlen ) {
    HQFAIL( "source and destination memory is identical - gs_storedashlist won't work" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  if ( dashlistlen != theDashListLen(*linestyle) ) {

    if ( theDashListLen(*linestyle) > 0 ) {
      mm_free( mm_pool_temp,
               theDashList(*linestyle),
               theDashListLen(*linestyle) * sizeof(SYSTEMVALUE) ) ;
      theDashListLen(*linestyle) = 0 ;
    }

    if ( dashlistlen > 0 ) {
      theDashList(*linestyle) = mm_alloc( mm_pool_temp,
                                          dashlistlen * sizeof(SYSTEMVALUE),
                                          MM_ALLOC_CLASS_GSTATE ) ;
      if ( theDashList(*linestyle) == NULL )
        return error_handler( VMERROR ) ;
      theDashListLen(*linestyle) = dashlistlen ;
    }
  }

  if ( dashlistlen > 0 ) {
    HqMemCpy( theDashList(*linestyle), dashlist, dashlistlen * sizeof(SYSTEMVALUE) ) ;
  }

  return TRUE ;
}

Bool setdash_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  if ( ! gs_setdash( & operandstack , TRUE ))
    return FALSE ;

  npop( 2 , & operandstack ) ;

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            currdash_()        author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 214.

---------------------------------------------------------------------------- */
Bool currdash_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Did we retain the array object passed to gs_setdash? */

  if ( oType(theDashPattern(theLineStyle(*gstateptr))) == OARRAY) {
    if ( ! push( & theDashPattern(theLineStyle(*gstateptr)) , & operandstack ))
      return FALSE ;
  }
  else {
    OBJECT fake = OBJECT_NOTVM_NOTHING ;

    if ( ! gs_fakedash( & fake ,
                        theDashList(theLineStyle(*gstateptr)) ,
                        theDashListLen(theLineStyle(*gstateptr))) ||
         ! push( & fake , & operandstack ))
      return FALSE ;
  }

  return stack_push_real( theDashOffset(theLineStyle(*gstateptr)),
                          &operandstack) ;
}

/* This routine is for the odd situation where the dash has
 * been set by PDF (not by setdash_) but currentdash_ has been
 * called, e.g. by IDLOM.
 */

Bool gs_fakedash( OBJECT *theo , SYSTEMVALUE *dashlist , uint16 len )
{
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT( theo , "theo NULL in gs_fakedash" ) ;

  theTags(*theo) = OARRAY | LITERAL | UNLIMITED ;
  SETGLOBJECT(*theo, corecontext) ;
  theLen(*theo) = len ;

  if ( len == 0 ) {
    oArray(*theo) = NULL ;
  }
  else {
    OBJECT *olist ;
    int32 i ;

    olist = get_omemory( len ) ;
    if ( olist == NULL )
      return error_handler( VMERROR ) ;

    oArray(*theo) = olist ;

    for ( i = 0 ; i < len ; ++i ) {
      object_store_numeric(&olist[i], *dashlist++) ;
    }
  }

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            setflat_()         author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 215.

---------------------------------------------------------------------------- */
Bool gs_setflat( STACK *stack )
{
  int32 stacksize ;
  OBJECT *theo ;
  USERVALUE arg ;
#ifdef flatness_control
  USERVALUE rng ;
#endif

  HQASSERT( stack , "stack is null in gs_setflat" ) ;

  stacksize = theIStackSize( stack ) ;
  if ( stacksize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( *stack , stacksize ) ;
  if ( !object_get_real(theo, &arg) )
    return FALSE ;

  /* Check the validity of flatness parameter. */
  if ( arg < 0.0f )
    arg = -arg ;

#ifdef flatness_control
  rng = sqrt( 0.5f * (( xdpi * xdpi ) + ( ydpi * ydpi ))) / 600.0f ;
  if ( arg < rng )
    arg = rng ;
#else
  if ( arg < 0.2f )
    arg = 0.2f ;
#endif
  else if ( arg > 100.0f )
    arg = 100.0f ;

  theFlatness(theLineStyle(*gstateptr)) = arg ;

  pop(stack) ;
  return TRUE ;
}

Bool setflat_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  return gs_setflat( & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            currflat_()        author:              Andrew Cave
   creation date:       15-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

    See PostScript reference manual page 135.

---------------------------------------------------------------------------- */
Bool currflat_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_real( theFlatness(theLineStyle(*gstateptr)),
                          &operandstack) ;
}


/* ---------------------------------------------------------------------- */
Bool gpath_(ps_context_t *pscontext)
{
  /* a Harlequin internaldict extension: replaces the current path with the
     path in the gstate on top of the graphics stack: used for trapping */

  GSTATE *gs = gstackptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return ( gs_newpath() &&
           path_copy(&thePathInfo(*gstateptr), &thePathInfo(*gs), mm_pool_temp)) ;
}


/* ----------------------------------------------------------------------------
   function:            gsave_()           author:              Andrew Cave
   creation date:       10-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 166.

---------------------------------------------------------------------------- */
Bool gsave_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

/*
  Push the graphics state onto the graphics stack, indicating
  that  it  was pushed  by  a  gsave, rather  than by a save.
*/
  return gs_gpush( GST_GSAVE ) ;
}


/* ----------------------------------------------------------------------------
   function:            grestore_()        author:              Andrew Cave
   creation date:       10-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 165.

---------------------------------------------------------------------------- */
Bool grestore_(ps_context_t *pscontext)
{
  GSTATE *gs_new = gstackptr ;
  Bool   docopy = FALSE ;
  Bool   dopop  = TRUE ;
  deactivate_pagedevice_t dpd ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  switch ( gs_new->gType ) {
  case GST_GSAVE:
    /* We use GST_something/GST_GSAVE pairs for nested targets eg. forms,
     * patterns, etc but we need to check that there is something else
     * 'cos there _may_ be a gsave+grestore early on when booting the
     * rip - before we've done the first save.
     */
    if ( gs_new->next == NULL )
      break ;
    switch ( gs_new->next->gType ) {
    case GST_GSAVE:
    case GST_SAVE:
    case GST_SHOWPAGE:
    case GST_SETPAGEDEVICE:
    case GST_PCL5:
    case GST_HPGL:
      break ;
    case GST_FORM:
    case GST_PATTERN:
    case GST_SETCHARDEVICE:
    case GST_SHADING:
    case GST_GROUP:
      docopy = TRUE ; /* Nested target; treat as save, grestore */
      dopop  = FALSE ;
      break ;
    case GST_NOTYPE:
    case GST_CURRENT:
      HQFAIL( "grestore: unexpected gstate type" ) ;
      return error_handler( UNREGISTERED ) ;
    default:
      HQFAIL( "grestore: unknown gstate type" ) ;
      return error_handler( UNREGISTERED ) ;
    }
    break ;

  case GST_SAVE:
  case GST_SHOWPAGE:
  case GST_SETPAGEDEVICE:
    docopy = TRUE ;
    dopop  = FALSE ;
    break ;

  case GST_FORM:
  case GST_PATTERN:
  case GST_SETCHARDEVICE:
  case GST_SHADING:
  case GST_GROUP:
    return error_handler( UNREGISTERED ) ;

  case GST_NOTYPE:
  case GST_CURRENT:
    HQFAIL( "grestore: unexpected gstate type" ) ;
    return error_handler( UNREGISTERED ) ;

  default:
    HQFAIL( "grestore: unknown gstate type" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  deactivate_pagedevice( gstateptr , gs_new , NULL , & dpd ) ;
  return gs_setgstate( gs_new , GST_GSAVE , docopy , dopop , TRUE , & dpd ) &&
         do_pagedevice_reactivate(pscontext, &dpd) ;
}


/* ----------------------------------------------------------------------------
   function:            clipsave_()     author:              Andrew Cave
   creation date:       06-Feb-1998     last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 166.

---------------------------------------------------------------------------- */
Bool clipsave_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  return gs_cpush() ;
}


/* ----------------------------------------------------------------------------
   function:            cliprestore_()     author:              Andrew Cave
   creation date:       06-Feb-1998        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 166.

---------------------------------------------------------------------------- */
Bool cliprestore_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  return gs_ctop() ;
}


/* ----------------------------------------------------------------------------
   function:            allgrestore_()     author:              Andrew Cave
   creation date:       10-Nov-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 166.

---------------------------------------------------------------------------- */
Bool allgrestore_(ps_context_t *pscontext)
{
  GSTATE *gs_new = gstackptr ;
  GSTATE *gs_next ;
  Bool foundbaselevel = FALSE ;
  deactivate_pagedevice_t dpd ;

  if ( gs_locked )
    return error_handler(INVALIDACCESS);

  while ( ! foundbaselevel ) {
    switch ( gs_new->gType ) {
    case GST_GSAVE:
      /* We use GST_something/GST_GSAVE pairs for nested targets eg. forms,
       * patterns, etc. A grestoreall can't be used before we've done our
       * first save.
       */
      if (( gs_next = gs_new->next) == NULL )
        return error_handler( UNDEFINEDRESULT ) ;
      switch ( gs_next->gType ) {
      case GST_GSAVE:
      case GST_SAVE:
      case GST_SHOWPAGE:
      case GST_SETPAGEDEVICE:
        gs_new = gs_next ;
        break ;

      case GST_FORM:
      case GST_PATTERN:
      case GST_SETCHARDEVICE:
      case GST_SHADING:
      case GST_GROUP:
        foundbaselevel = TRUE ;
        break ;

      case GST_NOTYPE:
      case GST_CURRENT:
        HQFAIL( "grestoreall: unexpected gstate type" ) ;
        return error_handler( UNREGISTERED ) ;
      default:
        HQFAIL( "grestoreall: unknown gstate type" ) ;
        return error_handler( UNREGISTERED ) ;
      }
      break ;

    case GST_SAVE:
    case GST_SHOWPAGE:
    case GST_SETPAGEDEVICE:
      foundbaselevel = TRUE ;
      break ;

    case GST_FORM:
    case GST_PATTERN:
    case GST_SETCHARDEVICE:
    case GST_SHADING:
    case GST_GROUP:
      return error_handler( UNREGISTERED ) ;

    case GST_NOTYPE:
    case GST_CURRENT:
      HQFAIL( "grestoreall: unexpected gstate type" ) ;
      return error_handler( UNREGISTERED ) ;

    default:
      HQFAIL( "grestoreall: unknown gstate type" ) ;
      return error_handler( UNREGISTERED ) ;
    }
  }

  deactivate_pagedevice( gstateptr , gs_new , NULL , & dpd ) ;
  return gs_setgstate( gs_new , GST_GSAVE , TRUE , TRUE , TRUE , & dpd ) &&
         do_pagedevice_reactivate(pscontext, &dpd) ;
}


/* These are not GC roots, because they contain the gstates allocated by */
/* the user, and not necessarily placed on any stack.  If the user loses all */
/* pointers to them, we want them to go away. */
GSTATE *lgframes = NULL ;
GSTATE *ggframes = NULL ;


/** freeGStatesList - discard gstates down to given save level */

void freeGStatesList( int32 slevel )
{
  HQASSERT( slevel >= 0 , "can't have -ve slevels" ) ;

  while ( lgframes &&
          lgframes->slevel > slevel ) {
    gs_discardgstate( lgframes ) ;
    lgframes = lgframes->next ;
  }

  if ( NUMBERSAVES( slevel ) <= 1 ) {
    while ( ggframes &&
            ggframes->slevel > slevel ) {
      gs_discardgstate( ggframes ) ;
      ggframes = ggframes->next ;
    }
  }
}


/** gstate_finalize - finalize a gstate_finalize: discard it and unlink it
 *
 * Compare this to fileio_finalize.
 */
void gstate_finalize(GSTATE *obj)
{
  register GSTATE *curr;
  register GSTATE **prev;
  GSTATE **next;
  int found = FALSE;

  gs_discardgstate( obj );
  /* Find which of the two lists it's on and unlink it */
  next = &ggframes;
  for ( prev = &lgframes ; prev ; prev = next, next = NULL ) {
    while (( curr = *prev ) != NULL ) {
      if ( curr == obj ) {
        *prev = curr->next; found = TRUE;
        break;
      } else {
        prev = &curr->next;
      }
    }
    if ( found ) break;
  }
  HQASSERT( found, "Couldn't unlink finalized gstate" );
}


/* ---------------------------------------------------------------------------*/

static void apply_pagebasematrix_to_one_path(OMATRIX *diffmatrix,
                                             PATHINFO *path)
{
  register LINELIST *theline ;
  register PATHLIST *thepath ;

  /* Need to clear this flag.
   * Can't transform the bbox since the two may differ (due to rotations, etc...).  */
  path->bboxtype = BBOX_NOT_SET ;

  for ( thepath = path->firstpath ; thepath ; thepath = thepath->next)
    for ( theline = theSubPath(*thepath) ; theline ; theline = theline->next)
      MATRIX_TRANSFORM_XY( theX(thePoint(*theline)) ,
                           theY(thePoint(*theline)) ,
                           theX(thePoint(*theline)) ,
                           theY(thePoint(*theline)) ,
                           diffmatrix ) ;
}

typedef struct page_matrix_args {
  OMATRIX *om1 ;
  OMATRIX *om2 ;
  int32 pagebaseid ;
} PAGE_MATRIX_ARGS ;

static Bool apply_pagebasematrix_to_one_gstate(GSTATE *gs, void *arg)
{
  CLIPRECORD *cliprec ;
  CLIPPATH *clippath ;
  OMATRIX *oldPageBaseMatrix ;
  OMATRIX *newPageBaseMatrix ;
  OMATRIX coord_transform ;
  OMATRIX pagebase_transform ;

  PAGE_MATRIX_ARGS *pmargs = ( PAGE_MATRIX_ARGS * )arg ;

  HQASSERT( gs , "gs NULL in apply_pagebasematrix_to_one_gstate" ) ;
  HQASSERT( arg , "arg NULL in apply_pagebasematrix_to_one_gstate" ) ;

  /** \todo @@@ TODO FIXME: If the pagebasematrixid is updated to an invalid
     value on opening a CHAR, NULL, or recursive pattern gstate, this test
     can be simplified to just use the pagebaseid test. */
  if ( ((!dev_is_bandtype(thegsDeviceType(*gs))) &&
        thegsDeviceType(*gs) != DEVICE_PATTERN1 &&
        thegsDeviceType(*gs) != DEVICE_PATTERN2) ||
       thegsPageBaseID(gs->thePDEVinfo) != pmargs->pagebaseid )
    return TRUE ;

  oldPageBaseMatrix = pmargs->om1 ;
  newPageBaseMatrix = pmargs->om2 ;

  /* let D  be the default matrix,
         P  be the page base matrix,
         Dp be the default matrix as the user perceives it, i.e. after
               manipulation by BeginPage
         U  be the transform the user has applied
         C  be the current CTM
               subscript f be from state and subscript t be to state.
     Then
         Dp = P D
     Also
         C = U Dp
     =>  C = U P D
     We want to fool the user in a number of places by taking out the
     effects of P, giveing a psuedo CTM Cp, where
         Cp = U D
     Now,
         C = U P D
     therefore
         C (P D)^-1 = U (P D) (P D)^-1
      => C (P D)^-1 = U
      => Cp = C (P D)^-1 D    (*)
      => Cp = C D^-1 P^-1 D
     In a grestore we want to both take out of the ctm the old page
     base (in the from state) and replace it with the new pagepbase
     in the to state. Normally
         Ct := Cf
     but in this case,
         Ct := Cpf Df^-1 Pt Dt
     (note the Pt). From (*) therefore
         Cpf = cf (Pf Df)^-1 Df
     and thus
         Ct = Cf (Pf Df)^-1 Df Df^-1 Pt Dt
      => Ct = Cf (Pf Df)^-1 Pt Dt
     and that is what the following code computes
  */

  matrix_mult(oldPageBaseMatrix,
              &thegsDevicePageCTM(*gs),
              &pagebase_transform);

  if (! matrix_inverse(& pagebase_transform, & pagebase_transform))
    return FALSE ;

  matrix_mult(&thegsPageCTM(*gs),
              &pagebase_transform,
              &pagebase_transform);

  matrix_mult(&pagebase_transform,
              newPageBaseMatrix,
              &pagebase_transform);

  matrix_mult(&pagebase_transform,
              &thegsDevicePageCTM(*gs),
              &thegsPageCTM(*gs));

  matrix_clean(&thegsPageCTM(*gs));

/* Now transform the points in the path + clipping paths */
/* First of all work out the lovely matrix... */
  matrix_mult(oldPageBaseMatrix,
              &thegsDevicePageCTM(*gs),
              &coord_transform);

  if (! matrix_inverse(&coord_transform, &coord_transform))
    return FALSE;

  matrix_mult(&coord_transform,
              newPageBaseMatrix,
              &coord_transform);

  matrix_mult(&coord_transform,
              &thegsDevicePageCTM(*gs),
              &coord_transform);

  matrix_clean(&coord_transform);

  apply_pagebasematrix_to_one_path(&coord_transform, &thePathInfo(*gs)) ;

  thegsPageBaseID(gs->thePDEVinfo) = pageBaseMatrixId ;

  /* Pattern phase (esp. shaded patterns) affected by base matrix of page at
   * makepattern time.
   */
  for ( clippath = &thegsPageClip(*gs) ;
        clippath ;
        clippath = clippath->next) {
    for ( cliprec = theClipRecord(*clippath) ; cliprec ; cliprec = cliprec->next) {
      /* Note that clipobject must be nulled rather than have pagebasematrix
       * applied to it, because it is shared between DL elements. Applying the
       * matrix would shift the area that already imaged page elements applied
       * to.
       */
      if ( thegsPageBaseID(*cliprec) != pageBaseMatrixId ) {
        thegsPageBaseID(*cliprec) = pageBaseMatrixId ;
        apply_pagebasematrix_to_one_path( & coord_transform ,
                                          & theClipPath(*cliprec)) ;
      }
    }
  }

  return TRUE ;
}


Bool apply_pagebasematrix_to_all_gstates(OMATRIX *oldPageBaseMatrix,
                                         OMATRIX *newPageBaseMatrix,
                                         int32 oldPageBaseMatrixId)
{
  PAGE_MATRIX_ARGS pmargs ;

  HQASSERT( oldPageBaseMatrix ,
            "oldpagebasematrix is null in apply_pagebasematrix_to_all_gstates" ) ;
  HQASSERT( newPageBaseMatrix ,
            "newpagebasematrix is null in apply_pagebasematrix_to_all_gstates" ) ;

  pmargs.om1 = oldPageBaseMatrix ;
  pmargs.om2 = newPageBaseMatrix ;
  pmargs.pagebaseid = oldPageBaseMatrixId ;

  return gs_forall(apply_pagebasematrix_to_one_gstate, &pmargs, FALSE, FALSE) ;
}


/* ---------------------------------------------------------------------------*/
static Bool clear_one_gstate_dlpointers( GSTATE *gs , void *unused_arg )
{
  UNUSED_PARAM( void * , unused_arg ) ;

  HQASSERT( gs , " gs is null in clear_one_gstate_cliprect" ) ;

  gs->theGSTAGinfo.structure = NULL ;

  return TRUE ;
}

void clear_gstate_dlpointers( void )
{
  /* Must invalidate all clip pointers in all gstates... */
  ( void ) gs_forall( clear_one_gstate_dlpointers , NULL , TRUE , TRUE ) ;
}

/* ---------------------------------------------------------------------------*/
static Bool invalidate_one_gstate_screens( GSTATE *gs , void *regenscreen )
{
  Bool val ;

  HQASSERT( gs , "gs is null in invalidate_one_gstate_screens" ) ;
  HQASSERT( regenscreen , "val is null in invalidate_one_gstate_screens" ) ;

  val = *(Bool *)regenscreen ;
  gsc_invalidate_one_gstate_screens(gs->colorInfo, ( uint8 )val) ;
  return TRUE ; /* return required for generic walk function. */
}

void invalidate_gstate_screens( void )
{
  Bool regenscreen = TRUE ;
  /* Invalidate gstate spotnos to force subsequent inheritance if
   * colour has changed.
   * Not necessary to invalidate gstate screens for the grestore
   * frames as they will get deleted anyway if they need to be.
   */
  (void)gs_forall(invalidate_one_gstate_screens, &regenscreen, FALSE, FALSE) ;
}

/* ---------------------------------------------------------------------------*/
/** Invalidate the color chains for all gstates or just those in the supplied
 * save level.
 */
static Bool invalidateOneGstateColor( GSTATE *gs , void *arg )
{
  int32 saveLevel;

  HQASSERT( gs != NULL, "gs NULL in invalidateOneGstateColor" ) ;
  HQASSERT( arg != NULL, "arg NULL in invalidateOneGstateColor" );

  saveLevel = *((int32 *) arg);

  if (saveLevel == -1 || saveLevel == gs->slevel)
    gsc_markChainsInvalid( gs->colorInfo );

  return TRUE;
}

void gs_InvalidColorForSaveLevel( void )
{
  int32 saveLevel = get_core_context_interp()->savelevel;

  ( void ) gs_forall( invalidateOneGstateColor , &saveLevel , TRUE , TRUE ) ;
}

void gs_invalidateAllColorChains( void )
{
  int32 saveLevel = -1; /* Means invalidate gstates for all save levels */

  ( void ) gs_forall( invalidateOneGstateColor , &saveLevel , TRUE , TRUE ) ;
}

/* ---------------------------------------------------------------------------*/
static Bool gs_forallpsgstates(GSTATE *gs,
                               Bool (*gs_fn)(GSTATE *, void *),
                               void *args)
{
  HQASSERT( gs_fn , "gs_fn is null is gs_forallpsgstates" ) ;

  while ( gs ) {
    if ( ! ( *gs_fn )( gs , args ))
      return FALSE ;
    gs = gs->next ;
  }
  return TRUE ;
}

Bool gs_forall(Bool (*gs_fn)(GSTATE *, void *), void *args,
               Bool dogstate, Bool dogrframes)
{
  HQASSERT( gstackptr == gstateptr->next,
            "gs_forall: graphics stack is corrupt" ) ;

  return gs_forallpsgstates( dogstate ? gstateptr : gstackptr , gs_fn , args ) &&
         gs_forallpsgstates( lgframes  , gs_fn , args ) &&
         gs_forallpsgstates( ggframes  , gs_fn , args ) &&
         ( ! dogrframes ||
           gs_forallpsgstates( grframes  , gs_fn , args )) &&
         pdf_walk_gstack( gs_fn , args ) ;
}


/* ----------------------------------------------------------------------------
   function:            gstate_           author:              Luke Tunmer
   creation date:       21-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool gstate_(ps_context_t *pscontext)
{
  GSTATE *gs ;
  OBJECT gobj = OBJECT_NOTVM_NOTHING ;
  corecontext_t *corecontext = pscontext->corecontext ;

  /* if the current alloc mode is global, check there are no local entries
     in the gstate */
  if ( corecontext->glallocmode ) {
    if ( ! check_gstate(corecontext, gstateptr ))
      return error_handler( INVALIDACCESS ) ;
  }
  gs = mm_ps_alloc_typed( mm_pool_ps_typed, gstate_size(NULL));
  if ( gs == NULL )
    return error_handler( VMERROR );
  gs_updatePtrs(gs);

  if ( ! gs_copygstate(gs, gstateptr, GST_GSAVE, NULL) )
    return FALSE ;

  gs->typetag = tag_GSTATE ;
  /* New gstate, so we give it a new id */
  gs->gId   = ++gstateId ;
  gs->gType = GST_NOTYPE ;
  gs->next  = NULL ;

  NEWGSTATESAVED(gs, corecontext) ;

  if ( corecontext->glallocmode ) {
    gs->next = ggframes ; /* Link onto globl list */
    ggframes = gs ;
  }
  else {
    gs->next = lgframes ; /* Link onto local list  */
    lgframes = gs ;
  }

  oGState(gobj) = gs ;
  theTags(gobj) = OGSTATE | LITERAL | UNLIMITED ;
  SETGLOBJECT(gobj, corecontext) ;
  return push( &gobj , &operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            currentgstate_    author:              Luke Tunmer
   creation date:       21-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool currentgstate_(ps_context_t *pscontext)
{
  int32 glmode ;
  OBJECT *theo ;
  GSTATE *gs ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;
  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OGSTATE )
    return error_handler( TYPECHECK ) ;
  if (! oCanWrite(*theo) && ! object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;
  glmode = oGlobalValue(*theo) ;
  if ( glmode )
    if ( ! check_gstate(corecontext, gstateptr ) )
      return error_handler( INVALIDACCESS ) ;

  gs = oGState(*theo) ;

  if ( GSTATEISNOTSAVED(gs, corecontext) )
    if ( ! check_gsave(corecontext, gs , glmode ))
      return FALSE ;

  /* Remove anything already in the destination gstate */
  gs_discardgstate( gs ) ;

  /* Copy the content of the gstate but leave id, type, next intact */
  if ( ! gs_copygstate(gs, gstateptr, GST_GSAVE, NULL) )
    return FALSE ;

  NEWGSTATESAVED(gs, corecontext) ;

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            setgstate_        author:              Luke Tunmer
   creation date:       21-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool setgstate_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  GSTATE *gs_new ;
  deactivate_pagedevice_t dpd ;

  /* Tests on Distiller 4.05 seem to disallow setgstate in char contexts. I
     am assuming it is the same for pattern contexts (it could change the
     device type, so this probably a safe assumption). */
  if ( DEVICE_INVALID_CONTEXT() )
    return error_handler( UNDEFINED ) ;

  if ( gs_locked )
    return error_handler( INVALIDACCESS );

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;
  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OGSTATE )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  gs_new = oGState(*theo) ;
  deactivate_pagedevice( gstateptr , gs_new , NULL , & dpd ) ;
  if ( ! gs_setgstate( gs_new ,
                       GST_GSAVE , TRUE , FALSE , TRUE , & dpd ))
    return FALSE ;

  pop( & operandstack ) ;

  return do_pagedevice_reactivate(pscontext, &dpd) ;
}


/* ----------------------------------------------------------------------------
   function:            check_gstate      author:              Luke Tunmer
   creation date:       21-Oct-1991       last modification:   ##-###-####
   arguments:
   description:

   Check that the gstate structure has no local object in it.
---------------------------------------------------------------------------- */

/* check a font */
static Bool fontInfo_areobjectsglobal(corecontext_t *corecontext, FONTinfo *fi)
{
  if ( illegalLocalIntoGlobal(&theMyFont(*fi), corecontext) )
    return FALSE ;
  if ( illegalLocalIntoGlobal(&fi->rootfont, corecontext) )
    return FALSE ;
  if ( illegalLocalIntoGlobal(&fi->subfont, corecontext) )
    return FALSE ;
  if ( illegalLocalIntoGlobal(&theEncoding(*fi), corecontext) )
    return FALSE ;
  if ( theMetrics(*fi) != NULL &&
       illegalLocalIntoGlobal(theMetrics(*fi), corecontext) )
    return FALSE ;
  if ( theMetrics2(*fi) != NULL &&
       illegalLocalIntoGlobal(theMetrics2(*fi), corecontext) )
    return FALSE ;
  if ( fi->fontbbox != NULL &&
       illegalLocalIntoGlobal(fi->fontbbox, corecontext) )
    return FALSE ;
  if ( illegalLocalIntoGlobal(&theFDepVector(*fi), corecontext) )
    return FALSE ;
  if ( illegalLocalIntoGlobal(&thePrefEnc(*fi), corecontext) )
    return FALSE ;
  return TRUE;
}

Bool check_gstate(corecontext_t *corecontext, GSTATE *gs )
{
  /* check device */
  if ( illegalLocalIntoGlobal(&thegsDeviceShowProc(*gs), corecontext) ||
       illegalLocalIntoGlobal(&thegsDevicePageDict(*gs), corecontext) )
    return FALSE ;

  if ( ! gsc_areobjectsglobal(corecontext, gs->colorInfo ))
    return FALSE ;

  /* check the linestyle */
  if ( illegalLocalIntoGlobal(&theDashPattern(theLineStyle(*gs)), corecontext) )
    return FALSE ;

  /* check the font */
  if ( ! fontInfo_areobjectsglobal(corecontext, &theFontInfo( *gs )))
    return FALSE;

  if ( illegalLocalIntoGlobal(&gs->theHDLTinfo.hooksDict, corecontext) )
    return FALSE ;

  if ( illegalLocalIntoGlobal(&gs->theHDLTinfo.hooksOrig, corecontext) )
    return FALSE ;

  if ( illegalLocalIntoGlobal(&gs->theGSTAGinfo.dict, corecontext) )
    return FALSE ;

  return TRUE ;
}

/* transState query method.
*/
TranState* gsTranState(GSTATE* gs)
{
  HQASSERT(gs != NULL, "gsTranState - 'gs' parameter cannot be NULL");

  return &gs->tranState;
}

/** gs_scan, fontInfo_scan - scanning function for GSTATEs
 *
 * This should match check_gstate, since both need look at all the VM
 * pointers. */

static mps_res_t fontInfo_scan(mps_ss_t ss, FONTinfo *fi)
{
  mps_res_t res;

  res = ps_scan_field( ss, &theMyFont( *fi ));
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &fi->rootfont );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &fi->subfont );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &theEncoding( *fi ));
  if ( res != MPS_RES_OK ) return res;
  if ( theMetrics(*fi) != NULL ) {
    res = ps_scan_field( ss, theMetrics(*fi));
    if ( res != MPS_RES_OK ) return res;
  }
  if ( theMetrics2(*fi) != NULL ) {
    res = ps_scan_field( ss, theMetrics2(*fi));
    if ( res != MPS_RES_OK ) return res;
  }
  if ( fi->fontbbox != NULL ) {
    res = ps_scan_field( ss, fi->fontbbox);
    if ( res != MPS_RES_OK ) return res;
  }
  res = ps_scan_field( ss, &theFDepVector( *fi ));
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &thePrefEnc( *fi ));
  return res;
}

mps_res_t MPS_CALL gs_scan(size_t *len_out, mps_ss_t ss, GSTATE *gs)
{
  mps_res_t res;

  /* The list of gstates is traversed in the caller. */
  res = gucr_rasterstyle_scan(ss, gsc_getRS(gs->colorInfo));
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field(ss, &thegsDeviceShowProc(*gs));
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field(ss, &thegsDevicePageDict(*gs));
  if ( res != MPS_RES_OK ) return res;
  res = gsc_scan ( ss, gs->colorInfo );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &theDashPattern(theLineStyle(*gs)));
  if ( res != MPS_RES_OK ) return res;
  res = fontInfo_scan( ss, &theFontInfo( *gs ));
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field(ss, &gs->theHDLTinfo.hooksDict);
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field(ss, &gs->theHDLTinfo.hooksOrig);
  if ( res != MPS_RES_OK ) return res;
  /* theIdlomObject and theIdlomBaseFontInfo are reachable from the
   * dictionary or elsewhere in the gstate. */
  /* backdropColor and transferFunction fields in TranState are PDF, not PS. */

  res = ps_scan_field( ss, &gs->theGSTAGinfo.dict );
  *len_out = gstate_size(NULL);
  return res;
}


/** gstate_size - return the size of the gstate object (not rounded) */
size_t gstate_size(GSTATE *gs)
{
  UNUSED_PARAM( GSTATE *, gs );
  return sizeof(GSTATE) + gsc_colorInfoSize();
}

/** File runtime initialisation */
void init_C_globals_gstate(void)
{
  gs_locked = FALSE;
  lgframes = NULL ;
  ggframes = NULL ;
}

/*
Log stripped */
