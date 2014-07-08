/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_metro!printticket.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Microsoft XPS Print Ticket interface
 */

#ifndef __PRINTTICKET_H__
#define __PRINTTICKET_H__ (1)

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Microsoft XPS Print Ticket Device number */
#define XPSPT_DEVICE_TYPE       (27)

/* Error codes picked up by the RIP */
#define XPSPT_ERROR_OUTOFMEM    (1)   /**< \brief Memory allocation failed */
#define XPSPT_ERROR_SYNTAX      (2)   /**< \brief Invalid XML or attribute value syntax */
#define XPSPT_ERROR_RANGECHECK  (3)   /**< \brief Attribute value is out of range */
#define XPSPT_ERROR_LIMITCHECK  (4)   /**< \brief Internal imlementation limit has been reached */

/* Special values for the NextPage device parameter. */
#define XPSPT_PAGES_ALL         (-1)  /**< \brief Render all remaining pages in the current document */
#define XPSPT_PAGES_NOMORE      (-2)  /**< \brief Do not render any more pages in the current document */
#define XPSPT_COUNT_PAGES       (-3)  /**< \brief Count pages without rendering any pages */

/* If pages are counted, one must explicitly specify each page after
   that to print them. i.e. You may no longer use XPSPT_PAGES_ALL as a
   return from the NextPage param. */

#ifdef __cplusplus
}
#endif


#endif /* !__PRINTTICKET_H__ */
