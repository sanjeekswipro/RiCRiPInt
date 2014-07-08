/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!export:fltrdimgh.h(EBDSDK_P.1) $
 * $Id: export:fltrdimgh.h,v 1.5.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Types for fltrdimg.c
 */

#ifndef __FLTRDIMGH_H__
#define __FLTRDIMGH_H__

/* --Public datatypes-- */

/* Image Filter */
typedef struct FilteredImage_s FilteredImage;

/* --Description--

This header contains a forward declaration of the FilteredImage datatype.
This is primarily required to prevent circular dependencies between fltrdimg.h
and images.h (they both need to include each other).
*/

#endif

/* Log stripped */
