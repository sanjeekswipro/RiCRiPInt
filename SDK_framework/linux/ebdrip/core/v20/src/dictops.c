/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:dictops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS dictionary operators
 */

#include "core.h"
#include "pscontext.h"
#include "mm.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "namedef_.h"

#include "params.h"     /* for psvm.h */
#include "psvm.h"
#include "control.h"    /* for extern OBJECT *topDictStackObj */
#include "dicthash.h"
#include "stacks.h"
#include "stackops.h"
#include "bitblts.h"
#include "display.h"
#include "matrix.h"
#include "graphics.h"
#include "swmemory.h"
#include "miscops.h"
#include "dictops.h"
#include "plotops.h"
#include "gstate.h"

#include "spdetect.h"
#include "gschead.h"
#include "rcbcntrl.h"
#include "gscequiv.h"

/* ----------------------------------------------------------------------------
   function:            dict_()            author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 146.

---------------------------------------------------------------------------- */
Bool dict_(ps_context_t *pscontext)
{
  register int32 len ;
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  len = theStackSize( operandstack ) ;
  if ( EmptyStack( len ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , len ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  len = oInteger(*theo) ;

  if ( len < 0 )
    return error_handler( RANGECHECK ) ;
  if ( len > MAXPSDICT )
    return error_handler( LIMITCHECK ) ;

  return ps_dictionary(theo, len) ;
}

/* For the length_() operator see file 'shared2ops.c' */

/* ----------------------------------------------------------------------------
   function:            maxlength_()       author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 185.

---------------------------------------------------------------------------- */
Bool maxlength_(ps_context_t *pscontext)
{
  OBJECT *theo;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( !oCanRead(*oDict(*theo)) && !object_access_override(oDict(*theo)) )
    return error_handler( INVALIDACCESS ) ;

  theTags(*theo) = OINTEGER | LITERAL;
  oInteger(*theo) = getDictMaxLength(oDict(*theo));
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            begin_()           author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 124.

---------------------------------------------------------------------------- */
Bool begin_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  OBJECT *thed ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  thed = oDict(*theo) ;
  if ( ! oCanRead(*thed) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;

  if ( ! begininternal( theo ))
    return FALSE ;

  pop( & operandstack ) ;
  return TRUE ;
}

Bool begininternal( OBJECT *theo )
{
  OBJECT *tmpo ;

/*  Push the dictionary on the dictionary stack, from the operand stack. */
  if ( ! push( theo , & dictstack ))
    return FALSE ;

  topDictStackObj = theTop( dictstack ) ;
  tmpo = topDictStackObj ;
  tmpo = oDict(*tmpo) ;
  ++oInteger(tmpo[-2]) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            end_()             author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 149.

---------------------------------------------------------------------------- */
Bool end_(ps_context_t *pscontext)
{
  OBJECT *tmpo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( dictstack ) < ( theISaveLangLevel( workingsave ) == 1 ? 2 : 3 ))
    return error_handler( DICTSTACKUNDERFLOW ) ;

  tmpo = topDictStackObj ;
  tmpo = oDict(*tmpo) ;
  --oInteger(tmpo[-2]) ;
  pop( & dictstack ) ;
  topDictStackObj = theTop( dictstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            cleardictstack_()  author:              Andrew Cave
   creation date:       08-Jul-1991        last modification:   ##-###-####
   arguments:           none .
   description:

   See Level II reference manual page 373.

---------------------------------------------------------------------------- */
Bool cleardictstack_(ps_context_t *pscontext)
{
  register int32 i ;
  register OBJECT *tmpo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  i = theStackSize( dictstack ) - ( theISaveLangLevel( workingsave ) == 1 ? 1 : 2 )  ;
  while ((--i) >= 0 ) {
    tmpo = topDictStackObj ;
    tmpo = oDict(*tmpo) ;
    --oInteger(tmpo[-2]) ;
    pop( & dictstack ) ;
    topDictStackObj = theTop( dictstack ) ;
  }
  return TRUE ;
}


/* -------------------------------------------------------------------------- */
/* NameOverride

   Allow job definitions to be overridden. Unlike shadowop, nameoverrides are
   defined in sets and all definitions must match before any are redefined.
   Also unlike shadowop, nameoverrideop is compatible with other def_() actions.
   Unlike idiom detection which is done by bind, nameoverriding can be done to
   unbound functions.

   The nameoverride operator takes a patchdict parameter which, having been
   syntax checked, is added into the /nameoverrides dictionary in internaldict
   and sets the NAMEOVERRIDEOP bit for every name in the patchdict.

   When a name with the NAMEOVERRIDEOP is seen by def_(), it calls nameoverride()
   to inspect the value and potentially redefine it and/or other keys.

   eg a patch which only redefines a function under certain circumstances:
     <<
       /X [{5 mul} {4 mul 1 add} false]  % redefine proc X
       /Y [27 dup false]                 % but only if Y is 27
       /Z [{showpage} dup false]         % and Z is defined as showpage
     >> nameoverride
 */

typedef struct {
  OBJECT * nameoverrides ;
  OBJECT * patchdict ;
} nameoverridedicts ;

/* The NAMEOVERRIDEOP bit is set for any NAME mentioned in a patchdict. However,
   for types other than ONAME the best we can do is to have a bit per PS type.
   This is a slight optimisation to avoid doing an extract_hash() for every
   non-name key given to def.
 */
static int32 nameoverridetypes = 0 ;

/* Check that all patchdict definitions are legal before adding any of them.
   Called from nameoverride_().
 */
static Bool nameoverridewalk_check_patch(OBJECT * key, OBJECT * def, void * ctx)
{
  nameoverridedicts * dicts = ctx ;
  OBJECT * part, * list ;

  HQASSERT(dicts && dicts->nameoverrides && dicts->patchdict,
           "nameoverride_check_patch incorrectly called") ;

  /* Check patch definition isn't crazy */
  if (oType(*def) != OARRAY || theLen(*def) != 3)
    return error_handler(RANGECHECK) ;

  /* [<old> <new> bool] - we don't care about the old and new, and the final
     boolean doesn't have to start at false */
  part = oArray(*def) ;
  if (oType(*(part+2)) != OBOOLEAN)
    return error_handler(TYPECHECK) ;

  list = extract_hash(dicts->nameoverrides, key) ;
  if (list && theLen(*list) == MAXPSARRAY) {
    HQFAIL("Far too many nameoverrides") ;
    return error_handler(RANGECHECK) ;
  }

  return TRUE ;
}

/* Add the patchdict to the nameoverrides dict. Called from nameoverride_().
 */
static Bool nameoverridewalk_add_patch(OBJECT * key, OBJECT * def, void * ctx)
{
  nameoverridedicts * dicts = ctx ;
  OBJECT * oldlist, newlist = OBJECT_NOTVM_NOTHING, * to ;
  int32 old = 0 ;
  Bool ok ;

  HQASSERT(dicts && dicts->nameoverrides && dicts->patchdict,
           "nameoverride_add_patch incorrectly called") ;

  oldlist = fast_extract_hash(dicts->nameoverrides, key) ;
  if (oldlist)
    old = theLen(*oldlist) ;

  HQASSERT(oType(*def) == OARRAY && theLen(*def) == 3, "patchdict check failed") ;
  HQASSERT(old < MAXPSARRAY, "Far too many nameoverrides") ;

  /* todo - use nulls to pad array to avoid churn */
  if (!ps_array(&newlist, old + 1))
    return FALSE ;

  /* copy existing patchlist */
  if (old) {
    OBJECT * from = oArray(*oldlist) ;
    int i ;
    to = oArray(newlist) ;
    for (i = 0 ; i <= old ; ++i, ++from, ++to)
      Copy(to, from) ;
  }

  /* add new patchdict */
  to = oArray(newlist) + old ;  /* new element */
  Copy(to, dicts->patchdict) ;

  /* add/update nameoverrides dict, and set the NAMEOVERRIDEOP flag */
  if (oType(*key) == ONAME) {
    ok = fast_insert_hash(dicts->nameoverrides, key, &newlist) ;
    if (ok)
      theIOpClass(oName(*key)) |= NAMEOVERRIDEOP ;
  } else {
    ok = insert_hash(dicts->nameoverrides, key, &newlist) ;
    if (ok)
      nameoverridetypes |= 1<<oType(*key) ;
  }

  return ok ;
}

/* <<patchdict>> nameoverride -> |
 */
Bool nameoverride_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  OBJECT * nameoverrides, * patchdict = theTop(operandstack) ;
  Bool global;
  OBJECT newdict = OBJECT_NOTVM_NOTHING ;
  nameoverridedicts dicts = {0} ;
  Bool ok ;
  
  if (0 > theStackSize(operandstack))
    return error_handler(STACKUNDERFLOW) ;

  if (oType(*patchdict) != ODICTIONARY)
    return error_handler(TYPECHECK) ;

  global = setglallocmode(context, FALSE) ;

  nameoverrides = fast_extract_hash_name(&internaldict, NAME_nameoverrides) ;
  if (!nameoverrides) {
    /* Create the nameoverrides dictionary */
    if (!ps_dictionary(&newdict, 10) ||
        !fast_insert_hash_name(&internaldict, NAME_nameoverrides, &newdict)) {
      setglallocmode(context, global) ;
      return FALSE ;
    }
    nameoverrides = fast_extract_hash_name(&internaldict, NAME_nameoverrides) ;
  }

  /* Now we must add the patchdict's keys to the nameoverrides dict, and add
     the patchdict to the keys' array values. */
  dicts.nameoverrides = nameoverrides ;
  dicts.patchdict = patchdict ;
  ok = walk_dictionary(patchdict, nameoverridewalk_check_patch, &dicts) &&
       walk_dictionary(patchdict, nameoverridewalk_add_patch, &dicts) ;
	
  setglallocmode(context, global) ;

  if (ok)
  pop(&operandstack) ;

  return ok ;
}


/* Check all the patchdict flags. Called from nameoverride().
 */
static Bool nameoverridewalk_check_flags(OBJECT * key, OBJECT * def, void * ctx)
{
  OBJECT * part = oArray(*def) ;

  UNUSED_PARAM(OBJECT *, key) ;
  UNUSED_PARAM(void *, ctx) ;
  HQASSERT(oType(*def) == OARRAY && theLen(*def) == 3, "Bad patch definition") ;

  return oBool(*(part+2)) ;
}

typedef struct {
  OBJECT * key ;
  OBJECT ** value ;
} nameoverride_def ;

/* Apply all the patches. Called from nameoverride().
 */
static Bool nameoverridewalk_apply_patch(OBJECT * key, OBJECT * def, void * ctx)
{
  nameoverride_def * params = ctx ;
  OBJECT * part = oArray(*def) ;
  Bool same ;

  HQASSERT(oType(*def) == OARRAY && theLen(*def) == 3, "Bad patch definition") ;

  /* A patch definition may be purely informational - eg if we want to redefine
     /A only when /B has been validated, but without changing /B. We do a simple
     comparison here for equality - like eq_() */
  if (!o1_eq_o2(part+0, part+1, &same))
    return FALSE ;
  if (same)
    return TRUE ;

  /* If we're changing the thing the def_() was about to set, allow it to do
     the change. */
  if (!o1_eq_o2(key, params->key, &same))
    return FALSE ;
  if (same) {
    *params->value = part+1 ;
    return TRUE ;
  }

  /* Note: This is in effect a def, but should it actually be a store?
     Although it is possible to contrive an example that would defeat the def,
     it is also possible to contrive one that defeats the store too. We will
     define the patch action as 'def', and note that this may not be sufficient
     in all cases. A possible extension is to allow a proc as an optional 4th
     element of the patch definition, and call that if present to do the update.
     This would also be essential if wildcards were allowed in the template, to
     allow more idiom-like recognition and customisation of the replacement.
  */
  return insert_hash(topDictStackObj, key, part+1) ;
}

/* Check for a patchdict key of a particular type. Ends walk if it finds one.
 */
static Bool nameoverridewalk_unused_type(OBJECT * key, OBJECT * def, void * ctx)
{
  OBJECT * check = ctx ;

  UNUSED_PARAM(OBJECT *, def) ;

  return (oType(*key) != oType(*check)) ;  /* FALSE if type matches */
}

/* This key found not to be in nameoverrides dict, so remove the flag that
   brought us here, if possible.
 */
static void unsetnameoverride(OBJECT * dict, OBJECT * key)
{
  if (oType(*key) == ONAME)
    theIOpClass(oName(*key)) &= ~NAMEOVERRIDEOP ;
  else if (!dict)
    nameoverridetypes = 0 ;
  else if (!walk_dictionary(dict, nameoverridewalk_unused_type, key))
    nameoverridetypes &= 1 << oType(*key) ;
}

/* The def_() hook called if NAMEOVERRIDEOP flag is set for a key.
 */
static Bool nameoverride(OBJECT * key, OBJECT ** value)
{
  OBJECT * nameoverrides, * patchlist, * patchdict ;
  nameoverride_def params = {0} ;
  int32 len, i ;

  if ( !oCanRead(**value) )
    return TRUE ;             /* Can't inspect value so can't change it */

  nameoverrides = fast_extract_hash_name(&internaldict, NAME_nameoverrides) ;
  if (!nameoverrides) {
    /* No patches - there must have been a restore */
    unsetnameoverride(NULL, key) ;
    return TRUE ;
  }

  /* Allow nameoverride_apply_patch to change what def_() will be defining */
  params.key = key ;
  params.value = value ; /* It'll be the top of the operandstack */

  /* nameoverrides is a dictionary of keys, whose values are an array of
     patchdicts that the key is part of. If this key isn't in nameoverrides,
     we merely clear the NAMEOVERRIDEOP bit (as a patch must have been restored
     away), otherwise we do a deep comparison with its target and if it matches,
     set its flag... if all flags in a patchdict are true, then we redefine all
     the keys in the set.
   */
  patchlist = extract_hash(nameoverrides, key) ;
  if (!patchlist) {
    /* No patches for this key, but we were called because of the NAMEOVERRIDEOP
       bit, so clear that - there must have been a restore.
     */
    unsetnameoverride(nameoverrides, key) ;
    return TRUE ;
  }

  /* patchlist is an array of patchdicts. There ought to be at least one! */
  patchdict = oArray(*patchlist) ;
  len = theLen(*patchlist) ;
  if (oType(*patchlist) != OARRAY || len < 1) {
    HQFAIL("nameoverrides entry is not an array of patchdicts") ;
    return TRUE ;
  }

  /* For each patchdict in the patchlist, find the patch definition for this
     key and take the appropriate action. */
  for (i = 0 ; i < len ; ++i, ++patchdict) {
    OBJECT * definition, * part ;

    /* Once again, caution */
    if (oType(*patchdict) != ODICTIONARY) {
      HQFAIL("patchdict is not a dictionary") ;
      continue ; /* ignore this 'patchdict' but continue to the next */
    }

    /* For each patchdict, compare value with the target and if it matches, set
       the 'seen' flag - then check all flags in the patchdict and if all are
       set, redefine all the keys in the set.
     */
    definition = extract_hash(patchdict, key) ;
    if (!definition) {
      /* This ought not to have happened - the patchdict should only be in a
         nameoverrides array for a key if that key is mentioned in the
         patchdict. */
      HQFAIL("patchdict doesn't contain this key") ;
      continue ;
    }

    part = oArray(*definition) ;
    if (oType(*definition) != OARRAY || theLen(*definition) != 3 ||
        oType(*part) != OARRAY || oType(*(part+1)) != OARRAY ||
        oType(*(part+2)) != OBOOLEAN) {
      HQFAIL("patchdict definition is not of the correct form") ;
      continue ;
    }

    /* If we've seen this one before, nothing to do */
    if (oBool(*(part+2)))
      continue ;

    /* Does it match the target? */
    if (!compare_objects(*value, part+0))
      continue ;

    /* A match! Set the flag, then check all the flags in the patchdict */
    oBool(*(part+2)) = TRUE ;

    if (walk_dictionary(patchdict, nameoverridewalk_check_flags, NULL)) {
      /* All the flags are set! Define all the things! */
      if (!walk_dictionary(patchdict, nameoverridewalk_apply_patch, &params)) {
        /* Oh dear. Something has gone badly wrong, can't continue. */
        return FALSE ;
      }
      /* Patch successfully applied. It's unlikely that this key/value is part
         of more than one patch, but we'll continue anyway just in case */
    } /* FALSE return from monkey_check_flags simply means all the flags aren't
         set - there hasn't been an error. */
  } /* for each patchdict */

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            def_()             author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   macro references:    ext_name(..) .
   arguments:           none .
   description:

   See PostScript reference manual page 145.

---------------------------------------------------------------------------- */
Bool def_(ps_context_t *pscontext)
{
#define DEF_ACTION( Lthed , Lther ) MACRO_START \
  /* NOTHING */ \
MACRO_END

#define DEF_TEST( Lthed )  \
  ( Lthed == oDict(*topDictStackObj) /* Must be top dictionary... */ )

#define DEF_RESULT() MACRO_START \
  npop( 2 , & operandstack ) ; \
MACRO_END /* return TRUE after invocation of this is implicit */

  register OBJECT *o1;
  OBJECT * o2;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o1) == ONAME ) {
    int32 opclass = theIOpClass(oName(*o1));
    if ( opclass & (SHADOWOP | OTHERDEFOP) ) {
      if ( opclass & SHADOWOP ) {
        HQASSERT((opclass & OTHERDEFOP) == 0,
                 "SHADOWOP has prevented other def operations occuring") ;
        defshadowproc (o1, & o2);
      }
      else {
        if ( opclass & RECOMBINEDETECTOP && rcbn_enabled() )
          gsc_rcbequiv_handle_detectop(o1, o2);
        if ( (opclass & SEPARATIONDETECTOP) != 0 &&
             oDict(*topDictStackObj) != oDict(systemdict) )
          theICMYKDetected( workingsave ) = theICMYKDetect( workingsave ) = TRUE ;
		if ( (opclass & NAMEOVERRIDEOP) != 0 )
          if ( !nameoverride(o1, & o2) )  /* o2 may get updated */
            return FALSE ;
      }
    }
  } else {
    /* Is there a non-name key in a patchdict of this type? */
    if ( nameoverridetypes & (1<<oType(*o1)) )
      if ( !nameoverride(o1, & o2) )  /* o2 may get updated */
        return FALSE ;
  }

  NAMECACHE_DICT_INSERT_HASH(corecontext, o1, o2, DEF_TEST, DEF_ACTION, DEF_RESULT) ;

/*  Attempt to insert the name & object into the top dictionary. */
  if ( ! insert_hash( topDictStackObj , o1 , o2 ))
    return FALSE ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            load_()            author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 181.

---------------------------------------------------------------------------- */
Bool load_(ps_context_t *pscontext)
{
#define LOAD_ACTION( Lthed , Lther ) MACRO_START \
  /* NOTHING */ \
MACRO_END

#define LOAD_TEST( Lthed ) \
  ( Lthed && oInteger(Lthed[-2]) /* Any dictionary will do... */ )

#define LOAD_RESULT( Lres ) MACRO_START \
  theo = Lres ;           \
  Copy( key , theo ) ;    \
MACRO_END /* return TRUE after invocation of this is implicit */

  register int32 loop , dstacksize ;
  register OBJECT *key ;
  register OBJECT *theo ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  key = theTop( operandstack ) ;

  NAMECACHE_DICT_EXTRACT_HASH( key , LOAD_TEST , LOAD_ACTION , LOAD_RESULT ) ;

  error_clear_newerror_context(ps_core_context(pscontext)->error);
  dstacksize = theStackSize( dictstack ) ;
  for ( loop = 0 ; loop <= dstacksize ; ++loop ) {
    if ( (theo = extract_hash(stackindex(loop, &dictstack), key)) != NULL ) {
      Copy( key , theo ) ;
      return TRUE ;
    }
    if ( newerror )
      return FALSE ;
  }

  return errorinfo_error_handler(UNDEFINED, &onull, key) ;
}

/* ----------------------------------------------------------------------------
   function:            store_()           author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 227.

---------------------------------------------------------------------------- */
Bool store_(ps_context_t *pscontext)
{
#define STORE_ACTION( Lthed , Lther ) MACRO_START \
  /* NOTHING */ \
MACRO_END

#define STORE_NOWHERE_ACTION() MACRO_START \
  if ( ! insert_hash( topDictStackObj , o1 , o2 )) \
    return FALSE ;     \
MACRO_END

#define STORE_TEST( Lthed )  \
  ( Lthed && oInteger(Lthed[-2]) /* Any dictionary will do... */ )

#define STORE_RESULT() MACRO_START \
  npop( 2 , & operandstack ) ; \
MACRO_END /* return TRUE after invocation of this is implicit */

  register int32 loop , dstacksize ;
  register OBJECT *o1 , *o2 ;
  register OBJECT *dict ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 1 , & operandstack ) ;
  o2 = theTop( operandstack ) ;

  NAMECACHE_DICT_INSERT_HASH(corecontext, o1, o2, STORE_TEST, STORE_ACTION, STORE_RESULT) ;
  NAMECACHE_DICT_NOT_ANYWHERE( o1 , STORE_NOWHERE_ACTION , STORE_RESULT ) ;

  error_clear_newerror_context(corecontext->error);
  dstacksize = theStackSize( dictstack ) ;
  for ( loop = 0 ; loop <= dstacksize ; ++loop ) {
    dict = stackindex( loop , & dictstack ) ;
    if ( extract_hash( dict , o1 )) {
      if ( ! insert_hash( dict , o1 , o2 ))
        return FALSE ;

      npop( 2 ,  & operandstack ) ;
      return TRUE ;
    }
    if ( newerror )
      return FALSE ;
  }
  if ( ! insert_hash( topDictStackObj , o1 , o2 ))
    return FALSE ;
  npop( 2, & operandstack ) ;
  return TRUE ;
}

/* For the get_() & put_() operators see file 'shared1ops.c' */

/* ----------------------------------------------------------------------------
   function:            known_()            author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 177.

---------------------------------------------------------------------------- */
Bool known_(ps_context_t *pscontext)
{
#define KNOWN_ACTION( Lthed , Lther ) MACRO_START \
  /* NOTHING */ \
MACRO_END

#define KNOWN_TEST( Lthed )  \
  ( Lthed == thed /* Must be our dictionary... */ )

#define KNOWN_RESULT( Lres ) MACRO_START \
  o2 = (& tnewobj) ;       \
  Copy( o1 , o2 ) ;        \
  pop( & operandstack ) ;  \
MACRO_END /* return TRUE after invocation of this is implicit */

  register OBJECT *o1 ;
  register OBJECT *o2 ;
  OBJECT *thed ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 1 , & operandstack ) ;
  if ( oType(*o1) != ODICTIONARY )
    return error_handler( TYPECHECK) ;

  o2 = theTop( operandstack ) ;

  thed = oDict(*o1) ;

  NAMECACHE_DICT_EXTRACT_HASH( o2 , KNOWN_TEST , KNOWN_ACTION , KNOWN_RESULT ) ;

  if ( ! oCanRead(*thed) && !object_access_override(thed) )
    return error_handler( INVALIDACCESS ) ;

  error_clear_newerror_context(ps_core_context(pscontext)->error);
/*  Attempt to find the object identified by the key in the dictionary. */
  if ( ! extract_hash( o1 , o2 )) {
    if ( newerror )
      return FALSE ;
    o2 = (& fnewobj) ;
  }
  else {
    o2 = (& tnewobj) ;
  }
  Copy( o1 , o2 ) ;
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            where_()           author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 238.

---------------------------------------------------------------------------- */
Bool where_(ps_context_t *pscontext)
{
#define WHERE_ACTION( Lthed , Lther ) MACRO_START \
  for ( loop = 0 ; loop <= dstacksize ; ++loop ) { \
    theo = stackindex( loop , & dictstack ) ;         \
    thed = oDict(*theo) ;           \
    if ( Lthed == thed ) {           \
      Lther = theo ;                 \
      break ;                        \
    }                                \
  }                                  \
MACRO_END

#define WHERE_TEST( Lthed )  \
  ( Lthed && oInteger(Lthed[-2]) /* Any dictionary will do... */ )

#define WHERE_RESULT( Lres ) MACRO_START \
  theo = Lres ;            \
  if ( ! push( & tnewobj , & operandstack )) \
    return FALSE ;         \
  Copy( key , theo ) ;     \
MACRO_END /* return TRUE after invocation of this is implicit */

  int32 loop , dstacksize ;
  OBJECT *key , *theo , *thed ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  key = theTop( operandstack ) ;
  error_clear_newerror_context(ps_core_context(pscontext)->error);
  dstacksize = theStackSize( dictstack ) ;

  NAMECACHE_DICT_EXTRACT_HASH( key , WHERE_TEST , WHERE_ACTION , WHERE_RESULT ) ;

  for ( loop = 0 ; loop <= dstacksize ; ++loop ) {
    theo = stackindex( loop , & dictstack ) ;
    if ( extract_hash( theo , key )) {
      if ( ! push( & tnewobj , & operandstack ))
        return FALSE ;
      Copy( key , theo ) ;
      return TRUE ;
    }
    if ( newerror )
      return FALSE ;
  }
/*  No object found in any of the dictionaries on the dictionary stack. */
  theo = (& fnewobj) ;
  Copy( key , theo ) ;
  return TRUE ;
}

/* For the copy_() operator see the file 'shared1ops.c' */
/* For the forall_() operator see the file 'execops.c' */
/* For the 'errordict' , 'systemdict' & 'userdict' see file 'dictsetup.c' */

/* ----------------------------------------------------------------------------
   function:            currdict_()        author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 134.

---------------------------------------------------------------------------- */
Bool currdict_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( topDictStackObj , & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            countdictstack_()  author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 133.

---------------------------------------------------------------------------- */
Bool countdictstack_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_integer(theStackSize(dictstack) + 1, &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            dictstack_()       author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 147.

---------------------------------------------------------------------------- */
Bool dictstack_(ps_context_t *pscontext)
{
  register int32 i ;
  register int32 dsize ;
  register int32 glmode ;
  register OBJECT *o1 ;
  register OBJECT *dptr ;
  register OBJECT *olist ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = theTop( operandstack ) ;
  switch ( oType(*o1) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  if ( !oCanWrite(*o1) && !object_access_override(o1) )
    return error_handler( INVALIDACCESS ) ;

  dsize = theStackSize( dictstack ) ;
  if ( theLen(* o1 ) <= dsize )
    return error_handler( RANGECHECK ) ;

/* Check OBJECTS for illegal LOCAL --> GLOBAL */
  if ( oGlobalValue(*o1) )
    for ( i = 0 ; i <= dsize ; ++i ) {
      dptr = stackindex( i , & dictstack ) ;
      if ( illegalLocalIntoGlobal(dptr, corecontext) )
        return error_handler( INVALIDACCESS ) ;
    }

  olist = oArray(*o1) ;
/* Check if saved. */
  glmode = oGlobalValue(*o1) ;
  if ( ! check_asave(olist, theLen(*o1), glmode, corecontext) )
    return FALSE ;

/*  Copy the elements of the dictionary stack into the array. */
  for ( i = dsize ; i >= 0 ; --i ) {
    dptr = stackindex( i , & dictstack ) ;
    Copy(olist, dptr) ;
    ++olist ;
  }

/*  Set up a new sub-array !! */
  theLen(* o1 ) = (uint16)(dsize + 1) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            enddictmark_()     author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript II reference manual page ??.

---------------------------------------------------------------------------- */
#define MAXEXTRA (32<<1)        /* max extra dictionary entries to alloc */

Bool enddictmark_(ps_context_t *pscontext)
{
  register int32 i ;
  register int32 dictsize, extra ;
  register OBJECT *theo , *thed ;
  OBJECT newobject = OBJECT_NOTVM_NOTHING ;
  corecontext_t *corecontext = pscontext->corecontext ;

/* Check for mark on stack, and count the number of objects to the mark. */
  dictsize = num_to_mark() ;
  if ( dictsize < 0 )
    return error_handler( UNMATCHEDMARK ) ;
  if (( dictsize >> 1 ) > MAXPSDICT )
    return error_handler( LIMITCHECK ) ;
  if ( dictsize & 1 )
    return error_handler( UNDEFINEDRESULT ) ;

/* Check OBJECTS for illegal LOCAL --> GLOBAL */
  if ( corecontext->glallocmode )
    for ( i = 0 ; i < dictsize ; ++i ) {
      theo = stackindex( i , & operandstack ) ;
      if ( illegalLocalIntoGlobal(theo, corecontext) )
        return error_handler( INVALIDACCESS ) ;
    }

  thed = ( & newobject ) ;

#ifdef REDUCE_HASH_COLLISIONS
  /* add 25%+ extra space to make dictionary insertion more efficient */
  extra = ((dictsize >> 2) + 2) & ~1;
  if (extra > MAXEXTRA)
    extra = MAXEXTRA;
  if ((dictsize + extra) > MAXPSDICT)
    extra = MAXPSDICT - dictsize;
#else
  extra = 0;
#endif

  if ( ! ps_dictionary(thed, (dictsize + extra) >> 1) )
    return FALSE ;

  for ( i = 0 ; i < dictsize ; i += 2 )
    if ( ! insert_hash( thed , stackindex( i + 1 , & operandstack ) , stackindex( i , & operandstack )))
      return FALSE ;

  npop( dictsize , & operandstack ) ;

/*  Replace the mark on the stack by the dictionary. */
  theo = theTop( operandstack ) ;
  Copy( theo , thed ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            undef_()           author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   macro references:    ext_name(..) .
   arguments:           none .
   description:

   See PostScript Level II reference manual page 477.

---------------------------------------------------------------------------- */
Bool undef_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o1) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( oType(*o2) == OSTRING )
    if ( theLen(* o2 ) > MAXPSNAME ) {
      npop( 2 , & operandstack ) ;
      return TRUE ;
    }

  if ( ! remove_hash( o1 , o2 , TRUE ))
    return FALSE ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}


/* Log stripped */
