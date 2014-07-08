/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:intrpltr.h(EBDSDK_P.1) $
 * $Id: src:intrpltr.h,v 1.6.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header for intrpltr.c
 */

#ifndef __INTRPLTR_H__
#define __INTRPLTR_H__

#include "imagfltr.h"

/* --Interface exposers-- */

ImageFilter* interpolatorExposeImageFilter(void);

/* --Description--

Interpolator is a type of image filter. It is capable of creating an enlarged 
version of an input image which is interpolated. It can handle both 8-bit and 
16-bit input data
*/

#endif

/* Log stripped */
