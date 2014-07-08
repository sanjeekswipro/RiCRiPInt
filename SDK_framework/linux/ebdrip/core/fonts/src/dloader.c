/** \file
 * \ingroup dld1
 *
 * $HopeName: COREfonts!src:dloader.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * VERSION DLD.1 Font format loader
 *
 * The DLD1 font format is a dynamically-loaded file-based version of the
 * Type 1 font format. DLD1 is a Harlequin/Global Graphics invention. The
 * DLD1 font file may be encrypted using the HQX crypt method, for extra
 * security. The DLD1 font format is described in the document
 * "ScriptWorks Information\Development\Core RIP\DLD1 Font Format"
 */

#include "core.h"
#include "coreinit.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "fonts.h"
#include "fontcache.h"
#include "mm.h"
#include "namedef_.h"

#include "graphics.h"
#include "adobe1.h"

#include "dloader.h"
#include "fcache.h"
#include "encoding.h"
#include "fontdata.h"

#include "charstring12.h" /* This file ONLY implements Type 1 charstrings */
#include "cache_handler.h"


#define DLD_HEADER_LEN   8 /* hoff, version, numchars */
#define DLD_NAME_LEN    32
#define DLD_COMMENT_LEN 64

/* #define debug_DLOADER */

/* Exported definition of the font methods for VM-based Type 1 fonts */
static Bool dld1_lookup_char(FONTinfo *fontInfo,
                             charcontext_t *context) ;
static Bool dld1_begin_char(FONTinfo *fontInfo,
                            charcontext_t *context) ;
static void dld1_end_char(FONTinfo *fontInfo,
                          charcontext_t *context) ;

font_methods_t font_dld1_fns = {
  fontcache_base_key,
  dld1_lookup_char,
  NULL, /* No subfont lookup */
  dld1_begin_char,
  dld1_end_char
} ;

/* --- Internal Functions --- */

/* Internal definition of Type 1 charstring methods */
static Bool dld1_get_info(void *data, int32 nameid, int32 index, OBJECT *info) ;
static Bool dld1_begin_subr(void *data, int32 subno, Bool global,
                            uint8 **subrstr, uint32 *subrlen) ;
static Bool dld1_begin_seac(void *data, int32 stdindex,
                            uint8 **subrstr, uint32 *subrlen) ;
static void dld1_end_substring(void *data, uint8 **subrstr, uint32 *subrlen) ;

static charstring_methods_t dld1_charstring_fns = {
  NULL,          /* private data (DLD_CACHE) */
  dld1_get_info,
  dld1_begin_subr,
  dld1_end_substring,
  dld1_begin_seac,
  dld1_end_substring
} ;

/** DLD structure cache type. This is used to prevent the DLD code from having
   to reload the DLD font data for every charstring. The DLD cache routines
   use the common font data cache to store charstrings routines. The font
   data pointer is not retained between characters; a new instance is opened
   for each character. */
typedef struct DLD_CACHE {
  charstring_methods_t dldmethods ; /* Local copy of dld_charstring_fns */
  fontdata_t *font_data ;
  const blobdata_methods_t *fdmethods ;
  FONTinfo *fontInfo ;
  int32 fid ;      /* Font identifier */
  OBJECT *source ; /* data source for this font */
  uint32 offset ;  /* offset into data source */
  uint8 protection ; /* Encryption type */
  uint32 numcharsize ;
  int32 subroffset ;
  struct DLD_CACHE *next ;
  OBJECT_NAME_MEMBER
} DLD_CACHE ;

#define DLD_CACHE_NAME "DLD cache"

static Bool retdld_GetChar(DLD_CACHE *dld_font,
                           uint32 offset,
                           uint8 **slist,
                           uint32 *slength);

static mps_root_t dld_gc_root ;

/* ----------------------------------------------------------------------------
   function:            dld_GetChar(..)       author:              Andrew Cave
   creation date:       07-Jun-1989           last modification:   ##-###-####
   arguments:           ccode.
   description:

   Returns the encrypted outline of the required character.

---------------------------------------------------------------------------- */
static Bool dld_GetChar(DLD_CACHE *dld_font,
                        register int32 ccode, uint8 **slist, uint32 *slength)
{
  register int32 istart , icurr , iend ;
  register uint8 *start , *end , *curr, *frame ;

  HQASSERT(dld_font, "No DLD font") ;

  if ( (frame = fontdata_frame(dld_font->font_data,
                               DLD_HEADER_LEN + DLD_NAME_LEN + DLD_COMMENT_LEN + dld_font->offset,
                               dld_font->numcharsize,
                               sizeof(uint8))) == NULL )
    return FALSE ;

/* Binary subdivision to look up character. */
  start = frame ;
  end = start + dld_font->numcharsize ;

/* Quick guess. */
  if ( ccode < (end - start ) >> 1 ) {
    curr = start + ( ccode << 1 ) ;
    icurr = (curr[0] << 8) | curr[1] ;
    if ( icurr == ccode )
      return retdld_GetChar(dld_font, CAST_PTRDIFFT_TO_UINT32(curr - frame), slist, slength) ;
  }

  istart = (start[0] << 8) | start[1] ;
  if ( istart == ccode )
    return retdld_GetChar(dld_font, CAST_PTRDIFFT_TO_UINT32(start - frame), slist, slength) ;

/* Check if end is equal. */
  iend = (end[0] << 8) | end[1] ;
  if ( iend == ccode )
    return retdld_GetChar(dld_font, CAST_PTRDIFFT_TO_UINT32(end - frame), slist, slength) ;

/* When less than or one char header left, can't have found it. */
  while ( end - start > 2 ) {
    curr = start + (((end - start) >> 2) << 1) ;

    icurr = (curr[0] << 8) | curr[1] ;
    if ( icurr == ccode )
      return retdld_GetChar(dld_font, CAST_PTRDIFFT_TO_UINT32(curr - frame), slist, slength) ;

/* Use bottom half of table. */
    if ( ccode < icurr ) {
      end = curr ;
      iend = icurr ;
    }
/* Use top half of table. */
    else {
      start = curr ;
      istart = icurr ;
    }
  }

  /* Not found */
  return FAILURE(FALSE) ;
}

static Bool retdld_GetChar(DLD_CACHE *dld_font,
                           uint32 offset,
                           uint8 **slist,
                           uint32 *slength)
{
  register uint32 itemp ;

  uint8 *frame ;

  /* The offset input is the number of bytes from the start of the char index
     table to the charstring entry found. The index table is two bytes per
     entry, the offset table is four bytes per entry, so this needs to be
     doubled to get the offset table address. */
  offset <<= 1 ;

  if ( (frame = fontdata_frame(dld_font->font_data,
                               DLD_HEADER_LEN + DLD_NAME_LEN + DLD_COMMENT_LEN +
                               dld_font->numcharsize + dld_font->offset + offset,
                               4,
                               sizeof(uint8))) == NULL )
    return FALSE ;

  itemp = (frame[0] << 24) | (frame[1] << 16) | (frame[2] << 8) | frame[3] ;
  itemp += dld_font->offset ;

  /* Charstring offsets are from the start of the hoff section. Charstrings
     start with two bytes of length data. */
  if ( (frame = fontdata_frame(dld_font->font_data, itemp, 2,
                               sizeof(uint8))) == NULL )
    return FALSE ;

  *slength = (frame[0] << 8) | frame[1] ;

  /* Finally extract a frame for the charstring. */
  if ( (*slist = fontdata_frame(dld_font->font_data, itemp + 2, *slength,
                                sizeof(uint8))) == NULL )
    return FALSE ;

  return TRUE ;
}

static Bool dld_initFont(DLD_CACHE *dld_font)
{
  uint8 *frame ;

  if ( (frame = fontdata_frame(dld_font->font_data,
                               dld_font->offset, DLD_HEADER_LEN,
                               sizeof(uint8))) == NULL )
    return FALSE ;

  /* Check file type & version number. */
  if ( frame[0] != 'h' ||
       frame[1] != 'o' ||
       frame[2] != 'f' ||
       frame[3] != 'f' ||
       frame[4] != 0 ||
       frame[5] != 2 )
    return FAILURE(FALSE) ;

  /* The size of the char index array is the number of chars times 2. */
  dld_font->numcharsize = (frame[6] << 9) | (frame[7] << 1) ;

  return TRUE ;
}

/*---------------------------------------------------------------------------*/
/* DLD structure cache routines. These are used to prevent the DLD code from
   having to reload the DLD font data for every charstring. At the moment,
   they only allow one DLD to be loaded. The DLD cache routines use the common
   font data cache to store charstrings routines. The font data pointer is
   not retained between characters; a new instance is opened for each
   character. */
static DLD_CACHE *dld_font_cache = NULL ;


/** Low-memory handling data for the DLD font cache. */
mm_simple_cache_t dld_mem_cache;


/** The size of the entire DLD font cache. */
#define dld_cache_size (dld_mem_cache.data_size)


static Bool dld_mem_purge(mm_simple_cache_t *cache,
                          Bool *purged_something, size_t purge);


static void init_C_globals_dloader(void)
{
  dld_gc_root = NULL ;
  dld_font_cache = NULL ;
  dld_mem_cache.low_mem_handler = mm_simple_cache_handler_init;
  dld_mem_cache.low_mem_handler.name = "DLD font cache";
  dld_mem_cache.low_mem_handler.tier = memory_tier_disk;
  /* Renderer threads don't touch this, so it's mt-safe. */
  dld_mem_cache.low_mem_handler.multi_thread_safe = TRUE;
  /* no init needed for offer */
  dld_mem_cache.data_size = dld_mem_cache.last_data_size = 0;
  dld_mem_cache.last_purge_worked = TRUE;
  /* DLD fonts are fairly easy to reconstruct. */
  dld_mem_cache.cost = 1.0f;
  dld_mem_cache.pool = mm_pool_temp;
  dld_mem_cache.purge = &dld_mem_purge;
}

/** Create a cache entry for the font in question, and initialise the fontdata
   and font set. */
static DLD_CACHE *dld_set_font(FONTinfo *fontInfo)
{
  DLD_CACHE *dld_font, **dld_prev = &dld_font_cache ;
  int32 fid ;

  HQASSERT(fontInfo, "No font info") ;
  fid = theCurrFid(*fontInfo) ;

  /* Search for a matching entry in the DLD data cache */
  while ( (dld_font = *dld_prev) != NULL ) {
    VERIFY_OBJECT(dld_font, DLD_CACHE_NAME) ;

    /* If this entry was trimmed by a GC scan, remove it */
    if ( dld_font->source == NULL ) {
      *dld_prev = dld_font->next ;
      HQASSERT(dld_font->font_data == NULL,
               "DLD font lost source (GC scan?) but has fontdata open") ;
      UNNAME_OBJECT(dld_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)dld_font, sizeof(DLD_CACHE)) ;
      dld_cache_size -= sizeof(DLD_CACHE) ;
    } else if ( fid == dld_font->fid ) {
      /* Move entry to head of MRU list */
      *dld_prev = dld_font->next ;
      break ;
    } else {
      dld_prev = &dld_font->next ;
    }
  }

  /* If no DLD cache found, allocate a new one and initialise the correct
     font entry in it. The object is copied into new object memory, because
     we don't know where the pointer came from. It could be a pointer from
     the C, PostScript or graphics stack, in which case its contents will
     change unpredictably. */
  if ( !dld_font ) {
    uint32 fontoffset = 0 ;
    int32 subroffset = 0 ;
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

    /* Lookup the dictionary "Private" from the current font. */
    if ( (theo = fast_extract_hash_name(&fontInfo->subfont, NAME_Private)) != NULL ) {
      /* Lookup the array "Subrs" from the Private dictionary. */
      if ( (theo = fast_extract_hash_name(theo, NAME_Subrs)) != NULL ) {
        /* Subrs is an offset into the DLD1 char index array */
        if ( oType(*theo) != OINTEGER ) {
          (void)error_handler(INVALIDFONT) ;
          return NULL ;
        }
        subroffset = oInteger(*theo) ;
      }
    }

    /* Don't mind if mm_alloc fails after get_lomemory, the object memory will
       be returned by a restore or GC. */
    if ( (theo = get_lomemory(1)) == NULL ||
         (dld_font = mm_alloc(mm_pool_temp, sizeof(DLD_CACHE),
                              MM_ALLOC_CLASS_CID_DATA)) == NULL ) {
      (void)error_handler(VMERROR) ;
      return NULL ;
    }

    Copy(theo, fontfile) ;
    dld_font->fid = fid ;
    dld_font->offset = fontoffset ;
    dld_font->protection = PROTECTED_NONE ;
    dld_font->subroffset = subroffset ;
    dld_font->source = theo ;
    dld_font->fdmethods = &blobdata_file_methods ;
    dld_font->fontInfo = NULL ;
    dld_font->dldmethods = dld1_charstring_fns ;
    dld_font->dldmethods.data = dld_font ;
    NAME_OBJECT(dld_font, DLD_CACHE_NAME) ;

    if ( (dld_font->font_data = fontdata_open(dld_font->source,
                                              dld_font->fdmethods)) == NULL ) {
      UNNAME_OBJECT(dld_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)dld_font, sizeof(DLD_CACHE)) ;
      return NULL ;
    }

    dld_font->protection = fontdata_protection(dld_font->source,
                                               dld_font->fdmethods) ;

    if ( !dld_initFont(dld_font) ) {
      fontdata_close(&dld_font->font_data) ;
      UNNAME_OBJECT(dld_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)dld_font, sizeof(DLD_CACHE)) ;
      return NULL ;
    }

    fontdata_close(&dld_font->font_data) ;

    dld_cache_size += sizeof(DLD_CACHE) ;
  }

  if ( dld_font->protection ) {
    if ( !isEncrypted(*fontInfo) )
      isEncrypted(*fontInfo) = dld_font->protection ;
    else if ( dld_font->protection != isEncrypted(*fontInfo) )
      isEncrypted(*fontInfo) = PROTECTED_BLANKET;
  }

  dld_font->next = dld_font_cache ;
  dld_font_cache = dld_font ;

  return dld_font ;
}

/* Clean out cache of DLD fonts */
void dld_restore(int32 savelevel)
{
  DLD_CACHE *dld_font, **dld_prev = &dld_font_cache ;
  int32 numsaves = NUMBERSAVES(savelevel) ;

  while ( (dld_font = *dld_prev) != NULL ) {
    VERIFY_OBJECT(dld_font, DLD_CACHE_NAME) ;

    /* Test if the data source will be restored */
    if ( dld_font->source == NULL ||
         mm_ps_check(numsaves, dld_font->source) != MM_SUCCESS ) {
      *dld_prev = dld_font->next ;

      HQASSERT(dld_font->font_data == NULL,
               "Font data open when purging DLD font") ;
      mm_free(mm_pool_temp, (mm_addr_t)dld_font, sizeof(DLD_CACHE)) ;
      dld_cache_size -= sizeof(DLD_CACHE) ;
    } else {
      dld_prev = &dld_font->next ;
    }
  }
}

/** GC scanning for DLD cache. I would prefer to have a hook to finalisation,
   so we can delete the cache entry when the object is GC'ed. */
static mps_res_t MPS_CALL dld_scan(mps_ss_t ss, void *p, size_t s)
{
  DLD_CACHE *dld_font ;

  UNUSED_PARAM(void *, p);
  UNUSED_PARAM(size_t, s);

  MPS_SCAN_BEGIN( ss )
    for ( dld_font = dld_font_cache ; dld_font ; dld_font = dld_font->next ) {
      VERIFY_OBJECT(dld_font, DLD_CACHE_NAME) ;

      /* If we're GC scanning, we are probably in a low memory situation.
         Mark this font entry as freeable if it's not in use, fix the source
         pointer if it is. The MPS is not reentrant, so we can't actually
         free it now. */
      if ( dld_font->font_data == NULL )
        MPS_SCAN_UPDATE( dld_font->source, NULL );
      else
        /* Fix the font data source objects, so they won't be collected. */
        MPS_RETAIN( &dld_font->source, TRUE );
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
static size_t dld_purge(size_t purge)
{
  DLD_CACHE *dld_font, **dld_prev = &dld_font_cache ;
  size_t orig_size = dld_cache_size, level;

  level = orig_size - purge;
  dld_cache_size = 0 ;

  while ( (dld_font = *dld_prev) != NULL ) {
    VERIFY_OBJECT(dld_font, DLD_CACHE_NAME) ;

    /* Purge fonts that are not in use, have been garbage collected, and
       are superfluous to our needs. */
    if ( dld_font->font_data == NULL &&
         (dld_cache_size >= level || dld_font->source == NULL) ) {
      *dld_prev = dld_font->next ;

      UNNAME_OBJECT(dld_font) ;
      mm_free(mm_pool_temp, (mm_addr_t)dld_font, sizeof(DLD_CACHE)) ;
    } else {
      dld_cache_size += sizeof(DLD_CACHE) ;
      dld_prev = &dld_font->next ;
    }
  }

  HQASSERT((dld_cache_size == 0) == (dld_font_cache == NULL) &&
           orig_size >= dld_cache_size,
           "Inconsistent DLD cache size") ;

  return orig_size - dld_cache_size ;
}


void dld_cache_clear(void)
{
  (void)dld_purge(dld_mem_cache.data_size);
}


/** Purge method for \c dld_mem_cache. */
static Bool dld_mem_purge(mm_simple_cache_t *cache,
                          Bool *purged_something, size_t purge)
{
  UNUSED_PARAM(mm_simple_cache_t *, cache);
  *purged_something = dld_purge(purge) != 0;
  return TRUE;
}


/*---------------------------------------------------------------------------*/

static Bool dld1_begin_subr(void *data, int32 subno, Bool global,
                            uint8 **subrstr, uint32 *subrlen)
{
  DLD_CACHE *dld_font = data ;

  UNUSED_PARAM(int32, global) ;

  HQASSERT(subrstr != NULL, "Nowhere for subr string") ;
  HQASSERT(subrlen != NULL, "Nowhere for subr length") ;
  HQASSERT(!global, "Global subr in DLD1 font") ;
  HQASSERT(dld_font, "No DLD font") ;

  return dld_GetChar(dld_font, subno + dld_font->subroffset, subrstr, subrlen) ;
}

static Bool dld1_begin_seac(void *data, int32 stdindex,
                            uint8 **subrstr, uint32 *subrlen)
{
  DLD_CACHE *dld_font = data ;
  OBJECT *theo ;
  FONTinfo *fontInfo ;

  HQASSERT(dld_font, "No DLD font") ;

  fontInfo = dld_font->fontInfo ;
  HQASSERT(fontInfo != NULL, "No font info") ;
  HQASSERT(subrstr != NULL, "Nowhere for subr string") ;
  HQASSERT(subrlen != NULL, "Nowhere for subr length") ;
  HQASSERT(FONT_IS_DLD1(theIFontType(fontInfo)), "Font is not DLD1") ;

  if ( stdindex < 0 || stdindex >= NUM_ARRAY_ITEMS(StandardEncoding) )
    return error_handler(RANGECHECK) ;

  oName(nnewobj) = StandardEncoding[stdindex] ;
  if ( NULL == (theo = extract_hash(theCharStrings(*fontInfo), &nnewobj)))
    return FALSE ;

  if (oType(*theo) != OINTEGER )
    return FAILURE(FALSE) ;

  return dld_GetChar(dld_font, oInteger(*theo), subrstr, subrlen) ;
}

static void dld1_end_substring(void *data, uint8 **subrstr, uint32 *subrlen)
{
  UNUSED_PARAM(void *, data) ;

  *subrstr = NULL ;
  *subrlen = 0 ;
}

/*---------------------------------------------------------------------------*/
/** Font lookup and top-level charstring routines for VM-based Type 1 fonts */
static Bool dld1_get_info(void *data, int32 nameid, int32 index, OBJECT *info)
{
  DLD_CACHE *dld_font = data ;

  HQASSERT(dld_font, "No DLD font info") ;

  return ps1_get_info(dld_font->fontInfo, nameid, index, info) ;
}

static Bool dld1_lookup_char(FONTinfo *fontInfo,
                             charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(fontInfo, "No selector") ;
  HQASSERT(context, "No  context") ;

  HQASSERT(FONT_IS_DLD1(theIFontType(fontInfo)), "Not in a DLD1 font") ;

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
    context->chartype = CHAR_Type1 ;
    break ;
  default:
    return error_handler(INVALIDFONT) ;
  }

  return TRUE ;
}

/** Determine if the named char exists in the font, and whether it has been
   replaced by a procedure. */
static Bool dld1_begin_char(FONTinfo *fontInfo,
                            charcontext_t *context)
{
  DLD_CACHE *dld_font ;
  uint8 *slist ;
  uint32 slen ;

  HQASSERT(context, "No char context") ;

  HQASSERT(FONT_IS_DLD1(theIFontType(fontInfo)), "Not in a DLD1 font") ;

  if ( context->chartype == CHAR_BuildChar )
    return (*font_type3_fns.begin_char)(fontInfo, context) ;

  HQASSERT(context->chartype == CHAR_Type1, "DLD1 string isn't Type 1") ;

  if ( (dld_font = dld_set_font(fontInfo)) == NULL )
    return FALSE ;

  VERIFY_OBJECT(dld_font, DLD_CACHE_NAME) ;

  HQASSERT(oType(context->definition) == OINTEGER,
           "Character definition is not glyph index") ;

  if ( (dld_font->font_data = fontdata_open(dld_font->source,
                                            dld_font->fdmethods)) == NULL )
    return FALSE ;

  /* This opens a fontdata frame, so it needs the font data open. */
  if ( !dld_GetChar(dld_font, oInteger(context->definition), &slist, &slen) ) {
    fontdata_close(&dld_font->font_data) ;
    return FALSE ;
  }

  /* DLD1 character lengths are a two-byte field */
  HQASSERT(slen <= MAXPSSTRING, "DLD1 character too long") ;

  theTags(context->definition) = OSTRING | LITERAL | READ_ONLY ;
  theLen(context->definition) = CAST_TO_UINT16(slen) ;
  oString(context->definition) = slist ;

  context->methods = &dld_font->dldmethods ;

  /* dld_get_info is based on ps1_get_info, which uses the fontInfo to
     extract information. Save the current font info between begin_char and
     end_char; this means we cannot call DLD fonts recursively. */
  HQASSERT(dld_font->fontInfo == NULL, "Should not call DLD recursively.") ;
  dld_font->fontInfo = fontInfo ;

  return TRUE ;
}

static void dld1_end_char(FONTinfo *fontInfo,
                          charcontext_t *context)
{
  DLD_CACHE *dld_font ;
  charstring_methods_t *dldmethods ;

  HQASSERT(context, "No context object") ;

  if ( context->chartype == CHAR_BuildChar ) {
    (*font_type3_fns.end_char)(fontInfo, context) ;
    return ;
  }

  dldmethods = context->methods ;
  HQASSERT(dldmethods, "DLD1 charstring methods lost") ;

  dld_font = dldmethods->data ;
  VERIFY_OBJECT(dld_font, DLD_CACHE_NAME) ;
  HQASSERT(dldmethods == &dld_font->dldmethods,
           "DLD charstring methods inconsistent with font structure") ;

  /* Release fontdata frame and then fontdata */
  fontdata_close(&dld_font->font_data) ;

  HQASSERT(fontInfo == dld_font->fontInfo,
           "Font info does not match at end of DLD character") ;
  dld_font->fontInfo = NULL ;

  /* Clear out string data, it's no longer allocated. */
  object_store_null(&context->definition) ;
}

static Bool dld_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( mps_root_create(&dld_gc_root, mm_arena, mps_rank_exact(),
                        0, dld_scan, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}

static void dld_finish(void)
{
  mps_root_destroy(dld_gc_root) ;
}

void dld_C_globals(core_init_fns *fns)
{
  init_C_globals_dloader() ;

  fns->swstart = dld_swstart ;
  fns->finish = dld_finish ;
}


/*
Log stripped */
