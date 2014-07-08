# Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
# Global Graphics Software Ltd. Confidential Information.
#
# Package containing file and directory manipulation routines, many of
# which can be called directly from the command-line (ie they interpret
# @ARGV if no arguments are given).
#
# $HopeName: HQNperl5libs!Fileutil.pm(EBDSDK_P.1) $
#
# Fileutil::Copy()          -- Copy files and directories.
# Fileutil::Erase()         -- Erase files and directories.
# Fileutil::Find()          -- Like the UNIX 'find' command.
# Fileutil::MakeDirectory() -- Make directories and their parents.
# Fileutil::ReadDir()       -- Return a list of entries minus dot and dotdot.
# Fileutil::Touch()         -- Set modified and creation times on files.
#
# The source code for the Perl extension (.xs) file can be found in the
# compound HQNperl5xs. There are separate versions for MacOS X and Win32.

package Fileutil;

use strict;
use vars qw($VERSION @ISA $HQNperl5libs %Options);

use Config;
use Carp;
use Complain;
use File::Copy;
use Getopt::Long;

use Universalfile;

require DynaLoader; @ISA = ("DynaLoader");

$VERSION = '0.01';

BEGIN {
  # Find the location of this module to infer the location of others
  $HQNperl5libs = $INC{"Fileutil.pm"};
  unless ($HQNperl5libs =~ s/[\\\/:]Fileutil.pm$//i) {
    croak "Couldn't locate HQNperl5libs";
  }
}
use lib $HQNperl5libs;

#
# Load any required extension for this platform
#
if ($Config{"osname"} =~ /^darwin/i)
{
  # Mac OS X
  # Locate via $Config{"xs_apiversion"} when defined (10.2, 10.3)
  if ($Config{"xs_apiversion"}) {
    if (-e $HQNperl5libs."/".$Config{"xs_apiversion"}."/".$Config{"archname"}."/auto/Fileutil") {
      push @INC, $HQNperl5libs."/".$Config{"xs_apiversion"}."/".$Config{"archname"} ;
      bootstrap Fileutil $VERSION;
    }
    else {
      croak "No Fileutil extension found for ".$Config{"xs_apiversion"}."/".$Config{"archname"};
    }
  }
  # Otherwise use $Config{"api_versionstring"}
  elsif ($Config{"api_versionstring"}) {
    if (-e $HQNperl5libs."/".$Config{"api_versionstring"}."/".$Config{"archname"}."/auto/Fileutil") {
      push @INC, $HQNperl5libs."/".$Config{"api_versionstring"}."/".$Config{"archname"} ;
      bootstrap Fileutil $VERSION;
    }
    else {
      croak "No Fileutil extension found for ".$Config{"api_versionstring"}."/".$Config{"archname"};
    }
  }
  else {
    croak "Unable to locate Fileutil extension";
  }
}
elsif ($Config{"osname"} =~ /^mswin/i) {
  # Windows
  # Locate via $Config{"archname"}
  if (-e $HQNperl5libs."/".$Config{"archname"}."/auto/Fileutil") {
    push @INC, $HQNperl5libs."/".$Config{"archname"} ;
    bootstrap Fileutil $VERSION;
  }
  else {
    croak "No Fileutil extension found for ".$Config{"archname"};
  }
}
elsif ($Config{"osname"} =~ /^linux/i) {
  # Linux
  # No extension so nothing to do 
}
elsif ($Config{"osname"} =~ /^solaris/i) {
  # solaris
  # no extension
}
elsif ($Config{"osname"} =~ /^netbsd/i) {
  # netbsd
  # no extension
}
else {
  croak "Unhandled OS";
}


#
# Getopt::Long configuration common to all command-line aware routines
#
  Getopt::Long::config( # Should be Configure()
    "bundling",       # eg -vax == -v -a -x
    "no_ignore_case", # We want case-sensitive options
  );


#
# Function for "touching" a file ie updating the modified time stamp.
#
  sub _Touch {
    my $now = shift(@_) ;
    my @files = map { new UniversalFile ($_) } @_ ;
    my $file;
    foreach $file (@files) {
      if ( ! -e $file->convert ) {
        if ( $Options{"create-missing"} ) {
	  if ( defined($file->parent) ) {
	    die "Cannot create directory for file ".$file->convert.": $!"
	      unless _CreateOrComplain(1, $file->parent) ;
	  }
	  die "Cannot touch missing file ".$file->convert.": $!"
	    unless open(NEW, ">" . $file->convert) && close(NEW) ;
        } else {
	  die "Cannot touch missing file ".$file->convert.": $!" ;
        }
      }
      my ($perm) = $file->permissions;
      unless (chmod($perm | 0666, $file->convert)) {
        moan "Failed to change permissions on ".$file->convert.": $!";
      }
      unless(utime($now, $now, $file->convert)) {
        moan "Failed to change timestamps on ".$file->convert.": $!";
      }
      my @now = (localtime($now))[reverse 0..5] ;
      $now[0] += 1900 if $now[0] < 1000 ;
      $now[1] += 1 ;
      printf "Updated times on %s to %04d-%02d-%02d %02d:%02d:%02d\n", $file->convert, @now if ($Options{verbose});
      unless (chmod($perm, $file->convert)) {
        moan "Failed to change permissions on ".$file->convert.": $!";
      }
    }
  }

#
# Function to get Hope timestamp of a file. The default time returned is the
# checkin time of the file.
# Usage:
#   HopeTime("platform-file")
#   HopeTime("platform-file", Fileutil::checkin)
#   HopeTime("platform-file", Fileutil::checkout)
#
  sub checkin { 5 }
  sub checkout { 4 }

  sub HopeTime {
    local $_ ;
    my $hopefile = new UniversalFile shift(@_) ;
    my $origname = $hopefile->convert ;

    my $timeindex = @_ != 0 ? shift(@_) : checkin ;
    die "HopeTime index invalid"
      unless $timeindex == checkin || $timeindex == checkout ;

    die "HopeTime has extra arguments @_" if @_ ;

    my @hopename ;
    FOUND: while ( defined($hopefile) ) {
      unshift(@hopename, $hopefile->base) ;
      my $parent = $hopefile->parent(1) ; # don't moan about missing parent
      if ( defined($parent) ) {
	my $version = $parent->append(".version") ;
	if ( -e $version->convert ) {
	  die "Can not read Hope version file ".$version->convert.": $!"
	    unless open(VERSION, $version->convert) ;

	  my $hopename = join(':', @hopename) ;
	  while (<VERSION>) {
	    chomp;
	    my @version = split(',', $_) ;
	    if ( $version[2] eq $hopename ) {
	      my $timestamp = $version[$timeindex] ;
	      close(VERSION) ;
	      return $timestamp ;
	    }
	  }
	  close(VERSION) ;
	}
      }
      $hopefile = $parent ;
    }

    return undef ;
  }

#
# Command-line aware function for "touching" a file, possibly using Hope
# timestamp of a file.
#
  sub Touch {
    local (%Options) = @_;
    unless (%Options) { # Parse command-line
      die "Failed to parse command-line options" unless (GetOptions(\%Options,
        "ci-time|i=s",
        "co-time|o=s",
        "create-missing|c",
        "default-hope|d",
        "help|h",
        "files=s@",
        "verbose|v",
      ));
      push(@{$Options{"files"}}, @ARGV) if @ARGV;
      @ARGV = ();
    }
    else {
      $Options{"files"} = [ $Options{"files"} ] unless ref($Options{"files"});
    }

    if ($Options{"help"}) {
      print STDERR <<_;

Usage: touch <options> [ <file> ... ]
  options:
    --ci-time file            Set times to Hope checkin time of file
    --co-time file            Set times to Hope checkout time of file
    --create-missing (or -m)  Create the file if missing
    --default-hope   (or -d)  Default time to now if no Hope time found
    --help           (or -h)  This usage summary.
    --verbose        (or -v)  Print out details of file operations.
_
      exit 0;
    }

    my $now ;
    if ( defined($Options{"ci-time"}) ) { # Use Hope checkin time of file
      die "--co-time cannot be used with --ci-time option"
	if defined($Options{"co-time"}) ;
      $now = HopeTime($Options{"ci-time"}, Fileutil::checkin) ;
      if ( !defined($now) ) {
	if ( $Options{"default-hope"} ) {
	  $now = (stat $Options{"ci-time"})[9] ;
        } else {
	  die "No Hope file found for " . $Options{"ci-time"} ;
	}
      }
    } elsif ( defined($Options{"co-time"}) ) { # Use Hope checkout time of file
      die "--ci-time cannot be used with --co-time option"
	if defined($Options{"ci-time"}) ;
      $now = HopeTime($Options{"co-time"}, Fileutil::checkout) ;
      if ( !defined($now) ) {
	if ( $Options{"default-hope"} ) {
	  $now = (stat $Options{"co-time"})[9] ;
	} else {
	  die "No Hope file found for " . $Options{"co-time"} ;
	}
      }
    } else {
      $now = time;
    }

    _Touch($now, @{$Options{"files"}}) ;
  }


#
# Function for copying files and directories with more options than you
# can shake a stick at.  (COMMAND-LINE AWARE)
#
  sub Copy {
    local (%Options) = @_;
    unless (%Options) { # Parse command-line
      die "Failed to parse command-line options" unless (GetOptions(\%Options,
        "create-missing|m",
        "destination|to=s",
        "dest-is-parent|d",
        "follow-links|l",
        "help|h",
        "ignore-netatalk",
        "newer|n",
        "permissions|p",
        "progress",
        "replace|f",
        "symlinks|s",
        "source|from=s@",
        "times|t",
        "update|u",
        "verbose|v",
      ));
      $Options{destination} = pop @ARGV if (@ARGV);
      push(@{$Options{source}}, @ARGV)  if (@ARGV);
      @ARGV = ();
    }
    else {
      $Options{source} = [ $Options{source} ] unless (ref($Options{source}));
    }

    if ($Options{help}) {
      print STDERR <<_;

Usage: hqcopy <options> <source> [ <source> ... ] <destination>
  options:
    --create-missing (or -m)  Create missing directories as required.
    --dest-is-parent (or -d)  Copies should be created _in_ the
                              specified destination directory.
    --follow-links   (or -l)  When the destination exists as a symbolic
                              link, put the copy where the symbolic link
                              points to, rather than replacing it.
    --help           (or -h)  This usage summary.
    --ignore-netatalk         Don't look for or copy Netatalk files.
    --newer          (or -n)  Only copy files that are newer than the
                              destination.
    --permissions    (or -p)  Attempt to preserve permissions in the
                              copy created.
    --replace        (or -f)  Force removal of existing
                              files/directories.
    --symlinks       (or -s)  Copy symbolic links, not the files they
                              point to
    --times          (or -t)  Attempt to preserve access and modified
                              times on the copies.
    --update         (or -u)  Update the access and modified times of
                              copies.
    --verbose        (or -v)  Print out details of file operations.
_
      exit 0;
    }

    my ($rv) = 1;
    my (@source) = map { new UniversalFile ($_) } @{$Options{"source"}};
    my ($dest)   = new UniversalFile $Options{"destination"};
    local ($Fileutil::progresscount) = 0 ;

    # Filter out Netatalk components if they have been passed on the
    # command-line
    if ($Options{"ignore-netatalk"}) {
      @source = grep( !grep(/^\.AppleD(esktop|ouble)$/, @{$_->{elements}}),
                      @source);
    }

    # Check the arguments
    unless ((@source && $dest)) {
      moan "Both source and destination must be specified" ;
      return 0;
    }
    if (@source > 1 && ! $Options{"dest-is-parent"}) {
      moan "Cannot copy more than one source to single destination.  Maybe you want to use dest-is-parent?";
      return 0;
    }
    if (defined($Options{times}) && defined($Options{update})) {
      moan "Cannot specify times and update together";
      return 0;
    }
    local ($|) = 1 if ($Options{progress});


    # The destination's parent directory is either $dest->parent, or $dest
    # itself if dest-is-parent is specified.  Assuming neither the dest
    # or the parent are the root directory, make sure that the
    # destination's parent exists.
    my ($destparent) = $dest;
    if (@{$dest->{elements}}
    && ($Options{"dest-is-parent"} || @{($destparent = $dest->parent)->{elements}}) ) {
      # Follow any symbolic links that exist there to find the actual
      # destination
      if ($Options{"follow-links"}) {
          my ($link);
          $destparent = $link while ($link = $destparent->readlink);
      }

      # Make sure that directory exists
      _RemoveOrComplain($Options{replace} && $Options{"create-missing"}, $destparent) or return 0;
      _CreateOrComplain($Options{"create-missing"}, $destparent) or return 0;
    }

    # Perform the recursive copy
    if ($Options{"dest-is-parent"}) {
      my ($i); foreach $i (@source) {
        my ($destination) = $dest->append($i->base);
        _GutsOfCopy($i, $destination) or $rv = 0;
      }
    }
    else {
      _GutsOfCopy($source[0], $dest) or $rv = 0;
    }
    print "\n" if ($Options{progress}) && $Fileutil::progresscount ;

    return $rv;
  }

  sub _GutsOfCopy {
    my ($source, $dest) = @_;
    my ($rv) = 1;

    # Follow symbolic links in the source and destination as required
    if ($Options{"follow-links"}) {
        my ($link);
        $dest = $link while ($link = $dest->readlink);
    }
    unless ($Options{symlinks}) {
        my ($link);
        $source = $link while ($link = $source->readlink);
    }

    if ($source->convert eq $dest->convert) {
      moan "Cannot copy ".$source->convert." to itself";
      return 0;
    }

    unless (-l $source->convert || -e $source->convert) {
      moan "Source ".$source->convert." does not exist -- can't copy";
      return 0;
    }

    if (-d $source->convert && ! -l $source->convert) {
      _RemoveOrComplain($Options{replace} && $Options{"create-missing"}, $dest) or return 0;
      _CreateOrComplain($Options{"create-missing"}, $dest, $source) or return 0;
      my ($dir) = $source->convert_to_dir;
      my (@directory) = ReadDir($dir);
      my ($i); foreach $i (grep !/^\.\.?$/, @directory) {
        # We cannot yet copy the icons for mac directories stored on
        # NTFS via services for macintosh.
        if ($i eq "Icon?") {
          print "WARNING: cannot copy icon for directory $dir to ",$dest->convert,"\n";
          next;
        }
        next if ($Options{"ignore-netatalk"} && $i =~ /^.AppleD(esktop|ouble)$/);
        _GutsOfCopy($source->append($i), $dest->append($i)) or $rv = 0;
      }
    }
    else {
      # Look for Netatalk files
      unless ($Options{"ignore-netatalk"}) {
        my ($applesrc, $appledst);
        $applesrc = $source->parent->append(".AppleDouble", $source->base);
        $appledst = $dest->parent->append(".AppleDouble", $dest->base);
        _GutsOfCopy($applesrc, $appledst) if (-e $applesrc->convert);
      }

      # If we are only copying newer files, then check (but only for real,
      # plain, files -- this seems a bit iffy for pipes, devices and links).
      if ($Options{newer}
       && (-l $dest->convert || -e $dest->convert)
       && (-f $source->convert && ! -l $source->convert)) {
        unless ($source->modified_time > $dest->modified_time) {
          if ($Options{verbose}) {
            print "Skipping newer file ".$dest->convert."\n";
          }
          # When Jam thinks it needs to update a target by copying, when
          # the timestamp of the actual source file isn't what caused
          # the need for an update, then we need to touch the
          # destination, because otherwise Jam will decide to do the
          # update again next build.  This is a problem for (eg) copied
          # header files, since every dependent source file will be
          # rebuilt again and again.
          _Touch(time, $dest) if ($Options{update});
          return 0;
        }
      }
      else {
        _RemoveOrComplain($Options{replace}, $dest) or return 0;
      }

      # So we are going to do the copy -- remove any existing destination.
      if ($Options{replace} && (-l $dest->convert || -e $dest->convert)) {
        _recursive_Erase($dest) or return 0;
      }

      # Do the copy.
      my ($success);
      if (-l $source->convert) {
        # Create a new symlink.  Note that we cannot use the readlink method
        # on the Universalfile, since we want to preserve the same
        # "relativeness" of the source in the copy.
        $success = symlink(readlink($source->convert), $dest->convert);
      }
      elsif (defined &Fileutil::syscopy) {
        $success = Fileutil::syscopy($source->convert, $dest->convert);
      }
      else {
        $success = File::Copy::syscopy($source->convert, $dest->convert);
      }
      unless ($success) {
        moan "Failed to copy ".$source->convert." to ".$dest->convert.": $!";
        return 0;
      }

      ++$Fileutil::progresscount, print "+" if ($Options{progress});
      if ($Options{verbose}) {
        print "Copied ".$source->convert." to ".$dest->convert."\n";
      }
    }

    # I'm not sure that attempting to change times and permissions on
    # symbolic links is either portable or profitable.  Let's not bother,
    # so that we at least know it won't happen.
    unless (-l $source->convert) {
      # Make the copy writable, so I can change the timestamps if I wish
      # (and so the destination permissions are well-defined).
      unless (chmod($source->permissions | 0666, $dest->convert)) {
        moan "Failed to change permissions on ".$dest->convert.": $!";
      }

      if ($Options{update}) {
        _Touch(time, $dest);
      }
      elsif ($Options{times}) {
        my (@utimes) = $source->utime;
        unless (utime(@utimes, $dest->convert)) {
          moan "Failed to change timestamps on ".$dest->convert.": $!";
        }
        print "Copied times to ".$dest->convert."\n" if ($Options{verbose});
      }

      if ($Options{permissions}) {
        unless (chmod($source->permissions, $dest->convert)) {
          moan "Failed to change permissions on ".$dest->convert.": $!";
        }
        print "Changed permissions on ".$dest->convert."\n" if ($Options{verbose});
      }
    }

    return $rv;
  }

  sub _CreateOrComplain {
    my ($create, $dir, $source) = @_;
    my ($dest) = $dir->convert_to_dir;
    my ($mode) = (defined $source)?$source->permissions:undef;
    unless (-d $dest) {
      if ($create) {
        ++$Fileutil::progresscount, print "." if ($Options{progress});
        return _MakeDirectory($dir, $source);
      }
      else {
        moan "Destination $dest does not exist";
        return 0;
      }
    }
    return 1;
  }

  sub _RemoveOrComplain {
    my ($remove, $dir) = @_;
    my ($dest) = $dir->convert;
    if (-l $dest || (-e $dest && ! -d $dest)) {
      if ($remove) {
        _recursive_Erase($dir) or return 0;
        ++$Fileutil::progresscount, print "-" if ($Options{progress});
        print "Removed $dest\n" if ($Options{verbose});
      }
      else {
        moan "Destination $dest already exists";
        return 0;
      }
    }
    return 1;
  }


#
# Function for creating directories. (COMMAND-LINE AWARE)
#
  sub MakeDirectory {
    local (%Options) = @_;
    unless (%Options) { # Parse command-line
      die "Failed to parse command-line options" unless (GetOptions(\%Options,
        "follow-links|l",
        "help|h",
        "name=s@",
        "replace|f",
        "source|s=s",
        "verbose|v",
      ));
      push(@{$Options{name}}, @ARGV) if (@ARGV);
      @ARGV = ();
    }
    else {
      $Options{name} = [ $Options{name} ] if (!ref($Options{name}) || ref($Options{name}) eq "UniversalFile");
    }

    if ($Options{help}) {
      print STDERR <<_;

Usage: MakeDirectory <options> <name> ...
  options:
    --follow-links   (or -l)  When the name exists as a symbolic link,
                              put the directory where the symbolic link
                              points to, rather than replacing it.
    --help           (or -h)  This usage summary.
    --replace        (or -f)  Force removal of existing
                              files.
    --source         (or -s)  Source directory to copy permissions from.
    --verbose        (or -v)  Print out details of file operations.
_
      exit 0;
    }

    my (@name) = map { ref($_)?($_):($_?(new UniversalFile ($_)):()) } @{$Options{"name"}};
    my ($source) = $Options{source}?(new UniversalFile ($Options{source})):undef;
    my ($i); foreach $i (@name) {
      my ($dir) = $i;
      if ($Options{"follow-links"}) {
        my ($link);
        while ($link = $dir->readlink) { $dir = $link; }
      }
      _RemoveOrComplain($Options{replace}, $dir) or return 0;
      _MakeDirectory($dir, $source) or return 0;
    }

    return 1;
  }
  sub _MakeDirectory {
    my ($dir, $source) = @_;
  # The only way we can see if UNC directories exist is to try and
  # create them with mkdir.  This means we can't use the
  # CreateDirectoryEx(), which is a pain.  Since we want to use
  # CreateDirectoryEx() if it exists, we will
  #
  # * Look for the directory with -d
  # * Attempt to make it with CreateDirectoryEx() if we have it
  # * Attempt to create it with mkdir()
  # * Recurse on our parent
  # * Try CreateDirectoryEx() and mkdir() again, failing for good, this
  #    time

  # Our "mkpath" function, which tries CreateDirectoryEx() and then
  # mkdir() (succeeding as soon as either does, or if mkdir() fails
  # because the directory already exists.
    my ($mkpath) = sub {
         (defined $source && defined &Fileutil::CreateDirectoryEx && Fileutil::CreateDirectoryEx($source->convert, $_[0]->convert))
      || (mkdir($_[0]->convert, (defined $source)?$source->permissions:0777) || $! == 17)
    };

    unless (-d $dir->convert) {
      unless (&{$mkpath}($dir)) {
        if (@{$dir->{elements}}) {
          _MakeDirectory($dir->parent, $source) or return 0;
        }
        unless (&{$mkpath}($dir)) {
          moan "Failed to create ".$dir->convert.": $!";
          return 0;
        }
      }
      print "Created directory ".$dir->convert."\n" if ($Options{verbose});
    }
    return 1;
  }


#
# Function for (recursively) deleting the specified file/directory.
# (COMMAND-LINE AWARE)
#
  sub Erase {
    local (%Options) = @_;
    unless (%Options) { # Parse command-line
      die "Failed to parse command-line options" unless (GetOptions(\%Options,
        "ignore-missing|i",
        "help|h",
        "name=s@",
        "force|f",
        "verbose|v",
      ));
      push(@{$Options{name}}, @ARGV)  if (@ARGV);
      @ARGV = ();
    }
    else {
      $Options{name} = [ $Options{name} ] if (!ref($Options{name}) || ref($Options{name}) eq "UniversalFile");
    }

    if ($Options{help}) {
      print STDERR <<_;

Usage: Erase <options> <name> ...
  options:
    --ignore-missing (or -m)  Ignore missing directories.
    --force          (or -f)  Force removal of existing files.
    --help           (or -h)  This usage summary.
    --verbose        (or -v)  Print out details of file operations.
_
      exit 0;
    }

    my (@name) = map { ref($_)?($_):($_?(new UniversalFile ($_)):()) } @{$Options{"name"}};
    my ($i); foreach $i (@name) {
        _recursive_Erase($i) if (!$Options{"ignore-missing"} || -e $i->convert || -l $i->convert);
    }
  }

  sub _recursive_Erase {
    my ($file) = @_;
    return unless ($file);

    my ($name) = $file->convert;

    chmod($file->permissions | 0777, $name);
    if (-d $name && ! -l $name) {
      my (@directory) = ReadDir($name);
      my ($j); foreach $j (grep !/^\.\.?$/, @directory) {
        _recursive_Erase($file->append($j));
        # no return here; try the other sub-directories, and let rmdir fail.
      }
      unless (rmdir($name)) {
        moan "Failed to rmdir $name: $!";
        return 0;
      }
    }
    else {
      unless (unlink($name)) {
        moan "Failed to erase $name: $!";
        return 0;
      }
    }
    return 1;
  }

#
# Function for recursively traversing a file tree ala UNIX 'find'.
# (COMMAND-LINE AWARE)
#
  sub Find {
    my (%Options) = @_;
    unless (%Options) { # Parse command-line
      die "Failed to parse command-line options" unless (GetOptions(\%Options,
        "action|a=s@",
        "directories|d=s",
        "follow-links|l",
        "help|h",
        "regexp|r=s",
        "root|name|n=s@",
        "type|t=s",
      ));
      push(@{$Options{root}}, @ARGV) if (@ARGV);
      @ARGV = ();
    }
    else {
      $Options{root}   = [ $Options{root}   ] unless (ref($Options{root})   eq "ARRAY");
      $Options{action} = [ $Options{action} ] unless (ref($Options{action}) eq "ARRAY");
    }

    $Options{action} = ["print"] unless ($Options{action} && @{$Options{action}});
    @{$Options{action}} = map {
        if (ref($_) eq "CODE") {
            $_;
        }
        elsif (lc $_ eq "print") {
            sub { my($i) = @_; print $i->convert, "\n"; }
        }
        else {
            moan "Don't understand built-in action '$_'";
            $Options{help} = 1;
            ();
        }
    } @{$Options{action}};
    
    if ($Options{directories}) {
      $Options{directories} = lc $Options{directories} ;
      unless(grep($Options{directories}, "before", "after")) {
        moan "Argument to --directories must be one of 'before' or 'after'";
        $Options{help} = 1;
      }
    }
    else {
      $Options{directories} = "before";
    }

    $Options{type} =~ s/^(.).*$/$1/ if ($Options{type});

    if ($Options{help}) {
      print STDERR <<_;

Usage: Find <options> <root> ...
  options:
    --action <action>(or -a)  Action to perform on files found.
                              (Defaults to "print")
    --directories    (or -d)  Given the argument "before" or "after".
                              Directories will be processed either
                              before or after their contents.  (Defaults
                              to "before")
    --follow-links   (or -l)  When the destination exists as a symbolic
                              link, act on what the link points to,
                              rather than the link itself.
    --help           (or -h)  This usage summary.
    --regexp         (or -r)  File's "basename" matches the given
                              regular expression.
    --type           (or -t)  Only process those files that match the
                              specified type ("l" for link, "f" for
                              plain file, "d" for directory).
_
      exit 0;
    }

    my (@root) = map { ref($_)?($_):($_?(new UniversalFile ($_)):()) } @{$Options{root}};

    my ($i); foreach $i (@root) {
      _recursive_Find(\%Options, $i);
    }
  }
  sub _recursive_Find {
    my ($Options, $file) = @_;
    return unless ($file);

    my (%Options) = %{$Options};
    if ($Options{"follow-links"}) {
        my ($link);
        $file = $link while ($link = $file->readlink);
    }

    _Find_process($Options, $file) if ($Options{directories} eq "before");

    my ($name) = $file->convert;
    if (-d $name && ! -l $name) {
      my (@directory) = ReadDir($name);
      my ($j); foreach $j (grep !/^\.\.?$/, @directory) {
        _recursive_Find($Options, $file->append($j));
      }
    }

    _Find_process($Options, $file) if ($Options{directories} eq "after");

    return 1;
  }
  sub _Find_process {
    my ($Options, $file) = @_;
    my (%Options) = %{$Options};

    my ($name) = $file->convert;

    if ($Options{regexp}) {
        return unless ($file->base =~ /$Options{regexp}/);
    }

    if ($Options{type}) {
      my ($type) = $Options{type};
      if ($type eq "l") {
        return unless (-l $name);
      }
      elsif ($type eq "f") {
        return unless (-f $name && ! -l $name);
      }
      elsif ($type eq "d") {
        return unless (-d $name && ! -l $name);
      }
    }

    my ($func); foreach $func (@{$Options{action}}) {
      &{$func}($file);
    }
  }


#
# A collection of constant functions for interpretting bit fields
#
  sub BACKUP_INVALID             {0}
  sub BACKUP_DATA                {1}
  sub BACKUP_EA_DATA             {2}
  sub BACKUP_SECURITY_DATA       {3}
  sub BACKUP_ALTERNATE_DATA      {4}
  sub BACKUP_LINK                {5}
  sub BACKUP_PROPERTY_DATA       {6}

  sub STREAM_NORMAL_ATTRIBUTE    {0}
  sub STREAM_MODIFIED_WHEN_READ  {1<<0}
  sub STREAM_CONTAINS_SECURITY   {1<<1}
  sub STREAM_CONTAINS_PROPERTIES {1<<2}


  sub ReadDir
  {
    my ($dir) = @_;
    if (opendir (TEMP_DIRH, $dir)) {
      my (@entries) = grep(!m/^\.\.?$/, readdir(TEMP_DIRH));

      # Escape special characters
      @entries = map { s/\\/$;/g; s/$;/\\\\/g; $_ } @entries;
      closedir (TEMP_DIRH);
      return @entries;
    }
    moan "*** can't open directory $dir: $!\n";
    return ();
  }

1;


# Log stripped
