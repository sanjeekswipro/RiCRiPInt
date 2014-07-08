/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!export:blends.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Type 2 & 3 smooth shading
 */

#ifndef __BLENDS_H__
#define __BLENDS_H__


struct SHADINGinfo ;

Bool axialblend(SYSTEMVALUE coords[4], USERVALUE colors[2], USERVALUE opacity[2],
                Bool extend[2], struct SHADINGinfo *sinfo) ;
Bool radialblend(SYSTEMVALUE coords[6], USERVALUE colors[2], USERVALUE opacity[2],
                 Bool extend[2], struct SHADINGinfo *sinfo) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
