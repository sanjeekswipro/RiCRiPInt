# $HopeName: HQNperltools!errormsg.pl(EBDSDK_P.1) $
#
# Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
# Global Graphics Software Ltd. Confidential Information.
#
# Log stripped

# Error message routines.
# The other functions in HQNperltools use these for all
# error messages. Callers can define Custom_* functions
# to override the default action, except for Quit, which
# is not really an error function and is not called from
# within this library.
#
# The default cleanup action is to release all lockfiles
# if lockfile.pl is being used.
#

$SIG{'INT'}  = 'Quit';
$SIG{'QUIT'} = 'Quit' if defined($SIG{'QUIT'});

sub Complain
{
	if (defined(&Custom_Complain)) {
		&Custom_Complain(@_);
	} else {
		print STDERR $_[0];
	}
	$Error_happened = 1;
}
sub Fatal
{
	if (defined(&Custom_Fatal)) {
		&Custom_Fatal(@_);
	} else {
		&Complain(@_);
		&Cleanup(@_);
	}
	exit 1;
}
sub Cleanup
{
	if (defined(&Custom_Cleanup)) {
		&Custom_Cleanup(@_);
	}
	if (defined(&lock_cleanup)) {
		&lock_cleanup();
	}
}
sub Quit
{
	&Cleanup(@_);
	exit $Error_happened;
}
$Error_happened = 0;

1;

