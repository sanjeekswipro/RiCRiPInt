/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!src:psock.h(EBDSDK_P.1) $
 * Socket related utility functions with per-platform implementations.
 */

/**
 * @file
 * @ingroup skinkit
 * @brief Socket related utility functions with per-platform implementations.
 */

#include "std.h"
#include "swdevice.h"

/**
 * @brief Performs any platform-specific initialization required to use sockets
 *
 * @param[out] pError
 * Set to an error code if operations fails
 *
 * @return 0 on success, -1 on failure
 */
extern int32 PKSocketInit( int32 * pError );

/**
 * @brief Performs any platform-specific finalization required to use sockets
 *
 * @param[out] pError
 * Set to an error code if operations fails
 *
 * @return 0 on success, -1 on failure
 */
extern int32 PKSocketFinalize( int32 * pError );

/**
 * \brief Opens a socket and sets it to listen for connections
 *
 * Once a connection is established use \c PKAcceptConnection() to accept it.
 *
 * @param[in] port
 * Port number
 *
 * @param[out] pError
 * Set to an error code if operations fails
 *
 * @return non-negative socket descriptor on success, -1 on failure
 */
extern DEVICE_FILEDESCRIPTOR PKOpenServerSocket( int32 port, int32 * pError );

/**
 * \brief Opens a socket connection using the specified address and port.
 *
 * @param[in] pbzAddr
 * The IP address to use when connecting.
 *
 * @param[in] port
 * Port number
 *
 * @param[out] pError
 * Set to an error code if operations fails
 *
 * @return non-negative socket descriptor on success, -1 on failure
 */
extern DEVICE_FILEDESCRIPTOR PKOpenClientSocket( const uint8* pbzAddr, int32 port, int32 * pError );

/**
 * \brief Blocks waiting for an incoming connection
 *
 * @param[in] descriptor
 * Socket descriptor, as returned by PKOpenSocket()
 *
 * @param[out] pError
 * Set to an error code if operations fails
 *
 * @return non-negative socket descriptor for connection on success, -1 on failure
 */
extern DEVICE_FILEDESCRIPTOR PKAcceptConnection( DEVICE_FILEDESCRIPTOR descriptor, int32 * pError );

/**
 * \brief Reads data from a socket
 *
 * @param[in] descriptor
 * Socket descriptor, as returned by PKAcceptConnection()
 *
 * @param[in] buff
 * Buffer to read data into
 *
 * @param[in] len
 * Size of buff
 *
 * @param[out] pError
 * Set to an error code if operations fails
 *
 * @return number of bytes read, -1 on failure
 */
extern int32 PKReadSocket( DEVICE_FILEDESCRIPTOR descriptor, uint8 * buff, int32 len, int32 * pError );

/**
 * \brief Writes data to a socket
 *
 * @param[in] descriptor
 * Socket descriptor, as returned by PKAcceptConnection()
 *
 * @param[in] buff
 * Buffer of data to write
 *
 * @param[in] len
 * Size of buff
 *
 * @param[out] pError
 * Set to an error code if operations fails
 *
 * @return number of bytes written, -1 on failure
 */
extern int32 PKWriteSocket( DEVICE_FILEDESCRIPTOR descriptor, uint8 * buff, int32 len, int32 * pError );

/**
 * \brief Closes a socket
 *
 * @param[in] descriptor
 * Socket descriptor, as returned by PKAcceptConnection()
 *
 * @param[out] pError
 * Set to an error code if operations fails
 *
 * @return 0 on success, -1 on failure
 */
extern int32 PKCloseSocket( DEVICE_FILEDESCRIPTOR descriptor, int32 * pError );


