/* Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:ripthread.h(EBDSDK_P.1) $
 * This file is a header file common to all the core RIP interface C files.
 */

#ifndef __RIPTHREAD_H__
#define __RIPTHREAD_H__

#include "std.h"
#include "mps.h"
#include "dlliface.h"

/** \file
 * \ingroup skinkit
 * \brief RIP lifecycle code.
 */

/* ---------------------------------------------------------------------- */

/** \brief
   Device Type numbering. See Programmer's Reference manual section 5.6.

   This is the means by which a device introduced by the Sys/ExtraDevices file
   is tied to its device type defined in C.

   With the single exception of the file system device type (numbered 0), all
   device types you define MUST have numbers in which the most significant
   16 bits are the OEM number allocated to you as a developer. For the purposes
   of these examples, the number 0xffff is used: this value is reserved
   for test development and should not be used for released products. However
   it will not coincide with any other numbers already used within the system.

   The choice of the least significant 16 bits is arbitrary: so long as the
   numbers used in the ExtraDevices file are consistent with those used in the
   C, everything should work.
*/
#define OEM_NUMBER              0xffff0000

#define MONITOR_DEVICE_TYPE     ( OEM_NUMBER | 1 )
#define CONFIG_DEVICE_TYPE      ( OEM_NUMBER | 2 )
#define PAGEBUFFER_DEVICE_TYPE  ( OEM_NUMBER | 3 )
#define SCREENING_DEVICE_TYPE   ( OEM_NUMBER | 4 )
#define CALENDAR_DEVICE_TYPE    ( OEM_NUMBER | 5 )
#define STREAM_DEVICE_TYPE      ( OEM_NUMBER | 6 )
#define SOCKET_DEVICE_TYPE      ( OEM_NUMBER | 7 )
#define RAM_DEVICE_TYPE         ( OEM_NUMBER | 8 )
#define DISK_DEVICE_TYPE        ( OEM_NUMBER | 9 )
#define HYBRID_DEVICE_TYPE      ( OEM_NUMBER | 10 )
#define PROGRESS_DEVICE_TYPE    ( OEM_NUMBER | 11 )
#define XPS_INPUT_DEVICE_TYPE   ( OEM_NUMBER | 12 )
#define PRINTER_DEVICE_TYPE     ( OEM_NUMBER | 13 )
#ifdef EMBEDDED
#define EMBEDDED_DEVICE_TYPE    ( OEM_NUMBER | 14 )
#endif

/**
 * \brief IOCTL operator code for ending page.
 * This allows devices, such as printerdev, to implements
 * device-specific page ending procedure.
 */
#define DeviceIOCtl_EndPage    ( OEM_NUMBER | 1 )

/**
 * \brief IOCTL opcode for setting the compression mode when storing
 * files on resource-limited devices. The RAM device implements this
 * opcode.
 */
#define DeviceIOCtl_SetCompressionMode ( OEM_NUMBER | 2 )

#define IOCTL_COMPRESSION_OFF 0
#define IOCTL_COMPRESSION_ON 1

/*
   OS_DEVICE_TYPE is predefined in swdevice.h

   There is a small number of device types already built into the rip which
   are available for OEM use. Their device type numbers are listed in the
   swdevice.h header file:
   NULL_DEVICE_TYPE (device type 1)  Like the unix /dev/null device: anything
                                     sent to a file opened on a device of
                                     this type is discarded.
   ABS_DEVICE_TYPE  (device type 10) A non-relative device type. A device
                                     of this type will actually be represented
                                     as a file on the %os% device with the
                                     same name as the device. For example
                                     if you mount a device of this type from
                                     PostScript called "(%state%)", opening a
                                     file on this device will actually open
                                     a file called "%os%state", using the
                                     file system device type calls.
*/


/* ----------------------------------------------------------------------
  Some miscellaneous items
*/

#define SIGNALTYPE void

/** \brief Used to communicate the arrival of an interrupt signal to
the tickle function in egconfig.c */
extern int32 interrupt_signal_seen ;


/** \brief Initialise memory for RIP, and call SwInit(). */
void InitRipMemory( size_t RIP_maxAddressSpace, size_t RIP_workingSizeInBytes,
                    size_t RIP_emergencySizeInBytes, void * pMemory );

/** \brief Sets up the environment for SwDllStart() and then calls it. */
void StartRip(void);

/**
 * \brief Skin-internal function to support \c SwLeSetTickleTimerFunctions().
 */
extern void setTickleTimerFunctions
  (
    SwStartTickleTimerFn * pfnSwStartTickleTimer,
    SwStopTickleTimerFn * pfnSwStopTickleTimer
  );

#endif

