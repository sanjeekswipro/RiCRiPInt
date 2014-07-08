/** \file
 * \ingroup multi
 *
 * $HopeName: SWmulti!src:ripmulti.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of rip multi-threading architecture
 */

#include "core.h"
#include "coreinit.h"
#include "ripmulti.h"
#include "mlock.h"
#include "swstart.h"
#include "hqassert.h"
#include "hqatomic.h"

#ifdef ASSERT_BUILD
/* These are only used for a smoke-test of the hqatomic operators. They're
   deliberately global scope so that the compiler is less inclined to
   optimise them away. */
hq_atomic_counter_t unit_test_atomic, *unit_test_atomic_ptr ;
#endif

/** Initialisation table for SWmulti, recursively calls multirip compound init
 * functions. */
static
core_init_t ripmulti_init[] = {
  CORE_INIT("mlock", mlock_C_globals)
};

/** File runtime initialisation */
static void init_C_globals_ripmulti(void)
{
#ifdef ASSERT_BUILD
  unit_test_atomic = 42 ;
  unit_test_atomic_ptr = &unit_test_atomic ;
#endif
}

static Bool multi_render_swinit(SWSTART *params)
{
#ifdef ASSERT_BUILD
  {
    hq_atomic_counter_t before, after = 99 ;
    Bool swapped ;

    HqAtomicIncrement(&unit_test_atomic, before) ;
    HQASSERT(unit_test_atomic == 43 && before == 42 && after == 99,
             "Atomic increment failed") ;
    HqAtomicCASPointer(&unit_test_atomic_ptr, &before, &after,
                       swapped, hq_atomic_counter_t) ;
    HQASSERT(BOOL_IS_VALID(swapped), "CAS swapped boolean is not valid") ;
    HQASSERT(!swapped &&
             unit_test_atomic_ptr == &unit_test_atomic &&
             unit_test_atomic == 43 && before == 42 && after == 99,
             "Atomic pointer CAS should not have swapped") ;
    HqAtomicCASPointer(&unit_test_atomic_ptr, &unit_test_atomic, &before,
                       swapped, hq_atomic_counter_t) ;
    HQASSERT(BOOL_IS_VALID(swapped), "CAS swapped boolean is not valid") ;
    HQASSERT(swapped &&
             unit_test_atomic_ptr == &before &&
             unit_test_atomic == 43 && before == 42 && after == 99,
             "Atomic pointer CAS should have swapped") ;
    HqAtomicDecrement(unit_test_atomic_ptr, after) ;
    HQASSERT(unit_test_atomic == 43 && before == 41 && after == 41,
             "Atomic decrement failed") ;
    HqAtomicCAS(&unit_test_atomic, before, 42, swapped) ;
    HQASSERT(BOOL_IS_VALID(swapped), "CAS swapped boolean is not valid") ;
    HQASSERT(!swapped &&
             unit_test_atomic == 43 && before == 41 && after == 41,
             "Atomic CAS should not have swapped") ;
    HqAtomicIncrement(&after, before) ;
    HQASSERT(unit_test_atomic == 43 && before == 41 && after == 42,
             "Atomic increment failed") ;
    HqAtomicCAS(&unit_test_atomic, 43, after, swapped) ;
    HQASSERT(BOOL_IS_VALID(swapped), "CAS swapped boolean is not valid") ;
    HQASSERT(swapped &&
             unit_test_atomic == 42 && before == 41 && after == 42,
             "Atomic CAS should have swapped") ;
  }
#endif

  return (core_swinit_run(ripmulti_init, NUM_ARRAY_ITEMS(ripmulti_init),
                          params));
}

static
Bool multi_render_swstart(
  struct SWSTART* params)
{
  return (core_swstart_run(ripmulti_init, NUM_ARRAY_ITEMS(ripmulti_init),
                           params));
}

static
Bool multi_render_postboot(void)
{
  return (core_postboot_run(ripmulti_init, NUM_ARRAY_ITEMS(ripmulti_init)));
}

static
void multi_render_finish(void)
{
  core_finish_run(ripmulti_init, NUM_ARRAY_ITEMS(ripmulti_init));
}

/** Compound runtime initialisation */
void multi_render_C_globals(core_init_fns *fns)
{
  init_C_globals_ripmulti() ;

  fns->swinit = multi_render_swinit ;
  fns->swstart = multi_render_swstart ;
  fns->postboot = multi_render_postboot ;
  fns->finish = multi_render_finish ;
  core_C_globals_run(ripmulti_init, NUM_ARRAY_ITEMS(ripmulti_init)) ;
}


/* Log stripped */
