/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!export:system.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface for pre-allocated gstate objects. Addition to this file is
 * deprecated.
 */

#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include "mm.h"
#include "graphict.h"

CLIPRECORD* get_cliprec(
  mm_pool_t     pool);

void free_cliprec(
  CLIPRECORD*   clipptr,
  mm_pool_t     pool);

/** \brief Take a new reference to a clip record.

    \param theclip The clip record on which an extra reference is being
    taken. This pointer can be NULL.

    This function should be called whenever a pointer to a clip record is
    stored in a structure. Note that we only count direct references to clip
    records, not indirect references, so if a clip record pointer is freed
    which has not been reserved, the clip chain will be corrupted.
 */
void gs_reservecliprec(/*@null@*/ /*@in@*/ CLIPRECORD *theclip);

/** \brief Release a reference to a clip record, clearing the clip record
    pointer.

    \param theclip The location of the clip record whose reference is being
    freed. This pointer must be non-null, but it can point to a NULL clip
    record reference.

    On exit, the clip record pointer is cleared. Clip records will be
    destroyed when the last reference is released. Note that we only count
    direct references to clip records, not indirect references, so if a clip
    record pointer is freed which has not been reserved, the clip chain will
    be corrupted.
 */
void gs_freecliprec(/*@notnull@*/ /*@in@*/ CLIPRECORD **theclip);

/** \brief Allocate a clip save structure, copying the old structure. */
CLIPPATH *gs_newclippath(CLIPPATH *top) ;

/** \brief Take a new reference to a clip save structure.

    \param clippath The clip save on which an extra reference is being taken.
    This pointer can be NULL.

    This function should be called whenever a pointer to a clip save is
    stored in a structure. Note that we only count direct references to clip
    saves, not indirect references, so if a clip save pointer is freed which
    has not been reserved, the clip stack will be corrupted.
 */
void gs_reserveclippath(/*@null@*/ /*@in@*/ CLIPPATH *clippath) ;

/** \brief Release a reference to a clip save structure, clearing the clip
    save pointer.

    \param clippath The location of the clip save whose reference is being
    freed. This pointer must be non-null, but it can point to a NULL clip
    save reference.

    On exit, the clip save pointer is cleared. The clip record references in
    clip save will be released and the clip save will be destroyed when the
    last reference to a clip save is released. Note that we only count direct
    references to clip saves, not indirect references, so if a clip save
    pointer is freed which has not been reserved, the clip stack will be
    corrupted.
 */
void gs_freeclippath(/*@notnull@*/ /*@in@*/ CLIPPATH **clippath) ;

PATHLIST* get_path(
  mm_pool_t     pool);
void free_path(
  PATHLIST*     pathptr,
  mm_pool_t     pool);
void path_free_list(
  PATHLIST*     thepath,
  mm_pool_t     pool);

LINELIST* get_line(
  mm_pool_t     pool);
LINELIST* get_3line(
  mm_pool_t     pool);
void free_line(
  LINELIST*     lineptr,
  mm_pool_t     pool);

CHARPATHS *get_charpaths( void );
void free_charpaths( void );

int32 initSystemMemoryCaches(
  mm_pool_t     pool);      /* I */
void clearSystemMemoryCaches(
  mm_pool_t     pool);      /* I */

extern CHARPATHS  *thecharpaths;

#endif /* protection for multiple inclusion */

/*
Log stripped */
