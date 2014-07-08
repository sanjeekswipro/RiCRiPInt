/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5state.h(EBDSDK_P.1) $
 * $Id: src:pcl5state.h,v 1.72.1.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL5 Print Environment, Cursor Posn Stack, etc.
 */

#ifndef __PCL5_STATE_H__
#define __PCL5_STATE_H__

#include "pcl5context.h"
#include "hpgl2state.h"
#include "printenvironment.h"
#include "cursorpos.h"
#include "pcl5color.h"
#include "pcl5fonts.h"
#include "garray.h"

#include "objnamer.h"
#include "matrix.h"
#include "objects.h"

#include "swpfinapi.h"


/* Reasons for setting MPE from device */
enum {
  PCL5_STATE_CREATION = 0,
  PCL5_STATE_CREATION_FOR_XL_PASSTHROUGH,
  PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH,
  PCL5_PAGE_SIZE_CHANGE,
  PCL5_STATE_MAINTENANCE
} ;

/* ============================================================================
 * PCL5 print state
 *
 * This is intended to contain all the state information that is part
 * of the page description. It contains both PCL5 and HP-GL/2
 * elements.
 *
 * This is not intended to be 'saved' or 'restored' on entry to or
 * exit from 'called' macros.
 *
 * N.B. It is not intended to contain the various stores such as macro
 * store, font store, symbol set store, pattern store.
 * ============================================================================
 */

#define NO_UNDERLINE -1
#define FIXED_UNDERLINE 0
#define FLOATING_UNDERLINE 3

typedef struct PCL5PrintState {
  /* Current Active Print Environment (MPE) */
  PCL5PrintEnvironment *mpe ;

  /* Print Environments (MPEs) list */
  sll_list_t mpe_list ;

  /* Current Active (Cursor) Position (CAP) */
  CursorPosition cursor ;
  Bool cursor_explicitly_set ;

  /* Cursor Position Stack */
  CursorPositionStack cursor_stack ;

  PCL5FontState font_state ;
  PS_FontState ps_font_state ;

  /* Character definition data */
  GROWABLE_ARRAY  char_data ;

  Bool possible_raster_data_trim ;

  /* Palette Store, Palette Stack, etc */
  ColorState color_state ;

  /* HP-GL/2 Items
     Current Pen Position
     Polygon buffer */
  HPGL2PrintState hpgl2_print_state ;

  /* The duplex page number or side may affect CTM */
  uint32 page_number ;          /* For convenience, debugging, etc */
  uint32 duplex_page_number ;   /* Odd on front sheet side, e.g. for simplex count 1, 3, 5, ... */

  /* Various Flags */
  Bool allow_macro_overlay ;    /* Is macro overlay enabled ? */  /** \todo Check not part of MPE */

  /** \todo Is this the right place for this? What if we need it earlier than this? */
  /* The PFIN module handle */
  /* NB: this field might also point to a FontFusion based PFIN module.
   * \todo : the name will need to be updated to reflect FontFusion possible */
  sw_pfin *ufst ;

  uint32 hw_resolution[2] ;     /* The HW resolution from the page device */ /** \todo Do something with this */
  uint32 media_orientation ;    /* From PageDevice LeadingEdge or from the base CTM */

  /** \todo Possibly integrate into colorstate ?  May want bool for mono_color_mode, etc */
  uint32 default_color_mode ;    /* The colormode on PCL5PrintState creation */
  uint32 current_color_mode ;    /* The current color mode e.g. monochrome, RGB, etc */

  /* Currently only pcl5_plotchar needs this, but it may be more general */
  uint32 setg_required ;        /* (The number of times) SETG is flagged as required */

  OBJECT back_channel ;         /* An OSTRING containing the back channel filename */
  OBJECT save ;                 /**< Save used for restore at end of page. */
} PCL5PrintState ;

/* Reasons for saving the print state */
enum {
  CREATE_MPE = 0,
  CREATE_MACRO_ENV, /* this could be nested in an overlay environment */
  CREATE_OVERLAY_ENV
} ;

Bool underline_callback(PCL5Context *pcl5_ctxt, Bool force_draw) ;
void cursor_has_moved_callback(PCL5Context *pcl5_ctxt) ;

Bool pcl5_printstate_create(PCL5Context *pcl5_ctxt, PCL5ConfigParams* config_params);
Bool pcl5_printstate_destroy(PCL5Context *pcl5_ctxt) ;
Bool pcl5_printstate_reset(PCL5Context *pcl5_ctxt) ;

Bool pcl5_mpe_save(PCL5Context *pcl5_ctxt, int32 reason) ;
Bool pcl5_mpe_restore(PCL5Context *pcl5_ctxt) ;

const PCL5PrintEnvironment* get_default_mpe(PCL5PrintState *p_state) ;
PCL5PrintEnvironment* get_current_mpe(PCL5Context *pcl5_ctxt) ;

Bool set_device_from_MPE(PCL5Context *pcl5_ctxt) ;
Bool set_MPE_from_device(PCL5Context *pcl5_ctxt, uint32 reason) ;

Bool pcl5_set_config_params(OBJECT *odict, PCL5ConfigParams* config_params);
extern PCL5ConfigParams pcl5_config_params;
extern uint32 pcl5_setpcl5params_call_count;


/* ============================================================================
* Log stripped */
#endif /* !__PCL5_STATE_H__ */
