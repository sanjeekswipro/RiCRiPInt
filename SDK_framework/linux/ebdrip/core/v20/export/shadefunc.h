/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!export:shadefunc.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Function-based shading expansion to DL addition.
 */

#ifndef __SHADEFUNC_H__
#define __SHADEFUNC_H__

struct SHADINGvertex ;
struct SHADINGinfo ;

/* Function type 1 (Function-based) hook, used by recombine */
Bool shading_function_decompose(struct SHADINGvertex *v0,
                                struct SHADINGvertex *v1,
                                struct SHADINGvertex *v2,
                                struct SHADINGvertex *v3,
                                struct SHADINGinfo *sinfo, int cindex) ;

#endif /* protection for multiple inclusion */



/* Log stripped */
