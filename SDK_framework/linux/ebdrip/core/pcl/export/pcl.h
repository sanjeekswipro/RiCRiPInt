/**
 * \file
 * \ingroup pcl
 *
 * $HopeName: COREpcl_base!export:pcl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL shared interface between PCL XL, PCL5c, PCL5e and HPGL2
 */

#ifndef __PCL_H__
#define __PCL_H__ 1

#include "fileioh.h"
#include "gu_ctm.h"
#include "objectt.h"
#include "matrix.h"

/**
 * \defgroup pcl PCL common interfaces.
 * \ingroup core
 * \{
 */

/* 1/6th of an inch at 7200 dpi */
#define PASS_THROUGH_MARGIN 1200

/* Pass through types. PCLXL_NO_PASS_THROUGH must be zero as we will
   use this value in boolean tests. */
enum {
  PCLXL_NO_PASS_THROUGH = 0,
  PCLXL_WHOLE_JOB_PASS_THROUGH = 1,
  PCLXL_SNIPPET_JOB_PASS_THROUGH = 2,
};

/**
 * An instance of the following structure is
 * created by the PCLXL interpreter when it detects the first
 * PCL[5]Passthrough operator in a PCLXL job.
 *
 * It partially completes/initializes this structure
 * and then passes it through to the PCL5 interpreter
 * for this and any subsequent PCL[5]Passthrough operators
 * that it encounters within this same [PCLXL] job.
 *
 * The PCL5 interpreter is expected to take this
 * partially initialized structure and,
 * if this is the first passthrough usage of this structure instance,
 * complete its own parts of this structure.
 *
 * The PCL5 interpreter uses the shared initial data items:
 * current_point, units_per_measure, physical page transformation matrix,
 * save object, font selection criteria (including whether or not these criteria
 * have changed inside PCLXL since the last Passthrough call),
 * current page number, duplex settings, media size and orientation
 * both requested and actual)
 * and proceeds with the handling of the passthrough PCL5 data.
 *
 * The PCL5 interpreter does not get any "warning" that it has reached
 * the end of the passthrough data.
 *
 * So to avoid it having to continuously maintain both its own state data
 * *AND* maintain all the values in this passthrough state data,
 * immediately after return to the PCLXL interpreter control
 * the PCLXL interpreter will make a second call(back) into the PCL5 interpreter
 * asking it to update the values in the passthrough state data
 */

typedef struct PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO {

  struct pclxl_context_struct*  pclxl_context;
  struct PCL5Context*           pcl5_ctxt;

  Bool        current_point;
  SYSTEMVALUE current_point_x;
  SYSTEMVALUE current_point_y;

  SYSTEMVALUE units_per_measure_x;
  SYSTEMVALUE units_per_measure_y;

  OMATRIX physical_page_ctm;     /* A *copy* of PCLXL's graphics state "physical page" CTM
                                   * which basically holds the full page printable area
                                   * with a top-left-y-increasing-downwards origin
                                   * with the page dimensions expressed in 1/7200ths of an inch
                                   *
                                   * PCL5 Passthrough actually picks its "physical page CTM"
                                   * from the current CTM, but it additionally maintains
                                   * this (copy of the) PCLXL physical page dimension
                                   * whenever it performs a page size/orientation/duplexing change
                                   *
                                   * This is so that PCLXL can pick up these changes
                                   * upon return from the pass through
                                   * IFF there are some marks upon the page
                                   *
                                   * IFF there are no marks on the page, then PCLXL
                                   * reverts to its own previous notion of the current page set-up
                                   */
  Bool    use_pcl5_page_clip;     /* Before entering a [PCL5] PassThrough, PCLXL saves its own current clip path
                                   * and then releases the current clip path back to be the whole [PCLXL] printable page.
                                   *
                                   * At the end of a "snippet" passthrough, PCLXL always re-instates its own original clip path
                                   *
                                   * But at the end of a whole-job passthrough,
                                   * iff there has been a PCL5 page size change or orientation change
                                   * then upon return, PCLXL may choose to "inherit" the page size/orientation change
                                   * and/or inherit the PCL5 page clip rather than re-instating its own saved clip path
                                   *
                                   * use_pcl5_clip_path is set to FALSE by pclxl_init_passthrough_state_info()
                                   * and then conditionally set to TRUE by handle_pcl5_page_change.
                                   *
                                   * Thereafter it is upto PCLXL whether it chooses to use its own saved clip path
                                   * or whether it chooses to use a PCLXL whole page printable area clip
                                   * or whether it sets up a clip path just inside the page border
                                   * by this a numberof 1/7200ths of an inch in from each edge of the (portrait) page
                                   */
  Bool    use_pcl5_page_setup;    /* Again this is initialized to FALSE by pclxl_init_passthrough_state_info()
                                   * And conditionally set to TRUE iff there is a page size or orientation change or PCL5 "reset"
                                   * And again it is upto PCLXL whether or not it chooses to accept this "hint" from PCL5
                                   */
  int32   pcl5_page_clip_offsets[4];
                                  /* 4 offsets in 1/7200ths of an inch in from the left, right, top and bottom of the portrait page */

  OBJECT* pclxl_save_object;      /* Pointer back to PCLXL's Postscript "save" object */

                                  /* Last PCL5 fontselection criteria */
  Bool    font_sel_criteria_changed; /* Set to true when *either* PCLXL or PCL5 makes a change
                                      * to any of the following individual selection criteria
                                      * to inform the other side of the passthrough that
                                      * their (copy of) these selection criteria are now
                                      * out of date
                                      */
  int32   font_sel_initialized;
  int32   font_sel_symbol_set;
  int32   font_sel_spacing;
  float   font_sel_pitch;
  float   font_sel_height;
  int32   font_sel_style;
  int32   font_sel_weight;
  int32   font_sel_typeface;

  uint32  pclxl_page_number;      /* PCLXL's page number count
                                   * Starts at "1" at the srt of each PCLXL session
                                   * and incremented (by 1) for each PCLXL EndPage operator
                                   * (and for any PCL5 "whole job" passthrough page throw */
  uint32  pclxl_duplex_page_side_count; /* PCLXL's duplex page side count
                                         * Starts at "1" at the start of each session
                                         * and incremented by 1 or 2 at each "showpage" */
  Bool    pclxl_duplex;           /* PCLXL's simplex/duplex setting
                                   * PCL5 Passthrough needs to set this whenever it changes PCL5's simplex/duplex setting
                                   * so that PCLXL can know whether to do any additional duplex page throws */
  uint8   pclxl_requested_orientation;
  uint8   pclxl_requested_media_size_value_type;
  uint8   pclxl_requested_media_size_enum;
  uint8*  pclxl_requested_media_size_name;
  struct {
    SYSTEMVALUE x;
    SYSTEMVALUE y;
  }       pclxl_requested_media_size_xy;
  uint8*  pclxl_requested_media_type;
  uint8   pclxl_requested_media_source;
  uint8   pclxl_requested_media_destination;
  Bool    pclxl_requested_duplex;
  uint8   pclxl_requested_simplex_page_side;
  uint8   pclxl_requested_duplex_page_side;
  uint8   pclxl_requested_duplex_binding;

} PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO;

Bool pcl5_execute_stream(corecontext_t* context,
                         FILELIST* flptr,
                         OBJECT* odict,
                         int32 pass_through_type,
                         PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info);

Bool pcl5_update_passthrough_state_info(PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info);

void pcl5_destroy_passthrough_context(PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info);


/** Values for the PCL Virtual Device parameter, packed from dictionary form.
    Bits 0..1 are used for DeviceGray, 2..3 for DeviceCMYK, 4..5 for
    DeviceRGB, 6..7 for other device spaces. */
enum {
  /** VirtualDeviceSpace DeviceRGB, OverprintPreview on, surface ROPs on. */
  PCL_VDS_RGB_T_T = 0,
  /** VirtualDeviceSpace DeviceCMYK, OverprintPreview off, surface ROPs on. */
  PCL_VDS_CMYK_F_T = 1,
  /** VirtualDeviceSpace DeviceRGB, OverprintPreview on, surface ROPs off. */
  PCL_VDS_RGB_T_F = 2,
  /** VirtualDeviceSpace DeviceGray, OverprintPreview off, surface ROPs on. */
  PCL_VDS_GRAY_F_T = 3,
  PCL_VDS_BITS = 2,       /**< Two bits used for each colorspace. */
  PCL_VDS_MASK = (1 << PCL_VDS_BITS) - 1, /**< Mask for selection value. */
  PCL_VDS_GRAY_SHIFT = 0, /**< Shift to extract gray PCM selection. */
  PCL_VDS_CMYK_SHIFT = PCL_VDS_GRAY_SHIFT + PCL_VDS_BITS, /**< Shift to extract CMYK PCM selection. */
  PCL_VDS_RGB_SHIFT = PCL_VDS_CMYK_SHIFT + PCL_VDS_BITS,  /**< Shift to extract RGB PCM selection. */
  PCL_VDS_OTHER_SHIFT = PCL_VDS_RGB_SHIFT + PCL_VDS_BITS, /**< Shift to extract Other PCM selection. */
  /** Default value for PCL VirtualDeviceSpace selection. */
  PCL_VDS_INIT = ((PCL_VDS_GRAY_F_T << PCL_VDS_GRAY_SHIFT) |
                  (PCL_VDS_RGB_T_T << PCL_VDS_RGB_SHIFT)   |
                  (PCL_VDS_CMYK_F_T << PCL_VDS_CMYK_SHIFT) |
                  (PCL_VDS_RGB_T_T << PCL_VDS_OTHER_SHIFT)),
} ;

Bool pcl_param_vds_select(OBJECT *dict, int32 *value) ;

/** \} */

#endif /* __PCL_H__ */

/******************************************************************************
* Log stripped */
