/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpspt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * XPS package PrintTicket device handling functions.
 */

#ifndef __XPSPT_H__
#define __XPSPT_H__ (1)

#include "swdevice.h"
#include "coretypes.h"
#include "xpsparts.h"

/**
 * \brief Maintains the state of PrintTicket handling for XPS.
 */
typedef struct XPS_PT {
  int32       scope;  /**< Scope of next PT to be seen */
  DEVICELIST* device; /**< PT device to send PTs to. */
  OBJECT      scope_file; /**< Cached start scope file. */
  double      width;  /**< Cached page width for when there is no PT device. */
  double      height; /**< Cached page height for when there is no PT device. */
} XPS_PT;

/**
 * \brief Initialise XPS PrintTicket state for the start of a new Job.
 *
 * The print ticket scope is initially set Job. The scope is reduced to document
 * and page by subsequent calls to pt_config_start() and raised again on calls
 * to pt_config_end().  Print tickets must be merged and validated prior to
 * calls to pt_config_start() to ensure they are applied at the correct scope.
 *
 * \param[out] pt
 * Pointer to PrintTicket state to be initialised.
 *
 * \returns Pointer to enabled PrintTicket device, else \c NULL.
 */
extern
void pt_init(
/*@out@*/ /*@notnull@*/
  XPS_PT*   pt);

/**
 * \brief Merge and validate the PrintTicket part according to the current
 * scope.
 *
 * This function must be called before the corresponding calls to
 * pt_config_start() and pt_config_end() in order to merge and validate the
 * Print Ticket parameters at the appropriate scope.
 *
 * \param[in] filter
 * Pointer to XML filter.
 * \param[in] pt
 * XPS PrintTicket handler.
 * \param[in] part
 * Pointer to print ticket part.
 *
 * \returns
 * \c TRUE if the XPS PT part parsed without error, else \c FALSE.
 */
extern
Bool pt_mandv(
/*@in@*/ /*@notnull@*/
  xmlGFilter*      filter,
/*@in@*/ /*@notnull@*/
  XPS_PT*          pt,
/*@in@*/ /*@notnull@*/
  xps_partname_t*  part);

/**
 * \brief Perform start of scope RIP configuration.
 *
 * \param[in] pt
 * XPS PrintTicket handler.
 *
 * \returns
 * \c TRUE if the XPS PT part parsed without error, else \c FALSE.
 */
extern
Bool pt_config_start(
/*@in@*/ /*@notnull@*/
  XPS_PT*   pt);

/**
 * \brief Perform end of scope RIP configuration.
 *
 * \param[in] pt
 * XPS PrintTicket handler.
 * \param[in] abortjob
 * Flag indicating job is currently being aborted.
 *
 * \returns
 * \c TRUE if the XPS PT part parsed without error, else \c FALSE.
 */
extern
Bool pt_config_end(
/*@in@*/ /*@notnull@*/
  XPS_PT*   pt,
  Bool      abortjob);

/**
 * \brief Send FixedPage media information to the PT device.
 *
 * \param[in] pt
 * XPS print ticket handler.
 * \param[in] width
 * The width of the page.
 * \param[in] height
 * The height of the page.
 * \param[in] bleed_box
 * Pointer to the page bleed box.
 * \param[in] content_box
 * Pointer to the page content box.
 *
 * \returns
 * \c TRUE if the page details are successfully sent to the PrintTicket device,
 * else \c FALSE.
 */
extern
Bool pt_page_details(
/*@in@*/ /*@notnull@*/
  XPS_PT*     pt,
  double      width,
  double      height,
/*@in@*/ /*@notnull@*/
  RECTANGLE*  bleed_box,
/*@in@*/ /*@notnull@*/
  RECTANGLE*  content_box);

/**
 * \brief Get the index of the next page in the current FixedDocument to
 * interpret.
 *
 * The next page index returned is 0 based.  There are two special values
 * that can be returned:
 * -# \c METROPT_PAGES_ALL which indicates all remaining pages in the document
 * should be rendered, and
 * -# \c METROPT_PAGES_NOMORE which indicates that no more pages in the document
 * should be rendered.
 *
 * \param[in] pt
 * XPS print ticket handler.
 * \param[in] p_next_page
 * Pointer to returned next page index.
 *
 * \returns
 * \c TRUE if the next page index was set up, else \c FALSE.
 */
extern
Bool pt_next_page(
/*@in@*/ /*@notnull@*/
  XPS_PT*   pt,
/*@in@*/ /*@notnull@*/
  int32*    p_next_page);

#endif

/* Log stripped */
