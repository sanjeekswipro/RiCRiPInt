/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:xpsfonts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * XPS font cache.
 */

#include "core.h"
#include "coreinit.h"
#include "lists.h"            /* dll_list_t */
#include "objnamer.h"         /* OBJECT_NAME_MEMBER */

#include "xml.h"

#include "mps.h"              /* mps_root_t */
#include "gcscan.h"           /* ps_scan_field */

#include "swerrors.h"         /* TYPECHECK */
#include "namedef_.h"
#include "objects.h"          /* setup_dictionary */
#include "fileioh.h"          /* FILELIST */
#include "stacks.h"           /* operandstack */
#include "dicthash.h"         /* insert_hash */
#include "fonth.h"            /* tt_definefont */
#include "psvm.h"             /* ps_string */
#include "uniqueid.h"
#include "fontcache.h"

#include "xpstypestream.h"
#include "xpspriv.h"
#include "xpsfonts.h"

#include "obfont.h"           /* obFontFilter */

/** \brief xps font cache entry. */
typedef struct XPS_FONT {
  dll_link_t    link;        /**< Font cache chain link. */
  xmlGIStr*     uri;         /**< Absolute font part URI. */
  int32         index;       /**< Font collection index number. */
  OBJECT        dict;        /**< PS dictionary of defined font. */
  int32         wmode;       /**< sideways flag. */

  OBJECT_NAME_MEMBER
} XPS_FONT;

/** \brief xps font cache structure name used to generate hash checksum. */
#define XPS_FONT_OBJECT_NAME "xps font"

/** \brief xps font cache size. */
#define XPS_FONT_CACHE_SIZE  (47u)

/**
 * \brief An xps font cache.
 *
 * A font cache caches fonts encountered when processing an xps. Each defined
 * font is uniquely identified by its absolute URI and font face index. The hash
 * table is indexed from the font URI and chain matches filtered on font face
 * index.
 */
typedef struct XPS_FONT_CACHE {
  dll_list_t  fonts[XPS_FONT_CACHE_SIZE];  /**< xps hash table keyed on font URI. Double linked lists are used for the chains. */
  OBJECT      setup_dict;   /**< PS dict in global memory used when defining fonts. */
#ifdef ASSERT_BUILD
  int32       f_init;       /**< Has the font cache been initialised. ASSERT_BUILD only. */
#endif /* ASSERT_BUILD */
} XPS_FONT_CACHE;

/** \brief The xps font cache.
 *
 * The font cache is held separate from the xps context since we will never be
 * interpreting more than one xps at a time, and it saves having to pass
 * through the xps context every time. This may change.
 */
static XPS_FONT_CACHE  font_cache;

/** Counter for generating uniqueIDs for XPS fonts. */
static int32 xps_UniqueID = 0x00FFFFFF | (UID_RANGE_XPS << 24);

static mps_root_t font_cache_root = NULL;

static
mps_res_t MPS_CALL font_cache_root_scan(
  mps_ss_t  ss,
  void*     p,
  size_t    s)
{
  int32       i;
  dll_list_t* p_chain;
  mps_res_t   res;
  XPS_FONT*  p_font;

  UNUSED_PARAM(void*, p);
  UNUSED_PARAM(size_t, s);

  /* Scan font setup dict */
  res = ps_scan_field(ss, &font_cache.setup_dict);
  if ( res != MPS_RES_OK ) {
    return(res);
  }

  /* Scan font dicts in cache */
  for ( i = 0; i < XPS_FONT_CACHE_SIZE; i++ ) {
    p_chain = &font_cache.fonts[i];
    p_font = DLL_GET_HEAD(p_chain, XPS_FONT, link);
    while ( p_font != NULL ) {
      res = ps_scan_field(ss, &p_font->dict);
      if ( res != MPS_RES_OK ) {
        return(res);
      }
      p_font = DLL_GET_NEXT(p_font, XPS_FONT, link);
    }
  }

  return(MPS_RES_OK);

} /* font_cache_root_scan */


/**
 * \brief
 * Calculate font cache hash table index.
 *
 * This is really horrid since it will break or have bad behaviour with 64 bit
 * pointers.
 *
 * \param[in] istr_uri
 * Name cache pointer for absolute font part URI.
 *
 * \return
 * Font cache hash table index.
 */
extern
uint32 xps_font_hash(
/*@in@*/ /*@null@*/
  xmlGIStr* istr_uri);
#define xps_font_hash(istr_uri) (((uintptr_t)(istr_uri)>>2)%XPS_FONT_CACHE_SIZE)

static
Bool init_setup_dict(
/*@out@*/ /*@notnull@*/
  OBJECT* setup_dict)
{
  OBJECT  nameobj = OBJECT_NOTVM_NOTHING;
  OBJECT  dictval = OBJECT_NOTVM_NOTHING;
  OBJECT  ros = OBJECT_NOTVM_NOTHING;

  HQASSERT((setup_dict != NULL),
           "init_setup_dict: NULL dict pointer");

  if ( !ps_dictionary(setup_dict, 4) ) {
    return(FALSE);
  }

  /* Add entries so all we have to do later is lookup and replace the values */
  object_store_name(&nameobj, NAME_CIDFont, LITERAL);
  if ( !insert_hash(setup_dict, &nameobj, &tnewobj) ) {
    return(FALSE);
  }
  object_store_name(&nameobj, NAME_SubFont, LITERAL);
  object_store_integer(&dictval, 0);
  if ( !insert_hash(setup_dict, &nameobj, &dictval) ) {
    return(FALSE);
  }
  object_store_name(&nameobj, NAME_XPSFont, LITERAL);
  if ( !insert_hash(setup_dict, &nameobj, &tnewobj) ) {
    return(FALSE);
  }

  /* CIDSystemInfo dictionary - Registry and Ordering must be strings, not names */
  if ( !ps_dictionary(&ros, 3) ) {
    return(FALSE);
  }
  object_store_name(&nameobj, NAME_Registry, LITERAL);
  if ( !ps_string(&dictval, NAME_AND_LENGTH("Adobe")) ) {
    return(FALSE);
  }
  if ( !insert_hash(&ros, &nameobj, &dictval) ) {
    return(FALSE);
  }
  object_store_name(&nameobj, NAME_Ordering, LITERAL);
  if ( !ps_string(&dictval, NAME_AND_LENGTH("Identity")) ) {
    return(FALSE);
  }
  if ( !insert_hash(&ros, &nameobj, &dictval) ) {
    return(FALSE);
  }
  object_store_name(&nameobj, NAME_Supplement, LITERAL);
  object_store_integer(&dictval, 0);
  if ( !insert_hash(&ros, &nameobj, &dictval) ) {
    return(FALSE);
  }
  object_store_name(&nameobj, NAME_CIDSystemInfo, LITERAL);
  if ( !insert_hash(setup_dict, &nameobj, &ros) ) {
    return(FALSE);
  }

  return(TRUE);

} /* init_setup_dict */


/* initialise font cache for use. */
Bool xps_font_cache_swstart(struct SWSTART *params)
{
  corecontext_t *context = get_core_context_interp();
  Bool gallocmode;
  Bool result;

  UNUSED_PARAM(struct SWSTART *, params) ;

  HQASSERT((!font_cache.f_init),
           "xps_font_cache_init: font cache already initialised");

  /* Set up configuration dictionary in global memory */
  gallocmode = setglallocmode(context, TRUE);
  result = init_setup_dict(&font_cache.setup_dict);
  setglallocmode(context, gallocmode);

  if (!result) {
    return(FALSE);
  }

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create(&font_cache_root, mm_arena, mps_rank_exact(), 0,
                       font_cache_root_scan, NULL, 0) != MPS_RES_OK) {
    return FAILURE(FALSE) ;
  }

#if ASSERT_BUILD
  font_cache.f_init = TRUE;
#endif /* ASSERT_BUILD */

  return(TRUE);
} /* xps_font_cache_swstart */


void xps_font_cache_finish(void)
{
  HQASSERT((font_cache.f_init),
           "xps_font_cache_finish: font cache not initialised");

  mps_root_destroy(font_cache_root);

#ifdef ASSERT_BUILD
  font_cache.f_init = FALSE;
#endif
}


/* xps_font_cache_purge() - purge cache of all fonts */
void xps_font_cache_purge(void)
{
  int32       i;
  dll_list_t* p_chain;
  XPS_FONT*  p_font;

  HQASSERT((font_cache.f_init),
           "xps_font_cache_purge: font cache not initialised");

  /* Free off all fonts in the cache */
  for ( i = 0; i < XPS_FONT_CACHE_SIZE; i++ ) {
    p_chain = &font_cache.fonts[i];
    while ( !DLL_LIST_IS_EMPTY(p_chain) ) {
      OBJECT *id;

      p_font = DLL_GET_HEAD(p_chain, XPS_FONT, link);
      DLL_REMOVE(p_font, link);
      id = fast_extract_hash_name(&p_font->dict, NAME_UniqueID);
      if ( id != NULL && (oInteger(*id) & 0xFF000000) == UID_RANGE_XPS << 24 )
        fontcache_make_useless(oInteger(*id), NULL);
      mm_free(mm_xml_pool, p_font, sizeof(XPS_FONT));
    }
  }
} /* xps_font_cache_purge */


/**
 * \brief Check leaf segment of obfuscated font partname is of the correct format.
 * \param[in] partname
 * Pointer to part name of font.
 * \return
 * \c TRUE if the partname is of the correct format, else \c FALSE.
 */
static
Bool xps_check_partname_leaf_format(
/*@in@*/ /*@notnull@*/
  xps_partname_t* partname)
{
  uint8 *s, *j, *e, i, c;
  uint32 shift = 0x88880;

  j = (uint8*)intern_value(partname->norm_name);
  e = j + intern_length(partname->norm_name);

  /* find final separator, s */
  for (s = j-1; j < e; j++)
    if (*j == '/')  s = j;

  /* check there's enough partname left */
  if (s + 36 > e)
    return FALSE;

  /* check format is XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX[.ext] */
  for ( i = 0; i<32; i++ ) {
    if ( !( ( (c = *++s)>='0' && c<='9') ||
            ( (c &= 0xDF)>='A' && c<='F') )) return FALSE;
    if ( (shift & 1) && *++s != '-' ) return FALSE;
    shift = shift >> 1;
  }

  /* only end of string or extension acceptable */
  return ( ++s == e || *s == '.' );
}


/**
 * \brief Define an XPS font using internal functions.
 *
 * \param[in] filter
 * Pointer to XML parse handle.
 * \param[in] font_partname
 * Pointer to part name of font.
 * \param[in] subfont_index
 * 0-based index of required font in truetype collection.
 * \param[in] wmode
 * Writing mode
 * \param[out] font_dict
 * Pointer to filled in PostScript font dictionary.
 *
 * \return
 * \c TRUE if the font was defined successfully, else \c FALSE.
 */
static
Bool xps_font_define_internal(
/*@in@*/ /*@notnull@*/
  xmlGFilter*  filter,
/*@in@*/ /*@notnull@*/
  xps_partname_t* font_partname,
/*@in@*/
  int32        subfont_index,
/*@in@*/
  int32        wmode,
/*@out@*/ /*@notnull@*/
  OBJECT*       font_dict)
{
  corecontext_t *context = get_core_context_interp();
  xmlGIStr* font_mimetype;
  OBJECT    font_part = OBJECT_NOTVM_NOTHING ;
  OBJECT    font_stream = OBJECT_NOTVM_NOTHING ;
  FILELIST* file_filter;
  xmlDocumentContext *xps_ctxt;
  Bool      gallocmode;
  Bool      definefont_ok = TRUE ;

  static XPS_CONTENT_TYPES font_content_types[] = {
    { XML_INTERN(mimetype_ms_opentype) },
    { XML_INTERN(mimetype_package_obfuscated_opentype) },
    XPS_CONTENT_TYPES_END
  } ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  /* We set global allocation mode to protect the font dictionaries and streams
   * from being wiped out in the restore which is performed at the end of each
   * page. */
  gallocmode = setglallocmode(context, TRUE);

  /* check the mimetype and open the file */
  if (! xps_open_file_from_partname(filter, font_partname, &font_part,
                                    XML_INTERN(rel_xps_2005_06_required_resource),
                                    font_content_types, &font_mimetype, TRUE)) {
    setglallocmode(context, gallocmode);
    return FALSE ;
  }

  /* stick an obFont filter on it if obfuscated */
  if ( font_mimetype == XML_INTERN(mimetype_package_obfuscated_opentype) ) {
    OBJECT partNameO = OBJECT_NOTVM_NOTHING ;

    if ( !xps_check_partname_leaf_format(font_partname) ) {
      setglallocmode(context, gallocmode);
      return(error_handler(INVALIDFONT));
    }
    if ( !ps_string(&partNameO,
                    (uint8*) intern_value(font_partname->norm_name),
                    intern_length(font_partname->norm_name)) ) {
      setglallocmode(context, gallocmode);
      return(FALSE);
    }
    file_filter = obFont_decode_filter();
    if ( !push(&font_part, &operandstack) ||
         !push(&partNameO, &operandstack) ||
         !filter_create_object(file_filter,
                               &font_part, NULL, &operandstack) ) {
      setglallocmode(context, gallocmode);
      return(error_handler(IOERROR));
    }
    font_mimetype = XML_INTERN(mimetype_ms_opentype);
  }

  /* now stick an RSD on it */
  file_filter = filter_standard_find(NAME_AND_LENGTH("ReusableStreamDecode"));
  HQASSERT((file_filter != NULL),
           "xmlXPS_Glyphs_Start: Can't find RSD filter");

  if (! push(&font_part, &operandstack) ||
      ! filter_create_object(file_filter, &font_stream, NULL, &operandstack) ) {
    definefont_ok = error_handler(IOERROR) ;
  }


  /* Get font dict for the font. */
  if (definefont_ok) {
    OBJECT *val;
    /* set setup_dict's SubFont key to be the uri fragment, or 0 if not specified */
    val = fast_extract_hash_name(&font_cache.setup_dict, NAME_SubFont);
    if ( val )
      oInteger(*val) = (subfont_index == -1) ? 0 : subfont_index;

    definefont_ok = tt_definefont(font_dict, &font_cache.setup_dict, &font_stream, NULL) ;

    if (! definefont_ok)
      definefont_ok = error_handler(INVALIDFONT) ;
  }

  /* Reset allocation mode */
  setglallocmode(context, gallocmode);

  if (! definefont_ok)
    return FALSE ;

  /* Insert wmode into the returned font dict. fast_insert_hash doesn't check
     dictionary permissions, so we don't need to reset them for this. */
  oInteger(inewobj) = wmode;
  if ( !fast_insert_hash_name(font_dict, NAME_WMode, &inewobj) )
    return FALSE ;

  /* Manufacture a UniqueID, so the glyphs are not regenerated every page. */
  xps_UniqueID--;
  if (xps_UniqueID < (UID_RANGE_XPS << 24))
    xps_UniqueID += 1<<24 ;  /* wrap round back into range */
  HQASSERT((xps_UniqueID & 0xFF000000) == UID_RANGE_XPS << 24,
           "Inconsistent XPS Unique ID");
  oInteger(inewobj) = xps_UniqueID;
  if ( !fast_insert_hash_name(font_dict, NAME_UniqueID, &inewobj) )
    return FALSE;

  return TRUE ;
} /* xps_font_define_internal */


/**
 * \brief Find font, by absolute URI, face index and wmode, in list.
 *
 * \param[in] p_chain
 * Pointer to list of fonts.
 * \param[in] istr_uri
 * Pointer to interned URI of font.
 * \param[in] subfont_index
 * 0-based index of required font in truetype collection.
 * \param[in] wmode
 * Non-zero if 'sideways'.
 *
 * \return
 * Pointer to xps font in the list, else \c NULL.
 */
static /*@null@*/ /*@null@*/
XPS_FONT* xps_font_find(
/*@in@*/ /*@notnull@*/
  dll_list_t*   p_chain,
/*@in@*/ /*@notnull@*/
  xmlGIStr*     istr_uri,
/*@in@*/
  int32         subfont_index,
/*@in@*/
  int32         wmode)
{
  corecontext_t *context = get_core_context_interp();
  XPS_FONT *p_font, *best_match = NULL;

  HQASSERT((p_chain != NULL),
           "xps_font_find: NULL list pointer");
  HQASSERT((istr_uri != NULL),
           "xps_font_find: NULL font URI pointer");

  p_font = DLL_GET_HEAD(p_chain, XPS_FONT, link);
  while ( p_font != NULL ) {
    if ( (p_font->uri == istr_uri) && (p_font->index == subfont_index) ) {
      if ( p_font->wmode == wmode ) {
        VERIFY_OBJECT(p_font, XPS_FONT_OBJECT_NAME);
        break;
      } else
        best_match = p_font;
    }
    p_font = DLL_GET_NEXT(p_font, XPS_FONT, link);
  }
  if (p_font == NULL && best_match) {
    ps_context_t *pscontext = context->pscontext ;
    Bool gallocmode;
    Bool ok ;

    /* We set global allocation mode to protect the font dictionaries and streams
     * from being wiped out in the restore which is performed at the end of each
     * page. */
    gallocmode = setglallocmode(context, TRUE);

    /* Found the font, but with the wrong wmode.
       Copy it and put in the right one */
    p_font = mm_alloc(mm_xml_pool, sizeof(XPS_FONT), MM_ALLOC_CLASS_XPS_FONT);
    if ( p_font == NULL ) {
      (void) error_handler(VMERROR);
      setglallocmode(context, gallocmode);
      return(NULL);
    }
    NAME_OBJECT(p_font, XPS_FONT_OBJECT_NAME);
    DLL_RESET_LINK(p_font, link);
    p_font->uri = best_match->uri;
    p_font->index = best_match->index;
    p_font->wmode = wmode;
    p_font->dict = onothing; /* Copy to set slot properties */

    /* Copy the font dict */
    if ( !push( &best_match->dict, &operandstack ) ||
         !dup_(pscontext) || !length_(pscontext) || !dict_(pscontext) || !copy_(pscontext) ) {
      setglallocmode(context, gallocmode);
      return NULL;
    }
    Copy(&p_font->dict, theITop(&operandstack) );
    pop(&operandstack);

    oName(nnewobj) = &system_names[NAME_WMode];
    oInteger(inewobj) = wmode;
    ok = insert_hash(&p_font->dict, &nnewobj, &inewobj);
    HQASSERT(ok, "Could not insert WMode into font dict") ;

    /* Add to head of chain. */
    DLL_ADD_HEAD(p_chain, p_font, link);

    /* Reset allocation mode */
    setglallocmode(context, gallocmode);

    /* and return this new font */
  }

  return(p_font);

} /* xps_font_find */


/**
 * \brief
 * Define font in the RIP and add to list of fonts.
 *
 * No check is done that the font does not already exist in the list - call
 * xps_font_find() first.
 *
 * \param[in] p_chain
 * Pointer to list of fonts.
 * \param[in] filter
 * Pointer to current XML parse handle.
 * \param[in] font_partname
 * Pointer to font part name.
 * \param[in] istr_font_uri
 * Pointer to interned version of font part name.
 * \param[in] subfont_index
 * 0-based index of required font in truetype collection.
 * \param[in] wmode
 * Non-zero if 'sideways'
 * \param[out] pp_font
 * Pointer to defined PS font.
 *
 * \return
 * \c TRUE if the font could be defined and was added to the list, else \c
 * FALSE.
 */
static
Bool xps_font_add(
/*@in@*/ /*@notnull@*/
  dll_list_t*   p_chain,
/*@in@*/ /*@notnull@*/
  xmlGFilter*   filter,
/*@in@*/ /*@notnull@*/
  xps_partname_t* font_partname,
/*@in@*/ /*@notnull@*/
  xmlGIStr*     istr_font_uri,
/*@in@*/
  int32         subfont_index,
/*@in@*/
  int32         wmode,
/*@in@*/ /*@notnull@*/
  XPS_FONT**    pp_font)
{
  OBJECT      font_dict = OBJECT_NOTVM_NOTHING;
  XPS_FONT*  p_font;

  HQASSERT((p_chain != NULL),
           "xps_font_add: NULL font list pointer.");
  HQASSERT((istr_font_uri != NULL),
           "xps_font_add: NULL font URI pointer");
  HQASSERT((font_cache.f_init),
           "xps_font_add: font cache not initialised");

  /* Load the font */
  if ( !xps_font_define_internal(filter, font_partname, subfont_index, wmode, &font_dict) ) {
    return(FALSE);
  }

  /* Allocate cache entry, fill in, and add to head of chain */
  p_font = mm_alloc(mm_xml_pool, sizeof(XPS_FONT), MM_ALLOC_CLASS_XPS_FONT);
  if ( p_font == NULL ) {
    return(error_handler(VMERROR));
  }
  NAME_OBJECT(p_font, XPS_FONT_OBJECT_NAME);

  /* Fill in font details */
  DLL_RESET_LINK(p_font, link);
  p_font->uri = istr_font_uri;
  p_font->index = subfont_index;
  p_font->dict = font_dict; /* Struct copy to set slot properties, font_dict is ISNOTVM */
  p_font->wmode = wmode;

  /* Add to head of chain. */
  DLL_ADD_HEAD(p_chain, p_font, link);

  /* Return new font */
  *pp_font = p_font;
  return(TRUE);

} /* xps_font_add */


/* xps_font_define() - look for font in font cache or add it */
Bool xps_font_define(
/*@in@*/ /*@notnull@*/
  xps_partname_t*  font_partname,
/*@in@*/
  int32         subfont_index,
/*@in@*/ /*@notnull@*/
  xmlGFilter*   filter,
/*@in@*/ /*@notnull@*/
  OBJECT**      p_font_dict,
  int32         wmode)
{
  XPS_FONT*  p_font;
  xmlGIStr*   istr_font_uri;
  dll_list_t* p_font_chain;
  uint8 *name;
  uint32 name_len;

  HQASSERT((font_partname != NULL),
           "xps_font_define: NULL font part name pointer");
  HQASSERT((p_font_dict != NULL),
           "xps_font_define: NULL font dict pointer");
  HQASSERT((font_cache.f_init),
           "xps_font_define: font cache not initialised");

  if (! hqn_uri_get_field(font_partname->uri,
                          &name,
                          &name_len,
                          HQN_URI_PATH))
    return error_handler(TYPECHECK);

  /* Convert font partname to interned string since that is what is used in the cache */
  if ( !intern_create(&istr_font_uri, name, name_len) ) {
    return(FALSE);
  }

  /* Look for font in cache, and if it is not there add it */
  p_font_chain = &font_cache.fonts[xps_font_hash(istr_font_uri)];
  p_font = xps_font_find(p_font_chain, istr_font_uri, subfont_index, wmode);
  if ( p_font == NULL ) {
    error_clear();
    if ( !xps_font_add(p_font_chain, filter, font_partname, istr_font_uri, subfont_index, wmode, &p_font) ) {
      return(FALSE);
    }

  } else {
    /* Keep font cache chains in MRU order */
    if ( !DLL_FIRST_IN_LIST(p_font, link) ) {
      DLL_REMOVE(p_font, link);
      DLL_ADD_HEAD(p_font_chain, p_font, link);
    }
  }

  *p_font_dict = &(p_font->dict);

  return(TRUE);

} /* xps_font_define */

void init_C_globals_xpsfonts(void)
{
  int32 i ;

  font_cache.setup_dict = onothing ; /* Struct copy to set slot properties */

  /* Reset hash table lists */
  for ( i = 0; i < XPS_FONT_CACHE_SIZE; i++ ) {
    DLL_RESET_LIST(&font_cache.fonts[i]);
  }

  font_cache_root = NULL ;
}

/* Log stripped */
