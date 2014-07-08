/* impl.c.version: VERSION INSPECTION
 *
 * $Id: version.c,v 1.13.1.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!version.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 *
 * PURPOSE
 *
 * The purpose of this module is to provide a means by which the
 * version of the MM library being used can be determined.
 *
 * DESIGN
 *
 * .design: See design.mps.version-library, but - to let you in on a
 * secret - it works by declaring a string with all the necessary info
 * in.  */

#include "mpm.h"

SRCID(version, "$Id: version.c,v 1.13.1.1.1.1 2013/12/19 11:27:08 anon Exp $");


/* MPS_RELEASE -- the release name
 *
 * .release: When making a new release, change the expansion of
 * MPS_RELEASE to be a string of the form "release.dylan.crow.2" or
 * whatever.
 */

#define MPS_RELEASE "release.epcore.koi Global Graphics development"


/* MPSCopyrightNotice -- copyright notice for the binary
 *
 * .copyright.year: This one should have the current year in it
 */

char MPSCopyrightNotice[] =
  "Copyright © 2001-2011 Ravenbrook Limited and Global Graphics Software Ltd.";


/* MPSVersion -- return version string
 *
 * The value of MPSVersion is a declared object comprising the
 * concatenation of all the version info.
 */

char MPSVersionString[] =
  "@(#)Ravenbrook MPS, "
  "product." MPS_PROD_STRING ", " MPS_RELEASE ", platform." MPS_PF_STRING
  ", variety." MPS_VARIETY_STRING ", compiled on " __DATE__ " " __TIME__;

char *MPSVersion(void)
{
  return MPSVersionString;
}
