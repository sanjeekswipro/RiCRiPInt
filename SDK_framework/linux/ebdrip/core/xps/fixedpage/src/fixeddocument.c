/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!fixedpage:src:fixeddocument.c(EBDSDK_P.1) $
 * $Id: fixedpage:src:fixeddocument.c,v 1.7.9.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed document callbacks.
 */

#define OBJECT_MACROS_ONLY

#include "core.h"
#include "objects.h"
#include "namedef_.h"
#include "swerrors.h"
#include "constant.h"
#include "swcopyf.h"
#include "graphics.h"
#include "miscops.h"
#include "gu_ctm.h"
#include "devops.h"
#include "gschead.h"
#include "stacks.h"
#include "gstate.h"
#include "params.h"
#include "swmemory.h"
#include "gschcms.h"
#include "monitor.h"
#include "dicthash.h"

#include "xml.h"
#include "xmltypeconv.h"

#include "xpspriv.h"
#include "xpspt.h"
#include "xpsscan.h"
#include "xpsfonts.h"
#include "xpsresblock.h"
#include "xpsiccbased.h"
#include "fixedpagepriv.h"

#include "printticket.h"

/** \defgroup fixeddoc XPS FixedDocument
    \ingroup xps */
/** \{ */

static Bool init_fixed_document(
      xmlDocumentContext *xps_ctxt)
{
  struct xpsFixedDocument *new_fixed_document ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  if ((new_fixed_document = mm_alloc(mm_xml_pool, sizeof(struct xpsFixedDocument),
                                     MM_ALLOC_CLASS_XPS_FIXED_DOCUMENT)) == NULL)
    return error_handler(VMERROR);

  new_fixed_document->number_of_pages = 0 ;
  new_fixed_document->number_of_interpreted_pages = 0 ;
  new_fixed_document->more_pages = TRUE ;
  new_fixed_document->pages.next = NULL ;
  new_fixed_document->active_pages = &(new_fixed_document->pages) ;

  /* Init the next page from the print ticket device. */
  if (! pt_next_page(&xps_ctxt->printticket, &new_fixed_document->next_page)) {
    mm_free(mm_xml_pool, new_fixed_document, sizeof(struct xpsFixedDocument)) ;
    return error_handler(UNDEFINED) ;
  }

  xps_ctxt->fixed_document = new_fixed_document ;

  return TRUE ;
}

static void finish_fixed_document(
    xmlDocumentContext *xps_ctxt)
{
  struct xpsFixedDocument *old_fixed_document ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  old_fixed_document = xps_ctxt->fixed_document ;
  HQASSERT(old_fixed_document != NULL, "old_fixed_document is NULL") ;

  {
    struct xpsPages *page_block = old_fixed_document->pages.next ;
    int32 i, limit ;

    limit = old_fixed_document->number_of_pages > XPS_PAGEBLOCK_SIZE ? XPS_PAGEBLOCK_SIZE : old_fixed_document->number_of_pages ;
    for (i=0; i < limit; i++) {
      HQASSERT(old_fixed_document->pages.page[i] != NULL, "page count incorrect") ;
      xps_partname_free(&(old_fixed_document->pages.page[i])) ;
    }
    old_fixed_document->number_of_pages -= i ;

    while (page_block != NULL) {
      struct xpsPages *next_page_block = page_block->next ;
      int32 limit = old_fixed_document->number_of_pages > XPS_PAGEBLOCK_SIZE ? XPS_PAGEBLOCK_SIZE : old_fixed_document->number_of_pages ;

      for (i=0; i < limit; i++) {
        HQASSERT(page_block->page[i] != NULL, "page count incorrect") ;
        xps_partname_free(&(page_block->page[i])) ;
      }
      old_fixed_document->number_of_pages -= i ;

      mm_free(mm_xml_pool, page_block, sizeof(struct xpsPages)) ;
      page_block = next_page_block ;
    }
  }

  HQASSERT(old_fixed_document->number_of_pages == 0, "number of pages is not zero") ;
  mm_free(mm_xml_pool, old_fixed_document, sizeof(struct xpsFixedDocument)) ;
  xps_ctxt->fixed_document = NULL ;
}

static Bool add_new_page(
      xmlDocumentContext *xps_ctxt,
      xps_partname_t *source)
{
  struct xpsFixedDocument *fixed_document ;
  int32 insert_point ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  fixed_document = xps_ctxt->fixed_document ;
  HQASSERT(fixed_document != NULL, "fixed_document is NULL") ;
  HQASSERT(source != NULL, "source is NULL") ;

  insert_point = fixed_document->number_of_pages % XPS_PAGEBLOCK_SIZE ;
  fixed_document->number_of_pages++ ;

  if (fixed_document->number_of_pages > XPS_PAGEBLOCK_SIZE) {
    if (insert_point == 0) {
      /* Allocate new pages block. */
      if ((fixed_document->active_pages->next = mm_alloc(mm_xml_pool, sizeof(struct xpsPages),
                                                         MM_ALLOC_CLASS_XPS_FIXED_DOCUMENT)) == NULL) {
        fixed_document->number_of_pages-- ;
        return error_handler(VMERROR) ;
      }
      fixed_document->active_pages = fixed_document->active_pages->next ;
      fixed_document->active_pages->next = NULL ;
    }
  }
  fixed_document->active_pages->page[insert_point] = source ;

  return TRUE ;
}

static Bool print_page(
      xmlGFilter *filter,
      xps_partname_t *source)
{
  xmlDocumentContext *xps_ctxt ;
  struct xpsFixedDocument *fixed_document ;
  xmlGIStr *fixedpage_mimetype ;
  Bool success ;

  static XMLG_VALID_CHILDREN fixedpage_doc_element[] = {
    { XML_INTERN(FixedPage), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  static XPS_CONTENT_TYPES fixedpage_content_types[] = {
    { XML_INTERN(mimetype_xps_fixedpage) },
    XPS_CONTENT_TYPES_END
  } ;

  HQASSERT(filter != NULL, "NULL filter") ;
  HQASSERT(source != NULL, "source is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  fixed_document = xps_ctxt->fixed_document ;
  HQASSERT(fixed_document != NULL, "fixed_document is NULL") ;

  /* NOTE: Start of page scope config cannot be run until we have seen
     device and media values in <FixedPage>. */

  /* There is no relationship type specified from a fixed document
     part to a fixed page part. */
  success = xps_parse_xml_from_partname(filter, source,
                                        XPS_PROCESS_PRINTTICKET_REL,
                                        XPS_PART_VERSIONED | XPS_LOWMEMORY,
                                        fixedpage_doc_element,
                                        NULL, /* relationship */
                                        fixedpage_content_types,
                                        &fixedpage_mimetype) ;

  fixed_document->number_of_interpreted_pages++ ;

  return success ;
}

static Bool process_pages(
      xmlGFilter *filter,
      xps_partname_t *source,
      Bool end_of_document)
{
  xmlDocumentContext *xps_ctxt ;
  struct xpsFixedDocument *fixed_document ;
  int32 next_page ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  fixed_document = xps_ctxt->fixed_document ;
  HQASSERT(fixed_document != NULL, "fixed_document is NULL") ;

  next_page = fixed_document->next_page ;

  while(fixed_document->more_pages) {
    switch (next_page) {
      case XPSPT_COUNT_PAGES:
        /* If its the end of the document, it means we have counted
           all the pages so inform the PT device and ask the PT device
           which page to print next. */
        if (end_of_document) {
          if (! pt_config_end(&xps_ctxt->printticket, FALSE))
            return FALSE ;

          if (! pt_config_start(&xps_ctxt->printticket))
            return FALSE ;
        }

        if (! pt_next_page(&xps_ctxt->printticket, &fixed_document->next_page))
            return error_handler(UNDEFINED) ;

        if (end_of_document)
        {
          next_page = fixed_document->next_page;
          break ;
        }

        return TRUE ;

      case XPSPT_PAGES_ALL:
        if (source != NULL) {
          return print_page(filter, source) ;
        } else {
          return TRUE ;
        }

      case XPSPT_PAGES_NOMORE:
        fixed_document->more_pages = FALSE ;
        return TRUE ;

      default:
        if (next_page < 1)
          return detail_error_handler(UNDEFINED,
                                      "Print ticket device returned negative next page.") ;

        if (next_page > fixed_document->number_of_pages) {
          if (end_of_document) {
            return detail_error_handler(UNDEFINED,
                                        "Print ticket device requested a page greater than the number of pages available.") ;
          }
          /* Its a page we do not have yet, but we have not yet seen all
             pages, so do nothing. */
          return TRUE ;

        } else {
          /* We have the page requested, so print it. */

          { /* Print the page specified. */
            int32 i ;
            struct xpsPages *page_block = &(fixed_document->pages) ;
            int32 page_position = next_page % XPS_PAGEBLOCK_SIZE ;

            if (next_page > XPS_PAGEBLOCK_SIZE) {
              int32 num_page_blocks = (next_page - page_position) / XPS_PAGEBLOCK_SIZE ;
              for (i=0; i < num_page_blocks; i++) {
                page_block = page_block->next ;
              }
            }

            /* We need to index from zero. */
            if (page_position == 0) {
              page_position = XPS_PAGEBLOCK_SIZE - 1 ;
            } else {
              page_position-- ;
            }

            source = page_block->page[page_position] ;
          }

          if (! print_page(filter, source))
            return FALSE ;

          /* Get the next page (after this one) from the print ticket
             device. */
          if (! pt_next_page(&xps_ctxt->printticket, &fixed_document->next_page))
            return error_handler(UNDEFINED) ;

          if (fixed_document->next_page == XPSPT_PAGES_ALL)
            return detail_error_handler(UNDEFINED,
                                        "Print ticket device requested all pages in wrong order.") ;

          /* Setup the next page. */
          next_page = fixed_document->next_page ;
        }
    } /* end of switch */
  }

  return TRUE ;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */
static int32 xps_FixedDocument_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  Bool success;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(PageContent), XML_INTERN(ns_xps_2005_06), XMLG_ONE_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Run any start document config. */
  success = pt_config_start(&xps_ctxt->printticket);

  if (success)
    success = init_fixed_document(xps_ctxt) ;

  return success ;
}

/** XPS FixedDocument element end callback. */
static int32 xps_FixedDocument_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  HQASSERT(filter != NULL, "NULL filter") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  /* Its possible we have processed the FixedDocument end element but
     have more pages to print. */
  success = success &&
            process_pages(filter, NULL, TRUE) ;

  finish_fixed_document(xps_ctxt) ;

  /* Must run any end document config */
  success = success &&
            pt_config_end(&xps_ctxt->printticket, ! success) ;

  /* Must do font cache purge */
  xps_font_cache_purge() ;

  return success ;
}

/** XPS FixedDocument element start callback. */
static int32 xps_PageContent_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt;
  Bool status = FALSE, matched, source_added = FALSE ;
  static xps_partname_t *sourcecheck ;
  xps_partname_t *source ;
  static Bool height_set, width_set ;
  static double height, width ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(PageContent_LinkTargets), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(Height), NULL, &height_set, xps_convert_dbl_ST_GEOne, &height},
    { XML_INTERN(Width), NULL, &width_set, xps_convert_dbl_ST_GEOne, &width},
    { XML_INTERN(Source), NULL, NULL, xps_convert_part_reference, &sourcecheck},
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  /* Extract xps types */
  HQASSERT(filter != NULL, "NULL filter") ;
  HQASSERT(localname != NULL, "NULL localname") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "NULL xps_ctxt") ;

  sourcecheck = NULL ;

#define return DO_NOT_RETURN_go_to_cleanup_INSTEAD!

  matched = xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) ;

  /* Transfer source value to an auto variable so that cleanup works
     in the presence of recursive part parsing. */
  source = sourcecheck ;

  if (! matched ) {
    (void)error_handler(UNDEFINED);
    goto cleanup ;
  }

  if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
    (void)error_handler(UNDEFINED);
    goto cleanup ;
  }

  if (xps_have_processed_part(filter, source)) {
    (void)detailf_error_handler(UNDEFINED, "FixedPage %.*s is referenced more than once.",
                                intern_length(source->norm_name),
                                intern_value(source->norm_name)) ;
    goto cleanup ;
  }

  if (! xps_mark_part_as_processed(filter, source))
    goto cleanup ;

  source_added = add_new_page(xps_ctxt, source) ;
  if (! source_added)
    goto cleanup ;

  if (! process_pages(filter, source, FALSE))
    goto cleanup ;

#undef return
  status = TRUE ;
  return status ;

cleanup:
  /* Only de-allocate the source if we have not added it to the
     fixedpage. */
  if (source != NULL && ! source_added)
    xps_partname_free(&source) ;

  return status;
}

/** XPS PageContent LinkTargets property element start callback. */
static int32 xps_PageContent_LinkTargets_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(LinkTarget), XML_INTERN(ns_xps_2005_06), XMLG_ONE_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  return TRUE;
}

/** \} */

/*=============================================================================
 * Register functions
 *=============================================================================
 */
xpsElementFuncts fixeddocument_functions[] =
{
  { XML_INTERN(FixedDocument),
    xps_FixedDocument_Start,
    xps_FixedDocument_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(PageContent),
    xps_PageContent_Start,
    NULL,
    NULL /* No characters callback. */
  },
  { XML_INTERN(PageContent_LinkTargets),
    xps_PageContent_LinkTargets_Start,
    NULL,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
} ;

/* ============================================================================
* Log stripped */
