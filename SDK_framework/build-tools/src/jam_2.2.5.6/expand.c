/*
 * Copyright 1993, 1995 Christopher Seiwald.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

# include "jam.h"
# include "lists.h"
# include "variable.h"
# include "expand.h"
# include "filesys.h"
# include "newstr.h"

# include "harlequin.h"

/*
 * expand.c - expand a buffer, given variable values
 *
 * External routines:
 *
 *     var_expand() - variable-expand input string into list of strings
 *
 * Internal routines:
 *
 *     var_edit() - copy input target name to output, performing : modifiers
 *     var_mods() - parse : modifiers into FILENAME structure
 *
 * 01/25/94 (seiwald) - $(X)$(UNDEF) was expanding like plain $(X)
 * 04/13/94 (seiwald) - added shorthand L0 for null list pointer
 */

static int	var_edit();
static void	var_mods();

# define MAGIC_COLON	'\001'
# define MAGIC_LEFT	'\002'
# define MAGIC_RIGHT	'\003'

/*
 * var_expand() - variable-expand input string into list of strings
 *
 * Would just copy input to output, performing variable expansion, 
 * except that since variables can contain multiple values the result
 * of variable expansion may contain multiple values (a list).  Properly
 * performs "product" operations that occur in "$(var1)xxx$(var2)" or
 * even "$($(var2))".
 *
 * Returns a newly created list.
 */

LIST *
var_expand( l, in, end, lol, cancopyin )
LIST	*l;
char	*in;
char	*end;
LOL	*lol;
int	cancopyin;
{
	char out_buf[ MAXSYM ];
	char *out = out_buf;
	char *inp = in;

	LIST *left = L0, *right = L0;
	char infixoperator = '\0';

	if( DEBUG_VAREXP )
	    printf( "expand '%.*s'\n", end - in, in );

    /*
     * Parse left-hand side of expression
     */

	if( in[0] == '$' && in[1] == '=' )
	{
	    if( cancopyin )
		return list_tack( l, EXPRESSION, newstr( in+2 ) );
	    memcpy( out_buf, in, end - in );
	    out_buf[end - in] = '\0';
	    return list_tack( l, EXPRESSION, newstr( out_buf ) );
	}

	/* Starts with literal string */

	else if( in[0] != '$' || in[1] != '(' )
	{
	    /*
	     * Watch out!  We could still have a $ which wasn't part of
	     * a special sequence at the beginning of the string.  This
	     * would cause the variable-detecting copy below to call
	     * var_expand() repeatedly until the buffer overflowed
	     * unless we jump it.
	     */
	    if( in[0] == '$' )
		*out++ = *in++;

	    for( ; in < end; in++, out++ )
	    {
		if( ( *out = *in ) == '$' )
		    break;
	    }

	/* Cancopyin is an optimization: if the input was already a list */
	/* item, we can use the copystr() to put it on the new list. */
	/* Otherwise, we use the slower newstr(). */

	    *out = '\0';

	/* (Provided this is the entire thing we intend to return) */

	    if( cancopyin && in == end )
		left = list_new( L0, copystr( inp ) );
	    else
		left = list_new( L0, newstr( out_buf ) );
	}


	/* Starts with variable expansion */

	else
	{
	    int depth = 1;
	    LIST *variables, *vars;
	    char varname[ MAXSYM ];

	    /*
	     * We now find the matching close paren, copying the variable and
	     * modifiers between the $( and ) temporarily into out_buf, so that
	     * we can replace :'s with MAGIC_COLON.  This is necessary to avoid
	     * being confused by modifier values that are variables containing
	     * :'s.  Ugly.
	     */

	    for( in += 2; in < end && depth; in++, out++ )
	    {
		switch( *out = *in )
		{
		case '(': depth++; break;
		case ')': depth--; break;
		case ':': *out = MAGIC_COLON; break;
		case '[': *out = MAGIC_LEFT; break;
		case ']': *out = MAGIC_RIGHT; break;
		}
	    }

	    /* Copied ) - back up. */

	    out--;

	    vars = var_expand( L0, out_buf, out, lol, 0 );
	    variables = var_collapse( L0, vars );
	    list_free( vars );

	    /* Expand the variable names found */

	    for( vars = variables; vars; vars = list_next( vars ) )
	    {
		LIST *value;
		LIST *valuemodified;
		LIST *collapsed = L0;
		char *colon = NULL;
		char *bracket = NULL;
		char *ptr1, *ptr2;
		int i, sub1 = 0, sub2 = 0;

		int QSORT		= 0;
		int CONSTRUCTPATH	= 0;
		int LAZYCOPY		= 0;
		int LAZYPTR		= 0;
		int ISFUNCTIONPARAM     = 0;

		/* Look for a : modifier in the variable name */
		/* Must copy into varname so we can modify it */

		for( ptr1 = varname, ptr2 = vars->value; *ptr2; ptr1++, ptr2++ )
		{
		    *ptr1 = *ptr2;
		    if( *ptr2 == MAGIC_COLON && ! colon )
			{ colon = ptr1 + 1; *ptr1 = '\0'; }
		    else if( *ptr2 == MAGIC_LEFT )
			*( bracket = ptr1 ) = '\0' ;
		}
		*ptr1 = '\0';

		if( colon && ! colon[1] )
		    switch( colon[0] )
		    {
		    case 'Q': QSORT		= 1; break;
		    case '/': CONSTRUCTPATH	= 1; break;
		    case '&': LAZYPTR	= 1; break;
		    case '*': LAZYCOPY	= 1; break;
		    }

		if( bracket )
		{
		    char *dash;

		    if( LAZYCOPY || LAZYPTR )
		    {
			printf( "warning: cannot use lazy syntax with other operators in %s -- ignoring\n", vars->value );
			LAZYCOPY = LAZYPTR = 0;
		    }

		    if( dash = strchr( ++bracket, '-' ) )
		    {
			*dash = '\0';
			sub1 = atoi( bracket );
			sub2 = atoi( dash + 1 );
		    }
		    else
		    {
			sub1 = sub2 = atoi( bracket );
		    }
		}

		/* Get variable value, specially handling $(<), $(>), $(n) */
		
		if( varname[0] == '<' && !varname[1] )
		{
		    ISFUNCTIONPARAM = 1;
		    value = lol_get( lol, 0 );
		}
		else if( varname[0] == '>' && !varname[1] )
		{
		    ISFUNCTIONPARAM = 1;
		    value = lol_get( lol, 1 );
		}
		else if( varname[0] >= '1' && varname[0] <= '9' && !varname[1] )
		{
		    ISFUNCTIONPARAM = 1;
		    value = lol_get( lol, varname[0] - '1' );
		}

		/* Now decide on which of three ways to use that value */

		if( LAZYPTR )
		{
		    if( ISFUNCTIONPARAM )
		    {
			printf( "warning: cannot take a reference to rule parameter $(%s) -- assuming $(%s:*)\n",
				varname, varname );
			LAZYCOPY = 1; LAZYPTR = 0;
		    }
		    else
		    {
			left = list_tack( left, VARREF, var_ref( varname ) );
			continue;
		    }
		}
		if( ! ISFUNCTIONPARAM ) value = var_get( varname );

		if( LAZYCOPY )
		{
		    left = list_copy( left, value );
		    continue;
		}

		value = collapsed = var_collapse( L0, value );

		/* The fast path: $(x) - just copy the variable value. */

		if( !bracket && !colon )
		{
		    if( collapsed )
			left = list_append( left, value );
		    else
			left = list_copy( left, value );
		    continue;
		}

		/* For each variable value */

		valuemodified = L0;
		for( i = 1; value; i++, value = list_next( value ) )
		{
		    /* Skip members not in subscript */

		    if( bracket && ( i < sub1 || sub2 && i > sub2 ) )
			continue;

		    /* Apply : mods, if present */

		    if( colon )
		    {
			if( ! var_edit( value->value, colon, out_buf ) )
			    continue;
			valuemodified = list_new( valuemodified, newstr( out_buf ) );
		    }
		    else
			valuemodified = list_new( valuemodified, copystr( value->value ) );
		}
		list_free( collapsed );

		/*
		 * Process those modifiers that affect the whole list,
		 * rather than the individual elements
		 */

		if( QSORT )
		{
		    LIST *element;
		    int number;
		    char **pointers, **ptr;

		    /*
		     * First put the pointers to the string values
		     * into an array for qsort()
		     */

		    for( number = 0, element = valuemodified; element; element = list_next( element ), number++ );
		    pointers = malloc( sizeof( char* ) * number );
		    for( ptr = pointers, element = valuemodified; element; element = list_next( element ), ptr++ )
			    *ptr = newstr( element->value );
		    list_free( valuemodified );

		    /*
		     * Qsort() the array of pointers
		     */

		    qsort( ( char* )pointers, number, sizeof( char* ), qsort_compar );

		    /*
		     * Reconstruct the new "valuemodified"
		     */

		    for( valuemodified = L0, ptr = pointers; number; ptr++, number-- )
			    valuemodified = list_new( valuemodified, *ptr );

		    free( pointers );
		}
		if( CONSTRUCTPATH )
		{
		    LIST *element;
		    *out_buf = '\0';
		    for( element = valuemodified; element; element = list_next( element ) )
		    {
			construct_path( out_buf, out_buf + strlen( out_buf ),
				element->value );
		    }
		    list_free( valuemodified );
		    valuemodified = list_new( L0, newstr( out_buf ) );
		}

		left = list_append( left, valuemodified );
	    }
	    list_free( variables );
	}


    /*
     * State of play:
     *
     *   aaaaaaaaaabbbbbb
     *   ^inp      ^in   ^end
     *
     *   (inp to in has been parsed into "left")
     *
     * We now have a list (left), and in points to the next
     * character in the input.  This may be an infix operator (such
     * as $/) or a literal string (if we first had a variable) or
     * another variable (in either case).  Find out if there is an
     * operator, and then expand the other side into right.  Finally
     * expand the product of left and right.
     */

	/*
	 * Short-cut: in==end means that we don't have to take a product,
	 * and can return "left"
	 */

	if( in == end )
	    return list_append( l, left );

	/*
	 * Look for an infix operator and jump over it
	 */

	if( in[0] == '$' && in[1] && strchr( "/", in[1] ) )
	{
	    infixoperator = in[1];
	    in += 2;
	}

	/*
	 * Expand right-hand-side
	 */

	right = var_expand( L0, in, end, lol, 0 );

	/*
	 * Expand the product, taking into account the infix operator
	 * (if any)
	 */
	{
	    LIST *nextleft, *nextright;

	    for( nextleft = left; nextleft; nextleft = list_next( nextleft ) )
	    {
	    	if( nextleft->type != STRING )
	    	{
		    LIST *collapsed = var_collapse( L0, nextleft );
		    list_free( left );
		    nextleft = left = collapsed;
		    puts( "warning: collapsed use of lazy values in product notation" );
	    	}
		out = out_buf + strlen( nextleft->value );

		switch( infixoperator )
		{
		case '/': /* Path construction */
		    break;
		default: /* None */
		    strcpy( out_buf, nextleft->value );
		    break;
		}

		for( nextright = right; nextright; nextright = list_next( nextright ) )
		{
		    if( nextright->type != STRING )
		    {
			LIST *collapsed = L0;
			LIST *p;
			for( p = right; p != nextright; p = list_next( p ) )
			    collapsed = list_dup( collapsed, p );
			nextright = var_collapse( L0, nextright );
			list_free( right );
			right = list_append( collapsed, nextright );
			puts( "warning: collapsed use of lazy values in product notation" );
		    }

		    switch( infixoperator )
		    {
		    case '/': /* Path construction */
			/* Reconstruct path each time; it gets broken */
			strcpy( out_buf, nextleft->value );
			construct_path( out_buf, out, nextright->value );
			break;
		    default: /* None */
			strcpy( out, nextright->value );
			break;
		    }
		    l = list_new( l, newstr( out_buf ) );
		}
	    }
	}

	list_free( left );
	list_free( right );

	return l;
}

/*
 * var_edit() - copy input target name to output, performing : modifiers
 */

typedef struct {
	char	downshift;	/* :L -- downshift result */
	char	upshift;	/* :U -- upshift result */
	char	parent;		/* :P -- go to parent directory */
	char	exists;		/* :E -- return 0 if file doesn't exist */
	char	escape;		/* :X -- escape special characters */
} VAR_ACTS ;
	
static int
var_edit( in, mods, out )
char	*in;
char	*mods;
char	*out;
{
	FILENAME old, new;
	VAR_ACTS acts;

	/* Parse apart original filename, putting parts into "old" */

	file_parse( in, &old );

	/* Parse apart modifiers, putting them into "new" */

	var_mods( mods, &new, &acts );

	/* Replace any old with new */

	if( new.f_grist.ptr )
	    old.f_grist = new.f_grist;

	if( new.f_root.ptr )
	    old.f_root = new.f_root;

	if( new.f_dir.ptr )
	    old.f_dir = new.f_dir;

	if( new.f_base.ptr )
	    old.f_base = new.f_base;

	if( new.f_suffix.ptr )
	    old.f_suffix = new.f_suffix;

	if( new.f_member.ptr )
	    old.f_member = new.f_member;

	/* If requested, modify old to point to parent */

	if( acts.parent )
	    file_parent( &old );

	/* Put filename back together */

	file_build( &old, out, 0 );

	/* Check to see if file exists, returning 0 if not */

	if( acts.exists )
	{
	    timestamp_t dummy;
	    if( file_time( out, &dummy ) )
		return 0;
	}

	/* Handle upshifting, downshifting now */

	if( acts.upshift )
	{
	    for( ; *out; ++out )
		*out = toupper( *out );
	}
	else if( acts.downshift )
	{
	    for( ; *out; ++out )
		*out = tolower( *out );
	}

	/* Escape special characters if necessary */
	if( acts.escape )
	{
	    char *copy = strdup( out );
	    char *origout = copy;
	    char *special;
	    while( special = strchr( origout, '\\' ) )
	    {
	    	int length = special - origout;
	    	memcpy( out, origout, length );
	    	out += length;
	    	origout += length ;
	    	*out++ = '\\';
	    	*out++ = *origout++;
	    }
	    strcpy( out, origout );
	    free( copy );
	}

	return 1;
}


/*
 * var_mods() - parse : modifiers into FILENAME structure
 *
 * The : modifiers in a $(varname:modifier) currently support replacing
 * or omitting elements of a filename, and so they are parsed into a 
 * FILENAME structure (which contains pointers into the original string).
 *
 * Modifiers of the form "X=value" replace the component X with
 * the given value.  Modifiers without the "=value" cause everything 
 * but the component X to be omitted.  X is one of:
 *
 *	G <grist>
 *	D directory name
 *	B base name
 *	S .suffix
 *	M (member)
 *	R root directory - prepended to whole path
 *
 * This routine sets:
 *
 *	f->f_xxx.ptr = 0
 *	f->f_xxx.len = 0
 *		-> leave the original component xxx
 *
 *	f->f_xxx.ptr = string
 *	f->f_xxx.len = strlen( string )
 *		-> replace component xxx with string
 *
 *	f->f_xxx.ptr = ""
 *	f->f_xxx.len = 0
 *		-> omit component xxx
 *
 * var_edit() above and file_build() obligingly follow this convention.
 */

static void
var_mods( mods, f, acts )
char		*mods;
FILENAME	*f;
VAR_ACTS	*acts;
{
	char *flags = "GRDBSM";
	int havezeroed = 0;
	memset( (char *)f, 0, sizeof( *f ) );
	memset( (char *)acts, 0, sizeof( *acts ) );

	while( *mods )
	{
	    char *fl;
	    struct filepart *fp;

	    /* First take care of :U or :L (upshift, downshift) */

	    if( *mods == 'L' )
	    {
		acts->downshift = 1;
		++mods;
		continue;
	    }
	    else if( *mods == 'U' )
	    {
		acts->upshift = 1;
		++mods;
		continue;
	    }
	    else if( *mods == 'P' )
	    {
		acts->parent = 1;
		++mods;
		continue;
	    }
	    else if( *mods == 'E' )
	    {
		acts->exists = 1;
		++mods;
		continue;
	    }
	    else if( *mods == 'X' )
	    {
		acts->escape = 1;
		++mods;
		continue;
	    }

	    /* Now handle the file component flags */

	    if( !( fl = strchr( flags, *mods++ ) ) )
		break;	/* should complain, but so what... */

	    fp = &f->part[ fl - flags ];

	    if( *mods++ != '=' )
	    {
		/* :X - turn everything but X off */

		int i;

		mods--;

		if( !havezeroed++ )
		    for( i = 0; i < 6; i++ )
		{
		    f->part[ i ].len = 0;
		    f->part[ i ].ptr = "";
		}

		fp->ptr = 0;
	    }
	    else
	    {
		/* :X=value - set X to value */

		char *p;

		if( p = strchr( mods, MAGIC_COLON ) )
		{
		    fp->ptr = mods;
		    fp->len = p - mods;
		    mods = p + 1;
		}
		else
		{
		    fp->ptr = mods;
		    fp->len = strlen( mods );
		    mods += fp->len;
		}
	    }
	}
}
