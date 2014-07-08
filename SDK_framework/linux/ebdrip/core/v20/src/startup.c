/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:startup.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * RIP startup handling.
 */

#include "core.h"
#include "coreinit.h"
#include "swstart.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swcopyf.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "monitor.h"
#include "fileio.h"
#include "rsd.h"
#include "procfilt.h"
#include "fonth.h"
#include "fontcache.h"
#include "namedef_.h"
#include "pclGstate.h"
#include "hqmemset.h"
#include "pscontext.h"
#include "statops.h"

#include "execops.h"
#include "bitblts.h"
#include "matrix.h"
#include "params.h"
#include "psvm.h"
#include "saves.h"
#include "ndisplay.h"
#include "display.h"
#include "graphics.h"
#include "gu_fills.h"
#include "gu_chan.h"
#include "routedev.h"
#include "asyncps.h"   /* init_async_memory */
#include "gstate.h"
#include "stacks.h"
#include "system.h"
#include "halftone.h"
#include "gofiles.h"
#include "fileops.h"
#include "gstack.h"
#include "scanner.h"
#include "fontops.h"
#include "devops.h"
#include "showops.h"
#include "randops.h"
#include "stackops.h"
#include "fcache.h"
#include "dictops.h"
#include "miscops.h"
#include "render.h" /* init_render_c */
#include "renderom.h" /* init_sepomit_debug */
#include "shadex.h" /* gouraud_init */
#include "bandtable.h" /* bandtable_init */
#include "mlock.h"
#include "control.h"
#include "swmemory.h"
#include "pathcons.h"
#include "pdfcntxt.h"
#include "upath.h"
#include "upcache.h"
#include "idiom.h"
#include "pathops.h"
#include "gu_path.h"
#include "clippath.h" /* init_clippath_debug */
#include "clipops.h"  /* init_clip_debug */
#include "groupPrivate.h" /* init_backdroprender_debug */
#include "vndetect.h" /* init_vignette_detection_debug */
#include "shading.h"

#include "startup.h"
#include "gschead.h"
#include "imstore.h"

#include "gs_color.h"
#include "trap.h"
#include "std_file.h"
#include "encoding.h"
#include "dicthash.h"

#include "tranState.h"
#include "cidfont.h"
#include "rcbcntrl.h" /* rcbn_init */
#include "toneblt.h"

#include "zipdev.h"
#include "patternshape.h"
#include "jobmetrics.h"

static void doReboot(void);
static Bool doBootup(void);

static Bool ps_context_swstart(SWSTART * start)
{
  corecontext_t *context = get_core_context() ;
  ps_context_t *pscontext ;
  FILELIST *flptr, *filters ;

  UNUSED_PARAM(SWSTART *, start) ;

  /* Create the base PostScript context and initialise it before doing
     anything else. */
  HQASSERT(context->pscontext == NULL, "PostScript context already exists") ;
  if ( (pscontext = mm_alloc_static(sizeof(ps_context_t))) == NULL )
    return FALSE ;

  HqMemZero(pscontext, sizeof(ps_context_t)) ;

  /* Make finding thread context easy from PS context */
  context->pscontext = pscontext ;
  pscontext->corecontext = context ;

  /** \todo ajcd 2009-01-14: Decide what goes into the PS context. */

  /* Initialise standard PostScript-specific filters. Standard filters and
     PDF/PDFOut filters are initialised through their own init routines. */
#define N_PS_FILTERS 2 /* PS-specific filters */
  filters = flptr = mm_alloc_static(N_PS_FILTERS * sizeof(FILELIST)) ;
  if ( filters == NULL )
    return FALSE ;

  procedure_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;

  procedure_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;

  HQASSERT( flptr - filters == N_PS_FILTERS ,
            "didn't allocate correct amount of memory for filters" ) ;

  return TRUE ;
}

static void ps_context_finish(void)
{
  CoreContext.pscontext = NULL ;
}

void ps_context_C_globals(core_init_fns *fns)
{
  fns->swstart = ps_context_swstart ;
  fns->finish = ps_context_finish ;
}

/** dostart -- initialize the RIP. */
Bool dostart( void )
{
  /** \todo ajcd 2009-11-20: What should remain here? Probably only
      initialisations that are specific to the state of a PostScript
      interpreter. All of the PostScript interpreter specific state should
      move into get_core_context()->pscontext.

      None of these functions should create new memory pools, or create
      new GC roots (these operations require tear-down, so should be part of
      a proper core_init_t table sequence).

      Long term, if we want to spawn multiple interpreters, the
      initialisations will have to avoid using a global core_init_t table.
      Each interpreter instance will need its own PS context, and all of
      the PS state will have to belong to that context.
  */
#if defined( DEBUG_BUILD )
  init_vignette_detection_debug() ; /* Vignette detection debug */
  init_shading_debug() ;            /* Smooth shading debug */
  init_clippath_debug();            /* Clippath debug */
  init_backdroprender_debug();      /* Backdrop rendering debug */
  init_render_debug();              /* Rendering debug */
  init_clip_debug();                /* Clip focus */
  init_stroke_debug();              /* Stroke debug */
  init_sepomit_debug();             /* Separation omission */
  init_patternshape_debug() ;       /* Pattern shapes debug/trace */
  init_polycache_debug() ;          /* Polygon caching debug/trace */
#endif

  /* Initial values of realtime and usertime. */
  initUserRealTime() ;

  /* Set various system params based on the amount of memory available. */
  setDynamicGlobalDefaults() ;

  /* Prepare the bottom of the execution stack. */
  doReboot() ;

  /* Run all of the files from the %boot% device. */
  if ( !doBootup() )
    return FALSE ;

  /* Re-allocate async memory according to systemparam. The allocation can be
     reduced as necessary, so there is no failure case. */
  init_async_memory(get_core_context()->systemparams) ;

  return TRUE ;
}


/** dostop - finish the RIP (the counterpart of dostart). */
void dostop(void)
{
}

/* ----------------------------------------------------------------------------
   function:            quit_()            author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page ??.

---------------------------------------------------------------------------- */
Bool quit_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  int32 i ;
  uint8 *s ;
  OBJECT *theo ;
  OBJECT tempo = OBJECT_NOTVM_NOTHING ;
  Bool exit_rip;

  deactivate_pagedevice_t dpd_res ;

  /* Allow a special really, really quit option if the name superstop is in the
   * $printerdict dictionary with the value superstop.
   */
  exit_rip = FALSE;
  theo = fast_extract_hash_name(&systemdict, NAME_DollarPrinterdict);
  HQASSERT(theo != NULL && oType(*theo) == ODICTIONARY,
           "$printerdict not a dictionary");
  if (theo != NULL && oType(*theo) == ODICTIONARY) {
    theo = fast_extract_hash_name(theo, NAME_superstop);
    if (theo != NULL && oType(*theo) == ONAME) {
      exit_rip = (oName(*theo) == system_names + NAME_superstop);
    }
  }

  /* Only allow if at savelevel 0 or forced RIP exit */
  if ( (NUMBERSAVES(context->savelevel) > 1) && !exit_rip ) {
    return(error_handler(INVALIDACCESS));
  }

  /* Initial clear of the execution stack; to handle recursive interpreter calls. */
  for ( i = theStackSize( executionstack ) ; i >= 1 ; --i ) {
    theo = theTop( executionstack ) ;
    if ( oType( *theo ) == ONULL )
      switch ( theLen(* theo )) {
      case ISINTERPRETEREXIT :
        oInteger (*theo) = NAME_quit;
        execStackSizeNotChanged = FALSE ;
        return TRUE;
      case ISPATHFORALL :
        free_forallpath( oOther( *theo )) ;
        break ;
    }
    pop( & executionstack ) ;
  }

  /* Recombine should know when quit is being called to clear its state and
     scrap any knowledege of the current page, otherwise recombine will try to
     force the page out on device deactivation. */
  if (rcbn_enabled())
    rcbn_quit();

  /* Deactivate the current page device; must do this after the first loop to
   * clear the execution stack since this may be inside multiple recursive
   * interpreter calls and we only want to do it once.
   */
  deactivate_pagedevice( gstateptr , NULL , NULL , & dpd_res ) ;
  if ( ! do_pagedevice_deactivate( & dpd_res ))
    return FALSE ;

  /* Clear the main three main stacks. */
  for ( i = theStackSize( executionstack ) ; i >= 1 ; --i ) {
    theo = theTop( executionstack ) ;
    if ( oType( *theo ) == ONULL )
      switch ( theLen(* theo )) {
      case ISINTERPRETEREXIT :
        HQFAIL( "Should not now be seeing any recursive interpreter calls." ) ;
        break ;
      case ISPATHFORALL :
        free_forallpath( oOther( *theo )) ;
        break ;
    }
    pop( & executionstack ) ;
  }
  ( void )clear_(pscontext) ;
  ( void )cleardictstack_(pscontext) ;

  if ( exit_rip ) {
    /* THIS IS A TEMPORARY SHUTDOWN MECHANISM UNTIL WE HAVE
       REFACTORED THE SKIN CODE IN ALL RIP PRODUCTS */
    if (swstart_must_return) {
      exiting_rip_cleanly = TRUE ;
    }

    if (! exiting_rip_cleanly) {
      (void)dispatch_SwExit(0, NULL);
    }
  }

  /* Otherwise drop through to do a reboot, but only if not exiting
     cleanly. */
  if (! exiting_rip_cleanly) {

    /* Reset necessary variables. */
    bandid = 0 ;

    initRandomNumberGenerator() ;
    error_clear_context(context->error) ;

    mflookpos = 0 ;

    ++context->page->eraseno ; /* Force a complete flush */
    font_caches_clear(context);
    purge_ucache(context);

    fid_count = 0 ;

    /* Pass non-NULL deactivate_pagedevice_t to purge_memory so that
       it doesn't do the deactivate. */
    if ( ! purge_memory( 0 , & dpd_res ))
      return dispatch_SwExit(swexit_error_startup_01,
                             "The Start Up Routine\n") ;

#if 0
    /* clear stacks again */
    ( void )clear_(pscontext) ;
    npop (theStackSize( executionstack ), & executionstack);
#endif

    while ( thecharpaths )
      free_charpaths() ;

    doReboot() ;

    /* Reopen standard PS files */
    init_std_files_table() ;

    /* Read in "reboot string". */
    s = ( uint8 * )"{ a4 save pop serverdict begin /firstjobdone (0) def \
end $error /initializing false put start } exec" ;

    theTags( tempo ) = OSTRING | EXECUTABLE | READ_ONLY ;
    theLen( tempo ) = (uint16)strlen(( char * )s ) ;
    oString( tempo ) = s ;

    execStackSizeNotChanged = FALSE ;
    if ( !push(&tempo, &executionstack) )
      HQFAIL("quit_ execution stack full") ;

    SwReboot() ; /* tell the oem/gui component the interpreter is rebooting */
  } /* end if (! exiting_rip_cleanly) */

  return TRUE ;
}

static void doReboot(void)
{
  OBJECT tempo = OBJECT_NOTVM_NOTHING ;
/*
  Set up a basic quit operator on the bottom of the exec stack - so can't run
  out of objects to execute; and also a mark for 'stopped'
*/
  object_store_operator(&tempo, NAME_quit) ;
  if ( !push(&tempo, &executionstack) )
    HQFAIL("Reboot exec stack full") ;

  theTags( tempo ) = OMARK | EXECUTABLE ;
  if ( !push(&tempo, &executionstack) )
    HQFAIL("Reboot exec stack full") ;
}

static Bool doBootup(void)
{
  OBJECT tempo = OBJECT_NOTVM_NOTHING , reado = OBJECT_NOTVM_NOTHING ;
  int32 length;

  OBJECT * theo;
  int32 i;

/* Read in "bootup strings. */
  object_store_string(&tempo,
                      STRING_AND_LENGTH("{ a4 save pop { start } stopped { handleerror } if } exec"),
                      EXECUTABLE) ;

  if ( !push(&tempo, &executionstack) )
    return FALSE ;

  /* Reset tags for use by get_startup_file() */
  theTags( tempo ) = OSTRING | READ_ONLY | LITERAL ;

  object_store_string(&reado, STRING_AND_LENGTH("r&"), LITERAL) ;

  /* Setup for systemdict init by first file */
  if ( !push(&systemdict, &operandstack) )
    return FALSE ;

  /* now run each of the startup files in turn; we can only have one boot
     file open at once, so run each before opening the next */
  i = 0;
  for (;;) {
    /* (name) (r) file
       Note that the run operator doesn't exist yet so we have to do
       it this way rather than just push the run operator to do it for us */
    get_startup_file(i, &oString(tempo), & length);
    if (! oString(tempo))
      break;
    theLen(tempo) = (uint16) length;
    if ( !push2(&tempo, &reado, &operandstack) ||
         !file_(get_core_context_interp()->pscontext) ) {
      return FALSE ;
    }
    i++;
    HQASSERT(!isEmpty(operandstack), "No bootstrap file on stack") ;
    theo = theTop( operandstack ); /* the file */
    if ( !push(theo, &executionstack) )
      return FALSE ;
    pop(& operandstack);
    if (! interpreter(1, NULL)) {
      /** \todo ajcd 2009-11-28: Augment error info:
          "PostScript error while initializing missing or corrupt SWROOT / SWPATH?"
      */
      return FALSE ;
    }
  }
  /* now disable the boot device */
  object_store_string(&tempo,
                      STRING_AND_LENGTH("(%boot%) << /Password 0 /Enable false>> setdevparams"),
                      EXECUTABLE) ;

  execStackSizeNotChanged = FALSE ;
  if ( !push(&tempo, &executionstack) ||
       !interpreter(1, NULL) ) {
      /** \todo ajcd 2009-11-28: Augment error info */
    return FALSE ;
  }

  return TRUE ;
}

/** \todo ajcd 2009-11-23: These initialisations are ones which I haven't
    decided where to put yet. */
IMPORT_INIT_C_GLOBALS( cmpprog )
IMPORT_INIT_C_GLOBALS( filename )
IMPORT_INIT_C_GLOBALS( raster )
IMPORT_INIT_C_GLOBALS( render )
IMPORT_INIT_C_GLOBALS( ripdebug )
IMPORT_INIT_C_GLOBALS( security )
IMPORT_INIT_C_GLOBALS( spdetect )
IMPORT_INIT_C_GLOBALS( vndetect )
IMPORT_INIT_C_GLOBALS( polycache )

/** Compound runtime initialisation */
void v20_C_globals(core_init_fns *fns)
{
  UNUSED_PARAM(core_init_fns *, fns) ;
  init_C_globals_cmpprog() ;
  init_C_globals_filename() ;
  init_C_globals_ripdebug() ;
  init_C_globals_security() ;
  init_C_globals_spdetect() ;
  init_C_globals_vndetect() ;
  init_C_globals_polycache();
}

/*
Log stripped */
