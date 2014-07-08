#ifndef __HQNURI_H__
#define __HQNURI_H__
/* ============================================================================
 * $HopeName: HQNuri!export:hqnuri.h(EBDSDK_P.1) $
 * $Id: export:hqnuri.h,v 1.6.4.1.1.1 2013/12/19 11:24:42 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Public interface for HQN URI interface.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "hqtypes.h" /* HQN basic types. */
#include <stddef.h>  /* size_t */

/* URI field bit masks. */
#define HQN_URI_SCHEME    0x00000001

/* Its either a hierachical part with segments below or opaque - this
   is a branch in the data stucture. */
#define HQN_URI_OPAQUE    0x00000002

/* Either an authority or its broken up into user@hostname:port - this
   is a branch case. Asking for authority will always return the
   authority segment - even if it is broken up into
   user@hostname:port. */
#define HQN_URI_AUTHORITY 0x00000004

#define HQN_URI_HOST      0x00000008
#define HQN_URI_USER      0x00000010
#define HQN_URI_PORT      0x00000020

#define HQN_URI_PATH      0x00000080
#define HQN_URI_QUERY     0x00000100
#define HQN_URI_FRAGMENT  0x00000200

/* The entire uri - special case, could be opaque or the hier */
#define HQN_URI_ENTIRE    0x00000800

/* The path, query and fragment */
#define HQN_URI_NAME      (HQN_URI_PATH | HQN_URI_QUERY | HQN_URI_FRAGMENT)

typedef struct hqn_uri_context_t hqn_uri_context_t ;

typedef struct hqn_uri_t hqn_uri_t ;

typedef struct hqn_uri_memhandler_t {
  /*@only@*/ /*@null@*/
  void * (*f_malloc)(
      size_t size) ;

  void * (*f_realloc)(
      /*@only@*/ /*@out@*/ /*@null@*/
      void *mem_addr,
      size_t size) ;

  void (*f_free)(
      /*@only@*/ /*@in@*/ /*@null@*/
      void *mem_addr) ;

} hqn_uri_memhandler_t ;

extern
HqBool hqn_uri_init(
      /*@out@*/ /*@null@*/
      hqn_uri_context_t **uri_context,
      /*@in@*/ /*@notnull@*/
      hqn_uri_memhandler_t *memory_handler) ;

extern
void hqn_uri_finish(
      /*@in@*/ /*@notnull@*/
      hqn_uri_context_t **uri_context) ;

extern
HqBool hqn_uri_new_empty(
      /*@in@*/ /*@notnull@*/
      hqn_uri_context_t *uri_context,
      /*@out@*/ /*@null@*/
      hqn_uri_t **new_uri_object) ;

/* NOTE: When copy_string is TRUE, the string provided will be copied
   into a buffer within the URI. If FALSE, the uri string passed to
   the parse routines MUST survive the lifetime of the URI. Its up to
   the calling code to ensure this. */
extern
HqBool hqn_uri_parse(
      /*@in@*/ /*@notnull@*/
      hqn_uri_context_t *uri_context,
      /*@out@*/ /*@null@*/
      hqn_uri_t **new_uri_object,
      /*@in@*/ /*@notnull@*/
      const uint8 *uri,
      uint32 uri_len,
      HqBool copy_string) ;

extern
HqBool hqn_uri_copy(
      /*@in@*/ /*@notnull@*/
      hqn_uri_context_t *uri_context,
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *from_uri_object,
      /*@out@*/ /*@null@*/
      hqn_uri_t **new_uri_object) ;

extern
HqBool hqn_uri_rel_parse(
      /*@in@*/ /*@notnull@*/
      hqn_uri_context_t *uri_context,
      /*@out@*/ /*@null@*/
      hqn_uri_t **new_uri_object,
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *base_uri_object,
      /*@in@*/ /*@notnull@*/
      const uint8 *rel_uri,
      uint32 rel_len) ;

extern
void hqn_uri_free(
      /*@in@*/ /*@notnull@*/
      hqn_uri_t **uri_object) ;

/**
 * Return a pointer to a buffer which contains the specified fields
 * from the URI. The buffer points to memory which belongs to the URI
 * so MUST NOT be de-allocated.
 */
extern
HqBool hqn_uri_get_field(
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *uri_object,
      /*@in@*/ /*@notnull@*/
      uint8 **field,
      /*@in@*/ /*@notnull@*/
      uint32 *field_len,
      uint32 which_fields) ;

extern
HqBool hqn_uri_set_field(
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *uri_object,
      uint32 field,
      /*@in@*/ /*@notnull@*/
      uint8 *buf,
      uint32 buf_len) ;

/**
 * Validate the percent encoding in the passed URI string. If the passed string
 * contains any '%' characters, they must be followed by two hex digits,
 * otherwise FALSE will be returned.
 *
 * \param string The string to validate.
 * \param length The length of the string to validate.
 * \param decodedLength The length of the string after percent decoding.
 * \return TRUE if the percent encoding is valid.
 */
HqBool hqn_uri_validate_percent_encoding(uint8* string, int32 length,
                                         int32* decodedLength);

/**
 * Percent decode the passed URI string.
 *
 * \param string The uri string.
 * \param length The length of 'string'.
 * \param decoded The decoded string will be written to this buffer.
 * \param decodedLength The allocated length of 'decoded'. This should be
 *        determined by first calling validatePercentEncoding().
 */
void hqn_uri_percent_decode(uint8* string, int32 length,
                            uint8* decoded, int32 decodedLength);

/**
 * Identify the leaf name within the passed URI.
 * For example, the leaf name of "http://dir1/test.pdf?a=1#anchor" is
 * "test.pdf". The leaf name of "test.pdf" is the whole string. The leaf name
 * of "/dir1/dir2/" is an empty string.
 *
 * \param uri The URI.
 * \param length The length of 'uri'.
 * \param start This will be set to the index of the first character of the
 *        leaf name within 'uri'.
 * \param leafLength This will be set to the length of the leaf name. This will
 *        be set to zero if the uri denotes a directory.
 */
void hqn_uri_find_leaf_name(uint8* uri, int32 length,
                            int32 *start, int32 *leafLength);

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__HQNURI_H__*/
