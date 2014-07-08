/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:dicthash.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2011, 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Dictionary insertion, extraction and deletion functions.
 */

#include "core.h"
#include "mm.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"

#include "objimpl.h"


#define DICT_MAX_LEN(_dictarr) theLen((_dictarr)[-1])

#define HASHKEY(key, len) (((key) >> 2) % (size_t)(len))


int32 getDictMaxLength(OBJECT *dict)
{
  if ( oType(dict[-1]) != ONOTHING )
    dict = oDict(dict[-1]);
  return DICT_MAX_LEN(dict);
}


static Bool extend_dict(OBJECT *pnewdict, OBJECT *olddict,
                        corecontext_t *corecontext,
                        OBJECT *(*alloc_func)(int32 size, void * params),
                        void *alloc_params)
{
  Bool currglmode, glmode;
  OBJECT *dict;
  size_t newentries, newsize;
  OBJECT newdict = OBJECT_NOTVM_NOTHING;
  DPAIR *newarray, *newend, *oldentry, *oldend;

  glmode = oGlobalValue(*olddict);
  newentries = min(2 * DICT_MAX_LEN(olddict) + 1, MAXPSDICT);
  newsize = min(DICT_ADJUSTED_SLOTS(newentries), MAXPSDICT);

  /* allocate dictionary memory from the required memory source */
  currglmode = setglallocmode(corecontext, glmode);
  dict = (*alloc_func)((int32)(2 * newsize + 3), alloc_params);
  setglallocmode(corecontext, currglmode);
  if ( dict == NULL )
    return error_handler(VMERROR);

  init_dictionary(&newdict, (int32)newentries,
                  (uint8)(theTags(*olddict) & ACCEMASK),
                  dict,
                  (int8)((theMark(*olddict) & VMMASK)
                         | GLMODE_SAVELEVEL(glmode, corecontext) | glmode));

  /* Rehash all the entries into the new dict */
  newarray = (DPAIR *)&oDict(newdict)[1]; newend = newarray + newsize;
  HQASSERT(DICT_ALLOC_LEN(oDict(newdict)) == newsize,
           "Dict size calculation mismatch");
  oldentry = (DPAIR *)&olddict[1]; oldend = oldentry + DICT_ALLOC_LEN(olddict);
  while ( oldentry < oldend ) {
    if ( oType(oldentry->key) != ONOTHING ) {
      DPAIR *startslot =
        &newarray[HASHKEY(OBJECT_GET_D1(oldentry->key),newsize)];
      DPAIR *slot = startslot;
      do {
        if ( oType(slot->key) == ONOTHING ) { /* empty */
          Copy(&slot->key, &oldentry->key); Copy(&slot->obj, &oldentry->obj);
          /* If key is a uniquely-defined name, update ptr to dict slot. */
          if ( oType(slot->key) == ONAME && oName(slot->key)->dictobj != NULL )
            oName(slot->key)->dictval = &slot->obj;
          break;
        }
        ++slot;
        if ( slot == newend ) /* wrap around */
          slot = &newarray[0];
        HQASSERT(slot != startslot, "Rehashed entry did not fit");
        /* Should always fit, because each old slot corresponds to at
           least two new ones. */
      } while ( slot != startslot );
    }
    ++oldentry;
  }
  theLen(*oDict(newdict)) = theLen(*olddict);

  Copy( pnewdict, &newdict );
  return TRUE;
}


/* ----------------------------------------------------------------------------
   function:            insert_hash(..)    author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   description:

  This procedure inserts the key (thekey) & object (theo) pair into the
  dictionary given by based.  Returns TRUE if correctly inserted, FALSE
  otherwise.

---------------------------------------------------------------------------- */
static OBJECT *fast_hash_thed = NULL ;

Bool insert_hash_with_alloc(register OBJECT *based,
                            const OBJECT *thekey,
                            const OBJECT *theo,
                            uint32 flags,
                            OBJECT *(*alloc_func)(int32 size, void * params),
                            void *alloc_params)
{
  register int32 thetype ;
  register uintptr_t key ;
  register DPAIR *anindex , *startindex ;
  Bool glmode;
  int32 maxsize ;
  OBJECT *base_dict;
  OBJECT *thed ;
  DPAIR *dplist , *endindex ;
  DPAIR *emptyentry ;
  OBJECT newo = OBJECT_NOTVM_NOTHING ;
  OBJECT *(*extract_func)(const OBJECT *dict, const OBJECT *key) = extract_hash ;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;
  HQASSERT(based, "Nowhere to insert object") ;
  HQASSERT(thekey, "No key to insert object into dictionary") ;
  HQASSERT(theo, "No object to insert into dictionary") ;
  HQASSERT(oType(*based) == ODICTIONARY , "insert_hash into NON dict!" ) ;
  HQASSERT(alloc_func, "No allocator function");

  thetype = oType(*thekey) ;
  HQASSERT((flags & INSERT_HASH_NAMED) == 0 || thetype == ONAME,
           "Key must be ONAME for insertion") ;

  thed = base_dict = oDict(*based);
  glmode = oGlobalValue(*thed) ;
  maxsize = DICT_ALLOC_LEN(thed);

  switch ( thetype ) {
  case ONULL :
    return error_handler( TYPECHECK ) ;

  case OSTRING :
    if ( !oCanRead(*thekey) &&
         (flags & INSERT_HASH_KEY_ACCESS) == 0 &&
         !object_access_override(thekey) )
      return error_handler( INVALIDACCESS ) ;
    if ( NULL == (oName(newo) =
                  cachename(oString(*thekey), theLen(*thekey))))
      return FALSE ;
    theTags( newo ) = CAST_UNSIGNED_TO_UINT8(oExec(*thekey) | ONAME) ;
    thetype = ONAME ;
    thekey = ( & newo ) ;
    /*@fallthrough@*/
  case ONAME:
    extract_func = fast_extract_hash ;
    break;
  }

  /* Check OBJECTS for illegal LOCAL --> GLOBAL */
  if ( glmode ) {
    if ( illegalLocalIntoGlobal(thekey, corecontext) ||
         illegalLocalIntoGlobal(theo, corecontext) )
      return error_handler( INVALIDACCESS ) ;
  }

  if ( !oCanWrite(*thed) && (flags & INSERT_HASH_DICT_ACCESS) == 0 ) {
    if ( ! object_access_override(thed) )
      return error_handler( INVALIDACCESS ) ;

    /* do not allow super_exec to re-define operators which are
     * also prevented from being shadowed.
     */
    if ( thetype == ONAME &&
         (theIOpClass(oName(*thekey)) & CANNOTSHADOWOP) != 0 )
      return error_handler(INVALIDACCESS);
  }

  emptyentry = NULL ;

  if ( maxsize == 0 && oType(thed[-1]) != ONOTHING ) {
    /* There's an extension, use it. */
    thed = oDict(thed[-1]); maxsize = DICT_ALLOC_LEN(thed);
  }
  if ( maxsize ) {
    dplist = ( DPAIR * )( thed + 1 ) ;
    endindex = dplist + maxsize ;
    /* Pick the slot to start from. */
    key = OBJECT_GET_D1(*thekey);
    startindex = anindex = &dplist[ HASHKEY(key, maxsize) ];
    do {
      if ( oType(theIKey( anindex )) == ONOTHING ) {
        if ( theLen( theIKey( anindex ))) {      /* really an empty slot */
          emptyentry = anindex ;
          break ;
        }
        /* @@@@ This could now just scan forward, instead of extract_func */
        if ( !(*extract_func)(based, thekey) ) { /* can reuse empty slot */
          emptyentry = anindex ;
          break ;
        }
      }
      else { /*  If key-value pair exists, replace it. */
        if ( (OBJECT_GET_D1( theIKey( anindex ))) == key )
          if ( oType( theIKey( anindex )) == thetype )
            switch ( thetype ) {
            case OARRAY :
            case OPACKEDARRAY :
            case OFILEOFFSET :  /* [65401 approval] unlikely, but correct */
              if ( theLen(*thekey) != theLen( theIKey( anindex )))
                break ;
            default:
              if ( SLOTISNOTSAVED(thed, corecontext) )
                if ( ! check_dsave(thed, corecontext) )
                  return FALSE ;

              thed = ( & theIObject( anindex )) ;
              Copy( thed , theo ) ;
              return TRUE ;
            }
      }

      ++anindex ;               /* Look at next location using linear scan. */
      if ( anindex >= endindex )
        anindex = dplist ;
    } while ( anindex != startindex ) ;
  } /* if (maxsize) */
  /* At this point, it must be a new entry. */

  if ( theLen(*thed)+1 > MAXPSDICT )
    return error_handler( DICTFULL );

  /* Decide if the dict needs to be extended (didn't find a slot, or
     would get too full). */
  if ( !emptyentry || theLen(*thed) >= DICT_MAX_LEN(thed) ) {
    if ( !object_extend_dict(corecontext, base_dict, alloc_func == PSmem_alloc_func) ) {
      if ( !emptyentry )
        return error_handler( DICTFULL );
      /* Allow exceeding the load factor if can't extend. */
    } else {
      if ( SLOTISNOTSAVED( base_dict, corecontext ))
        if ( !check_dsave( base_dict, corecontext ))
          return FALSE;
      if ( !extend_dict( &base_dict[-1], thed, corecontext,
                         alloc_func, alloc_params ))
        return FALSE;
      DICT_ALLOC_LEN(base_dict) = 0; /* Drop the old dict array */
      theLen(*base_dict) = 0;
      /* insert OBJECT into extension */
      return insert_hash_with_alloc(based, thekey, theo, flags,
                                    alloc_func, alloc_params);
    }
  }

  /*  dict is not full, so insert and increment the current size. */
  if ( SLOTISNOTSAVED(thed, corecontext) )
    if ( ! check_dsave(thed, corecontext) )
      return FALSE ;

  if ( thetype == ONAME ) {
    NAMECACHE *nptr ;

    nptr = oName(*thekey) ;
    if ( nptr->dictobj == NULL ) {
      if ( nptr->dictval == NULL ) {
        nptr->dictobj = base_dict;
        nptr->dictval = ( & theIObject( emptyentry )) ;
        nptr->dictsid = namepurges ;
        namepurges = nptr ;
        if ( corecontext->savelevel <= SAVELEVELINC ) {
          if (theISaveLevel(nptr) < corecontext->savelevel )
            nptr->dictcpy = NULL ;
          else
            nptr->dictcpy = nptr->dictobj ;
        }
        else {
          nptr->dictcpy = NC_DICTCACHE_RESET ;
        }
      }
      else {
        if ( corecontext->savelevel <= SAVELEVELINC )
          nptr->dictcpy = NULL ;        /* Invalidate reset*/
      }
    }
    else if ( nptr->dictobj != base_dict ) {
      if ( corecontext->savelevel <= SAVELEVELINC )
        nptr->dictcpy = NULL ;  /* Invalidate reset*/
      nptr->dictobj = NULL ;
    }
  }

  ++theLen(*thed) ;
  thed = ( & theIKey( emptyentry )) ;
  Copy( thed , thekey ) ;
  thed = ( & theIObject( emptyentry )) ;
  Copy( thed , theo ) ;
  return TRUE ;
}


OBJECT *no_dict_extension(int32 size, void *params)
{
  UNUSED_PARAM(int32, size) ;
  UNUSED_PARAM(void *, params) ;

  HQFAIL("Insertion attempted on non-extensible dictionary") ;
  HQASSERT(params == NULL,
           "Non-extensible dictionary allocator params should be NULL") ;

  return NULL ;
}

/* ----------------------------------------------------------------------------
   function:            extract_hash(..)   author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           thed , thekey .
   description:

   This procedure extracts the object corresponding to then from the dictionary
   thed. Returns a pointer to the object if it is found, otherwise a NULL ptr.

---------------------------------------------------------------------------- */

OBJECT *extract_hash(const OBJECT *thed,
                     const OBJECT *thekey)
{
  int32 thetype;
  int32 maxsize ;
  OBJECT newo = OBJECT_NOTVM_NOTHING ;

  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;
  HQASSERT( oType(*thed) == ODICTIONARY , "extract_hash from NON dict!" ) ;

  thed = oDict( *thed ) ;
  maxsize = DICT_ALLOC_LEN(thed);

  thetype = oType(*thekey) ;

  switch ( thetype ) {
  case ONULL :
    ( void ) error_handler( TYPECHECK ) ;
    return NULL ;

  case OSTRING :
    /* convert OSTRING to ONAME */
    if ( ! oCanRead(*thekey) )
      if ( ! object_access_override(thekey) ) {
        ( void )  error_handler( INVALIDACCESS ) ;
        return NULL ;
      }
    if ( NULL == ( oName(newo) =
                   cachename(oString(*thekey) , theLen(*thekey))) )
      return NULL ;

    theTags( newo ) = CAST_UNSIGNED_TO_UINT8(oExec(*thekey) | ONAME) ;
    thekey = ( & newo ) ;
    thetype = ONAME ;

    /* fall through to ONAME */

  case ONAME:
    {
      /* Is thekey uniquely defined in this dictionary?
       * If so, then we can just obtain its value from the namecache struct and
       * avoid the lookup.
       * If it's not defined in any dictionary then we can also return early -
       * otherwise we actually have to do the extraction. */
      NAMECACHE *kname = oName(*thekey) ;
      HQASSERT( kname->dictobj == NULL || kname->dictval,
                "key is uniquely defined but dictval is NULL in extract_hash" ) ;
      if ( kname->dictobj == thed ) {
#if defined(ASSERT_BUILD)
        const OBJECT *arr = oType(thed[-1]) != ONOTHING ? oDict(thed[-1]) : thed;
        HQASSERT(arr != NULL && kname->dictval > arr
                 && kname->dictval <= arr + 1 + 2 * DICT_ALLOC_LEN(arr),
                 "dictval out of range");
#endif
        if ( oCanRead(*thed) || object_access_override(thed) )
          return kname->dictval ;
        ( void )error_handler( INVALIDACCESS ) ;
        return NULL ;
      }
      else if ( kname->dictval == NULL ) {
        return NULL ;
      }
    }
  }

  if ( maxsize == 0 && oType(thed[-1]) != ONOTHING ) {
    /* There's an extension, use it. */
    thed = oDict(thed[-1]); maxsize = DICT_ALLOC_LEN(thed);
  }
  if ( maxsize ) {
    DPAIR *endindex , *dplist , *anindex , *startindex ;
    uintptr_t key ;
    int32 tmptype ;

    dplist = ( DPAIR * )( thed + 1 ) ;
    endindex = dplist + maxsize ;

    /* Pick the slot to start from. */
    key = OBJECT_GET_D1( *thekey ) ;
    startindex = anindex = &dplist[ HASHKEY(key, maxsize) ];
    do {
      if ( (OBJECT_GET_D1( theIKey( anindex ))) == key ) {
        if (( tmptype = oType( theIKey( anindex ))) == thetype ) {
          switch ( thetype ) {
          case OARRAY :
          case OPACKEDARRAY :
          case OFILEOFFSET :  /* [65401 approval] unlikely, but correct */
            if ( theLen( theIKey( anindex )) != theLen(*thekey) )
              break ;
          default:              /* Only give access error if find OBJECT */
            if ( ! oCanRead(*thed))
              if ( ! object_access_override(thed) ) {
                ( void )error_handler( INVALIDACCESS ) ;
                return NULL ;
              }
            return ( & theIObject( anindex )) ;
          }
        }
        if (( tmptype == ONOTHING ) &&
            ( theLen( theIKey( anindex ))))
          return NULL ;
      }
      else
        if (( oType( theIKey( anindex )) == ONOTHING ) &&
            ( theLen( theIKey( anindex ))))
          return NULL ;

      /* Look at next location using linear scan. */
      ++anindex ;
      if ( anindex >= endindex )
        anindex = dplist ;
    } while ( anindex != startindex ) ;
  }
  return NULL ;
}


/* Little macros to help when fast_extract_hash seems to get called
 * gajillions of times - they boil away to nothing when NAMECACHE_STATS
 * isn't defined.
 */

#if defined( NAMECACHE_STATS )
uint32 feh_reclevel = 0 ;
#define NAMECACHE_STATCOUNT( _k , _c) \
  MACRO_START \
    if ( feh_reclevel == 0 ) {                                            \
      oName(*_k)->_c++ ;                                                  \
    }                                                                     \
  MACRO_END
#define NAMECACHE_STATRECURSE( _op ) feh_reclevel _op ;
#else
#define NAMECACHE_STATCOUNT( _k , _c ) EMPTY_STATEMENT()
#define NAMECACHE_STATRECURSE( _op ) EMPTY_STATEMENT()
#endif

OBJECT *fast_extract_hash(const OBJECT *thed,
                          const OBJECT *thekey)
{
  int32 maxsize ;
  NAMECACHE *kname ;

  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;
  HQASSERT( oType(*thed) == ODICTIONARY , "fast_extract_hash from NON dict!" ) ;
  HQASSERT( oType(*thekey) == ONAME , "fast_extract_hash not using name!" ) ;

  thed = oDict(*thed) ;
  maxsize = DICT_ALLOC_LEN(thed);

  /* Is thekey uniquely defined in this dictionary?
   * If so, then we can just obtain it's value from the namecache struct and
   * avoid the lookup.
   * If it's not defined in any dictionary then we can also return early -
   * otherwise we actually have to do the extraction (it may also be the case
   * that we've extended the dictionary so we can't quickly tell if its not
   * defined in this dictionary).
   */
  kname = oName(*thekey) ;
  HQASSERT( kname->dictobj == NULL || kname->dictval,
            "key is uniquely defined but dictval is NULL in fast_extract_hash" ) ;
  if ( kname->dictobj == thed ) {
#if defined(ASSERT_BUILD)
    const OBJECT *arr = oType(thed[-1]) != ONOTHING ? oDict(thed[-1]) : thed;
    HQASSERT(arr != NULL && kname->dictval > arr
             && kname->dictval <= arr + 1 + 2 * DICT_ALLOC_LEN(arr),
             "dictval out of range");
#endif
    NAMECACHE_STATCOUNT( thekey , hit_shallow ) ;
    return kname->dictval ;
  }
  else if ( kname->dictval == NULL ) {
    NAMECACHE_STATCOUNT( thekey , miss_shallow ) ;
    return NULL ;
  }


  if ( maxsize == 0 && oType(thed[-1]) != ONOTHING ) {
    /* There's an extension, use it. */
    thed = oDict(thed[-1]); maxsize = DICT_ALLOC_LEN(thed);
  }
  if ( maxsize ) {
    DPAIR *anindex , *startindex, *endindex , *dplist ;
    uintptr_t key ;
    int32 tmptype ;

    dplist = ( DPAIR * )( thed + 1 ) ;
    endindex = dplist + maxsize ;

    /* Pick the slot to start from. */
    key = OBJECT_GET_D1( *thekey );
    startindex = anindex = &dplist[ HASHKEY(key, maxsize) ];
    do {
      if ( (OBJECT_GET_D1( theIKey( anindex ))) == key ) {
        if (( tmptype = oType( theIKey( anindex ))) == ONAME ) {
          NAMECACHE_STATCOUNT( thekey , hit_deep ) ;
          return ( & theIObject( anindex )) ;
        }
        if (( tmptype == ONOTHING ) &&
            ( theLen( theIKey( anindex )))) {
          NAMECACHE_STATCOUNT( thekey , miss_deep ) ;
          return NULL ;
        }
      }
      else
        if (( oType( theIKey( anindex )) == ONOTHING ) &&
            ( theLen( theIKey( anindex )))) {
          NAMECACHE_STATCOUNT( thekey , miss_deep ) ;
          return NULL ;
        }

      /* Look at next location using linear scan. */
      ++anindex ;
      if ( anindex >= endindex )
        anindex = dplist ;
    } while ( anindex != startindex ) ;
  } /* if (maxsize) */
  NAMECACHE_STATCOUNT( thekey , miss_deep ) ;
  return NULL ;
}


OBJECT *fast_extract_hash_name(const OBJECT *thed,
                               register int32 namenum)
{
  OBJECT temp = OBJECT_NOTVM_NOTHING;

  HQASSERT(thed, "No dictionary to extract name from") ;
  HQASSERT(namenum >= 0 && namenum < NAMES_COUNTED,
           "Invalid system name index") ;

  oName(temp) = &system_names[namenum];
  theTags(temp) = ONAME | LITERAL ;
  return fast_extract_hash(thed, &temp);
}


Bool remove_hash(register OBJECT *thed,
                 const OBJECT *thekey,
                 register Bool check_access)
{
  register int32 thetype ;
  register uintptr_t key ;
  register DPAIR *anindex , *startindex ;
  OBJECT newo = OBJECT_NOTVM_NOTHING ;
  uint8 tags ;
  int32 maxsize ;
  DPAIR *dplist , *endindex ;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;
  HQASSERT( oType(*thed) == ODICTIONARY , "remove_hash from NON dict!" ) ;

  thed = oDict(*thed) ;
  maxsize = DICT_ALLOC_LEN(thed);

  if (( thetype = oType(*thekey)) != ONAME ) {
    switch ( thetype ) {
    case ONULL :
      return error_handler( TYPECHECK ) ;

    case OSTRING :
      if ( ! oCanRead(*thekey))
        if ( ! object_access_override(thekey) )
          return error_handler( INVALIDACCESS ) ;
      if ( NULL == (oName(newo) = cachename(oString(*thekey), theLen(*thekey)))  )
        return FALSE ;
      theTags( newo ) = CAST_UNSIGNED_TO_UINT8(oExec(*thekey) | ONAME) ;
      thetype = ONAME ;
      thekey = ( & newo ) ;
    }
  }

  tags = theTags(*thed) ;
  if ( check_access )
    if ( ! oCanWrite(*thed) )
      if ( ! object_access_override(thed) )
        return error_handler( INVALIDACCESS ) ;

  /** \todo @@@@ Could try dictobj shortcut */

  if ( maxsize == 0 && oType(thed[-1]) != ONOTHING ) {
    /* There's an extension, use it. */
    thed = oDict(thed[-1]); maxsize = DICT_ALLOC_LEN(thed);
  }
  if ( maxsize ) {
    dplist = ( DPAIR * )( thed + 1 ) ;
    endindex = dplist + maxsize ;
    /* Pick the slot to start from. */
    key = OBJECT_GET_D1( *thekey );
    startindex = anindex = &dplist[ HASHKEY(key, maxsize) ];
    do {
      if ( oType( theIKey( anindex )) == ONOTHING ) {
        if ( theLen( theIKey( anindex ))) /* really an empty slot */
          return TRUE ;
      }
      else      /*  If key_value pair exists, then replace it. */
        if ( (OBJECT_GET_D1( theIKey( anindex ))) == key )
          if ( oType( theIKey( anindex )) == thetype )
            switch ( thetype ) {
            case OARRAY :
            case OPACKEDARRAY :
            case OFILEOFFSET :  /* [65401 approval] unlikely, but correct */
              if ( theLen(*thekey) != theLen( theIKey( anindex )))
                break ;
            default:
              if ( SLOTISNOTSAVED(thed, corecontext) )
                if ( ! check_dsave(thed, corecontext))
                  return FALSE ;

              --theLen(*thed) ;

              if ( thetype == ONAME ) {
                NAMECACHE *nptr = oName(*thekey) ;
                HQASSERT( nptr->dictval != NULL , "Got this entry from a dict so reset must be non-NULL" ) ;
                if ( corecontext->savelevel <= SAVELEVELINC )
                  nptr->dictcpy = NULL ;        /* Invalidate reset*/
                nptr->dictobj = NULL ;
              }

              Copy(&theIKey( anindex ), &onothing) ;
              HQASSERT(theLen(theIKey( anindex )) == 0, "A  deleted entry should have a lengh of 0");
              /* Clear value slot so GC won't mark through it. */
              Copy(&theIObject( anindex ), &onull) ;
              return TRUE ;
            }

      ++anindex ;               /* Look at next location using linear scan. */
      if ( anindex >= endindex )
        anindex = dplist ;
    } while ( anindex != startindex ) ;
  } /* if (maxsize) */
  return TRUE ;
}


void init_C_globals_dicthash(void)
{
  fast_hash_thed = NULL ;
#if defined( NAMECACHE_STATS )
  feh_reclevel = 0;
#endif
}

/* Declare global init functions here to avoid header inclusion
   nightmare. */
IMPORT_INIT_C_GLOBALS( ncache )
Bool init_C_runtime_objects(void* context)
{
  UNUSED_PARAM(void*, context);
  init_C_globals_dicthash();
  init_C_globals_ncache();
  return TRUE;
}

/*
Log stripped */
