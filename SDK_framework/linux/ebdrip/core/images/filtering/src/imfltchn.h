/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:imfltchn.h(EBDSDK_P.1) $
 * $Id: src:imfltchn.h,v 1.9.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header for imfltchn.c
 */

#ifndef __IMFLTCHN_H_
#define __IMFLTCHN_H_

#include "dataprpr.h"
#include "datatrns.h"
#include "matrix.h"
#include "imagfltr.h"

/* --Public datatypes-- */

typedef struct ImageFilterChain_s ImageFilterChain;

/* --Public methods-- */

extern int32 imageFilterChainImage(ImageFilterChain** chainOut);
extern int32 imageFilterChainMask(ImageFilterChain** chainOut);


/* Query method to quickly determin if the passed chain would filter the passed 
image.
No filters are actually instantiated, and as soon as any filter accepts the 
image 'filteringActive' is set to TRUE. If no filters accepted the image,
'filteringActive' will be FALSE. 
Returns FALSE on error. */
Bool imageFilterChainCheckImage(ImageFilterChain* self, 
                                FilteredImageArgs* args,
                                uint32 forceNoInterpolate,
                                Bool* filteringActive);
                                

/* Constructor. There are two stages to a filter chain - those filters which
always applied (as specified by the "filterNames" array), and those filters
which are activated only when the "interpolate" flag is true in an image
dictionary (as specified by "interpolatorNames" array). */
ImageFilterChain* imageFilterChainNew(uint32 filterCount, 
                                      uint32* filterNames, 
                                      uint32 interpolatorCount,
                                      uint32* interpolatorNames);
void imageFilterChainDelete(ImageFilterChain* self);

uint32 imageFilterChainEmpty(ImageFilterChain* self);
Bool imageFilterChainNewImage(ImageFilterChain* self, 
                              FilteredImageArgs* args, 
                              uint32 forceNoInterpolate,
                              uint32* totalFilters);
void imageFilterChainEndImage(ImageFilterChain* self);
void imageFilterChainSetDataSource(ImageFilterChain* self,
                                   DataTransaction* data);
void imageFilterChainPush(ImageFilterChain* self);
uint32 imageFilterChainPull(ImageFilterChain* self, uint8** result);

/* Call out to postscipt to allow user-level filter control.  */
Bool runFilterController(FilteredImageArgs* args, 
                         uint32 procName,
                         Bool* disableFilter,
                         FilteredImageResult* changes,
                         Bool* changesMade);

/* --Description--

This object manages a chain of image filters. Data flows into one filter, out 
of it, and into the next filter in the chain. This process is complicated by 
filters consuming and producing data at unpredictable rates, and by data 
dependencies between one filter and the next in the chain.

The interpolate flag in PostScipt is handled by the chain object - it appends 
one or more interpolate filters to the end of the chain (which may be empty 
prior to the appending) when the interpolate flag is set. The details of which
filter and how many of them should be appended is specifed in the new() method 
call.

Calls to setDataSource(), push(), and pull() are only valid between calls to 
newImage() and endImage().

The document "Image Filtering Overview.pdf" (currently [July 2001] this 
resides in the ScriptWorks information database in Notes, under 
"Image Filtering") contains a section on this object.
*/

#endif

/* Log stripped */
