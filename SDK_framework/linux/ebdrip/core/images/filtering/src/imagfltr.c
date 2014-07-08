/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:imagfltr.c(EBDSDK_P.1) $
 * $Id: src:imagfltr.c,v 1.9.10.1.1.1 2013/12/19 11:24:56 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface object for actual image filters.
 */

#include "core.h"
#include "imagfltrPrivate.h"

#include "swerrors.h"           /* error_handler */
#include "mm.h"

/* --Private macros-- */

#define IMAGEFILTER_NAME "Imagefilter interface"


/* --Public Methods-- */

/* Constructor - returns a blank ImageFilter object.
 */
ImageFilter* imageFilterNew(void)
{
  ImageFilter *self;

  self = (ImageFilter*) mm_alloc(mm_pool_temp, sizeof(ImageFilter), 
                                 MM_ALLOC_CLASS_IMAGE_FILTER);
  if (self == NULL) {
    (void) error_handler(VMERROR);
    return NULL;
  }
  NAME_OBJECT(self, IMAGEFILTER_NAME);

  return self;
}

/* Destructor - deletes the contained filter too, if it's valid
 */
void imageFilterDelete(ImageFilter* self)
{
  if (self == NULL) {
    return;
  }

  VERIFY_OBJECT(self, IMAGEFILTER_NAME);
  UNNAME_OBJECT(self);
  
  if (self->type == ImageFilterTypeLocal) {
    self->localFilter.destructor(self->instance);
  }
  else {
    HQFAIL("imageFilterDelete - only local filters are currently supported.");
  }
  
  /* free self. */
  mm_free(mm_pool_temp, self, sizeof(ImageFilter));
}

/* Is this image filter dead? (A dead filter is one that could not be found 
 * at runtime)
 */
Bool imageFilterDead(ImageFilter* self)
{
  VERIFY_OBJECT(self, IMAGEFILTER_NAME);

  return self->type == ImageFilterDead;
}

/* Present the passed image arguments to the filter, allowing it to accept 
 * the image (which may result in it changing the passed image args), or 
 * reject it.
 */
Bool imageFilterPresent(ImageFilter* self,
                        FilteredImageArgs* source,
                        Bool* accepted,
                        FilteredImageResult* changes)
{
  VERIFY_OBJECT(self, IMAGEFILTER_NAME);
  HQASSERT(source != NULL && accepted != NULL && changes != NULL, 
           "imageFilterPresent - parameters cannot be NULL");

  if (self->type == ImageFilterTypeLocal) {
    return self->localFilter.presenter(source, accepted, changes);
  }
  else {
    HQFAIL("imageFilterPresent - only local filters are currently supported.");
    return FALSE;
  }
}

/* Actually initialise the image filter. This is generally done after the 
 * filter has accepted the image through a call to present(). Returns 0 on 
 * success, 1 on error
 */
Bool imageFilterCreate(ImageFilter* self, 
                       FilteredImageArgs* source,
                       FilteredImageResult* target)
{
  VERIFY_OBJECT(self, IMAGEFILTER_NAME);
  HQASSERT(source != NULL, "imageFilterCreate - 'source' cannot be NULL");

  if (self->type == ImageFilterTypeLocal) {
    self->instance = self->localFilter.constructor(source, target);
    if (self->instance == NULL)
      return FALSE;
    else
      return TRUE;
  }
  else {
    HQFAIL("imageFilterCreate - only local filters are currently supported.");
    return FALSE;
  }
}

/* Destroy the image filter
 */
void imageFilterDestroy(ImageFilter* self)
{
  VERIFY_OBJECT(self, IMAGEFILTER_NAME);
  
  if (self->type == ImageFilterTypeLocal) {
    self->localFilter.destructor(self->instance);
    self->instance = NULL;
  }
  else {
    HQFAIL("imageFilterDestroy - only local filters are currently supported.");
  }
}

/* Some data at the filter. Filters consume exactly one input line at a time
 */
void imageFilterPush(ImageFilter* self, DataTransaction* data)
{
  VERIFY_OBJECT(self, IMAGEFILTER_NAME);
  HQASSERT(data != NULL, "imageFilterPush - 'data' cannot be NULL");

  if (self->type == ImageFilterTypeLocal) {
    self->localFilter.push(self->instance, data);
  }
  else {
    HQFAIL("imageFilterPush - only local filters are currently supported.");
  }
}

/* Try to get some data from the filter. There will either be one or more 
 * complete lines of data, or no data
 */
uint32 imageFilterPull(ImageFilter* self, uint8** result)
{
  VERIFY_OBJECT(self, IMAGEFILTER_NAME);
  HQASSERT(result!= NULL, "imageFilterPull - 'result' cannot be NULL");

  if (self->type == ImageFilterTypeLocal) {
    return self->localFilter.pull(self->instance, result);
  }
  else {
    HQFAIL("imageFilterPull - only local filters are currently supported.");
    return 0;
  }
}

/* Log stripped */
