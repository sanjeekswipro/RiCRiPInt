/* $HopeName: SWtools!src:comppss.c(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*  
Change history:
 1993-Nov-10-18:18 luke
	Created in compress_ps
Merged from:
	utilities/buildutils/pspress/source/cc/pspress.c
        utilities/buildutils/pcbuild/source/c/pspress.c
        utilities/buildutils/compress_ps/source/cc/compress_ps.c
and is now called comppss.c in component compress_ps

1992-Jan-22-18:51 bear = Tidy up error handling for MPW tools.
1991-Sep-10-11:39 paulh = Make sure that control_main returns 0 exit status
1991-Sep-10-11:39 paulh + for success.
1991-Jul-11-13:42 john = integrate change by Luke, done in sw/v20 sources!
1991-Jul-11-12:31 luke = Fix to allow << and >> operators
1991-Jun-26-14:32 john = convert to new make system (use control_main)
1990-Jan-13-17:21 markt: mac specifics

*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FALSE 0
#define TRUE  1

#if  defined(SYSV) || defined( MACINTOSH ) || defined( IBMPC ) || defined (WIN32) || defined(linux)
#define index strchr
#endif

#define GOTO_STATE( _state ) { state = (_state); }
/* the states: */
#define NORMAL     0
#define COMMENT    1
#define WHITESPACE 2
#define STRING     3
#define HEX        4

static void an_error ();
static int compress ();

static char * specials = "()<>[]{}/%\\";
static char * escapes = "nrtbf\\()01234567";

static FILE *fp = ( FILE* )NULL ;

#define output(_c) { putchar (p = (_c)); }

/* ---------------------------------------------------------------------- */
int main (argc, argv)
  int argc;
  char * argv [];
{
  if (argc == 1) {
    /* read from stdin */
    fp = stdin ;
    
    if (! compress (fp)) {
      an_error ("cannot compress standard input", NULL);
    }
  } else if (argc == 2) {
    int ok;

    fp = fopen (argv [1], "r");
    if (fp == NULL) {
      an_error ("cannot open file", argv [1]);
    }
    ok = compress (fp);
    fclose (fp);
    if (! ok) {
      an_error ("cannot compress", argv [1]);
    }
  } else {
    an_error ("excess arguments", argv [2]);
  }
  return EXIT_SUCCESS;
}

/* ------------------------------------------------------------------------- */
static void an_error (string, arg)
  char * string;
  char * arg;
{
  fprintf (stderr, "comppss: %s", string);
  if (arg != NULL) {
    fprintf (stderr, " (%s)", arg);
  }
  fprintf (stderr, "\n");

  if (( fp != ( FILE* )NULL ) && ( fp != stdin ))
  {
    fclose( fp ) ;
    fp = ( FILE* )NULL ;
  }

  exit (10);
}

/* ------------------------------------------------------------------------ */
static int compress (fp)
  FILE * fp; /* the source postscript */
{
  int paren_count;  /* Used to check for correct number of '('. */
  int state; /* state of this interpreter */
  int c = 0 ; /* the current character */
  int p; /* the last character printed */
  int last; /* the previous character seen */

  paren_count = 0;
  state = NORMAL;

  for (;;) {
 
    last = c;
    c = getc (fp);

    switch (state) {

    case NORMAL :

      switch (c) {
      case '(' :
	output (c);
	paren_count++;
        GOTO_STATE (STRING);
	break ;
      case ')' :
        fprintf (stderr, "')' found when not in string\n");
	return FALSE ;
      case '<' :
        last = c ; c = getc( fp ) ;
        if ( c == '<' ) {
          output( last ) ; output( c ) ;
        } else {
          ungetc( c , fp ) ;
          output ( last );
          GOTO_STATE (HEX);
        }
	break ;
      case '>' :
        last = c ; c = getc( fp ) ;
        if ( c == '>' ) {
          output( last ) ; output( c ) ;
        } else {
          fprintf (stderr, "'>' found when not in hex string\n");
          return FALSE ;
        }
        break ;
      case '\n' :
	fflush (stdout); /* in case we overflow an output buffer */
	/* but don't print anything */
	GOTO_STATE (WHITESPACE);
	break;
      case '\r' :
      case '\t' :
      case ' ' :
	/* don't print it */
	GOTO_STATE (WHITESPACE);
	break;
      case '%' :
	/* don't print the comment symbol */
	GOTO_STATE (COMMENT);
	break;
      case '\\' :
        fprintf (stderr, "backslash found outside string\n");
        return FALSE;
      case EOF :
        output ('\n'); /* terminate the file with a newline */
	return TRUE; /* we're done */
      default :
	output (c);
	break;
      }

      break;

    case COMMENT :

      switch (c) {
      case '\n' :
      case '\r' :
	/* end of comment - don't print, inside comment */
	GOTO_STATE (WHITESPACE);
	break;
      case EOF :
	output ('\n');
	fprintf (stderr, "(warning) end of file encountered in comment\n");
	return TRUE ;
      default :
	/* don't print anything inside comment */
	break;
      }
      break;

    case WHITESPACE :
      switch (c) {
      case '(' :
	output (c);
	paren_count++;
	GOTO_STATE (STRING);
	break ;
      case ')' :
        fprintf (stderr, "')' found when not in string\n");
	return FALSE ;
      case '<' :
        last = c ; c = getc( fp ) ;
        if ( c == '<' ) {
          output( last ) ; output( c ) ;
        } else {
          ungetc( c , fp ) ;
          output ( last );
          GOTO_STATE (HEX);
        }
	break ;
      case '>' :
        last = c ; c = getc( fp ) ;
        if ( c == '>' ) {
          output( last ) ; output( c ) ;
        } else {
          fprintf (stderr, "'>' found when not in hex string\n");
          return FALSE ;
        }
        break ;
      case '\n' :
	fflush (stdout); /* in case we overflow an output buffer */
	break;
      case '\r' :
      case '\t' :
      case ' ' :
	/* don't print whitespace */
	break;
      case '%' :
	/* don't print it */
	GOTO_STATE (COMMENT);
	break;
      case '/' :
      case '{' :
      case '}' :
      case '[' :
      case ']' :
	/* special character: doesn't need to be preceded by
	   whitespace under any circumstances */
	output (c);
	GOTO_STATE (NORMAL);
	break;
      case '\\' :
        fprintf (stderr, "backslash found outside string\n");
        return FALSE;
      case EOF :
	output ('\n');
	return TRUE ; /* we're done */
      default :
	/* not a special character so we need a space to separate if
	   the previous printed character wasn't a special */
        if ((char *)index (specials, p) == NULL) {
          output (' ');
        }
        output (c);
	GOTO_STATE (NORMAL);
	break ;
      }
      break;

    case STRING :
      switch (c) {
      case '(' :
	if (last != '\\') {
          paren_count++;
        } else {
	  output ('\\');
	}
	output (c);
	break ;
      case ')' :
        if (last != '\\') {
	  paren_count--;
	  if (paren_count == 0) {
	    GOTO_STATE (NORMAL);
	  }
	} else {
	  output ('\\');
	}
        output (c);
        break;
      case '\n' :
	fflush (stdout); /* in case we overflow an output buffer */
        /* drop through */
      case '\r' :
	if (last != '\\') {
	  /* our ouput contains no newlines (except a terminating one)
	     so convert to backslash-n */
	  output ('\\');
	  output ('n');
	}
        /* otherwise ignore it: was a line continuation mark */
	break;
      case '\\':
        /* ignore it for now - but what we do next depends on remebering it */
        break;
      case EOF :
	fprintf (stderr, "end of file encountered in string\n");
	return FALSE ;
      default :
	if (last == '\\' && (char *)index (escapes, c) != NULL) {
	  /* needs a backslash if it is one of the escaped characters */
          output ('\\');
        }
        output (c);
	break ;
      }
      break;

    case HEX :
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
	  (c >= 'a' && c <= 'f'))
      {
	/* valid hex string character */
	output (c);
      } else {
	switch (c) {
	case '\n' :
	case '\r' :
	case '\t' :
	case ' ' :
	  /* ignore these */
	  break;
	case '>' :
	  output (c);
	  GOTO_STATE (NORMAL);
	  break;
	case EOF :
	  fprintf (stderr, "end of file encountered in hex string\n");
	  return FALSE;
	default :
	  fprintf (stderr, "unexpected character inside hex string '%c'\n", c);
	  return FALSE ;
	}
      }
    }
  }
}

