/** \file
 * \ingroup datum
 *
 * $HopeName: COREdatum!src:swdatavirtual.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Generic routing and typechecking for structured data API.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "coreinit.h"

#include "rdrapi.h"
#include "apis.h"
#include "uvms.h"
#include "ripcall.h"
#include "swdataapi.h"
#include "swdataimpl.h"
#include "swdataraw.h"
#include "swblobapi.h"
#include "swerrors.h"
#include "hqmemcmp.h"

/* Translate a PS error to a SW data error. */
sw_data_result sw_data_result_from_error(int32 errorno)
{
  switch ( errorno ) {
  case VMERROR:
  case DICTFULL:
    return SW_DATA_ERROR_MEMORY ;
  case INVALIDACCESS:
    return SW_DATA_ERROR_INVALIDACCESS ;
  case RANGECHECK:
  case LIMITCHECK:
  case STACKOVERFLOW:
  case EXECSTACKOVERFLOW:
  case DICTSTACKOVERFLOW:
    return SW_DATA_ERROR_RANGECHECK ;
  case TYPECHECK:
    return SW_DATA_ERROR_TYPECHECK ;
  case UNDEFINED:
    return SW_DATA_ERROR_UNDEFINED ;
  case SYNTAXERROR:
    return SW_DATA_ERROR_SYNTAX ;
  }
  HQFAIL("PS error not translated; failing with invalid access") ;
  return SW_DATA_ERROR_INVALIDACCESS ;
}

/* Translate a SW data error to a PS error. */
int32 error_from_sw_data_result(sw_data_result errorno)
{
  switch ( errorno ) {
  case SW_DATA_ERROR_MEMORY:
    return VMERROR ;
  case SW_DATA_ERROR_RANGECHECK:
    return RANGECHECK ;
  case SW_DATA_ERROR_TYPECHECK:
    return TYPECHECK ;
  case SW_DATA_ERROR_UNDEFINED:
    return UNDEFINED ;
  case SW_DATA_ERROR_INVALIDACCESS:
    return INVALIDACCESS ;
  case SW_DATA_ERROR_SYNTAX:
    return SYNTAXERROR ;
  }
  HQFAIL("sw_data_result not translated; failing with unregistered") ;
  return UNREGISTERED ;
}

/*---------------------------------------------------------------------------*/

/** Entry point for (*get_indexed) in virtual data handler. */
static sw_data_result RIPCALL virtual_get_indexed(/*@notnull@*/ /*@in@*/ const sw_datum *array,
                                                  size_t index,
                                                  /*@notnull@*/ /*@out@*/ sw_datum *value)
{
  const sw_data_api *owner ;

  /* The source and destination must be specified. */
  if ( array == NULL || value == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  /* Only arrays can be indexed. */
  if ( array->type != SW_DATUM_TYPE_ARRAY )
    return SW_DATA_ERROR_TYPECHECK ;

  /* Check that there is a value for the array. */
  if ( array->value.opaque == NULL )
    return SW_DATA_ERROR_INVALIDACCESS ;

  /* Check the index is within bounds. */
  if ( index >= array->length )
    return SW_DATA_ERROR_RANGECHECK ;

  if ( (owner = array->owner) != NULL ) {
    if (owner->get_indexed == NULL) {
      HQFAIL("Data sub-class missing get_indexed method") ;
      return SW_DATA_ERROR_INVALIDACCESS ;
    }
    return owner->get_indexed(array, index, value) ;
  }

  /* Default to raw sw_datum access */
  return sw_data_api_raw.get_indexed(array, index, value) ;
}

static sw_data_result RIPCALL virtual_set_indexed(/*@notnull@*/ /*@in@*/ sw_datum *array,
                                                 size_t index,
                                                 /*@notnull@*/ /*@in@*/ const sw_datum *value)
{
  const sw_data_api *owner ;

  /* The source and destination must be specified. */
  if ( array == NULL || value == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  /* Only arrays can be indexed. */
  if ( array->type != SW_DATUM_TYPE_ARRAY )
    return SW_DATA_ERROR_TYPECHECK ;

  /* Check that there is a value for the array. */
  if ( array->value.opaque == NULL )
    return SW_DATA_ERROR_INVALIDACCESS ;

  /* Check the index is within bounds. */
  if ( index >= array->length )
    return SW_DATA_ERROR_RANGECHECK ;

  /* Check that the value is an allowable type. */
  switch ( value->type ) {
  case SW_DATUM_TYPE_NULL:
  case SW_DATUM_TYPE_BOOLEAN:
  case SW_DATUM_TYPE_INTEGER:
  case SW_DATUM_TYPE_FLOAT:
  case SW_DATUM_TYPE_STRING:
  case SW_DATUM_TYPE_DICT:
    break ;
  case SW_DATUM_TYPE_ARRAY:
    if ( value->subtype == SW_DATUM_SUBTYPE_NONE )
      break ;
    /* drop through */
  default:
    return SW_DATA_ERROR_TYPECHECK ;
  }

  if ( (owner = array->owner) != NULL ) {
    if (owner->set_indexed == NULL) {
      HQFAIL("Data sub-class missing set_indexed method") ;
      return SW_DATA_ERROR_INVALIDACCESS ;
    }
    return owner->set_indexed(array, index, value) ;
  }

  /* Default to raw sw_datum access */
  return sw_data_api_raw.set_indexed(array, index, value) ;
}

static sw_data_result RIPCALL virtual_get_keyed(/*@notnull@*/ /*@in@*/ const sw_datum *dict,
                                                /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                                /*@notnull@*/ /*@out@*/ sw_datum *value)
{
  const sw_data_api *owner ;

  /* The source, destination, and key must be specified. */
  if ( dict == NULL || key == NULL || value == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  /* Only dictionaries can be keyed. */
  if ( dict->type != SW_DATUM_TYPE_DICT )
    return SW_DATA_ERROR_TYPECHECK ;

  /* Check that there is a value for the dict. */
  if ( dict->value.opaque == NULL )
    return SW_DATA_ERROR_INVALIDACCESS ;

  /* Check the key is a simple type. */
  switch ( key->type ) {
  case SW_DATUM_TYPE_BOOLEAN:
  case SW_DATUM_TYPE_INTEGER:
  case SW_DATUM_TYPE_FLOAT:
  case SW_DATUM_TYPE_STRING:
    break ;
  default:
    return SW_DATA_ERROR_TYPECHECK ;
  }

  if ( (owner = dict->owner) != NULL ) {
    if (owner->get_keyed == NULL) {
      HQFAIL("Data sub-class missing get_keyed method") ;
      return SW_DATA_ERROR_INVALIDACCESS ;
    }
    return owner->get_keyed(dict, key, value) ;
  }

  /* Default to raw sw_datum access */
  return sw_data_api_raw.get_keyed(dict, key, value) ;
}

static sw_data_result RIPCALL virtual_set_keyed(/*@notnull@*/ /*@in@*/ sw_datum *dict,
                                               /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                               /*@notnull@*/ /*@in@*/ const sw_datum *value)
{
  const sw_data_api *owner ;

  /* The source, destination, and key must be specified. */
  if ( dict == NULL || key == NULL || value == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  /* Only dictionaries can be keyed. */
  if ( dict->type != SW_DATUM_TYPE_DICT )
    return SW_DATA_ERROR_TYPECHECK ;

  /* Check that there is a value for the dict. */
  if ( dict->value.opaque == NULL )
    return SW_DATA_ERROR_INVALIDACCESS ;

  /* Check the key is a simple type. */
  switch ( key->type ) {
  case SW_DATUM_TYPE_BOOLEAN:
  case SW_DATUM_TYPE_INTEGER:
  case SW_DATUM_TYPE_FLOAT:
  case SW_DATUM_TYPE_STRING:
    break ;
  default:
    return SW_DATA_ERROR_TYPECHECK ;
  }

  /* Check that the value is an allowable type. */
  switch ( value->type ) {
  case SW_DATUM_TYPE_NULL:
  case SW_DATUM_TYPE_BOOLEAN:
  case SW_DATUM_TYPE_INTEGER:
  case SW_DATUM_TYPE_FLOAT:
  case SW_DATUM_TYPE_STRING:
  case SW_DATUM_TYPE_DICT:
    break ;
  case SW_DATUM_TYPE_ARRAY:
    if ( value->subtype == SW_DATUM_SUBTYPE_NONE )
      break ;
    /* drop through */
  default:
    return SW_DATA_ERROR_TYPECHECK ;
  }

  if ( (owner = dict->owner) != NULL ) {
    if (owner->set_keyed == NULL) {
      HQFAIL("Data sub-class missing set_keyed method") ;
      return SW_DATA_ERROR_INVALIDACCESS ;
    }
    return owner->set_keyed(dict, key, value) ;
  }

  /* Default to raw sw_datum access */
  return sw_data_api_raw.set_keyed(dict, key, value) ;
}

static sw_data_result RIPCALL virtual_match(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                            /*@notnull@*/ /*@out@*/ sw_data_match match[],
                                            size_t match_length)
{
  const sw_data_api *owner ;

  if ( composite == NULL || match == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  /* Check that the type of the composite can be matched. */
  if ( composite->type != SW_DATUM_TYPE_DICT &&
       composite->type != SW_DATUM_TYPE_ARRAY )
    return SW_DATA_ERROR_TYPECHECK ;

  /* Check that there is a value for the composite object. */
  if ( composite->value.opaque == NULL )
    return SW_DATA_ERROR_INVALIDACCESS ;

  if ( (owner = composite->owner) != NULL ) {
    if (owner->match == NULL) {
      HQFAIL("Data sub-class missing match method") ;
      return SW_DATA_ERROR_INVALIDACCESS ;
    }
    return owner->match(composite, match, match_length) ;
  }

  /* Default to raw sw_datum access */
  return sw_data_api_raw.match(composite, match, match_length) ;
}

static sw_data_result RIPCALL virtual_iterate_begin(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                    /*@notnull@*/ /*@out@*/ sw_data_iterator **iterator)
{
  const sw_data_api *owner ;

  if ( composite == NULL || iterator == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  /* Check that the type of the composite can be iterated. */
  if ( composite->type != SW_DATUM_TYPE_DICT &&
       composite->type != SW_DATUM_TYPE_ARRAY )
    return SW_DATA_ERROR_TYPECHECK ;

  /* Check that there is a value for the composite object. */
  if ( composite->value.opaque == NULL )
    return SW_DATA_ERROR_INVALIDACCESS ;

  if ( (owner = composite->owner) != NULL ) {
    if (owner->iterate_begin == NULL) {
      HQFAIL("Data sub-class missing iterate_begin method") ;
      return SW_DATA_ERROR_INVALIDACCESS ;
    }
    return owner->iterate_begin(composite, iterator) ;
  }

  /* Default to raw sw_datum access */
  return sw_data_api_raw.iterate_begin(composite, iterator) ;
}

static sw_data_result RIPCALL virtual_iterate_next(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                   /*@notnull@*/ /*@in@*/ sw_data_iterator *iterator,
                                                   /*@notnull@*/ /*@out@*/ sw_datum *key,
                                                   /*@notnull@*/ /*@out@*/ sw_datum *value)
{
  const sw_data_api *owner ;

  if ( composite == NULL || iterator == NULL || key == NULL || value == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  /* Check that the type of the composite can be iterated. */
  if ( composite->type != SW_DATUM_TYPE_DICT &&
       composite->type != SW_DATUM_TYPE_ARRAY )
    return SW_DATA_ERROR_TYPECHECK ;

  /* Check that there is a value for the composite object. */
  if ( composite->value.opaque == NULL )
    return SW_DATA_ERROR_INVALIDACCESS ;

  if ( (owner = composite->owner) != NULL ) {
    if (owner->iterate_next == NULL) {
      HQFAIL("Data sub-class missing iterate_next method") ;
      return SW_DATA_ERROR_INVALIDACCESS ;
    }
    return owner->iterate_next(composite, iterator, key, value) ;
  }

  /* Default to raw sw_datum access */
  return sw_data_api_raw.iterate_next(composite, iterator, key, value) ;
}

static void RIPCALL virtual_iterate_end(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                        /*@notnull@*/ /*@in@*/ sw_data_iterator **iterator)
{
  const sw_data_api *owner ;

  if ( composite == NULL || iterator == NULL )
    return ;

  /* Check that the type of the composite can be iterated. */
  if ( composite->type != SW_DATUM_TYPE_DICT &&
       composite->type != SW_DATUM_TYPE_ARRAY )
    return ;

  /* Check that there is a value for the composite object. */
  if ( composite->value.opaque == NULL )
    return ;

  if ( (owner = composite->owner) != NULL ) {
    if (owner->iterate_end == NULL) {
      HQFAIL("Data sub-class missing iterate_end method") ;
      return ;
    }
    owner->iterate_end(composite, iterator) ;
    return ;
  }

  /* Default to raw sw_datum access */
  sw_data_api_raw.iterate_end(composite, iterator) ;
}

static HqBool RIPCALL virtual_equal(/*@notnull@*/ /*@in@*/ const sw_datum *one,
                                    /*@notnull@*/ /*@in@*/ const sw_datum *two)
{
  if ( one == NULL || two == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  return sw_data_equal_generic(one, two) ;
}

static sw_data_result RIPCALL virtual_open_blob(/*@notnull@*/ /*@in@*/ const sw_datum *datum,
                                                int32 mode,
                                                /*@notnull@*/ /*@out@*/ sw_blob_instance **blob)
{
  const sw_data_api *owner ;

  if ( datum == NULL || blob == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  if ( datum->type != SW_DATUM_TYPE_BLOB )
    return SW_DATA_ERROR_TYPECHECK ;

  /* Check that there is a value for the composite object. */
  if ( datum->value.opaque == NULL )
    return SW_DATA_ERROR_INVALIDACCESS ;

  if ( (owner = datum->owner) != NULL ) {
    if (owner->open_blob == NULL) {
      HQFAIL("Data sub-class missing open_blob method") ;
      return SW_DATA_ERROR_INVALIDACCESS ;
    }
    return owner->open_blob(datum, mode, blob) ;
  }

  /* Default to raw sw_datum access */
  return sw_data_api_raw.open_blob(datum, mode, blob) ;
}

static sw_data_result RIPCALL virtual_pop(/*@notnull@*/ /*@in@*/ sw_datum *datum)
{
  const sw_data_api *owner ;

  if ( datum == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  if ( datum->type != SW_DATUM_TYPE_ARRAY ||
       datum->subtype != SW_DATUM_SUBTYPE_STACK_ARRAY )
    return SW_DATA_ERROR_TYPECHECK ;

  if ( (owner = datum->owner) != NULL &&
       owner->info.version > SW_DATA_API_VERSION_20090415 &&
       owner->pop != NULL )
    return owner->pop(datum) ;

  return SW_DATA_ERROR_INVALIDACCESS ;
}

static sw_data_result RIPCALL virtual_push(/*@notnull@*/ /*@in@*/ sw_datum *datum,
                                           /*@notnull@*/ /*@in@*/ const sw_datum *value)
{
  const sw_data_api *owner ;

  if ( datum == NULL || value == NULL )
    return SW_DATA_ERROR_SYNTAX ;

  if ( datum->type != SW_DATUM_TYPE_ARRAY ||
       datum->subtype != SW_DATUM_SUBTYPE_STACK_ARRAY ||
       value->type == SW_DATUM_TYPE_NOTHING ||
       value->type == SW_DATUM_TYPE_INVALID )
    return SW_DATA_ERROR_TYPECHECK ;

  if ( (owner = datum->owner) != NULL &&
       owner->info.version > SW_DATA_API_VERSION_20090415 &&
       owner->push != NULL )
    return owner->push(datum, value) ;

  return SW_DATA_ERROR_INVALIDACCESS ;
}

/*---------------------------------------------------------------------------*/

/* The virtual data handler API. */
const sw_data_api sw_data_api_virtual = {
  {
    SW_DATA_API_VERSION_20130321,
    (const uint8 *)"swdatavirtual",
    UVS("Harlequin RIP structured data API"),
    sizeof(sw_datum) /* A datum is an instance of sw_data_api */
  },
  virtual_get_indexed,
  virtual_set_indexed,
  virtual_get_keyed,
  virtual_set_keyed,
  virtual_match,
  virtual_iterate_begin,
  virtual_iterate_next,
  virtual_iterate_end,
  virtual_equal,
  virtual_open_blob,
  virtual_pop,
  virtual_push
} ;

static Bool dataapi_swinit(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if (SwRegisterRDR(RDR_CLASS_API, RDR_API_DATA, 20130321,
                    (void*)&sw_data_api_virtual, sizeof(sw_data_api_virtual),
                    0) != SW_RDR_SUCCESS)
    return FALSE;

  return TRUE ;
}

static void dataapi_finish(void)
{
  (void)SwDeregisterRDR(RDR_CLASS_API, RDR_API_DATA, 20130321,
                        (void*)&sw_data_api_virtual, sizeof(sw_data_api_virtual));
}

void dataapi_C_globals(struct core_init_fns *fns)
{
  fns->swinit = dataapi_swinit ;
  fns->finish = dataapi_finish ;
}

/*---------------------------------------------------------------------------*/

sw_data_result RIPCALL sw_data_match_generic(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                             /*@notnull@*/ /*@out@*/ sw_data_match match[],
                                             size_t match_length)
{
  HQASSERT(composite != NULL, "No composite object to match") ;
  HQASSERT(composite->type == SW_DATUM_TYPE_DICT ||
           composite->type == SW_DATUM_TYPE_ARRAY,
           "Match object is not composite") ;
  HQASSERT(composite->value.opaque, "Composite object has no value") ;
  HQASSERT(match != NULL, "Nothing to match against composite object") ;

  for ( ; match_length > 0 ; --match_length, ++match ) {
    sw_datum *key = &match->key, *value = &match->value ;
    sw_data_result result ;

    HQASSERT(key->type == SW_DATUM_TYPE_NULL ||
             key->type == SW_DATUM_TYPE_BOOLEAN ||
             key->type == SW_DATUM_TYPE_INTEGER ||
             key->type == SW_DATUM_TYPE_FLOAT ||
             key->type == SW_DATUM_TYPE_STRING,
             "Invalid value type for match key") ;
    HQASSERT((match->type_mask & ~(SW_DATUM_BIT_NOTHING |
                                   SW_DATUM_BIT_NULL |
                                   SW_DATUM_BIT_BOOLEAN |
                                   SW_DATUM_BIT_INTEGER |
                                   SW_DATUM_BIT_FLOAT |
                                   SW_DATUM_BIT_STRING |
                                   SW_DATUM_BIT_ARRAY |
                                   SW_DATUM_BIT_DICT |
                                   SW_DATUM_BIT_BLOB |
                                   SW_DATUM_BIT_INVALID)) == 0,
             "Invalid type mask bit set") ;

    if ( composite->type == SW_DATUM_TYPE_ARRAY ) {
      if ( key->type != SW_DATUM_TYPE_INTEGER )
        return SW_DATA_ERROR_TYPECHECK ;

      /* Upper limit check is performed in virtual_get_indexed. */
      if ( key->value.integer >= 0 )
        result = virtual_get_indexed(composite, (size_t)key->value.integer, value) ;
      else
        result = SW_DATA_ERROR_UNDEFINED ;
    } else {
      HQASSERT(composite->type == SW_DATUM_TYPE_DICT,
               "Match is not dictionary") ;
      result = virtual_get_keyed(composite, key, value) ;
    }

    switch ( result ) {
    case SW_DATA_OK:
      /* Matching value was found. */
      if ( (match->type_mask & (1u << value->type)) == 0 ) {
        /* Matched value is not an allowed type. If SW_DATUM_TYPE_INVALID is
           in the type mask, set this to an invalid object. Otherwise,
           return an error immediately. */
        if ( (match->type_mask & SW_DATUM_BIT_INVALID) == 0 )
          return SW_DATA_ERROR_TYPECHECK ;

        /* The value existed, but was of the wrong type, and the match
           allowed invalid type returns. */
        value->type = SW_DATUM_TYPE_INVALID ;
        value->owner = NULL ;
        value->length = 0 ;
        value->value.opaque = NULL ;
      }
      break ;
    case SW_DATA_ERROR_UNDEFINED:
      /* Matching value was not found. Check whether the entry was
         required. */
      if ( (match->type_mask & SW_DATUM_BIT_NOTHING) == 0 )
        return SW_DATA_ERROR_UNDEFINED ;

      /* The value didn't exist, but the match allowed empty object
         returns. */
      value->type = SW_DATUM_TYPE_NOTHING ;
      value->owner = NULL ;
      value->length = 0 ;
      value->value.opaque = NULL ;
      break ;
    default:
      /* An error occurred extracting the match value. */
      return result ;
    }
  }

  return SW_DATA_OK ;
}


HqBool RIPCALL sw_data_equal_generic(const sw_datum* one, const sw_datum* two)
{
  HQASSERT(one != NULL && two != NULL, "Comparison operand is NULL") ;

  if ( one->type == two->type ) {
    /* The same type - an easy comparison */
    switch ( one->type ) {
    case SW_DATUM_TYPE_NULL:
    case SW_DATUM_TYPE_NOTHING:
    case SW_DATUM_TYPE_INVALID:
      return TRUE ;
    case SW_DATUM_TYPE_BOOLEAN:
      return (one->value.boolean) ? two->value.boolean : !two->value.boolean ;
    case SW_DATUM_TYPE_INTEGER:
      return (one->value.integer == two->value.integer) ;
    case SW_DATUM_TYPE_FLOAT:
      return (one->value.real == two->value.real) ;
    case SW_DATUM_TYPE_STRING:
      return (one->length == two->length &&
              HqMemCmp((const uint8 *)one->value.string, (int32)one->length,
                       (const uint8 *)two->value.string, (int32)two->length) == 0) ;
    case SW_DATUM_TYPE_ARRAY:
    case SW_DATUM_TYPE_DICT:
      return (one->owner == two->owner &&
              one->length == two->length &&
              one->value.opaque == two->value.opaque) ;
    }

  } else {
    /* dissimilar types - only INTEGER/FLOAT comparison possible */

    SYSTEMVALUE a,b ;

    switch ( one->type ) {
    case SW_DATUM_TYPE_INTEGER:
      a = (SYSTEMVALUE) one->value.integer ;
      break ;
    case SW_DATUM_TYPE_FLOAT:
      a = one->value.real ;
      break ;
    default:
      return FALSE ;
    }

    switch ( two->type ) {
    case SW_DATUM_TYPE_INTEGER:
      b = (SYSTEMVALUE) two->value.integer ;
      break ;
    case SW_DATUM_TYPE_FLOAT:
      b = two->value.real ;
      break ;
    default:
      return FALSE ;
    }
    return ( a == b ) ;
  }

  return FALSE ;
}

/* Log stripped */
