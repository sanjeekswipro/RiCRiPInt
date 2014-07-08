/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:binfile.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains the routines to implement the operators
 * writeobject , printobject , setobjectformat , currentobjectformat.
 */

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swctype.h"
#include "swoften.h"
#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "tables.h"
#include "hqmemcpy.h"

#include "psvm.h"
#include "stacks.h"
#include "chartype.h"
#include "binscan.h"
#include "control.h"

static Bool write_binary_sequence( OBJECT *pobj,
                                   FILELIST *flptr,
                                   int32 thetag );
static Bool write_binary_array( register OBJECT *pobj,
                                int32 length,
                                register uint8 *p );
static Bool get_sequence_length( register OBJECT *pobj,
                                 register int32 *ret_length,
                                 int32 level );

/* Globals for converting array tokens into binary sequences. They point
 * to the top and bottom free sections of the buffer which is used
 * to build up the sequence before it is written out.
 */
static uint8 *psequence = NULL ;
static uint8 *top_free = NULL ;
static uint8 *bot_free = NULL ;
static int32 ieee = 0 , hiorder = 0 ;

#if defined( ASSERT_BUILD )
static int32 sizeof_uservalue = sizeof(USERVALUE);
#endif

void init_C_globals_binfile(void)
{
  psequence = NULL ;
  top_free = NULL ;
  bot_free = NULL ;
  ieee = 0 ;
  hiorder = 0 ;

#if defined( ASSERT_BUILD )
  sizeof_uservalue = sizeof(USERVALUE) ;
#endif
}

/* ----------------------------------------------------------------------------
   function:            writeobject_(..)   author:              Luke Tunmer
   creation date:       16-May-1991        last modification:   ##-###-####
   arguments:
   description:

   This procedure implements the operator described on page 551 PS-L2. It
   calls write_binary_sequence to do all the dirty work.

---------------------------------------------------------------------------- */
Bool writeobject_(ps_context_t *pscontext)
{
  register int32    ssize ;
  register OBJECT   *theo , *thet , *thef;
  register FILELIST *flptr ;
  register int32    thetag ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theIObjectFormat( workingsave ) == 0 )
    return error_handler( UNDEFINED ) ;
  ssize = theStackSize( operandstack ) ;
  if ( ssize < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  thet = TopStack( operandstack , ssize ) ;
  if ( oType(*thet) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  thetag = oInteger(*thet) ;
  if ( ( thetag < 0 ) || ( thetag > 255 ))
    return error_handler( RANGECHECK ) ;

  theo = stackindex( 1 , &operandstack ) ;

  thef = stackindex( 2 , &operandstack ) ;
  if ( oType(*thef) != OFILE )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanWrite(*thef) && !object_access_override(thef) )
    return error_handler( INVALIDACCESS ) ;

  flptr = oFile(*thef) ;
  /* it is valid to check isIInputFile even if it is a dead filter */
  if ( isIInputFile( flptr ))
    return error_handler( IOERROR ) ;
  if ( ! isIOpenFileFilter ( thef, flptr ))
    return error_handler( IOERROR ) ;

  if ( ! write_binary_sequence( theo , flptr , thetag ))
    return FALSE ;

  npop( 3 , & operandstack ) ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            printobject_(..)   author:              Luke Tunmer
   creation date:       16-May-1991        last modification:   ##-###-####
   arguments:
   description:

   This procedure implements the operator described on page 464 PS-L2. It
   calls write_binary_sequence to do all the dirty work.

---------------------------------------------------------------------------- */
Bool printobject_(ps_context_t *pscontext)
{
  register int32    ssize ;
  register OBJECT   *theo , *thet ;
  register FILELIST *flptr ;
  register int32    thetag ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theIObjectFormat( workingsave ) == 0 )
    return error_handler( UNDEFINED ) ;
  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  thet = TopStack( operandstack , ssize ) ;
  theo = stackindex( 1 , &operandstack ) ;

  if ( oType(*thet) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  thetag = oInteger(*thet) ;
  if ( ( thetag < 0 ) || ( thetag > 255 ))
    return error_handler( RANGECHECK ) ;

  flptr = theIStdout( workingsave ) ;

  if (! isIOpenFileFilterById( theISaveStdoutFilterId( workingsave ), flptr ))
    return error_handler (IOERROR);

  if ( ! write_binary_sequence( theo , flptr , thetag ))
    return FALSE ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            write_binary_sequence(..)     author:   Luke Tunmer
   creation date:       16-May-1991        last modification:   ##-###-####
   arguments:   pobj , flptr , thetag
   description:

   This procedure writes the binary sequence which represents the
   object pobj to the file flptr, tagged with thetag.

---------------------------------------------------------------------------- */
static Bool write_binary_sequence( OBJECT *pobj,
                                   FILELIST *flptr,
                                   int32 thetag )
{
  int32          allocsize, numbytes= 0 ; /* number of bytes in the sequence */
  int32          header_size ;  /* 4 or 8 bit header */
  int32          i ;
  register uint8 *p ;
  uint8          header[8] ;    /* to build up header */

  if ( ! get_sequence_length( pobj , & numbytes , 0 ))
    return FALSE ;
  if ( numbytes > (65535 - 4) ) /* need extended header */
    header_size = 8 ;
  else
    header_size = 4 ;

  allocsize = numbytes;
  /* allocate scratch space to work on */
  psequence = mm_alloc(mm_pool_temp, allocsize, MM_ALLOC_CLASS_BINARY_FILE);
  if ( psequence == NULL )
    return error_handler( VMERROR ) ;

  top_free = psequence + numbytes ;
  bot_free = psequence + 8 ;  /* allow for top level object */
  hiorder = (( theIObjectFormat( workingsave ) == 1 ) ||
             ( theIObjectFormat( workingsave ) == 3 )) ;
  ieee    = (( theIObjectFormat( workingsave ) == 1 ) ||
             ( theIObjectFormat( workingsave ) == 2 )) ;

  /* setup header */
  p = header ;
  *(p++) = (uint8)(127 + theIObjectFormat( workingsave )) ;
  if ( header_size == 4 ) {
    *(p++) = 1 ; /* the top-level length is 1 */
    *( (uint16 * ) p ) = (uint16)(numbytes + header_size) ;
    if ( hiorder ) {
      HighOrder2Bytes( p ) ;
    } else {
      LowOrder2Bytes( p ) ;
    }
  } else {
    *(p++) = 0 ;
    if ( hiorder ) { /* the top-level length is 1 */
      *(p++) = 0 ;
      *(p++) = 1 ;
    } else {
      *(p++) = 1 ;
      *(p++) = 0 ;
    }
    *( ( uint32 * ) p ) = (uint32)(numbytes + header_size) ;
    if ( hiorder ) { /* the overall length */
      HighOrder4Bytes( p ) ;
    } else {
      LowOrder4Bytes( p ) ;
    }
  }

  if ( ! write_binary_array( pobj , 1 , psequence )) {
    mm_free(mm_pool_temp,psequence,allocsize);
    return FALSE ;
  }

  /* set tag byte in the top level object in the sequence */
  *( psequence + 1 ) = (uint8)thetag ;

  /* write the header out to flptr */
  p = header ;
  for (i = 0 ; i < header_size ; i++ , p++ ) {
    if ( Putc( *p , flptr ) == EOF ) {
      mm_free(mm_pool_temp,psequence,allocsize ) ;
      return (*theIFileLastError( flptr ))( flptr ) ;
    }
  }

  /* write the buffer out to flptr */
  p = psequence ;
  while (numbytes--) {
    if ( Putc( *p , flptr ) == EOF ) {
      mm_free(mm_pool_temp,psequence,allocsize ) ;
      return (*theIFileLastError( flptr ))( flptr ) ;
    }
    p++ ;
  }

  /* free the scratch space */
  mm_free(mm_pool_temp,psequence,allocsize ) ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            write_binary_array(..)       author:   Luke Tunmer
   creation date:       16-May-1991       last modification:   ##-###-####
   arguments:
   description:

   This procedure recurses through the object to write out the
   objects into a buffer.

---------------------------------------------------------------------------- */
static Bool write_binary_array( register OBJECT *pobj,
                                int32           length,
                                register uint8  *p )
{
  int32 i ;
  int32 len ;
  int32 literal ;
  uint8 *newstart ;

  for ( i = 0 ; i < length ; i++ , pobj++ ) {
    if ( oExecutable(*pobj) )
      literal = EXECUTABLE ;
    else
      literal = 0 ;

    switch ( oType(*pobj) ) {
    case ONULL :
      *(p++) = (uint8)(BNULL | literal) ;
      *(p++) = 0;
      *( (int16 *) p) = 0 ;
      p += 2 ;
      *( (int32 *) p) = 0 ;
      p += 4 ;
      break ;
    case OINTEGER :
      *(p++) = (uint8)(BINTEGER | literal) ;
      *(p++) = 0 ;
      *( (int16 *) p) = 0 ;
      p += 2 ;
      *( (int32 *) p) = oInteger(*pobj) ;
      if ( hiorder ) {
        HighOrder4Bytes( p ) ;
      } else {
        LowOrder4Bytes( p ) ;
      }
      p += 4 ;
      break ;
    case OREAL :
      *(p++) = (uint8)(BREAL | literal) ;
      *p++ = 0 ;
      *( (uint16 * ) p ) = ( uint16 ) 0 ;
      p += 2 ;
      HQASSERT( sizeof_uservalue == 4, "USERVALUE has wrong size");
      *( (USERVALUE *) p) = oReal(*pobj) ;
      if ( ieee ) {
        * ( (USERVALUE *) p ) = NativeToIEEE( * (USERVALUE *) p );
      }
      if ( hiorder ) {
        HighOrder4Bytes( p ) ;
      } else {
        LowOrder4Bytes( p ) ;
      }
      p += 4 ;
      break ;
    case ONAME :
      *(p++) = (uint8)(BNAME | literal) ;
      *(p++) = 0 ;
      len = (int32)theINLen( oName(*pobj)) ;
      *( ( uint16 * ) p ) = ( uint16 ) len ;
      if ( hiorder ) {
        HighOrder2Bytes( p ) ;
      } else {
        LowOrder2Bytes( p ) ;
      }
      p += 2 ;
      /* stuff the name at the top end of the buffer */
      top_free -= len ;
      HqMemCpy( top_free , theICList( oName(*pobj)) , len ) ;
      *( ( uint32 * ) p ) = ( uint32 ) ( top_free - psequence ) ;
      if ( hiorder ) {
        HighOrder4Bytes( p ) ;
      } else {
        LowOrder4Bytes( p ) ;
      }
      p += 4 ;
      break ;
    case OBOOLEAN :
      *(p++) = (uint8)(BBOOLEAN | literal) ;
      *(p++) = 0 ;
      *( (int16 *) p) = 0 ;
      p += 2 ;
      *( ( int32 * ) p ) = oBool(*pobj) ;
      if ( hiorder ) {
        HighOrder4Bytes( p ) ;
      } else {
        LowOrder4Bytes( p ) ;
      }
      p += 4 ;
      break ;
    case OSTRING :
      *(p++) = (uint8)(BSTRING | literal) ;
      *(p++) = 0 ;
      len = (int32)theLen(*pobj) ;
      *( ( uint16 * ) p ) = ( uint16 ) len ;
      if ( hiorder ) {
        HighOrder2Bytes( p ) ;
      } else {
        LowOrder2Bytes( p ) ;
      }
      p += 2 ;
      /* stuff the string at the top end of the buffer */
      top_free -= len ;
      HqMemCpy( top_free , oString(*pobj) , len ) ;
      *( ( uint32 * ) p ) = ( uint32 ) ( top_free - psequence ) ;
      if ( hiorder ) {
        HighOrder4Bytes( p ) ;
      } else {
        LowOrder4Bytes( p ) ;
      }
      p += 4 ;
      break ;
    case OARRAY :
      *(p++) = (uint8)(BARRAY | literal) ;
      *(p++) = 0;
      len = (int32)theLen(*pobj) ;
      *( ( uint16 * ) p ) = ( uint16 ) len ;
      if ( hiorder ) {
        HighOrder2Bytes( p ) ;
      } else {
        LowOrder2Bytes( p ) ;
      }
      p += 2 ;
      *( ( uint32 * ) p ) = (uint32)(bot_free - psequence) ;
      if ( hiorder ) {
        HighOrder4Bytes( p ) ;
      } else {
        LowOrder4Bytes( p ) ;
      }
      p += 4 ;
      newstart = bot_free ;
      bot_free += (len << 3) ; /* 8 bytes per object */
      /* recurse on the array */
      if ( ! write_binary_array( oArray(*pobj) ,
                                len , newstart ))
        return FALSE ;
      break ;
    case OMARK :
      *(p++) = (uint8)(BMARK | literal) ;
      *(p++) = 0;
      *( (int16 *) p) = 0 ;
      p += 2 ;
      *( (int32 *) p) = 0 ;
      p += 4 ;
      break ;
    default :
      return error_handler( UNREGISTERED ) ;
    }
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            get_sequence_length(..)      author:   Luke Tunmer
   creation date:       16-May-1991       last modification:   ##-###-####
   arguments:
   description:

   This procedure recurses through the composite object to calculate the
   total number of bytes required to transmit the binary sequence. It updates
   this total through the variable ret_length. It returns with an error if
   procedure descends too far into subarrays.

---------------------------------------------------------------------------- */
static Bool get_sequence_length( register OBJECT *pobj,
                                 register int32  *ret_length,
                                 int32 level )
{
  if ( level > MAX_ARRAY_DEPTH )
    return error_handler( LIMITCHECK ) ;
  switch (oType(*pobj)) {
  case ONULL :
  case OINTEGER :
  case OREAL :
  case OBOOLEAN :
  case OMARK :
    (*ret_length) += 8 ;
    break ;
  case ONAME :
    (*ret_length) += ( 8 + theINLen( oName(*pobj))) ;
    break ;
  case OSTRING :
    if ( ! oCanRead(*pobj))
      return error_handler( INVALIDACCESS ) ;
    (*ret_length) += ( 8 + (int32)theLen(*pobj)) ;
    break ;
  case OARRAY :
    if ( ! oCanRead(*pobj))
      return error_handler( INVALIDACCESS ) ;
    (*ret_length) += 8 ;
    {
      int32 i ;
      int32 l = (int32)theLen(*pobj) ;
      pobj = oArray(*pobj) ;
      for ( i = 0 ; i < l ; i++ , pobj++ ) {
        if ( ! get_sequence_length( pobj , ret_length , level + 1 ))
          return FALSE ;
      }
    }
    break ;
  default :
    return error_handler( TYPECHECK ) ;
  }
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            setobjectformat_(..)  author:              Luke Tunmer
   creation date:       21-May-1991          last modification:   ##-###-####
   arguments:
   description:

   See p.510 Level-2 Red book.

---------------------------------------------------------------------------- */
Bool setobjectformat_(ps_context_t *pscontext)
{
  register OBJECT *theo ;
  register int32    code ;
  register int32    ssize ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  code = oInteger(*theo) ;
  if ( ( code < 0 ) || ( code > 4 ))
    return error_handler( RANGECHECK ) ;

  /* set current objectformat */
  if ( code != theIObjectFormat( workingsave )) {
    theIObjectFormat( workingsave ) = code ;
    /* remap the chartypes so that chars can be scanned correctly */
    remap_bin_token_chars() ;
  }

  pop( &operandstack ) ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            currentobjectformat_(..)         author:   Luke Tunmer
   creation date:       21-May-1991          last modification:   ##-###-####
   arguments:           none
   description:

   See p.389 Level-2 Red book.

---------------------------------------------------------------------------- */
Bool currentobjectformat_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger( inewobj ) = theIObjectFormat( workingsave ) ;

  return push( &inewobj , &operandstack ) ;
}


/* Log stripped */
