/** \file
 * \ingroup tiffcore
 *
 * $HopeName: tiffcore!export:ifdreadr.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API for reading Tiff IFD's
 */

#ifndef __IFDREADR_H__
#define __IFDREADR_H__ (1)

/** \defgroup tiffcore IFD handling for Tiff images
    \ingroup images */
/** \{ */

#include "tifffile.h"   /* tiff_file_t */
#include "mm.h"         /* mm_pool_t */
#include "lists.h"      /* dll_link_t */

/*
 * IFD entry flags
 */
#define ENTRY_ARRAY     (0x01)    /* Entry is an array (count > 1) */
#define ENTRY_INDIRECT  (0x02)    /* Value not stored in offset */
#define ENTRY_PENDING   (0x04)    /* Value not yet read in (implies INDIRECT) */

/*
 *  Convenient constant when first reading an IFD
 */
#define ENTRY_INDIRECT_PENDING  (ENTRY_INDIRECT|ENTRY_PENDING)


/* 
 * The various IFD entry tag numbers we currently care about (these are the standard tiff6 set, which include some that are also used by wmphoto.
 */
#define TAG_NewSubfileType               ((tiff_short_t)(254))
#define TAG_SubfileType                  ((tiff_short_t)(255))
#define TAG_ImageWidth                   ((tiff_short_t)(256))
#define TAG_ImageLength                  ((tiff_short_t)(257))
#define TAG_BitsPerSample                ((tiff_short_t)(258))
#define TAG_Compression                  ((tiff_short_t)(259))
#define TAG_PhotometricInterpretation    ((tiff_short_t)(262))
#define TAG_Thresholding                 ((tiff_short_t)(263))
#define TAG_CellWidth                    ((tiff_short_t)(264))
#define TAG_CellLength                   ((tiff_short_t)(265))
#define TAG_FillOrder                    ((tiff_short_t)(266))
#define TAG_DocumentName                 ((tiff_short_t)(269))
#define TAG_ImageDescription             ((tiff_short_t)(270))
#define TAG_Make                         ((tiff_short_t)(271))
#define TAG_Model                        ((tiff_short_t)(272))
#define TAG_StripOffsets                 ((tiff_short_t)(273))
#define TAG_Orientation                  ((tiff_short_t)(274))
#define TAG_SamplesPerPixel              ((tiff_short_t)(277))
#define TAG_RowsPerStrip                 ((tiff_short_t)(278))
#define TAG_StripByteCounts              ((tiff_short_t)(279))
#define TAG_MinSampleValue               ((tiff_short_t)(280))
#define TAG_MaxSampleValue               ((tiff_short_t)(281))
#define TAG_XResolution                  ((tiff_short_t)(282))
#define TAG_YResolution                  ((tiff_short_t)(283))
#define TAG_PlanarConfiguration          ((tiff_short_t)(284))
#define TAG_PageName                     ((tiff_short_t)(285))
#define TAG_XPosition                    ((tiff_short_t)(286))
#define TAG_YPosition                    ((tiff_short_t)(287))
#define TAG_FreeOffsets                  ((tiff_short_t)(288))
#define TAG_FreeByteCounts               ((tiff_short_t)(289))
#define TAG_GrayResponseUnit             ((tiff_short_t)(290))
#define TAG_GrayResponseCurve            ((tiff_short_t)(291))
#define TAG_T4Options                    ((tiff_short_t)(292))
#define TAG_T6Options                    ((tiff_short_t)(293))
#define TAG_ResolutionUnit               ((tiff_short_t)(296))
#define TAG_PageNumber                   ((tiff_short_t)(297))
#define TAG_TransferFunction             ((tiff_short_t)(301))
#define TAG_Software                     ((tiff_short_t)(305))
#define TAG_DateTime                     ((tiff_short_t)(306))
#define TAG_Artist                       ((tiff_short_t)(315))
#define TAG_HostComputer                 ((tiff_short_t)(316))
#define TAG_Predictor                    ((tiff_short_t)(317))
#define TAG_WhitePoint                   ((tiff_short_t)(318))
#define TAG_PrimaryChromaticities        ((tiff_short_t)(319))
#define TAG_ColorMap                     ((tiff_short_t)(320))
#define TAG_HalftoneHints                ((tiff_short_t)(321))
#define TAG_TileWidth                    ((tiff_short_t)(322))
#define TAG_TileLength                   ((tiff_short_t)(323))
#define TAG_TileOffsets                  ((tiff_short_t)(324))
#define TAG_TileByteCounts               ((tiff_short_t)(325))
#define TAG_SubIFDs                      ((tiff_short_t)(330))  /* TIFF PM6.0 */
#define TAG_InkSet                       ((tiff_short_t)(332))
#define TAG_InkNames                     ((tiff_short_t)(333))
#define TAG_NumberOfInks                 ((tiff_short_t)(334))
#define TAG_DotRange                     ((tiff_short_t)(336))
#define TAG_TargetPrinter                ((tiff_short_t)(337))
#define TAG_ExtraSamples                 ((tiff_short_t)(338))
#define TAG_SampleFormat                 ((tiff_short_t)(339))
#define TAG_SMinSampleValue              ((tiff_short_t)(340))
#define TAG_SMaxSampleValue              ((tiff_short_t)(341))
#define TAG_TransferRange                ((tiff_short_t)(342))
#define TAG_ClipPath                     ((tiff_short_t)(343))  /* TIFF PM6.0 */
#define TAG_XClipPathUnits               ((tiff_short_t)(344))  /* TIFF PM6.0 */
#define TAG_YClipPathUnits               ((tiff_short_t)(345))  /* TIFF PM6.0 */
#define TAG_Indexed                      ((tiff_short_t)(346))  /* TIFF PM6.0 */
#define TAG_JPEGTables                   ((tiff_short_t)(347))
#define TAG_OPIProxy                     ((tiff_short_t)(351))  /* TIFF PM6.0 */
#define TAG_JPEGProc                     ((tiff_short_t)(512))
#define TAG_JPEGInterchangeFormat        ((tiff_short_t)(513))
#define TAG_JPEGInterchangeFormatLength  ((tiff_short_t)(514))
#define TAG_JPEGRestartInterval          ((tiff_short_t)(515))
#define TAG_JPEGLosslessPredictors       ((tiff_short_t)(517))
#define TAG_JPEGPointTransforms          ((tiff_short_t)(518))
#define TAG_JPEGQTables                  ((tiff_short_t)(519))
#define TAG_JPEGDCTables                 ((tiff_short_t)(520))
#define TAG_JPEGACTables                 ((tiff_short_t)(521))
#define TAG_YCbCrCoefficients            ((tiff_short_t)(529))
#define TAG_YCbCrSubSampling             ((tiff_short_t)(530))
#define TAG_YCbCrPositioning             ((tiff_short_t)(531))
#define TAG_ReferenceBlackWhite          ((tiff_short_t)(532))
#define TAG_ImageID                      ((tiff_short_t)(32781)) /* TIFF PM6.0 */
#define TAG_Copyright                    ((tiff_short_t)(33432))
#define TAG_IPTC_NAA                     ((tiff_short_t)(33723))
#define TAG_Site                         ((tiff_short_t)(34016))
#define TAG_ColorSequence                ((tiff_short_t)(34017))
#define TAG_IT8Header                    ((tiff_short_t)(34018))
#define TAG_RasterPadding                ((tiff_short_t)(34019))
#define TAG_BitsPerRunLength             ((tiff_short_t)(34020))
#define TAG_BitsPerExtendedRunLength     ((tiff_short_t)(34021))
#define TAG_ColorTable                   ((tiff_short_t)(34022))
#define TAG_ImageColorIndicator          ((tiff_short_t)(34023))
#define TAG_BackgroundColorIndicator     ((tiff_short_t)(34024))
#define TAG_ImageColorValue              ((tiff_short_t)(34025))
#define TAG_BackgroundColorValue         ((tiff_short_t)(34026))
#define TAG_PixelIntensityRange          ((tiff_short_t)(34027))
#define TAG_TransparencyIndicator        ((tiff_short_t)(34028))
#define TAG_ColorCharacterization        ((tiff_short_t)(34029))
#define TAG_Photoshop3ImageResource      ((tiff_short_t)(34377))
#define TAG_ICCProfile                   ((tiff_short_t)(34675))
#define TAG_ImageSourceData              ((tiff_short_t)(37724))
#define TAG_Annotations                  ((tiff_short_t)(50255))

#define TAG_Unknown   ((tiff_short_t)65535)

#define UNKNOWN_T  (uint8 *)("Unknown")


/* 
 * Contents for the attributes flags bitmask 
 */
#define HF_DEFAULTS   BIT(0)    /* Defaults to the given values if not present */
#define HF_MANDATORY  BIT(1)    /* Tag must exist */
#define HF_INVARIANT  BIT(2)    /* Tag must have the default contents if present */
#define HF_MINMAX     BIT(3)    /* Tag has min-max values (incompat with array,options) */
#define HF_ARRAY      BIT(4)    /* Tag has an array of values (incompat with minmax,options) */
#define HF_OPTIONS    BIT(5)    /* Tag has an array of possible values (incompat with minmax,array) */
#define HF_FIXED      BIT(6)    /* Tag has a single, fixed, value */


/* 
 * Abbreviations to make tables easier to build 
 */
#define HFD           HF_DEFAULTS
#define HFM           HF_MANDATORY
#define HFI           HF_INVARIANT
#define HFX           HF_MINMAX
#define HFA           HF_ARRAY
#define HFO           HF_OPTIONS
#define HFF           HF_FIXED


/* 
 * Abbreviations for type bitmasks 
 */
#define TB_B        BIT(ENTRY_TYPE_BYTE)
#define TB_A        BIT(ENTRY_TYPE_ASCII)
#define TB_S        BIT(ENTRY_TYPE_SHORT)
#define TB_L        BIT(ENTRY_TYPE_LONG)
#define TB_R        BIT(ENTRY_TYPE_RATIONAL)
#define TB_F        BIT(ENTRY_TYPE_FLOAT)
#define TB_D        BIT(ENTRY_TYPE_DOUBLE)
#define TB_U        BIT(ENTRY_TYPE_UNDEFINED)

#define TB_BS       (TB_B | TB_S)
#define TB_SL       (TB_S | TB_L)
#define TB_BL       (TB_B | TB_L)
#define TB_BSL      (TB_B | TB_S | TB_L) 
#define TB_INTEGER  (TB_B | TB_A | TB_S | TB_L)


/*
 * Reader context opaque type.
 */
 
typedef struct ifd_reader_s ifd_reader_t;

/*
 * IFD and IFD entry opaque types
 */
typedef struct ifd_ifd_s ifd_ifd_t;  
typedef struct ifd_ifdentry_s ifd_ifdentry_t;

/*
 * Structure for an IFD
 */
struct ifd_ifd_s {
  uint32            offset;       /* Offset to start of IFD */
  uint32            offset_next;  /* Offset to start of next IFD */
  uint32            num_entries;  /* Number of entries in IFD */
  ifd_ifdentry_t*   p_entries;    /* Pointer to array of entries */
};


/*
 * Structure for a IFD entry
 */
struct ifd_ifdentry_s {
  tiff_short_t  tag;        /* Tag number */
  tiff_short_t  type;       /* Tag type */
  int32         flags;      /* Entry attributes */
  union { /* Arrays to hold the local value */
    tiff_ascii_t  tiff_ascii[4];
    tiff_byte_t   tiff_byte[4];
    tiff_sbyte_t  tiff_sbyte[4];
    tiff_short_t  tiff_short[2];
    tiff_sshort_t tiff_sshort[2];
    tiff_long_t   tiff_long; /* Either the value, or the offset */
    tiff_slong_t  tiff_slong; /* Either the value, or the offset */
    tiff_float_t  tiff_float; /* Either the value, or the offset */
  } u;
  void*         p_data;     /* Pointer to extended value storage */
  tiff_long_t   count;      /* Number of values in entry */
};

/*
 * Structure holding meta data on IFD entries
 */
typedef struct ifd_ifdentry_data_s {
  tiff_short_t  tag;                    /* Tag value */
  tiff_short_t  spare;                  /* SPARE (I am a 16 bit space)*/
  uint8*        name;                   /* Name as a string */
  int32         f_read_immediate;       /* Read indirect values immediately */
} ifd_ifdentry_data_t;


/*
 * Structure for checking IFD entries against defaults.
 * TIFF 6 spec says a reader should accept BYTE, SHORT or LONG
 * values for any unsigned integer field, not just the types
 * given in the spec.  strict_types should match what the entry
 * is documented to have and compatible_types should have the
 * integer types that make up the list BSL.
 */
typedef struct ifd_ifdentry_check_s {
  tiff_short_t  tag;              /* TIFF tag */
  uint32        flags;            /* Attribute flags */ 
  uint32        strict_types;     /* Strict types bitmask */
  uint32        compatible_types; /* Compatible types bitmask */
  uint32        count;            /* Number of values */
  uint32        value;            /* Scalar value */
  uint32*       p_value;          /* Array of values */
} ifd_ifdentry_check_t;


/*
 * Struct for keeping a list of IFDs for a file
 */
typedef struct ifd_ifdlink_s {
  dll_link_t    dll;
  ifd_ifd_t*   p_ifd;
} ifd_ifdlink_t;

/*
 * ifd_new_reader() creates a new ifd reader context with the given
 * buffer context.  It returns TRUE if the reader is created ok, else 
 * FALSE and sets VMERROR.
 * ifd_free_reader() releases a ifd reader context and any IFDs (and
 * their entries, and so forth) that have been read in.
 */
Bool ifd_new_reader(
  tiff_file_t*    p_file,         /* I */
  mm_pool_t       mm_pool,        /* I */
  ifd_ifdentry_data_t* known_tags,
  int32         f_abort_on_unknown,
  int32         f_strict,
  int32         f_verbose,
  ifd_reader_t** pp_new_reader); /* O */

void ifd_free_reader(
  ifd_reader_t** pp_reader);     /* I */
  
void ifd_free_ifd(
  mm_pool_t       mm_pool,    /* I */
  ifd_ifd_t**    pp_ifd);     /* I */


/*
 * ifd_read_header() reads the TIFF/WMPhoto/Exif file Image File Header to check
 * the files endian byte order, the TIFF file check number, and the initial
 * IFD offset.  If all of these are ok, then it returns TRUE, else it returns
 * FALSE and sets IOERROR, or UNDEFINED.
 * ifd_read_ifds() reads all the IFDs in an ifd file, and returns the number
 * of IFDs successfully read.  If all the IFDs are read ok the function returns 
 * TRUE, else it returns FALSE and sets VMERROR, IOERROR, UNDEFINED, or 
 * TYPECHECK.  This function should only be called once for a file.
 */
Bool ifd_read_header(
  ifd_reader_t*  p_reader);      /* I */

Bool ifd_read_ifds(
  ifd_reader_t*  p_reader,       /* I */
  uint32*         p_number_ifds); /*  */

/*
 * ifd_verify_ifd() checks the given IFD against an array of given entry
 * data.  It returns TRUE if the IFD satisfies the check data, else it 
 * returns FALSE and sets UNDEFINED, TYPECHECK, or RANGECHECK.  Note, the
 * array of check data must be in ascending tag order, and all IFD entries
 * with offset values must have been loaded.
 */
extern int32 ifd_verify_ifd(
  ifd_reader_t*  p_reader,
  ifd_ifd_t*             p_ifd,            /* I */
  ifd_ifdentry_check_t*  p_ifd_defaults,   /* I */
  uint32                  num_defaults);    /* I */


ifd_ifd_t*   current_ifd(ifd_reader_t*  p_reader);
uint32  current_ifd_index(ifd_reader_t*  p_reader);
uint32 count_ifds(ifd_reader_t*  p_reader);
dll_list_t* dls_ifds(ifd_reader_t*  p_reader);
void set_ifd_iterator(ifd_reader_t*  p_reader, ifd_ifd_t* p_ifd, uint32 ifd_index);
uint32 first_ifd_offset(ifd_reader_t*  p_reader);
void set_first_ifd_offset(ifd_reader_t* p_reader, tiff_long_t f_off);

/*
 * ifd_find_entry() returns an IFD entry that matches the given entry tag,
 * else it returns NULL.
 */
extern ifd_ifdentry_t* ifd_find_entry(
  ifd_ifd_t*      p_ifd,           /* I */
  tiff_short_t     entry_tag);      /* I */


/*
 * ifd_entry_unsigned_int() and ifd_entry_rational() return the given entry 
 * scalar value as the appropriate type.  It is the callers responsibility to 
 * ensure the entry is scalar and of the right type.  ifd_entry_array_index() 
 * returns the given indexed element of the array entry widened to a uint32.  
 * It is the callers responsibility to ensure that the entry is an array and 
 * that the index is in range.
 */
extern uint32 ifd_entry_unsigned_int(
  ifd_ifdentry_t*  p_entry);       /* I */

extern int32 ifd_entry_signed_int(
  ifd_ifdentry_t*  p_entry);       /* I */
  
extern void ifd_entry_float(
  ifd_ifdentry_t*  p_entry,   /* I */
  tiff_float_t* flt );        /* O */
  
extern void ifd_entry_rational(
  ifd_ifdentry_t*  p_entry,        /* I */
  tiff_rational_t   rational);      /* O */

extern void ifd_entry_srational(
  ifd_ifdentry_t*  p_entry,        /* I */
  tiff_srational_t   srational);    /* O */

extern uint8* ifd_entry_string( ifd_ifdentry_t*  p_entry);

extern uint32 ifd_entry_array_index(
  ifd_ifdentry_t*  p_entry,        /* I */
  uint32            index);         /* I */

extern int32 ifd_signed_entry_array_index(
  ifd_ifdentry_t*  p_entry,      /* I */
  uint32            index);       /* I */

extern uint8* ifd_entry_array_fetch_string(
  ifd_ifdentry_t*  p_entry, 
  uint32 offset, 
  uint32 length, 
  uint8* dest);

extern int32 ifd_entry_array_check_substring(
  ifd_ifdentry_t*  p_entry, 
  uint32 offset, 
  uint32 length, 
  uint8* src);

extern void ifd_entry_rational_array_index(
  ifd_ifdentry_t*  p_entry,
  uint32            index, 
  tiff_rational_t   value);


/*
 * ifd_entry_type() returns the type of the IFD entry.
 * ifd_entry_count() returns the count of values for the IFD entry.
 * ifd_entry_offset() returns the file offset to the IFD entry data.
 */
extern tiff_short_t ifd_entry_type(
  ifd_ifdentry_t*  p_entry);       /* I */

extern uint32 ifd_entry_count(
  ifd_ifdentry_t*  p_entry);       /* I */

extern uint32 ifd_entry_offset(
  ifd_ifdentry_t*  p_entry);       /* I */

/** \} */

#endif /* !__IFDREADR_H__ */


/* Log stripped */
