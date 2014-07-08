/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:psstacks.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Setting up the stacks.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "stacks.h"
#include "gcscan.h"

/* MAIN THREE STACKS, GRAPHICS STACK AND GRAPHICS STATE  */
/* NOTE: Main three stacks store 1 less than actual size */
STACK operandstack   = { EMPTY_STACK , NULL , 65535, STACK_TYPE_OPERAND } ;
STACK executionstack = { EMPTY_STACK , NULL , 65535, STACK_TYPE_EXECUTION } ;
STACK dictstack      = { EMPTY_STACK , NULL , 65535, STACK_TYPE_DICTIONARY } ;
STACK temporarystack = { EMPTY_STACK , NULL , 65535, STACK_TYPE_OPERAND } ;

static mps_root_t stackroot;

static void init_C_globals_psstacks(void)
{
  STACK operandstack_init   = { EMPTY_STACK , NULL , 65535, STACK_TYPE_OPERAND } ;
  STACK executionstack_init = { EMPTY_STACK , NULL , 65535, STACK_TYPE_EXECUTION } ;
  STACK dictstack_init      = { EMPTY_STACK , NULL , 65535, STACK_TYPE_DICTIONARY } ;
  STACK temporarystack_init = { EMPTY_STACK , NULL , 65535, STACK_TYPE_OPERAND } ;
  operandstack = operandstack_init ;
  executionstack = executionstack_init ;
  dictstack = dictstack_init ;
  temporarystack = temporarystack_init ;
  stackroot = NULL ;
}

static mps_res_t MPS_CALL scanStacks(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res;

  UNUSED_PARAM( void *, p ); UNUSED_PARAM( size_t, s );
  res = ps_scan_stack( ss, &operandstack ) ;
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_stack( ss, &executionstack ) ;
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_stack( ss, &dictstack ) ;
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_stack( ss, &temporarystack ) ;
  return res;
}

/* allocPSStacks -- allocate the stacks and declare them as GC roots */
static Bool ps_stacks_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( (theStackFrame(operandstack) = mm_alloc_static(sizeof(SFRAME))) == NULL ||
       (theStackFrame(executionstack) = mm_alloc_static(sizeof(SFRAME))) == NULL ||
       (theStackFrame(dictstack) = mm_alloc_static(sizeof(SFRAME))) == NULL ||
       (theStackFrame(temporarystack) = mm_alloc_static(sizeof(SFRAME))) == NULL )
    return FALSE ;

  /* Create root last so we force cleanup on success. */
  if ( (mps_root_create(&stackroot, mm_arena, mps_rank_exact(),
                        0, scanStacks, NULL, 0)) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}


/* finishPSStacks -- clean up: deregister the stack roots */
static void ps_stacks_finish(void)
{
  mps_root_destroy(stackroot);
}

void ps_stacks_C_globals(core_init_fns *fns)
{
  init_C_globals_psstacks() ;

  fns->swstart = ps_stacks_swstart ;
  fns->finish = ps_stacks_finish ;
}

/* Log stripped */
