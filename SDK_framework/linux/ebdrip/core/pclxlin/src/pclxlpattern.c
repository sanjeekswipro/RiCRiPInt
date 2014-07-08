/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlpattern.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Raster pattern and other raster operation functions
 */

#include "core.h"
#include "pclxlpattern.h"

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlimage.h"
#include "pclxlscan.h"

#include "mm.h"
#include "gstack.h"
#include "gs_color.h"
#include "graphics.h"
#include "hqmemcpy.h"
#include "gu_chan.h"

#define CACHE_HASHTABLE_SIZE 17
#define BITS_IN_BYTE 8

#define PATTERN_STRIDE(_width, _depth, _direct) \
  (((_direct) || (_depth == 8)) ? (_width) : ((_depth) == 4 ? (((_width) + 1) / 2) : (((_width) + 7) / 8)))

#define PATTERN_DATA_SIZE(_width, _height, _depth, _direct) \
  ((_direct) ? (sizeof(PclPackedColor) * (_width) * (_height)) : PATTERN_STRIDE((_width), (_depth), (_direct)) *(_height))


/**
 * Free the passed pattern.
 */
static void free_pattern(PCLXL_CACHED_PATTERN* pattern)
{
  HQASSERT(pattern != NULL, "pattern should not be null.");
  mm_free(pattern->pool, pattern, sizeof(PCLXL_CACHED_PATTERN) +
          PATTERN_DATA_SIZE(pattern->pattern.size.x,
                            pattern->pattern.size.y,
                            pattern->pattern.bits_per_pixel,
                            pattern->pattern.direct));
}

/* See header for doc. */
Bool pclxl_pattern_free_construction_state(PCLXL_PARSER_CONTEXT parser_context,
                                           Bool error)
{
  Bool status = TRUE;

  PCLXL_CONTEXT context = parser_context->pclxl_context;
  PCLXL_PATTERN_CONTRUCTION_STATE* state = &parser_context->pattern_construction;

  if (state->image_reader != NULL) {
    status = pclxl_raster_read_context_destroy(parser_context,
                                               &state->image_reader, !error);
  }

  if (state->expansion_line != NULL) {
    mm_free(context->memory_pool, state->expansion_line,
            state->expansion_line_size);
    state->expansion_line = NULL;
  }

  pixelPackerDelete(state->packer);
  state->packer = NULL;

  if (error) {
    free_pattern(state->cache_entry);
    state->cache_entry = NULL;
  }

  return status;
}

/**
 * Pattern cache destroy method; called automatically by the IdHashTable.
 */
static void pclxl_pattern_entry_destroy(IdHashTableEntry* entry)
{
  free_pattern((PCLXL_CACHED_PATTERN*)entry);
}

/**
 * Create a new IdHashTable for the pattern caching system.
 */
static IdHashTable* new_pattern_hashtable(PCLXL_CONTEXT context)
{
  return id_hashtable_create(context->memory_pool, CACHE_HASHTABLE_SIZE, FALSE,
                             pclxl_pattern_entry_destroy);
}

/**
 * Add the passed cache to the list of held caches.
 */
static Bool add_gstate_cache(PCLXL_CONTEXT context, IdHashTable* cache)
{
  PCLXL_PATTERN_CACHES* self = &context->pattern_caches;
  PCLXL_PATTERN_CACHE_LINK* link;

  link = mm_alloc(context->memory_pool, sizeof(PCLXL_PATTERN_CACHE_LINK),
                  MM_ALLOC_CLASS_PCLXL_PATTERN);
  if (link == NULL)
    return FALSE;

  link->cache = cache;
  link->next = self->gstate_caches;
  self->gstate_caches = link;
  return TRUE;
}

/**
 * Delete all stored gstate caches.
 */
static void destroy_old_gstate_caches(PCLXL_CONTEXT context)
{
  PCLXL_PATTERN_CACHES* self = &context->pattern_caches;
  PCLXL_PATTERN_CACHE_LINK* link = self->gstate_caches;

  while (link != NULL) {
    PCLXL_PATTERN_CACHE_LINK* next = link->next;
    id_hashtable_destroy(&link->cache);
    mm_free(context->memory_pool, link, sizeof(PCLXL_PATTERN_CACHE_LINK));
    link = next;
  }
  self->gstate_caches = NULL;
}

/* See header for doc. */
Bool pclxl_patterns_init(PCLXL_CONTEXT context)
{
  PCLXL_PATTERN_CACHES* self = &context->pattern_caches;
  PCLXL_PATTERN_CACHES init = {NULL};

  *self = init;

  self->session = new_pattern_hashtable(context);
  self->page = new_pattern_hashtable(context);
  /* Gstate caches are created separately on demand. */

  return self->session != NULL && self->page != NULL;
}

/* See header for doc. */
void pclxl_patterns_finish(PCLXL_CONTEXT context)
{
  PCLXL_PATTERN_CACHES* self = &context->pattern_caches;

  if (self->session != NULL)
    id_hashtable_destroy(&self->session);

  if (self->page != NULL)
    id_hashtable_destroy(&self->page);

  destroy_old_gstate_caches(context);
}

/* See header for doc. */
Bool pclxl_pattern_gstate_created(PCLXL_CONTEXT context,
                                  PCLXL_GRAPHICS_STATE gstate)
{
  gstate->pattern_cache = new_pattern_hashtable(context);
  return gstate->pattern_cache != NULL;
}

/* See header for doc. */
Bool pclxl_pattern_gstate_deleted(PCLXL_CONTEXT context,
                                  PCLXL_GRAPHICS_STATE gstate)
{
  if ( !gstate->pattern_cache )
    return TRUE;

  if (id_hashtable_empty(gstate->pattern_cache)) {
    /* The cache is empty, therefore we can safely delete it. */
    id_hashtable_destroy(&gstate->pattern_cache);
    return TRUE;
  }

  /* The cache is not empty, therefore display list objects may be referring
   * to the entries. Add the cache to the list for purging after rendering. */
  return add_gstate_cache(context, gstate->pattern_cache);
}

/**
 * Called at the end of a page.
 */
void pclxl_pattern_end_page(PCLXL_CONTEXT context)
{
  PCLXL_PATTERN_CACHES* self = &context->pattern_caches;

  id_hashtable_remove_all(self->page, TRUE);
}

/**
 * Called at the end of a session.
 */
void pclxl_pattern_end_session(PCLXL_CONTEXT context)
{
  PCLXL_PATTERN_CACHES* self = &context->pattern_caches;

  id_hashtable_remove_all(self->session, TRUE);
}

/* See header for doc. */
void pclxl_pattern_rendering_complete(PCLXL_CONTEXT context)
{
  PCLXL_PATTERN_CACHES* self = &context->pattern_caches;

  id_hashtable_kill_zombies(self->session);
  id_hashtable_kill_zombies(self->page);
  destroy_old_gstate_caches(context);
}


/**
 * Color-convert the passed line of pattern data. This will use the image color
 * chain to perform the conversion.  This is for direct patterns.  For indirect
 * patterns we now pass the palette to the core along with the pattern data.
 */
static Bool color_convert_pattern_line(PclXLPattern* pattern,
                                       PCLXL_IMAGE_READ_CONTEXT* image_reader,
                                       uint8* inputLine,
                                       PclPackedColor* outputLine)
{
  int32 x;
  USERVALUE color[3];
  COLORVALUE converted[4];
  USERVALUE factor = (1.0f / 255.0f);
  DEVICESPACEID dspace ;

  /* The colorspace used by gsc_invokeChainBlock below. */
  guc_deviceColorSpace(gsc_getRS(gstateptr->colorInfo), &dspace, NULL) ;

  HQASSERT(pattern != NULL, "pattern should not be null.");
  HQASSERT(pattern->direct, "pattern should not be indexed.");

  switch (image_reader->num_components) {
  case 1:
    for (x = 0; x < pattern->size.x; x ++) {
      color[0] = inputLine[x] * factor;
      if (! gsc_invokeChainBlock(gstateptr->colorInfo, GSC_IMAGE, color,
                                 converted, 1)) {
        return FALSE;
      }
      if (dspace == DEVICESPACE_RGB) {
        outputLine[x] = PCL_PACK_RGB(converted);
      } else if (dspace == DEVICESPACE_CMYK) {
        outputLine[x] = PCL_PACK_CMYK(converted);
      } else {
        HQASSERT(dspace == DEVICESPACE_Gray, "Unsupported device colorspace") ;
        converted[2] = converted[1] = converted[0] ;
        outputLine[x] = PCL_PACK_RGB(converted);
      }
    }
    break;

  case 3:
    for (x = 0; x < pattern->size.x; x ++) {
      int i = x * 3;
      color[0] = inputLine[i] * factor;
      color[1] = inputLine[i + 1] * factor;
      color[2] = inputLine[i + 2] * factor;
      if (! gsc_invokeChainBlock(gstateptr->colorInfo, GSC_IMAGE, color,
                                 converted, 1)) {
        return FALSE;
      }
      if (dspace == DEVICESPACE_RGB) {
        outputLine[x] = PCL_PACK_RGB(converted);
      } else if (dspace == DEVICESPACE_CMYK) {
        outputLine[x] = PCL_PACK_CMYK(converted);
      } else {
        HQASSERT(dspace == DEVICESPACE_Gray, "Unsupported device colorspace") ;
        converted[2] = converted[1] = converted[0] ;
        outputLine[x] = PCL_PACK_RGB(converted);
      }
    }
    break;
  }
  return TRUE;
}

/**
 * Read a block of raster data into the current pattern.
 */
static Bool read_pattern_block(PCLXL_PARSER_CONTEXT parser_context,
                               PCLXL_IMAGE_READ_CONTEXT* image_reader)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_PATTERN_CONTRUCTION_STATE* state = &parser_context->pattern_construction;
  PclXLPattern* pattern = &state->cache_entry->pattern;
  PixelPackFunction packFunc = NULL;
  PclPackedColor* direct_dest = NULL;
  uint8* indexed_dest = NULL;
  uint8* line = state->expansion_line;
  int32 block_height = image_reader->block_height;
  uint32 lineBytes;
  int32 y;

  if (image_reader->start_line > pattern->size.y) {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_KERNEL,
                               PCLXL_FAIL,
                               ("Image block start out of range."));
    return FALSE;
  }

  /* Set the current colorspace if we will be using it to convert the pattern
     to final colors.  If the pattern is indexed, delay doing this as the
     palette may not yet be defined or could be changed.

     N.B. It has not been tested whether the colorspace for a direct pattern
          could change at a later stage between PCLXL_eRGB and PCLXL_eSRGB,
          (though this may make no difference).  Changes between other pairs
          of colorspaces do not appear possible as the reference printer checks
          whether it has the right amount of data during ReadRastPattern.
  */

  if (pattern->direct) {
    PCLXL_ColorSpace cspace = graphics_state->color_space_details->color_space ;
    /* It's a direct pattern, not indexed. Don't use
       pclxl_ps_set_colorspace_explicit() because it will set up an indexed
       space if there is a current palette. */
    if ( !gsc_setcolorspacedirect(gstateptr->colorInfo, GSC_IMAGE,
                                  cspace == PCLXL_eGray
                                  ? SPACE_DeviceGray
#ifdef DEBUG_BUILD
                                  : cspace == PCLXL_eCMYK
                                  ? SPACE_DeviceCMYK
#endif
                                  : SPACE_DeviceRGB) )
      return FALSE ;

    /* Direct patterns are expanded up to 8 bits */
    packFunc = pixelPackerGetPacker(state->packer);
  }

  /* Don't read more lines than we need. */
  if (image_reader->start_line + block_height > pattern->size.y) {
    block_height = pattern->size.y - image_reader->start_line;
  }

  /* For direct patterns this equates to size.x */
  lineBytes = image_reader->num_components * pattern->stride;

  for (y = 0; y < image_reader->block_height; y ++) {

    if (pattern->direct)
      direct_dest = pattern->data.pixels +
                     (pattern->size.x * (y + image_reader->start_line));
    else
      indexed_dest = pattern->data.indices +
                     (pattern->stride * (y + image_reader->start_line));

    if (! pclxl_raster_decode_block_segment(parser_context, image_reader))
      return FALSE;

    if (image_reader->uncompressed_bytes == 0) {
      /* Failed to decompress any more data; abort. */
      break;
    }
    HQASSERT(image_reader->uncompressed_bytes == image_reader->width_in_bytes,
             "Expected line of pattern data.");

    /* Convert direct patterns to their final colors now. */
    if (pattern->direct) {
      packFunc(state->packer, image_reader->start_of_string, line, lineBytes);
      if (! color_convert_pattern_line(pattern, image_reader, line, direct_dest))
        return FALSE;
      pattern->bits_per_pixel = 8;
    }
    else
      HqMemCpy(indexed_dest, image_reader->start_of_string, lineBytes);
  }

  return TRUE;
}

/*
 * Tag 0xb3 BeginRasterPattern
 */

Bool pclxl_op_begin_rast_pattern(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[8] = {
#define BEGINRASTPATTERN_COLOR_MAPPING      (0)
    {PCLXL_AT_ColorMapping | PCLXL_ATTR_REQUIRED},
#define BEGINRASTPATTERN_COLOR_DEPTH        (1)
    {PCLXL_AT_ColorDepth | PCLXL_ATTR_REQUIRED},
#define BEGINRASTPATTERN_SOURCE_WIDTH       (2)
    {PCLXL_AT_SourceWidth | PCLXL_ATTR_REQUIRED},
#define BEGINRASTPATTERN_SOURCE_HEIGHT      (3)
    {PCLXL_AT_SourceHeight | PCLXL_ATTR_REQUIRED},
#define BEGINRASTPATTERN_DESTINATION_SIZE   (4)
    {PCLXL_AT_DestinationSize | PCLXL_ATTR_REQUIRED},
#define BEGINRASTPATTERN_PATTERN_DEFINE_ID  (5)
    {PCLXL_AT_PatternDefineID | PCLXL_ATTR_REQUIRED},
#define BEGINRASTPATTERN_PERSISTANCE        (6)
    {PCLXL_AT_PatternPersistence | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION valid_color_mapping[] = {
    PCLXL_eDirectPixel,
    PCLXL_eIndexedPixel,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION valid_color_depth[] = {
    PCLXL_e1Bit,
    PCLXL_e4Bit,
    PCLXL_e8Bit,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION valid_pattern_persist[] = {
    PCLXL_eTempPattern,
    PCLXL_ePagePattern,
    PCLXL_eSessionPattern,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT context = parser_context->pclxl_context;
  PCLXL_ENUMERATION color_mapping, color_depth, pattern_persist;
  Bool direct;
  int32 source_width, source_height, pattern_id;
  int32 data_size;
  int32 bits_per_pixel;
  int32 num_components;
  PCLXL_SysVal_XY destination_size;
  PCLXL_ColorSpace color_space = pclxl_get_colorspace(context->graphics_state);
  PCLXL_CACHED_PATTERN* cached;
  PclXLPattern pattern_init = {0};
  PCLXL_PATTERN_CONTRUCTION_STATE* in_construction = &parser_context->pattern_construction;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* ColorMapping */
  if ( !pclxl_attr_match_enumeration(match[BEGINRASTPATTERN_COLOR_MAPPING].result,
                                     valid_color_mapping, &color_mapping, context,
                                     PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }
  /* ColorDepth */
  if ( !pclxl_attr_match_enumeration(match[BEGINRASTPATTERN_COLOR_DEPTH].result,
                                     valid_color_depth, &color_depth, context,
                                     PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }
  /* SourceWidth */
  source_width = pclxl_attr_get_int(match[BEGINRASTPATTERN_SOURCE_WIDTH].result);
  if ( source_width < 1 ) {
    PCLXL_ERROR_HANDLER(context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Pattern width < 1"));
    return(FALSE);
  }
  /* SourceHeight */
  source_height = pclxl_attr_get_int(match[BEGINRASTPATTERN_SOURCE_HEIGHT].result);
  if ( source_height < 1 ) {
    PCLXL_ERROR_HANDLER(context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Pattern height < 1"));
    return(FALSE);
  }
  /* DestinationSize */
  pclxl_attr_get_real_xy(match[BEGINRASTPATTERN_DESTINATION_SIZE].result, &destination_size);
  if ( (destination_size.x == 0) || (destination_size.y == 0)) {
    PCLXL_ERROR_HANDLER(context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Pattern destination width or height is zero"));
    return(FALSE);
  }
  /* PatternDefineID */
  pattern_id = pclxl_attr_get_int(match[BEGINRASTPATTERN_PATTERN_DEFINE_ID].result);
  /* PatternPersistence */
  if ( !pclxl_attr_match_enumeration(match[BEGINRASTPATTERN_PERSISTANCE].result,
                                     valid_pattern_persist, &pattern_persist, context,
                                     PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  direct = (color_mapping == PCLXL_eDirectPixel);
  bits_per_pixel = pclxl_color_depth_bits(color_depth);
  data_size = PATTERN_DATA_SIZE(source_width, source_height, bits_per_pixel, direct);

  /* Allocate new cache entry. */
  cached = mm_alloc(context->memory_pool,
                    sizeof(PCLXL_CACHED_PATTERN) + data_size,
                    MM_ALLOC_CLASS_PCLXL_PATTERN);
  if (cached == NULL) {
    PCLXL_ERROR_HANDLER(context, PCLXL_SS_KERNEL, PCLXL_INSUFFICIENT_MEMORY,
                        ("Out of memory."));
    return(FALSE);
  }
  /* Zero init. */
  cached->pattern = pattern_init;

  cached->pool = context->memory_pool;
  SETXY(cached->pattern.size, source_width, source_height);
  SETXY(cached->pattern.targetSize, (int32)destination_size.x,
        (int32)destination_size.y);
  cached->pattern.direct = direct;

  cached->pattern.stride = PATTERN_STRIDE(source_width, bits_per_pixel, direct);
  cached->pattern.bits_per_pixel = bits_per_pixel;

  if (cached->pattern.direct)
    cached->pattern.data.pixels = (PclPackedColor*)(cached + 1);
  else
    cached->pattern.data.indices = (uint8*)(cached +1);


  /* Prepare raster reading code for new raster blocks. Note that we pass
   * destination_size here, but it will not be used as we're not drawing an
   * actual image. */
  if (! pclxl_raster_read_context_create(parser_context,
                                         &in_construction->image_reader,
                                         color_mapping, color_depth,
                                         source_width, source_height,
                                         destination_size, FALSE)) {
    free_pattern(cached);
    return FALSE;
  }

  in_construction->id = pattern_id;
  in_construction->persist = pattern_persist;
  in_construction->cache_entry = cached;

  /* N.B. Indirect patterns are stored in an unconverted form */
  if (cached->pattern.direct) {
    /* Create a buffer for normalised 8-bit pixel data. */
    num_components = pclxl_color_space_num_components(color_space);
    in_construction->expansion_line_size = source_width * num_components;
    in_construction->expansion_line = mm_alloc(context->memory_pool,
                                               in_construction->expansion_line_size,
                                               MM_ALLOC_CLASS_PCLXL_PATTERN);
    if (in_construction->expansion_line == NULL) {
      pclxl_pattern_free_construction_state(parser_context, TRUE);
      return FALSE;
    }

    /* Create a PixelPacker to convert from the source bits per component to 8
     * bits. */
    in_construction->packer =
      pixelPackerNew(in_construction->image_reader->bits_per_component, 8, 1);

    if (in_construction->packer == NULL) {
      pclxl_pattern_free_construction_state(parser_context, TRUE);
      return FALSE;
    }
  }

  return TRUE;
}

/*
 * Tag 0xb4 ReadRastPattern
 */

Bool pclxl_op_read_rast_pattern(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[6] = {
#define READRASTPATTERN_COMPRESS_MODE       (0)
    {PCLXL_AT_CompressMode | PCLXL_ATTR_REQUIRED},
#define READRASTPATTERN_START_LINE          (1)
    {PCLXL_AT_StartLine | PCLXL_ATTR_REQUIRED},
#define READRASTPATTERN_BLOCK_HEIGHT        (2)
    {PCLXL_AT_BlockHeight | PCLXL_ATTR_REQUIRED},
#define READRASTPATTERN_PAD_BYTES_MULTIPLE  (3)
    {PCLXL_AT_PadBytesMultiple},
#define READRASTPATTERN_BLOCK_BYTE_LENGTH   (4)
    {PCLXL_AT_BlockByteLength},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION valid_compress_mode[] = {
    PCLXL_eNoCompression,
    PCLXL_eRLECompression,
    PCLXL_eJPEGCompression,
    PCLXL_eDeltaRowCompression,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_IMAGE_READ_CONTEXT* image_reader = parser_context->pattern_construction.image_reader;
  int32 block_height, start_line;
  int32 pad_bytes;
  uint32 data_size;
  PCLXL_ENUMERATION compress_mode;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    (void)pclxl_pattern_free_construction_state(parser_context, TRUE);
    return(FALSE);
  }

  /* CompressMode */
  if ( !pclxl_attr_match_enumeration(match[READRASTPATTERN_COMPRESS_MODE].result,
                                     valid_compress_mode, &compress_mode, pclxl_context,
                                     PCLXL_SS_KERNEL) ) {
    (void)pclxl_pattern_free_construction_state(parser_context, TRUE);
    return(FALSE);
  }
  /* StartLine */
  start_line = pclxl_attr_get_int(match[READRASTPATTERN_START_LINE].result);
  /* BlockHeight */
  block_height = pclxl_attr_get_int(match[READRASTPATTERN_BLOCK_HEIGHT].result);
  /* PadBytesMultiple */
  pad_bytes = 4;
  if ( match[READRASTPATTERN_PAD_BYTES_MULTIPLE].result ) {
    pad_bytes = pclxl_attr_get_int(match[READRASTPATTERN_PAD_BYTES_MULTIPLE].result);
  }
  /** \todo check, was 0-4 doc says 1-255! */
  if ( (pad_bytes < 1) || (pad_bytes > 255) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_IMAGE, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Image data pad value out of range [0,4]"));
    (void)pclxl_pattern_free_construction_state(parser_context, TRUE);
    return(FALSE);
  }
  /* BlockByteLength */
  if ( match[READRASTPATTERN_BLOCK_BYTE_LENGTH].result ) {
    /* Note - this is overwritten below anyway */
    image_reader->embedded_data_len =
      CAST_UNSIGNED_TO_INT32(pclxl_attr_get_uint(match[READRASTPATTERN_BLOCK_BYTE_LENGTH].result));
  }

  /* Init the data size. */
  if ( !pclxl_stream_read_data_length(pclxl_context,
                                      pclxl_parser_current_stream(parser_context),
                                      &data_size) ) {
    (void)pclxl_pattern_free_construction_state(parser_context, TRUE);
    return FALSE ;
  }
  image_reader->embedded_data_len = CAST_UNSIGNED_TO_INT32(data_size);

  if (compress_mode == PCLXL_eJPEGCompression)
    compress_mode = PCLXL_eJPEGCompressionForPattern ;

  pclxl_raster_read_context_block_init(parser_context,
                                       image_reader,
                                       start_line, block_height,
                                       compress_mode, pad_bytes);

  if (! read_pattern_block(parser_context, image_reader)) {
    (void)pclxl_pattern_free_construction_state(parser_context, TRUE);
    return FALSE ;
  }
  return TRUE;
}

/*
 * Tag 0xb5 EndRastPattern
 */

Bool pclxl_op_end_rast_pattern(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT context = parser_context->pclxl_context;
  PCLXL_PATTERN_CACHES* caches = &context->pattern_caches;
  PCLXL_PATTERN_CONTRUCTION_STATE* state = &parser_context->pattern_construction;
  IdHashTable* cache = NULL;

  HQASSERT(state->cache_entry != NULL, "No pattern to cache.");

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, context,
                                   PCLXL_SS_KERNEL) ) {
    (void)pclxl_pattern_free_construction_state(parser_context, FALSE);
    return(FALSE);
  }

  /* Free objects/memory used during pattern construction. */
  if ( !pclxl_pattern_free_construction_state(parser_context, FALSE) ) {
    return(FALSE);
  }

  /* Add the pattern to the appropriate cache. */
  switch (state->persist) {
    default:
      HQFAIL("Pattern lifetime unknown - defaulting to temporary.");
      /* FALLTHROUGH */
    case PCLXL_eTempPattern:
      cache = context->graphics_state->pattern_cache;
      break;

    case PCLXL_ePagePattern:
      cache = caches->page;
      break;

    case PCLXL_eSessionPattern:
      cache = caches->session;
      break;
  }

  id_hashtable_insert(cache, &state->cache_entry->hash_entry, state->id);
  state->cache_entry = NULL;

  return TRUE;
}

PclXLPattern* pclxl_pattern_find(PCLXL_CONTEXT context, int32 id)
{
  PCLXL_GRAPHICS_STATE graphics_state = context->graphics_state;
  PCLXL_PATTERN_CACHES* caches = &context->pattern_caches;
  PCLXL_CACHED_PATTERN *cached;

  for ( ; graphics_state; graphics_state = graphics_state->parent_graphics_state ) {
    cached = (PCLXL_CACHED_PATTERN*)id_hashtable_find(graphics_state->pattern_cache, id);
    if ( cached )
      return &cached->pattern;
  }

  cached = (PCLXL_CACHED_PATTERN*)id_hashtable_find(caches->page, id);
  if ( cached )
    return &cached->pattern;

  cached = (PCLXL_CACHED_PATTERN*)id_hashtable_find(caches->session, id);
  if ( cached )
    return &cached->pattern;

  return NULL;
}

/******************************************************************************
* Log stripped */
