/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:streamdev.h(EBDSDK_P.1) $
 */

#ifndef __STREAMDEV_H__
#define __STREAMDEV_H__

#include "std.h"
#include "ripcall.h"
#include "streams.h" /* HqnReadStream, HqnWriteStream */

/**
 * @file
 *
 * @brief Functions for interfacing the skinkit streaming device with the
 * client stream implementations.
 *
 * <p>The streaming device is implemented in streamdev.c in the skinkit.
 * It is a relative file device, but it supports just two special
 * file descriptors: <code>"/input"</code> and <code>"/output"</code>.
 * The input file will read its data from a <code>HqnReadStream</code>
 * structure; the output file will write its data to a
 * <code>HqnWriteStream</code> structure. The interfaces in this file
 * provide a way for the outside world to connect the
 * <code>HqnReadStream</code> and <code>HqnWriteStream</code>
 * implementations to the streaming device. They also permit the
 * implementation to be cleared again. When no implementation is connected,
 * the streaming device will generate ioerrors upon attempting to access the
 * abstract files.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register an input stream against a file name.
 * @param devname device name;
 * @param pFile file name;
 * @param pRS input stream;
 * @return TRUE if successful, FALSE otherwise.
 */
int32 registerStreamReader(const char * devname, const char * pFile, HqnReadStream *pRS );

/**
 * @brief Register an output stream against a file name.
 * @param devname device name;
 * @param pFile file name;
 * @param pWS write stream;
 * @return TRUE if successful, FALSE otherwise.
 */
int32 registerStreamWriter(const char * devname, const char * pFile, HqnWriteStream *pWS );

/**
 * @brief Unregister the input/output stream associated with the file.
 * @param devname device name;
 * @param pFile file name;
 */
void UnregisterStreamReaderWriter(const char * devname, const char * pFile);

#ifdef __cplusplus
}
#endif

#endif

