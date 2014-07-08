/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:rendert.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2010-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Forward type definitions for rendering.
 */

#ifndef __RENDERT_H__
#define __RENDERT_H__

/** \brief Opaque definition for span-level state. */
typedef struct render_blit_t render_blit_t ;

/** \brief Opaque definition for object-level state. */
typedef struct render_info_t render_info_t ;

/** \brief Opaque definition for page-level state. */
typedef struct render_state_t render_state_t ;

/** \brief Opaque definition for render output form collection. */
typedef struct render_forms_t render_forms_t ;

/** \brief Opaque definition for band render instance data. */
typedef struct band_data_t band_data_t ;

/** \brief Opaque definition for frame render instance data. */
typedef struct frame_data_t frame_data_t ;

/** \brief Opaque definition for sheet render instance data. */
typedef struct sheet_data_t sheet_data_t ;

/** \brief Opaque definition for pass render instance data. */
typedef struct pass_data_t pass_data_t ;

/** \brief Opaque definition for page render instance data. */
typedef struct page_data_t page_data_t ;

/** \brief Opaque definition for parameters passed back to
    render_band_callback_fn. */
typedef struct render_band_callback_t render_band_callback_t ;

/** \brief Render band callback function.
 *
 * This function is used when localising band rendering, permitting the caller
 * to keep local data on the C stack.
 */
typedef Bool (render_band_callback_fn)(render_band_callback_t *) ;

/** \brief Opaque definition for parameters passed back to
    render_region_callback_fn. */
typedef struct render_region_callback_t render_region_callback_t ;

/** \brief Render region callback function.
 *
 * This function is used when localising region rendering, permitting the
 * caller to keep local data on the C stack.
 */
typedef Bool (render_region_callback_fn)(render_region_callback_t *) ;

/* Log stripped */
#endif
