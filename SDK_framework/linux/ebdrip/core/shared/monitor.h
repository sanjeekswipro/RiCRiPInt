/** \file
 * \ingroup debug
 *
 * $HopeName: SWcore!shared:monitor.h(EBDSDK_P.1) $
 * $Id: shared:monitor.h,v 1.14.1.1.1.1 2013/12/19 11:24:44 anon Exp $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Monitor functions to implement debug output to log file/window.
 */

#ifndef __MONITOR_H__
#define __MONITOR_H__

#include "uvms.h" /* Most monitor messages should be localised */

void monitorf( uint8 *format, ...);

/*
Log stripped */
#endif /* Protection from multiple inclusion */
