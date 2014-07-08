/** \file
 * \ingroup tiffcore
 *
 * $HopeName: tiffcore!src:tiffmem.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Tiff memory control routines
 */

#include "core.h"
#include "swerrors.h" /* UNDEFINED */
#include "dictinit.h"
#include "objects.h"

#include "tiffmem.h"

/* Context structure for dictionary creation */
struct TIFF_DICT_ALLOC_PARAMS {
  mm_pool_t alloc_pool;
  uint32    alloc_class;
};

/*
 * tiff_alloc_psarray()
 */
Bool tiff_alloc_psarray(
  mm_pool_t   mm_pool,        /* I */
  uint32      size,           /* I */
  OBJECT*     oarray)         /* O */
{
  uint32  i;
  OBJECT* oelements;

  HQASSERT((size > 0),
           "tiff_alloc_psarray: creating array with no elements");
  HQASSERT((oarray != NULL),
           "tiff_alloc_psarray: NULL pointer to array object");

  oelements = mm_alloc(mm_pool, size * sizeof(OBJECT),
                       MM_ALLOC_CLASS_TIFF_PSOBJECT) ;
  if ( oelements == NULL ) {
    return error_handler(VMERROR);
  }

  theTags(*oarray) = OARRAY|LITERAL|UNLIMITED;
  theLen(*oarray) = CAST_UNSIGNED_TO_UINT16(size);
  oArray(*oarray) = oelements;
  SETGLOBJECTTO(*oarray, FALSE) ;

  i = 0;
  do {
    OBJECT_SET_D0(oelements[i],OBJECT_GET_D0(onull)) ; /* Set slot properties */
    OBJECT_SCRIBBLE_D1(oelements[i]);
  } while ( ++i < size );

  return TRUE ;
} /* Function tiff_alloc_psarray */


/*
 * tiff_free_psarray()
 * Note - this is not recursive!
 */
void tiff_free_psarray(
  mm_pool_t     mm_pool,      /* I */
  OBJECT*       oarray)       /* O */
{
  uint32  size;
  OBJECT* oelements;

  HQASSERT((oarray != NULL),
           "tiff_free_psarray: NULL array object pointer");

  size = theLen(*oarray);
  if ( size > 0 ) {
    /* Array has something in it - free them off */
    oelements = oArray(*oarray);
    mm_free(mm_pool, oelements, size*sizeof(OBJECT));

    /* Make array zero length */
    theLen(*oarray) = 0;
    oArray(*oarray) = NULL;
  }
} /* Function tiff_free_psarray */


static
void* tiff_dict_alloc(
  size_t  size,
  uintptr_t data)
{
  struct TIFF_DICT_ALLOC_PARAMS *alloc_params =
    (struct TIFF_DICT_ALLOC_PARAMS *)data;

  HQASSERT((alloc_params != NULL), "Null allocation params pointer");

  return (mm_alloc(alloc_params->alloc_pool, size, alloc_params->alloc_class));
}

/*
 * tiff_alloc_psdict()
 */
Bool tiff_alloc_psdict(
  mm_pool_t   mm_pool,      /* I */
  uint32      size,         /* I */
  OBJECT*     odict)        /* O */
{
  struct TIFF_DICT_ALLOC_PARAMS alloc_params;
  struct DICT_ALLOCATOR dict_alloc;

  HQASSERT((size > 0),
           "tiff_alloc_psdict: creating empty dictionary");
  HQASSERT((odict != NULL),
           "tiff_alloc_psdict: NULL dict pointer");

  alloc_params.alloc_pool = mm_pool;
  alloc_params.alloc_class = MM_ALLOC_CLASS_TIFF_PSOBJECT;
  dict_alloc.alloc_mem = tiff_dict_alloc;
  dict_alloc.data = (uintptr_t)&alloc_params;

  return (dict_create(odict, &dict_alloc, size, ISNOTVMDICTMARK(SAVEMASK)));

} /* Function tiff_alloc_psdict */


/*
 * tiff_free_psdict()
 */
void tiff_free_psdict(
  mm_pool_t   mm_pool,      /* I */
  OBJECT*     odict)        /* O */
{
  uint32  size;
  OBJECT* oentries;

  HQASSERT((odict != NULL),
           "tiff_free_psdict: NULL dict object pointer");
  HQASSERT(oType(*odict) == ODICTIONARY, "Not a dictionary");

  size = theLen(*odict);
  oentries = oDict(*odict) - 2;
  HQASSERT(oType(oentries[0]) == OINTEGER, "Dictionary insertion length corrupted") ;
  HQASSERT(oInteger(oentries[0]) == 0, "Destroying dictionary on dict stack") ;
  HQASSERT(oType(oentries[1]) == ONOTHING, "TIFF dictionary has been extended") ;

  mm_free(mm_pool, oentries, NDICTOBJECTS(size)*sizeof(OBJECT));

  /* Make the object into a null */
  theTags(*odict) = ONULL;
  oDict(*odict) = NULL;
  theLen(*odict) = 0;

} /* Function tiff_free_psdict */

Bool tiff_insert_hash(register OBJECT *thed,
                      int32 namenum,
                      OBJECT *theo)
{
  OBJECT key = OBJECT_NOTVM_NOTHING ;
  object_store_name(&key, namenum, LITERAL) ;
  return insert_hash_with_alloc(thed, &key, theo, INSERT_HASH_NORMAL,
                                no_dict_extension, NULL) ;
}

Bool tiff_alloc_icc_profile(mm_pool_t pool, OBJECT *icc_profile, size_t len)
{
  OBJECT *array ;

  /* Convert length in bytes to number of strings required. */
  len = (len + MAXPSSTRING - 1) / MAXPSSTRING ;

  array = mm_alloc(pool, sizeof(OBJECT) * len, MM_ALLOC_CLASS_IMAGE_CONTEXT);;
  if (array == NULL)
    return error_handler(VMERROR) ;

  theTags(*icc_profile) = OARRAY | LITERAL | UNLIMITED ;
  theLen(*icc_profile) = CAST_TO_UINT16(len) ; /* local array length */
  oArray(*icc_profile) = array ;
  SETGLOBJECTTO(*icc_profile, FALSE) ;

  /* in case we need to free memory halfway through processing profile */
  for ( ; len > 0; --len ) {
    *array++ = onothing ; /* Struct copy to set slot properties */
  }

  return TRUE ;
}

void tiff_free_icc_profile(mm_pool_t pool, OBJECT *icc_profile)
{
  if ( oType(*icc_profile) == OARRAY || oType(*icc_profile) == OPACKEDARRAY ) {
    uint32 len = theLen(*icc_profile);
    OBJECT *entry = oArray(*icc_profile) ;
    OBJECT *limit ;

    for ( limit = entry + len ; entry < limit ; ++entry ) {
      if (oType(*entry) == OSTRING) {
        mm_free(pool, oString(*entry), theLen(*entry));
        oArray(*entry) = NULL ;
        theLen(*entry) = 0 ;
        theTags(*entry) = ONOTHING;
      }
    }
    mm_free(pool, oArray(*icc_profile), sizeof(OBJECT)*len);
    oArray(*icc_profile) = NULL ;
    theLen(*icc_profile) = 0 ;
    theTags(*icc_profile) = ONOTHING;
  }
}

/* Log stripped */
