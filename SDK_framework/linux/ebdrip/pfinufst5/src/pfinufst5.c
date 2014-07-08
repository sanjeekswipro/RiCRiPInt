/** \file
* \ingroup PFIN
*
* $HopeName: SWpfin_ufst5!src:pfinufst5.c(EBDSDK_P.1) $
*
* Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
*
* This example is provided on an "as is" basis and without
* warranty of any kind. Global Graphics Software Ltd. does not
* warrant or make any representations regarding the use or results
* of use of this example.
*
* \brief Example PFIN module implementation using UFST5
*
* This is an example PFIN module implementation. It provides not only a fully
* featured PFIN implementation supplying fonts in outline and bitmap form, but
* also PCL5 and PCLXL font support through the use of PFIN miscops.
*/

#if (!defined(VXWORKS) && !defined(__CC_ARM))
#include <memory.h>       /* for memset etc. */
#endif
#include <string.h>       /* for strlen etc. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>

#include "std.h"
#include "swpfinapi.h"
#include "swpfinpcl.h"
#include "rdrapi.h"
#include "eventapi.h"
#include "pfinufst5.h"
#include "hqnpcleo.h"

/* The length of a simple array */
#define lengthof(arr) (sizeof(arr) / sizeof((arr)[0]))

#ifdef USE_UFST5

#define UFST_SHOW(_s_, ...)

/* ========================================================================== */

/* Define to automatically upscale low resolution bitmap glyphs */
#undef ENHANCED_RESOLUTION

enum { WS_ID = 0x1234 };

/* macro to check that our workspace is present and correct */
#define CHECK_WS(_ws)                                         \
  if ((_ws) == 0 || (_ws)->id != WS_ID || (_ws)->suspended) { \
    HQFAIL("Module not initialised/resumed correctly");       \
    return SW_PFIN_ERROR;                                     \
  }

/* The maximum number of fonts in an FCO - storage is allocated dynamically, so
 * this is just a sanity check.
 */
#define MAX_FONTS_IN_AN_FCO 256

/* Macro to make symbolset numbers easier to deal with, eg: wd = SS(579,'L') */
#define SS(n,l) ((n) * 32 + (l) - '@')

/* Macro to make UFST's symbolset reference numbers easier to deal with, eg
 * ufst = USS('R','8') - note that this is endian dependent */
#if (BYTEORDER == HILO)
#define USS(a,b) ((a)<<8|(b))
#else
#define USS(a,b) ((b)<<8|(a))
#endif

/* The module defaults to the ROMAN8 symbolset. If that can't be found, the
 * first symbolset will be used. This can be altered using the /DefaultSymbolSet
 * configuration key.
 */
#define ROMAN8 SS(8,'U')

/* Typeface nums and symbolsets of the PI fonts, which need special handling */
#define WINGDINGS 31402
#define DINGBATS  45101
#define SYMBOLPCL 16686
#define SYMBOLPS  45358

#define WINGDINGS_SS SS(579,'L')
#define DINGBATS_SS  SS(14,'L')
#define SYMBOL_SS    SS(19,'M')

/* Highest bound font type. Should be 9, but seems to need to be 2 */
#define BOUNDFONTTYPE 2

#if FCO_DISK
/* Disk FCOs filenames must be found through the RDR API, not hard coded.
 * Note though that UFST has a build-selectable hard limit on the number of
 * FCOs, which will have to be adhered to at run-time. The Dynamic Fonts build
 * option allows access to further FCOs, it appears.
 */
static unsigned char l_pPS3FCO[] = ".\\SW\\ufst\\ps3___xh.fco";
static unsigned char l_pWingDingFCO[] = ".\\SW\\ufst\\wd___xh.fco";
static unsigned char l_pPluginFCO[] = ".\\SW\\ufst\\plul__xi.fco";
#endif

/* The 'magic' Intellifont design size */
#define INTELLIFONT 8782

/* UFST's updated glyph definitions */
unsigned int hq3updt_lj4[] ;     /* Pointer to HQ3 Intellifont glyph updates */

/* Wide data access macros (native endianness) */
#define OP(data,O,P) ((data[O]<<8)|data[P])
#define OPQ(data,O,P,Q) ((data[O]<<16)|OP(data,P,Q))
#define OPQR(data,O,P,Q,R) (((uint32)data[O]<<24)|OPQ(data,P,Q,R))

#define USE_METRICS_CACHE 1

#if USE_METRICS_CACHE
/* -------------------------------------------------------------------------- */
/* A cache of glyph widths for a particular font/symbolset combination.
 *
 * This is hung off a uidcache and documents the widths for a short contiguous
 * set of cids. Widths are initialised to -1 meaning not yet calculated.
 */

/* must be a power of two... */
#define METRICSLENGTH 32
#define METRICSMASK (METRICSLENGTH-1)

typedef struct metricscache {
  struct metricscache * next ;    /* ptr to next or 0 */
  int32  set ;                    /* cid of the first chr in this set */
  float  widths[METRICSLENGTH] ;  /* -1 means width not yet calculated */
} metricscache ;

/* Magic numbers in the above widths array - initialised to METRICS_UNKNOWN */
#define METRICS_UNKNOWN -1
#define METRICS_MISSING -2
#endif

/* -------------------------------------------------------------------------- */
/* A tiny cache of UniqueIDs against symbolset references */

typedef struct uidcache {
  struct uidcache * next ;      /* ptr to next or 0 */
#if USE_METRICS_CACHE
  struct metricscache * metrics ;  /* ptr to metrics cache */
  HqBool missing ;                 /* TRUE if any MISSING metrics */
#endif
  int32  uid ;                  /* UniqueID allocated by PFIN */
  int32  ssref ;                /* symbolset reference */
  int32  bold ;                 /* amount of PCL6 enboldening */
  int32  faux ;                 /* faux italicisation flag */
} uidcache ;

/* -------------------------------------------------------------------------- */
/* Our representation of an FCO font */

typedef struct {
  int32  typeface;              /* apparent typeface family */
  uint16 also;                  /* proxied typeface family (or == typeface) */
  uint16 ord;                   /* for sorting the font list */
  uint32 nFid;                  /* UFST font id - (FCO handle << 16 | index) */
  size_t psnamelen;             /* just to save lots of strlens */
  size_t xlnamelen;             /* ditto */
  float  hmi;                   /* adjusted HMI (closer to target device) */
  float  scale;                 /* font size adjustment (or 1.0f) */
  float  height;                /* font height adjustment (or 1.0f) */
  TTFONTINFOTYPE *pttFontInfo;  /* UFST font info */
  uidcache * uids;              /* UniqueIDs allocated to this font */
} fco_font;

#define FONTNAME(_fid_) ((char*)ws->Fonts[_fid_].pttFontInfo + \
                         ws->Fonts[_fid_].pttFontInfo->psname)
#define PCLFONTNAME(_fid_) ((char*)ws->Fonts[_fid_].pttFontInfo + \
                            ws->Fonts[_fid_].pttFontInfo->tfName)
#define XLFONTNAME(_fid_) ((char*)ws->Fonts[_fid_].pttFontInfo + \
                           ws->Fonts[_fid_].pttFontInfo->pcltTypeface)

/* qsort comparator for the above structure */
int cmp_fco_font(const void* a, const void* b)
{
  return (int)(((fco_font*)a)->ord) - (int)(((fco_font*)b)->ord);
}


/* ========================================================================== */
/** \brief Subclass of \c sw_pfin_instance to keep private data about UFST
    fonts. */
typedef struct pfin_ufst5_instance {
  sw_pfin_instance super ; /**< PFIN instance superclass must be first */

  /* Admin */
  int    id;           /* a unique value because we are being paranoid */
  HqBool suspended;    /* TRUE if suspended by PFIN - sanity checking */

  /* Glyph cache */
  PIFOUTLINE pol;      /* glyph created during metrics, cached for
                        * subsequent outline/bitmap call */
  int32  fid;          /* the fid, */
  int32  cid;          /* cid and */
  int32  ss;           /* symbolset of the cached glyph */
  int32  bold;         /* boldness of the cached glyph */
  int32  faux;         /* PCL5 faux italicisation */
  int32  wmode;        /* vertical mode of cached glyph */
  HqBool bitmap;       /* bitimageness of the cached glyph */
  double xres;         /* resolution of cached bitmap glyph */
  double yres;         /* resolution of cached bitmap glyph */
  double dx;           /* size/res of cached bitmap glyph */
  double dy;           /* size/res of cached bitmap glyph */
  double matrix[6];    /* matrix of cached glyph */
  HqBool transformed;  /* If cached glyph is pretransformed */
  int    orientation_opts;  /* Flags to indicate PCL bitmap orientation. */

  /* Metrics cache */
  int32  prev_id;      /* For miscop metrics call - previous font id, or -1, */
  int32  prev_ss;      /* previous symbolset, */
  int32  prev_ssref;   /* and reference for the symbolset */
  int32  prev_ufst;    /* and previous UFST-internal symbolset number. */
  int32  prev_bold;    /* XL emboldening */
  int32  prev_faux;    /* PCL5 faux italic */

  HqBool make_bitmaps; /* set by the metrics method */

#ifdef ENHANCED_RESOLUTION
  uint8* x2;
  size_t x2size;
#endif
  /* Encoding stuff */
  sw_datum * encoding; /* UniqueEncoding in sw_datum array form */
  unsigned char * glyphname; /* Previous glyphname found in above encoding */
  signed int codepoint;      /* Codepoint of the above glyphname */

  /* Configuration stuff */
  int    defaultss;          /* ROMAN8 or whatever (UFST-internal) */
  int    couriers[8];        /* indices into sorted font list of the Couriers */
  HqBool darkcourier;        /* whether to prefer the dark Courier (if both) */
  HqBool bothcouriers;       /* true if FCOs contain both Couriers */
  HqBool matchpsfonts;       /* allow PCL font match to select non-PCL fonts */
  HqBool emulate4700;        /* emulate HP4700 'unexpected behaviour' */
  HqBool listpsfonts;        /* list PS fonts during resourceforall */
  int    maxbitmap;          /* maximum pixels per em for bitmaps, or 0 */

  /* User data */
  struct user_ss * user_symbolsets ;
  struct user_font * user_fonts ;
  int32  next_ss ;           /* Allocated ID for next softloaded symbolset */
  int32  next_ref ;          /* Allocated ID for next softloaded font */
  int32  LinePrinter ;       /* Allocated ID of LinePrinter "soft" font */

  /* RDR table indirection */
  PCL_FONTMAP *   pfontmap ;
  unsigned char * pglyphlist ;
  PCL_SSINFO *    psslist ;
  XL_DATA *       pxldata ;

  size_t          fontmaplength ;   /* length, not size - number of entries */
  size_t          sslistlen ;       /* ditto */

  /* UFST stuff */
  pfin_ufst5_fns* cgif ;

  IFCONFIG        ConfigBlock ;       /* UFST config block */

  /* FCOs */
  unsigned char * MainFCO ;           /* Pointer to the main FCO */
  unsigned char * SecondFCO ;         /* Pointer to the secondary FCO */
  unsigned char * PluginFCO ;         /* Pointer to the plugin FCO */

  short           MainFCOHandle ;     /* UFST handle for the main FCO */
  short           SecondFCOHandle ;   /* UFST handle for the secondary FCO */
  short           PluginFCOHandle ;   /* UFST handle for the plugin FCO */

  fco_font *      Fonts ;             /* The list of font infos */
  int32           TotalFonts ;        /* The number of fonts in all FCOs */
  int32           DefaultFont ;       /* The index of the default font */

  sw_pfin_outline_context * context ; /* Outline context for the current chr */

} pfin_ufst5_instance ;

/* Memory abstraction */
#define myalloc(size) ws->super.mem->implementation->alloc(ws->super.mem, (size))
#define myfree(block) ws->super.mem->implementation->free(ws->super.mem, (block))

/* forward reference */
int define_RDRs(void) ;
int find_RDRs(pfin_ufst5_instance * ws) ;
sw_datum * unique_encoding(pfin_ufst5_instance * ws) ;
static int32 get_symbolset_char_requirements(pfin_ufst5_instance *ws,
                                             int32 ss, uint8 char_requirements[8],
                                             int8 font_encoding) ;
static HqBool supports_all_symbol_sets(uint8 char_requirements[8],
                                       uint8 char_complement[8],
                                       int8 font_encoding) ;

/* -------------------------------------------------------------------------- */

/* static context for the non-re-entrant get_* functions */
struct pfin_ufst5_instance * get_id_ws ;
struct user_font * get_id_font ;
int    get_id_null ;

/* ========================================================================== */
/** Initialise hook is called once per start of the RIP, before any other
    methods. The implementation may be subclassed to attach class variables
    to it. */

#ifdef DEBUG_UFST_CACHE_HITS
static int32 outlines_made ;
static int32 cache_hits ;
static int32 cache_blocks ;
static int32 current_uids ;
static int32 peak_uids ;
#endif

static sw_pfin_result RIPCALL pfin_ufst5_init(sw_pfin_api *api,
                                              const sw_pfin_init_params *params)
{
  UNUSED_PARAM(sw_pfin_api *, api) ;
  UNUSED_PARAM(const sw_pfin_init_params *, params) ;

  HQASSERT(api, "No api?") ;
  HQASSERT(params, "No params?") ;

  if (!define_RDRs())
    return FALSE ;      /* RDR system is broken - how odd */

  /* Non re-entrant build of UFST requires these temporary globals */
  get_id_ws = NULL ;
  get_id_font = NULL ;

#ifdef DEBUG_UFST_CACHE_HITS
  outlines_made = 0 ;
  cache_hits = 0 ;
  cache_blocks = 0 ;
  current_uids = 0 ;
  peak_uids = 0 ;
#endif

  return TRUE ;
}

/** Finish hook is called once per RIP, after any other methods. The
    implementation may be subclassed to attach class variables to it. */
static void RIPCALL pfin_ufst5_finish(sw_pfin_api *api)
{
  UNUSED_PARAM(sw_pfin_api *, api) ;

  HQASSERT(api, "No api?") ;
}

/* ========================================================================== */
/**
 * \brief Load all the fonts from an FCO
 * \param allocated_fonts the number of fonts allocated in the ws->Fonts array.
 */
int load_FCO(pfin_ufst5_instance *ws,
             unsigned char * FCO,
             unsigned short Handle,
             int32 allocated_fonts)
{
  PCL_FONTMAP * fontMap = ws->pfontmap ;
  int i, status ;
  UW16 size ;

  UNUSED_PARAM(int32, allocated_fonts);

  for (i=0; i < MAX_FONTS_IN_AN_FCO; i++) {
    fco_font * font = NULL;
    char * name;

    status = ws->cgif->pfnCGIFfco_Access(FCO, (UW16)i, TFATRIB_KEY, &size, NULL);
    /* if status is 519, it just means this font index doesn't exist...
       we assume that there are no more fonts */
    if (status == ERR_fco_FontNew_CorruptData)
      break;
    if (status)
      return status;

    HQASSERT(ws->TotalFonts < allocated_fonts,
             "Tried to read more fonts than allocated for.");

    font = &ws->Fonts[ws->TotalFonts];
    font->pttFontInfo = (TTFONTINFOTYPE *) myalloc(size);
    if (font->pttFontInfo == NULL) {
      HQFAIL("pfin_ufst5_initialise: CGIFfco_Access() error: "
             "failed to allocate structure");
      return 1;
    }

    ++(ws->TotalFonts);  /* so the above allocation will be freed on error */
    font->nFid = Handle << 16 | i;

    status = ws->cgif->pfnCGIFfco_Access(FCO, (UW16)i, TFATRIB_KEY, &size,
                                          (LPSB8)font->pttFontInfo);
    if (status)
      return status;

    UFST_SHOW("%d \t%s\n", i, FONTNAME(i));

    name = XLFONTNAME(ws->TotalFonts-1) ;
    font->xlnamelen = (*name == 'x') ? 0 : strlen(name) ;

    name = FONTNAME(ws->TotalFonts-1); /* helps debugging */
    font->psnamelen = strlen(name) ; /* save on strlens */

#if 0
    {  /* debugging purposes */
       TTFONTINFOTYPE * f = font->pttFontInfo;
       char * n, * x, * p, * d;

       n = (char*)f + f->psname;
       x = (char*)f + f->pcltTypeface;
       p = (char*)f + f->tfName;
       n = (char*)f + f->familyName;
       n = (char*)f + f->weightName;
       d = (char*)f + f->tfDescriptor;
       n = (char*)f + f->copyrightNotice;

       /* Output unordered, unfiddled fontmap entries */
       printf((*x == 'x') ?
              "%03i  {%d, %d, %d},\t\t%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX /* %s */\n" :
              "%03i  {%d, %d, %d},\t\t%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX /* %s%s(%s) */ \n",
              i,
              font->pttFontInfo->pcltFontNumber,
              font->pttFontInfo->pcltTypeFamily,
              (font->pttFontInfo->pcltTypeFamily) ? 999 : UNMAPPEDFONT,
              f->pcltChComp[7],
              f->pcltChComp[6],
              f->pcltChComp[5],
              f->pcltChComp[4],
              f->pcltChComp[3],
              f->pcltChComp[2],
              f->pcltChComp[1],
              f->pcltChComp[0],
              p, (*p) ? " " : "", x
              ) ;
   }
#endif

    /* We take the FCO's PCL font numbering verbatim, unless overridden by
       the optional fontmap */
    font->typeface = font->also = font->pttFontInfo->pcltTypeFamily ;

    font->scale  = 1.0f;
    font->height = 1.0f;
    font->ord    = (font->typeface) ? UNMAPPEDFONT-1 : UNMAPPEDFONT;
    font->hmi    = (float) font->pttFontInfo->spaceBand /
                   ( (font->pttFontInfo->scaleFactor == INTELLIFONT) ?
                     72.307f / 72.0f * INTELLIFONT :
                     font->pttFontInfo->scaleFactor ) ;
    font->uids   = NULL;

    if (fontMap) {
      int a = 0, b = (int) ws->fontmaplength ;          /* lengthof(fontmap); */
      int32 ufst = font->pttFontInfo->pcltFontNumber ;  /* UFST's font number */

      while (a<b) {
        int m = (a+b)/2;
        if (fontMap[m].ufst == ufst) {
          /* If we find this font number in the fontmap, we have a new typeface
           * number, and may have an additional proxied typeface number, ordering
           * and HMI overrides...
           */
          font->typeface = fontMap[m].typeface;
          font->also     = (fontMap[m].also != 0) ?
                           fontMap[m].also : (uint16)font->typeface;
          font->ord      = fontMap[m].ord;
          if (fontMap[m].hmi != 0)
            font->hmi    = fontMap[m].hmi;
          if (fontMap[m].scale != 0)
            font->scale  = fontMap[m].scale;
          if (fontMap[m].height != 0)
            font->height = fontMap[m].height;
          break;
        }
        if (fontMap[m].ufst < ufst)
          a = m+1;
        else
          b = m;
      }
    }
  }

  return 0;
}

/* Return the number of fonts in an FCO, or -1 if there's a problem */

int32 count_FCO(pfin_ufst5_instance * ws, unsigned char * FCO)
{
  int32 count;
  UW16 max = 0 ;
  TTFONTINFOTYPE * dummy = 0 ;

  for (count = 0; count < MAX_FONTS_IN_AN_FCO; count++) {
    UW16 size;
    int status ;

    status = ws->cgif->pfnCGIFfco_Access(FCO, (UW16)count, TFATRIB_KEY,
                                          &size, NULL);

    /* This is a work-around for 'unexpected behaviour' from UFST - the above
       call only seems to return failure if we actually loaded the previous
       font - otherwise it reports that all 256 fonts are available. */
    if (status == 0) {
      if (size > max) {
        max = size ;
        if (dummy)
          myfree(dummy) ;
        dummy = (TTFONTINFOTYPE *) myalloc(max);
        if (!dummy) {
          HQFAIL("count_FCO: out of memory");
          return -1 ;
        }
      }
      status = ws->cgif->pfnCGIFfco_Access(FCO, (UW16)count, TFATRIB_KEY,
                                            &size, (LPSB8)dummy);
    }

    /* if status is 519, it just means this font index doesn't exist...
       we assume that there are no more fonts */
    if (status) {
      if (status != ERR_fco_FontNew_CorruptData) {
        HQFAILV(("count_FCO: CGIFfco_Access() error %d\n", status));
        count = -1;
      }
      break;
    }
  }

  if (dummy)
    myfree(dummy) ;

  return count;
}

/* forward declaration */
int find_symbolset(pfin_ufst5_instance * ws, int32 symbolset, struct user_ss ** ppss);

/* ========================================================================== */
/** \brief Find FCOs via RDR, or failing that, the old GGE method */

int find_FCO(pfin_ufst5_instance *ws,
             int i, unsigned char ** pFCO)
{
  void * vFCO ; /* Because we can't cast any** to void** in general */
  if (SwFindRDR(RDR_CLASS_FONT, RDR_FONT_FCO, i, &vFCO, 0) == SW_RDR_SUCCESS) {
    *pFCO = (unsigned char *) vFCO ; /* But we can cast void* to any* */
  } else {
    int status ;

    switch (i) {
      case 0: /* Main FCO */
        status = ws->cgif->pfnUFSTGetPS3FontDataPtr(pFCO);
        if (status) {
          HQFAILV(("pfin_ufst5_initialise: UFSTGetPS3FontDataPtr() error "
                   "%d\n", status));
          return FALSE;
        }
        break ;

      case 1: /* WingDing FCO */
        status = ws->cgif->pfnUFSTGetWingdingFontDataPtr(pFCO);
        if (status) {
          HQFAILV(("pfin_ufst5_initialise: UFSTGetWingdingFontDataPtr() error "
                   "%d\n", status));
          return FALSE ;
        }
        break ;

      case 2: /* Plugin FCO */
        status = ws->cgif->pfnUFSTGetPluginDataPtr(pFCO);
        if (status) {
          HQFAILV(("pfin_ufst5_initialise: UFSTGetPluginDataPtr() error "
                   "%d\n", status));
          return FALSE;
        }
        break ;
    }
  }

  return TRUE ;
}

int find_ss(pfin_ufst5_instance *ws, int i, LPUB8 * paddr)
{
  void * vaddr ;
  if (SwFindRDR(RDR_CLASS_PCL, RDR_PCL_SS, i, &vaddr, 0) == SW_RDR_SUCCESS) {
    *paddr = (LPUB8) vaddr ;
  } else {
    int status = ws->cgif->pfnUFSTGetSymbolSetDataPtr(paddr, i) ;
    return (status == 0) ;
  }

  return TRUE ;
}
/* ========================================================================== */
/* Start (or resume) the PFIN module */

static sw_pfin_result RIPCALL pfin_ufst5_start(sw_pfin_instance *instance,
                                               sw_pfin_define_context *context,
                                               sw_pfin_reason reason)
{
  /* Downcast to UFST PFIN instance subclass */
  pfin_ufst5_instance *ws = (pfin_ufst5_instance *)instance;
  int32 i;
  int status;

  get_id_ws = ws;

  UNUSED_PARAM(sw_pfin_define_context *, context) ;

  UFST_SHOW("pfin_ufst5_initialise\n");

  HQASSERT(instance, "No context?") ;

  if (reason == SW_PFIN_REASON_START) {
    int32 total_fco_fonts = 0;

    /* initialise workspace */

    /* Admin */
    ws->id = WS_ID;
    ws->suspended = FALSE;

    /* User data */
    ws->user_symbolsets = 0 ;
    ws->user_fonts = 0 ;
    ws->next_ref = 1000 ;     /* updated after loading and counting FCO fonts */
    ws->next_ss = 1000 ;
    ws->LinePrinter = -1 ;

    ws->TotalFonts = 0 ;
    ws->DefaultFont = 0 ;

    /* Find the RDRs we use (for this instance) */
    if (!find_RDRs(ws))
      return SW_PFIN_ERROR ;
    /* Update our allocated ss number, if too small */
    if (ws->next_ss < (int32)ws->sslistlen + 10)
      ws->next_ss = (int32)ws->sslistlen + 10 ;

    /* Glyph cache */
    ws->pol = 0;
    ws->make_bitmaps = FALSE;

#ifdef ENHANCED_RESOLUTION
    ws->x2 = 0;
    ws->x2size = 0;
#endif

    /* Metrics cache */
    ws->prev_id = -1;
    ws->prev_ss = -1;
    ws->prev_ssref = -1 ;
    ws->prev_bold = 0;
    ws->prev_faux = 0;

    /* Encoding */
    ws->encoding = 0;
    ws->glyphname = 0;

    /* Config */
    ws->bothcouriers = FALSE;
    ws->darkcourier  = FALSE;
    ws->matchpsfonts = FALSE;
    ws->emulate4700  = TRUE;
    ws->listpsfonts  = FALSE;
    ws->maxbitmap    = 540 ;    /* Default is 540ppem which is 40pt at 300dpi */
    ws->defaultss    = -1;
    ws->defaultss    = find_symbolset(ws, ROMAN8, NULL);
    if (ws->defaultss == -1)
      ws->defaultss  = 0;

    /* define some fonts */
    ws->MainFCO = 0 ;
    ws->SecondFCO = 0 ;
    ws->PluginFCO = 0 ;

    ws->MainFCOHandle = 0 ;
    ws->SecondFCOHandle = 0 ;
    ws->PluginFCOHandle = 0 ;

#if FCO_DISK
#else
    if (!find_FCO(ws, 0, &ws->MainFCO) ||
        !find_FCO(ws, 1, &ws->SecondFCO) ||
        !find_FCO(ws, 2, &ws->PluginFCO))
      return SW_PFIN_ERROR ;
#endif

    status = ws->cgif->pfnCGIFinit(FSA0);
    if (status) {
      HQFAILV(("pfin_ufst5_initialise: CGIFinit() error: %d\n", status));
      return SW_PFIN_ERROR;
    }

#if GG_CUSTOM
    /* This must come AFTER the call to CGIFinit() */
    if_state.mem_context = (void *) ws ;
#endif

    memset(&ws->ConfigBlock, 0x00, sizeof(ws->ConfigBlock));

#if FCO_DISK
#else
    {
      LPUB8 ss_addr[3] = {0,0,0};

      for (i=0; i<3; i++) {
        if (!find_ss(ws, i, &ss_addr[i]))
          break; /* no more symbolsets */
      }

      status = ws->cgif->pfnCGIFinitRomInfo((unsigned char *)&hq3updt_lj4[0], ss_addr); 
      if (status) {
        HQFAILV(("pfin_ufst5_initialise: UCGIFinitRomInfo() error "
                 "%d\n", status));
        return SW_PFIN_ERROR;
      }
    }
#endif

#if FCO_DISK
    /* If FCO on disk, then put fco files in sw\ufst folder or
    change location below as appropriate */
    strcpy(ws->ConfigBlock.ufstPath, ".\\SW\\ufst\\");
    strcpy(ws->ConfigBlock.typePath, ".\\SW\\ufst\\");
    ws->ConfigBlock.num_files = 5;  /* max open library files */
#endif
    ws->ConfigBlock.bit_map_width = 4;    /* bitmap width 1, 2 or 4 */
#if CHAR_SIZE
    ws->ConfigBlock.cc_buf_size = 2048;
    ws->ConfigBlock.cc_buf_ptr = myalloc(2048);   /* to avoid small allocation churn */
#endif

    status = ws->cgif->pfnCGIFconfig(FSA &ws->ConfigBlock);
    if (status) {
      HQFAILV(("pfin_ufst5_initialise: CGIFconfig() error: "
               "CGIFconfig %d\n", status));
      return SW_PFIN_ERROR;
    }
    status = ws->cgif->pfnCGIFenter(FSA0);
    if (status) {
      HQFAILV(("pfin_ufst5_initialise: CGIFenter() error: "
               "CGIFenter %u\n",status));
      return SW_PFIN_ERROR;
    }

    status = ws->cgif->pfnCGIFfco_Open(ws->MainFCO, &ws->MainFCOHandle);
    if (status) {
      HQFAILV(("pfin_ufst5_initialise: CGIFfco_Open(ps3fco) error: "
               "CGIFfco_Open %u\n",status));
      return SW_PFIN_ERROR;
    }

    status = ws->cgif->pfnCGIFfco_Open(ws->SecondFCO, &ws->SecondFCOHandle);
    if (status) {
      HQFAILV(("pfin_ufst5_initialise: CGIFfco_Open(wingding) error: "
               "CGIFfco_Open %u\n",status));
      return SW_PFIN_ERROR;
    }

    status = ws->cgif->pfnCGIFfco_Open(ws->PluginFCO, &ws->PluginFCOHandle);
    if (status) {
      HQFAILV(("pfin_ufst5_initialise: CGIFfco_Open() error: "
               "CGIFfco_Open %u\n",status));
      return SW_PFIN_ERROR;
    }

    status = ws->cgif->pfnCGIFfco_Plugin(ws->PluginFCOHandle);
    if (status) {
      HQFAILV(("pfin_ufst5_initialise: CGIFfco_Open() error: "
               "CGIFfco_Plugin %u\n",status));
      return SW_PFIN_ERROR;
    }

    /* Store a list of font infos */
    {
      /* Count how many fonts we have in our FCOs, and allocate enough memory */
      int32 n, m;

      if ((n = count_FCO(ws, ws->MainFCO)) < 0 ||
          (m = count_FCO(ws, ws->SecondFCO)) < 0)
        return SW_PFIN_ERROR;  /* UFST_FAIL already called */
      n += m;

      total_fco_fonts = n;
      ws->Fonts = (fco_font *) myalloc(total_fco_fonts * sizeof(fco_font));
    }
    if (ws->Fonts == NULL) {
      HQFAIL("pfin_ufst5_initialise: CGIFfco_Open() error: "
             "failed to allocate fco list");
      return SW_PFIN_ERROR;
    }

    /* load the fonts */
    ws->TotalFonts = 0;
    if ((status = load_FCO(ws, ws->MainFCO, ws->MainFCOHandle,
                           total_fco_fonts)) != 0 ||
        (status = load_FCO(ws, ws->SecondFCO, ws->SecondFCOHandle,
                           total_fco_fonts)) != 0) {
      HQFAILV(("pfin_ufst5_initialise: load_FCO() error: "
               "%u\n", status));
      return SW_PFIN_ERROR;
    }
    if (ws->TotalFonts == 0) {
      HQFAIL("pfin_ufst5_initialise: found no fonts");
      return SW_PFIN_ERROR;
    }

    /* now sort the font list into target device order */
    qsort(ws->Fonts, ws->TotalFonts, sizeof(fco_font), cmp_fco_font);

    /* Establish whether we have both Couriers, and which is preferred.
     *
     * PCL FCOs often have (at least) two Couriers, a standard and a dark
     * variant - it is configurable which will be preferred. We detect here
     * whether both are available, and if so remember where they are in the
     * font list, and whether the darker one has already been favoured by the
     * font map. This ordering can be changed through a config call.
     */
    {
      int i, n, present = 0;

      for (i = 0; i < ws->TotalFonts; i++) {
        int typeface = ws->Fonts[i].pttFontInfo->pcltFontNumber;

        /* Detect the presence of both PCL Couriers, in all styles */
        if (((n = typeface - 93950) >= 0 && n < 4) ||
            ((n = typeface - 4483 + 4) >= 4 && n < 6) ||
            ((n = typeface - 4486 + 6) >= 6 && n < 8) ) {
          present |= 1 << n;
          ws->couriers[n] = i;
        }
      }

      /* If we have four styles of both Couriers, we can switch them */
      ws->bothcouriers = (present == 255);

      /* Is the dark one already dominant? (Note early termination). */
      ws->darkcourier = ws->bothcouriers &&
        (ws->Fonts[ws->couriers[4]].ord < ws->Fonts[ws->couriers[0]].ord);
    }

    /* Update our allocated font number, if too small */
    if (ws->next_ref < ws->TotalFonts + 10)
      ws->next_ref = ws->TotalFonts + 10 ;

  } else {
    /* for resume, there's nothing much to do */
    HQASSERT(ws->suspended, "pfin_ufst5_start: Resume when not suspended");
    ws->suspended = FALSE;
  }

  return SW_PFIN_SUCCESS;
}

/* ========================================================================== */
/* Quit (or suspend) the PFIN module */

sw_pfin_result RIPCALL pfin_ufst5_stop(sw_pfin_instance* instance,
                                       sw_pfin_define_context *context,
                                       sw_pfin_reason reason)
{
  /* Downcast to UFST PFIN instance subclass */
  pfin_ufst5_instance *ws = (pfin_ufst5_instance *)instance;
  int32 i;

  UNUSED_PARAM(sw_pfin_define_context *, context) ;

  /* can't use CHECK_WS, as we may have been suspended already */
  if (ws == 0 || ws->id != WS_ID) {
    HQFAIL("Module not initialised correctly");
    return SW_PFIN_ERROR;
  }

  UFST_SHOW("pfin_ufst5_finalise\n");

#ifdef DEBUG_UFST_CACHE_HITS
  printf("Cache blocks: %i\nCache hits: %i\nOutlines made: %i\nPeak UIDs: %i\n",
         cache_blocks, cache_hits, outlines_made, peak_uids) ;
#endif

  /* do any tidying up, freeing caches etc (for suspend or quit) */

  ws->suspended = TRUE;

  if (ws->pol) {
    /* Discard cached glyph */
    myfree(ws->pol);
    ws->pol = 0;
  }

#ifdef ENHANCED_RESOLUTION
  if (ws->x2) {
    myfree(ws->x2);
    ws->x2 = 0;
    ws->x2size = 0;
  }
#endif

  if (ws->encoding) {
    /* discard UniqueEncoding */
    myfree(ws->encoding);
    ws->encoding = 0;
  }

  /* free our workspace on quit, but not on suspend */
  if (reason == SW_PFIN_REASON_STOP) {
    ws->id = 0;

    if (ws->TotalFonts > 0) {
      for (i=0; i<ws->TotalFonts; i++) {
        if (ws->Fonts[i].pttFontInfo) {
         myfree(ws->Fonts[i].pttFontInfo);
          ws->Fonts[i].pttFontInfo = NULL;
        }
      }
       myfree(ws->Fonts);
      ws->Fonts = NULL;
    }

    if (ws->MainFCOHandle) {
      ws->cgif->pfnCGIFfco_Close(ws->MainFCOHandle);
      ws->MainFCOHandle = 0;
    }

    if (ws->SecondFCOHandle) {
      ws->cgif->pfnCGIFfco_Close(ws->SecondFCOHandle);
      ws->SecondFCOHandle = 0;
    }

    if (ws->PluginFCOHandle) {
      ws->cgif->pfnCGIFfco_Close(ws->PluginFCOHandle);
      ws->PluginFCOHandle = 0;
    }
    ws->TotalFonts = 0;
  }

  return SW_PFIN_SUCCESS;
}

/* ========================================================================== */
/* Structures for holding softloaded fonts, glyphs and symbolsets */

#define HACK 1

typedef struct user_ss {
  struct user_ss * next ;
  int32  id ;              /* symbolset id number (i.e. symbolset number) */
  int32  ref ;             /* external unique reference to this symbolset */
  uint16 first ;
  uint16 last ;            /* valid code range */
  HqBool temp ;
  size_t length ;
  uint8* map ;             /* pointer into data */
  uint8  data[HACK] ;
} user_ss ;

typedef struct user_glyph {
  struct user_glyph * next ;      /* Next glyph in this glyph hash list */
  struct user_glyph * nextbyid ;  /* Next glyph in this id hash list */
  int32  code ;                   /* Character code */
  int32  id ;                     /* Glyph ID (TTs only) */
  size_t length ;                 /* Length of data[] array */
  uint8  data[HACK] ;
} user_glyph ;

/* The number of glyph lists to hang off a font. Must be a power of two */
#define USER_GLYPH_HEADS 32
#define USER_GLYPH_MASK (USER_GLYPH_HEADS-1)

/* The number of glyph id lists. Must be a power of two */
#define USER_ID_HEADS 8
#define USER_ID_MASK (USER_ID_HEADS-1)

typedef struct user_font {
  struct user_font * next ;
  int32  id ;       /* PCL font ID, or datatype of alphanumeric ID */
  int32  ref ;      /* Allocated font number - used for PS name generation */

  int32  proxy ;    /* >-1 for proxied internal font. length will be zero */
  int32  ss ;       /* symbolset for proxied font */
  int32  bold ;     /* XL emboldening of the proxied font :-( */

  int8   temp ;     /* Temporary status - see enumeration below */
  uint8  broken ;   /* Work-around required - this is an enumeration */

  /* The following are extracted from soft font headers */
  uint8  spacing ;    /* PCL5 spacing attribute from soft font header */
  uint8  type ;       /* Soft font type - see enumeration below */
  float  pitch ;      /* Criteria-compatible pitch */
  float  height ;     /* Criteria compatible height */
  float  hmi ;        /* HMI in ems */
  float  offset ;     /* Underline position */
  float  thick ;      /* Underline thickness */
  uint8  chrComp[8] ; /* symbolset chr complement */
  int32  xres ;
  int32  yres ;       /* Bitmap resolution */
  int32  scale ;      /* Scale factor */
  uint32 number ;     /* The font number - for HP4700 emulation */
  uint16 typeface ;   /* The typeface number */
  uint16 style ;      /* The PCL style */
  uint16 fh_ss ;      /* The symbolset from the font header */
  uint8  GC_def_ch ;  /* flag for special processing of default galley char. */
  int8  encoding ;    /* What type of character encoding this font uses. */

  size_t length ;   /* amount of font data - zero if proxied */
  size_t idlen ;    /* amount of string ID at end of data (starts at length) */
  user_glyph * glyphs[USER_GLYPH_HEADS] ;  /* there could be lots of glyphs */
  user_glyph * ids[USER_ID_HEADS] ; /* linked by glyph ID (TTs only) */
  uidcache * uids ;   /* list of UniqueIDs allocated to this font */
  uint8  data[HACK] ; /* fontdata, then string ID if idlen > 0 */
} user_font ;

enum { /* broken enumeration */
  NOT_BROKEN = 0,
  BROKEN_REVUE,        /* Font recognised as Revue Shdw */
  BROKEN_OLDENGLISH,   /* Font is OldEnglish */
  BROKEN_UNCIAL,       /* Font is Uncial */
  BROKEN_DOM_CASUAL,   /* Font is Dom Casual */
  BROKEN_PARK_AVENUE,  /* Font is Park Avenu */
  BROKEN_ITCLUBLNGRPH  /* Font is ITCLUBLNGRPH */
} ;

enum { /* These correspond to font_control action numbers. Don't change them. */
  IS_PROTECTED = -1, /* ROM fonts in PCLEO format cannot be deleted */
  IS_PERMANENT = 0,  /* Soft fonts can be marked "permanent" or "temporary" */
  IS_TEMPORARY = 1   /* Temporary fonts are usually deleted at page end */
} ;

enum { /* Type of soft font. b0 = some kind of bitmap */
  TYPE_PROXY = 0,     /* A proxied font */
  TYPE_BITMAP = 1,    /* Format 0 or 20 */
  TYPE_IF = 2,        /* Format 10 or 11 */
  TYPE_TT = 4,        /* Format 15 - type >= TYPE_TT for TrueTypes in general */
  TYPE_TT16 = 6,      /* Format 16 - type < TYPE_XL for non-XL fonts */
  TYPE_TT16BITMAP = 7,/* Format 16 - bitmap */
  TYPE_XL = 8,        /* Format 16 faked XL truetype */
  TYPE_XLBITMAP = 9   /* Format 16 faked XL bitmap */
} ;

/* font character encoding used */
#define UNKNOWN_CHAR_CODES -1
#define MSL_CHAR_CODES      1
#define UNICODE_CHAR_CODES  3

/* size macros for the above structures */
#define USER_SS_LENGTH(_l) (offsetof(user_ss, data) + (_l))
#define USER_GLYPH_LENGTH(_l) (offsetof(user_glyph, data) + (_l))
#define USER_FONT_LENGTH(_l) (offsetof(user_font, data) + (_l))

/* code number for TrueType components - multiple use */
#define TT_COMPONENT 65535

/* Macro to promote a user_ss, user_font or user_glyph to the head of a list */
#define PROMOTE(_this,_prev,_root,_next) \
if ((_this) && (_prev)) {           \
  (_prev)->_next = (_this)->_next ; \
  (_this)->_next = (_root) ;        \
  (_root)        = (_this) ;        \
}

/* -------------------------------------------------------------------------- */
/* Find a softloaded font by internal reference number */

user_font * finduserfontbyref(pfin_ufst5_instance * ws, int32 ref)
{
  user_font * font = ws->user_fonts ;
  user_font * prev = 0 ;

  while (font && font->ref != ref) {
    prev = font ;
    font = font->next ;
  }

  PROMOTE(font, prev, ws->user_fonts, next) ;

  return font ;
}

/* -------------------------------------------------------------------------- */
/* Callback to PFIN to flush a character or a whole font from the cache
 *
 * If identifier is NULL, the whole font is being discarded. Otherwise, only
 * a single character is being flushed from the cache.
 */

void flush(pfin_ufst5_instance * ws, int32 uid, sw_datum * identifier)
{

  /* Available from SW_PFIN_API_VERSION_20090401
   *
   * Not much we can do if this fails, so ignore the return code - the only
   * likely cause will be lack of memory, which will hit us elsewhere anyway.
   */
  (void) ws->super.callbacks->flush(uid, identifier) ;

  ws->cid = -1;

  return ;
}

/* -------------------------------------------------------------------------- */
/* Discard a uidcache */

void uidfree(pfin_ufst5_instance * ws, uidcache * uid)
{
#if USE_METRICS_CACHE
  metricscache * next = uid->metrics, * metrics ;
  uid->metrics = 0 ;
  /* Discard the metrics cache attached to this uidcache */
  while ((metrics = next) != 0) {
    next = metrics->next ;
    myfree(metrics) ;
  }
#ifdef DEBUG_UFST_CACHE_HITS
  current_uids-- ;
#endif
#endif
  flush(ws, uid->uid, NULL) ;      /* Flush this whole font */
  myfree(uid) ;
}

/* -------------------------------------------------------------------------- */
/* Discard a softloaded font */

void fontfree(pfin_ufst5_instance * ws, user_font * font)
{
  int i ;

  while (font) {
    /* Discard glyphs from the glyph hash */
    for (i = 0; i < USER_GLYPH_HEADS; i++) {
      user_glyph * glyph, * next = font->glyphs[i] ;

      font->glyphs[i] = 0 ;

      while ((glyph = next) != 0) {
        next = glyph->next ;
        myfree(glyph) ;
      }
    }

    /* We don't have to look at the id[] array, because that contains the
     * same glyphs in a different order.
     */
    for (i = 0; i < USER_ID_HEADS; i++)
      font->ids[i] = 0 ;

    /* Discard the UniqueIDs allocated to this font. */
    if (font->uids) {
      uidcache * uid, * next = font->uids ;

      font->uids = 0 ;

      while((uid = next) != 0) {
        next = uid->next ;
        uidfree(ws,uid) ;
      }
    }

    /* Now generate an Event */
    if (font->ref != -1)
      SwEvent(EVENT_PCL_FONT_DELETED, &font->ref, sizeof(int32)) ;

    /* TODO @@@ fixme
     * UFST caches various details of fonts in its own structures, and
     * associates the cached information with the related soft font using the
     * address of the structures used to contain the downloaded soft font data.
     * If a soft font is deleted, UFST needs to be notified in order to clear
     * the cache of information that is no longer needed.
     * Otherwise, it is possible that e.g. galley character configuration
     * for an earlier font is incorrectly associated to a new font.
     * The documentation suggests that the CGIFhdr_font_purge is the
     * appropriate way to do this, but provided little help in actaully
     * using that API.
     * For time being, flush all the buckets of deletion of soft font, and
     * have to suck up any performance penalty.
     * rgg.
     */
    CGIFbucket_purge(1);

    /* Discard the font */
    myfree(font) ;
    font = 0 ;
  }
}

/* -------------------------------------------------------------------------- */
/* Discard a soft-loaded symbolset */

void ssfree(pfin_ufst5_instance * ws, user_ss * ss)
{
  if (ss) {
    user_font * user = ws->user_fonts ;
    int i ;

    /* Go through all our allocated UniqueIDs to find which can be discarded */
    for (i = (user) ? -1 : 0; i < ws->TotalFonts; i++) {
      uidcache ** puid, * uid ;

      if (user) {
        puid = &user->uids ;
        user = user->next ;
        if (user)
          --i ;
      } else {
        fco_font * font = &ws->Fonts[i] ;
        puid = &font->uids ;
      }

      while ((uid = *puid) != 0) {
        if (uid->ssref == ss->ref) {
          *puid = uid->next ;         /* delink this UniqueID */
          uidfree(ws,uid) ;           /* and discard it */
        } else
          puid = &uid->next ;         /* check all uids for the font */
      }
    }

    /* Discard the symbolset */
    myfree(ss) ;
  }
}

/* -------------------------------------------------------------------------- */
/* Find a softloaded font by PCL integer ID or alphanumeric ID */

user_font * finduserfont(int32 id, sw_datum * idstr, user_font *** ppp)
{
  user_font * font, **ptr = *ppp ;
  size_t idlen = 0 ;

  /* If an XL string ID, "id" is the datatype and idlen the byte length */
  if (idstr && idstr->type == SW_DATUM_TYPE_STRING)
    idlen = idstr->length ;

  if (idstr) {
    /* this is an XL font, id is datatype of fontname */
    while ((font = *ptr) != 0) {
      if (font->id == id && font->idlen == idlen &&
          memcmp(idstr->value.string, font->data + font->length, idlen) == 0)
        break ;
      ptr = &font->next ;
    }
  } else {
    /* this is a PCL font, id is the only identifier */
    while ((font = *ptr) != 0 && (font->id != id || font->idlen != 0))
      ptr = &font->next ;
  }

  *ppp = ptr ;
  return font ;
}

user_font * finduserfont_with_symbols(pfin_ufst5_instance *ws,
                                      int32 id, int32 ss, sw_datum * idstr,
                                      user_font *** ppp)
{
  user_font *font ;
  int32 symbol_set ;
  uint8 chrReq[8] ;

  font = finduserfont(id, idstr, ppp) ;
  if (font != NULL) {
    if (font->encoding == UNKNOWN_CHAR_CODES)
      return font ;

    symbol_set = get_symbolset_char_requirements(ws, ss, &chrReq[0], font->encoding) ;

    if ( supports_all_symbol_sets(&chrReq[0], &font->chrComp[0], font->encoding) )
      return font ;
  }

  return NULL ;
}

/* -------------------------------------------------------------------------- */
/* Find a softloaded glyph by character code and user_font

   Note. Cannot find TrueType components, because they all have the same code.
*/

user_glyph * finduserglyph(user_font * font, int32 code)
{
  user_glyph * glyph ;
  user_glyph * prev = 0 ;

  if (font == 0 || code == TT_COMPONENT)
    return NULL ;

  glyph = font->glyphs[code & USER_GLYPH_MASK] ;

  while (glyph && glyph->code != code) {
    prev  = glyph ;
    glyph = glyph->next ;
  }

  PROMOTE(glyph, prev, font->glyphs[code & USER_GLYPH_MASK], next) ;

  return glyph ;
}

/* -------------------------------------------------------------------------- */
/* Find a softloaded glyph by glyph id and user_font */

user_glyph * finduserglyphbyid(user_font * font, int32 id)
{
  user_glyph * glyph ;
  user_glyph * prev = 0 ;

  if (font == 0)
    return NULL ;

  glyph = font->ids[id & USER_ID_MASK] ;

  while (glyph && glyph->id != id) {
    prev  = glyph ;
    glyph = glyph->nextbyid ;
  }

  PROMOTE(glyph, prev, font->ids[id & USER_ID_MASK], nextbyid) ;

  return glyph ;
}

/* -------------------------------------------------------------------------- */
/* Delete a glyph that has already been delinked from a font */

void glyphfree(pfin_ufst5_instance * ws, user_font * font, user_glyph * glyph)
{
  user_glyph ** ptr ;

  if (font == 0 || glyph == 0)
    return ;

  /* Remove from the glyph hash */
  ptr = &font->glyphs[glyph->code & USER_GLYPH_MASK] ;
  while (*ptr && *ptr != glyph)
    ptr = &(*ptr)->next ;
  if (*ptr && *ptr == glyph)
    *ptr = glyph->next ;

  /* And from the glyphID hash */
  if (glyph->id > -1) { /* Remove it from the glyphid hash too */
    ptr = &font->ids[glyph->id & USER_ID_MASK] ;
    while (*ptr && *ptr != glyph)
      ptr = &(*ptr)->nextbyid ;
    if (*ptr && *ptr == glyph)
      *ptr = glyph->nextbyid ;
  }

  /* Discard this glyph from the fontcache */
  if (font->uids && glyph->code != TT_COMPONENT) {
    uidcache * uid = font->uids ;
    if (ws->encoding == 0)
      (void) unique_encoding(ws) ;
    while (uid) {
      flush(ws, uid->uid, &ws->encoding[glyph->code + 1]) ;
#if USE_METRICS_CACHE
      /* Discard the glyph's width from the cache. This is easy enough for bound
       * fonts but for unbound and especially TrueTypes composite glyphs and
       * galley substitution makes it hard. Under these circumstances we have to
       * discard all cached metrics. This is still better than no cache at all!
       * As this is such a rare feature, don't bother making it complicated...
       */
      {
        metricscache * next = uid->metrics, * metrics ;

        uid->metrics = 0 ;   /* Note: discarding ALL cached metrics :-( */
        uid->missing = FALSE ;
        while ((metrics = next) != 0) {
          next = metrics->next ;
          myfree(metrics) ;  /* for bound fonts we could invalidate one width */
        }
      }
#endif
      uid = uid->next ;
    }
  }

  myfree(glyph) ;
}
/* -------------------------------------------------------------------------- */
/* Find a user-defined symbolset by symbolset reference.
 *
 * This is used to map a PFID SS reference to actual symbolset data.
 * Note that the PFID ss number is an ssList[] index or a reference number.
 * List is maintained in Most Recently Used order.
 */

user_ss * finduserssbyref(pfin_ufst5_instance * ws, int32 ref)
{
  user_ss * sset = ws->user_symbolsets ;
  user_ss * prev = 0 ;

  /* find ss and parent */
  while (sset && sset->ref != ref) {
    prev = sset ;
    sset = sset->next ;
  }

  PROMOTE(sset, prev, ws->user_symbolsets, next) ;

  return sset ;
}

/* -------------------------------------------------------------------------- */
/* Find a user-defined symbolset by PCL symbolset number.
 *
 * This is used when defining a symbolset to find an existing definition.
 * As such, there's no point reordering the list - if found, the symbolset
 * will get deleted anyway.
 */

user_ss * finduserss(int32 id, user_ss *** ppp)
{
  user_ss * sset, **ptr = *ppp ;

  while ((sset = *ptr) != 0 && (sset->id != id))
    ptr = &sset->next ;

  *ppp = ptr ;
  return sset ;
}

/* ========================================================================== */
/* \brief A version of StandardEncoding with unique names for all code points.
 *
 * We need the Encoding as a sw_datum array. Building it as one takes up quite a
 * lot of ROM (257*sizeof(sw_datum) + 105 ".XX" strings = 4.5KB, to say nothing
 * of string constant termination and alignment). Instead, we store the real
 * glyphnames tersely here (pascal style), with bytes>128 meaning "skip n-128
 * codepoints". Skipped codepoints get hex names of the form ".XX" constructed
 * automatically. As long as the code that handles this is less than 4KB, we
 * win (it's actually under 900 bytes, unoptimised!). The code assumes a
 * maximum of 110 constructed hex names - if this is insufficient the code will
 * have to be updated, or converted to a two-pass approach to count the number
 * required.
 *
 * The expanded encoding must be built in RAM of course, and currently is
 * cached for reuse. However, it is discarded on a SUSPEND (in low memory
 * situations) and will be recreated when next required. For devices with low
 * resources this caching can be disabled easily with very little performance
 * impact.
 *
 * Note that some compilers support more than two digit hex escapes, so
 * concatenation is used to prevent ambiguities. If your compiler changes the
 * byte values in this string it will have to be converted to an aggregate
 * array initialiser.
 */

static unsigned char glyphlist[] =
  "\x07.notdef\x9F\x05space\x06""exclam\x08quotedbl\x0Anumbersign\x06""dollar"
  "\x07percent\x09""ampersand\x0Aquoteright\x09parenleft\x0Aparenright"
  "\x08""asterisk\x04plus\x05""comma\x06hyphen\x06period\x05slash\x04zero"
  "\x03one\x03two\x05three\x04""four\x04""five\x03six\x05seven\x05""eight"
  "\x04nine\x05""colon\x09semicolon\x04less\x05""equal\x07greater\x08question"
  "\x02""at\x01""A\x01""B\x01""C\x01""D\x01""E\x01""F\x01G\x01H\x01I\x01J\x01K"
  "\x01L\x01M\x01N\x01O\x01P\x01Q\x01R\x01S\x01T\x01U\x01V\x01W\x01X\x01Y\x01Z"
  "\x0B""bracketleft\x09""backslash\x0C""bracketright\x0B""asciicircum"
  "\x0Aunderscore\x09quoteleft\x01""a\x01""b\x01""c\x01""d\x01""e\x01""f\x01g"
  "\x01h\x01i\x01j\x01k\x01l\x01m\x01n\x01o\x01p\x01q\x01r\x01s\x01t\x01u\x01v"
  "\x01w\x01x\x01y\x01z\x09""braceleft\x03""bar\x0A""braceright\x0A""asciitilde"
  "\xA2\x0A""exclamdown\x04""cent\x08sterling\x08""fraction\x03yen"
  "\x06""florin\x07section\x08""currency\x0Bquotesingle\x0Cquotedblleft"
  "\x0Dguillemotleft\x0Dguilsinglleft\x0Eguilsinglright\x02""fi\x02""fl\x81"
  "\x06""endash\x06""dagger\x09""daggerdbl\x0Eperiodcentered\x81\x09paragraph"
  "\x06""bullet\x0Equotesinglbase\x0Cquotedblbase\x0Dquotedblright"
  "\x0Eguillemotright\x08""ellipsis\x0Bperthousand\x81\x0Cquestiondown\x81"
  "\x05grave\x05""acute\x0A""circumflex\x05tilde\x06macron\x05""breve"
  "\x09""dotaccent\x08""dieresis\x81\x04ring\x07""cedilla\x81\x0Chungarumlaut"
  "\x06ogonek\x05""caron\x06""emdash\x90\x02""AE\x81\x0Bordfeminine\x84"
  "\x06Lslash\x06Oslash\x02OE\x0Cordmasculine\x85\x02""ae\x83\x08""dotlessi"
  "\x82\x06lslash\x06oslash\x02oe\x0Agermandbls\x84"
#ifdef SANITY_CHECK
  "!" /* this is a sanity check, see below */
#endif
;

/* \brief Build the encoding sw_datum array, from the terse glyphlist data. */
sw_datum * unique_encoding(pfin_ufst5_instance * ws)
{
  unsigned char * glyph;
  char * text;
  int32 i, c;
  sw_datum* datum, zero = SW_DATUM_NOTHING;

  HQASSERT(ws, "Bad workspace");

  /* built already? */
  if (ws->encoding)
    return ws->encoding;

  /* get enough memory for encoding array and up to 110 hex names */
  ws->encoding = (sw_datum *) myalloc(257*sizeof(sw_datum) + 110*3);
  if (ws->encoding == 0) {
    HQFAIL("Out of memory when creating UniqueEncoding");
    return 0;
  }

  glyph = ws->pglyphlist;
  datum = ws->encoding;
  text = (char*)(datum + 257);  /* hex names go after encoding array */

  /* make array sw_datum */
  *datum = zero ;
  datum->type = SW_DATUM_TYPE_ARRAY;
  datum->owner = 0;
  datum->length = 256;
  datum->value.opaque = datum+1;

  /* now make 256 sw_datum strings from the glyphlist */
  for (i=0, c=0; i<256; i++) {
    /* if we're not already skipping codepoints, get the next glyph token */
    if (c < 129)
      c = *glyph++;

#ifdef SANITY_CHECK
    if (c == '!')
      HQFAIL("Mistake in UniqueEncoding or glyphlist (1)");
#endif

    *(++datum) = zero ;
    datum->type = SW_DATUM_TYPE_STRING;
    datum->subtype = SW_DATUM_SUBTYPE_NAME_STRING ;

    if (c < 129) {
      /* glyphname as pascal string */
      datum->length = c;
      datum->value.string = (char*) glyph;
      glyph += c;
    } else {
      /* skip 'c-128' codepoints, creating hex glyphnames */
      static char hexDigit[] = "0123456789ABCDEF";
      datum->length = 3;
      datum->value.string = text;
      *text++ = '.';
      *text++ = hexDigit[i >> 4];
      *text++ = hexDigit[i & 15];
      --c; /* reuse the enclosing 'i' loop to skip multiple codepoints */
    }
  }

#ifdef SANITY_CHECK
  if (*glyph != '!')
    HQFAIL("Mistake in UniqueEncoding or glyphlist (2)");
#endif

  return ws->encoding;
}

/* TODO: This must be changed to a simpler array search, as the font may have
 * been re-encoded in PostScript. This can't happen to PCL fonts of course. */

/* Mapping back from glyphname to codepoint is easy - the hex forms are easily
 * spotted, and otherwise the above data is ideally suited for a simple linear
 * traversal. Note that we therefore don't need the expanded sw_datum Encoding
 * array.
 */

signed int codepoint_from_glyphname(pfin_ufst5_instance * ws,
                                    const sw_datum * glyphname)
{
  unsigned char * glyph;
  signed int i, c;

  if (glyphname == 0 || glyphname->type != SW_DATUM_TYPE_STRING)
    return -1;

  /* Each glyphname is referenced three times, so a simple cache is effective */
  if (ws->glyphname && /* glyphname points PAST the length byte */
      glyphname->length == (unsigned int) ws->glyphname[-1] &&
      memcmp(ws->glyphname, glyphname->value.string, glyphname->length) == 0)
    return ws->codepoint; /* the same as last time */

  /* Spot our constructed names (and any of the same format - oh well) */
  if (glyphname->length == 3 && glyphname->value.string[0] == '.') {
    /* probably a constructed hex name */
    i = glyphname->value.string[1];
    c = glyphname->value.string[2];
    i = (i <= '9') ? i - '0' :
        (i < 'A' || i > 'F') ? -1 :
        i - 'A' + 10 ;
    c = (c <= '9') ? c - '0' :
        (c < 'A' || c > 'F') ? -1 :
        c - 'A' + 10 ;
    if (i > -1 && c > -1)
      return (i << 4) + c;
  }

  /* Search through the glyph list, caching the result */
  for (i=0, glyph = ws->pglyphlist; i<256; i++) {
    c = *glyph++;

    if (c < 129) {
      /* glyph name in pascal format */
      if (glyphname->length == (unsigned int) c &&
          memcmp(glyph, glyphname->value.string, c) == 0) {
        ws->codepoint = i;
        ws->glyphname = glyph; /* note that length will be glyph[-1] */
        return i;
      }
      glyph += c;
    } else {
      /* skip 'c-128' code points */
      i += c - 129; /* yes, 129 (because of i++ above) */
    }
  }

  return -1;
}

/* -------------------------------------------------------------------------- */
/* Extract the font ID and symbolset from the font name

   Font names of the following form are detected:

      PCL:<fontid>:<symbolset>[i]
      PCL:<fontid>:<symbolset>:<XLemboldening>[i]
      PCL!<fontid>:<symbolset>:<font_size>[i]

   The PCL! prefix indicates a bitmap font that can be scaled according to
   the a font matrix. The font size element indicates the
   'intrinisic' size of the font and is used if the font definition has no
   notion of this size (e.g. PCLXL bitmap fonts).

   For softloaded fonts, fontid will be > ws->TotalFonts.
*/

int parse_fontname(pfin_ufst5_instance * ws, const sw_datum* fontname,
                   int32* id, int32* ss, int32* ssref, int32* bold, int32* faux,
                   int32 *height)
{
  int32 typeface = 0, symbolset = 0, fauxitalic = 0, symbolref = -1 ;
  uint32 i = 4 ;
  const char * t ;

  UNUSED_PARAM(pfin_ufst5_instance *, ws) ;

  if (fontname == 0 || fontname->type != SW_DATUM_TYPE_STRING ||
      fontname->length < 7)
    return FALSE ;

  t = fontname->value.string ;

  if (t[0] != 'P' || t[1] != 'C' || t[2] != 'L' || (t[3] != ':' && t[3] != '!'))
    return FALSE ;

  /* Now read in the internal typeface number and symbolset number */

  while (i < fontname->length && t[i] >= '0' &&
         t[i] <= '9')
    typeface = typeface * 10 + t[i++] - '0' ;

  if (i < fontname->length && t[i++] == ':') {
    PCL_SSINFO * ssList = ws->psslist ;
    int32 embolden = -1 ;
    int32 font_height = -1;

    while (i < fontname->length && t[i] >= '0' &&
           t[i] <= '9' && symbolset < 65536)
      symbolset = symbolset * 10 + t[i++] - '0' ;

    if (symbolset > (int32)ws->sslistlen) {
      /* user symbolset, reference number */
      user_ss * userss = finduserssbyref(ws, symbolset) ;
      if (userss) {
        symbolset = userss->id ;
        symbolref = userss->ref ;
      }
    }
    if (symbolref == -1) {
      symbolref = (symbolset < 0) ? ws->defaultss : symbolset ;
      symbolset = ssList[symbolref].ss ;
    }

    if (i < fontname->length && t[i] == ':') {
      int32 value = 0;
      HqBool sign_neg = FALSE;
      /* Next number is either an override of font height or emboldening
       * metric. */
      i++ ;
      if ( t[i] == '-' ) {
        sign_neg = TRUE;
        i++;
      }
      while (i < fontname->length && t[i] >= '0' &&
             t[i] <= '9' && value < 65536)
        value = value * 10 + t[i++] - '0' ;

      if (sign_neg) value = -value;

      if ( t[3] == '!' ) {
        font_height = value;
      } else {
        /* Optional PCLXL font emboldening metric is present */
        embolden = value;

        if (i < fontname->length && t[i] == 'i') {
          /* Optional faux italic is present */
          fauxitalic = 1 ;
          i++ ;
        }
      }
    }

    if (i == fontname->length) {
      /* found a font name of the correct format, and have typeface and
       * symbolset */
      UFST_SHOW(("pfin_ufst5_find: Recognised PCL font name syntax\n"));
      *id = typeface ;
      *ss = symbolset ;
      if (ssref)
        *ssref = symbolref ;
      *faux = fauxitalic ;
      if (embolden > -1)
        *bold = embolden ;
      if (height)
        *height = font_height;
      return TRUE ;
    } /* correct format */
  }

  return FALSE ;
}

/* ========================================================================== */
/* Find index into sslist[] or user symbolset reference number given a PCL
 * symbolset number.
 *
 * This reference number can then be used to find the ufst number by indexing
 * into ssList[], or by negating for user fonts.
 */

int find_symbolset(pfin_ufst5_instance * ws, int32 symbolset, user_ss ** ppss)
{
  /* First check the user symbolsets */
  {
    user_ss * user = ws->user_symbolsets ;

    while (user && user->id != symbolset)
      user = user->next ;

    if (user) {
      if (ppss)
        *ppss = user ;
      return user->ref ;               /* will be > ws->sslistlen */
    }
  }

  /* Now check the built-in ones */
  {
    int a = 0, b = (int) ws->sslistlen, m ;
    PCL_SSINFO * ssList = ws->psslist ;

    while (b > a) {
      m = (a+b)/2 ;                    /* binary search */
      if (ssList[m].ss == symbolset)
        return m ;                     /* found it */
      if (ssList[m].ss < symbolset)
        a = m+1 ;
      else
        b = m ;
    }
  }

  return ws->defaultss ;
}

/* ========================================================================== */
/* Define a font by id (soft font ref or hard font index), symbolset (or softss
 * ref), with optional emboldening and faux italic.
 */
int define(pfin_ufst5_instance * ws, sw_pfin_define_context *context,
           int32 id, int32 ss, int32 bold, int32 faux, const sw_datum* findname,
           int32 override_height)
{
  static sw_datum pfid[] = {
    SW_DATUM_ARRAY(pfid+1, 3),  /* the unemboldened (PCL5) form is assumed */
    SW_DATUM_INTEGER(0),
    SW_DATUM_INTEGER(0),
    SW_DATUM_INTEGER(0),        /* overridden height */
    SW_DATUM_INTEGER(0),        /* ...but there's room for the emboldening */
    SW_DATUM_INTEGER(0)};       /* and italicisation. */
  sw_datum * uniqueEnc = unique_encoding(ws);
  PCL_SSINFO * ssList = ws->psslist ;
  int32 ssref ;
  uidcache ** head = 0 ;

  /* UFST does not use PCL symbolset numbers, so search for it in the symbolset
   * list, and extract the ufst number from that list. Alternatively, if a user
   * symbolset, use the unique reference, made negative.
   */
  ss = ssref = find_symbolset(ws, ss, NULL) ;   /* returns -1 if not found */
  if (ss >= 0)
    ss = (ss < (int32)ws->sslistlen) ? (int32) ssList[ss].ufst : -ss ;

  /* PFID will be an array of three numbers: The index into our font list, the
   * internal UFST symbolset number and the 'intrinsic' font height. The font
   * height is -1 for fonts where the font definition data defines the height
   * (it is needed for PCLXL bitmap fonts).
   * Note that this is slightly different from
   * the font name, which contains the font index and PCL symbolset number.
   * If emboldened by XL or faking italic for PCL5, the PFID is longer.
   */

  UFST_SHOW(("pfin_ufst5_find: Defining font\n"));

  pfid[1].value.integer = id;
  pfid[2].value.integer = ss;
  pfid[3].value.integer = override_height;
  if (bold > 0 || faux != 0) {
    pfid[0].length = 5 ;
    pfid[4].value.integer = bold ;
    pfid[5].value.integer = faux ;
  } else
    pfid[0].length = 3 ;

  /* Find the font's UniqueID cache */
  if (id < ws->TotalFonts && id >= 0) {
    head = &ws->Fonts[id].uids ;
  } else {
    user_font * font = finduserfontbyref(ws, id) ;
    if (font)
      head = &font->uids ;
  }

  if (head) {
    uidcache * uid = *head, * prev = 0 ;
    /* UniqueID caching/reuse to be performed */

    while (uid &&
           (uid->ssref != ssref || uid->bold != bold || uid->faux != faux)) {
      prev = uid ;
      uid = uid->next ;
    }

    if (uid) {
      /* Found a previously allocated UniqueID, promote it. */
      PROMOTE(uid, prev, *head, next) ;
    } else {
      /* New font, so allocate a new cache and get the UniqueID */
      uid = myalloc(sizeof(uidcache)) ;
      uid->next  = *head ;
      uid->uid   = -1 ;        /* causes uid() call to READ the UniqueID */
      uid->ssref = ssref ;
      uid->bold  = bold ;
      uid->faux  = faux ;
#if USE_METRICS_CACHE
      uid->metrics = NULL ;
      uid->missing = FALSE ;
#endif
      *head = uid ;
#ifdef DEBUG_UFST_CACHE_HITS
      current_uids++ ;
      if (current_uids > peak_uids)
        peak_uids = current_uids ;
#endif
    }

    if (ws->super.callbacks->uid(context, &uid->uid) != SW_PFIN_SUCCESS)
      return FALSE ;
  }

  if (ws->super.callbacks->define(context, findname, pfid, uniqueEnc) !=
      SW_PFIN_SUCCESS)
    return FALSE ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* findresource or resourceforall.
*
* If a PFIN module defines all its fonts at initialise or config, then find
* isn't required, but this example responds to "PCL:###:###" format names and
* also matches the defined PostScript names of the FCO fonts.
*/

/* Map fonts to appropriate symbol sets. If symbol set not defined for a font
 * here, then code needs to define an default. This code does not explicitly
 * know which symbol sets are actually defined, so this info would be bette
 * provided by the skin.
 */

static uint8 symbolFonts[] = /* This can eventually be an RDR */
    "\x02\x6D"               /* Symbolset number, bigendian : Symbol */
    "\x05""Carta"            /* Names as Pascal strings */
    "\x06""Candid"
    "\x06Symbol"
    "\x0cZapfDingbats"
    "\x15HoeflerText-Ornaments"
    "\x08SymbolMT"
    "\x00"                   /* End of names for Symbol SS */
    "\x48\x6C"               /* Symbolset number, bigendian : Wingdings */
    "\x11Wingdings-Regular"
    "\x00"                   /* End of name for Wingdings SS. */
    "\x00\x00" ;             /* No more symbolsets */

/* Deduce an appropripriate symbol set value based on fontname.
 * If the fontname is one that is recognized from the SymbolFont list, ss
 * paramters is adjusted as per that list.
 * If fontname not recognized, the symbol set is not changed. *ss should be
 * set to appropriate default before calling this function.
 */


static void search_symbol_set(const sw_datum *findname, int32 *ss)
{
  uint8 * p = symbolFonts ;
  size_t  len = 0;
  int     chg ;

  /* Search for appropriate symbol set. */

  while (len == 0 && (chg = (p[0]<<8|p[1])) != 0) {  /* For each symbolset */
    p += 2 ;
    do {                                /* For each name in the group */
      len = *p++ ;
      if (len == findname->length
          && memcmp(findname->value.string, p, len) == 0) {
        *ss = chg ;                  /* matched the name, so change ss */
        break ;                              /* exit do{} with len!=0 */
      }
      p += len ;
    } while (len) ;                          /* exit do{} with len==0 */
  }
}

sw_pfin_result RIPCALL pfin_ufst5_find(sw_pfin_instance * instance,
                                       sw_pfin_define_context *context,
                                       const sw_datum* findname)
{
  /* Downcast to UFST PFIN instance subclass */
  pfin_ufst5_instance *ws = (pfin_ufst5_instance *)instance;
  int32 id = -1, ss, bold = -1, faux = 0 ;
  int32 override_height = -1;
  PCL_SSINFO * ssList ;

  CHECK_WS(ws);

  /* If we don't recognise the symbolset number (the PCL font name may have
   * been faked in PostScript!) then use the default. */
  ssList = ws->psslist ;
  ss = ssList[ws->defaultss].ss;

  if (findname->type == SW_DATUM_TYPE_STRING) {   /* Find a particular font */

    UFST_SHOW(("pfin_ufst5_find %s\n"), findname->value.string);

    /* Is this a PCL:#:# format fontname? */
    (void) parse_fontname(ws, findname, &id, &ss, NULL, &bold, &faux,
                          &override_height) ;

    if (id == -1) {
      /* else, try to match the PostScript font name */
      int32 i ;
      for ( i = 0; i < ws->TotalFonts; i++) {
        size_t psnamelen;

        psnamelen = ws->Fonts[i].psnamelen;

        if ( psnamelen == findname->length &&
             memcmp(FONTNAME(i), findname->value.string, findname->length) == 0 )
        {
          UFST_SHOW(("pfin_ufst5_find: Recognised PS font name\n"));

          id = i ;
          search_symbol_set(findname, &ss);
          break;
        }
      }
    }

    if (id >= 0 &&
        !define(ws, context, id, ss, bold, faux, findname, override_height))
      return SW_PFIN_ERROR ;

  } else {                               /* resourceforall is about to happen */
    UFST_SHOW(("pfin_ufst5_find\n"));

    if (ws->listpsfonts) {
      for (id = 0; id < ws->TotalFonts; id++) {
        if (ws->Fonts[id].psnamelen) {
          sw_datum name = SW_DATUM_STRING("") ;
          name.length = ws->Fonts[id].psnamelen ;
          name.value.string = FONTNAME(id) ;
          ss = ssList[ws->defaultss].ss;
          search_symbol_set(&name, &ss);
          if (!define(ws, context, id, ss, 0, 0, &name, -1))
            return SW_PFIN_ERROR ;
        }
      }
    }
  }

  /* return success whether we defined the font or not */
  return SW_PFIN_SUCCESS;
}

/* ========================================================================== */
/* miscop helpers */

int get_int(const sw_data_api* api, sw_datum* param, int32 i, int32* val)
{
  sw_datum datum;
  HQASSERT(api != 0 && param != 0, "Bad get_int");
  if (api->get_indexed(param,i,&datum) != SW_DATA_OK ||
      datum.type != SW_DATUM_TYPE_INTEGER)
    return FALSE;
  *val = datum.value.integer;
  return TRUE;
}

int get_bool(const sw_data_api* api, sw_datum* param, int32 i, int32* val)
{
  sw_datum datum;
  HQASSERT(api != 0 && param != 0, "Bad get_int");
  if (api->get_indexed(param,i,&datum) != SW_DATA_OK ||
      datum.type != SW_DATUM_TYPE_BOOLEAN)
    return FALSE;
  *val = datum.value.boolean;
  return TRUE;
}

int get_flt(const sw_data_api* api, sw_datum* param, int32 i, float* val)
{
  sw_datum datum;
  HQASSERT(api != 0 && param != 0, "Bad get_flt");
  (void) api->get_indexed(param,i,&datum); /* rely on 'returned' type */
  switch (datum.type) {
    case SW_DATUM_TYPE_FLOAT:
      *val = datum.value.real;
      break;
    case SW_DATUM_TYPE_INTEGER:
      *val = (float) datum.value.integer;
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

/* -------------------------------------------------------------------------- */
/* Whether to rasterise pre-transformed bitimages */

#define USE_FONT_MATRIX 1

/* Make a glyph of the currently predicted type, if we don't have it already */

sw_pfin_result make_char(pfin_ufst5_instance *ws, const sw_pfin_font *pfinfont,
                         int32 fid, int32 cid, int32 ss, int32 bold, int32 faux,
                         int spacesOK, int32 wmode, int32 override_height)
{
  FONTCONTEXT fontcontext = {0};
  PIFOUTLINE  pol = 0;
  SL32        FontSize[CHAR_SIZE_ARRAYSIZE], scaleFactor, font_id;
  SL32        factorX, factorY, factorA, factorB;
  UW16        format = FC_INCHES_TYPE;
  int         status;
  HqBool      bitmap, fakeSpace = FALSE, is_user_font = FALSE,
              is_user_bound_font = FALSE;
  float       scale, height, hmi;
  float       xres_scale = 1.0f, yres_scale = 1.0f;

  if (ws == 0 || fid < 0 || cid < 0 || bold < 0)
    return SW_PFIN_ERROR_SYNTAX;

#if GG_CUSTOM
    /* This is a precaution - multiple instantiation would get even more
     * broken otherwise, but once the UFST_REENTRANT switch is employed the
     * if_state will become part of ws, and a link back won't be needed. */
    if_state.mem_context = (void *) ws ;
#endif

  bitmap = ws->make_bitmaps;

  /* already have this cached? */
  if (ws->pol && ws->fid == fid && ws->cid == cid && ws->wmode == wmode &&
      ws->ss == ss && ws->bold == bold && ws->bitmap == bitmap &&
      (bitmap == 0 || pfinfont == 0 || (
#if USE_FONT_MATRIX
        memcmp(ws->matrix, pfinfont->matrix, sizeof(ws->matrix)) == 0 &&
#endif
        ws->dx == pfinfont->dx && ws->dy == pfinfont->dy &&
        ws->xres == pfinfont->xdpi && ws->yres == pfinfont->ydpi))) {
    /* same glyph again, so return cached result */

#if USE_FONT_MATRIX
    /* If glyph was pretransformed, inform PFIN if there's an outline context */
    if (ws->transformed && ws->context)
      (void) ws->super.callbacks->options(ws->context, PFIN_OPTION_TRANSFORMED,
                                                       PFIN_OPTION_TRANSFORMED) ;
#endif

    if (ws->context && ws->orientation_opts)
      (void) ws->super.callbacks->options(ws->context,
                  PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE
                  | PFIN_OPTION_BITMAP_ORIENTATION_REVERSE,
                  ws->orientation_opts);

    return SW_PFIN_SUCCESS;
  }

  /* discard it otherwise */
  if (ws->pol) {
    myfree(ws->pol);
    ws->pol = 0;
  }

#if 0
  /* Recurse through proxy fonts */
  if (fid >= ws->TotalFonts) { /* User font */
    user = finduserfontbyref(ws, fid);
    while (user && user->proxy >= ws->TotalFonts)
      user = finduserfontbyref(ws, user->proxy);
    if (user && user->proxy > -1)
      fid = user->proxy ;
  }
#endif

  /* An unfortunate UFST 'feature' causes all glyphs processed after an XL
   * bitmap to come out far too small.  Also, vertical text mode is not
   * always properly reset.  This is the workaround.
   */
  if_state.comp_pix_context.masterPt = 8 ;
  if_state.chIdptr = &cid ;
  if_state.CharVertWrit = 0 ;

  get_id_null = FALSE;

  if (fid < ws->TotalFonts) { /* FCO font */
    fco_font* font = &ws->Fonts[fid];
    TTFONTINFOTYPE* fontInfo = font->pttFontInfo;

    hmi    = font->hmi;
    scale  = font->scale;
    height = font->height;
    scaleFactor = fontInfo->scaleFactor;

    fontcontext.font_hdr   = 0;
    fontcontext.ExtndFlags = 0;

    font_id = font->nFid;
    format |= FC_FCO_TYPE;
    get_id_font = NULL;
    ws->orientation_opts = 0;
  } else { /* user font */
    font_header * header ;
    uint8 bound ;

    is_user_font = TRUE ;
    get_id_ws   = ws;
    get_id_font = finduserfontbyref(ws, fid);
    if (!get_id_font)
      return SW_PFIN_ERROR_UNKNOWN;

    /* Proxies should already have been resolved to a real font */
    HQASSERT(get_id_font->proxy == -1, "make_char given proxy font") ;
    if (get_id_font->proxy != -1)
      return SW_PFIN_ERROR_UNKNOWN;

    header = (font_header *)get_id_font->data ;

    /* translate the header->orientation into appropriate PFIN options flags
     * header orientation = 1 -> landscape, 2 -> reverse portrait,
     * 3->reverse landscape.
     */
    switch (header->orient) {
      case 1:
        ws->orientation_opts = PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE;
        break;
      case 2:
        ws->orientation_opts = PFIN_OPTION_BITMAP_ORIENTATION_REVERSE;
        break;
      case 3:
        ws->orientation_opts = PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE
                              | PFIN_OPTION_BITMAP_ORIENTATION_REVERSE;
        break;
      default:
        ws->orientation_opts = 0;
        break;
    }

    if (ws->context)
      (void) ws->super.callbacks->options(ws->context,
                  PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE
                  | PFIN_OPTION_BITMAP_ORIENTATION_REVERSE,
                  ws->orientation_opts);

    hmi    = get_id_font->hmi ;
    height = 1.0f ;
    scale  = 1.0f ;

    fontcontext.font_hdr   = get_id_font->data;
    fontcontext.ExtndFlags = 0;

    bound = get_id_font->data[typeO] ;
    if (bound <= BOUNDFONTTYPE &&
        (header->format == 0 || header->format == 20)) {
      is_user_bound_font = TRUE ;
      fontcontext.ssnum = DL_SYMSET ; /*set to disable plugin support */
      /* the following line causes a conflict with the above line , and causes the FCO/plugin to be searched for missing glyphs */
  /*     fontcontext.ExtndFlags |= EF_NOSYMSETMAP ;*/
    }

    /* Switch on contour checking for emboldened TrueTypes */
    if (bold > 0 && get_id_font->type >= TYPE_TT &&
        (get_id_font->type & TYPE_BITMAP) == 0)
      fontcontext.ExtndFlags |= EF_CONTCHK ;

    font_id = 0;
    scaleFactor = get_id_font->scale ;

    switch (get_id_font->type) {
    case TYPE_TT16BITMAP:
      fontcontext.ExtndFlags |= EF_XLFONT_TYPE ; /* fall through */
      /* TT16BITMAP need this flag to be treated as bitmaps. Lying about the
       * font type is presumably preferable to altering the header data that
       * is downloaded - eg forging a type 20, or type 0, header.
       */
    case TYPE_TT16:
      format |= FC_TT_TYPE | FC_NON_Z_WIND | FC_EXTERN_TYPE ;
      fontcontext.ExtndFlags |= EF_FORMAT16_TYPE ;
      break ;
    case TYPE_TT:
      format |= FC_TT_TYPE | FC_NON_Z_WIND | FC_EXTERN_TYPE ;
      break ;
    case TYPE_XLBITMAP:
      /* XL bitmap fonts are not defined with an intrinsic height. If we
       * want to scale the bitmaps, then we rely on obtaining the font height
       * from the PDL ( height before any page scaling etc. )
       * The height is defined in units of 1/100th of a point.
       * It is also possible to scale other bitmap fonts by using the value of
       * get_id_font->height, where that is defined. */
      if ( override_height > 0 ) {
        yres_scale = (float) (override_height /
                      ( 7200.0 / ( pfinfont->dy * pfinfont->ydpi ))) ;
        xres_scale = (float) (override_height /
                      ( 7200.0 / ( pfinfont->dx * pfinfont->xdpi ))) ;
      }
      /* fall through */
    case TYPE_XL:
      format |= FC_TT_TYPE | FC_NON_Z_WIND | FC_EXTERN_TYPE ;
      fontcontext.ExtndFlags |= EF_XLFONT_TYPE ;
      break ;
    default:
      format |= FC_IF_TYPE | FC_EXTERN_TYPE ;
    }
  }

  /* Note that ss here is a UFST symbolset number, NOT a PCL number */
  if (ss < 0) {
    /* User symbolset */
    user_ss * userss = finduserssbyref(ws, -ss) ;
    if (userss) {
      unsigned char * data = userss->data ;

      /* It seems that if the symbol set is a user defined symbol set
         AND uses MSL indexing, it is ignored when the font selected
         is a built in font. */
      if (data[4] == 3 || is_user_font) {
        int first = OP(data,6,7) ;
        int last  = OP(data,8,9) ;

        if (cid < first || cid > last)
          cid = 65535 ;
        else {
          int header_size = OP(data,0,1) ;
          cid = header_size + (cid - first)*2 ;
          cid = OP(data,cid,cid+1) ;
        }
        if (cid == 65535) {
          /* No glyph for this code */
          fakeSpace = 1 ;
          cid = 0 ;
        }
      }
      fontcontext.ssnum = DL_SYMSET ;
      fontcontext.ExtndFlags |= EF_NOSYMSETMAP ;
    } else {
      PCL_SSINFO * ssList = ws->psslist ;
      fontcontext.ssnum = (uint16) ssList[ws->defaultss].ufst ;
    }
  } else {
    /* UFST symbolset identifier */
    if (! is_user_bound_font)
      fontcontext.ssnum   = (uint16) ss;    /* or DL_SYMSET if cid is glyph ID */
  }

  fontcontext.xspot     = 0x10000L;
  fontcontext.yspot     = 0x10000L;

  /* We are going to use MAT0 scaling */
  fontcontext.fc_type   = FC_MAT0_TYPE;

#if BOLD_P6
  fontcontext.pcl6bold = (bold > 65535) ? 65535 : (UW16)bold;
#endif

  fontcontext.font_id = font_id;

  /* Galley replacement can happen for either orientation */
  fontcontext.ExtndFlags |= EF_GALLEYSEG_TYPE ;

  if ((wmode & 1) != 0) {
    fontcontext.ExtndFlags |= EF_VERTSUBS_TYPE |  /* This changes the glyphs, but not to the right ones */
                              EF_VERTEXSEG_TYPE ; /* These don't seem to do anything much */
    if ((wmode & 2) != 0)
      fontcontext.ExtndFlags |= EF_UFSTVERT_TYPE ; /* Rotate certain glyphs */
  }

retry_as_outline: /* See comment below */

  if (bitmap) {
    fontcontext.format  = format | FC_BITMAP_TYPE;
  } else {
    fontcontext.format    = format | FC_CUBIC_TYPE;
  }


  ws->transformed = FALSE ;
  /* For outlines we use a fixed scale, but for bitmaps... */
  if (bitmap && pfinfont) {
    float factor ;

    if (scaleFactor == INTELLIFONT) {
      fontcontext.s.m0.matrix_scale = 16 ;
      factor = 65536.0f * 72 / INTELLIFONT / 72.307f ;
    } else {
      fontcontext.s.m0.matrix_scale = 8 ;
      factor = 256.0f ;
    }

#if USE_FONT_MATRIX
    if (get_id_font && (get_id_font->type & 1) == 1) {
#endif
      /* Make the bitmap axis-aligned and transform later */
      factorX = (pfinfont->dx == 0.0) ? 0 :
                (SL32) (scale  * factor / pfinfont->dx + 0.5f) ;
      factorY = (pfinfont->dy == 0.0) ? 0 :
                (SL32) (height * factor / pfinfont->dy + 0.5f) ;
      factorA = 0 ;
      factorB = factorX * faux / 3 ;  /* Faux italic */

#if USE_FONT_MATRIX
    } else {
      /* Use the font matrix */
      factorX = (SL32) (0.5f + scale  * factor * pfinfont->matrix[0]) ;
      factorY =-(SL32) (0.5f + height * factor *
                        (pfinfont->matrix[3] + pfinfont->matrix[1] * faux / 3)) ;
      factorA =-(SL32) (0.5f + height * factor * pfinfont->matrix[1]) ;
      factorB = (SL32) (0.5f + scale  * factor *
                        (pfinfont->matrix[2] + pfinfont->matrix[0] * faux / 3)) ;

      ws->transformed = TRUE ;
      if (ws->context)
        (void) ws->super.callbacks->options(ws->context,
                                            PFIN_OPTION_TRANSFORMED,
                                            PFIN_OPTION_TRANSFORMED) ;
    }
#endif

  } else {

    /* Prepare the font matrix and matrix_scale for this type of font - detected
     * by the scaleFactor.
     */
    if (scaleFactor == INTELLIFONT) {
      fontcontext.s.m0.matrix_scale = 16;
      /*factorX = 30437/2 ;*/ /* 32768 * 72 / 72.307 * 8192 / INTELLIFONT */
      factorX = 32768 * 8192 / INTELLIFONT ;
    } else {
      fontcontext.s.m0.matrix_scale = 3;
      factorX = 8 * scaleFactor ;
    }

    factorY = factorX ;
    factorA = 0 ;
    factorB = factorX * faux / 3 ;  /* Faux italic */
  }

  fontcontext.s.m0.m[0] = factorX;
  fontcontext.s.m0.m[1] = factorA;
  fontcontext.s.m0.m[2] = factorB;
  fontcontext.s.m0.m[3] = factorY;

  /* Open the font to reset the fontmetrics data as the Access function
   * uses some of the current data*/
  status = ws->cgif->pfnCGIFfont(&fontcontext);
  if (status) {
    HQFAILV(("CGIFfont() error: %d\n", status));
    return SW_PFIN_ERROR;
  }

  /* Now get the glyph definition size and status */
  if (fakeSpace)
    status = ERR_find_cgnum ;
  else
    status = ws->cgif->pfnCGIFchar_size(FSA cid, FontSize, (SW16)0);

  if (status) { /* something went wrong */

    if (status == ERRDLBmpInProcess) {
      /* We're doing a PCLEO bitmap */
      struct {
        MEM_HANDLE DLBitmapPtr ;
      } bm ;

      status = ws->cgif->pfnCGIFmakechar((PIFBITMAP)&bm, NULL);
      if (status) {
        HQFAILV(("CGIFmakechar error %d\n", status));
        return SW_PFIN_ERROR ;

      } else {
        /* Note that this isn't yet optimal - we build an IFBITMAP simply
        to reuse the existing bitmap handling. Ideally, we'd pass the
        chr_header_bitmap out to the data provider to serve or expand as
        necessary. This would mean that there would be three different
        structures that can be created by make_char, and the caching and
        data provider code would have to reflect that. This suboptimal
        version will do for now. */
        PIFBITMAP pbm ;
        font_header * header = (font_header *) get_id_font->data ;
        chr_header_bitmap * glyph = (chr_header_bitmap *) bm.DLBitmapPtr ;
        static sw_pfin_font fakefont ;
        UW16 width, height, design ;
        size_t size ;
        SW16 Class, deltaX, bytes, left, top ;
        uint8 * data ;

        /* Work around for UFST's missing soft glyph 'unexpected behaviour' */
        if (get_id_null)
          goto hack_make_space ;

        fakefont.xdpi = get_id_font->xres * xres_scale;
        fakefont.ydpi = get_id_font->yres * yres_scale;
        if (pfinfont) {
          fakefont.dx = pfinfont->dx ;
          fakefont.dy = pfinfont->dy ;
          fakefont.matrix = pfinfont->matrix ;
        }
        else
        {
          fakefont.dx = 0 ;
          fakefont.dy = 0 ;
          fakefont.matrix = NULL;
        }
        bitmap = TRUE ;
        pfinfont = &fakefont ;

        if (glyph->format == 0) {
          /* XL bitmap */
          chr_header_xlb * xlb = (chr_header_xlb *) bm.DLBitmapPtr ;

          Class  = 1 ;
          left   = xlb->left ;
          top    = xlb->top ;
          width  = xlb->width ;
          height = xlb->height ;
          deltaX = 0 ;
          data   = xlb->data ;
          design = 1 ;

        } else {

          Class  = glyph->Class ;
          left   = glyph->left ;
          top    = glyph->top ;
          width  = glyph->width ;
          height = glyph->height ;
          deltaX = glyph->deltaX ;
          data   = glyph->data ;
          design = header->height ;

        }
        bytes = (width + 7) >> 3 ;
        size  = height * bytes ;

        pbm = (PIFBITMAP) myalloc(offsetof(IFBITMAP,bm) + size) ;
        if (!pbm)
          return SW_PFIN_ERROR_MEMORY;

        pbm->width      = bytes ;
        pbm->depth      = height ;
        pbm->xorigin    = 16 * left ;
        pbm->yorigin    = 16 * top ;
        pbm->du_emx     = design ;
        pbm->escapement = deltaX ;

        pol = (PIFOUTLINE)pbm ;
        pol->escapement = deltaX ;

        if (Class == 1) {
          /* Uncompressed */
          memcpy(pbm->bm, data, size) ;
        } else {
          /* Compressed

             This is a simple run-length-encoded bitfield, with the count of
             zero bits first. Additionally, complete scanlines can be repeated.
           */
          uint8 * in = data ;               /* The compressed data ptr */
          uint8 * out = (uint8 *) pbm->bm ; /* destination */
          uint8 * check = out + size ;      /* Overrun check */
          int   y ;

          for (y=0; y < height; y++) {  /* Consume all the data */
            uint8 acc = 0;              /* Shift accumulator */
            uint8 rem = 8;              /* Bits remaining to shift into acc */
            uint8 val = 0;              /* 0=white, 255=black */
            int   repeats = *in++ ;     /* Repeats of this complete scanline */
            int   bits = width ;        /* Bits left to expand this scanline */

            while (bits > 0) {  /* Build a complete line */
              uint8 n = *in++ ; /* Number of pixels of this colour */
              bits = bits - n ;

              if (n < rem) {    /* Will fit into current byte without writing */
                acc = (acc << n) | (val >> (8u-n)) ;
                rem = rem - n ;
              } else {          /* Fill byte and write out */
                acc = acc << rem | (val >> (8u-rem)) ;
                n = n - rem ;   /* Because n-=rem raises a bogus warning */
                *out++ = acc ;

                while (n >= 8) {/* Write out whole bytes of val */
                  *out++ = val ;
                  n = n - 8 ;   /* Ditto, re warning */
                }

                rem = 8 - n ;   /* Put the rest into acc */
                acc = val >> rem ;
              }
              val = 255 - val ; /* Toggle colour */
            }
            if (rem < 8u)
              *out++ = acc << rem ;     /* Output final partial byte */

            if (repeats >= height - y)  /* Check sanity of repeat count */
              repeats = height - y - 1 ;    /* BREAKPOINT HERE! */

            y += repeats ;              /* Repeat that scanline if required */
            while (repeats--) {
              memcpy(out, out-bytes, bytes) ;
              out += bytes ;
            }
          }

          /* We may not have filled the bitmap... */
          if (out != check) {
            if (out > check)            /* Buffer overrun! Fatal error */
              return SW_PFIN_ERROR ;    /* BREAKPOINT HERE! */

            while (out < check)         /* Otherwise, pad with white */
              *out++ = 0 ;
          }
        }
      }

    } else if (status == ERRmatrix_range && bitmap) {
      /* If we ask UFST for too large a bitmap we get this error, so try again
         as an outline. */
      bitmap = FALSE ;
      goto retry_as_outline ;

    } else if ((status == ERR_fixed_space && spacesOK) ||
               (status == ERR_find_cgnum && cid == 0) ) {
      /* if a fixed space, or .notdef is undefined, fake a fixed space */
hack_make_space:
      pol = (PIFOUTLINE) myalloc(sizeof(IFOUTLINE));
      if (!pol)
        return SW_PFIN_ERROR_MEMORY;

      pol->ol.num_loops = 0;
      /* Note! Our fontmap override */
      pol->escapement = (SW16) (hmi * 8192 + 0.5f);
      pol->du_emx     = (SW16) 8192;  /* Not scaleFactor in case INTELLIFONT */
      bitmap          = FALSE;
      ws->transformed = FALSE;

    } else if (status == ERR_find_cgnum || status == ERR_fixed_space) {
      /*HQFAILV(("Missing glyph, font_id = %d (%s), Unicode glyph ID = %d\n",
                fid, FONTNAME(fid), cid));*/
      return SW_PFIN_ERROR_UNKNOWN;

    } else {
      goto hack_make_space ;
      /*HQFAILV(("pfin_ufst5_metrics : CGIFchar_size error %d\n", status));*/
      /*return SW_PFIN_ERROR;*/
    }

  } else { /* char_size succeeded, so we can make the glyph */

    LPUB8 pnz = (FontSize[NZW_SIZE_IDX]) ? myalloc(FontSize[NZW_SIZE_IDX]) : 0;

    /* CCBUF resize required? */
    if (FontSize[CCBUF_SIZE_IDX] > ws->ConfigBlock.cc_buf_size) {
      myfree(ws->ConfigBlock.cc_buf_ptr) ;
      ws->ConfigBlock.cc_buf_size = (FontSize[CCBUF_SIZE_IDX] + 1023) & ~1023 ;
      ws->ConfigBlock.cc_buf_ptr = myalloc(ws->ConfigBlock.cc_buf_size);

      if (ws->ConfigBlock.cc_buf_ptr == 0) {
        ws->ConfigBlock.cc_buf_size = 0 ;
        if (pnz)  myfree(pnz) ;
        return SW_PFIN_ERROR_MEMORY ;
      }

      status = ws->cgif->pfnCGIFconfig(FSA &ws->ConfigBlock);
      if (status) {
        HQFAILV(("CGIFconfig() error: %d\n", status));
        if (pnz)  myfree(pnz) ;
        return SW_PFIN_ERROR ;
      }

      /* Work around for UFST's missing soft glyph 'unexpected behaviour' */
      if (get_id_null)
        goto hack_make_space ;
    }

    pol = (PIFOUTLINE) myalloc(FontSize[0]);
    if (pol == 0 || (FontSize[NZW_SIZE_IDX] && pnz == 0)) {
      if (pol)  myfree(pol);
      if (pnz)  myfree(pnz);
      return SW_PFIN_ERROR_MEMORY;
    }
    status = ws->cgif->pfnCGIFmakechar((PIFBITMAP)pol, pnz);
    if (pnz)
      myfree(pnz);
    if (status) {
      if (status == ERR_fixed_space)
        goto hack_make_space ;  /* returned here by format 16, unhelpfully */

      HQFAILV(("CGIFmakechar error %d\n", status));
      myfree(pol);
      return SW_PFIN_ERROR;
    }
  }

  /* Font size adjustment affects width too */
  pol->escapement = (SW16) (scale * pol->escapement + 0.5f);

  ws->pol    = pol;
  ws->fid    = fid;
  ws->cid    = cid;
  ws->ss     = ss;
  ws->bold   = bold;
  ws->bitmap = bitmap;
  ws->wmode  = wmode;
  if (bitmap && pfinfont) {
#if USE_FONT_MATRIX
    if (pfinfont->matrix)
      memcpy(ws->matrix, pfinfont->matrix, sizeof(ws->matrix)) ;
    else
      memset(ws->matrix, 0, sizeof(ws->matrix)) ;
#endif
    ws->xres = pfinfont->xdpi;
    ws->yres = pfinfont->ydpi;
    ws->dx   = pfinfont->dx;
    ws->dy   = pfinfont->dy;
  }

  return SW_PFIN_SUCCESS;
}

/* ========================================================================== */
/* Given a sw_datum PCL XL font name, find the PCL5 font number, style, stroke
 * weight and symbolset, returning success or failure.
 */
int XLtoPCL(pfin_ufst5_instance *ws,
            sw_datum* xl, int32* font, int32* mono,
            int32* ss, int32* weight, int32* style, float* hmi)
{
  XL_DATA * XL = ws->pxldata;
  signed int cmp = 0;
  signed int i;
  int result = 1, a, b, m = 0;

  /* initialise */
  *font = 0;
  *mono = 0;
  *weight = 0;
  *style = 0;
  *hmi = 1.0f;
  /* only use default symbolset if not set already */
  if (*ss == 0)
    *ss = ROMAN8;

  /* exit early if wrong format */
  if (xl == 0 || xl->type != SW_DATUM_TYPE_STRING || xl->length != 16) {
    UFST_SHOW("Ignoring PCL XL font");
    return 0;
  }

  /* Exit if no XL mapping data */
  if (XL == 0)
    return 0 ;

  /* find suffix, if any */
  for (i=15; xl->value.string[i] != 32 && i>8; i--);
  if (i < 15) {
    /* Either a suffix found, or not definitely ruled out */
    cmp = 1; /* i.e. no match yet */
    result = 0;

    if (xl->value.string[i] != 32) {
      /* did not find suffix separator - so try whole style list in reverse */
      i = 15; /* assume no suffix at all */
      for (m = XL->styles_length - 1; m >= 0; m--) {
        a = XL->styles[m].length;
        cmp = memcmp(XL->stylenames + XL->styles[m].offset,
                     xl->value.string + 16 - a, a);
        if (cmp == 0) {
          i -= a; /* update suffix */
          break;  /* ...with m being the index of the style */
        }
      }
    } else {
      /* found the suffix - find the style, if we can */
      const char * suffix = xl->value.string + i + 1;
      signed int length = 15 - i;

      /* find in suffix table - if found, override weight, style and ss */
      for (a=0, b=XL->styles_length ; a<b ; ) {
        signed int thislen;
        m = (a+b)/2;

        /* Compare lengths and if equal compare strings (keeping the result) */
        thislen = XL->styles[m].length;
        if (length == thislen) {
          cmp = memcmp(XL->stylenames + XL->styles[m].offset, suffix, length);
          if (cmp == 0)
            break;        /* ...with m being the index of the style */
          thislen += cmp; /* to make the following comparison simpler */
        }
        if ( length < thislen )
          b = m;
        else
          a = m+1;
      }
    }
    if (cmp == 0) {
      /* Matched the style suffix */
      int32 newss;

      *weight = (((int32)XL->styles[m].weightstyle) - XL_MEDIUM) / 16;
      *style = XL->styles[m].weightstyle & 15;
      newss = XL->symbolsets[XL->styles[m].ss];
      if (newss != 0)
        *ss = newss;
      result = 1;
    }
  }

  /*
   * find *length* of prefix
   * by scanning backwards down the string
   * looking for the (index of) first non-space character
   * and then adding one to that value
   */
  while (xl->value.string[i] == 32 && --i > 0) ;
  i++;

  /* find in font name table - if found, override font */
  for (a=0, b=XL->fonts_length ; a<b ; ) {
    signed int thislen;
    m = (a+b)/2;

    thislen = XL->fonts[m].length & XL_LENGTH;
    if (i == thislen) {
      cmp = memcmp(XL->fontnames + XL->fonts[m].offset, xl->value.string, i);
      if (cmp == 0) {
        /* matched font name */
        *font = XL->fonts[m].font;
        *mono = (XL->fonts[m].length & XL_MONO) != 0 ;
        if (*mono && XL->monoHMIs)
          *hmi = XL->monoHMIs[(XL->fonts[m].length >> XL_HMI)-1];
        if ((XL->fonts[m].length & XL_ITAL) != 0)
          *style = *style | 1 ;
        result |= 2;
        break;
      }
      thislen += cmp; /* to make the following comparison simpler */
    }
    if ( i < thislen )
      b = m;
    else
      a = m+1;
  }

  /* have we recognised the font and style? */
  if (result < 3) {
    char str[20];
    memcpy(str, xl->value.string, 16);
    str[16] = 0;
    UFST_SHOW("Did not recognise PCL XL font %s", str);
    return 0;
  }

  return 1;
}

/* -------------------------------------------------------------------------- */
/* A macro to formalise the scoring system in PCLfontmatch.
 *
 * Usually, if a score is worse than previous and earlier scores aren't already
 * better than the current best match, then we can continue to the next font.
 */

#define SCORE(n,s) \
score[n] = s ; \
if (!better && score[n] > fit[n])  continue ; \
if (score[n] < fit[n] && score[n] > -1)  better = TRUE ;

/* -------------------------------------------------------------------------- */
/* Do a PCL font match */

static
int PCLfontmatch(pfin_ufst5_instance * ws, int32 forXL,
                 int32 * symbolset, int32 * symbolsettype,
                 int32 fixedpitch, float pitch, float height,
                 int32 style, int32 weight, int32 typeface, int32 exclude,
                 int32 print_resolution,
                 int32 * bold, int32 * faux)
{
  int   i, ss_index, best = ws->DefaultFont /* Courier */,
    delta, symbolset_format = UNKNOWN_CHAR_CODES,
    font_format = UNKNOWN_CHAR_CODES ;
  enum  {ex=0,ss,sp,pt,ht,st,wt,tf,rs,lo,or,fx,lt,score_len} ;
  int32 score[score_len], fit[score_len] = { /* a very poor fit! */
    1,1,1,65536,65536,65536,65536,65536,65536,65536,65536,16777216,65539} ;
  unsigned char chrReq[8] ;
  PCL_SSINFO * ssList = ws->psslist ;
  user_font * nextuser = ws->user_fonts, * user ;
  user_ss * userss = 0 ;
  int32 fauxitalic, embolden ;

  /* Find character complement and symbolsettype */
  ss_index = find_symbolset(ws, *symbolset, &userss) ;

  if (userss) {
    /* A user symbolset */
    unsigned char * ssReq = (unsigned char *) &userss->data[10] ;

    for (i = 0; i < 8; i++)
      chrReq[i] = ssReq[i] ;

    symbolset_format = userss->data[4] ;
    if (symbolset_format != MSL_CHAR_CODES &&
        symbolset_format != UNICODE_CHAR_CODES)
      symbolset_format = UNKNOWN_CHAR_CODES ;

    if (symbolset_format == UNICODE_CHAR_CODES && userss->data[5] == 0) {
      *symbolsettype = 1 ;
    } else {
      *symbolsettype = userss->data[5] ;
    }
    *symbolset = userss->id ;
  } else {
    /* A built-in symbolset. Assume Unicode to begin with as these
       work with all internal fonts. */
    chrReq[0] = chrReq[1] = chrReq[2] = chrReq[3] = chrReq[6] = 0 ;
    chrReq[4] = ssList[ss_index].unicode_chrReq[0] ;
    chrReq[5] = ssList[ss_index].unicode_chrReq[1] ;
    chrReq[7] = ssList[ss_index].unicode_chrReq[2] ;

    symbolset_format = UNICODE_CHAR_CODES ;
    *symbolsettype = ssList[ss_index].type ;
    *symbolset = ssList[ss_index].ss ; /* in case we've defaulted */
  }

  /* Now do the font match */
  for ( i = (nextuser) ? -1 : 0; i < ws->TotalFonts; i++ ) {
    /* at each step, if the score is better than current best fit, set
     * better to be true, else if better is false and the score is worse,
     * we can exit early, otherwise continue to next step
     */
    int j, better = FALSE, ref = i, bitmap = FALSE, can_faux = FALSE ;
    fco_font * font, tempfont ;
    TTFONTINFOTYPE * fontInfo, tempinfo = {0} ;
    float temppitch = 0, tempheight = 0 ;
    int32 isFixedPitch, resolution = 600 ;

    user = nextuser ; /* will point to current user font or 0 for FCO font */
    if (i < 0) {
      /* Consider user fonts first */

      /* Deal with proxied fonts */
      while (user && user->proxy >= ws->TotalFonts)
        user = finduserfontbyref(ws, user->proxy) ;
      if (user == 0 ||              /* failed to find underlying font */
          user->type >= TYPE_XL) {  /* or is an XL soft font... then skip */
        nextuser = nextuser->next ;
        if (nextuser)
          --i ;
        continue ;
      }
      if (user->proxy > -1) {
        /* Proxied internal font. */
        font = &ws->Fonts[user->proxy] ;
        tempinfo = *(font->pttFontInfo) ;
        fontInfo = &tempinfo ;
        /* Reverse character complement for FCO fonts as they are in
           reverse order. Do it once here to keep consistent throughout
           code. */
        for (j=0; j<8; j++)
          fontInfo->pcltChComp[j] = font->pttFontInfo->pcltChComp[7 - j] ;
        fontInfo->pcltChComp[0] = 0xff ;
        fontInfo->pcltChComp[1] = 0xff ;
        fontInfo->pcltChComp[2] = 0xff ;
        fontInfo->pcltChComp[3] = 0xff ;

        isFixedPitch = fontInfo->isFixedPitch ;
        user = 0 ;  /* FCO font */

        font_format = UNICODE_CHAR_CODES ;

        if (! userss) {
          chrReq[0] = chrReq[1] = chrReq[2] = chrReq[3] = chrReq[6] = 0 ;
          chrReq[4] = ssList[ss_index].unicode_chrReq[0] ;
          chrReq[5] = ssList[ss_index].unicode_chrReq[1] ;
          chrReq[7] = ssList[ss_index].unicode_chrReq[2] ;
          symbolset_format = UNICODE_CHAR_CODES ;
        }

      } else {
        /* Soft font */
        font_header * header = (font_header *) user->data ;
        int skip = FALSE ;
        uint8 bound ;

        tempfont.also     = user->typeface ;
        tempfont.typeface = tempfont.also ;
        tempfont.ord      = 0 ;

        for (j=0; j<8; j++)
          tempinfo.pcltChComp[j] = user->chrComp[j] ;

        bitmap = ((user->type & TYPE_BITMAP) != 0) ;
        bound = user->data[typeO] ;

        if (bound <= BOUNDFONTTYPE) {
          *symbolsettype = 2 ;
        }

        /*
        if (header->format == 11 || header->format == 15) {
          font_format = tempinfo.pcltChComp[7] & 0x7 ;
        }
        */

        if (header->format == 11) {
          font_format = MSL_CHAR_CODES ;
          /* If its an internal symbol set make sure we use the correct
             character requirements for the soft font. */
          if (! userss && symbolset_format != MSL_CHAR_CODES) {
            chrReq[1] = chrReq[2] = chrReq[4] = chrReq[5] = chrReq[6] = 0 ;
            chrReq[0] = ssList[ss_index].msl_chrReq[0] ;
            chrReq[3] = ssList[ss_index].msl_chrReq[1] ;
            chrReq[7] = ssList[ss_index].msl_chrReq[2] ;
            symbolset_format = MSL_CHAR_CODES ;
          }
        } else if (header->format == 15) {
          font_format = UNICODE_CHAR_CODES ;
          /* If its an internal symbol set make sure we use the correct
             character requirements for the soft font. */
          if (! userss && symbolset_format != UNICODE_CHAR_CODES) {
            chrReq[0] = chrReq[1] = chrReq[2] = chrReq[3] = chrReq[6] = 0 ;
            chrReq[4] = ssList[ss_index].unicode_chrReq[0] ;
            chrReq[5] = ssList[ss_index].unicode_chrReq[1] ;
            chrReq[7] = ssList[ss_index].unicode_chrReq[2] ;
            symbolset_format = UNICODE_CHAR_CODES ;
          }
        } else {
          font_format = UNICODE_CHAR_CODES ;

          /* If its an internal symbol set make sure we use the correct
             character requirements for the soft font. */
          if (! userss && symbolset_format != UNICODE_CHAR_CODES) {
            chrReq[0] = chrReq[1] = chrReq[2] = chrReq[3] = chrReq[6] = 0 ;
            chrReq[4] = ssList[ss_index].unicode_chrReq[0] ;
            chrReq[5] = ssList[ss_index].unicode_chrReq[1] ;
            chrReq[7] = ssList[ss_index].unicode_chrReq[2] ;
            symbolset_format = UNICODE_CHAR_CODES ;
          }
        }

        isFixedPitch = (header->spacing == 0) ;
        can_faux = (user->type >= TYPE_TT16 &&
                    (user->type & TYPE_BITMAP) == 0) ;    /* Is this right? */

        tempinfo.pcltStyle    = user->style ;
        tempinfo.pcltStrokeWt = header->weight ;

        /* We currently ignore resolution and orientation */

        tempinfo.scaleFactor  = 2048 ;
        tempinfo.spaceBand    = (UL32)(2048.0f * user->hmi) ;

        resolution = user->xres ;

        if (user->id == -1 && user->typeface == 0) {
          /* LinePrinter is bound to only these symbolsets */
          int32 n = *symbolset >> 5 ;
          switch (*symbolset & 31) {
          case 'U'-64:
            skip = (n != 1 && n != 8 && (n < 10 || n > 12)) ;
            break ;
          case 'N'-64:
            skip = (n != 0 && n != 2 && n != 5 && n != 6 && n != 9) ;
            break ;
          default:
            skip = TRUE ;
          }

        } else {
          /* Ignore soft fonts bound to the wrong symbolset */
          if (user->ss > -1 && user->ss != *symbolset)
            skip = TRUE ;
        }

        if (skip) {
          nextuser = nextuser->next ;
          if (nextuser)
            --i ;
          continue ;
        }

        temppitch             = user->pitch ;
        tempheight            = user->height ;

        font = &tempfont ;
        fontInfo = &tempinfo ;
      }
      /* user may be 0 at this point (if a proxied FCO font) */
      ref = nextuser->ref ;     /* Note: original ref, not proxy's */

      nextuser = nextuser->next ;
      if (nextuser)
        --i ;   /* only progress to FCO fonts when user fonts exhausted */
      /* nextuser may be 0 at this point and always points to NEXT user font */
    } else {
      /* FCO fonts */
      font = &ws->Fonts[i] ;
      tempinfo = *(font->pttFontInfo) ;
      fontInfo = &tempinfo ;
      /* Reverse character complement for FCO fonts as they are in
         reverse order. Do it once here to keep consistent throughout
         code. */
      for (j=0; j<8; j++)
        fontInfo->pcltChComp[j] = font->pttFontInfo->pcltChComp[7 - j] ;
      fontInfo->pcltChComp[0] = 0xff ;
      fontInfo->pcltChComp[1] = 0xff ;
      fontInfo->pcltChComp[2] = 0xff ;
      fontInfo->pcltChComp[3] = 0xff ;

      isFixedPitch = fontInfo->isFixedPitch ;

      font_format = UNICODE_CHAR_CODES ;

      if (! userss) {
        chrReq[0] = chrReq[1] = chrReq[2] = chrReq[3] = chrReq[6] = 0 ;
        chrReq[4] = ssList[ss_index].unicode_chrReq[0] ;
        chrReq[5] = ssList[ss_index].unicode_chrReq[1] ;
        chrReq[7] = ssList[ss_index].unicode_chrReq[2] ;
        symbolset_format = UNICODE_CHAR_CODES ;
      }
    }

    if (font_format != UNKNOWN_CHAR_CODES && symbolset_format != UNKNOWN_CHAR_CODES &&
        font_format != symbolset_format) {
      continue ;
    }

    /* Ignore PS fonts? */
    if (font->ord == UNMAPPEDFONT && !ws->matchpsfonts)
      continue ;

    /* The very first criterion to check is the exclude bitmaps flag
     * for HPGL. */
    SCORE(ex, exclude && bitmap) ;

    /* The symbolset must match - note special behaviour for symbol fonts */
    if (*symbolset == WINGDINGS_SS)               /* Wingdings */
      delta = (font->typeface != WINGDINGS) ;
    else if (*symbolset == DINGBATS_SS)           /* Dingbats */
      delta = (font->typeface != DINGBATS) ;
    else if (*symbolset == SYMBOL_SS)             /* Symbol[PS] */
      delta = (font->typeface != SYMBOLPCL && font->typeface != SYMBOLPS) ;
    else {
      delta = 0 ;
      /* Check character complement */
      for (j=0; j<7; j++) {
        if (chrReq[j] & fontInfo->pcltChComp[j]) {
          delta = 1 ;
          break ;
        }
      }
      /* If still OK, check last byte high 5 bits so that we ignore
         vocabularies! */
      if ( ! delta && ( (chrReq[7] >> 3) & (fontInfo->pcltChComp[7] >> 3) ) )
        delta = 1 ;
    }
    SCORE(ss, delta) ;

    /* Spacing */
    SCORE(sp, abs(fixedpitch - isFixedPitch)) ;

    /* Pitch - ignored for proportional */
    delta = -1 ;
    if (isFixedPitch && !forXL) {
      delta = 0 ;
      if (bitmap) {
        delta = (int32) (4*(temppitch - pitch)) ; /* "if not available, the next greater" */
        if (delta < 0)
          delta = 65536 - delta ;   /* else "closest available lesser" */
      }
    }
    SCORE(pt, delta) ;

    /* Height - ignored for fixedPitch scaleable */
    if (bitmap && !forXL) {
      delta = abs((int)(4.0f *(height - tempheight))) ;
      if (delta > 2)
        continue ;
    } else
      delta = (isFixedPitch) ? -1 : 0 ;
    SCORE(ht, delta) ;

    /* Style - exact match required, but no match means it is ignored */
    if (can_faux) {
      delta = 0 ;
      fauxitalic = (style & 1) == 1 && (fontInfo->pcltStyle & 1) == 0 ;
      /* Note that we don't try to unoblique an italic, though we could. */
    } else {
      if (style == fontInfo->pcltStyle) {
        delta = 0 ;
      } else if (fontInfo->pcltStyle == 0 && style != 0) {
        delta = 1 ;
      } else {
        delta = 2 ;
      }
      fauxitalic = 0 ;
    }
    SCORE(st, delta) ;

    /* Stroke weight */
    if (weight < 0)
      delta = weight - fontInfo->pcltStrokeWt ;
    else
      delta = fontInfo->pcltStrokeWt - weight ;
    if (delta < 0 && can_faux) {
      embolden = delta * -150 ;  /* empirical/guesswork */
      delta = 0 ;
    } else
      embolden = 0 ;
    if (delta < 0)
      delta = 100-delta ; /* "If no thicker... closest available" */
    SCORE(wt, delta) ;

    /* Typeface family - if not a perfect match, match the base number */
    SCORE(tf, ((typeface&0xfe00) != 0 && ((font->typeface&0xfe00) == 0 || (font->also&0xfe00) == 0)) ? -1 :
              (typeface == font->typeface || typeface == font->also) ? 0 :
              (((typeface ^ font->typeface) & 4095) == 0 ||
               ((typeface ^ font->also) & 4095) == 0) ? 1 : 2) ;

    /* Resolution - Irrelevent for outlines */
    score[rs] = -1 ;
    if (! forXL) {
      if (bitmap) {
        if (resolution == print_resolution) {
          SCORE(rs, 0) ;
        } else if (resolution < print_resolution) {
          SCORE(rs, 2) ;
        }
        /* Bitmap resolution is greater than print resolution,
           so we can't select this.
        else {
          SCORE(rs, -1) ;
        }
        */
      } else {
        /* Scaleable is worse than a bitmap which has an identical
           resolution but better than a bitmap which has less
           resolution. */
        SCORE(rs, 1) ;
      }
    }

    /* Location - Internal or soft font. Although we download
       Lineprinter, it should not be regarded as a user font. */
    SCORE(lo, (user && user->id != -1) ? 0 : 3) ;

    /* Orientation - Irrelevant for outlines, NYI for bitmaps */
    score[or] = -1 ;

    /* The final score is the amount of fauxing necessary - this must be
     * less important than any other criteria while still allowing an
     * unfauxed match to better a fauxed one.
     */
    SCORE(fx, embolden + fauxitalic) ;

    /* Deal with location based on ID's as a last resort. This needs
       more investigation. See PCL5 Developers Guide. Lineprinter
       appears to be a special case. QL 5c FTS 1551 demonstrates
       this. */
#define MAX_NUM_USER_DEFINED_FONTS 32768 /* 0 - 32767 */
    if (user && user->id != -1) { /* User defined font. */
      SCORE(lt, user->id) ;
    } else {
        if (user && user->id == -1) { /* Lineprinter */
          /* Lineprinter appears to be a fraction more difficult to select
             that an internal font. */
          SCORE(lt, MAX_NUM_USER_DEFINED_FONTS + 1) ;
        } else { /* Internal font. */
          /* At present I score all internal fonts the same. */
          SCORE(lt, MAX_NUM_USER_DEFINED_FONTS) ;
        }
    }

    /* Having been scored, is this font a better match? */
    if (better) {
      int32 perfect = 0 ;
      /* this is a better match, so remember it */
      for (j=0; j<score_len; j++) {
        if (score[j] > -1)
          fit[j] = score[j] ;
        perfect += fit[j] ;
      }
      best = ref ;
      *faux = fauxitalic ;
      *bold = embolden ;
      if (perfect == 0)
        break ;  /* finish early if match can't be improved! */
    }
  } /* for all fonts */

  /* We did not match a single font with the user defined symbol set
     character requirements array so set the symbol set to Roman8 and
     try matching again. */
  if (userss && fit[ss] == 1) {
    *symbolset = ROMAN8 ;
    *symbolsettype = 1 ;
    best = PCLfontmatch(ws, forXL, symbolset, symbolsettype,
                        fixedpitch, pitch, height, style, weight, typeface, exclude,
                        print_resolution, bold, faux) ;
  }

  return best ;
}

/* -------------------------------------------------------------------------- */
/** \brief PCL font, glyph and symbolset definition and control */

int32 pcl_symbolset_control(pfin_ufst5_instance * ws, int32 action, int32 id)
{
  user_ss * ss, ** ptr = &ws->user_symbolsets ;

  switch (action) {
  case 0: /* Delete all/temporary user symbolsets */
  case 1:
    while ((ss = *ptr) != 0) {
      if (action <= ss->temp) { /* ignore those marked IS_PROTECTED */
        *ptr = ss->next ;
        id = ss->id ;
        myfree(ss) ;
        SwEvent(EVENT_PCL_SS_DELETED, &id, sizeof(int32)) ;
      } else {
        ptr = &ss->next ;
      }
    }
    break ;

  case 2: /* Delete specified user symbolset */
  case 4: /* Make specified user symbolset temporary */
  case 5: /* Make specified user symbolset permanent */
    while ((ss = *ptr) != 0 && ss->id != id)
      ptr = &ss->next ;
    if (ss == 0)
      return FALSE ;

    if (ss->temp > IS_PROTECTED) {
      if (action == 2) { /* Delete specified user symbolset */
        *ptr = ss->next ;
        ssfree(ws, ss) ;
        SwEvent(EVENT_PCL_SS_DELETED, &id, sizeof(int32)) ;
      } else { /* Make specified user symbolset temporary/permanent */
        ss->temp = (action == 4) ? IS_TEMPORARY : IS_PERMANENT ;
      }
    }
    break ;
  } /* switch(action) */
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
/* Manage softloaded fonts */

int32 pcl_font_control(pfin_ufst5_instance * ws, int32 action,
                       int32 id, /*@in@*/ sw_datum * idstr,
                       int32 code, /*@in@*/ /*@notnull@*/ sw_datum * name)
{
  user_font * font, **ptr = &ws->user_fonts ;
  size_t idlen = 0 ;
  PCL_SSINFO * ssList = ws->psslist ;

  if (idstr && idstr->type == SW_DATUM_TYPE_STRING)
    idlen = idstr->length ;

  if (action < 2 && action > -1) {
    /* 0 = Delete all soft fonts */
    /* 1 = Delete temporary soft fonts */
    while ((font = *ptr) != 0) {
      if (action <= font->temp) { /* ignore those marked IS_PROTECTED */
        *ptr = font->next ;
        id = font->id ;
        fontfree(ws,font) ;
      } else {
        ptr = &font->next ;
      }
    }
  } else {
    /* Do something to a specified font, so find it or bail out */
    font = finduserfont(id, idstr, &ptr) ;
    if (action < 6) {
      if (font == 0)
        return PCL_ERROR_UNDEFINED_FONT_NOT_REMOVED ;
      if (font->temp == IS_PROTECTED)
        return PCL_ERROR_INTERNAL_FONT_NOT_REMOVED ;
    }

    switch (action) {
    case 2: /* Delete specified soft font */
      *ptr = font->next ;
      fontfree(ws,font) ;
      break ;

    case 3: /* Delete specified glyph */
      if (code != TT_COMPONENT) {
        user_glyph * glyph = font->glyphs[code & USER_GLYPH_MASK] ;

        /* find the delete the existing glyph */
        while (glyph && glyph->code != code)
          glyph = glyph->next ;
        if (glyph)
          glyphfree(ws, font, glyph) ;
      }
      break ;

    case 4: /* Make specified font temporary */
    case 5: /* Make specified font permanent */
      font->temp = (action == 4) ? IS_TEMPORARY : IS_PERMANENT ;
      break ;

    case 6: /* Copy/Proxy named font as given ID */
    case 0x106: /* As above but always proxy (for alphanumeric ID) */
      HQASSERT(name && name->type == SW_DATUM_TYPE_STRING,
               "Bad font control miscop") ;

      /* Find the named font and proxy it if it's an internal font, or deep
       * copy it if a user font.
       */
      {
        int i ;
        int32 proxy = -1, ss, bold = 0, faux = 0;
        int8 encoding = UNKNOWN_CHAR_CODES ;

        if (parse_fontname(ws, name, &proxy, &ss, NULL, &bold, &faux, NULL)) {
          /* Note that we ignore faux italicisation at this stage - it can be
           * imposed on the copied font by a fontmatch later.
           */
          user_ss * userss = 0 ;

          ss = find_symbolset(ws, ss, &userss) ;
          ss = (userss) ? -ss : (int32) ssList[ss].ufst ;

          if (proxy < ws->TotalFonts || action == 0x106 ) {
            /* Internal font or our special action number - proxy it */
            user_font * orig ;

            if (action == 0x106 &&
                (orig = finduserfontbyref(ws, proxy)) != 0) {
              ss = orig->ss ;  /* Just in case */
              encoding = orig->encoding ;
            }

            if ((font = (user_font*) myalloc(USER_FONT_LENGTH(idlen))) != 0 ) {

              font->type   = TYPE_PROXY ;
              font->proxy  = proxy ;  /* this is the internal font number */
              font->length = 0 ;
              font->broken = NOT_BROKEN ;
              /* PCL criteria fields not relevant for proxies */

              /* There are no glyphs */
              for (i = 0; i < USER_ID_HEADS; i++)
                font->ids[i] = NULL ;
              for (i = 0; i < USER_GLYPH_HEADS; i++)
                font->glyphs[i] = NULL ;

              /* No UniqueIDs allocated. We can't share them with the original
               * as the two can diverge.
               */
              font->uids = NULL ;

              /* Store alphanumeric ID if required */
              if (idlen)
                memcpy(font->data + 0, idstr->value.string, idlen) ;
            }

          } else {
            /* Soft font - copy it entirely */
            user_font * orig = finduserfontbyref(ws, proxy) ;
            if (!orig)
              return PCL_ERROR_FONT_UNDEFINED ;

            if ((font = (user_font*) myalloc(
                   USER_FONT_LENGTH(orig->length + idlen))) != 0) {

              ss = orig->ss ;  /* Just in case */
              encoding = orig->encoding ;

              /* Copy the fields and the header data */
              memcpy(font, orig, USER_FONT_LENGTH(orig->length)) ;
              font->next = 0 ;

              /* Add the name */
              if (idlen)
                memcpy(font->data + font->length, idstr->value.string, idlen) ;

              /* Zero the lists, in case we have to delete half way through */
              for (i = 0; i < USER_ID_HEADS; i++)
                font->ids[i] = NULL ;
              for (i = 0; i < USER_GLYPH_HEADS; i++)
                font->glyphs[i] = NULL ;

              /* No UniqueIDs allocated. We can't share them with the original
               * as the two can diverge.
               */
              font->uids = NULL ;

              /* Now consistent, so copy the glyphs */
              for (i = 0; i < USER_GLYPH_HEADS; i++) {
                user_glyph * from = orig->glyphs[i] ;
                while (from) {
                  user_glyph * add ;

                  add = (user_glyph*) myalloc(USER_GLYPH_LENGTH(from->length)) ;
                  if (add) {
                    /* copy glyph */
                    memcpy(add, from, USER_GLYPH_LENGTH(from->length)) ;

                    /* add to id hash */
                    if (add->id > -1) {
                      add->nextbyid = font->ids[add->id & USER_ID_MASK] ;
                      font->ids[add->id & USER_ID_MASK] = add ;
                    } else
                      add->nextbyid = 0 ;

                    /* add to code hash */
                    add->next = font->glyphs[i] ;
                    font->glyphs[i] = add ;

                    from = from->next ;

                  } else {
                    /* Out of memory during font copy. Delete font and exit. */
                    font->id = -1 ;   /* Don't emit an event */
                    fontfree(ws,font) ;
                    font = 0 ;
                    i = USER_GLYPH_HEADS ;

                    from = 0 ;

                  } /* if add */
                } /* while from */
              } /* for i */
            } /* if orig */
          } /* if internal */

          if (font) {
            /* Fill in common font fields and add to list */
            font->next   = ws->user_fonts ;
            font->temp   = IS_TEMPORARY ;
            font->id     = id ;
            font->ss     = ss ;
            font->encoding = encoding ;
            font->bold   = bold ;   /* XL font emboldening */
            font->idlen  = idlen ;
            font->ref    = ws->next_ref++ ;
            if (ws->next_ref < 0)
              ws->next_ref = ws->TotalFonts + 10 ;

            ws->user_fonts = font ;

          } else
            return PCL_ERROR_INSUFFICIENT_MEMORY ;
        }
      }
      break ;
    } /* switch(action) */
  } /* if(action<2) */
  return FALSE ;
}

/* -------------------------------------------------------------------------- */

int32 pcl_define_symbolset(pfin_ufst5_instance * ws, int32 id,
                           /*@in@*/ /*@notnull@*/ uint8 * data, size_t length)
{
  user_ss * ss, **ptr = &ws->user_symbolsets ;

  /* Find and delete any existing symbolset */
  while ((ss = *ptr) != 0 && ss->id != id)
    ptr = &ss->next ;
  if (ss) {
    *ptr = ss->next ;
    myfree(ss) ;
    SwEvent(EVENT_PCL_SS_DELETED, &id, sizeof(int32)) ;
  }

  /* Build new symbolset */
  ss = (user_ss*) myalloc(USER_SS_LENGTH(length)) ;
  if (ss) {
    uint8 format = data[4], type = data[5] ;
    int32 header_size = OP(data,0,1) ;

    ss->temp   = IS_TEMPORARY ;            /* new definitions are temporary */
    ss->id     = id ;                      /* the PCL symbolset number */
    ss->ref    = ws->next_ss++ ;           /* our unique reference */
    ss->length = length ;                  /* the amount of data */
    ss->first  = OP(data,6,7) ;            /* first code in the map */
    ss->last   = OP(data,8,9) ;            /* last code in the map */
    ss->map    = ss->data + header_size ;  /* pointer to the map (past header) */

    /* HP printers (4250 & 4700) appear to be doing this for QL PCL 5c
       CET 20.01 page 13. i.e. Type zero symbol sets are being treated
       as type 1. */
    if (type == 0)
      type = data[5] = 1 ;

    memcpy(ss->data, data, length) ;       /* the map itself */

    if (ws->next_ss < 0)
      ws->next_ss = (int32)ws->sslistlen + 10 ;

    /* sanity check */
    if (header_size < 18 ||
        (format != 1 && format != 3) || type > 3 ||
        ss->last > 255 || ss->last < ss->first ||
        ss->map + (ss->last-ss->first+1) * 2 > ss->data + ss->length) {
      /* symbolset broken in some way */
      myfree(ss) ;
      return FALSE ;
    }

    ss->next   = ws->user_symbolsets ;     /* link into list */
    ws->user_symbolsets = ss ;

    SwEvent(EVENT_PCL_SS_DEFINED, &id, sizeof(int32)) ;
  }

  return FALSE ;
}

/* -------------------------------------------------------------------------- */

static
HqBool CHECKSUM_N_BYTES(uint8 **ch_ptrptr, uint8 *end_of_data,
                        int32 num_bytes, int32 *checksum)
{
  uint8 *c = *ch_ptrptr ;
  int32 i ;

  for (i=0; i < num_bytes; i++) {
    if (c < end_of_data) {
      *checksum += *c ;
      *checksum %= 256 ;
      c++ ;
    } else {
      return FALSE ; /* Run out of space */
    }
  }

  *ch_ptrptr = c ;
  return TRUE ;
}

static
HqBool CHECKSUM_N_BYTE_INT(uint8 **ch_ptrptr, uint8 *end_of_data,
                           int32 num_bytes, int32 *data_value, int32 *checksum)
{
  uint8 *c = *ch_ptrptr ;
  int32 i ;

  HQASSERT(num_bytes <= 4, "numbytes isgreater than 4!") ;

  *data_value = 0 ;

  for (i=0; i < num_bytes; i++) {
    if (c < end_of_data) {
      *data_value = *data_value << 8 ;
      *data_value += *c ;
      *checksum += *c ;
      *checksum %= 256 ;
      c++ ;
    } else {
      return FALSE ; /* Run out of space */
    }
  }

  *ch_ptrptr = c ;
  return TRUE ;
}

/* Check the checksum for font */
static
HqBool checksum_ok(uint8 header_format, uint8 *data, uint8 *end_of_data)
{
  int32 checksum = 0, data_value, i, seg_id, seg_size ;
  ptrdiff_t remaining_len  ;
  uint8 *chptr = data + 64 ; /* Need to calculate checksum from 64th
                                byte onwards. */

  switch (header_format) {
  case 10: /* Intellifont */
    if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Scale factor */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* X res */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Y res */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Master Underline pos */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Master Underline height */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* OR threshold */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Global Italic angle */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Global IF Data Size */
           CHECKSUM_N_BYTES(&chptr, end_of_data, data_value , &checksum)) ) {       /* Global IF Data */
      return FALSE ;
    }

    break ;
  case 11: /* Intellifont */
    if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Scale factor */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* X res */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Y res */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Master Underline pos */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Master Underline height */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* OR threshold */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Global Italic angle */
           CHECKSUM_N_BYTES(&chptr, end_of_data, 8, &checksum) &&             /* Character Complement */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Global IF Data Size */
           CHECKSUM_N_BYTES(&chptr, end_of_data, data_value , &checksum)) ) { /* Global IF Data */
      return FALSE ;
    }

    break ;
  case 15: case 16: /* TrueType */
    if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Scale factor */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Master Underline pos */
           CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&   /* Master Underline height */
           CHECKSUM_N_BYTES(&chptr, end_of_data, 1, &checksum)           &&   /* Font scaling tech */
           CHECKSUM_N_BYTES(&chptr, end_of_data, 1, &checksum)) ) {           /* Variety */
      return FALSE ;
    }

    {
      ptrdiff_t a_len = (data + 72) - chptr ;
      if (! CHECKSUM_N_BYTES(&chptr, end_of_data, (int32)a_len, &checksum)) /* Additional data */
        return FALSE ;
    }

    /* Segmented Font Data */
    do {
      if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &seg_id, &checksum)) /* Segment ID */
        return FALSE ;
   
      /* Segmented data size field width depends on format. Format 16 (
       * and PCLXL format ) would have 4 byte fields - other PCL5
       * formats have 2 byte fields.
       *
       * Since PCLXL format and format 16 have same size of size field,
       * this is why format 16 used to hold XL type data for some of the
       * "hybrid" fonts we've seen.
       */

      if (header_format == 16) {
        if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 4, &seg_size, &checksum)) /* Segment ID */
          return FALSE ;
      } else {
        if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &seg_size, &checksum)) /* Segment ID */
          return FALSE ;
      }

      switch (seg_id) {

      case 'V' << 8 | 'R' : /* VR vertical rotation */
        if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 1, &data_value, &checksum)) /* format */
          return FALSE ;
        if (data_value != 0)
          return FALSE ;
        if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 1, &data_value, &checksum)) /* substitute string */
          return FALSE ;
        if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum)) /* descender value */
          return FALSE ;
        break ;

      case 'G' << 8 | 'C' : /* GC Gallery characters. */
        if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum)) /* format */
          return FALSE ;
        if (data_value != 0)
          return FALSE ;
        if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum)) /* default galley char */
          return FALSE ;
        if (! CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum)) /* region count */
          return FALSE ;
        if (data_value != ((seg_size - 6) / 6))
          return FALSE ;

        i = 0 ;
        while (i++ < data_value) {
          int32 unused ;
          if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &unused, &checksum) &&
                 CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &unused, &checksum) &&
                 CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &unused, &checksum)) ) /* GC region */
            return FALSE ;
        }
        break;

      case 'C' << 8 | 'E' : /*CE character enhancements. */
        if (seg_size != 8) {
          if (! CHECKSUM_N_BYTES(&chptr, end_of_data, seg_size, &checksum)) /* Additional data */
            return FALSE ;
        }

        if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 4, &data_value, &checksum) &&
               CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&
               CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum)) )
          return FALSE ;
        break;
      
      case (0x5654): /* VT 22100 */
        {
          int32 glyph1;
          int32 glyph2;
          int32 local_seg_size = seg_size;

          while (local_seg_size > 0) {
            if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &glyph1, &checksum) &&
                   CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &glyph2, &checksum)) )
              return FALSE ;

            local_seg_size -= 4 ;
        
            if (glyph1 == 0xFFFF || glyph2 == 0xFFFF) { /* res is success */
              if (local_seg_size > 0) {
                (void)CHECKSUM_N_BYTES(&chptr, end_of_data, local_seg_size, &checksum) ;
              }
              break ;
            }
          }
        }
        break;

      case (0x5645): /* VE 22085 */
        {
          int32 range_count;

          if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 1, &range_count, &checksum) &&
                 CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 1, &data_value, &checksum)) )
            return FALSE ;

          while (range_count > 0) {
            if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&
                   CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum)) )
              return FALSE ;
          }
        }
        break;

      case ( ('B' << 8) | 'R' ): /* bitmap resoltion from PCLXL
                                  bitmaps, but sometimes seen in
                                  format16 headers.*/
        if (! (CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum) &&
               CHECKSUM_N_BYTE_INT(&chptr, end_of_data, 2, &data_value, &checksum)) )
          return FALSE ;
        break;

      default:
        if (! CHECKSUM_N_BYTES(&chptr, end_of_data, seg_size, &checksum))
          return FALSE ;
        break ;
      }

    } while (seg_id != 65535 && seg_size != 0) ;
    break ;

  default:
    return TRUE ;
  }

  /* COPYRIGHT */
  switch (header_format) {
  case 10: case 11: /* Intellifont */
    remaining_len = end_of_data - chptr ;
    if (remaining_len > 2) { /* more than 2 bytes left */
      if (! CHECKSUM_N_BYTES(&chptr, end_of_data, (int32)(remaining_len - 2), &checksum)) /* Copyright */
        return FALSE ;
    }
    break ;

  default:
    break ;
  }

  /* CHECKSUM */
  switch (header_format) {
  case 10: case 11: /* Intellifont */
  case 15: case 16: /* TrueType */
    if (! CHECKSUM_N_BYTES(&chptr, end_of_data, 1, &checksum)) /* Reserved */
      return FALSE ;

    if (chptr < end_of_data) {
      if ( (256 - checksum) != *chptr) /* Checksum mismatch*/
        return FALSE ;

      chptr++ ;
      HQASSERT(chptr == end_of_data, "We have not consumed all the checksum data?") ;
    } else {
      return FALSE ; /* Ran out of room for checksum */
    }
    break ;

  default:
    break ;
  }

  return TRUE ;
}

/* Define PCL font header */

int32 pcl_define_font(pfin_ufst5_instance * ws,
                      int32 id, /*@in@*/ sw_datum * idstr,
                      /*@in@*/ /*@notnull@*/ uint8 * data, size_t length)
{
  user_font * font, **ptr = &ws->user_fonts ;
  size_t idlen = 0 ;
  font_header * header = (font_header *) data ;
  int size = 0, len = (int) length ;
  UW16 gifct = 0 ;
  int error = FALSE ;
  float pitch = -1, height = -1, hmi = 0.6f, offset = -0.3f, thick = 0.3f ;
  uint8 type = 0, bound = 0, broken = NOT_BROKEN ;
  PCL_SSINFO * ssList = ws->psslist ;
  int ss = 0, xres = 300, yres = 300, scale = INTELLIFONT ;
  uint32 number = 0 ;
  uint16 GC_def_ch = 0xFFFF ;
  int8 encoding = UNKNOWN_CHAR_CODES ;

  /* XL fonts and PCL alphanumeric IDs - id is dataformat if idstr != 0. */
  if (idstr && idstr->type == SW_DATUM_TYPE_STRING)
    idlen = idstr->length ;

  /* Check data size */
  if (data == 0 || length > INT_MAX ||
      (size = OP(data,sizeO,sizeP)) > len) {

    error = PCL_ERROR_ILLEGAL_FONT_DATA ;

  } else {
    /* Check format */
    switch (header->format) {
    case 0: case 20: /* Bitmap */
      if (size < 64)
        error = PCL_ERROR_ILLEGAL_FONT_DATA ;
      else {
        type  = TYPE_BITMAP ;
        ss    = OP(data,symbolsetO,symbolsetP) ;
        bound = data[typeO] ;

        if (header->format == 20) {
          xres = OP(data,xresO,xresP) ;
          yres = OP(data,yresO,yresP) ;
        }

        pitch  = ((float)OPQ(data,pitchO,pitchP,pitchQ)) / 1024.0f ;
        height = ((float)OPQ(data,heightO,heightP,heightQ)) / 1024.0f ;
        offset = (float)data[underline_positionO] ;
        thick  = (float)data[underline_thicknessO] ;

        if (offset > 127)
          offset -= 256 ;

        offset = (height == 0) ? 0 : offset / height ;/* height = design size */
        thick  = (height == 0) ? 0 : thick / height ;

        pitch  = (pitch == 0) ? 0 : xres / pitch ;
        height = (yres == 0) ? 0 : 72.0f * height / yres ;

        hmi    = pitch * height ;
        hmi    = (hmi == 0) ? 0.6f : 72.0f / hmi ;

        if        (!memcmp(data + nameO, "Uncial          ", 16)) {
          broken = BROKEN_UNCIAL ;
        } else if (!memcmp(data + nameO, "Dom Casual      ", 16)) {
          broken = BROKEN_DOM_CASUAL ;
        } else if (!memcmp(data + nameO, "Park Avenue     ", 16)) {
          broken = BROKEN_DOM_CASUAL ;
        } else if (!memcmp(data + nameO, "ITCLublnGrph  Bk", 16)) {
          broken = BROKEN_ITCLUBLNGRPH ;
        }
      }
      break ;

    case 11: /* Intellifont */
      encoding = MSL_CHAR_CODES ;
    case 10:
      if (size + 2 > len ||
          (gifct = OP(data,size-2,size-1)) + size > len)
        error = PCL_ERROR_ILLEGAL_FONT_DATA ;
      else {
        uint8 *end_of_data = data + length ;

        if (! checksum_ok(header->format, data, end_of_data))
          return FALSE ;

        type   = TYPE_IF ;
        scale  = OP(data,scaleO,scaleP) ;
        ss     = OP(data,symbolsetO,symbolsetP) ;
        bound  = data[typeO] ;

        height = (float)OP(data,heightO,heightP) / 8.0f ;
        offset = (float)OP(data,master_positionO,master_positionP) ;
        thick  = (float)OP(data,master_thicknessO,master_thicknessP) ;
        hmi    = (float)OP(data,pitchO,pitchP) ;

#if 0
        xres = OP(data,xresIFO,xresIFP) ;
        yres = OP(data,yresIFO,yresIFP) ;
#endif

        if (offset > 32767)
          offset -= 65536 ;

        if (scale != 0) {
          hmi    = hmi / scale * 72 / 72.307f ;  /* INTELLIFONT point size */
          offset = offset / scale ;
          thick  = thick / scale ;
        } else {
          hmi    = 0.6f ;
          offset = 0 ;
          thick  = 0 ;
        }

        height = -1 ;
      }
      break ;

    case 15: case 16: /* TrueType */
      if (size < 72)
        error = PCL_ERROR_ILLEGAL_FONT_DATA ;
      else { /* Check that this is a TrueType */
        font_header_15 * header15 = (font_header_15 *) data ;
        if (header15->technology != 1 &&
            header15->technology != 254)
          error = PCL_ERROR_ILLEGAL_FONT_HEADER_FIELDS ;
        else {
#if 0
          /* Its still not clear if TrueType fonts checksum is
             actually checked. This still needs investigation. */
          uint8 *end_of_data = data + length ;
#endif

          int designsize = 2048 ;
          type = (header->format == 15) ?                 TYPE_TT :
                 (idlen == 0 || id == PCL_ALPHANUMERIC) ? TYPE_TT16 :
                                                          TYPE_XL ;

#if 0
          /* Its still not clear if TrueType fonts checksum is
             actually checked. This still needs investigation. */
          if (! checksum_ok(header->format, data, end_of_data))
            return FALSE ;
#endif

          encoding = UNICODE_CHAR_CODES ;

          if (type == TYPE_XL) {
            uint8 * ptr = data + size + 8 ;
            uint16  seg ;
            uint32  len ;
            int     found = 256 ;
            size_t  remainder = (length - size - 8);

            scale = 512 ;

            while (error == 0 && found > 255 && remainder > 0) {
              if ( remainder < 6) {
                /*
                 * There are less than 6 bytes of data left
                 * We therefore cannot extract the segment type and length
                 * To force the loop to exit we set "found" to 0
                 * (and remainder to 0 too)
                 */

                remainder = 0;
                found &= 255;
              } else {
                /*
                 * Yes there are at least 6 bytes of data left
                 * So we can go ahead and extract the 2-byte segment type
                 * and the 4-byte segment length
                 * (and decrement the remainder by 6)
                 */
                seg = OP(ptr,0,1) ;
                len = OPQR(ptr,2,3,4,5) ;
                remainder -= 6;

                switch (seg) {
                case 'B' << 8 | 'R': /* Bitmap Resolution segment */
                  if ( (remainder < len) || (len != 4) )
                    error = PCL_ERROR_ILLEGAL_BITMAP_RESOLUTION_SEGMENT ;
                  else {
                    xres = OP(ptr,6,7) ;
                    yres = OP(ptr,8,9) ;
                    type = TYPE_XLBITMAP ;
                    found |= 1 ;

                    /* Check the resolution is valid */
                    if (xres != yres)
                      yres = 0 ;
                    switch (yres) {
                    case 300:
                    case 600:
                    case 1200:
                      break ;
                    default:
                      error = PCL_ERROR_ILLEGAL_BITMAP_RESOLUTION_SEGMENT ;
                    }
                  }
                  break ;

                case 'G' << 8 | 'T' : /* Global TrueType segment */
  #ifndef NDEBUG
                  if ( (remainder < len) || (len < 4) )
                    error = PCL_ERROR_ILLEGAL_GLOBAL_TRUE_TYPE_SEGMENT ;
                  else {
                    uint32 sfntvers = OPQR(ptr,6,7,8,9) ;
                    HQASSERT(((sfntvers == 0x00010000) ||  /* Mandated value */
                              (sfntvers == 0x00000000) ||  /* Seen in SWI driver jobs */
                              (sfntvers == 0x74746366)),   /* Seen in MS Word jobs */
                             "Unexpected sfntvers value - please report job to core group");
                    /* It seems no one checks the sfntvers values these days.  If
                     * we need to check it again then change this case to a test
                     * for the valid values and set error as follows if the check
                     * fails.
                      error = PCL_ERROR_ILLEGAL_GLOBAL_TRUE_TYPE_SEGMENT ;
                     */
                  }
  #endif /* !NDEBUG */
                  found |= 2 ;
                  break ;

                case 'G' << 8 | 'C' : /* Galley Character segment */
                  if ( (remainder < len) || ((len % 6) != 0) || (len < 6) || (OP(ptr,6,7) != 0) )
                    error = PCL_ERROR_ILLEGAL_GALLEY_CHARACTER_SEGMENT ;
                  else {
                    GC_def_ch = OP(ptr,8,9);
                    found |= 4 ;
                  }
                  break ;

                case 'V' << 8 | 'E' : /* Vertical Exclude segment */
                  if ( (remainder < len) || (((len + 2) & 3) != 0) || (ptr[6] != 0) )
                    error = PCL_ERROR_ILLEGAL_VERTICAL_TX_SEGMENT ;
                  else
                    found |= 8 ;
                  break ;

                case 'V' << 8 | 'I' : /* Vendor Information segment */
                  /* Should we check the length? */
                  break ;

                case 'V' << 8 | 'R' : /* Vertical Rotation segment */
                  if ( (remainder < len) || (len != 4) || (OP(ptr,6,7) != 0) )
                    error = PCL_ERROR_ILLEGAL_VERTICAL_TX_SEGMENT ;
                  else
                    found |= 16 ;
                  break ;

                case 'V' << 8 | 'T' : /* Vertical Transformation segment */
                  if ((len & 3) != 0 || len < 4)
                    error = PCL_ERROR_ILLEGAL_VERTICAL_TX_SEGMENT ;
                  else
                    found |= 32 ;
                  break ;

                case 0xFFFF:
                  if (len != 0)
                    error = PCL_ERROR_ILLEGAL_NULL_SEGMENT_SIZE ;
                  found &= 255 ;
                  break ;

                default:
                  error = PCL_ERROR_ILLEGAL_FONT_SEGMENT ;
                  break ;
                }
                ptr += 6 + len ;
                remainder -= len;
              }
            }

            if (error == 0) {
              if (type == TYPE_XLBITMAP) {
                if ((found & 257) != 1)
                  error = PCL_ERROR_MISSING_REQUIRED_SEGMENT ;
                else if (found != 1)
                  error = PCL_ERROR_ILLEGAL_FONT_SEGMENT ;
              } else {
                /* XL TT */
                if ((found & 258) != 2)
                  error = PCL_ERROR_MISSING_REQUIRED_SEGMENT ;
                else if ((found & 257) != 0)
                  error = PCL_ERROR_ILLEGAL_FONT_SEGMENT ;
              }
            }

          } else {
            int found = 256 ;

            /* For PCLETTO objects, we need to search segmented data for
             * the font for details of bitmap resolution and/or default
             * galley char, if appropriate.
             */

            if ( len > size ) {
              uint8 *seg_ptr = data + size ;
              uint16 seg_type ;
              uint32 seg_len ;
              size_t remainder = (length - size);
              uint32 min_seg_size = header->format == 15 ? 4 : 6 ;

              while (error == 0 && found > 255 && remainder > 0) {
                if ( remainder < min_seg_size ) {
                  /*
                   * There are less than 6 bytes of data left
                   * We therefore cannot extract the segment type and length
                   * To force the loop to exit we set "found" to 0
                   * (and remainder to 0 too)
                   */

                  remainder = 0;
                  found &= 255;
                } else {
                  /*
                   * Yes there are at least 6 bytes of data left
                   * So we can go ahead and extract the 2-byte segment type
                   * and the 4-byte segment length
                   * (and decrement the remainder by 6)
                   */
                  seg_type = OP(seg_ptr,0,1) ;
                  seg_len = header->format == 15 ?
                              OP(seg_ptr,2,3) : OPQR(seg_ptr,2,3,4,5) ;
                  remainder -= min_seg_size;

                  switch (seg_type) {
                    case 'B' << 8 | 'R' : /* Bitmap resolution segment */
                      if ( (remainder < seg_len) || (seg_len != 4) )
                        error = PCL_ERROR_ILLEGAL_BITMAP_RESOLUTION_SEGMENT ;
                      else {
                        xres = OP(seg_ptr,6,7);
                        yres = OP(seg_ptr,8,9);

                        found |= 1;

                        /* check the resolution is valid. */
                        if (xres != yres)
                          yres = 0 ;
                        switch (yres) {
                        case 300:
                        case 600:
                        case 1200:
                          break ;
                        default:
                          error = PCL_ERROR_ILLEGAL_BITMAP_RESOLUTION_SEGMENT ;
                        }
                      }
                      break;

                    case 'V' << 8 | 'I' : /* Vendor Information segment */
                      /* Should we check the segment length? */
                      break ;

                   case 'G' << 8 | 'C' :
                      GC_def_ch = OP(seg_ptr,8,9);
                      found |= 4 ;
                      break;

                    case 0xFFFF: /* null segment. */
                      if (seg_len != 0)
                        error = PCL_ERROR_ILLEGAL_NULL_SEGMENT_SIZE ;

                      found &= 255;
                      break;

                    default:
                      break;
                  }

                  seg_ptr += min_seg_size + seg_len ;
                  remainder -= seg_len;

                  if ( seg_ptr > data + len )
                    error = PCL_ERROR_ILLEGAL_FONT_SEGMENT ;
                    /* run off end of data somehow. */
                }
              }

            }

            if (header15->technology == 254) {

              /* is it actually an error to be missing the bitmap res
               * segment? */
              if ( (error == 0) && ((found & 257) != 1) )
                error = PCL_ERROR_MISSING_REQUIRED_SEGMENT ;

              type |= TYPE_BITMAP ;  /* Note, resolution unspecified */
              pitch  = ((float)OPQ(data,pitchO,pitchP,pitchQ)) / 1024.0f ;
              height = ((float)OPQ(data,heightO,heightP,heightQ)) / 1024.0f ;

              pitch  = (pitch == 0) ? 0 : xres / pitch ;
              height = (yres == 0) ? 0 : 72.0f * height / yres ;

              /* Leave the hmi, offset and thickness of underline to be
               * calculated as per normal true type route.
               * \todo @@@ TODO this needs checking for correctness. It may
               * be we need to apply the bitmap criteria.
               */
            } else {
              scale = 512 ;
            }

            ss    = OP(data,symbolsetO,symbolsetP) ;
            bound = data[typeO] ;

            offset = (float)OP(data,master_positionTTO,master_positionTTP) ;
            thick  = (float)OP(data,master_thicknessTTO,master_thicknessTTP) ;
            hmi    = ((float)OPQ(data,pitchO,pitchP,pitchQ)) / 256.0f ;

            if (offset > 32767)
              offset -= 65536 ;

            if (type >= TYPE_TT16) {
              designsize = 256 ;
            } else {
              designsize = OP(data,scaleO,scaleP) ;
            }

            hmi    = hmi    / designsize ;
            offset = offset / designsize ;
            thick  = thick  / designsize ;

            /* Workaround to avoid UFST crashing with some fonts */
            if      (!memcmp(data + nameO, "Revue       Shdw", 16))
              broken = BROKEN_REVUE ;
            else if (!memcmp(data + nameO, "OldEnglish      ", 16))
              broken = BROKEN_OLDENGLISH ;
          }
        }
      }
      break ;

    default:
      error = PCL_ERROR_ILLEGAL_FONT_HEADER_FIELDS ;
    }

    if (error == 0) {
      /* For HP4700 emulation */
      number = OPQR(data,numberO,numberP,numberQ,numberR) ;

      /* See PCL5 Tech Ref pg 11-24 */
      if (bound >= 10 && ss != 56)
        error = PCL_ERROR_ILLEGAL_FONT_DATA ;
    }
  }

  /* Find and delete any existing font */
  font = finduserfont(id, idstr, &ptr) ;
  if (font) {
    if (type >= TYPE_XL)  /* Can't redefine XL fonts */
      error = PCL_ERROR_FONT_NAME_ALREADY_EXISTS ;
    else {                /* Can redefine PCL fonts */
      *ptr = font->next ;
      fontfree(ws,font) ;
    }
  }

  if (FALSE && ws->emulate4700 && number &&
      type < TYPE_XL && (type & TYPE_BITMAP) == 0) {
    /* Also delete any soft fonts that have the same "font number".
     *
     * This behaviour is clearly not expected by some QL tests, but is the
     * only explanation for observed output. Having said that, different
     * resolutions/sizes of the same bitmap font are allowed, so only do this
     * for scaleables.
     *
     * XL fonts are excused because they have a fake header.
     */
     ptr = &ws->user_fonts ;
     while ((font = *ptr) != 0) {
       if (font->proxy == -1 &&
           font->type < TYPE_XL &&
           font->number == number) {
         *ptr = font->next ;
         fontfree(ws,font) ;
       } else
         ptr = &font->next ;
     }
  }

  if (error)  /* Note we delete existant fonts first! */
    return error ;

  { /* Raise an event that says we're about to define a font.
     *
     * This is sent as a special case of EVENT_PCL_FONT_DELETED simply because
     * it is so closely related - it means that we've finished deleting and are
     * about to add a font, so any pending Select By Criteria should be done
     * now.
     */
    int32 minusone = -1 ;

    SwEvent(EVENT_PCL_FONT_DELETED, &minusone, sizeof(int32)) ;
  }

  /* Work around for UFST PCLswapHdr() 'unexpected behaviour' - avoid short headers.
   *
   * We pretend the header is at least 80 bytes for allocation purposes, and
   * if there's an XL idstr we copy it 80 bytes after the start.
   */
  if (len < 80)
    len = 80 ;

  /* Build new font header */
  font = (user_font*) myalloc(USER_FONT_LENGTH(len + idlen)) ;
  if (!font) {
    return PCL_ERROR_INSUFFICIENT_MEMORY ;
  } else {
    int i ;

    font->next   = ws->user_fonts ;
    font->temp   = (id == -1) ? IS_PROTECTED : IS_TEMPORARY ;
    font->id     = id ;
    font->proxy  = -1 ;
    font->ss     = (bound <= BOUNDFONTTYPE) ? ss : -1 ;

    font->broken = broken ;

    font->type   = type ;
    font->hmi    = hmi ;
    font->pitch  = pitch ;
    font->height = height ;
    font->offset = offset ;
    font->thick  = thick ;
    font->xres   = xres ;
    font->yres   = yres ;
    font->scale  = scale ;
    font->number = number ;
    font->typeface = OP(data,typefaceO,typefaceP) ;
    font->style    = OP(data,styleO,styleP) ;
    font->fh_ss    = OP(data,symbolsetO,symbolsetP) ;
    font->GC_def_ch = (GC_def_ch == 0xFFFF ? TRUE : FALSE ) ;

    if (header->format == 11) {
      /* Format 11 has the character complement data */
      font_header_11 * header11 = (font_header_11 *) header ;
      for (i=0; i<8; i++)
        font->chrComp[i] = header11->complement[i] ;

      if ((font->chrComp[7] & 0x1) == 0) {
        encoding = UNICODE_CHAR_CODES ;
      } else {
        encoding = MSL_CHAR_CODES ;
      }

    } else {
      /* Otherwise we have to look up the symbolset */
      int32 ss = OP(data,symbolsetO,symbolsetP) ;
      uint8* chrReq ;
      user_ss * userss = 0 ;

      ss = find_symbolset(ws, ss, &userss) ;

      if (userss) {
        chrReq = & userss->data[10] ;
        for (i=0; i<8; i++)
          font->chrComp[i] = 255 - chrReq[i] ;
      } else {

        for (i=0; i<8; i++)
          font->chrComp[i] = 255 ;

        if (encoding == MSL_CHAR_CODES) {
          chrReq = ssList[ss].msl_chrReq ;
          font->chrComp[0] = 255 - chrReq[0] ;
          font->chrComp[3] = 255 - chrReq[1] ;
          font->chrComp[7] = 255 - chrReq[2] ;
        } else {
          chrReq = ssList[ss].unicode_chrReq ;
          font->chrComp[4] = 255 - chrReq[0] ;
          font->chrComp[5] = 255 - chrReq[1] ;
          font->chrComp[7] = 255 - chrReq[2] ;
        }
      }
    }

    font->encoding = encoding ;

    for (i = 0; i < USER_GLYPH_HEADS; i++)
      font->glyphs[i] = NULL ;
    for (i = 0; i < USER_ID_HEADS; i++)
      font->ids[i] = NULL ;
    font->uids   = NULL ;

    font->length = len ;
    font->idlen  = idlen ;
    font->ref    = ws->next_ref++ ;
    if (ws->next_ref < 0)
      ws->next_ref = ws->TotalFonts + 100 ;

    if (id == -1 && ws->LinePrinter == -1 && OP(data,typefaceO,typefaceP) == 0)
      ws->LinePrinter = font->ref ;

    /* XL faked font header handling - lose 8 byte XL header from data */
    if (type >= TYPE_XL) {

      memcpy(font->data, data, size) ;
      memcpy(font->data + size, data + size + 8, length - size - 8) ;

      /* Copy Orientation and Font Scaling Technology from 8 byte XL header */
      if (size > technologyO)
        font->data[technologyO] = data[size + 4] ;  /* fst */
      if (size > orientO)
        font->data[orientO]     = data[size + 1] ;  /* orientation */

    } else
      memcpy(font->data, data, length) ;     /* Only copy actual header data */


#if (BYTEORDER == LOHI)
    if (font->type < TYPE_TT) {
      /* Safe to call UFST's own endian converter */
      ws->cgif->pfnPCLswapHdr(font->data, gifct);
    }
#endif
    if (idlen)
      memcpy(font->data + len, idstr->value.string, idlen) ;

    ws->user_fonts = font ;
  }

  return FALSE ;
}

/* -------------------------------------------------------------------------- */

int32 pcl_define_glyph(pfin_ufst5_instance * ws,
                       int32 id, /*@in@*/ sw_datum * idstr, int32 code,
                       /*@in@*/ /*@notnull@*/ uint8 * data, size_t length)
{
  user_font * font, **ptr = &ws->user_fonts ;
  int error = FALSE ;
  int glyphid = -1 ;
  user_glyph * glyph ;

  /* Find the font */
  font = finduserfont(id, idstr, &ptr) ;
  if (!font)
    return PCL_ERROR_FONT_UNDEFINED ;

  if (font->proxy > -1)  /* Can't add glyphs to proxied internal fonts */
    return PCL_ERROR_CANNOT_REPLACE_CHARACTER ;

  if (length < 4) {
    error = PCL_ERROR_ILLEGAL_CHARACTER_DATA ;
  } else {
    /* Sanity check the glyph definition and extract glyphid from TTs */
    switch (data[0]) {
    case 4: { /* Bitmap */
        chr_header_bitmap * chr = (chr_header_bitmap *) data ;

        if (!(font->type & TYPE_BITMAP) )
          error = PCL_ERROR_FST_MISMATCH ;
        else if (chr->Class != 1 && chr->Class != 2)
          error = PCL_ERROR_UNSUPPORTED_CHARACTER_CLASS ;
        else if (length < CHR_HEADER_BITMAP_SIZE ||
                 chr->size < 14)
          error = PCL_ERROR_ILLEGAL_CHARACTER_DATA ;
        }
      break ;

    case 10: { /* Intellifont */
        chr_header_intellifont_4 * chr = (chr_header_intellifont_4 *) data ;

        if (font->type != TYPE_IF)
          error = PCL_ERROR_FST_MISMATCH ;
        else if (chr->Class != 3 && chr->Class != 4)
          error = PCL_ERROR_UNSUPPORTED_CHARACTER_CLASS ;
        else if (length < CHR_HEADER_INTELLIFONT_3_SIZE ||
                 chr->size < 2 ||
                 (chr->Class == 4 && length < CHR_HEADER_INTELLIFONT_4_SIZE))
          error = PCL_ERROR_ILLEGAL_CHARACTER_DATA ;
        }
      break ;

    case 15: { /* TrueType */
        chr_header_truetype * chr = (chr_header_truetype *) data ;

        if (font->type < TYPE_TT || font->type > TYPE_TT16)
          error = PCL_ERROR_FST_MISMATCH ;
        else if (length < sizeof(chr_header_truetype) || chr->size < 2)
          error = PCL_ERROR_ILLEGAL_CHARACTER_DATA ;
        else if (chr->Class != 15)
          error = PCL_ERROR_UNSUPPORTED_CHARACTER_CLASS ;
        else {
          glyphid = OP(data,6,7) ;
        }
      }
      break ;

    case 1: { /* XL truetype */
        chr_header_truetype * chr = (chr_header_truetype *) data ;

        if (font->type != TYPE_XL)
          error = PCL_ERROR_FST_MISMATCH ;
        else if (length < 6)
          error = PCL_ERROR_ILLEGAL_CHARACTER_DATA ;
        else {
          switch (chr->continuation) {  /* actually Class */
          case 0: glyphid =  4 ; break ;
          case 1: glyphid =  8 ; break ;
          case 2: glyphid = 10 ; break ;
          default: error = PCL_ERROR_UNSUPPORTED_CHARACTER_CLASS ;
          }
          if (glyphid > -1)
            glyphid = OP(data,glyphid,glyphid+1) ;
        }
      }
      break ;

    case 0: /* XL bitmap */
      if (font->type != TYPE_XLBITMAP)
        error = PCL_ERROR_FST_MISMATCH ;
      else if (length < 10)
        error = PCL_ERROR_ILLEGAL_CHARACTER_DATA ;
      else if (data[1] != 0) /* Class must be zero */
        error = PCL_ERROR_UNSUPPORTED_CHARACTER_CLASS ;
      break ;

    default:
      error = PCL_ERROR_UNSUPPORTED_CHARACTER_FORMAT ;
    }
  }

  /* Find and delete any existing glyph */
  glyph = font->glyphs[code & USER_GLYPH_MASK] ;
  while (glyph && (glyph->code != code ||
                   (code == TT_COMPONENT && glyph->id != glyphid)))
    glyph = glyph->next ;
  if (glyph)
    glyphfree(ws, font, glyph) ;

#if USE_METRICS_CACHE
  { /* As we are supplying a glyph that may previously have been used while
       undefined, remove all such missing glyphs from the metrics cache. */
    uidcache * uid ;
    metricscache * metrics ;
    int i ;
    for (uid = font->uids ; uid != 0 ; uid = uid->next)
      if (uid->missing) {
        uid->missing = FALSE ;
        for (metrics = uid->metrics ; metrics != 0 ; metrics = metrics->next)
          for (i = 0 ; i < METRICSLENGTH; i++)
            if (metrics->widths[i] == METRICS_MISSING)
              metrics->widths[i] = METRICS_UNKNOWN ;
      }
  }
#endif

  /* We return an error AFTER deleting an existing glyph - is this right? */
  if (error)
    return error ;

  /* Store new glyph */
  glyph = (user_glyph*) myalloc(USER_GLYPH_LENGTH(length)) ;
  if (glyph) {
    uint16 * p = (uint16 *) glyph->data ;
    int i ;

    glyph->next     = font->glyphs[code & USER_GLYPH_MASK] ;
    glyph->nextbyid = (glyphid < 0) ? 0 : font->ids[glyphid & USER_ID_MASK] ;
    glyph->code     = code ;
    glyph->id       = glyphid ;
    glyph->length   = length ;
    memcpy(glyph->data, data, length) ;
#if (BYTEORDER == LOHI)
    /* PCLswapChar should be enough, but UFST 'unexpected behaviour' means it
       doesn't convert bitimage glyphs, so in that case we do so ourself.
     */
    switch (glyph->data[0]) {

    case 10: /* Intellifont */
      ws->cgif->pfnPCLswapChar( FSP glyph->data );
      break ;

    case 4: /* Bitmap glyph */
      for (i = 3; i < 8; i++) {
        uint16 w = p[i] ;
        p[i] = (w << 8) | (w >> 8) ;
      }
      break ;

    case 0: /* XL Bitmap */
      for (i = 1; i < 5; i++) {
        uint16 w = p[i] ;
        p[i] = (w << 8) | (w >> 8) ;
      }
      break ;
    }
#endif

    font->glyphs[code & USER_GLYPH_MASK] = glyph ;
    if (glyphid > -1)
      font->ids[glyphid & USER_ID_MASK] = glyph ;
  }
  return FALSE ;
}

/* -------------------------------------------------------------------------- */
/* Parse a PCL5 data stream containing a soft font - for LinePrinter really.

   This is very simple so cannot cope with continuations
 */

void define_pcl5_font(pfin_ufst5_instance * ws,
                      uint8 * pcl5, size_t length)
{
  int glyph = -1, font = 0 ;
  uint8 * end = pcl5 + length ;

  while (pcl5 + 4 < end) {
    int param = 0, which ;
    int command = (pcl5[0]<<16) | (pcl5[1]<<8) | pcl5[2] ;
    uint8 c = 0 ;

    switch (command) {
      case 0x1B2973: /* <esc>)s###W Define font */
        which = 0 ;
        break ;
      case 0x1B2A63: /* <esc>*c###E Glyph ID */
        which = 1 ;
        break ;
      case 0x1B2873: /* <esc>(s###W Define glyph */
        which = 2 ;
        break ;
      default:
        /* garbage in PCL stream... bail out now */
        return ;
    }
    pcl5 += 3 ;

    /* now read the parameter */
    while (pcl5 < end && (c = *pcl5++) >= '0' && c <= '9')
      param = param * 10 + c - '0' ;
    if (param > 65535)
      param = 65535 ; /* It is so defined */

    if (which == 1) {
      /* The ID of the next glyph */
      if (c != 'E')
        return ;
      glyph = param ;

    } else {
      /* Define font/glyph */
      if (c != 'W' || pcl5 + param > end)
        return ;

      if (param > 1) {
        if (which == 0) {
          /* Lineprinter has the following symbolsets within it so
             build up character complement bytes. 10U, 11U, 1U, 8U,
             0N, 2N, 5N, 6N, 9N, 12U */
          /*
          uint8 c4 = ~ (0xC0 | 0x82 | 0xC2 | 0xC4 | 0xA0 | 0x94 | 0x80) ;
          uint8 c5 = ~ (0x40 | 0x10) ;
          uint8 c7 = ~ 0x1 ;
          */
          static uint8 chcomp[8] = {0,0,0,0,9,175,0,254} ;
          uint8 *chrComp ;
          int i ;

          pcl_define_font(ws, -1, 0, pcl5, param) ;

          chrComp = ws->user_fonts->chrComp ;
          for (i=0; i<8; i++)
            chrComp[i] = chcomp[i] ;

          font = TRUE ;

        } else if (font && glyph > -1) {

          if (pcl5[1] != 0)
            return ; /* For now, fault continuations */

          pcl_define_glyph(ws, -1, 0, glyph, pcl5, param) ;
        }
      }

      pcl5 += param ;
    }
  }
  return ;
}

/* ========================================================================== */
/* A font specification - find the best match and return it

   > PCL_MISCOP_SPECIFY
     symbolset
     spacing
     pitch
     height
     style
     weight
     typeface
     exclude bitmaps
     print resolution
     [ bold ] (optional param)

   < (fontname) of the form (PCL:###:###)
     fontsize
     HMI
     symbolset type
     spacing
     underline offset
     underline thickness
     ref
     bitmapped
*/
static char fontname[32] = "PCL:00000:00000:00000" ;

static
sw_pfin_result pcl_miscop_specify(sw_pfin_instance * pfin,
                                  sw_datum ** param, sw_datum * a)
{
  pfin_ufst5_instance * ws = (pfin_ufst5_instance *) pfin ;
  size_t length = (*param)->length ;
  int32  fontnamelen = 0 ;

  static sw_datum reply[10] = {
    SW_DATUM_ARRAY(reply+1,9),
    SW_DATUM_STRING(""),           /* fontname */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* fontsize */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* HMI */
    SW_DATUM_INTEGER(0),           /* symbolsettype */
    SW_DATUM_INTEGER(0),           /* spacing */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* underline offset */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* underline thickness */
    SW_DATUM_INTEGER(0),           /* soft font ref (or -1) */
    SW_DATUM_BOOLEAN(FALSE)        /* bitmapped */
  };

  enum { /* enum for the above array */
    r_array = 0, r_name, r_size, r_hmi, r_sstype, r_spacing,
    r_offset, r_thick, r_ref, r_bitmap
  } ;

  enum { /* parameter indices */
    p_reason = 0, p_ss, p_space, p_pitch, p_height, p_style, p_weight, p_font,
    p_exclude, p_print_resolution, p_bold, max_param_length
  } ;

  int32 symbolset, spacing, style, weight, typeface, exclude, ref = -1 ;
  int32 symbolsettype = 0, fixedpitch, bitmapped, bold = 0 ;
  int32 embolden = 0, faux = 0, print_resolution = 0 ;
  float pitch, height, offset, thick ;
  float fontsize = 0.0f, hmi = 0.0f ;
  int   best ;
  fco_font * font ;
  TTFONTINFOTYPE * fontInfo ;
  user_font * user = 0 ;

  if ((*param)->owner) {
    /* Do things the official way for opaque datums */
    const sw_data_api * data = pfin->data_api ;

    if ((length != max_param_length - 1 && length != max_param_length) ||
        !get_int(data,*param,p_ss,&symbolset) ||
        !get_int(data,*param,p_space,&spacing)   ||
        !get_flt(data,*param,p_pitch,&pitch)     ||
        !get_flt(data,*param,p_height,&height)    ||
        !get_int(data,*param,p_style,&style)     ||
        !get_int(data,*param,p_weight,&weight)    ||
        !get_int(data,*param,p_font,&typeface)  ||
        !get_bool(data,*param,p_exclude,&exclude)   ||
        !get_int(data,*param,p_print_resolution,&print_resolution)   ||
        (length == max_param_length && !get_int(data,*param,p_bold,&bold)))
      return SW_PFIN_ERROR_SYNTAX ;
  } else {
    /* For raw datums, a already points at the first member. */
    HQASSERT((length == max_param_length - 1 || length == max_param_length) &&
             a[p_ss     ].type == SW_DATUM_TYPE_INTEGER &&
             a[p_space  ].type == SW_DATUM_TYPE_INTEGER &&
             a[p_pitch  ].type == SW_DATUM_TYPE_FLOAT   &&
             a[p_height ].type == SW_DATUM_TYPE_FLOAT   &&
             a[p_style  ].type == SW_DATUM_TYPE_INTEGER &&
             a[p_weight ].type == SW_DATUM_TYPE_INTEGER &&
             a[p_font   ].type == SW_DATUM_TYPE_INTEGER &&
             a[p_exclude].type == SW_DATUM_TYPE_BOOLEAN &&
             a[p_print_resolution].type == SW_DATUM_TYPE_INTEGER &&
             (length == max_param_length - 1 || a[p_bold].type == SW_DATUM_TYPE_INTEGER),
             "Bad miscop" ) ;
    symbolset = a[p_ss     ].value.integer ;
    spacing   = a[p_space  ].value.integer ;
    pitch     = a[p_pitch  ].value.real ;
    height    = a[p_height ].value.real ;
    style     = a[p_style  ].value.integer ;
    weight    = a[p_weight ].value.integer ;
    typeface  = a[p_font   ].value.integer ;
    exclude   = a[p_exclude].value.boolean ;
    print_resolution = a[p_print_resolution].value.boolean ;

    if ( length == max_param_length ) bold = a[p_bold].value.integer ;
  }

  fixedpitch = 1 - spacing ;

  /* Do a font match */
  best = PCLfontmatch(ws, FALSE, &symbolset, &symbolsettype, fixedpitch,
                      pitch, height, style, weight, typeface, exclude,
                      print_resolution,
                      &embolden, &faux) ;
  bold += embolden ;

  while (best >= ws->TotalFonts && (user = finduserfontbyref(ws,best)) != 0) {
    /* User font - recurse through proxies to the underlying font. */

    if (ref == -1)
      ref = user->ref ;

    if (user->proxy > -1)
      best = user->proxy ;
    if (user->proxy < ws->TotalFonts)
      break ;
  }

  /* We now have the best match */
  {
    int32 ssref = find_symbolset(ws, symbolset, NULL) ;
    fontnamelen = (bold) ?
                  sprintf(fontname, "PCL:%i:%i:%i", best, ssref, bold) :
                  sprintf(fontname, "PCL:%i:%i", best, ssref) ;
  }

  /* Faux italic */
  if ((faux & 1) == 1)
    fontname[fontnamelen++] = 'i' ;

  if (best < ws->TotalFonts) {
    /* FCO font */
    font = &ws->Fonts[best] ;
    fontInfo = font->pttFontInfo ;

    hmi = font->hmi ;
    offset = (float)fontInfo->uScoreDepth     / fontInfo->scaleFactor ;
    thick =  (float)fontInfo->uScoreThickness / fontInfo->scaleFactor ;
    fixedpitch = fontInfo->isFixedPitch ;
    bitmapped = 0 ;
  } else {
    /* User font */
    if (user) {
      font_header * header = (font_header *) user->data ;

      fixedpitch = 1 - header->spacing ;
      bitmapped = user->type == TYPE_BITMAP ;

      hmi    = user->hmi ;
      offset = user->offset ;
      thick  = user->thick ;
      if (user->pitch > -1)  pitch  = user->pitch ;
      if (user->height > -1) height = user->height ;
    } else {
      /* No font found, something's gone wrong... */
      hmi = 0.6f ;
      fixedpitch = 1 ;
      bitmapped = 0 ;
      offset = 0 ;
      thick = 0 ;
    }
  }

  /* round and range-limit returned font size */
  if (pitch > 480.0f)  pitch = 480.0f ;
  if (pitch < 0.12f)   pitch = 0.12f ;
  fontsize = (fixedpitch) ? 72.0f / (hmi * pitch) : height ;
  if (fontsize > 999.75f)  fontsize = 999.75f ;
  if (fontsize < 0.25f)    fontsize = 0.25f ;
  fontsize = ((int32)(4 * fontsize + 0.5f)) / 4.0f ;

  reply[r_name   ].value.string  = fontname ;
  reply[r_name   ].length        = fontnamelen ;
  reply[r_size   ].value.real    = fontsize ;
  reply[r_hmi    ].value.real    = hmi ;
  reply[r_sstype ].value.integer = symbolsettype ;
  reply[r_spacing].value.integer = 1-fixedpitch ;   /* spacing */
  reply[r_offset ].value.real    = offset ;
  reply[r_thick  ].value.real    = thick ;
  reply[r_ref    ].value.integer = ref ;
  reply[r_bitmap ].value.boolean = bitmapped ;
  *param = reply ;

  return SW_PFIN_SUCCESS ;
}

/* -------------------------------------------------------------------------- */
/* The list of LinePrinter symbolsets in order, which are treated as "internal
 * fonts" enumerated immediately after the FCO fonts */

int16 LinePrinterSS[] = {
  SS(10,'U'), /* PC-8 (CP437) */
  SS(11,'U'), /* PC-8 D/N (Danish/Norwegian) */
  SS( 1,'U'), /* Legal */
  SS( 8,'U'), /* Roman-8 */
  SS( 0,'N'), /* ECMA-94  Latin 1 (8859/1) */
  SS( 2,'N'), /* ECMA-94  Latin 2 (8859/2) */
  SS( 5,'N'), /* ECMA-128 Latin 5 (8859/9) */
  SS( 6,'N'), /* Latin 6 (8859/10) */
  SS( 9,'N'), /* Latin 9 (8859/15) */
  SS(12,'U')  /* PC-850 Multilingual */
} ;

/* -------------------------------------------------------------------------- */
/* Select a softloaded font by PCL5 integer ID, PCL5 alphanumeric ID,
   or PCL XL string ID, or select the default font by location and
   index.

  > PCL_MISCOP_SELECT takes one of the following request parameter
    lists

  a) Font ID/font number only   (i.e. 3 parameters in total)

    int      PCL_MISCOP_SELECT
    int      font ID (or font number)
    int      symbol set

  b) Font alphanumeric ID (i.e. 4 parameters in total)

    int      PCL_MISCOP_SELECT
    string   font alphanumeric name
    int      symbol set
    int      font name data format (0 = 8-bit, any other value caller-defined-meaning)

  c) PCLXL font name (+ data format) + symbol set (i.e. 5 parameters in total)

    int      PCL_MISCOP_SELECT
    string   font name
    int      font name data format (0 = 8-bit, any other value caller-defined-meaning)
    int      symbol set
    int      char boldness

  d) Font location + "index" + symbol set + pitch + height  (i.e. 6 parameters)

    int      PCL_MISCOP_SELECT
    int      font number
    int      font location
    int      symbol set
    float    pitch
    float    height

  e) As c) but with an explicitly provided default font size. This can be used to
    indicate the expected size of a PCLXL bitmap font. The height is given in
    1/100th of a point.

    PCLXL fontname ( + data format) + symbol set + height (i.e 6 parameters)

    int      PCL_MISCOP_SELECT
    string   font name
    int      font name data format (0 = 8-bit, any other value caller-defined-meaning)
    int      symbol set
    int      char boldness
    int      font size

  If the font cannot be selected, there is no reply. Otherwise:

  < string  fontname
    float   fontsize
    float   HMI
    int     symbolset      \
    int     spacing         |
    float   pitch           |
    float   height          } criteria as per PCL_MISCOP_SPECIFY
    int     style           |
    int     weight          |
    int     typeface       /
    int     symbolset type
    float   underline offset
    float   underline thickness
    int     ref
    bool    bitmapped
*/

sw_pfin_result pcl_miscop_select(sw_pfin_instance * pfin,
                                 sw_datum ** param, sw_datum * a)
{
  pfin_ufst5_instance * ws = (pfin_ufst5_instance *) pfin ;
  size_t length = (*param)->length ;
  sw_datum datum, * idstr = 0 ;
  int32 id = 0, arg = 0, ss = 0, bold = 0, ref = -1 ;
  float pitch = 0.0, height = 0.0 ;
  HqBool is_fonttype_16 = FALSE ;
  HqBool scalable_typeface = FALSE ;
  uint8 bound = 0 ;
  int32 symbolsettype = -1 ;
  int32 override_height = -1;
  HqBool select_default = FALSE;
  HqBool xl_bitmap = FALSE;

  static sw_datum reply[16] = {
    SW_DATUM_ARRAY(reply+1,15),
    SW_DATUM_STRING(""),           /* fontname */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* fontsize */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* HMI */
    SW_DATUM_INTEGER(0),           /* symbolset */
    SW_DATUM_INTEGER(0),           /* spacing */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* pitch */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* height */
    SW_DATUM_INTEGER(0),           /* style */
    SW_DATUM_INTEGER(0),           /* weight */
    SW_DATUM_INTEGER(0),           /* typeface */
    SW_DATUM_INTEGER(0),           /* symbolset type */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* underline offset */
    SW_DATUM_FLOAT(SW_DATUM_0_0F), /* underline thickness */
    SW_DATUM_INTEGER(0),           /* soft font ref or -1 */
    SW_DATUM_BOOLEAN(FALSE)        /* bitmapped */
  };

  enum { /* reply indices for the above array */
    r_array = 0, r_name, r_size, r_hmi, r_ss, r_space, r_pitch, r_height,
    r_style, r_weight, r_font, r_sstype, r_offset, r_thick, r_ref, r_bitmap
  } ;

  enum { /* parameter indices for the various cases */
    a_reason = 0, a_id, a_symbolset
  } ;
  enum {
    b_reason = 0, b_strid, b_symbolset, b_strtype
  } ;
  enum {
    c_reason = 0, c_strid, c_strtype, c_ss, c_bold
  } ;
  enum {
    d_reason = 0, d_number, d_locate, d_ss, d_pitch, d_height
  } ;
  enum {
    e_reason = 0, e_strid, e_strtype, e_ss, e_bold, e_override_height
  } ;

  if ((*param)->owner) {
    /* We must do things the "official way"
     * (i.e. use the get_indexed() and get_<type>() access functions)
     * for "opaque" datums (i.e. ones that have an "owner")
     */
    const sw_data_api * data = pfin->data_api ;

    if ( (length == 3) &&
         get_int(data, *param, a_id, &id) &&
         get_int(data, *param, a_symbolset, &ss )) {
      /*
       * We have received parameter list (a) (see above)
       * which consists of just a font ID
       */

      id = id ; /* nothing to do */

    } else if ( (length == 4) &&
                get_int(data, *param, b_symbolset, &ss ) &&
                (data->get_indexed(*param, b_strid, &datum) == SW_DATA_OK) &&
                (datum.type == SW_DATUM_TYPE_STRING) &&
                (get_int(data, *param, b_strtype, &arg)) ) {

      /*
       * We have received parameter list (b) (see above)
       * which consists of a font name, font name data type
       *
       * Note that we have "overloaded" the meaning of the value
       * stored into <id> to be any/all of an integer font ID, an
       * integer font number or the font name data format
       */

      id = arg;
      idstr = &datum;

    } else if ( (length == 5) &&
                (data->get_indexed(*param, c_strid, &datum) == SW_DATA_OK) &&
                (datum.type == SW_DATUM_TYPE_STRING) &&
                (get_int(data, *param, c_strtype, &arg)) &&
                (get_int(data, *param, c_ss, &ss)) &&
                (get_int(data, *param, c_bold, &bold)) ) {
      /*
       * We have received parameter list (c) (see above) which
       * consists of a font name, font name data type, symbol set and
       * char boldness.
       *
       * Note that we have "overloaded" the meaning of the value
       * stored into <id> to be any/all of an integer font ID, an
       * integer font number or the font name data format
       */

      id = arg;
      idstr = &datum;

    } else if ( (length == 6) &&
                (data->get_indexed(*param, e_strid, &datum) == SW_DATA_OK) &&
                (datum.type == SW_DATUM_TYPE_STRING) &&
                (get_int(data, *param, e_strtype, &arg)) &&
                (get_int(data, *param, e_ss, &ss)) &&
                (get_int(data, *param, e_bold, &bold)) &&
                (get_int(data, *param, e_override_height, &override_height)) ) {
      /* We have received parameter list (e). */
                  
      id = arg;
      idstr = &datum;

    } else if ( (length >= 6) &&
                get_int(data, *param, d_number, &id) &&
                get_int(data, *param, d_locate, &arg) &&
                get_int(data, *param, d_ss,     &ss) &&
                get_flt(data, *param, d_pitch,  &pitch) &&
                get_flt(data, *param, d_height, &height) ) {
      /*
       * We have received parameter list (d) (see above) which
       * consists of:
       *
       *  font number <id>
       *  font location <arg>
       *  symbol set <ss>
       *  pitch <pitch>
       *  height <height>
       */

      id = id ; /* nothing to do */

    } else {
      /*
       * This is an unrecognized parameter list
       */

      return SW_PFIN_ERROR_SYNTAX;
    }

  } else {
    /*
     * We have a "raw" datum array which we can access directly
     * indexing the "a" parameter.
     *
     * So we basically have a repeat of the above parameter validation
     * but only as a HQASSERT, so we must *always* perform the
     * appropriate assignments even if the HQASSERT fires.
     */

    HQASSERT((((length == 3) &&
               (a[a_id].type == SW_DATUM_TYPE_INTEGER) &&
               (a[a_symbolset].type == SW_DATUM_TYPE_INTEGER)) ||
              ((length == 4) &&
               (a[b_strid  ].type == SW_DATUM_TYPE_STRING) &&
               (a[b_symbolset].type == SW_DATUM_TYPE_INTEGER) &&
               (a[b_strtype].type == SW_DATUM_TYPE_INTEGER)) ||
              ((length == 5) &&
               (a[c_strid  ].type == SW_DATUM_TYPE_STRING) &&
               (a[c_strtype].type == SW_DATUM_TYPE_INTEGER) &&
               (a[c_ss     ].type == SW_DATUM_TYPE_INTEGER) &&
               (a[c_bold   ].type == SW_DATUM_TYPE_INTEGER)) ||
              ((length >= 6) &&
               (a[d_number].type == SW_DATUM_TYPE_INTEGER) &&
               (a[d_locate].type == SW_DATUM_TYPE_INTEGER) &&
               (a[d_ss    ].type == SW_DATUM_TYPE_INTEGER) &&
               (a[d_pitch ].type == SW_DATUM_TYPE_FLOAT) &&
               (a[d_height].type == SW_DATUM_TYPE_FLOAT)) ||
             ((length == 6) &&
               (a[e_strid  ].type == SW_DATUM_TYPE_STRING) &&
               (a[e_strtype].type == SW_DATUM_TYPE_INTEGER) &&
               (a[e_ss     ].type == SW_DATUM_TYPE_INTEGER) &&
               (a[e_bold   ].type == SW_DATUM_TYPE_INTEGER) &&
               (a[e_override_height].type == SW_DATUM_TYPE_INTEGER))),
             "Bad miscop");

    if ( length == 3 ) {
      id = a[a_id].value.integer;
      ss = a[a_symbolset].value.integer;

    } else if ( length == 4 ) {
      idstr    = &a[b_strid ];
      id = arg = a[b_strtype].value.integer;
      ss = a[b_symbolset].value.integer;

    } else if ( length == 5 ) {
      idstr    = &a[c_strid ];
      id = arg = a[c_strtype].value.integer;
      ss       = a[c_ss     ].value.integer;
      bold     = a[c_bold   ].value.integer;

    } else if (length == 6 ) {
      idstr    = &a[e_strid ];
      id = arg = a[e_strtype].value.integer;
      ss       = a[e_ss     ].value.integer;
      bold     = a[e_bold   ].value.integer;
      override_height = a[e_override_height].value.integer;

    } else { /* if length > 6 */
      /* Note we are not going to actually perform this check because
       * the HQASSERT may already have fired but we need to fall
       * through into the subsequent code with *some* values assigned
       * to the <id>, <arg>, <ss>, <pitch> and <height> cases below
       */
      id     = a[d_number].value.integer;
      arg    = a[d_locate].value.integer;
      ss     = a[d_ss    ].value.integer;
      pitch  = a[d_pitch ].value.real;
      height = a[d_height].value.real;

      select_default = TRUE;
    }
  }

  /*
   * Ok, if we get to here, then we have successfully validated the
   * parameter list to be one of the valid ones and extracted the
   * parameters into some or all of:
   *
   * <id>, <idstr>, <arg>, <ss>, <pitch> and <height>
   *
   * a) PCL5 select: <id>
   *
   * b) PCL5 select: <idstr> (font name), data format (in both <id> and <arg>)
   *
   * c) PCLXL select: <idstr> (font name), data format (in both <id> and <arg>), <ss>, <bold>
   *
   * d) Default select: <id> (fontnumber), <arg> (location), <ss>, <pitch>, <height>
   */

  if (select_default || length > 6) {
    /* d) Select default font by index and location */

    if (arg == 0 &&
        id >= 0 && id < ws->TotalFonts + (int32) lengthof(LinePrinterSS)) {
      /* Note magic values for LinePrinter, which override symbolset too */
      if (id > ws->TotalFonts) {
        ref = ws->LinePrinter ;
        ss  = LinePrinterSS[id - ws->TotalFonts] ;
      } else {
        /* FCO fonts. PI fonts override symbolset too */
        ref = id ;
        switch (ws->Fonts[ref].typeface) {
        case WINGDINGS: ss = WINGDINGS_SS ; break ;
        case DINGBATS:  ss = DINGBATS_SS  ; break ;
        case SYMBOLPCL:
        case SYMBOLPS:  ss = SYMBOL_SS    ; break ;
        }
      }
    }

  } else {
    /* Select by id (and perhaps idstr) */

#if 0
    /* Its not clear if we ignore the font if the symbol set does not
       match when selecting by id. This still needs investigation. */
    user_font **ptr = &ws->user_fonts,
              *font = finduserfont_with_symbols(ws, id, ss, idstr, &ptr) ;
#else
    user_font **ptr = &ws->user_fonts,
              *font = finduserfont(id, idstr, &ptr) ;
#endif

    /* Recurse through proxied soft fonts - this should no longer happen */
    while (font && font->proxy >= ws->TotalFonts)
      font = finduserfontbyref(ws, font->proxy) ;

    if (font) {
      if (font->proxy > -1) {
        /* Internal font */
        ref = font->proxy ;
        ss  = font->ss ;

      } else {
        /* User font */
        font_header    * header   = (font_header    *) font->data ;
        int   bitmap = ((font->type & TYPE_BITMAP) != 0) ;
        bound = font->data[typeO] ;
        xl_bitmap = font->type == TYPE_XLBITMAP ;

        if (bound <= BOUNDFONTTYPE) {
          symbolsettype = header->type ;
          HQASSERT(symbolsettype != -1, "Odd font type.") ;
        }
        ref = font->ref ;

        if (bitmap) {
          ss  = font->fh_ss ;  /* Symbolset from header */
        } else {
          scalable_typeface = TRUE ;
        }

        /* The common stuff from the font header */
        reply[r_space ].value.integer = header->spacing ;
        reply[r_style ].value.integer = font->style ;
        reply[r_weight].value.integer = header->weight ;
        reply[r_font  ].value.integer = font->typeface ;
        reply[r_bitmap].value.boolean = bitmap ;

        reply[r_size  ].value.real = font->height ;
        reply[r_hmi   ].value.real = font->hmi ;
        reply[r_offset].value.real = font->offset ;
        reply[r_thick ].value.real = font->thick ;

        if (header->format == 16)
          is_fonttype_16 = TRUE ;

        if (ws->emulate4700 && bitmap) {
          /* The Criteria overridden by bitmapped fonts in the HP4700 are
           * calculated as though the resolution was 300dpi, regardless of
           * the font's actual resolution...
           */
          reply[r_pitch ].value.real = font->pitch * 300 / font->xres ;
          reply[r_height].value.real = font->height * font->yres / 300 ;
        } else {
          reply[r_pitch ].value.real = font->pitch ;
          reply[r_height].value.real = font->height ;
        }
      }
    }
  }

  if (ref > -1) {
    PCL_SSINFO * ssList = ws->psslist ;
    int32 fontnamelen, ssref ;
    user_ss * userss = 0 ;

    if (ref < ws->TotalFonts) {
      fco_font * font = &ws->Fonts[ref] ;
      TTFONTINFOTYPE * fontInfo = font->pttFontInfo ;

      reply[r_space ].value.integer = 1 - fontInfo->isFixedPitch ;
      reply[r_style ].value.integer = fontInfo->pcltStyle ;
      reply[r_weight].value.integer = fontInfo->pcltStrokeWt ;
      reply[r_font  ].value.integer = font->typeface ;
      reply[r_bitmap].value.integer = FALSE ;

      reply[r_hmi   ].value.real = font->hmi ;
      reply[r_offset].value.real = (float)fontInfo->uScoreDepth     / fontInfo->scaleFactor ;
      reply[r_thick ].value.real = (float)fontInfo->uScoreThickness / fontInfo->scaleFactor ;

      reply[r_size  ].value.real  = -1 ;    /* any size will do */
      reply[r_pitch ].value.real  = -1 ;
      reply[r_height].value.real  = -1 ;
    }

    /* Not a user defined bound font so lookup the symbol set type. */
    ssref = find_symbolset(ws, ss, &userss) ;
    if (symbolsettype == -1) {
      if (userss) {
        symbolsettype = userss->data[5] ;
        ss = userss->id ;
      } else {
        symbolsettype = ssList[ssref].type ;
        if (scalable_typeface) {
          ss = ssList[ssref].ss ;
        }
      }
    } else {
      symbolsettype = 2 ;
    }

    if (is_fonttype_16) {
      symbolsettype = 3 ;
    }

    fontnamelen = (bold) ?
                  sprintf(fontname, "PCL:%i:%i:%i", ref, ssref, bold) :
                  ( (!xl_bitmap || override_height < 0) ?
                    sprintf(fontname, "PCL:%i:%i", ref, ssref) :
                    sprintf(fontname, "PCL!%i:%i:%i", ref, ssref,
                            override_height));

    reply[r_name  ].value.string  = fontname ;
    reply[r_name  ].length        = fontnamelen ;
    reply[r_ss    ].value.integer = ss ;
    reply[r_sstype].value.integer = symbolsettype ;
    reply[r_ref   ].value.integer = (ref < ws->TotalFonts) ? -1 : ref ;

    *param = reply ;
  }

  return SW_PFIN_SUCCESS ;
}

/* -------------------------------------------------------------------------- */
/*
  Get glyph existance, metrics and kerning information.

  > PCL_MISCOP_METRICS
    string  fontname           (or null for same font as last call)
    int     character code
    int     previous chr       (or -1)

  If the character is undefined there is no reply. Otherwise:

  < float  advance width
    float  kerning adjustment  (or 0)
*/

sw_pfin_result pcl_miscop_metrics(sw_pfin_instance * pfin,
                                  sw_datum ** param, sw_datum * a)
{
  pfin_ufst5_instance * ws = (pfin_ufst5_instance *) pfin ;
  size_t length = (*param)->length ;
  float x = METRICS_UNKNOWN, emx ;
  PCL_SSINFO * ssList = ws->psslist ;
  int32 cid, prev ;
  sw_datum datum, *font = &datum ;
  sw_pfin_result err ;
  static sw_datum reply[] = {
    SW_DATUM_ARRAY(reply+1,2),
    SW_DATUM_FLOAT(SW_DATUM_0_0F),  /* character width */
    SW_DATUM_FLOAT(SW_DATUM_0_0F)   /* kerning adjustment */
  };
#if USE_METRICS_CACHE
  uidcache     * cache = 0 ;
  metricscache * metrics = 0 ;      /* ptr to cache to store width into */
#endif

  int32 override_height = -1 ;

  enum { /* reply indices (into the above array */
    r_array = 0, r_width, r_kern
  } ;

  enum { /* parameter indices */
    p_reason = 0, p_name, p_code, p_prev
  } ;

  if ((*param)->owner) {
    /* Do things the official way for opaque datums */
    const sw_data_api * data = pfin->data_api ;

    if (length != 4 ||
        !get_int(data,*param,p_code,&cid) ||
        !get_int(data,*param,p_prev,&prev) ||
        data->get_indexed(*param,p_name,font) != SW_DATA_OK ||
        (font->type != SW_DATUM_TYPE_NULL &&
         (font->type != SW_DATUM_TYPE_STRING ||
          font->length > 6)))
      return SW_PFIN_ERROR_SYNTAX ;
  } else {
    /* For raw datums, a already points at the first member. */
    HQASSERT(length == 4 &&
             (a[p_name].type == SW_DATUM_TYPE_NULL ||
              a[p_name].type == SW_DATUM_TYPE_STRING) &&
             a[p_code].type == SW_DATUM_TYPE_INTEGER &&
             a[p_prev].type == SW_DATUM_TYPE_INTEGER &&
             a[p_name].length > 6,
             "Bad miscop") ;
    font = &a[p_name] ;
    cid  = a[p_code].value.integer ;
    prev = a[p_prev].value.integer ;
  }

  if (font->type == SW_DATUM_TYPE_NULL &&
      ws->prev_id == -1)
    return SW_PFIN_ERROR_SYNTAX ;

  if (font->type == SW_DATUM_TYPE_STRING) {
    int32 current_ss = ws->prev_ss ;
    int32 current_id = ws->prev_id ;
    ws->prev_bold = 0 ;
    ws->prev_faux = 0 ;
    if (!parse_fontname(ws, font, &ws->prev_id, &ws->prev_ss, &ws->prev_ssref,
                        &ws->prev_bold, &ws->prev_faux, &override_height))
      return SW_PFIN_ERROR_SYNTAX ;

    if ( current_ss != ws->prev_ss || current_id != ws->prev_id ) {
      user_ss * userss = 0 ;
      int i = find_symbolset(ws, ws->prev_ss, &userss) ;

      ws->prev_ufst = (userss) ? -userss->ref : ssList[i].ufst ;
    }
  }

#if USE_METRICS_CACHE
  /* Check the metrics cache - do we know the width already? */
  {
    if (ws->prev_id < ws->TotalFonts && ws->prev_id >= 0)
      cache = ws->Fonts[ws->prev_id].uids ;
    else {
      user_font * font = finduserfontbyref(ws, ws->prev_id) ;
      if (font)
        cache = font->uids ;
    }
    while (cache && cache->ssref != ws->prev_ssref)
      cache = cache->next ;                           /* find the uidcache */
    if (cache) {
      /* uidcache for this combination found. It will have been created on
       * define so we really shouldn't fail to find it.
       */
      int32 set = cid &~ METRICSMASK ;
      metricscache * prev = 0 ;

      /* Find the set of widths containing this cid */
      metrics = cache->metrics ;
      while (metrics && metrics->set != set) {
        prev = metrics ;
        metrics = metrics->next ;
      }
      if (metrics) {
        /* Found a cache containing this cid */
        x = metrics->widths[cid - set] ;              /* may be valid or -1 */
        PROMOTE(metrics,prev,cache->metrics,next) ;

        /* If previously found to be undefined, return now! */
        if (x == METRICS_MISSING)
          return SW_PFIN_SUCCESS ;
      } else {
        /* No cache containing this cid, so make one */
#ifdef DEBUG_UFST_CACHE_HITS
        cache_blocks++ ;
#endif
        metrics = myalloc(sizeof(metricscache)) ;
        if (metrics) {
          int i ;
          metrics->next = cache->metrics ;
          metrics->set  = set ;
          for (i = 0; i < METRICSLENGTH; i++)
            metrics->widths[i] = METRICS_UNKNOWN ;       /* all widths unknown */
          cache->metrics = metrics ;
        }
      }
    }
  }
  if (x == METRICS_UNKNOWN) {
#ifdef DEBUG_UFST_CACHE_HITS
    outlines_made++ ;
#endif
#endif

  ws->make_bitmaps = FALSE ; /* a terrible fiddle - we have no font size */
  ws->context = NULL ;
  err = make_char(ws, 0, ws->prev_id, cid, ws->prev_ufst, ws->prev_bold,
                  ws->prev_faux, FALSE, FALSE, override_height);
  switch (err) {
  default:
    return err ;
  case SW_PFIN_ERROR_UNKNOWN:
#if USE_METRICS_CACHE
    if (metrics) {
      metrics->widths[cid - metrics->set] = METRICS_MISSING ;
      cache->missing = TRUE ;
    }
#endif
     return SW_PFIN_SUCCESS ;
  case SW_PFIN_SUCCESS:
    break;
  }

  /* This is an adjustment to account for INTELLIFONT points being slightly
   * different from PostScript points.
   */
  if (ws->bitmap) {
    /* UFST casts two different structures as the same thing, and though they
     * share some field names, those fields are not necessarily in the same
     * place. Hence this apparent duplication. */
    PIFBITMAP pbm = (PIFBITMAP) ws->pol ;
    emx = (float)pbm->du_emx ;
    x = (float)pbm->escapement / emx ;
  } else {
    emx = (ws->pol->du_emx == INTELLIFONT) ? 72.307f / 72 * INTELLIFONT :
                                             (float)ws->pol->du_emx ;
    x = (float)ws->pol->escapement / emx ;
  }

#if USE_METRICS_CACHE
    if (metrics)
      metrics->widths[cid - metrics->set] = x ;     /* store for later reuse */
  }
#ifdef DEBUG_UFST_CACHE_HITS
  else cache_hits++ ;
#endif
#endif

  reply[r_width].value.real = x ;
  reply[r_kern ].value.real = 0 ; /* no kerning adjustment */
  *param = reply ;

  return SW_PFIN_SUCCESS ;
}

/* -------------------------------------------------------------------------- */
/* PCL XL font selection is by name, symbolset and size.

   > PCL_MISCOP_XL
     fontname         (string)
     datatype         (int)   0=8b unsigned chars
     symbolset        (int)
     bold             (int)
     height           (float)
     print resolution (int)

   < fontname  (string) (of the form <PCL:###:###>)
     fontsize  (float)
*/

static
sw_pfin_result pcl_miscop_xl(sw_pfin_instance * pfin,
                             sw_datum ** param, sw_datum * a)
{
  pfin_ufst5_instance * ws = (pfin_ufst5_instance *) pfin ;
  size_t length = (*param)->length ;

  static sw_datum reply[3] = {
    SW_DATUM_ARRAY(reply+1,2),
    SW_DATUM_STRING(""),           /* fontname */
    SW_DATUM_FLOAT(SW_DATUM_0_0F)  /* size */
  };

  enum { /* reply indices (into the above array) */
    r_array = 0, r_name, r_size
  } ;

  enum { /* parameter indices */
    r_reason = 0, r_strid, r_strtype, r_ss, r_bold, r_height, r_print_resolution,
    max_param_length
  } ;

  int32 typeface, symbolset = 0, symbolsettype, weight, style = 0 ;
  int32 bold = 0, mono, datatype = 0, fontnamelen, id = -1 ;
  int32 faux = 0, embolden = 0, print_resolution = 0;
  float pitch, fontsize, hmi = 1.0f ;
  sw_datum datum, * name = &datum ;

  if ((*param)->owner)
  {
    /* Do things the official way for opaque datums */
    const sw_data_api * data = pfin->data_api ;

    if ( !((length == max_param_length) &&
           (data->get_indexed(*param,r_strid,name) == SW_DATA_OK) &&
           (name->type == SW_DATUM_TYPE_STRING) &&
           (get_int(data, *param, r_strtype, &datatype)) &&
           (get_int(data, *param, r_ss, &symbolset)) &&
           (get_int(data, *param, r_bold, &bold)) &&
           (get_flt(data, *param, r_height, &fontsize)) &&
           (get_int(data, *param, r_print_resolution, &print_resolution)) ) )
    {
      return SW_PFIN_ERROR_SYNTAX;
    }
  }
  else
  {
    HQASSERT(((length == max_param_length) &&
              (a[r_strid  ].type == SW_DATUM_TYPE_STRING) &&
              (a[r_strtype].type == SW_DATUM_TYPE_INTEGER) &&
              (a[r_ss     ].type == SW_DATUM_TYPE_INTEGER) &&
              (a[r_bold   ].type == SW_DATUM_TYPE_INTEGER) &&
              (a[r_height ].type == SW_DATUM_TYPE_FLOAT) &&
              (a[r_print_resolution ].type == SW_DATUM_TYPE_INTEGER)),
             "Bad miscop");

    name      = &a[r_strid ];
    datatype  = a[r_strtype].value.integer;
    symbolset = a[r_ss     ].value.integer;
    bold      = a[r_bold   ].value.integer;
    fontsize  = a[r_height ].value.real;
    print_resolution = a[r_print_resolution].value.integer;
  }

  /* XL's call to SELECT_BY_ID could be avoided if did the soft font comparison
     here also. If found, we'd set id = font->ref ;
  */

  /* Try an FCO lookup of the XL name */
  if (id == -1 && datatype == 0 && name->length > 0) {
    int i ;
    for (i = 0; i < ws->TotalFonts; i++) {
      /* Compare XL names. Non-XL fonts have a zero length so can't match. */
      if ( name->length == ws->Fonts[i].xlnamelen &&
           memcmp(XLFONTNAME(i), name->value.string, name->length) == 0 ) {
        id = i ;
        break ;
      }
    }
  }

  /* Otherwise, can we can map the XL font name to a PCL typeface number? */
  if (id == -1 && datatype == 0 &&
     XLtoPCL(ws, name, &typeface, &mono, &symbolset, &weight, &style, &hmi)) {

    /* Fake a pitch */
    if (fontsize < 0.0025f)  fontsize = 0.0025f ;
    pitch = 72.0f / (hmi * fontsize) ;

    /* Do a PCL font match */
    id = PCLfontmatch(ws, TRUE, &symbolset, &symbolsettype,
                      mono, pitch, fontsize,
                      style, weight, typeface, 0, print_resolution,
                      &embolden, &faux) ;
    bold += embolden ;
  }

  if (id > -1) {
    int32 ssref = find_symbolset(ws, symbolset, NULL) ;
    fontnamelen = (bold) ?
                  sprintf(fontname, "PCL:%i:%i:%i", id, ssref, bold) :
                  sprintf(fontname, "PCL:%i:%i", id, ssref) ;

    /* Faux italic */
    if ((faux & 1) == 1)
      fontname[fontnamelen++] = 'i' ;

    if (fontsize > 999.75f)  fontsize = 999.75f ;
    if (fontsize < 0.0025f)    fontsize = 0.0025f ;

    reply[r_array].length        = 2 ;
    reply[r_name ].value.string  = fontname ;
    reply[r_name ].length        = fontnamelen ;
    reply[r_size ].value.real    = fontsize ;
    *param = reply ;
  }

  return SW_PFIN_SUCCESS ;
}

/* -------------------------------------------------------------------------- */
/* PCL font/symbolset control actions

   > [0] PCL_MISCOP_FONT or PCL_MISCOP_SYMBOLSET:
     [1] int     action   (from PCL command)
     [2] int/str ID       (required for action > 1. Can be string for XL.)
     [3] int     code     (required for action = 3 only)
         string  font     (required for action = 6 only)
     [4] int     datatype (optional. for action > 1 where ID is string)
*/

sw_pfin_result pcl_miscop_control(sw_pfin_instance * pfin,
                                  sw_datum ** param, sw_datum * a, int32 miscop)
{
  pfin_ufst5_instance * ws = (pfin_ufst5_instance *) pfin ;
  size_t length = (*param)->length ;

  int32 action = -1, id = -1, code = -1, datatype = 0, error = 0 ;
  sw_datum datum, * name = &datum, str = SW_DATUM_NOTHING, *idstr = 0 ;

  enum {
    p_reason = 0, p_action, p_id, p_code, p_name = p_code, p_strtype
  } ;

  if ((*param)->owner) {
    /* Do things the official way for opaque datums */
    const sw_data_api * data = pfin->data_api ;

    if ( length < 2   || length > 5  ||
         !get_int(data,*param,p_action,&action) ||
         (action > 1  && (length < 3 ||
           data->get_indexed(*param,p_id,&str) != SW_DATA_OK ||
           (str.type != SW_DATUM_TYPE_INTEGER &&
            (str.type != SW_DATUM_TYPE_STRING ||
             miscop == PCL_MISCOP_SYMBOLSET)))) ||
         (action == 3 && (length < 4 || !get_int(data,*param,p_code,&code))) ||
         ((action == 6 || action == 0x106) && (length < 4 ||
           data->get_indexed(*param,p_name,name) != SW_DATA_OK ||
           name->type != SW_DATUM_TYPE_STRING)) ||
         (action > 1 && length > 4 && str.type == SW_DATUM_TYPE_STRING &&
          !get_int(data,*param,p_strtype,&datatype)) )
      return SW_PFIN_ERROR_SYNTAX ;
    if (str.type == SW_DATUM_TYPE_INTEGER)
      id = str.value.integer ;
    else
      idstr = &str ;
  } else {
    /* For raw datums, a already points at the first member. */
    if (length > 1)
      action = a[1].value.integer ;
    HQASSERT(length > 1 && length < 6 &&
      a[1].type == SW_DATUM_TYPE_INTEGER  &&
      (action < 2  || (length > 2 && (a[p_id].type == SW_DATUM_TYPE_INTEGER ||
                                      (a[p_id].type == SW_DATUM_TYPE_STRING &&
                                       miscop != PCL_MISCOP_SYMBOLSET)))) &&
      (action != 3 || (length > 3 && a[p_code].type == SW_DATUM_TYPE_INTEGER)) &&
      ((action != 6 && action != 0x106) ||
       (length > 3 && a[p_name].type == SW_DATUM_TYPE_STRING)) &&
      (action < 2 || length < 5 || a[p_id].type != SW_DATUM_TYPE_STRING ||
       a[p_strtype].type == SW_DATUM_TYPE_INTEGER),
      "Bad miscop") ;
    if (action > 1) {
      if (a[p_id].type == SW_DATUM_TYPE_INTEGER)
        id = a[p_id].value.integer ;
      else
        idstr = &a[p_id] ;
      if (length > 4)
        datatype = a[p_strtype].value.integer ;
    }
    if (action == 3)
      code = a[p_code].value.integer ;
    if (action == 6 || action == 0x106)
      name = &a[p_name] ;
  }

  if (miscop == PCL_MISCOP_SYMBOLSET) {
    if (idstr)
      return SW_PFIN_ERROR_SYNTAX ;
    error = pcl_symbolset_control(ws, action, id) ;
  } else {
    if (idstr)
      id = datatype ;
    error = pcl_font_control(ws, action, id, idstr, code, name) ;
  }

  if (error) {
    /* There has been an error, return an XL-style error code */
    static sw_datum reply = SW_DATUM_INTEGER(0) ;

    reply.value.integer = error ;
    *param = &reply ;
  }

  return SW_PFIN_SUCCESS ;
}

/* -------------------------------------------------------------------------- */
/* Define font/symbolset

   > [0] PCL_MISCOP_DEFINE_FONT or PCL_MISCOP_DEFINE_SYMBOLSET
     [1] int/str ID
     [2] string  data
     [3] int     datatype (of ID if a string. Optional)
*/

sw_pfin_result pcl_miscop_define(sw_pfin_instance * pfin,
                                 sw_datum ** param, sw_datum * a, int32 miscop)
{
  pfin_ufst5_instance * ws = (pfin_ufst5_instance *) pfin ;
  size_t length = (*param)->length ;

  int32 id = 0, error ;
  sw_datum datum, * bytes = &datum, str = SW_DATUM_NOTHING, * idstr = 0 ;

  enum {
    p_reason = 0, p_id, p_data, p_strtype
  } ;

  if ((*param)->owner) {
    /* Do things the official way for opaque datums */
    const sw_data_api * data = pfin->data_api ;

    if ( length < 3 ||
         data->get_indexed(*param,p_id,&str) != SW_DATA_OK ||
         (str.type != SW_DATUM_TYPE_INTEGER &&
          str.type != SW_DATUM_TYPE_STRING) ||
         data->get_indexed(*param,p_data,bytes) != SW_DATA_OK ||
         bytes->type != SW_DATUM_TYPE_STRING ||
         (length > 3 && (miscop == PCL_MISCOP_DEFINE_SYMBOLSET ||
                         !get_int(data,*param,p_strtype,&id))) )
      return SW_PFIN_ERROR_SYNTAX ;
    if (str.type == SW_DATUM_TYPE_INTEGER)
      id = str.value.integer ;
    else
      idstr = &str ;
  } else {
    /* For raw datums, a already points at the first member. */
    HQASSERT( length >= 3 &&
              (a[p_id].type == SW_DATUM_TYPE_INTEGER ||
               a[p_id].type == SW_DATUM_TYPE_STRING) &&
              a[p_data].type == SW_DATUM_TYPE_STRING &&
              (length < 4 || a[p_id].type != SW_DATUM_TYPE_STRING ||
               (a[p_strtype].type == SW_DATUM_TYPE_INTEGER &&
                miscop != PCL_MISCOP_DEFINE_SYMBOLSET)),
              "Bad miscop") ;
    if (a[p_id].type == SW_DATUM_TYPE_INTEGER)
      id = a[p_id].value.integer ;
    else {
      idstr = &a[p_id] ;
      if (length > 3)
        id = a[p_strtype].value.integer ;
    }
    bytes = &a[p_data] ;
  }

  error = (miscop == PCL_MISCOP_DEFINE_SYMBOLSET) ?
    pcl_define_symbolset(ws, id, (uint8*)bytes->value.string, bytes->length) :
    pcl_define_font(ws, id, idstr, (uint8*)bytes->value.string, bytes->length) ;

  if (error) {
    /* There has been an error, return an XL-style error code */
    static sw_datum reply = SW_DATUM_INTEGER(0) ;

    reply.value.integer = error ;
    *param = &reply ;
  }

  return SW_PFIN_SUCCESS ;
}

/* -------------------------------------------------------------------------- */
/* Define a font character

   > [0] PCL_MISCOP_DEFINE_GLYPH
     [1] int/str ID    (of the font)
     [2] int     code
     [3] string  data
     [4] int     datatype (of ID if a string, optional)
*/

sw_pfin_result pcl_miscop_glyph(sw_pfin_instance * pfin,
                                sw_datum ** param, sw_datum * a)
{
  pfin_ufst5_instance * ws = (pfin_ufst5_instance *) pfin ;
  size_t length = (*param)->length ;

  int32 id = 0, code, error ;
  sw_datum datum, * bytes = &datum, str = SW_DATUM_NOTHING, * idstr = 0 ;

  enum {
    p_reason = 0, p_id, p_code, p_data, p_strtype
  } ;

  if ((*param)->owner) {
    /* Do things the official way for opaque datums */
    const sw_data_api * data = pfin->data_api ;

    if ( length < 4 ||
         data->get_indexed(*param,p_id,&str) != SW_DATA_OK ||
         (str.type != SW_DATUM_TYPE_INTEGER &&
          str.type != SW_DATUM_TYPE_STRING) ||
         !get_int(data,*param,p_code,&code) ||
         data->get_indexed(*param,p_data,&datum) != SW_DATA_OK ||
         datum.type != SW_DATUM_TYPE_STRING ||
         (length > 4 && str.type == SW_DATUM_TYPE_STRING &&
          !get_int(data,*param,p_strtype,&id)) )
      return SW_PFIN_ERROR_SYNTAX ;
    if (str.type == SW_DATUM_TYPE_INTEGER)
      id = str.value.integer ;
    else
      idstr = &str ;
  } else {
    /* For raw datums, a already points at the first member. */
    HQASSERT(length >= 4 &&
             (a[p_id ].type == SW_DATUM_TYPE_INTEGER ||
              a[p_id ].type == SW_DATUM_TYPE_STRING) &&
             a[p_code].type == SW_DATUM_TYPE_INTEGER &&
             a[p_data].type == SW_DATUM_TYPE_STRING &&
             (length < 5 || a[p_id].type != SW_DATUM_TYPE_STRING ||
              a[p_strtype].type == SW_DATUM_TYPE_INTEGER),
             "Bad miscop") ;
    if (a[p_id].type == SW_DATUM_TYPE_INTEGER)
      id = a[p_id].value.integer ;
    else {
      idstr = &a[p_id] ;
      if (length > 4)
        id = a[p_strtype].value.integer ;
    }
    code  = a[p_code].value.integer ;
    bytes = &a[p_data] ;
  }

  error = pcl_define_glyph(ws, id, idstr, code,
                           (uint8*)bytes->value.string, bytes->length) ;

  if (error) {
    /* There has been an error, return an XL-style error code */
    static sw_datum reply = SW_DATUM_INTEGER(0) ;

    reply.value.integer = error ;
    *param = &reply ;
  }

  return SW_PFIN_SUCCESS ;
}

/* ========================================================================== */
/* \brief The UFST module's PFIN miscop implementation.
 *
 * It is through this general purpose interface that all PCL-specific operations
 * are performed.
 */

static
sw_pfin_result RIPCALL pfin_ufst5_miscop(sw_pfin_instance * pfin,
                                         sw_pfin_define_context *define_context,
                                         sw_datum** param)
{
  /* Downcast to UFST PFIN instance subclass */
  pfin_ufst5_instance * ws = (pfin_ufst5_instance *) pfin ;
  sw_pfin_result result = SW_PFIN_ERROR_UNKNOWN ;
  int32 miscop ;
  sw_datum datum, * a ;

  UNUSED_PARAM(sw_pfin_define_context *,define_context) ;

  CHECK_WS(ws) ;

  if (param == 0 || (*param)->type != SW_DATUM_TYPE_ARRAY ||
      (*param)->length < 1)
    return SW_PFIN_ERROR_SYNTAX ;

  if ((*param)->owner) {
    /* Opaque datums require runtime type checking */
    /* This will be a miscop issued from PostScript */
    if (pfin->data_api->get_indexed(*param,0,&datum) != SW_DATA_OK ||
        datum.type != SW_DATUM_TYPE_INTEGER)
      return SW_PFIN_ERROR_SYNTAX ;
    a = &datum ;
  } else {
    /* Raw datum indicates an internal interface, so only need to assert */
    a = (sw_datum*) (*param)->value.opaque ;
    HQASSERT(a && a->type == SW_DATUM_TYPE_INTEGER, "Bad miscop") ;
  }
  /* a now points at the miscop array parameter first member, which is also
     the whole array in the raw case */

  miscop = a->value.integer ;
  switch (miscop) {

  case PCL_MISCOP_SPECIFY:
    result = pcl_miscop_specify(pfin, param, a) ;
    break ;

  case PCL_MISCOP_SELECT:
    result = pcl_miscop_select(pfin, param, a) ;
    break ;

  case PCL_MISCOP_METRICS:
    result = pcl_miscop_metrics(pfin, param, a) ;
    break ;

  case PCL_MISCOP_XL:
    result = pcl_miscop_xl(pfin, param, a) ;
    break ;

  case PCL_MISCOP_FONT:
  case PCL_MISCOP_SYMBOLSET:
    result = pcl_miscop_control(pfin, param, a, miscop) ;
    break ;

  case PCL_MISCOP_DEFINE_FONT:
  case PCL_MISCOP_DEFINE_SYMBOLSET:
    result = pcl_miscop_define(pfin, param, a, miscop) ;
    break ;

  case PCL_MISCOP_DEFINE_GLYPH:
    result = pcl_miscop_glyph(pfin, param, a) ;
    break ;
  }

  return result ;
}

/* ========================================================================== */
/* utility functions used by pfin_ufst5_metrics() and pfin_ufst5_outline() */

static HqBool supports_all_symbol_sets(uint8 char_requirements[8],
                                       uint8 char_complement[8],
                                       int8 font_encoding)
{
  int32 i ;
  int8 ss_encoding = UNKNOWN_CHAR_CODES ;


#if 0
  /* It seems that the last 3 bits of the character complement is NOT
     used when checking font compatibility. */
  if ((char_requirements[7] & 0x7) & (char_complement[7] & 0x7))
    return FALSE ;
#else
  if ( (char_requirements[7] & 0x7) == 0 ) {
    ss_encoding = MSL_CHAR_CODES ;
  } else if ( (char_requirements[7] & 0x7) == 1 ) {
    ss_encoding = UNICODE_CHAR_CODES ;
  }
#endif

  if (ss_encoding != font_encoding)
    return FALSE ;

  for (i=0; i<7; i++) {
    if (char_requirements[i] & char_complement[i])
      return FALSE ;
  }
  /* Low 3 bits specify vocabulary which we ignore. */
  if ((char_requirements[7] >> 3) & (char_complement[7] >> 3))
    return FALSE ;

  return TRUE ;
}

static int32 get_symbolset_char_requirements(pfin_ufst5_instance *ws,
                                             int32 ss, uint8 char_requirements[8],
                                             int8 font_encoding)
{
  PCL_SSINFO * ssList = ws->psslist ;
  int32 i, symbol_set ;
  uint8 *chrReq ;
  user_ss *userss = 0 ;

  HQASSERT(char_requirements, "Nowhere to put character requirements!") ;

  symbol_set = find_symbolset(ws, ss, &userss) ;

  if (userss) {
    chrReq = &userss->data[10] ;
    for (i=0; i<8; i++) {
      char_requirements[i] = chrReq[i] ;
    }
  } else {
    for (i=0; i<8; i++) {
      char_requirements[i] = 0 ;
    }

    if (font_encoding == 1) {
      chrReq = ssList[symbol_set].msl_chrReq ;
      char_requirements[0] = chrReq[0] ;
      char_requirements[3] = chrReq[1] ;
      char_requirements[7] = chrReq[2] ;
    } else {
      chrReq = ssList[symbol_set].unicode_chrReq ;
      char_requirements[4] = chrReq[0] ;
      char_requirements[5] = chrReq[1] ;
      char_requirements[7] = chrReq[2] ;
    }

  }

  return symbol_set ;
}

static signed int find_font(sw_pfin_instance * pfin, const sw_pfin_font * font,
                            int32 * ss, int32 * bold, int32 * faux,
                            int32 * wmode, int32 * override_height)
{
  /* pfin_ufst5_instance *ws = (pfin_ufst5_instance *)pfin; */
  static sw_datum key = SW_DATUM_STRING("PFID");
  static sw_datum wkey = SW_DATUM_STRING("WMode");
  static sw_datum vkey = SW_DATUM_STRING("VMode");
  sw_datum arr, value, sset, height, *h = &height, *set = &sset;
  /* indirection to avoid bogus compiler warning */
  int32 vmode = 0;
  signed int fid = -1;
  /*  UFST_SHOW("find_font\n"); */

  if ( pfin->data_api->get_keyed(&font->font, &key, &arr) == SW_DATA_OK &&
       arr.type == SW_DATUM_TYPE_ARRAY && arr.length >= 3 &&
       pfin->data_api->get_indexed(&arr, 0, &value) == SW_DATA_OK &&
       value.type == SW_DATUM_TYPE_INTEGER &&
       pfin->data_api->get_indexed(&arr, 1, &sset) == SW_DATA_OK &&
       sset.type == SW_DATUM_TYPE_INTEGER &&
       pfin->data_api->get_indexed(&arr, 2, &height) == SW_DATA_OK &&
       height.type == SW_DATUM_TYPE_INTEGER ) {
    fid = value.value.integer;
  }

  if ( pfin->data_api->get_keyed(&font->font, &wkey, &value) == SW_DATA_OK &&
       value.type == SW_DATUM_TYPE_INTEGER )
    *wmode = value.value.integer ;
  else
    *wmode = 0 ;  /* The default value */

  if ( pfin->data_api->get_keyed(&font->font, &vkey, &value) == SW_DATA_OK &&
       value.type == SW_DATUM_TYPE_INTEGER )
    vmode = value.value.integer ;
  else
    vmode = 0 ;  /* The default value */

  *wmode = ((vmode << 1) | *wmode) ;  /* The combined wmode */

  if (arr.length >= 4) {
    /* XL emboldening */
    sw_datum enbolden;
    if ( pfin->data_api->get_indexed(&arr, 3, &enbolden) == SW_DATA_OK &&
         enbolden.type == SW_DATUM_TYPE_INTEGER &&
         enbolden.value.integer > -1 )
      *bold = enbolden.value.integer ;
  }

  if (arr.length >= 5) {
    /* PCL5 faux italicisation */
    sw_datum italic ;
    if ( pfin->data_api->get_indexed(&arr, 4, &italic) == SW_DATA_OK &&
         italic.type == SW_DATUM_TYPE_INTEGER )
      *faux = italic.value.integer ;
  }

  *ss = set->value.integer;  /* the internal UFST, not PCL, symbolset number */
  *override_height = h->value.integer;
  return fid;
}

/* -------------------------------------------------------------------------- */

static signed int find_glyph(sw_pfin_instance * pfin, int fid,
                             const sw_datum* glyph)
{
  pfin_ufst5_instance *ws = (pfin_ufst5_instance *)pfin;
  signed int cid = -1;
  /* UFST_SHOW("find_glyph %s\n", glyph->value.string); */

  if (fid >= 0) {
    switch (glyph->type) {

    case SW_DATUM_TYPE_INTEGER:
      cid = glyph->value.integer;
      break;

    case SW_DATUM_TYPE_STRING:
      cid = codepoint_from_glyphname(ws, glyph);
      break;
    }
  }

  return cid;
}

/* ========================================================================== */
/* Return the metrics of a glyph */

sw_pfin_result RIPCALL pfin_ufst5_metrics(sw_pfin_instance * pfin,
                                          const sw_pfin_font* font,
                                          const sw_datum* glyph,
                                          double metrics[2])
{
  /* Downcast to UFST PFIN instance subclass */
  pfin_ufst5_instance *ws = (pfin_ufst5_instance *)pfin;
  int fid, cid;
  int32 ss = UNICODE;
  int32 bold = 0, faux = 0, wmode = 0;
  sw_pfin_result err;
  double maxbitmap, emx ;
  int32 override_height = -1;

  CHECK_WS(ws);

  UFST_SHOW("pfin_ufst5_metrics\n");

  if ((fid = find_font(pfin, font, &ss, &bold, &faux, &wmode,
                        &override_height)) == -1 ||
      (cid = find_glyph(pfin, fid, glyph)) == -1)
    return SW_PFIN_ERROR_UNKNOWN;

  /* 20080808 - Use bitimage hint now, when making the glyph */
  switch (font->prefer) {
  case PFIN_BITIMAGE_PREFERRED:
  case PFIN_OUTLINE_PREFERRED:
    /* Override PFIN preference by our own ppem threshold, unless it is
       negative meaning disabled */
    maxbitmap = (bold > 0) ? 1000 : ws->maxbitmap ;
    ws->make_bitmaps = (maxbitmap < 0) ?
                       font->prefer == PFIN_BITIMAGE_PREFERRED :
                       font->dy * ws->maxbitmap > 1.0f ;
    break ;
  default:
    /* Obey PFIN requirement */
    ws->make_bitmaps = font->prefer != PFIN_OUTLINE_REQUIRED ;
    break ;
  }

  ws->context = NULL ;
  err = make_char(ws, font, fid, cid, ss, bold, faux, TRUE, wmode,
                  override_height);
  if (err)
    return err;

  /* This is an adjustment to account for INTELLIFONT points being slightly
   * different from PostScript points.
   */
  if (ws->bitmap) {
    /* UFST casts two different structures as the same thing, and though they
     * share some field names, those fields are not necessarily in the same
     * place. Hence this apparent duplication. */
    PIFBITMAP pbm = (PIFBITMAP) ws->pol ;
    emx = (double)pbm->du_emx ;
    metrics[0] = (double)pbm->escapement / emx ;
  } else {
    emx = (ws->pol->du_emx == INTELLIFONT) ? 72.307f / 72 * INTELLIFONT :
                                             (double)ws->pol->du_emx ;
    metrics[0] = (double)ws->pol->escapement / emx ;
  }
  metrics[1] = 0;

  return SW_PFIN_SUCCESS;
}

/* ========================================================================== */
/* Simple x2 resolution enhancement */

#ifdef ENHANCED_RESOLUTION

/* NOTE This is a work in progress, and is not quite production ready */

void do_x2(uint8* b, int32 width, int32 height, uint8* d) {
  
  int    top, mid, bot ;    /* input shift registers */
  int    max, min ;         /* additional lines for 5x5 window */
  int    out[2] ;           /* output shift registers */
  int    in, bits ;         /* shift register widths */
  int32  y ;                /* line number */
  int    x ;                /* byte number */
  int    line ;             /* line stride */
  uint8 *bitmap = b ;       /* input bitmap */
  int    i ;                /* index into array */
  int32  next, end ;        /* offset to next output line, and ditto - 1 */

  enum {T=0x700,L=0x400,C=0x200,R=0x100,B=0x700} ;  /* sliding window masks */
  enum {FL=0x800,FR=0x080} ;                        /* far neighbours */
  enum {A=1,S,I=S+6} ;                              /* sliding window shifts */

  static uint8 x2[256] = {
    /* White pixels only - black pixels stay as is for 2x, so aren't stored.
       1 2
       4 8           26.5, 45.0 and 63.5 degree lines detected and smoothed.

       Anything larger than 15 is a case for the 5x5 sniffing code.
     */
    
    0,0,0,0,         0,0,0,0,        0,0,1,1,          0,0,3,0x20,
    0,0,2,3,         0,0,2,0x21,     0,0,3,0x23,       0,0,1,0,
    0,0,0,0,         0,0,0,0,        0,0,5,5,          0,0,1,0x11,
    0,0,2,3,         0,0,2,3,        0,0,3,2,          0,0,1,0x16,

    0,0,0,0,         0,0,0,0,        4,5,5,4,          4,5,5,4,
    8,8,10,10,       10,10,2,10,     12,12,0,14,       12,12,13,12, /* last 0 was 15... */
    0,0,0,0,         0,0,0,0,        4,0x22,1,0,       4,5,1,0x19,
    12,12,10,10,     8,8,8,3,        8,8,11,10,        8,5,9,8,

    0,0,0,0,         0,0,0,0,        0,0,1,1,          0,0,3,3,
    0,0,10,2,        0,0,0x1E,0x10,  0,0,3,2,          0,0,1,0x17,
    0,0,0,0,         0,0,0,0,        0,0,5,5,          0,0,1,0,
    0,0,10,2,        0,0,10,0,       0,0,3,5,          0,0,10,0,

    0,0,0,0,         0,0,0,0,        12,4,5,4,         5,4,5,3,
    8,8,2,2,         0x1F,10,0,0x14, 4,4,7,6,          4,10,5,4,
    0,0,0,0,         0,0,0,0,        0x1D,0x12,1,0x18, 12,0,12,0,
    0x1C,12,2,12,     0x13,0,0x15,0,  0,0x1B,3,2,       0x1A,0,1,0,
  } ;
  

  width = (width+7)>>3 ;  /* now bytes per line */
  line = width ;          /* in this implementation */
  end = width * 2 - 1 ;   /* offset to last byte of output line */
  next = width * 2 ;      /* or may be padded to word boundary */
  
  for (y=0 ; y < height ; y++) {
    b = bitmap + y * line ;         /* start of this scanline */
    bits = 0 ;                      /* no output bits accumulated yet */
    out[0] = out[1] = 0 ;           /* output lines empty */
    in = 8 ;                        /* we're about to load 8 input bits */
    /* Initialise the sliding window */
    max = (y < 2) ? 0 : b[-line<<1] << S ;
    top = (y < 1) ? 0 : b[-line] << S ;
    bot = (y >= height-1) ? 0 : b[line] << S ;
    min = (y >= height-2) ? 0 : b[line<<1] << S ;
    mid = *b++ << S ;
    /* For each input pixel... */
    for (x=0 ; x <= end ; ) {
      
      if ((mid & C) == C) {
        /* Black pixels don't change in this x2 design */
        i = 15 ;
      } else {
        /* White pixels can change */
        i = (top & T) | ((mid & R) << 3) | ((mid & L) << 2) | ((bot & B) << 5) ;
        if ((i & 90<<I) == 0) {
          /* but only if there is an immediate neighbour */
          i = 0 ;
        } else {
          i = x2[i>>I] ;
          if (i > 15) {
            /* Unfortunately, a 3x3 window is slightly too small so mistakes
               some curve radii as corners, eg:
               ******
               *###
               *#?   <-- this looks like a sharp corner, but isn't
               *#
               *
               *
               Consequently we have to peek into the larger 5x5 window for these
               cases. We do this in code to avoid expanding the 256 byte table
               into a 16MB one!
            */
            switch (i-16) {
              /* curved inner corners */
              case 0: /* top left corner */
                i = ((top & FR) == 0 && (min & L) == 0) ? 2 : 0 ;
                break ;
              case 1: /* top right corner */
                i = ((top & FL) == 0 && (min & R) == 0) ? 1 : 0 ;
                break ;
              case 2: /* bottom right corner */
                i = ((bot & FL) == 0 && (max & R) == 0) ? 4 : 0 ;
                break ;
              case 3: /* bottom left corner */
                i = ((bot & FR) == 0 && (max & L) == 0) ? 8 : 0 ;
                break ;

              /* diagonal joins */
              case 4: /* R right join */
                i = ((min & R) != 0) ? 8 : 0 ;
                break ;
              case 5: /* 2 join */
                i = ((max & R) != 0) ? 2 : 0 ;
                break ;
              case 6: /* d lower join */
                i = ((bot & FL) != 0) ? 2 : 0 ;
                break ;
              case 7: /* R lower join */
                i = ((bot & FR) != 0) ? 1 : 0 ;
                break ;
              case 8: /* Hflip 2 join */
                i = ((max & L) != 0) ? 1 : 0 ;
                break ;
              case 9: /* Z top join */
                i = ((min & L) != 0) ? 4 : 0 ;
                break ;
              case 10: /* K top join */
                i = ((top & FR) != 0) ? 4 : 0 ;
                break ;
              case 11: /* Hflip K top join */
                i = ((top & FL) != 0) ? 8 : 0 ;
                break ;

              /* shallow corrections */
              case 12: i = ((mid & FR)!=0) ? 8 : 12 ; break ;
              case 13: i = ((mid & FL)!=0) ? 4 : 12 ; break ;
              case 14: i = ((min & C)!=0) ? 2 : 10 ; break ;
              case 15: i = ((max & C)!=0) ? 8 : 10 ; break ;
              case 16: i = ((mid & FL)!=0) ? 1 : 3 ; break ;
              case 17: i = ((mid & FR)!=0) ? 2 : 3 ; break ;
              case 18: i = ((max & C)!=0) ? 4 : 5 ; break ;
              case 19: i = ((min & C)!=0) ? 1 : 5 ; break ;
                

            }
          }
        }
      }
      
      out[0] = out[0] << 2 | (i & 3) ;
      out[1] = out[1] << 2 | (i & 12) ; /* note: left shifted by two bits */
      bits += 2 ;
      if (bits == 8) {
        d[next] = (uint8)(out[1] >> 2) ;  /* output lower line first */
        *d++ = (uint8)out[0] ;            /* then higher line and increment */
        x++ ;
        bits = 0 ;
      }
      
      max <<= 1 ;
      top <<= 1 ;
      mid <<= 1 ;
      bot <<= 1 ;
      min <<= 1 ;
      in-- ;
      if (in == 1 && x < end) {
        if (y > 0) {
          top |= b[-line] << A ;
          if (y > 1)
            max |= b[-line<<1] << A ;
        }
        if (y < height-1) {
          bot |= b[line] << A ;
          if (y < height-2)
            min |= b[line<<1] << A ;
        }
        mid |= *b++ << A ;
        in += 8 ;
      }
      
    }
    d += next ;  /* skip lower line - only correct if no padding on output */
  }
}
#endif

/* ========================================================================== */
/* Return a glyph definition */

sw_pfin_result RIPCALL pfin_ufst5_outline(sw_pfin_instance *instance,
                                          sw_pfin_outline_context *context,
                                          const sw_pfin_font* font,
                                          const sw_datum* glyph)
{
  /* Downcast to UFST PFIN instance subclass */
  pfin_ufst5_instance *ws = (pfin_ufst5_instance *)instance;
  int            cid, fid;
  int32          ss = UNICODE, bold = 0, faux = 0, wmode = 0;
  sw_pfin_result err;
  fco_font     *thefont;
  int32 override_height = -1;

  CHECK_WS(ws);
  UFST_SHOW("pfin_ufst5_outline\n");

  if ((fid = find_font(instance, font, &ss, &bold, &faux, &wmode,
                        &override_height)) == -1 ||
      (cid = find_glyph(instance, fid, glyph)) == -1)
    return SW_PFIN_ERROR_UNKNOWN;

  UFST_SHOW("pfin_ufst5_outline fid=%d, cid=%d\n", fid, cid);

  ws->context = context ;  /* in case make_char needs it */
  err = make_char(ws, font, fid, cid, ss, bold, faux, TRUE, wmode,
                  override_height);
  if (err)
    return err;

  /* At this point, ws->pol is the generated glyph, and ws->bitmap is true
     if the glyph is a bitmap. */

  /* Build the path, get character metrics, push onto op stack */
  if (ws->bitmap) {
    /* We made a bitimage */
    PIFBITMAP pbm = (PIFBITMAP) ws->pol;
    double xres = ws->xres, yres = ws->yres;
    uint8 *bitmap = (uint8*) pbm->bm;
    int32 width = pbm->width * 8, height = pbm->depth;
    int32 xorigin = pbm->xorigin, yorigin = pbm->yorigin;
#ifdef ENHANCED_RESOLUTION
    int   enhanced = FALSE;

    while (xres*2 <= font->xdpi && yres*2 <= font->ydpi) {
      /* Resolution enhancement */
      size_t size = height * width>>1 ;
      
      if (size > ws->x2size) {
        if (ws->x2) {
          myfree(ws->x2) ;
          ws->x2size = 0 ;
        }
        size = (size+1023) &~ 1023 ;
        ws->x2 = myalloc(size);
        if (ws->x2 == 0) break ;
        ws->x2size = size ;
      }

      do_x2(bitmap,width,height,ws->x2);

      xres *= 2;
      yres *= 2;
      width <<= 1;
      height <<= 1;
      xorigin <<= 1;
      yorigin <<= 1;
      if (enhanced)
        myfree(bitmap);
      bitmap = ws->x2;
      enhanced = TRUE;
    }
#endif

    err = instance->callbacks->bitimage(context, width, height,
      -(xorigin + 8) / 16, height - ((yorigin + 8) / 16),
      xres, yres, bitmap,  ((width+7)>>3) * height);
#ifdef ENHANCED_RESOLUTION
/*    if (enhanced)
      myfree(bitmap);*/
#endif
    if (err)
      return err;

  } else {
    /* We have an outline (or a blank space) */
    double      xscale = 1.0f / 32768.0f, yscale = 1.0f / 32768.0f;
    double      xoff = 0.0f, yoff = 0.0f;
    int32       nNumCPs, nNumSegmts;
    int32       i, j;
    LPSB8       segment;
    PINTRVECTOR points;
    PIFOUTLINE  pol = ws->pol;
    POUTLINE_CHAR outchar = &pol->ol;

    /* font scale fiddle factor */
    if (fid < ws->TotalFonts) {
      thefont = &ws->Fonts[fid];
      xscale *= thefont->scale;
      yscale *= thefont->height;
    }
    /* Adjustment for INTELLIFONT point size */
    if (pol->du_emx == INTELLIFONT) {
      xscale *= 72.0f / 72.307f ;
      yscale *= 72.0f / 72.307f ;
    }
    xoff = 0.0f;
    yoff = 0.0f;

    nNumCPs = outchar->num_loops;
    for (i=0; i<nNumCPs; i++) {
      nNumSegmts = outchar->loop[i].num_segmts;

      segment = (LPSB8)((LPSB8)(outchar->loop) + outchar->loop[i].segmt_offset);
      points =
        (PINTRVECTOR)((LPSB8)(outchar->loop) + outchar->loop[i].coord_offset);

      if (instance->callbacks->move(context, 0, 0))
        return SW_PFIN_ERROR;

      for (j=0; j<nNumSegmts; j++) {
        switch(*segment) {

        case 0x00: /* Moveto */
          if ((err = instance->callbacks->move(context,
                       ((double)points->x * xscale)+xoff,
                       ((double)points->y * yscale)+yoff)) != SW_PFIN_SUCCESS) {
            HQFAIL("pfin->move failed");
            return err;
          }
          /*  UFST_SHOW("MoveTo %d %d\n", points->x, points->y); */
          points++;
          break;

        case 0x01: /* Lineto */
          if ((err = instance->callbacks->line(context,
                       ((double)points->x * xscale)+xoff,
                       ((double)points->y * yscale)+yoff)) != SW_PFIN_SUCCESS) {
            HQFAIL("pfin->line failed");
            return err;
          }
          /*  UFST_SHOW("LineTo %d %d\n", points->x, points->y); */
          points++;
          break;

        case 0x02: /* Quadto Bad ! */
          HQFAILV(("segment type Quadto not implemented %d\n", *segment));
          points++;
          break;

        case 0x03: /* Curveto */
          {
            /* use temporary variables here for the VxWorks compiler which
             * cannot cope with "complex" floating point expressions
             */
            double x0, y0, x1, y1, x2, y2;

            x0 = ((double)points[0].x * xscale) + xoff;
            y0 = ((double)points[0].y * yscale) + yoff;
            x1 = ((double)points[1].x * xscale) + xoff;
            y1 = ((double)points[1].y * yscale) + yoff;
            x2 = ((double)points[2].x * xscale) + xoff;
            y2 = ((double)points[2].y * yscale) + yoff;

            if ((err = instance->callbacks->curve(context,
                       x0, y0, x1, y1, x2, y2)) != SW_PFIN_SUCCESS) {
              HQFAIL("  instance->callbacks->curve failed 0");
              return err;
            }
          }
          /* UFST_SHOW("Curve %d %d, %d %d, %d %d\n",
                     points[0].x, points[0].y, points[1].x, points[1].y,
                     points[2].x, points[2].y); */

          points+=3;
          break;

        default:
          HQFAILV(("Unknown segment type %d\n", *segment));
          return SW_PFIN_ERROR;
        }
        segment++;
      }
    }
  } /* glyph type */

  return SW_PFIN_SUCCESS;
}

/* ========================================================================== */
/** \brief UFST module PFIN configuration - setpfinparams

    This implements the UFST module's handling of its setpfinparams parameter,
    which is expected to be a dictionary, eg:

    \code
    <<
      /UFST<<
        /DarkCourier      <boolean>
        /DefaultSymbolSet <integer> | <string>
        /Emulate4700      <boolean>
        /ListPSFonts      <boolean>
        /MatchPSFonts     <boolean>
        /MaxBitmap        <integer>
      >>
    >>setpfinparams
    \endcode

    /DarkCourier  This controls whether the heavier Courier font is used in FCOs
                  with more than one Courier. For FCOs with only one Courier it
                  is ignored. The default is dependent on the ordering of the
                  two font variants in the FCO (or fontmap).

    /DefaultSymbolSet  The symbolset to use if the job-specified symbolset is
                       not recognised. This can be specified as an integer, eg
                       "8U" = 8*32+'U'-'@' = 277, or more conveniently as a
                       string, eg "8U". The default is 8U if found in the
                       symbolset list, or the first entry in that list.

    /Emulate4700  This boolean switches emulation of unexpected HP4700 behaviour
                  including the case where Criteria overridden by selecting a
                  bitmap font by ID are calculated as though the font is 300dpi
                  regardless of its actual resolution. Default is true, so the
                  'unexpected behaviour' is emulated.

    /ListPSFonts  If true, the module will list (ie define) all its PS fonts in
                  response to a resourceforall - this currently has the effect
                  of overridding any existing RIP fonts of the same name, so the
                  default is false - no fonts defined during resourceforall.

    /MatchPSFonts  This controls whether FCO fonts that do not have a PCL5
                   typeface number can nevertheless be matched by a PCL5 font
                   match by criteria. Default is false - PS fonts ignored.

    /MaxBitmap  This integer is the greatest number of pixels per em for which
                a bitmap will be generated, if greater than zero. Zero means
                never produce bitmaps, negative means defer to PFIN's stated
                preference. The default is 540, which is 40pt @ 300dpi.
*/

/* Neat way of defining sw_data_match arrays */
#define REQUIREDKEY(type,name) {SW_DATUM_BIT_##type,SW_DATUM_STRING(""name"")}
#define OPTIONALKEY(type,name) {SW_DATUM_BIT_##type|SW_DATUM_BIT_NOTHING,SW_DATUM_STRING(""name"")}

sw_pfin_result RIPCALL pfin_ufst5_config(sw_pfin_instance* pfin,
                                         sw_pfin_define_context* define,
                                         const sw_datum* config)
{
  /* Downcast to UFST PFIN instance subclass */
  pfin_ufst5_instance *ws = (pfin_ufst5_instance *)pfin;

  /* Easy way of detecting and handling config keys... */
  static sw_data_match match[] = {
    OPTIONALKEY(BOOLEAN,"DarkCourier"),
    OPTIONALKEY(INTEGER|SW_DATUM_BIT_STRING,"DefaultSymbolSet"), /* icky :-( */
    OPTIONALKEY(BOOLEAN,"Emulate4700"),
    OPTIONALKEY(BOOLEAN,"ListPSFonts"),
    OPTIONALKEY(BOOLEAN,"MatchPSFonts"),
    OPTIONALKEY(INTEGER,"MaxBitmap")
  };
  enum{
    DarkCourier, DefaultSymbolSet, Emulate4700,
    ListPSFonts, MatchPSFonts, MaxBitmap
  };

  UNUSED_PARAM(sw_pfin_define_context *, define);

  /* Sanity check */
  CHECK_WS(ws);
  UFST_SHOW("pfin_ufst5_config\n");

  /* Do a config match, and only proceed if successful */
  if (pfin->data_api->match(config, match, lengthof(match)) == SW_DATA_OK) {
    sw_datum * ss ;

    /* Get the default Symbolset, if present */
    ss = &match[DefaultSymbolSet].value ;
    if (ss->type != SW_DATUM_TYPE_NOTHING) {
      int32 defaultss = -1 ;

      if (ss->type == SW_DATUM_TYPE_INTEGER)
        /* Numeric */
        defaultss = ss->value.integer;
      else {
        /* Decode string into symbolset number */
        if (ss->length > 1 && ss->length < 7) {
          uint8 * ptr = (uint8 *) ss->value.string ;
          int32 c, i, n = 0 ;
          for (i = (int32) ss->length; i > 1; i--) { /* note: length-1 times */
            c = *ptr++ ;
            if (c < '0' || c > '9')
              break ; /* value of i will flag error to if() below */
            else
              n = n * 10 + c - '0' ;
          }
          c = *ptr++ ;
          if (i == 1 && c >= 'A' && c <= 'Z' && c != 'X')
            defaultss = n * 32 + c - '@' ; /* valid symbolset string */
        }
      }

      if (defaultss > -1 && defaultss < 65536) {
        user_ss * userss = 0 ;
        int32 oldss = ws->defaultss ;
        ws->defaultss = -1;
        ws->defaultss = find_symbolset(ws, defaultss, &userss);
        if (ws->defaultss == -1 || userss)
          ws->defaultss = oldss;
      }
    }

    /* Get the Dark Courier boolean if present, and switch if necessary */
    if (match[DarkCourier].value.type != SW_DATUM_TYPE_NOTHING) {
      HqBool darkcourier = match[DarkCourier].value.value.boolean;
      if (ws->bothcouriers && darkcourier != ws->darkcourier) {
        /* font list needs to be changed - it is sorted, so needs swapping */
        int i;
        for (i = 0; i < 4; i++) {
          /* Swap the fontmap entries */
          fco_font swap = ws->Fonts[ws->couriers[i]];
          ws->Fonts[ws->couriers[i]]       = ws->Fonts[ws->couriers[i+4]];
          ws->Fonts[ws->couriers[i+4]]     = swap;
          /* But keep ord as they were (in case of UNMAPPEDFONT) */
          ws->Fonts[ws->couriers[i+4]].ord = ws->Fonts[ws->couriers[i]].ord;
          ws->Fonts[ws->couriers[i]].ord   = swap.ord;
        }
        ws->darkcourier = darkcourier;
      }
    }

    /* Get the "match PS fonts" flag if present */
    if (match[MatchPSFonts].value.type != SW_DATUM_TYPE_NOTHING)
      ws->matchpsfonts = match[MatchPSFonts].value.value.boolean;

    /* Get the "emulate HP4700 behaviour" flag if present */
    if (match[Emulate4700].value.type != SW_DATUM_TYPE_NOTHING)
      ws->emulate4700 = match[Emulate4700].value.value.boolean;

    /* Get the "list fonts during resourceforall" flag if present */
    if (match[ListPSFonts].value.type != SW_DATUM_TYPE_NOTHING)
      ws->listpsfonts = match[ListPSFonts].value.value.boolean;

    /* Get "Maximum Bitmap Pixels per Em" threshold if present.
       0 = no bitmaps, -ve = use PFIN default threshold. */
    if (match[MaxBitmap].value.type != SW_DATUM_TYPE_NOTHING)
      ws->maxbitmap = match[MaxBitmap].value.value.integer ;
  }
  /* config is always successful */
  return SW_PFIN_SUCCESS;
}

/* ========================================================================== */
/* This is a work-around for 'unexpected behaviour' in UFST triggered by
 * returning a NULL glyph ID from get_chId2Ptr() (when code == 32) or
 * get_glyphId2Ptr(). The following valid glyph definitions will be recognised
 * as being a proxy for NULL when returned by UFST.
 */

#if (BYTEORDER == HILO)
UB8 null_bitmap[] = {
  4,   0,  /* Format 4 */
  14,  1,  /* Length 14, class 1 (uncompressed) */
  0,   0,  /* orientation 0 */
  0,   0,  /* left 0 */
  0,   0,  /* top 0 */
  1,   0,  /* width 1 */
  1,   0,  /* height 1 */
  0,   0,  /* deltaX 0 */
  0        /* white pixel */
} ;
#else
UB8 null_bitmap[] = {
  4,   0,  /* Format 4 */
  14,  1,  /* Length 14, class 1 (uncompressed) */
  0,   0,  /* orientation 0 */
  0,   0,  /* left 0 */
  0,   0,  /* top 0 */
  0,   1,  /* width 1 */
  0,   1,  /* height 1 */
  0,   0,  /* deltaX 0 */
  0        /* white pixel */
} ;
#endif

UB8 null_if[] = {
  10,  0,   /* Format 10 */
  2,   4,   /* Length 2, class 4 (Intellifont compound chr) */
  0,   0,   /* Escapement 0 */
  0,   0,   /* numparts 0, spare */
            /* no code;X;Y; */
  0,   0xff /* pad, checksum */
} ;

UB8 null_tt[] = {
  15,   0,  /* Format 15 */
  2,   15,  /* Length 2, class 15 */
  0,   15,  /* Chr Data Size */
  255, 255, /* GlyphID */
  0,   0,   /* Number of contours 0 */
  0,   0,   /* xmin 0 */
  0,   0,   /* ymin 0 */
  0,   1,   /* xmax 1 */
  0,   1,   /* ymax 1 */
  0,   0,   /* instructions 0 */
  0         /* flags 0 */
  /* no further data necessary */
} ;

UB8 null_xl[] = {
  1,    0,  /* Format 1, Class 0 */
  0,   12,  /* Chr Data Size */
  255, 255, /* GlyphID */
  0,   0,   /* Number of contours 0 */
  0,   0,   /* xmin 0 */
  0,   0,   /* ymin 0 */
  0,   1,   /* xmax 1 */
  0,   1    /* ymax 1 */
  /* no further data necessary */
} ;

UB8 null_xlbitmap[] = {
  0,   0,  /* Format 0, class 0 */
  0,   0,  /* left 0 */
  0,   0,  /* top 0 */
  0,   1,  /* width 1 */
  0,   1,  /* height 1 */
  0        /* white pixel */
} ;


LPUB8 null_Id2Ptr(void)
{
  get_id_null = TRUE ;

  switch (get_id_font->type) {
  case TYPE_IF:        return NULL;
  case TYPE_BITMAP:    return null_bitmap ;
  case TYPE_TT:
  case TYPE_TT16:      return null_tt ;
  case TYPE_XL:        return null_xl ;
  case TYPE_XLBITMAP:  return null_xlbitmap ;
  }
  return NULL ;
}

/* -------------------------------------------------------------------------- */

#define SSMAPPING 1

#if SSMAPPING
/* A mapping from CG to MSL as two arrays of 866 numbers sorted by CG */

UW16 CG_nums[] = {
    1,    2,    3,    4,    5,    6,    7,    8,    9,   10,   11,   12,   13,
   14,   15,   16,   17,   18,   19,   20,   21,   22,   23,   24,   25,   26,
   27,   28,   29,   30,   31,   32,   33,   34,   35,   36,   37,   38,   39,
   40,   41,   42,   43,   44,   45,   46,   47,   48,   49,   50,   51,   52,
   53,   85,   86,   87,   88,   96,   97,   98,   99,  106,  172,  173,  174,
  175,  176,  177,  231,  232,  233,  234,  235,  236,  238,  239,  240,  241,
  242,  243,  244,  245,  246,  247,  248,  249,  250,  251,  252,  253,  254,
  255,  256,  257,  258,  259,  260,  261,  263,  266,  267,  268,  269,  276,
  291,  298,  299,  306,  307,  308,  349,  363,  364,  365,  366,  367,  368,
  369,  370,  371,  374,  375,  376,  377,  378,  380,  381,  382,  383,  384,
  385,  386,  387,  389,  390,  391,  392,  402,  403,  404,  405,  406,  407,
  408,  409,  410,  411,  412,  423,  424,  444,  445,  447,  448,  480,  493,
  497,  540,  541,  542,  551,  558,  562,  563,  564,  565,  566,  567,  568,
  569,  570,  571,  572,  573,  574,  575,  576,  577,  578,  579,  581,  582,
  583,  584,  585,  587,  588,  589,  591,  593,  594,  595,  596,  597,  599,
  664,  665,  719,  720,  721,  731,  733,  738,  739,  740,  745,  755,  756,
  757,  758,  759,  766,  767,  768,  769,  770,  771,  775,  777,  790,  791,
  812,  822,  830,  831,  839,  843,  844,  856,  857, 1094, 1215, 1228, 1231,
 1234, 1237, 1240, 1242, 1246, 1248, 1250, 1254, 1261, 1267, 1273, 1282, 1283,
 1286, 1303, 1307, 1323, 1324, 1335, 1374, 1404, 1406, 1407, 1408, 1451, 1565,
 1566, 1567, 1568, 1569, 1570, 1571, 1572, 1573, 1574, 1575, 1576, 1591, 1592,
 1593, 1594, 1595, 1596, 1597, 1598, 1599, 1600, 1601, 1602, 1603, 1619, 1620,
 1621, 1622, 1623, 1624, 1625, 1626, 1627, 1628, 1656, 1673, 1674, 1675, 1684,
 1700, 1702, 1703, 1704, 1705, 1706, 1707, 1708, 1709, 1711, 1712, 1713, 1714,
 1715, 1716, 1717, 1718, 1719, 1722, 1725, 1727, 1728, 1751, 1752, 1753, 1807,
 1825, 1827, 1846, 1847, 1857, 1858, 1859, 1860, 1861, 1904, 1969, 3171, 4757,
 4758, 4759, 4760, 4761, 4767, 4768, 4770, 4771, 4793, 7002, 7004, 7005, 7006,
 7009, 7018, 7248, 7249, 7250, 7251, 7252, 7253, 7254, 7255, 7256, 7274, 7275,
 7276, 7277, 7278, 7279, 7280, 7281, 7282, 7283, 7286, 7287, 7291, 7295, 7304,
 7305, 7306, 7307, 7308, 7309, 7310, 7311, 7312, 7313, 7314, 7315, 7316, 7317,
 7318, 7319, 7320, 7321, 7322, 7323, 7345, 7346, 7347, 7348, 7349, 7350, 7378,
 7379, 7380, 7381, 7382, 7383, 7384, 7385, 7386, 7387, 7388, 7389, 7390, 7391,
 7392, 7393, 7394, 7395, 7396, 7397, 7398, 7399, 7400, 7401, 7402, 7403, 7404,
 7405, 7406, 7407, 7408, 7409, 7410, 7411, 7412, 7413, 7414, 7415, 7416, 7417,
 7418, 7419, 7420, 7421, 7422, 7423, 7424, 7425, 7426, 7427, 7428, 7429, 7430,
 7431, 7432, 7433, 7434, 7435, 7436, 7437, 7438, 7439, 7440, 7441, 7442, 7444,
 7445, 7446, 7447, 7448, 7449, 7450, 7451, 7452, 7453, 7454, 7455, 7456, 7457,
 7458, 7459, 7460, 7461, 7462, 7463, 7464, 7466, 7467, 7468, 7469, 7470, 7471,
 7472, 7473, 7474, 7475, 7476, 7477, 7478, 7479, 7480, 7481, 7482, 7483, 7484,
 7485, 7486, 7487, 7488, 7489, 7490, 7491, 7492, 7493, 7494, 7495, 7496, 7497,
 7498, 7499, 7500, 7501, 7502, 7503, 7504, 7505, 7506, 7507, 7508, 7509, 7510,
 7511, 7512, 7513, 7514, 7515, 7516, 7517, 7518, 7519, 7520, 7521, 7522, 7523,
 7524, 7525, 7526, 7527, 7528, 7529, 7530, 7531, 7532, 7533, 7534, 7535, 7536,
 7537, 7538, 7539, 7540, 7541, 7542, 7543, 7544, 7545, 7546, 7547, 7548, 7549,
 7550, 7551, 7552, 7553, 7554, 7555, 7556, 7557, 7558, 7559, 7560, 7561, 7562,
 7563, 7564, 7565, 7566, 7567, 7568, 7569, 7570, 7571, 7572, 7573, 7574, 7575,
 7576, 7577, 7578, 7579, 7580, 7581, 7582, 7583, 7584, 7585, 7586, 7587, 7588,
 7589, 7590, 7591, 7592, 7593, 7594, 7595, 7596, 7597, 7598, 7599, 7600, 7601,
 7602, 7603, 7604, 7605, 7607, 7608, 7609, 7610, 7611, 7612, 7613, 7615, 7616,
 7617, 7618, 7619, 7620, 7621, 7622, 7623, 7679, 9133, 9134, 9135, 9136, 9137,
 9138, 9139, 9140, 9141, 9142, 9143, 9144, 9145, 9146, 9147, 9148, 9149, 9150,
 9151, 9152, 9153, 9154, 9155, 9156, 9157, 9158, 9159, 9160, 9161, 9162, 9163,
 9164, 9165, 9166, 9167, 9168, 9169, 9170, 9171, 9172, 9173, 9174, 9175, 9176,
 9177, 9178, 9179, 9180, 9181, 9182, 9183, 9184, 9185, 9186, 9187, 9188, 9189,
 9190, 9191, 9192, 9193, 9194, 9195, 9196, 9197, 9198, 9199, 9200, 9201, 9202,
 9203, 9204, 9205, 9206, 9207, 9208, 9209, 9210, 9211, 9212, 9213, 9214, 9215,
 9216, 9217, 9218, 9219, 9220, 9221, 9222, 9223, 9224, 9225, 9226, 9228, 9229,
 9230, 9231, 9232, 9233, 9234, 9235, 9236, 9237, 9238, 9239, 9240, 9241, 9242,
 9243, 9244, 9245, 9246, 9247, 9248, 9249, 9250, 9251, 9252, 9253, 9254, 9255,
 9256, 9257, 9258, 9259, 9260, 9261, 9262, 9263, 9264, 9265, 9266, 9267, 9268,
 9269, 9270, 9271, 9272, 9273, 9274, 9275, 9276, 9277, 9278, 9279, 9280, 9281,
 9282, 9283, 9284, 9285, 9286, 9287, 9288, 9289, 9290, 9291, 9292, 9293, 9294,
 9295, 9296, 9297, 9298, 9299, 9300, 9301, 9302, 9303, 9304, 9305, 9306, 9307,
 9308, 9309, 9310, 9311, 9312, 9313, 9314, 9315, 9316, 9317, 9318, 9319, 9320,
 9321, 9322, 9323, 9324, 9325, 9326, 9327, 9328, 9329, 9330, 9331, 9332, 9333,
 9334, 9335, 9336, 9337, 9338, 9339, 9340, 9341, 9342, 9343, 9344, 9345, 9346,
 9347, 9348, 9349, 9350, 9351, 9352, 9353, 9354
} ;

UW16 MSL_nums[] = {
   86,   81,   74,   80,   79,   78,   84,   73,   75,   82,   69,   88,   71,
   92,   70,   68,   85,   91,   72,   90,   67,   89,   76,   87,   83,   77,
   53,   48,   41,   47,   46,   45,   51,   40,   42,   49,   36,   55,   38,
   59,   37,   35,   52,   58,   39,   57,   34,   56,   43,   54,   50,   44,
    6,    1,   32,   66,    8,  188,  190,  121,  122,  221,  148, 1091,  152,
 1090, 1047,  159,  137,  133,  141,  129,  163,  149,  118,  138,  134,  142,
  130,  328,  150,  154,  158,  146,  120,  135,  139,  143,  131,  151,  136,
  140,  144,  132,  171,  401,  417,  429,  439,  451,  455,  457,  459,  117,
  147,  469,  467,  453,  475,  342, 1107,  164,  177,  165,  178,  114,  115,
 1061, 1063, 1065,  335,  404,  406,  422,  446,  462,  482,  484,  405,  407,
  423, 1096,  447,  463,  483,  485, 1095,  402,  420,  434,  454,  478,  403,
  421,  435,  479,  452,  474, 1093, 1092, 1017, 1018,  477,  476, 1036, 1104,
 1105,  410, 1106,  172,  101,  155,  468,  153,  145,  161,   99,  100,  162,
  160,  157,  102,  103,  167,  166,  104,  105,  119,  169,  168,  170,  174,
  111,  112,  156,  414,  442,  448,  466,  411,  415,  443,  449,  173, 1031,
  340,  341,  416,  176,  175,  339,  400,  438,  450,  444,  445,  486,  487,
  440,  441,  456,  418,  419,  470,  471,  480,  428,  338,  458, 1067, 1103,
    0,  432,  481,  433, 1068, 1062, 1064,  460,  461, 1019,  283,  285,  289,
  288,  290,  281,  291,  180,  284,  286,  294,  287,  301,  296, 1100, 1101,
  303, 1094, 1110,  297,  298,  295,  293,  189, 1111, 1109,  331,  653,   15,
   13,   27,   28,    9,   10,   60,   62,   14,  326,  325,   16,  332,   18,
   19,   20,   21,   22,   23,   24,   25,   26,   17,    4,  128,  200,  197,
  198, 1001, 1002, 1003, 1004, 1005, 1006, 1000,  324,  185,  184,  182,  302,
  124,    5,  127,   12,  183,  191,   30,  201,  202,  186,  187,  116,  312,
  327,  126,  181,   11,    3, 1028,  125,  310,  311, 1023, 1021, 1020,  232,
  123,   61, 1098, 1097,  316,  315, 1084, 1086, 1088,  179, 1069, 1060, 1040,
 1041, 1042, 1043, 1044,  196,  193,  313, 1034, 1099,  231,  230,  219,  218,
  661,  194,  308,   33,   93,   95,  305, 1108, 1102,  329,    2,  113,  314,
  233,  228,   31,   29,   63,   94,   64,   96,  192,  309,  306,  307,  322,
  330,  318,  317,  320,  319,  321, 1085,  323,  107,  106,  109,  108,  110,
 1084,  316,  199,  315, 1097, 1045, 1086, 1087, 1089, 1088, 1098, 1030,  254,
  236,  237,  250,  251,  252,  253,  248,  274,  249,  275,  273,  264,  266,
  238,  255,  247,  269,  270,  241,  263,  257,  258,  244,  245,  242,  259,
  260,  261,  262,  272,  256,  243,  239,  267,  265,  268,  271,  240,  246,
  276,  277,  280,  278,  279,  222,  235,  234,   97,  205,  206,  207,  208,
  203,  204,  215,  216,  214,  213,  217,  209,  211,  210,  212,  292,  299,
  300,  225,  224,  227,  226,  220,  223,  229,  628,  625,  626,  627,  554,
  552,  551,  553,  556,  555,  500,  501,  502,  503,  504,  505,  506,  507,
  508,  509,  510,  511,  512,  513,  514,  515,  516,  517,  518,  519,  520,
  522,  523,  524,  525,  526,  527,  528,  529,  530,  531,  532,  533,  534,
  535,  536,  537,  538,  539,  540,  541,  542,  543,  544,  545,  546,  547,
  548,  549,  550,  557,  558,  559,  560,  561,  562,  563,  564,  565,  566,
  567,  568,  569,  570,  571,  572,  573,  574,  575,  576,  577,  578,  579,
  580,  581,  582,  583,  584,  586,  585,  587,  588,  589,  590,  591,  592,
  593,  594,  595,  596,  597,  598,  599,  600,  601,  602,  603,  604,  605,
  606,  607,  608,  609,  610,  611,  612,  613,  614,  615,  616,  617,  618,
  619,  620,  621,  622,  623,  624,  629,  630,  631,  632,  633,  634,  635,
  636,  637,  638,  639,  640,  641,  642,  643,  644,  645,  646,  647,  648,
  649,  650,  651,  652,  654,  655,  656,  657,  658,  659,  660,  662,  663,
  664,  665,  666,  667,  521,  333,  668, 3812, 9133, 9134, 9135, 9136, 9137,
 9138, 9139, 9140, 9141, 9142, 9143, 9144, 9145, 9146, 9147, 9148, 9149, 9150,
 9151, 9152, 9153, 9154, 9155, 9156, 9157, 9158, 9159, 9160, 9161, 9162, 9163,
 9164, 9165, 9166, 9167, 9168, 9169, 9170, 9171, 9172, 9173, 9174, 9175, 9176,
 9177, 9178, 9179, 9180, 9181, 9182, 9183, 9184, 9185, 9186, 9187, 9188, 9189,
 9190, 9191, 9192, 9193, 9194, 9195, 9196, 9197, 9198, 9199, 9200, 9201, 9202,
 9203, 9204, 9205, 9206, 9207, 9208, 9209, 9210, 9211, 9212, 9213, 9214, 9215,
 9216, 9217, 9218, 9219, 9220, 9221, 9222, 9223, 9224, 9225, 9226, 9228, 9229,
 9230, 9231, 9232, 9233, 9234, 9235, 9236, 9237, 9238, 9239, 9240, 9241, 9242,
 9243, 9244, 9245, 9246, 9247, 9248, 9249, 9250, 9251, 9252, 9253, 9254, 9255,
 9256, 9257, 9258, 9259, 9260, 9261, 9262, 9263, 9264, 9265, 9266, 9267, 9268,
 9269, 9270, 9271, 9272, 9273, 9274, 9275, 9276, 9277, 9278, 9279, 9280, 9281,
 9282, 9283, 9284, 9285, 9286, 9287, 9288, 9289, 9290, 9291, 9292, 9293, 9294,
 9295, 9296, 9297, 9298, 9299, 9300, 9301, 9302, 9303, 9304, 9305, 9306, 9307,
 9308, 9309, 9310, 9311, 9312, 9313, 9314, 9315, 9316, 9317, 9318, 9319, 9320,
 9321, 9322, 9323, 9324, 9325, 9326, 9327, 9328, 9329, 9330, 9331, 9332, 9333,
 9334, 9335, 9336, 9337, 9338, 9339, 9340, 9341, 9342, 9343, 9344, 9345, 9346,
 9347, 9348, 9349, 9350, 9351, 9352, 9353, 9354
} ;


MLOCAL SW16 CG_to_MSL_index(UW16 cg_num)
{
  SW16 a = 0, b = lengthof(CG_nums) ;

  while (a < b) {
    SW16 i = (a+b)>>1 ;
    if (cg_num == CG_nums[i])
      return i ;
    if (cg_num > CG_nums[i])
      a = i + 1 ;
    else
      b = i ;
  }

  return -1;
}
#endif
/* -------------------------------------------------------------------------- */
/* UFST callbacks to find soft-font glyph definitions */

LPUB8 get_glyphId2Ptr(IF_STATE * ifstate, UW16 gid)
{
  UNUSED_PARAM(IF_STATE *, ifstate) ;

  if (get_id_ws && get_id_font) {
    user_glyph * glyph = finduserglyphbyid(get_id_font, gid) ;

    switch (get_id_font->broken) {
    case BROKEN_REVUE:
      if (gid == 142 || gid == 162 || gid == 216 || gid == 225 || gid == 228)
        glyph = 0 ;
      break ;
    case BROKEN_OLDENGLISH:
      if (gid == 162 || gid == 217)
        glyph = 0 ;
      break ;
    }

    if (glyph)
      return (LPUB8) &glyph->data[0] ;
  }
  return NULL ;
}

LPUB8 get_chId2Ptr(IF_STATE * ifstate, UW16 code)
{
  int space = (code == 32) ;
  UNUSED_PARAM(IF_STATE *, ifstate) ;

  if (get_id_ws && get_id_font) {
    user_glyph * glyph ;

#if SSMAPPING
    if (code == 65535 && get_id_font->GC_def_ch) {
      return get_glyphId2Ptr(ifstate, 0) ;

    } else {
      /* This is lifted from AGFA's example code. It's horrible. */
      int type = (get_id_font->type >= TYPE_TT) ? SYMBOLSET_TT : SYMBOLSET_IF ;

      if (!if_state.chr_def_hdr.is_compound &&
          FC_ISIF(&if_state.fcCur) &&
          if_state.fcCur.ssnum != DL_SYMSET &&
          if_state.PCL_font_type == PCL_BOUND_FONT &&
          (code < if_state.symbolset.ss_first_code[type] ||
           code > if_state.symbolset.ss_last_code[type]))
        return null_Id2Ptr() ;

      if (if_state.PCL_font_type == PCL_UNBOUND_FONT &&
          if_state.fcCur.ssnum != DL_SYMSET) {
        UW16 cgnum = SYMmap(FSA code, FC_IF_TYPE);
        if (cgnum != 0) {
          SW16 MSL_index = CG_to_MSL_index(cgnum);
          if (MSL_index != -1)
            code = MSL_nums[MSL_index];
          else
            code = 0;
          space = (code == 0) ;
        } else
          return null_Id2Ptr() ;
      }
    }
    /* end lift */
#endif

    glyph = finduserglyph(get_id_font, code) ;
    if (glyph)
      return (LPUB8) &glyph->data[0] ;
  }

  /* It turns out that the UFST null ptr misbehaviour only occurs for spaces */
  return (space && get_id_font && get_id_font->type < TYPE_TT) ?
         null_Id2Ptr() : NULL ;
}

/* ========================================================================== */
/* The contextualised memory allocation functions for UFST's extmem.c */

/* Allocator for UFST library */
void * UFSTalloc(IF_STATE * pIFS, size_t size)
{
  void * block = 0 ;
  pfin_ufst5_instance * ws ;

  if (pIFS != 0 &&
      (ws = (pfin_ufst5_instance *) pIFS->mem_context) != 0)
    block = myalloc(size) ;

  return block ;
}

/* Deallocator for UFST library */
void UFSTfree(IF_STATE * pIFS, void * block)
{
  pfin_ufst5_instance * ws ;

  if (pIFS != 0 &&
      (ws = (pfin_ufst5_instance *) pIFS->mem_context) != 0)
    myfree(block) ;

  return ;
}

/* ========================================================================== */
/* The PFIN registration call, for the UFST module. */

int RIPCALL pfin_ufst5_module( pfin_ufst5_fns * pFnTable )
{
  static pfin_ufst5_fns cgif = { NULL };
  static sw_pfin_api pfin_ufst5_api = {
    {
      SW_PFIN_API_VERSION_20090401,    /* we now require uid() and flush() */
      (uint8*)"UFST", /* The name by which the module will be configured */
      /* Caution! The following string is UTF8 encoded.

         Char       Unicode   UTF-8 form

         Copyright   0x00A9   C2 A9
         Registered  0x00AE   C2 AE
         Trademark   0x2122   E2 84 A2
      */
      (uint8*)"A PFIN font module using UFST v5\n"
              "\xC2\xA9 2007 - 2012 Global Graphics\xE2\x84\xA2\n"
              "Universal Font Scaling Technology (UFST\xC2\xAE) Version 5.0\n"
              "Copyright \xC2\xA9 1989-2006. All rights reserved, by "
              "Monotype Imaging Inc., Woburn, MA.",
      sizeof(pfin_ufst5_instance)
    },
    pfin_ufst5_init,
    pfin_ufst5_finish,
    pfin_ufst5_start,
    pfin_ufst5_stop,
    pfin_ufst5_config,
    pfin_ufst5_find,
    pfin_ufst5_metrics,
    pfin_ufst5_outline,
    pfin_ufst5_miscop,
    NULL                /* raster */
  };
  UFST_SHOW("pfin_ufst5_module\n");

  if (pFnTable) {
    /* The UFST CGIF function pointers would be better if registered as a static
       RDR, instead of having to be copied here. Rather than change the skin we
       make the parameter optional, and copy and register it here if not null.
    */
    cgif = *pFnTable;
    SwRegisterRDR(RDR_CLASS_API, RDR_API_CGIF, 0x10000, &cgif, sizeof(cgif), 0) ;

    /* Link our callbacks */
    (pFnTable->pfnUFSTSetCallbacks)( get_chId2Ptr, get_glyphId2Ptr
#if GG_CUSTOM
                                   , UFSTalloc, UFSTfree
#endif
                                   ) ;
  }

  return SwRegisterPFIN(&pfin_ufst5_api);
}

/* ========================================================================== */
/** \todo Direct calling into a PFIN module is NOT ALLOWED. These two functions to be called by miscops */

int pfin_ufst5_GetPCLFontTotal(void)
{
  int i, TotalFonts = 0;
  fco_font * font ;

  if (get_id_ws != NULL)
  {
    for (i = 0; i < get_id_ws->TotalFonts; i++) {
      font = &get_id_ws->Fonts[i];
      if (font->typeface != 0)
        TotalFonts++;
    }
  }
  return TotalFonts;
}

char *pfin_ufst5_GetPCLFontSpec(int index, int charper, int height, char *name)
{
  static char FontSpec[64];
  fco_font * font ;
  pfin_ufst5_instance *ws = get_id_ws;
  TTFONTINFOTYPE * fontInfo ;
  char symbolset[4][8] = {"8U", "19M","579L","14L"};
  int ssid = 0;
/*     for ( i = 0; i < l_nUFSTFonts; i++ ) { */
/*
  <esc>(SS<esc>(s0pWWhXsYbZZZZT	fixed pitch
SS    = symbolset
WW    = characters/inch
X     = style
Y     = Weight
ZZZZ  = Typeface
  <esc>(SS<esc>(s1pWWvXsYbZZZZT	proportional
SS    = symbolset
WW    = height in points
X     = style
Y     = Weight
ZZZZ  = Typeface
*/
  font = &ws->Fonts[index] ;
  if(font->typeface != 0)
  {
    fontInfo = font->pttFontInfo ;
    strcpy(name, PCLFONTNAME(index));
    if((font->typeface == 16686)||(font->typeface == 45358)) /* Symbol and SymbolPS or equivalent */
      ssid = 1;
    if(font->typeface == 31402)/* Wingdings or equivalent */
      ssid = 2;
    if(font->typeface == 45101)/* Zapf Dingbats or equivalent */
      ssid = 3;
    if(fontInfo->isFixedPitch)
      sprintf(FontSpec, "\x1b(%s\x1b(s0p%dh%ds%db%dT",
                  symbolset[ssid], charper, fontInfo->pcltStyle,
                      fontInfo->pcltStrokeWt, font->typeface);
    else
      sprintf(FontSpec, "\x1b(%s\x1b(s1p%dv%ds%db%dT",
                   symbolset[ssid], height, fontInfo->pcltStyle,
                      fontInfo->pcltStrokeWt, font->typeface);
  }
  else
  {
    FontSpec[0] = '\0';
  }
  return FontSpec;
}

/* -------------------------------------------------------------------------- */
/* List of symbolsets, their type and character requirements. */

/* These two static tables were automatically generated from
   the symbol set files distributed with UFST 5.x. */

static PCL_SSINFO sslist[] = { /* this must be in ascending order of SS */
  {SS(  0,'D'), USS('D','N'), 0, {0x80,0x00,0x00}, {0xC2,0x00,0x01}}, /* 0D - ISO 60: Danish/Norwegian from pcl6\mtdn.sym */
  {SS(  0,'I'), USS('I','T'), 0, {0x80,0x00,0x00}, {0xC0,0x00,0x01}}, /* 0I - ISO 15: Italian from pcl6\mtit.sym */
  {SS(  0,'N'), USS('E','1'), 1, {0x80,0x00,0x00}, {0xC4,0x00,0x01}}, /* 0N - ISO 8859/1 Latin 1 (EC94) from pcl6\mte1.sym */
  {SS(  0,'S'), USS('S','W'), 0, {0x80,0x00,0x00}, {0xC0,0x00,0x01}}, /* 0S - ISO 11: Swedish from pcl6\mtsw.sym */
  {SS(  0,'U'), USS('U','S'), 0, {0x80,0x00,0x00}, {0x80,0x00,0x01}}, /* 0U - ISO 6: ASCII from pcl6\mtus.sym */
  {SS(  1,'E'), USS('U','K'), 0, {0x80,0x00,0x00}, {0xC2,0x00,0x01}}, /* 1E - ISO 4: United Kingdom from pcl6\mtuk.sym */
  {SS(  1,'F'), USS('F','R'), 0, {0x80,0x00,0x00}, {0xC0,0x00,0x01}}, /* 1F - ISO 69: French from pcl6\mtfr.sym */
  {SS(  1,'G'), USS('G','R'), 0, {0x80,0x00,0x00}, {0xC0,0x00,0x01}}, /* 1G - ISO 21: German from pcl6\mtgr.sym */
  {SS(  1,'U'), USS('L','G'), 0, {0x80,0x00,0x00}, {0x82,0x00,0x01}}, /* 1U - Legal from pcl6\mtlg.sym */
  {SS(  2,'N'), USS('E','2'), 1, {0xC0,0x00,0x00}, {0xA0,0x00,0x01}}, /* 2N - ISO 8859/2 Latin 2 from pcl6\tde2.sym */
  {SS(  2,'S'), USS('S','P'), 0, {0x80,0x00,0x00}, {0xC0,0x00,0x01}}, /* 2S - ISO 17: Spanish from pcl6\mtsp.sym */
  {SS(  4,'U'), USS('R','9'), 1, {0x80,0x00,0x00}, {0xC2,0x00,0x01}}, /* 4U - Roman-9 from pcl6\mtr9.sym - MADE UP MSL &jwk */
  {SS(  5,'M'), USS('M','S'), 1, {0x80,0x04,0x00}, {0x80,0x08,0x01}}, /* 5M - PS Math from pcl6\mtms.sym */
  {SS(  5,'N'), USS('E','5'), 1, {0xA0,0x00,0x00}, {0x94,0x00,0x01}}, /* 5N - ISO 8859/9 Latin 5 from pcl6\tde5.sym */
  {SS(  5,'T'), USS('W','T'), 2, {0xA0,0x00,0x00}, {0x98,0x00,0x01}}, /* 5T - Windows 3.1 Latin 5 from pcl6\tdwt.sym */
  {SS(  6,'J'), USS('P','B'), 2, {0x80,0x06,0x00}, {0x00,0x04,0x01}}, /* 6J - Microsoft Publishing from pcl6\tdpb.sym */
  {SS(  6,'N'), USS('E','6'), 1, {0x10,0x00,0x00}, {0x80,0x10,0x01}}, /* 6N - ISO 8859/10 Latin 6 from pcl6\tde6.sym */
  {SS(  7,'J'), USS('D','T'), 1, {0x80,0x02,0x00}, {0xCE,0x00,0x01}}, /* 7J - DeskTop from pcl6\tddt.sym */
  {SS(  8,'M'), USS('M','8'), 1, {0x80,0x04,0x00}, {0x80,0x08,0x01}}, /* 8M - Math-8 from pcl6\tdm8.sym */
  {SS(  8,'U'), USS('R','8'), 1, {0x80,0x00,0x00}, {0xC2,0x00,0x01}}, /* 8U - Roman-8 from pcl6\mtr8.sym */
  {SS(  9,'E'), USS('W','E'), 2, {0xC0,0x00,0x00}, {0xA8,0x00,0x01}}, /* 9E - Windows 3.1 Latin 2 from pcl6\tdwe.sym */
  {SS(  9,'J'), USS('P','U'), 2, {0x80,0x00,0x00}, {0xCC,0x00,0x01}}, /* 9J - PC-1004 from pcl6\mtpu.sym */
  {SS(  9,'N'), USS('E','9'), 1, {0xA0,0x00,0x00}, {0xC4,0x00,0x01}}, /* 9N - ISO 8859/15 Latin 9 from pcl6\mte9.sym */
  {SS(  9,'T'), USS('P','T'), 2, {0xA0,0x02,0x00}, {0x90,0x40,0x01}}, /* 9T - PC-8 TK, Code Page 437T from pcl6\tdpt.sym */
  {SS(  9,'U'), USS('W','O'), 2, {0x80,0x00,0x00}, {0xC4,0x00,0x01}}, /* 9U - Windows 3.0 Latin 1 from pcl6\mtwo.sym */
  {SS( 10,'J'), USS('T','S'), 1, {0x80,0x00,0x00}, {0xCC,0x80,0x01}}, /* 10J - PS Text from pcl6\mtts.sym */
  {SS( 10,'U'), USS('P','C'), 2, {0x80,0x02,0x00}, {0xC0,0x40,0x01}}, /* 10U - PC-8, Code Page 437 from pcl6\mtpc.sym */
  {SS( 11,'U'), USS('P','D'), 2, {0x80,0x02,0x00}, {0xC0,0x40,0x01}}, /* 11U - PC-8 D/N, Code Page 437N from pcl6\mtpd.sym */
  {SS( 12,'J'), USS('M','C'), 2, {0x80,0x06,0x00}, {0xC9,0x00,0x01}}, /* 12J - Macintosh from pcl6\mtmc.sym */
  {SS( 12,'U'), USS('P','M'), 2, {0x80,0x02,0x00}, {0xC4,0x40,0x01}}, /* 12U - PC-850 Multilingual from pcl6\mtpm.sym */
  {SS( 13,'U'), USS('P','9'), 2, {0x80,0x00,0x00}, {0xC4,0x40,0x01}}, /* 13U - PC-858 Multilingual with Euro from pcl6\mtp9.sym - MADE UP MSL &jwk */
  {SS( 14,'L'), USS('L','$'), 2, {0x00,0x00,0x00}, {0x00,0x00,0x01}}, /* 14L - HP4000 ITC Zapf Dingbats from pcl6\mtl$.sym - MADE UP UNICODE &jwk */
  {SS( 15,'U'), USS('P','I'), 1, {0x80,0x04,0x00}, {0x80,0x08,0x01}}, /* 15U - Pi Font from pcl6\mtpi.sym */
  {SS( 17,'U'), USS('P','E'), 2, {0xC0,0x02,0x00}, {0xA0,0x40,0x01}}, /* 17U - PC-852 Latin 2 from pcl6\mtpe.sym */
  {SS( 19,'L'), USS('W','L'), 2, {0x10,0x00,0x00}, {0x88,0x10,0x01}}, /* 19L - Windows 3.1 Baltic (Latv,Lith) from pcl6\mtwl.sym */
  {SS( 19,'M'), USS('S','Y'), 1, {0x00,0x00,0x00}, {0x00,0x00,0x01}}, /* 19M - Symbol from pcl6\mtsy.sym - MADE UP UNICODE &jwk */
  {SS( 19,'U'), USS('W','1'), 2, {0x80,0x00,0x00}, {0xC8,0x00,0x01}}, /* 19U - Windows 3.1 Latin 1 from pcl6\mtw1.sym */
  {SS( 26,'U'), USS('P','V'), 2, {0x10,0x02,0x00}, {0x80,0x50,0x01}}, /* 26U - PC-775 Baltic from pcl6\mtpv.sym */
  {SS(579,'L'), USS('W','D'), 2, {0x00,0x00,0x00}, {0x00,0x00,0x01}}, /* 579L - Wingdings from pcl6\mtwd.sym - MADE UP UNICODE &jwk */
} ;

/* -------------------------------------------------------------------------- */
/* PCL XL standard font and style names are mapped to PCL 5 criteria */

/* A string containing all the style suffixes, overlaid and concatenated */
static char XL_stylenames[] =
  "CdBdIt"     /* 0 */
  "CdMdIt"     /* 6 */
  "XbKrsvHlb"  /* 12 */
  "0N2N5N1U8U" /* 21 */
  "10U11U12U"  /* 31 */
  "Antiqua"    /* 40 */
  "BkObDbOb"   /* 47 */
  "LtItDbIt"   /* 55 */
  "NrBdOb"     /* 65 */
  "NrOb"       /* 69 */
  "Rmn"        /* 73 */ /*=76*/
;

/* A list of symbolset numbers, so XL_STYLE struct can be smaller */
static uint16 XL_symbolsets[] = {
  0,           /* none specified */
  SS(0,'N'),   /* 0N */
  SS(2,'N'),   /* 2N */
  SS(5,'N'),   /* 5N */
  SS(1,'U'),   /* 1U */           /* we could add 6N and 9N to these tables */
  SS(8,'U'),   /* 8U */
  SS(10,'U'),  /* 10U */
  SS(11,'U'),  /* 11U */
  SS(12,'U')   /* 12U */ /*=18*/
};

/* This table is sorted alphabetically (shorter first) by suffix */
static XL_STYLE XL_styles[] = {
  /* length, offset, symbolset index, weight/style */
  {2, 21, 1, XL_MEDIUM},                   /* 0N */
  {2, 27, 4, XL_MEDIUM},                   /* 1U */
  {2, 23, 2, XL_MEDIUM},                   /* 2N */
  {2, 25, 3, XL_MEDIUM},                   /* 5N */
  {2, 29, 5, XL_MEDIUM},                   /* 8U */
  {2,  2, 0, XL_BOLD},                     /* Bd */
  {2, 47, 0, XL_MEDIUM},                   /* Bk */
  {2, 51, 0, XL_DEMI},                     /* Db */
  {2,  4, 0, XL_MEDIUM|XL_ITALIC},         /* It */
  {2, 55, 0, XL_LIGHT},                    /* Lt */
  {2,  8, 0, XL_MEDIUM},                   /* Md */
  {2, 65, 0, XL_MEDIUM|XL_COND},           /* Nr */
  {2, 49, 0, XL_MEDIUM|XL_ITALIC},         /* Ob */
  {2, 12, 0, XL_EXTRA},                    /* Xb */
  {3, 31, 6, XL_MEDIUM},                   /* 10U */
  {3, 34, 7, XL_MEDIUM},                   /* 11U */
  {3, 37, 8, XL_MEDIUM},                   /* 12U */
  {3, 18, 0, XL_BOLD},                     /* Hlb */
  {3, 73, 0, XL_MEDIUM},                   /* Rmn */
  {4,  2, 0, XL_BOLD|XL_ITALIC},           /* BdIt */
  {4, 67, 0, XL_BOLD|XL_ITALIC},           /* BdOb */
  {4, 47, 0, XL_MEDIUM|XL_ITALIC},         /* BkOb */
  {4,  0, 0, XL_BOLD|XL_COND},             /* CdBd */
  {4,  6, 0, XL_MEDIUM|XL_COND},           /* CdMd */
  {4, 59, 0, XL_DEMI|XL_ITALIC},           /* DbIt */
  {4, 51, 0, XL_DEMI|XL_ITALIC},           /* DbOb */
  {4, 14, 0, XL_MEDIUM|XL_ITALIC},         /* Krsv */
  {4, 69, 0, XL_MEDIUM|XL_COND|XL_ITALIC}, /* NrOb */
  {6,  0, 0, XL_BOLD|XL_COND|XL_ITALIC},   /* CdBdIt */
  {6,  6, 0, XL_MEDIUM|XL_COND|XL_ITALIC}, /* CdMdIt */
  {6, 65, 0, XL_BOLD|XL_COND|XL_ITALIC},   /* NrBdOb */
  {7, 40, 0, XL_MEDIUM},                   /* Antiqua */
  {7, 14, 0, XL_BOLD|XL_ITALIC}            /* KrsvHlb */ /*=128*/
};

/* String containing unterminated font names, concatenated */
static char XL_fontnames[] =
  "Albertus"      /* 0 */
  "AntiqOlive"    /* 8 */
  "Arial"         /* 18 */
  "CG Omega"      /* 23 */
  "CG Times"      /* 31 */
  "Clarendon"     /* 39 */
  "Coronet"       /* 48 */
  "CourierPS"     /* 55 */  /* and Courier */
  "Garamond"      /* 64 */
  "LetterGothic"  /* 72 */
  "Line Printer"  /* 84 */
  "Marigold"      /* 96 */
  "SymbolPS"      /* 104 */
  "TimesNewRmn"   /* 112 */  /* and Times */
  "Univers"       /* 123 */
  "Wingdings"     /* 130 */
  "ITCAvantGarde" /* 139 */  /* ITCAvantGard and AvantGarde */
  "ITCBookman"    /* 152 */  /* and Bookman */
  "NwCentSchlbk"  /* 162 */
  "Helvetica"     /* 174 */
  "Palatino"      /* 183 */
  "ZapfChancery"  /* 191 */  /* and Chancery */
  "ZapfDingbats"  /* 203 */  /*=215*/  /* and Dingbats */
;

/* This table is sorted alphabetically (shorter first) by font name */
static XL_FONT XL_fonts[] = {
  /* length, offset, font number */
  {         5,  18, 16602}, /* Arial */
  {         5, 112, 16901}, /* Times */
  {         6, 104, 16686}, /* Symbol */
  {         7, 155, 24623}, /* Bookman */
  {XL_ITAL| 7,  48,  4116}, /* Coronet */
  {XL_MONO| 7,  55,  4099}, /* Courier */
  {         7, 123,  4148}, /* Univers */
  {         8,   0,  4362}, /* Albertus */
  {XL_ITAL| 8, 195, 45099}, /* Chancery */
  {         8,  23,  4113}, /* CG Omega */
  {         8,  31,  4101}, /* CG Times */
  {         8, 207, 45101}, /* Dingbats */
  {         8,  64,  4197}, /* Garamond */
  {         8,  96,  4297}, /* Marigold */
  {         8, 183, 24591}, /* Palatino */
  {         8, 104, 16686}, /* SymbolPS */
  {XL_MONO| 9,  55,  4099}, /* CourierPS */
  {         9,  39,  4140}, /* Clarendon */
  {         9, 174, 24580}, /* Helvetica */
  {         9, 130, 31402}, /* WingDings */
  {        10,   8,  4168}, /* AntiqOlive */
  {        10, 142, 24607}, /* AvantGarde */
  {        10, 152, 24623}, /* ITCBookman */
  {        11, 112, 16901}, /* TimesNewRmn */
  {        12, 139, 24607}, /* ITCAvantGard */
  {XL_MONO|12,  72,  4102}, /* LetterGothic */
  {XL_MONO|12,  84,     0}, /* Line Printer */
  {        12, 162, 24703}, /* NwCentSchlbk */
  {XL_ITAL|12, 191, 45099}, /* ZapfChancery */
  {        12, 203, 45101}  /* ZapfDingbats */  /*=120*/
};

static float XL_HMIs[] = {0.5f} ;    /* For LinePrinter */

XL_DATA XL_data = {
  XL_fonts,      lengthof(XL_fonts),
  XL_fontnames,
  XL_styles,     lengthof(XL_styles),
  XL_symbolsets, lengthof(XL_symbolsets),
  XL_stylenames,
  XL_HMIs
};

/* -------------------------------------------------------------------------- */
/* The LinePrinter PCLEO font in a PCL stream */

static unsigned char lineprinter[] = {
  27,  41, 115,  49,  57,  57,  87,   0,  64,   0,  10,   0,   0,   0,  27,   0,
  18,   0,  38,   0,   0,   0,  56,   0,  72,   0, 141,   0,  68,   0,   0,   0,
   0,   0,   0,   0,   0, 250,   3,   0, 169,   0,  72,   0,  33,  36, 255,   0,
 171,   0,   0,   0,   0,   0,   0,  76, 105, 110, 101,  32,  80, 114, 105, 110,
 116, 101, 114,  32,  32,  32,  32,   0, 135,   1,  67, 111, 112, 121, 114, 105,
 103, 104, 116,  32,  40,  67,  41,  32,  72, 101, 119, 108, 101, 116, 116,  45,
  80,  97,  99, 107,  97, 114, 100,  44,  32,  49,  57,  56,  52,  44,  39,  56,
  53,  44,  39,  56,  54,  44,  39,  56,  55,  44,  39,  56,  56,  44,  39,  56,
  57,  46,  32,  65, 108, 108,  32, 114, 105, 103, 104, 116, 115,  32, 114, 101,
 115, 101, 114, 118, 101, 100,  46,  32,  65, 100, 100, 105, 116, 105, 111, 110,
  97, 108,  32, 103, 108, 121, 112, 104, 115,  32,  99, 111, 112, 121, 114, 105,
 103, 104, 116,  32,  40,  67,  41,  32,  71, 108, 111,  98,  97, 108,  32,  71,
 114,  97, 112, 104, 105,  99, 115,  44,  32,  50,  48,  48,  57,  46,  27,  42,
  99,  49,  69,  27,  40, 115,  51,  56,  87,   4,   0,  14,   1,   0,   0,   0,
   6,   0,  21,   0,   4,   0,  22,   0,  72, 240, 240, 240, 240, 240, 240, 240,
 240, 240, 240, 240, 240, 240, 240, 240, 240,   0,   0,   0, 240, 240, 240,  27,
  42,  99,  50,  69,  27,  40, 115,  52,  48,  87,   4,   0,  14,   1,   0,   0,
   0,   3,   0,  21,   0,  12,   0,  12,   0,  72, 240, 240, 240, 240, 240, 240,
 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240,
 240, 240,  27,  42,  99,  51,  69,  27,  40, 115,  56,  56,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  23,   0,  17,   0,  24,   0,  72,   3, 135,   0,
   3, 135,   0,   3, 135,   0,   3,   6,   0,   7,  14,   0,   7,  14,   0,   7,
  14,   0, 255, 255, 128, 255, 255, 128, 255, 255, 128,  14,  28,   0,  12,  24,
   0,  28,  56,   0,  28,  56,   0,  24,  48,   0, 255, 255,   0, 255, 255,   0,
 255, 255,   0,  56, 112,   0,  56, 112,   0,  48,  96,   0, 112, 224,   0, 112,
 224,   0, 112, 224,   0,  27,  42,  99,  52,  69,  27,  40, 115,  54,  52,  87,
   4,   0,  14,   1,   0,   0,   0,   1,   0,  23,   0,  15,   0,  24,   0,  72,
   3, 128,   3, 128,  31, 240, 127, 252, 115, 156, 227, 142, 227, 142, 227, 128,
 227, 128, 243, 128, 127, 128,  63, 224,  15, 248,   3, 252,   3, 158,   3, 142,
   3, 142, 227, 142, 227, 142, 115, 156, 127, 252,  31, 240,   3, 128,   3, 128,
  27,  42,  99,  53,  69,  27,  40, 115,  56,  56,  87,   4,   0,  14,   1,   0,
   0,   0,   1,   0,  23,   0,  17,   0,  24,   0,  72,  60,   3,   0, 126,   7,
   0, 231,   6,   0, 195,  14,   0, 195,  12,   0, 195,  28,   0, 231,  56,   0,
 126,  48,   0,  60, 112,   0,   0,  96,   0,   0, 224,   0,   1, 192,   0,   1,
 128,   0,   3, 128,   0,   3,   0,   0,   7,  30,   0,   6,  63,   0,  14, 115,
 128,  28,  97, 128,  24,  97, 128,  56,  97, 128,  48, 115, 128, 112,  63,   0,
  96,  30,   0,  27,  42,  99,  54,  69,  27,  40, 115,  54,  52,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  23,   0,  16,   0,  24,   0,  72,   7, 128,
  31, 224,  28, 224,  56, 112,  56, 112,  56, 112,  56, 112,  56, 224,  28, 224,
  29, 192,  15, 128,  31,   0,  63,   0, 119, 128, 115, 199, 225, 199, 224, 238,
 224, 254, 224, 124, 224, 120, 240, 248, 121, 252, 127, 222,  31,  15,  27,  42,
  99,  56,  69,  27,  40, 115,  50,  56,  87,   4,   0,  14,   1,   0,   0,   0,
   5,   0,  21,   0,   7,   0,  12,   0,  72,  30,  30,  60,  60,  60,  56, 120,
 120, 112, 112, 240, 224,  27,  42,  99,  57,  69,  27,  40, 115,  54,  54,  87,
   4,   0,  14,   1,   0,   0,   0,   4,   0,  21,   0,   9,   0,  25,   0,  72,
   3, 128,   7, 128,  15, 128,  31,   0,  62,   0,  60,   0, 120,   0, 120,   0,
 112,   0, 240,   0, 240,   0, 224,   0, 224,   0, 224,   0, 240,   0, 240,   0,
 112,   0, 120,   0, 120,   0,  60,   0,  62,   0,  31,   0,  15, 128,   7, 128,
   3, 128,  27,  42,  99,  49,  48,  69,  27,  40, 115,  54,  54,  87,   4,   0,
  14,   1,   0,   0,   0,   5,   0,  21,   0,   9,   0,  25,   0,  72, 224,   0,
 240,   0, 248,   0, 124,   0,  62,   0,  30,   0,  15,   0,  15,   0,   7,   0,
   7, 128,   7, 128,   3, 128,   3, 128,   3, 128,   7, 128,   7, 128,   7,   0,
  15,   0,  15,   0,  30,   0,  62,   0, 124,   0, 248,   0, 240,   0, 224,   0,
  27,  42,  99,  49,  49,  69,  27,  40, 115,  52,  52,  87,   4,   0,  14,   1,
   0,   0,   0,   1,   0,  23,   0,  15,   0,  14,   0,  72,   3, 128,   3, 128,
   3, 128,  67, 132, 115, 156, 251, 190, 255, 254,  31, 240,   7, 192,  15, 224,
  30, 240,  60, 120,  60, 120,  24,  48,  27,  42,  99,  49,  50,  69,  27,  40,
 115,  52,  54,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  18,   0,  15,
   0,  15,   0,  72,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,
 255, 254, 255, 254, 255, 254,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,
   3, 128,  27,  42,  99,  49,  51,  69,  27,  40, 115,  50,  54,  87,   4,   0,
  14,   1,   0,   0,   0,   5,   0,   3,   0,   7,   0,  10,   0,  72,  62,  62,
  60, 124, 124, 120, 120, 248, 240, 240,  27,  42,  99,  49,  52,  69,  27,  40,
 115,  50,  50,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  12,   0,   9,
   0,   3,   0,  72, 255, 128, 255, 128, 255, 128,  27,  42,  99,  49,  53,  69,
  27,  40, 115,  50,  49,  87,   4,   0,  14,   1,   0,   0,   0,   6,   0,   4,
   0,   6,   0,   5,   0,  72, 252, 252, 252, 252, 252,  27,  42,  99,  49,  54,
  69,  27,  40, 115,  54,  54,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,
  21,   0,  15,   0,  25,   0,  72,   0,  30,   0,  28,   0,  60,   0,  56,   0,
 120,   0, 112,   0, 240,   0, 224,   0, 224,   1, 224,   1, 192,   3, 192,   3,
 128,   7, 128,   7,   0,  15,   0,  14,   0,  14,   0,  30,   0,  28,   0,  60,
   0,  56,   0, 120,   0, 112,   0, 240,   0,  27,  42,  99,  49,  55,  69,  27,
  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,
  14,   0,  24,   0,  72,   7, 128,  31, 224,  63, 240, 120, 120, 112,  56, 224,
  28, 224,  28, 224,  28, 224,  28, 224,  28, 224,  28, 224,  28, 224,  28, 224,
  28, 224,  28, 224,  28, 224,  28, 224,  28, 224,  28, 112,  56, 120, 120,  63,
 240,  31, 224,   7, 128,  27,  42,  99,  49,  56,  69,  27,  40, 115,  52,  48,
  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  23,   0,   8,   0,  24,   0,
  72,  15,  31,  63, 127, 247, 231, 199, 135,   7,   7,   7,   7,   7,   7,   7,
   7,   7,   7,   7,   7,   7,   7,   7,   7,  27,  42,  99,  49,  57,  69,  27,
  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,
  14,   0,  24,   0,  72,  31, 192,  63, 240, 127, 248, 240, 120, 224,  60,   0,
  28,   0,  28,   0,  28,   0,  60,   0,  56,   0, 248,   3, 240,   7, 224,  15,
 128,  30,   0,  60,   0, 120,   0, 112,   0, 240,   0, 224,   0, 224,   0, 255,
 252, 255, 252, 255, 252,  27,  42,  99,  50,  48,  69,  27,  40, 115,  54,  52,
  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  23,   0,  15,   0,  24,   0,
  72, 255, 252, 255, 252, 255, 252,   0,  60,   0, 120,   0, 240,   1, 224,   3,
 192,   7, 128,  15, 192,  15, 240,  15, 248,   0, 124,   0,  28,   0,  30,   0,
  14,   0,  14,   0,  14,   0,  30,   0,  28, 224, 124, 255, 248, 255, 240,  63,
 192,  27,  42,  99,  50,  49,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  23,   0,  15,   0,  24,   0,  72,   1, 192,   1,
 192,   3, 128,   3, 128,   7,   0,   7,   0,  14,   0,  14,   0,  28,   0,  28,
   0,  56,   0,  56, 112, 112, 112, 112, 112, 224, 112, 224, 112, 255, 254, 255,
 254, 255, 254,   0, 112,   0, 112,   0, 112,   0, 112,   0, 112,  27,  42,  99,
  50,  50,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,
   2,   0,  23,   0,  13,   0,  24,   0,  72, 255, 240, 255, 240, 255, 240, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 255,   0, 255, 192, 255, 240, 129,
 240,   0, 120,   0, 120,   0,  56,   0,  56,   0,  56,   0, 120,   0, 112,   0,
 240,   3, 224, 255, 224, 255, 128, 252,   0,  27,  42,  99,  50,  51,  69,  27,
  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  23,   0,
  16,   0,  24,   0,  72,   0, 120,   0, 240,   1, 224,   3, 192,   7, 128,  15,
   0,  15,   0,  30,   0,  63, 224,  63, 248, 127, 252, 124,  62, 112,  14, 240,
  15, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 112,  14, 124,  62,  63,
 252,  31, 248,   7, 224,  27,  42,  99,  50,  52,  69,  27,  40, 115,  54,  52,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  16,   0,  24,   0,
  72, 255, 255, 255, 255, 255, 255,   0,  15,   0,  30,   0,  60,   0, 120,   0,
 240,   0, 224,   1, 192,   3, 192,   3, 128,   7, 128,   7,   0,  15,   0,  14,
   0,  14,   0,  30,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  27,  42,  99,  50,  53,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  23,   0,  16,   0,  24,   0,  72,   7, 224,  31,
 248,  63, 252,  60,  60, 120,  30, 112,  14, 112,  14,  56,  28,  60,  60,  30,
 120,  15, 240,  31, 248,  62, 124, 120,  30, 112,  14, 224,   7, 224,   7, 224,
   7, 224,   7, 240,  15, 120,  30, 127, 254,  63, 252,  15, 240,  27,  42,  99,
  50,  54,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,
   1,   0,  23,   0,  16,   0,  24,   0,  72,   7, 224,  31, 248,  63, 252, 124,
  62, 112,  14, 240,  15, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 112,
  14, 124,  62,  63, 254,  31, 252,   7, 252,   0, 120,   0, 240,   1, 224,   1,
 224,   3, 192,   7, 128,  15,   0,  30,   0,  27,  42,  99,  50,  55,  69,  27,
  40, 115,  51,  50,  87,   4,   0,  14,   1,   0,   0,   0,   6,   0,  15,   0,
   6,   0,  16,   0,  72, 252, 252, 252, 252, 252,   0,   0,   0,   0,   0,   0,
 252, 252, 252, 252, 252,  27,  42,  99,  50,  56,  69,  27,  40, 115,  51,  56,
  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  15,   0,   8,   0,  22,   0,
  72,  63,  63,  63,  63,  63,   0,   0,   0,   0,   0,   0,   0,  62,  62,  60,
 124, 124, 120, 120, 248, 240, 240,  27,  42,  99,  50,  57,  69,  27,  40, 115,
  52,  56,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  18,   0,  14,   0,
  16,   0,  72,   0,   4,   0,  28,   0, 124,   1, 248,   7, 224,  31, 128, 126,
   0, 248,   0, 124,   0,  31,   0,  15, 192,   3, 224,   1, 248,   0, 124,   0,
  28,   0,   4,  27,  42,  99,  51,  48,  69,  27,  40, 115,  51,  54,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  15,   0,  14,   0,  10,   0,  72, 255,
 252, 255, 252, 255, 252,   0,   0,   0,   0,   0,   0,   0,   0, 255, 252, 255,
 252, 255, 252,  27,  42,  99,  51,  49,  69,  27,  40, 115,  52,  56,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  18,   0,  14,   0,  16,   0,  72, 128,
   0, 224,   0, 248,   0, 126,   0,  31, 128,   7, 224,   1, 248,   0, 124,   0,
 248,   3, 224,  15, 192,  31,   0, 126,   0, 248,   0, 224,   0, 128,   0,  27,
  42,  99,  51,  50,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  21,   0,  13,   0,  22,   0,  72,  31, 128, 127, 224, 127,
 240, 240, 240, 224, 120, 224, 120,   0, 120,   0, 112,   0, 112,   0, 240,   1,
 224,   3, 192,   7, 128,   7,   0,  15,   0,  15,   0,   0,   0,   0,   0,   0,
   0,  15,   0,  15,   0,  15,   0,  27,  42,  99,  51,  51,  69,  27,  40, 115,
  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  22,   0,  16,   0,
  23,   0,  72,   3, 224,  31, 248,  63, 252, 124,  60, 112,  14, 224,  14, 224,
   7,   0,   7,   0,   7,   0,   7,  30, 199, 127, 199, 127, 199, 241, 199, 225,
 199, 225, 199, 225, 199, 225, 199, 225, 199, 225, 199, 123, 254, 127, 254,  30,
  60,  27,  42,  99,  51,  52,  69,  27,  40, 115,  56,  50,  87,   4,   0,  14,
   1,   0,   0,   0,   0,   0,  21,   0,  17,   0,  22,   0,  72,   3, 224,   0,
   3, 224,   0,   3, 224,   0,   7, 240,   0,   7, 112,   0,   7, 112,   0,  15,
 120,   0,  14,  56,   0,  14,  56,   0,  14,  56,   0,  30,  60,   0,  28,  28,
   0,  28,  28,   0,  31, 252,   0,  63, 254,   0,  63, 254,   0,  56,  14,   0,
  56,  14,   0, 120,  15,   0, 112,   7,   0, 112,   7,   0, 240,   7, 128,  27,
  42,  99,  51,  53,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  21,   0,  14,   0,  22,   0,  72, 255, 192, 255, 240, 255,
 240, 224, 120, 224,  56, 224,  56, 224,  56, 224,  56, 224, 112, 255, 240, 255,
 224, 255, 240, 224, 120, 224,  56, 224,  28, 224,  28, 224,  28, 224,  60, 224,
 120, 255, 248, 255, 240, 255, 192,  27,  42,  99,  51,  54,  69,  27,  40, 115,
  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  14,   0,
  22,   0,  72,  15, 224,  63, 248, 127, 248, 120,  60, 112,  28, 240,  28, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 240,  28, 112,  28, 120,  60, 127, 248,  63, 248,  15, 224,  27,
  42,  99,  51,  55,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   1,   0,  21,   0,  14,   0,  22,   0,  72, 255,   0, 255, 192, 255,
 224, 225, 240, 224, 120, 224,  56, 224,  60, 224,  28, 224,  28, 224,  28, 224,
  28, 224,  28, 224,  28, 224,  28, 224,  28, 224,  60, 224,  56, 224, 120, 225,
 240, 255, 224, 255, 192, 255,   0,  27,  42,  99,  51,  56,  69,  27,  40, 115,
  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  13,   0,
  22,   0,  72, 255, 248, 255, 248, 255, 248, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 255, 240, 255, 240, 255, 240, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 255, 248, 255, 248, 255, 248,  27,
  42,  99,  51,  57,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   4,   0,  21,   0,  11,   0,  22,   0,  72, 255, 224, 255, 224, 255,
 224, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255, 192, 255,
 192, 255, 192, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0,  27,  42,  99,  52,  48,  69,  27,  40, 115,
  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  14,   0,
  22,   0,  72,   7, 192,  31, 240,  63, 248, 120,  56, 112,  28, 240,  28, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224, 252, 224, 252, 224, 252, 224,
  28, 224,  28, 224,  28, 240,  28, 120,  28, 127, 252,  63, 252,  15, 252,  27,
  42,  99,  52,  49,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   3,   0,  21,   0,  12,   0,  22,   0,  72, 224, 112, 224, 112, 224,
 112, 224, 112, 224, 112, 224, 112, 224, 112, 224, 112, 224, 112, 255, 240, 255,
 240, 255, 240, 224, 112, 224, 112, 224, 112, 224, 112, 224, 112, 224, 112, 224,
 112, 224, 112, 224, 112, 224, 112,  27,  42,  99,  52,  50,  69,  27,  40, 115,
  51,  56,  87,   4,   0,  14,   1,   0,   0,   0,   5,   0,  21,   0,   7,   0,
  22,   0,  72, 254, 254, 254,  56,  56,  56,  56,  56,  56,  56,  56,  56,  56,
  56,  56,  56,  56,  56,  56, 254, 254, 254,  27,  42,  99,  52,  51,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  21,   0,
  11,   0,  22,   0,  72,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224, 224, 224, 224, 224, 224, 224, 224, 224, 241, 224, 255, 224, 127, 192,  63,
 128,  27,  42,  99,  52,  52,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  21,   0,  16,   0,  22,   0,  72, 224,  30, 224,
  60, 224, 120, 224, 240, 225, 224, 227, 192, 231, 128, 239,   0, 254,   0, 252,
   0, 248,   0, 252,   0, 254,   0, 239,   0, 231, 128, 227, 192, 225, 224, 224,
 240, 224, 120, 224,  60, 224,  30, 224,  15,  27,  42,  99,  52,  53,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  21,   0,
  12,   0,  22,   0,  72, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255, 240, 255, 240, 255,
 240,  27,  42,  99,  52,  54,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  21,   0,  15,   0,  22,   0,  72, 240,  30, 240,
  30, 248,  62, 248,  62, 248,  62, 252, 126, 252, 126, 236, 110, 238, 238, 238,
 238, 231, 206, 231, 206, 231, 206, 227, 142, 227, 142, 227, 142, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14,  27,  42,  99,  52,  55,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,
  13,   0,  22,   0,  72, 224,  56, 224,  56, 240,  56, 240,  56, 248,  56, 248,
  56, 252,  56, 252,  56, 254,  56, 238,  56, 239,  56, 231,  56, 231, 184, 227,
 184, 227, 248, 225, 248, 225, 248, 224, 248, 224, 248, 224, 120, 224, 120, 224,
  56,  27,  42,  99,  52,  56,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  21,   0,  16,   0,  22,   0,  72,   7, 224,  31,
 248,  63, 252, 124,  62, 112,  14, 240,  15, 224,   7, 224,   7, 224,   7, 224,
   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 112,
  14, 124,  62,  63, 252,  31, 248,   7, 224,  27,  42,  99,  52,  57,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,
  14,   0,  22,   0,  72, 255, 128, 255, 224, 255, 240, 224, 120, 224,  56, 224,
  28, 224,  28, 224,  28, 224,  28, 224,  56, 224, 120, 255, 240, 255, 224, 255,
 128, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0,  27,  42,  99,  53,  48,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  21,   0,  15,   0,  22,   0,  72,   7, 192,  31,
 240,  63, 248, 124, 124, 112,  28, 240,  30, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 227,  14, 227, 142, 241, 222, 112, 252, 124,
 124,  63, 248,  31, 252,   7, 206,   0,   6,  27,  42,  99,  53,  49,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,
  14,   0,  22,   0,  72, 255, 128, 255, 224, 255, 240, 224, 240, 224, 120, 224,
  56, 224,  56, 224,  56, 224, 120, 224, 240, 255, 240, 255, 192, 255,   0, 231,
 128, 227, 128, 227, 192, 225, 224, 224, 224, 224, 240, 224, 120, 224,  56, 224,
  60,  27,  42,  99,  53,  50,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  21,   0,  14,   0,  22,   0,  72,  15, 192,  63,
 240, 127, 248, 112,  56, 224,  28, 224,  28, 224,   0, 240,   0, 124,   0,  63,
   0,  15, 192,   3, 240,   0, 248,   0,  60,   0,  28,   0,  28, 224,  28, 224,
  28, 112,  56, 127, 248,  63, 240,  15, 192,  27,  42,  99,  53,  51,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,
  13,   0,  22,   0,  72, 255, 248, 255, 248, 255, 248,   7,   0,   7,   0,   7,
   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,
   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,
   0,  27,  42,  99,  53,  52,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  21,   0,  15,   0,  22,   0,  72, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 240,
  30, 120,  60, 127, 252,  63, 248,  15, 224,  27,  42,  99,  53,  53,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  21,   0,
  16,   0,  22,   0,  72, 224,   7, 224,   7, 240,  15, 112,  14, 112,  14, 112,
  14,  56,  28,  56,  28,  56,  28,  60,  60,  28,  56,  28,  56,  28,  56,  14,
 112,  14, 112,  14, 112,  15, 240,   7, 224,   7, 224,   7, 224,   3, 192,   3,
 192,  27,  42,  99,  53,  54,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  21,   0,  16,   0,  22,   0,  72, 224,   7, 224,
   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,
  15, 112,  14, 115, 206, 115, 206, 115, 206, 119, 238, 119, 238, 118, 110, 126,
 126,  62, 124,  60,  60,  60,  60,  60,  60,  27,  42,  99,  53,  55,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  21,   0,
  15,   0,  22,   0,  72, 224,  14, 240,  30, 112,  28, 120,  60,  56,  56,  60,
 120,  28, 112,  30, 240,  14, 224,  15, 224,   7, 192,   7, 192,  15, 224,  14,
 224,  30, 240,  28, 112,  60, 120,  56,  56, 120,  60, 112,  28, 240,  30, 224,
  14,  27,  42,  99,  53,  56,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  21,   0,  15,   0,  22,   0,  72, 240,  30, 112,
  28, 120,  60,  56,  56,  60, 120,  28, 112,  30, 240,  14, 224,  15, 224,   7,
 192,   7, 192,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3,
 128,   3, 128,   3, 128,   3, 128,   3, 128,  27,  42,  99,  53,  57,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,
  14,   0,  22,   0,  72, 255, 248, 255, 248, 255, 248,   0,  56,   0, 120,   0,
 112,   0, 240,   1, 224,   1, 192,   3, 192,   7, 128,   7,   0,  15,   0,  30,
   0,  28,   0,  60,   0, 120,   0, 112,   0, 240,   0, 255, 252, 255, 252, 255,
 252,  27,  42,  99,  54,  48,  69,  27,  40, 115,  52,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   8,   0,  21,   0,   8,   0,  24,   0,  72, 255, 255, 255,
 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,
 224, 224, 255, 255, 255,  27,  42,  99,  54,  49,  69,  27,  40, 115,  54,  54,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  15,   0,  25,   0,
  72, 240,   0, 112,   0, 120,   0,  56,   0,  60,   0,  28,   0,  30,   0,  14,
   0,  14,   0,  15,   0,   7,   0,   7, 128,   3, 128,   3, 192,   1, 192,   1,
 224,   0, 224,   0, 224,   0, 240,   0, 112,   0, 120,   0,  56,   0,  60,   0,
  28,   0,  30,  27,  42,  99,  54,  50,  69,  27,  40, 115,  52,  48,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,   8,   0,  24,   0,  72, 255,
 255, 255,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,
   7,   7,   7,   7, 255, 255, 255,  27,  42,  99,  54,  51,  69,  27,  40, 115,
  51,  54,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  14,   0,
  10,   0,  72,   3,   0,   7, 128,   7, 128,  15, 192,  28, 224,  24,  96,  48,
  48,  96,  24,  64,   8, 128,   4,  27,  42,  99,  54,  52,  69,  27,  40, 115,
  50,  53,  87,   4,   0,  14,   1,   0,   0,   0,   0, 255, 250,   0,  18,   0,
   3,   0,  72, 255, 255, 192, 255, 255, 192, 255, 255, 192,  27,  42,  99,  54,
  54,  69,  27,  40, 115,  50,  56,  87,   4,   0,  14,   1,   0,   0,   0,   6,
   0,  21,   0,   7,   0,  12,   0,  72, 240, 240, 120, 120, 120,  56,  60,  60,
  28,  28,  30,  14,  27,  42,  99,  54,  55,  69,  27,  40, 115,  53,  48,  87,
   4,   0,  14,   1,   0,   0,   0,   2,   0,  16,   0,  14,   0,  17,   0,  72,
  15, 224,  63, 248,  63, 248, 120,  60, 112,  28,   0,  28,   0,  28,  15, 252,
  63, 252, 124,  28, 240,  28, 224,  28, 224,  60, 240, 252, 127, 252, 127, 220,
  31,  28,  27,  42,  99,  54,  56,  69,  27,  40, 115,  54,  52,  87,   4,   0,
  14,   1,   0,   0,   0,   2,   0,  23,   0,  16,   0,  24,   0,  72, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 227, 224, 239, 248,
 255, 252, 252,  62, 240,  14, 240,  15, 224,   7, 224,   7, 224,   7, 224,   7,
 224,   7, 240,  15, 240,  14, 252,  62, 255, 252, 239, 248, 227, 224,  27,  42,
  99,  54,  57,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  16,   0,  14,   0,  17,   0,  72,   7, 192,  31, 240,  63, 248,
 120,  60, 112,  28, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 112,  28, 120,  60,  63, 248,  31, 240,   7, 192,  27,  42,  99,  55,
  48,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   1,
   0,  23,   0,  16,   0,  24,   0,  72,   0,   7,   0,   7,   0,   7,   0,   7,
   0,   7,   0,   7,   0,   7,   7, 199,  31, 247,  63, 255, 124,  63, 112,  15,
 240,  15, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 112,  15,
 124,  63,  63, 255,  31, 247,   7, 199,  27,  42,  99,  55,  49,  69,  27,  40,
 115,  53,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  16,   0,  15,
   0,  17,   0,  72,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,  14,
 224,  14, 255, 254, 255, 254, 224,   0, 224,   0, 224,   0, 112,   0, 120,  28,
  63, 252,  31, 248,   7, 224,  27,  42,  99,  55,  50,  69,  27,  40, 115,  54,
  52,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  23,   0,  12,   0,  24,
   0,  72,   0, 240,   3, 240,   7, 240,   7,   0,  14,   0,  14,   0,  14,   0,
  14,   0, 255, 240, 255, 240, 255, 240,  14,   0,  14,   0,  14,   0,  14,   0,
  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,
  14,   0,  27,  42,  99,  55,  51,  69,  27,  40, 115,  54,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   2,   0,  16,   0,  14,   0,  22,   0,  72,  15, 156,
  63, 252, 127, 252, 120, 124, 240,  60, 224,  28, 224,  28, 224,  28, 224,  28,
 224,  28, 240,  60, 120, 124, 127, 252,  63, 252,  15, 156,   0,  28,   0,  28,
 224,  60, 240, 120, 127, 248,  63, 240,  15, 192,  27,  42,  99,  55,  52,  69,
  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,
   0,  13,   0,  24,   0,  72, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 231, 192, 255, 240, 255, 240, 248, 248, 240, 120, 224,  56,
 224,  56, 224,  56, 224,  56, 224,  56, 224,  56, 224,  56, 224,  56, 224,  56,
 224,  56, 224,  56, 224,  56,  27,  42,  99,  55,  53,  69,  27,  40, 115,  52,
  48,  87,   4,   0,  14,   1,   0,   0,   0,   5,   0,  23,   0,   7,   0,  24,
   0,  72,  62,  62,  62,   0,   0,   0,   0, 252, 252, 252,  28,  28,  28,  28,
  28,  28,  28,  28,  28,  28,  28,  28,  28,  28,  27,  42,  99,  55,  54,  69,
  27,  40, 115,  55,  52,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  23,
   0,   9,   0,  29,   0,  72,  15, 128,  15, 128,  15, 128,   0,   0,   0,   0,
   0,   0,   0,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,
   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,
   7,   0,   7,   0,   7,   0,   7,   0,  15,   0, 254,   0, 254,   0, 248,   0,
  27,  42,  99,  55,  55,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,
   0,   0,   0,   3,   0,  23,   0,  14,   0,  24,   0,  72, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224, 120, 224, 240, 225, 224,
 227, 192, 231, 128, 239,   0, 254,   0, 252,   0, 252,   0, 254,   0, 239,   0,
 231, 128, 227, 192, 225, 224, 224, 240, 224, 120, 224,  60,  27,  42,  99,  55,
  56,  69,  27,  40, 115,  52,  48,  87,   4,   0,  14,   1,   0,   0,   0,   6,
   0,  23,   0,   7,   0,  24,   0,  72, 224, 224, 224, 224, 224, 224, 224, 224,
 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 240, 126, 126,  30,
  27,  42,  99,  55,  57,  69,  27,  40, 115,  54,  55,  87,   4,   0,  14,   1,
   0,   0,   0,   1,   0,  16,   0,  17,   0,  17,   0,  72, 223,  62,   0, 255,
 255,   0, 243, 231, 128, 225, 195, 128, 225, 195, 128, 225, 195, 128, 225, 195,
 128, 225, 195, 128, 225, 195, 128, 225, 195, 128, 225, 195, 128, 225, 195, 128,
 225, 195, 128, 225, 195, 128, 225, 195, 128, 225, 195, 128, 225, 195, 128,  27,
  42,  99,  56,  48,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  16,   0,  15,   0,  17,   0,  72, 227, 224, 239, 248, 255,
 252, 252,  60, 240,  30, 240,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,  27,  42,  99,
  56,  49,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,   0,   0,   0,
   2,   0,  16,   0,  15,   0,  17,   0,  72,   7, 192,  31, 240,  63, 248, 120,
  60, 112,  28, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 112,  28, 120,  60,  63, 248,  31, 240,   7, 192,  27,  42,  99,  56,  50,
  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  16,   0,  16,   0,  24,   0,  72, 227, 224, 239, 248, 255, 252, 252,  62, 240,
  14, 240,  15, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 240,
  14, 252,  62, 255, 252, 239, 248, 227, 224, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0,  27,  42,  99,  56,  51,  69,  27,  40, 115,
  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  16,   0,  16,   0,
  24,   0,  72,   7, 199,  31, 247,  63, 255, 124,  63, 112,  15, 240,  15, 224,
   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 112,  15, 124,  63,  63,
 255,  31, 247,   7, 199,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,
   7,   0,   7,  27,  42,  99,  56,  52,  69,  27,  40, 115,  53,  48,  87,   4,
   0,  14,   1,   0,   0,   0,   4,   0,  16,   0,  12,   0,  17,   0,  72, 231,
 128, 255, 224, 253, 240, 240, 112, 224, 112, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0,  27,  42,  99,  56,  53,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  16,   0,  14,   0,  17,   0,  72,  15, 192,  63,
 240, 127, 248, 240,  60, 224,  28, 224,   0, 248,   0, 127, 128,  63, 240,   7,
 248,   0, 124,   0,  28, 224,  28, 240,  60, 127, 248,  63, 240,  15, 192,  27,
  42,  99,  56,  54,  69,  27,  40, 115,  53,  56,  87,   4,   0,  14,   1,   0,
   0,   0,   3,   0,  20,   0,  12,   0,  21,   0,  72,  12,   0,  28,   0,  28,
   0,  28,   0, 255, 240, 255, 240, 255, 240,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  30,   0,  15,
 240,  15, 240,   3, 224,  27,  42,  99,  56,  55,  69,  27,  40, 115,  53,  48,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  16,   0,  15,   0,  17,   0,
  72, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  30, 240,  30, 120, 126, 127, 254,  63,
 238,  15, 142,  27,  42,  99,  56,  56,  69,  27,  40, 115,  54,  55,  87,   4,
   0,  14,   1,   0,   0,   0,   1,   0,  16,   0,  17,   0,  17,   0,  72, 224,
   3, 128, 112,   7,   0, 112,   7,   0, 120,  15,   0,  56,  14,   0,  60,  30,
   0,  28,  28,   0,  30,  60,   0,  14,  56,   0,  14,  56,   0,   7, 112,   0,
   7, 112,   0,   7, 112,   0,   3, 224,   0,   3, 224,   0,   1, 192,   0,   1,
 192,   0,  27,  42,  99,  56,  57,  69,  27,  40, 115,  53,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   2,   0,  16,   0,  15,   0,  17,   0,  72, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 227, 142, 227, 142, 247, 222,
 119, 220, 118, 220, 126, 252, 126, 252, 124, 124, 124, 124, 120,  60, 120,  60,
  27,  42,  99,  57,  48,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   1,   0,  16,   0,  16,   0,  17,   0,  72, 120,  30,  56,  28,
  28,  56,  30, 120,  14, 112,   7, 224,   7, 224,   3, 192,   7, 224,   7, 224,
  14, 112,  30, 120,  28,  56,  60,  28, 120,  30, 112,  14, 240,  15,  27,  42,
  99,  57,  49,  69,  27,  40, 115,  56,  53,  87,   4,   0,  14,   1,   0,   0,
   0,   1,   0,  16,   0,  17,   0,  23,   0,  72, 224,   3, 128, 240,   7, 128,
 112,   7,   0, 120,  15,   0,  56,  14,   0,  60,  30,   0,  28,  28,   0,  30,
  60,   0,  14,  56,   0,  15, 120,   0,   7, 112,   0,   7, 240,   0,   3, 224,
   0,   3, 224,   0,   1, 192,   0,   3, 192,   0,   3, 128,   0,   7, 128,   0,
   7,   0,   0,  15,   0,   0,  14,   0,   0,  30,   0,   0,  28,   0,   0,  27,
  42,  99,  57,  50,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   3,   0,  16,   0,  13,   0,  17,   0,  72, 255, 240, 255, 240, 255,
 240,   0, 240,   1, 224,   3, 192,   3, 128,   7, 128,  15,   0,  30,   0,  28,
   0,  60,   0, 120,   0, 240,   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,
  57,  51,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,
   2,   0,  21,   0,  14,   0,  24,   0,  72,   1, 252,   3, 252,   7, 128,   7,
   0,   7,   0,  15,   0,  15,   0,  15,   0,  30,   0,  30,   0,  60,   0, 248,
   0, 248,   0,  60,   0,  30,   0,  30,   0,  15,   0,  15,   0,  15,   0,  15,
   0,   7,   0,   7, 128,   3, 252,   1, 252,  27,  42,  99,  57,  52,  69,  27,
  40, 115,  53,  52,  87,   4,   0,  14,   1,   0,   0,   0,   8,   0,  27,   0,
   3,   0,  38,   0,  72, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,
 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,
 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,  27,  42,  99,  57,  53,
  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  21,   0,  14,   0,  24,   0,  72, 254,   0, 255,   0,   7, 128,   3, 128,   3,
 128,   3, 192,   3, 192,   3, 192,   1, 224,   1, 224,   0, 240,   0, 124,   0,
 124,   0, 240,   1, 224,   1, 224,   3, 192,   3, 192,   3, 192,   3, 192,   3,
 128,   7, 128, 255,   0, 254,   0,  27,  42,  99,  57,  54,  69,  27,  40, 115,
  50,  54,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  12,   0,  16,   0,
   5,   0,  72,  30,   6,  63, 143, 127, 254, 241, 252,  96, 120,  27,  42,  99,
  57,  55,  69,  27,  40, 115,  49,  51,  48,  87,   4,   0,  14,   1,   0,   0,
   0,   0,   0,  27,   0,  18,   0,  38,   0,  72, 227, 142,   0, 227, 142,   0,
 227, 142,   0,  28, 113, 192,  28, 113, 192,  28, 113, 192, 227, 142,   0, 227,
 142,   0, 227, 142,   0,  28, 113, 192,  28, 113, 192,  28, 113, 192, 227, 142,
   0, 227, 142,   0, 227, 142,   0,  28, 113, 192,  28, 113, 192,  28, 113, 192,
  28, 113, 192, 227, 142,   0, 227, 142,   0, 227, 142,   0,  28, 113, 192,  28,
 113, 192,  28, 113, 192, 227, 142,   0, 227, 142,   0, 227, 142,   0,  28, 113,
 192,  28, 113, 192,  28, 113, 192, 227, 142,   0, 227, 142,   0, 227, 142,   0,
  28, 113, 192,  28, 113, 192,  28, 113, 192,  28, 113, 192,  27,  42,  99,  57,
  57,  69,  27,  40, 115,  49,  48,  48,  87,   4,   0,  14,   1,   0,   0,   0,
   0,   0,  27,   0,  17,   0,  28,   0,  72,   7,   0,   0,   7, 128,   0,   1,
 192,   0,   0,  96,   0,   0,   0,   0,   0,   0,   0,   3, 224,   0,   3, 224,
   0,   3, 224,   0,   7, 240,   0,   7, 112,   0,   7, 112,   0,  15, 120,   0,
  14,  56,   0,  14,  56,   0,  14,  56,   0,  30,  60,   0,  28,  28,   0,  28,
  28,   0,  31, 252,   0,  63, 254,   0,  63, 254,   0,  56,  14,   0,  56,  14,
   0, 120,  15,   0, 112,   7,   0, 112,   7,   0, 240,   7, 128,  27,  42,  99,
  49,  48,  48,  69,  27,  40, 115,  49,  48,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   0,   0,  27,   0,  17,   0,  28,   0,  72,   1, 192,   0,   3, 224,
   0,   7, 112,   0,  14,  56,   0,   0,   0,   0,   0,   0,   0,   3, 224,   0,
   3, 224,   0,   3, 224,   0,   7, 240,   0,   7, 112,   0,   7, 112,   0,  15,
 120,   0,  14,  56,   0,  14,  56,   0,  14,  56,   0,  30,  60,   0,  28,  28,
   0,  28,  28,   0,  31, 252,   0,  63, 254,   0,  63, 254,   0,  56,  14,   0,
  56,  14,   0, 120,  15,   0, 112,   7,   0, 112,   7,   0, 240,   7, 128,  27,
  42,  99,  49,  48,  49,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,
   0,   0,   0,   3,   0,  27,   0,  13,   0,  28,   0,  72,  56,   0,  60,   0,
  14,   0,   3,   0,   0,   0,   0,   0, 255, 248, 255, 248, 255, 248, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255, 240, 255, 240, 255, 240,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255, 248,
 255, 248, 255, 248,  27,  42,  99,  49,  48,  50,  69,  27,  40, 115,  55,  50,
  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  27,   0,  13,   0,  28,   0,
  72,   7,   0,  15, 128,  29, 192,  56, 224,   0,   0,   0,   0, 255, 248, 255,
 248, 255, 248, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255,
 240, 255, 240, 255, 240, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,  49,  48,  51,  69,
  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  26,
   0,  13,   0,  27,   0,  72,  56, 224,  56, 224,  56, 224,   0,   0,   0,   0,
 255, 248, 255, 248, 255, 248, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 255, 240, 255, 240, 255, 240, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,  49,
  48,  52,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,
   5,   0,  27,   0,   9,   0,  28,   0,  72,  28,   0,  62,   0, 119,   0, 227,
 128,   0,   0,   0,   0, 127,   0, 127,   0, 127,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0, 127,   0, 127,   0, 127,
   0,  27,  42,  99,  49,  48,  53,  69,  27,  40, 115,  55,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   5,   0,  26,   0,   9,   0,  27,   0,  72, 227, 128,
 227, 128, 227, 128,   0,   0,   0,   0, 127,   0, 127,   0, 127,   0,  28,   0,
  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,
  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0, 127,   0,
 127,   0, 127,   0,  27,  42,  99,  49,  48,  54,  69,  27,  40, 115,  50,  49,
  87,   4,   0,  14,   1,   0,   0,   0,   8,   0,  24,   0,   7,   0,   5,   0,
  72,  14,  30,  60, 112, 192,  27,  42,  99,  49,  48,  55,  69,  27,  40, 115,
  50,  49,  87,   4,   0,  14,   1,   0,   0,   0,   5,   0,  24,   0,   7,   0,
   5,   0,  72, 224, 240, 120,  28,   6,  27,  42,  99,  49,  48,  56,  69,  27,
  40, 115,  50,  52,  87,   4,   0,  14,   1,   0,   0,   0,   5,   0,  24,   0,
   9,   0,   4,   0,  72,  28,   0,  62,   0, 119,   0, 227, 128,  27,  42,  99,
  49,  48,  57,  69,  27,  40, 115,  50,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   5,   0,  23,   0,  10,   0,   3,   0,  72, 225, 192, 225, 192, 225, 192,
  27,  42,  99,  49,  49,  48,  69,  27,  40, 115,  50,  52,  87,   4,   0,  14,
   1,   0,   0,   0,   3,   0,  24,   0,  12,   0,   4,   0,  72,  56,  48, 126,
 112, 231, 224, 193, 192,  27,  42,  99,  49,  49,  49,  69,  27,  40, 115,  55,
  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  27,   0,  15,   0,  28,
   0,  72,  28,   0,  30,   0,   7,   0,   1, 128,   0,   0,   0,   0, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 240,  30, 120,  60, 127, 252,  63, 248,  15, 224,  27,  42,  99,  49,  49,  50,
  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  27,   0,  15,   0,  28,   0,  72,   3, 128,   7, 192,  14, 224,  28, 112,   0,
   0,   0,   0, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 240,  30, 120,  60, 127, 252,  63, 248,  15, 224,  27,
  42,  99,  49,  49,  51,  69,  27,  40, 115,  50,  53,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  25,   0,  18,   0,   3,   0,  72, 255, 255, 192, 255,
 255, 192, 255, 255, 192,  27,  42,  99,  49,  49,  52,  69,  27,  40, 115,  55,
  50,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  27,   0,  15,   0,  28,
   0,  72,   0, 112,   0, 240,   1, 192,   3,   0,   0,   0,   0,   0, 240,  30,
 112,  28, 120,  60,  56,  56,  60, 120,  28, 112,  30, 240,  14, 224,  15, 224,
   7, 192,   7, 192,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,
   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,  27,  42,  99,  49,  49,  53,
  69,  27,  40, 115,  49,  48,  54,  87,   4,   0,  14,   1,   0,   0,   0,   1,
   0,  23,   0,  17,   0,  30,   0,  72,   0,  56,   0,   0, 120,   0,   0, 240,
   0,   1, 192,   0,   3,   0,   0,   0,   0,   0,   0,   0,   0, 224,   3, 128,
 240,   7, 128, 112,   7,   0, 120,  15,   0,  56,  14,   0,  60,  30,   0,  28,
  28,   0,  30,  60,   0,  14,  56,   0,  15, 120,   0,   7, 112,   0,   7, 240,
   0,   3, 224,   0,   3, 224,   0,   1, 192,   0,   3, 192,   0,   3, 128,   0,
   7, 128,   0,   7,   0,   0,  15,   0,   0,  14,   0,   0,  30,   0,   0,  28,
   0,   0,  27,  42,  99,  49,  49,  54,  69,  27,  40, 115,  51,  54,  87,   4,
   0,  14,   1,   0,   0,   0,   4,   0,  23,   0,  10,   0,  10,   0,  72,  63,
   0, 127, 128, 225, 192, 192, 192, 192, 192, 192, 192, 192, 192, 225, 192, 127,
 128,  63,   0,  27,  42,  99,  49,  49,  55,  69,  27,  40, 115,  55,  50,  87,
   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  14,   0,  28,   0,  72,
  15, 224,  63, 248, 127, 248, 120,  60, 112,  28, 240,  28, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 240,  28, 112,  28, 120,  60, 127, 248,  63, 248,  15, 224,   3,   0,   1, 128,
   0, 192,  12, 192,  12, 192,   7, 128,  27,  42,  99,  49,  49,  56,  69,  27,
  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  16,   0,
  14,   0,  23,   0,  72,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 112,  28, 120,
  60,  63, 248,  31, 240,   7, 192,   1, 128,   0, 192,   0,  96,   6,  96,   6,
  96,   3, 192,  27,  42,  99,  49,  49,  57,  69,  27,  40, 115,  55,  50,  87,
   4,   0,  14,   1,   0,   0,   0,   3,   0,  27,   0,  13,   0,  28,   0,  72,
  28,  24,  63,  56, 115, 240,  96, 224,   0,   0,   0,   0, 224,  56, 224,  56,
 240,  56, 240,  56, 248,  56, 248,  56, 252,  56, 252,  56, 254,  56, 238,  56,
 239,  56, 231,  56, 231, 184, 227, 184, 227, 248, 225, 248, 225, 248, 224, 248,
 224, 248, 224, 120, 224, 120, 224,  56,  27,  42,  99,  49,  50,  48,  69,  27,
  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,
  15,   0,  24,   0,  72,  14,  12,  31, 156,  57, 248,  48, 112,   0,   0,   0,
   0,   0,   0, 227, 224, 239, 248, 255, 252, 252,  60, 240,  30, 240,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14,  27,  42,  99,  49,  50,  49,  69,  27,  40, 115,  51,
  56,  87,   4,   0,  14,   1,   0,   0,   0,   7,   0,  16,   0,   4,   0,  22,
   0,  72, 240, 240, 240,   0,   0,   0, 240, 240, 240, 240, 240, 240, 240, 240,
 240, 240, 240, 240, 240, 240, 240, 240,  27,  42,  99,  49,  50,  50,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  16,   0,
  13,   0,  22,   0,  72,   7, 128,   7, 128,   7, 128,   0,   0,   0,   0,   0,
   0,   7, 128,   7, 128,   7,   0,  15,   0,  30,   0,  60,   0, 120,   0, 112,
   0, 112,   0, 240,   0, 240,  56, 240,  56, 120, 120, 127, 240,  63, 240,  15,
 192,  27,  42,  99,  49,  50,  51,  69,  27,  40, 115,  54,  49,  87,   4,   0,
  14,   1,   0,   0,   0,   0,   0,  16,   0,  18,   0,  15,   0,  72, 195, 240,
 192, 239, 253, 192, 126,  31, 128,  56,   7,   0,  24,   6,   0,  48,   3,   0,
  48,   3,   0,  48,   3,   0,  48,   3,   0,  48,   3,   0,  24,   6,   0,  56,
   7,   0, 126,  31, 128, 239, 253, 192, 195, 240, 192,  27,  42,  99,  49,  50,
  52,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,
   0,  23,   0,  15,   0,  24,   0,  72,   1, 224,   7, 248,  15, 252,  30,  28,
  28,  14,  60,  14,  56,   0,  56,   0,  56,   0,  56,   0,  56,   0, 255, 240,
 255, 240, 255, 240,  56,   0,  56,   0,  56,   0,  56,   0,  56,   0,  56,   0,
  56,   0, 127, 254, 255, 254, 255, 254,  27,  42,  99,  49,  50,  53,  69,  27,
  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,
  15,   0,  24,   0,  72, 240,  30, 112,  28, 120,  60,  56,  56,  60, 120,  28,
 112,  30, 240,  30, 224, 255, 254, 255, 254, 255, 254,   3, 128,   3, 128,   3,
 128, 255, 254, 255, 254, 255, 254,   3, 128,   3, 128,   3, 128,   3, 128,   3,
 128,   3, 128,   3, 128,  27,  42,  99,  49,  50,  54,  69,  27,  40, 115,  55,
  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  23,   0,  12,   0,  27,
   0,  72,  15, 128,  63, 224,  56, 224, 112, 112, 112, 112, 112,   0, 120,   0,
  62,   0,  31, 128,  63, 192, 123, 224, 112, 224, 224, 112, 224, 112, 224, 112,
 112, 224, 125, 224,  63, 192,  31, 128,   7, 192,   1, 224,   0, 224, 224, 224,
 224, 224, 113, 192, 127, 192,  31,   0,  27,  42,  99,  49,  50,  55,  69,  27,
  40, 115,  55,  54,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,
  15,   0,  30,   0,  72,   0,  14,   0,  62,   0, 126,   0, 240,   0, 224,   1,
 192,   1, 192,   1, 192,  31, 252,  31, 252,  31, 252,   3, 128,   3, 128,   3,
 128,   3, 128,   7,   0,   7,   0,   7,   0,   7,   0,  15,   0,  14,   0,  14,
   0,  14,   0,  28,   0,  28,   0,  56,   0, 120,   0, 240,   0, 224,   0, 192,
   0,  27,  42,  99,  49,  50,  56,  69,  27,  40, 115,  53,  54,  87,   4,   0,
  14,   1,   0,   0,   0,   2,   0,  19,   0,  14,   0,  20,   0,  72,   3, 128,
   3, 128,   7, 192,  31, 240,  63, 248, 123, 188, 115, 156, 227, 128, 227, 128,
 227, 128, 227, 128, 227, 128, 227, 128, 115, 156, 123, 188,  63, 248,  31, 240,
   7, 192,   3, 128,   3, 128,  27,  42,  99,  49,  50,  57,  69,  27,  40, 115,
  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  14,   0,
  24,   0,  72,   3, 128,   7, 192,  14, 224,  28, 112,   0,   0,   0,   0,   0,
   0,  15, 224,  63, 248,  63, 248, 120,  60, 112,  28,   0,  28,   0,  28,  15,
 252,  63, 252, 124,  28, 240,  28, 224,  28, 224,  60, 240, 252, 127, 252, 127,
 220,  31,  28,  27,  42,  99,  49,  51,  48,  69,  27,  40, 115,  54,  52,  87,
   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,   0,  72,
   3, 128,   7, 192,  14, 224,  28, 112,   0,   0,   0,   0,   0,   0,   7, 192,
  31, 240,  63, 248, 120,  60, 112,  28, 224,  14, 224,  14, 255, 254, 255, 254,
 224,   0, 224,   0, 224,   0, 112,   0, 120,  28,  63, 252,  31, 248,   7, 224,
  27,  42,  99,  49,  51,  49,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,   0,  72,   3, 128,   7,
 192,  14, 224,  28, 112,   0,   0,   0,   0,   0,   0,   7, 192,  31, 240,  63,
 248, 120,  60, 112,  28, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 112,  28, 120,  60,  63, 248,  31, 240,   7, 192,  27,  42,  99,
  49,  51,  50,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  23,   0,  15,   0,  24,   0,  72,   3, 128,   7, 192,  14, 224,
  28, 112,   0,   0,   0,   0,   0,   0, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  30,
 240,  30, 120, 126, 127, 254,  63, 238,  15, 142,  27,  42,  99,  49,  51,  51,
  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  23,   0,  14,   0,  24,   0,  72,   0, 112,   0, 240,   1, 224,   3, 128,   6,
   0,   0,   0,   0,   0,  15, 224,  63, 248,  63, 248, 120,  60, 112,  28,   0,
  28,   0,  28,  15, 252,  63, 252, 124,  28, 240,  28, 224,  28, 224,  60, 240,
 252, 127, 252, 127, 220,  31,  28,  27,  42,  99,  49,  51,  52,  69,  27,  40,
 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,
   0,  24,   0,  72,   0, 112,   0, 240,   1, 224,   3, 128,   6,   0,   0,   0,
   0,   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,  14, 224,  14,
 255, 254, 255, 254, 224,   0, 224,   0, 224,   0, 112,   0, 120,  28,  63, 252,
  31, 248,   7, 224,  27,  42,  99,  49,  51,  53,  69,  27,  40, 115,  54,  52,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,   0,
  72,   0, 112,   0, 240,   1, 224,   3, 128,   6,   0,   0,   0,   0,   0,   7,
 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 112,  28, 120,  60,  63, 248,  31, 240,   7,
 192,  27,  42,  99,  49,  51,  54,  69,  27,  40, 115,  54,  52,  87,   4,   0,
  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,   0,  72,   0,  56,
   0, 120,   0, 240,   1, 192,   3,   0,   0,   0,   0,   0, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  30, 240,  30, 120, 126, 127, 254,  63, 238,  15, 142,  27,  42,
  99,  49,  51,  55,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  23,   0,  14,   0,  24,   0,  72,  28,   0,  30,   0,  15,
   0,   3, 128,   0, 192,   0,   0,   0,   0,  15, 224,  63, 248,  63, 248, 120,
  60, 112,  28,   0,  28,   0,  28,  15, 252,  63, 252, 124,  28, 240,  28, 224,
  28, 224,  60, 240, 124, 127, 252, 127, 220,  31,  28,  27,  42,  99,  49,  51,
  56,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,
   0,  23,   0,  15,   0,  24,   0,  72,  28,   0,  30,   0,  15,   0,   3, 128,
   0, 192,   0,   0,   0,   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28,
 224,  14, 224,  14, 255, 254, 255, 254, 224,   0, 224,   0, 224,   0, 112,   0,
 120,  28,  63, 252,  31, 248,   7, 224,  27,  42,  99,  49,  51,  57,  69,  27,
  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,
  15,   0,  24,   0,  72, 112,   0, 120,   0,  60,   0,  14,   0,   3,   0,   0,
   0,   0,   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 112,  28, 120,  60,  63,
 248,  31, 240,   7, 192,  27,  42,  99,  49,  52,  48,  69,  27,  40, 115,  54,
  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,
   0,  72,  56,   0,  60,   0,  30,   0,   7,   0,   1, 128,   0,   0,   0,   0,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  30, 240,  30, 120, 126, 127, 254,  63, 238,
  15, 142,  27,  42,  99,  49,  52,  49,  69,  27,  40, 115,  54,  50,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  22,   0,  14,   0,  23,   0,  72,  28,
  56,  28,  56,  28,  56,   0,   0,   0,   0,   0,   0,  15, 224,  63, 248,  63,
 248, 120,  60, 112,  28,   0,  28,   0,  28,  15, 252,  63, 252, 124,  28, 240,
  28, 224,  28, 224,  60, 240, 252, 127, 252, 127, 220,  31,  28,  27,  42,  99,
  49,  52,  50,  69,  27,  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  22,   0,  15,   0,  23,   0,  72,  28,  56,  28,  56,  28,  56,
   0,   0,   0,   0,   0,   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28,
 224,  14, 224,  14, 255, 254, 255, 254, 224,   0, 224,   0, 224,   0, 112,   0,
 120,  28,  63, 252,  31, 248,   7, 224,  27,  42,  99,  49,  52,  51,  69,  27,
  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  22,   0,
  15,   0,  23,   0,  72,  56, 112,  56, 112,  56, 112,   0,   0,   0,   0,   0,
   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 112,  28, 120,  60,  63, 248,  31,
 240,   7, 192,  27,  42,  99,  49,  52,  52,  69,  27,  40, 115,  54,  50,  87,
   4,   0,  14,   1,   0,   0,   0,   2,   0,  22,   0,  15,   0,  23,   0,  72,
  28,  56,  28,  56,  28,  56,   0,   0,   0,   0,   0,   0, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  30, 240,  30, 120, 126, 127, 254,  63, 238,  15, 142,  27,  42,
  99,  49,  52,  53,  69,  27,  40, 115,  49,  48,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  27,   0,  17,   0,  28,   0,  72,   1, 192,   0,   3,
  96,   0,   6,  48,   0,   4,  16,   0,   6,  48,   0,   3,  96,   0,   1, 192,
   0,   3, 224,   0,   3, 224,   0,   7, 240,   0,   7, 112,   0,   7, 112,   0,
  15, 120,   0,  14,  56,   0,  14,  56,   0,  14,  56,   0,  30,  60,   0,  28,
  28,   0,  28,  28,   0,  31, 252,   0,  63, 254,   0,  63, 254,   0,  56,  14,
   0,  56,  14,   0, 120,  15,   0, 112,   7,   0, 112,   7,   0, 240,   7, 128,
  27,  42,  99,  49,  52,  54,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,
   1,   0,   0,   0,   5,   0,  23,   0,   9,   0,  24,   0,  72,  28,   0,  62,
   0, 119,   0, 227, 128,   0,   0,   0,   0,   0,   0, 126,   0, 126,   0, 126,
   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,
   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  27,  42,  99,
  49,  52,  55,  69,  27,  40, 115,  54,  56,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  23,   0,  15,   0,  26,   0,  72,   0, 112,   0, 112,   7, 224,
  31, 240,  63, 248, 124, 252, 112, 220, 241, 222, 225, 206, 225, 206, 225, 206,
 227, 142, 227, 142, 227, 142, 227, 142, 231,  14, 231,  14, 231,  14, 247,  30,
 118,  28, 126, 124,  63, 248,  31, 240,  15, 192,  28,   0,  28,   0,  27,  42,
  99,  49,  52,  56,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   1,   0,  21,   0,  15,   0,  22,   0,  72,  15, 254,  15, 254,  15,
 254,  29, 192,  29, 192,  29, 192,  29, 192,  25, 192,  57, 192,  57, 252,  57,
 252,  57, 252,  49, 192, 113, 192, 127, 192, 127, 192, 127, 192,  97, 192, 225,
 192, 225, 254, 225, 254, 225, 254,  27,  42,  99,  49,  52,  57,  69,  27,  40,
 115,  54,  56,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  25,   0,  14,
   0,  26,   0,  72,   3, 128,   6, 192,  12,  96,   8,  32,  12,  96,   6, 192,
   3, 128,   0,   0,   0,   0,  15, 224,  63, 248,  63, 248, 120,  60, 112,  28,
   0,  28,   0,  28,  15, 252,  63, 252, 124,  28, 240,  28, 224,  28, 224,  60,
 240, 252, 127, 252, 127, 220,  31,  28,  27,  42,  99,  49,  53,  48,  69,  27,
  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   5,   0,  23,   0,
   9,   0,  24,   0,  72,   3, 128,   7, 128,  15,   0,  28,   0,  48,   0,   0,
   0,   0,   0, 252,   0, 252,   0, 252,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  27,  42,  99,  49,  53,  49,  69,  27,  40, 115,  53,
  56,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  18,   0,  16,   0,  21,
   0,  72,   0,  28,   0,  28,   7, 248,  31, 248,  63, 252, 120, 126, 112, 110,
 224, 231, 224, 199, 225, 199, 225, 135, 227, 135, 227,   7, 231,   7, 118,  14,
 126,  30,  63, 252,  31, 248,  31, 224,  56,   0,  56,   0,  27,  42,  99,  49,
  53,  50,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,   0,   0,   0,
   2,   0,  16,   0,  15,   0,  17,   0,  72,  60, 120, 126, 252, 119, 222,  99,
 142,   3, 142,   3, 142,   3, 142,  63, 254, 127, 254, 243, 128, 227, 128, 227,
 128, 227, 142, 227, 142, 247, 222, 126, 252,  60, 120,  27,  42,  99,  49,  53,
  51,  69,  27,  40, 115,  57,  55,  87,   4,   0,  14,   1,   0,   0,   0,   0,
   0,  26,   0,  17,   0,  27,   0,  72,  28,  56,   0,  28,  56,   0,  28,  56,
   0,   0,   0,   0,   0,   0,   0,   3, 224,   0,   3, 224,   0,   3, 224,   0,
   7, 240,   0,   7, 112,   0,   7, 112,   0,  15, 120,   0,  14,  56,   0,  14,
  56,   0,  14,  56,   0,  30,  60,   0,  28,  28,   0,  28,  28,   0,  31, 252,
   0,  63, 254,   0,  63, 254,   0,  56,  14,   0,  56,  14,   0, 120,  15,   0,
 112,   7,   0, 112,   7,   0, 240,   7, 128,  27,  42,  99,  49,  53,  52,  69,
  27,  40, 115,  52,  48,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  23,
   0,   8,   0,  24,   0,  72, 224, 240, 120,  28,   6,   0,   0,  63,  63,  63,
   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,   7,  27,  42,
  99,  49,  53,  53,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   1,   0,  26,   0,  16,   0,  27,   0,  72,  56,  28,  56,  28,  56,
  28,   0,   0,   0,   0,   7, 224,  31, 248,  63, 252, 124,  62, 112,  14, 240,
  15, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,
   7, 224,   7, 224,   7, 240,  15, 112,  14, 124,  62,  63, 252,  31, 248,   7,
 224,  27,  42,  99,  49,  53,  54,  69,  27,  40, 115,  55,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   2,   0,  26,   0,  15,   0,  27,   0,  72,  56,  56,
  56,  56,  56,  56,   0,   0,   0,   0, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 240,  30, 120,  60, 127, 252,
  63, 248,  15, 224,  27,  42,  99,  49,  53,  55,  69,  27,  40, 115,  55,  50,
  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  27,   0,  13,   0,  28,   0,
  72,   1, 192,   3, 192,   7,   0,  12,   0,   0,   0,   0,   0, 255, 248, 255,
 248, 255, 248, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255,
 240, 255, 240, 255, 240, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,  49,  53,  56,  69,
  27,  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,   5,   0,  22,
   0,   9,   0,  23,   0,  72, 227, 128, 227, 128, 227, 128,   0,   0,   0,   0,
   0,   0, 252,   0, 252,   0, 252,   0,  28,   0,  28,   0,  28,   0,  28,   0,
  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,
  28,   0,  28,   0,  27,  42,  99,  49,  53,  57,  69,  27,  40, 115,  54,  48,
  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  21,   0,  13,   0,  22,   0,
  72,  31,   0, 127, 192, 127, 192, 241, 224, 224, 224, 224,  96, 224,  96, 224,
  96, 224, 192, 225, 192, 231, 128, 231, 224, 224, 240, 224, 112, 224,  56, 224,
  56, 224,  56, 224,  56, 224, 112, 224, 240, 231, 224, 231, 128,  27,  42,  99,
  49,  54,  48,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   1,   0,  27,   0,  16,   0,  28,   0,  72,   1, 192,   3, 224,   7, 112,
  14,  56,   0,   0,   0,   0,   7, 224,  31, 248,  63, 252, 124,  62, 112,  14,
 240,  15, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7,
 224,   7, 224,   7, 224,   7, 240,  15, 112,  14, 124,  62,  63, 252,  31, 248,
   7, 224,  27,  42,  99,  49,  54,  49,  69,  27,  40, 115,  49,  48,  48,  87,
   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,  17,   0,  28,   0,  72,
   0, 112,   0,   0, 240,   0,   1, 192,   0,   3,   0,   0,   0,   0,   0,   0,
   0,   0,   3, 224,   0,   3, 224,   0,   3, 224,   0,   7, 240,   0,   7, 112,
   0,   7, 112,   0,  15, 120,   0,  14,  56,   0,  14,  56,   0,  14,  56,   0,
  30,  60,   0,  28,  28,   0,  28,  28,   0,  31, 252,   0,  63, 254,   0,  63,
 254,   0,  56,  14,   0,  56,  14,   0, 120,  15,   0, 112,   7,   0, 112,   7,
   0, 240,   7, 128,  27,  42,  99,  49,  54,  50,  69,  27,  40, 115,  49,  48,
  48,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,  17,   0,  28,
   0,  72,   7,   6,   0,  15, 206,   0,  28, 252,   0,  24,  56,   0,   0,   0,
   0,   0,   0,   0,   3, 224,   0,   3, 224,   0,   3, 224,   0,   7, 240,   0,
   7, 112,   0,   7, 112,   0,  15, 120,   0,  14,  56,   0,  14,  56,   0,  14,
  56,   0,  30,  60,   0,  28,  28,   0,  28,  28,   0,  31, 252,   0,  63, 254,
   0,  63, 254,   0,  56,  14,   0,  56,  14,   0, 120,  15,   0, 112,   7,   0,
 112,   7,   0, 240,   7, 128,  27,  42,  99,  49,  54,  51,  69,  27,  40, 115,
  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  14,   0,
  24,   0,  72,  28,  24,  63,  56, 115, 240,  96, 224,   0,   0,   0,   0,   0,
   0,  15, 224,  63, 248,  63, 248, 120,  60, 112,  28,   0,  28,   0,  28,  15,
 252,  63, 252, 124,  28, 240,  28, 224,  28, 224,  60, 240, 252, 127, 252, 127,
 220,  31,  28,  27,  42,  99,  49,  54,  52,  69,  27,  40, 115,  54,  48,  87,
   4,   0,  14,   1,   0,   0,   0,   0,   0,  21,   0,  16,   0,  22,   0,  72,
  63, 192,  63, 240,  63, 248,  56, 124,  56,  30,  56,  14,  56,  15,  56,   7,
  56,   7,  56,   7, 255,   7, 255,   7,  56,   7,  56,   7,  56,   7,  56,  15,
  56,  14,  56,  30,  56, 124,  63, 248,  63, 240,  63, 192,  27,  42,  99,  49,
  54,  53,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,
   2,   0,  23,   0,  15,   0,  24,   0,  72,  28,  12,  31,  60,  15, 240,   3,
 192,  15, 224,  60, 240,  48, 112,   7, 184,  31, 248,  63, 252, 120,  60, 112,
  30, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 112,
  28, 120,  60,  63, 248,  31, 240,   7, 192,  27,  42,  99,  49,  54,  54,  69,
  27,  40, 115,  52,  52,  87,   4,   0,  14,   1,   0,   0,   0,   6,   0,  27,
   0,   7,   0,  28,   0,  72,  14,  30,  56,  96,   0,   0, 254, 254, 254,  56,
  56,  56,  56,  56,  56,  56,  56,  56,  56,  56,  56,  56,  56,  56,  56, 254,
 254, 254,  27,  42,  99,  49,  54,  55,  69,  27,  40, 115,  52,  52,  87,   4,
   0,  14,   1,   0,   0,   0,   6,   0,  27,   0,   7,   0,  28,   0,  72, 224,
 240,  56,  12,   0,   0, 254, 254, 254,  56,  56,  56,  56,  56,  56,  56,  56,
  56,  56,  56,  56,  56,  56,  56,  56, 254, 254, 254,  27,  42,  99,  49,  54,
  56,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   1,
   0,  27,   0,  16,   0,  28,   0,  72,   0, 224,   1, 224,   3, 128,   6,   0,
   0,   0,   0,   0,   7, 224,  31, 248,  63, 252, 124,  62, 112,  14, 240,  15,
 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7,
 224,   7, 224,   7, 240,  15, 112,  14, 124,  62,  63, 252,  31, 248,   7, 224,
  27,  42,  99,  49,  54,  57,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  27,   0,  16,   0,  28,   0,  72,  28,   0,  30,
   0,   7,   0,   1, 128,   0,   0,   0,   0,   7, 224,  31, 248,  63, 252, 124,
  62, 112,  14, 240,  15, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,
   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 112,  14, 124,  62,  63,
 252,  31, 248,   7, 224,  27,  42,  99,  49,  55,  48,  69,  27,  40, 115,  55,
  50,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  27,   0,  16,   0,  28,
   0,  72,  14,  12,  31, 156,  57, 248,  48, 112,   0,   0,   0,   0,   7, 224,
  31, 248,  63, 252, 124,  62, 112,  14, 240,  15, 224,   7, 224,   7, 224,   7,
 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15,
 112,  14, 124,  62,  63, 252,  31, 248,   7, 224,  27,  42,  99,  49,  55,  49,
  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  23,   0,  15,   0,  24,   0,  72,  28,  24,  63,  56, 115, 240,  96, 224,   0,
   0,   0,   0,   0,   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 112,  28, 120,
  60,  63, 248,  31, 240,   7, 192,  27,  42,  99,  49,  55,  50,  69,  27,  40,
 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  26,   0,  14,
   0,  27,   0,  72,  56, 112,  28, 224,  15, 192,   7, 128,   0,   0,   0,   0,
  15, 192,  63, 240, 127, 248, 112,  56, 224,  28, 224,  28, 224,   0, 240,   0,
 124,   0,  63,   0,  15, 192,   3, 240,   0, 248,   0,  60,   0,  28, 224,  28,
 224,  28, 112,  56, 127, 248,  63, 240,  15, 192,  27,  42,  99,  49,  55,  51,
  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  23,   0,  14,   0,  24,   0,  72,  56, 112,  28, 224,  15, 192,   7, 128,   0,
   0,   0,   0,   0,   0,  15, 192,  63, 240, 127, 248, 240,  60, 224,  28, 224,
   0, 248,   0, 127, 128,  63, 240,   7, 248,   0, 124,   0,  28, 224,  28, 240,
  60, 127, 248,  63, 240,  15, 192,  27,  42,  99,  49,  55,  52,  69,  27,  40,
 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  27,   0,  15,
   0,  28,   0,  72,   0, 112,   0, 240,   1, 192,   3,   0,   0,   0,   0,   0,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 240,  30, 120,  60, 127, 252,  63, 248,  15, 224,  27,  42,  99,  49,
  55,  53,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,   0,
   2,   0,  26,   0,  15,   0,  27,   0,  72,  28,  56,  28,  56,  28,  56,   0,
   0,   0,   0, 240,  30, 112,  28, 120,  60,  56,  56,  60, 120,  28, 112,  30,
 240,  14, 224,  15, 224,   7, 192,   7, 192,   3, 128,   3, 128,   3, 128,   3,
 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,  27,
  42,  99,  49,  55,  54,  69,  27,  40, 115,  49,  48,  51,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  22,   0,  17,   0,  29,   0,  72,  28,  56,   0,
  28,  56,   0,  28,  56,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 224,
   3, 128, 240,   7, 128, 112,   7,   0, 120,  15,   0,  56,  14,   0,  60,  30,
   0,  28,  28,   0,  30,  60,   0,  14,  56,   0,  15, 120,   0,   7, 112,   0,
   7, 240,   0,   3, 224,   0,   3, 224,   0,   1, 192,   0,   3, 192,   0,   3,
 128,   0,   7, 128,   0,   7,   0,   0,  15,   0,   0,  14,   0,   0,  30,   0,
   0,  28,   0,   0,  27,  42,  99,  49,  55,  55,  69,  27,  40, 115,  54,  48,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  15,   0,  22,   0,
  72, 224,   0, 224,   0, 224,   0, 224,   0, 255, 192, 255, 240, 255, 248, 224,
  60, 224,  28, 224,  14, 224,  14, 224,  14, 224,  14, 224,  28, 224,  60, 255,
 248, 255, 240, 255, 192, 224,   0, 224,   0, 224,   0, 224,   0,  27,  42,  99,
  49,  55,  56,  69,  27,  40, 115,  55,  56,  87,   4,   0,  14,   1,   0,   0,
   0,   1,   0,  23,   0,  16,   0,  31,   0,  72, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 227, 224, 239, 248, 255, 252, 252,  62,
 240,  14, 240,  15, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15,
 240,  14, 252,  62, 255, 252, 239, 248, 227, 224, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0,  27,  42,  99,  49,  55,  57,  69,  27,
  40, 115,  50,  48,  87,   4,   0,  14,   1,   0,   0,   0,   7,   0,  14,   0,
   4,   0,   4,   0,  72,  96, 240, 240,  96,  27,  42,  99,  49,  56,  48,  69,
  27,  40, 115,  56,  50,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  16,
   0,  17,   0,  22,   0,  72,  14,  28,   0,  14,  28,   0,  14,  28,   0,  14,
  28,   0,  28,  56,   0,  28,  56,   0,  28,  56,   0,  28,  56,   0,  28,  56,
   0,  56, 112,   0,  56, 112,   0,  56, 112,   0,  56, 115, 128, 124, 243, 128,
 127, 255, 128, 119, 191,   0, 115,  28,   0, 112,   0,   0, 224,   0,   0, 224,
   0,   0, 224,   0,   0, 224,   0,   0,  27,  42,  99,  49,  56,  49,  69,  27,
  40, 115,  55,  52,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  23,   0,
  16,   0,  29,   0,  72,   7, 255,  31, 255,  63, 204, 127, 204, 127, 204, 255,
 204, 255, 204, 255, 204, 255, 204, 255, 204, 127, 204, 127, 204,  63, 204,  31,
 204,   7, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,  27,
  42,  99,  49,  56,  50,  69,  27,  40, 115,  56,  56,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  23,   0,  18,   0,  24,   0,  72, 254,  12,   0, 254,
  24,   0,  12,  24,   0,  24,  24,   0,  48,  48,   0,  60,  48,   0,  14,  48,
   0,   6,  96,   0,   6,  96,   0,   6,  96,   0, 206, 192,   0, 252, 198,   0,
 120, 198,   0,   1, 140,   0,   1, 140,   0,   1, 152,   0,   3,  24,   0,   3,
  59,   0,   3,  51,   0,   6, 115,   0,   6, 127, 192,   6, 127, 192,  12,   3,
   0,  12,   3,   0,  27,  42,  99,  49,  56,  51,  69,  27,  40, 115,  50,  50,
  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  12,   0,  15,   0,   3,   0,
  72, 255, 254, 255, 254, 255, 254,  27,  42,  99,  49,  56,  52,  69,  27,  40,
 115,  56,  56,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  23,   0,  18,
   0,  24,   0,  72,  48,  24,   0, 112,  48,   0, 240,  48,   0, 176,  48,   0,
  48,  96,   0,  48,  96,   0,  48,  96,   0,  48, 192,   0,  48, 192,   0,  48,
 192,   0,  49, 128,   0,  49, 134,   0,  49, 134,   0,   3,  12,   0,   3,  12,
   0,   3,  24,   0,   6,  24,   0,   6,  59,   0,   6,  51,   0,  12, 115,   0,
  12, 127, 192,  12, 127, 192,  24,   3,   0,  24,   3,   0,  27,  42,  99,  49,
  56,  53,  69,  27,  40, 115,  56,  56,  87,   4,   0,  14,   1,   0,   0,   0,
   0,   0,  23,   0,  18,   0,  24,   0,  72,  48,  24,   0, 112,  48,   0, 240,
  48,   0, 176,  48,   0,  48,  96,   0,  48,  96,   0,  48,  96,   0,  48, 192,
   0,  48, 192,   0,  48, 192,   0,  49, 128,   0,  49, 143, 128,  49, 159, 192,
   3,  24, 192,   3,  24, 192,   3,   0, 192,   6,   1, 192,   6,   3, 128,   6,
   7,   0,  12,  14,   0,  12,  28,   0,  12,  24,   0,  24,  31, 192,  24,  31,
 192,  27,  42,  99,  49,  56,  54,  69,  27,  40, 115,  52,  52,  87,   4,   0,
  14,   1,   0,   0,   0,   5,   0,  21,   0,   9,   0,  14,   0,  72,  62,   0,
  63,   0,   3,   0,   3,   0,  63,   0, 127,   0,  99,   0,  99,   0, 127,   0,
  59,   0,   0,   0,   0,   0, 255, 128, 255, 128,  27,  42,  99,  49,  56,  55,
  69,  27,  40, 115,  52,  52,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,
  21,   0,  10,   0,  14,   0,  72,  30,   0,  63,   0, 115, 128,  97, 128,  97,
 128,  97, 128,  97, 128, 115, 128,  63,   0,  30,   0,   0,   0,   0,   0, 255,
 192, 255, 192,  27,  42,  99,  49,  56,  56,  69,  27,  40, 115,  53,  53,  87,
   4,   0,  14,   1,   0,   0,   0,   1,   0,  14,   0,  17,   0,  13,   0,  72,
   3, 131, 128,   7,   7,   0,  14,  14,   0,  28,  28,   0,  60,  60,   0, 120,
 120,   0, 240, 240,   0, 120, 120,   0,  60,  60,   0,  28,  28,   0,  14,  14,
   0,   7,   7,   0,   3, 131, 128,  27,  42,  99,  49,  56,  57,  69,  27,  40,
 115,  52,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  15,   0,  12,
   0,  12,   0,  72, 255, 240, 255, 240, 255, 240, 255, 240, 255, 240, 255, 240,
 255, 240, 255, 240, 255, 240, 255, 240, 255, 240, 255, 240,  27,  42,  99,  49,
  57,  48,  69,  27,  40, 115,  53,  53,  87,   4,   0,  14,   1,   0,   0,   0,
   1,   0,  14,   0,  17,   0,  13,   0,  72, 224, 224,   0, 112, 112,   0,  56,
  56,   0,  28,  28,   0,  30,  30,   0,  15,  15,   0,   7, 135, 128,  15,  15,
   0,  30,  30,   0,  28,  28,   0,  56,  56,   0, 112, 112,   0, 224, 224,   0,
  27,  42,  99,  49,  57,  49,  69,  27,  40, 115,  53,  50,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  17,   0,  13,   0,  18,   0,  72,   7,   0,   7,
   0,   7,   0,   7,   0,   7,   0, 255, 248, 255, 248, 255, 248,   7,   0,   7,
   0,   7,   0,   7,   0,   7,   0,   0,   0,   0,   0, 255, 248, 255, 248, 255,
 248,  27,  42,  99,  49,  57,  50,  69,  27,  40, 115,  52,  57,  87,   4,   0,
  14,   1,   0,   0,   0,   8,   0,  24,   0,   3,   0,  33,   0,  72, 224, 224,
 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,   0,   0,   0,   0,
 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,  27,
  42,  99,  49,  57,  51,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  21,   0,  18,   0,  18,   0,  72,   1, 224,   0,  15,
 252,   0,  28,  14,   0,  49, 227,   0,  99, 241, 128, 103,  57, 128, 198,  24,
 192, 204,   0, 192, 204,   0, 192, 204,   0, 192, 204,  12, 192, 198,  28, 192,
 103,  57, 128,  99, 241, 128,  48, 195,   0,  28,  14,   0,  15, 252,   0,   1,
 224,   0,  27,  42,  99,  49,  57,  52,  69,  27,  40, 115,  51,  48,  87,   4,
   0,  14,   1,   0,   0,   0,   1,   0,  15,   0,  16,   0,   7,   0,  72, 255,
 255, 255, 255, 255, 255,   0,   7,   0,   7,   0,   7,   0,   7,  27,  42,  99,
  49,  57,  54,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,
   0,   0,   0,  21,   0,  18,   0,  18,   0,  72,   1, 224,   0,  15, 252,   0,
  28,  14,   0,  48,   3,   0, 103, 241, 128, 103, 249, 128, 198,  24, 192, 198,
  24, 192, 199, 240, 192, 199, 240, 192, 198,  56, 192, 198,  24, 192, 102,  25,
 128, 102,  25, 128,  48,   3,   0,  28,  14,   0,  15, 252,   0,   1, 224,   0,
  27,  42,  99,  49,  57,  55,  69,  27,  40, 115,  50,  57,  87,   4,   0,  14,
   1,   0,   0,   0,   4,   0,  22,   0,   7,   0,  13,   0,  72, 124, 254, 198,
 198,   6,  14,  28,  56, 112, 224, 192, 254, 254,  27,  42,  99,  49,  57,  56,
  69,  27,  40, 115,  50,  57,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,
  22,   0,   7,   0,  13,   0,  72, 254, 254,  12,  24,  48,  60,  14,   6,   6,
   6, 206, 252, 120,  27,  42,  99,  49,  57,  57,  69,  27,  40, 115,  50,  50,
  87,   4,   0,  14,   1,   0,   0,   0,   6, 255, 255,   0,   6,   0,   6,   0,
  72,  48,  24,  12, 204, 204, 120,  27,  42,  99,  50,  48,  48,  69,  27,  40,
 115,  50,  57,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  22,   0,   4,
   0,  13,   0,  72,  48, 112, 240, 176,  48,  48,  48,  48,  48,  48,  48,  48,
  48,  27,  42,  99,  50,  48,  49,  69,  27,  40, 115,  52,  54,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  18,   0,  15,   0,  15,   0,  72,  64,   4,
 224,  14, 112,  28,  56,  56,  28, 112,  14, 224,   7, 192,   3, 128,   7, 192,
  14, 224,  28, 112,  56,  56, 112,  28, 224,  14,  64,   4,  27,  42,  99,  50,
  48,  50,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,   0,   0,   0,
   1,   0,  19,   0,  16,   0,  17,   0,  72,   1, 128,   3, 192,   3, 192,   1,
 128,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255,   0,   0,   0,
   0,   0,   0,   1, 128,   3, 192,   3, 192,   1, 128,  27,  42,  99,  50,  48,
  51,  69,  27,  40, 115,  54,  55,  87,   4,   0,  14,   1,   0,   0,   0,   0,
   0,  17,   0,  17,   0,  17,   0,  72,   7, 240,   0,  28,  28,   0,  48,   6,
   0,  96,   3,   0,  64,   1,   0, 198,  49, 128, 134,  48, 128, 128,   0, 128,
 128,   0, 128, 128,   0, 128, 140,  24, 128, 198,  49, 128,  67, 225,   0,  96,
   3,   0,  48,   6,   0,  28,  28,   0,   7, 240,   0,  27,  42,  99,  50,  48,
  52,  69,  27,  40, 115,  54,  55,  87,   4,   0,  14,   1,   0,   0,   0,   0,
   0,  17,   0,  17,   0,  17,   0,  72,   7, 240,   0,  31, 252,   0,  63, 254,
   0, 127, 255,   0, 115, 231,   0, 225, 195, 128, 225, 195, 128, 243, 231, 128,
 255, 255, 128, 255, 255, 128, 227, 227, 128, 225, 195, 128, 112,   7,   0, 124,
  31,   0,  63, 254,   0,  31, 252,   0,   7, 240,   0,  27,  42,  99,  50,  48,
  53,  69,  27,  40, 115,  55,  51,  87,   4,   0,  14,   1,   0,   0,   0,   0,
   0,  19,   0,  17,   0,  19,   0,  72,  60,  30,   0, 126,  63,   0, 255, 127,
 128, 255, 127, 128, 255, 255, 128, 255, 255, 128, 255, 255, 128, 127, 255,   0,
 127, 255,   0,  63, 254,   0,  31, 252,   0,  15, 248,   0,  15, 248,   0,   7,
 240,   0,   3, 224,   0,   3, 224,   0,   1, 192,   0,   0, 128,   0,   0, 128,
   0,  27,  42,  99,  50,  48,  54,  69,  27,  40, 115,  53,  52,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  19,   0,  15,   0,  19,   0,  72,   1,   0,
   3, 128,   7, 192,   7, 192,  15, 224,  31, 240,  63, 248,  63, 248, 127, 252,
 255, 254, 127, 252,  63, 248,  63, 248,  31, 240,  15, 224,   7, 192,   7, 192,
   3, 128,   1,   0,  27,  42,  99,  50,  48,  55,  69,  27,  40, 115,  53,  52,
  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  19,   0,  16,   0,  19,   0,
  72,   3, 192,   7, 224,  15, 240,  15, 240,  15, 240,  15, 240,   7, 224,   3,
 192,  57, 156, 125, 190, 255, 255, 255, 255, 255, 255, 125, 190,  57, 156,   1,
 128,   1, 128,   3, 192,   7, 224,  27,  42,  99,  50,  48,  56,  69,  27,  40,
 115,  53,  52,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  19,   0,  15,
   0,  19,   0,  72,   1,   0,   1,   0,   3, 128,   7, 192,  15, 224,  15, 224,
  31, 240,  63, 248, 127, 252, 127, 252, 255, 254, 255, 254, 255, 254, 253, 126,
 125, 124,  57,  56,   1,   0,   3, 128,   7, 192,  27,  42,  99,  50,  48,  57,
  69,  27,  40, 115,  52,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,
  15,   0,  12,   0,  12,   0,  72,  15,   0,  63, 192, 127, 224, 127, 224, 255,
 240, 255, 240, 255, 240, 255, 240, 127, 224, 127, 224,  63, 192,  15,   0,  27,
  42,  99,  50,  49,  48,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  18,   0,  18,   0,  18,   0,  72, 255, 255, 192, 255,
 255, 192, 255, 255, 192, 254,  31, 192, 248,   7, 192, 240,   3, 192, 240,   3,
 192, 224,   1, 192, 224,   1, 192, 224,   1, 192, 224,   1, 192, 240,   3, 192,
 240,   3, 192, 248,   7, 192, 254,  31, 192, 255, 255, 192, 255, 255, 192, 255,
 255, 192,  27,  42,  99,  50,  49,  49,  69,  27,  40, 115,  52,  56,  87,   4,
   0,  14,   1,   0,   0,   0,   1,   0,  17,   0,  16,   0,  16,   0,  72,   7,
 224,  31, 248,  60,  60, 112,  14,  96,   6, 224,   7, 192,   3, 192,   3, 192,
   3, 192,   3, 224,   7,  96,   6, 112,  14,  60,  60,  31, 248,   7, 224,  27,
  42,  99,  50,  49,  50,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  18,   0,  18,   0,  18,   0,  72, 255, 255, 192, 252,
  15, 192, 240,   3, 192, 225, 225, 192, 199, 248, 192, 207, 252, 192, 143, 252,
  64, 159, 254,  64, 159, 254,  64, 159, 254,  64, 159, 254,  64, 143, 252,  64,
 207, 252, 192, 199, 248, 192, 225, 225, 192, 240,   3, 192, 252,  15, 192, 255,
 255, 192,  27,  42,  99,  50,  49,  51,  69,  27,  40, 115,  54,  55,  87,   4,
   0,  14,   1,   0,   0,   0,   1,   0,  19,   0,  17,   0,  17,   0,  72,   0,
  31, 128,   0,  31, 128,   0,   7, 128,   0,  15, 128,  15, 157, 128,  63, 249,
 128, 112, 112,   0,  96,  48,   0, 192,  24,   0, 192,  24,   0, 192,  24,   0,
 192,  24,   0, 192,  24,   0,  96,  48,   0, 112, 112,   0,  63, 224,   0,  15,
 128,   0,  27,  42,  99,  50,  49,  52,  69,  27,  40, 115,  53,  54,  87,   4,
   0,  14,   1,   0,   0,   0,   3,   0,  19,   0,  13,   0,  20,   0,  72,  15,
 128,  63, 224, 112, 112,  96,  48, 192,  24, 192,  24, 192,  24, 192,  24, 192,
  24,  96,  48, 112, 112,  63, 224,  15, 128,   3,   0,   3,   0,   3,   0,  15,
 192,  15, 192,   3,   0,   3,   0,  27,  42,  99,  50,  49,  53,  69,  27,  40,
 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  24,   0,  13,
   0,  24,   0,  72,   1,   0,   1, 128,   1, 192,   1, 224,   1, 224,   1, 176,
   1, 152,   1, 152,   1, 136,   1, 136,   1, 136,   1, 128,   1, 128,   1, 128,
   1, 128,   1, 128,   1, 128,  61, 128, 127, 128, 255, 128, 255, 128, 255, 128,
 127,   0,  62,   0,  27,  42,  99,  50,  49,  54,  69,  27,  40, 115,  49,  48,
  57,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  27,   0,  17,   0,  31,
   0,  72,   1,   0,   0,   1, 128,   0,   1, 192,   0,   1, 224,   0,   1, 176,
   0,   1, 152,   0,   1, 140,   0,   1, 198,   0,   1, 227,   0,   1, 177, 128,
   1, 153, 128,   1, 141, 128,   1, 135, 128,   1, 131, 128,   1, 129, 128,   1,
 129, 128,   1, 129, 128,  61, 129, 128, 127, 129, 128, 255, 129, 128, 255, 129,
 128, 255, 129, 128, 127,   1, 128,  62,   1, 128,   0,  61, 128,   0, 127, 128,
   0, 255, 128,   0, 255, 128,   0, 255, 128,   0, 127,   0,   0,  62,   0,  27,
  42,  99,  50,  49,  55,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  19,   0,  18,   0,  18,   0,  72,   0, 192,   0,   0,
 192,   0,  48, 195,   0,  56, 199,   0,  29, 238,   0,  15, 252,   0,   6,  24,
   0,  12,  12,   0, 252,  15, 192, 252,  15, 192,  12,  12,   0,   6,  24,   0,
  15, 252,   0,  29, 238,   0,  56, 199,   0,  48, 195,   0,   0, 192,   0,   0,
 192,   0,  27,  42,  99,  50,  49,  56,  69,  27,  40, 115,  52,  54,  87,   4,
   0,  14,   1,   0,   0,   0,   1,   0,  14,   0,  15,   0,  15,   0,  72, 128,
   0, 224,   0, 248,   0, 254,   0, 255, 128, 255, 224, 255, 248, 255, 254, 255,
 248, 255, 224, 255, 128, 254,   0, 248,   0, 224,   0, 128,   0,  27,  42,  99,
  50,  49,  57,  69,  27,  40, 115,  52,  54,  87,   4,   0,  14,   1,   0,   0,
   0,   1,   0,  14,   0,  15,   0,  15,   0,  72,   0,   2,   0,  14,   0,  62,
   0, 254,   3, 254,  15, 254,  63, 254, 255, 254,  63, 254,  15, 254,   3, 254,
   0, 254,   0,  62,   0,  14,   0,   2,  27,  42,  99,  50,  50,  48,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  21,   0,
  11,   0,  22,   0,  72,   4,   0,  14,   0,  31,   0,  63, 128, 238, 224, 206,
  96,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,
   0,  14,   0,  14,   0, 206,  96, 238, 224,  63, 128,  31,   0,  14,   0,   4,
   0,  27,  42,  99,  50,  50,  49,  69,  27,  40, 115,  54,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   4,   0,  21,   0,  10,   0,  22,   0,  72, 243, 192,
 243, 192, 243, 192, 243, 192, 243, 192, 243, 192, 243, 192, 243, 192, 243, 192,
 243, 192, 243, 192, 243, 192, 243, 192, 243, 192, 243, 192, 243, 192,   0,   0,
   0,   0,   0,   0, 243, 192, 243, 192, 243, 192,  27,  42,  99,  50,  50,  50,
  69,  27,  40, 115,  51,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,
   6,   0,  12,   0,   7,   0,  72, 255, 240, 255, 240, 255, 240, 255, 240, 255,
 240, 255, 240, 255, 240,  27,  42,  99,  50,  50,  51,  69,  27,  40, 115,  54,
  56,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  21,   0,  11,   0,  26,
   0,  72,   4,   0,  14,   0,  31,   0,  63, 128, 238, 224, 206,  96,  14,   0,
  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,
  14,   0, 206,  96, 238, 224,  63, 128,  31,   0,  14,   0,   4,   0,   0,   0,
   0,   0, 255, 224, 255, 224,  27,  42,  99,  50,  50,  52,  69,  27,  40, 115,
  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,  21,   0,  11,   0,
  22,   0,  72,   4,   0,  14,   0,  31,   0,  63, 128, 238, 224, 206,  96,  14,
   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,
   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  27,
  42,  99,  50,  50,  53,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   4,   0,  21,   0,  11,   0,  22,   0,  72,  14,   0,  14,   0,
  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,
  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0, 206,  96, 238, 224,
  63, 128,  31,   0,  14,   0,   4,   0,  27,  42,  99,  50,  50,  54,  69,  27,
  40, 115,  52,  57,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  16,   0,
  18,   0,  11,   0,  72,   0,  12,   0,   0,  12,   0,   0,   6,   0,   0,   3,
   0, 255, 255, 128, 255, 255, 192, 255, 255, 128,   0,   3,   0,   0,   6,   0,
   0,  12,   0,   0,  12,   0,  27,  42,  99,  50,  50,  55,  69,  27,  40, 115,
  52,  57,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  16,   0,  18,   0,
  11,   0,  72,  12,   0,   0,  12,   0,   0,  24,   0,   0,  48,   0,   0, 127,
 255, 192, 255, 255, 192, 127, 255, 192,  48,   0,   0,  24,   0,   0,  12,   0,
   0,  12,   0,   0,  27,  42,  99,  50,  50,  56,  69,  27,  40, 115,  51,  48,
  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  12,   0,  16,   0,   7,   0,
  72, 224,   0, 224,   0, 224,   0, 224,   0, 255, 255, 255, 255, 255, 255,  27,
  42,  99,  50,  50,  57,  69,  27,  40, 115,  52,  57,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  16,   0,  18,   0,  11,   0,  72,  12,  12,   0,  12,
  12,   0,  24,   6,   0,  48,   3,   0, 127, 255, 128, 255, 255, 192, 127, 255,
 128,  48,   3,   0,  24,   6,   0,  12,  12,   0,  12,  12,   0,  27,  42,  99,
  50,  51,  48,  69,  27,  40, 115,  52,  54,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  14,   0,  15,   0,  15,   0,  72,   1,   0,   1,   0,   3, 128,
   3, 128,   7, 192,   7, 192,  15, 224,  15, 224,  31, 240,  31, 240,  63, 248,
  63, 248, 127, 252, 127, 252, 255, 254,  27,  42,  99,  50,  51,  49,  69,  27,
  40, 115,  52,  54,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  14,   0,
  15,   0,  15,   0,  72, 255, 254, 127, 252, 127, 252,  63, 248,  63, 248,  31,
 240,  31, 240,  15, 224,  15, 224,   7, 192,   7, 192,   3, 128,   3, 128,   1,
   0,   1,   0,  27,  42,  99,  50,  51,  50,  69,  27,  40, 115,  54,  54,  87,
   4,   0,  14,   1,   0,   0,   0,   1,   0,  21,   0,  15,   0,  25,   0,  72,
 255, 224, 127, 248,  63, 252,  56,  28,  56,  14,  56,  14,  56,  14,  56,  14,
  56,  28,  63, 252,  63, 248,  63, 224,  56,   0,  56,   0,  56,  48,  56,  48,
  56, 252,  56, 252,  56,  48,  56,  48, 124,  48, 254,  48,   0,  54,   0,  62,
   0,  28,  27,  42,  99,  50,  51,  51,  69,  27,  40, 115,  51,  48,  87,   4,
   0,  14,   1,   0,   0,   0,   1,   0,  15,   0,  16,   0,   7,   0,  72, 255,
 255, 255, 255, 255, 255, 224,   0, 224,   0, 224,   0, 224,   0,  27,  42,  99,
  50,  51,  52,  69,  27,  40, 115,  56,  52,  87,   4,   0,  14,   1,   0,   0,
   0,   0,   0,  27,   0,  15,   0,  34,   0,  72, 227, 142, 227, 142, 227, 142,
   0,   0,   0,   0,   0,   0, 227, 142, 227, 142, 227, 142,   0,   0,   0,   0,
   0,   0, 227, 142, 227, 142, 227, 142,   0,   0,   0,   0,   0,   0,   0,   0,
 227, 142, 227, 142, 227, 142,   0,   0,   0,   0,   0,   0, 227, 142, 227, 142,
 227, 142,   0,   0,   0,   0,   0,   0, 227, 142, 227, 142, 227, 142,  27,  42,
  99,  50,  51,  53,  69,  27,  40, 115,  49,  51,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  27,   0,  18,   0,  38,   0,  72, 255, 255, 192, 255,
 255, 192, 255, 255, 192, 227, 142,   0, 227, 142,   0, 227, 142,   0, 255, 255,
 192, 255, 255, 192, 255, 255, 192, 227, 142,   0, 227, 142,   0, 227, 142,   0,
 255, 255, 192, 255, 255, 192, 255, 255, 192, 227, 142,   0, 227, 142,   0, 227,
 142,   0, 227, 142,   0, 255, 255, 192, 255, 255, 192, 255, 255, 192, 227, 142,
   0, 227, 142,   0, 227, 142,   0, 255, 255, 192, 255, 255, 192, 255, 255, 192,
 227, 142,   0, 227, 142,   0, 227, 142,   0, 255, 255, 192, 255, 255, 192, 255,
 255, 192, 227, 142,   0, 227, 142,   0, 227, 142,   0, 227, 142,   0,  27,  42,
  99,  50,  51,  54,  69,  27,  40, 115,  53,  52,  87,   4,   0,  14,   1,   0,
   0,   0,   8,   0,  27,   0,   2,   0,  38,   0,  72, 192, 192, 192, 192, 192,
 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192,
 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192, 192,
 192,  27,  42,  99,  50,  51,  55,  69,  27,  40, 115,  57,  50,  87,   4,   0,
  14,   1,   0,   0,   0,   0,   0,  27,   0,  10,   0,  38,   0,  72,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192, 255, 192, 255, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,  27,  42,  99,  50,  51,  56,
  69,  27,  40, 115,  57,  50,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,
  27,   0,  10,   0,  38,   0,  72,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192, 255, 192, 255, 192,   0, 192,   0, 192, 255,
 192, 255, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,  27,  42,  99,  50,  51,  57,  69,  27,  40, 115,  57,  50,  87,
   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,  12,   0,  38,   0,  72,
   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,
   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,
   3,  48,   3,  48, 255,  48, 255,  48,   3,  48,   3,  48,   3,  48,   3,  48,
   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,
   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,  27,  42,  99,  50,
  52,  48,  69,  27,  40, 115,  53,  54,  87,   4,   0,  14,   1,   0,   0,   0,
   0,   0,   9,   0,  12,   0,  20,   0,  72, 255, 240, 255, 240,   3,  48,   3,
  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,  27,  42,  99,  50,  52,  49,  69,  27,  40, 115,  54,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   0,   0,  11,   0,  10,   0,  22,   0,  72, 255, 192,
 255, 192,   0, 192,   0, 192, 255, 192, 255, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,  27,  42,  99,  50,  52,  50,
  69,  27,  40, 115,  57,  50,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,
  27,   0,  12,   0,  38,   0,  72,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,   3,  48,   3,  48,   3,  48, 255,  48, 255,  48,   0,  48,   0,  48, 255,
  48, 255,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,   3,  48,  27,  42,  99,  50,  52,  51,  69,  27,  40, 115,  53,  52,  87,
   4,   0,  14,   1,   0,   0,   0,   6,   0,  27,   0,   6,   0,  38,   0,  72,
 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204,
 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204, 204,
 204, 204, 204, 204, 204, 204,  27,  42,  99,  50,  52,  52,  69,  27,  40, 115,
  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  11,   0,  12,   0,
  22,   0,  72, 255, 240, 255, 240,   0,  48,   0,  48, 255,  48, 255,  48,   3,
  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,  27,
  42,  99,  50,  52,  53,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  27,   0,  12,   0,  22,   0,  72,   3,  48,   3,  48,
   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,
   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48, 255,  48, 255,  48,
   0,  48,   0,  48, 255, 240, 255, 240,  27,  42,  99,  50,  52,  54,  69,  27,
  40, 115,  53,  54,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,
  12,   0,  20,   0,  72,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,  48,   3,
  48,   3,  48,   3,  48,   3,  48,   3,  48, 255, 240, 255, 240,  27,  42,  99,
  50,  52,  55,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,
   0,   0,   0,  27,   0,  10,   0,  22,   0,  72,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192, 255, 192, 255, 192,   0, 192,
   0, 192, 255, 192, 255, 192,  27,  42,  99,  50,  52,  56,  69,  27,  40, 115,
  53,  54,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,   9,   0,  10,   0,
  20,   0,  72, 255, 192, 255, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,  27,  42,  99,  50,  52,
  57,  69,  27,  40, 115,  53,  54,  87,   4,   0,  14,   1,   0,   0,   0,   8,
   0,  27,   0,  10,   0,  20,   0,  72, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 255, 192, 255, 192,
  27,  42,  99,  50,  53,  48,  69,  27,  40, 115,  55,  54,  87,   4,   0,  14,
   1,   0,   0,   0,   0,   0,  27,   0,  18,   0,  20,   0,  72,   0, 192,   0,
   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0,
 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,
   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,
   0, 192,   0, 255, 255, 192, 255, 255, 192,  27,  42,  99,  50,  53,  49,  69,
  27,  40, 115,  55,  54,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,   9,
   0,  18,   0,  20,   0,  72, 255, 255, 192, 255, 255, 192,   0, 192,   0,   0,
 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,
   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,
   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0,
 192,   0,  27,  42,  99,  50,  53,  50,  69,  27,  40, 115,  57,  50,  87,   4,
   0,  14,   1,   0,   0,   0,   8,   0,  27,   0,  10,   0,  38,   0,  72, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 255, 192, 255, 192, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,  27,  42,  99,  50,  53,
  51,  69,  27,  40, 115,  50,  50,  87,   4,   0,  14,   1,   0,   0,   0,   0,
   0,   9,   0,  18,   0,   2,   0,  72, 255, 255, 192, 255, 255, 192,  27,  42,
  99,  50,  53,  52,  69,  27,  40, 115,  49,  51,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   0,   0,  27,   0,  18,   0,  38,   0,  72,   0, 192,   0,   0,
 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,
   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,
   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0,
 192,   0, 255, 255, 192, 255, 255, 192,   0, 192,   0,   0, 192,   0,   0, 192,
   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,
   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0,
 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,  27,  42,
  99,  50,  53,  53,  69,  27,  40, 115,  57,  50,  87,   4,   0,  14,   1,   0,
   0,   0,   8,   0,  27,   0,  10,   0,  38,   0,  72, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 255, 192, 255, 192, 192,
   0, 192,   0, 255, 192, 255, 192, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0,  27,  42,  99,  50,  53,  54,  69,  27,  40,
 115,  57,  50,  87,   4,   0,  14,   1,   0,   0,   0,   6,   0,  27,   0,  12,
   0,  38,   0,  72, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
 204,   0, 204,   0, 204,   0, 204,   0, 207, 240, 207, 240, 204,   0, 204,   0,
 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
  27,  42,  99,  50,  53,  55,  69,  27,  40, 115,  54,  50,  87,   4,   0,  14,
   1,   0,   0,   0,   6,   0,  28,   0,  12,   0,  23,   0,  72, 204,   0, 204,
   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,
   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 207,
 240, 207, 240, 192,   0, 192,   0, 255, 240, 255, 240,  27,  42,  99,  50,  53,
  56,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   6,
   0,  11,   0,  12,   0,  22,   0,  72, 255, 240, 255, 240, 192,   0, 192,   0,
 207, 240, 207, 240, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
 204,   0, 204,   0,  27,  42,  99,  50,  53,  57,  69,  27,  40, 115,  56,  50,
  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,  18,   0,  22,   0,
  72,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,
   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,
  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,
   0, 255,  63, 192, 255,  63, 192,   0,   0,   0,   0,   0,   0, 255, 255, 192,
 255, 255, 192,  27,  42,  99,  50,  54,  48,  69,  27,  40, 115,  56,  50,  87,
   4,   0,  14,   1,   0,   0,   0,   0,   0,  11,   0,  18,   0,  22,   0,  72,
 255, 255, 192, 255, 255, 192,   0,   0,   0,   0,   0,   0, 255,  63, 192, 255,
  63, 192,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,
   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,
   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,
  48,   0,  27,  42,  99,  50,  54,  49,  69,  27,  40, 115,  57,  50,  87,   4,
   0,  14,   1,   0,   0,   0,   6,   0,  27,   0,  12,   0,  38,   0,  72, 204,
   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,
   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 207,
 240, 207, 240, 192,   0, 192,   0, 207, 240, 207, 240, 204,   0, 204,   0, 204,
   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,
   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,  27,  42,  99,  50,  54,
  50,  69,  27,  40, 115,  51,  52,  87,   4,   0,  14,   1,   0,   0,   0,   0,
   0,  11,   0,  18,   0,   6,   0,  72, 255, 255, 192, 255, 255, 192,   0,   0,
   0,   0,   0,   0, 255, 255, 192, 255, 255, 192,  27,  42,  99,  50,  54,  51,
  69,  27,  40, 115,  49,  51,  48,  87,   4,   0,  14,   1,   0,   0,   0,   0,
   0,  27,   0,  18,   0,  38,   0,  72,   3,  48,   0,   3,  48,   0,   3,  48,
   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,
   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,
  48,   0,   3,  48,   0,   3,  48,   0, 255,  63, 192, 255,  63, 192,   0,   0,
   0,   0,   0,   0, 255,  63, 192, 255,  63, 192,   3,  48,   0,   3,  48,   0,
   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,
  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,
   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,  27,  42,  99,  50,  54,  52,
  69,  27,  40, 115,  56,  50,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,
  27,   0,  18,   0,  22,   0,  72,   0, 192,   0,   0, 192,   0,   0, 192,   0,
   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0,
 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,
   0,   0, 192,   0,   0, 192,   0, 255, 255, 192, 255, 255, 192,   0,   0,   0,
   0,   0,   0, 255, 255, 192, 255, 255, 192,  27,  42,  99,  50,  54,  53,  69,
  27,  40, 115,  55,  54,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,
   0,  18,   0,  20,   0,  72,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,
  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,
   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,
   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0, 255, 255, 192, 255,
 255, 192,  27,  42,  99,  50,  54,  54,  69,  27,  40, 115,  56,  50,  87,   4,
   0,  14,   1,   0,   0,   0,   0,   0,  11,   0,  18,   0,  22,   0,  72, 255,
 255, 192, 255, 255, 192,   0,   0,   0,   0,   0,   0, 255, 255, 192, 255, 255,
 192,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,
   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0,
 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,
   0,  27,  42,  99,  50,  54,  55,  69,  27,  40, 115,  55,  54,  87,   4,   0,
  14,   1,   0,   0,   0,   0,   0,   9,   0,  18,   0,  20,   0,  72, 255, 255,
 192, 255, 255, 192,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,
   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,
  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,
   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,  27,  42,  99,  50,  54,  56,
  69,  27,  40, 115,  53,  54,  87,   4,   0,  14,   1,   0,   0,   0,   6,   0,
  27,   0,  12,   0,  20,   0,  72, 204,   0, 204,   0, 204,   0, 204,   0, 204,
   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,
   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 255, 240, 255, 240,  27,
  42,  99,  50,  54,  57,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   8,   0,  27,   0,  10,   0,  22,   0,  72, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 255, 192, 255, 192,
 192,   0, 192,   0, 255, 192, 255, 192,  27,  42,  99,  50,  55,  48,  69,  27,
  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   8,   0,  11,   0,
  10,   0,  22,   0,  72, 255, 192, 255, 192, 192,   0, 192,   0, 255, 192, 255,
 192, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,
   0,  27,  42,  99,  50,  55,  49,  69,  27,  40, 115,  53,  54,  87,   4,   0,
  14,   1,   0,   0,   0,   6,   0,   9,   0,  12,   0,  20,   0,  72, 255, 240,
 255, 240, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0, 204,   0,
 204,   0, 204,   0, 204,   0,  27,  42,  99,  50,  55,  50,  69,  27,  40, 115,
  49,  51,  48,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,  18,
   0,  38,   0,  72,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,
   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,
  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,
   0,   3,  48,   0,   3,  48,   0,   3,  48,   0, 255, 255, 192, 255, 255, 192,
   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,
  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,
   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,   3,  48,   0,
   3,  48,   0,   3,  48,   0,  27,  42,  99,  50,  55,  51,  69,  27,  40, 115,
  49,  51,  48,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,  18,
   0,  38,   0,  72,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,
   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0,
 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,
   0,   0, 192,   0, 255, 255, 192, 255, 255, 192,   0, 192,   0,   0, 192,   0,
 255, 255, 192, 255, 255, 192,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0,
 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,
   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,   0, 192,   0,
   0, 192,   0,   0, 192,   0,  27,  42,  99,  50,  55,  52,  69,  27,  40, 115,
  53,  54,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,  10,   0,
  20,   0,  72,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192, 255, 192, 255, 192,  27,  42,  99,  50,  55,
  53,  69,  27,  40, 115,  53,  54,  87,   4,   0,  14,   1,   0,   0,   0,   8,
   0,   9,   0,  10,   0,  20,   0,  72, 255, 192, 255, 192, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0, 192,   0,
  27,  42,  99,  50,  55,  54,  69,  27,  40, 115,  49,  51,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   0,   0,  27,   0,  18,   0,  38,   0,  72, 255, 255,
 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192,
 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255,
 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255,
 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192,
 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255,
 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255,
 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192,
  27,  42,  99,  50,  55,  55,  69,  27,  40, 115,  55,  51,  87,   4,   0,  14,
   1,   0,   0,   0,   0,   0,   8,   0,  18,   0,  19,   0,  72, 255, 255, 192,
 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255,
 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255,
 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192,
 255, 255, 192, 255, 255, 192,  27,  42,  99,  50,  55,  56,  69,  27,  40, 115,
  57,  50,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,   9,   0,
  38,   0,  72, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255,
 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255,
 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255,
 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255,
 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128,  27,
  42,  99,  50,  55,  57,  69,  27,  40, 115,  57,  50,  87,   4,   0,  14,   1,
   0,   0,   0,   9,   0,  27,   0,   9,   0,  38,   0,  72, 255, 128, 255, 128,
 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128,
 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128,
 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128,
 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128,
 255, 128, 255, 128, 255, 128, 255, 128,  27,  42,  99,  50,  56,  48,  69,  27,
  40, 115,  55,  51,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,   0,
  18,   0,  19,   0,  72, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255,
 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192,
 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255,
 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192, 255, 255, 192,  27,  42,
  99,  50,  56,  49,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   1,   0,  16,   0,  16,   0,  17,   0,  72,   3, 128,  15, 231,  31,
 231,  60, 247,  56, 119, 112, 118, 112, 126, 224, 124, 224, 124, 224, 120, 224,
 120, 224, 240, 224, 240, 113, 242, 127, 255,  63, 191,  14,  14,  27,  42,  99,
  50,  56,  51,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,
   0,   1,   0,  21,   0,  16,   0,  22,   0,  72, 255, 255, 255, 255, 255, 255,
  28,  15,  28,   7,  28,   7,  28,   7,  28,   0,  28,   0,  28,   0,  28,   0,
  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,
 255, 192, 255, 192, 255, 192,  27,  42,  99,  50,  56,  52,  69,  27,  40, 115,
  54,  55,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  16,   0,  17,   0,
  17,   0,  72,  15, 255, 128,  63, 255, 128, 127, 255, 128, 115, 156,   0, 227,
 156,   0, 227, 156,   0, 195,  24,   0,   7,  56,   0,   7,  56,   0,   7,  56,
   0,   6,  48,   0,  14, 112,   0,  14, 112,   0,  14, 112,   0,  14, 120,   0,
  30, 120,   0,  28,  56,   0,  27,  42,  99,  50,  56,  53,  69,  27,  40, 115,
  56,  50,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  21,   0,  17,   0,
  22,   0,  72, 255, 255, 128, 255, 255, 128, 255, 255, 128, 120,   7, 128,  60,
   3, 128,  30,   3, 128,  15,   3, 128,   7, 128,   0,   3, 192,   0,   1, 224,
   0,   0, 240,   0,   1, 224,   0,   3, 192,   0,   7, 128,   0,  15,   3, 128,
  30,   3, 128,  60,   3, 128, 120,   7, 128, 240,  15, 128, 255, 255, 128, 255,
 255, 128, 255, 255, 128,  27,  42,  99,  50,  56,  54,  69,  27,  40, 115,  54,
  55,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  16,   0,  17,   0,  17,
   0,  72,   1, 255, 128,   7, 255, 128,  15, 255, 128,  30,  56,   0,  56,  28,
   0, 120,  28,   0, 112,  28,   0, 240,  28,   0, 224,  28,   0, 224,  60,   0,
 224,  56,   0, 224, 120,   0, 224, 112,   0, 240, 240,   0, 127, 224,   0, 127,
 192,   0,  31,   0,   0,  27,  42,  99,  50,  56,  55,  69,  27,  40, 115,  53,
  48,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  16,   0,  15,   0,  17,
   0,  72,  15, 254,  63, 254, 127, 254, 113, 192, 225, 192, 227, 192, 195, 128,
   3, 128,   3, 128,   3, 128,   7,   0,   7,   0,   7,   0,   7,   0,  15,   0,
  14,   0,  14,   0,  27,  42,  99,  50,  56,  56,  69,  27,  40, 115,  54,  48,
  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  21,   0,  15,   0,  22,   0,
  72,  31, 240,  31, 240,  31, 240,   3, 128,   3, 128,  15, 224,  63, 248, 127,
 252, 115, 156, 227, 142, 227, 142, 227, 142, 227, 142, 115, 156, 127, 252,  63,
 248,  15, 224,   3, 128,   3, 128,  31, 240,  31, 240,  31, 240,  27,  42,  99,
  50,  56,  57,  69,  27,  40, 115,  56,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   0,   0,  21,   0,  18,   0,  22,   0,  72,   3, 240,   0,  15, 252,   0,
  31, 254,   0,  62,  31,   0, 120,   7, 128, 112,   3, 128, 240,   3, 192, 224,
   1, 192, 230,  25, 192, 230,  25, 192, 231, 249, 192, 231, 249, 192, 230,  25,
 192, 230,  25, 192, 224,   1, 192, 240,   3, 192, 112,   3, 128, 120,   7, 128,
  62,  31,   0,  31, 254,   0,  15, 252,   0,   3, 240,   0,  27,  42,  99,  50,
  57,  48,  69,  27,  40, 115,  56,  50,  87,   4,   0,  14,   1,   0,   0,   0,
   0,   0,  21,   0,  18,   0,  22,   0,  72,   3, 240,   0,  15, 252,   0,  31,
 254,   0,  62,  31,   0,  56,   7,   0, 120,   7, 128, 112,   3, 128, 112,   3,
 128, 112,   3, 128, 112,   3, 128,  56,   7,   0,  56,   7,   0,  56,   7,   0,
  28,  14,   0,  28,  14,   0,  28,  14,   0, 206,  28, 192, 206,  28, 192, 230,
  25, 192, 231,  57, 192, 127,  63, 128, 127,  63, 128,  27,  42,  99,  50,  57,
  49,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   1,
   0,  23,   0,  15,   0,  24,   0,  72,   0, 248,   3, 252,   3, 254,   7, 142,
   7,  14,   7,   0,   7,   0,   7, 128,   3, 128,   3, 192,   7, 224,  31, 240,
  60, 112, 120,  56, 112,  56, 240,  56, 224,  56, 224,  56, 224, 120, 224, 112,
 112, 240, 127, 224,  63, 192,  15,   0,  27,  42,  99,  50,  57,  50,  69,  27,
  40, 115,  53,  56,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  18,   0,
  18,   0,  14,   0,  72,   0,  28,   0,  30, 127,   0, 127, 127, 128, 127, 227,
 128, 243, 193, 192, 225, 193, 192, 225, 193, 192, 225, 193, 192, 225, 193, 192,
 243, 193, 192, 127, 227, 128, 127, 127, 128,  30, 127,   0,   0,  28,   0,  27,
  42,  99,  50,  57,  51,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   1,   0,  21,   0,  16,   0,  27,   0,  72,   0,  48,   0,  48,
   0,  48,   0,  96,   0,  96,   3, 240,  15, 252,  31, 254,  62, 222, 120, 207,
 112, 207, 241, 135, 225, 135, 225, 135, 225, 135, 225, 143, 243,  14, 243,  30,
 123, 124, 127, 248,  63, 240,  15, 192,   6,   0,   6,   0,  12,   0,  12,   0,
  12,   0,  27,  42,  99,  50,  57,  52,  69,  27,  40, 115,  53,  48,  87,   4,
   0,  14,   1,   0,   0,   0,   3,   0,  16,   0,  12,   0,  17,   0,  72,   3,
 240,  15, 240,  31, 240,  62,   0,  56,   0, 112,   0, 112,   0, 255, 192, 255,
 192, 255, 192, 224,   0, 224,   0, 224,   0, 112, 192, 127, 224,  63, 224,  15,
 128,  27,  42,  99,  50,  57,  53,  69,  27,  40, 115,  53,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  16,   0,  16,   0,  17,   0,  72,   7, 224,
  31, 248,  63, 252, 124,  62, 112,  14, 240,  15, 224,   7, 224,   7, 224,   7,
 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7,
  27,  42,  99,  50,  57,  54,  69,  27,  40, 115,  52,  54,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  18,   0,  16,   0,  15,   0,  72, 255, 255, 255,
 255, 255, 255,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255,   0,
   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255,  27,  42,  99,  50,  57,
  55,  69,  27,  40, 115,  53,  56,  87,   4,   0,  14,   1,   0,   0,   0,   2,
   0,  20,   0,  14,   0,  21,   0,  72, 128,   0, 224,   0, 248,   0, 126,   0,
  31, 128,   7, 224,   1, 248,   0, 124,   1, 248,   7, 224,  31, 128, 126,   0,
 248,   0, 224,   0, 128,   0,   0,   0,   0,   0,   0,   0, 255, 252, 255, 252,
 255, 252,  27,  42,  99,  50,  57,  56,  69,  27,  40, 115,  53,  56,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  20,   0,  14,   0,  21,   0,  72,   0,
   4,   0,  28,   0, 124,   1, 248,   7, 224,  31, 128, 126,   0, 248,   0, 126,
   0,  31, 128,   7, 224,   1, 248,   0, 124,   0,  28,   0,   4,   0,   0,   0,
   0,   0,   0, 255, 252, 255, 252, 255, 252,  27,  42,  99,  50,  57,  57,  69,
  27,  40, 115,  56,  52,  87,   4,   0,  14,   1,   0,   0,   0,   8,   0,  23,
   0,  10,   0,  34,   0,  72,  30,   0,  63, 128, 127, 128, 115, 192, 227, 192,
 227, 192, 225, 128, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,  27,  42,  99,  51,  48,  48,
  69,  27,  40, 115,  56,  54,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,
  27,   0,  10,   0,  35,   0,  72,   1, 192,   1, 192,   1, 192,   1, 192,   1,
 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,   1,
 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,   1,
 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,   1, 192,  97,
 192, 241, 192, 241, 192, 243, 128, 127, 128, 127,   0,  30,   0,  27,  42,  99,
  51,  48,  49,  69,  27,  40, 115,  52,  48,  87,   4,   0,  14,   1,   0,   0,
   0,   1,   0,  17,   0,  16,   0,  12,   0,  72,  14,   7,  63, 143, 127, 254,
 241, 252, 224, 112,   0,   0,   0,   0,  14,   7,  63, 143, 127, 254, 241, 252,
 224, 112,  27,  42,  99,  51,  48,  50,  69,  27,  40, 115,  50,  49,  87,   4,
   0,  14,   1,   0,   0,   0,   6,   0,  13,   0,   5,   0,   5,   0,  72, 112,
 248, 248, 248, 112,  27,  42,  99,  51,  48,  51,  69,  27,  40, 115,  49,  48,
  54,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  25,   0,  18,   0,  30,
   0,  72,   0,   0, 192,   0,   1, 192,   0,   1, 192,   0,   1, 192,   0,   3,
 128,   0,   3, 128,   0,   3, 128,   0,   7,   0,   0,   7,   0,   0,   6,   0,
   0,  14,   0,   0,  14,   0,   0,  12,   0,   0,  28,   0,   0,  28,   0,   0,
  56,   0,   0,  56,   0,  16,  56,   0,  56, 112,   0, 120, 112,   0, 220, 112,
   0,  28, 224,   0,  14, 224,   0,  14, 224,   0,   7, 192,   0,   7, 192,   0,
   3, 192,   0,   3, 128,   0,   1, 128,   0,   1,   0,   0,  27,  42,  99,  51,
  48,  53,  69,  27,  40, 115,  51,  52,  87,   4,   0,  14,   1,   0,   0,   0,
   5,   0,  13,   0,   9,   0,   9,   0,  72, 255, 128, 255, 128, 255, 128, 255,
 128, 255, 128, 255, 128, 255, 128, 255, 128, 255, 128,  27,  42,  99,  51,  48,
  54,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,
   0,  21,   0,  13,   0,  22,   0,  72, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,  48, 224, 120, 224, 120, 224,  48, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255, 240,
 255, 240, 255, 240,  27,  42,  99,  51,  48,  55,  69,  27,  40, 115,  54,  52,
  87,   4,   0,  14,   1,   0,   0,   0,   6,   0,  23,   0,  12,   0,  24,   0,
  72, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,  96, 224, 240, 224, 240, 224,  96, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 240,   0, 126,   0, 126,   0,  30,
   0,  27,  42,  99,  51,  48,  56,  69,  27,  40, 115,  54,  52,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  23,   0,  16,   0,  24,   0,  72,   1, 224,
   3, 240,   7, 240,   7,  56,  14,  56,  14,  56,  14,  56,  28, 112,  28, 112,
  28, 224,  28, 224,  29, 192,  31, 128,  31,   0,  30,   0,  28,   0,  60,   0,
 124,   0, 238,   0,  78,   0,   7,   2,   7, 143,   3, 254,   0, 248,  27,  42,
  99,  51,  48,  57,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,
   0,   0,   1,   0,  23,   0,  16,   0,  24,   0,  72,  14,   0,  30,   0,  60,
   0, 112,   0, 192,   0,   0,   0,   0,   0, 113, 240, 119, 252, 127, 254, 126,
  30, 120,  15, 120,   7, 112,   7, 112,   7, 112,   7, 112,   7, 112,   7, 112,
   7, 112,   7, 112,   7, 112,   7, 112,   7, 112,   7,  27,  42,  99,  51,  49,
  48,  69,  27,  40, 115,  52,  48,  87,   4,   0,  14,   1,   0,   0,   0,   4,
   0,  21,   0,   9,   0,  12,   0,  72,  15, 128,  15,   0,  31,   0,  30,   0,
  30,   0,  60,   0,  60,   0, 120,   0, 120,   0, 112,   0, 240,   0, 224,   0,
  27,  42,  99,  51,  49,  49,  69,  27,  40, 115,  52,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  21,   0,  16,   0,  12,   0,  72,  15, 159,  15,
  30,  31,  62,  30,  60,  30,  60,  60, 120,  60, 120, 120, 240, 120, 240, 112,
 224, 241, 224, 225, 192,  27,  42,  99,  51,  49,  50,  69,  27,  40, 115,  55,
  52,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  23,   0,  15,   0,  29,
   0,  72,   3, 128,   7, 192,   7, 192,   7, 192,   7, 192,   3, 128,   3, 128,
 123, 188, 255, 254, 255, 254, 255, 254, 123, 188,   3, 128,   3, 128,   3, 128,
   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,
   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,   3, 128,  27,  42,  99,  51,
  49,  51,  69,  27,  40, 115,  54,  49,  87,   4,   0,  14,   1,   0,   0,   0,
   0,   0,  21,   0,  18,   0,  15,   0,  72, 255,  96, 192, 255,  96, 192,  24,
  96, 192,  24, 113, 192,  24, 113, 192,  24, 113, 192,  24, 123, 192,  24, 123,
 192,  24, 127, 192,  24, 110, 192,  24, 110, 192,  24, 100, 192,  24,  96, 192,
  24,  96, 192,  24,  96, 192,  27,  42,  99,  51,  49,  52,  69,  27,  40, 115,
  52,  51,  87,   4,   0,  14,   1,   0,   0,   0,   0, 255, 250,   0,  18,   0,
   9,   0,  72, 255, 255, 192, 255, 255, 192, 255, 255, 192,   0,   0,   0,   0,
   0,   0,   0,   0,   0, 255, 255, 192, 255, 255, 192, 255, 255, 192,  27,  42,
  99,  51,  49,  53,  69,  27,  40, 115,  50,  52,  87,   4,   0,  14,   1,   0,
   0,   0,   4,   0,  23,   0,  10,   0,   4,   0,  72, 225, 192, 115, 128,  63,
   0,  30,   0,  27,  42,  99,  51,  50,  53,  69,  27,  40, 115,  50,  50,  87,
   4,   0,  14,   1,   0,   0,   0,   1,   0,  12,   0,  15,   0,   3,   0,  72,
 255, 254, 255, 254, 255, 254,  27,  42,  99,  51,  50,  54,  69,  27,  40, 115,
  50,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  12,   0,  13,   0,
   3,   0,  72, 255, 248, 255, 248, 255, 248,  27,  42,  99,  51,  50,  56,  69,
  27,  40, 115,  51,  51,  87,   4,   0,  14,   1,   0,   0,   0,   5,   0,  16,
   0,   6,   0,  17,   0,  72, 252, 252, 252,  28,  28,  28,  28,  28,  28,  28,
  28,  28,  28,  28,  28,  28,  28,  27,  42,  99,  51,  50,  57,  69,  27,  40,
 115,  50,  56,  87,   4,   0,  14,   1,   0,   0,   0,   7,   0,  21,   0,   4,
   0,  12,   0,  72, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240, 240,
  27,  42,  99,  51,  51,  50,  69,  27,  40, 115,  50,  53,  87,   4,   0,  14,
   1,   0,   0,   0,   4,   0,  18,   0,   8,   0,   9,   0,  72, 220, 254, 231,
 195, 195, 195, 195, 195, 195,  27,  42,  99,  51,  51,  51,  69,  27,  40, 115,
  52,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  13,   0,  14,   0,
  14,   0,  72,   3,   0,   7, 128,  15, 192,  28, 224,  56, 112, 112,  56, 224,
  28, 192,  12, 192,  12, 192,  12, 192,  12, 192,  12, 255, 252, 255, 252,  27,
  42,  99,  51,  51,  53,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   3,   0,  16,   0,  14,   0,  17,   0,  72, 224,  60, 224, 120,
 224, 240, 225, 224, 227, 192, 231, 128, 239,   0, 254,   0, 252,   0, 254,   0,
 239,   0, 231, 128, 227, 192, 225, 224, 224, 240, 224, 120, 224,  60,  27,  42,
  99,  51,  51,  56,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  21,   0,  13,   0,  22,   0,  72, 255, 248, 255, 248, 255,
 248,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0, 127,
 240, 127, 240, 127, 240,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,
   0,   7,   0,   7,   0,   7,   0,  27,  42,  99,  51,  51,  57,  69,  27,  40,
 115,  53,  56,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  20,   0,  12,
   0,  21,   0,  72,  12,   0,  28,   0,  28,   0,  28,   0, 255, 240, 255, 240,
 255, 240,  28,   0,  28,   0,  28,   0, 255, 240, 255, 240, 255, 240,  28,   0,
  28,   0,  28,   0,  28,   0,  30,   0,  15, 240,  15, 240,   3, 224,  27,  42,
  99,  51,  52,  48,  69,  27,  40, 115,  55,  52,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  21,   0,  13,   0,  29,   0,  72, 224,  56, 224,  56, 240,
  56, 240,  56, 248,  56, 248,  56, 252,  56, 252,  56, 254,  56, 238,  56, 239,
  56, 231,  56, 231, 184, 227, 184, 227, 248, 225, 248, 225, 248, 224, 248, 224,
 248, 224, 120, 224, 120, 224,  56,   0,  56,   0,  56,   0, 120,   0, 240,   3,
 240,   3, 224,   3, 128,  27,  42,  99,  51,  52,  49,  69,  27,  40, 115,  54,
  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  15,   0,  15,   0,  23,
   0,  72, 227, 224, 239, 248, 255, 252, 252,  60, 240,  30, 240,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14,   0,  14,   0,  14,   0,  30,   0,  60,   0, 252,   0, 248,   0, 224,
  27,  42,  99,  51,  52,  50,  69,  27,  40, 115,  57,  49,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  24,   0,  18,   0,  25,   0,  72,   0,   7,   0,
   0,   7,   0,   0,   7,   0,   0,  63, 192,   0,  63, 192,   0,  63, 192,   0,
   7,   0,   0,   7,   0,   7, 199,   0,  31, 247,   0,  63, 255,   0, 124,  63,
   0, 112,  15,   0, 240,  15,   0, 224,   7,   0, 224,   7,   0, 224,   7,   0,
 224,   7,   0, 224,   7,   0, 240,  15,   0, 112,  15,   0, 124,  63,   0,  63,
 255,   0,  31, 247,   0,   7, 199,   0,  27,  42,  99,  52,  48,  48,  69,  27,
  40, 115,  49,  48,  48,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  27,
   0,  17,   0,  28,   0,  72,  28,  14,   0,  30,  30,   0,  15, 252,   0,   7,
 248,   0,   0,   0,   0,   0,   0,   0,   3, 224,   0,   3, 224,   0,   3, 224,
   0,   7, 240,   0,   7, 112,   0,   7, 112,   0,  15, 120,   0,  14,  56,   0,
  14,  56,   0,  14,  56,   0,  30,  60,   0,  28,  28,   0,  28,  28,   0,  31,
 252,   0,  63, 254,   0,  63, 254,   0,  56,  14,   0,  56,  14,   0, 120,  15,
   0, 112,   7,   0, 112,   7,   0, 240,   7, 128,  27,  42,  99,  52,  48,  49,
  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  23,   0,  14,   0,  24,   0,  72, 112,  56, 120, 120,  63, 240,  31, 224,   0,
   0,   0,   0,   0,   0,  15, 224,  63, 248,  63, 248, 120,  60, 112,  28,   0,
  28,   0,  28,  15, 252,  63, 252, 124,  28, 240,  28, 224,  28, 224,  60, 240,
 252, 127, 252, 127, 220,  31,  28,  27,  42,  99,  52,  48,  50,  69,  27,  40,
 115,  57,  52,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,  25,   0,  17,
   0,  26,   0,  72,  15, 248,   0,  15, 248,   0,   0,   0,   0,   0,   0,   0,
   3, 224,   0,   3, 224,   0,   3, 224,   0,   7, 240,   0,   7, 112,   0,   7,
 112,   0,  15, 120,   0,  14,  56,   0,  14,  56,   0,  14,  56,   0,  30,  60,
   0,  28,  28,   0,  28,  28,   0,  31, 252,   0,  63, 254,   0,  63, 254,   0,
  56,  14,   0,  56,  14,   0, 120,  15,   0, 112,   7,   0, 112,   7,   0, 240,
   7, 128,  27,  42,  99,  52,  48,  51,  69,  27,  40, 115,  54,  48,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  14,   0,  22,   0,  72,  31,
 224,  31, 224,   0,   0,   0,   0,   0,   0,  15, 224,  63, 248,  63, 248, 120,
  60, 112,  28,   0,  28,   0,  28,  15, 252,  63, 252, 124,  28, 240,  28, 224,
  28, 224,  60, 240, 252, 127, 252, 127, 220,  31,  28,  27,  42,  99,  52,  48,
  52,  69,  27,  40, 115,  49,  48,  48,  87,   4,   0,  14,   1,   0,   0,   0,
   0,   0,  21,   0,  18,   0,  28,   0,  72,   3, 224,   0,   3, 224,   0,   3,
 224,   0,   7, 240,   0,   7, 112,   0,   7, 112,   0,  15, 120,   0,  14,  56,
   0,  14,  56,   0,  14,  56,   0,  30,  60,   0,  28,  28,   0,  28,  28,   0,
  31, 252,   0,  63, 254,   0,  63, 254,   0,  56,  14,   0,  56,  14,   0, 120,
  15,   0, 112,   7,   0, 112,   7,   0, 240,   7, 128,   0,   3,   0,   0,   6,
   0,   0,  12,   0,   0,  12, 192,   0,  12, 192,   0,   7, 128,  27,  42,  99,
  52,  48,  53,  69,  27,  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  16,   0,  15,   0,  23,   0,  72,  15, 224,  63, 248,  63, 248,
 120,  60, 112,  28,   0,  28,   0,  28,  15, 252,  63, 252, 124,  28, 240,  28,
 224,  28, 224,  60, 240, 252, 127, 252, 127, 220,  31, 156,   0,  24,   0,  48,
   0,  96,   0, 102,   0, 102,   0,  60,  27,  42,  99,  52,  48,  54,  69,  27,
  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  27,   0,
  14,   0,  28,   0,  72,   0, 224,   1, 224,   3, 128,   6,   0,   0,   0,   0,
   0,  15, 224,  63, 248, 127, 248, 120,  60, 112,  28, 240,  28, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 240,  28, 112,  28, 120,  60, 127, 248,  63, 248,  15, 224,  27,  42,  99,
  52,  48,  55,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  23,   0,  14,   0,  24,   0,  72,   0, 224,   1, 224,   3, 192,
   7,   0,  12,   0,   0,   0,   0,   0,   7, 192,  31, 240,  63, 248, 120,  60,
 112,  28, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 112,  28, 120,  60,  63, 248,  31, 240,   7, 192,  27,  42,  99,  52,  49,  48,
  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  27,   0,  14,   0,  28,   0,  72,  56, 112,  28, 224,  15, 192,   7, 128,   0,
   0,   0,   0,  15, 224,  63, 248, 127, 248, 120,  60, 112,  28, 240,  28, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 240,  28, 112,  28, 120,  60, 127, 248,  63, 248,  15, 224,  27,
  42,  99,  52,  49,  49,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,
   0,   0,   0,   2,   0,  23,   0,  14,   0,  24,   0,  72,  56, 112,  28, 224,
  15, 192,   7, 128,   0,   0,   0,   0,   0,   0,   7, 192,  31, 240,  63, 248,
 120,  60, 112,  28, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 112,  28, 120,  60,  63, 248,  31, 240,   7, 192,  27,  42,  99,  52,
  49,  52,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,
   1,   0,  27,   0,  14,   0,  28,   0,  72,  56, 112,  28, 224,  15, 192,   7,
 128,   0,   0,   0,   0, 255,   0, 255, 192, 255, 224, 225, 240, 224, 120, 224,
  56, 224,  60, 224,  28, 224,  28, 224,  28, 224,  28, 224,  28, 224,  28, 224,
  28, 224,  28, 224,  60, 224,  56, 224, 120, 225, 240, 255, 224, 255, 192, 255,
   0,  27,  42,  99,  52,  49,  53,  69,  27,  40, 115,  55,  50,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  27,   0,  16,   0,  28,   0,  72,  28,  56,
  14, 112,   7, 224,   3, 192,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,
   0,   7,   0,   7,   7, 199,  31, 247,  63, 255, 124,  63, 112,  15, 240,  15,
 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 112,  15, 124,  63,
  63, 255,  31, 247,   7, 199,  27,  42,  99,  52,  49,  54,  69,  27,  40, 115,
  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  27,   0,  13,   0,
  28,   0,  72,  56, 112,  28, 224,  15, 192,   7, 128,   0,   0,   0,   0, 255,
 248, 255, 248, 255, 248, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 255, 240, 255, 240, 255, 240, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,  52,  49,
  55,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,
   0,  23,   0,  15,   0,  24,   0,  72,  28,  56,  14, 112,   7, 224,   3, 192,
   0,   0,   0,   0,   0,   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28,
 224,  14, 224,  14, 255, 254, 255, 254, 224,   0, 224,   0, 224,   0, 112,   0,
 120,  28,  63, 252,  31, 248,   7, 224,  27,  42,  99,  52,  49,  56,  69,  27,
  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  26,   0,
  13,   0,  27,   0,  72,   7,   0,   7,   0,   7,   0,   0,   0,   0,   0, 255,
 248, 255, 248, 255, 248, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 255, 240, 255, 240, 255, 240, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,  52,  49,
  57,  69,  27,  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,
   0,  22,   0,  15,   0,  23,   0,  72,   3, 128,   3, 128,   3, 128,   0,   0,
   0,   0,   0,   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,  14,
 224,  14, 255, 254, 255, 254, 224,   0, 224,   0, 224,   0, 112,   0, 120,  28,
  63, 252,  31, 248,   7, 224,  27,  42,  99,  52,  50,  48,  69,  27,  40, 115,
  54,  56,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  25,   0,  13,   0,
  26,   0,  72,  63, 224,  63, 224,   0,   0,   0,   0, 255, 248, 255, 248, 255,
 248, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255, 240, 255,
 240, 255, 240, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,  52,  50,  49,  69,  27,  40,
 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  15,
   0,  22,   0,  72,  31, 240,  31, 240,   0,   0,   0,   0,   0,   0,   7, 192,
  31, 240,  63, 248, 120,  60, 112,  28, 224,  14, 224,  14, 255, 254, 255, 254,
 224,   0, 224,   0, 224,   0, 112,   0, 120,  28,  63, 252,  31, 248,   7, 224,
  27,  42,  99,  52,  50,  50,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  21,   0,  13,   0,  28,   0,  72, 255, 248, 255,
 248, 255, 248, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 255,
 240, 255, 240, 255, 240, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 255, 248, 255, 248, 255, 248,   3,   0,   6,   0,  12,   0,  12,
 192,  12, 192,   7, 128,  27,  42,  99,  52,  50,  51,  69,  27,  40, 115,  54,
  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  16,   0,  15,   0,  23,
   0,  72,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,  14, 224,  14,
 255, 254, 255, 254, 224,   0, 224,   0, 224,   0, 112,   0, 120,  28,  63, 252,
  31, 248,   7, 224,   1, 128,   3,   0,   6,   0,   6,  96,   6,  96,   3, 192,
  27,  42,  99,  52,  50,  56,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  21,   0,  14,   0,  28,   0,  72,   7, 192,  31,
 240,  63, 248, 120,  56, 112,  28, 240,  28, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224, 252, 224, 252, 224, 252, 224,  28, 224,  28, 224,  28, 240,
  28, 120,  28, 127, 252,  63, 252,  15, 252,   0,   0,   1, 192,   3, 192,   3,
 128,   7,   0,   6,   0,  27,  42,  99,  52,  50,  57,  69,  27,  40, 115,  55,
  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  22,   0,  14,   0,  28,
   0,  72,   0,  96,   0, 224,   1, 192,   3, 192,   3, 128,   0,   0,  15, 156,
  63, 252, 127, 252, 120, 124, 240,  60, 224,  28, 224,  28, 224,  28, 224,  28,
 224,  28, 240,  60, 120, 124, 127, 252,  63, 252,  15, 156,   0,  28,   0,  28,
 224,  60, 240, 120, 127, 248,  63, 240,  15, 192,  27,  42,  99,  52,  51,  50,
  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   5,   0,
  21,   0,   9,   0,  28,   0,  72, 254,   0, 254,   0, 254,   0,  56,   0,  56,
   0,  56,   0,  56,   0,  56,   0,  56,   0,  56,   0,  56,   0,  56,   0,  56,
   0,  56,   0,  56,   0,  56,   0,  56,   0,  56,   0,  56,   0, 254,   0, 254,
   0, 254,   0,   6,   0,  12,   0,  24,   0,  25, 128,  25, 128,  15,   0,  27,
  42,  99,  52,  51,  51,  69,  27,  40, 115,  52,  54,  87,   4,   0,  14,   1,
   0,   0,   0,   5,   0,  23,   0,   8,   0,  30,   0,  72,  62,  62,  62,   0,
   0,   0,   0, 252, 252, 252,  28,  28,  28,  28,  28,  28,  28,  28,  28,  28,
  28,  28,  28,  28,  12,  24,  48,  51,  51,  30,  27,  42,  99,  52,  51,  52,
  69,  27,  40, 115,  54,  56,  87,   4,   0,  14,   1,   0,   0,   0,   4,   0,
  25,   0,   9,   0,  26,   0,  72, 255, 128, 255, 128,   0,   0,   0,   0, 127,
   0, 127,   0, 127,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0, 127,   0, 127,   0, 127,   0,  27,  42,  99,  52,  51,
  53,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   5,
   0,  21,   0,   9,   0,  22,   0,  72, 255, 128, 255, 128,   0,   0,   0,   0,
   0,   0, 252,   0, 252,   0, 252,   0,  28,   0,  28,   0,  28,   0,  28,   0,
  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,
  28,   0,  28,   0,  27,  42,  99,  52,  51,  56,  69,  27,  40, 115,  55,  50,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  16,   0,  28,   0,
  72, 224,  30, 224,  60, 224, 120, 224, 240, 225, 224, 227, 192, 231, 128, 239,
   0, 254,   0, 252,   0, 248,   0, 252,   0, 254,   0, 239,   0, 231, 128, 227,
 192, 225, 224, 224, 240, 224, 120, 224,  60, 224,  30, 224,  15,   0,   0,   3,
 128,   7, 128,   7,   0,  14,   0,  12,   0,  27,  42,  99,  52,  51,  57,  69,
  27,  40, 115,  55,  54,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  23,
   0,  14,   0,  30,   0,  72, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224, 120, 224, 240, 225, 224, 227, 192, 231, 128, 239,   0,
 254,   0, 252,   0, 252,   0, 254,   0, 239,   0, 231, 128, 227, 192, 225, 224,
 224, 240, 224, 120, 224,  60,   0,   0,   3, 128,   7, 128,   7,   0,  14,   0,
  12,   0,  27,  42,  99,  52,  52,  48,  69,  27,  40, 115,  55,  50,  87,   4,
   0,  14,   1,   0,   0,   0,   3,   0,  27,   0,  12,   0,  28,   0,  72,   3,
 128,   7, 128,  14,   0,  24,   0,   0,   0,   0,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 255, 240, 255, 240, 255, 240,  27,  42,  99,  52,  52,  49,  69,  27,  40,
 115,  52,  53,  87,   4,   0,  14,   1,   0,   0,   0,   6,   0,  28,   0,   7,
   0,  29,   0,  72,  14,  30,  56,  96,   0, 224, 224, 224, 224, 224, 224, 224,
 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 240, 126, 126,
  30,  27,  42,  99,  52,  52,  50,  69,  27,  40, 115,  55,  50,  87,   4,   0,
  14,   1,   0,   0,   0,   3,   0,  27,   0,  12,   0,  28,   0,  72, 112, 224,
  57, 192,  31, 128,  15,   0,   0,   0,   0,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 255, 240, 255, 240, 255, 240,  27,  42,  99,  52,  52,  51,  69,  27,  40, 115,
  55,  52,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  28,   0,  10,   0,
  29,   0,  72, 225, 192, 115, 128,  63,   0,  30,   0,   0,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  30,   0,  15, 192,  15, 192,   3, 192,  27,  42,  99,
  52,  52,  52,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   3,   0,  21,   0,  12,   0,  28,   0,  72, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,
 255, 240, 255, 240, 255, 240,   0,   0,   7,   0,  15,   0,  14,   0,  28,   0,
  24,   0,  27,  42,  99,  52,  52,  53,  69,  27,  40, 115,  52,  54,  87,   4,
   0,  14,   1,   0,   0,   0,   5,   0,  23,   0,   8,   0,  30,   0,  72, 112,
 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112, 112,
 112, 112, 112, 120,  63,  63,  15,   0,  56, 120, 112, 224, 192,  27,  42,  99,
  52,  52,  54,  69,  27,  40, 115,  55,  52,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  28,   0,  13,   0,  29,   0,  72,   1, 192,   3, 192,   7, 128,
  14,   0,  24,   0,   0,   0,   0,   0, 224,  56, 224,  56, 240,  56, 240,  56,
 248,  56, 248,  56, 252,  56, 252,  56, 254,  56, 238,  56, 239,  56, 231,  56,
 231, 184, 227, 184, 227, 248, 225, 248, 225, 248, 224, 248, 224, 248, 224, 120,
 224, 120, 224,  56,  27,  42,  99,  52,  52,  55,  69,  27,  40, 115,  54,  52,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,   0,
  72,   0, 224,   1, 224,   3, 192,   7,   0,  12,   0,   0,   0,   0,   0, 227,
 224, 239, 248, 255, 252, 252,  60, 240,  30, 240,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14,  27,  42,  99,  52,  52,  56,  69,  27,  40, 115,  55,  50,  87,   4,   0,
  14,   1,   0,   0,   0,   2,   0,  27,   0,  13,   0,  28,   0,  72,  56, 112,
  28, 224,  15, 192,   7, 128,   0,   0,   0,   0, 224,  56, 224,  56, 240,  56,
 240,  56, 248,  56, 248,  56, 252,  56, 252,  56, 254,  56, 238,  56, 239,  56,
 231,  56, 231, 184, 227, 184, 227, 248, 225, 248, 225, 248, 224, 248, 224, 248,
 224, 120, 224, 120, 224,  56,  27,  42,  99,  52,  52,  57,  69,  27,  40, 115,
  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  22,   0,  15,   0,
  23,   0,  72,  28,  56,  14, 112,   7, 224,   3, 192,   0,   0,   0,   0, 227,
 224, 239, 248, 255, 252, 252,  60, 240,  30, 240,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14,  27,  42,  99,  52,  53,  48,  69,  27,  40, 115,  55,  50,  87,   4,   0,
  14,   1,   0,   0,   0,   2,   0,  21,   0,  13,   0,  28,   0,  72, 224,  56,
 224,  56, 240,  56, 240,  56, 248,  56, 248,  56, 252,  56, 252,  56, 254,  56,
 238,  56, 239,  56, 231,  56, 231, 184, 227, 184, 227, 248, 225, 248, 225, 248,
 224, 248, 224, 248, 224, 120, 224, 120, 224,  56,   0,   0,   3, 128,   7, 128,
   7,   0,  14,   0,  12,   0,  27,  42,  99,  52,  53,  49,  69,  27,  40, 115,
  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  16,   0,  15,   0,
  23,   0,  72, 227, 224, 239, 248, 255, 252, 252,  60, 240,  30, 240,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14,   0,   0,   1, 192,   3, 192,   3, 128,   7,   0,   6,
   0,  27,  42,  99,  52,  53,  50,  69,  27,  40, 115,  55,  48,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  26,   0,  16,   0,  27,   0,  72,   3, 156,
   7, 188,  14, 112,  24, 192,   0,   0,   7, 224,  31, 248,  63, 252, 124,  62,
 112,  14, 240,  15, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7,
 224,   7, 224,   7, 224,   7, 224,   7, 240,  15, 112,  14, 124,  62,  63, 252,
  31, 248,   7, 224,  27,  42,  99,  52,  53,  51,  69,  27,  40, 115,  54,  52,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,   0,
  72,   3, 156,   7, 188,  15, 120,  28, 224,  49, 128,   0,   0,   0,   0,   7,
 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 112,  28, 120,  60,  63, 248,  31, 240,   7,
 192,  27,  42,  99,  52,  53,  52,  69,  27,  40, 115,  54,  54,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  24,   0,  16,   0,  25,   0,  72,  15, 240,
  15, 240,   0,   0,   0,   0,   7, 224,  31, 248,  63, 252, 124,  62, 112,  14,
 240,  15, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7, 224,   7,
 224,   7, 224,   7, 240,  15, 112,  14, 124,  62,  63, 252,  31, 248,   7, 224,
  27,  42,  99,  52,  53,  53,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  21,   0,  15,   0,  22,   0,  72,  31, 240,  31,
 240,   0,   0,   0,   0,   7, 192,  31, 240,  63, 248, 120,  60, 112,  28, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 112,
  28, 120,  60,  63, 248,  31, 240,   7, 192,  27,  42,  99,  52,  53,  54,  69,
  27,  40, 115,  55,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  28,
   0,  14,   0,  29,   0,  72,   1, 192,   3, 192,   7, 128,  14,   0,  24,   0,
   0,   0,   0,   0, 255, 128, 255, 224, 255, 240, 224, 240, 224, 120, 224,  56,
 224,  56, 224,  56, 224, 120, 224, 240, 255, 240, 255, 192, 255,   0, 231, 128,
 227, 128, 227, 192, 225, 224, 224, 224, 224, 240, 224, 120, 224,  56, 224,  60,
  27,  42,  99,  52,  53,  55,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,
   1,   0,   0,   0,   4,   0,  23,   0,  12,   0,  24,   0,  72,   1, 192,   3,
 192,   7, 128,  14,   0,  24,   0,   0,   0,   0,   0, 231, 128, 255, 224, 253,
 240, 240, 112, 224, 112, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,  27,  42,  99,
  52,  53,  56,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  27,   0,  14,   0,  28,   0,  72, 112, 224,  57, 192,  31, 128,
  15,   0,   0,   0,   0,   0, 255, 128, 255, 224, 255, 240, 224, 240, 224, 120,
 224,  56, 224,  56, 224,  56, 224, 120, 224, 240, 255, 240, 255, 192, 255,   0,
 231, 128, 227, 128, 227, 192, 225, 224, 224, 224, 224, 240, 224, 120, 224,  56,
 224,  60,  27,  42,  99,  52,  53,  57,  69,  27,  40, 115,  54,  50,  87,   4,
   0,  14,   1,   0,   0,   0,   4,   0,  22,   0,  12,   0,  23,   0,  72, 112,
 224,  57, 192,  31, 128,  15,   0,   0,   0,   0,   0, 231, 128, 255, 224, 253,
 240, 240, 112, 224, 112, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0,  27,  42,  99,
  52,  54,  48,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  21,   0,  14,   0,  28,   0,  72, 255, 128, 255, 224, 255, 240,
 224, 240, 224, 120, 224,  56, 224,  56, 224,  56, 224, 120, 224, 240, 255, 240,
 255, 192, 255,   0, 231, 128, 227, 128, 227, 192, 225, 224, 224, 224, 224, 240,
 224, 120, 224,  56, 224,  60,   0,   0,   3, 128,   7, 128,   7,   0,  14,   0,
  12,   0,  27,  42,  99,  52,  54,  49,  69,  27,  40, 115,  54,  50,  87,   4,
   0,  14,   1,   0,   0,   0,   4,   0,  16,   0,  12,   0,  23,   0,  72, 231,
 128, 255, 224, 253, 240, 240, 112, 224, 112, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,   0, 224,
   0,   0,   0,  14,   0,  30,   0,  28,   0,  56,   0,  48,   0,  27,  42,  99,
  52,  54,  50,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,
   0,   2,   0,  27,   0,  14,   0,  28,   0,  72,   0, 224,   1, 224,   3, 128,
   6,   0,   0,   0,   0,   0,  15, 192,  63, 240, 127, 248, 120, 120, 224,  28,
 224,  28, 224,   0, 240,   0, 124,   0,  63,   0,  15, 192,   3, 240,   0, 248,
   0,  60,   0,  28,   0,  28, 224,  28, 224,  28, 120, 120, 127, 248,  63, 240,
  15, 192,  27,  42,  99,  52,  54,  51,  69,  27,  40, 115,  54,  52,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  14,   0,  24,   0,  72,   0,
 224,   1, 224,   3, 192,   7,   0,  12,   0,   0,   0,   0,   0,  15, 192,  63,
 240, 127, 248, 240,  60, 224,  28, 224,   0, 248,   0, 127, 128,  63, 240,   7,
 248,   0, 124,   0,  28, 224,  28, 240,  56, 127, 248,  63, 240,  15, 192,  27,
  42,  99,  52,  54,  54,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,
   0,   0,   0,   2,   0,  27,   0,  13,   0,  28,   0,  72,  56, 112,  28, 224,
  15, 192,   7, 128,   0,   0,   0,   0, 255, 248, 255, 248, 255, 248,   7,   0,
   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,
   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,
   7,   0,   7,   0,  27,  42,  99,  52,  54,  55,  69,  27,  40, 115,  55,  48,
  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  26,   0,  12,   0,  27,   0,
  72, 112, 224,  57, 192,  31, 128,  15,   0,   0,   0,   0,   0,  12,   0,  28,
   0,  28,   0,  28,   0, 255, 240, 255, 240, 255, 240,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  30,
   0,  15, 240,  15, 240,   3, 224,  27,  42,  99,  52,  54,  56,  69,  27,  40,
 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  21,   0,  13,
   0,  28,   0,  72, 255, 248, 255, 248, 255, 248,   7,   0,   7,   0,   7,   0,
   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,
   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,
   3,   0,   1, 128,   0, 192,  12, 192,  12, 192,   7, 128,  27,  42,  99,  52,
  54,  57,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,   0,
   3,   0,  20,   0,  12,   0,  27,   0,  72,  12,   0,  28,   0,  28,   0,  28,
   0, 255, 240, 255, 240, 255, 240,  28,   0,  28,   0,  28,   0,  28,   0,  28,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  30,   0,  15, 240,  15,
 240,   7, 224,   6,   0,   3,   0,   1, 128,  25, 128,  25, 128,  15,   0,  27,
  42,  99,  52,  55,  48,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   1,   0,  26,   0,  15,   0,  27,   0,  72,  28,  24,  63,  56,
 115, 240,  96, 224,   0,   0,   0,   0, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 240,  30, 120,  60, 127, 252,  63, 248,
  15, 224,  27,  42,  99,  52,  55,  49,  69,  27,  40, 115,  54,  52,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,   0,  72,  28,
  24,  63,  56, 115, 240,  96, 224,   0,   0,   0,   0,   0,   0, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  30, 240,  30, 120, 126, 127, 254,  63, 238,  15, 142,  27,
  42,  99,  52,  55,  52,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   1,   0,  26,   0,  15,   0,  27,   0,  72,   7,  56,  15, 120,
  28, 224,  49, 128,   0,   0,   0,   0, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 240,  30, 120,  60, 127, 252,  63, 248,
  15, 224,  27,  42,  99,  52,  55,  53,  69,  27,  40, 115,  54,  52,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  23,   0,  15,   0,  24,   0,  72,   3,
 156,   7, 188,  15, 120,  28, 224,  49, 128,   0,   0,   0,   0, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  30, 240,  30, 120, 126, 127, 254,  63, 238,  15, 142,  27,
  42,  99,  52,  55,  54,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,
   0,   0,   0,   1,   0,  26,   0,  15,   0,  27,   0,  72,   3, 128,   7, 192,
  12,  96,  12,  96,  12,  96,   7, 192, 227, 142, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 240,  30, 120,  60, 127, 252,  63, 248,
  15, 224,  27,  42,  99,  52,  55,  55,  69,  27,  40, 115,  54,  54,  87,   4,
   0,  14,   1,   0,   0,   0,   2,   0,  24,   0,  15,   0,  25,   0,  72,   3,
 128,   7, 192,  12,  96,  12,  96,  12,  96,   7, 192,   3, 128,   0,   0, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  30, 240,  30, 120, 126, 127, 254,  63, 238,  15,
 142,  27,  42,  99,  52,  55,  56,  69,  27,  40, 115,  54,  54,  87,   4,   0,
  14,   1,   0,   0,   0,   1,   0,  24,   0,  15,   0,  25,   0,  72,  31, 240,
  31, 240,   0,   0,   0,   0, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 240,  30, 120,  60, 127, 252,  63, 248,  15, 224,
  27,  42,  99,  52,  55,  57,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  21,   0,  15,   0,  22,   0,  72,  31, 240,  31,
 240,   0,   0,   0,   0,   0,   0, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  30, 240,
  30, 120, 126, 127, 254,  63, 238,  15, 142,  27,  42,  99,  52,  56,  48,  69,
  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,   0,   0,   1,   0,  21,
   0,  15,   0,  28,   0,  72, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14,
 224,  14, 224,  14, 224,  14, 224,  14, 240,  30, 120,  60, 127, 252,  63, 248,
  15, 224,   1, 128,   3,   0,   6,   0,   6,  96,   6,  96,   3, 192,  27,  42,
  99,  52,  56,  49,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  15,   0,  16,   0,  22,   0,  72, 224,  14, 224,  14, 224,
  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,  14, 224,
  30, 240,  30, 120, 126, 127, 254,  63, 238,  15, 142,   0,  12,   0,  24,   0,
  48,   0,  51,   0,  51,   0,  30,  27,  42,  99,  52,  56,  50,  69,  27,  40,
 115,  55,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  28,   0,  14,
   0,  29,   0,  72,   0, 224,   1, 224,   3, 192,   7,   0,  12,   0,   0,   0,
   0,   0, 255, 248, 255, 248, 255, 248,   0,  56,   0, 120,   0, 112,   0, 240,
   1, 224,   1, 192,   3, 192,   7, 128,   7,   0,  15,   0,  30,   0,  28,   0,
  60,   0, 120,   0, 112,   0, 240,   0, 255, 252, 255, 252, 255, 252,  27,  42,
  99,  52,  56,  51,  69,  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,
   0,   0,   3,   0,  23,   0,  13,   0,  24,   0,  72,   1, 192,   3, 192,   7,
 128,  14,   0,  24,   0,   0,   0,   0,   0, 255, 240, 255, 240, 255, 240,   0,
 240,   1, 224,   3, 192,   3, 128,   7, 128,  15,   0,  30,   0,  28,   0,  60,
   0, 120,   0, 240,   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,  52,  56,
  52,  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,
   0,  26,   0,  14,   0,  27,   0,  72,   7,   0,   7,   0,   7,   0,   0,   0,
   0,   0, 255, 248, 255, 248, 255, 248,   0,  56,   0, 120,   0, 112,   0, 240,
   1, 224,   1, 192,   3, 192,   7, 128,   7,   0,  15,   0,  30,   0,  28,   0,
  60,   0, 120,   0, 112,   0, 240,   0, 255, 252, 255, 252, 255, 252,  27,  42,
  99,  52,  56,  53,  69,  27,  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,
   0,   0,   3,   0,  22,   0,  13,   0,  23,   0,  72,   7,   0,   7,   0,   7,
   0,   0,   0,   0,   0,   0,   0, 255, 240, 255, 240, 255, 240,   0, 240,   1,
 224,   3, 192,   3, 128,   7, 128,  15,   0,  30,   0,  28,   0,  60,   0, 120,
   0, 240,   0, 255, 248, 255, 248, 255, 248,  27,  42,  99,  52,  56,  54,  69,
  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  26,
   0,  12,   0,  27,   0,  72,  56,  48, 126, 112, 231, 224, 193, 192,   0,   0,
   0,   0,  63, 128,  63, 128,  63, 128,  14,   0,  14,   0,  14,   0,  14,   0,
  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,  14,   0,
  14,   0,  14,   0,  14,   0,  63, 128,  63, 128,  63, 128,  27,  42,  99,  52,
  56,  55,  69,  27,  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,
   3,   0,  22,   0,  12,   0,  23,   0,  72,  56,  48, 126, 112, 231, 224, 193,
 192,   0,   0,   0,   0,  63,   0,  63,   0,  63,   0,   7,   0,   7,   0,   7,
   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,   0,   7,
   0,   7,   0,   7,   0,   7,   0,  27,  42,  99,  49,  48,  51,  49,  69,  27,
  40, 115,  54,  50,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  22,   0,
  13,   0,  23,   0,  72, 112, 224,  57, 192,  31, 128,  15,   0,   0,   0,   0,
   0, 255, 240, 255, 240, 255, 240,   0, 240,   1, 224,   3, 192,   3, 128,   7,
 128,  15,   0,  30,   0,  28,   0,  60,   0, 120,   0, 240,   0, 255, 248, 255,
 248, 255, 248,  27,  42,  99,  49,  48,  54,  49,  69,  27,  40, 115,  55,  50,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  27,   0,  14,   0,  28,   0,
  72, 112,  56, 120, 120,  63, 240,  31, 224,   0,   0,   0,   0,   7, 192,  31,
 240,  63, 248, 120,  56, 112,  28, 240,  28, 224,   0, 224,   0, 224,   0, 224,
   0, 224,   0, 224, 252, 224, 252, 224, 252, 224,  28, 224,  28, 224,  28, 240,
  28, 120,  28, 127, 252,  63, 252,  15, 252,  27,  42,  99,  49,  48,  54,  50,
  69,  27,  40, 115,  55,  48,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,
  21,   0,  14,   0,  27,   0,  72, 112,  56, 120, 120,  63, 240,  31, 224,   0,
   0,   0,   0,  15, 156,  63, 252, 127, 252, 120, 124, 240,  60, 224,  28, 224,
  28, 224,  28, 224,  28, 240,  60, 120, 124, 127, 252,  63, 252,  15, 156,   0,
  28,   0,  28, 224,  60, 240, 120, 255, 248, 127, 240,  31, 192,  27,  42,  99,
  49,  48,  54,  51,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  21,   0,  14,   0,  28,   0,  72,  15, 192,  63, 240, 127,
 248, 120, 120, 224,  28, 224,  28, 224,   0, 240,   0, 124,   0, 127,   0,  63,
 192,  15, 240,   3, 248,   0, 120,   0,  60,   0,  28, 224,  28, 240,  28, 120,
 120, 127, 248,  63, 240,  15, 192,   3,   0,   1, 128,   0, 192,  12, 192,  12,
 192,   7, 128,  27,  42,  99,  49,  48,  54,  52,  69,  27,  40, 115,  54,  50,
  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  16,   0,  14,   0,  23,   0,
  72,  15, 192,  63, 240, 127, 248, 240,  60, 224,  28, 224,   0, 248,   0, 127,
 128,  63, 240,   7, 248,   0, 124,   0,  28, 224,  28, 240,  56, 127, 248,  63,
 240,  15, 192,   3,   0,   1, 128,   0, 192,  12, 192,  12, 192,   7, 128,  27,
  42,  99,  49,  48,  54,  53,  69,  27,  40, 115,  52,  51,  87,   4,   0,  14,
   1,   0,   0,   0,   5,   0,  26,   0,   7,   0,  27,   0,  72,  56,  56,  56,
   0,   0, 254, 254, 254,  56,  56,  56,  56,  56,  56,  56,  56,  56,  56,  56,
  56,  56,  56,  56,  56, 254, 254, 254,  27,  42,  99,  49,  48,  54,  57,  69,
  27,  40, 115,  54,  52,  87,   4,   0,  14,   1,   0,   0,   0,   2,   0,  23,
   0,  15,   0,  24,   0,  72,   1, 224,   7, 248,  15, 252,  30,  28,  28,  14,
  60,  14,  56,   0,  56,   0, 255, 240, 255, 240, 255, 240,  56,   0,  56,   0,
 255, 224, 255, 224, 255, 224,  56,   0,  56,   0,  60,  14,  28,  14,  30,  28,
  15, 252,   7, 248,   1, 224,  27,  42,  99,  49,  48,  56,  52,  69,  27,  40,
 115,  50,  48,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  21,   0,  12,
   0,   2,   0,  72, 255, 240, 255, 240,  27,  42,  99,  49,  48,  56,  54,  69,
  27,  40, 115,  50,  52,  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  23,
   0,  12,   0,   4,   0,  72, 224, 112, 240, 240, 127, 224,  63, 192,  27,  42,
  99,  49,  48,  56,  56,  69,  27,  40, 115,  49,  57,  87,   4,   0,  14,   1,
   0,   0,   0,   8,   0,  22,   0,   3,   0,   3,   0,  72, 224, 224, 224,  27,
  42,  99,  49,  48,  57,  48,  69,  27,  40, 115,  53,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   2,   0,  16,   0,  15,   0,  17,   0,  72,  60, 120, 126,
 252, 247, 222, 227, 142, 227, 142, 227, 142, 227, 142, 227, 254, 227, 254, 227,
 128, 227, 128, 227, 128, 227, 142, 227, 142, 247, 222, 126, 252,  60, 120,  27,
  42,  99,  49,  48,  57,  49,  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,
   1,   0,   0,   0,   1,   0,  21,   0,  16,   0,  22,   0,  72,  30, 255, 127,
 255, 127, 255, 241, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,
 254, 224, 254, 224, 254, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224, 224,
 224, 241, 224, 127, 255, 127, 255,  30, 255,  27,  42,  99,  49,  48,  57,  53,
  69,  27,  40, 115,  54,  48,  87,   4,   0,  14,   1,   0,   0,   0,   0,   0,
  21,   0,  15,   0,  22,   0,  72,  28,   0,  28,   0,  28,   0,  28,   0,  28,
 128,  29, 128,  31, 128,  31,   0,  30,   0,  28,   0,  60,   0, 124,   0, 252,
   0, 220,   0, 156,   0,  28,   0,  28,   0,  28,   0,  28,   0,  31, 254,  31,
 254,  31, 254,  27,  42,  99,  49,  48,  57,  54,  69,  27,  40, 115,  54,  52,
  87,   4,   0,  14,   1,   0,   0,   0,   3,   0,  23,   0,  10,   0,  24,   0,
  72,  28,   0,  28,   0,  28,   0,  28,   0,  28,   0,  28, 128,  29, 128,  31,
 128,  31,   0,  30,   0,  28,   0,  60,   0, 124,   0, 252,   0, 220,   0, 156,
   0,  28,   0,  28,   0,  28,   0,  28,   0,  30,   0,  15, 192,  15, 192,   3,
 192,  27,  42,  99,  49,  48,  57,  55,  69,  27,  40, 115,  50,  54,  87,   4,
   0,  14,   1,   0,   0,   0,   4,   0,  23,   0,  12,   0,   5,   0,  72,  14,
 112,  30, 240,  61, 224, 115, 128, 198,   0,  27,  42,  99,  49,  48,  57,  56,
  69,  27,  40, 115,  50,  50,  87,   4,   0,  14,   1,   0,   0,   0,   7, 255,
 255,   0,   6,   0,   6,   0,  72,  48,  96, 192, 204, 204, 120,  27,  42,  99,
  49,  49,  48,  54,  69,  27,  40, 115,  55,  50,  87,   4,   0,  14,   1,   0,
   0,   0,   2,   0,  27,   0,  14,   0,  28,   0,  72,  56, 112,  28, 224,  15,
 192,   7, 128,   0,   0,   0,   0, 255, 248, 255, 248, 255, 248,   0,  56,   0,
 120,   0, 112,   0, 240,   1, 224,   1, 192,   3, 192,   7, 128,   7,   0,  15,
   0,  30,   0,  28,   0,  60,   0, 120,   0, 112,   0, 240,   0, 255, 252, 255,
 252, 255, 252
};

/* -------------------------------------------------------------------------- */
/* The HQ3 glyph updates for Brush, Dom Casual, Revue Shadow and Park Avenue.

   This is about 10K, and is generated by ixmak.exe from hq3updt.4lj and
   hq3updt.lj4, depending on byte order.

   The ROM array must be on a word boundary to work on ARM
   http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.faqs/ka11144.html
*/
unsigned int hq3updt_lj4[] = {
#if (BYTEORDER == HILO)
  /* Motorola byte order */
4U, 4294877163U, 7392U, 68U,
1189890U, 4294876970U, 82U, 68U,
1976325U, 4294876947U, 2408U, 68U,
4335620U, 4294875167U, 8370U, 68U,
1189890U, 5570561U, 32U, 2U,
32U, 3342335U, 0U, 0U,
0U, 90133U, 7392U, 1179649U,
1624637440U, 5373982U, 90349U, 2408U,
4325377U, 1742798848U, 548536338U, 4294967295U,
0U, 0U, 7340552U, 104529920U,
41420374U, 1115U, 109772800U, 94109695U,
2408U, 655360U, 34078820U, 78U,
2490472U, 116U, 1179754U, 134U,
655465U, 144U, 4325483U, 210U,
7209069U, 320U, 4325486U, 386U,
8847359U, 0U, 1819U, 1907792734U,
1495728130U, 896535160U, 65537U, 65536U,
16777217U, 256U, 13762918U, 33652532U,
507258890U, 336857620U, 6815744U, 1183876U,
2296362U, 205390U, 2293866U, 10U,
13762560U, 6881280U, 4325376U, 8782U,
117248132U, 575540132U, 352646610U, 3706585088U,
0U, 0U, 5732U, 4253030328U,
221844184U, 519766616U, 0U, 4180476353U,
49291461U, 117244011U, 110U, 82546U,
1970497568U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
1114797427U, 1746935840U, 538976288U, 538976288U,
538976288U, 1114598500U, 538976288U, 538976288U,
538976288U, 538976288U, 90326U, 507258890U,
336857620U, 7143424U, 4325379U, 23123456U,
675702272U, 0U, 744951444U, 728175236U,
1022296849U, 899025511U, 195302660U, 2105144U,
2105144U, 0U, 131072000U, 3385U,
166463980U, 157286510U, 134U, 84048U,
538976288U, 538976288U, 538976288U, 538976288U,
538968065U, 1112681802U, 760229173U, 758132781U,
808463664U, 758197553U, 825045044U, 825374765U,
825240881U, 758132034U, 1920299880U, 538976288U,
538976288U, 538976301U, 809054259U, 842411314U,
859124768U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 90326U, 2179171344U,
50921473U, 201326592U, 84411393U, 117637126U,
38536196U, 208408104U, 101647109U, 17235970U,
395527U, 134481153U, 51055180U, 787219471U,
252651268U, 117703680U, 4079764U, 251616257U,
243991162U, 2130808732U, 40620549U, 2549168501U,
35655167U, 3221585772U, 54289151U, 3174234422U,
901643548U, 5188097U, 2272493939U, 3791886418U,
617403138U, 4037279880U, 1345676290U, 4110950654U,
3287288856U, 3222146976U, 189119505U, 3268443972U,
3225978048U, 784875567U, 1359225173U, 2163590752U,
3817075484U, 453927929U, 2398616335U, 269373364U,
757072918U, 12081922U, 81336585U, 889293922U,
2302787975U, 267146767U, 748343140U, 1694528580U,
13919040U, 1817513672U, 2483973200U, 2393117709U,
1272515675U, 2602229359U, 296341122U, 439310833U,
2909663770U, 2311358680U, 3971016177U, 4276995842U,
33681917U, 4059952664U, 167833344U, 4261119753U,
266349580U, 116982008U, 4261276597U, 656343304U,
4076208650U, 419628300U, 245109939U, 4059689205U,
1979119613U, 4261478671U, 300219641U, 3909682416U,
3020752133U, 4126604231U, 821759234U, 4294834690U,
3633917972U, 201398257U, 1109254U, 84674772U,
316867881U, 4110096512U, 3734080797U, 1078716672U,
3152609787U, 4028184449U, 1884586572U, 679661596U,
2312123607U, 26424175U, 955753984U, 1478103052U,
3306553409U, 1358725720U, 4232069362U, 4282428469U,
4054197757U, 451896817U, 2858255898U, 3688259824U,
2036398842U, 3774808051U, 385492209U, 4095370505U,
33686789U, 3674538473U, 4227530751U, 4261149954U,
3690022014U, 50790660U, 184028431U, 4227790721U,
1076496899U, 150995212U, 4U, 16779016U,
50725381U, 206308356U, 1814564358U, 251855873U,
134218754U, 134874624U, 17368837U, 51072046U,
3959688975U, 254019335U, 67502080U, 826338328U,
3932717264U, 2641360934U, 264246670U, 3559075239U,
3610051540U, 231459626U, 660999007U, 1611383641U,
165587244U, 3943288170U, 3845127038U, 551239425U,
1263534343U, 3842323421U, 3814719279U, 2937671622U,
3758108588U, 10996352U, 3488088260U, 184818829U,
27023360U, 807546856U, 1660968063U, 3770199813U,
2152202808U, 1275189370U, 17594176U, 608112592U,
923743265U, 244320748U, 2617647300U, 1243715780U,
540021539U, 890773744U, 1359156564U, 1077703439U,
3696299781U, 86659312U, 3991209715U, 4009557754U,
117244676U, 66657790U, 4211737090U, 118171139U,
50662908U, 118632441U, 149870594U, 4191548929U,
4194171135U, 4260363767U, 4077321223U, 67184371U,
83167616U, 395867624U, 198414090U, 656935310U,
3775568584U, 1288642809U, 1233129555U, 3705688535U,
3234999252U, 4246877662U, 4028688648U, 642768515U,
3444449800U, 335544602U, 1U, 328450U,
134809348U, 101782029U, 202248207U, 185797132U,
202132484U, 72093224U, 256649220U, 70002703U,
740822536U, 16778499U, 152112651U, 1053713U,
387519232U, 353501442U, 370672390U, 135138308U,
84544271U, 151457100U, 778833416U, 1330382927U,
72090629U, 72101380U, 3961982735U, 117704452U,
67108907U, 1088067195U, 1392518037U, 12036897U,
1541833387U, 3808524320U, 1093159425U, 2964134046U,
578948998U, 2206956081U, 620355864U, 4138700837U,
2409638481U, 4063695362U, 1087865387U, 227688216U,
572344819U, 2355781890U, 347480964U, 4196467821U,
566791520U, 269099024U, 1225774208U, 256396558U,
3427897160U, 172914656U, 199422778U, 3259716015U,
1158430184U, 1643897264U, 1561136623U, 437087956U,
770133805U, 3474931069U, 2706531031U, 789642745U,
167642370U, 16123435U, 251791849U, 50335818U,
66898970U, 4193843198U, 16575493U, 1310333689U,
486672422U, 1879834520U, 759176620U, 3504357672U,
3121603620U, 260581902U, 4028010044U, 185333706U,
195002138U, 2439274631U, 2804003029U, 3893155450U,
1333215488U, 2068054415U, 436297666U, 53995527U,
3290358084U, 151978730U, 1338184192U, 1930428571U,
3028376528U, 18090498U, 3630432924U, 2298523744U,
801138944U, 3581468740U, 1913109589U, 23867649U,
4236509640U, 3460318287U, 262685199U, 807734660U,
334258165U, 40972824U, 2686554158U, 2471759436U,
2432256016U, 402850062U, 84544204U, 233043703U,
4143839970U, 3639803130U, 4227599346U, 135065130U,
4261218053U, 84024345U, 4011069449U, 452790014U,
51317235U, 402198523U, 501939196U, 4157738727U,
66387700U, 4261655287U, 4210228484U, 67372083U,
4045015788U, 268459307U, 1776291387U, 3842657088U,
176624650U, 1524107842U, 2485948162U, 237187616U,
199442511U, 4043470834U, 74979781U, 1144324729U,
839065926U, 87673056U, 921570255U, 3590314504U,
3323589632U, 3120179448U, 299554815U, 4277926196U,
4077696333U, 3224409043U, 2685173519U, 1314587392U,
1201668275U, 2969672601U, 49782663U, 4290320727U,
2267053072U, 5100080U, 626148363U, 2430662922U,
3773777808U, 3645440207U, 3318795778U, 3265135438U,
3800128356U, 1996931141U, 422841056U, 1540042802U,
2356154824U, 2751958064U, 4149096683U, 177265608U,
1355286216U, 2672021644U, 1033935226U, 2965157840U,
3451117810U, 2041903736U, 1074018704U, 154894357U,
4216852927U, 2454569168U, 973013241U, 2219894340U,
1927201872U, 1250488398U, 83385986U, 4898256U,
565916679U, 4215338425U, 1893185056U, 2310127520U,
3340242259U, 2987266874U, 1329025152U, 2270386494U,
2213646887U, 2801970090U, 1084463119U, 3888975671U,
2634896231U, 452161297U, 2913443725U, 3422227704U,
470715452U, 261367567U, 4027842700U, 1040255064U,
4024630523U, 4227787531U, 51971852U, 117901317U,
122289690U, 50726665U, 67896582U, 685966325U,
4261149950U, 66249213U, 405859078U, 235341836U,
166978852U, 4076230126U, 403243524U, 285490687U,
4143972351U, 4109956598U, 4244831992U, 4041534466U,
3993372159U, 84280073U, 4060148221U, 4160883458U,
118426370U, 252051969U, 4261409535U, 85063965U,
50857993U, 50855389U, 21231897U, 337117193U,
16385768U, 4211079929U, 16774643U, 2468U,
36438017U, 3024U, 2490368U, 215220272U,
3689U, 11272192U, 280690861U, 4998U,
111411200U, 369559229U, 6082U, 118423552U,
437846015U, 7392U, 3932160U, 36438116U,
78U, 3539048U, 132U, 2490474U,
170U, 655465U, 180U, 4325483U,
246U, 7209069U, 356U, 4325486U,
422U, 8847359U, 0U, 38161U,
1367497275U, 409403397U, 509875460U, 733358930U,
175636484U, 131841U, 16777476U, 131322U,
1U, 131622U, 1U, 9896294U,
33980212U, 507911690U, 168430100U, 6815744U,
2492807U, 2301241U, 2302542U, 2303843U,
2302867U, 2298151U, 2298151U, 2304005U,
2293866U, 10U, 9895936U, 6881280U,
4325376U, 8782U, 127930175U, 479595428U,
352646789U, 3609526272U, 0U, 0U,
5911U, 4155971116U, 162992240U, 492896850U,
0U, 4180476353U, 6105737U, 127926379U,
110U, 86113U, 1919623233U, 1986358901U,
1696604192U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 1348563563U, 541161061U,
1853187360U, 538976288U, 538976288U, 1315926637U,
1634476064U, 538976288U, 538976288U, 538976288U,
90349U, 507911690U, 168430100U, 7143424U,
4325385U, 23129344U, 675767808U, 0U,
908856321U, 739970235U, 1022296849U, 899025511U,
195302660U, 2105144U, 2105144U, 0U,
131072000U, 2487U, 166463980U, 157286510U,
134U, 84048U, 538976288U, 538976288U,
538976288U, 538976288U, 538968065U, 1347824969U,
760229173U, 758132781U, 808463664U, 758197552U,
942485556U, 825570605U, 809053489U, 758132048U,
1634888480U, 1098278254U, 1969561632U, 538976301U,
809054259U, 876162354U, 858796064U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
90349U, 2164557577U, 51118338U, 234881024U,
218631687U, 185204739U, 84148737U, 1275996175U,
738610248U, 117845508U, 721935U, 201917954U,
17563659U, 65548U, 17630215U, 151522054U,
34210565U, 3962457158U, 67372548U, 103745286U,
84344832U, 784335180U, 525673255U, 4051923956U,
822485524U, 4292882754U, 2420459984U, 1150184983U,
2250171910U, 3269753728U, 3841736911U, 3157310718U,
3943654704U, 3965652479U, 3043656072U, 1108809861U,
707534879U, 1264407921U, 1163269839U, 2937111989U,
3841070367U, 830689818U, 20779970U, 3652578568U,
839742273U, 214879245U, 782310304U, 3670993385U,
4217261936U, 3517378928U, 1863211957U, 3578983087U,
1404863911U, 261852942U, 1181555619U, 1601192447U,
4194762496U, 320018158U, 4277984739U, 200281347U,
4226948347U, 4143185149U, 4194050837U, 2869493433U,
3793481035U, 132757293U, 2174040349U, 67698689U,
251658240U, 134285058U, 67437318U, 7079942U,
103548460U, 67896328U, 67305474U, 234946571U,
117768973U, 33950216U, 67897644U, 672132143U,
1191446276U, 117780228U, 117702656U, 1136673172U,
4158331589U, 3007367958U, 888066561U, 2234648345U,
665768344U, 3931707807U, 2510055491U, 1617361808U,
720998421U, 1965819378U, 2700124465U, 3158684955U,
2983674298U, 4250540992U, 3443849698U, 3514953148U,
48900609U, 3278668307U, 3271667753U, 1099454489U,
1723286416U, 850208153U, 3609362639U, 960049782U,
827347017U, 1544654003U, 1448626219U, 1828540639U,
2915477019U, 3084010983U, 874679565U, 246484150U,
3441105497U, 3017833241U, 1242294986U, 3430361870U,
3902607085U, 2835376553U, 406235857U, 2242064696U,
1454120324U, 866589636U, 639679377U, 497752221U,
443939025U, 3563464880U, 140665610U, 1450445670U,
2148291151U, 1325307137U, 46072766U, 1828856677U,
55277828U, 260900083U, 2647913966U, 3521703172U,
4278654723U, 85327132U, 3807574770U, 3959358201U,
30208277U, 704966448U, 3909158122U, 687075833U,
4211091481U, 556466924U, 4195020777U, 4059097102U,
3858694962U, 451348U, 421661970U, 18092368U,
3865611502U, 3975021303U, 4042652845U, 3724162288U,
2216087293U, 48112154U, 3942581269U, 1379207203U,
490212117U, 4076080635U, 233375997U, 4202844750U,
570821888U, 18350080U, 788233U, 168299270U,
197121U, 67447812U, 738601988U, 738470919U,
1275593734U, 201918470U, 50397700U, 100664577U,
167905287U, 51054871U, 336401932U, 235868944U,
353440332U, 686690085U, 67372549U, 88998918U,
3961784068U, 117900623U, 68092928U, 6733889U,
501602062U, 3163246293U, 2365622747U, 14771261U,
3808574194U, 92799503U, 417694074U, 1092336288U,
1891438540U, 2353541546U, 126949927U, 1705094774U,
2248820420U, 453696324U, 3869884431U, 658081301U,
3457752539U, 4232034208U, 3968721847U, 4200083356U,
1160648093U, 1540366672U, 479690809U, 1380030722U,
1813581200U, 2107185355U, 60018435U, 121618658U,
2400172747U, 466737202U, 4008227771U, 1830624176U,
3284516573U, 2969625097U, 33965570U, 3337126343U,
3271654438U, 34638403U, 1231625724U, 3518268385U,
2199877410U, 625612976U, 2903526110U, 1614532924U,
394457107U, 3171945721U, 2483156768U, 4062237433U,
1994636365U, 2937998673U, 3227893445U, 2484623305U,
270611248U, 393938953U, 4284484609U, 806307088U,
344592438U, 3705934088U, 938303009U, 2518611730U,
1549895738U, 3426807206U, 3483966184U, 1920741146U,
3090067989U, 799774299U, 2816328171U, 840913214U,
2229380066U, 4017655494U, 2773545930U, 1157435153U,
2190702654U, 2267166885U, 2181223695U, 3107393221U,
68085848U, 2391024397U, 4091087398U, 4148904634U,
456120641U, 2963547197U, 4087464033U, 3072686321U,
4059496718U, 572324860U, 4109310674U, 3874223295U,
4025017058U, 4025079377U, 4245949188U, 100136944U,
4176807930U, 50461166U, 50334202U, 134288096U,
4124570622U, 4261610749U, 67032333U, 252189176U,
4076792847U, 67831045U, 217971728U, 3454412567U,
3757249825U, 285614876U, 419569139U, 5106158U,
122177527U, 4212913470U, 219295488U, 16777768U,
4160090101U, 4159952882U, 286330639U, 52643211U,
4025025788U, 4277993463U, 134189304U, 12594355U,
684330471U, 101253641U, 202442545U, 3316925570U,
3412272900U, 369492245U, 26U, 404298529U,
386469388U, 218827267U, 522191874U, 504566802U,
296486924U, 100926540U, 122160686U, 74188804U,
124520494U, 772016151U, 35528964U, 537728017U,
153164546U, 67371522U, 35000085U, 19145488U,
117841160U, 621093376U, 470030857U, 50463233U,
286261267U, 33818371U, 336269836U, 252184845U,
168103430U, 1275629062U, 67431972U, 101057542U,
72091471U, 671559438U, 117440596U, 1075470510U,
3770026728U, 2466279845U, 1473060202U, 4059730342U,
1458183397U, 297370033U, 1572163306U, 3540076270U,
83287321U, 1005758870U, 3082623327U, 2215744624U,
3189319059U, 1484216657U, 2405073432U, 3476266515U,
3435880226U, 967014783U, 2621935541U, 3833784612U,
331118050U, 465559120U, 3588403411U, 3995918266U,
1932280839U, 202113652U, 319290626U, 3641285598U,
3892834494U, 3039870841U, 279526585U, 2802312178U,
1984648947U, 33368088U, 1797747077U, 2055528677U,
95805881U, 2476204057U, 1090859411U, 2163918172U,
3622792285U, 180857610U, 265194231U, 4111086605U,
3668542395U, 1613895735U, 2725253136U, 4028586913U,
2298668356U, 2312078184U, 2625977368U, 429915104U,
997766166U, 3023106996U, 1894020204U, 3585323073U,
26235526U, 2150337428U, 361914578U, 3586170953U,
3498706024U, 2709551527U, 1877219899U, 1967235670U,
840607320U, 3517785224U, 3456627460U, 3286691820U,
3924294149U, 34147343U, 334498294U, 536282586U,
3840999456U, 85454612U, 805505225U, 3373468678U,
4127260408U, 4076075684U, 1006704143U, 68554779U,
4279642911U, 23634643U, 3907839760U, 51188750U,
522127615U, 4025413872U, 4042388466U, 4205380049U,
301858506U, 3740530429U, 4180785717U, 4159499738U,
4043112189U, 4261871339U, 101258243U, 236128275U,
4195680758U, 3909096970U, 134284560U, 571936809U,
302019423U, 954185203U, 2194981354U, 2587890134U,
2032050973U, 556913265U, 2870376730U, 4259226864U,
827329206U, 3760257105U, 2488751769U, 2675806621U,
2153519449U, 2389283465U, 3524841250U, 3493781232U,
3066755022U, 4059429072U, 235079913U, 168291369U,
252641800U, 33025270U, 3924476401U, 4173926528U,
1294835690U, 452885361U, 2900310554U, 3622404518U,
748821285U, 3513573357U, 1801500199U, 601826559U,
3942233340U, 3143649816U, 2084122673U, 500272170U,
2181372260U, 3355178730U, 3944026348U, 4261137150U,
319891203U, 845210912U, 181961345U, 2468546328U,
33626112U, 6933U, 370415379U, 286199821U,
151718922U, 33883908U, 100859930U, 18418732U,
122423310U, 1275595884U, 137235972U, 1279788846U,
100944904U, 788795663U, 33948162U, 403836424U,
119215380U, 286065152U, 33625623U, 285217811U,
51514882U, 118099983U, 185338118U, 353175556U,
86788143U, 755255343U, 3976481862U, 67372550U,
69750532U, 101582639U, 117440616U, 3225522656U,
1661809416U, 198587675U, 2119701948U, 3578402929U,
3368130229U, 1486708449U, 1294064437U, 366156120U,
342987097U, 2378410199U, 779535118U, 245304782U,
3670881098U, 846188666U, 1089480065U, 2152187841U,
2444334563U, 480505524U, 2232111936U, 203132698U,
2977704585U, 4293861932U, 3043962917U, 1403094954U,
3285742006U, 2606651687U, 805247292U, 2172667330U,
3784310988U, 3312239087U, 1011747951U, 3389672399U,
2284314906U, 2860723892U, 37445704U, 406561505U,
2292424280U, 2317656177U, 2590611304U, 2249794677U,
4095947008U, 4160245983U, 850187507U, 4342304U,
1292341299U, 1309938127U, 2431749344U, 3620325794U,
3368884783U, 2000006302U, 2082148564U, 2631871899U,
1704267025U, 2249916454U, 3249230208U, 2593523665U,
2742503194U, 2145427631U, 2722479141U, 2231393993U,
407131231U, 2242183928U, 3316318450U, 2442325012U,
3854361321U, 21087235U, 2172257981U, 1184437639U,
2174222143U, 420176673U, 2379559704U, 1750566274U,
896015318U, 3614029981U, 3048584081U, 2952641053U,
2999022041U, 2726798362U, 3414340012U, 2026965225U,
3545995316U, 25406287U, 409478204U, 999989473U,
98887371U, 3320639754U, 131729136U, 603914500U,
166652775U, 453047556U, 15657947U, 868377874U,
4059952826U, 421661193U, 219285512U, 319687259U,
4026199815U, 4045868297U, 182381539U, 4227004644U,
3991924983U, 873732368U, 303371049U, 1007809518U,
3905872637U, 3570866404U, 4225946819U, 3203723995U,
180820989U, 4243978737U, 3992270039U, 3355114215U,
4076209143U, 4032691744U, 891037196U, 201330175U,
4075619844U, 170599215U, 672409611U, 538914848U,
303561764U, 808061193U, 404564758U, 268959998U,
4176794090U, 4142389480U, 4011965313U, 3139899651U,
251789586U, 15U, 152045581U, 185336326U,
50401797U, 281614U, 672091144U, 208422927U,
788934148U, 101846285U, 100794897U, 235538179U,
235733506U, 983569U, 117968129U, 51119109U,
269355275U, 101379086U, 1283851268U, 67503951U,
67372550U, 1208945934U, 16862144U, 659343060U,
3781924356U, 2423391392U, 830675973U, 827722297U,
2141294976U, 2080767259U, 3268735982U, 141560751U,
1645913082U, 867033116U, 154666714U, 2499625040U,
473009258U, 2986494284U, 3632543371U, 1612460412U,
4028366976U, 423805960U, 67173059U, 552223456U,
3397443348U, 766906321U, 2226373144U, 1731305518U,
2506127194U, 3159213168U, 880607266U, 2013531506U,
1089103188U, 4033531915U, 3024100024U, 2552053184U,
394380296U, 3274899209U, 4026812448U, 391327774U,
362022603U, 1357745936U, 3406580877U, 1196535591U,
2484793706U, 3517664321U, 233744093U, 455217115U,
1627205884U, 1625145056U, 3980199161U, 4227859836U,
3759242672U, 629116689U, 2444624322U, 3911586747U,
3448778527U, 119245396U, 2899749600U, 3346530515U,
4159710443U, 3976008944U, 3413205547U, 940709101U,
184748037U, 53282809U, 3958568709U, 118234606U,
4109827590U, 100465436U, 118689291U, 65200878U,
167116282U, 4227784723U, 519173405U, 319160800U,
4007975665U, 17582595U, 597491450U, 4228056578U,
1043722538U, 286002194U, 1121911548U, 4160421371U,
4249681154U, 186457105U, 1021212420U, 84082427U,
4278335481U, 4264329814U, 655427356U, 33693184U,
8705U, 35724572U, 302977299U, 235932683U,
353765639U, 421135646U, 69148675U, 2321416U,
101067782U, 2351435527U, 68045896U, 252594730U,
202255364U, 1812464647U, 738661125U, 487457796U,
454563604U, 218761222U, 67314205U, 83890700U,
67240732U, 234949899U, 1772039U, 486671130U,
386275347U, 302582799U, 353960982U, 84415505U,
273436232U, 105787279U, 103678980U, 67628806U,
2802255620U, 67587844U, 67568644U, 120522496U,
6733835U, 2426657051U, 3820048635U, 1490222981U,
2478832143U, 1613026049U, 3319791848U, 1258713447U,
90755718U, 813892043U, 3644892068U, 2684856079U,
514997561U, 836673977U, 1021447810U, 2150516235U,
3688027344U, 321679395U, 926427964U, 3762881808U,
700895632U, 2185618325U, 790349149U, 2631917611U,
1387884879U, 385119615U, 379565345U, 250359979U,
1360687432U, 1995584799U, 369472774U, 2968745576U,
3589423313U, 3134751548U, 1889419824U, 2974297312U,
1218427157U, 2500159421U, 2567404833U, 2505610905U,
295750698U, 3505082475U, 1905567244U, 185501055U,
4216396513U, 2767238432U, 2792390678U, 197848856U,
1102392210U, 635109850U, 907616285U, 3078741456U,
601788451U, 405808725U, 1813580398U, 3955618013U,
2020649506U, 1087514864U, 3550786489U, 2507722106U,
1276849744U, 3500910528U, 2838505699U, 1042344002U,
2096498688U, 105771531U, 2387611730U, 2172936756U,
1030718016U, 2886916912U, 4044300512U, 2280180163U,
1083046176U, 2918531490U, 3843100018U, 3692013443U,
1338395701U, 3940549803U, 1845933184U, 4244698098U,
4075759883U, 17957629U, 3857575937U, 3976781294U,
419368458U, 318627603U, 973876989U, 4159764736U,
3876450016U, 150853896U, 117833471U, 4210620927U,
4279766796U, 185600701U, 4026400000U, 468925681U,
234498313U, 271191546U, 3002793459U, 3925785764U,
1442181370U, 4093179878U, 4109688764U, 4003655176U,
3307988455U, 772059142U, 3690327288U, 4176474880U,
20578823U, 203295242U, 84081870U, 2194146600U,
119406851U, 452984832U, 34144515U, 526084U,
488512543U, 100997920U, 555357970U, 403772692U,
168627982U, 202075144U, 795804680U, 67578884U,
740830468U, 740830726U, 1275996175U, 738610248U,
117845508U, 169149465U, 370348036U, 33620509U,
471537162U, 352452609U, 353899264U, 387186948U,
436404758U, 419762438U, 302582798U, 219155207U,
252185196U, 684592687U, 1275397636U, 67372808U,
3962457158U, 67372548U, 103745286U, 84345089U,
574634828U, 1406509275U, 2013633589U, 2200437785U,
1199310954U, 1014000645U, 3583670684U, 2257402629U,
971774074U, 2795512476U, 864541665U, 31564290U,
1568211885U, 2529482038U, 3994583051U, 320671039U,
1795852970U, 202013211U, 2253959610U, 1435450250U,
3274759972U, 2563745476U, 3283032207U, 4222663168U,
4088150114U, 2364803080U, 1605962343U, 1681705205U,
1489963333U, 117579015U, 4195337929U, 367327489U,
3758164463U, 1607806698U, 4262726912U, 300737767U,
601811281U, 687879121U, 3709136915U, 598142459U,
3455516161U, 285806382U, 1081305375U, 1428367345U,
2205414450U, 3712095487U, 3761324688U, 1162465348U,
2389579654U, 518391490U, 3833299172U, 4232105916U,
817692395U, 752300739U, 2133553589U, 1786873922U,
387482922U, 740302667U, 1565881116U, 1981206447U,
775402980U, 4061470513U, 2203195905U, 1024705241U,
3052210226U, 225657100U, 3469479214U, 2702942426U,
3471436283U, 1582265000U, 3337711727U, 734901717U,
1391636307U, 3162875663U, 2609843782U, 1830527839U,
1883373562U, 117374995U, 320270078U, 4242400011U,
4027384827U, 4061985782U, 4093443577U, 4230157739U,
150911458U, 468536071U, 3921095977U, 3231685862U,
3709415664U, 3918712748U, 433228705U, 2615289880U,
3785827716U, 176561062U, 2930880306U, 1176118167U,
3114293342U, 498508465U, 2178369834U, 454891938U,
2015473310U, 944635505U, 16164120U, 1784528949U,
2783069857U, 1042379119U, 2228426030U, 3833928575U,
548461524U, 1961597312U, 668537234U, 4188020098U,
3264807389U, 220887430U, 3289456190U, 849006997U,
2656240962U, 509100119U, 2176439544U, 170822042U,
2583134397U, 1085322435U, 3705226128U, 988015137U,
740305704U, 538189787U, 902292463U, 4110820085U,
3956979975U, 50014990U, 337522446U, 1457385498U,
3756904410U, 4143061790U, 689380364U, 1190926874U,
48870675U, 51312668U, 570766632U, 404220376U,
3875470582U, 7410U, 33556135U, 7922U,
4294901760U, 548470802U, 512U, 6553600U,
5111846U, 6815744U, 7602186U, 6946816U,
8257546U, 6881280U, 8912962U, 7012352U,
13238382U, 7143424U, 20447298U, 7208960U,
24772742U, 4294901760U, 0U, 1359069256U,
2227187400U, 144752U, 175636481U, 65537U,
53411840U, 66426U, 210U, 23462317U,
2134122054U, 503976980U, 169082984U, 10U,
159842339U, 6946816U, 655570U, 105U,
66U, 0U, 575538941U, 245110657U,
195302660U, 4167425757U, 655363U, 524295U,
393226U, 418905967U, 436146123U, 405610553U,
46989637U, 21362989U, 29490928U, 184289021U,
7012352U, 7208961U, 1148153120U, 1130460021U,
1634476064U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538985583U, 1830830945U,
1937072492U, 538976288U, 538976288U, 538988143U,
1919770988U, 538976288U, 538976288U, 538976288U,
538968065U, 1611996742U, 503976980U, 169082989U,
66U, 65888U, 352331843U, 1711276032U,
5790U, 177614327U, 343227631U, 51459478U,
174525348U, 352583712U, 523763744U, 523763712U,
2000U, 0U, 264964588U, 166463840U,
7208960U, 8781825U, 1213210656U, 538976288U,
538976288U, 538976288U, 538976288U, 83011U,
760360272U, 758459696U, 808267824U, 758132016U,
758134829U, 808726837U, 925708337U, 758132016U,
759459693U, 541286771U, 1969318944U, 538976288U,
539832377U, 808530739U, 758132784U, 807411744U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
538968065U, 1612022207U, 2265514762U, 68864U,
1797U, 100860420U, 134285568U, 1275595884U,
1208880655U, 252445447U, 50331660U, 167903241U,
50595847U, 184616198U, 3960351812U, 100926470U,
788793128U, 234881104U, 3234335861U, 2487411772U,
16551425U, 1700927572U, 890340298U, 1996557559U,
12363329U, 961118552U, 889316375U, 590617251U,
3556311740U, 654350380U, 1613835023U, 3509587872U,
747581473U, 824261393U, 2735536150U, 261887247U,
3759882272U, 1243113267U, 294107818U, 1107616513U,
2557575488U, 520129529U, 10478080U, 1742102539U,
4010703726U, 230365007U, 2401960186U, 3640709048U,
2168449795U, 1613299972U, 637619348U, 409182848U,
484021256U, 1314977416U, 3504212448U, 3963855018U,
2278076995U, 2431352656U, 3724084385U, 1709229353U,
28726531U, 462539860U, 3991425267U, 4140361598U,
1118469888U, 3787170035U, 3054697911U, 1140962768U,
196735515U, 4264567230U, 2869711360U, 3153215555U,
2937002768U, 248968220U, 2928769440U, 4206359280U,
3811005700U, 4451840U, 3823437079U, 2887712004U,
4211014926U, 284160791U, 4261479170U, 3623878398U,
4110808573U, 16843267U, 602864369U, 117371142U,
4178704906U, 168817219U, 3589347810U, 251134963U,
18218748U, 4294705672U, 34670337U, 4294966529U,
217441544U, 3238592293U, 1828782329U, 4167041539U,
4261347582U, 4180866822U, 151126528U, 470093048U,
4026267134U, 3793739536U, 4294774257U, 318905831U,
706945750U, 3724349721U, 3656579586U, 353775115U,
16646391U, 4232249344U, 549716528U, 11468800U,
586481663U, 9662U, 1179648U, 36700260U,
94U, 2490472U, 132U, 655466U,
142U, 655465U, 152U, 4325478U,
218U, 1048679U, 234U, 1048683U,
250U, 7209069U, 360U, 4325486U,
426U, 8847359U, 0U, 20226U,
1613742185U, 13959170U, 661263620U, 65537U,
66611U, 1U, 89128960U, 7733606U,
57877107U, 171061770U, 169740820U, 6815744U,
663979U, 2293866U, 10U, 7733248U,
6881280U, 4325376U, 8782U, 255793513U,
575540132U, 352645860U, 3711631360U, 0U,
0U, 4982U, 4258077098U, 321854163U,
544014685U, 0U, 4180476353U, 27205680U,
255787110U, 16U, 4294901761U, 274688U,
301924455U, 16U, 4294901761U, 279569U,
570359915U, 110U, 86629U, 1987405088U,
1399349604U, 1870077984U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 1382381173U,
1696604192U, 538976288U, 538976288U, 538976288U,
1165522034U, 1629512303U, 1818501152U, 538976288U,
538976288U, 92129U, 171061770U, 169740820U,
7143424U, 4325377U, 23585024U, 675964416U,
0U, 737348900U, 675025630U, 1022296849U,
899025511U, 195302660U, 2105144U, 2105144U,
0U, 131072000U, 4911U, 166463980U,
157286510U, 134U, 84048U, 538976288U,
538976288U, 538976288U, 538976288U, 538968065U,
1381379366U, 760229173U, 758132781U, 808791344U,
758132016U, 942485556U, 825832237U, 808463664U,
758132050U, 1702262117U, 538976288U, 538976339U,
1751414573U, 809054769U, 842607920U, 909455904U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 538976288U, 538976288U, 538976288U,
538976288U, 92129U, 2194320161U, 85463041U,
335544320U, 252122391U, 236327948U, 67962118U,
168886795U, 302188801U, 268964608U, 1275603208U,
1275332428U, 136914950U, 1814628398U, 134507528U,
690948100U, 84872716U, 168361993U, 17764885U,
269880851U, 286004231U, 218432267U, 67508044U,
139004046U, 688418350U, 145631496U, 100926470U,
67438608U, 352584192U, 168296448U, 591408668U,
820447404U, 2617853005U, 2594275889U, 2779756058U,
1944691122U, 497245100U, 1306624509U, 1662544335U,
4052761612U, 424695713U, 2503375513U, 1995895190U,
3232696117U, 1438972233U, 85119238U, 1994987976U,
4031805567U, 1328113022U, 2975959503U, 2958181608U,
2281178744U, 3675848097U, 629722016U, 465249344U,
2770231440U, 2555489432U, 4053427971U, 1906831323U,
147828659U, 100527589U, 3990286833U, 97060591U,
251325715U, 4075803963U, 122167345U, 102316034U,
4267777801U, 152186420U, 83819931U, 149020374U,
3806980857U, 3539867598U, 4246330386U, 520882697U,
673382458U, 474080794U, 3582804137U, 60402336U,
471502460U, 3517490885U, 457072453U, 3144496156U,
3998335009U, 2413050137U, 451160473U, 3478067503U,
98597713U, 1614663980U, 865280094U, 658610718U,
1226427896U, 3206991870U, 2146541794U, 2596278408U,
6329730U, 3435725825U, 2459526425U, 295330191U,
310794324U, 2917252146U, 470487400U, 147633818U,
1367605674U, 1608124839U, 2929655810U, 3940937948U,
3236943834U, 152110069U, 500426471U, 3525600789U,
438967836U, 288556029U, 1580075015U, 221130506U,
750841304U, 3924358123U, 3604474111U, 4227021054U,
3857714192U, 370018825U, 806240337U, 371761930U,
3218737783U, 978171467U, 407423680U, 753184821U,
4023929977U, 809895744U, 827387949U, 3697640346U,
3876467182U, 184602620U, 3823229178U, 4026009093U,
136123942U, 118035240U, 771095526U, 3824154369U,
1141836554U, 14685920U, 2583835867U, 53081857U,
1306550277U, 934148097U, 167219021U, 3395158134U,
3187794493U, 436605128U, 3858364658U, 618795029U,
1006630947U, 239082847U, 1658659876U, 2315635926U,
82902019U, 754401605U, 185521345U, 208176267U,
3290630879U, 3473639258U, 225114959U, 984809718U,
1897463564U, 353572101U, 4244173300U, 4075817203U,
67082U, 151785486U, 233840136U, 2159960113U,
3743546425U, 53507331U, 613835060U, 1334697994U,
198161421U, 1059189274U, 4076727298U, 4111205139U,
269352960U
#else
  /* Intel byte order */
4U, 4294877163U, 7392U, 68U, 
671219730U, 4294876970U, 82U, 68U, 
671416350U, 4294876947U, 2408U, 68U, 
671350850U, 4294875167U, 8370U, 68U, 
671219730U, 65621U, 32U, 131072U, 
32U, 4294901810U, 0U, 0U, 
0U, 90133U, 7392U, 1624637458U, 
5373953U, 1966080U, 90349U, 2408U, 
1742798914U, 548536321U, 1179648U, 4294967295U, 
0U, 7340032U, 34078720U, 41420347U, 
106299392U, 1115U, 94045835U, 4294901760U, 
2408U, 34078730U, 6553600U, 78U, 
6815782U, 116U, 6946834U, 134U, 
6881290U, 144U, 7012418U, 210U, 
7143534U, 320U, 7209026U, 386U, 
4294901894U, 0U, 0U, 0U, 
131072U, 175650160U, 16777217U, 1U, 
65537U, 65536U, 23462098U, 2134114817U, 
170408990U, 336204820U, 1179752U, 277086208U, 
170524707U, 575537155U, 6946851U, 10U, 
210U, 4325481U, 0U, 575537152U, 
277087997U, 195306062U, 4124185860U, 56558U, 
0U, 0U, 375652352U, 397999488U, 
316149049U, 39329531U, 0U, 29489453U, 
549782256U, 7014141U, 110U, 1916928001U, 
543716213U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
1937076802U, 538976360U, 538976288U, 538976288U, 
538976288U, 1684827970U, 538976288U, 538976288U, 
538976288U, 538976288U, 90326U, 170408990U, 
336204820U, 4325485U, 196608U, 14049281U, 
6702632U, 0U, 244591719U, 310651751U, 
51461359U, 174536086U, 352586660U, 523763744U, 
523763744U, 0U, 2000U, 221839360U, 
166463980U, 7211360U, 134U, 1346895873U, 
538976288U, 538976288U, 538976288U, 538976288U, 
73760U, 1244484162U, 892162093U, 758132781U, 
808267824U, 825045293U, 875572529U, 758657585U, 
825045041U, 1110257709U, 1752397170U, 538976288U, 
538976288U, 757080096U, 858798384U, 841823794U, 
540292403U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 90326U, 277119459U, 
16779523U, 12U, 17041413U, 100664071U, 
67390466U, 672033804U, 84086534U, 33556225U, 
118031872U, 17105928U, 1275726595U, 251980846U, 
69668623U, 263175U, 2487238144U, 22871822U, 
2046987022U, 2626617727U, 97676034U, 1966731671U, 
4279050242U, 1820263872U, 4284627971U, 922301117U, 
486391093U, 19549952U, 1937863559U, 1385694178U, 
47434788U, 2281743600U, 40121680U, 4263512309U, 
403501251U, 2685341376U, 297551115U, 1149227202U, 
3229894848U, 792774702U, 1428227153U, 1623651712U, 
485721059U, 4184018459U, 268171150U, 3025079824U, 
369369133U, 39565312U, 152688900U, 1653342517U, 
2277589385U, 257354767U, 1691327020U, 1148452965U, 
1080284160U, 3356382572U, 1349258900U, 219456654U, 
1527568715U, 1876826779U, 2194581777U, 4049153818U, 
452095405U, 3632841865U, 4058362092U, 46657022U, 
4260430082U, 418840049U, 15663114U, 159906813U, 
204267535U, 4160813318U, 3052142077U, 134291239U, 
167966450U, 201655065U, 3004472334U, 4126210545U, 
4260886133U, 251724286U, 4194100241U, 4026796521U, 
84217268U, 3355178741U, 34470704U, 50003455U, 
338729432U, 4044816652U, 116199424U, 3557297157U, 
688251666U, 2149121012U, 495817182U, 15289152U, 
4211206587U, 2167871984U, 1283609712U, 483426856U, 
3610562697U, 1865650945U, 10942264U, 202381912U, 
1090524869U, 1484979280U, 4064297212U, 900481279U, 
4247103217U, 4049989402U, 445013418U, 4032878299U, 
4210712697U, 4093640416U, 4045732374U, 157620980U, 
84214274U, 3909158363U, 4294966011U, 50199549U, 
2118447579U, 67176195U, 252573706U, 1089994491U, 
50997889U, 201392137U, 67108864U, 134676481U, 
84018691U, 67390476U, 101591148U, 17040143U, 
33816584U, 395784U, 84347137U, 776735491U, 
252642540U, 117908495U, 1540U, 418136113U, 
3498076394U, 653815709U, 2383790095U, 2804753364U, 
3556978135U, 717998861U, 1594582567U, 1505954656U, 
749329929U, 1791822315U, 2130391013U, 20962080U, 
117526603U, 3711108581U, 805265379U, 3327072687U, 
2888761568U, 2160764672U, 3288393935U, 2367423499U, 
5807105U, 3895403056U, 2136735843U, 95402208U, 
939673728U, 2060976460U, 1081543681U, 3490660132U, 
557059895U, 3960049678U, 3290433180U, 3298304330U, 
588460064U, 4028635189U, 1410663249U, 259210304U, 
84890076U, 4031785477U, 4077315565U, 4211014894U, 
67370246U, 4263377155U, 33688315U, 52824839U, 
4228711683U, 4180611591U, 47771144U, 32953849U, 
4294508025U, 4160614397U, 133957363U, 4079354116U, 
2148136196U, 3900020759U, 177197835U, 2382964775U, 
3365538529U, 4179677004U, 1393590345U, 3612467420U, 
3559641792U, 3728024317U, 149496048U, 3453898534U, 
136466051U, 436273172U, 16777216U, 33752320U, 
67569928U, 219287814U, 252710412U, 201986827U, 
72092684U, 672025604U, 69749775U, 254290948U, 
135145516U, 50659329U, 185471241U, 286527488U, 
1251607U, 33624597U, 100866070U, 67898888U, 
252381701U, 1275922185U, 135162926U, 1325943887U, 
84167684U, 70142980U, 252651500U, 67568647U, 
721420292U, 2073483840U, 2502099027U, 564901632U, 
2877482587U, 544735715U, 21637185U, 2652417456U, 
2249163298U, 830114691U, 417462564U, 630239222U, 
1361748111U, 34748402U, 729995072U, 406819341U, 
4081655074U, 39938700U, 2216932884U, 1828987386U, 
1619904545U, 270535184U, 2161381193U, 239945743U, 
1217352140U, 3765915146U, 989061643U, 2941340610U, 
3896314949U, 2967599969U, 4010610013U, 3563982106U, 
760211245U, 2101157839U, 3613545121U, 4194111535U, 
33947145U, 721876480U, 3909681679U, 1242562563U, 
449641475U, 4277663993U, 99417088U, 4179237454U, 
638059037U, 2566851440U, 2887598125U, 675406032U, 
618926010U, 237668367U, 1015944944U, 3405187851U, 
444571403U, 2271241361U, 3585089959U, 2060848360U, 
4290383U, 2399224955U, 3261006106U, 132658947U, 
1155604164U, 3926003465U, 1229647U, 2600472691U, 
3495657908U, 34214913U, 2617402584U, 1622147209U, 
6930479U, 1155561685U, 1438386034U, 20016129U, 
3355542780U, 1329873102U, 256026639U, 2215454000U, 
4116704275U, 405958914U, 781197728U, 1276007571U, 
273217936U, 234947352U, 3423209989U, 4160152333U, 
3808099830U, 4210881240U, 4060871931U, 720243720U, 
84409853U, 421265925U, 152048879U, 4261870874U, 
4077457155U, 4212128023U, 4244367901U, 3876508407U, 
4110349315U, 4155638782U, 83751674U, 855901188U, 
3960085233U, 727515152U, 990830697U, 1078659813U, 
169117450U, 1108007002U, 42937492U, 540156686U, 
1329652491U, 4067885809U, 3306780676U, 2030187844U, 
1176568626U, 3771218181U, 3473665590U, 148570069U, 
15997382U, 4163435193U, 4292336145U, 888798206U, 
1303710963U, 3549638848U, 259984544U, 219982U, 
3003162695U, 2577072561U, 2275407618U, 1461303807U, 
276570247U, 819088640U, 189026853U, 184148112U, 
2420633568U, 3472902617U, 47108293U, 1308860098U, 
1683718626U, 1170212471U, 3758765081U, 842058587U, 
3356586124U, 814483364U, 3945811703U, 3369832458U, 
3355887696U, 2362459039U, 2056888381U, 3501702320U, 
4074812365U, 2029696121U, 2419655744U, 360725257U, 
3205585147U, 3502263698U, 4177592121U, 1155944580U, 
1355079282U, 1324386378U, 2187261956U, 3502066176U, 
120896289U, 3119857915U, 548853616U, 2696917385U, 
1393367239U, 974327474U, 2152740687U, 1046041479U, 
663941507U, 2863334055U, 261661504U, 923782631U, 
1733496221U, 292549402U, 2376050605U, 4161600459U, 
1015811612U, 254252047U, 2348815600U, 1476919614U, 
4227654383U, 200015611U, 201791747U, 84412167U, 
452872455U, 151455235U, 100994052U, 4110672680U, 
4277992445U, 4259443203U, 116076568U, 201852686U, 
619049737U, 3998611186U, 67242264U, 4282188817U, 
4294967286U, 4143315188U, 4177396477U, 49079536U, 
4278519534U, 151193093U, 4260430066U, 34276088U, 
34279175U, 16909839U, 4294115325U, 502862085U, 
151521283U, 3724347139U, 435766017U, 151001108U, 
3892771328U, 4193976314U, 4092985088U, 2468U, 
66092U, 3024U, 215220262U, 3145728U, 
3689U, 280690860U, 11337728U, 4998U, 
369559204U, 113049600U, 6082U, 437782287U, 
4294901760U, 7392U, 36438076U, 6553600U, 
78U, 6815798U, 132U, 6946854U, 
170U, 6881290U, 180U, 7012418U, 
246U, 7143534U, 356U, 7209026U, 
422U, 4294901894U, 0U, 0U, 
0U, 327680U, 352591460U, 726805430U, 
264824U, 16974336U, 67174401U, 16384002U, 
16777216U, 36044802U, 16777216U, 23462039U, 
2134114822U, 169756190U, 336202250U, 2490472U, 
159842304U, 490274851U, 575537187U, 660799523U, 
596836387U, 287768611U, 287768611U, 671416355U, 
6946851U, 10U, 151U, 4325481U, 
0U, 575537152U, 255788960U, 195304598U, 
4135916804U, 55077U, 0U, 0U, 
387383296U, 372045751U, 275777975U, 38935905U, 
0U, 29489453U, 713621597U, 7014304U, 
110U, 1632632833U, 1092643698U, 1970169206U, 
538976357U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 1802658128U, 1702248736U, 
543520110U, 538976288U, 538976288U, 1836216142U, 
538995809U, 538976288U, 538976288U, 538976288U, 
90349U, 169756190U, 336202250U, 4325485U, 
589824U, 15556609U, 6702888U, 0U, 
201405996U, 213593115U, 51461359U, 174536086U, 
352586660U, 523763744U, 523763744U, 0U, 
2000U, 162988032U, 166463980U, 7211360U, 
134U, 1346895873U, 538976288U, 538976288U, 
538976288U, 538976288U, 73760U, 1227707984U, 
892162093U, 758132781U, 808267824U, 808268077U, 
875572536U, 758461745U, 825047344U, 1345138733U, 
543912545U, 1852143169U, 538994037U, 757080096U, 
858798384U, 841824564U, 540028979U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
90349U, 159875332U, 33623043U, 14U, 
118360077U, 50334219U, 16909317U, 254283340U, 
1212941868U, 70125063U, 251923200U, 33949964U, 
184552449U, 201326848U, 117705985U, 101386249U, 
84085250U, 1179397868U, 67503108U, 101134086U, 
1797U, 1275183150U, 656626975U, 4101211121U, 
337774129U, 1110565119U, 3494462864U, 393121348U, 
115744390U, 2155603138U, 3477142756U, 4273746108U, 
811929579U, 4279066604U, 2290182837U, 2232948546U, 
522202154U, 1901419851U, 3474347589U, 3049590959U, 
521532132U, 441615153U, 3256040705U, 149796313U, 
1098059058U, 231525900U, 2686165294U, 3924414170U, 
1884249851U, 1894885073U, 3042643567U, 2951893717U, 
2810559571U, 244292367U, 2736483654U, 4282478687U, 
16713466U, 3994424083U, 3822976254U, 51245067U, 
4212978427U, 4261213174U, 354680057U, 3120433323U, 
1273830370U, 767027463U, 490307989U, 16779524U, 
15U, 34013448U, 100861188U, 101215232U, 
738601990U, 134482948U, 33555204U, 184549646U, 
218301703U, 134874626U, 738790404U, 803999528U, 
67568711U, 70190343U, 1031U, 2487337027U, 
3306871799U, 382419123U, 30600756U, 419902085U, 
2563878439U, 2669238762U, 1130929301U, 2432394848U, 
361822506U, 4060425333U, 832696480U, 464864700U, 
3124877233U, 3223018237U, 3807986893U, 3169681873U, 
19589634U, 327314627U, 699400643U, 425232449U, 
2420356966U, 2569383218U, 3481281239U, 1983002937U, 
1230000177U, 3012301148U, 726161494U, 3746626924U, 
463914669U, 3880112823U, 227353140U, 3054285070U, 
1494883277U, 427548851U, 3404335946U, 237729740U, 
3977158120U, 2842230953U, 3517593112U, 942777221U, 
2216537174U, 3290146611U, 2444959782U, 2635639581U, 
3522721050U, 2955437780U, 174285320U, 1712026710U, 
1330777216U, 26607182U, 3187916546U, 1696793197U, 
75057923U, 4077161743U, 4009087901U, 82700497U, 
51841023U, 486348037U, 4076532450U, 4177723371U, 
368167937U, 820970538U, 3926131177U, 4193383208U, 
422052091U, 3959565089U, 3924757242U, 250147057U, 
839122917U, 350422528U, 302850585U, 1343296513U, 
4002179302U, 4144164588U, 2919036400U, 4030003933U, 
4258141828U, 438492674U, 352649194U, 587740498U, 
353056797U, 4211995890U, 4245219597U, 1317163770U, 
853538U, 6145U, 151456768U, 101386250U, 
16909056U, 69993732U, 69994028U, 120325164U, 
100927564U, 101189900U, 67240195U, 17104902U, 
117965322U, 386468611U, 202771732U, 269684494U, 
1276252437U, 621801000U, 84280324U, 100945413U, 
67577068U, 1325729543U, 265988U, 1103128064U, 
249029917U, 3578694588U, 3683188877U, 1030021376U, 
4063888099U, 251824133U, 2055333144U, 2696551233U, 
3423583600U, 2855356556U, 656052487U, 1991156069U, 
3292924550U, 1155205659U, 264284646U, 361117991U, 
3676117454U, 2696364028U, 3085143532U, 2620348666U, 
2635411013U, 1344393307U, 964728604U, 43598162U, 
2416777580U, 3407387005U, 63935235U, 3804249863U, 
3417968527U, 853070107U, 3149129966U, 2954567021U, 
3720267203U, 165806257U, 38143490U, 3347179718U, 
645661123U, 1133121538U, 4229785929U, 3783505105U, 
578232195U, 2954381861U, 3729395885U, 1020345184U, 
334529047U, 4178317501U, 553058708U, 4191297778U, 
1304224630U, 1364008623U, 3317589440U, 3377928340U, 
808657168U, 151550743U, 17588479U, 272961328U, 
907053588U, 135390428U, 559869239U, 318185110U, 
982016348U, 2800566476U, 3894061519U, 439319666U, 
363998904U, 1536600879U, 3955350951U, 1045503794U, 
3802390916U, 3331225839U, 3404943525U, 286260548U, 
1048089474U, 2771919495U, 265618050U, 3306043321U, 
1491602948U, 220431502U, 638245363U, 3124906999U, 
1104752411U, 1025811632U, 1640014323U, 4051183031U, 
235010033U, 4244315170U, 3524980724U, 3220499430U, 
3806521583U, 1373039087U, 67835133U, 4042782469U, 
4194825720U, 4009296131U, 4194893827U, 3759276296U, 
4277393397U, 4244898814U, 232128003U, 4162390031U, 
267190002U, 84216580U, 285015308U, 388490957U, 
555086815U, 472057361U, 4078764569U, 4008267008U, 
4148643847U, 1056250875U, 3084813U, 671219713U, 
4126143991U, 4074238967U, 252645649U, 2336563971U, 
4228180463U, 4160748798U, 4170252039U, 3006054400U, 
3876440616U, 151128326U, 822546700U, 3410277573U, 
69690242U, 352388630U, 436207616U, 555424024U, 
202246423U, 50989837U, 33824799U, 303305502U, 
201894929U, 1275331590U, 772163591U, 67660804U, 
772303879U, 386401326U, 69279234U, 286526752U, 
35332361U, 33686532U, 353310210U, 270738433U, 
136119815U, 2229541U, 152699932U, 16908803U, 
318771217U, 50791426U, 202509076U, 218695695U, 
101582090U, 109971532U, 619578372U, 100926982U, 
1325878276U, 237963048U, 1409286151U, 2925533760U, 
3893016288U, 2775646355U, 1780338007U, 2794584817U, 
3843877462U, 2978068753U, 3931026781U, 3997565395U, 
433976836U, 2527719995U, 1595522487U, 1888752004U, 
2468682174U, 1364817752U, 411196047U, 329135055U, 
576703436U, 2138678073U, 3046066076U, 619283172U, 
3799366675U, 1356775195U, 3551584981U, 3135188206U, 
121908339U, 1946291212U, 50136851U, 3734702553U, 
3203401704U, 2042572981U, 3107760400U, 4074964903U, 
4082518902U, 405339393U, 2238785387U, 3856696442U, 
3118576901U, 434149267U, 2469463361U, 1556478592U, 
1567420375U, 179029770U, 4153069071U, 221514485U, 
3145968090U, 924332640U, 269512866U, 2707103728U, 
1155859081U, 1753468809U, 406619548U, 3774586649U, 
380926011U, 3035836596U, 1819599984U, 1102099413U, 
2253557761U, 2492148608U, 3529544213U, 1235271893U, 
1745128144U, 2809757857U, 991618159U, 1453474165U, 
1487280690U, 2284105169U, 83036110U, 3975145155U, 
100329449U, 252446978U, 4127846419U, 3657824031U, 
536932836U, 351213317U, 3372745520U, 101454793U, 
4177395958U, 2767909874U, 253100348U, 454039044U, 
522917631U, 3550636033U, 283634920U, 236195075U, 
4278722335U, 4042321647U, 4076073456U, 3506547194U, 
3405708561U, 4260819934U, 901394937U, 3673025783U, 
4261346544U, 3959293694U, 51644678U, 319296270U, 
4127266298U, 169214185U, 268763400U, 688920354U, 
1601372178U, 4088782648U, 3938571394U, 3591454874U, 
497229433U, 1909600545U, 444143275U, 4037336829U, 
3054129201U, 1375215840U, 2572572564U, 2643557791U, 
1494834304U, 2308073870U, 583997650U, 4041096912U, 
3472345782U, 3505976817U, 3909616398U, 703334154U, 
134352655U, 4142724865U, 4056017641U, 2148583928U, 
3936300365U, 1903951386U, 440065964U, 2793531863U, 
622567980U, 3990842577U, 666263659U, 4280606499U, 
4239718890U, 407527611U, 825244028U, 713871645U, 
1679361410U, 3942054855U, 3960477163U, 4274715645U, 
52891923U, 551903282U, 2189547530U, 403121043U, 
1573122U, 354091008U, 320279574U, 219156241U, 
168561417U, 67568898U, 436208390U, 738990337U, 
235424775U, 1812727884U, 68038152U, 772229196U, 
139199494U, 252773423U, 33687042U, 135139864U, 
336927495U, 134417U, 387318018U, 320077841U, 
34476547U, 252709383U, 101256203U, 67636501U, 
793259013U, 793248813U, 1179387117U, 101057540U, 
72296452U, 788991494U, 1744830471U, 3767615936U, 
137563491U, 456513035U, 3155515518U, 1897417429U, 
3046556104U, 3781074264U, 903553357U, 1478087445U, 
1502703892U, 3618161549U, 247690798U, 3456999182U, 
1244909018U, 2060480306U, 2166747200U, 3251062656U, 
3818238353U, 3035538204U, 1079184261U, 445586188U, 
2301263025U, 740487167U, 623931317U, 2861015379U, 
3060914371U, 660168347U, 1008336687U, 3259334785U, 
3422589153U, 4022430917U, 1863077436U, 3477801674U, 
450963336U, 3022947242U, 1214266114U, 3785505560U, 
1486791560U, 1906320522U, 1754753434U, 1965300102U, 
3220468U, 3746625783U, 4090801202U, 541213184U, 
865077069U, 3474003022U, 3767333264U, 2731133399U, 
790285768U, 2662086007U, 3557825404U, 2603474844U, 
286102885U, 637541254U, 2152836033U, 3506935450U, 
440891299U, 2946556031U, 633095586U, 3378380933U, 
1599357976U, 4160923013U, 4060130245U, 351048337U, 
3923950821U, 63193345U, 3171318401U, 2266339654U, 
1073715073U, 560139033U, 406050189U, 2189776744U, 
3592120373U, 2646108631U, 2444473781U, 498793903U, 
3648111026U, 446990242U, 2897314507U, 3909669240U, 
882400211U, 1336640257U, 1009018904U, 3785398843U, 
3420906501U, 183626949U, 4026980871U, 67239715U, 
1743515145U, 83165211U, 3689672192U, 308658739U, 
3136749041U, 151659033U, 134746637U, 1527385619U, 
133167855U, 151857137U, 3823885834U, 3841520379U, 
4159762413U, 270079028U, 689116434U, 4008907068U, 
4259499752U, 3826833364U, 3285508859U, 3690132670U, 
4246456074U, 4059690492U, 3610572269U, 3891985095U, 
4144232178U, 553541104U, 203824181U, 4279042060U, 
67562994U, 790833930U, 187175976U, 540024608U, 
620500754U, 151595568U, 371924248U, 4261414928U, 
3939628280U, 3906529270U, 3149603311U, 51455873U, 
302055951U, 251658240U, 218632201U, 100797451U, 
85065987U, 239862784U, 139202344U, 256404492U, 
70125103U, 218960390U, 285344262U, 50792974U, 
33688846U, 285347584U, 17631239U, 84151299U, 
185142800U, 250350086U, 67405388U, 1325860356U, 
101057540U, 235736904U, 3226140929U, 3569765415U, 
76966881U, 2684646032U, 85754673U, 956978737U, 
2157027711U, 469566844U, 4008957122U, 2936762376U, 
4204993122U, 484486451U, 3657840649U, 1346436500U, 
1787572508U, 1279853234U, 2335605976U, 2083593312U, 
2147491056U, 147079705U, 3287941124U, 3762481696U, 
350191818U, 3507467821U, 415413124U, 782250343U, 
1517772949U, 1891913148U, 570457396U, 1913455736U, 
1415965248U, 198208240U, 3088728244U, 3225230744U, 
147095831U, 167719619U, 541590768U, 506483479U, 
3406205973U, 277867856U, 2370571467U, 665801031U, 
1794185876U, 1095543761U, 3718704653U, 3675202075U, 
4231069024U, 3770604896U, 4177804781U, 2080702716U, 
2960986592U, 294616869U, 3254892177U, 3138594537U, 
523210957U, 1418337031U, 3769292460U, 3540023495U, 
3945066743U, 4027907564U, 727871947U, 3977515576U, 
84411147U, 4177997059U, 100135659U, 3994881031U, 
117372660U, 486276101U, 185471751U, 4007846403U, 
4210947337U, 333512443U, 502657310U, 3758163475U, 
4057654510U, 55184385U, 4210990115U, 33948668U, 
720450878U, 302779409U, 4228046658U, 4227726071U, 
50154749U, 287055115U, 74964540U, 4227727877U, 
4181132031U, 2186685694U, 470225191U, 1966594U, 
19005440U, 471671042U, 319885074U, 185339918U, 
118035989U, 503650841U, 52436740U, 141304576U, 
103548422U, 118433932U, 1212943876U, 709496335U, 
69996044U, 117704812U, 85133100U, 67374621U, 
337057819U, 101190157U, 488768260U, 202506245U, 
469959172U, 185401614U, 118102784U, 436404765U, 
320341527U, 252446994U, 369367317U, 286525445U, 
1213090832U, 2402242054U, 67382790U, 116328196U, 
67569575U, 72288004U, 67372804U, 470791U, 
197158400U, 467248016U, 4216369635U, 2248135256U, 
268091283U, 30614624U, 3892371653U, 1735460427U, 
2261936133U, 3405873968U, 2762162393U, 262604704U, 
960344606U, 3114393137U, 2182013500U, 189148800U, 
3503608539U, 594553875U, 1009465399U, 268782048U, 
2429666857U, 2514699650U, 1573198639U, 736157596U, 
1332853074U, 2138436630U, 565550870U, 2872110094U, 
1215371857U, 523629174U, 112526614U, 1752626096U, 
3510694613U, 1015273658U, 809672304U, 3760998577U, 
364486472U, 3177645461U, 561842073U, 2576767125U, 
718053393U, 1800465360U, 212243569U, 2139426315U, 
3776598523U, 548794532U, 377516198U, 418368011U, 
2452337985U, 3657554725U, 488642870U, 3504177591U, 
596696611U, 1428566040U, 1846155628U, 3723544043U, 
581595256U, 4029207104U, 3115558099U, 2060286101U, 
1345198924U, 3232213968U, 3811061929U, 1122508862U, 
587388U, 200428806U, 1376800910U, 878871681U, 
1082289981U, 819663532U, 3760197617U, 3284789383U, 
553225536U, 2722231725U, 1913459173U, 2208763868U, 
894223951U, 2869223658U, 2159281774U, 4076011773U, 
187297778U, 4244771329U, 32566757U, 4007463149U, 
168754968U, 333446418U, 4247653434U, 16380151U, 
3773697511U, 148503816U, 4294837511U, 4294572282U, 
202316031U, 3171553291U, 16645615U, 4047303451U, 
153745933U, 4195166736U, 4093508274U, 2763849449U, 
4210095445U, 3875010803U, 3168793844U, 149857006U, 
3890555845U, 112198702U, 4176279003U, 15986680U, 
117586433U, 168435212U, 3472622085U, 671449800U, 
50404871U, 27U, 50399490U, 67569664U, 
521936413U, 538641670U, 303503905U, 336924952U, 
235867402U, 141298444U, 134508335U, 69994244U, 
70068268U, 103688236U, 254283340U, 1212941868U, 
70125063U, 419697930U, 68162326U, 486670594U, 
169220892U, 16777749U, 1251349U, 67179543U, 
369230618U, 101778713U, 235669778U, 118165517U, 
1812596751U, 789499432U, 67503436U, 134677508U, 
1179397868U, 67503108U, 101134086U, 16844549U, 
1278951458U, 3684750675U, 899417464U, 419702915U, 
1778940999U, 90992700U, 2625215189U, 87788934U, 
2048453689U, 2619515046U, 3789260595U, 44228865U, 
2902686045U, 919979158U, 192944366U, 1057824019U, 
2861173355U, 460982796U, 3132184710U, 2319159125U, 
618606787U, 3299266456U, 2400759747U, 12235003U, 
1647881459U, 134804620U, 1728756063U, 4123540580U, 
1158270808U, 119341575U, 3385200634U, 33154069U, 
4010344928U, 3928937823U, 857342U, 3890539537U, 
1374281251U, 3509780521U, 334501085U, 4226655779U, 
33486541U, 772737297U, 527004480U, 4045874005U, 
854881155U, 4279517917U, 2420257248U, 1154500933U, 
2249682574U, 3255232030U, 3833625572U, 3167699196U, 
3959340080U, 3274889004U, 3044092799U, 1116242282U, 
713365527U, 1260331052U, 477320541U, 2949584502U, 
3837081390U, 824120818U, 18502275U, 3653374781U, 
839445941U, 205615885U, 772656334U, 3667925921U, 
4226410958U, 2826063710U, 1869672902U, 3585461547U, 
1404039762U, 262636988U, 1175359387U, 1604524909U, 
4211032432U, 318832390U, 4277016083U, 199482876U, 
4211281392U, 4143652082U, 4194172147U, 2870289404U, 
3803840008U, 122416411U, 690862057U, 3869024192U, 
4029684189U, 2899284713U, 2710295065U, 405332635U, 
2217060321U, 2786821642U, 850375086U, 2535660102U, 
1582866617U, 2980230685U, 709482369U, 2719554843U, 
2662211960U, 1912491320U, 413529600U, 901799274U, 
2705777317U, 1869685054U, 773182340U, 2132510180U, 
3571167264U, 2157570932U, 2450643239U, 2182979833U, 
3707869634U, 2256087565U, 1042158020U, 2513541682U, 
1107645342U, 1463834654U, 4174690689U, 2592681482U, 
3179345817U, 3283398720U, 2420365788U, 569041722U, 
673914924U, 3676247072U, 4024682293U, 4112647925U, 
129620715U, 237763330U, 237968916U, 451992918U, 
3671059935U, 505148150U, 203167529U, 437976134U, 
330688770U, 486018563U, 674563362U, 3639154456U, 
4143775462U, 7410U, 111608320U, 7922U, 
548536319U, 1179648U, 512U, 5111908U, 
2490368U, 7602280U, 655360U, 8257642U, 
655360U, 8913001U, 4325376U, 13238379U, 
7208960U, 20447341U, 4325376U, 24772718U, 
8781824U, 65535U, 0U, 0U, 
0U, 896532482U, 68216U, 65792U, 
815U, 58327041U, 13762560U, 28115302U, 
1176403764U, 336857630U, 6820874U, 10U, 
2296199U, 655466U, 13762560U, 6881280U, 
66U, 0U, 117252686U, 394333852U, 
352586660U, 3739088997U, 196618U, 458760U, 
655366U, 4285470967U, 264968703U, 540612653U, 
21299917U, 4180476229U, 4277141953U, 117246716U, 
7209067U, 65536U, 544042820U, 1970495811U, 
538995809U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 1866735648U, 1631789165U, 
1818326387U, 538976288U, 538976288U, 1867391008U, 
1818324338U, 538976288U, 538976288U, 538976288U, 
1611997216U, 1176371201U, 336857630U, 7148554U, 
66U, 1610678273U, 1126694933U, 102U, 
379453440U, 771164822U, 1022301301U, 899023633U, 
195299943U, 2102532U, 2105144U, 7992U, 
131072000U, 0U, 166465483U, 157288940U, 
8781934U, 65536U, 538988616U, 538976288U, 
538976288U, 538976288U, 538976288U, 1128529921U, 
1345147437U, 808269101U, 808463664U, 808267821U, 
758657069U, 892417072U, 825240887U, 808267821U, 
1836008493U, 1935754016U, 543973749U, 538976288U, 
959458592U, 858992944U, 808464429U, 538976304U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
1611997216U, 2176778241U, 167971207U, 852224U, 
84344832U, 67240710U, 590088U, 1812727884U, 
252579400U, 117640207U, 201326595U, 150995466U, 
117965827U, 100991243U, 1143738092U, 100926470U, 
671548463U, 1342177294U, 1963772096U, 1021592212U, 
26147840U, 1410884197U, 3397587253U, 4144759159U, 
1101446144U, 1484867897U, 400818485U, 2736403491U, 
3154311635U, 748159015U, 254488928U, 2685350097U, 
556830508U, 289087793U, 385354915U, 253074447U, 
541072352U, 862394442U, 2864350993U, 31654978U, 
1082225048U, 4186636319U, 14851840U, 190895719U, 
1853558511U, 1326955277U, 4194315151U, 3100573913U, 
65748865U, 67184992U, 2488008998U, 2158125848U, 
143972636U, 2297847886U, 3758743248U, 2862629868U, 
1135790215U, 1350560656U, 2701457885U, 700833893U, 
55948801U, 1422692635U, 4082165997U, 2129119478U, 
8366658U, 4087397345U, 3086815926U, 3501523268U, 
468891915U, 3189846270U, 5377195U, 1128329915U, 
270208943U, 485807630U, 2692321710U, 4042438650U, 
72689635U, 15614720U, 386262499U, 83697324U, 
251526906U, 401862416U, 33751550U, 4278124503U, 
4260693493U, 50462977U, 4059754019U, 116522502U, 
184160761U, 1139937034U, 3793351125U, 4077123598U, 
4244509953U, 134413567U, 17240322U, 33423359U, 
149288204U, 637470913U, 4177527149U, 50487544U, 
4261478397U, 116863737U, 131593U, 4161537308U, 
4277533679U, 283058146U, 4044226047U, 3877437971U, 
3592561450U, 420609501U, 50000601U, 187569685U, 
4144037376U, 549733372U, 36700160U, 586416303U, 
4294901760U, 9662U, 36700178U, 6553600U, 
94U, 6815782U, 132U, 6946826U, 
142U, 6881290U, 152U, 6684738U, 
218U, 6750224U, 234U, 7012368U, 
250U, 7143534U, 360U, 7209026U, 
426U, 4294901894U, 0U, 34078720U, 
41420347U, 131072U, 352593770U, 16777217U, 
70451201U, 65536U, 1360U, 23462006U, 
577962867U, 171061770U, 336207370U, 655464U, 
564854784U, 6946851U, 10U, 118U, 
4325481U, 0U, 575537152U, 426315583U, 
195306062U, 4075033860U, 56635U, 0U, 
0U, 326500352U, 430636493U, 450040623U, 
22880365U, 0U, 29489453U, 540017055U, 
6688575U, 16U, 131071U, 3211268U, 
6815505U, 16U, 131071U, 289669124U, 
7077665U, 110U, 1699872769U, 543520118U, 
1684105299U, 538998639U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 1970693458U, 
538976357U, 538976288U, 538976288U, 538976288U, 
1920235589U, 1868701793U, 538993772U, 538976288U, 
538976288U, 92129U, 171061770U, 336207370U, 
4325485U, 65536U, 14771969U, 6703656U, 
0U, 220474355U, 316549180U, 51461359U, 
174536086U, 352586660U, 523763744U, 523763744U, 
0U, 2000U, 321847296U, 166463980U, 
7211360U, 134U, 1346895873U, 538976288U, 
538976288U, 538976288U, 538976288U, 73760U, 
640505426U, 892162093U, 758132781U, 808269104U, 
808267821U, 875572536U, 758331697U, 808267824U, 
1378693165U, 1702196837U, 538976288U, 1394614304U, 
762799208U, 825375024U, 808270130U, 540161334U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 538976288U, 538976288U, 538976288U, 
538976288U, 92129U, 564888266U, 17831941U, 
20U, 387254031U, 202642958U, 100994308U, 
184684810U, 17367826U, 1247248U, 136906828U, 
1275528268U, 103295240U, 772286828U, 141296648U, 
67645225U, 202247941U, 150997258U, 353505025U, 
319690256U, 118754321U, 184747277U, 1276577284U, 
2382907656U, 778962985U, 136949256U, 100926470U, 
268961028U, 132117U, 2058U, 472793123U, 
2886264624U, 1296304540U, 830644634U, 448180133U, 
2996955507U, 2891686685U, 4253147469U, 3479771235U, 
205295857U, 2706853913U, 2574923413U, 2532439670U, 
889696192U, 1241105493U, 114364933U, 3357141366U, 
2138591472U, 2120558927U, 3481887153U, 3896791728U, 
2013722759U, 2717718747U, 2697693221U, 1076149019U, 
2422218405U, 2561429912U, 57121521U, 3689916273U, 
3014643464U, 3857579269U, 4059158253U, 4010199301U, 
334363150U, 1003876338U, 824199175U, 37230854U, 
153051646U, 875434505U, 2617114116U, 3604930824U, 
4192922082U, 3457679058U, 316414461U, 151653407U, 
973087528U, 451297564U, 2839580117U, 2695534851U, 
2089687580U, 3316033745U, 1163869723U, 473198011U, 
566252014U, 423220367U, 2569659418U, 790187983U, 
1367072773U, 751910240U, 1579193139U, 513425703U, 
4174190921U, 4274988735U, 3802198399U, 2282799258U, 
2190827520U, 17091020U, 426088850U, 2405538321U, 
1415087634U, 851239341U, 1745947420U, 2595671048U, 
2852226129U, 2802440799U, 33595310U, 3706775018U, 
3671584704U, 4110618889U, 3890664221U, 359015634U, 
471738906U, 4244845329U, 117976670U, 170864141U, 
3639459884U, 3958630633U, 4293449686U, 4265145339U, 
269283557U, 151653910U, 1363152432U, 178464790U, 
1997200063U, 1270238522U, 3234482200U, 900785196U, 
2035079407U, 1074218544U, 770723889U, 2592302556U, 
3996192487U, 4241424395U, 4208517603U, 84343023U, 
638983432U, 672336135U, 3875271981U, 33026019U, 
168496964U, 3759595520U, 3677618842U, 32975107U, 
90234957U, 33074487U, 1301280521U, 1979735754U, 
1038221758U, 3356493338U, 4076403173U, 353428004U, 
603520827U, 1595752462U, 606395746U, 3603694986U, 
66908164U, 1161951020U, 3251899915U, 2340710412U, 
3741983684U, 1518799823U, 1341876749U, 4127241018U, 
218044529U, 85267221U, 4109498620U, 4077449458U, 
168165632U, 235932681U, 136245261U, 828423808U, 
972825055U, 58011651U, 878810660U, 182488399U, 
229953291U, 452075839U, 49085938U, 318967029U, 
3600U
#endif
} ;

/* -------------------------------------------------------------------------- */
#if 0

/* Output a soft font as a PCL stream

   This isn't much use on Intel as we'd have to convert it all for endianness
   on the fly.
*/

int dump_user_font(user_font * font)
{
  FILE * file ;
  char buffer[32] = {0} ;
  int  i, b ;

  if ( font == 0 || font->length == 0 ||
       (file = fopen("dumpfont.pcl", "wb")) == 0 )
    return 0 ;

  /* Write out font header */
  b = sprintf(buffer, "\x1B)s%iW", font->length) ;
  fwrite(buffer, 1, b, file) ;
  fwrite(font->data, 1, font->length, file) ;

  /* Write out the glyphs in no particular order */
  for (i = 0; i < USER_GLYPH_HEADS; i++) {
    user_glyph * glyph = font->glyphs[i] ;
    while (glyph) {
      if (glyph->length > 0) {
        b = sprintf(buffer, "\x1B*c%iE", glyph->code) ;
        fwrite(buffer, 1, b, file) ;
        b = sprintf(buffer, "\x1B(s%iW", glyph->length) ;
        fwrite(buffer, 1, b, file) ;
        fwrite(glyph->data, 1, glyph->length, file) ;
      }
      glyph = glyph->next ;
    }
  }

  fclose (file) ;

  return 0 ;
}

#endif
/* -------------------------------------------------------------------------- */

#define REGISTER(_type,_id,_thing,_len) \
(SwRegisterRDR(RDR_CLASS_PCL, (_type), (_id), \
               (_thing), (_len), SW_RDR_DEFAULT) == SW_RDR_SUCCESS)

int define_RDRs(void)
{
  int ok ;

  ok = REGISTER(RDR_PCL_GLYPHS, 0, glyphlist, sizeof(glyphlist)) ;
  if (ok)
    ok = REGISTER(RDR_PCL_SSMAP, 0, sslist, lengthof(sslist)) ;
  if (ok)
      ok = REGISTER(RDR_PCL_XLMAP, 0, &XL_data, sizeof(XL_data)) ;

  /* Define the default LinePrinter font in PCL format */
  if (ok)
      ok = SwRegisterRDRandID(RDR_CLASS_FONT, RDR_FONT_PCLEO, NULL, lineprinter,
                              sizeof(lineprinter), SW_RDR_DEFAULT) == SW_RDR_SUCCESS ;

  return ok ;
}

/* ========================================================================== */

#define FIND(_type,_id,_ptr,_length) \
(SwFindRDR(RDR_CLASS_PCL, (_type), (_id), (_ptr), (_length)) == SW_RDR_SUCCESS)

int find_RDRs(pfin_ufst5_instance * ws)
{
  int ok ;
  void * vfontmap = 0, * vglyphlist = 0, * vsslist = 0 ;
  void * vxldata = 0,  * vcgif = 0 ;

  HQASSERT(ws, "No workspace?") ;

  ws->fontmaplength = 0 ;
  ws->sslistlen = 0 ;

  /* fontmap is optional */
    ok = FIND(RDR_PCL_MAP,    0, &vfontmap,   &ws->fontmaplength) ;

    ok = FIND(RDR_PCL_GLYPHS, 0, &vglyphlist, 0) ; /* don't need length */
  if (ok)
    ok = FIND(RDR_PCL_SSMAP,  0, &vsslist,    &ws->sslistlen) ;
  if (ok)
    ok = FIND(RDR_PCL_XLMAP,  0, &vxldata,    0) ; /* don't need length */
  if (ok)
    ok = (SwFindRDR(RDR_CLASS_API, RDR_API_CGIF, 0x10000, &vcgif, 0)
            == SW_RDR_SUCCESS) ;

  /* It might seem more natural to put &ws->pfontmap etc in the above FINDs,
     but C can't in general cast from void** to any**, though it can cast from
     void* to any*.
   */
  ws->pfontmap   = (PCL_FONTMAP *) vfontmap ;
  ws->pglyphlist = (unsigned char *) vglyphlist ;
  ws->psslist    = (PCL_SSINFO *) vsslist ;
  ws->pxldata    = (XL_DATA *) vxldata ;
  ws->cgif       = (pfin_ufst5_fns *) vcgif ;

  if (ok) { /* Find and load ROM-supplied PCLEOs, like LinePrinter */
    size_t length ;
    void * pcl5 ;
    sw_rdr_iterator * it = SwFindRDRbyType(RDR_CLASS_FONT, RDR_FONT_PCLEO) ;
    while (SwNextRDR(it, 0, 0, 0, &pcl5, &length) == SW_RDR_SUCCESS)
      define_pcl5_font(ws, (uint8 *) pcl5, length) ;
    SwFoundRDR(it) ;
  }

  return ok ;
}

/* ========================================================================== */
#endif /* USE_UFST5*/

