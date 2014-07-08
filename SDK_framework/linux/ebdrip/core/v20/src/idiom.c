/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:idiom.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Language level 3 idioms.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "mmcompat.h"
#include "objects.h"
#include "monitor.h"
#include "namedef_.h"
#include "debugging.h"

#include "idiom.h"
#include "stacks.h"
#include "psvm.h"
#include "graphics.h" /* PATHFORALL for execops */
#include "miscops.h"
#include "execops.h"  /* setup_pending_exec */
#include "mps.h"
#include "gcscan.h"
#include "hqmemset.h"

#define IDIOM_INDEX_SIZE    0x100  /* Size of index: must be a power of 2. */
#define IDIOM_COMPAREDEPTH  10     /* Recursion depth limit (Adobe defined). */
#define PRIME_MULTIPLIER 13

typedef struct IDIOM {
  struct IDIOM *next ;       /* Singly linked list */
  uint32 hash ;              /* Template hash value */
  int32 sid ;                /* Savelevel at which defined */
  int32 uid ;                /* Savelevel at which undefined, same as sid
                              * means never undefined. If greater than sid,
                              * the index entry is not valid until the
                              * matching restore.
                              */
  OBJECT instance ;          /* PSVM is owned by the code in resource.pss  */
  OBJECT itemplate ;         /* The template procedure. */
  OBJECT isubstitute ;       /* Replace it with this when there's a match. */
  OBJECT icallback;          /* Call this callback, if any, on a substitution */
} IDIOM ;

static IDIOM **idiom_base = NULL ;

static uint32 idiom_hash ;
static int32  idiom_depth ;
static int32  idiom_maxdepth ;
static int32  idiom_counter ;
static int32  idiom_saved_hash;
static int32  idiom_saved_counter;

static mps_root_t idiom_root;


static Bool idiom_hit( OBJECT *o1 , OBJECT *o2 , uint32 depth , int32 *result , int32 *pushed ) ;
static Bool idiom_insert(corecontext_t *corecontext, OBJECT *theo , uint32 hash ) ;
static void idiom_remove(corecontext_t *corecontext, OBJECT *theo ) ;

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
static Bool debug_idiomrecognition = FALSE ;
#endif

static void init_C_globals_idiom(void)
{
  idiom_base = NULL ;
  idiom_hash = 0 ;
  idiom_depth = idiom_maxdepth = 0 ;
  idiom_counter = 0 ;
  idiom_saved_hash = 0 ;
  idiom_saved_counter = 0 ;
  idiom_root = NULL ;
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  debug_idiomrecognition = FALSE ;
#endif
}

/* idiom_scan_index - scanning function for the idiom index */
static mps_res_t MPS_CALL idiom_scan_index(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res = MPS_RES_OK;
  size_t i;

  UNUSED_PARAM( void *, p ); UNUSED_PARAM( size_t, s );
  /* map over the entire hash table */
  MPS_SCAN_BEGIN( ss )
    for ( i = 0 ; i < IDIOM_INDEX_SIZE ; i++ ) {
      IDIOM *current = idiom_base[ i ];

      while ( current ) {
        PS_MARK_BLOCK( ss, current, sizeof(IDIOM) );
        MPS_SCAN_CALL( res = ps_scan_field( ss, & current->instance ));
        if ( res != MPS_RES_OK ) return res;
        MPS_SCAN_CALL( res = ps_scan_field( ss, & current->itemplate ));
        if ( res != MPS_RES_OK ) return res;
        MPS_SCAN_CALL( res = ps_scan_field( ss, & current->isubstitute ));
        if ( res != MPS_RES_OK ) return res;
        MPS_SCAN_CALL( res = ps_scan_field( ss, & current->icallback ));
        if ( res != MPS_RES_OK ) return res;
        current = current->next;
      }
    }
  MPS_SCAN_END( ss );
  return res;
}


/* Initialise the idiom index. */
static Bool idiom_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  HQASSERT( idiom_base == NULL , "Shouldn't call idiom_init twice!" ) ;

  idiom_base = mm_alloc_static(IDIOM_INDEX_SIZE * sizeof(IDIOM *)) ;
  if ( idiom_base == NULL )
    return FALSE ;

  HqMemSetPtr(idiom_base, NULL, IDIOM_INDEX_SIZE) ;

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &idiom_root, mm_arena, mps_rank_exact(),
                        0, idiom_scan_index, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}


/* Finish the idiom index. */

static void idiom_finish( void )
{
  mps_root_destroy( idiom_root );
}

void idiom_C_globals(core_init_fns *fns)
{
  init_C_globals_idiom() ;

  fns->swstart = idiom_swstart ;
  fns->finish = idiom_finish ;
}


/* Reset the variables used in calculating the hash value. */

void idiom_resethash( void )
{
  idiom_hash = 0 ;
  idiom_depth = 0 ;
  idiom_maxdepth = 0 ;
  idiom_counter = 0 ;
}

/* Increment the hash calculation depth. */

void idiom_inchashdepth( void )
{
  idiom_depth++ ;
  if ( idiom_maxdepth < idiom_depth )
    idiom_maxdepth = idiom_depth ;
  /* remember current settings of the hash in case we need to restore them on pop */
  idiom_saved_hash = idiom_hash;
  idiom_saved_counter = idiom_counter;
}

/* Decrement the hash calculation depth. */

void idiom_dechashdepth(Bool fConstantArray)
{
  idiom_depth-- ;

  if (fConstantArray) {
    idiom_hash = idiom_saved_hash;
    idiom_counter = idiom_saved_counter;

    idiom_hash += OARRAY * idiom_counter;
    idiom_counter += PRIME_MULTIPLIER;
  }
}

/* Recursively calculate the hash value for the given procedure. */

void idiom_calchash( OBJECT *proc )
{
  int32 len ;
  OBJECT *alist ;
  Bool fConstantArray;

  HQASSERT( proc , "proc NULL in idiom_calchash." ) ;
  HQASSERT(oType(*proc) == OARRAY || oType(*proc) == OPACKEDARRAY ,
            "proc should be an array in idiom_calchash." ) ;

  len = theLen(*proc) ;

  if ( len == 0 )
    return ;

  /* Stops early if max depth has been exceeded: nice and efficient if
   * idiom_calchash has been called from defineidiom_ but more importantly
   * if called from bind_internal it prevents a possible infinite loop.
   */

  idiom_inchashdepth() ;

  if ( idiom_depth > IDIOM_COMPAREDEPTH ) {
    idiom_dechashdepth (TRUE /* or FALSE, it doesn't matter */) ;
    return ;
  }

  alist = oArray(*proc) + ( len - 1 ) ;

  fConstantArray = TRUE;
  while ( len-- > 0 ) {
    switch (oType(*alist)) {
    case OARRAY:
    case OPACKEDARRAY:
      fConstantArray = FALSE;
      idiom_calchash( alist ) ;
      break ;

    case OINTEGER:
    case OREAL:
    case OSTRING:
      idiom_updatehash( alist ) ;
      break;

    default:
      fConstantArray = FALSE;
      idiom_updatehash( alist ) ;
      break ;
    }
    --alist ;
  }

  idiom_dechashdepth (fConstantArray) ;
}

/* Update an idiom hash with a value derived from the given object. Factored
 * out so it can be called from bind_internal and idiom_calchash both.
 */

void idiom_updatehash( OBJECT *theo )
{
  int32 type_enumeration;
  HQASSERT( theo , "theo NULL in idiom_updatehash." ) ;

  switch (oType(*theo)) {
  case ONAME:
    if ( ! oExecutable(*theo) ) {
      /* The /...type names are special: we hash on the type they imply
         rather than the fact that they are literal names. If someone
         does use such a nem in the bound procedure, that's fine, it
         will still hash correctly, just differently, but it allows us
         to match wildcards for values of the implied type in the
         candidate procedure as well */
      switch ( oNameNumber(*theo) ) {
      case NAME_integertype:
      case NAME_realtype:
        /* treat all numbers the same, whether integer or real,
           for the purposes of hashing */
        type_enumeration = OREAL; break;
      case NAME_stringtype:
        type_enumeration = OSTRING; break;
      case NAME_dicttype:
        type_enumeration = ODICTIONARY; break;
      case NAME_booleantype:
        /* note: count booleans as names because true and false, which we want to
           match against, are names not booleans */
        type_enumeration = ONAME; break;
      case NAME_filetype:
        type_enumeration = OFILE; break;
      case NAME_gstatetype:
        type_enumeration = OGSTATE; break;
      case NAME_arraytype:
        /* arrays match complete arrays of an arbitrary number of integer,
           real and string constants only */
        type_enumeration = OARRAY; break;
      case NAME_nametype:
      default:
        type_enumeration = ONAME; break;
      }
    } else {
      type_enumeration = ONAME;
    }
    break;
  case OINTEGER:
    /* treat all numbers the same, whether integer or real,
       for the purposes of hashing */
    type_enumeration = OREAL;
    break;
  case OBOOLEAN:
    /* treat booleans as names because true and false, which we want to match
       against, are names not booleans, but here we've got the unusual case of a true
       boolean which we also want to match */
    type_enumeration = ONAME;
    break;
  case OPACKEDARRAY:
    /* treat packed array the same as array for hashing */
    type_enumeration = OARRAY;
    break;
  default:
    type_enumeration = oType(*theo);
    break;
  }

  idiom_hash += type_enumeration * idiom_counter;
  idiom_counter += PRIME_MULTIPLIER;
}

/* Determine whether a hash hit represents a genuine match or not. Returns
 * true if the test completed without error. On success, the value of *result
 * is set to TRUE if a match was found, FALSE otherwise.
 * *ppushed is set to the number of objects pushed on the stack in response to
 * wildcard matching, but if this is NULL, wildcard matching is suppressed.
 */

static Bool idiom_hit( OBJECT *ocandidate , OBJECT *otemplate , uint32 depth ,
                       int32 *result , int32 *ppushed)
{
  int32 i ;
  int32 len ;
  int32 pushed;
  Bool match = TRUE ;

  HQASSERT( ocandidate != NULL, "ocandidate NULL in idiom_hit." ) ;
  HQASSERT( otemplate != NULL , "otemplate NULL in idiom_hit." ) ;
  HQASSERT( result != NULL, "result is null in idiom_hit.");

  len = theLen(*ocandidate) ;
  pushed = 0;

  if ( ++depth > IDIOM_COMPAREDEPTH ) {
    HQFAIL( "Exceeded max comparison depth, but idiom_replace should have caught this." ) ;
    *result = FALSE ;
    return TRUE ;
  }

  HQASSERT((oType(*ocandidate) == OARRAY ) ||
           (oType(*ocandidate) == OPACKEDARRAY ) ,
           "Non-array 1st parameter in idiom_hit." ) ;

  if ( (oType(*otemplate) != OARRAY && oType(*otemplate) != OPACKEDARRAY) ||
       len != theLen(*otemplate) ) {
    *result = FALSE ;
    return TRUE ;
  }

  for ( i = 0 ; ( i < len ) && match ; i++ ) {
    OBJECT *theocandidate = & oArray(*ocandidate)[ i ] ;
    OBJECT *theotemplate = & oArray(*otemplate)[ i ] ;
    Bool wildcard_match = FALSE;

    /* Harlequin extension: if the template entry is a literal name of the /...type
       form, then we match against values of that (and similar) type, and we also
       push the value so matched on the operand stack. If we fail to match we get
       rid of those pushed values, of course. In order not to upset red book
       definition of idiom recognition, if we fail to match on the wildcard,
       compare the name directly as normal

       Ideally we would like an incatation that checks for arbitrary sequences of
       numbers as well. Note the length test above would then be an issue, as well as
       the hashing algorithm */

    if ( ppushed != NULL &&
         oType(*theotemplate) == ONAME &&
         ! oExecutable(*theotemplate) ) {
      switch ( oNameNumber(*theotemplate) ) {
      case NAME_integertype:
        wildcard_match = oType(*theocandidate) == OINTEGER;
        break;
      case NAME_realtype:
        wildcard_match = (oType(*theocandidate) == OINTEGER ||
                          oType(*theocandidate) == OREAL);
        break;
      case NAME_stringtype:
        wildcard_match = oType(*theocandidate) == OSTRING;
        break;
      case NAME_dicttype:
        wildcard_match = oType(*theocandidate) == ODICTIONARY;
        break;
      case NAME_booleantype:
        /* note: true and false are names, not boolean constants. Therefore we match
           these names as well. The value that gets pushed will be a name, not the
           boolean constant. Allow a boolean as well, since it is just possible that
           someone can poke a procedure or do a cvx where there is a genuine boolean
           involved */
        wildcard_match = (oType(*theocandidate) == OBOOLEAN ||
                          (oType(*theocandidate) == ONAME &&
                           (oNameNumber(*theocandidate) == NAME_true ||
                            oNameNumber(*theocandidate) == NAME_false)));
        break;
      case NAME_filetype:
        wildcard_match = oType(*theocandidate) == OFILE;
        break;
      case NAME_gstatetype:
        wildcard_match = oType(*theocandidate) == OGSTATE;
        break;
      case NAME_nametype:
        wildcard_match = oType(*theocandidate) == ONAME;
        break;
      case NAME_arraytype:
        wildcard_match = (oType(*theocandidate) == OARRAY ||
                          oType(*theocandidate) == OPACKEDARRAY);
        break;
      default:
        break; /* other names leave wildcard_match false */
      }
    }

    if (wildcard_match) {
      if (! push ( theocandidate, & operandstack )) {
        if (pushed > 0)
          npop (pushed, & operandstack);
        return FALSE;
      }
      pushed++;
    } else {
      if ( oType(*theocandidate) == OARRAY ||
           oType(*theocandidate) == OPACKEDARRAY )
      {
        int32 more_pushed = 0;

        if ( ! idiom_hit( theocandidate , theotemplate , depth , & match ,
                          ppushed == NULL ? NULL : & more_pushed ))
        {
          HQASSERT (more_pushed == 0, "more_pushed not zero, even though idiom_hit failed");
          if (pushed > 0)
            npop (pushed, & operandstack);
          return FALSE ;
        }

        pushed += more_pushed;
      }
      else {
        /* As the spec defines, there's a match if 'eq' returns true. */
        if ( ! push2(theocandidate, theotemplate, & operandstack) ||
             ! eq_(get_core_context_interp()->pscontext))
        {
          if (pushed > 0)
            npop(pushed, & operandstack);
          return FALSE ;
        }

        /* Boolean result should be on stack */
        if ( theStackSize( operandstack ) < 0 )
          return  error_handler( STACKUNDERFLOW ) ;
        theocandidate = theTop(operandstack) ;
        if (oType(*theocandidate) != OBOOLEAN )
          return error_handler( TYPECHECK ) ;
        match = oBool(*theocandidate) ;
        pop( & operandstack ) ;
      }
    }
  }

  if (! match && pushed > 0) {
    npop ( pushed, & operandstack );
    pushed = 0;
  }

  *result = match ;

  if ( ppushed != NULL )
    * ppushed = pushed;

  return TRUE ;
}

static Bool idiom_insert(corecontext_t *corecontext, OBJECT *theo , uint32 hash )
{
  OBJECT *olist ;
  IDIOM **baseline = & idiom_base[ ( hash & ( IDIOM_INDEX_SIZE - 1 )) ] ;
  IDIOM *newidiom ;

  HQASSERT( theo , "theo NULL in idiom_insert." ) ;

  if (oType(*theo) != OARRAY && oType(*theo) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  newidiom = ( IDIOM * )get_smemory(sizeof(IDIOM)) ;
  if ( ! newidiom )
    return error_handler( VMERROR ) ;

  newidiom->next = *baseline ;
  newidiom->hash = hash ;
  /* Global instances defined inside a job are not purged until the
   * server loop restore.
   */
  if ( corecontext->glallocmode && NOTINEXITSERVER(corecontext))
    newidiom->sid = 2 * SAVELEVELINC ;
  else
    newidiom->sid = corecontext->savelevel ;
  newidiom->uid = newidiom->sid ;

  Copy(object_slot_notvm(&newidiom->instance), theo) ;

  olist = oArray(*theo) ;

  Copy(object_slot_notvm(&newidiom->itemplate), &olist[0]) ;
  Copy(object_slot_notvm(&newidiom->isubstitute), &olist[1]) ;

  /* Harlequin extension: if there is a third element, and it is a procedure, this is
     used as a callback to do the substitution instead of simply substituting element
     1 for the matched procedure. Note that if we use the further extension of
     wildcard matches, there must be a callback */
  newidiom->icallback = onull ; /* Struct copy to set slot properties */
  if (theLen(*theo) > 2 &&
      (oType(olist[2]) == OARRAY || oType(olist[2]) == OPACKEDARRAY) &&
      oExecutable(olist[2]) )
    Copy( & newidiom->icallback , & olist[ 2 ] ) ;

  *baseline = newidiom ;

  if ( ! corecontext->glallocmode ) {
    theILastIdiomChange(workingsave) =
      CAST_SIGNED_TO_INT16(corecontext->savelevel) ;

    HQTRACE( debug_idiomrecognition ,
             ("Idiom index change noted at savelevel %d.",
              corecontext->savelevel)) ;
  }

  return TRUE ;
}

/* Called from bind_, this is the routine which finally decides
 * whether a match for the given candidate exists, and whether it
 * has a suitable replacement associated with it. If so it pops
 * the candidate and pushes the replacement, else it does nothing.
 */

Bool idiom_replace( OBJECT *theo )
{
  IDIOM *current = idiom_base[ ( idiom_hash & ( IDIOM_INDEX_SIZE - 1 )) ] ;

  if ( idiom_maxdepth <= IDIOM_COMPAREDEPTH ) {
    while ( current ) {
      int32 result ;

      if (( current->hash == idiom_hash ) &&
          ( ! oGlobalValue(*theo) ||
              oGlobalValue(current->isubstitute )) &&
          ( current->sid == current->uid ))
      {
        int32 pushed = 0;

        if ( ! idiom_hit( theo , & current->itemplate , 0 , & result ,
                          oType(current->icallback) == ONULL ? NULL : & pushed ))
        {
          HQASSERT (pushed == 0, "pushed not zero even though idiom_hit failed");
          return FALSE ;
        }

        if ( result ) {
          if ( oType(current->icallback) == ONULL ) {
            HQASSERT (pushed == 0, "wildcard matches present even though callback is null");
            pop( & operandstack ) ;
            return push( & current->isubstitute , & operandstack ) ;
          } else {
            /* original parameters... n_parameters template substitute =>
               real-substitute In principle, n_parameters is known, but this allows
               for an arbitrary number in the future */
            oInteger(inewobj) = pushed;
            if ( ! push3(&inewobj, &current->itemplate, &current->isubstitute,
                         &operandstack) ) {
              npop(pushed, & operandstack);
              return FALSE;
            }
            return setup_pending_exec( & current->icallback , TRUE /* exec immediately */ );
          }
        } else {
          HQASSERT (pushed == 0, "pushed not zero, even though no match");
        }
      }

      current = current->next ;
    }
  }

  /* No match, but that's not an error. */

  return TRUE ;
}

static void idiom_remove(corecontext_t *corecontext, OBJECT * theo )
{
  int32 i ;

  /* This is a brain-dead linear search, but I can't use the hashing here:
   * the match must be exact. It's not good enough to remove the first
   * instance whose template matches the template of the idiom being
   * removed. This is OK provided we assume IdiomSets aren't often
   * explicitly undefined in real life.
   */

  for ( i = 0 ; i < IDIOM_INDEX_SIZE ; i++ ) {
    IDIOM **current = & idiom_base[ i ] ;

    while ( *current ) {
      if ( oArray((*current)->instance) == oArray(*theo) ) {
        /* Discard the index entry if it was created at this savelevel, else
         * just invalidate it until the undefinition is restored away.
         */
        if (( *current )->sid == corecontext->savelevel )
          *current = (*current)->next ;
        else
          ( *current )->uid = corecontext->savelevel ;

        if ( ! corecontext->glallocmode ) {
          theILastIdiomChange(workingsave) =
            CAST_SIGNED_TO_INT16(corecontext->savelevel) ;

          HQTRACE( debug_idiomrecognition ,
                   ( "Idiom index change noted at savelevel %d." ,
                     corecontext->savelevel)) ;
        }

        return ;
      }

      current = & (*current)->next ;
    }
  }

  HQFAIL( "idiom_remove failed." ) ;
}

/* If the idiom index was last changed at a save level now being restored,
 * walk the entire index removing stale entries and reinstating entries
 * undefined within the outgoing save(s). So we have an early exit when
 * there's been no idiom index activity, which should be by far the most
 * common case. Always walk the whole index during the server loop restore
 * (to catch job-scoped global instances) and during reboots.
 */

void idiom_purge( int32 slevel )
{
  if (( theILastIdiomChange( workingsave ) > slevel ) ||
       ( NUMBERSAVES( slevel ) <= 1 )) {
    int32 i ;

    HQTRACE( debug_idiomrecognition ,
             ( "Purging idiom index changes in restoring from savelevel %d to %d." ,
               get_core_context_interp()->savelevel , slevel )) ;

    for ( i = 0 ; i < IDIOM_INDEX_SIZE ; i++ ) {
      IDIOM **current = & idiom_base[ i ] ;

      while ( *current ) {
        if (( *current )->sid > slevel ) {
          HQTRACE( debug_idiomrecognition ,
                   ( "Purging idiom with sid %d, hash value %x." ,
                     ( *current )->sid , ( *current )->hash )) ;
          *current = ( *current )->next ;
        }
        else {
          if (( *current )->uid > slevel )
            ( *current )->uid = ( *current )->sid ;
          current = & ( *current )->next ;
        }
      }
    }
  }
}

/* The defineidiom internaldict operator is called from IdiomSetDefineResource
 * with each idiom in the set in turn. The parameters are the array containing
 * the idiom template and its replacement and the name of the idiom. If we wanted
 * to we could call this directly without defining a resource in order to hide
 * some internal IR chicanery.
 */

Bool defineidiom_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  OBJECT *proc ;
  OBJECT *thek ;

  if ( theStackSize( operandstack ) < 1 )
    return  error_handler( STACKUNDERFLOW ) ;

  theo = theTop(operandstack) ;
  thek = &theo[-1] ;
  if ( !fastStackAccess(operandstack) )
    thek = stackindex( 1 , & operandstack ) ;

  if ( oType(*theo) != OARRAY && oType(*theo) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  /* Don't insist the key is a name since Adobe don't seem to. */

  if ( theLen(*theo) < 2 )
    return error_handler( RANGECHECK ) ;

  proc = oArray(*theo) ;

  if ( oType(*proc) != OARRAY && oType(*proc) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  idiom_resethash() ;
  idiom_calchash( proc ) ;

  if ( idiom_maxdepth <= IDIOM_COMPAREDEPTH ) {

#if defined( DEBUG_BUILD )
    if ( debug_idiomrecognition ) {
      monitorf(( uint8 * ) "defineidiom( " ) ;
      debug_print_object(thek) ;
      monitorf(( uint8 * ) ", %d , %x )\n" , idiom_maxdepth , idiom_hash ) ;
    }
#endif

    if ( ! idiom_insert(ps_core_context(pscontext), theo , idiom_hash ))
      return FALSE ;
  }

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

Bool undefineidiom_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  OBJECT *proc ;
  OBJECT *thek ;

  if ( theStackSize( operandstack ) < 1 )
    return  error_handler( STACKUNDERFLOW ) ;

  theo = theTop(operandstack) ;
  thek = &theo[-1] ;
  if ( !fastStackAccess(operandstack) )
    thek = stackindex( 1 , & operandstack ) ;

  if ( oType(*theo) != OARRAY && oType(*theo) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  /* Don't insist the key is a name since Adobe don't seem to. */

  if ( theLen(*theo) < 2 )
    return error_handler( RANGECHECK ) ;

  proc = oArray(*theo) ;

  if ( oType(*proc) != OARRAY && oType(*proc) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

#if defined( DEBUG_BUILD )
  if ( debug_idiomrecognition ) {
    monitorf(( uint8 * ) "undefineidiom( " ) ;
    debug_print_object(thek) ;
    monitorf(( uint8 * ) ")\n" ) ;
  }
#endif

  idiom_remove(ps_core_context(pscontext), theo ) ;

  npop( 2 , & operandstack ) ;

  return TRUE ;
}


/* Log stripped */
