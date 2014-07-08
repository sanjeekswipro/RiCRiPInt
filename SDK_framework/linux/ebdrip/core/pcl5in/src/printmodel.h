/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:printmodel.h(EBDSDK_P.1) $
 * $Id: src:printmodel.h,v 1.28.4.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

#ifndef __PRINTMODEL_H__
#define __PRINTMODEL_H__

#include "pcl5context.h"
#include "cursorpos.h"
#include "pclGstate.h"

/* Values for print model pixel placement */
#define PCL5_INTERSECTION_CENTERED 0
#define PCL5_GRID_CENTERED 1

/* Values for print model pattern type, and for passing to
 * set_current_pattern(). */
#define PCL5_SOLID_FOREGROUND 0
#define PCL5_ERASE_PATTERN 1
#define PCL5_SHADING_PATTERN 2
#define PCL5_CROSS_HATCH_PATTERN 3
#define PCL5_USER_PATTERN 4

#define PCL5_CURRENT_PATTERN 5
#define PCL5_WHITE_PATTERN 6

/* Be sure to update default_print_model() if you change this structure. */
typedef struct PrintModelInfo {
  Bool source_transparent;
  Bool pattern_transparent;
  uint32 rop;

  uint32 current_pattern_type;

  /* Setting a pattern ID alone does not change the current pattern; thus
   * only the pending ID is set, until a pattern type command is interpreted. */
  pcl5_resource_numeric_id pending_pattern_id;

  /* The current pattern id. Note that this is only changed when a pattern type
   * command is interpreted. */
  pcl5_resource_numeric_id current_pattern_id;

  /* The pattern origin in current PCL coordinates. */
  /** \todo Would it be easier, e.g. for HPGL2 to store in terms
   *  of printdirection zero?
   */
  CursorPosition pattern_origin;

  Bool patterns_follow_print_direction;

  uint32 pixel_placement;
} PrintModelInfo;

/**
 * Global cached_palette used for setting up colored patterns.
 */
extern Pcl5CachedPalette cached_palette;

/**
 * Return the print model state from the passed context.
 */
PrintModelInfo* get_print_model(PCL5Context* pcl5_ctxt);

/**
 * Perform any necessary printmodel actions when going up an MPE level.
 */
void save_pcl5_print_model(PCL5Context *pcl5_ctxt,
                           PrintModelInfo *to,
                           PrintModelInfo *from,
                           Bool overlay);

/**
 * Perform any necessary printmodel actions when going down an MPE level.
 */
void restore_pcl5_print_model(PCL5Context *pcl5_ctxt,
                              PrintModelInfo *to,
                              PrintModelInfo *from);

/**
 * Init method; this should be called before any PCL5 jobs are ripped.
 */
void pcl5_print_model_init(PCL5Context *pcl5_ctxt);

/**
 * Finish method; this should be called whenever PCL processing has finished,
 * and control is being returned to the core.
 */
void pcl5_print_model_finish(PCL5Context *pcl5_ctxt);

/**
 * Initialise default environment.
 */
void default_print_model(PrintModelInfo* self);

/**
 * Ensure that the PCL5 source and pattern transparency, along with the current
 * ROP are set in the core. This only needs to be called if the PCL5 gstate in
 * the core has been manipulated directly (e.g. by HP-GL/2 code).
 */
void reinstate_pcl5_print_model(PCL5Context *pcl5_ctxt);

/**
 * Set the pattern reference point to its default. This should only be called
 * once the page CTMs have been configured.
 */
void set_default_pattern_reference_point(PCL5Context *context);

/**
 * Set the current pattern in the core; this should be called before drawing
 * anything which is affected by the current pattern.
 *
 * \param type_override Used to override the current pattern type in the PCL5
 * state with that specified, which should be one of PCL5_SOLID_FOREGROUND,
 * PCL5_ERASE_PATTERN, etc. PCL5_CURRENT_PATTERN uses the current pattern type.
 */
void set_current_pattern(PCL5Context* context, uint32 type_override);

/**
 * Same as set_current_pattern(), but allowing the pattern ID to be specified,
 * rather than using the current PCL5 pattern ID.
 */
void set_current_pattern_with_id(PCL5Context* context,
                                 pcl5_resource_numeric_id id,
                                 uint32 type_override);

/**
 * Return the current pattern type, e.g. one of PCL5_SOLID_FOREGROUND,
 * PCL5_ERASE, etc.
 */
uint32 get_current_pattern_type(PCL5Context* pcl5_ctxt);

/**
 * Returns true if the passed pattern is solid black (and can therefore be
 * ignored).
 */
Bool pattern_is_black(pcl5_pattern *pattern);

/**
 * Set the current logical operation, aka ROP.
 */
void set_current_rop(PCL5Context *context, uint32 rop);

/**
 * Return the angle by which a pattern should be rotated, taking into account
 * page orientation and (if includePrintDirection is true) print direction.
 */
uint32 get_pattern_rotation(PCL5Context* context, Bool includePrintDirection);

/**
 * Map shading level to corresponding pattern id.
 */
pcl5_resource_numeric_id shading_to_pattern_id(pcl5_resource_numeric_id id);

/**
 * Set pixel placement in PrintModelInfo and update the gstate.
 */
void set_pixel_placement(PrintModelInfo* print_info, PCL5Integer pixel_placement);

/**
 *  Update the cached palette. This function may alter the current color of the
 *  gstate. Up to 256 colors can be held in the cached palette.
 */
Bool update_cached_palette(PCL5Context* pcl5_ctxt, Bool hpgl);

Bool pcl5op_star_c_G(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;
Bool pcl5op_star_c_Q(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;
Bool pcl5op_star_c_W(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;
Bool pcl5op_star_l_O(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;
Bool pcl5op_star_l_R(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;
Bool pcl5op_star_p_R(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;
Bool pcl5op_star_v_N(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;
Bool pcl5op_star_v_O(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;
Bool pcl5op_star_v_T(PCL5Context *pcl5_ctxt, Bool expliecit_positive, PCL5Numeric value) ;

/* ============================================================================
* Log stripped */
#endif
