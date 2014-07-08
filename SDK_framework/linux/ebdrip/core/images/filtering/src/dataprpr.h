/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:dataprpr.h(EBDSDK_P.1) $
 * $Id: src:dataprpr.h,v 1.11.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Header for dataprpr.c
 */

#ifndef __DATAPRPR_H__
#define __DATAPRPR_H__

#include "datatrns.h"
#include "images.h"

/* --Public Datatypes-- */

/* Return values from the getLine method */
typedef enum DPReturnState {
  DPNormal = 1,
  DPError,
  DPPrematureEnd
} DPReturnState;

/* Filtered Image args */
typedef struct FilteredImageArgs {
  uint32 width;
  uint32 height;
  uint32 widthOnDevice;
  uint32 heightOnDevice;
  uint32 colorantCount;
  /* Original image's bits per component (Bpc). */
  uint32 originalBpc;
  /* Normalized Bpc (either 1, 8, or 16). */
  uint32 bpc;
  /* Number of bytes per normalized pixel (either 0, 1, or 2 depending on the
  normalized Bpc. */
  uint32 containerSize;
  /* One of ImageDataSelector and MaskDataSelector. */
  uint32 imageOrMask;
  Bool coerceToFillFlag;
  Bool interpolateFlag;
  OMATRIX imageToDevice;
  OMATRIX imageTransform;

  /* Original dictionary passed to image/mask operator. Will be NULL when
  non-dictionary variant of operator is used. */
  OBJECT* dictionary;
} FilteredImageArgs;

/* Data Preparer */
typedef struct DataPreparer DataPreparer;

/* --Public methods-- */

/* Returns TRUE if the passed image is supported by the data preparer. */
Bool dataPreparerSupported(IMAGEARGS* source);

/* Quickly (and without any allocations) process the passed image args, and
fill the passed (client managed) FilteredImageArgs structures with the
processed image/mask arguments as required (the 'imagePresent'/'maskPresent'
parameters will be set to TRUE to indicate that the corresponding arguments are
valid).

The resulting arguments will exactly match those generated for a DataSource
constructed using the same IMAGEARGS, and thus allows the client to quickly
determin if actual construction is required. */
Bool dataPreparerGetProcessedArgs(IMAGEARGS* source,
                                  uint32 deviceAccuracy,
                                  FilteredImageArgs* imageArgs,
                                  FilteredImageArgs* maskArgs,
                                  Bool* imagePresent,
                                  Bool* maskPresent);

DataPreparer* dataPreparerNew(IMAGEARGS* source, uint32 deviceBpc);
void dataPreparerDelete(DataPreparer* self);

Bool dataPreparerGetPreparedImageArgs(DataPreparer* self,
                                      FilteredImageArgs* target);
Bool dataPreparerGetPreparedMaskArgs(DataPreparer* self,
                                     FilteredImageArgs* target);
Bool dataPreparerCommitParameterChanges(DataPreparer* self);
DataTransaction* dataPreparerGetImage(DataPreparer* self);
DataTransaction* dataPreparerGetMask(DataPreparer* self);
Bool dataPreparerPureMask(DataPreparer* self);
void dataPreparerSetFinishedWithImage(DataPreparer* self);
void dataPreparerSetFinishedWithMask(DataPreparer* self);
DPReturnState dataPreparerGetLine(DataPreparer* self);

/* --Description--

DataPreparer is used in image filtering. It manages an input data stream (from
the get_image_string() function), and converts it into a normalised form -
image data is packed into bytes or double-bytes, and mask data is separated
into a 1-bit stream. The normalised data is available in image line sized
chunks.

This normalised data is generally then consumed by FilteredImage object, which
generally passes it through the image and mask filter chains

The document "Image Filtering Overview.pdf" (currently [July 2001] this
resides in the ScriptWorks information database in Notes, under
"Image Filtering") contains a section on this object.
*/

#endif

/* Log stripped */
