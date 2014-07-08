/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:genhook.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Generic hook handling code.
 */

#include "core.h"
#include "hqmemcmp.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"

#include "stacks.h"
#include "control.h"
#include "dictops.h"
#include "dicthash.h"
#include "miscops.h"
#include "genhook.h"
#include "graphics.h"
#include "execops.h"

/*
 *  Macro used to compare two names based on the text associated with them.
 */

#define NAME_COMPARE(n1, n2) (HqMemCmp( (n1)->clist, (n1)->len, (n2)->clist, (n2)->len))

/*
 *  State used to walk through the dictionary finding the lowest name which is
 *  strictly greater than the previous one found.
 */

typedef struct DICT_STEP_BYNAME_STATE {
    NAMECACHE *key ;
    OBJECT *value ;
    NAMECACHE *prev_key ;
} DICT_STEP_BYNAME_STATE ;

static Bool hookStatus[MAX_HOOK] = {
  TRUE, TRUE, TRUE,
  TRUE, TRUE, TRUE,
  TRUE, TRUE, TRUE,
  TRUE
};

static int32 hookName[MAX_HOOK] = {
  NAME_StartImage,
  NAME_EndImage,
  NAME_StartVignette,
  NAME_EndVignette,
  NAME_StartPainting,
  NAME_StartPartialRender,
  NAME_StartRender,
  NAME_EndRender,
  NAME_StartJob,
  NAME_EndJob
};

void init_C_globals_genhook(void)
{
  int32 i ;
  for (i=0; i<MAX_HOOK; i++) {
    hookStatus[i] = TRUE ;
  }
}


/*
 *  This function is passed to walk_dictionary as a parameter.
 */
static Bool dict_step_byname_walk( OBJECT *okey,
                                   OBJECT *theo, void *pval)
{
  NAMECACHE *key ;
  DICT_STEP_BYNAME_STATE *dsbs = (DICT_STEP_BYNAME_STATE *) pval ;

  HQASSERT( okey , "okey NULL in dict_step_byname_walk" ) ;
  HQASSERT( pval , "pval NULL in dict_step_byname_walk" ) ;
  if ( oType(*okey) != ONAME )
    return error_handler( TYPECHECK ) ;
  key = oName(*okey) ;
  if ( dsbs->prev_key != NULL && NAME_COMPARE(key, dsbs->prev_key) <= 0)
    return TRUE ; /* Ignore this - it's too small */
  if ( dsbs->key != NULL && NAME_COMPARE(key, dsbs->key) >= 0)
    return TRUE ; /* Ignore this - we've found smaller before */
  /* This is the lowest so far above the threshold */
  dsbs->key = key ;
  dsbs->value = theo ;
  return TRUE ;
}


/*
 *  runHooks: Run the hooks associated with genhookId in dictionary "dict".
 *
 *  Common usage:

  if ( ! runHooks ( & thegsDevicePageDict( gstate ) ,
                         GENHOOK_SomeHookName ) )
    return FALSE;

 *  to run a hook called SomeHookName from the pagedevice dictionary.
 *
 *  If "name" associated with GENHOOK_SomeHookName isn't defined in "dict", this
 *  function does nothing, but we assert. Otherwise it gets the object associated
 *  with "name" in "dict".  This object must be a dictionary; otherwise, TYPECHECK.
 *  Next, step through the keys of the dictionary retrieved, executing the values
 *  associated with them one by one.
 *
 *  For predictability, these keys are used in sorted order; since there will
 *  usually be few keys, we sort them in a simplistic n^2 way, rather than writing
 *  a real sorting routine, or requiring that qsort be present.  Every time we
 *  are ready to execute a new key, we walk through the dictionary looking for
 *  the smallest key which is greater than the one we used last time.
 * */

Bool runHooks( OBJECT *dict, GenHookId genhookId)
{
  OBJECT *ohookd;
  DICT_STEP_BYNAME_STATE dsb_state;

  HQASSERT( dict , "dict NULL in do_run_hooks" ) ;
  HQASSERT( oType(*dict) == ODICTIONARY,
    "Non-dictionary passed as first argument to do_run_hooks") ;

  if (! hookStatus[genhookId])
    return TRUE;

  oName( nnewobj ) = system_names + hookName[genhookId];
  error_clear_newerror();
  ohookd = extract_hash( dict , & nnewobj ) ;

  /* Hook dictionary may be absent from pagedev if we have had an interrupt
   * and are in the process of shutting down.
   */
  if ( ohookd == NULL ) {
    /* If there was an error, return FALSE, otherwise TRUE */
    return ! newerror ;
  }

  /* arguable whether this should be an assert or a user error: I
     suppose it is just possible that someone could set the hook to
     something weird */
  if ( oType(*ohookd) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  hookStatus[genhookId] = FALSE; /* Until we learn otherwise */

  /* Initialise the structure used to step through the dictionary */
  dsb_state.prev_key = NULL;
  for (;;) {
    /* Step onto the next entry in the dictionary (sorted by name) */
    dsb_state.key = NULL ;
    if (!walk_dictionary( ohookd, dict_step_byname_walk, &dsb_state ))
      return FALSE ; /* Typecheck */
    if ( dsb_state.key == NULL )
      break ;
    dsb_state.prev_key = dsb_state.key ;

    /* Note that we've seen a hook */
    hookStatus[genhookId] = TRUE;

    /* Execute the value associated with that entry */
    if ( !setup_pending_exec( dsb_state.value, TRUE ) )
      return FALSE ;
  }
  return TRUE ;
}

/* ---------------------------------------------------------------------- */

/* resetHookStatus indicates that we have got to a position where the referenced hook
   may have been reset, so we don't know whether there are any or not yet - we'll
   find out the first time we are asked to run one
 */

void setHookStatus (GenHookId genhookId, int32 fStatus)
{
  if (genhookId == MAX_HOOK) {
    for (genhookId = 0; genhookId < MAX_HOOK; genhookId++)
      hookStatus[genhookId] = fStatus;
  } else {
    hookStatus[genhookId] = fStatus;
  }
}


/* ---------------------------------------------------------------------- */

/*
 *  runhooks_: Postscript binding to do_run_hooks in internaldict
 *
 *  The StartRender hooks are run from within Postscript, and others might also
 *  want to be.  To support this, we define a Postscript procedure in internaldict
 *  that gives access to do_run_hooks.  Here's an example of its use:

/runhooks 0 internaldict /runhooks get def
<<
  /Tubbies <<
    /tinkywinky {(tubby bye-bye) =}
    /dipsy {(tubbytoast) =}
    /laalaa {(tubbycustard) =}
    /po {(eh-oh) =}
  >>
  /Doesnt (matter)
>> /Tubbies runhooks

 * This should produce the output

tubbytoast
tubbycustard
eh-oh
tubby bye-bye

 * while "<< >> /noonoo runhooks" should do nothing without failing.
 */

Bool runhooks_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *otop, odict = OBJECT_NOTVM_NOTHING , oname = OBJECT_NOTVM_NOTHING ;
  GenHookId hookId;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  otop = TopStack( operandstack , ssize ) ;
  OCopy(oname, otop[ 0 ]) ;
  if ( fastStackAccess( operandstack )) {
    OCopy(odict, otop[ -1 ]) ;
  } else {
    Copy(&odict, stackindex(1, &operandstack)) ;
  }
  if ( oType( odict ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( oType( oname ) != ONAME )
    return error_handler( TYPECHECK ) ;

  hookId = 0;
  for (;;) {
    if (system_names + hookName[hookId] == oName(oname))
      break;
    hookId++;
    if (hookId == MAX_HOOK)
      return error_handler (RANGECHECK);
  }

  npop(2, &operandstack);

  return runHooks(&odict, hookId) ;
}

/* Log stripped */
