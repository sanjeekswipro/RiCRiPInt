/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:cvdecodes.h(EBDSDK_P.1) $
 * $Id: src:cvdecodes.h,v 1.1.4.1.1.1 2013/12/19 11:25:27 anon Exp $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Decode arrays for mapping COLORVALUE values to input color values;
 * used in the backdrop and for preconverting objects from blend spaces.
 */

#ifndef __CVDECODES_H__
#define __CVDECODES_H__

void fndecodes_init(void);
void fndecodes_term(void);
Bool cv_alloc_decodes(DL_STATE *page, uint32 ncomps, Bool subtractive,
                      float ***cv_fdecodes, int32 ***cv_ndecodes,
                      uint32 *alloc_ncomps);
void cv_free_decodes(DL_STATE *page,
                     float ***cv_fdecodes, int32 ***cv_ndecodes,
                     uint32 *alloc_ncomps);
void cv_reset_decodes(DL_STATE *page);
size_t cv_decodes_size(void);

/* =============================================================================
* Log stripped */

#endif /* protection for multiple inclusion */
