/* Copyright (c) 2007 Global Graphics Software Ltd. All Rights Reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: COREedoc!shared:discardStream.h(EBDSDK_P.1) $ */

/**
 * \file
 * \ingroup xps
 * \brief
 * Public interface for the XPS DiscardControl implementation.
 */

#ifndef _discardStream_h_
#define _discardStream_h_

#include "xpsparts.h"

typedef struct DiscardParser DiscardParser;

/**
 * Create a new discard control part parser. Note that 'discardPart' will be
 * managed by the parser and should NOT be deallocated by the caller, even if
 * this method returns FALSE.
 * \return FALSE on error.
 */
Bool xps_open_discard_parser(xps_partname_t* discardPart,
                             DiscardParser** discardParser);

/**
 * Destroy the passed discard parser.
 */
void xps_close_discard_parser(DiscardParser** discardParser);

/**
 * Process any discards for the page being processed by the passed filter.
 */
Bool xps_process_discards(xmlGFilter *fixedPageFilter,
                          DiscardParser* discardParser);

/**
 * Immediately discard the specified part.
 */
Bool xps_discard_part(xps_partname_t* part);

#endif

/* Log stripped */

