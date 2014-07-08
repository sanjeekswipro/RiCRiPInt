/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:builtin.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Builtin surfaces. This is a part of the Rainstorm Embedded interface.
 */

#ifndef __BUILTIN_H__
#define __BUILTIN_H__

/** \ingroup rendering */
/** \{ */

#include "surface.h"

struct Backdrop ;         /* COREbackdrop */
struct CompositeContext ; /* COREbackdrop */
struct render_blit_t ;    /* SWv20 */

/** \brief Hook up a surface to the builtin self-intersecting blit
    implementation. */
void surface_intersect_builtin(surface_t *surface) ;

/** \brief Hook up a surface to the builtin pattern blit implementation. */
void surface_pattern_builtin(surface_t *surface) ;

/** \brief Hook up a surface to the builtin gouraud blit implementation. */
void surface_gouraud_builtin_screened(surface_t *surface) ;

/** \brief Hook up a surface to the builtin gouraud blit implementation. */
void surface_gouraud_builtin_tone(surface_t *surface) ;

/** \brief Hook up a surface to the builtin gouraud blit implementation. */
void surface_gouraud_builtin_tone_multi(surface_t *surface) ;

/** Initialise a surface with the builtin 1-bit clip surface. */
void builtin_clip_1_surface(surface_t *surface, const surface_t *indexed[]) ;

/** Initialise a surface with the builtin spanlist clip surface. */
void builtin_clip_N_surface(surface_t *surfaces, const surface_t *indexed[]) ;

/** Attach builtin backdrop implementation to surface set. */
void surface_set_transparency_builtin(surface_set_t *set,
                                      surface_t *output,
                                      const surface_t *indexed[]) ;

/** Attach builtin modular halftone mask implementation to surface
    set. */
void surface_set_mht_mask_builtin(surface_set_t *set,
                                  const surface_t *indexed[]) ;

/** Attach builtin modular halftone 8-bit tone implementation to
    surface set. */
void surface_set_mht_ff_builtin(surface_set_t *set, const surface_t *indexed[]) ;

/** Attach builtin modular halftone 16-bit tone implementation to surface
    set. */
void surface_set_mht_ff00_builtin(surface_set_t *set, const surface_t *indexed[]) ;

void init_clip_surface(void) ;

/* Attach builtin trap surfaces to surface set */
void surface_set_trap_builtin(surface_set_t *set, const surface_t *index[]);

Bool render_backdrop_blocks(struct render_blit_t *rb,
                            struct CompositeContext *context,
                            const struct Backdrop *backdrop,
                            const dbbox_t *bounds,
                            Bool screened, SPOTNO spotNo, HTTYPE objtype);

Bool backdropblt_builtin(surface_handle_t handle,
                         struct render_blit_t *rb,
                         surface_backdrop_t group_handle,
                         surface_backdrop_t target_handle,
                         SPOTNO spotno,
                         HTTYPE objtype) ;
/** \} */

#endif /* __BUILTIN_H__ */

/* Log stripped */
