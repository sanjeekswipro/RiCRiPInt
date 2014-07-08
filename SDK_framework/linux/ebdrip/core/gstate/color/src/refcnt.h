/** \file
 * \ingroup gstate
 *
 * $HopeName: COREgstate!color:src:refcnt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Macros to assist in sharing clink structures between color chains
 * and graphics state.  The debug versions contain assertions.
 */

#ifndef __CLINK_REFCNT_H__
#define __CLINK_REFCNT_H__

/****************************************************************************/

/**
 * Splint cannot parse a header for intrinics on Windows.
 * The workaround here is to define refCnt to int32 just for the Splint checking
 * phase.  hq_atomic_counter_t is still used on all platforms for the actual
 * compilation.
 */
#if defined(WIN32) && defined(S_SPLINT_S)

typedef int32 cc_counter_t;

#define HqAtomicIncrement(ptr, before) MACRO_START \
  before = *ptr; \
  ++(*ptr); \
MACRO_END

#define HqAtomicDecrement(ptr, after) MACRO_START \
  --(*ptr); \
  after = *ptr; \
MACRO_END

#else

/* Non-Windows Splint checking phase OR compilation phase for all platforms. */
#include "hqatomic.h"
typedef hq_atomic_counter_t cc_counter_t;

#endif

/****************************************************************************/

#ifdef ASSERT_BUILD
void cc_checkRefcnt(cc_counter_t *refCnt);
#define CHECK_REFCNT(_refCnt)  cc_checkRefcnt(_refCnt)
#else
/* A hacky way to allow CLINK_OWNER to return a value and do the checking */
#define CHECK_REFCNT(_refCnt)  *_refCnt = *_refCnt
#endif

#define CLINK_OWNER(link) \
  (CHECK_REFCNT(&(link)->refCnt), \
   (link)->refCnt == 1)

#define CLINK_RESERVE(link) MACRO_START \
  cc_counter_t before; \
  CHECK_REFCNT(&(link)->refCnt); \
  HqAtomicIncrement(&(link)->refCnt, before) ; \
  HQASSERT(before != 0, "Reference count uninitialised") ; \
MACRO_END

#define CLINK_RELEASE(linkRef, freeFn) MACRO_START \
  cc_counter_t after; \
  CHECK_REFCNT(&(*(linkRef))->refCnt); \
  HqAtomicDecrement(&(*(linkRef))->refCnt, after); \
  if ( after == 0 ) \
    (*freeFn)(*linkRef); \
  *(linkRef) = NULL; \
MACRO_END

/** About to change link and therefore must copy link if not already the owner.
   The release on the original link may reduce the refcnt to zero if another
   thread released a reference during the update.  In this case the copy is a
   bit of a waste, but not harmful. */
#define CLINK_UPDATE(LinkType, linkRef, copyFn, freeFn) MACRO_START \
  LinkType *link = *(linkRef); \
  cc_counter_t before; \
  HQASSERT((link) == *(linkRef), "link/linkRef unexpected"); \
  CHECK_REFCNT(&(link)->refCnt); \
  HqAtomicIncrement(&(link)->refCnt, before) ; \
  HQASSERT(before != 0, "Reference count uninitialised") ; \
  if ( before != 1 ) { \
    cc_counter_t after; \
    if ( !(*copyFn)(link, linkRef) ) \
      return FALSE; \
    HqAtomicDecrement(&(link)->refCnt, after); \
    HQASSERT(after != 0, "Reference count can't be zero yet") ; \
  } \
  CLINK_RELEASE(&(link), freeFn); \
MACRO_END

/* Log stripped */
#endif /* __CLINK_REFCNT_H__ */
