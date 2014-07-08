#ifndef __XMLGATTRS_H__
#define __XMLGATTRS_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!export:xmlgattrs.h(EBDSDK_P.1) $
 * $Id: export:xmlgattrs.h,v 1.16.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \ingroup libgenxml
 * \brief Public interface for XML attributes manipulation.
 */

#include "xmlgtype.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Allocate an XML attributes structure.
 *
 * \retval FALSE allocation failed
 * \retval TRUE allocation succeeded
 */
extern
HqBool xmlg_attributes_create(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@out@*/ /*@null@*/ /*@owned@*/
      xmlGAttributes **attributes) ;

/**
 * \brief Insert a new item into the attribute hash.
 *
 * \retval FALSE insertion failed.
 * \retval TRUE insertion succeeded.
 */
extern HqBool xmlg_attributes_insert(
      /*@in@*/ /*@notnull@*/
      xmlGAttributes *attributes,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *attrlocalname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *attrprefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *attruri,
      /*@in@*/ /*@notnull@*/
      const uint8 *attrvalue,
      uint32 attrvalue_len
) ;

/**
 * \brief Lookup an attribute via its name and URI
 *
 * \returns NULL if attribute/URI does not exist in the attribute list.
 * Otherwise returns pointer to attributes string value.
 */
extern /*@null@*/ /*@observer@*/
HqBool xmlg_attributes_lookup(
      /*@in@*/ /*@notnull@*/
      xmlGAttributes *attributes,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *attrlocalname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *attruri,
      /*@in@*/ /*@null@*/
      const xmlGIStr **attrprefix,
      const uint8 **attrvalue,
      uint32 *attrvaluelen) ;

/**
 * \brief Scan attribute list callback.
 *
 * \param handler The parse handler.
 * \param localname The name of the element on which the attributes are set.
 * \param uri The URI of the element on which the attributes are set.
 * \param attrlocalname The name of the attribute being scanned.
 * \param attrprefix The namespace prefix of the attribute being scanned.
 * \param attruri The URI of the attribute being scanned.
 * \param attrvalue The UTF-8 string value of the attribute.
 * \param attrvaluelen The length of the attribute UTF-8 string.
 *
 * \retval TRUE Success. Scanning will continue.
 * \retval FALSE Abort. Scanning will cease.
 */
typedef
HqBool (*xmlGAttributesScanCallback)(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xmlGAttributes *attributes,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *attrlocalname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *attrprefix,
      /*@in@*/ /*@null@*/
      const xmlGIStr *attruri,
      /*@in@*/ /*@null@*/
      const uint8 *attrvalue,
      uint32 attrvaluelen) ;

/**
 * \brief Interate over all attributes calling specified callback function.
 *
 * \param filter XML filter pointer.
 * \param attributes Pointer to element attributes to scan.
 * \param localname Pointer to localname to pass to callback.
 * \param uri Pointer to URI to pass to callback.
 * \param f Pointer to callback to call for each attribute.
 *
 * \retval FALSE if callback returns false for an attribute. In this case it is
 * not guaranteed that the callback has been called for all attributes.
 * \retval TRUE if callback returns true for all attributes.
 */
extern
HqBool xmlg_attributes_scan_full(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xmlGAttributes *attributes,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@null@*/
      xmlGAttributesScanCallback f) ;

/** \brief Structure to match attributes for an element.
 *
 * The URI will normally be NULL, since XML attributes are not
 * associated with a namespace unless explicitly prefixed. If the
 * attribute is optional, a pointer to a boolean should be supplied;
 * the boolean will be updated if the attribute match succeeds, to
 * indicate whether the optional attribute was present or not. (If an
 * attribute is required, use NULL for the \c optional field.) The \c
 * data field will usually be used as an output field, as a location
 * to put the results of the type conversion. Occasionally it may be
 * used as an input field or both input and output. For example, when
 * noting resource references for later expansion, the interned name
 * of the expanded child element is passed in through the data field.
 */
typedef struct {
  const xmlGIStr *name ; /**< The interned name of the attribute to
                            match. */
  /*@null@*/
  const xmlGIStr *uri ; /**< The interned URI of the attribute to
                           match. */
  HqBool *optional ; /**< NULL if required attribute, otherwise
                        pointer to flag indicating if attribute was
                        present. */
  xmlGTypeConverter convert ; /**< A type converter function to call
                                 when the attribute is matched. */
  /*@null@*/
  void *data ; /**< User data field passed to the type converter
                  function. */
} XML_ATTRIBUTE_MATCH ;

/** \brief Final entry in attribute match array. */
#define XML_ATTRIBUTE_MATCH_END {NULL, NULL, NULL, NULL, NULL}

/**
 * \brief Match attributes, calling type converters for each type.
 *
 * \param filter A filter for throwing errors
 * \param localname The localname of the element.
 * \param uri The uri of the element.
 * \param attributes The attributes to match.
 * \param match A table of attribute names, URIs, and callback type converter
 *              functions.
 * \param check_unmatched Force checking of attributes not present in match.
 *
 * \retval TRUE All required attributes were present, and their type converters
 *              returned true values.
 * \retval FALSE Either a required attribute was not present, or its type
 *               converter did not return true.
 *
 * If the type converters create complex types, the caller must cope with
 * disposal if this function fails. This function may fail as a result of
 * calling a type converter, it will not perform any rollback on
 * previously-called converters. There is no guarantee of the order in which
 * type converters are called.
 */
extern
HqBool xmlg_attributes_match(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@null@*/ /*@observer@*/
      xmlGAttributes *attributes,
      /*@in@*/ /*@null@*/ /*@observer@*/
      XML_ATTRIBUTE_MATCH match[],
      HqBool check_unmatched) ;

/* If you write your own scanner, you can invoke your own match
   errors as if though the match call had been used. This aids in
   keeping error messages in the same format across code
   boundries. */
extern
HqBool xmlg_attributes_invoke_match_error(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      const xmlGIStr *attr_localname) ;

/**
 * \brief Increment attribute structure reference count.
 */
extern
void xmlg_attributes_reserve(
      /*@in@*/ /*@notnull@*/
      xmlGAttributes *attributes) ;

/**
 * \brief Destroy the attribute table.
 */
extern
void xmlg_attributes_destroy(
      /*@only@*/ /*@in@*/ /*@notnull@*/
      xmlGAttributes **attributes) ;

extern
void  xmlg_attributes_remove(
      /*@in@*/ /*@notnull@*/
      xmlGAttributes *attributes,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *attrlocalname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *attruri) ;

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLGATTRS_H__*/
