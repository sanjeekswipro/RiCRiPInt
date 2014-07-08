/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:compare.c(EBDSDK_P.1) $
 * $Id: src:compare.c,v 1.22.1.2.1.1 2013/12/19 11:25:00 anon Exp $
 *
 * Copyright (C) 1987-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Object comparison function.
 */

#include "core.h"
#include "hqmemcmp.h"
#include "swerrors.h"

#include "fileio.h"
#include "objects.h"
#include "objimpl.h"

/* ---------------------------------------------------------------------- */

/* When comparing composite objects, we require a scheme to avoid infinite loops
 * due to self-referential objects. This is acheived by a simple stack-like
 * device that pushes and pops objects as we move from comparing objects in one
 * nesting level to another.
 *
 * The 'o1_stack' is the stack of primary objects at each nesting level.
 * The 'o2_stack' is the stack of secondary objects. It's only used when
 * comparing dictionaries, it's a convenient means of passing the parent
 * dictionary to allow the second object to be extracted.
 */
#define MAX_NESTING_LEVEL   (10)

typedef struct {
  const OBJECT *o1_stack[MAX_NESTING_LEVEL];
  const OBJECT *o2_stack[MAX_NESTING_LEVEL];
  int32 level;
} NESTING;

static Bool compare_objects_internal(const OBJECT *o1,
                                     const OBJECT *o2,
                                     NESTING *nesting);


static Bool compare_dictionaries(OBJECT *key,
                                 OBJECT *value,
                                 void *arg)
{
  const OBJECT *value2;
  const OBJECT *dict2;
  NESTING *nesting = (NESTING *) arg;

  HQASSERT(nesting->level > 0, "Should have a parent dictionary");

  dict2 = nesting->o2_stack[nesting->level - 1];
  HQASSERT(oType(*dict2) == ODICTIONARY, "Expected a dict");

  value2 = fast_extract_hash(dict2, key);
  if (value2 == NULL)
    return FALSE;

  if ( !compare_objects_internal(value, value2, nesting) )
    return FALSE;

  return TRUE;
}

static Bool compare_arrays(const OBJECT *o1,
                           const OBJECT *o2,
                           NESTING *nesting )
{
  int32 length;

  HQASSERT((oType(*o1) == OARRAY || oType(*o1) == OPACKEDARRAY) &&
           (oType(*o2) == OARRAY || oType(*o2) == OPACKEDARRAY),
           "Should be comparing 2 arrays");
  HQASSERT(theLen(*o1) == theLen(*o2), "Arrays should be the same length");

  length = theLen(*o1);
  o1 = oArray(*o1);
  o2 = oArray(*o2);
  while (length--) {
    if ( !compare_objects_internal(o1++, o2++, nesting) )
      return FALSE;
  }

  return TRUE;
}

static Bool compare_objects_internal(const OBJECT *o1,
                                     const OBJECT *o2,
                                     NESTING *nesting)
{
  int32 len1;
  int32 len2;
  Bool equal;
  int32 i;
  Hq32x2 offset1;
  Hq32x2 offset2;

  /* The object types must always match */
  if (oType(*o1) != oType(*o2))
    return FALSE;

  /* We ignore access permissions, globalness, PS VM flag, and save level in
   * the comparison, but the executable flag must match for the object types
   * that can be meaningfully executed.
   */
  switch (oType(*o1)) {
    case ONAME:
    case OARRAY:
    case OPACKEDARRAY:
    case OSTRING:
    case OFILE:
      if (oExec(*o1) != oExec(*o2))
        return FALSE;
  }

  /* None of the known clients of this function are likely to compare
   * deeply nested objects. If we do happen to meet with one we'll
   * just return FALSE to be safe.  Similarly, none of the known
   * clients are likely to compare self-referential objects, so we'll
   * return FALSE if we meet one. It's possible to improve this
   * comparison at the expense of complexity should we need to in the
   * future.
   */
  if (nesting->level >= MAX_NESTING_LEVEL)
    return FALSE;
  for (i = 0; i < nesting->level; i++) {
    if (nesting->o1_stack[i] == o1)
      return FALSE;
  }

  /* Push the compared objects onto the stacks */
  nesting->o1_stack[nesting->level] = o1;
  nesting->o2_stack[nesting->level] = o2;
  nesting->level++;

  switch (oType(*o1)) {
    case ONOTHING:
      HQFAIL("Comparing ONOTHING");
    case OINFINITY:
    case OMARK:
    case ONULL:
      /* Simple objects that don't have a value */
      HQASSERT(OBJECT_GET_D1(*o1) == 0 && OBJECT_GET_D1(*o2) == 0,
               "Simple object may not be initialised");
      equal = TRUE;
      break;

    case OINTEGER:
    case OREAL:
    case OOPERATOR:
    case OBOOLEAN:
    case OFONTID:
    case ONAME:
    case OCPOINTER:
      /* Simple objects equal if values equal*/
      equal = (OBJECT_GET_D1(*o1) == OBJECT_GET_D1(*o2));
      break;

    case OINDIRECT:
      equal = (oXRefID(*o1) == oXRefID(*o2) && theGen(*o1) == theGen(*o2));
      break;

    case OFILEOFFSET:
      FileOffsetToHq32x2(offset1, *o1);
      FileOffsetToHq32x2(offset2, *o2);
      equal = (Hq32x2Compare(&offset1, &offset2) == 0);
      break;

    case OSAVE:
    case OLONGSTRING:
    case OGSTATE:
      /* We'll say these composite objects are equal if ptrs equal */
      equal = (OBJECT_GET_D1(*o1) == OBJECT_GET_D1(*o2));
      break;

    case OARRAY:
    case OPACKEDARRAY:
      if ( theILen(o1) != theILen(o2) )
        equal = FALSE;
      else if ( oArray(*o1) == oArray(*o2) )
        equal = TRUE;
      else
        equal = compare_arrays(o1, o2, nesting);
      break;

    case OSTRING:
      equal = (HqMemCmp(oString(*o1), theILen(o1),
                        oString(*o2), theILen(o2)) == 0);
      break;

    case ODICTIONARY:
      getDictLength(len1, o1);
      getDictLength(len2, o2);
      if (len1 != len2)
        equal = FALSE;
      else if (oDict(*o1) == oDict(*o2))
        equal = TRUE;
      else
        equal = walk_dictionary(o1, compare_dictionaries, (void *) nesting);
      break;

      /* For files we require that it is actually the same object and
         not a copy. */
    case OFILE:
      equal = (oFile(*o1) == oFile(*o2));
      break;

    default:
      HQFAIL("Unexpected object comparison");
      equal = FALSE;
      break;
  }

  /* Pop the compared objects from the stacks */
  nesting->level--;

  return equal;
}

Bool compare_objects(const OBJECT *o1, const OBJECT *o2)
{
  NESTING nesting;

  HQASSERT(object_asserts(), "Object system not initialised or corrupt");

  /* NULL pointers can be disregarded */
  if (o1 == NULL || o2 == NULL)
    return FALSE;

  nesting.level = 0;

  return compare_objects_internal(o1, o2, &nesting);
}

/** Convenience function: returns the length and character list
    pointer of a given string, longstring or name object. Returns TRUE
    iff the object was of a string-flavoured type. */

static Bool obj_len_clist( const OBJECT *theo , int32 *len , uint8 **clist )
{
  switch ( oType(*theo)) {
    case OSTRING:
      *clist = oString(*theo) ;
      *len = theLen(*theo) ;
      return TRUE ;

    case OLONGSTRING:
      *clist = theLSCList(*oLongStr(*theo)) ;
      *len = theLSLen(*oLongStr(*theo)) ;
      return TRUE ;

    case ONAME:
      *clist = theICList(oName(*theo)) ;
      *len = theINLen(oName(*theo)) ;
      return TRUE ;

    default:
      *len = 0 ;
      *clist = NULL ;
      return FALSE ;
  }
}

/** Abstraction of the numerical portion of the \c eq_ operator. */

static Bool eq_numbers( const OBJECT *o1 , const OBJECT *o2 , Bool *result )
{
  SYSTEMVALUE v1 , v2 ;

  switch ( oType(*o2)) {
    case OINTEGER :
      v2 = ( SYSTEMVALUE ) oInteger(*o2) ;
      break ;

    case OREAL :
      v2 = oReal(*o2) ;
      break ;

    default:
      *result = FALSE ;
      return TRUE ;
  }

  switch ( oType(*o1)) {
    case OINTEGER :
      v1 = ( SYSTEMVALUE ) oInteger(*o1) ;
      break ;

    case OREAL :
      v1 = oReal(*o1) ;
      break ;

    default:
      *result = FALSE ;
      return TRUE ;
  }

  *result = ( v1 == v2 ) ;
  return TRUE ;
}

/** Abstraction of the string/name portion of the \c eq_ operator. */

static Bool eq_alpha( const OBJECT *o1 , const OBJECT *o2 , Bool *result )
{
  uint8 *c1 , *c2 ;
  int32 l1 , l2 ;

  if ((( oType( *o1 ) == OSTRING || oType( *o1 ) == OLONGSTRING ) &&
       ! oCanRead(*o1) && !object_access_override(o1) ) ||
      (( oType( *o2 ) == OSTRING || oType( *o2 ) == OLONGSTRING ) &&
       ! oCanRead(*o2) && !object_access_override(o2) )) {
    return error_handler( INVALIDACCESS ) ;
  }

  ( void )obj_len_clist( o1 , & l1 , & c1 ) ;
  ( void )obj_len_clist( o2 , & l2 , & c2 ) ;

  *result = ! HqMemCmp( c1 , l1 , c2 , l2 ) ;

  return TRUE ;
}

/** Abstraction of the arbitrary composite objects portion of the \c
    eq_ operator. Note that this will have undefined results if the
    object slot once contained a 64-bit pointer and now has a 32-bit
    quantity assigned via, for example, object_store_boolean. */

static Bool eq_bitstypes( const OBJECT *o1 , const OBJECT *o2 ,
                          Bool ignorebits , Bool *result )
{
  *result = FALSE ;

  if ( oType(*o1) == oType(*o2)) {
    HQASSERT( ignorebits || ( isPSCompObj(*o1) && isPSCompObj(*o2)) ,
              "eq_bitstypes is only safe for composite objects" ) ;
    if ( ignorebits || ( OBJECT_GET_D1( *o1 ) == OBJECT_GET_D1( *o2 ))) {
      /* Special case for filters: the filter id's (stored in the object
         length fields) must also match otherwise they are not the same
         filter even if they refer to the same underlying file
         structure */
      if (oType(*o1) == OFILE &&
          theFilterIdPart( theLen(*o1)) != theFilterIdPart( theLen(*o2)))
        *result = FALSE ;
      else
        *result = TRUE ;
    }
  }

  return TRUE ;
}

/** Abstraction of the operators portion of the \c eq_ operator. */

static Bool eq_op( const OBJECT *o1 , const OBJECT *o2 , Bool *result )
{
  HQASSERT( o1 , "o1 NULL in eq_op." ) ;
  HQASSERT( o2 , "o2 NULL in eq_op." ) ;

  /* To compare operator objects we now compare the respective names.
   * Deals with the case where an operator is made using defineop: one
   * defined later but with the same name should be considered equal.
   * When eq_bitstypes was used they weren't, because the op structure
   * is freshly allocated each time an op is created with defineop.
   */
  *result = ( oType(*o1) == oType(*o2) &&
              theIOpName( oOp(*o1)) == theIOpName( oOp(*o2))) ;

  return TRUE ;
}

Bool o1_eq_o2( const OBJECT *o1 , const OBJECT *o2 , Bool *result )
{
  Bool ignorebits = TRUE ;

  switch ( oType(*o1)) {
    case OINTEGER :
    case OREAL :
      return eq_numbers( o1 , o2 , result ) ;
    case ONAME :
    case OSTRING :
    case OLONGSTRING :
      return eq_alpha( o1 , o2 , result ) ;
    case OOPERATOR :
      return eq_op( o1 , o2 , result ) ;
    case OBOOLEAN :
      *result = ( oType( *o1 ) == oType( *o2 ) &&
                  oBool( *o1 ) == oBool( *o2 )) ;
      return TRUE ;
    case OFONTID :
      *result = ( oType( *o1 ) == oType( *o2 ) &&
                  oFid( *o1 ) == oFid( *o2 )) ;
      return TRUE ;
    case OSAVE :
      *result = ( oType( *o1 ) == oType( *o2 ) &&
                  oSave( *o1 ) == oSave( *o2 )) ;
      return TRUE ;

      /* jonw 20130214: I left this comment in, even though it'd be
         tough to see AC these days and I'm not sure how relevant a
         LaserWriter Plus is :-> */
      /* Changed on purpose to simulate LW+ - see AC before changing. */
    case OFILE :
    case ODICTIONARY :
    case OARRAY :
    case OPACKEDARRAY :
    case OGSTATE :
      ignorebits = FALSE ;
      /*@fallthrough@*/
    case OMARK :
    case ONULL :
    case OINFINITY :
      return eq_bitstypes( o1 , o2 , ignorebits , result ) ;
    default:
      return error_handler( UNREGISTERED ) ;
  }
}

/** Simple abstract accessor for numerical values, but without the
    baggage of calling error_handler that comes with
    object_get_numeric. */

static Bool obj_get_num( const OBJECT *object , SYSTEMVALUE *value )
{
  HQASSERT( object != NULL , "object is NULL" ) ;
  HQASSERT( value != NULL , "Nowhere for numeric value" ) ;

  switch ( oType( *object )) {
    case OINTEGER:
      *value = ( SYSTEMVALUE )oInteger( *object ) ;
      return TRUE ;
    case OREAL:
      if ( object_get_XPF( object , value ) > XPF_INVALID ) {
        return TRUE ;
      }
      break ;
    case OINFINITY:
      *value = OINFINITY_VALUE ;
      return TRUE ;
  }

  return FALSE ;
}

Bool o1_gt_o2( const OBJECT *o1 , const OBJECT *o2 )
{
  SYSTEMVALUE t[ 2 ] ;
  int32 l1 , l2 ;
  uint8 *s1 , *s2 ;

  if ( obj_get_num( o1 , & t[ 0 ]) &&
       obj_get_num( o2 , & t[ 1 ])) {
    return ( t[ 0 ] > t[ 1 ] ) ;
  }

  if ( obj_len_clist( o1 , &l1 , &s1 ) &&
       obj_len_clist( o2 , &l2 , &s2 )) {
    if ( l1 == 0 || l2 == 0 ) {
      return ( l1 > l2 ) ;
    }
    else {
      return ( HqMemCmp( s1 , l1 , s2 , l2 ) > 0 ) ;
    }
  }
  else {
    /* We'll never get here when called from gtgeltle because of its
       type checks, but for other callers we can arbitrarily compare
       object type indices here. */

    return ( oType( *o1 ) > oType( *o2 )) ;
  }
}

/*
Log stripped */
