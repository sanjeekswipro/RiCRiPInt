/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:renderbd.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2013-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for rendering a backdrop.
 */

#include "core.h"
#include "renderfn.h"
#include "backdrop.h"
#include "blitcolors.h"
#include "blitcolorh.h"
#include "bitblts.h"
#include "blttables.h"
#include "pixelLabels.h"
#include "htrender.h"
#include "display.h"
#include "spotlist.h"
#include "cvcolcvt.h"
#include "often.h"


/** Set up a blit color from a 8-bit or 16-bit backdrop color.

    \param[in,out] color        The blit color to fill in.
    \param[in] color8   Pointer to the color value, if 8-bit.
    \param[in] color16  Pointer to the color value, if 16-bit.
    \param[in] expanded_to_plane Mapping from expander indices to plane indices.
    \param nexpanded            The number of channels of expanded data.
    \param[in] blit_to_expanded Mapping from blit color channels to expander
                                indices.
    \param label  Object type label.
    \param rendering_intent  The rendering intent.
    \param selected  This is an object of interest for a mask.
*/
static inline void expand_color(blit_color_t *color,
                                const uint8 *color8,
                                const COLORVALUE *color16,
                                int expanded_to_plane[],
                                unsigned int nexpanded,
                                const int blit_to_expanded[],
                                uint8 label,
                                uint8 rendering_intent,
                                Bool selected)
{
  const blit_colormap_t *map ;
  channel_index_t index ;

  UNUSED_PARAM(unsigned int, nexpanded) ; /* For asserts only */

  VERIFY_OBJECT(color, BLIT_COLOR_NAME) ;
  HQASSERT((color8 != NULL) ^ (color16 != NULL),
           "Must have either 8 or 16 bit color values, but not both");
  HQASSERT(blit_to_expanded != NULL, "No expander to blit channel mapping") ;
  HQASSERT(nexpanded > 0, "No expanded data channels") ;

  HQASSERT(color->quantised.spotno != SPOT_NO_INVALID,
           "Quantised screen not set for expander color") ;

  map = color->map ;
  VERIFY_OBJECT(map, BLIT_MAP_NAME) ;

  /* This loop handles rendering properties for the channels in the same way as
     blit_color_unpack(). It's slightly simpler, because this cannot be the
     erase color that is being unpacked, and the knockout/overprint status of
     the spans is fixed. It's objectionable having multiple copies of such a
     complex piece of logic, but I haven't come up with a suitable alternative
     that handles the difference between the backdrop filling in the quantised
     color directly and the unpacker filling in the unpacked color. */
  for ( index = 0 ; index < map->nchannels ; ++index ) {
    int eindex = blit_to_expanded[index] ;
    if ( eindex >= 0 ) {
      uint16 qcv = (color8
                    ? color8[expanded_to_plane[eindex]]
                    : color16[expanded_to_plane[eindex]]);

      HQASSERT(BITVECTOR_IS_SET(map->rendered, index),
               "Channel is expanded, but should not be rendered") ;
      HQASSERT((unsigned int)eindex < nexpanded, "Expanded index out of range") ;

      color->quantised.qcv[index] = qcv ;

      if ( qcv == COLORVALUE_TRANSPARENT ) { /* overprint */
        blit_channel_mark_absent(color, index) ;
      } else {
        HQASSERT(qcv <= color->quantised.htmax[index]
                 /* mask channel will override to 0/1 */
                 || blit_map_mask_channel(map, index),
                 "Expanded color is not quantised to valid range") ;
        blit_channel_mark_present(color, index) ;
      }
    }
  }

  /* Expand the type label into the blit entry */
  color->type = label ;
  /* Don't set color->quantised.type, because it won't be needed. */

  /* Map the quantised type through the type mapping array. */
  if ( map->type_lookup != NULL ) {
    color->quantised.qcv[map->type_index] = map->type_lookup[color->type] ;
  } else {
    color->quantised.qcv[map->type_index] = (channel_output_t)color->type ;
  }

  color->rendering_intent = rendering_intent;

  /* This is a bit sneaky; the backdrop has quantised data in it, so we don't
     have a valid unpacked value for this color. So, set it quantised, and hope
     we don't need the unpacked color. */
#ifdef ASSERT_BUILD
  color->valid = blit_color_quantised ;
#endif
  color->quantised.state = blit_quantise_unknown ;

  if ( map->apply_properties && color->ncolors > 0 )
    blit_apply_render_properties(color, selected, FALSE);
}


Bool render_backdrop_blocks(render_blit_t *rb, CompositeContext *context,
                                   const Backdrop *backdrop,
                                   const dbbox_t *bounds,
                                   Bool screened, SPOTNO spotNo, HTTYPE objtype)
{
  uint32 nComps;
  COLORANTINDEX *colorants = bd_backdropColorants(backdrop, &nComps);
  const blit_slice_t *slice = &rb->p_ri->surface->baseblits[0];
  blit_color_t saved_color = *rb->color; /* expand_color() twiddles it */
  int blit_to_expanded[BLIT_MAX_CHANNELS];
  int expanded_to_plane[BLIT_MAX_CHANNELS];
  unsigned int expanded_comps;
  Bool result;
  /* When a modular halftone is used for the erase color, the mask is
     initialised black, so we deselect the spans so that they get knocked
     out. If we're not using a modular halftone, or it's not used for the
     erase, we need to select the spans so they are masked with black. */
  Bool selected = !rb->p_ri->p_rs->cs.selected_mht_is_erase;

  /* Generate the two mappings needed, one for the expander, the other for the
     pixel extractor. */
  blit_expand_mapping(rb->color, colorants, nComps, FALSE,
                      expanded_to_plane, &expanded_comps,
                      blit_to_expanded);

  /* Iterate over the backdrop blocks. */
  for ( bd_readerInit(context, bounds);
        bd_readerNext(context, backdrop, &result); ) {
    const dbbox_t *block;
    uint8 *color8;
    COLORVALUE *color16;
    COLORINFO *info;

    if ( screened )
      LOCK_HALFTONE(rb->p_ri->ht_params);

    /* Iterate over the blit blocks. */
    while ( (block = bd_blockReader(context, backdrop,
                                    &color8, &color16, &info)) != NULL ) {
      /* Blit blocks which are not using the current spotno and object type are
         ignored.  These blits are rendered in another call to
         render_backdrop_blocks.  This avoids excessive switching between
         screens. */
      HQASSERT(info->spotNo != SPOT_NO_INVALID,
               "Invalid spot number in backdrop");
      HQASSERT(info->label != SW_PGB_INVALID_OBJECT,
               "Invalid pixel label in backdrop");
      if ( info->label != 0 &&
           (spotNo == SPOT_NO_INVALID || info->spotNo == spotNo) &&
           (objtype == HTTYPE_DEFAULT || info->reproType == objtype) ) {

        rb->ylineaddr =
          BLIT_ADDRESS(theFormA(*rb->outputform),
                       theFormL(*rb->outputform) *
                       (block->y1 - theFormHOff(*rb->outputform) - rb->y_sep_position));
        rb->ymaskaddr =
          BLIT_ADDRESS(theFormA(*rb->clipform),
                       theFormL(*rb->clipform) *
                       (block->y1 - theFormHOff(*rb->clipform) - rb->y_sep_position));

        expand_color(rb->color, color8, color16, expanded_to_plane,  expanded_comps,
                     blit_to_expanded, info->label,
                     COLORINFO_RENDERING_INTENT(info->lcmAttribs),
                     selected);
        if ( rb->color->ncolors == 0 ) /* complete overprint */
          continue;

        SET_BLITS_CURRENT(rb->blits, BASE_BLIT_INDEX, &slice[BLT_CLP_NONE],
                          &slice[BLT_CLP_RECT], &slice[BLT_CLP_COMPLEX]);

        blit_color_pack(rb->color);

        DO_BLOCK(rb, block->y1, block->y2, block->x1, block->x2);
      }
    }

    if ( screened )
      UNLOCK_HALFTONE(rb->p_ri->ht_params);
  }
  *rb->color = saved_color;
  return result;
}


static Bool render_backdrop_one_type(render_blit_t *rb,
                                     CompositeContext *context,
                                     const Backdrop *backdrop,
                                     const dbbox_t *bounds,
                                     SPOTNO spotNo, HTTYPE objtype)
{
  const render_state_t *p_rs = rb->p_ri->p_rs;

  /* Have a look into the blit color's map to see what the first channel's
     color index is. */
  COLORANTINDEX ci = blit_map_sole_index(p_rs->cs.blitmap);

  if ( p_rs->htm_info != NULL ) {
    if ( p_rs->cs.selected_mht == NULL ) {
      /* If doing in-RIP halftones only, skip modular ones. */
      if ( ht_getModularHalftoneRef(spotNo, objtype, ci) != NULL )
        return TRUE;
    } else {
      /* If doing a modular ht (mask), skip selected or non-selected, depending
         on whether the erase color uses this ht. This slightly obscure
         condition takes care of both cases. If the erase color is this MHT,
         then its mask was initialised to black, so we need to render all of the
         areas knocked out by other spots. If the erase color is not this MHT,
         then its mask was initialised to white, and we need to render this
         spot. */
      if ( (ht_getModularHalftoneRef(spotNo, objtype, ci)
            == p_rs->cs.selected_mht)
           == p_rs->cs.selected_mht_is_erase )
        return TRUE;
    }
  }

  /* Load the next screen. */
  blit_quantise_set_screen(rb->color, spotNo, objtype);
  blit_color_quantise(rb->color); /* requantise to get knockouts right */
  render_gethalftone(rb->p_ri->ht_params, spotNo, objtype, ci, NULL);

  return render_backdrop_blocks(rb, context, backdrop, bounds,
                                TRUE, spotNo, objtype) ;
}


Bool backdropblt_builtin(surface_handle_t handle,
                         render_blit_t *rb,
                         surface_backdrop_t group_handle,
                         surface_backdrop_t target_handle,
                         SPOTNO spotno,
                         HTTYPE objtype)
{
  const render_info_t *p_ri ;
  const render_state_t *p_rs ;

  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(surface_backdrop_t, target_handle) ;

  HQASSERT(RENDER_BLIT_CONSISTENT(rb), "Render blit not consistent") ;
  p_ri = rb->p_ri ;
  p_rs = p_ri->p_rs ;

  HQASSERT(p_rs->cs.fIsHalftone == p_ri->surface->screened,
           "Render state screening does not match surface screening") ;

  if ( spotno == SPOT_NO_INVALID ) {
    /* No spot number selected, do all blocks together. Halftone type
       does not matter in this case either, because it's not an object
       based screen, but we'll pass the parameter through anyway. */
    return render_backdrop_blocks(rb, p_rs->composite_context,
                                  group_handle, &p_ri->clip,
                                  p_rs->cs.fIsHalftone,
                                  spotno, objtype) ;
  }

  return render_backdrop_one_type(rb, p_rs->composite_context,
                                  group_handle, &p_ri->clip, spotno, objtype) ;
}

/* Log stripped */
