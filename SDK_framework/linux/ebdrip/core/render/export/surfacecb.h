/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:surfacecb.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Surface callback functions. These functions are provided by the rest of
 * the RIP for use by surface callbacks. They are declared separately here
 * to present a clean interface for surfaces to access RIP state, without
 * importing extraneous definitions that the surface should not need to
 * access.
 */

#ifndef __SURFACECB_H__
#define __SURFACECB_H__

/** \ingroup rendering */
/** \{ */

#include "rendert.h" /* render_state_t, etc */

struct GUCR_CHANNEL ;
struct RleColorantMap ;

/** \brief Return the rasterstyle handle representing the first non-omitted
    channel of a sheet.

    \param sheet  The opaque sheet data type, as passed to the surface's
                  \c sheet_begin and \c sheet_end methods.

    \returns The first non-omitted channel in the sheet.
*/
/*@notnull@*/
struct GUCR_CHANNEL *sheet_rs_handle(/*@notnull@*/ const sheet_data_t *sheet) ;

/** \brief Return the render pass handle for a sheet.

    \param sheet  The opaque sheet data type, as passed to the surface's
                  \c sheet_begin and \c sheet_end methods.

    \returns The opaque render pass handle for this sheet.
*/
/*@notnull@*/
pass_data_t *sheet_pass(/*@notnull@*/ const sheet_data_t *sheet) ;

/** \brief Discover if a render pass has transparent regions.

    \param pass  The opaque pass data type.

    \retval TRUE  If the render pass is constructing transparent regions.
    \retval FALSE If the render pass involves direct rendered regions only.
*/
Bool pass_doing_transparency(/*@notnull@*/ const pass_data_t *pass) ;

/** \brief Discover if a render pass is the final pass.

    \param pass  The opaque pass data type.

    \retval TRUE  If the render pass is the final pass.
    \retval FALSE If the render pass is not final.
*/
Bool pass_is_final(/*@notnull@*/ const pass_data_t *pass) ;

/** \brief Return the RLE colorant map to use for a region.

    \param region  The opaque region callback data type.

    \returns An RLE colorant map to use whilst rendering the region.
*/
struct RleColorantMap *region_rle_colorant_map(const render_region_callback_t *region) ;

/** \} */

#endif /* __SURFACECB_H__ */

/* Log stripped */
