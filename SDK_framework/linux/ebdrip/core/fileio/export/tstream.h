/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!export:tstream.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Tee stream for splitting a stream so that it can be sent to two different
 * destinations
 */

#ifndef __TSTREAM_H__
#define __TSTREAM_H__


extern int32 start_tstream (FILELIST * flptr_original, FILELIST * flptr_diversion,
                            uint8 defer_write);
extern int32 terminate_tstream (FILELIST * flptr);
extern FILELIST * diverted_tstream (void);

#endif /* __TSTREAM_H__ */


/* Log stripped */
