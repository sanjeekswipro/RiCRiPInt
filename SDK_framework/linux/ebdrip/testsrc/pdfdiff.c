/* Copyright (C) 2004-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfdiff.c(EBDSDK_P.1) $
 *
 * Program to difference two stylised PDF image files created by the
 * SWprod_coreRip regression test PDF skin, reporting percentage differences
 * and producing a 1-bit mask where there are differences.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>

#include "zlib.h"

/* Use a nice big buffer size. This tool is used by the regression system,
 * which often runs on heavily loaded servers; reading lots of small chunks from
 * the disk does not work well in that situation. */
#define PDFBUFSIZE (1024 * 500)
#define STRING_AND_LENGTH(s) "" s "", sizeof("" s "") - 1
#define TRUE 1
#define FALSE 0

#define CLRIP_IMAGE_STREAM_LENGTH 2
#define GUI_RIP_IMAGE_STREAM_LENGTH 13

enum {
    BEFORE = 1,
    AFTER = 2,
    DIFFS = 4,
  } ;

static char *program = NULL ;


static void usage(void)
{
  fprintf(stderr, "Copyright (C) Global Graphics Software Ltd, 2004-2011\n"
          "Usage: %s [-o maskfile] [-a{0,1,2}] [-n] [-p] [-r] [-s] [-c composite] "
          "[-x] [-h before|after|diffs] [-t tolerance] pdf1 pdf2\n"
          "\tpdf1,pdf2\t\tPDF image files to compare\n"
          "\t-o maskfile\t\tOutput PDF file showing input differences\n"
          "\t-a0 (the default)\tOutput maskfile shows differences as 1-bit black&white\n"
          "\t-a1\t\t\tOutput maskfile shows differences as 8-bit grayscale\n"
          "\t-a2\t\t\tOutput maskfile shows differences as 4-bit pseudo-color\n"
          "\t-n\t\t\tInclude /None colorants in difference checks (default ignore)\n"
          "\t-p\t\t\tReport number of pixels different\n"
          "\t-r\t\t\tReport locations of differing pixels\n"
          "\t-s\t\t\tDifferences from different sizes are not counted\n"
          "\t-c compfile\t\tCombine mask and originals into 'composite' file.\n"
          "\t-h layer\t\tHide layer in composite file.\n"
          "\t-t tolerance\t\tTolerance for color differences.\n"
          "\t-x\t\t\tOutput machine readable summary (total difference percentage, "
          "total difference count, maximum difference)\n",
          program) ;
  exit(1) ; /* Exit with zero to stop error from dropping messages */
}


static void error(char *format, ...) {
  va_list args;

  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  putchar('\n') ;
  exit(1);
}


typedef struct {
  FILE *fp ;
  char *name ;
  size_t available, processed, total ;
  Byte *buffer ;
} pdffile_t ;

/* readbytes and readline read from the input file, into the file buffer.
   They set the available field to indicate the number of bytes in the
   buffer, and rely on the processed field to indicate the number of bytes
   already used from the buffer. readline() updates processed and total, but
   readbytes() does not; the consumer must update these before calling
   readbytes again. */
static int readbytes(pdffile_t *in, Byte **start, size_t *length)
{
  for (;;) {
    *length = in->available - in->processed ;
    *start = in->buffer + in->processed ;

    if ( *length == 0 ) {
      size_t bytes = fread(in->buffer, 1, PDFBUFSIZE, in->fp) ;

      if ( bytes == 0 )
        return 0 ;

      in->available = bytes ;
      in->processed = 0 ;
    } else {
      return 1 ;
    }
  }
}

static int readline(pdffile_t *in, Byte **start, size_t *length,
                    char **sanswhite)
{
  char *scan ;

  for (;;) {
    size_t index ;
    int cr = 0, eol = 0 ;
    size_t extra ;

    for ( index = in->processed ; index < in->available ; ++index ) {
      if ( eol ) { /* Seen an EOL on previous char */
        *start = in->buffer + in->processed ;
        *length = index - in->processed ;
        in->processed = index ;
        goto gotline ;
      } else if ( in->buffer[index] == '\r' ) {
        if ( cr ) { /* A single \r without \n after it is an EOL */
          *start = in->buffer + in->processed ;
          *length = index - in->processed ;
          in->processed = index ;
          goto gotline ;
        }
        cr = 1 ;
      } else if ( in->buffer[index] == '\n' ) {
        eol = 1 ; /* LF with or without CR before is EOL */
      }
    }

    /* Shuffle down and try again */
    in->available -= in->processed ;
    memmove(in->buffer, in->buffer + in->processed, in->available) ;
    in->processed = 0 ;

    extra = fread(in->buffer + in->available, 1, PDFBUFSIZE - in->available, in->fp) ;
    if ( extra == 0 ) { /* No more data */
      if ( eol || cr ) { /* We already had an EOL, return this line */
        *start = in->buffer ;
        *length = in->available ;
        in->processed = in->available ;
        goto gotline ;
      }

      if ( in->available == 0 ) /* No more data available, at EOF */
        return 0 ;

      error("No end of line found reading file %s", in->name) ;
    }

    in->available += extra ;
  }

 gotline:
  in->total += *length ;

  for ( scan = (char *)*start ;
        *scan == ' ' || *scan == '\t' ; ++scan ) /* Find content */ ;

  *sanswhite = scan ;

  return 1 ;
}

/* readcompressedbyte() is a wrapper around readbytes() that
 * a) Does maintain total and processed counts in the input stream
 * b) arranges to "inflate" (i.e. decompress the bytes read from the input stream
 *    into an output buffer whenever more bytes are actually read)
 * c) Return the next available decompressed byte
 */

static int
readdecompressedbyte(pdffile_t* in,
                     struct z_stream_s* zstate,
                     Byte*  outbuf,
                     Byte* decompressed_byte)
{
  int zres = Z_OK;

  while ( zres != Z_STREAM_END && zstate->avail_out == PDFBUFSIZE ) {
    size_t avail_in = (size_t)zstate->avail_in ;

    if ( avail_in == 0 ) {
      if ( ! readbytes(in, &zstate->next_in, &avail_in) ) {
        error("No more bytes available from file %s", in->name) ;
        /*NOTREACHED*/
      }

      zstate->avail_in = (uInt)avail_in ;
    }

    zstate->next_out = outbuf ;
    zres = inflate(zstate, Z_SYNC_FLUSH) ;

    in->processed += avail_in - (size_t)zstate->avail_in;
    in->total += avail_in - (size_t)zstate->avail_in;

    if ( zres != Z_OK && zres != Z_STREAM_END ) {
      error("Problem decompressing file %s", in->name) ;
      /*NOTREACHED*/
    }

    zstate->next_out = outbuf ;
  }

  if ( zstate->avail_out == PDFBUFSIZE ) {
    error("Premature end of flate stream in file %s", in->name);
    /*NOTREACHED*/
  }

  (*decompressed_byte) = *zstate->next_out++ ;
  ++zstate->avail_out ;

  return zres;
}

static void writebytes(pdffile_t *out, Byte *buffer, size_t length)
{
  if ( out ) {
    if ( fwrite(buffer, 1, length, out->fp) != length )
      error("Error writing to file %s", out->name);

    out->total += length ;
  }
}

typedef struct colorant_t {
  struct colorant_t *next ;
  int isnone ; /* This colorant is /None. */
  char name[1] ; /* allocation for rest of name follows */
} colorant_t ;

colorant_t *newcolorant(char *name)
{
  colorant_t *colorant ;

  if ( (colorant = malloc(sizeof(colorant_t) + strlen(name))) == NULL )
    error("Memory exhausted allocating colorant %s", name) ;

  colorant->next = NULL ;
  colorant->isnone = (strcmp(name, "None") == 0) ;
  strcpy(colorant->name, name) ;

  return colorant ;
}

/**
 * Write the mask colorspace.
 */
void write_colorspace(int alpha, pdffile_t *out)
{
  char* csStartString = "  /ColorSpace [\n";
  char* csEndString = "  ]\n";

  writebytes(out, (Byte*)csStartString, strlen(csStartString));

  switch (alpha) {
    case 0:
    case 1:
      /*
       * For the "-a0" and "-a1" command-line options
       * We use a /DeviceGray color space
       */

      writebytes(out, (Byte *)STRING_AND_LENGTH("    /DeviceGray\n")) ;

      break;

    case 2:
      /*
       * For the "-a2" command-line option
       * We use an Indexed colour space that contains
       * 9 colours different colours.
       *
       * It could contain upto 16 different colours
       * and remain just a 4-bit colour space.
       *
       * The 9 colours are currently:
       * White, Red (100%), Green (100%), Blue (100%),
       * Cyan (100%), Magenta (100%), Yellow (100%), Black (100%) and Orange
       */

      writebytes(out, (Byte *)STRING_AND_LENGTH("    /Indexed\n"
                                                "    /DeviceRGB\n"
                                                "    8\n"
                                                "    <")) ;

#define INDEXED_CS_WHITE    0
#define INDEXED_CS_RED      1
#define INDEXED_CS_GREEN    2
#define INDEXED_CS_BLUE     3
#define INDEXED_CS_CYAN     4
#define INDEXED_CS_MAGENTA  5
#define INDEXED_CS_YELLOW   6
#define INDEXED_CS_BLACK    7
#define INDEXED_CS_ORANGE   8

      writebytes(out, (Byte *)STRING_AND_LENGTH("FFFFFF" /* White */
                                                "FF0000" /* Red (100%) */
                                                "00FF00" /* Green (100%) */
                                                "0000FF" /* Blue (100%) */
                                                "00FFFF" /* Cyan (100%) */
                                                "FF00FF" /* Magenta (100%) */
                                                "FFFF00" /* Yellow (100%) */
                                                "000000" /* Black (100%) */
                                                "FFAF00" /* Orange */
                                                ">\n")) ;
      break;

    default:
      error("Unrecognised \"alpha\" value %d", alpha);
      break;
  }
  writebytes(out, (Byte*)csEndString, strlen(csEndString));
}

/**
 * Store CMYK in 'colorants'.
 */
void setup_cmyk(colorant_t **colorants)
{
  *colorants = newcolorant("Cyan") ;
  colorants = &(*colorants)->next ;
  *colorants = newcolorant("Magenta") ;
  colorants = &(*colorants)->next ;
  *colorants = newcolorant("Yellow") ;
  colorants = &(*colorants)->next ;
  *colorants = newcolorant("Black") ;
}

/**
 * Read the colorspace details from 'file'. We've already read a line containing
 * '/Colorspace [', so here we're just reading the rest, including the
 * concluding ']'.
 */
void read_colorspace(pdffile_t *file, colorant_t **colorants)
{
  Byte *line ;
  char *start ;
  size_t bytes ;

  if ( !readline(file, &line, &bytes, &start) )
    error("No colourspace in file %s", file->name) ;

  if ( strncmp(start, STRING_AND_LENGTH("/DeviceGray")) == 0 ) {
    *colorants = newcolorant("Gray") ;
  } else if ( strncmp(start, STRING_AND_LENGTH("/DeviceRGB")) == 0 ) {
    *colorants = newcolorant("Red") ;
    colorants = &(*colorants)->next ;
    *colorants = newcolorant("Green") ;
    colorants = &(*colorants)->next ;
    *colorants = newcolorant("Blue") ;
  } else if ( strncmp(start, STRING_AND_LENGTH("/DeviceCMYK")) == 0 ) {
    setup_cmyk(colorants);
  } else if ( strncmp(start, STRING_AND_LENGTH("/Separation")) == 0 ) {
    if ( readline(file, &line, &bytes, &start) ) {
      char name[128] ; /* Max PDF name length */

      if ( sscanf(start, "/%s", name) != 1 )
        error("Unrecognised Separation colorant in file %s", file->name) ;

      *colorants = newcolorant(name) ;
      colorants = &(*colorants)->next ;
    } else {
      error("Missing Separation colorant in file %s", file->name) ;
    }
  } else if ( strncmp(start, STRING_AND_LENGTH("/DeviceN [")) == 0 ) {
    for (;;) {
      char name[128] ; /* Max PDF name length */

      if ( !readline(file, &line, &bytes, &start) )
        error("Can't read colorants from file %s", file->name) ;

      if ( *start == ']' )
        break ;

      if ( sscanf(start, "/%s", name) != 1 )
        error("Unrecognised DeviceN colorant in file %s", file->name) ;

      *colorants = newcolorant(name) ;
      colorants = &(*colorants)->next ;
    }
  } else {
    error("Unknown colourspace in file %s", file->name) ;
  }

  for (;;) {
    if ( !readline(file, &line, &bytes, &start) )
      error("Can't read ColorSpace from file %s", file->name) ;

    if ( *start == ']' )
      break ;
  }
}


#define MAXPDFOBJECT 200

typedef struct pdfobj_t {
  size_t where ;
  unsigned short generation ;
  char type ;
} pdfobj_t ;

static pdfobj_t objects[MAXPDFOBJECT] ;

long maxpdfobject = 0 ;

#define iseol(_c) ((_c) == '\r' || (_c) == '\n')

/**
 * Returns TRUE if the input file was generated by the GUI rip (otherwise the
 * clrip was the generator).
 */
static int pdfheader(pdffile_t *file, int alpha,
                     pdffile_t *out,
                     long *width, long *height, long *depth,
                     colorant_t **colorants)
{
  size_t bytes ;
  Byte *line ;
  char *start ;
  long otherwidth = *width, otherheight = *height ; /* barf! */
  int readColorSpace = FALSE;
  int guiRipGenerated = FALSE;

  *width = *height = *depth = 0 ;
  *colorants = NULL ;

  while ( readline(file, &line, &bytes, &start) ) {
    long objnum ;
    int objgen ;
    char nl ;

    if ( sscanf(start, "/BitsPerComponent %ld", depth) == 1 ) {
      switch (alpha) {
      case 0:
        writebytes(out, (Byte *)STRING_AND_LENGTH("  /BitsPerComponent 1\n")) ;
        break;

      case 1:
        writebytes(out, (Byte *)STRING_AND_LENGTH("  /BitsPerComponent 8\n")) ;
        break;

      case 2:
        writebytes(out, (Byte *)STRING_AND_LENGTH("  /BitsPerComponent 4\n")) ;
        break;

      default:
        error("Unrecognised \"alpha\" value %d", alpha);
        break;
      }
      continue ;
    }

    if ( sscanf(start, "/Width %ld", width) == 1 ) {
      if ( out ) {
        int length ;
        sprintf((char *)out->buffer, "  /Width %ld\n%n",
                *width > otherwidth ? *width : otherwidth, &length) ;
        writebytes(out, out->buffer, length) ;
      }
      continue ;
    }

    if ( sscanf(start, "/Height %ld", height) == 1 ) {
      if ( out ) {
        int length ;
        sprintf((char *)out->buffer, "  /Height %ld\n%n",
                *height > otherheight ? *height : otherheight, &length) ;
        writebytes(out, out->buffer, length) ;
      }
      continue ;
    }

    if ( out &&
         sscanf((char *)line, "%ld %d obj%c", &objnum, &objgen, &nl) == 3 &&
         iseol(nl) ) {
      if ( objnum >= MAXPDFOBJECT )
        error("PDF object number out of range in file %s", out->name) ;
      if ( objnum > maxpdfobject )
        maxpdfobject = objnum ;

      objects[objnum].type = 'n' ;
      objects[objnum].generation = (unsigned short)objgen ;
      objects[objnum].where = out->total ;
    }

    /* The GUI Rip's PDFraster plugin produces decode arrays; discard them. */
    if ( strncmp(start, STRING_AND_LENGTH("/Decode")) == 0 ) {
      continue;
    }

    /* The GUI Rip's PDFraster plugin produces colorspaces like this. */
    if ( strncmp(start, STRING_AND_LENGTH("/ColorSpace /DeviceCMYK")) == 0 ) {
      write_colorspace(alpha, out);
      setup_cmyk(colorants);
      /* Set this so we know the next stream is the image data. */
      readColorSpace = TRUE;
      guiRipGenerated = TRUE;
      continue;
    }

    /* The clrip's PDF raster backend produces colorspaces like this. */
    if ( strncmp(start, STRING_AND_LENGTH("/ColorSpace [")) == 0 ) {
      write_colorspace(alpha, out);
      read_colorspace(file, colorants);
      /* Set this so we know the next stream is the image data. */
      readColorSpace = TRUE;
      continue;
    }

    /* All others can copy the line without modification */
    writebytes(out, line, bytes) ;

    /* GUI Rip produced image stream. */
    if ( strncmp(start, STRING_AND_LENGTH("stream")) == 0 ) {
      if (readColorSpace)
        return guiRipGenerated;
    }

    /* Clrip produced image stream. */
    if ( strncmp(start, STRING_AND_LENGTH(">> stream")) == 0 ) {
      if (readColorSpace)
        return guiRipGenerated;
    }
  }

  error("Image stream not found in file %s", file->name) ;
  return guiRipGenerated;
}

int pdfdiff(pdffile_t *in1, pdffile_t *in2, pdffile_t *out,
            int alpha, int report, int sizematters, int nonematters,
            int tolerance,
            double *percent, unsigned long *pixels,
            unsigned long *orange_pixels,
            unsigned long *cyan_pixels,
            unsigned long *green_pixels,
            unsigned long *magenta_pixels,
            unsigned long *red_pixels,
            unsigned long *black_pixels,
            int *maximumDifference)
{
  long width1 = 0, height1 = 0, depth ;
  long width2, height2, depth2 ;
  long widthmin, heightmin ;
  long widthmax, heightmax ;
  long width, height ;
  long channels, i, linesize = 0, outdepth = 0;
  colorant_t *colorants1, *colorants2, *colorants ;
  double maxdiffs, alldiffs = 0 ;
  unsigned long pixdiffs = 0 ;
  unsigned long white_pixels = 0;
  struct z_stream_s zstate1, zstate2, zstateout ;
  Byte *outbuf1, *outbuf2 ;
  Byte *raster = NULL ;
  int zres ;
  int imageStreamLengthObject = CLRIP_IMAGE_STREAM_LENGTH;
  int imageStreamLengthObject2 = imageStreamLengthObject;

  outbuf1 = malloc(sizeof(Byte) * PDFBUFSIZE * 2);
  if (outbuf1 == NULL)
    error("Malloc failed.");
  outbuf2 = outbuf1 + PDFBUFSIZE;

  /* In case we bail out early */
  *percent = 100.0 ;
  *pixels = INT_MAX ;
  *maximumDifference = 0;

  /* Check basic parameters match: width, height, bitspersample, colorants. */
  if (pdfheader(in1, alpha, NULL, &width1, &height1, &depth, &colorants1))
    imageStreamLengthObject = GUI_RIP_IMAGE_STREAM_LENGTH;

  width2 = width1 ;
  height2 = height1 ;
  if (pdfheader(in2, alpha, out, &width2, &height2, &depth2, &colorants2))
    imageStreamLengthObject2 = GUI_RIP_IMAGE_STREAM_LENGTH;

  if (imageStreamLengthObject != imageStreamLengthObject2)
    error("Cannot compare output from GUI rip with output from clrip.");

  if ( width1 > width2 ) {
    widthmax = width1 ;
    widthmin = width2 ;
  } else {
    widthmax = width2 ;
    widthmin = width1 ;
  }

  if ( height1 > height2 ) {
    heightmax = height1 ;
    heightmin = height2 ;
  } else {
    heightmax = height2 ;
    heightmin = height1 ;
  }

  if ( depth != depth2 )
    error("Image depths do not match") ;

  colorants = colorants1 ; /* Save copy of colorants list */

  for ( channels = 0 ; ; ++channels ) {
    if ( colorants1 && colorants2 ) {
      if ( strcmp(colorants1->name, colorants2->name) != 0 )
        error("Colorants %s and %s do not match",
              colorants1->name, colorants2->name) ;
      colorants1 = colorants1->next ;
      colorants2 = colorants2->next ;
    } else if ( colorants1 || colorants2 ) {
      error("Number of colorants do not match") ;
    } else { /* finished with channels matching */
      break ;
    }
  }

  zstate1.zalloc = zstate2.zalloc = zstateout.zalloc = Z_NULL ;
  zstate1.zfree = zstate2.zfree = zstateout.zfree = Z_NULL ;
  zstate1.opaque = zstate2.opaque = zstateout.opaque = NULL ;

  zstate1.next_in = zstate2.next_in = zstateout.next_in = Z_NULL ;
  zstate1.avail_in = zstate2.avail_in = zstateout.avail_in = 0 ;
  zstate1.next_out = zstate2.next_out = zstateout.next_out = Z_NULL ;
  zstate1.avail_out = zstate2.avail_out = zstateout.avail_out = PDFBUFSIZE ;

  if ( inflateInit(&zstate1) != Z_OK ||
       inflateInit(&zstate2) != Z_OK )
    error("Error initialising Flate decompressor") ;

  if ( out ) {

    switch (alpha) {
    case 0:
      outdepth = 1;
      break;

    case 1:
      outdepth = 8;
      break;

    case 2:
      outdepth = 4;
      break;

    default:
      error("Unrecognised \"alpha\" value %d", alpha);
      break;
    }

    linesize = (((widthmax * outdepth) + 7) >> 3) ;

    if ( (raster = malloc(linesize)) == NULL )
      error("Memory exhausted allocating output line") ;

    if ( deflateInit(&zstateout, Z_DEFAULT_COMPRESSION) != Z_OK )
      error("Error initialising Flate compressor") ;
  }

  maxdiffs = (double)heightmax * widthmax * channels
    * ( sizematters ? ((1 << depth) - 1) : 1 );

  for ( height = 0 ; height < heightmin ; ++height ) {
    int bitsleft = 0 ; /* Bits left in current input bytes */
    Byte outmask = ' ', *outline = NULL;
    Byte inbyte1 = 0, inbyte2 = 0;

    if ( out ) {
      switch (alpha) {
      case 0:
        (void) memset((void*) raster, 0xff, linesize);
        break;

      case 1:
      case 2:
        (void) memset((void*) raster, 0x00, linesize);
        break;

      default:
        error("Unrecognised \"alpha\" value %d", alpha);
        break;
      }
      outmask = (Byte)(0xff00 >> outdepth) ;
      outline = raster ;
    }

    for ( width = 0 ; width < widthmax ; ++width ) {
      /* Init inbytes to pacify compiler; real init happens below,
       * because bitsleft==0 on entry. */
      long thisdiff = 0 ;
      long max_single_color_diff = 0;

      for ( colorants1 = colorants, i = channels ; i > 0 ; --i, colorants1 = colorants1->next ) {
        Byte diff ;

        if ( bitsleft == 0 ) {
          if ( width < width1 ) {
            zres = readdecompressedbyte(in1, &zstate1, outbuf1, &inbyte1);
          } else {
            inbyte1 = 0 ;
          }

          if ( width < width2 ) {
            zres = readdecompressedbyte(in2, &zstate2, outbuf2, &inbyte2);
          } else {
            inbyte2 = 0 ;
          }

          bitsleft = 8 ;
        }

        if ( colorants1->isnone && !nonematters ) {
          diff = 0 ; /* Ignore /None colorants */
        } else if ( width < width1 && width < width2 ) {
          diff = abs(((inbyte1 << depth) >> 8) - ((inbyte2 << depth) >> 8)) ;
          if ( diff <= tolerance )
            diff = 0;
        } else if ( sizematters ) {
          diff = (1 << depth) - 1 ;
        } else {
          diff = 0 ;
        }
        max_single_color_diff = ((diff > max_single_color_diff) ?
                                 diff :
                                 max_single_color_diff);
        thisdiff += diff ;

        /* Update the page-wide maximum difference. */
        if (diff > *maximumDifference)
          *maximumDifference = diff;

        inbyte1 <<= depth ;
        inbyte2 <<= depth ;
        bitsleft -= depth ;
      }

      if ( thisdiff ) {
        alldiffs += thisdiff ;
        ++pixdiffs ;
        if ( report && width < width1 && width < width2 ) {
          printf("(%ld,%ld) (%.2f,%.2f)%% differs by %ld over %ld channels\n",
                 width, height,
                 width * 100.0 / widthmax, height * 100.0 / heightmax,
                 thisdiff, channels) ;
        }
      }

      if ( out ) {
        /*
         * If we are producing a PDF "mask" of the differences
         * then we need to reflect this difference (if any)
         * in the output
         *
         * We have three options here
         *
         * 1) We use a simple black pixel to indicate
         *    that there was some diference at this location
         *
         * 2) We use an 8-bit value to indicate
         *    the actual diference value (from 0 to 255)
         *    with any values greater than this simply mapped to black
         *
         * 3) We use a number of well-defined colours to indicate
         *    the extent/significance of the difference
         *    rather than the exact difference value
         *
         * The value of <alpha> is used as follows:
         *
         * "0" means 1-bit monochrome
         *     i.e. black and white, where black = any amount of difference
         *
         * "1" means 8-bit actual difference as the shade of gray
         *     i.e. faint gray = small difference and black = big difference
         *
         * "2" means use a limited colour set
         *     to indicate the (quantized) magnitude of the difference
         *     rather than any direct representation of the exact difference
         */

        switch (alpha) {
        case 0:

          /* Monochrome black & white */

          if (thisdiff) {
            *outline &= ~outmask;
          }

          break;

        case 1:

          /* Actual difference in grayscale */

          *outline = ((thisdiff > 255) ? (0) : ((Byte) ~thisdiff));

          break;

        case 2:

          /*
           * "Quantized" difference
           *
           * No different => white
           *
           * Difference 1 in any one color channel => orange
           *
           * Difference 2 or 3 in any one color channel => cyan
           *
           * Difference 4, 5 or 6 in any one color channel => green
           *
           * Difference 7, 8, 9 or 10 in any one color channel => magenta
           *
           * Difference 11 to 24 in any one colour channel => red
           *
           * Difference 25 or greater in one channel => black
           */

#define ADD_PSEUDO_COLOR(LOCATION, COLOR, DEPTH, MASK, COLOR_PIXEL_COUNT) \
  (LOCATION) |= (((Byte) ((COLOR) | ((COLOR) << (DEPTH)))) & (MASK)); \
  if ((COLOR_PIXEL_COUNT) != NULL) (*(COLOR_PIXEL_COUNT))++

          /* Scale max_single_color_diff to 0..255 range */
          max_single_color_diff *= 255 / ((1 << depth) - 1) ;

          if (!max_single_color_diff) {
            ADD_PSEUDO_COLOR(*outline, INDEXED_CS_WHITE, outdepth, outmask, &white_pixels);
          } else if (max_single_color_diff == 1) {
            ADD_PSEUDO_COLOR(*outline, INDEXED_CS_ORANGE, outdepth, outmask, orange_pixels);
          } else if ((max_single_color_diff >= 2) &&
                     (max_single_color_diff <= 3)) {
            ADD_PSEUDO_COLOR(*outline, INDEXED_CS_CYAN, outdepth, outmask, cyan_pixels);
          } else if ((max_single_color_diff >= 4) &&
                     (max_single_color_diff <= 6)) {
            ADD_PSEUDO_COLOR(*outline, INDEXED_CS_GREEN, outdepth, outmask, green_pixels);
          } else if ((max_single_color_diff >= 7) &&
                     (max_single_color_diff <= 10)) {
            ADD_PSEUDO_COLOR(*outline, INDEXED_CS_MAGENTA, outdepth, outmask, magenta_pixels);
          } else if  ((max_single_color_diff >= 11) &&
                      (max_single_color_diff <= 24)) {
            ADD_PSEUDO_COLOR(*outline, INDEXED_CS_RED, outdepth, outmask, red_pixels);
          } else {
            ADD_PSEUDO_COLOR(*outline, INDEXED_CS_BLACK, outdepth, outmask, black_pixels);
          }

          break;

        default:
          error("Unrecognised \"alpha\" value %d", alpha);
          break;
        }

        outmask >>= outdepth ;
        if ( outmask == 0 ) {
          outmask = (Byte)(0xff00 >> outdepth) ;
          ++outline ;
        }
      }
    }

    if ( out ) {
      assert(outline - raster <= linesize) ;

      zstateout.next_in = raster ;
      zstateout.avail_in = linesize ;

      while ( zstateout.avail_in > 0 ) {
        zstateout.next_out = out->buffer ;
        zstateout.avail_out = PDFBUFSIZE ;

        if ( deflate(&zstateout, Z_NO_FLUSH) != Z_OK )
          error("Error compressing output") ;

        writebytes(out, (Byte *)out->buffer, PDFBUFSIZE - zstateout.avail_out) ;
      }
    }
  }

  /* Mark down non-intersecting remainder as difference or not. */
  if ( out ) {
    Byte c = 0 ;
    switch(alpha) {
    case 0:
    case 1:

      c = (sizematters ?
           0 :      /* black */
           0xff);   /* white */
      break;

    case 2:

      c = (sizematters ?
           ((INDEXED_CS_BLACK << outdepth) || INDEXED_CS_BLACK) :     /* black */
           ((INDEXED_CS_WHITE << outdepth) || INDEXED_CS_WHITE));     /* white */
      break;

    default:
      error("Unrecognised \"alpha\" value %d", alpha);
      break;
    }

    (void) memset(raster, c, linesize) ;
  }

  for ( ; height < heightmax ; ++height ) {

    if (height < height1) {
      Byte inbyte1 = 0;
      for ( width = 0 ; width < widthmax ; ++width ) {
        for ( i = channels ; i > 0 ; --i ) {
          if ( width < width1 ) {
            zres = readdecompressedbyte(in1, &zstate1, outbuf1, &inbyte1);
          }
        }
      }
    }

    if (height < height2) {
      Byte inbyte2 = 0;
      for ( width = 0 ; width < widthmax ; ++width ) {
        for ( i = channels ; i > 0 ; --i ) {
          if ( width < width2 ) {
            zres = readdecompressedbyte(in2, &zstate2, outbuf2, &inbyte2);
          }
        }
      }
    }

    if ( sizematters ) {
      long thisdiff = ((1 << depth) - 1) * widthmax * channels ;
      alldiffs += thisdiff ;
      pixdiffs += widthmax ;
      if (black_pixels != NULL) *black_pixels += widthmax;
    }

    if ( out ) {
      zstateout.next_in = raster ;
      zstateout.avail_in = linesize ;

      while ( zstateout.avail_in > 0 ) {
        zstateout.next_out = out->buffer ;
        zstateout.avail_out = PDFBUFSIZE ;

        if ( deflate(&zstateout, Z_NO_FLUSH) != Z_OK )
          error("Error compressing output") ;

        writebytes(out, (Byte *)out->buffer, PDFBUFSIZE - zstateout.avail_out) ;
      }
    }
  }

  if ( inflateEnd(&zstate1) != Z_OK ||
       inflateEnd(&zstate2) != Z_OK )
    error("Error terminating Flate decompressor") ;

  if ( out ) {
    size_t xref = 0 ;
    size_t bytes ;
    Byte *line ;
    char *start ;

    do {
      zstateout.next_out = out->buffer ;
      zstateout.avail_out = PDFBUFSIZE ;

      zres = deflate(&zstateout, Z_FINISH) ;
      if ( zres != Z_OK && zres != Z_STREAM_END )
        error("Error completing compressed output") ;

      writebytes(out, (Byte *)out->buffer, PDFBUFSIZE - zstateout.avail_out) ;
    } while ( zres != Z_STREAM_END ) ;

    if ( deflateEnd(&zstateout) != Z_OK )
      error("Error terminating Flate compressor") ;

    while ( readline(in2, &line, &bytes, &start) ) {
      long objnum ;
      int objgen ;
      char nl ;

      writebytes(out, (Byte *)line, bytes) ;

      if ( sscanf((char *)line, "%ld %d obj%c", &objnum, &objgen, &nl) == 3 &&
           iseol(nl) ) {
        if ( objnum >= MAXPDFOBJECT )
          error("PDF object number out of range in file %s", out->name) ;
        if ( objnum > maxpdfobject )
          maxpdfobject = objnum ;

        objects[objnum].type = 'n' ;
        objects[objnum].generation = (unsigned short)objgen ;
        objects[objnum].where = out->total - bytes ;

        if ( objnum == imageStreamLengthObject ) {
          int length ;

          if ( !readline(in2, &line, &bytes, &start) )
            error("Can't read stream length from file %s", in2->name) ;

          sprintf((char *)out->buffer, "%lu\n%n", zstateout.total_out, &length) ;
          writebytes(out, (Byte *)out->buffer, length) ;
        }
      } else if ( strncmp((char *)line, STRING_AND_LENGTH("xref")) == 0 ) {
        size_t lastfree = 0 ;
        int length ;

        xref = out->total - bytes ;

        sprintf((char *)out->buffer, "0 %ld\n%n", maxpdfobject + 1, &length) ;
        writebytes(out, (Byte *)out->buffer, length) ;

        for ( i = maxpdfobject ; i > 0 ; --i ) {
          if ( objects[i].type != 'n' ) {
            objects[i].type = 'f' ;
            objects[i].generation = 0 ;
            objects[i].where = lastfree ;
            lastfree = i ;
          }
        }

        objects[0].type = 'f' ;
        objects[0].generation = 0xffff ;
        objects[0].where = lastfree ;

        for ( i = 0 ; i <= maxpdfobject ; ++i ) {
          sprintf((char *)out->buffer, "%010lu %05u %c\r\n%n",
                  (unsigned long)objects[i].where,
                  (unsigned int)objects[i].generation, objects[i].type,
                  &length) ;
          writebytes(out, (Byte *)out->buffer, length) ;
        }

        for (;;) {
          if ( !readline(in2, &line, &bytes, &start) )
            error("Can't read xref table from file %s", in2->name) ;

          if ( strncmp((char *)line, STRING_AND_LENGTH("trailer")) == 0 ) {
            writebytes(out, (Byte *)line, bytes) ;
            break ;
          }
        }
      } else if ( strncmp((char *)line, STRING_AND_LENGTH("startxref")) == 0 ) {
        int length ;

        if ( !readline(in2, &line, &bytes, &start) )
          error("Can't read startxref from file %s", in2->name) ;

        sprintf((char *)out->buffer, "%lu\n%n", (unsigned long)xref, &length) ;
        writebytes(out, (Byte *)out->buffer, length) ;
      }
    }
  }

  assert(alldiffs <= maxdiffs) ;

  *percent = 100.0 * alldiffs / maxdiffs;
  *pixels = pixdiffs ;

  free(outbuf1);

  return pixdiffs != 0 ;
}

/* PDF Combining methods (using PDF optional content facility) */
#define MAX_DATA 50

typedef struct
{
  int offset;
  char *data[MAX_DATA];
  char *stream;
  int stream_length;
} OBJECT;

typedef struct
{
  OBJECT objs[MAXPDFOBJECT];
} PDF;

static void *getmem(long size)
{
  void *ptr;

  if ( (ptr = (void *)malloc(size)) == NULL )
    error("Malloc failed");
  memset(ptr,0,size);
  return ptr;
}

/** Increase the size of the passed 'buffer', updating 'currentSize' to match.
The contents of the old buffer are copied into the start of the new one, and the
remaining free space is zero initialized.
*/
static void *getMoreMemory(void* buffer, long* currentSize) {
  long oldSize = *currentSize;
  void* newBuffer = NULL;

  *currentSize = *currentSize * 2;
  newBuffer = (void*)malloc(*currentSize);

  if (newBuffer == NULL)
    error("Malloc failed");

  memcpy(newBuffer, buffer, oldSize);
  memset(((char*)newBuffer) + oldSize, 0, *currentSize - oldSize);
  return newBuffer;
}

static char *copy(char *str)
{
  char *n = getmem((long)strlen(str)+1);
  strcpy(n,str);
  return n;
}

static void read_stream(FILE *f, OBJECT *obj)
{
  int ch, bytes = 0;
  int di = 0;
  static char *done = "endstream";
  static char *data = NULL;

  /* Start with 5MB initially; we'll reallocate if it's not enough. */
  static long allocatedStreamSize = 1024 * 1024 * 5;

  if (data == NULL)
    data = getmem(allocatedStreamSize);

  for (;;) {
    ch = fgetc(f);
    if (ch == EOF)
      error("Unexpected EOF");
    data[bytes++] = ch;
    if ( bytes >= allocatedStreamSize)
      data = getMoreMemory(data, &allocatedStreamSize);

    if ( ch == done[di] )
    {
      di++;
      if ( done[di] == '\0')
      {
        bytes -= (long)strlen(done);
        obj->stream_length = bytes;
        obj->stream = getmem(bytes+1);
        memcpy(obj->stream,data,bytes);
        obj->stream[bytes] = '\0';
        while ( (ch = fgetc(f)) == '\n' || ch == '\r')
          ;
        ungetc(ch,f);
        return;
      }
    }
    else
      di = 0;
  }
}

static int get_obj(FILE *f, PDF *pdf)
{
  char line[4096];
  int n1, n2, data_i = 0;
  char eol;
  OBJECT *obj;

  while ( fgets(line,sizeof(line),f) )
  {
    if ( sscanf(line,"%d %d obj%c",&n1,&n2,&eol) == 3 )
    {
      if ( n1 >= MAXPDFOBJECT)
        error("object number too high");
      if ( n2 != 0)
        error("invalid object revision number");
      obj = &pdf->objs[n1];
      obj->offset = 1;
      while ( fgets(line,sizeof(line),f) )
      {
        if ( strstr(line, "endobj") )
          return 1;

        obj->data[data_i] = copy(line);
        data_i++;
        if (data_i >= MAX_DATA)
          error("Maximum data size exceeded.");

        if ( strstr(line,"stream") )
          read_stream(f,obj);
      }
    }
  }
  return 0;
}

static PDF *pdfparse(char *filename)
{
  FILE *f;
  PDF *pdf;

  if ( ( f = fopen(filename,"rb")) == NULL )
    return NULL;
  pdf = (PDF *)getmem(sizeof(PDF));
  while ( get_obj(f, pdf) )
    ;
  fclose(f);
  return pdf;
}

static void pdfwrite(PDF *pdf, char *outname, int root)
{
  FILE *out;
  long i,j, bytes = 0, maxobj = 0;

  if ( (out = fopen(outname,"wb")) == NULL )
    error("Cannot create output pdf");

  bytes += fprintf(out,"%%PDF-1.3\n");
  bytes += fprintf(out,"%%\xe3\xe2\xcf\xd3\n");

  for ( i=0; i < MAXPDFOBJECT; i++ )
  {
    OBJECT *obj = &pdf->objs[i];

    if (obj->offset)
    {
      maxobj = i;
      obj->offset = bytes;
      bytes += fprintf(out,"%ld 0 obj\n",i);
      for ( j=0; obj->data[j] != NULL ; j++)
        bytes += fprintf(out,"%s",obj->data[j]);
      if ( obj->stream )
      {
        bytes += (long)fwrite(obj->stream,1,obj->stream_length,out);
        bytes += fprintf(out,"endstream\n");
      }
      bytes += fprintf(out,"endobj\n");
    }
  }
  fprintf(out,"xref\n");
  fprintf(out,"0 %ld\n",maxobj+1);
  fprintf(out,"0000000000 65535 f\r\n");
  for ( i=1 ; i<=maxobj ; i++ )
    fprintf(out,"%010d 00000 n\r\n",pdf->objs[i].offset);
  fprintf(out,"trailer\n<<\n  /Size %ld\n  /Root %d 0 R\n>>\n"
             "startxref\n%ld\n%%%%EOF\n", maxobj+1, root, bytes);
  fclose(out);
}

static char *do_patch(char *s, char *from, char *to)
{
  char *p;
  char line[4096];

  if ( (p = strstr(s, from)) != NULL ) {
    strncpy(line, s, p-s);
    line[p-s] = '\0';
    strcat(line, to);
    strcat(line, p + strlen(from));
    free(s);
    s = copy(line);
    return s;
  }
  return s;
}

static void patch_str(OBJECT *obj, char *from, char *to, OBJECT *len)
{
  char line[512];
  char *s = obj->stream;

  obj->stream = do_patch(s,from,to);
  obj->stream_length = (int)strlen(obj->stream);

  free(len->data[0]);
  sprintf(line,"%d\n",obj->stream_length);
  len->data[0] = copy(line);
}

static void patch_obj(OBJECT *obj, char *from, char *to)
{
  int j;
  char *s;

  for ( j=0; (s = obj->data[j]) != NULL ; j++)
    obj->data[j] = do_patch(s,from,to);
}

static void new_obj(OBJECT *obj, char *str)
{
  obj->data[0] = copy(str);
  obj->offset = 1;
}

static int file_exists(char *name)
{
  FILE *f = fopen(name,"rb");
  if ( f == NULL )
    return 0;
  fclose(f);
  return 1;
}

/** Using the optional content feature of PDF, generate a single PDF consisting
of the passed three files, combined in the order indicated by their names.
*/
static void pdfoc(char *bottom, char *middle, char *top, char *outname,
                  int hide)
{
  PDF *before, *after, *mask, *oc;
  int root;
  int imageXObj, imageLen, pageXObj, pageLen, pagesXObj, resourcesXObj, catalogXObj;

  if (! file_exists(bottom) || ! file_exists(middle) || ! file_exists(top))
    error("Input file does not exist");

  before = pdfparse(bottom);
  after  = pdfparse(middle);
  mask   = pdfparse(top);

  /* create a new PDF by combining the three we have got */
  oc = getmem(sizeof(PDF));
  *oc = *before;

  if (before->objs[1].stream == NULL) {
    /* This is a gui RIP generated PDF. */
    if (after->objs[1].stream != NULL)
      error("Cannot compare output from GUI rip with clrip output\n");

    imageXObj = 12;
    imageLen = GUI_RIP_IMAGE_STREAM_LENGTH;
    pageXObj = 6;
    pageLen = 7;
    pagesXObj = 3;
    catalogXObj = 1;
    resourcesXObj = 5;

    if (imageXObj != 12 || pagesXObj != 3)
      error("Object indices don't match strings in code\n");

    if (before->objs[imageXObj].stream == NULL ||
        before->objs[pageXObj].stream == NULL ||
        after->objs[imageXObj].stream == NULL ||
        after->objs[pageXObj].stream == NULL ||
        mask->objs[imageXObj].stream == NULL ||
        mask->objs[pageXObj].stream == NULL) {
      /* The streams we require are missing; don't generate the composite. */
      return;
    }

    oc->objs[31] = after->objs[imageXObj];
    oc->objs[32] = after->objs[imageLen];
    patch_obj(&oc->objs[31], "9 0 R", "32 0 R");
    oc->objs[33] = mask->objs[imageXObj];
    oc->objs[34] = mask->objs[imageLen];
    patch_obj(&oc->objs[23], "9 0 R", "34 0 R");

    patch_obj(&oc->objs[resourcesXObj],"/Img1 12 0 R >>",
              "/Im1 12 0 R /Im2 31 0 R /Im3 33 0 R >>\n"
              "  /Properties << /MC1 21 0 R /MC2 22 0 R /MC3 23 0 R >>");
    patch_str(&oc->objs[pageXObj],"/Img1 Do",
              "\n/OC /MC1 BDC\n  q\n  /Im1 Do\n  Q\nEMC\n"
              "/OC /MC2 BDC\n  q\n  /Im2 Do\n  Q\nEMC\n"
              "/OC /MC3 BDC\n  q\n  /Im3 Do\n  Q\nEMC\n", &oc->objs[pageLen]);
    patch_obj(&oc->objs[catalogXObj], "3 0 R", "3 0 R\n/OCProperties 15 0 R");
    root = catalogXObj;
  }
  else {
    /* This is a clrip generated PDF. */
    if (after->objs[1].stream == NULL)
      error("Cannot compare output from clrip with GUI rip output\n");

    imageXObj = 1;
    imageLen = CLRIP_IMAGE_STREAM_LENGTH;
    pageXObj = 4;
    pageLen = 5;
    pagesXObj = 7;
    catalogXObj = 8;
    resourcesXObj = 3;

    if (imageXObj != 1 || pagesXObj != 7)
      error("Object indices don't match strings in code\n");

    if (before->objs[imageXObj].stream == NULL ||
        before->objs[pageXObj].stream == NULL ||
        after->objs[imageXObj].stream == NULL ||
        after->objs[pageXObj].stream == NULL ||
        mask->objs[imageXObj].stream == NULL ||
        mask->objs[pageXObj].stream == NULL) {
      /* The streams we require are missing; don't generate the composite. */
      return;
    }

    oc->objs[11] = after->objs[imageXObj];
    oc->objs[12] = after->objs[imageLen];
    patch_obj(&oc->objs[11],"2 0 R","12 0 R");
    oc->objs[13] = mask->objs[imageXObj];
    oc->objs[14] = mask->objs[imageLen];
    patch_obj(&oc->objs[13],"2 0 R","14 0 R");

#define CLRIP_IM_OBJ \
    "/Im1 1 0 R /Im2 11 0 R /Im3 13 0 R >>\n" \
    "  /Properties << /MC1 21 0 R /MC2 22 0 R /MC3 23 0 R >>"
    patch_obj(&oc->objs[resourcesXObj],"/Im1 1 0 R >>", CLRIP_IM_OBJ) ;
    patch_obj(&oc->objs[resourcesXObj],"/im 1 0 R >>", CLRIP_IM_OBJ) ;
#define CLRIP_IM_STR \
    "\n/OC /MC1 BDC\n  q\n  /Im1 Do\n  Q\nEMC\n"                        \
      "/OC /MC2 BDC\n  q\n  /Im2 Do\n  Q\nEMC\n"                        \
      "/OC /MC3 BDC\n  q\n  /Im3 Do\n  Q\nEMC\n"
    patch_str(&oc->objs[pageXObj],"/Im1 Do", CLRIP_IM_STR, &oc->objs[pageLen]);
    patch_str(&oc->objs[pageXObj],"/im Do", CLRIP_IM_STR, &oc->objs[pageLen]);
    patch_obj(&oc->objs[catalogXObj],"7 0 R","7 0 R\n  /OCProperties 15 0 R");
    root = catalogXObj;
  }
  new_obj(&oc->objs[15],"<< /D 16 0 R /OCGs 17 0 R >>\n");
  {
    char buffer[512] ;
    sprintf(buffer, "<< /Order 17 0 R /OFF [%s %s %s ] >>\n",
            (hide & BEFORE) ? " 18 0 R" : "",
            (hide & AFTER) ? " 19 0 R" : "",
            (hide & DIFFS) ? " 20 0 R" : "") ;
    new_obj(&oc->objs[16],buffer);
  }
  new_obj(&oc->objs[17],"[ 18 0 R 19 0 R 20 0 R ]\n");
  new_obj(&oc->objs[18],"<< /Type /OCG /Name (before) >>\n");
  new_obj(&oc->objs[19],"<< /Type /OCG /Name (after)  >>\n");
  new_obj(&oc->objs[20],"<< /Type /OCG /Name (diffs)  >>\n");
  new_obj(&oc->objs[21],"<< /Type /OCMD /OCGs [ 18 0 R ] >>\n");
  new_obj(&oc->objs[22],"<< /Type /OCMD /OCGs [ 19 0 R ] >>\n");
  new_obj(&oc->objs[23],"<< /Type /OCMD /OCGs [ 20 0 R ] >>\n");

  pdfwrite(oc, outname, root);
}


int main(int argc, char *argv[])
{
  pdffile_t out = { NULL, "-", 0, 0, 0 },
            in1 = { NULL, "-", 0, 0, 0 },
            in2 = { NULL, "-", 0, 0, 0 } ;
  double percent ;
  unsigned long pixels = 0L;
  unsigned long orange_pixels = 0L;
  unsigned long cyan_pixels = 0L;
  unsigned long green_pixels = 0L;
  unsigned long magenta_pixels = 0L;
  unsigned long red_pixels = 0L;
  unsigned long black_pixels = 0L;
  int alpha = 0, report = 0, tellpixels = 0, sizematters = 1, nonematters = 0 ;
  int tolerance = 0;
  int hide = 0 ;
  int maximumDifference = 0;
  int machineReport = 0;
  char* compositeFile = NULL;
  int producedMask = 0;
  int differ;

  out.buffer = malloc(sizeof(Byte) * PDFBUFSIZE * 3);
  if (out.buffer == NULL)
    error("Malloc failed.");
  in1.buffer = out.buffer + PDFBUFSIZE;
  in2.buffer = in1.buffer + PDFBUFSIZE;

#if defined(_MSC_VER) && _MSC_VER >= 1400
  (void)_set_printf_count_output(1) ;
#endif

  for (program = *argv++ ; --argc ; argv++ ) {
    if ( argv[0][0] == '-' ) {
      char *optarg = NULL, ch = argv[0][1] ;

      if ( strchr("ocht", ch) ) {
        if ( argv[0][2] )
          optarg = &argv[0][2] ;
        else if ( --argc )
          optarg = *++argv ;
        else
          usage() ;
      }

      switch ( ch ) {
      case 'o': /* output PDF */
        if ( (out.fp = fopen((out.name = optarg), "wb")) == NULL )
          error("can't open output file %s", optarg) ;
        producedMask = 1;
        break ;
      case 'a': /* mask is alpha */
        if (argv[0][2] && isdigit(argv[0][2])) alpha = (argv[0][2] - '0');
        else alpha = 1 ;
        break ;
      case 'r': /* report locations */
        report = 1 ;
        break ;
      case 'p': /* report number of pixels different */
        tellpixels = 1 ;
        break ;
      case 's': /* size differences don't count */
        sizematters = 0 ;
        break ;
      case 't': /* tolerance for differences */
        tolerance = atoi(optarg);
        break ;
      case 'x': /* Report machine-readable summary. */
        machineReport = 1;
        break;
      case 'c': /* Combine difference output into a composite file. */
        compositeFile = optarg ;
        break;
      case 'n': /* /None colorants should be compared. */
        nonematters = 1;
        break;
      case 'h': /* Hide this layer. */
        if ( strcmp(optarg, "before") == 0 ) {
          hide |= BEFORE;
        } else if ( strcmp(optarg, "after") == 0 ) {
          hide |= AFTER;
        } else if ( strcmp(optarg, "diffs") == 0 ) {
          hide |= DIFFS;
        } else {
          usage();
        }
        break;

      default:
        usage();
      }
    } else
      break ;
  }

  if ( argc != 2 )
    usage() ;

  if ( (in1.fp = fopen((in1.name = argv[0]), "rb")) == NULL ) {
    if ( out.fp )
      fclose(out.fp) ;
    error("can't open PDF input file %s", in1.name) ;
  }

  if ( (in2.fp = fopen((in2.name = argv[1]), "rb")) == NULL ) {
    fclose(in1.fp) ;
    if ( out.fp )
      fclose(out.fp) ;
    error("can't open PDF input file %s", in2.name) ;
  }

  differ = pdfdiff(&in1, &in2, out.fp ? &out : NULL,
                   alpha, report, sizematters, nonematters, tolerance,
                   &percent, &pixels,
                   &orange_pixels, &cyan_pixels, &green_pixels, &magenta_pixels,
                   &red_pixels, &black_pixels, &maximumDifference);

  fclose(in1.fp) ;
  fclose(in2.fp) ;
  if ( out.fp )
    fclose(out.fp) ;

  if (! differ)
    printf("PDFs %s and %s are identical\n", in1.name, in2.name) ;
  else {
    /* Combine ouput if required and possible. */
    if (compositeFile != NULL && producedMask)
      pdfoc(in1.name, in2.name, out.name, compositeFile, hide);

    if (machineReport) {
      printf("%f %lu %d\n", percent, pixels, maximumDifference);
    }
    else {
      if ( tellpixels && alpha == 2) {
        printf("PDFs %s and %s differ by %f%% (%lu pixels, %lu orange, %lu cyan, %lu green, %lu magenta, %lu red, %lu black)\n",
               in1.name, in2.name, percent, pixels,
               orange_pixels, cyan_pixels, green_pixels, magenta_pixels, red_pixels, black_pixels) ;
      }
      else if ( tellpixels ) {
        printf("PDFs %s and %s differ by %f%% (%lu pixels)\n",
               in1.name, in2.name, percent, pixels) ;
      } else {
        printf("PDFs %s and %s differ by %f%%\n", in1.name, in2.name, percent) ;
      }
    }
  }

  free(out.buffer);
  return differ;
}

/* Log stripped */
