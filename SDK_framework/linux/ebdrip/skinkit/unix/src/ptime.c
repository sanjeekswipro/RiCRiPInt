/* Copyright (C) 2011 Global Graphics Software Ltd. All Rights Reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!unix:src:ptime.c(EBDSDK_P.1) $
 *
 * \file
 * \brief Skin kit time functions.
 */

#ifdef linux
#define _XOPEN_SOURCE 500
#endif

#include "skinkit.h"
#include "ktime.h"

#include <unistd.h>

void PKMilliSleep(uint32 milliseconds)
{
  usleep(1000 * milliseconds) ;
}

