/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!export:t6params.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to access Tiff level 6 parameters
 */


#ifndef __T6PARAMS_H__
#define __T6PARAMS_H__ (1)

/** \brief
 * Currently supported TIFF parameters
 */
typedef struct TIFF6PARAMS {
  Bool f_abort_on_unknown;   /**< Abort on unknown tiff tags and types */
  Bool f_verbose;            /**< Emit warnings */
  Bool f_list_ifd_entries;   /**< List IFD entries */
  Bool f_ignore_orientation; /**< Ignore orientation in IFD */
  Bool f_adjust_ctm;         /**< Do scale and concat */
  Bool f_do_setcolorspace;   /**< Do setcolorspace based on PMI */
  Bool f_install_iccprofile; /**< Install ICC profile if one is present */
  Bool f_invert_image;       /**< Invert the TIFF image */
  Bool f_do_imagemask;       /**< Use tiff image as mask */
  Bool f_do_pagesize;        /**< Use tiff image size for pagesize */
  Bool f_ignore_ES0;         /**< ignore ES0 data */
  Bool f_ES0_as_ES2;         /**< treat ES0 as if it were ES2 */
  Bool f_no_units_same_as_inch; /**< if a file has no specified units use inch */
  Bool f_disablelineworkerror;  /**< toggle linework error */
  Bool f_strict;             /**< Strict TIFF IFD entry checks */
  USERVALUE defaultresolution[2]; /**< default resolution */
} TIFF6PARAMS;

#endif /* !__T6PARAMS_H__ */


/* Log stripped */
