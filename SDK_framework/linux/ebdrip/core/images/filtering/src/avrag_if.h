/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:avrag_if.h(EBDSDK_P.1) $
 * $Id: src:avrag_if.h,v 1.6.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header for avrag_if.c
 */

#ifndef __AVRAG_IF_H__
#define __AVRAG_IF_H__

#include "imagfltr.h"

/* --Interface exposers-- */

ImageFilter* averageIFExposeImageFilter(void);

/* --Description--

Reducing the size of an image by discarding whole lines/columns results in
a rather ugly result, which can look markedly different as the reduction 
factor is changed. This is particularly evident in images containing many thin
lines/features.

This image filter is able to shrink an input image by a fractional amount,
averaging the source image so that data is not lost (eg. thin lines become 
less intense rather than disappearing completely).

*/

#endif

/* Log stripped */
