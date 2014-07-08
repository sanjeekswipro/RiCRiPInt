/** \file
 * \ingroup saves
 *
 * $HopeName: SWcore!src:saves.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Combined save/restore for Core RIP
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "objects.h"
#include "gcscan.h"
#include "params.h"
#include "t6params.h"
#include "pdfparam.h"
#include "xmlops.h"
#include "psvm.h"
#include "fonts.h" /* fonts_restore_commit */
#include "blobdata.h" /* blob_restore_commit */
#include "fontcache.h" /* fontcache_restore_names */
#include "halftone.h" /* htcache_restore_names */
#include "saves.h"
#include "coreinit.h"
#include "swstart.h"
#include "xps.h"
#include "gscparams.h"  /* COLOR_USER_PARAMS */
#include "gs_color.h"
#include "dlstate.h"

/* Save structure consists of bits of all integrated sub-compounds */
struct SAVELIST {
  int32             savelevel ;
  OBJECTSAVE        objectsave ;
  PS_SAVEINFO       ps_saveinfo ;
  USERPARAMS        userparams ;
  COLOR_USER_PARAMS color_userparams;
  PDFPARAMS         pdfparams ;
  TIFF6PARAMS       tiff6params;
  MISCHOOKPARAMS    mischookparams ;
  XMLPARAMS         xmlparams;
  XPSPARAMS         xpsparams;
} ;

static SAVELIST *savestack = NULL ;

#if defined( ASSERT_BUILD )
/* Track save levels to make sure they go up & down properly */
static int32 nextsave = 0 ;
#endif


/* saveroot -- root for the saves */
static mps_root_t saveroot;

/* scan_saveroot -- scanning function for the saves */
static mps_res_t MPS_CALL scan_saveroot(mps_ss_t ss, void *p, size_t s)
{
  corecontext_t *context = get_core_context_interp();
  mps_res_t res;
  int32 i;
  size_t ix;

  HQASSERT(NUMBERSAVES(context->savelevel) >= MINSAVELEVEL &&
           NUMBERSAVES(context->savelevel) <= MAXSAVELEVELS,
           "invalid save level");
  UNUSED_PARAM( void *, p ); UNUSED_PARAM( size_t, s );

  res = ps_scan_saveinfo( ss, workingsave );
  if ( res != MPS_RES_OK ) return res;

  if ( NUMBERSAVES(context->savelevel) > MINSAVELEVEL ) {
    for ( i = context->savelevel - SAVELEVELINC ;
          NUMBERSAVES(i) >= MINSAVELEVEL ;
          i -= SAVELEVELINC ) {
      ix = NUMBERSAVES(i);
      /* objectsave only contains weak pointers (namepurge) */
      res = ps_scan_saveinfo( ss, &savestack[ix].ps_saveinfo );
      if ( res != MPS_RES_OK ) return res;
      res = scanUserParams( ss, (void *)&savestack[ix].userparams, 0 );
      if ( res != MPS_RES_OK ) return res;
      res = gsc_scanColorUserParams( ss, (void *)&savestack[ix].color_userparams, 0 );
      if ( res != MPS_RES_OK ) return res;
      res = pdfparams_scan( ss, (void *)&savestack[ix].pdfparams, 0 );
      if ( res != MPS_RES_OK ) return res;
      /* tiff6params don't contain any pointers. */
      res = scanMiscHookParams( ss, (void *)&savestack[ix].mischookparams, 0 );
      if ( res != MPS_RES_OK ) return res;
      /* xmlparams don't contain any pointers - yet! */
      res = xpsparams_scan( ss, (void *)&savestack[ix].xpsparams, 0 );
      if ( res != MPS_RES_OK ) return res;
    }
  }
  return MPS_RES_OK;
}


/* allocSaves -- allocate the savestack and register it as root */
Bool saves_swstart(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  HQASSERT(savestack == NULL, "Saves already allocated") ;
  if ( (savestack = mm_alloc_static(MAXSAVELEVELS * sizeof(SAVELIST))) == NULL )
    return FALSE ;

  /* Create MPS root last so we can clean it up on success */
  if ( mps_root_create( &saveroot, mm_arena, mps_rank_exact(),
                        0, scan_saveroot, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}


/* finishSaves - deinitialization for savestack */
void saves_finish(void)
{
  mps_root_destroy( saveroot );
}


SAVELIST *save_prepare(int32 slevel)
{
  HQASSERT(savestack, "No saves allocated") ;
  HQASSERT(NUMBERSAVES(slevel) == nextsave,
           "save alloc parameter doesn't match tracked save allocation") ;

  HQASSERT(NUMBERSAVES(SAVEMASK) >= MAXSAVELEVELS, "Incorrect_value_of_SAVE_MASK");

  if ( NUMBERSAVES(slevel) >= MAXSAVELEVELS ) {
    (void)error_handler(LIMITCHECK) ;
    return NULL ;
  }

  return &savestack[NUMBERSAVES(slevel)] ;
}

void save_commit(SAVELIST *save)
{
  corecontext_t *context = get_core_context_interp() ;

  HQASSERT(savestack, "No saves allocated") ;
  HQASSERT(save - savestack == nextsave,
           "Committed save doesn't match next save level") ;

  /* Call out to all the clients built into this RIP */
  objects_save_commit(context, &save->objectsave) ;
  ps_save_commit(&save->ps_saveinfo) ;
  gsc_save(context->savelevel);

  /* Params saves are simple enough we won't bother with callbacks for them. */
  save->mischookparams = *context->mischookparams ;
  save->userparams = *context->userparams ;
  save->color_userparams = *context->color_userparams ;
  save->pdfparams = *context->pdfparams ;
  save->tiff6params = *context->tiff6params ;
  save->xmlparams = *context->xmlparams;
  save->xpsparams = *context->xpsparams;

  save->savelevel = context->savelevel ;
  context->savelevel += SAVELEVELINC ;

#if defined( ASSERT_BUILD )
  nextsave = NUMBERSAVES(context->savelevel) ;
#endif
}

/* Prepare for restore by cleaning up all save levels between current and
   destination. */
Bool restore_prepare(int32 slevel)
{
  corecontext_t *context = get_core_context_interp();
  int32 i ;

  HQASSERT(savestack, "No saves allocated") ;
  HQASSERT(NUMBERSAVES(slevel) >= MINSAVELEVEL &&
           NUMBERSAVES(slevel) < MAXSAVELEVELS &&
           slevel < context->savelevel,
           "Restore to invalid save level") ;

  /* Call out to all the clients built into this RIP */
  for (i = context->savelevel - SAVELEVELINC ;
       i >= slevel ;
       i -= SAVELEVELINC ) {
    SAVELIST *sptr = &savestack[NUMBERSAVES(i)] ;

    objects_restore_prepare(&sptr->objectsave) ;
    if ( !ps_restore_prepare(&sptr->ps_saveinfo, i) )
      /* An error here is likely to be fatal */
      return FALSE ;
  }

  /* Removing names from the font cache will not block a restore, but is done
     before the rest of restore to be safe. Once all restore actions are in
     restore_commit, this can be the first action there. */
  if ( NUMBERSAVES(slevel) <= MAXGLOBALSAVELEVEL ) {
    fontcache_restore_names(slevel) ;
    htcache_restore_names(slevel);
  }
  return TRUE ;
}

void restore_commit(int32 slevel)
{
  SAVELIST *sptr ;
  corecontext_t *context = get_core_context_interp();

  HQASSERT(savestack, "No saves allocated") ;
  HQASSERT(NUMBERSAVES(slevel) >= MINSAVELEVEL &&
           NUMBERSAVES(slevel) < MAXSAVELEVELS &&
           slevel < context->savelevel,
           "Restore to invalid save level") ;

  sptr = &savestack[NUMBERSAVES(slevel)] ;

  /* Call out to all the clients built into this RIP */
  fonts_restore_commit(slevel) ;
  blob_restore_commit(slevel) ;
  objects_restore_commit(context, &sptr->objectsave) ;
  ps_restore_commit(&sptr->ps_saveinfo) ;

  /* Params saves are simple enough we won't bother with callbacks for them. */
  *context->mischookparams = sptr->mischookparams ;
  *context->userparams = sptr->userparams ;
  *context->color_userparams = sptr->color_userparams ;
  *context->pdfparams = sptr->pdfparams ;
  *context->tiff6params = sptr->tiff6params ;
  *context->xmlparams = sptr->xmlparams;
  *context->xpsparams = sptr->xpsparams;

  context->savelevel = sptr->savelevel ;

#if defined( ASSERT_BUILD )
  nextsave = NUMBERSAVES(context->savelevel) ;
#endif
}

int32 save_level(SAVELIST *save)
{
  HQASSERT(savestack, "No saves allocated") ;
  HQASSERT(save, "No save to get level from") ;
  HQASSERT(NUMBERSAVES(save->savelevel) == save - savestack,
           "Save level does not match save pointer") ;

  return save->savelevel ;
}


/*  save_objectsave_map - map over objectsaves in the save stack */

int save_objectsave_map(save_objectsave_map_fn fn, void *p)
{
  int32 i;
  int do_continue = TRUE;

  for ( i = get_core_context_interp()->savelevel - SAVELEVELINC ;
        NUMBERSAVES(i) >= MINSAVELEVEL && do_continue ;
        i -= SAVELEVELINC ) {
    SAVELIST *sptr = &savestack[NUMBERSAVES(i)];
    do_continue = (fn)( &sptr->objectsave, i, p );
  }
  return do_continue;
}


/*------------------------- OBJECT/SAVE INTERFACE ---------------------------*/

OBJECT *get_savememory(corecontext_t *corecontext, size_t size, Bool glmode)
{
  OBJECT *saveto ;
  int32 sindex ;
  int8 mark ;

  HQASSERT(savestack, "No saves allocated") ;
  HQASSERT(NUMBERSAVES(corecontext->savelevel) > MINSAVELEVEL &&
           NUMBERSAVES(corecontext->savelevel) <= MAXSAVELEVELS,
           "No previous save level") ;

  size += sizeof(OBJECT) * 2 ; /* Add space for headers */
  if ( glmode )
    saveto = (OBJECT *)get_gsmemory(size) ;
  else
    saveto = (OBJECT *)get_lsmemory(size) ;

  if ( saveto == NULL ) {
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  sindex = NUMBERSAVES(corecontext->savelevel - SAVELEVELINC) ;
  if ( glmode && sindex > 1 )
    sindex = 1 ;

  /* Save memory is in PSVM string memory, so object mark and tags are not
     initialised automatically. */
  mark = (int8)(ISPSVM|ISLOCAL|GLMODE_SAVELEVEL(glmode, corecontext)) ;

  /* Connect object to previous save memory list */
  theTags(*saveto) = OARRAY | READ_ONLY | LITERAL ;
  theMark(*saveto) = mark ;
  theLen(*saveto) = 1 ;
  oArray(*saveto) = theOMemory(savestack[sindex].ps_saveinfo) ;
  theOMemory(savestack[sindex].ps_saveinfo) = saveto ;

  ++saveto ;

  /* Default tags cause object to be ignored */
  theTags(*saveto) = ONULL | LITERAL ;
  theMark(*saveto) = mark ;
  theLen(*saveto) = 0 ;

  return saveto ;
}

/* Get PostScript saved info for a save level */
PS_SAVEINFO *saved_ps_info(int32 slevel)
{
  HQASSERT(savestack, "No saves allocated") ;
  HQASSERT(NUMBERSAVES(slevel) >= MINSAVELEVEL &&
           NUMBERSAVES(slevel) < MAXSAVELEVELS &&
           slevel < get_core_context_interp()->savelevel,
           "Trying to get saved PS info to invalid save level") ;

  return &savestack[NUMBERSAVES(slevel)].ps_saveinfo ;
}

void init_C_globals_saves(void)
{
  savestack = NULL ;
  saveroot = NULL ;
#if defined( ASSERT_BUILD )
  nextsave = 0 ;
#endif
}

/*
Log stripped */
