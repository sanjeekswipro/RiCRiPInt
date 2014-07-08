/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:mskscalr.h(EBDSDK_P.1) $
 * $Id: src:mskscalr.h,v 1.6.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header for mskscalr.c
 */

#ifndef __MSKSCALR_H__
#define __MSKSCALR_H__

#include "imagfltr.h"

/* --Interface exposers-- */

ImageFilter* maskScalerExposeImageFilter(void);

/* --Description--

MaskScaler is a type of image filter. It is capable of creating a 4x larger
version of a 1-bit image. Larger magnifications can be achieved by chaining
MaskScalers together in the filter chain. 

The document "Image Filtering Overview.pdf" (currently [July 2001] this 
resides in the ScriptWorks information database in Notes, under 
"Image Filtering") contains a section on this object.
*/

#endif

/* Log stripped */
