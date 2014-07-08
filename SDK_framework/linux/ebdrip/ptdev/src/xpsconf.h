/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:xpsconf.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
#ifndef __XPSCONF_H__
#define __XPSCONF_H__  (1)

/**
 * @file
 * @brief Configuration of PrintTicket device.
 */

#define FILE_BUFFER_SIZE  (16384)

/**
 * @brief cfg_start
 */
int32 cfg_start(void);

#define JOB_LEVEL       (0)
#define DOCUMENT_LEVEL  (1)
#define PAGE_LEVEL      (2)

/**
 * @brief Opening a configuration file.  If the file is a start file then we need to
 * handle a PrintTicket being sent before serving up configuration PS
 */
int32 cfg_open(
  int32 level,
  int32 start);

/**
 * @brief cfg_read
 */
int32 cfg_read(
  uint8*  buffer,
  int32*  len);

/**
 * @brief cfg_write
 */
int32 cfg_write(
  uint8*  buffer,
  int32*  len);

/**
 * @brief cfg_close
 */
int32 cfg_close(
  int32 level,
  int32 start,
  int32 abort);

/**
 * @brief cfg_eof
 */
int32 cfg_eof(
  int32 level,
  int32 start);

/**
 * @brief cfg_end
 */
int32 cfg_end(void);

/**
 * \brief Check the digital signing status of the currently-executing XPS
 * job, and return the result as a string for storage in the
 * \c DigitalSignatureStatus parameter of the Print Ticket device.
 *
 * This function forms part of the protocol for checking digital
 * signatures via the Print Ticket device using PostScript control code.
 * The Print Ticket device implements a parameter called
 * \c DigitalSignatureStatus, whose value is a string. By setting this
 * parameter to the special value \c "CheckNow", PostScript control code
 * can request a digital signature check on demand. This is useful, because
 * the check can be quite expensive for large files, and it isn't always
 * necessary to care about digital signatures. For example, if the
 * Print Ticket specifies \c PrintInvalidSignatures for the
 * \c JobDigitalSignatureProcessing feature, there is no need to check
 * the signature at all: the job can be printed anyway. The RIP uses
 * PostScript code to handle all aspects of Print Ticket configuration,
 * so it can easily use the \c "CheckNow" parameter value to perform
 * a signature check.
 *
 * When the Print Ticket device receives a request to set the
 * \c DigitalSignatureStatus parameter to "CheckNow", it does \e not actually
 * store that string as the new value of the parameter. Instead, it is
 * intercepted as a command. In response to that command, the Print Ticket
 * device makes a call to this function, cfgCheckDigitalSignatures().
 * Whatever string is returned becomes the new value for the parameter.
 * In this way, the information is effectively returned to the world
 * of PostScript. For example, the following PostScript control fragment
 * might be used to check the signature:
 *
 \verbatim
    (%xpspt%) << /DigitalSignatureStatus (CheckNow) >> setdevparams
    (%xpspt%) currentdevparams /DigitalSignatureStatus get cvn
    % digsig result now at the top of stack
 \endverbatim
 *
 * This function does not implement any digital signature checking
 * directly. Instead, it calls out to an optional external agent to perform
 * the check.
 *
 * \return A NUL-terminated PostScript string. The caller should not free this
 * pointer - it will point to constant, literal string data. The possible
 * results are:
 *
 *   - \c "NoFacility", meaning that the application or driver has no
 *     functionality for checking the signature. The job should be treated
 *     as having a potentially-invalid signature.
 *
 *   - \c "NotSigned", meaning that the job was checked successfully, and
 *     found not to have any signatures. It can be printed in all cases.
 *
 *   - \c "SignedInvalid", meaning that the job was checked successfully, and
 *     found to have an invalid signature. Unless the Print Ticket permits
 *     it, the job should not be printed.
 *
 *   - \c "SignedValid", meaning that the job was checked successfully, and
 *     found to have a valid signature. The job can be printed.
 *
 *   - \c "CheckingError", meaning that an attempt was made to check the
 *     job, but there was an internal error. The job should be treated as
 *     having a potentially-invalid signature.
 *
 *   - \c "NotChecked", meaning that the application has supplied a function
 *     to check the job, but the function did nothing. This would be
 *     unusual, but should be treated the same as \c "NoFacility".
 */
const char *cfgCheckDigitalSignatures(void);

#endif /* !__XPSCONF_H__ */


/* eof config.h */
