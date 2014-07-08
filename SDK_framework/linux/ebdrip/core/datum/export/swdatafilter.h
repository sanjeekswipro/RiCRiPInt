/** \file
 * \ingroup datum
 *
 * $HopeName: COREdatum!export:swdatafilter.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * Implementation for \c sw_datum interfaces filtering.
 */

#ifndef __SWDATAFILTER_H__
#define __SWDATAFILTER_H__

#include "swdataapi.h"

/** \brief Subclass of \c sw_data_api for filtering calls. This definition is
    exposed so that instantiation can be done on the stack. */
typedef struct sw_data_api_filter {
  sw_data_api super ;       /**< \c sw_data_api superclass must be first */
  const sw_data_api *prev ; /**< API that is being filtered. */
  uint8 name[128] ;         /**< Constructed name and display name. */
} sw_data_api_filter ;

/** \brief Type of the indexed getter function. See swdataapi.h for details. */
typedef sw_data_result (RIPCALL *sw_data_api_get_indexed_fn)(/*@notnull@*/ /*@in@*/ const sw_datum *array,
                                                             size_t index,
                                                             /*@notnull@*/ /*@out@*/ sw_datum *value) ;

/** \brief Type of the indexed setter function. See swdataapi.h for details. */
typedef sw_data_result (RIPCALL *sw_data_api_set_indexed_fn)(/*@notnull@*/ /*@in@*/ sw_datum *array,
                                                             size_t index,
                                                             /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

/** \brief Type of the keyed getter function.. See swdataapi.h for details */
typedef sw_data_result (RIPCALL *sw_data_api_get_keyed_fn)(/*@notnull@*/ /*@in@*/ const sw_datum *dict,
                                                           /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                                           /*@notnull@*/ /*@out@*/ sw_datum *value) ;

/** \brief Type of the keyed setter function. See swdataapi.h for details. */
typedef sw_data_result (RIPCALL *sw_data_api_set_keyed_fn)(/*@notnull@*/ /*@in@*/ sw_datum *dict,
                                                           /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                                           /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

/** \brief Type of the matcher. See swdataapi.h for details. */
typedef sw_data_result (RIPCALL *sw_data_api_match_fn)(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                       /*@notnull@*/ /*@out@*/ sw_data_match *match,
                                                       size_t match_length) ;

/** \brief Function type to start a new iteration over a data structure. See
    swdataapi.h for details.   */
typedef sw_data_result (RIPCALL *sw_data_api_iterate_begin_fn)(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                               /*@notnull@*/ /*@out@*/ sw_data_iterator **iterator) ;

/** \brief Function type to get a key-value pair from a composite structure.
    See swdataapi.h for details.   */
typedef sw_data_result (RIPCALL *sw_data_api_iterate_next_fn)(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                              /*@notnull@*/ /*@in@*/ sw_data_iterator *iterator,
                                                              /*@notnull@*/ /*@out@*/ sw_datum *key,
                                                              /*@notnull@*/ /*@out@*/ sw_datum *value) ;

/** \brief Function type to discard resources associated with a data
    structure iterator state. See swdataapi.h for details. */
typedef void (RIPCALL *sw_data_api_iterate_end_fn)(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                                   /*@notnull@*/ /*@in@*/ sw_data_iterator **iterator) ;

/** \brief Function type to compare two datums for equality. See swdataapi.h
    for details.  */
typedef HqBool (RIPCALL *sw_data_api_equal_fn)(/*@notnull@*/ /*@in@*/ const sw_datum *one,
                                               /*@notnull@*/ /*@in@*/ const sw_datum *two) ;

/** \brief Function type to open a SW_DATUM_TYPE_BLOB for access via the
    sw_blob_api interface. See swdataapi.h for details. */
typedef sw_data_result (RIPCALL *sw_data_api_open_blob_fn)(/*@notnull@*/ /*@in@*/ const sw_datum *datum,
                                                           int32 mode,
                                                           /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;


/** \brief Constructor for filtered data APIs. This constructor should be
    used rather than hand-rolling, because the unset procedures need to
    use trampolines to make sure that the owner is reset for each call
    down the filter chain.

    Note that there are no match or equal methods in the parameters; these
    use the generic functions, which call through the \c get_keyed or \c
    get_indexed methods to access the composite being matched. There are no
    \c iterate_begin or \c iterate_end methods either, because all of the
    interesting filtering for iteration can be done in \c iterate_next.

    Data API filters should only be used on homogeneous data heirarchies. They
    may become confused about object ownership if objects from more than one
    owner are present in the hierarchy.

    \param[out] filter A filter API to create.

    \param[in] filtered The API to filter.

    \param[in] get_indexed The indexed getter method, or NULL for the default.

    \param[in] set_indexed The indexed setter method, or NULL for the default.

    \param[in] get_keyed The keyed getter method, or NULL for the default.

    \param[in] set_keyed The indexed setter method, or NULL for the default.

    \param[in] iterate_next The iterator method, or NULL for the default.

    \param[in] open_blob A blob open method, or NULL for the default.

    \return A filtered API, suitable for passing to calls requiring \c
    sw_data_api parameters. The return value will never be NULL.
*/
/*@notnull@*/
sw_data_api *sw_data_api_filter_construct(/*@in@*/ /*@notnull@*/ sw_data_api_filter *filter,
                                          /*@in@*/ /*@notnull@*/ const sw_data_api *filtered,
                                          /*@in@*/ /*@notnull@*/ const char *prefix,
                                          sw_data_api_get_indexed_fn get_indexed,
                                          sw_data_api_set_indexed_fn set_indexed,
                                          sw_data_api_get_keyed_fn get_keyed,
                                          sw_data_api_set_keyed_fn set_keyed,
                                          sw_data_api_iterate_next_fn iterate_next,
                                          sw_data_api_open_blob_fn open_blob) ;

/* Trampoline methods for use in filtered APIs. These are exposed so that
   filtering methods can use them for safe downcalls to the filtered API,
   setting the owner fields as appropriate. */

/** \brief Trampoline for get_indexed. */
sw_data_result RIPCALL next_get_indexed(/*@notnull@*/ /*@in@*/ const sw_datum *array,
                                        size_t index,
                                        /*@notnull@*/ /*@out@*/ sw_datum *value) ;

/** \brief Trampoline for set_indexed. */
sw_data_result RIPCALL next_set_indexed(/*@notnull@*/ /*@in@*/ sw_datum *array,
                                        size_t index,
                                        /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

/** \brief Trampoline for get_keyed. */
sw_data_result RIPCALL next_get_keyed(/*@notnull@*/ /*@in@*/ const sw_datum *dict,
                                      /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                      /*@notnull@*/ /*@out@*/ sw_datum *value) ;
/** \brief Trampoline for set_keyed. */
sw_data_result RIPCALL next_set_keyed(/*@notnull@*/ /*@in@*/ sw_datum *dict,
                                      /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                      /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

/** \brief Trampoline for iterate_next. */
sw_data_result RIPCALL next_iterate_next(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                         /*@notnull@*/ /*@in@*/ sw_data_iterator *iterator,
                                         /*@notnull@*/ /*@out@*/ sw_datum *key,
                                         /*@notnull@*/ /*@out@*/ sw_datum *value) ;

/** \brief Trampoline for open_blob. */
sw_data_result RIPCALL next_open_blob(/*@notnull@*/ /*@in@*/ const sw_datum *datum,
                                      int32 mode,
                                      /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;

/* Invalid methods allow data filters to enforce read or write only on data.
   They immediately return SW_DATA_ERROR_INVALIDACCESS. */

/** \brief Invalid get_indexed. */
sw_data_result RIPCALL invalid_get_indexed(/*@notnull@*/ /*@in@*/ const sw_datum *array,
                                           size_t index,
                                           /*@notnull@*/ /*@out@*/ sw_datum *value) ;

/** \brief Invalid set_indexed. */
sw_data_result RIPCALL invalid_set_indexed(/*@notnull@*/ /*@in@*/ sw_datum *array,
                                           size_t index,
                                           /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

/** \brief Invalid get_keyed. */
sw_data_result RIPCALL invalid_get_keyed(/*@notnull@*/ /*@in@*/ const sw_datum *dict,
                                         /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                         /*@notnull@*/ /*@out@*/ sw_datum *value) ;

/** \brief Invalid set_keyed. */
sw_data_result RIPCALL invalid_set_keyed(/*@notnull@*/ /*@in@*/ sw_datum *dict,
                                         /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                         /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

/** \brief Invalid iterate_next. */
sw_data_result RIPCALL invalid_iterate_next(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                            /*@notnull@*/ /*@in@*/ sw_data_iterator *iterator,
                                            /*@notnull@*/ /*@out@*/ sw_datum *key,
                                            /*@notnull@*/ /*@out@*/ sw_datum *value) ;

/** \brief Invalid open_blob. */
sw_data_result RIPCALL invalid_open_blob(/*@notnull@*/ /*@in@*/ const sw_datum *datum,
                                         int32 mode,
                                         /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;

#endif /* __SWDATAIMPL_H__ */

/* Log stripped */
