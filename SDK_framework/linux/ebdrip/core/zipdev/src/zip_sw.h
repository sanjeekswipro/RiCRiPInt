/** \file
 * \ingroup zipdev
 *
 * $HopeName: COREzipdev!src:zip_sw.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Support for using the ZIP device as the RIP's %os% device.
 */

#ifndef __ZIP_SW_H__
#define __ZIP_SW_H__  (1)

/** \brief Name used for writing extracted files from zipped SW
    archive. This is the what the initial os device name gets renamed
    to. */
#define SWZIP_WRITE_DEV_NAME "_swzipwrite"

/** \brief Name used for reading zipped SW archive. */
#define SWZIP_READ_DEV_NAME "_swzipread"

/** \brief Zip archive name set from SwStart() params. */
extern uint8* zipArchiveName;

/**
 * \brief Make OS device a ZIP device.
 */
extern
void zip_mount_os(void);

/**
 * \brief Unmount OS ZIP device to cleanup any temporary files.
 */
extern
void zip_unmount_os(void);

#endif /* !__ZIP_SW_H__ */

/* Log stripped */

/* eof zip_sw.h */
