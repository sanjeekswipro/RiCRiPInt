/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!src:gu_chan.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to manage rasterstyle structures, describing real or virtual
 * colorspaces.
 */

#include "core.h"
#include "gu_chan.h"
#include "coremaths.h"    /* ugcd */

#include "debugging.h"    /* debug_print_object_indented */
#include "dlstate.h"      /* DOING_RUNLENGTH */
#include "gcscan.h"       /* ps_scan_field */
#include "graphics.h"     /* GSTATE (for gstateptr->colorInfo) */
#include "gs_color.h"     /* COLORSPACE_ID */
#include "gscequiv.h"     /* gsc_stdCMYKequiv_lookup */
#include "gschcms.h"      /* REPRO_COLOR_MODEL* */
#include "gschtone.h"     /* gsc_redo_setscreen */
#include "gscphotoink.h"  /* gsc_invokeNamedColorIntercept */
#include "gsctint.h"      /* gsc_invokeNamedColorIntercept */
#include "gstate.h"       /* gsave_ */
#include "gu_htm.h"       /* MODHTONE_REF */
#include "halftone.h"     /* ht_mergespotnoentry */
#include "hqatomic.h"     /* hq_atomic_counter_t etc */
#include "hqmemcpy.h"     /* HqMemCpy */
#include "dicthash.h"     /* fast_insert_hash etc */
#include "dictscan.h"     /* dictmatch */
#include "miscops.h"      /* run_ps_string */
#include "mm.h"           /* mm_alloc */
#include "mmcompat.h"     /* mm_alloc_with_header */
#include "monitor.h"      /* monitorf */
#include "namedef_.h"     /* NAME_* */
#include "objects.h"      /* OBJECT */
#include "params.h"       /* SystemParams */
#include "pcmnames.h"     /* pcmCMYKNames */
#include "pixelLabels.h"  /* RENDERING_PROPERTY_* */
#include "renderom.h"     /* OMIT_DETAILS */
#include "stacks.h"       /* operandstack */
#include "swerrors.h"     /* VMERROR */
#include "uvms.h"         /* UVS, UVM */
#include "bitbltt.h"      /* BLIT_WIDTH_BITS */
#include "blitcolors.h"   /* blit_colormap_t */
#include "blitcolorh.h"   /* blit_color_pack_generic */
#include "objnamer.h"     /* VERIFY_OBJECT */
#include "utils.h"        /* calculateAdler32 */
#include "bandtable.h"    /* band_resource_pools */
#include "swpgb.h" /* sw_pgb_special_handling */
#include "dictinit.h"
#include "taskh.h"


/* Context structure for dictionary creation */
struct DICT_ALLOC_PARAMS {
  mm_pool_t *alloc_pool;
  uint32    alloc_class;
};

/* Level 3 colorant management machinery.

   This is in four sections:
   (a) definitions and declarations
   (b) inquiry functions for use by the interpreter to separate its colors
   (c) service functions for the renderer to implement the renderer loops
   (d) construction functions for acting on setpagedevice

 */

typedef struct GUCR_SHEET GUCR_SHEET;

/* ---------------------------------------------------------------------- */
#define COLORANT_SET_WORD(_ci) ((_ci) >> 5)
#define COLORANT_SET_BIT(_ci) ((_ci) & 31)

struct GUCR_COLORANTSET {
  int32 cWords; /* Number of extra 32-bit flag words allocated */
  int32 nSet;   /* Number of bits set */
  uint32 afMember[1]; /* reallocated larger as necessary */
};

/* ---------------------------------------------------------------------- */
/** Stores the mapping from one colorant in a rasterstyle onto the equivalent
   colorant in the real rasterstyle.  Note it excludes any further mapping
   for recipe/photoinks (in which case the 'recipe' flag will be set).
   cimap uses the same style as recipe/photoink colorant mapping to simplify
   usage.  If the rasterstyle is the real rasterstyle then ci == cimap[0].
   Colorants which do have equivalents in the real rasterstyle are linked
   together and referenced by pRasterStyle->equivalentRealColorants for
   efficient access by guc_equivalentRealColorantIndex. */
typedef struct realcolorantmap_s {
  COLORANTINDEX ci;
  COLORANTINDEX cimap[2]; /* cimap[1] is always COLORANTINDEX_UNKNOWN. */
  Bool recipe; /* Check for another colorant mapping with recipe/photoinks. */
  struct realcolorantmap_s* next;
} REALCOLORANTMAP;

/* ---------------------------------------------------------------------- */
#define COLORANT_NAME "RasterStyle Colorant"

struct GUCR_COLORANT {
  GUCR_COLORANT * pNextColorant;
  GUCR_CHANNEL * pOwningChannel;
  GUCR_COLORANT_INFO info ;
  REALCOLORANTMAP equivalentRealColorant;
  uint8 fRenderOmit ;
  uint8 fRecalc ;   /* recalc cmyk, and equivalent srgb? */

  OBJECT_NAME_MEMBER
};

/* ---------------------------------------------------------------------- */
#define CHANNEL_NAME "RasterStyle Channel"

struct GUCR_CHANNEL {
  int32 nMapToThisChannel;
  GUCR_CHANNEL * pNextChannel;
  GUCR_SHEET * pOwningSheet;
  GUCR_COLORANT * pColorants;
  uint8   fRenderOmit;
  uint8   fRequired;        /* Plugin requires channel */
  uint8   fBlankColorants;  /* Channel has one or more blank colorants */
  /** \todo @@@ TODO FIXME ajcd 2004-06-08: The fRequiredByPartialPaint flag
     is a temporary hack because the pagebuffer device is incapable of
     dealing with the changes between partial and final render when
     separation omission is enabled. It should be removed when a properly
     enabled partial paint store is created. */
  uint8   fRequiredByPartialPaint; /* Required ONLY because of partial paint */
  int32   nRenderIndex;     /* Sequence number for a frame in the
                               renderer: -1 means not yet rendered */

  OBJECT_NAME_MEMBER
};

/* ---------------------------------------------------------------------- */
#define SHEET_NAME "RasterStyle Sheet"

struct GUCR_SHEET {
  GUCR_SHEET * pNextSheet;
  GUCR_RASTERSTYLE * pOwningRasterStyle;
  GUCR_CHANNEL * pChannels;
  int32         nSheetNumber;
  uint16        cRequired;      /* Count of channels required to output */
  uint8         fRenderOmit;
  uint8         spare1;
  int32         nNextRenderIndex;

  OBJECT_NAME_MEMBER
};

/*
 * Overflow limit for count of required channels on a sheet.  If count
 * changes this will need to be changed as well.
 */
#define GUC_REQD_CHAN_LIMIT (65535)

/* ---------------------------------------------------------------------- */
typedef struct colorantmap {
  COLORANTINDEX  ci ;
  COLORANTINDEX *cimap ;
  struct colorantmap *next ;
} COLORANTMAP ;

/* ---------------------------------------------------------------------- */
#define RASTERSTYLE_NAME "Rasterstyle"
#define NUM_CHANNEL_ASSIGNMENT 4

struct GUCR_RASTERSTYLE {
  uint32 id;
  uint32 flags; /* see RASTERSTYLE_FLAG_ */
  hq_atomic_counter_t nReferenceCount;
  GUCR_RASTERSTYLE *next;   /**< Next on global rasterstyle GC list. */
  GUCR_RASTERSTYLE *parent; /**< Parent rasterstyle in group structure. */
  mm_pool_t *dlpools; /**< Rasterstyles are allocated from dl pool or temp pool. */
  int32 nInterleavingStyle;
  int32 nValuesPerComponent;
  int32 nSeparationStyle;
  OBJECT colorSpace;
  int32 nProcessColorants;
  REALCOLORANTMAP* equivalentRealColorants;
  COLORANTINDEX ciMax;
  COLORANTINDEX ciBlack;
  COLORANTMAP *cmap ;
  OBJECT oFullyFledgedColorants;
  OBJECT oReservedColorants;
  OBJECT oDefaultScreenAngles;
  OBJECT oCustomConversions[3];
  NAMECACHE** colorChannelNames;
  int32 numColorChannels;
  OBJECT osRGB;
  NAMECACHE** processColorantNames;
  NAMECACHE * pBlackName;        /* If given, the name of the colorant used for Black (Separations) */
  int32 numProcessColorantNames;
  OBJECT oColorantPresence;
  OBJECT oColorantDetails;
  DEVICESPACEID processColorModel;
  COLORSPACE_ID calibrationColorModel;
  GUCR_PHOTOINK_INFO * photoinkInfo;
  int32 anChannelAssignment[NUM_CHANNEL_ASSIGNMENT];
  GUCR_SHEET * pSheets;
  GUCR_COLORANTSET * pBackgroundFlags;
  int32 nBandSize;
  int32 nMaxOffsetIntoBand;

  GUCR_COLORANTSET * pColorantOmits ; /**< The set of colorants which may be omitted. */
  GUCR_COLORANTSET * pCurrentOmits ;  /**< Subset of pColorantOmits, those currently blank. */

  OMIT_DETAILS omitDetails ;

  Bool fInheritSpotColors ;
  uint8 fAbortSeparations ;
  uint8 fAddAllSpotColors ;
  uint8 fRemoveAllSpotColors ;
  uint8 fOmitMonochrome ;
  uint8 fOmitSeparations ;
  uint8 fHaveEquivalent ;
  uint8 fOmitExtraSpot ; /* Omit dynamic seps if blank */
  uint8 fOmitProcess ; /* Omit process seps if blank */

  channel_output_t *object_map_translation; /**< Lookup table from pixel labels to map. */

  uint32 generation ; /**< Generation counter for colorant changes. */

  Bool fCalculatingColors ; /* To avoid calcaluting equiv colors in recursive cals */
  Bool screening ; /**< This is a halftoned rasterstyle. */

  OBJECT_NAME_MEMBER
};

static uint32 generation = 0 ;
static uint32 next_rasterstyle_id = 0;

/* GC root for rasterstyles. (Userd to be those not currently attached to a
   gstate, now all states, may change again in future.) */
static GUCR_RASTERSTYLE *rasterstyle_gc_list = NULL ;
static mps_root_t backdrop_gc_root;

/* ---------------------------------------------------------------------- */

static COLORANTINDEX gucs_colorantIndexInDictionary(
  /*@notnull@*/ /*@in@*/                  NAMECACHE * pColorantName,
  /*@notnull@*/                           const OBJECT * poDictionary);

static void gucs_removeColorantFromDictionary(NAMECACHE * pColorantName,
                                              OBJECT * poDictionary);

static Bool gucs_dictwalk_newColorantIndex(OBJECT * poKey, OBJECT * poValue,
                                           void * pvPrivate);

static COLORANTINDEX gucs_newColorantIndex(GUCR_RASTERSTYLE * pRasterStyle);

static Bool gucs_addColorantToDictionary(mm_pool_t *dlpools,
                                         NAMECACHE * pColorantName,
                                         COLORANTINDEX ci,
                                         OBJECT * poDictionary);

static NAMECACHE* guc_colorantIndexName(
  /*@notnull@*/ /*@in@*/                OBJECT*           odict,
                                        COLORANTINDEX     ci);

static Bool gucs_setupSheetsChannelsAndColorants(GUCR_RASTERSTYLE * pRasterStyle,
                                                 OBJECT * poColorantPresence,
                                                 OBJECT * poSeparationOrder);

static Bool gucs_copySheetsChannelsAndColorants(GUCR_RASTERSTYLE *rsSrc,
                                                GUCR_RASTERSTYLE *rsDst);

static Bool gucs_setupSeparationsChannelMapping(GUCR_RASTERSTYLE * pRasterStyle,
                                                GUCR_SHEET * pSheet,
                                                COLORANTINDEX ciSeparationOrderEntry,
                                                int32 * pnBestChoiceMapping);

static Bool gucs_dictwalk_clearColorant(OBJECT * poKey, OBJECT * poValue, void * pvPrivate);

static void gucs_clearColorant(
  /*@notnull@*/ /*@in@*/      GUCR_RASTERSTYLE * pRasterStyle,
                              COLORANTINDEX ci, Bool fAutomatic);

static Bool gucs_nameFromObject(const OBJECT * poName, NAMECACHE ** ppName);

static Bool gucs_colorantIsProcess(const GUCR_RASTERSTYLE* pRasterStyle,
                                   const NAMECACHE* pName);

static Bool gucs_newFrameComposite(GUCR_RASTERSTYLE * pRasterStyle,
                                   NAMECACHE * pColorantName,
                                   int32 x, int32 y,
                                   Bool fBackground,
                                   Bool fAutomatic,
                                   uint32 nRenderingProperties,
                                   GUC_FRAMERELATION frameRelation,
                                   int32 nRelativeToFrame,
                                   int32 specialHandling,
                                   USERVALUE neutralDensity,
                                   USERVALUE screenAngle,
                                   Bool fOverrideScreenAngle,
                                   COLORANTINDEX * pCi);

static Bool gucs_newFrameSeparations(GUCR_RASTERSTYLE * pRasterStyle,
                                     NAMECACHE * pColorantName,
                                     int32 x, int32 y,
                                     Bool fBackground,
                                     Bool fAutomatic,
                                     uint32 nRenderingProperties,
                                     GUC_FRAMERELATION frameRelation,
                                     int32 nRelativeToFrame,
                                     int32 specialHandling,
                                     USERVALUE neutralDensity,
                                     USERVALUE screenAngle,
                                     Bool fOverrideScreenAngle,
                                     COLORANTINDEX * pCi);

static Bool guc_overrideEquivalentColors(GUCR_SHEET * pSheet,
                                         GUCR_COLORANT * pColorant,
                                         NAMECACHE *sepName);

/**
 * Colorant presence values - used in Private dictionary ColorantPresence
 * entry. Each dict entry should have one of the following values.
 * Currently these are the same as in gdevdefs which are copied in in pagedev.pss
 * when ColorantPresence is created
 */
enum {
  COLORANT_CANBEOMITTED = 1,
  COLORANT_MUSTBEPRESENT = 2
} ;

/** Bit-flags for rasterstyle. */
enum {
  RASTERSTYLE_FLAG_BACKDROP = 1,     /**< This rasterstyle is a backdrop. */
  RASTERSTYLE_FLAG_VIRTUALDEVICE = 2 /**< The main page backdrop rasterstyle. */
} ;

/** Returns TRUE if the raster style is a backdrop and FALSE if it is
   a native output raster style. Use the macro in gu_chan.c only. */
#define BACKDROP_RASTERSTYLE(_pRasterStyle) \
  (((_pRasterStyle)->flags & RASTERSTYLE_FLAG_BACKDROP) != 0)

/** Returns TRUE if the raster style is the virtual raster style we construct
   for compositing when the job doesn't have it's own page group/canvas. Return
   FALSE otherwise. Use the macro in gu_chan.c only. */
#define BACKDROP_VIRTUALDEVICE(_pRasterStyle) \
  (((_pRasterStyle)->flags & RASTERSTYLE_FLAG_VIRTUALDEVICE) != 0)

/* ---------------------------------------------------------------------- */
/* this structure is used for various dictionary walks */

typedef struct {
  GUCR_RASTERSTYLE *pRasterStyle;
  COLORANTINDEX ci;
  Bool fDoingAll;
  Bool fClear;
} dictwalk_ci_t;

/* ---------------------------------------------------------------------- */

/* The device rasterstyle is first allocated from temp pool memory, but later
 * copied into dl pool memory.  This allows the pipelining of interpretation and
 * rendering. Backdrop rasterstyles are allocated from dl pool from the start.
 */

static void *gucs_alloc(mm_pool_t *dlpools, size_t bytes,
                        uint32 allocClass)
{
  if ( dlpools != NULL )
    return dl_alloc(dlpools, bytes, allocClass);
  else
    return mm_alloc(mm_pool_temp, bytes, allocClass);
}

static void gucs_free(mm_pool_t *dlpools, void *addr, size_t bytes,
                      uint32 allocClass)
{
  if ( dlpools != NULL )
    dl_free(dlpools, addr, bytes, allocClass);
  else
    mm_free(mm_pool_temp, addr, bytes);
}

static void *gucs_allocWithHeader(mm_pool_t *dlpools, size_t bytes,
                                  uint32 allocClass)
{
  if ( dlpools != NULL )
    return dl_alloc_with_header(dlpools, bytes, allocClass);
  else
    return mm_alloc_with_header(mm_pool_temp, bytes, allocClass);
}

static void gucs_freeWithHeader(mm_pool_t *dlpools, void *addr,
                                uint32 allocClass)
{
  if ( dlpools != NULL )
    dl_free_with_header(dlpools, addr, allocClass);
  else
    mm_free_with_header(mm_pool_temp, addr);
}

static OBJECT *gucs_objMemAllocFunc(int32 numobjs, void *param)
{
  mm_pool_t *dlpools = param;
  OBJECT *obj ;

  /* allocate some local PDF VM for an object of the given size */
  HQASSERT(numobjs > 0, "Should be at least one object to allocate") ;

  obj = gucs_alloc(dlpools, sizeof(OBJECT) * numobjs,
                   MM_ALLOC_CLASS_GUC_RASTERSTYLE);
  if ( obj != NULL ) {
    OBJECT *slot, transfer;

    theMark(transfer) = ISNOTVMDICTMARK(SAVEMASK);
    theTags(transfer) = ONULL;
    theLen(transfer) = 0;

    for ( slot = obj ; numobjs > 0 ; ++slot, --numobjs ) {
      OBJECT_SET_D0(*slot, OBJECT_GET_D0(transfer)) ; /* Set slot properties */
      OBJECT_SCRIBBLE_D1(*slot) ;
    }
  }

  return obj;
}

static
void *gucs_dict_allocator(
  size_t  size,
  uintptr_t data)
{
  struct DICT_ALLOC_PARAMS *alloc_params = (struct DICT_ALLOC_PARAMS *)data;

  HQASSERT((alloc_params != NULL), "Null allocation params pointer");

  return (gucs_allocWithHeader(alloc_params->alloc_pool, size,
                               alloc_params->alloc_class));
}

static Bool gucs_copyShallowDict(mm_pool_t *dlpools,
                                 OBJECT *dictSrc, OBJECT *dictDst)
{
  int32 len;
  struct DICT_ALLOC_PARAMS alloc_params;
  struct DICT_ALLOCATOR dict_alloc;

  getDictLength(len, dictSrc);

  alloc_params.alloc_pool = dlpools;
  alloc_params.alloc_class = MM_ALLOC_CLASS_GUC_RASTERSTYLE;
  dict_alloc.alloc_mem = gucs_dict_allocator;
  dict_alloc.data = (uintptr_t)&alloc_params;
  if (!dict_create(dictDst, &dict_alloc, len, ISNOTVMDICTMARK(SAVEMASK))) {
    return (FALSE);
  }

  if ( !CopyDictionary(dictSrc, dictDst, NULL, NULL) )
    return FALSE;

  return TRUE;
}

/* ====================================================================== */

/* Inquiry functions for the interpreter */

/* ---------------------------------------------------------------------- */
/** Return the canonical numerical index for a
    colorant name identified in setcolorspace and similar. Return
    COLORANTINDEX_UNKNOWN if the colorant is unknown. */
COLORANTINDEX guc_colorantIndex(const GUCR_RASTERSTYLE *pRasterStyle,
                                NAMECACHE * pColorantName)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  if (pColorantName == system_names + NAME_All)
    return COLORANTINDEX_ALL;
  else if (pColorantName == system_names + NAME_None)
    return COLORANTINDEX_NONE;

  return gucs_colorantIndexInDictionary(pColorantName,
                                        & pRasterStyle->oFullyFledgedColorants);
}

/* ---------------------------------------------------------------------- */
/** guc_get/setBlackColorantIndex allow access to the cached black
   colorant index in the RasterStyle. This is set up by gs_applyEraseColor.
   The black colorant index is cleared in setupRasterStyle
   when a setpagedevice occurs.

   guc_getBlackColorantIndex returns the black colorant, or
   COLORANTINDEX_NONE meaning there is no black colorant, or
   COLORANTINDEX_UNKOWN meaning the black colorant must be worked
   out. */

COLORANTINDEX guc_getBlackColorantIndex(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(pRasterStyle->ciBlack != COLORANTINDEX_UNKNOWN,
    "ciBlack is unknown in guc_getBlackColorantIndex");
  return pRasterStyle->ciBlack;
}

void guc_setBlackColorantIndex(GUCR_RASTERSTYLE *pRasterStyle,
                               COLORANTINDEX ciBlack)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(ciBlack != COLORANTINDEX_UNKNOWN,
    "Trying to set black colorant to unknown in guc_setBlackColorantIndex");
  pRasterStyle->ciBlack = ciBlack;
}

/* ---------------------------------------------------------------------- */
/** Set the colorant mapping from (e.g.)
 *  Cyan -> [ PhotoDarkCyan PhotoLightCyan ]
 * in terms of colorant indexes in the given raster style handle.
 * (note indices NOT names).
 * If a mapping already exists, then it replaces it.
 * If ci_map is NULL then it removes the existing mapping.
 */
Bool guc_setColorantMapping( GUCR_RASTERSTYLE *pRasterStyle ,
                             COLORANTINDEX ci ,
                             const COLORANTINDEX *cis , int32 n )
{
  /* If NULL then remove; note removal is done lazily by merely NULLing.
   * If already exists, then replace.
   * Otherwise add.
   */
  COLORANTMAP *cmap ;
  COLORANTINDEX *cis_new ;

  /* Find the device rasterstyle at the end of the chain. */
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  HQASSERT( ci != COLORANTINDEX_UNKNOWN &&
            ci != COLORANTINDEX_NONE &&
            ci != COLORANTINDEX_ALL ,
            "ci illegal value in guc_setColorantMapping" ) ;
  HQASSERT( ( cis == NULL && n == 0 ) ||
            ( cis != NULL && n != 0 ) ,
            "cis & n illegal values in guc_setColorantMapping" ) ;
  HQASSERT( n >= 0 , "n illegal value in guc_setColorantMapping" ) ;

  cmap = pRasterStyle->cmap ;

  /* Copy input cis. */
  cis_new = NULL ;
  if ( n > 0 ) {
    cis_new = gucs_allocWithHeader(pRasterStyle->dlpools,
                                   ( n + 1 ) * sizeof( COLORANTINDEX ),
                                   MM_ALLOC_CLASS_NCOLOR) ;
    if ( cis_new == NULL )
      return error_handler( VMERROR ) ;

    HqMemCpy( cis_new , cis , n * sizeof( COLORANTINDEX )) ;
    /* All colorant mappings are terminated by COLORANTINDEX_UNKNOWN. */
    cis_new[ n ] = COLORANTINDEX_UNKNOWN ;
  }

  while ( cmap != NULL ) {
    if ( cmap->ci == ci ) {
      /* Already exists, either replace or remove. */
      if ( cmap->cimap != NULL )
        gucs_freeWithHeader(pRasterStyle->dlpools, ( mm_addr_t )cmap->cimap,
                            MM_ALLOC_CLASS_NCOLOR) ;
      cmap->cimap = cis_new ;
      return TRUE ;
    }
    cmap = cmap->next ;
  }

  /* Doesn't exist, so add. */
  if ( cis_new == NULL )
    return TRUE ;

  cmap = gucs_allocWithHeader(pRasterStyle->dlpools, sizeof( COLORANTMAP ),
                              MM_ALLOC_CLASS_NCOLOR) ;
  if ( cmap == NULL ) {
    if ( cis_new != NULL )
      gucs_freeWithHeader(pRasterStyle->dlpools, ( mm_addr_t )cis_new,
                          MM_ALLOC_CLASS_NCOLOR) ;
    return error_handler( VMERROR ) ;
  }

  cmap->ci = ci ;
  cmap->cimap = cis_new ;
  cmap->next = pRasterStyle->cmap ;
  pRasterStyle->cmap = cmap ;

  pRasterStyle->generation = ++generation ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** Return the mapping that exists (if it does) for a given colorant (index).
 * e.g. given Cyan it returns [ PhotoDarkCyan PhotoLightCyan COLORANTINDEX_UNKNOWN ]
 *      (note indices NOT names).
 * e.g. given Fred it returns NULL.
 * note that if ci is COLORANTINDEX_ALL, this routine returns the first
 *      mapping that exists, which can be used to test if any mappings
 *      exist at all.
 */
COLORANTINDEX *guc_getColorantMapping(const GUCR_RASTERSTYLE *pRasterStyle ,
                                      COLORANTINDEX ci)
{
  COLORANTMAP *cmap ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT( ci != COLORANTINDEX_UNKNOWN &&
            ci != COLORANTINDEX_NONE ,
            "ci illegal value in guc_getColorantMapping" ) ;

  cmap = pRasterStyle->cmap ;

  if ( ci == COLORANTINDEX_ALL ) {
    while ( cmap != NULL ) {
      if ( cmap->cimap != NULL )
        return cmap->cimap ;
      cmap = cmap->next ;
    }
  }
  else {
    while ( cmap != NULL ) {
      if ( cmap->ci == ci )
        return cmap->cimap ;
      cmap = cmap->next ;
    }
  }
  return NULL ;
}

/* ---------------------------------------------------------------------- */
/** Return the colorant (index) that maps onto
 * a colorant (index) set which contains the given colorant (index).
 * e.g. given PhotoDarkCyan, it returns Cyan (note indices NOT names).
 * e.g. given Fred is returns COLORANTINDEX_UNKNOWN.
 */
COLORANTINDEX guc_getInverseColorant(const GUCR_RASTERSTYLE *pRasterStyle ,
                                     COLORANTINDEX ci)
{
  COLORANTMAP *cmap ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT( ci != COLORANTINDEX_UNKNOWN &&
            ci != COLORANTINDEX_NONE &&
            ci != COLORANTINDEX_ALL ,
            "ci illegal value in guc_getInverseColorant" ) ;

  cmap = pRasterStyle->cmap ;

  while ( cmap != NULL ) {
    COLORANTINDEX cit ;
    COLORANTINDEX *cimap = cmap->cimap ;
    while (( cit = (*cimap++)) != COLORANTINDEX_UNKNOWN )
      if ( cit == ci )
        return cmap->ci ;
    cmap = cmap->next ;
  }
  return COLORANTINDEX_UNKNOWN ;
}

/* ---------------------------------------------------------------------- */
/** This is like \c guc_colorantIndexPossiblyNewName
    except that it will return COLORANTINDEX_UNKNOWN instead of
    allocating a new number for an unidentified name */
COLORANTINDEX guc_colorantIndexReserved(const GUCR_RASTERSTYLE *pRasterStyle,
                                        NAMECACHE * pColorantName)
{
  COLORANTINDEX ci;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  if (pColorantName == system_names + NAME_Default) {

    ci = COLORANTINDEX_NONE;

  } else {

    ci = gucs_colorantIndexInDictionary(pColorantName,
                                        &pRasterStyle->oFullyFledgedColorants);

    if (ci == COLORANTINDEX_UNKNOWN)
      ci = gucs_colorantIndexInDictionary(pColorantName,
                                          &pRasterStyle->oReservedColorants);
  }

  return ci;
}

/* ---------------------------------------------------------------------- */
/** This is similar to \c guc_colorantIndexReserved, but names
    introduced in this way are not recognised in calls to
    guc_colorantIndex. This is intended to reserve a number for a name
    in the space of colorant indexes, particularly when sethalftone
    mentions it. */
Bool guc_colorantIndexPossiblyNewName(GUCR_RASTERSTYLE *pRasterStyle,
                                      NAMECACHE *pColorantName,
                                      COLORANTINDEX *ci)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  HQASSERT(ci != NULL, "null ci in guc_colorantIndexPossiblyNewName");

  *ci = guc_colorantIndexReserved(pRasterStyle, pColorantName);

  if ( *ci == COLORANTINDEX_UNKNOWN ) {
    *ci = gucs_newColorantIndex(pRasterStyle);
    if ( !gucs_addColorantToDictionary(pRasterStyle->dlpools,
                                       pColorantName, *ci,
                                       &pRasterStyle->oReservedColorants) )
      return FALSE;

    pRasterStyle->generation = ++generation ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool gucs_dictwalk_Promotion(OBJECT * poKey, OBJECT * poValue,
                                    void * pvPrivate)
{
  dictwalk_ci_t * pPromotion = pvPrivate;

  HQASSERT( oType(*poKey) == ONAME, "key is not a name in colorant dictionary");
  HQASSERT( oType(*poValue) == OINTEGER,
            "value is not integer in colorant dictionary");

  if ( oType(*poKey) == ONAME &&
       oType(*poValue) == OINTEGER &&
       oInteger(*poValue) == pPromotion->ci) {
    if (!gucs_addColorantToDictionary(pPromotion->pRasterStyle->dlpools,
                                      oName(*poKey), pPromotion->ci,
                                      & pPromotion->pRasterStyle->oFullyFledgedColorants))
      return FALSE;
    gucs_removeColorantFromDictionary(oName(*poKey),
                                      & pPromotion->pRasterStyle->oReservedColorants);
  }

  return TRUE; /* continue walking */
}

/* ---------------------------------------------------------------------- */
/** This is like \c guc_colorantIndex, returning
   the index of the colorant given its name. However instead of returning
   COLORANTINDEX_UNKNOWN, it will add the colorant to the list of
   known colorants if not already there.

   It will upgrade a reserved name
   to a fully functional separation as required, but not the other way
   around. */
Bool guc_colorantIndexPossiblyNewSeparation(GUCR_RASTERSTYLE *pRasterStyle,
                                            NAMECACHE * pColorantName,
                                            COLORANTINDEX *ci)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  HQASSERT(ci != NULL, "null ci in guc_colorantIndexPossiblyNewSeparation");

  *ci =  gucs_colorantIndexInDictionary(pColorantName,
                                        &pRasterStyle->oFullyFledgedColorants);
  if (*ci == COLORANTINDEX_UNKNOWN) {

    *ci = gucs_colorantIndexInDictionary(pColorantName,
                                         &pRasterStyle->oReservedColorants);

    HQASSERT(*ci != COLORANTINDEX_NONE, "trying to promote Default as a separation name");

    if (*ci != COLORANTINDEX_UNKNOWN) {
      dictwalk_ci_t promotion;

      /* promote the colorant to be fully fledged. Unfortunately this means
         walking the dictionary because we must promote all other colors with
         the same index */

      promotion.pRasterStyle = pRasterStyle;
      promotion.ci = *ci;
#if defined(DEBUG_BUILD)
      promotion.fDoingAll = FALSE; /* not used */
      promotion.fClear = FALSE; /* not used */
#endif /* DEBUG_BUILD */

      if (!walk_dictionary(& pRasterStyle->oReservedColorants,
                           gucs_dictwalk_Promotion,
                           & promotion)) {
        HQFAIL("Should've found a reserved colorant");
      }
    } else {

      /*  create a new fully fledged colorant */
      *ci = gucs_newColorantIndex(pRasterStyle);
      if (!gucs_addColorantToDictionary(pRasterStyle->dlpools,
                                        pColorantName, *ci,
                                        & pRasterStyle->oFullyFledgedColorants))
        return FALSE;
    }

    pRasterStyle->generation = ++generation ;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** We need to establish whether there is any special handling needed for this
   colorant. The ColorantDetails dictionary comes from TrappingDetails in
   the pagedevice. We look for a subdictionary named after the given colorant.
   One of the keys in there is the Adobe-defined ColorantType key, which for
   reasons of clarity we call SpecialHandling. If a ColorantType for this
   colorant is not found, it defaults to Normal. */
sw_pgb_special_handling guc_colorantSpecialHandling(
    const GUCR_RASTERSTYLE *pRasterStyle,
    NAMECACHE * pColorantName)
{
  OBJECT *dict;
  sw_pgb_special_handling specialHandling = SPECIALHANDLING_NONE;
  OBJECT localname = OBJECT_NOTVM_NOTHING ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  theTags(localname) = ONAME | LITERAL | UNLIMITED ;
  oName(localname) = pColorantName ;

  if ( oType(pRasterStyle->oColorantDetails) == ODICTIONARY )
    dict = fast_extract_hash( & pRasterStyle->oColorantDetails , & localname ) ;
  else
    return SPECIALHANDLING_NONE ;

  if ( dict ) {
    OBJECT *theo ;

    if ( oType(*dict) != ODICTIONARY ) {
      HQFAIL( "Expected a dictionary here in guc_colorantSpecialHandling." ) ;
      return SPECIALHANDLING_NONE ;
    }

    theo = fast_extract_hash_name(dict, NAME_ColorantType) ;

    if ( theo ) {
      if ( oType(*theo) != ONAME ) {
        HQFAIL( "Expected a name here in guc_colorantSpecialHandling." ) ;
        return SPECIALHANDLING_NONE ;
      }

      switch ( oNameNumber(*theo) ) {
      case NAME_Normal:
        specialHandling = SPECIALHANDLING_NONE ;
        break ;
      case NAME_Opaque:
        specialHandling = SPECIALHANDLING_OPAQUE ;
        break ;
      case NAME_OpaqueIgnore:
        specialHandling = SPECIALHANDLING_OPAQUEIGNORE ;
        break ;
      case NAME_Transparent:
        specialHandling = SPECIALHANDLING_TRANSPARENT ;
        break ;
      case NAME_TrapZones:
        specialHandling = SPECIALHANDLING_TRAPZONES ;
        break ;
      case NAME_TrapHighlights:
        specialHandling = SPECIALHANDLING_TRAPHIGHLIGHTS ;
        break ;
      default:
        HQFAIL( "Unknown ColorantType in guc_colorantSpecialHandling: assuming Normal" ) ;
      }
    }
  }

  return specialHandling ;
}

/* ---------------------------------------------------------------------- */
/** Return the neutral density associated with this colorant. */
USERVALUE guc_colorantNeutralDensity(const GUCR_RASTERSTYLE *pRasterStyle,
                                     NAMECACHE * pColorantName)
{
  OBJECT *dict = NULL;
  USERVALUE neutralDensity = -1.0f;
  OBJECT localname = OBJECT_NOTVM_NOTHING ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  theTags( localname ) = ONAME | LITERAL | UNLIMITED ;
  oName(localname) = pColorantName ;

  if ( oType(pRasterStyle->oColorantDetails) == ODICTIONARY )
    dict = fast_extract_hash( & pRasterStyle->oColorantDetails , & localname ) ;

  if ( dict ) {
    OBJECT *theo ;

    if ( oType(*dict) == ODICTIONARY ) {
      theo = fast_extract_hash_name(dict, NAME_NeutralDensity) ;

      if ( !object_get_real(theo, &neutralDensity) ) {
        HQFAIL( "Expected a number here in guc_colorantNeutralDensity." ) ;
      }
    }
    else {
      HQFAIL( "Expected a dictionary here in guc_colorantNeutralDensity." ) ;
    }
  }

  return neutralDensity ;
}

/* ---------------------------------------------------------------------- */
/** guc_screenAngleLookupColorantName:

   Expects poDefaultScreenAngles is of the form:
   << /Cyan << /Override bool
               /Angle    number >>  >>

   If pColorantName is found in the DefaultScreenAngles dictionary, the screen
   angle and the override flag from the sub-dictionary are returned.
 */
static Bool guc_screenAngleLookupColorantName(
  const OBJECT *    poDefaultScreenAngles,
  NAMECACHE * pColorantName,
  int32 *     pfFoundColorant,
  USERVALUE * pScreenAngle,
  int32 *     pfOverrideScreenAngle)
{
  OBJECT oLocalName = OBJECT_NOTVM_NOTHING;
  OBJECT * poDict;

  HQASSERT(poDefaultScreenAngles != NULL, "poDefaultScreenAngles is null");
  HQASSERT(oType(*poDefaultScreenAngles) == ODICTIONARY,
           "Expected poDefaultScreenAngles to be a dictionary by now");

  * pfFoundColorant = FALSE;

  theTags(oLocalName) = ONAME | LITERAL | UNLIMITED;
  oName(oLocalName) = pColorantName;

  poDict = fast_extract_hash(poDefaultScreenAngles, & oLocalName);
  if (poDict != NULL) {
    OBJECT * poValue;

    if (oType(*poDict) != ODICTIONARY)
      return error_handler(TYPECHECK);

    /* Angle */
    poValue = fast_extract_hash_name(poDict, NAME_Angle);

    if (poValue == NULL)
      return error_handler(UNDEFINED);

    if ( !object_get_real(poValue, pScreenAngle) )
      return error_handler(TYPECHECK);

    /* Override */
    poValue = fast_extract_hash_name(poDict, NAME_Override);

    if (poValue == NULL)
      return error_handler(UNDEFINED);

    if ( oType(*poValue) != OBOOLEAN)
      return error_handler(TYPECHECK);

    * pfOverrideScreenAngle = oBool(*poValue);

    /* found screen angle info for this colorant */
    * pfFoundColorant = TRUE;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------- */
#define INITIAL_ANGLE       (0.0f)
#define INITIAL_OVERRIDE    (FALSE)

/** guc_colorantScreenAngle:
   Looks for the colorant, or its complement, or the default colorant, in the
   DefaultScreenAngles dictionary. Sets the screen angle in pScreenAngle
   and the override flag in pfOverrideScreenAngle.
   Returns TRUE/FALSE for success/failure.

   *** This routine should only be called when a new colorant is about
       to be added. The screen angle enquiry routine is guc_screenAngle. ***
 */
Bool guc_colorantScreenAngle(GUCR_RASTERSTYLE *pRasterStyle,
                             NAMECACHE * pColorantName,
                             USERVALUE * pScreenAngle,
                             Bool *     pfOverrideScreenAngle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(pColorantName != NULL, "pColorantName null in guc_colorantScreenAngle");
  HQASSERT(pScreenAngle != NULL, "pScreenAngle null in guc_colorantScreenAngle");
  HQASSERT(pfOverrideScreenAngle != NULL, "pfOverrideScreenAngle null");

  *pScreenAngle = INITIAL_ANGLE; /* don't leave it floating */
  *pfOverrideScreenAngle = INITIAL_OVERRIDE;

  if ( oType(pRasterStyle->oDefaultScreenAngles) == ODICTIONARY) {
    Bool fFoundColorant;

    /* 1. Try the given colorant name */
    if (! guc_screenAngleLookupColorantName(& pRasterStyle->oDefaultScreenAngles,
                                            pColorantName, & fFoundColorant,
                                            pScreenAngle, pfOverrideScreenAngle))
      return FALSE;

    if (fFoundColorant)
      return TRUE;

    /* 2. Try the complement of the given colorant name */
    {
      NAMECACHE * pComplementColorantName;
      switch (pColorantName->namenumber) {
        default:           pComplementColorantName = NULL;                        break;
        case NAME_Cyan:    pComplementColorantName = system_names + NAME_Red;     break;
        case NAME_Magenta: pComplementColorantName = system_names + NAME_Green;   break;
        case NAME_Yellow:  pComplementColorantName = system_names + NAME_Blue;    break;
        case NAME_Black:   pComplementColorantName = system_names + NAME_Gray;    break;
        case NAME_Red:     pComplementColorantName = system_names + NAME_Cyan;    break;
        case NAME_Green:   pComplementColorantName = system_names + NAME_Magenta; break;
        case NAME_Blue:    pComplementColorantName = system_names + NAME_Yellow;  break;
        case NAME_Gray:    pComplementColorantName = system_names + NAME_Black;   break;
      }
      if (pComplementColorantName != NULL) {
        if (! guc_screenAngleLookupColorantName(& pRasterStyle->oDefaultScreenAngles,
                                                pComplementColorantName, & fFoundColorant,
                                                pScreenAngle, pfOverrideScreenAngle))
          return FALSE;

        if (fFoundColorant)
          return TRUE;
      }
    }

    /* 3. Try the default colorant name */
    if (! guc_screenAngleLookupColorantName(& pRasterStyle->oDefaultScreenAngles,
                                            system_names + NAME_Default, & fFoundColorant,
                                            pScreenAngle, pfOverrideScreenAngle))
      return FALSE;

    if (fFoundColorant)
      return TRUE;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------- */
Bool guc_colorantSynonym(GUCR_RASTERSTYLE *pRasterStyle,
                         NAMECACHE * pNewName,
                         NAMECACHE * pExistingName,
                         COLORANTINDEX *ci)
{
  OBJECT * poDictionary;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");
  HQASSERT(ci != NULL, "null ci in guc_colorantSynonym");

  *ci = guc_colorantIndexReserved(pRasterStyle, pNewName);
  if (*ci != COLORANTINDEX_UNKNOWN)
    return TRUE; /* it already exists somewhere */

  /* We must at least create the colorant of which this is a synonym in the reserved
     colorants, if it exists in neither colorant dictionary */
  poDictionary = & pRasterStyle->oFullyFledgedColorants;
  *ci = gucs_colorantIndexInDictionary(pExistingName, poDictionary);
  if (*ci == COLORANTINDEX_UNKNOWN) {
    if (!guc_colorantIndexPossiblyNewName(pRasterStyle, pExistingName, ci))
      return FALSE;
    poDictionary = & pRasterStyle->oReservedColorants;
  }

  if (!gucs_addColorantToDictionary(pRasterStyle->dlpools,
                                    pNewName, *ci, poDictionary))
    return FALSE;

  pRasterStyle->generation = ++generation ;

  return TRUE;
}

/* ----------------------------------------------------------------------- */
static COLORANTINDEX gucs_colorantIndexInDictionary(NAMECACHE * pColorantName,
                                                    const OBJECT * poDictionary)
{
  OBJECT * poResult;
  OBJECT cnObj = OBJECT_NOTVM_NOTHING;

  HQASSERT(oType(*poDictionary) == ODICTIONARY,
            "poDictionary is not a dictionary");

  object_store_namecache(&cnObj, pColorantName, LITERAL);
  poResult = fast_extract_hash(poDictionary, &cnObj);

  if (poResult == NULL)
    return COLORANTINDEX_UNKNOWN;

  HQASSERT(oType(*poResult) == OINTEGER,
           "colorant should be an integer in dictionary");

  return (COLORANTINDEX)oInteger(*poResult);
}

/* ---------------------------------------------------------------------- */
static void gucs_removeColorantFromDictionary(NAMECACHE * pColorantName,
                                              OBJECT * poDictionary)
{
  Bool result;
  OBJECT cnObj = OBJECT_NOTVM_NOTHING;

  object_store_namecache(&cnObj, pColorantName, LITERAL);

  result = remove_hash(poDictionary, &cnObj,
                       FALSE /* don't check dictionary protection */);
  HQASSERT(result, "failed to remove colorant name from dictionary");
}

/* ---------------------------------------------------------------------- */
static Bool gucs_dictwalk_newColorantIndex(OBJECT * poKey, OBJECT * poValue,
                                           void * pvPrivate)
{
  int32 * pciMax = pvPrivate;

  HQASSERT(oType(*poKey) == ONAME, "key is not a name in colorant dictionary");
  HQASSERT(oType(*poValue) == OINTEGER,
            "value is not integer in colorant dictionary");

  if ( oType(*poKey) == ONAME && oType(*poValue) == OINTEGER)
    if ( oInteger(*poValue) > *pciMax )
      *pciMax = oInteger(*poValue) ;

  return TRUE; /* continue walking */
}

/* ---------------------------------------------------------------------- */
static COLORANTINDEX gucs_newColorantIndex(GUCR_RASTERSTYLE * pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  if (pRasterStyle->ciMax == -1) {
    Bool result;

    result = walk_dictionary(& pRasterStyle->oReservedColorants,
                             gucs_dictwalk_newColorantIndex,
                             & pRasterStyle->ciMax);
    HQASSERT(result, "failed to walk oReservedColorants");
    result = walk_dictionary(& pRasterStyle->oFullyFledgedColorants,
                             gucs_dictwalk_newColorantIndex,
                             & pRasterStyle->ciMax);
    HQASSERT(result, "failed to walk oReservedColorants");
    /* if there were no colorants at all, this would give us -1 still,
       so the first colorant would be zero in the increment below */
  }

  return ++ (pRasterStyle->ciMax);
}

/* ---------------------------------------------------------------------- */
static Bool gucs_addColorantToDictionary(mm_pool_t *dlpools,
                                         NAMECACHE *pColorantName,
                                         COLORANTINDEX ci,
                                         OBJECT *poDictionary)
{
  OBJECT ciObj = OBJECT_NOTVM_NOTHING;
  OBJECT cnObj = OBJECT_NOTVM_NOTHING;

  HQASSERT(ci >= 0, "invalid colorant index (UNKNOWN not allowed)");
  HQASSERT(oType(*poDictionary) == ODICTIONARY, "poDictionary not a dictionary");

  object_store_integer(&ciObj, (int32) ci);
  object_store_namecache(&cnObj, pColorantName, LITERAL);

  if ( dlpools != NULL )
    return insert_hash_with_alloc(poDictionary, &cnObj, &ciObj,
                                  INSERT_HASH_NAMED|INSERT_HASH_DICT_ACCESS,
                                  gucs_objMemAllocFunc, dlpools);
  else
    return fast_insert_hash(poDictionary, &cnObj, &ciObj);
}

/** Find sRGB color name in dictionary, and fill in appropriate values. */
static Bool gucs_sRGBInDictionary(NAMECACHE *pColorantName,
                                  const OBJECT *poDictionary,
                                  USERVALUE sRGB[3])
{
  OBJECT * poResult;
  uint32 i ;
  OBJECT cnObj = OBJECT_NOTVM_NOTHING;

  HQASSERT(oType(*poDictionary) == ODICTIONARY,
           "poDictionary is not a dictionary");

  object_store_namecache(&cnObj, pColorantName, LITERAL);
  poResult = fast_extract_hash(poDictionary, &cnObj);

  if (poResult == NULL)
    return FALSE ;

  HQASSERT(oType(*poResult) == OARRAY && theLen(*poResult) == 3,
           "sRGB should be an array of three numbers in dictionary");

  poResult = oArray(*poResult) ;

  for ( i = 0 ; i < 3 ; ++i, ++poResult ) {
    if ( !object_get_real(poResult, &sRGB[i]) ) {
      HQFAIL("sRGB array value should be number");
      return FALSE ;
    }
  }

  return TRUE ;
}

typedef struct {
  mm_pool_t *dlpools;
  OBJECT *dictCopy;
} copyArrayDictWalkParams;

static Bool gucs_copyArrayDictWalk(OBJECT *key, OBJECT *val, void *args)
{
  copyArrayDictWalkParams *params = (copyArrayDictWalkParams*)args;
  OBJECT valcopy = OBJECT_NOTVM_NOTHING, *olist, *olistcopy;
  unsigned i;

  HQASSERT(oType(*val) == OARRAY, "Expecting an array");

  olistcopy = gucs_allocWithHeader(params->dlpools,
                                   theLen(*val) * sizeof(OBJECT),
                                   MM_ALLOC_CLASS_GUC_RASTERSTYLE);
  if ( olistcopy == NULL )
    return error_handler(VMERROR);

  olist = oArray(*val);
  for ( i = 0; i < theLen(*val); ++i ) {
    HQASSERT(oType(olist[i]) == OINTEGER || oType(olist[i]) == OREAL,
             "Expected only int or real type");
    Copy(&olistcopy[i], &olist[i]);
  }
  theTags(valcopy) = OARRAY | LITERAL | UNLIMITED;
  SETGLOBJECTTO(valcopy, FALSE);
  theLen(valcopy) = theLen(*val);
  oArray(valcopy) = olistcopy;

  return insert_hash_with_alloc(params->dictCopy, key, &valcopy,
                                INSERT_HASH_NAMED|INSERT_HASH_DICT_ACCESS,
                                gucs_objMemAllocFunc, params->dlpools);
}

static Bool gucs_freeArrayDictWalk(OBJECT *key, OBJECT *val, void *args)
{
  UNUSED_PARAM(OBJECT*, key);
  HQASSERT(oType(*val) == OARRAY, "Expecting an array");
  gucs_freeWithHeader((mm_pool_t*)args, oArray(*val),
                      MM_ALLOC_CLASS_GUC_RASTERSTYLE);
  return TRUE;
}

static Bool gucs_sRGBCopyDictionary(mm_pool_t *dlpools, OBJECT *dictSrc,
                                    OBJECT *dictDst)
{
  copyArrayDictWalkParams params;
  int32 len;
  struct DICT_ALLOC_PARAMS alloc_params;
  struct DICT_ALLOCATOR dict_alloc;

  HQASSERT(oType(*dictSrc) == ODICTIONARY, "dictSrc must be a dictionary");

  getDictLength(len, dictSrc);

  alloc_params.alloc_pool = dlpools;
  alloc_params.alloc_class = MM_ALLOC_CLASS_GUC_RASTERSTYLE;
  dict_alloc.alloc_mem = gucs_dict_allocator;
  dict_alloc.data = (uintptr_t)&alloc_params;
  if (!dict_create(dictDst, &dict_alloc, len,
                   ISNOTVMDICTMARK(SAVEMASK)/** \todo is this right??? */)) {
    return (FALSE);
  }

  params.dlpools = dlpools;
  params.dictCopy = dictDst;
  return walk_dictionary(dictSrc, gucs_copyArrayDictWalk, (void*)&params);
}

/* ---------------------------------------------------------------------- */
/** Find the angle and override flag for ci.
   Returns TRUE/FALSE for success/failure. */
Bool guc_overrideScreenAngle(const GUCR_RASTERSTYLE *pRasterStyle,
                             COLORANTINDEX ci,
                             SYSTEMVALUE * pAngle,
                             Bool *       pfOverride)
{
  GUCR_SHEET * pSheet;
  GUCR_CHANNEL * pChannel;
  GUCR_COLORANT * pColorant;
  int32 defaultIsDict;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!guc_backdropRasterStyle(pRasterStyle), "Expected real raster style");
  HQASSERT(ci != COLORANTINDEX_UNKNOWN && ci != COLORANTINDEX_ALL,
           "invalid colorant index passed to guc_overrideScreenAngle");
  HQASSERT(pAngle != NULL, "null pointer to angle passed to guc_overrideScreenAngle");

  * pAngle = 0.0; /* don't leave it floating */
  * pfOverride = FALSE;

  if ( oType(pRasterStyle->oDefaultScreenAngles) == ODICTIONARY )
    defaultIsDict = TRUE;
  else {
    HQASSERT(oType(pRasterStyle->oDefaultScreenAngles) == ONULL,
             "DefaultScreenAngles should be null");
    defaultIsDict = FALSE;
  }

  if (ci != COLORANTINDEX_NONE) {
    /* 0 If the colorant is mapped, walk down the mappings to the end and assume
     * that the first real colorant has the angle overrides relevant for the
     * virtual device. If the real colorant turns out to be part of a set that
     * has different override angles then we just take the first one.
     */
    if (defaultIsDict) {
      COLORANTINDEX *ciMap;

      do {
        ciMap = guc_getColorantMapping(pRasterStyle, ci);
        if (ciMap != NULL && ciMap[0] >= 0)
          ci = ciMap[0];
      } while (ciMap != NULL );
    }

    /* 1. Have we seen this color colorant before?

       If so, the override for the colorant came from either:
       guc_colorantScreenAngle (which looked in DefaultScreenAngles) or
       addtoseparationorder_ (which set the override for the autosep ci)
     */
    for (pSheet = pRasterStyle->pSheets;
         pSheet != NULL;
         pSheet = pSheet->pNextSheet) {
      for (pChannel = pSheet->pChannels;
           pChannel != NULL;
           pChannel = pChannel->pNextChannel) {
        for (pColorant = pChannel->pColorants;
             pColorant != NULL;
             pColorant = pColorant->pNextColorant) {
          if (pColorant->info.colorantIndex == ci) {
            *pAngle = (SYSTEMVALUE) pColorant->info.screenAngle;
            *pfOverride = (int32) pColorant->info.fOverrideScreenAngle;

            /* If the DefaultScreenAngles pagedevice dict is not null then the
             * extensions manual says we should take the angle from there. Otherwise
             * we should look at the systemparam and use that if it is set.
             * At this point, the screenAngle should either come from the
             * DefaultScreenAngles or else will be the initialised values.
             */
            if (defaultIsDict)
              return TRUE;
            else {
              HQASSERT(*pAngle == INITIAL_ANGLE, "Angle isn't at initial value");
              HQASSERT(*pfOverride == INITIAL_OVERRIDE, "Override isn't at initial value");
              goto TrySystemParam;
            }
          }
        }
      }
    }
  }

  /* 2. Try the default colorant name */
  if (defaultIsDict) {
    Bool fFoundColorant;
    USERVALUE screenAngle;
    if (! guc_screenAngleLookupColorantName(& pRasterStyle->oDefaultScreenAngles,
                                            system_names + NAME_Default,
                                            & fFoundColorant,
                                            & screenAngle,
                                            pfOverride))
      return FALSE;
    if (fFoundColorant) {
      * pAngle = (SYSTEMVALUE) screenAngle;
      /* * pfOverride already set */
      return TRUE;
    }
    else {
      HQFAIL("Didn't find Default entry in DefaultScreenAngles");
    }
  }

  /* 3. Try the OverrideAngle system param */
TrySystemParam:
  {
    SYSTEMPARAMS *systemparams = get_core_context_interp()->systemparams;
    if (systemparams->OverrideAngle >= 0.0) {
      * pAngle = systemparams->OverrideAngle;
      * pfOverride = TRUE;
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool guc_allChannelsOverrideScreenAngle(const GUCR_RASTERSTYLE *pRasterStyle)
{
  GUCR_SHEET* pSheet;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  for (pSheet = pRasterStyle->pSheets;
       pSheet != NULL;
       pSheet = pSheet->pNextSheet) {
    GUCR_CHANNEL* pChannel;
    for (pChannel = pSheet->pChannels;
         pChannel != NULL;
         pChannel = pChannel->pNextChannel) {
      GUCR_COLORANT* pColorant;
      for (pColorant = pChannel->pColorants;
           pColorant != NULL;
           pColorant = pColorant->pNextColorant) {
        if (! pColorant->info.fOverrideScreenAngle)
          return FALSE;
      }
    }
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Fill in the object you supply with a
   procedure which you can then call the interpreter on. Custom
   conversions are from DeviceGray, DeviceRGB or DeviceCMYK to the
   target DeviceN color space. Therefore you must say which one you
   want (and we do not allow anything other than these three simple
   spaces) on deviceSpaceId. The procedure expects 1, 3 or 4 operands
   on the stack in natural order of the simple space and will yield as
   many values as there are colorants in the output space, which is
   the same number as returned in pnColorants in guc_deviceColorSpace
   below */
void guc_CustomConversion(const GUCR_RASTERSTYLE *pRasterStyle,
                          DEVICESPACEID deviceSpaceId,
                          OBJECT * poProcedure)
{
  uint32 nCustomConversion;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(poProcedure != NULL, "guc_CustomConversion given NULL pointer for poProcedure");
  HQASSERT(pRasterStyle->processColorModel == DEVICESPACE_N,
           "custom conversion procedures can only be applied to DeviceN output color spaces");

  switch (deviceSpaceId) {
  case DEVICESPACE_Gray: nCustomConversion = 0; break;
  case DEVICESPACE_RGB:  nCustomConversion = 1; break;
  case DEVICESPACE_CMYK: nCustomConversion = 2; break;
  default:
    nCustomConversion = 0; /* quiet the compiler */
    HQFAIL("deviceSpaceId is not a simple space in guc_CustomConversion");
    break;
  }

  Copy(poProcedure, & pRasterStyle->oCustomConversions[nCustomConversion]);
}

/* ---------------------------------------------------------------------- */
/** Tell what color space the output device
   is working in: essentially this is ProcessColorModel from the page
   device. It also tells you how many _process_ colorants there are in
   the color space (so for DeviceCMYK, for example, there are exactly 4).
 */
void guc_deviceColorSpace(const GUCR_RASTERSTYLE* pRasterStyle,
                          DEVICESPACEID* pDeviceSpaceId, /* [optional] */
                          int32* pnColorants)            /* [optional] */
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  if ( pDeviceSpaceId != NULL )
    *pDeviceSpaceId = pRasterStyle->processColorModel;

  if ( pnColorants != NULL ) {
    switch (pRasterStyle->processColorModel) {
    case DEVICESPACE_Gray: *pnColorants = 1; break;
    case DEVICESPACE_CMYK: *pnColorants = 4; break;
    case DEVICESPACE_RGB:  *pnColorants = 3; break;
    case DEVICESPACE_Lab:  *pnColorants = 3; break;
    case DEVICESPACE_CMY:  *pnColorants = 3; break;
    case DEVICESPACE_RGBK: *pnColorants = 4; break;
    case DEVICESPACE_N:
      *pnColorants = pRasterStyle->nProcessColorants;
      break;
    default:
      *pnColorants = 0;
      HQFAIL("unrecognized process color model in guc_deviceColorSpace");
      break;
    }
    HQASSERT(*pnColorants == pRasterStyle->nProcessColorants,
             "unexpected difference between native color space notion of "
             "number of process colorants and what page device told us");
  }
}

/* ---------------------------------------------------------------------- */
/** Tell what color space we wish calibration
   and color management to be performed in. Normally, this will be the same
   as ProcessColorModel and will be indicated by returning SPACE_notset.
   For PhotoInk printers, we may want a different value. The only other
   supported values are for DeviceGray, DeviceRGB and DeviceCMYK.
 */
void guc_calibrationColorSpace(const GUCR_RASTERSTYLE *pRasterStyle,
                               COLORSPACE_ID * pCalibrationSpaceId)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(pCalibrationSpaceId != NULL,
            "NULL pCalibrationSpaceId incorrectly given to guc_calibrationColorSpace");

  * pCalibrationSpaceId = pRasterStyle->calibrationColorModel;
}

/* ---------------------------------------------------------------------- */
/** Set the flag according to the
   colorspace of the output device. If the colorspace is lab then it
   is not straightforward categorize as subtractive or additive in
   which case the routine should not be called.
 */
Bool guc_deviceColorSpaceSubtractive(const GUCR_RASTERSTYLE *pRasterStyle,
                                     Bool * pfSubtractivePCM )
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(pfSubtractivePCM != NULL,
            "NULL pfSubtractivePCM incorrectly given to guc_deviceColorSpaceSubtractive");

  *pfSubtractivePCM = FALSE;

  switch (pRasterStyle->processColorModel) {
  case DEVICESPACE_Gray:
  case DEVICESPACE_RGB:
  case DEVICESPACE_RGBK:
    return TRUE;

  case DEVICESPACE_CMYK:
  case DEVICESPACE_CMY:
  case DEVICESPACE_N:
    *pfSubtractivePCM = TRUE;
    return TRUE;

  case DEVICESPACE_Lab:
    HQFAIL("lab colorspace is not straightforward to categorize as"
           "additive/subtractive in guc_deviceColorSpaceSubtractive");
    return FALSE;
  default:
    HQFAIL("unrecognized process color model in guc_deviceColorSpaceSubtractive");
    return FALSE;
  }
}

/* ---------------------------------------------------------------------- */
/** Report a COLORSPACE_ID (ie. SPACE_...)
   equivalent of the DEIVCESPACEID colorspace the output device
   is working in: essentially this is ProcessColorModel from the page
   device. */
void guc_deviceToColorSpaceId(const GUCR_RASTERSTYLE *pRasterStyle,
                              COLORSPACE_ID *pColorSpaceId)
{
  DEVICESPACEID pDeviceSpaceId ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT( pColorSpaceId != NULL, "NULL pColorSpaceId" ) ;

  pDeviceSpaceId = pRasterStyle->processColorModel ;

  switch ( pDeviceSpaceId ) {
  case DEVICESPACE_Gray: *pColorSpaceId = SPACE_DeviceGray ; break ;
  case DEVICESPACE_CMYK: *pColorSpaceId = SPACE_DeviceCMYK ; break ;
  case DEVICESPACE_RGB:  *pColorSpaceId = SPACE_DeviceRGB  ; break ;
  case DEVICESPACE_Lab:  *pColorSpaceId = SPACE_Lab        ; break ;
  case DEVICESPACE_CMY:  *pColorSpaceId = SPACE_DeviceCMY  ; break ;
  case DEVICESPACE_RGBK: *pColorSpaceId = SPACE_DeviceRGBK ; break ;
  case DEVICESPACE_N:    *pColorSpaceId = SPACE_DeviceN    ; break ;
  default:
    HQFAIL( "unrecognized process color model in guc_deviceColorSpace" ) ;
    break ;
  }
}

void guc_colorSpace(const GUCR_RASTERSTYLE *pRasterStyle,
                    OBJECT *pColorSpace )
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT( pColorSpace != NULL, "NULL pColorSpace" ) ;

  if ( oType(pRasterStyle->colorSpace) != ONULL ) {
    HQASSERT(pRasterStyle->processColorModel == DEVICESPACE_N ||
             BACKDROP_RASTERSTYLE(pRasterStyle), "Expected a backdrop RS");
    Copy(pColorSpace, &pRasterStyle->colorSpace);
  } else {
    int32 pcmName = NAME_DeviceGray;
    switch ( pRasterStyle->processColorModel) {
    case DEVICESPACE_Gray: pcmName = NAME_DeviceGray; break;
    case DEVICESPACE_CMYK: pcmName = NAME_DeviceCMYK; break;
    case DEVICESPACE_RGB:  pcmName = NAME_DeviceRGB;  break;
    case DEVICESPACE_Lab:  pcmName = NAME_Lab;        break;
    case DEVICESPACE_CMY:  pcmName = NAME_DeviceCMY;  break;
    case DEVICESPACE_N:
      HQFAIL("A DeviceN PCM should have a colorSpace");
      break;
    case DEVICESPACE_RGBK:
      HQFAIL("RGBK used for internal conversions; do not expect RGBK PCM");
      break;
    default:
      HQFAIL("Unrecognized PCM in guc_colorSpace");
      break;
    }
    object_store_name(pColorSpace, pcmName, LITERAL);
  }
}

/* ---------------------------------------------------------------------- */
typedef struct GUC_COLORANTNAME {
  COLORANTINDEX ci;     /* Colorant index to find name for */
  NAMECACHE*    p_name; /* Returned name for colorant index */
} GUC_COLORANTNAME;

/** A \c walk_dictionary() callback function to return name for
 * given colorant index.
 */
static Bool guc_colorantName(OBJECT* poKey,
                             OBJECT* poValue,
                             void* pvPrivate)
{
  GUC_COLORANTNAME* p_colorant_name = pvPrivate;

  HQASSERT((pvPrivate != NULL),
           "guc_colorantName: NULL callback data pointer");
  HQASSERT(oType(*poValue) == OINTEGER,
           "guc_colorantName: reserved colorants dict entry value not integer");

  if ( oInteger(*poValue) == p_colorant_name->ci ) {
    p_colorant_name->p_name = oName(*poKey);
    return FALSE;
  }

  return TRUE;
}

/** Look for the name of a colorant given a colorant
 * dictionary and a colorantindex.
 * NOTE: returns the first name for the index.
 */
/*@null@*/
static NAMECACHE* guc_colorantIndexName(
  /*@notnull@*/ /*@in@*/                OBJECT*           odict,
                                        COLORANTINDEX     ci)
{
  GUC_COLORANTNAME  colorant_name;

  HQASSERT(odict != NULL, "NULL colorant dictionary pointer");
  HQASSERT(ci >= 0, "colorant index out of range");

  colorant_name.ci = ci;
  colorant_name.p_name = NULL;
  (void)walk_dictionary(odict, guc_colorantName, &colorant_name);

  return colorant_name.p_name;
}

/** Return the name of the first fully fledged colorant
 * associated with the colorant index in the named raster style. This name
 * isn't necessarily unique, or even the primary colorant, but can be used to
 * determine if a colorant index is fully fledged.
 */
NAMECACHE* guc_getColorantName(GUCR_RASTERSTYLE* pRasterStyle,
                               COLORANTINDEX     ci)
{
  NAMECACHE *name;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME);

  if (ci == COLORANTINDEX_ALL || ci == COLORANTINDEX_NONE)
    name = NULL;
  else
    name = guc_colorantIndexName(&pRasterStyle->oFullyFledgedColorants, ci);

  /* The photoink case, only match calibration colorants, which must be a
   * reserved colorant.
   */
  if (name == NULL) {
    COLORSPACE_ID calibrationColorSpace;
    guc_calibrationColorSpace(pRasterStyle, &calibrationColorSpace);
    if (calibrationColorSpace != SPACE_notset) {
      if (guc_photoink_colorant(pRasterStyle, ci))
        name = guc_colorantIndexName(&pRasterStyle->oReservedColorants, ci);
    }
  }

  return name;
}

/* ---------------------------------------------------------------------- */
/**
 * Returns true if any of the process colorants in the device colorspace are
 * mapped to another colorant and are therefore not actually output.
 * NOTE: It may be nicer to have a simple flag in the real raster that is set
 * whever a process colorant is mapped, and cleared when there are not any.
 */
Bool guc_processMapped(const GUCR_RASTERSTYLE* p_raster)
{
  COLORANTINDEX   ci;
  COLORSPACE_ID   calibrationColorSpace;
  COLORANTINDEX*  p_ci_map;
  NAMECACHE**     ppnm;

  VERIFY_OBJECT(p_raster, RASTERSTYLE_NAME) ;
  HQASSERT((!guc_backdropRasterStyle(p_raster)),
           "guc_processMapped: raster is not for real device");

  /* For photoink we currently lie (the process colorants are mapped but we
   * pretend they are not)
   */
  guc_calibrationColorSpace(p_raster, &calibrationColorSpace);
  if ( calibrationColorSpace != SPACE_notset )
    return FALSE;

  switch ( p_raster->processColorModel ) {
  case DEVICESPACE_CMYK:
    ppnm = pcmCMYKNames;
    break;
  case DEVICESPACE_RGB:
    ppnm = pcmRGBNames;
    break;
  case DEVICESPACE_Gray:
    ppnm = pcmGyName;
    break;
  case DEVICESPACE_CMY:
    ppnm = pcmCMYNames;
    break;
  case DEVICESPACE_RGBK:
    ppnm = pcmRGBKNames;
    break;

  default:
    /* This handles unexpected device colorspaces */
    HQFAIL("guc_processMapped: device has non-simple device colorspace");
    /* FALLTHROUGH */

  case DEVICESPACE_N:
    /* Process substitution is not defined on a DeviceN device */
    return FALSE;
  }

  do {
    /* Look for process colorant that is reserved and has a mapping */
    ci = gucs_colorantIndexInDictionary(*ppnm, &p_raster->oReservedColorants);
    if ( ci != COLORANTINDEX_UNKNOWN ) {
      p_ci_map = guc_getColorantMapping(p_raster, ci);
      if ( p_ci_map != NULL ) {
        /* Found process colorant that was reserved and had a mapping */
        return TRUE;
      }
    }
  } while ( *(++ppnm) != NULL );

  return FALSE;
}


/* ---------------------------------------------------------------------- */
/**
 * Fill in an array pMapping with the
 * colorant indexes corresponding to the colorants of the given color space
 * deviceSpaceId (some of which may be COLORANTINDEX_UNKNOWN), which must be one
 * of DEVICESPACE_... (but not DEVICESPACE_Lab or DEVICESPACE_N). pMapping must
 * be long enough: the number of elements is determined by guc_deviceColorSpace
 * (and which will always be 1, 3 or 4).
 *
 * The main purpose of this function is to provide a mapping for halftone
 * transfer functions and similar when they are used in the context of an
 * intercept.
 */
Bool guc_simpleDeviceColorSpaceMapping(GUCR_RASTERSTYLE *pRasterStyle,
                                       DEVICESPACEID deviceSpaceId,
                                       COLORANTINDEX * pMapping,
                                       int32 nElementsInMapping)
{
  NAMECACHE** pnNames;
  int32 nColor;
  COLORANTINDEX ci;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  switch (deviceSpaceId) {
  case DEVICESPACE_CMYK: {
    pnNames = pcmCMYKNames;
    HQASSERT(nElementsInMapping == 4, "insufficient elements in mapping array (CMYK)");
    break;
  }
  case DEVICESPACE_Gray: {
    HQASSERT(nElementsInMapping == 1, "insufficient elements in mapping array (Gray)");
    pnNames = pcmGyName;
    break;
  }
  case DEVICESPACE_RGB: {
    HQASSERT(nElementsInMapping == 3, "insufficient elements in mapping array (RGB)");
    pnNames = pcmRGBNames;
    break;
  }
  case DEVICESPACE_CMY: {
    HQASSERT(nElementsInMapping == 3, "insufficient elements in mapping array (CMY)");
    pnNames = pcmCMYNames;
    break;
  }
  case DEVICESPACE_RGBK: {
    HQASSERT(nElementsInMapping == 4, "insufficient elements in mapping array (RGBK)");
    pnNames = pcmRGBKNames;
    break;
  }
  default: {
      HQFAIL("guc_simpleDeviceColorSpaceMapping can only be called on simple spaces");
      return FALSE;
    }
  }

  for (nColor = 0; nColor < nElementsInMapping; nColor++) {
    /* See if the colorant is full fledged on given raster */
    ci = gucs_colorantIndexInDictionary(pnNames[nColor], &pRasterStyle->oFullyFledgedColorants);
    if ( ci == COLORANTINDEX_UNKNOWN ) {
      /* Not fully fledged - see if it is it reserved on given raster */
     if ( !guc_colorantIndexPossiblyNewName(pRasterStyle, pnNames[nColor], &ci) )
        return FALSE;
    }
    pMapping[nColor] = ci;
    HQASSERT(pMapping[nColor] != COLORANTINDEX_UNKNOWN, "Invalid colorant index for simple space");
  }

  return TRUE;
}


/* ---------------------------------------------------------------------- */
/** Produce a mapping from an output colorant
 * index array (the result of a color chain invocation) to the required
 * ordering for the (pseudo)device (and as described in the (psuedo)device
 * colorspace object if appropriate).
 */
Bool guc_outputColorSpaceMapping(mm_pool_t pool,
                                 GS_COLORinfo *colorInfo,
                                 GUCR_RASTERSTYLE *pRasterStyle,
                                 COLORSPACE_ID devColorSpaceId,
                                 OBJECT *devColorSpaceObj,
                                 int32 n_iColorants,
                                 COLORANTINDEX *oColorants,
                                 int32 n_oColorants,
                                 int32 **oToDevMapping,
                                 int32 *n_devColorants )
{
  COLORANTINDEX *dColorants ;
  int32 *dMapping ;
  int32 dIdx, oIdx ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  switch ( devColorSpaceId ) {
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMYK:
  case SPACE_DeviceGray:
  case SPACE_DeviceCMY:
  case SPACE_DeviceRGBK: {
    DEVICESPACEID deviceSpaceId ;
    int32 n_dColorants;

    guc_deviceColorSpace( pRasterStyle, & deviceSpaceId, & n_dColorants ) ;

    /* If we have all the colorants then no mapping is required */
    if ( n_dColorants == n_oColorants ) {
      *oToDevMapping = NULL ;
      *n_devColorants = n_dColorants ;
      return TRUE ;
    }

    /* If the number of colorants is different, then we get the device
     * colorant index array and iterate over the output colorants to
     * find which aren't in the device colorants array (we can assume
     * that the ordering of these arrays is the same).
     * We end up with an array which describes the required order of the
     * color values, with '-1' for the missing channels.
     */
    dMapping = mm_alloc(pool, sizeof( int32 ) * n_dColorants,
                        MM_ALLOC_CLASS_NCOLOR) ;
    if ( dMapping == NULL )
      return error_handler( VMERROR ) ;

    dColorants = mm_alloc(pool, sizeof( COLORANTINDEX ) * n_dColorants,
                          MM_ALLOC_CLASS_NCOLOR) ;
    if ( dColorants == NULL ) {
      mm_free(pool, dMapping, sizeof( int32 ) * n_dColorants) ;
      return error_handler( VMERROR ) ;
    }

    if (!guc_simpleDeviceColorSpaceMapping( pRasterStyle, deviceSpaceId,
                                            dColorants, n_dColorants ))
      return FALSE;

    for ( dIdx = 0, oIdx = 0 ; oIdx < n_oColorants ; dIdx++ ) {
      HQASSERT( dIdx < n_dColorants,
                "bad dIdx in guc_outputColorSpaceMapping" ) ;
      if ( dColorants[ dIdx ] == oColorants[ oIdx ] ) {
        dMapping[ dIdx ] = oIdx ++ ;
      } else {
        dMapping[ dIdx ] = -1 ;
        if ( oColorants[ oIdx ] < 0 ) {
          HQASSERT( oColorants[ oIdx ] == COLORANTINDEX_NONE,
                    "guc_outputColorSpaceMapping - unexpected special colorant." ) ;
          oIdx ++ ;
        }
      }
    }
    while ( dIdx < n_dColorants ) {
      /* We've already used everything in oColorants, so remaining
       * entries must be missing
       */
      dMapping[ dIdx++ ] = -1 ;
    }
    *oToDevMapping = dMapping ;
    *n_devColorants = n_dColorants ;

    mm_free(pool, dColorants, sizeof( COLORANTINDEX ) * n_dColorants) ;
    return TRUE ;
  }

  case SPACE_DeviceN:
    if ( oColorants[ 0 ] == COLORANTINDEX_ALL ) {
      /* Separation /All will be transformed as /DeviceN, with N+1 colorants
       * but we want to report /Separation All with a single colorant.
       */
      dMapping = mm_alloc(pool, sizeof( int32 ), MM_ALLOC_CLASS_NCOLOR ) ;
      if ( dMapping == NULL )
        return error_handler( VMERROR ) ;
      *dMapping = 0 ; /* Use first color value */
      *oToDevMapping = dMapping ;
      *n_devColorants = 1 ;
      return TRUE ;
    } else {
      Bool fAllMatch ;
      int32 res ;

      /* Obtain the colorant index array for the named colorspace */
      dColorants = mm_alloc(pool, sizeof( COLORANTINDEX ) * n_iColorants,
                            MM_ALLOC_CLASS_NCOLOR) ;
      if ( dColorants == NULL )
        return error_handler( VMERROR ) ;

      dMapping = mm_alloc(pool, sizeof( int32 ) * n_iColorants,
                          MM_ALLOC_CLASS_NCOLOR) ;
      if ( dMapping == NULL ) {
        mm_free(pool, dColorants, sizeof( COLORANTINDEX ) * n_iColorants) ;
        return error_handler( VMERROR ) ;
      }

      /* All colorants should be known on the device at this time so no need to
       * do named color interception */
      res = gsc_colorspaceNamesToIndex( pRasterStyle, devColorSpaceObj,
                                        FALSE, FALSE, dColorants,
                                        n_iColorants, colorInfo, &fAllMatch ) ;
      if ( res ) {
        HQASSERT( fAllMatch, "guc_outputColorSpaceMapping: "
                  "ColorspaceNamesToIndex didn't match all colorants" ) ;
        for ( dIdx = 0 ; dIdx < n_iColorants ; dIdx++ ) {
          /* Mark the device colorant as missing */
          dMapping[ dIdx ] = -1 ;
          for ( oIdx = 0 ; oIdx < n_oColorants ; oIdx++ ) {
            /* If we find the device colorant in the output colorants then
             * we mark its position in the mapping array
             */
            if ( dColorants[ dIdx ] == oColorants[ oIdx ] ) {
              dMapping[ dIdx ] = oIdx ;
              break ;
            }
          }
        }

        *oToDevMapping = dMapping ;
        *n_devColorants = n_iColorants ;
      }
      mm_free(pool, dColorants, sizeof( COLORANTINDEX ) * n_iColorants) ;
      return res ;
    }

  case SPACE_Separation:
    HQASSERT( n_oColorants == 1,
              "guc_outputColorSpaceMapping: too many colorants for /Separation" ) ;
    *n_devColorants = 1 ;
    *oToDevMapping = NULL ;
    return TRUE ;

  case SPACE_Pattern:
    /* No color values to report! */
    HQASSERT( n_oColorants == 0,
              "guc_outputColorSpaceMapping: /Pattern should have no color values" ) ;
    HQASSERT( oColorants == NULL,
              "guc_outputColorSpaceMapping: /Pattern should have no colorants array" ) ;
    *oToDevMapping = NULL ;
    *n_devColorants = 0 ;
    return TRUE ;

  case SPACE_Lab:
    HQFAIL( "guc_outputColorSpaceMapping: unsupported output colorspace id" ) ;
    break;

  case SPACE_DeviceK:
  case SPACE_PatternMask:
  case SPACE_CIETableA:
  case SPACE_CIETableABC:
  case SPACE_CIETableABCD:
  case SPACE_CIEBasedABC:
  case SPACE_CIEBasedA:
  case SPACE_CIEBasedDEF:
  case SPACE_CIEBasedDEFG:
  case SPACE_Indexed:
  case SPACE_Preseparation:
  case SPACE_FinalDeviceN:
  case SPACE_CIEXYZ:
  case SPACE_InterceptCMYK:
  case SPACE_InterceptRGB:
  case SPACE_InterceptGray:
    HQFAIL( "guc_outputColorSpaceMapping: unexpected output colorspace id" ) ;
    break;

  case SPACE_notset:
  default:
    HQFAIL( "guc_outputColorSpaceMapping: unrecognised output colorspace id" ) ;
    break ;
  }
  return FALSE ;
}


/* ====================================================================== */
/* now the renderer functions. */

/*
 * Simplify asserts for single channel in monochrome case - should this use
 * gucs_firstSheetChannel()?
 */
#define GUCR_MONO_HAS_ONE_CHANNEL(pChannel) \
  (((pChannel)->pOwningSheet->pOwningRasterStyle->nInterleavingStyle != GUCR_INTERLEAVINGSTYLE_MONO) || \
   (((pChannel)->pOwningSheet->pChannels == (pChannel)) && \
    ((pChannel)->pNextChannel == NULL)) )


/*
 * The following sheet and channel walking functions are prime candidates
 * for inlining via macros iff they are holding things up.
 */

/** Find the next non-omitted sheet with some channels,
 * returning NULL if there are no more.
 */
/*@dependent@*/ /*@null@*/
static inline GUCR_SHEET* gucs_findSheet(
  /*@notnull@*/ /*@in@*/          GUCR_SHEET *const *ppSheet)
{
  GUCR_SHEET *pSheet ;

  HQASSERT(ppSheet != NULL, "gucs_findSheet: Nowhere to get sheet from");

  /* Skip any omitted sheets or channelless sheets */
  while ( (pSheet = *ppSheet) != NULL &&
          (pSheet->fRenderOmit || pSheet->pChannels == NULL) ) {
    ppSheet = &pSheet->pNextSheet ;
  }

  return pSheet;
} /* Function gucs_findSheet */


/** Find the first non-omitted sheet for the raster style,
 * returns NULL when there are no non-omitted sheets.
 * While it is possible that there may be no sheets, the only situation where we
 * can get a sheet without channels is if this was done explicitly with the
 * addtoseparationorder operator or the Positions extension to sethalftone. The
 * most likely cause is that we are indexing sheets starting at 0, and the user
 * assumed it started at 1.
 */
/*@dependent@*/ /*@null@*/
static GUCR_SHEET* gucs_firstRasterSheet(
  /*@notnull@*/                          const GUCR_RASTERSTYLE* pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  /* Use first non-omitted sheet with some channels */
  return gucs_findSheet(&pRasterStyle->pSheets) ;
} /* Function gucs_firstRasterSheet */


/** Find the next non-omitted channel, returning NULL if there
 * are no more.
 */
/*@dependent@*/ /*@null@*/
static inline GUCR_CHANNEL* gucs_findChannel(
  /*@notnull@*/ /*@in@*/              GUCR_CHANNEL *const *ppChannel)
{
  GUCR_CHANNEL *pChannel ;

  HQASSERT(ppChannel != NULL,
           "gucs_findChannel: Nowhere to get channel from");

  /* Skip any omitted channels */
  while ( (pChannel = *ppChannel) != NULL &&
          pChannel->fRenderOmit ) {
    ppChannel = &pChannel->pNextChannel;
  }

  return pChannel;
} /* Function gucs_findChannel */


/** Find the first non-omitted channel on the given sheet,
 * returns NULL when there are no non-omitted channels.
 * We assert that a channel has at least one colorant (perhaps a blank) otherwise
 * we would not have created it.
 */
/*@dependent@*/ /*@null@*/
static GUCR_CHANNEL* gucs_firstSheetChannel(
  /*@notnull@*/                             const GUCR_SHEET*    pSheet)
{
  GUCR_CHANNEL*  pChannel;

  VERIFY_OBJECT(pSheet, SHEET_NAME) ;
  HQASSERT((pSheet->pChannels != NULL),
           "gucs_firstSheetChannel: sheet has no channels");

  /* Use first non-omitted channel */
  pChannel = gucs_findChannel(&pSheet->pChannels) ;

  HQASSERT(pChannel == NULL || pChannel->pColorants != NULL,
           "gucs_firstSheetChannel: channel has no colorants");

  return pChannel;
} /* Function gucs_firstSheetChannel */


/** Find the next non-omitted colorant, returning NULL if there
 * are no more.
 */
/*@dependent@*/ /*@null@*/
static inline GUCR_COLORANT* gucs_findColorant(
  /*@notnull@*/ /*@in@*/                GUCR_COLORANT *const *ppColorant)
{
  GUCR_COLORANT *pColorant ;

  HQASSERT(ppColorant != NULL,
           "gucs_findColorant: Nowhere to get colorant from");

  /* Skip any omitted channels */
  while ( (pColorant = *ppColorant) != NULL &&
          pColorant->fRenderOmit ) {
    ppColorant = &pColorant->pNextColorant;
  }

  return pColorant;
}


/** Find the first non-omitted colorant in the given
 * channel, returns NULL when there are no non-omitted colorants. If the
 * channel is required, then the first colorant is returned regardless.
 */
/*@dependent@*/ /*@null@*/
static inline GUCR_COLORANT *gucs_firstChannelColorant(
  /*@notnull@*/                                 const GUCR_CHANNEL* pChannel)
{
  /* This is the internal logic of gucr_colorantsStart, minus the asserts
     that prevent it from being used in gucr_colorantsNext. */
  GUCR_COLORANT *pColorant ;

  VERIFY_OBJECT(pChannel, CHANNEL_NAME) ;

  pColorant = pChannel->pColorants ;
  HQASSERT(pColorant != NULL, "colorants missing in channel");

  if ( !pChannel->fRequired ) {
    /* Skip to first non-omitted colorant in non-required channel. */
    pColorant = gucs_findColorant(&pColorant) ;
    HQASSERT(pColorant != NULL,
             "All colorants omitted in non-required channel, but channel not omitted");
  }

  HQASSERT(!pColorant->fRenderOmit ||
           pColorant->pOwningChannel->fRequired,
           "Omitted colorant in non-required channel should not be returned") ;

  return pColorant;
}

int32 guc_getSeparationId(const GUCR_CHANNEL *channel)
{
  /* All that matters for this ID is that it is different for each separation.
     We're deliberately not returning a pointer, to avoid temptation to abuse
     it in the skin. If necessary, we could hash the return value to
     obfuscate it, but that's probably overkill. */
  VERIFY_OBJECT(channel, CHANNEL_NAME) ;
  return (int32)((uintptr_t)channel->pOwningSheet / sizeof(GUCR_SHEET)) ;
}

/* ---------------------------------------------------------------------- */
/** gucr_framesStart, gucr_framesMore and gucr_framesNext are the
   functions for iterating over the frames of a page. These frames may
   end with page breaks in which case we end up with separations of
   the page (though still possibly more than one per sheet). Once a
   handle is acquired, one can make inquiries about the current frame
   or frame set, and iterate over the colorants in a frame
 */

GUCR_CHANNEL* gucr_framesStart(const GUCR_RASTERSTYLE *pRasterStyle)
{
  GUCR_CHANNEL* pChannel;
  GUCR_SHEET * pSheet;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  /* Find first non-omitted channel on first non-omitted sheet */
  pSheet = gucs_firstRasterSheet(pRasterStyle);
  pChannel = ((pSheet == NULL)
              ? NULL
              : gucs_firstSheetChannel(pSheet));
  return pChannel;
}

/* ---------------------------------------------------------------------- */
Bool gucr_framesMore(const GUCR_CHANNEL* pChannel)
{
  if ( pChannel != NULL )
    VERIFY_OBJECT(pChannel, CHANNEL_NAME) ;

  HQASSERT(pChannel == NULL || pChannel->pOwningSheet != NULL,
           "parent pointer is NULL in gucr_framesMore");
  HQASSERT(pChannel == NULL || GUCR_MONO_HAS_ONE_CHANNEL(pChannel),
           "more than one channel incorrectly found in monochrome interleaving");

  return pChannel != NULL;
}

/* ---------------------------------------------------------------------- */
void gucr_framesNext(GUCR_CHANNEL** ppChannel)
{
  GUCR_CHANNEL * pChannel = *ppChannel ;
  GUCR_CHANNEL * next;
  int32 nInterleavingStyle;

  VERIFY_OBJECT(pChannel, CHANNEL_NAME) ;

  /* Next frame walks the sheet/channel tree. In band interleaving and pixel
     interleaving modes the channels are within a band, so we should have
     used up all channels in a sheet before moving on to the next sheet
     (frame). (In these modes, we will move to the next frame regardless of
     whether we iterated all of the channels.) In mono mode, there should
     only ever be one channel per sheet. In frame interleaved mode, there
     will be multiple channels per frame. Occasionally we may have a sheet
     without channels - in which case, skip it */

  HQASSERT(pChannel->pOwningSheet != NULL && pChannel->pOwningSheet->pOwningRasterStyle != NULL,
            "parent pointer is NULL in gucr_framesNext");

  nInterleavingStyle = pChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle;

  HQASSERT(GUCR_MONO_HAS_ONE_CHANNEL(pChannel),
            "more than one channel incorrectly found in monochrome interleaving");

  if ( nInterleavingStyle == GUCR_INTERLEAVINGSTYLE_BAND ||
       nInterleavingStyle == GUCR_INTERLEAVINGSTYLE_PIXEL ||
       (next = gucs_findChannel(&pChannel->pNextChannel)) == NULL ) {
    /* Get first non-omitted channel on next non-omitted sheet */
    GUCR_SHEET *pSheet = gucs_findSheet(&pChannel->pOwningSheet->pNextSheet);

    next = (pSheet == NULL) ? NULL : gucs_firstSheetChannel(pSheet);
  }

  *ppChannel = next;
}


Bool gucr_sheetIterateColorants(GUCR_CHANNEL* hf,
                                gucr_sheetIterateColorantsFn fn, void *p)
{
  GUCR_COLORANT* hc;
  GUCR_CHANNEL* hfForAll;

  for ( hfForAll = hf ; ; ) {
    for ( hc = gucr_colorantsStart(hfForAll);
          gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
          gucr_colorantsNext(&hc) ) {
      const GUCR_COLORANT_INFO *colorantInfo;

      if ( gucr_colorantDescription(hc, &colorantInfo) )
        if ( !fn(colorantInfo, p) )
          return FALSE;
    }
    gucr_framesNext(&hfForAll);
    if ( !gucr_framesMore(hfForAll) )
      break;
    if ( gucr_framesStartOfSheet(hfForAll, NULL, NULL) )
      break;
  }
  return TRUE;
}


/* ---------------------------------------------------------------------- */
/** Return the interleaving style for the frame set */
int32 gucr_interleavingStyle(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  return pRasterStyle->nInterleavingStyle;
}

/* ---------------------------------------------------------------------- */
/** Return the separation style for the frame set */
int32 gucr_separationStyle(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  return pRasterStyle->nSeparationStyle;
}


/* ---------------------------------------------------------------------- */
/** Returns the number of values per color component in the final raster
 *
 * As set by setpagedevice. */
int32 gucr_valuesPerComponent(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  return pRasterStyle->nValuesPerComponent;
}


/* ---------------------------------------------------------------------- */
/* A simplified binary log */
int32 gucr_ilog2(int32 vpc)
{
  switch ( vpc ) {
  case 65536: case COLORVALUE_MAX + 1:
    return 16;
  case 4096:
    return 12;
  case 1024:
    return 10;
  case 256:
    return 8;
  case 16:
    return 4;
  case 8:
    return 3;
  case 4:
    return 2;
  default:
    HQFAIL("unexpected number of levels");
    /*@fallthrough@*/
  case 2:
    return 1;
  case 1:
    return 0;
  }
}


int32 gucr_rasterDepth(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME);
  return gucr_ilog2(pRasterStyle->nValuesPerComponent);
}


int32 gucr_rasterDepthShift(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME);
  return gucr_ilog2(gucr_ilog2(pRasterStyle->nValuesPerComponent));
}


Bool gucr_halftoning(const GUCR_RASTERSTYLE *pRasterStyle)
{
  return pRasterStyle->screening ;
}


/* ---------------------------------------------------------------------- */

Bool gucr_framesStartOfSheet(const GUCR_CHANNEL* pChannel,
                             int32 * pnBandMultiplier,
                             int32 * pnSheetNumber)
{
  int32 nInterleavingStyle;
  int32 nBandMultiplier;

  VERIFY_OBJECT(pChannel, CHANNEL_NAME) ;
  HQASSERT(pChannel->pOwningSheet != NULL && pChannel->pOwningSheet->pOwningRasterStyle != NULL,
            "parent pointer is NULL in gucr_framesStartOfSheet");

  nInterleavingStyle = pChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle;
  if (pnBandMultiplier == NULL)
    pnBandMultiplier = & nBandMultiplier; /* avoids special cases below */
  * pnBandMultiplier = 1;

  if (pnSheetNumber != NULL)
    * pnSheetNumber = pChannel->pOwningSheet->nSheetNumber;

  /* only frame mode can not be at the start of a sheet */
  switch (nInterleavingStyle) {
  case GUCR_INTERLEAVINGSTYLE_FRAME:
    return (pChannel == gucs_firstSheetChannel(pChannel->pOwningSheet));

  case GUCR_INTERLEAVINGSTYLE_BAND:
    for (pChannel = pChannel->pOwningSheet->pChannels, *pnBandMultiplier = 0;
         pChannel != NULL;
         pChannel = pChannel->pNextChannel) {
      if ( !pChannel->fRenderOmit ) {
        (*pnBandMultiplier)++;
      }
    }
    return TRUE;

  default:
    return TRUE;
  }
}

/* ---------------------------------------------------------------------- */
Bool gucr_framesEndOfSheet(const GUCR_CHANNEL* pChannel)
{
  int32 nInterleavingStyle;

  VERIFY_OBJECT(pChannel, CHANNEL_NAME) ;
  HQASSERT(pChannel->pOwningSheet != NULL && pChannel->pOwningSheet->pOwningRasterStyle != NULL,
           "parent pointer is NULL in gucr_framesEndOfSheet");

  nInterleavingStyle = pChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle;

  /* Only frame mode can not be at the end of a sheet */
  return (nInterleavingStyle != GUCR_INTERLEAVINGSTYLE_FRAME ||
          gucs_findChannel(&pChannel->pNextChannel) == NULL);
}

/* ---------------------------------------------------------------------- */

/** Return the total number of output channels
   remaining in a frame (or total when positioned at the start of the frame),
   which is not the same as the number of colorants since some channels may
   be unassigned and some may have several colorants assigned */
int32 gucr_framesChannelsTotal(const GUCR_CHANNEL* pChannel)
{
  int32 nCount;

  nCount = 0;

  for ( nCount = 0; pChannel != NULL; pChannel = pChannel->pNextChannel ) {
    nCount++;
  }

  return nCount;
}

/** Return the number of output channels remaining
   in a frame (or the total, when positioned at the start of the frame). This
   may be different to gucr_framesChannelsTotal as it takes channel omission
   into account. */
int32 gucr_framesChannelsLeft(const GUCR_CHANNEL* pChannel)
{
  int32 nCount;

  nCount = 0;

  for ( nCount = 0; pChannel != NULL; pChannel = pChannel->pNextChannel ) {
    if ( !pChannel->fRenderOmit ) {
      nCount++;
    }
  }

  return nCount;
}

/* ---------------------------------------------------------------------- */
/** Given a current frame, gucr_colorantsStart, gucr_colorantsMore and
   gucr_colorantsNext are the functions to control iteration over
   colorants of the frame, that is the colorants which need to be
   rendered in each band of the frame.

   In gucr_colorantsMore, fIncludingPixelInterleaved says whether you
   should get a further iteration for each colorant of pixel
   interleaved mode or not (use the manifest constant GUCR_INCLUDING_PIXEL_INTERLEAVED or
   ! GUCR_INCLUDING_PIXEL_INTERLEAVED as appropriate).
 */
GUCR_COLORANT* gucr_colorantsStart(const GUCR_CHANNEL* pChannel)
{
  /* set the handle to the first colorant of the given channel in
     frame and monochrome interleavings, and to the first colorant of
     the first channel of the current sheet for band and pixel
     interleavings. Generally, this will be where it already is. For
     band and pixel interleavings, we'll warn if the channel isn't the
     first one of the sheet. */

  VERIFY_OBJECT(pChannel, CHANNEL_NAME) ;
  HQASSERT(pChannel->pOwningSheet != NULL && pChannel->pOwningSheet->pOwningRasterStyle != NULL,
           "parent pointer is NULL in gucr_colorantsStart");
  HQASSERT((pChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle !=
            GUCR_INTERLEAVINGSTYLE_BAND &&
            pChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle !=
            GUCR_INTERLEAVINGSTYLE_PIXEL) ||
           gucs_firstSheetChannel(pChannel->pOwningSheet) == pChannel,
           "frame handle should be the first channel of a sheet for band and pixel interleaving");

  return gucs_firstChannelColorant(pChannel) ;
}

/* ---------------------------------------------------------------------- */
Bool gucr_colorantsMore(const GUCR_COLORANT* pColorant,
                        Bool fIncludingPixelInterleaved)
{
  /* in most cases this is an easy test - there are no more if we don't have a
     colorant. However in the case of pixel interleaved where we're told only to
     iterate once for all pixel interleaved colorants, we only say there are more if
     we are on the first channel of the sheet */

  if ( pColorant == NULL )
    return FALSE;

  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;

  HQASSERT(pColorant->pOwningChannel != NULL, "No owning channel") ;
  HQASSERT(pColorant->pOwningChannel->pOwningSheet != NULL, "No owning sheet") ;
  HQASSERT(pColorant->pOwningChannel->pOwningSheet->pOwningRasterStyle != NULL, "No owning rasterstyle") ;

  if ( fIncludingPixelInterleaved ||
       pColorant->pOwningChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle != GUCR_INTERLEAVINGSTYLE_PIXEL ) {
    return TRUE;
  }

  return (gucs_firstSheetChannel(pColorant->pOwningChannel->pOwningSheet) == pColorant->pOwningChannel);
}

/* ---------------------------------------------------------------------- */
void gucr_colorantsNext(GUCR_COLORANT** ppColorant)
{
  GUCR_COLORANT * pColorant = * ppColorant;
  GUCR_CHANNEL * pChannel;
  int32 nInterleavingStyle;

  /* in frame mode, we use only the colorants of the current channel
     (strictly speaking this is also the case for mono, but there
     should then only be one channel). For band and pixel
     interleaving, we move through all the colorants of all the
     channels of a sheet */

  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;
  HQASSERT(pColorant->pOwningChannel != NULL &&
            pColorant->pOwningChannel->pOwningSheet != NULL &&
            pColorant->pOwningChannel->pOwningSheet->pOwningRasterStyle != NULL,
            "parent pointer is NULL in gucr_colorantsNext");

  pChannel = pColorant->pOwningChannel;
  nInterleavingStyle = pChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle;

  HQASSERT((GUCR_MONO_HAS_ONE_CHANNEL(pChannel)),
           "more than one channel incorrectly found in monochrome interleaving");

  pColorant = gucs_findColorant(&pColorant->pNextColorant);

  if ( pColorant == NULL &&
       (nInterleavingStyle == GUCR_INTERLEAVINGSTYLE_BAND ||
        nInterleavingStyle == GUCR_INTERLEAVINGSTYLE_PIXEL) ) {
    /* Get next non-omitted channel if there is one */
    pChannel = gucs_findChannel(&pChannel->pNextChannel);

    if ( pChannel != NULL ) {
      pColorant = gucs_firstChannelColorant(pChannel);

      HQASSERT((pColorant != NULL),
               "no colorants in next channel in gucr_colorantsNext");
    }
  }

  HQASSERT(pColorant == NULL ||
           !pColorant->fRenderOmit ||
           pColorant->pOwningChannel->fRequired,
           "Omitted colorant in non-required channel should not be returned") ;

  *ppColorant = pColorant;
}

/* ---------------------------------------------------------------------- */
/** Indicate if the next colorant iteration
   would advance the channel. Only band interleaved mode supports this, when
   it will usually be true on each call. The only case when this is not true
   is when there are imposed separations in band interleaved composites
   (e.g. step and repeat). */

Bool gucr_colorantsBandChannelAdvance(const GUCR_COLORANT* pColorant)
{
  int32 nInterleavingStyle;

  /* we advance to the next channel when we are at the end of the
     colorants list and there are more channels to come */

  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;
  HQASSERT(pColorant->pOwningChannel != NULL &&
           pColorant->pOwningChannel->pOwningSheet != NULL &&
           pColorant->pOwningChannel->pOwningSheet->pOwningRasterStyle != NULL,
           "parent pointer is NULL in gucr_colorantsBandChannelAdvance");

  nInterleavingStyle =
    pColorant->pOwningChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle;

  return (nInterleavingStyle == GUCR_INTERLEAVINGSTYLE_BAND &&
          gucs_findColorant(&pColorant->pNextColorant) == NULL &&
          gucs_findChannel(&pColorant->pOwningChannel->pNextChannel) != NULL);
}

/* ---------------------------------------------------------------------- */
/** We do not always want to iterate over frames, sheets,
   but instead go straight to a particular colorant. This method returns the
   first GUCR_COLORANT (pColorant) for the given ci to enable all the usual
   colorant enquiry methods to be subsequently called. Note the handle will
   be null unless the colorant is fully fledged. Also, note that multiple
   colorant structures with the same index may exist, because extra
   separations (perhaps with different render properties) were introduced by
   the addtoseparationorder operator. If you care about this, iterate over
   all colorants instead of using this function. */
void gucr_colorantHandle(const GUCR_RASTERSTYLE *pRasterStyle,
                         COLORANTINDEX ci,
                         GUCR_COLORANT** ppColorant)
{
  GUCR_CHANNEL* pChannel;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  for (pChannel = gucr_framesStart(pRasterStyle);
       gucr_framesMore(pChannel);
       gucr_framesNext(& pChannel)) {
    for (*ppColorant = gucr_colorantsStart(pChannel);
         gucr_colorantsMore(*ppColorant, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext(ppColorant)) {
      GUCR_COLORANT* pColorant = *ppColorant;
      if (pColorant->info.colorantIndex == ci)
        return; /* found */
    }
  }
  /* Not a fully fledged colorant */
  *ppColorant = NULL; /* not found */
}

/* ---------------------------------------------------------------------- */
void gucr_setRenderIndex(GUCR_CHANNEL* pChannel, int32 nIndex)
{
  GUCR_COLORANT* pColorant;

  VERIFY_OBJECT(pChannel, CHANNEL_NAME) ;

  for (pColorant = gucr_colorantsStart(pChannel);
       gucr_colorantsMore(pColorant, GUCR_INCLUDING_PIXEL_INTERLEAVED);
       gucr_colorantsNext(&pColorant)) {
    pColorant->pOwningChannel->nRenderIndex = nIndex;
  }
}

/* ---------------------------------------------------------------------- */
int32 gucr_getRenderIndex(const GUCR_CHANNEL* pChannel)
{
  VERIFY_OBJECT(pChannel, CHANNEL_NAME) ;
  return pChannel->nRenderIndex;
}

/* ---------------------------------------------------------------------- */
Bool guc_backdropRasterStyle(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  /* If it's a backdrop, it mustn't be at the bottom of a gstate's stack.
   * If it's not a backdrop, it must be at the bottom of a gstate's stack.
   */
  HQASSERT((pRasterStyle->parent == NULL) ^ BACKDROP_RASTERSTYLE(pRasterStyle),
           "Inconsistent backdrop raster style flag");

  return BACKDROP_RASTERSTYLE(pRasterStyle) ;
}

Bool guc_virtualRasterStyle(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  /* If it's a backdrop, it mustn't be at the bottom of a gstate's stack.
   * If it's not a backdrop, it must be at the bottom of a gstate's stack.
   */
  HQASSERT((pRasterStyle->parent == NULL) ^ BACKDROP_RASTERSTYLE(pRasterStyle),
           "Inconsistent backdrop raster style flag");

  return BACKDROP_VIRTUALDEVICE(pRasterStyle) ;
}

/* ---------------------------------------------------------------------- */
/** gucr_colorantCount/gucr_colorantIndices:
   NOTE: guc_colorantCount returns nColorants which includes duplicates.
   This routine should only be used to work out the size of the colorant
   index buffer to subsequently pass into gucr_colorantIndices.  This
   routine fills in the cis with the colorant indices known to the given
   raster style.  The colorant indices are sorted and duplicates are
   removed.  Also returns nUniqueColorants which excludes duplicates. */
void gucr_colorantCount(const GUCR_RASTERSTYLE* pRasterStyle,
                        uint32* nColorants)
{
  GUCR_CHANNEL* pChannel;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(nColorants != NULL, "nColorants is null");

  *nColorants = 0;

  for (pChannel = gucr_framesStart(pRasterStyle);
       gucr_framesMore(pChannel);
       gucr_framesNext(& pChannel)) {
    GUCR_COLORANT* pColorant;
    for (pColorant = gucr_colorantsStart(pChannel);
         gucr_colorantsMore(pColorant, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext(& pColorant)) {
      const GUCR_COLORANT_INFO *colorantInfo;

      if ( gucr_colorantDescription(pColorant, &colorantInfo) ) {
        ++(*nColorants);
      }
    }
  }
}

void gucr_colorantIndices(const GUCR_RASTERSTYLE* pRasterStyle,
                          COLORANTINDEX* cis,
                          uint32* nUniqueColorants)
{
  GUCR_CHANNEL* pChannel;
  uint32 nColorantsFound = 0;
  uint32 i, j;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(cis != NULL, "cis is null");
  HQASSERT(nUniqueColorants != NULL, "nUniqueColorants is null");

  for (pChannel = gucr_framesStart(pRasterStyle);
       gucr_framesMore(pChannel);
       gucr_framesNext(& pChannel)) {
    GUCR_COLORANT* pColorant;
    for (pColorant = gucr_colorantsStart(pChannel);
         gucr_colorantsMore(pColorant, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext(& pColorant)) {
      const GUCR_COLORANT_INFO *colorantInfo;

      if ( gucr_colorantDescription(pColorant, &colorantInfo) ) {
        cis[nColorantsFound] = colorantInfo->colorantIndex;
        ++nColorantsFound;
      }
    }
  }

  /* Sort the colorants array */
  for (i = 0; i < nColorantsFound; ++i) {
    for (j = i + 1; j < nColorantsFound; ++j) {
      if (cis[i] > cis[j]) {
        COLORANTINDEX ciTmp = cis[j];
        cis[j] = cis[i];
        cis[i] = ciTmp;
      }
    }
  }

  /* Prune duplicates from the array */
  for (i = 0, j = 0; i < nColorantsFound; i++) {
    if (cis[i] != cis[j]) {
      ++j;
      cis[j] = cis[i];
    }
  }

  *nUniqueColorants = (j + 1);
}

/* ---------------------------------------------------------------------- */
/** Return a boolean saying whether a band may
   contain multiple colorants, and therefore whether it is unsafe to
   omit the band completely during rendering */
Bool gucr_colorantsMultiple(const GUCR_COLORANT* pColorant)
{
  GUCR_COLORANT* pColorantT;

  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;
  HQASSERT(pColorant->pOwningChannel != NULL &&
           pColorant->pOwningChannel->pOwningSheet != NULL &&
           pColorant->pOwningChannel->pOwningSheet->pOwningRasterStyle != NULL,
           "parent pointer is NULL in gucr_colorantsMultiple");

  switch (pColorant->pOwningChannel->pOwningSheet->pOwningRasterStyle->nInterleavingStyle) {
  case GUCR_INTERLEAVINGSTYLE_MONO:
  case GUCR_INTERLEAVINGSTYLE_FRAME:
    /* there are multiple colorants if the current channel (Gray for
       monochrome, or whatever frame we are in) has more than one
       colorant */
    pColorantT = gucs_findColorant(&pColorant->pOwningChannel->pColorants) ;

    /*When gucr_colorantsMultiple is called from render_erase_of_band,
      pColorantT may turn out to be NULL if a required channel with no
      colorant is omitted. */
    if (!pColorantT)
      return FALSE;

    return (gucs_findColorant(&pColorantT->pNextColorant) != NULL);
  case GUCR_INTERLEAVINGSTYLE_PIXEL:
  case GUCR_INTERLEAVINGSTYLE_BAND:
    /* there is always another colorant in band or pixel interleaved cases */
    return TRUE;
  default:
    HQFAIL("unrecognised interleaving style in gucr_colorantsMultiple");
    break;
  }

  return FALSE ;
}

static void gucr_setColorantColor(
  /*@notnull@*/ /*@in@*/          GUCR_COLORANT *pColorant,
  /*@notnull@*/ /*@in@*/          OBJECT *posRGB )
{
  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;

  pColorant->fRecalc = FALSE;

  /* Firstly pick up standard names for CMYK, RGB & Gray.
   * Secondly, override any sRGB value from those provided.
   * Spot colors only get their sRGB values set if supplied,
   * and only get their CMYK values set if probed when we get
   * a separation color space set.
   */
  switch ( pColorant->info.name - system_names ) {
  case NAME_Cyan:
    pColorant->info.sRGB[0] = 0.0f ;
    pColorant->info.sRGB[1] = pColorant->info.sRGB[2] = 1.0f ;
    pColorant->info.CMYK[0] = 1.0f ;
    pColorant->info.CMYK[1] = pColorant->info.CMYK[2] = pColorant->info.CMYK[3] = 0.0f ;
    break ;
  case NAME_Magenta:
    pColorant->info.sRGB[0] = pColorant->info.sRGB[2] = 1.0f ;
    pColorant->info.sRGB[1] = 0.0f ;
    pColorant->info.CMYK[0] = pColorant->info.CMYK[2] = pColorant->info.CMYK[3] = 0.0f ;
    pColorant->info.CMYK[1] = 1.0f ;
    break ;
  case NAME_Yellow:
    pColorant->info.sRGB[0] = pColorant->info.sRGB[1] = 1.0f ;
    pColorant->info.sRGB[2] = 0.0f ;
    pColorant->info.CMYK[0] = pColorant->info.CMYK[1] = pColorant->info.CMYK[3] = 0.0f ;
    pColorant->info.CMYK[2] = 1.0f ;
    break ;
  case NAME_Gray:
  case NAME_Black:
    pColorant->info.sRGB[0] = pColorant->info.sRGB[1] = pColorant->info.sRGB[2] = 0.0f ;
    pColorant->info.CMYK[0] = pColorant->info.CMYK[1] = pColorant->info.CMYK[2] = 0.0f ;
    pColorant->info.CMYK[3] = 1.0f ;
    break ;
  case NAME_Red:
    pColorant->info.sRGB[0] = 1.0f ;
    pColorant->info.sRGB[1] = pColorant->info.sRGB[2] = 0.0f ;
    pColorant->info.CMYK[0] = pColorant->info.CMYK[3] = 0.0f ;
    pColorant->info.CMYK[1] = pColorant->info.CMYK[2] = 1.0f ;
    break ;
  case NAME_Green:
    pColorant->info.sRGB[0] = pColorant->info.sRGB[2] = 0.0f ;
    pColorant->info.sRGB[1] = 1.0f ;
    pColorant->info.CMYK[0] = pColorant->info.CMYK[2] = 1.0f ;
    pColorant->info.CMYK[1] = pColorant->info.CMYK[3] = 0.0f ;
    break ;
  case NAME_Blue:
    pColorant->info.sRGB[0] = pColorant->info.sRGB[1] = 0.0f ;
    pColorant->info.sRGB[2] = 1.0f ;
    pColorant->info.CMYK[0] = pColorant->info.CMYK[1] = 1.0f ;
    pColorant->info.CMYK[2] = pColorant->info.CMYK[3] = 0.0f ;
    break ;
  default: /* Unknown name, not a process color, assume spot color. */
    if ( oType(*posRGB) == ODICTIONARY &&
         gucs_sRGBInDictionary(pColorant->info.name, posRGB, pColorant->info.sRGB) ) {
      /* Pick CMYK values for these sRGBs. Does not need to be "correct", since
         the CMYK space is arbitrary. Don't do this during boot-up because
         gstate not fully initialised. */
      (void) gsc_convertRGBtoCMYK(gstateptr->colorInfo,
                                  pColorant->info.sRGB, pColorant->info.CMYK) ;
    }
    else
      pColorant->fRecalc = TRUE;
    break ;
  }
}

/** ---------------------------------------------------------------------- */
/* Return information about a
   colorant identified by its handle. gucr_colorantDescription returns a flag
   indicating if the colorant is renderable (it is known, and is not
   omitted). It is the caller's responsibility to check the flag and override
   any fields from the colorant info that are not relevant. The information
   is returned as a const pointer to details within the colorant structure;
   do NOT cast away the constness of this pointer, this structure should be
   managed from within gu_chan.c ONLY. */
Bool gucr_colorantDescription(const GUCR_COLORANT *pColorant,
                              const GUCR_COLORANT_INFO **info)
{
  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;
  HQASSERT(info, "Need somewhere to put colorant description") ;

  HQASSERT(pColorant->info.name != NULL,
           "Colorant does not have a name") ;
  HQASSERT(pColorant->info.colorantIndex == COLORANTINDEX_UNKNOWN ||
           pColorant->info.colorantIndex >= 0,
           "Colorant has other special index") ;
  HQASSERT(pColorant->info.colorantIndex != COLORANTINDEX_UNKNOWN ||
           pColorant->info.name == &system_names[NAME_None],
           "Blank colorant not called /None") ;
  HQASSERT(pColorant->info.colorantIndex == COLORANTINDEX_UNKNOWN ||
           pColorant->info.colorantType != COLORANTTYPE_UNKNOWN,
           "Colorant does not have valid type") ;
  HQASSERT(pColorant->fRenderOmit ||
           (!pColorant->pOwningChannel->fRenderOmit &&
            !pColorant->pOwningChannel->pOwningSheet->fRenderOmit),
           "Colorant not omitted but channel or sheet is omitted") ;

  *info = &pColorant->info ;

  return (pColorant->info.colorantIndex != COLORANTINDEX_UNKNOWN &&
          !pColorant->fRenderOmit) ;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
/** This is a transition function which for a given
   channel number returns a number which is 0 for Cyan or Red or Gray, 1 for
   Magenta or Green and so on, according to the output space, or if any other
   colorants are found returns -1 (including for trailing unset colorants) */

int32 gucr_getChannelAssignment(const GUCR_RASTERSTYLE *pRasterStyle,
                                int32 nChannel)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(nChannel >= 0 && nChannel < NUM_CHANNEL_ASSIGNMENT,
            "nChannel out of range in gucr_getChannelOrder");

  return pRasterStyle->anChannelAssignment[nChannel];
}

/** Add the given colorant to a colorant set. */
static Bool guc_addColorantToSet(mm_pool_t *dlpools,
                                 GUCR_COLORANTSET **ppColorantSet,
                                 COLORANTINDEX ci)
{
  GUCR_COLORANTSET * pColorantSetOld;
  int32 nWord, mask;

  HQASSERT(ci != COLORANTINDEX_NONE && ci != COLORANTINDEX_ALL && ci != COLORANTINDEX_UNKNOWN,
            "COLORANTINDEX_NONE, ALL, and UNKNOWN not allowed in guc_addColorantToSet");

  pColorantSetOld = *ppColorantSet ;

  /* and set the relevant flag */
  nWord = COLORANT_SET_WORD((int32) ci);
  mask  = 1 << COLORANT_SET_BIT((int32) ci);

  if ( pColorantSetOld == NULL || nWord > pColorantSetOld->cWords ) {
    /* we must reallocate the flag set */
    GUCR_COLORANTSET *pColorantSetNew =
      gucs_alloc(dlpools, sizeof(GUCR_COLORANTSET) + nWord * sizeof(uint32),
                 MM_ALLOC_CLASS_GUC_COLORANTSET) ;
    int32 i ;

    if ( pColorantSetNew == NULL )
      return error_handler(VMERROR);

    pColorantSetNew->cWords = nWord ;
    pColorantSetNew->nSet = 0 ;
    for ( i = 0 ; i <= nWord ; ++i )
      pColorantSetNew->afMember[i] = 0;

    if ( pColorantSetOld != NULL ) {
      /* transfer the old flags into the new */
      for ( i = 0 ; i <= pColorantSetOld->cWords ; ++i )
        pColorantSetNew->afMember[i] = pColorantSetOld->afMember[i];
      pColorantSetNew->nSet = pColorantSetOld->nSet ;

      /* discard the old flags */
      gucs_free(dlpools, (mm_addr_t)pColorantSetOld,
                sizeof(GUCR_COLORANTSET) + pColorantSetOld->cWords * sizeof(uint32),
                MM_ALLOC_CLASS_GUC_COLORANTSET);
    }

    *ppColorantSet = pColorantSetNew;
    pColorantSetOld = pColorantSetNew;
  }

  if ( (pColorantSetOld->afMember[nWord] & mask) == 0 ) {
    ++pColorantSetOld->nSet ;
    pColorantSetOld->afMember[nWord] |= mask;
    HQASSERT(pColorantSetOld->nSet > 0 &&
             pColorantSetOld->nSet <= 32 * (pColorantSetOld->cWords + 1),
             "Colorant set nSet out of range in guc_addColorantToSet");
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Remove a colorant from a colorant set. */
static void guc_removeColorantFromSet(GUCR_COLORANTSET *pColorantSet, COLORANTINDEX ci)
{
  int32 nWord, mask;

  HQASSERT(ci != COLORANTINDEX_NONE && ci != COLORANTINDEX_UNKNOWN,
            "ci None or Unknown not allowed in guc_removeColorantFromSet");

  if (pColorantSet == NULL)
    return;

  if (ci == COLORANTINDEX_ALL) { /* Clear all flags */
    for ( nWord = 0 ; nWord <= pColorantSet->cWords ; ++nWord )
      pColorantSet->afMember[nWord] = 0 ;
    pColorantSet->nSet = 0 ;
    return ;
  }

  nWord = COLORANT_SET_WORD((int32) ci);
  mask  = 1 << COLORANT_SET_BIT((int32) ci);

  if (nWord > pColorantSet->cWords) /* no flag for it, cannot remove */
    return;

  /* otherwise clear the value of the bit */
  if ( (pColorantSet->afMember[nWord] & mask) != 0 ) {
    --pColorantSet->nSet ;
    pColorantSet->afMember[nWord] &= ~mask ;
    HQASSERT(pColorantSet->nSet >= 0 &&
             pColorantSet->nSet < 32 * (pColorantSet->cWords + 1),
             "Colorant set nSet out of range in guc_removeColorantFromSet");
  }
}

/* ---------------------------------------------------------------------- */
/** Query whether the colorant is in a colorant set. */
static Bool guc_colorantInSet(GUCR_COLORANTSET *pColorantSet, COLORANTINDEX ci)
{
  int32 nWord, mask;

  HQASSERT(ci != COLORANTINDEX_NONE && ci != COLORANTINDEX_UNKNOWN,
            "ci None or Unknown not allowed in guc_colorantInSet");

  if (ci == COLORANTINDEX_ALL)
    return FALSE;

  if (pColorantSet == NULL)
    return FALSE;

  nWord = COLORANT_SET_WORD((int32) ci);
  mask  = 1 << COLORANT_SET_BIT((int32) ci);

  if (nWord > pColorantSet->cWords) {
    /* no flag for it, therefore implied false */
    return FALSE;
  }

  /* otherwise it is the value of the bit */
  return ((pColorantSet->afMember[nWord]) & mask) != 0;
}

static Bool gucs_copyColorantSet(mm_pool_t *dlpools, GUCR_COLORANTSET *setSrc,
                                 GUCR_COLORANTSET **setDst)
{
  int32 i;

  if ( setSrc == NULL ) {
    *setDst = NULL;
    return TRUE;
  }

  *setDst = gucs_alloc(dlpools,
                       sizeof(GUCR_COLORANTSET) + setSrc->cWords * sizeof(uint32),
                       MM_ALLOC_CLASS_GUC_COLORANTSET);
  if ( *setDst == NULL )
    return error_handler(VMERROR);

  (*setDst)->cWords = setSrc->cWords;
  for ( i = 0; i <= setSrc->cWords; ++i )
    (*setDst)->afMember[i] = setSrc->afMember[i];
  (*setDst)->nSet = setSrc->nSet;

  return TRUE;
}


/* ====================================================================== */
/* Setup functions for setpagedevice */

/* ---------------------------------------------------------------------- */
static Bool gucs_setupSeparationDetails( OBJECT *poSeparationDetails ,
                                         DEVICESPACEID processColorModel ,
                                         uint8 *fAbortSeparations ,
                                         uint8 *fAddAllSpotColors ,
                                         uint8 *fRemoveAllSpotColors ,
                                         uint8 *fOmitMonochrome ,
                                         uint8 *fOmitSeparations ,
                                         uint8 *fOmitSpot ,
                                         uint8 *fOmitExtraSpot ,
                                         uint8 *fOmitProcess ,
                                         OMIT_DETAILS *pOmitDetails ,
                                         OBJECT **ppoOmitColorants )
{
  OBJECT *theo ;

  enum {
    details_Abort, details_Omit, details_Ignore, details_Add, details_Remove,
    details_DUMMY
  } ;
  static NAMETYPEMATCH details_match[details_DUMMY + 1] = {
    { NAME_Abort  | OOPTIONAL , 2 , { OBOOLEAN , ONULL }},
    { NAME_Omit   | OOPTIONAL , 2 , { ODICTIONARY , ONULL }},
    { NAME_Ignore | OOPTIONAL , 2 , { ODICTIONARY , ONULL }},
    { NAME_Add    | OOPTIONAL , 2 , { OBOOLEAN , ONULL }},
    { NAME_Remove | OOPTIONAL , 2 , { OBOOLEAN , ONULL }},
    DUMMY_END_MATCH
  } ;

  enum {
    omit_Monochrome, omit_Separations, omit_Process, omit_Spot, omit_Colorants,
    omit_ExtraSpot, omit_DUMMY
  } ;
  static NAMETYPEMATCH omit_match[omit_DUMMY + 1] = {
    { NAME_Monochrome    | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_Separations   | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_Process       | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_Spot          | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_Colorants     | OOPTIONAL , 2 , { OARRAY, OPACKEDARRAY }},
    { NAME_ExtraSpot     | OOPTIONAL , 1 , { OBOOLEAN }},
    DUMMY_END_MATCH
  } ;

  enum {
    ignore_BeginPage, ignore_EndPage, ignore_ImageContents, ignore_SuperBlacks,
    ignore_RegisterMarks, ignore_DUMMY
  } ;
  static NAMETYPEMATCH ignore_match[ignore_DUMMY + 1] = {
    { NAME_BeginPage     | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_EndPage       | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_ImageContents | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_SuperBlacks   | OOPTIONAL , 1 , { OBOOLEAN }},
    { NAME_RegisterMarks | OOPTIONAL , 1 , { OBOOLEAN }},
    DUMMY_END_MATCH
  } ;

  (*fAbortSeparations)    = FALSE ;
  (*fAddAllSpotColors)    = FALSE ;
  (*fRemoveAllSpotColors) = FALSE ;

  (*fOmitMonochrome)      = FALSE ;
  (*fOmitSeparations)     = FALSE ;
  (*fOmitSpot)            = FALSE ;
  (*fOmitExtraSpot)       = FALSE ;
  (*fOmitProcess)         = FALSE ;
  pOmitDetails->beginpage  = TRUE ;
  pOmitDetails->endpage    = TRUE ;
  pOmitDetails->imagecontents = FALSE ;
  pOmitDetails->superblacks = FALSE ;
  pOmitDetails->registermarks = FALSE ;
  pOmitDetails->knockouts = TRUE ;

  /* This can happen from the init call. */
  if ( oType(*poSeparationDetails) == ONULL )
    return TRUE ;

  HQASSERT( poSeparationDetails , "poSeparationDetails NULL in setup_separationdetails" ) ;
  if ( ! dictmatch( poSeparationDetails , details_match ))
    return FALSE ;

  theo = details_match[details_Abort].result ;
  if ( theo && oType(*theo) == OBOOLEAN )
    (*fAbortSeparations) = ( uint8 )oBool(*theo) ;

  theo = details_match[details_Omit].result ;
  if ( theo && oType(*theo) == ODICTIONARY ) {
    if ( ! dictmatch( theo , omit_match ))
      return FALSE ;

    (*fOmitMonochrome)  = TRUE ;
    (*fOmitSeparations) = TRUE ;
    (*fOmitProcess)  = TRUE ;
    (*fOmitSpot) = TRUE ;
    (*fOmitExtraSpot) = TRUE ;

    theo = omit_match[omit_Monochrome].result ;
    if ( theo )
      (*fOmitMonochrome) = ( uint8 )oBool(*theo) ;

    theo = omit_match[omit_Separations].result ;
    if ( theo )
      (*fOmitSeparations) = ( uint8 )oBool(*theo) ;

    theo = omit_match[omit_Process].result ;
    if ( theo )
      (*fOmitProcess) = ( uint8 )oBool(*theo) ;

    theo = omit_match[omit_Spot].result ;
    if ( theo )
      (*fOmitSpot) = ( uint8 )oBool(*theo) ;

    theo = omit_match[omit_Colorants].result ;
    if ( theo != NULL ) {
      int32 i ;
      for ( i = 0 ; i < theLen(*theo) ; ++i ) {
        NAMECACHE *name ;
        if ( !gucs_nameFromObject(&oArray(*theo)[i], &name) )
          return FALSE ;
      }
      *ppoOmitColorants = omit_match[omit_Colorants].result ;
    } else
      *ppoOmitColorants = NULL ;

    theo = omit_match[omit_ExtraSpot].result ;
    if ( theo )
      (*fOmitExtraSpot) = ( uint8 )oBool(*theo) ;
  }

  theo = details_match[details_Ignore].result ;
  if ( theo && oType(*theo) == ODICTIONARY ) {
    if ( ! dictmatch( theo , ignore_match ))
      return FALSE ;

    theo = ignore_match[ignore_BeginPage].result ;
    if ( theo )
      pOmitDetails->beginpage = ( uint8 )oBool(*theo) ;

    theo = ignore_match[ignore_EndPage].result ;
    if ( theo )
      pOmitDetails->endpage = ( uint8 )oBool(*theo) ;

    theo = ignore_match[ignore_ImageContents].result ;
    if ( theo )
      pOmitDetails->imagecontents = ( uint8 )oBool(*theo) ;

    theo = ignore_match[ignore_SuperBlacks].result ;
    /* SuperBlacks only makes sense in a space which supports Black. DeviceN
       is passed here, it will be removed later if there is no black index */
    if ( theo )
      if ( processColorModel == DEVICESPACE_Gray ||
           processColorModel == DEVICESPACE_CMYK ||
           processColorModel == DEVICESPACE_RGBK ||
           processColorModel == DEVICESPACE_N ) /* Benefit of the doubt... */
        pOmitDetails->superblacks = ( uint8 )oBool(*theo) ;

    theo = ignore_match[ignore_RegisterMarks].result ;
    /* RegisterMarks only makes sense in a space which has more than only one
       way to represent gray shades. We make no assumptions about DeviceN. */
    if ( theo )
      if ( processColorModel == DEVICESPACE_CMYK ||
           processColorModel == DEVICESPACE_RGBK ||
           processColorModel == DEVICESPACE_N ) /* Benefit of the doubt... */
        pOmitDetails->registermarks = ( uint8 )oBool(*theo) ;

    /* No PS interface to set ignore knockouts (default, TRUE). Use
       guc_omitSetIgnoreKnockouts() instead. */
  }

  theo = details_match[details_Add].result ;
  if ( theo && oType(*theo) == OBOOLEAN )
    (*fAddAllSpotColors) = ( uint8 )oBool(*theo) ;

  theo = details_match[details_Remove].result ;
  if ( theo && oType(*theo) == OBOOLEAN )
    (*fRemoveAllSpotColors) = ( uint8 )oBool(*theo) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool guc_setupRasterStyleGeneric(
  Bool fBackdropRasterStyle,
  Bool screening,
  mm_pool_t *dlpools,
  OBJECT * poProcessColorModel,
  OBJECT * poCalibrationColorModel,
  OBJECT * poInterleavingStyle,
  OBJECT * poValuesPerComponent,
  OBJECT * poSeparationStyle,
  OBJECT * poNumProcessColorants,
  OBJECT * poSeparationDetails,
  OBJECT * poSeparationOrder,
  OBJECT * poColorChannels,
  OBJECT * poFullyFledgedColorants,
  OBJECT * poReservedColorants,
  OBJECT * poDefaultScreenAngles,
  OBJECT * poCustomConversions,
  OBJECT * posRGB,
  OBJECT * poProcessColorants,
  OBJECT * poColorantPresence,
  OBJECT * poProcessColorant_Black,
  OBJECT * poColorantDetails,
  OBJECT * poObjectTypeMap,
  Bool fInheritSpotColors,
  /*@notnull@*/ /*@out@*/               GUCR_RASTERSTYLE **ppRasterStyle)
{
  GUCR_RASTERSTYLE * pRasterStyle;
  Bool fResult = FALSE;

  DEVICESPACEID processColorModel;
  NAMECACHE * pProcessColorModel;
  COLORSPACE_ID calibrationColorModel;
  NAMECACHE * pCalibrationColorModel;
  int32 nInterleavingStyle;
  int32 nValuesPerComponent;
  int32 nSeparationStyle;
  int32 nChannel;
  int32 nProcessColorants;
  size_t i;

  OBJECT *poOmitColorants = NULL ;

  OMIT_DETAILS omitDetails ;
  uint8 fAbortSeparations    = FALSE ;
  uint8 fAddAllSpotColors    = FALSE ;
  uint8 fRemoveAllSpotColors = FALSE ;
  uint8 fOmitMonochrome      = FALSE ;
  uint8 fOmitSeparations     = FALSE ;
  uint8 fOmitSpot            = FALSE ;
  uint8 fOmitExtraSpot       = FALSE ;
  uint8 fOmitProcess         = FALSE ;

  GUCR_SHEET *pSheet;

  if (next_rasterstyle_id == MAXUINT32) {
    HQFAIL("Run out of rasterstyle ids");
    return error_handler(LIMITCHECK);
  }

  /* extract ProcessColorModel from its object */

  if (! gucs_nameFromObject(poProcessColorModel, & pProcessColorModel))
    return FALSE;
  switch (pProcessColorModel - system_names) {
  case NAME_DeviceGray:
    processColorModel = DEVICESPACE_Gray;
    break;
  case NAME_DeviceRGB:
    processColorModel = DEVICESPACE_RGB;
    break;
  case NAME_DeviceCMYK:
    processColorModel = DEVICESPACE_CMYK;
    break;
  case NAME_DeviceN:
    processColorModel = DEVICESPACE_N;
    break;
  case NAME_DeviceCMY:
    processColorModel = DEVICESPACE_CMY;
    break;
  case NAME_DeviceRGBK:
    processColorModel = DEVICESPACE_RGBK;
    break;
  case NAME_DeviceLab:
    processColorModel = DEVICESPACE_Lab;
    break;
  default:
    return detail_error_handler(RANGECHECK, "Invalid ProcessColorModel");
  }

  /* extract CalibrationColorModel from its object */

  if ( oType(*poCalibrationColorModel) == ONULL)
    calibrationColorModel = SPACE_notset;
  else {
    if (! gucs_nameFromObject(poCalibrationColorModel, & pCalibrationColorModel))
      return FALSE;
    switch (pCalibrationColorModel - system_names) {
    case NAME_DeviceGray:
      calibrationColorModel = SPACE_DeviceGray;
      break;
    case NAME_DeviceRGB:
      calibrationColorModel = SPACE_DeviceRGB;
      break;
    case NAME_DeviceCMYK:
      calibrationColorModel = SPACE_DeviceCMYK;
      break;
    default:
      return detail_error_handler(RANGECHECK, "Invalid CalibrationColorModel");
    }
    /* The color chain construction will put calibration in the wrong place if
       calibrationColorModel is the same as processColorModel */
    if (pCalibrationColorModel == pProcessColorModel)
      calibrationColorModel = SPACE_notset;
  }

  /* extract ValuesPerComponent from its object */

  if ( oType(*poValuesPerComponent) != OINTEGER)
    return detail_error_handler(TYPECHECK, "ValuesPerComponent must be an integer");
  nValuesPerComponent = oInteger(*poValuesPerComponent);
  switch (nValuesPerComponent) {
  case 65536: /* Shorthand for pseudo-16 bit. */
    nValuesPerComponent = COLORVALUE_MAX + 1 ;
    /*@fallthrough@*/
  case 256:
  case 1024:
  case 4096:
  case COLORVALUE_MAX + 1:
  case 2:
  case 4:
  case 16:
    break;
  default:
    return detail_error_handler(RANGECHECK, "Invalid ValuesPerComponent");
  }

  /* extract SeparationStyle from its object */
  if ( oType(*poSeparationStyle) != OINTEGER)
    return detail_error_handler(TYPECHECK, "SeparationStyle must be an integer");
  nSeparationStyle = oInteger(*poSeparationStyle);
  switch (nSeparationStyle) {
  case GUCR_SEPARATIONSTYLE_MONOCHROME:
    if ( theLen(*poSeparationOrder) != 1 ) {
      return detail_error_handler(CONFIGURATIONERROR,
               "Monochrome output only supported with one separation.");
    }
    break ;
  case GUCR_SEPARATIONSTYLE_SEPARATIONS:
  case GUCR_SEPARATIONSTYLE_COLORED_SEPARATIONS:
  case GUCR_SEPARATIONSTYLE_PROGRESSIVES:
  case GUCR_SEPARATIONSTYLE_COMPOSITE:
    break;
  default:
    return detail_error_handler(RANGECHECK, "Invalid SeparationStyle");
  }

  /* Extract InterleavingStyle from its object and perform sanity checks for
     disallowed combinations */

  if ( oType(*poInterleavingStyle) != OINTEGER)
    return detail_error_handler(TYPECHECK, "InterleavingStyle must be an integer");
  nInterleavingStyle = oInteger(*poInterleavingStyle);
  switch (nInterleavingStyle) {
  case GUCR_INTERLEAVINGSTYLE_MONO:
    if ( nSeparationStyle != GUCR_SEPARATIONSTYLE_MONOCHROME &&
         nSeparationStyle != GUCR_SEPARATIONSTYLE_SEPARATIONS ) {
      return detail_error_handler(CONFIGURATIONERROR,
        "Monochrome interleaving not supported with colored separations, progressives or composite output.");
    }
    if ( theLen(*poColorChannels) != 1 ) {
      return detail_error_handler(CONFIGURATIONERROR,
        "Monochrome interleaving not supported with multiple channel output.");
    }
    break ;
  case GUCR_INTERLEAVINGSTYLE_PIXEL:
    if ( screening ) {
      return detail_error_handler(CONFIGURATIONERROR,
        "Halftoned output cannot be pixel interleaved.");
    }
    /** \todo ajcd 2010-04-05: Should we have this restriction? It's a pretty
        silly configuration since we'll only have one channel, but it will
        work, so should we care? */
    if ( !fBackdropRasterStyle && theLen(*poColorChannels) == 1 ) {
      return detail_error_handler(CONFIGURATIONERROR,
        "Pixel interleaving not supported with single channel output.");
    }
    break ;
  case GUCR_INTERLEAVINGSTYLE_BAND:
  case GUCR_INTERLEAVINGSTYLE_FRAME:
    break;
  default:
    return detail_error_handler(RANGECHECK, "Invalid InterleavingStyle");
  }

  /* extract NumProcessColorants from its object */
  if ( oType(*poNumProcessColorants) != OINTEGER)
    return detail_error_handler(TYPECHECK, "NumProcessColorants must be an integer");
  nProcessColorants = oInteger(*poNumProcessColorants);

  if (! gucs_setupSeparationDetails( poSeparationDetails ,
                                     processColorModel,
                                     & fAbortSeparations ,
                                     & fAddAllSpotColors ,
                                     & fRemoveAllSpotColors ,
                                     & fOmitMonochrome ,
                                     & fOmitSeparations ,
                                     & fOmitSpot ,
                                     & fOmitExtraSpot ,
                                     & fOmitProcess ,
                                     & omitDetails ,
                                     & poOmitColorants ))
    return FALSE;

  /* check CustomConversions - though we don't insist the three objects are
     executable or procedures, the top level must be an array of length 3 */
  if ( oType(*poCustomConversions) != ONULL ) {
    if ( oType(*poCustomConversions) != OARRAY &&
         oType(*poCustomConversions) != OPACKEDARRAY)
      return detail_error_handler(TYPECHECK, "CustomConversions must be an array");

    if (theLen(*poCustomConversions) != 3)
      return detail_error_handler(RANGECHECK, "CustomConversions array must be length 3");
  }

  /* populate the header structure */

  pRasterStyle = gucs_alloc(dlpools, sizeof(GUCR_RASTERSTYLE),
                            MM_ALLOC_CLASS_GUC_RASTERSTYLE);
  if ( pRasterStyle == NULL )
    return error_handler(VMERROR);

#define return DO_NOT_RETURN_-_GO_TO_cleanup_setupRasterStyleGeneric_INSTEAD!

  NAME_OBJECT(pRasterStyle, RASTERSTYLE_NAME);
  pRasterStyle->id = next_rasterstyle_id++;
  pRasterStyle->flags = fBackdropRasterStyle ? RASTERSTYLE_FLAG_BACKDROP : 0u;
  pRasterStyle->nReferenceCount = 1;
  pRasterStyle->next = NULL;
  pRasterStyle->parent = NULL;
  pRasterStyle->dlpools = dlpools;
  pRasterStyle->screening = screening ;
  pRasterStyle->nInterleavingStyle = nInterleavingStyle;
  pRasterStyle->nValuesPerComponent = nValuesPerComponent;
  pRasterStyle->nSeparationStyle = nSeparationStyle;
  pRasterStyle->nProcessColorants = nProcessColorants;
  pRasterStyle->equivalentRealColorants = NULL;

  pRasterStyle->fInheritSpotColors = fInheritSpotColors;
  pRasterStyle->fAbortSeparations = fAbortSeparations;
  pRasterStyle->fAddAllSpotColors = fAddAllSpotColors;
  pRasterStyle->fRemoveAllSpotColors = fRemoveAllSpotColors;
  pRasterStyle->fOmitMonochrome = fOmitMonochrome;
  pRasterStyle->fOmitSeparations = fOmitSeparations;
  pRasterStyle->fOmitExtraSpot = fOmitExtraSpot;
  pRasterStyle->fOmitProcess = fOmitProcess;
  pRasterStyle->fHaveEquivalent = TRUE ;
  pRasterStyle->pCurrentOmits = NULL ;
  pRasterStyle->pColorantOmits = NULL ;
  pRasterStyle->omitDetails = omitDetails ;

  pRasterStyle->pBlackName = NULL;

  /* we lazily work out new colorant indexes, when ciMax is -1 */
  pRasterStyle->ciMax = -1;

  /* black colorant worked out lazily (by guc_blackColorantIndex) */
  pRasterStyle->ciBlack = COLORANTINDEX_NONE;

  /* Colorant mappings. */
  pRasterStyle->cmap = NULL ;

  /* oFullyFledgedColorants and oReservedColorants can either be allocated from
     PSVM or dlpool. The rasterstyle takes ownership of the dlpool memory. */
  Copy(object_slot_notvm(&pRasterStyle->oFullyFledgedColorants), poFullyFledgedColorants);
  Copy(object_slot_notvm(&pRasterStyle->oReservedColorants), poReservedColorants);
  if ( dlpools != NULL ) {
    *poFullyFledgedColorants = onothing;
    *poReservedColorants = onothing;
  }
  HQASSERT(dlpools == NULL || oType(*poDefaultScreenAngles) == ONULL,
           "DefaultScreenAngles not copied into dlpool memory");
  Copy(object_slot_notvm(&pRasterStyle->oDefaultScreenAngles), poDefaultScreenAngles);
  for ( i = 0; i < 3; ++i ) {
    if ( oType(*poCustomConversions) == ONULL ) {
      theTags(pRasterStyle->oCustomConversions[i]) = OARRAY | EXECUTABLE | UNLIMITED;
      SETGLOBJECTTO(pRasterStyle->oCustomConversions[i], FALSE);
      theLen(pRasterStyle->oCustomConversions[i]) = 0;
      oArray(pRasterStyle->oCustomConversions[i]) = NULL;
    } else {
      HQASSERT(dlpools == NULL, "CustomConversions not copied into dlpool memory");
      Copy(object_slot_notvm(&pRasterStyle->oCustomConversions[i]),
           &oArray(*poCustomConversions)[i]);
    }
  }
  pRasterStyle->colorChannelNames = NULL;
  pRasterStyle->numColorChannels = 0;
  HQASSERT(dlpools == NULL || oType(*posRGB) == OBOOLEAN,
           "sRGB not copied into dlpool memory");
  Copy(object_slot_notvm(&pRasterStyle->osRGB), posRGB);
  pRasterStyle->processColorantNames = NULL;
  pRasterStyle->numProcessColorantNames = 0;
  HQASSERT(dlpools == NULL || oType(*poColorantPresence) == ODICTIONARY,
           "ColorantPresence not copied into dlpool memory");
  Copy(object_slot_notvm(&pRasterStyle->oColorantPresence), poColorantPresence);
  HQASSERT(dlpools == NULL || oType(*poColorantDetails) == ONOTHING,
           "ColorantDetails not copied into dlpool memory");
  Copy(object_slot_notvm(&pRasterStyle->oColorantDetails), poColorantDetails);

  pRasterStyle->processColorModel = processColorModel;
  pRasterStyle->calibrationColorModel = calibrationColorModel;
  pRasterStyle->colorSpace = onull;           /* Struct copy to initialise slot properties */
  pRasterStyle->photoinkInfo = NULL;

  /* now construct the sheets */
  pRasterStyle->pSheets = NULL; /* for now */

  pRasterStyle->pBackgroundFlags = NULL; /* there are no background separations initially */

  pRasterStyle->nBandSize = 1 ;
  pRasterStyle->nMaxOffsetIntoBand = 0 ;

  pRasterStyle->object_map_translation = NULL ;

  pRasterStyle->generation = ++generation ; /* new value */

  pRasterStyle->fCalculatingColors = FALSE ;

  if (poProcessColorant_Black) {
    if (oType(*poProcessColorant_Black) != ONULL) {
      if (! gucs_nameFromObject(poProcessColorant_Black, &pRasterStyle->pBlackName) )
        goto cleanup_setupRasterStyleGeneric;
    }
  }

  /* Unpack oColorChannels in case it is restored away and we still
     reference the rasterstyle because of deferred device deactivation. */
  if (theLen(*poColorChannels) > 0) {
    int32 numColorChannels = theLen(*poColorChannels);

    pRasterStyle->colorChannelNames =
      gucs_alloc(dlpools, numColorChannels * sizeof(NAMECACHE*),
                 MM_ALLOC_CLASS_GUC_RASTERSTYLE);
    if ( pRasterStyle->colorChannelNames == NULL ) {
      (void)error_handler(VMERROR);
      goto cleanup_setupRasterStyleGeneric;
    } else {
      OBJECT* poColorChannel = oArray(*poColorChannels);
      int32 iColorChannel;

      pRasterStyle->numColorChannels = numColorChannels;

      for (iColorChannel = 0; iColorChannel < numColorChannels; ++iColorChannel) {
        if (! gucs_nameFromObject(poColorChannel + iColorChannel,
                    & pRasterStyle->colorChannelNames[iColorChannel]))
          goto cleanup_setupRasterStyleGeneric;
      }
    }
  }

  /* Unpack oProcessColorants in case it is restored away and we still
     reference the rasterstyle because of deferred device deactivation. */
  if (theLen(*poProcessColorants) > 0) {
    int32 numProcessColorantNames = theLen(*poProcessColorants);

    pRasterStyle->processColorantNames =
      gucs_alloc(dlpools,
                 numProcessColorantNames * sizeof(NAMECACHE*),
                 MM_ALLOC_CLASS_GUC_RASTERSTYLE);
    if (pRasterStyle->processColorantNames == NULL) {
      (void)error_handler(VMERROR);
      goto cleanup_setupRasterStyleGeneric;
    } else {
      OBJECT* poColorants = oArray(*poProcessColorants);
      int32 iProcessColorant;

      pRasterStyle->numProcessColorantNames = numProcessColorantNames;

      for (iProcessColorant = 0; iProcessColorant < numProcessColorantNames; ++iProcessColorant) {
        if (! gucs_nameFromObject(poColorants + iProcessColorant,
                 & pRasterStyle->processColorantNames[iProcessColorant]))
          goto cleanup_setupRasterStyleGeneric;
      }
    }
  }

  /* If the PCM is DeviceN, manufacture a DeviceN color space which will be used
     by a few clients. A DeviceN PCM will never be seen for a backdrop, only for
     device raster styles => this color space is only for limited purposes. The
     most important is that gsc_getcolorspacetype() sees it as a DeviceN space.
     The alternate space and tint transform shouldn't be used so they can be
     illegal values to act as asserts. Neither should the colorant list be used,
     but they are populated with the list of process colorants for informational
     purposes. */
  if (processColorModel == DEVICESPACE_N) {
    int iProcessColorant;
    OBJECT *csa;
    uint16 csaSize = 4;
    int numProcessColorantNames = pRasterStyle->numProcessColorantNames;
    OBJECT *alloc;
    int allocSize = (csaSize + numProcessColorantNames) * sizeof(OBJECT);

    alloc = gucs_alloc(dlpools, allocSize, MM_ALLOC_CLASS_GUC_RASTERSTYLE);
    if ( alloc == NULL )
      goto cleanup_setupRasterStyleGeneric;

    theTags(pRasterStyle->colorSpace) = OARRAY | LITERAL | UNLIMITED;
    theLen(pRasterStyle->colorSpace) = csaSize;
    oArray(pRasterStyle->colorSpace) = &alloc[0];

    csa = oArray(pRasterStyle->colorSpace);
    for ( i = 0; i < csaSize; ++i )
      csa[i] = onothing;        /* Struct copy to initialise slot properties */
    object_store_name( &csa[0], NAME_DeviceN, LITERAL );

    /* Populate the colorant list with process colorants for information only */
    oArray(csa[1]) = &alloc[csaSize];
    theTags(csa[1]) = OARRAY | LITERAL | UNLIMITED;
    theLen(csa[1]) = (uint16) numProcessColorantNames ;
    for ( iProcessColorant = 0; iProcessColorant < numProcessColorantNames; ++iProcessColorant ) {
      oArray(csa[1])[iProcessColorant] = onull;  /* Struct copy to initialise slot properties */
      object_store_namecache( &oArray(csa[1])[iProcessColorant],
                              pRasterStyle->processColorantNames[iProcessColorant],
                              LITERAL );
    }

    object_store_name( &csa[2], NAME_DeviceN, LITERAL );  /* Illegal alternate space */
    theTags(csa[3]) = OARRAY | LITERAL | UNLIMITED;
    theLen(csa[3]) = 0 ;
  }

  /* Create the substructure which describes the sheets, channels and colorants
     we want to image */

  if ( !gucs_setupSheetsChannelsAndColorants(pRasterStyle,
                                             poColorantPresence,
                                             poSeparationOrder) )
    goto cleanup_setupRasterStyleGeneric;

  /* anChannelAssignment is for backward compatibility, so that older plugins
     can find out what order simple color spaces are working in. Note that
     this array says what color is in channel 0 and so on, where 0 is Cyan,
     Gray, Red, 1 is Magenta/Green etc, and we set all zeros if anything
     unusual is found. NOTE: this is the opposite way round to the
     colorValues array in page buffers which gives the position of the
     indexed colorant. Note also that this has nothing to do with the
     colorants rendered - this is channels */

  pRasterStyle->anChannelAssignment[0] = pRasterStyle->anChannelAssignment[1] =
    pRasterStyle->anChannelAssignment[2] = pRasterStyle->anChannelAssignment[3] = -1;
  if ( pRasterStyle->numColorChannels <= NUM_CHANNEL_ASSIGNMENT ) {
    for (nChannel = 0; nChannel < pRasterStyle->numColorChannels; nChannel++) {

      NAMECACHE * pnmChannel = pRasterStyle->colorChannelNames[nChannel];

      switch (pnmChannel - system_names) {
      case NAME_Cyan:
      case NAME_Gray:
      case NAME_Red:
        pRasterStyle->anChannelAssignment[nChannel] = 0;
        break;
      case NAME_Magenta:
      case NAME_Green:
        pRasterStyle->anChannelAssignment[nChannel] = 1;
        break;
      case NAME_Yellow:
      case NAME_Blue:
        pRasterStyle->anChannelAssignment[nChannel] = 2;
        break;
      case NAME_Black:
        pRasterStyle->anChannelAssignment[nChannel] = 3;
        break;
      default:
        break;
      }
      if (pRasterStyle->anChannelAssignment[nChannel] == -1)
        break;
    }
  }

  /* Walk over colorants, finding equivalent sRGB colors and marking omit if
     blank separations */

  /* Set ColorantOmit flag for all of Omit's Colorants */
  if ( poOmitColorants ) {
    int32 len = theLen(*poOmitColorants) ;

    while ( --len >= 0 ) {
      COLORANTINDEX cio ;
      NAMECACHE *pName ;

      if ( !gucs_nameFromObject(&oArray(*poOmitColorants)[len], &pName) ) {
        HQFAIL("gucs_nameFromObject failed second time round") ;
        goto cleanup_setupRasterStyleGeneric;
      }

      if (!guc_colorantIndexPossiblyNewName(pRasterStyle, pName, &cio))
        goto cleanup_setupRasterStyleGeneric;
      if ( cio != COLORANTINDEX_UNKNOWN &&
           cio != COLORANTINDEX_NONE &&
           cio != COLORANTINDEX_ALL ) {

        if ( !guc_addColorantToSet(pRasterStyle->dlpools,
                                   &pRasterStyle->pColorantOmits, cio) )
          goto cleanup_setupRasterStyleGeneric;
      }
    }
  }

  for (pSheet = pRasterStyle->pSheets; pSheet != NULL; pSheet = pSheet->pNextSheet) {
    GUCR_CHANNEL *pChannel;

    for (pChannel = pSheet->pChannels; pChannel != NULL; pChannel = pChannel->pNextChannel) {
      GUCR_COLORANT *pColorant;

      for (pColorant = pChannel->pColorants;
           pColorant != NULL;
           pColorant = pColorant->pNextColorant) {
        COLORANTINDEX ci = pColorant->info.colorantIndex ;

        gucr_setColorantColor( pColorant , posRGB ) ;

        if ( ci != COLORANTINDEX_UNKNOWN ) {
          HQASSERT(pColorant->info.colorantType != COLORANTTYPE_UNKNOWN,
                   "Colorant type not initialised correctly") ;

          if ( guc_colorantInSet(pRasterStyle->pColorantOmits, ci) ) {
            if ( !guc_addColorantToSet(pRasterStyle->dlpools,
                                       &pRasterStyle->pCurrentOmits, ci) )
              goto cleanup_setupRasterStyleGeneric;
          } else {
            Bool fOmitIfBlank ;

            if ( pColorant->info.colorantType == COLORANTTYPE_PROCESS ) {
              fOmitIfBlank = fOmitProcess ; /* Process omit */
            } else {
              HQASSERT(pColorant->info.colorantType == COLORANTTYPE_SPOT,
                       "Colorant is not process or spot before extras") ;
              fOmitIfBlank = fOmitSpot ; /* Spot omit */
            }
            if ( fOmitIfBlank ) {
              if ( !guc_addColorantToSet(pRasterStyle->dlpools,
                                         &pRasterStyle->pCurrentOmits, ci) ||
                   !guc_addColorantToSet(pRasterStyle->dlpools,
                                         &pRasterStyle->pColorantOmits, ci))
                goto cleanup_setupRasterStyleGeneric;
            }
          }
        }
      }
    }
  }

  /* Find out if we are dealing with a photoink device. This is needed before
   * we attempt to find the black colorant below.
   */
  if (! guc_findPhotoinkColorantIndices(pRasterStyle, &pRasterStyle->photoinkInfo) ||
      ! gsc_findBlackColorantIndex(pRasterStyle))
    goto cleanup_setupRasterStyleGeneric;

  if ( poObjectTypeMap != NULL ) {
    if ( oType(*poObjectTypeMap) == OARRAY ||
         oType(*poObjectTypeMap) == OPACKEDARRAY ) {
      if ( theLen(*poObjectTypeMap) == 256 ) {
        size_t i;
        int32 limit = nValuesPerComponent;
        /** \todo For RLE, limit is 256. Proper type channel will fix this. */
        channel_output_t *lookup = gucs_alloc(dlpools,
                                              256 * sizeof(channel_output_t),
                                              MM_ALLOC_CLASS_GUC_RASTERSTYLE);

        if ( lookup == NULL ) {
          (void)error_handler(VMERROR);
          goto cleanup_setupRasterStyleGeneric;
        }

        pRasterStyle->object_map_translation = lookup ;

        for ( i = 0, poObjectTypeMap = oArray(*poObjectTypeMap) ;
              i < 256 ;
              ++i, ++poObjectTypeMap, ++lookup ) {
          int32 element ;

          if ( oType(*poObjectTypeMap) != OINTEGER ) {
            (void)detail_error_handler(TYPECHECK,
                                       "ObjectTypeMap elements should be integers.");
            goto cleanup_setupRasterStyleGeneric;
          }

          element = oInteger(*poObjectTypeMap) ;
          if ( element < 0 || element >= limit ) {
            (void)detail_error_handler(RANGECHECK,
                                       "ObjectTypeMap element exceeds output range.");
            goto cleanup_setupRasterStyleGeneric;
          }

          *lookup = CAST_TO_UINT16(element) ;
        }
      } else {
        (void)detail_error_handler(RANGECHECK,
                                   "ObjectTypeMap array should have 256 elements.");
        goto cleanup_setupRasterStyleGeneric;
      }
    } else if ( oType(*poObjectTypeMap) != ONULL ) {
      (void)detail_error_handler(TYPECHECK,
                                 "ObjectTypeMap should be an array or null.");
      goto cleanup_setupRasterStyleGeneric;
    }
  }

  /* If we got here without skipping to the cleanup, all is good. */
  fResult = TRUE;

  /* Link rasterstyle into GC chain if not allocated from dlpool memory */
  if ( dlpools == NULL ) {
    pRasterStyle->next = rasterstyle_gc_list ;
    rasterstyle_gc_list = pRasterStyle ;
  }

  /* give the handle back to the caller */
  * ppRasterStyle = pRasterStyle;

 cleanup_setupRasterStyleGeneric:
  if (! fResult)
    guc_discardRasterStyle(&pRasterStyle);

#undef return
  return fResult;
}

/* ---------------------------------------------------------------------- */
/** Build the list of sheets, separations, color
   channels and colorants according to the various parameters that
   control it. The objects should all have been typechecked at the top
   level, though elements of arrays are checked here. */
Bool guc_setupRasterStyle(
  Bool screening,
  OBJECT * poProcessColorModel,
  OBJECT * poCalibrationColorModel,
  OBJECT * poInterleavingStyle,
  OBJECT * poValuesPerComponent,
  OBJECT * poSeparationStyle,
  OBJECT * poNumProcessColorants,
  OBJECT * poSeparationDetails,
  OBJECT * poSeparationOrder,
  OBJECT * poColorChannels,
  OBJECT * poFullyFledgedColorants,
  OBJECT * poReservedColorants,
  OBJECT * poDefaultScreenAngles,
  OBJECT * poCustomConversions,
  OBJECT * posRGB,
  OBJECT * poProcessColorants,
  OBJECT * poColorantPresence,
  OBJECT * poProcessColorant_Black,
  OBJECT * poColorantDetails,
  OBJECT * poObjectTypeMap,
  GUCR_RASTERSTYLE **ppRasterStyle)
{
  Bool fInheritSpotColors = FALSE; /* Value not important - no parent RS */

  return guc_setupRasterStyleGeneric(FALSE, /* Not a backdrop raster style. */
                                     screening,
                                     NULL, /* Use temp pool for allocations */
                                     poProcessColorModel,
                                     poCalibrationColorModel,
                                     poInterleavingStyle,
                                     poValuesPerComponent,
                                     poSeparationStyle,
                                     poNumProcessColorants,
                                     poSeparationDetails,
                                     poSeparationOrder,
                                     poColorChannels,
                                     poFullyFledgedColorants,
                                     poReservedColorants,
                                     poDefaultScreenAngles,
                                     poCustomConversions,
                                     posRGB,
                                     poProcessColorants,
                                     poColorantPresence,
                                     poProcessColorant_Black,
                                     poColorantDetails,
                                     poObjectTypeMap,
                                     fInheritSpotColors,
                                     ppRasterStyle);
}

static Bool gucs_copyNames(mm_pool_t *dlpools,
                           NAMECACHE **namesSrc, int32 numSrc,
                           NAMECACHE ***namesDst, int32 *numDst)
{
  int32 i;
  *namesDst = gucs_alloc(dlpools, numSrc * sizeof(NAMECACHE*),
                          MM_ALLOC_CLASS_GUC_RASTERSTYLE);
  if ( *namesDst == NULL )
    return error_handler(VMERROR);
  *numDst = numSrc;
  for ( i = 0; i < numSrc; ++i ) {
    (*namesDst)[i] = namesSrc[i];
  }
  return TRUE;
}

uint32 guc_rasterstyleId(const GUCR_RASTERSTYLE *pRasterStyle)
{
  return pRasterStyle->id;
}

/* ---------------------------------------------------------------------- */
/** Scan a rasterstyle for garbage collection. */
mps_res_t gucr_rasterstyle_scan(mps_ss_t ss, GUCR_RASTERSTYLE *pRasterStyle)
{
  mps_res_t res = MPS_RES_OK ;

  while ( pRasterStyle != NULL ) {
    /* The list of rasterstyles is traversed in the caller. */
    HQASSERT(pRasterStyle->dlpools == NULL,
             "A rasterstyle allocated from dlpool memory does not need gc scanning");
    res = ps_scan_field( ss, &pRasterStyle->oFullyFledgedColorants);
    if ( res != MPS_RES_OK ) return res;
    res = ps_scan_field( ss, &pRasterStyle->oReservedColorants);
    if ( res != MPS_RES_OK ) return res;
    res = ps_scan_field( ss, &pRasterStyle->oDefaultScreenAngles);
    if ( res != MPS_RES_OK ) return res;
    res = ps_scan_field( ss, &pRasterStyle->oCustomConversions[0]);
    if ( res != MPS_RES_OK ) return res;
    res = ps_scan_field( ss, &pRasterStyle->oCustomConversions[1]);
    if ( res != MPS_RES_OK ) return res;
    res = ps_scan_field( ss, &pRasterStyle->oCustomConversions[2]);
    if ( res != MPS_RES_OK ) return res;
    res = ps_scan_field( ss, &pRasterStyle->osRGB);
    if ( res != MPS_RES_OK ) return res;
    res = ps_scan_field( ss, &pRasterStyle->oColorantPresence);
    if ( res != MPS_RES_OK ) return res;
    res = ps_scan_field( ss, &pRasterStyle->oColorantDetails);
    if ( res != MPS_RES_OK ) return res;
    /* pOwningRasterStyle in the sheets is scanned through the rasterstyle list. */
    /* Colorant names are reachable through the pagedevice dictionary. */

    /* Scan all accessible (stacked or backdrop chain) rasterstyles, since
       group rasterstyles will not be reachable directly through gstate
       pointer. */
    pRasterStyle = pRasterStyle->next ;
  }

  return res;
}

/** Scan list of rasterstyles not attached to any particular gstate. This
    catches backdrop rasterstyles in softmask and isolated groups. */
static mps_res_t MPS_CALL gucr_backdrop_scan(mps_ss_t ss, void *p, size_t s)
{
  UNUSED_PARAM( void *, p );
  UNUSED_PARAM( size_t, s );

  return gucr_rasterstyle_scan(ss, rasterstyle_gc_list);
}

/* ---------------------------------------------------------------------- */
/** Set up a minimal implementation of single channel
   gray output, independent of any setpagedevice, so there is enough
   around to do things like setscreen before we encounter any
   setpagedevice. Unfortunately the operator lookup table isnt
   available when this is called during startup, so we can't do this
   by executing a string.  */
Bool guc_init(GUCR_RASTERSTYLE **ppRasterStyle)
{
  OBJECT
    oProcessColorModel = OBJECT_NOTVM_NAME(NAME_DeviceGray, LITERAL),
    oCalibrationColorModel = OBJECT_NOTVM_NULL,
    oInterleavingStyle = OBJECT_NOTVM_INTEGER(GUCR_INTERLEAVINGSTYLE_MONO),
    oValuesPerComponent = OBJECT_NOTVM_INTEGER(2),
    oSeparationStyle = OBJECT_NOTVM_INTEGER(GUCR_SEPARATIONSTYLE_MONOCHROME),
    oNumProcessColorants = OBJECT_NOTVM_INTEGER(1),
    oSeparationDetails = OBJECT_NOTVM_NULL,
    oSeparationOrder = OBJECT_NOTVM_NOTHING, /* initialised later */
    oColorChannels = OBJECT_NOTVM_NOTHING, /* initialised later */
    oFullyFledgedColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oReservedColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oDefaultScreenAngles = OBJECT_NOTVM_NULL,
    oCustomConversions = OBJECT_NOTVM_NULL,
    osRGB = OBJECT_NOTVM_NOTHING, /* initialised later */
    oProcessColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oColorantPresence = OBJECT_NOTVM_NOTHING, /* initialised later */
    oProcessColorant_Black = OBJECT_NOTVM_NULL,
    oColorantDetails = OBJECT_NOTVM_NOTHING,
    oObjectTypeMap = OBJECT_NOTVM_NULL,
    tmpo = OBJECT_NOTVM_NOTHING, /* initialised later */
    oCi = OBJECT_NOTVM_NOTHING, /* initialised later */
    oColorantPresenceValue = OBJECT_NOTVM_NOTHING /* initialised later */
    ;

  *ppRasterStyle = NULL;

  /* SeparationOrder: [/Gray] */
  if (!ps_array(&oSeparationOrder, 1) )
    return FALSE;
  object_store_name(&oArray(oSeparationOrder)[0], NAME_Gray, LITERAL);

  /* ColorChannels: also [/Gray] */
  Copy(& oColorChannels, & oSeparationOrder);

  /* FullyFledgedColorants: << /Gray 0 >> */
  object_store_integer(&oCi, 0) ;
  if ( !ps_dictionary(&oFullyFledgedColorants, 1) ||
       !fast_insert_hash_name(&oFullyFledgedColorants, NAME_Gray, &oCi) )
    return FALSE ;

  /* oReservedColorants: <<>> */
  if ( !ps_dictionary(&oReservedColorants, 0) )
    return FALSE ;

  /* sRGB: << /Gray [0.0 0.0 0.0] >> */
  if ( !ps_dictionary(&osRGB, 1) ||
       !ps_array(&tmpo, 3) ||
       !fast_insert_hash_name(&osRGB, NAME_Gray, &tmpo) )
    return FALSE ;
  object_store_real(&oArray(tmpo)[0], 0.0f);
  object_store_real(&oArray(tmpo)[1], 0.0f);
  object_store_real(&oArray(tmpo)[2], 0.0f);

  /* ProcessColorants: also [ /Gray ] */
  Copy(& oProcessColorants, & oSeparationOrder);

  /* ColorantPresence: << /Gray 2 >> */
  object_store_integer(&oColorantPresenceValue, COLORANT_MUSTBEPRESENT) ;
  if ( !ps_dictionary(&oColorantPresence, 1) ||
       !fast_insert_hash_name(&oColorantPresence, NAME_Gray, &oColorantPresenceValue) )
    return FALSE ;

  /* If there are any new parameters to guc_setupRasterStyle, they'll
     need to have default values added to aszSetupObjects, and
     nSetupObjects will need incrementing */

  if ( !guc_setupRasterStyle(TRUE, /* screening */
                             & oProcessColorModel,
                             & oCalibrationColorModel,
                             & oInterleavingStyle,
                             & oValuesPerComponent,
                             & oSeparationStyle,
                             & oNumProcessColorants,
                             & oSeparationDetails,
                             & oSeparationOrder,
                             & oColorChannels,
                             & oFullyFledgedColorants,
                             & oReservedColorants,
                             & oDefaultScreenAngles,
                             & oCustomConversions,
                             & osRGB,
                             & oProcessColorants,
                             & oColorantPresence,
                             & oProcessColorant_Black,
                             & oColorantDetails,
                             & oObjectTypeMap,
                             ppRasterStyle) )
    return FALSE ;

  /* Create the MPS root last, so we know we'll clean it up on success. */
  if ( mps_root_create( &backdrop_gc_root, mm_arena,
                        mps_rank_exact(), 0,
                        gucr_backdrop_scan, NULL, 0 ) != MPS_RES_OK ) {
    HQFAIL("Rasterstyle GC root creation failed.");
    return FAILURE(FALSE) ;
  }

  return TRUE ;
}

void guc_finish(void)
{
  rasterstyle_gc_list = NULL;
  if (backdrop_gc_root != NULL) {
    mps_root_destroy(backdrop_gc_root);
  }
}

/* ---------------------------------------------------------------------- */
#if defined( DEBUG_BUILD )
#include "groupPrivate.h" /* backdrop_render_debug */
#include "swcopyf.h"
#endif

#if defined( DEBUG_BUILD )
static COLORANTINDEX ciNext_one = 10; /* Allow 10 colorants in the device RS */
#endif

/** Create a special raster style for transparency group blend spaces and
   'virtual devices' for colormetric overprinting of arbitrary spot colors.
   The raster style is used as part of rendering objects to a backdrop
   object (similar to a linework object). The backdrop object may eventually
   be rendered (after being color converted) using the normal real output
   device raster style.

   The backdrop rasterstyle is immediately attached to the list of backdrop
   rasterstyles. If it is pushed on the gstate stack, it will be removed from
   this list and put on the gstate stack. When popped from the gstate stack,
   it will be returned to this list so it is reachable for GC scanning. */
Bool guc_setupBackdropRasterStyle(
  /*@notnull@*/ /*@out@*/         GUCR_RASTERSTYLE** ppRasterStyle,
  /*@notnull@*/ /*@in@*/          GUCR_RASTERSTYLE *parentRS,
  /*@notnull@*/ /*@in@*/          GS_COLORinfo *colorInfo,
                                  OBJECT *colorSpace,
                                  Bool fVirtualDevice,
                                  Bool fAutoSeparations,
                                  Bool fInheritSeparations)
{
  OBJECT
    oProcessColorModel = OBJECT_NOTVM_NOTHING, /* initialised later */
    oCalibrationColorModel = OBJECT_NOTVM_NULL,
    oInterleavingStyle = OBJECT_NOTVM_INTEGER(GUCR_INTERLEAVINGSTYLE_PIXEL),
    oValuesPerComponent = OBJECT_NOTVM_INTEGER(COLORVALUE_MAX + 1),
    oSeparationStyle = OBJECT_NOTVM_INTEGER(GUCR_SEPARATIONSTYLE_COMPOSITE),
    oNumProcessColorants = OBJECT_NOTVM_INTEGER(1),
    oSeparationDetails = OBJECT_NOTVM_NULL,
    oSeparationOrder = OBJECT_NOTVM_NOTHING, /* initialised later */
    oColorChannels = OBJECT_NOTVM_NOTHING, /* initialised later */
    oFullyFledgedColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oReservedColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oDefaultScreenAngles = OBJECT_NOTVM_NULL,
    oCustomConversions = OBJECT_NOTVM_NULL,
    osRGB = OBJECT_NOTVM_BOOLEAN(FALSE),
    oProcessColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oColorantPresence = OBJECT_NOTVM_NOTHING, /* initialised later */
    oProcessColorant_Black = OBJECT_NOTVM_NULL,
    oColorantDetails = OBJECT_NOTVM_NOTHING,
    oObjectTypeMap = OBJECT_NOTVM_NULL;
  struct DICT_ALLOC_PARAMS alloc_params;
  struct DICT_ALLOCATOR dict_alloc;
  corecontext_t *context = get_core_context_interp();
  mm_pool_t *dlpools = context->page->dlpools;
  uint8 *pstrSeparationOrder, *pstrColorantPresence;
  int32 PCMnamenumber, PCMdimension;
  Bool glmode;
  COLORANTINDEX ciBlack = COLORANTINDEX_NONE;
  Bool result = FALSE ;
  REPRO_COLOR_MODEL colorModel;
  COLORANTINDEX ciNext = 0;
#define DICT_LEN (10)

  if ( !gsc_colorModel(colorInfo, colorSpace, &colorModel) )
    return FALSE;

  /* Ensure subsequent allocs happen with global memory,
     backdrop raster styles need to stay around for rendering. Must cleanup
     properly by resetting allocation mode to local after this point. */
  glmode = setglallocmode(context, TRUE);

#define return USE_goto_tidyup

  alloc_params.alloc_pool = dlpools;
  alloc_params.alloc_class = MM_ALLOC_CLASS_GUC_RASTERSTYLE;
  dict_alloc.alloc_mem = gucs_dict_allocator;
  dict_alloc.data = (uintptr_t)&alloc_params;

  /* FullyFledgedColorants */
  if (!dict_create(&oFullyFledgedColorants, &dict_alloc, DICT_LEN, ISNOTVMDICTMARK(SAVEMASK))) {
    goto tidyup;
  }

  /* ReservedColorants */
  if (!dict_create(&oReservedColorants, &dict_alloc, DICT_LEN, ISNOTVMDICTMARK(SAVEMASK))) {
    goto tidyup;
  }

  switch ( colorModel ) {
  default: HQFAIL("Invalid colorModel");
  case REPRO_COLOR_MODEL_GRAY:
#if defined( DEBUG_BUILD )
    if ( (backdrop_render_debug & BR_DEBUG_COLORANTS) != 0 )
      ciNext = ciNext_one, ++ciNext_one;
#endif

    if ( !gucs_addColorantToDictionary(dlpools, &system_names[NAME_Gray],
                                       ciNext, &oFullyFledgedColorants) )
      goto tidyup;

    pstrSeparationOrder  = (uint8*)"[/Gray]";
    pstrColorantPresence = (uint8*)"<</Gray 2>>";
    PCMnamenumber        = NAME_DeviceGray;
    PCMdimension         = 1;
    ciBlack              = ciNext;
    break;

  case REPRO_COLOR_MODEL_RGB:
#if defined( DEBUG_BUILD )
    if ( (backdrop_render_debug & BR_DEBUG_COLORANTS) != 0 )
      ciNext = ciNext_one, ciNext_one += 3;
#endif

    if ( !gucs_addColorantToDictionary(dlpools, &system_names[NAME_Red],
                                       ciNext, &oFullyFledgedColorants) ||
         !gucs_addColorantToDictionary(dlpools, &system_names[NAME_Green],
                                       ciNext+1, &oFullyFledgedColorants) ||
         !gucs_addColorantToDictionary(dlpools, &system_names[NAME_Blue],
                                       ciNext+2, &oFullyFledgedColorants) )
      goto tidyup;

    pstrSeparationOrder  = (uint8*)"[/Red /Green /Blue]";
    pstrColorantPresence = (uint8*)"<</Red 2 /Green 2 /Blue 2>>";
    PCMnamenumber        = NAME_DeviceRGB;
    PCMdimension         = 3;
    ciBlack              = COLORANTINDEX_ALL ;
    break;

  case REPRO_COLOR_MODEL_CMYK:
#if defined( DEBUG_BUILD )
    if ( (backdrop_render_debug & BR_DEBUG_COLORANTS) != 0 )
      ciNext = ciNext_one, ciNext_one += 4;
#endif

    if ( !gucs_addColorantToDictionary(dlpools, &system_names[NAME_Cyan],
                                       ciNext, &oFullyFledgedColorants) ||
         !gucs_addColorantToDictionary(dlpools, &system_names[NAME_Magenta],
                                       ciNext+1, &oFullyFledgedColorants) ||
         !gucs_addColorantToDictionary(dlpools, &system_names[NAME_Yellow],
                                       ciNext+2, &oFullyFledgedColorants) ||
         !gucs_addColorantToDictionary(dlpools, &system_names[NAME_Black],
                                       ciNext+3, &oFullyFledgedColorants) )
      goto tidyup;

    pstrSeparationOrder  = (uint8*)"[/Cyan /Magenta /Yellow /Black]";
    pstrColorantPresence = (uint8*)"<</Cyan 2 /Magenta 2 /Yellow 2 /Black 2>>";
    PCMnamenumber        = NAME_DeviceCMYK;
    PCMdimension         = 4;
    ciBlack              = ciNext + 3;
    break;
  }

  /* ProcessColorModel */
  object_store_name(&oProcessColorModel, PCMnamenumber, LITERAL);

  /* ValuesPerComponent */
  HQASSERT(sizeof(COLORVALUE) == 2,
           "COLORVALUE changed, values per component no longer valid");

  /* NumProcessColorants */
  object_store_integer(&oNumProcessColorants, PCMdimension);

  /* SeparationDetails; not stored in the rasterstyle so use PSVM. */
  if (fAutoSeparations) {
    if ( !run_ps_string((uint8*)"<< /Add true /Remove true >>") )
      goto tidyup;
    Copy(& oSeparationDetails, theTop(operandstack));
    pop(& operandstack);
  }

  /* SeparationOrder; not stored in the rasterstyle so use PSVM. */
  if (! run_ps_string(pstrSeparationOrder))
    goto tidyup;
  Copy(& oSeparationOrder, theTop(operandstack));
  pop(& operandstack);

  /* ColorChannels; not stored in the rasterstyle so use PSVM. */
  Copy(& oColorChannels, & oSeparationOrder);

  /* ProcessColorants; not stored in the rasterstyle so use PSVM. */
  Copy(& oProcessColorants, & oSeparationOrder);

  /* ColorantPresence */
  if (! run_ps_string(pstrColorantPresence))
    goto tidyup;
  HQASSERT(COLORANT_MUSTBEPRESENT == 2, "ColorantPresence string is wrong");
  Copy(& oColorantPresence, theTop(operandstack));
  pop(& operandstack);

  if (! guc_setupRasterStyleGeneric(TRUE, /* A backdrop raster style. */
                                    FALSE, /* Not screening */
                                    dlpools, /* Use dl pool */
                                    & oProcessColorModel,
                                    & oCalibrationColorModel,
                                    & oInterleavingStyle,
                                    & oValuesPerComponent,
                                    & oSeparationStyle,
                                    & oNumProcessColorants,
                                    & oSeparationDetails,
                                    & oSeparationOrder,
                                    & oColorChannels,
                                    & oFullyFledgedColorants,
                                    & oReservedColorants,
                                    & oDefaultScreenAngles,
                                    & oCustomConversions,
                                    & osRGB,
                                    & oProcessColorants,
                                    & oColorantPresence,
                                    & oProcessColorant_Black,
                                    & oColorantDetails,
                                    & oObjectTypeMap,
                                    fInheritSeparations,
                                    ppRasterStyle))
    goto tidyup;

#if defined( DEBUG_BUILD )
  if ( (backdrop_render_debug & BR_DEBUG_COLORANTS) != 0 ) {
    (*ppRasterStyle)->ciMax = ciNext_one - 1;
    /* Allow allow up to 10 unique spots colorants (after this colorants may
       just stop being unique between backdrop rasterstyles). */
    ciNext_one += 10;
  }
#endif

  /* Successfully built a raster style, override any pertinent values */
  if (fVirtualDevice)
    (*ppRasterStyle)->flags |= (RASTERSTYLE_FLAG_VIRTUALDEVICE);
  (*ppRasterStyle)->ciBlack = ciBlack;

  /* The colorSpace is only appropriate for backdrop raster styles */
  Copy(&(*ppRasterStyle)->colorSpace, colorSpace);

  if ( parentRS ) {
    (*ppRasterStyle)->parent = parentRS ;
    guc_reserveRasterStyle(parentRS) ;

    /* Note which colorants exist on the device rasterstyle, at the end
       of the rasterstyle parent chain. */
    if ( !guc_updateEquivalentColorants(*ppRasterStyle, COLORANTINDEX_ALL) ) {
      guc_discardRasterStyle(ppRasterStyle) ;
      goto tidyup;
    }
  }

  result = TRUE ;

 tidyup:
  if ( oType(oFullyFledgedColorants) == ODICTIONARY )
    gucs_freeWithHeader(dlpools, oDict(oFullyFledgedColorants) - 2,
                        MM_ALLOC_CLASS_GUC_RASTERSTYLE);
  if ( oType(oReservedColorants) == ODICTIONARY )
    gucs_freeWithHeader(dlpools, oDict(oReservedColorants) - 2,
                        MM_ALLOC_CLASS_GUC_RASTERSTYLE);

  setglallocmode(context, glmode);

#undef return
  return result;
}


#if defined( DEBUG_BUILD )
static COLORANTINDEX ciNext_two = 10; /* Allow 10 colorants in the device RS */
#endif

Bool guc_setup_image_filter_RasterStyle(
  /*@notnull@*/ /*@out@*/         GUCR_RASTERSTYLE** ppRasterStyle,
                                  COLORSPACE_ID processSpace,
                                  int32   valuesPerComponent)
{
  OBJECT
    oProcessColorModel = OBJECT_NOTVM_NOTHING, /* initialised later */
    oCalibrationColorModel = OBJECT_NOTVM_NULL,
    oInterleavingStyle = OBJECT_NOTVM_INTEGER(GUCR_INTERLEAVINGSTYLE_BAND),
    oValuesPerComponent = OBJECT_NOTVM_NOTHING, /* initialised later */
    oSeparationStyle = OBJECT_NOTVM_INTEGER(GUCR_SEPARATIONSTYLE_COMPOSITE),
    oNumProcessColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oSeparationDetails = OBJECT_NOTVM_NULL,
    oSeparationOrder = OBJECT_NOTVM_NOTHING, /* initialised later */
    oColorChannels = OBJECT_NOTVM_NOTHING, /* initialised later */
    oFullyFledgedColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oReservedColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oDefaultScreenAngles = OBJECT_NOTVM_NULL,
    oCustomConversions = OBJECT_NOTVM_NULL,
    osRGB = OBJECT_NOTVM_BOOLEAN(FALSE),
    oProcessColorants = OBJECT_NOTVM_NOTHING, /* initialised later */
    oColorantPresence = OBJECT_NOTVM_NOTHING, /* initialised later */
    oProcessColorant_Black = OBJECT_NOTVM_NULL,
    oColorantDetails = OBJECT_NOTVM_NOTHING, /* initialised later */
    oObjectTypeMap = OBJECT_NOTVM_NULL
    ;

  corecontext_t *context = get_core_context_interp();

  uint8
    * pstrSeparationOrder,
    * pstrFullyFledged,
    * pstrColorantPresence;

  int32 PCMnamenumber, PCMdimension;
  Bool glmode;
  COLORANTINDEX ciBlack = COLORANTINDEX_NONE;
  Bool result = FALSE ;
  Bool fInheritSpotColors = TRUE; /* Inherit spots from parent RS */

#if defined( DEBUG_BUILD )
  uint8 fullyFledgedBuf[1024];
#endif

  switch (processSpace) {
  case SPACE_CalGray:
    HQFAIL("CalGray blend spaces NYI, will interpret simply as gray");
    /* fallthru */
  case SPACE_DeviceGray:
    pstrSeparationOrder  = (uint8*)"[/Gray]";
    pstrFullyFledged     = (uint8*)"<</Gray 0>>";
#if defined( DEBUG_BUILD )
    if ( (backdrop_render_debug & BR_DEBUG_COLORANTS) != 0 ) {
      swcopyf((uint8*)fullyFledgedBuf, (uint8*)"<</Gray %d>>",
              ciNext_two);
      ciNext_two += 1;
      pstrFullyFledged     = fullyFledgedBuf;
    }
#endif
    pstrColorantPresence = (uint8*)"<</Gray 2>>";
    PCMnamenumber        = NAME_DeviceGray;
    PCMdimension         = 1;
    ciBlack              = 0;
    break;
  case SPACE_CalRGB:
    HQFAIL("CalRGB blend spaces NYI, will interpret simply as RGB");
    /* fallthru */
  case SPACE_DeviceRGB:
    pstrSeparationOrder  = (uint8*)"[/Red /Green /Blue]";
    pstrFullyFledged     = (uint8*)"<</Red 0 /Green 1 /Blue 2>>";
#if defined( DEBUG_BUILD )
    if ( (backdrop_render_debug & BR_DEBUG_COLORANTS) != 0 ) {
      swcopyf((uint8*)fullyFledgedBuf, (uint8*)"<</Red %d /Green %d /Blue %d>>",
              ciNext_two, ciNext_two+1, ciNext_two+2);
      ciNext_two += 3;
      pstrFullyFledged     = fullyFledgedBuf;
    }
#endif
    pstrColorantPresence = (uint8*)"<</Red 2 /Green 2 /Blue 2>>";
    PCMnamenumber        = NAME_DeviceRGB;
    PCMdimension         = 3;
    break;
  case SPACE_DeviceCMYK:
    pstrSeparationOrder  = (uint8*)"[/Cyan /Magenta /Yellow /Black]";
    pstrFullyFledged     = (uint8*)"<</Cyan 0 /Magenta 1 /Yellow 2 /Black 3>>";
#if defined( DEBUG_BUILD )
    if ( (backdrop_render_debug & BR_DEBUG_COLORANTS) != 0 ) {
      swcopyf((uint8*)fullyFledgedBuf, (uint8*)"<</Cyan %d /Magenta %d /Yellow %d /Black %d>>",
              ciNext_two, ciNext_two+1, ciNext_two+2, ciNext_two+3);
      ciNext_two += 4;
      pstrFullyFledged     = fullyFledgedBuf;
    }
#endif
    pstrColorantPresence = (uint8*)"<</Cyan 2 /Magenta 2 /Yellow 2 /Black 2>>";
    PCMnamenumber        = NAME_DeviceCMYK;
    PCMdimension         = 4;
    ciBlack              = 3;
    break;
  default:
    /** \todo @@@ TODO FIXME other spaces */
    HQFAIL("Invalid processSpace");
    return error_handler(RANGECHECK);
  }

  /* Ensure subsequent allocs happen with global memory,
     backdrop raster styles need to stay around for rendering. Must cleanup
     properly by resetting allocation mode to local after this point. */
  glmode = setglallocmode(context, TRUE);

  /* ProcessColorModel */
  object_store_name(&oProcessColorModel, PCMnamenumber, LITERAL);

  /* ValuesPerComponent */
  object_store_integer(&oValuesPerComponent, valuesPerComponent);

  /* NumProcessColorants */
  object_store_integer(&oNumProcessColorants, PCMdimension);

  /* SeparationOrder */
  if (! run_ps_string(pstrSeparationOrder))
    goto tidyup;
  Copy(& oSeparationOrder, theTop(operandstack));
  pop(& operandstack);

  /* ColorChannels */
  Copy(& oColorChannels, & oSeparationOrder);

  /* FullyFledgedColorants */
  if (! run_ps_string(pstrFullyFledged))
    goto tidyup;
  Copy(& oFullyFledgedColorants, theTop(operandstack));
  pop(& operandstack);

  /* ReservedColorants */
  if ( !ps_dictionary(&oReservedColorants, 0) )
    return FALSE ;

  /* ProcessColorants */
  Copy(& oProcessColorants, & oSeparationOrder);

  /* ColorantPresence */
  if (! run_ps_string(pstrColorantPresence))
    goto tidyup;
  HQASSERT(COLORANT_MUSTBEPRESENT == 2, "ColorantPresence string is wrong");
  Copy(& oColorantPresence, theTop(operandstack));
  pop(& operandstack);

  /* ColorantDetails */
  if ( !ps_dictionary(&oColorantDetails, 0) )
    return FALSE ;

  if (! guc_setupRasterStyleGeneric(FALSE, /* Not a backdrop style */
                                    FALSE, /* Not screening */
                                    NULL, /* Use temp pool for allocations */
                                    & oProcessColorModel,
                                    & oCalibrationColorModel,
                                    & oInterleavingStyle,
                                    & oValuesPerComponent,
                                    & oSeparationStyle,
                                    & oNumProcessColorants,
                                    & oSeparationDetails,
                                    & oSeparationOrder,
                                    & oColorChannels,
                                    & oFullyFledgedColorants,
                                    & oReservedColorants,
                                    & oDefaultScreenAngles,
                                    & oCustomConversions,
                                    & osRGB,
                                    & oProcessColorants,
                                    & oColorantPresence,
                                    & oProcessColorant_Black,
                                    & oColorantDetails,
                                    & oObjectTypeMap,
                                    fInheritSpotColors,
                                    ppRasterStyle))
    goto tidyup;


  /* Successfully built a raster style, override any pertinent values */
  (*ppRasterStyle)->ciBlack = ciBlack;

  result = TRUE ;

 tidyup:
  setglallocmode(context, glmode);

  return result;
}


/* ---------------------------------------------------------------------- */
/**
 * Rasterstyles are copied to allow pipelining of interpreting and rendering.
 * It is not quite a complete copy as not everything needs copying over for the
 * backend.  The copied rasterstyle is allocated from the dl pool memory and no
 * PSVM objects should remain.
 */
Bool guc_copyRasterStyle(mm_pool_t *dlpools, GUCR_RASTERSTYLE *rsSrc,
                         GUCR_RASTERSTYLE **rsDst)
{
  GUCR_RASTERSTYLE *rsCopy;
  COLORANTMAP *cmap;
  size_t i;

  VERIFY_OBJECT(rsSrc, RASTERSTYLE_NAME);

  if ( next_rasterstyle_id == MAXUINT32 ) {
    HQFAIL("Run out of rasterstyle ids");
    return error_handler(LIMITCHECK);
  }

  rsCopy = gucs_alloc(dlpools, sizeof(GUCR_RASTERSTYLE),
                      MM_ALLOC_CLASS_GUC_RASTERSTYLE);
  if ( rsCopy == NULL )
    return error_handler(VMERROR);

  /* No returns until the basic init/copy is complete. */
  HqMemZero(rsCopy, sizeof(GUCR_RASTERSTYLE));
  rsCopy->id = next_rasterstyle_id++;
  rsCopy->flags = rsSrc->flags;
  rsCopy->nReferenceCount = 1;
  rsCopy->next = NULL; /* copy doesn't need adding to rasterstyle_gc_list */
  HQASSERT(rsSrc->parent == NULL, "Parent rasterstyle ptr not set in copy");
  rsCopy->parent = NULL;
  rsCopy->dlpools = dlpools;
  rsCopy->nInterleavingStyle = rsSrc->nInterleavingStyle;
  rsCopy->nValuesPerComponent = rsSrc->nValuesPerComponent;
  rsCopy->nSeparationStyle = rsSrc->nSeparationStyle;
  Copy(object_slot_notvm(&rsCopy->colorSpace), &rsSrc->colorSpace);
  rsCopy->nProcessColorants = rsSrc->nProcessColorants;
  rsCopy->equivalentRealColorants = NULL;
  rsCopy->ciMax = rsSrc->ciMax;
  rsCopy->ciBlack = rsSrc->ciBlack;
  rsCopy->cmap = NULL;
  rsCopy->oFullyFledgedColorants =
    rsCopy->oReservedColorants = onothing;
    rsCopy->oDefaultScreenAngles = onothing;
  rsCopy->oDefaultScreenAngles = rsSrc->oDefaultScreenAngles;
  /** \todo Ensure CustomConversions copy is safe for pipelining */
  rsCopy->oCustomConversions[0] = rsSrc->oCustomConversions[0];
  rsCopy->oCustomConversions[1] = rsSrc->oCustomConversions[1];
  rsCopy->oCustomConversions[2] = rsSrc->oCustomConversions[2];
  rsCopy->colorChannelNames = NULL;
  rsCopy->numColorChannels = 0;
  rsCopy->osRGB = fnewobj; /* default value, possibly overridden later */
  rsCopy->processColorantNames = NULL;
  rsCopy->pBlackName = rsSrc->pBlackName;
  rsCopy->numProcessColorantNames = 0;
  rsCopy->oColorantPresence = onothing;
  rsCopy->oColorantDetails = onothing;
  rsCopy->processColorModel = rsSrc->processColorModel;
  rsCopy->calibrationColorModel = rsSrc->calibrationColorModel;
  rsCopy->photoinkInfo = NULL;
  for ( i = 0; i < NUM_CHANNEL_ASSIGNMENT; ++i )
    rsCopy->anChannelAssignment[i] = rsSrc->anChannelAssignment[i];
  rsCopy->pSheets = NULL;
  rsCopy->pBackgroundFlags = NULL;
  rsCopy->nBandSize = rsSrc->nBandSize;
  rsCopy->nMaxOffsetIntoBand = rsSrc->nMaxOffsetIntoBand;
  rsCopy->pColorantOmits = NULL;
  rsCopy->pCurrentOmits = NULL;
  rsCopy->omitDetails = rsSrc->omitDetails;
  rsCopy->fInheritSpotColors = rsSrc->fInheritSpotColors;
  rsCopy->fAbortSeparations = rsSrc->fAbortSeparations;
  rsCopy->fAddAllSpotColors = rsSrc->fAddAllSpotColors;
  rsCopy->fRemoveAllSpotColors = rsSrc->fRemoveAllSpotColors;
  rsCopy->fOmitMonochrome = rsSrc->fOmitMonochrome;
  rsCopy->fOmitSeparations = rsSrc->fOmitSeparations;
  rsCopy->fHaveEquivalent = rsSrc->fHaveEquivalent;
  rsCopy->fOmitExtraSpot = rsSrc->fOmitExtraSpot;
  rsCopy->fOmitProcess = rsSrc->fOmitProcess;
  rsCopy->object_map_translation = NULL;
  rsCopy->generation = ++generation;
  rsCopy->fCalculatingColors = rsSrc->fCalculatingColors;
  rsCopy->screening = rsSrc->screening;
  NAME_OBJECT(rsCopy, RASTERSTYLE_NAME);

  /* Now rsCopy is properly initialised, it is safe to goto error. */
#define return USE_goto_error

  /* Copy the cmap list. */
  for ( cmap = rsSrc->cmap; cmap != NULL; cmap = cmap->next ) {
    int32 n = 0;
    if ( cmap->cimap != NULL ) {
      while ( cmap->cimap[n] != COLORANTINDEX_UNKNOWN ) {
        ++n;
      }
    }
    if ( !guc_setColorantMapping(rsCopy, cmap->ci, cmap->cimap, n) )
      goto error;
  }

  if ( !gucs_copyNames(dlpools, rsSrc->colorChannelNames, rsSrc->numColorChannels,
                       &rsCopy->colorChannelNames, &rsCopy->numColorChannels) ||
       !gucs_copyNames(dlpools, rsSrc->processColorantNames, rsSrc->numProcessColorantNames,
                       &rsCopy->processColorantNames, &rsCopy->numProcessColorantNames) )
    goto error;

  if (rsSrc->processColorModel == DEVICESPACE_N) {
    OBJECT *csaSrc = oArray(rsSrc->colorSpace);
    OBJECT *csaCopy;
    uint16 csaSize = 4;
    OBJECT *alloc;
    int allocSize = (csaSize + rsSrc->numProcessColorantNames) * sizeof(OBJECT);
#ifdef ASSERT_BUILD
        COLORSPACE_ID pcmSpaceId;
#endif

    HQASSERT(csaSrc != NULL &&
             oType(rsSrc->colorSpace) == OARRAY &&
             gsc_getcolorspacetype(&rsSrc->colorSpace, &pcmSpaceId) &&
             pcmSpaceId == SPACE_DeviceN &&
             oArray(csaSrc[1]) != NULL,
             "DeviceN raster style should have a colorSpace");

    alloc = gucs_alloc(dlpools, allocSize, MM_ALLOC_CLASS_GUC_RASTERSTYLE);
    if ( alloc == NULL )
      goto error;
    HqMemCpy(alloc, csaSrc, allocSize); /* Copy CSA + colorant list in one */

    /* Point the CSA + colorant list to the new copies */
    csaCopy = &alloc[0];
    oArray(rsCopy->colorSpace) = csaCopy;
    oArray(csaCopy[1]) = &alloc[csaSize];
  }

  if ( !gucs_copySheetsChannelsAndColorants(rsSrc, rsCopy) )
    goto error;

  if ( !gucs_copyColorantSet(dlpools, rsSrc->pBackgroundFlags, &rsCopy->pBackgroundFlags) ||
       !gucs_copyColorantSet(dlpools, rsSrc->pColorantOmits, &rsCopy->pColorantOmits) ||
       !gucs_copyColorantSet(dlpools, rsSrc->pCurrentOmits, &rsCopy->pCurrentOmits) )
    goto error;

  if ( rsSrc->object_map_translation != NULL ) {
    rsCopy->object_map_translation =
      gucs_alloc(dlpools, 256 * sizeof(channel_output_t),
                 MM_ALLOC_CLASS_GUC_RASTERSTYLE);
    if ( rsCopy->object_map_translation == NULL ) {
      (void)error_handler(VMERROR);
      goto error;
    }
    HqMemCpy(rsCopy->object_map_translation, rsSrc->object_map_translation,
             256 * sizeof(channel_output_t));
  }

  if ( oType(rsSrc->osRGB) == ODICTIONARY &&
       !gucs_sRGBCopyDictionary(dlpools, &rsSrc->osRGB, &rsCopy->osRGB) )
    goto error;

  if ( !gucs_copyShallowDict(dlpools, &rsSrc->oReservedColorants,
                             &rsCopy->oReservedColorants) ||
       !gucs_copyShallowDict(dlpools, &rsSrc->oFullyFledgedColorants,
                             &rsCopy->oFullyFledgedColorants) )
    goto error;

  if ( !guc_updateEquivalentColorants(rsCopy, COLORANTINDEX_ALL) )
    goto error;

  if ( !guc_findPhotoinkColorantIndices(rsCopy, &rsCopy->photoinkInfo) )
    goto error;

  *rsDst = rsCopy;

#undef return
  return TRUE;
 error:
  guc_discardRasterStyle(&rsCopy);
  return FALSE;
}

/* ---------------------------------------------------------------------- */

/**
 * Initialise colorant structure with default values
 */
static void gucs_initColorant(
  /*@notnull@*/ /*@out@*/     GUCR_COLORANT* pColorant)
{
  static GUCR_COLORANT defaultColorant = {
    NULL,                           /* Next colorant */
    NULL,                           /* Owning channel  */
    {                               /* GUCR_COLORANT_INFO public structure */
      COLORANTINDEX_UNKNOWN,          /* colorantIndex */
      COLORANTTYPE_UNKNOWN,           /* colorantType */
      system_names + NAME_None,       /* Name */
      NULL,                           /* Orginal name */
      0, 0, 0,                        /* Offset x, y, band */
      RENDERING_PROPERTY_RENDER_ALL,
      0,                              /* No special handling */
      { -1.0f, -1.0f, -1.0f },        /* sRGB */
      { -1.0f, -1.0f, -1.0f, -1.0f }, /* CMYK */
      FALSE,                          /* fBackground */
      FALSE,                          /* fAutomatic */
      FALSE,                          /* fOverrideScreenAngle */
      0.0f,                           /* screenAngle */
      0.0f                            /* neutralDensity */
    },
    {                               /* Real colorant map */
      COLORANTINDEX_UNKNOWN,          /* equivalentRealColorant.ci */
      {
        COLORANTINDEX_UNKNOWN,          /* equivalentRealColorant.cimap[0] */
        COLORANTINDEX_UNKNOWN,          /* equivalentRealColorant.cimap[1] */
      },
      FALSE,                          /* equivalentRealColorant.recipe */
      NULL,                           /* equivalentRealColorant.next */
    },
    FALSE,                          /* fRenderOmit */
    FALSE,                          /* fRecalc */
  };

  /* Nice n easy structure assignment */
  *pColorant = defaultColorant;

  NAME_OBJECT(pColorant, COLORANT_NAME) ;
} /* Function gucs_initColorant */

/**
 * Allocate and initialise new colorant structure.
 * Owning channel is passed in as it is common initialisation value.
 * Note - nRenderingProperties has initial value RENDERING_PROPERTY_RENDER_ALL
 */
static Bool gucs_newColorant(mm_pool_t *dlpools,
  /*@notnull@*/ /*@out@*/    GUCR_COLORANT** ppColorant,
  /*@notnull@*/              GUCR_CHANNEL *pOwningChannel)
{
  GUCR_COLORANT* pColorant = NULL;

  HQASSERT(ppColorant != NULL,
           "gucs_newColorant: NULL pointer to returned colorant pointer");

  pColorant = gucs_alloc(dlpools, sizeof(GUCR_COLORANT),
                         MM_ALLOC_CLASS_GUC_COLORANT);
  if ( pColorant != NULL ) {
    gucs_initColorant(pColorant);
    pColorant->pOwningChannel = pOwningChannel;
  }

  *ppColorant = pColorant;

  return (pColorant != NULL) || error_handler(VMERROR);
} /* Function gucs_newColorant */


/**
 * Allocate and initialise new channel structure.
 * Owning sheet is passed in as it is common initialisation value.
 */
static Bool gucs_newChannel(mm_pool_t *dlpools,
  /*@notnull@*/ /*@out@*/   GUCR_CHANNEL** ppChannel,
  /*@notnull@*/             GUCR_SHEET *pOwningSheet)
{
  GUCR_CHANNEL*  pChannel = NULL;

  HQASSERT(ppChannel != NULL,
           "gucs_newChannel: NULL pointer to returned channel pointer");

  pChannel = gucs_alloc(dlpools, sizeof(GUCR_CHANNEL),
                        MM_ALLOC_CLASS_GUC_CHANNEL);
  if ( pChannel != NULL ) {
    /* init Channel */
    NAME_OBJECT(pChannel, CHANNEL_NAME) ;
    pChannel->nMapToThisChannel = 0;
    pChannel->pNextChannel = NULL;
    pChannel->pOwningSheet = pOwningSheet;
    pChannel->pColorants = NULL;
    pChannel->fRenderOmit = FALSE;
    pChannel->fRequired = FALSE;
    pChannel->fBlankColorants = FALSE;
    pChannel->fRequiredByPartialPaint = FALSE;
    pChannel->nRenderIndex = -1;
  }

  *ppChannel = pChannel;

  return (pChannel != NULL) || error_handler(VMERROR);
} /* Function gucs_newChannel */

/**
 * Allocate and initialise new sheet structure.
 * Owning raster is passed in as it is common initialisation value.
 */
static Bool gucs_newSheet(
  /*@notnull@*/ /*@out@*/ GUCR_SHEET** ppSheet,
  /*@notnull@*/           GUCR_RASTERSTYLE* pOwningRasterStyle)
{
  GUCR_SHEET*  pSheet = NULL;

  HQASSERT(ppSheet != NULL,
           "gucs_newSheet: NULL pointer to returned sheet pointer");

  pSheet = gucs_alloc(pOwningRasterStyle->dlpools, sizeof(GUCR_SHEET),
                      MM_ALLOC_CLASS_GUC_SHEET);
  if ( pSheet != NULL ) {
    /* init Sheet */
    NAME_OBJECT(pSheet, SHEET_NAME) ;
    pSheet->pNextSheet = NULL;
    pSheet->pOwningRasterStyle = pOwningRasterStyle;
    pSheet->pChannels = NULL;
    pSheet->cRequired = 0;
    pSheet->fRenderOmit = FALSE;
    pSheet->nNextRenderIndex = 0;
  }

  *ppSheet = pSheet;

  return (pSheet != NULL) || error_handler(VMERROR);
} /* Function gucs_newSheet */

/* ---------------------------------------------------------------------- */
static Bool gucs_setupSheetsChannelsAndColorants(GUCR_RASTERSTYLE * pRasterStyle,
                                                 OBJECT * poColorantPresence,
                                                 OBJECT * poSeparationOrder)
{
  /* sets up the sheet hierarchy when separating */

  int32 nSheets, nSheet, nChannels, nChannel;
  int32 nBestChoiceMapping = 0;
  COLORANTINDEX ciSeparationOrderEntry;
  GUCR_SHEET * pPreviousSheet, * pSheet;
  GUCR_CHANNEL ** ppChannel, * pChannel;
  GUCR_COLORANT * pColorant;
  NAMECACHE * pName;
  OBJECT* poPresence;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  pPreviousSheet = NULL;

  switch (pRasterStyle->nSeparationStyle) {
  case GUCR_SEPARATIONSTYLE_SEPARATIONS:
  case GUCR_SEPARATIONSTYLE_COLORED_SEPARATIONS:
  case GUCR_SEPARATIONSTYLE_PROGRESSIVES:
    nSheets = theLen(*poSeparationOrder);
    break;

  case GUCR_SEPARATIONSTYLE_MONOCHROME:
    nSheets = 1;
    HQASSERT(theLen(*poSeparationOrder) == 1,
              "monochrome separation style has more than one separation");
    break;

  case GUCR_SEPARATIONSTYLE_COMPOSITE:
    nSheets = 1;
    break;

  default:
    HQFAIL("unrecognized separation style");
    return FALSE;
  }


  for (nSheet = 0; nSheet < nSheets; nSheet++) {

    /* First, set up a sheet for each separation, or just one for a composite page. */
    if ( !gucs_newSheet(&pSheet, pRasterStyle) )
      return FALSE;

    pSheet->nSheetNumber = nSheet;

    if (pPreviousSheet == NULL)
      pRasterStyle->pSheets = pSheet;
    else
      pPreviousSheet->pNextSheet = pSheet;

    /* Second, for each sheet set up a channel for each ColorChannels entry in each
       sheet (often, for separations, there will be only one channel) */

    nChannels = pRasterStyle->numColorChannels;
    ppChannel = & pSheet->pChannels;

    for (nChannel = 0; nChannel < nChannels; nChannel++) {

      if ( !gucs_newChannel(pRasterStyle->dlpools, &pChannel, pSheet) )
        return FALSE;

      * ppChannel = pChannel;
      ppChannel = & pChannel->pNextChannel;
    }

    /* Third, locate the best channel(s) onto which to map the
       colorants of SeparationOrder according to separation style and
       the channel name in ColorChannels. This sets the
       nMapToThisChannel flag for each channel to which we should map
       the colorant */

    /* What is the index of the separation we are looking for */
    ciSeparationOrderEntry = COLORANTINDEX_UNKNOWN;
    if (pRasterStyle->nSeparationStyle != GUCR_SEPARATIONSTYLE_COMPOSITE) {
      HQASSERT(nSheet < theLen(*poSeparationOrder),
        "insufficient entries in poSeparationOrder in gucs_setupSeparationsChannelMapping");
      if (! gucs_nameFromObject(&oArray(*poSeparationOrder)[nSheet], &pName) )
        return FALSE;
      ciSeparationOrderEntry = guc_colorantIndex(pRasterStyle, pName) ;

      if (! gucs_setupSeparationsChannelMapping(pRasterStyle, pSheet,
                                                ciSeparationOrderEntry,
                                                & nBestChoiceMapping))
        return FALSE;
    }

    /* Fourth, assign the colorants */
    for (nChannel = 0, pChannel = pSheet->pChannels;
         pChannel != NULL;
         pChannel = pChannel->pNextChannel, nChannel++) {
      OBJECT cnObj = OBJECT_NOTVM_NOTHING;

      /*
       * Mark channels that must be present - check all ColorChannels names in
       * ColorantPresence dict.
       */
      pName = pRasterStyle->colorChannelNames[nChannel];
      object_store_namecache(&cnObj, pName, LITERAL);
      poPresence = extract_hash(poColorantPresence, &cnObj);
      if ( poPresence != NULL ) {
        HQASSERT(oType(*poPresence) == OINTEGER,
                 "ColorantPresence entry is not integer value");
        pChannel->fRequired =
          (uint8)(oInteger(*poPresence) == COLORANT_MUSTBEPRESENT);
        HQASSERT(pChannel->fRequired ||
                 oInteger(*poPresence) == COLORANT_CANBEOMITTED,
                 "ColorantPresence entry has unknown value");
        if ( pChannel->fRequired ) {
          /* Count number of required channels on sheet */
          HQASSERT((pSheet->cRequired < GUC_REQD_CHAN_LIMIT),
                   "gucs_setupSeparationsChannelMapping: required channel count overflow");
          pSheet->cRequired++;
        }
      }

      if (pRasterStyle->nSeparationStyle == GUCR_SEPARATIONSTYLE_COMPOSITE) {
        /* What colorant (if any) from separation order should we map
           onto this channel. This is similar to
           gucs_setupSeparationsChannelMapping, but applies colorants
           to channels rather than colorants of channels to
           sheets. Particular note: this doesn't honor the order of
           SeparationOrder. That's because most composite devices
           can't change the order of their channels. When they can the
           channels will be re-ordered too. However, colors which are
           absent from SeparationOrder are honorably omitted */

        int32 nColorant;
        NAMECACHE * pSeparationOrderName;

        pChannel->nMapToThisChannel = -1; /* dont map a colorant to it, provisionally */
        for (nColorant = 0; nColorant < theLen(*poSeparationOrder); nColorant++) {
          if (! gucs_nameFromObject(&oArray(*poSeparationOrder)[nColorant],
                                    &pSeparationOrderName))
            return FALSE;
          if (pSeparationOrderName == pName) {
            pChannel->nMapToThisChannel = nBestChoiceMapping = 1;
            break;
          }
        }

      } else {
        /* we just need the name for this sheet (maybe) */
        if (! gucs_nameFromObject(&oArray(*poSeparationOrder)[nSheet], & pName))
          return FALSE;
      }

      /* Create and init new colorant for channel */
      if ( !gucs_newColorant(pRasterStyle->dlpools, &pColorant, pChannel) )
        return FALSE;

      pColorant->info.specialHandling = guc_colorantSpecialHandling(pRasterStyle,
                                                                    pName) ;
      pColorant->info.neutralDensity = guc_colorantNeutralDensity(pRasterStyle,
                                                                  pName) ;
      {
        Bool fOverrideScreenAngle;
        if (! guc_colorantScreenAngle(pRasterStyle, pName,
                                      & pColorant->info.screenAngle,
                                      & fOverrideScreenAngle))
          return FALSE;
        pColorant->info.fOverrideScreenAngle = (uint8) fOverrideScreenAngle;
      }

      pRasterStyle->fHaveEquivalent = FALSE ;

      pChannel->pColorants = pColorant;

      if (pRasterStyle->nSeparationStyle == GUCR_SEPARATIONSTYLE_PROGRESSIVES &&
          pPreviousSheet != NULL) {
        /* create the same mapping for the corresponding channel from the
           previous sheet, when a real colorant is mapped, which will then be
           augmented (when realistic) by the best preference for the new
           channel below */
        int32 nPreviousChannel;
        GUCR_CHANNEL * pPreviousChannel;
        for (pPreviousChannel = pPreviousSheet->pChannels, nPreviousChannel = 0;
             pPreviousChannel != NULL && nPreviousChannel < nChannel;
             pPreviousChannel = pPreviousChannel->pNextChannel, nPreviousChannel++)
          EMPTY_STATEMENT ();
        if (pPreviousChannel != NULL) {
          HQASSERT(pPreviousChannel->pColorants != NULL,
                    "previous sheet has no colorants attached to corresponding channel");
          HQASSERT(pPreviousChannel->pColorants->pNextColorant == NULL,
                    "previous sheet has more than one colorant attached to corresponding channel");
          if (pPreviousChannel->pColorants->info.colorantIndex != COLORANTINDEX_UNKNOWN) {
            pColorant->info.colorantIndex = pPreviousChannel->pColorants->info.colorantIndex;
            pColorant->info.colorantType  = pPreviousChannel->pColorants->info.colorantType;
            pColorant->info.name         = pPreviousChannel->pColorants->info.name;
          }
        }
      }

      /* and for all separation styles ... */

      if (pChannel->nMapToThisChannel == nBestChoiceMapping) {
        pColorant->info.colorantIndex = guc_colorantIndex(pRasterStyle, pName);
        if ( pColorant->info.colorantIndex != COLORANTINDEX_UNKNOWN ) {
          pColorant->info.colorantType = (gucs_colorantIsProcess(pRasterStyle, pName)
                                     ? COLORANTTYPE_PROCESS
                                     : COLORANTTYPE_SPOT);
        }
        pColorant->info.name = pName;
      }

      if (! BACKDROP_RASTERSTYLE(pRasterStyle)) {
        pColorant->equivalentRealColorant.ci = pColorant->info.colorantIndex;
        pColorant->equivalentRealColorant.cimap[0] = pColorant->info.colorantIndex;
        HQASSERT(pColorant->equivalentRealColorant.cimap[1] == COLORANTINDEX_UNKNOWN,
                 "cimap[1] must always COLORANTINDEX_UNKNOWN in this case");
        HQASSERT(! pColorant->equivalentRealColorant.recipe,
                 "recipe flag should be false");
      }

      pChannel->nMapToThisChannel = 0;
    }

    pPreviousSheet = pSheet;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool gucs_copySheetsChannelsAndColorants(GUCR_RASTERSTYLE *rsSrc,
                                                GUCR_RASTERSTYLE *rsDst)
{
  GUCR_SHEET *sheetSrc, **sheetDst;

  for ( sheetSrc = rsSrc->pSheets, sheetDst = &rsDst->pSheets;
        sheetSrc != NULL;
        sheetSrc = sheetSrc->pNextSheet, sheetDst = &(*sheetDst)->pNextSheet ) {
    GUCR_CHANNEL *chanSrc, **chanDst;

    if ( !gucs_newSheet(sheetDst, rsDst) )
      return FALSE;

    (*sheetDst)->nSheetNumber = sheetSrc->nSheetNumber;
    (*sheetDst)->cRequired = sheetSrc->cRequired;
    (*sheetDst)->fRenderOmit = sheetSrc->fRenderOmit;
    (*sheetDst)->nNextRenderIndex = sheetSrc->nNextRenderIndex;

    for ( chanSrc = sheetSrc->pChannels, chanDst = &(*sheetDst)->pChannels;
          chanSrc != NULL;
          chanSrc = chanSrc->pNextChannel, chanDst = &(*chanDst)->pNextChannel ) {
      GUCR_COLORANT *colSrc, **colDst;

      if ( !gucs_newChannel(rsDst->dlpools, chanDst, *sheetDst) )
        return FALSE;

      (*chanDst)->nMapToThisChannel = chanSrc->nMapToThisChannel;
      (*chanDst)->fRenderOmit = chanSrc->fRenderOmit;
      (*chanDst)->fRequired = chanSrc->fRequired;
      (*chanDst)->fRenderOmit = chanSrc->fRenderOmit;
      (*chanDst)->fBlankColorants = chanSrc->fBlankColorants;
      (*chanDst)->nRenderIndex = chanSrc->nRenderIndex;

      for ( colSrc = chanSrc->pColorants, colDst = &(*chanDst)->pColorants;
            colSrc != NULL;
            colSrc = colSrc->pNextColorant, colDst = &(*colDst)->pNextColorant ) {

        if ( !gucs_newColorant(rsDst->dlpools, colDst, *chanDst) )
          return FALSE;

        (*colDst)->info = colSrc->info;
        (*colDst)->equivalentRealColorant = colSrc->equivalentRealColorant;
        (*colDst)->fRenderOmit = colSrc->fRenderOmit;
        (*colDst)->fRecalc = colSrc->fRecalc;
      }
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool gucs_setupSeparationsChannelMapping(
  /*@notnull@*/                                 GUCR_RASTERSTYLE * pRasterStyle,
  /*@notnull@*/                                 GUCR_SHEET * pSheet,
                                                COLORANTINDEX ciSeparationOrderEntry,
                                                int32 * pnBestChoiceMapping)
{
  int32 nChannel;
  GUCR_CHANNEL * pChannel;
  NAMECACHE * pName;
  COLORANTINDEX ci;
  NAMECACHE * pBlackName;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  VERIFY_OBJECT(pSheet, SHEET_NAME) ;
  HQASSERT(pSheet->pOwningRasterStyle == pRasterStyle,
           "incorrect parameters to gucs_setupSeparationsChannelMapping");

  pBlackName = pRasterStyle->pBlackName;

  * pnBestChoiceMapping = MAXINT32;
  pName = NULL;

  for (nChannel = 0, pChannel = pSheet->pChannels;
       pChannel != NULL;
       nChannel++, pChannel = pChannel->pNextChannel) {
    switch (pRasterStyle->nSeparationStyle) {

    case GUCR_SEPARATIONSTYLE_MONOCHROME:
    case GUCR_SEPARATIONSTYLE_SEPARATIONS:

      /* Map in order of preference onto Black channel, Gray channel or first channel,
         except for the one special case of RGB output where we map onto all channels
         (but inverted) - because we want black output and that is the only way to get
         it in RGB */

      /** \todo TODO: implement RGB mapping */

      HQASSERT(nChannel < pRasterStyle->numColorChannels,
                "insufficient channels in Color Channels");
      pName = pRasterStyle->colorChannelNames[nChannel];

      /* The order is as follows:
         Check to see if the colorant name is Black, then Gray,
         then see if it is the process colorant black named in
         the colorantType in the pagedevice. Then see if
         gsc_findBlackColorantIndex has given a possible
         black index that matches. Failing all this opt
         for the first colorant.
      */
      if (pName == system_names + NAME_Black) {
        pChannel->nMapToThisChannel = 1; /* first choice */
      } else if (pName == system_names + NAME_Gray) {
        pChannel->nMapToThisChannel = 2; /* second choice */
      } else if ((pBlackName != NULL) && (pBlackName == pName))  {
        /* first check to see if the plugin has a ProcessColorant_Black type in one channel*/
        pChannel->nMapToThisChannel = 3;
      } else if (nChannel == 0) {
        pChannel->nMapToThisChannel = 4; /* grab the first channel*/
      }

      break;

    case GUCR_SEPARATIONSTYLE_COLORED_SEPARATIONS:
    case GUCR_SEPARATIONSTYLE_PROGRESSIVES:

      HQASSERT(nChannel < pRasterStyle->numColorChannels,
                "insufficient channels in Color Channels");
      pName = pRasterStyle->colorChannelNames[nChannel];

      ci = guc_colorantIndex( pRasterStyle, pName);

      /* first preference: an exact color alias match */
      if (ci == ciSeparationOrderEntry)
        pChannel->nMapToThisChannel = 1; /* first choice */

      /* second preference: special cases of cross matching on
         colorants; not yet implemented because of difficulty with
         negation */

      else if (pName == system_names + NAME_Black)
        pChannel->nMapToThisChannel = 3; /* third preference: Black */
      else if (pName == system_names + NAME_Gray)
        pChannel->nMapToThisChannel = 4; /* fourth preference: Gray */
      else if (nChannel == 0)
        pChannel->nMapToThisChannel = 5; /* fifth preference, the first channel */

      break;

    case GUCR_SEPARATIONSTYLE_COMPOSITE:
      HQFAIL("didn't expect to encounter composites in gucs_setupSeparationsChannelMapping");
      return FALSE ;

    default:
      HQFAIL("unrecognised separation style found in gucs_setupSeparationsChannelMapping");
      return FALSE ;
    }

    /* Is this the best yet? */
    if (pChannel->nMapToThisChannel > 0) {
      if (pChannel->nMapToThisChannel < * pnBestChoiceMapping)
        * pnBestChoiceMapping = pChannel->nMapToThisChannel;
    }

  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool gucs_nameFromObject(const OBJECT * poName, NAMECACHE ** ppName)
{
  switch ( oType(*poName) ) {
  case OSTRING:
    * ppName = cachename(oString(*poName), theLen(*poName));
    if (* ppName == NULL)
      return error_handler(VMERROR);
    break;
  case ONAME:
    * ppName = oName(*poName);
    break;
  default:
    *ppName = NULL;
    return error_handler(TYPECHECK);
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool gucs_colorantIsProcess(
  /*@notnull@*/                    const GUCR_RASTERSTYLE* pRasterStyle,
  /*@null@*/                       const NAMECACHE* pName)
{
  int32 iProcessColorant;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(pRasterStyle->processColorantNames == NULL ||
           pRasterStyle->numProcessColorantNames > 0,
           "processColorantNames array setup badly");

  for (iProcessColorant = 0;
       iProcessColorant < pRasterStyle->numProcessColorantNames;
       ++iProcessColorant) {
    if (pRasterStyle->processColorantNames[iProcessColorant] == pName)
      return TRUE; /* pName is a process colorant. */
  }

  return FALSE; /* pName is not a process colorant. */
}

/* ---------------------------------------------------------------------- */
void guc_reserveRasterStyle(GUCR_RASTERSTYLE *pRasterStyle)
{
  hq_atomic_counter_t before ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  HqAtomicIncrement(&pRasterStyle->nReferenceCount, before) ;
  HQASSERT(before != 0, "Reference count uninitialised") ;

  HQASSERT(pRasterStyle->nReferenceCount > 0, "Reference count overflowed") ;
}


/* ---------------------------------------------------------------------- */
/** Delete a sheet and all its channels and colorants. */
static void deleteSheet(mm_pool_t *dlpools,
  /*@notnull@*/ /*@only@*/ GUCR_SHEET* pSheet)
{
  GUCR_COLORANT* pColorant;
  GUCR_COLORANT* pNextColorant;
  GUCR_CHANNEL*  pChannel;
  GUCR_CHANNEL*  pNextChannel;

  VERIFY_OBJECT(pSheet, SHEET_NAME) ;

  for ( pChannel = pSheet->pChannels; pChannel != NULL; pChannel = pNextChannel ) {

    for ( pColorant = pChannel->pColorants; pColorant != NULL; pColorant = pNextColorant ) {
      pNextColorant = pColorant->pNextColorant;
      UNNAME_OBJECT(pColorant) ;
      gucs_free(dlpools, (mm_addr_t)pColorant, sizeof(GUCR_COLORANT),
                MM_ALLOC_CLASS_GUC_COLORANT);
    }

    pNextChannel = pChannel->pNextChannel;
    UNNAME_OBJECT(pChannel) ;
    gucs_free(dlpools, (mm_addr_t)pChannel, sizeof(GUCR_CHANNEL),
              MM_ALLOC_CLASS_GUC_CHANNEL);
  }

  UNNAME_OBJECT(pSheet) ;
  gucs_free(dlpools, (mm_addr_t)pSheet, sizeof(GUCR_SHEET),
            MM_ALLOC_CLASS_GUC_SHEET);

} /* Function deleteSheet */


/* ---------------------------------------------------------------------- */
/** Throw the (hierarchical) list away */
void guc_discardRasterStyle(GUCR_RASTERSTYLE **ppRasterStyle)
{
  GUCR_RASTERSTYLE * pRasterStyle = (*ppRasterStyle);
  GUCR_SHEET * pSheet, * next;
  hq_atomic_counter_t after ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(pRasterStyle->nReferenceCount > 0,
           "more discards than copies done to GUCR_RASTERSTYLE");

  HqAtomicDecrement(&pRasterStyle->nReferenceCount, after) ;
  if (after == 0) {
    mm_pool_t *dlpools = pRasterStyle->dlpools;

    UNNAME_OBJECT(pRasterStyle) ;

    if ( dlpools != NULL ) {
      /* These dictionaries are allocated from dl pool memory. Only the original
         dictionary is freed and any dictionary extensions remain unfreed until
         the dl pool is destroyed. */
      if ( oType(pRasterStyle->oFullyFledgedColorants) == ODICTIONARY )
        gucs_freeWithHeader(dlpools, oDict(pRasterStyle->oFullyFledgedColorants) - 2,
                            MM_ALLOC_CLASS_GUC_RASTERSTYLE);
      if ( oType(pRasterStyle->oReservedColorants) == ODICTIONARY )
        gucs_freeWithHeader(dlpools, oDict(pRasterStyle->oReservedColorants) - 2,
                            MM_ALLOC_CLASS_GUC_RASTERSTYLE);
      if ( oType(pRasterStyle->osRGB) == ODICTIONARY ) {
        (void)walk_dictionary(&pRasterStyle->osRGB, gucs_freeArrayDictWalk, dlpools);
        gucs_freeWithHeader(dlpools, oDict(pRasterStyle->osRGB) - 2,
                            MM_ALLOC_CLASS_GUC_RASTERSTYLE);
      }
    } else {
      GUCR_RASTERSTYLE **prev ;
      for ( prev = &rasterstyle_gc_list ; *prev != NULL ; prev = &(*prev)->next )
        if ( *prev == pRasterStyle ) { /* Unlink from GC chain */
          *prev = pRasterStyle->next ;
          break ;
        }
    }

    if (pRasterStyle->colorChannelNames != NULL)
      gucs_free(dlpools, (mm_addr_t)pRasterStyle->colorChannelNames,
                pRasterStyle->numColorChannels * sizeof(NAMECACHE*),
                MM_ALLOC_CLASS_GUC_RASTERSTYLE);

    if (pRasterStyle->processColorantNames != NULL)
      gucs_free(dlpools, (mm_addr_t)pRasterStyle->processColorantNames,
                pRasterStyle->numProcessColorantNames * sizeof(NAMECACHE*),
                MM_ALLOC_CLASS_GUC_RASTERSTYLE);

    if (pRasterStyle->processColorModel == DEVICESPACE_N) {
      if (oArray(pRasterStyle->colorSpace) != NULL) {
        OBJECT *csa;
        uint16 csaSize = 4;
#ifdef ASSERT_BUILD
        COLORSPACE_ID pcmSpaceId;
#endif
        HQASSERT(oType(pRasterStyle->colorSpace) == OARRAY &&
                 gsc_getcolorspacetype(&pRasterStyle->colorSpace, &pcmSpaceId) &&
                 pcmSpaceId == SPACE_DeviceN,
                 "DeviceN raster style should have a colorSpace");
        csa = oArray(pRasterStyle->colorSpace);
        gucs_free(dlpools, csa,
                  (csaSize + pRasterStyle->numProcessColorantNames) * sizeof(OBJECT),
                  MM_ALLOC_CLASS_GUC_RASTERSTYLE);
      }
    }

    for (pSheet = pRasterStyle->pSheets; pSheet != NULL; pSheet = next) {
      next = pSheet->pNextSheet;
      deleteSheet(dlpools, pSheet);
    }

    if (pRasterStyle->pBackgroundFlags != NULL)
      gucs_free(dlpools, pRasterStyle->pBackgroundFlags,
                (sizeof(GUCR_COLORANTSET) +
                 pRasterStyle->pBackgroundFlags->cWords * sizeof(uint32)),
                MM_ALLOC_CLASS_GUC_COLORANTSET);

    if (pRasterStyle->pCurrentOmits != NULL)
      gucs_free(dlpools, pRasterStyle->pCurrentOmits,
                (sizeof(GUCR_COLORANTSET) +
                 pRasterStyle->pCurrentOmits->cWords * sizeof(uint32)),
                MM_ALLOC_CLASS_GUC_COLORANTSET);

    if (pRasterStyle->pColorantOmits != NULL)
      gucs_free(dlpools, pRasterStyle->pColorantOmits,
                (sizeof(GUCR_COLORANTSET) +
                 pRasterStyle->pColorantOmits->cWords * sizeof(uint32)),
                MM_ALLOC_CLASS_GUC_COLORANTSET);

    if (pRasterStyle->cmap != NULL) {
      COLORANTMAP *cmap = pRasterStyle->cmap ;
      do {
        COLORANTMAP *cmap_next = cmap->next ;
        if ( cmap->cimap != NULL )
          gucs_freeWithHeader(dlpools, ( mm_addr_t )cmap->cimap,
                              MM_ALLOC_CLASS_NCOLOR) ;
        gucs_freeWithHeader(dlpools, ( mm_addr_t )cmap,
                            MM_ALLOC_CLASS_NCOLOR) ;
        cmap = cmap_next ;
      } while ( cmap != NULL ) ;
    }

    guc_destroyphotoinkinfo(&pRasterStyle->photoinkInfo);

    if (pRasterStyle->object_map_translation != NULL)
      gucs_free(dlpools, pRasterStyle->object_map_translation,
                256 * sizeof(channel_output_t),
                MM_ALLOC_CLASS_GUC_RASTERSTYLE);

    /* Discard the parent rasterstyle, if it exists. This may recursively
       discard other rasterstyles. */
    if ( pRasterStyle->parent != NULL )
      guc_discardRasterStyle(&pRasterStyle->parent);

    gucs_free(dlpools, pRasterStyle, sizeof(GUCR_RASTERSTYLE),
              MM_ALLOC_CLASS_GUC_RASTERSTYLE);
  }

  * ppRasterStyle = NULL;
}

/* ======================================================================
   The following functions (together with guc_colorantIndexPossiblyNewSeparation)
   form the C interface to dynamic separations.
   */

/* ---------------------------------------------------------------------- */
Bool guc_fOmitMonochrome(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  return pRasterStyle->fOmitMonochrome ;
}

Bool guc_fOmitSeparations(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  return pRasterStyle->fOmitSeparations ;
}

/* ---------------------------------------------------------------------- */
static Bool gucs_equivalentRealColorantIndex(
  /*@notnull@*/                              GUCR_RASTERSTYLE* pRasterStyle,
  /*@notnull@*/                              GUCR_COLORANT* pColorant)
{
  COLORANTINDEX* cimap;
  GUCR_COLORANT* pEquivalentColorant;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;

  HQASSERT(BACKDROP_RASTERSTYLE(pColorant->pOwningChannel->pOwningSheet->pOwningRasterStyle),
           "Do not expect real rasterstyle to have unknown equivalentRealColorant");

  switch (pColorant->equivalentRealColorant.cimap[0]) {
  case COLORANTINDEX_UNKNOWN:
    if ( pRasterStyle->parent == NULL ) {
      COLORANTINDEX ci;

      /* Raster style is the device raster style, colorant is from a layered
         rasterstyle, which must be a backdrop rasterstyle. */
      HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
               "End of rasterstyle chain is not device rasterstyle");

      /* Lookup the colorant in the real rasterstyle. */
      ci = gucs_colorantIndexInDictionary(pColorant->info.name,
                                          &pRasterStyle->oFullyFledgedColorants);
      if (ci != COLORANTINDEX_UNKNOWN) {
        /* Only consider fully fledged colorants with a full colorant object,
           apparently not all fully fledged colorant have one. */
        gucr_colorantHandle(pRasterStyle, ci, & pEquivalentColorant);
        if (pEquivalentColorant != NULL) {
          /* There exists an equivalent colorant in the real rasterstyle. */
          pColorant->equivalentRealColorant.ci = pColorant->info.colorantIndex;
          pColorant->equivalentRealColorant.cimap[0] = pEquivalentColorant->info.colorantIndex;
          HQASSERT(pColorant->equivalentRealColorant.cimap[1] == COLORANTINDEX_UNKNOWN,
                   "cimap[1] must always COLORANTINDEX_UNKNOWN in this case");
          HQASSERT(! pColorant->equivalentRealColorant.recipe,
                   "recipe flag should be false");
          return TRUE;
        }
      }

      /* Check if the colorant is dealt with as a recipe of several
         other colorants. */
      ci = guc_colorantIndexReserved(pRasterStyle, pColorant->info.name);
      if (ci != COLORANTINDEX_UNKNOWN) {
        cimap = guc_getColorantMapping(pRasterStyle, ci);
        /* Colorant is actually a recipe/photink colorant.
           The mapping refers to the equivalent reserved colorant in the real
           rasterstyle, excluding final recipe/photoink colorant mapping (the
           recipe/photoink mapping may change under our feet at any time). */
        if (cimap != NULL) {
          /* There exists an equivalent colorant in the real rasterstyle, check
           * that the results of the mapping are fully fledged also.
           * I think that either all or none of the result of a mapping must exist
           * in the real raster, so we can quit as soon as we find one that is not
           * known (which should be the first!) */
          while ( *cimap != COLORANTINDEX_UNKNOWN ) {
            gucr_colorantHandle(pRasterStyle, *cimap, &pEquivalentColorant);
            if ( pEquivalentColorant == NULL ) {
              break;
            }
            cimap++;
          }

          if ( *cimap == COLORANTINDEX_UNKNOWN ) {
            pColorant->equivalentRealColorant.ci = pColorant->info.colorantIndex;
            pColorant->equivalentRealColorant.cimap[0] = ci;
            HQASSERT(pColorant->equivalentRealColorant.cimap[1] == COLORANTINDEX_UNKNOWN,
                     "cimap[1] must always COLORANTINDEX_UNKNOWN in this case");
            pColorant->equivalentRealColorant.recipe = TRUE;
            return TRUE;
          }
        }
      }
    } else {
      REALCOLORANTMAP *p_cmap ;

      HQASSERT(BACKDROP_RASTERSTYLE(pRasterStyle),
               "RasterStyle to query is not backdrop rasterstyle");

      /* Search previous rasterstyle for equivalent colorant. If one exists,
         copy its mapping. */
      for ( p_cmap = pRasterStyle->equivalentRealColorants ;
            p_cmap != NULL ;
            p_cmap = p_cmap->next )
        if ( p_cmap->ci == pColorant->info.colorantIndex ) {
          pColorant->equivalentRealColorant.ci = pColorant->info.colorantIndex;
          pColorant->equivalentRealColorant.cimap[0] = p_cmap->cimap[0];
          HQASSERT(pColorant->equivalentRealColorant.cimap[1] == COLORANTINDEX_UNKNOWN,
                   "cimap[1] must always COLORANTINDEX_UNKNOWN in this case");
          pColorant->equivalentRealColorant.recipe = p_cmap->recipe;
          return TRUE;
        }
    }

    /* Set to none to avoid repeating this work. */
    pColorant->equivalentRealColorant.cimap[0] = COLORANTINDEX_NONE;
    HQASSERT(pColorant->equivalentRealColorant.cimap[1] == COLORANTINDEX_UNKNOWN,
             "cimap[1] must always COLORANTINDEX_UNKNOWN in this case");
    return FALSE ;
  case COLORANTINDEX_NONE:
    /* No equivalent colorant exists in the real rasterstyle,
       ie this is a virtual colorant. */
    return FALSE;
  default:
    /* There exists an equivalent colorant in the real rasterstyle. */
    return TRUE;
  }
}

/** Given a ci, returns true iff there is an equivalent colorant in the real
   rasterstyle.  In the case of recipe colors, a colorant can map to several
   other colorants.  The equivalent colorant(s) are passed back in cimap,
   the last cimap entry contains COLORANTINDEX_UNKNOWN. */
Bool guc_equivalentRealColorantIndex(const GUCR_RASTERSTYLE *pRasterStyle,
                                     COLORANTINDEX ci, COLORANTINDEX **cimap)
{
  REALCOLORANTMAP *cmap;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  if ( cimap )
    *cimap = NULL;

  if (! BACKDROP_RASTERSTYLE(pRasterStyle)) {
    GUCR_COLORANT* pColorant;

    gucr_colorantHandle(pRasterStyle, ci, & pColorant);
    if (pColorant == NULL)
      return FALSE;

    HQASSERT(pColorant->equivalentRealColorant.cimap[0]
             == pColorant->info.colorantIndex,
             "equivalentRealColorant cimap not setup properly");
    if ( cimap != NULL )
      *cimap = pColorant->equivalentRealColorant.cimap;
    return TRUE;
  }

  cmap = pRasterStyle->equivalentRealColorants;
  while (cmap && cmap->ci != ci) {
    cmap = cmap->next;
  }

  if (cmap) {
    HQASSERT(cmap->cimap[0] != COLORANTINDEX_UNKNOWN &&
             cmap->cimap[1] == COLORANTINDEX_UNKNOWN,
             "Invalid equivalentRealColorants cimap");
    if ( cimap != NULL )
      *cimap = cmap->cimap;

    /* Include any additional mapping for recipe/photoinks. */
    if (cmap->recipe) {
      const GUCR_RASTERSTYLE* deviceRS ;
      COLORANTINDEX *mapping;

      /* Find the device rasterstyle at the end of the chain. */
      for ( deviceRS = pRasterStyle ; deviceRS->parent != NULL ;
            deviceRS = deviceRS->parent ) {
        VERIFY_OBJECT(deviceRS, RASTERSTYLE_NAME) ;
      }
      HQASSERT(!BACKDROP_RASTERSTYLE(deviceRS),
               "End of rasterstyle chain is not device rasterstyle") ;

      mapping = guc_getColorantMapping(deviceRS, cmap->cimap[0]);
      if ( mapping == NULL) {
        HQFAIL("Expecting recipe colorant mapping");
        return FALSE;
      }
      if ( cimap != NULL )
        *cimap = mapping;
    }
    return TRUE;
  }

  return FALSE;
}

/** Add the given colorant to the list of colorants
 * in a virtual raster style that have an equivalent real colorant.
 */
static Bool guc_addEquivalentColorant(
  /*@notnull@*/ /*@in@*/              GUCR_RASTERSTYLE* pRasterStyle,
  /*@notnull@*/                       GUCR_COLORANT *p_colorant)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT((BACKDROP_RASTERSTYLE(pRasterStyle)),
           "guc_addEquivalentColorant: not backdrop raster style");
  HQASSERT((p_colorant != NULL),
           "guc_addEquivalentColorant: NULL colorant pointer");

  /* Add the new colorant to the equivalent real colorant linked-list. */
  p_colorant->equivalentRealColorant.next = pRasterStyle->equivalentRealColorants;
  pRasterStyle->equivalentRealColorants = &p_colorant->equivalentRealColorant;

  return TRUE;
}

/** Remove the colorant with the colorant index
 * from the list of equivalent real colorants.
 */
static void guc_removeEquivalentColorant(
  /*@notnull@*/ /*@in@*/                 GUCR_RASTERSTYLE* pRasterStyle,
                                         COLORANTINDEX ci)
{
  REALCOLORANTMAP*  p_cmap;
  REALCOLORANTMAP** pp_cmap;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT((BACKDROP_RASTERSTYLE(pRasterStyle)),
           "guc_removeEquivalentColorant: not backdrop raster style");

  /* Remove from list of colorants having real equivalent */
  pp_cmap = &(pRasterStyle->equivalentRealColorants);
  p_cmap = *pp_cmap;
  while ( p_cmap != NULL ) {
    if ( p_cmap->ci == ci ) {
      *pp_cmap = p_cmap->next;
      return;
    }
    pp_cmap = &(p_cmap->next);
    p_cmap = p_cmap->next;
  }
}

/** Update the list of equivalent colorants in this raster style and all its
 * ancestors.  An equivalent colorant is a colorant that also exists in the
 * device rasterstyle (the colorant indices may differ).
 *  This function is typically called after a colorant has been removed by
 * removefromseparationcolornames or has its colorant mapping changed by
 * setcolorantmapping. The colorant index may be COLORANTINDEX_ALL, to reset all
 * mappings, or a colorant index. The update is optimised by making the
 * generation number of the backdrop rasterstyles match the device rasterstyle
 * when complete.
 */
Bool guc_updateEquivalentColorants(
  /*@notnull@*/ /*@in@*/               GUCR_RASTERSTYLE* pRasterStyle,
                                       COLORANTINDEX ci)
{
  GUCR_RASTERSTYLE *parentRS ;
  GUCR_SHEET* pSheet;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  parentRS = pRasterStyle->parent ;

  if ( parentRS == NULL ) {
    HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
             "End of rasterstyle parent chain should be device rasterstyle") ;
    /* Only real colorants exist, no equivalent real colorants. */
    return TRUE ;
  }

  HQASSERT(BACKDROP_RASTERSTYLE(pRasterStyle),
           "Non-backdrop rasterstyle has parent") ;

  /* Must recursively update before checking generation numbers, because
     only the device rasterstyle is guaranteed to change. */
  if ( !guc_updateEquivalentColorants(parentRS, ci) )
    return FALSE ;

  /* If the generation is the same, we haven't changed anything in the device
     rasterstyle that would affect the virtual or real colorant mapping. */
  if ( parentRS->generation == pRasterStyle->generation )
    return TRUE ;

  /* Look for any virtual colorants; ie, colorants which are fully
     fledged in pRasterStyle but are not fully fledged in the
     underlying raster style. */
  for (pSheet = pRasterStyle->pSheets;
       pSheet != NULL;
       pSheet = pSheet->pNextSheet) {
    GUCR_CHANNEL* pChannel;
    for (pChannel = pSheet->pChannels;
         pChannel != NULL;
         pChannel = pChannel->pNextChannel) {
      GUCR_COLORANT* pColorant;
      for (pColorant = pChannel->pColorants;
           pColorant != NULL;
           pColorant = pColorant->pNextColorant) {
        if ( ci == COLORANTINDEX_ALL || pColorant->info.colorantIndex == ci ) {
          /* Always reset the equivalence mapping from the parent. */
          pColorant->equivalentRealColorant.cimap[0] = COLORANTINDEX_UNKNOWN;
          if ( gucs_equivalentRealColorantIndex(parentRS, pColorant) ) {
            /* See if there was an equivalent real colorant previously */
            REALCOLORANTMAP *p_cmap ;

            for ( p_cmap = pRasterStyle->equivalentRealColorants ;
                  p_cmap != NULL && p_cmap->ci != pColorant->info.colorantIndex ;
                  p_cmap = p_cmap->next )
              EMPTY_STATEMENT() ;

            if ( p_cmap == NULL ) {
              /* Got an equivalent colorant where there was none previously. */
              if (! guc_addEquivalentColorant(pRasterStyle, pColorant))
                return FALSE;
            }
          } else {
            /* No longer an equivalent colorant, remove from the list. */
            guc_removeEquivalentColorant(pRasterStyle, pColorant->info.colorantIndex);
          }
        }
      }
    }
  }

  pRasterStyle->generation = parentRS->generation ;

  return TRUE;
}

/* ---------------------------------------------------------------------- */

/** Make a new separation automatically
   for the colorant whose name is passed to it when the AllSpotColors
   page device key is set.
   If the rasterstyle does not support automatic separations then named color
   interception can be invoked to possibly set up a colorant mapping.

   NOTE: Named color intercept _only_ happens if the device does not support
         dynamic separations.  With suitable logic we could (conditionally)
         invoke named color intercept and use results from it to decide
         whether to add separation as a dynamic one! */
Bool guc_addAutomaticSeparation(GUCR_RASTERSTYLE *pRasterStyle,
                                NAMECACHE * pColorantName,
                                Bool f_do_nci)
{
  COLORANTINDEX ci;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(pColorantName != NULL, "NULL colorant name given to guc_addAutomaticSeparation");

  ci = guc_colorantIndex(pRasterStyle, pColorantName);
  if (ci == COLORANTINDEX_UNKNOWN) {
    GUCR_RASTERSTYLE *parentRS;
    Bool addSeparation = FALSE;

    /* Colorant is not known - add as dynamic spot, or if not able try named
     * color interception */

    /* If the colorant is known/allowed in this, or any of the ancestor raster
       styles, then add the colorant to this raster style.
       fInheritSpotColors will normally be true except for soft masks for which
       we don't want to add any spots. */
    for (parentRS = pRasterStyle; parentRS != NULL; parentRS = parentRS->parent) {
      if (parentRS->fAddAllSpotColors ||
          guc_colorantIndex(parentRS, pColorantName) != COLORANTINDEX_UNKNOWN) {
        addSeparation = TRUE;
        break;
      }
      if (!parentRS->fInheritSpotColors)
        break;
    }

    if (addSeparation) {
      Bool subtractive;

      /* PDF 1.4, Section 4.5.5, p. 203, states that if the PCM is additive, a
         Separation color space never applies a process colorant directly; it
         always reverts to the alternate colorspace. */
      if (guc_deviceColorSpaceSubtractive(pRasterStyle, &subtractive) && !subtractive) {
        switch (pColorantName - system_names) {
        case NAME_Cyan: case NAME_Magenta: case NAME_Yellow: case NAME_Black:
        case NAME_Red:  case NAME_Green:   case NAME_Blue:   case NAME_Gray:
          addSeparation = FALSE;
          break;
        default:
          break;
        }
      }

      if (addSeparation) {
        /* as well as determining that we don't already have such a
           separation, this also excludes None and All: */
        DL_STATE *page = get_core_context_interp()->page;
        int32 specialHandling =
          guc_colorantSpecialHandling(pRasterStyle, pColorantName) ;
        USERVALUE neutralDensity =
          guc_colorantNeutralDensity(pRasterStyle, pColorantName) ;
        USERVALUE screenAngle;
        Bool fOverrideScreenAngle;
        COLORANTINDEX newCi = COLORANTINDEX_UNKNOWN ;

        if (! guc_colorantScreenAngle(pRasterStyle, pColorantName,
                                      & screenAngle, & fOverrideScreenAngle))
          return FALSE;

        if (!guc_colorantIndexPossiblyNewSeparation(pRasterStyle, pColorantName, &ci))
          return FALSE;
        if (! guc_newFrame(pRasterStyle, pColorantName,
                           0, 0, /* offset to 0,0 */
                           FALSE, /* not background */
                           pRasterStyle->fRemoveAllSpotColors, /* automatic */
                           RENDERING_PROPERTY_RENDER_ALL,
                           GUC_FRAMERELATION_END,
                           GUC_RELATIVE_TO_FRAME_UNKNOWN,
                           specialHandling,
                           neutralDensity,
                           screenAngle, fOverrideScreenAngle,
                           DOING_RUNLENGTH(page),
                           & newCi))
          return FALSE;
        HQASSERT(ci == newCi, "new frame yielded different colorant index from its name");

#if defined(ASSERT_BUILD)
        {
          GUCR_COLORANT* pColorant;
          gucr_colorantHandle(pRasterStyle, ci, & pColorant);
          HQASSERT(pColorant != NULL,
                   "Successfully added new separation but no colorant handle!");
        }
#endif /* ASSERT_BUILD */

        if (pRasterStyle->parent != NULL) {
          HQASSERT(BACKDROP_RASTERSTYLE(pRasterStyle),
                   "Real raster style should not be stacked on other rasters");

          /* Try adding the colorant to the underlying raster style */
          if (! guc_addAutomaticSeparation(pRasterStyle->parent,
                                           pColorantName,
                                           f_do_nci))
            return FALSE;

          if (!guc_updateEquivalentColorants(pRasterStyle, COLORANTINDEX_ALL))
            return FALSE;
        }

        /* Following the successful addition of a new colorant, we will patch
         * in a new screen into the page's default screen, updating page->
         * default_spot_no in the process. This is beneficial for composited
         * objects in the new colorant that would otherwise get their screen
         * angles from some other place. */
        invalidate_gstate_screens() ;
        if (!gsc_redo_setscreen(gstateptr->colorInfo))
          return FALSE;
        if ( page->default_spot_no != gsc_getSpotno(gstateptr->colorInfo) ) {
          int32 newSpotno;
          newSpotno = ht_mergespotnoentry(page->default_spot_no,
                                          gsc_getSpotno(gstateptr->colorInfo),
                                          HTTYPE_DEFAULT, ci, page->eraseno);
          if ( newSpotno > 0 ) {
            page->default_spot_no = newSpotno;
            ht_set_page_default(newSpotno);
          } else {
            if (newSpotno != 0)
              return FALSE;
          }
        }
      }

    } else if ( f_do_nci && !BACKDROP_RASTERSTYLE(pRasterStyle) ) {
      /* Named color interception is allowed if the device is not
       * auto-separating and the device is a real output device
       * (i.e., not a softmask rasterstyle).
       * If the separation does not have a mapping then see if named color
       * interception defined, and if there is invoke it (ignoring its results).
       */
      ci = guc_colorantIndexReserved(pRasterStyle, pColorantName);
      if ( (ci == COLORANTINDEX_UNKNOWN) ||
           (guc_getColorantMapping(pRasterStyle, ci) == NULL) ) {
        Bool temp;
        OBJECT colorantObj = OBJECT_NOTVM_NOTHING ;

        object_store_namecache(&colorantObj, pColorantName, LITERAL);

        if ( !gsc_invokeNamedColorIntercept(gstateptr->colorInfo,
                                            &colorantObj, &temp,
                                            FALSE, NULL, NULL, NULL) ) {
          return FALSE;
        }
      }
    }
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
void guc_removeAutomaticSeparations(GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  gucs_clearColorant(pRasterStyle, COLORANTINDEX_UNKNOWN, TRUE);
}

/* ---------------------------------------------------------------------- */
/** Remove all evidence of the given colorant
   index. Any subsequent uses of this colorant, for example in DeviceN
   color spaces will be diverted through the tint transforms. However,
   the colorant index will still exist in the reserved colorants
   dictionary, and any objects already prepared using this color will
   remain on the display list, cause knockouts as normal, but will
   simply not be rendered. If ci is COLORANTINDEX_ALL, all colorants are
   removed. */
void guc_clearColorant(GUCR_RASTERSTYLE *pRasterStyle, COLORANTINDEX ci)
{
  dictwalk_ci_t dictwalk_clearColorant;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");
  HQASSERT(ci != COLORANTINDEX_NONE && ci != COLORANTINDEX_UNKNOWN,
            "in guc_clearColorant, ci may not be none or unknown");

  dictwalk_clearColorant.pRasterStyle = pRasterStyle;
  dictwalk_clearColorant.ci = ci;
  dictwalk_clearColorant.fDoingAll = (ci == COLORANTINDEX_ALL);
  dictwalk_clearColorant.fClear = TRUE;

  (void)walk_dictionary(&pRasterStyle->oFullyFledgedColorants,
                        gucs_dictwalk_clearColorant,
                        &dictwalk_clearColorant);
}

/* ---------------------------------------------------------------------- */
static void gucs_clearColorant(GUCR_RASTERSTYLE * pRasterStyle,
                               COLORANTINDEX ci, Bool fAutomatic)
{
  GUCR_SHEET**    ppSheet;
  GUCR_SHEET*     pSheet;
  GUCR_CHANNEL**  ppChannel;
  GUCR_CHANNEL*   pChannel;
  GUCR_COLORANT** ppColorant;
  GUCR_COLORANT*  pColorant;
  Bool           fNoOtherColorantSeen;
  dictwalk_ci_t  dictwalk_clearColorant;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME);
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");
  HQASSERT(ci != COLORANTINDEX_UNKNOWN || fAutomatic,
           "gucs_clearColorant: deleting colorant index UNKNOWN");

  /* Walk over all sheets */
  ppSheet = &(pRasterStyle->pSheets);
  while ( *ppSheet != NULL ) {
    pSheet = *ppSheet;

    /* Trace if any active colorants on a sheet */
    fNoOtherColorantSeen = TRUE;

    /* Walk over all channels */
    ppChannel = &(pSheet->pChannels);
    while ( *ppChannel != NULL ) {
      pChannel = *ppChannel;

      /* Walk over all colorants */
      ppColorant = &(pChannel->pColorants);
      while ( *ppColorant != NULL ) {
        pColorant = *ppColorant;

        if ( fAutomatic
             ? pColorant->info.fAutomatic
             : ci == pColorant->info.colorantIndex ) {
          /* Auto sep when looking for them, or colorant index matched */
          if ( pColorant->info.colorantIndex != COLORANTINDEX_UNKNOWN &&
               pColorant->info.colorantIndex != COLORANTINDEX_NONE &&
               pColorant->info.colorantIndex != COLORANTINDEX_ALL ) {

            guc_removeColorantFromSet(pRasterStyle->pCurrentOmits,
                                      pColorant->info.colorantIndex);
            if ( fAutomatic ) {
              /* Demote the colorant */
              dictwalk_clearColorant.pRasterStyle = pRasterStyle;
              dictwalk_clearColorant.ci           = pColorant->info.colorantIndex;
              dictwalk_clearColorant.fDoingAll    = FALSE;
              dictwalk_clearColorant.fClear       = FALSE;
              (void)walk_dictionary(&pRasterStyle->oFullyFledgedColorants,
                                    gucs_dictwalk_clearColorant,
                                    &dictwalk_clearColorant);
            }
          }

          /* Remove matching colorants from the list unless it is the last for
           * a required channel when it is made anonymous
           */
          if ( !(ppColorant == &pChannel->pColorants &&
                 pColorant->pNextColorant == NULL &&
                 pChannel->fRequired) ) {
            *ppColorant = pColorant->pNextColorant;
            gucs_free(pRasterStyle->dlpools, (mm_addr_t)pColorant,
                      sizeof(GUCR_COLORANT), MM_ALLOC_CLASS_GUC_COLORANT);

          } else {
            pColorant->info.name         = system_names + NAME_None;
            pColorant->info.originalName = NULL;
            pColorant->info.colorantIndex = COLORANTINDEX_UNKNOWN;
            pColorant->info.colorantType  = COLORANTTYPE_UNKNOWN;

            ppColorant = &(pColorant->pNextColorant);
          }

        } else { /* Check for an active colorant on the channel */
          fNoOtherColorantSeen = fNoOtherColorantSeen &&
            (pColorant->info.colorantIndex == COLORANTINDEX_UNKNOWN);
          ppColorant = &(pColorant->pNextColorant);
        }
      }

      /* Delete channels with no remaining colorants */
      if ( pChannel->pColorants != NULL ) {
        ppChannel = &(pChannel->pNextChannel);

      } else {
        HQASSERT((!pChannel->fRequired),
                 "gucs_clearColorant: deleting required channel");
        *ppChannel = pChannel->pNextChannel;
        UNNAME_OBJECT(pChannel) ;
        gucs_free(pRasterStyle->dlpools, (mm_addr_t)pChannel,
                  sizeof(GUCR_CHANNEL), MM_ALLOC_CLASS_GUC_CHANNEL);
      }
    }

    /* Secondly, if there are no real colorants at all left, remove the sheet and
       all its substructure; however, for composites, always keep the last
       remaining sheet with all its channels, even if all the colorants are now
       anonymous */
    if ( fNoOtherColorantSeen &&
         (pRasterStyle->nSeparationStyle != GUCR_SEPARATIONSTYLE_COMPOSITE ||
          !(ppSheet == &pRasterStyle->pSheets && pSheet->pNextSheet == NULL)) ) {
      *ppSheet = pSheet->pNextSheet;
      deleteSheet(pRasterStyle->dlpools, pSheet);

    } else { /* Move onto next sheet */
      ppSheet = &(pSheet->pNextSheet);
    }
  }
}


/** Clear a colorant out of a rasterstyle by
 * (optionally) removing it from the sheet/channel/colorant structure and
 * demoting it to a lowly reserved colorant.
 * NOTE: It is possible for a colorantindex to be referenced by more than one
 *       named colorant and they all need to be demoted.
 */
static Bool gucs_dictwalk_clearColorant(OBJECT* poKey, OBJECT* poValue,
                                        void* pvPrivate)
{
  dictwalk_ci_t* pDictwalk_clearColorant = pvPrivate;
  NAMECACHE* pnmColorantName;
  COLORANTINDEX ci;
  GUCR_RASTERSTYLE* deviceRS = pDictwalk_clearColorant->pRasterStyle;

  HQASSERT(!BACKDROP_RASTERSTYLE(deviceRS),
           "Expected real device raster style");

  HQASSERT(oType(*poValue) == OINTEGER,
           "gucs_dictwalk_clearColorant: non integer found in oFullyFledgedColorants");

  ci = (COLORANTINDEX)oInteger(*poValue);

  if ( pDictwalk_clearColorant->fDoingAll ||
       pDictwalk_clearColorant->ci == ci ) {

    /* Clear the colorant from the sheet/channel/colorant list */
    if ( pDictwalk_clearColorant->fClear )
      gucs_clearColorant(deviceRS, ci, FALSE);

    /* Transfer the colorant from oFullyFledgedColorants to oReservedColorants */
    if ( !gucs_nameFromObject(poKey, &pnmColorantName) )
      return FALSE;

    gucs_removeColorantFromDictionary(pnmColorantName,
                                      &deviceRS->oFullyFledgedColorants);
    if (!gucs_addColorantToDictionary(deviceRS->dlpools, pnmColorantName, ci,
                                      &deviceRS->oReservedColorants))
      return FALSE;

    deviceRS->generation = ++generation ;
  }

  return TRUE;
}

/** Add a named colorant for rendering, with the
   separation offset by x,y (left-handed coordinates, but in pixels). It
   must already be a known colorant. When in the rendering sequence the
   colorant will appear depends on frameRelation and
   nRelativeToFrame. nRelativeToFrame is the sequence number of the
   frame (separation or channel) where the new colorant is to be placed
   relative to, and frameRelation specifies the relation - before,
   after, at that frame, or at the end or start of all frames (in the
   last two cases nRelativeToFrame must be set to the unique value
   GUC_RELATIVE_TO_FRAME_UNKNOWN). */
Bool guc_newFrame(GUCR_RASTERSTYLE *pRasterStyle,
                  NAMECACHE * pColorantName,
                  int32 x, int32 y,
                  Bool fBackground,
                  Bool fAutomatic,
                  uint32 nRenderingProperties,
                  GUC_FRAMERELATION frameRelation,
                  int32 nRelativeToFrame,
                  int32 specialHandling,
                  USERVALUE neutralDensity,
                  USERVALUE screenAngle,
                  Bool fOverrideScreenAngle,
                  Bool doingRLE,
                  COLORANTINDEX * pCi)
{
  Bool fResult;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  if (pRasterStyle->nSeparationStyle == GUCR_SEPARATIONSTYLE_COMPOSITE) {

    if ( pRasterStyle->nInterleavingStyle == GUCR_INTERLEAVINGSTYLE_PIXEL &&
         !doingRLE && !BACKDROP_RASTERSTYLE(pRasterStyle) )
      return detail_error_handler(RANGECHECK,
        "Separations may not be added when pixel interleaving.");

    fResult = gucs_newFrameComposite(pRasterStyle, pColorantName, x, y,
                                     fBackground, fAutomatic, nRenderingProperties,
                                     frameRelation, nRelativeToFrame,
                                     specialHandling, neutralDensity,
                                     screenAngle, fOverrideScreenAngle, pCi);
  } else {
    fResult = gucs_newFrameSeparations(pRasterStyle, pColorantName, x, y,
                                       fBackground, fAutomatic, nRenderingProperties,
                                       frameRelation, nRelativeToFrame,
                                       specialHandling, neutralDensity,
                                       screenAngle, fOverrideScreenAngle, pCi);
  }

  /* Adding new colorants can change the size of the band required for
     band-interleaved colorants, and the (HHR) PGB device's band cache. We want
     to update the resource requirements as soon as possible, so that we don't
     run into VMERRORs at render start. */
  if ( fResult && !BACKDROP_RASTERSTYLE(pRasterStyle) ) {
    DL_STATE *page = get_core_context_interp()->page;

    HQASSERT(pRasterStyle == page->hr, "More than one device rasterstyle.");
    fResult = call_pagebuffer_raster_requirements(page->pgbdev, FALSE,
                                                  page, pRasterStyle,
                                                  max_simultaneous_tasks(),
                                                  0, NULL)
              && band_resource_pools(page);
  }
  if ( y != 0 )
    guc_positionSeparations( pRasterStyle , FALSE ) ;

  pRasterStyle->generation = ++generation ;

  return fResult;
}

/* ---------------------------------------------------------------------- */
static Bool gucs_newFrameSeparations(GUCR_RASTERSTYLE * pRasterStyle,
                                     NAMECACHE * pColorantName,
                                     int32 x, int32 y,
                                     Bool fBackground,
                                     Bool fAutomatic,
                                     uint32 nRenderingProperties,
                                     GUC_FRAMERELATION frameRelation,
                                     int32 nRelativeToFrame,
                                     int32 specialHandling,
                                     USERVALUE neutralDensity,
                                     USERVALUE screenAngle,
                                     Bool fOverrideScreenAngle,
                                     COLORANTINDEX * pCi)
{
  /* Adds new colors by sheet, for separting modes (and monochrome) */

  GUCR_SHEET * pSheet, * pNumberSheet, ** ppSheet;
  GUCR_CHANNEL * pChannel, ** ppChannel;
  GUCR_COLORANT * pColorant, ** ppColorant;
  int32 nNew;
  int32 nChannel, nChannels, nSheet;
  int32 nBestChoiceMapping;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  /* First, find the referred to sheet */
  switch (frameRelation) {
  case GUC_FRAMERELATION_START:
    HQASSERT(nRelativeToFrame == GUC_RELATIVE_TO_FRAME_UNKNOWN,
              "nRelativeToFrame must be GUC_RELATIVE_TO_FRAME_UNKNOWN when \
               frameRelation is GUC_FRAMERELATION_START");
    ppSheet = & pRasterStyle->pSheets;
    nNew = 1;
    break;

  case GUC_FRAMERELATION_END:
    HQASSERT(nRelativeToFrame == GUC_RELATIVE_TO_FRAME_UNKNOWN,
              "nRelativeToFrame must be GUC_RELATIVE_TO_FRAME_UNKNOWN when \
               frameRelation is GUC_FRAMERELATION_END");
    for (ppSheet = & pRasterStyle->pSheets;
         * ppSheet != NULL;
         ppSheet = & (* ppSheet)->pNextSheet)
      EMPTY_STATEMENT();
    nNew = 1;
    break;

  case GUC_FRAMERELATION_AT:
  case GUC_FRAMERELATION_AFTER:
  case GUC_FRAMERELATION_BEFORE:

    for (ppSheet = & pRasterStyle->pSheets, nSheet = 0;
         * ppSheet != NULL;
         ppSheet = & (* ppSheet)->pNextSheet, nSheet++) {
      if (nSheet == nRelativeToFrame)
        break;
    }

    /* If we want to put it at a particular frame, and that doesn't exist, we must
       create it and all the intermediate ones. Consider how /Positions works in the
       halftone dictionary - the caller quotes a sheet number, and the dictionary
       entries may come in any order, so we may be asked to create sheet 3 before
       sheet 1. We always start with an empty list in the case of a halftone
       dictionmary, so it all works out in thte end, though we may end up with some
       empty sheets. That is allowed. In other cases, the indexing shuffles around
       the shop, and it is intended that sheets are added relative to other colorants
       rather than by index, though it isn't a requirement. */

    nNew = 1;
    if (frameRelation == GUC_FRAMERELATION_AT) {
      if (* ppSheet == NULL) /* remembering that the last iteration above incremented nSheet */
        nNew = nRelativeToFrame - nSheet + 1;
      else
        nNew = 0;
    }

    if (frameRelation == GUC_FRAMERELATION_AFTER)
      ppSheet = & (* ppSheet)->pNextSheet;

    break;

  default:
    /* quiet the compiler */
    ppSheet = NULL;
    nNew = 0;
    HQFAIL("unexpected frame relation given to guc_colorantIndexPossiblyNewFrame");
    return FALSE;
  }

  /* Second, select or create the new sheet(s); renumber sheets */

  pSheet = * ppSheet;
  while (nNew > 0) {
    if ( !gucs_newSheet(&pSheet, pRasterStyle) )
      return FALSE;

    pSheet->pNextSheet = *ppSheet;

    * ppSheet = pSheet;
    ppSheet = & pSheet->pNextSheet;
    nNew--;
  }

  for (nSheet = 0, pNumberSheet = pRasterStyle->pSheets;
       pNumberSheet != NULL;
       nSheet++, pNumberSheet = pNumberSheet->pNextSheet) {
    pNumberSheet->nSheetNumber = nSheet;
  }

  /* Third, if the selected or created sheet has no channels, create them as required */

  if (pSheet->pChannels == NULL) {
    nChannels = pRasterStyle->numColorChannels;
    ppChannel = & pSheet->pChannels;

    for (nChannel = 0; nChannel < nChannels; nChannel++) {

      if ( !gucs_newChannel(pRasterStyle->dlpools, &pChannel, pSheet) )
        return FALSE;

      * ppChannel = pChannel;
      ppChannel = & pChannel->pNextChannel;
    }
  }

  /* Fourth, locate the best channel(s) onto which to map the new colorant */

  /* What is the index of the channel we may be tring to match */
  * pCi = guc_colorantIndex( pRasterStyle, pColorantName);

  if (! gucs_setupSeparationsChannelMapping(pRasterStyle, pSheet,
                                            * pCi, & nBestChoiceMapping))
    return FALSE;

  /* Fifth, assign the colorants */
  for (nChannel = 0, pChannel = pSheet->pChannels;
       pChannel != NULL;
       pChannel = pChannel->pNextChannel, nChannel++) {
    OBJECT* poPresence;
    OBJECT cnObj = OBJECT_NOTVM_NOTHING;

    /* Mark channels that must be present - check all ColorChannels names in
     * ColorantPresence dict. */
    object_store_namecache(&cnObj, pRasterStyle->colorChannelNames[nChannel], LITERAL);
    poPresence = extract_hash(&pRasterStyle->oColorantPresence, &cnObj);
    if (poPresence != NULL) {
      pChannel->fRequired = (uint8)(oInteger(*poPresence) == COLORANT_MUSTBEPRESENT);
      if (pChannel->fRequired) {
        pSheet->cRequired++;
      }
    }

    if (pChannel->pColorants == NULL || pChannel->nMapToThisChannel == nBestChoiceMapping) {

      /* Create and init new colorant for channel */
      if ( !gucs_newColorant(pRasterStyle->dlpools, &pColorant, pChannel) )
        return FALSE;

      pColorant->info.nRenderingProperties = nRenderingProperties;
      pColorant->info.fAutomatic = (uint8)fAutomatic;
      pColorant->info.specialHandling = specialHandling;
      pColorant->info.fOverrideScreenAngle = (uint8) fOverrideScreenAngle;
      pColorant->info.screenAngle = screenAngle;
      pColorant->info.neutralDensity = neutralDensity;

      pRasterStyle->fHaveEquivalent = FALSE ;

      if (pChannel->nMapToThisChannel == nBestChoiceMapping) {

        pColorant->info.colorantIndex = * pCi;
        pColorant->info.name =
          * pCi == COLORANTINDEX_UNKNOWN ? system_names + NAME_None : pColorantName;

        /* link in to colorant chain in correct y,x sort order */
        for (ppColorant = & pChannel->pColorants;
             * ppColorant != NULL;
             ppColorant = & (* ppColorant)->pNextColorant) {
          if (y > (* ppColorant)->info.offsetY ||
              (y == (* ppColorant)->info.offsetY && x <= (* ppColorant)->info.offsetX))
            break;
        }
        pColorant->pNextColorant = * ppColorant;
        pColorant->info.offsetX = x;
        pColorant->info.offsetY = y;

        if ( *pCi != COLORANTINDEX_UNKNOWN ) {
          Bool fOmitIfBlank ;
          if (gucs_colorantIsProcess(pRasterStyle, pColorant->info.name)) {
            pColorant->info.colorantType = COLORANTTYPE_PROCESS ;
            fOmitIfBlank = pRasterStyle->fOmitProcess ;
          } else {
            pColorant->info.colorantType = COLORANTTYPE_EXTRASPOT ;
            fOmitIfBlank = pRasterStyle->fOmitExtraSpot ;
          }
          if ( guc_colorantInSet(pRasterStyle->pColorantOmits, *pCi) ) {
            if (! guc_addColorantToSet(pRasterStyle->dlpools,
                                       &pRasterStyle->pCurrentOmits, *pCi))
              return FALSE;
          } else if ( fOmitIfBlank ) {
            if ( !guc_addColorantToSet(pRasterStyle->dlpools,
                                       &pRasterStyle->pCurrentOmits, *pCi) ||
                 !guc_addColorantToSet(pRasterStyle->dlpools,
                                       &pRasterStyle->pColorantOmits, *pCi) )
              return FALSE;
          }
          gucr_setColorantColor(pColorant, &pRasterStyle->osRGB) ;
        }

        if (! BACKDROP_RASTERSTYLE(pRasterStyle)) {
          pColorant->equivalentRealColorant.ci = pColorant->info.colorantIndex;
          pColorant->equivalentRealColorant.cimap[0] = pColorant->info.colorantIndex;
          HQASSERT(pColorant->equivalentRealColorant.cimap[1] == COLORANTINDEX_UNKNOWN,
                   "cimap[1] must always COLORANTINDEX_UNKNOWN in this case");
          HQASSERT(! pColorant->equivalentRealColorant.recipe,
                   "recipe flag should be false");
        }

        * ppColorant = pColorant;

      } else {
        pChannel->pColorants = pColorant;

        if (fOverrideScreenAngle) {

          if (pColorant->info.fOverrideScreenAngle &&
              screenAngle != pColorant->info.screenAngle)
            /* New override screen angle differs from previous value for this colorant */
            return error_handler(RANGECHECK);

          pColorant->info.fOverrideScreenAngle = (uint8) fOverrideScreenAngle;
          pColorant->info.screenAngle = screenAngle;
        }
      }

    }

    pChannel->nMapToThisChannel = 0;
  }

  if (fBackground)
    if (! guc_colorantSetBackgroundSeparation(pRasterStyle, *pCi) )
      return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool gucs_newFrameComposite(GUCR_RASTERSTYLE * pRasterStyle,
                                   NAMECACHE * pColorantName,
                                   int32 x, int32 y,
                                   Bool fBackground,
                                   Bool fAutomatic,
                                   uint32 nRenderingProperties,
                                   GUC_FRAMERELATION frameRelation,
                                   int32 nRelativeToFrame,
                                   int32 specialHandling,
                                   USERVALUE neutralDensity,
                                   USERVALUE screenAngle,
                                   Bool fOverrideScreenAngle,
                                   COLORANTINDEX * pCi)
{
  /* Adds new colors by channel - either a new channel or adding to an existing
     channel - for composite (but not monochrome) output */

  GUCR_CHANNEL ** ppChannel, * pChannel;
  GUCR_COLORANT ** ppColorant, * pColorant;
  int32 nNew;
  int32 nChannel;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  /* Firstly, find the referred to channel */

  HQASSERT(pRasterStyle->pSheets != NULL && pRasterStyle->pSheets->pNextSheet == NULL,
    "unexpected sheet configuration for composite style in guc_colorantIndexPossiblyNewFrame");

  switch (frameRelation) {
  case GUC_FRAMERELATION_START:
    HQASSERT(nRelativeToFrame == GUC_RELATIVE_TO_FRAME_UNKNOWN,
              "nRelativeToFrame must be GUC_RELATIVE_TO_FRAME_UNKNOWN when \
               frameRelation is GUC_FRAMERELATION_START");
    ppChannel = & pRasterStyle->pSheets->pChannels;
    nNew = 1;
    break;

  case GUC_FRAMERELATION_END:
    HQASSERT(nRelativeToFrame == GUC_RELATIVE_TO_FRAME_UNKNOWN,
              "nRelativeToFrame must be GUC_RELATIVE_TO_FRAME_UNKNOWN when \
               frameRelation is GUC_FRAMERELATION_END");

    nNew = 1;
    /* when we remove colorants from composites, we retain a blank
       channel. Therefore, GUC_FRAMERELATION_END means the first wholly blank channel
       if any, and only if there isnt one, then right at the end */
    for (ppChannel = & pRasterStyle->pSheets->pChannels;
         * ppChannel != NULL;
         ppChannel = & (* ppChannel)->pNextChannel) {
      if ((* ppChannel)->pColorants != NULL &&
          (* ppChannel)->pColorants->pNextColorant == NULL &&
          (* ppChannel)->pColorants->info.colorantIndex == COLORANTINDEX_UNKNOWN) {
        nNew = 0;
        break;
      }
    }
    break;

  case GUC_FRAMERELATION_AT:
  case GUC_FRAMERELATION_AFTER:
  case GUC_FRAMERELATION_BEFORE:
    for (ppChannel = & pRasterStyle->pSheets->pChannels, nChannel = 0;
         * ppChannel != NULL;
         ppChannel = & (* ppChannel)->pNextChannel, nChannel++) {
      if (nChannel == nRelativeToFrame)
        break;
    }

    /* See comment in gucs_newFrameSeparations about intervening frames */

    nNew = 1;
    if (frameRelation == GUC_FRAMERELATION_AT) {
      if (* ppChannel == NULL) /* remembering that the last iteration above incremented nSheet */
        nNew = nRelativeToFrame - nChannel + 1;
      else
        nNew = 0;
    }

    if (frameRelation == GUC_FRAMERELATION_AFTER)
      ppChannel = & (* ppChannel)->pNextChannel;

    break;

  default:
    HQFAIL("unexpected frame relation given to guc_colorantIndexPossiblyNewFrame");
    /* quiet the compiler */
    ppChannel = NULL;
    nNew = 0;
    return FALSE;
  }

  for (;;) {

    /* Secondly, select or create the new channel(s) */

    if (nNew > 0) {

      if ( !gucs_newChannel(pRasterStyle->dlpools, &pChannel,
                            pRasterStyle->pSheets) )
        return FALSE;

      pChannel->pNextChannel = *ppChannel;

      * ppChannel = pChannel;
      ppChannel = & pChannel->pNextChannel;
    } else {
      pChannel = * ppChannel;
    }

    /* Thirdly, Create a new colorant in the new or identified channel, or if we
       already have an unknown colorant and it is the one we are to insert at,
       replace it. We may end up still leaving it blank if it doesn't exist, or we
       may end up with more than one colorant (possibly the same one
       stepped-and-repeated) if there is already an extant colorant on this channel */

    if (pChannel->pColorants != NULL &&
        pChannel->pColorants->info.colorantIndex == COLORANTINDEX_UNKNOWN) {
      pColorant = pChannel->pColorants;

      if (fOverrideScreenAngle) {

        if (pColorant->info.fOverrideScreenAngle &&
            screenAngle != pColorant->info.screenAngle)
          /* New override screen angle differs from previous value for this colorant */
          return error_handler(RANGECHECK);

        pColorant->info.fOverrideScreenAngle = (uint8) fOverrideScreenAngle;
        pColorant->info.screenAngle = screenAngle;
      }
    } else {
      /* Create and init new colorant for channel */
      if ( !gucs_newColorant(pRasterStyle->dlpools, &pColorant, pChannel) )
        return FALSE;

      pColorant->info.nRenderingProperties = nRenderingProperties;
      pColorant->info.fAutomatic = (uint8)fAutomatic;
      pColorant->info.specialHandling = specialHandling;
      pColorant->info.fOverrideScreenAngle = (uint8) fOverrideScreenAngle;
      pColorant->info.screenAngle = screenAngle;
      pColorant->info.neutralDensity = neutralDensity;

      /* link in to colorant chain in correct y,x sort order */
      for ( ppColorant = &pChannel->pColorants;
            *ppColorant != NULL;
            ppColorant = &(*ppColorant)->pNextColorant ) {
        if ( (y > (* ppColorant)->info.offsetY) ||
             ((y == (* ppColorant)->info.offsetY) && (x <= (* ppColorant)->info.offsetX)) ) {
          break;
        }
      }
      pColorant->pNextColorant = *ppColorant;

      pRasterStyle->fHaveEquivalent = FALSE ;

      * ppColorant = pColorant;
    }

    /* is there an index for this colorant and is it the last channel to be added?
       If so, name it; otherwise leave it blank */
    if (nNew <= 1) {
      * pCi = guc_colorantIndex(pRasterStyle, pColorantName);
      if (* pCi != COLORANTINDEX_UNKNOWN) {
        Bool fOmitIfBlank ;
        GUCR_CHANNEL * pReferencedChannel = pColorant->pOwningChannel;
        GUCR_COLORANT * pReferencedColorant = pColorant->pNextColorant;

        gucs_initColorant(pColorant);

        pColorant->info.name = pColorantName;
        pColorant->info.colorantIndex = * pCi;
        if (! BACKDROP_RASTERSTYLE(pRasterStyle)) {
          pColorant->equivalentRealColorant.ci = pColorant->info.colorantIndex;
          pColorant->equivalentRealColorant.cimap[0] = pColorant->info.colorantIndex;
          HQASSERT(pColorant->equivalentRealColorant.cimap[1] == COLORANTINDEX_UNKNOWN,
                   "cimap[1] must always COLORANTINDEX_UNKNOWN in this case");
          HQASSERT(! pColorant->equivalentRealColorant.recipe,
                   "recipe flag should be false");
        }
        pColorant->info.offsetX = x;
        pColorant->info.offsetY = y;
        pColorant->info.nRenderingProperties = nRenderingProperties;
        pColorant->info.fAutomatic = (uint8)fAutomatic;
        pColorant->info.specialHandling = specialHandling;
        pColorant->info.fOverrideScreenAngle = (uint8) fOverrideScreenAngle;
        pColorant->info.screenAngle = screenAngle;
        pColorant->info.neutralDensity = neutralDensity;
        pColorant->pOwningChannel = pReferencedChannel;
        pColorant->pNextColorant = pReferencedColorant;

        pRasterStyle->fHaveEquivalent = FALSE ;
        if (gucs_colorantIsProcess(pRasterStyle, pColorant->info.name)) {
          pColorant->info.colorantType = COLORANTTYPE_PROCESS ;
          fOmitIfBlank = pRasterStyle->fOmitProcess ;
        } else {
          pColorant->info.colorantType = COLORANTTYPE_EXTRASPOT ;
          fOmitIfBlank = pRasterStyle->fOmitExtraSpot ;
        }
        if ( guc_colorantInSet(pRasterStyle->pColorantOmits, *pCi) ) {
          if (! guc_addColorantToSet(pRasterStyle->dlpools,
                                     &pRasterStyle->pCurrentOmits, *pCi))
            return FALSE; /* Named Omit */
        } else if ( fOmitIfBlank ) {
          if ( !guc_addColorantToSet(pRasterStyle->dlpools,
                                     &pRasterStyle->pCurrentOmits, *pCi))
            return FALSE;
        }
        gucr_setColorantColor(pColorant, &pRasterStyle->osRGB) ;
      }
      break;
    }

    nNew--;
  }

  if (fBackground)
    if (! guc_colorantSetBackgroundSeparation(pRasterStyle, *pCi) )
      return FALSE;

  return TRUE;
}

/** A value for nRelativeToFrame in
   guc_colorantIndexPossiblyNewFrame can be derived from a colorant index (and
   indirectly, using the existing guc_colorantIndex, from a name) using
   guc_frameRelation. This function finds the first occurrence of the colorant index
   and returns the appropriate frame number for use as nRelativeToFrame. In this way
   one can say "insert separation after Cyan" or "add a new instance of Magenta on
   the same sheet as it already exists on". The routine will return the unique value
   GUC_RELATIVE_TO_FRAME_UNKNOWN if the colorant cannot be found. */
int32 guc_frameRelation(const GUCR_RASTERSTYLE *pRasterStyle, COLORANTINDEX ci)
{
  GUCR_SHEET * pSheet;
  GUCR_CHANNEL * pChannel;
  GUCR_COLORANT * pColorant;
  int32 nFrameRelation;
  Bool fIncrementOnSheet;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(ci != COLORANTINDEX_NONE && ci != COLORANTINDEX_ALL && ci != COLORANTINDEX_UNKNOWN,
            "COLORANTINDEX_NONE, ALL, and UNKNOWN not allowed in guc_frameRelation");

  fIncrementOnSheet =
    pRasterStyle->nSeparationStyle == GUCR_SEPARATIONSTYLE_SEPARATIONS ||
    pRasterStyle->nSeparationStyle == GUCR_SEPARATIONSTYLE_COLORED_SEPARATIONS ||
    pRasterStyle->nSeparationStyle == GUCR_SEPARATIONSTYLE_PROGRESSIVES;

  nFrameRelation = 0;

  for (pSheet = pRasterStyle->pSheets; pSheet != NULL; pSheet = pSheet->pNextSheet) {
    for (pChannel = pSheet->pChannels; pChannel != NULL; pChannel = pChannel->pNextChannel) {
      for (pColorant = pChannel->pColorants;
           pColorant != NULL;
           pColorant = pColorant->pNextColorant) {
        if (pColorant->info.colorantIndex == ci)
          return nFrameRelation;
      }
      if (! fIncrementOnSheet)
        nFrameRelation++;
    }
    if (fIncrementOnSheet)
      nFrameRelation++;
  }

  return GUC_RELATIVE_TO_FRAME_UNKNOWN;
}

/* ---------------------------------------------------------------------- */
/** Mark the given colorant as a background separation; \c
    guc_colorantIsBackgroundSeparation answers the obvious question */
Bool guc_colorantSetBackgroundSeparation(GUCR_RASTERSTYLE *pRasterStyle,
                                         COLORANTINDEX ci)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  return guc_addColorantToSet(pRasterStyle->dlpools,
                              &pRasterStyle->pBackgroundFlags, ci) ;
}

/* ---------------------------------------------------------------------- */
Bool guc_colorantAnyBackgroundSeparations(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  /* Being a background separation is a property of the colorant. We keep an array of
     bits to represent this property, rather than tag each colorant
     individually. This also has the minor advantage that where there are no
     background separations, we need allocate no memory for it */

  return pRasterStyle->pBackgroundFlags != NULL ;
}

/* ---------------------------------------------------------------------- */
Bool guc_colorantIsBackgroundSeparation(const GUCR_RASTERSTYLE *pRasterStyle,
                                        COLORANTINDEX ci)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  /* Being a background separation is a property of the colorant. We keep an array of
     bits to represent this property, rather than tag each colorant
     individually. This also has the minor advantage that where there are no
     background separations, we need allocate no memory for it */

  return guc_colorantInSet(pRasterStyle->pBackgroundFlags, ci) ;
}

/* ---------------------------------------------------------------------- */
/** Ensure that the color frame specified has its sRGB
   and CMYK values set. If they are set already, then nothing is done. If not,
   a color is constructed and steps are taken in the following order of
   preference to find an equivalent RGB or CMYK color.

   1) The color is looked up in the /Roam /NamedColorOrder databases
   2) The color is looked up in a DeviceN Colorants dictionary
   3) The color is looked up in the CMYKCustomColors dictionary
   4) The tinttransform is used.

   If any of steps 1 through 3 are successful, this will result in
   setcolorspace being done with a new tinttransform, which will result in
   a further call to this function where the tinttransform will be used.

   It is assumed that the current colorspace is the process or separation space
   that gave rise to the color frame. */
Bool guc_setEquivalentColors(GS_COLORinfo *colorInfo,
                             GUCR_RASTERSTYLE* pRasterStyle,
                             int32 colorType,
                             COLORSPACE_ID colorSpaceId,
                             int32 nColorants,
                             COLORANTINDEX *pColorantIndexes,
                             OBJECT* PSColorSpace,
                             Bool usePSTintTransform)
{
  GUCR_SHEET * pSheet;
  GUCR_CHANNEL * pChannel;
  GUCR_COLORANT * pColorant;
  int32 i;
  Bool fHaveEquivalent = TRUE;
  Bool savedCalculatingColors = pRasterStyle->fCalculatingColors;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(nColorants > 0, "No colorants in colorspace?") ;
  HQASSERT((pColorantIndexes != NULL),
           "guc_setEquivalentColors: NULL colorant indices pointer");

  /* Avoid recursion, so gsc_roamRGBequiv_lookup() and gsc_stdCMYKequiv_lookup()
   * will never appear twice in the same call stack. If they did, the equivs
   * would be wrong because the colorInfo is deliberately different when
   * calculating sRGB equivs which means the CMYK equivs from the nested call
   * would 'stick'.
   */
  if (pRasterStyle->fCalculatingColors)
    return TRUE;
  pRasterStyle->fCalculatingColors = TRUE;

  if ( pRasterStyle->parent != NULL ) {
    /* Set equivalent colors for underlying raster style as well */
    int32 temp;
    Bool result;
    COLORANTINDEX* p_ci;
    mm_size_t mem_size;

    /* Set up array of colorantindices in terms of under rasterstyle and recurse */
    mem_size = nColorants*sizeof(COLORANTINDEX);
    p_ci = gucs_alloc(pRasterStyle->dlpools, mem_size, MM_ALLOC_CLASS_NCOLOR);
    if ( p_ci == NULL ) {
      pRasterStyle->fCalculatingColors = savedCalculatingColors;
      return error_handler(VMERROR);
    }
    result = gsc_colorspaceNamesToIndex(pRasterStyle->parent, PSColorSpace,
                                        FALSE, FALSE, p_ci, nColorants, colorInfo,
                                        &temp) &&
             guc_setEquivalentColors(colorInfo, pRasterStyle->parent, colorType,
                                     colorSpaceId, nColorants, p_ci,
                                     PSColorSpace, usePSTintTransform);
    gucs_free(pRasterStyle->dlpools, p_ci, mem_size, MM_ALLOC_CLASS_NCOLOR);
    if ( !result ) {
      pRasterStyle->fCalculatingColors = savedCalculatingColors;
      return FALSE;
    }
  }

  /* Optimise out loop if nothing has changed which needs it */
  if ( pRasterStyle->fHaveEquivalent ) {
    pRasterStyle->fCalculatingColors = savedCalculatingColors;
    return TRUE ;
  }

  /* for all sheets, for all channels, for all colorants, if colorant
     is this one, check its sRGB and CMYK values are valid and create if
     not. */
  for (pSheet = pRasterStyle->pSheets; pSheet != NULL; pSheet = pSheet->pNextSheet) {
    for (pChannel = pSheet->pChannels; pChannel != NULL; pChannel = pChannel->pNextChannel) {
      for (pColorant = pChannel->pColorants;
           pColorant != NULL;
           pColorant = pColorant->pNextColorant) {

        COLORANTINDEX ci = pColorant->info.colorantIndex ;

        if ( ci == COLORANTINDEX_NONE || ci == COLORANTINDEX_ALL ||
             ci == COLORANTINDEX_UNKNOWN )
          continue ;

        /* Find if it's in current colorspace set */
        for ( i = 0 ; i < nColorants ; ++i ) {
          if ( pColorantIndexes[i] == ci ) /* Found colorant in set */
            break ;
        }

        if ( i < nColorants ) {
          if (!BACKDROP_RASTERSTYLE(pRasterStyle)) {
            if ( pColorant->fRecalc || pColorant->info.sRGB[0] < 0.0f ) {

              if (!gsc_roamRGBequiv_lookup( colorInfo,
                                            pColorant->info.name,
                                            pColorant->info.sRGB,
                                            PSColorSpace,
                                            usePSTintTransform,
                                            i)) {
                pRasterStyle->fCalculatingColors = savedCalculatingColors;
                return FALSE;
              }
            }
          }
          else
            /* If the raster style is a backdrop we don't need roam equivs and
             * their calculation can be a performance drain, so set an equivalent
             * of black which should be obvious if we ever see it.
             */
            pColorant->info.sRGB[0] =
              pColorant->info.sRGB[1] = pColorant->info.sRGB[2] = 0.0;

          if ( pColorant->fRecalc || pColorant->info.CMYK[0] < 0.0f ) {

            if (!gsc_stdCMYKequiv_lookup( colorInfo,
                                          pColorant->info.name,
                                          pColorant->info.CMYK,
                                          PSColorSpace,
                                          usePSTintTransform,
                                          i)) {
              pRasterStyle->fCalculatingColors = savedCalculatingColors;
              return FALSE;
            }

            /* Test for Quark probe; Quark sets /Separation colorspace with tint
               transform to /DeviceCMYK {0.5 0.5 0.5 0} and then uses currentgray
               to divine whether the separation is being produced or not. If it is,
               we need to recalculate the equivalent colors when the real
               colorspace is set up.

               Note: if an (invalid) empty tint transform is provided
               (e.g. Illustrator 7 spot-to-spot blends), we also return these values
               so that we ignore them in the same way
            */
            if ( pColorant->info.CMYK[0] == 0.5 &&
                 pColorant->info.CMYK[1] == 0.5 &&
                 pColorant->info.CMYK[2] == 0.5 &&
                 pColorant->info.CMYK[3] == 0.0 ) {
              pColorant->fRecalc = TRUE ;
              fHaveEquivalent = FALSE;
            }
            else
              pColorant->fRecalc = FALSE;
          }
        }
        else if (pColorant->fRecalc) {

          /* This colorant wasn't named in pColorantIndexes so we didn't have
           * a chance to derive equivalent colors in this call. So if this
           * colorant hasn't yet got good equivalents, switch off the short
           * circuiting of this function.
           */
          if ( pColorant->info.sRGB[0] < 0.0f || pColorant->info.CMYK[0] < 0.0f )
            fHaveEquivalent = FALSE ;
        }
      }
    }
  }

  /* Base on local value in case a FALSE in the pRasterStyle has been
     overwritten when finding equivalents for a later colorant */
  if (fHaveEquivalent)
    pRasterStyle->fHaveEquivalent = TRUE ;

  pRasterStyle->fCalculatingColors = savedCalculatingColors;

  return TRUE ;
}

Bool guc_setCMYKEquivalents(GUCR_RASTERSTYLE* pRasterStyle,
                            COLORANTINDEX ci, EQUIVCOLOR equiv)
{
  GUCR_COLORANT* pColorant;

  gucr_colorantHandle(pRasterStyle, ci, & pColorant);
  if (pColorant == NULL) {
    HQFAIL("Didn't find the colorant to set CMYK equivalents");
    return error_handler(UNREGISTERED);
  }

  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;

  pColorant->info.CMYK[0] = equiv[0];
  pColorant->info.CMYK[1] = equiv[1];
  pColorant->info.CMYK[2] = equiv[2];
  pColorant->info.CMYK[3] = equiv[3];

  /* Leave RGB equivalents alone, either valid or invalid. */

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Accessor for the CMYK equivalent values for the given colorant index.
   These CMYK values are used to build a tint transform to map a DeviceN
   to a CMYK alternate color space.  Initially this is for backdrop
   flavoured raster styles. */
NAMECACHE* guc_getCMYKEquivalents(GUCR_RASTERSTYLE* pRasterStyle,
                                  COLORANTINDEX ci,
                                  EQUIVCOLOR **equiv,
                                  void *private_data)
{
  GUCR_COLORANT* pColorant;

  UNUSED_PARAM( void *, private_data );

  *equiv = NULL;

  gucr_colorantHandle(pRasterStyle, ci, & pColorant);
  if (pColorant == NULL)
    return NULL;

  VERIFY_OBJECT(pColorant, COLORANT_NAME) ;

  if (pColorant->info.CMYK[0] == -1.0f || pColorant->info.CMYK[1] == -1.0f ||
      pColorant->info.CMYK[2] == -1.0f || pColorant->info.CMYK[3] == -1.0f) {
    return NULL;
  }

  *equiv = &pColorant->info.CMYK;

  HQASSERT(pColorant->info.name != NULL, "pColorant->info.name is null");
  return pColorant->info.name;
}

/* ---------------------------------------------------------------------- */
/** Change the color name of all colorants with name
    colName to that of sepName */
Bool guc_overrideColorantName(GUCR_RASTERSTYLE *pRasterStyle,
                              const NAMECACHE *colName,
                              NAMECACHE *sepName)
{
  GUCR_SHEET * pSheet;
  GUCR_CHANNEL * pChannel;
  GUCR_COLORANT * pColorant;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  /* for all sheets, for all channels, for all colorants... */

  for (pSheet = pRasterStyle->pSheets; pSheet != NULL;
       pSheet = pSheet->pNextSheet) {
    for (pChannel = pSheet->pChannels; pChannel != NULL;
         pChannel = pChannel->pNextChannel) {
      for (pColorant = pChannel->pColorants; pColorant != NULL;
           pColorant = pColorant->pNextColorant) {
        if ( (pColorant->info.originalName == NULL &&
              pColorant->info.name == colName) ||
             (pColorant->info.originalName != NULL &&
              pColorant->info.originalName == colName) ) {
          if ( pColorant->info.originalName == NULL &&
               pColorant->info.name != sepName ) {

            pColorant->info.originalName = pColorant->info.name ;

            /* also set up new equivalent CMYK and sRGB values for roam */
            if ( !guc_overrideEquivalentColors( pSheet, pColorant, sepName ) )
              return FALSE;
          }
          pColorant->info.name = sepName ;
          if ( pColorant->info.originalName != NULL &&
               pColorant->info.originalName == sepName )
            pColorant->info.originalName = NULL ;
          gucr_setColorantColor( pColorant , & pRasterStyle->osRGB ) ;
        }
      }
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Do setcolorspace using the separation
    name passed into it.  Where necessary this creates a new sheet
    containing the correct CMYK and sRGB equivalent colors for roam.
    These values are then copied back into the colorant passed in (as
    we expect the new sheet to be discarded soon).  */
static Bool guc_overrideEquivalentColors(GUCR_SHEET * pSheet,
                                         GUCR_COLORANT * pColorant,
                                         NAMECACHE *sepName)
{
  ps_context_t *pscontext = CoreContext.pscontext ;
  OBJECT theo = OBJECT_NOTVM_NOTHING;
  int32 i ;
  GUCR_SHEET *pNewSheet;
  GUCR_CHANNEL *pNewChannel;
  GUCR_COLORANT * pNewColorant;
  Bool result = FALSE;
  int32 stacksize;
  Bool sheetFound = FALSE;

  HQASSERT(pSheet != NULL, "null sheet in guc_overrideEquivalentColors");
  HQASSERT(pColorant != NULL, "null colorant in guc_overrideEquivalentColors");
  HQASSERT(sepName != NULL, "null separation name in guc_overrideEquivalentColors");

  /* use the separation name to find out equivalent CMYK and sRGB values for roam */
  stacksize = theStackSize( operandstack );

  theTags(theo) = ONAME | LITERAL;
  oName(theo) = sepName ;

  if ( !push( &theo, &operandstack ) ) {
    return FALSE;
  }

  if ( !gsave_(pscontext)) {
    pop( &operandstack );
    return FALSE ;
  }

  /* default is black */
  result = run_ps_string((uint8*)"[ /Separation 3 -1 roll /DeviceCMYK {pop 0 0 0 1} ]setcolorspace") ;

  if ( !grestore_(pscontext) ) {
    return FALSE ;
  }

  if ( !result ) {
    HQTRACE(TRUE, ( "Unable to set equivalent colors for %.*s separation",
                    sepName->len, sepName->clist) );
    return FALSE ;
  }


  /* All seems well but check that the stack size is unaltered */
  if (theStackSize( operandstack ) < stacksize) {
    HQFAIL("guc_overrideColorantName reduced the stacksize");
    return error_handler(STACKUNDERFLOW);
  }
  else {
    if ( theStackSize( operandstack ) != stacksize ) {
      HQFAIL( "guc_overrideColorantName left wrong number of objects on stack" );
      return error_handler( UNDEFINED );
    }
  }

  /* In the case of a spot color this should have created an extra sheet with the same
     name as the separation but with the equivalent CMYK and sRGB values set up for roam.
     As any new sheet will soon be discarded, need to copy the equivalent values back
     into the current sheet. */

  for (pNewSheet = pSheet->pNextSheet;
       !sheetFound && pNewSheet != NULL ;
       pNewSheet = pNewSheet->pNextSheet ) {
    for (pNewChannel = pNewSheet->pChannels;
         !sheetFound && pNewChannel != NULL ;
         pNewChannel = pNewChannel->pNextChannel ) {
      for (pNewColorant = pNewChannel->pColorants;
           pNewColorant != NULL;
           pNewColorant = pNewColorant->pNextColorant) {

        if (pNewColorant->info.name == sepName) {
          sheetFound = TRUE;

          for ( i = 0; i < 4; ++i) {
            pColorant->info.CMYK[i] = pNewColorant->info.CMYK[i];

            if (i < 3) {
              pColorant->info.sRGB[i] = pNewColorant->info.sRGB[i];
            }
          }
          break;
        }
      }
    }
  }

  return TRUE;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
Bool guc_abortSeparations(const GUCR_RASTERSTYLE *pRasterStyle,
                          Bool fIsSeparations)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  if ( pRasterStyle->fAbortSeparations ) {
    if ( fIsSeparations )
      monitorf(UVS("Error: Aborting separation of pre-separated job\n"));
    else
      monitorf(UVS("Error: Aborting separation of Gray(monochrome) job\n"));
    return error_handler( CONFIGURATIONERROR ) ;
  }

  return TRUE ;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
Bool guc_omitSeparations(GUCR_RASTERSTYLE *pRasterStyle,
                         Bool fIsSeparations)
{
  GUCR_SHEET * pSheet;
  GUCR_CHANNEL * pChannel;
  GUCR_COLORANT * pColorant;
  NAMECACHE *pNameOutput ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(pRasterStyle->nSeparationStyle == GUCR_SEPARATIONSTYLE_SEPARATIONS ,
            "can only omit separations in GUCR_SEPARATIONSTYLE_SEPARATIONS mode");

  if ( ( fIsSeparations && !pRasterStyle->fOmitSeparations) ||
       (!fIsSeparations && !pRasterStyle->fOmitMonochrome) ) {
    /* Need to reset any changes we may have made to 'Black'. */
    NAMECACHE *colName ;
    colName = pRasterStyle->processColorModel == DEVICESPACE_Gray ?
              system_names + NAME_Gray :
              system_names + NAME_Black ;

    return guc_overrideColorantName( pRasterStyle , colName , colName ) ;
  }

  if ( pRasterStyle->nSeparationStyle != GUCR_SEPARATIONSTYLE_SEPARATIONS )
    return TRUE;

  /* Can't ignore non-black separations, they may be required for knockouts
     (e.g. with ContoneMask and HVD). */
  if ( !pRasterStyle->omitDetails.knockouts )
    return TRUE;

  if ( fIsSeparations )
    monitorf(UVS("Warning: Detected Pre-Separated page when auto-separating.\n")) ;
  else
    monitorf(UVS("Warning: Detected Gray page when auto-separating.\n")) ;

  /* for all sheets, for all channels, for all colorants... */

  pNameOutput = NULL ;
  for (pSheet = pRasterStyle->pSheets; pSheet != NULL;
       pSheet = pSheet->pNextSheet) {
    uint8 fOmitSheet = TRUE ;
    for (pChannel = pSheet->pChannels; pChannel != NULL;
         pChannel = pChannel->pNextChannel) {
      uint8 fOmitChannel = TRUE ;
      for (pColorant = pChannel->pColorants; pColorant != NULL;
           pColorant = pColorant->pNextColorant) {
        if ( pColorant->info.originalName == NULL ?
             pColorant->info.name != system_names + NAME_Black :
             pColorant->info.originalName != system_names + NAME_Black )
          pColorant->fRenderOmit = TRUE ;
        else {
          fOmitSheet = FALSE ;
          fOmitChannel = FALSE ;
          if ( ! fIsSeparations ) {
            pColorant->info.name = system_names + NAME_Gray ;
            pColorant->info.originalName = system_names + NAME_Black ;
          }
          if ( pNameOutput == NULL )
            pNameOutput = pColorant->info.name ;
        }
      }
      pChannel->fRenderOmit = fOmitChannel ;
    }
    pSheet->fRenderOmit = fOmitSheet ;
  }

  if ( pNameOutput != NULL ) {
    if ( fIsSeparations )
      monitorf(UVM("     Only producing %.*s separation.\n") ,
               theINLen(pNameOutput), theICList(pNameOutput)) ;
    else
      monitorf(UVM("     Only producing %.*s page.\n") ,
               theINLen(pNameOutput), theICList(pNameOutput)) ;
  } else {
    /* There is nothing left, because a black separation is not being
       produced, and pre-separated or gray pages are black by definition. */
    monitorf(UVS("     Omitting page (no black colorant).\n") ) ;
  }

  return TRUE;
}

/** Blank separation omission.
   guc_omitDetails returns an immutable pointer to the rasterstyle's omit
   details structure. */
const OMIT_DETAILS *guc_omitDetails(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  return &pRasterStyle->omitDetails ;
}

void guc_omitSetIgnoreKnockouts(GUCR_RASTERSTYLE *pRasterStyle,
                                Bool ignore_knockouts)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");
  pRasterStyle->omitDetails.knockouts = (uint8)ignore_knockouts;
}

/** Test to see if any
   separations might be omitted. It only determines whether separations were
   added to the omit set while setting up the raster style, not whether any
   separations are currently being omitted.
*/
Bool guc_fOmitBlankSeparations(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;

  return pRasterStyle->pCurrentOmits && pRasterStyle->pCurrentOmits->nSet > 0 ;
}

/** Note that a separation is not blank. */
Bool guc_dontOmitSeparation(GUCR_RASTERSTYLE *pRasterStyle, COLORANTINDEX ci)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  guc_removeColorantFromSet(pRasterStyle->pCurrentOmits, ci) ;

  return pRasterStyle->pCurrentOmits && pRasterStyle->pCurrentOmits->nSet > 0 ;
}

/** Test whether a separation is currently being omitted. */
Bool guc_fOmittingSeparation(const GUCR_RASTERSTYLE *pRasterStyle, COLORANTINDEX ci)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  return guc_colorantInSet(pRasterStyle->pCurrentOmits, ci) ;
}

Bool guc_saveOmitSeparations(GUCR_RASTERSTYLE *pRasterStyle,
                             GUCR_COLORANTSET **hsave)
{
  GUCR_COLORANTSET *pColorantSetNew = NULL ;
  GUCR_COLORANTSET *pColorantSetOld = pRasterStyle->pCurrentOmits ;
  int32 i ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");
  HQASSERT(hsave, "NULL save handle parameter in guc_saveOmitSeparations") ;

  if ( pColorantSetOld != NULL ) {
    pColorantSetNew = gucs_alloc(pRasterStyle->dlpools,
                                 sizeof(GUCR_COLORANTSET) +
                                 pColorantSetOld->cWords * sizeof(uint32),
                                 MM_ALLOC_CLASS_GUC_COLORANTSET) ;
    if (pColorantSetNew == NULL)
      return error_handler( VMERROR ) ;

    pColorantSetNew->cWords = pColorantSetOld->cWords ;
    pColorantSetNew->nSet = pColorantSetOld->nSet ;

    for ( i = 0 ; i <= pColorantSetOld->cWords ; ++i )
      pColorantSetNew->afMember[i] = pColorantSetOld->afMember[i];
  }

  *hsave = (GUCR_COLORANTSET*)pColorantSetNew ;

  return TRUE ;
}

Bool guc_restoreOmitSeparations(GUCR_RASTERSTYLE *pRasterStyle,
                                GUCR_COLORANTSET* pColorantSet,
                                Bool revert)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  if ( revert ) {
    GUCR_COLORANTSET *cs = pRasterStyle->pCurrentOmits ;
    pRasterStyle->pCurrentOmits = pColorantSet ;
    pColorantSet = cs ;
  }

  if ( pColorantSet != NULL )
    gucs_free(pRasterStyle->dlpools, (mm_addr_t)pColorantSet,
              sizeof(GUCR_COLORANTSET) + pColorantSet->cWords * sizeof(uint32),
              MM_ALLOC_CLASS_GUC_COLORANTSET);

  return pRasterStyle->pCurrentOmits && pRasterStyle->pCurrentOmits->nSet > 0 ;
}

/** Transfer colorant flags to the raster style structure. Note, this function
    is called only on the final render because the pagebuffer device is
    incapable of dealing with the changes between partial and final render when
    separation omission is enabled. */
Bool guc_markBlankSeparations(GUCR_RASTERSTYLE *pRasterStyle,
                              Bool rippedtodisk)
{
  GUCR_SHEET *pSheet;
  GUCR_CHANNEL *pChannel;
  GUCR_COLORANT *pColorant;
  Bool fOmitted = FALSE ;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  if (pRasterStyle->pCurrentOmits == NULL ||
      pRasterStyle->pCurrentOmits->nSet == 0) {
    return (TRUE);
  }

  for (pSheet = pRasterStyle->pSheets; pSheet != NULL;
       pSheet = pSheet->pNextSheet) {
    if ( ! pSheet->fRenderOmit ) {
      uint8 fOmitSheet = TRUE ;
      int32 cOmitted = 0;
      uint32 cRequiredBlank = 0;
      NAMECACHE *pNameOmitted = NULL;

      /*
       * First we scan all colorants for all channels for blank
       * ones (and unknown ones) and mark the channel as potential
       * for omitting if all colorants are blank.
       */
      for ( pChannel = pSheet->pChannels; pChannel != NULL;
            pChannel = pChannel->pNextChannel ) {

        if ( !pChannel->fRenderOmit ) {
          uint8 fOmitChannel = TRUE ;

          pChannel->fBlankColorants = FALSE;

          for (pColorant = pChannel->pColorants; pColorant != NULL;
               pColorant = pColorant->pNextColorant ) {
            COLORANTINDEX ci = pColorant->info.colorantIndex;

            if ( ci == COLORANTINDEX_UNKNOWN ) {
              pColorant->fRenderOmit = TRUE;
            }
            else if (pRasterStyle->pCurrentOmits != NULL &&
                     guc_colorantInSet(pRasterStyle->pCurrentOmits, ci)) {
              /* Colorant detected blank */
              pColorant->fRenderOmit = TRUE;
              pChannel->fBlankColorants = TRUE;
              cOmitted++;
              /* This is only used when we are going to a separating device and
                 the separation is detected blank and will be omitted
                 See guc_omitSeparations() for how channels and colorants
                 are setup for omission when separating */
              pNameOmitted = pColorant->info.name;
            }

            if ( !pColorant->fRenderOmit ) {
              fOmitChannel = FALSE;
              fOmitSheet = FALSE;
            }
          }

          pChannel->fRenderOmit = fOmitChannel;

          /** \todo @@@ TODO FIXME ajcd 2004-06-08: The
              fRequiredByPartialPaint flag is a temporary hack because the
              pagebuffer device is incapable of dealing with the changes
              between partial and final render when separation omission is
              enabled. It should be removed when a properly enabled partial
              paint store is created. */
          if ( !pChannel->fRequired && rippedtodisk &&
               pRasterStyle->nInterleavingStyle != GUCR_INTERLEAVINGSTYLE_MONO ) {
            /* Already written out this channel, cannot ignore it */
            pChannel->fRequired = TRUE ;
            pChannel->fRequiredByPartialPaint = TRUE ;
            pSheet->cRequired++ ;
          }

          if ( fOmitChannel && pChannel->fRequired ) {
            cRequiredBlank++;
          }
        }
      }

      HQASSERT((cRequiredBlank <= pSheet->cRequired),
               "counted more required channels than expected");

      if ( cRequiredBlank > 0 && pSheet->cRequired > cRequiredBlank ) {
        /* If not all required channels are blank then cannot omit any of them
           or their colorants */
        for ( pChannel = pSheet->pChannels; pChannel != NULL;
              pChannel = pChannel->pNextChannel ) {
          if ( pChannel->fRequired ) {
            pChannel->fRenderOmit = FALSE;
          }
        }
        fOmitSheet = FALSE;
      }

      if ( cOmitted > 0 ) {
        if ( !fOmitted ) {
          /* Say found blanks colorants first time round only */
          fOmitted = TRUE ;
          monitorf(UVS("Warning: Detected blank colorant(s).\n") ) ;
        }

        if ( fOmitSheet ) {
          if ( cOmitted > 1 ) {
            monitorf(UVS("     Omitting page.\n") ) ;

          } else {
            HQASSERT((pNameOmitted != NULL),
                     "name of omitted separation not setup");
            monitorf(UVM("     Omitting %.*s separation.\n") ,
                     theINLen(pNameOmitted),
                     theICList(pNameOmitted)) ;
          }

        } else {
          /* Loop over all channels on sheet */
          for ( pChannel = pSheet->pChannels;
                pChannel != NULL;
                pChannel = pChannel->pNextChannel) {
            if ( pChannel->fRenderOmit || pChannel->fBlankColorants ) {
              /* Found a channel to drop or has one or more blank
                 colorants to report */
              for ( pColorant = pChannel->pColorants;
                    pColorant != NULL;
                    pColorant = pColorant->pNextColorant) {
                if ( (pColorant->info.colorantIndex != COLORANTINDEX_UNKNOWN) &&
                     pColorant->fRenderOmit ) {
                  /* Output colorant name */
                  monitorf(UVM("     Omitting %.*s colorant.\n") ,
                           theINLen(pColorant->info.name),
                           theICList(pColorant->info.name)) ;
                }
              }
            }
          }
        }
      }
      pSheet->fRenderOmit = fOmitSheet ;
    }
  }

  return TRUE ;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
Bool guc_resetOmitSeparations(GUCR_RASTERSTYLE *pRasterStyle)
{
  GUCR_SHEET * pSheet;
  GUCR_CHANNEL * pChannel;
  GUCR_COLORANT * pColorant;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  /* for all sheets, for all channels, for all colorants... */

  /* Initialise omit colorant set with all seps being omitted if blank. */

  for (pSheet = pRasterStyle->pSheets; pSheet != NULL;
       pSheet = pSheet->pNextSheet) {
    for (pChannel = pSheet->pChannels; pChannel != NULL;
         pChannel = pChannel->pNextChannel) {
      /** \todo @@@ TODO FIXME ajcd 2004-06-08: The fRequiredByPartialPaint
         flag is a temporary hack because the pagebuffer device is incapable
         of dealing with the changes between partial and final render when
         separation omission is enabled. It should be removed when a properly
         enabled partial paint store is created. */
      if ( pChannel->fRequiredByPartialPaint ) {
        pChannel->fRequiredByPartialPaint = FALSE ;
        pChannel->fRequired = FALSE ;
        pSheet->cRequired-- ;
      }

      for (pColorant = pChannel->pColorants; pColorant != NULL;
           pColorant = pColorant->pNextColorant) {
        COLORANTINDEX ci = pColorant->info.colorantIndex ;

        pColorant->fRenderOmit = FALSE ;

        if ( ci != COLORANTINDEX_UNKNOWN ) {
          if ( guc_colorantInSet(pRasterStyle->pColorantOmits, ci) ) {
            if (! guc_addColorantToSet(pRasterStyle->dlpools,
                                       &pRasterStyle->pCurrentOmits, ci))
              return FALSE;
          } else {
            guc_removeColorantFromSet(pRasterStyle->pCurrentOmits, ci) ;
          }
        }
      }
      pChannel->fRenderOmit = FALSE ;
    }
    pSheet->fRenderOmit = FALSE ;
  }

  return TRUE;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
void guc_setRasterStyleBandSize(GUCR_RASTERSTYLE *pRasterStyle,
                                int32 nBandSize)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  HQASSERT( nBandSize > 0 , "funny nBandSize; should be > 0" ) ;

  pRasterStyle->nBandSize = nBandSize ;
}

/** Iterate over the colorants in the passed raster style, returning the highest
colorant index used.

If 'includeOmitted' is true, all colorant indices will be checked; otherwise
only the colorants which are not marked as being omitted will be checked.

This function will return -1 if no colorants are present, or no renderable
colorants are present and 'includeOmitted' is false.
*/
uint32 guc_getHighestColorantIndexInRasterStyle(GUCR_RASTERSTYLE *pRasterStyle,
                                                Bool includeOmitted)
{
  GUCR_CHANNEL* channel;
  GUCR_COLORANT* colorant;
  const GUCR_COLORANT_INFO *info = NULL;
  int32 maxIndex = -1;

  /* Iterate over all colorants in the frames and channels in the raster style. */
  for (channel = gucr_framesStart(pRasterStyle);
       gucr_framesMore(channel);
       gucr_framesNext(&channel)) {
    for (colorant = gucr_colorantsStart(channel);
         gucr_colorantsMore(colorant, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext(&colorant)) {

      /* Get the colorant info. This function will return FALSE if the colorant
      is not renderable, or unknown, in which case we don't add it to the list. */
      if (includeOmitted || gucr_colorantDescription(colorant, &info)) {
        if (info->colorantIndex != COLORANTINDEX_UNKNOWN &&
            info->colorantIndex > maxIndex)
          maxIndex = info->colorantIndex;
      }
    }
  }
  return maxIndex;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
void guc_positionSeparations(GUCR_RASTERSTYLE *pRasterStyle, int32 fUseRenderOmit )
{
  int32 nBandSize ;
  int32 nMaxOffsetIntoBand = 0 ;
  GUCR_SHEET * pSheet;
  GUCR_CHANNEL * pChannel;
  GUCR_COLORANT * pColorant;

  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  /* for all sheets, for all channels, for all colorants... */

  nBandSize = pRasterStyle->nBandSize ;

  for (pSheet = pRasterStyle->pSheets; pSheet != NULL; pSheet = pSheet->pNextSheet) {
    if ( ! fUseRenderOmit ||
         ! pSheet->fRenderOmit ) {
      for (pChannel = pSheet->pChannels; pChannel != NULL;
           pChannel = pChannel->pNextChannel) {
        if ( ! fUseRenderOmit || ! pChannel->fRenderOmit ) {
          for (pColorant = pChannel->pColorants; pColorant != NULL;
               pColorant = pColorant->pNextColorant) {
            if ( ! fUseRenderOmit || ! pColorant->fRenderOmit ) {
              int32 nOffsetY ;
              int32 nOffsetInBand ;
              int32 nOffsetWholeBands ;
              if (( nOffsetY = pColorant->info.offsetY ) != 0 ) {
                if ( nOffsetY < 0 ) {
                  nOffsetWholeBands = nOffsetY / nBandSize ; /* i.e. nearer zero */
                  nOffsetInBand = nOffsetWholeBands * nBandSize - nOffsetY ;
                }
                else {
                  nOffsetWholeBands = ( nOffsetY - 1 ) / nBandSize + 1 ;
                  nOffsetInBand = nOffsetWholeBands * nBandSize - nOffsetY ;
                }
                if ( nOffsetInBand > nMaxOffsetIntoBand )
                  nMaxOffsetIntoBand = nOffsetInBand ;
              }
              else {
                nOffsetWholeBands = 0 ;
              }
              pColorant->info.offsetBand = nOffsetWholeBands ;
            }
          }
        }
      }
    }
  }
  pRasterStyle->nMaxOffsetIntoBand = nMaxOffsetIntoBand ;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
int32 guc_getMaxOffsetIntoBand(const GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  return pRasterStyle->nMaxOffsetIntoBand ;
}

OBJECT *gucs_fullyFledgedColorants(GUCR_RASTERSTYLE *pRasterStyle)
{
  VERIFY_OBJECT(pRasterStyle, RASTERSTYLE_NAME) ;
  HQASSERT(!BACKDROP_RASTERSTYLE(pRasterStyle),
           "Expected real device raster style");

  return &pRasterStyle->oFullyFledgedColorants ;
}


/** Returns the photoinkInfo for pRasterStyle. */
const GUCR_PHOTOINK_INFO *guc_photoinkInfo(const GUCR_RASTERSTYLE *pRasterStyle)
{
  return pRasterStyle->photoinkInfo;
}


/** Fill in the next channel in a blit colormap. */
static Bool blit_colormap_channel(blit_colormap_t *map,
                                  const GUCR_RASTERSTYLE *rasterstyle,
                                  COLORANTINDEX ci,
                                  const GUCR_COLORANT *colorant,
                                  channel_output_t channel_max,
                                  Bool doing_rle,
                                  Bool mask,
                                  Bool compositing)
{
  channel_index_t nchannels ;

  HQASSERT(map, "No blit colormap to add colorant to") ;
  HQASSERT(compositing || colorant != NULL, "Must have colorant if not compositing") ;

  nchannels = map->nchannels ;
  if ( nchannels >= map->alloced_channels )
    return detail_error_handler(CONFIGURATIONERROR,
                                "Too many channels in pixel-interleaved render setup.") ;

  if ( ci == COLORANTINDEX_ALL ) {
    HQASSERT(map->all_index == BLIT_MAX_CHANNELS,
             "/All colorant already mapped in blit map") ;
    map->all_index = nchannels ;
  } else {
    HQASSERT(ci == COLORANTINDEX_UNKNOWN ||
             (ci >= 0 && ci <= rasterstyle->ciMax),
             "Invalid colorant index in raster style") ;
  }

  if ( (colorant != NULL && colorant->fRenderOmit) ||
       ci == COLORANTINDEX_UNKNOWN ) {
    /* The channel is either omitted, or is unmapped because we're in a setup
       that has a required channel but omits this colorant. This can happen
       with progressive or colored separations on a device with fixed
       channels. */
    map->channel[nchannels].ci = COLORANTINDEX_UNKNOWN ;
  } else {
    BITVECTOR_SET(map->rendered, nchannels) ;
    map->nrendered += 1 ;
    map->channel[nchannels].ci = ci ;

    /* For now, we assume that all channels appearing in the rasterstyle are
        colorants. The condition should hopefully handle this, it depends on
        what we do with type channels: */
    if ( ci == COLORANTINDEX_ALL || ci >= 0 )
      map->ncolors += 1 ;
  }

  if ( rasterstyle->processColorModel == DEVICESPACE_RGB || compositing ) {
    /* RGB and compositing are additive */
    map->channel[nchannels].pack_add = 0 ;
    map->channel[nchannels].pack_mul = 1 ;
  } else {
    map->channel[nchannels].pack_add = channel_max;
    map->channel[nchannels].pack_mul = -1 ; /* Invert for subtractive */
  }
  map->channel[nchannels].type = channel_is_color ;
  map->channel[nchannels].bit_offset = map->packed_bits ;
  map->channel[nchannels].bit_size = (unsigned int)gucr_ilog2(channel_max + 1);
  /* colorant_info may be null if a process colorant has been forced as part of
     a blend space.  However compositing doesn't require info. */
  map->channel[nchannels].colorant_info = colorant != NULL ? &colorant->info : NULL ;
  if ( mask ) {
    map->channel[nchannels].render_properties = RENDERING_PROPERTY_MASK_ALL ;
    map->apply_properties = TRUE ;
  } else {
    /* For compositing, render props are applied when rendering the backdrop. */
    map->channel[nchannels].render_properties = compositing
      ? RENDERING_PROPERTY_RENDER_ALL : colorant->info.nRenderingProperties ;
    if ( map->channel[nchannels].render_properties != RENDERING_PROPERTY_RENDER_ALL )
      map->apply_properties = TRUE ;
  }

  if ( doing_rle ) {
    /* Pack all RLE channels into 16 bits, for uniformity of handling. */
    map->channel[nchannels].bit_size = 16 ;
  }

  /** \todo ajcd 2008-10-10: There is actually no requirement that the packed
      bits for color channels are contiguous; they may have gaps between
      them. In future, this should become a property of the rasterstyle's
      colorant, or the blitmap should be passed in by the consumer. */
  map->packed_bits += map->channel[nchannels].bit_size ;
  map->nchannels = nchannels + 1 ;

  return TRUE ;
}


/* Centralise assert about "nchannels < BLIT_MAX_CHANNELS" so we can include
 * a test on alloced_channels too.
 */
static void check_max_channels(channel_index_t nchannels, blit_colormap_t *map)
{
  HQASSERT(nchannels < BLIT_MAX_CHANNELS, "Too many channels");

  if ( nchannels >= map->alloced_channels ) {
    monitorf((uint8 *)"Exceeding maximum num of expected channels %d %d\n",
      nchannels, map->alloced_channels);
    HQFAIL("Too many colorants");
  }
}

/* Create a color mapping for the current render channels, based on the
   current raster style. */
Bool blit_colormap_create(DL_STATE *page, blit_colormap_t **mapp,
                          mm_pool_t pool,
                          const surface_t *surface,
                          const GUCR_COLORANT *colorant,
                          int32 override_depth,
                          Bool append_type, Bool mask, Bool compositing,
                          Bool gather_all_colorants)
{
  blit_colormap_t *map ;
  GUCR_RASTERSTYLE *rasterstyle ;
  channel_index_t nchannels ;
  /** \todo ajcd 2008-08-25: For now, we only have one depth for the whole
      raster. This will change in future, to allow each channel to have a
      different depth. */
  size_t sz = sizeof(blit_colormap_t);
  channel_output_t channel_max;
  uint32 alloced_channels = BLIT_MAX_CHANNELS;

  HQASSERT(mapp, "Nowhere to put new blit color map") ;
  HQASSERT(surface, "No surface for new blit color map") ;

  VERIFY_OBJECT(colorant, COLORANT_NAME) ;
  HQASSERT(colorant->pOwningChannel != NULL &&
           colorant->pOwningChannel->pOwningSheet != NULL &&
           colorant->pOwningChannel->pOwningSheet->pOwningRasterStyle != NULL,
           "No owner for colorant");

  rasterstyle = colorant->pOwningChannel->pOwningSheet->pOwningRasterStyle ;
  VERIFY_OBJECT(rasterstyle, RASTERSTYLE_NAME) ;

  *mapp = NULL ;

  if ( rasterstyle->ciMax < 0 )
    return error_handler(UNREGISTERED) ;

  /* Work out an upper-estimate on the max number of colors to be used. This
     will allow us to reduce the size of the colormap allocated, thus saving
     memory which may be at a premium during a partial-paint. There are lots
     of different code paths, so just do it in the case that is typical and
     easiest to calculate. Can be no more that 8 channels in this case, so
     ensure just that amount of memory is allocated. */
  if ( !(gather_all_colorants || (compositing &&
         !BACKDROP_RASTERSTYLE(rasterstyle))) &&
       rasterstyle->nInterleavingStyle != GUCR_INTERLEAVINGSTYLE_PIXEL ) {
    alloced_channels = 8;
    sz = sizeof(*map) - sizeof(map->channel[0])*
                        (BLIT_MAX_CHANNELS - alloced_channels);
  }

  /* Allocate colormap with space for all colorant indices in the colormap.
   * There is a fixed size 256 element array within the colormap, most
   * of which is unused for typical jobs. So can cut this size down for
   * most cases and reduce partial-paint alloc sizes considerably.
   * Its safe to just not allocate the unused channels, as the code does
   * not touch them until the #channels gets that big.
   */
  if ( (map = mm_alloc_with_header(pool, sz,
                                   MM_ALLOC_CLASS_BLIT_COLORMAP)) == NULL )
    return error_handler(VMERROR) ;

  map->alloced_channels = alloced_channels;
  map->erase_color = NULL;
  map->knockout_color = NULL;
  map->type_lookup = rasterstyle->object_map_translation ;

  channel_max = CAST_SIGNED_TO_UINT16(rasterstyle->nValuesPerComponent - 1);

  /* override_depth is used by modular halftones to force 8/16-bit contone */
  /* 2^16 is used as a quick abbreviation for our pseudo 16-bit
     representation. */
  if ( override_depth == 65536 || compositing ) {
    map->override_htmax = channel_max = COLORVALUE_MAX;
  } else if ( override_depth != 0 ) {
    map->override_htmax = channel_max = CAST_SIGNED_TO_UINT16(override_depth - 1) ;
    HQASSERT((override_depth & channel_max) == 0,
             "Override depth was not a power of two") ;
  } else if ( gucr_halftoning(rasterstyle) ) {
    map->override_htmax = 0;
  } else {
    /** \todo ajcd 2008-11-14: When we allow different channel depths, this
        should be restricted to the override depth forced by modular
        halftoning, and not used for normal tone blits. */
    map->override_htmax = channel_max;
  }

  /* Type channel is the same width as the color channels. */
  map->type_htmax = (channel_max == COLORVALUE_MAX) ? 0xFFFF : channel_max;

  /* Iterate over channels. */
  map->nchannels = map->nrendered = map->ncolors = 0 ;
  map->packed_bits = 0 ;
  BITVECTOR_SET_ELEMENTS(map->rendered, BLIT_MAX_CHANNELS, BITVECTOR_ELEMENT_ZEROS) ;

  /* Initialise these to invalid indices at first, reset them at end of loop
     if not seen. */
  map->all_index = map->alpha_index = map->type_index = BLIT_MAX_CHANNELS ;

  /* Can optimise out property application unless we detect otherwise in channel
     iteration. */
  map->apply_properties = FALSE ;

  if ( gather_all_colorants
       || (compositing && !BACKDROP_RASTERSTYLE(rasterstyle)) ) {
    /* Create a colormap by picking out the unique non-rendered colorants from
       all sheets/frames/channels. Original added to help with
       trapping. Compositing may require all the process colorants. */
    Bool add_process = TRUE;
    GUCR_SHEET *sheet;

    /* If compositing in device space and the device space is equivalent to a
       standard blend space, force all colorants in the PCM to be included to
       allow non-separable blend modes during compositing. */
    if ( compositing && !BACKDROP_RASTERSTYLE(rasterstyle) &&
         (rasterstyle->processColorModel == DEVICESPACE_Gray ||
          rasterstyle->processColorModel == DEVICESPACE_RGB ||
          rasterstyle->processColorModel == DEVICESPACE_CMYK) ) {
      COLORANTINDEX pcmIndices[4];
      int32 i;

      if ( !guc_simpleDeviceColorSpaceMapping(rasterstyle,
                                              rasterstyle->processColorModel,
                                              pcmIndices,
                                              rasterstyle->nProcessColorants) ) {
        mm_free_with_header(pool, map) ;
        return FALSE ;
      }

      for ( i = 0; i < rasterstyle->nProcessColorants; ++i ) {
        if ( !blit_colormap_channel(map, rasterstyle, pcmIndices[i],
                                    NULL /* colorant handle not req */, channel_max,
                                    DOING_RUNLENGTH(page),
                                    mask, compositing) ) {
          mm_free_with_header(pool, map) ;
          return FALSE ;
        }
      }

      /* Process colorants have been added, now just add any spot colorants. */
      add_process = FALSE;
    }

    /* go through all the channels in all the sheets */
    for (sheet = rasterstyle->pSheets;
         sheet != NULL;
         sheet = sheet->pNextSheet) {
      GUCR_CHANNEL *channel;

      for (channel = sheet->pChannels;
           channel != NULL;
           channel = channel->pNextChannel ) {
        GUCR_COLORANT_INFO *info = &channel->pColorants->info ;

        /* Skip omitted channels, and omitted colorants in required channels.
           Skip duplicate colorants. These cases can happen when colored
           separations or progressives are selected with pixel interleaved
           styles: there will be multiple sheets, each with all channels, but
           only one (or the initial channels) set. Such a CMYK setup would
           have four sheets with CUUU, UMUU, UUYU, UUUK. We don't want to
           generate a blitmap with 16 channels in it! */
        if ( !channel->fRenderOmit &&
             (add_process || info->colorantType != COLORANTTYPE_PROCESS) ) {
          if ( info->colorantIndex != COLORANTINDEX_UNKNOWN ) {
            channel_index_t i ;

            /* Check for uniqueness. Progressives may have channels repeated:
               CUUU, CMUU, CMYU, CMYK. */
            /** \todo ajcd 2011-02-14: Is it possible to have a duplicate
                colorant with different special handling or neutral
                density? */
            for ( i = 0 ; i < map->nchannels ; ++i ) {
              const GUCR_COLORANT_INFO *oldinfo = map->channel[i].colorant_info ;
              /* Ignore process colorants forced in the colormap for compositing. */
              HQASSERT(oldinfo != NULL || compositing,
                       "Should have colorant info if colormap not for compositing");
              if ( oldinfo == NULL )
                continue;
              if ( info->colorantIndex == oldinfo->colorantIndex &&
                   info->nRenderingProperties == oldinfo->nRenderingProperties )
                break ;
            }

            if ( i == map->nchannels ) {
              if ( !blit_colormap_channel(map, rasterstyle, info->colorantIndex,
                                          channel->pColorants, channel_max,
                                          DOING_RUNLENGTH(page),
                                          mask, compositing) ) {
                mm_free_with_header(pool, map) ;
                return FALSE ;
              }
            }
          }
        }
      }
    }
  } else if ( rasterstyle->nInterleavingStyle == GUCR_INTERLEAVINGSTYLE_PIXEL ) {
    GUCR_CHANNEL *channel ;

    HQASSERT(gucr_colorantsStart(colorant->pOwningChannel) == colorant,
             "Handle not at start of colorants in pixel interleaving");

    for (channel = colorant->pOwningChannel;
         channel != NULL;
         channel = channel->pNextChannel) {
      /* Skip any omitted channels completely. Omitted colorants in required
         channels are preserved, but are given a colorant index of
         COLORANTINDEX_UNKNOWN. A mask of omitted colorants is built, so that
         it's easy to determine if there is anything to do for a blit
         channel. */
      if ( !channel->fRenderOmit ) {
        if ( !blit_colormap_channel(map, rasterstyle,
                                    channel->pColorants->info.colorantIndex,
                                    channel->pColorants,
                                    channel_max, DOING_RUNLENGTH(page),
                                    mask, compositing) ) {
          mm_free_with_header(pool, map) ;
          return FALSE ;
        }
      }
    }
  } else {
#if 0
    /** \todo ajcd 2008-11-12: The sole channel may be omitted if it was
         previously required for a partial paint, but for the final paint
         we've determined that it has no color applied. Hopefully this assert
         can be reinstated once the back-end rewrite is done. */
    HQASSERT(!colorant->fRenderOmit,
             "Shouldn't be calling this function for omitted band/frame/separation colorant") ;
#endif

    if ( !blit_colormap_channel(map, rasterstyle,
                                colorant->info.colorantIndex, colorant,
                                channel_max, DOING_RUNLENGTH(page),
                                mask, compositing) ) {
      mm_free_with_header(pool, map) ;
      return FALSE ;
    }
  }

  nchannels = map->nchannels ;

  /** \todo ajcd 2008-08-24: Integrate alpha channel directly into
      channel/colorant structure. Also need this for color RLE? */
  if ( compositing ) {
    check_max_channels(nchannels, map);
    map->alpha_index = nchannels ;
    BITVECTOR_SET(map->rendered, nchannels) ;
    map->nrendered += 1 ;
    map->channel[nchannels].ci = COLORANTINDEX_ALPHA ;
    map->channel[nchannels].pack_add = 0 ;
    map->channel[nchannels].pack_mul = 1 ;
    map->channel[nchannels].type = channel_is_alpha ;
    map->channel[nchannels].bit_offset = map->packed_bits ;
    map->channel[nchannels].bit_size = (unsigned int)gucr_ilog2(channel_max + 1);
    map->channel[nchannels].render_properties = RENDERING_PROPERTY_RENDER_ALL ;
    map->channel[nchannels].colorant_info = NULL ;

    map->packed_bits += map->channel[nchannels].bit_size ;
    ++nchannels;
  }

  /** \todo ajcd 2008-08-24: Integrate object type channel directly into
      channel/colorant structure. */
  if ( append_type ) {
    check_max_channels(nchannels, map);
    map->type_index = nchannels ;
    BITVECTOR_SET(map->rendered, nchannels) ;
    map->nrendered += 1 ;
    map->channel[nchannels].ci = COLORANTINDEX_UNKNOWN;
    map->channel[nchannels].pack_add = 0 ;
    map->channel[nchannels].pack_mul = 1 ;
    map->channel[nchannels].type = channel_is_type ;
    map->channel[nchannels].bit_offset = map->packed_bits ;
    map->channel[nchannels].bit_size = gucr_ilog2(map->type_htmax + 1);
    map->channel[nchannels].render_properties = RENDERING_PROPERTY_RENDER_ALL ;
    map->channel[nchannels].colorant_info = NULL ;

    map->packed_bits += map->channel[nchannels].bit_size ;
    ++nchannels;
  }

  check_max_channels(nchannels, map);
  HQASSERT(map->nrendered <= nchannels, "More channels rendered than exist") ;
  map->nchannels = nchannels ;
  map->expanded_bytes = map->packed_bits * BLIT_WIDTH_BITS / ugcd(map->packed_bits, BLIT_WIDTH_BITS) ;
  HQASSERT((map->expanded_bytes & 7) == 0, "Expanded size not a byte count") ;
  map->expanded_bytes >>= 3 ; /* Convert bits to bytes. */
  /* Don't expand the blit if the expanded size is bigger than the channel
     storage available. */
  if ( map->expanded_bytes > sizeof(blit_packed_t) )
    map->expanded_bytes = 0 ;

  /* Point unused All, alpha, and type channels at unused channel storage,
     beyond the range used for actual color, alpha, and type channels. */
  if ( map->all_index >= nchannels ) {
    check_max_channels(nchannels, map);
    map->all_index = nchannels ;
    map->channel[nchannels].ci = COLORANTINDEX_UNKNOWN ;
    map->channel[nchannels].pack_add = 0 ;
    map->channel[nchannels].pack_mul = 0 ;
    map->channel[nchannels].type = channel_is_special ;
    map->channel[nchannels].bit_offset = map->packed_bits ;
    map->channel[nchannels].bit_size = 0 ;
    map->channel[nchannels].render_properties = RENDERING_PROPERTY_IGNORE_ALL ;
    map->channel[nchannels].colorant_info = NULL ;
    ++nchannels ;
  }

  if ( map->alpha_index >= nchannels ) {
    check_max_channels(nchannels, map);
    map->alpha_index = nchannels ;
    map->channel[nchannels].ci = COLORANTINDEX_UNKNOWN ;
    map->channel[nchannels].pack_add = 0 ;
    map->channel[nchannels].pack_mul = 0 ;
    map->channel[nchannels].type = channel_is_special ;
    map->channel[nchannels].bit_offset = map->packed_bits ;
    map->channel[nchannels].bit_size = 0 ;
    map->channel[nchannels].render_properties = RENDERING_PROPERTY_IGNORE_ALL ;
    map->channel[nchannels].colorant_info = NULL ;
    ++nchannels ;
  }

  if ( map->type_index >= nchannels ) {
    check_max_channels(nchannels, map);
    map->type_index = nchannels ;
    map->channel[nchannels].ci = COLORANTINDEX_UNKNOWN ;
    map->channel[nchannels].pack_add = 0 ;
    map->channel[nchannels].pack_mul = 0 ;
    map->channel[nchannels].type = channel_is_special ;
    map->channel[nchannels].bit_offset = map->packed_bits ;
    map->channel[nchannels].bit_size = 0 ;
    map->channel[nchannels].render_properties = RENDERING_PROPERTY_IGNORE_ALL ;
    map->channel[nchannels].colorant_info = NULL ;
    ++nchannels ;
  }

  map->rasterstyle_id = guc_rasterstyleId(rasterstyle);

  /* Have a look at what we've constructed, and determine if we can optimise
     the color pack and expand methods. Unless otherwise indicated, use the
     generic blit pack routine. */
  map->pack_quantised_color = blit_color_pack_generic8 ;
  map->expand_packed_color = blit_color_expand_generic8 ;
  map->overprint_mask = blit_overprint_mask_generic8 ;

  NAME_OBJECT(map, BLIT_MAP_NAME) ;

  if ( surface->blit_colormap_optimise )
    (*surface->blit_colormap_optimise)(map) ;

  *mapp = map ;

  return TRUE ;
}


/* Destroy an existing color mapping. */
void blit_colormap_destroy(blit_colormap_t **mapp, mm_pool_t pool)
{
  blit_colormap_t *map ;

  HQASSERT(mapp != NULL, "No blit colormap stored") ;

  if ( (map = *mapp) != NULL ) {
    *mapp = NULL ;
    UNNAME_OBJECT(map) ;
    HQASSERT(map->rasterstyle_id != 0, "Destroying a mask color map");
    mm_free_with_header(pool, map);
  }
}


#if defined(DEBUG_BUILD)
#include "emit.h"
#include "psvm.h"

char *debug_get_interleavingstyle(int32 style)
{
  switch ( style ) {
  case GUCR_INTERLEAVINGSTYLE_MONO:
    return "Mono" ;
  case GUCR_INTERLEAVINGSTYLE_PIXEL:
    return "Pixel" ;
  case GUCR_INTERLEAVINGSTYLE_BAND:
    return "Band" ;
  case GUCR_INTERLEAVINGSTYLE_FRAME:
    return "Frame" ;
  default:
    return "Unknown" ;
  }
}

char *debug_get_separationstyle(int32 style)
{
  switch ( style ) {
  case GUCR_SEPARATIONSTYLE_MONOCHROME:
    return "Monochrome" ;
  case GUCR_SEPARATIONSTYLE_SEPARATIONS:
    return "Separations" ;
  case GUCR_SEPARATIONSTYLE_COLORED_SEPARATIONS:
    return "Colored Separations" ;
  case GUCR_SEPARATIONSTYLE_PROGRESSIVES:
    return "Progressives" ;
  case GUCR_SEPARATIONSTYLE_COMPOSITE:
    return "Composite" ;
  default:
    return "Unknown" ;
  }
}

char *debug_get_coloranttype(int32 type)
{
  switch ( type ) {
  case COLORANTTYPE_PROCESS:
    return "Process" ;
  case COLORANTTYPE_SPOT:
    return "Spot" ;
  case COLORANTTYPE_EXTRASPOT:
    return "ExtraSpot" ;
  default:
    return "Unknown" ;
  }
}

char *debug_get_devicespace(DEVICESPACEID id)
{
  switch ( id ) {
  case DEVICESPACE_Gray:
    return "DeviceGray" ;
  case DEVICESPACE_RGB:
    return "DeviceRGB" ;
  case DEVICESPACE_CMYK:
    return "DeviceCMYK" ;
  case DEVICESPACE_RGBK:
    return "DeviceRGBK" ;
  case DEVICESPACE_CMY:
    return "DeviceCMY" ;
  case DEVICESPACE_Lab:
    return "DeviceLab" ;
  case DEVICESPACE_N:
    return "DeviceN" ;
  default:
    return "Unknown" ;
  }
}

char *debug_get_specialhandling(int32 handling)
{
  switch ( handling ) {
  case SPECIALHANDLING_NONE:
    return "None" ;
  case SPECIALHANDLING_OPAQUE:
    return "Opaque" ;
  case SPECIALHANDLING_OPAQUEIGNORE:
    return "OpaqueIgnore" ;
  case SPECIALHANDLING_TRANSPARENT:
    return "Transparent" ;
  case SPECIALHANDLING_TRAPZONES:
    return "TrapZones" ;
  case SPECIALHANDLING_TRAPHIGHLIGHTS:
    return "TrapHighlights" ;
  default:
    return "Unknown" ;
  }
}

char *debug_get_renderingprops(uint32 props)
{
  if ( RENDERING_PROPERTY_HAS_RENDER(props) )
    return "Render" ;

  if ( RENDERING_PROPERTY_HAS_MASK(props) )
    return "Mask" ;

  if ( RENDERING_PROPERTY_HAS_KNOCKOUT(props) )
    return "Knockout" ;

  if ( RENDERING_PROPERTY_IS_IGNORE(props) )
    return "Ignore" ;

  return "Invalid" ;
}

char *debug_get_colorspace(int32 id)
{
  switch ( id ) {
  case SPACE_notset:
    return "notset" ;
  case SPACE_CIETableA:
    return "CIETableA" ;
  case SPACE_CIETableABC:
    return "CIETableABC" ;
  case SPACE_CIETableABCD:
    return "CIETableABCD" ;
  case SPACE_CIEBasedA:
    return "CIEBasedA" ;
  case SPACE_CIEBasedABC:
    return "CIEBasedABC" ;
  case SPACE_CIEBasedDEF:
    return "CIEBasedDEF" ;
  case SPACE_CIEBasedDEFG:
    return "CIEBasedDEFG" ;
  case SPACE_DeviceGray:
    return "DeviceGray" ;
  case SPACE_DeviceRGB:
    return "DeviceRGB" ;
  case SPACE_DeviceCMYK:
    return "DeviceCMYK" ;
  case SPACE_Pattern:
    return "Pattern" ;
  case SPACE_Indexed:
    return "Indexed" ;
  case SPACE_Separation:
    return "Separation" ;
  case SPACE_DeviceN:
    return "DeviceN" ;
  case SPACE_Lab:
    return "Lab" ;
  case SPACE_CalGray:
    return "CalGray" ;
  case SPACE_CalRGB:
    return "CalRGB" ;
  case SPACE_ICCBased:
    return "ICCBased" ;
  case SPACE_Preseparation:
    return "Preseparation" ;
  case SPACE_DeviceCMY:
    return "DeviceCMY" ;
  case SPACE_DeviceRGBK:
    return "DeviceRGBK" ;
  case SPACE_DeviceK:
    return "DeviceK" ;
  case SPACE_FinalDeviceN:
    return "FinalDeviceN" ;
  case SPACE_CIEXYZ:
    return "CIEXYZ" ;
  case SPACE_InterceptCMYK:
    return "InterceptCMYK" ;
  case SPACE_InterceptRGB:
    return "InterceptRGB" ;
  case SPACE_InterceptGray:
    return "InterceptGray" ;
  case SPACE_PatternMask:
    return "PatternMask" ;
  case SPACE_Recombination:
    return "Recombination" ;
  default:
    return "Unknown" ;
  }
}

void debug_print_gucr_colorantset(GUCR_COLORANTSET *flags)
{
  if ( flags ) {
    int32 i, ci ;
    monitorf((uint8 *)"extra words %d set %d [", flags->cWords, flags->nSet) ;
    for ( ci = i = 0 ; i <= flags->cWords ; ++i ) {
      uint32 mask ;
      for ( mask = 1 ; mask ; mask <<= 1, ++ci ) {
        if ( (flags->afMember[i] & mask) != 0 )
          monitorf((uint8 *)" %d", ci) ;
      }
    }
    monitorf((uint8 *)" ]\n") ;
  } else {
    monitorf((uint8 *)" none\n") ;
  }
}

char *debug_get_bool(Bool condition)
{
  if ( condition )
    return "true" ;

  return "false" ;
}

void debug_print_gucr_colorant(GUCR_COLORANT *colorant, int32 indent)
{
  REALCOLORANTMAP *rcmap ;
  char *spaces = "                                        " ;
  int32 i = strlen_int32(spaces) - indent ;

  if ( i < 0 )
    i = 0 ;
  spaces += i ;

  monitorf((uint8 *)"%sColorant %p\n", spaces, colorant) ;
  monitorf((uint8 *)"%s Index %d\n", spaces,
           colorant->info.colorantIndex) ;
  monitorf((uint8 *)"%s Type %s\n", spaces,
           debug_get_coloranttype(colorant->info.colorantType)) ;
  monitorf((uint8 *)"%s Name /%.*s\n", spaces,
           theINLen(colorant->info.name),
           theICList(colorant->info.name)) ;
  if ( colorant->info.originalName )
    monitorf((uint8 *)"%s Original name /%.*s\n", spaces,
             theINLen(colorant->info.originalName),
             theICList(colorant->info.originalName)) ;
  monitorf((uint8 *)"%s X position %d\n", spaces, colorant->info.offsetX) ;
  monitorf((uint8 *)"%s Y position %d\n", spaces, colorant->info.offsetY) ;
  monitorf((uint8 *)"%s Band offset %d\n", spaces, colorant->info.offsetBand) ;
  monitorf((uint8 *)"%s Rendering properties <<\n", spaces) ;
  for ( i = 0 ; i < 6 ; ++i ) {
    static struct {
      char *name ;
      uint32 mask ;
    } properties[8] = {
      { "User", RENDERING_PROPERTY_USER },
      { "NamedColor", RENDERING_PROPERTY_NAMEDCOLOR },
      { "Black", RENDERING_PROPERTY_BLACK },
      { "Linework", RENDERING_PROPERTY_LW },
      { "Text", RENDERING_PROPERTY_TEXT },
      { "Vignette", RENDERING_PROPERTY_VIGNETTE },
      { "Picture", RENDERING_PROPERTY_PICTURE },
      { "Composite", RENDERING_PROPERTY_COMPOSITE }
    } ;
    uint32 mask = (colorant->info.nRenderingProperties & properties[i].mask) ;

    monitorf((uint8 *)"%s  /%s /%s %% (%s)\n", spaces,
             properties[i].name,
             debug_get_renderingprops(mask),
             (mask & RENDERING_PROPERTY_EXPLICIT_ALL) ? "Explicit" : "Implicit") ;
  }
  monitorf((uint8 *)"%s >>\n", spaces) ;
  monitorf((uint8 *)"%s Background? %s\n", spaces,
           debug_get_bool(colorant->info.fBackground)) ;
  monitorf((uint8 *)"%s Dynamic separation? %s\n", spaces,
           debug_get_bool(colorant->info.fAutomatic)) ;
  monitorf((uint8 *)"%s Override screen angle? %s\n", spaces,
           debug_get_bool(colorant->info.fOverrideScreenAngle)) ;
  monitorf((uint8 *)"%s Screen angle %f\n", spaces,
           colorant->info.screenAngle) ;
  monitorf((uint8 *)"%s Special handling %s\n", spaces,
           debug_get_specialhandling(colorant->info.specialHandling)) ;
  monitorf((uint8 *)"%s Neutral density %f\n", spaces,
           colorant->info.neutralDensity) ;
  monitorf((uint8 *)"%s Equivalent real colors [\n", spaces) ;
  rcmap = &colorant->equivalentRealColorant ;
  {
    COLORANTINDEX *cis = rcmap->cimap ;
    monitorf((uint8 *)"%s  %d -> [", spaces, rcmap->ci) ;
    while ( cis && *cis != COLORANTINDEX_UNKNOWN ) {
      monitorf((uint8 *)" %d", *cis++) ;
    }
    monitorf((uint8 *)" ]\n") ;
    monitorf((uint8 *)"%s  Recipe? %s\n", spaces,
             debug_get_bool(rcmap->recipe)) ;
  }
  monitorf((uint8 *)"%s ]\n", spaces) ;
  monitorf((uint8 *)"%s sRGB [", spaces) ;
  for ( i = 0 ; i < 3 ; ++i ) {
    monitorf((uint8 *)" %f", colorant->info.sRGB[i]) ;
  }
  monitorf((uint8 *)" ]\n") ;
  monitorf((uint8 *)"%s CMYK [", spaces) ;
  for ( i = 0 ; i < 4 ; ++i ) {
    monitorf((uint8 *)" %f", colorant->info.CMYK[i]) ;
  }
  monitorf((uint8 *)" ]\n") ;
  monitorf((uint8 *)"%s Omitted? %s\n", spaces,
           debug_get_bool(colorant->fRenderOmit)) ;
  monitorf((uint8 *)"%s Needs recalculation? %s\n", spaces,
           debug_get_bool(colorant->fRecalc)) ;
  monitorf((uint8 *)"%s Channel advances on next? %s\n", spaces,
           debug_get_bool(gucr_colorantsBandChannelAdvance(colorant))) ;
}

void debug_print_gucr_channel(GUCR_CHANNEL *channel, int32 indent)
{
  GUCR_COLORANT *colorant ;
  char *spaces = "                                        " ;
  int32 i = strlen_int32(spaces) - indent ;

  if ( i < 0 )
    i = 0 ;
  spaces += i ;

  monitorf((uint8 *)"%sChannel %p\n", spaces, channel) ;
  monitorf((uint8 *)"%s Map to channel %d\n", spaces,
           channel->nMapToThisChannel) ;
  monitorf((uint8 *)"%s render index %d\n", spaces,
           channel->nRenderIndex) ;
  monitorf((uint8 *)"%s Required? %s\n", spaces,
           debug_get_bool(channel->fRequired)) ;
  monitorf((uint8 *)"%s Required by partial paint? %s\n", spaces,
           debug_get_bool(channel->fRequiredByPartialPaint)) ;
  monitorf((uint8 *)"%s Omitted? %s\n", spaces,
           debug_get_bool(channel->fRenderOmit)) ;
  monitorf((uint8 *)"%s Has blanks? %s\n", spaces,
           debug_get_bool(channel->fBlankColorants)) ;
  monitorf((uint8 *)"%s Start of sheet? %s\n", spaces,
           debug_get_bool(gucr_framesStartOfSheet(channel, NULL, NULL))) ;
  monitorf((uint8 *)"%s End of sheet? %s\n", spaces,
           debug_get_bool(gucr_framesEndOfSheet(channel))) ;
  monitorf((uint8 *)"%s Colorants\n", spaces) ;

  for ( colorant = channel->pColorants ; colorant ; colorant = colorant->pNextColorant ) {
    debug_print_gucr_colorant(colorant, indent + 2) ;
  }
}

void debug_print_gucr_sheet(GUCR_SHEET *sheet, int32 indent)
{
  GUCR_CHANNEL *channel ;
  char *spaces = "                                        " ;
  int32 i = strlen_int32(spaces) - indent ;

  if ( i < 0 )
    i = 0 ;

  spaces += i ;

  monitorf((uint8 *)"%sSheet %p\n", spaces, sheet) ;
  monitorf((uint8 *)"%s Sheet number %d\n", spaces, sheet->nSheetNumber) ;
  monitorf((uint8 *)"%s Required channels %d\n", spaces, sheet->cRequired) ;
  monitorf((uint8 *)"%s Omitted? %s\n", spaces,
           debug_get_bool(sheet->fRenderOmit)) ;
  monitorf((uint8 *)"%s Channels\n", spaces) ;
  for ( channel = sheet->pChannels ; channel ; channel = channel->pNextChannel ) {
    debug_print_gucr_channel(channel, indent + 2) ;
  }
}

void debug_print_gucr_rasterstyle(GUCR_RASTERSTYLE *hr, Bool recursive)
{
  do {
    int32 i ;
    GUCR_SHEET *sheet ;
    OBJECT custconv = OBJECT_NOTVM_NOTHING ;
    COLORANTMAP *cmap ;
    REALCOLORANTMAP *rcmap ;

    monitorf((uint8 *)"Rasterstyle %p\n", hr) ;
    monitorf((uint8 *)"  id %d\n", hr->id) ;
    monitorf((uint8 *)"  Backdrop flag %s\n",
             debug_get_bool(hr->flags & RASTERSTYLE_FLAG_BACKDROP)) ;
    monitorf((uint8 *)"  Virtual device flag %s\n",
             debug_get_bool(hr->flags & RASTERSTYLE_FLAG_VIRTUALDEVICE)) ;
    if ( (hr->flags & ~(RASTERSTYLE_FLAG_BACKDROP |
                        RASTERSTYLE_FLAG_VIRTUALDEVICE)) != 0 ) {
      monitorf((uint8 *)"  Unrecognised flags 0x%x\n",
               (hr->flags & ~(RASTERSTYLE_FLAG_VIRTUALDEVICE |
                              RASTERSTYLE_FLAG_BACKDROP))) ;
    }
    monitorf((uint8 *)"  References %d\n", hr->nReferenceCount) ;
    monitorf((uint8 *)"  Screening %s\n", debug_get_bool(hr->screening)) ;
    monitorf((uint8 *)"  Interleaving style %d (%s)\n",
             hr->nInterleavingStyle,
             debug_get_interleavingstyle(hr->nInterleavingStyle)) ;
    monitorf((uint8 *)"  Values per component %d\n", hr->nValuesPerComponent) ;
    monitorf((uint8 *)"  Separation style %d (%s)\n",
             hr->nSeparationStyle,
             debug_get_separationstyle(hr->nSeparationStyle)) ;
    monitorf((uint8 *)"  # Process colorants %d\n", hr->nProcessColorants) ;
    monitorf((uint8 *)"  Equivalent real colors [\n") ;
    for ( rcmap = hr->equivalentRealColorants ; rcmap ; rcmap = rcmap->next ) {
      COLORANTINDEX *cis = rcmap->cimap ;
      monitorf((uint8 *)"    %d -> [", rcmap->ci) ;
      while ( cis && *cis != COLORANTINDEX_UNKNOWN ) {
        monitorf((uint8 *)" %d", *cis++) ;
      }
      monitorf((uint8 *)" ]\n") ;
      monitorf((uint8 *)"    Recipe? %s\n", debug_get_bool(rcmap->recipe)) ;
    }
    monitorf((uint8 *)"  ]\n") ;
    monitorf((uint8 *)"  Max colorant index %d\n", hr->ciMax) ;
    monitorf((uint8 *)"  Black colorant index %d\n", hr->ciBlack) ;
    monitorf((uint8 *)"  Colorant map [\n") ;
    for ( cmap = hr->cmap ; cmap ; cmap = cmap->next ) {
      COLORANTINDEX *cis = cmap->cimap ;
      monitorf((uint8 *)"    %d -> [", cmap->ci) ;
      while ( cis && *cis != COLORANTINDEX_UNKNOWN ) {
        monitorf((uint8 *)" %d", *cis++) ;
      }
      monitorf((uint8 *)" ]\n") ;
    }
    monitorf((uint8 *)"  ]\n") ;

    debug_print_object_indented(&hr->oFullyFledgedColorants,
                                "  Fully-fledged colorants ", "\n", NULL) ;
    debug_print_object_indented(&hr->oReservedColorants,
                                "  Reserved colorants ", "\n", NULL) ;
    debug_print_object_indented(&hr->oDefaultScreenAngles,
                                "  Default screen angles ", "\n", NULL) ;
    theTags(custconv) = OARRAY | LITERAL | UNLIMITED ;
    theLen(custconv) = 3 ;
    oArray(custconv) = &hr->oCustomConversions[0] ;
    debug_print_object_indented(&custconv, "  Custom conversions ", "\n", NULL) ;
    monitorf((uint8 *)"  Color channels [") ;
    for ( i = 0 ; i < hr->numColorChannels ; ++i ) {
      if ( hr->colorChannelNames[i] ) {
        monitorf((uint8 *)" /%.*s", theINLen(hr->colorChannelNames[i]),
                 theICList(hr->colorChannelNames[i])) ;
      } else {
        monitorf((uint8 *)" NULL") ;
      }
    }
    monitorf((uint8 *)" ]\n") ;
    debug_print_object_indented(&hr->osRGB, "  sRGB object ", "\n", NULL) ;
    monitorf((uint8 *)"  Process colorants [") ;
    for ( i = 0 ; i < hr->numProcessColorantNames ; ++i ) {
      if ( hr->processColorantNames[i] ) {
        monitorf((uint8 *)" /%.*s", theINLen(hr->processColorantNames[i]),
                 theICList(hr->processColorantNames[i])) ;
      } else {
        monitorf((uint8 *)" NULL") ;
      }
    }
    monitorf((uint8 *)" ]\n") ;
    debug_print_object_indented(&hr->oColorantPresence,
                                "  Colorant presence ", "\n", NULL) ;
    debug_print_object_indented(&hr->oColorantDetails,
                                "  Colorant details ", "\n", NULL) ;
    monitorf((uint8 *)"  ProcessColorModel %d (%s)\n",
             hr->processColorModel,
             debug_get_devicespace(hr->processColorModel)) ;
    monitorf((uint8 *)"  CalibrationColorModel %d (%s)\n",
             hr->calibrationColorModel,
             debug_get_colorspace(hr->calibrationColorModel)) ;
    monitorf((uint8 *)"  photoinkInfo ") ;
    debug_print_gucr_photoink(hr->photoinkInfo, 2) ;

    monitorf((uint8 *)"  Channel assignments [") ;
    for ( i = 0 ; i < NUM_CHANNEL_ASSIGNMENT ; ++i ) {
      monitorf((uint8 *)" %d", hr->anChannelAssignment[i]) ;
    }
    monitorf((uint8 *)" ]\n") ;

    monitorf((uint8 *)"  Sheets\n") ;
    for ( sheet = hr->pSheets ; sheet ; sheet = sheet->pNextSheet ) {
      debug_print_gucr_sheet(sheet, 4) ;
    }

    monitorf((uint8 *)"  Background flags ") ;
    debug_print_gucr_colorantset(hr->pBackgroundFlags) ;
    monitorf((uint8 *)"  Band Size %d\n", hr->nBandSize) ;
    monitorf((uint8 *)"  Max Band Offset %d\n", hr->nMaxOffsetIntoBand) ;
    monitorf((uint8 *)"  Colorant omits ") ;
    debug_print_gucr_colorantset(hr->pColorantOmits) ;
    monitorf((uint8 *)"  Current omits ") ;
    debug_print_gucr_colorantset(hr->pCurrentOmits) ;
    monitorf((uint8 *)"  Omit details <<\n") ;
    monitorf((uint8 *)"    Ignore BeginPage? %s\n",
             debug_get_bool(hr->omitDetails.beginpage)) ;
    monitorf((uint8 *)"    Ignore EndPage? %s\n",
             debug_get_bool(hr->omitDetails.endpage)) ;
    monitorf((uint8 *)"    Test ImageContents? %s\n",
             debug_get_bool(hr->omitDetails.imagecontents)) ;
    monitorf((uint8 *)"    Ignore SuperBlacks? %s\n",
             debug_get_bool(hr->omitDetails.superblacks)) ;
    monitorf((uint8 *)"    Ignore RegisterMarks? %s\n",
             debug_get_bool(hr->omitDetails.registermarks)) ;
    monitorf((uint8 *)"    Ignore Knockouts? %s\n",
             debug_get_bool(hr->omitDetails.knockouts)) ;
    monitorf((uint8 *)"  >>\n") ;
    monitorf((uint8 *)"  Abort on separations? %s\n",
             debug_get_bool(hr->fAbortSeparations)) ;
    monitorf((uint8 *)"  Add dynamic separations? %s\n",
             debug_get_bool(hr->fAddAllSpotColors)) ;
    monitorf((uint8 *)"  Remove dynamic separations? %s\n",
             debug_get_bool(hr->fRemoveAllSpotColors)) ;
    monitorf((uint8 *)"  Omit monochrome pages? %s\n",
             debug_get_bool(hr->fOmitMonochrome)) ;
    monitorf((uint8 *)"  Omit blank separations? %s\n",
             debug_get_bool(hr->fOmitSeparations)) ;
    monitorf((uint8 *)"  Omit dynamic separation blanks? %s\n",
             debug_get_bool(hr->fOmitExtraSpot)) ;
    monitorf((uint8 *)"  Omit process blanks? %s\n",
             debug_get_bool(hr->fOmitProcess)) ;
    monitorf((uint8 *)"  Got all equivalents? %s\n",
             debug_get_bool(hr->fHaveEquivalent)) ;
    monitorf((uint8 *)"  Generation number %d\n", hr->generation) ;

    monitorf((uint8 *)"  Next GC rasterstyle %p\n", hr->next) ;
    monitorf((uint8 *)"  Parent rasterstyle %p\n", hr->parent) ;

    hr = hr->parent ;
  } while ( hr && recursive) ;
}
#endif

void init_C_globals_gu_chan(void)
{
  generation = 0 ;
  next_rasterstyle_id = 0;
  rasterstyle_gc_list = NULL ;
  backdrop_gc_root = NULL ;
#if defined( DEBUG_BUILD )
  ciNext_one = 10; /* Allow 10 colorants in the device RS */
  ciNext_two = 10; /* Allow 10 colorants in the device RS */
#endif
}

/* Log stripped */
