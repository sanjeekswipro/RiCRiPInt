/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:coreinit.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * SWcore initialisation.
 *
 * Each top-level core compound will have an entry in the global
 * initialisation table. Some modular functionality may also be treated
 * separately from the compound in which it is located.
 *
 * There are four initialisation phases; the init function for any of these
 * phases may be NULL, in which case that phase will be missed out. The
 * initialisation phases are:
 *
 * init_C_globals:
 *
 *   This phase is intended to reset the values of static and global
 *   variables to their initial state. It MUST NOT make any conditional
 *   tests, and it MUST NOT perform any operation that may fail. In
 *   principle, the initialiser for this phase may be called multiple times,
 *   and it may also be called after RIP shutdown.
 *
 * SwInit:
 *
 *   This phase is called during SwInit(). It is primarily used for modules
 *   which may receive SwRegister*() calls between SwInit and SwStart to
 *   initialise resources they may need, and for early operations such as
 *   starting up the memory subsystem (especially creating memory pools). The
 *   SWSTART parameter list is available to this operation.
 *
 * SwStart:
 *
 *   This is the main startup phase. The bulk of resource acquisition should
 *   be done during this phase. The SWSTART parameter list is available
 *   to this operation.
 *
 * Post-boot:
 *
 *   The post-boot phase happens when all other initialisation has been done,
 *   and the top-level PostScript server loop interpreter is about to be
 *   entered. Initialisation that relies on the PostScript environment being
 *   present should be done in this phase.
 *
 * All of the initialisation functions for a phase are called in turn. If any
 * of the SwInit, SwStart, or Post-boot functions returns FALSE or calls
 * dispatch_SwExit(), then the modules will be finalised in reverse order. If
 * any of the SwInit, SwStart, or Post-boot functions for a module have ever
 * returned TRUE since the init_C_globals phase completed, the finish
 * function will be called for that module.
 *
 * Initialisation for sub-compounds is devolved to the higher-level compound.
 * A compound with sub-compounds of its own should have its own init table,
 * and should call the appropriate init/finish functions from its own
 * init/finish routine.
 */

#ifndef __COREINIT_H__
#define __COREINIT_H__

struct SWSTART ; /* from core interface */

/** \brief Initialisation and finalisation functions for startup phases.

    These functions are wrapped up in a structure because the runtime
    initialisation for a module can opt to set them. This design exposes as
    little surface area as possible to the rest of the RIP, allowing modules
    to change the phase(s) in which they initialise without touching other
    parts of the RIP. */
typedef struct core_init_fns {
  /** Preboot initialisation function. All of the preboot functions are
      called in SwInit(). They can be used to set up state suitable for
      SwRegister*() functions. */
  Bool (*swinit)(struct SWSTART *params) ;

  /** Boot initialisation function. The boot functions are called during
      SwStart(), to prepare the interpreter and RIP state. Most modules
      should use this function for their main initialisation. */
  Bool (*swstart)(struct SWSTART *params) ;

  /** Post-boot initialisation function. The postboot functions are called at
      the end of SwStart(), just before the top-level interpreter loop is
      run. The PostScript environment will have been fully initialised at
      this point. The \c SWSTART list is not available for postboot
      functions. */
  Bool (*postboot)(void) ;

  /** Finalisation function. These are called in reverse order when the RIP
      shuts down, but only if one of the \c swinit, \c swstart, or \c
      postboot functions succeeded. */
  void (*finish)(void) ;
} core_init_fns ;

/** \brief Initialisation list consists of an array of these */
typedef struct core_init_t {
  /** The sub-system name. This is used to construct a default error name. */
  char *name ;

  /** C runtime initialisation. This should return static and global
      variables to the same state as at program load. It MUST NOT perform
      any resource allocation action, or action with I/O side-effects.

     \param[out] fns This routine must fill in pointers to the remaining
                     initialisation and finalisation routines for the module.
                     All of these pointers will be reset to NULL before
                     calling this routine.

    If present, this routine \e must initialise all global and file-scoped
    static variables in a module. It will be called before any other routine
    in a module is called, and immediately after the module is shut down.
    This routine *must* be idempotent.

    If this routine is not present, is is still possible to have an init
    table entry, but all of the \c core_init_fns must be specified directly.
    This alternate method is used for managing startup/shutdown sequences
    within a single source file. */
  void (*init_C_globals)(core_init_fns *fns) ;

  /** Remaining initialisation functions. These are set by the \c
      init_C_globals function. */
  core_init_fns fns ;

  /** A flag indicating if the finish function should be called. This will
      be true if any of the init functions succeeded. */
  Bool needs_finish ;
} core_init_t ;

/** \brief Macro used to initialise core init table module entrie.

     The module name must be a constant string. A single runtime init
     parameter is required, which is used to set up the remaining init phase
     functions. */
#define CORE_INIT(name_, runtime_) \
  { ("" name_ ""), runtime_, { NULL, NULL, NULL, NULL }, FALSE }

/** \brief Macro used to initialise a local core init table entry.

     The module name must be a constant string. Phase function pointers are
     required instead of a C runtime pointer. The initialiser's C runtime
     initialisation must be done by the parent module. */
#define CORE_INIT_LOCAL(name_, swinit_, swstart_, postboot_, finish_) \
  { ("" name_ ""), NULL, { swinit_, swstart_, postboot_, finish_ }, FALSE }

/** \brief Run all of the init_C_globals functions for an initialiser array,
    resetting all of the needs_finish flags to FALSE. */
void core_C_globals_run(core_init_t initialisers[], size_t n_init) ;

/** \brief Run all of the swinit functions for an initialiser array, cleaning
    up by running the finalisers if any initialiser fails. */
Bool core_swinit_run(core_init_t initialisers[], size_t n_init,
                     struct SWSTART *params) ;

/** \brief Run all of the swstart functions for an initialiser array,
    cleaning up by running the finalisers if any initialiser fails. */
Bool core_swstart_run(core_init_t initialisers[], size_t n_init,
                      struct SWSTART *params) ;

/** \brief Run all of the postboot functions for an initialiser array,
    cleaning up by running the finalisers if any initialiser fails. */
Bool core_postboot_run(core_init_t initialisers[], size_t n_init) ;

/** \brief Run all of the finish functions for an initialiser array,
    resetting the needs_finish flags to FALSE when done. */
void core_finish_run(core_init_t initialisers[], size_t n_init) ;

#endif /* Protection from multiple inclusion */

/* Log stripped */
