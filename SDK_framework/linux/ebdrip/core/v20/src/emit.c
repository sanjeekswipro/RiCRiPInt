/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:emit.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Emitter function; prints representation of objects (recursively) onto file
 * stream. Does not attempt to be smart about recursive structures, but gives
 * limitcheck error if looping too much.
 */

#include "core.h"
#include "hqmemcpy.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swctype.h"
#include "objects.h"
#include "namedef_.h"

#include "chartype.h"
#include "dictops.h"
#include "stacks.h"
#include "typeops.h"
#include "fileio.h"
#include "psvm.h"
#include "swoften.h"

#include "emit.h"

/* The structure for params to the dict_walk function. */

struct emit_dwfparams {
  FILELIST *flptr ;
  EMIT_STATE *state ;
  void *params ;
} ;

typedef struct emit_dwfparams EMIT_DWFPARAMS ;

typedef Bool (*emit_token_fn)(uint8 *buf, int32 len, FILELIST *flptr,
                              EMIT_STATE *state) ;

/* Local routines. */

static Bool do_ps_emit( OBJECT *theo , FILELIST *flptr ) ;
static Bool emit_partial_token(uint8 *src , int32 len ,
                               FILELIST *flptr , EMIT_STATE *state ) ;
static Bool emit_dictwalkfn( OBJECT *thek , OBJECT *theo , void *params ) ;

/* The PS incarnation of emit - dead useful for debugging and so on. */

Bool emit_(ps_context_t *pscontext)
{
  FILELIST *flptr ;
  OBJECT *o1 ;
  OBJECT *o2 ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 1 , & operandstack ) ;
  if ( oType(*o1) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*o1) ;
  if ( ! isIOutputFile( flptr ) || ! isIOpenFile( flptr ))
    return error_handler( IOERROR ) ;

  o2 = theTop( operandstack ) ;

  if ( !do_ps_emit( o2 , flptr ))
    return FALSE ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* The local dispatcher function whose sole purpose is to be the root
 * of recursive execution - holding the state in its stack frame etc.
 */

static Bool do_ps_emit( OBJECT *theo , FILELIST *flptr )
{
  uint8 buffer[256] ;
  EMIT_STATE root_state = { 0 } ;

  root_state.reclevel = 32 ; /* Max recursion level */
  root_state.buffer = buffer ;
  root_state.linelimit = 255 ;
  root_state.linelen = 0 ;
  root_state.totallen = 0 ; /* ignored */
  root_state.indent_size = 2 ; /* Prettyprint */
  root_state.indent_level = 0 ;
  root_state.lastchar = -1 ;
  root_state.optline = TRUE ;
  root_state.optspace = FALSE ;
  root_state.override = 0 ;
  root_state.extra = NULL ;

  return emit_internal( theo , flptr , & root_state , NULL ) ;
}


/* emit the buffer without adding indentation. */

static Bool emit_partial_token( uint8 *src , int32 len ,
                                FILELIST *flptr , EMIT_STATE *state )
{
  if ( !file_write(flptr, src, len) )
    return FALSE ;

  state->linelen += len ;
  state->totallen += len ;

  if ( theICount(flptr) == theIBufferSize(flptr) )
    if ( (*theIMyFlushFile(flptr))(flptr) == EOF )
      return FALSE ;

  return TRUE ;
}

/** Emit the given object to the file specified. Basic types are handled here,
 * recursively if necessary. Types overridden by a bitmask are passed to the
 * extra function in the state. In the case of PS, non-basic types will be
 * emitted as the type of the object surrounded by double dashes (e.g.
 * --marktype--). The override function is used by PDF to emit indirect
 * objects, reals without exponential format, and PDF streams from OFILEs.
 * Future work will include prettier output, particularly sticking compound
 * objects on a single line if they'll fit and better indentation control. */
Bool emit_internal(OBJECT *theo, FILELIST *flptr, EMIT_STATE *state,
                   void *params)
{
  uint8 *buf ;
  int32 len = 0, limit ;
  char *access = NULL ;
  OBJECT *accesso = theo ;

  HQASSERT( theo , "theo NULL in emit_internal." ) ;
  HQASSERT( flptr , "flptr NULL in emit_internal." ) ;
  HQASSERT( state , "state NULL in emit_internal." ) ;
  HQASSERT( isIOutputFile( flptr ) && isIOpenFile( flptr ) ,
            "Not a viable output file in emit_internal." ) ;

  buf = state->buffer ;
  limit = state->linelimit ;

  if ( --state->reclevel < 0 )
    return error_handler( LIMITCHECK ) ;

  switch ( oXType(*theo) ) {
  case ODICTIONARY:
    accesso = oDict(*theo) ;
    /* FALLTHRU */
  case OARRAY: case OPACKEDARRAY:
  case OLONGARRAY: case OLONGPACKEDARRAY:
  case OSTRING: case OLONGSTRING:
  case OFILE:
    if ( !oCanRead(*accesso) )
      if ( ! object_access_override(accesso) )
        return error_handler( INVALIDACCESS ) ;

    switch ( oAccess(*accesso) ) {
    case NO_ACCESS:
      access = "noaccess" ;
      break ;
    case EXECUTE_ONLY:
      access = "executeonly" ;
      break ;
    case READ_ONLY:
      access = "readonly" ;
      break ;
    }
  }

  /* Test if overridden */
  if ( state->extra && (state->override & (1 << oType(*theo))) ) {
    if ( !(*state->extra)(flptr, theo, state, &len, params) )
      return FALSE ;
  } else {
    Bool longarray = FALSE ;

    /* Use default method */
    switch ( oXType(*theo) ) {
    case OREAL:
    case OINTEGER :
    case OOPERATOR :
    case OBOOLEAN :
      if ( !do_cvs( theo , buf , limit , & len ))
        return FALSE ;
      break;

    case ONULL:
      if ( !emit_tokens( ( uint8 *)"null" , 4 , flptr , state ))
        return FALSE ;

      break;

    case ONOTHING:
      /* May be an unused slot in a dictionary: in any case,
       * no output makes sense.
       */
      break;

    case ONAME :
      {
        uint8 *nbuf = buf ;

        if ( ! oExecutable(*theo) )
          *nbuf++ = '/' ;

        if ( !do_cvs( theo , nbuf , limit - 1 , & len ))
          return FALSE ;

        if ( nbuf != buf )
          len++ ;

        break;
      }

    case OSTRING :
      {
        uint8 *string ;
        int32 ii = 0 ;
        emit_token_fn emitfn = emit_tokens ;

        state->optspace = TRUE ;

        string = oString(*theo) ;
        buf[ len++ ] = '(' ;

        while ( ii++ < theLen(*theo) ) {
          uint8 ch = *string++ ;
          int32 slash = TRUE ;

          switch ( ch ) { /* Convert known control codes */
          case 8:  ch = 'b' ; break ;
          case 9:  ch = 't' ; break ;
          case 10: ch = 'n' ; break ;
          case 12: ch = 'f' ; break ;
          case 13: ch = 'r' ; break ;
          case '(': case '\\': case ')': /* needs slash */
            break ;
          default:
            slash = FALSE ;
            break ;
          }

          if ( !isprint(ch) ) { /* Still not printable, use octal escape */
            buf[ len++ ] = '\\' ;
            buf[ len++ ] = (uint8)('0' + ((ch >> 6) & 7));
            buf[ len++ ] = (uint8)('0' + ((ch >> 3) & 7)) ;
            buf[ len++ ] = (uint8)('0' + ((ch >> 0) & 7)) ;
          } else { /* Printable, possibly needs escaping */
            if ( slash )
              buf[ len++ ] = '\\' ;

            buf[ len++ ] = ch ;
          }

          if ( ii != theLen(*theo) && len >= limit - 4 ) {
            buf[ len++ ] = '\\' ;
            buf[ len++ ] = '\n' ;

            if ( ! (*emitfn)(buf, len, flptr, state) )
              return FALSE ;

            emitfn = emit_partial_token ; /* No more indents etc */
            state->linelen = 0 ;
            len = 0 ;
          }
        }

        buf[ len++ ] = ')' ;
        if ( ! (*emitfn)(buf, len, flptr, state) )
          return FALSE ;
        len = 0 ;

        if ( access ) {
          state->optspace = TRUE ;

          if ( !emit_tokens((uint8 *)access, strlen_int32(access), flptr, state) )
            return FALSE ;
        }

        break;
      }

    case ODICTIONARY:
      {
        EMIT_DWFPARAMS dwf_params ;

        dwf_params.flptr = flptr ;
        dwf_params.state = state ;
        dwf_params.params = params ;

        state->optspace = TRUE ;

        if ( !emit_tokens( ( uint8 *)"<<" , 2 , flptr , state ))
          return FALSE ;

        state->indent_level += state->indent_size ;
        state->optline = TRUE ;

        if ( !walk_dictionary( theo ,
                               emit_dictwalkfn ,
                               ( void * )& dwf_params )) {
          return FALSE ;
        }

        state->optline = TRUE ;
        state->indent_level -= state->indent_size ;

        if ( !emit_tokens( ( uint8 *)">>" , 2 , flptr , state ))
          return FALSE ;

        len = 0 ;

        if ( access ) {
          state->optspace = TRUE ;

          if ( !emit_tokens((uint8 *)access, strlen_int32(access), flptr, state) )
            return FALSE ;
        }

        break ;
      }

    case OLONGARRAY:
    case OLONGPACKEDARRAY:
      longarray = TRUE ;
      /* DROP THROUGH */

    case OARRAY:
    case OPACKEDARRAY:
      {
        int32 alen = theXLen(*theo) ;
        int32 i ;

        state->optspace = TRUE ;

        if ( !emit_tokens( ( uint8 *)(oExecutable(*theo) ?
                                      "{" : "[" ) , 1 , flptr , state ))
          return FALSE ;

        state->indent_level += state->indent_size ;

        if ( alen ) {
          OBJECT *alist = (longarray) ? oLongArray(*theo) : oArray(*theo) ;

          for ( i = alen ; i > 0 ; --i ) {
            if ( alen > 6 )
              state->optline = TRUE ;
            else
              state->optspace = TRUE ;
            if ( !emit_internal( alist++ , flptr , state , params ))
              return FALSE ;
          }
          if ( alen > 6 )
            state->optline = TRUE ;
          else
            state->optspace = TRUE ;
        }

        state->indent_level -= state->indent_size ;

        if ( !emit_tokens( ( uint8 *)(oExecutable(*theo) ?
                                      "}" : "]" ) , 1 , flptr , state ))
          return FALSE ;

        len = 0 ;

        if ( access ) {
          state->optspace = TRUE ;

          if ( !emit_tokens((uint8 *)access, strlen_int32(access), flptr, state) )
            return FALSE ;
        }

        break ;
      }

    default: /* Default behaviour is to emit --typename-- */
      {
        OBJECT typename = OBJECT_NOTVM_NOTHING ;

        object_store_name(&typename, oType(*theo) + NAME_integertype - 1, LITERAL) ;

        if ( !do_cvs( & typename , buf + 2 , limit - 2, &len) )
          return FALSE ;

        if ( len > limit - 4 )
          return error_handler(RANGECHECK) ;

        buf[0] = buf[1] = buf[len + 2] = buf[len + 3] = '-' ;
        len += 4 ;

        break ;
      }
    }
  }

  ++state->reclevel ;

  return emit_tokens( buf , len , flptr , state) ;
}

/* Objects greater than linelimit will just be emitted as a single line. */

/** emit_tokens emits a whole token or more than one token, adding spaces
   as necessary for correct interpretation. If prettyprinting is enabled,
   optional spaces and indentation will be taken into account. Newlines are
   only noticed at the start and end of the output buffer; it is assumed that
   all tokens in between will be output as a single line. */
Bool emit_tokens(uint8 *buf , int32 len ,
                 FILELIST *flptr , EMIT_STATE *state)
{
  if ( len ) {
    int32 prettyprint = (state->indent_size > 0) ;
    int32 needspace = FALSE ;

    if ( state->lastchar >= 0 ) {
      if ( IsEndOfLine(state->lastchar) ) {
        state->optline = FALSE ;
        state->optspace = FALSE ;
      } else if ( isspace(state->lastchar) ) {
        state->optspace = FALSE ;
      } else {
        needspace = (!psdelimiter(buf[0]) && !psdelimiter(state->lastchar)) ;
      }
    }

    if ( state->linelen > 0 && !IsEndOfLine(buf[0]) ) {
      if ( (state->optline && prettyprint) ||
           state->linelen + len + (needspace |
                                   (state->optspace && prettyprint)) > state->linelimit ) {
        int32 i ;

        if ( TPutc( '\n' , flptr ) == EOF )
          return (*theIFileLastError( flptr ))( flptr ) ;

        needspace = FALSE ;
        state->totallen++ ;
        state->optline = FALSE ;
        state->optspace = FALSE ;

        for ( i = 0 ; i < state->indent_level ; i++ )
          if ( TPutc( ' ' , flptr ) == EOF )
            return (*theIFileLastError( flptr ))( flptr ) ;

        state->linelen = state->indent_level ;
        state->totallen += state->indent_level ;
      }
    }

    if ( needspace || (state->optspace && prettyprint) ) {
      if ( TPutc( ' ' , flptr ) == EOF )
        return (*theIFileLastError( flptr ))( flptr ) ;

      state->optspace = FALSE ;
      state->linelen++ ;
      state->totallen++ ;
    }

    if ( !emit_partial_token(buf, len, flptr, state) )
      return FALSE ;

    state->lastchar = buf[len - 1] ;
    if ( IsEndOfLine(state->lastchar) )
      state->linelen = 0 ;
  }

  return TRUE ;
}

/* The walk_dictionary function for the above */

static Bool emit_dictwalkfn( OBJECT *thek , OBJECT *theo , void *dwf_params )
{
  EMIT_DWFPARAMS *dwfparams = ( EMIT_DWFPARAMS * )dwf_params ;
  EMIT_STATE *state ;

  HQASSERT( theo , "theo NULL in emit_dictwalkfn." ) ;
  HQASSERT( thek , "thek NULL in emit_dictwalkfn." ) ;
  HQASSERT( dwfparams , "dwfparams NULL in emit_dictwalkfn." ) ;

  state = dwfparams->state ;
  HQASSERT( state , "dwfparams->state NULL in emit_dictwalkfn." ) ;

  state->optline = TRUE ;

  if ( !emit_internal( thek , dwfparams->flptr , state , dwfparams->params ) )
    return FALSE ;

  state->optspace = TRUE ;

  return emit_internal( theo , dwfparams->flptr , state , dwfparams->params ) ;
}

#if defined(DEBUG_BUILD)
#include "monitor.h"
#include "swcopyf.h"
#include "debugging.h"

#define DEBUG_INITIAL_RECURSION 32

void debug_print_object_indented(OBJECT *theo, char *pre, char *post,
                                 FILELIST *flptr)
{
  uint8 buffer[256] ;
  EMIT_STATE root_state = { 0 } ;
  int32 indent = 0 ;

  if ( flptr == NULL ) {
    flptr = theIStderr(workingsave) ;
  }

  if ( pre && *pre ) {
    ( void )file_write(flptr, ( uint8 * )pre, strlen_int32(pre)) ;

    while ( pre[indent] == ' ' )
      ++indent ;
  }

  root_state.reclevel = DEBUG_INITIAL_RECURSION ; /* Max recursion level */
  root_state.buffer = buffer ;
  root_state.linelimit = 255 ;
  root_state.linelen = 0 ;
  root_state.totallen = 0 ; /* ignored */
  root_state.indent_size = 1 ; /* Prettyprint */
  root_state.indent_level = indent ;
  root_state.lastchar = -1 ;
  root_state.optline = TRUE ;
  root_state.optspace = FALSE ;
  root_state.override = 0 ;
  root_state.extra = NULL ;

  if ( !emit_internal(theo, flptr, &root_state, NULL) )
    HQFAIL("Error emitting object in debug code") ;

  if ( post && *post ) {
    ( void )file_write(flptr, ( uint8 * )post, strlen_int32(post)) ;
  }

  (void)SwOftenActivateUnsafe();
}

void debug_print_object(OBJECT *theo)
{
  debug_print_object_indented(theo, NULL, NULL, NULL) ;
}

/* Extra emitter function for printing stacks; ONOTHING on execstack may
   be an operator. */
static int32 debug_print_stack_extra(FILELIST *flptr, OBJECT *theo,
                                     EMIT_STATE *state, int32 *len,
                                     void *params)
{
  Bool onexecstack = (params == &executionstack &&
                      state->reclevel == DEBUG_INITIAL_RECURSION) ;

  UNUSED_PARAM(FILELIST *, flptr) ;

  switch ( oType(*theo) ) {
  case ONOTHING:
    if ( onexecstack ) {
      swncopyf(state->buffer,state->linelimit,
               (uint8 *)"--%.*s--",
               theINLen(theIOpName(oOp(*theo))),
               theICList(theIOpName(oOp(*theo)))) ;
    } else {
      swncopyf(state->buffer,state->linelimit,(uint8 *)"**nothing**") ;
    }
    break ;
  case OINFINITY:
    swncopyf(state->buffer,state->linelimit,(uint8 *)"**infinity**") ;
    break ;
  case OMARK:
    if ( onexecstack ) {
      swncopyf(state->buffer,state->linelimit,(uint8 *)"**stopped**") ;
    } else {
      swncopyf(state->buffer,state->linelimit,(uint8 *)"**mark**") ;
    }
    break ;
  case ONULL:
    if ( onexecstack ) {
      switch ( theLen(*theo) ) {
      case ISINTERPRETEREXIT:
        swncopyf(state->buffer,state->linelimit,(uint8 *)"**stopped**") ;
        break ;
      case ISPATHFORALL:
        swncopyf(state->buffer,state->linelimit,(uint8 *)"**pathforall**") ;
        break ;
      case ISPROCEDURE:
        swncopyf(state->buffer,state->linelimit, (uint8*)"**call /%.*s**",
                 theINLen(theIOpName(oOp(*theo))),
                 theICList(theIOpName(oOp(*theo)))) ;
        break ;
      default:
        swncopyf(state->buffer,state->linelimit,(uint8 *)"**unknownnull**") ;
        break ;
      }
    } else {
      swncopyf(state->buffer,state->linelimit,(uint8 *)"null") ;
    }
    break ;
  case OFONTID:
    swncopyf(state->buffer,state->linelimit, (uint8 *)"**fontid %d flags %x**",
             oFid(*theo), theLen(*theo)) ;
    break ;
  case OSAVE:
    swncopyf(state->buffer,state->linelimit,(uint8 *)"**save**") ;
    break ;
  case OINDIRECT:
    swncopyf(state->buffer,state->linelimit, (uint8 *)"**xref %d gen %d**",
             oXRefID(*theo), theGen(*theo)) ;
    break ;
  case OFILE:
    if ( !isIFilter(oFile(*theo)) ) {
      swncopyf(state->buffer,state->linelimit, (uint8 *)"**file %%%s%%%.*s**",
               theIDevName(theIDeviceList(oFile(*theo))),
               theINLen(oFile(*theo)), theICList(oFile(*theo))) ;
    } else if ( theLen(*theo) == theIFilterId(oFile(*theo)) ) {
      swncopyf(state->buffer,state->linelimit, (uint8 *)"**filter %.*s**",
               theINLen(oFile(*theo)), theICList(oFile(*theo))) ;
    } else {
      swncopyf(state->buffer,state->linelimit,(uint8 *)"**closed filter**") ;
    }
    break ;
  case ODICTIONARY:
    swncopyf(state->buffer,state->linelimit,(uint8 *)"<<dictionary>>") ;
    break ;
  }

  *len = strlen_int32((char *)state->buffer) ;

  return TRUE ;
}

void dumpexecstack(void)
{
  debug_print_stack(&executionstack);
}
void dumpoperandstack(void)
{
  debug_print_stack(&operandstack);
}

void debug_print_stack(STACK *stack)
{
  uint8 buffer[256] ;
  EMIT_STATE root_state = { 0 } ;
  int32 i ;

  char *stacktypes[] = { "operand", "execution", "dictionary" } ;

  root_state.reclevel = 32 ; /* Max recursion level */
  root_state.buffer = buffer ;
  root_state.linelimit = 255 ;
  root_state.linelen = 0 ;
  root_state.totallen = 0 ; /* ignored */
  root_state.indent_size = 2 ; /* Prettyprint */
  root_state.indent_level = 0 ;
  root_state.lastchar = -1 ;
  root_state.optline = TRUE ;
  root_state.optspace = FALSE ;
  root_state.override = ((1 << ONOTHING) | (1 << OMARK) | (1 << OFILE) |
                         (1 << OINFINITY) | (1 << OFONTID) | (1 << OSAVE) |
                         (1 << OINDIRECT) | (1 << ODICTIONARY)) ;
  root_state.extra = debug_print_stack_extra ;

  monitorf((uint8 *)"Stack type %s limit %d size %d\n",
           stacktypes[stack->type], stack->limit, stack->size) ;

  for ( i = 0 ; i <= theStackSize(*stack) ; ++i ) {
    OBJECT *theo = stackindex(i, stack) ;
    monitorf((uint8*)"%d: ", i ) ;

    if ( !emit_internal(theo, theIStderr(workingsave), &root_state, stack) )
      HQFAIL("Error emitting object in debug code") ;

    monitorf((uint8*)"\n" ) ;
  }

  (void)SwOftenActivateUnsafe();
}
#endif

/*
Log stripped */
