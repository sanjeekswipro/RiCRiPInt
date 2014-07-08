/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!export:tifreadr.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to read tiff image data
 */


#ifndef __TIFREADR_H__
#define __TIFREADR_H__ (1)

#include "tifffile.h"   /* tiff_file_t */
#include "mm.h"         /* mm_pool_t */
#include "objects.h"    /* Need full OBJECT definition, and STACK typedef. */
#include "ifdreadr.h"   /* ifd_reader_t */

/** TIFF Reader context opaque type.
 */
typedef struct tiff_reader_t tiff_reader_t;


/** tiff_new_reader() creates a new tiff reader context with the given
 * buffer context.  It returns TRUE if the reader is created ok, else
 * FALSE and sets VMERROR.
 */
Bool tiff_new_reader(
  corecontext_t   *corecontext,
  tiff_file_t*    p_file,         /* I */
  mm_pool_t       mm_pool,        /* I */
  tiff_reader_t** pp_new_reader); /* O */

/** tiff_free_reader() releases a tiff reader context and any IFDs (and
 * their entries, and so forth) that have been read in.
 */
void tiff_free_reader(
  tiff_reader_t** pp_reader);     /* I */


/** tiff_read_header() reads the TIFF file Image File Header to check
 * the files endian byte order, the TIFF file check number, and the initial
 * IFD offset.  If all of these are ok, then it returns TRUE, else it returns
 * FALSE and sets IOERROR, or UNDEFINED.
 */
Bool tiff_read_header(
  corecontext_t   *corecontext,
  tiff_reader_t*  p_reader);      /* I */


/** tiff_set_image() sets the current image in the TIFF file to process.
 * The index must be in the range 1 to the number of images as returned
 * by tiff_read_ifds().
 */
Bool tiff_set_image(
  tiff_reader_t*  p_reader,       /* I */
  uint32          ifd_index);     /* I */

/** tiff_check_tiff6() checks the current selected IFD for standard TIFF6
 * entries and values.  If the IFD passes the checks the function returns
 * TRUE, else it returns FALSE and sets UNDEFINED, TYPECHECK, or RANGECHECK.
 */
Bool tiff_check_tiff6(
  corecontext_t   *corecontext,
  tiff_reader_t*  p_reader);      /* I */

/** tiff_check_exif() checks the current selected IFD for standard EXIF
 * entries and values.  If the IFD passes the checks the function returns
 * TRUE, else it returns FALSE and sets UNDEFINED, TYPECHECK, or RANGECHECK.
 */
Bool tiff_check_exif(
  corecontext_t   *corecontext,
  tiff_reader_t*  p_reader);       /* I */

/* -------------------------------------------------------------------------- */
/* Photoshop Private Info (0x8649) subtable handling */

enum {
  PSD_AlphaChannelsNames  = 0x3EE, /* Channel names, truncated to 31 chrs */
  PSD_DisplayInfo         = 0x3EF, /* Channel colors including Swatch tokens */
  PSD_UnicodeAlphaNames   = 0x415, /* Channel names in UCS16 untruncated */
  PSD_AlphaIdentifiers    = 0x41D, /* 32bit unique IDs for each channel */
  PSD_AlternateSpotColors = 0x42B  /* 32bit unique IDs and Lab colors */
};

enum PhotoShopColorSpace {
  PhotoShop_RGB = 0,     /* [0..255]*257 x3, 0 */
  PhotoShop_HSB,         /* [0..359]/360*65536, [0..100]*655.35 x2, 0 */
  PhotoShop_CMYK,        /* [0..100]*655.35 x4 */
  PhotoShop_Pantone,     /* six alphanumeric characters, 0 */
  PhotoShop_Focoltone,   /* six alphanumeric characters, 0 */
  PhotoShop_Trumatch,    /* six alphanumeric characters, 0 */
  PhotoShop_Toyo,        /* six alphanumeric characters, 0 */
  PhotoShop_Lab,         /* [0..100]*100, [-128..127]*100 x2, 0 */
  PhotoShop_Gray,        /* [0..100]*100 but apparently unsupported */
  PhotoShop_WideCMYK,    /* unknown */
  PhotoShop_HKS,         /* six alphanumeric characters, 0 */
  PhotoShop_DIC,         /* six alphanumeric characters, 0 */
  PhotoShop_TotalInk,    /* six alphanumeric characters, 0 */
  PhotoShop_MonitorRGB,  /* unknown */
  PhotoShop_Duotone,     /* unknown */
  PhotoShop_Opacity,     /* unknown */
  PhotoShop_Web,         /* unknown */
  PhotoShop_GrayFloat,   /* unknown */
  PhotoShop_RGBFloat,    /* unknown */
  PhotoShop_OpacityFloat,/* unknown */
  PhotoShop_ANPA = 3000  /* six alphanumeric characters, 0 */
};

/* Macro to emit compile time failure if sizeof is not the required size.
   Especially useful where mapping file contents to structures */
#define REQUIRE_SIZEOF(type_,size_) \
  typedef char wrong_##type_##size[2*(sizeof(type_)==size_)-1]

typedef struct {    /* 0x3EF - array of... */
  int16 colorSpace; /* 0-3, 7 */
  uint16 color[4];  /* Other than Lab's a & b, these are always unsigned */
  int16 opacity;    /* 0..100 - we can't use this unfortunately */
  char type;        /* 0=selected, 1=masked, 2=spot */
  char padding;     /* should be zero */
} PSDisplayInfo ;
REQUIRE_SIZEOF(PSDisplayInfo, 14) ; /* Array of 14 byte structures in file */

enum { /* PSDisplayInfo channel types */
  PSD_selection = 0,
  PSD_mask,
  PSD_spot
} ;

typedef struct {      /* 0x42B - two shorts then array of... */
  uint16 id1 ;        /* 32bit split to ensure sizeof is 14 bytes */
  uint16 id2 ;        /* As per 0x41d AlphaIdentifiers table */
  int16  colorSpace;  /* 7=Lab - other spaces not expected here */
  uint16 color[4];    /* L,a,b,0 */
} PSAlternateSpots ;
REQUIRE_SIZEOF(PSAlternateSpots, 14) ; /* Array of 14 byte structures in file */

enum {
  e_photoshopinks_none,
  e_photoshopinks_gray,
  e_photoshopinks_cmy,
  e_photoshopinks_cmyk,
  e_photoshopinks_rgb
};

/* -------------------------------------------------------------------------- */
/*
 * Struct to hold numbers and things for the image dictionary
 */
typedef struct {
  uint32          image_width;        /* in pixels */
  uint32          image_length;       /* in pixels */
  uint32          bits_per_sample;
  uint32          samples_per_pixel;  /* total num samples including extras */
  uint32          extra_samples;
  uint32          orientation;
  uint32          pmi;                /* PhotometricInterpretation */
  uint32          compression;
  uint32          res_unit;           /* Units for the ... */
  double          x_resolution;       /* ... number of x pixels per unit */
  double          y_resolution;       /* ... number of y pixels per unit */
  Bool            defaulted_resolution; /* true if the resolution above is a default */
  double          scale_factor;
  uint32          dot_range[2];
  uint32          predictor;          /* for LZW and FLATE */
  uint32          ccitt_t4options;
  uint32          ccitt_t6options;
  int32           f_icc_profile;
  uint32          icc_offset;
  uint32          icc_length;
  uint32          jpeg_qtable_size;
  uint8**         pp_jpeg_qtable;
  OBJECT          jpegtables;
  uint32          fill_order;
  uint32          planar_config;
  uint8*          colormap;           /* CLUT for palette-color images */
  int32           f_plain;            /* FALSE when we have extra samples representing spot colors, TRUE otherwise) */

  /* if f_plain == FALSE, then inknames is a string containing the ink names.
     inknameslen is the string length including NULL terminators that separate
     names. [e.g. "Cyan0Magenta0Silver0..."].
     Technically these are channels, only those marked as spots will be
     considered as inks. */
  uint8*            inknames;
  uint32            inknameslen;
  PSDisplayInfo*    inkdetails;      /* From 0x3EF */
  uint32            PSDchannels;     /* From 0x3EE, 0x3EF & 0x42B */
  uint32            PSDspots;        /* ditto */
  uint32            PSDmasks;        /* ditto */
  uint32            photoshopinks;
  COLORANTINDEX*    colindices;

  struct          cmyk_spot_data   *sp_data;
  int32           byte_index_within_sample; /* only used to skip extra samples */

  /* used by TIFF6 */
  uint32          number_strips ;
  uint32          rows_per_strip ;
  uint32          bytes_per_row ;
  Bool            shortswap ;
  tiff_rational_t whitepoint_x;       /* for Lab colorspace */
  tiff_rational_t whitepoint_y;
  uint32          strip_count;
  uint32*         strip_offset;
  uint32          strip_bytecount;
  uint32*         strip_bytes;

  Bool            alpha;
  Bool            premult;
} tiff_image_data_t;

ifd_reader_t* ifd_reader_from_tiff(tiff_reader_t*  p_reader);
ifd_ifd_t*  tiff_current_ifd(tiff_reader_t*  p_reader);

/** tiff_get_image_data() extracts tiff image data for the current IFD
 * from its entries or default values if they are not present.
 */
Bool tiff_get_image_data(
  corecontext_t       *corecontext,
  tiff_reader_t*      p_reader,   /* I */
  tiff_image_data_t*  p_data);    /* O */

void tiff_release_image_data(
  tiff_reader_t*      p_reader,
  tiff_image_data_t*  p_data );

/** tiff_free_image_data() releases any memory that may have been allocated
 * for the image data.
 */
void tiff_free_image_data(
  tiff_reader_t*      p_reader,     /* I */
  tiff_image_data_t*  p_data);    /* O */


/*
 * tiff_setup_read()
 * tiff_read_file()
 * tiff_byte_flip()
 */
Bool tiff_setup_read_data(
  corecontext_t       *corecontext,
  tiff_reader_t*      p_reader,   /* I/O */
  tiff_image_data_t*  p_data);    /* I/O */

Bool tiff_setup_read(
  corecontext_t       *corecontext,
  tiff_reader_t*      p_reader,   /* I/O */
  tiff_image_data_t*  p_data,     /* I */
  OBJECT*             ofile);  /* I */

Bool tiff_read_file(/*@in@*/ /*@notnull@*/ tiff_reader_t* p_reader,
                    /*@in@*/ /*@notnull@*/ STACK *stack);

Bool tiff_byte_flip(/*@in@*/ /*@notnull@*/ tiff_reader_t* p_reader,
                    /*@in@*/ /*@notnull@*/ STACK *stack);

/** \brief Shift a buffer of TIFF data from signed TIFF range to unsigned
    index range.

    Lab images are indexed [-128,127] or [-32768,32767] but our 8 and 16 bit
    maps run so that (8 bit example) [0,255] maps to [-128,127]. So we need
    to add 128 to the indices. Note that the L component is ok running
    [0,255] mapping [0,100].

    \param bits_per_sample The number of bits per sample, either 8 or 16.

    \param buffer A pointer to a buffer of sample data.

    \param bytes The length of the sample data in bytes. For 16-bit data, this
    must be a multiple of 2.

    \param component On input this is the channel index of the first sample
    of data in the buffer (between 0 and 2). On output, it will be set to the
    channel index of the next sample after this buffer. For planar data, the
    value is not changed, since all samples are in the same component. For
    chunky data, it allows this routine to handle Lab data in arbitrary sized
    chunks, not just multiples of three samples.

    \param chunky A flag indicating whether the data is in chunky or planar
    format.

*/
void tiff_Lab_shift(uint32 bits_per_sample,
                    /*@notnull@*/ /*@in@*/ uint8*  buffer,
                    uint32  bytes,
                    /*@notnull@*/ /*@in@*/ uint32 *component,
                    Bool chunky) ;

void do_byteflip(
  uint8*              p_buffer,   /* I/O */
  uint32              len);       /* I */

/** \brief Setup filter parameter dictionary for CCITT compression. */
Bool tiff_ccitt_filter(/*@notnull@*/ /*@in@*/ mm_pool_t mm_pool,
                       /*@notnull@*/ /*@in@*/ tiff_image_data_t *p_data,
                       uint32 rows_per_strip,
                       /*@notnull@*/ /*@out@*/ OBJECT *odict) ;

/** \brief Setup filter parameter dictionary for CCITT4 compression. */
Bool tiff_ccitt4_filter(/*@notnull@*/ /*@in@*/ mm_pool_t mm_pool,
                        /*@notnull@*/ /*@in@*/ tiff_image_data_t *p_data,
                        uint32 rows_per_strip,
                        /*@notnull@*/ /*@out@*/ OBJECT *odict) ;

/** \brief Setup filter parameter dictionary for CCITT6 compression. */
Bool tiff_ccitt6_filter(/*@notnull@*/ /*@in@*/ mm_pool_t mm_pool,
                       /*@notnull@*/ /*@in@*/ tiff_image_data_t *p_data,
                       /*@notnull@*/ /*@out@*/ OBJECT *odict) ;

/** \brief Setup filter parameter dictionary for JPEG compression. */
Bool tiff_jpeg_filter(/*@notnull@*/ /*@in@*/ mm_pool_t mm_pool,
                      /*@notnull@*/ /*@in@*/ tiff_image_data_t *p_data,
                      /*@notnull@*/ /*@out@*/ OBJECT *odict,
                      Bool f_oldmethod) ;

/** \brief Setup filter parameter dictionary for LZW compression. */
Bool tiff_lzw_filter(/*@notnull@*/ /*@in@*/ mm_pool_t mm_pool,
                     /*@notnull@*/ /*@in@*/ tiff_image_data_t *p_data,
                     uint32 eodcount,
                     Bool bigendian,
                     /*@notnull@*/ /*@out@*/ OBJECT *odict) ;

/** \brief Setup filter parameter dictionary for FLATE compression. */
Bool tiff_flate_filter(/*@notnull@*/ /*@in@*/ mm_pool_t mm_pool,
                       /*@notnull@*/ /*@in@*/ tiff_image_data_t *p_data,
                       /*@notnull@*/ /*@out@*/ OBJECT *odict) ;

/** \brief Setup filter parameter dictionary for subfiledecode filter. */
Bool tiff_subfiledecode_filter( /*@notnull@*/ /*@in@*/ mm_pool_t mm_pool,
                                /*@notnull@*/ /*@out@*/ OBJECT* odict);

#endif /* !__TIFREADR_H__ */


/* Log stripped */
