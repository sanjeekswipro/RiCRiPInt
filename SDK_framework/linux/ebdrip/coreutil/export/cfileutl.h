#ifndef __CFILEUTL_H__

/*
 * $HopeName: SWcoreutil!export:cfileutl.h(EBDSDK_P.1) $
 * Core file utility functions 
 */

/*
* Log stripped */


/* ------------------------ Includes --------------------------------------- */

#include "coreutil.h"   /* Includes std.h... */

/* ggcore */
#include "fwstring.h"   /* FwTextString */


#ifdef __cplusplus
extern "C"
{ 
#endif


/* ----------------------- Macros ------------------------------------------ */

/* Even though PostScript defines a flat filing system we impose the following
 * syntax for describing heirarchical filenames:
 *
 * [%<device name>%][<escaped directory name>/]*[<escaped leaf name>]
 *
 * To ensure consistent processing of these filenames the functions exported
 * in this file should be used. For example '/' can appear in any of the <>
 * elements above, as well as indicating a PostScript directory separator.
 *
 * For routines for operating on platform dependent filenames see:
 * application independent - HQNgui_core!export:fwfile.h
 * ScriptWorks specific    - SWcoregui!export:guifns.h
 */

#define PS_DIRECTORY_SEPARATOR  '/'
#define PS_DEVICE_DELIMITER     '%'
#define PS_FILENAME_ESCAPE      '\\'

#ifdef WIN32
#define SUPRA_PLAT  "pc"
#else
#ifdef MACINTOSH
#define SUPRA_PLAT  "macos"
#else
#ifdef UNIX
#define SUPRA_PLAT  "unix"
#else
  HQFAIL_AT_COMPILE_TIME("Unknown supra-plat")
#endif
#endif
#endif

/* ----------------------- Types ------------------------------------------- */

/* PSFilenameSkipToLeafname() returns a pointer to the leafname part of a PostScript
 * filename, returning a pointer to the character immediately after the last
 * unescaped PS_DIRECTORY_SEPARATOR.
 * _unless_ the filename consists of only a device name, in which case a pointer to
 * the device name returned (i.e. ptbzPS).
 */
extern FwTextString PSFilenameSkipToLeafname PROTO
((
  FwTextString ptbzPS                   /* Escaped PostScript filename */
));


/* PSFilenameSkipDevice() skips the device name part (if any) of a postscript filename,
 * returning a pointer to the character immediately after the second PS_DEVICE_DELIMITER,
 * or ptbzPS if there is no device name part.
 */
extern FwTextString PSFilenameSkipDevice PROTO
((
  FwTextString ptbzPS                   /* Escaped PostScript filename */
));


/* PSFilenameAddDirSeparator() adds a terminating PostScript
 * directory separator to a PostScript filename unless:
 * - the filename already has one
 * - the filename is the empty string
 * - the filename consists of just a device
 * It returns TRUE if an attempt is made to add a separator.
 */
extern int32 PSFilenameAddDirSeparator PROTO
((
  FwStrRecord * pPS                     /* Escaped PostScript filename */
));


/* PSPathExtractElement() copies an escaped PostScript path element up to an
 * unescaped PS_DIRECTORY_SEPARATOR or '\0'.
 */
extern uint32 PSPathExtractElement PROTO
((
  FwStrRecord * pOutput,                 /* Escaped PostScript path element */
  FwTextByte ** pptbInput                /* Escaped PostScript path */
));


/* PSPathElementEscape() escapes PS_DIRECTORY_SEPARATOR, PS_DEVICE_DELIMITER and PS_FILENAME_ESCAPE
 * in a PostScript path element with PS_FILENAME_ESCAPE.
 * Returns number of bytes written to the output record.
 */
extern uint32 PSPathElementEscape PROTO
((
  FwStrRecord * pOutput,                 /* Escaped PostScript path element */
  FwTextString  ptbzInput                /* Unescaped PostScript path element */
));


/* PSPathElementEscapeInSitu() is like PSPathElementEscape() but handles an
 * element already in a string record
 * The element is assumed to finish at the record terminator
 * Returns number of bytes of escaped element in the record.
 */
extern uint32 PSPathElementEscapeInSitu PROTO
((
  FwStrRecord * pOutput,                 /* FwStrRecord containing ptbzElement */
  FwTextString  ptbzElement              /* Unescaped PostScript path element within pOutput */
));


/* PSPathUnescapeElement() copies from an escaped PostScript path *pptbInput to pOutput
 * removing PS_FILENAME_ESCAPE characters until reaching an unescaped PS_DIRECTORY_SEPARATOR
 * or the terminator.
 * Returns number of bytes written to the output record.
 */
extern uint32 PSPathUnescapeElement PROTO
((
  FwStrRecord * pOutput,                 /* Unescaped PostScript path element */
  FwTextByte ** pptbInput                /* Escaped PostScript path */
));


/*
 * Get the full platform pathname of the SW or other folder. This string will
 * be terminated with a file separator.
 *
 * The implementation of this invokes a built-in search path to
 * find it.
 *     it first looks in FwAppDir()/,
 *     then in FwAppDir()/../../<SupraPlat>-all/
 *     and finally in FwAppDir()/../../../
 * This function returns NULL if it can't find the folder
 * in any of these places.
 *
 * PlatformSWDir looks for SW, then PRODUCT_SW (if defined), and caches it.
 * PlatformMessagesDir looks for Messages, and caches it.
 */
  extern FwTextString PlatformSWDir(void);
  extern FwTextString PlatformMessagesDir(void);
  extern FwTextString PlatformFindData(FwTextString ptbzName);

/* Convenience functions to return portions of PlatformSWDir() */
  extern FwTextString PlatformSWParent(void);
  extern FwTextString PlatformSWLeafname(void);

#ifdef __cplusplus
};
#endif

#endif /* protection for multiple inclusion */

/* EOF cfileutl.h */
