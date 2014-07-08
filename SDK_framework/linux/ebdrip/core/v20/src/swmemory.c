/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:swmemory.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Low-level PostScript memory routines.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "mpscepvm.h"
#include "gcscan.h"
#include "fileio.h"
#include "fontcache.h"
#include "procfilt.h"
#include "swpdf.h"

#include "display.h"
#include "matrix.h"
#include "graphics.h"
#include "gs_cache.h"     /* gsc_restore */
#include "dlstate.h"
#include "stacks.h"

#include "ndisplay.h"
#include "routedev.h"
#include "params.h"
#include "psvm.h" /* ps_restore() */
#include "control.h"
#include "dicthash.h" /* insert_hash */

#include "fontops.h"
#include "gstack.h"
#include "gstate.h"
#include "devops.h"
#include "lanlevel.h"
#include "chartype.h"
#include "fcache.h"
#include "fileops.h"
#include "swmemory.h"
#include "vndetect.h"
#include "cidfont.h"
#include "idiom.h"
#include "spdetect.h"
#include "saves.h"
#include "rcbcntrl.h"
#include "imagecontext.h"
#include "gu_chan.h"

#include "xps.h"

#ifdef DEBUG_BUILD
extern int32 mm_trace_close( void ); /* For lack of a header file for it. */
#endif


static void  clearMakeScaleFontCache( int32 slevel ) ;
static Bool checkValidRestoreStack(STACK *thestack, int32 ignore, int32 slevel) ;
static Bool deactivatePageDeviceForRestore( int32 gsid , deactivate_pagedevice_t *dpd ) ;

/* ----------------------------------------------------------------------------
   function:            purge_memory(..)   author:              Andrew Cave
   creation date:       16-Mar-1987        last modification:   ##-###-####
   arguments:           slevel .
   description:

   Clears all save objects down to the corresponding save level.
   Other side effects are :
     All files are closed whose sid numbers are greater than sid.
     All names from the cache whose sid numbers are greater than sid
        are removed.
     The graphics stack is popped down to the corresponding save level,
        and reset.

---------------------------------------------------------------------------- */
Bool purge_memory( int32 slevel , deactivate_pagedevice_t *dpd )
{
  int32 gid ;
  corecontext_t *context = get_core_context_interp() ;

  HQASSERT(slevel < context->savelevel,
           "Can't restore to save lower than current level") ;
  HQASSERT(dpd, "No pagedevice deactivation context") ;

  /* Check operand for stack existence of invalid objects. */
  if ( ! checkValidRestoreStack( & operandstack   , 0 , slevel ))
    return FALSE ;
  /* Check execution stack for existence of invalid objects. */
  if ( ! checkValidRestoreStack( & executionstack , 0 , slevel ))
    return FALSE ;
  /* Check dictionary stack for existence of invalid objects. */
  if ( ! checkValidRestoreStack( & dictstack , 2 + ( theISaveLangLevel( workingsave ) == 1 ? 0 : 1 ) , slevel ))
    return FALSE ;
  /* Check temporary stack for existence of invalid objects. */
  if ( ! checkValidRestoreStack( & temporarystack , 0 , slevel ))
    return FALSE ;

  gid = theIGSid(saved_ps_info(slevel));

  /* MUST deactive the pagedevice BEFORE restoring memory!!! */
  if ( ! deactivatePageDeviceForRestore( gid , dpd ))
    return FALSE ;

  /* Invalidate color chains and purge the ChainCache and ICC profile cache.
   * Must be done before freeGStatesList() to prevent chains being put back into
   * the color ChainCache for use in a lower save level with potentially
   * dangling pointers to freed memory.
   * Must be done before pdf_purge_execution_contexts() because pdf objects
   * may be dereferenced in the ChainCache. */
  if ( ! gsc_restore(slevel))
    return FALSE ;

  /* Free the corresponding original profiles held in the XPS cache */
  xps_icc_cache_purge( slevel );

  /* Free the pdf object pools and execution contexts.
     Must be prior to fileio_restore as PDF Out may need to create
     a filter to output fonts just before the restore */
  if ( ! pdf_purge_execution_contexts( slevel ))
    return FALSE ;

  /* Close any image contexts created after the save. */
  /* @@@ should not ignore return value but an early return not a good idea just
     because a close_file failed. */
  (void)imagecontext_restore(slevel);

  /* currentfile may be restored; clear cached pointer to it. */
  currfileCache = NULL ;

  /* Close any files that were created after the save. */
  fileio_restore(slevel) ;

  /* Deal with any procedure filters whose buffer may become invalid */
  if ( ! checkValidProcFilters(slevel) )
    return FALSE ;

  /* Must deal with the cache purges before purging the name cache. Callbacks
     also clear PostScript memory pointers. */
  if ( !restore_prepare(slevel) )
    return FALSE ;

  /* Wipe out the name cache entries for these save levels. */
  if ( NUMBERSAVES( slevel ) <= MAXGLOBALSAVELEVEL ) {
    /*
     * Before purging the name cache clear separation detection
     * name cache entries.
     */
    reset_separation_detection_on_restore();
    rcbn_term();

    /* Remove names from names cache */
    purge_ncache( slevel ) ;
  }

  /* Next to go is the idiom index. */
  idiom_purge( slevel ) ;

  freeGStatesList( slevel ) ;

  clearMakeScaleFontCache( slevel ) ;

  /* Change the actual save level */
  restore_commit(slevel) ;

  /* restore the MM PS state to the right save level */
  mm_ps_restore(NUMBERSAVES(slevel));

  /* May have altered allocation mode and parameters; act on new values */
  setglallocmode(context, context->glallocmode) ;

  operandstack.limit = context->userparams->MaxOpStack ;
  dictstack.limit = context->userparams->MaxDictStack ;
  executionstack.limit = context->userparams->MaxExecStack ;

  execStackSizeNotChanged = FALSE ;

  /* Language level changing happens on save/restore. On save, we can just
     set the workingsave values to the current systemparam values. On
     restore, we need to know the saved values; the save occurred before the
     language level change, so we swap the desired save values with the
     current systemparam values. setlanguagelevel uses these to optimise
     what needs changing. */
  {
    uint8 swap ;

    swap = theISaveLangLevel(workingsave) ;
    theISaveLangLevel(workingsave) = context->systemparams->LanguageLevel ;
    context->systemparams->LanguageLevel  = swap ;

    swap = theISaveColorExt(workingsave) ;
    theISaveColorExt(workingsave) = context->systemparams->ColorExtension ;
    context->systemparams->ColorExtension  = swap ;

    swap = theISaveCompFonts(workingsave) ;
    theISaveCompFonts(workingsave) = context->systemparams->CompositeFonts ;
    context->systemparams->CompositeFonts  = swap ;
  }

  /* go to different language level if required, and other variations */
  if ( !setlanguagelevel(context) )
    return FALSE ;

  /* Reset graphics state (must be last thing). */
  if (!gs_cleargstates( gid , GST_SAVE , dpd ))
    return FALSE;


#ifdef DEBUG_BUILD
  /* This code is useful for memory leak tracking. See the document "MM
     Debugging Tips" in SW information.  To use it, get in the debugger and
     set a breakpoint on the 'mm_trace_close()' line below.  Run until it
     breaks, step just the one line, and then go take a look in the 'pc' folder
     - you should see a file called 'mmlog'.  Rename it to something else (or
     move it), and then continue execution until it breaks here again. Save
     the new mmlog file again.  Repeat until you're bored.  What this achieves
     is a series of MM logs, one for each time the RIP's save level drops below
     that of a job (and therefore shows memory usage across jobs).  Don't forget
     that prior to this, you must have set the global variable debug_mm_watchlevel
     to a value - '8' is good.  As I said, see the aforementioned document!
   */
  {
   static Bool debug_mm_tracing_on = FALSE;  /* In the debugger, set this to 1 */
   if (debug_mm_tracing_on) {
     if ( NUMBERSAVES( slevel ) <= 1 ) {
         mm_debug_watch_live( mm_trace );     /* Write up all the info.     */
         (void) mm_trace_close();             /* Close 'mmlog' to allow access. */
      }
   }
  }
  /* NB: In order for the above memory leak tracking stuff to work properly,
     there shouldn't really be any further statements between here and the
     end of this function.
  */
#endif


  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            misc###()          author:              Andrew Cave
   creation date:       03-Feb-1987        last modification:   ##-###-####
   arguments:           none .
   description:

  Miscellaneous bit-n-bobs that help Uncle Restore along.

---------------------------------------------------------------------------- */
static void clearMakeScaleFontCache( int32 slevel )
{
  int32 src , dst , lim ;

  MFONTLOOK *mfltemp ;

  dst = 0 ;
  lim = mflookpos ;
  for ( src = 0 ; src < lim ; ++src ) {
    mfltemp = poldmfonts[ src ] ;
    if ( theISaveLevel( mfltemp ) > slevel )
      --mflookpos ;
    else {
      poldmfonts[ src ] = poldmfonts[ dst ] ;
      poldmfonts[ dst ] = mfltemp ;
      ++dst ;
    }
  }
}

static Bool checkValidRestoreStack( STACK *thestack , int32 ignore , int32 slevel )
{
  int32 i ;
  int32 aslevel ;
  OBJECT *theo ;

  aslevel = NUMBERSAVES(slevel) ;

  if ( fastIStackAccess( thestack )) {
    theo = theITop( thestack ) ;

    for ( i = theIStackSize( thestack ) - ignore ; i >= 0 ; --i ) {
      if ( oType( *theo ) == OSAVE ) {
        if ( save_level(oSave(*theo)) > slevel )
          return error_handler( INVALIDRESTORE ) ;
      }
      else if ( isSRCompObj(*theo) )
        if (mm_ps_check(aslevel, (mm_addr_t)oOther(*theo)) != MM_SUCCESS)
          return error_handler(INVALIDRESTORE);
      --theo ;
    }
  }
  else {
    for ( i = theIStackSize( thestack ) - ignore ; i >= 0 ; --i ) {
      theo = stackindex( i , thestack ) ;
      if ( oType( *theo ) == OSAVE ) {
        if ( save_level(oSave(*theo)) > slevel )
          return error_handler( INVALIDRESTORE ) ;
      }
      else if ( isSRCompObj(*theo) )
        if (mm_ps_check(aslevel, (mm_addr_t)oOther(*theo)) != MM_SUCCESS)
          return error_handler(INVALIDRESTORE);
    }
  }
  return TRUE ;
}


static Bool deactivatePageDeviceForRestore(int32 gsid ,
                                           deactivate_pagedevice_t *dpd)
{
  GSTATE *gs_new ;

  HQASSERT( dpd, "deactivatePageDeviceForRestore: dpd is NULL" ) ;

  for ( gs_new = gstackptr ;
        gs_new && gsid != gs_new->gId ;
        gs_new = gs_new->next) {
    EMPTY_STATEMENT() ;
  }

  if ( gs_new == NULL ) /* Didn't find our gstate */
    return error_handler( UNDEFINEDRESULT ) ;

  deactivate_pagedevice( gstateptr , gs_new , NULL , dpd ) ;

  if ( dpd->action == PAGEDEVICE_REACTIVATING ) {
    /* conditions for calling EndPage procedure met. Note: this may
     * force a call to the showproc to render a page
     */
    Bool result = do_pagedevice_deactivate( dpd ) ;
    DL_STATE *page ;

    /* Distillation status must be set to that of the incoming device,
     * since the PDF out machinery may well need to use items in the VM
     * which is about to be reclaimed by the restore.
     */
    if ( theIgsDistillID( gs_new ) != theIgsDistillID( gstateptr ))
      result = result && setDistillEnable( theIgsDistillID( gs_new ) != 0 ) ;

    /* We're changing pagedevice as part of the PSVM purge. The rasterstyle
       in the inputpage may contain references to PSVM memory that's going
       out of scope, so release it here. The call_resetpagedevice() in the
       gs_setgstate() at the end of gs_cleargstates() will grab a reference
       to the active rasterstyle after the purge. */
    page = inputpage_lock() ;
    if ( page->hr != NULL )
      guc_discardRasterStyle(&page->hr) ;
    inputpage_unlock() ;

    return result ;
  }
  else
    return TRUE ;
}

Bool gs_cleargstates( int32 gid , int32 gtype,
                       deactivate_pagedevice_t *dpd )
{
  corecontext_t *context = get_core_context_interp();
  GSTATE *gs ;
  GSTATE retry ;
  Bool   auto_restore = (gtype == GST_FORM) ; /* Others may be possible too */

  HQASSERT( gstackptr == gstateptr->next,
            "gs_cleargstates: gstack is corrupt" ) ;

  for ( gs = gstackptr ;
        gs && gid != gs->gId ;
        gs = gs->next) {
    /* Complain if there's a GST_SAVE before our GST_<other> */
    if ( gtype != GST_SAVE && gs->gType == GST_SAVE ) {
      /* [64325] Unbalanced saves - we can automatically restore at the end
         of a form, and indeed may be able to at the end of patterns, shadings
         and groups too. For now, just forms: */
      if ( !auto_restore || !ps_restore(context->pscontext, gs->slevel) )
        return error_handler( INVALIDRESTORE ) ;

      /* restart the loop */
      retry.next = gstackptr ;
      gs = &retry ;
    }
  }

  if ( gs == NULL ) /* Didn't find our gstate */
    return error_handler( UNDEFINEDRESULT ) ;

  return gs_setgstate( gs , gtype , FALSE , TRUE , FALSE , dpd ) ;
}


/* save_range -- used by check_asave and check_asave_one to save a range of
   array objects */

static Bool save_range(OBJECT * unsaved, OBJECT * unsavedend, int32 glmode,
                       corecontext_t *corecontext)
{
  OBJECT *saveto, *savetoelem;
  ptrdiff_t unsavedsize;

  HQASSERT(corecontext, "No context in asave_interval");
  HQASSERT(unsaved && unsavedend && unsavedend > unsaved,
           "Bad params in asave_interval");

  unsavedsize = unsavedend - unsaved;

  HQASSERT(unsavedsize < 65536, "Interval too large in asave_interval");

  /* Get memory in which to save array. get_savememory does not set the
     saveto slot properties, because it is also used to get save slots
     for gstates. */
  saveto = get_savememory(corecontext, unsavedsize * sizeof(OBJECT), glmode);
  if ( saveto == NULL )
    return FAILURE(FALSE) ;

  theTags(*saveto) = OARRAY ;
  theLen(*saveto) = CAST_SIGNED_TO_UINT16(unsavedsize);
  oArray(*saveto) = unsaved;
  savetoelem = saveto + 1;
  /* Copy over the memory. */
  while ( unsaved < unsavedend ) {
#   ifdef ASSERT_BUILD
      mps_pool_t pool;
      mps_epvm_save_level_t level;
#   endif
    HQASSERT(mps_epvm_check(&pool, &level, mm_arena, (mps_addr_t)unsaved) &&
             level < (mps_epvm_save_level_t)NUMBERSAVES(corecontext->savelevel),
             "Saving a non-VM or current-level object");

    *savetoelem = *unsaved ; /* Struct copy to save slot properties */
    SETSLOTSAVED(*unsaved, glmode, corecontext);  /* also asserts NOTVM */
    ++unsaved; ++savetoelem;
  }
  return TRUE ;
}


/* check_asave -- check and save any unsaved elements in the array */

Bool check_asave(OBJECT *array, int32 size, int32 glmode,
                 corecontext_t *corecontext)
{
  OBJECT *arrayend, *unsaved, *unsavedend ;

  /* zero-sized arrays can be ignored */
  if (size < 1)
    return TRUE;

  HQASSERT(array != NULL, "Null array in check_asave");
  HQASSERT(corecontext, "No corecontext in check_asave") ;

  /* as can non-VM objects */
  if ( NOTVMOBJECT(*array) ) {
#   ifdef ASSERT_BUILD
      mps_pool_t pool;
      mps_epvm_save_level_t level;
#   endif
    /* Let's just make sure that this isn't a VM object... */
    HQASSERT( !mps_epvm_check( &pool, &level, mm_arena, (mps_addr_t)array ),
              "Non-VM array appears to be in VM" );
    return TRUE;
  }

  /* Run along the whole of the array, getting intervals not saved. */
  for ( unsaved = array, arrayend = array + size ;
        unsaved < arrayend ;
        ++unsaved ) {
    HQASSERT(!NOTVMOBJECT(*unsaved), "ISNOTVM set in a VM slot");

    /* Start of interval not saved. */
    if ( SLOTISNOTSAVED(unsaved, corecontext) ) {
      unsavedend = unsaved + 1;
      while ( unsavedend < arrayend &&
              SLOTISNOTSAVED(unsavedend, corecontext) )
        ++unsavedend;
      /* unsavedend now points to the end of the interval not saved. */

      if (!save_range(unsaved, unsavedend, glmode, corecontext))
        return FALSE ;
    }
  }
  return TRUE ;
}


/* check_asave_one - check and save a specific array index (and neighbours) */

Bool check_asave_one(OBJECT* array, int32 size, int32 index, int32 glmode,
                     corecontext_t *corecontext)
{
# define CHECK_ASAVE_WINDOW 32
  int32 start = index & ~(CHECK_ASAVE_WINDOW-1) ;
  int32 end = start + CHECK_ASAVE_WINDOW ;
  int32 i ;

  /* zero sized arrays can be ignored */
  if (size < 1)
    return TRUE ;

  HQASSERT(array != NULL, "Null array in check_asave_one") ;
  HQASSERT(index >= 0 && index < size, "Bad index in check_asave_one") ;
  HQASSERT(corecontext, "No corecontext in check_asave_one") ;

  /* as can non-VM objects */
  if ( NOTVMOBJECT(*array) ) {
#   ifdef ASSERT_BUILD
      mps_pool_t pool;
      mps_epvm_save_level_t level;
#   endif
    /* Let's just make sure that this isn't a VM object... */
    HQASSERT( !mps_epvm_check( &pool, &level, mm_arena, (mps_addr_t)array ),
              "Non-VM array appears to be in VM" );
    return TRUE;
  }

  if (!SLOTISNOTSAVED(array+index, corecontext))
    return TRUE ;

  if (end > size)
    end = size ;

  for (i = index-1 ; i >= start ; --i) {
    if (!SLOTISNOTSAVED(array+i, corecontext))
      start = i+1 ;
  }

  for (i = index+1 ; i < end ; ++i) {
    if (!SLOTISNOTSAVED(array+i, corecontext))
      end = i ;
  }

  return save_range(array+start, array+end, glmode, corecontext) ;
}


Bool check_dsave(OBJECT *optr, corecontext_t *corecontext)
{
  size_t size;
  int32 glmode ;
  OBJECT *endptr , *saveto ;
#ifdef ASSERT_BUILD
  mps_pool_t pool;
  mps_epvm_save_level_t level;
#endif

  HQASSERT(corecontext && corecontext->is_interpreter,
           "Called without an interpreter context");

  if ( NOTVMOBJECT(*optr) ) {
    /* Let's just make sure that this isn't a VM object... */
#ifdef ASSERT_BUILD
    mps_pool_t pool;
    mps_epvm_save_level_t level;
#endif
    HQASSERT( !mps_epvm_check( &pool, &level, mm_arena, (mps_addr_t)optr ),
              "Non-VM dict appears to be in VM" );
    return TRUE;
  }
  /* It must be a real VM object, if it's stored in a savelist. */
  HQASSERT( mps_epvm_check( &pool, &level, mm_arena, (mps_addr_t)optr )
            && level < (mps_epvm_save_level_t)NUMBERSAVES(corecontext->savelevel),
            "Saving a non-VM or current-level object" );
  glmode = oGlobalValue(*optr) ;

  /* Much easier than array case - save whole dictionary. */
  size = DICT_ALLOC_LEN(optr);
  /* Get memory in which to save array. get_savememory does not set the
     saveto slot properties, because it is also used to get save slots for
     gstates. */
  saveto = get_savememory(corecontext, (2 + 2 * size) * sizeof(OBJECT), glmode) ;
  if ( saveto == NULL )
    return FALSE ;

  theTags(*saveto) = ODICTIONARY ;
  theLen(*saveto) = CAST_TO_UINT16(size) ;
  oDict(*saveto) = optr ;
  ++saveto ;

/* Copy over dictionary chain OBJECT */
  --optr ;
  *saveto = *optr ; /* Struct copy to save slot properties */
  SETSLOTSAVED(*optr, glmode, corecontext) ;
  ++optr ;
  ++saveto ;

  *saveto = *optr ; /* Struct copy to save slot properties */
  SETSLOTSAVED(*optr, glmode, corecontext) ;
  ++optr ;
  ++saveto ;
/* Copy over the memory. */
  for ( endptr = optr + size + size ; optr < endptr ; saveto += 2, optr += 2) {
    saveto[0] = optr[0] ; /* Struct copy to save slot properties */
    SETSLOTSAVED(optr[0], glmode, corecontext) ;
    saveto[1] = optr[1] ; /* Struct copy to save slot properties */
    SETSLOTSAVED(optr[1], glmode, corecontext) ;
  }
  return TRUE ;
}

Bool check_dsave_all(/*@notnull@*/ OBJECT *dict)
{
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT(oType(*dict) == ODICTIONARY,
           "Should be a dictionary prepared for modification") ;
  HQASSERT(oCanWrite(*oDict(*dict)),
           "Shouldn't be preparing a non-writable dictionary for modification") ;
  /* Traverse the dictionary pointer chain, checking if each extension dict
     needs saved. */
  do {
    dict = oDict(*dict) ;
    if ( SLOTISNOTSAVED(dict, corecontext) )
      if ( !check_dsave(dict, corecontext) )
        return FALSE ;
    --dict ;
  } while ( oType(*dict) == ODICTIONARY ) ;
  return TRUE ;
}


Bool check_gsave(corecontext_t *corecontext, GSTATE *gs_check , int32 glmode )
{
  OBJECT *saveto ;
  GSTATE *gs ;
#ifdef ASSERT_BUILD
  mps_pool_t pool;
  mps_epvm_save_level_t level;
#endif

  /* It must be a real VM object, if it's stored in a savelist. */
  HQASSERT( mps_epvm_check( &pool, &level, mm_arena, (mps_addr_t)gs_check )
            && level < (mps_epvm_save_level_t)NUMBERSAVES(corecontext->savelevel),
            "Saving a non-VM or current-level object" );

  /* Much easier than array/dictionary case, just copy top structure.
   * get memory in which to save gstate + object.
   */
  saveto = get_savememory(corecontext, gstate_size(NULL), glmode) ;
  if ( saveto == NULL )
    return FALSE ;

  gs = ( GSTATE * )(saveto + 1) ;
  gs_updatePtrs(gs);

  if ( ! gs_copygstate(gs, gs_check, GST_GSAVE, NULL) )
    return FALSE ;

  /* New gstate, so we give it a new id */
  gs->gId   = ++gstateId ;
  gs->gType = GST_NOTYPE ;
  gs->next  = NULL ;

  /* Also need to save/restore the current slevel. */
  gs->slevel = gs_check->slevel ; /* Must retain old save level */

  theTags(*saveto) = OGSTATE ;
  theLen(*saveto) = CAST_UNSIGNED_TO_UINT16(gstate_size(NULL)) ;
  oGState(*saveto) = gs_check ;

  SETGSTATESAVED(gs_check, glmode, corecontext) ;

  return TRUE ;
}


/* ---------------------------------------------------------------------------*/
typedef struct {
  OBJECT *newdict ;
  uint32 recursion ;
  int32 glmode ;
} psvm_copy_params ;

static Bool psvm_copy_dictwalkfn(OBJECT *thek, OBJECT *theo, void *data)
{
  psvm_copy_params *params = data ;
  OBJECT newobj = OBJECT_NOTVM_NOTHING ;

  HQASSERT(theo, "No object in HDLT copy dictwalk callback.") ;
  HQASSERT(thek, "No key in HDLT copy dictwalk callback.") ;

  return (psvm_copy_object(&newobj, theo, params->recursion - 1, params->glmode) &&
          insert_hash(params->newdict, thek, &newobj)) ;
}

/* Deep copy to get an object into PostScript VM. We make the assumption
   that, if the object is in PostScript VM, then all of its sub-objects are
   too. */
Bool psvm_copy_object(OBJECT *copy, OBJECT *orig,
                      uint32 recursion, int32 glmode)
{
  mps_pool_t pool;
  mps_epvm_save_level_t level;
  Bool currentglmode ;
  Bool result = FALSE ;
  corecontext_t *corecontext = get_core_context_interp() ;

  if ( recursion == 0 )
    return error_handler(LIMITCHECK) ;

  HQASSERT(copy, "Nowhere to copy object to.") ;
  HQASSERT(orig, "Nowhere to copy object from.") ;
  HQASSERT(copy != orig , "Source and destination should not match.") ;

  currentglmode = setglallocmode(corecontext, glmode) ;

#define return DO_NOT_RETURN_GO_TO_cleanup_INSTEAD!
  switch ( oType(*orig) ) {
  case OLONGSTRING:
  case OGSTATE:
    /* Nothing but PS creates OLONGSTRING or OGSTATE. */
    HQASSERT(mps_epvm_check(&pool, &level, mm_arena, (mps_addr_t)oOther(*orig)),
             "Object is not already in PSVM") ;
    /*@fallthrough@*/
    /* Complex types, which can just be copied directly (they don't appear
       in non PSVM objects). */
  case OFILE:
  case OSAVE:
    Copy(copy, orig) ;
    break ;
    /* Simple types */
  case OINTEGER:
  case OREAL:
  case OINFINITY:
  case OOPERATOR:
  case OBOOLEAN:
  case OMARK:
  case ONULL:
  case OFONTID:
  case ONAME:
  case OINDIRECT:
  case OFILEOFFSET:
  case OCPOINTER:
    Copy(copy, orig) ;
    break ;
  case ODICTIONARY:
    /** \todo ajcd 2007-12-29: Use ISNOTVM test on slot to do this now. */
    if ( !mps_epvm_check(&pool, &level, mm_arena, (mps_addr_t)oDict(*orig)) ) {
      psvm_copy_params params ;

      if ( ! ps_dictionary(copy, theLen(*orig)) )
        goto cleanup ;

      params.recursion = recursion ;
      params.newdict = copy ;
      params.glmode = glmode ;

      if ( !walk_dictionary(orig, psvm_copy_dictwalkfn, &params) )
        goto cleanup ;

      /* Copy access modes. */
      theTags(*oDict(*copy)) = theTags(*oDict(*orig)) ;
    } else {
      /* Dictionary is in PostScript VM, it's OK. */
      Copy(copy, orig) ;
    }
    break ;
  case OSTRING:
    /** \todo ajcd 2007-12-29: Use ISNOTVM test on slot to do this now. */
    if ( theLen(*orig) > 0 &&
         !mps_epvm_check(&pool, &level, mm_arena, (mps_addr_t)oString(*orig)) ) {
      if ( !ps_string(copy, oString(*orig), theLen(*orig)) )
        goto cleanup ;

      /* Copy access modes. */
      theTags(*copy) = theTags(*orig) ;
    } else {
      /* String is in PostScript VM, it's OK. */
      Copy(copy, orig) ;
    }
    break ;
  case OARRAY:
  case OPACKEDARRAY:
    /** \todo ajcd 2007-12-29: Use ISNOTVM test on slot to do this now. */
    if ( theLen(*orig) > 0 &&
         !mps_epvm_check(&pool, &level, mm_arena, (mps_addr_t)oArray(*orig)) ) {
      uint32 i ;
      OBJECT *from, *to ;

      if ( !ps_array(copy, theLen(*orig)) )
        goto cleanup ;

      for ( i = theLen(*copy), from = oArray(*orig), to = oArray(*copy) ;
            i > 0 ;
            --i, ++from, ++to ) {
        if ( !psvm_copy_object(to, from, recursion - 1, glmode) )
          goto cleanup ;

        /* Note that we leave the local into global test until after copying
           the object, because we don't trust the globalness of non-PSVM
           objects. */
        if ( glmode )
          if ( illegalLocalIntoGlobal(to, corecontext) ) {
            object_store_null(to) ;
            result = error_handler(INVALIDACCESS) ;
            goto cleanup ;
          }
      }

      /* Copy access modes. */
      theTags(*copy) = theTags(*orig) ;
    } else {
      /* Array is in PostScript VM, it's OK. */
      Copy(copy, orig) ;
    }
    break ;
  default:
    HQFAIL("Trying to copy an unknown or unsupported object type to PSVM.") ;
    goto cleanup ;
  }

  result = TRUE ;

 cleanup:
  setglallocmode(corecontext, currentglmode) ;

#undef return
  return result ;
}

Bool psvm_copy_dictmatch(/*@out@*/ /*@notnull@*/ OBJECT *dict,
                         /*@in@*/ /*@notnull@*/ NAMETYPEMATCH match[],
                         uint32 recursion, int32 glmode)
{
  HQASSERT(dict && oType(*dict) == ODICTIONARY,
           "No dictionary to fill from dictmatch.") ;
  HQASSERT(match, "No dictmatch to fill dict from.") ;

  while ( theISomeLeft(match) ) {
    if ( match->result != NULL ) {
      OBJECT nameobj = OBJECT_NOTVM_NOTHING ;
      OBJECT copy = OBJECT_NOTVM_NOTHING ;

      object_store_name(&nameobj, match->name & ~OOPTIONAL, LITERAL) ;

      if ( !psvm_copy_object(&copy, match->result, recursion, glmode) ||
           !insert_hash(dict, &nameobj, &copy) )
        return FALSE ;
    }

    ++match ;
  }

  return TRUE ;
}

#if defined(ASSERT_BUILD)
/* Assertion check for objects being in PostScript memory. */
Bool psvm_assert_check(void *memory)
{
  mps_pool_t pool;
  mps_epvm_save_level_t level;

  return (memory == NULL ||
          mps_epvm_check(&pool, &level, mm_arena, (mps_addr_t)memory)) ;
}
#endif

/*
Log stripped */
