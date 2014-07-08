/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!export:xmltypeconv.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Type converters for XML
 */

#ifndef __XMLTYPECONV_H__
#define __XMLTYPECONV_H__

#include "xmlg.h"
#include "hqunicode.h"

/**
 * \brief Id type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 string.
 * \param data Location to store the utf8_buffer Id.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 *
 * This function implements the ID datatype from XML schema recommendation
 * http://www.w3.org/TR/2001/REC-xmlschema-2-20010502/.
 */
Bool xml_convert_id(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                    /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                    /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                    /*@notnull@*/ void *data /* utf8_buffer* */) ;

/**
 * \brief Boolean type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 string.
 * \param data Location to store a Bool
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 *
 * This function implements the boolean datatype from XML schema recommendation
 * http://www.w3.org/TR/2001/REC-xmlschema-2-20010502/.
 */
Bool xml_convert_boolean(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                         /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                         /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                         /*@notnull@*/ void *data /* Bool* */) ;

/**
 * \brief xml:lang type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 string.
 * \param data Location to store an intern.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 *
 * This function implements the xml:lang datatype from
 * http://www.ietf.org/rfc/rfc3066.txt. It only tests the syntax as
 * per that specification, not that thelanguage is a currently
 * registered language code.
 */
Bool xml_convert_lang(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                      /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                      /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                      /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief enumeration type converter with a base of string.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 string.
 * \param data Location to store an intern.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 *
 * This function implements the enumeration constraint defined from
 * http://www.w3.org/TR/xmlschema-2/#rf-enumeration with a base of
 * string with no white space collapse.
 */
Bool xml_convert_string_enum(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                             /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                             /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                             /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief integer type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 string.
 * \param data Location to store an integer
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 *
 * This function implements the integer datatype from XML schema recommendation
 * http://www.w3.org/TR/2004/REC-xmlschema-2-20041028/datatypes.html#integer
 */
Bool xml_convert_integer(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* int32* */) ;

/**
 * \brief double type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 string.
 * \param data Location to store a double
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 *
 * This function implements the double datatype from XML schema recommendation
 * http://www.w3.org/TR/2004/REC-xmlschema-2-20041028/datatypes.html#built-in-primitive-datatypes
 */
Bool xml_convert_double(xmlGFilter *filter,
                        xmlGIStr *attrlocalname,
                        utf8_buffer* value,
                        void *data /* double* */) ;

/**
 * \brief float type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 string.
 * \param data Location to store a float
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 *
 * This function implements the float datatype from XML schema recommendation
 * http://www.w3.org/TR/2004/REC-xmlschema-2-20041028/datatypes.html#built-in-primitive-datatypes
 */
Bool xml_convert_float(xmlGFilter *filter,
                       xmlGIStr *attrlocalname,
                       utf8_buffer* value,
                       void *data /* float* */) ;

/**
 * \brief XML space matching
 *
 * \param input Pointer to a UTF-8 string.
 *
 * \retval Returns the number of whitespace characters consumed.
 *
 * Consumes any spaces from the start of the UTF-8 string.
 */
uint32 xml_match_space(/*@in@*/ /*@notnull@*/ utf8_buffer* input);

/**
 * \brief XML whitespace matching
 *
 * \param input Pointer to a UTF-8 string.
 *
 * \retval Returns the number of whitespace characters consumed.
 *
 * Consumes any whitespace from the start of the UTF-8 string.
 */
uint32 xml_match_whitespace(/*@in@*/ /*@notnull@*/ utf8_buffer* input);

/**
 * \brief XML string matching
 *
 * \param string Pointer to a UTF-8 string examined for a match.
 * \param match A UTF-8 string to match.
 *
 * \retval TRUE The match was present. The location and length of the UTF-8
 *              string are updated to a point at the first code unit after
 *              the match.
 * \retval FALSE No match was present. The location and length of the
 *               UTF-8 string are not updated.
 *
 * \note Neither of the strings are examined for validity, so partial code unit
 *       matches could succeed if used. This behaviour is not guaranteed in
 *       future versions, which may validate both strings.
 */
Bool xml_match_string(/*@in@*/ /*@notnull@*/ utf8_buffer* string,
                      /*@in@*/ /*@notnull@*/ const utf8_buffer* match);

/**
 * \brief XML Unicode value matching
 *
 * \param string Pointer to a UTF-8 string examined for a match.
 * \param unicode A Unicode value to match.
 *
 * \retval TRUE The match was present. The location and length of the UTF-8
 *              string are updated to a point at the first code unit after
 *              the match.
 * \retval FALSE No match was present. The location and length of the
 *               UTF-8 string are not updated.
 */
Bool xml_match_unicode(/*@in@*/ /*@notnull@*/ utf8_buffer* string,
                       UTF32 unicode) ;

/**
 * \brief
 * Test if a character is XML whitespace.
 */
#define IS_XML_WHITESPACE(ch) \
  ((ch) == ' ' || (ch) == '\r' || (ch) == '\n' || (ch) == '\t')

/**
 * \brief
 * Case clauses for XML whitespace; use as case XML_WHITESPACE_CASES:.
 */
#define XML_WHITESPACE_CASES ' ': case '\r': case '\n': case '\t'

/*
Log stripped */
#endif /*!__XMLTYPECONV_H__*/
