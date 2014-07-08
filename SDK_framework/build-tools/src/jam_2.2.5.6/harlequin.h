/*
 * Copyright 1999 Harlequin Group plc.
 *
 * This file is part of the Jam Dougnut
 */

/*
 * harlequin.c - the harlequin-specific parts of jam
 *
 * External functions:
 *
 * EXPAND.C
 *  construct_path( char *buffer, char *end, char *nextpart ) - combine
 *    two strings as if they were path elements, adding a file separator
 *    only if necessary, and interpretting ".."
 *  qsort_compar() - simple wrapper for strcmp
 *
 * COMPILE.C
 *	builtin_open() - FILEOPEN rule
 *	builtin_write() - FILEWRITE rule
 *	builtin_close() - FILECLOSE rule
 *
 * (Common)
 *  strdup( char *orig ) - for the macintosh, which doesn't have one
 *    under MPW
 */


/*
 * The file separators on each platform
 */

# ifdef macintosh
# define FS ':'
# endif
# ifdef unix
# define FS '/'
# endif
# ifdef NT
# define FS '\\'
# endif

/*
 * Some harlequin-specific symbols
 */

#define SWIGPATCH "6"
#define SWIGVERSION VERSION "." PATCHLEVEL "." SWIGPATCH

#define HQNSYMS "SWIGVERSION=" SWIGVERSION


void construct_path();
int qsort_compar( void const *, void const * );

void builtin_open();
void builtin_write();
void builtin_close();

# if defined(macintosh) && !defined(CW)
char *strdup();
# endif
