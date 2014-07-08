/* $HopeName: HQNlibicu_3_4!tools:cdata:cdata.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006 Global Graphics Software Ltd.  All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

/*****************************************************************************
PLEASE ALWAYS CALL tidyexit to ensure a proper clean up of heap & files on both
error exit and normal exit. tidyexit takes an exit code which is passed to exit
******************************************************************************/

#ifdef  MACINTOSH
#define DIR_SEP ':'
#else   /* !MACINTOSH */
#define DIR_SEP '/'
#endif  /* !MACINTOSH */

FILE *inputfile = NULL ;
FILE *outputfile = NULL ;

/* define a smaller buffer for dos (large one cause the program to hang).
*/

void tidyexit(int exitcode)
{
  /* for MPW tools make sure the MPW shell heap is as we were given it
     because we share the same heap space...allocated blocks will remain
     allocated, so don't be lazy
  */
  if (( inputfile != NULL ) && ( inputfile != stdin )) {
    fclose( inputfile ) ;
    inputfile = NULL ;
  }

  if (( outputfile != NULL ) && ( outputfile != stdout )) {
    fclose( outputfile ) ;
    outputfile = NULL ;
  }

  exit( exitcode ) ;
}

void usage(void)
{
  fprintf(stderr,
          "usage: cdata [-s symbol] [-o output] [[-l|-b|-d define|-D define] input...]\n"
          "Positional options:\n"
          "\t-h\t\tthis help\n"
          "\t-s symbol\tsymbol for data array\n"
          "\t-l\t\tinput is low-endian\n"
          "\t-b\t\tinput is big-endian\n"
          "\t-d\t\twrap input in #ifdef\n"
          "\t-D\t\twrap input in #ifndef\n"
          );
  tidyexit(0);
}

int main(int argc, char *argv[])
{
  unsigned char * ch ;
  size_t length, total ;
  char *symbol = "data" ;
  char *program = "cdata" ;
  char buffer[BUFSIZ] ;
  int indefine = 0 ;
  int hadinput = 0 ;

  inputfile = NULL ;
  outputfile = stdout;

  for ( program = *argv++ ; --argc ; ++argv ) {
    if ( argv[0][0] == '-' ) {
      switch ( argv[0][1] ) {
      default:
      case 'h':
        usage() ;
      case 's':
        if ( --argc < 1 || outputfile != stdout || hadinput )
          usage() ;
        symbol = *++argv ;
        break ;
      case 'o':
        if ( --argc < 1 || outputfile != stdout || hadinput )
          usage() ;
        if ( (outputfile = fopen(*++argv, "w")) == NULL ) {
          fprintf(stderr, "cdata: cannot open output: %s\n", *argv);
          tidyexit( 0 ) ;
        }
        fprintf(outputfile,
                "/* THIS FILE IS PRODUCED AUTOMATICALLY - DO NOT EDIT IT */\n"
                "#include \"std.h\"\n"
                "const unsigned char %s[] = {\n", symbol);
        break ;
      case 'l':
        if ( indefine )
          usage() ;
        fputs("\n#ifndef highbytefirst", outputfile) ;
        total = 0 ;
        indefine = 1 ;
        hadinput = 1 ;
        break ;
      case 'b':
        if ( indefine )
          usage() ;
        fputs("\n#ifdef highbytefirst", outputfile) ;
        total = 0 ;
        indefine = 1 ;
        hadinput = 1 ;
        break ;
      case 'd':
        if ( --argc < 1 || indefine )
          usage() ;
        fprintf(outputfile, "\n#ifdef %s", *++argv) ;
        total = 0 ;
        indefine = 1 ;
        hadinput = 1 ;
        break ;
      case 'D':
        if ( --argc < 1 || indefine )
          usage() ;
        fprintf(outputfile, "\n#ifdef %s", *++argv) ;
        total = 0 ;
        indefine = 1 ;
        hadinput = 1 ;
        break ;
      }
    } else {
      if ( (inputfile = fopen(*argv, "rb" )) == NULL ) {
        fprintf(stderr, "cdata: cannot open input: %s\n", *argv);
        tidyexit( 0 ) ;
      }

      while ( (length = fread(buffer, 1, BUFSIZ, inputfile)) > 0 ) {
        const char hexdigit[] = "0123456789abcdef" ;

        for ( ch = buffer, total = 0 ; length > 0 ; --length, ++ch, ++total ) {
          if ( total % 16 == 0 )
            fputs("\n", outputfile) ;
          fputs(" 0x", outputfile) ;
          fputc(hexdigit[(*ch >> 4) & 0xf], outputfile) ;
          fputc(hexdigit[*ch & 0xf], outputfile) ;
          fputc(',', outputfile) ;
        }
      }

      if (ferror(inputfile))
        fprintf(stderr, "cdata: error happened when reading %s.\n", *argv);

      fclose(inputfile) ;
      inputfile = NULL ;
      hadinput = 1 ;

      if ( indefine ) {
        fputs("\n#endif", outputfile) ;
        indefine = 0 ;
      }
    }
  }

  if ( indefine )
    usage() ;

  fprintf(outputfile,
          "\n"
          "};\n"
          "/* EOF */\n") ;

  tidyexit( 0 ) ;

  return EXIT_SUCCESS;
}

/*
Log stripped */
