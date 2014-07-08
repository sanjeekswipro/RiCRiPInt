/** \file
 * \ingroup datum
 *
 * $HopeName: COREdatum!src:swdatafilter.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for filtering structured data API instances.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"

#include <string.h>
#include "ripcall.h"
#include "swdataapi.h"
#include "swdataimpl.h"
#include "swdatafilter.h"
#include "hqassert.h"

/* Get indexed value from next API. */
sw_data_result RIPCALL next_get_indexed(const sw_datum *array,
                                        size_t index,
                                        sw_datum *value)
{
  sw_data_api_filter *filter ;
  sw_data_result result ;

  /* Downcast to the filtered API class data */
  filter = (sw_data_api_filter *)array->owner ;
  HQASSERT(filter, "No filtered API class data") ;

  {
    sw_datum *mine = (sw_datum *)array ; /* Remove constness */
    HQASSERT(filter->prev, "No previous API in filter chain") ;
    mine->owner = filter->prev ;
    result = filter->prev->get_indexed(mine, index, value) ;
    mine->owner = filter ;
  }
  if ( result == SW_DATA_OK ) {
    /* Take ownership of the returned value, in case it is a composite type. */
    HQASSERT(value->owner == NULL || value->owner == filter->prev,
             "Changing ownership of returned value") ;
    value->owner = filter ;
  }
  return result ;
}

/* Set indexed value in next API. */
sw_data_result RIPCALL next_set_indexed(sw_datum *array,
                                        size_t index,
                                        const sw_datum *value)
{
  sw_data_api_filter *filter ;
  sw_data_result result ;

  /* Downcast to the filtered API class data */
  filter = (sw_data_api_filter *)array->owner ;
  HQASSERT(filter, "No filtered API class data") ;

  {
    sw_datum *mine = (sw_datum *)array ; /* Remove constness */
    HQASSERT(filter->prev, "No previous API in filter chain") ;
    mine->owner = filter->prev ;
    result = filter->prev->set_indexed(mine, index, value) ;
    mine->owner = filter ;
  }

  return result ;
}

/* Get keyed value from next API. */
sw_data_result RIPCALL next_get_keyed(const sw_datum *dict,
                                      const sw_datum *key,
                                      sw_datum *value)
{
  sw_data_api_filter *filter ;
  sw_data_result result ;

  /* Downcast to the filtered API class data */
  filter = (sw_data_api_filter *)dict->owner ;
  HQASSERT(filter, "No filtered API class data") ;

  {
    sw_datum *mine = (sw_datum *)dict ; /* Remove constness */
    HQASSERT(filter->prev, "No previous API in filter chain") ;
    mine->owner = filter->prev ;
    result = filter->prev->get_keyed(dict, key, value) ;
    mine->owner = filter ;
  }
  if ( result == SW_DATA_OK ) {
    /* Take ownership of the value, in case it is a composite type. */
    HQASSERT(value->owner == NULL || value->owner == filter->prev,
             "Changing ownership of returned value") ;
    value->owner = filter ;
  }
  return result ;
}

/* Set keyed value in next API. */
sw_data_result RIPCALL next_set_keyed(sw_datum *dict,
                                      const sw_datum *key,
                                      const sw_datum *value)
{
  sw_data_api_filter *filter ;
  sw_data_result result ;

  /* Downcast to the filtered API class data */
  filter = (sw_data_api_filter *)dict->owner ;
  HQASSERT(filter, "No filtered API class data") ;

  {
    sw_datum *mine = (sw_datum *)dict ; /* Remove constness */
    HQASSERT(filter->prev, "No previous API in filter chain") ;
    mine->owner = filter->prev ;
    result = filter->prev->set_keyed(dict, key, value) ;
    mine->owner = filter ;
  }

  return result ;
}

/** Implement iteration start for sw_datum type handler. */
static sw_data_result RIPCALL next_iterate_begin(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                 /*@notnull@*/ /*@out@*/ sw_data_iterator **iterator)
{
  sw_data_api_filter *filter ;
  sw_data_result result ;

  /* Downcast to the filtered API class data */
  filter = (sw_data_api_filter *)composite->owner ;
  HQASSERT(filter, "No filtered API class data") ;

  {
    sw_datum *mine = (sw_datum *)composite ; /* Remove constness */
    HQASSERT(filter->prev, "No previous API in filter chain") ;
    mine->owner = filter->prev ;
    result = filter->prev->iterate_begin(composite, iterator) ;
    mine->owner = filter ;
  }

  return result ;
}

/** Implement iteration step for sw_datum type handler. */
sw_data_result RIPCALL next_iterate_next(const sw_datum *composite,
                                         sw_data_iterator *iterator,
                                         sw_datum *key,
                                         sw_datum *value)
{
  sw_data_api_filter *filter ;
  sw_data_result result ;

  /* Downcast to the filtered API class data */
  filter = (sw_data_api_filter *)composite->owner ;
  HQASSERT(filter, "No filtered API class data") ;

  {
    sw_datum *mine = (sw_datum *)composite ; /* Remove constness */
    HQASSERT(filter->prev, "No previous API in filter chain") ;
    mine->owner = filter->prev ;
    result = filter->prev->iterate_next(mine, iterator, key, value) ;
    mine->owner = filter ;
  }
  if ( result == SW_DATA_OK ) {
    /* Take ownership of the value and key, in case they are composite types. */
    HQASSERT(key->owner == NULL || key->owner == filter->prev,
             "Changing ownership of returned key") ;
    HQASSERT(value->owner == NULL || value->owner == filter->prev,
             "Changing ownership of returned value") ;

    key->owner = filter ;
    value->owner = filter ;
  }
  return result ;
}

/** Implement end of iterator for sw_datum type handler. */
static void RIPCALL next_iterate_end(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                     /*@notnull@*/ /*@in@*/ sw_data_iterator **iterator)
{
  sw_data_api_filter *filter ;

  /* Downcast to the filtered API class data */
  filter = (sw_data_api_filter *)composite->owner ;
  HQASSERT(filter, "No filtered API class data") ;

  {
    sw_datum *mine = (sw_datum *)composite ; /* Remove constness */
    HQASSERT(filter->prev, "No previous API in filter chain") ;
    mine->owner = filter->prev ;
    filter->prev->iterate_end(mine, iterator) ;
    mine->owner = filter ;
  }
}

sw_data_result RIPCALL next_open_blob(const sw_datum *datum,
                                      int32 mode,
                                      sw_blob_instance **blob)
{
  sw_data_api_filter *filter ;
  sw_data_result result ;

  /* Downcast to the filtered API class data */
  filter = (sw_data_api_filter *)datum->owner ;
  HQASSERT(filter, "No filtered API class data") ;

  {
    sw_datum *mine = (sw_datum *)datum ; /* Remove constness */
    HQASSERT(filter->prev, "No previous API in filter chain") ;
    mine->owner = filter->prev ;
    result = filter->prev->open_blob(datum, mode, blob) ;
    mine->owner = filter ;
  }

  return result ;
}

/*---------------------------------------------------------------------------*/

/* Restrict read access to data. */
sw_data_result RIPCALL invalid_get_indexed(const sw_datum *array,
                                           size_t index,
                                           sw_datum *value)
{
  UNUSED_PARAM(const sw_datum *, array) ;
  UNUSED_PARAM(size_t, index) ;
  UNUSED_PARAM(sw_datum *, value) ;
  return SW_DATA_ERROR_INVALIDACCESS ;
}

/* Restrict write access to data. */
sw_data_result RIPCALL invalid_set_indexed(sw_datum *array,
                                           size_t index,
                                           const sw_datum *value)
{
  UNUSED_PARAM(sw_datum *, array) ;
  UNUSED_PARAM(size_t, index) ;
  UNUSED_PARAM(const sw_datum *, value) ;
  return SW_DATA_ERROR_INVALIDACCESS ;
}

/* Restrict read access to data. */
sw_data_result RIPCALL invalid_get_keyed(const sw_datum *dict,
                                         const sw_datum *key,
                                         sw_datum *value)
{
  UNUSED_PARAM(const sw_datum *, dict) ;
  UNUSED_PARAM(const sw_datum *, key) ;
  UNUSED_PARAM(sw_datum *, value) ;
  return SW_DATA_ERROR_INVALIDACCESS ;
}

/* Restrict write access to data. */
sw_data_result RIPCALL invalid_set_keyed(sw_datum *dict,
                                         const sw_datum *key,
                                         const sw_datum *value)
{
  UNUSED_PARAM(sw_datum *, dict) ;
  UNUSED_PARAM(const sw_datum *, key) ;
  UNUSED_PARAM(const sw_datum *, value) ;
  return SW_DATA_ERROR_INVALIDACCESS ;
}

sw_data_result RIPCALL invalid_iterate_next(const sw_datum *composite,
                                            sw_data_iterator *iterator,
                                            sw_datum *key,
                                            sw_datum *value)
{
  UNUSED_PARAM(const sw_datum *, composite) ;
  UNUSED_PARAM(sw_data_iterator *, iterator) ;
  UNUSED_PARAM(sw_datum *, key) ;
  UNUSED_PARAM(sw_datum *, value) ;
  return SW_DATA_ERROR_INVALIDACCESS ;
}

sw_data_result RIPCALL invalid_open_blob(const sw_datum *datum,
                                         int32 mode,
                                         sw_blob_instance **blob)
{
  UNUSED_PARAM(const sw_datum *, datum) ;
  UNUSED_PARAM(int32 , mode) ;
  UNUSED_PARAM(sw_blob_instance **, blob) ;
  return SW_DATA_ERROR_INVALIDACCESS ;
}

/*---------------------------------------------------------------------------*/

sw_data_api *sw_data_api_filter_construct(sw_data_api_filter *filter,
                                          const sw_data_api *filtered,
                                          const char *prefix,
                                          sw_data_api_get_indexed_fn get_indexed,
                                          sw_data_api_set_indexed_fn set_indexed,
                                          sw_data_api_get_keyed_fn get_keyed,
                                          sw_data_api_set_keyed_fn set_keyed,
                                          sw_data_api_iterate_next_fn iterate_next,
                                          sw_data_api_open_blob_fn open_blob)
{
  size_t index ;

  HQASSERT(filter, "No filter API to setup") ;
  HQASSERT(filtered, "No API to filter") ;
  HQASSERT(filtered != &sw_data_api_virtual,
           "Filtered virtual API will loop infinitely") ;
  HQASSERT(prefix, "No prefix for filtered API.") ;

  for ( index = 0 ; index < sizeof(filter->name) >> 1 ; ++index ) {
    if ( (filter->name[index] = prefix[index]) == 0 )
      break ;
  }
  filter->name[index++] = '.' ;
  strncpy((char *)filter->name + index, (const char *)filtered->info.name,
          sizeof(filter->name) - index) ;
  /* ensure NUL terminated */
  filter->name[sizeof(filter->name) - 1] = '\0' ;

  filter->prev = filtered ;
  filter->super.info.version = SW_DATA_API_VERSION_20071111 ;
  filter->super.info.name =
    filter->super.info.display_name = (const uint8 *)filter->name ;
  filter->super.info.instance_size = sizeof(sw_datum) ;
  filter->super.get_indexed = get_indexed ? get_indexed : &next_get_indexed ;
  filter->super.set_indexed = set_indexed ? set_indexed : &next_set_indexed ;
  filter->super.get_keyed = get_keyed ? get_keyed : &next_get_keyed ;
  filter->super.set_keyed = set_keyed ? set_keyed : &next_set_keyed ;
  filter->super.match = &sw_data_match_generic ;
  filter->super.iterate_begin = &next_iterate_begin ;
  filter->super.iterate_next = iterate_next ? iterate_next : &next_iterate_next ;
  filter->super.iterate_end = &next_iterate_end ;
  filter->super.equal = &sw_data_equal_generic ;
  filter->super.open_blob = open_blob ? open_blob : &next_open_blob ;

  return &filter->super ;
}

/* Log stripped */
