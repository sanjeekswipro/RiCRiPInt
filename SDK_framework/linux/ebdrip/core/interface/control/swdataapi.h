/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swdataapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief
 * This header file provides definitions for the structured
 * data interface to the core RIP.
 *
 * The structured data interface is a callback-based interface used to access
 * parameters passed to a module.
 *
 * A sw_datum can represent a boolean, integer, float, string, name, array,
 * stack or dictionary. It can represent a Postscript object and act as a
 * proxy for it to allow manipulation of Postscript state, or it can be an
 * independent module-created 'raw' value.
 *
 * If 'raw' values are insufficient, implementing the sw_data_api interface
 * allows the content of complex types such as arrays and dictionaries to be
 * created on demand rather than in advance. The RIP uses this approach to
 * grant access to Postscript objects through particular interfaces.
 */


#ifndef __SWDATAAPI_H__
#define __SWDATAAPI_H__

/** \defgroup swdataapi Structured data callback API
 * \ingroup PLUGIN_callbackAPI
 * \{
 */


#include "ripcall.h"
#include <stddef.h> /* size_t */
#include "swapi.h" /* sw_api_version */
#include "swblobapi.h" /* sw_blob_instance */

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Version numbers defined for the structured data API. */
enum {
  SW_DATA_API_VERSION_20070525 = 2, /**< Obsolete as of 20070620. */
  SW_DATA_API_VERSION_20070620,     /**< Obsolete as of 20071111. */
  SW_DATA_API_VERSION_20071111,     /**< Obsolete as of 20090415. */
  SW_DATA_API_VERSION_20090415,     /**< Obsolete as of 20130321. */ /* subtypes */
  SW_DATA_API_VERSION_20130321      /**< Current version. */ /* stacks */
  /* new versions go here */
#ifdef CORE_INTERFACE_INTERNAL
  , SW_DATA_API_VERSION_NEXT /* Do not ever use this name. */
  /* SW_DATA_API_VERSION is provided so that the Harlequin Core RIP can test
     compatibility with current versions, without revising the registration
     code for every interface change.

     Implementations of sw_data_api within the Harlequin Core RIP should NOT
     use this; they should have explicit version numbers. Using explicit
     version numbers will allow them to be backwards-compatible without
     modifying the code for every interface change.
  */
  , SW_DATA_API_VERSION = SW_DATA_API_VERSION_NEXT - 1
#endif
} ;

/** \brief Return values from sw_data_api functions.
 *
 * All error values are guaranteed to be greater than or equal to
 * SW_DATA_ERROR. Errors may be implementation dependent so all return values
 * >= SW_DATA_ERROR must be treated as errors, even if not explicitly listed as
 * a method return value.
 */
enum {
  /* Success codes present in SW_DATA_API_VERSION_20071111: */
  SW_DATA_OK = 0,               /**< Return value for successful operations. */
  SW_DATA_FINISHED = -1,        /**< An iterator has finished. This is not an error. */
  /* End of success codes present in SW_DATA_API_VERSION_20071111 */

  /* Errors present in SW_DATA_API_VERSION_20071111: */
  SW_DATA_ERROR = 1,            /**< Non-specific error, also minimum error
                                     value. Please avoid using this if
                                     possible. */
  SW_DATA_ERROR_TYPECHECK,      /**< Type of value or key was incorrect. */
  SW_DATA_ERROR_RANGECHECK,     /**< Range of value or index was incorrect. */
  SW_DATA_ERROR_INVALIDACCESS,  /**< Return value for disallowed set or get. */
  SW_DATA_ERROR_UNDEFINED,      /**< A key was not found in an association. */
  SW_DATA_ERROR_MEMORY,         /**< Memory was exhausted. */
  SW_DATA_ERROR_SYNTAX          /**< Programming error (e.g. invalid parameters). */
  /* End of errors present in SW_DATA_API_VERSION_20071111 */
} ;

/** \brief Type of return values from sw_data_api functions. */
typedef int sw_data_result ;

/** \brief Data types for structured data.
 *
 * These definitions closely follow Postscript use, but future extension may
 * include XML nodes, Trees, or other data hierarchy forms.
 *
 * Data types are divided into three categories.
 *
 * The data types SW_DATUM_TYPE_NOTHING and SW_DATUM_TYPE_INVALID are
 * pseudo-types which are used by the RIP to indicate certain conditions have
 * occurred, such as missing or invalid data in data matches. They will not
 * be retrieved through the sw_data_api getter functions and cannot be stored
 * through the sw_data_api setter functions.
 *
 * Transparent data types are SW_DATUM_TYPE_NULL, SW_DATUM_TYPE_BOOLEAN,
 * SW_DATUM_TYPE_INTEGER, SW_DATUM_TYPE_FLOAT, and SW_DATUM_TYPE_STRING.
 * Values of these types can be examined directly, by looking at the
 * appropriate field of the sw_datum::value union. The null type has no union
 * branch, it is a type with a singleton value. Strings are not
 * nul-terminated, so the sw_datum::length field must be used to delimit
 * their size.
 *
 * Opaque data types are SW_DATUM_TYPE_ARRAY, SW_DATUM_TYPE_DICT, and
 * SW_DATUM_TYPE_BLOB. Datum of these types must only be accessed through the
 * sw_data_api method functions. In the case of arrays and dictionaries (hash
 * tables), the length can be examined to determine the array length or the
 * maximum storage space in the dictionary respectively.
 */
enum {
  /* Entries present in SW_DATA_API_VERSION_20071111: */

  SW_DATUM_TYPE_NOTHING = 0, /**< No datum present. */
  SW_DATUM_TYPE_NULL,        /**< The distinguished null value. */
  SW_DATUM_TYPE_BOOLEAN,     /**< TRUE or FALSE values. */
  SW_DATUM_TYPE_INTEGER,     /**< A type for integer values. */
  SW_DATUM_TYPE_FLOAT,       /**< Single-precision floating point numbers. */
  SW_DATUM_TYPE_STRING,      /**< A contiguous byte array. */
  SW_DATUM_TYPE_ARRAY,       /**< An opaque type for indexed arrays. */
  SW_DATUM_TYPE_DICT,        /**< An opaque type for associative arrays. */
  SW_DATUM_TYPE_BLOB,        /**< An opaque type for streamed or mapped data. */
  SW_DATUM_TYPE_INVALID = 31 /**< A match value's type was incorrect. This
                                  should have a higher value than any other
                                  type number. */

  /* End of entries present in SW_DATA_API_VERSION_20071111 */
} ;

/** \brief Data type bits.
 *
 * These are used to select allowable types in the sw_data_match
 * structure's type_mask field.
 */
enum {
  /* Entries present in SW_DATA_API_VERSION_20071111: */

  SW_DATUM_BIT_NOTHING = 1u << SW_DATUM_TYPE_NOTHING, /**< Match mask bit for SW_DATUM_TYPE_NOTHING */
  SW_DATUM_BIT_NULL    = 1u << SW_DATUM_TYPE_NULL,    /**< Match mask bit for SW_DATUM_TYPE_NULL */
  SW_DATUM_BIT_BOOLEAN = 1u << SW_DATUM_TYPE_BOOLEAN, /**< Match mask bit for SW_DATUM_TYPE_BOOLEAN */
  SW_DATUM_BIT_INTEGER = 1u << SW_DATUM_TYPE_INTEGER, /**< Match mask bit for SW_DATUM_TYPE_INTEGER */
  SW_DATUM_BIT_FLOAT   = 1u << SW_DATUM_TYPE_FLOAT,   /**< Match mask bit for SW_DATUM_TYPE_FLOAT */
  SW_DATUM_BIT_STRING  = 1u << SW_DATUM_TYPE_STRING,  /**< Match mask bit for SW_DATUM_TYPE_STRING */
  SW_DATUM_BIT_ARRAY   = 1u << SW_DATUM_TYPE_ARRAY,   /**< Match mask bit for SW_DATUM_TYPE_ARRAY */
  SW_DATUM_BIT_DICT    = 1u << SW_DATUM_TYPE_DICT,    /**< Match mask bit for SW_DATUM_TYPE_DICT */
  SW_DATUM_BIT_BLOB    = 1u << SW_DATUM_TYPE_BLOB,    /**< Match mask bit for SW_DATUM_TYPE_BLOB */
  SW_DATUM_BIT_INVALID = 1u << SW_DATUM_TYPE_INVALID  /**< Match mask bit indicating no match */

  /* End of entries present in SW_DATA_API_VERSION_20071111 */
} ;

/** \brief Subtypes
 *
 * These values allow data types to be subtyped. Strings and arrays are
 * subtyped, to indicate names and stacks respectively.
 */
enum {
  /* Entries present in SW_DATA_API_VERSION_20090415: */

  SW_DATUM_SUBTYPE_NONE = 0,         /**< Types usually have no subtype. */

  /* STRING subtypes: */
  SW_DATUM_SUBTYPE_NAME_STRING = 1,  /**< String subtype that represents Postscript NAMEs or similar atomic tokens */

  /* End of entries present in SW_DATA_API_VERSION_20090415 */

  /* ARRAY subtypes: */
  SW_DATUM_SUBTYPE_STACK_ARRAY = 1   /**< Array subtype that represents a Postscript stack or similar list */

  /* End of entries present in SW_DATA_API_VERSION_20130321 */
} ;

/** \brief Type definition for the structured data interface implementation. */
typedef struct sw_data_api sw_data_api ;

/** \brief Structured data instance type.
 *
 * Modules may create raw sw_datum values, including arrays and dictionaries
 * (associative arrays), but must ensure that all fields are initialised
 * properly. Static initialisers are provided which can be used to initialise
 * local datums, which can then be used directly or copied to initialise
 * dynamically allocated datums.
 *
 * For a raw sw_datum array of \a n elements, \a value.opaque points to a
 * contiguous array of \a n sw_datums. For a dictionary of \a n key/value pairs
 * it points to a contiguous array of \a n * 2 sw_datums. (Raw stacks are not
 * useful because their size cannot be safely changed, so push and pop are
 * not supported). The \a owner field must be set to NULL for raw datums created
 * by a module, and should never be modified for data provided to the module.
 * Note however it is possible for a module to implement the sw_data_api itself
 * and hence build 'owned' datums.
 *
 * Modules may treat all arrays and dictionaries as opaque and use the api
 * access methods even for values it has created itself. However, arrays and
 * dictionaries with a non-NULL owner are always opaque and their contents can
 * only be accessed using this api.
 */
typedef struct sw_datum {
  /* Entries present in SW_DATA_API_VERSION_20071111: */

  /* In SW_DATA_API_VERSION_20090415 type was demoted from int to char: */
  unsigned char type ;    /**< One of the \a SW_DATUM_TYPE_* types. */
  unsigned char subtype ; /**< Normally zero, see \a SW_DATUM_SUBTYPE_*. */
  unsigned char flags ;   /**< No flags defined yet, always zero. */
  unsigned char spare ;   /**< Always zero */
  const void *owner ;     /**< The owner of opaque values. The module should
                               set to NULL for objects it creates, and should
                               never alter this field for opaque objects
                               provided by the RIP. */
  size_t length ;         /**< Length of a string, array or dictionary. */
  /** A union containing the value of the datum. The branch of the value to
      use depends on the \a type field. */
  union {
    const void *opaque ;  /**< One of the opaque structured types. */
    const char *string ;  /**< Pointer to an unterminated read-only string. */
    int integer ;         /**< Value of integers. */
    HqBool boolean ;      /**< Value for booleans (TRUE or FALSE). */
    float real ;          /**< Value for reals. */
  } value ;


  /* End of entries present in SW_DATA_API_VERSION_20071111 */
} sw_datum ;

/* Some sw_datum initialisers. */

/** Static initialiser for sw_datum invalid values. e.g. \code
    sw_datum invalid = SW_DATUM_INVALID ; \endcode
    
    This type is more likely to be returned by a match call than created by
    module code. */
#define SW_DATUM_INVALID {SW_DATUM_TYPE_INVALID}

/** Static initialiser for sw_datum nothing values. e.g. \code
    sw_datum nothing = SW_DATUM_NOTHING ; \endcode 
    
    This type is more likely to be returned by a match call than created by
    module code. */
#define SW_DATUM_NOTHING {SW_DATUM_TYPE_NOTHING}

/** Static initialiser for sw_datum null values. e.g. \code
    sw_datum null = SW_DATUM_NULL ; \endcode */
#define SW_DATUM_NULL {SW_DATUM_TYPE_NULL}

/** Static initialiser for sw_datum integer values. e.g. \code
    sw_datum count = SW_DATUM_INTEGER(4) ; \endcode  */
#define SW_DATUM_INTEGER(_int) \
  {SW_DATUM_TYPE_INTEGER, 0,0,0, NULL, 0, { (void *)((intptr_t)(_int)) }}

/** Static initialiser for sw_datum boolean values. e.g. \code
    sw_datum bool = SW_DATUM_BOOLEAN(TRUE) ; \endcode */
#define SW_DATUM_BOOLEAN(_bool) \
  {SW_DATUM_TYPE_BOOLEAN, 0,0,0, NULL, 0, { (void *)((intptr_t)(_bool)) }}

/** Static initialiser for sw_datum string values. e.g. \code
    sw_datum text = SW_DATUM_STRING("Some text") ; \endcode */
#define SW_DATUM_STRING(_str) \
  {SW_DATUM_TYPE_STRING, 0,0,0, NULL, sizeof("" _str "") - 1, {(const void *)("" _str "")}}

/** Static initialiser for sw_datum name values. e.g. \code
    sw_datum key = SW_DATUM_NAME("Encoding") ; \endcode */
#define SW_DATUM_NAME(_str) \
  {SW_DATUM_TYPE_STRING, SW_DATUM_SUBTYPE_NAME_STRING,0,0, NULL, sizeof("" _str "") - 1, {(const void *)("" _str "")}}

/** Static initialiser for sw_datum array values. e.g. \code
    sw_datum elements[] = {SW_DATUM_INTEGER(5), SW_DATUM_NAME("Five")} ;
    sw_datum array = SW_DATUM_ARRAY(&elements[0], SW_DATA_ARRAY_LENGTH(elements)) ; \endcode
    
    It is also possible to initialise the array and contents simultaneously,
    if you are careful about the array length: \code
    sw_datum array[3] = {
      SW_DATUM_ARRAY(array+1, 2),   // The array datum
      SW_DATUM_INTEGER(5),          // The first element of the array
      SW_DATUM_NAME("Five")} ; \endcode
      
    Dynamically allocated datums are most easily initialised using a static
    datum as a template, eg: \code
    sw_datum * datum_from_intarray(int * intarray, int length)
    {
      static sw_datum num = SW_DATUM_INTEGER(0) ;     // integer template
      static sw_datum arr = SW_DATUM_ARRAY(NULL, 0) ; // array template
      sw_datum * datums = malloc((length+1) * sizeof(sw_datum)) ;
      int i ;
      datums[0] = arr ;     // initialise using template
      datums[0].length = length ;
      datums[0].value.opaque = &datums[1] ;
      for (i = 0 ; i < length ; ++i) {
        datums[i+1] = num ; // initialise using template
        datums[i+1].value.integer = intarray[i] ;
      }
      return datums ;
    } \endcode */
#define SW_DATUM_ARRAY(_array,_len) \
  {SW_DATUM_TYPE_ARRAY, 0,0,0, NULL, (_len), { (void *)(_array) }}

/** Static initialiser for sw_datum stack values. This is unlikely to be useful.
    \code sw_datum elements[] = {SW_DATUM_STRING("Just one")} ;
    sw_datum stack = SW_DATUM_STACK(&elements, SW_DATA_ARRAY_LENGTH(elements)) ; \endcode */
#define SW_DATUM_STACK(_stack,_len) \
  {SW_DATUM_TYPE_ARRAY, SW_DATUM_SUBTYPE_STACK_ARRAY,0,0, NULL, (_len), { (void *)(_stack) }}

/** Static initialiser for sw_datum dictionary values. e.g. \code
    sw_datum entries[] = {SW_DATUM_NAME("Key1"), SW_DATUM_STRING("Value 1"),
                          SW_DATUM_NAME("Key2"), SW_DATUM_INTEGER(2)} ;
    sw_datum dict = SW_DATUM_DICT(&entries[0], SW_DATA_DICT_LENGTH(entries)) ; \endcode */
#define SW_DATUM_DICT(_dict,_len) \
  {SW_DATUM_TYPE_DICT, 0,0,0, NULL, (_len), { (void *)(_dict) }}

/** Count the members of a local data array. Useful for statically
    initialising arrays. */
#define SW_DATA_ARRAY_LENGTH(_array) (sizeof(_array) / sizeof((_array)[0]))

/** Count the members of a local data dictionary. Useful for statically
    initialising dictionaries. */
#define SW_DATA_DICT_LENGTH(_dict) (SW_DATA_ARRAY_LENGTH(_dict) >> 1)


/** A constant of real value 0.0 for use with the SW_DATUM_FLOAT static
    initialiser. */
#define SW_DATUM_0_0F 0

/** A constant of real value 1.0 for use with the SW_DATUM_FLOAT static
    initialiser. */
#define SW_DATUM_1_0F 1065353216

/** A constant of real value 0.5 for use with the SW_DATUM_FLOAT static
    initialiser. */
#define SW_DATUM_0_5F 1056964608

/** Static initialiser for sw_datum float values, for use with the defined
    constants that can be used with this initialiser. e.g. one half:
    \code sw_datum half = SW_DATUM_FLOAT(SW_DATUM_0_5F) ; \endcode */
#define SW_DATUM_FLOAT(_real) \
  {SW_DATUM_TYPE_FLOAT, 0,0,0, NULL, 0, { (void *)((intptr_t)(_real)) }}

/** \brief A structure used for matching multiple entries in one go. */
typedef struct sw_data_match sw_data_match ;

/** \brief An opaque type used by the data provider to iterate data
    structures. */
typedef struct sw_data_iterator sw_data_iterator ;

/** \brief A structure containing callback functions for structured data
 * access.
 *
 * Data references provided from the RIP are automatically invalidated at the
 * end of the callback that passed them to a module. All data that is to be
 * stored between calls to the module must be copied from the RIP.
 *
 * Note that in the case of complex types such as strings, arrays and
 * dictionaries, it is not enough to copy the sw_datum - its contents must
 * be copied. This includes the bytes of a strings, and all the elements of
 * an array or dictionary. It is not usually necessary to copy datums in this
 * way.
 *
 * Creators of sw_datum complex types can implement this API, pointing the
 * datum's owner field at their sw_data_api structure, to enable on-demand
 * creation or delivery of array, stack and dictionary contents. In such a case,
 * the implementor uses the datum's value.opaque field for its own purposes.
 * A sw_data_api created for this purpose need only implement those methods that
 * are relevant - NULL methods return SW_DATA_ERROR_INVALIDACCESS if used.
 */
struct sw_data_api {
  /** Version number, name, display name, size. This is REQUIRED to be the
      first field. */
  sw_api_info info ;

  /* Entries present in SW_DATA_API_VERSION_20071111: */

  /** \brief Get an indexed value from an array or stack datum.

      \param[in] array An array or stack datum from which an indexed value will
      be extracted.

      \param[in] index The index of the value to extract. Zero means the first
      element of the array, or the topmost element of the stack.

      \param[out] value The location to be filled in with the returned value.

      \retval SW_DATA_OK Success. The \a value entry is filled with the
      indexed array or stack entry.

      \retval SW_DATA_ERROR_RANGECHECK Returned if the index is out of range.

      \retval SW_DATA_ERROR_TYPECHECK Returned if the indexed value is an
      unsupported type or the first parameter isn't an array.

      \retval SW_DATA_ERROR_INVALIDACCESS Returned if the array was not
      readable.
*/
  sw_data_result (RIPCALL *get_indexed)(/*@notnull@*/ /*@in@*/ const sw_datum *array,
                                        size_t index,
                                        /*@notnull@*/ /*@out@*/ sw_datum *value) ;

  /** \brief Store an indexed value in an array or stack datum.

      \param[in] array An array or stack datum into which an indexed value will
      be stored.

      \param[in] index The index at which to store the value. Zero means the
      first element of the array, or the topmost element of the stack.

      \param[in] value The datum to store into the array. Values in local
      module memory will be deep-copied when storing into RIP owned arrays or
      stacks.

      \retval SW_DATA_OK Returned if successful.

      \retval SW_DATA_ERROR_TYPECHECK Returned if the first parameter isn't an
      array.

      \retval SW_DATA_ERROR_RANGECHECK Returned if the index is out of range.

      \retval SW_DATA_ERROR_INVALIDACCESS Returned if the value cannot be
      stored into the array or stack.

      \retval SW_DATA_ERROR_MEMORY Returned if a memory allocation failed
      while trying to store the value.

      Note that using set_indexed to change an element of an array or stack will
      invalidate any opaque datum already fetched from that index - datums
      represent data, they are not necessarily an independent copy of that data.
      eg: \code

        sw_datum got, temp, change = SW_DATUM_INTEGER(0) ;
        (void)get_indexed(arr, 0, &got) ;      // ignore errors for this example
        (void)set_indexed(arr, 0, &change) ;   // implicitly invalidates 'got'
        if (got.type == SW_DATUM_TYPE_ARRAY) { // ERROR! got is no longer valid
          (void)get_indexed(&got, 0 &temp) ;   // CRASH!
      \endcode

      In the above example, array index 0 is changed after having been fetched,
      so the fetched opaque datum no longer represents the actual data. Any
      attempt to use that outdated representation can fail in unpredictable and
      serious ways.
*/
  sw_data_result (RIPCALL *set_indexed)(/*@notnull@*/ /*@in@*/ sw_datum *array,
                                        size_t index,
                                        /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

  /** \brief Get a keyed value from a dictionary datum.

      \param[in] dict A dictionary datum from which a keyed value will be
      extracted.

      \param[in] key A key for the value to extract. The type of the key may
      be restricted to transparent type (strings, integer, boolean, real,
      null), depending on the owner of the dictionary.

      \param[out] value The location to be filled in with the returned value.

      \retval SW_DATA_OK Success. The \a value entry is filled with the
      keyed array entry.

      \retval SW_DATA_ERROR_UNDEFINED Returned if the key does not exist in
      the dictionary.

      \retval SW_DATA_ERROR_TYPECHECK Returned if the keyed value is an
      unsupported type or the first parameter is not a dictionary.

      \retval SW_DATA_ERROR_INVALIDACCESS Returned if the dictionary was not
      readable.
*/
  sw_data_result (RIPCALL *get_keyed)(/*@notnull@*/ /*@in@*/ const sw_datum *dict,
                                      /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                      /*@notnull@*/ /*@out@*/ sw_datum *value) ;

  /** \brief Store a keyed value in a dictionary datum.

      \param[in] dict A dictionary datum into which a keyed value will be
      stored.

      \param[in] key A key for the value to store. The type of the key may be
      restricted to transparent type (strings, integer, boolean, real,
      null), depending on the owner of the dictionary.

      \param[in] value The datum to store into the dictionary. Values in local
      module memory will be deep-copied when stored in RIP owned dictionaries.

      \retval SW_DATA_OK Returned if successful.

      \retval SW_DATA_ERROR_TYPECHECK Returned if the first parameter is not a
      dictionary or the value is not of an appropriate type.

      \retval SW_DATA_ERROR_INVALIDACCESS Returned if the value cannot be
      stored into the dictionary.

      \retval SW_DATA_ERROR_MEMORY Returned if a memory allocation failed
      while trying to store the value.

      Note that like set_indexed, changing a dictionary value with set_keyed
      will invalidate any opaque datum fetched for that key. See set_indexed
      for details.
*/
  sw_data_result (RIPCALL *set_keyed)(/*@notnull@*/ /*@in@*/ sw_datum *dict,
                                      /*@notnull@*/ /*@in@*/ const sw_datum *key,
                                      /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

  /** \brief Match multiple data entries in a dictionary or array datum.

      \param[in] composite An array or dictionary datum from which values will
      be extracted.

      \param[in,out] match An array of match structures, determining the key
      to match, what types are acceptable for the match, how to treat
      type mismatches or missing values, and where to store the value.

      \param[in] match_length The number of entries to match.

      \retval SW_DATA_OK Success. The match result data are updated
      with the value of the extracted entries, or set to
      SW_DATUM_TYPE_NOTHING if the key was not matched but was optional.

      \retval SW_DATA_ERROR_UNDEFINED Returned if a required entry was not
      present. The match data should not be examined if this error occurs.

      \retval SW_DATA_ERROR_TYPECHECK Returned if a matched value has the
      wrong type, and the SW_DATA_TYPE_INVALID bit was not set in the allowed
      type mask of the match. The match data should not be examined if this
      error occurs. This error is also returned if the first parameter is not
      an array or dictionary.

      \retval SW_DATA_ERROR_INVALIDACCESS Returned if the data structure to
      match was not readable. The match data should not be examined if this
      error occurs.
*/
  sw_data_result (RIPCALL *match)(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                  /*@notnull@*/ /*@out@*/ sw_data_match *match,
                                  size_t match_length) ;

  /** \brief Start a new iteration over a dictionary or array datum.

      \param[in] composite The datum to iterate over.

      \param[out] iterator A pointer to an opaque iteration state.

      \retval SW_DATA_OK Returned if the iterator was started. \p
      iterate_end() must be called when complete to dispose of the iterator
      state.

      \retval SW_DATA_ERROR_TYPECHECK Returned if the composite datum is not of
      a suitable type to iterate.

      \retval SW_DATA_ERROR_MEMORY Returned if the iteration state could
      not be created because of a memory allocation failure.

      \retval SW_DATA_ERROR_INVALIDACCESS Returned if the composite datum
      was not readable.
  */
  sw_data_result (RIPCALL *iterate_begin)(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                          /*@notnull@*/ /*@out@*/ sw_data_iterator **iterator) ;

  /** \brief Get a key-value pair from a dictionary or array datum iteration.

      \param[in] composite The datum being iterated.

      \param[in] iterator An iterator state previously created by \p
      iterate_begin().

      \param[out] key A unique key identifying the datum in the composite
      structure. When iterating over arrays, this will be set to an integer
      datum with the index of the associated value.

      \param[out] value The value associated with \a key in the composite
      structure.

      \retval SW_DATA_OK Success. The \a key and \a value parameters
      are filled in.

      \retval SW_DATA_FINISHED Returned when there are no more keys
      to iterate over.

      \retval SW_DATA_ERROR_MEMORY Returned if the iteration
      state could not be updated because of a memory allocation failure.
  */
  sw_data_result (RIPCALL *iterate_next)(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                                         /*@notnull@*/ /*@in@*/ sw_data_iterator *iterator,
                                         /*@notnull@*/ /*@out@*/ sw_datum *key,
                                         /*@notnull@*/ /*@out@*/ sw_datum *value) ;

  /** \brief Discard resources associated with a data structure iterator
      state.

      If \p iterate_begin() succeeded, this function must be called to
      dispose of the iterator state.

      \param[in] composite The data structure being iterated.

      \param[in,out] iterator The location where the iterator state is
      stored. The iterator state pointer will be invalidated on exit.
  */
  void (RIPCALL *iterate_end)(/*@notnull@*/ /*@in@*/ const sw_datum *composite,
                              /*@notnull@*/ /*@in@*/ sw_data_iterator **iterator) ;

  /** \brief Compare two datums for equality.

      \param[in] one The first sw_datum.

      \param[in] two The second sw_datum.

      \retval FALSE the values were of incomparable types, or were not equal.

      \retval TRUE the values were of comparable types of the same value.

      This is a strict equality test for composites, but is otherwise an
      equivalence test, so the numeric value of a float can be equal to the
      value of an integer, for example. For composites such as arrays this does
      not compare the contents, but tests whether they are the same array.
  */
  HqBool (RIPCALL *equal)(/*@notnull@*/ /*@in@*/ const sw_datum *one,
                          /*@notnull@*/ /*@in@*/ const sw_datum *two) ;

  /** \brief Open a SW_DATUM_TYPE_BLOB datum for access via the sw_blob_api
    interface.

    \param[in] datum A datum of type SW_DATUM_TYPE_BLOB.

    \param[in] mode An access mode of SW_RDONLY, SW_WRONLY, or SW_RDWR,
    possibly combined with the flags SW_FONT and SW_EXCL, as defined in
    swdevice.h. If none of the access mode flags are set, the RIP will derive
    the access mode from the datum itself.

    \param[in] blob A pointer to a handle for a sw_blob_instance.

    \returns SW_DATA_OK if a blob was opened. In this case a valid blob
    handle will be stored in blob. The caller must release the blob reference
    when it has finished accessing it using the sw_blob_api::close method. If
    the blob could not be opened, then one of the sw_data_result error codes
    will be returned.
  */
  sw_data_result (RIPCALL *open_blob)(/*@notnull@*/ /*@in@*/ const sw_datum *datum,
                                      int32 mode,
                                      /*@notnull@*/ /*@out@*/ sw_blob_instance **blob) ;

  /* End of entries present in SW_DATA_API_VERSION_20071111 */

  /** \brief Pop the top element from the stack. A stack is an array datum of
      subtype SW_DATUM_SUBTYPE_STACK_ARRAY.

      \param[in] stack A stack datum from which the datum will be removed.

      \retval SW_DATA_OK Success. The top entry was popped from the stack.

      \retval SW_DATA_ERROR_RANGECHECK Returned if the stack is empty.

      \retval SW_DATA_ERROR_TYPECHECK Returned if stack is not an array datum
      of subtype SW_DATUM_SUBTYPE_STACK_ARRAY.

      \retval SW_DATA_ERROR_INVALIDACCESS Returned if the stack was not
      readable or mutable.

      Note that like set_indexed, pop implicitly invalidates any opaque datum
      representing the value that has been popped from the stack. eg: \code

        sw_datum top, temp ;
        (void)get_indexed(stack, 0, &top) ;    // get topmost element
        (void)pop(stack) ;                     // implicitly invalidates 'top'
        if (top.type == SW_DATUM_TYPE_ARRAY) { // ERROR! Invalid opaque type
          (void)get_indexed(stack, 0, &temp) ; // CRASH!
      \endcode

      Do not change or pop an opaque datum until you have finished with it.
      See set_indexed for more details.
  */
  sw_data_result (RIPCALL *pop)(/*@notnull@*/ /*@in@*/ sw_datum *stack) ;

  /** \brief Push an entry onto a stack. A stack is an array datum of subtype
      SW_DATUM_SUBTYPE_STACK_ARRAY.

      \param[in] stack A stack datum onto which the datum will be added.

      \param[in] value The datum to be pushed onto the top of the stack.

      \retval SW_DATA_OK Success. The \a value entry has been pushed onto the
      top of the stack.

      \retval SW_DATA_ERROR_RANGECHECK Returned if the stack is full.

      \retval SW_DATA_ERROR_TYPECHECK Returned if stack is not an array datum
      of subtype SW_DATUM_SUBTYPE_STACK_ARRAY or the value is not of a suitable
      type.

      \retval SW_DATA_ERROR_MEMORY Returned if a memory allocation failed
      while trying to push the value.

      \retval SW_DATA_ERROR_INVALIDACCESS Returned if the stack was not
      writeable.
  */
  sw_data_result (RIPCALL *push)(/*@notnull@*/ /*@in@*/ sw_datum *stack,
                                 /*@notnull@*/ /*@in@*/ const sw_datum *value) ;

  /* End of entries present in SW_DATA_API_VERSION_20130321 */
} ;

/** \brief A structure used for extracting multiple values from an array or
    dictionary in one go.

    The sw_data_api::match() method takes an array of these structures to
    fill in. Each entry in the array specifies a mask of allowable value
    types to match, a key to match, and storage for the extracted value. */
struct sw_data_match {
  /* Entries present in SW_DATA_API_VERSION_20071111: */

  /** A bitmask indicating which types the matched value may take. The bit
      value to match a type should be (1u << SW_DATUM_TYPE_*). Symbols are
      defined to make this easier such as SW_DATUM_BIT_INTEGER - see Data Type
      Bits above.
      
      Optional values are indicated by including (1u << SW_DATUM_TYPE_NOTHING)
      (or SW_DATUM_BIT_NOTHING) in the mask; if the key is not found, then the
      type of \a value will be set to SW_DATUM_TYPE_NOTHING. Lax checking of
      values may be enabled by  including (1u << SW_DATUM_TYPE_INVALID) (or
      SW_DATUM_BIT_INVALID) in the mask; if the keyed value exists but the type
      was not matched to any other bit, \a value's type field will be set to
      SW_DATUM_TYPE_INVALID. */
  uint32 type_mask ;
  sw_datum key ;    /**< The key of the field to be matched. */
  sw_datum value ;  /**< The matched value. */

  /* End of entries present in SW_DATA_API_VERSION_20071111 */
} ;

/** \brief A pair of initialiser macros that help build the above match
    structure.
    
    For example: \code
    static sw_data_match match[] = {
      SW_DATUM_OPTIONALKEY(BOOLEAN,"Verbose"),
      SW_DATUM_REQUIREDKEY(INTEGER,"Number"),
      SW_DATUM_REQUIREDKEY(STRING, "Name")
    } ; \endcode
    
    Note the shortening of the type field for simplicity in the usual case - the
    macro prepends SW_DATUM_BIT_ to this. Consequently, if more than one type is
    acceptable then others must be ORed in full, eg: \code
    SW_DATUM_OPTIONALKEY(FLOAT|SW_DATUM_BIT_INTEGER, "Number"), \endcode */

#define SW_DATUM_REQUIREDKEY(type,name) \
  {SW_DATUM_BIT_##type,SW_DATUM_STRING(""name""),SW_DATUM_NOTHING}

#define SW_DATUM_OPTIONALKEY(type,name) \
  {SW_DATUM_BIT_##type|SW_DATUM_BIT_NOTHING,SW_DATUM_STRING(""name""),SW_DATUM_NOTHING}

/** \brief A utility macro to extract an integer value from a sw_datum
    object of integer or float type.

    This prototype exists for Splint and Doxygen. There is no actual function
    of this name.

    \param[in] datum The object from which to extract an integer.

    \param[out] value The returned integer value.

    \param[out] result The return value of the macro.

    \retval SW_DATA_OK Success. The \a value entry is filled with the
    integer value of the datum.

    \retval SW_DATA_ERROR_RANGECHECK Returned if the datum value is not
    exactly representable as an integer.

    \retval SW_DATA_ERROR_TYPECHECK Returned if the datum value is not an
    integer or a real.

    \retval SW_DATA_ERROR_INVALIDACCESS Returned if the datum is invalid.
 */
void sw_datum_get_integer(/*@notnull@*/ /*@in@*/ const sw_datum *datum,
                          int value,
                          sw_data_result result) ;

#define sw_datum_get_integer(datum_, value_, result_) MACRO_START \
  const sw_datum *_datum_ = (datum_) ;                            \
  if ( _datum_ == NULL ) {                                        \
    (value_) = 0 ; /* Initialisation for dumb compilers */        \
    (result_) = SW_DATA_ERROR_INVALIDACCESS ;                     \
  } else if ( _datum_->type == SW_DATUM_TYPE_INTEGER ) {          \
    (value_) = _datum_->value.integer ;                           \
    (result_) = SW_DATA_OK ;                                      \
  } else if ( _datum_->type == SW_DATUM_TYPE_FLOAT ) {            \
    int _intvalue_ = (int)_datum_->value.real ;                   \
    if ( _datum_->value.real != _intvalue_ ) {                    \
      (value_) = 0 ; /* Initialisation for dumb compilers */      \
      (result_) = SW_DATA_ERROR_RANGECHECK ;                      \
    } else {                                                      \
      (value_) = _intvalue_ ;                                     \
      (result_) = SW_DATA_OK ;                                    \
    }                                                             \
  } else {                                                        \
    (value_) = 0 ; /* Initialisation for dumb compilers */        \
    (result_) = SW_DATA_ERROR_TYPECHECK ;                         \
  }                                                               \
MACRO_END

/* Typedefs to make the sw_datum_get_real prototype clearer */
typedef int float_or_double_specifier ;
typedef double float_or_double ;

/** \brief A utility macro to extract a real value from a sw_datum
    object of integer or float type.

    This prototype exists for Splint and Doxygen. There is no actual function
    of this name.

    \param[in] datum The object from which to extract a value.

    \param[in] type The required type of value - this MUST be float or double.

    \param[out] value The real value extracted from the sw_datum. This must be
    a float or double.

    \param[out] result The return value of the extraction.

    \retval SW_DATA_OK Success. The \a value entry is filled with the
    real value of the datum.

    \retval SW_DATA_ERROR_TYPECHECK Returned if the datum value is not an
    integer or a real.

    \retval SW_DATA_ERROR_INVALIDACCESS Returned if the datum is invalid.
 */
void sw_datum_get_real(/*@notnull@*/ /*@in@*/ const sw_datum *datum,
                       float_or_double_specifier type,
                       float_or_double value,
                       sw_data_result result) ;

#define sw_datum_get_real(datum_, type_, value_, result_) MACRO_START \
  const sw_datum *_datum_ = (datum_) ;                            \
  if ( _datum_ == NULL ) {                                        \
    (value_) = (type_)0 ; /* Initialisation for dumb compilers */ \
    (result_) = SW_DATA_ERROR_INVALIDACCESS ;                     \
  } else if ( _datum_->type == SW_DATUM_TYPE_INTEGER ) {          \
    (value_) = (type_)_datum_->value.integer ;                    \
    (result_) = SW_DATA_OK ;                                      \
  } else if ( _datum_->type == SW_DATUM_TYPE_FLOAT ) {            \
    (value_) = _datum_->value.real ;                              \
    (result_) = SW_DATA_OK ;                                      \
  } else {                                                        \
    (value_) = (type_)0 ; /* Initialisation for dumb compilers */ \
    (result_) = SW_DATA_ERROR_TYPECHECK ;                         \
  }                                                               \
MACRO_END

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWDATAAPI_H__ */
