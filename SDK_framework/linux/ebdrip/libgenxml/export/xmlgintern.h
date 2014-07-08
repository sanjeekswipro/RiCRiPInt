#ifndef __XMLGINTERN_H__
#define __XMLGINTERN_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!export:xmlgintern.h(EBDSDK_P.1) $
 * $Id: export:xmlgintern.h,v 1.10.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 * 
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */

/**
 * \file
 * \brief Public interface for intern strings.
 *
 * Design goals:
 *
 * Do not limit how interned strings might be implemented. This especially
 * applies to the reserve callback which is called when any additional
 * references to interned strings are kept within XMLG after the initial
 * create. This call in particular allows a reference count based interning
 * mechanism to be implemented. Other implementations might include keeping all
 * interned strings created until the pool terminate function is called and
 * making the reserve and destroy functions essentially noop functions.
 *
 * Do not assume a particular character encoding within the intern interface.
 * Eg: Do not assume that the byte array is NUL terminated. In UTF-8, NUL is
 * a valid codepoint and therefore does not necessarily signify the end of a
 * string. Hence, for efficient implementation, a bytelen ought to be used.
 *
 * Scoping rules:
 *
 * All non interned strings survive the life time of the callback in which they
 * exist. ie. All non-interned strings are de-allocated by XMLG after the
 * callback. This currently only applies to attribute values.
 *
 * All interned strings created during a parse MUST survive the life time the
 * entire parse. XMLG will call reserve and destroy appropriately.
 *
 * All interned strings created during function table building must survive
 * for the lifetime of the function table. XMLG will call reserve and destroy
 * appropriately.
 *
 * Because of this, the built in intern pool survives for the life time of the
 * XML sub-system. A more effecient intern mechansim may well use a
 * save/restore high water mark before and after each XML parse instance so
 * that newly interned strings during a parse would be de-allocated.
 */

#include "xmlgtype.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Intern string handler functions. */
typedef struct xmlGIStrHandler xmlGIStrHandler ;

/**
 * \brief Intern a string into the specified string pool.
 *
 * If the string does not exist in the intern pool, a copy should be made so
 * that the supplied string may be de-allocated. If the string exists within
 * the intern pool, the existing interned string pointer ought to be returned
 * and no new memory is allocated.
 *
 * XMLG will intern element names, their prefix and URI (if they exist), all
 * attribute names and their prefix and URI (if they
 * exist). Attribute values will not be interned.
 *
 * The generic XML parser will call destroy on all element names, prefixes,
 * URI's, attribute names and their prefix and URI (if they exist) at the end
 * of the start element callback.
 *
 * The generic XML parser will call destroy on element names, their prefix
 * and URI (if they exist) at the end of the end element callback.
 *
 * If the error stack feature is enabled, be aware that element names, prefixes
 * and URI's will have been reserved, which means that the destroy call is
 * likely to do no more than decrement a reference count (in such an
 * implementation).
 *
 * \param istr Location to store the interned string
 * \param strbuf Is a utf8 encoded string.
 * \param bytelen Length of the utf8 string excluding any NUL terminator.
 *
 * \retval FALSE on error.
 * \retval TRUE on success.
 */
typedef
HqBool (*xmlGIStrCreateFunct)(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@out@*/
      const xmlGIStr **istr,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const uint8 *strbuf,
      uint32 bytelen
) ;

/**
 * \brief Reserve another instance of this interned string.
 *
 * The generic XML parser will reserve element names, their prefix and URI
 * (if they exist) when the error stack is enabled.
 */
typedef
HqBool (*xmlGIStrReserveFunct)(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/
      const xmlGIStr *istr
) ;

/**
 * \brief Get the intern strings hash value.
 */
typedef
uintptr_t (*xmlGIStrHashFunct)(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr
) ;

/**
 * \brief Get the intern strings hash value.
 */
typedef
uint32 (*xmlGIStrLengthFunct)(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr
) ;

/**
 * \brief Get the intern strings hash value.
 */
typedef
/*@dependent@*/ const uint8 *(*xmlGIStrValueFunct)(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/
      const xmlGIStr *istr
) ;

/**
 * \brief Are the two strings equal.
 */
typedef
HqBool (*xmlGIStrEqualFunct)(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr1,
      /*@notnull@*/ /*@in@*/ /*@observer@*/
      const xmlGIStr *istr2
) ;

/**
 * \brief Remove the interned string if this is the last reference to it.
 *
 * \note The string we are destroying is marked as constant, so that it can
 * be used with references to a constant xmlGIStr. We don't damage the
 * data unless we are deallocating it, so this little white lie allows
 * other routines to use constant strings for safety.
 */
typedef
void (*xmlGIStrDestroyFunct)(
      /*@notnull@*/ /*@in@*/
      xmlGContext *xml_ctxt,
      /*@notnull@*/ /*@out@*/
      const xmlGIStr **istr
) ;

/**
 * \brief Terminate the intern string handler.
 *
 * This function is called at shutdown. The default handler destroys its
 * string pool.
 */
typedef void (*xmlGIStrHandlerTerminate)(
    /*@notnull@*/ /*@in@*/
    xmlGContext *xml_ctxt,
    /*@notnull@*/ /*@in@*/
    xmlGIStrHandler *handler
) ;

/**
 * \brief Convenience structure for replacing interned string functions.
 * For functions which are not implemented, set the field to NULL.
 */
struct xmlGIStrHandler {
  xmlGIStrCreateFunct           f_intern_create;       /* MUST be implemented */
  xmlGIStrReserveFunct          f_intern_reserve;      /* Optional */
  xmlGIStrDestroyFunct          f_intern_destroy;      /* Optional */
  xmlGIStrHashFunct             f_intern_hash;         /* MUST be implemented */
  xmlGIStrLengthFunct           f_intern_length;       /* MUST be implemented */
  xmlGIStrValueFunct            f_intern_value;        /* MUST be implemented */
  xmlGIStrEqualFunct            f_intern_equal;        /* MUST be implemented */
  xmlGIStrHandlerTerminate      f_intern_terminate;    /* Optional */
} ;

extern
HqBool xmlg_istring_create(
      xmlGContext *xml_ctxt,
      const xmlGIStr **istr,
      const uint8 *strbuf,
      uint32 buflen) ;

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLGINTERN_H__*/
