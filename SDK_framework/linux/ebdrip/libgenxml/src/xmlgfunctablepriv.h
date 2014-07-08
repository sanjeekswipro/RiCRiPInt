#ifndef __XMLGFUNCTABLEPPRIV_H__
#define __XMLGFUNCTABLEPPRIV_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgfunctablepriv.h(EBDSDK_P.1) $
 * $Id: src:xmlgfunctablepriv.h,v 1.9.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 * 
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief XML function tables.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern
void xmlg_set_funct_table(
      xmlGFilter *filter,
      xmlGFunctTable *table) ;

extern /*@null@*/
xmlGFunctTable* xmlg_remove_funct_table(
      xmlGFilter *filter) ;

/**
 * \brief Create a new XML callback function table.
 *
 * \param filter_chain XML filter context pointer.
 * \param table The address of a pointer to a function table.
 *
 * \returns TRUE on success, FALSE on failure.
 */
extern
HqBool xmlg_funct_table_create(
      /*@in@*/ /*@notnull@*/
      xmlGFilterChain *filter_chain,
      /*@out@*/ /*@notnull@*/
      xmlGFunctTable **table) ;

/**
 * \brief Register a single end element callback in a function table.
 */
extern
HqBool xmlg_funct_table_register_end_element_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFunctTable *table,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *istr_localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *istr_uri,
      /*@null@*/
      xmlGEndElementCallback f) ;

/**
 * \brief Destroy a XML callback function table.
 *
 * \param table The address of a pointer to a function table.
 */
extern
void xmlg_funct_table_destroy(
      /*@only@*/ /*@in@*/ /*@notnull@*/
      xmlGFunctTable **table) ;

/**
 * \brief Register a single start element callback in a function table.
 */
extern
HqBool xmlg_funct_table_register_start_element_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFunctTable *table,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *istr_localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *istr_uri,
      /*@null@*/
      xmlGStartElementCallback f) ;

/**
 * \brief Register a single characters callback in a function table.
 */
extern
HqBool xmlg_funct_table_register_characters_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFunctTable *table,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *istr_localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *istr_uri,
      /*@null@*/
      xmlGCharactersCallback f) ;

/**
 * \brief Remove a single start element callback in a function table.
 */
extern /*@null@*/
xmlGStartElementCallback xmlg_funct_table_remove_start_element_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFunctTable *table,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *istr_localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *istr_uri) ;

/**
 * \brief Remove a single end element callback in a function table.
 */
extern /*@null@*/
xmlGEndElementCallback xmlg_funct_table_remove_end_element_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFunctTable *table,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *istr_localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *istr_uri) ;

/**
 * \brief Remove a single characters callback in a function table.
 */
extern /*@null@*/
xmlGCharactersCallback xmlg_funct_table_remove_characters_cb(
      /*@in@*/ /*@notnull@*/
      xmlGFunctTable *table,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *istr_localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *istr_uri) ;

extern
xmlGStartElementCallback xmlg_start_funct_table_lookup(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      HqBool *found) ;

extern
xmlGEndElementCallback xmlg_end_funct_table_lookup(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      HqBool *found) ;

extern
xmlGCharactersCallback xmlg_characters_funct_table_lookup(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      HqBool *found) ;

HqBool xmlg_funct_table_degrade_lookup(
      xmlGFunctTable *table,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGStartElementCallback *fs,
      xmlGCharactersCallback *fc,
      xmlGEndElementCallback *fe) ;

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLGFUNCTABLEPPRIV_H__*/
