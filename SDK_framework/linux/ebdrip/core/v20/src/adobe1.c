/** \file
 * \ingroup fonts
 *
 * $HopeName: SWv20!src:adobe1.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font xxRun PS operators to decrypt various types of charstrings
 *
 * \li CCRun  for Type 4 fonts, implementing Adobe Type 1 charstrings
 * \li MPSRun for Morisawa Type 4 fonts
 * \li eCCRun for ATL encrypted Type 4 fonts
 */

#include "core.h"
#include "swstart.h"
#include "swerrors.h"
#include "objects.h"
#include "devices.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "monitor.h"
#include "dictscan.h"
#include "fonts.h"
#include "pscontext.h"
#include "namedef_.h"

#include "graphics.h"
#include "gstack.h"
#include "adobe1.h"
#include "fcache.h"
#include "stacks.h"
#include "miscops.h"
#include "showops.h"

/* ----------------------------------------------------------------------------
   function:            CCRun_(..)         author:              Andrew Cave
   creation date:       06-Oct-1987        last modification:   ##-###-####
   arguments:           type .
   description:

   Decodes an Adobe Type 4 font.

---------------------------------------------------------------------------- */
Bool CCRun_(ps_context_t *pscontext)
{
  corecontext_t *corecontext = ps_core_context(pscontext);
  Bool result ;
  OBJECT *o1 , *o2 , *o3 ;
  charcontext_t *charcontext = corecontext->charcontext ;

  HQASSERT(charcontext, "No character context") ;

  if ( charcontext == NULL ||
       !charcontext->buildthischar )
    return error_handler( UNDEFINED ) ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex(2, &operandstack) ;
  o2 = stackindex(1, &operandstack) ;
  o3 = theTop( operandstack ) ;

  if ( oType(*o1) != ODICTIONARY ||
       oType(*o2) != OINTEGER ||
       oType(*o3) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*oDict(*o1)) && !object_access_override(oDict(*o1))) ||
       (!oCanRead(*o3) && !object_access_override(o3)) )
    return error_handler( INVALIDACCESS ) ;

  if ( ! push( o3 , &temporarystack ))
    return FALSE ;

  npop( 3 , &operandstack ) ;

  /* we have discovered that NIS fonts poke (using superexec) the Private
     dictionary of the font while executing the BuildChar. This invalidates the
     lenIV we cached before entering the BuildChar so CCRun can't interpret
     its strings properly. No doubt we will find some fonts change lots
     of other things in the future, so go get all the entries again */
  if (!set_font())
    return FALSE;

#ifdef TEST_DECRYPTFONT
  { int32 slen ;
    uint8 *slist ;
    int32 xorchar = 0x00 ;
    OBJECT *orginal_string = theTop( temporarystack ) ;

    strategy = 1 + (( get_rtime() / 1000 ) & 1 ) ;

    monitorf("strategy: %d\n",strategy);

    slen = theLen(* orginal_string ) ;
    slist = oString(*orginal_string) ;
    while ((--slen) >= 0 ) {
      slist[ 0 ] ^= xorchar ;
      xorchar += slist[ 0 ] ;
      if ( strategy == 2 )
        xorchar += 0x10 ;
      slist++ ;
    }
  }
#endif /* TEST_DECRYPTFONT */

  HQASSERT(theFontType(theFontInfo(*gstateptr)) == FONTTYPE_4,
           "CCRun_ not called in Type 4 font") ;

  result = adobe_cache_type4(corecontext, charcontext,
                             &theFontInfo(*gstateptr),
                             theTop(temporarystack),
                             PROTECTED_NONE, CurrentPoint ) ;

  pop( &temporarystack ) ;

  return result ;
}

/* ----------------------------------------------------------------------------
   function:            MPSRun_(..)        author:              Angus Duggan
   creation date:       18 Jul 1994        last modification:   ##-###-####
   arguments:           type .
   description:

   Decodes a Morisawa encrypted charstring

---------------------------------------------------------------------------- */
Bool MPSRun_(ps_context_t *pscontext)
{
  corecontext_t *corecontext = ps_core_context(pscontext);
  Bool result ;
  OBJECT *o1 , *o2 , *o3 ;
  charcontext_t *charcontext = corecontext->charcontext ;

  if ( charcontext == NULL ||
       !charcontext->buildthischar )
    return error_handler( UNDEFINED ) ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex(2, &operandstack) ;
  o2 = stackindex(1, &operandstack) ;
  o3 = theTop( operandstack ) ;

  if ( oType(*o1) != ODICTIONARY ||
       oType(*o2) != OINTEGER ||
       oType(*o3) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*oDict(*o1)) && !object_access_override(oDict(*o1))) ||
       (!oCanRead(*o3) && !object_access_override(o3)) )
    return error_handler( INVALIDACCESS ) ;

  if ( !push(o3, &temporarystack) )
    return FALSE ;

  npop( 3 , &operandstack ) ;

  HQASSERT(theFontType(theFontInfo(*gstateptr)) == FONTTYPE_4,
           "MPSRun_ not called in Type 4 font") ;

  result = adobe_cache_type4(corecontext, charcontext,
                             &theFontInfo(*gstateptr),
                             theTop(temporarystack),
                             PROTECTED_MRSWA, CurrentPoint ) ;

  pop( &temporarystack ) ;

  return result ;
}

/* ----------------------------------------------------------------------------
   function:            ATLRun_(..)        author:              Jon Wilson
   creation date:       12 Jul 2000        last modification:   ##-###-####
   arguments:
   description:

   Decodes an ATL encrypted charstring (eCCRun)

---------------------------------------------------------------------------- */
Bool eCCRun_(ps_context_t *pscontext)
{
  corecontext_t *corecontext = ps_core_context(pscontext);
  Bool result ;
  OBJECT *o1 , *o2 , *o3 ;
  charcontext_t *charcontext = corecontext->charcontext ;

  if ( charcontext == NULL ||
       !charcontext->buildthischar )
    return error_handler( UNDEFINED ) ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex(2, &operandstack) ;
  o2 = stackindex(1, &operandstack) ;
  o3 = theTop( operandstack ) ;

  if ( oType(*o1) != ODICTIONARY ||
       oType(*o2) != OINTEGER ||
       oType(*o3) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*oDict(*o1)) && !object_access_override(oDict(*o1))) ||
       (!oCanRead(*o3) && !object_access_override(o3)) )
    return error_handler( INVALIDACCESS ) ;

  if ( !push(o3, &temporarystack) )
    return FALSE ;

  npop( 3 , &operandstack ) ;

  result = adobe_cache_type4(corecontext, charcontext,
                             &theFontInfo(*gstateptr),
                             theTop(temporarystack),
                             PROTECTED_ATL, CurrentPoint ) ;

  pop( &temporarystack ) ;

  return result ;
}

/*
Log stripped */
