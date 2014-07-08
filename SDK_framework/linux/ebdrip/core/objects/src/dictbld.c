/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!src:dictbld.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Build a dictionary either from a name match, or from an array of name,object
 * pairs.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dictbld.h"
#include "objimpl.h"
#include "namedef_.h" /* system_names */

/** Populate the dictionary from the given build objects. */
Bool dictbuild( OBJECT *dict ,
                NAMETYPEBUILD build_objects[],
                OBJECT * (*alloc_func)(int32 size, void * params),
                void * alloc_params )
{
  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;
  HQASSERT( dict , "dict NULL in dictbuild." ) ;
  HQASSERT( oType(*dict) == ODICTIONARY ,
            "dict not a dictionary in dictbuild." ) ;
  HQASSERT( build_objects , "build_objects NULL in dictbuild." ) ;

  while ( build_objects->name != DUMMY_END_NAME ) {
    if ( oType(build_objects->value) != ONULL ) {
      oName(nnewobj) = system_names + build_objects->name ;

      HQASSERT(( build_objects->name & OOPTIONAL ) == 0 ,
               "OOPTIONAL flag not allowed in dictbuild." ) ;

      if ( !insert_hash_with_alloc( dict ,
                                    &nnewobj ,
                                    &build_objects->value,
                                    INSERT_HASH_NAMED,
                                    alloc_func,
                                    alloc_params))
        return FALSE ;
    }

    ++build_objects ;
  }

  return TRUE ;
}

/** Sometimes it's useful to do a dictmatch, followed perhaps by some processing
 * of the results, followed by a dictbuild. An example of this is the PDF world
 * where we need to decant parameters from a PS dict, check/munge them and then
 * store them in a PDF dict, outside of PSVM.
 */
Bool dictmatchbuild( OBJECT *dict ,
                     NAMETYPEMATCH build_objects[] ,
                     OBJECT * (*alloc_func)(int32 size, void * params),
                     void * alloc_params  )
{
  HQASSERT(object_asserts(), "Object system not initialised or corrupt") ;
  HQASSERT( dict , "dict NULL in dictmatchbuild." ) ;
  HQASSERT( oType(*dict) == ODICTIONARY ,
            "dict not a dictionary in dictmatchbuild." ) ;
  HQASSERT( build_objects , "build_objects NULL in dictmatchbuild." ) ;

  while ( build_objects->name != DUMMY_END_NAME ) {
    if ( build_objects->result ) {
      oName(nnewobj) = system_names + ( build_objects->name & ~OOPTIONAL ) ;

      if ( !insert_hash_with_alloc( dict ,
                                    &nnewobj ,
                                    build_objects->result ,
                                    INSERT_HASH_NAMED,
                                    alloc_func,
                                    alloc_params))
        return FALSE ;
    }

    ++build_objects ;
  }

  return TRUE ;
}

/* Log stripped */
