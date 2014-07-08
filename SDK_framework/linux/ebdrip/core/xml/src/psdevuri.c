/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:psdevuri.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of mapping PS filenames to psdev URI's and vice
 * versa.
 *
 * Within the RIP, we introduce a new URI scheme called psdev:. This allows
 * us to map URI's to PS filenames which exist on a particular device. The
 * device name is encoded into the URI authority. Example:
 * "psdev://<DeviceName>/<filename>"
 */

#include "core.h"
#include "gcscan.h"    /* ps_scan_field */
#include "mmcompat.h"  /* mm_alloc_with_header etc.. */
#include "mps.h"       /* mps_root_t */
#include "fileio.h"    /* FILELIST */

#include "xml.h"       /* xml routines */
#include "psdevuri.h"  /* this interface */
#include "swerrors.h"  /* error_handler */
#include "hqmemcpy.h"  /* HqMemCpy */
#include "swcopyf.h"   /* swcopyf */

#define PSDEV_SCHEME_NAME_LEN 8
const uint8* psdev_scheme_name = (uint8*) "psdev://";

#define FILTER_SCHEME_NAME_LEN 9
const uint8* filter_scheme_name = (uint8*) "filter://";

typedef struct XML_FILE {
  FILELIST *flptr ;
  struct XML_FILE *next ;
  struct XML_FILE *prev ;
} XML_FILE ;

static mps_root_t xml_file_root ;
static XML_FILE *first_file ;

static
mps_res_t MPS_CALL xml_file_root_scan(mps_ss_t  ss, void *p, size_t s)
{
  XML_FILE *curr = first_file ;

  UNUSED_PARAM(void*, p) ;
  UNUSED_PARAM(size_t, s) ;

  MPS_SCAN_BEGIN(ss) ;
  while (curr != NULL) {
    MPS_RETAIN(&curr->flptr, TRUE);
    curr = curr->next ;
  }
  MPS_SCAN_END(ss) ;
  return MPS_RES_OK ;
}

static
Bool xml_file_open(OBJECT *ostr_filename, OBJECT *ofile)
{
  XML_FILE *new_file ;
  if ((new_file = mm_alloc_with_header(mm_xml_pool, sizeof(XML_FILE),
                                       MM_ALLOC_CLASS_XML_URI)) == NULL) {
    return FALSE ;
  }
  if (! file_open(ostr_filename, SW_RDONLY, READ_FLAG, FALSE, 0, ofile)) {
    mm_free_with_header(mm_xml_pool, new_file) ;
    return FALSE ;
  }

  /* MRU list */
  if (first_file != NULL) {
    HQASSERT(first_file->prev == NULL, "first_file previous is not NULL") ;
    first_file->prev = new_file ;
  }
  new_file->prev = NULL ;
  new_file->next = first_file ;
  /* Copy file pointer. */
  new_file->flptr = oFile(*ofile) ;
  first_file = new_file ;
  return TRUE ;
}

void xml_file_close(OBJECT *ofile)
{
  XML_FILE *curr_next ;
  XML_FILE *curr = first_file ;
  while (curr != NULL) {
    curr_next = curr->next ;
    if (curr->flptr == oFile(*ofile)) {
      file_close(ofile) ;
      if (curr->prev != NULL) {
        curr->prev->next = curr->next ;
      }
      if (curr->next != NULL) {
        curr->next->prev = curr->prev ;
      }
      if (curr == first_file) {
        first_file = curr->next ;
      }
      mm_free_with_header(mm_xml_pool, curr) ;
      break ;
    }
    curr = curr_next ;
  }
  HQASSERT(curr != NULL, "an open file was not in the scan root") ;
}

Bool psdevuri_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  /* Create root last so we force cleanup on success. */
  if (mps_root_create(&xml_file_root, mm_arena, mps_rank_exact(), 0,
                      xml_file_root_scan, NULL, 0) != MPS_RES_OK) {
    HQFAIL("Failed to register psdevuri file root.") ;
    return FAILURE(FALSE) ;
  }
  return TRUE ;
}

void psdevuri_finish(void)
{
  mps_root_destroy(xml_file_root) ;
}

Bool psdev_uri_to_ps_filename(
      hqn_uri_t *psdev_abs_uri,
      uint8 **ps_filename,
      uint32 *ps_filename_len)
{
  uint8 *buf, *insert_point, *authority, *path ;
  uint32 buf_len, authority_len, path_len ;

  HQASSERT(ps_filename != NULL, "ps_filename is NULL") ;
  HQASSERT(ps_filename_len != NULL, "ps_filename_len is NULL") ;
  HQASSERT(psdev_abs_uri != NULL, "psdev_abs_uri is NULL") ;

  *ps_filename = NULL ;

  /* take the authority and add percent characters around it, then
     append the path */

  if (! hqn_uri_get_field(psdev_abs_uri,
                          &authority,
                          &authority_len,
                          HQN_URI_AUTHORITY) ||
      ! hqn_uri_get_field(psdev_abs_uri,
                          &path,
                          &path_len,
                          HQN_URI_PATH)) {
    return error_handler(TYPECHECK) ;
  }

  /* Add %'s around the authority name - which is in fact the PS device name. */
  buf_len = authority_len + 2 + path_len ;

  if ((buf = mm_alloc_with_header(mm_xml_pool, buf_len,
                                  MM_ALLOC_CLASS_XML_URI)) == NULL) {
    return error_handler(VMERROR) ;
  }
  insert_point = buf ;

  HqMemCpy(insert_point, "%", 1) ;
  insert_point += 1 ;
  HqMemCpy(insert_point, authority, authority_len) ;
  insert_point += authority_len ;
  HqMemCpy(insert_point, "%", 1) ;
  insert_point += 1 ;
  HqMemCpy(insert_point, path, path_len) ;
  insert_point += path_len ;

  *ps_filename = buf ;
  *ps_filename_len = buf_len ;

  return TRUE ;
}

Bool psdev_uri_from_open_file(
      FILELIST* flptr,
      hqn_uri_t **psdev_abs_uri)
{
  hqn_uri_t *new_uri ;
  uint32 devicelen, clistlen, buf_len ;
  uint8 *insert_point ;
  uint8 buf[ PSDEV_SCHEME_NAME_LEN + LONGESTFILENAME +
             LONGESTDEVICENAME + 1 ] ;

  HQASSERT(flptr != NULL, "flptr is NULL") ;
  HQASSERT(psdev_abs_uri != NULL, "psdev_abs_uri is NULL") ;

  /* Expecting a file, not a filter. */
  if ( isIFilter(flptr) || !flptr->device )
    return error_handler(UNDEFINED) ;

  devicelen = strlen_uint32((char *)flptr->device->name) ;
  clistlen = flptr->len ; /* clist not always null terminated */

  insert_point = buf ;
  buf_len = PSDEV_SCHEME_NAME_LEN + devicelen + clistlen ;
  if (clistlen > 0 && flptr->clist[0] != '/')
    buf_len++ ;

  HqMemCpy(insert_point, psdev_scheme_name, PSDEV_SCHEME_NAME_LEN) ;
  insert_point += PSDEV_SCHEME_NAME_LEN ;
  HqMemCpy(insert_point, flptr->device->name, devicelen) ;
  insert_point += devicelen ;
  if (clistlen > 0 && flptr->clist[0] != '/')
    *(insert_point++) = '/' ;
  HqMemCpy(insert_point, flptr->clist, clistlen) ;
  insert_point += clistlen ;

  if (! hqn_uri_parse(core_uri_context,
                      &new_uri,
                      buf,
                      buf_len,
                      TRUE /* copy string */)) {
    return error_handler(TYPECHECK) ;
  }

  *psdev_abs_uri = new_uri ;

  return TRUE ;
}

Bool psdev_base_uri_from_open_file(
      FILELIST* flptr,
      hqn_uri_t **psdev_base_uri)
{
  hqn_uri_t *new_uri ;
  uint32 devicelen, buf_len ;
  uint8 *insert_point ;
  /* Allow for a / on the end */
  static uint8 buf[ PSDEV_SCHEME_NAME_LEN + LONGESTDEVICENAME + 1 ] ;
  static uint8 *rsd_scheme_name = (uint8*)"rsd://" ;

#define RSD_SCHEME_NAME_LEN (sizeof(rsd_scheme_name) - 1)

  HQASSERT(flptr != NULL, "flptr is NULL") ;
  HQASSERT(psdev_base_uri != NULL, "psdev_base_uri is NULL") ;

  insert_point = buf ;

  if (isIRSDFilter(flptr)) {
    buf_len = RSD_SCHEME_NAME_LEN ;
    HqMemCpy(insert_point, rsd_scheme_name, RSD_SCHEME_NAME_LEN) ;
  } else {
    devicelen = strlen_uint32((char *)flptr->device->name) ;
    buf_len = PSDEV_SCHEME_NAME_LEN + devicelen ;
    HqMemCpy(insert_point, psdev_scheme_name, PSDEV_SCHEME_NAME_LEN) ;
    insert_point += PSDEV_SCHEME_NAME_LEN ;
    HqMemCpy(insert_point, flptr->device->name, devicelen) ;
  }

  if (! hqn_uri_parse(core_uri_context,
                      &new_uri,
                      buf,
                      buf_len,
                      TRUE /* copy the string */)) {
    return error_handler(TYPECHECK) ;
  }

  *psdev_base_uri = new_uri ;

  return TRUE ;
}

Bool open_file_from_psdev_uri(
      hqn_uri_t *uri,
      OBJECT *ofile,
      Bool implicit_close_file)
{
  uint8 *ps_filename;
  uint32 ps_filename_len ;
  OBJECT ostr_filename ;
  Bool status ;

  HQASSERT(uri != NULL, "uri is NULL") ;

  if (! psdev_uri_to_ps_filename(uri, &ps_filename, &ps_filename_len))
    return FALSE ;

  if (ps_filename_len > LONGESTFILENAME) {
    mm_free_with_header(mm_xml_pool, ps_filename) ;
    return error_handler(LIMITCHECK) ;
  }

  theTags(ostr_filename) = OSTRING|UNLIMITED|LITERAL ;
  theLen(ostr_filename) = CAST_TO_UINT16(ps_filename_len) ;
  oString(ostr_filename) = ps_filename ;

  if (implicit_close_file) {
    status = file_open(&ostr_filename, SW_RDONLY, READ_FLAG, FALSE, 0, ofile) ;
  } else {
    status = xml_file_open(&ostr_filename, ofile) ;
  }

  mm_free_with_header(mm_xml_pool, ps_filename) ;

  return status ;
}

Bool open_file_from_istr_psdev_uri(
      xmlGIStr *istr_uri,
      OBJECT *ofile,
      Bool implicit_close_file)
{
  hqn_uri_t *uri ;
  Bool status ;

  HQASSERT(istr_uri != NULL, "istr_uri is NULL") ;

  if (! hqn_uri_parse(core_uri_context, &uri,
                      intern_value(istr_uri),
                      intern_length(istr_uri),
                      FALSE /* don't copy the string */))
    return error_handler(UNDEFINED) ;


  status = open_file_from_psdev_uri(uri, ofile, implicit_close_file) ;

  hqn_uri_free(&uri) ;

  return status ;
}

/* Do a file stat for a psdev uri. */
Bool stat_from_psdev_uri(
/*@in@*/ /*@notnull@*/
  hqn_uri_t*  uri,
/*@out@*/ /*@notnull@*/
  Bool*       exists,
/*@out@*/ /*@notnull@*/
  STAT*       stat)
{
  uint8 *ps_filename;
  uint32 ps_filename_len ;
  int32 status;

  /* Create PS filename from URI */
  if ( !psdev_uri_to_ps_filename(uri, &ps_filename, &ps_filename_len) ) {
    return(FALSE);
  }

  /* Stat the PS filename */
  oString(snewobj) = ps_filename;
  theLen(snewobj) = CAST_TO_UINT16(ps_filename_len);
  status = file_stat(&snewobj, exists, stat);

  /* Must free memory used for ps filename */
  mm_free_with_header(mm_xml_pool, ps_filename);

  return(status);

} /* stat_from_psdev_uri */

/* Creates a new filter URI from an open filter (e.g. from an RSD).
 * The caller is responsible for freeing the filter_uri returned via
 * hqn_uri_free.  The uri and a base uri are identical for a filter.
 * An example:
 * "filter://ReusableStreamDecode/1234"
 */
Bool uri_from_open_filter(
      FILELIST* flptr,
      hqn_uri_t **filter_uri)
{
  hqn_uri_t *new_uri ;
  uint8 *clist ;
  uint32 clistlen, buf_len ;
  uint8 *insert_point ;
#define FILTER_URI_TRAILOR 6 /* '/' + id */
  static uint8 buf[ FILTER_SCHEME_NAME_LEN + LONGESTFILENAME +
                    FILTER_URI_TRAILOR + 1 /* '\0' */ ] ;

  HQASSERT(flptr != NULL, "flptr is NULL") ;
  HQASSERT(filter_uri != NULL, "filter_uri is NULL") ;

  /* Expecting a filter, not a file. */
  if ( !isIFilter(flptr) || flptr->device )
    return error_handler(UNDEFINED) ;

  clist = flptr->clist ;
  clistlen = flptr->len ; /* clist not always null terminated */
  if ( clist[0] == '%' ) {
    /* Drop the leading '%' character (e.g. change %stringDecode to
       stringDecode) to avoid the % breaking the URI parse. */
    ++clist ;
    --clistlen ;
  }

  insert_point = buf ;
  buf_len = FILTER_SCHEME_NAME_LEN + clistlen + FILTER_URI_TRAILOR ;

  HqMemCpy(insert_point, filter_scheme_name, FILTER_SCHEME_NAME_LEN) ;
  insert_point += FILTER_SCHEME_NAME_LEN ;
  HqMemCpy(insert_point, clist, clistlen) ;
  insert_point += clistlen ;

  swcopyf(insert_point, (uint8*)"/%.5d%N", flptr->filter_id) ;
  /* null terminated by swcopyf */
  HQASSERT(strlen((char*)insert_point) == FILTER_URI_TRAILOR,
           "uri trailor ended up a different length than expected") ;
  HQASSERT(buf_len <= FILTER_SCHEME_NAME_LEN + LONGESTFILENAME + FILTER_SCHEME_NAME_LEN,
           "filter uri has run off the end of the temp buffer") ;

  if (! hqn_uri_parse(core_uri_context,
                      &new_uri,
                      buf,
                      buf_len,
                      TRUE /* copy the string */)) {
    return error_handler(TYPECHECK) ;
  }

  *filter_uri = new_uri ;

  return TRUE ;
}

void init_C_globals_psdevuri(void)
{
  xml_file_root = NULL ;
  first_file = NULL ;
}

/* ============================================================================
* Log stripped */
