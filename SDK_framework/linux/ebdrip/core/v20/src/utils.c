/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:utils.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS utility functions
 */

#include "core.h"
#include "utils.h"

#include "constant.h"
#include "objects.h"
#include "stacks.h"
#include "control.h"
#include "swerrors.h"
#include "params.h"
#include "psvm.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "swmemory.h"

/* ----------------------------------------------------------------------------
   function:            getIB(..)          author:              Andrew Cave
   creation date:       08-Jul-1991        last modification:   ##-###-####
   arguments:           intptr.
   description:

   Checks for existence of a single boolean argument on the operandstack.
   Returns its value in the integer pointer to by the given argument.

---------------------------------------------------------------------------- */
Bool get1B( int32 *intptr )
{
  register OBJECT *theo ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OBOOLEAN )
    return error_handler( TYPECHECK ) ;

  (*intptr) = oBool(*theo) ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            from_matrix(..)    author:              Andrew Cave
   creation date:       14-Oct-1987        last modification:   ##-###-####
   arguments:           olist , them .
   description:

   This procedure inserts the values of matrix m into the olist.  Note that it
   is  assumed that olist is a matrix (i.e. an array of size 6).

---------------------------------------------------------------------------- */
Bool from_matrix( register OBJECT *olist , OMATRIX *matrix , int32 glmode )
{
  register int32 i ;
  register SYSTEMVALUE temp ;
  corecontext_t *corecontext = get_core_context_interp() ;

/* Check if saved. */
  if ( ! check_asave(olist, 6, glmode, corecontext))
    return FALSE ;

  for ( i = 0 ; i < 6 ; ++i ) {

    temp = matrix->matrix[ i >> 1 ][ i & 1 ] ;

    if ( realrange( temp )) {
      if ( ! realprecision( temp ))
        temp = 0.0 ;
      object_store_real(olist, (USERVALUE)temp) ;
    }
    else
      Copy(olist, &ifnewobj) ;

    ++olist ;
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            is_matrix_internal  author:              Angus Duggan
   creation date:       9-Nov-1994
   arguments:           o , m .
   description:

   This procedure checks that the given object is a matrix - namely an array
   of size 6, containing numbers. These numbers are entered into the matrix.
   It returns the error code of any error, or FALSE if it succeeded.
---------------------------------------------------------------------------- */
static int32 is_matrix_internal( register OBJECT *o , OMATRIX *matrix )
{
  register int32 loop ;
  register SYSTEMVALUE temp ;

  switch ( oType(*o)) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return TYPECHECK ;
  }
  if ( (int32)theLen(*o) != 6 )
    return RANGECHECK ;

  o = oArray(*o) ;
  for ( loop = 0 ; loop < 6 ; ++loop ) {
    /* N.B. Do not use object_get_numeric() because this doesn't call
       error_handler() if the type is wrong. */
    switch ( oType(*o)) {
    case OINTEGER :
      temp = ( SYSTEMVALUE )oInteger(*o) ;
      break ;
    case OREAL :
      temp = ( SYSTEMVALUE )oReal(*o) ;
      break ;
    case OINFINITY :
      temp = ( SYSTEMVALUE )OINFINITY_VALUE ;
      break ;
    default:
      return TYPECHECK ;
    }

    matrix->matrix[ loop >> 1 ][ loop & 1 ] = temp ;
    ++o ;
  }

  MATRIX_SET_OPT_BOTH( matrix ) ;

  HQASSERT( matrix_assert( matrix ) , "result not a proper optimised matrix" ) ;

  return FALSE;
}

/* ----------------------------------------------------------------------------
   function:            is_matrix_noerror  author:              Angus Duggan
   creation date:       9-Nov-1994
   arguments:           o , m .
   description:

   This procedure checks that the given object is a matrix - namely an array
   of size 6, containing numbers. These numbers are entered into the matrix.
   It does not call error_handler if the object is not a matrix.
---------------------------------------------------------------------------- */
Bool is_matrix_noerror( OBJECT *o , OMATRIX *matrix )
{
  return is_matrix_internal( o, matrix ) == FALSE;
}

/* ----------------------------------------------------------------------------
   function:            is_matrix(..)      author:              Andrew Cave
   creation date:       17-Oct-1987        last modification:   ##-###-####
   arguments:           o , m .
   description:

   This procedure checks that the given object is a matrix - namely an array
   of size 6, containing numbers. These numbers are entered into the matrix.
   It calls error_handler if the object is not a matrix.
---------------------------------------------------------------------------- */
Bool is_matrix( OBJECT *o , OMATRIX *matrix )
{
  int32 errcode = is_matrix_internal(o, matrix) ;

  if ( !errcode )
    return TRUE ;

  return error_handler(errcode) ;
}



/* ----------------------------------------------------------------------
   function:       calculateAdler32(..)  author: Paul Gardner
   creation date   23Sep1997
   arguments:      byteptr, count, s1 ,s2
   Generates an adler32 checksum from the given byteptr (length count)
   s1 and s2 are pointers to the seeds which will be modified by the function.
   The result of this function is the adler32 checksum.
   Not sure what this really does as I've just moved it from stream.c
---------------------------------------------------------------------- */

/* For the Adler32 checksum: startlingly similar to that used in flate.c */

#define ADLER32_BASE 65521


int32 calculateAdler32( uint8 *byteptr, int32 count ,
                        uint16 * s1 , uint16 * s2 )
{
  uint16 s1local;
  uint16 s2local;

  HQASSERT( byteptr , "byteptr is null : calculateAdler32");
  HQASSERT( (s1 && s2) , "either s1 or s2 is NULL : calculateAdler32");

  s1local = *s1;
  s2local = *s2;

  while ( count-- ) {
    s1local = ( uint16 )(( s1local + *byteptr++ ) % ADLER32_BASE ) ;
    s2local = ( uint16 )(( s2local + s1local ) % ADLER32_BASE ) ;
  }

  /* update s1 and s2 */
  *s1 = s1local;
  *s2 = s2local;

  /* The Adler-32 checksum is stored as s2 * 65536 + s1. */
  return (( int32 ) s2local << 16 ) + ( int32 ) s1local ;
}



/* Log stripped */
