/*
 * Copyright 1993, 1995 Christopher Seiwald.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

# include "jam.h"

# include "lists.h"
# include "parse.h"
# include "compile.h"
# include "variable.h"
# include "rules.h"
# include "newstr.h"
# include "make.h"
# include "search.h"

# include "harlequin.h"

/*
 * compile.c - compile parsed jam statements
 *
 * External routines:
 *
 *	compile_foreach() - compile the "for x in y" statement
 *	compile_if() - compile 'if' rule
 *	compile_include() - support for 'include' - call include() on file
 *	compile_local() - declare (and set) local variables
 *	compile_null() - do nothing -- a stub for parsing
 *	compile_rule() - compile a single user defined rule
 *	compile_rules() - compile a chain of rules
 *	compile_set() - compile the "set variable" statement
 *	compile_setcomp() - support for `rule` - save parse tree 
 *	compile_setexec() - support for `actions` - save execution string 
 *	compile_settings() - compile the "on =" (set variable on exec) statement
 *	compile_switch() - compile 'switch' rule
 *
 * Internal routines:
 *
 *	debug_compile() - printf with indent to show rule expansion.
 *
 *	evaluate_if() - evaluate if to determine which leg to compile
 *
 *	builtin_depends() - DEPENDS/INCLUDES rule
 *	builtin_echo() - ECHO rule
 *	builtin_exit() - EXIT rule
 *	builtin_flags() - NOCARE, NOTFILE, TEMPORARY rule
 *
 * 02/03/94 (seiwald) -	Changed trace output to read "setting" instead of 
 *			the awkward sounding "settings".
 * 04/12/94 (seiwald) - Combined build_depends() with build_includes().
 * 04/12/94 (seiwald) - actionlist() now just appends a single action.
 * 04/13/94 (seiwald) - added shorthand L0 for null list pointer
 * 05/13/94 (seiwald) - include files are now bound as targets, and thus
 *			can make use of $(SEARCH)
 * 08/23/94 (seiwald) - Support for '+=' (append to variable)
 * 12/20/94 (seiwald) - NOTIME renamed NOTFILE.
 * 01/22/95 (seiwald) - Exit rule.
 * 02/02/95 (seiwald) - Always rule; LEAVES rule.
 * 02/14/95 (seiwald) - NoUpdate rule.
 */

static void debug_compile();

static int evaluate_if();

static void builtin_depends();
static void builtin_echo();
static void builtin_exit();
static void builtin_flags();

int glob();



/*
 * compile_builtin() - define builtin rules
 */

# define P0 (PARSE *)0
# define C0 (char *)0

void
compile_builtins()
{
    bindrule( "Always" )->procedure = 
    bindrule( "ALWAYS" )->procedure = 
	parse_make( builtin_flags, P0, P0, C0, C0, L0, L0, T_FLAG_TOUCHED );

    bindrule( "FileClose" )->procedure = 
    bindrule( "FILECLOSE" )->procedure = 
	parse_make( builtin_close, P0, P0, C0, C0, L0, L0, 0 );

    bindrule( "Depends" )->procedure = 
    bindrule( "DEPENDS" )->procedure = 
	parse_make( builtin_depends, P0, P0, C0, C0, L0, L0, T_DEPS_DEPENDS );

    bindrule( "NIDepends" )->procedure = 
    bindrule( "NIDEPENDS" )->procedure = 
	parse_make( builtin_depends, P0, P0, C0, C0, L0, L0, T_DEPS_NIDEPENDS );

    bindrule( "Echo" )->procedure = 
    bindrule( "ECHO" )->procedure = 
	parse_make( builtin_echo, P0, P0, C0, C0, L0, L0, 0 );

    bindrule( "Exit" )->procedure = 
    bindrule( "EXIT" )->procedure = 
	parse_make( builtin_exit, P0, P0, C0, C0, L0, L0, 0 );

    bindrule( "Includes" )->procedure = 
    bindrule( "INCLUDES" )->procedure = 
	parse_make( builtin_depends, P0, P0, C0, C0, L0, L0, T_DEPS_INCLUDES );

    bindrule( "Leaves" )->procedure = 
    bindrule( "LEAVES" )->procedure = 
	parse_make( builtin_flags, P0, P0, C0, C0, L0, L0, T_FLAG_LEAVES );

    bindrule( "NoCare" )->procedure = 
    bindrule( "NOCARE" )->procedure = 
	parse_make( builtin_flags, P0, P0, C0, C0, L0, L0, T_FLAG_NOCARE );

    bindrule( "NOTIME" )->procedure = 
    bindrule( "NotFile" )->procedure = 
    bindrule( "NOTFILE" )->procedure = 
	parse_make( builtin_flags, P0, P0, C0, C0, L0, L0, T_FLAG_NOTFILE );

    bindrule( "NoUpdate" )->procedure = 
    bindrule( "NOUPDATE" )->procedure = 
	parse_make( builtin_flags, P0, P0, C0, C0, L0, L0, T_FLAG_NOUPDATE );

    bindrule( "FileOpen" )->procedure = 
    bindrule( "FILEOPEN" )->procedure = 
	parse_make( builtin_open, P0, P0, C0, C0, L0, L0, 0 );

    bindrule( "Temporary" )->procedure = 
    bindrule( "TEMPORARY" )->procedure = 
	parse_make( builtin_flags, P0, P0, C0, C0, L0, L0, T_FLAG_TEMP );

    bindrule( "FileWrite" )->procedure = 
    bindrule( "FILEWRITE" )->procedure = 
	parse_make( builtin_write, P0, P0, C0, C0, L0, L0, 0 );
}

/*
 * compile_foreach() - compile the "for x in y" statement
 *
 * Compile_foreach() resets the given variable name to each specified
 * value, executing the commands enclosed in braces for each iteration.
 *
 *	parse->string	index variable
 *	parse->left	rule to compile
 *	parse->llist	variable values
 */

void
compile_foreach( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST	*nv = var_list( parse->llist, args );
	LIST	*l;

	/* Call var_set to reset $(parse->string) for each val. */

	for( l = nv; l; l = list_next( l ) )
	{
	    LIST *val = list_dup( L0, l );

	    var_set( parse->string, val, VAR_SET );

	    (*parse->left->func)( parse->left, args );
	}

	list_free( nv );
}

/*
 * compile_if() - compile 'if' rule
 *
 *	parse->left		condition tree
 *	parse->right->left	then tree
 *	parse->right->right	else tree
 */

void
compile_if( parse, args )
PARSE		*parse;
LOL		*args;
{
	if( evaluate_if( parse->left, args ) )
	{
	    (*parse->right->left->func)( parse->right->left, args );
	}
	else
	{
	    (*parse->right->right->func)( parse->right->right, args );
	}
}

/*
 * evaluate_if() - evaluate if to determine which leg to compile
 *
 * Returns:
 *	!0	if expression true - compile 'then' clause
 *	0	if expression false - compile 'else' clause
 */

static int
evaluate_if( parse, args )
PARSE		*parse;
LOL		*args;
{
	int	status;

	if( parse->num <= COND_OR )
	{
	    /* Handle one of the logical operators */

	    switch( parse->num )
	    {
	    case COND_NOT:
		status = !evaluate_if( parse->left, args );
		break;

	    case COND_AND:
		status = evaluate_if( parse->left, args ) &&
			 evaluate_if( parse->right, args );
		break;

	    case COND_OR:
		status = evaluate_if( parse->left, args ) ||
			 evaluate_if( parse->right, args );
		break;

	    default:
		status = 0;	/* can't happen */
	    }
	}
	else
	{
	    /* Handle one of the comparison operators */
	    /* Expand targets and sources */

	    LIST *nt, *nt1, *ns, *ns1;
	    nt1 = var_list( parse->llist, args );
	    ns1 = var_list( parse->rlist, args );
	    nt = var_collapse( L0, nt1 ); ns = var_collapse( L0, ns1 );
	    list_free( nt1 );         list_free( ns1 );

	    /* "a in b" make sure each of a is equal to something in b. */
	    /* Otherwise, step through pairwise comparison. */

	    if( parse->num == COND_IN )
	    {
		LIST *s, *t;

		/* Try each t until failure. */

		for( status = 1, t = nt; status && t; t = list_next( t ) )
		{
		    int stat1;

		    /* Try each s until success */

		    for( stat1 = 0, s = ns; !stat1 && s; s = list_next( s ) )
			stat1 = !strcmp( t->value, s->value );

		    status = stat1;
		}
	    }
	    else
	    {
		LIST *s = ns, *t = nt;

		status = 0;

		while( !status && ( t || s ) )
		{
		    char *st = t ? t->value : "";
		    char *ss = s ? s->value : "";

		    status = strcmp( st, ss );

		    t = t ? list_next( t ) : t;
		    s = s ? list_next( s ) : s;
		}
	    }

	    switch( parse->num )
	    {
	    case COND_EXISTS:	status = status > 0 ; break;
	    case COND_EQUALS:	status = !status; break;
	    case COND_NOTEQ:	status = status != 0; break;
	    case COND_LESS:	status = status < 0; break;
	    case COND_LESSEQ:	status = status <= 0; break;
	    case COND_MORE:	status = status > 0; break;
	    case COND_MOREEQ:	status = status >= 0; break;
	    case COND_IN:	/* status = status */ break;
	    }

	    if( DEBUG_IF )
	    {
		debug_compile( 0, "if" );
		list_print( nt );
		printf( "(%d)", status );
		list_print( ns );
		printf( "\n" );
	    }

	    list_free( nt );
	    list_free( ns );

	}

	return status;
}

/*
 * compile_include() - support for 'include' - call include() on file
 *
 * 	parse->llist	list of files to include (can only do 1)
 */

void
compile_include( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST	*nt1 = var_list( parse->llist, args );
	LIST	*nt = var_collapse( L0, nt1 );
	list_free( nt1 );

	yield( );

	if( DEBUG_COMPILE )
	{
	    debug_compile( 0, "include" );
	    list_print( nt );
	    printf( "\n" );
	}

	if( nt )
	{
	    TARGET *t = bindtarget( nt->value );

	    /* Bind the include file under the influence of */
	    /* "on-target" variables.  Though they are targets, */
	    /* include files are not built with make(). */

	    pushsettings( t->settings );
	    t->boundname = search( t->name, &t->time );
	    popsettings( t->settings );

	    parse_file( t->boundname );
	}

	list_free( nt );
}

/*
 * compile_local() - declare (and set) local variables
 *
 *	parse->llist	list of variables
 *	parse->rlist	list of values
 *	parse->left	rules to execute
 */

void
compile_local( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST *l;
	SETTINGS *s = 0;
	LIST	*ns = var_list( parse->rlist, args );
	LIST	*nt1 = var_list( parse->llist, args );
	LIST	*nt = var_collapse( L0, nt1 );
	list_free( nt1 );

	if( DEBUG_COMPILE )
	{
	    debug_compile( 0, "local" );
	    list_print( nt );
	    printf( " = " );
	    list_print( ns );
	    printf( "\n" );
	}

	/* Initial value is ns */

	for( l = nt; l; l = list_next( l ) )
	    s = addsettings( s, 0, l->value, list_copy( (LIST*)0, ns ) );

	list_free( ns );
	list_free( nt );

	/* Note that callees of the current context get this "local" */
	/* variable, making it not so much local as layered. */

	pushsettings( s );
	(*parse->left->func)( parse->left, args );
	popsettings( s );

	freesettings( s );
}

/*
 * compile_null() - do nothing -- a stub for parsing
 */

void
compile_null( parse, args )
PARSE		*parse;
LOL		*args;
{
}

/*
 * compile_rule() - compile a single user defined rule
 *
 *	parse->string	name of user defined rule
 *	parse->left	parameters (list of lists) to rule, recursing left
 */

void
compile_rule( parse, args )
PARSE		*parse;
LOL		*args;
{
	RULE	*rule = bindrule( parse->string );
	LOL	nargs[1];
	PARSE	*p;

	/* Build up the list of arg lists */

	lol_init( nargs );

	for( p = parse->left; p; p = p->left )
	    lol_add( nargs, var_list( p->llist, args ) );

	if( DEBUG_COMPILE )
	{
	    debug_compile( 1, parse->string );
	    lol_print( nargs );
	    printf( "\n" );
	}

	/* Check traditional targets $(<) and sources $(>) */

	if( !lol_get( nargs, 0 ) )
	    printf( "warning: no targets on rule %s\n",
		    rule->name);

	if( !rule->actions && !rule->procedure )
	    printf( "warning: unknown rule %s\n", rule->name );

	/* If this rule will be executed for updating the targets */
	/* then construct the action for make(). */

	if( rule->actions )
	{
	    TARGETS	*t;
	    ACTION	*action;

	    /* The action is associated with this instance of this rule */

	    action = (ACTION *)malloc( sizeof( ACTION ) );
	    memset( (char *)action, '\0', sizeof( *action ) );

	    action->rule = rule;
	    action->targets = targetlist( (TARGETS *)0, lol_get( nargs, 0 ) );
	    action->sources = targetlist( (TARGETS *)0, lol_get( nargs, 1 ) );

	    /* Append this action to the actions of each target */

	    for( t = action->targets; t; t = t->next )
		t->target->actions = actionlist( t->target->actions, action );
	}

	/* Now recursively compile any parse tree associated with this rule */

	if( rule->procedure )
	    (*rule->procedure->func)( rule->procedure, nargs );

	lol_free( nargs );

	if( DEBUG_COMPILE )
	    debug_compile( -1, 0 );
}

/*
 * compile_rules() - compile a chain of rules
 *
 *	parse->left	more compile_rules() by left-recursion
 *	parse->right	single rule
 */

void
compile_rules( parse, args )
PARSE		*parse;
LOL		*args;
{
	(*parse->left->func)( parse->left, args );
	(*parse->right->func)( parse->right, args );
}

/*
 * compile_set() - compile the "set variable" statement
 *
 *	parse->llist	variable names
 *	parse->rlist	variable values
 *	parse->num	ASSIGN_SET/APPEND/DEFAULT
 */

void
compile_set( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST	*nt1 = var_list( parse->llist, args );
	LIST	*ns = var_list( parse->rlist, args );
	LIST	*l;
	int	setflag;
	char	*trace;
	LIST	*nt = var_collapse( L0, nt1 );
	list_free( nt1 );

	switch( parse->num )
	{
	case ASSIGN_SET:	setflag = VAR_SET; trace = "="; break;
	case ASSIGN_APPEND:	setflag = VAR_APPEND; trace = "+="; break;
	case ASSIGN_DEFAULT:	setflag = VAR_DEFAULT; trace = "?="; break;
	default:		setflag = VAR_SET; trace = ""; break;
	}

	if( DEBUG_COMPILE )
	{
	    debug_compile( 0, "set" );
	    list_print( nt );
	    printf( " %s ", trace );
	    list_print( ns );
	    printf( "\n" );
	}

	/* Call var_set to set variable */
	/* var_set keeps ns, so need to copy it */
	/* avoid copy if just setting one variable. */

	for( l = nt; l; l = list_next( l ) )
	    var_set( l->value, 
		list_next( l ) ? list_copy( (LIST*)0, ns ) : ns,
		setflag );

	if( !nt )
	    list_free( ns );

	list_free( nt );
}

/*
 * compile_setcomp() - support for `rule` - save parse tree 
 *
 *	parse->string	rule name
 *	parse->left	rules for rule
 */

void
compile_setcomp( parse, args )
PARSE		*parse;
LOL		*args;
{
	RULE	*rule = bindrule( parse->string );

	/* Free old one, if present */

	if( rule->procedure )
	    parse_free( rule->procedure );

	rule->procedure = parse->left;

	/* we now own this parse tree */
	/* don't let parse_free() release it */

	parse->left = 0;	
}

/*
 * compile_setexec() - support for `actions` - save execution string 
 *
 *	parse->string	rule name
 *	parse->string1	OS command string
 *	parse->num	flags
 *	parse->llist	`bind` variables
 *
 * Note that the parse flags (as defined in compile.h) are transfered
 * directly to the rule flags (as defined in rules.h).
 */

void
compile_setexec( parse, args )
PARSE		*parse;
LOL		*args;
{
	RULE	*rule = bindrule( parse->string );
	
	/* Free old one, if present */

	if( rule->actions )
	{
	    freestr( rule->actions );
	    list_free( rule->bindlist );
	}

	rule->actions = copystr( parse->string1 );
	rule->bindlist = list_copy( L0, parse->llist );
	rule->flags |= parse->num; /* XXX translate this properly */
}

/*
 * compile_settings() - compile the "on =" (set variable on exec) statement
 *
 *	parse->llist		target names
 *	parse->left->llist	variable names
 *	parse->left->rlist	variable values
 *	parse->num		ASSIGN_SET/APPEND	
 */

void
compile_settings( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST	*nt1 = var_list( parse->left->llist, args );
	LIST	*ns = var_list( parse->left->rlist, args );
	LIST	*targets, *ts;
	int	append = parse->num == ASSIGN_APPEND;
	LIST	*nt = var_collapse( L0, nt1 );
	list_free( nt1 );

	/* Reset targets */

	nt1 = var_list( parse->llist, args );
	targets = var_collapse( L0, nt1 );
	list_free( nt1 );

	if( DEBUG_COMPILE )
	{
	    debug_compile( 0, "set" );
	    list_print( nt );
	    printf( "on " );
	    list_print( targets );
	    printf( " %s ", append ? "+=" : "=" );
	    list_print( ns );
	    printf( "\n" );
	}

	/* Call addsettings to save variable setting */
	/* addsettings keeps ns, so need to copy it */
	/* Pass append flag to addsettings() */

	for( ts = targets; ts; ts = list_next( ts ) )
	{
	    TARGET 	*t = bindtarget( ts->value );
	    LIST	*l;

	    for( l = nt; l; l = list_next( l ) )
		t->settings = addsettings( t->settings, append, 
				l->value, list_copy( (LIST*)0, ns ) );
	}

	list_free( ns );
	list_free( nt );
	list_free( targets );
}

/*
 * compile_switch() - compile 'switch' rule
 *
 *	parse->llist	switch value (only 1st used)
 *	parse->left	cases
 *
 *	cases->left	1st case
 *	cases->right	next cases
 *
 *	case->string	argument to match
 *	case->left	parse tree to execute
 */

void
compile_switch( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST	*nt1 = var_list( parse->llist, args );
	LIST	*nt = var_collapse( L0, nt1 );
	list_free( nt1 );

	if( DEBUG_COMPILE )
	{
	    debug_compile( 0, "switch" );
	    list_print( nt );
	    printf( "\n" );
	}

	/* Step through cases */

	for( parse = parse->left; parse; parse = parse->right )
	{
	    if( !glob( parse->left->string, nt ? nt->value : "" ) )
	    {
		/* Get & exec parse tree for this case */
		parse = parse->left->left;
		(*parse->func)( parse, args );
		break;
	    }
	}

	list_free( nt );
}



/*
 * builtin_depends() - DEPENDS/INCLUDES rule
 *
 * The DEPENDS builtin rule appends each of the listed sources on the 
 * dependency list of each of the listed targets.  It binds both the 
 * targets and sources as TARGETs.
 */

static void
builtin_depends( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST *targets = var_collapse( L0, lol_get( args, 0 ) );
	LIST *sources = lol_get( args, 1 );
	int which = parse->num;
	LIST *l;

	for( l = targets; l; l = list_next( l ) )
	{
	    TARGET *t = bindtarget( l->value );
	    t->deps[ which ] = targetlist( t->deps[ which ], sources );
	}
	list_free( targets );
}

/*
 * builtin_echo() - ECHO rule
 *
 * The ECHO builtin rule echoes the targets to the user.  No other 
 * actions are taken.
 */

static void
builtin_echo( parse, args )
PARSE		*parse;
LOL		*args;
{
	list_print( lol_get( args, 0 ) );
	printf( "\n" );
}

/*
 * builtin_exit() - EXIT rule
 *
 * The EXIT builtin rule echoes the targets to the user and exits
 * the program with a failure status.
 */

static void
builtin_exit( parse, args )
PARSE		*parse;
LOL		*args;
{
	list_print( lol_get( args, 0 ) );
	printf( "\n" );
	exit( EXITBAD ); /* yeech */
}

/*
 * builtin_flags() - NOCARE, NOTFILE, TEMPORARY rule
 *
 * Builtin_flags() marks the target with the appropriate flag, for use
 * by make0().  It binds each target as a TARGET.
 */

static void
builtin_flags( parse, args )
PARSE		*parse;
LOL		*args;
{
	LIST *targets = var_collapse( L0, lol_get( args, 0 ) );
	LIST *l = targets;

	for( ; l; l = list_next( l ) )
	    bindtarget( l->value )->flags |= parse->num;

	list_free( targets );
}

/*
 * debug_compile() - printf with indent to show rule expansion.
 */

static void
debug_compile( which, s )
int which;
char *s;
{
	static int level = 0;
	static char indent[36] = ">>>>|>>>>|>>>>|>>>>|>>>>|>>>>|>>>>|";
	int i = ((1+level) * 2) % 35;

	if( which >= 0 )
	    printf( "%*.*s ", i, i, indent );

	if( s )
	    printf( "%s ", s );

	level += which;
}
