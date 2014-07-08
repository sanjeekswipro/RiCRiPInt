#ifndef __XMLGVALID_H__
#define __XMLGVALID_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!export:xmlgvalid.h(EBDSDK_P.1) $
 * $Id: export:xmlgvalid.h,v 1.14.10.1.1.1 2013/12/19 11:24:21 anon Exp $
 * 
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \ingroup libgenxml
 * \brief Public interface for basic XML validity.
 *
 * Allows one to easily check valid child elements.
 */

#include "xmlgtype.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Specify occurances allowed for child elements.
 */
enum {
  XMLG_ZERO_OR_ONE,                 /* Zero or one instance can
                                       exist. Constraint arg MUST be
                                       XMLG_NO_GROUP. */
  XMLG_ZERO_OR_MORE,                /* Zero to unlimited instances can
                                       exist. Constraint arg MUST be
                                       XMLG_NO_GROUP. */
  XMLG_ANY,                         /* Allow any subelement beyond the
                                       already listed
                                       constraints. Constraint arg
                                       MUST be XMLG_NO_GROUP. */
  XMLG_ZERO,                        /* The element MUST not exist as a
                                       child element. Constraint arg
                                       MUST be XMLG_NO_GROUP. */
  XMLG_ONE,                         /* One instance and only one
                                       instance MUST exist. Constraint
                                       arg MUST be XMLG_NO_GROUP. */
  XMLG_ONE_OR_MORE,                 /* One or more instances MUST
                                       exist. Constraint arg MUST be
                                       XMLG_NO_GROUP. */
  XMLG_MIN_OCCURS,                  /* Specify how many child elements
                                       must exist. Constraint arg MUST
                                       be less than zero. Absolute
                                       value of arg represents the
                                       number of occurences that MUST
                                       exist. Example: -2 specifies
                                       that at least 2 child elements
                                       MUST exist. */
  XMLG_GROUP_ONE_OF,                /* Exactly one of the groups
                                       elements MUST exist. Constraint
                                       arg MUST be > 0 & < 32. */
  XMLG_GROUP_ZERO_OR_ONE_OF,        /* Zero or only one of the groups
                                       elements may exist. Constraint
                                       arg MUST be > 0 & < 32. */
  XMLG_GROUP_ONE_OR_MORE_OF,        /* One or more of the groups
                                       elements MUST exist. Constraint
                                       arg MUST be > 0 & < 32. */
  XMLG_G_SEQUENCED, /* Zero or one of each member MAY exist, but they
                       MUST appear in the specified sequence. */
  XMLG_G_SEQUENCED_ONE /* Exactly one of each member MAY exist, and they
                          MUST appear in the specified sequence. */
} ;

enum {
  XMLG_ERR_UNKNOWN,
  XMLG_ERR_INVALID_DOC_ELEMENT,
  XMLG_ERR_INVALID_CHILD,
  XMLG_ERR_UNMATCHED_ATTRIBUTE,
  XMLG_ERR_ATTRIBUTE_SCANERROR
} ;

/* This element does not belong to any group of elements. */
#define XMLG_NO_GROUP 0

#define XMLG_VALID_CHILDREN_END { NULL, NULL, 0, 0 }

/**
 * Convenience structure to define valid children.
 */
typedef struct XMLG_VALID_CHILDREN {
  xmlGIStr *localname;
  xmlGIStr *uri;
  int32 constraint;     /* One of the above enum's */
  int32 constraint_arg; /* The above constraints take a numeric
                           argument. See above comments. */
} XMLG_VALID_CHILDREN ;

/*
Within the RIP, we are able to do the following because interned strings
are compiled into a static structure:

static XMLG_VALID_CHILDREN valid_children[] = {
  { XML_INTERN(X_W), XML_INTERN(ns_xyz), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
  { XML_INTERN(X_Y), XML_INTERN(ns_xyz), XMLG_ONE, XMLG_NO_GROUP},
  { XML_INTERN(X_Z), XML_INTERN(ns_xyz), XMLG_ONE_OR_MORE, XMLG_NO_GROUP},
  { XML_INTERN(A_B), XML_INTERN(ns_xyz), XMLG_GROUP_ONE_OF, 1},
  { XML_INTERN(B_C), XML_INTERN(ns_xyz), XMLG_GROUP_ONE_OF, 1},
  XMLG_VALID_CHILDREN_END
} ;
*/

/**
 * \brief Register a child validation block.
 */
extern
HqBool xmlg_valid_children(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@notnull@*/
      const XMLG_VALID_CHILDREN valid_children[]) ;

extern
xmlGValidChildTable* xmlg_valid_children_get_top(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter) ;

extern
uint32 xmlg_valid_children_depth(
      /*@in@*/ /*@notnull@*/
      xmlGValidChildTable *table) ;

extern
HqBool xmlg_valid_children_link(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      const xmlGIStr *localname,
      /*@in@*/ /*@null@*/
      const xmlGIStr *uri,
      /*@in@*/ /*@notnull@*/
      xmlGValidChildTable *table,
      uint32 new_depth) ;

#ifdef __cplusplus
}
#endif

/* ============================================================================
* Log stripped */
#endif /*!__XMLGVALID_H__*/
