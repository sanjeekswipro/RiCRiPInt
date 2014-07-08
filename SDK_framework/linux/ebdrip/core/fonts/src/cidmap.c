/** \file
 * \ingroup cid
 *
 * $HopeName: COREfonts!src:cidmap.c(EBDSDK_P.1) $:
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * CID Map caching and handling
 *
 * NOTE: The term CIDMAP in this file describes the data structure that maps
 * from a CID (character identifier) to a font number and an offset to the
 * charstrings data.  This is a property of a CIDFont.  It should not be
 * confused with a CMap, which describes the mapping of input character codes
 * to CIDs.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "mm.h"
#include "mps.h"
#include "namedef_.h"

#include "fonts.h"
#include "cidmap.h"
#include "cidfont.h"
#include "fontdata.h"
#include "cache_handler.h"


/* ----------------------------------------------------------------------- */
/* The CID map cache is separate from the CID font cache, because Type 0 and
   Type 2 CID fonts can share the same definition of the CID map. A CIDMap
   provides a lookup from a CID to a character definition and possibly
   FDIndex. */
typedef struct CIDMAPCACHE {
  int32 fid ;        /* Font identifier */
  OBJECT *source ;   /* data source for CID map */
  uint32 offset ;    /* offset into map source */
  uint32 fdbytes ;   /* bytes encoding FDIndex */
  uint32 gdbytes ;   /* bytes encoding glyph offset */
  uint32 cidcount ;  /* number of CIDs */
  OBJECT *glyphdir ; /* GlyphDirectory for downloadable chars */
  struct CIDMAPCACHE *next ;
  OBJECT_NAME_MEMBER
} CIDMAPCACHE ;

#define CIDMAP_CACHE_NAME "CID map cache"


#if defined( ASSERT_BUILD )
int32 debug_cidmap = 0;
#endif

static mps_root_t cidmap_gc_root ;

/* ----------------------------------------------------------------------- */
/* Extract a high-byte first integer from an offset, updating the offset
   pointer. */
uint32 cidmap_offset(uint8 **cidmap, uint32 nbytes)
{
  uint8 *data ;
  uint32 result = 0 ;

  HQASSERT(cidmap, "No cidmap pointer");
  data = *cidmap ;

  HQASSERT(data, "No cidmap data");
  HQASSERT(nbytes <= 4, "Bytes to extract out of range") ;

  while ( nbytes > 0 ) {
    result = (result << 8) | *data++ ;
    --nbytes ;
  }

  *cidmap = data ;

  return result ;
}


/* ----------------------------------------------------------------------- */

/* The base pointer to the list of cid maps that we have cached */
static CIDMAPCACHE *cid_map_cache = NULL;


/** Low-memory handling data for the CID map cache. */
mm_simple_cache_t cidmap_mem_cache;


/** The size of the entire CID map cache. */
#define map_cache_size (cidmap_mem_cache.data_size)


static Bool cidmap_mem_purge(mm_simple_cache_t *cache,
                             Bool *purged_something, size_t purge);


static void init_C_globals_cidmap(void)
{
#if defined( ASSERT_BUILD )
  debug_cidmap = 0 ;
#endif
  cidmap_gc_root = NULL ;
  cid_map_cache = NULL ;
  cidmap_mem_cache.low_mem_handler = mm_simple_cache_handler_init;
  cidmap_mem_cache.low_mem_handler.name = "CID map cache";
  cidmap_mem_cache.low_mem_handler.tier = memory_tier_disk;
  /* Renderer threads don't touch this, so it's mt-safe. */
  cidmap_mem_cache.low_mem_handler.multi_thread_safe = TRUE;
  /* no init needed for offer */
  cidmap_mem_cache.data_size = cidmap_mem_cache.last_data_size = 0;
  cidmap_mem_cache.last_purge_worked = TRUE;
  /* CID maps are easy to regenerate, and most of the data is held in
     the fontdata anyway. */
  cidmap_mem_cache.cost = 1.0f;
  cidmap_mem_cache.pool = mm_pool_temp;
  cidmap_mem_cache.purge = &cidmap_mem_purge;
}

/* Create a cache entry for the font in question, and initialise the fontdata
   and font set. */
static CIDMAPCACHE *cid_set_map(FONTinfo *fontInfo)
{
  CIDMAPCACHE *cid_map, **cid_prev = &cid_map_cache ;
  int32 fid ;

  HQASSERT(fontInfo, "No font info") ;
  fid = theCurrFid(*fontInfo) ;

  /* Search for a matching entry in the CID data cache */
  while ( (cid_map = *cid_prev) != NULL ) {
    VERIFY_OBJECT(cid_map, CIDMAP_CACHE_NAME) ;

    /* If this entry was trimmed by a GC scan, remove it */
    if ( cid_map->source == NULL ) {
      *cid_prev = cid_map->next ;
      UNNAME_OBJECT(cid_map) ;
      mm_free(mm_pool_temp, (mm_addr_t)cid_map, sizeof(CIDMAPCACHE)) ;
      map_cache_size -= sizeof(CIDMAPCACHE) ;
    } else if ( fid == cid_map->fid ) {
      /* Move entry to head of MRU list */
      *cid_prev = cid_map->next ;
      break ;
    } else {
      cid_prev = &cid_map->next ;
    }
  }

  /* If no CID map cache found, allocate a new one and initialise the correct
     map entry in it. The object is copied into new object memory, because we
     don't know where the pointer came from. It could be a pointer from the
     C, PostScript or graphics stack, in which case its contents will change
     unpredictably. */
  if ( !cid_map ) {
    uint32 fontoffset = 0 ;
    uint32 fdbytes = 0, gdbytes = 0 ;
    uint32 cidmapoffset = 0 ;
    int32 fonttype = theFontType(*fontInfo) ;
    OBJECT *fdict = &theMyFont(*fontInfo) ;
    OBJECT *source = NULL ;
    OBJECT *theo ;

    /* Note that CIDTYPE2 with dictionary and integer form CIDMaps have no
       need for GDBytes. TN3011 which introduced these forms does not
       explicitly say it is not needed, but we allow its omission in these
       cases. CIDFONTTYPE2 never needs FDBytes or CIDMapOffset. It would be
       possible to relax the restrictions further if a GlyphDirectory is
       present, CIDMapOffset and GDBytes will not be needed in that case. We
       don't do this because it's not required and because it's a little bit
       more awkward to write. */
    enum { cidmap_match_CIDCount, cidmap_match_FDBytes, cidmap_match_GDBytes,
           cidmap_match_CIDMapOffset, cidmap_match_GlyphDirectory,
           cidmap_match_dummy } ;
    static NAMETYPEMATCH cidmap_match[cidmap_match_dummy + 1] = {
      /* Use the enum above to index this match */
      { NAME_CIDCount,                   1, { OINTEGER }},
      { NAME_FDBytes | OOPTIONAL,        1, { OINTEGER }},
      { NAME_GDBytes | OOPTIONAL,        1, { OINTEGER }},
      { NAME_CIDMapOffset | OOPTIONAL,   1, { OINTEGER }},
      { NAME_GlyphDirectory | OOPTIONAL, 3, { ODICTIONARY,
                                              OARRAY, OPACKEDARRAY }},
      DUMMY_END_MATCH
    };

    if ( fonttype == CIDFONTTYPE0 ) {
      source = fast_extract_hash_name(fdict, NAME_GlyphData)  ;

      /* Need all of FDBytes, GDBytes and CIDMapOffset */
      cidmap_match[cidmap_match_FDBytes].name &= ~OOPTIONAL ;
      cidmap_match[cidmap_match_GDBytes].name &= ~OOPTIONAL ;
      cidmap_match[cidmap_match_CIDMapOffset].name &= ~OOPTIONAL ;
    } else if ( fonttype == CIDFONTTYPE2 ) {
      source = fast_extract_hash_name(fdict, NAME_CIDMap)  ;

      /* May need GDBytes. We'll allow it later if we do. */
      cidmap_match[cidmap_match_FDBytes].name |= OOPTIONAL ;
      cidmap_match[cidmap_match_GDBytes].name |= OOPTIONAL ;
      cidmap_match[cidmap_match_CIDMapOffset].name |= OOPTIONAL ;
    }

    if ( source == NULL )
      return FAILURE(NULL) ;

    switch ( oType(*source) ) {
    case OINTEGER:
      if ( fonttype == CIDFONTTYPE0 ) { /* Font is on disk */
        /* FontFile specifies the source of font data. */
        if ( (source = fast_extract_hash_name(fdict, NAME_FontFile)) == NULL ||
             oType(*source) != OFILE )
          return FAILURE(NULL) ;

        /* FontOffset specifies the starting offset of the font data within
           the file. */
        if ( (theo = fast_extract_hash_name(fdict, NAME_FontOffset)) != NULL ) {
          if ( oType(*theo) != OINTEGER || oInteger(*theo) < 0 )
            return FAILURE(NULL) ;

          fontoffset = (uint32)oInteger(*theo) ;
        }
      } /* else CID type 2 offset map */

      break ;
    case ODICTIONARY: /* CID type 2 downloadable map */
      HQASSERT(fonttype == CIDFONTTYPE2,
               "Wrong GlyphData type in type 0 CID font") ;
      break ;
    case OSTRING:
    case OARRAY:
    case OPACKEDARRAY: /* Font is in memory. Can be CID Type 0 or Type 2 */
      cidmap_match[cidmap_match_GDBytes].name &= ~OOPTIONAL ;
      break ;
    default:
      return FAILURE(NULL) ;
    }

    /* Need to pull out the font's file details first */
    if ( !dictmatch(fdict, cidmap_match) )
      return NULL ;

    /* Rangecheck CIDCount */
    if ( oInteger(*cidmap_match[cidmap_match_CIDCount].result) < 0 )
      return FAILURE(NULL) ;

    /* Rangecheck FDBytes and GDBytes */
    if ( cidmap_match[cidmap_match_FDBytes].result ) {
      int32 temp = oInteger(*cidmap_match[cidmap_match_FDBytes].result) ;
      if ( temp < 0 || temp > 4 )
        return FAILURE(NULL) ;

      fdbytes = (uint32)temp ;
    }

    if ( cidmap_match[cidmap_match_GDBytes].result ) {
      int32 temp = oInteger(*cidmap_match[cidmap_match_GDBytes].result) ;
      /* relax gdbytes=0 - it's been checked already anyway */
      if ( temp < 0 || temp > 4 )
        return FAILURE(NULL) ;

      gdbytes = (uint32)temp ;
    }

    if ( cidmap_match[cidmap_match_CIDMapOffset].result ) {
      int32 temp = oInteger(*cidmap_match[cidmap_match_CIDMapOffset].result) ;
      if ( temp < 0 )
        return FAILURE(NULL) ;

      cidmapoffset = (uint32)temp ;
    }

    /* Don't mind if mm_alloc fails after get_lomemory, the object memory will
       be returned by a restore or GC. Since the source is copied to local
       memory, it will be restored at least as soon as GlyphDirectory. */
    if ( (theo = get_lomemory(1)) == NULL ||
         (cid_map = mm_alloc(mm_pool_temp, sizeof(CIDMAPCACHE),
                             MM_ALLOC_CLASS_CID_MAP)) == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    Copy(theo, source) ;
    cid_map->fid = fid ;
    cid_map->cidcount = (uint32)oInteger(*cidmap_match[cidmap_match_CIDCount].result) ;
    cid_map->fdbytes = fdbytes ;
    cid_map->gdbytes = gdbytes ;
    cid_map->offset = fontoffset + cidmapoffset ;
    cid_map->source = theo ;
    cid_map->glyphdir = cidmap_match[cidmap_match_GlyphDirectory].result ;
    NAME_OBJECT(cid_map, CIDMAP_CACHE_NAME) ;

    map_cache_size += sizeof(CIDMAPCACHE) ;
  }

  cid_map->next = cid_map_cache ;
  cid_map_cache = cid_map ;

  return cid_map ;
}


void cidmap_restore(int32 savelevel)
{
  CIDMAPCACHE *cid_map, **cid_prev = &cid_map_cache ;
  int32 numsaves = NUMBERSAVES(savelevel) ;

  while ( (cid_map = *cid_prev) != NULL ) {
    VERIFY_OBJECT(cid_map, CIDMAP_CACHE_NAME) ;

    /* Test if the data source will be restored */
    if ( cid_map->source == NULL ||
         mm_ps_check(numsaves, cid_map->source) != MM_SUCCESS ) {
      *cid_prev = cid_map->next ;

      mm_free(mm_pool_temp, (mm_addr_t)cid_map, sizeof(CIDMAPCACHE)) ;
      map_cache_size -= sizeof(CIDMAPCACHE) ;
    } else {
      HQASSERT(mm_ps_check(numsaves, cid_map->glyphdir) == MM_SUCCESS,
               "CID map source not restored but glyphdir is") ;
      cid_prev = &cid_map->next ;
    }
  }
}

/* GC scanning for CID cache. Since we are probably in a low memory situation,
   we will clear out the map cache entries. We can safely do this since they
   do not hold a fontdata open between calls. */
static mps_res_t MPS_CALL cidmap_scan(mps_ss_t ss, void *p, size_t s)
{
  CIDMAPCACHE *cid_map ;

  UNUSED_PARAM(void *, p);
  UNUSED_PARAM(size_t, s);
  UNUSED_PARAM(mps_ss_t, ss);

  for ( cid_map = cid_map_cache ; cid_map ; cid_map = cid_map->next ) {
    VERIFY_OBJECT(cid_map, CIDMAP_CACHE_NAME) ;

    cid_map->source = NULL ;
    cid_map->glyphdir = NULL ;
  }

  return MPS_RES_OK ;
}


/** \brief Clear a given quantity of data from the CID map cache.

    \param purge  The amount to purge.
    \return  The amount purged.
 */
static size_t cidmap_purge(size_t purge)
{
  CIDMAPCACHE *cid_map, **cid_prev = &cid_map_cache ;
  size_t orig_size = map_cache_size, level;

  level = orig_size - purge;
  map_cache_size = 0 ;

  /** \todo The CID map list is an MRU list, so it'd be best to purge
      starting from the tail. */
  while ( (cid_map = *cid_prev) != NULL ) {
    VERIFY_OBJECT(cid_map, CIDMAP_CACHE_NAME) ;

    if ( map_cache_size >= level || cid_map->source == NULL ) {
      *cid_prev = cid_map->next ;

      UNNAME_OBJECT(cid_map) ;
      mm_free(mm_pool_temp, (mm_addr_t)cid_map, sizeof(CIDMAPCACHE)) ;
    } else {
      map_cache_size += sizeof(CIDMAPCACHE) ;
      cid_prev = &cid_map->next ;
    }
  }

  HQASSERT((map_cache_size == 0) == (cid_map_cache == NULL) &&
           orig_size >= map_cache_size, "Inconsistent CID Map cache size") ;

  return orig_size - map_cache_size ;
}


void cidmap_cache_clear(void)
{
  (void)cidmap_purge(cidmap_mem_cache.data_size);
}


/** Purge method for \c cidmap_mem_cache. */
static Bool cidmap_mem_purge(mm_simple_cache_t *cache,
                             Bool *purged_something, size_t purge)
{
  UNUSED_PARAM(mm_simple_cache_t *, cache);
  *purged_something = cidmap_purge(purge) != 0;
  return TRUE;
}


/*---------------------------------------------------------------------------*/
/* Font lookup for CID Type 0. CID Type 0 has a subfont selector routine.
   The CID lookup sets the definition either to a string or a special NULL,
   with the length and integer fields as the length and offset of the
   charstring on disk. The subfont select routine sets the charstring type
   and selects the appropriate FDArray entry to get Private data from. */
int32 cid0_lookup_char(FONTinfo *fontInfo, charcontext_t *context)
{
  CIDMAPCACHE *cid_map ;
  int32 cid ;
  uint32 fdindex, offset1, offset2, elementsize ;
  uint8 *mapdata ;
  fontdata_t *font_data ;
  const blobdata_methods_t *fdmethods ;

  HQASSERT(fontInfo, "No selector") ;
  HQASSERT(context, "No context") ;

  HQASSERT(theIFontType(fontInfo) == CIDFONTTYPE0, "Not in a CID Type 0") ;

  /* Set up map cache unconditionally, so we can determine how to look up
     characters. */
  if ( (cid_map = cid_set_map(fontInfo)) == NULL )
    return FALSE ;

  HQASSERT(oType(context->glyphname) == OINTEGER, "Glyph not indexed by CID") ;
  cid = oInteger(context->glyphname) ;

  /* PLRM3 p.373, 378: Characters outside of range of CIDCount are treated as
     undefined glyphs. They should therefore be looked up in the notdef
     mappings in the same way as any other undefined character; this is left
     to the caller to implement. */
  if ( cid < 0 || (uint32)cid >= cid_map->cidcount ) {
    HQTRACE(debug_cidmap,
            ("Lookup overflow in CID Map for %d of %d",
             cid, cid_map->cidcount));
    context->chartype = CHAR_Undefined ;
    return TRUE ;
  }

  /* PLRM3 p. 376: Check for incrementally downloaded CID Type 0 font.
     GlyphDir is either an array or dictionary containing strings with the
     font number (for private data) and character definitions. */
  if ( cid_map->glyphdir != NULL ) {
    OBJECT *theo ;

    switch ( oType(*cid_map->glyphdir) ) {
    case OARRAY: case OPACKEDARRAY:
      if ( cid > theLen(*cid_map->glyphdir) ) {
        HQFAIL("Earlier typecheck mismatch between GlyphDirectory length and CIDCount") ;
        return error_handler(INVALIDFONT) ;
      }
      theo = &oArray(*cid_map->glyphdir)[cid] ;
      break ;
    case ODICTIONARY:
      oInteger(inewobj) = cid ;
      theo = extract_hash(cid_map->glyphdir, &inewobj) ;
      break ;
    default:
      HQFAIL("Unrecognised type of GlyphDirectory") ;
      return error_handler(INVALIDFONT) ;
    }

    if ( theo == NULL || oType(*theo) == ONULL ) {
      context->chartype = CHAR_Undefined ; /* Empty element */
      return TRUE ;
    } else if ( oType(*theo) == OSTRING ) {
      int32 length = theLen(*theo) - cid_map->fdbytes ;

      if ( length < 0 )
        return error_handler(INVALIDFONT) ;

      /* Extract sub-font */
      mapdata = oString(*theo) ;
      fdindex = cidmap_offset(&mapdata, cid_map->fdbytes);

      if ( fdindex > MAXPSARRAY )
        return error_handler(INVALIDFONT) ;

      HQASSERT(mapdata == oString(*theo) + cid_map->fdbytes,
               "Failed to skip FDIndex") ;
      theTags(context->definition) = OSTRING | LITERAL | READ_ONLY ;
      theLen(context->definition) = CAST_TO_UINT16(length) ;
      oString(context->definition) = mapdata ;

      theFDIndex(*fontInfo) = CAST_TO_UINT16(fdindex);

      /* CID Type 0 based on strings */
      context->chartype = CHAR_Type1 ;

      return TRUE ;
    }

    return error_handler(INVALIDFONT) ;
  }

  HQASSERT(cid_map->source, "CID Map source lost") ;

  switch ( oType(*cid_map->source) ) {
  case OSTRING:
    fdmethods = &blobdata_string_methods ;
    break ;
  case OARRAY: case OPACKEDARRAY:
    fdmethods = &blobdata_array_methods ;
    break ;
  case OFILE:
    fdmethods = &blobdata_file_methods ;
    break ;
  default:
    HQFAIL("Unrecognised type of CID Map source") ;
    return error_handler(INVALIDFONT) ;
  }

  /* Not incrementally downloaded, so it's a string, array of strings, or
     disk based CIDMap. Open a fontdata cache to get the offsets of interest.
     We get two indices, because we need to get the offset of this character
     and the offset of the next one to determine the character length. */
  elementsize = cid_map->fdbytes + cid_map->gdbytes;

  if ( (font_data = fontdata_open(cid_map->source, fdmethods)) == NULL )
    return FALSE ;

  if ( (mapdata = fontdata_frame(font_data,
                                 cid_map->offset + elementsize * cid,
                                 elementsize + elementsize,
                                 sizeof(uint8))) == NULL ) {
    fontdata_close(&font_data) ;
    return FALSE ;
  }

  fdindex = cidmap_offset(&mapdata, cid_map->fdbytes) ;
  offset1 = cidmap_offset(&mapdata, cid_map->gdbytes) ;
  mapdata += cid_map->fdbytes ; /* FDIndex of second entry ignored */
  offset2 = cidmap_offset(&mapdata, cid_map->gdbytes) ;

  fontdata_close(&font_data) ;

  if ( fdindex > MAXPSARRAY )
    return error_handler(INVALIDFONT) ;

  if ( offset2 < offset1 || offset2 > offset1 + MAXPSSTRING ) {
    HQTRACE(debug_cidmap, ("Invalid interval for cid %d", cid));
    return error_handler(INVALIDFONT) ;
  }

  offset2 -= offset1 ; /* offset2 is now the charstring length  */
  if ( offset2 == 0 ) { /* Zero size is an empty interval */
    context->chartype = CHAR_Undefined ; /* Empty element */
    return TRUE ;
  }

  /* Ooh, nasty tricks! Storing the charstring length in the object's length
     field allows us to pass both length and offset through in one object,
     saving a lookup in the begin_char routine. */
  theTags(context->definition) = ONULL ;
  theLen(context->definition) = CAST_TO_UINT16(offset2) ;
  oInteger(context->definition) = offset1 ;

  theFDIndex(*fontInfo) = CAST_TO_UINT16(fdindex);

  /* Disk-based CID Type 0 charstrings; leave char type decision until we've
     had a look at the FDArray dictionary, it may override the FontType. */
  context->chartype = CHAR_Undecided ;

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/* Font lookup routine for CID Type 2 TrueType fonts. CID Type 2 fonts
   usually have a VM-based sfnts array containing the data, but the CIDMap
   may be stored in binary data. Subsitution CIDMap for GlyphData and using
   the normal CID mapping code allows us access to this data. */
int32 cid2_lookup_char(FONTinfo *fontInfo, charcontext_t *context)
{
  corecontext_t *corecontext = get_core_context_interp();
  CIDMAPCACHE *cid_map ;
  int32 cid, map ;
  uint8 *mapdata ;
  fontdata_t *font_data ;
  const blobdata_methods_t *fdmethods ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context") ;

  HQASSERT(theFontType(*fontInfo) == CIDFONTTYPE2,
           "Not a CIDFontType 2 when looking up CID definition");

  /* Set up map cache unconditionally, so we can determine how to look up
     characters. */
  if ( (cid_map = cid_set_map(fontInfo)) == NULL )
    return FALSE ;

  HQASSERT(oType(context->glyphname) == OINTEGER, "Glyph not indexed by CID") ;
  cid = oInteger(context->glyphname) ;

  /* PLRM3 p.373, 378: Characters outside of range of CIDCount are treated as
     undefined glyphs. They should therefore be looked up in the notdef
     mappings in the same way as any other undefined character; this is left
     to the caller to implement. */
  if ( cid < 0 || (uint32)cid >= cid_map->cidcount ) {
    HQTRACE(debug_cidmap,
            ("Lookup overflow in CID Map for %d of %d",
             cid, cid_map->cidcount));
    context->chartype = CHAR_Undefined ;
    return TRUE ;
  }

  HQASSERT(cid_map->source, "CID Map source lost") ;

  switch ( oType(*cid_map->source) ) {
    OBJECT *theo ;
  case OSTRING:
    fdmethods = &blobdata_string_methods ;
    break ;
  case OARRAY: case OPACKEDARRAY:
    fdmethods = &blobdata_array_methods ;
    break ;
  case OINTEGER:
    /* TN3011 p.79: CIDMap can be a direct offset from CID to glyph index */
    theTags(context->definition) = OINTEGER ;
    map = cid + oInteger(*cid_map->source) ;

    if (cid == 0 && map == 0 && corecontext->fontsparams->ForceNullMapping)
      map = 1 ;  /* [300510] Force recommendation that chr 0 maps to .null */

    oInteger(context->definition) = map ;

    context->chartype = CHAR_TrueType ;

    return TRUE;
  case ODICTIONARY:
    /* TN3011 p.79: CIDMap can be a dictionary of downloaded values */
    oInteger(inewobj) = cid ;
    if ( (theo = extract_hash(cid_map->source, &inewobj)) == NULL ) {
      context->chartype = CHAR_Undefined ;
      return TRUE ;
    }

    if ( oType(*theo) != OINTEGER )
      return error_handler(INVALIDFONT) ;

    if (cid == 0 && oInteger(*theo) == 0 && corecontext->fontsparams->ForceNullMapping)
      oInteger(*theo) = 1 ;  /* [300510] chr 0 maps to .null */

    Copy(&context->definition, theo) ;

    context->chartype = CHAR_TrueType ;

    return TRUE;
  default:
    HQFAIL("Unrecognised type of CID Map source") ;
    return error_handler(INVALIDFONT) ;
  }

  /* PLRM3 p.378: CIDMap is either a string or array of strings, with packed
     glyph indices. */
  if ( (font_data = fontdata_open(cid_map->source, fdmethods)) == NULL )
    return FALSE ;

  if ( (mapdata = fontdata_frame(font_data,
                                 cid_map->offset + cid_map->gdbytes * cid,
                                 cid_map->gdbytes,
                                 sizeof(uint8))) == NULL ) {
    fontdata_close(&font_data) ;
    return FALSE ;
  }

  theTags(context->definition) = OINTEGER ;
  map = cidmap_offset(&mapdata, cid_map->gdbytes) ;
  if (cid == 0 && map == 0 && corecontext->fontsparams->ForceNullMapping)
    map = 1 ;  /* [300510] chr 0 maps to .null */
  oInteger(context->definition) = map ;

  fontdata_close(&font_data) ;

  context->chartype = CHAR_TrueType ;

  return TRUE;
}

static Bool cidmap_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( mps_root_create(&cidmap_gc_root, mm_arena, mps_rank_exact(),
                        0, cidmap_scan, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}

static void cidmap_finish(void)
{
  mps_root_destroy(cidmap_gc_root) ;
}

void cidmap_C_globals(core_init_fns *fns)
{
  init_C_globals_cidmap() ;

  fns->swstart = cidmap_swstart ;
  fns->finish = cidmap_finish ;
}

/* Log stripped */
