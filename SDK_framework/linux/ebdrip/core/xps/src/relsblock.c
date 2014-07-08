/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:relsblock.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of xps relationships.
 */

#include "core.h"
#include "mmcompat.h"          /* mm_alloc_with_header etc.. */
#include "hqmemcpy.h"          /* HqMemCpy */
#include "hqmemset.h"
#include "swerrors.h"
#include "xpspriv.h"
#include "xpsparts.h"
#include "xml.h"
#include "xpsrelsblock.h"
#include "xpspartspriv.h"

#include "namedef_.h"

/* Smallish prime.
 * Do not expect that many relationships in a single relationships file.
 */
#define RELATIONSHIPS_BLOCK_HASH_SIZE 109

struct xpsRelationship {
  xmlGIStr *id ;
  xmlGIStr *type ;
  xps_partname_t *target ;
  xmlGIStr *targetmode ;

  struct xpsRelationship *next_via_id ;
  struct xpsRelationship *next_via_type ;
  struct xpsRelationship *next_via_partname_and_type ;
} ;

struct relationships_parser_t {
  OBJECT ofile ;
  xps_partname_t *partname ;
  xmlGFilterChain *filter_chain ;
  xml_chunk_parser_t *chunk_parser ;
  Bool more_data ;
  struct xpsXmlPartContext *relationship_part_ctxt ;
} ;

typedef struct relationships_parser_t relationships_parser_t ;

struct xpsRelationshipsBlock {
  /* Three hash tables. One for looking up ID to check that it is
     unique. Another for looking up targets to check that their type
     is correct and another to look up just the type (used for
     checking the S0 start relationship). */
  uint32 num_entries ;
  struct xpsRelationship **id_table ;

  /* The partname_table uses a pair of keys. The partname and the
     relationship type. */
  struct xpsRelationship **partname_table ;

  /* The type_table uses the type as a key. */
  struct xpsRelationship **type_table ;

  /* The parse instance reading this relationships stream. */
  relationships_parser_t *relationships_parser ;
} ;

/** \todo This is a hack and in the wrong place. */
extern Bool xps_versioning_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      struct xpsSupportedUri **table) ;

/**
 * \brief Translate the absolute part name to its absolute relationship part
 * name.
 */
static
Bool xps_partname_to_relationship_partname(
      xps_partname_t *partname,
      hqn_uri_t *base_uri,
      xps_partname_t **part_relationship_name)
{
  uint8 buf[ LONGESTFILENAME ] ;
  uint8 *insert_point = buf, *path, *end ;
  uint32 path_len, buf_len, prefix_len ;

  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(partname->uri != NULL, "partname uri is NULL") ;
  HQASSERT(base_uri != NULL, "base_uri is NULL") ;
  HQASSERT(part_relationship_name != NULL, "part_relationship_name is NULL") ;

  *part_relationship_name = NULL ;

  if (! hqn_uri_get_field(partname->uri, &path, &path_len,
                          HQN_URI_PATH))
    return error_handler(TYPECHECK) ;

  if (path_len + 11 > LONGESTFILENAME)
    return error_handler(LIMITCHECK) ;

  HQASSERT(path[0] == '/', "path does not begin with a /") ;

  /* find last / */
  end = path + path_len - 1 ;
  while (end != path && *end != '/') {
    end-- ;
  }

  prefix_len = CAST_PTRDIFFT_TO_UINT32(end - path) ;

  HqMemCpy(insert_point, path, prefix_len ) ;
  insert_point += prefix_len ;

  /* basically need to add "/_rels" prefix and a ".rels" suffix */
  HqMemCpy(insert_point, "/_rels", 6) ;
  insert_point += 6 ;

  HqMemCpy(insert_point, end, path_len - prefix_len) ;
  insert_point += (path_len - prefix_len) ;
  HqMemCpy(insert_point, ".rels", 5) ;
  insert_point += 5 ;
  buf_len = 11 + path_len ;

  return xps_partname_new(part_relationship_name,
                          base_uri, buf, buf_len,
                          XPS_NORMALISE_PARTNAME) ;
}

/* =========================================================================
 * Open/Close and parse more of a relationships stream.
 * ========================================================================= */
static
Bool xps_xml_open_relationships_parser(
      xmlDocumentContext *xps_ctxt,
      int32 relationships_to_process,
      struct xpsRelationshipsBlock *rels_block,
      xps_partname_t *partname,
      hqn_uri_t *base_uri,
      relationships_parser_t** relationships_parser)
{
  xps_partname_t *relationship_partname = NULL ;
  OBJECT ofile = OBJECT_NOTVM_NOTHING ;
  Bool stream_opened_ok ;

  static XMLG_VALID_CHILDREN relationships_doc_element[] = {
    { XML_INTERN(Relationships), XML_INTERN(ns_package_2006_relationships), XMLG_ONE, XMLG_NO_GROUP },
    XMLG_VALID_CHILDREN_END
  } ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(base_uri != NULL, "base_uri is NULL") ;
  HQASSERT(relationships_parser != NULL, "relationships_parser is NULL") ;

  *relationships_parser = NULL ;

  { /* Check to see if the part exists. If so open it. */
    Bool exists;
    STAT stat;

    if (! xps_partname_to_relationship_partname(partname, base_uri,
                                                &relationship_partname)) {
      return FALSE ;
    }

    if (! stat_from_psdev_uri(relationship_partname->uri, &exists, &stat)) {
      xps_partname_free(&relationship_partname) ;
      return FALSE ;
    }

    if (exists) {
      stream_opened_ok = open_file_from_psdev_uri(relationship_partname->uri, &ofile, FALSE) ;
    } else {
      stream_opened_ok = FALSE ;
    }
  }

  /* If we are unable to open the relationships stream, its not an
     error. We assume that there is no relationships part to be
     had. */
  if (stream_opened_ok) {
    struct xpsXmlPartContext *relationship_part_ctxt ;
    relationships_parser_t *new_relationships_parser ;
    xmlGFilterChain *new_filter_chain ;
    xmlGFilter *new_filter ;
    xml_chunk_parser_t *new_chunk_parser ;
    FILELIST *flptr ;

    if (! xps_xml_part_context_new(xps_ctxt, relationship_partname,
                                   rels_block, relationships_to_process,
                                   FALSE, &relationship_part_ctxt)) {
      xps_partname_free(&relationship_partname) ;
      (void)xml_file_close(&ofile) ;
      return FALSE ;
    }

    if ((new_relationships_parser =  mm_alloc(mm_xml_pool,
                                              sizeof(relationships_parser_t),
                                              MM_ALLOC_CLASS_XPS_RELS)) == NULL) {

      (void)xps_xml_part_context_free(&relationship_part_ctxt) ;
      xps_partname_free(&relationship_partname) ;
      (void)xml_file_close(&ofile) ;
      return FALSE ;
    }

    new_relationships_parser->more_data = TRUE ;
    new_relationships_parser->ofile = ofile ; /* set slot properties */
    new_relationships_parser->relationship_part_ctxt = relationship_part_ctxt ;

    flptr = oFile(new_relationships_parser->ofile) ;

    if (! xmlg_fc_new(core_xml_subsystem,
                      &new_filter_chain,
                      &xmlexec_memory_handlers,
                      relationship_partname->uri,
                      partname->uri,  /* the part is the default base */
                      relationship_part_ctxt)) {

      (void)xps_xml_part_context_free(&relationship_part_ctxt) ;
      xps_partname_free(&relationship_partname) ;
      (void)xml_file_close(&ofile);
      mm_free(mm_xml_pool, new_relationships_parser, sizeof(relationships_parser_t)) ;
      return FALSE ;
    }

    if (! xps_versioning_filter_init(new_filter_chain, 3, &new_filter, xps_ctxt->supported_uris) ||
        ! xps_commit_filter_init(new_filter_chain, 5, &new_filter, xps_ctxt) ||
        ! xps_resource_filter_init(new_filter_chain, 7, &new_filter, xps_ctxt) ||
        ! xps_fixed_payload_filter_init(new_filter_chain, 10, &new_filter, relationships_doc_element, xps_ctxt)) {

      (void)xps_xml_part_context_free(&relationship_part_ctxt) ;
      xps_partname_free(&relationship_partname) ;
      xmlg_fc_destroy(&new_filter_chain) ;
      (void)xml_file_close(&ofile);
      mm_free(mm_xml_pool, new_relationships_parser, sizeof(relationships_parser_t)) ;
      return FALSE ;
    }

    /* Override the parse error callback to be XPS specific. */
    xmlg_fc_set_parse_error_cb(new_filter_chain, xps_parse_error_cb) ;

    if (! xml_parse_chunk_init(flptr, new_filter_chain, &new_chunk_parser)) {
      (void)xps_xml_part_context_free(&relationship_part_ctxt) ;
      xps_partname_free(&relationship_partname) ;
      xmlg_fc_destroy(&new_filter_chain) ;
      (void)xml_file_close(&ofile);
      mm_free(mm_xml_pool, new_relationships_parser, sizeof(relationships_parser_t)) ;
      return FALSE ;
    }

    new_relationships_parser->filter_chain = new_filter_chain ;
    new_relationships_parser->chunk_parser = new_chunk_parser ;
    new_relationships_parser->partname = relationship_partname ;

    *relationships_parser = new_relationships_parser ;
  } else {
    /* Remember that the relationships file is optional but we do need
       to free the partname because we have no parser. */
    xps_partname_free(&relationship_partname) ;
  }

  return TRUE ;
}

static
Bool xps_xml_close_relationships_parser(
      relationships_parser_t** relationships_parser,
      Bool error_occurred)
{
  OBJECT ofile = OBJECT_NOTVM_NOTHING ;
  xps_partname_t *partname ;
  xmlGFilterChain *filter_chain ;
  xml_chunk_parser_t *chunk_parser ;
  struct xpsXmlPartContext *relationship_part_ctxt ;
  Bool more_data ;
  relationships_parser_t* old_relationships_parser ;
  Bool status = TRUE ;

  HQASSERT(relationships_parser != NULL, "relationships_parser is NULL") ;
  HQASSERT(*relationships_parser != NULL, "*relationships_parser is NULL") ;

  old_relationships_parser = *relationships_parser ;

  ofile = old_relationships_parser->ofile ;
  filter_chain = old_relationships_parser->filter_chain ;
  chunk_parser = old_relationships_parser->chunk_parser ;
  more_data = old_relationships_parser->more_data ;
  partname = old_relationships_parser->partname ;
  relationship_part_ctxt = old_relationships_parser->relationship_part_ctxt ;

  /* Its possible that we have not finished parsing the relationships
     stream. In that case, continue reading more data and parsing but
     only if an error condition has not happened. */
  if (more_data && ! error_occurred) {
    while (more_data) {
      status = xml_parse_chunk(chunk_parser, &more_data) ;
      if (! status)
        break ;
    }
  }

  xml_parse_chunk_finish(&chunk_parser, error_occurred) ;
  xmlg_fc_destroy(&filter_chain) ;
  (void)xml_file_close(&ofile);
  (void)xps_xml_part_context_free(&relationship_part_ctxt) ;
  xps_partname_free(&partname) ;
  mm_free(mm_xml_pool, old_relationships_parser, sizeof(relationships_parser_t)) ;

  *relationships_parser = NULL ;
  return status ;
}

static
/*@null@*/
struct xpsRelationship* find_via_partname_and_type(
      struct xpsRelationshipsBlock *rels_block,
      const xmlGIStr *partname,
      const xmlGIStr *type,
      uint32 *hval)
{
  uintptr_t hash ;

  struct xpsRelationship *curr;
  HQASSERT(rels_block != NULL, "rels_block is NULL") ;
  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(type != NULL, "type is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  hash = intern_hash(partname) % RELATIONSHIPS_BLOCK_HASH_SIZE ;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr=rels_block->partname_table[*hval]; curr!=NULL; curr=curr->next_via_partname_and_type) {
    if ( partname == curr->target->norm_name && type == curr->type )
      return curr;
  }
  return NULL ;
}

static
/*@null@*/
struct xpsRelationship* find_via_type(
      struct xpsRelationshipsBlock *rels_block,
      const xmlGIStr *type,
      uint32 *hval)
{
  uintptr_t hash ;

  struct xpsRelationship *curr;
  HQASSERT(rels_block != NULL, "rels_block is NULL") ;
  HQASSERT(type != NULL, "type is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  hash = intern_hash(type) % RELATIONSHIPS_BLOCK_HASH_SIZE ;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr=rels_block->type_table[*hval]; curr!=NULL; curr=curr->next_via_type) {
    if ( type == curr->type )
      return curr ;
  }
  return NULL ;
}

static
/*@null@*/
struct xpsRelationship* find_via_id(
      struct xpsRelationshipsBlock *rels_block,
      const xmlGIStr *id,
      uint32 *hval)
{
  uintptr_t hash ;

  struct xpsRelationship *curr;
  HQASSERT(rels_block != NULL, "rels_block is NULL") ;
  HQASSERT(id != NULL, "id is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  hash = intern_hash(id) % RELATIONSHIPS_BLOCK_HASH_SIZE ;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr=rels_block->id_table[*hval]; curr!=NULL; curr=curr->next_via_id) {
    if ( id == curr->id )
      return curr ;
  }
  return NULL ;
}

Bool xps_create_relationship_block(
      struct xpsRelationshipsBlock **rels_block,
      xmlDocumentContext *xps_ctxt,
      int32 relationships_to_process,
      xps_partname_t *partname,
      Bool new_parse_instance)
{
  struct xpsRelationshipsBlock *new_rels_block ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(rels_block != NULL, "rels_block is NULL") ;
  HQASSERT(partname != NULL, "partname is NULL") ;

  *rels_block = NULL ;

  if ((new_rels_block = mm_alloc(mm_xml_pool,
                                 sizeof(struct xpsRelationshipsBlock),
                                 MM_ALLOC_CLASS_XPS_RELS)) == NULL)
    return error_handler(VMERROR) ;

  /* Allocate all 3 tables at once. */
  new_rels_block->id_table = mm_alloc(mm_xml_pool,
                                      sizeof(struct xpsRelationship*) *
                                      RELATIONSHIPS_BLOCK_HASH_SIZE * 3,
                                      MM_ALLOC_CLASS_XPS_RELS) ;
  if (new_rels_block->id_table == NULL) {
    mm_free(mm_xml_pool, new_rels_block,
            sizeof(struct xpsRelationshipsBlock)) ;
    return error_handler(VMERROR) ;
  }

  HqMemZero((uint8 *)new_rels_block->id_table,
        sizeof(struct xpsRelationship*) *
        RELATIONSHIPS_BLOCK_HASH_SIZE * 3) ;

  new_rels_block->partname_table = new_rels_block->id_table +
    RELATIONSHIPS_BLOCK_HASH_SIZE ;

  new_rels_block->type_table = new_rels_block->id_table +
    (RELATIONSHIPS_BLOCK_HASH_SIZE * 2) ;

  /* Initialize the table structure. */
  new_rels_block->num_entries = 0 ;

  if (new_parse_instance) {
    /* When creating the relationships parser, we make sure its
       relationships block is pointing at the existing parts hash. */
    if (! xps_xml_open_relationships_parser(xps_ctxt, relationships_to_process,
                                            new_rels_block, partname,
                                            partname->uri, &(new_rels_block->relationships_parser))) {
      xps_destroy_relationship_block(&new_rels_block) ;
      return FALSE ;
    }
  } else {
    new_rels_block->relationships_parser = NULL ;
  }

  *rels_block = new_rels_block ;
  return TRUE ;
}

Bool xps_destroy_relationship_block(
      struct xpsRelationshipsBlock **rels_block)
{
  Bool status = TRUE ;
  uint32 i ;
  struct xpsRelationship *curr, *next ;
  struct xpsRelationshipsBlock *old_rels_block ;

  HQASSERT(rels_block != NULL, "rels_block is NULL") ;
  old_rels_block = *rels_block ;
  HQASSERT(old_rels_block != NULL, "old_rels_block is NULL") ;

  if (old_rels_block->relationships_parser != NULL)
    status = xps_xml_close_relationships_parser(&(old_rels_block->relationships_parser),
                                                xps_error_occurred) ;
  HQASSERT(old_rels_block->relationships_parser == NULL, "relationships_parser is no NULL") ;

  /* The Id table tracks all relationships. */
  for (i=0; i<RELATIONSHIPS_BLOCK_HASH_SIZE; i++) {
    for (curr=old_rels_block->id_table[i]; curr!=NULL; curr=next) {
      next = curr->next_via_id ;
      HQASSERT(curr->target != NULL, "target is NULL") ;
      xps_partname_free(&curr->target) ;

      mm_free(mm_xml_pool, curr, sizeof(struct xpsRelationship)) ;
      old_rels_block->num_entries-- ;
    }
  }
  HQASSERT(old_rels_block->num_entries == 0, "num_entries is not zero.");
  mm_free(mm_xml_pool,
          old_rels_block->id_table,
          sizeof(struct xpsRelationship*) *
          RELATIONSHIPS_BLOCK_HASH_SIZE * 3) ;

  mm_free(mm_xml_pool,
          old_rels_block, sizeof(struct xpsRelationshipsBlock)) ;

  *rels_block = NULL ;
  return status ;
}

Bool xps_add_relationship(
      struct xpsRelationshipsBlock *rels_block,
      xmlGIStr *id,
      xmlGIStr *type,
      xps_partname_t *target,
      xmlGIStr *targetmode)
{
  struct xpsRelationship *curr, *added ;
  uint32 hval ;

  HQASSERT(rels_block != NULL, "rels_block is NULL") ;
  HQASSERT(id != NULL, "id is NULL") ;
  HQASSERT(target != NULL, "target is NULL") ;

  curr = find_via_id(rels_block, id, &hval) ;

  if (curr == NULL) {
    curr = mm_alloc(mm_xml_pool, sizeof(struct xpsRelationship),
                    MM_ALLOC_CLASS_XML_NAMESPACE) ;

    if (curr == NULL)
      return error_handler(VMERROR) ;

    curr->id = id ;
    curr->targetmode = targetmode ;
    curr->type = type ;
    curr->target = target ;
    curr->next_via_id = rels_block->id_table[hval] ;
    rels_block->id_table[hval] = curr ;
    rels_block->num_entries++ ;
  } else {
    return detailf_error_handler(UNDEFINED,
        "Attribute Id value \"%.*s\" is a duplicate in this relationships part.",
        intern_length(id), intern_value(id)) ;
  }

  /* Save the newly created entry. */
  added = curr ;

  /* We have added the relationship under its own Id, now set up the
     other hash tables for fast lookup. */

  /* We allow duplicates of everything, we don't care. */

  /* Add to beginning of linked list. */
  curr = find_via_type(rels_block, added->type, &hval) ;
  added->next_via_type = rels_block->type_table[hval] ;
  rels_block->type_table[hval] = added ;

  /* Add to beginning of linked list. */
  curr = find_via_partname_and_type(rels_block, added->target->norm_name,
                                    added->type, &hval) ;
  added->next_via_partname_and_type = rels_block->partname_table[hval] ;
  rels_block->partname_table[hval] = added ;

  return TRUE ;
}

Bool xps_lookup_relationship_type(
      xpsRelationshipsBlock *rels_block,
      xmlGIStr *type,
      xpsRelationship **relationship,
      Bool parse_more)
{
  uint32 hval ;
  relationships_parser_t *relationships_parser ;

  HQASSERT(rels_block != NULL, "rels_block is NULL") ;
  HQASSERT(type != NULL, "type is NULL") ;
  HQASSERT(relationship != NULL, "relationship is NULL") ;

  relationships_parser = rels_block->relationships_parser ;

  *relationship = find_via_type(rels_block, type, &hval) ;

  if (parse_more &&
      *relationship == NULL &&
      relationships_parser != NULL) {
    xml_chunk_parser_t *chunk_parser ;
    chunk_parser = relationships_parser->chunk_parser ;
    HQASSERT(chunk_parser != NULL, "chunk_parser is NULL") ;

    while (*relationship == NULL && relationships_parser->more_data) {
      if (! xml_parse_chunk(chunk_parser, &(relationships_parser->more_data) ))
        return FALSE ; /* An error. */

      *relationship = find_via_type(rels_block, type, &hval) ;
    }
  }

  return TRUE ;
}

Bool xps_lookup_relationship_target_type(
      xpsRelationshipsBlock *rels_block,
      xps_partname_t *target,
      xmlGIStr *type,
      xpsRelationship **relationship,
      Bool parse_more)
{
  uint32 hval ;
  relationships_parser_t *relationships_parser ;

  HQASSERT(rels_block != NULL, "rels_block is NULL") ;
  HQASSERT(target != NULL, "target is NULL") ;
  HQASSERT(type != NULL, "type is NULL") ;
  HQASSERT(relationship != NULL, "relationship is NULL") ;

  relationships_parser = rels_block->relationships_parser ;

  *relationship = find_via_partname_and_type(rels_block, target->norm_name,
                                             type, &hval) ;

  if (parse_more &&
      *relationship == NULL &&
      relationships_parser != NULL) {
    xml_chunk_parser_t *chunk_parser ;
    chunk_parser = relationships_parser->chunk_parser ;
    HQASSERT(chunk_parser != NULL, "chunk_parser is NULL") ;

    while (*relationship == NULL && relationships_parser->more_data) {
      if (! xml_parse_chunk(chunk_parser, &(relationships_parser->more_data) ))
        return FALSE ; /* An error. */

      *relationship = find_via_partname_and_type(rels_block, target->norm_name,
                                                 type, &hval) ;
    }
  }

  return TRUE ;
}

xps_partname_t *xps_rels_get_target(
      xpsRelationship *relationship)
{
  HQASSERT(relationship != NULL, "relationship is NULL") ;
  HQASSERT(relationship->target != NULL, "relationship target is NULL") ;
  return relationship->target ;
}

/* ============================================================================
* Log stripped */
