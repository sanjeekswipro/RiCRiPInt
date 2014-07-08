/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2technical.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Callbacks for the HPGL2 "Technical Extentions" category.
 * (BP, CT, DL, EC, FR, MG, MT, NR, OE, OH, OI, OP, OS, PS, QL, ST, VS are unsupported)
 *
 *   MC    % Merge control
 *   PP    % Pixel Placement
 */

#include "core.h"
#include "hpgl2technical.h"

#include "pcl5context_private.h"
#include "hpgl2scan.h"
#include "printmodel.h"
#include "gstack.h"
#include "graphics.h"
#include "ndisplay.h"
#include "params.h"

void default_HPGL2_technical_info(HPGL2TechnicalInfo* tech_info)
{
  tech_info->pixel_placement = PCL5_INTERSECTION_CENTERED;
}

void hpgl2_set_default_technical_info(HPGL2TechnicalInfo *tech_info,
                                      Bool initialize)
{
  /* This routine for the 'reset' which to match the printers sometimes doesn't
     actually default everything it ought to. */
  if ( initialize ) {
    /* PP value is not defaulted by the IN command [PCL 5 Comparison Guide] */
  } else {
    tech_info->pixel_placement = PCL5_INTERSECTION_CENTERED;
  }
}

void hpgl2_sync_pixelplacement(HPGL2TechnicalInfo* tech_info)
{
  switch ( tech_info->pixel_placement ) {
  case PCL5_INTERSECTION_CENTERED :
    gstateptr->thePDEVinfo.scanconversion = UserParams.PCLScanConversion;
    break;
  case PCL5_GRID_CENTERED :
    gstateptr->thePDEVinfo.scanconversion = SC_RULE_TESSELATE;
    break;
  default:
    /* ignore */
    break;
  }
}

void hpgl2_sync_technical_info(HPGL2TechnicalInfo *tech_info,
                               Bool initialize)
{
  UNUSED_PARAM(Bool, initialize);
  hpgl2_sync_pixelplacement(tech_info);
}

/**
 * Merge Control Operator. The behavior of this operator is odd and not
 * described well in the spec. The PCL rop code is always set by this command;
 * if merge control is disabled, the rop is set to 252 and any code specified
 * with the command is ignored.
 */
Bool hpgl2op_MC(PCL5Context *pcl5_ctxt)
{
  HPGL2Integer mode, opcode;
  uint8 temp;

  /* Read the mode. */
  if (hpgl2_scan_integer(pcl5_ctxt, &mode) <= 0)
    return TRUE;

  /* Abort early if the mode is invalid, otherwise default the opcode. */
  switch (mode) {
    default:
      return TRUE;

    case 0:
      opcode = 252;
      break;

    case 1:
      opcode = 168;
      break;
  }

  /* Read the optional opcode. */
  if (hpgl2_scan_terminator(pcl5_ctxt, &temp) == 0) {
    HPGL2Integer tempOpcode;

    if (hpgl2_scan_separator(pcl5_ctxt) <= 0 ||
        hpgl2_scan_integer(pcl5_ctxt, &tempOpcode) <= 0)
      return TRUE;

    /* Ignore command if ROP code is invalid. */
    if (opcode < 0 || opcode > 255)
      return TRUE;

    /* Any specified opcode is ignored if merge control is being disabled. */
    if (mode != 0)
      opcode = tempOpcode;

    /* Consume the terminator. */
    (void)hpgl2_scan_terminator(pcl5_ctxt, &temp);
  }

  set_current_rop(pcl5_ctxt, opcode);
  return TRUE;
}

/* Pixel Placement
   0 - Grid intersection (default)
   1 - Grid centred
 */
Bool hpgl2op_PP(PCL5Context *pcl5_ctxt)
{
  HPGL2TechnicalInfo *tech_info = get_hpgl2_technical_info(pcl5_ctxt);
  HPGL2Integer pp = PCL5_INTERSECTION_CENTERED;
  uint8 terminator;

  if ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) > 0 ) {
    pp = PCL5_INTERSECTION_CENTERED;
  } else {
    if ( hpgl2_scan_clamped_integer(pcl5_ctxt, &pp) <= 0 )
      return TRUE;

    (void)hpgl2_scan_terminator(pcl5_ctxt, &terminator);
  }

  /* Ignore invalid values. */
  if ( pp == PCL5_INTERSECTION_CENTERED || pp == PCL5_GRID_CENTERED ) {
    tech_info->pixel_placement = pp;

    hpgl2_sync_pixelplacement(tech_info);
  }

  return TRUE;
}

/* ============================================================================
* Log stripped */
