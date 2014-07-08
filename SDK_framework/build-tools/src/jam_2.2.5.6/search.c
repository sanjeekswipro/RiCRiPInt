/*
 * Copyright 1993, 1995 Christopher Seiwald.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

# include "jam.h"
# include "lists.h"
# include "search.h"
#ifdef FATFS
# include "timestam.h"
#else
# include "timestamp.h"
#endif
# include "filesys.h"
# include "variable.h"
# include "newstr.h"

/*
 * search.c - find a target along $(SEARCH) or $(LOCATE) 
 */

char *
search( target, time )
char	*target;
timestamp_t	*time;
{
	FILENAME f[1];
	LIST	*varlist;
	char	buf[ MAXJPATH ];

	/* Parse the filename */

	file_parse( target, f );

	f->f_grist.ptr = 0;
	f->f_grist.len = 0;

	if( varlist = var_collapse( L0, var_get( "LOCATE" ) ) )
	{
	    f->f_root.ptr = varlist->value;
	    f->f_root.len = strlen( varlist->value );
	    file_build( f, buf, 1 );
	    list_free( varlist );

	    if( DEBUG_SEARCH )
		printf( "locate %s: %s\n", target, buf );

	    timestamp( buf, time );

	    return newstr( buf );
	}
	else if( varlist = var_collapse( L0, var_get( "SEARCH" ) ) )
	{
	    LIST *varl;
	    for( varl = varlist; varl; varl = list_next( varl ) )
	    {
		f->f_root.ptr = varl->value;
		f->f_root.len = strlen( varl->value );

		file_build( f, buf, 1 );

		if( DEBUG_SEARCH )
		    printf( "search %s: %s\n", target, buf );

		timestamp( buf, time );

		if( TIME_NONZERO( *time ) )
		{
		    list_free( varlist );
		    return newstr( buf );
		}
	    }
	    list_free( varlist );
	}

	/* Look for the obvious */
	/* This is a questionable move.  Should we look in the */
	/* obvious place if SEARCH is set? */

	f->f_root.ptr = 0;
	f->f_root.len = 0;

	file_build( f, buf, 1 );

	if( DEBUG_SEARCH )
	    printf( "search %s: %s\n", target, buf );

	timestamp( buf, time );

	return newstr( buf );
}
