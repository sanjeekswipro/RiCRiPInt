/** \file
 * \ingroup renderloop
 *
 * $HopeName: CORErender!src:renderim.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image rendering code
 */

#ifndef __RENDERIM_H__
#define __RENDERIM_H__


#include "imageo.h"             /* IMAGEOBJECT */
#include "render.h"             /* render_blit_t */

Bool setupmaskandgo(IMAGEOBJECT *theimage, render_blit_t *rb) ;

Bool setuprotatedmaskandgo(IMAGEOBJECT *theimage, render_blit_t *rb);

Bool setupimageandgo(IMAGEOBJECT *theimage, render_blit_t *rb,
                     Bool screened) ;

Bool setupbackdropandgo(IMAGEOBJECT *theimage, render_blit_t *rb,
                        Bool screened) ;

Bool setup3partimageandgo(IMAGEOBJECT *theimage, render_blit_t *rb,
                          Bool screened) ;

Bool setuprotatedimageandgo(IMAGEOBJECT *theimage, render_blit_t *rb,
                            Bool screened) ;

/* ---------------------------------------------------------------------- */

#endif /* protection for multiple inclusion */



/* Log stripped */
