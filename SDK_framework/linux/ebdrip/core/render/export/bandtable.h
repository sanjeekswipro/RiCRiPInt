/** \file
 * \ingroup bandmgmt
 *
 * $HopeName: CORErender!export:bandtable.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2013 Global Graphics Software.  All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Bandtable management interface
 */

#ifndef __BANDTABLE_H__
#define __BANDTABLE_H__ 1

#include "dlstate.h" /* DL_STATE */
#include "render.h" /* render_forms_t */
#include "graphict.h" /* GUCR_RASTERSTYLE */
#include "taskres.h" /* resource_pool_t */
#include "timelineapi.h" /* sw_tl_extent */
#include "swdevice.h"
#include "swraster.h"


/** \defgroup bandmgmt Rendering band management
    \ingroup rendering */
/** \{ */

/** \brief Type of band indices. */
typedef int32 bandnum_t; /* should be in dlstate.h. MUST be signed. */

/** \brief Band descriptor in the renderer's band table. */
typedef struct band_t {
  blit_t *mem; /**< Pointer to the band's buffer memory. */
  Bool fCompressed; /**< Band has been compressed. */
  Bool fDontOutput; /**< Don't output the band (if nothing ripped). */
  int32 size_to_write; /**< Size of band to write to page buffer dev. */
  struct band_t *next ; /**< Next band in list of buffers. */
} band_t;

/** \brief Parameter to set how many inches per dl band.

 This is used to make DL bands larger than raster bands.  It is set from
 the SwStart parameter. */
extern float band_factor;

/** \brief Computes the band size from the rasterstyle and the
    dimensions, and updates \a page accordingly.

 \param[in,out] page  Contains some page setup info; updated with band dimensions.
 \param[in] rs       Current raster style.
 \param[in] width    Width of the page in pixels.
 \param[in] height   Height of the page in pixels.
 \param[in] ResamplingFactor   The resampling factor for anti-aliasing.
 \param[in] BandHeight  The BandHeight page device parameter.
 \return Success indicator. */
Bool determine_band_size(DL_STATE *page, GUCR_RASTERSTYLE *rs,
                         int32 width, int32 height,
                         int32 ResamplingFactor, int32 BandHeight);

/** \brief Find the largest bandheight we can divide the basemap into for
     shape mask rendering.

    \param[in,out] page   The DL set the size for.

    \retval TRUE  The band height was set in the DL.
    \retval FALSE The band height would be too large for the basemap.

    \note This function does NOT include output, modular halftone mask or
    tone bands in the height calculation. */
Bool max_basemap_band_height(/*@notnull@*/ DL_STATE *page);

/** \brief Set up the form pointers for reserved mask bands using the basemap.

    \param[in,out] forms  The forms for which the addresses will be assigned.
    \param[in,out] page   The DL to assign forms for.

    \note This function does NOT assign modular halftone mask or tone bands.
*/
Bool mask_bands_from_basemap(render_forms_t *forms, DL_STATE *page) ;


/** \brief Set up \a forms to point to the reserved bands allocated for the
    current task group.

    \param[in,out] forms  The forms for which the addresses will be assigned.
    \param band_flags     Flag set for bands needed, as in DL_STATE::reserved_bands.

    For each \e reserved band required for the job, this function sets
    up the memory address for the corresponding form, for the current
    task group. */
void fix_reserved_band_resources(render_forms_t *forms, uint8 band_flags);

/** \brief Return output bands detached from band groups.

    \param pool  Resource pool owning bands.
    \param frame_and_line
                 The first line that the PGB device has not read. Lines are
                 calculated as a continuous sequence across all frames of a
                 sheet.
*/
int32 bands_copied_notify(resource_pool_t *pool, sw_tl_extent frame_and_line) ;

/** \brief Construct or update the band and line resource pools for a page.

    \param[in] page  The page for which to construct the band pools.

    \retval TRUE  The band pools were constructed or updated successfully.
    \retval FALSE The band pools were not constructed or updated.

    The band pools may only be modified during construction of a page (i.e.,
    before the rendering resource request for the page is made ready). The
    pool needs updating if the number of colorants per band is modified. */
Bool band_resource_pools(DL_STATE *page) ;

/** \brief Ensure that modular halftoning has enough extra bands to render
    the page.

    \param[in] page  The page for which to construct the band pools.

    \retval TRUE  The band pools were constructed or updated successfully.
    \retval FALSE The band pools were not constructed or updated.

    The band pools may only be modified during construction of a page (i.e.,
    before the rendering resource request for the page is made ready). The
    pool needs updating if the number of colorants per band is modified. */
Bool mht_band_resources(DL_STATE *page) ;


/** Call the pagebuffer device to communicate raster metrics and give it a
    scratch buffer.

  This call is the means to specify a framebuffer, and to ask for and receive a
  scratch band buffer. */
Bool call_pagebuffer_raster_requirements(DEVICELIST *pgbdev,
                                         Bool rendering_starting,
                                         DL_STATE *page,
                                         GUCR_RASTERSTYLE *rs,
                                         unsigned minimum_bands,
                                         size_t scratch_size,
                                         void *scratch_band);

/** Allocate an extension buffer for an output band.

    This is used to let complex RLE bands extend buffer space to avoid
    sub-dividing and re-doing rendering work. Extension buffers are allocated
    from the same pool as the initial output band. If extension buffers are
    allocated, they must be cleaned up before the band is unfixed by calling
    \c free_band_extensions(). Extension buffers are allocated at minimum
    cost.

    The size of extension buffers allocated will be in the band descriptor's
    \c band_t::size_to_write field. This will be the same size as normal
    output band buffers.

    \param pool  The resource pool used for output band buffers.
    \param pband The output band descriptor which is being extended.

    \retval TRUE  An extension buffer was allocated, and placed at the head
                  of the \c band_t::next list.
    \retval FALSE No extension buffer was allocated.
*/
Bool alloc_band_extension(resource_pool_t *pool, band_t *pband) ;

/** Free any extension buffers allocated for an output band.

    If any extension buffers were allocated for an output band, this function
    must be called to free them before unfixing the band resource.

    \param pool  The resource pool used for output band buffers.
    \param pband The output band descriptor which was extended.
*/
void free_band_extensions(resource_pool_t *pool, band_t *pband) ;

/** \} */

#endif /* __BANDTABLE_H__ */

/* Log stripped */
