#ifndef __CORESKIN_H__
#define __CORESKIN_H__

/* $HopeName: SWcoreskin!export:coreskin.h(EBDSDK_P.1) $
 *
 * coreskin.h is the master include for the coreskin compound,
 * ie it is intended to be the first file include by all coreskin files.
 * It can also be use by compounds which will always be built with coreskin.
 * The following is the minimal set necessary to ensure safe compilation, and
 * that source files just have to include headers for the definitions they use,
 * without worrying about the definitions needed by these nested includes.
 *
* Log stripped */

/* ----------------------------- Includes ---------------------------------- */

/* Include coreutil master include file first as built on top of it */
#include "coreutil.h"   /* Includes std.h */

#include "product.h"    /* include pdsconf.h */
/* v20 */
#include "swvalues.h"   /* As pic files need USERVALUE etc, but cant include */

/* coreskin */
#include "skinconf.h"   /* As sets #defines which need to be processed first */

#endif /* protection for multiple inclusion */

/* eof coreskin.h */
