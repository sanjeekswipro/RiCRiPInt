# Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
# Global Graphics Software Ltd. Confidential Information.
# $HopeName: HQNperl5libs!Complain.pm(EBDSDK_P.1) $
# In the spirit of carp, cluck, croak and confess,
# two functions that print warnings and remember
# the fact in order to set exit status, and one
# that does the same from the point of view of
# its caller, by not being in the package.
#
# If $Complain::Prefix is set to a subroutine,
# its return values are printed ahead of the message.
# If you just want a string, use an inline function.

package Complain;
use Carp ();
use Exporter;
@ISA = qw(Exporter);
@EXPORT = qw(complain moan);

use vars '$Prefix';

my $errors = 0;

sub complain
{
	Carp::carp(defined($Prefix)? (&$Prefix): (), @_);
	$errors++;
}

sub chide
{
	Carp::cluck(defined($Prefix)? (&$Prefix): (), @_);
	$errors++;
}

END {
	$? = $errors unless $?;
}

sub moan
{
	my $level = $Carp::CarpLevel;
	$Carp::CarpLevel = 1;
	Carp::carp(defined($Prefix)? (&$Prefix): (), @_);
	$errors++;
	$Carp::CarpLevel = $level;
}

sub errors
{
	$errors;
}

1;

__END__
# Log stripped

