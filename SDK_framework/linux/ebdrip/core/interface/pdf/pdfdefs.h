/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_pdf!pdfdefs.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * Contains definitions of values used in pdf operators
 */

#ifndef __PDFDEFS_H__
#define __PDFDEFS_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Allowed values for PDFPARAMS::PageCropTo
 * note that the ordering of the following constants is important: if
 * we choose to crop to the artbox, but the artbox isn't specified, then
 * we should choose the next enclosing box, which is the trimbox, and then
 * the bleedbox and then the mediabox.  Hence if b1 < b2 then b1 "encloses" b2
 */

enum {
  PDF_PAGECROPTO_MEDIABOX = 0,
  PDF_PAGECROPTO_BLEEDBOX,
  PDF_PAGECROPTO_TRIMBOX,
  PDF_PAGECROPTO_ARTBOX,
  PDF_PAGECROPTO_CROPBOX,

  PDF_PAGECROPTO_N_VALUES
};

/* Allowed values for PDFPARAMS::EnforcePDFVersion.
 * These must correspond to the choices offered to the user, both in the gui
 * and by values set for EnforcePDFVersion pdf parameter.
 */
#define PDF_ACCEPT_AUTO_DETECT           (0)
#define PDF_ACCEPT_ANY                   (1)
#define PDF_ACCEPT_X1a                   (2)
#define PDF_ACCEPT_X3_X1a                 (3)
#define PDF_ACCEPT_X4_X3_X1a              (4)
#define PDF_ACCEPT_X4p_X4_X3_X1a          (5)
#define PDF_ACCEPT_X5g_X5pg_X4p_X3_X3_X1a (6)
#define PDF_ACCEPT_NUM_VALUES             (7)

/* Allowed values for PDFPARAMS::AbortForInvalidTypes
 */
#define PDF_INVALIDTYPES_WARN      (0)
#define PDF_INVALIDTYPES_ABORT     (1)
#define PDF_INVALIDTYPES_N_VALUES  (2)

#ifdef __cplusplus
}
#endif


#endif  /* __PDFDEFS_H__ */
