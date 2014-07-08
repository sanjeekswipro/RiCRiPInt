/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:xpsconfstate.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#include "xpsconfstate.h"

/**
 * @file
 * @brief Usage of XPS PrintTicket device.
 */


/**
 * @brief  Whether the XPS PrintTicket device should be used or not.
 *
 * @see xpspt_setEnabled()
 */
static int32 pt_enabled = TRUE;

/**
 * @brief  Whether certain XPS PrintTicket options should be emulated
 * in XPS output.
 *
 * @see xpspt_setEmulationEnabled()
 */
static int32 pt_emulation_enabled = FALSE;


void xpspt_setEnabled (int32 bEnabled)
{
  pt_enabled = bEnabled;
}

int32 xpspt_getEnabled ()
{
  return pt_enabled;
}

void xpspt_setEmulationEnabled (int32 bEnabled)
{
  pt_emulation_enabled = bEnabled;
}

int32 xpspt_getEmulationEnabled ()
{
  return pt_enabled && pt_emulation_enabled;
}

void init_C_globals_xpsconfstate(void)
{
  pt_emulation_enabled = FALSE;
  pt_enabled = TRUE;
}


/* EOF xpsconfstate.c */
