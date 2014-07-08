#ifndef __COREUTIL_H__
#define __COREUTIL_H__
/*
 * $HopeName: SWcoreutil!export:coreutil.h(EBDSDK_P.1) $
 *
 * coreutil.h is the master include for the coreutil compound,
 * ie it is intended to be the first file include by all coreutil files.
 * It can also be use by compounds which will always be built with coreutil.
 * The following is the minimal set necessary to ensure safe compilation, and
 * that source files just have to include headers for the definitions they use,
 * without worrying about the definitions needed by these nested includes.
 */

/*
* Log stripped */


/* ------------------------ Includes --------------------------------------- */

#include "fwcommon.h" /* includes std.h */


#endif /* multiple inclusion protection */

/* EOF coreutil.h */
