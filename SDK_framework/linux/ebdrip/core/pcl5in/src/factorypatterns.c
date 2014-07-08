/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:factorypatterns.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This class defines the factory-installed patterns for PCL5.
 */

#include "core.h"
#include "pcl5context.h"
#include "pcl5resources.h"
#include "factorypatterns.h"

#define PACK(b7, b6, b5, b4, b3, b2, b1, b0) \
  ((b7 << 7) | (b6 << 6) | (b5 << 5) | (b4 << 4) | \
   (b3 << 3) | (b2 << 2) | (b1 << 1) | b0)

static pcl5_pattern shadingPatterns[8];
static uint8 shading1Data[16] = {
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 1, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 1, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0)};
static uint8 shading2Data[8] = {
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 1, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 1, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0)};
static uint8 shading3Data[8] = {
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 1, 1, 0),
  PACK(0, 0, 0, 0, 0, 1, 1, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 1, 1, 0, 0, 0, 0, 0),
  PACK(0, 1, 1, 0, 0, 0, 0, 0)};
static uint8 shading4Data[8] = {
  PACK(0, 1, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 1, 0, 0),
  PACK(0, 0, 0, 0, 1, 1, 1, 0),
  PACK(0, 0, 0, 0, 1, 1, 1, 0),
  PACK(0, 0, 0, 0, 0, 1, 0, 0),
  PACK(0, 1, 0, 0, 0, 0, 0, 0),
  PACK(1, 1, 1, 0, 0, 0, 0, 0),
  PACK(1, 1, 1, 0, 0, 0, 0, 0)};
static uint8 shading5Data[8] = {
  PACK(0, 1, 0, 0, 0, 1, 0, 0),
  PACK(0, 0, 0, 0, 1, 1, 1, 0),
  PACK(0, 1, 0, 1, 1, 1, 1, 1),
  PACK(0, 0, 0, 0, 1, 1, 1, 0),
  PACK(0, 1, 0, 0, 0, 1, 0, 0),
  PACK(1, 1, 1, 0, 0, 0, 0, 0),
  PACK(1, 1, 1, 1, 0, 1, 0, 1),
  PACK(1, 1, 1, 0, 0, 0, 0, 0)};
static uint8 shading6Data[8] = {
  PACK(1, 1, 1, 0, 1, 1, 1, 0),
  PACK(0, 0, 0, 1, 1, 1, 1, 1),
  PACK(0, 0, 0, 1, 1, 1, 1, 1),
  PACK(0, 0, 0, 1, 1, 1, 1, 1),
  PACK(1, 1, 1, 0, 1, 1, 1, 0),
  PACK(1, 1, 1, 1, 0, 0, 0, 1),
  PACK(1, 1, 1, 1, 0, 0, 0, 1),
  PACK(1, 1, 1, 1, 0, 0, 0, 1)};
static uint8 shading7Data[8] = {
  PACK(1, 1, 1, 1, 1, 1, 1, 1),
  PACK(1, 0, 1, 1, 1, 1, 1, 1),
  PACK(0, 0, 0, 1, 1, 1, 1, 1),
  PACK(1, 0, 1, 1, 1, 1, 1, 1),
  PACK(1, 1, 1, 1, 1, 1, 1, 1),
  PACK(1, 1, 1, 1, 1, 0, 1, 1),
  PACK(1, 1, 1, 1, 0, 0, 0, 1),
  PACK(1, 1, 1, 1, 1, 0, 1, 1)};

static pcl5_pattern crossHatchPatterns[6];
static uint8 crossHatch1Data[32] = {
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 1, 1, 1, 1),
  PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 1, 1, 1, 1),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
};
static uint8 crossHatch2Data[32] = {
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
};
static uint8 crossHatch3Data[32] = {
  PACK(1, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 1),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 1, 1),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 1, 1, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 1, 1, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 1, 1, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 1, 1, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 1, 1, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(1, 1, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 1, 1), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 1, 1, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 1, 1, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 1, 1, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 1, 1, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 1, 1, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(1, 1, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
};
static uint8 crossHatch4Data[32] = {
  PACK(1, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 1),
  PACK(1, 1, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 1, 1, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 1, 1, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 1, 1, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 1, 1, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 1, 1, 0), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 1, 1), PACK(0, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(1, 1, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 1, 1, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 1, 1, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 1, 1, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 1, 1, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 1, 1, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 1, 1),
};
static uint8 crossHatch5Data[32] = {
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 1, 1, 1, 1),
  PACK(1, 1, 1, 1, 1, 1, 1, 1), PACK(1, 1, 1, 1, 1, 1, 1, 1),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
};
static uint8 crossHatch6Data[32] = {
  PACK(1, 0, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 0, 1),
  PACK(1, 1, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 1, 1),
  PACK(0, 1, 1, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 1, 1, 0),
  PACK(0, 0, 1, 1, 0, 0, 0, 0), PACK(0, 0, 0, 0, 1, 1, 0, 0),
  PACK(0, 0, 0, 1, 1, 0, 0, 0), PACK(0, 0, 0, 1, 1, 0, 0, 0),
  PACK(0, 0, 0, 0, 1, 1, 0, 0), PACK(0, 0, 1, 1, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 1, 1, 0), PACK(0, 1, 1, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 1, 1), PACK(1, 1, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 0, 1), PACK(1, 0, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 0, 1, 1), PACK(1, 1, 0, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 0, 1, 1, 0), PACK(0, 1, 1, 0, 0, 0, 0, 0),
  PACK(0, 0, 0, 0, 1, 1, 0, 0), PACK(0, 0, 1, 1, 0, 0, 0, 0),
  PACK(0, 0, 0, 1, 1, 0, 0, 0), PACK(0, 0, 0, 1, 1, 0, 0, 0),
  PACK(0, 0, 1, 1, 0, 0, 0, 0), PACK(0, 0, 0, 0, 1, 1, 0, 0),
  PACK(0, 1, 1, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 1, 1, 0),
  PACK(1, 1, 0, 0, 0, 0, 0, 0), PACK(0, 0, 0, 0, 0, 0, 1, 1),
};

/**
 * Init the passed cache entry using the specified values. The data is assumed
 * to be 1-bit monochrome. The DPI will be set to 300.
 */
static void cacheEntryInit(pcl5_pattern *pattern, pcl5_resource_numeric_id numeric_id,
                           uint32 width, uint32 height,
                           uint8* data)
{
  pattern->detail.resource_type= SW_PCL5_PATTERN;
  pattern->detail.numeric_id = numeric_id;
  pattern->detail.permanent = TRUE;
  pattern->detail.device = NULL;
  pattern->detail.private_data = NULL;
  pattern->detail.private_data = NULL;
  pattern->detail.PCL5FreePrivateData = NULL ;

  pattern->width = width;
  pattern->height = height;
  pattern->x_dpi = pattern->y_dpi = 300;
  pattern->color = FALSE;
  pattern->bits_per_pixel = 1;
  pattern->stride = (pattern->width + 7) / 8;
  pattern->data = data;
}

static Bool initShadingPatterns(PCL5IdCache *id_cache)
{
  int16 i;
  pcl5_pattern *new_pattern ; /* We ignore as we don't need it. */

  cacheEntryInit(&shadingPatterns[0], 1, 8, 16, shading1Data);
  cacheEntryInit(&shadingPatterns[1], 2, 8, 8, shading2Data);
  cacheEntryInit(&shadingPatterns[2], 3, 8, 8, shading3Data);
  cacheEntryInit(&shadingPatterns[3], 4, 8, 8, shading4Data);
  cacheEntryInit(&shadingPatterns[4], 5, 8, 8, shading5Data);
  cacheEntryInit(&shadingPatterns[5], 6, 8, 8, shading6Data);
  cacheEntryInit(&shadingPatterns[6], 7, 8, 8, shading7Data);
  /* The 8th shading pattern is fully black, so no need to add anything as
  the default is fully black. */

  for (i = 0; i < 7; i ++) {
    if (! pcl5_id_cache_insert_pattern(id_cache, shadingPatterns[i].detail.numeric_id,
                                       &shadingPatterns[i], &new_pattern)) {
      return FALSE ;
    }
  }
  return TRUE ;
}

static Bool initCrossHatchPatterns(PCL5IdCache *id_cache)
{
  int16 i;
  pcl5_pattern *new_pattern ; /* We ignore as we don't need it. */

  cacheEntryInit(&crossHatchPatterns[0], 1, 16, 16, crossHatch1Data);
  cacheEntryInit(&crossHatchPatterns[1], 2, 16, 16, crossHatch2Data);
  cacheEntryInit(&crossHatchPatterns[2], 3, 16, 16, crossHatch3Data);
  cacheEntryInit(&crossHatchPatterns[3], 4, 16, 16, crossHatch4Data);
  cacheEntryInit(&crossHatchPatterns[4], 5, 16, 16, crossHatch5Data);
  cacheEntryInit(&crossHatchPatterns[5], 6, 16, 16, crossHatch6Data);

  for (i = 0; i < 6; i ++) {
    if (! pcl5_id_cache_insert_pattern(id_cache, crossHatchPatterns[i].detail.numeric_id,
                                       &crossHatchPatterns[i], &new_pattern)) {
      return FALSE ;
    }
  }
  return TRUE ;
}

Bool init_pattern_caches(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  if (! initShadingPatterns(pcl5_rip_context->resource_caches.shading))
    return FALSE ;
  if (! initCrossHatchPatterns(pcl5_rip_context->resource_caches.cross_hatch))
    return FALSE ;

  return TRUE ;
}

void destroy_pattern_caches(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  UNUSED_PARAM(PCL5_RIP_LifeTime_Context*, pcl5_rip_context) ;
  /* Currently a noop because cache tear down will cleanup for us. */
}

/* Log stripped */

