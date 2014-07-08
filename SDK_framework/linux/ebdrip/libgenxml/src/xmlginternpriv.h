#ifndef __XMLGINTERNPRIV_H__
#define __XMLGINTERNPRIV_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlginternpriv.h(EBDSDK_P.1) $
 * $Id: src:xmlginternpriv.h,v 1.9.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 * 
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/* \file
 * \brief Implements default interned string pool.
 */

#include "xmlgintern.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Interned string pool.
 *
 * This is an opaque pointer for libgenxml.
 */
typedef struct xmlGInternPool xmlGInternPool;

/** \brief Set up the default intern string handler. */
HqBool xmlg_intern_default(
      /*@notnull@*/ /*@out@*/
      xmlGContext *xml_ctxt) ;

/*
Now exported as a public function.

#define xmlg_istring_create(c, i, s, l) \
  (*(c)->intern_handler.f_intern_create)((c), (i), (s), (l))
*/

/** \brief Convenience macro to test and call reserve method. */
#define xmlg_istring_reserve(c, s)                    \
  ((s) == NULL ||                                   \
   (c)->intern_handler.f_intern_reserve == NULL ||    \
   (*(c)->intern_handler.f_intern_reserve)((c), (s)))

/** \brief Convenience macro to test and call destroy method. */
#define xmlg_istring_destroy(c, s) MACRO_START           \
  if ( (s) != NULL && *(s) != NULL &&                  \
       (c)->intern_handler.f_intern_destroy != NULL )    \
    (*(c)->intern_handler.f_intern_destroy)((c), (s)) ;  \
MACRO_END

/** \brief Convenience macro to call string hash method. */
#define xmlg_istring_hash(c, s) \
  (*(c)->intern_handler.f_intern_hash)((c), (s))

/** \brief Convenience macro to call string length method. */
#define xmlg_istring_length(c, s) \
  ((*(c)->intern_handler.f_intern_length)((c), (s)))

/** \brief Convenience macro to call string value method. */
#define xmlg_istring_value(c, s) \
  ((*(c)->intern_handler.f_intern_value)((c), (s)))

/** \brief Convenience macro to call string equal method. */
#define xmlg_istring_equal(c, s1, s2) \
  ((s1) == (s2) || (*(c)->intern_handler.f_intern_equal)((c), (s1), (s2)))

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLGINTERNPRIV_H__*/
