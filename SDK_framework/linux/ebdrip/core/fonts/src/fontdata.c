/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:fontdata.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Shared font data cache for charstring and map data. The font data cache is
 * an instance of the blob data cache.
 */

#include "core.h"
#include "coreinit.h"
#include "objnamer.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "mm.h"
#include "fileio.h"
#include "paths.h"
#include "fontdata.h"
#include "blobdata.h"
#include "bloberrors.h"

/* ----------------------------------------------------------------------- */

/* The font data cache is an instance of the blob data cache. */
blobdata_cache_t *font_data_cache ;

static Bool fontdata_swstart(struct SWSTART *params)
{
  static mm_cost_t cost = { memory_tier_disk, 3.0f };
  UNUSED_PARAM(struct SWSTART *, params) ;

  font_data_cache = blob_cache_init("font data cache",
                                    1024u * 1024u, /* 1 MB data limit before purging */
                                    FILEBUFFSIZE, /* Block size */
                                    FONTFILEBUFFSIZE, /* Block quantum */
                                    10, /* Trim limit. The value of this
                                           parameter is a pure guess; the
                                           most that would make sense would
                                           be 16, which would allow one
                                           full-size (32k) block for each
                                           font and still fit in the default
                                           font data cache limit. */
                                    cost,
                                    TRUE, /* mt-safe, renderers don't access */
                                    mm_pool_temp /* pool for allocating blocks */) ;

  return (font_data_cache != NULL) ;
}

static void fontdata_finish(void)
{
  blob_cache_destroy(&font_data_cache) ;
}

static void init_C_globals_fontdata(void)
{
  font_data_cache = NULL ;
}

void fontdata_C_globals(core_init_fns *fns)
{
  init_C_globals_fontdata() ;

  fns->swstart = fontdata_swstart ;
  fns->finish = fontdata_finish ;
}

/* ----------------------------------------------------------------------- */
/* A fontdata pointer is really just a blob map. For debuggability, we'll
   pretend the fontdata structure is a subclass of sw_blob_map. */
struct fontdata_t { /* DO NOT ADD ANY OTHER FIELDS TO THIS STRUCTURE. */
  sw_blob_map map ; /**< Blob map superclass, MUST be the ONLY entry. */
} ;

/* Open and close the use of a fontdata object. If fontdata_open succeeds,
   fontdata_close MUST be called. The fontdata methods pointer MUST be a
   pointer to static or global data; the methods may get called even after
   fontdata_close has been called. The fontdata cache may hold onto some data
   even over restores, if it can identify the data by a global mechanism. The
   pointer returned by fontdata_open is only valid between matched open and
   close methods, even if the data is retained. */
fontdata_t *fontdata_open(OBJECT *source, const blobdata_methods_t *methods)
{
  sw_blob_result result ;
  sw_blob_instance *blob ;
  sw_blob_map *map ;

  HQASSERT(font_data_cache != NULL, "Font data cache not initialised") ;
  HQASSERT(source, "No font data source") ;
  HQASSERT(methods, "No font data methods") ;
  HQASSERT(methods->same, "No font cache comparison method") ;
  HQASSERT(methods->open, "No font source open method") ;
  HQASSERT(methods->create, "No font source create method") ;

  if ( (result = blob_from_object_with_methods(source, SW_RDONLY|SW_FONT,
                                               font_data_cache, methods,
                                               &blob)) != SW_BLOB_OK ) {
    (void)error_handler(error_from_sw_blob_result(result)) ;
    return NULL ;
  }

  HQASSERT(blob->implementation, "No blob implementation") ;
  if ( (result = blob->implementation->map_open(blob, &map)) != SW_BLOB_OK ) {
    blob->implementation->close(&blob) ;
    (void)error_handler(error_from_sw_blob_result(result)) ;
    return NULL ;
  }

  /* Close blob, leaving only map reference active. */
  blob->implementation->close(&blob) ;

  /* fontdata is really just a blob map */
  return (fontdata_t *)map ;
}

void fontdata_close(fontdata_t **font_ptr)
{
  sw_blob_map *map ;

  HQASSERT(font_ptr, "No font map pointer") ;

  /* A font data pointer is really just a blob map */
  map = *(sw_blob_map **)font_ptr ;
  HQASSERT(map, "No font data map") ;
  HQASSERT(map->implementation, "No blob map implementation") ;

  map->implementation->map_close(&map) ;

  *font_ptr = NULL ;
}

/* ----------------------------------------------------------------------- */

/* TT and CFF clients have been known to ask for zero-length frames. In some
   cases, the logic to prevent asking for such frames is too tortuous, so
   provide a special null frame pointer back if requested. */
static uint8 nullframe[] = { '\0' } ;

/* This is the main routine used by consumers of font data. It provides
   access to a contiguous buffer ("frame") of data, starting at the specified
   offset and having a specified length. The frame of data persists until the
   fontdata_close routine is called. */
uint8 *fontdata_frame(fontdata_t *font_data, uint32 offset, uint32 length,
                      size_t alignment)
{
  Hq32x2 start ;
  sw_blob_result result ;
  uint8 *frame ;
  /* A font data reference is really just a blob map. */
  sw_blob_map *map = &font_data->map ;

  HQASSERT(map, "No font data map") ;
  HQASSERT(map->implementation, "No blob map implementation") ;

  if ( length == 0 )
    return nullframe ;

  Hq32x2FromUint32(&start, offset) ;

  if ( (result = map->implementation->map_region(map,
                                                 start, length, alignment,
                                                 &frame)) != SW_BLOB_OK ) {
    (void)error_handler(error_from_sw_blob_result(result)) ;
    return NULL ;
  }

  return frame ;
}

/* ----------------------------------------------------------------------- */
uint8 fontdata_protection(OBJECT *source, const blobdata_methods_t *methods)
{
  sw_blob_protection prot = PROTECTED_BLANKET ;
  sw_blob_result result ;
  sw_blob_instance *blob ;

  HQASSERT(font_data_cache != NULL, "Font data cache not initialised") ;
  HQASSERT(source, "No font data source") ;
  HQASSERT(methods, "No font data methods") ;
  HQASSERT(methods->same, "No font cache comparison method") ;
  HQASSERT(methods->open, "No font source open method") ;
  HQASSERT(methods->create, "No font source create method") ;

  if ( (result = blob_from_object_with_methods(source, SW_RDONLY|SW_FONT,
                                               font_data_cache, methods,
                                               &blob)) == SW_BLOB_OK ) {
    HQASSERT(blob, "No font data") ;
    HQASSERT(blob->implementation, "No font data implementation") ;

    result = blob->implementation->protection(blob, &prot) ;

    blob->implementation->close(&blob) ;
  }

  if ( result != SW_BLOB_OK )
    return FAILURE(PROTECTED_BLANKET) ;

  return CAST_TO_UINT8(prot) ;
}

/* ----------------------------------------------------------------------- */

/* Log stripped */
