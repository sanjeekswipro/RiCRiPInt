# Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
# Global Graphics Software Ltd. Confidential Information.
#
# Package containing routines for manipulating paths platform-
# independently.
#
# $HopeName: HQNperl5libs!Universalfile.pm(trunk.19) $
#
# $file = new UniversalFile ($filename, %options)
#
# $file->append()         -- append path elements to a universal file
# $file->base()           -- get "basename" of universal file
# $file->convert()        -- convert to the local (or specified) format
# $file->convert_to_dir() -- enforce creation of directory syntax
# $file->access_time      \
# $file->modified_time     } return appropriate results from stat()
# $file->utime             }
# $file->stat             /
#

package UniversalFile;

use Carp;
use Complain;
use Platform;

#
# Parse a file or directory name, which could be from any platform,
# relative or absolute, into some internal representation.
#
#   $file = new UniversalFile ($filename, %options);
#
#   style => "mac", "pc", or "unix"
#
  sub new {
    my ($class) = shift; $class = ref($class) || $class;
    my ($this) = bless {'elements' => []}, $class;
    my ($unknownpath, %options) = @_;
    my ($style);

    return bless { %{$unknownpath} }, $class if (ref $unknownpath eq "UniversalFile");

    return undef unless ($unknownpath);

  # First try to identify the style

    unless (exists($options{style})
     && ($style = $options{style})
     && grep($_ eq $style, "mac", "pc", "unix")) {
      my (@elements) = split(/:/, $unknownpath, -1);

      if (@elements == 1) { # No :s
        if ($unknownpath =~ /^[\\\/]{2}(\w+)/) {
          $style = "pc";
        }
        else {
          $style = "unix";
        }
      }
      elsif (@elements == 2) { # One :
        my ($disk, $path) = @elements;

        if ($path =~ /[\\\/]/) {
          if (length($disk) == 1) { # Drive letter
            $style = "pc";
          }
          else { # Too long to be a drive letter
            $style = "unix";
          }
        }
        else {
          $style = "mac";
        }
      }
      else {
        $style = "mac";
      }
    }

  # Look for a machine name.
    if ($style eq "pc" && $unknownpath =~ s/^[\\\/]{2}(\w+)//) {
      $this->{machine} = $1;
    }
    elsif ($style eq "unix" && $unknownpath =~ s/^(\w+)://) {
      $this->{machine} = $1;
    }

  # Look for a disk name.
    if ($style eq "pc" && $unknownpath =~ s/^(\w)://) {
      $this->{disk} = $1;
    }

  # The presence of an initial file separator (or otherwise) tells us
  # whether the path is absolute or not.

    if ($unknownpath =~ /^[:\\\/]/) {
    # The path starts with a pathseparator.  On PC and UNIX this means
    # absolute.  On the mac, it means exactly the opposite.
      if ($style eq "mac") {
        $this->{is_absolute} = 0;
      }
      else {
        $this->{is_absolute} = 1;
      }
    }
    else {
    # The converse is true.  Note that this is not actually true on the
    # mac for files that don't contain any :'s, but then we have already
    # decided that they might as well be unix ones, anyway.
      if ($style eq "mac") {
        $this->{is_absolute} = 1;
      }
      else {
        $this->{is_absolute} = 0;
      }
    }

    $this = $this->append($unknownpath);

  # If we happen to know that the path is absolute on the mac, then it
  # must start with a drive name.  This will be whatever we have left as
  # the first of @elements.

    if ($style eq "mac" && $this->{is_absolute}) {
      unless (@{$this->{elements}}) {
        moan "No path left after processing '::' sequences";
        return undef;
      }
      $this->{disk} = shift @{$this->{elements}};
    }

    return $this;
  }

#
# Return parent directory
#
  sub parent {
    my $orig = shift ;
    my $quiet = shift ;
    my ($this) = bless { %{$orig} };
    $this->{elements} = [ @{$orig->{elements}} ];
    pop @{$this->{elements}};
    my $ok = @{$orig->{elements}};
    unless ($ok) {
      moan "Cannot take parent directory of " . $orig->convert unless $quiet ;
      return undef;
    }
    delete $this->{cache};
    $this->{is_directory} = 1;

    return $this;
  }

#
# Append path elements to a UniversalFile
#
  sub append {
    my ($orig, @splitted) = @_;
    my ($this) = bless { %{$orig} }; # Copy it, rather than modifying the original
    my (@elements) = @{$this->{elements}};

    @splitted = map { while(s/::/\/..:/) {}; $_ } @splitted;

    # Remove escaped "special" characters which will only confuse the
    # directory separator splitter
    @splitted = map { s/\\\\/$;/g; $_ } @splitted;

    @splitted = map { split(/[:\\\/]/, $_, -1) } @splitted;

    # Now put the escaped characters back.
    @splitted = map { s/$;/\\/g; $_ } @splitted;

  # Now process the list of elements, looking for ".." sequences. (And,
  # on PC and UNIX, removing "."s

    my ($i); foreach $i (@splitted) {
      if ($i eq "..") {
        my ($last) = pop @elements;
        if (!defined($last)) {
          if ($this->{is_absolute}) {
            moan "Cannot '..' above top-level";
            return undef;
          }
          push (@elements, "..");
        }
        elsif ($last eq "..") {
          push (@elements, "..", "..");
        }
      }
      elsif ($i eq "" || $i eq ".") {
        $this->{is_directory} = 1;
      }
      else {
        delete $this->{is_directory};
        push (@elements, $i);
      }
    }

    delete $this->{cache};
    $this->{elements} = [ @elements ];

    return $this;
  }


# basename
  sub base {
    my ($this) = @_;
    my (@elements) = @{$this->{elements}};
    return $elements[$#elements];
  }

#
# Routine to convert the internal representation into a system-specific
# representation.  It caches successive calls
#
#   $file->convert
#
  sub convert {
    my ($this, %Options) = @_;
    my ($style) = Platform::HostSupraOS;
    $style = $Options{style} if (exists($Options{style}));

    $this->{cache} = {} unless (exists($this->{cache}));
    my ($cache) = $this->{cache};

    $cache->{$style} = $this->_convert($style) unless ($cache->{$style});
    return $cache->{$style};
  }
  sub _convert {
    my ($this, $style) = @_;
    my ($path) = "";

    if ($style eq "mac") {
      if ( ($this->{disk} && ! $this->{is_absolute})
        || ($this->{is_absolute} && ! $this->{disk}) ) {
        moan "Cannot have non-absolute paths with drive specified or vice versa";
        return undef;
      }
      if ($this->{machine}) {
        moan "Can't convert machine name $this->{machine} to mac format path";
        return undef;
      }
      $path = $this->{disk};
      foreach (@{$this->{elements}})
      {
        if ($_ eq "..")
        {
          $path .= ":";
        }
        else
        {
          $path .= ":".$_;
        }
      }
      $path =~ s/:?$/:/ if ($this->{is_directory});
      return $path;
    }
    elsif ($style eq "pc") {
      if ($this->{machine}) {
        if ($this->{disk}) {
          moan "Can't convert disk $this->{disk} and machine name $this->{machine} to pc format path";
          return undef;
        }
        if (! $this->{is_absolute}) {
          moan "Can't convert relative path on machine $this->{machine} to pc format path";
          return undef;
        }
        $path = "\\\\$this->{machine}";
      }
      $path = $this->{disk}.":" if ($this->{disk});
      if ($this->{is_absolute}) { $path .= "\\"; }
      elsif (! $this->{disk})   { $path .= ".\\"; }
      $path .= join("\\", @{$this->{elements}});
      return $path;
    }
    elsif ($style eq "unix") {
      if ($this->{disk}) {
        moan "Failed to convert disk $this->{disk} to unix format path";
        return undef;
      }
      $path = "$this->{machine}:" if ($this->{machine});
      $path .= "." unless ($this->{is_absolute});
      $path .= "/".join("/", @{$this->{elements}});

      # Escape "special" characters
#      $path =~ s/([^\\])([\(\)])/$1\\$2/g;

      return $path;
    }
    else {
      moan "Cannot convert a path to $style-style";
      return undef;
    }
  }


#
# Same, but forces creation of directory path
#
  sub convert_to_dir {
    my ($this, @args) = @_;
    unless ($this->{is_directory}) {
      $this->{is_directory} = 1;
      delete $this->{cache};
    }
    return ($this->convert(@args));
  }


#
# Return a stat for the given path
#
  sub access_time   { my ($this) = @_; return $this->stat(8); }
  sub modified_time { my ($this) = @_; return $this->stat(9); }
  sub permissions   { my ($this) = @_; return $this->stat(2) & 0777; }
  sub utime         { my ($this) = @_; return ($this->access_time, $this->modified_time); }
  sub stat {
    my ($this, $num) = @_;

    $this->{cache} = {} unless (exists($this->{cache}));
    my ($cache) = $this->{cache};

    $cache->{"stat"} = [ stat($this->convert) ] unless ($cache->{"stat"});
    return $cache->{"stat"}->[$num];
  }


#
# Returns a universal file representing the destination of a symbolic link
#
  sub readlink {
    my ($this) = @_;
    my ($fn) = $this->convert;
    return undef unless (-l $fn);

    my ($link) = readlink($fn);
    return undef unless ($link);

    my ($style) = Platform::HostSupraOS;
    if ($newparent =~ /^\//) {
      return new UniversalFile $link, style => $style;
    }
    else {
      return $this->parent->append($link);
    }
  }


1;


# Log stripped
