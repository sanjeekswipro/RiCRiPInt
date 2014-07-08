/** \file
 * \ingroup otherdevs
 *
 * $HopeName: COREdevices!export:calendar.h(EBDSDK_P.1) $
 * $Id: export:calendar.h,v 1.7.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External declarations for interface to %Calendar% device
 */

#ifndef __CALENDAR_H__
#define __CALENDAR_H__

/** \defgroup otherdevs Non-file based devices
    \ingroup devices */
/** \{ */

/* ----------------------------- Function Prototypes ----------------------- */

int32 get_calendar_params(int32 *year, int32 *month, int32 *day, int32 *hour,
                          int32 *minute, int32 *second, int32 *running);

/** \} */

#endif /* __CALENDAR_H__ */

/* ------------------------------------------------------------------------- */
/* Log stripped */
