/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* ScriptWorks Source Control - this C program is for creating the date */
/* $HopeName: SWtools!src:gendate.c(EBDSDK_P.1) $ */

#include <stdio.h>
#include <errno.h>
#include <string.h>

#if defined(MACINTOSH) || defined(IBMPC) || defined(WIN32) || defined(linux)
#include <time.h>
#endif	/* MACINTOSH || IBMPC || WIN32 */

#ifdef UNIX
#include <sys/types.h>
#if defined(SGI) || defined(__clipper__) || defined (T188)
#include <sys/time.h>
#else
#include <sys/timeb.h>
#endif /* SGI || clipper || T188 */
#endif /* UNIX */

#include "warnings.h"

extern char *ctime() ;

int main(argc, argv)
     int argc;
     char ** argv;
{

  time_t tloc ;
  char *t ;
  int l ;
  
UNUSED_PARAM(int, argc)
UNUSED_PARAM(char **, argv)

  tloc = time( (time_t *) 0 ) ;
  t = ctime( &tloc ) ;
  l = strlen( t ) ;
  t[ l - 1 ] = '\0' ; 
  printf( "int build_time = %ld ;\n" , (long)tloc ) ;
  printf( "char *build_date = \"%s\" ;\n" , t ) ;

  return (0) ;
}


/* end of gendate.c */
