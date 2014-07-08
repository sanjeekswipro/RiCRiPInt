/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!export:zipdev.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exported interface to the PostScript ZIP device.
 */

#ifndef __ZIPDEV_H__
#define __ZIPDEV_H__  (1)

#include "swdevice.h"   /* DEVICELIST */

struct core_init_fns ; /* from SWcore */

/**
 * \defgroup zipdev PostScript ZIP archive filesystem device.
 * \ingroup devices
 * \{
 */

/** \brief Initialise the C globals for ZIP devices. */
void zipdev_C_globals(struct core_init_fns *fns) ;

/** \brief Unique PostScript device number. */
#define ZIP_DEVICE_TYPE   (26)

/** \brief IO Control opcode to discard all information about an entry. For this
     operation the generic data pointer points to a null-terminated uint8
     string. */
#define ZIP_IOCTL_DISCARD_ENTRY 1

/** \brief IO Control opcode to determine if any data can be read from a file
    without reading any more pieces. Only applicable for stream-read XPS
    packages.
    Returns 1 if the next piece is available for reading, 0 if not, -1 on
    error. */
#define ZIP_IOCTL_NEXT_PIECE_READY 2

/** \brief Iterator for devices that can hold extracted files from a ZIP
 * archive. */
typedef int32 ZIPDEV_DEVICE_ITERATOR;

/**
 * \brief Return the first device that the ZIP device can use to store extracted files.
 *
 * \param[out]  p_iter
 * Pointer to iterator state.
 *
 * \return
 * Pointer to the first device that the ZIP device can use to hold extracted
 * files, or \c NULL if no more devices that can be used.
 */
extern /*@observer@*/ /*@null@*/
DEVICELIST* zipdev_device_first(
/*@out@*/ /*@notnull@*/
  ZIPDEV_DEVICE_ITERATOR* p_iter);

/**
 * \brief Return the next device that the ZIP device can use to store extracted files.
 *
 * \param[in,out] p_iter
 * Pointer to iterator state.
 *
 * \return
 * Pointer to the next device that the ZIP device can use to hold extracted
 * files, or \c NULL if no more devices that can be used.
 */
extern /*@observer@*/ /*@null@*/
DEVICELIST* zipdev_device_next(
/*@in@*/ /*@notnull@*/
  ZIPDEV_DEVICE_ITERATOR* p_iter);

/** \brief Prefix used for names of extracted files. */
#define ZIPDEV_FILE_PREFIX  "ZIP/"

/** \} */

#endif /* !__ZIPDEV_H__ */

/* Log stripped */
