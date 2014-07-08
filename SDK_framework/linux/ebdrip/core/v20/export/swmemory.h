/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:swmemory.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ScriptWorks memory API
 */

#ifndef __SWMEMORY_H__
#define __SWMEMORY_H__

#include "objecth.h"
#include "graphics.h"
#include "bitbltt.h" /* FORMTYPE_BLANK */
#include "devops.h"
#include "dictscan.h"


/** \brief Clear all save objects down to the specified save level.

    Other side effects are:

      All files are closed which save levels are greater than the specified
      level.

      Names will be removed from the name cache whose save level is greater
      than the specified level (but only if the specified level is a global
      save level).

      The graphics state is popped down to the specified save level.

    \param slevel The save level to restore to.
    \param dpd Pagedevice deactivation parameters for the restored graphics
      state.
    \return TRUE if the function succeeds, FALSE if the function fails.
*/
Bool purge_memory(int32 slevel, deactivate_pagedevice_t *dpd);

/** \brief Clear all graphic states on the graphics stack, back to an
    identified saved graphics state. This routine is commonly used to restore
    back to a known state on completion of a target (a form, pattern,
    character, etc).

    \param gid The graphic state id of the graphics state to restore.
    \param gtype The graphic state type of the frame to restore. This is used
      to detect unrestored "save" graphics states.
    \param dpd Pagedevice deactivation parameters for the restored graphics
      state.
    \return TRUE if the function succeeds, FALSE if the function fails.
*/
Bool gs_cleargstates(int32 gid, int32 gtype, deactivate_pagedevice_t *dpd) ;

/* check_asave is declared in objecth.h, because it is used within
   COREobjects. */

/* check_dsave is declared in objecth.h, because it is used within
   COREobjects. */

/** \brief Check if any of a dictionary needs saving, because it will be
    modified at this savelevel.

    \param optr The top-level dictionary object.
    \return TRUE if the function succeeds, FALSE if the function fails (this
      may happen if save memory cannot be allocated).

    This routine should not be used in most cases; piecemeal saving of partial
    dictionaries is more efficient. This routine is used in HDLT callbacks,
    allowing results to be poked back into the dictionary through pointers
    obtained through NAMETYPEMATCHes.
*/
Bool check_dsave_all(/*@notnull@*/ OBJECT *optr);

/** \brief Check if a gstate represented in PostScript VM needs saving,
    because it will be modified at this savelevel. This routine is called
    when storing gstates in PostScript gstate objects, either through the
    currentgstate or copy operators. The storage for these gstate objects is
    in a special typed PostScript VM pool.

    \param gs_check The gstate object to check for saving.
    \param glmode The globalness of the gstate object being saved.
*/
Bool check_gsave(corecontext_t *context, GSTATE *gs_check, int32 glmode);

/** \brief Make a deep copy of an object into PostScript VM. This routine
    makes the assumption that if an object is already in PostScript VM, then
    all of its sub-objects are too. This routine should only be called when
    the object is not known to be in PostScript VM; it uses tests that are too
    expensive to perform for the general case.

    \param copy The object into which the copy will be made.
    \param orig The object from which the copy will be made. This must not
      be the same as the object into which the copy is being made.
    \param recursion The maximum depth of the object to copy. The routine will
      failed with a LIMITCHECK if this limit is exceeded.
    \param glmode The globalness of the newly-created object copies.
    \return TRUE if the function succeeds, FALSE if it failed. The function
      may fail due to memory exhaustion, recursion limit checks, or invalid
      local into global.
*/
Bool psvm_copy_object(/*@out@*/ /*@notnull@*/ OBJECT *copy,
                      /*@in@*/ /*@notnull@*/ OBJECT *orig,
                      uint32 recursion, int32 glmode);

/** \brief Make a deep copy of dictmatch results into PostScript VM, using
    psvm_copy_object.

    \param dict The dictionary into which the copies will be made.
    \param match The dictmatch from which the copies will be made.
    \param recursion The maximum depth of the objects to copy. The routine
      will failed with a LIMITCHECK if this limit is exceeded.
    \param glmode The globalness of the newly-created object copies.
    \return TRUE if the function succeeds, FALSE if it failed. The function
      may fail due to memory exhaustion, recursion limit checks, or invalid
      local into global.
*/
Bool psvm_copy_dictmatch(/*@out@*/ /*@notnull@*/ OBJECT *dict,
                         /*@in@*/ /*@notnull@*/ NAMETYPEMATCH match[],
                         uint32 recursion, int32 glmode);

#if defined(ASSERT_BUILD)
/** \brief This function is usable in asserts to check whether an object or
    the value of a composite object is in PostScript VM. If you have an OBJECT
    in hand, you probably want to test if it is a composite object first, and
    then call this on the data pointer of the object.

    \param memory The memory pointer to check is in PostScript VM.
    \return TRUE if the pointer is in PostScript VM, FALSE if it is not. NULL
      pointers are treated as if they are in PostScript VM.
*/
Bool psvm_assert_check(/*@null@*/ void *memory) ;
#endif

#endif /* protection for multiple inclusion */


/* Log stripped */
