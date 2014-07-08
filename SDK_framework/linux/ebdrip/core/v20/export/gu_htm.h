/** \file
 * \ingroup halftone
 *
 * $HopeName: SWv20!export:gu_htm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Modular halftones API
 */

#ifndef __GU_HTM_H__
#define __GU_HTM_H__

#include "lists.h"        /* dll_list_t */
#include "graphict.h"     /* GSTATE */
#include "objectt.h"      /* OBJECT */
#include "swhtm.h"        /* sw_htm_halftone_details */
#include "displayt.h" /* dl_erase_nr */
#include "dlstate.h" /* NUM_DISPLAY_LISTS */


/** Channel value that can never be used for a real channel. */
#define CHANNEL_INVALID (-1)


/** Structure list used to manage module registration and information.
 * Should be considered to be read-only outside gu_htm.c.
 */
typedef struct htm_module_entry {
  /** Pointer to the module's registered API.
   */
  sw_htm_api                 *impl ;

  /** render_inited is TRUE, during rendering, between htm_RenderInitiation
   * and htm_RenderCompletion, for each module that has halftone instances
   * selected, and reported success from its RenderInitiation function.
   * It is FALSE otherwise.
   */
  Bool                        render_inited ;

  struct htm_module_entry    *next ;
} htm_module_entry ;


/** Check whether a named external screening module is registered.
 * This is to allow an existence check early in sethalftone.
 */
Bool htm_IsModuleNameRegistered( OBJECT *modname );

/** Return the first module entry in the list.
 * Returns NULL if the list is empty.
 */
const htm_module_entry *htm_GetFirstModule() ;


/** Structure list used to manage references to modular halftones.
 * The list uniquely identifies each modular halftone instance that
 * has been selected but not yet released.
 * Halftones which require interrelated channels are added to the
 * head of the list, all others being added to the tail.
 * Should be considered to be read-only outside gu_htm.c.
 */

typedef struct MODHTONE_REF {
  htm_module_entry *mod_entry ; /* Link back to the module entry */

  dll_link_t link ; /**< Doubly-linked list link. */

  uint32 refcount ;

  /** Lists the DLs this is used on. Unused slots contain INVALID_DL. */
  dl_erase_nr usage[NUM_DISPLAY_LISTS];
  size_t last_set_usage_index;

  Bool reported; /**< Reported by screenforall/calibration? (Front-end only.) */
  Bool screenmark; /**< Encountered in current iteration? (Back-end only.) */

  /** A verification cookie, used when upcasting sw_htm_instance references
      passed back by the halftone implementation. This is set to a cookie
      value when this structure is initialised, and reset when the structure
      is deallocation. It provides a paranoid check that the instance pointer
      may actually be valid. Of course, it won't help if the implementation
      passes a pointer back in part of memory that isn't mapped. */
  uintptr_t cookie ;

  /* The sw_htm_instance *MUST* be the last field in this structure. It is
     sub-classed through an extendable allocation, using a data size
     determined by the OEM. The instance field *MUST* also be a value field,
     rather than a pointer field, because the RIP uses its address to do an
     upcast to the containing MODHTONE_REF. */
  sw_htm_instance instance;
} MODHTONE_REF ;


/** Return the first modular halftone instance for the given page.
 * Returns NULL if there are none.
 */
MODHTONE_REF *htm_first_halftone_ref(dl_erase_nr erasenr);

/** Return the next modular halftone instance for the given page.
 * Returns NULL if there are no more.
 */
MODHTONE_REF *htm_next_halftone_ref(dl_erase_nr erasenr, MODHTONE_REF *mhtref);


/** Return the source bit depth of the modular halftone instance. */
unsigned int htm_SrcBitDepth(const MODHTONE_REF *mhtref) ;

/** Query if a modular halftone instance needs an object map.
 */
Bool htm_WantObjectMap(const MODHTONE_REF *mhtref) ;

/** Query if a modular halftone instance needs to see empty bands.
 */
Bool htm_ProcessEmptyBands(const MODHTONE_REF *mhtref) ;

/** Return amount of latency required by modular halftone instance. */
unsigned int htm_Latency(const MODHTONE_REF *mhtref);

/** Increase the reference count of a MODHTONE_REF.
 */
void htm_AddRefHalftoneRef( MODHTONE_REF *mhtref  );

/** Release a MODHTONE_REF.
 * If this is the last reference, the screening module is called to
 * release its underlying halftone instance, and the structure freed.
 */
void htm_ReleaseHalftoneRef( MODHTONE_REF *mhtref ) ;


/** Test if the screen is used on the given DL. */
Bool htm_is_used(const MODHTONE_REF *mhtref, dl_erase_nr erasenr);

/** Mark the screen used on the given DL.

    \return TRUE if the screen was marked and the resources are available to
            render it. FALSE if it failed and an error was raised.

    This also declares the necessary resources for rendering this screen.
 */
Bool htm_set_used(MODHTONE_REF *mhtref, dl_erase_nr erasenr);

/** Mark the screen no longer used on the given DL. */
void htm_reset_used(MODHTONE_REF *mhtref, dl_erase_nr erasenr);

/** Returns the erase number of the DL the screen was last used on. */
dl_erase_nr mhtref_last_used(const MODHTONE_REF *mhtref);


/** Select a modular screen (halftone instance).
 * On success, this allocates a MODHTONE_REF structure, a pointer
 * to which is passed back via the pmhtref argument.
 * To release the halftone, use htm_ReleaseHalftoneRef() passing
 * it the MODHTONE_REF pointer that was given to you on select.
 * Returns FALSE on failure (error_handler will have been called).
 */
Bool htm_SelectHalftone(
    OBJECT        *modname ,
    NAMECACHE     *htcolor, /* e.g. color name in a type 5 */
    OBJECT        *htdict,  /* Can be the sub-type-5 dict */
    GS_COLORinfo  *colorInfo,
    MODHTONE_REF **pmhtref ) ;


/** For all screening modules which have halftone instances selected and used on
 * the given page, call their RenderInitiation function.
 *
 * Returns \c SW_HTM_SUCCESS if they all succeed, or the \c sw_htm_result
 * of the first failing call.
 * No \c error_handler call is issued, so that the renderer can consider
 * retrying if it thinks it can solve their issues.
 * The 'retry' argument should be set in such a case, so that only
 * the modules from the one reporting the failure onwards will be called.
 * If a second call is made with 'retry' FALSE, all modules will be called
 * again, regardless of whether they have been called already.
 */
sw_htm_result htm_RenderInitiation( sw_htm_render_info *render_info,
                                    Bool retry, dl_erase_nr erasenr );

/** For all screening modules which have halftone instances selected,
 * and have reported success from their RenderInitiation function, call
 * their RenderCompletion function.
 */
void htm_RenderCompletion( sw_htm_render_info *render_info,
                           dl_erase_nr erasenr, Bool aborted ) ;

/** Call the screening module to halftone one or more channels.
 * Returns FALSE if the screening module fails the request before having
 * started it.
 * The 'instance' member of the sw_htm_dohalftone_request structure
 * can be left uninitialized by the caller because it will be
 * filled in, from *mhtref, by htm_DoHalftone itself.
 */
Bool htm_DoHalftone( MODHTONE_REF *mhtref,
                     sw_htm_dohalftone_request *request );

/** Abort a previous DoHalftone with the same args (if the module supports it). */
void htm_AbortHalftone( MODHTONE_REF *mhtref,
                        sw_htm_dohalftone_request *request );

/** Return the PS error code for a SW_HTM_ERROR_xxxx value.
 * Returns UNREGISTERED for any which have no direct equivalent.
 */
int32 htm_MapHtmResultToSwError( sw_htm_result htmResult ) ;

/** \brief Ensure that the render resources for a modular screen are reserved.

    \param page   The page on which the screen will be used.
    \param mhtref The screen to reserve resources for.

    \retval TRUE  Resources to render the screen were reserved successfully.
    \retval FALSE Resources to render the screen were not reserved.
*/
Bool htm_ReserveResources(struct DL_STATE *page, const MODHTONE_REF *mhtref) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
