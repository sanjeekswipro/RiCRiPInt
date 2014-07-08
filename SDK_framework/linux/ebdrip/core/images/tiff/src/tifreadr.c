/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:tifreadr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * TIFF file reader.
 */

#include "core.h"
#include "swerrors.h"   /* IOERROR */
#include "swcopyf.h"    /* swcopyf() */
#include "objects.h"    /* OBJECT */
#include "objstack.h"   /* STACK */
#include "fileio.h"     /* FILELIST */
#include "monitor.h"
#include "tables.h"     /* reversed_bits_in_byte */
#include "caching.h"    /* PENTIUM_CACHE_LOAD */
#include "namedef_.h"   /* NAME_... */

#include "lists.h"      /* DLL_ */

#include "t6params.h"   /* TIFF6Params */
#include "tiffmem.h"    /* tiff_alloc_psdict() */
#include "ifdreadr.h"
#include "tifftags.h"
#include "tifreadr.h"
#include "hqmemcmp.h"   /* HqMemCmp() */
#include "hqmemcpy.h"   /* HqMemCpy */
#include "hqmemset.h"

#if defined( ASSERT_BUILD )
Bool tiff_debug = FALSE;
#endif


/*
 * Yer useful macro for getting number of array entries the lazy way
 */
#define TIFF_DPI_POINTS       (72.0)
#define TIFF_CM_TO_INCH       (2.54)

/* TIFF_MAX_BYTES_READ divisible by 2 and 3 so we can potentially
   make shorts big-endian and handle Lab (where all 3 channels need
   to be present when interleaved */
#define TIFF_MAX_BYTES_READ   (65532)
#define TIFF_LZW_DICT_SIZE    (6)
#define TIFF_JPEG_DICT_SIZE   (4)
#define TIFF_CCITT_DICT_SIZE  (6)
#define TIFF_CCITT4_DICT_SIZE (8)
#define TIFF_CCITT6_DICT_SIZE (4)
#define TIFF_FLIP_DICT_SIZE   (2)
#define TIFF_FLATE_DICT_SIZE  (4)

/*even so we can potentially make shorts big-endian*/
#define TIFF_MAX_STRIP_BYTES 65534

/** Number of bytes in a TIFF JPEG QTable for a channel
 */
#define TIFF_JPEGQTABLES_SIZE    (64u)

/*
 * Macro to test for single bits being set
 */
#define BIT_SET(v, i)   (((v)&BIT(i)) != 0)


/** Structure to hold state of strip reading.  Basically an image strip will be
 * read as a number of bytes_per_read file reads and a final bytes_last_read
 * to finish the strip off.  If read_count is 1 then we can read the image
 * strip in one go.
 */
typedef struct {
  uint32    current_read;     /* Number of read within strip - 1 based */
  uint32    reads_per_strip;  /* Total number of reads requird for whole strip */
  uint32    bytes_per_read;   /* Normal bytes per read */
  uint32    bytes_last_read;  /* Bytes for last read to end of strip */
  uint32    rows;             /* Number of rows in strip */
} tiff_strip_read_t;

#define TIFF_STRIP_LAST_READ(p_strip) ((p_strip)->current_read == (p_strip)->reads_per_strip)
#define TIFF_STRIP_FINISHED(p_strip)  ((p_strip)->current_read > (p_strip)->reads_per_strip)

/** TIFF planar image strip component state information.
 */
typedef struct {
  uint32            offset;
                  /* Offset to start of strip component NB: updated by reader! */
  uint32            bytecount;
                      /* Strip component byte count - used when  byte flipping */
  FILELIST*         file_read;
                             /* File to read decoded strip component data from */
  OBJECT            ostring;
                        /* PS string to return decoded strip component data in */
  OBJECT            ostring_flip;
                                  /* PS string to return flipped image data in */
} tiff_component_t;

/** TIFF image data state
 */
typedef struct {
  uint32            current_strip;               /* Strip currently being read */
  uint32            number_strips;               /* Number of strips for image */
  tiff_strip_read_t image_strip;      /* State for reading normal image strips */
  tiff_strip_read_t last_image_strip;
                               /* State for reading last or single image strip */
  uint32            current_component;
                              /* Index of strip component currently being read */
  uint32            number_components;
                                     /* Number of strip components of interest */
  tiff_component_t* component;           /* Pointer to current strip component */
  tiff_component_t* components;           /* Array of strip components to read */

  FILELIST*         flip_read; /* The source file to read tiff image data from */

  uint8*            p_flipmem;                   /* PS byte flip string memory */
  size_t            flipmem_size;
                        /* Size of PS string memory allocated for flip strings */
  size_t            flip_length;    /* Length of image byte flip string buffer */

  uint8*            p_strmem;                              /* PS string memory */
  size_t            strmem_size;         /* Size of PS string memory allocated */
  size_t            string_length;            /* Length of image string buffer */

  OBJECT            ofile_tiff;                        /* The actual TIFF file */
  ifd_ifdentry_t*  p_ifdentry_offsets;               /* Strip offset IFD entry */
  ifd_ifdentry_t*  p_ifdentry_bytecounts;         /* Strip bytecount IFD entry */
  Bool             f_planar;
                         /* Strips are in planar format (false implies chunky) */

  OBJECT            flip_source;              /* Flip filter data source array */
  OBJECT            odict_flip;                            /* Flip filter dict */

  Bool              f_decode;                            /* Decode filter used */
  Bool              f_need_close;
                        /* Should we try see if filter open so should close it */
  OBJECT            odict_decode;                        /* Decode filter dict */
  OBJECT            oname_decode;                        /* Decode filter name */
  Bool              f_update_rows;
                           /* Filter should have Rows entry set for last strip */
  Bool              f_update_eodcount;
                             /* Filter should have EODCount set for last strip */
  uint32            last_strip_eodcount;
                               /* Number of bytes in last strip after decoding */
  OBJECT            odict_sfd;        /* Compressed strip subfiledecode filter */
} tiff_file_data_t;


/** TIFF reader context
 */
struct tiff_reader_t {
  mm_pool_t         mm_pool;            /* MM pool to do all allocations in */
  tiff_file_t*      p_file;             /* The TIFF file */
  tiff_file_data_t  read_data;          /* Image read state */
  uint32            lab_shift;          /* 0 = don't shift,
                                           8 = shift 8 bit Lab image,
                                          16 = shift 16 bit Lab */
  Bool              bigendian;          /* true if file is bigendian */
  Bool              localendian;        /* true if file is local endian */
  Bool              shortswap;          /* true if we need to swap bytes in a short */
  ifd_reader_t      *ifd_reader;        /* the thing that does the hard work */
};

extern ifd_ifdentry_data_t tiff6_known_tags[];

static uint32 resolution_units[] = {
  RESUNIT_NONE, RESUNIT_INCH, RESUNIT_CM
};
static uint32 dot_range[] = {
  0, 255
};
static uint32 bits_per_sample[] = {
  1, 1, 1, 1
};
static uint32 compressions[] = {
  COMPRESS_None,
  COMPRESS_CCITT,
  COMPRESS_CCITT_T4,
  COMPRESS_CCITT_T6,
  COMPRESS_LZW,
  COMPRESS_JPEG_OLD,
  COMPRESS_JPEG,
  COMPRESS_FLATE,
  COMPRESS_FLATE_TIFFLIB,
  COMPRESS_Packbits
};
static uint32 photometric_interpretations[] = {
  PMI_WhiteIsZero,
  PMI_BlackIsZero,
  PMI_RGB,
  PMI_Separated,
  PMI_Palette,
  PMI_YCbCr,
  PMI_CIE_L_a_b_,
  PMI_ICCLab
};

/* NB: we allow orientation value 0 in to support Sony CyberShot
   JPEG images that supply it in APP1 TIFF data. We will treat
   these as ENTRY_DEF_ORIENTATION unless running in strict mode
   when we will instead fail the file. Currently orientation is
   not used by us for JPEG but we do want to at least accept this
   value. */
static uint32 orientations[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8
};
static uint32 extrasamples[] = {
  EXTRASAMPLES_UNSPECIFIED_DATA,
  EXTRASAMPLES_ASSOCIATED_ALPHA,
  EXTRASAMPLES_UNASSOCIATED_ALPHA
};
static uint32 predictor[] = {
  1, 2
};

static uint32 fill_orders[] = {
  FILLORDER_MSB_TO_LSB, FILLORDER_LSB_TO_MSB
};
static uint32 planar_configs[] = {
  PLANAR_CONFIG_CHUNKY,
  PLANAR_CONFIG_PLANAR
};

static uint32 tiff_inksets[] = {INKSET_CMYK, INKSET_NotCMYK};

static ifd_ifdentry_check_t tiff6_defaults[] = {
    { TAG_NewSubfileType,
      HFD,          TB_L,   TB_BS, 1, 0, },
    { TAG_ImageWidth,
      HFM,          TB_SL,  TB_B,  1, },
    { TAG_ImageLength,
      HFM,          TB_SL,  TB_B,  1, },
    { TAG_BitsPerSample,
      HFD|HFA,      TB_S,   TB_BL, NUM_ARRAY_ITEMS(bits_per_sample), ENTRY_DEF_BITS_PER_SAMPLE, bits_per_sample },
    /* Note: if compression is a BYTE then PACKBITS cannot be detected */
    { TAG_Compression,
      HFD|HFI|HFO,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(compressions), ENTRY_DEF_COMPRESSION, compressions },
    { TAG_PhotometricInterpretation,
      HFM|HFI|HFO,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(photometric_interpretations), PMI_WhiteIsZero, photometric_interpretations },
    { TAG_FillOrder,
      HFD|HFI|HFO,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(fill_orders), ENTRY_DEF_FILLORDER, fill_orders},
    { TAG_StripOffsets,
      HFM,          TB_SL,  TB_B,  0, },
    { TAG_Orientation,
      HFD|HFO|HFI,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(orientations), ENTRY_DEF_ORIENTATION, orientations },
    { TAG_SamplesPerPixel,
      HFD,          TB_S,   TB_BL, 1, 0, },
    { TAG_RowsPerStrip,
      HFD,          TB_SL,  TB_B,  1, 0xffffffff, },
    { TAG_StripByteCounts,
      HFO,          TB_SL,  TB_B,  0, },
    { TAG_XResolution,
      HFO,          TB_R,   0, 0, },
    { TAG_YResolution,
      HFO,          TB_R,   0, 0, },
    { TAG_PlanarConfiguration,
      HFD|HFI|HFO,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(planar_configs), ENTRY_DEF_PLANARCONFIG, planar_configs },
    { TAG_T4Options,
      HFD,          TB_L,   TB_BS, 1, ENTRY_DEF_T4OPTIONS, },
    { TAG_T6Options,
      HFD,          TB_L,   TB_BS, 1, ENTRY_DEF_T6OPTIONS, },
    { TAG_ResolutionUnit,
      HFD|HFI|HFO,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(resolution_units), ENTRY_DEF_RESOLUTION_UNITS, resolution_units },
    { TAG_Predictor,
      HFD|HFO,      TB_S,   TB_BL, NUM_ARRAY_ITEMS(predictor), ENTRY_DEF_PREDICTOR, predictor },
    { TAG_InkSet,
      HFO|HFI,      TB_S,   TB_BL, NUM_ARRAY_ITEMS(tiff_inksets), ENTRY_DEF_INKSET,tiff_inksets },
    { TAG_NumberOfInks,
      HFA,      TB_S,   TB_BL, 1, 4, },
    { TAG_InkNames,
      HFD,      TB_A,   TB_B, },
    { TAG_DotRange,
      HFD|HFA,      TB_BS,  TB_L,  NUM_ARRAY_ITEMS(dot_range), 0, dot_range },
    { TAG_ExtraSamples,
      HFO,          TB_S,   TB_BL, NUM_ARRAY_ITEMS(extrasamples), 0, extrasamples },
    { TAG_WhitePoint,
      HFD|HFA,      TB_R,   0, 0 }
};


static ifd_ifdentry_check_t exif_defaults[] = {
    { TAG_NewSubfileType,
      HFD,          TB_L,   TB_BS, 1, 0, },
    { TAG_BitsPerSample,
      HFD|HFA,      TB_S,   TB_BL, NUM_ARRAY_ITEMS(bits_per_sample), ENTRY_DEF_BITS_PER_SAMPLE, bits_per_sample },
    /* Note: if compression is a BYTE then PACKBITS cannot be detected */
    { TAG_Compression,
      HFD|HFI|HFO,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(compressions), ENTRY_DEF_COMPRESSION, compressions },
    { TAG_PhotometricInterpretation,
      HFI|HFO,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(photometric_interpretations), PMI_WhiteIsZero, photometric_interpretations },
    { TAG_Orientation,
      HFD|HFO|HFI,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(orientations), ENTRY_DEF_ORIENTATION, orientations },
    { TAG_SamplesPerPixel,
      HFD,          TB_S,   TB_BL, 1, 0, },
    { TAG_RowsPerStrip,
      HFD,          TB_SL,  TB_B,  1, 0xffffffff, },
    { TAG_XResolution,
      HFO,          TB_R,   0, 0, },
    { TAG_YResolution,
      HFO,          TB_R,   0, 0, },
    { TAG_T4Options,
      HFD,          TB_L,   TB_BS, 1, ENTRY_DEF_T4OPTIONS, },
    { TAG_T6Options,
      HFD,          TB_L,   TB_BS, 1, ENTRY_DEF_T6OPTIONS, },
    { TAG_ResolutionUnit,
      HFD|HFI|HFO,  TB_S,   TB_BL, NUM_ARRAY_ITEMS(resolution_units), ENTRY_DEF_RESOLUTION_UNITS, resolution_units },
    { TAG_Predictor,
      HFD|HFO,      TB_S,   TB_BL, NUM_ARRAY_ITEMS(predictor), ENTRY_DEF_PREDICTOR, predictor },
    { TAG_InkSet,
      HFO|HFI,      TB_S,   TB_BL, NUM_ARRAY_ITEMS(tiff_inksets), ENTRY_DEF_INKSET,tiff_inksets },
    { TAG_NumberOfInks,
      HFA,      TB_S,   TB_BL, 1, 4, },
    { TAG_InkNames,
      HFD,      TB_A,   TB_B, },
    { TAG_DotRange,
      HFD|HFA,      TB_BS,  TB_L,  NUM_ARRAY_ITEMS(dot_range), 0, dot_range },
    { TAG_ExtraSamples,
      HFO,          TB_S,   TB_BL, NUM_ARRAY_ITEMS(extrasamples), 0, extrasamples },
    { TAG_WhitePoint,
      HFD|HFA,      TB_R,   0, 0 }
};


static uint32 pmi_planes[PMI_MaxCount] = {    /* keyed off PhotometricInterpretation */
  1,  /* WhiteIsZero */
  1,  /* BlackIsZero */
  3,  /* RGB */
  1,  /* Palette */
  0,  /* Mask */
  4,  /* Separations: CMYK if InkSet = 1*/
  3,  /* YCbCr */
  0,  /* Undefined */
  3,  /* CIELab */
  3   /* ICCLab */
};

/* Convenience access procedures, they could be defined as MACROs but I don't like
such things, the compiler will be clever enough to optimize this. */

ifd_reader_t* ifd_reader_from_tiff(tiff_reader_t*  p_reader)
{
  return p_reader->ifd_reader;
}

static inline uint32 tiff_first_ifd_offset(tiff_reader_t*  p_reader)
{
  return first_ifd_offset(p_reader->ifd_reader);
}

ifd_ifd_t*  tiff_current_ifd(tiff_reader_t*  p_reader)
{
  return current_ifd(p_reader->ifd_reader);
}

static inline uint32 tiff_current_ifd_index(tiff_reader_t*  p_reader)
{
  return current_ifd_index(p_reader->ifd_reader);
}

static inline uint32 tiff_count_ifds(tiff_reader_t*  p_reader)
{
  return count_ifds(p_reader->ifd_reader);
}

static inline dll_list_t* tiff_dls_ifds(tiff_reader_t*  p_reader)
{
  return dls_ifds(p_reader->ifd_reader);
}

static inline void tiff_set_ifd_iterator(tiff_reader_t* p_reader, ifd_ifd_t* p_ifd, uint32 ifd_index)
{
  set_ifd_iterator(p_reader->ifd_reader, p_ifd, ifd_index);
}

static inline void tiff_set_first_ifd_offset(tiff_reader_t* p_reader, tiff_long_t f_off)
{
  set_first_ifd_offset(p_reader->ifd_reader,f_off);
}

/** tiff_new_reader() creates a new tiff reader context with the given
 * buffer context.  It returns TRUE if the reader is created ok, else
 * FALSE and sets VMERROR.
 */
Bool tiff_new_reader(
  corecontext_t   *corecontext,
  tiff_file_t*    p_file,         /* I */
  mm_pool_t       mm_pool,        /* I */
  tiff_reader_t** pp_new_reader)  /* O */
{
  TIFF6PARAMS *tiff6params = corecontext->tiff6params;
  tiff_reader_t*  p_reader;

  HQASSERT((p_file != NULL),
           "tiff_new_reader: NULL file pointer");
  HQASSERT((pp_new_reader != NULL),
           "tiff_new_reader: NULL pointer to returned pointer");

  p_reader = mm_alloc(mm_pool, sizeof(tiff_reader_t),
                      MM_ALLOC_CLASS_TIFF_READER);
  if ( p_reader == NULL ) {
    return error_handler(VMERROR);
  }

  /* Initialise the reader */
  p_reader->mm_pool           = mm_pool;
  p_reader->p_file            = p_file;

  /* Initialise sufficient bits of read_data to ensure
   * tiff_free_reader_elements() works */
  p_reader->read_data.p_strmem = NULL;
  p_reader->read_data.components = NULL;

  p_reader->read_data.p_flipmem = NULL;
  p_reader->read_data.ofile_tiff = p_reader->read_data.flip_source =
    p_reader->read_data.odict_flip = p_reader->read_data.odict_decode =
    p_reader->read_data.oname_decode = p_reader->read_data.odict_sfd =
    onull ; /* Struct copy to set slot properties */

  p_reader->lab_shift = 0; /* ie no shift */
  p_reader->bigendian = FALSE;
  p_reader->shortswap = FALSE;

  if (! ifd_new_reader(p_file,
                       mm_pool,
                       tiff6_known_tags,
                       tiff6params->f_abort_on_unknown,
                       tiff6params->f_strict,
                       tiff6params->f_verbose,
                       &p_reader->ifd_reader)) {
    return FALSE;
  }

  /* Return pointer and success flag */
  *pp_new_reader = p_reader;
  return TRUE ;

} /* Function tiff_new_reader */


/** Free elements allocated in tiff_setup_reader*/
void tiff_free_reader_elements(tiff_reader_t* p_reader)
{

  if ( p_reader->read_data.components != NULL ) {
    /* Created buffer for image string - free it off */
    mm_free(p_reader->mm_pool, p_reader->read_data.components,
            (p_reader->read_data.number_components)*sizeof(tiff_component_t));
  }

  if ( p_reader->read_data.p_strmem != NULL ) {
    /* Created buffer for image string - free it off */
    mm_free(p_reader->mm_pool, p_reader->read_data.p_strmem, p_reader->read_data.strmem_size);
  }

  if ( oType(p_reader->read_data.odict_decode) == ODICTIONARY ) {
    /* Free any compression decode filter dictionary */
    tiff_free_psdict(p_reader->mm_pool, &(p_reader->read_data.odict_decode));
  }

  if ( oType(p_reader->read_data.odict_sfd) == ODICTIONARY ) {
    /* Free any SFD filter dictionary */
    tiff_free_psdict(p_reader->mm_pool, &(p_reader->read_data.odict_sfd));
  }

  if ( p_reader->read_data.p_flipmem != NULL ) {
    /* Created buffer for flipping data - free it off */
    mm_free(p_reader->mm_pool, p_reader->read_data.p_flipmem, p_reader->read_data.flipmem_size);
  }

  if ( oType(p_reader->read_data.flip_source) == OARRAY ) {
    /* Free off PS structs used */
    tiff_free_psarray(p_reader->mm_pool, &(p_reader->read_data.flip_source));
  }

  if ( oType(p_reader->read_data.odict_flip) == ODICTIONARY ) {
    /* Free any flip filter dictionary */
    tiff_free_psdict(p_reader->mm_pool, &(p_reader->read_data.odict_flip));
  }
}

/** tiff_free_reader() releases a tiff reader context and any IFDs (and
 * their entries, and so forth) that have been read in.
 */
void tiff_free_reader(
  tiff_reader_t** pp_reader)      /* I */
{
  tiff_reader_t*  p_reader;

  HQASSERT((pp_reader != NULL),
           "tiff_free_reader: NULL pointer to context pointer");
  HQASSERT((*pp_reader != NULL),
           "tiff_free_reader: NULL context pointer");

  p_reader = *pp_reader;

  ifd_free_reader(&p_reader->ifd_reader);
  tiff_free_reader_elements(p_reader);

  /* Free of memory and NULL original pointer */
  mm_free(p_reader->mm_pool, p_reader, sizeof(tiff_reader_t));
  *pp_reader = NULL;

} /* Function tiff_free_reader */


/** tiff_read_header() reads the TIFF file Image File Header to check
 * the files endian byte order, the TIFF file check number, and the initial
 * IFD offset.  If all of these are ok, then it returns TRUE, else it returns
 * FALSE and sets IOERROR, or UNDEFINED.
 */
Bool tiff_read_header(
  corecontext_t   *corecontext,
  tiff_reader_t*  p_reader)       /* I */
{
  TIFF6PARAMS *tiff6params = corecontext->tiff6params;
  static uint32 check_word = (((tiff_byte_t)'M' << 24) + (tiff_byte_t)'I');
                                      /* Was computed at run-time - necessary? */
  tiff_short_t  magic;
  Bool          f_local_endian;
  tiff_byte_t   check_bytes[2];
  tiff_long_t   f_off; /* sorry couldn't resist it */

  HQASSERT((p_reader != NULL),
           "tiff_read_header: NULL context pointer");

  /* The header is at the beginning - duh! */
  if ( !tiff_file_seek(p_reader->p_file, 0) ) {
    return detail_error_handler(IOERROR, "Unable to set position to start of TIFF file.") ;
  }

  /* Check for valid lead bytes */
  if ( !tiff_get_byte(p_reader->p_file, 2, &check_bytes[0]) ) {
    return FALSE ;
  }
  if ( (check_bytes[0] != check_bytes[1]) ||
       ((check_bytes[0] != (tiff_byte_t)'I') && (check_bytes[0] != (tiff_byte_t)'M')) ) {
    return detail_error_handler(UNDEFINED, "TIFF endian check bytes not present.") ;
  }

  /* if little-endian then may need to swap shorts in 16 bit data later */
  p_reader->bigendian = (check_bytes[0] == (tiff_byte_t)'M');

  /* Determine if file data endianism matches the machines */
  f_local_endian = (check_bytes[0] == *((tiff_byte_t*)&check_word));

  p_reader->localendian = f_local_endian;

  tiff_set_endian(p_reader->p_file, f_local_endian);
  if ( tiff6params->f_verbose ) {
    monitorf((uint8*)"tiffexec: file is%s in this machines endian format.\n",
             (f_local_endian ? (uint8*)"" : (uint8*)" not"));
  }

  /* Check the TIFF magic value */
  if ( !tiff_get_short(p_reader->p_file, 1, &magic) ) {
    return FALSE ;
  }
  if ( magic != (tiff_short_t)42 ) {
    return detail_error_handler(UNDEFINED, "TIFF file check value not present.") ;
  }

  /* Extract offset to first ifd */
  if ( !tiff_get_long(p_reader->p_file, 1, &f_off) ) {
    return FALSE ;
  }

  tiff_set_first_ifd_offset(p_reader, f_off);

  /* Basic IFD offset checks */
  if ( tiff_first_ifd_offset(p_reader) < 8 ) {
    return detail_error_handler(UNDEFINED, "Invalid offset to first IFD.") ;
  }
  if ( ((tiff_first_ifd_offset(p_reader))&1) != 0 ) {
    if ( tiff6params->f_strict ) {
      return detail_error_handler(UNDEFINED, "First IFD offset is not word aligned.") ;

    } else if ( tiff6params->f_verbose ) {
      monitorf((uint8*)UVS("%%%%[ Warning: First IFD offset is not word aligned. ]%%%%\n"));
    }
  }
  HQTRACE(tiff_debug,
          ("tiff_read_header: first IFD offset %d", tiff_first_ifd_offset(p_reader)));
  return TRUE ;

} /* Function tiff_read_header */


/*
 * tiff_set_image()
 */
Bool tiff_set_image(
  tiff_reader_t*  p_reader,       /* I */
  uint32          ifd_index)      /* I */
{
  uint32          i;
  ifd_ifdlink_t* p_ifdlink;
  dll_list_t*     pdls;

  HQASSERT((p_reader != NULL),
           "tiff_set_image: NULL reader pointer");
  HQASSERT((ifd_index > 0),
           "tiff_set_image: image index is zero");

  /* Check sensible index */
  if ( ifd_index > tiff_count_ifds(p_reader) ) {
    return detail_error_handler(UNDEFINED, "TIFF image index is to big for this file.") ;
  }

  if ( tiff_current_ifd_index(p_reader) != ifd_index ) {
    /* Walk list upto indexed ifd */
    pdls =  tiff_dls_ifds(p_reader);
    p_ifdlink = DLL_GET_HEAD(pdls, ifd_ifdlink_t, dll);
    for ( i = 1; i < ifd_index; i++ ) {
      p_ifdlink = DLL_GET_NEXT(p_ifdlink, ifd_ifdlink_t, dll);
      HQASSERT((p_ifdlink != NULL),
               "tiff_set_image: IFD list prematurely empty");
    }
    /* Update cached ifd index and pointer */
    tiff_set_ifd_iterator(p_reader, p_ifdlink->p_ifd, ifd_index);
  }

  return TRUE ;

} /* Function tiff_set_image */


/** tiff_check_exif() checks the first IFD read in for standard exif
 * entries and values.  If the IFD passes the checks the function returns
 * TRUE, else it returns FALSE and sets UNDEFINED, TYPECHECK, or RANGECHECK.
 * The function first checks individual entries being valid before doing more
 * extensive cross checking for consistency of entries.
 */
static
Bool tiff_check(
  corecontext_t           *corecontext,
  tiff_reader_t*  p_reader,                /* I */
  ifd_ifdentry_check_t*  p_ifd_defaults,   /* I */
  uint32                  num_defaults)    /* I */
{
  TIFF6PARAMS *tiff6params = corecontext->tiff6params;
  uint32            samples_per_pixel;
  uint32            pmi;
  uint32            compression = COMPRESS_None;
  uint32            extra_samples;
  uint32            sample_count;
  uint32            bits_per_sample;
  uint32            i;
  uint32            count;
  uint32            rows_per_strip;
  uint32            fill_order;
  Bool              f_extra_samples;
  ifd_ifdentry_t*  p_ifdentry;
  Bool              f_CMYKInkSet;
  uint32            total_samples;
  uint32            planar_config;

  HQASSERT((p_reader != NULL),
           "tiff_check_tiff6: NULL reader pointer");
  HQASSERT((tiff_current_ifd_index(p_reader) != 0),
           "tiff_check_tiff6: ifd index has not been set");

  /* First check for expected IFD entries */
  if ( !ifd_verify_ifd( p_reader->ifd_reader,
                        tiff_current_ifd(p_reader),
                        p_ifd_defaults,
                        num_defaults) ) {
    return FALSE ;
  }

  /* PhotometricInterpretation is a required entry so must be present AND valid */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_PhotometricInterpretation);
  pmi = (p_ifdentry != NULL) ? ifd_entry_unsigned_int(p_ifdentry) : ENTRY_DEF_PMI;

  /* Check pixel sample information hangs together. Basically, that the declared
   * number of samples matches what we expect from the photometricinterpretation
   * and whether there is extra sample data. */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_SamplesPerPixel);
  samples_per_pixel = (p_ifdentry != NULL)
                          ? ifd_entry_unsigned_int(p_ifdentry)
                          : ENTRY_DEF_SAMPLES_PER_PIXEL;

  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_ExtraSamples);
  extra_samples = 0;
  f_extra_samples = (p_ifdentry != NULL);
  if ( f_extra_samples ) {
    extra_samples = ifd_entry_count(p_ifdentry);
  }

  f_CMYKInkSet = TRUE; /* default to CMYK InkSet */
  total_samples = pmi_planes[(uint32)pmi];

  /* if separations and InkSet != INKSET_CMYK then */
  if ( pmi == PMI_Separated ) {
    uint32 num_inks = 4;

    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_InkSet);
    if ( p_ifdentry != NULL ) {
      uint32  InkSet;
      InkSet = ifd_entry_unsigned_int(p_ifdentry);
      f_CMYKInkSet = (InkSet == INKSET_CMYK);
    }
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_NumberOfInks);
    if ( p_ifdentry != NULL ) {
      num_inks = ifd_entry_unsigned_int(p_ifdentry);
      if ( num_inks != (samples_per_pixel - extra_samples) ) {
        return detail_error_handler(UNDEFINED, "Number of inks in Separated TIFF image not correct.") ;
      }
    }

    if (!f_CMYKInkSet) {
      total_samples = num_inks;
      p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_InkNames);
      if ( p_ifdentry != NULL ) {
        /* The Ink Set is not CMYK so read in the names of the separations
           These are held in a continuous char array of NULL separated strings.
           There should be one string for each separation.
        */
        uint32 count,c;
        uint8 * inknames;
        uint32 inkcount;

        count = ifd_entry_count(p_ifdentry);

        inknames = ifd_entry_string(p_ifdentry);
        for (inkcount = 0, c = 0; c < count;c++) {
          if (inknames[c] == 0) {
            inkcount++;
            if (inkcount > num_inks)
              return detail_error_handler(UNDEFINED, "Too many ink names in Separated TIFF image.") ;
          }
        }
        if (inkcount != (samples_per_pixel - extra_samples))
          return detail_error_handler(UNDEFINED, "Number of ink names in Separated TIFF image not correct.") ;
      }
    }
  }

  /* check total samples (assumes CMYK for separations) */
  total_samples += extra_samples;
  if (total_samples != samples_per_pixel)
    return detail_error_handler(UNDEFINED, "Inconsistent samples per pixel information.") ;


  /*
   * Check number of samples per pixel against the count of bits per sample - we
   * only need to do this if BitsPerSample is present since we know the default
   * will be correct.
   */
  bits_per_sample = ENTRY_DEF_BITS_PER_SAMPLE;
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_BitsPerSample);
  if ( p_ifdentry != NULL ) {
    sample_count = ifd_entry_count(p_ifdentry);
    if ( sample_count != (uint32)samples_per_pixel ) {
      if ( tiff6params->f_strict || (sample_count != 1))
        return detail_error_handler(UNDEFINED, "SamplesPerPixel and BitsPerSample count are not the same.") ;
      else
        if ( tiff6params->f_verbose )
          monitorf(UVS("%%%%[ Warning: SamplesPerPixel and BitsPerSample count are not the same. ]%%%%\n"));
    }

    /* Check bits per sample is same for all samples */
    bits_per_sample = ifd_entry_array_index(p_ifdentry, 0);
    for ( i = 1; i < sample_count; i++ ) {
      if ( bits_per_sample != ifd_entry_array_index(p_ifdentry, i) ) {
        return detail_error_handler(UNDEFINED, "BitsPerSample array values are not all the same.") ;
      }
    }
  }

  /* Get byte fill order if present */
  fill_order = ENTRY_DEF_FILLORDER;
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_FillOrder);
  if ( p_ifdentry != NULL ) {
    fill_order = ifd_entry_unsigned_int(p_ifdentry);
  }

  /* Check for potential problems with flipped byte order and non-bilelvel images */
  if ( (fill_order == FILLORDER_LSB_TO_MSB)
        && (bits_per_sample > 1)
        && tiff6params->f_verbose ) {
    monitorf(UVS("%%%%[ Warning: Non-bilevel image data is byte flipped - results not predictable. ]%%%%\n"));
  }

  /* We can only handle same bits per sample as type 1 image can */
  switch ( bits_per_sample ) {
  case 1:
  case 2:
  case 4:
  case 8:
  case 12:
  case 16:
    break;
  default:
    return detail_error_handler(UNDEFINED, "Unable to handle image bits per sample.") ;
  }

  /* Check for redundant PlanarConfiguration entry */
  planar_config = ENTRY_DEF_PLANARCONFIG;
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_PlanarConfiguration);
  if (p_ifdentry != NULL) {
    planar_config = ifd_entry_unsigned_int(p_ifdentry);
    if ( (samples_per_pixel == 1) && tiff6params->f_verbose ) {
      /* Note: there will be no warning for bilevel/gray images with ExtraSamples > 0! */
      monitorf(UVS("%%%%[ Warning: Planar configuration specified for bilevel/grayscale image - ignored. ]%%%%\n"));
    }
  }

  /* Various checks based on compression if present */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_Compression);
  if ( p_ifdentry != NULL ) {

    compression = ifd_entry_unsigned_int(p_ifdentry);
    switch (compression) {
    case COMPRESS_CCITT:
    case COMPRESS_CCITT_T4:
    case COMPRESS_CCITT_T6:
      /* Check we have a bilevel image */
      if ( (bits_per_sample > 1) ||
           ( (planar_config != PLANAR_CONFIG_PLANAR) && (samples_per_pixel > 1)) )
      {
        return detail_error_handler(UNDEFINED, "CCITT compression defined but image is not bilevel.") ;
      }
      break;

    case COMPRESS_JPEG_OLD:
      /* Check QTables are present and of the right size */
      p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_JPEGQTables);
      if ( p_ifdentry == NULL ) {
        return detail_error_handler(UNDEFINED, "JPEG compression defined but IFD does not include QTables.") ;
      }
      if ( ifd_entry_count(p_ifdentry) != samples_per_pixel ) {
        return detail_error_handler(RANGECHECK, "TIFF JPEG QTables array size is not the same as the samples per pixel.") ;
      }
      break;

    case COMPRESS_FLATE:
      if (tiff6params->f_strict)
        return detail_error_handler(UNDEFINED, "Non-standard (FLATE) compression mode for TIFF 6 image.") ;

    default:
      break;
    }

    /* Check FillOrder where applicable */
    switch ( compression ) {
    default:
      if ( (fill_order == FILLORDER_LSB_TO_MSB) && tiff6params->f_verbose ) {
        monitorf(UVS("%%%%[ Warning: Invalid compression for byte flipped image data - results not predictable. ]%%%%\n"));
      }
      break;

    case COMPRESS_CCITT:
    case COMPRESS_CCITT_T4:
    case COMPRESS_CCITT_T6:
    case COMPRESS_None:
      break;
    }
  }


  if ( (pmi == PMI_CIE_L_a_b_) ||
       (pmi == PMI_ICCLab) ) {
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_WhitePoint);
    if ( p_ifdentry != NULL ) {
      tiff_rational_t x,y;

      if (ifd_entry_count(p_ifdentry) != 2)
        return detail_error_handler(RANGECHECK, "WhitePoint IFD Entry has wrong number of values.") ;

      ifd_entry_rational_array_index(p_ifdentry,0,x);
      ifd_entry_rational_array_index(p_ifdentry,1,y);

      if ((x[RATIONAL_NUMERATOR] == 0)||
          (y[RATIONAL_NUMERATOR] == 0)||
          (x[RATIONAL_DENOMINATOR] == 0)||
          (y[RATIONAL_DENOMINATOR] == 0))
        return detail_error_handler(RANGECHECK, "Invalid WhitePoint chromaticity value.") ;
    }
  }

  if ((pmi == PMI_YCbCr)
       && ((compression != COMPRESS_JPEG_OLD)&&(compression != COMPRESS_JPEG)))
    return detail_error_handler(UNDEFINED, "YCbCr inks only supported for JPEG compressed TIFF.") ;

  if ( pmi == PMI_Separated ) {
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_DotRange);
    if ( p_ifdentry != NULL ) {
      /* Check DotRange consistency for CMYK images */
      count = ifd_entry_count(p_ifdentry);
      if ( count < 2 ) {
        return detail_error_handler(UNDEFINED, "Insufficient values in DotRange.") ;
      }
      if ( (count > 2) && tiff6params->f_verbose ) {
        monitorf(UVS("%%%%[ Warning: More than two entries in DotRange entry - using first two only. ]%%%%\n"));
      }
    }

  } else {
    if (pmi == PMI_Palette) {
      if (  ( bits_per_sample != 1)  && ( bits_per_sample != 2)
            && ( bits_per_sample != 4) && ( bits_per_sample != 8) )
        return detail_error_handler(UNDEFINED, "Bits per sample wrong for palette-color TIFF image.") ;

      if ((compression != COMPRESS_None) && (compression != COMPRESS_Packbits)) {
        if (tiff6params->f_strict)
          return detail_error_handler(UNDEFINED, "Non-standard compression mode for palette-color TIFF image.") ;
        else
          monitorf(UVS("%%%%[ Warning: Non-standard compression mode for palette-color TIFF image. ]%%%%\n"));
      }


      p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_ColorMap);
      if ( p_ifdentry == NULL )
        return detail_error_handler(UNDEFINED, "Palette missing for TIFF image.") ;

      /* Check ColorMap size */
      /* should be 3*(2**bits_per_sample) - RGB colorspace */
      count = ifd_entry_count(p_ifdentry);
      if ( count != (uint32)(3<<bits_per_sample) )
        return detail_error_handler(UNDEFINED, "Palette size wrong for TIFF image.") ;
    }
  }

  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_RowsPerStrip);
  if ( p_ifdentry != NULL ) {
    /* Check for sensible RowsPerStrip */
    rows_per_strip = ifd_entry_unsigned_int(p_ifdentry);
    if ( rows_per_strip == 0 ) {
      return detail_error_handler(UNDEFINED, "TIFF image RowsPerStrip entry is zero.") ;
    }
  }

  return TRUE ;
}

/** tiff_check_exif() checks the first IFD read in for standard exif
 * entries and values.  If the IFD passes the checks the function returns
 * TRUE, else it returns FALSE and sets UNDEFINED, TYPECHECK, or RANGECHECK.
 * The function first checks individual entries being valid before doing more
 * extensive cross checking for consistency of entries.
 */
Bool tiff_check_exif(
  corecontext_t   *corecontext,
  tiff_reader_t*  p_reader)       /* I */
{
  /* First check for expected IFD entries */
  return tiff_check(corecontext, p_reader, exif_defaults, NUM_ARRAY_ITEMS(exif_defaults));
}

/** tiff_check_tiff6() checks the first IFD read in for standard TIFF6
 * entries and values.  If the IFD passes the checks the function returns
 * TRUE, else it returns FALSE and sets UNDEFINED, TYPECHECK, or RANGECHECK.
 * The function first checks individual entries being valid before doing more
 * extensive cross checking for consistency of entries.
 */
Bool tiff_check_tiff6(
  corecontext_t   *corecontext,
  tiff_reader_t*  p_reader)       /* I */
{
  /* First check for expected IFD entries */
  if ( tiff_check(corecontext, p_reader, tiff6_defaults, NUM_ARRAY_ITEMS(tiff6_defaults)))
  {
    ifd_ifdentry_t*  p_ifdentry;

    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_StripByteCounts);
    if ( p_ifdentry == NULL ) {
      /* some dumb tiff files have missing StripByteCounts, we manufacture one for the
         case of single strip uncompressed data */
      if ( corecontext->tiff6params->f_strict ) {
        return detail_error_handler(UNDEFINED, "Missing StripByteCounts.") ;
      }
      else
      {
        p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_Compression);
        if ( p_ifdentry != NULL ) {
          if ( ifd_entry_unsigned_int(p_ifdentry) != COMPRESS_None ) {
            return detail_error_handler(UNDEFINED, "Missing StripByteCounts in a compressed file.") ;
          }
        }
        p_ifdentry = ifd_find_entry( tiff_current_ifd(p_reader), TAG_StripOffsets );
        if ( p_ifdentry != NULL ) {
          if ( ifd_entry_count(p_ifdentry) != 1 ) {
            return detail_error_handler(UNDEFINED, "Missing StripByteCounts in a multi-strip file.") ;
          }
        }
      }
    }
    return TRUE;
  }
  return FALSE;
} /* Function tiff_check_tiff6 */

/* ========================================================================== */

Bool photoshop_private_data(tiff_image_data_t * p_data,
                            ifd_ifdentry_t    * p_ifdentry)
{
  uint32 block_length = ifd_entry_count(p_ifdentry);
  uint32 index = 0;
  Bool swatch = FALSE ;
  Bool alternatespots = FALSE ;
  uint8 * identifiers = NULL ;

  p_data->PSDchannels = 0 ;
  p_data->PSDspots = 0 ;
  p_data->PSDmasks = 0 ;
  p_data->inknames = NULL ;
  p_data->inkdetails = NULL ;

  while (index + 12 <= block_length &&
         ifd_entry_array_check_substring(p_ifdentry, index, 4, (uint8*)"8BIM")) {
    uint8 * name, * tptr ;
    int32 namelen, datasize ;
    uint32 ID ;

    index += 4;

    ID = ifd_entry_array_index(p_ifdentry,index++) * 256;
    ID += ifd_entry_array_index(p_ifdentry,index++);

    namelen = ifd_entry_array_index(p_ifdentry,index++);
    name = ifd_entry_array_fetch_string(p_ifdentry, index,namelen,NULL);
    index += namelen;
    if ((namelen & 1) == 0)
      index++;
    datasize = ifd_entry_array_index(p_ifdentry,index + 0)<<24 |
               ifd_entry_array_index(p_ifdentry,index + 1)<<16 |
               ifd_entry_array_index(p_ifdentry,index + 2)<<8  |
               ifd_entry_array_index(p_ifdentry,index + 3) ;
    index += 4;

    switch ( ID ) {
    case PSD_AlphaChannelsNames:
      /* Note: Channel names in this table are truncated to 31chrs */
      {
        int channels = 0 ;
        uint8 * end ;

        /* channel names as Pascal strings */
        p_data->inknames =
          ifd_entry_array_fetch_string(p_ifdentry,index,datasize,NULL) ;

        /* count the number of colors named */
        tptr = p_data->inknames ;
        end  = tptr + datasize ;
        while (tptr < end) {
          uint8 len = *tptr++ ;
          if (len == 0)
            break ;

          tptr += len ;
          if (tptr > end) {
            HQFAIL("Truncated AlphaChannelsNames table") ;
            break ;
          }

          ++channels ;
        }
        HQASSERT(tptr == end, "Unterminated AlphaChannelsNames table") ;

        if (channels == 0)
          p_data->inknames = NULL ;

        /* This is the first PSD table we see, so store channel count */
        p_data->PSDchannels = channels ;
      }
      break;

    /* UnicodeAlphaNames follows and is not truncated to 31chrs, but
       would require conversion from UCS16, so we'll not bother with it. */

    case PSD_DisplayInfo:
      /* Colorspace definitions, opacity and type for each channel.
         Note that we cannot use the opacity, as that controls how much the
         channel replaces or mixes with the underlying channels. We are in
         any case only interested in those channels marked as spot colors. */
      if (p_data->PSDchannels > 0) {
        uint32 channels = 0, spots = 0, masks = 0 ;
        uint8 * end, * ptr =
          ifd_entry_array_fetch_string(p_ifdentry, index, datasize, NULL) ;

        p_data->inkdetails = (PSDisplayInfo *) ptr ;
        end = ptr + datasize ;

        while (ptr < end) {
          PSDisplayInfo * chan = (PSDisplayInfo *) ptr ;
          if (ptr + 2*6 + 2 > end) {
            HQFAIL("Truncated DisplayInfo table") ;
            break ;
          }
#ifndef highbytefirst
          BYTE_SWAP16_BUFFER(ptr, ptr, 2 * 6) ; /* six shorts */
#endif
          if (chan->type == PSD_spot) {
            ++spots ;

            switch (chan->colorSpace) {
            case PhotoShop_RGB:  /* Common */
            case PhotoShop_HSB:  /* Less common */
            case PhotoShop_CMYK: /* PS CS cannot create this */
            case PhotoShop_Lab:  /* Common */
            case PhotoShop_Gray: /* PS CS cannot create this */
              break ;

            default:
              HQFAIL("Unknown colorspace found in DisplayInfo table") ;
              /* An unknown colorspace may be a new swatch collection, so */
              /* drop through... */

            case PhotoShop_Pantone:
            case PhotoShop_Focoltone:
            case PhotoShop_Trumatch:
            case PhotoShop_Toyo:
            case PhotoShop_HKS:
            case PhotoShop_DIC:
            case PhotoShop_TotalInk:
            case PhotoShop_ANPA:
              /* These (as of Aug2012) are the known swatch colorspaces, and
                 are defined by six alphanumeric characters - so we need the
                 AlternateSpotColors table to provide the color. */
              swatch = TRUE ;
              break ;
            }
          }
          else if (chan->type == PSD_mask)
          {
            ++masks ;
          }

          ++channels ;
          ptr += 2*6 + 2 ; /* six shorts and two chars */
        }

        if (channels != p_data->PSDchannels) {
          HQFAIL("DisplayInfo inconsistent channel count") ;
          if (channels < p_data->PSDchannels)
            p_data->PSDchannels = channels ;
        }
        p_data->PSDspots = spots ;
        p_data->PSDmasks = masks ;
      }
      break ;

    case PSD_AlphaIdentifiers:
      /* This contains 32bit IDs for each of the above channels. These IDs
         are used to look up spot color channel L*a*b definitions in the
         AlternateSpotColors table following. We only bother with this
         if we have swatch (or unknown) colorspaces in the DisplayInfo
         table above. */
      if (swatch) {
        int32 size = p_data->PSDchannels * 4 ;

        if (datasize != size) {
          if (datasize < size) {
            HQFAIL("Truncated AlphaIdentifiers table") ;
            p_data->PSDchannels = datasize / 4 ;
          } else {
            HQFAIL("AlphaIdentifiers table larger than expected") ;
          }
        }

        /* Remember this for use during the AlternateSpotColors table. There is
           no need to change the endianness of the IDs in either table. */
        identifiers =
          ifd_entry_array_fetch_string(p_ifdentry, index, datasize, NULL);
      }
      break;

    case PSD_AlternateSpotColors:
      /* Alternate color definitions for the channels already defined above.
         These definitions appear to always be in the L*a*b colorspace, which
         we require for the tokenised swatch cases above.

         Note that this only contains definitions for spot channels, not
         mask or selection channels. */
      if (swatch) {
        PSAlternateSpots * spot, * end ;
        uint16 * header ;
        int32 size ;
        uint32 i ;
        uint8 * data =
          ifd_entry_array_fetch_string(p_ifdentry, index, datasize, NULL);
#ifndef highbytefirst
        BYTE_SWAP16_BUFFER(data, data, 2 * 2) ; /* header is two shorts */
#endif
        HQASSERT(identifiers, "Missing AlphaIdentifiers table") ;

        header = (uint16 *) data ;
        /* 1st short is version, 2nd short is the number of spots. */
        HQASSERT(header[0] == 1, "Unexpected AlternateSpotColors version") ;
        size = (int32)(header[1]*sizeof(PSAlternateSpots) + 4) ;
        if (datasize != size) {
          if (datasize < size) {
            HQFAIL("Truncated AlternateSpotColors table") ;
          } else {
            HQFAIL("AlternateSpotColors table larger than expected") ;
          }
          /* Truncate to a whole number of spots */
          header[1] = (uint16)((datasize-4)/sizeof(PSAlternateSpots)) ;
        }

        spot = (PSAlternateSpots *) (data + 4) ; /* skip header */
        end = (PSAlternateSpots *) (data + 4 +
                                    header[1] * sizeof(PSAlternateSpots)) ;

        /* For each channel in the DisplayInfo table, find those that are spots
           with a difficult colorspace. */
        for (i = 0 ; i < p_data->PSDchannels ; ++i) {
          PSDisplayInfo * channel = p_data->inkdetails + i ;

          /* Ignore non-spots */
          if (channel->type != PSD_spot)
            continue ;

          /* Ignore simple colorspaces */
          switch (channel->colorSpace) {
            case PhotoShop_RGB:
            case PhotoShop_HSB:
            case PhotoShop_CMYK:
            case PhotoShop_Lab:
            case PhotoShop_Gray:
              continue ;
          }

          /* A spot color in a difficult colorspace.
             If we don't have an AlphaIdentifiers table, use the definitions
             in order. Otherwise, find the channel's identifier from the
             AlphaIdentifiers table, and then look that up in the
             AlternateSpotColors table. */
          if (identifiers) {
            uint16 * id = (uint16*) (identifiers + i * 4) ;
            for (spot = (PSAlternateSpots *) (data + 4) ; /* skip header */
                 spot < end ; ++spot) {
              if (spot->id1 == id[0] && spot->id2 == id[1])
                break ;
            }
          }

          /* spot is the definition to use, or ==end if not found */
          if (spot < end) {
            int j ;
#ifndef highbytefirst
            uint8 * shorts = ((uint8*)spot) + 4 ;       /* skip ID */
            BYTE_SWAP16_BUFFER(shorts, shorts, 2 * 5) ; /* five shorts */
#endif
            /* Spot colorspace should be L*a*b */
            switch (spot->colorSpace) {
              case PhotoShop_Lab:  /* I've only seen L*a*b in this table */
              case PhotoShop_RGB:
              case PhotoShop_HSB:
              case PhotoShop_CMYK:
              case PhotoShop_Gray:
                break ;
              default:
                HQFAIL("Unsupported colorspace in AlternateSpotColors table") ;
                break ;
            }

            /* Replace the difficult colorspace */
            channel->colorSpace = spot->colorSpace ;
            for (j = 0 ; j < 4 ; ++j)
              channel->color[j] = spot->color[j] ;

            ++spot ; /* if AlphaIdentifiers is missing, go to next spot */
          } else {
            /* Failed to find the definition */
            HQFAIL("Failed to find channel in AlternateSpotColors table") ;
          }

        } /* for i < channels */

        alternatespots = TRUE ;
      }
      break ;
    }
    if ((datasize & 1) != 0)
      ++datasize ;
    index += datasize ;
  } /* while < block_length */

  /* We could do something about the lack of spot color definitions here:
  HQASSERT(!swatch || alternatespots, "AlternateSpotColors table is"
           " required when DisplayInfo contains swatch colors") ;
  */

  /* This strikes me as terribly dodgy - why not just use the
     PhotometricInterpretation tag? Why in effect add in the non-spot
     channels too? Wrong surely? Anyway, this is what it used to do,
     so this is what it shall continue to do. simonb [66184] */
  /* No time to fix this properly here. So revert to treating masks as spots. But keep them
     separately, to remind ourselves that it needs further work. tina [368744] */
  if (p_data->PSDspots + p_data->PSDmasks >= 1) {
    HQASSERT((p_data->extra_samples >= p_data->PSDspots + p_data->PSDmasks),
             "tiff extra samples about to go negative");
    p_data->extra_samples -= (p_data->PSDspots + p_data->PSDmasks);
    if (p_data->f_plain) {
      switch (p_data->samples_per_pixel - (p_data->PSDspots + p_data->PSDmasks)) {
      case 3:
        /* looks like we have RGB plus spots so will convert to CMY */
        p_data->photoshopinks = e_photoshopinks_cmy;
        break;
      case 4:
        p_data->photoshopinks = e_photoshopinks_cmyk;
        break;
      case 1:
        p_data->photoshopinks = e_photoshopinks_gray;
        break;
      default:
        monitorf(UVS("TIFF file format not supported.\n"));
        return error_handler(RANGECHECK);
      }
    }

    p_data->f_plain = FALSE;
  }

  return TRUE ;
}

/* ========================================================================== */

/** tiff_get_image_data() extracts tiff image data from the IFD with the given
 * index, from its entries or default values if they are not present.  IFD
 * indexes are 1 based.
 */
Bool tiff_get_image_data(
  corecontext_t       *corecontext,
  tiff_reader_t*      p_reader,   /* I */
  tiff_image_data_t*  p_data)     /* O */
{
  TIFF6PARAMS *tiff6params = corecontext->tiff6params;
  Bool              f_list_ifd_entries;
  uint32            icc_length;
  uint32            i;
  uint32            offset;
  ifd_ifdentry_t*  p_ifdentry;
  static uint8*     is_default = UVS(" (default)");
  static uint8*     is_ignored = UVS(" (ignored)");
  static uint8*     blank = (uint8 *) "";
  tiff_rational_t   x_resolution;
  tiff_rational_t   y_resolution;
  double            xres, yres;
  Bool              resundefined;
  Bool              defaulted_resolution;


  HQASSERT((p_reader != NULL),
           "tiff_get_image_data: NULL reader pointer");
  HQASSERT((tiff_current_ifd_index(p_reader) != 0),
           "tiff_get_image_data: ifd index has not been set");
  HQASSERT((p_data != NULL),
           "tiff_get_image_data: NULL pointer to returned image data");

  /* Quick & dirty initialisation. */
  HqMemZero((uint8 *)p_data, sizeof(tiff_image_data_t)) ;

  p_data->f_plain = TRUE;
  p_data->jpegtables = onull; /* Struct copy to set slot properties */

  f_list_ifd_entries = tiff6params->f_list_ifd_entries;

  /* Image length and width are required entries so must be present */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_ImageLength);
  p_data->image_length = ifd_entry_unsigned_int(p_ifdentry);
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_ImageWidth);
  p_data->image_width = ifd_entry_unsigned_int(p_ifdentry);
  if ( f_list_ifd_entries ) {
    monitorf(UVM("ImageWidth: %u, ImageLength: %u\n"),
             p_data->image_width, p_data->image_length);
  }

  p_data->scale_factor = TIFF_POINTS_INCH;


  /* ResolutionUnit */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_ResolutionUnit);
  p_data->res_unit = (p_ifdentry != NULL)
                        ? ifd_entry_unsigned_int(p_ifdentry)
                        : ENTRY_DEF_RESOLUTION_UNITS;
  if ( f_list_ifd_entries ) {
    monitorf(UVM("ResolutionUnit: %s%s\n"),
             tiff_resunits_string(p_data->res_unit), (p_ifdentry == NULL ? is_default : blank));
  }
  if ((p_data->res_unit == RESUNIT_NONE) && tiff6params->f_no_units_same_as_inch )
    p_data->res_unit = RESUNIT_INCH;

  resundefined = TRUE;

  /* X and Y res are required entries so must be present */
  if ( (p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_XResolution)) != NULL) {
    ifd_entry_rational(p_ifdentry, x_resolution);
    if ( (p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_YResolution)) != NULL) {
      ifd_entry_rational(p_ifdentry, y_resolution);
      if ( f_list_ifd_entries ) {
        monitorf(UVM("XResolution: %u/%u, YResolution: %u/%u\n"),
             x_resolution[RATIONAL_NUMERATOR], x_resolution[RATIONAL_DENOMINATOR],
             y_resolution[RATIONAL_NUMERATOR], y_resolution[RATIONAL_DENOMINATOR]);
      }
        resundefined = FALSE;
    }
  }

  defaulted_resolution = FALSE;

  /* Check for sensible resolution */
  if ( (p_data->res_unit == RESUNIT_NONE) ||
        resundefined ||
       (x_resolution[RATIONAL_DENOMINATOR] == 0) ||
       (y_resolution[RATIONAL_DENOMINATOR] == 0) ||
       (x_resolution[RATIONAL_NUMERATOR] == 0) ||
       (y_resolution[RATIONAL_NUMERATOR] == 0) ) {

    if ( tiff6params->f_strict )
      return detail_error_handler(RANGECHECK, "No valid image resolution found.");

    /* no valid resolution found. Attempt default (always in DPI) */
      xres = tiff6params->defaultresolution[0];
      yres = tiff6params->defaultresolution[1];
      defaulted_resolution = TRUE;

    if ( tiff6params->f_verbose ) {
      if (resundefined) {
        monitorf(UVS("%%%%[ Warning: x and/or y resolution undefined ]%%%%\n"));
      }
      else if (p_data->res_unit == RESUNIT_NONE) {
        monitorf(UVM("%%%%[ Warning: TIFF resolution has no units - [%d/%d, %d/%d] ]%%%%\n"),
          x_resolution[RATIONAL_NUMERATOR], x_resolution[RATIONAL_DENOMINATOR],
          y_resolution[RATIONAL_NUMERATOR], y_resolution[RATIONAL_DENOMINATOR]);
      }
      else {
        monitorf(UVM("%%%%[ Warning: invalid TIFF resolution - [%d/%d, %d/%d] ]%%%%\n"),
          x_resolution[RATIONAL_NUMERATOR], x_resolution[RATIONAL_DENOMINATOR],
          y_resolution[RATIONAL_NUMERATOR], y_resolution[RATIONAL_DENOMINATOR]);
      }
    }

  }else{
    xres = ((double)x_resolution[RATIONAL_NUMERATOR])/x_resolution[RATIONAL_DENOMINATOR];
    yres = ((double)y_resolution[RATIONAL_NUMERATOR])/y_resolution[RATIONAL_DENOMINATOR];
    /* Adjust based on units */
    if ( p_data->res_unit == RESUNIT_CM ) {
      p_data->scale_factor /= TIFF_CM_TO_INCH;
    }
  }

  p_data->x_resolution = xres;
  p_data->y_resolution = yres;
  p_data->defaulted_resolution = defaulted_resolution;

  /* Orientation */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_Orientation);
  p_data->orientation = (p_ifdentry != NULL)
                            ? ifd_entry_unsigned_int(p_ifdentry)
                            : ENTRY_DEF_ORIENTATION;

  /* Special case. Sony cameras seem to be producing JPEGs with orientation wrongly set to 0
     in APP1 data (JPEG). So we allow such errors through as default orientations. Bad Sony! */
  if (p_data->orientation == 0) {
    if (!tiff6params->f_strict)
      p_data->orientation = ENTRY_DEF_ORIENTATION;
    else
      return detail_error_handler(RANGECHECK, "Bad orientation.");
  }

  if ( f_list_ifd_entries ) {
    monitorf(UVM("Orientation: %s%s\n"),
             tiff_orientation_string(p_data->orientation), (p_ifdentry == NULL) ? is_default : blank);
  }

  /* BitsPerSample */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_BitsPerSample);
  if ( p_ifdentry != NULL ) {

    /* Access as an array of values using first value */
    p_data->bits_per_sample = ifd_entry_array_index(p_ifdentry, 0);

  } else {
    p_data->bits_per_sample = ENTRY_DEF_BITS_PER_SAMPLE;
  }
  if ( f_list_ifd_entries ) {
    monitorf(UVM("BitsPerSample: %u%s\n"),
             p_data->bits_per_sample, (p_ifdentry == NULL ? is_default : blank));
  }

  /* SamplesPerPixel */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_SamplesPerPixel);
  p_data->samples_per_pixel = (p_ifdentry != NULL)
                                  ? ifd_entry_unsigned_int(p_ifdentry)
                                  : ENTRY_DEF_SAMPLES_PER_PIXEL;

  if ( f_list_ifd_entries ) {
    monitorf(UVM("SamplesPerPixel: %u%s\n"),
             p_data->samples_per_pixel, (p_ifdentry == NULL ? is_default : blank));
  }

  /*  ExtraSamples */
  p_data->alpha = 0;
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_ExtraSamples);
  p_data->extra_samples = (p_ifdentry != NULL)
                            ? ifd_entry_count(p_ifdentry)
                            : 0;
  if ( (p_ifdentry != NULL) ) {
    for ( i = 0; i < p_data->extra_samples; i++ ) {
      uint32 alpha = ifd_entry_array_index(p_ifdentry, i) ;
      if ( tiff6params->f_ES0_as_ES2 && (alpha == 0))
        alpha = 2;
      if ( alpha == EXTRASAMPLES_ASSOCIATED_ALPHA ||
           alpha == EXTRASAMPLES_UNASSOCIATED_ALPHA ) {
        p_data->alpha = TRUE;
        p_data->premult = (alpha == EXTRASAMPLES_ASSOCIATED_ALPHA) ;
#if defined(ASSERT_BUILD)
        while ( ++i < p_data->extra_samples ) {
          alpha = ifd_entry_array_index(p_ifdentry, i) ;
          HQASSERT(alpha != EXTRASAMPLES_ASSOCIATED_ALPHA &&
                   alpha != EXTRASAMPLES_UNASSOCIATED_ALPHA,
                   "More than one alpha channel in TIFF image") ;
        }
#endif
        break;
      }
    }

    if ( f_list_ifd_entries ) {
      monitorf(UVM("ExtraSamples: %u ["), p_data->extra_samples);
      for ( i = 0; i < p_data->extra_samples; i++ ) {
        if ( i > 0 ) {
          monitorf(UVS(","));
        }
        /* String is not marked UVM since that is handled in the called function. */
        monitorf((uint8*)" %s", tiff_extrasamples_string(ifd_entry_array_index(p_ifdentry, i)));
      }
      /* String is not marked UVS since we really want a ] */
      monitorf((uint8*)" ]\n");
    }
  }

  /* PhotometricInterpretation (required) */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_PhotometricInterpretation);
  p_data->pmi = ifd_entry_unsigned_int(p_ifdentry);
  if ( f_list_ifd_entries ) {
    monitorf(UVM("PhotometricInterpretation: %s\n"),
             tiff_pmi_string(p_data->pmi));
  }

  switch (p_data->pmi) {
    /* Load up palette colormap if required */
  case PMI_Palette:
    {
      uint32 numvals;
      uint8* colormap;

      p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_ColorMap);
      colormap = p_data->colormap =
        (uint8 *) mm_alloc( p_reader->mm_pool,
                            sizeof(uint8) * (3 << p_data->bits_per_sample),
                            MM_ALLOC_CLASS_TIFF_COLOR );
      if ( p_data->colormap == NULL )
        return error_handler(VMERROR);

      /* read in table (ordered as all red, all green, all blue)
         and store in rgb structure array as CLUT for image */
      numvals = (uint32)(1<<p_data->bits_per_sample);

      /* convert from range 0->65535 = 0->1.0 to 0->255 = 0->1.0 */
#define MAPTO255(i_) \
         (uint8)((ifd_entry_array_index(p_ifdentry,(i_))/65535.0) * 255.0 + 0.5)

      for (i = 0;i < numvals;i++) {
        *colormap++ = MAPTO255(i);
        *colormap++ = MAPTO255(i + numvals);
        *colormap++ = MAPTO255(i + (2*numvals));
      }
    }
    break;
  case PMI_Separated:
    {
      p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_InkSet);
      if ( p_ifdentry != NULL ) {
        uint32 InkSet = ifd_entry_unsigned_int(p_ifdentry);
        p_data->f_plain = (InkSet == INKSET_CMYK);
      }

      p_data->inknames = NULL;
      p_data->photoshopinks = e_photoshopinks_none;

      if (!p_data->f_plain) {
        p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_InkNames);
        if ( p_ifdentry != NULL ) {
          /* The Ink Set is not CMYK so read in the names of the separations
             These are held in a continuous char array of NULL separated strings.
             There should be one string for each separation.
          */
          p_data->inknameslen = ifd_entry_count(p_ifdentry);
          p_data->inknames = ifd_entry_string(p_ifdentry);
        }
      }
    }
    break;
  case PMI_YCbCr:
    {
      /* substitute RGB (for JPEG compression only) */
      p_data->pmi = PMI_RGB;
    }
    break;
  case PMI_ICCLab:
    break;
  case PMI_CIE_L_a_b_:
    /* we need to correct the indices for the a and b components by ensuring
       they run [0,255] rather than [-128,127]. Indicate whether 8 or 16 bit.*/
    p_reader->lab_shift = p_data->bits_per_sample ;

    break;
  default:
    break;
  }


  /* Load White Point for Lab colorspace if available */
  if ((p_data->pmi == PMI_ICCLab)||
      (p_data->pmi == PMI_CIE_L_a_b_)) {
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_WhitePoint);
    if ( p_ifdentry != NULL ) {
      ifd_entry_rational_array_index(p_ifdentry,0,p_data->whitepoint_x);
      ifd_entry_rational_array_index(p_ifdentry,1,p_data->whitepoint_y);
    } else {
      /* default is D50 */
      p_data->whitepoint_x[RATIONAL_NUMERATOR] = 345703;
      p_data->whitepoint_x[RATIONAL_DENOMINATOR] = 1000000;
      p_data->whitepoint_y[RATIONAL_NUMERATOR] = 358539;
      p_data->whitepoint_y[RATIONAL_DENOMINATOR] = 1000000;
    }

    if ( f_list_ifd_entries ) {
      monitorf(UVM("WhitePoint: X=%u/%u, Y=%u/%u%s\n"),
                 p_data->whitepoint_x[RATIONAL_NUMERATOR],
                 p_data->whitepoint_x[RATIONAL_DENOMINATOR],
                 p_data->whitepoint_y[RATIONAL_NUMERATOR],
                 p_data->whitepoint_y[RATIONAL_DENOMINATOR],
                 (p_ifdentry == NULL ? is_default : blank));
    }
  }

  /* PlanarConfiguration */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_PlanarConfiguration);
  p_data->planar_config = (p_ifdentry != NULL)
                      ? ifd_entry_unsigned_int(p_ifdentry)
                      : ENTRY_DEF_PLANARCONFIG;
  if ( f_list_ifd_entries ) {
    /* Note: default is chunky and always handled, but if it is planar then it
     * will be ignored if spp is 1 */
    monitorf(UVM("PlanarConfiguration: %s%s\n"),
             tiff_planarconfig_string(p_data->planar_config),
             ((p_ifdentry == NULL)
                ? is_default
                : ((ifd_entry_unsigned_int(p_ifdentry) == PLANAR_CONFIG_PLANAR) &&
                   (p_data->samples_per_pixel == 1))
                  ? is_ignored
                  : blank));
  }

  /* Compression */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_Compression);
  p_data->compression = (p_ifdentry != NULL)
                            ? ifd_entry_unsigned_int(p_ifdentry)
                            : ENTRY_DEF_COMPRESSION;
  if ( f_list_ifd_entries ) {
    monitorf(UVM("Compression: %s%s\n"),
             tiff_compression_string(p_data->compression),
             (p_ifdentry == NULL ? is_default : blank));
  }

  p_data->predictor = ENTRY_DEF_PREDICTOR;
  if (( p_data->compression == COMPRESS_LZW )||
      ( p_data->compression == COMPRESS_FLATE )||
      ( p_data->compression == COMPRESS_FLATE_TIFFLIB )) {
    /* Predictor */
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_Predictor);

    if ( p_ifdentry != NULL ) {
      p_data->predictor = ifd_entry_unsigned_int(p_ifdentry);
    }
    if ( f_list_ifd_entries ) {
      monitorf(UVM("Predictor: %u%s\n"),
               p_data->predictor, (p_ifdentry == NULL ? is_default : blank));
    }
  }

  p_data->ccitt_t4options = ENTRY_DEF_T4OPTIONS;
  if ( p_data->compression == COMPRESS_CCITT_T4 ) {
    /* CCITT Group 3 */
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_T4Options);

    if ( p_ifdentry != NULL ) {
      p_data->ccitt_t4options = ifd_entry_unsigned_int(p_ifdentry);
    }
    if ( f_list_ifd_entries ) {
      monitorf(UVM("T4Options: 0x%x%s\n"),
               p_data->ccitt_t4options, (p_ifdentry == NULL ? is_default : blank));
    }
  }

  p_data->ccitt_t6options = ENTRY_DEF_T4OPTIONS;
  if ( p_data->compression == COMPRESS_CCITT_T6 ) {
    /* CCITT Group 4 */
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_T6Options);

    if ( p_ifdentry != NULL ) {
      p_data->ccitt_t6options = ifd_entry_unsigned_int(p_ifdentry);
    }
    if ( f_list_ifd_entries ) {
      monitorf(UVM("T6Options: 0x%x%s\n"),
               p_data->ccitt_t6options, (p_ifdentry == NULL ? is_default : blank));
    }
  }

  /* The following must be done before any return from this function */
  p_data->jpeg_qtable_size = 0;
  p_data->pp_jpeg_qtable = NULL;
  if ( p_data->compression == COMPRESS_JPEG_OLD ) {
    uint32            qtable_size;
    uint8*            p_qtable_data;
    uint8**           pp_qtable;

    /* JPEG */
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_JPEGQTables);
    HQASSERT((p_ifdentry != NULL),
             "tiff_get_image_data: failed to find JPEG tables when expected");
    qtable_size = ifd_entry_count(p_ifdentry);
    HQASSERT((qtable_size == p_data->samples_per_pixel),
             "tiff_get_image_data: JPEG qtables wrong size");

    /* Array of ptrs to array */
    pp_qtable = mm_alloc(p_reader->mm_pool, qtable_size*sizeof(uint8*),
                         MM_ALLOC_CLASS_TIFF_QTABLE);
    if ( pp_qtable == NULL ) {
      return error_handler(VMERROR);
    }

    /* All the data arrays in one go */
    p_qtable_data = mm_alloc(p_reader->mm_pool,
                             qtable_size*TIFF_JPEGQTABLES_SIZE*sizeof(uint8),
                             MM_ALLOC_CLASS_TIFF_QTABLE);
    if ( p_qtable_data == NULL ) {
      mm_free(p_reader->mm_pool, pp_qtable, qtable_size*sizeof(uint8*));
      return error_handler(VMERROR);
    }

    /* Set up pointers to individual arrays and read data in */
    pp_qtable[0] = p_qtable_data;
    for ( i = 1; i < qtable_size; i++ ) {
      pp_qtable[i] = &(pp_qtable[i - 1][TIFF_JPEGQTABLES_SIZE]);
      offset = ifd_entry_array_index(p_ifdentry, i);
      if ( !tiff_file_seek(p_reader->p_file, offset) ) {
        return detail_error_handler(IOERROR, "Unable to set file position to start of JPEG table data.") ;
      }
      if ( !tiff_file_read(p_reader->p_file, pp_qtable[i], TIFF_JPEGQTABLES_SIZE) )
        return detail_error_handler(IOERROR, "Unable to read JPEG table data.") ;
    }
    if ( f_list_ifd_entries ) {
      monitorf(UVM("JPEGQTables: %u\n"), qtable_size);
    }

#if defined( ASSERT_BUILD )
    if ( tiff_debug ) {
      uint32  l, m;
      uint8*  pb;
      for ( l = 0; l < qtable_size; l++ ) {
        pb = pp_qtable[l];
        HQTRACE(TRUE,
                ("JPEGQTables: #%d", l));
        for ( m = 0; m < TIFF_JPEGQTABLES_SIZE; m += 8 ) {
          HQTRACE(TRUE,
                  ("    %n %n %n %n %n %n %n %n",
                   pb[0], pb[1], pb[2], pb[3], pb[4], pb[5], pb[6], pb[7]));
          pb += 8;
        }
      }
    }
#endif

    p_data->jpeg_qtable_size = qtable_size;
    p_data->pp_jpeg_qtable = pp_qtable;
  } else if ( p_data->compression == COMPRESS_JPEG ) {
    int32 table_size;
    int32 i;
    OBJECT tablestring = OBJECT_NOTVM_NOTHING;
    FILELIST *rsd ;
    SFRAME myframe ;
    STACK mystack = { EMPTY_STACK, NULL, FRAMESIZE, STACK_TYPE_OPERAND } ;

    mystack.fptr = &myframe ;

    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_JPEGTables);
    table_size = ifd_entry_count(p_ifdentry);

    if ( f_list_ifd_entries ) {
      monitorf(UVM("JPEGTables: %u\n"), table_size);
    }

    /** \todo ajcd 2007-12-06: This is dubious; the string is created in
        PSVM, not TIFF memory, so will have to wait for a GC or restore to be
        freed. We haven't forced the allocation mode to local, so it
        might persist longer than expected. */
    if (!ps_string(&tablestring, NULL, table_size))
      return FALSE;

    for (i = 0; i < table_size; i++)
      oString(tablestring)[i] = (uint8)ifd_entry_array_index(p_ifdentry, i);

    if ( ! push( & tablestring , & mystack ))
      return FALSE ;

    rsd = filter_standard_find(NAME_AND_LENGTH("ReusableStreamDecode")) ;
    HQASSERT(rsd, "Somehow lost ReusableStreamDecode") ;
    if ( !filter_create_object(rsd, &p_data->jpegtables, NULL, &mystack) )
      return FALSE;

    HQASSERT(oType(p_data->jpegtables) == OFILE, "No filter created");
  }

  p_data->dot_range[0] = 0;
  p_data->dot_range[1] = ((uint32)1<<(p_data->bits_per_sample)) - 1;
  if ( p_data->pmi == PMI_Separated ) {
    /* DotRange */
    p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_DotRange);
    if ( p_ifdentry != NULL ) {
      p_data->dot_range[0] = ifd_entry_array_index(p_ifdentry, 0);
      p_data->dot_range[1] = ifd_entry_array_index(p_ifdentry, 1);

      HQASSERT((p_data->dot_range[0] != p_data->dot_range[1]),
               "tiff_get_data: zero dot_range - whoops");

      /* Photoshop (Mac) 16 bit tiffs have 1 byte entries
         so 16-bit dot-range is wrong. Currently I suspect these
         should be jacked up by a byte. */
      if (p_data->bits_per_sample > 8) {
        if (p_data->dot_range[1] <= 255) {
          uint32 shift = 1 << (p_data->bits_per_sample - 8);
          p_data->dot_range[0] *= shift;
          p_data->dot_range[1] *= shift;
        }
      }
    }
    if ( f_list_ifd_entries ) {
      monitorf(UVM("DotRange: [%u, %u]%s\n"),
               p_data->dot_range[0], p_data->dot_range[1], (p_ifdentry == NULL ? is_default : blank));
    }
  }

  /* Allocate table for strip compressed sizes */
  p_ifdentry = ifd_find_entry( tiff_current_ifd(p_reader), TAG_StripByteCounts );
  if (p_ifdentry != NULL) {
    p_data->strip_bytecount = ifd_entry_count(p_ifdentry);
    p_data->strip_bytes = mm_alloc( p_reader->mm_pool,
                                    sizeof(uint32) *  p_data->strip_bytecount,
                                    MM_ALLOC_CLASS_TIFF_STRIP );
    if (p_data->strip_bytes == NULL)
      return error_handler(VMERROR);

    if (p_data->strip_bytecount > 1) {
      uint32 i;
      for (i = 0; i < p_data->strip_bytecount; i++)
        p_data->strip_bytes[i] = ifd_entry_array_index(p_ifdentry,i);
    } else {
      p_data->strip_bytes[0] = ifd_entry_unsigned_int(p_ifdentry);
    }
  }
  else  /* if we get here we have a single strip uncompressed file without a
          stripbytecount: so we manufacture one */
  {
    p_data->strip_bytecount = 1;
    p_data->strip_bytes = mm_alloc( p_reader->mm_pool,
                                    sizeof(uint32) *  p_data->strip_bytecount,
                                    MM_ALLOC_CLASS_TIFF_STRIP );
    if (p_data->strip_bytes == NULL)
      return error_handler(VMERROR);
    p_data->strip_bytes[0] = p_data->image_length * (((p_data->image_width *
                                                p_data->bits_per_sample *
                                                p_data->samples_per_pixel) +
                                                BITS_BYTE - 1)/BITS_BYTE);
  }

  /* Allocate table for strip offsets */
  p_ifdentry = ifd_find_entry( tiff_current_ifd(p_reader), TAG_StripOffsets );
  if ( p_ifdentry != NULL ) {
    p_data->strip_count = ifd_entry_count(p_ifdentry);

    p_data->strip_offset = mm_alloc( p_reader->mm_pool,
                                     sizeof(uint32) *  p_data->strip_count,
                                     MM_ALLOC_CLASS_TIFF_STRIP );
    if (p_data->strip_offset == NULL)
      return error_handler(VMERROR);

    if (p_data->strip_count > 1) {
      uint32 i;
      for (i = 0; i < p_data->strip_count; i++)
        p_data->strip_offset[i] = ifd_entry_array_index(p_ifdentry,i);
    } else {
      p_data->strip_offset[0] = ifd_entry_unsigned_int(p_ifdentry);
    }
  }

  /* ICC color profile */
  p_data->f_icc_profile = FALSE;
  p_data->icc_offset = 0;
  p_data->icc_length = 0;
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_ICCProfile);
  if ( p_ifdentry != NULL ) {
    /* ICCProfile */
    icc_length = ifd_entry_count(p_ifdentry);
    if ( icc_length > 1 ) {
      /* Only bother with profiles with useful data */
      p_data->f_icc_profile = TRUE;
      p_data->icc_length = icc_length;
      p_data->icc_offset = ifd_entry_offset(p_ifdentry);
      if ( f_list_ifd_entries ) {
        monitorf(UVM("ICCProfile: offset %u, length %u\n"),
                 p_data->icc_offset, p_data->icc_length);
      }
    }
  }

  /* AWOOGA - nasty hack to allow gray images with no extra samples but with a
   * photoshop image resource block to render.  Whole gray image with PS image
   * resource block handling needs to be reworked.
   */
  if ( (p_data->alpha && p_data->extra_samples > 1) ||
       (!p_data->alpha && p_data->extra_samples > 0)) {

    /* possibly get colornames from Photoshop 3 field */
    p_ifdentry =
      ifd_find_entry(tiff_current_ifd(p_reader), TAG_Photoshop3ImageResource);
    if ( p_ifdentry )
      if (!photoshop_private_data(p_data, p_ifdentry))
        return FALSE ;

  } /* if alpha/extrasamples */


  /* Image data fill order */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_FillOrder);
  p_data->fill_order = (p_ifdentry != NULL)
                          ? ifd_entry_unsigned_int(p_ifdentry)
                          : ENTRY_DEF_FILLORDER;
  if ( f_list_ifd_entries ) {
    monitorf(UVM("FillOrder: %s%s\n"),
             tiff_fillorder_string(p_data->fill_order), (p_ifdentry == NULL ? is_default : blank));
  }

  return TRUE ;

} /* Function tiff_get_image_data */

/*
 * tiff_free_image_data()
 */
void tiff_free_image_data(
  tiff_reader_t*      p_reader,     /* I */
  tiff_image_data_t*  p_data)       /* I */
{
  HQASSERT((p_data != NULL),
           "tiff_free_image_data: NULL image data pointer");

  if ( p_data->pp_jpeg_qtable != NULL ) {
    mm_free(p_reader->mm_pool, p_data->pp_jpeg_qtable[0],
            ((p_data->jpeg_qtable_size)*TIFF_JPEGQTABLES_SIZE*sizeof(uint8*)));
    mm_free(p_reader->mm_pool, p_data->pp_jpeg_qtable, p_data->jpeg_qtable_size*sizeof(uint8*));
    p_data->pp_jpeg_qtable = NULL;
    p_data->jpeg_qtable_size = 0;
  }

  if (p_data->colormap) {
    mm_free( p_reader->mm_pool ,
          ( mm_addr_t )( p_data->colormap ) ,
          (sizeof(uint8) *  (3 << p_data->bits_per_sample))) ;
    p_data->colormap = NULL;
  }

  if (p_data->strip_bytes ) {
    mm_free( p_reader->mm_pool,
             ( mm_addr_t )( p_data->strip_bytes ) ,
             sizeof(uint32) * p_data->strip_bytecount) ;
    p_data->strip_bytes = NULL;
  }

  if (p_data->strip_offset) {
    mm_free( p_reader->mm_pool,
             ( mm_addr_t )( p_data->strip_offset ) ,
             sizeof(uint32) * p_data->strip_count) ;
    p_data->strip_offset = NULL;
  }
} /* Function tiff_free_image_data */


/*
 * tiff_jpeg_filter()
 */
Bool tiff_jpeg_filter(
  mm_pool_t           mm_pool,     /* I */
  tiff_image_data_t*  p_data,      /* I */
  OBJECT*             odict,       /* O */
  Bool                f_oldmethod) /* I */
{
  uint32  i;
  uint32  j;
  uint8*  p_qtable_data;
  OBJECT  intobj = OBJECT_NOTVM_NOTHING;
  OBJECT  oarray = OBJECT_NOTVM_NOTHING;
  OBJECT  oarraydata = OBJECT_NOTVM_NOTHING;

  HQASSERT((odict != NULL),
           "tiff_jpeg_filter: NULL pointer to returned dict object");

  if ( !tiff_alloc_psdict(mm_pool, TIFF_JPEG_DICT_SIZE, odict) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->image_width) ;
  HQASSERT(oInteger(intobj) > 0, "Image width overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Columns, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->image_length) ;
  HQASSERT(oInteger(intobj) > 0, "Image height overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Rows, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->samples_per_pixel) ;
  HQASSERT(oInteger(intobj) > 0, "Image samples per pixel overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Colors, &intobj) ) {
    return FALSE ;
  }

  if (f_oldmethod) {
    if ( !ps_array(&oarray, (int32)p_data->jpeg_qtable_size) ) {
      return FALSE ;
    }
    for ( i = 0; i < p_data->jpeg_qtable_size; i++ ) {
      /* Create channel array */
      if ( !ps_array(&oarraydata, (int32)TIFF_JPEGQTABLES_SIZE) ) {
          return FALSE ;
      }
      /* Fill in with qtable data */
      p_qtable_data = p_data->pp_jpeg_qtable[i];
      for ( j = 0; j < TIFF_JPEGQTABLES_SIZE; j++ ) {
        object_store_integer(&oArray(oarraydata)[j], p_qtable_data[j]);
      }
      /* And add to QuantTables array */
      oArray(oarray)[i] = oarraydata;
    }
    if ( !tiff_insert_hash(odict, NAME_QuantTables, &oarray) ) {
      return FALSE ;
    }
  } else {
    if ( !tiff_insert_hash(odict, NAME_JPEGTables, &p_data->jpegtables) )
      return FALSE ;
  }

  return TRUE ;

} /* Function tiff_jpeg_filter */


/*
 * tiff_ccitt_filter()
 */
Bool tiff_ccitt_filter(
  mm_pool_t           mm_pool,        /* I */
  tiff_image_data_t*  p_data,         /* I */
  uint32              rows_per_strip, /* I */
  OBJECT*             odict)          /* O */
{
  OBJECT intobj = OBJECT_NOTVM_NOTHING;

  HQASSERT((p_data != NULL),
           "tiff_ccitt_filter: NULL pointer to image data");
  HQASSERT((rows_per_strip > 0),
           "tiff_ccitt_filter: rows per strip is zero");
  HQASSERT((odict != NULL),
           "tiff_ccitt_filter: NULL pointer to returned dict object");

  if ( !tiff_alloc_psdict(mm_pool, TIFF_CCITT_DICT_SIZE, odict) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->image_width);
  HQASSERT(oInteger(intobj) > 0, "Image width overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Columns, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, 0) ;
  if ( !tiff_insert_hash(odict, NAME_K, &intobj) ) {
    return FALSE ;
  }

  if ( !tiff_insert_hash(odict, NAME_EncodedByteAlign, &tnewobj) ) {
    return FALSE ;
  }

  if ( !tiff_insert_hash(odict, NAME_EndOfBlock, &fnewobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)rows_per_strip) ;
  HQASSERT(oInteger(intobj) > 0, "Rows per strip overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Rows, &intobj) ) {
    return FALSE ;
  }

  if ( !tiff_insert_hash(odict, NAME_BlackIs1, ((p_data->pmi == PMI_BlackIsZero) ? &fnewobj : &tnewobj)) ) {
    return FALSE ;
  }

  return TRUE ;

} /* Function tiff_ccitt_filter */


/*
 * tiff_ccitt4_filter()
 */
Bool tiff_ccitt4_filter(
  mm_pool_t           mm_pool,        /* I */
  tiff_image_data_t*  p_data,         /* I */
  uint32              rows_per_strip, /* I */
  OBJECT*             odict)          /* O */
{
  uint32 flags;
  OBJECT intobj = OBJECT_NOTVM_NOTHING;

  HQASSERT((p_data != NULL),
           "tiff_ccitt4_filter: NULL pointer to image data");
  HQASSERT((rows_per_strip > 0),
           "tiff_ccitt4_filter: rows per strip is zero");
  HQASSERT((odict != NULL),
           "tiff_ccitt4_filter: NULL pointer to returned dict object");

  if ( !tiff_alloc_psdict(mm_pool, TIFF_CCITT4_DICT_SIZE, odict) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->image_width);
  HQASSERT(oInteger(intobj) > 0, "Image width overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Columns, &intobj) ) {
    return FALSE ;
  }

  flags = p_data->ccitt_t4options;

  object_store_integer(&intobj, BIT_SET(flags, 0) ? 1 : 0) ;
  if ( !tiff_insert_hash(odict, NAME_K, &intobj) ) {
    return FALSE ;
  }

  if ( !tiff_insert_hash(odict, NAME_Uncompressed, (BIT_SET(flags, 1) ? &tnewobj : &fnewobj)) ) {
    return FALSE ;
  }

  if ( BIT_SET(flags, 2) ) {
    if ( !tiff_insert_hash(odict, NAME_EncodedByteAlign, &tnewobj) ) {
      return FALSE ;
    }
    if ( !tiff_insert_hash(odict, NAME_EndOfLine, &tnewobj) ) {
      return FALSE ;
    }
  }

  if ( !tiff_insert_hash(odict, NAME_EndOfBlock, &fnewobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)rows_per_strip);
  HQASSERT(oInteger(intobj) > 0, "Rows per strip overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Rows, &intobj) ) {
    return FALSE ;
  }

  if ( !tiff_insert_hash(odict, NAME_BlackIs1, ((p_data->pmi == PMI_BlackIsZero) ? &fnewobj : &tnewobj)) ) {
    return FALSE ;
  }

  return TRUE ;

} /* Function tiff_ccitt4_filter */


/*
 * tiff_ccitt6_filter()
 */
Bool tiff_ccitt6_filter(
  mm_pool_t           mm_pool,    /* I */
  tiff_image_data_t*  p_data,     /* I */
  OBJECT*             odict)      /* O */
{
  OBJECT intobj = OBJECT_NOTVM_NOTHING;

  HQASSERT((odict != NULL),
           "tiff_ccitt6_filter: NULL pointer to returned dict object");

  if ( !tiff_alloc_psdict(mm_pool, TIFF_CCITT6_DICT_SIZE, odict) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->image_width);
  HQASSERT(oInteger(intobj) > 0, "Image width overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Columns, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, -1);
  if ( !tiff_insert_hash(odict, NAME_K, &intobj) ) {
    return FALSE ;
  }

  if ( !tiff_insert_hash(odict, NAME_Uncompressed, (BIT_SET(p_data->ccitt_t6options, 1) ? &tnewobj : &fnewobj)) ) {
    return FALSE ;
  }

  if ( !tiff_insert_hash(odict, NAME_BlackIs1, ((p_data->pmi == PMI_BlackIsZero) ? &fnewobj : &tnewobj)) ) {
    return FALSE ;
  }

  return TRUE ;

} /* Function tiff_ccitt6_filter */


/*
 * tiff_lzw_filter()
 */
Bool tiff_lzw_filter(
  mm_pool_t           mm_pool,    /* I */
  tiff_image_data_t*  p_data,     /* I */
  uint32              eodcount,   /* I */
  Bool                bigendian,  /* I */
  OBJECT*             odict)      /* O */
{
  OBJECT intobj = OBJECT_NOTVM_NOTHING, boolobj = OBJECT_NOTVM_NOTHING;

  HQASSERT((odict != NULL),
           "tiff_lzw_filter: NULL pointer to returned dict object");

  if ( !tiff_alloc_psdict(mm_pool, TIFF_LZW_DICT_SIZE, odict) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->bits_per_sample);
  HQASSERT(oInteger(intobj) > 0, "Image bits per component overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_BitsPerComponent, &intobj) ) {
    return FALSE ;
  }

  /* With planar images the filter is decoding a single color only */
  object_store_integer(&intobj, p_data->planar_config == PLANAR_CONFIG_CHUNKY
                              ? (int32)p_data->samples_per_pixel
                              : 1);
  HQASSERT(oInteger(intobj) > 0, "Image samples per pixel overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Colors, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->image_width);
  HQASSERT(oInteger(intobj) > 0, "Image width overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Columns, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->predictor);
  HQASSERT(oInteger(intobj) > 0, "Image predictor overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Predictor, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, eodcount);
  if ( !tiff_insert_hash(odict, NAME_EODCount, &intobj) ) {
    return FALSE ;
  }

  object_store_bool(&boolobj, bigendian);
  if ( !tiff_insert_hash(odict, NAME_BigEndian, &boolobj) ) {
    return FALSE ;
  }

  return TRUE ;

} /* Function tiff_lzw_filter */

/*
 * tiff_subfiledecode_filter()
 */
Bool tiff_subfiledecode_filter(
  mm_pool_t           mm_pool,    /* I */
  OBJECT*             odict)      /* O */
{
  OBJECT intobj = OBJECT_NOTVM_INTEGER(0);
  OBJECT stringobj = OBJECT_NOTVM_NOTHING;

  HQASSERT((odict != NULL),
           "tiff_subfiledecode_filter: NULL pointer to returned dict object");

  if ( !tiff_alloc_psdict(mm_pool, TIFF_FLIP_DICT_SIZE, odict) ) {
    return FALSE;
  }

  if ( !tiff_insert_hash(odict, NAME_EODCount, &intobj) ) {
    return FALSE;
  }

  theTags(stringobj) = OSTRING|UNLIMITED|LITERAL;
  theLen(stringobj) = 0;
  oString(stringobj) = NULL;
  if ( !tiff_insert_hash(odict, NAME_EODString, &stringobj) ) {
    return FALSE;
  }

  return TRUE;

} /* Function tiff_subfiledecode_filter */

Bool tiff_flate_filter(
  mm_pool_t           mm_pool,    /* I */
  tiff_image_data_t*  p_data,     /* I */
  OBJECT*             odict)      /* O */
{
  OBJECT intobj = OBJECT_NOTVM_NOTHING;

  HQASSERT((odict != NULL),
           "tiff_flate_filter: NULL pointer to returned dict object");

  if ( !tiff_alloc_psdict(mm_pool, TIFF_FLATE_DICT_SIZE, odict) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->predictor);
  HQASSERT(oInteger(intobj) > 0, "Image predictor overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Predictor, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->bits_per_sample);
  HQASSERT(oInteger(intobj) > 0, "Image bits per component overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_BitsPerComponent, &intobj) ) {
    return FALSE ;
  }

  object_store_integer(&intobj, (int32)p_data->image_width);
  HQASSERT(oInteger(intobj) > 0, "Image width overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Columns, &intobj) ) {
    return FALSE ;
  }

  /* With planar images the filter is decoding a single color only */
  object_store_integer(&intobj, p_data->planar_config == PLANAR_CONFIG_CHUNKY
                              ? (int32)p_data->samples_per_pixel
                              : 1);
  HQASSERT(oInteger(intobj) > 0, "Image samples per pixel overflowed") ;
  if ( !tiff_insert_hash(odict, NAME_Colors, &intobj) ) {
    return FALSE ;
  }

  return TRUE ;
}

/*
 * tiff_setup_strip_read()
 */
static void tiff_setup_strip_read(
  uint32              bytes_per_row,  /* I */
  uint32              rows_per_strip, /* I */
  tiff_strip_read_t*  p_strip)        /* O */
{
  uint32  bytes_per_strip;
  uint32  bytes_per_read;
  uint32  reads_per_strip;
  uint32  bytes_last_read;

  HQASSERT((bytes_per_row > 0),
           "tiff_setup_strip_read: row length is zero bytes");
  HQASSERT((rows_per_strip > 0),
           "tiff_setup_strip_read: number of rows in a strip is zero");
  HQASSERT((p_strip != NULL),
           "tiff_setup_strip_read: NULL strip reead pointer");

  p_strip->rows = rows_per_strip;

  /* Assume we can read whole strip in one go */
  reads_per_strip = 1;
  bytes_per_strip = (bytes_per_row*rows_per_strip); /* Shouldn't wrap! */
  bytes_last_read = 0;

  if ( bytes_per_strip > TIFF_MAX_BYTES_READ ) {
    /* Find number of reads in max chunk, and the little bit left */
    bytes_per_read  = TIFF_MAX_BYTES_READ;
    reads_per_strip = bytes_per_strip/TIFF_MAX_BYTES_READ;
    bytes_last_read = bytes_per_strip%TIFF_MAX_BYTES_READ;
    if ( bytes_last_read > 0 ) {
      /* Need one more read to read last bit of strip */
      reads_per_strip++;

    } else { /* Last read of strip same size as others */
      bytes_last_read = bytes_per_read;
    }

  } else {
    bytes_last_read = bytes_per_read = bytes_per_strip;
  }

  /* Return strip reading info */
  p_strip->current_read    = 1;
  p_strip->reads_per_strip = reads_per_strip;
  p_strip->bytes_per_read  = bytes_per_read;
  p_strip->bytes_last_read = bytes_last_read;

  HQASSERT((((p_strip->reads_per_strip - 1)*(p_strip->bytes_per_read) +
                  p_strip->bytes_last_read) == (bytes_per_row*rows_per_strip)),
           "tiff_setup_strip_read: strip read data does not add up to strip total");

} /* Function tiff_setup_strip_read */


/** tiff_check_strip() - checks that the strip offset and length are valid
 * for the given file. Returns FALSE if they are not, else TRUE.
 */
static Bool tiff_check_strip(
  corecontext_t *corecontext,
  tiff_file_t*  p_file,         /* I */
  uint32        offset,         /* I */
  uint32        bytecount)      /* I */
{
  if ( !tiff_file_seek(p_file, offset) ) {
    if ( corecontext->tiff6params->f_verbose ) {
      monitorf(UVS("%%%%[ Error: Failed to seek to the start of an image strip - file truncated? ]%%%%\n"));
    }
    return detail_error_handler(IOERROR, "Unable to seek to start of TIFF image strip.") ;
  }

  if ( bytecount > 1 ) {
    offset += (bytecount - 1);
    if ( !tiff_file_seek(p_file, offset) ) {
      if ( corecontext->tiff6params->f_verbose ) {
        monitorf(UVS("%%%%[ Error: Failed to seek to end of an image strip - file truncated? ]%%%%\n"));
      }
      return detail_error_handler(IOERROR, "Unable to seek to the end of TIFF image strip.") ;
    }
  }

  return TRUE ;

} /* Function tiff_check_strip */


Bool tiff_setup_read_data(
  corecontext_t       *corecontext,
  tiff_reader_t*      p_reader,   /* I/O */
  tiff_image_data_t*  p_data)     /* I/O */
{
  uint32            samples_per_pixel;
  uint32            bytes_per_row;
  uint32            rows_per_strip;
  uint32            rows_last_strip;
  uint32            number_strips;
  uint32            i;
  uint32            offset;
  uint32            bytecount;
  uint32            offsetcount;
  ifd_ifdentry_t*  p_ifdentry;

  HQASSERT((p_reader != NULL), "NULL reader pointer");
  HQASSERT((p_data != NULL), "NULL pointer to image data");
  HQASSERT((tiff_current_ifd_index(p_reader) != 0), "ifd index has not been set");

  /* Get strip offsets and byte counts IFD entries */
  p_reader->read_data.p_ifdentry_offsets = ifd_find_entry(tiff_current_ifd(p_reader), TAG_StripOffsets);
  p_reader->read_data.p_ifdentry_bytecounts = ifd_find_entry(tiff_current_ifd(p_reader), TAG_StripByteCounts);

  /* Ignore planar setting if only 1 sample per pixel.
   * Note: we could have just checked the PhotometricInterpretation for bilevel
   * or grey images since they usually only have 1 spp, but they could have
   * ExtraSamples for transparency etc. so the check is on the number of
   * declared spp. */
  p_reader->read_data.f_planar = (p_data->planar_config == PLANAR_CONFIG_PLANAR) &&
                                 (p_data->samples_per_pixel > 1);

  /* Get number of strips and rows per strip */
  p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader), TAG_RowsPerStrip);
  if ( p_ifdentry != NULL ) {
    rows_per_strip = ifd_entry_unsigned_int(p_ifdentry);

    if ( rows_per_strip > p_data->image_length ) {
      rows_per_strip = p_data->image_length;
    }

    number_strips = (p_data->image_length + (rows_per_strip - 1))/rows_per_strip;

    HQTRACE(tiff_debug,
            ("tiff_read_header: RowsPerStrip %u", rows_per_strip));

  } else { /* No RowsPerStrip entry - default to one BIG strip */
    number_strips = 1;
    rows_per_strip = p_data->image_length;

    HQTRACE(tiff_debug,
            ("tiff_read_header: no RowsPerStrip - defaulting to ImageLength"));
  }

  /* Check we have the right number of offsets and bytecounts for the image */
  offsetcount = p_data->strip_count;
  if ( offsetcount != p_data->strip_bytecount ) {
    return detail_error_handler(UNDEFINED, "Number of strip offsets and byte counts are different.") ;
  }
  if ( p_reader->read_data.f_planar ) {
    if ( offsetcount%(p_data->samples_per_pixel) != 0 ) {
      return detail_error_handler(UNDEFINED, "Wrong number of strip offsets for planar image data.") ;
    }
    offsetcount /= p_data->samples_per_pixel;
  }
  if ( offsetcount != number_strips ) {
    return detail_error_handler(UNDEFINED, "Wrong number of strip offsets for image data.") ;
  }

  /* Check all strip offsets and bytecounts for possible file truncation */
  for ( i = 0; i < offsetcount; i++ ) {
    offset = p_data->strip_offset[i];
    bytecount = p_data->strip_bytes[i];

    if ( !tiff_check_strip(corecontext, p_reader->p_file, offset, bytecount) ) {
      return FALSE ;
    }
  }

  p_data->number_strips = number_strips ;
  p_data->rows_per_strip = rows_per_strip ;

  /* Set up image strip tracker */
  p_reader->read_data.current_strip = 0;
  p_reader->read_data.number_strips = number_strips;

  /*
   * Work out how to read image strips.  The general case is an image with 2 or more strips.
   * We first work out how to read full size strips as defined by RowsPerStrip entry,
   * followed by how to read the last strip, usually with fewer rows.  There are three
   * special cases:
   * 1. single strip - this is handled as if it is the last image strip, the main strip
   *    reading info will be ignored.
   * 2. last strip is same size as main strips in which case we can use the same information
   *    as the main strips to read it.
   * 3. for planar image data, we effectively have single sample per pixel, but
   *    for samples-per-pixel times per strip!
   */
  samples_per_pixel = (p_reader->read_data.f_planar) ? 1u : p_data->samples_per_pixel;

  p_data->bytes_per_row =
    bytes_per_row = ((p_data->image_width) *(p_data->bits_per_sample)
                      *samples_per_pixel + BITS_BYTE - 1)
                      /BITS_BYTE;

  if ( number_strips > 1 ) {
    tiff_setup_strip_read( bytes_per_row,
                           rows_per_strip,
                           &(p_reader->read_data.image_strip));

    rows_last_strip = (p_data->image_length)%rows_per_strip;
    if ( rows_last_strip > 0 ) {
      tiff_setup_strip_read( bytes_per_row,
                             rows_last_strip,
                             &(p_reader->read_data.last_image_strip));

    } else {
      p_reader->read_data.last_image_strip = p_reader->read_data.image_strip;
    }
    p_reader->read_data.string_length = p_reader->read_data.image_strip.bytes_per_read;

  } else {
    tiff_setup_strip_read(bytes_per_row, p_data->image_length, &(p_reader->read_data.last_image_strip));
    p_reader->read_data.string_length = p_reader->read_data.last_image_strip.bytes_per_read;

    /* This data copy allows a simple test of TIFF_STRIP_FINISHED in tiff_read_file */
    tiff_setup_strip_read( bytes_per_row,
                           p_data->image_length,
                           &(p_reader->read_data.image_strip));
  }

  /* Note: we are only interested in yer actual image data samples, not the
   * extras, but we cheat for non-planar images by saying there is only one
   * component */
  p_reader->read_data.current_component = 0;

  p_reader->read_data.number_components = (p_reader->read_data.f_planar ?
                                           p_data->samples_per_pixel - p_data->extra_samples : 1) ;

  /* Allocate string memory for all strip components */
  p_reader->read_data.strmem_size = p_reader->read_data.string_length
                                    * p_reader->read_data.number_components;

  p_reader->read_data.f_update_rows = FALSE;
  p_reader->read_data.f_update_eodcount = FALSE;
  p_reader->read_data.f_decode = FALSE;

  /* TIFF image data does not need byte flipping (default) */

  /* only do shorts swap if 2 byte data and not bigendian
     we generally want big endian shorts for the PS image
     operator */
  p_data->shortswap = (p_data->bits_per_sample > 8 && !p_reader->bigendian);
  p_reader->shortswap = p_data->shortswap;

  /* Forces first strip read */
  p_reader->read_data.image_strip.current_read = p_reader->read_data.image_strip.reads_per_strip + 1;
  p_reader->read_data.last_image_strip.current_read = 1;

  return TRUE ;
}

/*
 * tiff_setup_read()
 */
Bool tiff_setup_read(
  corecontext_t       *corecontext,
  tiff_reader_t*      p_reader,   /* I */
  tiff_image_data_t*  p_data,     /* I */
  OBJECT*             ofile )  /* I */
{
  uint32            bytes_per_row;
  uint32            rows_per_strip;
  uint32            rows_last_strip;
  uint32            number_strips;
  uint32            max_strip_bytecount;
  uint32            strip_bytecount;
  uint32            i;
  uint32            eodcount;

  HQASSERT((p_reader != NULL),
           "tiff_setup_read: NULL reader pointer");
  HQASSERT((p_data != NULL),
           "tiff_setup_read: NULL pointer to image data");
  HQASSERT((ofile != NULL),
           "tiff_setup_read: NULL file object pointer");
  HQASSERT((tiff_current_ifd_index(p_reader) != 0),
           "tiff_setup_read: ifd index has not been set");

  number_strips = p_data->number_strips ;
  rows_per_strip = p_data->rows_per_strip ;
  bytes_per_row = p_data->bytes_per_row ;

  /* Allocate strip components */
  p_reader->read_data.components =
    mm_alloc(p_reader->mm_pool,
             p_reader->read_data.number_components * sizeof(tiff_component_t),
             MM_ALLOC_CLASS_TIFF_BUFFER);
  if ( p_reader->read_data.components == NULL ) {
    return error_handler(VMERROR);
  }

  /* Allocate string memory for all strip components */
  p_reader->read_data.p_strmem =
    mm_alloc(p_reader->mm_pool,
             p_reader->read_data.strmem_size * sizeof(uint8),
             MM_ALLOC_CLASS_TIFF_BUFFER);
  if ( p_reader->read_data.p_strmem == NULL ) {
    /* No need to free, trust the context and the pool will now be destroyed. */
    return error_handler(VMERROR);
  }

  /* Setup string objects for each strip component used to return image data */
  for ( i = 0; i < p_reader->read_data.number_components; i++ ) {
    theTags(p_reader->read_data.components[i].ostring) = OSTRING|UNLIMITED|LITERAL;
    theMark(p_reader->read_data.components[i].ostring) = ISLOCAL|ISNOTVM|SAVEMASK;
    theLen(p_reader->read_data.components[i].ostring) = (uint16)p_reader->read_data.string_length;
    oString(p_reader->read_data.components[i].ostring) =
      p_reader->read_data.p_strmem + i*(p_reader->read_data.string_length);
  }

  if ( p_data->compression != COMPRESS_None ) {
    /* Layer on top decompression filter */
    switch ( p_data->compression ) {
    case COMPRESS_CCITT:
      p_reader->read_data.f_update_rows = (number_strips > 1);
      object_store_name(&p_reader->read_data.oname_decode,
                        NAME_CCITTFaxDecode, LITERAL);
      if ( !tiff_ccitt_filter( p_reader->mm_pool,
                               p_data,
                               rows_per_strip,
                               &(p_reader->read_data.odict_decode)) ) {
        return FALSE ;
      }
      break;

    case COMPRESS_CCITT_T4:
      p_reader->read_data.f_update_rows = (number_strips > 1);
      object_store_name(&p_reader->read_data.oname_decode,
                        NAME_CCITTFaxDecode, LITERAL);
      if ( !tiff_ccitt4_filter( p_reader->mm_pool,
                                p_data,
                                rows_per_strip,
                                &(p_reader->read_data.odict_decode)) ) {
        return FALSE ;
      }
      break;

    case COMPRESS_CCITT_T6:
      object_store_name(&p_reader->read_data.oname_decode,
                        NAME_CCITTFaxDecode, LITERAL);
      if ( !tiff_ccitt6_filter( p_reader->mm_pool,
                                p_data,
                                &(p_reader->read_data.odict_decode)) ) {
        return FALSE ;
      }
      break;

    case COMPRESS_LZW:
      if ( corecontext->tiff6params->f_strict ) {
        /* Have LZW decoder look for EOD as per spec */
        eodcount = 0;

      } else { /* Read upto but not including the EOD */
        eodcount = bytes_per_row*rows_per_strip;
        if ( number_strips > 1 ) {
          rows_last_strip = p_data->image_length%rows_per_strip;
          p_reader->read_data.f_update_eodcount = (rows_last_strip > 0);
          if ( p_reader->read_data.f_update_eodcount ) {
            /* Update eodcount for last strip since has less data in it */
            p_reader->read_data.last_strip_eodcount = bytes_per_row*rows_last_strip;
          }
        }
      }
      object_store_name(&p_reader->read_data.oname_decode,
                        NAME_LZWDecode, LITERAL);
      if ( !tiff_lzw_filter( p_reader->mm_pool,
                             p_data, eodcount,
                             p_reader->bigendian,
                             &(p_reader->read_data.odict_decode)) ) {
        return FALSE ;
      }
      break;

    case COMPRESS_JPEG_OLD:
    case COMPRESS_JPEG:
      object_store_name(&p_reader->read_data.oname_decode,
                        NAME_DCTDecode, LITERAL);
      if ( !tiff_jpeg_filter( p_reader->mm_pool,
                              p_data,
                              &(p_reader->read_data.odict_decode),
            p_data->compression == COMPRESS_JPEG_OLD) ) {
        return FALSE ;
      }
      break;

    case COMPRESS_FLATE:
    case COMPRESS_FLATE_TIFFLIB:
       object_store_name(&p_reader->read_data.oname_decode,
                         NAME_FlateDecode, LITERAL);
      if (p_data->predictor != 1) {
        if ( !tiff_flate_filter( p_reader->mm_pool,
                                 p_data,
                                 &(p_reader->read_data.odict_decode)) ) {
          return FALSE ;
        }
      }
      break;


    case COMPRESS_Packbits:
      object_store_name(&p_reader->read_data.oname_decode,
                        NAME_RunLengthDecode, LITERAL);

      /* Need to layer SFD on compressed strip data since most likely wont have EOD marker */
      if ( !tiff_subfiledecode_filter( p_reader->mm_pool,
                                       &(p_reader->read_data.odict_sfd)) ) {
        return FALSE;
      }
      break;

    default:
      HQFAIL("tiff_setup_read: unknown compression should have been caught earlier");
      return FALSE ;
    }

    p_reader->read_data.f_decode = TRUE;
    /* First time in the reader we can skip tryng to close the decode filters. */
    p_reader->read_data.f_need_close = FALSE;

  } else { /* No decode filter use TIFF file as is */
    p_reader->read_data.f_decode = FALSE;
  }

  if ( p_data->fill_order == FILLORDER_LSB_TO_MSB ) {
    /* Layer underneath a byte flip filter */

    if ( !tiff_subfiledecode_filter( p_reader->mm_pool,
                                     &(p_reader->read_data.odict_flip)) )
      return FALSE;

    /* Setup datasource procedure for flip filter */
    if ( !tiff_alloc_psarray(p_reader->mm_pool, 1, &(p_reader->read_data.flip_source)) )
      return FALSE ;

    theTags(p_reader->read_data.flip_source) |= EXECUTABLE ;
    theTags(oArray(p_reader->read_data.flip_source)[0]) = OOPERATOR|EXECUTABLE ;
    oOp(oArray(p_reader->read_data.flip_source)[0]) = &system_ops[NAME_tiffbyteflip];

    /* The aim is to flip as much of a strip as possible upto max PS string length */
    max_strip_bytecount = 0;
    for ( i = 0; i < number_strips; i++ ) {
      strip_bytecount = ifd_entry_array_index(p_reader->read_data.p_ifdentry_bytecounts, i);
      if ( strip_bytecount > max_strip_bytecount ) {
        max_strip_bytecount = strip_bytecount;
      }
    }

    if ( max_strip_bytecount > TIFF_MAX_STRIP_BYTES )
      max_strip_bytecount = TIFF_MAX_STRIP_BYTES;

    p_reader->read_data.flip_length = max_strip_bytecount;
    p_reader->read_data.flipmem_size = max_strip_bytecount*(p_reader->read_data.number_components);

    /* Try and allocate byte flip buffer */
    p_reader->read_data.p_flipmem =
      mm_alloc(p_reader->mm_pool,
               p_reader->read_data.flipmem_size * sizeof(uint8),
               MM_ALLOC_CLASS_TIFF_BUFFER);
    if ( p_reader->read_data.p_flipmem == NULL ) {
      return error_handler(VMERROR);
    }

    /* Setup string objects for each strip component used to flip raw image data */
    for ( i = 0; i < p_reader->read_data.number_components; i++ ) {
      theTags(p_reader->read_data.components[i].ostring_flip) = OSTRING|UNLIMITED|LITERAL;
      theMark(p_reader->read_data.components[i].ostring_flip) = ISLOCAL|ISNOTVM|SAVEMASK;
      theLen(p_reader->read_data.components[i].ostring_flip) = (uint16)p_reader->read_data.flip_length;
      oString(p_reader->read_data.components[i].ostring_flip) =
        p_reader->read_data.p_flipmem + i*(p_reader->read_data.flip_length);
    }

    /* Remember source of raw TIFF image data */
    p_reader->read_data.flip_read = oFile(*ofile);
  }

  /* Remember the TIFF file object */
  Copy(&(p_reader->read_data.ofile_tiff), ofile);

  return TRUE ;

} /* Function tiff_setup_read */


/** do_shortflip()  swap bytes in shorts.
 */
static void do_shortflip(
  uint16*    p_buffer16,   /* I/O */
  uint32    len)           /* I number of bytes */
{
  HQASSERT((p_buffer16 != NULL),
           "do_shortflip: NULL pointer to data");

  HQASSERT(((len & 1u) == 0u),
           "do_shortflip: odd number of bytes");

  /* just make sure its even */
  len &= ~1u;

  /* Flip raw image data. */
  BYTE_SWAP16_BUFFER(p_buffer16, p_buffer16, len) ;
}


/* Shift Lab data from signed to unsigned. */
void tiff_Lab_shift(uint32 bits_per_sample,
                    uint8 *buffer, uint32 len,
                    uint32 *component, Bool chunky)
{
  HQASSERT(buffer != NULL, "No Lab samples");
  HQASSERT(bits_per_sample == 8 || bits_per_sample == 16,
           "Lab data should either be 16-bit or 8-bit") ;
  HQASSERT(component != NULL, "No Lab sample component") ;
  HQASSERT(*component >= 0 && *component <= 2, "Lab component component is out of range") ;
  HQASSERT(BOOL_IS_VALID(chunky), "Chunkiness boolean is invalid") ;

  if ( bits_per_sample == 8 ) {
    /* Shift (signed) byte up by 128 to make unsigned [0,255]. We use XOR
       with 128 for consistency with the 16-bit case, which uses XOR to
       shift the high-byte shorts. */
    if ( chunky ) {
      switch ( *component ) {
      case 1: /* Need two shifts to align to L */
        if ( len > 0 ) {
          *buffer++ ^= 128u ;
          --len ;
        }
        /*@fallthrough@*/
      case 2: /* Need one shift to align to L */
        if ( len > 0 ) {
          *buffer++ ^= 128u ;
          --len ;
        }
        /*@fallthrough@*/
      case 0: /* Now aligned with L */
        break ;
      }

      while ( len >= 24 ) {
        PENTIUM_CACHE_LOAD(buffer + 23) ;
        buffer[1] ^= 128u ;  buffer[2] ^= 128u ;
        buffer[4] ^= 128u ;  buffer[5] ^= 128u ;
        buffer[7] ^= 128u ;  buffer[8] ^= 128u ;
        buffer[10] ^= 128u ; buffer[11] ^= 128u ;
        buffer[13] ^= 128u ; buffer[14] ^= 128u ;
        buffer[16] ^= 128u ; buffer[17] ^= 128u ;
        buffer[19] ^= 128u ; buffer[20] ^= 128u ;
        buffer[22] ^= 128u ; buffer[23] ^= 128u ;
        buffer += 24 ;
        len -= 24;
      }
      while ( len >= 3 ) {
        buffer[1] ^= 128u ; buffer[2] ^= 128u ;
        buffer += 3;
        len -= 3;
      }

      if ( len > 1 )
        buffer[1] ^= 128u ;

      *component = len ;
    } else if ( *component != 0 ) {
      /* planar but skip L and only do a and b
         as L is already [0,255] */
      while ( len >= 8 ) {
        PENTIUM_CACHE_LOAD(buffer + 7) ;
        buffer[0] ^= 128u ;
        buffer[1] ^= 128u ;
        buffer[2] ^= 128u ;
        buffer[3] ^= 128u ;
        buffer[4] ^= 128u ;
        buffer[5] ^= 128u ;
        buffer[6] ^= 128u ;
        buffer[7] ^= 128u ;
        buffer += 8 ;
        len -= 8;
      }
      while ( len-- ) {
        *buffer++ ^= 128u ;
      }
    } /* else do nothing for L */
  } else {
    HQASSERT((len & 1) == 0, "Lab buffer length is not even") ;

    if (chunky) {
      switch ( *component ) {
      case 1: /* Need two shifts to align to L */
        if ( len > 0 ) {
          buffer[0] ^= 128u ;
          buffer += 2 ;
          len -= 2 ;
        }
        /*@fallthrough@*/
      case 2: /* Need one shift to align to L */
        if ( len > 0 ) {
          buffer[0] ^= 128u ;
          buffer += 2 ;
          len -= 2 ;
        }
        /*@fallthrough@*/
      case 0: /* Now aligned with L */
        break ;
      }

      while ( len >= 24 ) {
        PENTIUM_CACHE_LOAD(buffer + 23) ;
        buffer[2] ^= 128u ;  buffer[4] ^= 128u ;
        buffer[8] ^= 128u ;  buffer[10] ^= 128u ;
        buffer[14] ^= 128u ; buffer[16] ^= 128u ;
        buffer[20] ^= 128u ; buffer[22] ^= 128u ;
        buffer += 24 ;
        len -= 24;
      }
      while ( len >= 6 ) {
        buffer[2] ^= 128u ; buffer[4] ^= 128u ;
        buffer += 6;
        len -= 6;
      }

      if ( len > 2 )
        buffer[2] ^= 128u ;

      *component = len / 2 ;
    } else if ( *component != 0 ) {
      /* planar but skip L and only do a and b
         as L is already [0,65535] */
      while ( len >= 16 ) {
        PENTIUM_CACHE_LOAD(buffer + 15) ;
        buffer[0] ^= 128u ;
        buffer[2] ^= 128u ;
        buffer[4] ^= 128u ;
        buffer[6] ^= 128u ;
        buffer[8] ^= 128u ;
        buffer[10] ^= 128u ;
        buffer[12] ^= 128u ;
        buffer[14] ^= 128u ;
        buffer += 16 ;
        len -= 16;
      }
      while ( len ) {
        buffer[0] ^= 128u ;
        buffer += 2 ;
        len -= 2 ;
      }
    }
  }
}




/*
 * tiff_read_file()
 */
Bool tiff_read_file(tiff_reader_t*  p_reader,
                    STACK *stack)
{
  uint32            bytes;
  uint32            i;
  uint8*            p_buffer;
  tiff_file_data_t* p_data;
  tiff_strip_read_t* p_strip;
  tiff_component_t* p_component;
  OBJECT*           intobj;

  HQASSERT((p_reader != NULL),
           "tiff_read_file: NULL reader pointer");

  /* Convenience */
  p_data = &(p_reader->read_data);

  if ( p_data->current_strip != p_data->number_strips &&
       TIFF_STRIP_FINISHED(&p_data->image_strip) ) {
    /* Finished reading strip (and all components) - start a new one */

    if ( p_data->f_decode && p_data->f_need_close ) {
      /* Close all decode filter file objects before we create any new ones! */
      for ( i = 0; i < p_data->number_components; i++ ) {
        p_component = &(p_data->components[i]);

        if ( isIOpenFile(p_component->file_read) ) {
          /* Close decode filter(s) ready for next strip */
          if ( !(*theIMyCloseFile(p_component->file_read))(p_component->file_read, CLOSE_EXPLICIT) ) {
            return error_handler(IOERROR) ;
          }
        }
      }
      /* Will need to close filters next time around */
      p_data->f_need_close = TRUE;
    }

    for ( i = 0; i < p_data->number_components; i++ ) {
      OBJECT sourcefile = OBJECT_NOTVM_NOTHING ;

      p_component = &(p_data->components[i]);

      /* Find next strip(component) offset and byte count */
      p_component->offset = ifd_entry_array_index(p_data->p_ifdentry_offsets,
                                                   (i*(p_data->number_strips) + p_data->current_strip));
      HQTRACE(tiff_debug,
              ("tiff_read_file: strip %u(%u) initial offset is %u",
                p_data->current_strip, i, p_component->offset));

      /* Set up filter chain for decoding strip data.  Start off with a possible
       * byte flipping filter.  Next there could be a SubFileDecode to limit the
       * amount of strip data read to the length of the strip after compression.
       * Finally, add on any required compression decoding.
       * Rather than keep popping and pushing the current topmost filter it is
       * left on the operand stack until all the filters have been created.
       * Note: the SFD used to limit strip length reading is primarily for
       *       handling Packbits compression since the TIFF6 spec does not
       *       require an EOD marker.  The other compression schemes are
       *       (nominally) self limiting. */

      if ( oType(p_data->odict_flip) == ODICTIONARY ) {
        /* Set up byte flip filter */
        p_component->bytecount = ifd_entry_array_index(p_data->p_ifdentry_bytecounts,
                                                        (i*(p_data->number_strips) + p_data->current_strip));
        HQTRACE(tiff_debug,
                ("tiff_read_file: strip %u(%u) bytecount is %u",
                p_data->current_strip, i, p_component->bytecount));
        /* <file> <<..>> /SubFileDecode filter */
        if ( !filter_layer_object(&p_data->flip_source,
                                  NAME_AND_LENGTH("SubFileDecode"),
                                  &p_data->odict_flip,
                                  &sourcefile) )
          return FALSE ;
      } else {
        /* Data source is the TIFF file object */
        sourcefile = p_data->ofile_tiff ;
      }

      if ( oType(p_data->odict_sfd) == ODICTIONARY ) {
        /* Layer SubFileDecode on compressed strip data */

        /* Limit sfd to length of strip compressed data */
        intobj = fast_extract_hash_name(&(p_data->odict_sfd), NAME_EODCount);
        HQASSERT((intobj != NULL),
                 "tiff_read_file: EODCount not in decode dictionary when expected (2)");
        HQASSERT(oType(*intobj) == OINTEGER,
                 "tiff_read_file: EODCount in decode dictionary not integer (2)");
        oInteger(*intobj) = (int32)ifd_entry_array_index(p_data->p_ifdentry_bytecounts,
                                                          (i*(p_data->number_strips) + p_data->current_strip));

        /* <file> <<..>> /SubFileDecode filter */
        if ( !filter_layer_object(&sourcefile,
                                  NAME_AND_LENGTH("SubFileDecode"),
                                  &p_data->odict_sfd,
                                  &sourcefile) )
          return FALSE ;
      }

      if ( p_data->f_decode ) {
        /* Restart the decode filter for the new strip */

        /* Update rows for last strip if needed */
        if ( p_data->f_update_rows &&
             (p_data->current_strip == (p_data->number_strips - 1)) ) {
          intobj = fast_extract_hash_name(&(p_data->odict_decode), NAME_Rows);
          HQASSERT((intobj != NULL),
                   "tiff_read_file: Rows not in decode dictionary when expected");
          HQASSERT(oType(*intobj) == OINTEGER,
                   "tiff_read_file: Rows in decode dictionary is not an integer");
          oInteger(*intobj) = (int32)p_data->last_image_strip.rows;
          HQASSERT(oInteger(*intobj) > 0, "Image Rows overflowed") ;
        } else if ( p_data->f_update_eodcount &&
                    (p_data->current_strip == (p_data->number_strips - 1)) ) {
          /* Update EODCount if needed */
          intobj = fast_extract_hash_name(&(p_data->odict_decode), NAME_EODCount);
          HQASSERT((intobj != NULL),
                   "tiff_read_file: EODCount not in decode dictionary when expected");
          HQASSERT(oType(*intobj) == OINTEGER,
                   "tiff_read_file: EODCount in decode dictionary is not an integer");
          oInteger(*intobj) = (int32)p_data->last_strip_eodcount;
          HQASSERT(oInteger(*intobj) > 0, "EODCount overflowed") ;
        }

        /* <file> <<..>> /<decodefilter> filter */
        if ( !filter_layer_object(&sourcefile,
                                  theICList(oName(p_data->oname_decode)),
                                  theINLen(oName(p_data->oname_decode)),
                                  (oType(p_data->odict_decode) == ODICTIONARY ?
                                   &p_data->odict_decode : NULL),
                                  &sourcefile) )
          return FALSE ;
      }

      HQASSERT(oType(sourcefile) == OFILE, "TIFF filtered source is not a file") ;
      p_component->file_read = oFile(sourcefile);
    }

    p_data->component = &(p_data->components[0]);

    HQTRACE(tiff_debug,
            ("tiff_read_file: strip %u(0) offset is %u",
             p_data->current_strip, p_data->component->offset));

    /* Reposition TIFF file for first component of next strip */
    if ( !tiff_file_seek(p_reader->p_file, p_data->component->offset) ) {
      return detail_error_handler(IOERROR, "Unable to seek to start of TIFF image strip.") ;
    }

    /* Reset strip and component read counts for normal strip */
    p_data->image_strip.current_read = 1;
    p_data->current_component = 1;
    p_data->current_strip++;

  } else if ( p_data->f_planar ) {
    /* Move onto next strip component to read from */
    if ( p_data->current_component == p_data->number_components ) {
      p_data->current_component = 0;
    }
    p_data->component = &(p_data->components[p_data->current_component]);

    HQTRACE(tiff_debug,
            ("tiff_read_file: strip %u(%u) offset is %u",
             (p_data->current_strip - 1), p_data->current_component, p_data->component->offset));

    p_data->current_component++;

    /* Reposition underlying TIFF file for next strip component */
    if ( !tiff_file_seek(p_reader->p_file, p_data->component->offset) ) {
      return detail_error_handler(IOERROR, "Unable to seek to TIFF image strip component data.") ;
    }
  }

  /* Pick up appropriate strip read info based on strip being read and bytes to read */
  p_strip = (p_data->current_strip == p_data->number_strips)
    ? &(p_data->last_image_strip)
    : &(p_data->image_strip);

  bytes = TIFF_STRIP_LAST_READ(p_strip)
    ? p_strip->bytes_last_read
    : p_strip->bytes_per_read;

  if (p_reader->shortswap) {
    bytes &= ~1u; /* just incase its odd */
  }
  if ( p_data->current_component == p_data->number_components ) {
    /* Update current read within strip whenever we have read all the components */
    p_strip->current_read++;
  }

  p_component = p_data->component;
  p_buffer = oString(p_component->ostring);

  if ( file_read(p_component->file_read, p_buffer, bytes, NULL) <= 0 )
    return detail_error_handler(IOERROR, "Unable to read image data.") ;

  if (p_reader->shortswap) {
    /* image data is little endian make it big endian for the CORE */
    do_shortflip((uint16*)p_buffer,bytes);
  }

  if (p_reader->lab_shift != 0) {
    /* shift lab images up a bit */
    uint32 component = p_data->current_component - 1;

    /* for CIE Lab colorspace the a and b indexes run [-128,128] so shift them
       so they run [0,255] */
    tiff_Lab_shift(p_reader->lab_shift, p_buffer, bytes, &component,
                   !p_data->f_planar) ;
  }

  if ( p_data->f_planar ) {
    /* Keep track of where to read next lot of data from for this component */
    if ( !tiff_file_pos(p_reader->p_file, &(p_component->offset)) ) {
      return detail_error_handler(IOERROR, "Unable to find position in TIFF file.") ;
    }
    HQTRACE(tiff_debug,
            ("tiff_read_file: strip %u(%u) new offset %u",
             (p_data->current_strip - 1), (p_data->current_component - 1), p_component->offset));
  }

  /* Update how many bytes in string and put data string on the stack */
  theLen(p_component->ostring) = CAST_TO_UINT16(bytes);
  return push(&(p_component->ostring), stack) ;

} /* Function tiff_read_file */


/** do_byteflip().  Reverses the bit order for each byte in the buffer.
 */
void do_byteflip(
  uint8*    p_buffer,   /* I/O */
  uint32    len)        /* I */
{
  HQASSERT((p_buffer != NULL),
           "do_byteflip: NULL pointer to data");

  /* Flip raw image data. */
  if ( len >= 8 ) {
    do {
      p_buffer[7] = reversed_bits_in_byte[p_buffer[7]];
      p_buffer[6] = reversed_bits_in_byte[p_buffer[6]];
      p_buffer[5] = reversed_bits_in_byte[p_buffer[5]];
      p_buffer[4] = reversed_bits_in_byte[p_buffer[4]];
      p_buffer[3] = reversed_bits_in_byte[p_buffer[3]];
      p_buffer[2] = reversed_bits_in_byte[p_buffer[2]];
      p_buffer[1] = reversed_bits_in_byte[p_buffer[1]];
      p_buffer[0] = reversed_bits_in_byte[p_buffer[0]];
      p_buffer += 8;
    } while ( (len -= 8) >= 8 );
  }

  switch ( len ) {
  case 7: p_buffer[6] = reversed_bits_in_byte[p_buffer[6]];
  case 6: p_buffer[5] = reversed_bits_in_byte[p_buffer[5]];
  case 5: p_buffer[4] = reversed_bits_in_byte[p_buffer[4]];
  case 4: p_buffer[3] = reversed_bits_in_byte[p_buffer[3]];
  case 3: p_buffer[2] = reversed_bits_in_byte[p_buffer[2]];
  case 2: p_buffer[1] = reversed_bits_in_byte[p_buffer[1]];
  case 1: p_buffer[0] = reversed_bits_in_byte[p_buffer[0]];
  default:
    break;
  }

} /* Function do_byteflip */


/** tiff_byte_flip() - data source procedure for SubFileDecode filter
 * used to reverse bytes in TIFF image data when FillOrder == 2(LSB to MSB).
 */
Bool tiff_byte_flip(tiff_reader_t* p_reader,
                    STACK *stack)
{
  uint32            bytes;
  uint8*            p_buffer;
  tiff_file_data_t* p_data;
  tiff_component_t* p_component;

  HQASSERT((p_reader != NULL),
           "tiff_byte_flip: NULL reader pointer");
  HQASSERT(oType(p_reader->read_data.odict_flip) == ODICTIONARY,
           "tiff_byte_flip: called when not byte flipping!");

  /* Convenience */
  p_data = &(p_reader->read_data);
  p_component = p_data->component;

  /* Work out how many bytes in strip component to read and flip */
  bytes = (p_component->bytecount > p_data->flip_length)
              ? CAST_SIZET_TO_UINT32(p_data->flip_length)
              : p_component->bytecount;

  HQTRACE(tiff_debug,
          ("tiff_byte_flip: strip %u(%u) bytes to flip %u",
           (p_data->current_strip - 1), (p_data->current_component - 1), bytes));

  /* Update number of bytes in strip component string that will be read */
  theLen(p_component->ostring_flip) = CAST_TO_UINT16(bytes);

  if ( bytes > 0 ) {
    /* There is to data read - update amount left to read from strip component later */
    p_component->bytecount -= bytes;

    HQTRACE(tiff_debug,
            ("tiff_byte_flip: strip %u(%u) remaining bytecount %u",
             (p_data->current_strip - 1), (p_data->current_component - 1), p_component->bytecount));

    /* Read raw image data */
    p_buffer = oString(p_component->ostring_flip);
    if ( file_read(p_data->flip_read, p_buffer, bytes, NULL) <= 0 )
      return detail_error_handler(IOERROR, "Unable to read raw image data.") ;

    /* Flip raw image data. */
    do_byteflip(p_buffer, bytes);
  }

  /* Put data string on the stack */
  return push(&(p_component->ostring_flip), stack) ;

} /* Function tiff_byte_flip */


/* Log stripped */
