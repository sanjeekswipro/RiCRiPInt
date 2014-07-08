# Copyright (C) 2007-2013 Global Graphics Software Ltd. All rights reserved.
# Global Graphics Software Ltd. Confidential Information.
#
# This package allows scripts to find out information about the system
# they are running on.
#
# $HopeName: HQNperl5libs!Platform.pm(trunk.46) $
#

package Platform;

use Config;
use Carp;
use Complain;

$ComponentSeparator = "-";
$VersionSeparator = "_";

use vars qw($HQNperl5libs);

BEGIN {
  # Find the location of this module to infer the location of others
  $HQNperl5libs = $INC{"Platform.pm"};
  unless ($HQNperl5libs =~ s/[\\\/:]Platform.pm$//i) {
    croak "Couldn't locate HQNperl5libs";
  }
  $xs_apidir = defined($Config{xs_apiversion}) ?
    "$HQNperl5libs/$Config{xs_apiversion}" : $HQNperl5libs ;

}

use lib "$xs_apidir/$Config{archname}", "$xs_apidir", "$HQNperl5libs";

# Functions to return information about the host system
#
sub HostOS {
  _init();
  return wantarray ? @_HostOS   : join($VersionSeparator, @_HostOS)  ;
}
sub HostArch {
  _init();
  return wantarray ? @_HostArch : join($VersionSeparator, @_HostArch);
}
sub HostPlat {
  _init();
  return HostOS.$ComponentSeparator.HostArch;
}
sub HostSupraOS {
  _init();
  return $_HostSupraOS;
}
sub FS {
  _init();
  return $_FS;
}
sub PS {
  _init();
  return $_PS;
}

#
# Function to return a list of those Arch or Os classifications that
# apply to the given one. We could cache them, but it proved to be
# slower when tested.
#
sub Compatible {
  my ($type, $spec) = @_;

  my (@output) = _Compatible($type, $spec);

  while ($spec =~ s/$VersionSeparator[^$VersionSeparator]*$//o)
  {
    push(@output, _Compatible($type, $spec));
  }
  push(@output, "all") if ($output[$#output] ne "all");

  return (@output);
}
#
# Function to return all compatible classifications
#
sub SearchList {
  my $sep = shift;
  my ($OS, $Arch) = _Parse_OS_Arch(@_);
  my (@output) = ();

  my (@OS)   = Compatible("os",   $OS);
  my (@Arch) = Compatible("arch", $Arch);

  foreach $OS (@OS)
  {
    foreach $Arch (@Arch)
    {
      push(@output, $OS.$sep.$Arch);
    }
  }
  return @output;
}

#
# Wrapper to find a file/directory given a sprintf pattern to use for
# looking for directory entries
#
sub Find {
  my ($pattern, $sep, @platform) = @_;
  my (@matches) = ();

  my (@searchlist) = SearchList($sep, @platform);
  my ($i, $file); for $i (@searchlist)
  {
    $file = sprintf($pattern, $i);
    if (wantarray)
    {
      push(@matches, $file) if (-e $file);
    }
    else
    {
      # Uncomment this to debug compatibility code --johnk
      # print "FILE=$file\n";
      return $file if (-e $file);
    }
  }
  return wantarray ? @matches : undef;
}

#
# Say whether one platform includes another, e.g. win-all includes win_nt-pentium.
#
sub Includes {
  my ($general, $specific) = @_;
  my ($gen_os, $gen_arch) = _Parse_OS_Arch($general);
  my ($spe_os, $spe_arch) = _Parse_OS_Arch($specific);

  return 0 unless grep($_ eq $gen_os,   Compatible('os',   $spe_os));
  return 0 unless grep($_ eq $gen_arch, Compatible('arch', $spe_arch));
  return 1;
}


#
# Say whether one platform overlaps another, e.g. win_nt-all overlaps win-pentium at win_nt-pentium.
# Return the intersection or empty string.
#
sub Overlaps {
  my ($plat1, $plat2) = @_;
  my ($os1, $arch1) = _Parse_OS_Arch($plat1);
  my ($os2, $arch2) = _Parse_OS_Arch($plat2);
  my ($os, $arch);

  if (grep($_ eq $os1,   Compatible('os', $os2)))
  {
    $os = $os2;
  }
  elsif (grep($_ eq $os2,   Compatible('os', $os1)))
  {
    $os = $os1;
  }
  else
  {
    return '';
  }
  if (grep($_ eq $arch1,   Compatible('arch', $arch2)))
  {
    $arch = $arch2;
  }
  elsif (grep($_ eq $arch2,   Compatible('arch', $arch1)))
  {
    $arch = $arch1;
  }
  else
  {
    return '';
  }
  return $os.$ComponentSeparator.$arch;
}


##########################
# Implementation details #
##########################

# Function that does the recursive part of Compatible(), without backing
# up the list of version elements
sub _Compatible {
  my ($type, $spec, @output) = @_;
  my ($i);

  _init();

  return @output if (grep($spec eq $_, @output));
  push(@output, $spec);

  foreach $i (@{$_Compatible{$type, $spec}})
  {
    @output = _Compatible($type, $i, @output);
  }

  return @output;
}

# Fill in defaults for OS and architecture arguments.
sub _Parse_OS_Arch
{
  my ($OS, $Arch) = @_;

  if (defined($OS) && $OS =~ /^([^$ComponentSeparator]+)$ComponentSeparator([^$ComponentSeparator]+)$/o)
  {
    ($OS, $Arch) = ($1, $2);
  }
  return ($OS || HostOS || 'all', $Arch || HostArch || 'all');
}


# Initialisation
sub _init {
  return if ($_HostSupraOS);
  if ($ENV{"PROCESSOR_ARCHITECTURE"})
  {
    $_HostSupraOS = "pc";
  }
  elsif ($ENV{"COMSPEC"})
  {
    $_HostSupraOS = "pc";
  }
  elsif ($Config{"osname"} =~/^macos/i)
  {
    $_HostSupraOS = "mac";
  }
  else
  {
    $_HostSupraOS = "unix";
  }

  &{"_$_HostSupraOS"}();

  # Backwards compatibility information

  _AssertOSCompat("solaris", "unix");
  _AssertOSCompat("sunos", "unix");
  _AssertOSCompat("irix", "unix");
  _AssertOSCompat("linux", "unix");
  _AssertOSCompat("netbsd", "unix");
  _AssertOSCompat("solaris_2_7", "solaris_2_6", "solaris_2_5");

  # "win" covers all versions of Microsoft Winsows. i.e. NT3, NT4,
  # 2000, 2003, XP and Vista. PLEASE DO NOT MAKE USE OF dos or pc if
  # at all possible. These are now obsolete but MUST remain in this
  # file for backward compatibility.

  _AssertOSCompat("win", "dos", "pc");

  # "win_32" covers NT3, NT4, 2000, 2003, XP and Vista (i.e. 32 bit OS
  # versions). PLEASE DO NOT MAKE USE OF win_98 or win_95 if at all
  # possible. These are now obsolete but MUST remain in this file for
  # backward compatibility.

  _AssertOSCompat("win_98", "win_95", "win_32");

  # "win_64" covers 2003, XP and Vista (i.e. 64 bit OS versions)

  _AssertOSCompat("win_64", "win");

  # PLEASE DO NOT MAKE USE OF win_nt, win_nt_4 or win_nt_3 if at all
  # possible. These are now obsolete but MUST remain in this file for
  # backward compatibility. "win_nt" covers NT3 and NT4 and 2003.

  _AssertOSCompat("win_nt", "win_32");
  _AssertOSCompat("win_nt_4", "win_nt_3");

  _AssertOSCompat("macos", "mac");
  _AssertOSCompat("macos_9", "macos_8");

  # If we need more detailed configuration by Windows OS, we can
  # introduce new OS names such as:
  # "vista64", "vista32", "xp32", "xp64", etc..
  # Currently I don't see a need for this.

  # "x86" covers the x86 instruction set on 32 bit architectures.
  # "gen64" covers all 64 bit architectures

  _AssertArchCompat("opteron", "amd64", "gen64");
  _AssertArchCompat("turon", "amd64", "gen64");
  _AssertArchCompat("athlon", "amd64", "gen64");

  _AssertArchCompat("itanium", "gen64");
  _AssertArchCompat("duo_64", "pentium4_64");
  _AssertArchCompat("xeon_64", "pentium4_64");
  _AssertArchCompat("pentium4_64", "amd64", "gen64");

  # PLEASE DO NOT MAKE USE OF 486, 386, 486_dx or 486_sx if at all
  # possible. These are now obsolete but MUST remain in this file for
  # backward compatibility.

  _AssertArchCompat("pentium", "486", "386", "x86");
  _AssertArchCompat("486_dx", "486_sx");

  # Unless someone suspects otherwise, make all mips equivalent from r4k
  _AssertArchCompat("mips_r10000", "mips_r8000", "mips_r5000", "mips_r4000", "mips_r10000");

  _AssertArchCompat("g5", "g4", "x704", "g3", "604", "603", "601", "ppc");
  _AssertArchCompat("e500", "ppc");
  _AssertArchCompat("ub_ppc", "ppc");
  _AssertArchCompat("ub_ppc", "ub");
  _AssertArchCompat("ub_386", "386");
  _AssertArchCompat("ub_386", "ub");
  _AssertArchCompat("ub_x86_64", "x86_64");
  _AssertArchCompat("ub_x86_64", "ub");
  _AssertArchCompat("x86_64", "386");
}

# SupraOS=pc-specific bits
sub _pc {
  my ($pa) = $ENV{"PROCESSOR_ARCHITEW6432"};
  my ($ver); chop($ver = `ver`);
  my ($win64_program_files) = $ENV{"ProgramFiles(x86)"} ;

  # For some reason, PROCESSOR_ARCHITECTURE gets trashed in a Win32 emacs on Win64.

  if (! $pa) {
    $pa = $ENV{"PROCESSOR_ARCHITECTURE"};
  }

  $_FS = "\\"; $_PS = ";";
  # Output from the "ver" command on each platform:
  #
  # 2000      - "Microsoft Windows 2000 [Version 5.00.2195]"
  # 2003      - "Microsoft Windows [Version 5.2.3790]"
  # Win32 XP  - "Microsoft Windows XP [Version 5.1.2600]"
  # Win64 XP  - "Microsoft Windows [Version 5.2.3790]"
  #
  # NOTE that 2003 and Win64 are the same. Argg!
  #
  if ($pa) {
    # NT 3 or NT 4 (I think)
    if ($ver =~ /Windows NT Version ([\d\.]*)/)
    {
        push(@_HostOS, "win_32", "nt", split(/[\.$VersionSeparator$ComponentSeparator]/, $1));
    }
    # Win64 XP or Win 2003
    elsif ($ver =~ /Microsoft Windows \[Version ([\d\.]*)\]/)
    {
        # Win64 XP or Win64 2003
        # This is a bit of a hack. We need to be careful about Win32
        # installed on a 64 bit architecture and also 32/64 bit
        # versions of 2003 and Vista --johnk
	if (defined $win64_program_files) {
          push(@_HostOS, "win_64", split(/[\.$VersionSeparator$ComponentSeparator]/, $1));
        # Win 2003
        } else {
          push(@_HostOS, "win_32", "nt", split(/[\.$VersionSeparator$ComponentSeparator]/, $1));
        }
    }
    # Win 2000 or Win32 XP
    elsif ($ver =~ /Windows( 2000| XP)? \[Version ([\d\.]*)\]/)
    {
        push(@_HostOS, "win_32", "nt", split(/[\.$VersionSeparator$ComponentSeparator]/, $2));
    }
    else
    {
        push(@_HostOS, $ver);
    }

    if ($pa eq "ALPHA")
    {
        push(@_HostArch, "alpha", $ENV{"PROCESSOR_LEVEL"}, $ENV{"PROCESSOR_REVISION"});
    }
    elsif ($pa eq "x86")
    {
      my ($family) = $ENV{"PROCESSOR_LEVEL"};
      my ($stepping) = hex($ENV{"PROCESSOR_REVISION"});
      my ($model) = $stepping >> 8;
      $stepping %= 256;

      if ($family == 4)
      {
        push(@_HostArch, "486");

        if ($model < 2)
        {
            push(@_HostArch, "dx");
        }
        elsif ($model == 2)
        {
            push(@_HostArch, "sx");
        }
        elsif ($model == 3)
        {
            push(@_HostArch, "dx", "2");
        }
        elsif ($model == 4)
        {
            push(@_HostArch, "sl");
        }
        elsif ($model == 5)
        {
            push(@_HostArch, "sx", "2");
        }
        elsif ($model == 7)
        {
            push(@_HostArch, "dx", "2");
        }
        else
        {
            push(@_HostArch, "unknown");
        }
      }
      elsif ($family == 5)
      {
        push(@_HostArch, "pentium");
        if ($model == 1  || $model == 2)
        {
        }
        elsif ($model == 3)
        {
            push(@_HostArch, "overdrive");
        }
        elsif ($model == 4)
        {
            push(@_HostArch, "mmx");
        }
        else
        {
            push(@_HostArch, "unknown");
        }
      } elsif ($family == 6)
      {
        push(@_HostArch, "pentium");
        if ($model == 1)
        {
            push(@_HostArch, "pro");
        }
        elsif ($model == 3)
        {
            push(@_HostArch, "2", "3");
        }
        elsif ($model == 5)
        {
            push(@_HostArch, "2", "5");
        }
        elsif ($model == 6)
        {
            push(@_HostArch, "celeron");
        }
        elsif ($model == 7)
        {
            push(@_HostArch, "3", "7");
        }
        elsif ($model == 8)
        {
          push(@_HostArch, "3", "8");
        }
        else
        {
          push(@_HostArch, "unknown");
        }
      } elsif ($family == 15)
      {
        push(@_HostArch, "pentium");
        push(@_HostArch,"4");
      } elsif ($family == 16)
      {
        push(@_HostArch, "pentium");
        push(@_HostArch,"4");
      } else
      {
        push(@_HostArch, "unknown");
      }
    }
    elsif ($pa eq "AMD64")
    {
      my ($family) = $ENV{"PROCESSOR_LEVEL"};
      my ($stepping) = hex($ENV{"PROCESSOR_REVISION"});
      my ($model) = $stepping >> 8;
      $stepping %= 256;

      if ($family == 15)
      {
        push(@_HostArch, "amd64");
        if ($model == 37) {
           push(@_HostArch, "opteron");
        }
      }
      elsif ($family == 6)
      {
        push(@_HostArch, "pentium4_64");
        if ($model == 15) {
          push(@_HostArch, "duo_64");
        }
      }
      else 
      {
        push(@_HostArch, "unknown");
      }
    }
    elsif ($ENV{"COMSPEC"})
    {
      push(@_HostOS, "dos");
    }
    else
    {
      push(@_HostArch, "unknown");
    }

  } # end if pa
}

# SupraOS=unix-specific bits
sub _unix {
  local ($u_mac, $u_rel, @u_rel, $u_sys, $u_ver, $u_pro, $u_imp);

  $_FS="/"; $_PS = ":";

  chop($u_mac = `uname -m 2>/dev/null`);
  chop($u_rel = `uname -r 2>/dev/null`);
  chop($u_sys = `uname -s 2>/dev/null`);
  chop($u_ver = `uname -v 2>/dev/null`);
  chop($u_pro = `uname -p 2>/dev/null`);
  chop($u_imp = `uname -i 2>/dev/null`);

  @u_rel = split(/[\.$VersionSeparator$ComponentSeparator]/o, $u_rel);

  _unix_os();
  _unix_arch();
}
sub _unix_os {
  if ($u_sys =~ /^IRIX/)
  {
    push(@_HostOS, "irix");
    push(@_HostOS, @u_rel);
  }
  elsif ($u_sys =~ /^Darwin/)
  {
    push(@_HostOS, "macos_x");
    if ($u_mac =~ /^i386/ || $u_mac =~ /^x86_64/)
    {
      if ($u_rel =~ /^13/) {
          push(@_HostOS, "10_9");
      } elsif ($u_rel =~ /^12/) {
          push(@_HostOS, "10_8");
      } elsif ($u_rel =~ /^11/) {
          push(@_HostOS, "10_7");
      } elsif ($u_rel =~ /^10/) {
        push(@_HostOS, "10_6");
      } elsif ($u_rel =~ /^9/) {
        push(@_HostOS, "10_5");
      } elsif ($u_rel =~ /^8/) {
        push(@_HostOS, "10_4");
      } else {
        push(@_HostOS, "unknown");
      }
    }
    else
    {
      &_mac_os();
    }
  }
  elsif ($u_sys eq "SunOS")
  {
    if ($u_rel[0] == 5 || $u_rel[0] == 6)
    {
      $u_rel[0] -= 3;
      push(@_HostOS, "solaris");
    }
    else
    {
      push(@_HostOS, "sunos");
    }
    push(@_HostOS, @u_rel);
  }
  elsif ($u_sys eq "Linux")
  {
    push(@_HostOS, "linux");
  }
  elsif ($u_sys eq "NetBSD")
  {
    push(@_HostOS, "netbsd");
  }
  else
  {
    push(@_HostOS, "unknown");
  }
}
sub _unix_arch {
  if ($u_sys =~ /^IRIX/)
  {
    my ($hinv) = `hinv -t cpu 2>/dev/null`;
    if ($hinv =~ /^CPU: MIPS \S*(R\d+) Processor/i)
    {
      push(@_HostArch, "mips", "\L$1");
      return;
    }
  }
  elsif ($u_sys =~ /^Darwin/)
  {
    if ($u_mac =~ /^i386/)
    {
      push(@_HostArch, "386");
    }
    elsif ($u_mac =~ /^x86_64/)
    {
      push(@_HostArch, "x86_64");
    }
    else
    {
      &_mac_arch();
    }
  }
  elsif ($u_mac =~ /^i(\d)86/)
  {
    if ($1 > 4)
    {
      push(@_HostArch, "pentium");
      if ($1 > 5)
      {
        push(@_HostArch, $1-4 );
      }
    }
    else
    {
      push(@_HostArch, $1."86");
    }
  }
  elsif ($u_mac =~ /^x86_64/)
  {
    push(@_HostArch, "amd64");
  }
  elsif ($u_pro)
  {
    push(@_HostArch, "\L$u_pro");
    if ($u_pro eq "sparc" && $u_imp =~ /^SUNW,(.*)$/)
    {
      push(@_HostArch, split(/[\.$VersionSeparator$ComponentSeparator]/o, "\L$1"));
    }
  }
  elsif ($u_mac =~ /^sun3/)
  {
    push(@_HostArch, "m68k");
  }
  elsif ($u_mac =~ /^sun4/)
  {
    push(@_HostArch, "sparc");
  }
  else
  {
    push(@_HostArch, "unknown");
  }
}

# SupraOS=mac-specific bits
sub _mac {

  push(@_HostOS, "macos");
  $_FS=":"; $_PS = ",";

  &_mac_os();
  &_mac_arch();
}
sub _mac_os {
  require Mac::Gestalt;
  import Mac::Gestalt qw(%Gestalt);
  import Mac::Gestalt qw(gestaltSystemVersion);

  my ($os) = $Gestalt{gestaltSystemVersion()};
  if (defined($os) && $os)
  {
    my $osver = ($os & 0xff00) >> 8;
    $osver = sprintf("%x",$osver);
    push(@_HostOS, $osver, ($os & 0xf0)>>4, ($os & 0xf));
  }
  else
  {
    push(@_HostOS, "unknown");
  }
}
sub _mac_arch {
  require Mac::Gestalt;
  import Mac::Gestalt qw(%Gestalt);
  import Mac::Gestalt qw(gestaltSysArchitecture gestalt68k gestaltPowerPC);

  my ($arch) = $Gestalt{gestaltSysArchitecture()};

  if (defined($arch))
  {
    if ($arch == gestalt68k())
    {
      push(@_HostArch, "68k");
    }
    elsif ($arch == gestaltPowerPC())
    {
      push(@_HostArch, "ppc");
      import Mac::Gestalt qw(gestaltNativeCPUtype);
      $arch = $Gestalt{gestaltNativeCPUtype()};
      if (defined($arch))
      {
        if ($arch == 0x101)
        {
          push(@_HostArch, "601");
        }
        elsif ($arch == 0x103)
        {
          push(@_HostArch, "603");
        }
        elsif ($arch == 0x104)
        {
          push(@_HostArch, "604");
        }
        elsif ($arch == 0x106)
        {
          push(@_HostArch, "603", "e");
        }
        elsif ($arch == 0x107)
        {
          push(@_HostArch, "603", "e", "v");
        }
        elsif ($arch == 0x108)
        {
          push(@_HostArch, "g3");
        }
        elsif ($arch == 0x109)
        {
          push(@_HostArch, "604", "e");
        }
        elsif ($arch == 0x10a)
        {
          push(@_HostArch, "604", "e", "v");
        }
        elsif ($arch == 0x154)
        {
          push(@_HostArch, "x704");
        }
        elsif ($arch == 0x160)
        {
          push(@_HostArch, "x704", "2");
        }
        elsif ($arch == 0x10c)
        {
          push(@_HostArch, "g4");
        }
        elsif ($arch == 0x139)
        {
          push(@_HostArch, "g5");
        }
        else
        {
          push(@_HostArch, "unknown", $arch);
        }
      }
      else
      {
        push(@_HostArch, "unknown");
      }
    }
    else
    {
      push(@_HostArch, "unknown", $arch);
    }
  }
  else
  {
    push(@_HostArch, "unknown");
  }
}
# Unknown SupraOS-specific bits
sub _unknown {
  push(@_HostOS, "unknown");
  push(@_HostArch, "unknown");
}

# Functions to store compatibility info
sub _AssertOSCompat   {
  _AssertCompat("os", @_);
}
sub _AssertArchCompat {
  _AssertCompat("arch", @_);
}

sub _AssertCompat {
  my ($hash, @list) = @_;
  while ($#list)
  {
    # Uncomment this to debug compatibility code --johnk
    # print "$hash $list[0] $list[1]\n" ;
    push(@{$_Compatible{$hash,$list[0]}}, $list[1]);
    shift @list;
  }
}

1;

# Log stripped
