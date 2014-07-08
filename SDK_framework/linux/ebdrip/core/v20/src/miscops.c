/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:miscops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Miscellaneous PostScript operators.
 */

#include "core.h"
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swctype.h"
#include "swstart.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "monitor.h"
#include "mm.h"
#include "mmcompat.h"
#include "swpdfout.h"
#include "swpdf.h"
#include "namedef_.h"
#include "debugging.h"
#include "timing.h"

#include "stacks.h"
#include "dicthash.h" /* insert_hash */
#include "dictops.h"
#include "control.h"
#include "matrix.h"
#include "params.h"
#include "swmemory.h"
#include "psvm.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
/* needed by render.h: */
#include "dlstate.h"
#include "render.h"
#include "fileops.h"
#include "execops.h"
#include "ndisplay.h"
#include "routedev.h"
#include "gstate.h"
#include "filters.h"
#include "miscops.h"
#include "stackops.h"
#include "security.h"
#include "swcopyf.h"
#include "idiom.h"
#include "system.h"
#include "often.h"
#include "tranState.h"
#include "setup.h" /* xsetup_operator */
#include "uniqueid.h"  /* UniqueID ranges */

#include "pscontext.h"
#include "rdrapi.h"
#include "swdataapi.h"
#include "swdataimpl.h"
#include "swevents.h"
#include "timelineapi.h"

/* Static prototypes. */

static Bool checkoneshadowop( OBJECT *thekey , OBJECT *theo ,
                              void *argsBlockPtr ) ;
static Bool makeoneshadowop( OBJECT *thekey , OBJECT *theo ,
                             void *argsBlockPtr ) ;
static Bool makeonedefineop( OBJECT *thekey , OBJECT *theo ,
                             void *argsBlockPtr ) ;

static OBJECT *shadowproc = NULL ;
static OBJECT *defineproc = NULL ;
static OPFUNCTION *shadowop = NULL ;

static Bool bind_internal(corecontext_t *context, OBJECT *proc ) ;
static Bool bind_recurse( OBJECT *proc , int32 ir ) ;

/* -------------------------------------------------------------------------- */
#ifdef DEBUG_BUILD

/* Issue an event that the Timeline implementation will respond to */

Bool ptimelines_(ps_context_t *pscontext)
{

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  debug_print_timelines(SW_TL_REF_INVALID) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* timelineop - internaldict operator interface to the Timeline system */

Bool timelineop_(ps_context_t *pscontext)
{
  OBJECT *o, *p1 = NULL, *p2 = NULL, *p3 = NULL ;
  uint8 type = OINVALID ;
  uint16 len = 0 ;
  sw_tl_result result ;
  Bool fast = fastStackAccess(operandstack) ;

  /* We need at least one parameter */
  if ( 0 > theStackSize(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  /* It must be a name */
  o = theTop(operandstack) ;
  if (oType(*o) != ONAME)
    return error_handler(TYPECHECK) ;

  /* Most require at least one further parameter */
  if ( 1 <= theStackSize(operandstack))
    p1 = (fast) ? &o[-1] : stackindex(1, &operandstack) ;
  if ( 2 <= theStackSize(operandstack))
    p2 = (fast) ? &o[-2] : stackindex(2, &operandstack) ;

  switch (oName(*o)->namenumber) {
  case NAME_Current:
    /* /Current timelineop -> current interpret timeline */
    if (!pscontext || !pscontext->corecontext || !pscontext->corecontext->page)
      return error_handler(UNDEFINED) ;
    oInteger(*o) = pscontext->corecontext->page->timeline ;
    type = OINTEGER ;
    break ;

  case NAME_End:
    /* int /End timelineop -> bool */
    if (1 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;

    if (oType(*p1) != OINTEGER)
      return error_handler(TYPECHECK) ;

    result = SwTimelineEnd((sw_tl_ref)oInteger(*p1)) ;
    if (result == SW_TL_ERROR_UNKNOWN)
      return error_handler(UNDEFINED) ;
    pop(&operandstack) ;
    type = OBOOLEAN ;
    oBool(*o) = (result == SW_TL_SUCCESS) ;
    break ;

  case NAME_Abort:
    /* int int /Abort timelineop -> bool */
    if (2 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;
    p2 = (fast) ? &o[-2] : stackindex(2, &operandstack) ;

    if (oType(*p2) != OINTEGER || oType(*p2) != OINTEGER)
      return error_handler(TYPECHECK) ;

    result = SwTimelineAbort((sw_tl_ref)oInteger(*p2), oInteger(*p1)) ;
    if (result == SW_TL_ERROR_UNKNOWN)
      return error_handler(UNDEFINED) ;
    npop(2, &operandstack) ;
    type = OBOOLEAN ;
    oBool(*o) = (result == SW_TL_SUCCESS) ;
    break ;

  case NAME_SetTitle:
    /* int (string)|null /SetTitle timelineop -> | */
    if (2 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;

    if (oType(*p2) != OINTEGER || (oType(*p1) != ONULL && oType(*p1) != OSTRING))
      return error_handler(TYPECHECK) ;

    {
      uint8 * title  = NULL ;
      size_t  length = 0 ;

      if (oType(*p1) == OSTRING) {
        title  = oString(*p1) ;
        length = theLen(*p1) ;
      }

      result = SwTimelineSetTitle((sw_tl_ref)oInteger(*p2), title, length) ;
    }
    if (result == SW_TL_ERROR_UNKNOWN)
      return error_handler(UNDEFINED) ;
    npop(2, &operandstack) ;
    break ;

  case NAME_GetTitle:
    /* int /GetTitle timelineop -> (string)|null */
    if (1 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;

    if (oType(*p1) != OINTEGER)
      return error_handler(TYPECHECK) ;

    {
      uint8 * buffer = NULL ;
      size_t actual = 512, current = 0 ;

      do { /* loop until buffer is large enough */
        if (buffer) /* discard previous too-small buffer */
          mm_free(mm_pool_temp, buffer, current) ;
        buffer = mm_alloc(mm_pool_temp, actual, MM_ALLOC_CLASS_GENERAL) ;
        if (buffer == 0)
          return error_handler(VMERROR) ;
        current = actual ; /* current is the size of the allocated buffer */
        actual = SwTimelineGetTitle((sw_tl_ref)oInteger(*p1), buffer, current) ;
      } while (actual > current) ;
      if (actual > MAXPSSTRING)
        actual = MAXPSSTRING ;
      /* actual is the length of the string, not necessarily the whole buffer */

      if (actual == 0) {
        type = ONULL ;
        oInteger(*p1) = 0 ;
      } else {
        type = OSTRING | CANWRITE ;
        if (!ps_string(p1, buffer, (int32)actual)) {
          mm_free(mm_pool_temp, buffer, current) ;
          return FALSE ;
        }
        len = theLen(*p1) ;
      }
      mm_free(mm_pool_temp, buffer, current) ;
    }
    pop(&operandstack) ;
    break ;

  case NAME_SetExtent:
    /* int numeric numeric /SetExtent timelineop -> | */
    if (3 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;
    p3 = (fast) ? &o[-3] : stackindex(3, &operandstack) ;

    if (oType(*p3) != OINTEGER || (oType(*p2) != OINTEGER && oType(*p2) != OREAL) ||
        (oType(*p1) != OINTEGER || oType(*p1) != OREAL || oType(*p1) != OINFINITY))
      return error_handler(TYPECHECK) ;

    {
      double start, end ;

      (void)object_get_XPF(p2, &start) ;
      if (oType(*p1) == OINFINITY)
        end = SW_TL_EXTENT_INDETERMINATE ;
      else
        (void)object_get_XPF(p1, &end) ;

      result = SwTimelineSetExtent((sw_tl_ref)oInteger(*p3), start, end) ;
    }
    if (result == SW_TL_ERROR_UNKNOWN)
      return error_handler(UNDEFINED) ;
    npop(3, &operandstack) ;
    break ;

  case NAME_SetProgress:
    /* int numeric /SetProgress timelineop -> | */
    if (2 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;

    if (oType(*p2) != OINTEGER || (oType(*p1) != OINTEGER && oType(*p1) != OREAL))
      return error_handler(TYPECHECK) ;

    {
      double progress ;

      (void) object_get_XPF(p1, &progress) ;

      result = SwTimelineSetProgress((sw_tl_ref)oInteger(*p2), progress) ;
    }
    if (result == SW_TL_ERROR_UNKNOWN)
      return error_handler(UNDEFINED) ;
    npop(2, &operandstack) ;
    break ;

  case NAME_GetProgress:
    /* int /GetProgress timelineop -> real */
    if (1 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;

    if (oType(*p1) != OINTEGER)
      return error_handler(TYPECHECK) ;

    {
      double progress ;

      result = SwTimelineGetProgress((sw_tl_ref)oInteger(*p1), NULL, NULL, &progress, NULL) ;

      if (result == SW_TL_ERROR_UNKNOWN)
        return error_handler(UNDEFINED) ;
      object_store_XPF(p1, progress) ;
      len = theLen(*p1) ;  /* in case it is an XPF */
    }
    pop(&operandstack) ;
    type = OREAL ;
    break ;

  case NAME_Ancestor:
  case NAME_OfType:
    /* int /Type|(Type)|int /Ancestor timelineop -> int */
    if (2 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;

    if (oType(*p2) != OINTEGER || (oType(*p1) != ONAME &&
          oType(*p1) != OSTRING && oType(*p1) != OINTEGER))
      return error_handler(TYPECHECK) ;

    {
      sw_tl_type tltype = SW_TL_TYPE_ANY ;
      sw_tl_ref  tl ;
      Bool       found = FALSE ;

      if (oType(*p1) == OINTEGER)
        tltype = oInteger(*p1) ;
      else {
        /* Lookup Timeline type name via RDR */
        uint8 * name ;
        size_t length ;

        if (oType(*p1) == OSTRING) {
          name   = oString(*p1) ;
          length = theLen(*p1) ;
        } else {
          name   = oName(*p1)->clist ;
          length = oName(*p1)->len ;
        }

        if (length == 3 && memcmp("Any", name, 3) == 0) {
          found = TRUE ;
        } else {
          sw_rdr_iterator * it = SwFindRDRbyType(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME) ;
          sw_rdr_id  id ;
          void *     rdr ;
          size_t     rdrlen ;

          if (it) {
            while (SwNextRDR(it, NULL, NULL, &id, &rdr, &rdrlen) == SW_RDR_SUCCESS) {
              if (rdr && rdrlen == length && memcmp(rdr, name, length) == 0) {
                tltype = (sw_tl_type) id ;
                found = TRUE ;
                break ;
              }
            }
            SwFoundRDR(it) ;
          }
        }
        if (!found)
          return error_handler(UNDEFINED) ;
      }

      if (oName(*o)->namenumber == NAME_Ancestor)
        tl = SwTimelineGetAncestor((sw_tl_ref)oInteger(*p2), tltype) ;
      else {
        if (tltype == SW_TL_TYPE_ANY)
          return error_handler(RANGECHECK) ;
        tl = SwTimelineOfType((sw_tl_ref)oInteger(*p2), tltype) ;
      }

      oInteger(*p2) = (int32)tl ;
    }
    npop(2, &operandstack) ;
    type = OINTEGER ;
    break ;

  case NAME_GetType:
    /* int /GetType timelineop -> /Type|int */
    if (1 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;

    if (oType(*p1) != OINTEGER)
      return error_handler(TYPECHECK) ;

    {
      sw_tl_type tltype ;
      void * rdr ;
      size_t length ;

      tltype = SwTimelineGetType((sw_tl_ref)oInteger(*p1)) ;
      if (tltype == SW_TL_TYPE_NONE)
        return error_handler(UNDEFINED) ;

      if (SwFindRDR(RDR_CLASS_TIMELINE, TL_DEBUG_TYPE_NAME, (sw_rdr_id)tltype,
                    &rdr, &length) == SW_RDR_SUCCESS) {
        oName(*p1) = cachename((uint8*)rdr, (uint32)length) ;
        type = ONAME ;
      } else {
        oInteger(*p1) = tltype ;
        type = OINTEGER ;
      }
    }
    pop(&operandstack) ;
    break ;

  case NAME_SetContext:
  case NAME_GetContext:
    /* int /ID|int <any> /SetContext timelineop -> | */
    /* int /ID|int /GetContext timelineop -> <any>|null */
    /* There is a garbage collection issue here, so don't implement these yet */
    return error_handler(UNDEFINED) ;

  case NAME_GetPriority:
    /* int /GetPriority timelineop -> int */
    if (1 > theStackSize(operandstack))
      return error_handler(STACKUNDERFLOW) ;

    if (oType(*p1) != OINTEGER)
      return error_handler(TYPECHECK) ;

    {
      sw_tl_priority priority ;

      priority = SwTimelineGetPriority((sw_tl_ref)oInteger(*p1)) ;

      if (priority == SW_TL_PRIORITY_UNKNOWN)
        return error_handler(UNDEFINED) ;

      oInteger(*p1) = priority ;
    }
    pop(&operandstack) ;
    type = OINTEGER ;
    break ;

  default:
    return error_handler(UNDEFINED) ;
  }

  /* Are we returning a parameter? */
  if (type == OINVALID)
    pop(&operandstack) ;
  else {
    o = theTop(operandstack) ;
    theTags(*o) = type | LITERAL ;
    theLen(*o)  = len ;
  }

  return TRUE ;
}
#endif

/* -------------------------------------------------------------------------- */
/* note that we avoid uid=-1 as that means don't cache */
static int32 nextuniqueid = 0xFFFFFE | (UID_RANGE_temp << 24) ;

int32 getnewuniqueid(void)
{
  if ( --nextuniqueid < (UID_RANGE_temp << 24) )
    nextuniqueid += 0xFFFFFF ; /* wrap round back into range, avoiding -1 */
  return nextuniqueid;
}

Bool getnewuniqueid_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = getnewuniqueid();
  return push( &inewobj , &operandstack ) ;
}

/* -------------------------------------------------------------------------- */
/** Check the stack content against a template, generating an appropriate error
 * now, rather than halfway through a proc.
 *
 * The parameter is either a template or an array of templates. A template is
 * an array of strings (or names, which is useful in the executable array form).
 *
 * \todo Add range checking syntax for integers, reals and arrays.
 */
Bool checkparams_(ps_context_t *pscontext)
{
  OBJECT *check, *array, store = OBJECT_NOTVM_NOTHING ;
  int32  count, i, t, type, templates, result = 0, contiguous;
  Bool   simple ;

  enum {
    CP_SYNTAX,
    CP_WILDCARD,
    CP_KEY,
    CP_INTEGER,
    CP_REAL,
    CP_ARRAY,
    CP_PROC,
    CP_NOWT = 255,          /* can never match an OBJECT type */
  } ;

  /* table of allowable type(s) and special action for each mnemonic */
  static unsigned char mnemonic[] = {
    /* type,      other type,   special action */
    CP_NOWT,      CP_NOWT,      CP_WILDCARD,          /* '?' is a wildcard */
    CP_NOWT,      CP_NOWT,      CP_SYNTAX,            /* '@' is not used */
    OARRAY,       OPACKEDARRAY, CP_ARRAY,             /* 'A' = array */
    OBOOLEAN,     CP_NOWT,      CP_NOWT,              /* 'B' = boolean */
    OCPOINTER,    CP_NOWT,      CP_NOWT,              /* 'C' = C pointer */
    ODICTIONARY,  CP_NOWT,      CP_NOWT,              /* 'D' = dictionary */
    CP_NOWT,      CP_NOWT,      CP_SYNTAX,            /* 'E' is not used */
    OFILE,        CP_NOWT,      CP_NOWT,              /* 'F' = file */
    OGSTATE,      CP_NOWT,      CP_NOWT,              /* 'G' = gstate */
    OFILEOFFSET,  CP_NOWT,      CP_NOWT,              /* 'H' = "huge" number */
    OINTEGER,     CP_NOWT,      CP_INTEGER,           /* 'I' = integer */
    CP_NOWT,      CP_NOWT,      CP_SYNTAX,            /* 'J' is not used */
    ONAME,        OSTRING,      CP_KEY,               /* 'K' = key */
    OLONGSTRING,  OSTRING,      CP_NOWT,              /* 'L' = (long)string */
    OMARK,        CP_NOWT,      CP_NOWT,              /* 'M' = mark */
    ONAME,        CP_NOWT,      CP_NOWT,              /* 'N' = name */
    OOPERATOR,    CP_NOWT,      CP_NOWT,              /* 'O' = operator */
    OARRAY,       OPACKEDARRAY, CP_PROC,              /* 'P' = proc */
    OFONTID,      CP_NOWT,      CP_NOWT,              /* 'Q' = font id */
    OREAL,        OINTEGER,     CP_REAL,              /* 'R' = real/integer */
    OSTRING,      CP_NOWT,      CP_NOWT,              /* 'S' = string */
    CP_NOWT,      CP_NOWT,      CP_SYNTAX,            /* 'T' is not used */
    ONULL,        CP_NOWT,      CP_NOWT,              /* 'U' = null */
    OSAVE,        CP_NOWT,      CP_NOWT,              /* 'V' = save */
    CP_NOWT,      CP_NOWT,      CP_SYNTAX,            /* 'W' is not used */
    OINDIRECT,    CP_NOWT,      CP_NOWT,              /* 'X' = indirected */
    OINFINITY,    CP_NOWT,      CP_NOWT,              /* 'Y' = infinity */
    ONOTHING,     CP_NOWT,      CP_NOWT,              /* 'Z' = nothing */
  } ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* checkparams takes one parameter */
  if ( 0 > theStackSize(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  /* which must be an array - it can be an executable array of names though */
  check = theTop(operandstack) ;
  if (oType(*check) != OARRAY && oType(*check) != OPACKEDARRAY)
    return error_handler(TYPECHECK) ;

  /* Ensure we have a copy in case we detect a syntax error having popped the
     parameter when it was the last in a stack frame */
  Copy(&store, check) ;
  check = &store ;
  count = theLen(*check) ;
  array = oArray(*check) ;

  /* the array must not be empty, and the last element is the 'operator' name */
  if (count < 1 || oType(array[count-1]) != ONAME ||
      ((type = oType(array[0])) != OSTRING && type != ONAME &&
        type != OARRAY && type != OPACKEDARRAY) )
    return error_handler(TYPECHECK) ;

  /* all other entries must be of the same type (string/name or packed/array) */
  simple = (type != OARRAY && type != OPACKEDARRAY) ;
  for (i = 1; i < count-1; i++) {
    int32 t = oType(array[i]) ;
    switch (t) {
    case OSTRING:
    case ONAME:
      if (!simple)
        return error_handler(TYPECHECK) ;
      break ;
    case OARRAY:
    case OPACKEDARRAY:
      if (simple)
        return error_handler(TYPECHECK) ;
    }
  }

  /* Any further errors will probably be the parameters.
   * i.e. otherwise goto syntax_error,
   */
  pop(&operandstack) ;

  /* The number of objects in the top stack frame */
  /* NB This requires FRAMESIZE to be a power of two */
  contiguous = contiguousStackSize(operandstack) ;

  /* The number of templates we have been given */
  templates = (simple) ? 1 : count-1 ;

  result = TYPECHECK ;
  for (t = 0; t < templates && result; t++) {
    OBJECT *tmplt ;
    int32 params ;

    /* for each template */
    if (simple) {
      tmplt = array ;
      params = count-1 ;
    } else {
      tmplt = oArray(array[t]) ;
      params = theLen(array[t]) ;
    }

    /* first check there are enough parameters on the stack for this template */
    if ( params-1 > theStackSize(operandstack) )
      result = STACKUNDERFLOW ;
    else {
      /* enough parameters on the stack, so check each parameter */
      result = 0 ;  /* once a parameter fails to match, check no more */

      for (i = 0; i < params && result == 0; i++) {
        unsigned char * s;
        int32 len, ptype, down = params-1 - i ;
        OBJECT* param ;

        /* for each parameter, check against the parameter template string */
        if (down > contiguous)
          param = stackindex(down, &operandstack) ;
        else
          param = theTop(operandstack) - down ;
        ptype = oType(*param) ;

        /* we must check each element of the template string, so find it */
        switch (oType(tmplt[i])) {
        case OSTRING:  /* eg [(I)(SN)(A)/op] checkparams */
          s = oString(tmplt[i]) ;
          len = theLen(tmplt[i]) ;
          break ;
        case ONAME:    /* eg {I SN A op} checkparams */
          s = theICList(oName(tmplt[i])) ;
          len = theINLen(oName(tmplt[i])) ;
          break ;
        default:
          s = 0 ;  /* shut compiler up */
          len = 0 ;
        }
        if (len < 1)
          goto syntax_error ;

        /* the commonest case, and keep checking until we match */
        result = TYPECHECK;
        while (len-- && result) {
          int c ;

          while ((c = *s++) == 32 && len--) ;
          if (len < 0)
            break ;
          c = toupper(c) ;

          /** Ranged Integers and Reals have an optional mnemonic,
           * so if 'c' is a digit or minus, infer I or R from the number
           */
          c = (c - '?')*3 ;         /* index into mnemonic[] */
          if ( c < 0 || c >= sizeof(mnemonic) || mnemonic[c+2] == CP_SYNTAX )
            goto syntax_error ;

          /* check type is acceptable, or if it is a wildcard */
          if ( ptype != mnemonic[c] && ptype != mnemonic[c+1] &&
               mnemonic[c+2] != CP_WILDCARD && mnemonic[c+2] != CP_KEY) {
            /* This is not a match, so skip to the next mnemonic.
             */
            /** \todo when range syntax is supported for arrays etc, the range
             * will have to be skipped if present.
             */
            continue ;
          }

          /* now perform any special action (like check range) */
          switch (mnemonic[c+2]) {
          case CP_PROC:
            /* only executable arrays are procs */
            if (oCanExec(*param))
              result = 0 ;
            break ;
          case CP_KEY:
            /* any type that is legal as a key */
            if (ptype != ONULL && ptype != ONOTHING)
              result = 0 ;
            break ;
          case CP_INTEGER:
          case CP_REAL:
          case CP_ARRAY:
            /** \todo parse optional range syntax and return RANGECHECK if bad.
             * For now, drop through into outright success.
             */
          case CP_NOWT:
          case CP_WILDCARD:
            result = 0 ;            /* succesful match */
            break ;
          default:
            HQFAIL("Error in checkparams mnemonic array") ;
            goto syntax_error ;
          } /* special action */
        } /* while len and result */
      } /* for i params */
    } /* for t templates */
  }

  if (result) {
    /* if returning an error, then ensure it is correctly attributed */
    errobject = array[count-1] ;
    theTags(errobject) &= ~EXECUTABLE ;  /* turn 'op' back into  '/op' */
    return error_handler(result) ;
  }
  return TRUE ;

syntax_error:
  /* This is an error with checkparams, and not with the parameters it is
   * checking, so push our parameter back on the stack. We raise a syntax
   * error here so that it is distinct from the RANGECHECK and TYPECHECK
   * errors we raise above.
   */
  push(check, &operandstack);
  return error_handler(SYNTAXERROR);
}

/* -------------------------------------------------------------------------- */
Bool bind_(ps_context_t *pscontext)
{
  int32 ssize ;
  OBJECT *proc ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  proc = TopStack( operandstack , ssize ) ;

  if ( oType(*proc) != OARRAY && oType(*proc) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  return bind_internal(ps_core_context(pscontext), proc ) ;
}

/* -------------------------------------------------------------------------- */
/* This routine implements internal automatic binding. This has the side effect
 * of fixing the execution of a procedure that is used in a callback on the
 * first use of said procedure. It also has the side effect of doing idiom
 * recognition on it, and further, of doing a callback if the idiom has the
 * relevant Harlequin extension.
 */
Bool bind_automatically(corecontext_t *context, OBJECT *proc )
{
  int32 ssize ;
  OBJECT *theo ;

  HQASSERT( proc != NULL , "proc NULL in bind_auto" ) ;
  HQASSERT( oType(*proc) == OARRAY || oType(*proc) == OPACKEDARRAY ,
            "proc not a procedure" ) ;
  HQASSERT( context->userparams->AutomaticBinding ,
            "calling bind_automatically but not AutomaticBinding" ) ;

  /* The object MUST be on the operand stack for idiom replacement to work
   * (with the callback variant).
   */
  if ( ! push( proc , & operandstack ))
    return FALSE ;

  /* Can call bind_internal with old proc pointer. */
  if ( ! bind_internal(context, proc )) {
    /* Deliberately don't pop the arg on failure. */
    return FALSE ;
  }

  /* Check that we've got a procedure back... */
  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;

  if ( oType(*theo) != OARRAY && oType(*theo) != OPACKEDARRAY )
    return error_handler( TYPECHECK ) ;

  Copy( proc , theo ) ;
  pop( & operandstack ) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool bind_internal(corecontext_t *context, OBJECT *proc )
{
  HQASSERT( proc != NULL , "proc NULL in bind_internal" ) ;
  HQASSERT( oType(*proc) == OARRAY || oType(*proc) == OPACKEDARRAY ,
            "proc not a procedure" ) ;

  if (( theISaveLangLevel( workingsave ) >= 3 ) &&
      context->userparams->IdiomRecognition ) {
    idiom_resethash() ;

    if ( bind_recurse( proc , TRUE ))
      return idiom_replace( proc ) ;
    else
      return FALSE ;
  }
  else
    return bind_recurse( proc , FALSE ) ;
}

/* -------------------------------------------------------------------------- */
/* Recursive function which does the work of the bind operator. Bizarrely, the
 * Red Book defines that every sub-procedure is reduced to read-only access
 * after it has been bound, but the top-level procedure stays as it was.
 */
static Bool bind_recurse( OBJECT *proc , int32 ir )
{
  int32 glmode ;
  int32 len ;
  OBJECT *alist ;
  uint8 tags ;
  int32 fConstantArray;
  corecontext_t *corecontext = get_core_context_interp() ;

  HQASSERT( proc != NULL , "proc NULL in bind_recurse" ) ;
  HQASSERT( oType(*proc) == OARRAY || oType(*proc) == OPACKEDARRAY ,
            "proc not a procedure" ) ;

  len = theLen(*proc) ;
  if ( len == 0 )
    return TRUE ;

  /* Exit early if the array is read only etc. This used to be done
   * in the loop below before recursing: simplifies bind_ this way
   * and more importantly allows continued evaluation of the idiom
   * recognition hash when bind has given up.
   */
  tags = theTags(*proc) ;
  switch ( oType(*proc)) {
  case OARRAY :
    if ( !oCanWrite(*proc) ) {
      if ( ir )
        idiom_calchash( proc ) ;
      return TRUE ;
    }
    break ;

  case OPACKEDARRAY :
    if ( !oCanRead(*proc) ) {
      if ( ir )
        idiom_calchash( proc ) ;
      return TRUE ;
    }
    break ;

  default:
    HQFAIL( "bind_recurse should only be called with procedures." ) ;
    return error_handler( TYPECHECK ) ;
  }

  HQASSERT( !NOTVMOBJECT(oArray(*proc)[0]) , "Can't bind a non VM object" ) ;

  if ( ir )
    idiom_inchashdepth() ;

  theTags(*proc) = (uint8)(NO_ACCESS | (theTags(*proc) & (ETYPEMASK|EXECMASK))) ;
  alist = oArray(*proc) ;
  glmode = oGlobalValue(*proc) ;
  if ( ! check_asave(alist, len, glmode, corecontext)) {
    theTags(*proc) = tags ;
    return FALSE ;
  }

  alist += ( len - 1 ) ;

  fConstantArray = TRUE;
  while ( len-- > 0 ) {
    register uint8 tags = theTags(*alist) ;

    switch ( oType(*alist) ) {
    case OARRAY :
      fConstantArray = FALSE;
      if ( oExecutable(*alist) ) {
        if ( bind_recurse( alist , ir )) {
          if ( oCanWrite(*alist) )
            theTags(*alist) = ( uint8 )
              ( READ_ONLY | (theTags(*alist) & (ETYPEMASK|EXECMASK))) ;
        }
        else {
          theTags(*proc) = tags ;
          return FALSE ;
        }
      }
      else if ( ir )
        idiom_calchash( proc ) ;
      break ;

    case OPACKEDARRAY :
      fConstantArray = FALSE;
      if ( oExecutable(*alist) ) {
        if ( ! bind_recurse( alist , ir )) {
          theTags(*proc) = tags ;
          return FALSE ;
        }
      }
      else if ( ir )
        idiom_calchash( proc ) ;
      break ;

    case OINTEGER:
    case OREAL:
    case OSTRING:
      if ( ir )
        idiom_updatehash( alist ) ;
      break;

    case ONAME :
      {
        register OBJECT *ares ;

        if ( ( ares = name_is_operator( alist ) ) != NULL ) {
          Copy(alist, ares) ;
        }
      }
      /* Drop through */

    default:
      fConstantArray = FALSE;
      if ( ir )
        idiom_updatehash( alist ) ;
      break ;
    }

    --alist ;
  }

  theTags(*proc) = tags ;

  if ( ir )
    idiom_dechashdepth(fConstantArray) ;

  return TRUE ;
}


/**
 * Check to see if the given name maps to an operator when looked-up in
 * the context of the current dictionary stack.
 *
 * Use the namecache to avoid having to do a full dictionary search.
 * If the namecache marks the name as unique to some dictionary, we just
 * have to check to see if that dictionary is on the dict stack and we are
 * done. Otherwise, the namecache may mark the name as not being defined in
 * any dictionary, so there is no point searching at all. Only if neither
 * of these special cases is true (i.e. the name is defined in more than
 * one dictionary) do we have to do the full dictstack search.
 */
OBJECT *name_is_operator(OBJECT *name)
{
  int32 anindex;
  int32 nodicts;
  OBJECT *ares;

  HQASSERT(name, "Null name in name_is_operator.");

  if ( oExecutable(*name) )
  {
    NAMECACHE *kname = oName(*name);

    nodicts = theStackSize(dictstack) - 1;

    if ( kname->dictobj ) /* uniquely defined somewhere... */
    {
      /* Check systemdict first, as this is the most likely */
      if ( kname->dictobj == oDict(systemdict) )
      {
        ares = kname->dictval;
        if ( oType(*ares) == OOPERATOR )
          return ares;
        else
          return NULL;
      }
      /* Otherwise search through the dictstack */
      for ( anindex = 0 ; anindex <= nodicts+1; ++anindex )
      {
        const OBJECT *thed = stackindex(anindex, &dictstack);
        thed = oDict(*thed);

        if ( kname->dictobj == thed )
        {
          ares = kname->dictval;
          if ( oType(*ares) == OOPERATOR )
            return ares;
          else
            return NULL;
        }
      }
      /* Fall through if its unique but not in any dict on the stack */
      return NULL;
    }
    else if ( kname->dictval == NULL ) /* Name not in any dict */
      return NULL;

    /* Get here if name is not unique, so have to search through dicts
     * to see which definition we find first.
     */
    for ( anindex = 0 ; anindex < nodicts ; ++anindex ) {
      if (( ares = fast_extract_hash( stackindex( anindex , & dictstack ) ,
                                      name )) != NULL ) {
        if ( oType(*ares) == OOPERATOR )
          return ares ;
        else
          break ;
      }
    }
    if ( anindex == nodicts ) {
      if (( ares = fast_user_extract_hash( name )) != NULL ) {
        if ( oType(*ares) == OOPERATOR )
          return ares ;
        else
          return NULL ;
      }
      if (( ares = fast_sys_extract_hash( name )) != NULL ) {
        if ( oType(*ares) == OOPERATOR )
          return ares ;
        else
          return NULL ;
      }
    }
  }
  return NULL ;
}

/* For the null_() operator see file 'setup.c' */

/* ---------------------------------------------------------------------- */

Bool serialnumber_(ps_context_t *pscontext)
{
  MISCHOOKPARAMS *mischookparams = ps_core_context(pscontext)->mischookparams;

  /* part of the EP2000 preflight checks. Must know whether serialnumber has been
   * called to figure out whether fonts being downloaded are copy protected.
   * NOTE: Called *before* getting the number so the proc can't fudge the op stack.
   */
  if ( oType(mischookparams->SecurityChecked) != ONULL ) {
    if ( ! push( & mischookparams->SecurityChecked ,
                 & executionstack ))
      return FALSE ;
    if ( ! interpreter( 1 , NULL ))
      return FALSE;
  }
  oInteger(inewobj) = DongleSecurityNo();
  return push( & inewobj , & operandstack ) ;
}

/* ---------------------------------------------------------------------- */

static uint32 rtime = 0 , utime = 0 ;

void initUserRealTime( void )
{
  utime = get_utime() ;
  rtime = get_rtime() ;
}

Bool realtime_(ps_context_t *pscontext)
{
  uint32 lrtime = get_rtime() ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = lrtime - rtime ;
  return push( & inewobj , & operandstack );
}

/* ----------------------------------------------------------------------------
   function:            usertime_()        author:              Andrew Cave
   creation date:       13-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See PostScript reference manual page 237.

---------------------------------------------------------------------------- */

Bool usertime_(ps_context_t *pscontext)
{
  uint32 lutime = get_utime() ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = lutime - utime ;
  return push( & inewobj , & operandstack );
}


/* ----------------------------------------------------------------------------
   function:            daytime_()        author:              Andrew Cave
   creation date:       13-Oct-1987        last modification:   ##-###-####
   arguments:           none .
   description:

   See NTX reference manual page ???.

---------------------------------------------------------------------------- */

static Bool getdaytime(corecontext_t *context, int32 fLocal )
{
  static uint8 swtime[64];
  int32 len;

  (void)strcpy( (char *)swtime, (char *)get_date( fLocal ));
  len = strlen_int32( (char *)swtime );
  theLen( snewobj ) = (uint16) len ;
  oString(snewobj) = ( uint8 * )swtime ;

  UNUSED_PARAM(corecontext_t *, context);

  return push( & snewobj , & operandstack ) ;
}

Bool daytime_(ps_context_t *pscontext)
{
  return getdaytime(ps_core_context(pscontext), FALSE ) ;
}


/* ----------------------------------------------------------------------------
   function:            localedaytime_()   author:              Mike Richmond
   creation date:       18-Feb-1997        last modification:   ##-###-####
   arguments:           none .
   description:

   As daytime_(), but returns a string in the current locale

---------------------------------------------------------------------------- */

Bool localedaytime_(ps_context_t *pscontext)
{
  return getdaytime(ps_core_context(pscontext), TRUE ) ;
}

/* ----------------------------------------------------------------------------
   function:            cexec_(..)         author:              Andrew Cave
   creation date:       13-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   Simulates the cexec operator, by throwing away its arguments.

---------------------------------------------------------------------------- */
Bool cexec_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            eexec_(..)         author:              Andrew Cave
   creation date:       13-Oct-1987        last modification:   ##-###-####
   arguments:           none.
   description:

   Pushes an EEXECFILE onto the execution stack using the given file.
   On the fly decodes the eexec stuff.
   Beware that :
   1. No recursive eexec's currently catered for.
   2. No eexec on %lineedit, %statementedit files.
   3. exit from eexec causes error.
   4. P.S. operator read (etc...) currently de'eexecs.

---------------------------------------------------------------------------- */

static int32 eexecFilterClose( register FILELIST *flptr, int32 flag )
{
  int32 result = 0 ;
  OBJECT * theo;

  HQASSERT( flptr , "flptr NULL in eexecFilterClose." ) ;

  result = FilterCloseFile(flptr, flag) ;

  theo = theTop( dictstack ) ;
  if ( oDict(*theo) == oDict(systemdict))
    if ( ! end_(get_core_context_interp()->pscontext))
      result = EOF ;

  return result ;
}

Bool eexec_(ps_context_t *pscontext)
{
  int32 ch ;
  OBJECT *theo ;
  FILELIST *flptr , *nflptr ;
  OBJECT fileo = OBJECT_NOTVM_NOTHING ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OFILE &&
       oType(*theo) != OSTRING
#if 0
       /* We could allow a procedure source/target to eexec by uncommenting
          these checks. */
       && oType(*theo) != OARRAY
       && oType(*theo) != OPACKEDARRAY
#endif
       )
    return error_handler( TYPECHECK) ;

  if ( ! oCanRead(*theo))
    if ( ! object_access_override(theo))
      return error_handler( INVALIDACCESS ) ;

  if ( oType(*theo) == OSTRING ||
       oType(*theo) == OARRAY ||
       oType(*theo) == OPACKEDARRAY ) {
    OBJECT subfile = OBJECT_NOTVM_NAME(NAME_SubFileDecode, LITERAL) ;

    oInteger(inewobj) = 0 ;
    theLen(snewobj) = 0 ;
    oString(snewobj) = NULL ;

    if ( ! push3(&inewobj, &snewobj, &subfile, &operandstack) ||
         ! filter_(pscontext) )
      return FALSE ;

    if ( isEmpty( operandstack ))
      return error_handler( STACKUNDERFLOW ) ;

    theo = theTop( operandstack ) ;
  }

  HQASSERT(oType(*theo) == OFILE, "No file object for eexec") ;

  flptr = oFile(*theo) ;
  /* it is valid to check isIInputFile before checking for dead filter (in
     isIOpenFileFilter) */
  if ( ! isIInputFile( flptr ))
    return error_handler( IOERROR ) ;
  if ( ! isIOpenFileFilter( theo , flptr )) { /* Implies that associated file is closed. */
    Copy( theo , (& fnewobj)) ;
    return TRUE ;
  }

  nflptr = filter_standard_find(NAME_AND_LENGTH("EExecDecode")) ;
  HQASSERT(nflptr, "No EExecDecode filter found") ;
  HQASSERT(isIFilter(nflptr), "EExecDecode is not a filter") ;

  {
    uint8 tags = theTags(*theo) ;

    /* Initialisation of filter removes file from stack */
    if ( ! filter_create_object(nflptr, &fileo, NULL, &operandstack) )
      return FALSE ;

    theTags(fileo) = tags ; /* Tags are same as original file */
    nflptr = oFile(fileo) ;   /* Get instance of filter for special close */
  }

  /* Skip any white space. */
  do {
    ch = Getc( flptr ) ;  /* Note: flptr not nflptr. */
    if ( ch == EOF )
      return (*theIFileLastError( flptr ))( flptr ) ;
  } while (( ch == ' ' ) || ( ch == LF ) ||
           ( ch == CR ) || ( ch == '\t' )) ;

  UnGetc( ch, flptr );    /* rewind to reread this character again */

  /* Push systemdict onto the dictionary stack as required by the spec. */
  if ( !begininternal(& systemdict) )
    return FALSE ;

  /* Now install special close filter, which removes systemdict from
     dictstack, and make eexec filter currentfile. */
  theIMyCloseFile(nflptr) = eexecFilterClose ;

  currfileCache = NULL ; /* currentfile has changed */
  execStackSizeNotChanged = FALSE ;
  return push(&fileo, &executionstack) ;
}

/* ----------------------------------------------------------------------------
   function:            internaldict()       author:              Andrew Cave
   creation date:       22-Oct-1987          last modification:   ##-###-####
   arguments:           none .
   description:

   If passwd correct, return internaldict.

---------------------------------------------------------------------------- */
Bool internaldict_(ps_context_t *pscontext)
{
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

/*  The passwd must be an integer.  */
  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;

#if defined( DEBUG_BUILD )
  if ( oInteger(*theo) != 0 )
#endif
    if ( oInteger(*theo) != 1183615869 )
      return error_handler( INVALIDACCESS ) ;

  Copy( theo , (& internaldict)) ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            useinternaldictop author:              Luke Tunmer
   creation date:       16-Aug-1991       last modification:   ##-###-####
   arguments:
   description:

   This "operator" extracts the name of the operator that called it from the
   errobject variable and looks it up in the internaldict. It pushes that
   value onto the execution stack.

---------------------------------------------------------------------------- */
Bool useinternaldictop_(ps_context_t *pscontext)
{
  OBJECT *theo ;

  if ( oType(errobject) != OOPERATOR )
    return error_handler( UNREGISTERED ) ;

  oName(nnewobj) = theIOpName(oOp(errobject)) ;
  if ( NULL == ( theo = fast_extract_hash( &internaldict , &nnewobj )))
    return error_handler( UNDEFINED ) ;

  if ( ! push( theo, &operandstack))
    return FALSE ;
  return myexec_(pscontext) ;
}

/* ---------------------------------------------------------------------- */
/* intercepts: makeshadowop takes a dictionary full of procedures (or other
   executable objects). It puts these into shadowproc, and at the same time
   takes a copy of the operator of the same name into shadowop. It then
   replaces the opcall of the real operator with a call to useshadowop_.
   That then calls the users procedure from shadowproc; any operators called
   within there are the real ones. This way we can redefine operators,
   but still have them look like operators */

static Bool callrealoperator = FALSE;
Bool useshadowop_(ps_context_t *pscontext)
{
  Bool result;
  register OBJECT *theproc ;
  register int32 opindex;

  /* we want to call the intercept procedure; during that, any operator
     should call the real one - but we will still end up here. However, we
     know that we have been here so we can divert to call the real one the
     second time. Also if we find the entry isn't present in the
     dictionary, it's probably been restored away so we'll just reinstate
     things */

  oName(nnewobj) = theIOpName(oOp(errobject)) ;

  if ( callrealoperator || CURRENT_DEVICE() == DEVICE_CHAR )
    return (* shadowop[oNameNumber(nnewobj)])(pscontext);

  theproc = fast_extract_hash( shadowproc , &nnewobj );
  if (theproc == NULL) {
    /* it has been restored away or otherwise got rid of */
    opindex = oNameNumber(nnewobj);
    theIOpCall(&system_ops[opindex]) = shadowop[opindex];
    return (* shadowop[opindex])(pscontext);
  }

  /* temporarily call the real operator */
  if ( oType(*theproc) == ONULL)
    return (* shadowop[oNameNumber(nnewobj)])(pscontext);

  /* call the users shadow procedure instead */
  if (! push (theproc, & executionstack))
    return FALSE;

  gc_safe_in_this_operator();
  callrealoperator = TRUE; /* for any recursive call */
  result = interpreter(1, NULL);
  callrealoperator = FALSE;
  return result;
}

/* ---------------------------------------------------------------------- */

void defshadowproc(register OBJECT * o1, /* a name */
                   register OBJECT ** o2p) /* replacement */
{
  register OBJECT *theproc ;

  /* check if the name is in shadowproc; if it is replace o1p with its
     value */
  if ( oType(*o1) != ONAME)
    return;

  theproc = fast_extract_hash( shadowproc , o1 );
  if (theproc == NULL) {
    /* it has been restored away or otherwise got rid of */
    theIOpClass(oName(*o1)) &= ~ SHADOWOP;
    return;
  }

  if ( oType(*theproc) == ONULL)
    return;

  /* replace o2p with a pointer to the users shadow procedure instead */
  * o2p = theproc;
  return;
}

/* ---------------------------------------------------------------------- */
static Bool checkoneshadowop( OBJECT *thekey, OBJECT *theo,
                              void *argsBlockPtr )
{
  register int32 opindex;

  /* check that the name of the shadowed operator/procedure is
   * not one of the disallowed ones
   */

  if ( oType(*thekey) != ONAME )
    return error_handler(UNDEFINEDRESULT);

  if ( theIOpClass(oName(*thekey)) & CANNOTSHADOWOP )
    return error_handler(INVALIDACCESS);

  opindex = oNameNumber(*thekey);
  if ( argsBlockPtr != NULL ) {
    /* Here for shadowop: can't shadowop an already define'd op */
    if ( defineproc )
      if ( fast_extract_hash( defineproc , thekey ) != NULL )
        return error_handler( INVALIDACCESS ) ;
  }
  else {
    /* Here for defineop: can't defineop an already existing op */
    if ( opindex >= 0 && opindex < OPS_COUNTED )
      return error_handler (INVALIDACCESS);
  }

  /* check theo is an executable object of a suitable type */
  switch ( oType(*theo)) {
  case OARRAY :
  case OSTRING :
  case OPACKEDARRAY :
  case OFILE :
    if ( ! oExecutable(*theo))
      return error_handler (INVALIDACCESS);
    if ( ! oCanExec(*theo) && !object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
    if ( oType(*theo) == OFILE  &&
        ! isIInputFile(oFile(*theo)) )
      return error_handler( INVALIDACCESS ) ;
    break;
  case ONAME :
  case OOPERATOR :
    if ( ! oExecutable(*theo) )
      return error_handler(INVALIDACCESS);
    break;
  case ONULL:
    /* cancel the effect - doesn't need to be executable */
    break;
  default:
    return error_handler (TYPECHECK);
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool makeoneshadowop( OBJECT *thekey, OBJECT *theo,
                             void *argsBlockPtr )
{
  register int32 opindex;

  UNUSED_PARAM(void *, argsBlockPtr);

  /* replace the call in the real operator to one to useshadowop_
     (this may have already been done, but that doesnt matter) */
  opindex = oNameNumber(*thekey);
  if ( opindex >= 0 && opindex < OPS_COUNTED &&
       system_ops[opindex].opcall != NULL ) {
    theIOpCall(&system_ops[opindex]) = useshadowop_;
  } else {
    HQASSERT((theIOpClass(oName(*thekey)) & OTHERDEFOP) == 0,
             "SHADOWOP could prevent other def operations occuring") ;
    theIOpClass(oName(*thekey)) |= SHADOWOP;
  }

  /* put the user procedure in shadowproc dictionary; if this is null it
     will call the ordinary operator, but via the intercept mechanism */
  return insert_hash (shadowproc, thekey, theo);
}

/* ---------------------------------------------------------------------- */
Bool makeshadowop_(ps_context_t *pscontext)
{
  register OBJECT *theo ;
  register int32 i;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* takes a dictionary. For each element:
     if the name is present in shadowproc in internaldict
       if value is null
         restore sthe status quo
       else replace it with the value given
     if not, add it, and copy the operator pointer into shadowop
  */

  if (shadowop == NULL) {
    shadowproc = fast_extract_hash_name( &internaldict , NAME_shadowproc );
    if (shadowproc == NULL)
      return error_handler (UNDEFINED);
    /* make a duplicate of the table of operator pointers to hang on to
       the originals */
    shadowop = (OPFUNCTION *) mm_alloc_with_header(mm_pool_temp,
                                                   OPS_COUNTED * sizeof(OPFUNCTION),
                                                   MM_ALLOC_CLASS_SHADOWOP);
    if (shadowop == NULL)
      return error_handler(VMERROR);
    for (i = 0; i < OPS_COUNTED; i++) {
      shadowop[i] = theIOpCall(&system_ops[i]);
    }
  }

  theo = theTop(operandstack);
  if ( oType(*theo) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead(*oDict(*theo)) )
    if ( ! object_access_override(oDict(*theo)) )
      return error_handler( INVALIDACCESS ) ;

  if (! walk_dictionary(theo, checkoneshadowop, theo))
    return FALSE;
  if (! walk_dictionary(theo, makeoneshadowop, NULL))
    return FALSE;
  pop(& operandstack);
  return TRUE;
}

Bool usedefineop_(ps_context_t *pscontext)
{
  register OBJECT *theproc ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oName(nnewobj) = theIOpName(oOp(errobject)) ;
  theproc = fast_extract_hash( defineproc , &nnewobj );
  if (theproc == NULL)
    /* it has been restored away or otherwise got rid of */
    return error_handler( UNREGISTERED ) ;

  /* call the users define procedure instead */
  if (! push(theproc, & executionstack))
    return FALSE;
  return interpreter(1, NULL);
}

/* ---------------------------------------------------------------------- */
static Bool makeonedefineop( OBJECT *thekey, OBJECT *theo,
                             void *argsBlockPtr )
{
  Bool      result ;
  OPERATOR *newop ;
  OBJECT    newobj = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(void *, argsBlockPtr);

  if ( oType(*theo) != ONULL ) {
    /* Make a real operator */
    if ( NULL == ( newop = ( OPERATOR * )get_gsmemory( sizeof( OPERATOR ))))
      return error_handler(VMERROR) ;
    theIOpName(newop) = oName(*thekey) ;
    theIOpCall(newop) = (oType(*theo) == OOPERATOR ?
                         theIOpCall(oOp(*theo)) :
                         usedefineop_) ;

    theTags( newobj ) = OOPERATOR | EXECUTABLE ;
    SETGLOBJECTTO(newobj, TRUE) ;
    oOp(newobj) = newop ;

    /* Insert into defineproc */
    if ( ! insert_hash( defineproc , thekey , theo ))
      return FALSE ;
  }

  /* Insert into (or remove from) systemdict */
  if ( oType(*theo) == ONULL )
    result = remove_hash(&systemdict, thekey, FALSE) ;
  else
    result = insert_hash_even_if_readonly(&systemdict, thekey, &newobj) ;

  return  result  ;
}

/* ---------------------------------------------------------------------- */
Bool makedefineop_(ps_context_t *pscontext)
{
  register OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* takes a dictionary. For each element:
     if the name is present in defineproc in internaldict
       if value is null
         restore sthe status quo
       else replace it with the value given
     if not, add it, and copy the operator pointer into defineop
  */

  if (defineproc == NULL) {
    defineproc = fast_extract_hash_name( &internaldict , NAME_defineproc );
    if (defineproc == NULL)
      return error_handler (UNDEFINED);
  }

  theo = theTop (operandstack);
  if ( oType(*theo) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead(*oDict(*theo)) )
    if ( ! object_access_override(oDict(*theo)) )
      return error_handler( INVALIDACCESS ) ;

  if (! walk_dictionary(theo, checkoneshadowop, NULL))
    return FALSE;
  if (! walk_dictionary(theo, makeonedefineop, NULL))
    return FALSE;
  pop (& operandstack);
  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            superexec_()         author:              Andrew Cave
   creation date:       22-Oct-1987          last modification:   ##-###-####
   arguments:           none .
   description:

   Similar to exec, but like going super user.

---------------------------------------------------------------------------- */
Bool superexec_(ps_context_t *pscontext)
{
  OBJECT *theo ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( ! oExecutable(*theo))
    return TRUE ;

  switch ( oType(*theo) ) {
  case OARRAY :
  case OSTRING :
  case OFILE :
  case OPACKEDARRAY :
    if ( ! oCanExec(*theo) )
      if ( ! object_access_override(theo) )
        return error_handler( INVALIDACCESS ) ;
    if ( oType(*theo) == OFILE ) {
      currfileCache = NULL ;
      if ( ! isIInputFile(oFile(*theo)) )
        return error_handler( IOERROR ) ;
    }
    /*@fallthrough@*/
  case ONAME :
  case OOPERATOR :
    if ( ! xsetup_operator( NAME_xsuperexec  , 0 ))
      return FALSE ;
    if ( ! push( theo , & executionstack ))
      return FALSE ;
    /*@fallthrough@*/
  case ONULL :
    pop( & operandstack ) ;
    /*@fallthrough@*/
  default:
    return TRUE ;
  }
}

/* ----------------------------------------------------------------------------
   function:            xsuperexec()         author:              Andrew Cave
   creation date:       22-Oct-1987          last modification:   ##-###-####
   arguments:           none .
   description:

   Finishes off a super exec.

---------------------------------------------------------------------------- */
Bool xsuperexec(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  pop( & executionstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            in_super_exec()      author:              Andrew Cave
   creation date:       22-Oct-1987          last modification:   ##-###-####
   arguments:           none .
   description:

   Check if object permissions can be overridden; possible if in a superexec
   level.

---------------------------------------------------------------------------- */
Bool in_super_exec(void)
{
  register int32 i , l ;
  register OBJECT *theo ;

  l = theStackSize( executionstack ) ;
  for ( i = 0 ; i <= l ; ++i ) {
    theo = stackindex( i , & executionstack ) ;
    if ( oType(*theo) == ONOTHING )
      if ( oOp(*theo) == & system_ops[NAME_xsuperexec] )
        return TRUE ;
  }
  return FALSE ;
}

/* Ever wished you could do arbitrary bits of PS from the debugger,
   for example to look at dictionaries? Well now you can, just put
   run_ps_string("whatever ps you like") in the quick watch window
   and watch it execute.
 */

Bool run_ps_string_len(
  uint8*  ps,
  int32   len)
{
  OBJECT os = OBJECT_NOTVM_NOTHING;

  theTags(os) = OSTRING|EXECUTABLE|READ_ONLY;
  theLen(os) = CAST_TO_UINT16(len);
  oString(os) = ps;
  SETGLOBJECTTO(os, FALSE);

  return setup_pending_exec(&os, TRUE /* exec immediately */) ;
}

Bool run_ps_string(uint8* ps)
{
  return run_ps_string_len(ps, strlen_int32((char*)ps)) ;
}


/* ----------------------------------------------------------------------------
   function:            abcop_()             author:              Mike Williams
   creation date:       10-Feb-1997          last modification:   ##-###-####
   arguments:           none .
   description:

   Linear interpolation operator for CRD indexing procedures in EncodeABC
   Note: operator clips to lower or upper bounds rather than extrapolate.

---------------------------------------------------------------------------- */
Bool abcop_(ps_context_t *pscontext)
{
  register int32       alen;
  OBJECT*              top;
  OBJECT*              valo;
  register OBJECT*     alist;
  register SYSTEMVALUE val;
  SYSTEMVALUE lowcol;
  SYSTEMVALUE highcol;
  SYSTEMVALUE lowidx;
  SYSTEMVALUE highidx;
  SYSTEMVALUE res;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Operator requires two items on the stack */
  if ( 1 > theStackSize(operandstack) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  /* Check top of stack is an array */
  top = theTop(operandstack);
  switch ( oType(*top) ) {
  case OARRAY:
  case OPACKEDARRAY:
    break;
  default:
    return error_handler(TYPECHECK) ;
  }

  /* Check other argument is a real and get it */
  valo = &top[-1];
  if ( !fastStackAccess(operandstack) ) {
    valo = stackindex(1, &operandstack);
  }

  if ( OREAL != oType(*valo) ) {
    return error_handler(TYPECHECK) ;
  }

  val = (SYSTEMVALUE)oReal(*valo);

  /* Check that we have an even number and 4 or more entries in the array */
  alist = oArray(*top);
  alen = (int32)theLen(*top);

  if ( ((alen & 1) == 1) || (alen < 4) ) {
    return error_handler(RANGECHECK) ;
  }

  /* Get the initial low end of the interpolator */
  if ( !object_get_numeric(alist++, &lowcol) ||
       !object_get_numeric(alist++, &lowidx) )
    return FALSE ;

  if ( val < lowcol ) {
    /* Clip to lower index */
    pop(&operandstack);
    oReal(*valo) = (USERVALUE)lowidx;
    return TRUE ;
  }

  /* Loop over pairs to find interval to interpolate in */
  while ( (alen -= 2) > 0 ) {

    /* Find interval upper colour value */
    if ( !object_get_numeric(alist++, &highcol) )
      return FALSE ;

    /* Ensure colour values monotonic increasing */
    if ( highcol < lowcol ) {
      return error_handler(RANGECHECK) ;
    }

    /* Find interval upper index value */
    if ( !object_get_numeric(alist++, &highidx) )
      return FALSE ;

    if ( (val >= lowcol) && (val <= highcol) ) {
      /* Found the interval - do interpolation */
      res = ((val - lowcol)/(highcol - lowcol))*(highidx - lowidx) + lowidx;

      /* Consume the interpolation array and return interpolated index value */
      pop(&operandstack);
      oReal(*valo) = (USERVALUE)res;
      return TRUE ;

    } else { /* Use range upper values as next range lower values */
      lowcol = highcol;
      lowidx = highidx;
    }
  }

  /* Consume the interpolation array and return last index value */
  pop(&operandstack);
  oReal(*valo) = (USERVALUE)lowidx;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            interpop_()          author:              John Jefferies
   creation date:       01-Dec-1997          last modification:   ##-###-####
   arguments:           none .
   description:

   Linear interpolation operator for CRD indexing procedures in EncodeABC
   Note: operator clips to lower or upper bounds rather than extrapolate.

---------------------------------------------------------------------------- */
Bool interpop_(ps_context_t *pscontext)
{
  register int32       alen;
  OBJECT*              top;
  OBJECT*              valo;
  register OBJECT*     alist;
  register SYSTEMVALUE val;
  SYSTEMVALUE lowval;
  SYSTEMVALUE highval;
  int32       lowidx;
  SYSTEMVALUE          res;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Operator requires two items on the stack */
  if ( 1 > theStackSize(operandstack) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  /* Check top of stack is an array */
  top = theTop(operandstack);

  switch ( oType(*top) ) {
  case OARRAY:
  case OPACKEDARRAY:
    break;

  default:
    return error_handler(TYPECHECK) ;
  }

  /* Check other argument is a real and get it */
  valo = &top[-1];
  if ( !fastStackAccess(operandstack) ) {
    valo = stackindex(1, &operandstack);
  }

  if ( OREAL != oType(*valo) ) {
    return error_handler(TYPECHECK) ;
  }

  val = (SYSTEMVALUE)oReal(*valo);

  /* We expect an input value between 0 and 1. Clip if necessary.
   */
  if ( val < 0 )
    val = 0;
  else if ( val > 1 )
    val = 1;

  /* Check that we have 2 or more entries in the array */
  alist = oArray(*top);
  alen = (int32)theLen(*top);

  if ( alen < 2 ) {
    return error_handler(RANGECHECK) ;
  }

  /* Get the index of the low value */
  lowidx = (int32) ((alen - 1) * val);

  /* Get the low value of the interpolator */
  alist += lowidx;
  if ( !object_get_numeric(alist++, &lowval) )
    return FALSE ;

  /* If at the top of the array, consume the interpolation array
   * and return the extreme array value */
  if ( lowidx == alen - 1 ) {
    pop(&operandstack);
    oReal(*valo) = (USERVALUE)lowval;
    return TRUE ;
  }

  /* Get the high value of the interpolator */
  if ( !object_get_numeric(alist, &highval) )
    return FALSE ;

  /* Found the interval - do interpolation */
  res = (val * (alen - 1) - lowidx) * (highval - lowval) + lowval;

  /* Consume the interpolation array and return interpolated index value */
  pop(&operandstack);
  oReal(*valo) = (USERVALUE)res;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            paracurvop_()        author:              ljw
   creation date:       02-12-2004           last modification:   ##-###-####
   arguments:           none .
   description:

   Operator for applying an ICC parametricCurve function.
---------------------------------------------------------------------------- */
Bool paracurvop_(ps_context_t *pscontext)
{
#define max_para_curve_params 7

  register int32       alen;
  OBJECT*              top;
  OBJECT*              valo;
  register OBJECT*     alist;
  register SYSTEMVALUE val;

  int32                type, length;

  union{
    struct {
      SYSTEMVALUE gamma;
      SYSTEMVALUE a;
      SYSTEMVALUE b;
      SYSTEMVALUE c;
      SYSTEMVALUE d;
      SYSTEMVALUE e;
      SYSTEMVALUE f;
    } p;

    SYSTEMVALUE params[max_para_curve_params];
  } para;

  SYSTEMVALUE          res;
  int32       i;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Operator requires two items on the stack */
  if ( 1 > theStackSize(operandstack) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  /* Check top of stack is an array */
  top = theTop(operandstack);

  switch ( oType(*top) ) {
  case OARRAY:
  case OPACKEDARRAY:
    break;
  default:
    return error_handler(TYPECHECK) ;
  }

  /* Check other argument is a real and get it */
  valo = &top[-1];
  if ( !fastStackAccess(operandstack) ) {
    valo = stackindex(1, &operandstack);
  }

  if ( OREAL != oType(*valo) ) {
    return error_handler(TYPECHECK) ;
  }

  val = (SYSTEMVALUE)oReal(*valo);

  /* We expect an input value between 0 and 1. Clip if necessary.
   */
  if ( val < 0 )
    val = 0;
  else if ( val > 1 )
    val = 1;

  /* Check we have at least 3 array entries, i.e. type, length, gamma */
  alist = oArray(*top);
  alen = (int32)theLen(*top);

  if ( alen < 3 ) {
    return error_handler(RANGECHECK) ;
  }

  /* Get the type and length */
  if ( !object_get_integer(alist++, &type))
    return FALSE;

  if ( !object_get_integer(alist++, &length))
    return FALSE;

  /* Get the curve parameters */
  HqMemZero((uint8 *)&para, (int)sizeof(para));

  for ( i = 0; i < alen - 2; i++ ) {
    if ( !object_get_numeric(alist++, &(para.params[i])))
      return FALSE;
  }

  /* Consume the array with the curve parameters */
  pop(&operandstack);

  /* Now do the maths */
  switch (type) {
  case 0:
    res = (SYSTEMVALUE) pow(val, para.p.gamma);
    break;

  case 1:
    if ( para.p.a != 0 )
    {
      if ( val >= -para.p.b/para.p.a )
      {
        res = (SYSTEMVALUE) pow ((para.p.a * val + para.p.b), para.p.gamma);
      }
      else
      {
        res = 0;
      }
    }
    else
    {
      /* Strictly the result is undefined in this case, but silently default to linear */
      res = val;
    }
    break;

  case 2:
    if ( para.p.a != 0 )
    {
      if ( val > -para.p.b/para.p.a )
      {
        res = (SYSTEMVALUE) pow ((para.p.a * val + para.p.b), para.p.gamma) + para.p.c;
      }
      else
      {
        res = para.p.c;
      }
    }
    else
    {
      /* Strictly the result is undefined in this case, but silently default to linear */
      res = val;
    }
    break;

  case 3:
    if ( val >= para.p.d )
    {
      res = (SYSTEMVALUE) pow ((para.p.a * val + para.p.b), para.p.gamma);
    }
    else
    {
      res = para.p.c * val;
    }
    break;

  case 4:
    if ( val >= para.p.d )
    {
      res = (SYSTEMVALUE) pow ((para.p.a * val + para.p.b), para.p.gamma) + para.p.e;
    }
    else
    {
      res = (para.p.c * val+ para.p.f);
    }
    break;
  default:
    return FALSE;
  }

  /* We expect an output value between 0 and 1. Clip if necessary.
   */
  if ( res < 0 )
    res = 0;
  else if ( res > 1 )
    res = 1;

  oReal(*valo) = (USERVALUE)res;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            invparacurvop_()     author:              ljw
   creation date:       02-12-2004           last modification:   ##-###-####
   arguments:           none .
   description:

   Operator for applying the inverse of an ICC parametricCurve function.
---------------------------------------------------------------------------- */
Bool invparacurvop_(ps_context_t *pscontext)
{
#define max_para_curve_params 7

  register int32       alen;
  OBJECT*              top;
  OBJECT*              valo;
  register OBJECT*     alist;
  register SYSTEMVALUE val;

  int32                type, length;

  union{
    struct {
      SYSTEMVALUE gamma;
      SYSTEMVALUE a;
      SYSTEMVALUE b;
      SYSTEMVALUE c;
      SYSTEMVALUE d;
      SYSTEMVALUE e;
      SYSTEMVALUE f;
    } p;

    SYSTEMVALUE   params[max_para_curve_params];
  } para;

  SYSTEMVALUE          res;
  int32       i;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Operator requires two items on the stack */
  if ( 1 > theStackSize(operandstack) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  /* Check top of stack is an array */
  top = theTop(operandstack);

  switch ( oType(*top) ) {
  case OARRAY:
  case OPACKEDARRAY:
    break;

  default:
    return error_handler(TYPECHECK) ;
  }

  /* Check other argument is a real and get it */
  valo = &top[-1];
  if ( !fastStackAccess(operandstack) ) {
    valo = stackindex(1, &operandstack);
  }

  if ( OREAL != oType(*valo) ) {
    return error_handler(TYPECHECK) ;
  }

  val = (SYSTEMVALUE)oReal(*valo);

  /* We expect an input value between 0 and 1. Clip if necessary.
   */
  if ( val < 0 )
    val = 0;
  else if ( val > 1 )
    val = 1;

  /* Check we have at least 3 array entries, i.e. type, length, gamma */
  alist = oArray(*top);
  alen = (int32)theLen(*top);

  if ( alen < 3 ) {
    return error_handler(RANGECHECK) ;
  }

  /* Get the type and length */
  if ( !object_get_integer(alist++, &type))
    return FALSE;
  if ( !object_get_integer(alist++, &length))
    return FALSE;

  /* Get the curve parameters */
  HqMemZero((uint8 *)&para, (int)sizeof(para));
  for ( i = 0; i < alen - 2; i++ ) {
    if ( !object_get_numeric(alist++, &(para.params[i])))
      return FALSE;
  }

  /* Consume the array with the curve parameters */
  pop(&operandstack);

  switch (type) {
  case 0:
    if ( para.p.gamma != 0 )
    {
      res = (SYSTEMVALUE) pow(val, (1.0 / para.p.gamma));
    }
    else
    {
      /* Strictly the result is undefined in this case, but silently default to linear */
      res = val;
    }
    break;

  case 1:
    if ( para.p.gamma != 0 && para.p.a != 0 )
    {
      res = (SYSTEMVALUE) (pow(val, (1.0 / para.p.gamma)) - para.p.b) / para.p.a;
    }
    else
    {
      /* Strictly the result is undefined in this case, but silently default to linear */
      res = val;
    }
    break;

  case 2:
    /* We expect an input value between c and 1 */
    if ( val < para.p.c )
    {
      val = para.p.c;
    }

    if ( para.p.gamma != 0 && para.p.a != 0 )
    {
      res = (SYSTEMVALUE) (pow((val - para.p.c), (1.0 / para.p.gamma)) - para.p.b) / para.p.a;
    }
    else
    {
      /* Strictly the result is undefined in this case, but silently default to linear */
      res = val;
    }
    break;

  case 3:
    if ( val < para.p.c * para.p.d )
    {
      /* the result is not necessarily well defined but following is reasonable
         if the original parametric curve is stricly monotonic */

      if ( para.p.c > 0 )
      {
        res = val / para.p.c;
      }
      else
      {
        if ( para.p.gamma != 0 && para.p.a != 0 )
        {
          res = (SYSTEMVALUE) (pow(val, (1.0 / para.p.gamma)) - para.p.b) / para.p.a;
        }
        else
        {
          /* Strictly the result is undefined in this case, but silently default to linear */
          res = val;
        }
      }
    }
    else if ( val < pow((para.p.a * para.p.d) + para.p.b, para.p.gamma ))
    {
      res = para.p.d;
    }
    else
    {
      if ( para.p.gamma != 0 && para.p.a != 0 )
      {
        res = (SYSTEMVALUE) (pow(val, (1.0 / para.p.gamma)) - para.p.b) / para.p.a;
      }
      else
      {
        /* Strictly the result is undefined in this case, but silently default to linear */
        res = val;
      }
    }
    break;

  case 4:
    if ( val < (para.p.c * para.p.d) + para.p.f )
    {
      if ( para.p.c > 0 )
      {
        res = (val - para.p.f) / para.p.c;
      }
      else
      {
        if ( para.p.gamma != 0 && para.p.a != 0 )
        {
          res = (SYSTEMVALUE) (pow((val - para.p.e), (1.0 / para.p.gamma)) - para.p.b) / para.p.a;
        }
        else
        {
          /* Strictly the result is undefined in this case, but silently default to linear */
          res = val;
        }
      }
    }
    else if ( val < pow((para.p.a * para.p.d) + para.p.b, para.p.gamma ) + para.p.e )
    {
      res = para.p.d;
    }
    else
    {
      if ( para.p.gamma != 0 && para.p.a != 0 )
      {
        res = (SYSTEMVALUE) (pow((val - para.p.e), (1.0 / para.p.gamma)) - para.p.b) / para.p.a;
      }
      else
      {
        /* Strictly the result is undefined in this case, but silently default to linear */
        res = val;
      }
    }
    break;
  default:
    return FALSE;
  }

  /* We expect an output value between 0 and 1. Clip if necessary.
   */
  if ( res < 0 )
    res = 0;
  else
    if ( res > 1 )
      res = 1;

  oReal(*valo) = (USERVALUE)res;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            pdfmark_()           author:              Mike Edie
   creation date:       11-Sep-2000          last modification:   ##-###-####
   arguments:           none .
   description:

   passes information from PS world to PDFout.

---------------------------------------------------------------------------- */

/** \todo ajcd 2005-02-09: Modularise pdfmark so that stuff
    pdfout wants to see doesn't get in the way. */
Bool pdfmark_(ps_context_t *pscontext)
{
  int32           ssize ;
  int32           namnum ;
  PDFCONTEXT*     pdfc = ps_core_context(pscontext)->pdfout_h ;
  OBJECT *o1, *dict ;

  if (isEmpty(operandstack))
    return error_handler( STACKUNDERFLOW ) ;

  ssize = theStackSize( operandstack ) ;

  o1 = TopStack( operandstack , ssize ) ;
  if ( oType(*o1) != ONAME )
    return error_handler( TYPECHECK ) ;

  /* get the key */
  namnum = oNameNumber(*o1) ;
  pop(&operandstack) ;

  /* count how many params till the last mark */
  ssize = num_to_mark() ;

  if (ssize < 0)
    return error_handler( UNMATCHEDMARK ) ; /* no mark */

  /* This code used to convert the stacked objects into a dictionary by
     default, but some pdfmark features, such as MP/DP/BMC/BDC may have an
     odd numbers of parameters onto the stack. */

  switch (namnum) {
  case NAME_ANN:
    { /* annotation note (text only) */
      int32 subtype = NAME_Text ; /* defaults to Text */

      enum {
        ANNmatch_Subtype, ANNmatch_Rect,
        ANNmatch_n_entries
      };
      static NAMETYPEMATCH ANNmatch[ANNmatch_n_entries + 1] = {
        { NAME_Subtype | OOPTIONAL, 1, { ONAME }},
        { NAME_Rect, 1, { OARRAY }},
        DUMMY_END_MATCH
      };

      enum {
        TEXTmatch_Contents, TEXTmatch_Open,
        TEXTmatch_n_entries
      };
      static NAMETYPEMATCH TEXTmatch[TEXTmatch_n_entries + 1] = {
        { NAME_Contents, 1, { OSTRING }},
        { NAME_Open | OOPTIONAL, 1, { OBOOLEAN }},
        DUMMY_END_MATCH
      };

      /* put all argument supplied to mark in a dict on the stack */
      if (!enddictmark_(pscontext))
        return FALSE;

      dict = theTop(operandstack) ;
      HQASSERT(oType(*dict) == ODICTIONARY,
               "enddictmark didn't create a dictionary") ;

      if ( !dictmatch(dict, ANNmatch) )
        return FALSE;

      pop(&operandstack) ;

      if ( theLen(*ANNmatch[ANNmatch_Rect].result) != 4 )
        return error_handler(RANGECHECK) ;

      if ( (o1 = ANNmatch[ANNmatch_Subtype].result) != NULL )
        subtype = oNameNumber(*o1) ;

      switch ( subtype ) {
      case NAME_Text:
        if ( !dictmatch(dict, TEXTmatch) )
          return FALSE;

        /* if no pdfout then dump args and return */
        if ( pdfout_enabled() ) {
          if ( !pdfout_annotation(pdfc,
                                  TEXTmatch[TEXTmatch_Contents].result,
                                  TEXTmatch[TEXTmatch_Open].result ? TEXTmatch[TEXTmatch_Open].result : &fnewobj,
                                  ANNmatch[ANNmatch_Rect].result) )
            return FALSE ;
        }

        break ;
      default:
        /* Ignore unknown annotation types */
        break ;
      }
    }
    break ;

  case NAME_PAGES:
  case NAME_PAGE:
    {
      enum {
        PGEmatch_CropBox, PGEmatch_HQNBleedBox,
        PGEmatch_n_entries
      };
      static NAMETYPEMATCH PGEmatch[PGEmatch_n_entries + 1] = {
        { NAME_CropBox, 1, { OARRAY }},
        { NAME_HQNBleedBox | OOPTIONAL, 1, { OARRAY }},

        DUMMY_END_MATCH
      };

      /* put all argument supplied to mark in a dict on the stack */
      if (!enddictmark_(pscontext))
        return FALSE;

      dict = theTop(operandstack) ;
      HQASSERT(oType(*dict) == ODICTIONARY,
               "enddictmark didn't create a dictionary") ;

      if ( !dictmatch(dict, PGEmatch) )
        return FALSE;

      pop(&operandstack) ;

      if ( theLen(*PGEmatch[PGEmatch_CropBox].result) != 4 )
        return error_handler(RANGECHECK) ;

      /* if no pdfout then dump args and return */
      if ( pdfout_enabled() ) {
        if ( !pdfout_setcropbox(pdfc, PGEmatch[PGEmatch_CropBox].result,
                                namnum == NAME_PAGES) )
          return FALSE ;

        if ( PGEmatch[PGEmatch_HQNBleedBox].result != NULL &&
             !pdfout_Hqn_mark(pdfc, namnum, NAME_BleedBox,
                              PGEmatch[PGEmatch_HQNBleedBox].result) )
          return FALSE ;
      }
    }
    break ;

  case NAME_SetTransparency:
    {
      enum {
        STmatch_BM, STmatch_SMask, STmatch_CA, STmatch_ca,
        STmatch_AIS, STmatch_TK,
        STmatch_n_entries
      };
      static NAMETYPEMATCH STmatch[STmatch_n_entries + 1] = {
        { NAME_BM | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, ONAME }},
        { NAME_SMask | OOPTIONAL, 2, { ODICTIONARY, ONAME }},
        { NAME_CA | OOPTIONAL, 2, { OREAL, OINTEGER }},
        { NAME_ca | OOPTIONAL, 2, { OREAL, OINTEGER }},
        { NAME_AIS | OOPTIONAL, 1, { OBOOLEAN }},
        { NAME_TK | OOPTIONAL, 1, { OBOOLEAN }},
        DUMMY_END_MATCH
      };

      /* put all argument supplied to mark in a dict on the stack */
      if (!enddictmark_(pscontext))
        return FALSE;

      dict = theTop(operandstack) ;
      HQASSERT(oType(*dict) == ODICTIONARY,
               "enddictmark didn't create a dictionary") ;

      if ( !dictmatch(dict, STmatch) )
        return FALSE;

      pop(&operandstack) ;

      /* Typecheck blend mode */
      if ( (o1 = STmatch[STmatch_BM].result) != NULL ) {
        if ( oType(*o1) != ONAME ) {
          int32 len ;

          HQASSERT(oType(*o1) == OARRAY || oType(*o1) == OPACKEDARRAY,
                   "Blend mode is not an array of names") ;

          if ( (len = theLen(*o1)) <= 0 )
            return error_handler(RANGECHECK) ;

          for ( o1 = oArray(*o1) ; len > 0 ; --len, ++o1 ) {
            if ( oType(*o1) != ONAME )
              return error_handler(TYPECHECK) ;
          }
        }
      }

      /* Only pay attention to pdfmark transparency extensions when doing
         transparency. */

      /** \todo ajcd 2005-10-14: Hack to prevent setting
          transparency in cached characters. This currently prevents
          uncoloured patterns too, it shouldn't. Wait for 12145 before
          fixing properly. */
      if ( DEVICE_INVALID_CONTEXT() )
        return error_handler(UNDEFINED) ;

      /* Non-stroking Alpha */
      if ( (o1 = STmatch[STmatch_ca].result) != NULL ) {
        tsSetConstantAlpha(gsTranState(gstateptr), FALSE,
                           (USERVALUE)object_numeric_value(o1),
                           gstateptr->colorInfo);
      }

      /* Stroking Alpha */
      if ( (o1 = STmatch[STmatch_CA].result) != NULL ) {
        tsSetConstantAlpha(gsTranState(gstateptr), TRUE,
                           (USERVALUE)object_numeric_value(o1),
                           gstateptr->colorInfo);
      }

      /* Alpha is shape */
      if ( (o1 = STmatch[STmatch_AIS].result) != NULL ) {
        tsSetAlphaIsShape(gsTranState(gstateptr), oBool(*o1));
      }

      /* Text knockout mode */
      if ( (o1 = STmatch[STmatch_TK].result) != NULL ) {
        tsSetTextKnockout(gsTranState(gstateptr), oBool(*o1));
      }

      /* Blend mode */
      if ( (o1 = STmatch[STmatch_BM].result) != NULL ) {
        tsSetBlendMode(gsTranState(gstateptr), *o1, gstateptr->colorInfo) ;
      }

      /* Softmask is the last of the options, because it may dispatch a
         group in future, and the rest of the gstate should be set up when
         it does so. */
      if ( (o1 = STmatch[STmatch_SMask].result) != NULL ) {
        if ( oType(*o1) == ONAME ) {
          if (oNameNumber(*o1) != NAME_None)
            return error_handler(UNDEFINED);

          /* Set an empty softmask */
          if (! tsSetSoftMask(gsTranState(gstateptr), EmptySoftMask,
                              HDL_ID_INVALID, gstateptr->colorInfo))
            return FALSE;
        } else {
          HQASSERT(oType(*o1) == ODICTIONARY,
                   "Softmask is not dictionary or name") ;
          return error_handler(UNDEFINED) ;
        }
      }
    }
    break ;

    /* Document information. */
  case NAME_DOCINFO:
    {
      enum {
        DOCINFOmatch_Author, DOCINFOmatch_CreationDate,
        DOCINFOmatch_Creator, DOCINFOmatch_Producer,
        DOCINFOmatch_Title, DOCINFOmatch_Subject,
        DOCINFOmatch_Keywords, DOCINFOmatch_ModDate,
        DOCINFOmatch_n_entries
      };
      static NAMETYPEMATCH DOCINFOmatch[DOCINFOmatch_n_entries + 1] = {
        { NAME_Author | OOPTIONAL, 1, { OSTRING }},
        { NAME_CreationDate | OOPTIONAL, 1, { OSTRING }},
        { NAME_Creator | OOPTIONAL, 1, { OSTRING }},
        { NAME_Producer | OOPTIONAL, 1, { OSTRING }},
        { NAME_Title | OOPTIONAL, 1, { OSTRING }},
        { NAME_Subject | OOPTIONAL, 1, { OSTRING }},
        { NAME_Keywords | OOPTIONAL, 1, { OSTRING }},
        { NAME_ModDate | OOPTIONAL, 1, { OSTRING }},
        DUMMY_END_MATCH
      };

      /* put all argument supplied to mark in a dict on the stack */
      if (!enddictmark_(pscontext))
        return FALSE;

      dict = theTop(operandstack) ;
      HQASSERT(oType(*dict) == ODICTIONARY,
               "enddictmark didn't create a dictionary") ;

      if ( !dictmatch(dict, DOCINFOmatch) )
        return FALSE;

      pop(&operandstack) ;

      if ( pdfout_enabled() ) {
        int32 index;

        for ( index = 0; index < DOCINFOmatch_n_entries ; index++ ) {
          if ( DOCINFOmatch[index].result != NULL &&
               !pdfout_Hqn_mark(pdfc,
                                namnum,
                                DOCINFOmatch[index].name & (~OOPTIONAL),
                                DOCINFOmatch[index].result) )
            return FALSE ;
        }
      }
    }
    break ;

    /* Harlequin proprietary keys */
  case NAME_HQN:
    {
      enum {
        HQNmatch_BleedBox,
        HQNmatch_n_entries
      };
      static NAMETYPEMATCH HQNmatch[HQNmatch_n_entries + 1] = {
        { NAME_BleedBox | OOPTIONAL, 1, { OARRAY }},
        DUMMY_END_MATCH
      };

      /* put all argument supplied to mark in a dict on the stack */
      if (!enddictmark_(pscontext))
        return FALSE;

      dict = theTop(operandstack) ;
      HQASSERT(oType(*dict) == ODICTIONARY,
               "enddictmark didn't create a dictionary") ;

      if ( !dictmatch(dict, HQNmatch) )
        return FALSE;

      pop(&operandstack) ;

      if ( pdfout_enabled() ) {
        int32 index;

        for ( index = 0; index < HQNmatch_n_entries ; index++ ) {
          if ( HQNmatch[index].result != NULL &&
               !pdfout_Hqn_mark(pdfc, namnum,
                                HQNmatch[index].name & (~OOPTIONAL),
                                HQNmatch[index].result) )
            return FALSE ;
        }
      }
    }
    break ;

  case NAME_LNK:
    /** \todo add URL code in here */
  case NAME_OUT:
    /** \todo add bookmark code in here */
  default:
    /* Ignore unknown pdfmark features */
    return cleartomark_(pscontext) ;
  }

  return TRUE ;
}

/** PostScript operator to control execution profiling.

    \code
    null --profiling--
    integer --profiling--
    object /begin --profiling--
    object /end --profiling--
    object /mark --profiling--
    object /add --profiling--
    object /count --profiling--
    \endcode

    The operator takes either a boolean and a designator object (suspending
    or resuming the user trace), a name and a designator object (putting a
    mark, quantifying the amount of a trace, or adding to a counting trace),
    and integer (enabling a trace group or toggling an individual trace, or
    null (to reset and clear the trace information). */
Bool profiling_(ps_context_t *pscontext)
{
  int32 ssize, nargs ;
  OBJECT *trace ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize(operandstack) ;
  if ( EmptyStack(ssize) )
    return error_handler(STACKUNDERFLOW) ;

  trace = TopStack(operandstack, ssize) ;

  nargs = 1 ;
  if ( oType(*trace) == ONULL ) {
    /* If the skin is listening to us, inform it that we're resetting. */
    probe_other(SW_TRACE_USER, SW_TRACETYPE_RESET, 0) ;
  } else if ( oType(*trace) == OINTEGER ) {
    int32 tracenum = oInteger(*trace) ;

    if ( tracenum > 0 )
      probe_other(tracenum, SW_TRACETYPE_ENABLE, TRUE /* Origin is user */) ;
    else
      probe_other(-tracenum, SW_TRACETYPE_DISABLE, TRUE /* Origin is user */) ;
  } else if ( oType(*trace) == ONAME ) {
    OBJECT *designator ;

    if ( ssize < 1 ) /* Need a designator object arg for these types */
      return error_handler(STACKUNDERFLOW) ;

    designator = &trace[-1] ;
    if ( !fastStackAccess(operandstack) )
      designator = stackindex(1, &operandstack);

    nargs = 2 ;

    switch ( oNameNumber(*trace) ) {
    case NAME_begin:
      probe_begin(SW_TRACE_USER, (intptr_t)oOther(*designator)) ;
      break ;
    case NAME_end:
      probe_end(SW_TRACE_USER, (intptr_t)oOther(*designator)) ;
      break ;
    case NAME_mark:
      probe_other(SW_TRACE_USER, SW_TRACETYPE_MARK, (intptr_t)oOther(*designator)) ;
      break ;
    case NAME_add:
      probe_other(SW_TRACE_USER, SW_TRACETYPE_ADD, (intptr_t)oOther(*designator)) ;
      break ;
    case NAME_count:
      probe_other(SW_TRACE_USER, SW_TRACETYPE_AMOUNT, (intptr_t)oOther(*designator)) ;
      break ;
    case NAME_notify: /* bad name */
      probe_other(SW_TRACE_USER, SW_TRACETYPE_VALUE, (intptr_t)oOther(*designator)) ;
      break ;
    default:
      return error_handler(RANGECHECK) ;
    }
  } else
    return error_handler(TYPECHECK) ;

  npop(nargs, &operandstack) ;
  return TRUE ;
}

/* escape_format - return a format string to escape a string based on the
 * cleanness format for it.
 */
static Bool escape_format(
  OBJECT* theoclean,
  uint8** pp_format)
{
  HQASSERT(theoclean == NULL || oType(*theoclean) == OINTEGER,
           "escape_format: non integer string clean format");
  HQASSERT((pp_format != NULL),
           "escape_format: NULL returned format string pointer");

  /* Set up format string for binary, 7 or 8 bit clean */
  *pp_format = (uint8*)"%.*!s%N";
  if ( theoclean != NULL ) {
    switch ( oInteger(*theoclean) ) {
    case 7:
      *pp_format = (uint8*)"%.*!7s%N";
      break;
    case 8:
      *pp_format = (uint8*)"%.*!8s%N";
      break;

    default:
      return error_handler(RANGECHECK) ;
    }
  }
  return TRUE ;
}

/* escapedstringlength_() - implements escapedstringlength internaldict operator.
 *          string escapedstringlength integer
 *  string integer escapedstringlength integer
 * Returns the minimum length for a string such that it can hold the result of
 * escaping various characters in the string using the escapedstring operator.
 * Note: the length returned can be greater than the largest possible PS string
 * length so that the string to be escaped may have to be broken int osubstrings
 * first.
 */
Bool escapedstringlength_(ps_context_t *pscontext)
{
  int32   n;
  int32   ssize;
  uint8*  p_format;
  OBJECT* theoclean = NULL;
  OBJECT* theostring;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Check for string on top of operand stack */
  ssize = theStackSize(operandstack) ;
  if ( EmptyStack(ssize) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  theostring = TopStack(operandstack, ssize);

  /* Check if we have cleanbits value */
  if ( oType(*theostring) == OINTEGER ) {
    /* Got integer giving clean bits - expect 2 operands */
    if ( ssize < 1 ) {
      return error_handler(STACKUNDERFLOW) ;
    }

    /* Now pick up the string */
    theoclean = theostring;
    theostring = &theoclean[-1];
    if ( !fastStackAccess(operandstack) ) {
      theostring = stackindex(1, &operandstack);
    }
  }

  /* Check we have a string */
  if ( oType(*theostring) != OSTRING ) {
    return error_handler(TYPECHECK) ;
  }

  /* Check string is readable */
  if ( !oCanRead(*theostring) ) {
    if ( !object_access_override(theostring) ) {
      return error_handler(INVALIDACCESS) ;
    }
  }

  /* Set up format string for binary, 7 or 8 bit clean */
  if ( !escape_format(theoclean, &p_format) ) {
    return FALSE ;
  }

  /* Get length of string required to hold escaped version of string.
   * NOTE: return value from swncopyf() does not include terminating NUL which
   * is what PS wants */
  n = swncopyf(NULL, 0, p_format, theLen(*theostring), oString(*theostring));

  if ( theoclean != NULL ) {
    /* Pop clean bits operand if given */
    pop(&operandstack);
  }

  /* Return length of string needed to hold escaped version of string */
  oInteger(inewobj) = n;
  OCopy(*theostring, inewobj);
  return TRUE ;
}

/* escapedstring_() - implements escapedstring internaldict operator.
 *          string1 string2 escapedstring substring2
 *  string1 string2 integer escapedstring substring2
 * Returns a copy of string1 as a substring of string2 with various characters
 * escaped such that the resultant string, contained in a PS string, can be
 * parsed by the PostScript interpreter.  The characters (, ), and \ are always
 * converted to their escaped character equivalents (\(, \), and \\
 * respectively).  When an integer operand is given it should have a value of 7
 * or 8 to indicate if the escaped string should be 7 or 8 bit clean, as defined
 * in the DSC Specification V3.0.
 */
Bool escapedstring_(ps_context_t *pscontext)
{
  int32   n;
  int32   ssize;
  int32   c_operands = 1;
  uint8*  p_format;
  OBJECT* theoclean = NULL;
  OBJECT* theoescapedstring;
  OBJECT* theostring;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Check for string on top of operand stack */
  ssize = theStackSize(operandstack) ;
  if ( EmptyStack(ssize) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  theoescapedstring = TopStack(operandstack, ssize);
  if ( oType(*theoescapedstring) == OINTEGER ) {
    /* Got integer giving clean bits - expect 3 operands */
    c_operands++;

    theoclean = theoescapedstring;
    theoescapedstring = &theoclean[-1];
    if ( !fastStackAccess(operandstack) ) {
      theoescapedstring = stackindex(1, &operandstack);
    }
  }

  /* Check we have enough operands on the stack */
  if ( ssize < c_operands ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  theostring = fastStackAccess(operandstack)
                  ? &theoescapedstring[-1]
                  : stackindex(2, &operandstack);

  /* Check we have two strings */
  if ( oType(*theoescapedstring) != OSTRING ||
       oType(*theostring) != OSTRING ) {
    return error_handler(TYPECHECK) ;
  }

  /* Check source string is readable and dest string is writable */
  if ( (!oCanRead(*theostring) &&
        !object_access_override(theostring)) ||
       (!oCanWrite(*theoescapedstring) &&
        !object_access_override(theoescapedstring)) ) {
    return error_handler(INVALIDACCESS) ;
  }

  /* Set up format string for binary, 7 or 8 bit clean */
  if ( !escape_format(theoclean, &p_format) ) {
    return FALSE ;
  }

  /* Check length of string required to hold escaped version of string is not
   * longer than string that will hold it.
   * NOTE: return value from swncopyf() does not include terminating NUL */
  n = swncopyf(NULL, 0, p_format, theLen(*theostring), oString(*theostring));
  if ( n > theLen(*theoescapedstring) ) {
    return error_handler(RANGECHECK) ;
  }

  if ( n > 0 ) {
    /* Escape the PS string.  Although we are suppressing the terminating NUL we
     * have to behave as if we are getting it, i.e. pass a buffer count in that
     * includes it! */
    (void)swncopyf(oString(*theoescapedstring), (n + 1), p_format,
                   theLen(*theostring), oString(*theostring));
  }

  /* Return substring of string passed in to hold escaped string */
  theLen(*theoescapedstring) = CAST_TO_UINT16(n);
  OCopy(*theostring, *theoescapedstring);

  /* Pop rest of operands */
  npop(c_operands, &operandstack);
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            calibrationop_()       author:              ljw
   creation date:       20-03-2013             last modification:   ##-###-####
   arguments:           none

   description:

   Operator for performing calibration setup related operations.

   optional...params /action calibrationop -> optional...results

   In each case there may be any number of optional params (or none).

   The required action is defined by /action.  Event handlers that do not
   support this action should immediately return SW_EVENT_CONTINUE.  The
   handler(s) (if any) are responsible for ensuring the stack is left in
   an appropriate state as this operator does not manipulate the stack
   itself.
---------------------------------------------------------------------------- */
Bool calibrationop_(ps_context_t *pscontext)
{
  OBJECT  *action ;
  SWMSG_CALIBRATION_OP cal_msg ;
  sw_datum stack ;
  sw_data_result result ;
  sw_event_result event_result ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Operator requires at least one item on the stack */
  if ( theStackSize(operandstack) < 0 )
    return error_handler(STACKUNDERFLOW) ;

  /* The required action. */
  action = theTop(operandstack) ;

  if ( oType(*action) != ONAME )
    return error_handler(TYPECHECK) ;

  /* Convert the operandstack to datum - note this includes the action */
  if ((result = swdatum_from_stack(&stack, &operandstack)) != SW_DATA_OK)
    return error_from_sw_data_result(result) ;

  /* Fill in the event message including the PS_STRING action */
  cal_msg.action.string = theICList(oName(*action));
  cal_msg.action.length = theINLen(oName(*action));
  cal_msg.stack = &stack ;

  /* Issue the event to say we have calibration stuff needing doing */
  event_result = SwEvent(SWEVT_CALIBRATION_OP, &cal_msg, sizeof(cal_msg)) ;

  if (event_result == SW_EVENT_UNHANDLED)
    return error_handler(UNREGISTERED) ;

  /* Error ranges for event_result and sw_data_result values are compatible */
  if (event_result >= SW_EVENT_ERROR)
    return error_from_sw_data_result((sw_data_result) event_result) ;

  HQASSERT(event_result == SW_EVENT_HANDLED,
           "Unexpected result from SWEVT_CALIBRATION_OP") ;

  /* The handler should have manipulated the stack as necessary */
  return TRUE;
}


void init_C_globals_miscops(void)
{
  static Bool first_time_ever = TRUE ;
  static OPERATOR system_ops_save[OPS_COUNTED] ;

  if (first_time_ever) {
    HqMemCpy(system_ops_save, system_ops, sizeof(OPERATOR) * OPS_COUNTED) ;
    first_time_ever = FALSE ;
  } else {
    HqMemCpy(system_ops, system_ops_save, sizeof(OPERATOR) * OPS_COUNTED) ;
  }

  shadowproc = NULL ;
  defineproc = NULL ;
  shadowop = NULL ;
  rtime = 0 ;
  utime = 0 ;
  callrealoperator = FALSE;
}

/* Log stripped */
