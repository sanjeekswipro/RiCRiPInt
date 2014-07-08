/** \file
 * \ingroup render
 *
 * $HopeName: CORErender!src:stubs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2012-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for functions in the render compound which are accessed regardless
 * of variant.
 */

#include "core.h"
#include "rendert.h"
#include "surface.h"
#include "rleColorantMapping.h" /* RleColorantMap et. al. */

struct DL_STATE ;

#if !defined(BLIT_RLE_MONO) && !defined(BLIT_RLE_COLOR)
const GUCR_COLORANT_INFO *rle_sheet_colorants(sheet_data_t *sheet,
                                              surface_handle_t handle,
                                              ColorantListIterator *iterator,
                                              Bool *groupColorant)
{
  UNUSED_PARAM(sheet_data_t *, sheet) ;
  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(ColorantListIterator *, iterator) ;
  UNUSED_PARAM(Bool *, groupColorant) ;
  return NULL ;
}

RleColorantMap *rle_sheet_colorantmap(sheet_data_t *sheet,
                                      surface_handle_t handle)
{
  UNUSED_PARAM(sheet_data_t *, sheet) ;
  UNUSED_PARAM(surface_handle_t, handle) ;
  HQFAIL("Should not be called") ;
  return NULL ;
}

uint32 *rle_block_first(dcoord y, dcoord y1)
{
  UNUSED_PARAM(dcoord, y) ;
  UNUSED_PARAM(dcoord, y1) ;
  HQFAIL("Should not be called") ;
  return NULL ;
}
#endif

#ifndef BLIT_BACKDROP
void surface_set_transparency_builtin(surface_set_t *set,
                                      surface_t *output,
                                      const surface_t *indexed[])
{
  UNUSED_PARAM(surface_set_t *, set) ;
  output->backdropblit = NULL ;
  indexed[SURFACE_TRANSPARENCY] = NULL ;
}
#endif

/* Log stripped */
