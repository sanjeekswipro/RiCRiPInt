#!/usr/bin/perl
# Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
# Global Graphics Software Ltd. Confidential Information.
#
# Script accompanying the Substitute JAM rule to replace one string with
# another in a text file.
#
# Options are described below:
#
# $HopeName: HQNbuildtools!subst.pl(EBDSDK_P.1) $
#

#our($opt_t, $opt_f, $opt_h, $opt_i);

use Getopt::Long;

my $searchfor;
my $replacewith;
my $inplace = 0;
my $help = 0;
my $verbose = 0;
my $extension = '.orig';

Getopt::Long::Configure(
                        "bundling",       # eg -vax == -v -a -x
                        "no_ignore_case", # We want case-sensitive options
                       );

sub HelpMessage {
  print "subst.pl -s <from> -s <to> [-hvi] [fromfile tofile]|[file1, ...]\n";
  print "  -s <from>    set the string that is searched for\n";
  print "  -r <to>      set the string to replace the search string with\n";
  print "  -i           inplace edit of the files\n";
  print "  -h           print this help text\n\n";
  print "  -v           verbose - print filenames as theyre done.\n";
  print "If inplace edit is enabled, each file is processed separately,\n";
  print "using a temp file with the same path and extension .orig. If\n";
  print "inplace edit is not enabled, there must be exactly two file\n";
  print "paths on the command line, in the order 'fromfile tofile'.\n";
  exit 0;
}

sub doSubst {
  my($INFILE, $OUTFILE) = @_;

  while ($ln = <$INFILE>) {
    $ln =~ s/$searchfor/$replacewith/go;
  } continue {
    print $OUTFILE $ln  or die("Cannot write to output file.\n");;
  }
}


GetOptions("s|search=s"  => \$searchfor,
           "r|replace=s" => \$replacewith,
           "i|inplace"   => \$inplace,
           "v|verbose"   => \$verbose,
           "h|help"      => sub { HelpMessage(); } );

# print "$#ARGV args: {". join(',', @ARGV)."}";

if ( $searchfor eq "" || $replacewith eq "" ) {
  print STDERR "Cannot substitute without both search and replace strings, given s/$searchfor/$replacewith/\n";
  exit 1;
}

if (! $inplace ) {
  if ($#ARGV == 0 or $#ARGV > 2 ) {
    print STDERR "Unless inplace edit is enabled, there must be either zero or two file arguments\n";
    exit 1;
  }
}


if ($#ARGV >= 0) {
  if ( ! $inplace ) {
    print STDERR "Processing $ARGV[0] to $ARGV[1]..." if ($verbose);

    open OUTFILE, ">$ARGV[1]" or die("Cannot open output $ARGV[1]");
    open INFILE, "<$ARGV[0]" or die("Cannot open input $ARGV[0]");;
    doSubst(\*INFILE, \*OUTFILE);
    close(INFILE);
    close(OUTFILE);
    print STDERR "Done.\n" if ($verbose);
  } else {
    foreach $file (@ARGV) {
      if ( $inplace ) {
        $backup = $file . $extension;

        print STDERR "Processing $file inplace (via $backup)..." if ($verbose);

        rename $file, $backup or die("Cannot rename input file as backup $backup");
        open OUTFILE, ">$file" or die("Cannot open output $file");
        open INFILE, "<$backup" or die("Cannot open input $backup");
        doSubst(\*INFILE, \*OUTFILE);
        close(INFILE);
        close(OUTFILE);
        unlink($backup) or die("Cannot delete backup file $backup");
        print STDERR "Done.\n" if ($verbose);
      }
    }
  }
} else {
  print STDERR "Processing stdin to stdout..." if ($verbose);
  doSubst(\*STDIN, \*STDOUT);
  print STDERR "Done.\n" if ($verbose);
}

#
# Log stripped
