/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:twocharops.c(EBDSDK_P.1) $
 * $Id: src:twocharops.c,v 1.25.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for two character escape sequences.
 *
 * The function signatures need to match the parameterized escape
 * sequence function signtuares so we may have a single lookup
 * table. Hence the comment about ignoring the params.
 */

#include "core.h"
#include "twocharops.h"
#include "jobcontrol.h"
#include "misc.h"
#include "pagecontrol.h"
#include "pcl5context_private.h"
#include "pcl5scan.h"
#include "pcl5fonts.h"

#include "monitor.h"

/* ESC-Y */
/* Enable display functions mode. */
Bool pcl5op_Y(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  PageCtrlInfo *page_info ;
  FontSelInfo *font_sel_info ;
  FontInfo *font_info ;
  Font *font ;
  FILELIST *flptr ;
  Bool pcl5_display_functions = TRUE ;
  int32 ch ;
  int32 old_mode ;
  int32 old_line_termination_mode ;
  PCL5Numeric eof_value = { 0 } ;

  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  if (! pcl5_start_text_run(pcl5_ctxt, &font_sel_info, &font_info, &font))
    return TRUE ;

  page_info = get_page_ctrl_info(pcl5_ctxt) ;
  flptr = pcl5_ctxt->flptr ;

  /* Print all characters. */
  old_mode = pcl5_ctxt->interpreter_mode ;
  pcl5_ctxt->interpreter_mode = TRANSPARENT_PRINT_DATA_MODE ;

  /* CR to be treated as if though it were a CR LF */
  old_line_termination_mode = page_info->line_termination ;
  page_info->line_termination = 3 ;

  while (pcl5_display_functions) {
    if ((ch = Getc(pcl5_ctxt->flptr)) != EOF) {
      if (! pcl5_text_character(pcl5_ctxt,  font_info, &font, (uint16)ch))
        continue ;
      switch (ch) {
      case ESCAPE:
        if ((ch = Getc(pcl5_ctxt->flptr)) != EOF) {
          if (! pcl5_text_character(pcl5_ctxt,  font_info, &font, (uint16)ch))
            continue ;
          if (ch == 'Z')
            pcl5_display_functions = FALSE ;
        } else {
          pcl5_display_functions = FALSE ;
          if (! pcl5op_E(pcl5_ctxt, NOSIGN, eof_value))
            continue ;
        }
        break ;
      case CR:
        if (! pcl5op_control_CR(pcl5_ctxt))
          continue ;
        break ;
      default:
        break ;
      }
    } else {
      pcl5_display_functions = FALSE ;
      if (! pcl5op_E(pcl5_ctxt, NOSIGN, eof_value))
        continue ;
    }
  }

#ifdef RQ64403
  (void)pcl5_set_text_run_restart_required(pcl5_ctxt, TRUE) ;
#else
  (void)pcl5_end_text_run(pcl5_ctxt, TRUE) ;
#endif
  pcl5_ctxt->interpreter_mode = old_mode ;
  page_info->line_termination = old_line_termination_mode ;

  return TRUE ;
}

/* ESC-Z */
/* Disable display functions mode. */
Bool pcl5op_Z(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{

  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  /* If this is seen, it means an enable display functions mode was
     never set so we should do nothing. */
  return TRUE ;
}

/* ESC-z */
/* Ignore explicit_positive and value as they will NOT be set for two
   character escape sequences. */
Bool pcl5op_z(PCL5Context *pcl5_ctxt, int32 explicit_sign, PCL5Numeric value)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(int32, explicit_sign) ;
  UNUSED_PARAM(PCL5Numeric, value) ;

  return TRUE ;
}

/* ============================================================================
* Log stripped */
