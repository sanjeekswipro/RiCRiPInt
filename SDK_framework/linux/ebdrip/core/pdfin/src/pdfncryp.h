/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfncryp.h(EBDSDK_P.1) $
 * $Id: src:pdfncryp.h,v 1.12.4.1.1.1 2013/12/19 11:25:14 anon Exp $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface to PDF decryption routines.
 */

#ifndef __PDFNCRYP_H__
#define __PDFNCRYP_H__

extern Bool pdf_decrypt_string(
      PDFXCONTEXT *pdfxc,
      uint8 *string_in,
      uint8 *string_out,
      int32 len,
      int32 *newlen,
      uint32 objnum,
      uint32 objgen) ;

extern Bool pdf_insert_decrypt_filter(
      PDFCONTEXT *pdfc,
      OBJECT *pdfobj,
      OBJECT *filter,
      OBJECT *decodeparams,
      uint32 objnum,
      uint32 objgen,
      Bool is_stream) ;

extern Bool pdf_begin_decryption(
      PDFCONTEXT *pdfc,
      OBJECT *encryptionobj,
      OBJECT *file_id) ;

extern void pdf_end_decryption(
      PDFCONTEXT *pdfc) ;

extern void set_reading_metadata(
    PDFCONTEXT *pdfc,
    Bool are_we) ;

/* ============================================================================
* Log stripped */
#endif
