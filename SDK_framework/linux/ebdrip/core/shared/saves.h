/** \file
 * \ingroup saves
 *
 * $HopeName: SWcore!shared:saves.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Save/restore support. Save/restore are awkward because they are
 * fundamental parts of the object subsystem, however every module may have
 * information which it requires to be saved/restored. To prevent each module
 * having to know about the other module's details, the hook for implementing
 * save and restore is done at the SWcore level, where all of the sub-modules
 * are integrated.
 *
 * Each module is responsible for maintaining its own state. When a save or
 * restore is made, the module will be called back with a pointer to a
 * sub-structure of the save state. The module is responsible for saving its
 * data through that pointer.
 */

#ifndef __SAVES_H__
#define __SAVES_H__

struct SWSTART ; /* from COREinterface */
struct OBJECTSAVE; /* from COREobjects */
struct PS_SAVEINFO; /* from SWv20 */

/** \defgroup saves Save/Restore support
    \ingroup ps */
/* \{ */

typedef struct SAVELIST SAVELIST ;

/** Runtime initialisation for save levels. */
Bool saves_swstart(struct SWSTART *params) ;
void saves_finish(void) ;

/** Return the save level of a save object */
int32 save_level(SAVELIST *save) ;

/** The save protocol is two-phase. First, there is a call to save_prepare.
   This call can return a NULL pointer and set an error code, in which case
   save_commit will not be called. If other checks pass, then save_commit is
   called with the pointer returned from the previous save_prepare.
   save_commit MUST succeed, so save_prepare should check if there is any
   reason the save might not succeed and throw an error if necessary. There
   will only be one pending save_prepare at once; there is no save_abort
   call, it is assumed that a second save_prepare in a row implies the
   previous save was aborted. save_commit is called as the last thing before
   the MM system is notified of a save level change, and resets the input
   context's save level. */
SAVELIST *save_prepare(int32 slevel) ;
void save_commit(SAVELIST *save) ;

/* The restore protocol is also two-phase. restore_prepare is called before
   making any irrevocable changes. This implementation calls the module's
   restore prepare functions for every save level from the current level down
   to and including the target save level. restore_prepare may return an
   error, however an error during the callbacks is likely to be fatal, there
   is no transaction rollback interface. restore_commit is called as the last
   thing before the MM system is notified of a save level change, and resets
   the input context's save level. It MUST succeed; errors should have been
   notified during the restore_prepare phase. In this implementation, the
   modules receive only one callback on their restore commit interfaces, for
   the target save level. */
int32 restore_prepare(int32 slevel) ;
void restore_commit(int32 slevel) ;


/* save_objectsave_map - map over objectsaves in the save stack
 *
 * The step function takes an objectsave, the save level it belongs to,
 * and the closure argument passed through from the call to s_o_m.
 */

typedef int save_objectsave_map_fn(struct OBJECTSAVE *objectsave,
                                   int32 level, void *p);

int save_objectsave_map(save_objectsave_map_fn fn, void *p);


/** Get memory from save system. The pointer returned is an object immediately
   followed by the size of memory allocated. The client should set the tags
   of the object returned to indicate the type of object saved, and set the
   object's value to the saved object. SWv20 supports OGSTATE, ODICTIONARY
   and OARRAY in these objects. The object immediately preceding the object
   returned is an array object containing a chain pointer to the previous
   save memory allocated */
OBJECT *get_savememory(corecontext_t *corecontext, size_t size, int32 glmode) ;

/** Yuck! Unfortunately PostScript needs some of the information it stored away
   in the save level before we are ready to call restore_prepare(). Get the
   PostScript save info for a saved level. */
struct PS_SAVEINFO *saved_ps_info(int32 slevel) ;

/* \} */

#endif /* Protection from multiple inclusion */

/*
Log stripped */
