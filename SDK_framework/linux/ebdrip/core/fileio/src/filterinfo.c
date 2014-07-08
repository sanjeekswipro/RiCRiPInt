/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:filterinfo.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for image filters to provide filter info for image
 * context operations.
 */


#include "core.h"
#include "swerrors.h"
#include "hqmemcmp.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "namedef_.h"

#include "fileio.h"
#include "filterinfo.h"

/*---------------------------------------------------------------------------*/
/* Helper functions for setting image filter decode values. */

imagefilter_match_t *filter_info_match(OBJECT *key,
                                       imagefilter_match_t *list)
{
  uint8 *keyval ;
  uint32 keylen ;

  HQASSERT(key, "No key for image context") ;

  switch ( oType(*key) ) {
  case OSTRING:
    keyval = oString(*key) ;
    keylen = theLen(*key) ;
    break ;
  case ONAME:
    keyval = theICList(oName(*key)) ;
    keylen = theINLen(oName(*key)) ;
    break ;
  default:
    HQFAIL("Image context key is not a string or a name") ;
    return NULL ;
  }

  for ( ; list != NULL ; list = list->next ) {
    uint8 *listval ;
    uint32 listlen ;

    /* Quick test for object equivalence, before extracting value. */
    if ( OBJECTS_IDENTICAL(list->key, *key) )
      return list ;

    switch ( oType(list->key) ) {
    case OSTRING:
      listval = oString(list->key) ;
      listlen = theLen(list->key) ;
      break ;
    case ONAME:
      listval = theICList(oName(list->key)) ;
      listlen = theINLen(oName(list->key)) ;
      break ;
    default:
      HQFAIL("Image context match key is not a string or a name") ;
      return NULL ;
    }

    if ( keylen == listlen && HqMemCmp(keyval, keylen, listval, listlen) == 0 )
      return list ;
  }

  return NULL ;
}

/* Call image decode callback for filter tags. Note that the return value IS
   NOT the normal success/failure value; this routine returns TRUE if the
   calling routine should exit, but the return value should be the value of
   the done parameter to indicate success/failure. */
Bool filter_info_callback(imagefilter_match_t *match,
                          int32 namenum, OBJECT *value, Bool *done)
{
  OBJECT key = OBJECT_NOTVM_NOTHING ;

  HQASSERT(namenum >= 0 && namenum < NAMES_COUNTED, "Invalid decode info name") ;
  HQASSERT(value, "No value for decode info callback") ;
  HQASSERT(done, "No return flag for decode info callback") ;

  object_store_name(&key, namenum, LITERAL) ;

  if ( (match = filter_info_match(&key, match)) != NULL &&
       match->callback != NULL ) {
    HQASSERT(!*done, "Expected filter decode done flag to be initialised to false") ;
    switch ( (*match->callback)(match, value) ) {
    case IMAGEFILTER_MATCH_DONE:
      *done = TRUE ; /* Return early, but with a success value. */
      /*@fallthrough@*/
    case IMAGEFILTER_MATCH_FAIL:
      return TRUE ;
    default:
      HQFAIL("Unexpected return value from image decode callback") ;
      return TRUE ;
    case IMAGEFILTER_MATCH_MORE:
      break ;
    }
  }

  return FALSE ; /* Not a failure, just not an early return */
}

Bool filter_info_ImageMatrix(imagefilter_match_t *match,
                             SYSTEMVALUE m00, SYSTEMVALUE m01,
                             SYSTEMVALUE m10, SYSTEMVALUE m11,
                             SYSTEMVALUE m20, SYSTEMVALUE m21,
                             Bool *done)
{
  OBJECT name = OBJECT_NOTVM_NAME(NAME_ImageMatrix, LITERAL) ;

  if ( (match = filter_info_match(&name, match)) != NULL &&
       match->callback != NULL ) {
    OBJECT matrix[6] = {
      OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
      OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
      OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING
    } ;
    OBJECT omatrix = OBJECT_NOTVM_NOTHING ;

    HQASSERT(!*done, "Expected filter decode done flag to be initialised to false") ;

    object_store_numeric(&matrix[0], m00) ;
    object_store_numeric(&matrix[1], m01) ;
    object_store_numeric(&matrix[2], m10) ;
    object_store_numeric(&matrix[3], m11) ;
    object_store_numeric(&matrix[4], m20) ;
    object_store_numeric(&matrix[5], m21) ;

    theTags(omatrix) = OARRAY | LITERAL | UNLIMITED ;
    theLen(omatrix) = 6 ;
    oArray(omatrix) = &matrix[0] ;

    switch ( (*match->callback)(match, &omatrix) ) {
    case IMAGEFILTER_MATCH_DONE:
      *done = TRUE ; /* Return early, but with a success value. */
      /*@fallthrough@*/
    case IMAGEFILTER_MATCH_FAIL:
      return TRUE ;
    default:
      HQFAIL("Unexpected return value from image decode callback") ;
      return TRUE ;
    case IMAGEFILTER_MATCH_MORE:
      break ;
    }
  }

  return FALSE ; /* Not a failure, just not an early return */
}


Bool filter_info_Decode(imagefilter_match_t *match,
                        uint32 ncomps, USERVALUE d0, USERVALUE d1,
                        Bool *done)
{
  OBJECT name = OBJECT_NOTVM_NAME(NAME_Decode, LITERAL) ;
  Bool result = FALSE ;

  HQASSERT(ncomps > 0, "No components for filter info decode") ;

  if ( (match = filter_info_match(&name, match)) != NULL &&
       match->callback != NULL ) {
    OBJECT decode[8], odecode = OBJECT_NOTVM_NOTHING ;

    HQASSERT(!*done, "Expected filter decode done flag to be initialised to false") ;

    ncomps <<= 1 ; /* We're dealing with decode pairs */

    theTags(odecode) = OARRAY | LITERAL | UNLIMITED ;
    theLen(odecode) = CAST_UNSIGNED_TO_UINT16(ncomps) ;
    oArray(odecode) = &decode[0] ;

    if ( theLen(odecode) > NUM_ARRAY_ITEMS(decode) &&
         (oArray(odecode) = mm_alloc(mm_pool_temp,
                                     sizeof(OBJECT) * theLen(odecode),
                                     MM_ALLOC_CLASS_IMAGE_CONTEXT)) == NULL ) {
      *done = error_handler(VMERROR) ;
      return TRUE ; /* Early return with failure */
    }

    while ( ncomps > 0 ) {
      object_store_real(object_slot_notvm(&oArray(odecode)[--ncomps]), d1) ;
      object_store_real(object_slot_notvm(&oArray(odecode)[--ncomps]), d0) ;
    }

    switch ( (*match->callback)(match, &odecode) ) {
    case IMAGEFILTER_MATCH_DONE:
      *done = TRUE ; /* Return early, but with a success value. */
      /*@fallthrough@*/
    case IMAGEFILTER_MATCH_FAIL:
      result = TRUE ;
      break ;
    default:
      HQFAIL("Unexpected return value from image decode callback") ;
      result = TRUE ;
      /*@fallthrough@*/
    case IMAGEFILTER_MATCH_MORE:
      break ;
    }

    if ( oArray(odecode) != &decode[0] ) {
      mm_free(mm_pool_temp,
              oArray(odecode),
              sizeof(OBJECT) * theLen(odecode)) ;
    }
  }

  return result ;
}

Bool filter_info_Lab_CSD(imagefilter_match_t *match,
                         USERVALUE wp_x, USERVALUE wp_y, USERVALUE wp_z,
                         int32 amin, int32 amax, int32 bmin, int32 bmax,
                         Bool *done)
{
#define CSDICT_LENGTH 2
  OBJECT nameobj = OBJECT_NOTVM_NOTHING;
  OBJECT value = OBJECT_NOTVM_NOTHING;
  OBJECT colorspace[2] = {
    OBJECT_NOTVM_NAME(NAME_Lab, LITERAL), OBJECT_NOTVM_NOTHING,
  } ;
  OBJECT whitepoint[3] = {
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
  } ;
  OBJECT labdict[NDICTOBJECTS(CSDICT_LENGTH)] ;
  /* decodearray[2-5] are also used for Range in the colorspace
     definition. */
  OBJECT decodearray[6] = {
    OBJECT_NOTVM_INTEGER(0), OBJECT_NOTVM_INTEGER(100),
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
  } ;
  Bool ok ;

  object_store_integer(&decodearray[2], amin) ;
  object_store_integer(&decodearray[3], amax) ;
  object_store_integer(&decodearray[4], bmin) ;
  object_store_integer(&decodearray[5], bmax) ;

  theTags(value) = OARRAY | LITERAL | READ_ONLY ;
  theLen(value) = 6 ; /* local array of length 6 */
  oArray(value) = decodearray ;

  if ( filter_info_callback(match, NAME_Decode, &value, done))
    return TRUE ; /* Propagate done value back to caller. */

  /* colorspace[0] set by initialiser */
  init_dictionary(&colorspace[1], CSDICT_LENGTH, UNLIMITED,
                  labdict, ISNOTVMDICTMARK(SAVEMASK)) ;

  object_store_real(&whitepoint[0], wp_x) ;
  object_store_real(&whitepoint[1], wp_y) ;
  object_store_real(&whitepoint[2], wp_z) ;

  theTags(value) = OARRAY | LITERAL | READ_ONLY ;
  theLen(value) = 3 ; /* local array of length 3 */
  oArray(value) = whitepoint ;

  object_store_name(&nameobj, NAME_WhitePoint, LITERAL) ;
  ok = insert_hash_with_alloc(&colorspace[1], &nameobj, &value,
                              INSERT_HASH_NORMAL, no_dict_extension, NULL) ;
  HQASSERT(ok, "Failed to insert WhitePoint") ;

  /* Re-use tail part of decode array for Range */
  theTags(value) = OARRAY | LITERAL | UNLIMITED ;  /* Prepare temp to hold an array */
  theLen(value) = 4 ; /* local array of length 4 */
  oArray(value) = &decodearray[2] ;

  object_store_name(&nameobj, NAME_Range, LITERAL) ;
  ok = insert_hash_with_alloc(&colorspace[1], &nameobj, &value,
                              INSERT_HASH_NORMAL, no_dict_extension, NULL) ;
  HQASSERT(ok, "Failed to insert Range") ;

  theTags(value) = OARRAY | LITERAL | UNLIMITED ;  /* Prepare temp to hold an array */
  theLen(value) = 2 ; /* local array of length 2 */
  oArray(value) = colorspace ;

  return filter_info_callback(match, NAME_ColorSpace, &value, done) ;
}

/*
Log stripped */
