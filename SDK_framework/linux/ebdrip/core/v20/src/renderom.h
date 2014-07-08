/** \file
 * \ingroup rendering
 *
 * $HopeName: SWv20!src:renderom.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Separation omission logic
 */

#ifndef __RENDEROM_H__
#define __RENDEROM_H__

#include "dlstate.h"

typedef struct OMIT_DETAILS {
  uint8 beginpage, endpage, imagecontents, superblacks, registermarks, knockouts ;
}  OMIT_DETAILS ;

typedef struct DL_OMITDATA DL_OMITDATA;

/* Clear the accumulated data about separations marked. This is called when
   the page is erased, and data accumulates over possibly multiple partial
   paints until it is called again. */
Bool start_separation_omission(DL_STATE *page);
void finish_separation_omission(DL_STATE *page);
void omit_unpack_rasterstyle(const DL_STATE *page);

/* Scan the display list, accumulating information about separations used. */
Bool omit_blank_separations(const DL_STATE *page) ;

/* If all objects on a page were registermarks, superblacks, or /All separation
   objects, then we need only output the Black plate. This function effects
   the transfer, setting the omit flags for all but the black plate. */
Bool omit_black_transfer(DL_OMITDATA *omit_data) ;

/* Prepare for backdrop table separation omission */
Bool omit_backdrop_prepare(DL_STATE *page) ;

/* Scan a backdrop colorant before quantisation. */
struct COLORINFO;
Bool omit_backdrop_color(DL_OMITDATA *omit_data,
                         int32 nComps, 
                         const COLORANTINDEX *cis,
                         const COLORVALUE *cvs,
                         const struct COLORINFO *info,
                         Bool *maybe_marked) ;

/* Scan a backdrop colorant after halftone quantisation. */
Bool omit_backdrop_devicecolor(DL_OMITDATA *omit_data,
                               int32 nComps,
                               const COLORANTINDEX *cis,
                               const COLORVALUE *cvs,
                               SPOTNO spotno,
                               HTTYPE httype);

#if defined(DEBUG_BUILD)
void init_sepomit_debug(void);
#endif

#endif /* protection for multiple inclusion */


/* Log stripped */
