/** \file
 * \ingroup PFIN
 *
 * $HopeName: COREfonts!src:pfin.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file provides the Pluggable Font Interface handler.
 */

#include "core.h"
#include "coreinit.h"
#include "timing.h"
#include "objnamer.h"
#include "swerrors.h"
#include "hqunicode.h"  /* utf8_validate */
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "objects.h"
#include "objstack.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "lowmem.h"
#include "gcscan.h"
#include "tables.h"
#include "namedef_.h"
#include "stacks.h"

#include "graphics.h"
#include "system.h"     /* path_free_list */
#include "control.h"
#include "dicthash.h"
#include "gstack.h"
#include "formOps.h"    /* make_form() */
#include "rlecache.h"   /* form_to_rle() */
#include "gu_ctm.h"     /* gs_setctm() */
#include "swmemory.h"   /* gs_cleargstates() */

#include "fonts.h"      /* font types */
#include "cidfont.h"    /* cidfont types */
#include "fontcache.h"
#include "fcache.h"
#include "uniqueid.h"   /* UniqueID ranges */

#include "paths.h"      /* PATHINFO */
#include "gu_path.h"    /* path_*() */

#include "swdataapi.h"
#include "swdataimpl.h"
#include "swdevice.h"   /* SwLengthPatternMatch() */

#include "swmemapi.h"
#include "track_swmemapi.h"
#include "swblobfactory.h"
#include "blobdata.h"

#include "swpfinapi.h"
#include "pfin.h"

/** \defgroup PFIN Pluggable Font Interface
    \ingroup fonts
    \{ */

int32 pfin_cycle = 1 ;

/* ========================================================================== */

/** Internal sw_pfin_result value used for low memory flow control */
#define RETRY (SW_PFIN_SUCCESS - 1)

/** States for \c sw_pfin::state */
enum {
  PFIN_BROKEN, PFIN_INACTIVE, PFIN_SUSPENDED, PFIN_ACTIVE,
} ;

/** Internal structure to link together PFIN modules and singleton
    instances. */
struct sw_pfin {
  struct sw_pfin* next ;  /**< pointer to the next sw_pfin */
  struct sw_pfin* base ;  /**< pointer to original instantiation, or 0 */
  sw_pfin_api *impl ;     /**< pointer to the module API supplied during registration */
  sw_pfin_instance *instance ; /**< pointer to the singleton instance. */
  /* the following are used to monitor the module */
  sw_memory_instance *mem ; /**< Tracking memory instance. */
  int          state ;    /**< module status */
  int          threaded ; /**< thread (active call use) count */
  unsigned int namelen ;  /**< length of the name of this module */
  Bool         pass_on_error ; /**< fonts reported as invalid by this module should be passed to other modules */
  Bool         pass_on_unsupported ; /**< fonts reported as unsupported by this module should be passed to other modules */
  const char name[1] ;    /**< Name of this instance */
} ;

/* Macro to calculate the length of a sw_pfin */
#define SW_PFIN_LENGTH(_l) (offsetof(sw_pfin, name) + (_l))

/** Memory pool that sw_pfin entries are taken from. */
#define pfin_pool mm_pool_temp

/* -------------------------------------------------------------------------- */
/* linked list of PFIN module instantiations */

/* Overall state of the PFIN module */
enum { PFIN_NOT_INITIALISED, PFIN_PREBOOT, PFIN_STARTED } ;

static int pfin_state = PFIN_NOT_INITIALISED ;
static int pfin_ignore = 1 ;

static sw_pfin* pfin_module_list = NULL ;

static sw_memory_instance *pfin_class_memory = NULL ;

#define pfin_alloc(n_) \
  HQASSERT_EXPR(pfin_class_memory != NULL && \
                pfin_class_memory->implementation != NULL && \
                pfin_class_memory->implementation->alloc != NULL, \
                "PFIN class memory not initialised properly", \
                (pfin_class_memory->implementation->alloc(pfin_class_memory, (n_))))

#define pfin_free(p_) MACRO_START \
  HQASSERT(pfin_class_memory, "PFIN class memory not initialised") ; \
  HQASSERT(pfin_class_memory->implementation, \
           "PFIN class memory implementation missing") ; \
  HQASSERT(pfin_class_memory->implementation->free, \
           "PFIN class memory implementation free function missing") ; \
  pfin_class_memory->implementation->free(pfin_class_memory, (p_)) ; \
MACRO_END

#define pfin_data_api sw_data_api_virtual

/* -------------------------------------------------------------------------- */
/** Context for font definition and undefinition. */
struct sw_pfin_define_context {
  uint32 cookie ;  /**< Paranoia cookie. */
  sw_pfin *pfin ;  /**< The PFIN context (un)defining this font. */
  Bool defined ;   /**< Did callback define any fonts? */
  Bool prefix ;    /**< Whether names should be prefixed by the module name */
  int32 uid ;      /**< The UniqueID to be used by this font */
} ;

#define PFIN_DEFINE_COOKIE (12345678 + SW_PFIN_API_VERSION)

/* -------------------------------------------------------------------------- */
/** The PFIN 'outline' context - now a misnomer as it applies to bitimage too */
struct sw_pfin_outline_context {
  sw_pfin *pfin ;     /**< The PFIN context initiating this outline */
  sw_pfin_font *font ;/**< The font information */
  charcontext_t *chr ;/**< Current charcontext */
  FORM *form ;        /**< FORM pointer if allocated and filled in bitimage() */
  double *metrics ;   /**< The metrics returned by the module */
  PATHINFO path ;     /**< The path being created */
  Bool open ;         /**< The path is open */
  Bool marked ;       /**< The path contains some kind of mark */
  int32 state ;       /**< Outline/bitmap preference/delivery status */
  int32 options ;     /**< Optional flags */
  uint32 cookie ;     /**< Paranoia cookie */
  int32 gid ;         /**< Glyph ID from plotchar */
  double x ;          /**< X coordinate of the current point */
  double y ;          /**< Y coordinate of the current point */
  corecontext_t *corecontext; /**< Thread's core context. */
} ;

#define PFIN_OUTLINE_COOKIE (81234567 + SW_PFIN_API_VERSION)

/* -------------------------------------------------------------------------- */
/* \brief  Check that a sw_datum is an array, and is all of one type

   \param[in] array  The datum to check.

   \param[in] type   The SW_DATUM_TYPE_ that the array must comprise.

   \retval  TRUE if the datum is an array containing only entries of the
   given type. FALSE otherwise.
 */

static Bool pfin_array_is_all(/*@in@*/ /*@notnull@*/ const sw_datum* array,
                              unsigned int type)
{
  unsigned int i ;

  HQASSERT(array != NULL, "No array to check") ;
  HQASSERT(array->type == SW_DATUM_TYPE_ARRAY, "Not an array") ;

  for (i = 0 ; i < array->length ; i++) {
    sw_datum value ;

    if ( pfin_data_api.get_indexed(array, i, &value) != SW_DATA_OK ||
         value.type != type)
      return FAILURE(FALSE) ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* The PFIN api, and forward declarations */

static sw_pfin_result pfin_thread(sw_pfin* pfin) ;
static void pfin_unthread(sw_pfin* pfin) ;

static sw_pfin_result RIPCALL pfin_define(sw_pfin_define_context *context,
                                          const sw_datum* fontname,
                                          const sw_datum* id,
                                          sw_datum* encoding) ;
static sw_pfin_result RIPCALL pfin_undefine(sw_pfin_define_context *context,
                                            sw_datum* fontname) ;
static sw_pfin_result RIPCALL pfin_move(sw_pfin_outline_context *context,
                                        double x, double y) ;
static sw_pfin_result RIPCALL pfin_line(sw_pfin_outline_context *context,
                                        double x, double y) ;
static sw_pfin_result RIPCALL pfin_curve(sw_pfin_outline_context *context,
                                         double x0, double y0,
                                         double x1, double y1,
                                         double x2, double y2) ;
static sw_pfin_result RIPCALL pfin_bitimage(sw_pfin_outline_context *context,
                                            int width,   int height,
                                            int xoffset, int yoffset,
                                            double xdpi, double ydpi,
                                            unsigned char * buffer,
                                            size_t length);
static int RIPCALL pfin_options(sw_pfin_outline_context *context,
                                int mask, int toggle);
static sw_pfin_result RIPCALL pfin_uid(sw_pfin_define_context *context,
                                       int *puid) ;
static sw_pfin_result RIPCALL pfin_flush(int32 uniqueid,
                                         const sw_datum *glyphname) ;

/* There's only one of these, which all sw_pfin contexts will point to */

static const sw_pfin_callback_api pfin_callback_impl = {
  /* SW_PFIN_API_VERSION_20071111 */
  pfin_define,
  pfin_undefine,
  pfin_move,
  pfin_line,
  pfin_curve,
  /* SW_PFIN_API_VERSION_20080808 */
  pfin_bitimage,
  pfin_options,
  /* SW_PFIN_API_VERSION_20090305 */
  pfin_uid,
  /* SW_PFIN_API_VERSION_20090401 */
  pfin_flush
} ;

/* ========================================================================== */
/** \brief  Convert a PFIN return code into a PS error code.

    \param[in] error  PFIN return code.

    \return  Error code to be passed to errorhandler().
 */

int32 pfin_error(sw_pfin_result error)
{
  switch (error) {
  case SW_PFIN_ERROR_MEMORY:
    return VMERROR ;

  case SW_PFIN_ERROR_UNSUPPORTED:
    return TYPECHECK ;

  case SW_PFIN_ERROR_INVALID:
    return INVALIDFONT ;

  case SW_PFIN_ERROR_SYNTAX:
    return SYNTAXERROR ;

  case SW_PFIN_ERROR_UNKNOWN:
    return UNDEFINED ;

  case SW_PFIN_ERROR:
    return LIMITCHECK ;
  }

  /* We shouldn't be called with any other error (or success) so complain */
  HQFAIL("Cannot convert PFIN return code to PS error") ;
  return IOERROR ;
}

/* ========================================================================== */
static void pfin_stop(sw_pfin* pfin, sw_pfin_reason reason) ;

/** \brief  Try to recover memory from unthreaded PFIN modules.

    \param[in] ignore  A PFIN context to ignore when trying to free memory.

    If pfin_austerity() is being called from within an attempt to access a
    module, then suspending it will immediately result in it being resumed
    again if suspension had caused memory to be freed. This would result in an
    infinite loop if there still wasn't enough memory - the same module would
    be suspended again, and so on.

    \retval  TRUE if it is worth retrying the operation that failed due to
    lack of memory.

    If a PFIN module fails with SW_PFIN_ERROR_MEMORY, PFIN will retry if this
    routine returns TRUE.
 */
Bool pfin_austerity(sw_pfin *ignore)
{
  sw_pfin *pfin ;

  /* Go through the module list to find a module to suspend. We always ignore
     threaded and suspended modules, but also ignore the passed-in module to
     avoid an infinite loop. If a suspension results in free() being called,
     the routine immediately returns TRUE.
   */
  for (pfin = pfin_module_list ; pfin ; pfin = pfin->next) {
    if (pfin != ignore && pfin->state == PFIN_ACTIVE && pfin->threaded < 1) {
      /* can only suspend unthreaded active modules */
      (void)track_swmemory_freed(pfin->mem) ; /* Clear the free flag */
      pfin_stop(pfin, SW_PFIN_REASON_SUSPEND) ;
      if (track_swmemory_freed(pfin->mem)) /* Test the free flag */
        return TRUE ;
    } /* if active */
  } /* for pfin */

  /* We were unable to free any memory */
  return FALSE ;
}


/** Solicit method of the PFIN low-memory handler. */
static low_mem_offer_t *pfin_lowmem_solicit(low_mem_handler_t *handler,
                                            corecontext_t *context,
                                            size_t count,
                                            memory_requirement_t* requests)
{
  static low_mem_offer_t offer;
  sw_pfin *pfin;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  /* nothing to assert about count */
  HQASSERT(requests != NULL, "No requests");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !context->is_interpreter )
    /* PFIN is not thread-safe, but only the interpreter thread uses it. */
    return NULL;
  for ( pfin = pfin_module_list ; pfin != NULL ; pfin = pfin->next )
    if ( pfin->state == PFIN_ACTIVE && pfin->threaded < 1 )
      /* can only suspend unthreaded active modules */
      break;
  if ( pfin == NULL )
    return NULL; /* no suitable modules found */
  offer.pool = pfin_pool;
  offer.offer_size = 100; /* no way to know, but not likely to be much */
  offer.offer_cost = 1.0; /* no way to know, but probably just reread */
  offer.next = NULL;
  return &offer;
}


/** Release method of the PFIN low-memory handler. */
static Bool pfin_lowmem_release(low_mem_handler_t *handler,
                                corecontext_t *context, low_mem_offer_t *offer)
{
  sw_pfin *pfin;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);
  UNUSED_PARAM(low_mem_offer_t*, offer);

  /* The state of the modules might have changed, so just search the
     list again. */
  for ( pfin = pfin_module_list ; pfin != NULL ; pfin = pfin->next )
    if ( pfin->state == PFIN_ACTIVE && pfin->threaded < 1 ) {
      /* can only suspend unthreaded active modules */
      (void)track_swmemory_freed(pfin->mem); /* Clear the free flag */
      pfin_stop(pfin, SW_PFIN_REASON_SUSPEND);
      if ( track_swmemory_freed(pfin->mem) ) /* Test the free flag */
        return TRUE;
    }
  return TRUE;
}


/** The PFIN low-memory handler. */
static low_mem_handler_t pfin_lowmem_handler = {
  "PFIN module stop",
  memory_tier_disk, pfin_lowmem_solicit, pfin_lowmem_release, TRUE,
  0, FALSE };


/* ========================================================================== */
/* Exported definition of the font methods for VM-based PFIN fonts */

static Bool pfin_lookup_cid_char(FONTinfo *fontInfo, charcontext_t *context)
{
  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context") ;

  /** \todo PFIN CID lookup */

  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(oType(context->glyphname) == OINTEGER, "Glyph not indexed by CID") ;

  context->definition = context->glyphname ;
  context->chartype = CHAR_PFIN ;

  return TRUE ;
}

static Bool pfin_lookup_char(FONTinfo *fontInfo, charcontext_t *context)
{
  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context") ;

  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  context->definition = context->glyphname ;
  context->chartype = CHAR_PFIN ;

  return TRUE ;
}

/* our charstring_methods_t for PFIN-owned fonts */
struct charstring_methods_t {
  FONTinfo* font ;
  OBJECT*   module ;
  int       modules ;
} ;

static Bool pfin_begin_char(FONTinfo *fontInfo, charcontext_t *context)
{
  charstring_methods_t* methods ;
  OBJECT* pfin, * module ;
  int modules ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No char context") ;

  /* get PFIN name or array */
  pfin = fast_extract_hash_name(&fontInfo->thefont, NAME_PFIN) ;
  if (!pfin)
    return error_handler(INVALIDFONT) ;

  switch (oType(*pfin)) {
  case ONAME:
  case OSTRING:
    modules = 1 ;
    module = pfin ;
    break ;
  case OARRAY:
    modules = theLen(*pfin) ;
    module = oArray(*pfin) ;
    break ;
  default:
    return error_handler(INVALIDFONT) ;
  }

  /* store FONTinfo for later use */
  if ( (methods = pfin_alloc(sizeof(charstring_methods_t))) == NULL )
    return error_handler(VMERROR) ;

  methods->font = fontInfo ;
  methods->module = module ;
  methods->modules = modules ;

  context->methods = methods ;

  return TRUE ;
}

static void pfin_end_char(FONTinfo *fontInfo, charcontext_t *context)
{
  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context object") ;

  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  /* discard our copy of FONTinfo */
  if (context->methods) {
    pfin_free(context->methods) ;
    context->methods = NULL ;
  }

  object_store_null(&context->definition) ;
}

font_methods_t font_pfin_fns = {
  fontcache_base_key,
  pfin_lookup_char,
  NULL, /* No subfont lookup */
  pfin_begin_char,
  pfin_end_char
} ;

/* Exported definition of the font methods for PFIN CID fonts */
font_methods_t font_pfin_cid_fns = {
  fontcache_cid_key,
  pfin_lookup_cid_char,
  NULL, /* No subfont lookup */
  pfin_begin_char,
  pfin_end_char
} ;

/* ========================================================================== */
/** \brief Enumeration of state of the pfin_cache() state machine, and some
    additional states used by the sw_pfin_outline_context structure.
*/
enum {
  PFIN_CACHE_FIND,                /*<< Finding an appropriate PFIN module */
  PFIN_CACHE_METRICS,             /*<< Getting glyph metrics */
  PFIN_CACHE_OUTLINE,             /*<< Getting glyph definition */
  PFIN_CACHE_DONE,                /*<< finished */
  /* Further states for the outline context: */
  PFIN_CACHE_EMPTY_OUTLINE,       /*<< No glyph, but need an outline */
  PFIN_CACHE_EMPTY,               /*<< No glyph specified yet */
  PFIN_CACHE_BITIMAGE,            /*<< Finished bitimage provided */
  PFIN_CACHE_BITIMAGE_PARTIAL     /*<< Partial bitimage provided */
} ;

/* -------------------------------------------------------------------------- */
/** \brief Cache a glyph from a PFIN-controlled font.
 *
 * The appropriate PFIN module will be woken if necessary, asked for the
 * glyph metrics which may result in notdef being selected or the module
 * rejecting the font and another module being selected instead, and finally
 * the outline is fetched and rasterised.
 */

Bool pfin_cache(corecontext_t *context,
                charcontext_t *charcontext, LINELIST *currpt, int32 gid)
{
  charstring_methods_t *methods ;
  sw_pfin_font  font ;
  sw_datum      glyph ;
  sw_pfin       *pfin = NULL ;
  sw_pfin_result result ;

  int           state = PFIN_CACHE_FIND ;
  double        metrics[10] ;  /* wx,wy,x0,y0,x1,y1 */
  SYSTEMVALUE   xo = 0, yo = 0 ;
  CHARCACHE     *cptr = NULL ;
  sbbox_t       bbox ;
  int32         mflags = MF_W0 ; /* | MF_LL | MF_UR ;*/

  static double dx = 0, dy = 0, lastx = 0, lasty = 0 ;
  OMATRIX       *m ;
  double        x, y ;
  sw_pfin_outline_context outline = { 0 } ;
  DL_STATE *page = context->page ;

  /* [63514] There may be no currentpoint when doing a stringwidth */
  if (charcontext->modtype != DOSTRINGWIDTH) {
    HQASSERT( currpt != NULL, "No currentpoint in pfin_cache" ) ;
    outline.x  = theX(theIPoint(currpt)) ;
    outline.y  = theY(theIPoint(currpt)) ;
  } else {
    outline.x  = 0 ;
    outline.y  = 0 ;
  }

  outline.corecontext = context ;
  outline.font = &font ;
  outline.chr  = charcontext ;
  outline.gid  = gid ;

  /* calculate pixel size */
  methods = charcontext->methods ;
  m = &theFontMatrix(*methods->font) ;

  x = m->matrix[0][0] * m->matrix[0][0] + m->matrix[0][1] * m->matrix[0][1] ;
  y = m->matrix[1][0] * m->matrix[1][0] + m->matrix[1][1] * m->matrix[1][1] ;
  if (x != lastx || y != lasty) {
    lastx = x ;  x = sqrt(x) ;  dx = (x == 0.0) ? 0.0 : 1.0 / x ;
    lasty = y ;  y = sqrt(y) ;  dy = (y == 0.0) ? 0.0 : 1.0 / y ;
  }
  font.dx = dx ;
  font.dy = dy ;

  /* The font matrix (20090305) */
  font.matrix = (double *) m->matrix ;

  /* Get device resolution (20080808) */
  font.xdpi = page->xdpi ;
  font.ydpi = page->ydpi ;

  /* Set glyph preference (20080808) - note arbitrary 40pt theshold */
  font.prefer = (charcontext->modtype == DOCHARPATH) ? PFIN_OUTLINE_REQUIRED :
    (dy * page->ydpi <= 72.0/40.0) ? PFIN_OUTLINE_PREFERRED : PFIN_BITIMAGE_PREFERRED ;

  /* turn the font dictionary and glyph selector into datums */
  if (swdatum_from_object(&font.font, &methods->font->thefont) != SW_DATA_OK ||
      swdatum_from_object(&glyph, &charcontext->definition) != SW_DATA_OK)
    return error_handler(INVALIDFONT) ;

  while (state < PFIN_CACHE_DONE) { /* loop until total success */
    if (state == PFIN_CACHE_FIND) {
      /* need to find the first/next module to try */
      /* charcontext->methods contains the FONTinfo and the name of the
       * (first) module to try - so find a suitable module */
      pfin = NULL ;
      while (pfin == NULL && methods->modules > 0) {
        pfin = pfin_findpfin(methods->module) ;
        if (!pfin) {
          --methods->modules ;
          ++methods->module ;
        }
      }
      /* failed to find a module? */
      if (methods->modules == 0)
        return error_handler(INVALIDFONT) ;

      HQASSERT(pfin->impl->metrics, "Module can't supply metrics?") ;
      state = PFIN_CACHE_METRICS ;  /* next get the metrics */

      outline.pfin = pfin ;
    }
    /* can only be at state 1 or 2 here */
    HQASSERT( state == PFIN_CACHE_METRICS ||
              state == PFIN_CACHE_OUTLINE, "Unexpected state") ;

    /* call the module to get the metrics or outline */
    if ((result = pfin_thread(pfin)) == SW_PFIN_SUCCESS) {
      if (state == PFIN_CACHE_METRICS) {
        /* Request metrics from the module */
        PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
          result = pfin->impl->metrics(pfin->instance, &font, &glyph, metrics));
      } else {
        /* Ask the module to make an outline or bitimage */
        outline.cookie  = PFIN_OUTLINE_COOKIE ;
        path_init(&outline.path) ;
        outline.open    = FALSE ;
        outline.marked  = FALSE ;
        outline.options = 0 ;
        outline.metrics = metrics ;
        outline.state   = (charcontext->modtype == DOCHARPATH) ?
                          PFIN_CACHE_EMPTY_OUTLINE : PFIN_CACHE_EMPTY ;
        PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
          result = pfin->impl->outline(pfin->instance, &outline, &font, &glyph));
        outline.cookie = 0 ; /* paranoia */
        /* We now have the glyph - to some extent. The module will have created
           a path, or may have called bitimage. If it was a bitimaged call, we
           may have dealt with it already. Equally, if the result was an error,
           there may be a FORM to discard. */
      }
      pfin_unthread(pfin) ;
    }

    if (result && outline.form) {
      /* If there was an error after the FORM was created, destroy it. */
      destroy_Form(outline.form) ;
      outline.form = 0 ;
    }

    switch (result) {
    case SW_PFIN_SUCCESS:
      /* we now have the width and bounding box, so can we bail out early? */
      if (state == PFIN_CACHE_METRICS) {
        COMPUTE_STRINGWIDTH(metrics, charcontext) ;
        if (charcontext->modtype == DOSTRINGWIDTH)
          return TRUE ;
      }
      ++state ;
      break ;
    case SW_PFIN_ERROR_UNKNOWN:
      /* unrecognised character, so switch to notdef, if not already! */
      if (glyph.type == SW_DATUM_TYPE_INTEGER && glyph.value.integer == 0)
        return error_handler(INVALIDFONT) ;
      else {
        sw_datum notdef = SW_DATUM_INTEGER(0) ;
        glyph = notdef ;
      }
      /* stay at current state - retry */
      break ;
    case SW_PFIN_ERROR_MEMORY:
      /* insufficient memory, free some and try again */
      if (!pfin_austerity(pfin))
        return error_handler(VMERROR) ;
      /* stay at current state - retry */
      break ;
    case SW_PFIN_ERROR_UNSUPPORTED:
      /* this module can't support the font, so pass to the next if possible */
      if (!pfin->pass_on_unsupported)
        return error_handler(INVALIDFONT) ;
      state = PFIN_CACHE_FIND ;
      break ;
    case SW_PFIN_ERROR_INVALID:
      /* the module thinks the font is broken */
      if (!pfin->pass_on_error)
        return error_handler(INVALIDFONT) ;
      state = PFIN_CACHE_FIND ;
      break ;
    default:
      return error_handler(pfin_error(result)) ;
    } /* switch result */
  } /* while state<PFIN_CACHE_DONE */

  result = TRUE ;
  switch (outline.state) {
  default:
    outline.marked = FALSE ;
    outline.open = FALSE ;
    /* drop through into PFIN_CACHE_OUTLINE */

  case PFIN_CACHE_OUTLINE:
    /* Outlined or empty glyph */

    /* ensure the path is closed */
    if (outline.open && (outline.options & PFIN_OPTION_OPEN) == 0 &&
        !path_close(CLOSEPATH, &outline.path))
      return FALSE ;

    /* we now have the path */
    if (outline.marked)
      (void)path_bbox(&outline.path, &bbox, BBOX_IGNORE_NONE|BBOX_LOAD) ;
    else
      bbox_store(&bbox, 0,0,0,0) ;

    if (charcontext->modtype != DOCHARPATH) {
      /* set up a cache */
      SYSTEMVALUE bearings[4] ;
      SYSTEMVALUE *bbindexed ;

      bbox_as_indexed(bbindexed, &bbox) ;
      char_bearings(charcontext, bearings, bbindexed,
                    &theFontMatrix(*methods->font)) ;
      xo = bearings[0] ;
      yo = bearings[1] ;

      if ( (outline.options & PFIN_OPTION_DYNAMIC) == 0 )
        cptr = char_cache(charcontext, metrics, mflags, bearings, !outline.marked) ;
    }

    /* draw (or cache) it... */
    result = char_draw(charcontext, currpt, cptr, metrics, mflags,
                       xo, yo, !outline.marked, FALSE, &outline.path,
                       &theFontMatrix(*methods->font)) ;

    /* discard the path */
    path_free_list(outline.path.firstpath, mm_pool_temp) ;
    break ;

  case PFIN_CACHE_BITIMAGE:
    /* context->form is a fontcache form ready to be added to the DL */

    /** \todo NYI */
    break ;

  case PFIN_CACHE_BITIMAGE_PARTIAL:
    /* bitimaged glyph already delivered through imagemask */
    break ;
  }

  return result ;
}

/* ========================================================================== */
/* PFIN path building API */

static sw_pfin_result RIPCALL pfin_move(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                        double x, double y)
{
  if ( context == NULL || context->cookie != PFIN_OUTLINE_COOKIE ||
       context->state > PFIN_CACHE_EMPTY)
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  /* add a move, closing previous contour */
  if (context->open && (context->options & PFIN_OPTION_OPEN) == 0 &&
      !path_close(CLOSEPATH, &context->path))
    return SW_PFIN_ERROR ;

  if (!path_moveto(x, y, MOVETO, &context->path))
    return SW_PFIN_ERROR ;

  context->open  = FALSE ;
  context->state = PFIN_CACHE_OUTLINE ;

  return SW_PFIN_SUCCESS ;
}

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static sw_pfin_result RIPCALL pfin_line(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                        double x, double y)
{
  if ( context == NULL || context->cookie != PFIN_OUTLINE_COOKIE ||
       context->state > PFIN_CACHE_EMPTY )
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  /* add a line */
  if (!path_segment(x, y, LINETO, TRUE, &context->path))
    return SW_PFIN_ERROR ;

  context->open   = TRUE ;
  context->marked = TRUE ;
  context->state  = PFIN_CACHE_OUTLINE ;

  return SW_PFIN_SUCCESS ;
}

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static sw_pfin_result RIPCALL pfin_curve(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                         double x0, double y0,
                                         double x1, double y1,
                                         double x2, double y2)
{
  SYSTEMVALUE b[6] ;

  if ( context == NULL || context->cookie != PFIN_OUTLINE_COOKIE ||
       context->state > PFIN_CACHE_EMPTY )
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  /* add a bezier */
  b[0] = x0 ;  b[1] = y0 ;
  b[2] = x1 ;  b[3] = y1 ;
  b[4] = x2 ;  b[5] = y2 ;
  if (!path_curveto(b, TRUE, &context->path))
    return SW_PFIN_ERROR ;

  context->open   = TRUE ;
  context->marked = TRUE ;
  context->state  = PFIN_CACHE_OUTLINE ;

  return SW_PFIN_SUCCESS ;
}

/* -------------------------------------------------------------------------- */

/* The contextual synthetic OPERATOR structure for our data source function */

typedef struct {
  OPERATOR op ;             /* The subclassed OPERATOR structure */
  sw_pfin * pfin ;          /* The PFIN context in question */
  unsigned char * buffer ;  /* Ptr to any prefetched data */
  size_t length ;           /* Length of prefetched data */
} pfin_image_op ;

/** \brief  The synthetic operator used as a datasource for an image operator.

    This is called from within imagemask to provide raster data. If we already
    have some data prefetched, we return it - subject to the maximum length of
    a Postscript string. Otherwise we call into the module's raster() method to
    fetch some more.
*/
static Bool pfin_datasource(ps_context_t *pscontext)
{
  pfin_image_op * context = (pfin_image_op *)oOp(errobject) ;
  sw_pfin * pfin ;
  size_t some ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* check the context of the call */
  if (oType(errobject) != OOPERATOR ||
      context->op.opcall != pfin_datasource)
    return error_handler(SYNTAXERROR) ;

  pfin = context->pfin ;

  if (context->length == 0) {
    sw_pfin_result result = RETRY;
    sw_datum data = {SW_DATUM_TYPE_STRING} ;

    /* Call into the module to get some data. If the module doesn't return any
       it won't get called again (for this glyph). Also note that at this stage
       of calling raster() we definitely supply a null string datum - the
       opportunity for direct rasterisation into a fontcache form was offered
       (and evidently refused) earlier during the encompassing bitimage() call.

       We don't need to thread the module, as we're already threaded:
       PFIN->outline()->bitimage()->imagemask
    */

    while (result == RETRY) {
      data.type = SW_DATUM_TYPE_STRING ;
      data.owner = 0 ;
      data.length = 0 ;
      data.value.string = 0 ;

      PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
        result = pfin->impl->raster(pfin->instance, &data));
      if (result == SW_PFIN_ERROR_MEMORY && pfin_austerity(pfin))
        result = RETRY ;
    }
    if (result)
      return error_handler(pfin_error(result)) ;

    context->buffer = (unsigned char *)data.value.string ;
    context->length = (context->buffer) ? data.length : 0 ;
  }

  /* deliver some data, or an empty string if we're finished */
  some = context->length ;
  if (some > MAXPSSTRING)
    some = MAXPSSTRING ;  /* strings are limited in length */

  theLen(snewobj)  = CAST_SIZET_TO_UINT16(some) ;
  oString(snewobj) = context->buffer ;

  context->length -= some ;
  context->buffer += some ;


  if (0) {
    int i, l = theLen(snewobj) ;
    uint8 * c = oString(snewobj) ;
    for (i = 0; i < l; i++)
      c[i] = c[i] | 0xAA;
  }

  return push(&snewobj, &operandstack) ;
}

/* The PFIN bitimage method.

   Issued by the module in response to a request for a glyph, there are three
   possible usages:

     Complete bitimage

       If we're caching and it's compatible with the form, we copy into it,
       changing data format if necessary, and mark the context as containing
       a cached glyph.

     Partial bitimage

       We stuff the data into our datasource context, then...

     No bitimage

       If we're caching, call setcachedevice.
       Issue an image operator, using a synthetic datasource operator which
       will call back into the module.

     Note the the data source operator above will return the partial data first,
     before calling for more. Note also that the module can return more data
     than can be returned to PostScript, so the partial behaviour may be
     repeated.
*/

#define DONT_USE_DIRECT_FORM 1

static sw_pfin_result RIPCALL
pfin_bitimage(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
              int width,   int height,
              int xoffset, int yoffset,
              double xres, double yres,
              /*@in@*/ unsigned char * buffer,
              size_t length)
{
  int x = abs(width), y = abs(height), bytes = (x+7) >> 3 ;
  size_t required = bytes * y ;
  sw_pfin_result result = SW_PFIN_SUCCESS ;
  FORM * form = NULL ;
  sw_pfin * pfin ;
  corecontext_t *corecontext;
  ps_context_t *pscontext;
  DL_STATE *page;
  uint32 orientation_opts;

  /* Error if we're called at the wrong time */
  if ( context == NULL || context->cookie != PFIN_OUTLINE_COOKIE )
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  corecontext = context->corecontext;
  pscontext = corecontext->pscontext ;
  page = corecontext->page ;
  orientation_opts = context->options &
                      (PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE |
                       PFIN_OPTION_BITMAP_ORIENTATION_REVERSE);

  pfin = context->pfin ;

  switch (context->state) {
  case PFIN_CACHE_EMPTY:
    break ;
  case PFIN_CACHE_EMPTY_OUTLINE:
    context->state = PFIN_CACHE_OUTLINE ;
    return SW_PFIN_SUCCESS ;        /* ignore bitimages during charpath */
  default:
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;   /* module programming error */
  }

  /* If all data has not been supplied, then the module needs a raster method */
  if (length < required &&
      (pfin->impl->info.version < SW_PFIN_API_VERSION_20080808 ||
       pfin->impl->raster == 0))
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  /* Defaults */
  if (xres == 0.0)  xres = page->xdpi ;   /* zero means device resolution */
  if (yres == 0.0)  yres = page->ydpi ;
  if (buffer == 0)  length = 0 ;    /* zero length or null buffer mean */
  if (length == 0)  buffer = 0 ;    /*  no data supplied */

  /* Update our context */
  context->state = PFIN_CACHE_BITIMAGE_PARTIAL ;

  /* Spot empty bitimages as a special case */
  if (width == 0 || height == 0) {
    context->state = PFIN_CACHE_OUTLINE ;
    return SW_PFIN_SUCCESS ;
  }

  /* Decide whether to cache, and allocate a form if so. */
  if (
#if DONT_USE_DIRECT_FORM
      FALSE &&  /* form avoidance - always use imagemask for now */
#endif
      (context->options & PFIN_OPTION_DYNAMIC) == 0)
    form = context->form = make_Form(x,y) ;

  /* Check whether the bitimage is directly compatible with a fontcache form,
     because if it is, we may be able to rasterise directly into it, or at least
     copy data into it without taking the imagemask route.

     We should be able to cope with all four data formats (and with bytes not a
     multiple of four), but for now we'll insist on the matching data format.
  */
  if (form && xres == page->xdpi && yres == page->ydpi &&
      width > 0 && height > 0 &&
      form->l == bytes) {
    /* Bitimage is compatible with the fontcache form we're about to use */

    if (length == 0) {
      /* We have no data yet, so ask module to rasterise directly into the form.
         It may not comply and just return some data instead, which is the same
         as supplying that data to this bitimage() call in the first place. */

      sw_datum data = {0} ;        /* nullify all fields */
      sw_pfin_result result = RETRY ;

      while (result == RETRY) {
        /* data is completely reinitialised in case it was written to during a
           raster() call that subsequently failed due to lack of memory. */
        data.type = SW_DATUM_TYPE_STRING ;
        data.owner = 0 ;
        data.length = required ;
        data.value.string = (char *) form->addr ;

        PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
          result = pfin->impl->raster(pfin->instance, &data));

        if (result == SW_PFIN_ERROR_MEMORY && pfin_austerity(pfin))
          result = RETRY ;
      } /* while retry */
      if (result)       /* form will be discarded in outlying call */
        return FAILURE(result) ;

      /* Eventually we may allow a different datum type as a datasource, but
         for now we insist on string */
      if (data.type != SW_DATUM_TYPE_STRING)
        return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

      buffer = (unsigned char *) data.value.string ;
      length = (buffer) ? data.length : 0 ;

      /* if no data returned by raster() either, then it's a space */
      if (length == 0) {
        buffer = 0 ;
        context->state = PFIN_CACHE_OUTLINE ;
      }

      /* otherwise, use this first chunk of data */
    } /* if length==0 */

    if (buffer && length >= required) {
      /* We already have all the data (possibly as a result of calling raster()
         above), so copy it into the form (if not already there) */

      if (buffer != (unsigned char *) form->addr) {
        /* Note if bytes is not a multiple of four, one scanline at a time will
           need to be appropriately copied.
         */
        HqMemCpy(form->addr, buffer, required) ;
      }

      context->state = PFIN_CACHE_BITIMAGE ;
      /* This form will be added to the DL by the outlying call. */
    }
  } /* if form... */

  if (context->state == PFIN_CACHE_BITIMAGE_PARTIAL) {
    /* We need to do an imagemask */
    int32  tempgid = GS_INVALID_GID ;
    pfin_image_op op = {{pfin_datasource, system_names + NAME_image}} ;
    OBJECT dataop    = OBJECT_NOTVM_OPERATOR(NULL) ;  /* would be &op */
    OBJECT dataproc  = OBJECT_NOTVM_PROC(NULL,1) ;    /* would be &dataop */
    OBJECT w    = OBJECT_NOTVM_INTEGER(0) ;
    OBJECT h    = OBJECT_NOTVM_INTEGER(0) ;
    OBJECT m[7] = {OBJECT_NOTVM_ARRAY(0,6),
                   OBJECT_NOTVM_INTEGER(0), /* These are reals, but there's */
                   OBJECT_NOTVM_INTEGER(0), /* no initialiser for real      */
                   OBJECT_NOTVM_INTEGER(0), /* OBJECTs, unlike sw_datums :( */
                   OBJECT_NOTVM_INTEGER(0),
                   OBJECT_NOTVM_INTEGER(0),
                   OBJECT_NOTVM_INTEGER(0)} ;
    OMATRIX mat = {0}; /* to placate the compiler */
    double ax, ay ;

    /* Set our context for the data provider */
    op.pfin          = pfin ;
    op.buffer        = buffer ;
    op.length        = length ;
    oCPointer(dataop) = &op ;
    oArray(dataproc) = &dataop ;

    /* Discard form, because setcachedevice will do that for us */
    if (form) {
      context->form = NULL ;
      destroy_Form(form) ;
    }

    /* Fake BuildChar-like environment */
    context->chr->buildthischar = TRUE ;
    context->chr->buildchar = TRUE ;

    /* bracket_plot */
    result = SW_PFIN_SUCCESS ;
    if (context->gid == GS_INVALID_GID &&
        !bracket_plot(context->chr, &tempgid))
      result = SW_PFIN_ERROR ;

#define return DONT USE RETURN!

    /* The advance width, in case we need to transform it */
    ax = context->metrics[0] ;
    ay = context->metrics[1] ;

    /* CTM manipulation (somewhat influenced by Type32 handling) */
    if (result == SW_PFIN_SUCCESS) {
      FONTinfo    *fontInfo = &theFontInfo(*gstateptr) ;
      MATRIXCACHE *lmatrix = theLookupMatrix(*fontInfo) ; /* preserve these */
      uint8       gotmatrix = gotFontMatrix(*fontInfo) ;  /* across gs_setctm */

      /* Set CTM */
      MATRIX_COPY( &mat, &theFontMatrix(*fontInfo)) ;
      if ( context->chr->modtype == DOSTRINGWIDTH ) {
        mat.matrix[2][0] = 0 ;
        mat.matrix[2][1] = 0 ;
        if ( ! nulldevice_(pscontext))
          result = SW_PFIN_ERROR ;
      } else {
        mat.matrix[2][0] += context->x ;
        mat.matrix[2][1] += context->y ;
      }
      if ((context->options & PFIN_OPTION_TRANSFORMED) != 0) {
        /* Glyph bitimage is pretransformed, so matrix must be axis-aligned */

        /* We must transform the advance width into this coordinate space */
        double px = ax ;
        ax = (ax*mat.matrix[0][0] + ay*mat.matrix[1][0]) * context->font->dx ;
        ay =-(px*mat.matrix[0][1] + ay*mat.matrix[1][1]) * context->font->dy ;

        /* The axis-aligned matrix */
        mat.matrix[0][0] = 1 / context->font->dx ;
        mat.matrix[0][1] = 0 ;
        mat.matrix[1][0] = 0 ;
        mat.matrix[1][1] = -1 / context->font->dy ;
        mat.opt = MATRIX_OPT_0011 ;
      }

      if (result == SW_PFIN_SUCCESS) {
        MATRIX_COPY(&thegsDeviceCTM(*gstateptr), &mat) ;
        if (!gs_setctm(&mat, FALSE)) {
          HQFAIL("gs_setctm should not fail with FALSE argument") ;
          result = SW_PFIN_ERROR ;
        } else {
          /* Restore the font matrices zeroed by gs_setctm */
          gotFontMatrix(*fontInfo) = gotmatrix ;
          theLookupMatrix(*fontInfo) = lmatrix ;
        }
      }
    }

    /* Cache, if the glyph isn't dynamic */
    if (result == SW_PFIN_SUCCESS &&
        (context->options & PFIN_OPTION_DYNAMIC) == 0) {
      /* call setcachedevice, if appropriate */
      double rx = page->xdpi / xres ;
      double ry = page->ydpi / yres ;
      double dx = context->font->dx * rx ;
      double dy = context->font->dy * ry ;
      double x0 = dx * -xoffset ;
      double y0 = dy * -yoffset ;
      double x1 = dx * (x-xoffset) ;
      double y1 = dy * (y-yoffset) ;
      double temp;

      /* cachedevice bbox needs to match orientation */
      switch ( orientation_opts ) {
        case PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE:
          temp = x0;
          x0 = y0;
          y0 = -temp;
          temp = x1;
          x1 = y1;
          y1 = -temp;
          break;

        case PFIN_OPTION_BITMAP_ORIENTATION_REVERSE: /* 'reverse portrait' */
          x0 = -x0;
          y0 = -y0;
          x1 = -x1;
          y1 = -y1;
          break;

        case (PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE
              | PFIN_OPTION_BITMAP_ORIENTATION_REVERSE):
          temp = x0;
          x0 = -y0;
          y0 = temp;
          temp = x1;
          x1 = -y1;
          y1 = temp;
          break;

        default:
          break;

      }

      if (!mark_(pscontext))
        result = SW_PFIN_ERROR ;

      if (result == SW_PFIN_SUCCESS &&
          (!stack_push_real(ax, &operandstack) ||
           !stack_push_real(ay, &operandstack) ||
           !stack_push_real(x0, &operandstack) ||
           !stack_push_real(y0, &operandstack) ||
           !stack_push_real(x1, &operandstack) ||
           !stack_push_real(y1, &operandstack))) {
        (void) cleartomark_(pscontext) ;
        result = SW_PFIN_ERROR ;
      }

#ifdef DEBUG_SHOW_PFIN_CACHE_FORMS
      if ( context->chr->modtype != DOSTRINGWIDTH ) {
        char buffer[1024] ;
        sprintf(buffer, "gsave 0.005 setlinewidth newpath "
                        "%f %f %f %f %f %f 2 copy moveto "
                        "3 copy pop exch lineto 4 copy pop pop lineto "
                        "3 index 1 index lineto closepath stroke "
                        "2 copy moveto 4 copy pop pop lineto "
                        "3 copy pop exch moveto 3 index 1 index lineto stroke "
                        "pop pop pop pop 0 0 moveto rlineto stroke "
                        "grestore",
                        ax, ay, x1, y1, x0, y0);
        run_ps_string(buffer);
      }
#endif

      if (result == SW_PFIN_SUCCESS &&
          (!gs_setcachedevice(&operandstack, FALSE) || !cleartomark_(pscontext)))
        result = SW_PFIN_ERROR ;
    }

    if (result == SW_PFIN_SUCCESS) {
      /* image with callback (uses any supplied data first) */
      int i ;
      USERVALUE temp;

      oInteger(w) = x ;
      oInteger(h) = y ;

      oArray(m[0])   = m+1 ;
      for (i=0; i<4; i++)
        theTags(m[i+1])  = OREAL | LITERAL ;
      oReal(m[1])  = (USERVALUE) (xres / (page->xdpi * context->font->dx)) ;
      oReal(m[2])  = 0 ;
      oReal(m[3])  = 0 ;
      oReal(m[4])  = (USERVALUE) (-yres / (page->ydpi * context->font->dy)) ;
      oInteger(m[5]) = (width < 0) ? x - xoffset : xoffset ;
      oInteger(m[6]) = (height < 0) ? yoffset : y - yoffset ;

      oName(nnewobje) = system_names + NAME_imagemask ;

      /* image matrix to match font orientation */
      switch ( orientation_opts ) {
        case PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE:
          temp = oReal(m[3]);
          oReal(m[3]) = -oReal(m[1]);
          oReal(m[1]) = temp;
          temp = oReal(m[4]);
          oReal(m[4]) = -oReal(m[2]);
          oReal(m[2]) = temp;
          break;

        case PFIN_OPTION_BITMAP_ORIENTATION_REVERSE: /* reverse portrait */
          oReal(m[1]) = -oReal(m[1]);
          oReal(m[2]) = -oReal(m[2]);
          oReal(m[3]) = -oReal(m[3]);
          oReal(m[4]) = -oReal(m[4]);
          break;

        case PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE
            | PFIN_OPTION_BITMAP_ORIENTATION_REVERSE:
          temp = oReal(m[3]);
          oReal(m[3]) = oReal(m[1]);
          oReal(m[1]) = -temp;
          temp = oReal(m[4]);
          oReal(m[4]) = oReal(m[2]);
          oReal(m[2]) = -temp;
          break;

        default:
          break;

      }

      if (result == SW_PFIN_SUCCESS &&
          !interpreter_clean(&nnewobje, &w, &h, &tnewobj, m, &dataproc, NULL)) {
        result = SW_PFIN_ERROR ;
        if (context->chr->cachelevel == CacheLevel_Cached) {
          /** \todo undo any pending setcachedevice */
          context->chr->cachelevel = CacheLevel_Error ;
        }
      }

      /* If it was a CACHEBITMAPTORLE form, we have to do the RLE compression
         now. It's a pity it isn't done as required in do_render_char().
      */
      if (context->chr->cachelevel == CacheLevel_Cached &&
          context->chr->cptr->thebmapForm->type == FORMTYPE_CACHEBITMAPTORLE)
        (void)form_to_rle(context->chr->cptr, 0) ;
    }

    /* Clear up bracketed plot */
    if (tempgid != GS_INVALID_GID &&
        !gs_cleargstates(tempgid, GST_SETCHARDEVICE, NULL))
      result = SW_PFIN_ERROR ;
  }

#undef return

  return result ;
}

/* ========================================================================== */
/* Flush a font or a glyph from the fontcache */

static sw_pfin_result RIPCALL pfin_flush(int32 uniqueid,
                                         const sw_datum *glyphname)
{
  OBJECT obj = OBJECT_NOTVM_NOTHING, *glyph = 0 ;

  if (glyphname) {
    glyph = &obj ;
    if (object_from_swdatum(glyph, glyphname) != SW_DATA_OK)
      return SW_PFIN_ERROR ;
  }

  fontcache_make_useless(uniqueid, glyph) ;

  return SW_PFIN_SUCCESS ;
}

/* ========================================================================== */
/* Set, clear, toggle and read option flags. */

static int RIPCALL pfin_options(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                int mask, int toggle)
{
  int old ;

  if ( context == NULL || context->cookie != PFIN_OUTLINE_COOKIE )
    return FAILURE(0) ;

  old = context->options ;
  context->options = (old & ~mask) ^ toggle ;

  return old ;
}

/* ========================================================================== */
/* Free all remaining allocations attached to a PFIN context. */

static void pfin_free_all(/*@in@*/ /*@notnull@*/ sw_pfin* pfin)
{
  track_swmemory_purge(pfin->mem) ;

  /* The instance comes out of the module's allocator, so make sure it is
     cleared too. */
  pfin->instance = NULL ;
}

/* -------------------------------------------------------------------------- */
/* Close all remaining blobs attached to a PFIN context. */

static void pfin_close_all(/*@in@*/ /*@notnull@*/ sw_pfin* pfin)
{
  UNUSED_PARAM(sw_pfin *, pfin) ;
#if 0
  /** \todo How will PFIN monitor individual blob transactions without an
     opaque blob context (sw_memory_api style) or a global (stack of) PFIN
     contexts? Yuck.

     [ajcd 12th November 2007 - Create a sw_blob_api subclass which
     intercepts the open() and open_named() methods, building a list of
     the successfully opened blobs.]
   */
  int           i ;
  sw_pfin_blob  *a, *n ;

  if (pfin == NULL || pfin->version != SW_PFIN_VERSION_BOGUS) {
    HQFAIL("Not a PFIN context") ;
    return ;
  }
  n = priv->blobs ;
  while (n) {
    a = n ;
    n = a->next ;
    pfin_blob_api.close(&a->blob) ;
    pfin_free(NULL, a) ;
  }
#endif
}

static void pfin_free_resources(sw_pfin* pfin)
{
  pfin_close_all(pfin) ;
  pfin_free_all(pfin) ;
}

/* ========================================================================== */
/** \brief  Start a PFIN module.

    A module returning an error from its start call will be marked as
    broken and won't be stopped or restarted.

    \param[in] pfin  The PFIN context for the module to start.

    \param[in] reason  This should be SW_PFIN_REASON_START or
    SW_PFIN_REASON_RESUME.

    \return SW_PFIN_SUCCESS if the module was started correctly. The module
    will be in the \c PFIN_ACTIVE state in this case. On failure, one of the
    \c sw_pfin_result error codes will be returned.
 */
static sw_pfin_result pfin_start(sw_pfin* pfin, sw_pfin_reason reason)
{
  sw_pfin_result result = SW_PFIN_SUCCESS ;
  Bool start = FALSE ;

  HQASSERT(reason == SW_PFIN_REASON_START ||
           reason == SW_PFIN_REASON_RESUME,
           "Invalid PFIN start reason") ;

  HQASSERT(pfin->threaded == 0, "PFIN module unexpectedly threaded") ;

  switch (pfin->state) {
  case PFIN_INACTIVE:
    start = (reason == SW_PFIN_REASON_START) ;
    HQASSERT(pfin->instance == NULL,
             "PFIN instance exists for inactive module") ;
    break ;
  case PFIN_SUSPENDED:
    start = (reason == SW_PFIN_REASON_RESUME) ;
    HQASSERT(pfin->instance != NULL,
             "PFIN instance doesn't exist for suspended module") ;
    break ;
  }

  if (start) {
    if (pfin->impl->start) {
      sw_pfin_define_context context = {0} ;
      sw_pfin_instance *instance ;

      if ( (instance = pfin->instance) == NULL ) {
        sw_memory_instance *mem = pfin->mem ;

        HQASSERT(mem, "No allocator for PFIN module memory") ;
        HQASSERT(reason == SW_PFIN_REASON_START,
                 "Creating new instance, but not starting module") ;

        /* The allocation for the instance comes out of the PFIN module
           allocations. When the module's allocator is purged, the
           instance will be destroyed as part of that.  */
        instance = mem->implementation->alloc(mem,
                                              pfin->impl->info.instance_size) ;
        if ( instance == NULL )
          return FAILURE(SW_PFIN_ERROR_MEMORY) ;

        /* Initialise callback APIs and data that the RIP is aware of. */
        HqMemZero(instance, pfin->impl->info.instance_size) ;
        instance->implementation = pfin->impl ;
        instance->callbacks = &pfin_callback_impl ;
        instance->mem = mem ;
        /** \todo This API should be a blob factory which contains only an
            tracks uses of its open_named() method. The blob returned by that
            method should monitor the open(), close(), map_open(), and
            map_close() calls. PFIN may even want to instantiate a new blob
            factory for each module so that different font modules can access
            different parts of the file system, or even virtual namespaces
            with font resources.
         */
        instance->blob_factory = &sw_blob_factory_objects ;
        /** \todo The data API should be a filtered data API, which monitors
            the open_blob method, and proxies the returned blob with an
            implementation that monitors open(), close(), open_map() and
            close_map(). This would allow PFIN the complete control it wants
            over resources, so when a PFIN instance is shut down any opened
            blob resources can be recovered. */
        instance->data_api = &pfin_data_api ;

        pfin->instance = instance ;
      }

      context.pfin = pfin ;
      context.prefix = FALSE ;
      context.defined = FALSE ;
      context.cookie = PFIN_DEFINE_COOKIE ;
      context.uid = -1 ;

      ++pfin->threaded ;
      PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
        result = (*pfin->impl->start)(instance, &context, reason));
      --pfin->threaded ;

      context.cookie = 0 ;

      /* it's unlikely that we can do anything about memory usage during
       * initialisation, so don't treat SW_PFIN_ERROR_MEMORY specially. */
      if (result != SW_PFIN_SUCCESS) {
        pfin->state = PFIN_BROKEN ;

        /* Generate warning message? */
        HQFAIL("PFIN module failed to initialise") ;

        /* It is possible that the module started consuming resources before
         * failing, so tidy them up if necessary */
        pfin_free_resources(pfin) ;
      } else {
        /* Successfully called start method */
        pfin->state = PFIN_ACTIVE ;
      }
    } else { /* no start() method, set to active anyway */
      pfin->state = PFIN_ACTIVE ;
    }
  } /* if starting */

  return result ;
}


/* -------------------------------------------------------------------------- */
/** \brief  Initialise all active PFIN modules.

    A module returning an error from its initialise call will be marked as
    inactive and won't be finalised or restarted.

    \param[in] reason  This should be SW_PFIN_REASON_START.

    Support for SW_PFIN_REASON_RESUME may be added later.
 */

static void pfin_start_modules(sw_pfin_reason reason)
{
  sw_pfin *pfin ;
  for (pfin = pfin_module_list ; pfin ; pfin = pfin->next)
    (void) pfin_start(pfin, reason) ;
}

/* ========================================================================== */
/** \brief  Finalise a PFIN module.

    \param[in] pfin  The PFIN context to finalise.

    \param[in] reason  This must be SW_PFIN_REASON_STOP or
    SW_PFIN_REASON_SUSPEND.
 */
static void pfin_stop(sw_pfin* pfin, sw_pfin_reason reason)
{
  Bool   stop = FALSE ;
  int    state ;
  sw_pfin_result result = SW_PFIN_SUCCESS ;

  HQASSERT(pfin, "No PFIN module to stop") ;
  HQASSERT(reason == SW_PFIN_REASON_STOP ||
           reason == SW_PFIN_REASON_SUSPEND,
           "Invalid PFIN stop reason") ;

  if (pfin->threaded > 0) {
    HQFAIL("Can't finalise a threaded module") ;
    return ;
  }

  state = pfin->state ;

  switch (reason) {
  case SW_PFIN_REASON_SUSPEND :
    if (state == PFIN_ACTIVE) {
      stop = TRUE ;
      state = PFIN_SUSPENDED ;
    }
    break ;
  case SW_PFIN_REASON_STOP :
    if (state > PFIN_INACTIVE) {
      stop = TRUE ;
      state = PFIN_INACTIVE ;
    }
    break ;
  }

  if ( stop ) {
    if ( pfin->impl->stop ) {
      sw_pfin_define_context context = {0} ;

      /* stop this module */
      HQASSERT(pfin->instance, "Stopping PFIN module but no instance") ;

      context.pfin = pfin ;
      context.prefix = FALSE ;
      context.defined = FALSE ;
      context.cookie = PFIN_DEFINE_COOKIE ;
      context.uid = -1 ;

      ++pfin->threaded ;
      PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
        result = (*pfin->impl->stop)(pfin->instance, &context, reason));
      --pfin->threaded ;

      context.cookie = 0 ;

      if (result != SW_PFIN_SUCCESS) {
        /* An error! We can't do ANYTHING about errors from stop, so set the
           state to broken, and force cleanup of the resources and
           instance. */
        state = PFIN_BROKEN ;
        reason = SW_PFIN_REASON_STOP ;
      } /* if result */
    }

    pfin->state = state ;
  } /* if stopping */

  /* Free up any resources and the instance for this module, regardless of
     whether there was a stop call. */
  if (reason == SW_PFIN_REASON_STOP)
    pfin_free_resources(pfin) ;
}

/* -------------------------------------------------------------------------- */
/** \brief  Finalise all active PFIN modules.

    \param[in] reason  This should be SW_PFIN_REASON_STOP or
    SW_PFIN_REASON_SUSPEND.
 */

static void pfin_stop_modules(sw_pfin_reason reason)
{
  sw_pfin* pfin ;
  for (pfin = pfin_module_list ; pfin ; pfin = pfin->next)
    pfin_stop(pfin, reason) ;
}

/* ========================================================================== */
/** \brief  Bracket calls into a PFIN module.

    \param[in] pfin  The PFIN module's context.

    pfin_thread() is called immediately before calling a PFIN module, and
    pfin_unthread() immediately after. These calls maintain the module's
    thread count, and also resume or restart the module if it was inactive.
 */
static sw_pfin_result pfin_thread(sw_pfin* pfin)
{
  sw_pfin_result error ;

  HQASSERT(pfin, "No PFIN module to activate") ;

  switch (pfin->state) {
  case PFIN_ACTIVE:
    break ;
  case PFIN_SUSPENDED:
    error = pfin_start(pfin, SW_PFIN_REASON_RESUME) ;
    if (error != SW_PFIN_SUCCESS)
      return error ;
    break ;
  case PFIN_INACTIVE:
    error = pfin_start(pfin, SW_PFIN_REASON_START) ;
    if (error != SW_PFIN_SUCCESS)
      return error ;
    break ;
  default:
    return SW_PFIN_ERROR ;
  }
  HQASSERT(pfin->instance, "PFIN threaded, but no instance is present") ;
  ++pfin->threaded ;
  return SW_PFIN_SUCCESS ;
}

static void pfin_unthread(sw_pfin* pfin)
{
  HQASSERT(pfin, "No PFIN to unthread") ;

  if (--pfin->threaded < 0) {
    HQFAIL("PFIN threading has got confused") ;
    pfin->threaded = 0 ;
  }
}

/* ========================================================================== */
/** \brief  Structure for holding the PFIN Exceptions configuration

    This is stored in comparison order as the exceptions can be wildcarded and
    so must be compared in an appropriate order. For example, /HelveticaNeue*
    must be tried before /Helvetica*, in case they point to different modules.
*/
typedef struct pfin_exception_list {
  struct pfin_exception_list* next ;  /**< a linked list store in comparison order */
  NAMECACHE** name ;      /**< Array of NAMECACHE pointers */
  int         names ;     /**< The number of names in the above array */
  int         len ;       /**< The length of this exception string */
  uint8       clist[1] ;  /**< The exception string */
} pfin_exception_list ;

/** A macro used to calculate the size of the above structure */
#define EXCEPTION_LIST_SIZE(_len) ((_len)+offsetof(pfin_exception_list,clist))

/** PFIN's list of exceptions, in comparison order */
static pfin_exception_list* pfin_exceptions_list = NULL ;

/* -------------------------------------------------------------------------- */
/** \brief  Structure for holding PFIN font types configuration */

typedef struct pfin_type_list {
  struct pfin_type_list* next ; /**< a linked list */
  NAMECACHE** name ;      /**< Array of NAMECACHE pointers */
  int         names ;     /**< The number of names in the above array */
  int32       type ;      /**< the font type */
} pfin_type_list ;

/** PFIN's list of fonttypes to claim */
static pfin_type_list* pfin_types_list = NULL ;

/* ========================================================================== */
/** \brief  A postscript operator using SwLengthPatternMatch()

    target template wildeq -> boolean

    Both target and template can be strings, longstrings or names, but
    template may contain '*' and '?' wildcards, which must be prefixed by
    '\' if to be treated as literal characters.
 */
Bool wildeq_(ps_context_t *pscontext)
{
  OBJECT * o[2] ;
  uint8 * s[2] ;
  int32 l[2], i ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* get parameters */
  if ( theStackSize(operandstack) < 1 )
    return error_handler(STACKUNDERFLOW) ;

  o[1] = theTop(operandstack) ;
  o[0] = stackindex(1, &operandstack) ;

  for ( i = 0 ; i < 2 ; i++ ) {
    switch (oType(*o[i])) {
    case OSTRING:
      s[i] = oString(*o[i]) ;
      l[i] = theLen(*o[i]) ;
      break ;
    case ONAME:
      s[i] = theICList(oName(*o[i])) ;
      l[i] = theINLen(oName(*o[i])) ;
      break ;
    case OLONGSTRING:
      s[i] = theLSCList(*oLongStr(*o[i])) ;
      l[i] = theLSLen(*oLongStr(*o[i])) ;
      break ;
    default:
      return error_handler(TYPECHECK) ;
    }
  }

  pop(&operandstack) ;
  object_store_bool(o[0], SwLengthPatternMatch(s[1], l[1], s[0], l[0])) ;
  return TRUE ;
}

/* ========================================================================== */
/** /brief  Compare two wildcarded counted strings.

    This is used to sort wildcarded strings into search order, largest first.

    /param[in] one  The first wildcarded template.
    /param[in] two  The second wildcarded template.

    /retval  0 if the templates have equal priority, such as "abc*" and "123*",
    -1 if the first has the lower priority, eg "a*" and "abc*", and 1 if the
    first has higher priority, such as "HelveticaNeue*" and "Helvetica*".
 */
int CRT_API wildcardnorder(const void* elem1, const void* elem2)
{
  pfin_exception_list * ones = (pfin_exception_list*) elem1,
                      * twos = (pfin_exception_list*) elem2 ;
  uint8  *one, *two ;
  size_t onelen, twolen ;
  int    a, b;

  enum { SMALLER = -1, EQUAL, LARGER } ;

  one = ones->clist ;
  onelen = ones->len ;
  two = twos->clist ;
  twolen = twos->len ;

  while (onelen > 0 && twolen > 0) {
    a = *one++ ;  --onelen ;
    b = *two++ ;  --twolen ;
    if (a == '*' && b == '*') {
      while (a == '*')
        a = (onelen < 1) ? -1 : *one++ ;
      while (b == '*')
        b = (twolen < 1) ? -1 : *two++ ;
      if (a == -1 && b == -1)
        return EQUAL ;
      if (a == -1)
        return LARGER ;
      if (b == -1)
        return SMALLER ;
    }
    if (a == '*')
      return LARGER ;
    if (b == '*')
      return SMALLER ;
    if (a == '?' && b != '?')
      return LARGER ;
    if (a != '?' && b == '?')
      return SMALLER ;
  }
  if (onelen < 1 && twolen < 1)
    return EQUAL ;
  return (onelen < 1) ? LARGER : SMALLER ;
}

/* ========================================================================== */
/** /brief  Hook to allow PFIN to claim ownership of 'normal' fonts

    PFIN is given the opportunity to replace built-in handling of any font. It
    can choose to claim a font either by fonttype or by individual font name.

    /param[in] fonttype  The FontType extracted from the font dictionary.

    /param[in] dict      The font dictionary.

    If PFIN wants to claim this font, it pushes a PFIN key into the font
    dictionary and returns TRUE.

    /return  TRUE if PFIN is claiming this font.
 */

Bool pfin_offer_font(int32 fonttype,
                     /*@in@*/ /*@notnull@*/ OBJECT* dict)
{
  pfin_type_list* type ;
  NAMECACHE** name = NULL ;
  int         names = 0 ;

  HQASSERT(dict, "Null dict") ;

  if (pfin_exceptions_list) {
    pfin_exception_list* list ;
    OBJECT* object = fast_extract_hash_name(dict, NAME_FontName) ;
    if (object == NULL)
      object = fast_extract_hash_name(dict, NAME_CIDFontName) ;
    if (object == NULL || oType(*object) != ONAME)
      return FALSE ;

    /* Check font name against our Exceptions dictionary first. */
    for ( list = pfin_exceptions_list ; name == NULL && list ; list = list->next ) {
      if (SwLengthPatternMatch(&list->clist[0], list->len,
                               oName(*object)->clist, oName(*object)->len)) {
        names = list->names ;
        name = list->name ;
        /* bail out early if this is an exception from the fonttype list */
        if (names == 1 && name[0]->namenumber == NAME_SW)
          return FALSE ;  /* we explicitly don't want this font */
      } /* if match */
    } /* for list */
  } /* if list */

  /* If that didn't match, check fonttype against our list. */
  for ( type = pfin_types_list ; name == NULL && type ; type = type->next ) {
    if ( type->type == fonttype) {
      names = type->names ;
      name = type->name ;
    }
  }

  if (name) {
    Bool result ;
    OBJECT newo = OBJECT_NOTVM_NOTHING ;

    /* we have found this font, so push a PFIN key with either a single name
       or an array of the names from this list entry
     */
    if (names > 1) {
      OBJECT* array ;
      int     i ;

      /* an array of names is necessary */
      if (!ps_array(&newo, names)) {
        HQFAIL("Unable to create /PFIN array") ;
        return FALSE ;
      }

      array = oArray(newo) ;
      for ( i = 0 ; i < names ; i++ ) {
        oName(nnewobj) = name[i] ;
        OCopy(array[i], nnewobj) ;
      }
    } else {
      /* Only one module name, so store it as a name object */
      oName(nnewobj) = name[0] ;
      OCopy(newo, nnewobj) ;
    }

    result = fast_insert_hash_name(dict, NAME_PFIN, &newo) ;
    HQASSERT(result, "Unable to insert /PFIN key") ;

    return result ;   /* we claim this font */
  } /* if name */

  return FALSE ;  /* we ignore this font */
}

/* ========================================================================== */
/** \brief  PFIN configuration.

    Takes a dictionary of PFIN configuration options.
 */
static Bool pfin_configure(OBJECT* dict)
{

  /* Any PFIN configuration options are switched here.
   *
   * There currently aren't any.
   */
  UNUSED_PARAM(OBJECT*, dict) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */


static Bool pfin_fonttype_dict_handler(OBJECT *key, OBJECT *value, void *arg)
{
  pfin_type_list ***pprev = arg;
  size_t names = 0, j;
  pfin_type_list *next;

  switch (oType(*key)) {
  case ONOTHING: /* nothing in this dict slot */
    break ;

  case OINTEGER: /* a fonttype mapping */
    switch (oType(*value)) {
    case ONAME: /* this fonttype mapped to a single module */
      names = 1 ;
      break ;
    case OARRAY: /* this fonttype mapped to a list of modules */
      names = (size_t)theLen(*value);
      for ( j = 0 ; j < names ; j++ )
        if (oType(oArray(*value)[j]) != ONAME)
          return error_handler(TYPECHECK) ;
      break ;
    default:
      return error_handler(TYPECHECK) ;
    } /* switch value type */
    if (names > 0) {
      NAMECACHE **name ;

      name = pfin_alloc(names * sizeof(NAMECACHE*)) ;
      if (name == NULL)
        return error_handler(VMERROR) ;

      switch (oType(*value)) {
      case ONAME:
        name[0] = oName(*value) ;
        break ;
      case OARRAY:
        for ( j = 0 ; j < names ; j++ )
          name[j] = oName(oArray(*value)[j]) ;
        break ;
      }

      next = pfin_alloc(sizeof(pfin_type_list)) ;
      if (next == NULL) {
        pfin_free(name) ;
        return error_handler(VMERROR) ;
      }
      next->next = NULL ;
      next->name = name ;
      next->names = (int32)names;
      next->type = oInteger(*key);

      **pprev = next ;
      *pprev = &next->next ;
    }
    break ;
  default:
    return error_handler(TYPECHECK) ;
  } /* switch key type */
  return TRUE;
}


/** \brief  PFIN font type configuration.

    Takes a dictionary of font types.
 */
static Bool pfin_fonttype(OBJECT* dict)
{
  pfin_type_list *list, *next, **prev ;

  /* discard existing list */
  next = pfin_types_list ;
  pfin_types_list = NULL ;
  while ((list = next) != NULL) {
    next = list->next ;
    pfin_free(list) ;
  }

  prev = &pfin_types_list;
  return walk_dictionary(dict, pfin_fonttype_dict_handler, &prev);
}


/* -------------------------------------------------------------------------- */

typedef struct {
  pfin_exception_list **array;
  size_t length, count;
} pfin_exception_iter_state;


static Bool pfin_exception_dict_handler(OBJECT *key, OBJECT *value, void *arg)
{
  pfin_exception_iter_state *state = arg;
  NAMECACHE* wild = NULL ;
  size_t names = 0;
  size_t j;

  switch (oType(*key)) {
  case ONOTHING: /* nothing in this dict slot */
    break ;
  case ONAME: /* the potentially wildcarded font name */
    wild = oName(*key) ;
    switch (oType(*value)) {
    case ONAME: /* this exception mapped to a single module */
      names = 1 ;
      break ;
    case OARRAY: /* this exception mapped to a list of modules */
      names = theLen(*value) ;
      for ( j = 0 ; j < names ; j++ )
        if (oType(oArray(*value)[j]) != ONAME)
          return error_handler(TYPECHECK) ;
     break ;
    default:
      return error_handler(TYPECHECK) ;
    }
    break ;
  default:
    return error_handler(TYPECHECK) ;
  } /* switch type */

  if (names > 0) {
    /* we have an exception, so store the wildcarded name and the
     * PFIN module name(s) associated with it. */
    NAMECACHE** name ;
    pfin_exception_list *next;

    if (state->count >= state->length)
      return error_handler(RANGECHECK) ;

    name = pfin_alloc(names * sizeof(NAMECACHE*)) ;
    if (name == NULL)
      return error_handler(VMERROR) ;

    switch (oType(*value)) {
    case ONAME:
      name[0] = oName(*value) ;
      break ;
    case OARRAY:
      for ( j = 0 ; j < names ; j++ )
        name[j] = oName(oArray(*value)[j]) ;
      break ;
    }

    next = pfin_alloc(EXCEPTION_LIST_SIZE(wild->len)) ;
    if (next == NULL) {
      pfin_free(name) ;
      return error_handler(VMERROR) ;
    }
    next->name = name ;
    next->names = (int32)names;
    next->len = wild->len ;
    for ( j = 0 ; j < (size_t)next->len ; j++ )
      next->clist[j] = wild->clist[j] ;

    state->array[state->count++] = next;
  } /* if names */
  return TRUE;
}


/** \brief  PFIN exceptions configuration.

    Takes a dictionary of (wildcarded) font names
 */
static Bool pfin_exceptions(OBJECT* dict)
{
  pfin_exception_list *list, *next, **array ;
  int32 length, count, i;
  pfin_exception_iter_state state;

  /* discard existing list */
  next = pfin_exceptions_list ;
  pfin_exceptions_list = NULL ;
  while ((list = next) != NULL) {
    next = list->next ;
    pfin_free(list) ;
  }

  /* build array of new exceptions, then sort and store as a list */
  length = theLen(*dict) ;
  if (length < 1)
    return TRUE ;

  if ((array = pfin_alloc(length * sizeof(pfin_exception_list*))) == NULL )
    return error_handler(VMERROR) ;

  state.array = array; state.length = (size_t)length; state.count = 0;
  if (!walk_dictionary(dict, pfin_exception_dict_handler, &state))
    return FALSE;
  count = (int32)state.count;

  /* sort the array */
  if (count > 1)
    qsort(array, count, sizeof(pfin_exception_list*), wildcardnorder) ;

  /* make the linked list */
  if (count) {
    pfin_exception_list ** prev = &pfin_exceptions_list ;
    for ( i = 0 ; i < count ; i++ ) {
      *prev = array[i] ;
      prev = &(*prev)->next ;
    }
  }

  pfin_free(array) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \brief  Postscript operator to configure PFIN and PFIN modules.

    Takes a dictionary containing /PFIN, /FontType, /Exceptions and/or module
    names as keys.
 */

Bool setpfinparams_(ps_context_t *pscontext)
{
  OBJECT        *dict, *obj ;
  sw_datum      datum = SW_DATUM_NOTHING ;
  sw_pfin*      pfin ;
  sw_pfin_result result ;
  Bool          cycle = FALSE, retry = TRUE ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  HQASSERT(pfin_state == PFIN_STARTED, "setpfinparams called before PFIN started") ;

  /* get dictionary parameter */
  if ( theStackSize(operandstack) < 0 )
    return error_handler(STACKUNDERFLOW) ;
  dict = theTop(operandstack) ;
  if ( oType(*dict) != ODICTIONARY )
    return error_handler(TYPECHECK) ;
  if ( !oCanRead(*oDict(*dict)) &&
       !object_access_override(oDict(*dict)) )
    return error_handler(INVALIDACCESS) ;

  /* look for the keys we recognise */
  if ((obj = fast_extract_hash_name(dict, NAME_PFIN)) != NULL &&
      !pfin_configure(obj))
    return FALSE ;
  if ((obj = fast_extract_hash_name(dict, NAME_FontType)) != NULL) {
    if (!pfin_fonttype(obj))
      return FALSE ;
    cycle = TRUE ;
  }
  if ((obj = fast_extract_hash_name(dict, NAME_Exceptions)) != NULL) {
    if (!pfin_exceptions(obj))
      return FALSE ;
    cycle = TRUE ;
  }
  /* If ownership of non-PFIN fonts could have changed, bump the cycle number */
  if (cycle && (++pfin_cycle & !PFIN_MASK))
    pfin_cycle = 1 ;  /* and wrap around */

  /* now go through all the modules */
  for (pfin = pfin_module_list ; pfin ; pfin = pfin->next) {
    if (pfin->state != PFIN_BROKEN) {
      /* is there a configure key for this module? */
      if ( (oName(nnewobj) = cachename((uint8 *)pfin->name,
                                       pfin->namelen)) == NULL )
        return FALSE;
      if ((obj = fast_extract_hash(dict, &nnewobj)) != NULL) {
        if ((result = swdatum_from_object(&datum, obj)) != SW_DATA_OK)
          return error_from_sw_data_result(result) ;

        /* If it is a dictionary, extract the two keys which PFIN handles on
         * behalf of the module.
         */
        if (datum.type == SW_DATUM_TYPE_DICT) {
          static sw_data_match match[2] = {
            { SW_DATUM_BIT_BOOLEAN | SW_DATUM_BIT_NOTHING,
              SW_DATUM_STRING("PassOnError"),
              SW_DATUM_INVALID },
            { SW_DATUM_BIT_BOOLEAN | SW_DATUM_BIT_NOTHING,
              SW_DATUM_STRING("PassOnUnsupported"),
              SW_DATUM_INVALID }
          } ;
          if (pfin_data_api.match(&datum, match, 2) == SW_DATA_OK) {
            if (match[0].value.type == SW_DATUM_TYPE_BOOLEAN)
              pfin->pass_on_error = match[0].value.value.boolean ;
            if (match[1].value.type == SW_DATUM_TYPE_BOOLEAN)
              pfin->pass_on_unsupported = match[1].value.value.boolean ;
          }
        }

        /* now pass it to the module */
        retry = (pfin->impl->configure != NULL) ;
        while (retry) {
          sw_pfin_define_context context = {0} ;

          context.pfin = pfin ;
          context.prefix = FALSE ;
          context.defined = FALSE ;
          context.cookie = PFIN_DEFINE_COOKIE ;
          context.uid = -1 ;

          if ((result = pfin_thread(pfin)) == SW_PFIN_SUCCESS) {
            PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
              result = pfin->impl->configure(pfin->instance, &context, &datum));
            pfin_unthread(pfin) ;
          }
          context.cookie = 0 ;

          switch (result) {
          case SW_PFIN_SUCCESS:
            retry = FALSE ;
            break ;
          case SW_PFIN_ERROR_MEMORY:
            if (pfin_austerity(pfin))
              break ;
            /* else drop through */
          default:
            return error_handler(pfin_error(result)) ;
          } /* switch result */
        } /* while retry */
      } /* if config key */
    } /* if module cares */
  } /* for each module */
  pop( &operandstack ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/** \brief  Call a module's miscop entry. */

sw_pfin_result pfin_miscop(sw_pfin * pfin, sw_datum ** pparam)
{
  sw_pfin_result result = RETRY ;
  sw_datum     * entry ;

  if ( !pfin || !pparam || pfin->state == PFIN_BROKEN ||
       pfin->impl->info.version < SW_PFIN_API_VERSION_20071231 ||
       pfin->impl->miscop == 0 )
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  entry = *pparam ;

  while (result == RETRY) {
    sw_pfin_define_context context = {0} ;
    context.pfin = pfin ;
    context.prefix = FALSE ;
    context.defined = FALSE ;
    context.cookie = PFIN_DEFINE_COOKIE ;
    context.uid = -1 ;

    *pparam = entry ;
    if ((result = pfin_thread(pfin)) == SW_PFIN_SUCCESS) {
      PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
        result = pfin->impl->miscop(pfin->instance, &context, pparam));
      pfin_unthread(pfin) ;
    }
    context.cookie = 0 ;

    if (result == SW_PFIN_ERROR_MEMORY && pfin_austerity(pfin))
      result = RETRY ;
  } /* while retry */

  if (result)
    return FAILURE(result) ;

  if (*pparam == entry)
    *pparam = 0 ;

  return SW_PFIN_SUCCESS ;
}

/* -------------------------------------------------------------------------- */
/** \brief  Postscript operator allowing implementation-defined interaction
    with a specified PFIN module.

    module_name anything pfinop -> optional_result
*/

Bool pfinop_(ps_context_t *pscontext)
{
  OBJECT       * oname, * oparam ;
  sw_datum       datum = SW_DATUM_NOTHING, * param = 0 ;
  sw_pfin      * pfin ;
  sw_pfin_result result ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  HQASSERT(pfin_state == PFIN_STARTED, "pfinop called before PFIN started") ;

  /* get name and parameter */
  if ( theStackSize(operandstack) < 1 )
    return error_handler(STACKUNDERFLOW) ;

  oparam = theTop(operandstack) ;
  oname  = (fastStackAccess(operandstack)) ?
           oparam - 1 : stackindex(1, &operandstack) ;

  pfin = pfin_findpfin(oname);
  if (!pfin)
    return error_handler(UNDEFINED) ;

  /* If module broken or it doesn't have a miscop call, silently ignore
     like setpfinparams, by leaving param = 0. */
  if ( pfin->state != PFIN_BROKEN &&
       pfin->impl->info.version >= SW_PFIN_API_VERSION_20071231 &&
       pfin->impl->miscop ) {

    /* convert oparam to datum and call module */
    param = &datum ;

    if ((result = swdatum_from_object(param, oparam)) != SW_DATA_OK)
      return error_from_sw_data_result(result) ;

    if ((result = pfin_miscop(pfin, &param)) != SW_PFIN_SUCCESS)
      return error_handler(pfin_error(result)) ;
  }

  if (param) {
    /* convert the result back into an OBJECT, push into oname and pop oparam */
    if ((result = object_from_swdatum(oname, param)) != SW_DATA_OK)
      return error_from_sw_data_result(result) ;
  } else {
    pop(&operandstack) ; /* no result, so pop both parameters */
  }

  pop(&operandstack) ;
  return TRUE ;
}

/* ========================================================================== */
/** \brief  Postscript operator primarily to allow PFIN to hook into
    findresource and resourceforall.

    name pfinhook -> bool       } define the named font if possible
    string pfinhook -> bool     }
    null pfinhook -> |          define all fonts
    true pfinhook -> |          start up all PFIN modules (call from hqnstart)
    false pfinhook -> |         shut down all PFIN modules
    int pfinhook -> |           alter the number of null pfinhooks to ignore
*/
Bool pfinhook_(ps_context_t *pscontext)
{
  OBJECT*       obj ;
  sw_datum      name = SW_DATUM_NOTHING ;
  sw_pfin*      pfin ;
  Bool          found = FALSE ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  HQASSERT(pfin_state == PFIN_STARTED, "pfinhook called before PFIN started") ;

  /* get name/string parameter */
  if ( theStackSize(operandstack) < 0 )
    return error_handler(STACKUNDERFLOW) ;

  obj = theTop(operandstack) ;
  switch (oType(*obj)) {
  case OBOOLEAN:
    /* PFIN already started - so shut down */
    pfin_stop_modules(SW_PFIN_REASON_STOP) ;
    if (oBool(*obj)) { /* true pfinhook */
      /* Start all font modules */
      pfin_start_modules(SW_PFIN_REASON_START) ;
    }
    pop(&operandstack) ;
    return TRUE ;

  case ONULL:
    /* A resourceforall is about to occur - define all fonts. */
    /** \todo it may be better to pass the template in and pass it onto the
     * modules. This would require THEM to do wildcard matching, which would
     * really require adding wildcard comparison to the data interface.
     */
    if (pfin_ignore > 0) {
      /* This is a horrible hack. During RIP start-up but after the PFIN
       * system has been initialised, something does a resourceforall on the
       * Font category. This would cause all the newly initialised PFIN
       * modules to define the very fonts they were hoping to defer until
       * they are required, which defeats the object somewhat. So, ignore it,
       * but only the once.
       */
      --pfin_ignore ;
      pop(&operandstack) ;
      return TRUE ;
    }
    /* name is already of type NOTHING, so nowt to do */
    break ;

  case OINTEGER:
    /* Allow the horrible hack above to be controlled.
     *
     * Parameter increments or decrements the current count, or resets it to
     * zero if zero.
     */
    if (oInteger(*obj))
      pfin_ignore += oInteger(*obj) ;
    else
      pfin_ignore = 0 ;
    return TRUE ;

  case OSTRING:
  case ONAME:
    /* a findresource is in the process of failing */
    {
      sw_data_result result = swdatum_from_object(&name, obj) ;
      if (result != SW_DATA_OK)
        return error_handler(error_from_sw_data_result(result)) ;
    }
    break ;

  default:
    return error_handler(TYPECHECK) ;
  }

  /* now have the font name in name (or a nothing for resourceforall) */
  for (pfin = pfin_module_list ; pfin ; pfin = pfin->next) {
    if (pfin->state != PFIN_BROKEN && pfin->impl->find) {
      sw_pfin_result result = RETRY;

      while (result == RETRY) {
        sw_pfin_define_context context = {0} ;
        context.pfin    = pfin ;
        context.defined = FALSE ;
        context.cookie  = PFIN_DEFINE_COOKIE ;
        context.prefix  = FALSE ;
        context.uid     = -1 ;

        if ((result = pfin_thread(pfin)) == SW_PFIN_SUCCESS) {
          PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
            result = pfin->impl->find(pfin->instance, &context, &name));
          pfin_unthread(pfin) ;
        }
        context.cookie = 0 ;

        if (result == SW_PFIN_ERROR_MEMORY && pfin_austerity(pfin))
          result = RETRY ;

        /* Note that when looking for a named font we continue calling
           all PFIN modules even once the font has been defined - this is
           to be consistent with "null pfinhook" which would result in the
           last redefinition winning. */
        if (!result)
          found |= context.defined ;
      }
      if (result)
        return error_handler(pfin_error(result)) ;

    } /* if active */
  } /* for each module */

  /* Finally, if we failed to define anything and the font is of the form
     /<modulename>#<fontname>, pass the <fontname> directly to the module
     called <modulename>, which will cause a definition of <fontname> to be
     defined as /<modulename>#<fontname> and hence allow access to a specific
     font even when multiple definitions exist. This is primarily for testing,
     but may be useful for FontSubstitution.

     Note that once we allow multiple instantiation, where a module name could
     be <module>:<instance>, we have to allow fontnames of the format
     /<module>:<instance>#<fontname> */
  if (!found && oType(*obj) != ONULL && name.length > 2) {
    size_t hash ;
    for (hash = name.length-2; hash > 0; hash--) {
      if (name.value.string[hash] == '#')
        break ;
    }
    if (hash && hash < 65536) {
      OBJECT module   = OBJECT_NOTVM_STRING("") ;
      theLen(module)  = (uint16) hash ;
      oString(module) = (uint8 *) name.value.string ;

      pfin = pfin_findpfin(&module) ;
      if (pfin && pfin->state != PFIN_BROKEN && pfin->impl->find) {
        sw_pfin_result result = RETRY;

        name.value.string += hash + 1 ;
        name.length -= hash + 1 ;

        while (result == RETRY) {
          sw_pfin_define_context context = {0} ;
          context.pfin    = pfin ;
          context.defined = FALSE ;
          context.cookie  = PFIN_DEFINE_COOKIE ;
          context.prefix  = TRUE ;
          context.uid     = -1 ;

          if ((result = pfin_thread(pfin)) == SW_PFIN_SUCCESS) {
            PROBE(SW_TRACE_FONT_PFIN, (intptr_t)pfin->impl,
              result = pfin->impl->find(pfin->instance, &context, &name));
            pfin_unthread(pfin) ;
          }
          context.cookie = 0 ;

          if (result == SW_PFIN_ERROR_MEMORY && pfin_austerity(pfin))
            result = RETRY ;

          if (!result)
            found |= context.defined ;
        }
        if (result)
          return error_handler(pfin_error(result)) ;
      }/* module recognised */
    }/* hash found in fontname */
  }/* named font not defined */

  /* return a boolean of whether the given font was defined */
  if (oType(*obj) == ONULL)
    pop(&operandstack) ;
  else
    object_store_bool(obj, found) ;  /* tell findresource whether to retry */
  return TRUE ;
}

/* ========================================================================== */
/* PFIN implementation class initialisation and finalisation. These are called
   at RIP boot-up and shut-down. */

/** Dispose of an individual PFIN entry, removing it from the list. This does
    not call the finish() method, because this may be called during startup,
    before initialisation of the registered API. */
static void pfin_entry_dispose(sw_pfin **pentry)
{
  sw_pfin *pfin ;

  HQASSERT(pentry, "Nowhere to find PFIN entry") ;

  pfin = *pentry ;
  HQASSERT(pfin, "No PFIN entry") ;
  HQASSERT(pfin->state == PFIN_INACTIVE,
           "PFIN module not inactive, but getting killed") ;

  *pentry = pfin->next ;

  /* Kill the instance's memory tracker, then the instance itself. */
  track_swmemory_destroy(&pfin->mem) ;
  mm_free(pfin_pool, pfin, SW_PFIN_LENGTH(pfin->namelen)) ;
}

/** Initialise an individual PFIN entry. If the initialisation fails, the
    entry is disposed of. */
static Bool pfin_entry_init(sw_pfin **pentry)
{
  sw_pfin *pfin ;
  sw_pfin_api *impl ;
  sw_pfin_init_params params ;
  Bool result;

  HQASSERT(pentry, "Nowhere to find PFIN entry") ;

  pfin = *pentry ;
  HQASSERT(pfin, "No PFIN entry") ;

  impl = pfin->impl ;
  HQASSERT(impl, "No implementation in PFIN entry") ;

  HQASSERT(pfin_class_memory, "PFIN memory callbacks not initialised") ;
  params.mem = pfin_class_memory ;

  /* Initialise the implementation. */
  if ( impl->init == NULL )
    return TRUE ;
  PROBE(SW_TRACE_FONT_PFIN, (intptr_t)impl,
    result = impl->init(impl, &params));
  if ( result )
    return TRUE ;

  /* Initialisation failed. Remove from the entry list. */
  pfin_entry_dispose(pentry) ;

  return FAILURE(FALSE) ;
}

/** Finalise an individual PFIN entry, destroying its memory and removing it
    from the module list. */
static void pfin_entry_finish(sw_pfin **pentry)
{
  sw_pfin *pfin ;
  sw_pfin_api *impl ;

  HQASSERT(pentry, "Nowhere to find PFIN entry") ;

  pfin = *pentry ;
  HQASSERT(pfin, "No PFIN entry") ;

  impl = pfin->impl ;
  HQASSERT(impl, "No implementation in PFIN entry") ;

  /* Finalise the implementation. */
  if ( impl->finish != NULL ) {
    PROBE(SW_TRACE_FONT_PFIN, (intptr_t)impl,
      impl->finish(impl));
  }

  pfin_entry_dispose(pentry) ;
}

/* Add a new (instantiation of a) module to the list of PFIN modules. */

sw_api_result new_module(sw_pfin_api * impl, sw_pfin * base,
                         uint8 * name, unsigned int namelen,
                         sw_pfin ** result)
{
  sw_pfin * pfin, ** prev ;

  if (impl == 0 && base == 0)
    return FAILURE(SW_API_ERROR) ;

  /* Ensure we don't already have a PFIN module with this name. */
  for (prev = &pfin_module_list ; (pfin = *prev) != NULL ; prev = &pfin->next) {
    if (pfin->namelen == namelen &&
        strcmp((char*)pfin->name, (char*)name) == 0)
      return FAILURE(SW_API_ERROR) ;
  }

  pfin = mm_alloc(pfin_pool, SW_PFIN_LENGTH(namelen), MM_ALLOC_CLASS_PFIN) ;
  if (!pfin)
    return FAILURE(SW_API_ERROR) ;

  pfin->next     = NULL ;
  pfin->instance = 0 ;
  pfin->state    = PFIN_INACTIVE ;
  pfin->threaded = 0 ;
  pfin->namelen  = namelen ;
  memcpy((char*)&pfin->name[0], name, namelen) ;
  if (base) {
    /* Clone the parent */
    pfin->base                = base ;
    pfin->impl                = base->impl ;
    pfin->pass_on_error       = base->pass_on_error ;
    pfin->pass_on_unsupported = base->pass_on_unsupported ;
  } else {
    /* Initialise with given implementation */
    pfin->base                = NULL ;
    pfin->impl                = impl ;
    pfin->pass_on_error       = FALSE ;
    pfin->pass_on_unsupported = TRUE ;
  }

  /* Create a tracking allocator for this module implementation. */
  if ( (pfin->mem = track_swmemory_create(pfin_pool, MM_ALLOC_CLASS_PFIN)) == NULL ) {
    mm_free(pfin_pool, pfin, SW_PFIN_LENGTH(pfin->namelen)) ;
    return FAILURE(SW_API_ERROR) ;
  }

  /* Append new instance to end of list */
  *prev = pfin ;

  /* if PFIN is already running, initialise this module immediately */
  if (pfin_state == PFIN_STARTED ) {
    if ( !pfin_entry_init(prev) )
      return FAILURE(SW_API_INIT_FAILED) ;

    /** \todo Should this really be an error, considering that PFIN can
        handle failure of the start() method? */
    if ( pfin_start(pfin, SW_PFIN_REASON_START) != SW_PFIN_SUCCESS )
      return FAILURE(SW_API_INIT_FAILED) ;
  }

  if (result)
    *result = pfin ;

  return SW_API_REGISTERED ;
}

/* ========================================================================== */
/** \brief Find a PFIN module given a name or array of names */

sw_pfin* pfin_findpfin(OBJECT* theo)
{
  sw_pfin*   pfin, *base = 0 ;
  NAMECACHE* name = NULL ;
  unsigned int colon = 0, namelen ;

  HQASSERT(theo, "Null ptr") ;

  while (name == NULL) {
    switch (oType(*theo)) {
    case ONAME:
      name = oName(*theo) ;
      break ;
    case OSTRING:
      if ( (name = cachename(oString(*theo), theLen(*theo))) == NULL )
        return FAILURE(NULL) ;
      break ;
    case OARRAY:
      theo = oArray(*theo) ;
      if (oType(*theo) == OARRAY)     /* nested arrays aren't allowed */
        return FAILURE(NULL) ;
      /* round again with the first element of the array */
      break ;
    default:
      return FAILURE(NULL) ;
    }
  }

  namelen = name->len ;

  /* Spot names of the form "<module>:<instance>" */
  for (colon = 1; colon < namelen-1; colon++)
    if (name->clist[colon] == ':')
      break ;
  if (colon >= namelen-1)
    colon = 0 ;

  /* We now have the name of the PFIN module required for the font */
  for ( pfin = pfin_module_list ; pfin ; pfin = pfin->next ) {
    if ( namelen == pfin->namelen &&
         strncmp((char*)name->clist, (char*)pfin->name, namelen) == 0 )
      return pfin ;
    if ( colon == pfin->namelen &&
         strncmp((char*)name->clist, (char*)pfin->name, colon) == 0 )
      base = pfin ;
  }

  /* Automatic multiple instantiation.
   *
   * If the name was not matched but contains a colon, it is of the form
   * <module name>:<instance name> - if the module name CAN be matched,
   * a new instantiation will be invoked.
   */
  if (colon && base &&
      new_module(0, base, name->clist, namelen, &pfin) == SW_API_REGISTERED)
    return pfin ;

  return FAILURE(NULL) ;
}

/* ========================================================================== */
/* The next UniqueID+1 to be allocated to a font */
static uint32 pfinuniqueid = 0xFFFFFF | (UID_RANGE_PFIN << 24) ;

/** \brief  UniqueID override callback.

*/

static sw_pfin_result RIPCALL pfin_uid(/*@in@*/ /*@notnull@*/ sw_pfin_define_context *context,
                                       /*@in@*/ /*@notnull@*/ int *puid)
{
  sw_pfin *pfin ;

  /* Check syntax */
  if ( context == NULL ||
       context->cookie != PFIN_DEFINE_COOKIE ||
       (pfin = context->pfin) == NULL ||
       puid == NULL )
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  if (*puid == -1) {
    /* Font module wants to know what the UniqueID will be */

    if (context->uid == -1) {
      /* uid was set to "automatic", so allocate one now */
      context->uid = pfinuniqueid-- ;
      if (pfinuniqueid < (UID_RANGE_PFIN << 24))
        pfinuniqueid += 1<<24 ;  /* wrap round back into range */
    }

    *puid = context->uid ;

  } else {
    /* Font module is overriding the UniqueID */

    context->uid = *puid ;
  }

  return SW_PFIN_SUCCESS ;
}


/** \brief  Font definition callback.

    This is called by the module during initialise, configure or find.
 */

static sw_pfin_result RIPCALL pfin_define(/*@in@*/ /*@notnull@*/ sw_pfin_define_context *context,
                                          /*@in@*/ /*@notnull@*/ const sw_datum* fontname,
                                          /*@in@*/ /*@notnull@*/ const sw_datum* id,
                                          /*@in@*/ sw_datum* encoding)
{
  OBJECT encodearray = OBJECT_NOTVM_NOTHING ;
  Bool cid = FALSE ;
  int32 fonts = 1, i, prefixlen = 0 ;
  sw_datum temp = SW_DATUM_NOTHING, *dict ;
  const sw_datum *name = fontname ;
  sw_pfin *pfin ;
  char prefixed[128] ;

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* match used to check syntax of encoding array when used as a CID ROS */
  static sw_data_match ros[3] = {
    {SW_DATUM_BIT_STRING,   SW_DATUM_INTEGER(0)},
    {SW_DATUM_BIT_STRING,   SW_DATUM_INTEGER(1)},
    {SW_DATUM_BIT_INTEGER,  SW_DATUM_INTEGER(2)},
  } ;

  /* default font matrix */
  static sw_datum fontmatrix[6] = {
    SW_DATUM_FLOAT(SW_DATUM_1_0F),  SW_DATUM_FLOAT(SW_DATUM_0_0F),
    SW_DATUM_FLOAT(SW_DATUM_0_0F),  SW_DATUM_FLOAT(SW_DATUM_1_0F),
    SW_DATUM_FLOAT(SW_DATUM_0_0F),  SW_DATUM_FLOAT(SW_DATUM_0_0F)
  } ;

  /* CID ROS dictionary */
  static sw_datum cidrosdict[6] = {
    SW_DATUM_STRING("Registry"),    SW_DATUM_STRING(""),
    SW_DATUM_STRING("Ordering"),    SW_DATUM_STRING(""),
    SW_DATUM_STRING("Supplement"),  SW_DATUM_INTEGER(0)
  } ;

  /* Jobs may attempt to treat PFIN fonts like e.g. type 1 font and alter
   * charstrings, without checking the actual font type. Add trivial
   * charstrings to (temporarily) patch this error. */
  static sw_datum charstrings[3] = {  SW_DATUM_DICT(&charstrings[1],1),
    SW_DATUM_STRING(".notdef"), SW_DATUM_INTEGER(0),
   } ;

  /* CID font dictionary */
  static sw_datum cidfontdict[17] = {  SW_DATUM_DICT(&cidfontdict[1],8),
    SW_DATUM_STRING("CIDFontType"),    SW_DATUM_INTEGER(FONTTYPE_PFINCID),
    SW_DATUM_STRING("CIDFontName"),    SW_DATUM_STRING(""),
    SW_DATUM_STRING("CIDSystemInfo"),  SW_DATUM_DICT(cidrosdict,3),
    SW_DATUM_STRING("FontMatrix"),     SW_DATUM_ARRAY(fontmatrix,6),
    SW_DATUM_STRING("PFIN"),           SW_DATUM_STRING("PFIN"),
    SW_DATUM_STRING("PFID"),           SW_DATUM_NULL,
    SW_DATUM_STRING("UniqueID"),       SW_DATUM_INTEGER(0),
    SW_DATUM_STRING("CharStrings"),    SW_DATUM_DICT(charstrings,1),
  } ;

  /* font dictionary (in the same order and format as cidfontdict) */
  static sw_datum fontdict[17] = {  SW_DATUM_DICT(&fontdict[1],8),
    SW_DATUM_STRING("FontType"),    SW_DATUM_INTEGER(FONTTYPE_PFIN),
    SW_DATUM_STRING("FontName"),    SW_DATUM_STRING(""),
    SW_DATUM_STRING("Encoding"),    SW_DATUM_NULL,
    SW_DATUM_STRING("FontMatrix"),  SW_DATUM_ARRAY(fontmatrix,6),
    SW_DATUM_STRING("PFIN"),        SW_DATUM_STRING("PFIN"),
    SW_DATUM_STRING("PFID"),        SW_DATUM_NULL,
    SW_DATUM_STRING("UniqueID"),    SW_DATUM_INTEGER(0),
    SW_DATUM_STRING("CharStrings"), SW_DATUM_DICT(charstrings,1),
  } ;

  /* StandardEncoding */
  static sw_datum standard_encoding = SW_DATUM_STRING("StandardEncoding") ;

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  /* check pfin, fontname and id parameters */
  if ( context == NULL ||
       context->cookie != PFIN_DEFINE_COOKIE ||
       (pfin = context->pfin) == NULL ||
       fontname == NULL || id == NULL)
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  /* check fontname */
  switch (fontname->type) {
  case SW_DATUM_TYPE_STRING:
    /* name = fontname, fonts = 1 */
    break ;
  case SW_DATUM_TYPE_ARRAY:
    if (!pfin_array_is_all(fontname, SW_DATUM_TYPE_STRING) ||
        pfin_data_api.get_indexed(fontname, 0, &temp) != SW_DATA_OK )
      return FAILURE(SW_PFIN_ERROR_SYNTAX) ;
    name = &temp ;
    fonts = CAST_SIZET_TO_INT32(fontname->length) ;
    break ;
  default:
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;
  }

  /* Check encoding parameter. There are four possibilities:
     NULL : StandardEncoding
     String : Some (other) named encoding
     Array[string,string,int] : a CIDFont Registry/Ordering/Supplement
     Array[1-256 strings] : an encoding array
  */
  if (encoding == NULL)
    encoding = &standard_encoding ;

  switch (encoding->type) {
  case SW_DATUM_TYPE_STRING:
    if (encoding->length > MAXPSSTRING)
      return FAILURE(SW_PFIN_ERROR_SYNTAX);  /* string much too long! */
    /* named encoding - so find the encoding resource */
    oString(snewobj) = (uint8*) encoding->value.string ; /* const poison */
    theLen(snewobj) = CAST_SIZET_TO_UINT16(encoding->length) ;
    oName(nnewobj) = system_names + NAME_Encoding ;
    oName(nnewobje) = system_names + NAME_findresource ;
    if (!interpreter_clean(&nnewobje, &snewobj, &nnewobj, NULL))
      return FAILURE(SW_PFIN_ERROR) ;
    OCopy(encodearray, *theTop(operandstack)) ;
    pop(&operandstack) ;
    break ;
  case SW_DATUM_TYPE_ARRAY:
    /* is this a CID ROS? */
    if (encoding->length == 3 &&
        pfin_data_api.match(encoding, ros, 3) == SW_DATA_OK) {
      cid = TRUE ;
      break ;
    }
    /* is this an encoding array? */
    if (encoding->length > 0 && encoding->length < 257 &&
        pfin_array_is_all(encoding, SW_DATUM_TYPE_STRING))
      break ;
    /* otherwise drop through to error */
  default:
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;
  }

  /* Create the font dictionary.
   * It's so much easier to do this as SW_DATA and build it all in one go.
   */
  if (cid) {
    cidrosdict[1] = ros[0].value ;
    cidrosdict[3] = ros[1].value ;
    cidrosdict[5] = ros[2].value ;
    dict = cidfontdict ;
  } else {
    if (encoding->type == SW_DATUM_TYPE_STRING) {
      /* lookup encoding later */
      fontdict[6].type = SW_DATUM_TYPE_NULL ;
    } else
      fontdict[6] = *encoding ;
    dict = fontdict ;
  }

  dict[10].value.string = pfin->name ;
  dict[10].length = pfin->namelen ;
  dict[12] = *id ;

  /* Allow 20090305 modules to set the UniqueID, otherwise allocate. */
  if (context->uid == -1) {
    dict[14].value.integer = pfinuniqueid-- ;
    if (pfinuniqueid < (UID_RANGE_PFIN << 24))
      pfinuniqueid += 1<<24 ;  /* wrap round back into range */
  } else
    dict[14].value.integer = context->uid ;

  /* Build the fontname prefix, if we're prefixing by the module name */
  if (context->prefix && pfin->namelen < 128) {
    HqMemCpy(prefixed, pfin->name, pfin->namelen) ;
    prefixed[pfin->namelen] = '#' ;
    prefixlen = pfin->namelen + 1 ;
  }

  /* for each fontname to be defined as this font dictionary... */
  for (i = 1 ; i <= fonts ; i++) {
    OBJECT ps_op = OBJECT_NOTVM_NAME(NAME_defineresource, EXECUTABLE) ;
    OBJECT ps_cat = OBJECT_NOTVM_NOTHING, ps_dict = OBJECT_NOTVM_NOTHING,
      *ps_name ;

    /* The (CID)FontName (optionally prefixed) */
    dict[4] = *name ;
    if (context->prefix && prefixlen + dict[4].length < 129) {
      HqMemCpy(prefixed + prefixlen, dict[4].value.string, dict[4].length) ;
      dict[4].value.string = prefixed ;
      dict[4].length += prefixlen ;
    }

    /* create the dictionary */
    if (object_from_swdatum(&ps_dict, dict) != SW_DATA_OK)
      return FAILURE(SW_PFIN_ERROR) ;
    /* if a named encoding, poke in the encoding value */
    if (encoding->type == SW_DATUM_TYPE_STRING &&
        !fast_insert_hash_name(&ps_dict, NAME_Encoding, &encodearray) )
      return FAILURE(SW_PFIN_ERROR) ;
    /* define the font */
    if ((ps_name = fast_extract_hash_name(&ps_dict,
                                          cid ? NAME_CIDFontName : NAME_FontName)) == NULL )
      return FAILURE(SW_PFIN_ERROR) ;
    object_store_name(&ps_cat, cid ? NAME_CIDFont : NAME_Font, LITERAL) ;
    /* define it as a font/cidfont resource */
    if (!interpreter_clean(&ps_op, ps_name, &ps_dict, &ps_cat, NULL))
      return FAILURE(SW_PFIN_ERROR) ;
    pop(&operandstack) ;  /* discard the resulting font dictionary */
    /* get next fontname, if any */
    if (i < fonts &&
        pfin_data_api.get_indexed(fontname, i, &temp) != SW_DATA_OK)
      return FAILURE(SW_PFIN_ERROR) ;
  } /* for all font names */

  /* mark context as having defined fonts (for pfinhook_()) */
  context->defined = TRUE ;
  context->uid     = -1 ;   /* After a definition, UniqueID goes to automatic */

  return SW_PFIN_SUCCESS ;
}

/* ========================================================================== */
/* undefine a single font */

static Bool pfin_undefine_font(sw_pfin *pfin,
                               sw_datum* fontname)
{
  OBJECT ps_name = OBJECT_NOTVM_NOTHING,
    ps_cat = OBJECT_NOTVM_NAME(NAME_Font, LITERAL),
    ps_op = OBJECT_NOTVM_NAME(NAME_resourcestatus, EXECUTABLE) ;
  Bool exists ;

  HQASSERT(pfin, "No PFIN context") ;

  if ( fontname->type != SW_DATUM_TYPE_STRING ||
       fontname->length > MAXPSNAME )
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  /* do "<fontname> /Font resourcestatus" */
  theTags(ps_name) = OSTRING | READ_ONLY | LITERAL ;
  theLen(ps_name)  = (uint16) fontname->length ;
  oString(ps_name) = (uint8*) fontname->value.string ;

  if (!interpreter_clean(&ps_op, &ps_name, &ps_cat, NULL))
    return FALSE ;

  /* stack contains "status size true" if the resource exists, or "false" */
  if (oType(*theTop(operandstack)) != OBOOLEAN)
    return FAILURE(FALSE) ;

  exists = oBool(*theTop(operandstack)) ;
  pop(&operandstack) ;

  if (exists) {
    int32 status ;

    HQASSERT(theStackSize(operandstack) > 1, "Stack underflow") ;

    pop(&operandstack) ; /* drop size */

    if (oType(*theTop(operandstack)) != OINTEGER)
      return FALSE ;

    status = oInteger(*theTop(operandstack)) ;
    pop(&operandstack) ; /* drop status */

    if (status == 0) {
      OBJECT dict = OBJECT_NOTVM_NOTHING, *value ;

      /* resource exists due to a defineresource, check it was us */
      oName(ps_op) = system_names + NAME_findresource ;
      if (!interpreter_clean(&ps_op, &ps_name, &ps_cat, NULL))
        return FALSE ;

      OCopy(dict, *theTop(operandstack)) ;
      pop(&operandstack) ;

      if ((value = fast_extract_hash_name(&dict, NAME_PFIN)) != NULL &&
          oType(*value) == ONAME &&
          oName(*value)->len == pfin->namelen &&
          HqMemCmp(oName(*value)->clist, oName(*value)->len,
                   (uint8 *)pfin->name, (int32)pfin->namelen) == 0 ) {
        oName(ps_op) = system_names + NAME_undefineresource ;
        if (!interpreter_clean(&ps_op, &ps_name, &ps_cat, NULL))
          return FALSE ;
      }
    }
  }
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Undefine one or a number of fonts */

static sw_pfin_result RIPCALL pfin_undefine(/*@in@*/ /*@notnull@*/ sw_pfin_define_context *context,
                                            /*@in@*/ /*@notnull@*/ sw_datum* fontname)
{
  sw_pfin *pfin ;

  if ( context == NULL ||
       context->cookie != PFIN_DEFINE_COOKIE ||
       (pfin = context->pfin) == NULL ||
       fontname == NULL)
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;

  switch (fontname->type) {
  case SW_DATUM_TYPE_STRING:
    /* undefine this font */
    if ( ! pfin_undefine_font(pfin, fontname) )
      return SW_PFIN_ERROR ;
    break ;

  case SW_DATUM_TYPE_ARRAY:
    /* undefine this array of font names */
    if (pfin_array_is_all(fontname, SW_DATUM_TYPE_STRING)) {
      size_t i;
      sw_data_result result ;
      sw_datum value ;

      for (i = 0 ; i < fontname->length ; i++) {
        result = pfin_data_api.get_indexed(fontname, i, &value) ;
        if (result != SW_DATA_OK)
          return FAILURE(SW_PFIN_ERROR) ;
        if (!pfin_undefine_font(pfin, &value))
          return SW_PFIN_ERROR ;
      }
      break ;
    }
    /* otherwise, drop through into syntax error */
  default:
    return FAILURE(SW_PFIN_ERROR_SYNTAX) ;
  }

  return SW_PFIN_SUCCESS ;
}

/* ========================================================================== */
/* Register a PFIN module */

sw_api_result RIPCALL SwRegisterPFIN(/*@in@*/ /*@notnull@*/ sw_pfin_api* impl)
{
  unsigned int namelen ;
  static char* reserved[] = {"SW","PFIN","FontType","Exceptions"} ;
  unsigned int i ;

  if ( pfin_state == PFIN_NOT_INITIALISED )
    return FAILURE(SW_API_TOO_EARLY) ;

  /* Check parameters. init(), finish(), construct(), destruct(),
     configure(), find() are optional. metrics() and outline() are mandatory.
   */
  if (impl == NULL ||
      impl->info.name == NULL || *impl->info.name == '\0' ||
      impl->info.display_name == NULL || *impl->info.display_name == '\0' ||
      impl->metrics == NULL || impl->outline == NULL )
    return FAILURE(SW_API_INCOMPLETE) ;

  /* Check versions and sizes. Every supported version should have a case to
     check its instance size here. */
  switch (impl->info.version ) {
  /* this just can't work...
  case SW_PFIN_API_VERSION:
    if (impl->info.instance_size < sizeof(sw_pfin_instance))
      return FAILURE(SW_API_INSTANCE_SIZE) ;
    break ;
  */
  case SW_PFIN_API_VERSION_20071111:
  case SW_PFIN_API_VERSION_20071231:
  case SW_PFIN_API_VERSION_20080808:
  case SW_PFIN_API_VERSION_20090305:
  case SW_PFIN_API_VERSION_20090401:
    break ;
  default:
    if (impl->info.version > SW_PFIN_API_VERSION)
      return FAILURE(SW_API_VERSION_TOO_NEW) ;
    return FAILURE(SW_API_VERSION_TOO_OLD) ;
  }

  /* Get module name */
  namelen = strlen_int32((const char *)impl->info.name) ;
  if (namelen > MAXPSNAME)
    return FAILURE(SW_API_INVALID) ;

  /* Module names cannot contain colons (instantiation naming) */
  for (i = 0; i < namelen; i++)
    if (impl->info.name[i] == ':')
      return FAILURE(SW_API_NOT_UNIQUE) ;

  /* Some module names are reserved */
  for (i = 0; i < NUM_ARRAY_ITEMS(reserved); i++) {
    if ( strcmp((const char *)impl->info.name, reserved[i]) == 0 )
      return FAILURE(SW_API_NOT_UNIQUE) ;
  }

  /* Check display name validity */
  if ( utf8_validate(impl->info.display_name,
                     NULL, /* zero terminated */
                     NULL, /* don't need number of UTF8 units */
                     NULL, /* don't need number of UTF16 units */
                     NULL  /* don't need number of code points units */
                     ) > 0 )
    return FAILURE(SW_API_BAD_UNICODE) ;

  return new_module(impl, 0, (uint8*) impl->info.name, namelen, 0) ;
}

/* -------------------------------------------------------------------------- */

static void pfin_finish(void);


#if defined(DEBUG_BUILD)
extern sw_pfin_api pfin_example_module ;
#endif


/** \brief  Preboot initialisation of the PFIN system.

    This is called from SwInit(), and is used to register an example PFIN
    module.
 */
static Bool pfin_swinit(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  HQASSERT(pfin_state == PFIN_NOT_INITIALISED, "PFIN not initialised correctly") ;

  /* Preboot initialisation is done. */
  pfin_state = PFIN_PREBOOT ;

#if defined(DEBUG_BUILD)
   /* Register an example PFIN module */
   if (SwRegisterPFIN(&pfin_example_module) != SW_API_REGISTERED)
     return FAILURE(FALSE) ;
#endif

  return TRUE ;
}


/** \brief  Initialise the PFIN system.
 */
static Bool pfin_postboot(void)
{
  sw_pfin **pentry = &pfin_module_list ;

  HQASSERT(pfin_state == PFIN_PREBOOT, "PFIN not initialised correctly") ;

  /* Create a tracking allocator for PFIN implementation memory. */
  if ( (pfin_class_memory = track_swmemory_create(pfin_pool,
                                                  MM_ALLOC_CLASS_PFIN)) == NULL )
    return FALSE ;

  while ( *pentry != NULL ) {
    if ( !pfin_entry_init(pentry) ) {
      /* Failed to construct an entry. Start by disposing of all not-yet
         constructed entries. */
      while ( *pentry )
        pfin_entry_dispose(pentry) ;

      /* Now dispose of all previously-constructed entries. */
      while ( pfin_module_list )
        pfin_entry_finish(&pfin_module_list) ;

      /* Destroy the class memory callback instance. */
      track_swmemory_destroy(&pfin_class_memory) ;

      return FALSE ;
    }

    pentry = &(*pentry)->next ;
  }

  pfin_state = PFIN_STARTED ;

  if ( !low_mem_handler_register(&pfin_lowmem_handler) ) {
    pfin_finish();
    return FALSE;
  }
  /* Modules are all initialised. Now start them all, creating instances. */
  pfin_start_modules(SW_PFIN_REASON_START) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* \brief  Finalise the PFIN system.

   At this point we quit all remaining PFIN modules, and discard their contexts.

   None should be threaded, but there's not much we can do if they are.
 */

static void pfin_finish(void)
{
  sw_pfin* pfin ;

  HQASSERT(pfin_state == PFIN_PREBOOT || pfin_state == PFIN_STARTED,
           "PFIN not initialised correctly");

  /* Ensure no PFIN modules are still threaded */
  for (pfin = pfin_module_list ; pfin ; pfin = pfin->next) {
    HQASSERT(pfin->threaded == 0, "PFIN module still threaded") ;
    pfin->threaded = 0 ;
  }

  /* quit remaining PFIN modules */
  pfin_stop_modules(SW_PFIN_REASON_STOP) ;

  if (pfin_state == PFIN_STARTED) {
    low_mem_handler_deregister(&pfin_lowmem_handler);
  }

  while ( pfin_module_list ) {
    pfin_entry_finish(&pfin_module_list) ;
  }

  track_swmemory_destroy(&pfin_class_memory) ;

  pfin_state = PFIN_NOT_INITIALISED ;
}

/* -------------------------------------------------------------------------- */
static void init_C_globals_pfin(void)
{
  pfin_cycle = 1 ;
  pfin_exceptions_list = NULL ;
  pfin_state = PFIN_NOT_INITIALISED ;
  pfin_ignore = 1 ;
  pfin_module_list = NULL ;
  pfin_types_list = NULL ;
  pfin_class_memory = NULL ;
}

void pfin_C_globals(core_init_fns *fns)
{
  init_C_globals_pfin() ;

  fns->swinit = pfin_swinit ;
  fns->postboot = pfin_postboot ;
  fns->finish = pfin_finish ;
}

/** \} */
/* ========================================================================== */

/* Log stripped */
