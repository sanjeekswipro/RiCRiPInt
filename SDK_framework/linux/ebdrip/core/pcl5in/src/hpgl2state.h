/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2state.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 *
 */
#ifndef _hpgl2state_h_
#define _hpgl2state_h_

#include "pcl5types.h"
#include "hpgl2vector.h"
#include "hpgl2config.h"
#include "hpgl2linefill.h"
#include "hpgl2polygon.h"
#include "hpgl2fonts.h"
#include "hpgl2misc.h"
#include "hpgl2technical.h"
#include "printenvironment.h"

#include "matrix.h"
/* #include "paths.h" */

/* Be sure to update default_hpgl2printenv() if you change this structure. */
typedef struct HPGL2Ctms {

  /* cache the CTM defining the default picture frame. */
  OMATRIX     default_picture_frame_ctm;
  OMATRIX     inverse_default_picture_frame_ctm;

  /* HPGL2 plotter unit coord space to PCL coord transform. This will be
   * concatenated with the gstate CTM on entry to HPGL2 processing. This
   * matrix will reflect any rotation of the HPGL2 coord space. */
  OMATRIX     working_picture_frame_ctm;
  OMATRIX     inverse_working_picture_frame_ctm;

  /* scaled (user unit) HPGL2 coord to plotter unit HPGL2 coord transform */
  OMATRIX     picture_frame_scaling_ctm;
  OMATRIX     inverse_picture_frame_scaling_ctm;

} HPGL2Ctms;

struct HPGL2PolygonBuffer;

typedef struct HPGL2PrintState {
  /* For %A %B, handling. Allows subsequent invocations of HPGL2 to
   * pick up the pen position defined by the previous HPGL2 invocation.
   * On exit from HPGL2 it is updated with the current pen position.
   * Pen position is defined in terms of the HPGL2 plotter unit coord
   * space.
   * */
  HPGL2Point initial_pen_position ;

  /* Line/vector related state. */
  Bool path_to_draw ;

  /* Label/character related state. */
  HPGL2Point Carriage_Return_point ;

  /* Symbol Mode. */
  HPGL2PointList *SM_points ; /** List of points requiring symbols to be drawn. */

  /* Polygon buffer path and associated state. */
  struct HPGL2PolygonBuffer *polygon_buffer;

  /* LOST mode */
  Bool lost;

  /* dot_candidate is used to handle PD; operator. Whether a dot is drawn
   * depends on the subsquent operator.
   */
  Bool dot_candidate ;
} HPGL2PrintState;

/**
 * Initialise HPGL2 members of the passed print environment.
 */
void default_hpgl2printenv(PCL5PrintEnvironment* env);
void save_hpgl2printenv(PCL5Context *pcl5_ctxt, PCL5PrintEnvironment *to, PCL5PrintEnvironment *from) ;
void restore_hpgl2printenv(PCL5Context *pcl5_ctxt, PCL5PrintEnvironment *to, PCL5PrintEnvironment *from) ;

/** reset to default values, conditional on whether reset done in DF or IN
    context.*/
void hpgl2_set_default_print_state(HPGL2PrintState *print_state, Bool initialize);

/** synchronise the interpreter gstate with HPGL2 vector info */
void hpgl2_sync_print_state(HPGL2PrintState *print_state, Bool init);

HPGL2PrintState* get_hpgl2_print_state(PCL5Context *pcl5_ctxt);
HPGL2VectorInfo* get_hpgl2_vector_info(PCL5Context* pcl5_ctxt);
HPGL2ConfigInfo* get_hpgl2_config_info(PCL5Context* pcl5_ctxt);
HPGL2CharacterInfo* get_hpgl2_character_info(PCL5Context* pcl5_ctxt);
HPGL2LineFillInfo* get_hpgl2_line_fill_info(PCL5Context* pcl5_ctxt);
HPGL2PolygonInfo* get_hpgl2_polygon_info(PCL5Context* pcl5_ctxt);
HPGL2PaletteExtensionInfo* get_hpgl2_palette_extension(PCL5Context* pcl5_ctxt);
HPGL2TechnicalInfo* get_hpgl2_technical_info(PCL5Context* pcl5_ctxt);
HPGL2Ctms* get_hpgl2_ctms(PCL5Context* pcl5_ctxt);

/* MPE control for HPGL2. */
void hpgl2_set_plot_mode(PCL5Context* pcl5_ctxt, uint8 mode);
void hpgl2_set_pen_mode(PCL5Context* pcl5_ctxt, uint8 mode);

void hpgl2_default_hpgl2_print_state(HPGL2PrintState* print_state);
Bool hpgl2_print_state_init(HPGL2PrintState *print_state);
Bool hpgl2_print_state_destroy(HPGL2PrintState *print_state);

void hpgl2_set_lost_mode(PCL5Context *pcl_ctxt, Bool lost);
Bool hpgl2_in_lost_mode(PCL5Context *pcl_ctxt);

#endif

/* Log stripped */

