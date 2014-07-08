# $HopeName: SWprod_hqnrip!testsrc:perl5libs:PDF:Writer.pm(EBDSDK_P.1) $
# Simple PDF writer
# Creates PDF::Writer object and manipulate it
# Create:
#  $pdf = new PDF::Writer "filename"
#  $pdf = new PDF::Writer("filename", 1.3)
# Manipulate:
#  $pdf->reserve() return an object number that can be used later
#  $pdf->object() opens an object and returns the object number
#  $pdf->object($objnum) opens a numbered object
#  $pdf->endobj() closes an open object. It is optional.
#  $pdf->objref(objnum ...) returns a string for each object number
#  $pdf->raw(data...) writes raw data to the file
#  $pdf->finish closes finishes the PDF file
#  $pdf->stream(string ...) returns an object number for a stream
#  $pdf->text(fontname, size, string ...) returns text for to insert
#  $pdf->page(w, h, streamobj ...) returns an object number for a page

package PDF::Writer ;
require Exporter ;
@ISA = qw(Exporter) ;
@EXPORT = qw(new) ;
@EXPORT_OK = () ;

sub INIT {
  eval 'require Compress::Raw::Zlib' ;
  $have_zlib = $@ eq "" ;
}

sub new {
  my $class = shift ;
  my $self = {} ;
  my $old_ = $_ ;

  bless $self, $class ;

  my $version = 1.4 ;
  if ( @_ == 2 ) { # Filename and version supplied
    $version = $_[1] ;
  } elsif ( @_ != 1 ) {
    die "PDF::Writer new requires filename" ;
  }

  my $filename = $_[0] ;

  die "PDF::Writer can't write to file $filename" if !open(FILE, ">$filename") ;
  binmode(FILE) ;

  $self->{handle} = \*FILE ;
  $self->{written} = 0 ;
  $self->{pages} = [] ;
  $self->{objects} = [ 0 ] ;
  $self->{resources} = {} ; # hash of hashes
  $self->{fonts} = {} ;
  $self->{inobject} = 0 ;
  $self->{pagesobj} = $self->reserve() ;
  $self->{resourcesobj} = $self->reserve() ;

  $self->raw("%PDF-$version\n%\xe3\xe2\xcf\xd3\n") ;

  $_ = $old_ ;

  $self ;
}

sub raw {
  my $self = shift ;
  my $handle = $self->{handle} ;
  map {
    my $bytesout = length($_) ;
    $self->{written} += $bytesout ;
    print $handle $_ ;
  } @_ ;
}

sub stream {
  my $self = shift ;

  my $sizeobj = $self->reserve() ; # for "Length"
  my $stream = $self->object() ;
  $self->raw("<<\n  /Length " . $self->objref($sizeobj) .
	     ($have_zlib ? "\n  /Filter /FlateDecode" : "") . "\n>>\nstream\n") ;
  my $size = $self->{written} ;
  if ( $have_zlib ) {
    my $deflate = Compress::Raw::Zlib::Deflate::new()
      or die "Cannot create compression filter, $!" ;
    my $output ;
    map {
      die "Cannot compress stream, $!"
	if $deflate->deflate($_, $output) != Z_OK ;
      $_ = $output ;
    } @_ ;
    die "Cannot finish compressed stream, $!"
      if $deflate->flush($output) != Z_OK ;
    push(@_, $output, "\n") ;
  }
  map {
    $self->raw($_) ;
  } @_ ;
  $size = $self->{written} - $size ;
  $self->raw("endstream\n") ;
  $self->endobj() ;

  # Now write size object
  $self->object($sizeobj) ;
  $self->raw("$size\n") ;
  $self->endobj() ;

  return $stream ;
}

sub text {
  my ($self, $font, $size, $x, $y, @text) = @_ ;
  @text = grep { length($_) > 0 } @text ;
  return "" if @text == 0 ;
  return join(" ",
              sprintf("BT /%s %d Tf %.3f %.3f Td", $font, $size, $x, $y),
              (map { "($_) Tj" } @text),
              "ET");
}

sub font {
  my $self = shift ;
  my $fontname = shift ;

  if ( !defined($self->{fonts}->{$fontname}) ) {
    my $fontobj = $self->object() ;
    $self->raw("<<\n  /Type /Font\n  /Subtype /Type1\n  /BaseFont /$fontname\n>>\n") ;
    $self->endobj() ;

    # Put references into font and resources hash tables
    my $resname = "F$fontobj" ;
    $self->{fonts}->{$fontname} = $resname ;
    $self->{resources}->{Font}->{$resname} = $fontobj ;
  }

  return $self->{fonts}->{$fontname} ;
}

sub page {
  my ($self, $w, $h, @contents) = @_ ;
  my $pages = $self->{pages} ;

  my $page = $self->object() ;
  $self->raw("<<\n  /Type /Page\n  /Resources "
  .          $self->objref($self->{resourcesobj})
  .          "\n  /Contents ["
  .          $self->objref(@contents)
  .          "]\n  /Parent "
  .          $self->objref($self->{pagesobj})
  .          "\n  /MediaBox [0 0 $w $h]\n>>\n") ;
  $self->endobj() ;

  push(@{$pages}, $page) ;
}

sub reserve {
  my $self = shift ;
  my $objects = $self->{objects} ;
  my $objnum = scalar(@{$objects}) ;
  $objects->[$objnum] = 0 ;
  return $objnum ;
}

sub object {
  my $self = shift ;

  # Close any open object
  $self->endobj() if $self->{inobject} ;

  # Get or allocate new object number
  my $objects = $self->{objects} ;
  my $objnum ;
  if ( @_ == 0 ) {
    $objnum = scalar(@{$objects}) ;
  } elsif ( @_ == 1 ) {
    $objnum = $_[0] ;
  } else {
    die "PDF::Writer::object has too many arguments, @_" ;
  }
  die "PDF::Writer::object number $objnum already used"
    if $objects->[$objnum] != 0 ;
  $objects->[$objnum] = $self->{written} ;
  $self->raw("$objnum 0 obj\n") ;
  $self->{inobject} = 1 ;
  return $objnum ;
}

sub endobj {
  my $self = shift ;
  $self->raw("endobj\n") ;
  $self->{inobject} = 0 ;
}

sub objref {
  my $self = shift ;
  join(" ", map { "$_ 0 R" } @_) ;
}

sub finish {
  my $self = shift ;

  my $pages = $self->{pages} ;

  # Write Resources, Catalog, Pages, trailer, then remaining furniture
  my $resourcesobj = $self->object($self->{resourcesobj}) ;
  $self->raw("<<\n  /ProcSet [/PDF /ImageB /ImageC /Text]\n") ;
  my $resources = $self->{resources} ;
  map {
    $self->raw("  /$_ <<\n") ;
    my $elements = $resources->{$_} ;
    map {
      $self->raw("    /$_ " . $self->objref($elements->{$_}) . "\n") ;
      
    } keys %{$elements} ;
    $self->raw("  >>\n") ;
  } keys %{$resources} ;
  $self->raw(">>\n") ;
  $self->endobj() ;

  my $pagesobj = $self->object($self->{pagesobj}) ;
  $self->raw("<<\n  /Type /Pages\n  /Kids ["
  .          $self->objref(@{$pages})
  .          "]\n  /Count "
  .          scalar(@{$pages})
  .          "\n>>\n") ;
  $self->endobj() ;

  my $catalog = $self->object() ;
  $self->raw("<<\n  /Type /Catalog\n  /Pages " . $self->objref($pagesobj) . "\n>>\n") ;
  $self->endobj() ;

  my $objects = $self->{objects} ;

  # Write xref table, trailer, then remaining furniture
  my $xref = $self->{written} ;
  $self->raw("xref\n0 " . scalar(@{$objects}) . "\n") ;

  my $gen = 65535 ;
  for ( my $objnum = 0 ; $objnum < @{$objects} ; $objnum++ ) {
    my $offset = $objects->[$objnum] ;
    $self->raw(sprintf("%010d %05d %s\r\n", $offset, $gen, $offset == 0 ? 'f' : 'n')) ;
    $gen = 0 ;
  }

  $self->raw("trailer\n<<\n  /Size "
  .          scalar(@{$objects})
  .          "\n  /Root "
  .          $self->objref($catalog)
  .          "\n>>\nstartxref\n"
  .          $xref
  .          "\n%%EOF\n") ;

  close $self->{handle} ;

  $self->{handle} = undef ;
}

1 ;

# Log stripped
