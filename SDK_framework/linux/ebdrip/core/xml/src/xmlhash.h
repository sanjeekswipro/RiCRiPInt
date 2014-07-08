/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlhash.h(EBDSDK_P.1) $
 * $Id: src:xmlhash.h,v 1.5.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * xml hash function
 */

#ifndef __XMLHASH_H__
#define __XMLHASH_H__ (1)

#include "core.h"

uint32 xml_strhash(
/*@in@*/ /*@notnull@*/
  const uint8* p_string,
  uint32 str_len);

/* ============================================================================
* Log stripped */
#endif /*!__XMLHASH_H__*/
