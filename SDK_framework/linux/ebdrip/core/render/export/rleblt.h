/** \file
 * \ingroup rleblit
 *
 * $HopeName: CORErender!export:rleblt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rle-length-Encode (RLE) blitting API
 */

#ifndef __RLEBLT_H__
#define __RLEBLT_H__ 1

/** \defgroup rleblit RLE blits
    \ingroup rendering */
/** \{ */

#include "coreinit.h"
#include "surfacet.h" /* sheet_handle_t */
#include "rendert.h" /* sheet_data_t */
#include "rleColorantMapping.h"

struct DL_STATE ; /* from SWv20 */
struct GUCR_COLORANT_INFO ;

/* --- Types --- */

/** Initialisation function called from renderinit.c */
void rleblt_C_globals(core_init_fns *fns) ;

/** Get the first data block pointer for a line, relative to the current
    band.

    \param y  The line to get the data block for.
    \param y1 The first line in this band.

    \returns  A pointer to the first RLE data block on the line.
*/
/*@notnull@*/
uint32 *rle_block_first(dcoord y, dcoord y1) ;

/** \brief Start an iteration over the RLE sheet colorant list. */
const struct GUCR_COLORANT_INFO *rle_sheet_colorants(sheet_data_t *sheet,
                                                     surface_handle_t handle,
                                                     ColorantListIterator *iterator,
                                                     Bool *groupColorant) ;

/** \brief Get the RLE sheet colorant map. */
RleColorantMap *rle_sheet_colorantmap(sheet_data_t *sheet,
                                      surface_handle_t handle) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
