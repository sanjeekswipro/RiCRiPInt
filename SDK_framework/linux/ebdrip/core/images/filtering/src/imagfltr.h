/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:imagfltr.h(EBDSDK_P.1) $
 * $Id: src:imagfltr.h,v 1.7.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header for imagfltr.c
 */

#ifndef __IMAGFLTR_H__
#define __IMAGFLTR_H__

#include "datatrns.h"
#include "dataprpr.h"

/* --Public datatypes-- */

/* Abstract Image filter object. */
typedef struct ImageFilter_s ImageFilter;

/* The structure allows filters to specify the changes they indend to make 
to an image as a result of their filter operation. Any field not listed in
this structure cannot be changed by a filter; i.e. the width of an image can be
changed, but the number of colorants cannot. */
typedef struct {
  uint32 width, height;
  /* The value for 'bpc' must be one of 1, 8 or 16. */
  uint32 bpc;
  /* Some filters may wish to supress interpolation by subsequent filters. */
  Bool interpolate;
}FilteredImageResult;

/* Macros for dealing with FilteredImageResult. */
#define filteredImageResultSet(self_, width_, height_, bpc_, interpolate_) \
  MACRO_START \
  (self_).width = (width_); \
  (self_).height = (height_); \
  (self_).bpc = (bpc_); \
  (self_).interpolate = (interpolate_); \
  MACRO_END

#define filteredImageResultEqual(self_, other_) \
  ((self_).width == (other_).width && (self_).height == (other_).height && \
   (self_).bpc == (other_).bpc)
  
/* --Public methods-- */

extern void imageFilterDelete(ImageFilter* self);
extern Bool imageFilterDead(ImageFilter* self);
extern Bool imageFilterPresent(ImageFilter* self,
                               FilteredImageArgs* source,
                               Bool* accepted,
                               FilteredImageResult* changes);
extern Bool imageFilterCreate(ImageFilter* self, 
                              FilteredImageArgs* source,
                              FilteredImageResult* target);
extern void imageFilterDestroy(ImageFilter* self);
extern void imageFilterPush(ImageFilter* self, DataTransaction* data);
extern uint32 imageFilterPull(ImageFilter* self, uint8** result);

/* --Description--

Note: imagfltrPrivate.h contains information for those wishing to write a new
local image filter.

ImageFilter is abstract interface to a real image filter. Actual instances of 
an ImageFilter must be obtained from the implementing object (e.g. via
averageIFExposeImageFilter()) - thus there is no constructor provided in this 
file.

The intent is that a filter's implementation be hidden behind this layer,
allowing it to take various forms (e.g. local code, Postscript code, plugin
interface, etc), whilst the client simply calls a standard set of methods to
perform data processing.

The document "Image Filtering Overview.pdf" (currently [July 2001] this 
resides in the ScriptWorks information database in Notes, under 
"Image Filtering") contains a section on this object.
*/

#endif

/* Log stripped */
