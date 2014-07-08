/** \file
 * \ingroup hqmem
 *
 * $HopeName: HQNc-standard!src:hqmemcmp.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * String utility functions. Currently only string comparison.
 */

#include "std.h"
#include "hqmemcmp.h"

/* ----------------------------------------------------------------------------
   function:            HqMemCmp(..)       author:              Andrew Cave
   creation date:       05-Oct-1987        last modification:   ##-###-####
   arguments:           s1 , ln1 , s2 , ln2 .
   description:

     This function lexically compares two strings of the given lengths, and
     returns :
       -ve    if s1 < s2       0    if s1 == s2       +ve    if s1 > s2 .

---------------------------------------------------------------------------- */
int32 HQNCALL HqMemCmp(const void *v1, int32 ln1, const void *v2, int32 ln2)
{
  const uint8 *s1 = v1 ;
  const uint8 *s2 = v2 ;
  const uint8 *limit ;

  HQASSERT(ln1 >= 0, "Comparison length should not be negative (1)") ;
  HQASSERT(ln2 >= 0, "Comparison length should not be negative (2)") ;
  HQASSERT(s1 != NULL || ln1 == 0, "No memory to compare (1)") ;
  HQASSERT(s2 != NULL || ln2 == 0, "No memory to compare (2)") ;

  if ( ln1 < ln2 )
    limit = s1 + ln1 ;
  else
    limit = s1 + ln2 ;

  while  ( s1 < limit && *s1 == *s2 ) {
    ++s1 ; ++s2 ;
  }

  if ( s1 < limit )
    return ( *s1 - *s2 ) ;
  else
    return ( ln1 - ln2 ) ;
}

/*
Log stripped */
