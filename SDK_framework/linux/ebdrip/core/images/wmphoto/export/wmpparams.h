/** \file
 * \ingroup hdphoto
 *
 * $HopeName: COREwmphoto!export:wmpparams.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API into WMPhoto parameter access
 */


#ifndef __WMPPARAMS_H__
#define __WMPPARAMS_H__ (1)

/** \brief
 * Currently supported HDPhoto parameters
 */
typedef struct WMPPARAMS {
  Bool f_abort_on_unknown;        /**< Abort on unknown tiff tags and types */
  Bool f_verbose;                 /**< Emit warnings */
  Bool f_list_ifd_entries;        /**< List IFD entries */
  Bool f_strict;                  /**< Strict TIFF IFD entry checks */
  int32 max_decode_buffer_size;  /**< buffer size for decoding */
  USERVALUE defaultresolution[2]; /**< default resolution */
} WMPPARAMS;

#endif /* !__WMPPARAMS_H__ */


/* Log stripped */
