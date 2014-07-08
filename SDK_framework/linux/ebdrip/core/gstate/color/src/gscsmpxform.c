/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscsmpxform.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Routines to create and cache colorspace objects and custom tinttransform data
 * for backend color transformations.
 */

#include "core.h"
#include "mm.h"                 /* mm_alloc */
#include "namedef_.h"           /* NAME_* */
#include "swerrors.h"           /* VMERROR */
#include "objnamer.h"
#include "gs_colorpriv.h"       /* CLINK */
#include "gs_chaincachepriv.h"  /* cc_chainCachePurgeSpaceCache */
#include "gscsmpxformpriv.h"    /* externs */

/*----------------------------------------------------------------------------*/
typedef struct SPOT_DATA {
  EQUIVCOLOR equivcolor; /* CMYK equivalent color for spot */
  Bool skip; /* For DeviceN, whether to ignore altogether */
} SPOT_DATA;

typedef Bool (*fn_invokeTransform)(GSC_SIMPLE_TRANSFORM *simpleTransform,
                                   CLINK *clink, USERVALUE *ocolor);

#define SMPXFORM_NAME "SimpleTransform"

struct GSC_SIMPLE_TRANSFORM {
  int32 simpleTransformId;
  int32 nSpots;
  fn_invokeTransform invokeTransform;
  COLORSPACE_ID cspace; /* Separation or DeviceN */
  DEVICESPACEID inputProcessColorModel;
  SPOT_DATA *spot_data;
  OBJECT_NAME_MEMBER
};

typedef struct CACHEENTRY {
  OBJECT csobj;
  GSC_SIMPLE_TRANSFORM *simpleTransform;
  uint32 key;
  int32 nSpots;
  COLORANTINDEX *spotcolorants;
  CMYK_EQUIV_CALLBACK cmyk_equiv_callback;
  uint32 inputRS_id;
  uint32 targetRS_id;
  struct CACHEENTRY *next;
} CACHEENTRY;

#define SPACECACHE_NAME "SpaceCache"

#define SPACECACHE_TABLE_SIZE (32)

struct SPACECACHE {
  CACHEENTRY **table;
  int32 count;
  int32 nextEntryId;
  OBJECT_NAME_MEMBER
};

/*----------------------------------------------------------------------------*/
static Bool transform_ncolor_to_cmyk(GSC_SIMPLE_TRANSFORM *simpleTransform,
                                     CLINK *pLink, USERVALUE *oColorValues)
{
  int32 n_iColorants = pLink->n_iColorants;
  SPOT_DATA *spot_data = simpleTransform->spot_data;
  USERVALUE *iColorValues = pLink->iColorValues;
  USERVALUE c, m, y, k;
  int32 i;

  HQASSERT(simpleTransform->nSpots > 1, "Expected DeviceN space");
  HQASSERT(n_iColorants == simpleTransform->nSpots, "num inputs != spots");

  /* Initialize ouputs */
  c = m = y = k = 0.0f;

  /* Do the conversions using the same method as the interceptdevicen chain
   * link.  Colour values are merged by iteratively multiplying values. This has
   * the effect of making every merge at least as dark the previous iteration.
   */
  for ( i = 0 ; i < n_iColorants; ++i ) {
    if ( !spot_data->skip ) {
      USERVALUE spotval = iColorValues[i];
      COLOR_01_ASSERT(spotval, "transform_ncolor_to_cmyk input");
      c += (1.0f - c) * spotval * spot_data->equivcolor[0];
      m += (1.0f - m) * spotval * spot_data->equivcolor[1];
      y += (1.0f - y) * spotval * spot_data->equivcolor[2];
      k += (1.0f - k) * spotval * spot_data->equivcolor[3];
    }
    ++spot_data;
  }

  oColorValues[0] = c;
  oColorValues[1] = m;
  oColorValues[2] = y;
  oColorValues[3] = k;

  return TRUE;
}

/*----------------------------------------------------------------------------*/
static Bool transform_singlespot_to_cmyk(GSC_SIMPLE_TRANSFORM *simpleTransform,
                                         CLINK *pLink, USERVALUE *oColorValues)
{
  SPOT_DATA *spot_data = simpleTransform->spot_data;
  USERVALUE spotval = pLink->iColorValues[0];

  HQASSERT(pLink->n_iColorants == 1 && simpleTransform->nSpots == 1,
           "Expected a single spot");

  COLOR_01_ASSERT(spotval, "transform_singlespot_to_cmyk input");
  oColorValues[0] = spotval * spot_data->equivcolor[0];
  oColorValues[1] = spotval * spot_data->equivcolor[1];
  oColorValues[2] = spotval * spot_data->equivcolor[2];
  oColorValues[3] = spotval * spot_data->equivcolor[3];

  return TRUE;
}

/*----------------------------------------------------------------------------*/
static Bool create_array(OBJECT *thearray, int32 len)
{
  OBJECT *array;
  int32 i;

  HQASSERT(thearray != NULL, "thearray NULL in create_array.");

  if ( len < 0 )
    return error_handler(RANGECHECK);
  if ( len > MAXPSARRAY )
    return error_handler(LIMITCHECK);

  if ( len > 0 ) {
    array = mm_alloc(mm_pool_color, sizeof(OBJECT) * len,
                      MM_ALLOC_CLASS_CSPACE_CACHE);
    if ( array == NULL )
      return error_handler(VMERROR);
  } else
    array = NULL;

  theITags(thearray) = OARRAY | UNLIMITED | LITERAL;
  theILen(thearray) = CAST_TO_UINT16(len);
  oArray(*thearray) = array;

  for ( i = 0; i < len; ++i ) {
    array[i] = onull; /* Struct copy to set slot properties */
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
static void destroy_array(OBJECT *thearray)
{
  int32 len;

  HQASSERT(thearray != NULL, "thearray NULL in destroy_array");
  HQASSERT(oType(*thearray) == OARRAY, "Non-array passed into destroy_array");

  len = theILen(thearray);
  HQASSERT(len >= 0, "len must be positive");

  if ( len != 0 ) {
    OBJECT *array = oArray(*thearray);
    int32 i;

    HQASSERT(array != NULL, "array NULL in destroy_array");

    for ( i = 0; i < len; ++i ) {
      if ( oType(array[i]) == OARRAY )
        destroy_array(&array[i]);
    }

    mm_free(mm_pool_color, array, sizeof(OBJECT) * len);
  }
  *thearray = onothing;
}

/*----------------------------------------------------------------------------*/
static void cacheentry_free(CACHEENTRY **cache)
{
  CACHEENTRY *entry = *cache;

  if ( entry == NULL )
    return;

  if ( oType(entry->csobj) == OARRAY )
    destroy_array(&entry->csobj);

  if ( entry->spotcolorants != NULL )
    mm_free(mm_pool_color, entry->spotcolorants,
            entry->nSpots * sizeof(COLORANTINDEX));

  if ( entry->simpleTransform != NULL ) {
    if ( entry->simpleTransform->spot_data != NULL )
      mm_free(mm_pool_color, entry->simpleTransform->spot_data,
              sizeof(SPOT_DATA) * entry->nSpots);

    UNNAME_OBJECT(entry->simpleTransform);
    mm_free(mm_pool_color, entry->simpleTransform,
            sizeof(GSC_SIMPLE_TRANSFORM));
  }

  mm_free(mm_pool_color, entry, sizeof(CACHEENTRY));
  *cache = NULL;
}

/*----------------------------------------------------------------------------*/
static Bool cacheentry_alloc(int32 nSpots, CACHEENTRY **cache)
{
  CACHEENTRY *entry;

  entry = mm_alloc(mm_pool_color, sizeof(CACHEENTRY),
                   MM_ALLOC_CLASS_CSPACE_CACHE);
  if ( entry == NULL )
    return error_handler(VMERROR);

  entry->csobj = onull; /* Struct copy to set slot properties */
  entry->nSpots = nSpots;
  entry->spotcolorants = NULL;

  entry->simpleTransform = mm_alloc(mm_pool_color, sizeof(GSC_SIMPLE_TRANSFORM),
                                    MM_ALLOC_CLASS_CSPACE_CACHE);
  if ( entry->simpleTransform == NULL ) {
    cacheentry_free(&entry);
    return error_handler(VMERROR);
  }

  entry->simpleTransform->nSpots = nSpots;

  entry->simpleTransform->spot_data =
    mm_alloc(mm_pool_color, nSpots * sizeof(SPOT_DATA),
             MM_ALLOC_CLASS_CSPACE_CACHE);
  if ( entry->simpleTransform->spot_data == NULL ) {
    cacheentry_free(&entry);
    return error_handler(VMERROR);
  }

  entry->spotcolorants = mm_alloc(mm_pool_color,
                                  nSpots * sizeof(COLORANTINDEX),
                                  MM_ALLOC_CLASS_CSPACE_CACHE);
  if ( entry->spotcolorants == NULL ) {
    cacheentry_free(&entry);
    return error_handler(VMERROR);
  }

  if ( !create_array(&entry->csobj, 4) ) {
    cacheentry_free(&entry);
    return error_handler(VMERROR);
  }

  *cache = entry;
  return TRUE;
}

/*----------------------------------------------------------------------------*/
static CACHEENTRY *spacecache_newobj(SPACECACHE *spacecache,
                                     GS_COLORinfo *colorInfo,
                                     GUCR_RASTERSTYLE *inputRS,
                                     int32 nSpots, COLORANTINDEX *spotcolorants,
                                     CMYK_EQUIV_CALLBACK cmyk_equiv_callback,
                                     void *private_callback_data)
{
  CACHEENTRY *cache = NULL;
  GSC_SIMPLE_TRANSFORM *simpleTransform;
  OBJECT *olist, *o_cspace, *o_transform;
  SPOT_DATA *spot_data;
  int32 i;

  if ( !cacheentry_alloc(nSpots, &cache) ) {
    cacheentry_free(&cache);
    return NULL;
  }

  cache->key = 0;
  HQASSERT(cache->nSpots == nSpots, "nSpots should be set already");
  for ( i = 0; i < nSpots; ++i ) {
    cache->spotcolorants[i] = spotcolorants[i];
  }
  cache->cmyk_equiv_callback = cmyk_equiv_callback;
  cache->inputRS_id = inputRS == NULL ? 0 : guc_rasterstyleId(inputRS);
  cache->targetRS_id = guc_rasterstyleId(gsc_getTargetRS(colorInfo));
  cache->next = NULL;

  simpleTransform = cache->simpleTransform;

  simpleTransform->simpleTransformId = ++spacecache->nextEntryId;
  HQASSERT(simpleTransform->nSpots == nSpots, "nSpots should be set already");
  if ( nSpots == 1 ) {
    simpleTransform->invokeTransform = transform_singlespot_to_cmyk;
    simpleTransform->cspace = SPACE_Separation;
  } else {
    simpleTransform->invokeTransform = transform_ncolor_to_cmyk;
    simpleTransform->cspace = SPACE_DeviceN;
  }
  if ( inputRS != NULL )
    guc_deviceColorSpace(inputRS, &simpleTransform->inputProcessColorModel,
                         NULL);
  else
    simpleTransform->inputProcessColorModel = SPACE_DeviceGray;
  NAME_OBJECT(simpleTransform, SMPXFORM_NAME);

  /* Pick up the PS colorspace array */
  o_cspace = oArray(cache->csobj);

  /* Type of colorspace */
  if ( nSpots == 1 ) {
    object_store_name(&o_cspace[0], NAME_Separation, LITERAL);
    olist = &o_cspace[1];
  } else {
    object_store_name(&o_cspace[0], NAME_DeviceN, LITERAL);
    /* Create array for colorant names */
    if ( !create_array(&o_cspace[1], nSpots) ) {
      cacheentry_free(&cache);
      return NULL;
    }
    olist = oArray(o_cspace[1]);
  }

  /* Set up colorspace colorant names, and record CMYK equivalence and if need
   * to skip or merge it into CMYK colorants (latter not really relevant for
   * Separation).
   */
  spot_data = simpleTransform->spot_data;
  for ( i = 0; i < nSpots; ++i ) {
    EQUIVCOLOR *equivcolor;
    NAMECACHE *sepname;

    /* inputRS is allowed to be NULL here, but cmyk_equiv_callback must cope */
    sepname = cmyk_equiv_callback(inputRS, spotcolorants[i],
                                  &equivcolor, private_callback_data);

    if ( sepname == NULL ) {
      /* It's probable that we have a colorant but couldn't obtain CMYK
       * equivalent values for it. If so, there shouldn't be any objects to
       * render in this colorant because the color code will have converted
       * objects to the alternate space.
       * We can assert that the colorant exists. It's harder to assert that
       * objects haven't been painted using it.
       */
      HQASSERT(inputRS != NULL &&
               guc_getColorantName(inputRS, spotcolorants[i]) != NULL,
               "Expected a fully fledged colorant");
      theTags(olist[i]) = ONAME|LITERAL;
      oName(olist[i]) = system_names + NAME_None;
      spot_data[i].skip = TRUE;
    } else {
      int32 j;

      HQASSERT((*equivcolor)[0] != -1.0f && (*equivcolor)[1] != -1.0f &&
               (*equivcolor)[2] != -1.0f && (*equivcolor)[3] != -1.0f,
               "CMYK equivalents are not properly set");

      /* Add colorant name to colorspace */
      theTags(olist[i]) = ONAME|LITERAL;
      oName(olist[i]) = sepname;

      /* Remember the CMYK equivalent */
      for ( j = 0; j < NUM_EQUIV_COLORS; ++j ) {
        spot_data[i].equivcolor[j] = (*equivcolor)[j];
      }
      spot_data[i].skip = FALSE;
    }
  }

  /* Alternate colorspace */
  object_store_name(&o_cspace[2], NAME_DeviceCMYK, LITERAL);

  /* Alternate colorspace tint transform - this can be invoked so we're making
   * it point back into the spacecache using a special marker of the name,
   * SimpleTintTransform, as the first element, and a pointer to the spacecache
   * entry. This will be recognised by cc_invokeSimpleTransform() and used to
   * trampoline a Separation/DeviceN space onto a spacecache transform function.
   */
  if ( !create_array(&o_cspace[3], 2) ) {
    cacheentry_free(&cache);
    return NULL;
  }
  /* Make array executable, but also read-only so automatic binding won't
     traverse it. */
  theTags(o_cspace[3]) = OARRAY|EXECUTABLE|READ_ONLY;

  o_transform = oArray(o_cspace[3]);

  object_store_name(&o_transform[0], NAME_SimpleTintTransform, LITERAL);
  theTags(o_transform[1]) = OCPOINTER | LITERAL;
  oCPointer(o_transform[1]) = cache->simpleTransform;

  return cache;
}

/*----------------------------------------------------------------------------*/
static CACHEENTRY *spacecache_lookup(SPACECACHE *spacecache,
                                     GS_COLORinfo *colorInfo,
                                     GUCR_RASTERSTYLE *inputRS,
                                     int32 nSpots, COLORANTINDEX *spotcolorants,
                                     CMYK_EQUIV_CALLBACK cmyk_equiv_callback,
                                     void *private_callback_data)
{
  GUCR_RASTERSTYLE *targetRS = gsc_getTargetRS(colorInfo);
  CACHEENTRY *cache;
  uint32 key;
  int32 i;

  HQASSERT(nSpots > 0, "no spot count");
  HQASSERT(spotcolorants, "no spotcolorants");

  /* First calculate the hash key */
  key = nSpots;
  for ( i = 0; i < nSpots; ++i ) {
    key = (key << 1) + spotcolorants[i];
  }

  for ( cache = spacecache->table[key & (SPACECACHE_TABLE_SIZE - 1)];
        cache != NULL; cache = cache->next ) {
    if ( cache->key == key &&
         cache->nSpots == nSpots &&
         cache->cmyk_equiv_callback == cmyk_equiv_callback &&
         inputRS != NULL && cache->inputRS_id == guc_rasterstyleId(inputRS) &&
         cache->targetRS_id == guc_rasterstyleId(targetRS) ) {
      for ( i = 0; i < nSpots; ++i ) {
        if ( spotcolorants[i] != cache->spotcolorants[i] )
          break;
      }
      if ( i == nSpots ) /* Got a full match */
        return cache;
    }
  }

  /* No match if we get here, so create a new cache object: */
  cache = spacecache_newobj(spacecache, colorInfo, inputRS,
                            nSpots, spotcolorants,
                            cmyk_equiv_callback, private_callback_data);
  if ( cache == NULL )
    return NULL;

  /* And insert it */
  cache->key = key;
  cache->next = spacecache->table[key & (SPACECACHE_TABLE_SIZE - 1)];
  spacecache->table[key & (SPACECACHE_TABLE_SIZE - 1)] = cache;
  ++spacecache->count;

  return cache;
}

/*----------------------------------------------------------------------------*/
Bool gsc_spacecache_init(SPACECACHE **spacecacheRef)
{
  SPACECACHE *spacecache;

  HQASSERT(*spacecacheRef == NULL, "Spacecache already exists");

  spacecache = mm_alloc(mm_pool_color, sizeof(SPACECACHE),
                        MM_ALLOC_CLASS_CSPACE_CACHE);
  if ( spacecache == NULL )
    return error_handler(VMERROR);

  spacecache->table = NULL;
  spacecache->count = 0;
  spacecache->nextEntryId = 0;

  spacecache->table = mm_alloc(mm_pool_color,
                               SPACECACHE_TABLE_SIZE * sizeof(CACHEENTRY*),
                               MM_ALLOC_CLASS_CSPACE_CACHE);
  if ( spacecache->table == NULL ) {
    mm_free(mm_pool_color, spacecache, sizeof(SPACECACHE));
    return error_handler(VMERROR);
  }

  HqMemZero((uint8 *)spacecache->table,
            SPACECACHE_TABLE_SIZE * sizeof(CACHEENTRY*));

  NAME_OBJECT(spacecache, SPACECACHE_NAME);
  *spacecacheRef = spacecache;
  return TRUE;
}

/*----------------------------------------------------------------------------*/
void gsc_spacecache_destroy(SPACECACHE **spacecacheRef,
                            GS_CHAIN_CACHE_STATE *chainCacheState,
                            GS_COLORinfoList *colorInfoList)
{
  SPACECACHE *spacecache = *spacecacheRef;
  CACHEENTRY **table;
  int32 count, i;

  if ( spacecache == NULL )
    return;

  VERIFY_OBJECT(spacecache, SPACECACHE_NAME);

  /* Purge all named colorant caches. NB. We only need to clear out those cache
     entries that that might rely on simple transforms, but the caches don't
     know which they are, so we're forced to purge all entries. */
  cc_restoreNamedColorantCache(-1, colorInfoList);

  /* Purge the color chain cache of all entries containing a color space that is
     about to be destroyed. */
  cc_chainCachePurgeSpaceCache(chainCacheState);

  table = spacecache->table;
  count = spacecache->count;
  i = SPACECACHE_TABLE_SIZE - 1;
  while ( count > 0 ) {
    CACHEENTRY *cache = table[i];
    HQASSERT(i >= 0, "i gone -ve");
    while ( cache != NULL ) {
      CACHEENTRY *next = cache->next;
      cacheentry_free(&cache);
      cache = next;
      --count;
    }
    HQASSERT(count >= 0, "count gone -ve");
    --i;
  }

  if ( table != NULL )
    mm_free(mm_pool_color, table, SPACECACHE_TABLE_SIZE * sizeof(CACHEENTRY*));
  UNNAME_OBJECT(spacecache);
  mm_free(mm_pool_color, spacecache, sizeof(SPACECACHE));
  *spacecacheRef = NULL;
}

/*----------------------------------------------------------------------------*/
OBJECT *gsc_spacecache_getcolorspace(GS_COLORinfo *colorInfo,
                                     GUCR_RASTERSTYLE *inputRS,
                                     int32 nSpots, COLORANTINDEX *spotcolorants,
                                     CMYK_EQUIV_CALLBACK cmyk_equiv_callback,
                                     void *private_callback_data)
{
  SPACECACHE *spacecache = colorInfo->colorState->spacecache;
  CACHEENTRY *cache;

  VERIFY_OBJECT(spacecache, SPACECACHE_NAME);
  HQASSERT(nSpots > 0, "no spots?");
  HQASSERT(spotcolorants != NULL, "no spotcolorants");

  cache = spacecache_lookup(spacecache, colorInfo, inputRS,
                            nSpots, spotcolorants,
                            cmyk_equiv_callback,
                            private_callback_data);

  return cache != NULL ? &cache->csobj : NULL;
}

/*----------------------------------------------------------------------------*/
/* Build a simple transform colorspace and install it as the current
 * colorspace. */
Bool gsc_spacecache_setcolorspace(GS_COLORinfo *colorInfo,
                                  GUCR_RASTERSTYLE *inputRS,
                                  int32 colorType,
                                  int32 nColorants,
                                  COLORANTINDEX *colorantIndices,
                                  Bool fCompositing,
                                  CMYK_EQUIV_CALLBACK cmyk_equiv_callback,
                                  void *private_callback_data)
{
  OBJECT *colorSpace;

  HQASSERT(colorInfo != NULL, "colorInfo is null");
  VERIFY_OBJECT(colorInfo->colorState->spacecache, SPACECACHE_NAME);
  HQASSERT(nColorants >= 0, "nColorants is negative");
  HQASSERT(colorantIndices != NULL, "colorantIndices is null");

  colorSpace = gsc_spacecache_getcolorspace(colorInfo, inputRS,
                                            nColorants, colorantIndices,
                                            cmyk_equiv_callback,
                                            private_callback_data);
  if ( colorSpace == NULL )
    return FALSE;

  return gsc_setcustomcolorspacedirect(colorInfo, colorType, colorSpace,
                                       fCompositing);
}

/*----------------------------------------------------------------------------*/
Bool cc_invokeSimpleTransform(GSC_SIMPLE_TRANSFORM *simpleTransform,
                              CLINK *pLink, USERVALUE *oCols)
{
  VERIFY_OBJECT(simpleTransform, SMPXFORM_NAME);
  HQASSERT(simpleTransform->invokeTransform != NULL,
           "NULL transform function pointer");
  HQASSERT(pLink != NULL, "pLink is null");
  return simpleTransform->invokeTransform(simpleTransform, pLink, oCols);
}

/*----------------------------------------------------------------------------*/
/* 'colorSpaceObj' is expected to be a valid Separation/DeviceN space which may
 * contain a simple transform from the space cache or a conventional tint
 * transform. If it contains a simple transform then it is returned, otherwise
 * NULL is returned.
 * The simple transform will be recognised by cc_invokeSimpleTransform() and
 * used to trampoline a Separation/DeviceN space onto a spacecache transform
 * function.
 */
GSC_SIMPLE_TRANSFORM *cc_csaGetSimpleTransform(OBJECT *colorSpaceObj)
{
  OBJECT *tintTransform;

  HQASSERT((oType(*colorSpaceObj) == OARRAY ||
            oType(*colorSpaceObj) == OPACKEDARRAY) &&
           (theLen(*colorSpaceObj) == 4 || theLen(*colorSpaceObj) == 5) &&
           oType(oArray(*colorSpaceObj)[0]) == ONAME &&
           (oNameNumber(oArray(*colorSpaceObj)[0]) == NAME_Separation ||
            oNameNumber(oArray(*colorSpaceObj)[0]) == NAME_DeviceN),
           "Expected a valid Separation/DeviceN space");

  tintTransform = &oArray(*colorSpaceObj)[3];

  /* Simple transforms are characterised by a 2 element array with the name
   * object, SimpleTintTransform, as the first element and a pointer to the
   * simple transform as the second.
   */
  if ( theLen(*tintTransform) == 2 &&
       oType(oArray(*tintTransform)[0]) == ONAME &&
       oNameNumber(oArray(*tintTransform)[0]) == NAME_SimpleTintTransform &&
       oType(oArray(*tintTransform)[1]) == OCPOINTER ) {
    GSC_SIMPLE_TRANSFORM *simpleTransform =
      oCPointer(oArray(*tintTransform)[1]);
    VERIFY_OBJECT(simpleTransform, SMPXFORM_NAME);
    return simpleTransform;
  }

  return NULL;
}

/*----------------------------------------------------------------------------*/
/** Returns the process color model of the input rasterstyle. */
DEVICESPACEID cc_spacecache_PCMofInputRS(GSC_SIMPLE_TRANSFORM *simpleTransform)
{
  VERIFY_OBJECT(simpleTransform, SMPXFORM_NAME);
  return simpleTransform->inputProcessColorModel;
}

/*----------------------------------------------------------------------------*/
int32 cc_spacecache_id(GSC_SIMPLE_TRANSFORM *simpleTransform)
{
  VERIFY_OBJECT(simpleTransform, SMPXFORM_NAME);
  return simpleTransform->simpleTransformId;
}

/* Log stripped */
