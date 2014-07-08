/*
 * Copyright 1993, 1995 Christopher Seiwald.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

/*
 * variable.h - handle jam multi-element variables
 *
 * 08/23/94 (seiwald) - Support for '+=' (append to variable)
 */

/*
 * VARIABLE - a user defined multi-value variable
 */

typedef struct _variable VARIABLE ;

struct _variable {
	char	*symbol;
	LIST	*value;
} ;

VARIABLE *var_ref( char* );
LIST *var_get();
LIST *var_collapse( LIST*, LIST* );
void var_defines();
void var_set();
LIST *var_swap();
LIST *var_list();
int var_string();
void var_done();

/*
 * Defines for var_set().
 */

# define VAR_SET	0	/* override previous value */
# define VAR_APPEND	1	/* append to previous value */
# define VAR_DEFAULT	2	/* set only if no previous value */

