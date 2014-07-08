# Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
# Global Graphics Software Ltd. Confidential Information.
#
# Harlequin build system (Jam Doughnut)
#
# $HopeName: HQNbuildtools!Hqmake.pm(trunk.29) $
#

package Hqmake;

use strict;
use vars qw($HQNbuildtools @JamVersions %Toggles @SAVE_ARGS);
use Carp;

BEGIN {
  # Find the location of this module to infer the location of others
  $HQNbuildtools = $INC{"Hqmake.pm"};
  unless ($HQNbuildtools =~ s/[\\\/:]Hqmake.pm$//i) {
    croak "Couldn't locate HQNbuildtools";
  }
}
use lib "$HQNbuildtools/perl5libs", "$HQNbuildtools:perl5libs";
use Platform;
use Universalfile;

@JamVersions = ( [ 2,2,5,6 ], [ 2,2,5,5 ], [ 2,2,5,3 ], [ 2,2,5,2 ] );

select((select(STDOUT), $| = 1)[0]);
select((select(STDERR), $| = 1)[0]);
select((select(STDIN),  $| = 1)[0]);

@SAVE_ARGS = @ARGV ;

#
# Function to parse command line options and create a new build
#
  %Toggles = (
    "a(ssert)?"   =>  "Assert",
    "d(ebug)?"    =>  "Debug",
    "n(oopt)?"    =>  "Noopt",
    "g(lobal)?"   =>  "Global",
    "p(rofile)?"  =>  "Profile",
    "t(iming)?"   =>  "Timing",
    "r(elease)?"  =>  "Release",
    "c(overage)?" =>  "Coverage",
    "i(nline)?"   =>  "Inline",
  );
  sub CommandLineUsage {
    print STDERR "Usage: hqmake ", (map { "[+/-$_]" } keys %Toggles), "\n";
    print STDERR "         [-jamdebug\n";
    print STDERR "         [-t(arget)? <targetplatform>] [-va(riant)? [Non_]<variant>[=<value>]]\n";
    print STDERR "         [-v(erbose)? ...] [-m(axthreads) n] [-q(uitquick)]\n";
    exit 1;
  }
  sub CommandLine {
    my (@NEWARGV);
    my (@OLDARGV) = @ARGV;
    my ($debuglevel) = 1;
    my ($maxthreads) = 1;
    my ($opt, %variant, %regime, @JamOptions);

    if (defined $ENV{"HQMAKE_PREPEND_ARGS"}) {
      @NEWARGV = split(/ /,$ENV{"HQMAKE_PREPEND_ARGS"});
    }

    push(@NEWARGV, @OLDARGV);

    if (defined $ENV{"HQMAKE_APPEND_ARGS"}) {
      push(@NEWARGV, split(/ /,$ENV{"HQMAKE_APPEND_ARGS"}));
    }

    @ARGV = @NEWARGV;

    # only print out command args if we have changed them from what
    # the user types in
    if (defined $ENV{"HQMAKE_PREPEND_ARGS"} ||
        defined $ENV{"HQMAKE_APPEND_ARGS"}) {
      print "cmdargs = ", join(' ', @ARGV), "\n";
    }

    while ($ARGV[0]) {
      if (($opt) = map { $Toggles{$_} } grep($ARGV[0] =~ /^\+$_$/i, keys %Toggles)) {
        $regime{$opt} = 1;
      }
      elsif (($opt) = map { $Toggles{$_} } grep($ARGV[0] =~ /^-$_$/i, keys %Toggles)) {
        $regime{$opt} = 0;
      }
      elsif ($ARGV[0] =~ /^-t(arget)?$/i) {
        shift @ARGV;
        $regime{Targetplatform} = $ARGV[0];
      }
      elsif ($ARGV[0] =~ /^-q(uitquick)?$/i) {
        push (@JamOptions, "-q");
      }
      elsif ($ARGV[0] =~ /^-va(riant)?$/i) {
        shift @ARGV;
        my ($off, $name, $value) = ($ARGV[0] =~ /^(Non_)?([^=]+)=?(.*)$/);
        if ($value eq "") { $value = ($off?0:1); }
        if (exists $variant{$name})
        {
          # variant already specified so concatenate new value
          $variant{$name} = $variant{$name}." ".$value;
        }
        else
        {
          $variant{$name} = $value;
        }
      }
      elsif ($ARGV[0] =~ /^-v(erbose)?$/i) { $debuglevel++ ; }

      elsif ($ARGV[0] =~ /^-m(axthreads)?$/i) 
      {
        shift @ARGV;
        my $mvalue = $ARGV[0];
        $maxthreads = $mvalue    if ( $mvalue >= 2 && $mvalue <= 8 );
      }   

      elsif ($ARGV[0] =~ /^-j(amdebug)?$/i)
      {
        # Not yet
        # $ENV{JAMDEBUG} = 1 ;
        $ENV{JAMDEBUG_INCLUDE} = 1 ;
      }
      elsif ($ARGV[0] eq "--") { shift @ARGV; last; }

      elsif ($ARGV[0] =~ /^[\+-]/) { CommandLineUsage(); }

      else { last; }
      
      shift @ARGV;
    }
    push (@JamOptions, @ARGV);

    DoBuild(%regime, Debuglevel => $debuglevel, Maxthreads => $maxthreads, Variants => \%variant, Targets => \@JamOptions);
  }


#
# Function that actually performs the build.  The argument list is a
# hash table.  The valid keys are
#
# Assert         => 0 or 1              (default controlled by jam)
# Debug          => 0 or 1              (default controlled by jam)
# Noopt          => 0 or 1              (default controlled by jam)
# Global         => 0 or 1              (default controlled by jam)
# Inline         => 0 or 1              (default controlled by jam)
# Release        => 0 or 1              (default controlled by jam)
# Timing         => 0 or 1              (default controlled by jam)
# Coverage       => 0 or 1              (default controlled by jam)
# Debuglevel     => n                   (default: 1)
# Maxthreads     => n                   (default: 1)
# Targetplatform => eg "win_32-pentium" (default: host platform)
# Variants       => A hash table ref of name=>value pairs
# Targets        => A list of strings representing buildable targets
#
  sub DoBuild {
    my (%Argument) = @_;
    my ($key, $val);
    my (@JamVariables) = ();

#
# Parse options
#
  # Build flavours
    for $key (values %Toggles) {
      if (defined($Argument{$key})) {
        push(@JamVariables, uc $key, $Argument{$key});
      }
    }

  # Variants
    my (%variant) = %{$Argument{Variants}};
    push(@JamVariables,
      "JAM_VARIANTS", join(" ", keys %variant),
      map { ( "Variant_$_", $variant{$_} ) } keys %variant
    );

  # Process other options
    my ($targetplatform);
    if (defined($Argument{Targetplatform})) {
      $targetplatform = $Argument{Targetplatform};

      push(@JamVariables, "JAM_TARGET_PLATFORM", $targetplatform);
    }
    else {
      $targetplatform = Platform::HostPlat;
    }
    my ($debuglevel) =
      defined($Argument{Debuglevel})?$Argument{Debuglevel}:1;
    my ($maxthreads) =
      defined($Argument{Maxthreads})?$Argument{Maxthreads}:1;

# Output a compact line of what is being executed. This is so when
# people store build logs, we can easily see what command line
# option was used. This is useful for builds from the build servers
# and regression runs. Please do not remove! --johnk
    print "target: ($targetplatform) args: (" . join(" ", @SAVE_ARGS) . ")\n" ;

#
# Platform stuff
#
  # Find which platform we are running on and run platform-specific bits
    my ($jamappfilespec, $buildroot);
    if (Platform::HostSupraOS eq "mac") {
      $buildroot = `Directory`;
      $jamappfilespec = ":%s:jam";
    }
    elsif (Platform::HostSupraOS eq "pc") {
      $buildroot = `cd`;
      $jamappfilespec = "\\%s\\jam.exe";
    }
    elsif (Platform::HostSupraOS eq "unix") {
      $buildroot = `pwd`;
      $jamappfilespec = "/%s/jam";
    }
    chomp($buildroot); push(@JamVariables, "BUILDROOT", $buildroot);

  # Check that there aren't any spaces in the path
    my (@buildrootparts);
    @buildrootparts = split ' ', $buildroot;
    if (scalar(@buildrootparts) > 1) {
      die "Sorry, unable to build in a directory with spaces in its path: " . $buildroot . "\n";
    }

  # Multiple concurrent builds fail because some tools don't check and
  # cope with files that already exist where they want to put their
  # temporary files.  Let's move where we think the temporary directory
  # is to try and avoid these problems:
    $ENV{"TEMP"} = $ENV{"TMP"} = $ENV{"TMPDIR"} = $buildroot;

  # Find the right jam executable to use and set up the platform
  # variables
    my (@availableversions) = map { $ENV{join("_","CV_JAM",@{$_})}?$_:() } @JamVersions;
    my ($bestversion)   = $availableversions[0];
    my ($latestversion) = $JamVersions[0];
    unless (defined($bestversion) && $bestversion == $latestversion) {
      my ($ver)              = join(".",@{$latestversion});
      my ($latestcompound)   = join("_","HQNjam",@{$latestversion});
      my ($latestcvvariable) = join("_","CV_JAM",@{$latestversion});
      print STDERR <<_;
#
# You do not appear to have the latest version ($ver) of Jam.  Please set
# $latestcvvariable to the checkout directory of $latestcompound.
#
_
      if ($bestversion) {
        my ($ver) = join(".",@{$bestversion});
        print STDERR "# I'll use version $ver for now...\n";
      }
      else {
        print STDERR "# Can't find any Jam at all!\n";
        die;
      }
      print STDERR "#\n";
    }

    my (@targets);
    foreach (@{$Argument{Targets}}) {
      if (/^[<{(](.+)[>})](.+)$/) {
        my ($uf) = new UniversalFile($1);
        my ($target) = $2;
        if (defined $uf) {
          push (@targets, "<".$uf->convert.">".$target);
          next;
        }
      }
      push (@targets, $_);
    }

    my ($CV_variable) = join("_","CV_JAM",@{$bestversion});
    my ($jampath) = $ENV{$CV_variable};
    $jampath =~ s/:$//;
    my ($jamapp);
    $jamapp = Platform::Find("$jampath$jamappfilespec", $Platform::ComponentSeparator);
    croak "There doesn't appear to be a suitable jam executable for "
      . Platform::HostPlat unless ($jamapp);

    if (Platform::HostSupraOS ne "mac") {
      my ($appdirspec) = $HQNbuildtools . Platform::FS . "%s";
      my (@PATH) = Platform::Find($appdirspec, $Platform::ComponentSeparator);
      $ENV{"PATH"} = join(Platform::PS, @PATH, $ENV{"PATH"});
    }

    push(@JamVariables,
      "JAM_HOST_PATH",
        join(Platform::PS,Platform::SearchList($Platform::ComponentSeparator)),
      "JAM_TARG_PATH",
        join(Platform::PS,Platform::SearchList($Platform::ComponentSeparator,$targetplatform)),
      "HQNbuildtools",
        $HQNbuildtools,
      "HqmakePerl",
        $^X,
      "JAM_ARGUMENTS",
        join(" ", @targets)
    );


#
# Executing jam
#
  # Assemble command line
    my ($jobfile) = $buildroot . ":JamJob.mpw";
    if (Platform::HostSupraOS eq "mac") {
      $jamapp .= " -o $jobfile";
    }
    my ($commandline) =
      $jamapp." -f ".$HQNbuildtools.Platform::FS."jambits".Platform::FS."base.jam"
             ." -d".((Platform::HostSupraOS eq "mac")?1:$debuglevel);

    # add option for concurrent shell processing if -m parameter set
    $commandline .= " -j$maxthreads"   if ($maxthreads > 1);
    
    while (@JamVariables) {
      $key = shift @JamVariables; $val = shift @JamVariables;
      $commandline .= " -s$key=\"$val\"";
    }
    $commandline .= " ".join(" ", map { "\"$_\"" } @targets);

    print STDERR "Jam: running $commandline\n" if ($debuglevel > 1);
    if (Platform::HostSupraOS eq "mac") {
      print $commandline;
      return 0;
    }
    else {
      return system($commandline)>>8;
    }
  }


1;


# Log stripped
