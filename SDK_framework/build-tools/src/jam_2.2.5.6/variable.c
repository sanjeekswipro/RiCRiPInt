/*
 * Copyright 1993, 1995 Christopher Seiwald.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

# include "jam.h"
# include "lists.h"
# include "parse.h"
# include "variable.h"
# include "expand.h"
# include "hash.h"
# include "filesys.h"
# include "newstr.h"

/*
 * variable.c - handle jam multi-element variables
 *
 * External routines:
 *
 *	var_collapse() - return a list of strings given a list of values
 *	var_defines() - load a bunch of variable=value settings
 *	var_list() - variable expand an input list, generating a new list
 *	var_string() - expand a string with variables in it
 *	var_ref() - get VARIABLE * for a user defined symbol
 *	var_get() - get value of a user defined symbol
 *	var_set() - set a variable in jam's user defined symbol table
 *	var_swap() - swap a variable's value with the given one
 *	var_done() - free variable tables
 *
 * Internal routines:
 *
 *	var_enter() - make new var symbol table entry, returning var ptr
 *	var_dump() - dump a variable to stdout
 *
 * 04/13/94 (seiwald) - added shorthand L0 for null list pointer
 * 08/23/94 (seiwald) - Support for '+=' (append to variable)
 * 01/22/95 (seiwald) - split environment variables at blanks or :'s
 * 05/10/95 (seiwald) - split path variables at SPLITPATH (not :)
 */

static struct hash *varhash = 0;

static VARIABLE	*var_enter();
static void	var_dump();



/*
 * var_collapse() - collapse a list, replacing all the variable
 * references with the current value of the variable.  A new list is
 returned, which should be list_free()'ed after it is finished with.
 */

LIST *
var_collapse( to, from )
LIST *to;
LIST *from;
{
	char *s;
	for( ; from; from = list_next( from ) )
	{
	    switch( from->type )
	    {
	    case STRING:
		to = list_new( to, copystr( from->value ) );
		break;
	    case EXPRESSION:
   		s = from->value;
		to = var_expand( to, s, s + strlen(s), NULL, 1 );
		break;
	    case VARREF:
		to = var_collapse( to, ((VARIABLE *)from->value)->value );
		break;
	    }
	}
	return to;
}

/*
 * var_defines() - load a bunch of variable=value settings
 *
 * If variable name ends in PATH, split value at :'s.  
 * Otherwise, split at blanks.
 */

void
var_defines( e )
char **e;
{
	for( ; *e; e++ )
	{
	    char *val;

	    if( val = strchr( *e, '=' ) )
	    {
		LIST *l = L0;
		char *pp, *p;
		char split = ' ';
		char buf[ MAXSYM ];

		/* Split *PATH at :'s, not spaces */

		if( val - 4 >= *e )
		{
		    if( !strncmp( val - 4, "PATH", 4 ) ||
		        !strncmp( val - 4, "Path", 4 ) ||
		        !strncmp( val - 4, "path", 4 ) )
			    split = SPLITPATH;
		}

		/* Do the split */

		for( pp = val + 1; p = strchr( pp, split ); pp = p + 1 )
		{
		    strncpy( buf, pp, p - pp );
		    buf[ p - pp ] = '\0';
		    l = list_new( l, newstr( buf ) );
		}

		l = list_new( l, newstr( pp ) );

		/* Get name */

		strncpy( buf, *e, val - *e );
		buf[ val - *e ] = '\0';

		var_set( buf, l, VAR_SET );
	    }
	}
}

/*
 * var_list() - variable expand an input list, generating a new list
 *
 * Returns a newly created list.
 */

LIST *
var_list( ilist, lol )
LIST	*ilist;
LOL	*lol;
{
	LIST *olist = 0;

	for( ; ilist; ilist = list_next( ilist ) )
	{
	    char *s;

	    switch( ilist->type )
	    {
	    case STRING:
		s = ilist->value;
		olist = var_expand( olist, s, s + strlen(s), lol, 1 );
		break;
	    default:
		olist = list_dup( olist, ilist );
	    }
	}

	return olist;
}


/*
 * var_string() - expand a string with variables in it
 *
 * Copies in to out; doesn't modify targets & sources.
 */

int
var_string( in, out, outsize, lol )
char	*in;
char	*out;
int	outsize;
LOL	*lol;
{
	char 	*out0 = out;
	char	*oute = out + outsize - 1;

	while( *in )
	{
	    char	*lastword;
	    int		dollar = 0;

	    /* Copy white space */

	    while( isspace( *in ) )
	    {
		if( out >= oute )
		    return -1;

		*out++ = *in++;
	    }

	    lastword = out;

	    /* Copy non-white space, watching for variables */

	    while( *in && !isspace( *in ) )
	    {
	        if( out >= oute )
		    return -1;

		if( in[0] == '$' && in[1] == '(' )
		    dollar++;

		*out++ = *in++;
	    }

	    /* If a variable encountered, expand it and and embed the */
	    /* space-separated members of the list in the output. */

	    if( dollar )
	    {
		LIST	*l = var_expand( L0, lastword, out, lol, 0 );
		LIST	*collapsed = var_collapse( L0, l );
		list_free( l );

		out = lastword;

		for( l = collapsed; l; l = list_next( l ) )
		{
		    int so = strlen( l->value );

		    if( out + so >= oute )
			return -1;

		    strcpy( out, l->value );
		    out += so;
		    *out++ = ' ';
		}

		list_free( collapsed );
	    }
	}

	if( out >= oute )
	    return -1;

	*out++ = '\0';

	return out - out0;
}

/*
 * var_ref() - get VARIABLE * for a user defined symbol
 */

VARIABLE *
var_ref( symbol )
char	*symbol;
{
	return var_enter( symbol );
}

/*
 * var_get() - get value of a user defined symbol
 *
 * Returns NULL if symbol unset.
 */

LIST *
var_get( symbol )
char	*symbol;
{
	VARIABLE var, *v = &var;

	v->symbol = symbol;

	if( varhash && hashcheck( varhash, (HASHDATA **)&v ) )
	{
	    if( DEBUG_VARGET )
		var_dump( v->symbol, v->value, "get" );
	    return v->value;
	}
    
	return 0;
}

/*
 * var_set() - set a variable in jam's user defined symbol table
 *
 * 'flag' controls the relationship between new and old values of
 * the variable: SET replaces the old with the new; APPEND appends
 * the new to the old; DEFAULT only uses the new if the variable
 * was previously unset.
 *
 * Copies symbol.  Takes ownership of value.
 */

void
var_set( symbol, value, flag )
char	*symbol;
LIST	*value;
int	flag;
{
	VARIABLE *v = var_enter( symbol );

	if( DEBUG_VARSET )
	    var_dump( symbol, value, "set" );

	switch( flag )
	{
	case VAR_SET:
	    /* Replace value */
	    list_free( v->value );
	    v->value = value;
	    break;

	case VAR_APPEND:
	    /* Append value */
	    v->value = list_append( v->value, value );
	    break;

	case VAR_DEFAULT:
	    /* Set only if unset */
	    if( !v->value )
		v->value = value;
	    else
		list_free( value );
	    break;
	}
}

/*
 * var_swap() - swap a variable's value with the given one
 */

LIST *
var_swap( symbol, value )
char	*symbol;
LIST	*value;
{
	VARIABLE *v = var_enter( symbol );
	LIST 	 *oldvalue = v->value;

	if( DEBUG_VARSET )
	    var_dump( symbol, value, "set" );

	v->value = value;

	return oldvalue;
}



/*
 * var_enter() - make new var symbol table entry, returning var ptr
 */

static VARIABLE *
var_enter( symbol )
char	*symbol;
{
	VARIABLE var, *v = &var;

	if( !varhash )
	    varhash = hashinit( sizeof( VARIABLE ), "variables" );

	v->symbol = symbol;
	v->value = 0;

	if( hashenter( varhash, (HASHDATA **)&v ) )
	    v->symbol = newstr( symbol );	/* never freed */

	return v;
}

/*
 * var_dump() - dump a variable to stdout
 */

static void
var_dump( symbol, value, what )
char	*symbol;
LIST	*value;
char	*what;
{
	printf( "%s %s = ", what, symbol );
	list_print( value );
	printf( "\n" );
}

/*
 * var_done() - free variable tables
 */

void
var_done()
{
	hashdone( varhash );
}
