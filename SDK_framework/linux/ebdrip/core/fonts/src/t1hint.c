/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:t1hint.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 * Copyright (C) 1992 IBM, Lexmark
 *
 * \brief
 * This file contains most of the extra code added to get type1 hinted
 * fonts going. Original code from the IBM/Lexmark type 1 renderer in
 * X11r5.
 */

#include <math.h> /* floor */

#include "core.h"
#include "objects.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "monitor.h"
#include "graphics.h"
#include "fonts.h"
#include "namedef_.h"

#include "params.h"
#include "charstring12.h"
#include "chbuild.h"
#include "matrix.h"
#include "gstate.h"
#include "adobe1.h"
#include "t1hint.h"
#include "fontcache.h"
#include "fontparam.h"
#include "constant.h" /* EPSILON - yuck */


#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )

#include "showops.h"
#include "ripdebug.h"

/** Set DEBUG_HINT_PICTURE for a pretty postscript picture of the hints before
   and after. You will also need to copy COREfonts!testsrc:procsets:HintDebug
   to procsets/HintDebug in your SW folder. */
enum {
  DEBUG_HINT_PICTURE = 1,
  DEBUG_HINT_INTEGRAL = 2,
  DEBUG_HINT_FAILURE = 4,
  DEBUG_HINT_INFO = 8
} ;
static int32 debug_hints = 0;

#endif


#if defined( DEBUG_BUILD )

enum {
  HINT_METHOD_XFLEX = 8,    /* set to inhibit stem use recording */
  HINT_METHOD_COUNTER = 32  /* set to deactivate counter hints */
} ;
static int32 hint_method = 0;

#endif

/* --- Constants -- */
#define MAXSTACK 24        /* Adobe Type1 limit */
#define MAXCALLSTACK 10    /* Adobe Type1 limit */
#define MAXPSFAKESTACK 32  /* Max depth of fake PostScript stack (local) */
#define MAXSTRLEN 512      /* Max length of a Type 1 string (local) */
#define MAXLABEL 256       /* Maximum number of new hints */
#define EPS (0.001)        /* Small number for comparisons */
/* numbers for rough matching */
#define BIGEPSX (10)       /* another, bigger, small number for X */
#define BIGEPSY (8)        /* another, bigger, small number for Y */
#define EXTRABLUEFUZZ (4)  /* Different from the spec, but necessary. */

#define FIXED_STEMS 32     /* Number of preallocated stems */

#define MAXALIGNMENTZONES ((NUMBLUEVALUES+NUMOTHERBLUES)/2)

/* BLUES */
/* Note that we're currently doing nothing for minfeature and password. */

#define NUMBLUEVALUES 14
#define NUMOTHERBLUES 10
#define NUMFAMILYBLUES 14
#define NUMFAMILYOTHERBLUES 10
#define NUMSTEMSNAPH 12
#define NUMSTEMSNAPV 12
#define NUMSTDHW 10
#define NUMSTDVW 10

#define DEFAULTBLUESCALE 0.039625
#define DEFAULTBLUESHIFT 7
#define DEFAULTBLUEFUZZ 1
#define DEFAULTFORCEBOLD FALSE
#define DEFAULTLANGUAGEGROUP 0
#define DEFAULTRNDSTEMUP FALSE
#define DEFAULTLENIV 4
#define DEFAULTEXPANSIONFACTOR 0.06
#define DEFAULTBOLDSTEMWIDTH 2

/* --- Types --- */

typedef struct alignmentzone {
  Bool topzone;        /* TRUE if a topzone, FALSE if a bottom zone */
  SYSTEMVALUE bottomy, topy;       /* interval of this alignment zone */
} *align_ptr ;

typedef struct blues_struct {
  /* Identifying information for this set of blues. */
  int32 UniqueID, FID, FDIndex ;

  /* First, all the enumerators.  They are first so it is
     easier to init the structure.
  */
  int32 numBlueValues;   /* # of BlueValues in following array */
  int32 numOtherBlues;   /* # of OtherBlues values in following array */
  int32 numFamilyBlues;   /* # of FamilyBlues values in following array */
  int32 numFamilyOtherBlues; /* # of FamilyOtherBlues values in  */
  int32 BlueShift;
  int32 BlueFuzz;
  int32 numStemSnapH;   /* # of StemSnapH values in following array */
  int32 numStemSnapV;   /* # of StemSnapV values in following array */
  int32 ForceBold;
  int32 LanguageGroup;
  int32 RndStemUp;
  SYSTEMVALUE BlueScale;
  SYSTEMVALUE StdHW ;  /* Technically, these are arrays but they only */
  SYSTEMVALUE StdVW ;  /* ever have one entry */
  SYSTEMVALUE ExpansionFactor;

  /* Now yer actualarrays of stuff */
  int32 BlueValues[NUMBLUEVALUES];
  int32 OtherBlues[NUMOTHERBLUES];
  int32 FamilyBlues[NUMFAMILYBLUES];
  int32 FamilyOtherBlues[NUMFAMILYOTHERBLUES]; /* this array */
  SYSTEMVALUE StemSnapH[NUMSTEMSNAPH];
  SYSTEMVALUE StemSnapV[NUMSTEMSNAPV];
} *blues_ptr ;
/* This is font-wide */

/* --- Macros -- */
#define NEAREST(x) ((tmpx = (x)) < 0.0 ? ((int32)(tmpx - 0.5)) : ((int32)(tmpx + 0.5)))

/* --- Types --- */

/* Used to pass around x or y coords in a hint zone */
typedef struct hint {
  ch_float x, y;
  Bool xvalid, yvalid;
} hint_t;


/* --- Internal Functions --- */
static Bool get_blues(charstring_methods_t *charfns) ;

/* --- Font Variables --- */
static struct blues_struct blues_str ;
static struct alignmentzone alignmentzones[MAXALIGNMENTZONES];
static int32 numalignmentzones = 0;
static int32 baseline_index = -1;
static Bool alignmentzone_change = TRUE ;
static SYSTEMVALUE onepixelX, onepixelY;
static SYSTEMVALUE unitpixelsX = 0.0, unitpixelsY = 0.0 ;
static SYSTEMVALUE unitpixelsX2 = 0.0, unitpixelsY2 = 0.0 ;

#define ONEPIXEL(v) ((v) ? onepixelX : onepixelY)
#define UNITPIXELS(v) ((v) ? unitpixelsX : unitpixelsY)

#define STEMFRACTION 3.0

/*--------------------------------------------------------------------------*/
/** Stems are kept in sorted doubly-linked lists for active and inactive,
   horizontal and vertical stems. The list head may point anywhere in the
   list, and is modified to reflect the closest stem to the current point, to
   make stem lookups faster. The character builder gives each stem an index
   number when it is created, which will be used by the hintmask and cntrmask
   operators to refer to that stem in future. Stem indices are reset when
   setting bearings, allowing SEAC characters to use the same indices for the
   consituent characters (the hints are not discarded, however). */
typedef struct stem_t {
  ch_float z, dz;      /* 'z' means either 'x' or 'y' depending on "vertical" */
  ch_float lo_delta, hi_delta;
  struct stem_t *prev, *next ;
  uint32 edge ;      /* Edge flags */
  int32 index ;      /* The index by which the char builder knows the stem */
  int32 idealwidth ; /* Number of pixels width of adjusted stem */
  int32 otherwidth ; /* Alternate width in pixels */
  uint32 group ;     /* Counter control group */
  uint32 vertical;   /* STEM_H or STEM_V? */
  uint32 fixed ;     /* Stem edges (flags) fixed by bluezone or cntrmask */
  Bool prealloc ;    /* Pre-allocated, don't free with MM */
} stem_t ;

enum { /* Edge flags */
  HINT_EDGE_HIGH = 1,
  HINT_EDGE_LOW = 2
} ;

static stem_t *stem_find(stem_t **stem_list, ch_float z, ch_float dz, uint32 edge) ;
static stem_t *stem_find_point(stem_t **stem_list, ch_float z) ;
static stem_t *stem_find_index(stem_t **stem_list, int32 index) ;
static stem_t *stem_find_group(stem_t **stem_list, uint32 group) ;
static void stem_remove(stem_t **stem_list) ;
static void stem_add(stem_t **stem_list, stem_t *stem) ;
void stem_first(stem_t **stems) ;
#if defined(ASSERT_BUILD)
static Bool stems_assert_valid(stem_t *stems) ;
#endif

/* Constants to find the correct stem lists */
enum {
  STEM_H = 0, STEM_V = 1
} ;

enum {
  STEM_INACTIVE = 0, STEM_ACTIVE = 1
} ;

/* --- Char variables --- */
typedef struct {
  charstring_build_t pathfns ;
  PATHINFO *path ; /* Path built by underlying ch_build_path */

  int32 numstems;  /* The number of stems for this char */
  Bool InDotSection ;
  ch_float sidebearingY, sidebearingX, baselineDy ;

  USERVALUE t1stemsnap;

  stem_t *stems[2][2] ; /* H/V, Active/Inactive */

  /* An area to keep some pre-allocated stems - most chars have < 10
     stems. So is 32 to have a chance with kanji characters. */
  stem_t prealloc_stems[FIXED_STEMS] ;

#if defined(DEBUG_BUILD)
  hint_t last_hint;  /* most recent hint employed */
#endif
} t1hint_info_t ;

void init_C_globals_t1hint(void)
{
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  debug_hints = 0 ;
#endif
#if defined( DEBUG_BUILD )
  hint_method = 0 ;
#endif
  numalignmentzones = 0;
  baseline_index = -1;
  alignmentzone_change = TRUE ;
  unitpixelsX = 0.0;
  unitpixelsY = 0.0 ;
  unitpixelsX2 = 0.0;
  unitpixelsY2 = 0.0 ;
}

/*--------------------------------------------------------------------------*/
/* chbuild interface to hinting; the hinter acts as a filter, adjusting
   points and passing calls through to the underlying charstring build
   routines. The hinting filter does not call the base charstring builder
   with any hinting routines, only initchar, endchar, setbearing, setwidth,
   moveto, lineto, curveto and closepath. */
static Bool hint_initchar( void *data ) ;
static Bool hint_hstem(void *data, ch_float y1, ch_float y2,
                       Bool tedge, Bool bedge, int32 index) ;
static Bool hint_vstem(void *data, ch_float x1, ch_float x2,
                       Bool ledge, Bool redge, int32 index) ;
static Bool hint_hintmask( void *data, int32 index, Bool activate ) ;
static Bool hint_cntrmask( void *data, int32 index, uint32 group ) ;
static Bool hint_flex( void *data, ch_float curve_a[6], ch_float curve_b[6],
                       ch_float depth, ch_float thresh, Bool hflex ) ;
static Bool hint_dotsection( void *data ) ;
static Bool hint_change( void *data ) ;
static Bool hint_setwidth( void *data, ch_float xwidth , ch_float ywidth ) ;
static Bool hint_setbearing( void *data, ch_float xbear , ch_float ybear ) ;
static Bool hint_moveto( void *data, ch_float x , ch_float y ) ;
static Bool hint_lineto( void *data, ch_float x , ch_float y ) ;
static Bool hint_curveto( void *data, ch_float curve[6] ) ;
static Bool hint_closepath( void *data ) ;
static Bool hint_endchar( void *data, Bool result ) ;

/*--------------------------------------------------------------------------*/

#if defined(DEBUG_BUILD)
static void DumpStem(stem_t *s, Bool countershift)
{
  HQASSERT((debug_hints & DEBUG_HINT_PICTURE) != 0, "In DumpStem but not debugging!") ;
  monitorf((uint8 *)"%s %f %f %f %f %s\n",
           countershift ? "true" : "false",
           s->z, s->z + s->dz,
           s->z + s->lo_delta,
           s->z + s->dz + s->hi_delta,
           (s->vertical == STEM_V) ? "VStem" :"HStem") ;
}

static void DumpZone(align_ptr z)
{
  HQASSERT((debug_hints & DEBUG_HINT_PICTURE) != 0, "In DumpZone but not debugging!") ;
  if ( z->topzone ) {
    monitorf((uint8 *)"%f %f TopZone\n", z->bottomy, z->topy);
  } else {
    monitorf((uint8 *)"%f %f BottomZone\n", z->bottomy, z->topy);
  }
}
#endif /* defined(DEBUG_BUILD) */

/* ---------------------------------------------------------------------- */
static void set_t1_scale ( void )
{
  register SYSTEMVALUE m00, m01, m10, m11 ;
  SYSTEMVALUE oldpixelsX2 = unitpixelsX2, oldpixelsY2 = unitpixelsY2 ;

  m00 = theFontMatrix( theFontInfo(*gstateptr)).matrix[ 0 ][ 0 ] ;
  m01 = theFontMatrix( theFontInfo(*gstateptr)).matrix[ 0 ][ 1 ] ;
  m10 = theFontMatrix( theFontInfo(*gstateptr)).matrix[ 1 ][ 0 ] ;
  m11 = theFontMatrix( theFontInfo(*gstateptr)).matrix[ 1 ][ 1 ] ;

  /* unitpixelsX/Y are the number of pixels per character space unit. These
     are calculated from the lengths of the character space X and Y
     direction vectors. Non-square resolutions and non-orthogonal axes are a
     problem; it's not possibly to produce a simple shift in X and Y which
     will align stems to pixel boundaries. */
  unitpixelsX2 = m00 * m00 + m01 * m01 ;
  unitpixelsY2 = m10 * m10 + m11 * m11 ;

  /* The alignment zones depend upon the scale to decide if the Family Blues
     are used rather than the character Blues. We can also use this to avoid
     expensive square root operations if the squared values do not match. */
  if ( oldpixelsX2 != unitpixelsX2 ||
       oldpixelsY2 != unitpixelsY2 ) {
    alignmentzone_change = TRUE ;

    unitpixelsX = sqrt(unitpixelsX2) ;
    unitpixelsY = sqrt(unitpixelsY2) ;

    /* onepixelX/Y are length of one pixel in character space */
    if (unitpixelsX == 0.0)
      onepixelX = 0.0;
    else
      onepixelX = 1.0 / unitpixelsX ;
    if (unitpixelsY == 0.0)
      onepixelY = 0.0;
    else
      onepixelY = 1.0 / unitpixelsY ;
  }
}

Bool t1hint_build_path(corecontext_t *context,
                       charstring_methods_t *methods,
                       OBJECT *stringo,
                       charstring_decode_fn decoder,
                       charstring_build_t *buildnew,
                       charstring_build_t *buildtop,
                       PATHINFO *path, ch_float *xwidth, ch_float *ywidth)
{
  t1hint_info_t t1hint_info ;

  /* Template for the hinting filter. This is copied into a new charstring
     build structure, never modified directly. */
  static charstring_build_t ch_template = {
    NULL ,

    hint_initchar ,

    hint_setbearing ,
    hint_setwidth ,

    hint_hstem ,
    hint_vstem ,
    hint_hintmask ,
    hint_cntrmask ,

    hint_flex,
    hint_dotsection,
    hint_change,

    hint_moveto ,
    hint_lineto ,
    hint_curveto ,
    hint_closepath ,

    hint_endchar
  } ;

  t1hint_info.path = path ;
  t1hint_info.t1stemsnap = context->systemparams->Type1StemSnap ;

  set_t1_scale();

  if ( !get_blues(methods) )
    return FAILURE(FALSE) ;

  /* Now allocate the temporary storage
   * that I will need for this  character
   */

#if defined(DEBUG_BUILD)
  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 ) {
    OBJECT *fontnameobject;
    charcontext_t *charcontext = char_current_context() ;

    HQASSERT(charcontext, "No character context") ;

    monitorf((uint8 *)"Init\n");
    monitorf((uint8 *)"%f OnePixelX\n", onepixelX);
    monitorf((uint8 *)"%f OnePixelY\n", onepixelY);
    monitorf((uint8 *)"%f UnitPixelsX\n", unitpixelsX);
    monitorf((uint8 *)"%f UnitPixelsY\n", unitpixelsY);
    monitorf((uint8 *)"%d BlueShift\n", blues_str.BlueShift);
    monitorf((uint8 *)"%d BlueFuzz\n", blues_str.BlueFuzz);
    monitorf((uint8 *)"%f BlueScale\n", blues_str.BlueScale);
    monitorf((uint8 *)"%d ForceBold\n", blues_str.ForceBold);
    monitorf((uint8 *)"/%s FillRule\n",
             context->fontsparams->fontfillrule == EOFILL_TYPE
              ? "eofill"
              : "fill") ;

    if ( oType(charcontext->glyphname) == OINTEGER ) {
      /* Get font name */
      fontnameobject = fast_extract_hash_name( &theMyFont(theFontInfo(*gstateptr)), NAME_CIDFontName );
      if (fontnameobject != NULL ) {
        monitorf((uint8 *)"/%.*s CIDFont\n", theINLen(oName(*fontnameobject)),
                 theICList(oName(*fontnameobject)) );
      }
      /* CID font */
      monitorf((uint8 *)"%d Char\n", oInteger(charcontext->glyphname)) ;
    } else {
      /* Get font name */
      fontnameobject = fast_extract_hash_name( &theMyFont(theFontInfo(*gstateptr)), NAME_FontName );
      if (fontnameobject != NULL ) {
        monitorf((uint8 *)"/%.*s Font\n", theINLen(oName(*fontnameobject)),
                 theICList(oName(*fontnameobject)) );
      }
      switch ( charcontext->glyphchar ) {
      case NO_CHARCODE:
        HQASSERT(oType(charcontext->glyphname) == ONAME,
                 "No glyphname for unencoded char") ;
        monitorf((uint8 *)"/%.*s Char\n",
                 theINLen(oName(charcontext->glyphname)),
                 theICList(oName(charcontext->glyphname))) ;
        break ;
      case '(': case ')': case '\\':
        monitorf((uint8 *)"(\\%c) Char\n", charcontext->glyphchar) ;
        break ;
      default:
        monitorf((uint8 *)"(%c) Char\n", charcontext->glyphchar) ;
        break ;
      }
    }
    monitorf((uint8 *)"InitDone\n");
  }
#endif

  *buildnew = ch_template ;
  buildnew->data = &t1hint_info ;

  return ch_build_path(context, methods, stringo, decoder,
                       &t1hint_info.pathfns, buildtop,
                       path, xwidth, ywidth) ;
}

/* ---------------------------------------------------------------------- */
/** Compute the dislocation that a stemhint should cause for points
   inside the stem. The offset is used for fonts with non-zero baselines;
   stems are adjusted AS IF they were located at stem->z + offset.
*/

static void ComputeStem(stem_t *tstem, double offset, USERVALUE t1stemsnap)
{
  int32 idealwidth, otherwidth ;
  ch_float stemstart, stemwidth;
  int32 i;
  SYSTEMVALUE stemshift;
  SYSTEMVALUE widthdiff; /* Number of character space units to adjust width */
  SYSTEMVALUE absdiff;
  SYSTEMVALUE tmpx; /* for NEAREST */
  SYSTEMVALUE onepixel, unitpixels ;
  Bool vertical = (tstem->vertical == STEM_V) ;
  Bool standard ; /* Stem is standard width? */

  /************************************************/
  /* DETERMINE ORIENTATION OF CHARACTER ON DEVICE */
  /************************************************/

  /* Determine orientation of stem */
  stemstart = tstem->z;
  stemwidth = tstem->dz;
  onepixel = ONEPIXEL(vertical) ;
  unitpixels = UNITPIXELS(vertical) ;

  HQASSERT(stemwidth >= 0, "Stem width is negative") ;

  /**********************/
  /* ADJUST STEM WIDTHS */
  /**********************/

  widthdiff = 0.0;
  absdiff = 10000.0; /* large initial rogue value */

  /* Find standard stem with smallest width difference from this stem */
  if (vertical) { /* vertical stem */
    if (blues_str.StdVW != 0.0) { /* there is an entry for StdVW */
      widthdiff = blues_str.StdVW - stemwidth;
      absdiff = fabs( widthdiff );
    }
    for (i = 0; i < blues_str.numStemSnapV; ++i) { /* now look at StemSnapV */
      SYSTEMVALUE z = blues_str.StemSnapV[i] - stemwidth;
      SYSTEMVALUE az = fabs(z) ;
      if ( az < absdiff ) {
        /* this standard width is the best match so far for this stem */
        widthdiff = z;
        absdiff = az;
      }
    }
  } else {                      /* horizontal stem */
    if (blues_str.StdHW != 0.0) { /* there is an entry for StdHW */
      widthdiff = blues_str.StdHW - stemwidth;
      absdiff = fabs( widthdiff );
    }
    for (i = 0; i < blues_str.numStemSnapH; ++i) { /* now look at StemSnapH */
      SYSTEMVALUE z = blues_str.StemSnapH[i] - stemwidth;
      SYSTEMVALUE az = fabs(z) ;
      if ( az < absdiff ) {
        /* this standard width is the best match so far for this stem */
        widthdiff = z;
        absdiff = az;
      }
    }
  }

  /* Only expand or contract stems if they differ by less than half a pixel
     from the closest standard width, otherwise make the width difference =
     0. */
  standard = TRUE ;
  if ( absdiff * 2.0 > onepixel ) {
    widthdiff = 0.0;
    standard = FALSE ;
  }

  /* Expand or contract stem to the NEAREST integral number of pixels. Ensure
     that all stems are at least one pixel wide, unless the stem was
     specified as zero pixels (in which case they probably meant it; edge
     stems can be implemented by zero-width stems). otherwidth is the
     alternate rounding of the corrected stem width. If a stem is snapped to
     a standard width, we do not allow an alternate rounding (it could make
     uneven variations across multiple characters). If a stem is close to the
     ideal width, we do not allow an alternate rounding. */
  if ( stemwidth > EPSILON ) {
    SYSTEMVALUE tweak = t1stemsnap ;
    ch_float pixelwidth = (stemwidth + widthdiff) * unitpixels ;

    if (tweak == TYPE1STEMSNAPDISABLED)  tweak = 0 ;

    {
      SYSTEMVALUE tweakedwidth = pixelwidth - tweak ;
      if (tweakedwidth < 0)  tweakedwidth = 0 ;

      idealwidth = otherwidth = NEAREST(tweakedwidth);
    }

    if ( idealwidth == 0 ) {
      /* Force very thin stems to one pixel, with no alternate allowed. */
      idealwidth = otherwidth = 1;
    } else if ( !standard ) {
      /* Stems closer than 0.25 pixel to the ideal width do not have an
         alternate rounding. This number is chosen arbitrarily. */
      if ( pixelwidth - idealwidth > 0.25 )
        otherwidth = idealwidth + 1 ;
      else if ( pixelwidth - idealwidth < -0.25 )
        otherwidth = idealwidth - 1 ;
    }

    /* Apply ForceBold to vertical stems. */
    if ( blues_str.ForceBold && vertical ) {
      /* Force this vertical stem to be at least DEFAULTBOLDSTEMWIDTH wide. */
      if (idealwidth < DEFAULTBOLDSTEMWIDTH)
        idealwidth = otherwidth = DEFAULTBOLDSTEMWIDTH;
    }
  } else { /* Force indended zero-width stems to really be zero width */
    idealwidth = otherwidth = 0 ;
    stemwidth = 0.0 ;
  }

  /* Store the rounded number of pixels in the stem structure */
  tstem->idealwidth = idealwidth ;
  tstem->otherwidth = otherwidth ;

  /* Now compute the number of character space units necessary */
  widthdiff = idealwidth * onepixel - stemwidth;

  HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("widthdiff = %f", widthdiff));
  /* Calculate shift required to move the lower stem position (adjusted by
     half the width difference) to a pixel boundary. The stem is rounded AS
     IF it were shifted by the baseline shift. Note that the baseline shift
     is not actually added to the stem's position, because this would
     mis-align the stem against any alignment zones. */
  stemshift = stemstart + offset ;
  stemshift = floor((stemshift - widthdiff * 0.5) * unitpixels + 0.5) * onepixel - stemshift ;

  /*********************************************************************/
  /* ALIGNMENT ZONES AND OVERSHOOT SUPPRESSION - HORIZONTAL STEMS ONLY */
  /*********************************************************************/

  if (!vertical) {
    int32 extrai = numalignmentzones;
    ch_float stembottom = stemstart;
    ch_float stemtop = stemstart + stemwidth;

    /* Find out if this stem intersects an alignment zone (the BlueFuzz  */
    /* entry in the Private dictionary specifies the number of character */
    /* units to extend (in both directions) the effect of an alignment   */
    /* zone on a horizontal stem.  The default value of BlueFuzz is 1.   */
    for (i = 0; i < numalignmentzones; ++i) {
      if (alignmentzones[i].topzone) {
        if (stemtop >= alignmentzones[i].bottomy - blues_str.BlueFuzz &&
            stemtop <= alignmentzones[i].topy + blues_str.BlueFuzz &&
            stembottom < alignmentzones[i].bottomy - blues_str.BlueFuzz) {

          /* If any kind of stem snap adjustment is in place, including zero,
             then do correct topzone handling. Otherwise allow the existing
             incorrect topzone handling to continue for old times sake.
           */
          if (t1stemsnap != TYPE1STEMSNAPDISABLED) {
            break ;
          } else {
            /* Look for dodgy topzone / HStem relationship and ignore the
               topzone if we find one.
               In a decently designed font, the stemtop for an HStem should fall
               at or very near to the lower BlueValue for most characters, or
               deliberately at/near the overshoot (higher bluevalue) for others
               like O, C etc.  If the stemtop is away from the Blue, then the
               topzone is considered to be wrong and ignored.
               It's quite possible that the value of STEMFRACTION may need
               tweaking at some point.
             */

            if ((stemtop - alignmentzones[i].bottomy <= EXTRABLUEFUZZ &&
                 stemtop - alignmentzones[i].bottomy <= stemwidth / STEMFRACTION)
                ||
                (alignmentzones[i].topy - stemtop <= EXTRABLUEFUZZ &&
                 alignmentzones[i].topy - stemtop <= stemwidth / STEMFRACTION))
              break; /* We found a top-zone */
            else
              continue; /* Don't want to fall into the next case */
          }
        }
        if (stemtop >= alignmentzones[i].bottomy - EXTRABLUEFUZZ &&
            stemtop <= alignmentzones[i].topy + EXTRABLUEFUZZ &&
            stembottom < alignmentzones[i].bottomy - EXTRABLUEFUZZ)
          extrai = i; /* flag a stretched fit */
      } else {
        if (stembottom <= alignmentzones[i].topy + blues_str.BlueFuzz &&
            stembottom >= alignmentzones[i].bottomy - blues_str.BlueFuzz &&
            stemtop > alignmentzones[i].topy + blues_str.BlueFuzz)
          break; /* We found a bottom-zone */
        if (stembottom <= alignmentzones[i].topy + EXTRABLUEFUZZ &&
            stembottom >= alignmentzones[i].bottomy - EXTRABLUEFUZZ &&
            stemtop > alignmentzones[i].topy + EXTRABLUEFUZZ)
          extrai = i; /* flag a stretched fit */
      }
    }

    if ( i >= numalignmentzones )
      i = extrai; /* no "proper" fit found so use a stretched fit */

    if ( i < numalignmentzones ) {
      /* We found an intersecting zone (number i).*/
      SYSTEMVALUE flatshift, flatposition, flatpospixels, overshoot ;

      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
              ("...Found a Blue Zone[%d]", i));

      /*************************************************/
      /* ALIGN THE FLAT POSITION OF THE ALIGNMENT ZONE */
      /*************************************************/

      /* Compute the position of the alignment zone's flat position in
         device space and the amount of shift needed to align it on a
         pixel boundary. Also compute a non-negative overshoot amount
         (negative overshoots get the normal stem pixel adjustment). */
      if (alignmentzones[i].topzone) {
        flatposition = alignmentzones[i].bottomy;
        overshoot = stemtop - flatposition ;
      } else {
        flatposition = alignmentzones[i].topy;
        overshoot = flatposition - stembottom ;
      }

      /* Find the flat position in pixels */
      flatpospixels = (flatposition + offset) * unitpixels;

      /* Find the stem shift necessary to align the flat position on a pixel
         boundary. The flat position rounding is performed AS IF the flat
         position had the baseline shift applied. Note that the baseline
         shift is not actually added to the flat position, because this would
         mis-align the stem against any alignment zones.*/
      flatshift = (floor(flatpospixels + 0.5) - flatpospixels) * onepixel;

      /************************************************/
      /* HANDLE OVERSHOOT ENFORCEMENT AND SUPPRESSION */
      /************************************************/

      /* When 1 character space unit is rendered smaller than BlueScale
         device units (pixels), we must SUPPRESS overshoots.  Otherwise,
         if the top (or bottom) of this stem is more than BlueShift character
         space units away from the flat position, we must ENFORCE overshoot.
         Otherwise, we should align the stem to the grid normally. */
      if ( unitpixels < blues_str.BlueScale ) {
        /* SUPPRESS overshoot by aligning the stem to the alignment zone's
           flat position. */
        HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
                ("...suppress overshoot of %f", overshoot));

        if (alignmentzones[i].topzone)
          stemshift = flatshift - overshoot - widthdiff ;
        else
          stemshift = flatshift + overshoot;
      } else if ( overshoot >= blues_str.BlueShift ) {
        /* ENFORCE overshoot by shifting the entire stem (if necessary) so that
           it falls at least one pixel beyond the flat position. */
        HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
                ("...enforce overshoot of %f", overshoot));

        if (overshoot < onepixel) {
          if (alignmentzones[i].topzone)
            stemshift = flatshift - overshoot + onepixel - widthdiff ;
          else
            stemshift = flatshift + overshoot - onepixel;
        }
      }

      /************************************************************/
      /* COMPUTE HINT VALUES FOR EACH SIDE OF THE HORIZONTAL STEM */
      /************************************************************/

      /* If the stem was aligned by a topzone, we expand or contract the stem
         only at the bottom - since the stem top was aligned by the zone. If
         the stem was aligned by a bottomzone, we expand or contract the stem
         only at the top - since the stem bottom was aligned by the zone. */
      if (alignmentzones[i].topzone) {
        tstem->fixed = HINT_EDGE_HIGH ;      /* top edge is fixed */
      } else {
        tstem->fixed = HINT_EDGE_LOW ;       /* bottom edge is fixed */
      }
    } /* endif (i < numalignmentzones) */

    /* We didn't find any alignment zones intersecting this stem, so
       proceed with normal stem alignment below. */

  } /* endif (!vertical) */

  /* Adjust the boundaries of the stem */
  tstem->lo_delta = stemshift ;                 /* left or bottom */
  tstem->hi_delta = stemshift + widthdiff ;     /* right or top */

#if defined(ASSERT_BUILD)
  tmpx = (tstem->z + tstem->lo_delta + offset) * unitpixels ;
  HQASSERT(fabs(floor(tmpx + 0.5) - tmpx) < EPSILON,
           "Stem low edge not adjusted to pixel boundary") ;

  tmpx = (tstem->z + tstem->dz + tstem->hi_delta + offset) * unitpixels ;
  HQASSERT(fabs(floor(tmpx + 0.5) - tmpx) < EPSILON,
           "Stem high edge not adjusted to pixel boundary") ;
#endif
}

/* ---------------------------------------------------------------------- */
/** Find the vertical and horizontal stems that the current point
   (x, y) may be involved in.  At most one horizontal and one vertical
   stem can apply to a single point, since there are no overlaps
   allowed.
     The actual hintvalue is returned as a location.
   Hints are ignored inside a DotSection.
*/
static void stem_interpolate(stem_t **stem_list, ch_float z,
                             ch_float *zdiff, Bool *zvalid)
{
  stem_t *zstem ;

  if ( (zstem = stem_find_point(stem_list, z)) != NULL ) {
    /* Interpolate proportionately to position in stem. Zero-width stems
       just use the lo delta (the high delta should be identical). */
    if ( zstem->dz > EPSILON ) {
      ch_float prop = (z - zstem->z) / zstem->dz ;
      HQASSERT(prop >= 0.0 && prop <= 1.0,
               "Stem interpolation exceeds valid range") ;
      *zdiff = zstem->lo_delta * (1.0 - prop) + zstem->hi_delta * prop ;
    } else {
      HQASSERT(zstem->hi_delta == zstem->lo_delta,
               "Zero-width stem has different hi and lo delta") ;
      *zdiff = zstem->lo_delta ;
    }
    *zvalid = TRUE;
  } else if ( (zstem = *stem_list) != NULL ) {
    stem_t *next ;
    if ( z < zstem->z ) {
      /* Before first stem, move by the low delta of the first stem */
      HQASSERT(zstem->prev == NULL,
               "First stem position does not match ordering") ;
      *zdiff = zstem->lo_delta ;
    } else if ( (next = zstem->next) == NULL ) {
      /* After last stem, move by the high delta of the last stem */
      HQASSERT(z - zstem->z > zstem->dz,
               "Last stem position does not match ordering") ;
      *zdiff = zstem->hi_delta ;
    } else {
      /* Between two stems; move proportionate to edge difference between
         them. */
      ch_float cntrwidth = next->z - zstem->z - zstem->dz ;
      if ( cntrwidth > EPSILON ) {
        ch_float prop = (z - zstem->z - zstem->dz) / cntrwidth ;
        HQASSERT(prop >= 0.0 && prop <= 1.0,
                 "Counter interpolation exceeds valid range") ;
        *zdiff = zstem->hi_delta * (1.0 - prop) + next->lo_delta * prop ;
      } else {
        /* Original counter was zero width. Arbitrarily choose one edge. */
        HQTRACE((debug_hints & DEBUG_HINT_FAILURE) != 0,
                ("Zero-width counter")) ;
        *zdiff = zstem->hi_delta ;
      }
    }
  }
}

static void hint_point(t1hint_info_t *info, ch_float x, ch_float y, hint_t *p)
{
  /* Check for stems and then apply the hints */
  p->xvalid = p->yvalid = FALSE;
  p->x = p->y = 0.0;

  if (info->InDotSection) {
#if defined(DEBUG_BUILD)
    if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 )
      monitorf((uint8 *)"%% In dot section, not hinting %d %d\n", x, y);
#endif

    return; /* do not hint inside dot sections */
  }

  stem_interpolate(&info->stems[STEM_V][STEM_ACTIVE], x, &p->x, &p->xvalid) ;
  stem_interpolate(&info->stems[STEM_H][STEM_ACTIVE], y, &p->y, &p->yvalid) ;

  HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("dest.x = %g, dest.y = %g", p->x, p->y ));
}

/* ---------------------------------------------------------------------- */
/** Stem finding functions find a stem and also set it as the head of the stem
   list if found. */
static stem_t *stem_find(stem_t **stem_list, ch_float z, ch_float dz, uint32 edge)
{
  stem_t *stem ;

  HQASSERT(stem_list, "No stem list") ;

  if ( (stem = *stem_list) != NULL ) {
    /* Stems are sorted in ascending order from the left/bottom edges, and
       lowest to highest width for the same shared edge. */
    if ( z > stem->z ) {
      while ( (stem = stem->next) != NULL && z != stem->z ) {
        if ( z < stem->z )
          return NULL ;
      }
    } else if ( z < stem->z ) {
      while ( (stem = stem->prev) != NULL && z != stem->z ) {
        if ( z > stem->z )
          return NULL ;
      }
    }

    if ( stem != NULL ) {
      /* At this point, we have a stem which matches the left/bottom edge.
         Test if any of the stems with this edge match the delta. */
      if ( dz > stem->dz ) {
        while ( (stem = stem->next) != NULL && dz != stem->dz ) {
          if ( dz < stem->dz )
            return NULL ;
        }
      } else if ( dz < stem->dz ) {
        while ( (stem = stem->prev) != NULL && dz != stem->dz ) {
          if ( dz > stem->dz )
            return NULL ;
        }
      }

      if ( stem != NULL ) {
        /* Matched both z and dz exactly. Now match the edge flags; they are
           sorted for convenience of access, even though the sorting order is
           meaningless (though a nice side-effect is that non-edge stems sort
           before edge stems). */
        if ( edge > stem->edge ) {
          while ( (stem = stem->next) != NULL && edge != stem->edge ) {
            if ( edge < stem->edge )
              return NULL ;
          }
        } else if ( edge < stem->edge ) {
          while ( (stem = stem->prev) != NULL && edge != stem->edge ) {
            if ( edge > stem->edge )
              return NULL ;
          }
        }

        if ( stem != NULL ) {
          /* Found the exact same stem already */
          *stem_list = stem ;
          return stem ;
        }
      }
    }
  }

  return NULL ;
}

/** Find the active stem a point is in. If the point is not in a stem, we
   leave the stem list at the stem before the point, so that counters can be
   interpolated easily. */
static stem_t *stem_find_point(stem_t **stem_list, ch_float z)
{
  stem_t *stem ;

  HQASSERT(stem_list, "No stem list") ;

  if ( (stem = *stem_list) != NULL ) {
    /* Stems are sorted in ascending order from the left/bottom edges, and
       lowest to highest width for the same shared edge. For active stems,
       there should be no overlaps, so we'll take the first stem that
       includes this point. */
    if ( z < stem->z ) {
      while ( (stem = stem->prev) != NULL ) {
        *stem_list = stem ;
        if ( z >= stem->z ) {
          if ( z - stem->z <= stem->dz )
            return stem ;

          return NULL ;
        }
      }
    } else if ( z - stem->z > stem->dz ) {
      while ( (stem = stem->next) != NULL ) {
        if ( z - stem->z <= stem->dz ) {
          if ( z >= stem->z ) {
            *stem_list = stem ;
            return stem ;
          }

          return NULL ;
        }
        *stem_list = stem ;
      }
    } else {
      *stem_list = stem ;
      return stem ;
    }
  }

  return NULL ;
}

static stem_t *stem_find_index(stem_t **stem_list, int32 index)
{
  stem_t *stem ;

  HQASSERT(stem_list, "No stem list") ;
  HQASSERT(index >= 0, "Stem index invalid") ;

  if ( (stem = *stem_list) != NULL ) {
    /* Type 2 stems must be introduced in ascending order. Type 1 stems are
       often introduced in ascending order. Use this to optimise the search
       direction. */
    if ( index > stem->index ) {
      /* Search forwards first */
      while ( (stem = stem->next) != NULL ) {
        if ( stem->index == index ) {
          *stem_list = stem ;
          return stem ;
        }
      }
      stem = *stem_list ;
      while ( (stem = stem->prev) != NULL ) {
        if ( stem->index == index ) {
          *stem_list = stem ;
          return stem ;
        }
      }
    } else if ( index < stem->index ) {
      /* Search backwards first */
      while ( (stem = stem->prev) != NULL ) {
        if ( stem->index == index ) {
          *stem_list = stem ;
          return stem ;
        }
      }
      stem = *stem_list ;
      while ( (stem = stem->next) != NULL ) {
        if ( stem->index == index ) {
          *stem_list = stem ;
          return stem ;
        }
      }
    } else {
      *stem_list = stem ;
      return stem ;
    }
  }

  return NULL ;
}

static stem_t *stem_find_group(stem_t **stem_list, uint32 group)
{
  stem_t *stem, *found ;

  HQASSERT(stem_list, "No stem list") ;
  HQASSERT(group > 0, "Stem group invalid") ;

  for ( stem = *stem_list ; stem ; stem = stem->next ) {
    if ( stem->group == group ) {
      *stem_list = stem ;
      return stem ;
    }
  }

  found = NULL ;
  for ( stem = *stem_list ; stem ; stem = stem->prev ) {
    /* N.B. No return when going backwards; we want to find the first in the
       list so that subsequent forward searches are faster. */
    if ( stem->group == group )
      *stem_list = found = stem ;
  }

  return found ;
}

/** Remove the head of a stem list, leaving the next element (preferentially,
   the previous if there is no next) as the head of the list. */
static void stem_remove(stem_t **stem_list)
{
  stem_t *stem ;

  HQASSERT(stem_list, "No stem list") ;

  stem = *stem_list ;
  HQASSERT(stem, "No stem to remove") ;

  if ( (*stem_list = stem->next) != NULL ) {
    stem->next->prev = stem->prev ;
    if ( stem->prev != NULL )
      stem->prev->next = stem->next ;
  } else if ( (*stem_list = stem->prev) != NULL ) {
    stem->prev->next = stem->next ;
    if ( stem->next != NULL )
      stem->next->prev = stem->prev ;
  }

  stem->next = stem->prev = NULL ; /* For safety */
}

static void stem_add(stem_t **stem_list, stem_t *stem)
{
  stem_t *before, *after ;

  HQASSERT(stem_list, "No stem list") ;
  HQASSERT(stem, "No stem to add to list") ;

  /* Stems are sorted in ascending order from the left/bottom edges, and
     lowest to highest width for the same shared edge. Find the stems before
     and after the insertion point for this stem. */
  if ( (before = after = *stem_list) != NULL ) {
    if ( stem->z < before->z ||
         (stem->z == before->z &&
          (stem->dz < before->dz ||
           (stem->dz == before->dz && stem->edge < before->edge))) ) {
      do {
        after = before ;
        before = before->prev ;
      } while ( before &&
                (stem->z < before->z ||
                 (stem->z == before->z &&
                  (stem->dz < before->dz ||
                   (stem->dz == before->dz && stem->edge < before->edge)))) ) ;
    } else if ( stem->z > after->z ||
                (stem->z == after->z &&
                 (stem->dz > after->dz ||
                  (stem->dz == after->dz && stem->edge > after->edge))) ) {
      do {
        before = after ;
        after = after->next ;
      } while ( after &&
                (stem->z > after->z ||
                 (stem->z == after->z &&
                  (stem->dz > after->dz ||
                   (stem->dz == after->dz && stem->edge > after->edge)))) ) ;
    } else {
      HQFAIL("Inserting same stem as list head") ;
      return ; /* For safety */
    }
  }

  if ( (stem->prev = before) != NULL )
    before->next = stem ;
  if ( (stem->next = after) != NULL )
    after->prev = stem ;

  *stem_list = stem ;

  HQASSERT(stems_assert_valid(stem),
           "Stem list not valid after inserting new entry") ;
}

/* Find and return first stem in list, resetting head of list. */
stem_t *stem_find_first(stem_t **stem_list)
{
  stem_t *first = NULL, *stem ;

  HQASSERT(stem_list, "No stem list to find first") ;

  if ( (stem = *stem_list) != NULL ) {
    do {
      first = stem ;
      stem = stem->prev ;
    } while ( stem != NULL ) ;
    *stem_list = first ;
  }

  return first ;
}

#if defined(ASSERT_BUILD)
static Bool stems_assert_valid(stem_t *stems)
{
  stem_t *prev, *next ;

  while ( (prev = stems->prev) != NULL )
    stems = prev ;

  while ( (next = stems->next) != NULL ) {
    if ( stems->prev != prev ) {
      HQFAIL("Stem list not doubly linked correctly") ;
      return FAILURE(FALSE) ;
    }
    if ( stems->z > next->z ||
         (stems->z == next->z &&
          (stems->dz > next->dz ||
           (stems->dz == next->dz && stems->edge >= next->edge))) ) {
      HQFAIL("Stem list not sorted correctly") ;
      return FAILURE(FALSE) ;
    }

    prev = stems ;
    stems = next ;
  }

  return TRUE ;
}
#endif

/* ---------------------------------------------------------------------- */
static Bool VHStem(t1hint_info_t *info, uint32 vh,
                   ch_float z, ch_float dz, uint32 edge, int32 index)
{
  register stem_t *tstem;
  stem_t **stem_from, **stem_to ;

  HQASSERT(vh == STEM_H || vh == STEM_V,
           "Stem is neither horizontal nor vertical") ;
  HQASSERT(dz >= 0, "Stem width is negative") ;

  HQASSERT(stem_find_index(&info->stems[STEM_H][STEM_ACTIVE], index) == NULL &&
           stem_find_index(&info->stems[STEM_H][STEM_INACTIVE], index) == NULL &&
           stem_find_index(&info->stems[STEM_V][STEM_ACTIVE], index) == NULL &&
           stem_find_index(&info->stems[STEM_V][STEM_INACTIVE], index) == NULL,
           "Stem already exists with index") ;

  stem_from = &info->stems[vh][STEM_INACTIVE] ;
  stem_to = &info->stems[vh][STEM_ACTIVE] ;

  if ( (tstem = stem_find(stem_from, z, dz, edge)) != NULL ) {
    HQASSERT(*stem_from == tstem, "Found stem not moved to head of list") ;
    HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
            ("Re-activating stem %d: %f +%f",
             tstem->index, tstem->z, tstem->dz)) ;
    stem_remove(stem_from) ;
    stem_add(stem_to, tstem) ;
  } else if ( (tstem = stem_find(stem_to, z, dz, edge)) != NULL ) {
    /* Stems should not be defined more than once. Unfortunately, some
       Adobe font developers cannot read their own spec, and do this (e.g.
       Frutiger-UltraBlack). In this case, we will update the stem index to
       the latest number, and lose the ability to control the first
       stem. */
    HQASSERT(*stem_to == tstem, "Found stem not moved to head of list") ;
    HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
            ("Duplicate stem %d: %f +%f",
             tstem->index, tstem->z, tstem->dz)) ;
    tstem->index = index ;
  } else {
    /* Create a new stem. If the pre-allocated stems have run out, rely
       on the MM system to be fast enough to allocate extra stems. */
    if ( info->numstems < FIXED_STEMS ) {
      tstem = &info->prealloc_stems[info->numstems];
      tstem->prealloc = TRUE ;
    } else {
      if ( (tstem = mm_alloc(mm_pool_temp, sizeof(stem_t),
                             MM_ALLOC_CLASS_STEM_BLOCK)) == NULL )
        return error_handler(VMERROR) ;
      tstem->prealloc = FALSE ;
    }

    tstem->vertical = vh ;
    tstem->z = z;
    tstem->dz = dz;
    tstem->lo_delta = 0.0 ;
    tstem->hi_delta = 0.0 ;
    tstem->index = index ;
    tstem->group = 0 ; /* Group 0 never used by cntrmask */
    tstem->edge = edge ;
    tstem->fixed = 0 ;
    tstem->prev = NULL ;
    tstem->next = NULL ;

    ComputeStem(tstem, (vh == STEM_H) ? info->baselineDy : 0.0,
                info->t1stemsnap);

    HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
            (" lo_del %g, hi_del %g", tstem->lo_delta, tstem->hi_delta ));

    stem_add(stem_to, tstem) ;

    info->numstems++;
  }

#if defined(DEBUG_BUILD)
  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 )
    DumpStem(tstem, FALSE);
#endif

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static void Hint_Move_and_Line(t1hint_info_t *info,
                               ch_float *dx_new, ch_float *dy_new)
{
  hint_t new_hint ;

  ch_float dx = *dx_new + info->sidebearingX ;
  ch_float dy = *dy_new + info->sidebearingY;

  hint_point(info, dx, dy, &new_hint);

  *dx_new = dx + new_hint.x;
  *dy_new = dy + new_hint.y + info->baselineDy;

  HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
          ("point: ( %f %f ) -> ( %f %f )", dx, dy, *dx_new, *dy_new ));

#if defined(DEBUG_BUILD)
  info->last_hint = new_hint ;
  info->last_hint.x = *dx_new ;
  info->last_hint.y = *dy_new ;

  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 ) {
    monitorf((uint8 *)"%f %f OrigPoint\n", dx, dy);
    monitorf((uint8 *)"%f %f Point\n", *dx_new, *dy_new);
  }
#endif
}

/* ---------------------------------------------------------------------- */
static Bool t1hint_object_integer(OBJECT *object, int32 *value, int32 bad)
{
  SYSTEMVALUE num ;

  if ( !object_get_numeric(object, &num) )
    return FALSE ;

  if ( !intrange(num) )
    *value = bad ;
  else
    *value = (int32)num ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** The hinting information uses the Type 1/2 charstring get_info method to
   get details of the blues structures. Most errors are ignored. */
static Bool get_blues(charstring_methods_t *charfns)
{
  int32 i;
  OBJECT info = OBJECT_NOTVM_NOTHING ;
  int32 UniqueID = -1, FID = 0, FDIndex = -1 ;

  HQASSERT(charfns, "No charstring methods to get information") ;

#if defined(DEBUG_BUILD)
  {
    static int32 init = 0;
    if ( init != debug_hints ) {
      if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 )
        monitorf((uint8 *)"/HintDebug /ProcSet findresource pop\n");
      init = debug_hints ;
    }
  }
#endif

  /* Get the subfont identifying information. If there is a UniqueID, then
     use that and the FDIndex. Otherwise, use the FID and FDIndex to identify
     the sub-font. If any element doesn't match, then we have to re-load all
     of the blues. UniqueID should fit in 24 bits; PLRM3, 5.6.1, p.536. */
  if ( (*charfns->get_info)(charfns->data, NAME_UniqueID, -1, &info) &&
       oType(info) == OINTEGER && UniqueID >= 0 ) {
    UniqueID = oInteger(info) ;
  } else if ( (*charfns->get_info)(charfns->data, NAME_FID, -1, &info) &&
              oType(info) == OINTEGER ) {
    FID = oInteger(info) ;
    HQASSERT(FID != 0, "Invalid Font ID returned by charstring method") ;
  }

  if ( (*charfns->get_info)(charfns->data, NAME_SubFont, -1, &info) &&
       oType(info) == OINTEGER ) {
    FDIndex = oInteger(info) ;
  }

  if ( UniqueID != blues_str.UniqueID ||
       FID != blues_str.FID ||
       FDIndex != blues_str.FDIndex ) {
    blues_str.UniqueID = UniqueID ;
    blues_str.FID = FID ;
    blues_str.FDIndex = FDIndex ;

    /* init everything to defaults now before attempting to extract */
    blues_str.numBlueValues =
      blues_str.numOtherBlues =
      blues_str.numFamilyBlues =
      blues_str.numFamilyOtherBlues =
      blues_str.numStemSnapH =
      blues_str.numStemSnapV = 0;

    blues_str.BlueShift = DEFAULTBLUESHIFT;
    blues_str.BlueFuzz = DEFAULTBLUEFUZZ;
    blues_str.ForceBold = DEFAULTFORCEBOLD;
    blues_str.LanguageGroup = DEFAULTLANGUAGEGROUP;
    blues_str.RndStemUp = DEFAULTRNDSTEMUP;
    blues_str.BlueScale = DEFAULTBLUESCALE;
    blues_str.ExpansionFactor = DEFAULTEXPANSIONFACTOR;

    blues_str.StdHW = 0.0;
    blues_str.StdVW = 0.0;

    /* Lookup the array "BlueValues". */
    if ( (*charfns->get_info)(charfns->data, NAME_BlueValues, -1, &info) &&
         oType(info) == OINTEGER ) {
      /* Type check the BlueValues. */
      blues_str.numBlueValues = oInteger(info) ;
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("numBlueValues=%i",  blues_str.numBlueValues));
      for ( i = 0;  i < blues_str.numBlueValues && i < NUMBLUEVALUES; ++i ) {
        if ( !(*charfns->get_info)(charfns->data, NAME_BlueValues, i, &info) ||
             !t1hint_object_integer(&info, &blues_str.BlueValues[i], 32768) )
          return FALSE ;
        HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ( "BlueValue of=%i",  blues_str.BlueValues[i] ));
      }
    }

    /* Lookup the array "OtherBlues". */
    if ( (*charfns->get_info)(charfns->data, NAME_OtherBlues, -1, &info) &&
         oType(info) == OINTEGER ) {
      /* Type check the OtherBlues. */
      blues_str.numOtherBlues = oInteger(info);
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("numOtherBlues=%i",  blues_str.numOtherBlues));
      for ( i = 0; i < blues_str.numOtherBlues && i < NUMOTHERBLUES; ++i ) {
        if ( !(*charfns->get_info)(charfns->data, NAME_OtherBlues, i, &info) ||
             !t1hint_object_integer(&info, &blues_str.OtherBlues[i], 32768) )
          return FALSE ;
        HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ( "OtherBlue of=%i",  blues_str.OtherBlues[i] ));
      }
    }

    /* Lookup the array "FamilyBlues". */
    if ( (*charfns->get_info)(charfns->data, NAME_FamilyBlues, -1, &info) &&
         oType(info) == OINTEGER ) {
      /* Type check the FamilyBlues. */
      blues_str.numFamilyBlues = oInteger(info);
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("numFamilyBlues=%i",  blues_str.numFamilyBlues));
      for ( i = 0; i < blues_str.numFamilyBlues && i < NUMFAMILYBLUES; ++i ) {
        if ( !(*charfns->get_info)(charfns->data, NAME_FamilyBlues, i, &info) ||
             !t1hint_object_integer(&info, &blues_str.FamilyBlues[i], 32768) )
          return FALSE ;
        HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ( "FamilyBlue of=%i",  blues_str.FamilyBlues[i] ));
      }
    }

    /* Lookup the array "FamilyOtherBlues". */
    if ( (*charfns->get_info)(charfns->data, NAME_FamilyOtherBlues, -1, &info) &&
         oType(info) == OINTEGER ) {
      /* Type check the FamilyOtherBlues. */
      blues_str.numFamilyOtherBlues = oInteger(info);
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("numFamilyOtherBlues=%i", blues_str.numFamilyOtherBlues));
      for ( i = 0;  i < blues_str.numFamilyOtherBlues && i < NUMFAMILYOTHERBLUES; ++i ) {
        if ( !(*charfns->get_info)(charfns->data, NAME_FamilyOtherBlues, i, &info) ||
             !t1hint_object_integer(&info, &blues_str.FamilyOtherBlues[i], 32768) )
          return FALSE ;
        HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ( "FamilyOtherBlue of=%i",  blues_str.FamilyOtherBlues[i] ));
      }
    }

    /* Lookup the real "BlueScale". */
    if ( (*charfns->get_info)(charfns->data, NAME_BlueScale, -1, &info) &&
         oType(info) != ONULL ) {
      if ( !object_get_numeric(&info, &blues_str.BlueScale) )
        return FALSE ;
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("BlueScale=%f",  blues_str.BlueScale));
    }

    /* Lookup the int "BlueShift". */
    if ( (*charfns->get_info)(charfns->data, NAME_BlueShift, -1, &info) &&
         oType(info) != ONULL ) {
      if ( !t1hint_object_integer(&info, &blues_str.BlueShift, 7) )
        return FALSE ;
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("BlueShift=%i",  blues_str.BlueShift));
    }

    /* Lookup the int "BlueFuzz". */
    if ( (*charfns->get_info)(charfns->data, NAME_BlueFuzz, -1, &info) &&
         oType(info) != ONULL ) {
      if ( !t1hint_object_integer(&info, &blues_str.BlueFuzz, 0) )
        return FALSE ;
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("BlueFuzz=%i",  blues_str.BlueFuzz));
    }

    /* Lookup the array "StdHW". N.B. automatically look for first entry
       of array. */
    if ( (*charfns->get_info)(charfns->data, NAME_StdHW, 0, &info) &&
         oType(info) != ONULL ) {
      /* Type check the StdHW. */
      if ( !object_get_numeric(&info, &blues_str.StdHW) )
        return FALSE ;
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("StdHW=%f",  blues_str.StdHW));
    }

    /* Lookup the array "StdVW". N.B. automatically look for first entry
       of array. */
    if ( (*charfns->get_info)(charfns->data, NAME_StdVW, 0, &info) &&
         oType(info) != ONULL ) {
      /* Type check the StdVW. */
      if ( !object_get_numeric(&info, &blues_str.StdVW) )
        return FALSE ;
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("StdVW=%f",  blues_str.StdVW));
    }

    /* Lookup the array "StemSnapH". */
    if ( (*charfns->get_info)(charfns->data, NAME_StemSnapH, -1, &info) &&
         oType(info) == OINTEGER ) {
      /* Type check the StemSnapH. */
      blues_str.numStemSnapH = oInteger(info);
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("numStemSnapH=%i",  blues_str.numStemSnapH));
      for ( i = 0; i < blues_str.numStemSnapH && i < NUMSTEMSNAPH; ++i ) {
        if ( !(*charfns->get_info)(charfns->data, NAME_StemSnapH, i, &info) ||
             !object_get_numeric(&info, &blues_str.StemSnapH[i]) )
          return FALSE ;
        HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, (" stemSnapH[%d]=%g", i, blues_str.StemSnapH[i]));
      }
    }

    /* Lookup the array "StemSnapV". */
    if ( (*charfns->get_info)(charfns->data, NAME_StemSnapV, -1, &info) &&
         oType(info) == OINTEGER ) {
      /* Type check the StemSnapV. */
      blues_str.numStemSnapV = oInteger(info);
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("numStemSnapV=%i",  blues_str.numStemSnapV));
      for ( i = 0; i < blues_str.numStemSnapV && i < NUMSTEMSNAPV; ++i ) {
        if ( !(*charfns->get_info)(charfns->data, NAME_StemSnapV, i, &info) ||
             !object_get_numeric(&info, &blues_str.StemSnapV[i]) )
          return FALSE ;
        HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, (" stemSnapV[%d]=%g", i, blues_str.StemSnapV[i]));
      }
    }

    /* Lookup the Boolean "ForceBold". */
    if ( (*charfns->get_info)(charfns->data, NAME_ForceBold, -1, &info) &&
         oType(info) == OBOOLEAN ) {
      /* Type check the ForceBold. */
      blues_str.ForceBold = oBool(info);
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("ForceBold=%i",  blues_str.ForceBold));
    }

    /* Lookup the int "LanguageGroup". */
    if ( (*charfns->get_info)(charfns->data, NAME_LanguageGroup, -1, &info) &&
         oType(info) == OINTEGER ) {
      blues_str.LanguageGroup = oInteger(info);
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("LanguageGroup=%f",  blues_str.LanguageGroup));
    }

    /* Lookup the Boolean "RndStemUp". */
    if ( (*charfns->get_info)(charfns->data, NAME_RndStemUp, -1, &info) &&
         oType(info) == OBOOLEAN ) {
      /* Type check the RndStemUp. */
      blues_str.RndStemUp = oBool(info);
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("RndStemUp=%i",  blues_str.RndStemUp));
    }

    /* Lookup the real "ExpansionFactor". */
    if ( (*charfns->get_info)(charfns->data, NAME_ExpansionFactor, -1, &info) &&
         oType(info) != ONULL ) {
      if ( !object_get_numeric(&info, &blues_str.ExpansionFactor) )
        return FALSE ;
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0, ("ExpansionFactor=%f",  blues_str.ExpansionFactor));
    }

    /* New Blues means new alignment zones */
    alignmentzone_change = TRUE ;
  }

  if ( alignmentzone_change ) {
    SYSTEMVALUE bluezonepixels, familyzonepixels ;

    /* We've read all we want to from the font, now set up the alignment zone
       structures */
    numalignmentzones = 0;     /* initialize total # of zones */
    baseline_index = -1 ;

    /* do the BlueValues zones */
    for (i = 0; i < blues_str.numBlueValues; i +=2, ++numalignmentzones) {
      /* the 0th & 1st numbers in BlueValues are for a bottom zone */
      /* the rest are topzones */
      if (i == 0) {          /* bottom zone */
        alignmentzones[numalignmentzones].topzone = FALSE;
        baseline_index = 0 ; /* first pair is baseline overshoot & position */
      } else                 /* top zone */
        alignmentzones[numalignmentzones].topzone = TRUE;

      if (i < blues_str.numFamilyBlues) {    /* we must consider FamilyBlues */
        bluezonepixels = (blues_str.BlueValues[i] - blues_str.BlueValues[i+1]) * unitpixelsY ;
        familyzonepixels = (blues_str.FamilyBlues[i] - blues_str.FamilyBlues[i+1]) * unitpixelsY ;

        /* is the difference in size of the zones less than 1 pixel? */
        if (fabs(bluezonepixels - familyzonepixels) < 1.0) {
          /* use the Family zones */
          alignmentzones[numalignmentzones].bottomy = blues_str.FamilyBlues[i];
          alignmentzones[numalignmentzones].topy    = blues_str.FamilyBlues[i+1];
          continue;
        }
      }
      /* use this font's Blue zones */
      alignmentzones[numalignmentzones].bottomy = blues_str.BlueValues[i];
      alignmentzones[numalignmentzones].topy    = blues_str.BlueValues[i+1];
    }

    /* do the OtherBlues zones */
    for (i = 0; i < blues_str.numOtherBlues; i +=2, ++numalignmentzones) {
      /* all of the OtherBlues zones are bottom zones */
      alignmentzones[numalignmentzones].topzone = FALSE;
      if (i < blues_str.numFamilyOtherBlues) {/* consider FamilyOtherBlues  */
        bluezonepixels = (blues_str.OtherBlues[i] - blues_str.OtherBlues[i+1]) * unitpixelsY ;
        familyzonepixels = (blues_str.FamilyOtherBlues[i] - blues_str.FamilyOtherBlues[i+1]) * unitpixelsY ;

        /* is the difference in size of the zones less than 1 pixel? */
        if (fabs(bluezonepixels - familyzonepixels) < 1.0) {
          /* use the Family zones */
          alignmentzones[numalignmentzones].bottomy = blues_str.FamilyOtherBlues[i];
          alignmentzones[numalignmentzones].topy    = blues_str.FamilyOtherBlues[i+1];
          continue;
        }
      }
      /* use this font's Blue zones (as opposed to the Family Blues) */
      alignmentzones[numalignmentzones].bottomy = blues_str.OtherBlues[i];
      alignmentzones[numalignmentzones].topy    = blues_str.OtherBlues[i+1];
    }

    alignmentzone_change = FALSE ; /* Done changes to alignment zones */
  }

#if defined(DEBUG_BUILD)
  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 ) {
    for (i = 0; i < numalignmentzones; i++) {
      DumpZone(&alignmentzones[i]);
    }
  }
#endif

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** change_hints() is called when an explicit hint change is done in a Type 1
   character, and also when setbearing() is called. The latter case catches
   SEAC, which could otherwise result in overlapping hints. We deactivate all
   of the stems, but retain their snapping in case the same stem is re-instated
   later. */
static void change_hints(t1hint_info_t *info)
{
  uint32 vh ;

  /* Inactivate all hints when told to change the character hints. Adding
     stems back will re-activate old stems, preserving the previous snapping
     parameters. */
  for ( vh = 0 ; vh < 2 ; ++vh ) {
    stem_t **stems_active = &info->stems[vh][STEM_ACTIVE] ;
    stem_t **stems_inactive = &info->stems[vh][STEM_INACTIVE] ;
    stem_t *stem ;

    while ( (stem = *stems_active) != NULL ) {
      stem_remove(stems_active) ;
      stem_add(stems_inactive, stem) ;
    }

    /* We're expecting a new load of hints. Reset the index numbers on these
       so they won't interfere. If they are re-activated by a stem command,
       they will get a new index. */
    for ( stem = *stems_inactive ; stem ; stem = stem->next )
      stem->index = -1 ;

    for ( stem = *stems_inactive ; stem ; stem = stem->prev )
      stem->index = -1 ;
  }

#if defined(DEBUG_BUILD)
  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 )
    monitorf((uint8 *)"ChangeHints\n");
#endif
}

/** \page counters Counter Hinting
   This routine is called with a list of stems which need their counters
   adjusting. We try to regularise the space between the stems whilst keeping
   within the ExpansionFactor, and preserving the distance between fixed stem
   edges.

   This is a constraint solving problem. Most such problems are NP-complete
   (fairly trivially transformable to satisfiability). Brute-force search for
   a solution has a worst case of 2^(2n - 1) possibilities for n stems, since
   there are n-1 counters, and we will only allow stems and counters to round
   to one of two adjacent pixel widths.

   In our favour, the number of stems per counter group is likely to be
   small, of the order of 3-8 stems, or 5-15 variables in the utility function.

   The constraints weighted in the utility function are (in order of
   importance):

   1. Absolute constraints. A sufficient penalty is added for each violation
      to make the result unusable. Even with the penalties, it is possible to
      determine if a neighbouring solution is better than the current solution.
      1a) Distance between successive fixed stem edges remains constant.
      1b) Total width from first stem edge to last stem edge does not increase
          or decrease by more than ExpansionFactor.

   2. Stem roundings are consistent with other similar stems. Minimises

      Sum_{i=1..n,j=1..n} (s_i' - s_j')^2 forall i,j : round(s_i) = round(s_j)

   3. Stem roundings are closest to ideal roundings. Minimises

      Sum_{i=1..n} (s_i' - s_i)^2

   4. Counter roundings are consistent with other similar counters. Minimises

      Sum_{i=1..m,j=1..m} (c_i' - c_j')^2 forall i,j : round(c_i) = round(c_j)

   5. Counter ratios are consistent with each other. Minimises

      Sum_{i=1..m,j=1..m} (c_i'/mean(c_j') - c_i/mean(c_j))^2

   6. Counter roundings are closest to ideal roundings. Minimises

      Sum_{i=1..m} (c_i' - c_i)^2

   This function uses a local search strategy to adjust from the initial
   condition. An integer is used to represent a set of decisions about the
   alternate widths of counters and stems. A weighting utility function is
   computed for each decision tree, and then changes to each decision are
   tested to see if the fit can be improved. A local minimum is searched for
   by selecting the most improvement in each round.

   The decision tree is represented in a way which will hopefully converge on
   a minimum quickly. The zero decision state represents the case where all
   of the stems and counters are rounded to their "ideal" width. Bits are
   used from the decision variable as necessary to represent:

   1) Alternate roundings of similar counters. (So if there are four counters
      with two different ideal widths and three different alternate
      roundings, there are three decisions and hence three bits used.
      Counters with widths of 1.7 and 2.4 pixels width both have the same
      ideal width of 2, but different alternate roundings of 1 and 3
      respectively.)
   2) Alternate roundings of individual counters. If the "similar counter"
      decision for this counter was taken, then the ideal width rather than the
      alternate rounding is used.
   3) Alternate roundings of similar stems. If there are no alternate roundings
      for a particular stem width, then there is no decision involved and no
      bit used.
   4) Alternate roundings for individual stems. If the "similar stem"
      decision for this stem was taken, then the ideal width rather than the
      alternate rounding is used. If there is no alternate rounding for a
      particular stem width, then there is no decision involved and no
      bit used.

   Since a 32-bit integer is used to represent the decision tree, only the
   first 32 decisions can be changed. Grouping counters and stems into similar
   groups should help minimise the utility function quickly, since there are
   terms for the consistency of stems evaluated in the function. */

#define FIXED_FIT_PENALTY 1000 /* Penalty for not fitting fixed widths */
#define EXPANSION_PENALTY 1000 /* Penalty for exceeding ExpansionFactor */
#define STEM_INCONSISTENT_PENALTY 1 /* Penalty for stem not matching similar */
#define COUNTER_INCONSISTENT_PENALTY 1 /* Penalty for counter not matching similar */

#define STEM_ROUND_WEIGHT 3.0    /* Weight given to bad stem rounding */
#define COUNTER_ROUND_WEIGHT 1.0 /* Weight given to bad counter rounding */
#define COUNTER_RATIO_WEIGHT 2.0 /* Weight given to bad counter ratios */

#define ACCEPT_FIT_LIMIT  1000 /* Acceptable fits are all below this */


/* Unsigned integer type used to represent decision tree */
typedef uint32 counter_choice_t ;
#define MAX_DECISIONS (sizeof(counter_choice_t) * 8)

typedef struct {
  int32 idealwidth ;  /* idealwidth as decided by stem/counter computation */
  int32 otherwidth ;  /* Alternate rounding of stem/counter width */
  uint32 usage ;      /* Number of times this stem/counter appears */
  int32 actualwidth ; /* Width chosen, unless overridden */
} rounding_set_t ;

typedef struct {
  ch_float realwidth ;  /* Correct width in fractional pixels */
  int32 idealwidth ;    /* Ideal width decided by stem/counter computation */
  int32 otherwidth ;    /* Alternate rounding of stem/counter width */
  int32 actualwidth ;   /* Width chosen */
  rounding_set_t *set ; /* Which stem/counter set is this part of? */
} rounding_t ;

/** Struct containing collected information for counter hinting computations */
typedef struct {
  rounding_set_t counterset[MAX_DECISIONS] ;
  rounding_set_t stemset[MAX_DECISIONS] ;
  rounding_t roundings[MAX_DECISIONS * 2] ; /* Stem, counter, stem, ... */
  uint32 nrounds ; /* Number of stems+counters */
  uint32 ncounterset, nstemset ; /* Number of counter and stem sets */
  uint32 ncounterchoice, nstemchoice ; /* Number of individual c/s choices */
  uint32 totalwidth ;  /* snapped width of roundings */
  ch_float realwidth ; /* original width of roundings */
  ch_float hintwidth ; /* original hinted width before counter adjusting */
  ch_float meancounter ; /* mean width of original counters */
  ch_float meanicounter ; /* mean width of rounded counters */
} counter_info_t ;

/** Set the roundings for stems and counters for a particular decision tree */
static void counter_fix(counter_choice_t decisions,
                        counter_info_t *info)
{
  uint32 i ;
  uint32 totalwidth = 0 ;
  uint32 cntrwidth = 0 ;

  HQASSERT(info != NULL, "No counter hinting info") ;

  /* Fix counter histogram rounding decisions */
  for ( i = 0 ; i < info->ncounterset ; ++i ) {
    rounding_set_t *set = &info->counterset[i] ;
    set->actualwidth = (decisions & 1) ? set->otherwidth : set->idealwidth ;
    decisions >>= 1 ;
  }

  /* Fix individual counter rounding decisions */
  for ( i = 1 ; i < info->nrounds ; i += 2 ) {
    rounding_t *rounding = &info->roundings[i] ;
    rounding_set_t *set ;
    int32 actualwidth = rounding->idealwidth ;

    if ( (set = rounding->set) != NULL ) {
      Bool useother = (set->actualwidth != set->idealwidth) ;

      if ( set->usage > 1 ) {
        /* Decision may be specifically overridden if there is more than
           one counter in a set. */
        if ( (decisions & 1) != 0 )
          useother = !useother ;

        decisions >>= 1 ;
      }

      if ( useother )
        actualwidth = rounding->otherwidth ;
    }

    rounding->actualwidth = actualwidth ;
    totalwidth += actualwidth ;
    cntrwidth += actualwidth ;
  }

  /* Mean width of rounded counters */
  info->meanicounter = (ch_float)cntrwidth / (ch_float)(info->nrounds >> 1) ;

  /* Fix individual stem histogram rounding decisions */
  for ( i = 0 ; i < info->nstemset ; ++i ) {
    rounding_set_t *set = &info->stemset[i] ;
    set->actualwidth = (decisions & 1) ? set->otherwidth : set->idealwidth ;
    decisions >>= 1 ;
  }

  /* Fix individual stem rounding decisions */
  for ( i = 0 ; i < info->nrounds ; i += 2 ) {
    rounding_t *rounding = &info->roundings[i] ;
    rounding_set_t *set ;
    int32 actualwidth = rounding->idealwidth ;

    if ( (set = rounding->set) != NULL ) {
      Bool useother = (set->actualwidth != set->idealwidth) ;

      if ( set->usage > 1 ) {
        /* Decision may be specifically overridden if there is more than
           one counter in a set. */
        if ( (decisions & 1) != 0 )
          useother = !useother ;

        decisions >>= 1 ;
      }

      if ( useother )
        actualwidth = rounding->otherwidth ;
    }

    rounding->actualwidth = actualwidth ;
    totalwidth += actualwidth ;
  }

  info->totalwidth = totalwidth ;
}

/** Evaluate the utility function that we will try to optimise. */
static double counter_evaluate(stem_t *cntrstems,
                               counter_choice_t decisions,
                               counter_info_t *info,
                               SYSTEMVALUE unitpixels)
{
  double fit = 0.0 ;
  ch_float fixed_edge = 0.0 ;  /* Position of last fixed edge */
  int32 fixedwidth = 0 ; /* Adjusted width since last fixed edge */
  Bool got_fixed = FALSE ;
  double excess ;
  stem_t *stem ;
  rounding_t *rounding ;

  HQASSERT(cntrstems && cntrstems->prev == NULL,
           "Not at start of counter stems") ;
  HQASSERT(info != NULL, "No counter hinting info") ;

  /* Choose roundings for all stems and counters */
  counter_fix(decisions, info) ;

  for ( stem = cntrstems, rounding = info->roundings ;;) {
    int32 actualwidth ;
    ch_float tmpx ; /* for NEAREST */
    stem_t *next = stem->next ;

    HQASSERT(rounding - info->roundings < MAX_DECISIONS * 2,
             "Max stems/counters exceeded in evaluation") ;
    HQASSERT(fabs(rounding->realwidth - stem->dz * unitpixels) < EPSILON &&
             rounding->idealwidth == stem->idealwidth &&
             rounding->otherwidth == stem->otherwidth,
             "Stem and rounding do not correspond") ;

    /* Check if lower edge of stem is fixed, and if so, whether the distance
       since the last fixed edge matches. */
    if ( (stem->fixed & HINT_EDGE_LOW) != 0 ) {
      ch_float new_edge = stem->z + stem->lo_delta ;

      /* If there was a previous fixed edge, check that the difference is
         as expected, and add the fixed fit penalty if not. */
      if ( got_fixed ) {
        ch_float width = (new_edge - fixed_edge) * unitpixels ;
        HQASSERT(fabs(width - NEAREST(width)) < EPSILON,
                 "Fixed edges are not an exact number of pixels apart") ;
        if ( NEAREST(width) != fixedwidth )
          fit += FIXED_FIT_PENALTY ;
      }

      /* We definitely have a fixed edge now. */
      got_fixed = TRUE ;
      fixed_edge = new_edge ;
      fixedwidth = 0 ;
    }

    actualwidth = rounding->idealwidth ;
    fixedwidth += actualwidth ;

    /* Add penalty for inconsistent rounding of stems */
    if ( rounding->set != NULL &&
         rounding->set->actualwidth != actualwidth ) {
      fit += STEM_INCONSISTENT_PENALTY ;
    }

    /* Add penalty for poor rounding of stems */
    tmpx = rounding->realwidth - actualwidth ;
    fit += tmpx * tmpx * STEM_ROUND_WEIGHT ;

    /* Check if upper edge of a stem is fixed, and if so, whether the distance
       since the last fixed edge matches. */
    if ( (stem->fixed & HINT_EDGE_HIGH) != 0 ) {
      ch_float new_edge = stem->z + stem->dz + stem->hi_delta ;

      /* If there was a previous fixed edge, check that the difference is
         as expected, and add the fixed fit penalty if not. */
      if ( got_fixed ) {
        ch_float width = (new_edge - fixed_edge) * unitpixels ;
        HQASSERT(fabs(width - NEAREST(width)) < EPSILON,
                 "Fixed edges are not an exact number of pixels apart") ;
        if ( NEAREST(width) != fixedwidth )
          fit += FIXED_FIT_PENALTY ;
      }

      /* We definitely have a fixed edge now. */
      got_fixed = TRUE ;
      fixed_edge = new_edge ;
      fixedwidth = 0 ;
    }

    ++rounding ;

    if ( next == NULL )
      break ;

    /* If there is a next stem, there is a counter between them. */
    actualwidth = rounding->actualwidth ;
    fixedwidth += actualwidth ;

    /* Add penalty for inconsistent rounding of counters */
    if ( rounding->set != NULL &&
         rounding->set->actualwidth != actualwidth ) {
      fit += COUNTER_INCONSISTENT_PENALTY ;
    }

    /* Add penalty for poor rounding of counters */
    tmpx = rounding->realwidth - actualwidth ;
    fit += tmpx * tmpx * COUNTER_ROUND_WEIGHT ;

    /* Add penalty for distorting counter ratios. */
    if ( info->meancounter > EPSILON && info->meanicounter > EPSILON ) {
      tmpx = (rounding->realwidth / info->meancounter -
              actualwidth / info->meanicounter) ;
      fit += tmpx * tmpx * COUNTER_RATIO_WEIGHT ;
    }

    ++rounding ;

    stem = next ;
  }

  /* ExpansionFactor should really depend on the bounding box of the
     character as a whole. Unfortunately, we don't have that information at
     this point, so we base the expansion on the difference between the
     min/max stems given in this group. We always give counter adjusting one
     pixel's wiggle room; the default ExpansionFactor of 0.06 is too small to
     allow optimisation for very small characters.

     Calculate how many pixels the total width exceeds the expansion factor
     by. This is deliberately truncated to allow a little slop for small
     characters. The penalty assessed is in proportion to the excess. */
  excess = fabs(info->totalwidth - info->realwidth) ;
  if ( excess >= 1.0 ) { /* Allow up to one pixel slop always */
    excess -= info->realwidth * blues_str.ExpansionFactor ;
    if ( excess > 0 )
      fit += excess * EXPANSION_PENALTY ;
  }

  return fit ;
}

static void counter_adjust(stem_t *cntrstems)
{
  register stem_t *stem ;
  double bestfit ;
  counter_choice_t decisionmask, thistry, lasttry, besttry ;
  uint32 ndecisions ;
  SYSTEMVALUE unitpixels ;
  counter_info_t info ;
  rounding_t *rounding ;

  HQASSERT(cntrstems && cntrstems->prev == NULL,
           "Not at start of counter stems") ;

  /* If there is only one stem, there cannot be a counter by definition */
  if ( cntrstems->next == NULL )
    return ;

  unitpixels = UNITPIXELS(cntrstems->vertical == STEM_V) ;

  info.nrounds = 0 ;
  info.ncounterset = info.nstemset = 0 ;
  info.ncounterchoice = info.nstemchoice = 0 ;
  info.meancounter = 0.0 ;

  /* Pre-scan the list, producing the stem and counter histograms, determining
     the number of decisions and the real width. */
  for ( stem = cntrstems, rounding = info.roundings ; ; ) {
    stem_t *next = stem->next ;
    uint32 i ;

    /* If there are too many stems and counters, punt */
    if ( info.nrounds >= MAX_DECISIONS * 2 ) {
      HQTRACE((debug_hints & DEBUG_HINT_FAILURE) != 0,
              ("Too many stems in counter hinting group")) ;
      return ;
    }

    rounding = &info.roundings[info.nrounds++] ;
    rounding->realwidth = stem->dz * unitpixels ;
    rounding->idealwidth = stem->idealwidth ;
    rounding->actualwidth = rounding->idealwidth ;
    rounding->otherwidth = stem->otherwidth ;
    rounding->set = NULL ;

    if ( stem->idealwidth != stem->otherwidth ) {
      rounding_set_t *set = info.stemset ;

      /* If there is a decision to be made, determine if we have seen this
         stem type before. If we had seen it, but there was only one of them
         before, then add an extra individual stem choice because we didn't
         add an individual choice when creating the set. */
      for ( i = 0 ; i < info.nstemset ; ++i ) {
        if ( set[i].idealwidth == stem->idealwidth &&
             set[i].otherwidth == stem->otherwidth ) {
          if ( set[i].usage == 1 )
            info.nstemchoice += 1 ;

          info.nstemchoice += 1 ;
          set[i].usage += 1 ;
          rounding->set = &set[i] ;
          break ;
        }
      }

      /* Haven't seen it before, add a new stem rounding entry. */
      if ( i == info.nstemset && i < MAX_DECISIONS ) {
        rounding->set = &set[i] ;
        set[i].idealwidth = stem->idealwidth ;
        set[i].otherwidth = stem->otherwidth ;
        set[i].usage = 1 ;
        set[i].actualwidth = stem->idealwidth ;
        info.nstemset += 1 ;
      }
    }

    /* If there is a next stem, there is a counter between them. */
    if ( next != NULL ) {
      int32 idealwidth, otherwidth ;
      ch_float pixelwidth = (next->z - stem->z - stem->dz) * unitpixels ;
      ch_float tmpx ; /* for NEAREST */

      info.meancounter += pixelwidth ;

      rounding = &info.roundings[info.nrounds++] ;

      idealwidth = NEAREST(pixelwidth);
      if ( idealwidth < 1 ) /* All counters should have space if possible */
        idealwidth = 1 ;

      otherwidth = idealwidth ;
      if ( pixelwidth > idealwidth + EPSILON )
        otherwidth += 1 ;
      else if ( pixelwidth < idealwidth - EPSILON )
        otherwidth -= 1 ;

      rounding->realwidth = pixelwidth ;
      rounding->idealwidth = idealwidth ;
      rounding->actualwidth = rounding->idealwidth ;
      rounding->otherwidth = otherwidth ;
      rounding->set = NULL ;

      if ( otherwidth != idealwidth ) {
        rounding_set_t *set = info.counterset ;

        /* Have we seen this counter type before? */
        for ( i = 0 ; i < info.ncounterset ; ++i ) {
          if ( set[i].idealwidth == idealwidth &&
               set[i].otherwidth == otherwidth ) {
            if ( set[i].usage == 1 )
              info.ncounterchoice += 1 ;

            info.ncounterchoice += 1 ;
            set[i].usage += 1 ;
            rounding->set = &set[i] ;
            break ;
          }
        }

        /* Haven't seen it before, add a new stem rounding entry. */
        if ( i == info.ncounterset ) {
          HQASSERT(i < MAX_DECISIONS, "Counter set too large") ;
          rounding->set = &set[i] ;
          set[i].idealwidth = idealwidth ;
          set[i].otherwidth = otherwidth ;
          set[i].usage = 1 ;
          set[i].actualwidth = idealwidth ;
          info.ncounterset += 1 ;
        }
      }
    } else {
      break ;
    }

    stem = next ;
  }

  /* No next stem. Total width is from the start to the end of this stem */
  info.realwidth = (stem->z + stem->dz - cntrstems->z) * unitpixels ;
  info.hintwidth = (stem->z + stem->dz + stem->hi_delta -
                    cntrstems->z - cntrstems->lo_delta) * unitpixels ;
  info.meancounter /= (info.nrounds >> 1) ;

  HQASSERT(fabs(info.hintwidth - floor(info.hintwidth + 0.5)) < EPSILON,
           "Hint adjusted width before counter adjust not whole pixels") ;

  /* Reduce the set sizes such that the total number of decisions does not
     overflow the decision tree word. This has the effect of limiting the
     decision tree to the first MAX_DECISIONS. Remaining decisions are left
     at their default values. The counter set cannot be equal to
     MAX_DECISIONS, because there is one less counter than stem, and the
     number of stems are limited to MAX_DECISIONS. */
  HQASSERT(info.ncounterset < MAX_DECISIONS,
           "Counter set has too many entries") ;
  ndecisions = MAX_DECISIONS - info.ncounterset ;

  if ( ndecisions < info.ncounterchoice )
    info.ncounterchoice = ndecisions ;
  ndecisions -= info.ncounterchoice ;

  if ( ndecisions < info.nstemset )
    info.nstemset = ndecisions ;
  ndecisions -= info.nstemset ;

  if ( ndecisions < info.nstemchoice )
    info.nstemchoice = ndecisions ;
  ndecisions -= info.nstemchoice ;

  /* Limit the number of changes to those representible in a bitmask. The
     decision mask is used to determine when to retry the first decision. If
     there no decisions that can be made, give up immediately, the problem is
     over-constrained. */
  if ( ndecisions >= MAX_DECISIONS ) {
    HQTRACE((debug_hints & DEBUG_HINT_FAILURE) != 0,
            ("Counter hinting over-constrained")) ;
    return ;
  }

  decisionmask = (uint32)-1 >> ndecisions ;

  /* Find solution for "ideal" roundings */
  besttry = 0 ;
  bestfit = counter_evaluate(cntrstems, besttry, &info, unitpixels) ;

  /* Try to improve upon this by testing neighbouring solutions. We start
     testing decisions in turn until we return to a decision we have already
     made, or we find a better fit. On finding a better fit, we replace the
     current fit and reset the test mask to try all changes from the current
     change onwards. This may test some of the less-likely options, but
     should avoid some repeated re-trial of previously tested changes. We
     could try multiple starting fits to improve the chance of finding a
     global optimum; because of the way the utility function works, the most
     likely values are those which change just the counter decisions. */
  thistry = lasttry = 1 ; /* Start and end with the first decision */
  do {
    double fit = counter_evaluate(cntrstems, besttry ^ thistry,
                                  &info, unitpixels) ;
    /*
     * Got a weird random lockup on Linux. Looks to me like a compiler
     * bug with float/double rounding. If I run in a debugger or add printf
     * statements the issue goes away. We seem to be going around and around
     * this do...while loop forever. I think we keep switching between two
     * entries that we think are the best fit. I think its because we get
     * two doubles which are almost equal and double rounding confuses the
     * less-than test below. Adding a small epsilon seems to fix it. Needs
     * to be debugged properly when we get the time.....
     * \todo BMJ 24-Apr-14: Debug this properly
     */
#define BESTFIT_EPSILON 0.0000000001
    if ( fit < bestfit - BESTFIT_EPSILON ) {
      /* Found a better fit. Continue testing from here. */
      bestfit = fit ;
      besttry ^= thistry ;
      lasttry = thistry ;
    }

    /* Rotate test bit to next decision. */
    thistry = (thistry << 1) & decisionmask ;
    if ( thistry == 0 )
      thistry = 1 ;
  } while ( thistry != lasttry ) ;

  if ( bestfit < ACCEPT_FIT_LIMIT ) {
    SYSTEMVALUE onepixel = ONEPIXEL(cntrstems->vertical == STEM_V) ;
    ch_float tmpx ; /* for NEAREST */
    int32 difference ;

    /* Apply decision tree to roundings, and shift to align fixed edges */
    counter_fix(besttry, &info) ;

    /* Distribute the difference between the totalwidth and hintwidth evenly
       on both sides. If we come across a fixed stem, we will backtrack and
       adjust the stems before the current point. Widths after the fixed stem
       should match. Stem deltas are set up for idealwidth on entry to the
       routine. */
    difference = NEAREST(info.hintwidth - info.totalwidth) / 2 ;

    for ( stem = cntrstems, rounding = info.roundings ;; ) {
      stem_t *prev, *next = stem->next ;
      ch_float counterwidth ;

      HQASSERT(rounding - info.roundings < MAX_DECISIONS * 2,
               "Max stems/counters exceeded in evaluation") ;
      HQASSERT(fabs(rounding->realwidth - stem->dz * unitpixels) < EPSILON &&
               rounding->idealwidth == stem->idealwidth &&
               rounding->otherwidth == stem->otherwidth,
               "Stem and rounding do not correspond") ;

      /* If an edge of the stem is fixed, and the difference is not zero, then
         backtrack to patch up the previous unfixed stems. */
      if ( (stem->fixed & HINT_EDGE_LOW) != 0 && difference != 0 ) {
        ch_float fdiff = difference * onepixel ;
        for ( prev = stem->prev ; prev != NULL ; prev = prev->prev ) {
          prev->lo_delta -= fdiff ;
          prev->hi_delta -= fdiff ;
        }
        difference = 0 ;
      }
      stem->lo_delta += difference * onepixel ;
      difference += rounding->actualwidth - stem->idealwidth ;

      /* If an edge of the stem is fixed, and the difference is not zero, then
         backtrack to patch up the previous unfixed stems. */
      if ( (stem->fixed & HINT_EDGE_HIGH) != 0 && difference != 0 ) {
        ch_float fdiff = difference * onepixel ;
        stem->lo_delta -= fdiff ;
        for ( prev = stem->prev ; prev != NULL ; prev = prev->prev ) {
          prev->lo_delta -= fdiff ;
          prev->hi_delta -= fdiff ;
        }
        difference = 0 ;
      }

      /* Save the original high edge position before adjusting */
      counterwidth = stem->z + stem->dz + stem->hi_delta ;
      stem->hi_delta += difference * onepixel ;

      /* Fix both edges, and set the ideal and otherwidth to the fixed
         width. */
      stem->fixed |= HINT_EDGE_LOW|HINT_EDGE_HIGH ;
      stem->idealwidth = stem->otherwidth = rounding->actualwidth ;

      ++rounding ;

      if ( next == NULL )
        break ;

      /* If there is a next stem, there is a counter between the stems. The
         original counter width is implicit as the width between the adjacent
         stems. */
      counterwidth = (next->z + next->lo_delta - counterwidth) * unitpixels ;
      HQASSERT(fabs(counterwidth - NEAREST(counterwidth)) < EPSILON,
               "Counter width is not whole pixels before adjusting") ;
      difference += rounding->actualwidth - NEAREST(counterwidth) ;
      ++rounding ;

      stem = next ;
    }
  }
}

#if defined( DEBUG_BUILD )
void t1hint_debug_init(void)
{
  register_ripvar(NAME_debug_hint, OINTEGER, &debug_hints) ;
}


void t1hint_method(int32 method)
{
  if ( method != hint_method )
    /* Purge the whole glyph cache. */
    fontcache_clear(get_core_context_interp()->fontsparams);
  hint_method = method ;
}
#endif

/* ---------------------------------------------------------------------- */
/* Charstring build filter routines */
static Bool hint_initchar( void *data )
{
  t1hint_info_t *info = data ;
  charstring_build_t *pathfns = &info->pathfns ;

  info->InDotSection = FALSE ;
  info->numstems = 0;
  info->sidebearingX = info->sidebearingY = 0.0;
  info->baselineDy = 0.0 ;
  info->stems[STEM_H][STEM_ACTIVE] = NULL ;
  info->stems[STEM_H][STEM_INACTIVE] = NULL ;
  info->stems[STEM_V][STEM_ACTIVE] = NULL ;
  info->stems[STEM_V][STEM_INACTIVE] = NULL ;

#if defined(DEBUG_BUILD)
  info->last_hint.x = 0.0;
  info->last_hint.y = 0.0;
  info->last_hint.xvalid = FALSE;
  info->last_hint.yvalid = FALSE;
#endif

  return (*pathfns->initchar)(pathfns->data) ;
}

static Bool hint_setbearing( void *data, ch_float xbear , ch_float ybear )
{
  t1hint_info_t *info = data ;
  charstring_build_t *pathfns = &info->pathfns ;

  HQTRACE((debug_hints & DEBUG_HINT_INTEGRAL) &&
          ((int32)xbear != xbear || (int32)ybear != ybear),
          ("LSB points not integral %f %f", xbear, ybear)) ;

  change_hints(info) ;

  /* Sidebearing values are xbear, sby args, plus accent offset from Seac(). */
  info->sidebearingX = xbear;
  info->sidebearingY = ybear ;

  /* Find the shift to move the baseline to a pixel boundary. The baseline is
     only explicitly represented if there are BlueValues in the font.
     Otherwise, we use the Y value of the sidebearing as a stand-in for the
     baseline if there are no BlueValues. We also check that the baseline is
     positive; TN5015 says that a negative topy of the baseline is a
     convention to indicate that vertical alignment zones are not needed. */
  if ( baseline_index >= 0 && alignmentzones[baseline_index].topy >= 0 )
    ybear = alignmentzones[baseline_index].topy ;

  info->baselineDy = floor(ybear * unitpixelsY + 0.5) * onepixelY - ybear;

#if defined(DEBUG_BUILD)
  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 ) {
    monitorf((uint8 *)"%f SideBearingX\n", info->sidebearingX);
    monitorf((uint8 *)"%f SideBearingY\n", info->sidebearingY);
    monitorf((uint8 *)"%f BaselineShift\n", info->baselineDy);
  }
#endif

  /** \todo @@@ TODO FIXME ajcd 2003-05-22:
     Add a default moveto(0,0). This is not
     in the specs anywhere, but we have instances of Type 1 fonts that
     require it (see request 11935). The (0,0) moveto needs to be hinted,
     which is why it isn't here, and is left in charstring building. */

  /* The hinting layer takes care of applying the sidebearings */
  return (*pathfns->setbearing)(pathfns->data, 0, 0) ;
}

static Bool hint_setwidth( void *data, ch_float xwidth , ch_float ywidth )
{
  t1hint_info_t *info = data ;
  charstring_build_t *pathfns = &info->pathfns ;

  HQTRACE((debug_hints & DEBUG_HINT_INTEGRAL) &&
          ((int32)xwidth != xwidth || (int32)ywidth != ywidth),
          ("Width points not integral %f %f", xwidth, ywidth)) ;

  return (*pathfns->setwidth)(pathfns->data, xwidth, ywidth) ;
}

/* ---------------------------------------------------------------------- */
/** Declares the vertical range of a horizontal stem zone
   between coordinates y1 and y2
   y1,y2 are relative to the left sidebearing point, but the stem is inserted
   and adjusted in non sidebearing space, because sidebearingY has been
   adjusted to align the baseline on a pixel boundary.
*/
static Bool hint_hstem(void *data, ch_float y1, ch_float y2,
                       Bool tedge, Bool bedge, int32 index)
{
  uint32 edge = 0 ;
  ch_float dy ;
  t1hint_info_t *info = data ;

  if ( tedge )
    edge |= HINT_EDGE_HIGH ;
  if ( bedge )
    edge |= HINT_EDGE_LOW ;

  dy = y2 - y1 ;
  if ( dy < 0 ) {
    y1 += dy ;
    dy = -dy ;
  }

  HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
          ("HStem: y %f, dy %f", y1, dy));

  return VHStem(info, STEM_H, y1 + info->sidebearingY, dy, edge, index);
}

/* ---------------------------------------------------------------------- */
/** Declares the horizontal range of a vertical stem zone
   between coordinates x and x + dx
   x is relative to the left sidebearing point
*/

static Bool hint_vstem(void *data, ch_float x1, ch_float x2,
                       Bool ledge, Bool redge, int32 index)
{
  uint32 edge = 0 ;
  ch_float dx ;
  t1hint_info_t *info = data ;

  if ( ledge )
    edge |= HINT_EDGE_HIGH ;
  if ( redge )
    edge |= HINT_EDGE_LOW ;

  dx = x2 - x1 ;
  if ( dx < 0 ) {
    x1 += dx ;
    dx = -dx ;
  }

  HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
          ("VStem: x %f, dx %f", x1, dx));

  return VHStem(info, STEM_V, x1 + info->sidebearingX, dx, edge, index);
}

/* ---------------------------------------------------------------------- */
/* Activate or de-activate an indexed stem. */
static Bool hint_hintmask( void *data, int32 index, Bool activate )
{
  t1hint_info_t *info = data ;
  uint32 vh ;

  HQASSERT(activate == STEM_INACTIVE || activate == STEM_ACTIVE,
           "Neither activating nor inactivating hint") ;

  for ( vh = 0 ; vh < 2 ; ++vh ) {
    stem_t *stem ;

    if ( (stem = stem_find_index(&info->stems[vh][!activate], index)) != NULL ) {
      HQASSERT(info->stems[vh][!activate] == stem,
               "Wrong stem activated/deactivated") ;
      HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
              ("%s %s stem %d: %f +%f",
               activate ? "Activating" : "Inactivating",
               vh ? "V" : "H",
               stem->index, stem->z, stem->dz)) ;

      stem_remove(&info->stems[vh][!activate]) ;
      stem_add(&info->stems[vh][activate], stem) ;
    }
  }

  return TRUE ;
}

/** Counter control hints. Each counter control group is built up one stem at
   a time. When the set of stems is complete, an index less than zero is used
   to indicate the group is complete. All of the calculations are done at that
   time. */
static Bool hint_cntrmask( void *data, int32 index, uint32 group )
{
  t1hint_info_t *info = data ;
  uint32 vh, active ;

  HQASSERT(group > 0, "Counter group should not be zero") ;

#if defined(DEBUG_BUILD)
  if ( (hint_method & HINT_METHOD_COUNTER) != 0 )
    return TRUE ;
#endif

  if ( index >= 0 ) {
    for ( vh = 0 ; vh < 2 ; ++vh ) {
      for ( active = 0 ; active < 2 ; ++active ) {
        stem_t *stem ;

        /* Invert active counter to search active stems first; stems are more
           likely to be on the active list, since they may have been created
           by H/VStem just before calling cntrmask. Return immediately if
           found, since there should only be one stem with a particular
           index. */
        if ( (stem = stem_find_index(&info->stems[vh][!active], index)) != NULL ) {
          stem->group = group ;
          return TRUE ;
        }
      }
    }
  } else {
    /* Counter control group is complete. Consider the vertical and horizontal
       stems separately. */
    for ( vh = 0 ; vh < 2 ; ++vh ) {
      stem_t *stem, *cntrstems = NULL  ;

      /* Collect all of the stems with a particular group together. We
         retain the original active/non-active status in the group variable,
         so we can re-distribute the stems back to their original location. */
      for ( active = 0 ; active < 2 ; ++active ) {
        stem_t **stems_from = &info->stems[vh][active] ;

        while ( (stem = stem_find_group(stems_from, group)) != NULL ) {
          HQASSERT(stem == *stems_from, "Group stem doesn't match head stem") ;
          stem_remove(stems_from) ;
          stem_add(&cntrstems, stem) ;
          stem->group = active ; /* Save original active status */
        }
      }

      if ( cntrstems != NULL ) {
        counter_adjust(stem_find_first(&cntrstems)) ;

        /* Having finished with the stem adjustment, put them back where they
           came from. */
        do {
          stem = cntrstems ;
          stem_remove(&cntrstems) ;
          stem_add(&info->stems[vh][stem->group], stem) ;
          stem->group = 0 ;

#if defined(DEBUG_BUILD)
          if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 )
            DumpStem(stem, TRUE);
#endif
        } while ( cntrstems != NULL ) ;
      }
    }
  }

  return TRUE ;
}

static Bool hint_change( void *data )
{
  t1hint_info_t *info = data ;

  change_hints(info) ;

  return TRUE ;
}

static Bool hint_dotsection( void *data )
{
  t1hint_info_t *info = data ;

  info->InDotSection = !info->InDotSection ;

  return TRUE ;
}

static Bool hint_flex( void *data, ch_float curve_a[6], ch_float curve_b[6],
                       ch_float depth, ch_float thresh, Bool hflex)
{
  HQTRACE((debug_hints & DEBUG_HINT_FAILURE) != 0, ("Flex not tested")) ;

  /* Convert flex depth expressed in font space to device pixels */
  if ( hflex )
    depth *= unitpixelsY ;
  else
    depth *= unitpixelsX ;

  /* Threshold is expressed in percentage of a device pixel */
  if ( fabs(depth) * 100.0 < thresh )
    return hint_lineto(data, curve_b[4], curve_b[5]) ;

  /* Do two Bezier curves instead of straight line. */
  return (hint_curveto(data, curve_a) && hint_curveto(data, curve_b)) ;
}

static Bool hint_moveto( void *data, ch_float x , ch_float y )
{
  t1hint_info_t *info = data ;
  charstring_build_t *pathfns = &info->pathfns ;

  HQTRACE((debug_hints & DEBUG_HINT_INTEGRAL) &&
          ((int32)x != x || (int32)y != y),
          ("Move points not integral (%f, %f)", x, y)) ;

  Hint_Move_and_Line(info, &x, &y) ;

  return (*pathfns->moveto)(pathfns->data, x, y) ;
}

static Bool hint_lineto( void *data, ch_float x , ch_float y )
{
  t1hint_info_t *info = data ;
  charstring_build_t *pathfns = &info->pathfns ;

  HQTRACE((debug_hints & DEBUG_HINT_INTEGRAL) &&
          ((int32)x != x || (int32)y != y),
          ("Line points not integral (%f, %f)", x, y)) ;

  Hint_Move_and_Line(info, &x, &y) ;

  return (*pathfns->lineto)(pathfns->data, x, y) ;
}

static Bool hint_curveto( void *data, ch_float curve[6] )
{
  t1hint_info_t *info = data ;
  charstring_build_t *pathfns = &info->pathfns ;
  uint32 i ;
  ch_float ncurve[6] ;
  hint_t new_hint = { 0.0, 0.0, FALSE, FALSE } ; /* Keep compiler quiet */

  /* Apply the hinting to all the points in the curve  */
  for ( i = 0 ; i < 6 ; i += 2 ) {
    HQTRACE((debug_hints & DEBUG_HINT_INTEGRAL) &&
            (curve[i] != (int32)curve[i] ||
             curve[i + 1] != (int32)curve[i + 1]),
            ("Curve points not integral (%f %f)", curve[i], curve[i + 1])) ;

    ncurve[i + 0] = curve[i + 0] + info->sidebearingX ;
    ncurve[i + 1] = curve[i + 1] + info->sidebearingY ;
    hint_point(info, ncurve[i], ncurve[i + 1], &new_hint);
    ncurve[i + 0] += new_hint.x;
    ncurve[i + 1] += new_hint.y + info->baselineDy;

    HQTRACE((debug_hints & DEBUG_HINT_INFO) != 0,
            ("con %d: ( %f %f ) -> ( %f %f )", i >> 1,
             curve[i + 0], curve[i + 1], ncurve[i + 0], ncurve[i + 1] ));
  }

#if defined(DEBUG_BUILD)
  if ( (hint_method & HINT_METHOD_XFLEX) != 0 ) {
    /* this implements an equivalent to the "flex" hint in the x direction
     * only, by hand, by spotting shallow curves near stems.
     */
    if ( info->last_hint.xvalid ) { /* perhaps snap to start point */
      if ( fabs( ncurve[0] - info->last_hint.x ) < onepixelX ) {
        ncurve[0] = info->last_hint.x; /* snap first control point */
        if ( new_hint.xvalid && fabs(ncurve[2] - ncurve[4]) < onepixelX ) {
          ncurve[2] = ncurve[4]; /* snap second to end value */
        }
        else if ( fabs( ncurve[2] - info->last_hint.x ) < onepixelX ) {
          ncurve[2] = info->last_hint.x; /* snap second to first value */
        }
      }
    }
    else if ( new_hint.xvalid ) { /* perhaps snap to end point */
      if ( fabs( ncurve[2] - ncurve[4] ) < onepixelX ) {
        ncurve[2] = ncurve[4]; /* snap first control point */
        if ( fabs( ncurve[0] - ncurve[4] ) < onepixelX ) {
          ncurve[0] = ncurve[4]; /* snap second to end value */
        }
      }
    }
  }

  info->last_hint = new_hint ;
  info->last_hint.x = ncurve[4];
  info->last_hint.y = ncurve[5];

  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 ) {
    /* use temporary variables here for the VxWorks compiler which
     * cannot cope with "complex" floating point expressions
     */
    ch_float f0 = curve[0] + info->sidebearingX;
    ch_float f2 = curve[2] + info->sidebearingX;
    ch_float f4 = curve[4] + info->sidebearingX;
    ch_float f1 = curve[1] + info->sidebearingY;
    ch_float f3 = curve[3] + info->sidebearingY;
    ch_float f5 = curve[5] + info->sidebearingY;

    monitorf((uint8 *)"%f %f %f %f %f %f OrigCurve\n",
             f0, f1, f2, f3, f4, f5);
    monitorf((uint8 *)"%f %f %f %f %f %f Curve\n",
             ncurve[0], ncurve[1],
             ncurve[2], ncurve[3],
             ncurve[4], ncurve[5]);
  }
#endif

  return (*pathfns->curveto)(pathfns->data, ncurve) ;
}

static Bool hint_closepath( void *data )
{
  t1hint_info_t *info = data ;
  charstring_build_t *pathfns = &info->pathfns ;

#if defined(DEBUG_BUILD)
  info->last_hint.xvalid = FALSE;
  info->last_hint.yvalid = FALSE;

  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 )
    monitorf((uint8 *)"ClosePath\n");
#endif

  return (*pathfns->closepath)(pathfns->data) ;
}

/* ---------------------------------------------------------------------- */
/* Cleanup after the character, terminating the STEM hint data structures */

static Bool hint_endchar( void *data, Bool result )
{
  t1hint_info_t *info = data ;
  charstring_build_t *pathfns = &info->pathfns ;

  if ( info->numstems > FIXED_STEMS ) { /* Need to free allocated stems */
    uint32 vh, active ;

    for ( vh = 0 ; vh < 2 ; ++vh ) {
      for ( active = 0 ; active < 2 ; ++active ) {
        stem_t *stem, **stems = &info->stems[vh][active] ;

        while ( (stem = *stems) != NULL ) {
          stem_remove(stems) ;
          if ( !stem->prealloc )
            mm_free(mm_pool_temp, (mm_addr_t)stem, sizeof(stem_t)) ;
        }
      }
    }
  }

#if defined(DEBUG_BUILD)
  if ( (debug_hints & DEBUG_HINT_PICTURE) != 0 )
    monitorf((uint8 *)"EndChar\n");
#endif

  return (*pathfns->endchar)(pathfns->data, result) ;
}

/*
Log stripped */
