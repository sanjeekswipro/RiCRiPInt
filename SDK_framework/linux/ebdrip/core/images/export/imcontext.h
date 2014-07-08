/** \file
 * \ingroup images
 *
 * $HopeName: COREimages!export:imcontext.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image sub-core context
 */

#ifndef __IMCONTEXT_H__
#define __IMCONTEXT_H__

/** \brief Core context sub-pointer for per-thread image data. */
typedef struct im_context_t {
  struct IM_BLOCK *blockinuse ; /**< Block in use for this thread. */
  struct IM_EXPBUF *expbuf ;    /**< Expander buffer for this thread. */
} im_context_t ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
