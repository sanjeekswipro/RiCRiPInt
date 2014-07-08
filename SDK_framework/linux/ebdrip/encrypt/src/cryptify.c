/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $HopeName: HQNencrypt!src:cryptify.c(EBDSDK_P.1) $ */

/*  
* Log stripped */

/*
 * Encrypt the input stream using the HQNencrypt!src:hqcrypt.c method.
 * Can be run as a filter.
 */

/* Turn off ANSI locally to get MS extension for MinGW build. */
#undef __STRICT_ANSI__
#include <stdio.h>
#include <stdlib.h>
#ifdef WIN32
#include <fcntl.h>
#endif

#ifdef MINGW_GCC
#define __STRICT_ANSI__
#endif

#include "std.h"
#include "hqcrypt.h"

static void an_error (char * string, char * arg);
static int encrypt (FILE * fp, uint32 key);

static FILE *fp = NULL;

uint32 xtoi (char * s)
{
  uint32 val = 0;
  static char hex[] = "0123456789abcdefABCDEF";
  char * p;

  while (*s && (p = strchr(hex, *s)) != NULL)
    {
      uint32 digit = (uint32)(p - hex);
      if (digit > 15)
        digit -= 6;
      val = val * 16 + digit;
      s++;
    }
  return val;
}

/* ---------------------------------------------------------------------- */
int main (int argc, char * argv [])
{
  uint32 key;

#ifdef WIN32
  /* Do all i/o in binary.  Other platforms do this already. */
  _fmode = O_BINARY;
  setmode (fileno (stdin), O_BINARY);
  setmode (fileno (stdout), O_BINARY);
#endif

  if (argc < 2)
    an_error ("usage: cryptify key [inputfile]", NULL);

  if (strncmp (argv[1], "0x", 2) == 0 ||
      strncmp (argv[1], "0X", 2) == 0)
    key = xtoi (argv[1] + 2);
  else
    key = atoi (argv[1]);
  argc--;
  argv++;

  if (argc == 1) {
    /* read from stdin */
    fp = stdin ;
    
    if (! encrypt (fp, key)) {
      an_error ("cannot encrypt standard input", NULL);
    }
  } else if (argc == 2) {
    int ok;

    fp = fopen (argv [1], "r");
    if (fp == NULL) {
      an_error ("cannot open file", argv [1]);
    }
    ok = encrypt (fp, key);
    fclose (fp);
    if (! ok) {
      an_error ("cannot encrypt", argv [1]);
    }
  } else {
    an_error ("excess arguments", argv [2]);
  }

  return(0);
}

/* ------------------------------------------------------------------------- */
static void an_error (char * string, char * arg)
{
  fprintf (stderr, "cryptify: %s", string);
  if (arg != NULL)
    fprintf (stderr, " (%s)", arg);
  fprintf (stderr, "\n");

  if (( fp != NULL ) && ( fp != stdin ))
  {
    fclose( fp );
    fp = NULL;
  }

  exit (10);
}

/* ------------------------------------------------------------------------ */
static int encrypt (FILE * fp, uint32 key)
{
  unsigned char buffer[ 16 * 1024 ];
  int32 nbytes;

  /* Include a short header sequence as a "signature" to identify the
   * fact that the file is encrypted, and which encryption scheme was
   * used (the 4th byte is an encryption scheme number).  The first 3
   * bytes are "HQE" to mean "Harlequin Encrypted".
   */
  if (fwrite (HQN_ENCRYPT_LEAD, 1, strlen(HQN_ENCRYPT_LEAD), stdout) != 4)
    an_error ("io error writing stdout", NULL);

  while ((nbytes = (int32)(fread (buffer, 1, sizeof(buffer), fp))) > 0)
  {
    /* Can safely encrypt buffer in situ. */
    HqEncrypt (&key, nbytes, buffer, buffer);
    if (fwrite (buffer, 1, nbytes, stdout) != nbytes)
      an_error ("io error writing stdout", NULL);
  }

  return 1;
}

/* end of cryptify.c */
