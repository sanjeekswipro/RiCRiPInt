/* impl.c.mpsliban: RAVENBROOK MEMORY POOL SYSTEM LIBRARY INTERFACE (ANSI)
 *
 * $Id: mpsliban.c,v 1.18.1.1.1.1 2013/12/19 11:27:09 anon Exp $
 * $HopeName: MMsrc!mpsliban.c(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * .purpose: The purpose of this code is
 *   1. to connect the MPS Library Interface to the ANSI C libraries,
 *      where they exist, and
 *   2. to provide an example of how to implement the MPS Library
 *      Interface.
 *
 * .readership: For MPS client application developers and MPS developers.
 * .sources: design.mps.lib
 *
 *
 * TRANSGRESSIONS (rule.impl.trans)
 *
 * .trans.file: The ANSI standard says (in section 7.9.1) that FILE is an
 * object type, and hence the casts between FILE and mps_lib_FILE (an
 * incomplete type) are not necessarily valid.  We assume that this trick
 * works, however, in all current environments.
 */

#include "mpslib.h"

#include "mpstd.h"
#include "misc.h"

#ifdef MPS_OS_SU
#include "ossu.h"
#endif
#ifdef MPS_OS_XC
#include "osxc.h"
#endif

#ifdef MPS_OS_IA
struct itimerspec; /* stop complaints from time.h */
#endif
#include <time.h>

#ifdef MPS_OS_SU
extern int fputc (int c, FILE *stream);
extern int fputs (const char *s, FILE *stream);
extern clock_t clock(void);
extern long strtol(const char *, char **, int);
/* @@@@ This doesn't do quite the right thing, but will get by. */
#define strtoul(a,b,c) (unsigned long)strtol(a, b, c)
extern void *memset(void *, int, size_t);
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


int MPS_CALL mps_lib_get_EOF(void)
{
  return EOF;
}

mps_lib_FILE *MPS_CALL mps_lib_get_stderr(void)
{
  return (mps_lib_FILE *)stderr; /* see .trans.file */
}

mps_lib_FILE *MPS_CALL mps_lib_get_stdout(void)
{
  return (mps_lib_FILE *)stdout; /* see .trans.file */
}

int MPS_CALL mps_lib_fputc(int c, mps_lib_FILE *stream)
{
  return fputc(c, (FILE *)stream); /* see .trans.file */
}

int MPS_CALL mps_lib_fputs(const char *s, mps_lib_FILE *stream)
{
  return fputs(s, (FILE *)stream); /* see .trans.file */
}


void MPS_CALL mps_lib_assert_fail(const char *message)
{
  fflush(stdout); /* synchronize */
  fprintf(stderr, "\nMPS ASSERTION FAILURE: %s\n", message);
  fflush(stderr); /* make sure the message is output */
  abort();
}


void *MPS_CALL mps_lib_memset(void *s, int c, size_t n)
{
  return memset(s, c, n);
}

void *MPS_CALL mps_lib_memcpy(void *s1, const void *s2, size_t n)
{
  return memcpy(s1, s2, n);
}

int MPS_CALL mps_lib_memcmp(const void *s1, const void *s2, size_t n)
{
  return memcmp(s1, s2, n);
}


/* @@@@ Platform specific conversion? */
/* See http://devworld.apple.com/dev/techsupport/insidemac/OSUtilities/OSUtilities-94.html#MARKER-9-32 */
mps_clock_t MPS_CALL mps_clock(void)
{
  return (mps_clock_t)clock();
}

mps_bool_t MPS_CALL mps_lib_event_filter(const void *event, size_t length)
{
  UNUSED(event) ;
  UNUSED(length) ;
  return TRUE ;
}

unsigned long MPS_CALL mps_lib_telemetry_control(void)
{
  char *s;
  char **null = NULL;

  s = getenv("MPS_TELEMETRY_CONTROL");
  if(s != NULL)
    return strtoul(s, null, 0);
  else
    return 0;
}
