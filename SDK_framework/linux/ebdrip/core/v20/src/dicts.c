/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:dicts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS dictionary creation
 */

#include "core.h"
#include "coreinit.h"
#include "mm.h" /* mm_arena */
#include "mmcompat.h"
#include "mps.h" /* mps_root_t */
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "uvms.h" /* UVS/UVM */
#include "namedef_.h"
#include "miscops.h" /* in_super_exec() */

#include "params.h"     /* for psvm.h */
#include "psvm.h"
#include "dicthash.h"
#include "control.h"    /* for OBJECT *topDictStackObj */
#include "lanlevel.h"
#include "stacks.h"
#include "gcscan.h" /* ps_scan */
#include "bitblts.h"
#include "display.h"
#include "matrix.h"
#include "graphics.h"
#include "swmemory.h"

#define EQSTRING_LENGTH 1024

#define DICTIONARY_INCREMENT_SIZE 32

OBJECT userdict ;
OBJECT systemdict ;
OBJECT globaldict ;
OBJECT internaldict ;

static mps_root_t internaldict_root;

/* Memory used in the printing routines =string from PS */
static uint8 *pstored = NULL ;


static void init_C_globals_dicts(void)
{
  userdict = onothing ;
  systemdict = onothing ;
  globaldict = onothing ;
  internaldict = onothing ;
  internaldict_root = NULL ;
  pstored = NULL ;
}

/* Initialises the base PS dictionaries on the dictionary stack. */
static Bool ps_dicts_swstart(struct SWSTART *params)
{
  OBJECT *tmpo ;
  OBJECT key = OBJECT_NOTVM_NOTHING, obj = OBJECT_NOTVM_NOTHING ;
  OBJECT *toplevelsysdict;
  Bool ok ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( (pstored = mm_alloc_static( EQSTRING_LENGTH )) == NULL )
    return FALSE ;

  if ( (toplevelsysdict = initLanguageLevelDicts()) == NULL )
    return FALSE ;

  Copy( & systemdict , toplevelsysdict ) ;

  if ( !ps_dictionary(&globaldict, GLODICT_SIZE) ) {
    finishLanguageLevelDicts();
    return FALSE ;
  }

  setglallocmode(get_core_context(), FALSE ) ;

  if ( ! ps_dictionary(&userdict, USRDICT_SIZE) ) {
    finishLanguageLevelDicts();
    return FALSE ;
  }

  if ( ! ps_dictionary(&internaldict, INTERNAL_SIZE) ) {
    finishLanguageLevelDicts();
    return FALSE ;
  }

  theTags( key ) = ONAME | LITERAL ;
  theTags( obj ) = OOPERATOR | EXECUTABLE ;

  oName(key) = ( & system_names[ NAME_systemdict ] ) ;
  ok = insert_hash( & systemdict , & key , & systemdict ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  oName(key) = ( & system_names[ NAME_userdict ] ) ;
  ok = insert_hash( & systemdict , & key , & userdict ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  oName(key) = ( & system_names[ NAME_globaldict ] ) ;
  ok = insert_hash( & systemdict , & key , & globaldict ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  oName(key) = ( & system_names[ NAME_shareddict ] ) ;
  ok = insert_hash( & systemdict , & key , & globaldict ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  oName(key) = &system_names[ NAME_languagelevel ] ;
  oInteger(inewobj) = 3 ;
  ok = insert_hash( & systemdict , & key , & inewobj ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  oName(key) = & system_names[NAME_m_op];
  oOp(obj) = & system_ops[NAME_m_op];
  ok = insert_hash( & systemdict, &key, &obj ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  oName(key) = & system_names[NAME_dup];
  oOp(obj) = & system_ops[NAME_dup];
  ok = insert_hash( & systemdict, &key, &obj ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  oName(key) = &system_names[NAME_false];
  ok = insert_hash( & systemdict , & key , & fnewobj ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  oName(key) = &system_names[NAME_true];
  ok = insert_hash( & systemdict , & key , & tnewobj ) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  theTags( obj ) = OSTRING | LITERAL | UNLIMITED ;
  theLen( obj ) = EQSTRING_LENGTH ;
  oString(obj) = pstored ;
  oName(key) = & system_names[NAME_equalsstring];
  ok = insert_hash( & systemdict , & key , & obj) ;
  HQASSERT(ok, "Dictionary setup insertion failed") ;

  /* now set up the dictionary stack */
  ok = push( & systemdict , & dictstack ) ;
  HQASSERT(ok, "Dictionary setup push failed") ;
  tmpo = oDict(systemdict) ;
  ++oInteger(tmpo[-2]) ;

  ok = push( & globaldict , & dictstack ) ;
  HQASSERT(ok, "Dictionary setup push failed") ;
  tmpo = oDict(globaldict) ;
  ++oInteger(tmpo[-2]) ;

  ok = push( &   userdict , & dictstack ) ;
  HQASSERT(ok, "Dictionary setup push failed") ;
  tmpo = oDict(userdict) ;
  ++oInteger(tmpo[-2]) ;

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create_fmt( &internaldict_root, mm_arena, mps_rank_exact(),
                            0, ps_scan, &internaldict, &internaldict + 1 )
       != MPS_RES_OK ) {
    finishLanguageLevelDicts();
    return FAILURE(FALSE) ;
  }

  return TRUE ;
}

/* finishDicts - finish the base PS dictionaries */
static void ps_dicts_finish(void)
{
  /* Make sure we don't use it after it's been finished. */
  theTags( internaldict ) = ONULL;
  mps_root_destroy( internaldict_root );
  finishLanguageLevelDicts();
}

IMPORT_INIT_C_GLOBALS( lanlevel )

void ps_dicts_C_globals(core_init_fns *fns)
{
  init_C_globals_dicts() ;
  init_C_globals_lanlevel() ;

  fns->swstart = ps_dicts_swstart ;
  fns->finish = ps_dicts_finish ;
}


/** Defines a name in a dictionary as its operator, if it has one.
     dict str num m_op -
 */
Bool make_operator_(ps_context_t *pscontext)
{
  OBJECT *top;
  int32 num;

  if ( theStackSize(operandstack) < 2 )
    return error_handler( STACKUNDERFLOW );
  top = theTop( operandstack ) ;
  if ( !object_get_integer(top, &num) )
    return FALSE;
  HQASSERT(num >= 0 && num < OPS_COUNTED , "p_op creating segv operator" );

  if ( system_ops[num].opcall != NULL ) {
    /* replace num with the operator and define it */
    theTags(* top ) = OOPERATOR | EXECUTABLE;
    oOp( *top ) = &system_ops[num];
    return put_(pscontext);
  } else {
    npop(3, &operandstack);
    return TRUE;
  }
}


/* ----------------------------------------------------------------------------
   function:            insert_hash(..)    author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           thed , thekey , theo .
   description:

  This procedure inserts the namecache value - then -  & object - theo - pair
  into the dictionary given by thedict.  Returns TRUE  if  correctly inserted,
  FALSE otherwise - namely if dictionary overflows.

---------------------------------------------------------------------------- */
OBJECT * PSmem_alloc_func(int32 size, void * dummy)
{
  UNUSED_PARAM(void*,dummy) ;

  return get_omemory(size) ;
}


Bool insert_hash(register OBJECT *thed,
                 register OBJECT *thekey,
                 OBJECT *theo)
{
  /* N.B.: assumes PS VM for non-PS VM (e.g. PDF call insert_hash_with_alloc directly) */
  return insert_hash_with_alloc(thed, thekey, theo, INSERT_HASH_NORMAL,
                                PSmem_alloc_func, NULL) ;
}

Bool insert_hash_even_if_readonly(register OBJECT *thed,
                                  register OBJECT *thekey,
                                  OBJECT *theo)
{
  /* N.B.: assumes PS VM for non-PS VM (e.g. PDF call insert_hash_with_alloc directly) */
  return insert_hash_with_alloc(thed, thekey, theo, INSERT_HASH_DICT_ACCESS,
                                PSmem_alloc_func, NULL) ;
}

Bool fast_insert_hash(register OBJECT *thed,
                      register OBJECT *thekey,
                      OBJECT *theo)
{
  /* N.B.: assumes PS VM for non-PS VM (e.g. PDF call insert_hash_with_alloc directly) */
  return insert_hash_with_alloc(thed, thekey, theo,
                                INSERT_HASH_NAMED|INSERT_HASH_DICT_ACCESS,
                                PSmem_alloc_func, NULL) ;
}

Bool fast_insert_hash_name(register OBJECT *thed, int32 namenum,
                           OBJECT *theo)
{
  OBJECT nameobj = OBJECT_NOTVM_NOTHING;

  HQASSERT(namenum >= 0 && namenum < NAMES_COUNTED,
           "Invalid system name index") ;

  object_store_name(&nameobj, namenum, LITERAL) ;

  /* N.B.: assumes PS VM for non-PS VM (e.g. PDF call insert_hash_with_alloc directly) */
  return insert_hash_with_alloc(thed, &nameobj, theo,
                                INSERT_HASH_NAMED|INSERT_HASH_DICT_ACCESS,
                                PSmem_alloc_func, NULL) ;
}


Bool CopyDictionary(OBJECT *src,
                    OBJECT *dst,
                    Bool (*doInsert)(OBJECT *, void *),
                    void *doData )
{
  int32 len1 ;
  OBJECT *thed ;
  OBJECT *thed1 , *thed2 ;
  DPAIR *dplist ;

  HQASSERT( oType(*src) == ODICTIONARY , "CopyDictionary from NON dict!" ) ;
  HQASSERT( oType(*dst) == ODICTIONARY , "CopyDictionary into NON dict!" ) ;

  thed1 = oDict(*src) ;
  thed2 = oDict(*dst) ;
  if ( (!oCanWrite(*thed2) && !object_access_override(thed2)) ||
       (!oCanRead(*thed1) && !object_access_override(thed1)) )
    return error_handler( INVALIDACCESS ) ;

  thed = src ;
  do {
    thed = oDict(*thed) ;
    len1 = (int32)theLen(* thed ) ;
    dplist = ( DPAIR * )( thed + 1 ) ;
    while ( len1 > 0 ) {
      if ( oType(theIKey(dplist)) != ONOTHING ) {
        --len1 ;
        if ( !doInsert || doInsert( & theIKey( dplist ) , doData ))
          if ( ! insert_hash(dst ,
                             & theIKey( dplist ) ,
                             & theIObject( dplist )))
            return FALSE ;
      }
      ++dplist ;
    }
    --thed ;
  } while ( oType(*thed) == ODICTIONARY ) ;
  return TRUE ;
}

/*
 * Set the system dictionary to point to the dictionary sysd. Used when
 * the language level is changed.
 */
void install_systemdict(OBJECT *sysd)
{
  Copy( &systemdict , sysd ) ;
}


/* Log stripped */
