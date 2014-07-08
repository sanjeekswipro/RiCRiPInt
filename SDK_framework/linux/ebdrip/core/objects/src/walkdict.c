/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:walkdict.c(EBDSDK_P.1) $
 * $Id: src:walkdict.c,v 1.10.1.2.1.1 2013/12/19 11:25:00 anon Exp $
 *
 * Copyright (C) 1987-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Dictionary walking functions. Execute procedure for all objects in a
 * dictionary.
 */

#include "core.h"
#include "objects.h"
#include "objimpl.h"

#include "swerrors.h"
#include "mm.h"

/* ----------------------------------------------------------------------------
   function:            walk_dictionary   author:              Luke Tunmer
   creation date:       09-Oct-1991       last modification:   ##-###-####
   arguments:
   description:

   For all entries in a dictionary, call the provided procedure. Give up
   if that procedure returns FALSE (which should call error_handler to set
   up the error properly). Access permissions are assumed to have been done.
---------------------------------------------------------------------------- */
Bool walk_dictionary(const OBJECT *dicto,
                     Bool (*proc)(OBJECT *, OBJECT *, void *),
                     void *argBlockPtr)
{
  OBJECT *thed ;
  int32 len ;
  DPAIR *dplist ;

  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;
  HQASSERT(dicto, "No dictionary to walk") ;
  HQASSERT(oType(*dicto) == ODICTIONARY, "Not a dictionary to walk") ;

  thed = oDict(*dicto) ;
  len = DICT_ALLOC_LEN(thed) - 1;
  if ( len < 0 ) { /* end of dict array reached, check for extension */
    OBJECT *ext = thed - 1;

    if ( oType(*ext) == ONOTHING )
      return TRUE;
    thed = oDict(*ext);
    len = DICT_ALLOC_LEN(thed) - 1;
  }
  dplist = (DPAIR *) ( thed + 1 ) + len ;
  while ( len >= 0 ) {
    if ( oType(theIKey(dplist)) != ONOTHING ) {
      /* call the procedure */
      if ( ! (*proc)( &theIKey( dplist ) , &theIObject( dplist ) ,
                      argBlockPtr ))
        return FALSE;
    }
    len-- ;
    dplist-- ;
  }
  return TRUE;
}

/** Private state structure for walking dictionaries in a sorted
    order. */
typedef struct {
  DPAIR *dparray ;
  int32 len ;
  int32 count ;
}
WALK_DICTIONARY_SORT_STATE ;

/** Callback for walk_dictionary which populates an array entry of
    type \c WALK_DICTIONARY_SORT_STATE ready for the sorting
    operation. */
static Bool walk_dictionary_sort( OBJECT *thek , OBJECT *theo ,
                                  void *priv )
{
  WALK_DICTIONARY_SORT_STATE *state = ( WALK_DICTIONARY_SORT_STATE * )priv ;

  HQASSERT( state->count < state->len , "Overflowing sort state" ) ;
  state->dparray[ state->count ].key = *thek ;
  state->dparray[ state->count++ ].obj = *theo ;

  return TRUE ;
}

/** Callback for qsort to compare two dictionary keys. */
static int CRT_API walk_dictionary_compare( const void *va, const void *vb )
{
  const DPAIR *a = (const DPAIR *)va ;
  const DPAIR *b = (const DPAIR *)vb ;
  Bool result ;

  if ( ! o1_eq_o2( & a->key , & b->key , & result )) {
    error_clear() ;
    result = TRUE ; /* Pick a value, any value. */
  }

  if ( result ) {
    return 0 ;
  }
  else {
    return ( o1_gt_o2( & a->key , & b->key ) ? 1 : -1 ) ;
  }
}

Bool walk_dictionary_sorted(const OBJECT *dicto,
                            Bool (*proc)(OBJECT *, OBJECT *, void *),
                            void *argBlockPtr)
{
  WALK_DICTIONARY_SORT_STATE state = { 0 } ;
  OBJECT *thed ;
  Bool result = FALSE ;
  int32 i ;

  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;
  HQASSERT(dicto, "No dictionary to walk") ;
  HQASSERT(oType(*dicto) == ODICTIONARY, "Not a dictionary to walk") ;

  thed = oDict( * dicto ) ;
  state.len = DICT_ALLOC_LEN( thed ) - 1 ;
  state.count = 0 ;
  if ( state.len < 0 ) { /* end of dict array reached, check for extension */
    OBJECT *ext = thed - 1;

    if ( oType(*ext) == ONOTHING )
      return TRUE;
    thed = oDict(*ext);
    state.len = DICT_ALLOC_LEN(thed) - 1;
  }

  state.dparray = mm_alloc( mm_pool_temp ,
                            sizeof( *state.dparray ) * state.len ,
                            MM_ALLOC_CLASS_WALKDICT_TEMP ) ;

  if ( state.dparray == NULL ) {
    return error_handler( VMERROR ) ;
  }

  if ( ! walk_dictionary( dicto , walk_dictionary_sort , & state )) {
    goto CLEANUP ;
  }

  qsort( state.dparray , state.count , sizeof( state.dparray[ 0 ]) ,
         walk_dictionary_compare ) ;

  for ( i = 0 ; i < state.count ; i++ ) {
    DPAIR *dplist = & state.dparray[ i ] ;

    HQASSERT( oType(theIKey(dplist)) != ONOTHING ,
              "Nothings should have been skipped" ) ;
    if ( ! (*proc)( &theIKey( dplist ) , &theIObject( dplist ) ,
                    argBlockPtr )) {
      goto CLEANUP ;
    }
  }

  result = TRUE ;

 CLEANUP:

  if ( state.dparray != NULL ) {
    mm_free( mm_pool_temp , ( mm_addr_t )state.dparray ,
             sizeof( *state.dparray ) * state.len ) ;
  }

  return result ;
}

/*
Log stripped */
