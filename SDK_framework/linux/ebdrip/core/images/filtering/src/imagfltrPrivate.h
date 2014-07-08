/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:imagfltrPrivate.h(EBDSDK_P.1) $
 * $Id: src:imagfltrPrivate.h,v 1.6.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private types and methods for the ImageFilter object.
 */

#ifndef __IMAGEFLTRPRIVATE_h__
#define __IMAGEFLTRPRIVATE_h__

#include "imagfltr.h"
#include "objnamer.h"

/* --Public types-- */

/* Pointer to a local image filter instance. */
typedef void *LocalImageFilter;

/* Interface definitions for local (i.e. C code) image filters. */

/* The present method presents an image (as described by the 'image' parameter)
to a filter, and allows it to accept this image for further processing (via the
'accepted' parameter, which is initialized by the caller to FALSE), and to 
specify what changes it will perform if processing takes place (via the 
'changes' parameter, which is initialized by the caller to match the source 
image).
This method is purely informational, allowing the image filtering system to 
determin which filters to create, and how to configure the filter chain. */
typedef Bool (*IFPresenter)(FilteredImageArgs* image, 
                            Bool* accepted, 
                            FilteredImageResult* changes);
                            
/* Image filter constructor. This is called only after the image (specified by
the 'image' parameter) has been accepted by a call to the presenter interface.
The 'target' parameter will be exactly as configure by the filter's presenter 
method. The image produced by the filter should not deviate from that specified
by 'target'. */
typedef LocalImageFilter (*IFConstructor)(FilteredImageArgs* image,
                                          FilteredImageResult* target);

/* Destructor - called after a single image has been processed. */
typedef void (*IFDestructor)(LocalImageFilter);


typedef void (*IFPushData)(LocalImageFilter, DataTransaction*);
typedef uint32 (*IFPullData)(LocalImageFilter, uint8**);

/* Local Image filter interface - a local filter must provide implementions for
all of these methods. */
typedef struct LocalImageFilterInterface_s {
  IFPresenter presenter;
  IFConstructor constructor;
  IFDestructor destructor;
  IFPushData push;
  IFPullData pull;
}LocalImageFilterInterface;

/* Image filter types */
enum {
  ImageFilterTypeLocal = 1,
  ImageFilterDead
};

/* Image filter container object */
struct ImageFilter_s {
  uint32 type;
  LocalImageFilter instance;
  LocalImageFilterInterface localFilter;
  
  OBJECT_NAME_MEMBER
};


/* --Public methods-- */

/* Allocate a blank image filter interface. */
ImageFilter* imageFilterNew(void);


/* --Description--

--Local Filter Interface details--

Sequence of events regarding construction and use of image filters:

1. A client obtains an interface to a specific filter.

2. When a new image/mask requires filtering, the IFPresenter interface is
called. The image/mask is described by the FilteredImageArgs structure
passed into the IFPresenter method.

If the filter accepts the image/mask for processing, the 'accepted' parameter
should be set to TRUE, and the FilteredImageResult structure should be modified
to reflect the output of the filter. E.g. if the filter produces an image that
is twice the size of the input, the width and height structure elements should
be doubled.

If the image was accepted by the filter:

3. The filter will be constructed, via the IFConstructor interface - the
FilteredImageArgs structure passed into this call will have exactly the same
values as that passed to the imageFilterPresent() method, but the 
FilteredImageResult structure will contain any modifications made in the present
method (this means that the filter does not need to repeat any calculations to
determin the target width/height etc).

The filter is allowed to return an instance pointer - this will be passed into
all subsequent calls.

4. The filter will given some data via the IFPushData interface. It is possible
that this method will be called when no data is available (i.e. the
DataTransaction passed into this method will contain no data).

5. The filter will be requested to emit some data via the IFPullData interface.
The passed pointer should be set to point at the filtered data, and the amount
available should be returned. It is acceptable to return 0 is no data is
available, although of course to avoid a stall condition you must return data
at some stage (it is not allowed for a filter to simply consume data).

6. The process of pushing and pulling data will continue until all lines of the
filtered image have been read. The filter will then be destroyed via the 
IFDestructor interface.


"intrpltr.c" contains a concrete example of a local image filter implementation.

--Using a Filter--

Once you have implemented a filter, in order to have it used you will need to 
add it to the image/mask filtering chain. Please refer to 
imageFilterChainImage() or imageFilterChainMask() in imfltchn.c.


The document "Image Filtering Overview.pdf" (currently [July 2001] this 
resides in the ScriptWorks information database in Notes, under 
"Image Filtering") contains a section on this object.
*/

#endif

/* Log stripped */
