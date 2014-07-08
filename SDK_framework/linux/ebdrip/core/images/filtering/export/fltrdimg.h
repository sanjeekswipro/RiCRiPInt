/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!export:fltrdimg.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header for fltrdimg.c.
 */


#ifndef __FLTRDIMG_H__
#define __FLTRDIMG_H__

#include "fltrdimgh.h"
#include "images.h"

struct core_init_fns ; /* from SWcore */

/** \defgroup imgfilter Image filtering.
    \ingroup images
    \{ */

/** Image or mask data selectors */
enum {
  ImageDataSelector = 1,
  MaskDataSelector
};

/* --Public methods-- */

/** Constructor; Returns FALSE on error. This configures the image filtering
system to process the image specified (which may be an image/mask/masked image).
If this image is 'accepted' by the filtering system, the FilteredImage instance
pointer will be set in the passed 'image' structure, and the elements of the
'image' structure may be changed to reflect the output of the image filtering
system (i.e. the dimensions of the image/mask may have changed).

No processing takes place during construction - this occurs as the client
requests filtered data via the filteredImageGetString() interface. */
Bool filteredImageNew(DL_STATE *page, IMAGEARGS *image);

/** Destructor. */
void filteredImageDelete(FilteredImage* this);

/** Read some filtered data; returns FALSE on error.
'target' will be set to point to the filtered data.
'dataLength' will be set to the amount of data available (this may be zero).
'imageOrMask' will be set to one of ImageDataSelector and MaskDataSelector,
indicating what type of data is available (this may change with each call when
processing a masked image). */
Bool filteredImageGetString(FilteredImage* this,
                            uint8** target,
                            uint32* dataLength,
                            uint32* imageOrMask);

/* --Description--

FilteredImage is used in image filtering. It represents an image that is being
filtered on its way into the rip.

Notes for Image Filter Writers
The FilteredImage object is responsible for configuring and using the image and
mask filter chains. In the case of a masked image, this object guarantees that
the image data will be presented to the image filter chain BEFORE the mask data
is presented to the mask filter chain. This information is only useful to those
writing single instance image/mask dual filters.

The document "Image Filtering Overview.pdf" (currently [July 2001] this
resides in the ScriptWorks information database in Notes, under
"Image Filtering") contains a section on this object.
*/

void filtering_C_globals(struct core_init_fns *fns) ;

/** \} */
#endif

/* Log stripped */
