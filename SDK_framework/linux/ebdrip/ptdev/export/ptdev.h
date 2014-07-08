/* Copyright (C) 2005-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWptdev!export:ptdev.h(EBDSDK_P.1) $
 */

#ifndef __PTDEV_H__
#define __PTDEV_H__

/**
 * \file
 * \brief Private interface for Microsoft print ticket.
 */

#include "swdevice.h" /* DEVICETYPE */

extern DEVICETYPE XpsPrintTicket_Device_Type;

#ifndef INRIP_PTDEV
/*
 * A last error TLS value is not provided by the rip, so ptdev must create one
 * for itself.
 */
int32 ptdev_start_last_error(void);
void ptdev_finish_last_error(void);
#endif /* !INRIP_PTDEV */

#endif /*!__PTDEV_H__*/
