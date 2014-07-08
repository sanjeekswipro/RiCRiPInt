/** \file
 * \ingroup psloop
 *
 * $HopeName: SWv20!src:execops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS execution operators
 */

#include "core.h"
#include "pscontext.h"
#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "dicthash.h"
#include "fileio.h"
#include "namedef_.h"

#include "images.h"
#include "graphics.h"
#include "routedev.h"
#include "params.h"
#include "psvm.h"
#include "gu_path.h"
#include "gu_ctm.h"
#include "gstate.h"
#include "stacks.h"
#include "fileops.h"
#include "miscops.h"
#include "pathops.h"
#include "startup.h"
#include "system.h"
#include "swmemory.h"
#include "pathcons.h"
#include "control.h"
#include "interrupts.h"
#include "setup.h" /* xsetup_operator */
#include "execops.h"
#include "vndetect.h"

static Bool template_match(register uint8 *template,
                           register int32 template_len,
                           register uint8 *str, register int32 len);
static Bool dostopped(ps_context_t *pscontext, Bool super) ;
static Bool dothestop(ps_context_t *pscontext, Bool super);


Bool setup_pending_exec(OBJECT *theo, Bool exec_immediately)
{
  error_context_t *errcontext = get_core_context_interp()->error;
  int32 old_err = 0;
  Bool result;

  if ( ! oExecutable(*theo) ) {
    return push( theo, &operandstack );
  }

  switch ( oType(*theo) ) {

  case OARRAY :
  case OSTRING :
  case OPACKEDARRAY :
  case OFILE :
    if ( ! oCanExec(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
    if ( oType(*theo) == OFILE ) {
      currfileCache = NULL ;
      /* it is valid to check isIInputFile even if it is a dead filter */
      if ( ! isIInputFile(oFile(*theo)) ) {
        return error_handler( INVALIDACCESS ) ;
      }
    }
    /*FALLTHROUGH*/

  case ONAME :
  case OOPERATOR :
    execStackSizeNotChanged = FALSE ;
    if ( ! push( theo , & executionstack ))
      return FALSE ;
    /* This is what the operator form of exec does; we don't do it,
     * because the object wasn't on the stack to pop it from.
     * pop( & operandstack ) ;
     */
    if ( exec_immediately ) {
      if ( error_signalled_context(errcontext) )
        error_save_context(errcontext, &old_err);
      result = interpreter(1, NULL);
      if ( old_err)
        error_restore_context(errcontext, old_err);
      return result ;
    }
    /*FALLTHROUGH*/

  case ONULL:
    return TRUE ;

  default:
    return push( theo, &operandstack ) ;
  }
}

/* ----------------------------------------------------------------------------
   function:            myexec_()            author:              Andrew Cave
   creation date:       22-Oct-1987          last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 152.

---------------------------------------------------------------------------- */
Bool myexec_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  OBJECT obj = OBJECT_NOTVM_NOTHING;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;

  OCopy( obj, *theo );

  pop ( &operandstack );

  return setup_pending_exec(&obj, FALSE);
}

/* ----------------------------------------------------------------------------
   function:            if_()              author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 169.

---------------------------------------------------------------------------- */
Bool if_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o1) != OBOOLEAN )
    return error_handler( TYPECHECK ) ;

  switch ( oType(*o2) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  if ( ! oCanExec(*o2) && !object_access_override(o2) )
    return error_handler( INVALIDACCESS ) ;

  if ( oBool(*o1) ) {
    if ( oExecutable(*o2) ) {
      execStackSizeNotChanged = FALSE ;
      if ( ! push( o2 , & executionstack ))
        return FALSE ;
    }
    else {
      Copy( o1 , o2 ) ;
      pop( & operandstack ) ;
      return TRUE ;
    }
  }
  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            ifelse_()          author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 169.

---------------------------------------------------------------------------- */
Bool ifelse_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 , *o3 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  o3 = theTop( operandstack ) ;
  o2 = ( & o3[ -1 ] ) ;
  o1 = ( & o3[ -2 ] ) ;
  if ( ! fastStackAccess( operandstack )) {
    o2 = stackindex( 1 , & operandstack ) ;
    o1 = stackindex( 2 , & operandstack ) ;
  }

  if ( oType(*o1) != OBOOLEAN )
    return error_handler( TYPECHECK ) ;

  switch ( oType(*o2) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  switch ( oType(*o3) ) {
  case OARRAY :
  case OPACKEDARRAY :
    if ( (!oCanExec(*o2) && !object_access_override(o2)) ||
         (!oCanExec(*o3) && !object_access_override(o3)) )
      return error_handler( INVALIDACCESS ) ;
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  if ( oBool(*o1) )
    o3 = o2 ;

  if ( oExecutable(*o3) ) {
    execStackSizeNotChanged = FALSE ;
    if ( ! push( o3 , & executionstack ))
      return FALSE ;
    npop( 3 , & operandstack ) ;
  }
  else {
    Copy( o1 , o3 ) ;
    npop( 2 , & operandstack ) ;
  }

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            for_()             author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 160.  The manual does not define
   correct behaviour when increment is zero, so we emulate behaviour observed
   in (Level 2 and 3) Adobe RIPs, which is to finish without looping if
   initial < limit, and loop forever otherwise, pushing initial on each iteration.

---------------------------------------------------------------------------- */
Bool for_(ps_context_t *pscontext)
{
  register int32 inc , current , ilimit ;
  register OBJECT *o1 , *o2 , *o3 , *o4 ;
  int32 loop ;
  SYSTEMVALUE args[ 3 ] ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 3 )
    return error_handler( STACKUNDERFLOW ) ;

  o4 = theTop( operandstack ) ;
  o3 = ( & o4[ -1 ] ) ;
  o2 = ( & o4[ -2 ] ) ;
  o1 = ( & o4[ -3 ] ) ;
  if ( ! fastStackAccess( operandstack )) {
    o3 = stackindex( 1 , & operandstack ) ;
    o2 = stackindex( 2 , & operandstack ) ;
    o1 = stackindex( 3 , & operandstack ) ;
  }

  switch ( oType(*o4) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  if ( ! oCanExec(*o4) && !object_access_override(o4) )
    return error_handler( INVALIDACCESS ) ;

  if ( oType(*o1) == OINTEGER &&
       oType(*o2) == OINTEGER &&
       oType(*o3) == OINTEGER ) {
    current     = oInteger(*o1) ;
    inc         = oInteger(*o2) ;
    ilimit      = oInteger(*o3) ;

    /* Tests indicate we should loop forever iff inc == 0 && current >= ilimit */

    if (( inc > 0 ) ? ( current > ilimit ) : ( current < ilimit )) {
      npop( 4 , & operandstack ) ;
      return TRUE ;
    }

    if ( ! push4( o1, o2 , o3 , o4 , & executionstack ))
      return FALSE ;
    if ( ! xsetup_operator( NAME_xfastfori , 5 ))
      return FALSE ;
    if ( ! push( o4 , & executionstack ))
      return FALSE ;

    npop( 3 , & operandstack ) ;
    HQASSERT(oInteger(*o1) == current, "current changed mysteriously") ;

    return TRUE ;
  }
/* Slow default case. */

  if ( !object_get_numeric(o1, &args[0]) ||
       !object_get_numeric(o2, &args[1]) ||
       !object_get_numeric(o3, &args[2]) )
    return FALSE ;

  if ( oType(*o1) == OINTEGER && oType(*o2) == OINTEGER && intrange(args[2])) {
    if ( ! push4( o1 , o2 , o3 , o4 , & executionstack ))
      return FALSE ;
    if ( ! xsetup_operator( NAME_xfori , 5 ))
      return FALSE ;
  }
  else {
    for ( loop = 0 ; loop < 3 ; ++loop )
      if ( ! stack_push_real(args[loop], &executionstack) )
        return FALSE ;
    if ( ! push( o4 , & executionstack ))
      return FALSE ;
    if ( ! xsetup_operator( NAME_xforr , 5 ))
      return FALSE ;
  }
  npop( 4 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            repeat_()          author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 202.

---------------------------------------------------------------------------- */
Bool repeat_(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  if ( oType(*o1) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  switch ( oType(*o2) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  if ( ! oExecutable(*o2) && !object_access_override(o2) )
    return error_handler( INVALIDACCESS ) ;

  if ( oInteger(*o1) < 0 )
    return error_handler( RANGECHECK ) ;
  else if ( oInteger(*o1) == 0 || theLen(*o2) == 0 ) {
    npop( 2 , & operandstack ) ;
    return TRUE ;
  }
  if ( ! push( o1 , & executionstack ))
    return FALSE ;
  if ( ! push( o2 , & executionstack ))
    return FALSE ;
  if ( ! xsetup_operator( NAME_xrepeat , 3 ))
    return FALSE ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            loop_()            author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 182.

---------------------------------------------------------------------------- */
Bool loop_(ps_context_t *pscontext)
{
  OBJECT *o1 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = theTop( operandstack ) ;
  switch ( oType(*o1) ) {
  case OARRAY :
  case OPACKEDARRAY :
    if ( ! oExecutable(*o1) ) {
      pop( & operandstack ) ;
      return error_handler( STACKOVERFLOW ) ;
    }
    break ;
/* Return stack overflow, as our stacks can grow ever so large. */
  default:
    return error_handler( TYPECHECK ) ;
  }
  if ( ! oCanExec(*o1) && !object_access_override(o1) )
    return error_handler( INVALIDACCESS ) ;

  if ( ! push( o1 , & executionstack ))
    return FALSE ;
  if ( ! xsetup_operator( NAME_xloop , 2 ))
    return FALSE ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            exit_()            author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 154.

---------------------------------------------------------------------------- */
Bool exit_(ps_context_t *pscontext)
{
  register OBJECT *o1 ;

  execStackSizeNotChanged = FALSE ;
  while ( ! isEmpty( executionstack )) {
    o1 = theTop( executionstack ) ;

    switch ( oType(*o1) ) {
    case OARRAY:
      /* This case used to be explicitly represented as the first if-branch.
         Presumably it was explicitly different to the default for efficiency
         reasons, but that seems a bit spurious now. */
      pop( & executionstack ) ;
      break ;

    case ONOTHING:
      /* Represents a looping context if len non-zero */
      if ( theLen(*o1)) {
        npop( theLen(*o1), & executionstack ) ;
        return TRUE ;
      }
      pop( & executionstack ) ;
      break ;

    case ONULL :
      switch ( theLen(*o1)) {
      case ISINTERPRETEREXIT:
        oInteger(*o1) = NAME_exit;
        return TRUE;

      case ISPATHFORALL :
        path_free_list(thePathOf(*(PATHFORALL *)oOther(*o1)), mm_pool_temp) ;
        free_forallpath(oOther(*o1)) ;
        npop( 5 , & executionstack ) ;
        return TRUE ;
      }
      pop( & executionstack ) ;
      break ;

    case OMARK :       /* Represents a stopped mark */
      return error_handler( INVALIDEXIT ) ;

    case OFILE :       /* Represents a run object - if length set to special value */
      currfileCache = NULL ;
      if ( IsRunFile( o1 ))
        return error_handler( INVALIDEXIT ) ;

    default:
      pop( & executionstack ) ;
    }
  }
  return quit_(pscontext) ;
}

/* ----------------------------------------------------------------------------
   function:            stop_()            author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 226.

---------------------------------------------------------------------------- */
Bool stop_(ps_context_t *pscontext)
{
  return dothestop(pscontext, FALSE) ;
}

Bool superstop_(ps_context_t *pscontext)
{
  return dothestop(pscontext, TRUE) ;
}

static Bool dothestop(ps_context_t *pscontext, Bool super)
{
  execStackSizeNotChanged = FALSE ;

  while ( ! isEmpty( executionstack )) {
    register OBJECT *o1 = theTop(executionstack) ;
    int32 *tptr ;
    FILELIST *flptr ;

    switch ( oType(*o1) ) {
    case OMARK :   /* Represents a stopped mark */
      if ( ! super || theLen(*o1)) {
        mm_context_t *mmc = ps_core_context(pscontext)->mm_context;

        if ( theLen(*o1)) {
          allow_interrupt = FALSE; /* superstop disables interrupt */
          clear_interrupts();
        }
        /* Reset allocation cost after error handling. */
        if ( mmc != NULL) /* this can run before MM init */
          mm_set_allocation_cost(mmc, mm_cost_normal);
        pop( & executionstack ) ;
        return push( & tnewobj , & operandstack ) ;
      }
      pop( & executionstack ) ;
      break ;

    case ONULL :
      switch ( theLen(*o1)) {
      case ISINTERPRETEREXIT :
        /* indicate that we're in a stopped context by changing the object's
           value */
        oInteger(*o1) = super ? NAME_superstop : NAME_stop ;
        return TRUE;

      case ISPATHFORALL :
        tptr = oOther(*o1) ;
        path_free_list(thePathOf(*(PATHFORALL *)tptr), mm_pool_temp) ;
        free_forallpath( tptr ) ;
        npop( 4 , & executionstack ) ;
        break ;
      }
      pop( & executionstack ) ;
      break ;

    case OFILE : /* Represents a run object - if length is special value */
      currfileCache = NULL ;
      if ( IsRunFile ( o1 )) {
        flptr = oFile(*o1) ;
        if ( isIOpenFileFilter( o1 , flptr ))
          (void)(*theIMyCloseFile( flptr ))( flptr, CLOSE_EXPLICIT ) ;
      }
      pop( & executionstack ) ;
      break ;

    default:
      pop( & executionstack ) ;
    }
  }

  return quit_(pscontext) ;
}

/* ----------------------------------------------------------------------------
   function:            stopped_()        author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 227.

---------------------------------------------------------------------------- */
Bool stopped_(ps_context_t *pscontext)
{
  return dostopped(pscontext, FALSE) ;
}

Bool superstopped_(ps_context_t *pscontext)
{
  return dostopped(pscontext, TRUE) ;
}

static Bool dostopped(ps_context_t *pscontext, Bool super)
{
  int32 temp ;
  OBJECT omark = OBJECT_NOTVM_MARK ;

  temp = theStackSize( operandstack ) ;
  if ( temp < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theLen(omark) = CAST_SIGNED_TO_UINT16(super) ; /* Represents a stopped mark */
  if ( ! push(&omark, &executionstack) )
    return FALSE ;

  if ( ! myexec_(pscontext))  /* Indicates stackoverflow. */
    return FALSE ;

  if ( theStackSize( operandstack ) == temp ) {
    pop( & executionstack ) ;
    return push( & fnewobj , & operandstack ) ;
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            countexecstack_()  author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 133.

---------------------------------------------------------------------------- */
Bool countexecstack_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return stack_push_integer( theStackSize( executionstack ) + 1, &operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            execstack_()       author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 152.

---------------------------------------------------------------------------- */
Bool execstack_(ps_context_t *pscontext)
{
  register OBJECT *o1 ;
  int32 esize, realrun ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = theTop( operandstack ) ;
  switch ( oType(*o1) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  if ( !oCanWrite(*o1) && !object_access_override(o1) )
    return error_handler( INVALIDACCESS ) ;

  esize = theStackSize( executionstack) ;
  if ( theLen(*o1) <= esize )
    return error_handler( RANGECHECK ) ;

  for ( realrun = 0 ; realrun <= 1 ; ++realrun ) {
    register OBJECT *olist = oArray(*o1) ;
    int32 glmode = oGlobalValue(*o1) ;
    int32 i, realcount = 0;          /* shuts lint up */
    Bool realtype = FALSE ;

    if ( realrun ) {
/* Check if saved. */
      if ( ! check_asave(olist, theLen(*o1), glmode, corecontext) )
        return FALSE ;
    }

/* Copy the elements of the execution stack into the array. */
    for ( i = 0, olist += esize ; i <= esize ; ++i, --olist ) {
      register OBJECT *theo = stackindex( i , & executionstack ) ;

      if ( realtype ) {

/* Check OBJECTS for illegal LOCAL --> GLOBAL */
        if ( glmode )
          if ( illegalLocalIntoGlobal(theo, corecontext) )
            return error_handler( INVALIDACCESS ) ;

        if ( realrun ) {
          Copy( olist , theo ) ;
        }
        ++realcount ;
        if ( realcount == 4 )
          realtype = FALSE ;
      }
      else {
        if ( realrun ) {
          theTags(*olist) = OOPERATOR | EXECUTABLE ;
          theLen(*olist) = 1 ;
        }
        switch ( oType(*theo) ) {
        case ONOTHING :
          if ( realrun ) {
            HQASSERT(theINameNumber(theIOpName(oOp(*theo))) >= 0 &&
                     theINameNumber(theIOpName(oOp(*theo))) < OPS_COUNTED ,
                     "execstack_ creating segv operator" ) ;
            oOp(*olist) = & system_ops[theINameNumber(theIOpName(oOp(*theo)))];
          }
          break ;
        case OMARK :
          if ( realrun )
            oOp(*olist) = & system_ops[NAME_stopped];
          break ;
        case ONULL :
          switch ( theLen(*theo)) {
          case ISINTERPRETEREXIT :
/* not the best of things to return, but what else? */
            if ( realrun )
              oOp(*olist) = & system_ops[NAME_exit];
            break;
          case ISPATHFORALL :
            realcount = 0 ;
            realtype = TRUE ;
            if ( realrun )
              oOp(*olist) = & system_ops[NAME_pathforall];
            break ;
          case ISPROCEDURE :
            /* No executed effect - flush is the only NOP I can think of */
            if ( realrun )
              oOp(*olist) = & system_ops[NAME_flush] ; /* or yield */
            break ;
          default:
            HQFAIL("Unknown ONULL variant found on execstack") ;
          }
          break ;
        default:
/* Check OBJECTS for illegal LOCAL --> GLOBAL */
          if ( glmode )
            if ( illegalLocalIntoGlobal(theo, corecontext) )
              return error_handler( INVALIDACCESS ) ;

          if ( realrun ) {
            Copy( olist , theo ) ;
          }
          break ;
        }
      }
    }
  }
/* Set up a new sub-array. */
  theLen(*o1) = CAST_SIGNED_TO_UINT16(esize + 1) ;
  return TRUE ;
}

/* Other relevant operators that require control setup */

/* ----------------------------------------------------------------------------
   function:            forall_()          author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 161.

---------------------------------------------------------------------------- */
Bool forall_(ps_context_t *pscontext)
{
  int32 which, number, type ;
  OBJECT *o1, *o2, *accessobj ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  switch ( oType(*o2) ) {
  case OARRAY :
  case OPACKEDARRAY :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  number = 3 ;
  accessobj = o1 = stackindex(1, &operandstack) ;
  type = oXType(*o1) ;

  switch ( type ) {

  case ODICTIONARY :
    accessobj = oDict(*o1) ;
    oInteger(inewobj) = DICT_ALLOC_LEN(accessobj);
    if ( ! push( & inewobj , & executionstack ))
      return FALSE ;
    which = NAME_xdforall ;
    ++number ;
    break ;
  case OSTRING :
    which = NAME_xsforall ;
    break ;
  case OLONGSTRING :
    oInteger(inewobj) = 0 ;
    if ( ! push( & inewobj , & executionstack ))
      return FALSE ;
    which = NAME_xlsforall ;
    ++number ;
    break ;
  case OLONGARRAY :
  case OLONGPACKEDARRAY :
    /* The trampoline must be pushed too, as it is modified by xaforall */
    if ( ! push( oArray(*o1)   , & executionstack ) || /* length */
         ! push( oArray(*o1)+1 , & executionstack ))   /* subarray */
      return FALSE ;
    number+=2;
    /* drop through */
  case OARRAY :
  case OPACKEDARRAY :
    which = NAME_xaforall ;
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  if ( (!oCanRead(*accessobj) && !object_access_override(accessobj)) ||
       (!oCanExec(*o2) && !object_access_override(o2)) ) {
    while ( number-- > 3 )
      pop( & executionstack ) ;
    return error_handler( INVALIDACCESS ) ;
  }
  if ( ! push( o2 , & executionstack ))
    return FALSE ;
  if ( ! push( o1 , & executionstack ))
    return FALSE ;

  switch ( type ) {
    case OLONGARRAY:
    case OLONGPACKEDARRAY:
      /* point the pushed OEXTENDED at the pushed trampoline below it */
      o1 = theTop(executionstack) ;
      oArray(*o1) = stackindex( 3 , & executionstack ) ;
      break ;
  }

  if ( ! xsetup_operator( which , number ))
    return FALSE ;

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            run_()             author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 207.

---------------------------------------------------------------------------- */
Bool run_(ps_context_t *pscontext)
{
  register OBJECT *o1 ;
  OBJECT fileo = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = theTop( operandstack ) ;
  if ( oType(*o1) != OSTRING )
    return error_handler( TYPECHECK) ;

  if ( !oCanRead(*o1) && !object_access_override(o1) )
    return error_handler( INVALIDACCESS ) ;

  if ( !file_open(o1, SW_RDONLY|SW_FROMPS, READ_FLAG, FALSE, 0, &fileo) )
    return FALSE ;

  SetRunFile(&fileo) ;
  theTags(fileo) = OFILE | EXECUTABLE | READ_ONLY ;

  currfileCache = NULL ;
  execStackSizeNotChanged = FALSE ;
  if ( ! push(&fileo, &executionstack) )
    return FALSE;

  pop( & operandstack ) ;

  return TRUE ;
}

/* Graphics operators that require control setup. */

/* ----------------------------------------------------------------------------
   function:            pathforall_()      author:              Andrew Cave
   creation date:       22-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 192.

---------------------------------------------------------------------------- */
Bool pathforall_(ps_context_t *pscontext)
{
  int32 pushed = 0 ;
  PATHINFO newpath = {0} ;
  PATHFORALL *newone ;
  PATHINFO *lpath = &(thePathInfo(*gstateptr)) ;
  Bool result = FALSE ;
  OBJECT opfa = OBJECT_NOTVM_NULL ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ( thePathInfo(*gstateptr)).protection)
    return error_handler( INVALIDACCESS ) ;

  if ( theStackSize( operandstack ) < 3 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( lpath->lastline == NULL ) {
    npop( 4 , & operandstack ) ;
    return TRUE ;
  }

  for ( pushed = 0 ; pushed < 4 ; ++pushed ) {
    if ( ! push( stackindex( pushed , & operandstack ) , & executionstack )) {
      npop(pushed, &executionstack) ;
      return FALSE ;
    }
  }

  if ( NULL == ( newone = get_forallpath())) {
    npop(pushed, &executionstack) ;
    return error_handler( VMERROR ) ;
  }

  SET_SINV_SMATRIX( & thegsPageCTM(*gstateptr) , NEWCTM_ALLCOMPONENTS ) ;
  if ( SINV_NOTSET( NEWCTM_ALLCOMPONENTS )) {
    free_forallpath( newone ) ;
    npop(pushed, &executionstack) ;
    return error_handler( UNDEFINEDRESULT ) ;
  }
  MATRIX_COPY(&thePathCTM(*newone), &sinv) ;

  execStackSizeNotChanged = FALSE ;
  theLen(opfa) = ISPATHFORALL ;
  oCPointer(opfa) = newone ;

  if ( push(&opfa, &executionstack) && ++pushed > 0 &&
       path_copy_list(lpath->firstpath, &newpath.firstpath,
                      &newpath.lastpath, &newpath.lastline, mm_pool_temp) &&
       path_close(MYCLOSE, &newpath) ) {

    thePathOf(*newone) = newpath.firstpath ;
    npop(4, &operandstack) ;
    result = TRUE ;

  } else {

    free_forallpath(newone) ;
    npop(pushed, &executionstack) ;
  }

  return result ;
}


/* ---------------------------------------------------------------------- */
Bool xfastfori(ps_context_t *pscontext)
{
  register OBJECT *o1 , *o2 , *o3 , *o4 ;
  register int32 inc , current , ilimit , inext ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  o4 = stackindex( 1 , & executionstack ) ;
  o3 = ( & o4[ -1 ] ) ;
  o2 = ( & o4[ -2 ] ) ;
  o1 = ( & o4[ -3 ] ) ;
  if ( ! fastStackAccess( executionstack )) {
    o3 = stackindex( 2 , & executionstack ) ;
    o2 = stackindex( 3 , & executionstack ) ;
    o1 = stackindex( 4 , & executionstack ) ;
  }

  current = oInteger(*o1) ;
  inc = oInteger(*o2) ;
  ilimit = oInteger(*o3) ;
  inext = current + inc;

  /* Check for overflow when checking termination condition
   * Preserve inconsistent behaviour when inc == 0 */
  if (( inc > 0 ) ? ( inext > ilimit || inext < current) : ( inext < ilimit || inext > current)) {
    npop( 5 , & executionstack ) ;
    return TRUE ;
  }

  oInteger(inewobj) = inext ;
  if ( ! push( & inewobj , & operandstack ))
    return FALSE ;

  oInteger(*o1) = inext ;

  return push( o4 , & executionstack ) ;
}

/* ---------------------------------------------------------------------- */
Bool xfori(ps_context_t *pscontext)
{
  register OBJECT *theo , *ano ;
  register int32 inc , current , ilimit ;
  SYSTEMVALUE slimit ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  theo = stackindex( 4 , & executionstack ) ;
  current = oInteger(*theo) ;
  inc = oInteger(*stackindex(3, &executionstack)) ;

  ano = stackindex( 2 , & executionstack ) ;
  if ( oType(*ano) == OINTEGER ) {
    ilimit = oInteger(*ano) ;
    if (( inc > 0 ) ? ( current > ilimit ) : ( current < ilimit )) {
      npop( 5 , & executionstack ) ;
      return TRUE ;
    }
  }
  else {
    slimit = oReal(*ano) ;
    if (( inc > 0 ) ? ( current > slimit ) : ( current < slimit )) {
      npop( 5 , & executionstack ) ;
      return TRUE ;
    }
  }

  oInteger(inewobj) = current ;
  if ( ! push( & inewobj , & operandstack ))
    return FALSE ;

  oInteger(*theo) = current + inc ;

  theo = stackindex( 1 , & executionstack ) ;
  if ( oExecutable(*theo) )
    return push( theo , & executionstack ) ;
  else
    return push( theo , & operandstack ) ;
}

/* ---------------------------------------------------------------------- */
Bool xforr(ps_context_t *pscontext)
{
  register OBJECT *theo , *other ;
  SYSTEMVALUE inc , current , limit ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  theo = stackindex( 2 , & executionstack ) ;
  if ( oType(*theo) == OREAL )
    limit = oReal(*theo) ;
  else
    limit = OINFINITY_VALUE ;
  theo = stackindex( 4 , & executionstack ) ;
  if ( oType(*theo) == OREAL )
    current = oReal(*theo) ;
  else
    current = OINFINITY_VALUE ;
  other = stackindex( 3 , & executionstack ) ;
  if ( oType(*other) == OREAL )
    inc = oReal(*other) ;
  else
    inc = OINFINITY_VALUE ;

  if (( inc > 0.0 ) ? ( current > limit ) : ( current < limit )) {
    npop( 5 , & executionstack ) ;
    return TRUE ;
  }
  if ( ! stack_push_real( current, &operandstack ))
    return FALSE ;

  current += inc ;

  if ( realrange( current )) {
    if ( ! realprecision( current ))
      oReal(*theo) = 0.0f ;
    else
      oReal(*theo) = ( USERVALUE )current ;
  }
  else
    theTags(*theo) = OINFINITY | LITERAL ;

  theo = stackindex( 1 , & executionstack ) ;
  if ( oExecutable(*theo) )
    return push( theo , & executionstack ) ;
  else
    return push( theo , & operandstack ) ;
}

/* ---------------------------------------------------------------------- */
Bool xrepeat(ps_context_t *pscontext)
{
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  theo = stackindex( 2 , & executionstack ) ;
  if ( ! oInteger(*theo) ) {
    npop( 3 , & executionstack ) ;
    return TRUE ;
  }
  --oInteger(*theo) ;
  theo = stackindex( 1 , & executionstack ) ;
  if ( oExecutable(*theo) )
    return push( theo , & executionstack ) ;
  else
    return push( theo , & operandstack ) ;
}

/* ---------------------------------------------------------------------- */
Bool xloop(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( stackindex( 1 , & executionstack ) , & executionstack ) ;
}

/* ---------------------------------------------------------------------- */
/* For xaforall, the execution stack contains:
      0 ONOTHING  len=size of this block (3 or 5)  oOp = xaforall
      1 OARRAY    len=elements left to deliver  oArray=next element to deliver
        or OEXTENDED  len=0  oArray = trampoline below
      2 OARRAY - the proc to run
   For long arrays:
      3 OARRAY    len=0  oArray = next element to deliver
      4 OINTEGER  len=0  oInteger = remaining elements
*/
Bool xaforall(ps_context_t *pscontext)
{
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  theo = stackindex( 1 , & executionstack ) ;

  if ( ! theXLen(*theo)) {
    npop( theLen(*theTop(executionstack)) , & executionstack ) ;
    return TRUE ;
  }

  if ( oType(*theo) == OEXTENDED ) {  /* Long arrays */

    if ( !push(oLongArray(*theo), &operandstack) )
      return FALSE ;

    if (--oLongArrayLen(*theo) == 0)
      oLongArray(*theo) = NULL ;
    else
      ++oLongArray(*theo) ;

  } else {  /* Normal arrays */

    if ( ! push(oArray(*theo) , & operandstack ))
      return FALSE ;

    if ( --theLen(*theo) == 0 )
      oArray(*theo) = NULL ;
    else
      ++oArray(*theo) ;
  }

  theo = stackindex( 2 , & executionstack ) ;
  if ( oExecutable(*theo) )
    return push( theo , & executionstack ) ;
  else
    return push( theo , & operandstack ) ;
}

/* ---------------------------------------------------------------------- */
Bool xsforall(ps_context_t *pscontext)
{
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  theo = stackindex( 1 , & executionstack ) ;

  if ( ! theLen(*theo)) {
    npop( 3 , & executionstack ) ;
    return TRUE ;
  }

  if ( ! stack_push_integer(( int32 )*oString(*theo), &operandstack) )
    return FALSE ;

  if ( --theLen(*theo) == 0 )
    oString(*theo) = NULL ;
  else
    ++oString(*theo) ;

  theo = stackindex( 2 , & executionstack ) ;
  if ( oExecutable(*theo) )
    return push( theo , & executionstack ) ;
  else
    return push( theo , & operandstack ) ;
}

/* ---------------------------------------------------------------------- */
Bool xlsforall(ps_context_t *pscontext)
{
  OBJECT  *theo ;
  LONGSTR *lstr ;
  int32    idx ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  theo = stackindex( 1 , & executionstack ) ;
  lstr = oLongStr(*theo) ;

  HQASSERT( lstr, "xsforall: invalid longstring object" ) ;

  theo = stackindex( 3 , & executionstack ) ;
  idx = oInteger(*theo) ;
  if ( idx == theLSLen(* lstr )) {
    npop( 4 , & executionstack ) ;
    return TRUE ;
  }

  if ( ! stack_push_integer((int32)theLSCList(*lstr)[idx],
                            &operandstack))
    return FALSE ;

  oInteger(*theo) = idx + 1 ;

  theo = stackindex( 2 , & executionstack ) ;
  if ( oExecutable(*theo) )
    return push( theo , & executionstack ) ;
  else
    return push( theo , & operandstack ) ;
}

/* ---------------------------------------------------------------------- */
Bool xdforall(ps_context_t *pscontext)
{
  register int32 i ;
  register OBJECT *o1 , *o2 ;
  register DPAIR *dplist ;
  OBJECT *thed ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  o1 = stackindex( 3 , & executionstack ) ;
  i = oInteger(*o1) - 1 ;
  o2 = stackindex( 1 , & executionstack ) ;

  thed = oDict(*o2) ;
  if ( i < 0 ) { /* end of dict array reached, check for extension */
    OBJECT *ext = thed - 1;

    if ( oType(*ext) == ONOTHING ) {
      npop( 4 , & executionstack ) ;
      return TRUE ;
    }
    Copy( o2 , ext ) ;       /* put chain dictionary onto exec stack */
    /* set new dictionary and search length: */
    thed = oDict(*o2) ;
    i = DICT_ALLOC_LEN(thed) - 1;
  }

  dplist = ( DPAIR * )( thed + 1 ) ;

  /* Get next key-object pair in the dictionary */
  dplist = ( & ( dplist[ i ] )) ;
  while ( i >= 0 ) {
    if ( oType(theIKey(dplist)) != ONOTHING )
      break ;
    --i ;
    --dplist ;
  }
  if ( i < 0 ) {
    npop( 4, & executionstack );
    return TRUE;
  }

/* Push key */
  if ( ! push( & theIKey( dplist ) , & operandstack ))
    return FALSE ;
/* Push name */
  if ( ! push( & theIObject( dplist ), & operandstack ))
    return FALSE ;

  oInteger(*o1) = i ;
  o1 = stackindex( 2 , & executionstack ) ;
  if ( oExecutable(*o1) )
    return push( o1 , & executionstack ) ;
  else
    return push( o1 , & operandstack ) ;
}

/* ---------------------------------------------------------------------- */
Bool xpathforall(PATHFORALL *thepath)
{
  OBJECT *theo ;
  LINELIST *currline , *theline , *templine ;
  PATHLIST *currpath ;
  SYSTEMVALUE x , y ;

  currpath = thePathOf(*thepath) ;
  currline = theSubPath(*currpath) ;

  for (;;) {
    if ( ! currline ) {
      if ( ! currpath->next) {
        path_free_list( currpath, mm_pool_temp ) ;
        free_forallpath( thepath ) ;
        npop( 5 , & executionstack ) ;
        return TRUE ;
      }
      else {
        thePathOf(*thepath) = currpath->next ;
        currpath->next = NULL ;
        path_free_list( currpath, mm_pool_temp ) ;
        currpath = thePathOf(*thepath) ;
        currline = theSubPath(*currpath) ;
      }
    }
    switch ( theLineType(*currline) ) {
    case MYMOVETO :
      currline = currline->next ;
      continue ;
    case MYCLOSE :
      thePathOf(*thepath) = currpath->next ;
      currpath->next = NULL ;
      path_free_list( currpath, mm_pool_temp ) ;
      currpath = thePathOf(*thepath) ;
      if ( ! currpath ) {
        free_forallpath( thepath ) ;
        npop( 5 , & executionstack ) ;
        return TRUE ;
      }
      currline = theSubPath(*currpath) ;
      continue ;
    }
    break ;
  }

  theo = NULL ;

  switch ( theLineType(*currline) ) {

  case CURVETO :
    theo = stackindex( 3 , & executionstack ) ;
    MATRIX_TRANSFORM_XY( theX(thePoint(*currline)),
                         theY(thePoint(*currline)),
                         x, y, &thePathCTM(*thepath)) ;
    if ( ! stack_push_real( x, &operandstack ))
      return FALSE ;
    if ( ! stack_push_real( y, &operandstack ))
      return FALSE ;
    currline = currline->next ;
    MATRIX_TRANSFORM_XY( theX(thePoint(*currline)),
                         theY(thePoint(*currline)),
                         x, y, &thePathCTM(*thepath)) ;
    if ( ! stack_push_real( x, &operandstack ))
      return FALSE ;
    if ( ! stack_push_real( y, &operandstack ))
      return FALSE ;
    currline = currline->next ;

  case MOVETO :
    if ( ! theo )
      theo = stackindex( 1 , & executionstack ) ;

  case LINETO :
    if ( ! theo )
      theo = stackindex( 2 , & executionstack ) ;
    MATRIX_TRANSFORM_XY( theX(thePoint(*currline)),
                         theY(thePoint(*currline)),
                         x, y, &thePathCTM(*thepath)) ;
    if ( ! stack_push_real( x, &operandstack ))
      return FALSE ;
    if ( ! stack_push_real( y, &operandstack ))
      return FALSE ;

  case CLOSEPATH :
    if ( ! theo )
      theo = stackindex( 4 , & executionstack ) ;
    currline = currline->next ;
    break ;
  }

/* Free any structs going ... */
  theline = theSubPath(*currpath) ;
  while ( theline != currline ) {
    templine = theline ;
    theline = theline->next ;
    free_line( templine, mm_pool_temp ) ;
  }
  theSubPath(*currpath) = theline ;

  return setup_pending_exec( theo, FALSE ) ;
}


/** This routine tests to see if an object matches
   the template provided on the top of the stack. The object is the second
   object on the stack.
   This is used by the resourceforall operator. (Which is way it is in
   this file). */
Bool matchtemplate_(ps_context_t *pscontext)
{
  int32 len , template_len ;
  uint8 *str , *template ;
  OBJECT *o1 , *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  len = 0;
  str = NULL;
  o2 = theTop( operandstack ) ;
  o1 = stackindex( 1 , & operandstack ) ;
  if ( oType(*o2) != OSTRING )
    return error_handler( TYPECHECK ) ;
  template_len = theLen(*o2) ;
  template = oString(*o2) ;
  switch ( oType(*o1) ) {
  case ONAME :
    str = theICList(oName(*o1)) ;
    len = theINLen(oName(*o1)) ;
    break ;
  case OSTRING :
    str = oString(*o1) ;
    len = theLen(*o1) ;
    break ;
  default:    /* only if the string is (*) is there a match */
    if ( template_len == 1 && *template == (uint8)'*' )
      o1 = &tnewobj ;
    else
      o1 = &fnewobj ;
    pop( &operandstack ) ;
    if ( ! push( o1 , &operandstack ))
      return FALSE ;
  }
  if (template_match( template , template_len , str , len ))
    o1 = &tnewobj ;
  else
    o1 = &fnewobj ;
  npop( 2, &operandstack ) ;
  if ( ! push( o1 , &operandstack ))
    return FALSE ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            template_match    author:              Luke Tunmer
   creation date:       05-Jul-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool template_match(
                           register uint8 *template,
                           register int32 template_len,
                           register uint8 *str,
                           register int32 len
)
{
  while ( template_len && len ) {
    if ( *template == (uint8)'?' ) {  /* always matches */
      template++ ; template_len--;
      str++ ; len-- ;
    } else if ( *template == (uint8)'*' ) {
      /* matches any sequence of characters */
      while ( *template == (uint8)'*' && template_len ) {
        /* skip over *'s */
        template++ ; template_len-- ;
      }
      if ( ! template_len )
        return TRUE ;       /* will match to the end of the string */
      while (len) {
        if ( *template == *str || *template == (uint8)'?' ) {
          if ( template_len == 1 && len == 1 )
            return TRUE ;
          if ( template_match( template+1, template_len-1, str+1, len-1 ))
            return TRUE ;
        }
        str++ ; len-- ;
      }
      return FALSE ;
    } else {
      if ( *template == (uint8)'\\' ) {
        template++ ; template_len-- ;
        if ( ! template_len )
          return FALSE ;
      }
      if ( *template != *str )
        return FALSE ;

      ++template ; ++str ;
      --template_len ; --len ;
    }
  }

  if ( ! len ) {
    if ( ! template_len )
      return TRUE ;
    if ( *template == (uint8)'*' && template_len == 1 )
      return TRUE ;
  }
  return FALSE ;
}


/* Log stripped */
