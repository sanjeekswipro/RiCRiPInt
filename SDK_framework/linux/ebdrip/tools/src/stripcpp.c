/* $HopeName: SWtools!src:stripcpp.c(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*  
Change history:
1992-Feb-28-20:28 bear = Keep the Mac punters happy.
 1991-Dec-13-11:38 bear = Created

*/

/* utilities/buildutils/stripcpp stripcpp.c */

#include <stdio.h>
#include <stdlib.h>

#define FALSE 0
#define TRUE  1

int strip_comments (fp)
     FILE * fp;
{
  int  ch = getc (fp);
  if ( ferror( fp ))
    return ( TRUE ) ;
  
  while  (ch != EOF)
    {
      if  (ch == '#')
	{
	  ch = getc (fp);
	  while ((ch != '\n') && (ch != EOF))
	    ch = getc (fp);
	  if  (ch == '\n')  
	    ch = getc (fp);
	}
      else
	{
	  while  ((ch != '\n') && (ch != EOF))
	    {
	      putchar (ch);
	      ch = getc (fp);
	    }
	  if  (ch == '\n')  
	    {
	      putchar (ch);
	      ch = getc (fp);
	    }
	}
    }
  return ( FALSE ) ;
}

/* ------------------------------------------------------------------------- */
static void an_error (string, arg)
  char * string;
  char * arg;
{
  fprintf (stderr, "stripcpp: %s", string);
  if (arg != NULL) {
    fprintf (stderr, " (%s)", arg);
  }
  fprintf (stderr, "\n");
  exit (1);
}

/* ------------------------------------------------------------------------ */
int main (argc, argv)
  int argc;
  char * argv [];
{
  if (argc == 1) {
    /* read from stdin */
    if ( strip_comments (stdin)) {
      an_error ("cannot strip comments from standard input", NULL);
    }
  } else if (argc == 2) {
    FILE * fp;
    int bad;
    
    fp = fopen (argv [1], "r");
    if (fp == NULL) {
      an_error ("cannot open file", argv [1]);
    }
    bad = strip_comments (fp);
    fclose (fp);
    if ( bad ) {
      an_error ("cannot strip comments", argv [1]);
    }
  } else {
    an_error ("excess arguments", argv [2]);
  }
  
  return EXIT_SUCCESS;
}

/* end of utilities/buildutils/stripcpp stripcpp.c */
