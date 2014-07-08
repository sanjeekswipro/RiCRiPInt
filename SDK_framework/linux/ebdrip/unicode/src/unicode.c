/** \file
 * \ingroup unicode
 *
 * $HopeName: HQNc-unicode!src:unicode.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Unicode initialisation.
 *
 * Glue to initialise and terminate HQNc-unicode compound.
 */

#include "std.h" /* For HQASSERT, HQFAIL */
#include <stdlib.h> /* malloc, free, realloc */

#include "hqunicode.h"
#include "uprivate.h"

#ifdef HQNlibicu
#include "unicode/uclean.h"
#include "unicode/putil.h"

#if defined(ICU_DATA_BUILTIN)
#include "unicode/udata.h"

extern const char icu_data[] ;
#endif
#endif

/* Memory allocation functions */
static void *default_alloc_fn(const void *context, size_t size)
{
  UNUSED_PARAM(const void *, context) ;
  return malloc(size) ;
}

static void *default_realloc_fn(const void *context, void *mem, size_t size)
{
  UNUSED_PARAM(const void *, context) ;
  return realloc(mem, size) ;
}

static void default_free_fn(const void *context, void *mem)
{
  UNUSED_PARAM(const void *, context) ;
  free(mem) ;
}

unicode_memory_t unicode_mem_handle = {
  NULL,
  default_alloc_fn,
  default_realloc_fn,
  default_free_fn
} ;

static HqBool initialised_unicode = FALSE ;

#ifdef HQNlibicu
static HqBool initialised_icu = FALSE ;

HqBool unicode_icu_ready(void)
{
  if ( !initialised_icu ) {
    UErrorCode status = U_ZERO_ERROR ;

    u_init(&status) ;

    if ( U_FAILURE(status) )
      return FALSE ;

    initialised_icu = TRUE ;
  }

  return TRUE ;
}
#endif

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
int debug_unicode = 0 ; /* Debug variable for tracing */
#endif

/* Placed here because I want to change ICU as little as
   possible. --johnk */
extern void init_C_globals_icu_filestrm(void) ;

HqBool unicode_init(const char *prefix,
                    unicode_fileio_t *file_handler,
                    unicode_memory_t *mem_handler)
{
  if ( !initialised_unicode ) {
#ifdef HQNlibicu
    UErrorCode status = U_ZERO_ERROR ;

    init_C_globals_icu_filestrm() ;

    if ( mem_handler ) {
      u_setMemoryFunctions(mem_handler->context,
                           mem_handler->alloc_fn,   /* UMemAllocFn */
                           mem_handler->realloc_fn, /* UMemReAllocFn */
                           mem_handler->free_fn,    /* UMemFreeFn */
                           &status) ;

      if ( U_FAILURE(status) )
        return FALSE ;

      unicode_mem_handle = *mem_handler ;
    }

    if ( prefix )
      u_setDataDirectory(prefix) ;

    if ( file_handler ) {
      u_setFileFunctions(file_handler->context,
                         file_handler->open_fn,
                         file_handler->close_fn,
                         file_handler->read_fn,
                         file_handler->write_fn,
                         file_handler->rewind_fn,
                         file_handler->extent_fn,
                         file_handler->eof_fn,
                         file_handler->remove_fn,
                         file_handler->error_fn,
                         &status) ;
    }

#if defined(ICU_DATA_BUILTIN)
    udata_setCommonData(icu_data, &status) ;
#endif

    if ( U_FAILURE(status) )
      return FALSE ;
#else /* !HQNlibicu */
    UNUSED_PARAM(const char *, prefix) ;
    UNUSED_PARAM(unicode_fileio_t *, file_handler) ;

    if ( mem_handler ) {
      unicode_mem_handle = *mem_handler ;
    }
#endif /* !HQNlibicu */

    initialised_unicode = TRUE ;
  } else {
    HQFAIL("Already initialised HQNc-unicode") ;
  }

  return TRUE ;
}

void unicode_finish(void)
{
  if ( initialised_unicode ) {
#ifdef HQNlibicu
    u_cleanup() ;
    if ( initialised_icu ) {
      unicode_mem_handle.alloc_fn = default_alloc_fn ;
      unicode_mem_handle.realloc_fn = default_realloc_fn ;
      unicode_mem_handle.free_fn = default_free_fn ;
      unicode_mem_handle.context = NULL ;
    }
    initialised_icu = FALSE ;
#endif
    initialised_unicode = FALSE ;
  }
}

/*
Log stripped */
