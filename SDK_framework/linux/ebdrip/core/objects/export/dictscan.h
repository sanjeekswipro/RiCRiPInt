/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!export:dictscan.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Automatic dictionary matching.
 */

#ifndef __DICTSCAN_H__
#define __DICTSCAN_H__

#include "objectt.h" /* Only need incomplete type of objects */

/* Type declaration, 4 words long with up to 9 allowable types. Type is like
   this, so that we can easily statically allocate valid objects of this
   type. The match[] array contains types (such as ONULL, OARRAY), or'ed with
   the access permissions required to the object (e.g. CANREAD, CANWRITE.)
   For optional lookups, or in the top bit, OOPTIONAL, so that if lookup
   fails, we just ignore the error (and set the result field to be a null
   pointer.) */

typedef struct {
  uint16 name ;           /* 32767 means end of array, top bit means optional */
  uint8 count ;           /* The number of types present */
  uint8 match[ 9 ] ;      /* If 9 isn't enough(!), set count to 0,
                             and do it manually. */

  /*@null@*/ /*@dependent@*/
  OBJECT *result ;        /* The result of the lookup is placed here */
} NAMETYPEMATCH ;

/* possible access macros for above */
#define OOPTIONAL               (0x8000)
#define END_MATCH_MARKER        0x7FFF
#define DUMMY_END_MATCH         {END_MATCH_MARKER}
#define theIMName( val )       	(& system_names[ (int32)((val)->name) & 0x7FFF ])
#define theIOptional( val )     ( (int32)((val)->name) & OOPTIONAL)
#define theIDictResult( val )	  ((val)->result)
#define theDictResult( val )	  ((val).result)
#define theIMCount( val )	      ((val)->count)
#define theIMatch( val)		      ((val)->match)
#define theISomeLeft( val )	    ( (int32)((val)->name) != END_MATCH_MARKER)

Bool dictmatch(
  /*@notnull@*/ /*@in@*/        OBJECT *dict ,
  /*@notnull@*/ /*@in@*/        NAMETYPEMATCH match_objects[]) ;

/*  
Log stripped */

#endif /* protection for multiple inclusion */
