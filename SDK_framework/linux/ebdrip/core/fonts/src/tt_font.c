/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:tt_font.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to interface TrueType interpreters to SWv20, and general TT table
 * reading and extraction functions.
 */

#include "core.h"
#include "ttf.h"
#include "tt_font.h"

#include "coreinit.h"
#include "objnamer.h"
#include "hqmemcmp.h"
#include "swerrors.h"
#include "swoften.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "monitor.h"
#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "gcscan.h"
#include "dictscan.h"
#include "dicthash.h"
#include "namedef_.h"

#include "fonts.h"
#include "fontdata.h"
#include "fontcache.h"
#include "fontparam.h"
#include "chbuild.h"

#include "often.h"
#include "control.h"  /* interpreter() */
#include "matrix.h"
#include "stacks.h"
#include "graphics.h"
#include "pathops.h"
#include "gstate.h"
#include "swmemory.h"
#include "showops.h"
#include "gu_path.h"
#include "system.h"
#include "fcache.h"
#include "rlecache.h"
#include "encoding.h"
#include "psvm.h"
#include "utils.h"
#include "basemap.h" /* get_basemap_semaphore */

#include "routedev.h"
#include "swcopyf.h"
#include "agl.h"
#include "tt_sw.h"

#include "cidmap.h"   /* cid2_lookup_char */
#include "cidfont.h"  /* CIDFONTTYPE */
#include "charstringtt.h" /* This file ONLY implements TrueType fonts */

#include "hqunicode.h"
#include "cache_handler.h"


#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
/** Define this for a pretty postscript picture. You will also need to copy the
   SWv20!testsrc:hinting:hintdbg to SW/procsets/HintDebug in your SW folder */
#define DEBUG_PICTURE
#endif

static mps_root_t ttfont_gc_root ;

int tt_global_error;    /* error value */
static double tt_xwidth, tt_ywidth;

/* Exported definition of the font methods for VM-based Type 42 fonts */
static Bool tt_lookup_char(FONTinfo *fontInfo,
                           charcontext_t *context) ;
static Bool tt_begin_char(FONTinfo *fontInfo,
                          charcontext_t *context) ;
static void tt_end_char(FONTinfo *fontInfo,
                        charcontext_t *context) ;

Bool tt_base_key(FONTinfo *fontInfo,
                 char_selector_t *selector,
                 charcontext_t *context)
{
  if (selector->pdf && !selector->name) {
    /* [sab] Cope with Symbolic PDF lookups */
    theTags(context->glyphname) = OINTEGER ;
    oInteger(context->glyphname) = selector->cid ;

    context->glyphchar = selector->cid ;

    return TRUE ;
  }

  /* else do the usual thing */
  return fontcache_base_key(fontInfo, selector, context) ;
}

font_methods_t font_truetype_fns = {
  tt_base_key,
  tt_lookup_char,
  NULL, /* No subfont lookup */
  tt_begin_char,
  tt_end_char
} ;

/* Exported definition of the font methods for CID Font Type 2 (TrueType) */
font_methods_t font_cid2_fns = {
  fontcache_cid_key,
  cid2_lookup_char,
  NULL, /* No subfont lookup */
  tt_begin_char,
  tt_end_char
} ;

/* Internal definition of TrueType charstring methods. The static definition
   below is only ever used as a prototype, its data pointer is never altered.
   Copies are stored in the CFF font cache structure, and accessed through
   there. */
static uint8 *tt_frame_open(void *data, uint32 offset, uint32 length) ;
static void tt_frame_close(void *data, uint8 **frame) ;

static const charstring_methods_t tt_charstring_fns = {
  NULL,   /* private data (TT_CACHE) */
  tt_frame_open,
  tt_frame_close
} ;


/*---------------------------------------------------------------------------*/
#ifdef DEBUG_PICTURE
#include "ripdebug.h"

static SYSTEMVALUE minv[4] ;

int32 debug_tt = 0 ;
#endif

/*---------------------------------------------------------------------------*/
/* Routines exported to SWTrueType. */
void *tt_malloc(unsigned int size)
{
  void *ptr ;
  ptr = (void *)mm_alloc_with_header( mm_pool_temp, size,
                                      MM_ALLOC_CLASS_TTFONT ) ;
  if ( !ptr )
    ( void )error_handler(VMERROR);
  else
    HqMemZero(ptr, size) ;

  return ptr ;
}

void tt_free(void *ptr)
{
  mm_free_with_header(mm_pool_temp, (mm_addr_t) ptr);
}

/*****************************************************************************/
/* Generic TrueType table access functions, moved here from tt242.c. These will
 * move again when the COREfonts module is created.
 */
#define TTC_SIZE_HEADER 12u
#define TTC_SIZE_OFFSET 4u

#define TT_SIZE_HEADER 12u
#define TT_SIZE_TABLE  16u

#define TT_SIZE_HEAD   54u
#define TT_SIZE_MAXP   32u
#define TT_SIZE_CMAP    4u
#define TT_SIZE_POST   32u
#define TT_SIZE_HHEA   36u
#define TT_SIZE_NAME    6u
#define TT_SIZE_OS2_0  68u /* Original Apple table version 0 size */
#define TT_SIZE_OS2_1  86u /* Microsoft version 1.0 table size */
#define TT_SIZE_VORG    8u
#define TT_SIZE_GSUB   10u /* Header. Script, Feature & Lookup lists follow */

#define TT_SIZE_CMAP_TABLE1 8u
#define TT_SIZE_CMAP_TABLE2_16 6u
#define TT_SIZE_CMAP_TABLE2_32 12u

#define TT_SIZE_CMAP_TABLE_DATA_0 256u
#define TT_SIZE_CMAP_TABLE_DATA_2 0u
#define TT_SIZE_CMAP_TABLE_DATA_4 8u
#define TT_SIZE_CMAP_TABLE_DATA_6 4u
#define TT_SIZE_CMAP_TABLE_DATA_12 4u

#define TT_SIZE_CMAP_TABLE_DATA_2_1 512u
#define TT_SIZE_CMAP_TABLE_DATA_2_2 8u

#define TT_SIZE_CMAP_TABLE_DATA_4_1 8u
#define TT_SIZE_CMAP_TABLE_DATA_4_2 2u

#define TT_SIZE_CMAP_TABLE_DATA_12_1 12u

#define TT_SIZE_NAME_RECORD       12u

/* The GSUB table contains a huge number of options encoded in three layers of
 * lists that in turn reference other lists. The vast majority we do not and
 * can not support. We shouldn't need to support any of it, but we need to pass
 * certain ROS CID ranges through the 'vrt2' or 'vert' lookups to correct the
 * CIDtoGIDmap constructed via Unicode (UCS2 CMap): Unicode cannot encode the
 * rotated forms of Japanese glyphs so we'd erroneously get the horizontal form
 * when the vertical was requested.
 *
 * This is a work around for an insolvable problem: If the PDF does not have
 * a Japanese font embedded then it is *impossible* to access over 8000 chrs
 * from the Adobe-Japan1 ROS if the installed font only has Unicode cmaps. We
 * can only use the single CID to single CID lookup, and only sensibly for the
 * 'vrt2' and 'vert' features. As all other aspects of the highly complex GSUB
 * table will be discarded, we do not store the contents of the GSUB table.
 * Instead we build only the 'vert' map while building the TT_CMAP and discard
 * it afterwards. [307872] */

/* The only GSUB tables we support */
#define TT_SIZE_SCRIPTLIST         2u /* Count, then ScriptRecords */
#define TT_SIZE_SCRIPTRECORD       6u /* Script tag & offset */
#define TT_SIZE_SCRIPT             4u /* Default offset & count, then records */
#define TT_SIZE_LANGSYSRECORD      6u /* Language tag & offset */
#define TT_SIZE_LANGSYSTABLE       6u /* Order, index & count, then records */
#define TT_SIZE_FEATURELIST        2u /* Count, then FeatureRecords */
#define TT_SIZE_FEATURERECORD      6u /* Feature tag & offset */
#define TT_SIZE_FEATURETABLE       4u /* FeatureParams offset & Lookup count */
#define TT_SIZE_LOOKUPLIST         2u /* Count, then LookupTables */
#define TT_SIZE_LOOKUPTABLE        6u /* Type, Flag, Count, then subtables */
#define TT_SIZE_SINGLESUBST        6u /* Format, coverage offset & delta/count */
#define TT_SIZE_COVERAGE           4u /* Format & count, then glyphs/ranges */
#define TT_SIZE_RANGERECORD        6u /* Start id, end id and index */

#define TT_GSUB_SINGLESUBSTITUTION  1 /* Type enum in GSUB LookupTables */

/* -------------------------------------------------------------------------- */
/* Codes of the form xxxxFFFE and xxxxFFFF are defined as NOT A CHARACTER by
   the Unicode standard. Conveniently, 0xFFFF can also be used to indicate an
   invalid glyph ID, since the number of glyphs in the max profile is
   represented by a uint16 (and hence the last glyph index possible is
   0xfffe). */
#define TT_NO_GLYPH   (( uint16 )0xFFFF)

#define TT_BAD_GLYPH  0x20000     /* neither TT_NO_GLYPH nor TT_NO_GLYPH+1 */

/* Order of names in the name index array below. Entries in the name index
   array are Name IDs in the 'name' table. */
enum { TT_NAME_FAMILYNAME,
       TT_NAME_WEIGHT,
       TT_NAME_PSNAME,
       TT_NAME_CIDFONTNAME } ;
static int32 tt_name_index[] = {
   1,  /* TT_NAME_FAMILYNAME */
   2,  /* TT_NAME_WEIGHT */
   6,  /* TT_NAME_PSNAME */
   20, /* TT_NAME_CIDFONTNAME */
   /* TT_NAME_COPYRIGHT        0 */
   /* TT_NAME_UNIQUE           3 */
   /* TT_NAME_FULLNAME         4 */
   /* TT_NAME_VERSION          5 */
   /* TT_NAME_TRADEMARK        7 */
} ;

#define TT_NAME_MAX NUM_ARRAY_ITEMS(tt_name_index)

/* Definitions of code page range bits in 'OS/2' table */
#define TT_OS2_JAPANESE_JIS   (1u << 17u) /* JIS/Japan (Adobe/Japan1) */
#define TT_OS2_CHINESE_SIMPLE (1u << 18u) /* Chinese simplified (Adobe/GB1) */
#define TT_OS2_KOREAN_WANSUNG (1u << 19u) /* Korean Wansung (Adobe/Korea1) */
#define TT_OS2_CHINESE_TRAD   (1u << 20u) /* Chinese traditional (Adobe/CNS1) */
#define TT_OS2_KOREAN_JOHAB   (1u << 21u) /* Korean Johab (Adobe/Korea1) */
#define TT_OS2_SYMBOL         (1u << 31u) /* Symbol */

/* Definitions of valid types in the 'name' table. */
#define TT_PID_APPLE_UNICODE    0
#define TT_PID_MACINTOSH        1
#define TT_PID_ISO              2
#define TT_PID_MICROSOFT        3
#define TT_PID_UNDEFINED        -1

#define TT_EID_UNDEFINED        -1
#define TT_LID_UNDEFINED        -1

/* TT_PID_MACINTOSH */
#define TT_EID_ROMAN            0

#define TT_LID_MAC_ENGLISH      0
#define TT_LID_MAC_NONSPECIFIC  0xffff

/* TT_PID_ISO */
#define TT_EID_ASCII            0
#define TT_EID_ISO_10646        1
#define TT_EID_ISO_8859_1       2

/* TT_PID_MICROSOFT */
#define TT_EID_WIN_SYMBOL       0
#define TT_EID_WIN_UGL          1
#define TT_EID_WIN_SHIFTJIS     2
#define TT_EID_WIN_PRC          3
#define TT_EID_WIN_BIG5         4
#define TT_EID_WIN_WANSUNG      5
#define TT_EID_WIN_JOHAB        6
#define TT_EID_WIN_UCS4        10

#define TT_LID_WIN_ENG_USA      0x0409
  /* None of the following used for now. */
#define TT_LID_WIN_ENG_GB       0x0809
#define TT_LID_WIN_ENG_AUS      0x0c09
#define TT_LID_WIN_ENG_CAN      0x1009
#define TT_LID_WIN_ENG_NZ       0x1409
#define TT_LID_WIN_ENG_IR       0x1809

/* for faking an 8bit cmap(3,0) table in tt_readcmapTable */
#define TT_FAKE_MIN_CODE        0
#define TT_FAKE_MAX_CODE        255

/* "tried" and "loaded" are uint8s used to indicate whether individual TrueType
 * tables have been looked for, and succesfully loaded, respectively (They should
 * have been a single state but that's an argument for a different day). They have
 * previously contained the values FALSE or TRUE, but in some cases there are
 * multiple versions of a table, some containing more information than others. If
 * such a table is present in a smaller form (not supplying all the information we
 * require) then loaded == PARTIAL, and caution must be observed. We also define
 * COMPLETE as an alias for TRUE, to avoid confusion in comparisons.
 *
 * See tt_readOS2Table and get_xps_metrics for usage.
 */
#define PARTIAL 3  /* i.e. PARTIAL != TRUE, but (PARTIAL & TRUE) == TRUE */
#define COMPLETE 1 /* because a comparison with TRUE would be misleading */

/* The priority structures are used for searching for 'name' and 'cmap' table
   entries for particular platform, encoding and language combinations. cmap
   tables don't have language fields, so the lid is ignored. */
typedef struct {
  int32 pid, eid, lid ;
} PEL_PRIORITY ;

typedef struct tt_table {
  uint32 tag ;
  uint32 checkSum ;
  uint32 offset ;
  uint32 length ;
} TT_TABLE ;

typedef struct tt_head {
   int32 fontRevision ;
  uint16 unitsPerEm ;
   int16 xMin , yMin , xMax , yMax ;
  uint8 loaded, tried, repaired ;
} TT_HEAD ;

typedef struct tt_maxp {
  uint16 numGlyphs ;
  uint8 loaded, tried ;
} TT_MAXP ;

typedef struct tt_os2 {
  uint32 ulUnicodeRange1, ulUnicodeRange2, ulUnicodeRange3, ulUnicodeRange4 ;
  uint32 ulCodePageRange1, ulCodePageRange2 ;
  int16  sTypoAscender ;
  int16  sTypoDescender ;
  uint8 loaded, tried ;
} TT_OS2 ;

typedef struct tt_post {
  uint32 FormatType ;
  uint16 numPostGlyphs ;      /* renamed to avoid confusion with maxp.numGlyphs */
  NAMECACHE **GlyphNames ;
  /* And for FontInfo. */
  int32 italicAngle ;
  int16 underlinePosition ;
  int16 underlineThickness ;
  uint8 isFixedPitch ;

  uint32 minMemType42 ;
  uint32 maxMemType42 ;
  uint8 loaded, tried, repaired ;
} TT_POST ;

/* [62037] Non-sparse array optimisation: If the segment is a one-to-one
 * mapping, in effect an offset, then there is a single entry in the
 * glyphIndex[] array, which is an offset from the code to the glyph index,
 * modulo 2^16. The mapping is calculated on the fly, with 0xFFFF->0, as
 * usual.
 */
#define TT_CMAP_SEGMENT_SIZE(startCode, endCode, useOffset) \
  (sizeof(TT_CMAP_SEGMENT) + \
   ((useOffset) ? 0 : sizeof(uint16) * ((endCode) - (startCode))))

typedef struct tt_cmap_segment {
  uint32 startCode, endCode ;     /* range of input codes */
  uint8  useOffset ;              /* Is this range offset mapped? */
  uint16 glyphIndex[1] ; /* Extended by (endCode-startCode)*sizeof(uint16) */
} TT_CMAP_SEGMENT ;

#define TT_CMAP_ENTRY_SIZE(nSegments) \
  (sizeof(TT_CMAP_ENTRY) + sizeof(TT_CMAP_SEGMENT *) * ((nSegments) - 1))

typedef struct tt_cmap_entry {
  struct tt_cmap_entry *next ;
  int32 size; /* [12467] size of struct may not be related to nSegments */
  uint16 PlatformID, EncodeID, LanguageID, nSegments ;
  Bool repaired ;
  TT_CMAP_SEGMENT *segments[1] ; /* Extended by (nSegments-1) pointers */
} TT_CMAP_ENTRY ;

typedef struct tt_cmap {
  TT_CMAP_ENTRY *cmaps ;
  uint8 loaded, tried ;
} TT_CMAP ;

typedef struct tt_hhea {  /* also used for vhea */
  int16  Ascender ;
  int16  Descender ;
  uint16 numberOfLongMetrics ; /* numberOfHMetrics or numOfLongVerMetrics */
  uint8  loaded, tried ;
} TT_HHEA ;

typedef struct tt_nf {
  uint16 pid, eid, lid ;   /* platform, encode, language */
  uint16 len ;
  uint8 *str ; /* NULL means not set */
  uint8 loaded, tried ;
} TT_NF ;

typedef struct tt_name {
  TT_NF names[ TT_NAME_MAX ] ;
  uint8 loaded, tried ;
} TT_NAME ;

typedef struct tt_hmtx {        /* also used for vmtx */
  uint16  numGlyphs ;           /* copy */
  uint16  numberOfLongMetrics ; /* from hhea or vhea */
  uint16* advance ;             /* advanceWidth[numberOfLongMetrics] or advanceHeight */
  int16*  sideBearing ;         /* leftSideBearing[numGlyphs] or topSideBearing */
  uint8   loaded, tried ;
} TT_HMTX ;

/* The TT VORG table contains a sparse array of vertical origins.
 * However, if a continuous subsetted array is smaller, or not very big, we use that
 * in preference. Either way, indices are compared with the range before trying the
 * array, and in the case of a subsetted array the binary search is avoided.
 */
typedef struct {
  uint16 glyphIndex ;
  int16  vertOriginY ;
} TT_VORG_ENTRY ;

typedef struct tt_vorg {
  int16   defaultVertOriginY ;
  uint16  numVertOriginYMetrics ;
  uint16  minIndex ;        /* range of indices in either flavour array */
  uint16  maxIndex ;
  void    *array ;          /* Array of TT_VORG_ENTRY[numVertOriginYMetrics] OR */
  uint8   sparse ;          /*   uint16[numGlyphs], depending on value of sparse */
  uint8   loaded, tried ;
} TT_VORG ;

typedef struct tt_data {
  uint8 unused, unused2 ; /* Use me first */
  uint16 numTables ;
  TT_TABLE *Tables ;
  uint32 version ;

  TT_HEAD tt_head ;
  TT_MAXP tt_maxp ;
  TT_CMAP tt_cmap ;
  TT_POST tt_post ;
  TT_NAME tt_name ;
  TT_OS2  tt_os2 ;

  /* [51068] for XPS vertical text handling */
  TT_HHEA tt_hhea ;
  TT_HMTX tt_hmtx ;
  TT_HHEA tt_vhea ;
  TT_HMTX tt_vmtx ;
  TT_VORG tt_vorg ;

} TT_DATA ;

/* -------------------------------------------------------------------------- */
#define tt_maptag(s_) (uint32)((((uint8 *)(s_))[0] << 24) | \
                               (((uint8 *)(s_))[1] << 16) | \
                               (((uint8 *)(s_))[2] << 8) | \
                               ((uint8 *)(s_))[3])

/* note that the following is happy with loaded == TRUE, COMPLETE or PARTIAL */
#define tt_must_have(s_) ((s_).loaded || error_handler(INVALIDFONT))

/*---------------------------------------------------------------------------*/
/** TrueType font structure cache type. This is used to prevent the TrueType
   code from having to reload the TT font data for every charstring. The TT
   cache routines use the common font data cache to store sfnt blocks
   routines. The font data pointer is not retained between characters; a new
   instance is opened for each character. The BitStream interpreter does not
   use any information from the TT_DATA structure during interpretation, but
   it is required to locate TrueType collection headers. */
typedef struct TT_CACHE {
  TT_DATA tt_data ;
  charstring_methods_t ttmethods ; /* Local copy of charstring methods */
  fontdata_t *font_data ;
  const blobdata_methods_t *fdmethods ;
  int32 fid ;       /* Font identifier */
  OBJECT *source ;  /* data source for this font */
  uint32 offset ;   /* offset into data source */
  int32 type ;      /* See enum below */
  uint32 numFonts ; /* # fonts in TrueType Collections (1 for TTF/OTTO) */
  int32 fontindex ; /* Index of font in TrueType collection */
  struct TT_CACHE *next ;
  OBJECT_NAME_MEMBER
} TT_CACHE ;

/** Enumeration of TT font types, for type field above */
enum { TT_FONT_NONE, TT_FONT_TTF, TT_FONT_TTC, TT_FONT_OTTO } ;

#define TT_CACHE_NAME "TT font cache"

static TT_CACHE *tt_set_font(int32 fid, OBJECT *fdict) ;

/*---------------------------------------------------------------------------*/
static void tt_freeFont(TT_DATA *TT_data) ;
static Bool tt_initFontCollection(TT_CACHE *TT_cache) ;
static Bool tt_initFont(TT_CACHE *TT_cache, int32 index) ;

static TT_TABLE *tt_findTable(TT_DATA *TT_data, uint32 tag) ;

static Bool tt_readheadTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readmaxpTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readcmapTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readpostTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readhheaTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readvheaTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readhmtxTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readvmtxTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readvorgTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_readnameTable(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_reados2Table(charstring_methods_t *ttfns, TT_DATA *TT_data) ;
static Bool tt_fakepostTable(TT_DATA *TT_data) ;


static Bool tt_nameIsUnicode(TT_NF *tt_name) ;

static Bool tt_hmtx_lookup(TT_DATA *TT_data, uint16 glyphIndex,
                           uint16 *advance, int16 *sideBearing) ;
static Bool tt_vmtx_lookup(TT_DATA *TT_data, uint16 glyphIndex,
                           uint16 *advance, int16 *sideBearing) ;
static Bool tt_vorg_lookup(TT_DATA *TT_data, uint16 glyphIndex,
                           int16 *vertOriginY) ;
static Bool tt_cmap_lookup(TT_CMAP_ENTRY *cmap, uint32 charCode,
                           uint16 *glyphIndex,
                           Bool (*failed)(void)) ;
static Bool tt_cmap_lookup_failed(void) ;

#ifdef DEBUG_BUILD
static Bool tt_cmap_lookup_quiet(void)
{
  return FALSE ;
}
#else
#define tt_cmap_lookup_quiet tt_cmap_lookup_failed
#endif

static void tt_free_Xmtx(TT_HMTX* hmtx) ;
static void tt_free_vorg(TT_VORG* vorg) ;

#if defined(TT_CORRECT_CHECKSUMS) || defined(TT_CHECK_CHECKSUMS) || defined(TT_TESTING)
static uint32 tt_schecksum(charstring_methods_t *ttfns,
                           int32 offset, int32 length) ;
#endif

/* Character building routines. Post modularisation, the TT code may be able
   to share the character building routines from chbuild.h. */
static PATHINFO tt_path = PATHINFO_STATIC(NULL,NULL,NULL) ;

void tt_open_outline(int32 setwx, int32 setwy, int32 minx, int32 maxx, int32 miny, int32 maxy)
{
  UNUSED_PARAM(int32, minx);
  UNUSED_PARAM(int32, maxx);
  UNUSED_PARAM(int32, miny);
  UNUSED_PARAM(int32, maxy);

  tt_xwidth = setwx ;
  tt_ywidth = setwy ;

  path_init(&tt_path) ;
}

/* N.B. int return value, since this is accessed from SWTrueType, which
   doesn't have access to core Bool definition. */
int tt_start_contour(int32 x, int32 y, int outside)
{
  UNUSED_PARAM(int, outside);

#ifdef DEBUG_PICTURE
  if ( (debug_tt & DEBUG_TT_PICTURE) != 0 ) {
    monitorf((uint8 *)"%f %f Point\n",
             x * minv[0] + y * minv[2], x * minv[1] + y * minv[3]);
  }
#endif

  return path_moveto(x, y, MOVETO, &tt_path) ;
}

/* N.B. int return value, since this is accessed from SWTrueType, which
   doesn't have access to core Bool definition. */
int tt_line_to(int32 x, int32 y)
{
#ifdef DEBUG_PICTURE
  if ( (debug_tt & DEBUG_TT_PICTURE) != 0 ) {
    monitorf((uint8 *)"%f %f Point\n",
             x * minv[0] + y * minv[2], x * minv[1] + y * minv[3]);
  }
#endif

  return path_segment(x, y, LINETO, TRUE, &tt_path);
}

/* N.B. int return value, since this is accessed from SWTrueType, which
   doesn't have access to core Bool definition. */
int tt_curve_to(int32 x1, int32 y1, int32 x2, int32 y2, int32 x3, int32 y3)
{
  SYSTEMVALUE args[6] ;

#ifdef DEBUG_PICTURE
  if ( (debug_tt & DEBUG_TT_PICTURE) != 0 ) {
    SYSTEMVALUE f1 = x1 * minv[0] + y1 * minv[2];
    SYSTEMVALUE f2 = x1 * minv[1] + y1 * minv[3];
    SYSTEMVALUE f3 = x2 * minv[0] + y2 * minv[2];
    SYSTEMVALUE f4 = x2 * minv[1] + y2 * minv[3];
    SYSTEMVALUE f5 = x3 * minv[0] + y3 * minv[2];
    SYSTEMVALUE f6 = x3 * minv[1] + y3 * minv[3];

    monitorf((uint8 *)"%f %f %f %f %f %f Curve\n", f1, f2, f3, f4, f5, f6);
  }
#endif

  args[0] = x1 ;
  args[1] = y1 ;
  args[2] = x2 ;
  args[3] = y2 ;
  args[4] = x3 ;
  args[5] = y3 ;

  return path_curveto(args, TRUE, &tt_path) ;
}

/* N.B. int return value, since this is accessed from SWTrueType, which
   doesn't have access to core Bool definition. */
int tt_close_contour(void)
{
#ifdef DEBUG_PICTURE
  if ( (debug_tt & DEBUG_TT_PICTURE) != 0 ) {
    monitorf((uint8 *)"ClosePath\n");
  }
#endif

  return path_close(CLOSEPATH, &tt_path);
}

/* N.B. int return value, since this is accessed from SWTrueType, which
   doesn't have access to core Bool definition. */
int tt_close_outline(void)
{
  if ( tt_path.lastline &&
       theLineType(*tt_path.lastline) == MOVETO ) {
    /* Remove last subpath from path and dispose of it. */
    PATHLIST *subpath ;

    path_remove_last_subpath(&tt_path, &subpath) ;
    path_free_list(subpath, mm_pool_temp) ;
  }

  return TRUE;
}

/* N.B. int return value, since this is accessed from SWTrueType, which
   doesn't have access to core Bool definition. */
int tt_check_glyphdir_exists( void )
{
  FONTinfo *fontInfo = &gstateptr->theFONTinfo ;

  if (fast_extract_hash_name(&theMyFont(*fontInfo), NAME_GlyphDirectory) == NULL)
    return FAILURE(FALSE);

  return TRUE;
}

/* N.B. int return value, since this is accessed from SWTrueType, which
   doesn't have access to core Bool definition. */
int tt_glyphdir_lookup(int32 index, uint8 **gdata, int32 *glen,
                       int32 *width, int32 *left, int32 *height, int32 *top)
{
  OBJECT *glyphdir, *theo ;
  int32 pass, goffset = 0 ;
  FONTinfo *fontInfo = &gstateptr->theFONTinfo ;

  HQASSERT( gdata != NULL, "tt_glyphdir_lookup: gdata is NULL" ) ;
  HQASSERT( glen  != NULL, "tt_glyphdir_lookup: glen is NULL" ) ;

  *gdata = NULL ;
  *glen  = 0 ;

  /* Extract GlyphDirectory from the font - if its missing then the font
   * is invalid
   */
  glyphdir = fast_extract_hash_name(&theMyFont(*fontInfo), NAME_GlyphDirectory) ;
  if ( glyphdir == NULL )
    return error_handler( INVALIDFONT ) ;

  /* Check if there is a MetricsCount */
  theo = fast_extract_hash_name(&theMyFont(*fontInfo), NAME_MetricsCount) ;
  if ( theo != NULL ) {
    if ( oType(*theo) != OINTEGER )
      return error_handler( INVALIDFONT ) ;

    goffset = oInteger(*theo) * 2 ;

    switch ( goffset ) {
    case 0: case 4: case 8:
      break ;
    default:
      return error_handler( INVALIDFONT ) ;
    }
  }

  for ( pass = 0 ; pass <= 1 ; pass++ ) {
    switch ( oType(*glyphdir) ) {
    case OARRAY:
    case OPACKEDARRAY:
      if ( index >= theLen(*glyphdir) )
        return error_handler( RANGECHECK ) ;
      theo = oArray(*glyphdir) + index ;
      break ;
    case ODICTIONARY:
      oInteger(inewobj) = index ;
      theo = extract_hash( glyphdir, &inewobj ) ;
      break ;
    default:
      return error_handler( INVALIDFONT ) ;
    }

    if ( theo ) {
      if ( oType(*theo) == OSTRING ) {
        uint8 *cs = oString(*theo) ;
        int32 len = theLen(*theo) ;

        if ( len < goffset )
          return error_handler(INVALIDFONT) ;

        /* Flags metrics not set by using negative value for advance width. */
        *height = *width = -1 ;
        *left = *top = 0 ;

        switch ( goffset ) {
        case 8:
          *height = (cs[0] << 8) | cs[1] ;
          *top = (int16)((cs[2] << 8) | cs[3]) ;
          cs += 4 ;
          /* FALLTHRU */
        case 4:
          *width = (cs[0] << 8) | cs[1] ;
          *left = (int16)((cs[2] << 8) | cs[3]) ;
          cs += 4 ;
        }

        *gdata = cs ;
        *glen = len - goffset ;

        /*
         * If we are on the second pass (pass=1), that means that the first
         * lookup failed and we are falling back to using .notdef. However
         * it appears Adobe does not do this. If a glyph name is missing from
         * the Encoding, .notdef is used. But if the name is present in the
         * Encoding, but missing from the GlyphDirectory, then it outputs the
         * equivalent of a .null instead (even if the font does not contain a
         * .null entry at position 1). i.e. the result is a blank glyph, but
         * escapement is updated by the width of .notdef.
         * Easiest way to achieve this is to leave the width/height we have
         * parsed from .notdef, but Null-out the return data.
         * [ Note if glyph is missing from Encoding, then we will be passed
         *   in a request for .notdef, and will load and use it on pass==0 ]
         */
        if ( pass == 1 )
        {
          *gdata = NULL;
          *glen = 0;
        }
        return TRUE ;
      }
      else if ( oType(*theo) != ONULL )
        return error_handler( INVALIDFONT ) ;
    }

    /* Only need to lookup the notdef once - if the character requested
     * doesn't exist, so we want the /.notdef char (this should work for
     * CIDType2 fonts when we get around to this, where the /.notdef must
     * give us index 0).
     */
    if ( pass == 0 ) {
      OBJECT sdef = OBJECT_NOTVM_NOTHING ;

      oName(nnewobj) = system_names + NAME_notdef ;
      if ( ! get_sdef(&theFontInfo(*gstateptr), &nnewobj, &sdef ))
        return FALSE ;

      if ( oType(sdef) != OINTEGER )
        return error_handler( INVALIDFONT ) ;

      index = oInteger(sdef) ;
    }
  } /* for 'pass' */
  /* If the notdef lookup gave an empty or missing entry in GlyphDirectory
   * then the font is invalid.
   */
  return error_handler( INVALIDFONT ) ;
}

/* ----------------------------------------------------------------------------
 * Do XPS vertical metric calculations using the various OpenType tables.
 */

Bool get_xps_metrics( charstring_methods_t* ttfns,
                                  TT_CACHE* tt_font,
                                    OBJECT* glyphname,
                                SYSTEMVALUE bbox[4],
                                SYSTEMVALUE metrics[ 10 ],
                                     int32* mflags,
                                       Bool scale)
  /* metrics contains:
   * [ 0 ] : MF_W0 => x-width
   * [ 1 ] : MF_W0 => y-width
   * [ 2 ] : MF_LL => new lhs (x) of bbox
   * [ 3 ] : MF_UR => translation (y) of bbox
   * [ 4 ] : unset
   * [ 5 ] : unset
   * [ 6 ] : MF_W1 => x-width-v
   * [ 7 ] : MF_W1 => y-width-v
   * [ 8 ] : MF_VO => x-vec-oH-oV
   * [ 9 ] : MF_VO => y-vec-oH-oV
   *
   * also, UR => LL => W0
   *       W1 <=> VO
   */
{
  SYSTEMVALUE X, Y = 0, H = -1, D ;
  int         ok = 7 ; /* flags to indicate calculation of the three metrics */
  TT_DATA*    tt = & tt_font->tt_data ;
  int16       Short ;
  uint16      Ushort, unitsPerEm, code = (uint16) oInteger(*glyphname) ;

  /* This logic is slightly tortuous, but is basically to ensure that we've tried loading
   * all tables we need. The XPS 1.0 spec is misleading, but :
   *
   * We REQUIRE head.
   * We REQUIRE hmtx.
   * We WOULD LIKE VORG and vmtx.
   * We WOULD LIKE OS/2 if it's a suitable version, or we REQUIRE hhea.
   * (OS/2 is a REQUIRED table according to OT1.4, but comes in various sizes)
   *
   * So, we only need hhea if we don't have a suitable OS/2, but hhea gets loaded with
   * hmtx so we can't avoid it.
   */
  if ( !tt->tt_head.tried || !tt->tt_hmtx.tried || !tt->tt_os2.tried ||
       !tt->tt_vorg.tried || !tt->tt_vmtx.tried ) {
    Bool open = FALSE ;
    Bool fail = FALSE ;

    /* not everything is loaded, so open the font data if necessary */
    if ( tt_font->font_data == NULL) {
      tt_font->font_data = fontdata_open(tt_font->source, tt_font->fdmethods);
      if ( tt_font->font_data == NULL )
        return FAILURE(FALSE) ;
      open = TRUE ;
    }

    /* now ensure all tables are tried in order and required tables are present */
    if ( (!tt->tt_head.tried && !tt_readheadTable(ttfns, tt)) || !tt->tt_head.loaded || /* head required */
         (!tt->tt_hmtx.tried && !tt_readhmtxTable(ttfns, tt)) || !tt->tt_hmtx.loaded || /* also REQUIRES hhea */
         (!tt->tt_os2.tried  && !tt_reados2Table(ttfns, tt) ) ||
         (!tt->tt_vorg.tried && !tt_readvorgTable(ttfns, tt)) ||
         (!tt->tt_vmtx.tried && !tt_readvmtxTable(ttfns, tt)) ) /* also REQUIRES vhea */
      fail = TRUE ;

    /* close the font data again, if we opened it */
    if ( open )
      fontdata_close(&tt_font->font_data) ;

    /* return if something went wrong during the above */
    if ( fail )
      return FAILURE(FALSE) ;

  } else {

    /* all tables have already been tried, so ensure required ones are present */
    if ( !tt->tt_head.loaded || !tt->tt_hmtx.loaded || (!tt->tt_os2.loaded && !tt->tt_hhea.loaded) )
      return FAILURE(FALSE) ;
  }

  /* now do the calculations. See [51068] or XPS spec for details */
  unitsPerEm = (uint16)((scale) ? tt->tt_head.unitsPerEm : 1) ;
  HQASSERT( unitsPerEm, "unitsPerEm == 0") ;

  /* vertical origin X is half advance width */
  if ( tt_hmtx_lookup(tt, code, &Ushort, &Short) ) { /* returns width and LSB */
    /* XPS spec says Ushort/2, but we need to offset from the boundingbox and
     * correct the lsb */
    X = Ushort / 2 - Short + bbox[0] * unitsPerEm ;
  } else {
    /* XPS spec doesn't say what to do if hmtx is present but too small, so... */
    HQFAIL("Unable to calculate vertical origin X") ;
    X = unitsPerEm * (bbox[0] + bbox[2]) / 2 ;
    ok -= 1 ;
  }

  /* vertical advance. This is the bit which is highly misleading in the 1.0 spec.
   * It states that advance width should ALWAYS be calculated from the vertical
   * origin and descender. That's rubbish - if we have vmtx, use it!
   */
  if ( tt->tt_vmtx.loaded && tt_vmtx_lookup(tt, code, &Ushort, &Short) ) {
    /* vmtx present, so use boundingbox data (from glyf or cff) */
    Y = Short + unitsPerEm * bbox[3] ;
    H = Ushort ;
  }

  /* vertical origin Y */
  if ( tt->tt_vorg.loaded && tt_vorg_lookup(tt, code, &Short) ) {
    /* from VORG */
    Y = Short ;
  } else {
    /* no VORG */
    if ( !tt->tt_vmtx.loaded || H == -1 ) {
      /* no vmtx (or vmtx lookup failed) */
      if ( tt->tt_os2.loaded ) {
        /* the viewer fakes TypoAscender/Descender for short Apple tables */
        if ( tt->tt_os2.loaded == COMPLETE )
          /* from OS/2 table of sufficient size to contain typographic ascender */
          Y = tt->tt_os2.sTypoAscender ;
        else
          Y = unitsPerEm * 3 / 4 ; /* This is empirical */
      } else {
        /* no OS/2, note that this is contrary to OT1.4 spec */
        if ( tt->tt_hhea.loaded ) {
          /* from hhea table */
          Y = tt->tt_hhea.Ascender ;
        } else {
          /* Can't calculate Y. We may have calculated X ok though */
          HQFAIL("Unable to calculate vertical origin Y") ;
          Y = unitsPerEm * bbox[3] ;
          ok -= 2 ;
        }
      }
    }
  }

  /* descender is only required if we didn't have vmtx */
  if ( H == -1 ) {
    if ( tt->tt_os2.loaded) {
      /* fake if necessary as the viewer does */
      if ( tt->tt_os2.loaded == COMPLETE )
        /* only if OS/2 table contains it */
        D = tt->tt_os2.sTypoDescender ;
      else
        D = -unitsPerEm / 4 ; /* This is empirically what the XPS viewer does */
    } else {
      if ( tt->tt_hhea.loaded ) {
        D = tt->tt_hhea.Descender ;
      } else {
        /* can't calculate D */
        HQFAIL("Unable to calculate advance height") ;
        D = 0 ;
        ok -= 4 ;
      }
    }
    /* advance height */
    H = Y + fabs(D) ;  /* <shudder!> Would have been H=Y-D if fonts weren't so broken. */
  }

  /* Scale for TrueType */
  if (scale) {
    H /= unitsPerEm ;
    X /= unitsPerEm ;
    Y /= unitsPerEm ;
  }

  if ( ok & 4 ) {
    /* only set the advance height if we were able to calculate it */
    metrics[6] = 0 ;
    metrics[7] = -H ;
    *mflags |= MF_W1 ;
  }
  if ( ok & 3 ) {
    /* only set the vertical origin if at least one of the coordinates is correct */
    metrics[8] = X ;
    metrics[9] = Y ;
    *mflags |= MF_VO ;
  }

  return TRUE ;
}

/* external version - called from adobe_cache() */

Bool tt_xps_metrics( FONTinfo *fontInfo,
                     OBJECT* glyphname,
                     SYSTEMVALUE bbox[4],
                     SYSTEMVALUE metrics[ 10 ],
                     int32* mflags )
{
  Bool     result, detach;
  TT_CACHE *tt_font = tt_set_font(theCurrFid(*fontInfo), &theMyFont(*fontInfo)) ;

  if ( tt_font == NULL )
    return FALSE ;            /* can't find font, error already reported */

  if ( tt_font->type != TT_FONT_OTTO )
    return FAILURE(TRUE) ;    /* We shouldn't have been called. Why were we? */

  VERIFY_OBJECT(tt_font, TT_CACHE_NAME) ;
  HQASSERT(oType(*glyphname) == OINTEGER, "Glyphname is not glyph index") ;

  /* regrettably the inuse count isn't enough to detach pointers correctly,
   * so if we weren't attached before, ensure we detach manually.
   */
  detach = (tt_font->font_data == NULL) ;

  result = get_xps_metrics( &tt_font->ttmethods, tt_font,
                            glyphname, bbox, metrics, mflags, FALSE ) ;

  if (detach)
    tt_font->font_data = NULL ;

  return result ;
}

/* -------------------------------------------------------------------------- */

Bool tt_cache(corecontext_t *context,
              charcontext_t *charcontext, LINELIST *currpt, Bool xps)
{
  double transform[6];
  Bool result;
  TT_CACHE *tt_font ;
  SYSTEMVALUE metrics[10] ;
  int32 mflags ;
  charstring_methods_t *ttfns ;
  FONTinfo *fontInfo = & gstateptr->theFONTinfo ;
  double tt_font_scale ;
  OMATRIX tt_ifmatrix ;    /* Inverse fontmatrix, without tt_font_scale */
  OBJECT *glyphid ;
  int32 showtype ;

  UNUSED_PARAM(corecontext_t *, context);

  HQASSERT(charcontext, "No character context") ;
  HQASSERT(charcontext == char_current_context(),
           "Character context is not current") ;

  glyphid = &charcontext->definition ;
  showtype = charcontext->modtype ;

  ttfns = charcontext->methods ;
  HQASSERT(ttfns, "No TrueType charstring methods") ;

  tt_font = ttfns->data ;
  HQASSERT(tt_font, "No TT font data") ;

  if ( oType(*glyphid) != OINTEGER )
    return error_handler(INVALIDFONT) ;

  if ( !get_metrics(&charcontext->glyphname, metrics, &mflags) )
    return error_handler( INVALIDFONT ) ;

  /* At this point, metrics contains:
   * [ 0 ] : MF_W0 => x-width                         else unset thoughout
   * [ 1 ] : MF_W0 => y-width
   * [ 2 ] : MF_LL => new lhs (x) of bbox
   * [ 3 ] : MF_UR => translation (y) of bbox
   * [ 4 ] : unset
   * [ 5 ] : unset
   * [ 6 ] : MF_W1 => x-width-v
   * [ 7 ] : MF_W1 => y-width-v
   * [ 8 ] : MF_VO => x-vec-oH-oV
   * [ 9 ] : MF_VO => y-vec-oH-oV
   *
   * also, UR => LL => W0
   *       W1 <=> VO
   */

  /* If metrics (width) & stringwidth, and no cdevproc, and not xps, done enough. */
  if ( showtype == DOSTRINGWIDTH &&
       oType(theCDevProc(*fontInfo)) == ONULL &&
       !xps ) {
    /* then NO CDevProc, so no new info will be forthcoming */
    SYSTEMVALUE *wxy = NULL ;

    if ( theWMode(*fontInfo) != 0 && (mflags & MF_W1) != 0 )
      wxy = &metrics[6] ; /* wmode 1, W1 is defined */
    else if ( (mflags & MF_W0) != 0 )
      wxy = &metrics[0] ; /* wmode 0 or W1 is undefined; use W0 widths */

    if ( wxy != NULL ) {
      COMPUTE_STRINGWIDTH(wxy, charcontext) ;
      return TRUE ;
    }
  }

  /* Use the FontMatrix which is already concatenated with thegsPageCTM */
  /* and extract the transformation matrix */
  transform[0] = MATRIX_00(&theFontMatrix(*fontInfo));
  transform[1] = MATRIX_01(&theFontMatrix(*fontInfo));
  transform[2] = MATRIX_10(&theFontMatrix(*fontInfo));
  transform[3] = MATRIX_11(&theFontMatrix(*fontInfo));
  transform[4] = 0.0;
  transform[5] = 0.0;

  /* [62661] We'll need unitsPerEm to adjust scale factors */
  HQASSERT(tt_font->font_data, "TT font data should already be open") ;
  if ( !tt_readheadTable(ttfns, &tt_font->tt_data) ||
       !tt_must_have(tt_font->tt_data.tt_head) )
    return error_handler(INVALIDFONT) ;

  /* Adjust transformation matrix and set font scale to convert values from
     TT compound delivered values to correctly scaled coordinates without
     overflowing TrueType interpreter internal fixed-point values. */
  tt_adjust_transform(&tt_font_scale, transform,
                      tt_font->tt_data.tt_head.unitsPerEm) ; /* [62661] */

  /* Find inverse of adjusted matrix; this takes values from the scaled
     device space in which values are passed to the outline callbacks to font
     space. This matrix is only used for delta transforms, so zero the
     translational components. */
  if ( !matrix_inverse(&theFontMatrix(*fontInfo), &tt_ifmatrix) )
    return error_handler(UNDEFINEDRESULT) ;

  MATRIX_00(&tt_ifmatrix) *= tt_font_scale ;
  MATRIX_01(&tt_ifmatrix) *= tt_font_scale ;
  MATRIX_10(&tt_ifmatrix) *= tt_font_scale ;
  MATRIX_11(&tt_ifmatrix) *= tt_font_scale ;
  MATRIX_20(&tt_ifmatrix) = 0.0 ;
  MATRIX_21(&tt_ifmatrix) = 0.0 ;

#ifdef DEBUG_PICTURE
  {
    static int32 init = 0;

    if ( init != debug_tt ) {
      if ( (debug_tt & DEBUG_TT_PICTURE) != 0 )
        monitorf((uint8 *)"/HintDebug /ProcSet findresource pop\n");
      init = debug_tt ;
    }
  }

  if ( (debug_tt & DEBUG_TT_PICTURE) != 0 ) {
    OBJECT *theo;
    SYSTEMVALUE m00, m01, m10, m11 ;
    SYSTEMVALUE onepixelX, onepixelY, unitpixelsX, unitpixelsY ;
    SYSTEMVALUE det ;
    TT_DATA *TT_data = &tt_font->tt_data ;
    int32 unitsPerEm ;

    HQASSERT(tt_font->font_data, "TT font data should already be open") ;

    unitsPerEm = TT_data->tt_head.unitsPerEm ;
    monitorf((uint8 *)"%d CoordSpace\n", unitsPerEm);
    monitorf((uint8 *)"Init\n");

    /* Get font name */
    if ( (theo = fast_extract_hash_name(&theMyFont(theIFontInfo(gstateptr)) ,
                                        NAME_FontName)) != NULL ) {
      if ( oType(*theo) == ONAME ) {
        monitorf((uint8 *)"/%s Font\n", theICList(oName(*theo)));
      } else if ( oType(*theo) == OSTRING ) {
        monitorf((uint8 *)"(%.*s) Font\n", theLen(*theo), oString(*theo));
      }
    }
    switch ( charcontext->glyphchar ) {
    case '(': case ')': case '\\':
      monitorf((uint8 *)"(\\%c) Char\n", charcontext->glyphchar) ;
      break ;
    default:
      monitorf((uint8 *)"(%c) Char\n", charcontext->glyphchar) ;
      break ;
    }

    /* unitpixelsX/Y are the number of pixels per character space unit.
       These are calculated from the lengths of the character space X and Y
       direction vectors. */
    m00 = transform[0] / (65536.0 * unitsPerEm * tt_font_scale) ;
    m01 = transform[1] / (65536.0 * unitsPerEm * tt_font_scale) ;
    m10 = transform[2] / (65536.0 * unitsPerEm * tt_font_scale) ;
    m11 = transform[3] / (65536.0 * unitsPerEm * tt_font_scale) ;
    unitpixelsX = sqrt( m00 * m00 + m01 * m01 ) ;
    unitpixelsY = sqrt( m10 * m10 + m11 * m11 ) ;

    /* Create matrix to translate from TT coordinate space to EM space */
    det = (transform[0] * transform[3] - transform[1] * transform[2]) ;
    HQASSERT(det != 0.0, "Non invertible matrix") ;
    minv[0] = transform[3] * tt_font_scale * unitsPerEm / det ;
    minv[1] = -transform[1] * tt_font_scale * unitsPerEm / det ;
    minv[2] = -transform[2] * tt_font_scale * unitsPerEm / det ;
    minv[3] = transform[0] * tt_font_scale * unitsPerEm / det ;

    /* onepixelX/Y are length of one pixel in character space */
    if (unitpixelsX == 0.0)
      onepixelX = 0.0;
    else
      onepixelX = 1.0 / unitpixelsX ;
    if (unitpixelsY == 0.0)
      onepixelY = 0.0;
    else
      onepixelY = 1.0 / unitpixelsY ;

    monitorf((uint8 *)"%f OnePixelX\n", onepixelX);
    monitorf((uint8 *)"%f OnePixelY\n", onepixelY);
    monitorf((uint8 *)"%f UnitPixelsX\n", unitpixelsX);
    monitorf((uint8 *)"%f UnitPixelsY\n", unitpixelsY);
    monitorf((uint8 *)"/%s FillRule\n",
             context->fontsparams->fontfillrule == EOFILL_TYPE
              ? "eofill"
              : "fill") ;
    monitorf((uint8 *)"InitDone\n");
  }
#endif

  tt_global_error = 0 ;
  HQASSERT(!error_signalled(), "Starting char while in error condition");
  error_clear_newerror();
  result = tt_do_char(ttfns->data, ttfns->open_frame, ttfns->close_frame,
                      (long)tt_font->fid,
                      oInteger(*glyphid), transform,
                      0 ) ; /* [61857] */

  if ( !result ) {
    /* There was an error in tt_do_char. If we set the error (via
       tt_glyphdir_lookup) then don't lose it. */
    if ( !newerror )
      (void)error_handler( INVALIDFONT ) ; /* Well it was, wasn't it?! */
  } else { /* tt_do_char OK */
    sbbox_t tt_bbox ;
    PATHLIST *thepath ;
    LINELIST *theline ;
    Bool tt_marked ;
    SYSTEMVALUE *bbindexed ;

    /* Convert stored widths to font space */
    MATRIX_TRANSFORM_DXY(tt_xwidth, tt_ywidth, tt_xwidth, tt_ywidth,
                         &tt_ifmatrix) ;

    if ( !(mflags & MF_W0) ) {
      metrics[0] = tt_xwidth ;
      metrics[1] = tt_ywidth ;
      mflags |= MF_W0 ;
    }

    /* Detect blank characters and set the bbox to zero. Blank characters have
       either no path, or MOVETO followed by a CLOSEPATH. The implementation
       of the contour routines always adds a CLOSEPATH, so a non-empty contour
       must have at least three elements. */
    if ( (thepath = tt_path.firstpath) != NULL &&
         (theline = theSubPath(*thepath)) != NULL &&
         (theline = theline->next) != NULL &&
         theLineType(*theline) != CLOSEPATH &&
         theLineType(*theline) != MYCLOSE ) {
      tt_marked = TRUE ;
      (void)path_transform_bbox(&tt_path, &tt_bbox, BBOX_IGNORE_NONE,
                                &tt_ifmatrix) ;
    } else {
      HQASSERT((thepath = tt_path.firstpath) == NULL ||
               ((theline = theSubPath(*thepath)) != NULL &&
                theLineType(*theline) == MOVETO &&
                ((theline = theline->next) == NULL ||
                 theLineType(*theline) == CLOSEPATH)),
               "Blank TT character path incorrect") ;
      tt_marked = FALSE ;
      bbox_store(&tt_bbox, 0, 0, 0, 0) ;
    }

    bbox_as_indexed(bbindexed, &tt_bbox) ;

    /* XPS vertical metric calculation */
    if ( xps &&
         !get_xps_metrics( ttfns, tt_font, &charcontext->glyphname,
                           bbindexed, metrics, &mflags, TRUE) )
      return error_handler( INVALIDFONT ) ;

    result = char_metrics(charcontext, metrics, &mflags, bbindexed) ;

    if ( result && showtype != DOSTRINGWIDTH ) {
      uint32 accurate = 0 ;
      SYSTEMVALUE glyphsize ;

      result = char_accurate(&glyphsize, &accurate) ;

      if ( result ) {
        CHARCACHE *cptr = NULL ;
        SYSTEMVALUE xoff = 0.0, yoff = 0.0 ;
        static OMATRIX tt_dmatrix = { /* Matrix from scaled dspace to cache dspace */
          1.0, 0.0, 0.0, 1.0, 0.0, 0.0, MATRIX_OPT_0011
        } ;

        MATRIX_00(&tt_dmatrix) = tt_font_scale ;
        MATRIX_11(&tt_dmatrix) = tt_font_scale ;

        if ( showtype != DOCHARPATH ) {
          SYSTEMVALUE bearings[4] ;
          char_bearings(charcontext, bearings, bbindexed, &theFontMatrix(*fontInfo)) ;
          cptr = char_cache(charcontext, metrics, mflags, bearings, !tt_marked) ;
          xoff = bearings[0] ;
          yoff = bearings[1] ;
        }

        result = char_draw(charcontext, currpt, cptr, metrics, mflags,
                           xoff, yoff, !tt_marked, accurate,
                           &tt_path, &tt_dmatrix) ;
      }
    }
  }

#ifdef DEBUG_PICTURE
  if ( (debug_tt & DEBUG_TT_PICTURE) != 0 )
    monitorf((uint8 *)"EndChar\n");
#endif

  /* Free the TT path if it was not taken over by char_draw. */
  path_free_list(tt_path.firstpath, mm_pool_temp);
  path_init(&tt_path) ;

  return result;
}

/*****************************************************************************/
/* Generic TrueType table decoding */

#define TT_skip( _mem , _offset , _number ) MACRO_START \
MACRO_END

#define TT_fixed( _mem , _offset , _val ) MACRO_START   \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = (( _lmem_[ 0 ] << 24 ) |                     \
            ( _lmem_[ 1 ] << 16 ) |                     \
            ( _lmem_[ 2 ] <<  8 ) |                     \
            ( _lmem_[ 3 ] <<  0 )) ;                    \
MACRO_END

#define TT_ulong( _mem , _offset , _val ) MACRO_START   \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = ( uint32 )(( _lmem_[ 0 ] << 24 ) |           \
                      ( _lmem_[ 1 ] << 16 ) |           \
                      ( _lmem_[ 2 ] <<  8 ) |           \
                      ( _lmem_[ 3 ] <<  0 )) ;          \
MACRO_END

#define TT_long( _mem , _offset , _val ) MACRO_START    \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = ( int32 )(( _lmem_[ 0 ] << 24 ) |            \
                     ( _lmem_[ 1 ] << 16 ) |            \
                     ( _lmem_[ 2 ] <<  8 ) |            \
                     ( _lmem_[ 3 ] <<  0 )) ;           \
MACRO_END

#define TT_ushort( _mem , _offset , _val ) MACRO_START  \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = ( uint16 )(( _lmem_[ 0 ] << 8 ) |            \
                      ( _lmem_[ 1 ] << 0 )) ;           \
MACRO_END

#define TT_offset TT_ushort

#define TT_short( _mem , _offset , _val ) MACRO_START   \
  uint8 *_lmem_ ;                                       \
  int32 _tmp_ ;                                         \
  _lmem_ = (_mem) + (_offset) ;                         \
  _tmp_ = (( _lmem_[ 0 ] << 8 ) |                       \
           ( _lmem_[ 1 ] << 0 )) ;                      \
  if ( _tmp_ & 0x8000 )                                 \
    _tmp_ |= 0xFFFF0000 ;                               \
  (_val) = ( int16 )_tmp_ ;                             \
MACRO_END

#define TT_char( _mem , _offset , _val ) MACRO_START    \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = ( int8 )( _lmem_[ 0 ] ) ;                    \
MACRO_END

#define TT_byte( _mem , _offset , _val ) MACRO_START    \
  uint8 *_lmem_ ;                                       \
  _lmem_ = (_mem) + (_offset) ;                         \
  (_val) = ( uint8 )( _lmem_[ 0 ] ) ;                   \
MACRO_END

#define TT_fword( _mem , _offset , _val ) TT_short( _mem , _offset , _val )

static TT_CMAP_ENTRY *tt_findCmap(TT_DATA *TT_data,
                                  PEL_PRIORITY priorities[],
                                  uint32 npriorities) ;


/* -------------------------------------------------------------------------- */

static void tt_free_cmap_entry(TT_CMAP_ENTRY *cmape)
{
  int32 i ;

  if ( cmape ) {
    for ( i = 0 ; i < cmape->nSegments ; ++i ) {
      TT_CMAP_SEGMENT *cmaps = cmape->segments[i] ;

      if ( cmaps )
        mm_free(mm_pool_temp, (mm_addr_t)cmaps,
                TT_CMAP_SEGMENT_SIZE(cmaps->startCode, cmaps->endCode, cmaps->useOffset)) ;
    }
    mm_free(mm_pool_temp, (mm_addr_t)cmape, cmape->size) ;
  }
}

static void tt_freeFont(TT_DATA *TT_data)
{
  uint16 numTables ;
  TT_TABLE *Tables ;
  TT_CMAP_ENTRY *cmap ;
  uint16 numGlyphs ;
  NAMECACHE **GlyphNames ;

  int32 i ;

  numTables = TT_data->numTables ;
  Tables    = TT_data->Tables ;
  if ( Tables && numTables != 0 ) {
    mm_free( mm_pool_temp , ( mm_addr_t )Tables , numTables * sizeof( TT_TABLE )) ;
    TT_data->numTables = 0 ;
    TT_data->Tables = NULL ;
  }

  numGlyphs  = TT_data->tt_post.numPostGlyphs ;
  GlyphNames = TT_data->tt_post.GlyphNames ;
  if ( GlyphNames && numGlyphs ) {
    mm_free( mm_pool_temp , ( mm_addr_t )GlyphNames , numGlyphs * sizeof(NAMECACHE *)) ;
    TT_data->tt_post.numPostGlyphs = 0 ;
    TT_data->tt_post.GlyphNames = NULL ;
  }

  for ( i = 0 ; i < TT_NAME_MAX ; ++i ) {
    TT_NF *tt_name = &TT_data->tt_name.names[i] ;
    if ( tt_name->str && tt_name->len > 0 ) {
      mm_free(mm_pool_temp, (mm_addr_t)tt_name->str, tt_name->len) ;
      tt_name->len = 0 ;
      tt_name->str = NULL ;
    }
  }

  cmap = TT_data->tt_cmap.cmaps ;
  while ( cmap ) {
    TT_CMAP_ENTRY *next = cmap->next ;
    tt_free_cmap_entry(cmap) ;
    cmap = next ;
  }
  TT_data->tt_cmap.cmaps = NULL ;

  tt_free_Xmtx( & TT_data->tt_hmtx ) ;
  tt_free_Xmtx( & TT_data->tt_vmtx ) ;
  tt_free_vorg( & TT_data->tt_vorg ) ;
}

/* -------------------------------------------------------------------------- */
static Bool tt_initFontCollection(TT_CACHE *TT_cache)
{
  uint32 ttctag, numFonts ;
  int32 ttcver ;
  uint8 *ttmem ;
  charstring_methods_t *ttfns ;

  HQASSERT(TT_cache, "No TT cache") ;
  ttfns = &TT_cache->ttmethods ;

  HqMemZero(&TT_cache->tt_data, sizeof(TT_DATA)) ; /* In case of failure */

  TT_cache->type = TT_FONT_NONE ;
  TT_cache->numFonts = 0 ;
  TT_cache->fontindex = -1 ;

  /* Read the header Tag, and decide if this is a TTC or a TTF. Since the
   * TTF header length is an offset table, we can just
   * this is done by reading the header, which contains:
   *  Type      Name            Description
   *  TAG       TTCtag          'ttcf'
   *  Fixed     version         1.0 or 2.0 (2.0 has digital sig after offsets)
   *  ULONG     numFonts        Number of fonts in collection.
   *  ULONG     OffsetTable[numFonts]  Start of offset tables for sub-fonts
   * v2.0 then adds:
   *  TAG       dsigTag         'DSIG'
   *  ULONG     dSigLength      Length of digital signature
   *  ULONG     dSigOffset      Offset to digital signature
   */

  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, 0, TTC_SIZE_HEADER)) == NULL )
    return error_handler(INVALIDFONT) ;

  TT_ulong( ttmem ,  0 , ttctag ) ;
  TT_fixed( ttmem ,  4 , ttcver ) ;
  TT_ulong( ttmem ,  8 , numFonts ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  if ( ttctag == 0x00010000 || ttctag == tt_maptag("true") ) {
    TT_cache->type = TT_FONT_TTF ; /* Normal TTF file */
    TT_cache->numFonts = 1 ;
  } else if ( ttctag == tt_maptag("ttcf") ) {
    TT_cache->type = TT_FONT_TTC ; /* TrueType Collection */
    TT_cache->numFonts = numFonts ;
    if ( ttcver != 0x00010000u && ttcver != 0x00020000u ) {
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid ttcver: %08x", ttcver));
      return error_handler( INVALIDFONT ) ;
    }
  } else if ( ttctag == tt_maptag("OTTO") ) {
    TT_cache->type = TT_FONT_OTTO ; /* OpenType CFF file */
    TT_cache->numFonts = 1 ;
  } else {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid sfntver: %08x", ttctag));
    return error_handler( INVALIDFONT ) ;
  }

  TT_cache->fontindex = -1 ; /* Note that no font is loaded */

  return TRUE ;
}

static Bool tt_initFont(TT_CACHE *TT_cache, int32 index)
{
  /* First of all check it's a TT font.
   * this is done by reading the header, which contains:
   *  Type      Name            Description
   *  Fixed     sfnt version    0x00010000 for version 1.0.
   *  USHORT    numTables       Number of tables.
   *  USHORT    searchRange     (Maximum power of 2 <= numTables) x 16.
   *  USHORT    entrySelector   Log2(maximum power of 2 <= numTables).
   *  USHORT    rangeShift      NumTables x 16-searchRange.
   */

  uint16 numTables , searchRange , entrySelector , rangeShift ;
  uint32 sfntver ;
  uint8 *ttmem ;
  TT_TABLE *Tables ;
  TT_DATA *TT_data ;
  uint32 i ;
  uint32 position = 0 ;
  charstring_methods_t *ttfns ;

  HQASSERT(TT_cache, "No TT cache") ;

  TT_data = &TT_cache->tt_data ;
  HqMemZero(TT_data, sizeof(TT_DATA)) ; /* In case of failure */

  ttfns = &TT_cache->ttmethods ;

  if ( index < 0 || (uint32)index >= TT_cache->numFonts )
    return error_handler(INVALIDFONT) ;

  if ( TT_cache->type == TT_FONT_TTC ) {
    /* Convert the font index to an offset to the appropriate font's offset
       table. */
    if ( (ttmem = (*ttfns->open_frame)(ttfns->data, TTC_SIZE_HEADER,
                                       TTC_SIZE_OFFSET * TT_cache->numFonts)) == NULL )
      return error_handler(INVALIDFONT) ;

    TT_ulong(ttmem, index * TTC_SIZE_OFFSET, position) ;

    (*ttfns->close_frame)(ttfns->data, &ttmem) ;
  }

  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, position, TT_SIZE_HEADER)) == NULL )
    return error_handler(INVALIDFONT) ;

  TT_fixed ( ttmem ,  0 , sfntver ) ;
  TT_ushort( ttmem ,  4 , numTables ) ;
  TT_ushort( ttmem ,  6 , searchRange ) ;
  TT_ushort( ttmem ,  8 , entrySelector ) ;
  TT_ushort( ttmem , 10 , rangeShift ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  if ( sfntver != 0x00010000 &&
       sfntver != tt_maptag("true") &&
       sfntver != tt_maptag("OTTO") ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid sfntver: %08x",sfntver));
    return error_handler( INVALIDFONT ) ;
  }

  if ( numTables == 0 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("no tables: %.d",numTables));
    return error_handler( INVALIDFONT ) ;
  }

  if ( (Tables = mm_alloc(mm_pool_temp, numTables * sizeof(TT_TABLE),
                          MM_ALLOC_CLASS_TTFONT)) == NULL )
    return error_handler( VMERROR ) ;

  /* For now don't bother checking "searchRange, entrySelector & rangeShift". */

  /* (2) Read in and cache all the headers, checking they are valid.
   *  Type      Name            Description
   *  ULONG     tag             4 -byte identifier.
   *  ULONG     checkSum        CheckSum for this table.
   *  ULONG     offset          Offset from beginning of TrueType font file.
   *  ULONG     length          Length of this table.
   */

  position += TT_SIZE_HEADER ;
  for ( i = 0 ; i < numTables ; ++i ) {
    uint32 tag , checkSum , offset , length ;
    uint8 *ttmem ;

    if ( (ttmem = (*ttfns->open_frame)(ttfns->data, position,
                                       TT_SIZE_TABLE)) == NULL ) {
      mm_free(mm_pool_temp, (mm_addr_t)Tables, numTables * sizeof(TT_TABLE)) ;
      return error_handler( INVALIDFONT ) ;
    }

    TT_ulong( ttmem ,  0 , tag ) ;
    TT_ulong( ttmem ,  4 , checkSum ) ;
    TT_ulong( ttmem ,  8 , offset ) ;
    TT_ulong( ttmem , 12 , length ) ;

    (*ttfns->close_frame)(ttfns->data, &ttmem) ;

    Tables[ i ].tag      = tag ;
    Tables[ i ].checkSum = checkSum ;
    Tables[ i ].offset   = offset ;
    Tables[ i ].length   = length ;

#if defined(TT_CHECK_CHECKSUMS)
    {
      uint32 checksum = tt_schecksum(ttfns, offset , length ) ;
      if ( checksum != checkSum ) {
        mm_free(mm_pool_temp, (mm_addr_t)Tables, numTables * sizeof(TT_TABLE)) ;
        return error_handler( INVALIDFONT ) ;
      }
    }
#endif

    position += TT_SIZE_TABLE ;
  }

  TT_data->version = sfntver ;
  TT_data->Tables = Tables ;
  TT_data->numTables = numTables ;

  TT_cache->fontindex = index ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static TT_TABLE *tt_findTable(TT_DATA *TT_data, uint32 tag)
{
  int32 i, numTables ;
  TT_TABLE *Tables ;

  HQASSERT(TT_data, "No TT font data") ;

  numTables = TT_data->numTables ;
  Tables = TT_data->Tables ;

  for ( i = 0 ; i < numTables ; ++i )
    if ( Tables[ i ].tag == tag && Tables[ i ].length )
      return ( & Tables[ i ] ) ;

  return NULL ;
}

/* -------------------------------------------------------------------------- */
static Bool tt_readheadTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  /* (3) Read in and cache all the 'head' header, checking it is valid.
   *  Type      Name                    Description
   *  Fixed     Table version number    0x00010000 for version 1.0.
   *  Fixed     fontRevision            Set by font manufacturer.
   *  ULONG     checkSumAdjustment      To compute:  set it to 0, sum the entire font
   *                                    as ULONG, then store 0xB1B0AFBA - sum.
   *  ULONG     magicNumber             Set to 0x5F0F3CF5.
   *  USHORT    flags                   Bit 0 - baseline for font at y=0;
   *                                    Bit 1 - left sidebearing at x=0;
   *                                    Bit 2 - instructions may depend on point size;
   *                                    Bit 3 - force ppem to integer values for all
   *                                    internal scaler math; may use fractional ppem
   *                                    sizes if this bit is clear;
   *                                    Bit 4 - instructions may alter advance width
   *                                    (the advance widths might not scale linearly);
   *                                    Note: All other bits must be zero.
   *  USHORT    unitsPerEm              Valid range is from 16 to 16384
   *  LDT(*)    created                 International date (8-byte field).
   *  LDT(*)    modified                International date (8-byte field).
   *  FWord     xMin                    For all glyph bounding boxes.
   *  FWord     yMin                    For all glyph bounding boxes.
   *  FWord     xMax                    For all glyph bounding boxes.
   *  FWord     yMax                    For all glyph bounding boxes.
   *  USHORT    macStyle                Bit 0 bold (if = 1); Bit 1 italic (if = 1)
   *                                    Bits 2-15 reserved (set to 0).
   *  USHORT    lowestRecPPEM           Smallest readable size in pixels.
   *  SHORT     fontDirectionHint        0   Fully mixed directional glyphs;
   *                                     1   Only strongly left to right;
   *                                     2   Like 1 but also contains neutrals ;
   *                                    -1   Only strongly right to left;
   *                                    -2   Like -1 but also contains neutrals.
   *  SHORT     indexToLocFormat        0 for short offsets, 1 for long.
   *  SHORT     glyphDataFormat         0 for current format.
   *
   * (*) LDT == longDateTime
   */

   int32 Tableversionnumber ;
   int32 fontRevision ;
  uint32 checkSumAdjustment ;
  uint32 magicNumber ;
  uint16 flags ;
  uint16 unitsPerEm ;
   int16 xMin , yMin , xMax , yMax ;
  uint16 macStyle ;
  uint16 lowestRecPPEM ;
   int16 fontDirectionHint ;
   int16 indexToLocFormat ;
   int16 glyphDataFormat ;
   uint8 *ttmem ;

  TT_HEAD *Head_data = ( & TT_data->tt_head ) ;
  TT_TABLE *Tab_head ;

  if ( Head_data->tried )
    return TRUE ;

  Head_data->tried = TRUE ;
  Head_data->repaired = FALSE ;
  if ( (Tab_head = tt_findTable(TT_data, tt_maptag("head"))) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, Tab_head->offset,
                                    TT_SIZE_HEAD)) == NULL )
    return error_handler( INVALIDFONT ) ;

  TT_fixed ( ttmem ,  0 , Tableversionnumber ) ;
  TT_fixed ( ttmem ,  4 , fontRevision ) ;
  TT_ulong ( ttmem ,  8 , checkSumAdjustment ) ;
  TT_ulong ( ttmem , 12 , magicNumber ) ;
  TT_ushort( ttmem , 16 , flags ) ;
  TT_ushort( ttmem , 18 , unitsPerEm ) ;
  TT_skip  ( ttmem , 20 , 8 ) ;
  TT_skip  ( ttmem , 28 , 8 ) ;
  TT_fword ( ttmem , 36 , xMin ) ;
  TT_fword ( ttmem , 38 , yMin ) ;
  TT_fword ( ttmem , 40 , xMax ) ;
  TT_fword ( ttmem , 42 , yMax ) ;
  TT_ushort( ttmem , 44 , macStyle ) ;
  TT_ushort( ttmem , 46 , lowestRecPPEM ) ;
  TT_short ( ttmem , 48 , fontDirectionHint ) ;
  TT_short ( ttmem , 50 , indexToLocFormat ) ;
  TT_short ( ttmem , 52 , glyphDataFormat ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  /* Strictly, Tableversionnumber should be either 0x10000 for version 1. There
   * isn't a version 2 yet, but there are some bad fonts with funny numbers.
   */
  if ( Tableversionnumber >= 0x00020000 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid Tableversionnumber: %08x",Tableversionnumber));

    /* Tableversion can only be 1, so an incorrect version number is a
     * repairable error - assume version is 1 and judge the validity of rest
     * of table as if it is version 1. */
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("repair head table version")) ;
    Tableversionnumber = 0x00010000 ;
    Head_data->repaired = TRUE ;
  }

  if ( magicNumber != 0x5F0F3CF5 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid magicNumber: %08x",magicNumber));
    return error_handler( INVALIDFONT ) ;
  }

  if (( flags & ~0x1F) != 0 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid flags: %08x",flags));
#ifdef TT242_RESERVED_FLAGS_ZERO
    return error_handler( INVALIDFONT ) ;
#endif
  }

  if ( unitsPerEm < 16 || unitsPerEm > 16384 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid unitsPerEm: %d",unitsPerEm));
    return error_handler( INVALIDFONT ) ;
  }

  Head_data->fontRevision = fontRevision ;

  Head_data->unitsPerEm = unitsPerEm ;

  Head_data->xMin = xMin ;
  Head_data->yMin = yMin ;
  Head_data->xMax = xMax ;
  Head_data->yMax = yMax ;

  HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0 && xMin>xMax, ("bad xMin/Max: %d,%d",xMin,xMax));
  HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0 && yMin>yMax, ("bad yMin/Max: %d,%d",yMin,yMax));

  if (( macStyle & ~0x03) != 0 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid macStyle: %08x",macStyle));
#ifdef TT242_RESERVED_FLAGS_ZERO
    return error_handler( INVALIDFONT ) ;
#endif
  }

/* We don't need to check these, but I'll leave them here in case we need to in future. */

  if ( fontDirectionHint < -2 || fontDirectionHint > 2 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid fontDirectionHint: %d",fontDirectionHint));
    /*return error_handler( INVALIDFONT ) ;*/
  }

  if ( indexToLocFormat != 0 && indexToLocFormat != 1 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid indexToLocFormat: %d",indexToLocFormat));
    /*return error_handler( INVALIDFONT ) ;*/
  }

  if ( glyphDataFormat != 0 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid glyphDataFormat: %d",glyphDataFormat));
    /*return error_handler( INVALIDFONT ) ;*/
  }

  Head_data->loaded = TRUE ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool tt_readmaxpTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  /* (4) Read in and cache all the 'maxp' header, checking it is valid.
   * Type       Name                    Description
   * Fixed      Table version number    0x00010000 for version 1.0.
   * USHORT     numGlyphs               The number of glyphs in the font.
   * ...
   */

  int32 version ;
  uint16 numGlyphs ;
  uint8 *ttmem ;
  TT_TABLE *Tab_maxp ;

  TT_MAXP *Maxp_data = ( & TT_data->tt_maxp ) ;

  if ( Maxp_data->tried )
    return TRUE ;

  Maxp_data->tried = TRUE ;
  if ( (Tab_maxp = tt_findTable(TT_data, tt_maptag("maxp"))) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  if ( Tab_maxp->length < 6 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0, ("Invalid maxp length"));
    return error_handler( INVALIDFONT ) ;
  }

  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, Tab_maxp->offset,
                                    TT_SIZE_MAXP)) == NULL )
    return error_handler( INVALIDFONT ) ;

  TT_fixed ( ttmem , 0 , version ) ;
  TT_ushort( ttmem , 4 , numGlyphs ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  Maxp_data->numGlyphs = numGlyphs ;
  Maxp_data->loaded = TRUE ;

  if ( version == 0x00010000 ||    /* for TT & TT OpenType */
       version == 0x00005000 ) {   /* for CFF OpenType     */
    return TRUE ;
  }

  HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0, ("Invalid maxp version: %d",version));

  /* Bad version number, so check major version and exact length */
  version >>= 16 ;
  if ( (version == 0 && Tab_maxp->length == 6) ||
       (version == 1 && Tab_maxp->length == 32) ) {
    return TRUE ;
  }

  Maxp_data->loaded = FALSE ;

  return error_handler( INVALIDFONT ) ;
}

/* ========================================================================== *
 * tt_readcmapTable - modularised [62037]
 *
 * Each format has been separated out to keep tt_readcmapTable manageable
 * ========================================================================== */

TT_CMAP_ENTRY * read_cmap_format_0(
  charstring_methods_t *ttfns, uint32 offset, uint32 length32,
  uint16 PlatformID, uint16 EncodeID, uint16 LanguageID)
{
  TT_CMAP_SEGMENT *cmaps ;
  TT_CMAP_ENTRY *cmape ;
  uint16 *glyphIdArray ;
  uint16 length = (uint16)length32 ;
  uint8 *ttmem ;
  int32 size, i ;

  HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,("cmap format: 0"));

  if (length32 > 65535) {
    (void)error_handler(INVALIDFONT) ;
    return NULL ;
  }

  size = TT_CMAP_ENTRY_SIZE(1) ;
  if ( (cmape = mm_alloc(mm_pool_temp, size,
                         MM_ALLOC_CLASS_TTFONT)) == NULL ) {
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  cmape->size       = size ;
  cmape->next       = NULL ;
  cmape->nSegments  = 1 ;
  cmape->PlatformID = PlatformID ;
  cmape->EncodeID   = EncodeID ;
  cmape->LanguageID = LanguageID ;
  cmape->repaired   = FALSE ;
  if ( (cmape->segments[0] = cmaps = mm_alloc(mm_pool_temp,
                                              TT_CMAP_SEGMENT_SIZE(0, 255, FALSE),
                                              MM_ALLOC_CLASS_TTFONT)) == NULL ) {
    tt_free_cmap_entry(cmape) ;
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  cmaps->startCode = 0 ;
  cmaps->endCode   = 255 ;
  cmaps->useOffset = FALSE ;

  if ( length < TT_SIZE_CMAP_TABLE_DATA_0 ||
       (ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                                     TT_SIZE_CMAP_TABLE_DATA_0)) == NULL ) {
    tt_free_cmap_entry(cmape) ;
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
  }

  glyphIdArray = &cmaps->glyphIndex[0] ;
  for ( i = 0 ; i < 256 ; ++i ) {
    uint8 glyphId ;
    TT_byte( ttmem , i , glyphId ) ;
    glyphIdArray[ i ] = glyphId ;
  }

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  return cmape ;
}

/* -------------------------------------------------------------------------- */

TT_CMAP_ENTRY * read_cmap_format_2(
  charstring_methods_t *ttfns, uint32 offset, uint32 length32,
  uint16 PlatformID, uint16 EncodeID, uint16 LanguageID)
{
  TT_CMAP_ENTRY *cmape ;
  uint16 segCount = 0 ; /* Number of segments */
  uint16 soffset = 0 ;  /* Highest subheader offset */
  uint16 length = (uint16)length32 ;
  int32  size, i ;
  uint8  *ttmem ;

  HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,("cmap format: 2"));

  if (length32 > 65535) {
    (void)error_handler(INVALIDFONT) ;
    return NULL ;
  }

  /*   +
   * Type   Name                    Description
   * USHORT subHeaderKeys[256]      Array that maps high bytes to
   *                                subHeaders; value is subHeader
   *                                index * 8
   * 4 words struct subHeaders[]    Variable-length array of subHeader structures
   * USHORT glyphIndexArray[ ]      Variable-length array containing
   *                                subarrays used for mapping the low byte
   *                                of 2-byte characters.
   *
   * A subHeader is structured as follows:
   * Type   Name                    Description
   * USHORT firstCode               First valid low byte for this subHeader
   * USHORT entryCount              Number of valid low bytes for this subHeader
   * SHORT idDelta                  Delta for all character codes in a subHeader
   * USHORT idRangeOffset           Offset into glyphIndexArray for first code
   */

  if ( length < TT_SIZE_CMAP_TABLE_DATA_2_1 ||
       (ttmem = (*ttfns->open_frame)(ttfns->data, offset, length)) == NULL ) {
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
  }

  /* The Microsoft definition of the cmap type 2 table is very ambiguous.
     The Apple definition at
     http://developer.apple.com/fonts/TTRefMan/RM06/Chap6cmap.html should
     be read instead. Subheader index 0 should only be used for
     single-byte codes, and other high bytes should indicate that they
     override the low byte table by using a different subheader index.
     The high byte 0 table may cover the whole byte range (but fairly
     obviously must be overridden by other high bytes, or it would only
     be an 8-bit encoding). */

  for ( i = 0 ; i < 256 ; ++i ) {
    uint16 lsoffset ;

    TT_ushort( ttmem , i << 1 , lsoffset ) ;

    if ( lsoffset != 0 || i == 0 )
      ++segCount ;

    /* Value is a subheader offset, it must be a multiple of subheader
       size. */
    if ( lsoffset % TT_SIZE_CMAP_TABLE_DATA_2_2 != 0 ) {
      (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      (void)error_handler( INVALIDFONT ) ;
      return NULL ;
    }

    if ( lsoffset > soffset )
      soffset = lsoffset ;
  }

  size = TT_CMAP_ENTRY_SIZE(segCount) ;
  if ( (cmape = mm_alloc(mm_pool_temp, size,
                         MM_ALLOC_CLASS_TTFONT)) == NULL ) {
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  cmape->size       = size ;
  cmape->next       = NULL ;
  cmape->nSegments  = segCount ;
  cmape->PlatformID = PlatformID ;
  cmape->EncodeID   = EncodeID ;
  cmape->LanguageID = LanguageID ;
  cmape->repaired   = FALSE ;
  for ( i = 0 ; i < segCount ; ++i )
    cmape->segments[i] = NULL ;

  /* Start of glyphIndexArray[] */
  soffset += TT_SIZE_CMAP_TABLE_DATA_2_1 + TT_SIZE_CMAP_TABLE_DATA_2_2 ;
  if ( soffset > length ) {
    (*ttfns->close_frame)(ttfns->data, &ttmem) ;
    tt_free_cmap_entry(cmape) ;
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
  }

  for ( segCount = 0, i = 0 ; i < 256 ; ++i ) {
    TT_CMAP_SEGMENT *cmaps ;
    uint16 *glyphIdArray ;
    uint16 lsoffset ;
    uint16 firstCode ;
    uint16 entryCount ;
    int16  idDelta ;
    uint16 idRangeOffset ;
    uint16 endCode ;
    uint16 currCode ;

    TT_ushort( ttmem , i << 1 , lsoffset ) ;

    if ( lsoffset == 0 && i != 0 )
      continue ;

    /* Read subheader struct to get size */
    lsoffset += TT_SIZE_CMAP_TABLE_DATA_2_1 ;
    TT_ushort( ttmem , lsoffset , firstCode ) ;
    TT_ushort( ttmem , lsoffset + 2u, entryCount ) ;
    TT_short ( ttmem , lsoffset + 4u, idDelta ) ;
    TT_ushort( ttmem , lsoffset + 6u, idRangeOffset ) ;

    /* [65013] Ignore segments that are malformed. */
    if ( entryCount == 0 ) {
      --cmape->nSegments ;
      continue ;
    }

    HQASSERT(firstCode < 256,
             "cmap table 2 firstCode should be a low byte") ;
    /* entryCount==0 has been seen, so is handled politely */
    HQASSERT(firstCode + entryCount <= 256,
             "cmap table 2 segment overlaps next high byte range") ;

    firstCode = CAST_TO_UINT16(firstCode | (i << 8)) ;
    endCode = (uint16)(firstCode + entryCount - 1) ;

    /* Turn lsoffset into an offset into glyphIndexArray[]. idRangeOffset
       is relative to its own storage location. */
    lsoffset = (uint16)(lsoffset + 6u + idRangeOffset) ;

    if ( lsoffset + (entryCount << 1) > length ||
         (cmape->segments[segCount++] = cmaps =
          mm_alloc(mm_pool_temp,
                   TT_CMAP_SEGMENT_SIZE(firstCode, endCode, FALSE),
                   MM_ALLOC_CLASS_TTFONT)) == NULL ) {
      (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    cmaps->startCode = firstCode ;
    cmaps->endCode   = endCode ;
    cmaps->useOffset = FALSE ;

    glyphIdArray = &cmaps->glyphIndex[0] ;

    ++endCode;  /* deliberate wrap when 0xffff for the following */
                /* note that endCode is not used thereafter      */
    for ( currCode = firstCode ; currCode != endCode ; ++currCode, lsoffset += 2 ) {
      uint16 glyphId ;

      TT_ushort(ttmem, lsoffset, glyphId) ;

      if ( glyphId != 0 ) {
        glyphId = (uint16)(glyphId + idDelta) ; /* Note deliberate overflows and wraps */
        if ( glyphId == 0xFFFF )
          glyphId = 0 ;
      }

      glyphIdArray[currCode - firstCode] = glyphId ;
    }
  }
  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  HQASSERT(segCount >= cmape->nSegments,
           "Mismatch between counted segments and filled segments") ;

  return cmape ;
}

/* -------------------------------------------------------------------------- */

TT_CMAP_ENTRY * read_cmap_format_4(
  charstring_methods_t *ttfns, uint32 offset, uint32 length32,
  uint16 PlatformID, uint16 EncodeID, uint16 LanguageID)
{
  TT_CMAP_SEGMENT *fake = 0 ;
  TT_CMAP_ENTRY *cmape ;
  uint16 *glyphIdArray ;
  uint16 segCountX2 ;
  uint16 searchRange ;
  uint16 entrySelector ;
  uint16 rangeShift ;
  uint16 segCount ;
  uint16 soffset ;
  uint16 length = (uint16)length32 ;
  int32  size, i ;
  uint8  *ttmem ;

  HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,("cmap format: 4"));

  if (length32 > 65535) {
    (void)error_handler(INVALIDFONT) ;
    return NULL ;
  }

  /*   +
   * Type   Name                    Description
   * USHORT segCountX2              2 x segCount.
   * USHORT searchRange             2 x (2**floor(log2(segCount)))
   * USHORT entrySelector           log2(searchRange/2)
   * USHORT rangeShift              2 x segCount - searchRange
   * USHORT endCount[segCount]      End characterCode for each segment, last =0xFFFF.
   * USHORT reservedPad             Set to 0.
   * USHORT startCount[segCount]    Start character code for each segment.
   * USHORT idDelta[segCount]       Delta for all character codes in segment.
   * USHORT idRangeOffset[segCount] Offsets into glyphIdArray or 0
   * USHORT glyphIdArray[ ]         Glyph index array (arbitrary length)
   */
  if ( length < TT_SIZE_CMAP_TABLE_DATA_4 ||
       (ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                                     TT_SIZE_CMAP_TABLE_DATA_4)) == NULL ) {
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
  }

  TT_ushort( ttmem , 0 , segCountX2 ) ;
  TT_ushort( ttmem , 2 , searchRange ) ;
  TT_ushort( ttmem , 4 , entrySelector ) ;
  TT_ushort( ttmem , 6 , rangeShift ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  offset += TT_SIZE_CMAP_TABLE_DATA_4 ;
  length -= TT_SIZE_CMAP_TABLE_DATA_4 ;

  segCount = ( uint16 )( segCountX2 >> 1 ) ;
  if (( segCountX2 & 1 ) != 0 || segCount == 0 ) {
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
  }

  for ( i = 0 ; i < 16 ; ++i )
    if (( 1 << i ) > segCount )
      break ;
  /* There are just too many fonts out there with bogus values for the
     searchRange and entrySelector. Since we don't actually use them,
     rely on segCount being correct, and allow trace messages to detect
     broken fonts. See request 24915. */
  if (( 1 << i ) != searchRange ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
            ("invalid cmap searchRange: %d != %d", (1 << i), searchRange));
#ifdef TT_CMAP_ERRORS
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
#endif
  }
  if (( i - 1 ) != entrySelector ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
            ("invalid cmap entrySelector: %d != %d", i - 1, searchRange));
#ifdef TT_CMAP_ERRORS
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
#endif
  }
  if ( segCountX2 - searchRange != rangeShift ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
            ("invalid cmap rangeShift: %d != %d",
             segCountX2 - searchRange, searchRange));
#ifdef TT_CMAP_ERRORS
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
#endif
  }

  /* [50969] make a faked 8bit range, which will be filled in from other ranges.
     Note: if actual 0x00-0xFF ranges are found, they will not be removed,
     this is a harmless duplication, but it's very unlikely indeed. */
  if ( PlatformID == 3 && EncodeID == 0 ) {
    ++segCount ;
    if ( (fake = mm_alloc(mm_pool_temp,
                          TT_CMAP_SEGMENT_SIZE(TT_FAKE_MIN_CODE,TT_FAKE_MAX_CODE, FALSE),
                          MM_ALLOC_CLASS_TTFONT)) == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }
    fake->startCode = TT_FAKE_MIN_CODE ;
    fake->endCode   = TT_FAKE_MAX_CODE ;
    fake->useOffset = FALSE ;
    glyphIdArray = &fake->glyphIndex[0] ;
    for ( i = 0; i <= TT_FAKE_MAX_CODE-TT_FAKE_MIN_CODE; ++i )
      glyphIdArray[i] = 0;
  } else {
    fake = 0 ;
  }

  size = TT_CMAP_ENTRY_SIZE(segCount) ;
  if ( (cmape = mm_alloc(mm_pool_temp, size,
                         MM_ALLOC_CLASS_TTFONT)) == NULL ) {
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  cmape->size       = size ;
  cmape->next       = NULL ;
  cmape->nSegments  = segCount ;
  cmape->PlatformID = PlatformID ;
  cmape->EncodeID   = EncodeID ;
  cmape->LanguageID = LanguageID ;
  cmape->repaired   = FALSE ;
  for ( i = 0 ; i < segCount ; ++i )
    cmape->segments[i] = NULL ;

  if ( length < segCountX2 * 4 + TT_SIZE_CMAP_TABLE_DATA_4_2 ||
       (ttmem = (*ttfns->open_frame)(ttfns->data, offset, length)) == NULL ) {
    tt_free_cmap_entry(cmape) ;
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
  }

  for ( soffset = 0, i = 0 ; ; soffset += 2, ++i ) {
    TT_CMAP_SEGMENT *cmaps ;
    uint16 endCode ;
    uint16 startCode ;
    uint16 idDelta ;
    uint16 idRangeOffset ;
    uint16 lsoffset = soffset ;
    uint16 currCode ;

    /* if we are faking, add the fake range first */
    if ( i==0 && fake )
      cmape->segments[i++] = fake ;

    TT_ushort( ttmem , lsoffset , endCode ) ;
    lsoffset = (uint16)(lsoffset + segCountX2 + TT_SIZE_CMAP_TABLE_DATA_4_2) ;
    TT_ushort( ttmem , lsoffset, startCode ) ;
    lsoffset = (uint16)(lsoffset + segCountX2) ;
    TT_ushort( ttmem , lsoffset , idDelta ) ;
    lsoffset = (uint16)(lsoffset + segCountX2) ;
    TT_ushort( ttmem , lsoffset , idRangeOffset ) ;

    HQASSERT(endCode >= startCode, "cmap table 4 char codes out of order") ;

    if (endCode < startCode) { endCode = startCode; }

    if ( soffset + 2 == segCountX2 && endCode != 0xFFFF ) {
      (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      tt_free_cmap_entry(cmape) ;
      (void)error_handler( INVALIDFONT ) ;
      return NULL ;
    }

    /* [12467] Don't just stomp on memory if too many segments! */
    HQASSERT( i < segCount, "Too many cmap segments") ;
    if ( i >= segCount ) {
      (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      tt_free_cmap_entry(cmape) ;
      (void)error_handler( INVALIDFONT ) ;
      return NULL ;
    }

    if ( (cmape->segments[i] = cmaps =
          mm_alloc(mm_pool_temp, TT_CMAP_SEGMENT_SIZE(startCode, endCode, FALSE),
                   MM_ALLOC_CLASS_TTFONT)) == NULL ) {
      (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      tt_free_cmap_entry(cmape) ;
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    cmaps->startCode = startCode ;
    cmaps->endCode   = endCode ;
    cmaps->useOffset = FALSE ;

    glyphIdArray = &cmaps->glyphIndex[0] ;
    glyphIdArray[endCode - startCode ] = 0 ; /* Default final value */

    HQASSERT(endCode >= startCode, "Cmap segment out of order") ;

    /* TrueType spec says: "For the search to terminate, the final
       endCode value must be 0xFFFF. This segment need not contain any
       valid mappings. (It can just map the single character code
       0xFFFF to missingGlyph). However, the segment must be present."

       This is ambiguous; it can be interpreted to mean that all values
       in the final segment can be bogus, or that they might be valid
       (including a valid notdef mapping for 0xFFFF). Instead, we
       interpret as meaning the final code is bogus, and we ignore it
       by not executing the loop. The default initialisation above takes
       care of the mapping. */
    for ( currCode = startCode ; currCode != 0xFFFF ; ++currCode ) {
      uint16 actualCode ;

      if ( idRangeOffset == 0 ) {
        actualCode = ( uint16 )( idDelta + currCode ) ; /* Note deliberately overflows and wraps. */
        if ( actualCode == 0xFFFF )
          actualCode = 0 ;
      } else {
        uint32 goffset = (uint32)(lsoffset + idRangeOffset + ((currCode - startCode) << 1)) ;
        if ( goffset + 2 > length ) {
          /* [12422] Defer complaints of incomplete cmaps until it we actually try to access
           * the missing values.
           */
          actualCode = 0xFFFF ;
        } else {
          TT_ushort( ttmem , goffset , actualCode ) ;
          if ( actualCode != 0 )
            actualCode = ( uint16 )( idDelta + actualCode ) ; /* Note deliberately overflows and wraps. */
            if ( actualCode == 0xFFFF )
              actualCode = 0 ;
        }
      }
      glyphIdArray[ currCode - startCode ] = actualCode ;

      /* if we are faking, add this into the faked range, if we haven't already */
      if ( fake ) {
        uint16 fakeCode = CAST_UNSIGNED_TO_UINT16 ( currCode & 255 ) ;
        if (
#if TT_FAKE_MIN_CODE > 0
             fakeCode >= TT_FAKE_MIN_CODE &&
#endif
#if TT_FAKE_MAX_CODE < 255
             fakeCode <= TT_FAKE_MAX_CODE &&
#endif
             fake->glyphIndex[fakeCode-TT_FAKE_MIN_CODE] == 0 ) {
          fake->glyphIndex[fakeCode-TT_FAKE_MIN_CODE] = actualCode ;
        }
      }

      if ( currCode == endCode )
        break ;
    }

    if ( endCode == 0xFFFF )
      break ;
  }

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  /* [12467] If too few segments, correct header. Block will still be freed
   * correctly, thanks to new size member. */
  cmape->nSegments = (uint16) (i+1) ;

  return cmape ;
}

/* -------------------------------------------------------------------------- */

TT_CMAP_ENTRY * read_cmap_format_6(
  charstring_methods_t *ttfns, uint32 offset, uint32 length32,
  uint16 PlatformID, uint16 EncodeID, uint16 LanguageID)
{
  TT_CMAP_SEGMENT *cmaps ;
  TT_CMAP_ENTRY *cmape ;
  uint16 *glyphIdArray ;
  uint16 firstCode ;
  uint16 entryCount ;
  uint16 endCode ;
  uint16 currCode ;
  uint16 soffset ;
  uint16 length = (uint16)length32 ;
  uint8  *ttmem ;
  int32  size ;

  HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,("cmap format: 6"));

  if (length32 > 65535) {
    (void)error_handler(INVALIDFONT) ;
    return NULL ;
  }

  /*   +
   * Type   Name            Description
   * USHORT firstCode       First character code of subrange.
   * USHORT entryCount      Number of character codes in subrange.
   * USHORT glyphIdArray    Array of glyph index values for character
   *                        [entryCount]    codes in the range.
   */
  if ( length < TT_SIZE_CMAP_TABLE_DATA_6 ||
       (ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                                     TT_SIZE_CMAP_TABLE_DATA_6)) == NULL ) {
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
  }

  TT_ushort( ttmem , 0 , firstCode ) ;
  TT_ushort( ttmem , 2 , entryCount ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  endCode = (uint16)(firstCode + entryCount - 1) ;

  offset += TT_SIZE_CMAP_TABLE_DATA_6 ;
  length -= TT_SIZE_CMAP_TABLE_DATA_6 ;

  size = TT_CMAP_ENTRY_SIZE(1) ;
  if ( (cmape = mm_alloc(mm_pool_temp, size,
                         MM_ALLOC_CLASS_TTFONT)) == NULL ) {
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  cmape->size       = size ;
  cmape->next       = NULL ;
  cmape->nSegments  = 1 ;
  cmape->PlatformID = PlatformID ;
  cmape->EncodeID   = EncodeID ;
  cmape->LanguageID = LanguageID ;
  cmape->repaired   = FALSE ;
  if ( (cmape->segments[0] = cmaps = mm_alloc(mm_pool_temp,
                                              TT_CMAP_SEGMENT_SIZE(firstCode, endCode, FALSE),
                                              MM_ALLOC_CLASS_TTFONT)) == NULL ) {
    tt_free_cmap_entry(cmape) ;
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  cmaps->startCode = firstCode ;
  cmaps->endCode   = endCode ;
  cmaps->useOffset = FALSE ;

  glyphIdArray = &cmaps->glyphIndex[0] ;

  if ( length < 2u * entryCount ||
       (ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                                     2u * entryCount)) == NULL ) {
    tt_free_cmap_entry(cmape) ;
    (void)error_handler( INVALIDFONT ) ;
    return NULL ;
  }

  HQASSERT(endCode >= firstCode, "Cmap segment out of order") ;

  entryCount = (uint16)(entryCount + entryCount) ;
  for ( soffset = 0, currCode = firstCode ;
        soffset < entryCount ;
        soffset += 2, ++currCode ) {
    uint16 glyphId ;
    TT_ushort( ttmem , soffset , glyphId ) ;
    if ( glyphId == 0xFFFF )
      glyphId = 0 ;
    glyphIdArray[ currCode - firstCode ] = glyphId ;
  }

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  return cmape ;
}

/* -------------------------------------------------------------------------- */

TT_CMAP_ENTRY * read_cmap_format_12(
  charstring_methods_t *ttfns, uint32 offset, uint32 length,
  uint16 PlatformID, uint16 EncodeID, uint16 LanguageID)
{
  TT_CMAP_ENTRY *cmape ;
  uint32 nGroups, nFound = 0, nextCode = 0 ;
  uint32 i, size ;
  uint8  *ttmem ;
  int    error = 0 ;

  HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,("cmap format: 12"));
  /*   +
   * Type   Name                    Description
   * ULONG  nGroups                 The number of groups following...
   *
   * Each group is structured as follows:
   * ULONG  startCharCode           First character code in this group
   * ULONG  endCharCode             Last character code in this group
   * ULONG  startGlyphID            First glyph index in this group
   *
   * Groups must be in ascending order of startCharCode, with no overlaps.
   */

  if ( length < TT_SIZE_CMAP_TABLE_DATA_12 ||
       (ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                                     TT_SIZE_CMAP_TABLE_DATA_12)) == NULL ) {
    (void)error_handler(INVALIDFONT) ;
    return NULL ;
  }

  TT_ulong(ttmem, 0, nGroups) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;
  offset += TT_SIZE_CMAP_TABLE_DATA_12 ;
  length -= TT_SIZE_CMAP_TABLE_DATA_12 ;

  HQASSERT(nGroups < 10000, "Unfeasibly large cmap") ;
  if (nGroups > 65535u) {
    (void)error_handler(INVALIDFONT) ;
    return NULL ;
  }

  size = TT_CMAP_ENTRY_SIZE(nGroups) ;
  if ((cmape = mm_alloc(mm_pool_temp, size, MM_ALLOC_CLASS_TTFONT)) == NULL ) {
    (*ttfns->close_frame)(ttfns->data, &ttmem) ;
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  /* build the cmap entry */
  cmape->size       = size ;
  cmape->next       = NULL ;
  cmape->nSegments  = 0 ;
  cmape->PlatformID = PlatformID ;
  cmape->EncodeID   = EncodeID ;
  cmape->LanguageID = LanguageID ;
  cmape->repaired   = FALSE ;

  /* load each group */
  for ( i = 0; error == 0 && i < nGroups; ++i ) {

    /* If there's enough data, load this group */
    if (length < TT_SIZE_CMAP_TABLE_DATA_12_1) {
      /* Group header truncated! */
      /* We could handle the case of length==0 by correcting nGroups,
       * but this hasn't been seen in the wild. */

      error = FAILURE(INVALIDFONT) ;

    } else {
      if ((ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                    TT_SIZE_CMAP_TABLE_DATA_12_1)) == NULL ) {

        error = FAILURE(INVALIDFONT) ;

      } else {
        uint32 startCharCode, endCharCode, glyphIndex ;
        Bool doInsert ; /* Should we insert a new segment? */
        uint32 segIndex ; /* Insertion point of new segment */
        uint16 glyphOffset ; /* glyphIndex offset from startCharCode */

        /* load this group */
        TT_ulong( ttmem , 0, startCharCode ) ;
        TT_ulong( ttmem , 4, endCharCode ) ;
        TT_ulong( ttmem , 8, glyphIndex ) ;

        HQASSERT(glyphIndex <= 0xffff, "Glyph index is out of range") ;

        (*ttfns->close_frame)(ttfns->data, &ttmem) ;
        offset += TT_SIZE_CMAP_TABLE_DATA_12_1 ;
        length -= TT_SIZE_CMAP_TABLE_DATA_12_1 ;

        glyphOffset = (uint16)(glyphIndex - startCharCode) ;

        /* Test if the segments are ordered correctly. The OpenType 1.5 spec
           says they MUST be, however we've come across bad fonts that don't
           obey this (see request 63164). The font in that request was
           particularly bad; it has overlapping ranges, and whole sections of
           the char code range that appear out of order. */
        if ( startCharCode >= nextCode ) { /* In order */
          segIndex = nFound ;
          cmape->nSegments = (uint16)++nFound ;
          doInsert = TRUE ;
        } else { /* Out of order. */
          HQASSERT(nFound > 0,
                   "Should not have overlap if no existing segments") ;

          /* Start by finding the first segment that is affected. Many of
             the overlapping segments overlap the final segment, so test
             for that first. */
          segIndex = nFound - 1 ;
          if ( startCharCode < cmape->segments[segIndex]->startCode ) {
            /* Binary search for the first segment affected. The search finds
               either the first segment including the start code, or the
               segment immediately following it. */
            uint32 lo = 0, hi = nFound - 1 ;

            do {
              uint32 middle = (lo + hi) / 2 ;
              TT_CMAP_SEGMENT *segment = cmape->segments[middle] ;
              if ( segment->endCode < startCharCode )
                lo = middle + 1 ;
              else if ( segment->startCode > startCharCode )
                hi = middle ; /* (lo+hi)/2 rounds down, so don't use middle-1. */
              else /* Found segment containing the start code. */
                lo = hi = middle ;
            } while ( lo < hi ) ;

            HQASSERT(lo == hi, "Search did not terminate correctly") ;

            segIndex = lo ;
            HQASSERT(segIndex < nFound &&
                     cmape->segments[segIndex]->endCode >= startCharCode &&
                     (segIndex == 0 || cmape->segments[segIndex - 1]->endCode < startCharCode),
                     "Did not find segment containing or following start") ;
          }

          /* Now we have the first segment index which either overlaps or
             follows the segment we're trying to create. */
          if ( endCharCode < cmape->segments[segIndex]->startCode ) {
            /* The new segment is completely in a gap before the existing
               segment, so make space to insert a new record. */
            HqMemMove(&cmape->segments[segIndex + 1],
                      &cmape->segments[segIndex],
                      (char *)&cmape->segments[nFound] - (char *)&cmape->segments[segIndex]) ;
            cmape->nSegments = (uint16)++nFound ;
            doInsert = TRUE ; /* Fall through to normal insertion*/
          } else {
            /* The new segment overlaps the existing segment. */
            Bool expand = FALSE ;
            uint32 lastIndex = segIndex ;
            uint32 startSegmentCode, endSegmentCode ;

            /* Determine the range of segments affected by the new segment,
               and set a flag to indicate if we should expand the segment to
               an array to handle glyph index mapping mismatches. */
            do {
              TT_CMAP_SEGMENT *segment = cmape->segments[lastIndex] ;
              HQASSERT(startCharCode <= segment->endCode &&
                       endCharCode >= segment->startCode,
                       "No overlap with segment") ;
              /* Expand to an array if either the overlapped segment is
                 represented using an array already, or if the mapping of the
                 glyph indices doesn't match. The glyph indices are derived
                 using an offset from the char code, so if the offset for the
                 existing segment doesn't map to the same as for the new
                 glyph index/start code combination, the mapping isn't the
                 same. */
              if ( !segment->useOffset ||
                   segment->glyphIndex[0] != glyphOffset )
                expand = TRUE ;
              ++lastIndex ;
            } while ( lastIndex < nFound &&
                      cmape->segments[lastIndex]->startCode <= endCharCode ) ;

            /* lastindex is now one past the final affected segment.
               Decrement it to point at the last affected segment. */
            --lastIndex ;

            /* Set the expanded limits of the segment we're about to create. */
            startSegmentCode = cmape->segments[segIndex]->startCode ;
            if ( startCharCode < startSegmentCode )
              startSegmentCode = startCharCode ;

            endSegmentCode = cmape->segments[lastIndex]->endCode ;
            if ( endCharCode > endSegmentCode )
              endSegmentCode = endCharCode ;

            if ( expand ) {
              TT_CMAP_SEGMENT *cmaps ;

              /* We have to expand the segments to an array because of
                 different mappings, so allocate a new segment to cover all
                 of the affected segments. */
              if ( (cmaps = mm_alloc(mm_pool_temp,
                                     TT_CMAP_SEGMENT_SIZE(startSegmentCode, endSegmentCode, FALSE),
                                     MM_ALLOC_CLASS_TTFONT)) == NULL) {
                error = FAILURE(VMERROR) ;
              } else {
                uint32 position = 0, mergeIndex ;

                cmaps->startCode = startSegmentCode ;
                cmaps->endCode   = endSegmentCode ;
                cmaps->useOffset = FALSE ;

                for ( mergeIndex = segIndex ; mergeIndex <= lastIndex ; ++mergeIndex ) {
                  TT_CMAP_SEGMENT *segment = cmape->segments[mergeIndex] ;

                  /* Initial section before merge segment maps OK. This can
                     only happen if the new segment starts before the
                     segment we're examining, so the glyphIndex mapping is
                     valid. */
                  while ( startSegmentCode < segment->startCode ) {
                    HQASSERT(startSegmentCode <= endSegmentCode,
                             "Exceeded range of segment") ;
                    HQASSERT(startSegmentCode >= startCharCode &&
                             startSegmentCode <= endCharCode,
                             "glyphOffset used outside of valid range") ;
                    cmaps->glyphIndex[position] = (uint16)(startSegmentCode + glyphOffset) ;
                    ++startSegmentCode, ++position ;
                  }

                  if ( !segment->useOffset ) {
                    /* Existing mapping is array mapping. Check each entry's
                       mapping individually. */
                    while ( startSegmentCode <= segment->endCode ) {
                      uint16 sglyph = segment->glyphIndex[startSegmentCode - segment->startCode] ;
                      HQASSERT(startSegmentCode <= endSegmentCode,
                               "Exceeded range of segment") ;
                      if ( startSegmentCode >= startCharCode &&
                           startSegmentCode <= endCharCode &&
                           sglyph != (uint16)(startSegmentCode + glyphOffset) )
                        cmaps->glyphIndex[position] = 0xffffu ;
                      else
                        cmaps->glyphIndex[position] = sglyph ;

                      ++startSegmentCode, ++position ;
                    }
                  } else if ( glyphOffset == segment->glyphIndex[0] ) {
                    /* Range coincides with existing mapping. */
                    while ( startSegmentCode <= segment->endCode ) {
                      HQASSERT(startSegmentCode <= endSegmentCode,
                               "Exceeded range of segment") ;
                      cmaps->glyphIndex[position] = (uint16)(startSegmentCode + glyphOffset) ;
                      ++startSegmentCode, ++position ;
                    }
                  } else {
                    /* Range conflicts with existing mapping. */
                    while ( startSegmentCode <= segment->endCode ) {
                      HQASSERT(startSegmentCode <= endSegmentCode,
                               "Exceeded range of segment") ;
                      if ( startSegmentCode >= startCharCode &&
                           startSegmentCode <= endCharCode )
                        cmaps->glyphIndex[position] = 0xffffu ;
                      else
                        cmaps->glyphIndex[position] = (uint16)(startSegmentCode + segment->glyphIndex[0]) ;

                      ++startSegmentCode, ++position ;
                    }
                  }

                  /* Done with this segment, free it. */
                  mm_free(mm_pool_temp, segment,
                          TT_CMAP_SEGMENT_SIZE(segment->startCode,
                                               segment->endCode,
                                               segment->useOffset)) ;
                }

                /* Final section after all segments maps OK. */
                while ( startSegmentCode <= endSegmentCode ) {
                  cmaps->glyphIndex[position] = (uint16)(startSegmentCode + glyphOffset) ;
                  ++startSegmentCode, ++position ;
                }

                /* Insert the new segment in place of the first freed
                   segment. */
                cmape->segments[segIndex] = cmaps ;

                if ( lastIndex != segIndex ) {
                  /* Shuffle down the memory, and adjust the number of groups
                     to reflect removal of some segments. */
                  HqMemMove(&cmape->segments[segIndex + 1],
                            &cmape->segments[lastIndex + 1],
                            (char *)&cmape->segments[nFound] - (char *)&cmape->segments[lastIndex + 1]) ;
                  nFound -= lastIndex - segIndex ;
                  cmape->nSegments = (uint16)nFound ;
                }
              }
            } else {
              /* We don't have to expand the segments. The offsets for all
                 affected segments must be the same, and they are also the
                 same as the offset for the new segment. We'll expand the
                 first segment to cover the whole range, then remove any
                 superfluous segments. */
              TT_CMAP_SEGMENT *segment = cmape->segments[segIndex] ;

              HQASSERT(segment->useOffset,
                       "Segment doesn't use offset mapping") ;
              HQASSERT(segment->glyphIndex[0] == glyphOffset,
                       "Segment offset doesn't match new mapping") ;
              segment->startCode = startSegmentCode ;
              segment->endCode = endSegmentCode ;

              if ( lastIndex != segIndex ) {
                uint32 freeIndex ;

                HQASSERT(lastIndex > segIndex,
                         "Last Index must be greater than first Index") ;

                /* Free all of the segments that we don't need any more. */
                freeIndex = lastIndex ;
                do {
                  TT_CMAP_SEGMENT *segment = cmape->segments[freeIndex] ;
                  HQASSERT(segment->startCode >= startCharCode &&
                           segment->endCode <= endCharCode,
                           "Segment freed, but not covered by new range") ;
                  HQASSERT(segment->useOffset,
                           "Segment freed, but wasn't offset mapped") ;
                  HQASSERT(segment->glyphIndex[0] == glyphOffset,
                           "Segment freed, but offset doesn't match new range") ;
                  mm_free(mm_pool_temp, segment,
                          TT_CMAP_SEGMENT_SIZE(segment->startCode,
                                               segment->endCode,
                                               segment->useOffset)) ;
                  --freeIndex ;
                } while ( freeIndex > segIndex ) ;

                HqMemMove(&cmape->segments[segIndex + 1],
                          &cmape->segments[lastIndex + 1],
                          (char *)&cmape->segments[nFound] - (char *)&cmape->segments[lastIndex + 1]) ;

                nFound -= lastIndex - segIndex ;
                cmape->nSegments = (uint16)nFound ;
              }
            } /* end range mapping clause */

            /* We might have expanded the final segment, so reset the high
               water mark. */
            nextCode = cmape->segments[nFound - 1]->endCode + 1 ;

            /* We've now handled the overlap case, and no insertion is
               required. Avoid the insertion clause below. */
            doInsert = FALSE ;
          } /* end overlap clause */
        }

        if ( doInsert ) {
          TT_CMAP_SEGMENT *cmaps ;

          if ((cmape->segments[segIndex] = cmaps = mm_alloc(mm_pool_temp,
                                                            TT_CMAP_SEGMENT_SIZE(startCharCode, endCharCode, TRUE),
                                                            MM_ALLOC_CLASS_TTFONT)) == NULL) {
            error = FAILURE(VMERROR) ;
          } else {
            /* group allocated and linked, so fill in necessary fields before
             * syntax checking, so tt_free_cmap_entry will work */
            cmaps->startCode = startCharCode ;
            cmaps->endCode   = endCharCode ;
            cmaps->useOffset = TRUE ;
            cmaps->glyphIndex[0] = glyphOffset ;

            /* We might have expanded the final segment, so reset the high
               water mark. */
            nextCode = cmape->segments[nFound - 1]->endCode + 1 ;
          }
        } /* if insert */
      } /* if ttmem */
    } /* if length >= 12 */

  } /* for each group */

  HQASSERT(nFound <= nGroups, "Overran segment allocation") ;

  if (error) {
    tt_free_cmap_entry(cmape) ;
    (void)error_handler(error) ;
    return NULL ;
  }
  return cmape ;
}

/* -------------------------------------------------------------------------- */

static Bool tt_readcmapTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  /* (5) Read in and cache all the 'cmap' header, checking it is valid.
   * Type       Description
   * USHORT     Table version number (0).
   * USHORT     Number of encoding tables, n.
   *   +
   * USHORT     Platform ID.
   * USHORT     Platform-specific encoding ID.
   * ULONG      Byte offset from beginning of table to the subtable for this encoding.
   *   +
   * Type       Name    Description
   * USHORT     format  Format number is set to X.
   * USHORT     length  Length in bytes.
   * USHORT     LanguageID LanguageID number (starts at 0)
   */

  int32  nEncodings ;

  uint16 Tableversionnumber ;
  uint16 Numberencodingtables ;

  uint16 format ;
  uint32 length ;
  uint16 LanguageID ;

  uint16 PlatformID ;
  uint16 EncodeID ;
  uint32 byteoffset ;

  uint32 base ;

  uint8 *ttmem ;

  TT_CMAP_ENTRY **cmapp ;
  TT_CMAP *Cmap_data = &TT_data->tt_cmap ;
  TT_TABLE *Tab_cmap ;

  if ( Cmap_data->tried )
    return TRUE ;

  Cmap_data->tried = TRUE ;
  if ( (Tab_cmap = tt_findTable(TT_data, tt_maptag("cmap"))) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  base = Tab_cmap->offset ;
  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, base, TT_SIZE_CMAP)) == NULL )
    return error_handler( INVALIDFONT ) ;

  TT_ushort( ttmem , 0 , Tableversionnumber ) ;
  TT_ushort( ttmem , 2 , Numberencodingtables ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  if ( Tableversionnumber != 0 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid Tableversionnumber: %d",Tableversionnumber));
    return error_handler( INVALIDFONT ) ;
  }

  if ( Numberencodingtables != 1 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("multiple Numberencodingtables: %d",Numberencodingtables));
#ifdef TT242_UNIQUE_ENCODINGS
    return error_handler( INVALIDFONT ) ;
#else
    if ( Numberencodingtables == 0 )
      return error_handler( INVALIDFONT ) ;
#endif
  }

  cmapp = &Cmap_data->cmaps ;

  for ( nEncodings = 0 ; nEncodings < Numberencodingtables ; ++nEncodings ) {
    uint32 offset = base + TT_SIZE_CMAP + nEncodings * TT_SIZE_CMAP_TABLE1 ;
    TT_CMAP_ENTRY *cmape = 0 ;

    if ( (ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                                      TT_SIZE_CMAP_TABLE1)) == NULL )
      return error_handler( INVALIDFONT ) ;

    TT_ushort( ttmem , 0 , PlatformID ) ;
    TT_ushort( ttmem , 2 , EncodeID ) ;
    TT_ulong ( ttmem , 4 , byteoffset ) ;

    (*ttfns->close_frame)(ttfns->data, &ttmem) ;

    offset = base + byteoffset ;

    /* [62037] 32bit cmaps have different sized length and language fields, so
       get the format first and then the rest, appropriately */
    if ( (ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                                       TT_SIZE_CMAP_TABLE2_16)) == NULL )
      return error_handler( INVALIDFONT ) ;

    TT_ushort( ttmem , 0 , format ) ;

    if (format < 8) {
      /* 16bit cmaps have 16bit length and language */

      TT_ushort( ttmem , 2 , length ) ;
      TT_ushort( ttmem , 4 , LanguageID ) ;

      /* We have seen cmap tables with a table length of 0.
       * This cannot be correct, since the presence of the cmap means its size
       * is not 0.
       * Some tables have fixed size, but to "repair" the length, the presumed
       * table data would have to be examined for validity.
       * Ignore the broken table.
       */
      if (length == 0)
        continue;

      offset += TT_SIZE_CMAP_TABLE2_16 ;
      length -= TT_SIZE_CMAP_TABLE2_16 ;

    } else {
      uint32 LanguageID32 ;
      /* 32bit cmaps have 32bit length and language */

      (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      if ( (ttmem = (*ttfns->open_frame)(ttfns->data, offset,
                                         TT_SIZE_CMAP_TABLE2_32)) == NULL )
        return error_handler( INVALIDFONT ) ;

      TT_ulong( ttmem , 4 , length ) ;
      TT_ulong( ttmem , 8 , LanguageID32 ) ;

      offset += TT_SIZE_CMAP_TABLE2_32 ;
      length -= TT_SIZE_CMAP_TABLE2_32 ;

      /* MS cmap format 12 subsetter appears to corrupt (or not fill in) the
       * LanguageID field, resulting in a random value. It's only useful for
       * Mac cmaps anyway. [62037] */
      if (LanguageID32 > 0xFFFF) {
        if (PlatformID == TT_PID_MACINTOSH) {
          HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
                  ("invalid Mac LanguageID: 0x%08x", LanguageID32));
          return error_handler( INVALIDFONT ) ;
        }
        LanguageID = TT_LID_MAC_NONSPECIFIC ; /* will be overridden anyway */
      } else
        LanguageID = (uint16)LanguageID32 ;
    }
    (*ttfns->close_frame)(ttfns->data, &ttmem) ;

    /* LanguageID *must* be zero for non-Macintosh cmaps, as
     * mandated by the OT1.4 spec [62037] */
    if (PlatformID != TT_PID_MACINTOSH &&
        LanguageID != 0) {
      HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,
              ("invalid non-Mac LanguageID: 0x%08x", LanguageID));
      LanguageID = 0 ;
    }

    switch ( format ) {

    case 0:
      cmape = read_cmap_format_0(ttfns, offset, length,
                                 PlatformID, EncodeID, LanguageID) ;
      break ;

    case 2:
      cmape = read_cmap_format_2(ttfns, offset, length,
                                 PlatformID, EncodeID, LanguageID) ;
      break ;

    case 4:
      cmape = read_cmap_format_4(ttfns, offset, length,
                                 PlatformID, EncodeID, LanguageID) ;
      break ;

    case 6:
      cmape = read_cmap_format_6(ttfns, offset, length,
                                 PlatformID, EncodeID, LanguageID) ;
      break ;

    case 8: /* mixed 16bit and 32bit */
      HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,("cmap format: 8 (unimplemented)"));
      /*   +
       * Type   Name                    Description
       * BYTE   is32[8192]              Tightly packed array of bits indicating
       *                                whether a 16bit index is the start of a
       *                                32bit code.
       * ULONG  nGroups                 Number of groups which follow.
       *
       * Each group is structured as follows:
       * Type   Name                    Description
       * ULONG  startCharCode           First code in this group
       * ULONG  endCharCode             Last code in this group
       * ULONG  startGlyphID            First glyph index in this group
       */
      continue ; /* Ignore this - MS don't support it so we won't see it */

    case 10: /* Trimmed array */
      HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,("cmap format: 10 (unimplemented)"));
      /*   +
       * Type   Name                    Description
       * ULONG  startCharCode           First character code covered
       * ULONG  numChars                Number of character codes covered
       * USHORT glyphs[]                Array of glyph IDs
       */
      continue ; /* Once again unsupported by MS, so we won't implement it */
       /* Note that AppleSymbols contains one of these as a (0,3) in addition to
        * the (1,0) that we use instead. That isn't likely to change. */

    case 12: /* Segmented coverage */
      cmape = read_cmap_format_12(ttfns, offset, length,
                                  PlatformID, EncodeID, LanguageID) ;
      break ;

    case 14: /* Unicode Variation Sequences */
      continue ; /* Ignore this - we don't need to support it */

    default:
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid format: %d",format));
      return error_handler( INVALIDFONT ) ;
    }

    /* we now have a cmape, if all went well */
    if (cmape == 0)
      return FALSE ;  /* error_handler already called */

    /* link into list */
    *cmapp = cmape ;
    cmapp = &cmape->next ;
  }

  Cmap_data->loaded = TRUE ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool tt_reados2Table(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  /* The OS/2 table contains many pieces of metrics data that we are not
   * interested in, and a few we are. Version 1 of the table (TT 1.6 spec)
   * shows the following fields. We are only interested in ulCodePageRange,
   * to determine if this font should be a CID font.
   *
   * Type       Name                    Description
   * USHORT     version                 0, 1, 2, or 3
   * SHORT      xAvgCharWidth
   * USHORT     usWeightClass
   * USHORT     usWidthClass
   * SHORT      fsType                  (USHORT in version 3 table)
   * SHORT      ySubscriptXSize
   * SHORT      ySubscriptYSize
   * SHORT      ySubscriptXOffset
   * SHORT      ySubscriptYOffset
   * SHORT      ySuperscriptXSize
   * SHORT      ySuperscriptYSize
   * SHORT      ySuperscriptXOffset
   * SHORT      ySuperscriptYOffset
   * SHORT      yStrikeoutSize
   * SHORT      yStrikeoutPosition
   * SHORT      sFamilyClass
   * PANOSE     panose
   * ULONG      ulUnicodeRange1         Bits 0–31
   * ULONG      ulUnicodeRange2         Bits 32–63
   * ULONG      ulUnicodeRange3         Bits 64–95
   * ULONG      ulUnicodeRange4         Bits 96–127
   * CHAR       achVendID[4]
   * USHORT     fsSelection
   * USHORT     usFirstCharIndex
   * USHORT     usLastCharIndex
   *
   * Version 1 was introduced by Microsoft in TrueType 1.5 (Apple do not
   * mention it in their specification). It adds:
   *
   * USHORT     sTypoAscender
   * USHORT     sTypoDescender
   * USHORT     sTypoLineGap
   * USHORT     usWinAscent
   * USHORT     usWinDescent
   * ULONG      ulCodePageRange1        Bits 0-31
   * ULONG      ulCodePageRange2        Bits 32-63
   *
   * Version 2 was used for OpenType 1.2. Version 3 of this table is used for
   * OpenType 1.4, and adds:
   *
   * Type       Name                    Description
   * SHORT      sxHeight
   * SHORT      sCapHeight
   * USHORT     usDefaultChar
   * USHORT     usBreakChar
   * USHORT     usMaxContext
   */

  uint16 Tableversionnumber ;
  uint8 *ttmem ;

  TT_OS2 *os2_data = &TT_data->tt_os2 ;
  TT_TABLE *Tab_os2 ;

  if ( os2_data->tried )
    return TRUE ;

  os2_data->tried = TRUE ;
  if ( (Tab_os2 = tt_findTable(TT_data, tt_maptag("OS/2"))) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, Tab_os2->offset,
                                     TT_SIZE_OS2_0)) == NULL )
    return error_handler( INVALIDFONT ) ;

  TT_ushort(ttmem, 0, Tableversionnumber) ;
  TT_ulong(ttmem, 42, os2_data->ulUnicodeRange1) ;
  TT_ulong(ttmem, 46, os2_data->ulUnicodeRange2) ;
  TT_ulong(ttmem, 50, os2_data->ulUnicodeRange3) ;
  TT_ulong(ttmem, 54, os2_data->ulUnicodeRange4) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  if ( Tableversionnumber < 0x0001 ) {
    os2_data->sTypoAscender = 0 ;
    os2_data->sTypoDescender = 0 ;
    os2_data->ulCodePageRange1 = 0 ;
    os2_data->ulCodePageRange2 = 0 ;
    os2_data->loaded = PARTIAL ;      /* note! */
    return TRUE ;
  }

  /* Version 1.0 and above contain the values we're after. We'll load the
     extra part of the table separately. */
  if ( (ttmem = (*ttfns->open_frame)(ttfns->data,
                                     Tab_os2->offset + TT_SIZE_OS2_0,
                                     TT_SIZE_OS2_1 - TT_SIZE_OS2_0)) == NULL )
    return error_handler( INVALIDFONT ) ;

  TT_ushort(ttmem, 0, os2_data->sTypoAscender) ;
  TT_ushort(ttmem, 2, os2_data->sTypoDescender) ;
  TT_ulong(ttmem, 10, os2_data->ulCodePageRange1) ;
  TT_ulong(ttmem, 14, os2_data->ulCodePageRange2) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  os2_data->loaded = COMPLETE ;

  return TRUE ;
}

/** Determine if this font should be a CID font, even if not requested. If
   it is a CID font, we want to find the Registry, Ordering, Supplement,
   and the CIDMap mapping from CID (character collection index) to GID.

   If there is a name ID 20 (PostScript CID fontfont name), it is a CID font.
   The CID findfont name is name ID 6 followed by the CMap name, so we can
   strip name ID 6 from name ID 20 to get the CMap, and load it. This gives
   us the Registry and Ordering, and also a mapping from an encoding to the
   CID. We may also use the supplement from the CMap if we don't know any
   better (see below). In this case, there should be a corresponding
   Macintosh cmap table, giving a mapping from encoding to GID. To get the
   CID to GID mapping, we need to reverse-map the CMap to get encodings, and
   forward-map these through the cmap table. This may be a tricky mapping to
   perform, since it goes through and unknown multi-byte encoding, so we may
   just search for a ToUnicode CMap for the character collection, having
   found the Registry and Ordering from the named CMap.

   If there is no name ID 20, we will look up the OS/2 table to find out
   which character sets are supported. The character collection can be
   inferred from the Code Page Character Range bitset. Having found a
   character collection (and thus Registry and Ordering), we will use a
   ToUnicode CMap and any Unicode cmap table to map CIDs to Unicode and
   thence to GIDs.

   For unspecified character collections, or missing ToUnicode CMaps, we will
   assume that CIDs match input values, and construct a CIDMap based upon the
   highest preference cmap table found (this will usually be a Unicode cmap,
   giving unicode CID order).

   The CIDMap will be generated using the basemap as temporary storage (it
   will not exceed 128k, so should always fit). It will be optimised to a
   string, array of strings, dictionary, or and integer, depending on the
   highest CID value present and the coverage.

   For character collections that we know about, we can probe the CIDMap to
   see which characters are present. For each supplement known, if an
   acceptable number of the characters are present, we'll say that the
   supplement is supported. The defining documents for character collections
   are Adobe Technotes:

   TN5094 Adobe CJKV Character Collections and CMaps for CID-Keyed Fonts

   and

   TN5078 Adobe Japan 1-4 Character Collection
   TN5079 Adobe GB 1-4 Character Collection
   TN5080 Adobe CNS 1-4 Character Collection
   TN5093 Adobe Korea 1-0 Character Collection
   TN5097 Adobe Japan 2-0 Character Collection
   TN5146 Adobe Japan 1-5 Character Collection

   For the first try, we'll not check name 20, but just use the OS/2
   table. */
static Bool tt_findCMapResource(OBJECT *cmap)
{
  HQASSERT(cmap, "No name for CMap resource") ;
  HQASSERT(oType(*cmap) == ONAME || oType(*cmap) == OSTRING,
           "CMap name not a string or a name") ;

  /* N.B. nnewobje going on execstack, nnewobj on operandstack */
  oName(nnewobj) = &system_names[NAME_CMap];
  oName(nnewobje) = &system_names[NAME_findresource];

  if ( !interpreter_clean(&nnewobje, cmap, &nnewobj, NULL) )
    return FALSE ;

  Copy(cmap, theTop(operandstack)) ;
  pop(&operandstack);

  if ( oType(*cmap) != ODICTIONARY )
    return error_handler(TYPECHECK);

  return TRUE ;
}

static
Bool tt_CMapResourceStatus(
  OBJECT  *cmap,
  Bool  *exists)
{
  HQASSERT(cmap, "No name for CMap resource") ;
  HQASSERT(oType(*cmap) == ONAME || oType(*cmap) == OSTRING,
           "CMap name not a string or a name") ;
  HQASSERT(exists, "NULL returned exists pointer");

  /* N.B. nnewobje going on execstack, nnewobj on operandstack */
  oName(nnewobj) = &system_names[NAME_CMap];
  oName(nnewobje) = &system_names[NAME_resourcestatus];

  if (!interpreter_clean(&nnewobje, cmap, &nnewobj, NULL)) {
    return (FALSE);
  }
  if (theStackSize(operandstack) < 0 ||
      oType(*theTop(operandstack)) != OBOOLEAN) {
    return (FALSE);
  }

  *exists = oBool(*theTop(operandstack));
  npop(*exists ? 3 : 1, &operandstack);
  return (TRUE);
}

typedef struct {
  uint16 *        mapping ;   /* The array of mappings so far */
  uint32          mapsize ;   /* Size of this array in bytes */
  Bool            identity ;  /* Flag, initially TRUE, cleared if cid!=gid */
  uint32          ncids ;     /* The number of mapped cids in the array */
  uint32          maxcid ;    /* The highest mapped cid in the array */
  TT_CMAP_ENTRY * cmap ;      /* The cmap entry being walked */
} cmap_walk_data ;

#define TT_CIDMAP_UNDEFINED 0 /* Undefined CID->GID mapping */

static Bool cidmap_add_mapping(cmap_walk_data *cmap_data,
                               uint32 cid, uint16 gid)
{
  HQASSERT(cmap_data, "No cmap data") ;
  HQASSERT(gid != TT_CIDMAP_UNDEFINED, "Cannot add undefined mapping") ;

  if ( cid > cmap_data->maxcid ) {
    /* Check there is enough space in which to map this CID. */
    if ( cmap_data->mapsize < (cid << 1) + 2 )
      return error_handler(VMERROR) ;

    /* All unassigned glyphs map to GID 0. The Microsoft spec implies that
       GID 0 is the missing character only for Macintosh fonts, but in
       practice all fonts use it. */
    HqMemSet16((uint16 *)cmap_data->mapping + cmap_data->maxcid + 1,
            (int16)TT_CIDMAP_UNDEFINED, cid - cmap_data->maxcid);

    cmap_data->maxcid = cid ;
  }

  /* Check if it's still an identity mapping. */
  if ( cid != gid )
    cmap_data->identity = FALSE ;

  /* If this CID was not defined, it's a new mapping */
  if ( cmap_data->mapping[cid] == TT_CIDMAP_UNDEFINED )
    cmap_data->ncids++ ;

  cmap_data->mapping[cid] = gid ;

  return TRUE ;
}

/** This iterator function is used to correlate CMaps with TT 'cmap' tables,
   to produce CID to GID mappings. There are two cases; when the CMap
   extracted from a 'name' ID 20 was found, we iterate over the CMap, mapping
   the input codes to CIDs using the CMap, and input codes to GIDs using the
   corresponding 'cmap' table. Correlating these gives the CIDMap. The other
   case is when we have found a unicode 'cmap' table and a corresponding
   ToUnicode CMap resource. In this case, we map the CMap CIDs to unicode
   values, and the unicode values to GIDs using the 'cmap' table to find the
   mapping.

   The OpenType 1.4 spec says that each 'name' ID 20 will have a
   corresponding Mac 'cmap' table. The Mac cmaps are (presumably) used
   because high-byte through table mappings are multi-byte, and can represent
   a CMap adequately. The example in the OpenType 1.4 spec is that name ID 6
   is KozMinStd-Regular and name ID 20 is KozMinStd-Regular-83pv-RKSJ-H. */
static Bool cmap_to_cidmap(OBJECT *from, OBJECT *to, uint32 ncodes, void *data)
{
  cmap_walk_data *cmap_data = data ;

  HQASSERT(cmap_data, "No cmap data") ;
  HQASSERT(from && to, "No CMap mapping objects") ;
  HQASSERT(ncodes > 0, "CMap mapping range empty") ;
  HQASSERT(oType(*from) == OSTRING, "CMap mapping from non-string") ;

  /* Ignore names and arrays of names. We're only interested in CID mappings
     or ToUnicode bf* mappings. */
  if ( oType(*to) == OINTEGER ) { /* Encoding to CID mapping. */
    uint32 code = 0, cid, i, segsleft ;
    TT_CMAP_SEGMENT **seglist ;

    for ( i = 0 ; i < theLen(*from) ; ++i ) {
      code <<= 8 ;
      code |= oString(*from)[i] ;
    }

    cid = (uint32)oInteger(*to) ;

    /* The 'cmap' table segments are ordered we're going to traverse the
       segments in parallel with the CMap section, mapping them as we go.
       This is an optimisation; we could have just searched the cmap table
       for each cid in turn. */
    HQASSERT(cmap_data->cmap, "No cmap table entry while walking CMap") ;
    seglist = cmap_data->cmap->segments ;
    segsleft = cmap_data->cmap->nSegments ;
    while ( ncodes > 0 && segsleft > 0 ) {
      TT_CMAP_SEGMENT *segment = *seglist ;

      /* If the unicode value is past the end of this segment, find the next
         segment in which it could lie. If it can't lie in any, there is no
         mapping. */
      while ( code > segment->endCode ) {
        segment = *++seglist ;
        if ( --segsleft == 0 )
          return TRUE ;
      }

      /* The CID is now less than or equal to the segment end. If it is also
         greater than or equal to the segment start, then it's in this
         segment. */
      if ( code >= segment->startCode ) {
        uint16 gid = segment->glyphIndex[code - segment->startCode] ;

        if ( gid != TT_CIDMAP_UNDEFINED &&
             !cidmap_add_mapping(cmap_data, cid, gid) )
          return FALSE ;
      }

      ++cid ;
      ++code ;
      --ncodes ;
    }
  } else if ( oType(*to) == OSTRING &&
              theLen(*to) == 2 ) { /* CID to Unicode mapping. */
    uint32 cid = 0, unicode = 0, i, nSegments ;
    TT_CMAP_ENTRY *cmap_entry ;
    TT_CMAP_SEGMENT *segment ;

    for ( i = 0 ; i < theLen(*from) ; ++i ) {
      cid <<= 8 ;
      cid |= oString(*from)[i] ;
    }

    for ( i = 0 ; i < theLen(*to) ; ++i ) {
      unicode <<= 8 ;
      unicode |= oString(*to)[i] ;
    }

    /* We are reverse mapping the ToUnicode 'cmap' table, so the ordering of
       the segments in the 'cmap' does not help us. We'll keep a current
       segment, but if the Unicode value is outside the range of that
       segment, we have to search all of the segments for it. */
    cmap_entry = cmap_data->cmap ;
    HQASSERT(cmap_entry, "No cmap table entry while walking CMap") ;

    nSegments = cmap_entry->nSegments ;
    HQASSERT(nSegments > 0, "No segments in cmap table entry") ;

    segment = cmap_entry->segments[0] ; /* First segment is current */
    while ( ncodes > 0 ) {
      /* If the mapped unicode value is in range, set up a CID->GID mapping
         for it. Otherwise search all segments for one containing it. */
      if ( unicode >= segment->startCode && unicode <= segment->endCode ) {
        uint16 gid = segment->glyphIndex[unicode - segment->startCode] ;

        if ( gid != TT_CIDMAP_UNDEFINED &&
             !cidmap_add_mapping(cmap_data, cid, gid) )
          return FAILURE(FALSE) ;
      } else {
        for ( i = 0 ; i < nSegments ; ++i ) {
          segment = cmap_entry->segments[i] ;

          if ( unicode >= segment->startCode && unicode <= segment->endCode ) {
            uint16 gid = segment->glyphIndex[unicode - segment->startCode] ;

            if ( gid != TT_CIDMAP_UNDEFINED &&
                 !cidmap_add_mapping(cmap_data, cid, gid) )
              return FAILURE(FALSE) ;

            /* It is possible that there are multiple mappings from CID to
               Unicode value. Removing this break would allow all of them to
               be found, but the current segment would have to be reset to
               one of the segments found afterward. */
            break ;
          }
        }
      }

      ++cid ;
      ++unicode ;
      --ncodes ;
    }
  }

  return TRUE ;
}

/** This function is used when there is no suitable ToUnicode CMap available,
   but we do have a unicode 'cmap' table. It constructs a cmap with the CIDs
   on the unicode codepoints. */
static Bool hqn_unicode_cidmap(cmap_walk_data *cmap_data)
{
  uint32 segindex ;
  TT_CMAP_ENTRY *cmap_entry ;

  HQASSERT(cmap_data, "No cmap data") ;

  cmap_entry = cmap_data->cmap ;
  HQASSERT(cmap_entry, "No cmap table entry for Unicode CIDMap") ;

  /* Traverse all 'cmap' table segments, adding mappings. */
  for ( segindex = 0 ; segindex < cmap_entry->nSegments ; ++segindex ) {
    TT_CMAP_SEGMENT *segment = cmap_entry->segments[segindex] ;
    uint32 cid = segment->endCode ;

    /* Map these in reverse order so we call cidmap_add_mapping() with the
       highest CID first. This reduces the number of times HqMemSet() is
       called. */
    do {
      uint16 gid = segment->glyphIndex[cid - segment->startCode] ;

      if ( gid != TT_CIDMAP_UNDEFINED &&
           !cidmap_add_mapping(cmap_data, cid, gid) )
        return FAILURE(FALSE) ;
    } while ( cid-- != segment->startCode ) ;
  }

  return TRUE ;
}

typedef struct {
  uint16 startcid, endcid ;
} TT_CMAP_SUPPLEMENT ;

static int32 cidmap_guess_supplement(uint16 *mapping, uint32 maxcid,
                                     OBJECT *registry, OBJECT *ordering)
{
  TT_CMAP_SUPPLEMENT *supplement ;
  int32 sindex ;

  HQASSERT(mapping && registry && ordering,
           "Need to know mapping and RO to set supplement") ;

  /* See the following Adobe Technotes for details:

     TN5094 Adobe CJKV Character Collections and CMaps for CID-Keyed Fonts

     TN5078 Adobe Japan 1-4 Character Collection
     TN5079 Adobe GB 1-4 Character Collection
     TN5080 Adobe CNS 1-4 Character Collection
     TN5093 Adobe Korea 1-0 Character Collection
     TN5097 Adobe Japan 2-0 Character Collection
     TN5146 Adobe Japan 1-5 Character Collection
  */
  HQASSERT(oType(*registry) == OSTRING, "CID font Registry is not a string") ;
  HQASSERT(oType(*ordering) == OSTRING, "CID font Ordering is not a string") ;

  if ( HqMemCmp(oString(*registry), theLen(*registry),
                STRING_AND_LENGTH("Adobe")) != 0 )
    return 0 ; /* Don't know registry, return supplement 0 */

  /* Adobe-Japan2 does not have any supplements yet */

  if ( HqMemCmp(oString(*ordering), theLen(*ordering),
                STRING_AND_LENGTH("Japan1")) == 0 ) {
    static TT_CMAP_SUPPLEMENT adobe_japan1[] = {
      {     0,  8283 },
      {  8284,  8358 },
      {  8359,  8719 },
      {  8720,  9353 },
      {  9354, 15443 },
      { 15444, 20316 },
      { 20317, 23057 },
      { 0, 0 }
    } ;

    supplement = adobe_japan1 ;
  } else if ( HqMemCmp(oString(*ordering), theLen(*ordering),
                       STRING_AND_LENGTH("CNS1")) == 0 ) {
    static TT_CMAP_SUPPLEMENT adobe_cns1[] = {
      {     0, 14098 },
      { 14099, 17407 },
      { 17408, 17600 },
      { 17601, 18845 },
      { 18846, 18964 },
      { 18965, 19087 },
      { 19088, 19155 },
      { 0, 0 }
    } ;

    supplement = adobe_cns1 ;
  } else if ( HqMemCmp(oString(*ordering), theLen(*ordering),
                       STRING_AND_LENGTH("GB1")) == 0 ) {
    static TT_CMAP_SUPPLEMENT adobe_gb1[] = {
      {     0,  7716 },
      {  7717,  9896 },
      {  9897, 22126 },
      { 22127, 22352 },
      { 22353, 29063 },
      { 29064, 30283 },
      { 0, 0 }
    } ;

    supplement = adobe_gb1 ;
  } else if ( HqMemCmp(oString(*ordering), theLen(*ordering),
                       STRING_AND_LENGTH("Korea1")) == 0 ) {
    static TT_CMAP_SUPPLEMENT adobe_korea1[] = {
      {     0,  9332 },
      {  9333, 18154 },
      { 18155, 18351 },
      { 0, 0 }
    } ;

    supplement = adobe_korea1 ;
  } else
    return 0 ; /* Unknown character collection */

  /* We will assume that Supplement 0 of the stated collection is sufficiently
     covered. */
  for ( sindex = 1 ; supplement[sindex].endcid != 0 ; ++sindex ) {
    uint32 startcid = supplement[sindex].startcid ;
    uint32 endcid = supplement[sindex].endcid ;
    uint32 ncids = endcid - startcid + 1 ;
    uint32 nfound = 0 ;

    HQASSERT(endcid >= startcid, "Supplement does not contain any CIDs") ;

    do {
      if ( startcid <= maxcid && mapping[startcid] != TT_CIDMAP_UNDEFINED )
        ++nfound ;
    } while ( startcid++ != endcid ) ;

    /* Let's say the supplement is covered if 80% of the CIDs are present. */
    if ( nfound * 10 < ncids * 8 )
      break ;
  }

  return sindex - 1 ;
}

/** \todo @@@ TODO FIXME ajcd 2003-04-29: This function should iterate over CID font
   name entries in the 'name' table, seeing if we can use it to find the
   character collection and create a CID->GID mapping. At the moment, it only
   tests the most preferred CIDFONTNAME. */
static Bool tt_cidfontname_cmap(TT_DATA *tt_data, OBJECT *cmap,
                                TT_CMAP_ENTRY **tt_cmap)
{
  TT_NF *psname, *cidname ;
  uint32 i ;
  PEL_PRIORITY mac_cmap ;

  HQASSERT(tt_data, "No TT data") ;
  HQASSERT(tt_cmap, "Nowhere for TT cmap entry") ;

  HQASSERT(tt_data->tt_name.tried, "Names table not loaded") ;

  /* Find an appropriate CMap and cmap. If there is a name ID 20, we want to
     find the cmap matching the name. We will work out the CMap name by
     stripping psname from the front of CID font name. We'll check that the
     names match in encoding and language too, so we don't strip the wrong
     things. */
  psname = &tt_data->tt_name.names[TT_NAME_PSNAME] ;
  cidname = &tt_data->tt_name.names[TT_NAME_CIDFONTNAME] ;
  if ( psname->str == NULL || cidname->str == NULL ||
       psname->pid != cidname->pid ||
       psname->eid != cidname->eid ||
       psname->lid != cidname->lid ||
       psname->len >= cidname->len )
    return FAILURE(FALSE) ;

  HQFAIL("Untested TT mapping; please notify globalgraphics.com") ;

  /* Set up the priority list to find a Mac cmap matching the CID font name
     record. */
  mac_cmap.pid = cidname->pid ;
  mac_cmap.eid = cidname->eid ;
  mac_cmap.lid = (uint16)(cidname->lid + 1) ; /* N.B. arithmetic modulo 2^16 */

  if ( (*tt_cmap = tt_findCmap(tt_data, &mac_cmap, 1)) == NULL )
    return FALSE ;

  for ( i = 0 ; i < psname->len ; ++i ) {
    if ( psname->str[i] != cidname->str[i] )
      return FAILURE(FALSE) ;
  }

  /* In the OpenType 1.4 examples, they use 'KozMinStd-Regular' for the
     PostScript name and 'KozMinStd-Regular-83pv-RKSJ-H' for the CID font
     name. Stripping the PostScript name from the CID font name leaves a CMap
     name prefixed by '-'. */
  while ( cidname->str[i] == '-' ) {
    if ( ++i == cidname->len )
      return FAILURE(FALSE) ;
  }

  /* Create a name object for the CMap, and search for the CMap. */
  theTags(*cmap) = ONAME | LITERAL ;
  if ( (oName(*cmap) = cachename(&cidname->str[i], cidname->len - i)) == NULL )
    return FALSE ;

  return tt_findCMapResource(cmap) ;
}

/* Set up a new CIDSystemInfo sub-dictionary (aka ROS), inserting the supplied
   R-O-S. We set up a new dictionary rather than copying it from any CMap
   found because we probably changed the supplement number, and we need to
   make sure it is in PostScript memory in the correct global/local memory. */
static Bool tt_makeCIDSystemInfo(OBJECT *ros,
                                 OBJECT *registry, OBJECT *ordering,
                                 int32 supplement)
{
  HQASSERT(ros, "Nowhere for new CIDSystemInfo") ;
  HQASSERT(registry && oType(*registry) == OSTRING, "No registry") ;
  HQASSERT(ordering && oType(*ordering) == OSTRING, "No ordering") ;

  if ( !ps_dictionary(ros, 3) )
    return FALSE ;

  if ( !fast_insert_hash_name(ros, NAME_Registry, registry) ||
       !fast_insert_hash_name(ros, NAME_Ordering, ordering) )
    return FALSE ;

  oInteger(inewobj) = supplement ;
  if ( !fast_insert_hash_name(ros, NAME_Supplement, &inewobj) )
    return FALSE ;

  return object_access_reduce(READ_ONLY, ros) ;
}

/** Unpack a (possibly NULL) CIDSystemInfo to get the Registry, Ordering and
   Supplement. */
static Bool tt_unpackCIDSystemInfo(OBJECT *ros,
                                   OBJECT *registry, OBJECT *ordering,
                                   int32 *supplement)
{
  OBJECT *r, *o, *s ;

  HQASSERT(registry && ordering && supplement, "No where for ROS") ;


  if ( ros == NULL ||
       oType(*ros) != ODICTIONARY ||
       (r = fast_extract_hash_name(ros, NAME_Registry)) == NULL ||
       oType(*r) != OSTRING ||
       (o = fast_extract_hash_name(ros, NAME_Ordering)) == NULL ||
       oType(*o) != OSTRING ||
       (s = fast_extract_hash_name(ros, NAME_Supplement)) == NULL ||
       oType(*s) != OINTEGER )
    return error_handler(TYPECHECK) ;

  Copy(registry, r) ;
  Copy(ordering, o) ;
  *supplement = oInteger(*s) ;

  return TRUE ;
}

/* Compare a (possibly NULL) CIDSystemInfo object to an existing Registry,
   Ordering and Supplement for compatibility. */
static Bool tt_compareCIDSystemInfo(OBJECT *ros,
                                    OBJECT *registry, OBJECT *ordering,
                                    int32 supplement, Bool *same)
{
  OBJECT rmatch = OBJECT_NOTVM_NOTHING, omatch = OBJECT_NOTVM_NOTHING ;
  int32 smatch ;

  if ( !tt_unpackCIDSystemInfo(ros, &rmatch, &omatch, &smatch) )
    return FALSE ;

  *same = (compare_objects(registry, &rmatch) &&
           compare_objects(ordering, &omatch) &&
           smatch >= supplement) ;

  if ( *same ) {
    /* Use the copies of the registry and ordering from the cmap; the
       originals may be from PDF memory, but these were put in PS memory. */
    Copy(registry, &rmatch) ;
    Copy(ordering, &omatch) ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* GSUB handling for correction of vertical forms mapped through Unicode.
 *
 * See [307872] for details.
 */

/* Structure to hold a subset of the cmap mapping, and invert it */
typedef struct {
  uint16 cid ;
  uint16 gid ;
} TT_GIDMAP ;


/* qsort comparator for inverting an array of TT_GIDMAPs */
static int CRT_API gidmap_cmp(const void *a, const void *b)
{
  TT_GIDMAP * mapa = (TT_GIDMAP *) a ;
  TT_GIDMAP * mapb = (TT_GIDMAP *) b ;
  int result = ((int)mapa->gid) - ((int)mapb->gid) ;
  if (result == 0)
      result = ((int)mapa->cid) - ((int)mapb->cid) ;

  return result ;
}

/* These macros cut down the amount of typing and opportunity for error. They
   could be used throughout this file as this naming idiom is ubiquitous. */
#define TT_open(_o,_s) (*ttfns->open_frame)(ttfns->data, _o, _s)
#define TT_close(_mem) (*ttfns->close_frame)(ttfns->data, &(_mem))


/* If a character set such as Adobe-Japan1 has been mapped through Unicode,
   vertical forms will have been erroneously mapped to horizontal forms. This
   function tries to apply the font's vertical substitution feature to correct
   these (and only these) mismappings.

   This is a compromise. A PDF consumer should not have to delve into the
   complexity of the GSUB table, and the vertical substitution feature can only
   fix a small fraction of the mismappings caused by mapping via Unicode.

   The Unicode mismapping is exaggerated by the fact that we cannot handle
   one-to-many mapping, and Unicode often relies on Unicode sequences to encode
   the more unusual glyphs that have a unique code in a ROS such as Japan1.

   Although this approach can improve the appearance of rotated punctuation as
   an example, it is important to note that this is not what Acrobat does. It
   instead employs per-glyph font substitution rather than delving into the
   GSUB table. Consequently it is able to display the correct character (though
   not the correct glyph) when the font has no GSUB and even when the font does
   not contain the glyph at all.

   Traversing the GSUB table is highly complicated, and is described in the
   OpenType 1.6 specification under the GSUB and Common Table Formats sections,
   which must be consulted simultaneously. The OpenType spec is available at
   http://www.microsoft.com/typography/otspec/
   */
void tt_apply_gsub_to_cmap(charstring_methods_t * ttfns,
                           TT_DATA * tt_data,
                           cmap_walk_data * cmap_data,
                           TT_CMAP_SUPPLEMENT * vertical_CIDs)
{
  uint32 scriptlist = 0, featurelist = 0, lookuplist = 0 ;
  uint8 * ttmem = NULL ;
  TT_GIDMAP * gidmap = NULL ;
  TT_TABLE * gsub = tt_findTable(tt_data, tt_maptag("GSUB")) ;
  if (gsub)
    ttmem = TT_open(gsub->offset, TT_SIZE_GSUB) ;

  if (ttmem) do { /* Note: We 'do' so we can 'break' */
    /* We have a GSUB table with at least a header */
    int script, langsys, feature, lookup, offset ;
    int count, ftcount, i, index, type, cids ;
    uint32 version, tag ;

    /* GSUB table header - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    TT_fixed(ttmem, 0, version) ;
    TT_offset(ttmem, 4, scriptlist) ;
    TT_offset(ttmem, 6, featurelist) ;
    TT_offset(ttmem, 8, lookuplist) ;
    TT_close(ttmem) ;

    if (version < 0x10000 || version >= 0x20000) {
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("Invalid GSUB table version number: %d", version));
      break ; /* with !ttmem */
    }

    /* Load the features list count - needed later */
    ttmem = TT_open(gsub->offset + featurelist, TT_SIZE_FEATURELIST) ;
    if (!ttmem) break ; /* with !ttmem */
    TT_ushort(ttmem, 0, ftcount) ;
    TT_close(ttmem) ;
    if (!ftcount) {
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("GSUB table seems to have no Features"));
      break ; /* with !ttmem - no features, so nothing we can do */
    }

    /* The inverse subset map - - - - - - - - - - - - - - - - - - - - - - */
    {
      TT_CMAP_SUPPLEMENT * range ;
      int max ;

      /* Find the vertical subset of the current mapping, and invert it to
         form a GID-to-CID map of the vertical glyphs */

      /* Find the maximum size of the subset */
      for (range = vertical_CIDs, max = 0 ; range->startcid > 0 ; ++range)
        max += 1 + range->endcid - range->startcid ;

      gidmap = (TT_GIDMAP *) tt_malloc(max * sizeof(TT_GIDMAP)) ;
      if (!gidmap) {
        HQFAIL("Out of memory for inverse map") ;
        break ; /* with !ttmem */
      }

      /* Extract the subset */
      for (range = vertical_CIDs, cids = 0 ;
           range->startcid > 0 && range->startcid <= cmap_data->maxcid ;
           ++range) {
        uint16 cid, end ;
        end = (uint16) min(range->endcid, cmap_data->maxcid) ;

        for (cid = range->startcid ; cid <= end ; ++cid) {
          uint16 gid = cmap_data->mapping[cid] ;
          /* We don’t bother with notdefs, obviously */
          if (gid && gid != TT_NO_GLYPH) {
            HQASSERT(cids < max, "gidmap overrun") ;
            gidmap[cids].cid = cid ;
            gidmap[cids].gid = gid ;
            ++cids ;
          }
        }
      }

      if (cids == 0) {
        /* Font does not have any vertical glyphs */
        tt_free(gidmap) ;
        break ; /* with !ttmem */
      }

      /* And now invert it */
      qsort(gidmap, cids, sizeof(TT_GIDMAP), gidmap_cmp) ;

      /* gidmap[cids] is now a monotonic map of horizontal GIDs to vertical
         CIDs used to minimise traversals of the GSUB vrt2/vert lookup. */
    }

    /* GSUB:ScriptList - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    /* Load the ScriptList and count the ScriptRecords. */
    ttmem = TT_open(gsub->offset + scriptlist, TT_SIZE_SCRIPTLIST) ;
    if (!ttmem) break ; /* with !ttmem */
    TT_ushort(ttmem, 0, count) ;
    TT_close(ttmem) ;

    if (!count) { /* TODO: Feature fallback */
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("GSUB table appears to have no Scripts"));
      break ; /* with !ttmem */
    }

    ttmem = TT_open(gsub->offset + scriptlist + TT_SIZE_SCRIPTLIST,
                    count * TT_SIZE_SCRIPTRECORD) ;
    if (!ttmem) break ; /* with !ttmem */

    /* We have 'count' ScriptRecords, in which we want to find 'kana' or
       failing that, 'DFLT'. */
    script = 0 ;
    for (i = 0;
         i < count * (int)TT_SIZE_SCRIPTRECORD;
         i += TT_SIZE_SCRIPTRECORD) {
      uint32 tag, offset ;

      TT_ulong(ttmem, i+0, tag) ;
      TT_offset(ttmem, i+4, offset) ;

      if (tag == tt_maptag("kana")) {
        script = scriptlist + offset ;
        break ; /* for */
      } else if (!script && tag == tt_maptag("DFLT"))
        script = offset ;
    }
    TT_close(ttmem) ;
    if (!script) { /* TODO: feature fallback */
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("GSUB table appears to have no kana script"));
      break ; /* with !ttmem */
    }

    /* GSUB:Script - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    /* 'script' is offset from start of ScriptList to a Script table.
       Look there for 'JAN' or use the default langsys */
    ttmem = TT_open(gsub->offset + script, TT_SIZE_SCRIPT) ;
    if (!ttmem) break ; /* with !ttmem */
    TT_offset(ttmem, 0, langsys) ; /* Which is zero if no default langsys */
    TT_ushort(ttmem, 2, count) ; /* Which can be zero */
    TT_close(ttmem) ;

    if (count) {
      ttmem = TT_open(gsub->offset + script + TT_SIZE_SCRIPT,
                      count * TT_SIZE_LANGSYSRECORD) ;
      if (!ttmem) break ; /* with !ttmem */

      for (i = 0;
           i < count * (int)TT_SIZE_LANGSYSRECORD;
           i += TT_SIZE_LANGSYSRECORD) {
        uint32 tag, offset ;

        TT_ulong(ttmem, i+0, tag) ;
        TT_offset(ttmem, i+4, offset) ;

        if (tag == tt_maptag("JAN ")) {
          langsys = offset ;
          break ;
        }
      }
      TT_close(ttmem) ;
    }

    if (!langsys) { /* TODO: Feature fallback */
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("GSUB table appears to have no JAN language"));
      break ;
    }

    /* GSUB:LangSysTable - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    /* 'langsys' is the offset from the start of the script table to the
       LangSys table to search for 'vrt2' or 'vert' features. */
    langsys += script ;
    ttmem = TT_open(gsub->offset + langsys, TT_SIZE_LANGSYSTABLE) ;
    if (!ttmem) break ; /* with !ttmem */
    /* We don't care about LookupOrder offset at 0 */
    TT_ushort(ttmem, 2, index) ; /* ReqFeatureIndex or 0xFFFF */
    TT_ushort(ttmem, 4, count) ; /* Which can be zero */
    TT_close(ttmem) ;
    if (index == 0xFFFF && count == 0) /* TODO: Feature fallback */
      break ;

    /* 'index' (ReqFeatureIndex) /may/ be a vert, but probably isn't. If not,
       we must check the list for 'vrt2' or 'vert'. We load in 2 extra bytes
       so we can easily check 'index' first - when i==0. */
    if (count) {
      ttmem = TT_open(gsub->offset + langsys + TT_SIZE_LANGSYSTABLE - 2,
                      count * 2 + 2) ;
      if (!ttmem) break ; /* with !ttmem */
    }
    feature = 0 ;
    for (i = (index == 0xFFFF) ? 2 : 0; i <= count*2; i += 2) {
      /* Note 'i' is offset by 2, so is 0 for the ReqFeatureIndex. */
      if (i)
        TT_ushort(ttmem, i, index) ;
      if (index < ftcount) { /* Ignore nonsensical feature indices */
        uint8 * ftmem ;
        ftmem = TT_open(gsub->offset + featurelist + TT_SIZE_FEATURELIST +
                        index * TT_SIZE_FEATURERECORD, TT_SIZE_FEATURERECORD) ;
        if (!ftmem) {
          if (ttmem) TT_close(ttmem) ;
          break ; /* with !ttmem */
        }
        TT_ulong(ftmem, 0, tag) ;
        TT_offset(ftmem, 4, offset) ;
        TT_close(ftmem) ;

        if (tag == tt_maptag("vrt2") || tag == tt_maptag("vert")) {
          feature = featurelist + offset ;
          break ; /* for */
        }
      }
    }
    if (ttmem) TT_close(ttmem) ;
    if (!feature) {
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("GSUB table appears to have no suitable vertical feature"));
      break ; /* with !ttmem - no 'vrt2' or 'vert' found */
    }

    /* GSUB:FeatureTable - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    /* We have the offset of the vertical FeatureTable to apply */
    ttmem = TT_open(gsub->offset + feature, TT_SIZE_FEATURETABLE + 2) ;
    /* Don't care about FeatureParams */
    TT_ushort(ttmem, 2, count) ;
    TT_ushort(ttmem, 4, index) ;
    TT_close(ttmem) ;
    if (count < 1) {
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("GSUB Feature table appears to have no features"));
      break ; /* with !ttmem - no features in featurelist */
    }
    /* Since a lookup can contain any number of subtables, there should not
       be multiple lookups for the vertical feature */
    HQASSERT(count == 1, "vrt2/vert feature with more than one lookup") ;

    /* GSUB:LookupList - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
    /* Get the LookupList table count then load the correct offset */
    ttmem = TT_open(gsub->offset + lookuplist, TT_SIZE_LOOKUPLIST) ;
    if (!ttmem) break ; /* with !ttmem */
    TT_ushort(ttmem, 0, count) ;
    TT_close(ttmem) ;

    if (index >= count) {
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("GSUB lookup index out of range"));
      break ; /* with !ttmem - silly lookup index */
    }
    ttmem = TT_open(gsub->offset + lookuplist + TT_SIZE_LOOKUPLIST +
                    index * 2, 2) ;
    if (!ttmem) break ; /* with !ttmem */
    TT_offset(ttmem, 0, offset) ;
    TT_close(ttmem) ;
    lookup = lookuplist + offset ;

    /* GSUB:LookupTable - - - - - - - - - - - - - - - - - - - - - - - - - - */
    ttmem = TT_open(gsub->offset + lookup, TT_SIZE_LOOKUPTABLE) ;
    if (!ttmem) break ; /* with !ttmem */
    TT_ushort(ttmem, 0, type) ; /* We only support single substitutions - 1 */
    /* We ignore the flags - none of which should apply to verts */
    TT_ushort(ttmem, 4, count) ;
    TT_close(ttmem) ;

    if (type != TT_GSUB_SINGLESUBSTITUTION || !count) {
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
              ("GSUB vertical feature is too complex"));
      break ; /* with !ttmem - can only do single substitutions */
    }

    ttmem = TT_open(gsub->offset + lookup + TT_SIZE_LOOKUPTABLE,
                    count*2) ;
    if (!ttmem) break ; /* with !ttmem */
    for (i = 0; i < count; ++i) {
      /* For each Substitution subtable... */
      uint8 * ssmem, * cvmem ;
      int substitution, coverage, j, next, prev ;
      int delta, coveragecount, coverageformat ;

      TT_offset(ttmem, i*2, offset) ;
      substitution = lookup + offset ;

      ssmem = TT_open(gsub->offset + substitution, TT_SIZE_SINGLESUBST) ;
      if (!ssmem) {
        TT_close(ttmem) ;
        break ; /* with !ttmem */
      }

      TT_ushort(ssmem, 0, type) ;
      TT_offset(ssmem, 2, offset) ;
      TT_ushort(ssmem, 4, delta) ; /* Note, delta is signed but count isn't */
      coverage = substitution + offset ;

      if (type == 1) {
        if (delta > 0x7FFF)
          delta -= 65536 ; /* not strictly necessary as we modulo anyway */
      } else if (type == 2) {
        /* Format 2 is followed by an array of shorts */
        TT_close(ssmem) ;
        ssmem = TT_open(gsub->offset + substitution + TT_SIZE_SINGLESUBST,
                        delta * 2) ; /* delta is count */
        if (!ssmem) {
          TT_close(ttmem) ;
          break ; /* with !ttmem */
        }
      } else {
        /* Unrecognised subtable format */
        TT_close(ssmem) ;
        TT_close(ttmem) ;
        break ;
      }

      /* GSUB:Coverage - - - - - - - - - - - - - - - - - - - - - - - - - - -*/
      cvmem = TT_open(gsub->offset + coverage, TT_SIZE_COVERAGE) ;
      if (!cvmem) {
        TT_close(ssmem) ;
        TT_close(ttmem) ;
        break ;
      }
      TT_ushort(cvmem, 0, coverageformat) ;
      TT_ushort(cvmem, 2, coveragecount) ;
      TT_close(cvmem) ;
      if (coverageformat == 1) {
        HQASSERT(type == 1 || delta == coveragecount,
                 "Coverage count mismatch") ;
        cvmem = TT_open(gsub->offset + coverage + TT_SIZE_COVERAGE,
                        coveragecount * 2) ;
      } else if (coverageformat == 2) {
        cvmem = TT_open(gsub->offset + coverage + TT_SIZE_COVERAGE,
                        coveragecount * TT_SIZE_RANGERECORD) ;
      } else {
        TT_close(cvmem) ;
        TT_close(ssmem) ;
        TT_close(ttmem) ;
        break ;
      }

      /* GSUB:CoverageRange - - - - - - - - - - - - - - - - - - - - - - - - */

      /* Now we have the following:
         cvmem: array of glyphs (coverageformat==1) or glyph ranges
         ssmem: array of glyphs (type==2) or not used
         ttmem: array of subtable offsets (used in outer loop)
         delta: glyph increment (type==1) or entries in ssmem array
         gidmap: GID to CID map

         Now apply the substitution (delta/ssmem) to those glyphs in the
         coverage (cvmem) that are present in the font (gidmap) and update
         the cmap_data accordingly:
       */

      next = 0 ; /* index into gidmap - coverage ought to be monotonic */
      prev = -1 ; /* monotonicity check */
      for (j = 0 ; j < coveragecount ; ++j) {
        uint16 gid, startgid, endgid ;

        /* Read in a coverage ID or range - these are GIDs */
        if (coverageformat == 1) {
          TT_ushort(cvmem, j * 2, startgid) ;
          endgid = startgid ;
          index = j ;
        } else {
          int record = j * TT_SIZE_RANGERECORD ;
          TT_ushort(cvmem, record+0, startgid) ;
          TT_ushort(cvmem, record+2, endgid) ;
          TT_ushort(cvmem, record+4, index) ;
        }

        HQASSERT(startgid > prev, "Coverage range not monotonic") ;
        prev = endgid ;

        /* If this coverage range is before our inverse map, try the next */
        if (endgid < gidmap[next].gid)
          continue ;

        /* If this coverage range is after our inverse map, we're done */
        if (startgid > gidmap[cids-1].gid) {
          j = coveragecount ;
          continue ;
        }

        /* Process this range of GIDs */
        for (gid = startgid ; gid <= endgid ; ++gid, ++index) {
          /* Find the next entry in gidmap >= gid */
          for ( ; next < cids && gidmap[next].gid < gid ; ++next) ;
          /* If there isn't one, we're done */
          if (next >= cids) {
            gid = endgid ;
            j = coveragecount ;
            continue ;
          }
          if (gidmap[next].gid == gid) {
            /* At least one CID is mapping to the wrong glyph, fix them */
            uint16 newGID ;
            if (type == 1)
              newGID = (uint16)((delta + gid) & 65535) ; /* wrap round */
            else
              TT_ushort(ssmem, index*2, newGID) ;
            /* It is possible that multiple CIDs map to the same GID in the
               gidmap, so remap all of them. */
            while (next < cids && gidmap[next].gid == gid) {
              cmap_data->mapping[gidmap[next].cid] = newGID ;
              ++next ;
            } /* while gids match */
          } /* if gids match */
        } /* for gid - every gid in this coverage range*/
      } /* for j - every coverage range for this lookup */

      TT_close(cvmem) ;
      TT_close(ssmem) ;
    } /* for i (substitution subtable) */

    /* ttmem is still unclosed here - success */

  } while (FALSE) ; /* ie, just the once - so we could 'break' out early */

  if (gidmap) /* Discard the gidmap if we created it */
    tt_free(gidmap) ;

  if (ttmem) { /* successfully loaded GSUB 'vrt2' or 'vert' feature */
    TT_close(ttmem) ;

  } else { /* failure to find a suitable GSUB */
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0, ("Mapping a CMap"
              " through Unicode may produce incorrect character forms"));
  }
  return ;
}
#undef TT_open
#undef TT_close

static
Bool ensure_global_string(
  OBJECT *ostring)
{
  Bool res = TRUE;

  HQASSERT(ostring, "NULL string object pointer");
  HQASSERT(oType(*ostring) == OSTRING, "object is not a string");

  if (!oGlobalValue(*ostring)) {
    corecontext_t *context = get_core_context_interp();
    OBJECT gl_ostring = OBJECT_NOTVM_NULL;
    Bool glmode = setglallocmode(context, TRUE);
    res = ps_string(&gl_ostring, oString(*ostring), theLen(*ostring));
    setglallocmode(context, glmode);
    Copy(ostring, &gl_ostring);
  }
  return (res);
}

/* -------------------------------------------------------------------------- */

static Bool tt_makeCIDinfo(charstring_methods_t *ttfns, TT_DATA *tt_data,
                           OBJECT *force_cid, OBJECT *match, Bool *is_cid,
                           OBJECT *ros, OBJECT *cidmap, int32 *cidcount)
{
  OBJECT *font_desc;
  OBJECT *font_dir;
  OBJECT *cidsysinfo;
  OBJECT cmap = OBJECT_NOTVM_NULL, registry = OBJECT_NOTVM_NULL,
    ordering = OBJECT_NOTVM_NULL ;
  int32 supplement = 0 ;
  void *basemap ;
  uint32 sema, preferred = 0;
  Bool result = FALSE, japan1 = FALSE ;
  Bool hqn_unicode = FALSE, try_identity = FALSE, try_fontname ;
  cmap_walk_data cmap_data = {
    NULL,  /* basemap */
    0,     /* basemapsize */
    TRUE,  /* Assume identity mapping until told otherwise. */
    0,     /* number of CIDs defined in cmap */
    0,     /* maximum CID defined in cmap */
    NULL   /* TT cmap entry */
  } ;

  /* Unicode cmap types, in preferred order */
  static PEL_PRIORITY unicode_cmaps[] = {
    { TT_PID_MICROSOFT, TT_EID_WIN_UGL, TT_LID_UNDEFINED },
    { TT_PID_MICROSOFT, TT_EID_WIN_SYMBOL, TT_LID_UNDEFINED },
    { TT_PID_APPLE_UNICODE, TT_EID_UNDEFINED, TT_LID_UNDEFINED },
    { TT_PID_ISO, TT_EID_ISO_10646, TT_LID_UNDEFINED },
  } ;

  HQASSERT(tt_data, "No TT data") ;
  HQASSERT(is_cid && ros && cidmap, "Nowhere for CID info results") ;

  /* The cidflag flag passed in may be either a boolean, or null
     (unspecified). If the font is determined to be a CID font, the ROS,
     CIDMap, and CIDCount will be determined and extracted. The cidflag
     indicates:

     FALSE: Font is NOT a CID font.
     TRUE:  Font IS a CID font. If the cidsysinfo is a dictionary it must be
            matched. Otherwise, if there is a CIDFONTNAME entry in the name
            table and a matching cmap entry, correlate the CMap and cmap entry
            to get the CIDMap, use the R-O from the CIDFONTNAME CMap.
            Otherwise, if there is only one character set supported in the
            OS/2 table code page flags, select that character set, and use a
            ToUnicode mapping and unicode cmap entry to get the CIDMap. If
            there are multiple character sets, or they are indeterminate, but
            there is a Unicode cmap entry, use Harlequin-Unicode and make a
            unicode CIDMap. Finally, if all of the above fail, use an Identity
            CID->GID mapping.
     NULL:  Font MAY be a CID font. If the cidsysinfo is a dictionary it must
            be matched in order to indicate a CID font. If there is a
            CIDFONTNAME entry in the name table and a matching cmap entry,
            correlate the CMap and cmap entry to get the CIDMap, use the R-O
            from the CIDFONTNAME CMap. Otherwise, if there is only one
            character set supported in the OS/2 table code page flags, select
            that character set, and use a ToUnicode mapping and unicode cmap
            entry to get the CIDMap. If there are multiple character sets and
            there is a Unicode cmap entry, use Harlequin-Unicode and make a
            unicode CIDMap. Finally, if all of the above fail, the font is not
            a CID font.

     The cidsysteminfo object indicates if a particular Registry and Ordering
     must be used. If the cid flag is true, and the ROS cannot be matched, then
     an error is returned. If the preferred ROS is Adobe-Identity, an identity
     CID->GID mapping is used. If the preferred ROS is Harlequin-Unicode
     and there is a Unicode cmap entry, a unicode CIDMap is created.
  */
  /** \todo @@@ TODO FIXME ajcd 2003-04-24:
     These methods currently do not take into
     account that there may be multiple CIDFONTNAME entries and corresponding
     cmap entries. Also, if the ordering is Japan1, we should be able to use
     a Microsoft ShiftJIS cmap in place of a Unicode map to derive a CIDMap.
     Likewise for GB1 (PRC), CNS1 (Big5), Korea1 (Wansung,Johab). UCS4 is
     currently not recognised, nor are mixed UCS2/UCS4 with surrogates cmaps.
  */

  /* is_cid is used as a flag to determine if we know the ROS yet. */
  *is_cid = FALSE ;
  object_store_integer(cidmap, 0) ; /* for safety */
  *cidcount = 0 ; /* for safety */

  /* For PDFs a copy of the ROS is squirrelled away in internaldict, so we can
   * fish that out find a suitable CMap.  If one does not exist we then try the
   * other heuristics for deriving one. [367713]
   */
  if ((font_desc = fast_extract_hash_name(&internaldict,
                                          NAME_FontDescriptor)) != NULL &&
      (font_dir = fast_extract_hash_name(font_desc,
                                         NAME_FontDirectory)) != NULL &&
      (cidsysinfo = fast_extract_hash_name(font_dir,
                                           NAME_CIDSystemInfo)) != NULL) {
    /* By allowing the ROS resource name to exceed the supported max PS name
     * length then cachename will detect the error for us rather than appearing
     * from somewhere within the CMap resource lookup.
     */
    uint8 ros_name[MAXPSNAME + 4];
    int32 ros_len;
    Bool exists;

    /* Get the registry and ordering and ensure they are in global VM */
    if (!tt_unpackCIDSystemInfo(cidsysinfo, &registry, &ordering, &supplement)) {
      return (FALSE);
    }
    if (!ensure_global_string(&registry) || !ensure_global_string(&ordering)) {
      return (FALSE);
    }

    /* Catch special case CMap since fall-through logic is tortuous.
       Identity does not require mapping, by definition.
       Note: applying Identity to an unembedded font is asking for trouble. */
    if (HqMemCmp(oString(ordering), theLen(ordering),
                 STRING_AND_LENGTH("Identity")) == 0) {
      *is_cid = TRUE ;
      object_store_integer(cidmap, 0);
      *cidcount = tt_data->tt_maxp.numGlyphs;
      return (tt_makeCIDSystemInfo(ros, &registry, &ordering, 0));
    }
    /* Check that the required CMap actually exists otherwise the lookup fails
     * raising a PS error and leaving objects on the operand stack.
     */
    ros_len = swncopyf(ros_name, sizeof(ros_name), (uint8*)"%.*s-%.*s-UCS2%N",
                       theLen(registry), oString(registry),
                       theLen(ordering), oString(ordering));
    theTags(cmap) = ONAME|LITERAL;
    if ((oName(cmap) = cachename(ros_name, ros_len)) == NULL) {
      return (FALSE);
    }
    if (!tt_CMapResourceStatus(&cmap, &exists)) {
      return (FALSE);
    }
    if (exists && tt_findCMapResource(&cmap) &&
        (cmap_data.cmap = tt_findCmap(tt_data, unicode_cmaps,
                                      NUM_ARRAY_ITEMS(unicode_cmaps))) != NULL) {
      *is_cid = TRUE;

      /* Is GSUB remapping required? [307872] */
      if (HqMemCmp(oString(registry), theLen(registry),
                   STRING_AND_LENGTH("Adobe")) == 0) {
        if (HqMemCmp(oString(ordering), theLen(ordering),
                  STRING_AND_LENGTH("Japan1")) == 0)
          japan1 = TRUE ;
        /* Other orderings would be detected here */
      }
    }
  }

  if (!*is_cid) {
    /* Check if forced to be or not be a CID font.  */
    if ( force_cid ) {
      HQASSERT(oType(*force_cid) == OBOOLEAN,
               "Force CID flag is not boolean") ;
      if ( !oBool(*force_cid) ) /* It's not a CID font */
        return TRUE ;

      /* Forced to be a CID font. We'll determine if it must match a CMap just
         below. Default to allowing any one charset (else clause of
         CIDSystemInfo match test below), or Harlequin-Unicode. Fallback to
         Identity if no others applicable. */
      try_identity = TRUE ;
      hqn_unicode = TRUE ;
    } else {
      try_identity = FALSE ;
      hqn_unicode = FALSE ;
    }

    if ( match ) {
      HQASSERT(oType(*match) == ODICTIONARY,
               "CID system info match is wrong type") ;
      /* Set the preferred charset from the OS/2 code page flags. */
      if ( !tt_unpackCIDSystemInfo(match, &registry, &ordering, &supplement) )
        return FALSE ;

      /* Set default values for OS/2 charset, identity and unicode so that
         unrecognised ordering only allows CIDFONTNAME name entry to match.
         If we're matching a CMap exactly, don't fallback to Identity or Unicode
         unless explicitly told to match. */
      preferred = 0 ;
      try_fontname = TRUE ;
      try_identity = FALSE ;
      hqn_unicode = FALSE ;

      /* Adobe-Japan2 is not included in this list because there is no
         ToUnicode map for it. We cannot distinguish between Japan1 and Japan2
         in the OS/2 flags anyway, and Japan1 is far more common. */
      if ( HqMemCmp(oString(registry), theLen(registry),
                    STRING_AND_LENGTH("Adobe")) == 0 ) {
        if ( HqMemCmp(oString(ordering), theLen(ordering),
                      STRING_AND_LENGTH("Identity")) == 0 ) {
          try_fontname = FALSE ;
          try_identity = TRUE ;
        } else if ( HqMemCmp(oString(ordering), theLen(ordering),
                             STRING_AND_LENGTH("Japan1")) == 0 ) {
          preferred = TT_OS2_JAPANESE_JIS ;
        } else if ( HqMemCmp(oString(ordering), theLen(ordering),
                             STRING_AND_LENGTH("CNS1")) == 0 ) {
          preferred = TT_OS2_CHINESE_TRAD ;
        } else if ( HqMemCmp(oString(ordering), theLen(ordering),
                             STRING_AND_LENGTH("GB1")) == 0 ) {
          preferred = TT_OS2_CHINESE_SIMPLE ;
        } else if ( HqMemCmp(oString(ordering), theLen(ordering),
                             STRING_AND_LENGTH("Korea1")) == 0 ) {
          preferred = TT_OS2_KOREAN_WANSUNG|TT_OS2_KOREAN_JOHAB ;
        }
      } else if ( HqMemCmp(oString(registry), theLen(registry),
                           STRING_AND_LENGTH("Harlequin")) == 0 &&
                  HqMemCmp(oString(ordering), theLen(ordering),
                           STRING_AND_LENGTH("Unicode")) == 0 ) {
        /* Harlequin Unicode matches only through unicode cmap entry, not OS/2
           charsets or CIDFONTNAME. */
        try_fontname = FALSE ;
        hqn_unicode = TRUE ;
      }
    } else {
      /* Not specific CMap match. Allow any one charset to match. */
      try_fontname = TRUE ;
      preferred = (TT_OS2_JAPANESE_JIS|
                   TT_OS2_CHINESE_TRAD|TT_OS2_CHINESE_SIMPLE|
                   TT_OS2_KOREAN_JOHAB|TT_OS2_KOREAN_WANSUNG) ;
      HQASSERT(oType(registry) == ONULL, "Registry is not ONULL") ;
      HQASSERT(oType(ordering) == ONULL, "Ordering is not ONULL") ;
      supplement = 0 ;
    }

    /* Test for CIDFONTNAME first. If that fails, test OS/2 charset flags, Hqn
       Unicode and Identity in that order. */
    if ( try_fontname && tt_cidfontname_cmap(tt_data, &cmap, &cmap_data.cmap) ) {
      OBJECT *cidsysinfo = fast_extract_hash_name(&cmap, NAME_CIDSystemInfo) ;
      if ( match ) {
        /* Check registry and ordering match CMap. */
        if ( !tt_compareCIDSystemInfo(cidsysinfo, &registry, &ordering, supplement, is_cid) )
          return FALSE ;
      } else {
        /* Use CMap's registry and ordering */
        if ( !tt_unpackCIDSystemInfo(cidsysinfo, &registry, &ordering, &supplement) )
          return FALSE ;
        *is_cid = TRUE ;
      }
    }
  }

  /* CIDFONTNAME didn't match? Try OS/2 flags. */
  if ( !*is_cid && preferred != 0 ) {
    char *tounicode ;

    HQASSERT(tt_data->tt_os2.tried, "OS/2 table not loaded") ;

    /* Check if there is a single charset defined from the preferred
       sets. */
    switch ( preferred & tt_data->tt_os2.ulCodePageRange1 ) {
    case TT_OS2_JAPANESE_JIS:
      tounicode = "Adobe-Japan1-UCS2" ;
      japan1 = TRUE ;
      break ;
    case TT_OS2_CHINESE_TRAD:
      tounicode = "Adobe-CNS1-UCS2" ;
      break ;
    case TT_OS2_CHINESE_SIMPLE:
      tounicode = "Adobe-GB1-UCS2" ;
      break ;
    case TT_OS2_KOREAN_WANSUNG:
    case TT_OS2_KOREAN_JOHAB:
    case TT_OS2_KOREAN_WANSUNG|TT_OS2_KOREAN_JOHAB:
      tounicode = "Adobe-Korea1-UCS2" ;
      break ;
    default: /* More than one character set present. If auto-detecting, allow
                Harlequin-Unicode. */
      if ( !force_cid )
        hqn_unicode = TRUE ;
      /* FALLTHROUGH */
    case 0: /* None of the character sets are present */
      tounicode = NULL ;
      break ;
    }

    if ( tounicode ) {
      /* Try to find an appropriate ToUnicode CMap.  */
      theTags(cmap) = ONAME | LITERAL ;
      if ( (oName(cmap) = cachename((uint8 *)tounicode,
                                    strlen_uint32(tounicode))) == NULL )
        return FALSE ;

      if ( tt_findCMapResource(&cmap) &&
           (cmap_data.cmap = tt_findCmap(tt_data, unicode_cmaps,
                                         NUM_ARRAY_ITEMS(unicode_cmaps))) != NULL ) {
        /* Extract first two components from the ToUnicode name for the
           registry and ordering. Once upon a time, Adobe sensibly used the
           Registry and Ordering of the character collection being mapped to
           Unicode. This is no longer the case, so we have to assume the cmap
           is correct. */
        uint16 length ;

        for ( length = 0 ; tounicode[length] != '-' ; ++length )
          HQASSERT(tounicode[length] != '\0', "ToUnicode Registry not terminated") ;

        if ( !ps_string(&registry, (uint8 *)tounicode, length) )
          return FALSE ;

        for ( tounicode += length + 1, length = 0 ; tounicode[length] != '-' ; ++length )
          HQASSERT(tounicode[length] != '\0', "ToUnicode Ordering not terminated") ;

        if ( !ps_string(&ordering, (uint8 *)tounicode, length) )
          return FALSE ;

        supplement = 0 ;
        *is_cid = TRUE ;
      }
    }
  }

  /* CIDFONTNAME and OS/2 flags didn't match? Try Harlequin-Unicode. */
  if ( !*is_cid && hqn_unicode ) {
    if ( (cmap_data.cmap = tt_findCmap(tt_data, unicode_cmaps,
                                       NUM_ARRAY_ITEMS(unicode_cmaps))) != NULL ) {
      if ( !ps_string(&registry, STRING_AND_LENGTH("Harlequin")) ||
           !ps_string(&ordering, STRING_AND_LENGTH("Unicode")) )
        return FALSE ;

      object_store_null(&cmap) ;
      supplement = 0 ;
      *is_cid = TRUE ;
    }
  }

  /* CIDFONTNAME, OS/2 flags and Harlequin-Unicode didn't match?. Try
     identity. */
  if ( !*is_cid && try_identity ) {
    HQASSERT(tt_data->tt_maxp.loaded, "maxp table not loaded") ;

    /* Identity means CID->GID identity mapping. We've got enough
       information to finish now. */
    *is_cid = TRUE ;
    object_store_integer(cidmap, 0) ;
    *cidcount = tt_data->tt_maxp.numGlyphs ;
    return (ps_string(&registry, STRING_AND_LENGTH("Adobe")) &&
            ps_string(&ordering, STRING_AND_LENGTH("Identity")) &&
            tt_makeCIDSystemInfo(ros, &registry, &ordering, 0)) ;
  }

  /* STILL not a CID font? Give up in disgust. */
  if ( !*is_cid ) {
    if ( force_cid )
      return error_handler(RANGECHECK) ;

    return TRUE ;
  }

  /* After grabbing the basemap semaphore, we MUST free it before returning.
     Jump to tt_cidmap_error to do this. */
  if ( (sema = get_basemap_semaphore(&basemap, &cmap_data.mapsize)) == 0 )
    return FALSE ;

  /* The basemap size is checked and incrementally cleared as necessary.
     Initially, check there is enough room for the undefined glyph. */
  cmap_data.mapping = basemap ;
  if ( cmap_data.mapsize < 2 )
    goto tt_cidmap_error ;

  cmap_data.mapping[0] = TT_CIDMAP_UNDEFINED ;

  if ( oType(cmap) == ODICTIONARY ) {
    if ( !cmap_walk(&cmap, cmap_to_cidmap, &cmap_data) )
      goto tt_cidmap_error ;
  } else if ( !hqn_unicode_cidmap(&cmap_data) )
    goto tt_cidmap_error ;

  /* There MUST be a mapping for CID 0 (the undefined glyph). If not already
     set, make it GID 0. maxcid is initialised to 0, so we don't need to
     increase it here. This is an identity-preserving mapping. */
  if ( cmap_data.mapping[0] == TT_CIDMAP_UNDEFINED ) {
    cmap_data.mapping[0] = 0 ;
    ++cmap_data.ncids ;
  }

  /* [307872] If we created a Japan1 mapping via Unicode, then use the GSUB
     table if present to remap certain character ranges from this Ordering.
     This is mainly Supplements 3 & 4 (vertical forms of 0-2), but also some
     ranges from Supplements 0 & 3-6.

     Ultimately we may want to handle other Orderings too, as defined by Ken
     Lunde who wrote "Supplements that were defined strictly for [vertical
     substitution] include Adobe-GB1-3, Adobe-CNS1-2, Adobe-Japan1-3 and
     Adobe-Korea1-2".

     That quote is slightly misleading as the GID ranges that require remapping
     are not limited only to the supplements he mentions. For now, we deal with
     Japan1 only:
   */
  if (japan1) {
    /* See Adobe-Japan1-6 (Adobe TN #5078) for derivation of ranges... */
    static TT_CMAP_SUPPLEMENT japan1_vertical_CIDs[] = {
      {  7887,  7917}, /* A-J1-0 contains rotated punctuation */
      {  8720,  9353}, /* A-J1-3 is entirely vertical */
      { 12870, 13319}, /* A-J1-4 vertical subrange */
      { 15976, 16192}, /* A-J1-5 vertical proportional subrange */
      { 16382, 16411}, /* A-J1-5 vertical kana subrange */
      { 16469, 16778}, /* A-J1-5 vertical latin & italic subranges */
      { 20961, 21070}, /* A-J1-6 vertical prop. & italic subranges */
      { 0, 0 }
    } ;
    tt_apply_gsub_to_cmap(ttfns, tt_data, &cmap_data, japan1_vertical_CIDs) ;
  }

  if ( cmap_data.identity ) {
    /* An identity mapping can be implemented with a simple offset. */
    object_store_integer(cidmap, 0) ;
  } else if ( cmap_data.ncids * sizeof(OBJECT) < cmap_data.maxcid ||
              cmap_data.maxcid > MAXPSSTRING ) {
    /* If the CIDMap is sparse, it is more efficient to use a dictionary
       rather than a string or strings. We've removed a factor of two from
       both sides of the conditional expression. A dictionary is also used in
       case we allow full UCS-4 mappings and the maxcid is greater than
       MAXPSSTRING. */
    OBJECT from = OBJECT_NOTVM_NOTHING, to = OBJECT_NOTVM_NOTHING ;
    uint32 cid ;

    if ( !ps_dictionary(cidmap, cmap_data.ncids) )
      goto tt_cidmap_error ;

    /* Insert mapping for CID 0 always. Other mappings are only inserted if
       they do not map the the undefined GID. */
    object_store_integer(&from, 0) ;
    object_store_integer(&to, cmap_data.mapping[0]) ;
    if ( !insert_hash(cidmap, &from, &to) )
      goto tt_cidmap_error ;

    for ( cid = 1 ; cid <= cmap_data.maxcid ; ++cid ) {
      if ( cmap_data.mapping[cid] != TT_CIDMAP_UNDEFINED ) {
        object_store_integer(&from, cid) ;
        object_store_integer(&to, cmap_data.mapping[cid]) ;

        if ( !insert_hash(cidmap, &from, &to) )
          goto tt_cidmap_error ;
      }
    }
  } else {
    /* One or more strings required. Note that maxcid is the last CID index. */
    uint32 extrastrings = cmap_data.maxcid / (MAXPSSTRING >> 1) ;
    uint32 cid = 0, index ;

    if ( extrastrings > 0 ) {
      /* More than one string required, in an array. If we fail after this,
         the strings and array will eventually be restored or garbage
         collected. This conditional will do the initial set of strings,
         the final string mapping is shared between the array and single
         string case */
      if ( !ps_array(cidmap, extrastrings + 1) )
        goto tt_cidmap_error ;

      cidmap = oArray(*cidmap) ;
      do {
        if ( !ps_string(cidmap, NULL, MAXPSSTRING & ~1) )
          goto tt_cidmap_error ;

        for ( index = 0 ; index < (MAXPSSTRING & ~1) ; index += 2 ) {
          int32 gid = cmap_data.mapping[cid++] ;

          oString(*cidmap)[index] = (uint8)(gid >> 8) ;
          oString(*cidmap)[index + 1] = (uint8)gid ;
        }

        ++cidmap ;
      } while ( --extrastrings > 0 ) ;
    }

    if ( cid <= cmap_data.maxcid ) {
      /* Finish off any remaining CID mappings. This also takes care of the
         case in which the mapping fitted in a single string. */
      if ( !ps_string(cidmap, NULL, (cmap_data.maxcid - cid + 1) << 1) )
        goto tt_cidmap_error ;

      for ( index = 0 ; cid <= cmap_data.maxcid ; index += 2 ) {
        int32 gid = cmap_data.mapping[cid++] ;

        oString(*cidmap)[index] = (uint8)(gid >> 8) ;
        oString(*cidmap)[index + 1] = (uint8)gid ;
      }
    }
  }

  /* Let's play "guess the supplement!" by poking at the CIDMap we've just
     created and seeing what's in it. */
  supplement = cidmap_guess_supplement(cmap_data.mapping, cmap_data.maxcid,
                                       &registry, &ordering) ;
  result = tt_makeCIDSystemInfo(ros, &registry, &ordering, supplement) ;

  *cidcount = (int32)(cmap_data.maxcid + 1) ;

tt_cidmap_error:
  free_basemap_semaphore(sema) ;

  return result ;
}

/* -------------------------------------------------------------------------- */
static Bool tt_readpostTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  /* (6) Read in and cache all the 'post' header, checking it is valid.
   * Type       Name                    Description
   * Fixed      Format Type             0x00010000 for format 1.0, 0x00020000 for
   *                                    format 2.0, and so on...
   * Fixed      italicAngle             Italic angle in counter-clockwise degrees from
   *                                    the vertical. Zero for upright text, negative
   *                                    for text that leans to the right (forward)
   * FWord      underlinePosition       Suggested values for the underline position
   *                                    (negative values indicate below baseline).
   * FWord      underlineThickness      Suggested values for the underline thickness.
   * ULONG      isFixedPitch            Set to 0 if the font is proportionally spaced,
   *                                    non-zero if the font is not proportionally
   *                                    spaced (i.e. monospaced).
   * ULONG      minMemType42            Minimum memory usage when a TrueType font is.
   *                                    downloaded as a Type 42 font.
   * ULONG      maxMemType42            Maximum memory usage when a TrueType font is.
   *                                    downloaded as a Type 42 font.
   * ULONG      minMemType1             Minimum memory usage when a TrueType font is
   *                                    downloaded as a Type 1 font.
   * ULONG      maxMemType1             Maximum memory usage when a TrueType font is
   *                                    downloaded as a Type 1 font.
   */

   int32 FormatType ;
   int32 italicAngle ;
   int16 underlinePosition ;
   int16 underlineThickness ;
  uint32 isFixedPitch ;
  uint32 minMemType42 ;
  uint32 maxMemType42 ;
  uint32 minMemType1 ;
  uint32 maxMemType1 ;

  int32 i ;
  uint16 numGlyphs ;
  NAMECACHE **GlyphNames ;

  TT_TABLE *Tab_post;
  TT_POST *Post_data = ( & TT_data->tt_post ) ;
  uint32 offset, length ;
  uint8 *ttmem ;

  if ( Post_data->tried )
    return TRUE ;

  Post_data->tried = TRUE ;
  Post_data->repaired = FALSE ;
  if ( (Tab_post = tt_findTable(TT_data, tt_maptag("post"))) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  offset = Tab_post->offset ;
  length = Tab_post->length ;

  if (length < TT_SIZE_POST ||
      (ttmem = (*ttfns->open_frame)(ttfns->data, offset, TT_SIZE_POST)) == NULL)
    return error_handler( INVALIDFONT ) ;

  TT_fixed ( ttmem ,  0 , FormatType ) ;
  TT_fixed ( ttmem ,  4 , italicAngle ) ;
  TT_fword ( ttmem ,  8 , underlinePosition ) ;
  TT_fword ( ttmem , 10 , underlineThickness ) ;
  TT_ulong ( ttmem , 12 , isFixedPitch ) ;
  TT_ulong ( ttmem , 16 , minMemType42 ) ;
  TT_ulong ( ttmem , 20 , maxMemType42 ) ;
  TT_ulong ( ttmem , 24 , minMemType1 ) ;
  TT_ulong ( ttmem , 28 , maxMemType1 ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  offset += TT_SIZE_POST ;
  length -= TT_SIZE_POST ;

  HQASSERT(TT_data->tt_maxp.loaded, "maxp table not loaded") ;
  numGlyphs = TT_data->tt_maxp.numGlyphs ;

  Post_data->numPostGlyphs = 0 ;
  Post_data->GlyphNames = NULL ;

  Post_data->FormatType = FormatType ;
  Post_data->italicAngle = italicAngle ;
  Post_data->underlinePosition = underlinePosition ;
  Post_data->underlineThickness = underlineThickness ;
  Post_data->isFixedPitch = ( uint8 )( isFixedPitch != 0 ? TRUE : FALSE ) ;

  Post_data->minMemType42 = minMemType42 ;
  Post_data->maxMemType42 = maxMemType42 ;

  switch ( FormatType ) {
    uint16 tnumGlyphs, maxGlyphIndex, actualGlyphs ;
    uint16 *glyphIndexes ;
    NAMECACHE **glyphStrings ;

  case 0x00010000:
    HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,
            ("post table FormatType: 0x%08x",FormatType));

    tnumGlyphs = (uint16)min(NUM_ARRAY_ITEMS(MacTTGlyphNames), numGlyphs) ;

    Post_data->numPostGlyphs = tnumGlyphs ;
    Post_data->GlyphNames = GlyphNames =
      mm_alloc(mm_pool_temp, tnumGlyphs * sizeof(NAMECACHE *), MM_ALLOC_CLASS_TTFONT) ;
    if ( ! GlyphNames )
      return error_handler(VMERROR) ;

    for ( i = 0 ; i < tnumGlyphs ; ++i )
      GlyphNames[ i ] = MacTTGlyphNames[i] ;

    break ;

  case 0x00020000:
    HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,
            ("post table FormatType: 0x%08x",FormatType));

    /* We allow zero length post data. See Request #65547 */
    if (length != 0) {
      if ( length < 2 ||
           (ttmem = (*ttfns->open_frame)(ttfns->data, offset, 2)) == NULL )
        return error_handler( INVALIDFONT ) ;

      TT_ushort( ttmem , 0 , actualGlyphs ) ;
      (*ttfns->close_frame)(ttfns->data, &ttmem) ;

      offset += 2 ;
      length -= 2 ;

      tnumGlyphs = min(actualGlyphs, numGlyphs) ; /* all we can use */

      if ( actualGlyphs > numGlyphs ) {
        HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
                ("more glyphs in post table (%d) than in head (%d)",
                 actualGlyphs, numGlyphs)) ;
      }

      Post_data->numPostGlyphs = tnumGlyphs ;
      Post_data->GlyphNames = GlyphNames =
        mm_alloc(mm_pool_temp, tnumGlyphs * sizeof(NAMECACHE *), MM_ALLOC_CLASS_TTFONT) ;
      if ( ! GlyphNames )
        return error_handler(VMERROR) ;

      /* Load the glyph indexes into temporary storage */
      glyphIndexes = mm_alloc(mm_pool_temp,
                              tnumGlyphs * sizeof(uint16),
                              MM_ALLOC_CLASS_TTFONT) ;
      if ( ! glyphIndexes )
        return error_handler(VMERROR) ;

      if ( length < (uint32)tnumGlyphs * 2 ||
           (ttmem = (*ttfns->open_frame)(ttfns->data, offset, tnumGlyphs * 2)) == NULL ) {
        mm_free(mm_pool_temp, glyphIndexes, tnumGlyphs * sizeof(uint16)) ;
        return error_handler( INVALIDFONT ) ;
      }

      for ( i = 0, maxGlyphIndex = 0 ; i < tnumGlyphs ; ++i ) {
        uint16 glyphNameIndex ;
        TT_ushort( ttmem , i << 1 , glyphNameIndex ) ;
        glyphIndexes[ i ] = glyphNameIndex ;
        if ( glyphNameIndex > maxGlyphIndex )
          maxGlyphIndex = glyphNameIndex ;
      }

      (*ttfns->close_frame)(ttfns->data, &ttmem) ;

      offset += actualGlyphs * 2 ;

      /* We skip the actual glyphs present making sure we do not
         underflow. */
      if (length < (uint32)(actualGlyphs * 2)) {
        length = 0 ;
      } else {
        length -= actualGlyphs * 2 ;
      }

      if ( maxGlyphIndex >= NUM_ARRAY_ITEMS(MacTTGlyphNames) ) {
        /* Read the Pascal strings into temporary storage. */
        maxGlyphIndex -= NUM_ARRAY_ITEMS(MacTTGlyphNames) ;

        glyphStrings = mm_alloc(mm_pool_temp,
                                (maxGlyphIndex + 1) * sizeof(NAMECACHE *),
                                MM_ALLOC_CLASS_TTFONT) ;
        if ( ! glyphStrings ) {
          mm_free(mm_pool_temp, glyphIndexes, tnumGlyphs * sizeof(uint16)) ;
          return error_handler(VMERROR) ;
        }

        if ( (ttmem = (*ttfns->open_frame)(ttfns->data, offset, length)) == NULL ) {
          mm_free(mm_pool_temp, glyphStrings, (maxGlyphIndex + 1) * sizeof(NAMECACHE *)) ;
          mm_free(mm_pool_temp, glyphIndexes, tnumGlyphs * sizeof(uint16)) ;
          return error_handler( INVALIDFONT ) ;
        }

        for ( i = 0, offset = 0 ; offset < length && i <= maxGlyphIndex ; ++i ) {
          uint32 next = offset + ttmem[offset] + 1 ;

          if ( ttmem[offset] > 0 ) {
            /* Pascal string(!) */
            if ( next > length ||
                 (glyphStrings[i] = cachename(ttmem + offset + 1,
                                              ttmem[offset])) == NULL ) {
              mm_free(mm_pool_temp, glyphStrings, (maxGlyphIndex + 1) * sizeof(NAMECACHE *)) ;
              mm_free(mm_pool_temp, glyphIndexes, tnumGlyphs * sizeof(uint16)) ;
              return error_handler(VMERROR) ;
            }
          } else {
            glyphStrings[i] = NULL ;
          }

          offset = next ;
        }

        for ( ; i <= maxGlyphIndex ; ++i )
          glyphStrings[i] = NULL ;

        (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      } else {
        glyphStrings = NULL ;
        maxGlyphIndex = 0 ;
      }

      /* Re-order the Pascal strings according to the indices. Duplicate indices
         are possible. */
      for ( i = 0 ; i < tnumGlyphs ; ++i ) {
        uint16 glyphNameIndex = glyphIndexes[i] ;

        if ( glyphNameIndex < NUM_ARRAY_ITEMS(MacTTGlyphNames) ) {
          GlyphNames[i] = MacTTGlyphNames[glyphNameIndex] ;
        } else {
          glyphNameIndex -= NUM_ARRAY_ITEMS(MacTTGlyphNames) ;
          GlyphNames[i] = glyphStrings[glyphNameIndex] ;
        }
      }

      if ( glyphIndexes )
        mm_free(mm_pool_temp, glyphIndexes, tnumGlyphs * sizeof(uint16)) ;
      if ( glyphStrings )
        mm_free(mm_pool_temp, glyphStrings, (maxGlyphIndex + 1) * sizeof(NAMECACHE *)) ;

    } else {

      /* NULL everything so they do not get used. */
      tnumGlyphs = actualGlyphs = 0;
      Post_data->GlyphNames = GlyphNames = NULL;
      glyphIndexes = NULL;
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0, ("zero length data in post table")) ;
    }

    break ;

  case 0x00028000:
    HQFAIL("UNTESTED, DEPRECATED 'post' table format");
    HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,("UNTESTED FormatType: 0x00028000"));
    /* The specification for this table appears to have changed between
       TrueType 1.66 and OpenType 1.4. The more recent version has a USHORT
       numGlyphs at the start of the table, in the older version it is
       implicitly the 'maxp' numGlyphs. The Apple definition agrees with the
       more recent definition, so we'll use this. */

    if ( length < 2 ||
         (ttmem = (*ttfns->open_frame)(ttfns->data, offset, 2)) == NULL )
      return error_handler( INVALIDFONT ) ;

    TT_ushort( ttmem , 0 , tnumGlyphs ) ;

    (*ttfns->close_frame)(ttfns->data, &ttmem) ;

    offset += 2 ;
    length -= 2 ;

    Post_data->numPostGlyphs = tnumGlyphs ;
    Post_data->GlyphNames = GlyphNames =
      mm_alloc(mm_pool_temp, tnumGlyphs * sizeof(NAMECACHE *), MM_ALLOC_CLASS_TTFONT) ;
    if ( ! GlyphNames )
      return error_handler(VMERROR) ;

    if ( length < tnumGlyphs ||
         (ttmem = (*ttfns->open_frame)(ttfns->data, offset, tnumGlyphs)) == NULL )
      return error_handler( INVALIDFONT ) ;

    for ( i = 0 ; i < tnumGlyphs ; ++i ) {
      int8 diff ;
      uint16 glyphNameIndex ;
      TT_char( ttmem , i , diff ) ;
      glyphNameIndex = ( uint16 )( i + diff ) ; /* Underflow OK */
      if ( glyphNameIndex < NUM_ARRAY_ITEMS(MacTTGlyphNames) )
        GlyphNames[i] = MacTTGlyphNames[i] ;
      else
        GlyphNames[i] = NULL ;
    }

    (*ttfns->close_frame)(ttfns->data, &ttmem) ;
    break ;

  case 0x00030000:
    HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,
            ("post table FormatType: 0x%08x",FormatType));
    break ;

  case 0x00040000:
    /* Format 4.0 'post' tables are not standard in the TT or OpenType
       specifications, but are used by the Apple PostScript drivers for
       composite fonts. The contain a table of unsigned shorts, one for each
       glyph, giving the (primary) character code from which the glyph index
       was mapped. See:

       http://developer.apple.com/fonts/TTRefMan/RM06/Chap6post.html
    */
    HQTRACE((debug_tt & DEBUG_TT_INFO) != 0,
            ("post table FormatType: 0x%08x",FormatType));

    /* Table might be too short (is has happened!), so use the lesser of the two extents */
    if (numGlyphs > (uint16)(length>>1))
      numGlyphs = (uint16)(length>>1) ;

    Post_data->numPostGlyphs = numGlyphs ;
    Post_data->GlyphNames = GlyphNames =
      mm_alloc(mm_pool_temp, numGlyphs * sizeof(NAMECACHE *), MM_ALLOC_CLASS_TTFONT) ;
    if ( ! GlyphNames )
      return error_handler(VMERROR) ;

    /* No need to check length as numGlyphs is set above if length is too short. */
    if ( (ttmem = (*ttfns->open_frame)(ttfns->data, offset, numGlyphs * 2)) == NULL )
      return error_handler( INVALIDFONT ) ;

    /* No more data to read so no need to continue tracking length and offset.
    length -= numGlyphs * 2;
    offset += numGlyphs * 2;
     */

    for ( i = 0 ; i < numGlyphs ; ++i ) {
      uint16 glyphNameIndex ;
      uint8 buffer[8] ; /* Enough for 'aXXXX' */

      TT_ushort( ttmem , i << 1 , glyphNameIndex ) ;
      swcopyf(buffer, (uint8 *)"a%04X", glyphNameIndex) ;

      if ( (GlyphNames[i] = cachename(buffer, strlen_uint32((char *)buffer))) == NULL ) {
        (*ttfns->close_frame)(ttfns->data, &ttmem) ;
        return FALSE ;
      }
    }

    (*ttfns->close_frame)(ttfns->data, &ttmem) ;
    break ;

  default:
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid FormatType: %08x",FormatType));

    /* Invalid version numbers in post tables have been observed in a number
     * of fonts. This error can be repaired under some circumstances. If the
     * size of the post table is such that there is no data after the post
     * table, then the table has to be version 1 or version 3. Attempt to
     * fix up by assuming version 3. */
    if (length == 0) {
      Post_data->FormatType = FormatType = 0x00030000 ;
      Post_data->repaired = TRUE ;

      HQTRACE((debug_tt && DEBUG_TT_FAILURES) != 0,
              ("repair post table version"));
    }
    else
      return error_handler( INVALIDFONT ) ;
  }

  Post_data->loaded = TRUE ;

  return TRUE ;
}

static Bool tt_fakepostTable(TT_DATA *TT_data)
{
  TT_POST *Post_data ;
  TT_CMAP_ENTRY *cmap ;

  static PEL_PRIORITY cmap_fakepost[] = {
    { TT_PID_MICROSOFT, TT_EID_WIN_UGL, TT_LID_UNDEFINED },
    { TT_PID_MICROSOFT, TT_EID_WIN_SYMBOL, TT_LID_UNDEFINED },
    { TT_PID_APPLE_UNICODE, TT_EID_UNDEFINED, TT_LID_UNDEFINED },
    { TT_PID_ISO, TT_EID_ISO_10646, TT_LID_UNDEFINED },
    { TT_PID_MACINTOSH, TT_EID_ROMAN, TT_LID_UNDEFINED }
  } ;

  HQASSERT(TT_data != NULL, "No TrueType tables") ;

  Post_data = & TT_data->tt_post ;
  HQASSERT(Post_data->tried, "loading post table has not been attempted") ;
  HQASSERT(!Post_data->loaded, "post table has been loaded, but faking another") ;

  Post_data->FormatType = 0x00030000 ;

  Post_data->italicAngle = 0 ;
  Post_data->underlinePosition = 0 ;
  Post_data->underlineThickness = 0 ;
  Post_data->isFixedPitch = FALSE ;

  Post_data->minMemType42 = 0 ;
  Post_data->maxMemType42 = 0 ;

  Post_data->numPostGlyphs = 0 ;
  Post_data->GlyphNames = NULL ;

  HQASSERT(TT_data->tt_os2.tried, "OS/2 table not loaded") ;
  HQASSERT(TT_data->tt_cmap.tried, "cmap table not loaded") ;

  /* For non symbol fonts, fake a 'post' table by looking up the GID
     mappings for Mac or Unicode cmaps and correlating these with their
     names. The standard names don't make sense for Symbol fonts. */
  if ( (TT_data->tt_os2.ulCodePageRange1 & TT_OS2_SYMBOL) == 0 &&
       (cmap = tt_findCmap(TT_data, cmap_fakepost,
                           NUM_ARRAY_ITEMS(cmap_fakepost))) != NULL ) {
    NAMECACHE *glyphName, **GlyphNames ;
    uint16 numGlyphs, gid ;
    uint32 code ;

    HQASSERT(TT_data->tt_maxp.loaded, "maxp table not loaded") ;

    numGlyphs = TT_data->tt_maxp.numGlyphs ;
    GlyphNames = mm_alloc(mm_pool_temp,
                          numGlyphs * sizeof(NAMECACHE *),
                          MM_ALLOC_CLASS_TTFONT) ;
    if ( ! GlyphNames )
      return error_handler(VMERROR) ;

    Post_data->numPostGlyphs = numGlyphs ;
    Post_data->GlyphNames = GlyphNames ;

    for ( gid = 0 ; gid < numGlyphs ; ++gid )
      GlyphNames[gid] = NULL ;

    if ( cmap->PlatformID == TT_PID_MACINTOSH &&
         cmap->EncodeID == TT_EID_ROMAN ) { /* MacRomanEncoding lookup. */
      for ( code = 0 ; code < 256 ; ++code ) {
        if ( tt_cmap_lookup(cmap, code, &gid, tt_cmap_lookup_failed) &&
             gid < numGlyphs &&
             GlyphNames[gid] == NULL ) {
          switch ( code ) {
          case 0xca: /* This is really a non-breaking space. */
            if ( (glyphName = cachename(STRING_AND_LENGTH("nbspace"))) == NULL )
              return FALSE ;
            break ;
          default:
            glyphName = MacRomanEncoding[code] ;
            HQASSERT(glyphName, "Missing name entry in MacRomanEncoding") ;
            break ;
          }

          if ( glyphName != &system_names[NAME_notdef] )
            GlyphNames[gid] = glyphName ;
        }
      }
    } else { /* Unicode lookup through AGL. */
      uint32 i ;

      for ( i = 0 ; i < cmap->nSegments ; ++i ) {
        TT_CMAP_SEGMENT *segment = cmap->segments[i] ;
        uint32 ncodes = segment->endCode - segment->startCode + 1 ;

        for ( code = segment->endCode ; ncodes > 0 ; --code ) {
          gid = segment->glyphIndex[--ncodes] ;

          if ( code < TT_NO_GLYPH && /* agl_getname is 16bit only */
               gid < numGlyphs &&
               GlyphNames[gid] == NULL ) {
            if ( (glyphName = agl_getname((uint16)code)) == NULL )
              return FAILURE(FALSE) ;

            GlyphNames[gid] = glyphName ;
          }
        }

        SwOftenUnsafe() ;
      }
    }
  }

  Post_data->loaded = TRUE ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Read in the entirety of the hmtx or vmtx table.
 */

/* This is written as though loading hmtx, but is used for vmtx too */
static Bool tt_readXmtxTable(charstring_methods_t *ttfns, TT_DATA *TT_data,
                             TT_HMTX *hmtx, uint32 tag, uint16 numberOfLongMetrics)
{
  TT_TABLE *table ;
  int16*   sideBearing = NULL ;
  uint16   numGlyphs ;
  uint16*  advance = NULL ;
  uint8*   ttmem ;
  uint32   size ;
  int      i,j ;

  if ( hmtx->tried )
    return TRUE ;

  hmtx->tried = TRUE ;
  hmtx->advance = NULL ;
  hmtx->sideBearing = NULL ;
  hmtx->numberOfLongMetrics = 0 ;

  /* dependency - need to know numGlyphs */
  if ( !TT_data->tt_maxp.loaded && !tt_readmaxpTable(ttfns, TT_data) )
    return FALSE ;
  numGlyphs = TT_data->tt_maxp.numGlyphs ;

  if ( numberOfLongMetrics > numGlyphs ) {
    HQFAIL( "Too many long metrics" ) ;
    numberOfLongMetrics = numGlyphs ;
  }
  hmtx->numGlyphs = numGlyphs ;                      /* length of sideBearing array */
  hmtx->numberOfLongMetrics = numberOfLongMetrics ;  /* length of advance array */

  /* Find the table */
  if ( (table = tt_findTable(TT_data, tag)) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  /* Load it.
   * Table contains numberOfLongMetrics*{uint16,int16}
   * followed by numGlyphs-numberOfLongMetrics*int16, so... */
  size = 2 * (numberOfLongMetrics + numGlyphs) ;

  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, table->offset, size)) == NULL )
    return error_handler( INVALIDFONT ) ;

  /* Allocate advance and sideBearing arrays */
  if ( (advance = mm_alloc(mm_pool_temp, numberOfLongMetrics*sizeof(uint16), MM_ALLOC_CLASS_TTFONT)) == NULL ||
       (sideBearing = mm_alloc(mm_pool_temp, numGlyphs*sizeof(int16), MM_ALLOC_CLASS_TTFONT)) == NULL ) {
    (*ttfns->close_frame)(ttfns->data, &ttmem) ;
    if (advance)
      mm_free(mm_pool_temp, (mm_addr_t)advance, numberOfLongMetrics*sizeof(uint16)) ;
    return error_handler(VMERROR) ;
  }

  /* Read in long metrics */
  for ( i=0, j=0 ; i<numberOfLongMetrics ; i++, j+=4 ) {
    TT_ushort( ttmem, j, advance[i] ) ;
    TT_short( ttmem, j+2, sideBearing[i] ) ;
  }
  /* Read the remaining sidebearings */
  for ( i=numberOfLongMetrics ; i<numGlyphs ; i++, j+=2 )
    TT_short( ttmem, j, sideBearing[i] ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  hmtx->sideBearing = sideBearing ;
  hmtx->advance = advance ;
  hmtx->loaded = TRUE ;

  return TRUE ;
}

static Bool tt_readhmtxTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  /* dependency - need to know numberOfHMetrics */
  if ( !TT_data->tt_hhea.loaded && !tt_readhheaTable(ttfns, TT_data) )
    return FALSE ;

  return tt_readXmtxTable( ttfns, TT_data, & TT_data->tt_hmtx,
                           tt_maptag("hmtx"), TT_data->tt_hhea.numberOfLongMetrics ) ;
}

static Bool tt_readvmtxTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  /* dependency - need to know numOfLongVerMetrics */
  if ( !TT_data->tt_vhea.loaded && !tt_readvheaTable(ttfns, TT_data) )
    return FALSE ;

  return tt_readXmtxTable( ttfns, TT_data, & TT_data->tt_vmtx,
                           tt_maptag("vmtx"), TT_data->tt_vhea.numberOfLongMetrics ) ;
}


/* Free the hmtx or vmtx arrays. */

static void tt_free_Xmtx(TT_HMTX* hmtx)
{
  if ( hmtx->loaded ) {
    if ( hmtx->advance ) {
      mm_free(mm_pool_temp, (mm_addr_t)hmtx->advance, hmtx->numberOfLongMetrics*sizeof(uint16)) ;
      hmtx->advance = NULL ;
    }
    if ( hmtx->sideBearing ) {
      mm_free(mm_pool_temp, (mm_addr_t)hmtx->sideBearing, hmtx->numGlyphs*sizeof(int16)) ;
      hmtx->sideBearing = NULL ;
    }
  }
}


/* Lookup a glyph index in the hmtx or vmtx arrays, to get an advance and sidebearing. */

static Bool tt_Xmtx_lookup(TT_HMTX *hmtx, uint16 glyphIndex, uint16 *advance, int16 *sideBearing)
{
  if ( !hmtx->loaded )
    return FALSE ;

  HQASSERT( hmtx->advance, "Advance table missing") ;
  HQASSERT( hmtx->sideBearing, "SideBearing table missing") ;

  if ( glyphIndex >= hmtx->numGlyphs || !hmtx->advance || !hmtx->sideBearing )
    return FAILURE(FALSE) ;

  *advance = hmtx->advance[ ( glyphIndex < hmtx->numberOfLongMetrics ) ?
                            glyphIndex : hmtx->numberOfLongMetrics-1 ] ;
  *sideBearing = hmtx->sideBearing[glyphIndex] ;

  return TRUE ;
}

static Bool tt_hmtx_lookup(TT_DATA *TT_data, uint16 glyphIndex, uint16 *advance, int16 *sideBearing)
{
  return tt_Xmtx_lookup(&TT_data->tt_hmtx, glyphIndex, advance, sideBearing) ;
}

static Bool tt_vmtx_lookup(TT_DATA *TT_data, uint16 glyphIndex, uint16 *advance, int16 *sideBearing)
{
  return tt_Xmtx_lookup(&TT_data->tt_vmtx, glyphIndex, advance, sideBearing) ;
}

/* -------------------------------------------------------------------------- */
/* Read in the VORG table. This contains a sparse array of indices and origins.
 * If numVertOriginYMetrics is greater than half of numGlyphs, or numGlyphs is
 * smaller than 1001, then we build a full array. Otherwise it remains sparse.
 *
 * Note that VORG is only loaded if it is a CFF flavour OpenType - otherwise it
 * is ignored.
 */
static Bool tt_readvorgTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  TT_VORG       *vorg_data = & TT_data->tt_vorg ;
  uint16        numVertOriginYMetrics ;
  int32         tableVersionNumber ;
  int16         defaultVertOriginY ;
  TT_TABLE      *vorg_table ;
  uint16        minIndex = TT_NO_GLYPH ;
  uint16        maxIndex = 0 ;
  int16         numGlyphs ;
  TT_VORG_ENTRY *array = NULL ;
  int16         *full = NULL ;
  uint8         sparse ;
  uint8         *ttmem ;
  int32         prev ;
  size_t        size ;
  int           i,j ;
  int           length ;

  if ( vorg_data->tried )
    return TRUE ;
  vorg_data->tried = TRUE ;
  vorg_data->array = NULL ;
  vorg_data->defaultVertOriginY = 0;
  vorg_data->minIndex = TT_NO_GLYPH ;
  vorg_data->numVertOriginYMetrics = 0 ;

  /* find the optional VORG table, but only if it's a cff */
  if ( tt_findTable(TT_data, tt_maptag("CFF ")) == NULL ||
       (vorg_table = tt_findTable(TT_data, tt_maptag("VORG"))) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  /* dependency - needs numGlyphs */
  if ( !TT_data->tt_maxp.loaded && !tt_readmaxpTable(ttfns, TT_data) )
    return FALSE;
  numGlyphs = TT_data->tt_maxp.numGlyphs ;

  /* read in everything but the array */
  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, vorg_table->offset, TT_SIZE_VORG)) == NULL )
    return error_handler( INVALIDFONT ) ;

  TT_fixed ( ttmem , 0 , tableVersionNumber ) ;
  TT_short(  ttmem , 4 , defaultVertOriginY ) ;
  TT_ushort( ttmem , 6 , numVertOriginYMetrics ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  /* panic if we don't recognise the content */
  if ( tableVersionNumber != 0x00010000 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid VORG version: %d",tableVersionNumber));
    return error_handler( INVALIDFONT ) ;
  }

  if ( numVertOriginYMetrics == 0 ) {
    /* no array at all */
    sparse = 0 ;
    array = NULL ;
  } else {
    /* sanity check */
    if ( numVertOriginYMetrics > numGlyphs ) {
      HQFAIL( "Too many VORG metrics" ) ;
      numVertOriginYMetrics = numGlyphs ;
    }

    /* read in the sparse array */
    if ( (ttmem = (*ttfns->open_frame)(ttfns->data, vorg_table->offset+TT_SIZE_VORG,
                                       numVertOriginYMetrics*4)) == NULL )
      return error_handler( INVALIDFONT ) ;

    /* find the range of indices. We silently ignore indices >= numGlyphs */
    for ( j=0 ; j<4*numVertOriginYMetrics ; j+=4 ) {
      uint16 glyphIndex ;

      TT_ushort( ttmem, j, glyphIndex ) ;

      if ( glyphIndex < numGlyphs ) {
        if ( glyphIndex < minIndex )
          minIndex = glyphIndex ;
        if ( glyphIndex > maxIndex )
          maxIndex = glyphIndex ;
      }
    }

    if ( minIndex > maxIndex ) {
      /* no valid entries! */
      (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      return error_handler(INVALIDFONT) ;
    }

    /* decide which kind of array to build */
    length = maxIndex - minIndex + 1 ;
    if ( length < 1001 || numVertOriginYMetrics > length/2 ) {
      /* a full subset is smaller, or small enough, and is far more efficient */
      sparse = 0 ;
      size = length * sizeof(int16) ;
    } else {
      /* keep it as a sparse array */
      sparse = 1 ;
      length = numVertOriginYMetrics ;
      size = length * sizeof(TT_VORG_ENTRY) ;
    }

    /* allocate the array, and initialise it if not sparse */
    if ( (array = mm_alloc(mm_pool_temp, size, MM_ALLOC_CLASS_TTFONT)) == NULL ) {
      (*ttfns->close_frame)(ttfns->data, &ttmem) ;
      return error_handler(VMERROR) ;
    }
    if ( !sparse ) {
      full = (int16*) array ;
      for ( i=0; i<length; i++ )
        full[i] = defaultVertOriginY ;
    }

    /* populate the array, monitoring monotonicity for the sparse case */
    prev = -1 ;
    for ( i=0,j=0 ; i<numVertOriginYMetrics ; i++,j+=4 ) {
      uint16 glyphIndex ;
      int16  vertOriginY ;

      TT_ushort( ttmem, j, glyphIndex ) ;
      TT_short( ttmem, j+2, vertOriginY ) ;

      if ( glyphIndex < numGlyphs ) {

        /* is the sparse array disordered? */
        prev = ( glyphIndex > prev ) ? glyphIndex : TT_BAD_GLYPH ;

        if ( sparse ) {
          /* it is safe to populate the sparse array, even if disordered. See later */
          array[i].glyphIndex = glyphIndex ;
          array[i].vertOriginY = vertOriginY ;
        } else
          full[glyphIndex - minIndex] = vertOriginY ;
      }
    }

    (*ttfns->close_frame)(ttfns->data, &ttmem) ;

    if ( prev == TT_BAD_GLYPH ) {
      /* The array was not sorted, or has duplicate entries. If !sparse, we have already coped.
       * However, if sparse this will break the binary search on the sparse array.
       * We /could/ sort here, discard duplicates and adjust numVertOriginYMetrics, but we'd
       * have to remember 'size' for freeing, and until we see such a font in the wild... */
      mm_free(mm_pool_temp, (mm_addr_t)array, size) ;
      return error_handler( INVALIDFONT ) ;
    }

  }
  /* everything seems OK */
  vorg_data->numVertOriginYMetrics = numVertOriginYMetrics ;
  vorg_data->defaultVertOriginY = defaultVertOriginY ;
  vorg_data->minIndex = minIndex ;
  vorg_data->maxIndex = maxIndex ;
  vorg_data->sparse = sparse;
  vorg_data->array = array ;        /* either type of array */
  vorg_data->loaded = TRUE ;

  return TRUE ;
}


/* Free the vorg data. */

static void tt_free_vorg(TT_VORG* vorg)
{
  if ( vorg->loaded && vorg->array ) {
    size_t size = (vorg->sparse) ?
                  vorg->numVertOriginYMetrics * sizeof(TT_VORG_ENTRY) :
                  (vorg->maxIndex - vorg->minIndex + 1) * sizeof(int16) ;
    mm_free(mm_pool_temp, (mm_addr_t)vorg->array, size) ;
    vorg->array = NULL ;
  }
}


/* Lookup a glyph index in the VORG array or return the default origin. */

static Bool tt_vorg_lookup(TT_DATA *TT_data, uint16 glyphIndex, int16 *vertOriginY)
{
  TT_VORG *vorg = & TT_data->tt_vorg ;

  HQASSERT( vorg->tried, "VORG table not loaded!" ) ;
  if ( !vorg->loaded )
    return FALSE ;

  /* do we check the array? */
  if ( vorg->array &&
       glyphIndex >= vorg->minIndex &&
       glyphIndex <= vorg->maxIndex ) {

    if ( !vorg->sparse ) {
      /* subsetted contiguous array */
      int16 *array = (int16*) vorg->array ;

      *vertOriginY = array[glyphIndex - vorg->minIndex] ;
      return TRUE ;

    } else {
      /* full binary search required */

      TT_VORG_ENTRY *array = (TT_VORG_ENTRY*) vorg->array ;
      int b = 0 ; /* inclusive */
      int t = vorg->maxIndex - vorg->minIndex + 1 ; /* exclusive */

      while ( b < t ) {
       int   i = (b+t) / 2 ;
       int32 diff = array[i].glyphIndex - glyphIndex ;

       if ( diff == 0 ) {
         *vertOriginY = array[i].vertOriginY ;
         return TRUE ;
       }
       if ( diff > 0 )
         t = i ;
       else
         b = i + 1 ;
      }
    }
  }

  *vertOriginY = vorg->defaultVertOriginY ;
  return TRUE ;
}
/* -------------------------------------------------------------------------- */

/* This is written as though loading hhea, but is used to load vhea too */
static Bool tt_readXheaTable(charstring_methods_t *ttfns, TT_DATA *TT_data,
                             TT_HHEA *Hhea_data, uint32 hhea_tag)
{
  /* (7) Read in and cache all the 'hhea' header, checking it is valid.
   * Type       Name                    Description
   * Fixed      Table version number    0x00010000 for version 1.0.
   * FWord      Ascender                Typographic ascent.
   * FWord      Descender               Typographic descent.
   * ...
   * USHORT     numberOfHMetrics        Number of long metrics in hmtx
   */

  int32 Tableversionnumber ;
  int16 Ascender ;
  int16 Descender ;
  uint16 numberOfLongMetrics ;
  uint8 *ttmem ;

  TT_TABLE *Tab_hhea;

  if ( Hhea_data->tried )
    return TRUE ;

  Hhea_data->tried = TRUE ;
  if ( (Tab_hhea = tt_findTable(TT_data, hhea_tag)) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, Tab_hhea->offset,
                                    TT_SIZE_HHEA)) == NULL )
    return error_handler( INVALIDFONT ) ;

  TT_fixed ( ttmem , 0 , Tableversionnumber ) ;
  TT_ushort( ttmem , 4 , Ascender ) ;
  TT_ushort( ttmem , 6 , Descender ) ;
  TT_ushort( ttmem ,34 , numberOfLongMetrics ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  if ( Tableversionnumber != 0x00010000 ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("invalid Tableversionnumber: %d",Tableversionnumber));
    return error_handler( INVALIDFONT ) ;
  }

  Hhea_data->Ascender = Ascender ;
  Hhea_data->Descender = Descender ;
  Hhea_data->numberOfLongMetrics = numberOfLongMetrics ;
  Hhea_data->loaded = TRUE ;

  return TRUE ;
}

static Bool tt_readhheaTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  return tt_readXheaTable(ttfns, TT_data, & TT_data->tt_hhea, tt_maptag("hhea") ) ;
}

static Bool tt_readvheaTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  return tt_readXheaTable(ttfns, TT_data, & TT_data->tt_vhea, tt_maptag("vhea") ) ;
}

/* -------------------------------------------------------------------------- */
static Bool tt_unicodeisascii( uint8 *str , int32 len )
{
  uint16 ch ;

  if (( len & 1 ) != 0 )
    return FAILURE(FALSE) ;

  while ((len -= 2) >= 0 ) {
    ch = (*str++) ;
    ch <<= 8 ;
    ch |= (*str++) ;
    if ( ch < 0x20 || ch > 0x7e )
      return FALSE ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static PEL_PRIORITY name_priorities[] = {
  { TT_PID_ISO ,           TT_EID_ASCII ,      TT_LID_UNDEFINED } ,
  { TT_PID_MACINTOSH ,     TT_EID_ROMAN ,      TT_LID_MAC_ENGLISH } ,
  { TT_PID_MICROSOFT ,     TT_EID_WIN_UGL ,    TT_LID_WIN_ENG_USA } ,
  { TT_PID_MICROSOFT ,     TT_EID_WIN_SYMBOL , TT_LID_WIN_ENG_USA } ,
  { TT_PID_APPLE_UNICODE , TT_EID_UNDEFINED ,  TT_LID_UNDEFINED } ,
  { TT_PID_ISO ,           TT_EID_ISO_8859_1 , TT_LID_UNDEFINED } ,
  { TT_PID_MACINTOSH ,     TT_EID_ROMAN ,      TT_LID_UNDEFINED } ,
  { TT_PID_MACINTOSH ,     TT_EID_UNDEFINED ,  TT_LID_UNDEFINED } ,
  { TT_PID_UNDEFINED ,     TT_EID_UNDEFINED ,  TT_LID_UNDEFINED } ,
} ;

/** Predicate to tell if name is in UCS2/UTF-16. All Microsoft names are
   UCS-2, regardless of encoding. */
#define NAME_IS_UNICODE(pid_, eid_) \
  ((pid_) == TT_PID_MICROSOFT || \
   (pid_) == TT_PID_APPLE_UNICODE || \
   ((pid_) == TT_PID_ISO && (eid_) == TT_EID_ISO_10646))

static Bool tt_readname(charstring_methods_t *ttfns,
                        int32 NumberofNameRecords ,
                        int32 requestNameID ,
                        uint32 nOffset, uint32 sOffset, uint32 nLength,
                        PEL_PRIORITY priorities[], uint32 npriorities,
                        TT_NF *name )
{
  /* (8.1) Read in and cache a 'name' header, checking it is valid.
   *  Type      Description
   *  USHORT    Platform ID.
   *  USHORT    Platform-specific encoding ID.
   *  USHORT    Language ID.
   *  USHORT    Name ID.
   *  USHORT    String length (in bytes).
   *  USHORT    String offset from start of storage area (in bytes).
   */
  name->len = 0 ;
  name->str = NULL ;

  while ((--NumberofNameRecords) >= 0 ) {
    uint8 *ttmem ;
    uint16 PlatformID ;
    uint16 EncodingID ;
    uint16 LanguageID ;
    uint16 NameID ;
    uint16 Stringlength ;
    uint16 Stringoffset ;

    if ( (ttmem = (*ttfns->open_frame)(ttfns->data,
                                      nOffset + TT_SIZE_NAME_RECORD * NumberofNameRecords,
                                      TT_SIZE_NAME_RECORD)) == NULL )
      return error_handler( INVALIDFONT ) ;

    TT_ushort( ttmem ,  0 , PlatformID ) ;
    TT_ushort( ttmem ,  2 , EncodingID ) ;
    TT_ushort( ttmem ,  4 , LanguageID ) ;
    TT_ushort( ttmem ,  6 , NameID ) ;
    TT_ushort( ttmem ,  8 , Stringlength ) ;
    TT_ushort( ttmem , 10 , Stringoffset ) ;

    (*ttfns->close_frame)(ttfns->data, &ttmem) ;

    if ( NameID == requestNameID && Stringlength > 0 &&
         ((uint32)Stringoffset + Stringlength) <= nLength ) {
      uint32 i ;

      for ( i = 0 ; i < npriorities ; ++i ) {

        if ( (priorities[ i ].pid == TT_PID_UNDEFINED ||
              priorities[ i ].pid == PlatformID) &&
             (priorities[ i ].eid == TT_EID_UNDEFINED ||
              priorities[ i ].eid == EncodingID) &&
             (priorities[ i ].lid == TT_LID_UNDEFINED ||
              priorities[ i ].lid == LanguageID) ) {

          if ( (ttmem = (*ttfns->open_frame)(ttfns->data,
                                             Stringoffset + sOffset,
                                             Stringlength)) == NULL )
            return error_handler( INVALIDFONT ) ;
          if ( !NAME_IS_UNICODE(PlatformID, EncodingID) ||
               tt_unicodeisascii( ttmem , Stringlength )) {
            if ( name->str && name->len != Stringlength ) {
              mm_free(mm_pool_temp, (mm_addr_t)name->str, name->len) ;
              name->str = NULL ;
              name->len = 0 ;
            }

            if ( !name->str &&
                 (name->str = mm_alloc(mm_pool_temp, Stringlength, MM_ALLOC_CLASS_TTFONT)) == NULL ) {
              (*ttfns->close_frame)(ttfns->data, &ttmem) ;
              return error_handler(VMERROR) ;
            }

            HqMemCpy(name->str, ttmem, Stringlength) ;
            name->len = Stringlength ;
            name->pid = PlatformID ;
            name->eid = EncodingID ;
            name->lid = LanguageID ;
          }
          (*ttfns->close_frame)(ttfns->data, &ttmem) ;

          if ( i == 0 ) /* It can't get any better than this. */
            return TRUE ;

          npriorities = i ; /* This is now the priority limit */
          break ;
        }
      }
    }
  }

  return TRUE ;
}

static Bool tt_readnameTable(charstring_methods_t *ttfns, TT_DATA *TT_data)
{
  /* (8) Read in and cache all the 'name' header, checking it is valid.
   *  Type              Description
   *  USHORT            Format selector (=0).
   *  USHORT            Number of NameRecords that follow n.
   *  USHORT            Offset to start of string storage (from start of table).
   *  NameRecords[n]    The NameRecords.
   *  (Variable)        Storage for the actual string data.
   */

  uint16 Formatselector = 0 ;
  uint16 NumberofNameRecords = 0 ;
  uint16 Offset = 0 ;
  uint32 base = 0 ;

  int32 i ;
  uint8 *ttmem ;

  TT_NAME *Name_data = ( & TT_data->tt_name ) ;
  TT_TABLE *Tab_name;

  if ( Name_data->tried )
    return TRUE ;

  Name_data->tried = TRUE ;
  if ( (Tab_name = tt_findTable(TT_data, tt_maptag("name"))) == NULL )
    return TRUE ; /* Not an error; caller must test if loaded. */

  base = Tab_name->offset ;

  if ( (ttmem = (*ttfns->open_frame)(ttfns->data, base, TT_SIZE_NAME)) == NULL )
    return error_handler( INVALIDFONT ) ;

  TT_ushort( ttmem , 0 , Formatselector ) ;
  TT_ushort( ttmem , 2 , NumberofNameRecords ) ;
  TT_ushort( ttmem , 4 , Offset ) ;

  (*ttfns->close_frame)(ttfns->data, &ttmem) ;

  /* We have seen 'true' format TT fonts with Formatselector of 65535
     (Request 26209). */
  if ( Formatselector != 0 &&
       (Formatselector != 0xffff || TT_data->version != tt_maptag("true")) ) {
    HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,
            ("invalid name table Formatselector: %d",Formatselector));
    return error_handler( INVALIDFONT ) ;
  }

  for ( i = 0 ; i < TT_NAME_MAX ; ++i ) {
    if ( ! tt_readname(ttfns, NumberofNameRecords,
                       tt_name_index[i],
                       base + TT_SIZE_NAME, base + Offset, Tab_name->length - Offset,
                       name_priorities, NUM_ARRAY_ITEMS(name_priorities),
                       &Name_data->names[i]) ) {
      HQTRACE((debug_tt & DEBUG_TT_FAILURES) != 0,("missing name: %d",i));
      return error_handler( INVALIDFONT ) ;
    }
  }

  Name_data->loaded = TRUE ;

  return TRUE ;
}

static Bool tt_nameIsUnicode(TT_NF *tt_name)
{
  return (tt_name->str != NULL &&
          NAME_IS_UNICODE(tt_name->pid, tt_name->eid)) ;
}

/* -------------------------------------------------------------------------- */
#if defined(TT_CORRECT_CHECKSUMS) || defined(TT_CHECK_CHECKSUMS) || defined(TT_TESTING)
static uint32 tt_schecksum(charstring_methods_t *ttfns,
                           uint32 offset, uint32 ttlen)
{
  uint32 checksum ;
  uint32 nextword ;
  uint8  buf[ 4 ] ;
  uint8 *frame ;

  checksum = 0 ;

  if ( (frame = (*ttfns->open_frame)(ttfns->data, offset, ttlen)) != NULL ) {
    uint8 *ttmem = frame ;

    while ( ttlen >= 4 ) {
      TT_ulong( ttmem , 0 , nextword ) ;
      checksum += nextword ;
      ttmem += 4 ;
      ttlen -= 4 ;
    }

    /* Capture last word correctly. */
    if ( ttlen > 0 ) {
      int32 i ;
      for ( i = 0 ; i < ttlen ; ++i )
        buf[ i ] = ttmem[ i ] ;
      for ( i = ttlen ; i < 4 ; ++i )
        buf[ i ] = ( uint8 )0 ;
      TT_ulong( buf , 0 , nextword ) ;
      checksum += nextword ;
    }
    (*ttfns->close_frame)(ttfns->data, &frame) ;
  }

  return checksum ;
}
#endif

/*--------------------------------------------------------------------------*/
/* Failure routine to call when not debugging or when we definitely want to
   see the cmap lookup failures. */
static Bool tt_cmap_lookup_failed(void)
{
  return FAILURE(FALSE) ;
}

/** Look up a charcode in a specific cmap entry. Returns TRUE and sets
   glyphIndex if found, returns FALSE if not found. */
static Bool tt_cmap_lookup(TT_CMAP_ENTRY *cmap, uint32 charCode,
                           uint16 *glyphIndex, Bool (*failed)(void))
{
  uint32 i ;

  HQASSERT(cmap->nSegments > 0, "No segments in TrueType cmap lookup") ;

  for ( i = 0 ; i < cmap->nSegments ; ++i ) {
    TT_CMAP_SEGMENT *segment = cmap->segments[i] ;

    if ( charCode < segment->startCode ) /* Segments are ordered */
      return (*failed)() ;

    if ( charCode > segment->endCode ) /* Not in this segment? */
      continue ;

    /* Found segment containing charcode */
    /* [62037] Optimisation for unsparse arrays:
     * There's no point unpacking a one-to-one mapping into an array when an
     * offset is all that's required. In this case, useOffset is TRUE, and
     * glyphIndex[0] is the offset (mod 1<<16).
     */
    if ( segment->useOffset ) {
      *glyphIndex = (uint16)(charCode + segment->glyphIndex[0]) ;
      if ( *glyphIndex == 0xFFFF )
        *glyphIndex = 0 ;
      return TRUE ;
    }

    /* [12422] Deferred reporting of messed-up cmap:
     * There is a subtle difference in behaviour here. If the application has
     * used a code point that isn't in the cmap, we return FALSE here, and the caller
     * will most likely use notdef. However, if the code IS in the cmap, but the cmap
     * was incomplete in some way, we generate INVALIDFONT. In this way, we only fault
     * corrupt cmaps if they would have affected output in an unexpected way. However,
     * there is still the argument that we should just return FALSE and therefore use
     * notdef regardless. We shall see.
     */
    if ( (*glyphIndex = segment->glyphIndex[charCode - segment->startCode]) == 0xFFFF )
      return error_handler(INVALIDFONT) ;
    return TRUE ;
  }

  return (*failed)() ;
}

/** Find a cmap, given a list of priorities to search for. If no matching cmap
   is found, return NULL. */
static TT_CMAP_ENTRY *tt_findCmap(TT_DATA *TT_data,
                                  PEL_PRIORITY *priorities,
                                  uint32 npriorities)
{
  TT_CMAP_ENTRY *cmap, *best = NULL ;
  Bool report_font_repairs =
    get_core_context_interp()->fontsparams->ReportFontRepairs;

  HQASSERT(TT_data, "No TrueType data tables") ;
  HQASSERT(TT_data->tt_cmap.tried, "cmap tables not loaded") ;

  for ( cmap = TT_data->tt_cmap.cmaps ; cmap != NULL ; cmap = cmap->next ) {
    uint32 i ;

    for ( i = 0 ; i < npriorities ; ++i ) {
      int32 PlatformID = priorities[i].pid ;
      int32 EncodeID = priorities[i].eid ;
      int32 LanguageID = priorities[i].lid ;

      if ( (PlatformID == TT_PID_UNDEFINED || PlatformID == cmap->PlatformID) &&
           (EncodeID == TT_EID_UNDEFINED || EncodeID == cmap->EncodeID) &&
           (LanguageID == TT_LID_UNDEFINED || LanguageID == cmap->LanguageID) ) {
        if ( i == 0 ) { /* Highest priority match, can't get better than this */
          if ( cmap->repaired && report_font_repairs )
            monitorf((uint8*)"Repaired cmap table referenced.\n");
          return cmap ;
        }

        npriorities = i ;
        best = cmap ;
        break ;
      }
    }
  }

  if ( best && best->repaired && report_font_repairs )
    monitorf((uint8*)"Repaired cmap table referenced.\n");

  return best ;
}

/*---------------------------------------------------------------------------*/
static void tt_stripunicode( uint8 *dst_str , int32 *dst_len ,
                             uint8 *src_str , int32  src_len )
{
  int32 len = 0 ;

  HQASSERT( src_len >= 0 , "can't strip -ve sized strings" ) ;
  HQASSERT( src_len >  0 , "why strip empty strings?" ) ;

  while ((src_len -= 2) >= 0 ) {
    dst_str[ 0 ] = src_str[ 1 ] ;
    dst_str += 1 ;
    src_str += 2 ;
    ++len ;
  }

  (*dst_len) = len ;
}

static void tt_stripspaces( uint8 *dst_str , int32 *dst_len ,
                            uint8 *src_str , int32  src_len )
{
  uint8 ch ;
  int32 len = 0 ;

  HQASSERT( src_len >= 0 , "can't strip -ve sized strings" ) ;
  HQASSERT( src_len >  0 , "why strip empty strings?" ) ;

  while ((--src_len) >= 0 ) {
    ch = (*src_str++) ;
    if ( ch != ' ' ) {
      (*dst_str++) = ch ;
      ++len ;
      }
    }
  (*dst_len) = len ;
}

/*---------------------------------------------------------------------------*/
/** If we don't have a font name, read the names table to find one. If there
   isn't a PostScript name, synthesise one out of the family name and the
   weight. */
static Bool tt_match_fontname(TT_DATA *tt_data,
                              OBJECT *forcename, OBJECT *foundname,
                              int32 fontindex)
{
  uint8 tempname[MAXPSSTRING] ; /* Temporary workspace for stripped names */
  uint8 *fontnamestr ;
  int32 fontnamelen ;
  TT_NAME *name_data = &tt_data->tt_name ;
  TT_NF *tt_names = name_data->names ;
  OBJECT fnames = OBJECT_NOTVM_NOTHING ;

  HQASSERT(name_data->tried, "name table not loaded") ;
  HQASSERT(foundname, "No input and output fontname") ;

  /* Product FontName; if TT_NAME_PSNAME exists then use this, otherwise
   * synthesise the name from both Font Family Name & Font Subfamily name.
   */
  if ( tt_names[TT_NAME_PSNAME].str != NULL ) {
    TT_NF *tt_name = &tt_names[TT_NAME_PSNAME] ;
    fontnamestr = tt_name->str ;
    fontnamelen = tt_name->len ;
    if ( tt_nameIsUnicode(tt_name) ) {
      tt_stripunicode(tempname, &fontnamelen, fontnamestr, fontnamelen) ;
      fontnamestr = tempname ;
    }
  } else if ( tt_names[TT_NAME_FAMILYNAME].str != NULL ) {
    /* No PostScript name, so synthesise from family, weight */
    TT_NF *tt_name = &tt_names[TT_NAME_FAMILYNAME] ;

    fontnamestr = tt_name->str ;
    fontnamelen = tt_name->len ;
    if ( tt_nameIsUnicode(tt_name) ) {
      tt_stripunicode(tempname, &fontnamelen, fontnamestr, fontnamelen) ;
      fontnamestr = tempname ;
    }

    tt_name = & name_data->names[ TT_NAME_WEIGHT ] ;
    if ( tt_name->str != NULL && tt_name->len > 0 ) {
      int32 fontweightlen ;
      uint8 *tempweight = tempname + fontnamelen + 1 ;

      if ( tt_nameIsUnicode(tt_name) ) {
        if ( (tt_name->len >> 1) + fontnamelen + 1 > MAXPSSTRING )
          return error_handler(INVALIDFONT) ;
        tt_stripunicode(tempweight, &fontweightlen,
                        tt_name->str, tt_name->len) ;
      } else {
        if ( tt_name->len + fontnamelen + 1 > MAXPSSTRING )
          return error_handler(INVALIDFONT) ;
        HqMemCpy(tempweight, tt_name->str, tt_name->len) ;
        fontweightlen = tt_name->len ;
      }

      if ( fontweightlen != 0 &&
           HqMemCmp(tempweight, fontweightlen, STRING_AND_LENGTH("Regular")) != 0 ) {
        if ( fontnamestr != tempname ) {
          HqMemCpy(tempname, fontnamestr, fontnamelen) ;
          fontnamestr = tempname ;
        }
        tempname[fontnamelen] = '-' ;
        fontnamelen += fontweightlen + 1 ;
      }
    }

    tt_stripspaces(tempname, &fontnamelen, fontnamestr, fontnamelen) ;
    fontnamestr = tempname ;
  } else if ( forcename ) {
    /* No names table, but we're overriding the name, so use that */
    if ( oType(*forcename) == ONAME ) {
      fontnamestr = theICList(oName(*forcename)) ;
      fontnamelen = theINLen(oName(*forcename)) ;
    } else if ( oType(*forcename) == OSTRING ) {
      fontnamestr = oString(*forcename) ;
      fontnamelen = theLen(*forcename) ;
    } else
      return error_handler(INVALIDFONT) ;
  } else /* We have no way of naming this font */
    return error_handler(INVALIDFONT) ;


  HQASSERT(fontnamelen > 0 && fontnamelen <= MAXPSSTRING,
           "TT name length invalid") ;

  /* Make sure names are represented in an array for consistency */
  Copy(&fnames, foundname) ;
  if ( oType(fnames) != OARRAY && oType(fnames) != OPACKEDARRAY ) {
    theTags(fnames) = OARRAY | LITERAL | READ_ONLY ;
    theLen(fnames) = 1 ;
    oArray(fnames) = foundname ;
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

    /* fnamelen will not match if the font is indexed, so HqMemCmp won't
       be called with a null fnamestr. */
    if ( fnameindex == fontindex ||
         (fnamelen == fontnamelen &&
          HqMemCmp(fontnamestr, fontnamelen, fnamestr, fnamelen) == 0) ) {
      /* Reset the fontname passed in to the name of the font we've
         actually matched on. */
      if ( fontnamelen <= MAXPSNAME ) {
        if ( (oName(nnewobj) = cachename(fontnamestr, fontnamelen)) == NULL )
          return FALSE ;
        Copy(foundname, &nnewobj) ;
      } else {
        if ( !ps_string(foundname, fontnamestr, fontnamelen) )
          return FALSE ;
      }

      return TRUE ;
    }

    /* Prepare for next font name match. */
    --theLen(fnames) ;
    ++oArray(fnames) ;
  }

  /* No match for a valid fontname */
  object_store_null(foundname) ;

  return TRUE ;
}


/*--------------------------------------------------------------------------*/
/** Construct a name for a GID that is not mapped correctly by the 'post'
   table. */
NAMECACHE *tt_gid_name(uint16 gid)
{
  uint8 buffer[9] ; /* Enough for 'gidDDDDD' */

  swcopyf(buffer, (uint8 *)"gid%05d", gid) ;

  return cachename(buffer, strlen_uint32((char *)buffer)) ;
}

/* -------------------------------------------------------------------------- */
/** Return a name for a 3,1 cmap index */

NAMECACHE * tt_fake_glyphname(uint16 gid)
{
  NAMECACHE *name = agl_getname(gid) ;

  if (!name)
    name = tt_gid_name(gid) ;

  return name ;
}

/*--------------------------------------------------------------------------*/
/** Build a default encoding for the font. The PDF 1.3 reference manual,
   *second edition*, documents how Adobe derive encodings for TrueType fonts:

  `A TrueType font program's "cmap" table consists of one or more
   subtables, each identified by the combination of a platform ID and a
   platform-specific encoding ID. If a named encoding (WinAnsiEncoding,
   MacRomanEncoding, or MacExpertEncoding) is specified in a font
   dictionary's Encoding entry or in an encoding dictionary's BaseEncoding
   entry, a "cmap" subtable is selected and used as described below.

   * If a "cmap" subtable with platform ID 3 and encoding ID 1 (Microsoft
     Unicode) is present, it is used as follows: A character code is first
     mapped to a character name as specified by the font's Encoding entry.
     The character name is then mapped to a Unicode value by consulting the
     Adobe Glyph List (see the Bibliography). Finally, the Unicode value is
     mapped to a glyph description according to the (3, 1) subtable.

   * If a "cmap" subtable with platform ID 1 and encoding 0 (Macintosh
     Roman) is present, it is used as follows: A character code is first
     mapped to a character name as specified by the font's Encoding entry.
     The character name is then mapped back to a character code according
     to MacRomanEncoding (see Appendix D). Finally, the code is mapped to a
     glyph description according to the (1, 0) subtable.

   * In either of the cases above, if the character name cannot be mapped
     as specified, the character name is looked up in the font program's
     "post" table (if one is present) and the associated glyph description
     is used.

   If no Encoding entry is specified in the font dictionary, the "cmap"
   subtable with platform ID 1 and encoding 0 will be used to map directly
   from character codes to glyph descriptions, without any consideration of
   character names. This is the normal convention for symbolic fonts.

   If a character cannot be mapped in any of the ways described above, the
   results are implementation-dependent.'

   ScriptWorks performs character lookup through the T42's Encoding and
   CharStrings indices, which means that the internal TT to Type 42 filter
   has to derive the appropriate names for the CharStrings and Encoding
   entries to perform the mapping above. Here is how ScriptWorks performs
   the lookup for PostScript and PDF:

     PostScript                       PDF
    glyphshow  show                  Tj/TJ
       |       |                      |
       |       v                      v
       |     string                 string
       |       | get_selector()       | get_selector()
       |       v                      v
       |    character              character-------+
       |      code                   code          | pdf_getencoding()
       |       | Encoding             | Encoding   |       +
       |       v                      v            | tt_pdf_lookup()
       +----->name                   name<---------+
               | CharStrings          | CharStrings
               v                      v
             glyph                  glyph
             index                  index

   To implement the mapping above, the CharStrings names are derived from the
   "post" table, and by manufacturing names for any glyphs which appear in the
   cmap(1,0) table that are not mapped by a post table entry. tt_pdf_lookup()
   performs the cmap(3,1), cmap(1,0) or post table lookup behaviour if a
   named Encoding is present. The default behaviour is implemented by making
   the implicit font encoding one which maps every code of the cmap(1,0) table
   to a glyph name for the respective glyph index. */

/* Common routine to build an array of NAMECACHE entries. Used by PDFOut
   default encoding and PS encoding creation. */
static Bool tt_encoding_names(NAMECACHE *names[256], OBJECT *charstrings,
                              TT_DATA *tt_data)
{
  TT_CMAP_ENTRY *cmap ;
  uint32 code ;

  static PEL_PRIORITY cmap_encodings[] = {
    { TT_PID_MACINTOSH, TT_EID_ROMAN, TT_LID_UNDEFINED }, /* Mac Roman first */
    { TT_PID_MICROSOFT, TT_EID_WIN_UGL, TT_LID_UNDEFINED }, /*  Then MS UGL */
    { TT_PID_MICROSOFT, TT_EID_UNDEFINED, TT_LID_UNDEFINED }, /* Any MS next */
    { TT_PID_MACINTOSH, TT_EID_UNDEFINED, TT_LID_UNDEFINED }, /* Any Mac */
    { TT_PID_UNDEFINED, TT_EID_UNDEFINED, TT_LID_UNDEFINED }, /* Any old thing */
  } ;

  HQASSERT(names, "Nowhere for encoding names") ;
  HQASSERT(charstrings, "No charstrings") ;
  HQASSERT(tt_data, "No TT data") ;

  /* If we have a suitable cmap, map character codes 0..255 through it. */
  if ( (cmap = tt_findCmap(tt_data, cmap_encodings,
                           NUM_ARRAY_ITEMS(cmap_encodings))) != NULL ) {
    TT_POST *post = &tt_data->tt_post ;
    OBJECT *gido ;
    uint16 gid = 0;

    HQASSERT(post->loaded, "post table not loaded") ;

    for ( code = 0 ; code < 256 ; ++code ) {
      NAMECACHE *glyphName ;

      names[code] = &system_names[NAME_notdef] ; /* Safety default .notdef */

      if ( tt_cmap_lookup(cmap, code, &gid, tt_cmap_lookup_failed) ) {
        if ( gid < post->numPostGlyphs &&
             (glyphName = post->GlyphNames[gid]) != NULL ) {
          /* Check if the post name maps to this glyph. There may be multiple
             GIDs mapped to a single name in the 'post' table.  */
          oName(nnewobj) = glyphName ;
          if ( (gido = fast_extract_hash(charstrings, &nnewobj)) != NULL ) {
            HQASSERT(oType(*gido) == OINTEGER,
                     "TT CharStrings GID entry is not an integer") ;
            /* Default to using the 'post' name, even if we can't find a name
               for this exact GID. */
            names[code] = glyphName ;
            if ( gid != oInteger(*gido) ) {
              /* 'post' name doesn't map to this GID. Check if we created a
                 special name for this GID. */
              if ( (glyphName = tt_gid_name(gid)) == NULL )
                return FALSE ;

              oName(nnewobj) = glyphName ;
              if ( (gido = fast_extract_hash(charstrings, &nnewobj)) != NULL ) {
                HQASSERT(oType(*gido) == OINTEGER,
                         "TT CharStrings GID entry is not an integer") ;
                HQASSERT(oInteger(*gido) == gid,
                         "TT CharStrings special name doesn't map to GID") ;
                names[code] = glyphName ; /* GID has special name, use it */
              }
            }
          }
        } else {
          /* GID does not exist in 'post' table. See if it has a special name,
             if not leave the entry as notdef. */
          if ( (glyphName = tt_gid_name(gid)) == NULL )
            return FALSE ;

          oName(nnewobj) = glyphName ;
          if ( (gido = fast_extract_hash(charstrings, &nnewobj)) != NULL ) {
            HQASSERT(oType(*gido) == OINTEGER,
                     "TT CharStrings GID entry is not an integer") ;
            HQASSERT(oInteger(*gido) == gid,
                     "TT CharStrings special name doesn't map to GID") ;
            names[code] = glyphName ;
          }
        }
      }
    }
  } else {
    for ( code = 0 ; code < 256 ; ++code ) /* Safety default to .notdef */
      names[code] = &system_names[NAME_notdef] ;
  }

  return TRUE ;
}

static Bool tt_ps_encoding(OBJECT *encoding, OBJECT *charstrings,
                           TT_DATA *tt_data)
{
  uint32 code ;
  NAMECACHE *names[256] ;

  HQASSERT(encoding, "Nowhere for encoding array") ;

  if ( !tt_encoding_names(names, charstrings, tt_data) ||
       !ps_array(encoding, 256) ||
       !object_access_reduce(READ_ONLY, encoding) )
    return FALSE ;

  for ( encoding = oArray(*encoding), code = 0 ; code < 256 ; ++code, ++encoding ) {
    HQASSERT(names[code], "Invalid encoding name") ;
    theTags(*encoding) = ONAME | LITERAL ;
    oName(*encoding) = names[code] ;
  }

  return TRUE ;
}


/** Bodge routine to build an encoding until a better modularity solution can
   be found. */
Bool pdfout_buildttencoding(OBJECT *font, NAMECACHE *encoding[256])
{
  Bool result = FALSE ;
  TT_DATA *tt_data ;
  TT_CACHE *tt_cache ;
  OBJECT *fido, *charstrings ;
  charstring_methods_t *ttfns ;

  HQASSERT( encoding , "encoding NULL" ) ;

  if ( oType(*font) != ODICTIONARY ||
       (fido = fast_extract_hash_name(font, NAME_FID)) == NULL ||
       oType(*fido) != OFONTID ||
       (charstrings = fast_extract_hash_name(font, NAME_CharStrings)) == NULL ||
       oType(*charstrings) != ODICTIONARY )
    return error_handler(INVALIDFONT) ;

  if ( (tt_cache = tt_set_font(oFid(*fido), font)) == NULL )
    return FALSE ;

  VERIFY_OBJECT(tt_cache, TT_CACHE_NAME) ;
  ttfns = &tt_cache->ttmethods ;
  tt_data = &tt_cache->tt_data ;

  if ( (tt_cache->font_data = fontdata_open(tt_cache->source,
                                            tt_cache->fdmethods)) == NULL )
    return FALSE ;

  /* Load tables only if not done so already. We need the 'cmap' table and
     the 'post' table. We need to know the number of glyphs in the font to
     get the post table size, so we also load 'maxp'. Faking a 'post' table
     may need OS/2 and 'cmap' tables, so load them first. */
  if ( tt_readmaxpTable(ttfns, tt_data) && tt_must_have(tt_data->tt_maxp) &&
       tt_readcmapTable(ttfns, tt_data) && /* optional */
       tt_reados2Table(ttfns, tt_data) &&  /* optional */
       tt_readpostTable(ttfns, tt_data) &&
       (tt_data->tt_post.loaded || tt_fakepostTable(tt_data)) ) {
    result = tt_encoding_names(encoding, charstrings, tt_data) ;
  }

  fontdata_close(&tt_cache->font_data) ;

  return result ;
}

/*---------------------------------------------------------------------------*/
/** Build charstrings mapping for the font.
 *
 * [60727] TT PDF lookup rewrite:
 *
 * The original intention was to implement the PDF spec's TT lookup by building
 * Charstrings such that it could be used for Symbolic AND NonSymbolic lookups,
 * thus coping with the unlikely case of a single font stream being shared
 * between differing FontDescriptors but containing contradictory cmaps, whilst
 * simultaneously removing the per-glyph complexity of tt_pdf_lookup.
 *
 * This was achieved by adding the Symbolic (gid=cmap[code]) mapping into the
 * Charstrings dictionary using integer keys, and thus allowing an integer
 * selector to be passed through plotchar before being looked up in Charstrings
 * as is normal for PS. Polymorphism prevents contradictory glyphname clashes.
 *
 * However, Acrobat does not stick to the published algorithm in the awkward
 * case of an Encoded font with a (3,1) cmap but a Symbolic flag. Acrobat seems
 * to take the presence of the Encoding (and this is an Embedded Encoding, not
 * the 'named' Encoding the spec describes) as mandating a Nonsymbolic mapping,
 * and so chooses the (3,1) cmap. However, it then takes the Symbolic flag as
 * meaning it should look up character codes directly - despite it being a
 * unicode cmap!
 *
 * We work around this difficulty by noting that much of the 0-255 unicode range
 * has existing AGL glyphnames that we have already added to Charstrings. So, we
 * simply invent glyphnames for the remaining 8bit unicodes and add them. Then,
 * when dealing with such a font in pdf_show, we select the glyphname that will
 * map to the gid we *would* have selected through the unfortunate misuse of the
 * unicode cmap.
 */

static Bool tt_ps_charstrings(OBJECT *charstrings, TT_DATA *tt_data)
{
  uint32 code ;
  uint16 gid ;
  NAMECACHE *glyphName ;
  TT_CMAP_ENTRY *cmap ;
  TT_POST *post ;

/* symbolic cmap priority */
  static PEL_PRIORITY cmap_symbolic_lookup[] = {
    { TT_PID_MICROSOFT, TT_EID_WIN_SYMBOL, TT_LID_UNDEFINED },  /* 3,0 */
    { TT_PID_MACINTOSH, TT_EID_ROMAN, TT_LID_UNDEFINED }        /* 1,0 */
  } ;

/* nonsymbolic cmap priority */
  static PEL_PRIORITY cmap_nonsymbolic_lookup[] = {
    { TT_PID_MICROSOFT, TT_EID_WIN_UGL, TT_LID_UNDEFINED },  /* 3,1 */
    { TT_PID_MACINTOSH, TT_EID_ROMAN, TT_LID_UNDEFINED }     /* 1,0 */
  } ;

  HQASSERT(charstrings, "No charstrings object") ;
  HQASSERT(oType(*charstrings) == ODICTIONARY, "charstrings is not dictionary") ;
  HQASSERT(tt_data, "No TT data") ;

  post = &tt_data->tt_post ;

  /* 'Post' table reverse mapping. Lowest GID with a name takes priority. */
  if (post->loaded) {
    for ( gid = 0 ; gid < post->numPostGlyphs ; ++gid ) {
      if ( (glyphName = post->GlyphNames[gid]) != NULL ) {
        oName(nnewobj) = glyphName ;
        if ( !fast_extract_hash(charstrings, &nnewobj) ) {
          oInteger(inewobj) = gid ;
          if ( !fast_insert_hash(charstrings, &nnewobj, &inewobj) )
            return FALSE ;
        }
      }
    }
  }

/* Do symbolic mapping.
 *
 * This is a non-standard extension - the symbolic mapping is stored as an
 * integer-to-integer lookup in the Charstrings dict.
 */
  cmap = tt_findCmap(tt_data, cmap_symbolic_lookup,
                     NUM_ARRAY_ITEMS(cmap_symbolic_lookup)) ;
  if (cmap) {
    uint16 c ;
    OBJECT map = OBJECT_NOTVM_INTEGER(0) ;
    for (c = 0; c < 256; c++) {
      if (tt_cmap_lookup(cmap, c, &gid, tt_cmap_lookup_quiet) &&
          gid != TT_NO_GLYPH) {
        oInteger(inewobj) = c ;
        oInteger(map) = gid ;
        if (!insert_hash(charstrings, &inewobj, &map))
          return FALSE ;
      }
    }
  }

/* Do nonsymbolic mapping */
  cmap = tt_findCmap(tt_data, cmap_nonsymbolic_lookup,
                     NUM_ARRAY_ITEMS(cmap_nonsymbolic_lookup)) ;
  if (cmap) {
    if ( cmap->PlatformID == TT_PID_MICROSOFT ) {
      int32 i ;
      uint16 len, code ;
      uint8* name ;
      char agl[256] ;

      /* If the font has an encoding (which should cause a nonsymbolic mapping)
       * and a (3,1) is available, but the Descriptor says it is symbolic,
       * Acrobat gets in a muddle and looks up the character code directly in
       * the (3,1), which is nonsensical.
       *
       * We cope with this travesty by mapping from code to glyphname specially,
       * and ensuring that suitable mappings exist in Charstrings.
       */
      for (i=0; i<256; i++)
        agl[i] = FALSE ;

      /* (3,1) is mapped through the AGL
       * name -> AGL[] -> Unicode -> cmap[] -> gid
       *
       * As we want to cope with other encodings at a later date, we
       * need to include as much of the AGL as we can map through the
       * cmap.
       */
      i = 0 ;
      while (i > -1) {
        if (agl_iterate(&i, &name, &len, &code)) {
          /* Look up this AGL member in the cmap */
          if (tt_cmap_lookup(cmap, code, &gid, tt_cmap_lookup_quiet) &&
              gid != TT_NO_GLYPH) {
            /* Successful mapping, add to the Charstrings */
            if ( (oName(nnewobj) = cachename(name, len)) == NULL )
              return FALSE;
            oInteger(inewobj) = gid ;
            if (code < 256)
              agl[code] = TRUE ;
            if ( !fast_insert_hash(charstrings, &nnewobj, &inewobj) )
              return FALSE ;
          }
        }
      }

      /* And now the dreadful (3,1) symbolic lookup - for those mappings not
       * already covered by the above AGL lookup, invent glyphnames.
       */
      for (code=0; code<256; code++) {
        if (!agl[code] &&
            tt_cmap_lookup(cmap, code, &gid, tt_cmap_lookup_quiet) &&
            gid && gid != TT_NO_GLYPH) {
          /* No AGL mapping for this (3,1) glyph - invent one */
          if ( (oName(nnewobj) = tt_gid_name(code)) == NULL )
            return FALSE;
          oInteger(inewobj) = gid ;
          if (!fast_insert_hash(charstrings, &nnewobj, &inewobj))
            return FALSE ;
        }
      }

    } else {

      /* (1,0) is mapped through the "MacOS" encoding
       * name -> MacOS[] -> code -> cmap[] -> gid
       *
       * Again, we encapsulate as much of this encoding as possible.
       */
      for (code = 32; code < 256; code++) {
        if (tt_cmap_lookup(cmap, code, &gid, tt_cmap_lookup_quiet) &&
            gid && gid != TT_NO_GLYPH) {
          oName(nnewobj) = MacOSEncoding[code] ;
          oInteger(inewobj) = gid ;
          if (!fast_insert_hash(charstrings, &nnewobj, &inewobj))
            return FALSE ;
        }
      }
    }
  }

  /* Charstrings now contains the 'post' table, the 256-integer symbolic
   * mapping, plus an AGL or MacOS mapping for the nonsymbolic case.
   */

  return TRUE ;
}


/* tt_xps_maxindex() - get maximum font glyph index
 */
Bool tt_xps_maxindex(
/*@in@*/ /*@notnull@*/
  OBJECT* font,
/*@out@*/ /*@notnull@*/
  int32*  max_index)
{
  OBJECT*   fido;
  OBJECT*   fonttype;
  TT_CACHE* tt_cache;
  TT_DATA*  tt_data;

  HQASSERT(font,"tt_xps_maxindex: NULL font object");
  HQASSERT(max_index,"tt_xps_maxindex: NULL max_index");

  /* Check font dict is valid CID font from TTF */
  if ( oType(*font) != ODICTIONARY ||
       (fido = fast_extract_hash_name(font, NAME_FID)) == NULL ||
       oType(*fido) != OFONTID ||
       (fonttype = fast_extract_hash_name(font, NAME_FontType)) == NULL ||
       oType(*fonttype) != OINTEGER ||
       oInteger(*fonttype) != CIDFONTTYPE2 ) {
    return error_handler(INVALIDFONT);
  }

  if ( (tt_cache = tt_set_font(oFid(*fido), font)) == NULL )
    return FALSE;

  VERIFY_OBJECT(tt_cache, TT_CACHE_NAME) ;
  tt_data = &tt_cache->tt_data;

  /* If maxp not yet loaded, load it */
  if ( !tt_data->tt_maxp.tried ) {
    Bool ok;

    tt_cache->font_data = fontdata_open(tt_cache->source, tt_cache->fdmethods);
    if ( tt_cache->font_data == NULL )
      return FAILURE(FALSE) ;

    ok = (tt_readmaxpTable(&tt_cache->ttmethods, tt_data) &&
          tt_must_have(tt_data->tt_maxp)) ;

    fontdata_close(&tt_cache->font_data);

    if ( !ok )
      return FALSE ;
  }

  *max_index = tt_data->tt_maxp.numGlyphs - 1;
  return TRUE;
}

/*---------------------------------------------------------------------------*/
/* XPS Unicode mapping - from unicode to CMAP */

static int32              map_type = 0;
static uint32             map_ID = 0xFFFFFFFF;
static unicode_convert_t* map_convert = 0;


/* Convert a unicode into a codepoint that can be looked up in the cmap.
 */
int32 map_unicode(int32 unicode)
{
  uint8  buffer[8];
  uint8* output = &buffer[0];
  uint8* input = &buffer[4];
  int32 result, in = 4, out = 4;

  if (map_type == 0) {
    /* cmap is unicode already */
    return unicode;
  }

  input[0] = (uint8) (255 & unicode);
  input[1] = (uint8) (255 & (unicode >> 8));
  input[2] = (uint8) (255 & (unicode >> 16));
  input[3] = (uint8) (255 & (unicode >> 24));

  if ( unicode_convert_buffer(map_convert, &input, &in, &output, &out, TRUE)
       != UTF_CONVERT_OK)
    return 0;

  out = 4 - out;      /* number of output bytes received */
  output -= out;      /* ptr to first byte */
  result = output[0]; /* single byte result */
  in = 1;             /* bytes expected */

  if (out == 0)
    return 0;         /* return notdef, if we failed to map */

  switch (map_type) {
  case 1: /* mac */
    HQASSERT( out == 1, "Unexpected Macintosh encoding");
    break;
  case 2: /* Shift-JIS */
    if ( (result > 128 && result < 160) || (result > 223 && result < 253) ) {
      HQASSERT( out == 2, "Unexpected Shift-JIS encoding");
      in = 2;
    } else {
      HQASSERT( out == 1, "Unexpected Shift-JIS encoding");
    }
    break;
  case 3: /* PRC */
    if ( result > 160 && result < 255 ) {
      HQASSERT( out == 2, "Unexpected PRC encoding");
      in = 2;
    } else {
      HQASSERT( out == 1, "Unexpected PRC encoding");
    }
    break;
  case 4: /* Big5 */
    if ( result > 128 && result < 255 ) {
      HQASSERT( out == 2, "Unexpected Big5 encoding");
      in = 2;
    } else {
      HQASSERT( out == 1, "Unexpected Big5 encoding");
    }
    break;
  case 5: /* Wansung */
    if (result > 128 && result < 255 ) {
      HQASSERT( out == 2, "Unexpected Wansung encoding");
      in = 2;
    } else {
      HQASSERT( out == 1, "Unexpected Wansung encoding");
    }
    break;
  default:
    HQFAIL("Unknown map type");
    in = 0;
  }

  if (out != in)  return 0;  /* unexpected encoding, so return notdef */

  if (out == 2)   result = (result << 8) | output[1];
  return result;
}


/* tt_xps_lookup() - get font glyph index for Unicode codepoint.
 */
Bool tt_xps_lookup(
/*@out@*/ /*@notnull@*/
  int32*  gindex,
/*@in@*/ /*@notnull@*/
  OBJECT* font,
  int32   codepoint,
  void**  cmap_ptr)
{
  uint16    gid;
  TT_CACHE* tt_cache;
  OBJECT*   fido;
  OBJECT*   fonttype;
  TT_DATA*  tt_data;
  TT_CMAP_ENTRY** cmap = (TT_CMAP_ENTRY**)cmap_ptr;

  /* Prefer Unicode mappings */
  static PEL_PRIORITY cmap_xps_lookup[] = {
    { TT_PID_MICROSOFT, TT_EID_WIN_UGL,      TT_LID_UNDEFINED },
    { TT_PID_MICROSOFT, TT_EID_WIN_SYMBOL,   TT_LID_UNDEFINED },
    { TT_PID_MACINTOSH, TT_EID_ROMAN,        TT_LID_UNDEFINED },
    { TT_PID_MICROSOFT, TT_EID_WIN_SHIFTJIS, TT_LID_UNDEFINED },
    { TT_PID_MICROSOFT, TT_EID_WIN_PRC,      TT_LID_UNDEFINED },
    { TT_PID_MICROSOFT, TT_EID_WIN_BIG5,     TT_LID_UNDEFINED },
    { TT_PID_MICROSOFT, TT_EID_WIN_WANSUNG,  TT_LID_UNDEFINED },
  } ;

  HQASSERT( cmap, "tt_xps_lookup: NULL cmap pointer");
  HQASSERT( gindex, "tt_xps_lookup: NULL gindex pointer");
  HQASSERT( font, "tt_xps_lookup: NULL font pointer");

  if ( *cmap == NULL ) { /* the first call for this Glyph element... */
    uint32 ID;

    /* Check font dict is valid CID font from TTF */
    if ( oType(*font) != ODICTIONARY ||
         (fido = fast_extract_hash_name(font, NAME_FID)) == NULL ||
         oType(*fido) != OFONTID ||
         (fonttype = fast_extract_hash_name(font, NAME_FontType)) == NULL ||
         oType(*fonttype) != OINTEGER ||
         oInteger(*fonttype) != CIDFONTTYPE2 ) {
      return error_handler(INVALIDFONT);
    }

    if ( (tt_cache = tt_set_font(oFid(*fido), font)) == NULL )
      return FALSE;

    VERIFY_OBJECT(tt_cache, TT_CACHE_NAME) ;
    tt_data = &tt_cache->tt_data;

    /* If cmap not yet loaded, load it */
    if ( !tt_data->tt_cmap.tried ) {
      Bool ok;

      tt_cache->font_data = fontdata_open(tt_cache->source, tt_cache->fdmethods);
      if ( tt_cache->font_data == NULL )
        return FAILURE(FALSE) ;

      ok = (tt_readcmapTable(&tt_cache->ttmethods, tt_data) &&
            tt_must_have(tt_data->tt_cmap)) ;

      fontdata_close(&tt_cache->font_data);

      if ( !ok )
        return FALSE ;
    }

    /* Find the Unicode map */
    *cmap = tt_findCmap(tt_data, cmap_xps_lookup, NUM_ARRAY_ITEMS(cmap_xps_lookup));
    if ( *cmap == NULL )
      return error_handler(INVALIDFONT);

    /* Select the correct transcoder */
    ID = ((*cmap)->PlatformID << 16) | (*cmap)->EncodeID;

    if ( ID != map_ID ) {
      char*  name = 0;
      uint32 len = 3;

      if ( map_convert ) {
        /* close previous transcoder */
        unicode_convert_close(&map_convert);
        map_convert = 0;
      }

      map_ID = ID;
      switch ( ID ) {
      case 0x30000:  /* unicode cmaps */
      case 0x30001:  map_type = 0;  break;
      case 0x10000:  map_type = 1;  name = "mac";           break;
      case 0x30002:  map_type = 2;  name = "pck";           break;
      case 0x30003:  map_type = 3;  name = "1383"; len = 4; break;
      case 0x30004:  map_type = 4;  name = "Big5"; len = 4; break;
      case 0x30005:  map_type = 5;  name = "ksc";           break;
      default:
        map_type = 0;
        HQFAIL("Unknown cmap type");
      }
      if ( map_type ) {
        map_convert = unicode_convert_open((uint8*)"UTF-32LE", 8, (uint8*)name, len,
                                           UCONVERT_BOM_REMOVE, (uint8*)"\0" ,1);
        if (map_convert == 0) {
          return error_handler(INVALIDFONT); /* it's not, but we can't handle it */
        }
      }
    } /* (ID != map_ID) */
  } /* (cmap == NULL) */

  gid = 0xFFFF;
  if (map_type) {
    codepoint = map_unicode(codepoint);
    if (codepoint == 0)
      gid = 0; /* couldn't be transcoded, so use notdef */
  }
  if ( gid == 0xFFFF &&
       (codepoint < 0 ||
        !tt_cmap_lookup(*cmap, (uint32)codepoint, &gid, tt_cmap_lookup_failed)) ) {
    gid = 0;  /* unknown codepoints are shown as notdef in XPS*/
  }

  *gindex = gid;

  return TRUE;
} /* tt_xps_lookup */

/*---------------------------------------------------------------------------*/
/** Define the PostScript VM wrapper for a TrueType font directly. Global/local
   mode should already be set when this is called. This replaces the old
   DecodeTrueType filter. */
static Bool tt_definefont_internal(OBJECT *newfont, OBJECT *subfont,
                                   OBJECT *fontname,
                                   OBJECT *force_cid, OBJECT *match,
                                   OBJECT *encoding, Bool xps,
                                   TT_CACHE *tt_cache)
{
  corecontext_t *context = get_core_context_interp();
  OBJECT foundname = OBJECT_NOTVM_NOTHING, cff_font = OBJECT_NOTVM_NOTHING ;
  int32 index, defined = 0 ;
  charstring_methods_t *ttfns ;
  TT_DATA *tt_data ;
  Bool report_font_repairs = context->fontsparams->ReportFontRepairs;

  HQASSERT(newfont, "No TrueType font object") ;
  HQASSERT(tt_cache, "No TrueType cache") ;
  HQASSERT(tt_cache->source, "No data source") ;
  HQASSERT(oType(*tt_cache->source) == OFILE,
           "TrueType file object is not OFILE") ;
  HQASSERT(encoding, "No TrueType font encoding") ;

  ttfns = &tt_cache->ttmethods ;
  tt_data = &tt_cache->tt_data ;

  switch ( tt_cache->type ) {
  case TT_FONT_TTF:
  case TT_FONT_TTC:
    break ;
  case TT_FONT_OTTO:
    /* It's a CFF flavour OpenType, so find the CFF table, pass that to
       cff_definefont, and squirrel away the resulting font to be stuffed
       into the outer TT font later. [29899] */
    {
      TT_TABLE *cff_table = NULL ;
      OBJECT params = OBJECT_NOTVM_NOTHING ;

      if ( !tt_initFont(tt_cache, 0) ||
           (cff_table = tt_findTable(tt_data, tt_maptag("CFF "))) == NULL )
        return FAILURE(FALSE);
      oInteger(inewobj) = (int32) (tt_cache->offset + cff_table->offset) ;
      if ( !ps_dictionary(&params, 1) ||
           !fast_insert_hash_name(&params, NAME_FontOffset, &inewobj) ||
           !cff_definefont(&cff_font, &params, tt_cache->source) )
        return FALSE ;
      HQASSERT( oType(cff_font) == ODICTIONARY, "No OTTO CFF font") ;
    }
    break ;
  default:
    return error_handler(INVALIDFONT) ;
  }

  /* If there are multiple fonts, and we want to override the name, but we
     haven't selected one to override, then we shouldn't proceed (we'd
     unhelpfully define them all with the same name). */
  if ( tt_cache->numFonts > 1 && subfont == NULL && fontname != NULL )
    return error_handler(UNDEFINED) ;

  /* Loop over all fonts in a TTC */
  for ( index = 0 ; (uint32)index < tt_cache->numFonts ; ++index ) {
    OBJECT ttfont = OBJECT_NOTVM_NOTHING, fontinfo = OBJECT_NOTVM_NOTHING ;
    OBJECT fontmatrix = OBJECT_NOTVM_NOTHING, fontbbox = OBJECT_NOTVM_NOTHING,
      charstrings = OBJECT_NOTVM_NOTHING ;
    OBJECT fencod = OBJECT_NOTVM_NULL ;
    OBJECT cidsysinfo = OBJECT_NOTVM_NOTHING, cidmap = OBJECT_NOTVM_NOTHING ;
    Bool is_cid, genuine_post ;
    int32 cidcount ;
    TT_HEAD *tt_head = &tt_data->tt_head ;
    TT_MAXP *tt_maxp = &tt_data->tt_maxp ;
    TT_POST *tt_post = &tt_data->tt_post ;

    if ( subfont != NULL )
      OCopy(foundname, *subfont) ;
    else
      object_store_integer(&foundname, index) ;

    if ( !tt_initFont(tt_cache, index) )
      return FALSE ;

    /* Read enough to get the names table and determine if this is the font
       we want. */
    if ( !tt_readheadTable(ttfns, tt_data) || !tt_must_have(*tt_head) ||
         !tt_readmaxpTable(ttfns, tt_data) || !tt_must_have(*tt_maxp) ||
         !tt_readnameTable(ttfns, tt_data) )
      return FALSE ;

    /* If we don't have a font name, read the names table to find one. If there
       isn't a PostScript name, synthesise one out of the family name and the
       weight. */
    if ( !tt_match_fontname(tt_data, fontname, &foundname, index) )
      return FALSE ;

    /* If it didn't match, try the next name */
    if ( oType(foundname) == ONULL ) {
      /* Free the font data, and re-load for the next font. */
      tt_freeFont(tt_data) ;
      continue ;
    }

    HQASSERT(oType(foundname) == ONAME || oType(foundname) == OSTRING,
             "Font name found is wrong type") ;

    /* Post, OS/2 and CMap tables are optional. If there is no 'post' table,
       pretend there was a format 3.0 table, which gives no useful data. Need
       'OS/2' table and 'cmap' before 'post', because faked post may use AGL
       and MacRoman names for GIDs required by cmap(3,1) and cmap(1,0).
       OS/2 is required for tt_makeCIDinfo */
    if ( !tt_reados2Table(ttfns, tt_data) ||
         !tt_readcmapTable(ttfns, tt_data) ||
         !tt_readpostTable(ttfns, tt_data) )
      return FALSE ;

    genuine_post = tt_post->loaded ;

    /* Warn about use of repaired font tables in this font if actually used. */
    if ( (tt_head->repaired || tt_post->repaired) && report_font_repairs ) {
      uint8* fontname_str = (uint8*) "" ;
      int32 fontname_str_len = 0;

      if ( fontname != NULL ) {
        switch ( oType(*fontname) ) {
          case ONAME :
            fontname_str = theICList(oName(*fontname)) ;
            fontname_str_len = theINLen(oName(*fontname)) ;
            break;

          case OSTRING:
            fontname_str = oString(*fontname) ;
            fontname_str_len = theLen(*fontname) ;
            break;

          default:
            break;
        }
      }

      if ( tt_head->repaired && report_font_repairs )
        monitorf((uint8*)"Font %.*s : repaired head table.\n",
                  fontname_str_len, fontname_str) ;

      if ( genuine_post && tt_post->repaired  && report_font_repairs )
        monitorf((uint8*)"Font %.*s : repaired post table.\n",
                  fontname_str_len, fontname_str) ;
    }

/* [sab] Don't fake a post table
    if ( !genuine_post && !tt_fakepostTable(tt_data) )
      return FAILURE(FALSE) ;
*/

    /* Determine if this font should be a CID font, even if not requested. If
       it is a CID font, we want to find the Registry, Ordering, Supplement,
       and the CIDMap mapping from CID (character collection index) to GID. */
    if ( !tt_makeCIDinfo(ttfns, tt_data, force_cid, match,
                         &is_cid, &cidsysinfo, &cidmap, &cidcount) )
      return FALSE ;

    /* Create the TrueType font dictionary */
    if ( !ps_dictionary(&ttfont, 16) )
      return FALSE ;

    if ( !fast_insert_hash_name(&ttfont,
                                is_cid ? NAME_CIDFontName : NAME_FontName,
                                &foundname) )
      return FALSE ;

    /* [51068] If this is an XPS font and we are defining it as a CID, insert
     * a null CDevProc to avoid the vertical metrics being fudged by StdCDevProc. */
    if ( xps && is_cid ) {
      if ( !fast_insert_hash_name(&ttfont, NAME_CDevProc, &onull) )
        return FALSE ;
    }

    /* if there's a CFF subfont, stick that in. [29899] */
    if ( oType(cff_font) == ODICTIONARY ) {
      if ( !fast_insert_hash_name(&ttfont, NAME_OpenType, &cff_font) )
        return FALSE ;
    }

    /* Put the font file and font offset into the font dict. */
    oInteger(inewobj) = (int32)tt_cache->offset ;
    if ( !fast_insert_hash_name(&ttfont, NAME_FontOffset, &inewobj) ||
         !fast_insert_hash_name(&ttfont, NAME_sfnts, tt_cache->source) )
      return FALSE ;

    if ( tt_cache->type == TT_FONT_TTC ) {
      /* Put the font index in the collection. */
      oInteger(inewobj) = index ;
      if ( !fast_insert_hash_name(&ttfont, NAME_SubFont, &inewobj) )
        return FALSE ;
    }

    /* /CharStrings is a dictionary lookup from names to glyph IDs. Set this
       up before /Encoding so we have name to GID mapping. numGlyphs is not
       quite right for the CharStrings dictionary, because multiple mapping
       methods may cause several names to map to a GID. */
    if ( !ps_dictionary(&charstrings, is_cid ? 1 : tt_maxp->numGlyphs) )
      return FALSE ;

    oInteger(inewobj) = 0 ;
    if ( !fast_insert_hash_name(&charstrings, NAME_notdef, &inewobj) )
      return FALSE ;

    if ( !is_cid && !tt_ps_charstrings(&charstrings, tt_data) )
      return FAILURE(FALSE) ;

    if ( !object_access_reduce(READ_ONLY, &charstrings) ||
         !fast_insert_hash_name(&ttfont, NAME_CharStrings, &charstrings) )
      return FALSE ;

    /* If there is no encoding supplied, either create one, get it from a
       resource, or check it is the correct type. PLRM3 p.378 says that
       CIDFontType 2 must have an Encoding for compatibility with Type 42, but
       it will be ignored. */
    Copy(&fencod, encoding) ;
    if ( oType(*encoding) == ONULL ) {
      /* Create a new encoding. CID fonts get a notdef encoding, mapping all
         characters to /.notdef */
      if ( is_cid ) {
        object_store_name(&fencod, NAME_NotDefEncoding, LITERAL) ;
        /* N.B. nnewobje going on execstack, nnewobj on operandstack */
        oName(nnewobj) = &system_names[NAME_Encoding];
        oName(nnewobje) = &system_names[NAME_findresource];

        if ( !interpreter_clean(&nnewobje, &fencod, &nnewobj, NULL) )
          return FALSE ;

        Copy(&fencod, theTop(operandstack)) ;
        pop(&operandstack);
      } else if ( !tt_ps_encoding(&fencod, &charstrings, tt_data) )
        return FALSE ;
    }

    if ( oType(fencod) != OARRAY && oType(fencod) != OPACKEDARRAY )
      return error_handler(TYPECHECK);

    if ( theLen(fencod) != 256 )
      return error_handler(RANGECHECK);

    if ( !fast_insert_hash_name(&ttfont, NAME_Encoding, &fencod) )
      return FALSE ;

    oInteger(inewobj) = 0 ;
    if ( !fast_insert_hash_name(&ttfont, NAME_PaintType, &inewobj) ||
         !fast_insert_hash_name(&ttfont, NAME_StrokeWidth, &inewobj) )
      return FALSE ;

    oInteger(inewobj) = 42 ;
    if ( !fast_insert_hash_name(&ttfont, NAME_FontType, &inewobj) )
      return FALSE ;

    /* FontMatrix [1 0 0 1 0 0] or OTTO CFF FontMatrix [29899] */
    if ( oType(cff_font) == ODICTIONARY ) {
      OBJECT *cff_matrix ;
      cff_matrix = fast_extract_hash_name(&cff_font, NAME_FontMatrix) ;
      HQASSERT( cff_matrix != NULL, "OTTO CFF font has no FontMatrix") ;
      fontmatrix = *cff_matrix ;
    } else {
      if ( !ps_array(&fontmatrix, 6) ||
           !from_matrix(oArray(fontmatrix), &identity_matrix,
                        context->glallocmode) )
        return FALSE ;
    }
    if ( !object_access_reduce(READ_ONLY, &fontmatrix) ||
         !fast_insert_hash_name(&ttfont, NAME_FontMatrix, &fontmatrix) )
      return FALSE ;

    /* FontBBox [] */
    if ( !ps_array(&fontbbox, 4) )
      return FALSE ;

    theTags(oArray(fontbbox)[0]) = OREAL | LITERAL ;
    oReal(oArray(fontbbox)[0]) = (USERVALUE)((double)tt_head->xMin / (double)tt_head->unitsPerEm) ;
    theTags(oArray(fontbbox)[1]) = OREAL | LITERAL ;
    oReal(oArray(fontbbox)[1]) = (USERVALUE)((double)tt_head->yMin / (double)tt_head->unitsPerEm) ;
    theTags(oArray(fontbbox)[2]) = OREAL | LITERAL ;
    oReal(oArray(fontbbox)[2]) = (USERVALUE)((double)tt_head->xMax / (double)tt_head->unitsPerEm) ;
    theTags(oArray(fontbbox)[3]) = OREAL | LITERAL ;
    oReal(oArray(fontbbox)[3]) = (USERVALUE)((double)tt_head->yMax / (double)tt_head->unitsPerEm) ;

    if ( !object_access_reduce(READ_ONLY, &fontbbox) ||
         !fast_insert_hash_name(&ttfont, NAME_FontBBox, &fontbbox) )
      return FALSE ;

    /* FontInfo sub-dictionary */
    if ( !ps_dictionary(&fontinfo, 5) )
      return FALSE ;

    /* /ascent */
    /* /Notice (COPYRIGHT + TRADEMARK) */

    if ( genuine_post ) {
      /* /UnderlinePosition */
      oReal(rnewobj) = (USERVALUE)((double)tt_post->underlinePosition /
                                   (double)tt_head->unitsPerEm) ;
      if ( !fast_insert_hash_name(&fontinfo, NAME_UnderlinePosition, &rnewobj) )
        return FALSE ;

      /* /UnderlineThickness */
      oReal(rnewobj) = (USERVALUE)((double)tt_post->underlineThickness /
                                   (double)tt_head->unitsPerEm) ;
      if ( !fast_insert_hash_name(&fontinfo, NAME_UnderlineThickness, &rnewobj) )
        return FALSE ;

      /* /isFixedPitch */
      if ( !fast_insert_hash_name(&fontinfo, NAME_isFixedPitch,
                                  tt_post->isFixedPitch ? &tnewobj : &fnewobj) )
        return FALSE ;

      oReal(rnewobj) = (USERVALUE)((double)(tt_post->italicAngle >> 16) +
                                   (double)(tt_post->italicAngle & 0xffff) /
                                   (double)0xffff) ;
      if ( !fast_insert_hash_name(&fontinfo, NAME_ItalicAngle, &rnewobj) )
        return FALSE ;
    }

    if ( !object_access_reduce(READ_ONLY, &fontinfo) ||
         !fast_insert_hash_name(&ttfont, NAME_FontInfo, &fontinfo) )
      return FALSE ;

    /* CID fonts need a few extra entries */
    if ( is_cid ) {
      oInteger(inewobj) = 2 ;
      if ( !fast_insert_hash_name(&ttfont, NAME_CIDFontType, &inewobj) )
        return FALSE ;

      oInteger(inewobj) = cidcount ;
      if ( !fast_insert_hash_name(&ttfont, NAME_CIDCount, &inewobj) )
        return FALSE ;

      /* The CIDMap should be created as a string, array, or dictionary from
         the mapping from CID to GID. */
      HQASSERT(oType(cidmap) == OSTRING ||
               oType(cidmap) == OINTEGER ||
               oType(cidmap) == OARRAY ||
               oType(cidmap) == ODICTIONARY, "CIDMap is incorrect type") ;
      if ( !fast_insert_hash_name(&ttfont, NAME_CIDMap, &cidmap) )
        return FALSE ;

      /* Insert GDBytes regardless of whether CIDMap is of an appropriate
         type. The Red Book says it's required anyway, even though we don't
         check if CIDMap is a dictionary or an integer. */
      oInteger(inewobj) = 2 ;
      if ( !fast_insert_hash_name(&ttfont, NAME_GDBytes, &inewobj) )
        return FALSE ;

      HQASSERT(oType(cidsysinfo) == ODICTIONARY,
               "CIDSystemInfo is incorrect type") ;
      if ( !fast_insert_hash_name(&ttfont, NAME_CIDSystemInfo, &cidsysinfo) )
        return FALSE ;
    }

    /* Execute "<name> dict /(CID)Font defineresource" through PostScript. */

    /* N.B. nnewobje going on execstack, nnewobj on operandstack */
    oName(nnewobj) = &system_names[is_cid ? NAME_CIDFont : NAME_Font];
    oName(nnewobje) = &system_names[NAME_defineresource];

    if ( !interpreter_clean(&nnewobje,
                            fontname ? fontname : &foundname,
                            &ttfont, &nnewobj, NULL) )
      return FALSE ;

    Copy(newfont, theTop(operandstack)) ;
    pop(&operandstack) ;

    ++defined ;

    /* If we were requested to find a specific font, we're done. Otherwise,
       search for and define the next font in the TTC font set. */
    if ( subfont != NULL )
      return TRUE ;

    /* Free the font data, and re-load for the next font in a TT Collection. */
    tt_freeFont(tt_data) ;
  }

  if ( defined == 0 )
    return error_handler(INVALIDFONT) ;

  return TRUE ;
}

Bool tt_definefont(OBJECT *newfont, OBJECT *params, OBJECT *ttfile, Bool * found_3_1)
{
  OBJECT *force_cid = NULL, *cidsysinfo = NULL ;
  Bool result = FALSE ;
  FILELIST *flptr ;
  TT_CACHE tt_cache ;
  OBJECT *subfont = NULL, *fontname = NULL ;
  OBJECT encoding = OBJECT_NOTVM_NULL ;
  Bool xps = FALSE ;

  enum { tt_match_SubFont, tt_match_CIDFont, tt_match_Encoding,
         tt_match_FontOffset, tt_match_FontName, tt_match_CIDSystemInfo,
         tt_match_XPSFont, tt_match_n_entries
  } ;
  static NAMETYPEMATCH tt_match[tt_match_n_entries + 1] = {
    { NAME_SubFont | OOPTIONAL, 5, { ONAME, OINTEGER, OSTRING, OARRAY, OPACKEDARRAY }},
    { NAME_CIDFont | OOPTIONAL, 1, { OBOOLEAN }},
    { NAME_Encoding | OOPTIONAL, 3, { ONAME, OARRAY, OPACKEDARRAY }},
    { NAME_FontOffset | OOPTIONAL, 1, { OINTEGER }},
    { NAME_FontName | OOPTIONAL, 2, { ONAME, OSTRING }},
    { NAME_CIDSystemInfo | OOPTIONAL, 1, { ODICTIONARY }},
    { NAME_XPSFont | OOPTIONAL, 1, { OBOOLEAN }},
    DUMMY_END_MATCH
  };

  HQASSERT(ttfile, "No TrueType file object") ;

  if ( oType(*ttfile) != OFILE )
    return error_handler(INVALIDFONT) ;

  /* Check the input TT file. It must be an input file, and must be
     seekable. */
  flptr = oFile(*ttfile) ;
  if ( !isIOpenFile(flptr) || !isIInputFile(flptr) || !file_seekable(flptr) )
    return error_handler(IOERROR) ;

  /* Set up a stack-based TT cache structure */
  tt_cache.fid = 0 ;
  tt_cache.source = ttfile ;
  tt_cache.offset = 0 ;
  tt_cache.font_data = NULL ;
  tt_cache.fdmethods = &blobdata_file_methods ;
  tt_cache.next = NULL ;

  tt_cache.ttmethods = tt_charstring_fns ;
  tt_cache.ttmethods.data = &tt_cache ;

  NAME_OBJECT(&tt_cache, TT_CACHE_NAME) ;

  if ( params != NULL ) {
    if ( oType(*params) != ODICTIONARY ||
         !dictmatch(params, tt_match) )
      return error_handler(TYPECHECK) ;

    subfont = tt_match[tt_match_SubFont].result ;
    fontname = tt_match[tt_match_FontName].result ;

    force_cid = tt_match[tt_match_CIDFont].result ;
    cidsysinfo = tt_match[tt_match_CIDSystemInfo].result ;
    if ( tt_match[tt_match_Encoding].result )
      Copy(&encoding, tt_match[tt_match_Encoding].result) ;
    if ( tt_match[tt_match_FontOffset].result ) {
      int32 offset = oInteger(*tt_match[tt_match_FontOffset].result) ;
      if ( offset < 0 )
        return error_handler(RANGECHECK) ;
      tt_cache.offset = (uint32)offset ;
    }
    if ( tt_match[tt_match_XPSFont].result )
      xps = oBool( *tt_match[tt_match_XPSFont].result) ;
  }

  /* If the encoding was supplied as a name, run "findencoding" to convert it
     into an array. */
  if ( oType(encoding) == ONAME ) {
    /* N.B. nnewobje going on execstack, nnewobj on operandstack */
    oName(nnewobj) = &system_names[NAME_Encoding];
    oName(nnewobje) = &system_names[NAME_findresource];

    if ( !interpreter_clean(&nnewobje, &encoding, &nnewobj, NULL) )
      return FALSE ;

    Copy(&encoding, theTop(operandstack)) ;
    pop(&operandstack);
  }

  /* If this succeeds, we MUST call fontdata_close. */
  if ( (tt_cache.font_data = fontdata_open(tt_cache.source,
                                           tt_cache.fdmethods)) == NULL )
    return FALSE ;

  /* After this, must cleanup on exit by calling tt_freeFont */
  result = (tt_initFontCollection(&tt_cache) &&
            tt_definefont_internal(newfont, subfont, fontname,
                                   force_cid, cidsysinfo, &encoding, xps,
                                   &tt_cache)) ;

  if (found_3_1) { /* [64587] Did the font contain a (3,1) cmap? */
    TT_CMAP_ENTRY * cmap = tt_cache.tt_data.tt_cmap.cmaps ;

    while (cmap) {
      if (cmap->PlatformID == 3 && cmap->EncodeID == 1) {
        cmap = 0 ;
        *found_3_1 = TRUE ;
      } else
        cmap = cmap->next ;
    }
  }

  tt_freeFont(&tt_cache.tt_data) ;
  fontdata_close(&tt_cache.font_data) ;

  UNNAME_OBJECT(&tt_cache) ;

  return result ;
}

/** PostScript operator to expose tt_definefont for findfont. This operator
   takes a file or a file and a parameter dictionary, and defines a TrueType
   font stub that uses the file in its /sfnts entry. */
Bool definettfont_(ps_context_t *pscontext)
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

  if ( !tt_definefont(&newfont, params, fonto, NULL) )
    return FALSE ;

  /* Return new font on stack*/
  Copy(fonto, &newfont) ;

  if ( pop_args > 0 )
    npop(pop_args, &operandstack) ;

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/** TrueType font structure cache type. This is used to prevent the TrueType
   code from having to reload the TT font data for every charstring. The TT
   cache routines use the common font data cache to store sfnt blocks
   routines. The font data pointer is not retained between characters; a new
   instance is opened for each character. */
static TT_CACHE *tt_font_cache = NULL ;

/* Somewhat spuriously, we don't store the allocation size of each TT font.
   We may do so in future, so long as we have a quick way of determining how
   large retained fonts are. */


/** Low-memory handling data for the TrueType font cache. */
mm_simple_cache_t tt_mem_cache;


/** The size of the entire TrueType cache. */
#define tt_cache_size (tt_mem_cache.data_size)


/** Create a cache entry for the font in question, and initialise the fontdata
   and font set. */
static TT_CACHE *tt_set_font(int32 fid, OBJECT *fdict)
{
  TT_CACHE *tt_font, **tt_prev = &tt_font_cache ;

  HQASSERT(fdict, "No font dictionary") ;

  /* Search for a matching entry in the TT data cache */
  while ( (tt_font = *tt_prev) != NULL ) {
    VERIFY_OBJECT(tt_font, TT_CACHE_NAME) ;

    /* If this entry was trimmed by a GC scan, remove it */
    if ( tt_font->source == NULL ) {
      *tt_prev = tt_font->next ;
      HQASSERT(tt_font->font_data == NULL,
               "TT font lost source (GC scan?) but has fontdata open") ;
      tt_clear_font((long)tt_font->fid) ; /* Clear font from interpreter */
      tt_freeFont(&tt_font->tt_data) ;
      UNNAME_OBJECT(tt_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)tt_font, sizeof(TT_CACHE)) ;
      tt_cache_size -= sizeof(TT_CACHE) ;
    } else if ( fid == tt_font->fid ) {
      /* Move entry to head of MRU list */
      *tt_prev = tt_font->next ;
      break ;
    } else {
      tt_prev = &tt_font->next ;
    }
  }

  /* If no TT cache found, allocate a new one and initialise the correct
     font entry in it. The object is copied into new object memory, because
     we don't know where the pointer came from. It could be a pointer from
     the C, PostScript or graphics stack, in which case its contents will
     change unpredictably. */
  if ( !tt_font ) {
    int32 fontindex = 0 ;
    uint32 fontoffset = 0 ;
    OBJECT *sfnts, *theo ;
    const blobdata_methods_t *fdmethods ;

    /* sfnts specifies the source of font data. It may be an array of strings
       or a file. */
    if ( (sfnts = fast_extract_hash_name(fdict, NAME_sfnts)) == NULL ) {
      (void)error_handler(INVALIDFONT) ;
      return NULL ;
    }

    switch ( oType(*sfnts) ) {
    case OFILE:
      /* FontOffset specifies the starting offset of the font data within the
         an sfnts file. */
      if ( (theo = fast_extract_hash_name(fdict, NAME_FontOffset)) != NULL ) {
        if ( oType(*theo) != OINTEGER || oInteger(*theo) < 0 ) {
          (void)error_handler(INVALIDFONT) ;
          return NULL ;
        }
        fontoffset = (uint32)oInteger(*theo) ;
      }

      fdmethods = &blobdata_file_methods ;
      break ;
    case OARRAY:
    case OPACKEDARRAY:
      fdmethods = &blobdata_sfnts_methods ;
      break ;
    default:
      (void)error_handler(INVALIDFONT) ;
      return NULL ;
    }

    /* SubFont specifies the font index within a TrueType Collection. */
    if ( (theo = fast_extract_hash_name(fdict, NAME_SubFont)) != NULL ) {
      if ( oType(*theo) != OINTEGER || oInteger(*theo) < 0 ) {
        (void)error_handler(INVALIDFONT) ;
        return NULL ;
      }
      fontindex = oInteger(*theo) ;
    }

    /* Don't mind if mm_alloc fails after get_lomemory, the object memory will
       be returned by a restore or GC. */
    if ( (theo = get_lomemory(1)) == NULL ||
         (tt_font = mm_alloc(mm_pool_temp, sizeof(TT_CACHE),
                             MM_ALLOC_CLASS_CID_DATA)) == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    Copy(theo, sfnts) ;
    tt_font->fid = fid ;
    tt_font->offset = fontoffset ;
    tt_font->source = theo ;
    tt_font->fdmethods = fdmethods ;
    tt_font->font_data = NULL ;
    tt_font->ttmethods = tt_charstring_fns ;
    tt_font->ttmethods.data = tt_font ;

    NAME_OBJECT(tt_font, TT_CACHE_NAME) ;

    if ( (tt_font->font_data = fontdata_open(tt_font->source,
                                             tt_font->fdmethods)) == NULL ) {
      UNNAME_OBJECT(tt_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)tt_font, sizeof(TT_CACHE)) ;
      return NULL ;
    }

    /* Only initialise the TT font set, which does not allocate any TT_data */
    if ( !tt_initFontCollection(tt_font) ||
         !tt_initFont(tt_font, fontindex) ) {
      tt_freeFont(&tt_font->tt_data) ;
      fontdata_close(&tt_font->font_data) ;
      UNNAME_OBJECT(tt_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)tt_font, sizeof(TT_CACHE)) ;
      return NULL ;
    }

    fontdata_close(&tt_font->font_data) ;

    tt_cache_size += sizeof(TT_CACHE) ;
  }

  tt_font->next = tt_font_cache ;
  tt_font_cache = tt_font ;

  return tt_font ;
}

/** Clean out cache of TT fonts */
void tt_restore(int32 savelevel)
{
  TT_CACHE *tt_font, **tt_prev = &tt_font_cache ;
  int32 numsaves = NUMBERSAVES(savelevel) ;

  while ( (tt_font = *tt_prev) != NULL ) {
    VERIFY_OBJECT(tt_font, TT_CACHE_NAME) ;

    /* Test if the data source will be restored */
    if ( tt_font->source == NULL ||
         mm_ps_check(numsaves, tt_font->source) != MM_SUCCESS ) {
      *tt_prev = tt_font->next ;

      HQASSERT(tt_font->font_data == NULL,
               "Font data open when restoring TT font") ;

      /* TTCs will have to free any extra data allocated */
      tt_clear_font((long)tt_font->fid) ; /* Clear font from interpreter */
      tt_freeFont(&tt_font->tt_data) ;
      UNNAME_OBJECT(tt_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)tt_font, sizeof(TT_CACHE)) ;
      tt_cache_size -= sizeof(TT_CACHE) ;
    } else {
      tt_prev = &tt_font->next ;
    }
  }
}


/** GC scanning for TT cache. I would prefer to have a hook to finalisation,
   so we can delete the cache entry when the object is GC'ed. */
static mps_res_t MPS_CALL tt_scan(mps_ss_t ss, void *p, size_t s)
{
  TT_CACHE *tt_font ;

  UNUSED_PARAM(void *, p);
  UNUSED_PARAM(size_t, s);

  MPS_SCAN_BEGIN( ss )
    for ( tt_font = tt_font_cache ; tt_font ; tt_font = tt_font->next ) {
      /* If we're GC scanning, we are probably in a low-memory situation.
         Mark this font entry as freeable if it's not in use, fix the source
         pointer if it is. The MPS is not reentrant, so we can't actually
         free it now. */
      if ( tt_font->font_data == NULL )
        MPS_SCAN_UPDATE( tt_font->source, NULL );
      else
        /* Fix the font data source objects, so they won't be collected. */
        MPS_RETAIN( &tt_font->source, TRUE );
    }
  MPS_SCAN_END( ss );

  return MPS_RES_OK ;
}


/** \brief Clear a given quantity of data from the TrueType cache.

    \param purge  The amount to purge.
    \return  The amount purged.

  This won't touch data currently in use, so it may fail to clear as
  much as requested.
 */
static size_t tt_purge(size_t purge)
{
  TT_CACHE *tt_font, **tt_prev = &tt_font_cache ;
  size_t orig_size = tt_cache_size, level;

  level = orig_size - purge;
  tt_cache_size = 0 ;

  /** \todo The TT font list is an MRU list, so it'd be best to purge
      starting from the tail. */
  while ( (tt_font = *tt_prev) != NULL ) {
    VERIFY_OBJECT(tt_font, TT_CACHE_NAME) ;

    if ( tt_font->font_data == NULL &&
         /* Garbage-collected fonts are always deleted. */
         (tt_cache_size >= level || tt_font->source == NULL) ) {
      *tt_prev = tt_font->next ;

      /* TTCs will have to free any extra data allocated */
      tt_clear_font((long)tt_font->fid) ; /* Clear font from interpreter */
      tt_freeFont(&tt_font->tt_data) ;
      UNNAME_OBJECT(tt_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)tt_font, sizeof(TT_CACHE)) ;
    } else {
      tt_cache_size += sizeof(TT_CACHE) ;
      tt_prev = &tt_font->next ;
    }
  }
  HQASSERT((tt_cache_size == 0) == (tt_font_cache == NULL) &&
           orig_size >= tt_cache_size,
           "Inconsistent TT cache size") ;
  return orig_size - tt_cache_size ;
}


void tt_cache_clear(void)
{
  (void)tt_purge(tt_mem_cache.data_size);
}


/** Purge method for \c tt_mem_cache. */
static Bool tt_mem_purge(mm_simple_cache_t *cache,
                         Bool *purged_something, size_t purge)
{
  UNUSED_PARAM(mm_simple_cache_t *, cache);
  *purged_something = tt_purge(purge) != 0;
  return TRUE;
}


/*---------------------------------------------------------------------------*/
/* Charstring routines for TrueType fonts */

static uint8 *tt_frame_open(void *data, uint32 offset, uint32 length)
{
  TT_CACHE *tt_font = data ;
  TT_DATA *tt_data ;
  uint8 *frame ;

  HQASSERT(tt_font != NULL, "No TT font info") ;
  tt_data = &tt_font->tt_data ;

  if ( tt_font->type == TT_FONT_TTC &&
       offset + length <= TT_SIZE_HEADER + tt_data->numTables * TT_SIZE_TABLE ) {
    /* If offset and length would place this frame within the header or table
       directory, redirect it to the appropriate destination in the
       collection.
    */
    /** \todo @@@ TODO FIXME ajcd 2003-04-12:
       There is a problem accessing the start
       of the first table within the file if the TT header is longer than the
       TTC header (which is very likely). In this case, requests for the
       start of the table will be mis-interpreted as requests for the TT
       header. There is no way round this without distinguishing between
       header and non-header requests. */
    uint8 *ttmem ;
    uint32 position ;

    /* Convert the font index to an offset to the appropriate font's offset
       table. */
    if ( (ttmem = fontdata_frame(tt_font->font_data,
                                 tt_font->offset + TTC_SIZE_HEADER,
                                 TTC_SIZE_OFFSET * tt_font->numFonts,
                                 sizeof(uint8))) == NULL ) {
      (void)error_handler(INVALIDFONT) ;
      return NULL ;
    }

    HQASSERT(tt_font->fontindex >= 0 &&
             (uint32)tt_font->fontindex < tt_font->numFonts,
             "TTC font index is out of range") ;
    TT_ulong(ttmem, tt_font->fontindex * TTC_SIZE_OFFSET, position) ;

    offset += position ;
  }

  /* Barf. The Bitstream TT interpreter directly overlays the frame pointer
     we return with a structure. This can cause non-aligned data crashes on
     some architectures. We have no idea the purpose of the data we're
     returning, so we have to align all such frames with a word boundary. */
  if ( (frame = fontdata_frame(tt_font->font_data, tt_font->offset + offset, length,
                               sizeof(int32))) == NULL )
    return NULL ;

  HQASSERT(WORD_IS_ALIGNED(uintptr_t, frame),
           "TT fontdata frame is not aligned correctly") ;

  return frame ;
}

static void tt_frame_close(void *data, uint8 **frame)
{
  UNUSED_PARAM(void *, data) ;

  HQASSERT(frame, "No TT frame to release from") ;

  *frame = NULL ;
}

/*---------------------------------------------------------------------------*/
/** Font lookup and top-level charstring routines for TrueType fonts */
static Bool tt_lookup_char(FONTinfo *fontInfo,
                           charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context") ;

  HQASSERT(theIFontType(fontInfo) == FONTTYPE_TT, "Not in a TrueType font") ;

  if ( !get_sdef(fontInfo, &context->glyphname, &context->definition) )
    return FALSE ;

  /* PLRM3 p.351: Array (procedure) for charstring is a glyph replacement
     procedure. */
  switch ( oType(context->definition) ) {
  case OARRAY: case OPACKEDARRAY:
    /* Replacement glyph detected. Use Type 3 charstring methods. */
    context->chartype = CHAR_BuildChar ;
    break ;
  case OINTEGER:
    context->chartype = CHAR_TrueType ;
    break ;
  default:
    return error_handler(INVALIDFONT) ;
  }

  return TRUE ;
}

/** Determine if the named char exists in the font, and whether it has been
   replaced by a procedure. CID Font Type 2 does not allow glyph substitution
   (there is no CharStrings), but in other respects is similar to the
   TrueType begin_char; we'll use the same routine for now. */
static Bool tt_begin_char(FONTinfo *fontInfo,
                          charcontext_t *context)
{
  TT_CACHE *tt_font ;

  HQASSERT(context, "No char context") ;
  HQASSERT(theIFontType(fontInfo) == FONTTYPE_TT ||
           theIFontType(fontInfo) == CIDFONTTYPE2, "Not in a TrueType font") ;

  if ( context->chartype == CHAR_BuildChar )
    return (*font_type3_fns.begin_char)(fontInfo, context) ;

  HQASSERT(context->chartype == CHAR_TrueType, "No decision about char type") ;

  if ( (tt_font = tt_set_font(theCurrFid(*fontInfo),
                              &theMyFont(*fontInfo))) == NULL )
    return FALSE ;

  /* OTTO fonts are handed to the cff code [29899] */
  if ( tt_font->type == TT_FONT_OTTO ) {
    FONTinfo cff_fontInfo = *fontInfo ;
    OBJECT *cff_dict ;

    context->chartype = CHAR_Type2 ;
    cff_fontInfo.fonttype = FONTTYPE_CFF ;

    cff_dict = fast_extract_hash_name(&fontInfo->thefont, NAME_OpenType) ;
    HQASSERT(cff_dict, "CFF end_char: No CFF font") ;
    cff_fontInfo.thefont = *cff_dict ;

    return (*font_cff_fns.begin_char)(&cff_fontInfo, context) ;
  }

  VERIFY_OBJECT(tt_font, TT_CACHE_NAME) ;
  HQASSERT(oType(context->definition) == OINTEGER,
           "Character definition is not glyph index") ;

  if ( (tt_font->font_data = fontdata_open(tt_font->source,
                                           tt_font->fdmethods)) == NULL )
    return FALSE ;

  context->methods = &tt_font->ttmethods ;

  return TRUE ;
}

static void tt_end_char(FONTinfo *fontInfo,
                        charcontext_t *context)
{
  TT_CACHE *tt_font ;
  charstring_methods_t *ttmethods ;

  HQASSERT(context, "No character context") ;
  HQASSERT(theIFontType(fontInfo) == FONTTYPE_TT ||
           theIFontType(fontInfo) == CIDFONTTYPE2, "Not in a TrueType font") ;

  if ( context->chartype == CHAR_BuildChar ) {
    (*font_type3_fns.end_char)(fontInfo, context) ;
    return ;
  }

  /* OTTO fonts are handed to the cff code. [29899]
     Note that we can no longer get the tt_font from the context, coz that
     may be a CFF context, so do it the long way. */
  if ( (tt_font = tt_set_font(theCurrFid(*fontInfo),
                              &theMyFont(*fontInfo))) == NULL )
    return;
  VERIFY_OBJECT(tt_font, TT_CACHE_NAME) ;

  if ( tt_font->type == TT_FONT_OTTO ) {
    FONTinfo cff_fontInfo = *fontInfo ;
    OBJECT *cff_dict ;

    cff_fontInfo.fonttype = FONTTYPE_CFF ;

    cff_dict = fast_extract_hash_name(&fontInfo->thefont, NAME_OpenType) ;
    HQASSERT(cff_dict, "CFF end_char: No CFF font") ;
    cff_fontInfo.thefont = *cff_dict ;

    (*font_cff_fns.end_char)(&cff_fontInfo, context) ;
    return ;
  }

  ttmethods = context->methods ;
  HQASSERT(ttmethods, "TT charstring methods lost") ;
  HQASSERT(ttmethods == &tt_font->ttmethods,
           "TT charstring methods inconsistent with font structure") ;

  /* Release fontdata */
  fontdata_close(&tt_font->font_data) ;

  object_store_null(&context->definition) ;
}

/*---------------------------------------------------------------------------*/
/** OpenType sfnt-wrapped CFF font support (OTTO). These fonts are an OpenType
   table wrapper containing a "CFF " table with an entire CFF font in it. We
   use the TrueType wrappers to get sufficient information to find the CFF
   table, modify the offset, and then let the CFF code have at it. */
Bool otf_definefont(OBJECT *newfont, OBJECT *params, OBJECT *otffile)
{
  Bool result = FALSE ;
  FILELIST *flptr ;
  TT_CACHE otf_cache ;
  OBJECT otfparams = OBJECT_NOTVM_NOTHING ;
  TT_TABLE *tab_cff ;
  uint32 offset = 0 ;

  enum { otf_match_FontOffset, otf_match_n_entries } ;
  static NAMETYPEMATCH otf_match[otf_match_n_entries + 1] = {
    { NAME_FontOffset | OOPTIONAL, 1, { OINTEGER }},
    DUMMY_END_MATCH
  };

  HQASSERT(otffile, "No OpenType file object") ;

  if ( oType(*otffile) != OFILE )
    return error_handler(INVALIDFONT) ;

  /* Check the input OTF file. It must be an input file, and must be
     seekable. */
  flptr = oFile(*otffile) ;
  if ( !isIOpenFile(flptr) || !isIInputFile(flptr) || !file_seekable(flptr) )
    return error_handler(IOERROR) ;

  /* Set up a stack-based TT cache structure */
  otf_cache.fid = 0 ;
  otf_cache.source = otffile ;
  otf_cache.offset = 0 ;
  otf_cache.font_data = NULL ;
  otf_cache.fdmethods = &blobdata_file_methods ;
  otf_cache.next = NULL ;

  otf_cache.ttmethods = tt_charstring_fns ;
  otf_cache.ttmethods.data = &otf_cache ;

  NAME_OBJECT(&otf_cache, TT_CACHE_NAME) ;

  if ( params != NULL ) {
    if ( oType(*params) != ODICTIONARY ||
         !dictmatch(params, otf_match) )
      return error_handler(TYPECHECK) ;

    if ( otf_match[otf_match_FontOffset].result ) {
      int32 offset = oInteger(*otf_match[otf_match_FontOffset].result) ;
      if ( offset < 0 )
        return error_handler(RANGECHECK) ;
      otf_cache.offset = (uint32)offset ;
    }
  }

  HQASSERT(!error_signalled(), "Starting font while in error condition");
  error_clear_newerror();

  /* If this succeeds, we MUST call fontdata_close. */
  if ( (otf_cache.font_data = fontdata_open(otf_cache.source,
                                            otf_cache.fdmethods)) == NULL )
    return FALSE ;

  /* After this, must cleanup on exit by calling tt_freeFont */
  if ( tt_initFontCollection(&otf_cache) &&
       otf_cache.type == TT_FONT_OTTO &&
       tt_initFont(&otf_cache, 0) &&
       (tab_cff = tt_findTable(&otf_cache.tt_data, tt_maptag("CFF "))) != NULL ) {
    offset = otf_cache.offset + tab_cff->offset ;
    result = TRUE ;
  }

  tt_freeFont(&otf_cache.tt_data) ;
  fontdata_close(&otf_cache.font_data) ;

  UNNAME_OBJECT(&otf_cache) ;

  /* Allow OTF routines to return FALSE, defaulting to INVALIDFONT. */
  if ( !result ) {
    if ( !newerror )
      result = error_handler(INVALIDFONT) ;
    return FALSE ;
  }

  if ( !params ) {
    if ( !ps_dictionary(&otfparams, 1) )
      return FALSE ;
    params = &otfparams ;
  }

  oInteger(inewobj) = (int32)offset ;
  if ( !fast_insert_hash_name(params, NAME_FontOffset, &inewobj) )
    return FALSE ;

  result = cff_definefont(newfont, params, otffile) ;

  /* restore old FontOffset in case font is read again. [12806] */
  oInteger(inewobj) = (int32)otf_cache.offset ;

  if ( ! fast_insert_hash_name(params, NAME_FontOffset, &inewobj) )
    return FALSE ;

  return result ;
}

/*---------------------------------------------------------------------------*/
/** PostScript operator to expose otf_definefont for findfont. This operator
   takes a file or a file and a parameter dictionary, and defines a CFF
   font stub that uses the file in its /FontFile entry. */
Bool defineotffont_(ps_context_t *pscontext)
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

  if ( !otf_definefont(&newfont, params, fonto) )
    return FALSE ;

  /* Return new font on stack*/
  Copy(fonto, &newfont) ;

  if ( pop_args > 0 )
    npop(pop_args, &operandstack) ;

  return TRUE ;
}


static void init_C_globals_tt_font(void)
{
  ttfont_gc_root = NULL ;
  tt_global_error = 0 ;
  tt_xwidth = 0 ;
  tt_ywidth = 0 ;
#ifdef DEBUG_PICTURE
  debug_tt = 0 ;
#endif
  map_type = 0 ;
  map_ID = 0xFFFFFFFF ;
  map_convert = 0 ;
  tt_font_cache = NULL ;
  tt_mem_cache.low_mem_handler = mm_simple_cache_handler_init;
  tt_mem_cache.low_mem_handler.name = "TrueType font cache";
  tt_mem_cache.low_mem_handler.tier = memory_tier_disk;
  /* Renderer threads don't touch this, so it's mt-safe. */
  tt_mem_cache.low_mem_handler.multi_thread_safe = TRUE;
  /* no init needed for offer */
  tt_mem_cache.data_size = tt_mem_cache.last_data_size = 0;
  tt_mem_cache.last_purge_worked = TRUE;
  /* There is a fair amount of processing to build a TT font. */
  tt_mem_cache.cost = 5.0f;
  tt_mem_cache.pool = mm_pool_temp;
  tt_mem_cache.purge = &tt_mem_purge;
}


static Bool ttfont_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

#ifdef DEBUG_PICTURE
  register_ripvar(NAME_debug_tt, OINTEGER, &debug_tt) ;
#endif

  if ( !low_mem_handler_register(&tt_mem_cache.low_mem_handler) )
    return FALSE;
  if ( mps_root_create(&ttfont_gc_root, mm_arena, mps_rank_exact(),
                       0, tt_scan, NULL, 0 ) != MPS_RES_OK ) {
    low_mem_handler_deregister(&tt_mem_cache.low_mem_handler);
    return FAILURE(FALSE);
  }
  return TRUE ;
}


static void ttfont_finish(void)
{
  mps_root_destroy(ttfont_gc_root) ;
  low_mem_handler_deregister(&tt_mem_cache.low_mem_handler);
}


void ttfont_C_globals(core_init_fns *fns)
{
  init_C_globals_tt_font() ;

  fns->swstart = ttfont_swstart ;
  fns->finish = ttfont_finish ;
}

/* Log stripped */
