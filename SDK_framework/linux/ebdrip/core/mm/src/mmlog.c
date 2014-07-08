/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!src:mmlog.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Basic MMI logging code (that's the low level one)
 */

#include <stdarg.h>
#include <stdio.h>

#include "core.h"
#include "mm.h"
#include "mmlog.h"


/* Nothing doing, if MM_DEBUG_LOGGING isn't compiled in */
#ifdef MM_DEBUG_LOGGING

static FILE *logf = NULL ; /* whither to log */
static int logging = 0 ;   /* whether to log */


void mm_log_init( void )
{
  FILE *f ;

  f = fopen( "mmilog", "w" ) ;
  if ( f == NULL )
    logging = FALSE ;
  else {
    logf = f ;
    logging = TRUE ;
  }
}

void mm_log_finish( void )
{
  if ( logging ) {
    ( void )fclose( logf ) ;
    logging = FALSE ;
  }
}

void mm_log( char *id, char *fmt, ... )
{
  va_list params ;

  if ( logging ) {
    fputs( id, logf ) ;

    va_start( params, fmt ) ;
    vfprintf( logf, fmt, params ) ;
    va_end( params ) ;

    fputs( "\n", logf ) ;
  }
}

#endif

void init_C_globals_mmlog(void)
{
#ifdef MM_DEBUG_LOGGING
  logf = NULL ;
  logging = 0 ;
#endif
}

/* Log stripped */
