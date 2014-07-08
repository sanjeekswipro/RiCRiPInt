#!/usr/local/bin/perl
# Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
# Global Graphics Software Ltd. Confidential Information.
#
# $HopeName: SWtools!mksw.pl(EBDSDK_P.1) $
#
# This utility is a replacement for the old swroot, which maps from the
# /usr/epsrc copies of all .swf files into a SW folder for a given machine.
# This version instead expects that HOPE will have given you all of the swf
# files, and that all it needs to do is massage them into the correct form,
# creating a SW directory wherever it is run.  It is also assumed that HOPE
# will have done the right newline translations already.
#
# usage: mksw -pc|-mac|macos_x|-unix|-fat|-ntfs|-lmrdr [-verbose...] [-warn|-keepgoing]
#        [-exclude swfdir] [-swfdirs] [-plugin plugin] [-outdir directory]
#        [-subset subset]...
#        [-swf swfdir -map mapfile]...
#
# The algorithm, roughly, is:
#    either
#      find all swf sub-directories of current directory
#    or
#      read a list of swf directories from a file
#    read timestamp of last mksw
#    foreach swf directory
#      foreach file in the swf directory
#        check if file is newer than timestamp
#    decide whether to replace, update, or quit, based on -replacesw flag
#    foreach swf directory
#      read swfdir/compound.map
#        enter mappings into filemap, checking for duplicates
#      foreach file in swfdir
#        check if mapping exists
#        add to source name to platform name mapping
#        mark as directory/file
#    remove old SW directory if necessary
#    foreach directory in directory list
#      create directory
#    foreach file in file list
#      copy file to SW/map-dirs/map-name, stripping header
#    write platform file mapping table if needed
#    recursively copy config to factory settings, if possible
#    write new timestamp file
#
# Globals:
#   $verbose is used to test for the -v flag, which causes human-friendly
#      output to be displayed
#   $warn does the same for -w, which reports compatible repeated mappings
#      (incompatible ones are an error)
#   $keepgoing will slowly replace $warn.  It works in a similar fashion, but
#      returns an error at the end of the script if any errors were
#      encountered.  It is meant to work in a similar way to -k for make.
#   $newer indicates if the source tree has changed since mksw last created an
#      SW tree.
#   $replace indicates if the entire source tree should be updated
#   %OSparams attempts to encode the variations between the different OS's,
#      noting whether they need mapping, what the directory separator is,
#      what directory entries are ignorable (eg. . and ..), and so on.
#   %Plat2Swf associates fully-qualified destination (platform-mapped) names
#      with fully-qualified source names.
#   %PlatDirs and %PlatFiles indicate if the destination name is mapped from
#      a file or a directory.
#   %PStoGUI associates Postscript path elements with platform (GUI) names.
#   %Where is used for error reporting in the mapping
#   %FactSets associates the source file with a name relative to the Config
#     directory
#   $SWdir is where the folder should go
#   $plugin is whether we are doing a plugin... otherwise doing a main SW directory
#   $plugname is the name of the plugin, if it is a plugin we are preparing
#   $mapDest is where the mapping information goes, which is different when
#     it is a plugin directory that we are collecting.
# 
##################################################

############ include pltools library #############

local ($toolsdir) = $0; $toolsdir =~ s/[\\\/:]?[^\\\/:]*$//;
$toolsdir = "." if ($toolsdir eq "");
unshift(@INC, $toolsdir, $toolsdir . '/pltools', $toolsdir . ':pltools');
require('errormsg.pl'); # For &Complain, &Fatal and &Quit functions

########### set up global parameters #############

# Turn all buffering off.
select((select(STDOUT), $| = 1)[0]);
select((select(STDERR), $| = 1)[0]);
select((select(STDIN),  $| = 1)[0]);

$; = '\0';                # so strings in $dict{$str,$str2} will be distinct
$replace = $verbose = $warn = $keepgoing = 0;
%exclude = ();                  # list of swf dirs to exclude
%PlatDirs = () ;                # platform path -> directory flag
%PlatFiles = () ;               # platform path -> file flag
%Plat2Swf = () ;                # mapping platform path -> hope path
%Newer = () ;                   # hope path is newer than timestamp
%PStoGUI = () ;                 # mapping PS name -> native GUI name
%GUItoPS = () ;                 # mapping native GUI name -> PS name
%PStoFAT = () ;                 # mapping PS name -> FAT (8.3) name
%FATtoPS = () ;                 # mapping FAT name -> PS name
%Canon = () ;                   # defines canonical case for GUI names
%Where = () ;                   # mapping name -> where defined
%FactSets = () ;                # mapping source name -> factory setting name
$ConfigName = "Config";         # name of config directory
$plugin = 0;                    # whether we are doing a plugin
$plugname = undef;              # name of plugin if we are doing one
@plugexs = ();                  # list of plugin executables to copy
$outdir = undef;                # output directory (if forced)
$oldmap = 0 ;                   # compatibility mode for swf maps
$FacSetName = "Factory Settings"; # name of factory settings directory
%OSparams = ("fat" => {
               "dirSep" => '\\',
               "absPrefix" => '\\',
               "backOne" => '..',
               "thisDir" => '.',
               "skipEnts" => '^\\.\\.?$',
               "mapFile" => 'FILEMAP.PS',
               "mapIndex" => 2,
               "checkInodes" => 0,
               "mapstoFAT" => 1,
               "needsCanon" => 1,
             },
             "ntfs" => {
               "dirSep" => '\\',
               "absPrefix" => '\\', 
               "backOne" => '..', 
               "thisDir" => '.', 
               "skipEnts" => '^\\.\\.?$',
               "mapFile" => 'FILEMAP.PS',
               "mapIndex" => 1,
               "checkInodes" => 0,
               "mapstoFAT" => 1,
               "needsCanon" => 1,
             },
             "lmrdr" => {
               "dirSep" => '/',
               "absPrefix" => '/', 
               "backOne" => '..', 
               "thisDir" => '.', 
               "skipEnts" => '^\\.\\.?$',
               "mapFile" => 'FILEMAP.PS',
               "mapIndex" => 2,
               "checkInodes" => 1,
               "mapstoFAT" => 1,
               "needsCanon" => 1,
             },
             "macos_x" => {
               "dirSep" => '/',
               "absPrefix" => '/',
               "backOne" => '..',
               "thisDir" => '.',
               "skipEnts" => '^\\.\\.?$',
               "mapFile" => 'FILEMAP.PS',
               "mapIndex" => 1,
               "checkInodes" => 0,
               "mapstoFAT" => 0,
               "needsCanon" => 1,
             },
             "mac" => {
               "dirSep" => ':',
               "absPrefix" => '',
               "backOne" => '',
               "thisDir" => ':',
               "skipEnts" => '^\\.\\.?$',
               "mapFile" => 'FILEMAP.PS',
               "mapIndex" => 1,
               "checkInodes" => 0,
               "mapstoFAT" => 0,
               "needsCanon" => 1,
             },
             "unix" => {
               "dirSep" => '/',
               "absPrefix" => '/',
               "backOne" => '..',
               "thisDir" => '.',
               "skipEnts" => '^\\.\\.?$',
               "mapFile" => 'FILEMAP.PS',
               "mapIndex" => 1,
               "checkInodes" => 1,
               "mapstoFAT" => 0,
               "needsCanon" => 0,
             }
            );
# $SWdir isn't defined until we know what OS we're on....
# $mapDest is defined when we know whether we're doing a plugin or not

########### parse arguments off cmd. line #############

%swfDirs = () ; # Hash of map file to directory pathname
undef $mapfile ;
undef $swfdir ;
undef $check ;
$searchswf = 1 ; # Will we search the SWF for files, or rely on the map file?
%subsets = () ;

undef $os;
$replacesw = 'query' ;
while ($#ARGV >= 0) {
  $_ = shift(@ARGV) ;
  if (/^-pc?$/ || /-f(at)?/) {
    $os = 'fat';
  } elsif (/^-n(tfs)?$/) {
    $os = 'ntfs';
  } elsif (/^-lm(rdr)?$/) {
    $os = 'lmrdr';
  } elsif (/^-macos_x?$/) {
    $os = 'macos_x';
  } elsif (/^-m(ac)?$/) {
    $os = 'mac';
  } elsif (/^-u(nix)?$/) {
    $os = 'unix';
  } elsif (/^-v(erbose)?(\d*)$/) {
    $verbose = $2 + 1;
  } elsif (/^-c(heck)?$/) {
    $check = 1;
  } elsif (/^-o(ldmap)?$/) {
    $oldmap = 1;
  } elsif (/^-w(arn)?$/) {      # warns about non-fatal errors
    $warn = 1;
  } elsif (/^-k(eep(going)?)?$/) { # Like -k to make -- warns but returns
    $keepgoing = 1;               # an error upon exiting.
  } elsif (/^-e(xclude)?$/) {   # swf dir to exclude
    &Complain("Obsolete option $_" . ($_ ne "-exclude" ? " (-exclude) " : " ") . "used.\n  Please use -swf and -map pairs instead.\n") ;
    $exclude{ shift(@ARGV) } = 1;
  } elsif (/^-s(wfdirs)?$/) { # reads swf dirs from a file
    &Complain("Using old swf map format because obsolete option $_" . ($_ ne "-swfdirs" ? " (-swfdirs) " : " ") . "used.\n  Please use -swf and -map pairs instead.\n") ;
    $warn = 1;
    $oldmap = 1 ;
    $swflist = 1 ;
  } elsif (/^-map$/) { # Names map file for swf dir
    &Fatal("Missing filename for -map option\n") if @ARGV == 0 ; 
    &Fatal("Unpaired -map option\n") if defined($mapfile) ;
    $searchswf = 0 ; # New-style options. No searching.
    $mapfile = shift(@ARGV) ;
    if ( defined($swfdir) ) {
      $swfDirs{$mapfile} = $swfdir ;
      undef $mapfile ;
      undef $swfdir ;
    }
  } elsif (/^-swf$/) { # Names swf dir for map file
    &Fatal("Missing filename for -swf option\n") if @ARGV == 0 ;
    &Fatal("Unpaired -swf option\n") if defined($swfdir) ;
    $searchswf = 0 ; # New-style options. No searching.
    $swfdir = shift(@ARGV) ;
    if ( defined($mapfile) ) {
      $swfDirs{$mapfile} = $swfdir ;
      undef $mapfile ;
      undef $swfdir ;
    }
  } elsif (/^-plugin$/) {
    &Fatal("Missing name for -plugin option\n") if @ARGV == 0 ;
    $plugin = 1;
    $plugname = shift(@ARGV);
  } elsif (/^-out(dir)?$/) {
    $outdir = shift(@ARGV);
  } elsif (/^-subset$/) {
    $subsets{shift(@ARGV)} = 1 ;
  } elsif (/^-plugex$/) {
    &Fatal("Missing extension for -plugex option\n") if @ARGV == 0 ;
    push(@plugexs, shift(@ARGV));
  } elsif (/^-r(eplacesw)?$/) {
    $replacesw = shift(@ARGV) ;
    if ( $replacesw !~ /^yes$/i && $replacesw !~ /^no$/i &&
         $replacesw !~ /^new$/i && $replacesw !~ /^query$/i &&
         $replacesw !~ /^abort$/i ) {
      &Complain("Option '$replacesw' to -replacesw not recognized\n");
      $badArg = 1 ;
    }
  } else {
    &Complain("Unrecognized argument '$_'\n");
    $badArg = 1;
  }
}

if ($badArg || ! defined($os)) {
   &Fatal("Usage: $0 platform [-verbose] [-warn|-keepgoing]\n"
        . "       [-replacesw yes|no|new|query|abort]\n"
        . "       {-swf dir -map file}\n"
        . "       [-out dir]\n"
        . "       [-subset subset]...\n"
        . "       [-plugin plugname [{-plugex plugex}]]\n"
        . "  platforms are -pc, -fat, -ntfs, -lmrdr, -mac, -macos_x, or -unix\n");
}

&Complain("Obsolete mksw options.\n  Please use -swf and -map pairs.\n")
  if $searchswf ;

&Fatal("Unpaired -map or -swf option\n")
  if defined($mapfile) || defined($swfdir) ;

if ( $os eq "mac" && @plugexs != 0 ) {
   &Fatal("Error, the -plugex parameter may not be used on the Mac, because Perl cannot copy a binary file without breaking it.\n");
}

# Default subsets
$subsets{"normal"} = 1 if scalar(keys %subsets) == 0 ;

#### done with argument parsing #######

# these parameters are used so often, it's worth unpacking them now:
$dirSep = $OSparams{$os}->{'dirSep'} ;
$thisDir = $OSparams{$os}->{'thisDir'} ;
$skipEnts = $OSparams{$os}->{'skipEnts'} ;
$mapIndex = $OSparams{$os}->{'mapIndex'} ;
$mapstoFAT = $OSparams{$os}->{'mapstoFAT'} ;
$checkInodes = $OSparams{$os}->{'checkInodes'} ;

unless ( defined($outdir) ) {
  $outdir = defined($plugname) ? $plugname : "SW" ;
  &Complain("Obsolete mksw options.\n  Please use -outdir option, defaulting to $outdir\n") ;
}
$SWdir = &joinPaths($thisDir, $outdir);

if ($plugin) {
  # set the output directory and mapfile for this plugin
  print "Collecting plugin $plugname\n" if $verbose;

  $mapDest = &joinPaths($SWdir, $plugname . '.map');

  if ( $swflist ) {             # use swf directory supplied on stdin
    &readSWF($plugname);
  } elsif ( scalar keys %swfDirs == 0 ) { # find directories to copy from
    print "Using single swf directory for plugin\n" if $verbose;
    my $dir = &joinPaths($thisDir, 'swf') ;
    $swfDirs{&joinPaths($dir, $plugname . ".map")} = $dir ;
  }
} else {
  $mapDest = &joinPaths($SWdir, $OSparams{$os}->{'mapFile'});
  $mapData = &joinPaths($SWdir, 'FILEMAP.DAT');

  if ( $swflist ) {             # use swf directory supplied on stdin
     &readSWF();
  } elsif ( scalar keys %swfDirs == 0 ) { # find directories to copy from
     print "Searching for SWF dirs\n" if $verbose ;

     # [13-Feb-97 AndrewI] Limit the recursion search to 3 levels, since
     # all swf dirs are currently in the top-level dir of their
     # respective compounds
     &findSWF($thisDir, $thisDir, 3) ; # last arg is max_depth-1
  }
}

######## Exclude directories we have asked to avoid
foreach (keys %swfDirs) {
   delete $swfDirs{$_} if $exclude{$swfDirs{$_}};
}


######### find timestamp of SWF directory

print "Reading timestamp file\n" if $verbose ;
chop($SWdir) if $SWdir =~ /$dirSep$/;
$oldstampfile = &joinPaths($thisDir, "mksw.tim") ;
if (-f $oldstampfile) {
   &Fatal("Error, old-style timestamp $oldstampfile still present -\n"
        . "  aborting for safety's sake; to fix this:\n"
        . "  manually delete $oldstampfile.\n");
}
$stampfile = "${SWdir}.tim" ;
if (-f $stampfile) {
   $timestamp = (stat($stampfile))[9] ; # use modification time
   $newer = 0 ;
} else {                        # no timestamp file
   $timestamp = 0;              # irrelevant, because it's always newer...
   $newer = 1 ;
}

######### build the filemaps... #############
$errors = 0;
foreach $swf (sort keys %swfDirs) {
   local %HopeMap = (); # mapping found name->assoc
   &readMap($swfDirs{$swf}, $swf, $SWdir); # read mapping file, construct mappings and cross-check
}

exit 1 if $errors > 0 ;

if (-d $SWdir) {
   if ($replacesw =~ /^no$/i) {
      if ( $newer ) {
         &Complain("Warning, the following files in $SWdir are out of date:\n"
                 . join("\n ", sort keys %Newer) . "\n"
                 . "not updating\n");
      }
      print "done.\n";          # successful exit
      &myQuit();
   } elsif ($replacesw =~ /^abort$/i) {
      &Fatal("Error, $SWdir already exists - aborting\n");
   } elsif ($replacesw =~ /^query$/i) {
      local($done) = 0 ;
      do {
         if ($newer) {
            print "The following files in $SWdir are out of date:\n ",
             join("\n ", sort keys %Newer),
             "\n(r)eplace whole SW folder, (u)pdate out of date files, or (a)bort)? ";
         } else {
            print "$SWdir exists; (r)eplace whole SW folder or (a)bort? " ;
         }
         if (! length($_ = <STDIN>)) {
            &Fatal("Error, EOF reading standard input\n");
         }
         if ( /^a(bort)?$/ ) {
            &Fatal("okay, aborting...\n");
         } elsif ( /^r(eplace)?$/i ) {
            $done = $replace = 1 ;
         } elsif ( $newer && /^u(pdate)?$/i ) {
            $done = 1 ;
         }
      } while ( ! $done ) ;
   } elsif ($replacesw =~ /^new$/i) {
      if ( $newer ) {
         print "$SWdir is out of date, updating...\n" if $verbose;
      } else {
         if ($plugin)
         {
             goto copyplugbin;
         }
         print "$SWdir is up to date, exiting...\n" if $verbose;
         print "done.\n";       # successful exit if older
         &myQuit();
      }
   } elsif ($replacesw =~ /^yes$/i) {
      $replace = 1 ;            # do the lot...
   } else {
      &Fatal("Confusion, -replacesw $replacesw not recognised\n");
   }
} else {
   $replace = 1 ;               # replace non-existant folder
}

if ( $replace && -d $SWdir ) {
   print "Deleting old $SWdir...\n" if $verbose;
   if ( ! &nukeDir($SWdir) ) {  # nukeDir printed an error msg.
      exit 1 ;
   }
}

print "Making $SWdir for $os\n" if $verbose;

if (! -d $SWdir && ! mkdir ($SWdir, 0755)) {
   &Fatal("Error, cannot make $SWdir: $!\n");
}

######### done mapping; install files... ########
foreach (sort keys %PlatDirs) { # sort ensures prefixes done first
   if (! -d $_ && ! mkdir ($_, 0755)) {
      &Fatal("Error, cannot make $_: $!\n");
   }
}

foreach (keys %PlatFiles) {     # copy all files
   &copyFile($Plat2Swf{$_}, $_) if $replace || $Newer{$Plat2Swf{$_}} ;
}
print "\n" if $verbose ;

###### and output the filemap (all of them combined) for later... ######
if ( defined($OSparams{$os}->{'mapFile'}) ) {
   print "Recording filemap in $mapDest\n" if $verbose;
   if (!open(FILEH, ">$mapDest")) {
      &Fatal("Error, unable to open $mapDest: $!\n");
   }
   print FILEH "/Mappings\n[\n";
   foreach (sort keys %PStoGUI) {
      print FILEH "($_) ($PStoGUI{$_})\n" if $PStoGUI{$_} ne $_ && !/^\s*$/ ;
   }
   print FILEH "]\n";
   close(FILEH);
}

# Output new mapping tables in FILEMAP.DAT if doing SW folder
if ( !$plugin ) {
   print "Recording new-style filemap data in $mapData\n" if $verbose;
   if (!open(FILEH, ">$mapData")) {
      &Fatal("Error, unable to open $mapData: $!\n");
   }

   # PC platforms map to FAT even if SW folder is long name
   if ( $OSparams{$os}->{'mapstoFAT'} ) {
      *table = *PStoFAT;
   } else {
      *table = *PStoGUI;
   }
   foreach (sort keys %table) {
      print FILEH "($_)($table{$_})M\n" if $table{$_} ne $_ && !/^\s*$/ ;
   }
   if ( $OSparams{$os}->{'needsCanon'} ) {
      foreach (sort keys %Canon) {
         print FILEH "($_)($Canon{$_})C\n";
      }
   }
   close(FILEH);
}

# copy (Config) tree into (Config/Factory Settings)
if (defined($config = $PStoGUI{$ConfigName}) &&
    defined($factsets = $PStoGUI{$FacSetName})) {
  my $cfdir = &joinPaths($SWdir, $config) ;

  if ( -d $cfdir ) {
    my $fsdir = &joinPaths($cfdir, $factsets) ;

    print "Copying factory settings to $fsdir\n" if $verbose;

    if (! -d $fsdir && ! mkdir ($fsdir, 0755)) {
      &Fatal("Error, cannot make $fsdir: $!\n");
    }

    foreach $from (sort keys %FactSets) {
      my $to = &joinPaths($fsdir, $FactSets{$from}) ;
      if ( -d $from ) {
	if ( ! -d $to && ! mkdir ($to, 0755)) {
	  &Fatal("Error, cannot make $to: $!\n");
	}
      } elsif ( -f $from ) {
	&copyFile($from, $to) if $replace || $Newer{$from} ;
      } else {
	&Fatal("Confusion, $from is neither a file nor a directory\n");
      }
    }
    print "\n" if $verbose;
  }
}

copyplugbin:
{
  if ($plugin) {
    if (@plugexs != 0) {
      print "Copying plugin executables\n" if $verbose;
      foreach $plugex (@plugexs) {
        if ($os eq "mac") {
          &Fatal("Error, the -plugex parameter may not be used on\n"
                 . "  the Mac, because Perl cannot copy a binary file\n"
                 . "  without breaking it.\n");
        }
        chop($plugex) if ($plugex =~ /$dirSep$/);
        my $quoted = $dirSep ;
        $quoted =~ s/([\\\.\$\*\?\+\(\)\[\]])/\\$1/g ; # quote dir separator
        my @nameparts = split($quoted, $plugex);
        my $execname = &joinPaths($SWdir, pop(@nameparts));
        if ( ! -e $execname ||
             (stat($plugex))[9] > (stat($execname))[9] ) {
          print "Copying executable $plugex into $execname\n" if $verbose;
          &copyFileBinary($plugex, $execname);
        } else {
          print "Plugin executable $plugex is up to date\n" if $verbose;
        }
      }
    }
  } else {
    &Warning("Must not give -plugex without -plugin") if @plugexs != 0 ;
  }
}

# finally update timestamp file
unlink($stampfile) ;
if ( open(STAMP, ">$stampfile") ) {
  print "Writing timestamp file $stampfile\n" if $verbose;
  my ($sec, $min, $hr, $mdy, $mon, $year, $wdy, $ydy, $dst) = localtime(time) ;
  printf STAMP "mksw built at %d:%02d:%02d on %d/%d/%d\n",
    $hr, $min, $sec, $year + 1900, $mon, $mdy ;
  close(STAMP);
} else {
  &Warning("can't write timestamp file");
}

print "done.\n";
&myQuit();

##########################################################################
#                     Subroutines only below here                        #
##########################################################################

##############################
# nukeDir is a simple directory delete function (recursive).  It takes one
#    argument (the dirname) and returns 1 for success, other for failure.
#    It also prints its own failure messages.
sub nukeDir {
   local($killme) = @_;
   local(@entries, $entry, $stat);

   opendir (DIRH, $killme);
   @entries = grep(! /$skipEnts/, readdir(DIRH));
   closedir (DIRH);
   foreach (@entries) {
      $entry = &joinPaths($killme, $_);
      if (-d $entry) {
         $stat = &nukeDir($entry);
      } else {
         $stat = unlink ($entry);
      }
      if ($stat != 1) {
         &Complain("Error, can't unlink $entry: $!\n") if $stat;
         return 0;
      }
   }
   $stat = rmdir($killme);
   &Complain("Error, can't rmdir $killme: $!\n") unless $stat==1;
   return $stat;
}

##############################
# joinPaths is a simple pathname concatenation function.  It takes two 
#    arguments -- an absolute or relative first pathname and a relative 
#    second pathname, and returns the composed pathname.  
#    It copes with the Mac's sloppy pathname syntax, where the pathname of 
#    a directory may or may not end with a colon, and where a relative 
#    pathname either begins with a colon or contains no colons.  
sub joinPaths {     # input is path1, path2
  my ($p1, @rest) = @_ ;

  chop($p1) if ($p1 =~ /$dirSep$/);  # remove trailing dirSep

  foreach $p2 (@rest) {
    if ($os eq "mac") {                 # Mac relative paths start with colon (remove it before joining) ...
      if ($p2 =~ /^:/) {
        $p2 = $'; # quote for emacs perl-mode: '
      }
      else {                           # ... or contain no colons (confirm there are no colons).
        if ($p2 =~ /:/) {
          &Complain("Error, joinPaths second pathname must be relative, was $p2\n");
        }
      }
    }
  }
   
  return join($dirSep, $p1, grep(defined($_), @rest)) ;
}

########################################
# Find swf directories, and insert into an associative array of swf
# directory->compound name. Only used if no swf directory list is passed in.
sub findSWF {
  my ($root, $base, $recurse) = @_ ; # root directory, basename of this dir
  if ( $checkInodes ) {
    my $here = $root ;
    while ( -l $here ) {
      $here = readlink($here) ;
    }
    my $inode = (stat($here))[1] ;
    return if $seen_inodes{$inode}++ ;
  }
  if (opendir(ROOT, $root)) {
    my @entries = grep(!/$skipEnts/o && -d &joinPaths($root, $_), readdir(ROOT)) ;
    closedir(ROOT) ;
    map {
      if ( /^swf$/i ) {
        my $dir = &joinPaths($root, $_);
        print STDERR "  found $dir\n" if $verbose;
        $swfDirs{&joinPaths($dir, $base . ".map")} = $dir ;
      } elsif ( $recurse && !/^(SW|obj|src|make|export)$/i ) {
        &findSWF(&joinPaths($root, $_), $_, $recurse - 1) ;
      }
    } sort @entries ;
  } else {
    &Warning("can't open directory $root");
  }
}

########################################
# Read mapping file from a swf directory, and cross-check mappings if possible
sub readMap {
  my ($swfdir, $mapname, $outdir) = @_ ;

  if (open(MAPH, $mapname)) {
    my @hopenames = () ;

    print "Reading mapping file $mapname\n" if $verbose;

    local %basedirs = (); # mapping swf dir basename->directories
    &oldMap($swfdir) if $oldmap ;

    while (<MAPH>) {
      next if /^\s*(%.*)?$/;    # skip blanks and comments

      my @mappings = &paren_token($_, $mapname) ;
      my $hopename = $mappings[0] ;
      my @directory = split('/', $hopename) ; # / for emacs perl-mode
      my $basename = pop(@directory) ;
      my $PSname = $mappings[1] ;

      # mappings containing "?" generate a unique ID for the ? string
      if ( $mappings[2] =~ /\?/ ) {
        $mappings[2] =~ s/(\?+)/&generateid($PSname, length($1))/ge ;
      }

      my $FATname = $mappings[2] ;

      if ( $FATname eq "" ) {
        # create default PC mapping if none specified
        $FATname = $basename ;
      } elsif ( $FATname !~ /^([-A-Z0-9_]{1,8})(\.[-A-Z0-9_]{1,3})?$/ ) {
        &DelayedWarning("Invalid FAT name $FATname for PS name $PSname in\n"
                        . "  $mapname, line $.") ;
      }

      if ( $FATname =~ /^(.*)\.([^.]+)$/ ) {
        $FATname = &shorten($1, 8) . "." . &shorten($2, 3) ;
      } else {
        $FATname = &shorten($FATname, 8) ;
      }

      if ( $mappings[2] ne "" && $FATname ne $mappings[2] ) {
        &Complain("Invalid FAT name $mappings[2] converted to $FATname in\n"
                . "  $mapname, line $.\n") ;
      }
      $mappings[2] = $FATname ;

      # Check validity of PS names; disallow non-ascii chars
      # (not allowed as GUI names on Windows, and would mess up
      # canonical case definitions below)
      if ( $PSname =~ /[\200-\377]/ ) {
        &DelayedError("non-ascii characters in PS name $PSname\n"
                      . "  in $mapname, at line $.");
      }

      # PS names must be at most 31 chars long; any longer and
      # they require mapping on the Mac.
      if ( length($PSname) > 31 ) {
        &DelayedError("more than 31 chars in PS name $PSname\n"
                      . "  in $mapname, at line $.");
      }

      if ( $PSname =~ /[A-Za-z]/ ) {
        # Define $PSname as canonical; error if case conflict
        my $StdName = "\U$PSname\E";
        if ( defined($Canon{$StdName}) &&
             $Canon{$StdName} ne $PSname ) {
          &DelayedError("case conflict for PS name $PSname\n"
                        . "  $Canon{$StdName} in $Where{$Canon{$StdName}}, "
                        . "  $PSname in $mapname, line $.");
        } else {
          $Canon{$StdName} = $PSname ;
        }
      }

      # $GUIname will be the filename in SW folder; for -fat
      # this will be $FATname, otherwise $PSname itself.
      my $GUIname = $mappings[$mapIndex];

      # Check and define mappings for SW folder
      if ( defined($PStoGUI{$PSname}) &&
           $PStoGUI{$PSname} ne $GUIname ) {
        &DelayedError("incompatible mappings for PS name $PSname\n"
                      . "  $PStoGUI{$PSname} in $Where{$PSname}, $GUIname in "
                      . "  $mapname, line $.");
      } elsif ( defined($GUItoPS{$GUIname}) &&
                $GUItoPS{$GUIname} ne $PSname ) {
        &DelayedError("incompatible mappings for GUI name $GUIname\n"
                      . "  $GUItoPS{$GUIname} in $Where{$GUItoPS{$GUIname}}, "
                      . "  $PSname in $mapname, line $.");
      } else {
        $PStoGUI{$PSname} = $GUIname ;
        $GUItoPS{$GUIname} = $PSname ;
      }

      # Check and define mappings for FAT drives.
      if ( defined($PStoFAT{$PSname}) &&
           $PStoFAT{$PSname} ne $FATname ) {
        &DelayedError("incompatible FAT mappings for PS name $PSname\n"
                      . "  $PStoFAT{$PSname} in $Where{$PSname}, $FATname in "
                      . "  $mapname, line $.");
      } elsif ( defined($FATtoPS{$FATname}) &&
                $FATtoPS{$FATname} ne $PSname ) {
        &DelayedError("incompatible mappings for FAT name $FATname\n"
                      . "  $FATtoPS{$FATname} in $Where{$FATtoPS{$FATname}}, "
                      . "  $PSname in $mapname, line $.");
      } else {
        $PStoFAT{$PSname} = $FATname ;
        $FATtoPS{$FATname} = $PSname ;
      }

      # Remember first location of this triage of definitions
      if ( !defined($Where{$PSname}) ) {
        $Where{$PSname} = "$mapname, at line $." ;
      }

      # Test if file is in the selected subsets.
      my $insubset = 0 ;
      push(@mappings, "normal") if @mappings <= 3 ;
      while ( @mappings > 3 ) {
        $insubset = 1, last if $subsets{pop(@mappings)} ;
      }

      if ( $insubset && $basename ne "" ) { # hope file/directory should exist
        if ( !$oldmap ) {
          if ( defined($HopeMap{$hopename}) ) {
            if ( $HopeMap{$hopename}->{PSname} ne $PSname ||
                 $HopeMap{$hopename}->{GUIname} ne $GUIname ) {
              &DelayedError("file $hopename already has an incompatible mapping in $Where{$PSname},\n"
                            . "  $PSname in $mapname, line $.");
            } else {
              &DelayedWarning("file $hopename already has a mapping in $Where{$PSname},\n"
                            . "  $PSname in $mapname, line $.");
            }
          } else {
            push(@hopenames, $hopename) ;

            $HopeMap{$hopename}->{basename} = $basename ;
            $HopeMap{$hopename}->{PSname} = $PSname ;
            $HopeMap{$hopename}->{GUIname} = $GUIname ;
            $HopeMap{$hopename}->{path} = [ @directory ] ;
          }
        } else {
          foreach ( @{$basedirs{$basename}} ) {
            @directory = @{$_} ;
            $hopename = join("/", @directory, $basename) ;

            if ( defined($HopeMap{$hopename}) ) {
              if ( $HopeMap{$hopename}->{PSname} ne $PSname ||
                   $HopeMap{$hopename}->{GUIname} ne $GUIname ) {
                &DelayedError("file $hopename already has an incompatible mapping in $Where{$PSname},\n"
                              . "  $PSname in $mapname, line $.");
              } else {
                &DelayedWarning("file $hopename already has a mapping in $Where{$PSname},\n"
                                . "  $PSname in $mapname, line $.");
              }
            } else {
              push(@hopenames, $hopename) ;

              $HopeMap{$hopename}->{basename} = $basename ;
              $HopeMap{$hopename}->{PSname} = $PSname ;
              $HopeMap{$hopename}->{GUIname} = $GUIname ;
              $HopeMap{$hopename}->{path} = [ @directory ] ;
            }
          }
        }
      }
    }
    close(MAPH);

    foreach my $hopename ( sort @hopenames ) {
      my @directory = @{$HopeMap{$hopename}->{path}} ;
      my $basename = $HopeMap{$hopename}->{basename} ;
      my $PSname = $HopeMap{$hopename}->{PSname} ;
      my $GUIname = $HopeMap{$hopename}->{GUIname} ;

      my $from = &joinPaths($swfdir, @directory, $basename) ;

      # If there is a parent directory, we should have seen the parent
      if ( @directory != 0 ) {
        my $previous = join('/', @directory) ;

        unless ( defined($HopeMap{$previous}) ) {
          &DelayedError("parent folder for $hopename is not defined in $mapname");
        }

        $HopeMap{$hopename}->{GUIpath} = &joinPaths($HopeMap{$previous}->{GUIpath}, $GUIname) ;
        if ( $HopeMap{$previous}->{PSname} eq $ConfigName ||
             defined($HopeMap{$previous}->{config}) ) {
          $FactSets{$from} = $HopeMap{$hopename}->{config} =
            &joinPaths(grep($_ ne "", $HopeMap{$previous}->{config}, $GUIname)) ;
        }
      } else {
        $HopeMap{$hopename}->{GUIpath} = $GUIname ;
      }

      my $to = &joinPaths($outdir, $HopeMap{$hopename}->{GUIpath}) ;
      if ( -d $from ) { # current element is a directory
        if ( $PlatFiles{$to} ) {
          &DelayedError("file $Plat2Swf{$to} already mapped to file $to");
        } else {
          $PlatDirs{$to} = 1 ; # yes, it's a directory
          $Plat2Swf{$to} = $from ;
        }
      } elsif ( -f $from ) { # current element is a file
        $newer = $Newer{$from} = 1 if (stat(_))[9] > $timestamp ;

        if ( $PlatFiles{$to} ) {
          &DelayedError("file $Plat2Swf{$to} already mapped to file $to");
        } elsif ( $PlatDirs{$to} ) {
          &DelayedError("directory $Plat2Swf{$to} already mapped to $to");
        } else {
          $PlatFiles{$to} = 1 ; # yes, it's a file
          $Plat2Swf{$to} = $from ;
        }
      } else {
        &DelayedWarning("no file for mapping $from -> $PSname");
      }
    }

    &checkSWF($swfdir, $mapname) if $check ;
  } else {
    &DelayedError("cannot open swf map file $mapname");
  }
}

########################################
# Shorten a filename component to a specified length, trying to preserve enough
# of its essential nature to make it unique.
sub shorten {
  my ($name, $maxlen) = @_ ;

  if ( length($name) > $maxlen ) {
    my %parts ;
    my $total = 0 ;
    my $index = 0 ;

    foreach ( split(/[^A-Za-z0-9]+/, $name) ) {
      map { # split at boundaries of digits, capitalised
        if ( $_ ne "" ) {
          $total += length($_) ;
          $parts{$index++} = $_ if $index < $maxlen ;
        }
      } split /(\d+|[A-Z][a-z]+)/ ;
    }

    while ( $total > $maxlen ) {
      # Shorten all parts by incrementally adjusted ratio of total to maxlen
      map {
        my $oldlength = length($parts{$_}) ;
        if ( $oldlength > 1 ) {
          my $newlength = int($oldlength * $maxlen / $total) ;
          my $start = 0 ;
          $total -= $oldlength ;
          if ( $parts{$_} =~ /^\d+$/ ) { # special rules for digits
            if ( $parts{$_} =~ /^(19|20)\d\d$/ ) { # looks like a date
              $newlength = $oldlength - 2 ;
              $start = 2 ;
            } else {
              # Shrink by one at a time; digits are usually important
              $newlength = $oldlength - 1 ;
            }
          }
          $parts{$_} = substr($parts{$_}, $start, $newlength) ;
          $total += $newlength ;
          last if $total <= $maxlen ;
        }
      } sort { # Longest to shortest part, part order for same
        length($parts{$b}) <=> length($parts{$a}) or $a <=> $b ;
      } keys %parts ;
    }

    $name = join("", map { $parts{$_} } sort keys %parts) ;
  }

  # Return uppercase name
  $name =~ tr/a-z/A-Z/ ;
  return $name ;
}

########################################
# Search swf directory for files. This is only used for the obsolete old-style
# map files, until they are converted to new-style SWF rules in Jam files.
sub oldMap {
  my ($dir, @prefix) = @_ ;
  my $prefix = [ @prefix ] ;
  if ( opendir(DIRH, $dir) ) {
    my @entries = grep(! /$skipEnts/, readdir(DIRH));
    closedir(DIRH);
    foreach ( @entries ) {
      my $filename = &joinPaths($dir, $_) ;
      push(@{$basedirs{$_}}, $prefix) ;
      &oldMap($filename, @prefix, $_) if -d $filename ;
    }
  } else {
    &Warning("can't open directory $dir");
  }
}

########################################
# Generate a unique ID based on the characters in a name
sub generateid {
  my ($name, $max) = @_ ;
  my $id = 0 ;

  $max = 10 ** $max ;

  foreach (unpack("C*", $name)) {
    $id = ($id * 37) + $_ ;
    $id = ($id % $max) + int($id / $max) ; # Restrict to 7 digits
  }

  return $id ;
}

########################################
# Check swf directories for extra files not mapped
sub checkSWF {
  my ($root, $mapname, @prefix) = @_ ; # root directory, basename of this dir
  if (opendir(ROOT, $root)) {
    my @entries = grep(!/$skipEnts/o && !/^_ignore$/ && !/~$/ && !/\.bak$/ && &joinPaths($root, $_) ne $mapname, readdir(ROOT)) ;
    closedir(ROOT) ;
    map {
      my $filename = &joinPaths($root, $_) ;
      my $hopename = join("/", @prefix, $_) ;
      &DelayedWarning("no mapping for file $filename")
        if !defined($HopeMap{$hopename}) ;
      push(@prefix, $_) ;
      &checkSWF($filename, $mapname, @prefix) if -d $filename ;
      pop(@prefix);
    } sort @entries ;
  }
}

##########################
# readSWF reads a list of SWF directory paths from stdin. If an argument is
# supplied, it expects to find a single SW directory, and sets the map name
# of that directory to the argument specified. If no argument is supplied, it
# infers the stemname of the mapfile to be found in that directory by
# extracting the penultimate pathname segment (that which immediately precedes
# the final "swf" segment, so ":coregui:swf:" should contain a mapfile called
# "coregui.map").  
sub readSWF {
  my ($mapnamestem) = @_ ;
  if ( $plugin ) {
    unless ( defined($mapnamestem) ) {
      &Fatal("Error, readSWF requires a mapname for plugins\n");
    }
  } else {
    if ( defined($mapnamestem) ) {
      &Fatal("Error, readSWF should not be supplied a mapname unless being\n"
             . "used for plugins\n");
    }
  }
  print "Reading SWF list\n" if $verbose ;
  my $quoted = $dirSep ;
  $quoted =~ s/([\\\.\$\*\?\+\(\)\[\]])/\\$1/g ; # quote dir separator
  while ( <> ) {
    chop;                       # remove newline
    next if /^\s*$/ ;           # discard empty lines
    foreach (split(/\s+/)) {    # Multiple swf files per line OK
      my @parts = split($quoted, $_) ;
      pop(@parts) while ( @parts && $parts[$#parts] eq "" ) ;
      next if $parts[$#parts] !~ /^swf$|^swf-.+$/i ;
        
      my $dir = join($dirSep, @parts) ;
      my $base = defined($mapnamestem) ? $mapnamestem : $parts[$#parts-1] ;
      $swfDirs{&joinPaths($dir, $base . ".map")} = $dir ;
    }
  }
}

###########################
# copyFile copies the contents of a file from one place to another, stripping
#   out changelogs, changing %%Creator comments, /TargetID and /ProfileID names and,
#   skipping %%For lines. Perl can quite happily handle binary files in the method used 
#   to copy the file so binary files don't need to be treated specially. binmode is set
#   on the files just to make sure.
sub copyFile {
  my ($from, $to) = @_ ;

  if (! open(SRC, $from)) {
    &Fatal("Error, can't open $from to copy\n");
  }
  if (! open(DST, ">$to")) {
    close(SRC);
    &Fatal("Error, can't open $to to write\n");
  }

  # Set binmode allows us to copy the file directly not worrying
  # if its binary or not. Perl 4 or greater should not care. 
  binmode(SRC) ;
  binmode(DST) ;

  my $lines = 0;
  my $strip = 0;
  while ( <SRC> ) {
    if ( $strip ) {
      next if /^\s*%/ ;
      $strip = 0 ;
    }

    if ( /^%%Creators?:/ && 
         ! /^%%Creator: Apple Software Engineering/ ) {
      # Replace developers names in the %%Creator lines with a generic
      # Harlequin company name, making sure we preserve line endings.
      # Initial test also makes sure we skipped Apples prep files.

      print "In $to:\n replacing: $_" if $verbose > 1 ;
      s/^[^\r\n]+/%%Creator: Global Graphics Software Limited/;
      print " with: $_" if $verbose > 1 ;
    } elsif ( /^%%For:/ ) {
      # %%Creator names often have %%For: Harlequin so strip that out
      # by skipping it.

      print "In $to Skipping $_\n" if $verbose > 1;
      next ;
    } elsif ( /\/(Target|Profile)ID\s\(\/[\w]+\/[\w]+\// ) {
      # Strip machine and developer e-mail addresses from
      # TargetID and ProfileID.

      print "In $to:\n replacing: $_" if $verbose > 1;
      s/(\/(Target|Profile)ID\s\(\/)[\w]+\/[\w]+\//$1/;
      print " with: $_" if $verbose > 1;
    } elsif ( /Harlequin (Group|Ltd)/ ){
      # Catch all files with old copyrights in them. Creator comes first in
      # this if else list so it will be dealt with above.
      print "In $to:\n replacing: $_" if $verbose > 1 ;
      s/(Harlequin Group,? plc|The Harlequin Group Limited|Harlequin Ltd\.?)/Global Graphics Software Limited/;
      print " with: $_" if $verbose > 1 ;
    } elsif  ( /^\s*%.*\$(Log|Id|HopeName|Author|Date).*\$/ ) {
      $strip = 1 ;
      next ;
    }

    &Complain("Assert, strip should not be 1 here\n") if ($strip) ;

    # Now print out to the destination
    print DST $_ ;
    $lines++ ;
  }

  if ( $lines == 0 && $strip) {
    # If we were stripping a hope comment and it ended up not finding
    # a matching end while trying to copy it, in which case nothing will get written
    # out. Trap this condition, and do it as a binary file. This does mean
    # that the log comments will be left in for such files. Warn the user
    # about this.
    # In practice this code appears not to be triggered and is a candidate
    # for long term removal. Its being left in here for safety reasons and
    # compatibility with what went before.
    &Complain("Warning, hope comments not stripped from $from, file is binary\n");

    my $len, $buf, $offset, $written ;
    if (! close(SRC) || ! open(SRC, $from)) {
      &Fatal("Error, can't open $from to copy\n");
    }
    if (! close(DST) || ! open(DST, "> $to")) {
      close(SRC);
      &Fatal("Error, can't open $to to write\n");
    }
    binmode(SRC) ;
    binmode(DST) ;
    # read/write; watch out for PC's CTRL-Z translation
     # copied from camel book p.192
    while ($len = sysread(SRC, $buf, 4096)) {
      if ( ! defined($len) ) {
        next if $! =~ /^Interrupted/;
        &Fatal("Error reading from $from\n");
      }
      $offset = 0 ;
      while ($len) {
        $written = syswrite(DST, $buf, $len, $offset) ;
        if ( !defined($written) ) {
          &Fatal("Error writing to $to\n");
        }
        $len -= $written ;
        $offset += $written ;
      }
    }
    if ( $verbose > 1 ) {
      print "Copied binary file $to\n";
    } elsif ( $verbose ) {
      print "+" ;
    }
  } else {
    if ( $verbose > 1 ) {
      print "Stripped text file $to\n";
    } elsif ( $verbose ) {
      print "." ;
    }
  }
  close(SRC);
  close(DST);
}

###########################
# copyFileBinary makes a copy of a known binary file, eg a plugin executable
#   Perl can quite happily handle binary files in the method used 
#   to copy the file so binary files don't need to be treated specially. binmode is set
#   on the files just to make sure.
sub copyFileBinary {
   local($from, $to) = @_ ;

   if (! open(SRC, $from)) {
      &Fatal("Error, can't open $from to copy\n");
   }
   if (! open(DST, ">$to")) {
      close(SRC);
      &Fatal("Error, can't open $to to write\n");
   }

   # Set binmode allows us to copy the file directly not worrying
   # if its binary or not. Perl 4 or greater should not care. 
   binmode(SRC) ;
   binmode(DST) ;

   while ( <SRC> ) {
      print DST $_ ;
   }

   close(SRC);
   close(DST);
}

###########################
# paren_token takes a string and returns an array which contains
#   the "parenthesized tokens" from the string; i.e. each element
#   in the array contains the characters which were between balanced
#   sets of parentheses.
#
sub paren_token {
  my ($String, $mapname) = @_;
  my $pcount = 0, @tokens = () ;

  my $token = "" ;
  foreach (split(/([()])/, $String)) {
    if ( $_ eq '(' ) {
      $token .= $_ if $pcount++ > 0 ;
    } elsif ( $pcount && $_ eq ')' ) {
      if ( --$pcount == 0 ) {
        push(@tokens, $token) ;
        $token = "" ;
      } else {
        $token .= $_ ;
      }
    } elsif ( $pcount ) {
      $token .= $_ ;
    } elsif ( /^\s*%/ ) {
      last ;
    } elsif ( /\S/ ) {
      &DelayedError("non-space character between tokens in $mapname at line $.");
    }
  }

  &DelayedError("unfinished mapping in $mapname at line $.")
    if $token ne "" ;

  return @tokens ;
}


sub Warning {
    local ($msg) = @_;

    if ($warn || $keepgoing) {
        &Complain("Warning, $msg; continuing\n");
    }
    else {
        &Fatal("Error, $msg; aborting\n");
    }
}

sub DelayedError {
    local ($msg) = @_;

    &Complain("Error, $msg\n");
    $errors++;
}

sub DelayedWarning {
    local ($msg) = @_;

    if ($warn || $keepgoing) {
        &Complain("Warning, $msg\n");
    }
    else {
        &DelayedError($msg);
    }
}

sub myQuit{
    if ($keepgoing) {
        &Quit();
    }
    else {
        exit 0;
    }
}

##################################################
# Log stripped
