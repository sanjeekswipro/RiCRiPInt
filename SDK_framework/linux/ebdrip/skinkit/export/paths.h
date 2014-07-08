/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:paths.h(EBDSDK_P.1) $
 */

#ifndef __PATHS_H__
#define __PATHS_H__

/**
 * @file
 * @brief File name modification and manipulation functions.
 */

/* Platform specific parts */
#include "file.h"


/**
 * @brief Converts a PostScript prefix and relative filename to a
 * platform dependent filename.  It takes any device name from
 * ptbzPSPrefix to form the root, and combines the remainder of
 * ptbzPSPrefix with ptbzPSRelative to form the remainder Returns the
 * number of bytes written to the output buffer.
 */
extern uint32 PSPrefixAndFilenameToPlatform(
  uint8 * pPlatform,              /**< Platform specific name */
  uint8 * ptbzPSPrefix,           /**< Escaped PostScript prefix */
  uint8 * ptbzPSRelative          /**< Escaped PostScript relative filename */
);

/**
 * @brief Returns a pointer to the next element of a Platform
 * filename, returning a pointer to the next DIRECTORY_SEPARATOR, or
 * to the terminator if ptbzInput points to a leafname.
 */
extern uint8 * SkipPathElement( uint8 * ptbzInput );

/**
 * @brief Converts a platform specific path element to a PostScript
 * path element by doing PostScript escaping.  Input is terminated by
 * the platform directory separator DIRECTORY_SEPARATOR or '\\0'.
 */
extern void PlatformPathElementToPS(uint8 * pszOutput, uint8 ** ppszInput);


/** @brief Returns a pointer to the leafname part of a Platform
 * filename, returning a pointer to the character immediately after
 * the last DIRECTORY_SEPARATOR.  If there is is no
 * DIRECTORY_SEPARATOR ptbzPlatform is returned.
 */
extern uint8 * PlatformFilenameSkipToLeafname(uint8 * ptbzPlatform);


/**
 * @brief Converts the root part (if any) of a platform specific
 * filename to an escaped PostScript device name.
 */
extern uint32 PlatformFilenameRootToPS(
  uint8 * pOutput,         /**< Escaped PostScript device name */
  uint8 ** pptbInput       /**< Platform filename */
);

/**
 * @brief Converts a platform specific filename to its PostScript
 * equivalent.
 * @return The number of bytes written to the output record.
 */
extern uint32 PlatformFilenameToPS( uint8 * pszOutput, uint8 * pszInput );

/**
 * @brief Returns a pointer to the leafname part of a PostScript
 * filename, returning a pointer to the character immediately after
 * the last unescaped PS_DIRECTORY_SEPARATOR.  This will be a pointer
 * to the '\\0' terminator if the filename ends with an unescaped
 * PS_DIRECTORY_SEPARATOR.  _unless_ the filename consists of only a
 * device name, in which case a pointer to the device name returned
 * (i.e. ptbzPS).
 *
 * @param ptbzPS is an escaped PostScript filename.
 */
extern uint8 * PSFilenameSkipToLeafname(uint8 * ptbzPS);

/**
 * @brief Skips the device name part (if any) of a PostScript
 * filename, returning a pointer to the character immediately after
 * the second PS_DEVICE_DELIMITER, or ptbzPS if there is no device
 * name part.
 *
 * @param ptbzPS is an escaped PostScript filename.
 */
extern uint8 * PSFilenameSkipDevice(uint8 * ptbzPS);

/**
 * @brief Finds the device name part (if any) of a PostScript filename.
 * (The PS_DEVICE_DELIMITER characters are excluded from the output.)
 *
 * @return The number of bytes written to the output record.
 */
extern uint32 PSFilenameToDevice(uint8 * pszOutput, uint8 * pszInput);

#endif
