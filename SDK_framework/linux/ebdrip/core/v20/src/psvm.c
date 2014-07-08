/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:psvm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript Memory save/restore routines.
 */

#include "core.h"
#include "psvm.h"
#include "coreinit.h"

#include "swerrors.h"
#include "monitor.h"
#include "objects.h"
#include "fileio.h"
#include "hqmemcpy.h"
#include "mmcompat.h"
#include "fontcache.h"
#include "pscontext.h"

#include "std_file.h"
#include "params.h"
#include "stacks.h"
#include "graphics.h"
#include "gstack.h"
#include "lanlevel.h"
#include "vndetect.h"
#include "swmemory.h"
#include "devops.h"
#include "fontops.h"
#include "utils.h"
#include "mps.h"
#include "gcscan.h"
#include "saves.h"
#include "chartype.h" /* remap_bin_token_chars */
#include "control.h" /* ps_interpreter_level */
#include "render.h" /* outputpage */
#include "display.h" /* dl_mem_used */

#include <limits.h> /* CHAR_BIT */

PS_SAVEINFO *workingsave = NULL ;

static void init_C_globals_psvm(void)
{
  workingsave = NULL ;
}

static Bool ps_vm_swstart(struct SWSTART *params)
{
  SYSTEMPARAMS *systemparams = get_core_context()->systemparams;

  UNUSED_PARAM(struct SWSTART *, params) ;

  /** \todo ajcd 2009-11-26: This should be in the PostScript context. */
  if ( (workingsave = mm_alloc_static(sizeof(PS_SAVEINFO))) == NULL )
    return FALSE ;

  theIOMemory     ( workingsave ) = NULL ;      /* For safety. */
  theIPacking     ( workingsave ) = FALSE ;

  HQASSERT(std_files != NULL, "Standard files not initialised") ;
  theIStdin       ( workingsave ) = ( & std_files[ STDIN  ]) ;
  theIStdout      ( workingsave ) = ( & std_files[ STDOUT ]) ;
  theIStderr      ( workingsave ) = ( & std_files[ STDERR ]) ;

  theISaveStdinFilterId  ( workingsave ) = 0 ; /* invalid value for a filter id */
  theISaveStdoutFilterId ( workingsave ) = 0 ; /* invalid value for a filter id */
  theISaveStderrFilterId ( workingsave ) = 0 ; /* invalid value for a filter id */

  theILastIdiomChange( workingsave )   = 0 ;

  theIGSid        ( workingsave ) = 0 ;        /* For safety. */
#ifdef highbytefirst
  theIObjectFormat( workingsave ) = 1 ;
#else
  theIObjectFormat( workingsave ) = 2 ;
#endif
  theISaveLangLevel ( workingsave ) = systemparams->LanguageLevel ;
  theISaveColorExt  ( workingsave ) = systemparams->ColorExtension ;
  theISaveCompFonts ( workingsave ) = systemparams->CompositeFonts ;
  theICMYKDetect  ( workingsave ) = FALSE ;
  theICMYKDetected( workingsave ) = FALSE ;

  remap_bin_token_chars() ;

  return TRUE ;
}

void ps_vm_C_globals(core_init_fns *fns)
{
  init_C_globals_psvm() ;
  fns->swstart = ps_vm_swstart ;
}

/* ----------------------------------------------------------------------------
   function:            save_()            author:              Andrew Cave
   creation date:       03-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 208.

---------------------------------------------------------------------------- */
Bool save_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  register SAVELIST *sptr ;
  Bool result ;
  OBJECT saveo = OBJECT_NOTVM_NOTHING ;

  if ( (sptr = save_prepare(context->savelevel)) == NULL )
    return FALSE ;

/* Create a save object representing this, and push it on the operand stack. */
  theTags(saveo) = OSAVE | LITERAL ;
  oSave(saveo) = sptr ;
  if ( ! push(&saveo, &operandstack) )
    return FALSE ;

  /* The gsave needs the new save level, while other stuff below requires the old
   * level. A bit hacky, but the simplest way of doing it.
   */
  context->savelevel += SAVELEVELINC ;
  result = gs_gpush( GST_SAVE );
  context->savelevel -= SAVELEVELINC ;
  if (!result) {
    pop( & operandstack ) ;
    return FALSE ;
  }

  save_commit(sptr) ;

  mm_ps_save(NUMBERSAVES(context->savelevel));

  return setlanguagelevel(context);
}

/*------------------------- SAVE/RESTORE CALLBACKS --------------------------*/

/* Save workingsave info in pointer provided */
void ps_save_commit(PS_SAVEINFO *sptr)
{
  HQASSERT(sptr, "Nowhere to save PostScript info") ;
  HQASSERT(workingsave, "No PostScript info to save") ;

/* Add to new savelist. */
  theIOMemory( sptr )        = NULL ;
  theIPacking( sptr )        = theIPacking( workingsave ) ;
  theIObjectFormat( sptr )   = theIObjectFormat( workingsave ) ;

  theIStdin ( sptr )         = theIStdin ( workingsave ) ;
  theIStdout( sptr )         = theIStdout( workingsave ) ;
  theIStderr( sptr )         = theIStderr( workingsave ) ;
  theISaveStdinFilterId( sptr )  = theISaveStdinFilterId( workingsave ) ;
  theISaveStdoutFilterId( sptr ) = theISaveStdoutFilterId( workingsave ) ;
  theISaveStderrFilterId( sptr ) = theISaveStderrFilterId( workingsave ) ;

  theILastIdiomChange( sptr ) = theILastIdiomChange( workingsave ) ;

  theIGSid( sptr )           = gstackptr->gId ;
  theISaveLangLevel( sptr )  = theISaveLangLevel( workingsave ) ;
  theISaveColorExt ( sptr )  = theISaveColorExt( workingsave ) ;
  theISaveCompFonts( sptr )  = theISaveCompFonts( workingsave ) ;

  theICMYKDetect  ( sptr )   = theICMYKDetect  ( workingsave ) ;
  theICMYKDetected( sptr )   = theICMYKDetected( workingsave ) ;
}

/* Restore workingsave info from pointer provided */
void ps_restore_commit(PS_SAVEINFO *sptr)
{
  HQASSERT(workingsave, "Nowhere to restore PostScript info") ;
  HQASSERT(sptr, "No PostScript info to restore") ;

  theIPacking( workingsave )      = theIPacking( sptr ) ;
  if ( theIObjectFormat( workingsave ) != theIObjectFormat( sptr )) {
    /* chartypes array must be remapped */
    theIObjectFormat( workingsave ) = theIObjectFormat( sptr ) ;
    remap_bin_token_chars() ;
  }

  theIStdin ( workingsave ) = theIStdin ( sptr ) ;
  theIStdout( workingsave ) = theIStdout( sptr ) ;
  theIStderr( workingsave ) = theIStderr( sptr ) ;

  theISaveStdinFilterId( workingsave )  = theISaveStdinFilterId( sptr ) ;
  theISaveStdoutFilterId( workingsave ) = theISaveStdoutFilterId( sptr ) ;
  theISaveStderrFilterId( workingsave ) = theISaveStderrFilterId( sptr ) ;

  theILastIdiomChange( workingsave ) = theILastIdiomChange( sptr ) ;

  theISaveLangLevel(workingsave) = theISaveLangLevel(sptr);
  theISaveColorExt(workingsave) = theISaveColorExt(sptr);
  theISaveCompFonts(workingsave) = theISaveCompFonts(sptr);

  theICMYKDetect  ( workingsave ) = theICMYKDetect  ( sptr ) ;
  theICMYKDetected( workingsave ) = theICMYKDetected( sptr ) ;
}


/* This should match ps_scan_saveinfo. */

Bool ps_restore_prepare(PS_SAVEINFO *sptr, int32 slevel)
{
  OBJECT *o2 ;

  /* Reset the V.M. */
  o2 = theIOMemory( sptr ) ;
  while ( o2 ) {
    OBJECT *theo = o2 + 1 ;

    switch ( theTags(*theo)) {
    case OGSTATE:
      {
        /* Copy back GSTATE OBJECT - freeing previous copy. */
        GSTATE *gs = (GSTATE *)(theo + 1) ;

        if ( gs->slevel != ( int32 )( SAVEMASK + 1 )) {
          GSTATE *gs_check = oGState(*theo) ;

          /* Remove anything already in the destination gstate */
          gs_discardgstate( gs_check ) ;

          if ( ! gs_copygstate( gs_check, gs, GST_GSAVE, NULL )) {
            HQFAIL("Failed to restore gstate");
            return error_handler( UNREGISTERED ) ; /* Probably fatal error */
          }
          gs_discardgstate( gs ) ;

          /* Need to save/restore the current slevel,
           * but type, id and next remain unchanged
           */
          gs_check->slevel = gs->slevel ;

          /* Mark as having restored, so we don't do it again. */
          gs->slevel = SAVEMASK + 1 ;
        }
      }
      break ;

    case ODICTIONARY: {
      OBJECT *dict = oDict(*theo);
      OBJECT *currext = oType(dict[-1]) != ONOTHING ? oDict(dict[-1]) : NULL;
      uint16 savedlen = theLen( *theo );
      DPAIR *loop, *limit, *vmptr;

      DICT_ALLOC_LEN(dict) = savedlen; /* restore size */
      dict[-1] = theo[1]; /* copy back extension link */
      dict[0]  = theo[2]; /* copy back control data */
      if ( savedlen == 0 && oType(dict[-1]) != ONOTHING ) {
        /* restored an extension, just update dictvals */
        OBJECT *ext = oDict(dict[-1]);
        for ( loop = (DPAIR *)(ext + 1), limit = loop + DICT_ALLOC_LEN(ext);
              loop < limit ; ++loop )
          if ( oType(loop->key) == ONAME )
            ncache_restore_prepare(oName(loop->key), &loop->obj,
                                   dict, currext, slevel);
      } else /* Restored the array, copy back entries & update dictvals. */
        for ( vmptr = (DPAIR *)(theo + 3),
                loop = (DPAIR *)(dict + 1), limit = loop + savedlen ;
              loop < limit ; ++loop, ++vmptr ) {
          HQASSERT(!NOTVMOBJECT(vmptr->key), "Restoring non-PSVM object");
          HQASSERT(!NOTVMOBJECT(vmptr->obj), "Restoring non-PSVM object");
          HQASSERT(!NOTVMOBJECT(loop->key), "Restoring to non-PSVM slot");
          HQASSERT(!NOTVMOBJECT(loop->obj), "Restoring to non-PSVM slot");
          *loop = *vmptr; /* Struct copy to restore slot properties. */
          if ( oType(loop->key) == ONAME && currext != NULL )
            ncache_restore_prepare(oName(loop->key), &loop->obj,
                                   dict, currext, slevel);
        }

      } break;

    case OARRAY: {
      OBJECT *loop = oArray( *theo );
      OBJECT *limit = loop + theLen( *theo );
      OBJECT *vmptr;

      /* Copy back memory. */
      for ( vmptr = theo + 1 ; loop < limit ; ) {
        HQASSERT(!NOTVMOBJECT(*vmptr), "Restoring non PSVM object") ;
        HQASSERT(!NOTVMOBJECT(*loop), "Restoring to non PSVM slot") ;
        *loop++ = *vmptr++ ; /* Struct copy to restore slot properties. */
      }
    } break;

    case ONULL: /* Ignore this; it means a failed allocation attempt */
      break ;

    default:
      HQFAIL("Unknown saved type in save memory") ;
    }

    /* Take next object to be restored. */
    theo = oArray( *o2 ) ;
    o2 = theo ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */

Bool ps_restore(ps_context_t *pscontext, int32 slevel)
{
  corecontext_t *context = pscontext->corecontext ;
  deactivate_pagedevice_t dpd ;

  if ( slevel >= context->savelevel )
    return error_handler( INVALIDRESTORE ) ;

  if ( slevel == 2 )
    context->page->force_deactivate = FALSE;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if ( ! purge_memory( slevel , & dpd ))
    return FALSE ;

  return do_pagedevice_reactivate(pscontext, &dpd) ;
}

/* ----------------------------------------------------------------------------
   function:            restore_()         author:              Andrew Cave
   creation date:       16-Mar-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 203.

---------------------------------------------------------------------------- */
Bool restore_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  OBJECT tempo = OBJECT_NOTVM_NOTHING ;
  int32 slevel ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OSAVE )
    return error_handler( TYPECHECK ) ;

  Copy( & tempo , theo ) ;
  pop( & operandstack ) ;

  slevel = save_level(oSave(tempo)) ;

  if ( ! ps_restore(pscontext, slevel) ) {
    ( void ) push( & tempo , & operandstack ) ;
    return FALSE ;
  }

  return TRUE ;
}


/* ps_scan_saveinfo - scan the saved objects in a saveinfo
 *
 * This should match ps_restore_prepare.
 */
mps_res_t MPS_CALL ps_scan_saveinfo(mps_ss_t ss, PS_SAVEINFO *sptr)
{
  OBJECT *o2 ;
  mps_res_t res ;
  size_t dummy;

  o2 = theIOMemory( sptr ) ;
  MPS_SCAN_BEGIN( ss )
    while ( o2 ) { /* map over list of saved objects */
      /* This has to fix the data block allocated for the save data.
       * Because it's on a string segment, the object has to be explicitly
       * scanned as well.  Since fixing needs to know the length (limit),
       * and one needs to determine the type to calculate it, scan while
       * calculating, before fixing. */
      OBJECT *theo = o2 + 1 ;
      OBJECT *tvalue = theo + 1;
      OBJECT *limit = tvalue + theLen(*theo);

      /* Must mark the object on the old savelevel, or it could be
       * reclaimed, and then restore would fail. */
      MPS_SCAN_CALL( res = ps_scan_field( ss, theo ));
      if ( res != MPS_RES_OK ) return res ;

      switch ( theTags(*theo)) {
      case OGSTATE: {
        GSTATE *gs = (GSTATE *)(void *)(theo + 1) ;

        limit = (OBJECT *)( gs + 1 ) ;
        MPS_SCAN_CALL( res = gs_scan( &dummy, ss, gs )) ;
      } break;

      case ODICTIONARY: {
        OBJECT *dict = oDict(*theo);

        /* Marking the old object above is not enough, because it may
           have been truncated by extend_dict(). */
        if ( DICT_ALLOC_LEN(dict) != theLen( *theo ) )
          PS_MARK_BLOCK( ss, dict + 1, theLen( *theo ) * sizeof(DPAIR) );
        limit += ( theILen( theo ) + 2 ) ;
        MPS_SCAN_CALL( res = ps_scan( ss, tvalue, limit ));
      } /* Fall through */
      case OARRAY:
        MPS_SCAN_CALL( res = ps_scan( ss, tvalue, limit )) ;
        break ;

      case ONULL: /* Ignore this; it means a failed allocation attempt */
        res = MPS_RES_OK ;
        break;

      default:
        HQFAIL("Unknown saved type in save memory") ;
        res = MPS_RES_OK ; /* shut up the compiler */
      }
      if ( res != MPS_RES_OK ) return res ;
      PS_MARK_BLOCK( ss, o2, ADDR_OFFSET ( o2, limit )) ;
      /* Take next object. */
      o2 = oArray( *o2 ) ;
    }

    /* Scan stdio filelists since may not be referenced from object in PS memory */
    MPS_RETAIN(&sptr->mystdin, TRUE);
    MPS_RETAIN(&sptr->mystdout, TRUE);
    MPS_RETAIN(&sptr->mystderr, TRUE);

  MPS_SCAN_END( ss ) ;
  return MPS_RES_OK ;
}


/* PS operator to emit a string into the MM log to label this moment */

Bool labelmomentinlog_(ps_context_t *pscontext)
{
  register OBJECT *theo ;
  mps_word_t label;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW );
  theo = theTop( operandstack );
  if ( oType( *theo ) != OSTRING )
    return error_handler( TYPECHECK );

  label = mps_telemetry_intern_length( (char*)oString( *theo ), theLen( *theo ));
  mps_telemetry_label( 0, label );

  pop( & operandstack );
  return TRUE;
}

#define MAXPSINTEGER \
  (~(1ul << (CHAR_BIT * sizeof(oInteger(inewobj)) - 1)))


Bool vmstatus_(ps_context_t *pscontext)
{
  int32 slevel ;
  size_t mused , mtotal ;
  corecontext_t *context = ps_core_context(pscontext) ;

  /* First, memory supplied to the (default) VM pool */
  mtotal = mm_pool_size(mm_pool_ps) + mm_pool_size(mm_pool_ps_typed);
  mused  = mtotal - mm_pool_free_size(mm_pool_ps)
           - mm_pool_free_size(mm_pool_ps_typed);

  /* It is tempting to take the size of the temp pool as available,
   * BUT there are also, at least, open file buffers in there that cannot be
   * freed by paint-to-disc or cache flushing, so adding it up piecemeal is
   * best.  -Huge.
   */
  mtotal += mm_pool_free_size(mm_pool_temp);
  mtotal += fontcache_available_memory();
  mtotal += mm_no_pool_size(TRUE);
  mtotal += dl_mem_used(context->page);

  slevel = NUMBERSAVES(context->savelevel) - 1 ;

  if ( mtotal > MAXPSINTEGER ) mtotal = (size_t)MAXPSINTEGER;
  if ( mused > MAXPSINTEGER ) mused = (size_t)MAXPSINTEGER;

  return (stack_push_integer( slevel, &operandstack ) &&
          stack_push_integer( (int32)mused, &operandstack ) &&
          stack_push_integer( (int32)mtotal, &operandstack )) ;
}


/* ----------------------------------------------------------------------------
   function:            setglobal_()
   creation date:       08-Jul-1991
   arguments:           none .
   description:

   Sets global/local VM allocation mode.

   Note: when setting global mode, must temporarily rebind FontDirectory.

---------------------------------------------------------------------------- */
Bool setglobal_(ps_context_t *pscontext)
{
  Bool global ;

  if ( ! get1B( &global ) )
    return FALSE ;

  setglallocmode(ps_core_context(pscontext), global ) ;

  return TRUE ;
}


Bool setglallocmode(corecontext_t *context, Bool glmode )
{
  Bool prev_glmode = context->glallocmode;

  HQASSERT(prev_glmode != -1, "setting glmode in non-intepreter thread");

  if (prev_glmode != glmode) {
    /* Swap over local/global VM page allocation mode here. */
    if ( glmode ) {
      HQASSERT(mm_pool_ps_global != NULL && mm_pool_ps_typed_global != NULL,
               "Global PSVM pool not initialised") ;
      mm_pool_ps = mm_pool_ps_global ;
      mm_pool_ps_typed = mm_pool_ps_typed_global ;
      if ( fontdirptr ) {
        HQASSERT(oType(gfontdirobj) == ODICTIONARY,
                 "Global font dictionary not initialised") ;
        Copy( lfontdirptr , &gfontdirobj ) ;
        fontdirptr = gfontdirptr ;
      }
    }
    else {
      HQASSERT(mm_pool_ps_local != NULL && mm_pool_ps_typed_local != NULL,
               "Local PSVM pool not initialised") ;
      mm_pool_ps = mm_pool_ps_local ;
      mm_pool_ps_typed = mm_pool_ps_typed_local ;
      if ( fontdirptr ) {
        HQASSERT(oType(gfontdirobj) == ODICTIONARY,
                 "Local font dictionary not initialised") ;
        Copy( lfontdirptr , &lfontdirobj ) ;
        fontdirptr = lfontdirptr ;
      }
    }
    context->glallocmode = glmode ;
  }

  return (prev_glmode);
}


Bool currglobal_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  return push(context->glallocmode ? &tnewobj : &fnewobj, &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            gcheck_()
   creation date:       08-Jul-1991
   arguments:           none .
   description:

   Checks if an OBJECT is simple or composite.

---------------------------------------------------------------------------- */
Bool gcheck_(ps_context_t *pscontext)
{
  register OBJECT *reso ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;

  reso = ( & tnewobj ) ;
  if ( isPSCompObj(*theo) && !oGlobalValue(*theo) )
    reso = ( & fnewobj ) ;

  Copy( theo , reso ) ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            vmreclaim_()       author:              Andrew Cave
   creation date:       03-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript Level II reference manual page 546.

---------------------------------------------------------------------------- */
Bool vmreclaim_(ps_context_t *pscontext)
{
  int32 tmp ;

  if ( ! stack_get_integers(&operandstack, & tmp , 1) )
    return FALSE ;

  if (( tmp < -2 ) || ( tmp > 2 ))
    return error_handler( RANGECHECK ) ;

  if ( tmp > 0 ) {
    if ( ps_interpreter_level == 1 ) {
      if ( ! garbage_collect( TRUE, tmp == 2 ) )
        return FALSE ;
    } /* else
       * should defer GC until top level, but avoiding partial paint is
       * complicated, and dosomeaction is too much of a mess. */
  } else
    ps_core_context(pscontext)->userparams->VMReclaim = tmp ;

  pop( & operandstack ) ;

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            setvmthreshold_()  author:              Andrew Cave
   creation date:       03-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript Level II reference manual page 546.

---------------------------------------------------------------------------- */
Bool setvmthreshold_(ps_context_t *pscontext)
{
  int32 tmp ;
  double limit;
  corecontext_t *context = ps_core_context(pscontext) ;

  if ( ! stack_get_integers(&operandstack, & tmp , 1 ))
    return FALSE ;
  if ( tmp < -1 )
    return error_handler( RANGECHECK ) ;

  limit = mm_set_gc_threshold( (double)tmp, &dosomeaction );
  context->userparams->VMThreshold = limit > MAXINT32 ? MAXINT32 : (int32)limit;

  pop( & operandstack ) ;
  return TRUE ;
}


Bool releasereservememory_(ps_context_t *pscontext)
{
  mm_set_allocation_cost(ps_core_context(pscontext)->mm_context,
                         mm_cost_all);
  return TRUE;
}


Bool resetreservememory_(ps_context_t *pscontext)
{
  mm_set_allocation_cost(ps_core_context(pscontext)->mm_context,
                         mm_cost_normal);
  return TRUE;
}


/*
Log stripped */
