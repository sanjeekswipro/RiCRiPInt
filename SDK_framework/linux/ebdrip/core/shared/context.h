/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:context.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Core RIP contexts. These are built out of definitions which are common to
 * the configuration of the core RIP being built. This file should NOT include
 * PDL-specific header files; the context may contain pointers to structs and
 * variables defined in PDL-specific files.
 *
 * Each module in the core RIP can require variables to be present in the
 * input context; the module should include this header, and then (preferably)
 * define its own macros based on CurrentContext to access the variables
 * it put there. Compounds should access each other's context information
 * through headers which the sibling compounds export, not directly through
 * CurrentContext.
 */

#ifndef __CONTEXT_H__
#define __CONTEXT_H__

/** \defgroup core Core RIP layer */
/** \{ */

/** \brief Per-thread context structure.

    This structure should contain incomplete type pointers for each subsystem
    that needs to store per-thread or globally accessible data. Where possible,
    the pointer to this structure should be passed down the call hierarchy,
    rather than retrieved via \c get_core_context(). */
typedef struct corecontext_t {
  /** Error context */
  struct error_context_t *error ;

  /** Interpreter thread? This is really a Bool, but this header is used
      in SWtruetype, where that isn't defined. Fake it for now. */
  int is_interpreter ;

  /** Thread index for SwThreadIndex(). \todo ajcd 2010-10-17: I don't like
      SwThreadIndex(), it feels like whatever it's indexing should be part of
      this structure instead. */
  unsigned int thread_index ;

  /* Page DL state */
  struct DL_STATE *page ;

  /* Object system context */
  int32 savelevel ;
  int32 glallocmode ;

  struct mm_context_t *mm_context;
  int between_operators; /* for low-mem actions, really a Bool */

  /** Halftone cache context. */
  struct ht_context_t *ht_context ;

  /** Image system context. */
  struct im_context_t *im_context ;

  /** Thread pool system context. This is private to the thread pool system, it
      should not be modified by any other part of the system. */
  struct task_context_t *taskcontext ;

  /* PostScript interpreter context. */
  struct ps_context_t *pscontext ;

  /* PostScript/configuration context */
  struct mischookparams *mischookparams ;
  struct userparams *userparams ;
  struct SYSTEMPARAMS *systemparams ;
  struct charcontext_t *charcontext ;

  /* COREdevices Context */
  struct devicesparams *devicesparams ;

  /* COREfileio Context */
  struct fileioparams *fileioparams ;

  /* COREgstate/color Context */
  struct COLOR_SYSTEM_PARAMS *color_systemparams ;
  struct COLOR_USER_PARAMS *color_userparams ;

  /* PDF Parameters */
  struct pdfparams *pdfparams ;

  /* COREimages Contexts */
  struct TIFF6PARAMS *tiff6params ;
  struct WMPPARAMS *wmpparams ;

  /* PDF Context */
  struct PDFCONTEXT *pdfin_h ;

  /* PDFOut Context */
  struct PDFCONTEXT *pdfout_h ;

  /* COREfonts Context */
  struct FONTSPARAMS *fontsparams ;

  /* XML context */
  struct XMLPARAMS *xmlparams ;

  /* XPS Context */
  struct XPSPARAMS *xpsparams ;

  /* Trapping context */
  struct TRAP_SYSTEM_PARAMS * trapping_systemparams ;

  /* Generic parameter control for modules */
  /*@dependent@*/
  struct module_params_t *userparamlist, *systemparamlist, *xmlparamlist ;
} corecontext_t ;

/** Please access fields in the current context using "."
   directly. Fields should be named as CoreContext.fieldname (most
   will be pointers, so Context.fieldname->subfield will be more
   common).
*/
#define CoreContext (*(get_core_context()))

/** Accessor function for per-thread core context. We use an accessor
    function so that we don't pollute every core compound with needing
    to implement variant 'parallel'. It also allows per-file
    localisation of the context, by redefining the \c CoreContext
    macro to refer to a local variable name. */
/*@notnull@*/ /*@dependent@*/
extern corecontext_t *get_core_context(void) ;

/** Accessor function for interpreter thread core context. */
corecontext_t *get_core_context_interp(void);

/** Setter function for per-thread core context. This should only be used by
    the thread initialisation code.

    \param context  The context address that will be set for this thread.
*/
void set_core_context(/*@notnull@*/ corecontext_t *context) ;

/** Clear per-thread core context.
 *
 * This should only be used for threads that have transitory core contexts
 * when they are no longer needed.
 */
void clear_core_context(void);

/** \todo ajcd 2008-12-18: If we introduce an interpreter context pointer to
    the corecontext_t type, then we could test if it's non-NULL here
    instead. */
#define IS_INTERPRETER() (CoreContext.is_interpreter)

/** Opaque data for thread specialisers. This is passed to
    context_specialise_next() to perform the next thread localisation, or
    to call the worker function. */
typedef struct context_specialise_private context_specialise_private ;

/** CoreContext specialiser function for multi-threaded RIPs. On
    initialisation, modules can add a specialiser function to a global list.
    All new threads will call through the specialiser function when running
    the new thread's worker function, keeping its stack frame active whilst
    the thread is alive. The specialiser function can specialise parts of the
    new thread's context by copying the previous contents to a stack-local
    variable, and resetting the context's pointer to point at the stack
    variable. Specialiser functions should not be used for task-specific
    localisation; that can be done through a task specialiser function. They
    should be used by modules that need to specialise parts of the core
    context for *every* possible task. The last action a context specialiser
    should take is to call \c context_specialise_next(), passing the context
    and data pointers it received in its own arguments to the next
    specialiser.

    \param context  The new thread context. This is initialised to a copy of
                    the parent thread's context. Specialisers will usually make
                    copies of context sub-structures and replace the pointer
                    in the context with a pointer to the copy.
    \param data     An opaque reference that must be passed to the
                    \c context_specialise_next() call.
 */
typedef void (context_specialiser_fn)(corecontext_t *context,
                                      context_specialise_private *data) ;

/** Call the next specialiser function, or the task worker function if there
    are no more thread specialisers. CoreContext specialisers are all called
    before the task specialiser, and finally the thread's worker function.
    All specialisers should call \c context_specialise_next() as their last
    actions.

    \param context  The new thread context. This will be passed to each context
                    and task specialiser.
    \param data     An opaque reference that will be passed to the next
                    context specialiser.
*/
void context_specialise_next(corecontext_t *context,
                             context_specialise_private *data) ;

/** \brief CoreContext specialiser element for multi-threaded RIPs.

    Modules that add context spacialisation should statically allocate one of
    these structures, set the "next" pointer to NULL, and call
    context_specialise_add() during module initialisation. */
typedef struct context_specialiser_t {
  context_specialiser_fn *fn ;
  struct context_specialiser_t *next ;
} context_specialiser_t ;

/** \brief Register a thread specialiser function.

    Any module that needs to specialise part of the core context can add a
    specialiser function. The specialiser function should be registered once
    during module initialisation, (the core SwInit() or SwStart() phase
    initialiser routine) using a statically allocated \c context_specialiser_t
    structure.

    \param element  Pointer to a statically-allocated \c context_specialiser_t
                    structure. The \c context_specialiser_t::fn field should be
                    set to the specialisation function, the \c
                    context_specialiser_t::next field should be set to NULL
                    before calling this function.

    Context specialisers will be called when new worker pool threads are
    spawned by the RIP. An assertion will be raised if this function is called
    after the first worker pool thread is spawned.
*/
void context_specialise_add(context_specialiser_t *element) ;

/** \brief Forward reference to the PostScript interpreter context. */
typedef struct ps_context_t ps_context_t ;

/** \brief Opaque accessor to get the core context from the PostScript
    interpreter context. This accessor is here, rather than in the PostScript
    context header because PostScript gets its fingers into many unrelated
    compounds in order to support operators. Some of these operators don't
    (yet) need the full PostScript context definition. */
/*@notnull@*/
corecontext_t *ps_core_context(/*@notnull@*/ const ps_context_t *pscontext) ;

/** \} */

#endif /* Protection from multiple inclusion */

/*
Log stripped */
