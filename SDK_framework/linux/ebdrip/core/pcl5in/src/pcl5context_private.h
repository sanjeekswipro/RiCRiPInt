/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5context_private.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PCL5Context type definition.
 */
#ifndef _PCL5CONTEXT_PRIVATE_H_
#define _PCL5CONTEXT_PRIVATE_H_

#include "pcl5context.h"
#include "pcl5state.h"
#include "hpgl2dispatch.h"

#include "fileio.h"
#include "objnamer.h"
#include "eventapi.h"

struct PCL5Context {
  /** List links - must be first. */
  sll_link_t sll;
  /** Unique context id - for if we have pcl5open/pcl5execid/etc. */
  pcl5_contextid_t id;

  /** Per-thread core context */
  corecontext_t *corecontext ;

  /** Filelist of PCL5 / HPGL2 source. */
  FILELIST *flptr;
  /** Last character read when parsing HP-GL/2 */
  int32 last_char;

  /* PCL5 scanner and interpreter members. */
  PCL5PrintState *print_state;

  /** Place to hold the callback operator hash used for dispatching
      functions. */
  PCL5FunctTable *pcl5_ops_table ;

  /** Are we scanning a combined command? */
  Bool is_combined_command ;
  /** All used when we have a command cached for some reason. */
  Bool is_cached_command ;
  uint8 cached_operation ;
  uint8 cached_group_char ;
  uint8 cached_termination_char ;
  PCL_VALUE cached_value;
  /** Have we seen a UEL in the stream? */
  Bool UEL_seen ;
  Bool pcl5c_enabled;
  /* Some commands are only allowed in some modes. */
  Bool interpreter_mode_end_graphics_pending ;
  int32 interpreter_mode_on_start_graphics ;
  int32 interpreter_mode ;

  /* What pass through mode are we in, if any? */
  int32 pass_through_mode ;
  /* If a PCL5c is being executed in a PassThrough mode, this
     structure will point to state information required from PCLXL. */
  PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO *state_info ;

  /* HPGL2 scanner and interpreter members. */

  /** Do we need to go back to PCL5. */
  PCL5Operator end_of_hpgl2_op ; /* %A, Esc E or UEL */
  PCL5Numeric end_of_hpgl2_arg ;

  /** Place to hold the callback operator hash used for dispatching
      functions. */
  HPGL2FunctTable *ops_table ;

  /* The global resource caches. */
  PCL5ResourceCaches resource_caches ;

  /* PCL5 config params supplied as a dictionary to pcl5exec_() or setpcl5params_() */
  struct PCL5ConfigParams config_params ;

  OBJECT_NAME_MEMBER
};

extern PCL5_RIP_LifeTime_Context pcl5_rip_context ;

#endif

/* Log stripped */

