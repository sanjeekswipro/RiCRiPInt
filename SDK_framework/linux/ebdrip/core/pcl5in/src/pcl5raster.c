/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5raster.c(EBDSDK_P.1) $
 * $Id: src:pcl5raster.c,v 1.174.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the PCL5 "Raster Graphics" category.
 *
 * Raster Resolution            ESC * t # R
 * Presentation                 ESC * r # F
 * Source Raster Height         ESC * r # T
 * Source Raster Width          ESC * r # S
 * Destination Raster Height    ESC * t # V
 * Destination Raster Width     ESC * t # H
 * Scale Algorithm              ESC * t # K
 * Start Raster Graphics        ESC * r # A
 * Y Offset                     ESC * b # Y
 * Set Compression Mode         ESC * b # M
 * Transfer Raster Data         ESC * b # W [raster data ]
 * Transfer Planar Data         ESC * b # V [raster data ]
 * End Raster Graphics          ESC * r B
 *                              ESC * r C
 *
 *                              ESC * r Q
 */

#include "core.h"
#include "swerrors.h"
#include "dicthash.h"
#include "graphics.h"
#include "gschead.h"
#include "gstate.h"
#include "namedef_.h"
#include "stacks.h"
#include "images.h"
#include "timing.h"
#include "display.h"

#include "pcl5raster.h"
#include "pcl5context_private.h"
#include "printenvironment_private.h"
#include "pcl5scan.h"
#include "macros.h"
#include "cursorpos.h"
#include "pclutils.h"
#include "routedev.h"

#define QUANTIZE_IMAGE_SCALING 1

#ifdef DEBUG_BUILD
/* Do this redefinition for the benefit of non-debug builds */
#define TRACE(fCondition, printf_style_args)   HQTRACE(fCondition, printf_style_args)
#else /* !DEBUG_BUILD */
#define TRACE(fCondition, printf_style_args)   EMPTY_STATEMENT()
#endif /* DEBUG_BUILD */


/** \page QUANTIZE_IMAGE_SCALING
 * When scaling images, if the destination dimension is undefined, (or set to
 * zero), our reference printer appears to require an integer number of device
 * pixels per source image pixel, (or in some cases an integer number of source
 * image pixels per device pixel).  Since this may be calculated separately for
 * the X and Y dimensions, this can result in non-isotropic image scaling.
 *
 * This is in contrast to the image scaling behaviour described in the PCL5
 * Color Tech Ref, which indicates that isotropic scaling should be maintained
 * in this case.
 *
 * Define QUANTIZE_IMAGE_SCALING to emulate the reference printer behaviour.
 * Do not define QUANTIZE_IMAGE_SCALING to maintain isotropic scaling.
 */

/** \page PRESENTATION_0_RESETS_5c_WIDTH
 * Our color reference printer resets the source width on receipt of a
 * presentation mode command with parameter value zero, unless both the source
 * width and height have been explicitly set.
 *
 * This results in the source width being taken as the least restrictive of the
 * physical and logical page limits.  If the source image rows are not long
 * enough for this, the 'missing' pixels will be padded with color zero, which
 * could be any color, (as they would be in any other situation where there are
 * not enough source row pixels to make up the full width of the image).
 *
 * This can be seen as an undesirable effect, hence the option to not emulate
 * this.
 *
 * Define RESET_5c_SOURCE_WIDTH_ON_PRESENTATION_MODE_ZERO to emulate the
 * reference printer.
 *
 * Do not define RESET_5c_SOURCE_WIDTH_ON_PRESENTATION_MODE_ZERO to avoid
 * resetting the source width in this case.
 */

#define INVALID_VALUE 0x80000000
#define INVALID_MAGIC 987654321   /* A conspicuous out of range value */

/* There are a maximum of 8 planes. */
#define MAX_PLANES 8

/* Raster reader mode. */
enum {
  /* The reader_mode_on_end_graphics is only set if needed */
  READER_MODE_NOT_SET = -1,
  /* The default mode executes the PCL5 interpreter to obtain the next
     raster row command. */
  DEFAULT_MODE = 0,
  /* Is this context being created because we have seen graphics data
     without having seen a start raster graphics command? */
  EXECUTING_FROM_DATA,
  /* Is this context being created because we have seen graphics plane
     data without having seen a start raster graphics command? */
  EXECUTING_FROM_PLANE_DATA,
  /* The y-offset mode means we are still generating zero raster row
     data. */
  EXECUTING_Y_OFFSET,
  /* We are padding in the vertical direction which means we have read
     all the image data but we do not have enough rows. */
  EXECUTING_Y_PAD,
  /* The adaptive command mode means we are still scanning raster rows
     from an adaptive date block. */
  EXECUTING_ADAPTIVE_COMMAND,
  /* The empty row mode means we are executing an adaptive command to
     generate empty rows. */
  EXECUTING_ADAPTIVE_EMPTY_ROW,
  /* The duplicate row mode means we are executing an adaptive command
     to generate duplicate rows. */
  EXECUTING_ADAPTIVE_DUPLICATE_ROW,
  /* We are generating missing planes, e.g. at end graphics.
   * N.B. This mode has been added for convenience, and the pcl5imageread
   *      operator should never encounter it, (as we only get back there
   *      once per complete row, so the mode should have been set to
   *      EXECUTING_POSTPONED_END_GRAPHICS or EXECUTING_Y_OFFSET before
   *      getting back to pcl5imageread). */
  GENERATING_MISSING_PLANES,
  /* We previously postponed an end graphics command, or a command which
   * causes graphics to end, to allow us to pad some missing planes. */
  EXECUTING_POSTPONED_END_GRAPHICS
} ;

struct PCL5_RasterReader {
  /* Allocation size of this structure. */
  size_t alloc_size ;
  /* The mode of the reader. */
  int32 reader_mode ;
  /* The mode of the reader on entry to end graphics.
   * This is only used if we need to generate missing planes */
  int32 reader_mode_on_end_graphics ;
  /* How many bytes for a full row do we need to generate. */
  int32 width_in_bytes ;
  /* How many bytes for each plane are required? */
  int32 required_bytes_per_plane ;
  /* Storage for a row of raster data. */
  uint8 *zee_bytes[MAX_PLANES] ;
  /* Storage for a row of planar raster data (all planes merged). */
  uint8 *merged_row_bytes ;
  /* Number of bytes in storage. */
  int32 num_bytes ;
  /* String object for DataSource to return. */
  OBJECT string_object ;
  /* How many rows of data have we seen? */
  uint32 num_rows ;
  /* What adaptive command is being executed? */
  int32 adaptive_command ;
  /* How many bytes involved in the row transfer command? */
  int32 read_bytes ;
  /* Counter for repeat commands (empty rows and duplicated rows). */
  int32 repeat_remaining ;
  /* The number of planes seen so far for this row. Only used when
     pixel encoding is indexed by plane or direct by plane. */
  int32 num_planes_seen ;
  /* Reset cursor after the image?
   * We need to do this if no data followed the start raster command */
  Bool  reset_cursor ;
} ;

RasterGraphicsInfo* get_rast_info(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  PCL5PrintEnvironment *mpe ;
  RasterGraphicsInfo *rast_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;
  mpe = print_state->mpe ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  rast_info = &(mpe->raster_graphics) ;

  return rast_info ;
}

PCL5_RasterReader* get_raster_reader(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  PCL5PrintEnvironment *mpe ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;
  mpe = print_state->mpe ;
  HQASSERT(mpe != NULL, "mpe is NULL") ;

  return mpe->raster_graphics.reader ;
}

/* See header for doc. */
void default_raster_graphics_info(
  RasterGraphicsInfo* rast_info)
{
  rast_info->left_margin = 0 ;
  rast_info->resolution = 75 ;
  rast_info->presentation = 3 ;
  rast_info->raster_scaling = FALSE ;
  rast_info->compression_method = PCL5_UNENCODED ;
  rast_info->orig_source_width = INVALID_VALUE ;
  rast_info->source_width = INVALID_MAGIC ;
  rast_info->explicit_source_width = FALSE ;
  rast_info->orig_source_height = INVALID_VALUE ;
  rast_info->source_height = INVALID_MAGIC ;
  rast_info->explicit_source_height = FALSE ;
  rast_info->destination_width = INVALID_VALUE ;
  rast_info->explicit_destination_width = FALSE ;
  rast_info->destination_height = INVALID_VALUE ;
  rast_info->explicit_destination_height = FALSE ;
  rast_info->x_scale = 1 ;
  rast_info->y_scale = 1 ;
  rast_info->colorspace = PCL5_CS_GRAY ;
  rast_info->pixel_encoding_mode = PCL5_MONOCHROME ;
  rast_info->bits_per_index = 1;
  rast_info->bits_for_component_one = 1 ;
  rast_info->bits_for_component_two = 0 ;
  rast_info->bits_for_component_three = 0 ;
  rast_info->num_components = 1 ;
  rast_info->ps_bits_per_index = 1 ;
  rast_info->num_planes = 1 ;
  rast_info->graphics_started = FALSE ;
  rast_info->num_rows = 0 ;
  rast_info->orig_cursor_pos.x = 0 ;
  rast_info->orig_cursor_pos.y = 0 ;
  rast_info->start_pos.x = 0 ;
  rast_info->start_pos.y = 0 ;
  rast_info->reader = NULL ;
}

void save_raster_graphics_info(PCL5Context *pcl5_ctxt,
                               RasterGraphicsInfo *to,
                               RasterGraphicsInfo *from,
                               Bool overlay)
{
  HQASSERT(to != NULL && from != NULL, "RasterGraphicsInfo is NULL") ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;

  if (overlay) {
    /* N.B. It is against the idea of an overlay macro to copy over any but the
     *      most essential items for page and job control, but unlike some
     *      other printers, our reference printer does appear to copy various
     *      items of raster graphics state on entry to an overlay macro.
     *      No mention is made of these in the PCL5 Tech Ref or Color Tech Ref.
     *
     *      The bools below for explicit_source_width or height, and
     *      explicit_destination_width or height have been copied because this
     *      seems logical, given that orig_source_width and height, and
     *      destination_width and height are copied.
     *
     *      Testing with the reference printer has established that none of
     *      left_margin, resolution, presentation, compression_method,
     *      raster_scaling, (nor indeed the raster graphics start mode),
     *      source_width or source_height (by analogy with source_width),
     *      should be copied here.  All the remaining elements of the
     *      RasterGraphicsInfo structure should get set up anyway during the
     *      overlay macro graphics, so there is no need to consider copying
     *      them here.
     */
    to->orig_source_width = from->orig_source_width ;
    to->orig_source_height = from->orig_source_height ;

    to->explicit_source_width = from->explicit_source_width ;
    to->explicit_source_height = from->explicit_source_height ;

    to->destination_width = from->destination_width ;
    to->destination_height = from->destination_height ;

    to->explicit_destination_width = from->explicit_destination_width ;
    to->explicit_destination_height = from->explicit_destination_height ;
  }
}


void restore_raster_graphics_info(PCL5Context *pcl5_ctxt,
                                  RasterGraphicsInfo *to,
                                  RasterGraphicsInfo *from)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(RasterGraphicsInfo*, to) ;
  UNUSED_PARAM(RasterGraphicsInfo*, from) ;
}


/* Predefine some functions. */
static
Bool planes_are_missing(PCL5Context *pcl5_ctxt) ;

static
Bool generate_missing_planes(PCL5Context *pcl5_ctxt) ;

static
Bool merge_planar_data(PCL5Context *pcl5_ctxt, PCL5_RasterReader *reader,
                       int32 *output_length, int32 num_bytes, Bool is_plane_data) ;

static
Bool decode_row_block(PCL5Context *pcl5_ctxt, int32 num_bytes, Bool is_plane_data, Bool pcl_cmd) ;

static
void decode_row_block_for_zero_sized_image(PCL5Context *pcl5_ctxt, int32 num_bytes, Bool is_plane_data) ;

static
Bool expand_adaptive(PCL5Context *pcl5_ctxt, PCL5_RasterReader *reader,
                     int32 *out_length, int32 which_plane) ;

static
Bool set_image_ps_state(PCL5Context *pcl5_ctxt) ;

static
Bool set_image_ps_colorspace(PCL5Context *pcl5_ctxt) ;

static
Bool image_has_zero_height(PCL5Context *pcl5_ctxt) ;

static
Bool image_has_zero_width(PCL5Context *pcl5_ctxt) ;

static
void calculate_image_start_position(PCL5Context *pcl5_ctxt, CursorPosition *start_pos) ;

static
void calculate_max_source_height(PCL5Context *pcl5_ctxt, uint32 *max_height) ;

static
void calculate_max_source_width(PCL5Context *pcl5_ctxt, uint32 *max_width) ;

static
void calculate_max_available_height(PCL5Context *pcl5_ctxt, PCL5Real *max_height) ;

static
void calculate_max_available_width(PCL5Context *pcl5_ctxt, PCL5Real *max_width) ;

/* Do the image rows go across the logical page, i.e.  do they run parallel
 * to the x-axis of the logical page ?
 */
static
Bool image_is_width_aligned(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info ;
  PageCtrlInfo *page_info ;
  Bool width_aligned = TRUE ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;

  /* In presentation mode 0 the rows always go across the logical page */
  if (rast_info->presentation == 3) {

    /* Is the combination of orientation and print direction landscape ? */
    if (page_info->orientation == 0 || page_info->orientation == 2) {
      if (page_info->print_direction == 90 || page_info->print_direction == 270)
        width_aligned = FALSE ;
    }
    else if (page_info->print_direction == 0 || page_info->print_direction == 180)
      width_aligned = FALSE ;
  }

  return width_aligned ;
}

#define zero_seed_row(r, p) MACRO_START \
  if ((r)->required_bytes_per_plane > 0) \
    HqMemZero((int8*)((r)->zee_bytes[(p)]), (r)->required_bytes_per_plane); \
MACRO_END

#define zero_merged_row(r) MACRO_START \
  if ((r)->width_in_bytes > 0) \
    HqMemZero((int8*)((r)->merged_row_bytes), (r)->width_in_bytes); \
MACRO_END


/* ============================================================================
 * Dispatch the rendering of the image.
 * ============================================================================
 */

static
Bool create_raster_read_context(PCL5Context *pcl5_ctxt, RasterGraphicsInfo *rast_info,
                                PCL5_RasterReader **read,
                                int32 reader_mode, int32 num_bytes)
{
  PCL5_RasterReader *new_reader ;
  int32 size, i ;
  double width, width_per_plane ;

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  HQASSERT(read != NULL, "read is NULL") ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  HQASSERT(reader_mode == DEFAULT_MODE || reader_mode == EXECUTING_FROM_DATA ||
           reader_mode == EXECUTING_FROM_PLANE_DATA,
           "Invalid reader_mode when creating read context.") ;

  HQASSERT((rast_info->source_width != INVALID_MAGIC),
           "Should have a valid source width by now");
  HQASSERT((rast_info->source_height != INVALID_MAGIC),
           "Should have a valid source height by now");

  *read = NULL ;

  HQASSERT(rast_info->bits_per_index > 0, "corrupt bits per index") ;
  width = ((double)rast_info->source_width * (double)rast_info->num_components * (double)rast_info->ps_bits_per_index) / 8 ;
  if (rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE) {
    width_per_plane = width / 3 ;
  } else if (rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE) {
    HQASSERT(rast_info->ps_bits_per_index > 0, "corrupt ps bits per index") ;
    width_per_plane = (double)rast_info->source_width / 8 ;
  } else {
    width_per_plane = width ;
  }

  /* If there is a fractional part to the number of bytes, we need
     another byte. */
  if ((width - (int32)width) != 0)
    width++ ;
  if ((width_per_plane - (int32)width_per_plane) != 0)
    width_per_plane++ ;

  /* Truncate fractional part. */
  width = (int32)width ;
  width_per_plane = (int32)width_per_plane ;

  /* Width per plane is the maximum number of bytes we will
     uncompress. */
  size = (int32)sizeof(PCL5_RasterReader) ;

  /* If we are dealing with planar data we need extra space to merge
     the different planes before handing to the PS image operator. */
  if (rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE ||
      rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE) {

    size += (int32)width ;
  }

  if ((new_reader = mm_alloc(mm_pcl_pool,
                             size,
                             MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL)
    return FALSE ;

  new_reader->alloc_size = size ;
  new_reader->num_bytes = 0 ;
  new_reader->num_rows = 0 ;
  new_reader->reader_mode = reader_mode ;
  new_reader->reader_mode_on_end_graphics = READER_MODE_NOT_SET ;
  new_reader->adaptive_command = -1 ;
  new_reader->repeat_remaining = 0 ;
  new_reader->read_bytes = num_bytes ;
  new_reader->num_planes_seen = 0 ;
  new_reader->width_in_bytes = (int32)width ;
  new_reader->required_bytes_per_plane = (int32)width_per_plane ;
  for (i=0; i < MAX_PLANES; i++) {
    new_reader->zee_bytes[i] = NULL ;
  }

  new_reader->reset_cursor = FALSE ;

  /* We need a seed row per plane. */
  for (i=0; i < rast_info->num_planes; i++) {
    if (width_per_plane > 0) {
      if ((new_reader->zee_bytes[i] = mm_alloc(mm_pcl_pool,
                                               (size_t)width_per_plane,
                                               MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
        for (i=0; i < rast_info->num_planes; i++) {
          if (new_reader->zee_bytes[i] != NULL) {
            mm_free(mm_pcl_pool, new_reader->zee_bytes[i], (size_t)width_per_plane) ;
          }
        }
        mm_free(mm_pcl_pool, new_reader, new_reader->alloc_size) ;
        return FALSE ;
      }
      zero_seed_row(new_reader, i) ;
    }
  }

  if (rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE ||
      rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE) {
    /* We need the merged seed rows. */
    new_reader->merged_row_bytes = (uint8*)new_reader + sizeof(PCL5_RasterReader) ;
    zero_merged_row(new_reader) ;
    oString(new_reader->string_object) = new_reader->merged_row_bytes ;
    theLen(new_reader->string_object) = (uint16)width ;
  } else {
    /* We only ever need one seed row. */
    new_reader->merged_row_bytes = NULL ;
    oString(new_reader->string_object) = new_reader->zee_bytes[0] ;
    theLen(new_reader->string_object) = (uint16)width_per_plane ;
  }

  theTags(new_reader->string_object) = OSTRING|UNLIMITED|LITERAL ;
  theMark(new_reader->string_object) = ISLOCAL|ISNOTVM|SAVEMASK ;

  *read = new_reader ;
  return TRUE ;
}

static
void destroy_raster_read_context(PCL5_RasterReader **read)
{
  int32 i ;
  HQASSERT(read != NULL, "read is NULL") ;
  HQASSERT(*read != NULL, "*read is NULL") ;

  for (i=0; i < MAX_PLANES; i++) {
    if ((*read)->zee_bytes[i] != NULL) {
      mm_free(mm_pcl_pool, (*read)->zee_bytes[i], (*read)->required_bytes_per_plane) ;
    }
  }

  mm_free(mm_pcl_pool, *read, (*read)->alloc_size) ;
  *read = NULL ;
}

/**
 * Returns TRUE if the entries in a 1-bit palette are compatible with the
 * transparent-image-as-imagemask optimisation.
 */
static Bool palette_colors_like_mask(PCL5Context *pcl5_ctxt,
                                     Bool* polarity)
{
  uint32 zeroth = classify_palette_entry(pcl5_ctxt, 0);
  uint32 first = classify_palette_entry(pcl5_ctxt, 1);

  if (zeroth == PCL_COLOR_IS_WHITE && first == PCL_COLOR_IS_BLACK) {
    *polarity = TRUE;
    return TRUE;
  }

  if (zeroth == PCL_COLOR_IS_BLACK && first == PCL_COLOR_IS_WHITE) {
    *polarity = FALSE;
    return TRUE;
  }

  return FALSE;
}

/**
 * Returns true if the current image can be treated as a masked image.
 */
static Bool image_is_like_mask(PCL5Context *pcl5_ctxt,
                               RasterGraphicsInfo* rast_info,
                               Bool* polarity)
{
  if (rast_info->bits_per_index != 1 || rast_info->num_components != 1)
    return FALSE;

  *polarity = TRUE;
  switch (rast_info->pixel_encoding_mode) {
    default:
      HQFAIL("Unsupported pixel encoding.");
      return FALSE;

    case PCL5_DIRECT_BY_PLANE:
    case PCL5_DIRECT_BY_PIXEL:
      HQFAIL("Single component direct image not expected.");
      return FALSE;

    case PCL5_MONOCHROME:
      return TRUE;

    case PCL5_INDEXED_BY_PLANE:
    case PCL5_INDEXED_BY_PIXEL:
      return palette_colors_like_mask(pcl5_ctxt, polarity);
  }
}

static Bool pcl5_image_args(PCL5Context *pcl5_ctxt,
                            USERVALUE decodes[],
                            OBJECT *data_source,
                            IMAGEARGS *imageargs)
{
  RasterGraphicsInfo *rast_info = get_rast_info(pcl5_ctxt) ;
  PrintModelInfo* print_model = get_print_model(pcl5_ctxt) ;
  Bool mask_polarity ;
  int32 decode_size ;

  init_image_args(imageargs, GSC_IMAGE) ;
  imageargs->imageop = NAME_image ;
  imageargs->imagetype = TypeImageImage ;
  imageargs->interleave = INTERLEAVE_NONE ;

  imageargs->width = rast_info->source_width ;
  imageargs->height = imageargs->lines_per_block =
    rast_info->source_height ;

  imageargs->omatrix.matrix[0][0] = rast_info->source_width ;
  imageargs->omatrix.matrix[1][1] = rast_info->source_height ;
  imageargs->omatrix.opt = MATRIX_OPT_0011 ;

  imageargs->nprocs = 1 ;
  object_store_operator(data_source, NAME_pcl5imageread) ;
  imageargs->data_src = data_source ;

  imageargs->decode = decodes ;
  decode_size = rast_info->num_components ;
  HQASSERT(decode_size <= MAX_PLANES, "Too many channels in PCL image") ;
  if (rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE ||
      rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PIXEL) {
    int32 max_decode = (1 << rast_info->ps_bits_per_index) - 1 ;
    HQASSERT(max_decode <= 255, "decode max is too large") ;
    while (--decode_size >= 0) {
      decodes[decode_size * 2] = 0 ;
      decodes[decode_size * 2 + 1] = (USERVALUE)max_decode ;
    }
  } else {
    if (rast_info->num_components > 1) {
      while (--decode_size >= 0) {
        decodes[decode_size * 2] = 0 ;
        decodes[decode_size * 2 + 1] = 1 ;
      }
    } else {
      while (--decode_size >= 0) {
        decodes[decode_size * 2] = 1 ;
        decodes[decode_size * 2 + 1] = 0 ;
      }
    }
  }

  imageargs->bits_per_comp = rast_info->ps_bits_per_index ;

  imageargs->image_pixel_centers = FALSE ;
  imageargs->clean_matrix = FALSE ;

  /* NYI check_image_misc_hooks(). */

  set_image_order(imageargs);

  if (print_model->source_transparent &&
#ifdef DEBUG_BUILD
      (debug_pcl5 & PCL5_NOMASK) == 0 &&
#endif
      image_is_like_mask(pcl5_ctxt, rast_info, &mask_polarity)) {
    imageargs->imageop = NAME_imagemask ;
    imageargs->imagetype = TypeImageMask ;
    imageargs->colorType = GSC_FILL ;
    imageargs->polarity = CAST_TO_INT8(mask_polarity) ;
    setPclSourceTransparent(FALSE) ;
    /* We're converting an image into a mask, which will have the same color
       for all samples. As well as setting source transparency FALSE (the
       mask spans define the implicit black source areas), we can set
       PCL_FOREGROUND_IN_DL_COLOR so that normal blits take effect. The ROP
       will be appropriately simplified by getPclAttrib(). */
    if ( !setPclForegroundSource(pcl5_ctxt->corecontext->page,
                                 PCL_DL_COLOR_IS_FOREGROUND) )
      HQFAIL("Should not fail to set DL color to foreground");

    /* Replace the decode array with one suitable for the mask. */
    if (mask_polarity) {
      decodes[0] = 1 ;
      decodes[1] = 0 ;
    } else {
      decodes[0] = 0 ;
      decodes[1] = 1 ;
    }
  } else {
    /* Set up the image colorspace */
    if (! set_image_ps_colorspace(pcl5_ctxt))
      return FALSE ;
  }

  /* Set colorspace details */
  imageargs->ncomps = gsc_dimensions(gstateptr->colorInfo, GSC_FILL) ;
  imageargs->image_color_space =
    gsc_getcolorspace(gstateptr->colorInfo, GSC_FILL) ;
  Copy(object_slot_notvm(&imageargs->image_color_object),
       gsc_getcolorspaceobject(gstateptr->colorInfo, GSC_FILL)) ;
  /* Struct copy now OK, the image_color_object is NOTVM. */
  imageargs->colorspace = imageargs->image_color_object ;

  if ( !filter_image_args(pcl5_ctxt->corecontext, imageargs) )
    return FALSE ;

  return TRUE ;
}

/* We dispatch images in PCL5 by invoking the PS image operator using
   a PS procedure as the DataSource. This PS procedure invokes the
   PCL5 interpreter recursively to continue reading image data. */
static
Bool pcl5_dispatch_image(PCL5Context *pcl5_ctxt, int32 reader_mode, int32 num_bytes, Bool explicit_start)
{
  RasterGraphicsInfo *rast_info ;
  PCL5_RasterReader *new_reader ;
  PCL5PrintState *print_state = pcl5_ctxt->print_state;
  Bool status ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;
  HQASSERT(rast_info->reader == NULL, "reader is not NULL") ;
  HQASSERT(reader_mode == DEFAULT_MODE || reader_mode == EXECUTING_FROM_DATA ||
           reader_mode == EXECUTING_FROM_PLANE_DATA,
           "Invalid reader_mode when creating read context.") ;

    /* We do this lazily now */
  if (! pcl5_flush_text_run(pcl5_ctxt, 1))
    return FALSE ;

  /* A DEVICE_SETG will be required for plotchar after this point as this
   * will set the color_type to GSC_IMAGE.
   */
  print_state->setg_required += 1;

  /* create image read context */
  if (! create_raster_read_context(pcl5_ctxt, rast_info, &new_reader, reader_mode, num_bytes))
    return FALSE ;

  /* If we never get any image data we will need to reset the cursor.
   * (If we have an implicit start we must have had some data already,
   * so this will not apply).
   */
  new_reader->reset_cursor = explicit_start ;

  /* Set core state for image. */
  if ( !set_image_ps_state(pcl5_ctxt) ) {
    destroy_raster_read_context(&new_reader) ;
    return(FALSE);
  }

  {
    IMAGEARGS imageargs ;
    USERVALUE decodes[MAX_PLANES * 2] ;
    OBJECT data_source = OBJECT_NOTVM_NOTHING ;

    /** \todo ajcd 2013-12-03: Do we need to flush_vignette() and test
        DEVICE_INVALID_CONTEXT() here as gs_image does? */

    probe_begin(SW_TRACE_INTERPRET_IMAGE, 5);
    if ( !pcl5_image_args(pcl5_ctxt,
                          decodes, &data_source, &imageargs) ) {
      destroy_raster_read_context(&new_reader) ;
      probe_end(SW_TRACE_INTERPRET_IMAGE, 5);
      return FALSE;
    }

    rast_info->reader = new_reader ;

    /** \todo ajcd 2013-12-04: stack isn't used, except for the npop(), which
        asserts if there isn't any stack to remove from. */
    status = DEVICE_IMAGE(pcl5_ctxt->corecontext->page, &operandstack, &imageargs) ;
    finish_image_args(&imageargs) ;

    probe_end(SW_TRACE_INTERPRET_IMAGE, 5);
  }

  reinstate_pcl5_print_model(pcl5_ctxt) ;

  /* cleanup */
  /* N.B. This may in fact be the only place where we need to set the
   *      print_state->possible_raster_data_trim flag, i.e. it may be
   *      that any time we do it in end_raster_graphics_callback, we
   *      could have done it here.
   */
  if ( new_reader->read_bytes > 0 ) {
    (void)file_skip(pcl5_ctxt->flptr, new_reader->read_bytes, NULL);
  }

  if (rast_info->reader != NULL) {
    destroy_raster_read_context(&rast_info->reader) ;
    rast_info->reader = NULL ;
  }

  return status ;
}

/* Move the cursor the given number of raster rows */
void move_cursor_down_n_raster_rows(PCL5Context *pcl5_ctxt, uint32 n)
{
  RasterGraphicsInfo *rast_info ;
  PCL5Real height ;   /* pcl internal units */

  if (n > 0) {
    rast_info = get_rast_info(pcl5_ctxt) ;

    height = (PCL5Real) (n * 7200 * rast_info->y_scale / rast_info->resolution) ;

    if (image_is_width_aligned(pcl5_ctxt))
      move_cursor_y_relative(pcl5_ctxt, height) ;
    else
      move_cursor_x_relative(pcl5_ctxt, (-height)) ;
  }
}

static
Bool set_image_ps_colorspace(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  RasterGraphicsInfo *rast_info ;
  OBJECT indexedSpaceObj = OBJECT_NOTVM_NOTHING ;

  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  rast_info = get_rast_info(pcl5_ctxt) ;

  if (rast_info->pixel_encoding_mode == PCL5_MONOCHROME) {
    if (! set_ps_colorspace_internal(PCL5_CS_GRAY))
      return FALSE ;

  } else if (rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE ||
             rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PIXEL) {

    switch (rast_info->colorspace) {
    case PCL5_CS_RGB:
      if (! set_ps_colorspace_internal(PCL5_CS_RGB))
        return FALSE ;
      break ;

    case PCL5_CS_CMY:
      if (! set_ps_colorspace_internal(PCL5_CS_CMY))
        return FALSE ;
      break ;
    }
  } else {
    if (! create_indexed_space_from_palette(pcl5_ctxt, &indexedSpaceObj) ||
        ! gsc_setcustomcolorspacedirect(gstateptr->colorInfo, GSC_FILL,
                                        &indexedSpaceObj, FALSE))
      return FALSE ;
  }

  return TRUE ;
}

/**
 * Returns true if the current foreground color should be applied to the image.
 *
 * Although not documented, it has been observed that the reference printer
 * (HP 4700n) does not apply the foreground color to images in some situations,
 * e.g. when the foreground is in CMY, but the image is in RGB. If a pattern is
 * in effect we always apply the foreground color.
 */
static Bool apply_foreground_color(PCL5Context *pcl5_ctxt)
{
  ColorPalette* palette = get_active_palette(pcl5_ctxt);
  ColorInfo* info = get_color_info(pcl5_ctxt);

  /* Patterned images always have the foreground applied. */
  switch (get_current_pattern_type(pcl5_ctxt)) {
    case PCL5_SHADING_PATTERN:
    case PCL5_CROSS_HATCH_PATTERN:
    case PCL5_USER_PATTERN:
      return TRUE;
  }

  /* Check for colorspace compatibility. */
  if (palette->colorspace == info->foreground_color.colorspace) {
    uint32 a = palette->type;
    uint32 b = info->foreground_color.type;

    /* CID and simple palettes are compatible; it seems that only a cross-over
     * between the default black and white palette with a simple/CID palette is
     * incompatible. */
    if ((a == BLACK_AND_WHITE && b == BLACK_AND_WHITE) ||
        (a != BLACK_AND_WHITE && b != BLACK_AND_WHITE))
      return TRUE;
  }

  return FALSE;
}

/* Get physical image width and height, (the maximum width and height that will
 * fit on the physical page), allowing for page orientation and image width
 * alignment.
 *
 * N.B. Although only the image area inside the printable area of the page will
 *      be visible, it is necessary to go to the physical page edge, (at least
 *      for default height where the logical page goes all the way to the end
 *      of the physical page), in order to ensure correct cursor positioning
 *      after the image.
 */
static
void physical_width_height(
  PCL5Context*  pcl5_ctxt,
  uint32*       width,
  uint32*       height)
{
  CursorPosition cursor;
  RasterGraphicsInfo* rast_info;
  PageCtrlInfo* page_info;
  PCL5Ctms* ctms;
  PCL5Real scale_factor;
  PCL5Real temp_width, temp_height;
  int32 orientation;

  HQASSERT((width != NULL),
           "physical_width_height: returned width pointer NULL");
  HQASSERT((height != NULL),
           "physical_width_height: returned height pointer NULL");

  rast_info = get_rast_info(pcl5_ctxt);
  page_info = get_page_ctrl_info(pcl5_ctxt);
  ctms = get_pcl5_ctms(pcl5_ctxt);

  *width = 0;
  *height = 0;

  /* Transform the image start position to the base CTM, since
   * the position is needed relative to the physical page.
   */
  calculate_image_start_position(pcl5_ctxt, &cursor);
  transform_cursor(&cursor, &ctms->print_direction, &ctms->base);

  /* To find out the image print direction assume it follows the logical page
   * (presentation mode of 0) and then rotate if necessary if printing
   * across physical page. */
  orientation = (page_info->orientation + page_info->print_direction/90)%4;
  if ( rast_info->presentation == 3 ) {
    orientation &= 0x02;
  }

  /* Since all CTMs have a scale of 1/7200 we just need the difference between
   * cursor and appropriate physical page edge.
   */
  /** \todo Is it necessary to round to nearest pixel below, or to round up? */
  scale_factor = rast_info->resolution/7200.0;
  switch ( orientation ) {
  default: /* Also silences compiler warning */
    HQFAIL("Unexpected page orientation.");
    /* FALLTHROUGH */
  case 0: /* Portrait */
    temp_height = (page_info->physical_page_length - cursor.y) * scale_factor;
    temp_width =  (page_info->physical_page_width - cursor.x) * scale_factor;
    break;
  case 1: /* Landscape */
    temp_height = (page_info->physical_page_width - cursor.x) * scale_factor;
    temp_width =  cursor.y * scale_factor;
    break;
  case 2: /* Reverse Portrait */
    temp_height = cursor.y * scale_factor;
    temp_width =  cursor.x *scale_factor;
    break;
  case 3: /* Reverse Landscape */
    temp_height = cursor.x * scale_factor;
    temp_width =  (page_info->physical_page_length - cursor.y) * scale_factor;
    break;
  }

  if (temp_height > 0)
    *height = (uint32) temp_height ;

  if (temp_width > 0)
    *width = (uint32) temp_width ;

} /* physical_width_height */


static Bool set_image_ps_state(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info ;
  PageCtrlInfo *page_info ;
  double image_x_size, image_y_size ;
  Bool width_aligned ;
  OMATRIX matrix_90 = {{{0, 1}, {-1, 0}, {0, 0}}, MATRIX_OPT_1001} ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;

  /* We shouldn't be here with values that will result in zero, or undefined
   * image sizes.
   */
  HQASSERT((rast_info->source_width != 0 &&
            rast_info->source_width != INVALID_MAGIC &&
            rast_info->x_scale != 0) &&
           (rast_info->source_height != 0 &&
            rast_info->source_height != INVALID_MAGIC &&
            rast_info->y_scale != 0),
           "Whoops zero or undefined image size") ;

  HQASSERT(rast_info->resolution != 0, "corrupt raster resolution") ;

  /* Is the image aligned with the logical page? */
  width_aligned = image_is_width_aligned(pcl5_ctxt) ;

  /* Work out the correct scaling for the CTM
   * N.B. All valid image resolutions divide into 7200
   */
  /** \todo Possibly could just store image_x_size rather than x_scale, etc */
  image_x_size = 7200.0 * rast_info->source_width * rast_info->x_scale / rast_info->resolution ;
  image_y_size = 7200.0 * rast_info->source_height * rast_info->y_scale / rast_info->resolution ;

  {
    /* Install the image transformation. */
    OMATRIX image_ctm = *ctm_current(get_pcl5_ctms(pcl5_ctxt));
    matrix_translate(&image_ctm, rast_info->start_pos.x, rast_info->start_pos.y, &image_ctm) ;

    if (! width_aligned)
      matrix_mult(&matrix_90, &image_ctm, &image_ctm) ;

    matrix_scale(&image_ctm, image_x_size, image_y_size, &image_ctm) ;

    if (! pcl_setctm(&image_ctm, FALSE))
      return FALSE ;
  }

  if (apply_foreground_color(pcl5_ctxt)) {
    /* Set the PS colorspace and foreground color; images are colored by both
     * their inherent color and the foreground color. */
    if ( !set_ps_colorspace(pcl5_ctxt) || !set_ps_color(pcl5_ctxt) )
      return FALSE ;
  } else {
    /* If the foreground and image colorspaces are not compatible, set the
     * current color to black so it has no effect on the image. */
    if ( !set_black() )
      return FALSE ;
  }

  /* This captures the current color into the foreground color, as well as
     setting the source flag. */
  if (! setPclForegroundSource(pcl5_ctxt->corecontext->page,
                               PCL_FOREGROUND_IN_PCL_ATTRIB))
    return FALSE;

  /* Set the current pattern. Note that the erase pattern means a 'white'
   * pattern in the context of images. */
  if (get_current_pattern_type(pcl5_ctxt) == PCL5_ERASE_PATTERN) {
    set_current_pattern(pcl5_ctxt, PCL5_WHITE_PATTERN);
  }
  else {
    set_current_pattern(pcl5_ctxt, PCL5_CURRENT_PATTERN);
  }

  /* set_image_ps_colorstate() must be left until after we've decided if the
     image will be converted into a mask. */

  /* N.B. The above PS changes would require us to set the setg_required flag,
   * but in fact we have already set this in dispatch_image due to the change
   * in object type.
   */

  return TRUE ;
}

/* Set up various items of RasterGraphicsInfo from the active
   palette. */
void set_rast_info_from_palette(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  RasterGraphicsInfo *rast_info ;
  ColorPalette *palette ;
  uint8 bits_per_index ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  palette = get_active_palette(pcl5_ctxt) ;
  HQASSERT(palette != NULL, "palette is NULL" ) ;

  if ( !pcl5_ctxt->pcl5c_enabled ) {
    bits_per_index = 1 ;
    rast_info->num_components = 1 ;
    rast_info->num_planes = 1 ;
    rast_info->bits_for_component_one = 0 ;
    rast_info->bits_for_component_two = 0 ;
    rast_info->bits_for_component_three = 0 ;
    rast_info->ps_bits_per_index = 1 ;

    HQASSERT(palette->pixel_encoding_mode == PCL5_MONOCHROME,
             "Incorrect palette installed for PCL 5e mode.") ;
    return ;
  }

  rast_info->colorspace = palette->colorspace ;
  rast_info->pixel_encoding_mode = palette->pixel_encoding_mode ;

  /* Set up the bits per index, bits per component, and number of
   * components for the image, (note that indexed colorspaces have
   * only one component as far as PS is concerned).
   *
   * N.B. The bits per component is derived from the other values, (as
   * opposed to the real values from the job).
   */


  /* This is a hack. We have discovered that PCL5 images do NOT use
     HPGL2 color palattes. This is NOT documented anywhere. */
  if (palette->creator == PCL5) {
    bits_per_index = palette->bits_per_index ;
  } else { /* HPGL2 */
    bits_per_index = 1 ;
    rast_info->num_components = 1 ;
    rast_info->num_planes = 1 ;
    rast_info->bits_for_component_one = 0 ;
    rast_info->bits_for_component_two = 0 ;
    rast_info->bits_for_component_three = 0 ;
    rast_info->ps_bits_per_index = 1 ;
    rast_info->pixel_encoding_mode = PCL5_MONOCHROME ;
    return ;
  }

  switch (palette->pixel_encoding_mode) {
    case PCL5_INDEXED_BY_PLANE:
      rast_info->num_components = 1 ;
      rast_info->num_planes = bits_per_index ;
      rast_info->bits_for_component_one = 0 ;
      rast_info->bits_for_component_two = 0 ;
      rast_info->bits_for_component_three = 0 ;
      break ;

    case PCL5_INDEXED_BY_PIXEL:
      rast_info->num_components = 1 ;
      rast_info->num_planes = 1 ;
      rast_info->bits_for_component_one = 0 ;
      rast_info->bits_for_component_two = 0 ;
      rast_info->bits_for_component_three = 0 ;
      break ;

    case PCL5_DIRECT_BY_PLANE:
      bits_per_index = 1 ;
      rast_info->num_components = 3 ;
      rast_info->num_planes = 3 ;
      rast_info->bits_for_component_one = 0 ;
      rast_info->bits_for_component_two = 0 ;
      rast_info->bits_for_component_three = 0 ;
      break ;

    case PCL5_DIRECT_BY_PIXEL:
      bits_per_index = 8 ;
      rast_info->num_components = 3 ;
      rast_info->num_planes = 1 ;
      rast_info->bits_for_component_one = 0 ;
      rast_info->bits_for_component_two = 0 ;
      rast_info->bits_for_component_three = 0 ;
      break ;

    case PCL5_MONOCHROME:
      bits_per_index = 1 ;
      rast_info->num_components = 1 ;
      rast_info->num_planes = 1 ;
      rast_info->bits_for_component_one = 0 ;
      rast_info->bits_for_component_two = 0 ;
      rast_info->bits_for_component_three = 0 ;
      break ;
  }

  rast_info->bits_per_index = bits_per_index ;

  /* PS only handles 1,2,4,8 and 12 bits per component. Setup this internal state. */
  if (bits_per_index == 3) {
    rast_info->ps_bits_per_index = 4 ;
  } else if (bits_per_index == 5 ||
             bits_per_index == 6 ||
             bits_per_index == 7) {
    rast_info->ps_bits_per_index = 8 ;
  } else {
    rast_info->ps_bits_per_index = bits_per_index ;
  }
}


/* Will the image end up having zero width or height?
 * Returns FALSE if it is too soon to establish this.
 *
 * If the image height, width or scale factors ended up as zero, because there
 * was not enough room on the page, (as opposed to the original width or height
 * from the job being zero), we need to wait until we have started raster
 * graphics again, before stating that there is a zero dimension, as the image
 * will now be differently positioned on the page.  (It is not enough to check
 * whether the reader is NULL because this can be left as NULL following a
 * start_raster_graphics callback, if the image area would have ended up as
 * zero).
 */
static
Bool image_has_zero_dimension(PCL5Context *pcl5_ctxt)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  return (image_has_zero_width(pcl5_ctxt) ||
          image_has_zero_height(pcl5_ctxt)) ;
}

/* Will the image end up having zero height?
 * Returns FALSE if it is too soon to establish this.
 * See also image_has_zero_dimension.
 */
static
Bool image_has_zero_height(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info = get_rast_info(pcl5_ctxt) ;

  return (rast_info->orig_source_height == 0 ||
          (rast_info->graphics_started &&
           (rast_info->source_height == 0 || rast_info->y_scale == 0))) ;
}

/* Will the image end up having zero width?
 * Returns FALSE if it is too soon to establish this.
 * See also image_has_zero_dimension.
 */
static
Bool image_has_zero_width(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info = get_rast_info(pcl5_ctxt) ;

  return (rast_info->orig_source_width == 0 ||
          (rast_info->graphics_started &&
          (rast_info->source_width == 0 || rast_info->x_scale == 0))) ;
}

/* N.B. This appears to mean 50 dots at 300 dpi, i.e. 1/6 inch.
 *      Although Color Tech Ref page 6-17 only mentions this as affecting X-pos
 *      it can affect either X or Y, as per page 6-11, of the same document.
 */
void calculate_left_graphics_margin(PCL5Context *pcl5_ctxt, Bool default_margin, PCL5Real *left_margin)
{
  PageCtrlInfo *page_info ;
  PCL5Real x_pos, y_pos ;
  Bool width_aligned ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  width_aligned = image_is_width_aligned(pcl5_ctxt) ;

  *left_margin = 0 ;

  if (width_aligned) {
    if (!default_margin)
      get_cursor_position(pcl5_ctxt, left_margin, &y_pos) ;
  }
  else {
    if (!default_margin) {
      /* Work out the distance from the cursor to the top of the page */
      get_cursor_position(pcl5_ctxt, &x_pos, left_margin) ;
      *left_margin = *left_margin + page_info->top_margin ;
    }
    else {
      /* ((7200 * 50)/300) PCL Internal Units from top of logical page */
      *left_margin = 1200 ;
    }
  }
}


void set_left_graphics_margin(PCL5Context *pcl5_ctxt, Bool default_margin)
{
  RasterGraphicsInfo *rast_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is Null") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is Null") ;

  calculate_left_graphics_margin(pcl5_ctxt, default_margin, &(rast_info->left_margin)) ;
}

/* Calculate the image start position, (the coordinates of the top left
 * corner of the image), from the original cursor position.
 */
static
void calculate_image_start_position(PCL5Context *pcl5_ctxt, CursorPosition *start_pos)
{
  PageCtrlInfo *page_info;
  RasterGraphicsInfo *rast_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(start_pos != NULL, "start_pos is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is Null") ;

  *start_pos = rast_info->orig_cursor_pos ;

  if (image_is_width_aligned(pcl5_ctxt))
    start_pos->x = rast_info->left_margin;
  else
    start_pos->y = - (PCL5Real) page_info->top_margin + rast_info->left_margin;
}

/* Calculate the maximum available height from current cursor Y position to the
 * logical page boundary in PCL internal units.
 */
void calculate_max_available_height(PCL5Context *pcl5_ctxt, PCL5Real *max_height)
{
  PageCtrlInfo *page_info;
  PCL5Real x_pos, y_pos ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(max_height != NULL, "max_height is NULL") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Limit the height so that columns end within the logical page boundary */
  get_cursor_position(pcl5_ctxt, &x_pos, &y_pos) ;

  if (image_is_width_aligned(pcl5_ctxt))
    *max_height = page_info->max_text_length - y_pos ;
  else
    *max_height = x_pos ;
}


/* Calculate the maximum source height from current cursor Y position to the
 * logical page boundary.
 */
void calculate_max_source_height(PCL5Context *pcl5_ctxt, uint32 *max_height)
{
  RasterGraphicsInfo *rast_info ;
  PCL5Real scale_factor, pcl_internal_height ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(max_height != NULL, "max_height is NULL") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  scale_factor = rast_info->resolution / 7200.0f ;

  /* Get the height in PCL internal units from the Y position
   * to logical page boundary.
   */
  calculate_max_available_height(pcl5_ctxt, &pcl_internal_height) ;

  /* N.B. It seems that rounding to nearest pixel here results in
   *      images that are too large.
   */
  *max_height = (uint32) (pcl_internal_height * scale_factor) ;
}


/* Calculate the maximum available width from the left graphics margin to the
 * logical page boundary in PCL internal units.
 */
void calculate_max_available_width(PCL5Context *pcl5_ctxt, PCL5Real *max_width)
{
  PageCtrlInfo *page_info;
  RasterGraphicsInfo *rast_info ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(max_width != NULL, "max_width is NULL") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  page_info = get_page_ctrl_info(pcl5_ctxt) ;

  HQASSERT(rast_info != NULL, "rast_info is NULL") ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  /* Limit the width so that rows end within the logical page boundary */
  if (image_is_width_aligned(pcl5_ctxt))
    *max_width = page_info->page_width - rast_info->left_margin ;
  else
    *max_width = page_info->page_length - rast_info->left_margin ;
}

/* Calculate the maximum source width in image pixels */
static
void calculate_max_source_width(PCL5Context *pcl5_ctxt, uint32 *max_width)
{
  RasterGraphicsInfo *rast_info ;
  PCL5Real scale_factor, pcl_internal_width ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(max_width != NULL, "max_width is NULL") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  scale_factor = rast_info->resolution / 7200.0f ;

  /* Get the width in PCL internal units from left graphics margin
   * to logical page boundary.
   */
  calculate_max_available_width(pcl5_ctxt, &pcl_internal_width) ;

  /* N.B. It seems that rounding to nearest pixel here results in
   *      images that are too large.
   */
  *max_width = (uint32) (pcl_internal_width * scale_factor) ;
}

/* Fill in rast_info->x_scale and rast_info->y_scale */
#ifdef QUANTIZE_IMAGE_SCALING
/* The HP4700 appears to require an integer number of device pixels
 * per source image pixel, (or in some cases an integer number of
 * source pixels per device pixel), if the destination dimension is
 * undefined, (or set to zero).
 *
 * Since this may be calculated separately for the X and Y
 * dimensions, this means that isotropic scaling is not necessarily
 * maintained, in contrast to what is indicated in the PCL5 Color
 * Tech Ref.
 */
static
void calculate_scale_factors(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState *print_state ;
  RasterGraphicsInfo *rast_info ;
  double image_x_size, image_y_size ;
  uint32 hw_resolution ;
  Bool explicit_destination_width, explicit_destination_height ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  print_state = pcl5_ctxt->print_state ;
  HQASSERT(print_state != NULL, "print_state is NULL") ;

  /** \todo Deal with different X and Y resolutions */
  hw_resolution = print_state->hw_resolution[0] ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  HQASSERT(rast_info->explicit_source_width &&
           rast_info->explicit_source_height,
           "Expected explicit source dimensions for scaled image") ;

  HQASSERT(rast_info->source_height != 0 &&
           rast_info->source_width != 0,
           "Unexpected source height or width for scaled image") ;

  /*
   * N.B. All valid image resolutions divide into 7200
   */
  image_x_size = 7200.0 * rast_info->source_width / rast_info->resolution ;
  image_y_size = 7200.0 * rast_info->source_height / rast_info->resolution ;

  /* Zero destination width or height counts as one that isn't set for the
   * purposes of this calculation.
   */
  explicit_destination_width = rast_info->explicit_destination_width &&
                               rast_info->destination_width != 0 ;

  explicit_destination_height = rast_info->explicit_destination_height &&
                                rast_info->destination_height != 0 ;

  /* If neither of them are set. */
  if (! explicit_destination_width && ! explicit_destination_height) {
    double scale_factor ;
    uint32 x_scale_factor, y_scale_factor, device_scale_factor ;
    PCL5Real available_width, available_height ;

    /* The HP4700 appears to require a whole number of device pixels to
     * represent each original image pixel.
     * It works out the maximum number of device pixels that can be used for
     * each image pixel, such that the image will fit within the logical page.
     * (Actually, such that the image will almost fit within the logical page,
     * as it appears to round to the nearest number of whole device pixels.
     * Having worked out this number for X and Y, it then takes the lowest
     * of the two in order to preserve isotropic scaling.
     */

    /* Get the available space on the logical page in PCL internal units */
    calculate_max_available_width(pcl5_ctxt, &available_width) ;
    calculate_max_available_height(pcl5_ctxt, &available_height) ;

    /* How many device pixels could we use for each source pixel? */
    x_scale_factor = (uint32) ((available_width * hw_resolution / (7200.0f * rast_info->source_width)) + 0.5f) ;
    y_scale_factor = (uint32) ((available_height * hw_resolution / (7200.0f * rast_info->source_height)) + 0.5f) ;

    /* Take the smallest number to preserve isotropicity */
    /** \todo What if this is zero? */
    device_scale_factor = min(x_scale_factor, y_scale_factor) ;
    scale_factor = (device_scale_factor * rast_info->resolution) / (double) hw_resolution ;

    /* Cache these values so we can use them for cursor positioning
       when scaling is on. */
    rast_info->x_scale = scale_factor ;
    rast_info->y_scale = scale_factor ;

  } else {   /* At least one destination dimension is set */

    if (explicit_destination_width) {

      HQASSERT(image_x_size != 0, "Unexpected image_x_size") ;
      HQASSERT(rast_info->destination_width != 0, "Zero destination width") ;

      rast_info->x_scale = rast_info->destination_width / image_x_size ;

      if (! explicit_destination_height) {
        /* N.B. The HP4700 works out how many device pixels would
         *      correspond to one source pixel for this scaling, and
         *      depending on whether this is larger than 1, rounds this,
         *      or its inverse(!) to the nearest integer to scale the
         *      dimension that is not explicitly specified.
         */
        double device_pixels_per_source_pixel = (rast_info->x_scale * hw_resolution / (double) rast_info->resolution) ;
        uint32 pixels ;

        if (device_pixels_per_source_pixel >= 1)
          pixels = (uint32) (device_pixels_per_source_pixel + 0.5f) ;
        else
          pixels = (uint32) ((1 / device_pixels_per_source_pixel) + 0.5f) ;

        rast_info->y_scale = pixels * rast_info->resolution / (double) hw_resolution ;
      }
    }

    if (explicit_destination_height) {
      if (rast_info->source_height != 0) {

        HQASSERT(image_y_size != 0, "Unexpected image_y_size") ;
        HQASSERT(rast_info->destination_height != 0.0, "Zero destination height") ;
        rast_info->y_scale = rast_info->destination_height / image_y_size  ;

        if (! explicit_destination_width) {
          /* N.B. The HP4700 works out how many device pixels would
           *      correspond to one source pixel for this scaling, and
           *      depending on whether this is larger than 1, rounds this,
           *      or its inverse(!) to the nearest integer to scale the
           *      dimension that is not explicitly specified.
           */
          double device_pixels_per_source_pixel = (rast_info->y_scale * hw_resolution / (double) rast_info->resolution) ;
          uint32 pixels ;

          if (device_pixels_per_source_pixel >= 1)
            pixels = (uint32) (device_pixels_per_source_pixel + 0.5f) ;
          else
            pixels = (uint32) ((1 / device_pixels_per_source_pixel) + 0.5f) ;

          rast_info->x_scale = pixels * rast_info->resolution / (double) hw_resolution ;
        }
      }
    }
  }
}

#else
/* See comment for alternative code above.  This version maintains isotropicity
 * as per the PCL5 Color Tech Ref.
 */
static
void calculate_scale_factors(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info ;
  double image_x_size, image_y_size ;
  Bool explicit_destination_width, explicit_destination_height ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  HQASSERT(rast_info->explicit_source_width &&
           rast_info->explicit_source_height,
           "Expected explicit source dimensions for scaled image") ;

  HQASSERT(rast_info->source_height != 0 &&
           rast_info->source_width != 0,
           "Unexpected source height or width for scaled image") ;

  /*
   * N.B. All valid image resolutions divide into 7200
   */
  image_x_size = 7200.0 * rast_info->source_width / rast_info->resolution ;
  image_y_size = 7200.0 * rast_info->source_height / rast_info->resolution ;

  /* Zero destination width or height counts as one that isn't set for the
   * purposes of this calculation.
   */
  explicit_destination_width = rast_info->explicit_destination_width &&
                               rast_info->destination_width != 0 ;

  explicit_destination_height = rast_info->explicit_destination_height &&
                                rast_info->destination_height != 0 ;

  /* If neither of them are set. */
  if (! explicit_destination_width && ! explicit_destination_height) {
    double scale_factor, x_scale_factor, y_scale_factor ;
    PCL5Real available_width, available_height ;

    /* Work out the scaling factor to just reach the edge of the logical page
     * for X and Y, and take the lowest of the two to preserve isotropic scaling.
     */

    /* Get the available space on the logical page in PCL internal units */
    calculate_max_available_width(pcl5_ctxt, &available_width) ;
    calculate_max_available_height(pcl5_ctxt, &available_height) ;

    x_scale_factor = available_width * rast_info->resolution / (7200.0f * rast_info->source_width) ;
    y_scale_factor = available_height * rast_info->resolution / (7200.0f * rast_info->source_height) ;

    /* Take the smallest number to preserve isotropicity */
    /** \todo What if this is zero? */
    if (x_scale_factor <= y_scale_factor)
      scale_factor = x_scale_factor ;
    else
      scale_factor = y_scale_factor ;

    /* Cache these values so we can use them for cursor positioning
       when scaling is on. */
    rast_info->x_scale = scale_factor ;
    rast_info->y_scale = scale_factor ;

  } else {   /* At least one destination dimension is set */

    if (explicit_destination_width) {

      HQASSERT(image_x_size != 0, "Unexpected image_x_size") ;
      HQASSERT(rast_info->destination_width != 0, "Zero destination width") ;

      rast_info->x_scale = rast_info->destination_width / image_x_size ;

      if (! explicit_destination_height) {
        rast_info->y_scale = rast_info->x_scale ;
      }
    }

    if (explicit_destination_height) {

      HQASSERT(image_y_size != 0, "Unexpected image_y_size") ;
      HQASSERT(rast_info->destination_height != 0, "Zero destination height") ;

      rast_info->y_scale = rast_info->destination_height / image_y_size  ;

      if (! explicit_destination_width) {
        rast_info->x_scale = rast_info->y_scale ;
      }
    }
  }
}
#endif

static
Bool generate_y_offset_data(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info ;
  PCL5_RasterReader *reader ;
  int32 i ;

  reader = get_raster_reader(pcl5_ctxt) ;
  HQASSERT(reader->reader_mode == EXECUTING_Y_OFFSET, "not executing a y offset") ;
  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  /* If the number of planes seen has not been reset to zero, it means that
   * we've only seen planar data for this row, (no row data), and we are here
   * from a recursive pcl5_execops call in merge_planar_data.
   *
   * In order to allow the pcl5imageread operator to push the image bytes,
   * we need to postpone generating the Y offset data.  We also generate any
   * missing planes at this point, (as ESC*b0V or ESC*b0W).
   *
   * N.B. By analogy with end_raster_graphics_callback, do not postpone
   *      generating Y offset here if there is an explicit source height,
   *      (though difficulty was encountered getting the reference printer
   *      to print such a test job at all).
   */
  if (reader->num_planes_seen > 0 && !rast_info->explicit_source_height) {
    if (planes_are_missing(pcl5_ctxt)) {
      reader->reader_mode = GENERATING_MISSING_PLANES ;
      if (! generate_missing_planes(pcl5_ctxt))
        return FALSE ;
    }
    else {
      reader->num_planes_seen = 0 ;
    }

    reader->reader_mode = EXECUTING_Y_OFFSET ;
    return TRUE ;
  }

  for (i=0; i < rast_info->num_planes; i++) {
    zero_seed_row(reader, i) ;
  }
  if (rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE ||
      rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE) {
    zero_merged_row(reader) ;
  }

  reader->num_bytes = reader->width_in_bytes ;
  theLen(reader->string_object) = (uint16)reader->width_in_bytes ;

  if (--reader->repeat_remaining == 0) {
    reader->reader_mode = DEFAULT_MODE ;
  }

  HQASSERT(reader->repeat_remaining >= 0, "repeat remaining is less than zero") ;
  return TRUE ;
}

static
Bool generate_y_pad(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info ;
  PCL5_RasterReader *reader ;
  int32 i ;

  reader = get_raster_reader(pcl5_ctxt) ;
  HQASSERT(reader->reader_mode == EXECUTING_Y_PAD, "not executing y pad") ;
  rast_info = get_rast_info(pcl5_ctxt) ;

  if (reader->repeat_remaining == 0) {
    reader->num_bytes = 0 ;
    theLen(reader->string_object) = 0 ;
  } else {
    for (i=0; i < rast_info->num_planes; i++) {
      zero_seed_row(reader, i) ;
    }
    if (rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE ||
        rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE) {
      zero_merged_row(reader) ;
    }
    reader->num_bytes = reader->width_in_bytes ;
    theLen(reader->string_object) = (uint16)reader->width_in_bytes ;
    reader->repeat_remaining-- ;
  }

  HQASSERT(reader->repeat_remaining >= 0, "repeat remaining is less than zero") ;
  return TRUE ;
}


/* Generate planes that are missing at e.g. end raster graphics. */
static
Bool generate_missing_planes(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info ;
  PCL5_RasterReader *reader ;

  reader = get_raster_reader(pcl5_ctxt) ;
  HQASSERT(reader != NULL, "reader is NULL") ;
  HQASSERT(reader->reader_mode == GENERATING_MISSING_PLANES,
           "Unexpected reader mode") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;
  HQASSERT(reader->num_planes_seen < rast_info->num_planes,
           "Generating too many planes") ;

  if (reader->num_planes_seen < rast_info->num_planes - 1) {
    /* This will result in generate_missing_planes being called
     * recursively as necessary to generate all the planes. */
    return decode_row_block(pcl5_ctxt, 0, TRUE, TRUE) ;
  }
  else
    return decode_row_block(pcl5_ctxt, 0, FALSE, TRUE) ;
}


/* Are we expecting more planar data? */
/** \todo Can this situation arise with adaptive compression? */
static
Bool planes_are_missing(PCL5Context *pcl5_ctxt)
{
  RasterGraphicsInfo *rast_info ;
  PCL5_RasterReader *reader ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  reader = get_raster_reader(pcl5_ctxt) ;
  HQASSERT(reader != NULL, "reader is NULL") ;
  HQASSERT(reader->num_planes_seen <= rast_info->num_planes,
           "Too many planes seen") ;

  return (( rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE ||
            rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE ) &&
          ( reader->num_planes_seen > 0 &&
            reader->num_planes_seen < rast_info->num_planes )) ;
}


Bool pcl5imageread_(ps_context_t *pscontext)
{
  Bool success ;
  PCL5Context *pcl5_ctxt ;
  PCL5_RasterReader* reader ;
  RasterGraphicsInfo *rast_info ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ((pcl5_ctxt = pcl5_current_context()) == NULL) {
    HQFAIL("This operator should never be called outside of a pcl5exec.") ;
    return error_handler(UNDEFINED) ;
  }

  HQASSERT(pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE,
           "Invalid mode for pcl5imageread.") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;
  reader = get_raster_reader(pcl5_ctxt) ;
  HQASSERT(reader != NULL, "reader is NULL") ;

  switch (reader->reader_mode) {
  case DEFAULT_MODE:
    /* In graphics mode this should read in one scan line of data or hit
       an end of raster graphics command (or a command which causes that
       indirectly). */

    success = pcl5_execops(pcl5_ctxt) ;
    break ;

  case EXECUTING_FROM_PLANE_DATA:
    success = decode_row_block(pcl5_ctxt, reader->read_bytes, TRUE, FALSE) ;
    break ;

  case EXECUTING_FROM_DATA:
    success = decode_row_block(pcl5_ctxt, reader->read_bytes, FALSE, FALSE) ;
    break ;

  case EXECUTING_Y_OFFSET:
    success = generate_y_offset_data(pcl5_ctxt) ;
    break ;

  case EXECUTING_Y_PAD:
    success = generate_y_pad(pcl5_ctxt) ;
    break ;

  /* In progress of an adaptive duplicate row execution. */
  case EXECUTING_ADAPTIVE_DUPLICATE_ROW:
  /* In progress of an adaptive empty row execution. */
  case EXECUTING_ADAPTIVE_EMPTY_ROW:
  /* Ready for the next adaptive command. */
  case EXECUTING_ADAPTIVE_COMMAND:
    {
      int32 out_length = 0 ;

      if (reader->read_bytes > 0 || reader->repeat_remaining > 0) {
        success = expand_adaptive(pcl5_ctxt, reader, &out_length, reader->num_planes_seen) ;
      } else {
        success = TRUE ;
      }

      if (out_length > 0)
        success = success && merge_planar_data(pcl5_ctxt, reader, &out_length, reader->read_bytes, FALSE) ;

      theLen(reader->string_object) = CAST_SIGNED_TO_UINT16(out_length);

      /* Its the last row of the adaptive command which we need to execute. */
      if (reader->read_bytes <= 0 && reader->repeat_remaining == 0) {
        reader->reader_mode = DEFAULT_MODE ;
        reader->adaptive_command = -1 ;
      }
#ifdef ASSERT_BUILD
      else {
        HQASSERT(out_length == reader->width_in_bytes,
                 "adaptive generated row is not correct width") ;
      }
#endif
    }
    break ;
  case EXECUTING_POSTPONED_END_GRAPHICS:
    success = end_raster_graphics_callback(pcl5_ctxt, TRUE) ;
    break ;
  default:
    /* N.B. The pseudo mode GENERATING_MISSING_PLANES should not
     *      be reached here.
     */
    HQFAIL("Invalid reader mode.") ;
    success = FALSE ;
    break ;
  }

  /* Ohh no, we need to abort the image as an error has occured. */
  if (! success) {
    /* We do not know what state the string object is in, so set it
       explicitly to zero length. */
    reader->num_bytes = 0 ;
    theLen(reader->string_object) = 0 ;
  }

  /** \todo Can we set print_state->possible_raster_data_trim here, regardless
   *  of whether the source height is explicit, (since we always work one out
   *  and will need to consume excess data), as soon as the source height
   *  equals the number of rows?  (As opposed to waiting for the
   *  end_raster_graphics_callback)?
   */

  if (success && rast_info->explicit_source_height) {
     /* Do we need to pad in the Y direction? */
    if (theLen(reader->string_object) == 0 &&
        reader->num_rows != rast_info->source_height) {

      reader->repeat_remaining = rast_info->source_height - reader->num_rows ;
      reader->reader_mode = EXECUTING_Y_PAD ;
      success = generate_y_pad(pcl5_ctxt) ;
    }

    /* We need to truncate rows so bomb out now. Ironically, this test
       is never true because we pass the height to the PS image
       operator and that has consumed enough data so does not call
       pcl5imageread_ back. */
    if (rast_info->source_height < reader->num_rows) {
      reader->num_bytes = 0 ;
      theLen(reader->string_object) = 0 ;
    }
  }

  if (success && theLen(reader->string_object) > 0) {
    /* The image may be scaled so wait till the image is finished before
     * moving the cursor down.
     */
    reader->num_rows++ ;
    rast_info->num_rows++ ;
  }

  /* Return the bytes as a string. */
  success = success && push(&(reader->string_object), &operandstack) ;

  return success ;
}

/* ============================================================================
 * Image decoders
 * ============================================================================
 */

#define pad_row(b, l) MACRO_START HqMemZero((int8*)(b), (l)); MACRO_END

/* Each decode expander returns TRUE if successful or FALSE if it
   finds the end of file or some other problem occured. */

/* Returns TRUE on success, FALSE if EOF was found. */
static
Bool expand_unencoded(
  PCL5Context *pcl5_ctxt,
  PCL5_RasterReader *reader,
  int32 num_bytes,
  int32 *out_length,
  int32 which_plane)
{
  uint8*  image_data;
  int32 bytes;

  HQASSERT(which_plane >= 0 && which_plane < MAX_PLANES, "which_plane is out of range") ;
  HQASSERT(reader->zee_bytes[which_plane] != NULL, "NULL pointer to plane data") ;

  image_data = reader->zee_bytes[which_plane];
  if ( num_bytes > 0 ) {
    /* Never read more data than the plane requires */
    INLINE_MIN32(bytes, num_bytes, reader->required_bytes_per_plane);
    if ( file_read(pcl5_ctxt->flptr, image_data, bytes, NULL) <= 0 ) {
      return(FALSE);
    }
    image_data += bytes;
  }

  /* If there are more data bytes then needed then silently consume the excess.
   * If there are fewer data bytes, then pad the row data.  Calculating the
   * difference means the conditions should just become tests on the zero and
   * sign flags.
   */
  bytes = num_bytes - reader->required_bytes_per_plane;
  if ( bytes > 0 ) {
    (void)file_skip(pcl5_ctxt->flptr, bytes, NULL);

  } else if ( bytes < 0 ) {
    pad_row(image_data, -bytes);
  }
  *out_length = reader->required_bytes_per_plane;

  return(TRUE);

} /* expand_unencoded */


/* Returns TRUE on success, FALSE if EOF was found. */
static
Bool expand_rle(
  PCL5Context *pcl5_ctxt,
  PCL5_RasterReader *reader,
  int32 num_bytes,
  int32 *out_length,
  int32 which_plane)
{
  FILELIST* flptr;
  uint8*  image_data;
  int32 bytes_to_fill;
  int32 repeat;
  int32 byte;

  HQASSERT(which_plane >= 0 && which_plane < MAX_PLANES, "which plane is corrupt") ;
  HQASSERT(reader->zee_bytes[which_plane] != NULL, "zee bytes is NULL") ;

  image_data = reader->zee_bytes[which_plane];
  flptr = pcl5_ctxt->flptr;
  bytes_to_fill = reader->required_bytes_per_plane;
  if ( num_bytes > 0 ) {
    /* Simple RLE - a repeat count and a byte to fill memory with */
    do {
      if ( (repeat = Getc(flptr)) == EOF ) {
        return(FALSE);
      }
      if ( --num_bytes > 0 ) {
        if ( (byte = Getc(flptr)) == EOF ) {
          return(FALSE);
        }
        repeat++;
        INLINE_MIN32(repeat, bytes_to_fill, repeat);
        HqMemSet8(image_data, (uint8)(byte&0xFF), repeat);
        image_data += repeat;
        bytes_to_fill -= repeat;
        --num_bytes;
      }
    } while ( (num_bytes > 0) && (bytes_to_fill > 0) );
  }

  if ( num_bytes > 0 ) {
    /* Excess image source data, consume remaining bytes */
    (void)file_skip(flptr, num_bytes, NULL);

  } else if ( bytes_to_fill > 0 ) {
    /* Not enough source data, pad image row */
    pad_row(image_data, bytes_to_fill);
  }
  *out_length = reader->required_bytes_per_plane;

  return(TRUE);

} /* expand_rle */

/* Returns TRUE on success, FALSE if EOF was found. */
static
Bool expand_tiff(
  PCL5Context *pcl5_ctxt,
  PCL5_RasterReader *reader,
  int32 num_bytes,
  int32 *out_length,
  int32 which_plane)
{
  FILELIST* flptr;
  uint8*  image_data;
  int32 bytes_to_fill;
  int32 control;
  int32 byte;

  HQASSERT(which_plane >= 0 && which_plane < MAX_PLANES, "which plane is corrupt") ;
  HQASSERT(reader->zee_bytes[which_plane] != NULL, "zee bytes is NULL") ;

  image_data = reader->zee_bytes[which_plane];
  bytes_to_fill = reader->required_bytes_per_plane;
  flptr = pcl5_ctxt->flptr;
  if ( num_bytes > 0 ) {
    do {
      if ( (control = Getc(flptr)) == EOF ) {
        return(FALSE);
      }
      if ( --num_bytes > 0 ) {
        if ( control == 128 ) {
          continue;
        }
        if ( control > 128 ) {
          /* Run length */
          control = (256 - control) + 1;
          if ( (byte = Getc(flptr)) == EOF ) {
            return(FALSE);
          }
          INLINE_MIN32(control, bytes_to_fill, control);
          HqMemSet8(image_data, (uint8)(byte&0xFF), control);
          --num_bytes;

        } else { /* Literal byte sequence */
          control++;
          INLINE_MIN32(control, num_bytes, control);
          INLINE_MIN32(control, bytes_to_fill, control);
          if ( file_read(flptr, image_data, control, NULL) <= 0 ) {
            return(FALSE);
          }
          num_bytes -= control;
        }
        image_data += control;
        bytes_to_fill -= control;
      }
    } while ( (num_bytes > 0) && (bytes_to_fill > 0) );
  }

  if ( num_bytes > 0 ) {
    /* Excess image source data, consume remaining bytes */
    (void)file_skip(flptr, num_bytes, NULL);

  } else if ( bytes_to_fill > 0 ) {
    /* Not enough source data, pad image row */
    pad_row(image_data, bytes_to_fill);
  }
  *out_length = reader->required_bytes_per_plane;

  return(TRUE);

} /* expand_tiff */

/* Returns TRUE on success, FALSE if EOF was found. */
static
Bool expand_delta(
  PCL5Context*  pcl5_ctxt,
  PCL5_RasterReader*  reader,
  int32 num_bytes,
  int32*  out_length,
  int32 which_plane)
{
  FILELIST* flptr;
  uint8*  image_data;
  uint8*  delta_data;
  int32   bytes_to_fill;
  int32   ch;
  int32   delta_bytes;
  int32   offset;

  HQASSERT(which_plane >= 0 && which_plane < MAX_PLANES, "which plane is corrupt") ;
  HQASSERT(reader->zee_bytes[which_plane] != NULL, "zee bytes is NULL") ;

  /* Its always going to be the full width. */
  *out_length = reader->required_bytes_per_plane;

  if ( num_bytes == 0 ) {
    return(TRUE);
  }

  image_data = reader->zee_bytes[which_plane];
  bytes_to_fill = reader->required_bytes_per_plane;
  flptr = pcl5_ctxt->flptr;

  /* Keep decoding while there is source data and space to fill */
  do {
    /* Read the control byte for the number of replacement bytes and the initial
     * relative offset.
     */
    if ( (ch = Getc(flptr)) == EOF ) {
      return(FALSE);
    }
    if  ( --num_bytes == 0 ) {
      return(TRUE);
    }
    delta_bytes = ((ch & 0xE0) >> 5) + 1;
    offset = (ch & 0x1F);
    /* Read any continuation offset bytes */
    if ( offset == 31 ) {
      do {
        if ( (ch = Getc(flptr)) == EOF ) {
          return(FALSE);
        }
        offset += ch;
      } while ( (--num_bytes > 0) && (ch == 255) );
    }

    if ( num_bytes == 0 ) {
      return(TRUE);
    }
    bytes_to_fill -= offset;
    if ( bytes_to_fill > 0 ) {
      /* Limit number of replacement bytes to what is left in source data and
       * space left to fill.
       */
      INLINE_MIN32(delta_bytes, num_bytes, delta_bytes);
      INLINE_MIN32(delta_bytes, bytes_to_fill, delta_bytes);
      HQASSERT((delta_bytes > 0),
               "expand_delta: trying to copy 0 replacement bytes");

      num_bytes -= delta_bytes;
      bytes_to_fill -= delta_bytes;
      image_data += offset;
      if ( theICount(flptr) > delta_bytes ) {
        /* Enough data in the filter buffer, access directly */
        delta_data = theIPtr(flptr);
        switch ( delta_bytes ) {
        case 8: *image_data++ = *delta_data++;
        case 7: *image_data++ = *delta_data++;
        case 6: *image_data++ = *delta_data++;
        case 5: *image_data++ = *delta_data++;
        case 4: *image_data++ = *delta_data++;
        case 3: *image_data++ = *delta_data++;
        case 2: *image_data++ = *delta_data++;
        case 1: *image_data++ = *delta_data++;
        }
        theIPtr(flptr) = delta_data;
        theICount(flptr) -= delta_bytes;

      } else { /* Source data spans buffers, use Getc to trigger refill */
        do {
          if ( (ch = Getc(flptr)) == EOF ) {
            return(FALSE);
          }
          *image_data++ = (uint8)(ch & 0xff);
        } while ( --delta_bytes > 0 );
      }
    }

  } while ( (num_bytes > 0) && (bytes_to_fill > 0) );

  if ( num_bytes > 0 ) {
    /* Image data row has been covered, consume remaining source bytes */
    (void)file_skip(flptr, num_bytes, NULL);
  }

  return(TRUE);

} /* expand_delta */

/* Returns TRUE on success, FALSE if EOF was found. */
static
Bool expand_adaptive(PCL5Context *pcl5_ctxt, PCL5_RasterReader *reader,
                     int32 *out_length, int32 which_plane)
{
  RasterGraphicsInfo *rast_info ;
  int32 command ;
  int32 highbyte, lowbyte ;
  int32 count ;
  int32 i ;

  HQASSERT(which_plane >= 0 && which_plane < MAX_PLANES,
           "which plane is corrupt") ;
  HQASSERT(reader->zee_bytes[which_plane] != NULL, "zee bytes is NULL") ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  *out_length = 0 ;

  if (reader->reader_mode == EXECUTING_ADAPTIVE_COMMAND) {
    Bool zero_empty_or_duplicate ;
    do {
      zero_empty_or_duplicate = FALSE ;

      if (reader->read_bytes == 0) {
        /* If we have no bytes to read, it seems thats treated as an
           empty row. */
        command = 4 ;
        count = 1 ;
      } else {
        if ( reader->read_bytes-- > 0) {
          if ((command = Getc(pcl5_ctxt->flptr)) == EOF)
            return FALSE ;
        } else {
          return TRUE ;
        }

        if ( reader->read_bytes-- > 0) {
          if ((highbyte = Getc(pcl5_ctxt->flptr)) == EOF)
            return FALSE ;
        } else {
          return TRUE ;
        }

        if ( reader->read_bytes-- > 0) {
          if ((lowbyte = Getc(pcl5_ctxt->flptr)) == EOF)
            return FALSE ;
        } else {
          return TRUE ;
        }

        count = (int32)highbyte ;
        count = count << 8 ;
        count |= (int32)lowbyte ;
      }

      reader->adaptive_command = command ;

      if (command == 4 || command == 5) { /* Empty row or duplicate row */
        reader->repeat_remaining = count ;
        /* Nothing to repeat or generate, so we need to look for the
           next adaptive command otherwise the imagereader_ callback
           will assume its the end of the image. */
        if (count == 0)
          zero_empty_or_duplicate = TRUE ;

        count = 0 ;
      } else {
        reader->repeat_remaining = 0 ;
        /* What happens if the adaptive count tries to inform a decoder that
           there are more bytes available that what the adaptive block has
           left to read? */
        if (count > reader->read_bytes) {
          count = reader->read_bytes ;
        }
      }
    } while (zero_empty_or_duplicate) ;
  } else {
    /* We are executing either a empty row or duplicate row. */
    command = reader->adaptive_command ;
    count = 0 ;
    HQASSERT(command == 4 || command == 5,
             "Unexpected adaptive repeat command.") ;
    HQASSERT(reader->reader_mode == EXECUTING_ADAPTIVE_EMPTY_ROW ||
             reader->reader_mode == EXECUTING_ADAPTIVE_DUPLICATE_ROW,
             "Unexpected repeat adaptive mode.") ;
  }

  switch (command) {
  case 0: /* Unencoded */
    if (! expand_unencoded(pcl5_ctxt, reader, count, out_length, which_plane))
      return FALSE ;
    break ;
  case 1: /* Run-Length Encoding */
    if (! expand_rle(pcl5_ctxt, reader, count, out_length, which_plane))
      return FALSE ;
    break ;
  case 2: /* TIFF */
    if (! expand_tiff(pcl5_ctxt, reader, count, out_length, which_plane))
      return FALSE ;
    break ;
  case 3: /* Delta row */
    if (! expand_delta(pcl5_ctxt, reader, count, out_length, which_plane))
      return FALSE ;
    break ;
  case 4: /* Empty row */
    for (i=0; i < rast_info->num_planes; i++) {
      zero_seed_row(reader, i) ;
    }
    if (rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE ||
        rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE) {
      zero_merged_row(reader) ;
    }
    *out_length = reader->required_bytes_per_plane ;
    reader->repeat_remaining-- ;
    if (reader->repeat_remaining == 0) {
      reader->reader_mode = EXECUTING_ADAPTIVE_COMMAND ;
    } else {
      reader->reader_mode = EXECUTING_ADAPTIVE_EMPTY_ROW ;
    }

    break ;
  case 5: /* Duplicate row */
    *out_length = reader->required_bytes_per_plane ;
    reader->repeat_remaining-- ;
    if (reader->repeat_remaining == 0) {
      reader->reader_mode = EXECUTING_ADAPTIVE_COMMAND ;
    } else {
      reader->reader_mode = EXECUTING_ADAPTIVE_DUPLICATE_ROW ;
    }

    break ;
  default:
    /* If an out-of-range command byte is encountered, the
       remainder of the block is skipped, the cursor is not
       updated, and the seed row is cleared. */
    for (i=0; i < rast_info->num_planes; i++) {
      zero_seed_row(reader, i) ;
    }
    if (rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE ||
        rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE) {
      zero_merged_row(reader) ;
    }

    /* Skip remainder of block. */
    if ( file_skip(pcl5_ctxt->flptr, reader->read_bytes, NULL) <= 0 ) {
      return FALSE ;
    }
    reader->read_bytes = 0;
    return TRUE ;
  }

  reader->read_bytes -= count ;

  HQASSERT(reader->read_bytes >= 0,
           "Tried to read too many adaptive bytes.") ;
  HQASSERT(*out_length == reader->required_bytes_per_plane,
           "generated row is not correct width") ;

  return TRUE ;
}

/* ============================================================================
 * Start/end raster graphics mode.
 * ============================================================================
 */

/* N.B. The raster graphics start mode (of 0, 1, 2, or 3) does not appear to
 *      have any meaning beyond the immediate command, i.e. does not seem to
 *      be part of the MPE.
 */
Bool start_raster_graphics_callback(PCL5Context *pcl5_ctxt, int32 start_mode,
                                    int32 reader_mode, int32 num_bytes, Bool explicit_start)
{
  RasterGraphicsInfo *rast_info ;
  PCL5Real orig_left_margin ;
  uint32 max_width = 0, max_height = 0, physical_width = 0, physical_height = 0 ;
  Bool success ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(!pcl5_ctxt->print_state->possible_raster_data_trim,
           "We shouldn't be starting graphics while trimming") ;

  rast_info = get_rast_info(pcl5_ctxt) ;

  if (rast_info->reader != NULL) {
    HQFAIL("It should be impossible to already have a raster reader.") ;
    return error_handler(UNDEFINED) ;
  }

  /* Although there should not be a reader, we may still be in raster graphics
   * mode if we have not received an explicit end graphics, since the last
   * image.
   */
  if (pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE) {
    HQASSERT(pcl5_ctxt->interpreter_mode_on_start_graphics != MODE_NOT_SET &&
             !pcl5_ctxt->interpreter_mode_end_graphics_pending,
             "Incompatible interpreter modes" );

    pcl5_ctxt->interpreter_mode = pcl5_ctxt->interpreter_mode_on_start_graphics ;
    pcl5_ctxt->interpreter_mode_on_start_graphics = MODE_NOT_SET ;
  }

  /* We keep track of the number of rows of data seen in RasterGraphicsInfo, as
   * it may be needed in the case where no reader is allocated, or we may need
   * it after the reader has been deallocated.
   */
  rast_info->graphics_started = TRUE ;
  rast_info->num_rows = 0 ;

  HQASSERT(rast_info->explicit_source_height ||
           rast_info->orig_source_height == INVALID_VALUE,
           "Source height not set and not invalid") ;

  HQASSERT(rast_info->explicit_source_width ||
           rast_info->orig_source_width == INVALID_VALUE,
           "Source width not set and not invalid") ;

  /* The image is not drawn for zero height or width images, nor is the left
   * graphics margin changed, and no image scaling occurs.  However, in the
   * case of a zero width image, the cursor will be moved down one line per
   * raster data row seen.
   */
  if (rast_info->orig_source_height == 0 ||
      rast_info->orig_source_width == 0)
    return TRUE ;

  /* Set the left graphics margin if required */
  orig_left_margin = rast_info->left_margin ;

  if (explicit_start || rast_info->left_margin == 0 ) {
    set_left_graphics_margin(pcl5_ctxt, (start_mode == 0 || start_mode == 2  || !explicit_start)) ;
  }

  /* Note the original cursor position and initialise the image start
   * position to this.  (If we end up with an image that has zero width
   * we will not need to change this value, nor in fact move to it, as
   * it will be the same as the current cursor position).
   */
  rast_info->orig_cursor_pos = *get_cursor(pcl5_ctxt) ;
  rast_info->start_pos = rast_info->orig_cursor_pos ;

  /* Initialise the scale factors */
  rast_info->x_scale = 1 ;
  rast_info->y_scale = 1 ;

  /* Has the job asked for scaling ? */
  rast_info->raster_scaling = (start_mode == 2 || start_mode == 3) ;

  /* Find out whether scaling will really take place */
  rast_info->raster_scaling = rast_info->raster_scaling &&
                              rast_info->explicit_source_width &&
                              rast_info->explicit_source_height ;

  if (rast_info->raster_scaling) {
    HQASSERT(explicit_start, "Expected explicit start for scaled image") ;
    rast_info->source_width = rast_info->orig_source_width ;
    rast_info->source_height = rast_info->orig_source_height ;
    calculate_scale_factors(pcl5_ctxt) ;
  }
  else {
    /* Calculate the max width and height that will fit the physical page */
    if (!rast_info->explicit_source_width || !rast_info->explicit_source_height)
      physical_width_height(pcl5_ctxt, &physical_width, &physical_height) ;

    /* If we use the maximum source width or height, this limits the image
     * dimensions to the logical page, whereas if no explicit width or height
     * is provided, these appear to be defaulted to whichever is the least
     * restrictive of the physical and logical page limits.
     * (Certainly it can go to the physical page limit, but it appears that
     * the logical page limit is possible too, if it is less restrictive).
     *
     * N.B.   Once the source_width and source_height have been calculated,
     *        they are generally left alone in the case of an implicit start.
     *        The fact that there is an exception for both the unset value
     *        of INVALID_MAGIC and zero, may indicate that the reference
     *        printer uses zero rather than an out of range value.
     *
     * N.N.B. Much of the behaviour for height below has been coded by
     *        analogy with the behaviour for width.
     */
    if (!rast_info->explicit_source_width) {
      /* The left margin must be set before calculating the max source width */
      calculate_max_source_width(pcl5_ctxt, &max_width) ;
      rast_info->source_width = max(physical_width, max_width) ;
    }
    else if (explicit_start ||
             rast_info->source_width == INVALID_MAGIC ||
             rast_info->source_width == 0)  {
      calculate_max_source_width(pcl5_ctxt, &max_width) ;

      /* N.B.   This can change the original source width, so will affect
       *        subsequent images.
       */
      if (max_width == 0)
        rast_info->source_width = 0 ;
      else {
        if (rast_info->orig_source_width > max_width)
          rast_info->orig_source_width = max_width ;

        rast_info->source_width = rast_info->orig_source_width ;
      }
    }

    if (!rast_info->explicit_source_height) {
      calculate_max_source_height(pcl5_ctxt, &max_height) ;
      rast_info->source_height = max(physical_height, max_height) ;
    }
    else if (explicit_start ||
             rast_info->source_height == INVALID_MAGIC ||
             rast_info->source_height == 0) {
      calculate_max_source_height(pcl5_ctxt, &max_height) ;

      /* N.B.   This can change the original source height, so will affect
       *        subsequent images.
       */
      if (max_height == 0)
        rast_info->source_height = 0 ;
      else {
        if (rast_info->orig_source_height > max_height)
          rast_info->orig_source_height = max_height ;

        rast_info->source_height = rast_info->orig_source_height ;
      }
    }
  }

  /* N.B. This behaviour for zero source width has been observed if the
   *      max_width is zero, and extrapolated to apply to all cases.
   *      The behaviour for for zero scale factors is a sensible guess.
   *      Similarly, the behaviour for height has been assumed to match
   *      that for width, but this has not been tested.
   */
  if (rast_info->source_width == 0 || rast_info->x_scale == 0 ||
      rast_info->source_height == 0 || rast_info->y_scale == 0) {
    rast_info->left_margin = orig_left_margin ;
    return TRUE ;
  }

  /* We really intend to draw an image so calculate the PCL start position,
   * take a note of it, and move to it.
   * N.B. It does not appear that RASTER_GRAPHICS_MODE is entered, (e.g. for
   *      the purpose of locking out commands disallowed during graphics),
   *      if we return out of this function above this point.
   */
  calculate_image_start_position(pcl5_ctxt, &rast_info->start_pos) ;
  set_cursor_position(pcl5_ctxt, rast_info->start_pos.x, rast_info->start_pos.y) ;

  /* Fill in info from the active palette */
  set_rast_info_from_palette(pcl5_ctxt) ;

  probe_begin(SW_TRACE_INTERPRET_PCL5_IMAGE, pcl5_ctxt);

  pcl5_ctxt->interpreter_mode_end_graphics_pending = FALSE ;
  pcl5_ctxt->interpreter_mode_on_start_graphics = pcl5_ctxt->interpreter_mode ;
  pcl5_ctxt->interpreter_mode = RASTER_GRAPHICS_MODE ;

  success = pcl5_dispatch_image(pcl5_ctxt, reader_mode, num_bytes, explicit_start) ;

  /* Although we may have had an explicit end graphics command since starting
   * the image, we need to call end graphics again, (as an implicit end
   * graphics), in order to correctly position the cursor, since Y-pad data
   * may have been generated after the explicit end graphics.
   *
   * Note that if no explicit end graphics is seen before this point, the
   * interpreter mode is left as RASTER_GRAPHICS_MODE so that commands that
   * are disallowed during raster graphics will still be disallowed.
   */

  HQASSERT(pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE &&
           pcl5_ctxt->interpreter_mode_on_start_graphics != MODE_NOT_SET,
           "Unexpected interpreter modes") ;

  success = end_raster_graphics_callback(pcl5_ctxt, FALSE) && success ;

  if (pcl5_ctxt->interpreter_mode_end_graphics_pending) {
    /* We have previously seen an explicit end graphics */
    pcl5_ctxt->interpreter_mode = pcl5_ctxt->interpreter_mode_on_start_graphics ;
    pcl5_ctxt->interpreter_mode_on_start_graphics = MODE_NOT_SET ;
    pcl5_ctxt->interpreter_mode_end_graphics_pending = FALSE ;
  }

  probe_end(SW_TRACE_INTERPRET_PCL5_IMAGE, pcl5_ctxt);

  return success ;
}

/* This function gets called when an end of raster graphics occurs
   explicitly or implicitly. NOTE: It can end up being called twice!
   Also, it can end up being called outside of a graphics context as
   it seems seeing a *rC outside of a graphics context DOES reset the
   state. */
Bool end_raster_graphics_callback(PCL5Context *pcl5_ctxt, Bool explicit_end)
{
  PCL5PrintState *print_state = pcl5_ctxt->print_state ;
  PCL5_RasterReader *reader ;
  RasterGraphicsInfo *rast_info ;
  PageCtrlInfo *page_info ;
  CursorPosition* cursor ;
  int32 i ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(explicit_end ||
           (pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE &&
            pcl5_ctxt->interpreter_mode_on_start_graphics != MODE_NOT_SET &&
            pcl5_ctxt->interpreter_mode_on_start_graphics != RASTER_GRAPHICS_MODE),
           "Unexpected interpreter modes in end raster graphics") ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  HQASSERT(page_info != NULL, "page_info is NULL") ;
  cursor = get_cursor(pcl5_ctxt) ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  reader = get_raster_reader(pcl5_ctxt) ;
  HQASSERT(explicit_end || reader == NULL,
           "Unexpected reader for implicit end graphics") ;

  /* If the PS image operator has seen enough bytes, the reader will
     have been deallocated and we would now be in the top level
     pcl5_execops(). */
  if (reader != NULL) {

    /* If the number of planes seen has not been reset to zero, it means that
     * we've only seen planar data for this row, (no row data), and we are here
     * from a recursive pcl5_execops call in merge_planar_data.
     *
     * In order to allow the pcl5imageread operator to push the image bytes,
     * we need to postpone end graphics in this case.  We also generate any
     * missing planes at this point, (as ESC*b0V or ESC*b0W).
     *
     * N.B. Though there doesn't seem to be any logical reason why we couldn't
     *      pad planes here, and then pad missing rows to the source height,
     *      the HP4700 does not appear to pad planes where an explicit source
     *      height is provided, (and hence it may need to pad rows).
     */
    if (reader->num_planes_seen > 0 && !rast_info->explicit_source_height) {
      reader->reader_mode_on_end_graphics = reader->reader_mode ;

      if (planes_are_missing(pcl5_ctxt)) {
        reader->reader_mode = GENERATING_MISSING_PLANES ;

        if (! generate_missing_planes(pcl5_ctxt))
          return FALSE ;
      }
      else {
        pcl5_suspend_execops() ;
        reader->num_planes_seen = 0 ;
      }

      reader->reader_mode = EXECUTING_POSTPONED_END_GRAPHICS ;
      return TRUE ;
    }
    else {
      if (reader->reader_mode == EXECUTING_POSTPONED_END_GRAPHICS) {
        reader->reader_mode = reader->reader_mode_on_end_graphics ;
        reader->reader_mode_on_end_graphics = READER_MODE_NOT_SET ;
      }
      else {
        HQASSERT( reader->reader_mode != EXECUTING_FROM_DATA &&
                  reader->reader_mode != EXECUTING_FROM_PLANE_DATA,
                  "About to suspend the top level PCL5 interpreter") ;
        pcl5_suspend_execops() ;
      }

      reader->num_bytes = 0 ;
      theLen(reader->string_object) = 0 ;

      for (i=0; i < rast_info->num_planes; i++) {
        zero_seed_row(reader, i) ;
      }
      if (rast_info->pixel_encoding_mode == PCL5_INDEXED_BY_PLANE ||
          rast_info->pixel_encoding_mode == PCL5_DIRECT_BY_PLANE) {
        zero_merged_row(reader) ;
      }

      /* If there was no data ensure CAP is set back to what it was */
      if ( reader->reset_cursor ) {
        set_cursor_position(pcl5_ctxt, rast_info->orig_cursor_pos.x, rast_info->orig_cursor_pos.y) ;
      }
    }
  }

  /* N.B.     In fact this condition is probably only met in the case where we
   *          did at least get as far as creating a reader, although it may
   *          have been deallocated before now.
   *
   * N.N.B.   If we have previously had an explicit end graphics, it should not
   *          be necessary to trim raster data, since any new data will form a
   *          new image.
   */
  if (! explicit_end && !pcl5_ctxt->interpreter_mode_end_graphics_pending &&
      (rast_info->num_rows >= rast_info->source_height))
    print_state->possible_raster_data_trim = TRUE ;
  else
    print_state->possible_raster_data_trim = FALSE ;

  /* Set the final PCL cursor position
   * This has already been done for the type of images for which we don't
   * create a raster reader, (although we could do here for those images
   * too).
   */
  HQASSERT(rast_info->num_rows == 0 ||
           ! (rast_info->orig_source_height == 0 ||
              rast_info->orig_source_width == 0 ||
              (rast_info->graphics_started &&
               (rast_info->source_height == 0 || rast_info->y_scale == 0 ||
                rast_info->source_width == 0 || rast_info->x_scale == 0))),
           "Unexpected number of image rows") ;

  move_cursor_down_n_raster_rows(pcl5_ctxt, rast_info->num_rows) ;

  rast_info->graphics_started = FALSE ;
  rast_info->num_rows = 0 ;

  /* If this is an implicit end graphics, (ie. the reader has seen enough
   * bytes), and we have not already seen an explict end graphics, leave
   * the mode as RASTER_GRAPHICS_MODE, because commands that are disallowed
   * during raster graphics still need to be disallowed.
   * If it is an explicit end grapics, only reset the modes if the reader
   * has been deallocated.  If that hasn't quite happened yet, it will be
   * more logical to reset the modes once it has.
   */

  if (explicit_end && pcl5_ctxt->interpreter_mode_on_start_graphics != MODE_NOT_SET ) {
    if (reader == NULL) {
      pcl5_ctxt->interpreter_mode = pcl5_ctxt->interpreter_mode_on_start_graphics ;
      pcl5_ctxt->interpreter_mode_on_start_graphics = MODE_NOT_SET ;
      pcl5_ctxt->interpreter_mode_end_graphics_pending = FALSE ;
    }
    else
      pcl5_ctxt->interpreter_mode_end_graphics_pending = TRUE ;
  }

  return TRUE ;
}

/* ============================================================================
 * Pack a plane into a merged raster row.
 * ============================================================================
 */
static Bool pack_direct_by_plane_bits(uint8 *from_buf, int32 from_length,
                                      uint8 *out, int32 out_length,
                                      int32 left_shift)
{
  uint32 d ;
  uint8 b, *end, *p = out ;
  int32 i, c, zee_shift ;
  ptrdiff_t remaining_bytes ;

  end = out + out_length ;

  /* For each byte in the input plane data. */
  for (c=0; c < from_length; c++) {
    d = 0x00000000 ;
    b = *from_buf++ ;
    zee_shift = left_shift ;

    /* Scan each bit. */
    for (i=0; i < 8; i++) {
      d = d | ((uint32)(b & 0x80) << zee_shift) ;
      zee_shift -= 3 ;
      b = b << 1 ;
    }
    remaining_bytes = end - p ;

    switch (remaining_bytes) {
    case 2:
      *p++ |= (uint8)(d >> 24) ;
      *p++ |= (uint8)(d >> 16) ;
      break ;
    case 1:
      *p++ |= (uint8)(d >> 24) ;
      break ;
    case 0:
      HQFAIL("Truncating image data because merge row is not big enough.") ;
      break ;
    default:
      HQASSERT(remaining_bytes > 0, "Seems we have overrun our buffer.") ;
      *p++ |= (uint8)(d >> 24) ;
      *p++ |= (uint8)(d >> 16) ;
      *p++ |= (uint8)(d >> 8) ;
      break ;
    }
  }
  HQASSERT(p == end,
           "Seems we did not fill the merged data row correctly!") ;

  return TRUE ;
}

static Bool pack_plane_bits(uint8 *from_buf, int32 from_length,
                            uint8 *out, int32 out_length,
                            int32 left_shift, int32 bits_per_index)
{
  uint32 d ;
  uint8 b, *end, *p = out ;
  int32 i, c, zee_shift ;
  ptrdiff_t remaining_bytes ;
  end = out + out_length ;

  if (bits_per_index > 4) {

    /* 5,6,7 and 8 get packed into a single byte. */
    HQASSERT(bits_per_index == 8,
             "bits per index is not 8") ;

    for (c=0; c < from_length; c++) {
      d = 0x00000000 ;
      b = *from_buf++ ;
      zee_shift = left_shift ;

      /* Scan each bit in first nibble. */
      for (i=0; i < 4; i++) {
        if (zee_shift < 0) {
          d = d | ((uint32)(b & 0x80) >> abs(zee_shift)) ;
        } else {
          d = d | ((uint32)(b & 0x80) << zee_shift) ;
        }
        zee_shift -= bits_per_index ;
        b = b << 1 ;
      }

      remaining_bytes = end - p ;

      switch (remaining_bytes) {
      case 7:
        *p++ |= (uint8)(d >> 16) ;
      case 6:
        *p++ |= (uint8)(d >> 8) ;
      case 5:
        *p++ |= (uint8)(d) ;
        break ;
      case 4:
      case 3:
      case 2:
      case 1:
        break ;
      case 0:
        HQFAIL("Truncating image data because merge row is not big enough.") ;
        break ;
      default:
        HQASSERT(remaining_bytes >= 8, "corrupt remaining bytes") ;
        *p++ |= (uint8)(d >> 24) ;
        *p++ |= (uint8)(d >> 16) ;
        *p++ |= (uint8)(d >> 8) ;
        *p++ |= (uint8)(d) ;
        break ;
      }

      d = 0x00000000 ;
      zee_shift = left_shift ;

      /* Scan each bit in second nibble. */
      for (i=4; i < 8; i++) {
        if (zee_shift < 0) {
          d = d | ((uint32)(b & 0x80) >> abs(zee_shift)) ;
        } else {
          d = d | ((uint32)(b & 0x80) << zee_shift) ;
        }
        zee_shift -= bits_per_index ;
        b = b << 1 ;
      }

      /* Do not trash memory, but rather truncate. */
      switch (remaining_bytes) {
      case 3:
        *p++ |= (uint8)(d >> 16) ;
      case 2:
        *p++ |= (uint8)(d >> 8) ;
      case 1:
        *p++ |= (uint8)(d) ;
        break ;
      case 0:
        HQFAIL("Truncating image data because merge row is not big enough.") ;
        break ;
      default:
        HQASSERT(remaining_bytes >= 4, "corrupt remaining bytes") ;
        *p++ |= (uint8)(d >> 24) ;
        *p++ |= (uint8)(d >> 16) ;
        *p++ |= (uint8)(d >> 8) ;
        *p++ |= (uint8)(d) ;
        break ;
      }
    }

  } else { /* 4 or less bits. */

    HQASSERT(bits_per_index == 4 ||
             bits_per_index == 2 ||
             bits_per_index == 1,
             "bits per index is not 1,2 or 4") ;

    /* For each byte in the input plane data. */
    for (c=0; c < from_length; c++) {
      d = 0x00000000 ;
      b = *from_buf++ ;
      zee_shift = left_shift ;

      /* Scan each bit. */
      for (i=0; i < 8; i++) {
        if (zee_shift < 0) {
          d = d | ((uint32)(b & 0x80) >> abs(zee_shift)) ;
        } else {
          d = d | ((uint32)(b & 0x80) << zee_shift) ;
        }
        zee_shift -= bits_per_index ;
        b = b << 1 ;
      }

      /* Be careful not to overrun the merged buffer when dealing with
         an odd number of nibbles. */
      remaining_bytes = end - p ;
      switch (bits_per_index) {
      case 4:
        switch (remaining_bytes) {
        case 3:
          *p++ |= (uint8)(d >> 24) ;
          *p++ |= (uint8)(d >> 16) ;
          *p++ |= (uint8)(d >> 8) ;
          break ;
        case 2:
          *p++ |= (uint8)(d >> 24) ;
          *p++ |= (uint8)(d >> 16) ;
          break ;
        case 1:
          *p++ |= (uint8)(d >> 24) ;
          break ;
        case 0:
          HQFAIL("Truncating image data because merge row is not big enough.") ;
          break ;
        default:
          HQASSERT(remaining_bytes > 0, "Seems we have overrun our buffer.") ;
          *p++ |= (uint8)(d >> 24) ;
          *p++ |= (uint8)(d >> 16) ;
          *p++ |= (uint8)(d >> 8) ;
          *p++ |= (uint8)(d) ;
          break ;
        }
        break ;
      case 2:
        switch (remaining_bytes) {
        case 1:
          *p++ |= (uint8)(d >> 24) ;
          break ;
        case 0:
          HQFAIL("Truncating image data because merge row is not big enough.") ;
          break ;
        default:
          HQASSERT(remaining_bytes > 0, "Seems we have overrun our buffer.") ;
          *p++ |= (uint8)(d >> 24) ;
          *p++ |= (uint8)(d >> 16) ;
          break ;
        }
        break ;
      case 1:
        switch (remaining_bytes) {
        case 0:
          HQFAIL("Truncating image data because merge row is not big enough.") ;
          break ;
        default:
          HQASSERT(remaining_bytes > 0, "Seems we have overrun our buffer.") ;
          *p++ |= (uint8)(d >> 24) ;
          break ;
        }
        break ;
      default:
        HQFAIL("Something wrong with bits per index and number of planes.") ;
        break ;
      }
    }
    HQASSERT(c == from_length, "We have not comsumed all the input data.") ;
  }
  HQASSERT(p == end,
           "Seems we did not fill the merged data row correctly!") ;

  return TRUE ;
}


static
Bool merge_planar_data(PCL5Context *pcl5_ctxt,
                       PCL5_RasterReader *reader,
                       int32 *output_length,
                       int32 num_bytes,
                       Bool is_plane_data)
{
  RasterGraphicsInfo *rast_info ;
  int32 out_length = *output_length ;
  Bool suspend_is_on ;
  Bool success = TRUE ;

  rast_info = get_rast_info(pcl5_ctxt) ;

  if (rast_info->pixel_encoding_mode != PCL5_MONOCHROME) {
      /* The row has been unpacked into zee_bytes but if its planar data,
       * we need to read all the planes (bit shifting them) first before
       * passing any data back to the image operator.
       */

      HQASSERT(out_length == reader->required_bytes_per_plane, "required bytes per plane appears to be wrong") ;
      if (is_plane_data ) {

        switch (rast_info->pixel_encoding_mode) {
        case PCL5_DIRECT_BY_PLANE:
          HQASSERT(rast_info->num_components == 3, "num_component has become corrupt") ;

          switch (reader->num_planes_seen) {
          case 0:
            zero_merged_row(reader) ;

          case 1:
          case 2:
            pack_direct_by_plane_bits(reader->zee_bytes[reader->num_planes_seen], out_length,
                                      reader->merged_row_bytes, reader->width_in_bytes,
                                      24 - reader->num_planes_seen) ;

            reader->num_planes_seen++ ;
            out_length = reader->width_in_bytes ;

            /* Keep going recursive until we have seen all plane data.
             * But if we are generating missing planes, we still need to
             * finish end graphics, so do not suck in more commands now.
             */
            if (reader->reader_mode != GENERATING_MISSING_PLANES ) {
              suspend_is_on = pcl5_get_suspend_state() ;
              success = pcl5_execops(pcl5_ctxt) ;
              if (suspend_is_on)
                pcl5_suspend_execops() ;
            }
            else if (reader->num_planes_seen < rast_info->num_planes) {
              generate_missing_planes(pcl5_ctxt) ;
            }
            break ;
          default:
            HQFAIL("Should never reach here.") ;
            out_length = 0 ;
            break ;
          }
          break ;

        case PCL5_INDEXED_BY_PLANE:
          HQASSERT(rast_info->bits_per_index > 0 &&
                   rast_info->bits_per_index < 9, "bits_per_index has become corrupt") ;
          HQASSERT(rast_info->num_components == 1, "num_component has become corrupt") ;

          switch (reader->num_planes_seen) {
            int32 left_shift ;
          case 0: /* doing 1st plane */
            zero_merged_row(reader) ;

          case 1: /* doing 2nd plane */
          case 2: /* doing 3rd plane */
          case 3: /* doing 4th plane */
          case 4: /* doing 5th plane */
          case 5: /* doing 6th plane */
          case 6: /* doing 7th plane */

            left_shift = 24 - (rast_info->ps_bits_per_index - 1) + reader->num_planes_seen ;
            pack_plane_bits(reader->zee_bytes[reader->num_planes_seen], out_length,
                            reader->merged_row_bytes, reader->width_in_bytes,
                            left_shift, rast_info->ps_bits_per_index) ;

            reader->num_planes_seen++ ;
            out_length = reader->width_in_bytes ;

            /* Keep going recursive until we have seen all plane data.
             * But if we are generating missing planes, we still need to
             * finish end graphics, so do not suck in more commands now.
             */
            if (reader->reader_mode != GENERATING_MISSING_PLANES ) {
              suspend_is_on = pcl5_get_suspend_state() ;
              success = pcl5_execops(pcl5_ctxt) ;
              if (suspend_is_on)
                pcl5_suspend_execops() ;
            }
            else if (reader->num_planes_seen < rast_info->num_planes) {
              generate_missing_planes(pcl5_ctxt) ;
            }
            break ;

          default:
            HQFAIL("Should never reach here.") ;
            out_length = 0 ;
            break ;
          }
          break ;

        default:
          /* What do we do if we see planar data but we have not been told
             that we are to use a planar image. */
          reader->num_planes_seen++ ;
          out_length = reader->width_in_bytes ;

          /* Keep going recursive until we have seen all plane data
             but throw this data out. */
          suspend_is_on = pcl5_get_suspend_state() ;
          success = pcl5_execops(pcl5_ctxt) ;
          if (suspend_is_on)
            pcl5_suspend_execops() ;
          break ;
        }

      } else {
        /* Not a plane data command */
        switch (rast_info->pixel_encoding_mode) {
        case PCL5_DIRECT_BY_PLANE:
          HQASSERT(rast_info->num_components == 3, "num_component has become corrupt") ;

          switch (reader->num_planes_seen) {
          case 0: /* Dealing with 1st plane. */
            if ( (rast_info->compression_method == 3 /* Delta row compression. */ &&
                  num_bytes == 0)) {
              out_length = reader->width_in_bytes ;
              break ;
            }
            zero_merged_row(reader) ;
          case 1: /* Not enough planes seen. */
          case 2: /* Dealing with 3rd plane. */
            pack_direct_by_plane_bits(reader->zee_bytes[reader->num_planes_seen], out_length,
                                      reader->merged_row_bytes, reader->width_in_bytes,
                                      24 - reader->num_planes_seen) ;

            if (reader->num_planes_seen != (rast_info->num_planes - 1)) {
              /* So we just pretend we have seen them */
              reader->num_planes_seen = rast_info->num_planes - 1 ;
            }

            reader->num_planes_seen++ ;
            out_length = reader->width_in_bytes ;
            break ;
          default:
            HQFAIL("Should never reach here.") ;
            out_length = 0 ;
            break ;
          }
          break ;

        case PCL5_INDEXED_BY_PLANE:
          HQASSERT(rast_info->bits_per_index > 0 &&
                   rast_info->bits_per_index < 9, "bits_per_index has become corrupt") ;
          HQASSERT(rast_info->num_components == 1, "num_component has become corrupt") ;

          switch (reader->num_planes_seen) {
            int32 left_shift ;
          case 0: /* Doing 1st plane. */
            if ( (rast_info->compression_method == 3 /* Delta row compression. */ &&
                  num_bytes == 0) ) {
              out_length = reader->width_in_bytes ;
              break ;
            }
            zero_merged_row(reader) ;
          case 1: /* doing 2nd plane */
          case 2: /* doing 3rd plane */
          case 3: /* doing 4th plane */
          case 4: /* doing 5th plane */
          case 5: /* doing 6th plane */
          case 6: /* doing 7th plane */
          case 7: /* doing 8th plane */
            left_shift = 24 - (rast_info->ps_bits_per_index - 1) + reader->num_planes_seen ;
            pack_plane_bits(reader->zee_bytes[reader->num_planes_seen], out_length,
                            reader->merged_row_bytes, reader->width_in_bytes,
                            left_shift, rast_info->ps_bits_per_index) ;

            if (reader->num_planes_seen != (rast_info->num_planes - 1)) {
              /* So we just pretend we have seen them */
              reader->num_planes_seen = rast_info->num_planes - 1 ;
            }

            reader->num_planes_seen++ ;
            out_length = reader->width_in_bytes ;
            break ;

          default:
            HQFAIL("Should never reach here.") ;
            out_length = 0 ;
            break ;
          }
          break ;

        case PCL5_DIRECT_BY_PIXEL:
          out_length = reader->width_in_bytes ;
          break ;

        case PCL5_INDEXED_BY_PIXEL:
          out_length = reader->width_in_bytes ;
          break ;

        default:
          /* Pixel encoding does not involve planes so nothing to do. */
          out_length = 0 ;
          break ;
        }

        /* Seeing a raster data command always resets the number of planes
           seen to zero. */
        reader->num_planes_seen = 0 ;
      }

  } else { /* We have a 1 bit monochrome image. */
      if (is_plane_data) {
        reader->num_planes_seen++ ;
        /* Keep going recursive until we have seen all plane data
           but throw this data out. */
        suspend_is_on = pcl5_get_suspend_state() ;
        success = pcl5_execops(pcl5_ctxt) ;
        if (suspend_is_on)
          pcl5_suspend_execops() ;
      } else {
        reader->num_planes_seen = 0 ;
      }
  }

  *output_length = out_length ;
  return success ;
}

/* ============================================================================
 * Decode a row of data.
 * ============================================================================
 */

/* This gets called once per transfer data by row/block command. */
static
Bool decode_row_block(PCL5Context *pcl5_ctxt, int32 num_bytes, Bool is_plane_data, Bool pcl_cmd)
{
  PCL5_RasterReader* reader ;
  RasterGraphicsInfo *rast_info ;
  int32 out_length ;
  PCL5PrintState *print_state = pcl5_ctxt->print_state ;
  Bool success = TRUE ;
  Bool suspend_is_on ;

  rast_info = get_rast_info(pcl5_ctxt) ;
  reader = get_raster_reader(pcl5_ctxt) ;

  /* Handle zero height and width before checking if reader is NULL */
  if (image_has_zero_dimension(pcl5_ctxt)) {
    HQASSERT(reader == NULL, "Unexpected raster reader") ;
    decode_row_block_for_zero_sized_image(pcl5_ctxt, num_bytes, is_plane_data) ;
    return TRUE ;
  }

  /* If the reader is NULL, it means we have graphics data without
   * having seen a start raster graphics command.
   *
   * N.B. Raster scaling only takes place for explicit start raster
   *      graphics.
   */
  if (reader == NULL) {
    /* Its possible that we are trimming rows of an already rendered
       image, so check to see if the previous command was a raster
       command. If it was, we want to simply throw the remaining image
       data out. */
    if (print_state->possible_raster_data_trim) {
      if ( num_bytes > 0 ) {
        (void)file_skip(pcl5_ctxt->flptr, num_bytes, NULL);
      }
      return TRUE ;
    }

    /* N.B. Although a start mode of 0 is passed in here, any non-zero
     *      left grapics margin will be respected for an implicit start
     *      raster graphics command.
     */
    success = start_raster_graphics_callback(pcl5_ctxt, 0,
                                             is_plane_data ? EXECUTING_FROM_PLANE_DATA : EXECUTING_FROM_DATA,
                                             num_bytes, FALSE) ;

    if (image_has_zero_dimension(pcl5_ctxt)) {
      HQASSERT(get_raster_reader(pcl5_ctxt) == NULL, "Unexpected raster reader") ;
      decode_row_block_for_zero_sized_image(pcl5_ctxt, num_bytes, is_plane_data) ;
    }
    else {
      /* Having had some data the cursor is regarded as explicitly set */
      mark_cursor_explicitly_set(pcl5_ctxt) ;
    }

    return success ;

  } else if ( pcl_cmd ) {
    reader->read_bytes = num_bytes;
    reader->reset_cursor = (num_bytes == 0) && reader->reset_cursor;
  }

  /* If we are reading this row because the image has been started by
     seeing a raster data command without having seen a start raster
     graphics command, it means we are within the top level PCL5
     interpreter and hence do NOT want to suspend that. */
  if (reader->reader_mode != EXECUTING_FROM_DATA &&
      reader->reader_mode != EXECUTING_FROM_PLANE_DATA)
    pcl5_suspend_execops() ;
  else
    reader->reader_mode = DEFAULT_MODE ;

  /* Are we receiving too many planes? */
  if (reader->num_planes_seen == rast_info->num_planes) {
    HQASSERT(reader->reader_mode != GENERATING_MISSING_PLANES,
             "Generating too many planes") ;

    /* Throw this data out. */
    if ( reader->read_bytes > 0 ) {
      (void)file_skip(pcl5_ctxt->flptr, reader->read_bytes, NULL);
    }
    reader->read_bytes = 0;

    if (is_plane_data) {
      /* But we need to go recursive again as there may be more plane
         data or the last plane might be sent. */

      suspend_is_on = pcl5_get_suspend_state() ;
      success = pcl5_execops(pcl5_ctxt) ;
      if (suspend_is_on)
        pcl5_suspend_execops() ;

    } else {
      /* Seeing a raster data command always resets the number of planes
         seen to zero. */
      reader->num_planes_seen = 0 ;
    }
    out_length = reader->width_in_bytes ;
    return TRUE ;
  }

  HQASSERT((num_bytes >= 0),
           "read too much stream data - please report job to core group");
  if (num_bytes < 0)
    return TRUE ;

  /* Having had some data the cursor is regarded as explicitly set */
  mark_cursor_explicitly_set(pcl5_ctxt) ;

  out_length = 0 ;

  /** \todo It is not totally clear it always does this when padding missing
   *        planes, as opposed to e.g just padding with zeroes.  Needs more
   *        investigation.
   */
  switch (rast_info->compression_method) {

    /* ------------------------------------------------------- */
  case 0: /* Unencoded. */
    if (! expand_unencoded(pcl5_ctxt, reader, num_bytes, &out_length, reader->num_planes_seen)) {
      return TRUE ;
    }
    reader->read_bytes -= num_bytes;
    break ;

    /* ------------------------------------------------------- */
  case 1: /* Run-length encoding. */
    if (! expand_rle(pcl5_ctxt, reader, num_bytes, &out_length, reader->num_planes_seen)) {
      return TRUE ;
    }
    reader->read_bytes -= num_bytes;
    break ;

    /* ------------------------------------------------------- */
  case 2: /* Tagged Imaged File Format (TIFF) rev 4.0 */
    if (! expand_tiff(pcl5_ctxt, reader, num_bytes, &out_length, reader->num_planes_seen)) {
      return TRUE ;
    }
    reader->read_bytes -= num_bytes;
    break ;

    /* ------------------------------------------------------- */
  case 3: /* Delta row compression. */
    if (! expand_delta(pcl5_ctxt, reader, num_bytes, &out_length, reader->num_planes_seen)) {
      return TRUE ;
    }
    reader->read_bytes -= num_bytes;
    break ;

    /* ------------------------------------------------------- */
  case 5: /* Adaptive compression. */
    reader->read_bytes = num_bytes ;
    reader->reader_mode = EXECUTING_ADAPTIVE_COMMAND ;
    if (! expand_adaptive(pcl5_ctxt, reader, &out_length, reader->num_planes_seen)) {
      return TRUE ;
    }
    break ;

  default:
    HQFAIL("Illegal compression method should not be possible.") ;
    return FALSE ;
  }

  /* ----------------------------------------------------------------------- */
  /* Deal with planar data which needs merging. */
  /* ----------------------------------------------------------------------- */

  if (out_length > 0) {
    success = merge_planar_data(pcl5_ctxt, reader, &out_length, num_bytes, is_plane_data) ;
  }

  HQASSERT(out_length == reader->width_in_bytes ||
           out_length == 0,
           "generated row is not correct width") ;

  reader->num_bytes = out_length ;
  theLen(reader->string_object) = CAST_SIGNED_TO_UINT16(out_length);

  return success ;
}


/* This is analogous to decode_row_block.
 * Throw the data away and set or move the cursor as appropriate.
 */
static
void decode_row_block_for_zero_sized_image(PCL5Context *pcl5_ctxt, int32 num_bytes, Bool is_plane_data)
{
  RasterGraphicsInfo *rast_info = get_rast_info(pcl5_ctxt) ;

  HQASSERT(image_has_zero_dimension(pcl5_ctxt), "Unexpected image size") ;

  if ( num_bytes > 0 )
    (void)file_skip(pcl5_ctxt->flptr, num_bytes, NULL);

  if (rast_info->orig_source_height != 0)
    mark_cursor_explicitly_set(pcl5_ctxt) ;

  if (! image_has_zero_height(pcl5_ctxt)) {

    /* It appears that the cursor is still moved down the appropriate number of
     * raster rows, where any scaling is ignored.
     * (The QL CET ref for PCL5c CET 21-09 page 10 shows the cursor being moved
     * down here for each plane, but this seems wrong, and other printers agree
     * with us).
     */
    /** \todo It may be necessary to pad and move the cursor down for plane
     *        data too, (if the wrong number of planes provided - although
     *        in order to know this was the case, we would need to count the
     *        planes in the RasterGraphicsInfo as well as the image reader,
     *        since the reader is NULL when we call this function).
     */
    if (!is_plane_data)
      move_cursor_down_n_raster_rows(pcl5_ctxt, 1) ;
  }
}

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

/*
ESC * t # R

# = 75 - 75 dots-per-inch
   100 - 100 dots-per-inch
   200 - 200 dots-per-inch
   150 - 150 dots-per-inch
   300 - 300 dots-per-inch
   600 - 600 dots-per-inch

   Default = 75
   Range = 75, 100, 150, 200, 300, 600

This command must be sent prior to the start graphics command. The
factory default resolution is 75 dots-per-inch. */
Bool pcl5op_star_t_R(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;
  uint32 resolution = (uint32)value.real ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*t%dR - Set raster resolution", resolution));

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  rast_info = get_rast_info(pcl5_ctxt) ;

  switch (resolution) {
  case 75:
  case 100:
  case 150:
  case 200:
  case 300:
  case 600:
    break ;
  default:
    if (resolution < 75) {
      resolution = 75 ;
    } else if (resolution < 100) {
      resolution = 100 ;
    } else if (resolution < 150) {
      resolution = 150 ;
    } else if (resolution < 200) {
      resolution = 200 ;
    } else if (resolution < 300) {
      resolution = 300 ;
    } else {
      /* All invalid values above 300 set the resolution to 600. */
      resolution = 600 ;
    }
    break ;
  }

  rast_info->resolution = resolution ;

  return TRUE ;
}

/* Presentation Mode */
/* N.B. It doesn't look like this defaults the left graphics margin. */
Bool pcl5op_star_r_F(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;
  uint32 presentation = (uint32)value.real ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*r%dF - Presentation mode", presentation));

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  rast_info = get_rast_info(pcl5_ctxt) ;

  switch (presentation) {
  case 0:
  case 3:
    rast_info->presentation = presentation ;
    break ;
  default:
    return TRUE ;
  }

  if (presentation == 0) {
    /* The 4250 resets both width and height */
    if (!pcl5_ctxt->pcl5c_enabled) {
      /** \todo Could omit resetting width in agreement with HP4700 code below? */
      rast_info->orig_source_width = INVALID_VALUE ;
      rast_info->explicit_source_width = FALSE ;
      rast_info->orig_source_height = INVALID_VALUE ;
      rast_info->explicit_source_height = FALSE ;

      /* Reset the following for completeness */
      rast_info->source_width = INVALID_MAGIC ;
      rast_info->source_height = INVALID_MAGIC ;
    }
    else {
      /* The 4700 only resets them if they are not both set */
      if (!rast_info->explicit_source_width || !rast_info->explicit_source_height) {

#ifdef PRESENTATION_0_RESETS_5c_WIDTH
        /* Resetting this value here, gives black bars on PCL5c FTS 103,
         * (as per the 4700), which may be seen as undesirable.
         */
        /** \todo Given this should we also omit to reset the height? */
        rast_info->orig_source_width = INVALID_VALUE ;
        rast_info->explicit_source_width = FALSE ;

        /* Reset the following for completeness */
        rast_info->source_width = INVALID_MAGIC ;
#endif

        rast_info->orig_source_height = INVALID_VALUE ;
        rast_info->explicit_source_height = FALSE ;

        /* Reset the following for completeness */
        rast_info->source_height = INVALID_MAGIC ;
      }
    }
  }

  return TRUE ;
}

/* Source Height */
Bool pcl5op_star_r_T(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;
  uint32 height ;

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  rast_info = get_rast_info(pcl5_ctxt) ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*r%dT - Image source height", value.integer));

  /* Negative values are treated as positive, and assume the value is
   * limited, by analogy with source width.
   */
  height = (uint32)pcl5_limit_to_range(abs(value.integer), 0, 32767) ;

  /* Reset this so that it will be recalculated */
  rast_info->source_height = INVALID_MAGIC ;

  rast_info->orig_source_height = height ;
  rast_info->explicit_source_height = TRUE ;

  return TRUE ;
}

/* Source Width */
Bool pcl5op_star_r_S(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;
  uint32 width ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*r%dS - Image source width", value.integer));

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is NULL") ;

  /* Negative values are treated as positive and the width appears to be
   * limited, (although it was not possible to test the exact value of
   * the limit).
   */
  width = (uint32)pcl5_limit_to_range(abs(value.integer), 0, 32767) ;

  /* Reset this so that it will be recalculated */
  rast_info->source_width = INVALID_MAGIC ;

  rast_info->orig_source_width = width ;
  rast_info->explicit_source_width = TRUE ;

  return TRUE ;
}


/* Image destination height */
Bool pcl5op_star_t_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;

  UNUSED_PARAM(int32, explicit_sign) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*t%fV - Image destination height", value.real));

  /* Ignore negative values by analogy with image destination width */
  /** \todo How many decimal places are allowed, and does rounding take place
   *  before limit testing?
   */
  if (value.real < 0.0)
    return TRUE ;

  /* Limit to range by analogy with image destination width */
  value.real = pcl5_limit_to_range(value.real, 0, 32767) ;

  rast_info = get_rast_info(pcl5_ctxt) ;

  /* Store the destination height in PCL internal units */
  rast_info->destination_height = 10 * value.real ;
  rast_info->explicit_destination_height = TRUE ;

  return TRUE ;
}

/* Image destination width */
Bool pcl5op_star_t_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*t%fH - Image destination width", value.real));

  /** \todo How many decimal places are allowed, and does rounding take place
   *  before limit testing ?
   */
  if (value.real < 0.0)
    return TRUE ;

  /* Although negative values are ignored, positive values appear to be limited,
   * (although it was not possible to test the exact value of the limit).
   */
  value.real = pcl5_limit_to_range(value.real, 0, 32767) ;

  rast_info = get_rast_info(pcl5_ctxt) ;

  /* Store the destination width in PCL internal units */
  rast_info->destination_width = 10 * value.real ;
  rast_info->explicit_destination_width = TRUE ;

  return TRUE ;
}

/* Unable to find documentation for this command so far. */
Bool pcl5op_star_t_K(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  return TRUE ;
}

/* Start raster graphics command. */
Bool pcl5op_star_r_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 start_mode = abs(value.integer) ;  /* Treat negative values as positive */

  UNUSED_PARAM(int32, explicit_sign) ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*r%dA - Image start", value.integer));

  /* Out of range values appear to be treated as zero */
  if (start_mode > 3)
    start_mode = 0 ;

  return start_raster_graphics_callback(pcl5_ctxt, start_mode, DEFAULT_MODE,
                                        /* zero bytes of data */ 0, TRUE) ;
}

/* Image Y Offset */
Bool pcl5op_star_b_Y(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PCL5_RasterReader *reader = get_raster_reader(pcl5_ctxt) ;
  int32 num_raster_lines = abs(value.integer);  /* Treat negative values as positive */

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*b%dY - Image Y offset", num_raster_lines));

  UNUSED_PARAM(int32, explicit_sign) ;

  HQASSERT(reader == NULL ||
           !pcl5_ctxt->print_state->possible_raster_data_trim,
           "If we're trimming data, the reader should have been deallocated.") ;

  if (reader == NULL)
    return TRUE ;     /* Temporary safety */

  /* The command does not appear to be ignored for values over 32767,
   * so assume they are limited to range.
   */
  num_raster_lines = (int32) pcl5_limit_to_range(num_raster_lines, 0, 32767) ;

  /* N.B. If the source_width or the original_source_width is zero, some
   *      printers move the cursor down by the number of lines in this
   *      command before returning.  (It was not investigated whether this
   *      is limited by the image height).
   *
   *      In many ways it seems the logical thing to do to move the cursor
   *      down here, (see also decode_row_block), but the reference printer
   *      does not appear to do it.
   */

  /* No data will be generated so we need to look for the next
     operator, so do not suspend the interpreter. */
  if (num_raster_lines == 0)
    return TRUE ;

  pcl5_suspend_execops() ;

  reader->reader_mode = EXECUTING_Y_OFFSET ;
  reader->repeat_remaining = num_raster_lines ;

  return generate_y_offset_data(pcl5_ctxt) ;
}

/* Set compression method. */
/* N.B. When doing planar data changing compression mode between planes appears
 *      to be allowed, and does not cause the remaining (missing) planes to be
 *      generated.
 */
Bool pcl5op_star_b_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;
  uint32 compression_method = (uint32)value.real ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*b%dM - Image compression", compression_method));

  UNUSED_PARAM(int32, explicit_sign) ;

  rast_info = get_rast_info(pcl5_ctxt) ;

  switch (compression_method) {
  case 0: /* Unencoded. */
  case 1: /* Run-length encoding. */
  case 2: /* Tagged Imaged File Format (TIFF) rev 4.0 */
  case 3: /* Delta row compression. */
  case 5: /* Adaptive compression. */
    rast_info->compression_method = compression_method ;
    break;
  }

  return TRUE ;
}

/* QL CET 21.06 test shows that image data is truncated at 32767 */

Bool pcl5op_star_b_V(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;
  int32 num_bytes;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*b%dV - Image plane data", value.integer));

  UNUSED_PARAM(int32, explicit_sign) ;
  rast_info = get_rast_info(pcl5_ctxt) ;
  HQASSERT(rast_info != NULL, "rast_info is Null") ;

  num_bytes = min(value.integer, 32767);

  /* Adaptive compression is not suitable for planar data, so
   * silently pretend this is row data.
   */
  /** \todo There are indications that the above is true, but
   *  some more testing with the reference printer in this
   *  area may be beneficial.
   */
  if (rast_info->compression_method == 5)
    return decode_row_block(pcl5_ctxt, num_bytes, FALSE, TRUE) ;
  else
    return decode_row_block(pcl5_ctxt, num_bytes, TRUE, TRUE) ;
}

Bool pcl5op_star_b_W(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  int32 num_bytes;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*b%dW - Image block data", value.integer));

  UNUSED_PARAM(int32, explicit_sign) ;

  num_bytes = min(value.integer, 32767);

  return decode_row_block(pcl5_ctxt, num_bytes, FALSE, TRUE) ;
}

/* End raster graphics command. Older version. See pcl5op_star_r_C() */
/* N.B. The end_raster_graphics_callback is called from the scanner, in
 *      the same way as it is for other commands that end graphics.
 */
Bool pcl5op_star_r_B(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*r%dB - Image end", value.integer));

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  return TRUE ;
}

/* End raster graphics command. New version. See pcl5op_star_r_B() */
/* N.B. The end_raster_graphics_callback is called from the scanner, in
 *      the same way as it is for other commands that end graphics.
 */
Bool pcl5op_star_r_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  RasterGraphicsInfo *rast_info ;

  TRACE(debug_pcl5 & PCL5_IMAGEOPS,
        ("Esc*r%dC - Image end", value.integer));

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  rast_info = get_rast_info(pcl5_ctxt) ;

  /* N.B. Old command and implicit end raster graphics don't default
     the left graphics margin. */
  rast_info->left_margin = 0 ;

  /* Set compression mode to zero. */
  rast_info->compression_method = PCL5_UNENCODED ;

  return TRUE ;
}

/* ============================================================================
* Log stripped */
