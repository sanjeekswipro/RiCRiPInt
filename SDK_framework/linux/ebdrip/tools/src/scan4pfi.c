/* $HopeName: SWtools!src:scan4pfi.c(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
Log stripped */

/*
	Scan4PFI - a tools to scan plugin source files fo calls to PFI


	The basic criteria is;
		text which starts with "PlgFw" and has only letters and
		numbers between there and the next (.
		White space before the ( is allowed.
		Line breaks or other noise are NOT!

	I/O is via std in and out;	scan4pif < plugin.c > plugin.pfi

*/
#include <stdio.h>
#ifndef MACINTOSH
#include <stdlib.h>
#endif

#define LINE_LEN 512


/* Data structures to hold the names found */

struct	names {
		struct names	   *name_link;
		char	name_value[1];
	};

struct	names	*name_list = NULL;


/*
	Allocate and fill a list item

*/
struct	names *make_entry( char *name )
{
	struct	names	*new;

	if ( (new=(struct names *)malloc( sizeof(struct names) + strlen( name ))) == NULL )
	{
		fprintf(stderr,"??Out of memory adding %s\n",name);
		exit(99);
	}

	strcpy( &(new->name_value), name );
	return new;
}


/*
	Add a name to the list

*/
void add_to_list( char *name )
{
	struct	names	*this,*new,*previous;

	if ( (this=name_list) == NULL )
	{
		name_list = make_entry( name );
		name_list->name_link = NULL;
		return;
	}

	for ( previous=NULL;; )
	{
		int	cmp;

		cmp = strcmp( name, &(this->name_value) );

		if ( cmp == 0 )
		{
			return;
		}
		if ( cmp < 0
		  || this->name_link == NULL )
		{
			new = make_entry( name );
			new->name_link = this;
			if ( previous == NULL )
			{
				name_list = new;
			}
			else
			{
				previous->name_link = new;
			}

			return;
		}

		previous = this;
		this = this->name_link;
	}
}


/*
	Display the final list.
*/
void dump_list()
{
	struct	names	*this;

	for ( this=name_list; this != NULL; this = this->name_link )
	{
		fprintf(stdout,"e%s,\n", this->name_value );
	}
}


void free_list()
{
	struct	names	*this,*next;

	for ( this=name_list; this != NULL; this = next )
	{
		next = this->name_link;
		free( this );	
	}
}


/*
	Search the input stream for suitable text

*/
int main()
{
	char	*cp,a_line[LINE_LEN];

	while( fgets( a_line, LINE_LEN, stdin ) != NULL )
	{
		for ( cp=a_line; *cp != '\0'; cp++ )
		{
			if ( strncmp( "PlgFw", cp, 5 ) == 0
			  || strncmp( "PlgPf", cp, 5 ) == 0	 )
			{
				char   *xp;
				for ( xp = cp+5; isalnum( *xp ); xp++ )
				{
					;
				}
				for ( ;isspace( *xp ); xp++ )
				{
					;
				}
				if ( *xp == '(' )
				{
					*xp = '\0';
					add_to_list( cp );

					cp = xp;
				}
			}
		}
	}
	dump_list();
	
	free_list();

	exit(0);	/* Some belt and braces functionality to */
	return 0;	/* ensure that we exit with a "no error" status */
}
