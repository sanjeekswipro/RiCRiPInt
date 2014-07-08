/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2state.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 */
#include "core.h"
#include "hpgl2state.h"
#include "hpgl2polygon.h"

#include "pcl5context_private.h"
#include "printenvironment_private.h"

#include "graphics.h"
#include "gstack.h"
#include "gu_cons.h"

/* See header for doc. */
void default_hpgl2printenv(PCL5PrintEnvironment* env)
{
  default_HPGL2_config_info(&env->hpgl2_config_info);
  default_HPGL2_character_info(&env->hpgl2_character_info);
  default_HPGL2_line_fill_info(&env->hpgl2_line_fill_info);
  default_HPGL2_vector_info(&env->hpgl2_vector_info);
  default_HPGL2_polygon_info(&env->hpgl2_polygon_info);
  default_HPGL2_palette_extension_info(&env->hpgl2_palette_extension);
  default_HPGL2_technical_info(&env->hpgl2_tech_info);

  env->hpgl2_ctms.default_picture_frame_ctm = identity_matrix;
  env->hpgl2_ctms.inverse_default_picture_frame_ctm = identity_matrix;
  env->hpgl2_ctms.picture_frame_scaling_ctm = identity_matrix;
  env->hpgl2_ctms.inverse_picture_frame_scaling_ctm = identity_matrix;
  env->hpgl2_ctms.working_picture_frame_ctm = identity_matrix;
  env->hpgl2_ctms.inverse_working_picture_frame_ctm = identity_matrix;
}

void save_hpgl2printenv(PCL5Context *pcl5_ctxt,
                        PCL5PrintEnvironment *to,
                        PCL5PrintEnvironment *from)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt);

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(to != NULL && from != NULL, "PCL5PrintEnvironment is NULL") ;

  to->hpgl2_config_info = from->hpgl2_config_info ;
  to->hpgl2_character_info = from->hpgl2_character_info ;
  to->hpgl2_line_fill_info = from->hpgl2_line_fill_info ;
  to->hpgl2_vector_info = from->hpgl2_vector_info ;
  to->hpgl2_polygon_info = from->hpgl2_polygon_info ;
  to->hpgl2_palette_extension = from->hpgl2_palette_extension ;
  to->hpgl2_tech_info = from->hpgl2_tech_info ;
  to->hpgl2_ctms = from->hpgl2_ctms ; /** \todo Do we need this? */
}

void restore_hpgl2printenv(PCL5Context *pcl5_ctxt,
                           PCL5PrintEnvironment *to,
                           PCL5PrintEnvironment *from)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(PCL5PrintEnvironment*, to) ;
  UNUSED_PARAM(PCL5PrintEnvironment*, from) ;
}

/** reset to default values, conditional on whether reset done in DF or IN context.*/
void hpgl2_set_default_print_state(HPGL2PrintState *print_state, Bool initialize)
{
  if ( initialize ) {
    print_state->initial_pen_position.x = 0.0;
    print_state->initial_pen_position.y = 0.0;
    print_state->Carriage_Return_point.x = 0.0;
    print_state->Carriage_Return_point.y = 0.0;
    print_state->lost = FALSE;
  }
  print_state->dot_candidate = FALSE;
}

/** synchronise the interpreter gstate with HPGL2 vector info, depending on
  * calling context DF or IN.
  */
void hpgl2_sync_print_state(HPGL2PrintState *print_state, Bool initialize)
{
  SYSTEMVALUE curr_point[2];
  Bool result = TRUE;

  if ( initialize ) {
    /* only move current point of state if actaully re-initializing state. */
    curr_point[0] = print_state->initial_pen_position.x;
    curr_point[1] = print_state->initial_pen_position.y;

    result = gs_moveto(TRUE, curr_point, &gstateptr->thepath);

    HQASSERT(result, "Cannot sync pen position");
  }

  return;
}

HPGL2PrintState* get_hpgl2_print_state(PCL5Context *pcl5_ctxt)
{
  PCL5PrintState* p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5 context NULL" );

  p_state = pcl5_ctxt->print_state;
  HQASSERT( p_state != NULL, "PCL5PrintState is NULL");

  return &p_state->hpgl2_print_state;

}

/** \todo Put MPE type manipulation for HPGL2 here for the moment, but
 * might need another home.
 */

/** Helper functions to retreive various parts of HPGL2 state from the
 * current MPE
 */
HPGL2VectorInfo* get_hpgl2_vector_info(PCL5Context* pcl5_ctxt)
{
  PCL5PrintState *p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT(p_state != NULL, "PrintState is NULL");

  HQASSERT(p_state->mpe != NULL, "PrintEnviroment is NULL");

  return &p_state->mpe->hpgl2_vector_info;
}

HPGL2ConfigInfo* get_hpgl2_config_info(PCL5Context* pcl5_ctxt)
{
  PCL5PrintState *p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT(p_state != NULL, "PrintState is NULL");

  HQASSERT(p_state->mpe != NULL, "PrintEnnviroment is NULL");

  return &p_state->mpe->hpgl2_config_info;
}

HPGL2CharacterInfo* get_hpgl2_character_info(PCL5Context* pcl5_ctxt)
{
  PCL5PrintState *p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT(p_state != NULL, "PrintState is NULL");

  HQASSERT(p_state->mpe != NULL, "PrintEnnviroment is NULL");

  return &p_state->mpe->hpgl2_character_info;
}

HPGL2LineFillInfo* get_hpgl2_line_fill_info(PCL5Context* pcl5_ctxt)
{
  PCL5PrintState *p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT(p_state != NULL, "PrintState is NULL");

  HQASSERT(p_state->mpe != NULL, "PrintEnnviroment is NULL");

  return &p_state->mpe->hpgl2_line_fill_info;
}

HPGL2PolygonInfo* get_hpgl2_polygon_info(PCL5Context* pcl5_ctxt)
{
  PCL5PrintState *p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT(p_state != NULL, "PrintState is NULL");

  HQASSERT(p_state->mpe != NULL, "PrintEnnviroment is NULL");

  return &p_state->mpe->hpgl2_polygon_info;
}

HPGL2PaletteExtensionInfo* get_hpgl2_palette_extension(PCL5Context* pcl5_ctxt)
{
  PCL5PrintState *p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT(p_state != NULL, "PrintState is NULL");

  HQASSERT(p_state->mpe != NULL, "PrintEnnviroment is NULL");

  return &p_state->mpe->hpgl2_palette_extension;
}

HPGL2TechnicalInfo* get_hpgl2_technical_info(PCL5Context* pcl5_ctxt)
{
  PCL5PrintState *p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT(p_state != NULL, "PrintState is NULL");

  HQASSERT(p_state->mpe != NULL, "PrintEnnviroment is NULL");

  return &p_state->mpe->hpgl2_tech_info;
}

HPGL2Ctms* get_hpgl2_ctms(PCL5Context* pcl5_ctxt)
{
  PCL5PrintState *p_state = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL");

  p_state = pcl5_ctxt->print_state;
  HQASSERT(p_state != NULL, "PrintState is NULL");

  HQASSERT(p_state->mpe != NULL, "PrintEnnviroment is NULL");

  return &p_state->mpe->hpgl2_ctms;
}

void hpgl2_set_plot_mode(PCL5Context* pcl5_ctxt, uint8 mode)
{
  HPGL2VectorInfo* hpgl2_vector_info = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL" );
  HQASSERT( mode == HPGL2_PLOT_ABSOLUTE || mode == HPGL2_PLOT_RELATIVE,
            "Illegal HPGL2 plot mode" );

  hpgl2_vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  HQASSERT( hpgl2_vector_info != NULL, "VectorInfo is NULL" );

  hpgl2_vector_info->plot_mode = mode;

  return;
}

void hpgl2_set_pen_mode(PCL5Context* pcl5_ctxt, uint8 mode)
{
  HPGL2VectorInfo* hpgl2_vector_info = NULL;

  HQASSERT( pcl5_ctxt != NULL, "PCL5Context is NULL" );
  HQASSERT( mode == HPGL2_PEN_DOWN || mode == HPGL2_PEN_UP,
              "Illegal HPGL2 plot mode" );

  hpgl2_vector_info = get_hpgl2_vector_info(pcl5_ctxt);
  HQASSERT( hpgl2_vector_info != NULL, "VectorInfo is NULL" );

  hpgl2_vector_info->pen_state = mode;

  return;
}

void hpgl2_default_hpgl2_print_state(HPGL2PrintState* print_state)
{
  HQASSERT(print_state != NULL, "HPGL2 print state is NULL");

  print_state->initial_pen_position.x = 0 ;
  print_state->initial_pen_position.y = 0 ;

  print_state->path_to_draw = FALSE ;

  print_state->Carriage_Return_point.x = 0 ;
  print_state->Carriage_Return_point.y = 0 ;

  print_state->SM_points = NULL ;

  print_state->lost = FALSE;

  print_state->dot_candidate = FALSE;
}

/* Creation of specific structures for HPGL2 print state. */
Bool hpgl2_print_state_init(HPGL2PrintState* print_state)
{
  HQASSERT(print_state != NULL, "HPGL2 print state is NULL");
  print_state->polygon_buffer = hpgl2_polygon_buffer_create();

  return ( print_state->polygon_buffer != NULL );
}

/* Clear up of HPGL2 print state structures. */
Bool hpgl2_print_state_destroy(HPGL2PrintState *print_state)
{
  HQASSERT(print_state != NULL, "HPGL2 print state is NULL");
  return hpgl2_polygon_buffer_destroy(print_state->polygon_buffer);
}

Bool hpgl2_in_lost_mode(PCL5Context *pcl5_ctxt)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt);

  return print_state->lost;
}

void hpgl2_set_lost_mode(PCL5Context *pcl5_ctxt, Bool lost)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt);

  print_state->lost = lost;
}

/* Log stripped */

