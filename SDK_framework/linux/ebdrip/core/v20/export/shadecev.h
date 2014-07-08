/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!export:shadecev.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Colour and vertex utility functions for smooth shading.
 */

#ifndef __SHADECEV_H__
#define __SHADECEV_H__

#include "graphict.h" /* GUCR_RASTERSTYLE */
#include "functns.h"  /* FN_EVAL_UPWARDS/DOWNWARDS */
#include "dl_color.h" /* dl_color_t */
#include "shadex.h"   /* SHADINGvertex, SHADINGinfo */
#include "mm.h"       /* mm_pool_t */

Bool vertex_color(SHADINGvertex *vertex, SHADINGinfo *sinfo, Bool probe) ;

void vertex_interpolate(SHADINGinfo *sinfo,
                        int32 nweights, SYSTEMVALUE *weights,
                        SHADINGvertex *overtex, SHADINGvertex **ivertex,
                        int32 ncomp) ;

Bool vertex_alloc(SHADINGvertex **vert, int32 n) ;
void vertex_free(SHADINGinfo *sinfo, SHADINGvertex **vert, int32 n) ;

extern mm_pool_t mm_pool_shading ;

Bool shading_color(USERVALUE *comps, USERVALUE opacity,
                   Bool upwards, SHADINGinfo *sinfo, Bool probe) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
