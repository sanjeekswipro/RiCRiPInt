/** \file
 * \ingroup datum
 *
 * $HopeName: COREdatum!src:swdataraw.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface connecting raw sw_datum to structured data API.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"

#include "uvms.h"
#include "ripcall.h"
#include "swdataapi.h"
#include "swdataimpl.h"
#include "swdataraw.h"
#include "hqmemcmp.h"
#include "mm.h"

/* Simple type ordering function for sw_datum. */
int CRT_API sw_datum_cmp(const void *pa, const void *pb)
{
  const sw_datum *a = pa ;
  const sw_datum *b = pb ;

  HQASSERT(a != NULL && b != NULL, "Missing sw_datum to compare") ;

  if ( a->type != b->type )
    return a->type - b->type ;

  switch ( a->type ) {
  case SW_DATUM_TYPE_NOTHING: /* No value for null or nothing. */
  case SW_DATUM_TYPE_NULL:
  case SW_DATUM_TYPE_INVALID:
    return 0 ;
  case SW_DATUM_TYPE_BOOLEAN:
    return a->value.boolean - b->value.boolean ;
  case SW_DATUM_TYPE_INTEGER:
    return a->value.integer - b->value.integer ;
  case SW_DATUM_TYPE_FLOAT:
    if ( a->value.real < b->value.real )
      return -1 ;
    if ( a->value.real > b->value.real )
      return 1 ;
    return 0 ;
  case SW_DATUM_TYPE_STRING:
    if ( a->value.string == b->value.string )
      return (int32)a->length - (int32)b->length ;

    return HqMemCmp((const uint8 *)a->value.string, (int32)a->length,
                    (const uint8 *)b->value.string, (int32)b->length) ;
  case SW_DATUM_TYPE_ARRAY:
  case SW_DATUM_TYPE_DICT:
  case SW_DATUM_TYPE_BLOB:
    break ;
  default:
    HQFAIL("Cannot compare invalid types") ;
  }

  /* Complex types use the opaque pointer. */
  if ( (uintptr_t)a->value.opaque < (uintptr_t)b->value.opaque )
    return -1 ;

  if ( (uintptr_t)a->value.opaque > (uintptr_t)b->value.opaque )
    return 1 ;

  return 0 ;
}

/* Get indexed value from raw array. */
static sw_data_result RIPCALL swdatum_get_indexed(/*@notnull@*/ /*@in@*/ const sw_datum *array,
                                                  size_t index,
                                                  /*@notnull@*/ /*@out@*/ sw_datum *value)
{
  const sw_datum *entries ;

  /* All parameter checking has been done by the virtual layer. Assert
     this is the case here. */
  HQASSERT(array != NULL, "Nowhere to get indexed value from") ;
  HQASSERT(array->type == SW_DATUM_TYPE_ARRAY, "Container is not an array") ;
  HQASSERT(index < array->length, "Index out of bounds") ;
  HQASSERT(value != NULL, "No where to store indexed value") ;

  /* Check that there is a value for the array. */
  entries = array->value.opaque ;
  HQASSERT(entries != NULL, "No array pointer") ;

  *value = entries[index] ;

  return SW_DATA_OK ;
}

/* Set indexed value from raw array. */
static sw_data_result RIPCALL swdatum_set_indexed(/*@notnull@*/ /*@in@*/ sw_datum *array,
                                                  size_t index,
                                                  /*@notnull@*/ /*@in@*/ const sw_datum *value)
{
  sw_datum *entries ;

  /* All parameter checking has been done by the virtual layer. Assert
     this is the case here. */
  HQASSERT(array != NULL, "No where to set indexed value") ;
  HQASSERT(array->type == SW_DATUM_TYPE_ARRAY, "Container is not an array") ;
  HQASSERT(index < array->length, "Index out of bounds") ;
  HQASSERT(value != NULL, "No indexed value to store") ;

  /* Check that there is a value for the array. */
  entries = (sw_datum *)array->value.opaque ;
  HQASSERT(entries != NULL, "No array pointer") ;

  /* Note that we can store any other type of value into a raw sw_datum
     container, but in generate the converse is not true. Raw sw_datum
     values will be usually copied or converted to some other format before
     storing into other owners' containers. */
  entries[index] = *value ;

  return SW_DATA_OK ;
}

/* Get keyed value from raw dictionary. */
static sw_data_result RIPCALL swdatum_get_keyed(/*@notnull@*/ /*@in@*/ const sw_datum *dict,
                                                /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                                /*@notnull@*/ /*@out@*/ sw_datum *value)
{
  const sw_datum *entries ;
  size_t length ;

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

  entries = dict->value.opaque ;
  HQASSERT(entries != NULL, "No dictionary pointer") ;

  /* Raw sw_datum dicts are pairs of sw_datum for key,value. Search for the
     key as the first of a pair. */
  for ( length = dict->length ; length > 0 ; --length, entries += 2 ) {
    if ( sw_datum_cmp(&entries[0], key) == 0 ) {
      *value = entries[1] ;
      return SW_DATA_OK ;
    }
  }

  return SW_DATA_ERROR_UNDEFINED ;
}

/* Set keyed value in raw dictionary. */
static sw_data_result RIPCALL swdatum_set_keyed(/*@notnull@*/ /*@in@*/ sw_datum *dict,
                                                /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                                /*@notnull@*/ /*@in@*/ const sw_datum *value)
{
  sw_datum *entries ;
  size_t length ;

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

  HQASSERT(dict->value.opaque != NULL, "No dictionary pointer") ;

  /* Note that we can store any other type of value into a raw sw_datum
     container, but in generate the converse is not true. Raw sw_datum
     values will be usually copied or converted to some other format before
     storing into other owners' containers. */

  /* Raw sw_datum dicts are pairs of sw_datum for key,value. Search for the key
     as first of a pair. */
  for ( length = dict->length, entries = (sw_datum *)dict->value.opaque ;
        length > 0 ;
        --length, entries += 2 ) {
    if ( sw_datum_cmp(&entries[0], key) == 0 ) {
      entries[1] = *value ;
      return SW_DATA_OK ;
    }
  }

  /* We didn't find the key in the dictionary. Search for an empty slot to
     stick the value in. */
  for ( length = dict->length, entries = (sw_datum *)dict->value.opaque ;
        length > 0 ;
        --length, entries += 2 ) {
    if ( entries[0].type == SW_DATUM_TYPE_NOTHING ) {
      entries[0] = *key ;
      entries[1] = *value ;
      return SW_DATA_OK ;
    }
  }

  /* Nowhere to put it, sorry. */
  return SW_DATA_ERROR_MEMORY ;
}

/* Definition of sw_data_iterator for sw_datum raw handler. */
struct sw_data_iterator {
  const sw_datum *current ; /**< The current item to iterate. */
  size_t remaining ;        /**< Number of remaining items to iterate. */
#if defined(ASSERT_BUILD)
  const sw_datum *root ;    /**< The object being iterated. */
#endif
} ;

/** Implement iteration start for sw_datum type handler. */
static sw_data_result RIPCALL swdatum_iterate_begin(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                    /*@notnull@*/ /*@out@*/ sw_data_iterator **iterator)
{
  sw_data_iterator *iter ;

  HQASSERT(composite != NULL, "No composite object to iterate") ;
  HQASSERT(composite->type == SW_DATUM_TYPE_DICT ||
           composite->type == SW_DATUM_TYPE_ARRAY,
           "Iteration object is not composite") ;
  HQASSERT(composite->value.opaque, "Composite object has no value") ;
  HQASSERT(iterator != NULL, "Nowhere to store iterator") ;

  if ( (iter = mm_alloc(mm_pool_temp, sizeof(struct sw_data_iterator), MM_ALLOC_CLASS_SW_DATUM)) == NULL )
    return SW_DATA_ERROR_MEMORY ;

  iter->current = composite->value.opaque ;
  iter->remaining = composite->length ;
#if defined(ASSERT_BUILD)
  iter->root = composite ;
#endif

  *iterator = iter ;

  return SW_DATA_OK ;
}

/** Implement iteration step for sw_datum type handler. */
static sw_data_result RIPCALL swdatum_iterate_next(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                   /*@notnull@*/ /*@in@*/ sw_data_iterator *iterator,
                                                   /*@notnull@*/ /*@out@*/ sw_datum *key,
                                                   /*@notnull@*/ /*@out@*/ sw_datum *value)
{
  HQASSERT(composite != NULL, "No composite object to iterate") ;
  HQASSERT(composite->type == SW_DATUM_TYPE_DICT ||
           composite->type == SW_DATUM_TYPE_ARRAY,
           "Iteration object is not composite") ;
  HQASSERT(composite->value.opaque, "Composite object has no value") ;
  HQASSERT(iterator != NULL, "No iterator") ;
  HQASSERT(key != NULL, "Nowhere to store key") ;
  HQASSERT(value != NULL, "Nowhere to store value") ;

  HQASSERT(iterator->root == composite, "Iterator doesn't match object iterated") ;

  /* Bail out with undefined if there are no more values to be had. */
  if ( iterator->remaining == 0 )
    return SW_DATA_FINISHED ;

  if ( composite->type == SW_DATUM_TYPE_DICT ) {
    *key = *iterator->current++ ;
  } else if ( composite->type == SW_DATUM_TYPE_ARRAY ) {
    key->type = SW_DATUM_TYPE_INTEGER ;
    key->owner = NULL ;
    key->length = 0 ;
    key->value.integer = CAST_SIZET_TO_UINT32(composite->length - iterator->remaining) ;
  } else
    return SW_DATA_ERROR_TYPECHECK ;

  *value = *iterator->current++ ;
  iterator->remaining -= 1 ;

  return SW_DATA_OK ;
}

/** Implement end of iterator for sw_datum type handler. */
static void RIPCALL swdatum_iterate_end(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                        /*@notnull@*/ /*@in@*/ sw_data_iterator **iterator)
{
  UNUSED_PARAM(const sw_datum *, composite) ;

  HQASSERT(composite != NULL, "No composite object to iterate") ;
  HQASSERT(composite->type == SW_DATUM_TYPE_DICT ||
           composite->type == SW_DATUM_TYPE_ARRAY,
           "Iteration object is not composite") ;
  HQASSERT(composite->value.opaque, "Composite object has no value") ;
  HQASSERT(iterator != NULL, "No iterator") ;
  HQASSERT((*iterator)->root == composite, "Iterator doesn't match object iterated") ;

  mm_free(mm_pool_temp, *iterator, sizeof(struct sw_data_iterator)) ;

  *iterator = NULL ;
}

static sw_data_result RIPCALL swdatum_open_blob(const sw_datum *datum,
                                                int32 mode,
                                                sw_blob_instance **blob)
{
  UNUSED_PARAM(const sw_datum *, datum) ;
  UNUSED_PARAM(int32, mode) ;
  UNUSED_PARAM(sw_blob_instance **, blob) ;

  /* There is no such thing as a raw blob. */
  return SW_DATA_ERROR_INVALIDACCESS ;
}

/* The raw data API definition. */
const sw_data_api sw_data_api_raw = {
  {
    SW_DATA_API_VERSION_20071111,
    (const uint8 *)"swdataraw",
    UVS("Harlequin RIP structured data raw implementation"),
    sizeof(sw_datum) /* A datum is an instance of sw_data_api */
  },
  swdatum_get_indexed,
  swdatum_set_indexed,
  swdatum_get_keyed,
  swdatum_set_keyed,
  sw_data_match_generic,
  swdatum_iterate_begin,
  swdatum_iterate_next,
  swdatum_iterate_end,
  sw_data_equal_generic,
  swdatum_open_blob,
} ;

/* Log stripped */
