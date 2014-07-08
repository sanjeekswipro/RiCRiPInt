/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:printenvironment_private.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL5 Print environment type.
 */
#ifndef _printenvironment_private_h_
#define _printenvironment_private_h_

#include "printenvironment.h"

#include "printmodel.h"
#include "pagecontrol.h"
#include "cursorpos.h"
#include "fontselection.h"
#include "jobcontrol.h"
#include "misc.h"
#include "pcl5color.h"
#include "pcl5raster.h"
#include "pictureframe.h"
#include "macros.h"
#include "status.h"
#include "hpgl2config.h"
#include "pcl5ctm_private.h"
#include "areafill.h"

/* Various types of print environment - N.B. the order is important */
/** \todo This might be more complicated than necessary */
enum {
  GG_DEFAULT_ENV = 0,
  MODIFIED_DEFAULT_ENV,
  PJL_CURRENT_ENV,
  MODIFIED_PRINT_ENV,
  MACRO_ENV_1,
  MACRO_ENV_2,
  MACRO_ENV_3,
  OVERLAY_ENV_1,
  OVERLAY_ENV_2,
  OVERLAY_ENV_3,
  MAX_PRINT_ENVIRONMENT
};

/* ============================================================================
 * PCL5 print environment.
 *
 * This consists partly of settings which form part of the PCL5 factory
 * default environment, and partly of additional variables which are implicitly
 * part of the PCL5 MPE.  It contains both PCL5 and HP-GL/2 elements.
 *
 * The explicitly required settings may be changed via an optional dictionary
 * parameter to pcl5exec.
 *
 * It is the intention that the PCL5 print environment will be 'saved' on entry
 * to a 'called' macro, and the original 'restored' afterwards.
 *
 * N.B. Any for which no command has been found might be prime candidates for
 * dealing with outside of this structure.
 * ============================================================================
 */

/* PCL5 print environment. */
struct PCL5PrintEnvironment {

  /* The print environment features (PCL) */
  /* Job Control */
  JobCtrlInfo           job_ctrl;

  /* Page Control */
  PageCtrlInfo          page_ctrl;

  /* Text Features */
  TextInfo              text_info;

  /* Font Features */
  FontInfo              font_info;

  FontMgtInfo           font_management;

  /* Macro Features */
  MacroInfo             macro_info;

  /* Print Model. */
  PrintModelInfo        print_model;

  /* Rectangular area fill information. */
  AreaFillInfo          area_fill;

  /* Raster Graphics */
  RasterGraphicsInfo    raster_graphics;

  /* Troubleshooting */
  TroubleShootingInfo   trouble_shooting;

  /* Status Readback */
  StatusReadBackInfo    status_readback;

  /* CTMs */
  PCL5Ctms              pcl5_ctms;

  /* Color Features */
  ColorInfo             color_info;

  /* HP-GL/2 Picture Frame */
  PictureFrameInfo      picture_frame;

  /* Print environment features (HP-GL/2) */
  /* Configuration and Status Group */
  HPGL2ConfigInfo       hpgl2_config_info;

  /* Character Group */
  HPGL2CharacterInfo    hpgl2_character_info;

  /* Line and Fill Group */
  HPGL2LineFillInfo     hpgl2_line_fill_info;

  /* Vector Group */
  HPGL2VectorInfo       hpgl2_vector_info;

  /* Polygon Group */
  HPGL2PolygonInfo      hpgl2_polygon_info;

  /* Palette Extension Group */
  HPGL2PaletteExtensionInfo hpgl2_palette_extension;

  /* Technical Group */
  HPGL2TechnicalInfo hpgl2_tech_info;

  /* HPGL2 ctms */
  HPGL2Ctms             hpgl2_ctms;
};

typedef struct PCL5PrintEnvironmentLink {
  /** List links - more efficient if first. */
  sll_link_t sll;
  int32 slevel;
  int32 macro_nest_level;
  int32 mpe_type;
  PCL5PrintEnvironment mpe;
} PCL5PrintEnvironmentLink;

#endif

/* Log stripped */

