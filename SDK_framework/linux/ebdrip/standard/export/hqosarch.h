/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:hqosarch.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Define a classification scheme for platforms.
 *
 * Platforms are made up of identifiers for operating system and architecture,
 * separated by COMPONENT_SEPARATOR, for instance "win_nt-pentium".
 * Programs that run on "win_32" will also run on "win_nt", so
 *
 *     platform_includes("win_32-pentium", "win_nt-pentium")
 *
 * returns a true value. If the first parameter is not one that the function
 * knows about, it will have successive  version parts stripped off in an
 * attempt to find one that is known. For instance, in the call
 *
 *    platform_includes("win_32-pentium", "win_nt_foo_bar_baz-pentium")
 *
 * the OS "win_nt_foo_bar_baz" is not known, so the function will try
 * "win_nt_foo_bar", "win_nt_foo", and finally "win_nt", which it knows.
 * The assumption is that all these operating systems are merely specialised
 * versions of each other. If not, they should be assigned different
 * identifying strings. Architecture identifiers work the same way.
 *
 *   platform_overlaps("win_nt-all", "win-pentium")
 *
 * returns true, because the platforms overlap at operating system "win" and
 * architecture "pentium". Programs written for "win-pentium" will run on both.
 */

#ifndef __HQOSARCH_H__
#define __HQOSARCH_H__

#define OSARCH_NAMESIZE 16
#define PLAT_NAMESIZE (2*OSARCH_NAMESIZE)

#define VERSION_SEPARATOR '_'
#define COMPONENT_SEPARATOR '-'

typedef int (PLATFORM_TEST_FN)(char *,   char *);

extern int platform_includes(char *general,   char *specific);
extern int platform_included(char *specific,  char *general);
extern int platform_overlaps(char *platform1, char *platform2);
extern int platform_identical(char *platform1, char *platform2);
extern int platform_different(char *platform1, char *platform2);

/*
 * Give the most specific code available for the current platform.
 * The result is guaranteed to fit into char[PLAT_NAMESIZE].
 */
extern void host_platform(char *result);

#endif /* __HQOSARCH_H__ */

/*
* Log stripped */
