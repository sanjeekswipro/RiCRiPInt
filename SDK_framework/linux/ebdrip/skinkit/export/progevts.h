/* Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:progevts.h(EBDSDK_P.1) $
 */

#ifndef __PROGEVTS_H__
#define __PROGEVTS_H__ (1)

/* Initialise progress system event handlers and event generation timer */
int progevts_init(void);

void progevts_enable_times(void);

/* Shutdown the progress system */
void progevts_finish(void);

#endif /* !__PROGEVTS_H__ */

