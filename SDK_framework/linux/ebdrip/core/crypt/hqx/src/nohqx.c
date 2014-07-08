/** \file
 * \ingroup hqx
 *
 * $HopeName: COREcrypt!hqx:src:nohqx.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for HQX encryption functions when compiled out.
 */


#include "core.h"
#include "swerrors.h"
#include "fileioh.h"
#include "swdevice.h"
#include "hqxcrypt.h"
#include "hqxfonts.h"

uint32 HqxDepth = 0 ;
int32 hqxdataextra = 0 ;
int32 hqxdatastart = 0 ;

static void init_C_globals_nohqx(void)
{
  HqxDepth = 0 ;
  hqxdataextra = 0 ;
  hqxdatastart = 0 ;
}

void hqx_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;
  init_C_globals_nohqx() ;
}

Bool hqxNonPSSetup(FILELIST *flptr)
{
  UNUSED_PARAM(FILELIST *, flptr) ;
  return FALSE ;
}

int32 hqxfont_test(DEVICE_FILEDESCRIPTOR fd, DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd) ;
  UNUSED_PARAM(DEVICELIST *, dev) ;
  return 1 ; /* non-zero indicates not Hqx */
}

void hqx_crypt_region(uint8 *cc, int32 pos, int32 length, int32 flag)
{
  UNUSED_PARAM(uint8 *, cc) ;
  UNUSED_PARAM(int32, pos) ;
  UNUSED_PARAM(int32, length) ;
  UNUSED_PARAM(int32, flag) ;
}

int32 hqx_check_leader_buffer(uint8 * buffer, int32 * pos, int32 key, int32 * seedout)
{
  UNUSED_PARAM(uint8 *, buffer) ;
  UNUSED_PARAM(int32 *, pos) ;
  UNUSED_PARAM(int32, key) ;
  UNUSED_PARAM(int32 *, seedout) ;
  return -1 ; /* Negative indicates error */
}

Bool _hqxrun_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return error_handler(UNDEFINED) ;
}

Bool _hqxstop_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return error_handler(UNDEFINED) ;
}

/* Log stripped */
