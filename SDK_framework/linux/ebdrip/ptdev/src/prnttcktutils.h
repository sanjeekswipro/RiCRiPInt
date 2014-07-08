/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWptdev!src:prnttcktutils.h(EBDSDK_P.1) $
 */

#ifndef __PRINT_TICKET_UTILS_H__
#define __PRINT_TICKET_UTILS_H__

/**
 * @file
 * @brief Print ticket utilities.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  \brief Create a PostScript string to configure the RIP for
 *  job level processing.
 *
 *  Converts a Print Ticket tree into a PostScript representation filtered
 *  to contain settings only at the job level.  This object is then used to
 *  format the PostScript sequence needed to submit this data to the
 *  Print Ticket procset.
 *
 *  \param  pParam  Pointer to the Print Ticket parameter
 *  \return a malloc'ed string containing the PostScript (or NULL on error)
 */
char* pt_createJobStartPSFromPTParam (struct PT_PARAM* pParam);

/**
 *  \brief Create a PostScript string to configure the RIP for
 *  document level processing.
 *
 *  Converts a Print Ticket tree into a PostScript representation filtered
 *  to contain settings only at the document level.  This object is then used to
 *  format the PostScript sequence needed to submit this data to the
 *  Print Ticket procset.
 *
 *  \param  pParam  Pointer to the Print Ticket parameter
 *  \return a malloc'ed string containing the PostScript (or NULL on error)
 */
char* pt_createDocumentStartPSFromPTParam (struct PT_PARAM* pParam);

/**
 *  \brief Create a PostScript string to configure the RIP for
 *  page level processing.
 *
 *  Converts a Print Ticket tree into a PostScript representation filtered
 *  to contain settings only at the page level.  This object is then used to
 *  format the PostScript sequence needed to submit this data to the
 *  Print Ticket procset.
 *
 *  \param  pPageAreas  Points to a structure defining various page areas (such
 *  as the size of the FixedPage, ContentBox area, etc). All values should be
 *  in PostScript points.
 *  \param  fXPSSignatureValid  Should be FALSE if signature is invalid
 *  \param  pParam  Pointer to the Print Ticket parameter
 *  \return a malloc'ed string containing the PostScript (or NULL on error)
 */
char* pt_createPageStartPSFromPTParam (const struct PT_PAGEAREAS* pPageAreas,
                                       int fXPSSignatureValid,
                                       struct PT_PARAM* pParam);

#ifdef __cplusplus
};
#endif

#endif /* !__PRINT_TICKET_UTILS_H__ */

