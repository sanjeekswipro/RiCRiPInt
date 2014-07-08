/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!export:psdevuri.h(EBDSDK_P.1) $
 * $Id: export:psdevuri.h,v 1.16.8.1.1.1 2013/12/19 11:25:09 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to manipulate PS device URI's.
 */

#ifndef __PSDEVURI_H__
#define __PSDEVURI_H__

#include "hqnuri.h"   /* URI interface */
#include "fileioh.h"  /* FILELIST */
#include "swdevice.h" /* STAT */

/* Convert an absolute psdev URI to a PS filename. The PS filename will have
 * the device specified.
 * The caller is responsible for freeing the string returned via
 * mm_free_with_header from the mm_xml_pool. Example:
 *
 * "psdev://%DeviceName%/Filename"
 */
extern
Bool ps_filename_from_psdev_abs_uri(
      hqn_uri_t *psdev_abs_uri,
      uint8 **ps_filename,
      uint32 *ps_filename_len) ;

/* Creates a new psdev URI from an open file.
 * The caller is responsible for freeing the psdev_uri returned via
 * hqn_uri_free.
 */
extern
Bool psdev_uri_from_open_file(
      FILELIST* flptr,
      hqn_uri_t **psdev_abs_uri) ;

/* Creates a new psdev base URI from an open file.
 * The caller is responsible for freeing the psdev_uri returned via
 * hqn_uri_free.
 */
extern
Bool psdev_base_uri_from_open_file(
      FILELIST* flptr,
      hqn_uri_t **psdev_base_uri) ;

/**
 * \brief
 * Open a URI for readonly access returning a PS file object for the filestream.
 *
 * \param[in] uri
 * Pointer to HQN URI structure.
 * \param[out] ofile
 * Pointer to returned PS filestream for URI.
 *
 * \return
 * \c TRUE if successfully opened URI for reading, else \c FALSE.
 */
extern
Bool open_file_from_psdev_uri(
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *uri,
      /*@in@*/ /*@notnull@*/
      OBJECT *ofile,
      Bool implicit_close_file) ;

/**
 * \brief
 * Returns the PS filename and length equivalent for the uri.
 *
 * \param[in] psdev_abs_uri
 * Pointer to HQN URI structure.
 * \param[out] ps_filename
 * Pointer to returned PS filename for URI.
 * \param[out] ps_filename_len
 * Pointer to returned PS filename length for URI.
 *
 * \return
 * \c TRUE if successfully opened URI for reading, else \c FALSE.
 */
Bool psdev_uri_to_ps_filename(
      /*@in@*/ /*@notnull@*/
      hqn_uri_t *psdev_abs_uri,
      /*@in@*/ /*@notnull@*/
      uint8 **ps_filename,
      /*@in@*/ /*@notnull@*/
      uint32 *ps_filename_len) ;

/**
 * \brief
 * Open a URI for readonly access returning a PS file object for the filestream.
 *
 * \param[in] istr_uri
 * Pointer to a interned string representing the URI.
 * \param[out] ofile
 * Pointer to returned PS filestream for URI.
 *
 * \return
 * \c TRUE if successfully opened URI for reading, else \c FALSE.
 */
Bool open_file_from_istr_psdev_uri(
      /*@in@*/ /*@notnull@*/
      xmlGIStr *istr_uri,
      /*@in@*/ /*@notnull@*/
      OBJECT *ofile,
      Bool implicit_close_file) ;

/** \brief Do a file stat for a psdev uri.
 *
 * \param[in] uri
 * Pointer to HQN URI structure.
 * \param[out] exists
 * Pointer to returned psdev file exists.
 * \param[out] stat
 * Pointer to returned stat structure. Only valid if \c exists is TRUE.
 *
 * \return
 *\c TRUE if file exists, else \c FALSE.
 */
extern
Bool stat_from_psdev_uri(
/*@in@*/ /*@notnull@*/
  hqn_uri_t*  uri,
/*@out@*/ /*@notnull@*/
  Bool*       exists,
/*@out@*/ /*@notnull@*/
  STAT*       stat);

/* Creates a new filter URI from an open filter (e.g. from an RSD).
 * The caller is responsible for freeing the filter_uri returned via
 * hqn_uri_free.  The uri and a base uri are identical for a filter.
 * An example:
 * "filter://ReusableStreamDecode/1234"
 */
extern
Bool uri_from_open_filter(
      FILELIST* flptr,
      hqn_uri_t **filter_uri) ;

/* Close a file. */
void xml_file_close(OBJECT *ofile) ;

/* ============================================================================
* Log stripped */
#endif /*!__PSDEVURI_H__*/
