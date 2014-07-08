/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:tiffclsp.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions of TIFF colorspace information
 */

#ifndef __TIFFSPOTS_H__
#define __TIFFSPOTS_H__

#include "objectt.h"  /* NAMECACHE */
#include "gs_color.h" /* COLORSPACE_ID */
#include "tifreadr.h" /* tiff_image_data_t */

/*
 * Structure holding colorspace specific setup information.
 */

typedef struct {
  uint32        f_supported;
  uint32        count_channels;
  COLORSPACE_ID space_id;
  NAMECACHE*    base_space;
  NAMECACHE**   channel_name;
} tiff_color_space_t;

typedef struct cmyk_spot_data cmyk_spot_data ;

Bool tiff_init_noncmyk_colorspace(tiff_image_data_t *self,
                                  mm_pool_t mm_pool);

void tiff_destroy_noncmyk_colorspace(tiff_image_data_t *self);

Bool tiff_get_noncmyk_colorspace(tiff_image_data_t *self, OBJECT *cspace);

Bool tiff_set_DeviceN_colorspace(tiff_color_space_t* p_color_space,
                                 tiff_image_data_t*  p_image_data);

Bool tiff_set_noncmyk_colorspace(tiff_image_data_t *self);

/* Log stripped */

#endif /* protection for multiple inclusion */
