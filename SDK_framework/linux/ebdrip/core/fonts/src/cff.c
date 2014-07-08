/** \file
 * \ingroup cff
 *
 * $HopeName: COREfonts!src:cff.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to create PostScript wrapper for CFF and OpenType fonts, and
 * extract table and charstring data from CFFs. First come the routines to
 * read CFF tables, then the routines to build PostScript stubs, then the
 * routines to manage a CFF cache (similar to the CID map cache), and finally
 * the charstring and font access routines.
 */

#include "core.h"
#include "coreinit.h"
#include "objnamer.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "gcscan.h"
#include "tables.h"
#include "namedef_.h"

#include "fonts.h"
#include "fontcache.h"
#include "fontdata.h"

#include "graphics.h"
#include "often.h"
#include "stacks.h"
#include "psvm.h"
#include "dicthash.h"
#include "dictscan.h"
#include "miscops.h"
#include "control.h"

#include "t1hint.h"  /* t1hinting_now. Yuck. */
#include "tt_font.h"
#include "cidfont.h" /* CIDFONTTYPE */
#include "fcache.h"

#include "charstring12.h" /* This file ONLY implements Type 1/2 charstrings */
#ifdef ASSERT_BUILD
#include "encoding.h" /* for 'StandardEncoding' */
#endif
#include "cache_handler.h"


/* This file contains code to create a PostScript dictionary out of a CFF For
 * prior (assumed) reading, "The Compact Font Format, Technical Note 5176,
 * Version 1.0" is a must.
 *
 * This file contains two types of routines: (1) code to read CFF fonts
 * and (2) code to build PostScript structures from the CFF data
 */

/* CFF features not supported:
 * 0. Multiple fonts in the same file efficiently; done to a certain extent but
 *    some work required to make it efficient.
 * 1. Embedded PostScript. Don't have to support this.
 * 2. Multi Masters 'aint been tested and probably need some (harder) code changes
 *    to support following keys in Top Dict: { MultipleMaster, BlendDesignMap,
 *    & BlendAxisTypes}. In particular the T2 operator will need implementing.
 * 3. Chameleon fonts; will never be supported (since propietary to Adobe).
 */

#define CFF_SIZE_HEADER 12u

#define CFF_SIZE_INDEX   2u
#define CFF_SIZE_INDEX_OFFSIZE 1u

#define CFF_ENCODING     1u
#define CFF_ENCODING_0   1u
#define CFF_ENCODING_0_1 1u
#define CFF_ENCODING_1   1u
#define CFF_ENCODING_1_1 2u
#define CFF_ENCODING_S   1u
#define CFF_ENCODING_S_1 3u

#define CFF_CHARSETS     1u
#define CFF_CHARSETS_0   2u
#define CFF_CHARSETS_1   3u
#define CFF_CHARSETS_2   4u

#define CFF_FDSELECT     1u
#define CFF_FDSELECT_0   1u
#define CFF_FDSELECT_3   4u
#define CFF_FDSELECT_3_1 3u

typedef struct CFF_STRING {
  uint32 length ;
  uint32 frame ; /* fontdata frame offset */
} CFF_STRING ;

typedef struct CFF_INDEX {
  uint32 count, size ;
  CFF_STRING strings[ 1 ] ;
} CFF_INDEX ;

typedef struct CFF_OBJECT {
  int32 type ;
  char *name ;
  char *defalt ;
  int32 length ;
  uint8 found ;
  uint8 waste1 ;  /* use me first! */
  uint16 waste2 ; /* use me first! */
  union {
    int32 integer ;
    float real ;
    struct CFF_OBJECT *array ;
  } value ;
} CFF_OBJECT ;

enum { CFF_SID , CFF_BOOL , CFF_INT , CFF_REAL , CFF_NUM ,
       CFF_ARRAY , CFF_DELTA , CFF_IGNORE } ;

#define CFF_OP_SUBOP              12

#define CFF_OP_BLUEVALUES          6
#define CFF_OP_OTHERBLUES          7
#define CFF_OP_FAMILYBLUES         8
#define CFF_OP_FAMILYOTHERBLUES    9
#define CFF_OP_STDHW              10
#define CFF_OP_STDVW              11
#define CFF_OP_UNIQUEID           13
#define CFF_OP_XUID               14
#define CFF_OP_CHARSET            15
#define CFF_OP_ENCODING           16
#define CFF_OP_CHARSTRINGS        17
#define CFF_OP_PRIVATE            18
#define CFF_OP_SUBRS              19
#define CFF_OP_GSUBRS             19
#define CFF_OP_DEFAULTWIDTHX      20
#define CFF_OP_NOMINALWIDTHX      21

#define CFF_SUBOP_CHARTYPE         6
#define CFF_SUBOP_FONTMATRIX       7

#define CFF_SUBOP_BLUESCALE        9
#define CFF_SUBOP_BLUESHIFT       10
#define CFF_SUBOP_BLUEFUZZ        11
#define CFF_SUBOP_STEMSNAPH       12
#define CFF_SUBOP_STEMSNAPV       13
#define CFF_SUBOP_FORCEBOLD       14
#define CFF_SUBOP_FORCEBOLDTHRESHOLD 15
#define CFF_SUBOP_LENIV           16
#define CFF_SUBOP_LANGUAGEGROUP   17
#define CFF_SUBOP_EXPANSIONFACTOR 18
#define CFF_SUBOP_INITIALRANDOMSEED 19
#define CFF_SUBOP_SYNTHETICBASE   20
#define CFF_SUBOP_POSTSCRIPT      21

#define CFF_SUBOP_BASEFONTNAME    22
#define CFF_SUBOP_BASEFONTBLEND   23

#define CFF_SUBOP_MULTIPLEMASTER  24
#define CFF_SUBOP_BLENDDESIGNMAP  25
#define CFF_SUBOP_BLENDAXISTYPES  26

#define CFF_SUBOP_NDV             27
#define CFF_SUBOP_CDV             28
#define CFF_SUBOP_LENBUILDCHARARRAY 29

#define CFF_SUBOP_ROS             30

#define CFF_SUBOP_CIDFONTVERSION  31
#define CFF_SUBOP_CIDFONTREVISION 32
#define CFF_SUBOP_CIDFONTTYPE     33
#define CFF_SUBOP_CIDCOUNT        34

#define CFF_SUBOP_UIDBASE         35

#define CFF_SUBOP_FDARRAY         36
#define CFF_SUBOP_FDSELECT        37

#define CFF_SUBOP_FONTNAME        38
#define CFF_SUBOP_CHAMELEON       39

#define CFF_FONT_MAX               7
#define CFF_FONTINFO_MAX          11
#define CFF_PRIVATE_MAX           11

#define CFF_OPFDEND              (-1)

#define CFF_MAXOPS                22
#define CFF_EXTRAOPS               2

typedef struct CFF_OPFD {
  int32 opnum ;
  int32 opsubnum ;
} CFF_OPFD ;

/* Arrange the following in whatever order you like... */
static CFF_OPFD cff_font_ops[] = {
  /* UniqueID, FontBBox, XUID, PaintType, FontType, FontMatrix, StrokeWidth */
  { 13 } , { 5 } , { 14 } ,
  { 12 , 5 } , { 12 , 6 } , { 12 , 7 } , { 12 , 8 } ,
  { CFF_OPFDEND }
} ;

static CFF_OPFD cff_fontinfo_ops[] = {
  /* version, Notice, FullName, FamilyName, Weight,
     Copyright, isFixedPitch, ItalicAngle, UnderlinePosition,
     UnderlineThickness, BaseFontName */
  { 0 } , { 1 } , { 2 } , { 3 } , { 4 } ,
  { 12 , 0 } , { 12 , 1 } , { 12 , 2 } , { 12 , 3 } , { 12 , 4 } , { 12 , 22 } ,
  { CFF_OPFDEND }
} ;

static CFF_OPFD cff_private_ops[] = {
  /* BlueValues, OtherBlues, FamilyBlues, FamilyOtherBlues, StdHW, StdVW,
     defaultWidthX, nominalWidthX, BlueScale, BlueShift, BlueFuzz,
     StemSnapH, StemSnapV, ForceBold, ForceBoldThreshold, lenIV, LanguageGroup,
     ExpansionFactor, initialRandomSeed, NDV, CDV, lenBuildCharArray */
  { 6 } , { 7 } , { 8 } , { 9 } , { 10 } , { 11 } , { 20 } , { 21 } ,
  { 12 ,  9 } , { 12 , 10 } , { 12 , 11 } , { 12 , 12 } , { 12 , 13 } , { 12 , 14 } ,
  { 12 , 15 } , { 12 , 16 } , { 12 , 17 } , { 12 , 18 } , { 12 , 19 } ,
  { 12 , 27 } , { 12 , 28 } , { 12 , 29 } ,
  { CFF_OPFDEND }
} ;

static CFF_OPFD cff_cid_ops1[] = {
  /* CIDFontVersion, CIDFontType, CIDCount */
  { 12 , 31 } , { 12 , 33 } , { 12 , 34 } ,
  { CFF_OPFDEND }
} ;

static char *cid_ros[] = {
 "Registry" , "Ordering" , "Supplement"
} ;


/* Essentially: Table 9 Top DICT Operator Entries
 * and:         Table 25 Private DICT Operators
 * NOTE we don't currently distinguish between the two.
 */
static CFF_OBJECT cff_ops[ CFF_MAXOPS + CFF_EXTRAOPS ] = {
  { CFF_SID ,   "version" ,             NULL ,    0 } , /*  0 */
  { CFF_SID ,   "Notice" ,              NULL ,    0 } , /*  1 */
  { CFF_SID ,   "FullName" ,            NULL ,    0 } , /*  2 */
  { CFF_SID ,   "FamilyName" ,          NULL ,    0 } , /*  3 */
  { CFF_SID ,   "Weight" ,              NULL ,    0 } , /*  4 */
  { CFF_ARRAY , "FontBBox" ,     "[0 0 0 0]" ,    4 } , /*  5 */
  { CFF_DELTA , "BlueValues" ,          NULL ,    0 } , /*  6 */
  { CFF_DELTA , "OtherBlues" ,          NULL ,    0 } , /*  7 */

  { CFF_DELTA , "FamilyBlues" ,         NULL ,    0 } , /*  8 */
  { CFF_DELTA , "FamilyOtherBlues" ,    NULL ,    0 } , /*  9 */
  { CFF_ARRAY , "StdHW" ,               NULL ,    1 } , /* 10 */
  { CFF_ARRAY , "StdVW" ,               NULL ,    1 } , /* 11 */
  { CFF_SID ,    NULL ,                 NULL ,    0 } , /* 12 */
  { CFF_NUM ,   "UniqueID" ,            NULL ,    0 } , /* 13 */
  { CFF_ARRAY , "XUID" ,                NULL ,    0 } , /* 14 */
  { CFF_NUM ,   "charset" ,             NULL ,    0 } , /* 15 */

  { CFF_INT ,   "Encoding" ,            NULL ,    0 } , /* 16 */
  { CFF_NUM ,   "CharStrings" ,         NULL ,    0 } , /* 17 */
  { CFF_ARRAY , "Private" ,             NULL ,    2 } , /* 18 */
  { CFF_NUM ,   "Subrs" ,               NULL ,    0 } , /* 19 */
  { CFF_NUM ,   "defaultWidthX" ,       "0" ,     0 } , /* 20 */
  { CFF_NUM ,   "nominalWidthX" ,       "0" ,     0 } , /* 21 */
} ;

#define CFF_DEFAULT_FONTMATRIX "[0.001 0 0 0.001 0 0]"
#define CFF_IDENTITY_FONTMATRIX "[1 0 0 1 0 0]"

#define CFF_MAXSUBOPS 40

static CFF_OBJECT cff_subops[ CFF_MAXSUBOPS ] = {
  { CFF_SID ,   "Copyright" ,           NULL ,    0 } , /*  0 */
  { CFF_BOOL ,  "isFixedPitch" ,        "false" , 0 } , /*  1 */
  { CFF_NUM ,   "ItalicAngle" ,         "0" ,     0 } , /*  2 */
  { CFF_NUM ,   "UnderlinePosition" ,   "-100" ,  0 } , /*  3 */
  { CFF_NUM ,   "UnderlineThickness" ,  "50" ,    0 } , /*  4 */
  { CFF_NUM ,   "PaintType" ,           "0" ,     0 } , /*  5 */
  { CFF_INT ,   "CharStringType" ,      "2" ,     0 } , /*  6 */
  { CFF_ARRAY , "FontMatrix" ,          NULL ,    6 } , /*  7 */

  { CFF_NUM ,   "StrokeWidth" ,         "0" ,           0 } , /*  8 */
  { CFF_NUM ,   "BlueScale" ,           "0.039625" ,    0 } , /*  9 */
  { CFF_NUM ,   "BlueShift" ,           "7" ,           0 } , /* 10 */
  { CFF_NUM ,   "BlueFuzz" ,            "1" ,           0 } , /* 11 */
  { CFF_DELTA , "StemSnapH" ,           NULL ,          0 } , /* 12 */
  { CFF_DELTA , "StemSnapV" ,           NULL ,          0 } , /* 13 */
  { CFF_BOOL ,  "ForceBold" ,           "false" ,       0 } , /* 14 */
  { CFF_NUM ,   "ForceBoldThreshold" ,  "0" ,           0 } , /* 15 */

  { CFF_NUM ,   "lenIV" ,               "-1" ,          0 } , /* 16 */
  { CFF_NUM ,   "LanguageGroup" ,       NULL ,          0 } , /* 17 */
  { CFF_NUM ,   "ExpansionFactor" ,     NULL ,          0 } , /* 18 */
  { CFF_NUM ,   "initialRandomSeed" ,   "0" ,           0 } , /* 19 */
  { CFF_NUM ,   "SyntheticBase" ,       NULL ,          0 } , /* 20 */
  { CFF_SID ,   "PostScript" ,          NULL ,          0 } , /* 21 */
  { CFF_SID ,   "BaseFontName" ,        NULL ,          0 } , /* 22 */
  { CFF_DELTA , "BaseFontBlend" ,       NULL ,          0 } , /* 23 */

  { CFF_ARRAY , "MultipleMaster" ,      NULL ,          0 } , /* 24 */
  { CFF_ARRAY , "BlendDesignMap" ,      NULL ,          0 } , /* 25 */
  { CFF_ARRAY , "BlendAxisTypes" ,      NULL ,          0 } , /* 26 */
  { CFF_NUM ,   "NDV" ,                 "65535" ,       0 } , /* 27 */
  { CFF_NUM ,   "CDV" ,                 "65535" ,       0 } , /* 28 */
  { CFF_NUM ,   "lenBuildCharArray" ,   "32" ,          0 } , /* 29 */
  { CFF_ARRAY , "ROS" ,                 NULL ,          3 } , /* 30 */
  { CFF_NUM ,   "CIDFontVersion" ,      "0" ,           0 } , /* 31 */

  { CFF_NUM ,   "CIDFontRevision" ,     "0" ,           0 } , /* 32 */
  { CFF_NUM ,   "CIDFontType" ,         "0" ,           0 } , /* 33 */
  { CFF_NUM ,   "CIDCount" ,            "8720" ,        0 } , /* 34 */
  { CFF_NUM ,   "UIDBase" ,             NULL ,          0 } , /* 35 */
  { CFF_NUM ,   "FDArray" ,             NULL ,          0 } , /* 36 */
  { CFF_NUM ,   "FDSelect" ,            NULL ,          0 } , /* 37 */
  { CFF_SID ,   "FontName" ,            NULL ,          0 } , /* 38 */
  { CFF_NUM ,   "Chameleon" ,           NULL ,          0 } , /* 39 */
} ;

enum { CFF_EMBEDDED = 3 , CFF_ISOADOBE = 0 , CFF_EXPERT = 1 , CFF_EXPERTSUBSET = 2 } ;

enum { CFF_NO_FONT, CFF_FONT , CFF_MMFONT , CFF_CIDFONT , CFF_CHAMELEON } ;

/* Invalid GID for SID/CID->GID mapping table. This is a safe value, since the
   charstrings Index length is represented using two bytes, the maximum length
   of the index is 0xffffu. */
enum { CFF_INVALID_GID = 0xffffu } ;

typedef struct CFF_DATA {
  CFF_INDEX *nameIndex ;
  CFF_INDEX *topDictIndex ;
  CFF_INDEX *fontDictIndex ;
  CFF_INDEX *stringIndex ;
  CFF_INDEX *globalSubrIndex ;

  CFF_INDEX *charStringsIndex ;
  CFF_INDEX *localSubrIndex ;

  /* Encoding vector. */
  uint32 glyphSIDArray[ 256 ] ;

  CFF_OBJECT cff_ops[ CFF_MAXOPS ] ;
  CFF_OBJECT cff_subops[ CFF_MAXSUBOPS ] ;

  /* Charset vector. */
  int32 charset ;
  uint32 charsetMaxCID ; /* The highest SID/CID used */
  uint16 *charsetNames ; /* Mapping of GID to SID/CID */
  uint32 charsetLength ;

  uint16 *cidMapping ;   /* Mapping of SID/CID to GID (charsetNames index) */
  uint32 cidMappingLength ;

  uint8 *fdSelect ;      /* Mapping of CID to FDIndex */
  uint32 fdSelectLength ;
  int32 fdbytes, gdbytes ;

  int32 fonttype ;
  int32 fontindex ;  /* Current font in CFF loaded, initialised to -1 */
  int32 fdindex ;    /* Current FDArray loaded, initialised to -1 */

  uint8 offSize ; /* size for absolute offsets */
  uint8 gotFontMatrix ; /* flag indicates if top-level FontMatrix is present */
  uint8 dchartype, chartype ; /* Default chartype from top-level, and actual */

  OMATRIX fdmatrix ; /* font matrix for sub-font loaded */
} CFF_DATA ;

/* CFF structure cache type. This is used to prevent the CFF code from having
   to reload the CFF font data for every charstring. The CFF cache routines
   use the common font data cache to store charstrings routines. The font
   data pointer is not retained between characters; a new instance is opened
   for each character. */
typedef struct CFF_CACHE {
  CFF_DATA cff_data ;
  charstring_methods_t cffmethods ; /* Local copy of cff_charstring_fns */
  fontdata_t *font_data ;
  const blobdata_methods_t *fdmethods ;
  int32 fid ;      /* Font identifier */
  OBJECT *source ; /* data source for this font */
  uint32 offset ;  /* offset into data source */
  struct CFF_CACHE *next ;
  OBJECT_NAME_MEMBER
} CFF_CACHE ;

#define CFF_CACHE_NAME "CFF cache"

/* Table 2 CFF Data Types
 * Name         Range           Description
 * Card8        0 - 255         1-byte unsigned number
 * Card16       0 - 65535       2-byte unsigned number
 * Offset       varies          1, 2, 3, or 4 byte offset
 *                              (specified by OffSize field)
 * OffSize      1-4             1-byte unsigned number specifies the size
 *                              of an Offset field or fields
 * SID          0 - 65535       2-byte string identifier
 */

#define CFF_Card8( _mem , _offset , _val ) MACRO_START  \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = ( uint8 )( _lmem_[ 0 ] ) ;                   \
MACRO_END

#define CFF_Card16( _mem , _offset , _val ) MACRO_START \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = ( uint16 )(( _lmem_[ 0 ] << 8 ) |            \
                      ( _lmem_[ 1 ] << 0 )) ;           \
MACRO_END

#define CFF_Offset( _mem , _offset , _size , _val ) MACRO_START \
  uint8 _size_ ;                                                \
  uint32 _val_ ;                                                \
  uint8 *_lmem_ ;                                               \
  _lmem_ = (_mem) + (_offset) ;                                 \
  _val_ = 0u ;                                                  \
  for ( _size_ = (_size) ; _size_ > 0 ; --_size_ )              \
    _val_ = ( _val_ << 8 ) | (*_lmem_++) ;                      \
  (_val) = _val_ ;                                              \
MACRO_END

#define CFF_OffSize( _mem , _offset , _val ) MACRO_START \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = ( uint8 )( _lmem_[ 0 ] & (uint8) 0x07 ) ;                    \
MACRO_END

#define CFF_SID( _mem , _offset , _val ) CFF_Card16( _mem , _offset , _val )

#define FG_BYTES(_v) ((uint32)(_v) > 0xffffff ? 4 : \
                      (uint32)(_v) > 0xffff ? 3 : \
                      (uint32)(_v) > 0xff ? 2 : \
                      (uint32)(_v) != 0)

/* -------------------------------------------------------------------------- */
/* Exported definition of the font methods for CFF non-CID fonts */
static Bool cff_lookup_char(FONTinfo *fontInfo,
                            charcontext_t *context) ;
static Bool cff_begin_char(FONTinfo *fontInfo,
                            charcontext_t *context) ;
static void cff_end_char(FONTinfo *fontInfo,
                         charcontext_t *context) ;

font_methods_t font_cff_fns = {
  fontcache_base_key,
  cff_lookup_char,
  NULL, /* No subfont lookup */
  cff_begin_char,
  cff_end_char
} ;

/* Exported definition of the font methods for CFF CID Type 0 fonts */
static Bool cff_cid0_lookup_char(FONTinfo *fontInfo,
                                 charcontext_t *context) ;
static Bool cff_cid0_select_subfont(FONTinfo *fontInfo,
                                    charcontext_t *context) ;

font_methods_t font_cid0c_fns = {
  fontcache_cid_key,
  cff_cid0_lookup_char,
  cff_cid0_select_subfont,
  cff_begin_char,
  cff_end_char
} ;

/* Private definition of Type 1/2 charstring methods for CFF fonts. The static
   definition below is only ever used as a prototype, its data pointer is
   never altered. Copies are stored in the CFF font cache structure, and
   accessed through there. */
static Bool cff_get_info(void *data, int32 nameid, int32 index, OBJECT *info) ;
static Bool cff_begin_subr(void *data, int32 subno, int32 global,
                           uint8 **subrstr, uint32 *subrlen) ;
static Bool cff_begin_seac(void *data, int32 stdindex,
                           uint8 **subrstr, uint32 *subrlen) ;
static void cff_end_substring(void *data, uint8 **subrstr, uint32 *subrlen) ;

static const charstring_methods_t cff_charstring_fns = {
  NULL,          /* private data (CFF_CACHE *) */
  cff_get_info,
  cff_begin_subr,
  cff_end_substring,
  cff_begin_seac,
  cff_end_substring
} ;

/*---------------------------------------------------------------------------*/
static Bool cff_checkheader(CFF_CACHE *cff_cache, uint32 *phdrSize,
                             uint8 *poffSize);

static Bool cff_readIndex(CFF_CACHE *cff_cache, uint32 *frame, CFF_INDEX **pindex) ;
static void cff_freeIndex( CFF_INDEX **index ) ;

static Bool cff_readCharStringsIndex(CFF_CACHE *cff_cache, uint32 frame) ;
static Bool cff_readLocalSubrIndex(CFF_CACHE *cff_cache, uint32 frame) ;
static Bool cff_readfontDictIndex(CFF_CACHE *cff_cache, uint32 frame) ;

static Bool cff_decodeDict(uint8 *smem, uint32 slen, CFF_DATA *CFF_data) ;

static Bool cff_readEncoding(CFF_CACHE *cff_cache, uint32 frame) ;
static Bool cff_readCharsets(CFF_CACHE *cff_cache, uint32 frame) ;
static Bool cff_fakeCharset(CFF_DATA *CFF_data) ;
static Bool cff_mapCIDtoGID(CFF_DATA *CFF_data) ;
static Bool cff_readFDSelect(CFF_CACHE *cff_cache, uint32 frame) ;
static uint32 cff_parseName( uint8 *pRet, uint8 *pStr, uint32 len );
static Bool cff_encoding(CFF_CACHE *cff_cache, OBJECT *encoding) ;
static Bool cff_initSubFont(CFF_CACHE *cff_cache, int32 fontindex) ;
static Bool cff_opfd_dict(CFF_CACHE *cff_cache,
                           CFF_OPFD *cff_opfd, OBJECT *dict) ;
static void cff_opfd_release(CFF_DATA *CFF_data, CFF_OPFD *cff_opfd) ;

#if defined( ASSERT_BUILD )
static Bool debug_cff = FALSE ;
#endif

static mps_root_t cff_gc_root ;

/* -------------------------------------------------------------------------- */
/* CFF wrapper for fontdata open; adds file offset onto frame offset before
   asking for font data.  */
static uint8 *cff_frame(CFF_CACHE *cff_cache, uint32 offset, uint32 length)
{
  HQASSERT(cff_cache, "No CFF source entry") ;

  return fontdata_frame(cff_cache->font_data,
                        cff_cache->offset + offset, length, sizeof(uint8)) ;
}

/* -------------------------------------------------------------------------- */
static void cff_releaseop(CFF_OBJECT *cff_op, CFF_OBJECT *cff_original)
{
  HQASSERT(cff_op->name == cff_original->name,
           "CFF op and original do not match when released") ;
  if ( cff_op->found ) {
    if ( (cff_op->type == CFF_ARRAY || cff_op->type == CFF_DELTA) &&
         cff_op->length != 0 )
      mm_free(mm_pool_temp, (mm_addr_t)cff_op->value.array,
              cff_op->length * sizeof(CFF_OBJECT)) ;
    *cff_op = *cff_original ;
  }
}

/* -------------------------------------------------------------------------- */
static Bool cff_checkheader(CFF_CACHE *cff_cache, uint32 *phdrSize, uint8 *poffSize)
{
  /* First of all check it's a T1c font.
   * this is done by reading the header, which contains:
   *
   * Type       Name    Description
   * Card8      major   Format major version (starting at 1)
   * Card8      minor   Format minor version (starting at 0)
   * Card8      hdrSize Header size (bytes)
   * OffSize    offSize Absolute offset (0) size
   */
  uint8 *cffmem ;
  uint8 major , minor , hdrSize , offSize ;

  HQASSERT( cff_cache, "cff_cache NULL in cff_checkheader" ) ;
  HQASSERT( phdrSize , "phdrSize NULL in cff_checkheader" ) ;
  HQASSERT( poffSize , "poffSize NULL in cff_checkheader" ) ;

  if ( (cffmem = cff_frame(cff_cache, 0, CFF_SIZE_HEADER)) == NULL )
    return FALSE ;

  CFF_Card8( cffmem , 0 , major ) ;
  CFF_Card8( cffmem , 1 , minor ) ;
  CFF_Card8( cffmem , 2 , hdrSize ) ;
  CFF_OffSize( cffmem , 3 , offSize ) ;

  if ( major != 0x01 ) {
    HQTRACE(debug_cff,("invalid major version: %08x",major));
    return FALSE ;
  }

  /* Should NOT abort on this, though a warning may be in order. See section 6
     of TN 5176, Compact Font Format. */
#if 0
  if ( minor != 0x00 ) {
    HQTRACE(debug_cff,("unrecognised minor version: %08x",minor));
    return FALSE ;
  }
#endif

  (*phdrSize) = hdrSize ;
  (*poffSize) = offSize ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool cff_readIndex(CFF_CACHE *cff_cache, uint32 *frame, CFF_INDEX **pIndex)
{
  /* This code reads in a single Index table of the following format:
   * Type       Name    Description
   * Card16     count   Number of objects stored in INDEX
   * OffSize    offSize Offset array element size
   * Offset     offset  Offset array (from byte preceding
   *            [count+1]       object data)
   * Card8      data    Object data
   *            [<varies>]
   */

  uint8  offSize ;
  uint16 count , acount ;

  uint8 *cffmem ;

  int32 i ;
  uint32 offset0 , offset1 ;
  uint32 offindex ;
  CFF_INDEX *index ;

  HQASSERT( pIndex  , "pIndex  NULL in cff_readIndex" ) ;
  HQASSERT( cff_cache  , "cff_cache  NULL in cff_readIndex" ) ;
  HQASSERT( frame  , "frame  NULL in cff_readIndex" ) ;

  /* TN5176 (16 Mar 00), p. 10: "An empty index is represented by a count
     field with a 0 value and no additional fields. Thus, the total size of
     an empty INDEX is 2 bytes." */
  if ( (cffmem = cff_frame(cff_cache, *frame, CFF_SIZE_INDEX)) == NULL )
    return FALSE ;

  CFF_Card16( cffmem , 0 , count ) ;

  *frame += 2 ;

  acount = ( uint16 )( count == 0 ? 0 : count - 1 ) ;
  index = mm_alloc( mm_pool_temp ,
                    sizeof( CFF_INDEX ) + acount * sizeof( CFF_STRING ) ,
                    MM_ALLOC_CLASS_CFF_INDEX ) ;
  if ( ! index )
    return error_handler( VMERROR ) ;

  HqMemZero(index, sizeof(CFF_INDEX) + acount * sizeof(CFF_STRING));

  index->count = count ;
  index->size = 0 ;

  if ( count == 0 ) {
    *pIndex = index ;
    return TRUE ;
  }

  /* Get the offset size, so we can determine how many bytes the offset table
     takes. */
  if ( (cffmem = cff_frame(cff_cache, *frame,
                           CFF_SIZE_INDEX_OFFSIZE)) == NULL ) {
    mm_free(mm_pool_temp, (mm_addr_t)index,
            sizeof(CFF_INDEX) + acount * sizeof(CFF_STRING)) ;
    return FALSE ;
  }

  CFF_OffSize( cffmem , 0 , offSize ) ;

  *frame += 1 ;

  if ( (cffmem = cff_frame(cff_cache, *frame,
                           (count + 1u) * offSize)) == NULL ) {
    mm_free(mm_pool_temp, (mm_addr_t)index,
            sizeof(CFF_INDEX) + acount * sizeof(CFF_STRING)) ;
    return FALSE ;
  }

  /* Now read (count+1) Offsets of size offSize: */
  CFF_Offset( cffmem , 0 , offSize , offset0 ) ;
  HQTRACE(debug_cff && offset0 != 1,
          ("CFF first index offset incorrect: %d", offset0));
  for ( offindex = offSize, i = 0 ; i < count ; ++i ) {
    CFF_Offset( cffmem , offindex , offSize , offset1 ) ;

    index->strings[ i ].length = offset1 - offset0 ;
    index->size += index->strings[i].length ;

    offset0 = offset1 ;
    offindex += offSize ;
  }

  /* Set the index data offset. Don't bother checking it for size; if a
     consumer tries to open a frame at an invalid index, it will be caught
     then. */
  offset0 = *frame + offindex ;

  /* Set up the frame offsets. Strings can then be accessed using the
     fontdata open_frame() and close_frame() methods. */
  for ( i = 0 ; i < count ; ++i ) {
    index->strings[ i ].frame = offset0 ;
    offset0 += index->strings[ i ].length ;
  }

  *frame = offset0 ;
  (*pIndex) = index ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static void cff_freeIndex( CFF_INDEX **indexp )
{
  int32 acount ;
  int32 size ;
  CFF_INDEX *index ;

  HQASSERT( indexp , "index NULL in cff_freeIndex" ) ;
  if ( (index = *indexp) != NULL ) {
    acount = ( index->count == 0 ? 0 : index->count-1 ) ;
    size = sizeof( CFF_INDEX ) + acount * sizeof( CFF_STRING ) ;
    mm_free( mm_pool_temp , ( mm_addr_t )index , size ) ;
    *indexp = NULL ;
  }
}

/* -------------------------------------------------------------------------- */
/* Substitute encoded # bytes for the direct ascii value (e.g. '#2B' for '+').
   The output string buffer (parameter '*pRet') should be at least as long
   as the input string (parameter *pStr).  Parameter 'len' must state the
   length of the input string to be read (no null-termination here!). Hex
   escapes are recursively removed (i.e. #2341 -> #41 -> 'A') */

static uint32 cff_parseName( uint8 *pRet, uint8 *pStr, uint32 len )
{
  uint32 retLen = 0;

  HQASSERT(pRet, "Nowhere to put parsed name") ;
  HQASSERT(pStr, "No name to parse") ;

  while ( len > 0 ) {
    uint8 ch = *pStr++ ;
    int8 ch0, ch1 ;

    --len ;
    while ( ch == '#' && len >= 2 &&
            (ch0 = char_to_hex_nibble[pStr[0]]) >= 0 &&
            (ch1 = char_to_hex_nibble[pStr[1]]) >= 0 ) {
      ch = CAST_TO_UINT8((ch0 << 4) | ch1) ;
      pStr += 2 ;
      len -= 2 ;
    }

    pRet[retLen++] = ch ;
  }

  return retLen;
}

/* -------------------------------------------------------------------------- */
static Bool cff_readCharStringsIndex(CFF_CACHE *cff_cache, uint32 frame)
{
  /* Read in all the CharStrings in the Type1C font.
   */
  CFF_INDEX *charStringsIndex ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT( CFF_data, "CFF_data NULL in cff_readCharStringsIndex" ) ;

  if ( ! cff_readIndex(cff_cache, &frame, &CFF_data->charStringsIndex) )
    return FALSE ;

  charStringsIndex = CFF_data->charStringsIndex ;

  /* Calculate representation size for largest string offset */
  CFF_data->gdbytes = FG_BYTES(charStringsIndex->size) ;
  HQASSERT(CFF_data->gdbytes >= 1 && CFF_data->gdbytes <= 4,
           "GDBytes must be between 1 and 4") ;

  if ( charStringsIndex->count == 0 ) {
    HQTRACE(debug_cff,("no CharStrings exist - can't handle (yet)"));
    return error_handler( INVALIDFONT ) ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool cff_readLocalSubrIndex(CFF_CACHE *cff_cache, uint32 frame)
{
  /* Read in all the local Subrs in the Type1C font.
   */
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT( CFF_data , "CFF_data NULL in cff_readLocalSubrIndex" ) ;

  cff_freeIndex(&CFF_data->localSubrIndex) ;

  if ( ! cff_readIndex(cff_cache, &frame, &CFF_data->localSubrIndex) )
    return FALSE ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool cff_readfontDictIndex(CFF_CACHE *cff_cache, uint32 frame)
{
  /* Read in and all the font dict entries in the FDArray.
   */
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT( CFF_data , "CFF_data NULL in cff_readfontDictIndex" ) ;

  if ( ! cff_readIndex(cff_cache, &frame, &CFF_data->fontDictIndex) )
    return FALSE ;

  CFF_data->fdbytes = FG_BYTES(CFF_data->fontDictIndex->count - 1) ;
  HQASSERT(CFF_data->fdbytes == 0 || CFF_data->fdbytes == 1,
           "FDBytes must be 0 or 1") ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Table 5 Nibble Definitions
 * Nibble       Represents
 * 0 - 9        0 - 9
 * a            . (decimal point)
 * b            E
 * c            E-
 * d            <reserved>
 * e            - (minus)
 * f            end of number
 */

#define SR_GETC_T( _byte , _smem , _slen ) MACRO_START  \
  uint8 _tmp_ ;                                         \
  if ( (_slen) == 0 )                                   \
    return FALSE ;                                      \
  _tmp_ = (*(_smem)) ;                                  \
  (_byte) = ( _tmp_ >> 4 ) & 0xf ;                      \
  firstnibble = FALSE ;                                 \
MACRO_END

#define SR_GETC_F( _byte , _smem , _slen ) MACRO_START  \
  uint8 _tmp_ ;                                         \
  _tmp_ = (*(_smem)++) ;                                \
  --(_slen) ;                                           \
  (_byte) = ( _tmp_ >> 0 ) & 0xf ;                      \
  firstnibble = TRUE ;                                  \
MACRO_END

#define SR_GETC( _byte , _smem , _slen ) MACRO_START    \
  if ( firstnibble )                                    \
    SR_GETC_T( _byte , _smem , _slen ) ;                \
  else                                                  \
    SR_GETC_F( _byte , _smem , _slen ) ;                \
MACRO_END

#define SR_0      0x00
#define SR_9      0x09
#define SR_PERIOD 0x0A
#define SR_EXP_P  0x0B
#define SR_EXP_M  0x0C
#define SR_XXXXX  0x0D
#define SR_MINUS  0x0E
#define SR_EOF    0x0F

static Bool cff_scanreal(uint8 **psmem, uint32 *pslen, float *number)
{
  uint8 *smem = (*psmem) ;
  uint32 slen = (*pslen) ;

  Bool firstnibble = TRUE ;

  int32 ch ;

  int32 sign ;
  int32 signexp ;
  int32 ntotal ;
  int32 nleading ;
  int32 nleadingexp ;
  int32 ileading ;
  int32 ileadingexp ;
  double fleading ;
  double fleadingexp ;
  double fnumber ;
  double fexponent ;

  HQASSERT( psmem  , "psmem  NULL in cff_scanreal" ) ;
  HQASSERT( pslen  , "pslen  NULL in cff_scanreal" ) ;
  HQASSERT( number , "number NULL in cff_scanreal" ) ;

  sign = 1 ;
  SR_GETC_T( ch , smem , slen ) ;
  if ( ch == SR_MINUS ) {
    sign = -1 ;
    SR_GETC_F( ch , smem , slen ) ;
  }

  nleading = 0 ;
  ileading = 0 ;
  fleading = 0.0 ;
  /* Scan the m part of a (m.nEx) number. */
  while ( ch >= SR_0 && ch <= SR_9 ) {
    ch = ( ch - SR_0 ) ;
    ++nleading ;
    if ( nleading < 10 )
      ileading = 10 * ileading + ch ;
    else if ( nleading > 10 )
      fleading = 10.0 * fleading + ch ;
    else
      fleading = 10.0 * ( double )ileading + ch ;
    SR_GETC( ch , smem , slen ) ;
  }

  ntotal = nleading ;
  /* Scan the . part of a (m.nEx) number. */
  if ( ch == SR_PERIOD ) {
    /* Scan the n part of a (m.nEx) number. */
    SR_GETC( ch , smem , slen ) ;
    while ( ch >= SR_0 && ch <= SR_9 ) {
      ch = ( ch - SR_0 ) ;
      ++ntotal ;
      if ( ntotal < 10 )
        ileading = 10 * ileading + ch ;
      else if ( ntotal > 10 )
        fleading = 10.0 * fleading + ch ;
      else
        fleading = 10.0 * ( double )ileading + ch ;
      SR_GETC( ch , smem , slen ) ;
    }
  }

  signexp = 1 ;
  nleadingexp = 0 ;
  ileadingexp = 0 ;
  fleadingexp = 0.0 ;
  /* Scan the E part of a (m.nEx) number. */
  if ( ch == SR_EXP_P ||
       ch == SR_EXP_M ) {
    if ( ch == SR_EXP_M )
      signexp = -1 ;
    /* Scan the x part of a (m.nEx) number. */
    SR_GETC( ch , smem , slen ) ;
    if ( ! ( ch >= SR_0 && ch <= SR_9 ))
      return error_handler( SYNTAXERROR ) ;
    while ( ch >= SR_0 && ch <= SR_9 ) {
      ch = ( ch - SR_0 ) ;
      ++nleadingexp ;
      if ( nleadingexp < 10 )
        ileadingexp = 10 * ileadingexp + ch ;
      else if ( nleading > 10 )
        fleadingexp = 10.0 * fleadingexp + ch ;
      else
        fleadingexp = 10.0 * ( double )ileadingexp + ch ;
      SR_GETC( ch , smem , slen ) ;
    }
  }

  if ( ch != SR_EOF )
    return error_handler( SYNTAXERROR ) ;

  if ( ! firstnibble ) {
    SR_GETC_F( ch , smem , slen ) ;
    if ( ch != SR_EOF )
      return error_handler( SYNTAXERROR ) ;
  }

  if ( ntotal == nleading && nleading > 0 ) {
    if ( nleading < 10 )
      fnumber = ( sign < 0 ? -ileading : ileading ) ;
    else
      fnumber = ( sign < 0 ? -fleading : fleading ) ;
  }
  else {
    int32 ntrailing = ntotal - nleading ;
    double fdivs[ 11 ] = { 0.0 , 0.1 , 0.01 , 0.001 , 0.0001 , 0.00001 , 0.000001 ,
                                0.0000001 , 0.00000001 , 0.000000001 , 0.0000000001 } ;
    if ( ntotal < 10 )
      fnumber = ( double )( sign < 0 ? -ileading : ileading ) * fdivs[ ntrailing ] ;
    else {
      if ( ntrailing < 10 )
        fnumber = ( sign < 0 ? -fleading : fleading ) * fdivs[ ntrailing ] ;
      else
        fnumber = ( sign < 0 ? -fleading : fleading ) / pow( 10.0 , ( double )( ntrailing )) ;
    }
  }

  if ( nleadingexp > 0 ) {
    if ( nleadingexp < 10 )
      fexponent = ( signexp < 0 ? -ileadingexp : ileadingexp ) ;
    else
      fexponent = ( signexp < 0 ? -fleadingexp : fleadingexp ) ;

    fnumber = fnumber * pow( 10.0 , fexponent ) ;
  }

  if ( ! realrange( fnumber ))
    return FALSE ;
  if ( ! realprecision( fnumber ))
    fnumber = 0.0 ;

  (*number) = ( float )fnumber ;
  (*psmem) = smem ;
  (*pslen) = slen ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */

#define CFF_STACK_MAX   48

typedef struct CFF_STACK {
  int32 size ;
  CFF_OBJECT objects[ CFF_STACK_MAX ] ;
} CFF_STACK ;

/* Table 3 Operand Encoding
 * Size         b0 range        Value range             Value calculation
 * 1            32 - 246        -107 - +107             b0-139
 * 2            247 - 250       +108 - +1131            (b0-247) * 256+b1+108
 * 2            251 - 254       -1131 - -108            -(b0-251) * 256-b1-108
 * 3            28              -32768 +32767           b1<<8|b2
 * 5            29              -(2^31) - +(2^31-1)     b1<<24|b2<<16|b3<<8|b4
 */

static Bool cff_decodeDict(uint8 *smem, uint32 slen, CFF_DATA *CFF_data)
{
  int32 i ;
  CFF_STACK stack ;

  HQASSERT( smem     , "smem     NULL in cff_decodeDict" ) ;
  HQASSERT( CFF_data , "CFF_data NULL in cff_decodeDict" ) ;

  stack.size = 0 ;

  while ( slen > 0 ) {
    int32 ival ;
    uint8 b0 = (*smem++) ;
    slen -= 1 ;
    if ( 32 <= b0 && b0 <= 246 ) {
      ival = b0 - 139 ;
      if ( stack.size == CFF_STACK_MAX )
        return error_handler( STACKOVERFLOW ) ;
      stack.objects[ stack.size ].type = CFF_INT ;
      stack.objects[ stack.size ].value.integer = ival ;
      ++stack.size ;
    }
    else if ( 247 <= b0 && b0 <= 250 ) {
      if ( slen < 1 )
        return error_handler( INVALIDFONT ) ;
      else {
        uint8 b1 = (*smem++) ;
        slen -= 1 ;
        ival = (( b0 - 247 ) << 8 ) + b1 + 108 ;
        if ( stack.size == CFF_STACK_MAX )
          return error_handler( STACKOVERFLOW ) ;
        stack.objects[ stack.size ].type = CFF_INT ;
        stack.objects[ stack.size ].value.integer = ival ;
        ++stack.size ;
      }
    }
    else if ( 251 <= b0 && b0 <= 254 ) {
      if ( slen < 1 )
        return error_handler( INVALIDFONT ) ;
      else {
        uint8 b1 = (*smem++) ;
        slen -= 1 ;
        ival = -(( b0 - 251 ) << 8 ) - b1 - 108 ;
        if ( stack.size == CFF_STACK_MAX )
          return error_handler( STACKOVERFLOW ) ;
        stack.objects[ stack.size ].type = CFF_INT ;
        stack.objects[ stack.size ].value.integer = ival ;
        ++stack.size ;
      }
    }
    else if ( b0 == 28 ) {
      if ( slen < 2 )
        return error_handler( INVALIDFONT ) ;
      else {
        uint8 b1 = (*smem++) ;
        uint8 b2 = (*smem++) ;
        slen -= 2 ;
        ival = (int16)(( b1 << 8 ) | b2 ) ; /* sign extend the 16 bit number. */
        if ( stack.size == CFF_STACK_MAX )
          return error_handler( STACKOVERFLOW ) ;
        stack.objects[ stack.size ].type = CFF_INT ;
        stack.objects[ stack.size ].value.integer = ival ;
        ++stack.size ;
      }
    }
    else if ( b0 == 29 ) {
      if ( slen < 4 )
        return error_handler( INVALIDFONT ) ;
      else {
        uint8 b1 = (*smem++) ;
        uint8 b2 = (*smem++) ;
        uint8 b3 = (*smem++) ;
        uint8 b4 = (*smem++) ;
        slen -= 4 ;
        ival = (int32)(( b1 << 24 ) | ( b2 << 16 ) | ( b3 << 8 ) | b4) ;
        if ( stack.size == CFF_STACK_MAX )
          return error_handler( STACKOVERFLOW ) ;
        stack.objects[ stack.size ].type = CFF_INT ;
        stack.objects[ stack.size ].value.integer = ival ;
        ++stack.size ;
      }
    }
    else if ( b0 == 30 ) {
      float number = 0.0f ;
      if ( ! cff_scanreal(&smem, &slen, &number) )
        return FALSE ;
      if ( stack.size == CFF_STACK_MAX )
        return error_handler( STACKOVERFLOW ) ;
      stack.objects[ stack.size ].type = CFF_REAL ;
      stack.objects[ stack.size ].value.real = number ;
      ++stack.size ;
    }
    else if ( b0 == 31 ) {
      HQTRACE(debug_cff,("Type2 program not supported (yet)"));
      return error_handler( INVALIDFONT ) ;
    }
    else {
      CFF_OBJECT *cff_op = NULL ;
      CFF_OBJECT *cff_array = NULL ;
      int16 found = TRUE ; /* Unless told otherwise, pretend we've seen it */

      if ( b0 == 12 ) { /* This is a sub operator. */
        if ( slen < 1 )
          return error_handler( INVALIDFONT ) ;
        else {
          uint8 b1 = (*smem++) ;
          slen -= 1 ;
          if ( b1 < CFF_MAXSUBOPS ) {
            cff_op = ( & CFF_data->cff_subops[ b1 ] ) ;
            found = cff_op->found ;
          } else {
            HQTRACE(debug_cff,("out of range sub-op (%d)",b1));
          }
        }
      }
      else {            /* And this is a operator. */
        if ( b0 < CFF_MAXOPS ) {
          cff_op = ( & CFF_data->cff_ops[ b0 ] ) ;
          if ( cff_op->name != NULL ) {
            found = cff_op->found ;
          } else {
            HQTRACE(debug_cff,("reserved operator (%d)", b0));
          }
        } else {
          HQTRACE(debug_cff,("out of range op (%d)",b0));
        }
      }

      if ( !found ) {
        /* Now decompose args into structure.
           Don't do this bit if this operator has been seen before
           (it may have been if processing a sythetic font). */
        switch ( cff_op->type ) {
        case CFF_SID:
          if ( (stack.size--) == 0 ) {
            HQTRACE(debug_cff,("stack underflow"));
            return error_handler( STACKUNDERFLOW ) ;
          }
          if ( stack.objects[ stack.size ].type != CFF_INT )
            return error_handler( TYPECHECK ) ;
          ival = stack.objects[ stack.size ].value.integer ;
          if ( ival < 0x0000 || ival > 0xFFFF ) {
            HQTRACE(debug_cff,("invalid SID (%d)",ival));
            return error_handler( UNDEFINEDRESULT ) ;
          }
          cff_op->value.integer = ival ;
          break ;
        case CFF_BOOL:
          if ( (stack.size--) == 0 ) {
            HQTRACE(debug_cff,("stack underflow"));
            return error_handler( STACKUNDERFLOW ) ;
          }
          if ( stack.objects[ stack.size ].type != CFF_INT )
            return error_handler( TYPECHECK ) ;
          ival = stack.objects[ stack.size ].value.integer ;
          if ( ival != 0 && ival != 1 ) {
            HQTRACE(debug_cff,("invalid bool (%d)",ival));
            return error_handler( UNDEFINEDRESULT ) ;
          }
          cff_op->value.integer = ival ;
          break ;
        case CFF_INT:
          if ( (stack.size--) == 0 ) {
            HQTRACE(debug_cff,("stack underflow"));
            return error_handler( STACKUNDERFLOW ) ;
          }
          cff_op->type = stack.objects[ stack.size ].type ;
          if ( stack.objects[ stack.size ].type != CFF_INT )
            return error_handler( UNDEFINEDRESULT ) ;
          cff_op->value.integer = stack.objects[ stack.size ].value.integer ;
          break ;
        case CFF_NUM:
          if ( (stack.size--) == 0 ) {
            HQTRACE(debug_cff,("stack underflow"));
            return error_handler( STACKUNDERFLOW ) ;
          }
          cff_op->type = stack.objects[ stack.size ].type ;
          if ( stack.objects[ stack.size ].type == CFF_INT )
            cff_op->value.integer = stack.objects[ stack.size ].value.integer ;
          else
            cff_op->value.real    = stack.objects[ stack.size ].value.real ;
          break ;
        case CFF_ARRAY:
          if ( cff_op->length != 0 &&
               stack.size != cff_op->length )
            return error_handler( INVALIDFONT ) ;
          if ( stack.size == 0 ) {
            cff_op->length = 0 ;
            cff_op->value.array = NULL ;
          }
          else {
            cff_array = mm_alloc( mm_pool_temp ,
                                  stack.size * sizeof( CFF_OBJECT ) ,
                                  MM_ALLOC_CLASS_CFF_STACK ) ;
            if ( ! cff_array )
              return error_handler( VMERROR ) ;
            cff_op->length = stack.size ;
            cff_op->value.array = cff_array ;
            for ( i = 0 ; i < stack.size ; ++i ) {
              cff_array[ i ].type = stack.objects[ i ].type ;
              cff_array[ i ].length = 0 ;
              cff_array[ i ].found = TRUE ;
              cff_array[ i ].defalt = NULL ;
              if ( stack.objects[ i ].type == CFF_INT )
                cff_array[ i ].value.integer = stack.objects[ i ].value.integer ;
              else
                cff_array[ i ].value.real    = stack.objects[ i ].value.real ;
            }
          }
          break ;
        case CFF_DELTA:
          if ( stack.size == 0 ) {
            cff_op->length = 0 ;
            cff_op->value.array = NULL ;
          }
          else {
            Bool isint;
            float fval ;
            cff_array = mm_alloc( mm_pool_temp ,
                                  stack.size * sizeof( CFF_OBJECT ) ,
                                  MM_ALLOC_CLASS_CFF_STACK ) ;
            if ( ! cff_array )
              return error_handler( VMERROR ) ;
            cff_op->length = stack.size ;
            cff_op->value.array = cff_array ;
            ival = 0 ;
            fval = 0.0f ;
            isint = TRUE ;
            for ( i = 0 ; i < stack.size ; ++i ) {
              cff_array[ i ].length = 0 ;
              cff_array[ i ].found = TRUE ;
              cff_array[ i ].defalt = NULL ;
              if ( stack.objects[ i ].type == CFF_INT ) {
                int32 itmp = stack.objects[ i ].value.integer ;
                if ( isint )
                  ival = ival + itmp ;
                else
                  fval = fval + itmp ;
              }
              else {
                float ftmp = stack.objects[ i ].value.real ;
                if ( isint )
                  fval = ( float )ival + ftmp ;
                else
                  fval = fval + ftmp ;
                isint = FALSE ;
              }
              if ( isint ) {
                cff_array[ i ].type = CFF_INT ;
                cff_array[ i ].value.integer = ival ;
              }
              else {
                cff_array[ i ].type = CFF_REAL ;
                cff_array[ i ].value.real = fval ;
              }
            }
          }
          break ;
        case CFF_IGNORE:
          break ;
        default:
          HQTRACE(debug_cff,("unknown op type"));
          break ;
        }
        cff_op->found = TRUE ;
      }                     /* this op not seen before */
      stack.size = 0 ;
    }
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool cff_readEncoding(CFF_CACHE *cff_cache, uint32 frame)
{
  uint8 format ;
  uint8 *cffmem ;
  uint8 nCodes ;
  uint8 nRanges ;
  uint16 glyph ;
  uint32 offset ;
  int32 i ;
  uint32 *glyphSIDArray ;
  CFF_DATA *CFF_data ;
  uint32 glyph_count ;

  /* Since SIDs are 16 bit numbers, and all other references refer to
   * a charset number, and, the maximum number of charsets is 65535
   * we can use this top bit to indicate if we're storing a charset
   * reference or a SID.
   */
#define CFF_NOTASID 0x80000000

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT( CFF_data , "CFF_data NULL in cff_readEncoding" ) ;
  HQASSERT( CFF_data->charStringsIndex ,
            "CFF_data->charStringsIndex NULL in cff_readEncoding");
  glyph_count = CFF_data->charStringsIndex->count ;

  if ( (cffmem = cff_frame(cff_cache, frame, CFF_ENCODING)) == NULL )
    return FALSE ;

  CFF_Card8( cffmem , 0 , format ) ;

  frame += CFF_ENCODING ;

  glyphSIDArray = ( & CFF_data->glyphSIDArray[ 0 ] ) ;

  for ( i = 0 ; i < 256 ; ++i )
    glyphSIDArray[ i ] = 0 | CFF_NOTASID ;   /* aka ".notdef" */

  /* TN5176 (16 Mar 00) p. 17: "A few fonts have multiply-encoded glyphs which
     are not supported directly by any of the above formats. This situation
     is indicated by setting the high-order bit in the format byte and
     supplementing the encoding, regardless of format type, as shown in Table
     14." */
  switch ( format & (~0x80) ) {
  case 0:
    /* Table 13 Format 0
     * Type     Name    Description
     * Card8    format  =0
     * Card8    nCodes  Number of encoded glyphs
     * Card8    code    Code array
     *           [nCodes]
     */
    if ( (cffmem = cff_frame(cff_cache, frame, CFF_ENCODING_0)) == NULL )
      return FALSE ;

    CFF_Card8( cffmem , 0 , nCodes ) ;

    frame += CFF_ENCODING_0 ;

    if ( (cffmem = cff_frame(cff_cache, frame,
                             CFF_ENCODING_0_1 * nCodes)) == NULL )
      return FALSE ;

    for ( glyph = 1, offset = 0 ; nCodes > 0 ; ++glyph, --nCodes ) {
      uint8 code ;

      CFF_Card8( cffmem , offset , code ) ;
      offset += CFF_ENCODING_0_1 ;

      glyphSIDArray[ code ] = glyph | CFF_NOTASID ;
    }

    frame += offset ;

    break ;

  case 1:
    /* Table 14 Format 1
     * Type     Name    Description
     * Card8    format  =1
     * Card8    nRanges Number of code ranges
     * struct   Range1  Range1 array (see Table 15)
     *           [nRanges]
     *
     */
    if ( (cffmem = cff_frame(cff_cache, frame, CFF_ENCODING_1)) == NULL )
      return FALSE ;

    CFF_Card8( cffmem , 0 , nRanges ) ;

    frame += CFF_ENCODING_1 ;

    if ( (cffmem = cff_frame(cff_cache, frame,
                             nRanges * CFF_ENCODING_1_1)) == NULL )
      return FALSE ;

    for ( glyph = 1, offset = 0 ; nRanges > 0 ; --nRanges ) {
      uint8 first , nLeft ;

      /* Table 15 Range1 Format (Encoding)
       * Type   Name    Description
       * Card8  first   First code in range
       * Card8  nLeft   Codes left in range (excluding first)
       */
      CFF_Card8( cffmem , offset , first ) ;
      CFF_Card8( cffmem , offset + 1 , nLeft ) ;
      offset += CFF_ENCODING_1_1 ;
      if (( uint32 )first + ( uint32 )nLeft >= 256u )
        return error_handler( INVALIDFONT ) ;

      for (;;) {

        glyphSIDArray[first] = glyph++ | CFF_NOTASID ;

        if ( nLeft == 0 || glyph >= glyph_count )
          break ;

        ++first ;
        --nLeft ;
      }
    }

    frame += offset ;

    break ;

  default:
    HQTRACE(debug_cff,("invalid format: %d",format));
    return error_handler( INVALIDFONT ) ;
  }

  if ( format & 0x80 ) {
    /* Table 16 Supplemental Encoding Data
     * Type     Name            Description
     * Card8    nSups           Number of supplementary mappings
     * struct   Supplement      Supplementary encoding array (see
     *           [nSups]        Table 17 below)
     */
    /* Supplementary tables. */
    uint8 nSups ;

    if ( (cffmem = cff_frame(cff_cache, frame, CFF_ENCODING_S)) == NULL )
      return FALSE ;

    CFF_Card8( cffmem , 0 , nSups ) ;

    frame += CFF_ENCODING_S ;

    if ( (cffmem = cff_frame(cff_cache, frame,
                             CFF_ENCODING_S_1 * nSups)) == NULL )
      return FALSE ;

    for ( offset = 0 ; nSups > 0 ; --nSups ) {
      uint8 code ;

      CFF_Card8( cffmem , offset , code ) ;
      CFF_SID( cffmem , offset + 1 , glyph ) ;
      offset += CFF_ENCODING_S_1 ;
      if ( glyphSIDArray[ code ] != ( 0 | CFF_NOTASID ))
        return error_handler( INVALIDFONT ) ;

      glyphSIDArray[ code ] = glyph ;
    }

    frame += offset ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool cff_readCharsets(CFF_CACHE *cff_cache, uint32 frame)
{
  uint8 format ;
  uint16 nGlyphs ;
  uint16 glyphSID ;
  uint16 firstSID ;
  uint16 nLeft ;
  int32 i ;
  int32 maxCID ;
  uint32 offset ;

  uint16 *charsetNames ;
  uint8 *cffmem ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT( CFF_data , "CFF_data NULL in cff_readCharsets" ) ;

  if ( (cffmem = cff_frame(cff_cache, frame, CFF_CHARSETS)) == NULL )
    return FALSE ;

  CFF_Card8( cffmem , 0 , format ) ;

  frame += CFF_CHARSETS ;

  nGlyphs = ( uint16 )CFF_data->charStringsIndex->count ;
  if ( nGlyphs == 0 )
    return error_handler( INVALIDFONT ) ;

  charsetNames = mm_alloc( mm_pool_temp , nGlyphs * sizeof(uint16) , MM_ALLOC_CLASS_CFF_NAME ) ;
  if ( ! charsetNames )
    return error_handler( VMERROR ) ;

  CFF_data->charset = CFF_EMBEDDED ;
  CFF_data->charsetMaxCID = 0 ;
  CFF_data->charsetNames = charsetNames ;
  CFF_data->charsetLength = nGlyphs ;

  for ( i = 0 ; i < nGlyphs ; ++i )
    charsetNames[ i ] = 0 ;

  maxCID = 0 ;
  switch ( format ) {
  case 0:
    /* Table 19 Format 0
     * Type     Name    Description
     * Card8    format  =0
     * SID      glyph   Glyph name array
     *           [nGlyphs-1]
     */
    if ( (cffmem = cff_frame(cff_cache, frame,
                             CFF_CHARSETS_0 * (nGlyphs - 1))) == NULL )
      return FALSE ;

    for ( offset = 0, i = 1 ; i < nGlyphs ; ++i ) {
      CFF_SID( cffmem , offset , glyphSID ) ;
      offset += CFF_CHARSETS_0 ;
      charsetNames[ i ] = glyphSID ;
      if ( glyphSID > maxCID )
        maxCID = glyphSID ;
    }

    frame += offset ;

    break ;

  case 2:
    /* Table 20 Format 1
     * Type     Name    Description
     * Card8    format  =1
     * struct   Range1  Range1 array (see Table 21)
     *           [<varies>]
     *
     * Table 21 Range1 Format (Charset)
     * Type     Name    Description
     * SID      first   First glyph in range
     * Card8    nLeft   Glyphs left in range (excluding first)
     */

    /* FALL THROUGH */
  case 1:
    /*
     * Table 22 Format 2
     * Type     Name    Description
     * Card8    format  =2
     * struct   Range2  Range2 array (see Table 23)
     *           [<varies>]
     *
     * Table 23 Range2  Format
     * Type     Name    Description
     * SID      first   First glyph in range
     * Card16   nLeft   Glyphs left in range (excluding first)
     */
    {
      uint32 length = (format == 1) ? CFF_CHARSETS_1 : CFF_CHARSETS_2 ;

      for ( i = 1 ; i < nGlyphs ; ) {
        if ( (cffmem = cff_frame(cff_cache, frame, length)) == NULL )
          return FALSE ;

        CFF_SID( cffmem , 0, firstSID ) ;

        if ( format == 1 ) {
          CFF_Card8( cffmem , 2 , nLeft ) ;
          frame += CFF_CHARSETS_1 ;
        } else {
          CFF_Card16( cffmem , 2, nLeft ) ;
          frame += CFF_CHARSETS_2 ;
        }

        if ( i + nLeft >= nGlyphs )
          return error_handler( INVALIDFONT ) ;

        for (;;) {
          charsetNames[ i++ ] = firstSID++ ;

          if ( nLeft == 0 )
            break ;

          --nLeft ;
        }
        if ( --firstSID > maxCID )
          maxCID = firstSID ;
      }
    }

    break ;

  default:
    HQTRACE(debug_cff,("invalid charset: %d",format));
    return error_handler( INVALIDFONT ) ;
  }

  CFF_data->charsetMaxCID = maxCID ;

  return TRUE ;
}

/* The CFF spec says that there are no default charsets for CID fonts.
   However, we have come across cases where just the .notdef character exists
   in an embedded CFF font, and it has no charset. Fake an identity mapping
   if no charset exists. */
static Bool cff_fakeCharset(CFF_DATA *CFF_data)
{
  uint16 nGlyphs ;
  int32 i ;
  uint16 *charsetNames ;

  HQASSERT( CFF_data , "CFF_data NULL in cff_fakeCharset" ) ;

  nGlyphs = ( uint16 )CFF_data->charStringsIndex->count ;
  if ( nGlyphs == 0 )
    return error_handler( INVALIDFONT) ;

  charsetNames = mm_alloc(mm_pool_temp, nGlyphs * sizeof(uint16), MM_ALLOC_CLASS_CFF_NAME) ;
  if ( ! charsetNames )
    return error_handler( VMERROR) ;

  CFF_data->charset = CFF_EMBEDDED ;
  CFF_data->charsetMaxCID = nGlyphs - 1 ;
  CFF_data->charsetNames = charsetNames ;
  CFF_data->charsetLength = nGlyphs ;

  for ( i = 0 ; i < nGlyphs ; ++i )
    charsetNames[ i ] = (uint16)i ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool cff_readFDSelect(CFF_CACHE *cff_cache, uint32 frame)
{
  uint8 format ;
  uint16 nGlyphs, nRanges, first ;
  int32 numCID ;
  int32 i ;
  uint32 offset ;

  uint8 *fdSelect ;
  uint8 *cffmem ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT( CFF_data , "CFF_data NULL" ) ;

  if ( (cffmem = cff_frame(cff_cache, frame, CFF_FDSELECT)) == NULL )
    return FALSE ;

  CFF_Card8( cffmem , 0 , format ) ;

  frame += CFF_FDSELECT ;

  nGlyphs = ( uint16 )CFF_data->charsetLength ;
  numCID = CFF_data->charsetMaxCID + 1 ;
  if ( nGlyphs == 0 )
    return error_handler( INVALIDFONT ) ;

  fdSelect = mm_alloc( mm_pool_temp , numCID * sizeof( uint8 ) , MM_ALLOC_CLASS_CFF_FDS ) ;
  if ( ! fdSelect )
    return error_handler( VMERROR ) ;

  CFF_data->fdSelect = fdSelect ;
  CFF_data->fdSelectLength = numCID ;

  for ( i = 0 ; i < numCID ; ++i )
    fdSelect[ i ] = 0 ;

  switch ( format ) {
  case 0:
    /* Table 29 Format 0
     * Type     Name    Description
     * Card8    format  =0
     * Card8    fds     FD Selector array
     *           [nGlyphs]
     */
    if ( (cffmem = cff_frame(cff_cache, frame,
                             nGlyphs * CFF_FDSELECT_0)) == NULL )
      return FALSE ;

    for ( offset = 0, i = 0 ; i < nGlyphs ; ++i ) {
      uint16 cid = CFF_data->charsetNames[ i ] ;

      HQASSERT(cid < numCID, "CID showhow larger than max glyph CID") ;

      CFF_Card8( cffmem , offset , fdSelect[cid] ) ;
      offset += CFF_FDSELECT_0 ;
    }

    frame += offset ;

    break ;

  case 3:
    /* Table 30 Format 3
     * Type     Name    Description
     * Card8    format  =3
     * Card16   nRanges Number of ranges
     * struct   Range3  Range3 array (see Table 31)
     *           [nRanges]
     * Card16   sentinel Sentinel GID
     *
     * Table 31 Range3 Format
     * Type     Name    Description
     * Card16   first   First glyph index in range
     * Card8    fd      FD index for all glyphs in range
     */
    if ( (cffmem = cff_frame(cff_cache, frame, CFF_FDSELECT_3)) == NULL )
      return FALSE ;

    CFF_Card16( cffmem , 0 , nRanges ) ;
    CFF_Card16( cffmem , 2 , first ) ;

    frame += CFF_FDSELECT_3 ;

    if ( (cffmem = cff_frame(cff_cache, frame,
                             nRanges * CFF_FDSELECT_3_1)) == NULL )
      return FALSE ;

    for ( offset = 0, i = 0 ; i < nRanges ; ++i ) {
      uint16 next ;
      uint8 fd ;

      CFF_Card8( cffmem , offset , fd ) ;
      CFF_Card16( cffmem , offset + 1 , next ) ;
      offset += CFF_FDSELECT_3_1 ;

      if ( first > next || next > numCID )
        return error_handler( INVALIDFONT ) ;

      while ( first < next ) {
        uint16 cid = CFF_data->charsetNames[first++] ;
        HQASSERT(cid < numCID, "CID showhow larger than max glyph CID") ;
        fdSelect[cid] = fd ;
      }
    }

    frame += offset ;

    break ;

  default:
    HQTRACE(debug_cff,("invalid fdselect: %d",format));
    return error_handler( INVALIDFONT ) ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Initialise a CFF font set; this will be the first routine to be called when
   a new font is initialised. It checks the header, and reads the global
   indices. After it is called, cff_matchName will usually be called to select
   a font index, and then cff_initFont will be called to initialise a
   particular font within the font set. CIF fonts will call cff_initSubFont
   to load private font data and subrs from FDArray. */
static Bool cff_initFontSet(CFF_CACHE *cff_cache)
{
  uint32 frame = 0 ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT(CFF_data, "No font data") ;

  HqMemZero(CFF_data, sizeof(CFF_DATA));

  /* First of all check it's a CFF font. */
  if ( ! cff_checkheader(cff_cache, &frame, &CFF_data->offSize) ||
       ! cff_readIndex(cff_cache, &frame, &CFF_data->nameIndex) ||
       ! cff_readIndex(cff_cache, &frame, &CFF_data->topDictIndex) )
    return FALSE ;

  if ( CFF_data->topDictIndex->count != CFF_data->nameIndex->count ) {
    HQTRACE(debug_cff,("differing font names/top dicts: %d,%d",
                       CFF_data->topDictIndex->count,
                       CFF_data->nameIndex->count));
    return FALSE ;
  }

  if ( ! cff_readIndex(cff_cache, &frame, &CFF_data->stringIndex) ||
       ! cff_readIndex(cff_cache, &frame, &CFF_data->globalSubrIndex) )
    return FALSE ;

  /* Set loaded font and subfont to invalid indices */
  CFF_data->fontindex = -1 ;
  CFF_data->fdindex = -1 ;

  return TRUE ;
}

/* Find a particular named font in a font set. The fontname object can be a
   name or string, indicating a particular named font should be used, an
   integer, indicating the nth font from the set should be used, or an array
   of names, strings, or integers, indicating a preference order. The
   fontname selected will be put back in the fontname object. If the name
   does not match, then the return status will be true but the fontname will
   be ONULL. */
static Bool cff_matchName(CFF_CACHE *cff_cache, CFF_STRING *cffname,
                          int32 is_pdf, OBJECT *fontname, int32 *fontindex)
{
  int32 namelen ;
  uint8 *frame, strName[MAXPSSTRING] ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  HQASSERT(cffname, "No CFF name") ;
  HQASSERT(fontname, "No font name") ;
  HQASSERT(fontindex, "No font index") ;

  HQASSERT(oType(*fontname) == ONAME ||
           oType(*fontname) == OSTRING ||
           oType(*fontname) == OINTEGER ||
           oType(*fontname) == OPACKEDARRAY ||
           oType(*fontname) == OARRAY,
           "CFF fontname is not one of required types") ;

  namelen = (int32)cffname->length ;
  HQASSERT(namelen <= MAXPSSTRING,
           "CFF name too long or too short") ;

  if ( (frame = cff_frame(cff_cache, cffname->frame, namelen)) == NULL )
    return FALSE ;

  /* Substitute escaped hex bytes (via '#' sequence) for the direct
     ascii value, but only if the font has been obtained from a PDF
     file. Nowt in the specs about this but it seems certain PDF
     writers are want to do this. */
  if ( is_pdf > 0 )
    namelen = cff_parseName(strName, frame, namelen);
  else
    HqMemCpy(strName, frame, namelen);

  /* If the name has been invalidated, don't increment the fontindex. */
  if ( namelen == 0 || strName[0] != '\0' ) {
    OBJECT fnames = OBJECT_NOTVM_NOTHING, fnamearray = OBJECT_NOTVM_NOTHING ;

    /* Make sure names are represented in an array for consistency */
    Copy(&fnames, fontname) ;
    if ( oType(fnames) != OARRAY && oType(fnames) != OPACKEDARRAY ) {
      Copy(&fnamearray, fontname) ;  /* [60118] Can't use original object as array, */
      theTags(fnames) = OARRAY | LITERAL | READ_ONLY ;
      theLen(fnames) = 1 ;
      oArray(fnames) = &fnamearray ;
    }

    while ( theLen(fnames) > 0 ) {
      OBJECT *nameobj = oArray(fnames) ;
      uint8 *fnamestr = NULL ;
      int32 fnamelen = -1 ;
      int32 fnameindex = -1 ;

      switch ( oType(*nameobj) ) {
      case OSTRING:
        fnamestr = oString(*nameobj);
        fnamelen = theLen(*nameobj);
        break ;
      case ONAME:
        fnamestr = theICList(oName(*nameobj));
        fnamelen = theINLen(oName(*nameobj));
        break ;
      case OINTEGER:
        fnameindex = oInteger(*nameobj) ;
        if ( fnameindex < 0 )
          return error_handler(RANGECHECK) ;
        break ;
      default:
        return error_handler( TYPECHECK) ;
      }

      /* We can't directly index the names index, because deleted names are
         not excluded from it. TN5176 (16 Mar 00) p. 12 says that if the
         first byte of a name is set to zero, the font should be ignored.
         fnamelen will not match if the font is indexed, so HqMemCmp won't
         be called with a null fnamestr. */
      if ( fnameindex == *fontindex ||
           (fnamelen == namelen &&
            HqMemCmp(strName, namelen, fnamestr, fnamelen) == 0) ) {
        /* Reset the fontname passed in to the name of the font we've
           actually matched on. */
        if ( namelen <= MAXPSNAME ) {
          if ( (oName(nnewobj) = cachename(namelen > 0 ? strName : NULL, namelen)) == NULL )
            return FALSE ;
          Copy(fontname, &nnewobj) ;
        } else {
          if ( !ps_string(fontname, strName, namelen) )
            return FALSE ;
        }

        return TRUE ;
      }

      /* Prepare for next font name match. */
      --theLen(fnames) ;
      ++oArray(fnames) ;
    }

    /* Valid fontname, but did not match anything */
    *fontindex += 1 ;
  }

  /* Not a valid fontname, or not a match for a valid fontname */
  object_store_null(fontname) ;

  return TRUE ;
}

/* Return the charstring type of the unpacked font. */
static uint8 cff_charstringType(CFF_DATA *CFF_data)
{
  CFF_OBJECT *cff_op ;

  HQASSERT(CFF_data, "No CFF data") ;

  cff_op = &CFF_data->cff_subops[CFF_SUBOP_CHARTYPE] ;
  if ( cff_op->found ) {
    HQASSERT(cff_op->type == CFF_INT, "Charstring type is not integer") ;
    switch ( cff_op->value.integer ) {
    case 1:
      return CHAR_Type1 ;
    case 2:
      return CHAR_Type2 ;
    default:
      return CHAR_Undefined ;
    }
  }

  /* Default to Type 2, as in TN5176 (16 Mar 00), p.21 */
  return CHAR_Type2 ;
}

static Bool cff_initFont(CFF_CACHE *cff_cache, int32 fontindex)
{
  uint8 *smem ;
  Bool result;
  CFF_STRING *string ;
  CFF_OBJECT *cff_op ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT(CFF_data, "No CFF data") ;
  HQASSERT(CFF_data->nameIndex->count == CFF_data->topDictIndex->count,
           "Name and top dict index out of step") ;

  if ( fontindex < 0 || (uint32)fontindex >= CFF_data->nameIndex->count ) {
    /* Shouldn't happen, because we have control over the font index, but
       in case someone pokes the PostScript SubFont key in a CFF font, this
       should catch it. */
    HQFAIL("Font index is out of range") ;
    return error_handler(RANGECHECK) ;
  }

  /* Set currently selected font */
  CFF_data->fontindex = fontindex ;
  CFF_data->fdindex = -1 ;

  /* Re-initialise instance specific data from generic CFF op tables*/
  HqMemCpy(CFF_data->cff_ops, cff_ops, CFF_MAXOPS * sizeof(CFF_OBJECT)) ;
  HqMemCpy(CFF_data->cff_subops, cff_subops, CFF_MAXSUBOPS * sizeof(CFF_OBJECT)) ;

  string = &CFF_data->topDictIndex->strings[fontindex] ;
  if ( (smem = cff_frame(cff_cache, string->frame, string->length)) == NULL )
    return FALSE ;

  result = cff_decodeDict(smem, string->length, CFF_data) ;

  if ( !result )
    return FALSE ;

  while ( CFF_data->cff_subops[ CFF_SUBOP_SYNTHETICBASE ].found ) {
    /* Probably a re-encoded font (e.g. CE variant) */
    int32 synthindex = CFF_data->cff_subops[ CFF_SUBOP_SYNTHETICBASE ].value.integer ;

    HQASSERT( debug_cff , "font with SyntheticBase" ) ;

    CFF_data->cff_subops[ CFF_SUBOP_SYNTHETICBASE ].found = FALSE ;

    if ( synthindex < 0 || (uint32)synthindex > CFF_data->nameIndex->count ) {
      HQTRACE(debug_cff,("Invalid SyntheticBase: %d,%d",
                         synthindex, CFF_data->nameIndex->count));
      return error_handler( INVALIDFONT ) ;
    }

    /* now read the base font dictionary, any operators in both dicts will be
       ignored in the second pass. */

    string = &CFF_data->topDictIndex->strings[fontindex] ;
    if ( (smem = cff_frame(cff_cache, string->frame, string->length)) == NULL )
      return FALSE ;

    result = cff_decodeDict(smem, string->length, CFF_data) ;

    if ( !result )
      return FALSE ;
  }

  cff_op = ( & CFF_data->cff_subops[ CFF_SUBOP_MULTIPLEMASTER ] ) ;
  if ( cff_op->found ) {
    HQTRACE(1,("Multiple Master font in CFF font"));
    return error_handler( UNDEFINEDRESULT ) ;
  }

  cff_op = ( & CFF_data->cff_subops[ CFF_SUBOP_CHAMELEON ] ) ;
  if ( cff_op->found ) {
    HQTRACE(1,("Chameleon font in CFF font"));
    return error_handler( UNDEFINEDRESULT ) ;
  }

  cff_op = ( & CFF_data->cff_subops[ CFF_SUBOP_ROS ] ) ;
  if ( cff_op->found ) {
    HQTRACE(debug_cff,("CID font in CFF font"));
    CFF_data->fonttype = CFF_CIDFONT ;
  }
  else {
    CFF_data->fonttype = CFF_FONT ;
  }

  cff_op = ( & CFF_data->cff_ops[ CFF_OP_CHARSTRINGS ] ) ;
  if ( cff_op->found ) {
    uint32 offset = cff_op->value.integer ;
    if ( ! cff_readCharStringsIndex(cff_cache, offset) )
      return FALSE ;
  } else
    return error_handler( INVALIDFONT ) ;


  /* Encoding comes after charstring handling, as it uses the
   * charstring count to detect encoding problems. */
  cff_op = ( & CFF_data->cff_ops[ CFF_OP_ENCODING ] ) ;
  if ( cff_op->found ) {
    uint32 offset = cff_op->value.integer ;
    if ( offset != 0 && offset != 1 ) {
      if ( ! cff_readEncoding(cff_cache, offset) )
        return FALSE ;
    }
  }

  /* And Private dict; MUST come before Subrs */
  switch ( CFF_data->fonttype ) {
  case CFF_FONT:
    cff_op = ( & CFF_data->cff_ops[ CFF_OP_PRIVATE ] ) ;
    if ( cff_op->found ) {
      uint32 pdlength, pdoffset ;

      cff_op = cff_op->value.array ;
      pdlength = ( uint32 )cff_op[ 0 ].value.integer ;
      pdoffset = ( uint32 )cff_op[ 1 ].value.integer ;
      if ( pdlength ) {
        Bool result;

        if ( (smem = cff_frame(cff_cache, pdoffset, pdlength)) == NULL )
          return FALSE ;

        result = cff_decodeDict(smem, pdlength, CFF_data) ;

        if ( !result )
          return FALSE ;
      }

      /* MUST come after Private. */
      cff_op = ( & CFF_data->cff_ops[ CFF_OP_SUBRS ] ) ;
      if ( cff_op->found ) {
        /* Offset is relative to Private dict. */
        uint32 offset = (uint32)cff_op->value.integer + pdoffset ;
        if ( ! cff_readLocalSubrIndex(cff_cache, offset) )
          return FALSE ;
      }
    } else
      return error_handler( INVALIDFONT ) ;

    break ;
  case CFF_CIDFONT:
    cff_op = ( & CFF_data->cff_subops[ CFF_SUBOP_FDARRAY ] ) ;
    if ( cff_op->type != CFF_INT )
      return error_handler( TYPECHECK ) ;
    if ( ! cff_readfontDictIndex(cff_cache, (uint32)cff_op->value.integer) )
      return FALSE ;
    break ;
  default:
    HQFAIL("Can only deal with Font/CIDFont" ) ;
    return error_handler(UNDEFINED) ;
  }

  /* MUST come after CharStrings. */
  cff_op = ( & CFF_data->cff_ops[ CFF_OP_CHARSET ] ) ;
  if ( cff_op->found ) {
    int32 offset = cff_op->value.integer ;
    switch ( offset ) {
    case CFF_ISOADOBE:
    case CFF_EXPERT:
    case CFF_EXPERTSUBSET:
      if ( CFF_data->fonttype == CFF_CIDFONT ) {
        if ( ! cff_fakeCharset(CFF_data) )
          return FALSE ;
      } else
        CFF_data->charset = offset ;

      break ;
    default:
      if ( ! cff_readCharsets(cff_cache, (uint32)offset) )
        return FALSE ;
      break ;
    }
  } else {
    if ( CFF_data->fonttype == CFF_CIDFONT ) {
      if ( ! cff_fakeCharset(CFF_data) )
        return FALSE ;
    } else /* Defined to be CFF_ISOADOBE if not present. */
      CFF_data->charset = CFF_ISOADOBE ;
  }

  /* MUST come after CHARSET. Always create a mapping table of SID/CID to
     GID */
  if ( ! cff_mapCIDtoGID(CFF_data) )
    return FALSE ;

  /* MUST come after CharSet. */
  cff_op = ( & CFF_data->cff_subops[ CFF_SUBOP_FDSELECT ] ) ;
  if ( cff_op->found ) {
    int32 offset = cff_op->value.integer ;
    if ( ! cff_readFDSelect(cff_cache, (uint32)offset) )
      return FALSE ;
  }

  /* Set both top-level and current charstring type. */
  CFF_data->dchartype = CFF_data->chartype = cff_charstringType(CFF_data) ;

  /* No sub-font loaded, so set fdmatrix to identity. We need to know if there
     was a top-level FontMatrix to determine if the sub-font matrices should be
     scaled by a factor of 1000. See PLRM3 p.374.*/
  MATRIX_COPY(&CFF_data->fdmatrix, &identity_matrix) ;
  CFF_data->gotFontMatrix = CFF_data->cff_subops[CFF_SUBOP_FONTMATRIX].found ;

  return TRUE ;
}

static Bool cff_initSubFont(CFF_CACHE *cff_cache, int32 fontindex)
{
  uint8 *frame ;
  Bool result;
  CFF_STRING *string ;
  CFF_OBJECT *cff_op ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT(CFF_data, "No CFF data") ;
  HQASSERT(CFF_data->fonttype == CFF_CIDFONT, "Not a CID font") ;

  HQASSERT(fontindex >= 0 &&
           (uint32)fontindex < CFF_data->fontDictIndex->count,
           "Sub-font index is out of range") ;

  /* Set currently selected sub-font */
  CFF_data->fdindex = fontindex ;

  string = &CFF_data->fontDictIndex->strings[fontindex] ;

  cff_op = ( & CFF_data->cff_ops[ CFF_OP_SUBRS ] ) ;
  cff_releaseop(cff_op, &cff_ops[CFF_OP_SUBRS]) ; /* Must free Subrs dict before loading sub-font */

  cff_op = ( & CFF_data->cff_ops[ CFF_OP_PRIVATE ] ) ;
  cff_releaseop(cff_op, &cff_ops[CFF_OP_PRIVATE]) ; /* Must free Private dict before loading sub-font */

  cff_opfd_release(CFF_data, cff_font_ops) ;
  cff_opfd_release(CFF_data, cff_fontinfo_ops) ;
  cff_opfd_release(CFF_data, cff_private_ops) ;

  if ( (frame = cff_frame(cff_cache, string->frame, string->length)) == NULL )
    return FALSE ;

  result = cff_decodeDict(frame, string->length, CFF_data) ;

  if ( !result )
    return FALSE ;

  cff_op = ( & CFF_data->cff_ops[ CFF_OP_PRIVATE ] ) ;
  if ( cff_op->found ) {
    uint32 pdlength, pdoffset ;

    cff_op = cff_op->value.array ;
    pdlength = ( uint32 )cff_op[ 0 ].value.integer ;
    pdoffset = ( uint32 )cff_op[ 1 ].value.integer ;
    if ( pdlength ) {
      if ( (frame = cff_frame(cff_cache, pdoffset, pdlength)) == NULL )
        return FALSE ;

      result = cff_decodeDict(frame, pdlength, CFF_data) ;

      if ( !result )
        return FALSE ;
    }

    /* MUST come after Private. */
    cff_op = ( & CFF_data->cff_ops[ CFF_OP_SUBRS ] ) ;
    if ( cff_op->found ) {
      /* Offset is relative to Private dict. */
      uint32 offset = (uint32)cff_op->value.integer + pdoffset ;
      if ( ! cff_readLocalSubrIndex(cff_cache, offset) )
        return FALSE ;
    } else if ( CFF_data->localSubrIndex ) {
      cff_freeIndex(&CFF_data->localSubrIndex) ;
    }
  } else
    return error_handler( INVALIDFONT ) ;

  /* Set current charstring type. */
  CFF_data->chartype = cff_charstringType(CFF_data) ;

  /* Set the font matrix in the CFF data. This is either an identity matrix
     or the unconcatenated sub-font matrix, depending on whether a sub-font
     is loaded. The purpose of this matrix is concatenation with the
     fontInfo's FontMatrix when a CID sub-font is selected. The subfont
     matrix is therefore scaled by a factor of 1000 if there was no top-level
     FontMatrix, according to the rules for CID fonts on p.374 of PLRM3. Note
     that the sub-font matrix cannot be changed in the PostScript stub,
     because the internal version is used. */
  cff_op = ( & CFF_data->cff_subops[ CFF_SUBOP_FONTMATRIX ] ) ;
  if ( cff_op->found ) {
    OMATRIX *matrix = &CFF_data->fdmatrix ;
    CFF_OBJECT *cff_array = cff_op->value.array ;
    uint32 index ;

    HQASSERT(cff_op->length == 6, "FontMatrix length is not 6") ;

    for ( index = 0 ; index < 6 ; ++index ) {
      if ( cff_array[index].type == CFF_INT ) {
        matrix->matrix[index >> 1][index & 1] = cff_array[index].value.integer ;
      } else {
        HQASSERT(cff_array[index].type == CFF_REAL,
                 "CFF array element is not INT or REAL") ;
        matrix->matrix[index >> 1][index & 1] = cff_array[index].value.real ;
      }
    }

    MATRIX_SET_OPT_BOTH(matrix) ;

    if ( !CFF_data->gotFontMatrix ) {
      /* There was no top-level font matrix, so we need to scale the sub-font
         matrix by 1000. The top-level font matrix will default to scaling by
         1/1000. */
      MATRIX_00(matrix) *= 1000 ;
      MATRIX_01(matrix) *= 1000 ;
      MATRIX_10(matrix) *= 1000 ;
      MATRIX_11(matrix) *= 1000 ;
    }
  } else { /* Identity matrix if no sub-font matrix is defined */
    MATRIX_COPY(&CFF_data->fdmatrix, &identity_matrix) ;
  }

  return TRUE ;
}

/* Free the stored CFF structures. freeAll determines whether the top-level
   structures (name index, top dict index, string index and global subr index)
   are freed. We may want to leave these alone if we're interrogating multiple
   fonts in a font set. */
static void cff_freeFont(CFF_DATA *CFF_data, Bool freeAll)
{
  int32 i ;

  HQASSERT( CFF_data , "CFF_data NULL" ) ;

  if ( freeAll ) {
    cff_freeIndex(&CFF_data->nameIndex) ;
    cff_freeIndex(&CFF_data->topDictIndex) ;
    cff_freeIndex(&CFF_data->stringIndex) ;
    cff_freeIndex(&CFF_data->globalSubrIndex) ;
  }

  cff_freeIndex(&CFF_data->fontDictIndex) ;
  cff_freeIndex(&CFF_data->charStringsIndex) ;
  cff_freeIndex(&CFF_data->localSubrIndex) ;

  if ( CFF_data->charsetNames ) {
    HQASSERT( CFF_data->charset == CFF_EMBEDDED , "unexpected charset type" ) ;
    mm_free( mm_pool_temp , ( mm_addr_t )CFF_data->charsetNames ,
             CFF_data->charsetLength * sizeof( uint16 )) ;
    CFF_data->charsetNames = NULL ;
  }

  if ( CFF_data->fdSelect ) {
    mm_free( mm_pool_temp , ( mm_addr_t )CFF_data->fdSelect ,
             CFF_data->fdSelectLength * sizeof( uint8 )) ;
    CFF_data->fdSelect = NULL ;
  }

  if ( CFF_data->cidMapping ) {
    mm_free(mm_pool_temp, (mm_addr_t)CFF_data->cidMapping ,
            CFF_data->cidMappingLength * sizeof(uint16)) ;
    CFF_data->cidMapping = NULL ;
  }

  for ( i = 0 ; i < CFF_MAXOPS ; ++i )
    cff_releaseop(&CFF_data->cff_ops[i], &cff_ops[i]) ;

  for ( i = 0 ; i < CFF_MAXSUBOPS ; ++i )
    cff_releaseop(&CFF_data->cff_subops[i], &cff_subops[i]) ;
}

/* -------------------------------------------------------------------------- */
/* Appendix A: Standard Strings */

#define SID_STD_STRING_MAX ( sizeof( sid_stdstrings ) / \
                             sizeof( sid_stdstrings[ 0 ] ))
static char *sid_stdstrings[] = {
/* 0-31 */
  ".notdef", "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
  "quoteright", "parenleft", "parenright", "asterisk", "plus", "comma", "hyphen", "period",
  "slash", "zero", "one", "two", "three", "four", "five", "six",
  "seven", "eight", "nine", "colon", "semicolon", "less", "equal", "greater",
/* 32-63 */
  "question", "at", "A", "B", "C", "D", "E", "F",
  "G", "H", "I", "J", "K", "L", "M", "N",
  "O", "P", "Q", "R", "S", "T", "U", "V",
  "W", "X", "Y", "Z", "bracketleft", "backslash", "bracketright", "asciicircum",
/* 64-95 */
  "underscore", "quoteleft", "a", "b", "c", "d", "e", "f",
  "g", "h", "i", "j", "k", "l", "m", "n",
  "o", "p", "q", "r", "s", "t", "u", "v",
  "w", "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde",
/* 96-127 */
  "exclamdown", "cent", "sterling", "fraction", "yen", "florin", "section", "currency",
  "quotesingle", "quotedblleft", "guillemotleft", "guilsinglleft", "guilsinglright", "fi", "fl", "endash",
  "dagger", "daggerdbl", "periodcentered", "paragraph", "bullet", "quotesinglbase", "quotedblbase", "quotedblright",
  "guillemotright", "ellipsis", "perthousand", "questiondown", "grave", "acute", "circumflex", "tilde",
/* 128-159 */
  "macron", "breve", "dotaccent", "dieresis", "ring", "cedilla", "hungarumlaut", "ogonek",
  "caron", "emdash", "AE", "ordfeminine", "Lslash", "Oslash", "OE", "ordmasculine",
  "ae", "dotlessi", "lslash", "oslash", "oe", "germandbls", "onesuperior", "logicalnot",
  "mu", "trademark", "Eth", "onehalf", "plusminus", "Thorn", "onequarter", "divide",
/* 160-191 */
  "brokenbar", "degree", "thorn", "threequarters", "twosuperior", "registered", "minus", "eth",
  "multiply", "threesuperior", "copyright", "Aacute", "Acircumflex", "Adieresis", "Agrave", "Aring",
  "Atilde", "Ccedilla", "Eacute", "Ecircumflex", "Edieresis", "Egrave", "Iacute", "Icircumflex",
  "Idieresis", "Igrave", "Ntilde", "Oacute", "Ocircumflex", "Odieresis", "Ograve", "Otilde",
/* 192-223 */
  "Scaron", "Uacute", "Ucircumflex", "Udieresis", "Ugrave", "Yacute", "Ydieresis", "Zcaron",
  "aacute", "acircumflex", "adieresis", "agrave", "aring", "atilde", "ccedilla", "eacute",
  "ecircumflex", "edieresis", "egrave", "iacute", "icircumflex", "idieresis", "igrave", "ntilde",
  "oacute", "ocircumflex", "odieresis", "ograve", "otilde", "scaron", "uacute", "ucircumflex",
/* 224-255 */
  "udieresis", "ugrave", "yacute", "ydieresis", "zcaron", "exclamsmall", "Hungarumlautsmall", "dollaroldstyle",
  "dollarsuperior", "ampersandsmall", "Acutesmall", "parenleftsuperior", "parenrightsuperior", "twodotenleader", "onedotenleader", "zerooldstyle",
  "oneoldstyle", "twooldstyle", "threeoldstyle", "fouroldstyle", "fiveoldstyle", "sixoldstyle", "sevenoldstyle", "eightoldstyle",
  "nineoldstyle", "commasuperior", "threequartersemdash", "periodsuperior", "questionsmall", "asuperior", "bsuperior", "centsuperior",
/* 256-287 */
  "dsuperior", "esuperior", "isuperior", "lsuperior", "msuperior", "nsuperior", "osuperior", "rsuperior",
  "ssuperior", "tsuperior", "ff", "ffi", "ffl", "parenleftinferior", "parenrightinferior", "Circumflexsmall",
  "hyphensuperior", "Gravesmall", "Asmall", "Bsmall", "Csmall", "Dsmall", "Esmall", "Fsmall",
  "Gsmall", "Hsmall", "Ismall", "Jsmall", "Ksmall", "Lsmall", "Msmall", "Nsmall",
/* 288-319 */
  "Osmall", "Psmall", "Qsmall", "Rsmall", "Ssmall", "Tsmall", "Usmall", "Vsmall",
  "Wsmall", "Xsmall", "Ysmall", "Zsmall", "colonmonetary", "onefitted", "rupiah", "Tildesmall",
  "exclamdownsmall", "centoldstyle", "Lslashsmall", "Scaronsmall", "Zcaronsmall", "Dieresissmall", "Brevesmall", "Caronsmall",
  "Dotaccentsmall", "Macronsmall", "figuredash", "hypheninferior", "Ogoneksmall", "Ringsmall", "Cedillasmall", "questiondownsmall",
/* 320-351 */
  "oneeighth", "threeeighths", "fiveeighths", "seveneighths", "onethird", "twothirds", "zerosuperior", "foursuperior",
  "fivesuperior", "sixsuperior", "sevensuperior", "eightsuperior", "ninesuperior", "zeroinferior", "oneinferior", "twoinferior",
  "threeinferior", "fourinferior", "fiveinferior", "sixinferior", "seveninferior", "eightinferior", "nineinferior", "centinferior",
  "dollarinferior", "periodinferior", "commainferior", "Agravesmall", "Aacutesmall", "Acircumflexsmall", "Atildesmall", "Adieresissmall",
/* 352-383 */
  "Aringsmall", "AEsmall", "Ccedillasmall", "Egravesmall", "Eacutesmall", "Ecircumflexsmall", "Edieresissmall", "Igravesmall",
  "Iacutesmall", "Icircumflexsmall", "Idieresissmall", "Ethsmall", "Ntildesmall", "Ogravesmall", "Oacutesmall", "Ocircumflexsmall",
  "Otildesmall", "Odieresissmall", "OEsmall", "Oslashsmall", "Ugravesmall", "Uacutesmall", "Ucircumflexsmall", "Udieresissmall",
  "Yacutesmall", "Thornsmall", "Ydieresissmall", "001.000", "001.001", "001.002", "001.003", "Black",
/* 384-390 */
  "Bold", "Book", "Light", "Medium", "Regular", "Roman", "Semibold"
} ;

/* -------------------------------------------------------------------------- */
/* cff_opensid opens a frame (returned in the frame pointer), and returns a
   string. If the SID is a standard string, the frame pointer will be NULL,
   but the string returned will be non-NULL. */
static uint8 *cff_opensidframe(CFF_CACHE *cff_cache, uint32 sid,
                               uint8 **frame, uint32 *length)
{
  uint8 *string = NULL ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT( CFF_data , "CFF_data NULL" ) ;
  HQASSERT( frame   , "frame   NULL" ) ;
  HQASSERT( length   , "length   NULL" ) ;

  if ( sid >= SID_STD_STRING_MAX ) {
    CFF_STRING *cff_string ;
    CFF_INDEX *stringIndex = CFF_data->stringIndex ;
    if ( ! stringIndex ) {
      (void)error_handler(RANGECHECK) ;
      return NULL ;
    }
    sid -= SID_STD_STRING_MAX ;
    if ( sid >= stringIndex->count ) {
      (void)error_handler(RANGECHECK) ;
      return NULL ;
    }
    cff_string = ( & stringIndex->strings[ sid ] ) ;
    if ( (string = cff_frame(cff_cache, cff_string->frame,
                             cff_string->length)) == NULL )
      return NULL ;
    *length = cff_string->length ;
    *frame = string ;
  } else {
    *frame = NULL ;
    string = (uint8 *)sid_stdstrings[sid] ;
    *length = strlen_uint32((char *)string) ;
  }

  return string ;
}

/* -------------------------------------------------------------------------- */
/* Appendix C: Predefined Charsets
 * Note can simply refer to standard SID table for the following which means
 * that we can use a 2 byte references as opposed to either a 4 byte pointer
 * or the strings themselves (which would be even longer).
 */

#define CHARSET_ISOADOBE_MAX 229
#define CHARSET_ISOADOBE_MAXCID (CHARSET_ISOADOBE_MAX - 1)

/* No table needed here since this is an exact subset of the standard SIDs. */

#define CHARSET_EXPERT_MAX ( sizeof( charset_expert ) / \
                             sizeof( charset_expert[ 0 ] ))
static uint16 charset_expert[] = {
  0,   1, 229, 230, 231, 232, 233, 234,
235, 236, 237, 238,  13,  14,  15, 99,
239, 240, 241, 242, 243, 244, 245, 246,
247, 248,  27,  28, 249, 250, 251, 252,
253, 254, 255, 256, 257, 258, 259, 260,
261, 262, 263, 264, 265, 266, 109, 110,
267, 268, 269, 270, 271, 272, 273, 274,
275, 276, 277, 278, 279, 280, 281, 282,
283, 284, 285, 286, 287, 288, 289, 290,
291, 292, 293, 294, 295, 296, 297, 298,
299, 300, 301, 302, 303, 304, 305, 306,
307, 308, 309, 310, 311, 312, 313, 314,
315, 316, 317, 318, 158, 155, 163, 319,
320, 321, 322, 323, 324, 325, 326, 150,
164, 169, 327, 328, 329, 330, 331, 332,
333, 334, 335, 336, 337, 338, 339, 340,
341, 342, 343, 344, 345, 346, 347, 348,
349, 350, 351, 352, 353, 354, 355, 356,
357, 358, 359, 360, 361, 362, 363, 364,
365, 366, 367, 368, 369, 370, 371, 372,
373, 374, 375, 376, 377, 378
} ;
#define CHARSET_EXPERT_MAXCID 378

#define CHARSET_EXPERTSUBSET_MAX ( sizeof( charset_expertsubset ) / \
                                   sizeof( charset_expertsubset[ 0 ] ))
static uint16 charset_expertsubset[] = {
  0,   1, 231, 232, 235, 236, 237, 238,
 13,  14,  15,  99, 239, 240, 241, 242,
243, 244, 245, 246, 247, 248,  27,  28,
249, 250, 251, 253, 254, 255, 256, 257,
258, 259, 260, 261, 262, 263, 264, 265,
266, 109, 110, 267, 268, 269, 270, 272,
300, 301, 302, 305, 314, 315, 158, 155,
163, 320, 321, 322, 323, 324, 325, 326,
150, 164, 169, 327, 328, 329, 330, 331,
332, 333, 334, 335, 336, 337, 338, 339,
340, 341, 342, 343, 344, 345, 346
} ;
#define CHARSET_EXPERTSUBSET_MAXCID 346

/* This table maps StandardEncoding to SIDs. This is used by the SEAC
   implementation to find a SID that is reverse-mapped through the charset to
   a GID. */
#define ENCODING_STANDARD_MAX ( sizeof( encoding_standard ) / \
                                sizeof( encoding_standard[ 0 ] ))
static uint16 encoding_standard[] = {
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  1,   2,   3,   4,   5,   6,   7,   8,
  9,  10,  11,  12,  13,  14,  15,  16,
 17,  18,  19,  20,  21,  22,  23,  24,
 25,  26,  27,  28,  29,  30,  31,  32,
 33,  34,  35,  36,  37,  38,  39,  40,
 41,  42,  43,  44,  45,  46,  47,  48,
 49,  50,  51,  52,  53,  54,  55,  56,
 57,  58,  59,  60,  61,  62,  63,  64,
 65,  66,  67,  68,  69,  70,  71,  72,
 73,  74,  75,  76,  77,  78,  79,  80,
 81,  82,  83,  84,  85,  86,  87,  88,
 89,  90,  91,  92,  93,  94,  95,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0,  96,  97,  98,  99, 100, 101, 102,
103, 104, 105, 106, 107, 108, 109, 110,
  0, 111, 112, 113, 114,   0, 115, 116,
117, 118, 119, 120, 121, 122,  0,  123,
  0, 124, 125, 126, 127, 128, 129, 130,
131,   0, 132, 133,   0, 134, 135, 136,
137,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,
  0, 138,   0, 139,   0,   0,   0,   0,
140, 141, 142, 143,   0,   0,   0,   0,
  0, 144,   0,   0,   0, 145,   0,   0,
  146, 147, 148, 149, 0,   0,   0,   0
} ;

static uint8 *cff_opencharsetframe(CFF_CACHE *cff_cache, uint32 index,
                                   uint8 **frame, uint32 *length)
{
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;
  HQASSERT( CFF_data , "CFF_data NULL" ) ;

  switch ( CFF_data->charset ) {
  case CFF_ISOADOBE:
    if ( index >= CHARSET_ISOADOBE_MAX )
      return NULL ;
    /* ISO encoding is a straight subset of SIDs */
    break ;
  case CFF_EXPERT:
    if ( index >= CHARSET_EXPERT_MAX )
      return NULL ;
    /* Map from expert charset index to SID */
    index = charset_expert[ index ] ;
    break ;
  case CFF_EXPERTSUBSET:
    if ( index >= CHARSET_EXPERTSUBSET_MAX )
      return NULL ;
    /* Map from expert subset charset index to SID */
    index = charset_expertsubset[ index ] ;
    break ;
  case CFF_EMBEDDED:
    if ( index >= CFF_data->charsetLength )
      return NULL ;
    /* Map from embedded charset index to SID */
    index = CFF_data->charsetNames[ index ] ;
    break ;
  default:
    HQFAIL( "Unknown charset format" ) ;
    return NULL ;
  }

  return cff_opensidframe(cff_cache, index, frame, length) ;
}

/* -------------------------------------------------------------------------- */
/* It takes too long to search for each CID individually. Create a mapping
   table of SIDs/CIDs to GIDs. This is used by cff_locatecidindex to get the
   GID quickly. */
static Bool cff_mapCIDtoGID(CFF_DATA *CFF_data)
{
  uint16 charsetLength ;
  uint32 maxCID, i ;
  uint16 *cidMapping, *charsetNames ;

  HQASSERT( CFF_data , "No CFF data" ) ;

  switch ( CFF_data->charset ) {
  case CFF_ISOADOBE:
    charsetLength = CHARSET_ISOADOBE_MAX ;
    charsetNames  = NULL ;
    maxCID        = CHARSET_ISOADOBE_MAXCID ;
    break ;
  case CFF_EXPERT:
    charsetLength = CHARSET_EXPERT_MAX ;
    charsetNames  = charset_expert ;
    maxCID        = CHARSET_EXPERT_MAXCID ;
    break ;
  case CFF_EXPERTSUBSET:
    charsetLength = CHARSET_EXPERTSUBSET_MAX ;
    charsetNames  = charset_expertsubset ;
    maxCID        = CHARSET_EXPERTSUBSET_MAXCID ;
    break ;
  case CFF_EMBEDDED:
    charsetLength = (uint16)CFF_data->charsetLength ;
    charsetNames  = CFF_data->charsetNames ;
    maxCID        = CFF_data->charsetMaxCID ;
    if ( charsetLength )
      break ;
    /* drop through */
  default:
    return error_handler(INVALIDFONT) ;
  }

#ifdef DEBUG_BUILD
  /* This checks that the *_MAXCID definitions are correct and the font isn't
     broken */
  if (charsetNames) {
    uint32 max = 0 ;
    for (i=0; i<charsetLength; ++i)
      if (charsetNames[i] > max)
        max = charsetNames[i] ;
    /* Only assert if maxCID is too small, we're not a font validator */
    HQASSERT(max <= maxCID, "Wrong maxCID in cff_mapCIDtoGID") ;
  }
#endif

  cidMapping = mm_alloc(mm_pool_temp, (maxCID + 1) * sizeof(uint16),
                        MM_ALLOC_CLASS_CFF_FDS) ;
  if ( !cidMapping )
    return error_handler(VMERROR) ;

  CFF_data->cidMapping = cidMapping ;
  CFF_data->cidMappingLength = maxCID + 1 ;

  for ( i = 0 ; i <= maxCID ; ++i )
    cidMapping[i] = CFF_INVALID_GID ;

  /* Only map the GIDs which exist; standard charsets may be used when there
     are less than that number of glyphs present, so long as the glyphs
     present are a subset of the standard charset. */
  if ( charsetLength > CFF_data->charStringsIndex->count )
    charsetLength = (uint16)CFF_data->charStringsIndex->count ;

  /* Map CID to lowest GID having this index. ISOAdobe is an identity
     mapping. */
  while ( charsetLength > 0 ) {
    uint16 cid = --charsetLength ;

    if ( charsetNames )
      cid = charsetNames[charsetLength] ;

    cidMapping[cid] = charsetLength ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Insert a PostScript version of an unpacked CFF object into a supplied
   dictionary */
static Bool cff_objectinsert(CFF_CACHE *cff_cache, CFF_OBJECT *cff_object,
                             OBJECT *dict)
{
  OBJECT psobj = OBJECT_NOTVM_NULL ; /* The object to insert */

  HQASSERT(cff_cache, "No CFF cache data") ;
  HQASSERT( cff_object , "cff_object NULL" ) ;
  HQASSERT( dict , "dict NULL" ) ;

  if ( !cff_object->found ) {
    if ( cff_object->defalt != NULL ) {
#if defined(ASSERT_BUILD)
      int32 ssize = theStackSize(operandstack) ;
#endif

      /* Set up the default object using run_ps_string. This is ugly, but
         easier than doing it all by hand. */
      if ( !run_ps_string((uint8 *)cff_object->defalt) )
        return FALSE ;

      HQASSERT(theStackSize(operandstack) == ssize + 1,
               "CFF default value PS bogus") ;
      Copy(&psobj, theTop(operandstack)) ;
      pop(&operandstack) ;
    } else /* No default value and no set value, nothing to do. */
      return TRUE ;
  } else {
    switch ( cff_object->type ) {
      uint32 index, length ;
      uint8 *string, *frame ;
      CFF_OBJECT *cff_array ;
    case CFF_SID:
      index = (uint32)cff_object->value.integer ;
      if ( (string = cff_opensidframe(cff_cache, index, &frame, &length)) == NULL )
        return FALSE ;

      if ( !ps_string(&psobj, string, length) )
        return FALSE ;

      break ;
    case CFF_BOOL:
      object_store_bool(&psobj, cff_object->value.integer) ;
      break ;
    case CFF_INT:
      object_store_integer(&psobj, cff_object->value.integer) ;
      break ;
    case CFF_REAL:
      object_store_real(&psobj, cff_object->value.real) ;
      break ;
    case CFF_ARRAY:
    case CFF_DELTA:
      if ( !ps_array(&psobj, cff_object->length) ||
           !object_access_reduce(READ_ONLY, &psobj) )
        return FALSE ;
      length = cff_object->length ;
      cff_array = cff_object->value.array ;
      for ( index = 0 ; index < length ; ++index ) {
        if ( cff_array[index].type == CFF_INT ) {
          object_store_integer(&oArray(psobj)[index], cff_array[index].value.integer) ;
        } else {
          HQASSERT(cff_array[index].type == CFF_REAL,
                   "CFF array element is not INT or REAL") ;
          object_store_real(&oArray(psobj)[index], cff_array[index].value.real) ;
        }
      }
      break ;
    default:
      HQFAIL("Unknown CFF object type") ;
      break ;
    }
  }

  oName(nnewobj) = cachename((uint8 *)cff_object->name,
                             strlen_int32(cff_object->name)) ;

  if ( !oName(nnewobj) ||
       !fast_insert_hash(dict, &nnewobj, &psobj) )
    return FALSE ;

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
static Bool cff_opfd_dict(CFF_CACHE *cff_cache, CFF_OPFD *cff_opfd,
                          OBJECT *dict)
{
  uint32 index ;

  HQASSERT(cff_cache, "No CFF cache data") ;

  for ( index = 0 ; ; ++index ) {
    CFF_OBJECT *cff_op ;
    int32 opnum = cff_opfd[index].opnum ;

    if ( opnum == CFF_OPFDEND )
      return TRUE ;

    if ( opnum == CFF_OP_SUBOP )
      cff_op = &cff_cache->cff_data.cff_subops[cff_opfd[index].opsubnum] ;
    else
      cff_op = &cff_cache->cff_data.cff_ops[opnum] ;

    if ( ! cff_objectinsert(cff_cache, cff_op, dict) )
      return FALSE ;
  }
  /* NOTREACHED */
}

static void cff_opfd_release(CFF_DATA *CFF_data, CFF_OPFD *cff_opfd)
{
  uint32 index ;

  for ( index = 0 ; ; ++index ) {
    CFF_OBJECT *cff_op, *cff_orig ;
    int32 opnum = cff_opfd[index].opnum ;

    if ( opnum == CFF_OPFDEND )
      return ;

    if ( opnum == CFF_OP_SUBOP ) {
      cff_op = &CFF_data->cff_subops[cff_opfd[index].opsubnum] ;
      cff_orig = &cff_subops[cff_opfd[index].opsubnum] ;
    } else {
      cff_op = &CFF_data->cff_ops[opnum] ;
      cff_orig = &cff_ops[opnum] ;
    }

    cff_releaseop(cff_op, cff_orig) ;
  }
  /* NOTREACHED */
}

/*---------------------------------------------------------------------------*/
static Bool cff_encoding(CFF_CACHE *cff_cache, OBJECT *encoding)
{
  int32 i ;
  uint32 *glyphSIDArray ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  HQASSERT(encoding, "No encoding object") ;

  glyphSIDArray = &cff_cache->cff_data.glyphSIDArray[0] ;

  if ( !ps_array(encoding, 256) ||
       !object_access_reduce(READ_ONLY, encoding) )
    return FALSE ;

  for ( encoding = oArray(*encoding), i = 0 ; i < 256 ; ++i, ++encoding ) {
    uint32 length = 0;
    uint32 glyph = glyphSIDArray[ i ] ;
    uint8 *string, *frame ;
    NAMECACHE *glyphName ;

    SwOftenUnsafe() ;

    if (( glyph & CFF_NOTASID ) != 0 ) {
      glyph &= ~CFF_NOTASID ;
      string = cff_opencharsetframe(cff_cache, glyph, &frame, &length) ;
    } else {
      string = cff_opensidframe(cff_cache, glyph, &frame, &length) ;
    }

    if ( string == NULL ) /* Couldn't find SID or charset index */
      return FALSE ;

    glyphName = cachename(string, length) ;

    if ( glyphName == NULL )
      return FALSE ;

    theTags(*encoding) = ONAME | LITERAL ;
    theLen(*encoding) = 0 ;
    oName(*encoding) = glyphName ;
    SETGLOBJECTTO(*encoding, FALSE) ;
  }

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/* Populate a charstrings dictionary. This maps from the charset names to
   GIDs. */
static Bool cff_charstrings(CFF_CACHE *cff_cache, OBJECT *dict)
{
  uint32 index, cslength ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;

  cslength = CFF_data->charStringsIndex->count ;
  if ( !ps_dictionary(dict, cslength) )
    return FALSE ;

  for ( index = 0 ; index < cslength ; ++index ) {
    uint8 *frame, *string ;
    uint32 length = 0 ;

    /* Map from GID to SID */
    if ( (string = cff_opencharsetframe(cff_cache, index,
                                        &frame, &length)) == NULL )
      return FALSE ;

    if (NULL == (oName(nnewobj) = cachename(string, length)))
      return FALSE;

    oInteger(inewobj) = (int32)index ;
    if ( !fast_insert_hash(dict, &nnewobj, &inewobj) )
      return FALSE ;
  }

  /* Finally, ensure that the notdef entry is 0 */
  oInteger(inewobj) = 0 ;
  return fast_insert_hash_name(dict, NAME_notdef, &inewobj) ;
}

/*--------------------------------------------------------------------------*/
/* Routines for constructing PostScript VM stubs for CFF fonts. There are
   separate routines to create stubs for CID and non-CID fonts. */
static Bool cff_makeFont(CFF_CACHE *cff_cache,
                         OBJECT *newfont, OBJECT *fontname,
                         OBJECT *encoding)
{
  OBJECT cfffont = OBJECT_NOTVM_NOTHING, fencod = OBJECT_NOTVM_NOTHING,
    fontinfo = OBJECT_NOTVM_NOTHING, charstrings = OBJECT_NOTVM_NOTHING ;
  CFF_OBJECT *cff_op ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;

  /* Create the CFF font dictionary */
  if ( !ps_dictionary(&cfffont, NUM_ARRAY_ITEMS(cff_font_ops) + 10) )
    return FALSE ;

  if ( !fast_insert_hash_name(&cfffont, NAME_FontName, fontname) )
    return FALSE ;

  if (!encoding) {

    /* Do SOMETHING that can be spotted in get_cdef [12764] */
    theTags(fencod) = OARRAY | EXECUTABLE | READ_ONLY ;
    theLen(fencod) = 0 ;
    oCPointer(fencod) = &onothing ; /* some mutually agreed pointer */
    encoding = &fencod ;

  } else {

    /* If the encoding is not overridden, use the one in the current font. */
    if ( oType(*encoding) == ONULL ) {
      cff_op = ( & CFF_data->cff_ops[ CFF_OP_ENCODING ] ) ;
      /* Note we don't contain any Encoding vectors in this filter, or put any
         standard ones out. It is assumed that StandardEncoding exists (since
         it is part of the Level II spec) and that ExpertEncoding is correctly
         defined as a Level II resource. */
      if ( cff_op->found ) {
        switch ( cff_op->value.integer ) {
        case 0:
          object_store_name(&fencod, NAME_StandardEncoding, LITERAL) ;
          break ;
        case 1:
          object_store_name(&fencod, NAME_ExpertEncoding, LITERAL) ;
          break ;
        default:
          if ( !cff_encoding(cff_cache, &fencod) )
            return FALSE ;
          break ;
        }
      } else {
        object_store_name(&fencod, NAME_StandardEncoding, LITERAL) ;
      }

      encoding = &fencod ;
    }
  }

  if ( oType(*encoding) == ONAME ) {
    /* N.B. nnewobje going on execstack, nnewobj on operandstack */
    oName(nnewobj) = &system_names[NAME_Encoding];
    oName(nnewobje) = &system_names[NAME_findresource];

    if ( !interpreter_clean(&nnewobje, encoding, &nnewobj, NULL) )
      return FALSE ;

    Copy(encoding, theTop(operandstack)) ;
    pop(&operandstack);

    if ( oType(*encoding) != OARRAY && oType(*encoding) != OPACKEDARRAY )
      return error_handler(TYPECHECK);
  }

  HQASSERT(oType(*encoding) == OARRAY || oType(*encoding) == OPACKEDARRAY,
           "Encoding is not an array") ;

  if ( theLen(*encoding) != 256
       && oCPointer(*encoding) != &onothing ) /* PDF CID CFF [12764] */
    return error_handler(RANGECHECK);

  if ( !fast_insert_hash_name(&cfffont, NAME_Encoding, encoding) )
    return FALSE ;

  /* Add other top-level items: UniqueID, FontBBox, XUID, PaintType,
     CharStringType, FontMatrix, StrokeWidth. */
  CFF_data->cff_subops[CFF_SUBOP_FONTMATRIX].defalt = CFF_DEFAULT_FONTMATRIX ;
  if ( !cff_opfd_dict(cff_cache, cff_font_ops, &cfffont) )
    return FALSE ;

  /* Set the FontType to make sure it uses the CFF routines. The charstring
     type will be set when the character is looked up. */
  oInteger(inewobj) = 2 ;
  if ( !fast_insert_hash_name(&cfffont, NAME_FontType, &inewobj) )
    return FALSE ;

  /* Create FontInfo sub-dictionary */
  if ( !ps_dictionary(&fontinfo, NUM_ARRAY_ITEMS(cff_fontinfo_ops)) )
    return FALSE ;

  if ( !cff_opfd_dict(cff_cache, cff_fontinfo_ops, &fontinfo) )
    return FALSE ;

  if ( !object_access_reduce(READ_ONLY, &fontinfo) ||
       !fast_insert_hash_name(&cfffont, NAME_FontInfo, &fontinfo) )
    return FALSE ;

  /* /CharStrings is a dictionary lookup from names to glyph IDs. */
  if ( !cff_charstrings(cff_cache, &charstrings) ||
       !object_access_reduce(READ_ONLY, &charstrings) ||
       !fast_insert_hash_name(&cfffont, NAME_CharStrings, &charstrings) )
    return FALSE ;

  Copy(newfont, &cfffont) ;

  return TRUE ;
}

static uint16 cff_locatecidindex(CFF_DATA *CFF_data , int32 cid)
{
  HQASSERT(CFF_data, "No CFF data") ;
  HQASSERT(cid >= 0, "CID negative when looking for GID") ;

  /* Lookup CID in CID to GID table */
  if ( (uint32)cid < CFF_data->cidMappingLength )
    return CFF_data->cidMapping[cid] ;

  return CFF_INVALID_GID ;
}

static uint16 cff_locatecidfd( CFF_DATA *CFF_data , int32 cid )
{
  HQASSERT(CFF_data, "No CFF data") ;
  HQASSERT(cid >= 0, "CID negative when looking for FD") ;

  if ( CFF_data->fdSelect && (uint32)cid < CFF_data->fdSelectLength )
    return CFF_data->fdSelect[cid] ;

  return 0 ;
}

static Bool cff_makeCIDFont(CFF_CACHE *cff_cache,
                            OBJECT *newfont, OBJECT *fontname)
{
  OBJECT cfffont = OBJECT_NOTVM_NOTHING, fdarray = OBJECT_NOTVM_NOTHING,
    fontinfo = OBJECT_NOTVM_NOTHING, cidsysteminfo = OBJECT_NOTVM_NOTHING ;
  CFF_OBJECT *cff_op ;
  CFF_INDEX *cff_index ;
  uint32 index ;
  CFF_DATA *CFF_data ;

  HQASSERT(cff_cache, "No CFF cache data") ;
  CFF_data = &cff_cache->cff_data ;

  /* Create the CFF font dictionary */
  if ( !ps_dictionary(&cfffont,
                      NUM_ARRAY_ITEMS(cff_cid_ops1) +
                      NUM_ARRAY_ITEMS(cff_font_ops) + 13) )
    return FALSE ;

  if ( !fast_insert_hash_name(&cfffont, NAME_CIDFontName, fontname) )
    return FALSE ;

  /* Top level dict for CIDFont */
  if ( !cff_opfd_dict(cff_cache, cff_cid_ops1, &cfffont) )
    return FALSE ;

  /* The spec is unclear as to whether FontMatrix must/can exist in both
     top-level and FDArray dictionaries, so we'll take a pragmatic approach
     and cover all possibilities; if there are FDArray matrices but not a
     top-level one, leave the top-level empty, using the normal CID Type 0
     font rules on defineresource to scale the FDArray matrices (PLRM3
     p.374). If there is a top level FontMatrix but no FDArray matrices,
     assume the FDArray ones are identity. If there are both, they are both
     used. If there are neither, then the default for the FDArray is 1000
     units to the EM and the top level is empty, which will invoked the
     normal rules to re-scale them. */
  CFF_data->cff_subops[CFF_SUBOP_FONTMATRIX].defalt = NULL ;
  if ( !cff_opfd_dict(cff_cache, cff_font_ops, &cfffont) )
    return FALSE ;

  /* CIDSystemInfo sub-dictionary */
  if ( !ps_dictionary(&cidsysteminfo, 3) )
    return FALSE ;

  cff_op = ( & CFF_data->cff_subops[ CFF_SUBOP_ROS ] ) ;
  HQASSERT( cff_op->found , "Doing CID Font but no ROS" ) ;
  HQASSERT( cff_op->length == 3 , "ROS length not 3" ) ;
  cff_op = cff_op->value.array ;
  HQASSERT( cff_op != NULL , "ROS array NULL" ) ;

  for ( index = 0 ; index < 3 ; ++index ) {
    cff_op[ index ].name = cid_ros[ index ] ;

    if ( index < 2 )
      cff_op[ index ].type = CFF_SID ;

    if ( ! cff_objectinsert(cff_cache, &cff_op[index], &cidsysteminfo) )
      return FALSE ;
  }

  if ( !object_access_reduce(READ_ONLY, &cidsysteminfo) ||
       !fast_insert_hash_name(&cfffont, NAME_CIDSystemInfo, &cidsysteminfo) )
    return FALSE ;

  /* Create FontInfo sub-dictionary */
  if ( !ps_dictionary(&fontinfo, NUM_ARRAY_ITEMS(cff_fontinfo_ops)) )
    return FALSE ;

  if ( !cff_opfd_dict(cff_cache, cff_fontinfo_ops, &fontinfo) )
    return FALSE ;

  if ( !object_access_reduce(READ_ONLY, &fontinfo) ||
       !fast_insert_hash_name(&cfffont, NAME_FontInfo, &fontinfo) )
    return FALSE ;

  /* Fill in some CIDFontType 0 fields, because PLRM3 says they are required.
     The values won't be used. */
  oInteger(inewobj) = 0 ;
  if ( !fast_insert_hash_name(&cfffont, NAME_CIDMapOffset, &inewobj) ||
       !fast_insert_hash_name(&cfffont, NAME_GlyphData, &inewobj) )
    return FALSE ;

  oInteger(inewobj) = CFF_data->fdbytes ;
  if ( !fast_insert_hash_name(&cfffont, NAME_FDBytes, &inewobj) )
    return FALSE ;

  oInteger(inewobj) = CFF_data->gdbytes ;
  if ( !fast_insert_hash_name(&cfffont, NAME_GDBytes, &inewobj) )
    return FALSE ;

  /* Create FDArray and populate with font dictionaries */
  cff_index = CFF_data->fontDictIndex ;
  if ( !ps_array(&fdarray, cff_index->count) )
    return FALSE ;

  for ( index = 0 ; index < cff_index->count ; ++index ) {
    OBJECT *subfont = &oArray(fdarray)[index] ;

    if ( !cff_initSubFont(cff_cache, index) )
      return FALSE ;

    /* Create a font dictionary in FDArray */
    if ( !ps_dictionary(subfont, NUM_ARRAY_ITEMS(cff_font_ops) + 2) )
      return FALSE ;

    CFF_data->cff_subops[CFF_SUBOP_FONTMATRIX].defalt = CFF_DEFAULT_FONTMATRIX ;
    if ( CFF_data->gotFontMatrix )
      CFF_data->cff_subops[CFF_SUBOP_FONTMATRIX].defalt = CFF_IDENTITY_FONTMATRIX ;

    if ( !cff_opfd_dict(cff_cache, cff_font_ops, subfont) )
      return FALSE ;

    if ( !ps_dictionary(&fontinfo, NUM_ARRAY_ITEMS(cff_fontinfo_ops)) )
      return FALSE ;

    if ( !cff_opfd_dict(cff_cache, cff_fontinfo_ops, &fontinfo) )
      return FALSE ;

    if ( !object_access_reduce(READ_ONLY, &fontinfo) ||
         !fast_insert_hash_name(subfont, NAME_FontInfo, &fontinfo) )
      return FALSE ;

    if ( !object_access_reduce(READ_ONLY, subfont) )
      return FALSE ;
  }

  if ( !object_access_reduce(READ_ONLY, &fdarray) ||
       !fast_insert_hash_name(&cfffont, NAME_FDArray, &fdarray) )
    return FALSE ;

  Copy(newfont, &cfffont) ;

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/* Define the PostScript VM wrapper for a CFF font directly. Global/local
   mode should already be set when this is called. This replaces the old
   DecodeType1C filter. */
static Bool cff_definefont_internal(OBJECT *newfont, OBJECT *subfont,
                                    OBJECT *fontname, OBJECT *encoding,
                                    int32 isPDF, CFF_CACHE *cff_cache)
{
  OBJECT foundname = OBJECT_NOTVM_NOTHING ;
  uint32 index ;
  int32 fontindex ;
  CFF_DATA *cff_data ;
  CFF_INDEX *nameIndex ;
  int32 defined = 0 ;

  HQASSERT(newfont, "No CFF font object") ;
  HQASSERT(cff_cache->source, "No data source") ;
  HQASSERT(oType(*cff_cache->source) == OFILE,
           "CFF source object is not OFILE") ;
  HQASSERT(cff_cache, "No CFF cache data") ;
  cff_data = &cff_cache->cff_data ;

  nameIndex = cff_data->nameIndex ;
  HQASSERT(nameIndex, "No CFF name index") ;

  /* If there are multiple fonts, and we want to override the name, but we
     haven't selected one to override, then we shouldn't proceed (we'd
     unhelpfully define them all with the same name). */
  if ( nameIndex->count > 1 && subfont == NULL && fontname != NULL )
    return error_handler(UNDEFINED) ;

  if ( subfont != NULL )
    OCopy(foundname, *subfont) ;

  /* Search the CFF fontset for the font we want. CFF fonts may be deleted
     from a fontset without removing the name entry by setting the first
     byte of the name to zero, so maintain a separate index of the fonts
     found. */
  for ( fontindex = 0, index = 0 ; index < nameIndex->count ; ++index ) {
    OBJECT cfffont = OBJECT_NOTVM_NOTHING ;
    Bool is_cid = FALSE ;

    if ( subfont == NULL )
      object_store_integer(&foundname, fontindex) ;

    if ( !cff_matchName(cff_cache, &nameIndex->strings[index],
                        isPDF, &foundname, &fontindex) )
      return FALSE ;

    HQASSERT(fontindex >= 0 && (uint32)fontindex < nameIndex->count,
             "Font index is out of range") ;

    /* If we did not match the name, try the next one. */
    if ( oType(foundname) == ONULL )
      continue ;

    HQASSERT(oType(foundname) == ONAME || oType(foundname) == OSTRING,
             "Font name found is wrong type") ;

    if ( !cff_initFont(cff_cache, fontindex) )
      return FALSE ;

    is_cid = (cff_data->fonttype == CFF_CIDFONT) ;
    HQASSERT(cff_data->fonttype == CFF_FONT ||
             cff_data->fonttype == CFF_CIDFONT,
             "CFF font is not a plain or CID font") ;

    if ( !is_cid ) {
      if (isPDF == 2)
        encoding = 0 ;
      if ( !cff_makeFont(cff_cache, &cfffont, &foundname, encoding) )
        return FALSE ;
    } else {
      if ( !cff_makeCIDFont(cff_cache, &cfffont, &foundname) )
        return FALSE ;
    }

    oInteger(inewobj) = (int32)cff_cache->offset ;
    if ( !fast_insert_hash_name(&cfffont, NAME_FontOffset, &inewobj) ||
         !fast_insert_hash_name(&cfffont, NAME_FontFile, cff_cache->source) )
      return FALSE ;

    /* Put the font index in the collection. */
    oInteger(inewobj) = fontindex ;
    if ( !fast_insert_hash_name(&cfffont, NAME_SubFont, &inewobj) )
      return FALSE ;

    /* Find a way if indicating that this font is a CFF rather than a normal
       CID Type 0 */
    oInteger(inewobj) = 2 ;
    if ( !fast_insert_hash_name(&cfffont, NAME_FontType, &inewobj) )
      return FALSE ;

    /* Execute "<name> dict /(CID)Font defineresource" through PostScript. */

    /* N.B. nnewobje going on execstack, nnewobj on operandstack */
    oName(nnewobj) = &system_names[is_cid ? NAME_CIDFont : NAME_Font];
    oName(nnewobje) = &system_names[NAME_defineresource];

    if ( !interpreter_clean(&nnewobje,
                            fontname ? fontname : &foundname,
                            &cfffont, &nnewobj, NULL) )
      return FALSE ;

    Copy(newfont, theTop(operandstack)) ;
    pop(&operandstack) ;

    ++defined ;

    /* If we were requested to find a specific font, we're done. Otherwise,
       search for and define the next font in the CFF font set. */
    if ( subfont != NULL )
      return TRUE ;

    /* Free all except the top indices, which we can re-use. */
    cff_freeFont(&cff_cache->cff_data, FALSE /*freeAll*/) ;
  }

  /* If we didn't define any fonts, throw an error. */
  if ( defined == 0 )
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

Bool cff_definefont(OBJECT *newfont, OBJECT *params, OBJECT *cfffile)
{
  Bool result ;
  int32 isPDF = 0 ;
  FILELIST *flptr ;
  CFF_CACHE cff_cache ;
  OBJECT *fontname = NULL, *subfont = NULL ;
  OBJECT encoding = OBJECT_NOTVM_NULL ;

  enum { cff_match_SubFont, cff_match_PDF, cff_match_Encoding,
         cff_match_FontOffset, cff_match_FontName, cff_match_CIDFont,
         cff_match_dummy } ;
  static NAMETYPEMATCH cff_match[cff_match_dummy + 1] = {
    /* Use the enum above to index this dictmatch */
    { NAME_SubFont | OOPTIONAL, 5, { ONAME, OINTEGER, OSTRING, OARRAY, OPACKEDARRAY }},
    { NAME_PDF      | OOPTIONAL, 2, { OBOOLEAN, OINTEGER }},
    { NAME_Encoding | OOPTIONAL, 3, { ONAME, OARRAY, OPACKEDARRAY }},
    { NAME_FontOffset | OOPTIONAL, 1, { OINTEGER }},
    { NAME_FontName | OOPTIONAL, 2, { ONAME, OSTRING }},
    { NAME_CIDFont  | OOPTIONAL, 1, { OBOOLEAN }},
    DUMMY_END_MATCH
  };

  HQASSERT(cfffile, "No CFF file object") ;

  if ( oType(*cfffile) != OFILE )
    return error_handler(INVALIDFONT) ;

#ifdef ASSERT_BUILD
  { int32 i;
    HQASSERT( ENCODING_STANDARD_MAX == NUM_ARRAY_ITEMS(StandardEncoding),
              "encoding_standard length problem in cff" );
    for ( i = 0; i < ENCODING_STANDARD_MAX; i++ ) {
      int32 len = strlen_int32(sid_stdstrings[encoding_standard[i]]);
      if (strncmp( (char *)StandardEncoding[i]->clist,
                   sid_stdstrings[encoding_standard[i]], len ) != 0) {
        HQFAIL( "Standard encoding mis-match in cff" );
        break;
      }
    }
  }
#endif


  /* Check the input TT file. It must be an input file, and must be
     seekable. */
  flptr = oFile(*cfffile) ;
  if ( !isIOpenFile(flptr) || !isIInputFile(flptr) || !file_seekable(flptr) )
    return error_handler(IOERROR) ;

  /* Initialise all of the CFF cache fields for safety */
  cff_cache.fid = 0 ;
  cff_cache.source = cfffile ;
  cff_cache.offset = 0 ;
  cff_cache.font_data = NULL ;
  cff_cache.fdmethods = &blobdata_file_methods ;
  cff_cache.next = NULL ;

  cff_cache.cffmethods = cff_charstring_fns ;
  cff_cache.cffmethods.data = &cff_cache ;

  NAME_OBJECT(&cff_cache, CFF_CACHE_NAME) ;

  if ( params != NULL ) {
    if ( oType(*params) != ODICTIONARY ||
         !dictmatch(params, cff_match) )
      return error_handler(TYPECHECK) ;

    subfont = cff_match[cff_match_SubFont].result ;
    fontname = cff_match[cff_match_FontName].result ;

    if ( cff_match[cff_match_PDF].result ) {
      if (oType(*cff_match[cff_match_PDF].result) == OBOOLEAN)
        isPDF = (oBool(*cff_match[cff_match_PDF].result) == TRUE) ? 1 : 0 ;
      else
        isPDF = oInteger(*cff_match[cff_match_PDF].result) ;
    }

    if ( cff_match[cff_match_CIDFont].result &&
         oBool(*cff_match[cff_match_CIDFont].result) ) {
      isPDF |= 2 ; /* [12764] Yes, it's really a CID */
      HQASSERT(isPDF == 2, "Unexpected /PDF /CIDFont interaction") ;
    }

    if ( cff_match[cff_match_Encoding].result )
      Copy(&encoding, cff_match[cff_match_Encoding].result) ;
    if ( cff_match[cff_match_FontOffset].result ) {
      int32 offset = oInteger(*cff_match[cff_match_FontOffset].result) ;
      if ( offset < 0 )
        return error_handler(RANGECHECK) ;
      cff_cache.offset = (uint32)offset ;
    }
  }

  HQASSERT(!error_signalled(), "Starting font while in error condition");
  error_clear_newerror();

  /* If this succeeds, we MUST call fontdata_close. */
  if ( (cff_cache.font_data = fontdata_open(cff_cache.source,
                                            cff_cache.fdmethods)) == NULL )
    return FALSE ;

  /* After this, must cleanup on exit by calling cff_freeFont */
  result = (cff_initFontSet(&cff_cache) &&
            cff_definefont_internal(newfont, subfont, fontname,
                                    &encoding, isPDF, &cff_cache)) ;

  cff_freeFont(&cff_cache.cff_data, TRUE /*freeAll*/) ;
  fontdata_close(&cff_cache.font_data) ;

  UNNAME_OBJECT(&cff_cache) ;

  /* Allow CFF internal routines to return FALSE, defaulting to INVALIDFONT. */
  if ( !result && !newerror )
    result = error_handler(INVALIDFONT) ;

  return result ;
}

/*---------------------------------------------------------------------------*/
/* PostScript operator to expose cff_definefont for findfont. This operator
   takes a file or a file and a parameter dictionary, and defines a CFF
   font stub that uses the file in its /FontFile entry. */
Bool definecfffont_(ps_context_t *pscontext)
{
  OBJECT newfont = OBJECT_NOTVM_NOTHING ;
  OBJECT *fonto, *params = NULL ;
  int32 pop_args = 0 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  fonto = theTop(operandstack) ;
  if ( oType(*fonto) == ODICTIONARY ) {
    if ( theStackSize(operandstack) < 1 )
      return error_handler(STACKUNDERFLOW) ;

    params = fonto ;
    pop_args = 1 ;
    fonto = stackindex(1, &operandstack) ;
  }

  if ( oType(*fonto) != OFILE )
    return error_handler( TYPECHECK ) ;

  if ( !cff_definefont(&newfont, params, fonto) )
    return FALSE ;

  /* Return new font on stack*/
  Copy(fonto, &newfont) ;

  if ( pop_args > 0 )
    npop(pop_args, &operandstack) ;

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/* CFF structure cache routines. These are used to prevent the CFF code from
   having to reload the CFF font data for every charstring. At the moment,
   they only allow one CFF to be loaded. The CFF cache routines use the common
   font data cache to store charstrings routines. The font data pointer is
   not retained between characters; a new instance is opened for each
   character. */
static CFF_CACHE *cff_font_cache = NULL ;

/* Somewhat spuriously, we don't store the allocation size of each CFF font.
   We may do so in future, so long as we have a quick way of determining how
   large retained fonts are. */


/** Low-memory handling data for the CFF font cache. */
mm_simple_cache_t cff_mem_cache;


/** The size of the entire CFF font cache. */
#define cff_cache_size (cff_mem_cache.data_size)


/* Create a cache entry for the font in question, and initialise the fontdata
   and font set. */
static CFF_CACHE *cff_set_font(FONTinfo *fontInfo)
{
  CFF_CACHE *cff_font, **cff_prev = &cff_font_cache ;
  int32 fid ;

  HQASSERT(fontInfo, "No font info") ;
  fid = theCurrFid(*fontInfo) ;

  /* Search for a matching entry in the CFF data cache */
  while ( (cff_font = *cff_prev) != NULL ) {
    VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;

    /* If this entry was trimmed by a GC scan, remove it */
    if ( cff_font->source == NULL ) {
      *cff_prev = cff_font->next ;
      HQASSERT(cff_font->font_data == NULL,
               "CFF font lost source (GC scan?) but has fontdata open") ;
      cff_freeFont(&cff_font->cff_data, TRUE) ;
      UNNAME_OBJECT(cff_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)cff_font, sizeof(CFF_CACHE)) ;
      cff_cache_size -= sizeof(CFF_CACHE) ;
    } else if ( fid == cff_font->fid ) {
      /* Move entry to head of MRU list */
      *cff_prev = cff_font->next ;
      break ;
    } else {
      cff_prev = &cff_font->next ;
    }
  }

  /* If no CFF cache found, allocate a new one and initialise the correct
     font entry in it. The object is copied into new object memory, because
     we don't know where the pointer came from. It could be a pointer from
     the C, PostScript or graphics stack, in which case its contents will
     change unpredictably. */
  if ( !cff_font ) {
    uint32 fontindex = 0, fontoffset = 0 ;
    OBJECT *fdict = &theMyFont(*fontInfo) ;
    OBJECT *fontfile, *theo ;

    /* FontFile specifies the source of font data. */
    if ( (fontfile = fast_extract_hash_name(fdict, NAME_FontFile)) == NULL ||
         oType(*fontfile) != OFILE ) {
      (void)error_handler(INVALIDFONT) ;
      return NULL ;
    }

    /* FontOffset specifies the starting offset of the font data within the
       file. */
    if ( (theo = fast_extract_hash_name(fdict, NAME_FontOffset)) != NULL ) {
      if ( oType(*theo) != OINTEGER || oInteger(*theo) < 0 ) {
        (void)error_handler(INVALIDFONT) ;
        return NULL ;
      }
      fontoffset = (uint32)oInteger(*theo) ;
    }

    /* SubFont specifies the font index within the CFF structure. */
    if ( (theo = fast_extract_hash_name(fdict, NAME_SubFont)) != NULL ) {
      if ( oType(*theo) != OINTEGER || oInteger(*theo) < 0 ) {
        (void)error_handler(INVALIDFONT) ;
        return NULL ;
      }
      fontindex = (uint32)oInteger(*theo) ;
    }

    /* Don't mind if mm_alloc fails after get_lomemory, the object memory will
       be returned by a restore or GC. */
    if ( (theo = get_lomemory(1)) == NULL ||
         (cff_font = mm_alloc(mm_pool_temp, sizeof(CFF_CACHE),
                              MM_ALLOC_CLASS_CID_DATA)) == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    Copy(theo, fontfile) ;
    cff_font->fid = fid ;
    cff_font->offset = fontoffset ;
    cff_font->source = theo ;
    cff_font->fdmethods = &blobdata_file_methods ;
    cff_font->cffmethods = cff_charstring_fns ;
    cff_font->cffmethods.data = cff_font ;
    NAME_OBJECT(cff_font, CFF_CACHE_NAME) ;

    if ( (cff_font->font_data = fontdata_open(cff_font->source,
                                              cff_font->fdmethods)) == NULL ) {
      UNNAME_OBJECT(cff_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)cff_font, sizeof(CFF_CACHE)) ;
      return NULL ;
    }

    if ( !cff_initFontSet(cff_font) ||
         !cff_initFont(cff_font, fontindex) ) {
      cff_freeFont(&cff_font->cff_data, TRUE) ;
      fontdata_close(&cff_font->font_data) ;
      UNNAME_OBJECT(cff_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)cff_font, sizeof(CFF_CACHE)) ;
      return NULL ;
    }

    fontdata_close(&cff_font->font_data) ;

    cff_cache_size += sizeof(CFF_CACHE) ;
  }

  cff_font->next = cff_font_cache ;
  cff_font_cache = cff_font ;

  return cff_font ;
}

/* Clean out cache of CFF fonts */
void cff_restore(int32 savelevel)
{
  CFF_CACHE *cff_font, **cff_prev = &cff_font_cache ;
  int32 numsaves = NUMBERSAVES(savelevel) ;

  while ( (cff_font = *cff_prev) != NULL ) {
    VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;

    /* Test if the data source will be restored */
    if ( cff_font->source == NULL ||
         mm_ps_check(numsaves, cff_font->source) != MM_SUCCESS ) {
      *cff_prev = cff_font->next ;

      HQASSERT(cff_font->font_data == NULL,
               "Font data open when purging CFF font") ;
      cff_freeFont(&cff_font->cff_data, TRUE) ;
      mm_free(mm_pool_temp, (mm_addr_t)cff_font, sizeof(CFF_CACHE)) ;
      cff_cache_size -= sizeof(CFF_CACHE) ;
    } else {
      cff_prev = &cff_font->next ;
    }
  }
}


/* GC scanning for CFF cache. I would prefer to have a hook to finalisation,
   so we can delete the cache entry when the object is GC'ed. */
static mps_res_t MPS_CALL cff_scan(mps_ss_t ss, void *p, size_t s)
{
  CFF_CACHE *cff_font ;

  UNUSED_PARAM(void *, p);
  UNUSED_PARAM(size_t, s);

  MPS_SCAN_BEGIN( ss )
    for ( cff_font = cff_font_cache ; cff_font ; cff_font = cff_font->next ) {
      VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;

      /* If we're GC scanning, we are probably in a low-memory situation.
         Mark this font entry as freeable if it's not in use, fix the source
         pointer if it is. The MPS is not reentrant, so we can't actually
         free it now. */
      if ( cff_font->font_data == NULL )
        MPS_SCAN_UPDATE( cff_font->source, NULL );
      else
        /* Fix the font data source objects, so they won't be collected. */
        MPS_RETAIN( &cff_font->source, TRUE );
    }
  MPS_SCAN_END( ss );

  return MPS_RES_OK ;
}


/** \brief Clear a given quantity of data from the DLD font cache.

    \param purge  The amount to purge.
    \return  The amount purged.

  This won't touch data currently in use, so it may fail to clear as
  much as requested.
 */
static size_t cff_purge(size_t purge)
{
  CFF_CACHE *cff_font, **cff_prev = &cff_font_cache ;
  size_t orig_size = cff_cache_size, level;

  level = orig_size - purge;
  cff_cache_size = 0 ;

  while ( (cff_font = *cff_prev) != NULL ) {
    VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;

    if ( cff_font->font_data == NULL &&
         (cff_cache_size >= level || cff_font->source == NULL) ) {
      *cff_prev = cff_font->next ;

      cff_freeFont(&cff_font->cff_data, TRUE) ;
      UNNAME_OBJECT(cff_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)cff_font, sizeof(CFF_CACHE)) ;
    } else {
      cff_cache_size += sizeof(CFF_CACHE) ;
      cff_prev = &cff_font->next ;
    }
  }

  HQASSERT((cff_cache_size == 0) == (cff_font_cache == NULL) &&
           orig_size >= cff_cache_size,
           "Inconsistent CFF cache size") ;

  return orig_size - cff_cache_size ;
}


void cff_cache_clear(void)
{
  (void)cff_purge(cff_mem_cache.data_size);
}


/** Purge method for \c cff_mem_cache. */
static Bool cff_mem_purge(mm_simple_cache_t *cache,
                          Bool *purged_something, size_t purge)
{
  UNUSED_PARAM(mm_simple_cache_t *, cache);
  *purged_something = cff_purge(purge) != 0;
  return TRUE;
}


/*---------------------------------------------------------------------------*/
/* CFF charstring access routines for Type 1/2 charstrings. */

static Bool cff_get_info(void *data, int32 nameid, int32 index, OBJECT *info)
{
  CFF_CACHE *cff_font = data ;
  CFF_OBJECT *cff_op = NULL ;
  CFF_OBJECT *cff_ops, *cff_subops ;

  VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;
  HQASSERT(info, "No data object") ;

  object_store_null(info) ;

  cff_ops = cff_font->cff_data.cff_ops ;
  cff_subops = cff_font->cff_data.cff_subops ;

  switch ( nameid ) {
  case NAME_WeightVector:
  case NAME_NormalizedDesignVector:
    /* Arrays, but where are they from? */
    return TRUE ;

    /* Subops: */
  case NAME_BlueScale:
    cff_op = &cff_subops[CFF_SUBOP_BLUESCALE] ;
    break ;
  case NAME_BlueShift:
    cff_op = &cff_subops[CFF_SUBOP_BLUESHIFT] ;
    break ;
  case NAME_BlueFuzz:
    cff_op = &cff_subops[CFF_SUBOP_BLUEFUZZ] ;
    break ;
  case NAME_StemSnapH:
    cff_op = &cff_subops[CFF_SUBOP_STEMSNAPH] ;
    break ;
  case NAME_StemSnapV:
    cff_op = &cff_subops[CFF_SUBOP_STEMSNAPV] ;
    break ;
  case NAME_ForceBold:
    cff_op = &cff_subops[CFF_SUBOP_FORCEBOLD] ;
    break ;
  case NAME_ForceBoldThreshold:
    cff_op = &cff_subops[CFF_SUBOP_FORCEBOLDTHRESHOLD] ;
    break ;
  case NAME_lenIV:
    /* TN5176 (18 Mar 98), p.19 says -1 is the only lenIV value supported.
       lenIV is not mentioned by TN5176 (16 Mar 00). Default the value to
       -1 in case we have Type 1 charstrings inside a CFF wrapper, but allow
       a real operator to override it. */
    object_store_integer(info, -1) ;
    cff_op = &cff_subops[CFF_SUBOP_LENIV] ;
    break ;
  case NAME_LanguageGroup:
    cff_op = &cff_subops[CFF_SUBOP_LANGUAGEGROUP] ;
    break ;
  case NAME_ExpansionFactor:
    cff_op = &cff_subops[CFF_SUBOP_EXPANSIONFACTOR] ;
    break ;
  case NAME_initialRandomSeed:
    cff_op = &cff_subops[CFF_SUBOP_INITIALRANDOMSEED] ;
    break ;
  case NAME_UserDesignVector:
    cff_op = &cff_subops[CFF_SUBOP_BASEFONTBLEND] ;
    break ;
  case NAME_lenBuildCharArray:
    cff_op = &cff_subops[CFF_SUBOP_LENBUILDCHARARRAY] ;
    break ;

    /* Top dict operators */
  case NAME_BlueValues:
    cff_op = &cff_ops[CFF_OP_BLUEVALUES] ;
    break ;
  case NAME_OtherBlues:
    cff_op = &cff_ops[CFF_OP_OTHERBLUES] ;
    break ;
  case NAME_FamilyBlues:
    cff_op = &cff_ops[CFF_OP_FAMILYBLUES] ;
    break ;
  case NAME_FamilyOtherBlues:
    cff_op = &cff_ops[CFF_OP_FAMILYOTHERBLUES] ;
    break ;
  case NAME_StdHW:
    cff_op = &cff_ops[CFF_OP_STDHW] ;
    break ;
  case NAME_StdVW:
    cff_op = &cff_ops[CFF_OP_STDVW] ;
    break ;
  case NAME_UniqueID:
    cff_op = &cff_ops[CFF_OP_UNIQUEID] ;
    break ;
  case NAME_XUID:
    cff_op = &cff_ops[CFF_OP_XUID] ;
    break ;
  case NAME_defaultWidthX:
    cff_op = &cff_ops[CFF_OP_DEFAULTWIDTHX] ;
    break ;
  case NAME_nominalWidthX:
    cff_op = &cff_ops[CFF_OP_NOMINALWIDTHX] ;
    break ;

    /* Special */
  case NAME_FID:
    object_store_integer(info, cff_font->fid) ;
    return TRUE ;
  case NAME_SubFont:
    object_store_integer(info, cff_font->cff_data.fdindex) ;
    return TRUE ;

    /* Ignored keys */
  case NAME_RndStemUp: case NAME_OtherSubrs:
    return TRUE ;
  default:
    HQFAIL( "Unknown key to look up" ) ;
    return FALSE ;
  }

  HQASSERT(cff_op, "No CFF object found for info") ;
  if ( cff_op->found ) {
    if ( cff_op->type == CFF_ARRAY || cff_op->type == CFF_DELTA ) {
      if ( index < 0 ) {
        object_store_integer(info, cff_op->length) ;
        return TRUE ;
      } else if ( index >= cff_op->length )
        return error_handler(RANGECHECK) ;

      /* Array subop must be a simple object */
      cff_op = &cff_op->value.array[index] ;
    }

    switch ( cff_op->type ) {
    case CFF_BOOL:
      object_store_bool(info, cff_op->value.integer) ;
      break ;
    case CFF_INT:
      object_store_integer(info, cff_op->value.integer) ;
      break ;
    case CFF_REAL:
      object_store_real(info, cff_op->value.real) ;
      break ;
    default:
      return error_handler(TYPECHECK) ;
    }
  }

  return TRUE ;
}

/* CFF charstring subrs. This relies on the sub-font having been loaded. */
static Bool cff_begin_subr(void *data, int32 subno, int32 global,
                           uint8 **subrstr, uint32 *subrlen)
{
  CFF_CACHE *cff_font = data ;
  CFF_INDEX *subr_index ;
  CFF_STRING *string ;

  VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;
  HQASSERT(subrstr != NULL, "Nowhere for subr string") ;
  HQASSERT(subrlen != NULL, "Nowhere for subr length") ;

  if ( global )
    subr_index = cff_font->cff_data.globalSubrIndex ;
  else
    subr_index = cff_font->cff_data.localSubrIndex ;

  HQASSERT(subr_index, "No subroutines of appropriate type") ;
  if ( !subr_index )
    return FALSE;

  /* Work out subr bias. Type 1 charstrings have no bias applied. See TN5176
     (16 Mar 00), p.23. */
  if ( cff_font->cff_data.chartype == CHAR_Type2 ) {
    if ( subr_index->count < 1240 )
      subno += 107 ;
    else if ( subr_index->count < 33900 )
      subno += 1131 ;
    else
      subno += 32768 ;
  }

  /* Range check the subroutine number. */
  if ( subno < 0 || (uint32)subno >= subr_index->count )
    return FALSE ;

  string = &subr_index->strings[subno] ;
  *subrlen = string->length ;
  if ( (*subrstr = fontdata_frame(cff_font->font_data,
                                  cff_font->offset + string->frame,
                                  string->length, sizeof(uint8))) == NULL )
    return FALSE ;

  return TRUE ;
}

/* CFF Type 1/2 SEAC recursive charstrings. This does not allow for glyph
   replacement, which is probably the correct behaviour. */
static Bool cff_begin_seac(void *data, int32 stdindex,
                           uint8 **subrstr, uint32 *subrlen)
{
  CFF_CACHE *cff_font = data ;
  CFF_INDEX *charstrings ;
  CFF_STRING *string ;
  CFF_DATA *CFF_data ;

  VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;
  HQASSERT(subrstr != NULL, "Nowhere for subr string") ;
  HQASSERT(subrlen != NULL, "Nowhere for subr length") ;

  if ( stdindex < 0 || stdindex >= ENCODING_STANDARD_MAX )
    return error_handler(RANGECHECK) ;

  /* We need to map from a StandardEncoding index to a GID, and open the
     character frame for this GID. StandardEncoding will map a char code to
     a name. The charsetNames index maps GIDs to names, so we need a reverse
     lookup on charsetNames to find the GID for a StandardEncoding index. */
  stdindex = encoding_standard[stdindex] ; /* stdindex to SID */

  CFF_data = &cff_font->cff_data ;
  stdindex = cff_locatecidindex(CFF_data, stdindex) ; /* SID to GID */

  charstrings = CFF_data->charStringsIndex ;
  if ( !charstrings || stdindex < 0 || stdindex == CFF_INVALID_GID )
    return FALSE ;

  string = &charstrings->strings[stdindex] ;
  *subrlen = string->length ;
  if ( (*subrstr = fontdata_frame(cff_font->font_data,
                                  cff_font->offset + string->frame,
                                  string->length, sizeof(uint8))) == NULL )
    return FALSE ;

  return TRUE ;
}

static void cff_end_substring(void *data, uint8 **subrstr, uint32 *subrlen)
{
  UNUSED_PARAM(void *, data) ;
  *subrstr = NULL ;
  *subrlen = 0 ;
}

/*---------------------------------------------------------------------------*/
/* Font lookup and top-level charstring routines for CFF Type 2 fonts. See
   PLRM3, pp. 343-347. The normal (non-CID) CFF font is looked up in a similar
   way to Type 1 fonts. Glyph replacement can be performed. */
static Bool cff_lookup_char(FONTinfo *fontInfo, charcontext_t *context)
{
  CFF_CACHE *cff_font ;

  HQASSERT(context, "No char context") ;
  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(theIFontType(fontInfo) == FONTTYPE_CFF, "Not in a Type 2/CFF font") ;

  if (oType(context->glyphname) == OINTEGER) {
    context->definition = context->glyphname ;
  } else {
    if ( !get_sdef(fontInfo, &context->glyphname, &context->definition) )
      return FALSE ;
  }

  if ( (cff_font = cff_set_font(fontInfo)) == NULL )
    return FALSE ;

  VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;

  /* PLRM3 p.351: Array (procedure) for charstring is a glyph replacement
     procedure. */
  switch ( oType(context->definition) ) {
  case OARRAY: case OPACKEDARRAY:
    /* Replacement glyph detected. Use Type 3 charstring methods. */
    context->chartype = CHAR_BuildChar ;
    break ;
  case OINTEGER:
    context->chartype = cff_font->cff_data.chartype ;
    break ;
  default:
    return error_handler(INVALIDFONT) ;
  }

  return TRUE ;
}

/* Determine if the named char exists in the font, and whether it has been
   replaced by a procedure. */
static Bool cff_begin_char(FONTinfo *fontInfo, charcontext_t *context)
{
  CFF_CACHE *cff_font ;
  CFF_INDEX *charstrings ;
  CFF_STRING *string ;
  uint8 *frame ;
  int32 index ;

  HQASSERT(context, "No char context") ;
  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(FONT_IS_CFF(theIFontType(fontInfo)), "Not in a Type 2/CFF font") ;

  if ( context->chartype == CHAR_BuildChar )
    return (*font_type3_fns.begin_char)(fontInfo, context) ;

  HQASSERT(context->chartype == CHAR_Type1 || context->chartype == CHAR_Type2,
           "Charstring wrong type for CFF font") ;

  /* Find the cache entry and open the font data. The font data stays open
     until the end_char routine. */
  if ( (cff_font = cff_set_font(fontInfo)) == NULL )
    return FALSE ;

  VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;

  HQASSERT(oType(context->definition) == OINTEGER,
           "Character definition is not glyph index") ;
  index = oInteger(context->definition) ;

  charstrings = cff_font->cff_data.charStringsIndex ;
  if ( !charstrings || index < 0 || (uint32)index >= charstrings->count )
    return FALSE ;

  if ( (cff_font->font_data = fontdata_open(cff_font->source,
                                            cff_font->fdmethods)) == NULL )
    return FALSE ;

  string = &charstrings->strings[index] ;
  if ( (frame = fontdata_frame(cff_font->font_data,
                               cff_font->offset + string->frame,
                               string->length, sizeof(uint8))) == NULL ) {
    fontdata_close(&cff_font->font_data) ;
    return FALSE ;
  }

  /* Suppress single null byte definitions [64571] */
  if (string->length == 1 && frame[0] == 0)
    frame[0] = 14 ; /* endchar */

  /* Convert charstring index into string object */
  theTags(context->definition) = OSTRING | LITERAL | READ_ONLY ;
  theLen(context->definition) = CAST_UNSIGNED_TO_UINT16(string->length) ;
  oString(context->definition) = frame ;

  context->methods = &cff_font->cffmethods ;

  return TRUE ;
}

static void cff_end_char(FONTinfo *fontInfo,
                         charcontext_t *context)
{
  CFF_CACHE *cff_font ;
  charstring_methods_t *cffmethods ;

  HQASSERT(context, "No char context") ;
  HQASSERT(FONT_IS_CFF(theIFontType(fontInfo)), "Not in a Type 2/CFF font") ;

  if ( context->chartype == CHAR_BuildChar ) {
    (*font_type3_fns.end_char)(fontInfo, context) ;
    return ;
  }

  cffmethods = context->methods ;
  HQASSERT(cffmethods, "CFF charstring methods lost") ;

  cff_font = cffmethods->data ;
  VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;
  HQASSERT(cffmethods == &cff_font->cffmethods,
           "CFF charstring methods inconsistent with font structure") ;

  /* Release fontdata frame and then fontdata */
  fontdata_close(&cff_font->font_data) ;

  object_store_null(&context->definition) ;
}

/*---------------------------------------------------------------------------*/

/* Font lookup and top-level charstring routines for CFF Type 2 fonts. See
   PLRM3, pp. 343-347. This is the CID variant, not documented in PLRM3, but
   in TN5176 (16 Mar 00), and embedded in PDF. */
static Bool cff_cid0_lookup_char(FONTinfo *fontInfo, charcontext_t *context)
{
  CFF_CACHE *cff_font ;
  int32 cid ;
  int32 index ;

  HQASSERT(context, "No char context") ;
  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(theIFontType(fontInfo) == CIDFONTTYPE0C,
           "Not in a CID Type 0C font") ;

  if ( (cff_font = cff_set_font(fontInfo)) == NULL )
    return FALSE ;

  VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;

  HQASSERT(oType(context->glyphname) == OINTEGER, "Glyph not indexed by CID") ;
  cid = oInteger(context->glyphname) ;

  if ( (index = cff_locatecidindex(&cff_font->cff_data, cid)) == CFF_INVALID_GID ) {
    context->chartype = CHAR_Undefined ;
    return TRUE ;
  }

  /* The character definition is the index in the charstrings table */
  object_store_integer(&context->definition, index) ;

  /* Lookup fdindex for the CID in the FDSelect structure */
  theFDIndex(*fontInfo) = cff_locatecidfd(&cff_font->cff_data, cid) ;

  /* Set the default charstring type. This may be overridden by the FDArray
     dictionaries. */
  context->chartype = cff_font->cff_data.chartype ;

  return TRUE ;
}

/* Subfont selector for CFF-type CID fonts. The charstring routines are
   shared with non-CID CFF fonts. */
static Bool cff_cid0_select_subfont(FONTinfo *fontInfo, charcontext_t *context)
{
  CFF_CACHE *cff_font ;

  HQASSERT(context, "No char context") ;
  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(theIFontType(fontInfo) == CIDFONTTYPE0C, "Not in a CFF CID font") ;

  if ( (cff_font = cff_set_font(fontInfo)) == NULL )
    return FALSE ;

  VERIFY_OBJECT(cff_font, CFF_CACHE_NAME) ;

  /* Only load the sub-font if it is different to the previously loaded
     one. */
  if ( theFDIndex(*fontInfo) != cff_font->cff_data.fdindex ) {
    Bool result;

    if ( (cff_font->font_data = fontdata_open(cff_font->source,
                                              cff_font->fdmethods)) == NULL )
      return FALSE ;

    result = cff_initSubFont(cff_font, theFDIndex(*fontInfo)) ;

    fontdata_close(&cff_font->font_data) ;

    if ( !result )
      return FALSE ;
  }

  /* Multiply this by the current fontmatrix to give a new one */
  matrix_mult(&cff_font->cff_data.fdmatrix,
              &theFontMatrix(*fontInfo), &theFontMatrix(*fontInfo));
  matrix_mult(&cff_font->cff_data.fdmatrix,
              &theFontCompositeMatrix(*fontInfo),
              &theFontCompositeMatrix(*fontInfo));

  /* gotFontMatrix is still set, from the set_font call preceding this. Leave
     theLookupFont set if it was already, since the FID in the parent
     dictionary is still valid. The matrix has changed though, so clear the
     lookup matrix. */
  theLookupMatrix(*fontInfo) = NULL ;

  context->chartype = cff_font->cff_data.chartype ;

  return TRUE ;
}


static void init_C_globals_cff(void)
{
#if defined( ASSERT_BUILD )
  debug_cff = FALSE ;
#endif

  cff_gc_root = NULL ;
  cff_cache_size = 0 ;
  cff_mem_cache.low_mem_handler = mm_simple_cache_handler_init;
  cff_mem_cache.low_mem_handler.name = "CFF font cache";
  cff_mem_cache.low_mem_handler.tier = memory_tier_disk;
  /* Renderer threads don't touch this, so it's mt-safe. */
  cff_mem_cache.low_mem_handler.multi_thread_safe = TRUE;
  /* no init needed for offer */
  cff_mem_cache.data_size = cff_mem_cache.last_data_size = 0;
  cff_mem_cache.last_purge_worked = TRUE;
  /* There is a fair amount of processing to build a CFF font. */
  cff_mem_cache.cost = 5.0f;
  cff_mem_cache.pool = mm_pool_temp;
  cff_mem_cache.purge = &cff_mem_purge;
}

static Bool cff_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( mps_root_create(&cff_gc_root, mm_arena, mps_rank_exact(),
                        0, cff_scan, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}

static void cff_finish(void)
{
  mps_root_destroy(cff_gc_root) ;
}

void cff_C_globals(core_init_fns *fns)
{
  init_C_globals_cff() ;

  fns->swstart = cff_swstart ;
  fns->finish = cff_finish ;
}


/* Log stripped */
