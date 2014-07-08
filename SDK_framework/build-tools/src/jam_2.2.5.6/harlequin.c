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

# include "jam.h"
# include "lists.h"
# include "newstr.h"
# include "parse.h"
# include "variable.h"

# include "harlequin.h"


/*
 * An empty jambase -- we don't use a built-in one
 */

char *jambase[] = { 0 };


/*
 * construct_path()
 */

void
construct_path( buffer, end, nextpart )
char *buffer;
char *end;
char *nextpart;
{
	/*
	 * If end == buffer, then just copy in nextpart
	 */

	if( buffer == end )
	{
	    strcpy( buffer, nextpart );
	    return;
	}

	/*
	 * First ensure there is a file separator on the end of buffer
	 * and eliminate the first from nextpart (if any exists)
	 */

	if( end[-1] != FS )   *end++ = FS;
	if( *nextpart == FS ) nextpart++;

	/*
	 * buffer =~ /$  (  :$ on mac )
	 *         end^   end^
	 *       ^/?     (     ^:? )
	 *  nextpart^     nextpart^
	 *
	 * Continue processing ".."s until we run out of them (from the
	 * beginning of nextpart
	 */

	while( nextpart[0] == '.'
# ifdef macintosh
	    || nextpart[0] == ':'
# endif
	     )
	{
	    char *lastseparator = end - 2;

            /*
             * Check that this is indeed a ../ or :: sequence, and
             * advance over the next file separator
             */
	    if( nextpart[0] == '.' )
	    {
		if( ! nextpart[1] ) /* /.$ */
		{
		    nextpart++;
		    break;
		}
		if( nextpart[1] == FS ) /* /./ */
		{
		    nextpart += 2;
		    continue;
	    	}
		if( nextpart[1] != '.' ) /* /.a */
		    break;

		if( nextpart[2] && nextpart[2] != FS ) /* /..a */
		    break;

		if( *(nextpart+=2) ) nextpart++;
	    }
# ifdef macintosh
            else
              /* There must have been a fileseparator before the one
               * that nextpart is looking at, so no check needed, just
               * advance. */
              nextpart++;
# endif

	    /*
	     * buffer =~ /$      (   :$ on mac )
	     *        ls^ ^end    ls^ ^end
	     *       ^/?..(/|$)  (     ^:: )
	     *      nextpart^     nextpart^
	     *
	     * Attempt to back out the last path element
	     */

	    while( lastseparator >= buffer && *lastseparator != FS )
		lastseparator--;

	    if( lastseparator >= buffer )
	    {
		/*
		 * buffer =~ /[^/]* /$     (  :[^:]*:$ on mac )
		 *         ls^       ^end   ls^      ^end
		 *       ^/?..(/|$)       (     ^:: )
		 *      nextpart^          nextpart^
		 *
		 * We should be able to remove the bit between
		 * lastseparator and end, unless it happens to be a ".."
		 * sequence.
		 */

		/*
		 * end - lastseparator is too many characters to be
		 * special
		 */

		if( end - lastseparator > 4

		/*
		 * there is at least one character between them that is
		 * not a '.'.
		 */

		||( end - lastseparator > 2 &&
		    ( lastseparator[1] != '.' || end[-2] != '.' )
		  )
		)
		{
		    end = lastseparator + 1;

		    /*
		     * buffer =~ /[^/]* /$  (  :[^:]*:$ on mac )
		     *            ^end         ^end
		     */

		    continue;
		}
	    }

	    /*
	     * buffer =~ /$      (  :$ on mac )
	     *            ^end       ^end
	     *       ^/?..(/|$)  (     ^:: )
	     *      nextpart^     nextpart^
	     *
	     * OK, so for some reason or another, we can't back out the
	     * last part of buffer.  We must append the correct ".."
	     * sequence.
	     */

# ifdef macintosh
	    *end++ = ':';
# else
	    *end++ = '.';
	    *end++ = '.';
	    *end++ = FS;
# endif
	    /*
	     * buffer =~ /../../$     (  :::$ on mac )
	     *                  ^end        ^end
	     */
	}

	/*
	 * buffer =~ /$  (  :$ on mac )
	 *         end^   end^
	 *       ^/?     (     ^:?[^:] )
	 *  nextpart^     nextpart^
	 *
	 * If nextpart != "", just concat it on the end.
	 * If nextpart == "", then remove the trailing file separator unless either
	 *  a) Mac: we have two file separators on the end, or buffer == ":"
	 *  b) Unix: buffer == "/"
	 *  c) NT: buffer == "\" or buffer == "?:\"
	 */

	if( *nextpart )
	    strcpy( end, nextpart );
	else
	{
# ifdef macintosh
	    if( end[-2] == ':' || end == buffer + 1 ) /* ::$ or ^:$ */
		end[0] = '\0';                        /*   ^      ^ */
	    else                                      /* [^:]:$ */
		end[-1] = '\0';                       /*      ^ */
# endif

# ifdef unix
	    if( end == buffer + 1 )                   /* ^/$ */
		end[0] = '\0';                        /*   ^ */
	    else
		end[-1] = '\0';
# endif

# ifdef NT
	    if( ( end[-2] == ':' && end == buffer + 3 ) || end == buffer + 1 )
	                                              /* ^.:\$ or ^\$ */
		end[0] = '\0';                        /*     ^      ^ */
	    else
		end[-1] = '\0';
# endif
	}
}


/*
 * qsort_compar()
 */

int
qsort_compar( s1, s2 )
void const *s1;
void const *s2;
{
	return strcmp( *(char**)s1, *(char**)s2 );
}


/*
 * builtin_close() - FILECLOSE rule
 *
 * The FILECLOSE builtin rule closes the file with the handle stored
 * in the given variable.
 */

void
builtin_close( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST *fpl = var_collapse( L0, lol_get( args, 0 ) );
	LIST *fps;
	FILE *handle;

	for( fps = fpl; fps; fps = list_next( fps ) )
	{
	    handle = (FILE*)atol( fps->value );
	    if( fclose( handle ) )
	    	printf( "warning: invalid file handle given to FILECLOSE\n" );
	}
	list_free( fpl );
}

/*
 * builtin_open() - FILEOPEN rule
 *
 * The FILEOPEN builtin rule opens a named file and associates it
 * with the named variable
 */

void
builtin_open( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST *fpl = var_collapse( L0, lol_get( args, 0 ) );
	LIST *fnl = var_collapse( L0, lol_get( args, 1 ) );
	LIST *fml = var_collapse( L0, lol_get( args, 2 ) );
	LIST *fps;
	char *filename, *mode;
	FILE *handle;

	/* Take the first of fns as the filename to open */
	if( fnl )
	    filename = fnl->value ;
	else
	{
	    printf( "warning: no filename given on rule FILEOPEN\n" );
	    list_free( fpl );
	    list_free( fml );
	    return;
	}

	/* Take the first of fms as the mode in which to open it */
	if( fml )
	    mode = fml->value ;
	else
	    mode = "r+" ;

	handle = fopen( filename, mode );
	
	if( ! handle )
	{
	    printf( "warning: failed to open file '%s'\n", filename );
	    list_free( fpl );
	    list_free( fnl );
	    list_free( fml );
	    return;
	}

	/*
	 * Reuse filename pointer to store a string containing the
	 * integer representation of the FILE*
	 */
	filename = malloc( 3 * sizeof( FILE* ) + 1 );
	sprintf( filename, "%ld", (long)handle );

	/*
	 * Put a newstr() copy in mode, and use that to initialise the
	 * jam variables, before free()ing filename
	 */
	mode = newstr( filename ); free( filename );

	for( fps = fpl; fps; fps = list_next( fps ) )
	    var_set( fps->value,
	    	list_new( L0, copystr( mode ) ),
	    	VAR_SET );

	list_free( fpl );
	list_free( fnl );
	list_free( fml );
	freestr( mode );
}

/*
 * builtin_write() - FILEWRITE rule
 *
 * The FILEWRITE builtin rule writes the given list to the given files
 * as a newline-terminated space-separated list of strings.
 */

void
builtin_write( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST *fpl = var_collapse( L0, lol_get( args, 0 ) );
	LIST *msg = var_collapse( L0, lol_get( args, 1 ) );
	LIST *strng, *fps;
	FILE *handle;

	for( fps = fpl; fps; fps = list_next( fps ) )
	{
	    handle = (FILE*)atol( fps->value );
	    for( strng = msg; strng; strng = list_next( strng ) )
	    {
	    	if( 0 > fprintf( handle, "%s ", strng->value ) )
	    	{
	    	    printf( "warning: Invalid file handle given to FILEWRITE\n" );
	    	    break;
	    	}
	    }
	    fprintf( handle, "\n" );
	}

	list_free( fpl );
	list_free( msg );
}


# if defined(macintosh) && !defined(CW)
/*
 * Hack -- mpw doesn't appear to have a strdup
 */
char *
strdup( orig )
char *orig;
{
	return strcpy(malloc(strlen(orig)+1),orig);
}
# endif
