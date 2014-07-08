/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:gofiles.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to PS Startup configuration
 */

#ifndef __GOFILES_H__
#define __GOFILES_H__

extern void get_startup_file (int32 i, uint8 **filename, int32 *length);
extern void get_startup_string (uint8 *filename,
                                int32 filelength,
                                uint8 **string,
                                int32 *stringlength);

#endif /* protection for multiple inclusion */

/* Log stripped */
