/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pagecontrol.h(EBDSDK_P.1) $
 * $Id: src:pagecontrol.h,v 1.44.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __PAGECONTROL_H__
#define __PAGECONTROL_H__ (1)

#include "pcl5context.h"

#define INVALID_HMI -1
#define UNKNOWN_PAGE_SIZE -1

#define LETTER 2
#define A4 26 /* page_size */

#define A4_PAGE_SIZE

/** \todo There is a discrepancy between sizes in manual and normal sizes */
#ifdef  A4_PAGE_SIZE
#define DEFAULT_PHYSICAL_PAGE_WIDTH     59520
#define DEFAULT_PHYSICAL_PAGE_LENGTH    84168
#define DEFAULT_TOP_MARGIN               3600  /* 7200/2            */
#define DEFAULT_TEXT_LENGTH             76968  /* 80568 - 3600      */
#define DEFAULT_MAX_TEXT_LENGTH         80568  /* 84168 - 3600      */
#define DEFAULT_PAGE_WIDTH              56112
#define DEFAULT_PAGE_LENGTH             84168
#define DEFAULT_PORTRAIT_OFFSET          1704
#define DEFAULT_LANDSCAPE_OFFSET         1416

#else /* letter */
#define DEFAULT_PHYSICAL_PAGE_WIDTH     61200
#define DEFAULT_PHYSICAL_PAGE_LENGTH    79200
#define DEFAULT_TOP_MARGIN               3600  /* 7200/2            */
#define DEFAULT_TEXT_LENGTH             72000  /* 75600 - 3600      */
#define DEFAULT_MAX_TEXT_LENGTH         75600  /* 79200 - 3600      */
#define DEFAULT_PAGE_WIDTH              57600
#define DEFAULT_PAGE_LENGTH             79200
#define DEFAULT_PORTRAIT_OFFSET          1800
#define DEFAULT_LANDSCAPE_OFFSET         1440
#endif

#define DEFAULT_CLIP_WIDTH               1200
#define MAIN_SOURCE 1

/* Duplex binding */
enum {
  SIMPLEX = 0,
  LONG_EDGE = 1,
  SHORT_EDGE = 2
};

/* Be sure to update default_page_control() if you change this structure. */
typedef struct PageCtrlInfo {
  /* Explicitly required */
  int32        page_size;      /* N.B. This is now last page size requested of the pagedevice */
  uint32       print_direction;
  uint32       orientation;
  uint32       paper_source;  /* N.B. This is the last paper source requested of the page device */
  uint32       media_type;    /* N.B. This should strictly be the last media type REQUESTED of the page device */

  uint32       vmi;             /* In PCL internal units */
  int32        hmi;             /* In PCL internal units */

  /* Margins are in PCL internal units, in the active coordinate space, which
  includes both page orientation and print direction transformations. */
  uint32       top_margin;      /* Its height. */
  uint32       text_length;     /* Logical page y coord of bottom margin. */
  uint32       left_margin;     /* Its logical page x coord. */
  uint32       right_margin;    /* Its logical page x coord. */

  uint32       custom_page_width;  /* In decipoints */
  uint32       custom_page_length; /* In decipoints */

  Bool         perforation_skip;
  uint32       line_termination;

  /* Directly derived (convenience) quantities */
  uint32       physical_page_width;  /* In PCL internal units - see Tech Ref Table 2.1 col A */
  uint32       physical_page_length; /* In PCL internal units - see Tech Ref Table 2.1 col B */
  uint32       clip[4];         /* In PCL internal units, (x0, y0, x1, y1) of top-left, bottom-right corners of printable area, Tech Ref Tables 2.1, 2.2 */
  uint32       portrait_offset; /* Offset of portrait logical from physical page in PCL internal units, see Tech Ref Table 2.1 col E */
  uint32       landscape_offset;/* Offset of landscape logical from physical page in PCL internal units, see Tech Ref Table 2.2 col E */
  uint32       page_width;      /* Logical page width in PCL internal units, see Tech Ref Table 2.1 col C */
  uint32       page_length;     /* Logical page length in PCL internal units, see Tech Ref Table 2.1 col D (=B) */
  uint32       max_text_length; /* Logical page y coord of page bottom in PCL internal units */

  /* The size of a 'PCL unit' in PCL internal units, as defined by the units
  of measure command. By default the units of measure is 300 dpi, so each PCL
  unit is equal to 24 PCL internal units (i.e. 7200 / 300). */
  uint32       pcl_unit_size;

} PageCtrlInfo;

/* Get hold of the PageCtrlInfo */
PageCtrlInfo* get_page_ctrl_info(PCL5Context *pcl5_ctxt) ;

/* Get hold of the PageCtrlInfo from the PJL current environment (base) level MPE */
const PageCtrlInfo* get_default_page_ctrl_info(PCL5Context *pcl5_ctxt) ;

/* Initialise, save and restore page control. */
void default_page_control(PageCtrlInfo* self, PCL5ConfigParams* config_params) ;
Bool pagecontrol_apply_pjl_changes(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params) ;
void save_page_control(PCL5Context *pcl5_ctxt, PageCtrlInfo *to, PageCtrlInfo *from, Bool overlay) ;
void restore_page_control(PCL5Context *pcl5_ctxt, PageCtrlInfo *to, PageCtrlInfo *from) ;

/**
 * Mark the HMI invalid.
 */
void invalidate_hmi(PCL5Context *pcl5_ctxt) ;

/**
 * Take a font pitch and convert it to an HMI rounding using the UOM.
 */
void scale_hmi_for_page(PCL5Context *pcl5_ctxt, PCL5Real font_hmi) ;

/**
 * Set the PCL5 margins afresh or adjust them for a new page size.
 */
void set_pcl5_margins(PCL5Context *pcl5_ctxt, Bool job_start) ;

/**
 * Set the default page dimensions for the page size and logical page offsets provided.
 */
void set_default_pcl_page_dimensions(PCL5Context *pcl5_ctxt, Bool job_start, uint32 left, uint32 right, uint32 top, uint32 bottom) ;

/**
 * Take margin values for current printdirection and set them for the direction
 * provided, e.g. if the current printdirection is zero and the required
 * direction is 90, set the new top margin to the value of the current left
 * margin, etc.
 */
void rotate_margins_for_printdirection(PageCtrlInfo *page_info, uint32 printdirection) ;

/**
 * Throw a page (PS showpage) only if there are marks on the page, or
 * 'unconditional' is true.
 * If 'reset' is true, reset_page() will be called after the page has been
 * rendered.
 * Note that unconditional page throws will preserve the horizontal cursor
 * position onto the next page, even if 'reset' is true.
 */
Bool throw_page(PCL5Context *pcl5_ctxt, Bool reset, Bool unconditional) ;

/**
 * Reset PS and PCL5 page settings, CTMs, at the beginning of each page
 */
Bool reset_page(PCL5Context *pcl5_ctxt) ;

/**
 * Handle a change to PCL5 page size, orientation etc, calling PS setpagedevice
 * with the buffer provided, using the results to modify the PCL5 state, and
 * doing any other necessary PCL5 state changes.  If no buffer is provided,
 * the setpagedevice call is made filling in all relevant items from the MPE.
 * N.B. It is assumed that the previous page has already been thrown.
 */
Bool handle_pcl5_page_change(PCL5Context *pcl5_ctxt,
                             uint8 *buffer,
                             int32 reason,
                             Bool reset_printdirection);

/**
 * Set the size of a 'PCL unit' in PCL internal units.
 */
void set_pcl_unit_size(PageCtrlInfo* self, uint32 number_of_internal_units);

/**
 * Return the size of a 'PCL unit', in internal units.
 */
PCL5Real pcl_unit_size(PageCtrlInfo* self);

/**
 * Convert the passed number of PCL units into PCL internal units.
 */
PCL5Real pcl_unit_to_internal(PageCtrlInfo* self, PCL5Real pcl_units);

/**
 * Round a number of PCL Internal Units to the nearest PCL Unit.
 */
int32 round_pcl_internal_to_pcl_unit(PCL5Context *pcl5_ctxt, PCL5Real internal_units);

/**
 * Convert the passed value in decipoints into pcl internal units.
 */
#define DECIPOINTS_TO_INTERNAL(_v) ((_v) * 10)

/**
 * Calculate default page length, based on margins that are to be applied for
 * print direction 0.
 */
uint32 get_default_text_length(PCL5Context *pcl5_ctxt);

void set_top_margin(PCL5Context *pcl5_ctxt, uint32 value) ;
void reset_text_length(PCL5Context *pcl5_ctxt) ;
Bool set_text_length(PCL5Context *pcl5_ctxt, uint32 value) ;
void set_pcl5_margins(PCL5Context *pcl5_ctxt, Bool job_start) ;

Bool set_media_type(PCL5Context *pcl5_ctxt, uint32 media_type) ;
Bool set_media_type_from_alpha(PCL5Context *pcl5_ctxt, uint8 *media_type_buf, int32 length) ;

/* ============================================================================
 * Operator callbacks are below here.
 * ============================================================================
 */

Bool pcl5op_9(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_a_L(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_a_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_a_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_f_I(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_f_J(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_k_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_A(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_C(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_D(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_E(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_F(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_H(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_L(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_M(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_O(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;
Bool pcl5op_ampersand_l_P(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */
#endif
