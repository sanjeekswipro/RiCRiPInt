/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:filters.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript interface to filters
 */

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swctype.h"
#include "swmemory.h"
#include "objects.h"
#include "dictscan.h"
#include "hqmemcmp.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "devices.h"
#include "namedef_.h"

#include "stacks.h"
#include "params.h"
#include "control.h"
#include "miscops.h"
#include "execops.h"
#include "dictops.h"
#include "progress.h"
#include "chartype.h"

#include "filters.h"


/* ----------------------------------------------------------------------------
   function:            filter_()            author:              Paul Attridge
   creation date:       26-Jun-1991          last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual second edition page 416.

---------------------------------------------------------------------------- */
Bool filter_(ps_context_t *pscontext)
{
  FILELIST *flptr ;
  OBJECT nameo = OBJECT_NOTVM_NOTHING ;
  OBJECT fileo = OBJECT_NOTVM_NOTHING ;
  uint8  *filter_name ;
  int32  name_length ;
  int32  find_error ;
#if defined( ASSERT_BUILD )
  int32  stack_size ;
#endif

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  Copy(&nameo, theTop(operandstack)) ;

  if ( oType(nameo) == ONAME ) {
    filter_name = theICList(oName(nameo)) ;
    name_length = theINLen(oName(nameo)) ;
  } else if ( oType(nameo) == OSTRING ) {
    /* undocumented - but they allow strings as an argument */
    filter_name = oString(nameo) ;
    name_length = theLen(nameo) ;
  } else
    return error_handler( TYPECHECK ) ;

  /* find the filter name in the external or standard filter table */
  flptr = filter_external_find(filter_name, name_length, &find_error, TRUE) ;
  if ( flptr == NULL ) {
    if ( find_error != NOT_AN_ERROR)
      return error_handler( find_error );
    flptr = filter_standard_find(filter_name, name_length) ;
    if ( flptr == NULL )
      return error_handler( UNDEFINED ) ;
  }

  /* sanity check */
  HQASSERT(isIFilter(flptr), "Not a filter") ;

  /* Create our own copy of this filter - reusing a previous (dead) one if
   * possible; otherwise will actually alloc a new one.
   */
  pop(&operandstack) ; /* Don't want name on stack during filter init */

#if defined( ASSERT_BUILD )
  /* The init routine removes the arguments it needs, including the underlying
     source/target from the stack. Check that it actually did remove
     something. */
  stack_size = theStackSize(operandstack) ;
#endif

  if ( ! filter_create_object(flptr, &fileo, NULL, &operandstack) ) {
    (void)push(&nameo, &operandstack) ; /* Restore name for error reporting */
    return FALSE ;
  }

  HQASSERT(theStackSize(operandstack) <= stack_size - 1,
           "Filter init routine should have removed underlying source/target") ;

  return push(&fileo, &operandstack) ;
}

/* get external filter name and device type number from stack and
 * add them to the list of external filters
 * called as defineresource etc on a non-standard filter
*/

Bool externalfilter_(ps_context_t *pscontext)
{
  OBJECT *o1, *o2 ;
  int32 dev_number, name_length ;
  uint8 *filter_name ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  HQTRACE( debug_filters , ( "Welcome to externalfilter" )) ;

  /* externalfilter is always called with two args on the stack, even if
     the second one isn't used */
  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = theTop( operandstack ) ;   /* integer code or dictionary */

  switch ( oType(*o1) ) {
  case OINTEGER:
    dev_number = oInteger(*o1);
    break ;
  case ODICTIONARY:
    o2 = stackindex( 1, &operandstack ) ; /* filter name */
    return filter_external_define( o1 , o2 );  /* device to be added */
  default:
    return error_handler(TYPECHECK) ;
  }

  if ( dev_number == -3 ) {            /* resourceforall */
    pop( &operandstack ) ;
    return filter_external_forall(&operandstack);
  }

  o2 = stackindex(1, &operandstack ) ;         /* filter name */

  switch ( oType(*o2) ) {
  case ONAME:
    filter_name = theICList(oName(*o2)) ;
    name_length = theINLen(oName(*o2)) ;
    break ;
  case OSTRING:
    /* undocumented - but they allow strings as an argument */
    filter_name = oString(*o2) ;
    name_length = theLen(*o2) ;
    break ;
  default:
    return error_handler( TYPECHECK ) ;

  }

  switch ( dev_number ) {
  case -2: /* findresource */
    if ( filter_external_exists(filter_name, name_length) ) {
      Copy(o1, &tnewobj) ;
    } else {
      Copy(o1, &fnewobj) ;
    }
    return TRUE ;
  case -1: /* undefineresource */
    npop(2, &operandstack) ;

    return filter_external_undefine(filter_name, name_length);
  }

  return error_handler(RANGECHECK) ;
}

/* Filter hook procedure, called whenever a new filter is created. This
   performs preflight checks (checking for DCTDecode filter and calling
   mischook). */
Bool ps_filter_preflight(FILELIST *filter)
{
  OBJECT *ocompress_jpeg = &get_core_context_interp()->mischookparams->CompressJPEG;

  HQASSERT(filter, "No filter in hook") ;

  /* part of the EP2000 preflight checks. Must be handled here rather than in
   * DCTDecode specific code in case an external JPEG input plugin is in use.
   */
  if ( oType(*ocompress_jpeg) != ONULL &&
       HqMemCmp(theICList(filter), theINLen(filter),
                NAME_AND_LENGTH("DCTDecode")) == 0 ) {
    if ( ! push( ocompress_jpeg, & executionstack ) )
      return FALSE ;
    if ( ! interpreter( 1 , NULL ))
      return FALSE ;
  }

  return TRUE ;
}

/* Another filter hook procedure, called whenever a new filter is created.
   This runs the PostScript Install procedure for GenericEncode and
   GenericDecode filters. There is no reason running an Install procedure
   could not be extended to other filter types (e.g. Image filters), to allow
   parameter manipulation. */
Bool ps_filter_install(FILELIST *filter, OBJECT *args, STACK *stack)
{
  HQASSERT(filter, "No filter in hook") ;
  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;

  if ( HqMemCmp(theICList(filter), theINLen(filter),
                NAME_AND_LENGTH("GENERICEncode")) == 0 ||
       HqMemCmp(theICList(filter), theINLen(filter),
                NAME_AND_LENGTH("GENERICDecode")) == 0 ) {
    if ( ! args && !isEmpty(*stack) )
      args = theITop(stack) ;

    /* Look up an /Install procedure in the filter's dictionary, and run it. */
    if ( args && oType(*args) == ODICTIONARY ) {
      OBJECT *install ;

      enum {
        filterdictmatch_Install,
        filterdictmatch_dummy
      } ;
      static NAMETYPEMATCH filterdictmatch[filterdictmatch_dummy+1] = {
        { NAME_Install | OOPTIONAL, 1, { OSTRING }},           /* 0 */
        DUMMY_END_MATCH
      } ;

      if ( ! dictmatch(args, filterdictmatch) )
        return FALSE ;

      if ( (install = filterdictmatch[filterdictmatch_Install].result) != NULL ) {
        DEVICELIST *dlist = theIDeviceList(filter);
        DEVICEPARAM param;

        HQASSERT(dlist, "No devicelist for generic filter") ;

        theDevParamName(param) = (uint8*)"Install" ;
        theDevParamNameLen(param) = 7 ;

        if ( (*theIGetParam(dlist))(dlist, &param) == ParamAccepted ) {
          /* We want the filter dictionary on the operandstack during the
             install procedure, so it can modify arguments. We may be called
             in a context in which the underlying filter is not on the stack,
             so the install procedure should not rely on modifying it. */
          if ( stack != &operandstack ) {
            if ( !push(args, &operandstack) )
              return FALSE ;
          }

          if ( !setup_pending_exec(install, TRUE) )
            return FALSE ;

          if ( stack != &operandstack )
            pop(&operandstack) ;
        }
      }
    }
  }

  return TRUE ;
}


/* Log stripped */
