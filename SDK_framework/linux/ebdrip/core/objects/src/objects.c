/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:objects.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Object definitions and COREobjects initialisation/shutdown.
 */

#include <stdlib.h> /* abs(), for assertions */

#include "core.h"
#include "coreinit.h"
#include "mm.h"
#include "gcscan.h"
#include "mps.h"
#include "swerrors.h"
#include "objects.h"
#include "objimpl.h"
#include "dictinit.h"
#include "saves.h"
#include "namedef_.h"
#include "hqmemcpy.h"
#include "hqmemset.h"

/* Used to temporarily store new created objects until they are stacked */
OBJECT tnewobj = OBJECT_NOTVM_BOOLEAN(TRUE) ;   /* TRUE  boolean */
OBJECT fnewobj = OBJECT_NOTVM_BOOLEAN(FALSE) ;   /* FALSE boolean */
OBJECT ifnewobj = OBJECT_NOTVM_INFINITY ;  /* Infinity object */
OBJECT onothing = OBJECT_NOTVM_NOTHING ;  /* Nothing object */
OBJECT onull = OBJECT_NOTVM_NULL ;        /* Null object */

OBJECT inewobj ;   /* Integer object */
OBJECT fonewobj ;  /* FileOffset object */
OBJECT rnewobj ;   /* Real object */
OBJECT nnewobj ;   /* Literal Name object */
OBJECT nnewobje ;  /* Executable Name object */
OBJECT snewobj ;   /* String object */


/* Declare global init functions here to avoid header inclusion
   nightmare. */
static void init_C_globals_objects(void)
{
  namepurges = NULL ;

  theTags(inewobj)  = OINTEGER | LITERAL ;
  theMark(inewobj)  = ISNOTVM | ISLOCAL | SAVEMASK ;
  theTags(fonewobj) = OFILEOFFSET | LITERAL ;
  theMark(fonewobj) = ISNOTVM | ISLOCAL | SAVEMASK ;
  theTags(rnewobj)  = OREAL | LITERAL ;
  theMark(rnewobj)  = ISNOTVM | ISLOCAL | SAVEMASK ;

  theTags(nnewobj)  = ONAME | LITERAL ;
  theMark(nnewobj)  = ISNOTVM | ISLOCAL | SAVEMASK ;
  theTags(nnewobje) = ONAME | EXECUTABLE ;
  theMark(nnewobje) = ISNOTVM | ISLOCAL | SAVEMASK ;
  theTags(snewobj)  = OSTRING | LITERAL | UNLIMITED ;
  theMark(snewobj)  = ISNOTVM | ISLOCAL | SAVEMASK ;
}

/* Initialise the object subsystem. No other functions should be called until
   this one has been called (this is not yet asserted). */
static Bool objects_swstart(struct SWSTART *params)
{
  corecontext_t *context = get_core_context() ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  context->savelevel = 0 ;
  context->glallocmode = FALSE ;

  return ncache_init() ;
}


/* objects_finish - deinitialization for the object subsystem */
static void objects_finish(void)
{
  HQASSERT(object_asserts(), "Object subsystem not initialised or corrupted") ;

  ncache_finish() ;
}

IMPORT_INIT_C_GLOBALS(ncache)
IMPORT_INIT_C_GLOBALS(dicthash)

void objects_C_globals(core_init_fns *fns)
{
  init_C_globals_dicthash() ;
  init_C_globals_ncache() ;
  init_C_globals_objects() ;

  fns->swstart = objects_swstart ;
  fns->finish = objects_finish ;
}

/* -------------------------------------------------------------------------- */
/* Extended precision reals
 *
 * Using the 16bit theLen() field to store the difference between the intended
 * SYSTEMVALUE and the USERVALUE in oReal() extends the range of integers that
 * can be represented (in an OBJECT) from 2,147,483,647 to 1,099,511,595,007
 */

/** \brief Get an extended precision real (XPF) from a Postscript float.

    \return   XPF_INVALID   malformed XPF
              XPF_STANDARD  real
              XPF_EXTENDED  XPF
  */
int object_get_XPF(const OBJECT *object, SYSTEMVALUE *real)
{
  uint16 len ;
  int16 ext ;

  HQASSERT(object && oType(*object) == OREAL, "object isn't a REAL") ;

  *real = (SYSTEMVALUE)oReal(*object) ;
  len   = theLen(*object) ;

  if (*real < XPF_MINIMUM || *real > XPF_MAXIMUM) {
    if (len) {
      HQFAIL("Malformed extended precision float") ;
      return XPF_INVALID ; /* malformed XPF */
    }
    return XPF_STANDARD ;
  }

  ext = (len < 0x8000) ? (int16)len : (int16)(len - 0x10000) ;
  *real += (SYSTEMVALUE)ext ;

  return XPF_EXTENDED ;
}

/** \brief Store a real, XPF or infinity value */

void object_store_XPF(OBJECT *object, SYSTEMVALUE value)
{
  HQASSERT(object, "Need an object") ;

  if ( !realrange(value) ) {
    theTags(*object) = OINFINITY | LITERAL ;
    theLen(*object) = 0 ;
    OBJECT_SET_D1(*object, 0) ;

  } else {
    USERVALUE real ;
    int16 ext = 0 ;

    if ( !realprecision(value))
      value = 0.0;
    real = (USERVALUE)value ;

    if (real >= XPF_MINIMUM && real <= XPF_MAXIMUM) {
      value -= (SYSTEMVALUE)real ;
      ext    = (int16)value ;
      if (value != (SYSTEMVALUE)ext)
        ext = 0 ;                     /* precision lost */
    }

    theTags(*object) = OREAL | LITERAL ;
    theLen(*object)  = (ext < 0) ? (uint16)(ext + 0x10000) : (uint16)ext ;
    oReal(*object)   = real ;

    HQASSERT((real >= XPF_MINIMUM && real <= XPF_MAXIMUM) ||
             theLen(*object) == 0, "object_store_XPF made malformed XPF") ;
  }
}

/* -------------------------------------------------------------------------- */
/* Initialise a non-VM slot. Slots must be initialised BEFORE storing anything
   in them. */
OBJECT *object_slot_notvm(OBJECT *slot)
{
  HQASSERT(slot, "Not slot to initialise") ;

  theMark(*slot) = ISLOCAL|ISNOTVM|SAVEMASK ;

  return slot ;
}

/* Store a null object in an existing slot. */
void object_store_null(OBJECT *slot)
{
  theTags(*slot) = ONULL | LITERAL;
  SETGLOBJECTTO(*slot, FALSE) ;
  theLen(*slot) = 0 ;
  OBJECT_SET_D1(*slot , 0);
}

/* Store an infinity object in an existing slot. */
void object_store_infinity(OBJECT *slot)
{
  theTags(*slot) = OINFINITY | LITERAL;
  SETGLOBJECTTO(*slot, FALSE) ;
  theLen(*slot) = 0 ;
  OBJECT_SET_D1(*slot , 0);
}

/* Store a boolean object in an existing slot. */
void object_store_bool(OBJECT *slot, Bool value)
{
  HQASSERT(BOOL_IS_VALID(value), "Invalid value being put into boolean") ;

  theTags(*slot) = OBOOLEAN | LITERAL;
  SETGLOBJECTTO(*slot, FALSE) ;
  theLen(*slot) = 0 ;
  oBool(*slot) = value ;
}

/* Store a real object in an existing slot. */
void object_store_real(OBJECT *slot, USERVALUE value)
{
  theTags(*slot) = OREAL | LITERAL;
  SETGLOBJECTTO(*slot, FALSE) ;
  theLen(*slot) = 0 ;
  oReal(*slot) = value ;
}

/* Store an integer object in an existing slot. */
void object_store_integer(OBJECT *slot, int32 value)
{
  theTags(*slot) = OINTEGER | LITERAL;
  SETGLOBJECTTO(*slot, FALSE) ;
  theLen(*slot) = 0 ;
  oInteger(*slot) = value ;
}

/* Store a new integer, real or infinity object in an existing slot,
   depending on the range of the parameter value. */
void object_store_numeric(OBJECT *slot, SYSTEMVALUE value)
{
  SETGLOBJECTTO(*slot, FALSE) ;

  if ( intrange(value) && (int32)value == value ) {
    theTags(*slot) = OINTEGER | LITERAL;
    oInteger(*slot) = (int32)value ;
  } else
    object_store_XPF(slot, value) ;
}

/* Stores a new name object in an existing slot. If 'executable' is
   EXECUTABLE, the name will have the EXECUTABLE attribute set, otherwise it
   will have the LITERAL attribute set. */
void object_store_name(OBJECT *slot, int32 name_id, uint8 litexec)
{
  object_store_namecache(slot, system_names + name_id, litexec);
}

/* Stores a new name object in an existing slot. If 'executable' is
   EXECUTABLE, the name will have the EXECUTABLE attribute set, otherwise it
   will have the LITERAL attribute set. */
void object_store_namecache(OBJECT *slot, NAMECACHE* name, uint8 litexec)
{
  HQASSERT(litexec == EXECUTABLE || litexec == LITERAL,
           "Executable flag is incorrect") ;

  theTags(*slot) = (uint8)(ONAME | litexec) ;
  SETGLOBJECTTO(*slot, FALSE) ;
  theLen(*slot) = 0 ;
  oName(*slot) = name;
}

/* Stores a constant global string object in an existing slot. If
   'executable' is EXECUTABLE, the name will have the EXECUTABLE attribute
   set, otherwise it will have the LITERAL attribute set. The string is
   READ_ONLY. */
void object_store_string(OBJECT *slot, uint8 *string, uint16 length,
                         uint8 litexec)
{
  HQASSERT(litexec == EXECUTABLE || litexec == LITERAL,
           "Executable flag is incorrect") ;

  theTags(*slot) = (uint8)(OSTRING | READ_ONLY | litexec) ;
  SETGLOBJECTTO(*slot, FALSE) ;
  theLen(*slot) = length ;
  oString(*slot) = string;
}

/* Stores a new operator object in an existing slot. */
void object_store_operator(OBJECT *slot, int32 name_id)
{
  HQASSERT(name_id >= 0 && name_id < OPS_COUNTED,
           "Name id is outside of system operators range") ;

  theTags(*slot) = (uint8)(OOPERATOR|EXECUTABLE|UNLIMITED) ;
  SETGLOBJECTTO(*slot, FALSE) ;
  theLen(*slot) = 0 ;
  oOp(*slot) = system_ops + name_id;
}

/* Save/restore functions for objects module */
void objects_save_commit(corecontext_t *context, OBJECTSAVE *savehere)
{
  HQASSERT(object_asserts(), "Object subsystem not initialised or corrupted") ;
  HQASSERT(context, "No context to be saved") ;
  HQASSERT(savehere, "Nowhere to save object state") ;

  savehere->glallocmode = context->glallocmode ;
  savehere->namepurges = namepurges ;

  namepurges = NULL ;
}

void objects_restore_prepare(OBJECTSAVE *restorefrom)
{
  HQASSERT(object_asserts(), "Object subsystem not initialised or corrupted") ;
  HQASSERT(restorefrom, "No object state to restore") ;

  /* Purge current fast name lookups. This operates lazily; each call to
     objects_restore_prepare removes the previous save's name entries,
     finally leaving the destination savelevel's entries in the namepurges
     variable. */
  while ( namepurges ) {
    namepurges->dictobj = NULL ;
    namepurges = namepurges->dictsid  ;
  }

  namepurges = restorefrom->namepurges ;
  restorefrom->namepurges = NULL ;
}

void objects_restore_commit(corecontext_t *context, OBJECTSAVE *restorefrom)
{
  HQASSERT(object_asserts(), "Object subsystem not initialised or corrupted") ;
  HQASSERT(context, "No context to restore") ;
  HQASSERT(restorefrom, "No object state to restore") ;
  HQASSERT(restorefrom->namepurges == NULL,
           "Name purge list not removed; was restore_prepare called?") ;

  context->glallocmode = restorefrom->glallocmode ;
}


#ifdef ASSERT_BUILD
Bool namepurges_is_circular(NAMECACHE *namepurges)
{
  register NAMECACHE *slow;
  register NAMECACHE *fast;

  if ( namepurges == NULL ) return FALSE;
  slow = namepurges; fast = namepurges;
  for (;;) {
    fast = fast->dictsid;
    if ( fast == NULL ) return FALSE;
    if ( slow == fast ) return TRUE;
    fast = fast->dictsid;
    if ( fast == NULL ) return FALSE;
    if ( slow == fast ) return TRUE;
    slow = slow->dictsid;
  }
}
#endif


/* namepurges_unlink - unlink the name from the purge list if it's on it. */

static Bool namepurges_unlink(NAMECACHE **namepurges_loc, NAMECACHE *obj)
{
  register NAMECACHE *curr;
  register NAMECACHE **prev;

  HQASSERT( !namepurges_is_circular( *namepurges_loc ), "namepurges is circular!" );
  /* Run down the purge list and unlink the name */
  prev = namepurges_loc;
  while (( curr = *prev ) != NULL ) {
    if ( curr == obj ) {
      *prev = curr->dictsid;
      return TRUE;
    } else {
      prev = &curr->dictsid;
    }
  }
  return FALSE;
}


/* ncache_purge_finalize - finalize a NAMECACHE: unlink it from the purges
 *
 * If the name was allocated at a higher level, then it can't be in the
 * purges, and we can stop the iteration (return FALSE).
 */


static int namepurges_finalize(OBJECTSAVE *objectsave, int32 level, void *p)
{
  NAMECACHE *obj;

  obj = (NAMECACHE *)p;
  if ( theISaveLevel( obj ) <= level ) {
    if ( namepurges_unlink( &objectsave->namepurges, obj ))
      return FALSE;
    else
      return TRUE;
  } else {
    return FALSE;
  }
}


void ncache_purge_finalize(NAMECACHE *obj)
{
  if ( obj->dictobj != NULL || obj->dictval != NULL ) {
    if ( !namepurges_unlink( &namepurges, obj ))
      (void)save_objectsave_map( namepurges_finalize, (void *)obj );
  }
}


/* Utility function to change object's access level */
Bool object_access_reduce(int32 new_acc, register OBJECT *theo)
{
  uint8 tags ;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT(object_asserts(), "Object subsystem not initialised or corrupted") ;
  HQASSERT(theo, "No object to change access on") ;
  HQASSERT(new_acc == NO_ACCESS ||
           new_acc == EXECUTE_ONLY ||
           new_acc == READ_ONLY ||
           new_acc == UNLIMITED, "Invalid object access mode") ;

  tags = theTags(*theo) ;
  switch ( oType(*theo) ) {
    register OBJECT *thed ;
  case ODICTIONARY :
    thed = theo ;
    do {
      thed = oDict(*thed) ;
      tags = theTags(*thed) ;
      if ( tagsAccess( tags ) < new_acc )
        return error_handler(INVALIDACCESS) ;
      if ( SLOTISNOTSAVED(thed, corecontext) )
        if ( ! check_dsave(thed, corecontext) )
          return FALSE ;
      theTags(*thed) = CAST_TO_UINT8(new_acc | (tags & ~ACCEMASK)) ;
      --thed ;
    } while ( oType(*thed) == ODICTIONARY ) ;
    tags = theTags(*theo) ;
    theTags(*theo) = CAST_TO_UINT8(new_acc | (tags & ~ACCEMASK)) ;
    break ;

  case OGSTATE:
    if (new_acc == EXECUTE_ONLY)
      return error_handler (TYPECHECK);
    /* else drop through */
  case OSTRING:
  case OLONGSTRING:
  case OARRAY:
  case OPACKEDARRAY:
    if ( tagsAccess( tags ) < new_acc )
      return error_handler(INVALIDACCESS) ;
    theTags(*theo) = CAST_TO_UINT8(new_acc | (tags & ~ACCEMASK)) ;
    break ;

  case OFILE:
    if ( tagsAccess( tags ) < new_acc )
      return error_handler(INVALIDACCESS) ;
    if ( oCanWrite(*theo) && new_acc != NO_ACCESS )
      return error_handler(INVALIDACCESS) ;
    theTags(*theo) = CAST_TO_UINT8(new_acc | (tags & ~ACCEMASK)) ;
    break;

  default:
    return error_handler( TYPECHECK ) ;
  }

  return TRUE ;
}

/* Return numerical value of object, if it has been typechecked already. */
SYSTEMVALUE object_numeric_value(const OBJECT *object)
{
  SYSTEMVALUE value ;

  HQASSERT(object != NULL, "object is NULL");

  switch ( oType(*object) ) {
  case OINTEGER:
    return (SYSTEMVALUE)oInteger(*object);
  case OREAL:
    if (object_get_XPF(object, &value) > XPF_INVALID)
      return value ; /* real or XPF */
    break ;          /* malformed XPF */
  case OINFINITY:
    return OINFINITY_VALUE;
  }

  HQFAIL("object is not numeric");
  return -OINFINITY_VALUE ;
}

/* Return numerical value of object, typechecking as we go. */
Bool object_get_numeric(const OBJECT *object, SYSTEMVALUE *value)
{
  HQASSERT(object != NULL, "object is NULL");
  HQASSERT(value != NULL, "Nowhere for numeric value");

  switch ( oType(*object) ) {
  case OINTEGER:
    *value = (SYSTEMVALUE)oInteger(*object);
    return TRUE ;
  case OREAL:
    if (object_get_XPF(object, value) > XPF_INVALID)
      return TRUE ;
    /* Got an out of range XPF, TYPECHECK since we have the opportunity */
    break ;
  case OINFINITY:
    *value = OINFINITY_VALUE;
    return TRUE ;
  }

  return error_handler(TYPECHECK) ;
}

Bool object_get_numeric_array(const OBJECT *array, SYSTEMVALUE *values, int32 n)
{
  HQASSERT(array != NULL, "No object array") ;
  HQASSERT(values != NULL, "Nowhere for numeric values");

  if ( oType(*array) != OARRAY && oType(*array) != OPACKEDARRAY )
    return error_handler(TYPECHECK) ;

  if ( theLen(*array) != n )
    return error_handler(RANGECHECK) ;

  array = oArray(*array) ;
  HQASSERT(array, "No array storage in array") ;

  while ( n > 0 ) {
    if ( !object_get_numeric(array++, values++) )
      return FALSE ;

    --n ;
  }

  return TRUE ;
}

/* Return USERVALUE value of object, typechecking as we go. This routine MAY
   LOSE PRECISION when converting from integers to real values. */
Bool object_get_real(const OBJECT *object, USERVALUE *value)
{
  HQASSERT(object != NULL, "object is NULL");
  HQASSERT(value != NULL, "Nowhere for real value");

  switch ( oType(*object) ) {
  case OINTEGER:
    *value = (USERVALUE)oInteger(*object);

#if defined(ASSERT_BUILD)
    {
      int32 ai = abs(oInteger(*object)) ;

      /* The trace assumes that IEEE 4-byte floats are used for USERVALUES. */
      HQASSERT(sizeof(USERVALUE) == 4, "USERVALUE is not a 4-byte float");

      /* If there were more than 24 bits in the mantissa, then we have lost
         precision. I don't want to include COREtables just for this assert,
         so I'll check that there is no way of representing the integer in 24
         consecutive bits before complaining. */
      HQTRACE((ai & 0x00ffffff) != ai &&
              (ai & 0x01fffffe) != ai &&
              (ai & 0x03fffffc) != ai &&
              (ai & 0x07fffff8) != ai &&
              (ai & 0x0ffffff0) != ai &&
              (ai & 0x1fffffe0) != ai &&
              (ai & 0x3fffffc0) != ai &&
              (ai & 0x7fffff80) != ai,
              ("Precision lost converting integer %d to float %f",
               oInteger(*object), *value)) ;
    }
#endif

    return TRUE ;
  case OREAL:
    *value = oReal(*object);
    return TRUE ;
  case OINFINITY:
    *value = BIGGEST_REAL;
    return TRUE ;
  }

  return error_handler(TYPECHECK) ;
}

/* Return integer value of object, typechecking as we go. */
Bool object_get_integer(const OBJECT *object, int32 *value)
{
  HQASSERT(object != NULL, "object is NULL");

  switch ( oType(*object) ) {
    USERVALUE rval ;
  case OREAL:
    rval = oReal(*object) ;
    if ( intrange(rval) && rval == (int32)rval ) {
      *value = (int32)rval ;
      return TRUE ;
    }
    break ;
  case OINTEGER:
    *value = oInteger(*object);
    return TRUE ;
  case OINFINITY:
    *value = MAXINT32;
    return TRUE ;
  }

  return error_handler(TYPECHECK) ;
}

/* ----------------------------------------------------------------------------
   Extract a bounding box from the passed PS number array. If any of the
   objects in the array are not numeric, a typecheck error is raised. The bbox
   is normalised. */
Bool object_get_bbox(const OBJECT *object, sbbox_t *bbox)
{
  SYSTEMVALUE *bbindexed ;

  HQASSERT(object, "No bbox object") ;
  HQASSERT(bbox, "Nowhere for bbox") ;

  bbox_as_indexed(bbindexed, bbox) ;

  return object_get_numeric_array(object, bbindexed, 4) ;
}

static
void *ps_object_allocator(
  size_t size,
  uintptr_t data)
{
  UNUSED_PARAM(size_t, size);

  HQASSERT((NDICTOBJECTS(data)*sizeof(OBJECT) == size), "Inconsistent dictionary memory size");

  return (get_omemory(NDICTOBJECTS(data)));
}

static
DICT_ALLOCATOR ps_dict_allocator = {
  ps_object_allocator, 0
};

/* Create a new dictionary, out of the appropriate global/local memory. */
Bool ps_dictionary(OBJECT *thedict, int32 lnth)
{
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT(thedict, "No return pointer for dictionary object") ;
  HQASSERT(lnth >= 0, "Negative length for dictionary") ;

  ps_dict_allocator.data = lnth;
  return (dict_create(thedict, &ps_dict_allocator, lnth,
                      (int8)(ISPSVM |
                             GLMODE_SAVELEVEL(corecontext->glallocmode, corecontext) |
                             corecontext->glallocmode)));
}


/* Function to initialise a newly-allocated dictionary */
void init_dictionary(OBJECT *thedict, int32 lnth, uint8 access,
                     OBJECT *dict, int8 mark)
{
  register OBJECT *loop , *limit ;
  register DPAIR *dpairs ;
  uint32 transfer0, transfer1 ;
  int8 lmark = (int8)(mark & ~GLOBMASK) ; /* Local mark, for slots */
  size_t alloc_size = min(DICT_ADJUSTED_SLOTS(lnth), MAXPSDICT);

  HQASSERT(object_asserts(), "Object subsystem not initialised or corrupted") ;
  HQASSERT(dict, "No dictionary to initialise") ;
  HQASSERT(thedict, "No dictionary header object") ;

  HQASSERT((access & ~ACCEMASK) == 0, "Dictionary access tag has wrong type") ;

  theTags(*dict) = OINTEGER ;
  theMark(*dict) = lmark ; /* This field is not used. */
  theLen(*dict) = CAST_TO_UINT16(alloc_size); /* Actual dict array length */
  oInteger(*dict) = 0 ; /* Reference count from dict stack */
  ++dict ;

  theTags(*dict) = ONOTHING ; /* extendable link */
  theMark(*dict) = lmark ; /* Field is not used, unless dict is extended. */
  theLen(*dict) = CAST_TO_UINT16(lnth); /* Maximum dictionary capacity */
  OBJECT_SET_D1(*dict, 0); /* Field is not used, unless dict is extended. */
  ++dict ;

  loop = ( dict + 1 ) ;
  dpairs = ( DPAIR * )( loop ) ;

  /* Dictionary header gets copy of access and globalness from dictionary. */
  theTags(*thedict) = (uint8)(ODICTIONARY | LITERAL | access) ;
  SETGLOBJECTTO(*thedict, (mark & GLOBMASK)) ; /* Copy globalness to parent */
  theLen(*thedict) = CAST_TO_UINT16(lnth) ; /* Copy length to parent */
  oDict( *thedict ) = dict ;

  theTags(*dict) = (uint8)(ONOTHING | LITERAL | access) ;
  theMark(*dict) = mark ; /* Includes global flag for dictionary */
  theLen(*dict) = 0 ;   /* Current length of dictionary */
  oDict(*dict ) = (OBJECT*)dpairs;

  /* Get the p0 and p1 transfer words to initialise the dictionary pairs. */
  {
    OBJECT transfer ;

    theMark(transfer) = lmark ; /* Slots are local */

    theTags(transfer) = ONOTHING ;
    theLen(transfer) = 1 ;       /* non-zero means really empty for dpairs[0],
                                    zero means deleted entry. */
    transfer0 = OBJECT_GET_D0(transfer) ; /* Combined tags, mark, len
                                             for dpairs[0] */

    theTags(transfer) = ONULL | LITERAL ; /* dpairs[1] must be tagged for GC */
    theLen(transfer) = 0 ;
    transfer1 = OBJECT_GET_D0(transfer) ; /* Combined tags, mark, len
                                             for dpairs[1] */
  }

  limit = loop + 2 * alloc_size;
  while ( loop < limit ) {
    OBJECT_SET_D0(*loop, transfer0) ; /* Set slot properties */
    OBJECT_SET_D1(*loop, 0) ;
    ++loop ;
    OBJECT_SET_D0(*loop, transfer1) ; /* Set slot properties */
    OBJECT_SET_D1(*loop, 0) ;
    ++loop ;
  }
}

/* Extended error handlers. Provide key and value information for hash
   extraction and insertion operations.

   errorinfo_error_handler uses objects, namedinfo_error_handler passes a
   name number. theval can be NULL for namedinfo_error_handler.
*/
Bool errorinfo_error_handler(int32 errorno,
                             const OBJECT *thekey, const OBJECT *theval)
{
  HQASSERT(object_asserts(), "Object subsystem not initialised or corrupted") ;

  return object_error_info( thekey , theval ) && error_handler( errorno ) ;
}

Bool namedinfo_error_handler(int32 errorno, int32 nameno, const OBJECT *theval)
{
  OBJECT name = OBJECT_NOTVM_NAME(0, LITERAL);
  OBJECT val = onull ;

  HQASSERT(object_asserts(), "Object subsystem not initialised or corrupted") ;

  oName(name) = system_names + nameno ;

  if ( theval )
    OCopy(val, *theval) ;

  return object_error_info( & name , & val ) && error_handler( errorno ) ;
}


#if defined( ASSERT_BUILD )
Bool object_asserts(void)
{
  return (theTags(tnewobj) == (OBOOLEAN|LITERAL) &&
          oBool(tnewobj) == TRUE &&
          theMark(tnewobj) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(fnewobj) == (OBOOLEAN|LITERAL) &&
          oBool(fnewobj) == FALSE &&
          theMark(fnewobj) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(inewobj) == (OINTEGER|LITERAL) &&
          theMark(inewobj) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(fonewobj) == (OFILEOFFSET|LITERAL) &&
          theMark(fonewobj) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(rnewobj) == (OREAL|LITERAL) &&
          theMark(rnewobj) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(ifnewobj) == (OINFINITY|LITERAL) &&
          theMark(ifnewobj) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(nnewobj) == (ONAME|LITERAL) &&
          theMark(nnewobj) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(nnewobje) == (ONAME|EXECUTABLE) &&
          theMark(nnewobje) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(onull) == (ONULL|LITERAL) &&
          theMark(onull) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(onothing) == (ONOTHING|LITERAL|NO_ACCESS) &&
          theMark(onothing) == (ISNOTVM|ISLOCAL|SAVEMASK) &&
          theTags(snewobj) == (OSTRING|LITERAL|UNLIMITED) &&
          theMark(snewobj) == (ISNOTVM|ISLOCAL|SAVEMASK)) ;
}
#endif


/* ps_scan -- a scan method for the GC */
mps_res_t MPS_CALL ps_scan(mps_ss_t scan_state, mps_addr_t base, mps_addr_t limit)
{
  register OBJECT *obj;
  OBJECT *obj_limit;
  register mps_addr_t ref;
  size_t len;

  obj_limit = limit;
  MPS_SCAN_BEGIN( scan_state )
    for ( obj = base; obj < obj_limit; obj++ ) {
      ref = (mps_addr_t)oOther( *obj );
      switch ( oXType( *obj )) {
      case OOPERATOR:
        /* It refers to a NAMECACHE, do that just like ONAME below. */
        MPS_RETAIN(&theIOpName(oOp(*obj)), TRUE);
        len = sizeof(OPERATOR);
        break;
      case ONAME:
        MPS_RETAIN(&oName(*obj), TRUE);
        continue;
      case OSAVE: /* must be in the savestack */
        continue;
      case ODICTIONARY:
        /* Dictionaries are weird: two fields before the pointer. */
        ref = ADDR_SUB( ref, 2 * sizeof(OBJECT) );
        len = (2 * DICT_ALLOC_LEN(oDict(*obj)) + 3) * sizeof(OBJECT);
        break;
      case OSTRING: {
        mps_addr_t ref_limit;

        ref_limit = ADDR_ADD( ref, theLen(*obj));
        /* ref could point into the middle of a string, so align it. */
        ref = PTR_ALIGN_DOWN( mps_addr_t, ref, MM_PS_ALIGNMENT );
        len = ADDR_OFFSET( ref, ref_limit );
      } break;
      case OFILE:
        MPS_RETAIN(&oFile(*obj), TRUE);
        continue;
      case OARRAY:
      case OPACKEDARRAY:
        len = theLen(*obj) * sizeof( OBJECT );
        break;
      case OLONGARRAY:
      case OLONGPACKEDARRAY:
        /* Mark the trampoline then point to the actual array */
        PS_MARK_BLOCK( scan_state, ref, 2 * sizeof( OBJECT ));
        len = oLongArrayLen(*obj);
        ref = (mps_addr_t)oLongArray(*obj);
        break;
      case OGSTATE:
        MPS_RETAIN(&oGState(*obj), TRUE);
        continue;
      case OLONGSTRING: {
        mps_addr_t ref_limit;

        /* Mark the header. */
        PS_MARK_BLOCK(scan_state, ref, sizeof(LONGSTR));

        /* Set up to mark the string. */
        ref = (mm_addr_t)theLSCList(*oLongStr(*obj)) ;
        ref_limit = ADDR_ADD(ref, theLSLen(*oLongStr(*obj)));
        /* ref could point into the middle of a string, so align it. */
        ref = PTR_ALIGN_DOWN( mps_addr_t, ref, MM_PS_ALIGNMENT );
        len = ADDR_OFFSET(ref, ref_limit);
      } break;
      default: continue; /* not a composite object */
      }
      PS_MARK_BLOCK( scan_state, ref, len );
    }
  MPS_SCAN_END( scan_state );
  return MPS_RES_OK;
}


/* ps_typed_scan - scan method for a typed pool */
mps_res_t MPS_CALL ps_typed_scan(mps_ss_t ss, mps_addr_t base, mps_addr_t limit)
{
  TypeTag tag;
  mps_addr_t obj = base;
  mps_res_t res = MPS_RES_OK;
  size_t size;

  HQASSERT( PTR_IS_ALIGNED_P2( mps_addr_t, base, MM_PS_TYPED_ALIGNMENT ),
            "unaligned object" );
  while ( obj < limit && res == MPS_RES_OK ) {
    /* The tag is in the same place in all types, that's the point of it. */
    tag = ( (struct generic_typed_object *)obj )->typetag;
    switch (tag) {
    case tag_NCACHE: {
      res = ncache_scan( &size, ss, (NAMECACHE *)obj );
    } break;
    case tag_FILELIST: {
      res = ps_scan_file( &size, ss, (struct FILELIST *)obj );
    } break;
    case tag_GSTATE: {
      res = gs_scan( &size, ss, (struct GSTATE *)obj );
    } break;
    default: {
      HQFAIL("Invalid tag in scan");
      res = MPS_RES_FAIL;
      size = 4; /* No value correct here; this to silence the compiler. */
    }
    }
    obj = ADDR_ADD( obj, SIZE_ALIGN_UP_P2( size, MM_PS_TYPED_ALIGNMENT ));
  }
  return res;
}


/* ps_typed_skip -- skip method for a typed pool */
mps_addr_t MPS_CALL ps_typed_skip( mps_addr_t object )
{
  TypeTag tag;
  size_t size;

  tag = ( (struct generic_typed_object *)object )->typetag;
  HQASSERT( PTR_IS_ALIGNED_P2( mps_addr_t, object, MM_PS_TYPED_ALIGNMENT ),
            "unaligned object" );
  switch (tag) {
  case tag_NCACHE: {
    size = sizeof(NAMECACHE) + theINLen( (NAMECACHE *)object );
  } break;
  case tag_FILELIST: {
    size = filelist_size( (struct FILELIST *)object );
  } break;
  case tag_GSTATE: {
    size = gstate_size( (struct GSTATE *)object );
  } break;
  default:
    HQFAIL("Invalid tag in skip");
    size = 4; /* No value correct here; this to silence the compiler. */
  }
  return ADDR_ADD( object, SIZE_ALIGN_UP_P2( size, MM_PS_TYPED_ALIGNMENT ));
}


/* object_finalize -- the callback for finalizing an object */
void object_finalize(mm_addr_t obj)
{
  TypeTag tag;

  tag = ( (struct generic_typed_object *)obj )->typetag;
  switch (tag) {
  case tag_NCACHE: {
    ncache_finalize( (NAMECACHE *)obj );
  } break;
  case tag_FILELIST: {
    fileio_finalize( (struct FILELIST *)obj );
  } break;
  case tag_GSTATE: {
    gstate_finalize( (struct GSTATE *)obj );
  } break;
  default: HQFAIL("Invalid tag in finalize");
  }
}


/* --------------------------------------------------------------------------*/
/* Routines to allocate arrays and strings in current PostScript memory */
Bool ps_array(OBJECT *theo, int32 arraysize)
{
  register OBJECT *olist ;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT( theo, "theo NULL in ps_array" ) ;

  if ( arraysize < 0 )
    return error_handler(LIMITCHECK);

  if ( arraysize > MAXPSARRAY )
    return error_handler(RANGECHECK);

  if (arraysize == 0) {
    olist = NULL;
  } else {
    olist = get_omemory(arraysize);
    if (olist == NULL)
      return error_handler(VMERROR);
  }

  theTags(*theo) = OARRAY | LITERAL | UNLIMITED ;
  SETGLOBJECT(*theo, corecontext) ;
  theLen(*theo) = CAST_TO_UINT16(arraysize) ;
  oArray(*theo) = olist ;

  return TRUE ;
}

/** Create and return a long array trampoline

    The caller must have selected the appropriate allocation mode before calling
    this routine. eg for array_, it will be the same as the new array, and for
    getinterval_ it will be the allocation mode of the root, NOT the array. */
OBJECT * extended_array(OBJECT * olist, int32 length)
{
  corecontext_t *corecontext = get_core_context_interp() ;
  OBJECT * extend = get_omemory(2) ;
  if (extend) {
    /* The trampoline */
    theTags(*extend) = OINTEGER | LITERAL | UNLIMITED ;
    SETGLOBJECT(*extend, corecontext) ;
    theLen(*extend) = 0 ;
    oInteger(*extend) = length ;
    ++extend ;

    theTags(*extend) = OARRAY | LITERAL | UNLIMITED ;
    SETGLOBJECT(*extend, corecontext) ;
    theLen(*extend) = 0 ;
    oArray(*extend) = olist ;
    --extend ;
  }
  return extend ;
}

Bool ps_array_copy(OBJECT* o1, OBJECT* o2)
{
  uint16 lo1 , lo2 ;
  int32 glmode ;
  int32 i ;
  OBJECT *olist1 ;
  OBJECT *olist2 ;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT((o1 && o2),
           "ps_array_copy: NULL array pointer");
  HQASSERT(oType(*o1) == OARRAY || oType(*o1) == OPACKEDARRAY,
           "ps_array_copy: o1 not an array");
  HQASSERT(oType(*o2) == OARRAY || oType(*o2) == OPACKEDARRAY,
           "ps_array_copy: o1 not an array");

  lo1 = theLen(*o1) ;
  lo2 = theLen(*o2) ;
  if ( lo1 > lo2 )
    return error_handler( RANGECHECK ) ;

  olist1 = oArray(*o1) ;
  olist2 = oArray(*o2) ;

  /* Check OBJECTS for illegal LOCAL --> GLOBAL */
  glmode = oGlobalValue(*o2) ;
  if ( glmode ) {
    for ( i = 0 ; i < lo1 ; ++i ) {
      if ( illegalLocalIntoGlobal(olist1, corecontext) )
        return error_handler( INVALIDACCESS ) ;
      ++olist1 ;
    }
    olist1 = oArray(*o1) ;
  }

  if (lo1 > 0) {
    /* Check if saved. */
    if ( ! check_asave(olist2 , lo2 , glmode, corecontext ))
      return FALSE ;

    /*  Copy arrays values across */
    if ( olist1 < olist2 ) {
      for ( i = lo1 ; i > 0 ; --i ) {
        Copy(olist2, olist1) ;
        ++olist1 ; ++olist2 ;
      }
    }
    else {
      olist1 += ( lo1 - 1 ) ;
      olist2 += ( lo1 - 1 ) ;
      for ( i = lo1 ; i > 0 ; --i ) {
        Copy(olist2, olist1) ;
        --olist1 ; --olist2 ;
      }
    }
  }

  return TRUE;
}

Bool ps_string(OBJECT *theo, const uint8 *string, int32 length)
{
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT(theo, "No PS string object" ) ;

  if ( length < 0 )
    return error_handler(LIMITCHECK);

  if ( length > MAXPSSTRING )
    return error_handler(RANGECHECK);

  if ( length == 0 ) {
    oString(*theo) = NULL ;
  } else {
    oString(*theo) = get_smemory(length);
    if (oString(*theo) == NULL)
      return error_handler(VMERROR);

    if ( string ) /* Initialise from string contents, if supplied */
      HqMemCpy(oString(*theo), string, length) ;
    else
      HqMemZero(oString(*theo), length);
  }

  theTags(*theo) = OSTRING | UNLIMITED | LITERAL ;
  SETGLOBJECT(*theo, corecontext) ;
  theLen(*theo) = CAST_TO_UINT16(length) ;

  return TRUE ;
}

Bool ps_longstring(OBJECT *theo, const uint8 *string, int32 length)
{
  LONGSTR *longstr ;
  uint8 *clist = NULL ;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT(theo, "No PS string object" ) ;

  if ( length < 0 )
    return error_handler(RANGECHECK);

  longstr = (LONGSTR *)get_smemory(sizeof(LONGSTR) + length);
  if (longstr == NULL)
    return error_handler(VMERROR);

  if ( length > 0 ) {
    clist = (uint8 *)longstr + sizeof(LONGSTR) ;

    if ( string ) /* Initialise from string contents, if supplied */
      HqMemCpy(clist, string, length) ;
    else
      HqMemZero(clist, length);
  }

  theTags(*theo) = OLONGSTRING | UNLIMITED | LITERAL ;
  SETGLOBJECT(*theo, corecontext) ;
  theLen(*theo) = 0 ;
  oLongStr(*theo) = longstr ;
  theLSLen(*longstr) = length ;
  theLSCList(*longstr) = clist ;

  return TRUE ;
}

/** Returns a string which will be either a long or a normal length string,
as required by the passed 'length'.
*/
Bool ps_long_or_normal_string(OBJECT *object, const uint8 *string, int32 length)
{
  if ( length > MAXPSSTRING )
    return ps_longstring(object, string, length) ;
  else
    return ps_string(object, string, length) ;
}

Bool ps_string_interval(OBJECT *out, const OBJECT *in,
                        int32 start, int32 length)
{
  uint8 *clist ;
  int32 inlen ;

  HQASSERT(out, "No output string object" ) ;
  HQASSERT(in, "No input string object" ) ;

  if ( oType(*in) == OLONGSTRING ) {
    clist = theLSCList(*oLongStr(*in)) ;
    inlen = theLSLen(*oLongStr(*in)) ;
  } else if ( oType(*in) == OSTRING ) {
    clist = oString(*in) ;
    inlen = theLen(*in) ;
  } else
    return error_handler(TYPECHECK) ;

  if ( !oCanRead(*in) && !object_access_override(in) )
    return error_handler(INVALIDACCESS) ;

  if ( length < 0 || start < 0 || start + length > inlen )
    return error_handler(RANGECHECK);

  if ( length <= MAXPSSTRING ) {
    theTags(*out) = CAST_TO_UINT8((theTags(*in) & ~ETYPEMASK) | OSTRING) ;
    SETGLOBJECTTO(*out, oGlobalValue(*in)) ;
    theLen(*out) = CAST_TO_UINT16(length) ;
    if ( length != 0 )
      oString(*out) = clist + start ;
    else
      oString(*out) = NULL ;
  } else {
    LONGSTR *longstr ;

    HQASSERT(oType(*in) == OLONGSTRING,
             "Type for string longer than MAXPSSTRING should be OLONGSTRING") ;

    if ( oGlobalValue(*in) )
      longstr = (LONGSTR *)get_gsmemory(sizeof(LONGSTR));
    else
      longstr = (LONGSTR *)get_lsmemory(sizeof(LONGSTR));

    if ( longstr == NULL )
      return error_handler(VMERROR) ;

    theTags(*out) = theTags(*in) ;
    SETGLOBJECTTO(*out, oGlobalValue(*in)) ;
    theLen(*out) = 0 ;
    oLongStr(*out) = longstr ;
    theLSLen(*longstr) = length ;
    theLSCList(*longstr) = clist + start ;
  }

  return TRUE ;
}


/*
Log stripped */
