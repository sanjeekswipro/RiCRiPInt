/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!src:surface.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Surface global list for CORErender module.
 */

#include <stddef.h>

#include "core.h"
#include "coreinit.h"
#include "objects.h"
#include "swstart.h"
#include "namedef_.h"
#include "ripdebug.h"
#include "surface.h"
#include "hqmemcmp.h"
#include "hqbitops.h"
#include "dlstate.h"
#include "bitblts.h"
#include "bitblth.h"
#include "tables.h"

static surface_set_t *surface_set_list ;

#if defined(ASSERT_BUILD)
static Bool select_datum_hides(const sw_datum *new_select,
                               const sw_datum *old_select)
{
  HQASSERT(new_select != NULL, "No new datum to compare") ;
  HQASSERT(old_select != NULL, "No old datum to compare") ;

  if ( old_select->type == new_select->type ) {
    switch (old_select->type) {
    case SW_DATUM_TYPE_INTEGER:
      return old_select->value.integer == new_select->value.integer ;
    case SW_DATUM_TYPE_STRING:
      return HqMemCmp(old_select->value.string,
                      CAST_UNSIGNED_TO_INT32(old_select->length),
                      new_select->value.string,
                      CAST_UNSIGNED_TO_INT32(new_select->length)) == 0 ;
    case SW_DATUM_TYPE_BOOLEAN: /* values must match */
      return old_select->value.boolean == new_select->value.boolean ;
    case SW_DATUM_TYPE_ARRAY: /* length and entries must match */
      if ( old_select->length == new_select->length ) {
        size_t length = old_select->length ;
        old_select = old_select->value.opaque ;
        new_select = new_select->value.opaque ;
        while ( length > 0 ) {
          if ( !select_datum_hides(old_select, new_select) )
            return FALSE ;
          ++old_select ;
          ++new_select ;
          --length ;
        }
        return TRUE ;
      }
      break ;
    case SW_DATUM_TYPE_DICT:
      /* All keys in new must exist and match values in old. Ergo, if old is
         longer than new, it doesn't match (we're ignoring the possibility of
         duplicate values). */
      if ( new_select->length <= old_select->length ) {
        const sw_datum *new_item = new_select->value.opaque ;
        size_t new_length = new_select->length ;

        /* For each pair, check that the new dict has this pair. */
        while ( new_length > 0 ) {
          size_t old_length = old_select->length ;
          const sw_datum *old_item = old_select->value.opaque ;

          HQASSERT(new_item->type == SW_DATUM_TYPE_STRING,
                   "Surface dict select key is not a string") ;
          while ( old_length > 0 ) {
            HQASSERT(old_item->type == SW_DATUM_TYPE_STRING,
                     "Surface dict select key is not a string") ;
            if ( HqMemCmp(old_item->value.string,
                          CAST_UNSIGNED_TO_INT32(old_item->length),
                          new_item->value.string,
                          CAST_UNSIGNED_TO_INT32(new_item->length)) == 0 ) {
              if ( !select_datum_hides(&new_item[1], &old_item[1]) )
                return FALSE ;
              goto next_new ;
            }
            old_item += 2 ;
            --old_length ;
          }

          return FALSE ; /* Old key was not found, so cannot match */

        next_new:
          new_item += 2 ;
          --new_length ;
        }
        return TRUE ;
      }
      break ;
    default:
      HQFAIL("Surface selection doesn't support datum type") ;
      return FALSE ;
    }
  }

  return FALSE ;
}
#endif

void surface_set_register(surface_set_t *set)
{
  HQASSERT(set != NULL, "No surface set to register") ;
  HQASSERT(set->conditions.type == SW_DATUM_TYPE_ARRAY ||
           set->conditions.type == SW_DATUM_TYPE_DICT,
           "Surface selection is not array") ;

#if defined(ASSERT_BUILD)
  {
    const surface_set_t *surfaces ;

    for ( surfaces = surface_set_list ; surfaces != NULL ; surfaces = surfaces->next ) {
      /* Check we haven't already registered this set. */
      HQASSERT(surfaces != set, "Surface set already registered in set list") ;

      /* We should now check that the search criteria are more specific than
         the registered set, or this new set will prevent the
         previously-registered set from being selected. More specific means
         that either:

         1) A key exists in the new surface entry that does not exist in the
            old entry.
         2) A key in the new entry requires a different value than the old
            entry.
      */
      HQASSERT(!select_datum_hides(&set->conditions, &surfaces->conditions),
               "Surface definition hides existing surface definition") ;
    }
  }
  HQASSERT(set->indexed != NULL, "No surfaces in set") ;
  HQASSERT(set->n_indexed > 0, "No surfaces in set") ;
  HQASSERT(set->indexed[SURFACE_OUTPUT] != NULL,
           "Surface set has no output surface") ;
  {
    size_t i ;

    for ( i = 0 ; i < set->n_indexed ; ++i ) {
      const surface_t *surface = set->indexed[i] ;
      if ( surface != NULL ) {
        const clip_surface_t *clip = surface->clip_surface ;
        HQASSERT(clip != NULL,
                 "Surface in set has empty clip surface") ;
        HQASSERT(clip->complex_clip != NULL,
                 "Clip surface does not have complex_clip method") ;
        HQASSERT(clip->base.backdropblit == NULL,
                 "Clip surface backdropblit method is pointless") ;
      }
    }
  }

  {
    const transparency_surface_t *trans ;
    if ( (trans = surface_find_transparency(set)) != NULL ) {
      HQASSERT(trans->region_request != NULL,
               "Transparency surface does not have region_request method") ;
      HQASSERT(trans->region_create != NULL,
               "Transparency surface does not have region_create method") ;
    }
  }

  HQASSERT(set->packing_unit_bits == 8 ||
           set->packing_unit_bits == 16 ||
           set->packing_unit_bits == 32 ||
           set->packing_unit_bits == sizeof(blit_t) * 8,
           "Packing unit size for surface set is odd") ;
#endif

  set->next = surface_set_list ;
  surface_set_list = set ;
}

static Bool surface_datum_matches(const sw_datum *datum, const OBJECT *object)
{
  HQASSERT(datum != NULL, "No surface selection datum to compare") ;
  HQASSERT(object != NULL, "No page device object to compare") ;

  switch (datum->type) {
  case SW_DATUM_TYPE_INTEGER:
    /* Integer values must match (no real coercion) */
    if ( oType(*object) == OINTEGER )
      return datum->value.integer == oInteger(*object) ;
    /* Automatically coerce object array to the length of the array, for
       convenience in matching. */
    if ( oType(*object) == OARRAY || oType(*object) == OPACKEDARRAY )
      return datum->value.integer == theLen(*object) ;
    break ;
  case SW_DATUM_TYPE_STRING:
    /* String values must match exactly */
    switch ( oType(*object) ) {
    case OSTRING:
      return HqMemCmp(datum->value.string,
                      CAST_UNSIGNED_TO_INT32(datum->length),
                      oString(*object), theLen(*object)) == 0 ;
    case OLONGSTRING:
      return HqMemCmp(datum->value.string,
                      CAST_UNSIGNED_TO_INT32(datum->length),
                      theLSCList(*oLongStr(*object)),
                      theLSLen(*oLongStr(*object))) == 0 ;
    case ONAME:
      return HqMemCmp(datum->value.string,
                      CAST_UNSIGNED_TO_INT32(datum->length),
                      theICList(oName(*object)),
                      theINLen(oName(*object))) == 0 ;
    }
    return FALSE ;
  case SW_DATUM_TYPE_BOOLEAN:
    /* Boolean values must match exactly */
    return (oType(*object) == OBOOLEAN &&
            datum->value.boolean == oBool(*object)) ;
  case SW_DATUM_TYPE_ARRAY:
    if ( oType(*object) == OARRAY || oType(*object) == OPACKEDARRAY ) {
      /* Array length and entries must match. */
      if ( datum->length == theLen(*object) ) {
        size_t length = datum->length ;
        datum = datum->value.opaque ;
        object = oArray(*object) ;
        while ( length > 0 ) {
          if ( !surface_datum_matches(datum, object) )
            return FALSE ;
          ++datum ;
          ++object ;
          --length ;
        }
        return TRUE ;
      }
    } else {
      /* Datum has array, object has value. We should know the types of
         values in the pagedevice, so we overload this case to automatically
         use datum array as a set of alternative values. */
      size_t length = datum->length ;
      datum = datum->value.opaque ;
      while ( length > 0 ) {
        if ( surface_datum_matches(datum, object) )
          return TRUE ;

        ++datum ;
        --length ;
      }
    }
    break ;
  case SW_DATUM_TYPE_DICT:
    /* All entries in the datum dict must match an entry in the object dict. */
    if ( oType(*object) == ODICTIONARY ) {
      size_t length = datum->length ;
      datum = datum->value.opaque ;

      while ( length > 0 ) {
        OBJECT *value ;
        OBJECT name = OBJECT_NOTVM_NULL ;

        HQASSERT(datum->type == SW_DATUM_TYPE_STRING,
                 "Surface dict select key is not a string") ;

        if ( (oName(name) = cachename((const uint8 *)datum->value.string,
                                      CAST_SIZET_TO_UINT32(datum->length))) == NULL ) {
          HQFAIL("Name cache failed for surface select datum") ;
          return FALSE ;
        }
        theTags(name) = ONAME | LITERAL ;

        if ( (value = fast_extract_hash(object, &name)) == NULL ||
             !surface_datum_matches(&datum[1], value) )
          return FALSE ;

        datum += 2 ;
        --length ;
      }

      return TRUE ;
    }
    return FALSE ;
  case SW_DATUM_TYPE_NOTHING:
    /* Datum type nothing is a wild card, matching anything. */
    return TRUE ;
  default:
    HQFAIL("Surface selection doesn't support datum type") ;
    return FALSE ;
  }

  return FALSE ;
}

const surface_set_t *surface_set_select(const OBJECT *pagedevdict)
{
  const surface_set_t *surfaces ;

  for ( surfaces = surface_set_list ; surfaces != NULL ; surfaces = surfaces->next ) {
    if ( surface_datum_matches(&surfaces->conditions, pagedevdict) )
      return surfaces ;
  }

  return NULL ;
}

#if 0
Bool surface_set_init(const surface_set_t *surfaces,
                      surface_handle_t *handle,
                      uint32 surfaces_used)
{
  size_t i ;

  HQASSERT(surfaces != NULL, "No surface set") ;

  for ( i = 0 ; i < surfaces->n_indexed ; ++i ) {
    if ( surfaces_used & BIT(i) ) {
      const surface_t *surface = surfaces->indexed[i] ;
      HQASSERT(surface != NULL, "Surface type used, but has no definition") ;
      if ( surface->init != NULL && !(*surface->init)(surface, handle) )
        return FALSE ;
    }
  }

  return TRUE ;
}
#endif

void dl_surface_used(DL_STATE *page, int type)
{
  uint32 mask = BIT(type) ;
  const surface_set_t *surfaces ;

  HQASSERT(page != NULL, "No page for used surface") ;
  surfaces = page->surfaces ;

  if ( surfaces != NULL && (page->surfaces_used & mask) == 0 ) {
    const surface_t *surface = surface_find(page->surfaces, type) ;

    if ( surface != NULL && surface->reserve != NULL )
      (*surface->reserve)() ;

    page->surfaces_used |= mask ;
  }
}

const surface_t *surface_find(const surface_set_t *surfaces, int type)
{
  HQASSERT(surfaces != NULL, "No surface set") ;
  HQASSERT(type >= 0 && type < N_SURFACE_TYPES,
           "Searching for invalid surface type") ;

  if ( (size_t)type >= surfaces->n_indexed )
    return NULL ;

  return surfaces->indexed[type] ;
}

const transparency_surface_t *surface_find_transparency(const surface_set_t *surfaces)
{
  const surface_t *entry = surface_find(surfaces, SURFACE_TRANSPARENCY) ;
  if ( entry != NULL ) {
    const transparency_surface_t *trans =
      (const transparency_surface_t *)((const char *)entry -
                                       offsetof(transparency_surface_t, base)) ;
    VERIFY_OBJECT(trans, TRANSPARENCY_SURFACE_NAME) ;
    return trans ;
  }
  return NULL ;
}

uint32 surfaces_image_depth(/*@notnull@*/ const surface_set_t *surfaces,
                            Bool ingroup)
{
  const surface_t *entry = surface_find(surfaces,
                                        ingroup ? SURFACE_TRANSPARENCY : SURFACE_OUTPUT) ;

  if ( entry != NULL )
    return entry->image_depth ;

  /* Not sure if this really should be an assert; we've got a viable
     fall-back position. */
  HQFAIL("No surface found for image depth query") ;

  return 0 ;
}

/** The well-known invalid surface. */
surface_t invalid_surface = SURFACE_INIT ;

static surface_prepare_t render_prepare_invalid(surface_handle_t handle,
                                                render_info_t *p_ri)
{
  UNUSED_PARAM(surface_handle_t, handle) ;
  UNUSED_PARAM(render_info_t *, p_ri) ;

  HQFAIL("Render prepare called for invalid surface") ;

  return SURFACE_PREPARE_OK ;
}

static void init_C_globals_surface(void)
{
  invalid_surface.prepare = render_prepare_invalid ;
  invalid_surface.areafill = invalid_area ;

  surface_set_list = NULL ;
}

/** Compound runtime initialisation */
void surface_C_globals(core_init_fns *fns)
{
  UNUSED_PARAM(core_init_fns*, fns) ;
  init_C_globals_surface() ;
}

/* Log stripped */
