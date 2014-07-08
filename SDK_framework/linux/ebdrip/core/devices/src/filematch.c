/** \file
 * \ingroup devices
 *
 * $HopeName: COREdevices!src:filematch.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Pattern matching utilities for devices
 */

#include "core.h"
#include "swdevice.h"

/* ----------------------------------------------------------------------------
   function:            SwPatternMatch    suspect author:      Luke Tumner
   creation date:       16-Jan-1992       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
int32 RIPCALL SwPatternMatch( uint8 *pattern, uint8 *string )
{
  while ( *string && *pattern ) {
    if ( (int32)*pattern == (int32)'?' ) { /* match any character */
      pattern += 1 ;
      string += 1 ;
    } else if ( (int32)*pattern == (int32)'*' ) {
      /* see if the rest of the pattern matches any trailing substring
       * of the string. */
      pattern += 1 ;
      if ( (int32)*pattern == 0 ) 
        return TRUE ; /* trailing * must match rest */
      while ( *string ) {
        if ( SwPatternMatch( pattern , string ))
          return TRUE ;
        string++ ;
      }
      return FALSE ;
    } else {
      if ( (int32)*pattern == (int32)'\\' )
        pattern++;   /* has strange, but harmless effects if the last
                        character is a '\\' */
      if ( (int32)*pattern++ != (int32)*string++ )
        return FALSE ;
    }
  }
  while ( (int32)*pattern == (int32)'*' )
    pattern++ ;
        
  return ( (int32)*pattern == 0 && (int32)*string == 0 ) ;
}



/* ----------------------------------------------------------------------------
   function:            SwLengthPatternMatch   author:              ###########
   creation date:       16-Jan-1992            last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
int32 RIPCALL SwLengthPatternMatch( uint8 *pattern, int32 plen, uint8 *string, int32 slen )
{
  while ( plen && slen ) {
    if ( (int32)*pattern == (int32)'?' ) { /* match any character */
      if ( slen > 1 && (int32)*string == (int32)'\\' ) {
        string += 1 ; slen-- ;
          }
      pattern += 1 ; plen-- ;
      string += 1 ; slen-- ;
    } else if ( (int32)*pattern == (int32)'*' ) {
      /* see if the rest of the pattern matches any trailing substring
       * of the string. */
      pattern += 1 ;
      plen-- ;
      if ( plen == 0 ) 
        return TRUE ; /* trailing * must match rest */
      while ( slen ) {
        if ( SwLengthPatternMatch( pattern , plen , string , slen ))
          return TRUE ;
        string++ ;
        slen-- ;
      }
      return FALSE ;
    } else {
      if ( slen > 1 && (int32)*string == (int32)'\\' ) {
        string++; slen-- ;
      }
      if ( plen > 1 && (int32)*pattern == (int32)'\\' ) {
        pattern++; 
        plen-- ; /* has strange, but harmless effects if the last
                  * character is a '\\' */
      }
      if ( (int32)*pattern != (int32)*string )
        return FALSE ;
      pattern++ ; 
      plen-- ;
      string++ ;
      slen-- ;
    }
  }
  while ( plen && (int32)*pattern == (int32) '*' ) {
        pattern++ ; 
        plen-- ;
  }
  
  return ( plen == 0 && slen == 0 ) ;
}


/* Log stripped */
