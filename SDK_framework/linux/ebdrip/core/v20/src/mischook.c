/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:mischook.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Preflight hook controls
 */

#include "core.h"
#include "swerrors.h"

#include "objects.h"
#include "fileio.h"
#include "dicthash.h"
#include "namedef_.h"
#include "stacks.h"

#include "params.h"
#include "dictops.h"
#include "miscops.h"

static Bool check_one_value(OBJECT *thekey, OBJECT *theo, USERVALUE *ptr)
{
  if ( !object_get_real(theo, ptr) )
    return errorinfo_error_handler(TYPECHECK, thekey, theo) ;

  if ( *ptr < 0.0 && *ptr != -1.0f )
    return errorinfo_error_handler(RANGECHECK, thekey, theo) ;

  return TRUE ;
}

static Bool checkonemischook(OBJECT *thekey, OBJECT *theo, void *argsBlockPtr)
{
  USERVALUE temp ;

  UNUSED_PARAM(void *, argsBlockPtr);

  if ( oType(*thekey) != ONAME )
    return errorinfo_error_handler(UNDEFINEDRESULT, thekey, theo);

  /* check the name is in the supported list */
  switch ( theINameNumber(oName(*thekey)) ) {
  case NAME_ImageRGB:
  case NAME_CompressJPEG:
  case NAME_ImageLowRes:
  case NAME_FontBitmap:
  case NAME_SecurityChecked:
    /* check theo is an executable object of a suitable type */
    switch ( oType(*theo) ) {
    case OARRAY :
    case OSTRING :
    case OPACKEDARRAY :
    case OFILE :
      if ( ! oExecutable(*theo) )
        return errorinfo_error_handler(INVALIDACCESS, thekey, theo);
      if ( ! oCanExec(*theo) && !object_access_override(theo) )
        return errorinfo_error_handler( INVALIDACCESS, thekey, theo ) ;
      if ( oType(*theo) == OFILE && ! isIInputFile(oFile(*theo)) )
        return errorinfo_error_handler( INVALIDACCESS, thekey, theo ) ;
      break;
    case ONAME :
    case OOPERATOR :
      if ( ! oExecutable(*theo) )
        return errorinfo_error_handler(INVALIDACCESS, thekey, theo);
      break;
    case ONULL:
      /* cancel the effect - doesn't need to be executable */
      break;
    default:
      return errorinfo_error_handler(TYPECHECK, thekey, theo);
    }
    break ;
  case NAME_ImageLowLW :
  case NAME_ImageLowCT :
    /* check theo is a number */
    switch ( oType(*theo) ) {
    case OINTEGER :
    case OREAL :
      if ( ! check_one_value(thekey, theo, & temp) )
        return FALSE ;
      break ;
    default:
      return errorinfo_error_handler(TYPECHECK, thekey, theo);
    }
    break ;
  default:
    return errorinfo_error_handler( UNDEFINED, thekey, theo) ;
  }

  return TRUE;
}


static
Bool makeonemischook(OBJECT *thekey, OBJECT *theo, void *argsBlockPtr)
{
  MISCHOOKPARAMS *mischookparams = argsBlockPtr;

  switch ( theINameNumber(oName(*thekey)) ) {
  case NAME_ImageRGB:
    Copy((& mischookparams->ImageRGB), theo) ;
    break ;
  case NAME_CompressJPEG:
    Copy((& mischookparams->CompressJPEG), theo) ;
    break ;
  case NAME_ImageLowRes:
    Copy((& mischookparams->ImageLowRes), theo) ;
    break ;
  case NAME_ImageLowLW :
    if (! check_one_value(thekey, theo, & mischookparams->ImageLowLW) )
      return FALSE ;
    break ;
  case NAME_ImageLowCT :
    if (! check_one_value(thekey, theo, & mischookparams->ImageLowCT) )
      return FALSE ;
    break ;
  case NAME_SecurityChecked:
    Copy((& mischookparams->SecurityChecked), theo) ;
    break ;
  case NAME_FontBitmap:
    Copy((&mischookparams->FontBitmap), theo);
    break;
  }

  return TRUE ;
}


Bool setmischooks_(ps_context_t *pscontext)
{
  register OBJECT *theo ;

  /* takes a dictionary. For each element:
     if the name is present in conditionprocs in internaldict
       if value is null
         restore the status quo
       else replace it with the value given
     if not, add it
  */

   if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop(operandstack);

  if ( oType(*theo) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead(*oDict(*theo)) )
    if ( !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;

  if (! walk_dictionary(theo, checkonemischook, NULL))
    return FALSE;

  if (! walk_dictionary(theo, makeonemischook,
                        ps_core_context(pscontext)->mischookparams))
    return FALSE;

  pop (& operandstack);
  return TRUE;
}

Bool currentmischooks_(ps_context_t *pscontext)
{
  MISCHOOKPARAMS *mischookparams = ps_core_context(pscontext)->mischookparams;
  OBJECT thed = OBJECT_NOTVM_NOTHING ;

  if ( ! ps_dictionary(&thed, MISCHOOKPARAMENTRIES))
    return FALSE ;

  /* ImageLowLW */
  oName(nnewobj) = &system_names[ NAME_ImageLowLW ] ;
  oReal(rnewobj) = (USERVALUE) mischookparams->ImageLowLW ;
  if ( ! insert_hash( &thed , &nnewobj , &rnewobj ))
    return FALSE ;

  /* ImageLowCT */
  oName(nnewobj) = &system_names[ NAME_ImageLowCT ] ;
  oReal(rnewobj) = (USERVALUE) mischookparams->ImageLowCT ;
  if ( ! insert_hash( &thed , &nnewobj , &rnewobj ))
    return FALSE ;

  /* FontBitmap */
  oName(nnewobj) = &system_names[ NAME_FontBitmap ] ;
  if ( ! insert_hash( &thed , &nnewobj , & mischookparams->FontBitmap))
    return FALSE ;

  /* ImageRGB */
  oName(nnewobj) = &system_names[ NAME_ImageRGB ] ;
  if ( ! insert_hash( &thed , &nnewobj , & mischookparams->ImageRGB))
    return FALSE ;

  /* CompressJPEG */
  oName(nnewobj) = &system_names[ NAME_CompressJPEG ] ;
  if ( ! insert_hash( &thed , &nnewobj , & mischookparams->CompressJPEG))
    return FALSE ;

  /* ImageLowRes */
  oName(nnewobj) = &system_names[ NAME_ImageLowRes ] ;
  if ( ! insert_hash( &thed , &nnewobj , & mischookparams->ImageLowRes))
    return FALSE ;

  /* SecurityChecked */
  oName(nnewobj) = &system_names[ NAME_SecurityChecked ] ;
  if ( ! insert_hash( &thed , &nnewobj , & mischookparams->SecurityChecked))
    return FALSE ;

  return push( &thed , &operandstack ) ;
}


/* EOF */

/* Log stripped */
