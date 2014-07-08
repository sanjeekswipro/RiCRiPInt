/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpspriv.h(EBDSDK_P.1) $
 * $Id: shared:xpspriv.h,v 1.73.10.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * xps context and utility functions. This header is private xps.
 *
 * This file is private to this compound and its sibling compounds.
 */

#ifndef __XPSPRIV_H__
#define __XPSPRIV_H__

#include "lists.h"         /* sll_link_t */
#include "group.h"
#include "hqunicode.h"
#include "matrix.h"
#include "objnamer.h"
#include "paths.h"

#include "hqnuri.h"        /* hqn_uri_t */

#include "xps.h"
#include "xpstypestream.h"
#include "xpspt.h"
#include "xpsparts.h"
#include "xpsscan.h"
#include "xpscommit.h"
#include "xpsrelsblock.h"
#include "discardStream.h"

extern Bool xps_error_occurred ;

typedef struct xpsCallbackState xpsCallbackState ;

typedef struct xpsCompatBlock xpsCompatBlock ;

typedef struct xpsSupportedUri xpsSupportedUri ;

typedef struct xpsProcessedPartName xpsProcessedPartName ;

/** \brief State subclass types. */
enum {
  XPS_STATE_FIXEDPAGE,
  XPS_STATE_CANVAS,
  XPS_STATE_GLYPHS,
  XPS_STATE_PATH,
  XPS_STATE_GEOMETRY,
  XPS_STATE_IMAGEBRUSH,
  XPS_STATE_VISUALBRUSH,
  XPS_STATE_GRADIENTBRUSH,
  XPS_STATE_PRINTTICKET
} ;

/** \brief This MUST be the first entry in xpsCallbackState subclasses. */
typedef struct {
  int32 type ;
  xpsCallbackState *next ;
} xpsCallbackStateBase ;

/** \todo I want this to be entirely private to xps */
struct xpsPartContext {
  struct xmlDocumentContext *xps_ctxt ;

  xps_partname_t *part_name ;

  /** Relationship types to load. */
  uint32 relationships_to_process ;
} ;

/* Most documents will be under 500 pages. 4K to hold page
   pointers is reasonable. */
#define XPS_PAGEBLOCK_SIZE 500

/* A block of pages. We use the array to number pages for out of order
   page processing which can be specified in an XPS print ticket. */
struct xpsPages {
  xps_partname_t* page[ XPS_PAGEBLOCK_SIZE ] ;
  struct xpsPages *next ;
} ;

/* An XPS fixed document. */
struct xpsFixedDocument {
  int32 number_of_pages ;
  int32 number_of_interpreted_pages ;
  int32 next_page ;
  Bool more_pages ;
  struct xpsPages pages ;
  struct xpsPages *active_pages ;
} ;

/** \brief XPS part context which holds global state information while
    processing a particular XPS part. */
struct xpsXmlPartContext {
  struct xpsPartContext base ;

  /** Are we defining resources. */
  Bool defining_resources ;

  /** Are we defining a drawing brush resource. */
  Bool defining_brush_resource ;

  /** Resource dictionary stack for resolving resource references. */
  sll_list_t resourceblock_stack ;

  /** The active resource we are caching. Only gets set when defining
      a resource. */
  xpsResource *active_resource ;

  /** Tracks executing resources. */
  xpsResource *executing_stack ;

  /** Keeps track of XML resource definition depth so we know when the
      current resource definition has completed. */
  int32 active_res_depth ;

  /** Keeps track of the element depth for the first use of the
      UserLabel attribute so we know when it becomes out of scope and
      should be turned off. Nested UserLabel's have no effect. A value
      of zero indicates that a user label has not been set at all. */
  uint32 outermost_userlabel_depth ;

  /** Holds commit arguments and function pointer to be able to call
      the commit function at the appropriate time. */
  xpsCommitBlock *commit ;

  /** Holds all the relationships for this part. This pointer is NULL
      when a part is being parsed, but when a parts relationships file
      is being parsed, it points to the relationships associated
      part. */
  xpsRelationshipsBlock *relationships ;

  /* Used when parsing core properties to determine whether we are
     within an element or not. */
  Bool within_element ;

  /* If the part context is created with an indirect relationships
     block, we must not de-allocate as someone else owns that
     block. */
  Bool indirect_relationships ;

  /* Have we seen a printticket relationship for this part already? */
  Bool printticket_relationship_seen ;

  /* If the part is open, this will point to the file. */
  FILELIST *flptr ;

  OBJECT_NAME_MEMBER
} ;

/** \brief Image state required to draw an image directly, rather than via a
    pattern. */
typedef struct {
  Bool allow ; /**< Are we allowed to draw an image directly? */
  Bool drawing ; /**< Drawing a direct image (state below has been captured)? */
  int32 colortype ;
  OMATRIX transform, matrix_adj ;
  USERVALUE opacity ;
  RECTANGLE viewbox ;
  int32 xref ;
  OBJECT image_filenameobj ;
  OBJECT image_profilenameobj ;
  xmlGIStr *image_mimetype ;
} xps_direct_image_t ;

/** \brief Global XPS document context which holds global state
    information while processing XPS. */
struct xmlDocumentContext {
  typestream_parser_t *typestream_parser ;

  /** Have we loaded a start part? */
  Bool startpart_loaded ;

  /** Have we seen a coreproperties part? */
  Bool coreproperties_relationship_seen ;

  /** Default part types. */
  XmlStrHashTable *ext_to_mimetype ;

  /** Override part types. */
  XmlStrHashTable *partname_to_mimetype;

  /** Partnames we have processed. */
  xpsProcessedPartName **parts_processed ;

  /** DiscardControl parser; will be null if no DiscardControl part is
  available. */
  DiscardParser* discard_parser ;

  /** XPS PrintTicket. */
  XPS_PT printticket ;

  /** Namespace URI's we fully support. */
  xpsSupportedUri **supported_uris ;

  /** Current fill rule (NZFILL_TYPE or EOFILL_TYPE). */
  int32 fill_rule ;

  /** Current colour type (GSC_UNDEFINED, GSC_STROKE or GSC_FILL). */
  int32 colortype ;

  /** Default rendering intent for the current page. */
  NAMECACHE *defaultRenderingIntentName ;

  /**< Stroke and fill opacity from a path brush.  They need to be kept
     separate from the gstate alphas to be able to handle the implicit group
     in dostrokefill according to XPS rules. */
  Bool capture_opacity;
  USERVALUE stroke_brush_opacity;
  USERVALUE fill_brush_opacity;

  Bool ignore_isstroked ; /**< IsStroked/IsClosed applies only within Path.Data contexts (not *.Clip). */
  Bool use_pathfill ; /**< TRUE indicates pathfill should be used for filling,
                           otherwise the fill and stroke are the same and path
                           can be used for stroking. */

  /**< The paths in the xps context should only be used during path construction; once the
       path is complete, the paths should be copied elsewhere (since these paths are used for
       filling, stroking and clipping). */
  PATHINFO path ; /**< For fill and stroke if they are the same, or just stroke. */
  PATHINFO pathfill ; /**< For filling only, if fill and stroke are different. */

  /** Stack of states for managing properties around callbacks. */
  xpsCallbackState *callback_state ;

  OMATRIX *transform ; /**< Where to put matrix captured by MatrixTransform. */

  Bool strict ; /**< Strict mode is the default, matching the spec. */

  /** Are we parsing a remote resource part? */
  Bool remote_resource ;
  /** Part to load the resource into. In the case of remote resources,
      this will be the Source part. */
  xpsXmlPartContext *remote_resource_source ;
  /** Source parts remote resource depth. */
  uint32 remote_resource_depth ;

  /** Resource dictionary uid. Set to zero for each new job. */
  uint32 resourceblock_uid ;

  /** The fixed document we are currently processing. If we need to
      track all fixed documents in a job, then this would become a
      list of fixed documents. */
  struct xpsFixedDocument *fixed_document ;

  /* The _rels/.rels part context. */
  struct xpsXmlPartContext *startpart_xmlpart_ctxt ;

  xps_partname_t *startpart_partname ;

  hqn_uri_t *package_uri ;

  /** If an image brush is untiled then it's more efficient to draw the image
      directly and use the path as a clip. This struct tracks the necessary
      image brush details to draw the image late in the path commit callback. */
  xps_direct_image_t direct_image ;
} ;

typedef struct xpsElementFuncts {
  xmlGIStr *localname ;
  xmlGStartElementCallback f_start;
  xmlGEndElementCallback f_end;
  xmlGCharactersCallback f_chars;
} xpsElementFuncts;

/* End element for functs */
#define XPS_ELEMENTFUNCTS_END { NULL, NULL, NULL, NULL }


/* ============================================================================
 * These structures are defined here so that we can set up SAC
 * allocations for them. All structures ought to be 8 byte aligned
 * although this is protected by aligning the structure size on
 * allocation and de-allocation.
 */

struct ComplexProperty {
  const xmlGIStr *localname ;
#ifdef DEBUG_BUILD /* So I can inspect this stack via the debugger. */
  const uint8* localname_str ;
#endif
  const xmlGIStr *uri ;
  Bool haveseen ;
  Bool optional ;
  xpsResourceReference *reference ;
  /* next for hash table */
  struct ComplexProperty *next ;
  /* prev for hash table */
  struct ComplexProperty *prev ;

  /* next for stack */
  struct ComplexProperty *stack_next ;
  /* prev for stack */
  struct ComplexProperty *stack_prev ;

  HqBool is_being_used ;
  void* pad1 ;
  void* pad2 ;
} ;

struct xpsCommitBlock {
  const xmlGIStr *localname ;
  const xmlGIStr *prefix ;
  const xmlGIStr *uri ;
  xmlGAttributes *attrs ;
  xmlGStartElementCallback f_commit ;

  /* Has this complex property been fired. */
  Bool fired ;

  /* Are we executing this commit block? Need this to avoid going
     recursive while also needing to leave the commit block at the top
     of the stack for validation. */
  Bool executing ;

  /* Hash table of complex properties. */
  uint32 num_properties ;
  struct ComplexProperty **table ;
  struct ComplexProperty *stack ;

  /* we need to insert complex properties in the order they are
     defined */
  struct ComplexProperty *stack_tail ;

  /* We use this to make sure commits are scoped to their parts. One
     filter chain per XPS part. */
  xmlGFilterChain *filter_chain ;

  /* List of resource references which are NOT complex properties. */
  xpsResourceReference *reference ;

  /* We have a single stack of commit blocks for all XPS parts. */
  xpsCommitBlock *next ;

  /* We need to pad if OBJECT_NAME_MEMBER is being used. */
  OBJECT_NAME_MEMBER
#ifdef ASSERT_BUILD
  void* pad ;
#endif
} ;

/* Smallish prime. Profiled 20th Nov 2006.
 * Do not expect many complex property child elements.
 */
#define COMPLEXPROPERTY_HASH_SIZE 31u

#define SAC_ALLOC_COMPLEX_PROPERTY_SIZE (DWORD_ALIGN_UP(size_t, sizeof(struct ComplexProperty)))

#define SAC_ALLOC_COMMIT_BLOCK_SIZE (DWORD_ALIGN_UP(size_t, sizeof(xpsCommitBlock) + \
                                                    (COMPLEXPROPERTY_HASH_SIZE * sizeof(struct ComplexProperty*)) + \
                                                    (COMPLEXPROPERTY_HASH_SIZE * sizeof(struct ComplexProperty))))

struct CompatUriEntry {
  const xmlGIStr *uri ;
  struct CompatUriEntry *next ;
} ;

struct CompatProcessContentEntry {
  const xmlGIStr *uri ;
  /* Can be * to represent all elements in the namespace. */
  const xmlGIStr *localname ;
  struct CompatProcessContentEntry *next ;
  void *pad ;
} ;

struct xpsCompatBlock {
  /* We use depth to track compatibility block scope. */
  uint32 depth ;

  /* 3 hash tables to hold compatibility informaion that might be
     present. */
  uint32 num_ignorable ;
  uint32 num_mustunderstand ;
  uint32 num_processcontent ;

  struct CompatUriEntry **ignorable ;
  struct CompatUriEntry **mustunderstand ;
  struct CompatProcessContentEntry **processcontent ;

  Bool ignore_this_element ;
  Bool ignore_block ;

  xpsCompatBlock *next ;
} ;

/* Smallish prime. Profiled 20th Nov 200.
 * Do not expect that many MustUnderstand and Ignorable namespaces to be
 * defined in a single element.
 */
#define COMPATBLOCK_HASH_SIZE 31u

#define SAC_ALLOC_COMPAT_URI_ENTRY_SIZE (DWORD_ALIGN_UP(size_t, sizeof(struct CompatUriEntry)))

#define SAC_ALLOC_COMPAT_PROCESS_CONTENT_ENTRY_SIZE (DWORD_ALIGN_UP(size_t, sizeof(struct CompatProcessContentEntry)))

#define SAC_ALLOC_COMPATBLOCK_SIZE (DWORD_ALIGN_UP(size_t, sizeof(struct xpsCompatBlock)))

/* ============================================================================
 * Function interfaces.
 */

extern
Bool xps_fixed_payload_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      XMLG_VALID_CHILDREN *valid_children,
      xmlDocumentContext *xps_ctxt) ;

extern
Bool xps_core_properties_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      XMLG_VALID_CHILDREN *valid_children,
      xmlDocumentContext *xps_ctxt) ;

/** \brief Register callback functions for child elements of the current
    element.
 */
extern
Bool xps_register_cb_array(
      xmlDocumentContext *xps_ctxt,
      xmlGFilter *filter,
      xmlGIStr *namespace,
      xpsElementFuncts *functArray);

/** \brief Register a resource reference
 * \param filter XML handler to register the reference against.
 * \param elementname The child element name under which the reference will
 *                    be expanded.
 * \param attrlocalname The attribute local name.
 * \param refname UTF-8 string containing the resource reference.
 * \param reflen Length of the resource reference.
 *
 * This function notes that a resource reference was used, looks up the
 * reference, and prepares it for expansion. Resource references are expanded
 * immediately after the start tag in which they are created is closed. The
 * element name under which they are expanded is set by the caller. The caller
 * should use the transformational rule, that:
 *
 * \code
 *   <MyElement MyProperty="{reference}">...</MyElement>
 * \endcode
 *
 * is equivalent to:
 *
 * \code
 *   <MyElement>
 *     <MyElement.MyProperty>
 *       ...
 *     </MyElement.MyProperty>
 *   </MyElement>
 * \endcode
 */
extern
Bool xps_resource_reference(xmlGFilter *filter,
                            xmlGIStr *elementname,
                            xmlGIStr *attrlocalname,
                            const uint8 *refname, uint32 reflen) ;

extern
Bool xps_add_processed_partname(
      xmlDocumentContext *xps_ctxt,
      const xmlGIStr *norm_name) ;

extern
Bool xps_is_processed_partname(
      xmlDocumentContext *xps_ctxt,
      const xmlGIStr *norm_name) ;

extern
Bool vxps_validity_error_cb(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      int32 error_type,
      char *format,
      va_list vlist) ;

extern
Bool xps_validity_error_cb(
      xmlGFilter *handler,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs,
      int32 error_type,
      char *format,
      ...) ;

extern
void xps_user_error_cb(
      xmlGFilter *handler,
      uint32 line,
      uint32 column) ;

extern
void xps_parse_error_cb(
      xmlGParser *xml_parser,
      hqn_uri_t *uri,
      uint32 line,
      uint32 column,
      uint8 *detail,
      int32 detail_len) ;

extern
Bool xps_is_supported_namespace(
      struct xpsSupportedUri **table,
      const xmlGIStr *uri) ;

extern
Bool lowmem_pre_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter) ;

extern
Bool lowmem_post_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter) ;

extern
void ggs_xps_setuserlabel(
    xmlGFilter *filter,
    Bool value) ;

/* ============================================================================
* Log stripped */
#endif
