/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:typestream.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * xps generic functions and callbacks.
 *
 * NOTE: extension and partnames have been normalised by the xps converters
 * before they are added to these tables.
 */

#include "core.h"
#include "coreinit.h"
#include "objects.h"
#include "namedef_.h"
#include "swctype.h"
#include "swerrors.h"
#include "swcopyf.h"
#include "gcscan.h"    /* ps_scan_field */
#include "mps.h"       /* mps_root_t */
#include "fileio.h"    /* FILELIST */
#include "hqmemcpy.h"

#include "xml.h"
#include "xps.h"
#include "xpspriv.h"
#include "xpsscan.h"
#include "xpstypestream.h"

static uint8* xps_content_typestream_partname = (uint8 *)"/[Content_Types].xml" ;
static uint32 xps_content_typestream_partname_len  = 20u ;

static mps_root_t typestream_root ;
static FILELIST *typestream_flptr ;

void init_C_globals_typestream(void)
{
  typestream_root = NULL ;
  typestream_flptr = NULL ;
}

static
mps_res_t MPS_CALL typestream_root_scan(mps_ss_t  ss, void *p, size_t s)
{
  UNUSED_PARAM(void*, p) ;
  UNUSED_PARAM(size_t, s) ;

  MPS_SCAN_BEGIN(ss) ;
  MPS_RETAIN(&typestream_flptr, TRUE);
  MPS_SCAN_END(ss) ;
  return MPS_RES_OK ;
}

Bool xps_typestream_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  /* Create root last so we force cleanup on success. */
  if (mps_root_create(&typestream_root, mm_arena, mps_rank_exact(), 0,
                      typestream_root_scan, NULL, 0) != MPS_RES_OK) {
    HQFAIL("Failed to register typestream root.") ;
    return FAILURE(FALSE) ;
  }
  return TRUE ;
}

void xps_typestream_finish(void)
{
  mps_root_destroy(typestream_root) ;
}

Bool xps_types_add_default(
      xmlGFilter *filter,
      xps_extension_t *extension,
      xmlGIStr *mimetype)
{
  xmlDocumentContext *xps_ctxt;
  void *old_mimetype = NULL;

  HQASSERT(filter != NULL, " filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  HQASSERT(extension != NULL, "extension is NULL") ;
  HQASSERT(extension->extension != NULL, "extension extension is NULL") ;

  /** \todo TODO: Re-write content type stream hash tables to use
     intern string hash. */
  if (! xmlstr_hash_add(xps_ctxt->ext_to_mimetype,
                        intern_value(extension->extension),
                        intern_length(extension->extension),
                        mimetype, (void **)&old_mimetype))
    return FALSE ;

  /* We are not allowed duplicates. */
  if (old_mimetype != NULL)
    return detailf_error_handler(SYNTAXERROR,
                                 "Duplicate <Default> for extension \"%.*s\".",
                                 intern_length(extension->extension),
                                 intern_value(extension->extension)) ;
  return TRUE;
}

Bool xps_types_add_override(
      xmlGFilter *filter,
      xps_partname_t *partname,
      xmlGIStr *mimetype)
{
  xmlDocumentContext *xps_ctxt;
  void *old_mimetype  = NULL;
  uint8 *name ;
  uint32 name_len ;

  HQASSERT(filter != NULL, " filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(partname->uri != NULL, "partname uri is NULL") ;

  if (! hqn_uri_get_field(partname->uri, &name, &name_len,
                          HQN_URI_PATH))
    return error_handler(TYPECHECK) ;

  /** \todo TODO: Re-write content type stream hash tables to use intern string
     hash. */
  if (! xmlstr_hash_add(xps_ctxt->partname_to_mimetype, name, name_len,
                        mimetype, (void **)&old_mimetype))
    return FALSE ;

  /* We are not allowed duplicates. */
  if (old_mimetype != NULL)
    return detailf_error_handler(SYNTAXERROR,
                                 "Duplicate <Override> for partname %.*s.",
                                 name_len, name) ;
  return TRUE;
}

/* =========================================================================
 * Open/Close and parse more of the type stream.
 * ========================================================================= */
struct typestream_parser_t {
  OBJECT ofile ;
  hqn_uri_t *base_uri ;
  xmlGFilterChain *filter_chain ;
  xml_chunk_parser_t *chunk_parser ;
  Bool more_data ;
} ;

/* This lazily looks up the content type of parts reading more of the
   content type stream only if an Override can not be found in the
   hash. If no Override has been found by the end of the stream, the
   code will fallback to a Default value. We need to do this as we do
   not know until the entire stream has been read whether an Override
   exists for the part before we can rely on a Default value. This is
   why the MS spec suggests that Overrides always be used in the
   streaming scenario.

   -------
   Lookup part name with an Override
   While not found and not end of content type stream
     Read "some more" of the content type stream and process XML
     Lookup part name with an Override

   If Override found
     Return mimetype
   Else
     Is there a Default mimetype for this extension?
       Return Default mimetype

   Return not found
   -------

   For a carefully written device, the "some more" would be just
   enough bytes for the next Override element which would hopefully be
   for the part name being queried.
 */
Bool xps_types_get_part_mimetype(
      xmlGFilter *filter,
      xps_partname_t *partname,
      xmlGIStr **mimetype)
{
  xmlDocumentContext *xps_ctxt ;
  uint8 *name ;
  uint32 name_len ;
  xml_chunk_parser_t *chunk_parser ;
  typestream_parser_t *typestream_parser ;

  HQASSERT(filter != NULL, " filter is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(partname->uri != NULL, "partname uri is NULL") ;
  HQASSERT(mimetype != NULL, "mimetype is NULL") ;

  typestream_parser = xps_ctxt->typestream_parser ;
  HQASSERT(typestream_parser != NULL, "typestream_parser is NULL") ;
  chunk_parser = typestream_parser->chunk_parser ;
  HQASSERT(chunk_parser != NULL, "chunk_parser is NULL") ;

  *mimetype = NULL ;

  if (! hqn_uri_get_field(partname->uri, &name, &name_len,
                          HQN_URI_PATH))
    return error_handler(TYPECHECK) ;

  /* ===================================================== */

  /* Search for the first matching Override until the end of the
     stream. */
  {
    Bool found ;
    found = xmlstr_hash_get(xps_ctxt->partname_to_mimetype,
                            name, name_len, (void **)mimetype) ;

    while (! found && typestream_parser->more_data) {
      if (! xml_parse_chunk(chunk_parser, &(typestream_parser->more_data)))
        return FALSE ;

      found = xmlstr_hash_get(xps_ctxt->partname_to_mimetype,
                              name, name_len, (void **)mimetype) ;
    }

    if (found)
      return TRUE ;
  }

  /* Find the extension. Search backwards for a '.'. */
  {
    uint32 extension_len = 0 ;
    const uint8 *end = name + name_len ;
    while (end != name) {
      if (*--end == '.') {
        if (xmlstr_hash_get(xps_ctxt->ext_to_mimetype, end + 1, extension_len,
                            (void **)mimetype)) {
          return TRUE ;
        }
        break ; /* We only search backwards for the first '.' */
      }
      ++extension_len ;
    }
  }
  /* ===================================================== */

  return detailf_error_handler(UNDEFINED, "Target part %.*s does not exist within package due to missing content type.",
                               name_len, name) ;
}

typestream_parser_t* xps_xml_open_typestream_parser(
      xmlDocumentContext *xps_ctxt,
      hqn_uri_t *package_uri)
{
  OBJECT typestream_ofile = OBJECT_NOTVM_NOTHING ;
  uint8 *authority ;
  uint32 authority_len, buf_len ;
  /* Allow for surrounding %'s and NUL termination (hence + 2 + 1). */
  uint8 buf[ LONGESTFILENAME + LONGESTDEVICENAME + 2 + 1 ] ;
  xmlGFilterChain *new_filter_chain ;
  xmlGFilter *new_filter ;
  xml_chunk_parser_t *new_chunk_parser ;
  hqn_uri_t *base_uri ;
  typestream_parser_t *new_typestream_parser ;

  /* This is used to test the content type stream document element. */
  static XMLG_VALID_CHILDREN typestream_doc_element[] = {
    { XML_INTERN(Types), XML_INTERN(ns_package_2006_content_types), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(package_uri != NULL, "package_uri is NULL") ;

  if ((new_typestream_parser = mm_alloc(mm_xml_pool, sizeof(typestream_parser_t),
                                        MM_ALLOC_CLASS_XPS_CONTENTTYPE)) == NULL) {
    (void)error_handler(VMERROR) ;
    return NULL ;
  }

  new_typestream_parser->more_data = TRUE ;

  if (! hqn_uri_get_field(package_uri,
                          &authority,
                          &authority_len,
                          HQN_URI_AUTHORITY)) {

    mm_free(mm_xml_pool, new_typestream_parser, sizeof(typestream_parser_t)) ;
    (void)error_handler(TYPECHECK) ;
    return NULL ;
  }

  /* Add %'s around the authority name - which is in fact the PS device name. */
  buf_len = authority_len + 2 + xps_content_typestream_partname_len ;

  swcopyf(buf, (uint8*)"%%%.*s%%%.*s", authority_len, authority,
          xps_content_typestream_partname_len,
          xps_content_typestream_partname) ;

  theLen(snewobj) = CAST_TO_UINT16(buf_len) ;
  oString(snewobj) = buf ;

  if (! file_open(&snewobj, SW_RDONLY, READ_FLAG, FALSE, 0, &typestream_ofile)) {
    mm_free(mm_xml_pool, new_typestream_parser, sizeof(typestream_parser_t)) ;
    return NULL ;
  }

  typestream_flptr = oFile(typestream_ofile) ;

  if (! psdev_base_uri_from_open_file(typestream_flptr, &base_uri)) {
    (void)file_close(&typestream_ofile);
    (void)error_handler(UNDEFINED) ;
    mm_free(mm_xml_pool, new_typestream_parser, sizeof(typestream_parser_t)) ;
    return NULL ;
  }

  if (! hqn_uri_set_field(base_uri, HQN_URI_PATH,
                          xps_content_typestream_partname,
                          xps_content_typestream_partname_len)) {
    hqn_uri_free(&base_uri) ;
    (void)file_close(&typestream_ofile) ;
    (void)error_handler(UNDEFINED) ;
    mm_free(mm_xml_pool, new_typestream_parser, sizeof(typestream_parser_t)) ;
    return NULL ;
  }

  if (! xmlg_fc_new(core_xml_subsystem,
                    &new_filter_chain,
                    &xmlexec_memory_handlers,
                    base_uri,    /* stream uri */
                    base_uri,    /* base uri */
                    xps_ctxt)) {

    hqn_uri_free(&base_uri) ;
    (void)file_close(&typestream_ofile) ;
    (void)error_handler(UNDEFINED) ;
    mm_free(mm_xml_pool, new_typestream_parser, sizeof(typestream_parser_t)) ;
    return NULL ;
  }

  /* Override the parse error callback to be XPS specific. */
  xmlg_fc_set_parse_error_cb(new_filter_chain, xps_parse_error_cb) ;

  if (! xps_fixed_payload_filter_init(new_filter_chain, 10, &new_filter,
                                      typestream_doc_element, xps_ctxt)) {
    xmlg_fc_destroy(&new_filter_chain) ;
    hqn_uri_free(&base_uri) ;
    (void)file_close(&typestream_ofile) ;
    mm_free(mm_xml_pool, new_typestream_parser, sizeof(typestream_parser_t)) ;
    return NULL ;
  }

  if (! xml_parse_chunk_init(typestream_flptr, new_filter_chain, &new_chunk_parser)) {
    xmlg_fc_destroy(&new_filter_chain) ;
    hqn_uri_free(&base_uri) ;
    (void)file_close(&typestream_ofile) ;
    mm_free(mm_xml_pool, new_typestream_parser, sizeof(typestream_parser_t)) ;
    return NULL ;
  }

  new_typestream_parser->ofile = typestream_ofile ; /* sets slot properties */
  new_typestream_parser->base_uri = base_uri ;
  new_typestream_parser->filter_chain = new_filter_chain ;
  new_typestream_parser->chunk_parser = new_chunk_parser ;

  return new_typestream_parser ;
}


void xps_xml_close_typestream_parser(
      typestream_parser_t** typestream_parser,
      Bool error_occurred)
{
  hqn_uri_t *base_uri ;
  OBJECT ofile = OBJECT_NOTVM_NOTHING ;
  xmlGFilterChain *filter_chain ;
  xml_chunk_parser_t *chunk_parser ;
  Bool more_data ;

  HQASSERT(typestream_parser != NULL, "typestream_parser is NULL") ;
  HQASSERT(*typestream_parser != NULL, "*typestream_parser is NULL") ;

  ofile = (*typestream_parser)->ofile ;
  base_uri = (*typestream_parser)->base_uri ;
  filter_chain = (*typestream_parser)->filter_chain ;
  chunk_parser = (*typestream_parser)->chunk_parser ;
  more_data = (*typestream_parser)->more_data ;

  /* Its possible that we have not finished parsing the content type
     stream. In that case, continue reading more data and parsing but
     only of an error condition has not happened. */
  if (more_data && ! error_occurred) {
    while (more_data) {
      if (! xml_parse_chunk(chunk_parser, &more_data))
        break ;
    }
  }

  xml_parse_chunk_finish(&chunk_parser, error_occurred) ;

  xmlg_fc_destroy(&filter_chain) ;
  hqn_uri_free(&base_uri) ;
  (void)file_close(&ofile);
  mm_free(mm_xml_pool, *typestream_parser, sizeof(typestream_parser_t)) ;
  *typestream_parser = NULL ;
  typestream_flptr = NULL;
}

/* ============================================================================
* Log stripped */
