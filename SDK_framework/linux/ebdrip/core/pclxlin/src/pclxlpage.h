/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpage.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Additional page/media-related functions
 */

#ifndef __PCLXLPAGE_H__
#define __PCLXLPAGE_H__ 1

#include "pclxltypes.h"

extern Bool
pclxl_throw_page(PCLXL_GRAPHICS_STATE graphics_state,
                 PCLXL_NON_GS_STATE   non_gs_state,
                 PCLXL_MEDIA_DETAILS  media_details,
                 uint32               page_copies,
                 Bool                 duplex_alignment_page_throw);

extern Bool
pclxl_handle_duplex(PCLXL_CONTEXT       pclxl_context,
                    PCLXL_MEDIA_DETAILS previous_media_details,
                    PCLXL_MEDIA_DETAILS current_media_details);

extern Bool
pclxl_get_current_media_details(PCLXL_CONTEXT       pclxl_context,
                                PCLXL_MEDIA_DETAILS requested_media_details,
                                PCLXL_MEDIA_DETAILS current_media_details);

extern Bool
pclxl_setup_page_device(PCLXL_CONTEXT pclxl_context,
                        PCLXL_MEDIA_DETAILS previous_media_details,
                        PCLXL_MEDIA_DETAILS requested_media_details,
                        PCLXL_MEDIA_DETAILS current_media_details);

extern Bool
pclxl_set_default_ctm(PCLXL_CONTEXT pclxl_context,
                      PCLXL_GRAPHICS_STATE graphics_state,
                      PCLXL_NON_GS_STATE   non_gs_state);

Bool pclxl_set_page_clip(PCLXL_CONTEXT pclxl_context);

#endif

/******************************************************************************
* Log stripped */
