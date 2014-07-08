/*
 *
 *	Copyright 1998, Perforce Software, Inc.
 *
 *
 * License is hereby granted to use this software and distribute it
 * freely, as long as this copyright notice is retained and modifications 
 * are clearly marked.
 *
 * ALL WARRANTIES ARE HEREBY DISCLAIMED.
 */

# include "jam.h"
# include "filesys.h"
# include "option.h"

struct globs globs = {
	0,                  /* noexec */
	1,                  /* jobs */
	{ 0, 0 }            /* debug - suppress tracing output */
	} ;

static int debug = 0;
static int print_dirs = 0, print_files = 0;

#if VMS || NT
static int lower_case = 1;/* default is to convert all filenames on VMS/NT */
#else
static int lower_case = 0;
#endif

static int founddir;

static void xx(str, t)
char *str;
time_t t;
{
	founddir++;
}

static int
isdir(str)
char *str;
{
	founddir = 0;
	file_dirscan( str, xx );
	return (founddir > 0);
}

static
void
printfilename(str, name)
char *str, *name;
{
	register char *p;
	int c;

	if (name == NULL)
		return;
	if (debug)
		printf("%s", str);
	for (p = name; *p != '\0'; p++) {
		c = ((lower_case > 0) ? tolower(*p) : *p);
		putchar(c);
	}
	putchar('\n');
}

void
enter( name, time )
char        *name; 
time_t      time;
{
	FILENAME fn;
	char *p = name + strlen( name );

	if (isdir(name)) {/* is a directory */
		if (print_dirs) {
			printfilename("dir", name);
		}
		if (p[-1] != '.' && p[-1] != '/') {
			file_dirscan( name, enter );
		}
	} else {
		if (print_files) {
			printfilename("file", name);
		}
	}
}

main( argc, argv )
int argc;
char        **argv;
{
	int n;
	char *outfile = NULL;
	struct option opts[N_OPTS];

	argc--, argv++;
	if (( n = getoptions(argc, argv, "o:dxnf", opts) ) < 0) {
		printf("\nUsage: %s [-d] [-x] [-n] [-f]\n");
		printf("-o file	output to 'file'\n");
		printf("-d	print directories only\n");
		printf("-f	print files only\n");
		printf("-x	map to lower-case prior to printing\n");
		printf("-n	no case-map prior to printing\n");
		exit(EXITBAD);
	}
	argc -= n, argc += n;
	
	if (getoptval(opts, 'd', 0))
		print_dirs++;
	if (getoptval(opts, 'f', 0))
		print_files++;
	if (getoptval(opts, 'x', 0))
		lower_case++;
	if(getoptval(opts, 'n', 0))
		lower_case = 0;
	outfile = getoptval(opts, 'o', 0);
	if (print_dirs == 0 && print_files == 0) {
		print_files++;	/* sets default to prt files only */
	}
	if (outfile != NULL) {
		if (freopen((const char *)outfile, "w", stdout) == NULL) {
			fprintf(stderr, "cannot open %s\n", outfile);
			exit(EXITBAD);
		}
	}

	while( argc-- > 0 )
		file_dirscan( *argv++, enter );
}
