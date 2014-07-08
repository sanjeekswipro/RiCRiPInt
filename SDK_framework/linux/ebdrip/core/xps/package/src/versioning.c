/** \file
 * \ingroup xpsver
 *
 * $HopeName: COREedoc!package:src:versioning.c(EBDSDK_P.1) $
 * $Id: package:src:versioning.c,v 1.63.4.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of xps versioning callbacks.
 *
 * See localFunctions declaration at the end of this file for the element
 * callbacks this file implements.
 */

/** \defgroup xpsver XPS Versioning
    \ingroup xps */

#include "core.h"
#include "objects.h"         /* OINTEGER, NAMECACHE */
#include "namedef_.h"
#include "swerrors.h"
#include "stacks.h"          /* operandstack */
#include "dictscan.h"        /* NAMETYPEMATCH */
#include "dicthash.h"
#include "hqmemset.h"

#include "xml.h"
#include "xmltypeconv.h"

#include "xpspriv.h"
#include "xpsscan.h"

#undef PROFILE_VERSIONING_FILTER

#ifdef PROFILE_VERSIONING_FILTER
#include <stdio.h>
#define XML_TRACE(_out_) printf(_out_)
#else
#define XML_TRACE(_out_)
#endif

/** \todo THIS MUST BE MOVED INTO libgenxml */
extern
Bool xmlg_register_cb_array(
    xmlGFilter *filter,
    xmlGIStr *xml_namespace,
    xpsElementFuncts *funct_array) ;

typedef struct xpsAlternateContent {
  /** We have seen at least one Choice element. */
  Bool seen_choice ;

  /** We have seen a Fallback element. */
  Bool seen_fallback ;

  /** We have already selected and processed a Choice or Fallback
      block. */
  Bool have_processed_alternate ;

  /** We are within a Choice or Fallback. We use this to check for
      invalid children within an AlternateContent. */
  Bool within_selection ;

  /** Element depth of this alternate content block. */
  uint32 depth ;

  /** Are we ignoring this choice. */
  Bool ignore_choice ;

  /** The Choice/Fallback element depth. */
  uint32 choice_depth ;

  struct xpsAlternateContent *next;
} xpsAlternateContent ;

typedef struct xpsVersioningContext {
  /** AlternateContent block stack. */
  xpsAlternateContent *alternate_stack ;

  /** Keeps track of compatibility rules for versioning. */
  xpsCompatBlock *compatibility_stack ;

  xpsSupportedUri **supported_uris ;

  /** Used purely to check that validity error handlers on a filter and
      filter chain only *ever* get called once. */
  Bool error_occurred ;

} xpsVersioningContext ;

/* Common definitions for XPS versioning children and attributes */

/* Never inspected, see macro below. */
Bool optional_versioning_attribute ;

/*=============================================================================
 * Compatibility block functions
 *=============================================================================
 */

static
struct CompatUriEntry *find_uri(
      struct CompatUriEntry **table,
      const xmlGIStr *uri,
      uint32 *hval)
{
  uintptr_t hash ;
#ifdef PROFILE_VERSIONING_FILTER
  HqBool collision = FALSE ;
#endif

  struct CompatUriEntry  *curr ;
  HQASSERT(uri != NULL, "uri is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  if (table == NULL)
    return NULL ;

  /* Lets just use the hash value of the interned uri. */
  hash = (intern_hash(uri) >> 2 ) % COMPATBLOCK_HASH_SIZE ;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr = table[*hval]; curr != NULL; curr = curr->next) {
#ifdef PROFILE_VERSIONING_FILTER
    if (collision) {
      XML_TRACE("  compatibility_collision\n") ;
    }
    collision = TRUE ;
#endif

    if (curr->uri == uri)
      return curr;
  }

  return NULL;
}

static
struct CompatProcessContentEntry *find_pc(
      struct CompatProcessContentEntry **table,
      const xmlGIStr *uri,
      const xmlGIStr *localname,
      uint32 *hval)
{
  uintptr_t hash ;
  struct CompatProcessContentEntry *curr ;
  HQASSERT(uri != NULL, "uri is NULL") ;
  HQASSERT(localname != NULL, "localname is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  if (table == NULL)
    return NULL ;

  /* Lets just use the hash value of the interned localname. */
  hash = intern_hash(localname) % COMPATBLOCK_HASH_SIZE ;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr = table[*hval]; curr != NULL; curr = curr->next)
    if (curr->uri == uri && curr->localname == localname)
      return curr;

  return NULL;
}

Bool xps_is_ignorable(
      xmlGFilter *filter,
      const xmlGIStr *uri,
      uint32 depth)
{
  xpsVersioningContext *ver_ctxt;
  xpsCompatBlock *top_compatblock ;
  struct xpsCompatBlock *curr ;
  uint32 hval ;

  UNUSED_PARAM( uint32 , depth ) ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "ver_ctxt is NULL") ;

  top_compatblock = ver_ctxt->compatibility_stack ;

  for (curr = top_compatblock; curr!= NULL; curr = curr->next) {
    HQASSERT(curr->depth <= depth,
             "compat block depth is not greater or equal to current parse depth") ;

    if (find_uri(curr->mustunderstand, uri, &hval) != NULL) {
      return FALSE ;
    } else {
      if (find_uri(curr->ignorable, uri, &hval) != NULL)
        return TRUE ;
    }
  }
  return FALSE ;
}

Bool xps_mustunderstand(
      xmlGFilter *filter,
      const xmlGIStr *uri,
      uint32 depth)
{
  xpsVersioningContext *ver_ctxt;
  xpsCompatBlock *top_compatblock ;
  struct xpsCompatBlock *curr ;
  uint32 hval ;

  UNUSED_PARAM( uint32 , depth ) ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "ver_ctxt is NULL") ;

  top_compatblock = ver_ctxt->compatibility_stack ;

  for (curr = top_compatblock; curr!= NULL; curr = curr->next) {
    HQASSERT(curr->depth <= depth,
             "compat block depth is not greater or equal to current parse depth") ;
    if (find_uri(curr->mustunderstand, uri, &hval) != NULL) {
      return TRUE ;
    } else {
      if (find_uri(curr->ignorable, uri, &hval) != NULL)
        return FALSE ;
    }
  }
  return TRUE ;
}

Bool xps_process_content(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      uint32 depth)
{
  xpsVersioningContext *ver_ctxt;
  xpsCompatBlock *top_compatblock ;
  struct xpsCompatBlock *curr ;
  uint32 hval ;

  UNUSED_PARAM( uint32 , depth ) ;

  HQASSERT(localname != NULL, "localname is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "ver_ctxt is NULL") ;

  top_compatblock = ver_ctxt->compatibility_stack ;

  for (curr = top_compatblock; curr!= NULL; curr = curr->next) {
    HQASSERT(curr->depth <= depth,
             "compat block depth is not greater or equal to current parse depth") ;
    if (find_pc(curr->processcontent, uri, localname, &hval) != NULL) {
      return TRUE ;
    } else {
      if (find_pc(curr->processcontent, uri, XML_INTERN(ProcessContent_AllElements), &hval) != NULL)
        return TRUE ;
    }
  }
  return FALSE ;
}

uint32 xps_compatblock_depth(
      xpsVersioningContext *ver_ctxt)
{
  xpsCompatBlock *top_compatblock ;

  HQASSERT(ver_ctxt != NULL, "ver_ctxt is NULL") ;

  top_compatblock = ver_ctxt->compatibility_stack ;
  HQASSERT(top_compatblock != NULL, "top_compatblock is NULL") ;

  return top_compatblock->depth ;
}

void xps_compatblock_destroy(
      xpsVersioningContext *ver_ctxt,
      uint32 depth)
{
  xpsCompatBlock *top_compatblock ;
  struct CompatUriEntry *curr_uri, *next_uri ;
  struct CompatProcessContentEntry *curr_pc, *next_pc ;
  uint32 i ;

  HQASSERT(ver_ctxt != NULL, "ver_ctxt is NULL") ;

  top_compatblock = ver_ctxt->compatibility_stack ;

  if (top_compatblock->ignore_block && top_compatblock->depth != depth) {
    HQASSERT(top_compatblock->depth < depth,
             "compat block is not depth is not less than element depth") ;
    return ;
  }

  if (top_compatblock == NULL || top_compatblock->depth != depth) {
    HQFAIL("attempting to destroy incorrect compat block") ;
    return ;
  }

  ver_ctxt->compatibility_stack = top_compatblock->next ;

  /* Free hash tables */
  if (top_compatblock->mustunderstand != NULL) {
    for (i=0; i<COMPATBLOCK_HASH_SIZE; i++) {
      for (curr_uri = top_compatblock->mustunderstand[i];
           curr_uri != NULL; curr_uri = next_uri) {
        next_uri = curr_uri->next;
        mm_sac_free(mm_xml_pool, curr_uri, SAC_ALLOC_COMPAT_URI_ENTRY_SIZE) ;
      }
    }
    mm_free(mm_xml_pool, top_compatblock->mustunderstand,
            sizeof(struct CompatUriEntry*) * COMPATBLOCK_HASH_SIZE) ;
  }

  if (top_compatblock->ignorable != NULL) {
    for (i=0; i<COMPATBLOCK_HASH_SIZE; i++) {
      for (curr_uri = top_compatblock->ignorable[i];
           curr_uri != NULL; curr_uri = next_uri) {
        next_uri = curr_uri->next;
        mm_sac_free(mm_xml_pool, curr_uri, SAC_ALLOC_COMPAT_URI_ENTRY_SIZE) ;
      }
    }
    mm_free(mm_xml_pool, top_compatblock->ignorable,
            sizeof(struct CompatUriEntry*) * COMPATBLOCK_HASH_SIZE) ;
  }

  if (top_compatblock->processcontent != NULL) {
    for (i=0; i<COMPATBLOCK_HASH_SIZE; i++) {
      for (curr_pc = top_compatblock->processcontent[i];
           curr_pc != NULL; curr_pc = next_pc) {
        next_pc = curr_pc->next;
        mm_sac_free(mm_xml_pool, curr_pc, SAC_ALLOC_COMPAT_PROCESS_CONTENT_ENTRY_SIZE) ;
      }
    }

    mm_free(mm_xml_pool, top_compatblock->processcontent,
            sizeof(struct CompatProcessContentEntry*) * COMPATBLOCK_HASH_SIZE) ;
  }

  mm_sac_free(mm_xml_pool, top_compatblock, SAC_ALLOC_COMPATBLOCK_SIZE) ;
}


static Bool xps_compat_scan_attrs(
      xmlGFilter *filter,
      xmlGAttributes *attrs,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      const xmlGIStr *attrlocalname,
      const xmlGIStr *attrprefix,
      const xmlGIStr *attruri,
      const uint8 *attrvalue,
      uint32 attrvaluelen)
{
  xpsVersioningContext *ver_ctxt;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( const xmlGIStr* , attrprefix ) ;
  UNUSED_PARAM( const uint8* , attrvalue ) ;
  UNUSED_PARAM( uint32, attrvaluelen ) ;

  HQASSERT(filter != NULL, "filter is NULL");
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "ver_ctxt is NULL");
  depth = xmlg_get_element_depth(filter) ;

  /* do I understand this attributes uri?
     if not, is this uri ignorable?
       if it is, then remove attribute
     else
       raise error */

  if (attruri != NULL) { /* we understand the no namespace attributes */
    /* do I inderstand this attributes uri? */
    if (! xps_is_supported_namespace(ver_ctxt->supported_uris, attruri)) {
      /* if not, is this uri ignorable? */
      if (xps_is_ignorable(filter, attruri, depth)) {
        /* if it is, then remove the attribute */
        xmlg_attributes_remove(attrs, attrlocalname, attruri) ;
      } else {
        /* raise error */

        /* what we actually do is allow the attribute through the
           versioning filter so that XPS validation can pick it up */
        return TRUE ;
      }
    }
  }
  return TRUE ;
}

static
Bool attribute_not_empty(
  utf8_buffer*  attribute)
{
  HQASSERT((attribute != NULL),
           "attribute_not_empty: NULL attribute value pointer");

  (void)xml_match_space(attribute);
  return(attribute->unitlength > 0);
}

static
Bool uri_from_prefix(
  xmlGFilter*       filter,
  xmlGIStr*         attr_name,
  utf8_buffer*      attr_value,
  const xmlGIStr**  uri)
{
  utf8_buffer   prefix;
  xmlGIStr*     iprefix;

  HQASSERT((filter != NULL),
           "uri_from_prefix: NULL filter pointer");
  HQASSERT((attr_name != NULL),
           "uri_from_prefix: NULL attribute name pointer");
  HQASSERT((attr_value != NULL),
           "uri_from_prefix: NULL attribute value pointer");
  HQASSERT((uri != NULL),
           "uri_from_prefix: NULL pointer to returned uri");

  /* Get a XML prefix ... */
  if ( !xps_convert_prefix_no_colon(filter, attr_name, attr_value, &prefix) ||
       !intern_create(&iprefix, prefix.codeunits, prefix.unitlength) ) {
    return(detailf_error_handler(SYNTAXERROR, "%.*s attribute is invalid.",
                                 intern_length(attr_name), intern_value(attr_name)));
  }

  /* ... and it the namespace it maps to. */
  if ( !xmlg_get_namespace_uri(filter, iprefix, uri) ) {
    return(detailf_error_handler(SYNTAXERROR, "Unable to resolve prefix within %.*s.",
                                 intern_length(attr_name), intern_value(attr_name)));
  }

  return(TRUE);
}

static
Bool add_uri_if_new(
  const xmlGIStr*         istr_uri,
  struct CompatUriEntry** uri_table,
  uint32*                 table_count)
{
  uint32                  hval = 0;
  struct CompatUriEntry*  curr_uri;

  HQASSERT((uri_table != NULL),
           "add_uri_if_new: NULL URI table pointer");
  HQASSERT((table_count != NULL),
           "add_uri_if_new: NULL URI table count pointer");
  HQASSERT((istr_uri != NULL),
           "add_uri_if_new: NULL URI pointer");

  /* Add the URI to the table if it does not exist already */
  curr_uri = find_uri(uri_table, istr_uri, &hval);
  if ( curr_uri == NULL ) {
    curr_uri = mm_sac_alloc(mm_xml_pool, SAC_ALLOC_COMPAT_URI_ENTRY_SIZE, MM_ALLOC_CLASS_XPS_COMPAT);
    if ( curr_uri == NULL ) {
      return(error_handler(VMERROR));
    }

    curr_uri->uri = istr_uri;
    curr_uri->next = uri_table[hval];
    uri_table[hval] = curr_uri;
    *table_count += 1;
  }

  return(TRUE);
}

static
Bool are_prefix_ns_ignorable(
  xmlGFilter*             filter,
  xmlGIStr*               attr_name,
  utf8_buffer*            attr_value,
  struct CompatUriEntry** ignorable)
{
  utf8_buffer prefix;
  utf8_buffer localpart;
  const xmlGIStr* iprefix;
  const xmlGIStr* iuri;
  uint32      hval;

  HQASSERT((filter != NULL),
           "are_prefix_ns_ignorable: NULL filter pointer");
  HQASSERT((attr_name != NULL),
           "are_prefix_ns_ignorable: NULL attribute name pointer");
  HQASSERT((attr_value != NULL),
           "are_prefix_ns_ignorable: NULL attribute value  pointer");
  HQASSERT((ignorable != NULL),
           "are_prefix_ns_ignorable: NULL ignorable URIs pointer");

  (void)xml_match_space(attr_value);

  /* Check all prefixes of qnames in attribute value map to URIs in the
   * ignorable URI list.  Skip over local part.
   */
  while ( attr_value->unitlength != 0 ) {
    if ( !xps_convert_prefix(filter, attr_name, attr_value, &prefix) ||
         !xps_convert_local_name(filter, attr_name, attr_value, &localpart) ||
         !intern_create(&iprefix, prefix.codeunits, prefix.unitlength)) {
      return(detailf_error_handler(SYNTAXERROR, "%.*s attribute is invalid.",
                                   intern_length(attr_name), intern_value(attr_name)));
    }

    if ( !xmlg_get_namespace_uri(filter, iprefix, &iuri) ) {
      return(detailf_error_handler(SYNTAXERROR, "Unable to resolve prefix within %.*s.",
                                   intern_length(attr_name), intern_value(attr_name)));
    }

    if ( find_uri(ignorable, iuri, &hval) == NULL ) {
      return(detailf_error_handler(SYNTAXERROR, "%.*s URI is not within Ignorable list.",
                                   intern_length(attr_name), intern_value(attr_name)));
    }
    (void)xml_match_space(attr_value);
  }

  return(TRUE);
}

static
Bool alloc_uri_table(
  struct CompatUriEntry***  table)
{
  int32 i;
  struct CompatUriEntry** new_table;

  HQASSERT((table != NULL),
           "alloc_uri_table: NULL returned table pointer");

  new_table = mm_alloc(mm_xml_pool, sizeof(struct CompatUriEntry*)*COMPATBLOCK_HASH_SIZE,
                       MM_ALLOC_CLASS_XPS_COMPAT);
  if ( new_table == NULL ) {
    return(error_handler(VMERROR));
  }

  for ( i = 0; i < COMPATBLOCK_HASH_SIZE; i++ ) {
    new_table[i] = NULL;
  }

  *table = new_table;

  return(TRUE);
}


/* Unfortunately we need to handle the case when an AlternateContent
   contains a known element with a namespace (or indeed an element
   with no namespace at all) as a child. This is treated as an
   error. Because there is no subsequent filter to do the validation
   of this, because the versioning filter does its own validation, we
   need to trap it as a special case. Choice and Fallback elements
   allow any child elements through, so this is traped at execution
   time. */

/* The only time called_from_versioning_error_cb is TRUE is when
   validation checks that the AlternateContent children are valid. */
static Bool xps_compatblock_create(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      uint32 depth,
      Bool called_from_versioning_error_cb)
{
  xpsVersioningContext *ver_ctxt;
  xpsCompatBlock *top_compatblock, *compatblock;
  struct CompatProcessContentEntry *curr_pc ;
  static utf8_buffer mustunderstandcheck, ignorablecheck, processcontentcheck,
                     preserveelementscheck, preserveattributescheck ;
  static Bool mustunderstand_set, ignorable_set, processcontent_set,
              preserveelements_set, preserveattributes_set ;
  utf8_buffer prefix, element ;
  xmlGIStr *istr_prefix, *istr_element ;
  const xmlGIStr *istr_uri ;
  uint32 i, hval = 0 ;

  /* Note that these are *carefully* constructed to NOT cause an error
     otherwise the validity error handler could go recursive. */
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(MustUnderstand), XML_INTERN(ns_markup_compatibility_2006), &mustunderstand_set, xps_convert_utf8, &mustunderstandcheck },
    { XML_INTERN(Ignorable), XML_INTERN(ns_markup_compatibility_2006), &ignorable_set, xps_convert_utf8, &ignorablecheck },
    { XML_INTERN(ProcessContent), XML_INTERN(ns_markup_compatibility_2006), &processcontent_set, xps_convert_utf8, &processcontentcheck },
    { XML_INTERN(PreserveElements), XML_INTERN(ns_markup_compatibility_2006), &preserveelements_set, xps_convert_utf8, &preserveelementscheck },
    { XML_INTERN(PreserveAttributes), XML_INTERN(ns_markup_compatibility_2006), &preserveattributes_set, xps_convert_utf8, &preserveattributescheck },
    XML_ATTRIBUTE_MATCH_END
  } ;

  HQASSERT(filter != NULL, "filter is NULL");
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "ver_ctxt is NULL");

  top_compatblock = ver_ctxt->compatibility_stack ;

  if (top_compatblock != NULL) {
    if (top_compatblock->ignore_block)
      return TRUE ;

    /* If the depth from the top compatibility block is the same, the
       compatibility block has already been created. We need this check because
       this function is likely to be called more than once during the start
       element callback dispatch process. Primarily from the validity error
       callback and the global pre-table start callback. */
    if (top_compatblock->depth == depth) {
      return TRUE ;
    }
  }

  /* See if we have any compatibility information in the attributes, if so,
     create a new compatibility block. If not, nothing to do.

     Because this function gets called from the vaility error handler when
     can be called from xmlGAttributesMatch, be careful that this does not
     go recursive - hence the FALSE to say we do not want unmatched attribute
     checkin to be done.
   */
  if ( ! xmlg_attributes_match(filter, localname, uri, attrs, match, FALSE))
    return error_handler(UNDEFINED) ;

  /* If any of these attributes are empty or only have white space in
     their value, mark them as not present. */
  mustunderstand_set = mustunderstand_set && attribute_not_empty(&mustunderstandcheck);
  ignorable_set = ignorable_set && attribute_not_empty(&ignorablecheck);
  processcontent_set = processcontent_set && attribute_not_empty(&processcontentcheck);
  preserveelements_set = preserveelements_set && attribute_not_empty(&preserveelementscheck);
  preserveattributes_set = preserveattributes_set && attribute_not_empty(&preserveattributescheck);

#if 0
  /* Some simple validation.
   * ECMA final draft no longer makes the following statements. I want to leave
   * this code around in case it changes in the future. */
  if (processcontent_set && ! ignorable_set)
    return detail_error_handler(SYNTAXERROR,
           "ProcessContent attribute used with no Ignorable attribute.") ;

  if (preserveelements_set && ! ignorable_set)
    return detail_error_handler(SYNTAXERROR,
           "PreserveElements attribute used with no Ignorable attribute.") ;

  if (preserveattributes_set && ! ignorable_set)
    return detail_error_handler(SYNTAXERROR,
           "PreserveAttributes attribute used with no Ignorable attribute.") ;
#endif

  /* Create the compatibility block */
  compatblock = mm_sac_alloc(mm_xml_pool, SAC_ALLOC_COMPATBLOCK_SIZE,
                             MM_ALLOC_CLASS_XPS_COMPAT) ;
  if (compatblock == NULL)
    return error_handler(VMERROR);

  /* From here on, you MUST goto error so that the code cleans up. */

  /* Initialize compatibility block */
  compatblock->num_ignorable = 0 ;
  compatblock->num_mustunderstand = 0 ;
  compatblock->num_processcontent = 0 ;
  compatblock->ignorable = NULL ;
  compatblock->mustunderstand = NULL ;
  compatblock->processcontent = NULL ;
  compatblock->depth = depth ;
  compatblock->ignore_this_element = FALSE ;
  compatblock->ignore_block = FALSE ;

  /* Add to stack */
  compatblock->next = ver_ctxt->compatibility_stack ;
  ver_ctxt->compatibility_stack = compatblock ;

  /* Process the values and do further validation */
  if (mustunderstand_set) {
    /* Create empty mustunderstand hash */
    if ( !alloc_uri_table(&compatblock->mustunderstand) ) {
      goto error;
    }

    (void)xml_match_space(&mustunderstandcheck) ;
    /* Add all mustunderstand URI's */
    while ( mustunderstandcheck.unitlength != 0) {
      /* Get uri for prefix ... */
      if ( !uri_from_prefix(filter, XML_INTERN(MustUnderstand), &mustunderstandcheck, &istr_uri) ) {
        goto error ;
      }

      /* ... check uri is supported ... */
      if (! xps_is_supported_namespace(ver_ctxt->supported_uris, istr_uri)) {
        (void)detail_error_handler(RANGECHECK,
                                   "MustUnderstand references an unsupported namespace.") ;
        goto error ;
      }

      /* ... and if it is add it to MustUnderstand table if not already present */
      if ( !add_uri_if_new(istr_uri, compatblock->mustunderstand, &compatblock->num_mustunderstand) ) {
        goto error;
      }

      (void)xml_match_space(&mustunderstandcheck) ;
    }
  }

  /* Create empty ignorable hash */
  if ( !alloc_uri_table(&compatblock->ignorable) ) {
    goto error;
  }

  if (ignorable_set) {
    /* Add all ignorable URI's, check that each URI is not within the
       mustunderstand list. */

    (void)xml_match_space(&ignorablecheck) ;

    while ( ignorablecheck.unitlength != 0 ) {
      /* Get uri for prefix ... */
      if ( !uri_from_prefix(filter, XML_INTERN(Ignorable), &ignorablecheck, &istr_uri) ) {
        goto error;
      }

      /* ... check uri is not also a MustUnderstand namespace ... */
      if (compatblock->mustunderstand != NULL) {
        if (find_uri(compatblock->mustunderstand, istr_uri, &hval) != NULL) {
          (void)detail_error_handler(SYNTAXERROR,
                                     "Ignorable URI conflict with MustUnderstand URI.") ;
          goto error ;
        }
      }

      /* ... and if it is add it to Ignorable table if not already present */
      if ( !add_uri_if_new(istr_uri, compatblock->ignorable, &compatblock->num_ignorable) ) {
        goto error;
      }

      (void)xml_match_space(&ignorablecheck) ;
    }
  }

  if (processcontent_set) {
    /* Create empty processcontent hash */
    if ((compatblock->processcontent =
         mm_alloc(mm_xml_pool,
                  sizeof(struct CompatProcessContentEntry*) * COMPATBLOCK_HASH_SIZE,
                  MM_ALLOC_CLASS_XPS_COMPAT)) == NULL) {
      (void) error_handler(VMERROR) ;
      goto error ;
    }

    for (i=0; i < COMPATBLOCK_HASH_SIZE; i++) {
      compatblock->processcontent[i] = NULL;
    }

    (void)xml_match_space(&processcontentcheck) ;

    /* Add all process content element names/URI's, check that each
       URI is within the ignorable URI list. */
    while ( processcontentcheck.unitlength != 0 ) {
      if ( !xps_convert_prefix(filter, XML_INTERN(ProcessContent), &processcontentcheck, &prefix) ||
           !xps_convert_local_name(filter, XML_INTERN(ProcessContent), &processcontentcheck, &element) ||
           !intern_create(&istr_prefix, prefix.codeunits, prefix.unitlength)) {
        (void)detail_error_handler(SYNTAXERROR,
                                   "ProcessContent attribute is invalid.") ;
        goto error ;
      }

      if (! xmlg_get_namespace_uri(filter, istr_prefix, &istr_uri)) {
        (void)detail_error_handler(SYNTAXERROR,
                                   "Unable to resolve prefix within ProcessContent.") ;
        goto error ;
      }

      if (! intern_create(&istr_element, element.codeunits, element.unitlength))
        goto error ;

      if (find_uri(compatblock->ignorable, istr_uri, &hval) == NULL) {
        (void)detail_error_handler(SYNTAXERROR, "ProcessContent URI is not within Ignorable list.") ;
        goto error ;
      }

      /* Do we need to create a new entry? */
      if ((curr_pc = find_pc(compatblock->processcontent, istr_uri, istr_element, &hval)) == NULL) {
        if ((curr_pc = mm_sac_alloc(mm_xml_pool, SAC_ALLOC_COMPAT_PROCESS_CONTENT_ENTRY_SIZE, MM_ALLOC_CLASS_XPS_COMPAT)) == NULL) {
          (void) error_handler(VMERROR) ;
          goto error ;
        }

        compatblock->num_processcontent++ ;

        curr_pc->uri = istr_uri ;
        curr_pc->localname = istr_element ;

        curr_pc->next = compatblock->processcontent[hval] ;
        compatblock->processcontent[hval] = curr_pc ;
      } /* else its already in the hash - we don't care */
      (void)xml_match_space(&processcontentcheck) ;
    }
  }

  /* Even though we make no use of these attributes, at least check
     their syntax and values as best as possible. */
  if (preserveelements_set) {
    if ( !are_prefix_ns_ignorable(filter, XML_INTERN(PreserveElements),
                                  &preserveelementscheck, compatblock->ignorable) ) {
      goto error;
    }
  }

  if (preserveattributes_set) {
    if ( !are_prefix_ns_ignorable(filter, XML_INTERN(PreserveAttributes),
                                  &preserveattributescheck, compatblock->ignorable) ) {
      goto error;
    }
  }

  if (attrs != NULL) {
    xmlg_attributes_remove(attrs, XML_INTERN(MustUnderstand),
                           XML_INTERN(ns_markup_compatibility_2006)) ;
    xmlg_attributes_remove(attrs, XML_INTERN(Ignorable),
                           XML_INTERN(ns_markup_compatibility_2006)) ;
    xmlg_attributes_remove(attrs, XML_INTERN(ProcessContent),
                           XML_INTERN(ns_markup_compatibility_2006)) ;
    xmlg_attributes_remove(attrs, XML_INTERN(PreserveAttributes),
                           XML_INTERN(ns_markup_compatibility_2006)) ;
    xmlg_attributes_remove(attrs, XML_INTERN(PreserveElements),
                           XML_INTERN(ns_markup_compatibility_2006)) ;

    /* for all attributes on this element
       do I understand this attributes uri?
       if not, is this uri ignorable?
       if it is, then remove attribute
       else
       raise error */
    if (! xmlg_attributes_scan_full(filter, attrs, localname, uri, xps_compat_scan_attrs)) {
      (void) error_handler(UNDEFINED) ;
      goto error ;
    }
  }


  /* do I understand this elements uri?
     if not, is this uri ignorable?
     do we have a process content on this element?
     if we do, then only skip this element
     else
     skip elemement and all its content
     else
     raise error */

  if (uri != NULL) { /* let validation pick up no namespace elements */
    /* do I understand this elements uri? */
    if (! xps_is_supported_namespace(ver_ctxt->supported_uris, uri)) {
      /* if not, is this uri ignorable? */
      if (xps_is_ignorable(filter, uri, depth)) {
        /* do we have a process content on this element? */
        if (xps_process_content(filter, localname, uri, depth)) {
          /* if we do, then only skip this element */
          compatblock->ignore_this_element = TRUE ;
        } else {
          /* skip elemement and all its content */
          compatblock->ignore_block = TRUE ;
        }
      } else {
        /* raise error */

        /* what we actually do is allow the element through the
           versioning filter so that XPS validation can pick it up,
           unless its the versioning filter where we need to catch the
           case here. */
      }
    }

    /* catch elements directly within an AlternateContent. */
    if (called_from_versioning_error_cb) {
      if (! compatblock->ignore_block && ! compatblock->ignore_this_element) {
        (void)detailf_error_handler(SYNTAXERROR,
                                    "<AlternateContent> contains invalid subelement <%.*s>.",
                                    intern_length(localname), intern_value(localname)) ;
        goto error ;
      }
    }

  } else { /* no URI */
    /* catch elements with no namespace directly within an
       AlternateContent. */
    if (called_from_versioning_error_cb) {
      (void)detailf_error_handler(SYNTAXERROR,
                                  "<AlternateContent> contains invalid subelement <%.*s>.",
                                  intern_length(localname), intern_value(localname)) ;
      goto error ;
    }
  }

  return TRUE ;

error:
  xps_compatblock_destroy(ver_ctxt, depth) ;
  return FALSE ;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */

static int32 xps_AlternateContent_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *new_alternate, *alternate ;
  uint32 depth ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Choice), XML_INTERN(ns_markup_compatibility_2006), XMLG_ONE_OR_MORE, XMLG_NO_GROUP},
    { XML_INTERN(Fallback), XML_INTERN(ns_markup_compatibility_2006), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  depth = xmlg_get_element_depth(filter) ;
  alternate = ver_ctxt->alternate_stack ;

  /* Note that the compatblock create removes compatibility
     attributes, hence needs to be called before an attribute
     match. */
  if (! xps_compatblock_create(filter, localname, uri, attrs, depth, FALSE))
    return FALSE ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) ) {
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return error_handler(UNDEFINED) ;
  }

  /* If we are ignoring this Choice, do nothing. */
  if (alternate != NULL && alternate->ignore_choice)
    return XMLG_RESULT_HANDLED ;

  if ( (new_alternate = mm_alloc(mm_xml_pool,
                                 sizeof(struct xpsAlternateContent),
                                 MM_ALLOC_CLASS_XPS_ALTERNATE)) == NULL) {
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return error_handler(VMERROR) ;
  }

  new_alternate->seen_choice = FALSE ;
  new_alternate->seen_fallback = FALSE ;
  new_alternate->have_processed_alternate = FALSE ;
  new_alternate->within_selection = FALSE ;
  new_alternate->depth = xmlg_get_element_depth(filter) ;
  new_alternate->next = ver_ctxt->alternate_stack ;
  new_alternate->ignore_choice = FALSE ;
  new_alternate->choice_depth = 0 ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
    mm_free(mm_xml_pool, new_alternate, sizeof(struct xpsAlternateContent)) ;
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return error_handler(UNDEFINED) ;
  }

  ver_ctxt->alternate_stack = new_alternate ;

  return XMLG_RESULT_HANDLED ;
}

static int32 xps_AlternateContent_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;
  HQASSERT(alternate != NULL, "alternate is NULL") ;
  depth = xmlg_get_element_depth(filter) ;

  xps_compatblock_destroy(ver_ctxt, depth) ;

  if (! success)
    return FALSE ;

  if (alternate->ignore_choice)
    return XMLG_RESULT_HANDLED ;

  HQASSERT(alternate->depth == depth,
           "alternate depth is not equal to element depth") ;

  ver_ctxt->alternate_stack = alternate->next ;
  mm_free(mm_xml_pool, alternate, sizeof(struct xpsAlternateContent)) ;

  return XMLG_RESULT_HANDLED ;
}

static int32 xps_Choice_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  static utf8_buffer requirescheck ;
  utf8_buffer prefix_seg ;
  xmlGIStr *istr_prefix ;
  const xmlGIStr *istr_uri ;
  Bool skip_choice = FALSE ; /* Assume this is the one */
  uint32 depth ;
  xpsCompatBlock *top_compatblock ;

  /* You may have anything in a Choice *except* another Choice or
     Fallback. */
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Choice), XML_INTERN(ns_markup_compatibility_2006), XMLG_ZERO, XMLG_NO_GROUP},
    { XML_INTERN(Fallback), XML_INTERN(ns_markup_compatibility_2006), XMLG_ZERO, XMLG_NO_GROUP},
    /* When setting XMLG_ANY, the localname is ignored but must not be
       NULL. */
    { XML_INTERN(Choice), NULL, XMLG_ANY, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Requires), NULL, NULL, xps_convert_utf8, &requirescheck },
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;

  if (alternate == NULL)
    return detail_error_handler(SYNTAXERROR,
                                "Choice not within an AlternateContent element.") ;

  depth = xmlg_get_element_depth(filter) ;

  /* Its a nested Choice because we are not immediately within an
     AlternateContent depth. */
  if ((alternate->depth + 1 ) != depth) {
    HQASSERT(depth > alternate->depth, "incorrect Choice depth") ;

    /* If we are ignoring this Choice, do nothing. */
    if (alternate->ignore_choice) {
      return XMLG_RESULT_HANDLED ;
    } else {
      return detail_error_handler(SYNTAXERROR,
                                  "Choice not within an AlternateContent element.") ;
    }
  }

  /* Note that the compatblock create removes compatibility
     attributes, hence needs to be called before an attribute
     match. */
  if (! xps_compatblock_create(filter, localname, uri, attrs, depth, FALSE))
    return FALSE ;

  top_compatblock = ver_ctxt->compatibility_stack ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE)) {
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return error_handler(UNDEFINED) ;
  }

  /* Look for a "not understood" namespaces to determine if we ought to skip
     this choice. */
  while ( requirescheck.unitlength != 0 ) {

    (void)xml_match_space(&requirescheck) ;

    if ( !xps_convert_prefix_no_colon(filter, XML_INTERN(Requires), &requirescheck, &prefix_seg) ||
         !intern_create(&istr_prefix, prefix_seg.codeunits, prefix_seg.unitlength) ) {
      xps_compatblock_destroy(ver_ctxt, depth) ;
      return FALSE ;
    }

    if (! xmlg_get_namespace_uri(filter, istr_prefix, &istr_uri)) {
      xps_compatblock_destroy(ver_ctxt, depth) ;
      return detail_error_handler(SYNTAXERROR,
                                  "Unable to resolve prefix within PreserveAttributes.") ;
    }

    if (istr_uri != NULL) {
      if (! xps_is_supported_namespace(ver_ctxt->supported_uris, istr_uri))
        skip_choice = TRUE ;
    }
  }

  if (alternate->seen_fallback) {
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return detail_error_handler(SYNTAXERROR,
                                "Choice found after Fallback within AlternateContent.") ;
  }

  /* we have seen at least a single choice element */
  alternate->seen_choice = TRUE ;
  alternate->choice_depth = xmlg_get_element_depth(filter) ;

  HQASSERT(!alternate->ignore_choice,
           "ignore_choice is not FALSE") ;

  /* If we do not understand the required namespaces or we have
     already processed a choice, ignore this one. */
  if (skip_choice || alternate->have_processed_alternate) {
    /* We inform the compatibility checking that the block is to be
       entirely ignored and hence we do not check compatibility
       attributes in a non-processed Choice or Fallback. */
    top_compatblock->ignore_block = TRUE ;
    alternate->ignore_choice = TRUE ;
  } else {
    /* we are about to process this choice */
    alternate->have_processed_alternate = TRUE ;

    if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
      xps_compatblock_destroy(ver_ctxt, depth) ;
      return error_handler(UNDEFINED) ;
    }
  }

  alternate->within_selection = TRUE ;

  return XMLG_RESULT_HANDLED ;
}

static int32 xps_Choice_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;
  HQASSERT(alternate != NULL, "alternate is NULL") ;
  depth = xmlg_get_element_depth(filter) ;

  /* Its a nested Choice because we are not immediately within an
     AlternateContent depth. */
  if ((alternate->depth + 1) != depth) {
    HQASSERT(depth > alternate->depth, "incorrect Choice depth") ;

    /* If we are ignoring this Choice, do nothing. */
    if (alternate->ignore_choice) {
      return XMLG_RESULT_HANDLED ;
    }
  }

  xps_compatblock_destroy(ver_ctxt, depth) ;

  if (! success)
    return FALSE ;

  HQASSERT(alternate->choice_depth == xmlg_get_element_depth(filter),
           "alternate choice depth is not equal to element depth") ;

  alternate->ignore_choice = FALSE ;

  return XMLG_RESULT_HANDLED ;
}

static int32 xps_Fallback_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  uint32 depth ;
  xpsCompatBlock *top_compatblock ;

  /* You may have anything in a Choice *except* another Choice or
     Fallback. */
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Choice), XML_INTERN(ns_markup_compatibility_2006), XMLG_ZERO, XMLG_NO_GROUP},
    { XML_INTERN(Fallback), XML_INTERN(ns_markup_compatibility_2006), XMLG_ZERO, XMLG_NO_GROUP},
    /* When setting XMLG_ANY, the localname is ignored but must not be
       NULL. */
    { XML_INTERN(Fallback), NULL, XMLG_ANY, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;

  if (alternate == NULL)
    return detail_error_handler(SYNTAXERROR,
                                "Fallback not within an AlternateContent element.") ;

  depth = xmlg_get_element_depth(filter) ;

  /* Its a nested Fallback because we are not immediately within an
     AlternateContent depth. */
  if ((alternate->depth + 1) != depth) {
    HQASSERT(depth > alternate->depth, "incorrect Choice depth") ;

    /* If we are ignoring this Choice, do nothing. */
    if (alternate->ignore_choice) {
      return XMLG_RESULT_HANDLED ;
    } else {
      return detail_error_handler(SYNTAXERROR,
                                  "Fallback not within an AlternateContent element.") ;
    }
  }

  /* Note that the compatblock create removes compatibility
     attributes, hence needs to be called before an attribute
     match. */
  if (! xps_compatblock_create(filter, localname, uri, attrs, depth, FALSE))
    return FALSE ;

  top_compatblock = ver_ctxt->compatibility_stack ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE)) {
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return error_handler(UNDEFINED) ;
  }

  HQASSERT(!alternate->seen_fallback,
           "we are seeing a second fallback - validity checking not working") ;

  /* mark that we have seen a fallback */
  alternate->seen_fallback = TRUE ;
  alternate->choice_depth = xmlg_get_element_depth(filter) ;

  HQASSERT(!alternate->ignore_choice,
           "ignore_choice is not FALSE") ;

  if (alternate->have_processed_alternate) {
    /* We inform the compatibility checking that the block is to be
       entirely ignored and hence we do not check compatibility
       attributes in a non-processed Choice or Fallback. */
    top_compatblock->ignore_block = TRUE ;
    alternate->ignore_choice = TRUE ;
  } else {
    /* we are about to process this choice */
    alternate->have_processed_alternate = TRUE ;

    if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
      xps_compatblock_destroy(ver_ctxt, depth) ;
      return error_handler(UNDEFINED) ;
    }
  }

  alternate->within_selection = TRUE ;

  return XMLG_RESULT_HANDLED ;
}

static int32 xps_Fallback_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;
  HQASSERT(alternate != NULL, "alternate is NULL") ;
  depth = xmlg_get_element_depth(filter) ;

  /* Its a nested Fallback because we are not immediately within an
     AlternateContent depth. */
  if ((alternate->depth + 1) != depth) {
    HQASSERT(depth > alternate->depth, "incorrect Choice depth") ;

    /* If we are ignoring this Choice, do nothing. */
    if (alternate->ignore_choice) {
      return XMLG_RESULT_HANDLED ;
    }
  }

  xps_compatblock_destroy(ver_ctxt, depth) ;

  if (! success)
    return FALSE ;

  HQASSERT(alternate->choice_depth == xmlg_get_element_depth(filter),
           "alternate choice depth is not equal to element depth") ;

  alternate->ignore_choice = FALSE ;

  return XMLG_RESULT_HANDLED ;
}

static int32 ver_characters_all(
      xmlGFilter *filter,
      const uint8 *buf,
      uint32 buflen)
{
  xpsCompatBlock *top_compatblock ;
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  uint32 depth ;

  UNUSED_PARAM( const uint8* , buf ) ;
  UNUSED_PARAM( uint32 , buflen ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;
  depth = xmlg_get_element_depth(filter) ;

  top_compatblock = ver_ctxt->compatibility_stack ;
  if (top_compatblock->ignore_this_element)
    return XMLG_RESULT_HANDLED ;

  if (top_compatblock->ignore_block && top_compatblock->depth <= depth)
    return XMLG_RESULT_HANDLED ;

  if (alternate != NULL && alternate->ignore_choice) {
    /* The assert is less than or equal because you will get character
       data inbetween the Choice/Fallback and the next immediate
       child. */
    HQASSERT(alternate->choice_depth <= xmlg_get_element_depth(filter),
             "attempting to ignore characters outside a choice context") ;
    return XMLG_RESULT_HANDLED ;
  }

  return XMLG_RESULT_FORWARD ;
}

static int32 ver_start_element_all(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  xpsCompatBlock *top_compatblock ;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( xmlGAttributes* , attrs) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;
  depth = xmlg_get_element_depth(filter) ;

  /* Note that the compatblock create removes compatibility
     attributes, hence needs to be called before an attribute
     match. */
  if (! xps_compatblock_create(filter, localname, uri, attrs, depth, FALSE))
    return FALSE ;

  top_compatblock = ver_ctxt->compatibility_stack ;
  if (top_compatblock->ignore_this_element)
    return XMLG_RESULT_HANDLED ;

  if (top_compatblock->ignore_block && top_compatblock->depth <= depth)
    return XMLG_RESULT_HANDLED ;

  if (alternate != NULL && alternate->ignore_choice) {
    HQASSERT(alternate->choice_depth < xmlg_get_element_depth(filter),
             "attempting to ignore start element outside a choice context") ;
    return XMLG_RESULT_HANDLED ;
  }

  /* We have a start element callback but its not within a Choice or
     Fallback, which is an error. We need to handle this case
     especially in the event that we have an Ignorable subelement of
     AlternateContent which also has a ProcessContent associated with
     it. Normal validation does not work because the depths do not
     match in the versioning filter. See request 45500 */
  if (alternate != NULL && ! alternate->within_selection) {
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return detail_error_handler(SYNTAXERROR,
                                "Only Choice and Fallback elements are valid children of an AlternateContent element.") ;
  }

  return XMLG_RESULT_FORWARD ;
}

static int32 ver_end_element_all(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  xpsCompatBlock *top_compatblock ;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;
  depth = xmlg_get_element_depth(filter) ;

  top_compatblock = ver_ctxt->compatibility_stack ;
  if (top_compatblock->ignore_this_element) {
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return success ? XMLG_RESULT_HANDLED : success ;
  }

  if (top_compatblock->ignore_block && top_compatblock->depth <= depth) {
    xps_compatblock_destroy(ver_ctxt, depth) ;
    return success ? XMLG_RESULT_HANDLED : success ;
  }

  xps_compatblock_destroy(ver_ctxt, depth) ;

  if (! success)
    return FALSE ;

  if (alternate != NULL && alternate->ignore_choice) {
    HQASSERT(alternate->choice_depth < xmlg_get_element_depth(filter),
             "attempting to ignore end element outside a choice context") ;
    return XMLG_RESULT_HANDLED ;
  }

  return XMLG_RESULT_FORWARD ;
}

static int32 ver_namespace_all(
      xmlGFilter *filter,
      const xmlGIStr *prefix,
      const xmlGIStr *uri)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  xpsCompatBlock *top_compatblock ;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;
  depth = xmlg_get_element_depth(filter) ;

  top_compatblock = ver_ctxt->compatibility_stack ;
  /* NOTE: the top of the compatibility stack is the previous
     element. If the document element has namespaces on it, obviously
     there will be nothing on the stack. */
  if (top_compatblock != NULL &&
      top_compatblock->ignore_block && top_compatblock->depth <= depth)
    return XMLG_RESULT_HANDLED ;

  if (alternate != NULL && alternate->ignore_choice) {
    HQASSERT(alternate->choice_depth < xmlg_get_element_depth(filter),
             "attempting to ignore namespace outside a choice context") ;
    return XMLG_RESULT_HANDLED ;
  }

  return XMLG_RESULT_FORWARD ;
}


/* If this returns FALSE, parsing will abort immediately. */
Bool xps_versioning_validity_error_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      int32 error_type,
      char *format,
      ...)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;
  Bool result = FALSE ;
  va_list vlist ;
  int32 errorno ;
  uint32 depth ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( xmlGAttributes* , attrs ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "NULL ver_ctxt") ;
  alternate = ver_ctxt->alternate_stack ;
  depth = xmlg_get_element_depth(filter) ;

  /* NOTE: this error callback only gets called from the versioning filter. */
  HQASSERT(!xps_error_occurred,
           "A validity error callback has been called after an error has been set.") ;
  if (xps_error_occurred)
    return FALSE ;

  errorno = SYNTAXERROR ;

  /* If its an invalid child error, check to see if the element is
     ignorable. This can only be done in the versioning filter. */
  if (error_type == XMLG_ERR_INVALID_CHILD ||
      error_type == XMLG_ERR_INVALID_DOC_ELEMENT) {
    /* The depth is "+1" because this gets called before the start of
       the next element. */
    depth++ ;

    result = xps_compatblock_create(filter, localname, uri, attrs, depth, TRUE) ;
    /* We let the filter pipeline continue. Versioning start callback
       will remove any ignorable child elements. S0 filter will pick
       up any invalid child elements. */
  } else {
    va_start( vlist, format ) ;
    result = vxps_validity_error_cb(filter, localname, prefix, uri, attrs, error_type, format, vlist) ;
    va_end( vlist );
  }

  return result ;
}

/* ============================================================================
 * Versioning filter init/dispose.
 * ============================================================================
 */

void xps_versioning_filter_dispose(
    xmlGFilter *filter)
{
  xpsVersioningContext *ver_ctxt ;
  xpsAlternateContent *alternate ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  ver_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(ver_ctxt != NULL, "ver_ctxt is NULL") ;

  while (ver_ctxt->compatibility_stack != NULL) {
    xps_compatblock_destroy(ver_ctxt, xps_compatblock_depth(ver_ctxt)) ;
  }
  HQASSERT(ver_ctxt->compatibility_stack == NULL,
           "xps compatibility stack is not empty");

  while (ver_ctxt->alternate_stack != NULL) {
    alternate = ver_ctxt->alternate_stack ;
    ver_ctxt->alternate_stack = alternate->next ;
    mm_free(mm_xml_pool, alternate, sizeof(struct xpsAlternateContent)) ;
  }

  mm_free(mm_xml_pool, ver_ctxt, sizeof(xpsVersioningContext)) ;
}

Bool xps_versioning_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      struct xpsSupportedUri **table)
{
  xmlGFilter *new_filter ;
  xpsVersioningContext *ver_ctxt ;

  static xpsElementFuncts local_functions[] =
  {
    { XML_INTERN(AlternateContent),
      xps_AlternateContent_Start,
      xps_AlternateContent_End,
      NULL /* No characters callback. */
    },
    { XML_INTERN(Choice),
      xps_Choice_Start,
      xps_Choice_End,
      NULL /* No characters callback. */
    },
    { XML_INTERN(Fallback),
      xps_Fallback_Start,
      xps_Fallback_End,
      NULL /* No characters callback. */
    },
    XPS_ELEMENTFUNCTS_END
  } ;

  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(table != NULL, "table is NULL") ;

  *filter = NULL ;

  if ( (ver_ctxt = mm_alloc(mm_xml_pool, sizeof(xpsVersioningContext),
                            MM_ALLOC_CLASS_XPS_VER_CONTEXT)) == NULL)
    return error_handler(VMERROR);

  HqMemZero((uint8 *)ver_ctxt, sizeof(xpsVersioningContext)) ;

  ver_ctxt->supported_uris = table ;

  if (! xmlg_fc_new_filter(filter_chain, &new_filter, position, ver_ctxt,
                           xps_versioning_filter_dispose)) {
    mm_free(mm_xml_pool, ver_ctxt, sizeof(xpsVersioningContext)) ;
    return error_handler(UNDEFINED) ;
  }

  xmlg_set_validity_error_cb(new_filter, xps_versioning_validity_error_cb) ;
  xmlg_set_user_error_cb(new_filter, xps_user_error_cb) ;

  /* We look at all XML data going through this filter. */

  /* Register specific versioning callbacks. */
  if (! xmlg_register_cb_array(new_filter,
                               XML_INTERN(ns_markup_compatibility_2006),
                               local_functions)) {
    mm_free(mm_xml_pool, ver_ctxt, sizeof(xpsVersioningContext)) ;
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  /* Register callbacks on all other XML content. */
  if (! xmlg_register_start_element_cb(new_filter, NULL, NULL, /* all elements */
                                       ver_start_element_all) ||
      ! xmlg_register_end_element_cb(new_filter, NULL, NULL, /* all elements */
                                     ver_end_element_all)) {
    mm_free(mm_xml_pool, ver_ctxt, sizeof(xpsVersioningContext)) ;
    xmlg_f_destroy(&new_filter) ;
    return error_handler(UNDEFINED) ;
  }

  xmlg_set_character_cb(new_filter, ver_characters_all) ; /* all character data */
  xmlg_set_namespace_cb(new_filter, ver_namespace_all) ;

  *filter = new_filter ;

  return TRUE ;
}

/* ============================================================================
* Log stripped */
