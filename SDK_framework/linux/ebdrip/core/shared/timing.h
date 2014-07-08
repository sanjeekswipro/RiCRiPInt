/** \file
 * \ingroup debug
 *
 * $HopeName: SWcore!shared:timing.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to implement fine-grained execution profiling.
 *
 * Normally, an execution profile will show timings for all functions for
 * interpretation, rendering, and in the skin. This interface allows
 * execution profiling to be suspended or resumed at various points in the
 * code. This interface is exposed by SWv20 in an internaldict operator which
 * allows PostScript control of profiling. The interface also allows internal
 * calls to enable and disable profiling at a granularity that is not
 * amenable to PostScript control.
 */

#ifndef __TIMING_H__
#define __TIMING_H__

#include "swtrace.h"

#ifdef PROBE_BUILD

extern SwTraceHandlerFn *probe_handler ;

/** Indicate we're starting a functional block. */
#define probe_begin(_p, _d) MACRO_START \
  HQASSERT((_p) > SW_TRACE_INVALID && (_p) < CORE_TRACE_N, \
           "Invalid trace identifier") ; \
  if ( probe_handler != NULL ) \
    (*probe_handler)(_p, SW_TRACETYPE_ENTER, (intptr_t)(_d)) ; \
MACRO_END

/** Indicate we're finishing a functional block. */
#define probe_end(_p, _d) MACRO_START \
  HQASSERT((_p) > SW_TRACE_INVALID && (_p) < CORE_TRACE_N, \
           "Invalid trace identifier") ; \
  if ( probe_handler != NULL ) \
    (*probe_handler)(_p, SW_TRACETYPE_EXIT, (intptr_t)(_d)) ; \
MACRO_END

/** Issue a non begin/end probes to the skin. */
#define probe_other(_p, _t, _d) MACRO_START \
  HQASSERT((_p) >= 0, "Invalid trace identifier") ; \
  HQASSERT((_t) > SW_TRACETYPE_INVALID && \
           (_t) != SW_TRACETYPE_ENTER && (_t) != SW_TRACETYPE_EXIT && \
           (_t) < CORE_TRACETYPE_N, \
           "Invalid trace type") ; \
  if ( probe_handler != NULL ) \
    (*probe_handler)((_p), (_t), (intptr_t)(_d)) ; \
MACRO_END

#else /* !PROBE_BUILD */

#define probe_begin(_p, _d) EMPTY_STATEMENT()
#define probe_end(_p, _d) EMPTY_STATEMENT()
#define probe_other(_p, _t, _d) EMPTY_STATEMENT()

#endif /* !PROBE_BUILD */

/** Wrapper for profiling around a statement. This wrapper is thread-safe,
    and will generate an enter/exit event pair every time it is used. This
    macro should NOT be re-defined to compile to nothing, it is necessary to
    execute the statement for all build flavours. */
#define PROBE(trace_, designator_, statement_) MACRO_START \
  probe_begin((trace_), (designator_)) ; \
  statement_ ; \
  probe_end((trace_), (designator_)) ; \
MACRO_END

/*
Log stripped */
#endif /* Protection from multiple inclusion */
