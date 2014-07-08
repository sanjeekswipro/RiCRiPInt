/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:printmodel.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Print Model" category.
 */

#include "core.h"
#include "printmodel.h"

#include "pcl5context.h"
#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"
#include "pclutils.h"
#include "cursorpos.h"
#include "pclGstate.h"
#include "factorypatterns.h"
#include "gstack.h"
#include "graphics.h"
#include "ndisplay.h"
#include "pcl5metrics.h"
#include "params.h"
#include "resourcecache.h"
#include "hqmemset.h"

/* Values for print model source/pattern transparency */
#define PCL5_TRANSPARENT 0
#define PCL5_OPAQUE 1

/**
 * The global cached palette, used to communicate the current palette to the
 * core.
 */
Pcl5CachedPalette cached_palette;

/* See header for doc. */
PrintModelInfo* get_print_model(PCL5Context* pcl5_ctxt)
{
  return &pcl5_ctxt->print_state->mpe->print_model;
}

/**
 * Update the cached palette to match the current PCL palette.
 */
Bool update_cached_palette(PCL5Context* pcl5_ctxt, Bool hpgl)
{
  ColorPalette* pcl_palette = get_active_palette(pcl5_ctxt);
  DL_STATE *page;
  uint32 i;

  cached_palette.uid ++;
  cached_palette.size = ENTRIES_IN_PALETTE(pcl_palette);

  HQASSERT(cached_palette.size <= 256, "Exceeded max size of cached palette");

  page = pcl5_ctxt->corecontext->page;

  for ( i = 0; i < cached_palette.size; ++i ) {
    if ( !set_ps_color_from_palette_index(pcl5_ctxt, i) ||
         !pclPackCurrentColor(page, &cached_palette.colors[i]) )
      return FALSE;
  }

  /* Set the current color in the core since we've messed with it. */
  return hpgl || (set_ps_colorspace(pcl5_ctxt) && set_ps_color(pcl5_ctxt));
}

/* See header for doc. */
pcl5_resource_numeric_id shading_to_pattern_id(pcl5_resource_numeric_id id)
{
  if (id == 0)
    return 0;
  if (IN_RANGE(id, 1, 2))
    return 1;
  if (IN_RANGE(id, 3, 10))
    return 2;
  if (IN_RANGE(id, 11, 20))
    return 3;
  if (IN_RANGE(id, 21, 35))
    return 4;
  if (IN_RANGE(id, 36, 55))
    return 5;
  if (IN_RANGE(id, 56, 80))
    return 6;
  if (IN_RANGE(id, 81, 99))
    return 7;

  return 8;
}

/**
 * Return the angle by which a pattern should be rotated, taking into account
 * the media orientation, (which is the leading edge), the page orientation,
 * and (if includePrintDirection is true) print direction.
 */
uint32 get_pattern_rotation(PCL5Context* pcl5_ctxt,
                            Bool includePrintDirection)
{
  PageCtrlInfo* page_control = get_page_ctrl_info(pcl5_ctxt);
  PCL5PrintState *print_state = pcl5_ctxt->print_state ;
  uint32 angle = ((print_state->media_orientation + page_control->orientation) %4) * 90;

  if (includePrintDirection) {
    angle = (angle + page_control->print_direction) % 360;
  }
  return angle;
}

/* See header for doc. */
void set_current_pattern_with_id(PCL5Context* pcl5_ctxt,
                                 pcl5_resource_numeric_id id,
                                 uint32 type_override)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);
  PCL5ResourceCaches *caches = &pcl5_ctxt->resource_caches;
  uint32 dpi = ctm_get_device_dpi(get_pcl5_ctms(pcl5_ctxt));
  pcl5_pattern *cache_entry = NULL;

  IPOINT origin = ctm_transform(pcl5_ctxt,
                                self->pattern_origin.x,
                                self->pattern_origin.y);

  if (type_override == PCL5_CURRENT_PATTERN)
    type_override = self->current_pattern_type;

  /* Note that setting a pattern of NULL is the same as setting a solid-black
   * pattern. Solid white is treated specially, and therefore we must use the
   * solidWhitePattern global to activate this special behavior. */
  switch (type_override) {
    case PCL5_SOLID_FOREGROUND:
      /* Solid black is the default. */
      setPcl5Pattern(NULL, dpi, 0, &origin, NULL);
      break;

    case PCL5_ERASE_PATTERN:
      setPcl5Pattern(&erasePattern, dpi, 0, &origin, NULL);
      break;

    case PCL5_WHITE_PATTERN:
      setPcl5Pattern(&whitePattern, dpi, 0, &origin, NULL);
      break;

    case PCL5_SHADING_PATTERN:
      id = shading_to_pattern_id(id);

      if (id == 0) {
        /* This is what the printer appears to do */
        setPcl5Pattern(&whitePattern, dpi, 0, &origin, NULL);
      }
      else {
        /* This also means that the solid black pattern will be used for Id
         * values greater than 100, as well as 100, which is what the printer
         * appears to do.
         */
        setPcl5Pattern(pcl5_id_cache_get_pattern(caches->shading, id), dpi, 0,
                       &origin, NULL);
      }
      break;

    case PCL5_CROSS_HATCH_PATTERN:
    case PCL5_USER_PATTERN: {
      uint32 angle = 0;

      /* User defined pattern. */
      if (type_override == 3)
        cache_entry = pcl5_id_cache_get_pattern(caches->cross_hatch, id);
      else {
        cache_entry = pcl5_id_cache_get_pattern(caches->user, id);
      }

      if (cache_entry == NULL) {
        cache_entry = &invalidPattern;
      }
      else {
        if (cache_entry->data == NULL) {
          /* This entry was solid black, and is therefore the same as the
           * default pattern. */
          cache_entry = NULL;
        }
        else {
          angle = get_pattern_rotation(pcl5_ctxt,
                                       self->patterns_follow_print_direction);
          if (cache_entry->color) {
            if (! update_cached_palette(pcl5_ctxt, FALSE)) {
              HQFAIL("Failed to update cached palette.");
              return;
            }
          }
        }
      }
      setPcl5Pattern(cache_entry, dpi, angle, &origin, &cached_palette);
      break;
    }
  }
}

/* See header for doc. */
void set_current_pattern(PCL5Context* pcl5_ctxt, uint32 type_override)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);

  set_current_pattern_with_id(pcl5_ctxt, self->current_pattern_id, type_override);
}

/* See header for doc. */
uint32 get_current_pattern_type(PCL5Context* pcl5_ctxt)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);
  return self->current_pattern_type;
}

/**
 * Set the current pattern origin.
 *
 * \param pcl5_ctxt PCL5 context.
 * \param pos       Reference point in PCL5 Coordinates.
 */
void set_pattern_origin(PCL5Context* pcl5_ctxt, CursorPosition* pos)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);
  self->pattern_origin = *pos;
}

/**
 * Return the current pattern origin.
 */
CursorPosition get_pattern_origin(PCL5Context* pcl5_ctxt)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);
  return self->pattern_origin;
}

/* Implementation for reinstate_pcl5_print_model(). */
static void reinstate_pcl5_print_model_internal(PrintModelInfo* self)
{
  setPclSourceTransparent(self->source_transparent);
  setPclPatternTransparent(self->pattern_transparent);
  setPclRop((uint8)self->rop);
}

/* See header for doc. */
void reinstate_pcl5_print_model(PCL5Context *pcl5_ctxt)
{
  reinstate_pcl5_print_model_internal(get_print_model(pcl5_ctxt));
}

/* See header for doc. */
void default_print_model(PrintModelInfo* self)
{
  IPOINT origin = {0, 0};
  self->source_transparent = TRUE;
  self->pattern_transparent = TRUE;
  self->rop = PCL_ROP_TSo;
  self->current_pattern_type = PCL5_SOLID_FOREGROUND;
  self->current_pattern_id = self->pending_pattern_id = 0;
  self->pattern_origin.x = 0;
  self->pattern_origin.y = 0;
  self->patterns_follow_print_direction = TRUE;

  set_pixel_placement(self, PCL5_INTERSECTION_CENTERED);

  reinstate_pcl5_print_model_internal(self);
  setPcl5Pattern(NULL, 300, 0, &origin, NULL);
}

/* See header for doc. */
void save_pcl5_print_model(PCL5Context *pcl5_ctxt,
                           PrintModelInfo *to,
                           PrintModelInfo *from,
                           Bool overlay)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(PrintModelInfo*, from) ;

  HQASSERT(to != NULL, "PrintModelInfo is NULL") ;

  if (overlay) {
    /* Keep the PS state and any C statics in line with MPE */
    reinstate_pcl5_print_model_internal(to) ;
    set_pixel_placement(to, to->pixel_placement) ;
  }
}

/* See header for doc. */
void restore_pcl5_print_model(PCL5Context *pcl5_ctxt,
                              PrintModelInfo *to,
                              PrintModelInfo *from)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(PrintModelInfo*, from) ;

  HQASSERT(to != NULL, "PrintModelInfo is NULL") ;

  /* Keep the PS state and any C statics in line with MPE */
  /** \todo Can this happen while HPGL is in effect? */
  reinstate_pcl5_print_model_internal(to) ;
  set_pixel_placement(to, to->pixel_placement) ;
}

/* Init method. */
void pcl5_print_model_init(PCL5Context *pcl5_ctxt)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt);

  cached_palette.uid = 0;
}

/* Finish method. */
void pcl5_print_model_finish(PCL5Context *pcl5_ctxt)
{
  IPOINT origin = {0, 0};

  /* Remove any current pattern reference. */
  setPcl5Pattern(NULL, 300, 0, &origin, NULL);

  /* Delete all temporary patterns in user pattern the cache. */
  pcl5_id_cache_remove_all(pcl5_ctxt->resource_caches.user, FALSE);
  pcl5_id_cache_remove_all(pcl5_ctxt->resource_caches.hpgl2_user, FALSE);
}


/* See header for doc. */
void set_default_pattern_reference_point(PCL5Context *pcl5_ctxt)
{
  PageCtrlInfo *page_info ;
  CursorPosition cursor ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* So far it appears that apart from state creation and reset, this is only
   * defaulted on a page size or orientation command.  We expect the
   * printdirection to be zero at all these times, (and hence there is no
   * issue over which top margin, etc, to take).
   */
  HQASSERT(page_info->print_direction == 0,
           "Expected a zero printdirection") ;

  /* The spec clearly states that the default pattern reference point is at the
   * (0, 0) cursor position. However, experimentation shows that it is at the
   * top left of the logical page, (regardless of whether patterns rotate with
   * printdirection).
   */
  cursor.x = 0 ;
  cursor.y = - (PCL5Real) page_info->top_margin ;

  set_pattern_origin(pcl5_ctxt, &cursor);
}

/* See header for doc. */
void set_current_rop(PCL5Context *pcl5_ctxt, uint32 rop)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);
  struct PCL5PrintState* print_state = pcl5_ctxt->print_state;

  HQASSERT(rop <= 255, "Invalid rop code.");

  self->rop = rop;

  if (rop != getPclRop()) {
    print_state->setg_required += 1;
    setPclRop((uint8)self->rop);
  }
}

/**
 * Source Transparency Mode Command
 */
Bool pcl5op_star_v_N(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign);

  if (IN_RANGE(value.integer, 0, 1)) {
    self->source_transparent = (value.integer == PCL5_TRANSPARENT);

    if (self->source_transparent != isPclSourceTransparent()) {
      struct PCL5PrintState* print_state = pcl5_ctxt->print_state ;
      print_state->setg_required += 1;
      setPclSourceTransparent(self->source_transparent);
    }
  }

  return TRUE;
}

/**
 * Pattern Transparency Mode Command
 */
Bool pcl5op_star_v_O(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign);

  if (IN_RANGE(value.integer, 0, 1)) {
    self->pattern_transparent = (value.integer == PCL5_TRANSPARENT);

    if (self->pattern_transparent != isPclPatternTransparent()) {
      struct PCL5PrintState* print_state = pcl5_ctxt->print_state ;
      print_state->setg_required += 1;
      setPclPatternTransparent(self->pattern_transparent);
    }
  }

  return TRUE;
}

/**
 * Select Current Pattern Command
 *
 * This sets the type of the current pattern, e.g. solid white, shading pattern,
 * user pattern, etc.
 */
Bool pcl5op_star_v_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);
  PCL5ResourceCaches *caches = &pcl5_ctxt->resource_caches;

  /* N.B. Printer treats negative values as positive */
  uint32 pattern_type = abs(value.integer);

  UNUSED_PARAM(int32, explicit_sign);

  if (!IN_RANGE(pattern_type, 0, 4))
    return TRUE;

  /* This is where the printer applies some range checking using the pending
   * pattern ID, (though apparently not for shading patterns - perhaps as
   * something fairly sensible can be done later for these).
   */
  switch (pattern_type) {
    case PCL5_CROSS_HATCH_PATTERN:
      if (! IN_RANGE(self->pending_pattern_id, 1, 6))
        return TRUE;
      break;

    case PCL5_USER_PATTERN:
      /* Check whether the pattern exists */
      if (pcl5_id_cache_get_pattern(caches->user, self->pending_pattern_id) == NULL)
        return TRUE;
      break;

    default:
      break;
  }

  self->current_pattern_type = pattern_type;

  /* Update the current pattern from the pending pattern. */
  self->current_pattern_id = self->pending_pattern_id;

  return TRUE;
}

/**
 * Pattern ID (Area Fill ID) Command
 */
Bool pcl5op_star_c_G(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);

  UNUSED_PARAM(int32, explicit_sign);

  /* The printer treats negative values as positive and limits the value
   * to the range.
   *
   * Note that we cannot apply range checking based on the current pattern type
   * at this point - that may be changed before anything actually uses the pattern.
   */
  value.integer = (int32) pcl5_limit_to_range(value.integer, -32767, 32767);
  self->pending_pattern_id = (pcl5_resource_numeric_id) abs(value.integer);

  return TRUE;
}

/**
 * Returns true if the passed pattern is solid black (and can therefore be
 * ignored).
 */
Bool pattern_is_black(pcl5_pattern *pattern)
{
  int32 x, y;

  if (pattern->color)
    return FALSE;

  HQASSERT(pattern->bits_per_pixel == 1 || pattern->bits_per_pixel == 8,
           "Unexpected bit depth.");

  for (y = 0; y < pattern->height; y ++) {
    uint8* line = pattern->data + (y * pattern->stride);

    if (pattern->bits_per_pixel == 1) {
      for (x = 0; x < pattern->width; x ++) {
        uint8 byte = line[x >> 3];
        if (((byte >> (7 - (x % 8))) & 1) == 0)
          return FALSE;
      }
    }
    else {
      for (x = 0; x < pattern->width; x ++) {
        if (line[x] == 0)
          return FALSE;
      }
    }
  }
  return TRUE;
}

/**
 * Download Pattern Command
 */
Bool pcl5op_star_c_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  uint32 bytesLeft;
  uint32 dataSize, patternBytesToRead;
  uint8 header[12];
  pcl5_pattern *new_entry;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  bytesLeft = min(value.integer, 32767);
  if ( bytesLeft == 0 ) {
    return(TRUE);
  }

  if (bytesLeft < 8) {
    /* Invalid header - ignore command. */
    return(file_skip(pcl5_ctxt->flptr, bytesLeft, NULL) > 0);
  }

  /* The pattern header can take two forms; 8 bytes or 12. Read 8 bytes and
  try to figure out what we've got. */
  if ( file_read(pcl5_ctxt->flptr, header, 8, NULL) <= 0 ) {
    return(FALSE);
  }
  bytesLeft -= 8;

  /* Create template entry within code block to ensure its not used by
     mistake once the allocated entry has been obtained. */
  {
    pcl5_pattern entry;
    PrintModelInfo* self = get_print_model(pcl5_ctxt);

    entry.detail.resource_type = SW_PCL5_PATTERN;
    entry.detail.numeric_id = self->pending_pattern_id;
    entry.detail.permanent = FALSE;
    entry.detail.private_data = NULL;
    entry.detail.PCL5FreePrivateData = NULL;
    entry.detail.device = NULL;
    entry.highest_pen = 0;

    if (header[0] == 1) {
      /* Color pattern. */
      if ( !pcl5_ctxt->pcl5c_enabled ) {
        /* We can't handle color patterns in 5e mode; ignore this pattern
         * definition. */
        (void)file_skip(pcl5_ctxt->flptr, bytesLeft, NULL);
        return TRUE;
      }
      entry.color = TRUE;
      entry.bits_per_pixel = header[2];
      if (entry.bits_per_pixel != 1 && entry.bits_per_pixel != 8)
        return(file_skip(pcl5_ctxt->flptr, bytesLeft, NULL) > 0);
    }
    else {
      /* Mono pattern; fabricate a black and white palette. */
      entry.color = FALSE;
      entry.bits_per_pixel = 1;
    }

    entry.height = READ_SHORT(&header[4]);
    entry.width = READ_SHORT(&header[6]);

    /* If the pattern is degenerate, there's nothing we can do. Abort now. */
    if (entry.width == 0 || entry.height == 0) {
      if (bytesLeft > 0)
        (void)file_skip(pcl5_ctxt->flptr, bytesLeft, NULL);
      return TRUE;
    }

    dataSize = pcl5_id_cache_pattern_data_size(entry.width, entry.height,
                                               entry.bits_per_pixel);
    if (bytesLeft > dataSize && bytesLeft - dataSize == 4) {
      /* We must have the extended header which contains a DPI. */
      if ( file_read(pcl5_ctxt->flptr, &header[8], 4, NULL) <= 0 ) {
        return(FALSE);
      }
      bytesLeft -= 4;

      entry.y_dpi = READ_SHORT(&header[8]);
      entry.x_dpi = READ_SHORT(&header[10]);
      /* Default bad values to 300 dpi. */
      if (entry.y_dpi <= 0)
        entry.y_dpi = 300;
      if (entry.x_dpi <= 0)
        entry.x_dpi = 300;
    }
    else {
      entry.y_dpi = entry.x_dpi = 300;
    }
    entry.stride = dataSize / entry.height;
    entry.data = NULL;
    if (! pcl5_id_cache_insert_pattern(pcl5_ctxt->resource_caches.user,
                                       self->pending_pattern_id,
                                       &entry, &new_entry)) {
      return FALSE;
    }
  }

  /* Copy the pattern data into the newly allocated cache entry. */

  if (bytesLeft < dataSize) {
    patternBytesToRead = bytesLeft;
    bytesLeft = 0;
  }
  else {
    patternBytesToRead = dataSize;
    bytesLeft -= dataSize;
  }

  if ( file_read(pcl5_ctxt->flptr, new_entry->data, patternBytesToRead, NULL) <= 0 ) {
    /* Ran out of data - abort and leave the pattern in the half-created state
     * it's in (since this case should never happen anyway). */
    return(TRUE);
  }

  /* Pad any missing data with white (not in the spec but observed on the
   * printer). */
  if ( patternBytesToRead < dataSize ) {
    HqMemZero(&new_entry->data[patternBytesToRead],
              dataSize - patternBytesToRead);
  }

  /* Consume any excess. */
  if (bytesLeft > 0)
    (void)file_skip(pcl5_ctxt->flptr, bytesLeft, NULL);

  /* Check for solid-black pattern. */
  if (pattern_is_black(new_entry)) {
    /* The pattern is all black and thus can be replaced with the default (NULL)
     * pattern. We indicate this by releasing the pattern raster data; we can't
     * simply remove the pattern from the cache as missing patterns have a
     * different behavior already (i.e. objects drawn in missing patterns are
     * not rendered). */
    pcl5_id_cache_release_pattern_data(pcl5_ctxt->resource_caches.user,
                                       new_entry->detail.numeric_id) ;
  }

#ifdef METRICS_BUILD
  pcl5_metrics.userPatterns ++;
#endif

  return TRUE;
}

/* Values for the pattern control command */
enum {
  DELETE_ALL = 0,
  DELETE_TEMPORARY = 1,
  DELETE_CURRENT = 2,
  MAKE_TEMPORARY = 4,
  MAKE_PERMANENT = 5
} ;

/**
 * Pattern Control Command
 */
Bool pcl5op_star_c_Q(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);
  PCL5IdCache *userCache = pcl5_ctxt->resource_caches.user;

  UNUSED_PARAM(int32, explicit_sign);

  if (IN_RANGE(value.integer, 0, 5)) {
    switch (value.integer) {
      default:
        /* Note that 3 is not a valid value. */
        break;

      case DELETE_ALL:
        pcl5_id_cache_remove_all(userCache, TRUE) ;
        break;

      case DELETE_TEMPORARY:
        pcl5_id_cache_remove_all(userCache, FALSE) ;
        break;

      case DELETE_CURRENT:
        pcl5_id_cache_remove(userCache, self->pending_pattern_id, FALSE) ;
        break;

      case MAKE_TEMPORARY:
        pcl5_id_cache_set_permanent(userCache, self->pending_pattern_id, FALSE);
        break;

      case MAKE_PERMANENT:
        pcl5_id_cache_set_permanent(userCache, self->pending_pattern_id, TRUE);
        break;
    }
  }

  return TRUE;
}

/**
 * Logical Operation Command
 */
Bool pcl5op_star_l_O(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign);

  if (IN_RANGE(value.integer, 0, 255)) {
    set_current_rop(pcl5_ctxt, value.integer);
  }

  return TRUE;
}

void set_pixel_placement(PrintModelInfo* print_info, PCL5Integer pixel_placement)
{
  switch ( pixel_placement ) {
  case PCL5_INTERSECTION_CENTERED :
    print_info->pixel_placement = pixel_placement;
    gstateptr->thePDEVinfo.scanconversion = UserParams.PCLScanConversion;
    break;
  case PCL5_GRID_CENTERED :
    print_info->pixel_placement = pixel_placement;
    gstateptr->thePDEVinfo.scanconversion = SC_RULE_TESSELATE;
    break;
  default:
    /* ignore */
    break;
  }
}

/**
 * Pixel Placement Command
 * N.B. The reference printer locks this command out during raster graphics.
 */
Bool pcl5op_star_l_R(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(int32, explicit_sign);

  if (IN_RANGE(value.integer, 0, 1))
    set_pixel_placement(get_print_model(pcl5_ctxt), value.integer);

  return TRUE;
}

/**
 * Set Pattern Reference Point Command
 */
Bool pcl5op_star_p_R(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PrintModelInfo* self = get_print_model(pcl5_ctxt);
  CursorPosition temp_cursor = *get_cursor(pcl5_ctxt);
  PageCtrlInfo temp_page_info, *page_info = get_page_ctrl_info(pcl5_ctxt) ;
  PCL5Ctms *pcl5_ctms = get_pcl5_ctms(pcl5_ctxt) ;
  PCL5Real dist_from_page_top ;

  UNUSED_PARAM(int32, explicit_sign);

  /* N.B. The Tech Ref description of this makes no mention of non-zero
   *      printdirections, and indeed one printer appears to behave according
   *      to the Tech Ref description, whereby we would always simply set the
   *      pattern origin from the current cursor position.
   *
   *      However, the reference printer sets the pattern reference point
   *      at (x,y) in the current printdirection, where x equals the value
   *      that cursor.x would have if transformed to printdirection zero,
   *      and y is the same distance from the top of the logical page in
   *      the current printdirection as cursor.y would be from the top
   *      of its logical page, if page and cursor were transformed to
   *      printdirection zero.
   */

  if (page_info->print_direction != 0) {
    /* N.B. This would all come to a null op for printdirection 0 */
    temp_page_info = *page_info ;
    transform_cursor(&temp_cursor, ctm_current(pcl5_ctms), ctm_orientation(pcl5_ctms)) ;

    rotate_margins_for_printdirection(&temp_page_info, 0);
    dist_from_page_top = temp_cursor.y + temp_page_info.top_margin ;
    temp_cursor.y = dist_from_page_top - page_info->top_margin ;
  }

  set_pattern_origin(pcl5_ctxt, &temp_cursor);

  /* Set how patterns follow the print direction. */
  if (IN_RANGE(value.integer, 0, 1))
    self->patterns_follow_print_direction = (value.integer == 0);

  return TRUE;
}

void init_C_globals_printmodel(void)
{
  Pcl5CachedPalette init = { 0 } ;
  cached_palette = init ;
}

/* ============================================================================
* Log stripped */
