
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/*  pcleo.h  */


#if PCLEO_RDR

#ifndef __PCLEO__
#define __PCLEO__

/* Offsets into font header data when not referencing via struct -ss 6/2/92 */
/****
#define    FNT_DESC_SIZE        (0)
***/
#define    DESC_FORMAT          (2)
#define    SS_ORG               (3)
/***
#define    STYLE_MSB            (4)
#define    BASELINE_POS         (6)
***/
#define    CELL_WIDTH           (8)
#define    CELL_HEIGHT          (10)
/***
#define    ORIENTATION          (11)
#define    SPACING              (12)
***/
#define    SS_CODE              (14)
/***
#define    PITCH                (16)
#define    HEIGHT               (18)
#define    XHEIGHT              (20)
#define    APPEAR_WIDTH         (22)
#define    STYLE_LSB            (23)
#define    STROKE_WT            (24)
#define    TYPEFACE_LSB         (25)
#define    TYPEFACE_MSB         (26)
#define    SERIF_STYLE          (27)
#define    QUALITY              (28)
#define    PLACEMENT            (29)
#define    WAS_ULINE_POS        (30)
#define    WAS_ULINE_HT         (31)
***/
#define    TEXT_HEIGHT          (32)
#define    TEXT_WIDTH           (34)
#define    FIRST_CODE           (36)
#define    LAST_CODE            (38)
/***
#define    PITCH_EXT            (40)
#define    HEIGHT_EXT           (41)
#define    CAP_HEIGHT           (42)
***/
#define    FONT_NUMBER          (44)
/***
#define    FONT_NAME            (48)
#define    SCALE_FACTOR         (64)
#define    XRES                 (66)
#define    YRES                 (68)
#define    ULINE_POS            (70)
#define    ULINE_HT             (72)
#define    OR_THRESH            (74)
#define    GLOBAL_IT_ANG        (76)
#define    GIFCT                (78)
***/
/* Offsets into font descriptor format 15 header data for values >= 64 */
#define    PTO_SCALEFACTOR         64
/***
#define    PTO_MASTERULINEPOSITION 66
#define    PTO_MASTERULINEHEIGHT   68
***/
#define    PTO_FST                 70
/***
#define    PTO_VARIETY             71
***/

#define BOTH_10_AND_11 78	/* length of shared header, Formats 10 & 11 */
#define CHAR_COMPL_LEN 8	/* length of char-complement field, Format 11 */

#define FORMAT_0   64		/* length of format 0 header */
#define FORMAT_20  68		/* length of format 20 header */

#define    PCLFONT_NUMBER_MASK  (0x00FFFFFFL) /* actual # in low 6 bytes */

typedef struct
{
    UW16    fontDescriptorSize;       /* min of 80 for Format 10, 88 for Format 11 */
    UB8    descriptorFormat;         /* 10-Intellifont 11-Unbound Intellifont */
    UB8    symbolSetOrganization;    /* FontType from symset map */
    UB8    style_msb;                /* from Font Alias Table = FAT */
    UB8    Reserved1;
    UW16    baselinePosition;         /* Attr Hdr, also Display Hdr */
    UW16    cellWidth;                /* ignored by Galaxy */
    UW16    cellHeight;               /* ignored by Galaxy */
    UB8    orientation;              /* 0, not used */
    UB8    spacing;                  /* fixed/proportional, FAT */
    UW16    symSetCode;               /* derived from symset map */
    UW16    pitch;                    /* spaceband from Attr Hdr */
    UW16    height;                   /* master pt size from Display Hdr */
    UW16    xHeight;                  /* Attr Hdr */
    SB8     appearanceWidth;          /* WidthType, FAT */
    UB8    style_lsb;                /* posture, FAT */
    SB8     strokeWeight;             /* from FAT */
    UB8    lsb_typeface;             /* from FAT (LSB) (02-20-91 jfd) */
    UB8    msb_typeface;             /* from FAT (MSB) (02-20-91 jfd) */
    UB8    serifStyle;               /* from FAT */
    UB8    quality;                  /* 2, ignored */
    UB8    placement;                /* 0, ignored */
    SB8     was_underlinePosition;    /* Attr Hdr value needs word */
    UB8    was_underlineHeight;      /* Attr Hdr value needs word */
    UW16    textHeight;               /* for HPGL */
    UW16    textWidth;                /* for HPGL */
    UW16    firstCode;                /* from symset map */
    UW16    lastCode;                 /* from symset map */
    UB8    pitch_ext;                /* 0, ignored */
    UB8    height_ext;               /* 0, ignored */
    UW16    capHeight;                /* Attr Hdr */
    UL32    fontNumber;               /* FAT */
    UB8    fontName[16];             /* FAT */
    UW16    scaleFactor;              /* Attr Hdr */
    UW16    xRes;                     /* Display Hdr */
    UW16    yRes;                     /* Display Hdr */
    SW16     underlinePosition;        /* Attr Hdr */
    UW16    underlineHeight;          /* Attr Hdr */
    UW16    orThreshold;              /* from Raster Params */
    UW16    globItalAng;              /* from Raster Params */
	UW16    gifct;                   /* global IF size */
	UB8		char_complement[8];		 /* char complement (Format 11 only) */
		
} HPFH;
typedef HPFH * PHPFH;

/* PCL 5 class types */
#define PCL_BITMAP_CLASS   1
#define PCL_COMPRESSED_BITMAP_CLASS   2
#define PCL_SIMPLE_CHR     3
#define PCL_COMPOUND_CHR   4
#define PCL_BOUND_FONT		0	/* 08-06-01 jfd */
#define PCL_UNBOUND_FONT	1	/* 08-06-01 jfd */

#define PCL_SCALABLE_FONT   1   /* jwd, 07/21/03. */
#define PCL_BITMAP_FONT_0   2
#define PCL_BITMAP_FONT_20  3
#define PCL_XL_FONT         5

#define PCL_BITMAP_FONT     254

#define PCL_UNBOUND_FONT_SS  56
typedef struct {
   UB8 format;
   UB8 continuation;
   UB8 descriptorsize;
   UB8 ch_class;
} PCL_CHR_HDR;
typedef PCL_CHR_HDR * PPCL_CHR_HDR;
#define PCL_CHR_HDR_SIZE   4
typedef struct {
   UB8 format;
   UB8 continuation;
   UB8 descriptor_size;
   UB8 ch_class;
   UB8 orientation;
   UB8 reserved;
   SW16 left_offset;
   SW16 top_offset;
   UW16 character_width;
   UW16 character_height;
   SW16 delta_x;
} PCL_BITMAP_CHR_DESCRIPTOR;
typedef PCL_BITMAP_CHR_DESCRIPTOR * PPCL_BITMAP_CHR_DESCRIPTOR;
#define PCL_BITMAP_CHR_DESCRIPTOR_SIZE   16

#define MAX_PCL_BITMAP_RUNS_PER_LINE   20	/* NOTE: arbitrary number */
typedef struct {
	UW16 num_line_repeats;
	UW16 num_runs;
	UW16 run_starts[MAX_PCL_BITMAP_RUNS_PER_LINE];
	UW16 run_lengths[MAX_PCL_BITMAP_RUNS_PER_LINE];
} PCL_BITMAP_LINE_TRANS;

typedef struct {
	UL32 num_entries;
	PCL_BITMAP_LINE_TRANS *pcl_trans;
} PCL_BITMAP_TRANS;

#define EFM_TAG_SIZE   24
#define EFM_TAG_OFFSET 4
#define EFM_NAME_OFFSET (EFM_TAG_OFFSET + EFM_TAG_SIZE)
#define EFM_FONTNAME_SIZE 80
#define EFM_DL_FST_TYPE 4
#define EFM_HEADER_SIZE	108

typedef struct {
   UW16 descriptor;				/* size of header (EFM_HEADER_SIZE) */
   UB8 format;					/* PCL bitmap (0) */
   UB8 font_type;				/* unicode, unbound char codes (11) */
   UB8 efm_tag[EFM_TAG_SIZE];	/* "EXTERNAL FONT MODULE", null-terminated */
   UB8 fontname[EFM_FONTNAME_SIZE];				/* name of font */
} EFM_FONT_HDR;
typedef EFM_FONT_HDR * PEFM_FONT_HDR;
/*  format of compound character part  */
typedef struct {
   UW16             chId;            /*  ascii character code      */
   UW16             xoffset;
   UW16             yoffset;
} PCL_CC_COMPONENT;
typedef PCL_CC_COMPONENT * PPCL_CC_COMPONENT;

typedef struct {
   PCL_CHR_HDR       hdr;             /*  common PCLEO char header  */
   UW16             cc_esc;          /*  escapement                */
   UB8             nparts;          /*  # parts of components     */
   SB8              pad;             /*           added -ss 4/2/91 */
   PPCL_CC_COMPONENT cc_list;         /*  ptr to list of components */
} PCL_CC_DESC;
typedef PCL_CC_DESC * PPCL_CC_DESC;

#endif	/* __PCLEO__ */

#endif  /*  PCLEO_RDR  */

