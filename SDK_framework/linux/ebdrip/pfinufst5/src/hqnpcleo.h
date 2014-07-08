/* Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWpfin_ufst5!src:hqnpcleo.h(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup PFIN
 *  \brief Header file describing PCL soft font and glyph structures
 */

#ifndef _PCLEO_HQN_H_
#define _PCLEO_HQN_H_

#ifdef USE_UFST5

#include <stddef.h>

#define OPTIONAL 1
#define VARIABLE 1

/* ========================================================================== */
/* Enumeration of PCL font header offsets, in original endianness */
enum {
  sizeO = 0,            sizeP,         /* sizeO is MSB, sizeP is LSB, etc */
  formatO,              typeO,
  styleO,               reservedO,
  baselineO,            baselineP,
  cell_widthO,          cell_widthP,
  cell_heightO,         cell_heightP,
  orientO,              spacingO,
  symbolsetO,           symbolsetP,
  pitchO,               pitchP,
  heightO,              heightP,
  x_heightO,            x_heightP,
  width_typeO,          styleP,
  weightO,              typefaceP,
  typefaceO,            serifO,
  qualityO,             placementO,
  underline_positionO,  underline_thicknessO,
  text_heightO,         text_heightP,
  text_widthO,          text_widthP,
  first_codeO,          first_codeP,
  last_codeO,           last_codeP,
  pitchQ,               heightQ,        /* pitch and height extended */
  cap_heightO,          cap_heightP,
  numberO,              numberP,
  numberQ,              numberR,
  nameO,

  /* format 20 */
  xresO = 64,           xresP,
  yresO,                yresP,

  /* format 10/11 */
  scaleO = 64,          scaleP,
  xresIFO,              xresIFP,
  yresIFO,              yresIFP,
  master_positionO,     master_positionP,
  master_thicknessO,    master_thicknessP,
  thresholdO,           thresholdP,
  angleO,               angleP,
  complementO,

  /* format 15/16 */
  master_positionTTO = 66, master_positionTTP,
  master_thicknessTTO,  master_thicknessTTP,
  technologyO,          varietyO
} ;

/* PCLEO header shared by many formats */
typedef struct {
  uint16 size ;             /* at least 64 */
  uint8  format ;
  uint8  type ;             /* fonttype from symset map */
  uint8  styleMSB ;         /* top two bits of structure */
  uint8  reserved1 ;
  uint16 baseline ;         /* baseline position */
  uint16 cell_width ;
  uint16 cell_height ;
  uint8  orient ;
  uint8  spacing ;          /* actually a boolean */
  uint16 symbolset ;
  uint16 pitch ;            /* default HMI */
  uint16 height ;
  uint16 x_height ;
  int8   width_type ;
  uint8  styleLSB ;         /* posture, width and low 3 bits of structure */
  int8   weight ;
  uint8  typefaceLSB ;
  uint8  typefaceMSB ;
  uint8  serif ;
  uint8  quality ;
  int8   placement ;
  int8   underline_position ;
  uint8  underline_thickness ;
  uint16 text_height ;
  uint16 text_width ;
  uint16 first_code ;       /* or reserved2 */
  uint16 last_code ;        /* or number of contours/characters */
  uint8  pitch_ext ;
  uint8  height_ext ;
  uint16 cap_height ;
  uint32 number ;           /* but only bottom 3 bytes */
  uint8  name[16] ;
} font_header ;

/* Check the size of the font_header structure to spot packing problems */
extern char font_header_is_wrong_size[1-2*(sizeof(font_header) != 64)] ;

/* Font format 0 header - PCL Bitmapped */
typedef struct {
  font_header header ;
  uint16 copysize ;             /* of this, unknown byte and copyright */
  uint8  unknown ;              /* 1 */
  uint8  copyright[OPTIONAL] ;  /* null terminated */
  /* The following can't be used of course... */
  uint16 datasize ;             /* apparently, of this and remainder */
  uint8  data[OPTIONAL] ;
} font_header_0 ;

/* Font format 20 header - Resolution-specified Bitmapped */
typedef struct {
  font_header header ;
  uint16 xres ;
  uint16 yres ;
  uint8  copyright[OPTIONAL] ;
} font_header_20 ;

/* Font format 10 header - Intellifont bound scalable */
typedef struct {
  font_header header ;        /* size = 80 */
  uint16 scale ;
  uint16 xres ;
  uint16 yres ;
  int16  master_position ;
  uint16 master_thickness ;
  uint16 threshold ;
  int16  angle ;
  uint16 datasize ;         /* Note that this is positioned at offset
                               (header->size - 2), and so will be an offset
                               into the data[] array */
  uint16 data[VARIABLE] ;   /* followed by optional copyright then zero and
                               checksum byte */
} font_header_10 ;

#define FONT_HEADER_10_SIZE(_n) (offsetof(data,font_header_10) + (_n))

/* Font format 11 header - Intellifont unbound scalable */
typedef struct { /* header.type == 10 */
  font_header header ;
  uint16 scale ;
  uint16 xres ;
  uint16 yres ;
  int16  master_position ;
  uint16 master_thickness ;
  uint16 threshold ;
  int16  angle ;
  uint8  complement[8] ;
  uint16 datasize ;         /* Note as above */
  uint16 data[VARIABLE] ;   /* variable size, followed by optional copyright
                               then zero and checksum byte */
} font_header_11 ;

#define FONT_HEADER_11_SIZE(_n) (offsetof(data,font_header_11) + (_n))

/* Font format 15 header - TrueType scalable */
typedef struct {
  font_header header ;
  uint16 scale ;
  int16  master_position ;
  uint16 master_thickness ;
  uint8  technology ;       /* 0=Intellifont, 1=TrueType */
  uint8  variety ;
  uint16 data[VARIABLE] ;   /* variable size, followed by checksum byte */
} font_header_15 ;

/* ========================================================================== */
/* Glyph format 4 header */
typedef struct {
  uint8  format ;           /* 4 */
  uint8  continuation ;     /* 0 */
  uint8  size ;             /* 14 */
  uint8  Class ;            /* 1|2 - 'Class' because VC dislikes 'class' */
  uint8  orient ;
  uint8  reserved1 ;
  int16  left ;
  int16  top ;
  uint16 width ;
  uint16 height ;
  int16  deltaX ;
  uint8  data[VARIABLE] ;
} chr_header_bitmap ;

/* Minimum bitmap character header size in bytes */
#define CHR_HEADER_BITMAP_SIZE  (17)

typedef struct {
  uint8  format ;           /* 4 */
  uint8  continuation ;     /* !0 */
  uint8  data[VARIABLE] ;
} chr_header_bitmap_continuation ;

/* Glyph format 10 class 3 header */
typedef struct {
  uint8  format ;           /* 10 */
  uint8  continuation ;     /* 0 */
  uint8  size ;             /* >=2 */
  uint8  Class ;            /* 3 - 'Class' because VC dislikes 'class' */
  uint16 data[VARIABLE] ;
} chr_header_intellifont_3 ;

/* Minimum IF type 3 character header size in bytes */
#define CHR_HEADER_INTELLIFONT_3_SIZE (6)

typedef struct {
  uint16 code ;
  int16  xoffset ;
  int16  yoffset ;
} component_list ;

/* Glyph format 10 class 4 header */
typedef struct {
  uint8  format ;           /* 10 */
  uint8  continuation ;     /* 0 */
  uint8  size ;             /* >=6 */
  uint8  Class ;            /* 4 */
  int16  escapement ;
  uint16 components ;
  component_list component[VARIABLE] ;
} chr_header_intellifont_4 ;

/* Minimum IF type 4 character header size in bytes */
#define CHR_HEADER_INTELLIFONT_4_SIZE (14)

typedef struct {
  uint16 contoursize ;
  int16  metricoffset ;
  int16  dataoffset ;
  int16  contouroffset ;
  int16  xyoffset ;
} intellifont_data ;

typedef struct {
  uint8  format ;           /* 10 */
  uint8  continuation ;     /* !0 */
  uint16 data[VARIABLE] ;
} chr_header_intellifont_continuation ;

/* Glyph format 15 header */
typedef struct {
  uint8  format ;           /* 15 */
  uint8  continuation ;     /* 0 */
  uint8  size ;             /* >=2 */
  uint8  Class ;            /* 15 - 'Class' because VC dislikes 'class' */
  uint16 data[VARIABLE] ;
} chr_header_truetype ;

/* Minimum TrueType character header size in bytes */
#define CHR_HEADER_TRUETYPE_SIZE  (6)

/* Glyph format 0 - XL bitmap */
typedef struct {
  uint8  format ;           /* 0 */
  uint8  Class ;            /* 0 */
  int16  left ;
  int16  top ;
  uint16 width ;
  uint16 height ;
  uint8  data[VARIABLE] ;
} chr_header_xlb ;

#undef OPTIONAL
#undef VARIABLE

/* ========================================================================== */

typedef struct {
  uint16 size ;             /* 32 */
  uint8  format ;           /* from font header */
  uint8  type ;             /* from font header */
  char   efm[24] ;          /* EXTERNAL FONT MODULE */
  char   name[80] ;         /* Font name */
} font_header_efm ;

/* ========================================================================== */
#endif
#endif


