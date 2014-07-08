#!/usr/bin/perl
# $HopeName: SWprod_hqnrip!testsrc:probegraph.pl(EBDSDK_P.1) $
#
# Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
#
# Perl file to analyse output of Harlequin RIP probe log files. These files
# are output when the probe option is used, and contain detailed tracing
# information showing internal state of the RIP.

use FindBin ;
use lib "$FindBin::Bin/perl5libs" ;
use PDF::Writer ;
use POSIX qw(ceil floor strftime) ;

# Page size defaults to letter
my $paperwidth = 612 ;
my $paperheight = 792 ;

# Margins for paper handling and notes
my $xtrans = 30 ;
my $ytrans = 30 ;

# Default to landscape
my $pagewidth = $paperheight ;
my $pageheight = $paperwidth ;

my $pages = 0 ;
my $window = 0 ;
my $showdata = 0 ;
my $tracecontrol = 0 ;
my $first = undef ;
my $last = undef ;
my $pdffile = "probegraph.pdf" ;

my %ignore = ("THREAD" => 1, "CORE" => 1) ; # Lookup from probe name to trace flag
my %affinity = ("PROBE" => -1) ; # Lookup from event Id->thread for thread affinity
my %timeline = () ; # Lookup from event Id to flag: designator used for nesting?
my %donottrace = ("PROBE" => 1) ; # Lookup from event Id to trace flag?
my %showdata = () ; # Show designator for {Id}{Type}

my %papersizes = (# ISO paper sizes
		  "a4" => [595, 842],  "A4" => [595, 842],
		  "a3" => [842, 1191], "A3" => [842, 1191],
		  "a2" => [1191, 1684], "A2" => [1191, 1684],
		  "a1" => [1684, 2384], "A1" => [1684, 2384],
		  "a0" => [2384, 3370], "A0" => [2384, 3370],
                  # US paper sizes
		  "A" => [612, 792],   "letter" => [612, 792],
		  "legal" => [612, 1008],
		  "B" => [792, 1224],  "ledger" => [792, 1224],
		  ) ;

# Lookup from timeline type to mark. First value is vertical offset from the
# timeline centre, as a proportion of the timeline height, in the range
# -1..+1. Second coordinate is an offset from the horizontal position, in
# points.
my %tlmark = (
  "TITLE" => [0,0,"m", 1,0,"l", 1,1,"l", 1/3,-1,"m", 1/3,1,"l"],
  "ENDING" => [-1,1,"m", -1,0,"l", 1,0,"l", 1,1,"l", 0,0,"m", 0,1,"l"],
  "ABORTING" => [-1,-1,"m", 1,1,"l", -1,1,"m", 1,-1,"l"],
  "EXTEND" => [-1,0,"m", 1,0,"l", -1,0,"m", 1,0,"l"],
  "PROGRESS" => [0,0,"m", -1,0,"l", -3/4,1,"l", -1/2,0,"l"]
  ) ;

while (@ARGV) {
  $_ = shift(@ARGV) ;
  if ( /^-(\d+)$/ ) {
    usage() if $1 <= 0 || $window > 0 ;
    $pages = $1 + 0 ;
  } elsif ( /^-([A-Z][A-Za-z0-9_]+)$/ ) { # Ignore probe: Initial caps reserved
    $ignore{$1} = 1 ;
  } elsif ( /^\+([A-Z][A-Za-z0-9_]+)$/ ) { # Show probe: Initial caps reserved
    $ignore{$1} = 0 ;
  } elsif ( /^([A-Z][A-Za-z0-9_]+)=(\d+)$/ ) { # Probe options: Initial caps reserved
    &option($1, $2, -1) ;
  } elsif ( /^-d(ata)?$/ ) {
    $showdata = 1 ;
  } elsif ( /^-t(race)?$/ ) {
    $tracecontrol = 1 ;
  } elsif ( /^-l(andscape)?$/ ) {
    if ( @ARGV > 0 && defined($papersizes{$ARGV[0]}) ) {
      my $paper = shift(@ARGV) ;
      $paperwidth = $papersizes{$paper}->[0] ;
      $paperheight = $papersizes{$paper}->[1] ;
    }
    $pagewidth = $paperheight ;
    $pageheight = $paperwidth ;
  } elsif ( /^-p(ortrait)?$/ ) {
    if ( @ARGV > 0 && defined($papersizes{$ARGV[0]}) ) {
      my $paper = shift(@ARGV) ;
      $paperwidth = $papersizes{$paper}->[0] ;
      $paperheight = $papersizes{$paper}->[1] ;
    }
    $pagewidth = $paperwidth ;
    $pageheight = $paperheight ;
  } elsif ( /^-s(tart)?$/ ) {
    usage() if @ARGV == 0 ;
    $first = shift(@ARGV) + 0 ;
    usage() if $first < 0 ;
  } elsif ( /^-e(nd)?$/ ) {
    usage() if @ARGV == 0 ;
    $last = shift(@ARGV) + 0 ;
    usage() if $last <= 0 ;
  } elsif ( /^-w(indow)?$/ ) {
    usage() if @ARGV == 0 ;
    $window = shift(@ARGV) + 0 ;
    usage() if $window <= 0 || $pages > 0;
  } elsif ( /^-o(ut)?$/ ) {
    usage() if @ARGV == 0 ;
    $pdffile = shift(@ARGV) ;
  } elsif ( /^-m(erge)?$/ ) {
    usage() if @ARGV == 0 ;
    $affinity{shift(@ARGV)} = -1 ;
  } elsif ( /^-help$/ || /^-\?/ ) { # / perl-mode gets quoting wrong
    usage() ;
  } else {
    unshift(@ARGV, $_) ;
    last ;
  }  
}

push(@ARGV, "clrip.log") if @ARGV == 0 && -f "clrip.log" ;
push(@ARGV, "ebdprobe.log") if @ARGV == 0 && -f "ebdprobe.log" ;
push(@ARGV, "probe.log") if @ARGV == 0 ;

# This is the drawing area
my $width = $pagewidth - 120 ;
my $height = $pageheight - 90 ;

my @events = () ;
my %traceids = () ;
my $ntraceids = 0 ;
my %threaddata = () ;
my $maxsametime = 0 ; # Max number of events that happen at the same time
my %totaltime = () ;

my $precision = 0 ;
my $tickspersec = 0 ;

my $timeformat = "%Y/%m/%d %H:%M:%S %z" ;

my $tagline = "Graph generated on " . strftime($timeformat, localtime(time)) . "." ;
foreach my $file (@ARGV) {
  my @filestat = stat($file) ;
  $tagline .= " Log file $file modified " . strftime($timeformat, localtime($filestat[9])) . "." ;
}

$tagline =~ s/([\\()])/\\\1/g ; # Quote parentheses and backslash, just in case

my $ripsize = 0xffffffff ; # 32 or 64-bit RIP? Assume 32-bit to start.

my $nevents = 0 ;
while ( <> ) {
  s/[\r\n]+$// ;

  # Suck key-value pairs into a hash reference:
  /^\s*/g ; # Solely to set position for \G:
  my $event = { /(?:\G(\w+)=(\S+)\s*)/gc } ;
  if ( (pos() && ! /\G$/g) ||  ! keys(%{$event}) ) {
    print "Line $. invalid: $_\n" ;
    next ;
  }

  $event->{Designator} = hex($event->{Designator}) ;

  if ( defined($affinity{$event->{Id}}) ) {
    $event->{afothread} = $event->{Thread} + 0 ; # Ensure numeric
    $event->{Thread} = $affinity{$event->{Id}} ;
  }
  if ( defined($timeline{$event->{Id}}) ) {
    if ( defined($timeline{$event->{Id}}->{$event->{Designator}}) ) {
      # Set thread to existing event,designator value
      $event->{tlothread} = $event->{Thread} ;
      $event->{Thread} = $timeline{$event->{Id}}->{$event->{Designator}} ;
    } else {
      # Set event,designator lookup to existing thread
      $event->{tlothread} = $event->{Thread} ;
      $timeline{$event->{Id}}->{$event->{Designator}} = $event->{Thread} ;
    }
  }

  $precision = length($1) if $event->{Time} =~ /\.(\d+)/ && length($1) > $precision ;
  $event->{Time} += 0 ; # Ensure numeric
  $event->{Thread} += 0 ; # Ensure numeric

  my $timestamp = $event->{Time} ;
  my $threadid = $event->{Thread} ;
  my $traceid = $event->{Id} ;
  my $tracetype = $event->{Type} ;

  # We still want to sort on thread index, even if filtering out thread
  # events. Set the index on the EXIT event rather than the ENTER event,
  # because we don't want to create a thread object if there are no other
  # events on it.
  $threaddata{$threadid}->{index} = $event->{Designator} if
    $traceid eq "THREAD" && $tracetype eq "EXIT" &&
    defined($threaddata{$threadid}) ;

  next if $ignore{$event->{Id}} ;
  if ( $traceid eq "PROBE" and $tracetype eq "MARK" ) {
    $tickspersec = $tickspersec * 2**32 + $event->{Designator} ;
    next ;
  }

  # Ensure we have a thread object for this ID, and can track timestamps on it
  if ( !defined($threaddata{$threadid}) ) {
    $threaddata{$threadid} = {
      partitions => {}, # Mapping from Id x Type to partition data
      stack => [],
      Time => -1,
      samestamp => 0,
      index => -1,
      trace => [],  # Thread of control slot trace
      tracetime => undef,  # Previous event time
    } ;
  }
  my $thread = $threaddata{$threadid} ;
  my $partitions = $thread->{partitions} ;

  # Note the maximum number of events happening at the same time, so we can
  # work out how much to displace the representation of those events to
  # discriminate them visually.
  if ( $timestamp == $thread->{Time} ) {
    $thread->{Time} = $timestamp ;
    $maxsametime = $thread->{samestamp}
      if ++$thread->{samestamp} > $maxsametime ;
  } else {
    $thread->{Time} = $timestamp ;
    $thread->{samestamp} = 1 ;
  }

  if ( !defined($traceids{$traceid}) ) {
    # Hash of trace IDs seen to ordinal. This is used to derive the color
    $traceids{$traceid} = $ntraceids++ ;
  }

  $event->{eventnum} = ++$nevents ; # event number
  $event->{prev_value} = 0 ;  # current value before event
  $event->{post_value} = 0 ; # current value after event

  # If we're resetting an event type, clear all of the partition data for
  # it, and remove all entries from the stack.
  if ( $tracetype eq "RESET" ) {
    print "NYI RESET $traceid \@ $timestamp on thread $threadid, event $nevents\n" ;
  } elsif ( $tracetype eq "OPTION" ) {
    &option($traceid, $event->{Designator}, $threadid) ;
  } else {
    # Ensure that thread partition data exists.
    my $pkey = &pkey($event) ;
    if ( !defined($partitions->{$pkey}) ) {
      $partitions->{$pkey} = {
	name => (split('/', $pkey))[1],
	id => $traceid,  # same for all with this pkey
	max_value => 0,
	pagemax => 0,
	curr_value => 0,
	prev_value => 0,
	post_value => 0,
	totaltime => 0,
	pagetime => 0,
	slot => 0,
	stack => [],
      } ;
    }

    # Track the max nesting or value of the event
    my $pdata = $partitions->{$pkey} ;

    $event->{partition} = $pdata ;
    $event->{prev_value} = $pdata->{curr_value} ;

    if ( $tracetype eq "ENTER" ) {
      # Push on per-thread, per-Id stack
      $event->{partincr} = 1 ;
      &stackpush($pdata->{stack}, $event) ;
      $pdata->{curr_value} += 1 ;
    } elsif ( $tracetype eq "EXIT" ) {
      if ( my $match = &stackpop($pdata->{stack}, $event) ) {
	# If ending the oldest, accumulate time from the start of the oldest
	# to the start of the next oldest.
	my $oldest = $timestamp ;
	foreach my $save (@{$pdata->{stack}}) {
	  if ( defined($save) ) {
	    $oldest = $save->{Time} if $oldest > $save->{Time} ;
	  }
	}
	if ( $oldest > $match->{Time} ) {
	  $pdata->{totaltime} += $oldest - $match->{Time} ;
	  # Remove wasted time in PROBE from all nested events on this thread
	  my $wasted, $afotdata, $afoparts ;
	  if ( $traceid eq "PROBE"
	       and $tickspersec > 0
	       and defined($event->{afothread})
	       and ($wasted = $event->{Designator} / $tickspersec) > 0
	       and defined($afotdata = $threaddata{$event->{afothread}})
	       and defined($afoparts = $afotdata->{partitions}) ) {
	    while ( my ($akey, $apart) = each %{$afoparts} ) {
	      $apart->{totaltime} -= $wasted if @{$apart->{stack}} > 0 ;
	    }
	  }
	}
	$pdata->{curr_value} -= 1 ;
      } else {
	print "Unmatched EXIT $traceid \@ $timestamp on thread $threadid, event $nevents\n" ;
      }
    } elsif ( $tracetype eq "AMOUNT" || $tracetype eq "VALUE" ) {
      $pdata->{curr_value} = $event->{Designator} ;
      $event->{partincr} = $pdata->{curr_value} ;
    } elsif ( $tracetype eq "ADD" ) {
      # Perl interprets hex() as unsigned, we need the designator as signed
      # here. Perl may not implement the same width of integer as the RIP
      # does, so we somehow need to know how wide the RIP's data was. We can
      # crudely do this by comparing all designators with 0xffffffff. The
      # case we care about most is 64-bit Perl with a 32-bit RIP log. The
      # other way round (32-bit Perl and 64-bit RIP log) is unlikely to
      # happen.
      $ripsize = ~0 if $event->{Designator} > 0xffffffff ;
      if ( $event->{Designator} > ($ripsize >> 1) ) {
	$event->{Designator} = ($ripsize ^ $event->{Designator}) - 1 ;
      }
      $pdata->{curr_value} += $event->{Designator} ;
      $event->{partincr} = $pdata->{curr_value} ;
    } elsif ( defined($tlmark{$tracetype}) ) {
      # Don't change current value for timeline types
    } else { # Usually MARK, but we'll use this for unknown event types too.
      $pdata->{curr_value} = 0 ;
    }

    $event->{post_value} = $pdata->{curr_value} ;

    $pdata->{max_value} = $pdata->{curr_value}
      if $pdata->{curr_value} > $pdata->{max_value} ;

    # global array of event refs
    push(@events, $event) ;
  }
}

# Precision gives the maximum number of decimal places in the logged data. We
# want to set epsilon such that we can disambiguate events that occur at the
# same time; we know the maximum number of conflicting events, so divide the
# time period to allow for that number of events plus one (so that we can
# displace the exit of a module entry/exit by another epsilon and not run
# over the next event).
my $interval = 0.1 ** $precision ; # Simultaneous event displacement
my $epsilon = $interval / ++$maxsametime ; # Simultaneous event displacement

# Work out the X and Y scales of the pages. We split the time line up into
# a set of windows, one for each page. For the thread axis, we work out the
# maximum module nesting depth for the thread, and allocate space for all
# threads proportionately.
$first = $events[0]->{Time} - $interval if !defined($first) ;
$last = $events[$#events]->{Time} + $interval if !defined($last) ;

die "No events in time range $first-$last" if $first >= $last ;

# Translate pages to window size or vice-versa, defaulting to one page
# that shows everything.
if ( $pages > 0 ) {
  $window = ($last - $first) / $pages ;
} elsif ( $window > 0 ) {
  $pages = int(ceil(($last - $first) / $window)) ;
} else {
  $pages = 1 ;
  $window = $last - $first ;
}
my @threadorder = sort {
  $threaddata{$a}->{index} <=> $threaddata{$b}->{index} or $a <=> $b
} keys %threaddata ;
my @partitions = () ;
foreach my $tkey (@threadorder) {
  # Get total number of partitions, and set partition offsets
  my $thread = $threaddata{$tkey} ;
  $thread->{partbase} = scalar(@partitions) ;

  my $partitions = $thread->{partitions} ;
  foreach my $pkey (sort keys %{$partitions}) {
    my $pdata = $partitions->{$pkey} ;

    # Note the slot index for this partition, and push the partition onto
    # an array for easy enter/exit handling.
    $pdata->{slot} = scalar(@partitions) ;
    push(@partitions, $pdata) ;

    # Check that there were no unmatched enter events
    while (@{$pdata->{stack}}) {
      my $event = pop(@{$pdata->{stack}}) ;
      if ( defined($event) ) {
	print "Unmatched ENTER partition $pkey thread $tkey $event->{Id} ($event->{Designator}) \@ $event->{Time} on thread $event->{Thread}, event $event->{eventnum}\n" ;
      } else {
	print "Unshrunk partition $pkey thread $tkey\n" ;
      }
    }
  }

  # Reset some thread data ready for event windows
  $thread->{Time} = -1 ;
  $thread->{samestamp} = 0 ;
}

my $timescale = $width / $window ; # timeline scale
my $partwidth = $height / scalar(@partitions) ; # thread scale

# Work out the marks to put on the time scale; one tick per decimal. Put a
# fractional round down fudge factor in so exact power of 10 seconds will have
# ticks.
my $ticksize = 10 ** floor(log($window * 0.999) / log(10)) ;

# We're going to build a PDF output file
my $pdf = new PDF::Writer($pdffile) ;
my $keylead = 7 ;
my @keyfont = ($pdf->font("Times-Roman"), $keylead) ;
my @labelfont = ($pdf->font("Helvetica"), 5) ;
my $dashpatt = "[1 5] 2" ;

# Be careful with page window arithmetic to ensure that we can get a half-open
# range without rounding errors.
my $wfirst = $first ;
for ( $page = 0 ; $page++ < $pages ; ) {
  # Set page window start, end, and scale
  my $wlast = $wfirst + $window ;

  print "Page $page window ${wfirst}s-${wlast}s\n" ;

  # Quickly discard all events before the start of this window. We include
  # all events greater than or equal to $wfirst, to those less than $wlast.
  while ( @events > 0 && $events[0]->{Time} < $wfirst ) {
    my $event = shift(@events) ;
    my $pdata = $event->{partition} ;
    my $stack = $pdata->{stack} ;
    my $thread = $threaddata{$event->{Thread}} ;

    if ( $event->{Type} eq "ENTER" ) {
      # Link together thread of control
      if ( $tracecontrol && !$donottrace{$event->{Id}} ) {
	push(@{$thread->{trace}}, $pdata->{slot}) ;
	$thread->{tracetime} = $event->{Time} ;
      }
      &stackpush($stack, $event) ;
    } elsif ( $event->{Type} eq "EXIT" ) {
      # Link together thread of control
      if ( $tracecontrol && !$donottrace{$event->{Id}} ) {
	pop(@{$thread->{trace}}) ;
	$thread->{tracetime} = $event->{Time} ;
      }
      &stackpop($stack, $event) ;
    } elsif ( $event->{Type} eq "AMOUNT" || $event->{Type} eq "ADD" ) {
      pop(@{$stack}) ;
      push(@{$stack}, $event) ;
    }
  }

  # Now prepare three PDF content streams; the prologue (key, furniture, etc),
  # the content (time trace), and the labels. The streams are concatenated
  # together so that the labels overwrite the trace if they conflict.
  my @prologue = ("0 -1 1 0 @{[f($xtrans,$pageheight-$ytrans)]} cm 0 w 0 J\n",
		  "0 -1 m @{[f($height)]} -1 l\n",
		  "0 @{[f($width + 1)]} m @{[f($height, $width + 1)]} l\n",
		  "@{[f($height)]} 0 m @{[f($height,$width)]} l S\n",
		  $pdf->text(@keyfont, $height, 0, " @{[f($wfirst)]}s"), "\n",
		  $pdf->text(@keyfont, $height, $width, " @{[f($wlast)]}s"), "\n") ;
  my @content = () ;
  my @labels = ("0 g 0 G\n") ;
  my $contentcolor = {} ;

  # If on last page, and last time is sufficiently different from end of
  # window, add a tick line for it
  if ( $page == $pages && ($last - $wfirst) < 0.95 * $window ) {
    my $y = ($last - $wfirst) * $timescale ;
    push(@labels,
	 "q $dashpatt d 0 @{[f($y)]} m @{[f($height,$y)]} l S Q\n",
	 $pdf->text(@keyfont, $height, $y, " @{[f($last)]}s"), "\n") ;
  }

  # Put time tick marks
  for ( my $tick = int($wfirst / $ticksize) * $ticksize + $ticksize ;
	$tick < $wlast ; $tick += $ticksize ) {
    my $y = ($tick - $wfirst) * $timescale ;
    push(@labels,
	 "q $dashpatt d 0 @{[f($y)]} m @{[f($height,$y)]} l S Q\n",
	 $pdf->text(@keyfont, $height, $y, " ${tick}s"), "\n") ;
  }

  # Print generated on tagline
  push(@prologue,
       "q 0 1 -1 0 0 0 cm\n ",
       $pdf->text(@keyfont, 0, 3, $tagline), "\n",
       " Q\n") ;

  foreach ( @threadorder ) {
    my $thread = $threaddata{$_} ;
    my $x = $thread->{partbase} * $partwidth ;

    # Print thread partition furniture
    push(@labels,
         "q 0 1 -1 0 @{[f($x)]} 0 cm ", $pdf->text(@keyfont, 2, -9, "Thread $_"), " Q\n",
	 "q\n") ;

    foreach (keys %{$thread->{partitions}}) {
      push(@labels,
	   "@{[f($x)]} 0 m @{[f($x,$width)]} l S\n",
	   "$dashpatt d\n") ;
      $x += $partwidth ;
    }

    # Reset timestamp for continued control traces to start of this window
    $thread->{tracetime} = $wfirst ;

    push(@labels, "Q\n") ;
  }

  # Reset timestamp for continued partitions to the start of this window
  foreach my $pdata ( @partitions ) {
    $pdata->{pagetime} = 0 ;
    $pdata->{pagemax} = 0 ;

    my $dolabel = $showdata ;

    foreach my $save ( reverse @{$pdata->{stack}} ) {
      if ( defined($save) ) {
	$save->{Time} = $wfirst ;

	# Print continuation labels for all active partitions
	if ( $dolabel ) {
	  my $xold = &xpos($pdata, $save->{prev_value}) ;
	  push(@labels,
	       $pdf->text(@labelfont, $xold, 0, " $save->{Designator}"),
	       "\n") ;
	  $dolabel = 0 ;
	}
      }
    }
  }

  # Continue filtering events until we find the end of this window. We include
  # all events greater than or equal to $wfirst, to those less than $wlast.
  while ( @events > 0 && $events[0]->{Time} < $wlast ) {
    my $event = shift(@events) ;
    # Get thread data accessor
    my $thread = $threaddata{$event->{Thread}} ;
    # Get partition data hash
    my $pdata = $event->{partition} ;
    my $stack = $pdata->{stack} ;

    $pdata->{pagemax} = $event->{post_value}
      if $event->{post_value} > $pdata->{pagemax} ;

    # Generate an adjusted time for this event to disambiguate marks if it
    # was the same as the previous event time.
    if ( $event->{Time} == $thread->{Time} ) {
      ++$thread->{samestamp} ;
    } else {
      $thread->{Time} = $event->{Time} ;
      $thread->{samestamp} = 0 ;
    }
    my $eadjust = $event->{Time} + $epsilon * $thread->{samestamp} ;

    my $rgb = &tracecolor($event->{Id}) ;

    # Get the position for this time
    my $ynew = ($eadjust - $wfirst) * $timescale ;

    # Note enter/exit events
    if ( $event->{Type} eq "ENTER" ) {
      my $stacksize = @{$stack} ;
      # Draw previous section up to this point
      if ( $stacksize > 0 ) {
	my $newest = 0 ;

	# Set block colour
	push(@content, &setcolor($contentcolor, $rgb, "rg ")) ;

	foreach my $save (@{$stack}) {
	  if ( defined($save) ) {
	    my $xold = &xpos($pdata, $save->{partindex}) ;
	    my $xnew = &xpos($pdata, $save->{partindex} + $save->{partincr}) ;
	    my $yold = ($save->{Time} - $wfirst) * $timescale ;

	    push(@content,
	         "@{[f($xold,$yold,$xnew-$xold,$ynew-$yold)]} re f\n") ;

	    $newest = $save->{Time} if $newest < $save->{Time} ;
	    $save->{Time} = $event->{Time} ;
	  }
	}

	# Add difference from newest event to page time
	$pdata->{pagetime} += $event->{Time} - $newest ;
      }

      &stackpush($stack, $event) ; # Push on per-thread stack

      # Write trace type to label stream
      if ( $showdata ) {
	my $xold = &xpos($pdata, $event->{partindex}) ;
	push(@labels,
	     $pdf->text(@labelfont, $xold, $ynew, " $event->{Designator}"),
	     "\n") ;
      }

      # If tracing control, non-timeline ENTER adds label marks
      if ( $tracecontrol && !$donottrace{$event->{Id}} ) {
	my $tracesize = @{$thread->{trace}} ;
	if ( $tracesize > 0 ) {
	  my $pslot = $thread->{trace}->[$tracesize-1] ;
	  my $yold = ($thread->{tracetime} - $wfirst) * $timescale ;
	  my $x = ($pdata->{slot} + 0.5) * $partwidth ; # Middle of slot
	  if ( $pdata->{slot} != $pslot ) { # Different partition
	    my $xold = ($pslot + 0.5) * $partwidth ;
	    push(@labels,
	         "@{[f($xold,$yold)]} m @{[f($xold,$ynew)]} l @{[f($x,$ynew)]} l S\n") ;
	  } else { # Same partition
	    push(@labels,
	         "@{[f($x,$yold)]} m @{[f($x,$ynew)]} l S\n") ;
	  }
	}
	push(@{$thread->{trace}}, $pdata->{slot}) ;
	$thread->{tracetime} = $event->{Time} ;
      }
    } elsif ( $event->{Type} eq "EXIT" ) {
      my $stacksize = @{$stack} ;
      if ( $stacksize > 0 ) {
	my $newest = 0 ;

	# Set block colour
	push(@content, &setcolor($contentcolor, $rgb, "rg ")) ;

	foreach my $save (@{$stack}) {
	  if ( defined($save) ) {
	    my $xold = &xpos($pdata, $save->{partindex}) ;
	    my $xnew = &xpos($pdata, $save->{partindex} + $save->{partincr}) ;
	    my $yold = ($save->{Time} - $wfirst) * $timescale ;

	    push(@content,
	         "@{[f($xold,$yold,$xnew-$xold,$ynew-$yold)]} re f\n") ;

	    $newest = $save->{Time} if $newest < $save->{Time} ;
	    $save->{Time} = $event->{Time} ;
	  }
	}

	# Add difference from newest event to page time
	$pdata->{pagetime} += $event->{Time} - $newest ;
      }

      # Remove item from thread-specific stack
      if ( my $match = &stackpop($stack, $event) ) {
	# If the labels differ, add the exit label
	if ( $match->{Designator} ne $event->{Designator} && $showdata ) {
	  my $xold = &xpos($pdata, $match->{partindex}) ;

	  push(@labels,
	       $pdf->text(@labelfont, $xold, $ynew, " $match->{Designator}"),
	       "\n") ;
	}
      }

      # If tracing control, non-timeline EXIT adds label marks
      if ( $tracecontrol && !$donottrace{$event->{Id}} ) {
	pop(@{$thread->{trace}}) ; # Remove matching ENTER
	my $yold = ($thread->{tracetime} - $wfirst) * $timescale ;
	my $x = ($pdata->{slot} + 0.5) * $partwidth ; # Middle of slot
	my $tracesize = @{$thread->{trace}} ;
	if ( $tracesize > 0 ) {
	  my $pslot = $thread->{trace}->[$tracesize-1] ;
	  if ( $pslot != $pdata->{slot} ) { # Different slot
	    my $xnew = ($pslot + 0.5) * $partwidth ;
	    push(@labels,
	         "@{[f($x,$yold)]} m @{[f($x,$ynew)]} l @{[f($xnew,$ynew)]} l S\n") ;
	  } else { # Same partition
	    push(@labels,
	         "@{[f($x,$yold)]} m @{[f($x,$ynew)]} l S\n") ;
	  }
	} else { # No previous partition
	  push(@labels,
	       "@{[f($x,$yold)]} m @{[f($x,$ynew)]} l S\n") ;
	}
	$thread->{tracetime} = $event->{Time} ;
      }

      # If this was a probe, adjust the page time for all nested events on
      # the probe's original thread, and draw a gray-out over the part of the
      # graph that should be ignored.
      # Remove wasted time in PROBE from all nested events on this thread
      my $wasted, $afotdata, $afoparts ;
      if ( $event->{Id} eq "PROBE"
	   and $tickspersec > 0
	   and defined($event->{afothread})
	   and ($wasted = $event->{Designator} / $tickspersec) > 0
	   and defined($afotdata = $threaddata{$event->{afothread}})
	   and defined($afoparts = $afotdata->{partitions}) ) {
	while ( my ($akey, $apart) = each %{$afoparts} ) {
	  $apart->{pagetime} -= $wasted ;
	}

	# Mark entire block in gray
	$wasted *= $timescale ;
	my $xold = $afotdata->{partbase} * $partwidth ;
	my $xnew = $xold + $partwidth * (scalar keys %{$afoparts}) ;
	push(@prologue,
	     "q 0.5 g @{[f($xold,$ynew-$wasted,$xnew,$wasted)]} re f Q\n") ;
      }
    } elsif ( $event->{Type} eq "AMOUNT" || $event->{Type} eq "ADD" ) {
      # Draw block showing old value up to this point
      my $xold = &xpos($pdata, 0) ;

      if ( my $top = pop(@{$stack}) ) {
	my $yold = ($top->{Time} - $wfirst) * $timescale ;
	my $xnew = &xpos($pdata, $top->{post_value}) ;

	# Mark block in colour
	push(@content,
	     &setcolor($contentcolor, $rgb, "rg "),
	     "@{[f($xold,$yold,$xnew-$xold,$ynew-$yold)]} re f\n") ;
      }

      push(@{$stack}, $event) ; # Push on per-thread stack

      # Add the new label
      if ( $showdata || $showdata{$event->{Id}}{"AMOUNT"} ) {
	push(@labels,
	     $pdf->text(@labelfont, $xold, $ynew, " $event->{Designator}"),
	     "\n") ;
      }
    } elsif ( $event->{Type} eq "VALUE" ) {
      # Point value, similar to AMOUNT/ADD
      my $xold = &xpos($pdata, 0) ;
      my $xnew = &xpos($pdata, $event->{post_value}) ;

      # Mark block in colour
      push(@content,
	   &setcolor($contentcolor, $rgb, "rg "),
	   "@{[f($xold,$ynew,$xnew-$xold)]} 1 re f\n") ;

      # If the labels differ, add the new label
      push(@labels,
	   $pdf->text(@labelfont, $xold, $ynew, " $event->{Designator}"),
	   "\n") ;
    } else {
      # Not a module enter/exit, so put a mark at the timestamp. If a timeline,
      # use a special mark for each of the event types.
      if ( my $match = &stackmatch($pdata->{stack}, $event) ) {
	my $x = &xpos($pdata, $match->{partindex}+0.5) ;
	my $xd = $x-&xpos($pdata, $match->{partindex}+0.1) ;
	my @mark = @{$tlmark{$event->{Type}}} or
	  # Normal chevron for others:
	  ( -1,-1,"m", 0,0,"l", 1,-1,"l" ) ;
	while ( @mark ) {
	  my $xo = shift @mark ;
	  my $yo = shift @mark ;
	  my $lm = shift @mark ;
	  push(@labels, "@{[f($x+$xo*$xd,$ynew+$yo)]} $lm ") ;
	}
	push(@labels, "S\n") ;
      } else {
	my $x = ($pdata->{slot} + 0.25) * $partwidth ;
	push(@content, &setcolor($contentcolor, $rgb, "RG "),
	     "@{[f($x-4,$ynew-1)]} m @{[f($x,$ynew)]} l @{[f($x+4,$ynew-1)]} l S\n") ;

	if ( $showdata || $showdata{$event->{Id}}{"MARK"} ) {
	  push(@labels,
               $pdf->text(@labelfont, $x, $ynew, " $event->{Designator}"),
	       "\n") ;
	}
      }
    }
  }

  # We need to finish this page. Any partitions that have not exited need to
  # draw a continuation line from their start point to $wlast.
  foreach my $pdata ( @partitions ) {
    my $stack = $pdata->{stack} ;
    my $stacksize = @{$stack} ;
    if ( $stacksize > 0 ) {
      my $newest = 0 ;

      foreach my $save (@{$stack}) {
	if ( defined($save) ) {
	  my $xold = &xpos($pdata, $save->{partindex}) ;
	  my $xnew = &xpos($pdata, $save->{partindex} + $save->{partincr}) ;
	  my $yold = ($save->{Time} - $wfirst) * $timescale ;

	  if ( $newest == 0 ) {
	    $newest = $save->{Time} ;
	    push(@content, # Set block colour
                 &setcolor($contentcolor, &tracecolor($save->{Id}), "rg ")) ;
	  } elsif ( $newest < $save->{Time} ) {
	    $newest = $save->{Time} 
	  }

	  push(@content,
	       "@{[f($xold,$yold,$xnew-$xold,$width-$yold)]} re f\n") ;
	}
      }

      # Add difference from newest event to page time
      $pdata->{pagetime} += $wlast - $newest if $newest > 0 ;
    }
  }

  if ( $tracecontrol ) {
    foreach my $tkey (@threadorder) {
      my $thread = $threaddata{$tkey} ;
      my $tracesize = @{$thread->{trace}} ;
      if ( $tracesize > 0 ) {
	my $pslot = $thread->{trace}->[$tracesize-1] ;
	my $x = ($pslot + 0.5) * $partwidth ; # Middle of slot
	my $yold = ($thread->{tracetime} - $wfirst) * $timescale ;
	push(@labels,
             "@{[f($x,$yold)]} m @{[f($x,$width)]} l S\n") ;
	$thread->{tracetime} = $wlast ;
      }
    }
  }

  # Print trace colors key, with totals
  push(@prologue,
       "q 0 1 -1 0 0 @{[f($width)]} cm\n ",
       $pdf->text(@keyfont, 3, 0, "Key"), "\n") ;

  foreach my $pdata (@partitions) {
    my $linebase = $pdata->{slot} * $partwidth ; # 2 lines
    push(@prologue,
         " ", &tracecolor($pdata->{id}), " rg\n ",
         $pdf->text(@keyfont, 3, -$linebase - $keylead,
		    $pdata->{name}), "\n ",
         $pdf->text(@keyfont, 5, -$linebase - 2 * $keylead,
		    $pdata->{totaltime} > 0 ?
		    "t=@{[f($pdata->{totaltime})]}s p=@{[f($pdata->{pagetime})]}s" :
		    ""), "\n",
         $pdf->text(@keyfont, 5, -$linebase - 3 * $keylead,
		    $pdata->{max_value} <= 1 ?
	            "" :
		    $pdata->{pagemax} > 0 ?
		    "max=$pdata->{max_value} p=$pdata->{pagemax}" :
		    "max=$pdata->{max_value}"),
         "\n") ;
  }
  push(@prologue, "Q\n") ;

  # Page size is intersection of letter and A4
  $pdf->page($pagewidth, $pageheight,
             $pdf->stream(@prologue),
             $pdf->stream(@content),
             $pdf->stream(@labels)) ;
  # Update start for next loop
  $wfirst = $wlast ;
}

$pdf->finish() ;

print "Output is in $pdffile\n" ;

exit 0 ;

sub usage {
  print "Usage: probegraph [-<pages>] [-<id-to-ignore>] [+<id-to-show>] [<id>=<options>] [-data] [-tracecontrol] [-landscape [<paper>]] [-portrait [<paper>]] [-start <time>] [-end <time>] [-window <time>] [-merge <id>] [-o <file.pdf>] [logfile]\n" ;
  exit 1 ;
}

sub hsbtorgb { # HSB/HSV to RGB, defaulting sat and value to 1 if missing
  my ($h, $s, $v) = (@_, 1, 0.85) ; # fully saturated, reduce value for readability
  # For our purpose, h is in range 0..1
  my $sext = int($h * 6) % 6 ;
  my $frac = $h * 6 - int($h * 6) ;
  my $p = $v * (1 - $s) ;
  my $q = $v * (1 - $frac * $s) ;
  my $t = $v * (1 - (1 - $frac) * $s) ;

  return "@{[f($v,$t,$p)]}" if $sext == 0 ;
  return "@{[f($q,$v,$p)]}" if $sext == 1 ;
  return "@{[f($p,$v,$t)]}" if $sext == 2 ;
  return "@{[f($p,$q,$v)]}" if $sext == 3 ;
  return "@{[f($t,$p,$v)]}" if $sext == 4 ;
  return "@{[f($v,$p,$q)]}" if $sext == 5 ;

  die "Can't convert HSV $h $s $v to RGB" ;
}

# Get trace color from a trace id
sub tracecolor {
  return hsbtorgb($traceids{$_[0]} / $ntraceids) ;
}

# Lazy color setting
sub setcolor {
  my ($current, $new, $fillstroke) = @_ ;
  if ( $current->{$fillstroke} ne $new ) {
    $current->{$fillstroke} = $new ;
    return "$new $fillstroke" ;
  }
  return "" ;
}

# Return the X position for a partition, with a value field
sub xpos {
  my ($pdata, $value) = @_ ;
  return ($pdata->{slot} + 0.05) * $partwidth if $pdata->{max_value} == 0 ;
  return ($pdata->{slot} + $value * 0.9 / $pdata->{max_value} + 0.05) * $partwidth ;
}

# Get the most recent element of the partition, or undef if empty
sub stackmatch {
  my $stack = shift ;
  my $event = shift ;

  if ( defined($event->{tlothread}) ) { # Match designator for this event Id
    foreach my $match ( @{$stack} ) {
      if ( defined($match) && $match->{Designator} == $event->{Designator} ) {
	return $match ;
      }
    }
  }

  undef ;
}

# Push an event onto a partition stack
sub stackpush {
  my $stack = shift ;
  my $event = shift ;
  my $i ;
  for ( $i = 0 ; $i < @{$stack} ; ++$i ) {
    if ( !defined($stack->[$i]) ) {
      $event->{partindex} = $i ;
      $stack->[$i] = $event ;
      return ;
    }
  }
  $event->{partindex} = $i ;
  push(@{$stack}, $event) ;
}

# Pop an event from a partition stack
sub stackpop {
  my $stack = shift ;
  my $event = shift ;
  if ( defined($event->{tlothread}) ) { # Match designator for this event Id
    foreach my $match (@{$stack} ) {
      if ( defined($match) && $match->{Designator} == $event->{Designator} ) {
	my $save = $match ; # Match is an alias to the stack
	$stack->[$match->{partindex}] = undef ;
	while (@{$stack}) { # Remove trailing entries from stack
	  my $top = pop(@{$stack}) ;
	  if ( defined($top) ) {
	    push(@{$stack}, $top) ;
	    last ;
	  }
	}
	return $save ;
      }
    }
    print "Unmatched timeline event $event->{Id} ($event->{Designator})\n" ;
  } else {
    return pop(@{$stack}) ;
  }
  undef ;
}

# For interpolative contexts, output float format
sub f {
  map {
    $_ = sprintf("%.5f", $_) ; # Need fixed-point
    s/\.?0+$// ; # But don't need trailing zeros
    $_ ;
  } @_ ;
}

# Determine the partition hash key for event
sub pkey {
  my %pkey = ("ENTER" => "", "EXIT" => "",
              "ENDING" => "", "ABORTING" => "",
              "PROGRESS" => "", "TITLE" => "",
	      "AMOUNT" => "#", "ADD" => "#") ;
  my %order = ("ENTER" => "", "EXIT" => "",
               "ENDING" => "", "ABORTING" => "",
               "PROGRESS" => "", "TITLE" => "",
               "AMOUNT" => "1", "ADD" => "1", "VALUE" => "2", "MARK" => "3") ;
  my $event = shift ;
  my $type = $event->{Type} ;
  my $id = "$order{$type}/$event->{Id}" ;
  return $id . $pkey{$type} if defined($pkey{$type}) ;
  return "$id $type" ;
}

# Set options for an event name
sub option {
  my ($traceid, $designator, $threadid) = @_ ;
  $affinity{$traceid} = $threadid if $designator & 1 ;
  $showdata{$traceid}{"MARK"} = 1 if $designator & 2 ;
  $showdata{$traceid}{"AMOUNT"} = 1 if $designator & 4 ;
  if ( $designator & 8 ) {
    $timeline{$traceid} = {} ;
    $donottrace{$traceid} = 1 ;
  }
}

#ifdef HQN
# Log stripped
