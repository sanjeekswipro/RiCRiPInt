/** \file
 * \ingroup datum
 *
 * $HopeName: COREdatum!export:swdataimpl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * Implementation for \c sw_datum interfaces.
 */

#ifndef __SWDATAIMPL_H__
#define __SWDATAIMPL_H__

#include "swdataapi.h"

struct core_init_fns ; /* from SWcore */
struct OBJECT ; /* from COREobjects */
struct STACK ;  /* from COREobjects */

/**
 * \defgroup datum External interface data provisioning.
 * \ingroup core
 */
/** \{ */

/** \brief Comparison function for sw_datum elements.

    This function is quicksort-compatible. It compares two sw_datum elements,
    indicating if the first is less, equal, or greater to the second. This
    function will return a stable result for composite datum, but will not
    sort on the content of the object, only the value of the opaque pointer.

    \param[in] one Pointer to a \c sw_datum.

    \param[in] two Pointer to a \c sw_datum.

    \return Returns a negative value if \a one was lexically less than \a
    two, 0 if \a one was equal to \a two, or a positive value if \a one was
    greater than \a two.
*/
int CRT_API sw_datum_cmp(const void *one, const void *two) ;

/** \brief Generic equality checker for \c sw_data_api equal function.

    This is a strict equality checker for composite \c sw_datum, but an
    equivalence test for simple \c sw_datum.

    \param[in] one Pointer to a \c sw_datum.

    \param[in] two Pointer to a \c sw_datum.

    \retval FALSE the values were of incomparable types, or were not equal.

    \retval TRUE the values were of comparable types of the same value.

    This is a strict equality test for composites, but is otherwise
    an equivalence test, so a float can be equal to an integer for example.
*/
HqBool RIPCALL sw_data_equal_generic(/*@notnull@*/ /*@in@*/ const sw_datum *one,
                                     /*@notnull@*/ /*@in@*/ const sw_datum *two) ;

/** \brief Generic implementation of \c sw_data_api match function.

    This function can be used by implementations of \c sw_data_api as a match
    function. It uses \c sw_data_cmp, and the virtual data provider's \c
    get_indexed and \c get_keyed functions to extract, match, and fill in
    results.

    \param[in] composite An array or dictionary datum from which values will
    be extracted.

    \param[in, out] match An array of match structures, determining the key
    to match, what types are acceptable for the match, how to treat
    type mismatches or missing values, and where to store the value.

    \param[in] match_length The number of entries to match.

    \retval SW_DATA_OK Success. The match result data are updated
    with the value of the extracted entries, or set to \a
    SW_DATUM_TYPE_NOTHING if the key was not matched but was optional.
    \retval SW_DATA_ERROR_UNDEFINED Returned if a required entry was not
    present. The match data should not be examined if this error occurs.
    \retval SW_DATA_ERROR_TYPECHECK Returned if a matched value has the
    wrong type, and the \a SW_DATA_TYPE_INVALID bit was not set in the
    allowed type mask of the match. The match data should not be examined
    if this error occurs.
    \retval SW_DATA_ERROR_INVALIDACCESS Returned if the data structure to
    match was not readable. The match data should not be examined if this
    error occurs.
*/
sw_data_result RIPCALL sw_data_match_generic(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                             /*@notnull@*/ /*@out@*/ sw_data_match match[],
                                             size_t match_length) ;

/** \brief Utility function to create a sw_datum from an OBJECT.
 *
 * \param[out] datum Pointer to a datum that will be filled in from the
 * object.
 *
 * \param[in] object The object to be translated to a \c sw_datum.
 *
 * \retval SW_DATA_OK Returned if the object was converted successfully.
 * \retval SW_DATA_ERROR_INVALIDACCESS Returned if the object is not readable.
 * \retval SW_DATA_ERROR_TYPECHECK Returned if the object was not a type that
 * can be represented as a \c sw_datum.
 */
sw_data_result swdatum_from_object(/*@notnull@*/ /*@out@*/ sw_datum *datum,
                                   /*@notnull@*/ /*@in@*/ const struct OBJECT *object) ;

/** \brief Utility function to create an OBJECT from a \c sw_datum.
 *
 * \param[out] object The object to be filled in from the object.
 *
 * \param[in] datum Pointer to a datum to be translated to an \c OBJECT.
 *
 * \retval SW_DATA_OK Returned if the object was converted successfully.
 * \retval SW_DATA_ERROR_RANGECHECK Returned if the \c sw_datum is too large
 * to be represented in an object.
 * \retval SW_DATA_ERROR_MEMORY Returned if a memory allocation failed for
 * the object.
 * to be represented in an object.
 * \retval SW_DATA_ERROR_INVALIDACCESS Returned if the object is not writable,
 * or if an illegal global into local would be performed.
 * \retval SW_DATA_ERROR_TYPECHECK Returned if the sw_datum was a type that
 * can not be represented as an \c OBJECT.
 */
sw_data_result object_from_swdatum(/*@notnull@*/ /*@out@*/ struct OBJECT *object,
                                   /*@notnull@*/ /*@in@*/ const sw_datum *datum) ;

/** \brief Utility function to create an OBJECT from a \c sw_datum, suitable
 * for use as a dictionary key.
 *
 * The difference between this function and \c object_from_swdatum is that
 * this function will return an \c ONAME for string types, if possible.
 *
 * \param[out] object The object to be filled in from the object.
 *
 * \param[in] datum Pointer to a datum to be translated to an \c OBJECT.
 *
 * \retval SW_DATA_OK Returned if the object was converted successfully.
 * \retval SW_DATA_ERROR_RANGECHECK Returned if the \c sw_datum is too large
 * to be represented in an object.
 * \retval SW_DATA_ERROR_MEMORY Returned if a memory allocation failed for
 * the object.
 * to be represented in an object.
 * \retval SW_DATA_ERROR_INVALIDACCESS Returned if the object is not writable,
 * or if an illegal global into local would be performed.
 * \retval SW_DATA_ERROR_TYPECHECK Returned if the sw_datum was a type that
 * can not be represented as an \c OBJECT.
 */
sw_data_result object_key_from_swdatum(/*@notnull@*/ /*@out@*/ struct OBJECT *object,
                                       /*@notnull@*/ /*@in@*/ const sw_datum *datum) ;

/** \brief Utility function to create a sw_datum from a STACK.
 *
 * \param[out] datum Pointer to a datum that will be filled in from the
 * stack.
 *
 * \param[in] stack The stack to be represented by a \c sw_datum.
 *
 * \retval SW_DATA_OK Returned if the stack was represented successfully.
 */
sw_data_result swdatum_from_stack(/*@notnull@*/ /*@out@*/ sw_datum *datum,
                                  /*@notnull@*/ /*@in@*/ const struct STACK *stack) ;

/** \brief Translate a \c sw_data_result error to a PostScript error.
 *
 * \param[in] errorno The \c sw_data_result error to be translated.
 *
 * \returns A PostScript error code.
 */
int32 error_from_sw_data_result(sw_data_result errorno) ;

/** \brief Translate a PostScript error to a \c sw_data_result.
 *
 * \param[in] errorno The PostScript error to be translated.
 *
 * \returns A \c sw_data_result error code.
 */
sw_data_result sw_data_result_from_error(int32 errorno) ;

/** \brief The virtual \c sw_datum API handler.

     This API implementation uses the owner field to indirect to a specific
     implementation for the data owner. This API performs all of the
     parameter checking for other API implementations, before dispatching to
     the correct owner. */
extern const sw_data_api sw_data_api_virtual ;

/** Compound runtime initialisation */
void dataapi_C_globals(struct core_init_fns *fns) ;

/** \} */
#endif /* __SWDATAIMPL_H__ */

/* Log stripped */
