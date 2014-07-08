/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:lanlevel.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS language level functions
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "fileparam.h"
#include "namedef_.h"
#include "mm.h" /* mm_arena */
#include "mps.h" /* mps_root_t */
#include "gcscan.h" /* ps_scan */

#include "stacks.h"
#include "params.h"
#include "psvm.h"
#include "dicthash.h"
#include "control.h"
#include "chartype.h"
#include "lanlevel.h"
#include "execops.h"
#include "swmemory.h"
#include "uvms.h" /* UVS/UVM */

OBJECT level1sysdict = OBJECT_NOTVM_NOTHING ;
OBJECT level2sysdict = OBJECT_NOTVM_NOTHING ;
OBJECT level3sysdict = OBJECT_NOTVM_NOTHING ;

static OBJECT *levelNsysdicts[] =
{ NULL ,
  & level1sysdict ,
  & level2sysdict ,
  & level3sysdict
} ;

static mps_root_t level1sysdict_root;
static mps_root_t level2sysdict_root;
static mps_root_t level3sysdict_root;


#define MAXLANGUAGELEVEL ( sizeof( levelNsysdicts ) / sizeof( levelNsysdicts[ 0 ] ) - 1 )

static Bool make_systemdict( OBJECT *newsystemdict , int32 level , int32 wantColorOps ) ;


/* initLanguageLevelDicts - initialize the system dicts for each lang. level */
OBJECT *initLanguageLevelDicts(void)
{
  corecontext_t *context = get_core_context_interp();

  setglallocmode(context, FALSE );
  if ( ! ps_dictionary(&level1sysdict, SYSDICT_SIZE) )
    return NULL ;
  setglallocmode(context, TRUE );

  if ( ! ps_dictionary(&level2sysdict, SYSDICT_SIZE) )
    return NULL;

  if ( ! ps_dictionary(&level3sysdict, SYSDICT_SIZE) )
    return NULL;

  /* Register the roots. */
  if ( mps_root_create_fmt( &level1sysdict_root, mm_arena, mps_rank_exact(),
                            0, ps_scan, &level1sysdict, &level1sysdict + 1 )
       != MPS_RES_OK )
    return NULL ;

  if ( mps_root_create_fmt( &level2sysdict_root, mm_arena, mps_rank_exact(),
                            0, ps_scan, &level2sysdict, &level2sysdict + 1 )
       != MPS_RES_OK ) {
    mps_root_destroy(level1sysdict_root) ;
    return NULL ;
  }

  if ( mps_root_create_fmt( &level3sysdict_root, mm_arena, mps_rank_exact(),
                            0, ps_scan, &level3sysdict, &level3sysdict + 1 )
       != MPS_RES_OK ) {
    mps_root_destroy(level1sysdict_root) ;
    mps_root_destroy(level2sysdict_root) ;
    return NULL;
  }

  return &level3sysdict;
}


/* finishLanguageLevelDicts - finish the system dicts for each lang. level */
void finishLanguageLevelDicts(void)
{
  /* Make sure we don't use them after they've been finished. */
  theTags( level1sysdict ) = ONULL;
  mps_root_destroy( level1sysdict_root );
  theTags( level2sysdict ) = ONULL;
  mps_root_destroy( level2sysdict_root );
  theTags( level3sysdict ) = ONULL;
  mps_root_destroy( level3sysdict_root );
}


/* ----------------------------------------------------------------------------
   function:            setlanguagelevel  author:              Luke Tunmer
   creation date:       28-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool setlanguagelevel( corecontext_t *context )
{
  SYSTEMPARAMS *systemparams = context->systemparams;
  int32 new_level , old_level ;
  int32 changed_level , changed_colops ;

  new_level = systemparams->LanguageLevel ;
  old_level = theISaveLangLevel( workingsave ) ;
  changed_level = old_level != new_level ;

  HQASSERT( new_level <= MAXLANGUAGELEVEL , "Bad LanguageLevel" ) ;

  /* test whether color extension is required or not */
  if ( new_level == 1 ) {
    changed_colops = ( (int32)theISaveColorExt( workingsave ) != (int32) systemparams->ColorExtension ) ;
    theISaveColorExt( workingsave ) = systemparams->ColorExtension ;
  }
  else {
    /* Color extension is required as part of all other levels. */
    changed_colops = FALSE ; /* Don't really care */
    theISaveColorExt( workingsave ) = systemparams->ColorExtension = TRUE ;
  }

  /* test whether composite fonts are required */
  if ( new_level == 1 ) {
    theISaveCompFonts( workingsave ) = systemparams->CompositeFonts ;
  }
  else {
    theISaveCompFonts( workingsave ) = systemparams->CompositeFonts = TRUE ;
  }

  if ( changed_level || ( new_level == 1 && changed_colops )) {
    OBJECT *newsystemdict = levelNsysdicts[ new_level ] ;
    if ( new_level != MAXLANGUAGELEVEL )
      if ( ! make_systemdict( newsystemdict , new_level , systemparams->ColorExtension ))
        return FALSE ;

    if ( changed_level ) {
      int32 dsize , i ;
      OBJECT *tmpo ;
      OBJECT *oldsystemdict = levelNsysdicts[ old_level ] ;

      /* Change the dictionary stack, adding or removing globaldict as necessary. */
      if ( old_level == 1 ) {
        /* Need to add in globaldict. */
        OBJECT *dict1 , *dict2 ;

        /* Copy dictionaries up one OBJECT */
        dict1 = theTop( dictstack ) ;
        ( void )push( dict1 , & dictstack ) ;
        dsize = theStackSize( dictstack ) ;
        dict1 = stackindex( 1 , & dictstack ) ;
        for ( i = 2 ; i < dsize ; ++i ) {
          dict2 = dict1 ;
          dict1 = stackindex( i , & dictstack ) ;
          Copy( dict2 , dict1 ) ;
        }

        /* Increment reference count on globaldict. */
        tmpo = ( & globaldict ) ;
        tmpo = oDict(*tmpo) ;
        ++oInteger(tmpo[-2]) ;

        /* Copy globaldict. */
        Copy( dict1 , & globaldict ) ;

        /* Reset dictionary top object cache pointer */
        topDictStackObj = theTop( dictstack ) ;
      }

      if ( new_level == 1 ) {
        /* Need to remove globaldict. */
        OBJECT *dict1 , *dict2 ;

        /* Copy dictionaries down one OBJECT */
        dsize = theStackSize( dictstack ) - 1 ;
        dict1 = stackindex( dsize , & dictstack ) ;
        for ( i = dsize - 1 ; i >= 0 ; --i ) {
          dict2 = dict1 ;
          dict1 = stackindex( i , & dictstack ) ;
          Copy( dict2 , dict1 ) ;
        }

        /* Decrement reference count on globaldict. */
        tmpo = ( & globaldict ) ;
        tmpo = oDict(*tmpo) ;
        --oInteger(tmpo[-2]) ;

        /* Throw away top dictionary. */
        pop( & dictstack ) ;

        /* Reset dictionary top object cache pointer */
        topDictStackObj = theTop( dictstack ) ;
      }

      /* Change bottom reference to oldsystemdict on the dictionary stack with references to the new one. */

      /* Decrement reference counts on old systemdict... */
      tmpo = oDict(*oldsystemdict) ;
      --oInteger(tmpo[-2]) ;

      /* ...increment reference counts on new systemdict... */
      tmpo = oDict(*newsystemdict) ;
      ++oInteger(tmpo[-2]) ;

      /* ...and install new systemdict on dictionary stack. */
      dsize = theStackSize( dictstack ) ;
      tmpo = stackindex( dsize , & dictstack ) ;
      Copy( tmpo , newsystemdict ) ;

      install_systemdict( newsystemdict ) ;

      /* [Re]set various things like ObjectFormat, LanguageLevel,... */
      if ( new_level == 1 ) {
        theIObjectFormat( workingsave ) = 0 ;
        _char_table[ '\\' ] = SPECIAL_CHAR ;
      }
      else {
        _char_table[ '\\' ] = REGULAR ;
      }
      systemparams->LanguageLevel = ( uint8 )new_level ;
      theISaveLangLevel( workingsave ) = ( int8 )new_level ;

      tmpo = fast_extract_hash_name(&internaldict, NAME_changelanguagelevel) ;
      if ( ! tmpo )
        return error_handler( UNREGISTERED ) ;
      return setup_pending_exec( tmpo , TRUE ) ;
    }
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            makelevel1dict    author:              Luke Tunmer
   creation date:       28-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool isNotLevelNOp( OBJECT *theo , void *doData )
{
  HQASSERT( doData , "doData NULL in isNotLevelNOp" ) ;

  if ( oType(*theo) == ONAME ) {
    int32 level = (*( int32 * )doData ) ;
    if ( (theIOpClass(oName(*theo)) & LEVELMASK) > level )
      return FALSE ;
  }
  return TRUE ;
}

static Bool isNotLevel1OpOrColorOp( OBJECT *theo , void *doData )
{
  UNUSED_PARAM( void * , doData ) ;

  if ( oType(*theo) == ONAME ) {
    if ( (theIOpClass(oName(*theo)) & LEVELMASK) > 1 )
      return FALSE ;
    if ( (theIOpClass(oName(*theo)) & COLOREXTOP) != 0 )
      return FALSE ;
  }
  return TRUE ;
}

/* This routine copies the elements from the Level N systemdict (currently N=3)
 * into the Level M systemdict (where M < N).
 * Level 1 systemdict is always in local memory, and Levels 2 < M < N are always
 * in global memory.
 */
static Bool make_systemdict( OBJECT *newsystemdict , int32 level , int32 wantColorOps )
{
  corecontext_t *context = get_core_context_interp();
  int32 glmode ;
  int32 slevel ;
  Bool ok ;
  OBJECT *theo ;
  OBJECT *fullsystemdict = levelNsysdicts[ MAXLANGUAGELEVEL ] ;

  Bool (*doInsert)( OBJECT * , void * ) ;

  doInsert = isNotLevelNOp ;
  if ( level == 1 && ! wantColorOps )
    doInsert = isNotLevel1OpOrColorOp ;

  slevel = context->savelevel ;
  glmode = oGlobalValue(*newsystemdict) ;
  SET_DICT_ACCESS(newsystemdict, UNLIMITED) ;
  if ( glmode ) {
    /* For global systemdicts we have to do something sneaky.  That's
     * because some local OBJECTs do get put into the Level N systemdict
     * (even though this should not be allowed).  We allow this to on
     * bootup when the savelevel is 0, and so to allow this for late
     * building of non-Level N systemdicts we need to pretend we're at
     * savelevel 0.  That means saving the dictionary (for save/restore)
     * first of all since insert_hash now won't do this.  */
    if ( ! check_dsave_all(newsystemdict) ) {
      SET_DICT_ACCESS(newsystemdict, READ_ONLY) ;
      return FALSE ;
    }
    context->savelevel = 0 ;
  }

  ok = CopyDictionary( fullsystemdict , newsystemdict , doInsert , ( void * )( & level )) ;
  SET_DICT_ACCESS(newsystemdict, READ_ONLY) ;

  if ( glmode )
    context->savelevel = slevel ;

  if ( !ok )
    return FALSE ;

  /* Change the systemdict entry in the dictionary. */
  if ( NULL == (theo = fast_extract_hash_name(newsystemdict, NAME_systemdict)) )
    return error_handler( UNREGISTERED ) ;

  Copy( theo , newsystemdict ) ;

  /* Change language level in the dictionary */
  if ( NULL == (theo = fast_extract_hash_name(newsystemdict, NAME_languagelevel)) )
    return error_handler( UNREGISTERED ) ;

  oInteger(inewobj) = level ;
  Copy( theo , & inewobj ) ;

  return TRUE ;
}

void init_C_globals_lanlevel(void)
{
  OBJECT oinit = OBJECT_NOTVM_NOTHING ;
  level1sysdict = level2sysdict = level3sysdict = oinit ;
  level1sysdict_root = level2sysdict_root = level3sysdict_root = NULL ;
}

/* Log stripped */
