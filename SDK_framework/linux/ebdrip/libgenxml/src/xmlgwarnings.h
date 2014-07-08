#ifndef __XMLGWARNINGS_H__
#define __XMLGWARNINGS_H__
/* ============================================================================
 * $HopeName: HQNgenericxml!src:xmlgwarnings.h(EBDSDK_P.1) $
 * $Id: src:xmlgwarnings.h,v 1.5.11.1.1.1 2013/12/19 11:24:21 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Private interface to XMLG_UNUSED_PARAM.
 */

/* Copied from HQNc-standard!warnings.h */

#if defined(lint) || defined(NO_DUMMYPARAM)

# define XMLG_UNUSED_PARAM(type, param) /* VOID */

#else  /* !defined(lint) && [!NO_DUMMYPARAM] */

#if defined(MAC68K) && !defined(__GNUC__) && !defined(__SC__)
       /* define XMLG_UNUSED_PARAM for MPW "C" compiler, whose optimizer is dumb */
#define XMLG_UNUSED_PARAM(type, param) \
  { param; }
  
#else

#ifdef SGI
       /* The SGI compiler can use the definition used below. */
#define XMLG_UNUSED_PARAM(type, param) \
  (void)param;

#else
       /* definition of XMLG_UNUSED_PARAM for all other compilers */
#define XMLG_UNUSED_PARAM(type, param) \
  { \
     type dummy_param; \
     type dummy_param2; \
     dummy_param = param; \
     dummy_param2 = dummy_param; \
     dummy_param = dummy_param2; \
  }

#endif /* SGI */
#endif /* which compiler? */
#endif /* defined(HAS_PRAGMA_UNUSED) || defined(lint) */

/* ============================================================================
* Log stripped */
#endif
