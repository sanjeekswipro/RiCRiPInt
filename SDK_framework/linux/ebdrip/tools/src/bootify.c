/*
 * $HopeName: SWtools!src:bootify.c(EBDSDK_P.1) $
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
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

/* Some compilers have the audacity to swap '\n' and '\r', so : */
#define  LF  0x0A
#define  CR  0x0D

#if defined(WIN32)
#define DIR_SEP '\\'
#else
#define DIR_SEP '/'
#endif  /* !WIN32 */

FILE *inputfile = ( FILE* )NULL ;
FILE *outputfile = ( FILE* )NULL ;
int debugoutput = 0 ;

#define LENGTH 100 /* divide the input into that many strings */

unsigned char *string = ( unsigned char* )NULL ;

/* define a smaller buffer for dos (large one cause the program to hang).
*/

#ifdef _DOS
#define STRINGSIZE 32768
#else
#define STRINGSIZE 65536
#endif

static void ourbcopy (here, there, count)
  unsigned char * here, * there ;
  int count ;
{
  if ( (int) here < (int) there ) {
	here += count - 1 ;
	there += count - 1 ;
	while ( count-- ) {
	  *there-- = *here-- ;
	}
  } else {
	while ( count-- ) {
	  *there++ = *here++ ;
	}
  }
}

void tidyexit( exitcode )
  int  exitcode ;
{
  /* for MPW tools make sure the MPW shell heap is as we were given it
	 because we share the same heap space...allocated blocks will remain
	 allocated, so don't be lazy
  */
  if ( string != ( unsigned char* )NULL ) {
	free( string ) ;
	string = ( unsigned char* )NULL ;
  }

  if (( inputfile != ( FILE* )NULL ) && ( inputfile != stdin )) {
	fclose( inputfile ) ;
	inputfile = ( FILE* )NULL ;
  }

  if (( outputfile != ( FILE* )NULL ) && ( outputfile != stdout )) {
	fclose( outputfile ) ;
	outputfile = ( FILE* )NULL ;
  }

  exit( exitcode ) ;
}

int main(argc, argv)
  int argc;
  char * argv [];
{
  unsigned char toout ;
  unsigned char * ch ;
  int length, i, loop, remaining_length, chunk ;

  /* options */
  int rsourceformat = 0 ;
  int newstyle = 0;
  char derivative_filename [1024];

  string = (unsigned char *) malloc (STRINGSIZE);
  if (string == NULL) {
	fprintf (stderr, "bootify: failed to allocate string storage\n");
	tidyexit (0);
  }

  if ((strcmp (argv [1], "-h") == 0) || (argc > 4)) {
	fprintf (stderr, "usage: bootify [-n] [-r] [-d] [input [output]]\n\
Positional options: -h this help, -n newstyle, -r MPW source rez format\n\
-d debug output with c comments\n");
	tidyexit (0);
  }

  if (argc > 1) {
	if (strcmp (argv [1], "-n") == 0) {
	  argc--;
	  argv++;
	  newstyle = 1;
	}
	if ( strcmp( argv[ 1 ], "-r" ) == 0 ) {
	  argc--;
	  argv++;
	  rsourceformat = 1 ;
	}
	if ( strcmp( argv[ 1 ], "-d" ) == 0 ) {
	  argc--;
	  argv++;
	  debugoutput = 1 ;
	}
  }

  if (argc < 2) {
	inputfile = stdin;
	strcpy (derivative_filename, "unknown");
  } else {
	char *c = derivative_filename;
	if (( inputfile = fopen( argv [1] , "r" )) == ( FILE * )NULL ) {
	  fprintf (stderr, "bootify: cannot open input: %s\n", argv [1]);
	  tidyexit( 0 ) ;
	}
	strcpy (derivative_filename, argv [1]);
	while (*c) {
	  (*c) = tolower (*c);
	  c++;
	}
  }

  if (argc < 3) {
	outputfile = stdout;
  } else {
	if (( outputfile = fopen( argv [2] , "w" )) == ( FILE * )NULL ) {
	  fprintf (stderr, "bootify: cannot open output: %s\n", argv [2]);
	  tidyexit( 0 ) ;
	}
	strcpy (derivative_filename, argv [2]);
  }

  ch = (unsigned char *) (derivative_filename + strlen (derivative_filename));
  for (;;) {
	if (* ch == '.') {
	  * ch = '\0';
	} else if (* ch == DIR_SEP) {
	  strcpy (derivative_filename, (char *)ch + 1);
	  break;
	}
	if (ch == (unsigned char *) derivative_filename)
	  break;
	ch--;
  }

  /* read the whole input file: if compressed, this will typically be a
	 single string, terminating with a newline, but it could be separate
	 strings. Watch for trailing '\' meaning 'continuation line'.
   */
  ch = string ;
  if (newstyle) {
	ch += 2;                          /* reserve room for length count */
	remaining_length = STRINGSIZE - 2 ;
  }
  else
	remaining_length = STRINGSIZE;

  while (fgets ((char *)ch, remaining_length, inputfile) != NULL) {

	int length = strlen ((char *)ch);

	if (remaining_length <= 0) {
	  fprintf (stderr, "bootify: out of workspace\n");
	  tidyexit (10);
	}

	while (* ch != '\0') {
	  if (* ch == '\\') {
	int shift = 1;

		switch (* (ch + 1)) {
	case 'n' :
	  * ch = LF ;
	  break;
	case 'r' :
	  * ch = CR ;
	  break;
	case 't' :
	  * ch = '\t';
	  break;
	case 'b' :
	  * ch = '\b';
	  break;
	case 'f' :
	  * ch = '\f';
	  break;
	case '\\' :
	  break;
	case '0' :
	case '1' :
	case '2' :
	case '3' :
	case '4' :
	case '5' :
	case '6' :
	case '7' :
		  {
			unsigned int i = (int) * (ch + 1) - (int) '0';
			if (* (ch + 2) >= '0' && * (ch + 2) <= '7') {
			  shift++;
			  i = i * 8 + (int) * (ch + 2) - (int) '0';
			  if (* (ch + 3) >= '0' && * (ch + 3) <= '7') {
				shift++;
			i = i * 8 + (int) * (ch + 3) - (int) '0';
			  }
		}
			* ch = (unsigned char) i;
		  }
	  break ;
	case '\0' :
	  * ch = '\0';
	  continue;
	case '(' :
	case ')' :
	default :
	  * ch = * (ch + 1);
	  shift = 1;
	  break;
	}

	ourbcopy(ch + 1 + shift , ch + 1 , strlen ((char *)ch) - shift ) ;
	  }

	  ch++;
	  remaining_length--;
	}
  }

  /* pad the rest of the string with nulls */
  while (remaining_length-- > 0) {
	* ch ++ = '\0';
  }

  /* fill in the count if necessary: the xor will mean it is in clear by the
	 time it is output */
  ch = string;
  if (newstyle) {
	length = strlen ((char *)string + 2);
	string [0] = (length >> 8) ^ 0xaa;
	string [1] = (length & 0xff) ^ 0xaa;
	length += 2;
  } else {
	length = strlen ((char *)string);
  }

  /* apparently, some compilers cannot cope with very long string constants,
	 so divide it up into a number of strings here, but treat it as one
	 long string when used (padded with trailing nulls in the last string).
	 This only uses up to one component string's worth of extra memory.
   */

  /* remember, we need one extra for the null, but the formula is (n-1)/d+1 */
  chunk = length / LENGTH + 1 ;

  fprintf( outputfile ,
	 "/* THIS FILE IS PRODUCED AUTOMATICALLY - DO NOT EDIT IT */\n"
  );

  /* generate a resource source format file for MPW resource compiler */
  if ( rsourceformat ) {
	if (newstyle) {
	  fprintf( outputfile , "#include \"macres.h\"\n\n" ) ;
	  fprintf( outputfile , "type RESTYPE (RESID_BOOT) " ) ;
	}
	else {
	  fprintf( outputfile , "#include \"macresources.h\"\n\n" ) ;
	  fprintf( outputfile , "type RESTYPE (RESID_bootstrap) " ) ;
	}
	fprintf( outputfile , "{ array [ %d ] { array [%d] { unsigned byte; }; }; };\n",
		chunk, LENGTH
		) ;
	if (newstyle)
	  fprintf( outputfile , "resource RESTYPE (RESID_BOOT, \"%s\", RESATTRS_BOOT) { {\n", derivative_filename ) ;
	else
	  fprintf( outputfile , "resource RESTYPE (RESID_bootstrap, RESATTRS) { {\n" ) ;
  } else {
	fprintf( outputfile , "unsigned char %s [%d][%d] = {\n",
	  newstyle ? derivative_filename : "bootup",
	  chunk, LENGTH
	) ;
  }

  for ( loop = 0 ; loop < chunk ; ++loop ) {
	unsigned char *och ;

	och = ch ;
	fprintf( outputfile , "{ " ) ;
	for ( i = 0 ; i < LENGTH ; ++i ) {
	  if (( i % 10 == 0 ) && ( i != 0 ))
	{
	  if ( debugoutput )
	  {
		fprintf( outputfile, "\t/* " ) ;
		do
		  {
		/*
		  eliminate * from the output, we dont want
		  slash * and * slash do we now?
		*/

		fputc((( !isprint( *och )) ||
			   ( *och == '*' )) ? '.' : *och, outputfile ) ;
		och++ ;
		  } while ( och < ch ) ;
		fprintf( outputfile , " */\n" ) ;
		och = ch ;
	  }
	  else
		fprintf( outputfile, "\n" ) ;
	}
	  toout = * ch ;
	  toout = ( toout ^ 0xaa ) ; /* encrypt the character - exclusive or */
	  fprintf( outputfile , "0x%02x%c " ,
		( unsigned char )toout,
		( i+1 == LENGTH ) ? ' ' : (( rsourceformat ) ? ';' : ',' )
	  ) ;
	  ch ++;
	}

	fprintf( outputfile , "}%c\n",
	  ( loop+1 == chunk ) ? ' ' : (( rsourceformat ) ? ';' : ',' )
	);
  }

  if ( rsourceformat ) {
	fprintf( outputfile , "} " ) ;
  }
  fprintf( outputfile , "} ;\n" ) ;

  tidyexit( 0 ) ;

  return EXIT_SUCCESS;
}

/* Log stripped */
