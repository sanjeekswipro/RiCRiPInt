/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpsparts.h(EBDSDK_P.1) $
 * $Id: shared:xpsparts.h,v 1.24.1.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2007-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to Metro parts and extensions.
 */

#ifndef __XPSPARTS_H__
#define __XPSPARTS_H__

#include "hqnuri.h"    /* hqn_uri_t */
#include "xml.h"       /* core XML interface */

typedef struct xpsXmlPartContext xpsXmlPartContext ;

typedef struct xps_partname_t {
  xmlGIStr *norm_name ;
  xmlGIStr *mimetype ;
  hqn_uri_t *uri ;
  int32 uid ;
} xps_partname_t ;

typedef struct xps_extension_t {
  xmlGIStr *extension ;
} xps_extension_t ;

#define XPS_CONTENT_TYPES_END { NULL }

/**
 * Convenience structure to define valid children.
 */
typedef struct XPS_CONTENT_TYPES {
  xmlGIStr *content_type ;
} XPS_CONTENT_TYPES ;

/* Use these to signal which relationships should be processed. */
#define XPS_PROCESS_PRINTTICKET_REL     0x1
#define XPS_PROCESS_COREPROPERTIES_REL  0x2

/* Use these to signal what additional XML processing filters should
   be installed. */
#define XPS_PART_VERSIONED     0x1
#define XPS_PART_SIGNED        0x2
#define XPS_CORE_PROPERTIES    0x4
#define XPS_LOWMEMORY          0x8 /* Mandatory: low-mem handling and user filters */

extern
void xps_partname_context_init(void) ;

extern
void xps_partname_context_finish(void) ;

extern
Bool xps_partname_new(
      /*@in@*/ /*@notnull@*/
      xps_partname_t **partname,
      /*@in@*/ /*@null@*/
      hqn_uri_t *base_uri,
      uint8 *name,
      uint32 name_len,
      uint32 type) ;

extern
Bool xps_partname_copy(
      /*@in@*/ /*@notnull@*/
      xps_partname_t **to_partname,
      /*@in@*/ /*@notnull@*/
      xps_partname_t *from_partname ) ;

extern
Bool xps_parse_xml_from_partname(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xps_partname_t *partname,
      uint32 relationships_to_process,
      uint32 additional_filters,
      /*@in@*/ /*@notnull@*/
      XMLG_VALID_CHILDREN *valid_children,
      /*@in@*/ /*@null@*/
      xmlGIStr *required_relationship,
      /*@in@*/ /*@null@*/
      XPS_CONTENT_TYPES *allowable_content_types,
      /*@out@*/ /*@notnull@*/
      xmlGIStr **content_type) ;

/* If implicit_close_file is TRUE, then the file will not be installed
   in a scan list and is therefore open for garbage collection during
   a low memory handling call or via a restore. */
extern
Bool xps_open_file_from_partname(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xps_partname_t *partname,
      /*@in@*/ /*@notnull@*/
      OBJECT *ofile,
      /*@in@*/ /*@null@*/
      xmlGIStr *required_relationship,
      /*@in@*/ /*@null@*/
      XPS_CONTENT_TYPES *allowable_content_types,
      /*@out@*/ /*@notnull@*/
      xmlGIStr **content_type,
      Bool implicit_close_file) ;

Bool xps_ps_filename_from_partname(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xps_partname_t *partname,
      /*@in@*/ /*@notnull@*/
      uint8 **ps_filename,
      /*@in@*/ /*@notnull@*/
      uint32 *ps_filename_len,
      /*@in@*/ /*@null@*/
      xmlGIStr *required_relationship,
      /*@in@*/ /*@null@*/
      XPS_CONTENT_TYPES *allowable_content_types,
      /*@out@*/ /*@notnull@*/
      xmlGIStr **content_type) ;

extern
void xps_partname_free(
      /*@in@*/ /*@notnull@*/
      xps_partname_t **partname) ;

extern
Bool xps_extension_new(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xps_extension_t **extension,
      /*@in@*/ /*@notnull@*/
      uint8 *name,
      uint32 name_len) ;

extern
void xps_extension_free(
      /*@in@*/ /*@notnull@*/
      xps_extension_t **extension ) ;

extern
Bool  xps_have_processed_part(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xps_partname_t *partname) ;

extern
Bool xps_mark_part_as_processed(
      /*@in@*/ /*@notnull@*/
      xmlGFilter *filter,
      /*@in@*/ /*@notnull@*/
      xps_partname_t *partname) ;

/* ============================================================================
* Log stripped */
#endif
