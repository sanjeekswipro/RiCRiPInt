/** \file
 * \ingroup datum
 *
 * $HopeName: COREdatum!src:swdataobj.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Interface connecting COREobjects to structured data API.
 */

#include "core.h"

#include "uvms.h"
#include "ripcall.h"
#include "swdataapi.h"
#include "swdataimpl.h"
#include "blobdata.h"
#include "bloberrors.h"
#include "swerrors.h"
#include "objects.h"
#include "objstack.h"
#include "mm.h"

/* Forward declarations for object data API methods */
static sw_data_result RIPCALL objdata_get_indexed(/*@notnull@*/ /*@in@*/ const sw_datum *array,
                                                  size_t index,
                                                  /*@notnull@*/ /*@out@*/ sw_datum *value) ;
static sw_data_result RIPCALL objdata_set_indexed(/*@notnull@*/ /*@in@*/ sw_datum *array,
                                                  size_t index,
                                                  /*@notnull@*/ /*@in@*/ const sw_datum *value) ;
static sw_data_result RIPCALL objdata_get_keyed(/*@notnull@*/ /*@in@*/ const sw_datum *dict,
                                                /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                                /*@notnull@*/ /*@out@*/ sw_datum *value) ;
static sw_data_result RIPCALL objdata_set_keyed(/*@notnull@*/ /*@in@*/ sw_datum *dict,
                                                /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                                /*@notnull@*/ /*@in@*/ const sw_datum *value) ;
static sw_data_result RIPCALL objdata_iterate_begin(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                    /*@notnull@*/ /*@out@*/ sw_data_iterator **iterator) ;
static sw_data_result RIPCALL objdata_iterate_next(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                   /*@notnull@*/ /*@in@*/ sw_data_iterator *iterator,
                                                   /*@notnull@*/ /*@out@*/ sw_datum *key,
                                                   /*@notnull@*/ /*@out@*/ sw_datum *value) ;
static void RIPCALL objdata_iterate_end(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                        /*@notnull@*/ /*@in@*/ sw_data_iterator **iterator) ;
static sw_data_result RIPCALL objdata_open_blob(/*@notnull@*/ /*@in@*/ const sw_datum *datum,
                                                int32 mode,
                                                /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;
static sw_data_result RIPCALL objdata_pop(/*@notnull@*/ /*@in@*/ sw_datum *stack) ;
static sw_data_result RIPCALL objdata_push(/*@notnull@*/ /*@in@*/ sw_datum *stack,
                                           /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

/** The OBJECT data API definition. */
static const sw_data_api sw_data_api_objects = {
  {
    SW_DATA_API_VERSION_20130321,  /* we now support subtypes & stacks */
    (const uint8 *)"swdataobj",
    UVS("Harlequin RIP structured data OBJECT implementation"),
    sizeof(sw_datum) /* A datum is an instance of sw_data_api */
  },
  objdata_get_indexed,
  objdata_set_indexed,
  objdata_get_keyed,
  objdata_set_keyed,
  sw_data_match_generic,
  objdata_iterate_begin,
  objdata_iterate_next,
  objdata_iterate_end,
  sw_data_equal_generic,
  objdata_open_blob,
  objdata_pop,
  objdata_push
} ;

/*---------------------------------------------------------------------------*/

/* Prepare a sw_datum value representing a Postscript stack. */
sw_data_result swdatum_from_stack(sw_datum *datum, const STACK *stack)
{
  HQASSERT(datum != NULL, "No datum to fill in") ;
  HQASSERT(stack != NULL, "No stack to represent") ;

  datum->type = SW_DATUM_TYPE_ARRAY ;
  datum->subtype = SW_DATUM_SUBTYPE_STACK_ARRAY ;
  datum->flags = 0 ;
  datum->spare = 0 ;
  datum->owner = &sw_data_api_objects ;
  datum->length = stack->size + 1 ;
  datum->value.opaque = stack ;

  return SW_DATA_OK ;
}

/*---------------------------------------------------------------------------*/

/* Prepare a sw_datum value representing an object value. */
sw_data_result swdatum_from_object(sw_datum *datum, const OBJECT *object)
{
  HQASSERT(datum != NULL, "No datum to fill in") ;
  HQASSERT(object != NULL, "No object to extract") ;

  datum->type = SW_DATUM_TYPE_INVALID ;
  datum->subtype = SW_DATUM_SUBTYPE_NONE ;
  datum->flags = 0 ;
  datum->spare = 0 ;
  datum->owner = &sw_data_api_objects ;
  datum->length = 0 ;
  datum->value.opaque = NULL ;

  switch ( oType(*object) ) {
  case ONOTHING:
    HQFAIL("Why is an ONOTHING being returned?") ;
    datum->type = SW_DATUM_TYPE_NOTHING ;
    break ;
  case ONULL:
    datum->type = SW_DATUM_TYPE_NULL ;
    break ;
  case OBOOLEAN:
    datum->type = SW_DATUM_TYPE_BOOLEAN ;
    datum->value.boolean = oBool(*object) ;
    break ;
  case OINTEGER:
    datum->type = SW_DATUM_TYPE_INTEGER ;
    datum->value.integer = oInteger(*object) ;
    break ;
  case OREAL:
    datum->type = SW_DATUM_TYPE_FLOAT ;
    datum->value.real = oReal(*object) ;
    break ;
  case OINFINITY:
    datum->type = SW_DATUM_TYPE_FLOAT ;
    datum->value.real = BIGGEST_REAL ;
    break ;
  case OSTRING:
    if ( !oCanRead(*object) )
      return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;

    datum->type = SW_DATUM_TYPE_STRING ;
    datum->length = theLen(*object) ;
    datum->value.string = (const char *)oString(*object) ;
    break ;
  case OLONGSTRING:
    if ( !oCanRead(*object) )
      return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;

    datum->type = SW_DATUM_TYPE_STRING ;
    datum->length = theLSLen(*oLongStr(*object)) ;
    datum->value.string = (const char *)theLSCList(*oLongStr(*object)) ;
    break ;
  case ONAME:
    datum->type = SW_DATUM_TYPE_STRING ;
    datum->subtype = SW_DATUM_SUBTYPE_NAME_STRING ;
    datum->length = theINLen(oName(*object)) ;
    datum->value.string = (const char *)theICList(oName(*object)) ;
    break ;
  case OARRAY:
  case OPACKEDARRAY:
    /* We're not revealing the contents of the array, so don't check for
       access until indexed. */
    datum->type = SW_DATUM_TYPE_ARRAY ;
    datum->length = theLen(*object) ;
    datum->value.opaque = object ;
    break ;
  case ODICTIONARY:
    /* We're not revealing the contents of the array, so don't check for
       access until keyed. */
    datum->type = SW_DATUM_TYPE_DICT ;
    getDictLength(datum->length, object) ;
    datum->value.opaque = object ;
    break ;
  case OFILE:
    datum->type = SW_DATUM_TYPE_BLOB ;
    datum->length = theLen(*object) ; /* The filter ID of the object */
    datum->value.opaque = object ;
    break ;
  default:
    return FAILURE(SW_DATA_ERROR_TYPECHECK) ;
  }

  return SW_DATA_OK ;
}

/* Create an equivalent object from a swdatum value. */
sw_data_result object_from_swdatum(/*@notnull@*/ /*@out@*/ OBJECT *object,
                                   /*@notnull@*/ /*@in@*/ const sw_datum *datum)
{
  corecontext_t *context = get_core_context_interp();

  HQASSERT(object != NULL, "No object to fill in") ;
  HQASSERT(datum != NULL, "No datum to extract") ;

  switch ( datum->type ) {
  case SW_DATUM_TYPE_NULL:
    object_store_null(object) ;
    break ;
  case SW_DATUM_TYPE_BOOLEAN:
    object_store_bool(object, datum->value.boolean) ;
    break ;
  case SW_DATUM_TYPE_INTEGER:
    object_store_integer(object, datum->value.integer) ;
    break ;
  case SW_DATUM_TYPE_FLOAT:
    object_store_real(object, datum->value.real) ;
    break ;
  case SW_DATUM_TYPE_STRING:
    /** \todo We would like to re-accept PS owned strings without copying,
       but they may have been generated from names in the first place. It
       would be dangerous to create a string pointing into the namecache,
       because a PostScript program could change the value in a superexec
       context. */

    /* As of 20090415 we now support direct NAME creation: */
    if ( datum->subtype == SW_DATUM_SUBTYPE_NAME_STRING ) {
      NAMECACHE * name ;

      /* length is limited to 127 characters */
      if ( datum->length > (size_t)127 )
        return FAILURE(SW_DATA_ERROR_RANGECHECK) ;

      name = cachename((uint8*)datum->value.string, (uint32)datum->length) ;
      if (!name)
        return FAILURE(SW_DATA_ERROR_RANGECHECK) ;
      theTags(*object) = LITERAL | ONAME ;
      oName(*object) = name ;

    } else {

      /* size_t may be longer than int32 used in ps_long_or_normal_string. */
      if ( datum->length > (size_t)MAXINT32 )
        return FAILURE(SW_DATA_ERROR_RANGECHECK) ;

      /** \todo Yuck, this signals an error with error_handler() that may get
         ignored by the module. */
      error_clear_newerror_context(context->error) ;
      if ( !ps_long_or_normal_string(object, (uint8 *)datum->value.string,
                                     (int32)datum->length) )
        return FAILURE(sw_data_result_from_error(newerror)) ;

#if 0
      /** \todo We only need to reduce the access to read only if we don't copy
         the string. */
      if ( !object_access_reduce(READ_ONLY, object) )
        return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;
#endif
    }

    break ;
  case SW_DATUM_TYPE_ARRAY:
    if ( datum->length > MAXPSARRAY )
      return FAILURE(SW_DATA_ERROR_RANGECHECK) ;

    if (datum->subtype != SW_DATUM_SUBTYPE_NONE)
      return FAILURE(SW_DATA_ERROR_TYPECHECK) ;

    if ( datum->owner == &sw_data_api_objects ) {
      /* Array is already in PSVM memory. */
      const OBJECT *oarray = datum->value.opaque ;
      Copy(object, oarray) ;
    } else {
      /* Copy array from another owner into PSVM. */
      sw_datum key, value ;
      sw_data_iterator *iter ;
      sw_data_result result ;

      if ( !ps_array(object, (int32)datum->length) )
        return SW_DATA_ERROR_MEMORY ;

      if ( (result = sw_data_api_virtual.iterate_begin(datum, &iter)) != SW_DATA_OK )
        return result ;

#define return DO_NOT_return_break_FROM_LOOP_INSTEAD!
      while ( (result = sw_data_api_virtual.iterate_next(datum, iter, &key, &value)) == SW_DATA_OK ) {
        OBJECT oentry = OBJECT_NOTVM_NOTHING ;

        HQASSERT(key.type == SW_DATUM_TYPE_INTEGER,
                 "Iterated array key is not an integer") ;

        if ( (result = object_from_swdatum(&oentry, &value)) != SW_DATA_OK )
          break ;

        if ( oGlobalValue(*object) &&
             illegalLocalIntoGlobal(&oentry, context) ) {
          result = SW_DATA_ERROR_INVALIDACCESS ;
          break ;
        }

        if ( NOTVMOBJECT(oentry) )
          if ( !NOTVMOBJECT(*object) ) {
            result = SW_DATA_ERROR_INVALIDACCESS ;
            break ;
          }

        OCopy(oArray(*object)[key.value.integer], oentry) ;
      }

      sw_data_api_virtual.iterate_end(datum, &iter) ;
#undef return

      if ( result >= SW_DATA_ERROR )
        return result ;
    }
    break ;
  case SW_DATUM_TYPE_DICT:
    if ( datum->length > MAXPSDICT )
      return FAILURE(SW_DATA_ERROR_RANGECHECK) ;

    if ( datum->owner == &sw_data_api_objects ) {
      /* Dict is already in PSVM memory. */
      const OBJECT *odict = datum->value.opaque ;
      Copy(object, odict) ;
    } else {
      /* Copy raw array into PSVM. */
      sw_datum key, value ;
      sw_data_iterator *iter ;
      sw_data_result result ;

      if ( !ps_dictionary(object, (int32)datum->length) )
        return SW_DATA_ERROR_MEMORY ;

      if ( (result = sw_data_api_virtual.iterate_begin(datum, &iter)) != SW_DATA_OK )
        return result ;

#define return DO_NOT_return_break_FROM_LOOP_INSTEAD!
      while ( (result = sw_data_api_virtual.iterate_next(datum, iter, &key, &value)) == SW_DATA_OK ) {
        OBJECT okey = OBJECT_NOTVM_NOTHING, ovalue = OBJECT_NOTVM_NOTHING ;

        if ( (result = object_key_from_swdatum(&okey, &key)) != SW_DATA_OK ||
             (result = object_from_swdatum(&ovalue, &value)) != SW_DATA_OK )
          break ;

        /** \todo Yuck, this signals an error with error_handler() that may
            get ignored by the module. */
        error_clear_newerror_context(context->error) ;
        if ( !insert_hash_with_alloc(object, &okey, &ovalue, INSERT_HASH_NORMAL, no_dict_extension, NULL) ) {
          result = sw_data_result_from_error(newerror) ;
          break ;
        }
      }

      sw_data_api_virtual.iterate_end(datum, &iter) ;
#undef return

      if ( result >= SW_DATA_ERROR )
        return result ;
    }
    break ;
  case SW_DATUM_TYPE_BLOB:
    if ( datum->owner == &sw_data_api_objects ) {
      /* File is already in PSVM memory. */
      const OBJECT *ofile = datum->value.opaque ;
#if 0
      /* Would like to do this, but am not prepared to bring in Sw20 just for
         this one function. */
      HQASSERT(psvm_assert_check(ofile),
               "BLOB datum did not originate as PSVM") ;
#endif
      *object = *ofile ;
    } else {
      /* Can only handle OBJECT blobs. */
      return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;
    }
    break ;
  default:
    return FAILURE(SW_DATA_ERROR_TYPECHECK) ;
  }

  return SW_DATA_OK ;
}

/* Create an equivalent object from a swdatum value, suitable for use as a
   dictionary key. */
sw_data_result object_key_from_swdatum(/*@notnull@*/ /*@out@*/ OBJECT *object,
                                       /*@notnull@*/ /*@in@*/ const sw_datum *datum)
{
  HQASSERT(object != NULL, "No object to fill in") ;
  HQASSERT(datum != NULL, "No datum to extract") ;

  if ( datum->type == SW_DATUM_TYPE_STRING && datum->length <= MAXPSNAME ) {
    theTags(*object) = ONAME | LITERAL;
    SETGLOBJECTTO(*object, FALSE) ;

    if ( (oName(*object) = cachename((uint8 *)datum->value.string,
                                     (uint32)datum->length)) != NULL )
      return SW_DATA_OK ;

    /* Fallthrough to normal object creation */
  }

  return object_from_swdatum(object, datum) ;
}

/*---------------------------------------------------------------------------*/

/* Get indexed value from object array. */
static sw_data_result RIPCALL objdata_get_indexed(const sw_datum *array,
                                                  size_t index,
                                                  sw_datum *value)
{
  const OBJECT *oarray ;

  /* All parameter checking has been done by the virtual layer. Assert
     this is the case here. */
  HQASSERT(array != NULL, "No array to get indexed value from") ;
  HQASSERT(array->type == SW_DATUM_TYPE_ARRAY, "Container is not an array") ;
  HQASSERT(index < array->length, "Index out of bounds") ;
  HQASSERT(value != NULL, "Nowhere to store indexed value") ;

  if (array->subtype == SW_DATUM_SUBTYPE_STACK_ARRAY) {
    STACK *stack = (STACK*)array->value.opaque ;

    if (index > MAXINT32 || (int32)index > stack->size)
      return FAILURE(SW_DATA_ERROR_RANGECHECK) ;

    /* Note that index==0 means the top of the stack */
    return swdatum_from_object(value, stackindex((int32)index, stack)) ;
  }

  /* Check that there is a value for the array. */
  oarray = array->value.opaque ;
  HQASSERT(oarray != NULL, "No array pointer") ;

  /* Check that underlying object type is the same as the sw_datum. These are
     tests rather than assertions because of paranoia. */
  if ( oType(*oarray) != OARRAY && oType(*oarray) == OPACKEDARRAY )
    return FAILURE(SW_DATA_ERROR_TYPECHECK) ;

  /* Check the index is within bounds. */
  if ( index >= theLen(*oarray) )
    return FAILURE(SW_DATA_ERROR_RANGECHECK) ;

  if ( !oCanRead(*oarray) )
    return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;

  return swdatum_from_object(value, &oArray(*oarray)[index]) ;
}

/* Set indexed value from object array. */
static sw_data_result RIPCALL objdata_set_indexed(sw_datum *array,
                                                  size_t index,
                                                  const sw_datum *value)
{
  const OBJECT *oarray ;
  OBJECT entry = OBJECT_NOTVM_NOTHING ;
  sw_data_result result ;
  corecontext_t *corecontext = get_core_context() ;

  /* All parameter checking has been done by the virtual layer. Assert
     this is the case here. */
  HQASSERT(array != NULL, "Nowhere to store indexed value") ;
  HQASSERT(array->type == SW_DATUM_TYPE_ARRAY, "Container is not an array") ;
  HQASSERT(index < array->length, "Index out of bounds") ;
  HQASSERT(value != NULL, "No indexed value to store") ;

  if (array->subtype == SW_DATUM_SUBTYPE_STACK_ARRAY) {
    STACK *stack = (STACK*)array->value.opaque ;

    if (index > MAXINT32 || (int32)index > stack->size)
      return FAILURE(SW_DATA_ERROR_RANGECHECK) ;

    /* Note that index==0 means the top of the stack */
    return object_from_swdatum(stackindex((int32)index, stack), value) ;
  }

  /* Check that there is a value for the array. */
  oarray = array->value.opaque ;
  HQASSERT(oarray != NULL, "No array pointer") ;

  /* Check that underlying object type is the same as the sw_datum. These are
     tests rather than assertions because of paranoia. */
  if ( oType(*oarray) != OARRAY && oType(*oarray) == OPACKEDARRAY )
    return FAILURE(SW_DATA_ERROR_TYPECHECK) ;

  /* Check the index is within bounds. */
  if ( index >= theLen(*oarray) )
    return FAILURE(SW_DATA_ERROR_RANGECHECK) ;

  if ( !oCanWrite(*oarray) )
    return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;

  if ( (result = object_from_swdatum(&entry, value)) != SW_DATA_OK )
    return result ;

  if ( oGlobalValue(*oarray) && illegalLocalIntoGlobal(&entry, corecontext) )
    return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;

  if ( !check_asave(oArray(*oarray), theLen(*oarray), oGlobalValue(*oarray),
                    corecontext) )
    return FAILURE(SW_DATA_ERROR_MEMORY) ;

  OCopy(oArray(*oarray)[index], entry) ;

  return SW_DATA_OK ;
}

/* Get keyed value from object dictionary. */
static sw_data_result RIPCALL objdata_get_keyed(const sw_datum *dict,
                                                const sw_datum *key,
                                                sw_datum *value)
{
  OBJECT okey = OBJECT_NOTVM_NOTHING, *ovalue ;
  const OBJECT *odict ;
  sw_data_result result ;

  /* All parameter checking has been done by the virtual layer. Assert
     this is the case here. */
  HQASSERT(dict != NULL, "Nowhere to get keyed value from") ;
  HQASSERT(dict->type == SW_DATUM_TYPE_DICT, "Container is not a dict") ;
  HQASSERT(key != NULL, "No key") ;
  HQASSERT(key->type == SW_DATUM_TYPE_BOOLEAN ||
           key->type == SW_DATUM_TYPE_INTEGER ||
           key->type == SW_DATUM_TYPE_FLOAT ||
           key->type == SW_DATUM_TYPE_STRING, "Key is not simple type") ;
  HQASSERT(value != NULL, "No where to store keyed value") ;

  odict = dict->value.opaque ;
  HQASSERT(odict != NULL, "No dictionary pointer") ;

  /* Paranoia consistency checks between sw_datum and objects. */
  if ( oType(*odict) != ODICTIONARY )
    return FAILURE(SW_DATA_ERROR_TYPECHECK) ;

  if ( (result = object_key_from_swdatum(&okey, key)) != SW_DATA_OK )
    return result ;

  if ( !oCanRead(*oDict(*odict)) )
    return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;

  /** \todo Yuck, this signals an error with error_handler() that may get
      ignored by the module. */
  error_clear_newerror() ;
  if ( (ovalue = extract_hash(odict, &okey)) == NULL ) {
    if ( newerror )
      return sw_data_result_from_error(newerror) ;
    return SW_DATA_ERROR_UNDEFINED ;
  }

  return swdatum_from_object(value, ovalue) ;
}

/* Set keyed value in object dictionary. */
static sw_data_result RIPCALL objdata_set_keyed(sw_datum *dict,
                                                const sw_datum *key,
                                                const sw_datum *value)
{
  OBJECT okey = OBJECT_NOTVM_NOTHING, ovalue = OBJECT_NOTVM_NOTHING, *odict ;
  sw_data_result result ;

  /* All parameter checking has been done by the virtual layer. Assert
     this is the case here. */
  HQASSERT(dict != NULL, "Nowhere to store keyed value") ;
  HQASSERT(dict->type == SW_DATUM_TYPE_DICT, "Container is not a dict") ;
  HQASSERT(key != NULL, "No key") ;
  HQASSERT(key->type == SW_DATUM_TYPE_BOOLEAN ||
           key->type == SW_DATUM_TYPE_INTEGER ||
           key->type == SW_DATUM_TYPE_FLOAT ||
           key->type == SW_DATUM_TYPE_STRING, "Key is not simple type") ;
  HQASSERT(value != NULL, "No keyed value to store") ;
  HQASSERT(value->type == SW_DATUM_TYPE_NULL ||
           value->type == SW_DATUM_TYPE_BOOLEAN ||
           value->type == SW_DATUM_TYPE_INTEGER ||
           value->type == SW_DATUM_TYPE_FLOAT ||
           value->type == SW_DATUM_TYPE_STRING ||
           value->type == SW_DATUM_TYPE_ARRAY ||
           value->type == SW_DATUM_TYPE_DICT,
           "Value is not storable type") ;

  odict = (OBJECT *)dict->value.opaque ;
  HQASSERT(odict != NULL, "No dictionary pointer") ;

  if ( oType(*odict) != ODICTIONARY )
    return FAILURE(SW_DATA_ERROR_TYPECHECK) ;

  if ( !oCanWrite(*oDict(*odict)) )
    return FAILURE(SW_DATA_ERROR_INVALIDACCESS) ;

  if ( (result = object_key_from_swdatum(&okey, key)) != SW_DATA_OK ||
       (result = object_from_swdatum(&ovalue, value)) != SW_DATA_OK )
    return result ;

  /** \todo Yuck, this signals an error with error_handler() that may
      get ignored by the module. */
  error_clear_newerror() ;
  if ( !insert_hash_with_alloc(odict, &okey, &ovalue, INSERT_HASH_NORMAL, no_dict_extension, NULL) )
    return sw_data_result_from_error(newerror) ;

  return SW_DATA_OK ;
}

/** Sub-class of data API iterator specialised to core OBJECT types. */
struct sw_data_iterator {
  OBJECT *section ;    /**< Start of section array entry. */
  uint32 remaining ;   /**< Entries left in this section. */
  union {
    DPAIR *pair ;        /**< Next dictionary key-value */
    OBJECT *entry ;      /**< Next array entry. */
  } next ;             /**< Next entry to look at. */
#ifdef ASSERT_BUILD
  const OBJECT *root ; /**< Original object to iterate. */
#endif
} ;

/** Implement iteration start for core OBJECT handler. */
static sw_data_result RIPCALL objdata_iterate_begin(const sw_datum *composite,
                                                    sw_data_iterator **iterator)
{
  sw_data_iterator *iter ;
  const OBJECT *object ;

  HQASSERT(composite != NULL, "No composite object to iterate") ;
  HQASSERT(composite->type == SW_DATUM_TYPE_DICT ||
           composite->type == SW_DATUM_TYPE_ARRAY,
           "Iteration object is not composite") ;
  HQASSERT(composite->value.opaque, "Composite object has no value") ;
  HQASSERT(iterator != NULL, "Nowhere to store iterator") ;

  if ( (iter = mm_alloc(mm_pool_temp, sizeof(struct sw_data_iterator), MM_ALLOC_CLASS_SW_DATUM)) == NULL )
    return FAILURE(SW_DATA_ERROR_MEMORY) ;

  object = (OBJECT *)composite->value.opaque ;

  /* Check that the type of the composite can be iterated and fill in the
     data to iterate the object. */
  if ( composite->type == SW_DATUM_TYPE_ARRAY &&
       composite->subtype == SW_DATUM_SUBTYPE_NONE &&
       (oType(*object) == OARRAY || oType(*object) == OPACKEDARRAY) ) {
    HQASSERT(composite->length == theLen(*object),
             "Array length does not match proxy") ;
    iter->section = iter->next.entry = oArray(*object) ;
    iter->remaining = theLen(*object) ;
  } else if ( composite->type == SW_DATUM_TYPE_DICT &&
              oType(*object) == ODICTIONARY ) {
    iter->section = oDict(*object) ;
    iter->remaining = DICT_ALLOC_LEN(iter->section);
    iter->next.pair = (DPAIR *)(iter->section + 1) ;
  } else {
    mm_free(mm_pool_temp, iter, sizeof(struct sw_data_iterator)) ;
    return FAILURE(SW_DATA_ERROR_TYPECHECK) ;
  }
#ifdef ASSERT_BUILD
  iter->root = object ;
#endif

  *iterator = iter ;

  return SW_DATA_OK ;
}

/** Implement iteration step for core OBJECT handler. */
static sw_data_result RIPCALL objdata_iterate_next(const sw_datum *composite,
                                                   sw_data_iterator *iterator,
                                                   sw_datum *key,
                                                   sw_datum *value)
{
  const OBJECT *object ;

  HQASSERT(composite != NULL, "No composite object to iterate") ;
  HQASSERT(composite->type == SW_DATUM_TYPE_DICT ||
           composite->type == SW_DATUM_TYPE_ARRAY,
           "Iteration object is not composite") ;
  HQASSERT(composite->value.opaque, "Composite object has no value") ;
  HQASSERT(iterator != NULL, "No iterator") ;
  HQASSERT(key != NULL, "Nowhere to store key") ;
  HQASSERT(value != NULL, "Nowhere to store value") ;

  object = (OBJECT *)composite->value.opaque ;
  HQASSERT(iterator->root == object, "Iterator doesn't match object iterated") ;

  if ( composite->type == SW_DATUM_TYPE_ARRAY &&
       (oType(*object) == OARRAY || oType(*object) == OPACKEDARRAY) ) {
    if ( iterator->remaining == 0 )
      return SW_DATA_FINISHED ;

    key->type = SW_DATUM_TYPE_INTEGER ;
    key->owner = &sw_data_api_objects ;
    key->length = 0 ;
    key->value.integer = CAST_PTRDIFFT_TO_INT32(iterator->next.entry - oArray(*object)) ;
    iterator->remaining-- ;

    return swdatum_from_object(value, iterator->next.entry++) ;
  } else if ( composite->type == SW_DATUM_TYPE_DICT &&
              oType(*object) == ODICTIONARY ) {
    for (;;) {
      DPAIR *pair ;

      /* If finished scanning this dictionary, look at any extension dicts. */
      while ( iterator->remaining == 0 ) {
        OBJECT *extension = iterator->section - 1 ;
        /* Bail out if there are no more values to be had. */
        if ( oType(*extension) == ONOTHING )
          return SW_DATA_FINISHED ;

        iterator->section = oDict(*extension) ;
        iterator->remaining = DICT_ALLOC_LEN(iterator->section);
        iterator->next.pair = (DPAIR *)(iterator->section + 1) ;
      }

      pair = iterator->next.pair++ ;
      iterator->remaining-- ;

      /* We're going to ignore values of types we cannot translate. */
      switch ( oType(theIKey(pair)) ) {
      case ONULL:
      case OBOOLEAN:
      case OINTEGER:
      case OREAL:
      case OINFINITY:
      case OSTRING:
      case ONAME:
      case OLONGSTRING:
      case OARRAY:
      case OPACKEDARRAY:
      case ODICTIONARY:
      case OFILE:
        {
          sw_data_result result ;

          if ( (result = swdatum_from_object(key, &theIKey(pair))) != SW_DATA_OK ||
               (result = swdatum_from_object(value, &theIObject(pair))) != SW_DATA_OK )
            return result ;

          return SW_DATA_OK ;
        }
      }
    }
  } else
    return FAILURE(SW_DATA_ERROR_TYPECHECK) ;
}

/** Implement end of iterator for core OBJECT handler. */
static void RIPCALL objdata_iterate_end(const sw_datum *composite,
                                        sw_data_iterator **iterator)
{
  UNUSED_PARAM(const sw_datum *, composite) ;

  HQASSERT(composite != NULL, "No composite object to iterate") ;
  HQASSERT(composite->type == SW_DATUM_TYPE_DICT ||
           composite->type == SW_DATUM_TYPE_ARRAY,
           "Iteration object is not composite") ;
  HQASSERT(composite->value.opaque, "Composite object has no value") ;
  HQASSERT(iterator != NULL, "No iterator") ;
  HQASSERT(*iterator != NULL, "No iterator") ;
  HQASSERT((*iterator)->root == composite->value.opaque, "Iterator doesn't match object iterated") ;

  mm_free(mm_pool_temp, *iterator, sizeof(struct sw_data_iterator)) ;

  *iterator = NULL ;
}

static sw_data_result RIPCALL objdata_open_blob(const sw_datum *datum,
                                                int32 mode,
                                                sw_blob_instance **blob)
{
  sw_blob_result result ;

  HQASSERT(datum != NULL, "No datum for blob") ;
  HQASSERT(datum->type == SW_DATUM_TYPE_BLOB, "Datum is not a blob") ;
  HQASSERT(datum->value.opaque, "Datum no value") ;
  HQASSERT(blob != NULL, "Nowhere to put blob") ;

  if ( (result = blob_from_object((OBJECT *)datum->value.opaque, mode,
                                  global_blob_store, blob)) != SW_BLOB_OK )
    return FAILURE(sw_data_result_from_error(error_from_sw_blob_result(result))) ;

  return SW_DATA_OK ;
}

static sw_data_result RIPCALL objdata_pop(/*@notnull@*/ /*@in@*/ sw_datum *datum)
{
  STACK *stack ;

  HQASSERT(datum != NULL, "No datum") ;
  HQASSERT(datum->type == SW_DATUM_TYPE_ARRAY &&
           datum->subtype == SW_DATUM_SUBTYPE_STACK_ARRAY, "Datum is not a stack") ;

  stack = (STACK*)datum->value.opaque ;
  HQASSERT(stack != NULL, "No stack") ;
  HQASSERT(datum->length == (size_t)(stack->size + 1), "Stack/datum mismatch") ;

  if (stack->size < 0) {
    return FAILURE(SW_DATA_ERROR_RANGECHECK) ;
  }

  pop(stack) ;
  datum->length = stack->size + 1 ;

  return SW_DATA_OK ;
}

static sw_data_result RIPCALL objdata_push(/*@notnull@*/ /*@in@*/ sw_datum *datum,
                                           /*@notnull@*/ /*@in@*/ const sw_datum *value)
{
  OBJECT object = OBJECT_NOTVM_NOTHING ;
  sw_data_result result ;
  STACK *stack ;

  HQASSERT(datum != NULL, "No datum") ;
  HQASSERT(datum->type == SW_DATUM_TYPE_ARRAY &&
           datum->subtype == SW_DATUM_SUBTYPE_STACK_ARRAY, "Datum is not a stack") ;

  stack = (STACK*)datum->value.opaque ;
  HQASSERT(stack != NULL, "No stack") ;
  HQASSERT(datum->length == (size_t)(stack->size + 1), "Stack/datum mismatch") ;

  result = object_from_swdatum(&object, value) ;
  if (result >= SW_DATA_ERROR)
    return result ;

  error_clear_newerror() ;
  if (!push(&object, stack))
    sw_data_result_from_error(newerror) ;
  datum->length = stack->size + 1 ;

  return SW_DATA_OK ;
}

/* Log stripped */
