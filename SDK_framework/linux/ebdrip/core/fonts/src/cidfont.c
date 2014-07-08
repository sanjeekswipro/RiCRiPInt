/** \file
 * \ingroup cid
 *
 * $HopeName: COREfonts!src:cidfont.c(EBDSDK_P.1) $:
 *
 * Copyright (C) 1997-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Support for CID Type 0 fonts (using Type 1 charstrings), as per Adobe Tech
 * Spec #5014, Adobe CMap and CIDFont Files Specification, and PostScript
 * Language extensions for CID-Keyed Fonts.
 *
 * The CID Maps are managed by a separate cache in cidmap.c. The term CIDMAP
 * in this file describes the data structure that maps from a CID (character
 * identifier) to a font number and an offset to the charstrings data. This
 * is a property of a CIDFont. It should not be confused with a CMAP, which
 * describes the mapping of input character codes to CIDs.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "fonts.h"
#include "devices.h"
#include "mm.h"
#include "mps.h"
#include "gcscan.h"
#include "namedef_.h"

#include "cidmap.h"
#include "cidfont.h"
#include "cidfont0.h"
#include "charstring12.h" /* This file ONLY implements Type 1/2 charstrings */
#include "fontcache.h"
#include "fontdata.h"

#include "matrix.h"
#include "graphics.h"
#include "showops.h"  /* theSubsCount. Yuck. */
#include "utils.h"    /* is_matrix */
#include "cache_handler.h"


/* Exported definition of the font methods for CID Font Type 0 */
static Bool cid0_select_subfont(FONTinfo *fontInfo,
                                 charcontext_t *context) ;
static Bool cid0_begin_char(FONTinfo *fontInfo,
                             charcontext_t *context) ;
static void cid0_end_char(FONTinfo *fontInfo,
                          charcontext_t *context) ;

font_methods_t font_cid0_fns = {
  fontcache_cid_key,
  cid0_lookup_char,
  cid0_select_subfont,
  cid0_begin_char,
  cid0_end_char
} ;

/* ----------------------------------------------------------------------- */
/* Internal definitions for Type 1 CIDFontType 0 charstring methods. */
static Bool cid0_get_info(void *data, int32 nameid, int32 index, OBJECT *info) ;
static Bool cid0_begin_subr(void *data, int32 subno, int32 global,
                             uint8 **subrstr, uint32 *subrlen) ;
static Bool cid0_begin_seac(void *data, int32 stdindex,
                             uint8 **subrstr, uint32 *subrlen) ;
static void cid0_end_substring(void *data, uint8 **subrstr, uint32 *subrlen) ;

static charstring_methods_t cid0_charstring_fns = {
  NULL,          /* private data (CID0_CACHE) */
  cid0_get_info,
  cid0_begin_subr,
  cid0_end_substring,
  cid0_begin_seac,
  cid0_end_substring
} ;

/* ----------------------------------------------------------------------- */


/* The Charstrings data typically forms the largest part of a CID font - the
   smallest I've come accross is in the region of 1.5 MB.  Until we have a
   better idea about how many CIDFonts get used in a job, and what the access
   patterns are like in the real world, we use a fairly simple cache.
   There are a maximum of CIDDATACACHE_MAX_BLOCKS entries in the cache, each
   one will hold CIDDATACACHE_DEFBSIZE bytes of charstring data, unless either
   the charstring we've been asked for is bigger than this, or the whole data
   section is smaller than this. */

typedef struct CID0_CACHE {
  charstring_methods_t cid0methods ; /* Local copy of cid0_charstring_fns */
  fontdata_t *font_data ;
  const blobdata_methods_t *fdmethods ;
  FONTinfo *fontInfo ;
  int32 fid ;      /* Font identifier */
  OBJECT *source ; /* data source for this font */
  uint32 offset ;  /* offset into data source */
  OBJECT *subrs ;  /* Subrs */
  uint32 subrmapoffset ;
  uint32 subrcount ;
  uint32 sdbytes ;
  struct CID0_CACHE *next ;
  OBJECT_NAME_MEMBER
} CID0_CACHE;

#define CID0_CACHE_NAME "CID 0 font cache"

#if defined( ASSERT_BUILD )
int32 debug_cid = 0;
#endif

static mps_root_t cid0_gc_root ;

/*---------------------------------------------------------------------------*/
/* CID Type 0 structure cache routines. These are used to prevent the CID
   code from having to reload the CID font data for every charstring. The CID
   cache routines use the common font data cache to store charstrings
   routines. The font data pointer is not retained between characters; a new
   instance is opened for each character. */
static CID0_CACHE *cid0_font_cache = NULL ;


/** Low-memory handling data for the CID type 0 font cache. */
mm_simple_cache_t cid0_mem_cache;


/** The size of the entire CID type 0 font cache. */
#define cid0_cache_size (cid0_mem_cache.data_size)


static Bool cid0_mem_purge(mm_simple_cache_t *cache,
                           Bool *purged_something, size_t purge);


static void init_C_globals_cidfont(void)
{
#if defined( ASSERT_BUILD )
  debug_cid = 0 ;
#endif
  cid0_gc_root = NULL ;
  cid0_font_cache = NULL ;
  cid0_mem_cache.low_mem_handler = mm_simple_cache_handler_init;
  cid0_mem_cache.low_mem_handler.name = "CID type 0 font cache";
  cid0_mem_cache.low_mem_handler.tier = memory_tier_disk;
  /* Renderer threads don't touch this, so it's mt-safe. */
  cid0_mem_cache.low_mem_handler.multi_thread_safe = TRUE;
  /* no init needed for offer */
  cid0_mem_cache.data_size = cid0_mem_cache.last_data_size = 0;
  cid0_mem_cache.last_purge_worked = TRUE;
  /* It is fairly trivial to regenerate the CID font header, and most of
     the data is held in the fontdata anyway. */
  cid0_mem_cache.cost = 1.0f;
  cid0_mem_cache.pool = mm_pool_temp;
  cid0_mem_cache.purge = &cid0_mem_purge;
}


/* Create a cache entry for the font in question, and initialise the fontdata
   and font. */
static CID0_CACHE *cid0_set_font(FONTinfo *fontInfo)
{
  CID0_CACHE *cid_font, **cid_prev = &cid0_font_cache ;
  int32 fid ;

  HQASSERT(fontInfo, "No font info") ;
  fid = theCurrFid(*fontInfo) ;

  /* Search for a matching entry in the CID data cache */
  while ( (cid_font = *cid_prev) != NULL ) {
    VERIFY_OBJECT(cid_font, CID0_CACHE_NAME) ;

    /* If this entry was trimmed by a GC scan, remove it */
    if ( cid_font->source == NULL ) {
      *cid_prev = cid_font->next ;
      HQASSERT(cid_font->font_data == NULL,
               "CID font lost source (GC scan?) but has fontdata open") ;
      UNNAME_OBJECT(cid_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)cid_font, sizeof(CID0_CACHE)) ;
      cid0_cache_size -= sizeof(CID0_CACHE) ;
    } else if ( fid == cid_font->fid ) {
      /* Move entry to head of MRU list */
      *cid_prev = cid_font->next ;
      break ;
    } else {
      cid_prev = &cid_font->next ;
    }
  }

  /* If no CID cache found, allocate a new one and initialise the correct
     font entry in it. The object is copied into new object memory, because
     we don't know where the pointer came from. It could be a pointer from
     the C, PostScript or graphics stack, in which case its contents will
     change unpredictably. */
  if ( !cid_font ) {
    uint32 fontoffset = 0 ;
    OBJECT *fdict = &theMyFont(*fontInfo) ;
    OBJECT *theo, *source ;
    const blobdata_methods_t *fdmethods ;

    if ( (source = fast_extract_hash_name(fdict, NAME_GlyphData)) == NULL )
      return NULL ;

    switch ( oType(*source) ) {
    case OINTEGER:
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

      fdmethods = &blobdata_file_methods ;
      break ;
    case OSTRING:
      fdmethods = &blobdata_string_methods ;
      break ;
    case OARRAY:
    case OPACKEDARRAY: /* Font is in memory. Can be CID Type 0 or Type 2 */
      fdmethods = &blobdata_array_methods ;
      break ;
    default:
      return FAILURE(NULL);
    }

    /* Don't mind if mm_alloc fails after get_lomemory, the object memory will
       be returned by a restore or GC. */
    if ( (theo = get_lomemory(1)) == NULL ||
         (cid_font = mm_alloc(mm_pool_temp, sizeof(CID0_CACHE),
                              MM_ALLOC_CLASS_CID_DATA)) == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    Copy(theo, source) ;
    cid_font->fid = fid ;
    cid_font->offset = fontoffset ;
    cid_font->source = theo ;
    cid_font->subrs = NULL ;
    cid_font->subrmapoffset = 0 ;
    cid_font->subrcount = 0 ;
    cid_font->sdbytes = 0 ;
    cid_font->font_data = NULL ;
    cid_font->fdmethods = fdmethods ;
    cid_font->fontInfo = NULL ;
    cid_font->cid0methods = cid0_charstring_fns ;
    cid_font->cid0methods.data = cid_font ;
    NAME_OBJECT(cid_font, CID0_CACHE_NAME) ;

    cid0_cache_size += sizeof(CID0_CACHE) ;
  }

  cid_font->next = cid0_font_cache ;
  cid0_font_cache = cid_font ;

  return cid_font ;
}

/* Clean out cache of CID fonts */
void cid0_restore(int32 savelevel)
{
  CID0_CACHE *cid_font, **cid_prev = &cid0_font_cache ;
  int32 numsaves = NUMBERSAVES(savelevel) ;

  while ( (cid_font = *cid_prev) != NULL ) {
    VERIFY_OBJECT(cid_font, CID0_CACHE_NAME) ;

    /* Test if the data source will be restored */
    if ( cid_font->source == NULL ||
         mm_ps_check(numsaves, cid_font->source) != MM_SUCCESS ) {
      *cid_prev = cid_font->next ;

      HQASSERT(cid_font->font_data == NULL,
               "Font data open when purging CID font") ;
      mm_free(mm_pool_temp, (mm_addr_t)cid_font, sizeof(CID0_CACHE)) ;
      cid0_cache_size -= sizeof(CID0_CACHE) ;
    } else {
      cid_prev = &cid_font->next ;
    }
  }
}

/* GC scanning for CID cache. I would prefer to have a hook to finalisation,
   so we can delete the cache entry when the object is GC'ed. */
static mps_res_t MPS_CALL cid0_scan(mps_ss_t ss, void *p, size_t s)
{
  CID0_CACHE *cid_font ;

  UNUSED_PARAM(void *, p);
  UNUSED_PARAM(size_t, s);

  MPS_SCAN_BEGIN( ss )
    for ( cid_font = cid0_font_cache ; cid_font ; cid_font = cid_font->next ) {
      VERIFY_OBJECT(cid_font, CID0_CACHE_NAME) ;

      /* If we're GC scanning, we are probably in a low-memory situation.
         Mark this font entry as freeable if it's not in use, fix the source
         pointer if it is. The MPS is not reentrant, so we can't actually
         free it now. */
      if ( cid_font->font_data == NULL )
        MPS_SCAN_UPDATE( cid_font->source, NULL );
      else
        /* Fix the font data source objects, so they won't be collected. */
        MPS_RETAIN( &cid_font->source, TRUE );
    }
  MPS_SCAN_END( ss );

  return MPS_RES_OK ;
}


/** \brief Clear a given quantity of data from the CID cache.

    \param purge  The amount to purge.
    \return  The amount purged.

  This won't touch data currently in use, so it may fail to clear as
  much as requested.
 */
static size_t cid0_purge(size_t purge)
{
  CID0_CACHE *cid_font, **cid_prev = &cid0_font_cache ;
  size_t orig_size = cid0_cache_size, level;

  level = orig_size - purge;
  cid0_cache_size = 0 ;

  /** \todo The CID font list is an MRU list, so it'd be best to purge
      starting from the tail. */
  while ( (cid_font = *cid_prev) != NULL ) {
    VERIFY_OBJECT(cid_font, CID0_CACHE_NAME) ;

    /* Purge fonts that are not in use, have been garbage collected, and
       are superfluous to our needs. */
    if ( cid_font->font_data == NULL &&
         (cid0_cache_size >= level || cid_font->source == NULL) ) {
      *cid_prev = cid_font->next ;

      UNNAME_OBJECT(cid_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)cid_font, sizeof(CID0_CACHE)) ;
    } else {
      cid0_cache_size += sizeof(CID0_CACHE) ;
      cid_prev = &cid_font->next ;
    }
  }
  HQASSERT((cid0_cache_size == 0) == (cid0_font_cache == NULL) &&
           orig_size >= cid0_cache_size,
           "Inconsistent CID Type 0 cache size") ;
  return orig_size - cid0_cache_size ;
}


void cid0_cache_clear(void)
{
  (void)cid0_purge(cid0_mem_cache.data_size);
}


/** Purge method for \c cid0_mem_cache. */
static Bool cid0_mem_purge(mm_simple_cache_t *cache,
                           Bool *purged_something, size_t purge)
{
  UNUSED_PARAM(mm_simple_cache_t *, cache);
  *purged_something = cid0_purge(purge) != 0;
  return TRUE;
}


/*---------------------------------------------------------------------------*/
/* CID Font Type 0 charstring definitions. Subroutines allow PostScript
   and lookup disk/string lookup. SEAC is supported. PS Type 1 get_info is
   used because font structure is shared. */
static Bool cid0_begin_subr(void *data, int32 subno, int32 global,
                             uint8 **subrstr, uint32 *subrlen)
{
  CID0_CACHE *cid_font = data ;

  UNUSED_PARAM(int32, global) ;

  HQASSERT(cid_font != NULL, "No CID font") ;
  HQASSERT(subrstr != NULL, "Nowhere for subr string") ;
  HQASSERT(subrlen != NULL, "Nowhere for subr length") ;
  HQASSERT(!global, "CID Type 0 does not support global subrs") ;

  /* Range check the subroutine number. */
  if ( subno < 0 || (uint32)subno >= cid_font->subrcount )
    return FAILURE(FALSE) ;

  /* PLRM3 relaxed the rules about Subrs to allow the old form as well
     as the shared form for CID Fonts */
  if ( cid_font->subrs ) {
    OBJECT *theo = cid_font->subrs ;

    HQASSERT(oType(*theo) == OARRAY || oType(*theo) == OPACKEDARRAY,
             "Subrs is not an array") ;
    HQASSERT(theLen(*theo) == cid_font->subrcount,
             "SubrCount does not match Subrs length") ;

    theo = &oArray(*theo)[subno] ;

    /* Type check the subroutine. */
    if (oType(*theo) != OSTRING )
      return FAILURE(FALSE) ;

    *subrstr = oString(*theo) ;
    *subrlen = theLen(*theo) ;
  } else { /* No Subrs, check the SubrMap */
    uint8 *frame, *mapdata ;
    uint32 offset1, offset2 ;

    HQASSERT(cid_font->font_data, "Font data is not open in CID subr") ;

    /* Read the start and end offsets of the subroutine from the subr map */
    if ( (frame = fontdata_frame(cid_font->font_data,
                                 cid_font->offset + cid_font->subrmapoffset + subno * cid_font->sdbytes,
                                 cid_font->sdbytes + cid_font->sdbytes,
                                 sizeof(uint8))) == NULL )
      return FALSE ;

    mapdata = frame ;
    offset1 = cidmap_offset(&mapdata, cid_font->sdbytes) ;
    offset2 = cidmap_offset(&mapdata, cid_font->sdbytes) ;

    if ( offset2 < offset1 )
      return error_handler(INVALIDFONT) ;

    /* Convert the offsets to a length, and open a frame for the subr */
    *subrlen = offset2 - offset1 ;
    if ( (*subrstr = fontdata_frame(cid_font->font_data,
                                    cid_font->offset + offset1,
                                    *subrlen,
                                    sizeof(uint8))) == NULL )
      return FALSE ;
  }

  return TRUE ;
}

/* CID Type 0 does not support SEAC */
static Bool cid0_begin_seac(void *data, int32 stdindex,
                             uint8 **subrstr, uint32 *subrlen)
{
  UNUSED_PARAM(void *, data) ;
  UNUSED_PARAM(int32, stdindex) ;
  UNUSED_PARAM(uint8 **, subrstr) ;
  UNUSED_PARAM(uint32 *, subrlen) ;

  HQFAIL("SEAC should not appear in a Type 0 CID font") ;

  return error_handler(INVALIDFONT) ;
}

static void cid0_end_substring(void *data, uint8 **subrstr, uint32 *subrlen)
{
  UNUSED_PARAM(void *, data) ;

  *subrstr = NULL ;
  *subrlen = 0 ;
}

/*---------------------------------------------------------------------------*/
/* Font methods for CID Type 0. The lookup routine is handled by the CIDMap
   cache, in cidmap.c. CID Type 0 has a subfont selector routine. The CID
   lookup sets the definition either to a string or a special NULL, with the
   length and integer fields as the length and offset of the charstring on
   disk. The subfont select routine sets the charstring type and selects the
   appropriate FDArray entry to get Private data from. It is like set_font(),
   but only called when the gstate has just been given a font dictionary from
   the FDArray of a Type 0 CIDFont. Unlike set_font, it also concatenates the
   matrix from the new font onto the current fontmatrix. */
static Bool cid0_get_info(void *data, int32 nameid, int32 index, OBJECT *info)
{
  CID0_CACHE *cid_font = data ;

  HQASSERT(cid_font, "No CID font info") ;

  return ps1_get_info(cid_font->fontInfo, nameid, index, info) ;
}


static Bool set_cid_subfont(FONTinfo *fontInfo, OBJECT *dict)
{
  CID0_CACHE *cid_font ;
  OBJECT *theo;
  OMATRIX matrix ;

  enum { mch_subdict_Private, mch_subdict_FontMatrix, mch_subdict_dummy };
  static NAMETYPEMATCH mch_subdict[mch_subdict_dummy + 1] = {
    /* Use the enum above to index these */
    { NAME_Private,              1, { ODICTIONARY }},
    { NAME_FontMatrix,           2, { OARRAY, OPACKEDARRAY }},
    DUMMY_END_MATCH
  };

  enum { mch_private_Subrs, mch_private_SubrMapOffset, mch_private_SubrCount,
         mch_private_SDBytes, mch_private_RunInt, mch_private_dummy };
  static NAMETYPEMATCH mch_private[mch_private_dummy + 1] = {
    /* Use the enum above to index these */
    { NAME_Subrs | OOPTIONAL,         2, { OARRAY, OPACKEDARRAY }},
    { NAME_SubrMapOffset | OOPTIONAL, 1, { OINTEGER }},
    { NAME_SubrCount | OOPTIONAL,     1, { OINTEGER }},
    { NAME_SDBytes | OOPTIONAL,       1, { OINTEGER }},
    { NAME_RunInt | OOPTIONAL,        1, { ONAME }},
    DUMMY_END_MATCH
  };

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(dict, "No CID sub-dict") ;

  if ( (cid_font = cid0_set_font(fontInfo)) == NULL )
    return FALSE ;

  VERIFY_OBJECT(cid_font, CID0_CACHE_NAME) ;

  /* Extract the necessary font parameters */
  if ( !dictmatch(dict, mch_subdict) ||
       !dictmatch(mch_subdict[mch_subdict_Private].result, mch_private) )
    return error_handler(INVALIDFONT);

  if ( mch_private[mch_private_Subrs].result ) {
    cid_font->subrs = mch_private[mch_private_Subrs].result ;
    cid_font->subrcount = theLen(*cid_font->subrs) ;
    cid_font->subrmapoffset = cid_font->sdbytes = 0 ;
  } else {
    /* No Subrs, look for SubrMap details. We don't enforce requiring the
       offsets if there is no SubrCount or it is zero. */
    cid_font->subrs = NULL ;

    if ( (theo = mch_private[mch_private_SubrCount].result) != NULL ) {
      if ( oInteger(*theo) < 0 )
        return error_handler(INVALIDFONT) ;

      cid_font->subrcount = (uint32)oInteger(*theo) ;
    }

    if ( (theo = mch_private[mch_private_SDBytes].result) != NULL ) {
      if ( oInteger(*theo) < 0 || oInteger(*theo) > 4 )
        return error_handler(INVALIDFONT) ;

      cid_font->sdbytes = (uint32)oInteger(*theo) ;
    }

    if ( (theo = mch_private[mch_private_SubrMapOffset].result) != NULL ) {
      if ( oInteger(*theo) < 0 )
        return error_handler(INVALIDFONT) ;

      cid_font->subrmapoffset = (uint32)oInteger(*theo) ;
    }
 }

  /* No Encoding array. */
  theTags(theEncoding(*fontInfo)) = ONULL;

  /* No charstring dictionary. */
  theCharStrings(*fontInfo) = NULL;

  /* Metrics dictionary will be inherited from the top level CIDFont, so don't
     do anything with theMetrics and theMetrics2. */

  /* Test if it's a Morisawa or ATL encypted CID font */
  isEncrypted(*fontInfo) = PROTECTED_NONE ;
  theo = mch_private[mch_private_RunInt].result;
  if ( theo ) {
    if ( oName(*theo) == &system_names[ NAME_SpecialRun ] )
      isEncrypted(*fontInfo) = PROTECTED_MRSWA ;
    else if ( oName(*theo) == &system_names[ NAME_eCCRun ] )
      isEncrypted(*fontInfo) = PROTECTED_ATL ;
  }

  theSubsCount(*fontInfo) = 0;

  if ( !is_matrix(mch_subdict[mch_subdict_FontMatrix].result, &matrix) )
    return FALSE;

  /* Multiply this by the current fontmatrix to give a new one */
  matrix_mult(&matrix, &theFontMatrix(*fontInfo), &theFontMatrix(*fontInfo));
  matrix_mult(&matrix, &theFontCompositeMatrix(*fontInfo),
              &theFontCompositeMatrix(*fontInfo));

  /* gotFontMatrix is still set, from the set_font call preceding this. Leave
     theLookupFont set if it was already, since the FID in the parent
     dictionary is still valid. The matrix has changed though, so clear the
     lookup matrix. */
  theLookupMatrix(*fontInfo) = NULL ;

  /* Finally, install the FDArray subdict as the source for Private
     information */
  Copy(&fontInfo->subfont, dict) ;

  return TRUE;
}


static Bool cid0_select_subfont(FONTinfo *fontInfo,
                                 charcontext_t *context)
{
  OBJECT *fdict, *theo ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context") ;

  HQASSERT(theIFontType(fontInfo) == CIDFONTTYPE0, "Not in a CID Type 0") ;

  /* Now we've got the font number, check that it's within the bounds of the
     FDArray and have a go at setting the fontinfo */

  /* Extract the FD_Array. */
  theo = fast_extract_hash_name(&theMyFont(*fontInfo), NAME_FDArray);
  if ( !theo )
    return error_handler(INVALIDFONT);

  if ( oType(*theo) != OARRAY &&
       oType(*theo) != OPACKEDARRAY )
    return error_handler(INVALIDFONT);

  if ( theFDIndex(*fontInfo) >= theLen(*theo) )
    return error_handler(INVALIDFONT);

  /* And finally get our hands on the font dictionary */
  fdict = oArray(*theo) + theFDIndex(*fontInfo) ;
  if ( oType(*fdict) != ODICTIONARY )
    return error_handler(INVALIDFONT);

  /* Then do the necessary for the sub-dictionary. Currently, this
     concatenates the sub fonts matrix onto the font matrix, and sets the
     charstring type. */
  if ( !set_cid_subfont(fontInfo, fdict) )
    return FALSE ;

  context->chartype = CHAR_Type1 ;

  return TRUE ;
}

/* If a character is on disk, pull it into the CID data cache. */
static Bool cid0_begin_char(FONTinfo *fontInfo,
                            charcontext_t *context)
{
  CID0_CACHE *cid_font ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No character context") ;

  HQASSERT(theIFontType(fontInfo) == CIDFONTTYPE0, "Not in a CID Type 0") ;
  HQASSERT(context->chartype == CHAR_Type1 ||
           context->chartype == CHAR_Type2, "Not a Type 1/2 charstring") ;

  if ( (cid_font = cid0_set_font(fontInfo)) == NULL )
    return FALSE ;

  VERIFY_OBJECT(cid_font, CID0_CACHE_NAME) ;

  if ( (cid_font->font_data = fontdata_open(cid_font->source,
                                            cid_font->fdmethods)) == NULL )
    return FALSE ;

  if ( oType(context->definition) == ONULL ) {
    /* cid0_lookup_char creates a special ONULL object containing an integer
       offset and a length. This is now converted into a string by opening a
       fontdata frame. */
    if ( (oString(context->definition) =
          fontdata_frame(cid_font->font_data,
                         cid_font->offset + oInteger(context->definition),
                         theLen(context->definition),
                         sizeof(uint8))) == NULL ) {
      fontdata_close(&cid_font->font_data) ;
      return FALSE ;
    }

    theTags(context->definition) = OSTRING | LITERAL | READ_ONLY ;
  }

  HQASSERT(oType(context->definition) == OSTRING,
           "Definition of CID character is wrong type") ;

  context->methods = &cid_font->cid0methods ;

  /* cid0_get_info is based on ps1_get_info, which uses the fontInfo to
     extract information. Save the current font info between begin_char and
     end_char; this means we cannot call CID0 fonts recursively. */
  HQASSERT(cid_font->fontInfo == NULL, "Should not call CID0 recursively.") ;
  cid_font->fontInfo = fontInfo ;

  return TRUE ;
}

static void cid0_end_char(FONTinfo *fontInfo,
                          charcontext_t *context)
{
  CID0_CACHE *cid_font ;
  charstring_methods_t *cid0methods ;

  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(context, "No context object") ;

  cid0methods = context->methods ;
  HQASSERT(cid0methods, "CID Type 0 charstring methods lost") ;

  cid_font = cid0methods->data ;
  VERIFY_OBJECT(cid_font, CID0_CACHE_NAME) ;
  HQASSERT(cid0methods == &cid_font->cid0methods,
           "CID charstring methods inconsistent with font structure") ;

  fontdata_close(&cid_font->font_data) ;

  HQASSERT(fontInfo == cid_font->fontInfo,
           "Font info does not match at end of CID0 character") ;
  cid_font->fontInfo = NULL ;

  object_store_null(&context->definition) ;
}

static Bool cid0_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( mps_root_create(&cid0_gc_root, mm_arena, mps_rank_exact(),
                        0, cid0_scan, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}

static void cid0_finish(void)
{
  mps_root_destroy(cid0_gc_root) ;
}

void cid0_C_globals(core_init_fns *fns)
{
  init_C_globals_cidfont() ;

  fns->swstart = cid0_swstart ;
  fns->finish = cid0_finish ;
}


/* Log stripped */
