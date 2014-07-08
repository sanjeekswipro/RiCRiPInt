/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!export:xmlintern.h(EBDSDK_P.1) $
 * $Id: export:xmlintern.h,v 1.11.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Public intern string interface for XML callbacks within the
 * core.
 */

#ifndef __XMLINTERN_H__
#define __XMLINTERN_H__ (1)

#include "objects.h" /* NAMECACHE */

/** \brief Interned strings are an alias for namecache entries.

    This implementation of interning uses the names cache directly. This
    definition allows simple typesafe downcasts (as &str.name). Upcasts are
    nasty; they need casts. */
struct xmlGIStr {
  NAMECACHE name ;
} ;

/** \brief Quick access macro for value of interned string. */
#define intern_value(s) ((const uint8 *)((s)->name.clist))

/** \brief Quick access macro for length of interned string. */
#define intern_length(s) ((s)->name.len)

/** \brief Quick access macro for hash value of interned string. */
#define intern_hash(s) ((uintptr_t)(s))

/** \brief Quick access macro to create an interned string. */
#define intern_create(s, n, l) ((*(s) = (xmlGIStr *)cachelongname((n), (l))) != NULL)

/** \brief Quick lookup macro to see if an interned string exists. */
#define intern_lookup(s, n, l) ((*(s) = (xmlGIStr *)lookuplongname((n), (l))) != NULL)

/** \brief Get an interned string from a name index.

    This is suitable for use in static initialisers. The intern index is a
    name, without the NAME_ prefix (so this macro can be changed if we do
    decide to build a new intern string table). */
#define XML_INTERN(i) ((xmlGIStr *)&system_names[NAME_ ## i])

/** \brief Get an integer for use in \c switch statements from an interned
    string */
#define XML_INTERN_SWITCH(s) ((s)->name.namenumber)

/** \brief Get a case label for a predefined interned name. */
#define XML_INTERN_CASE(i) (NAME_ ## i)

/* ============================================================================
* Log stripped */
#endif
