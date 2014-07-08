/** \file
 * \ingroup RDR
 *
 * $HopeName: SWrdr!src:rdrglue.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2014 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for
 * any reason except as set forth in the applicable Global Graphics license
 * agreement.
 *
 * \brief  This file provides interfacing between the core ROM Data Resource API
 * and the skin.
 */

#define RDR_IMPLEMENTOR
#include "rdrpriv.h"
#include "apis.h"

/* -------------------------------------------------------------------------- */
/* Define the API structure, and the skin-visible version of the API pointer */

static sw_rdr_api_20110201 rdr_api_20110201 = {
  FALSE,
  SwRegisterRDR,
  SwRegisterRDRandID,
  SwDeregisterRDR,
  SwFindRDR,
  SwFindRDRbyType,
  SwFindRDRbyClass,
  SwFindRDRs,
  SwNextRDR,
  SwFoundRDR,
  SwRestartFindRDR,
  SwLockNextRDR
} ;

/* This is the API pointer that will be used by the skin */
sw_rdr_api_20110201 * rdr_api = &rdr_api_20110201 ;

static int booted = 0 ;  /* bitfield set during rdr_start() */

/* -------------------------------------------------------------------------- */

static void init_C_globals_rdr(void)
{
  rdr_list  = NULL ;
  live_list = NULL ;
  dead_list = NULL ;
  rdr_iterations = 0 ;
  booted = 0 ;
  rdr_api_20110201.valid = FALSE ;
}


/* -------------------------------------------------------------------------- */
/** \brief Initialise the RDR system

    The mutex and control variables are created and configured, and the
    concurrency count zeroed.
*/

int HQNCALL rdr_start(void)
{
  HQASSERT(booted == 0, "rdr_start being called multiple times?") ;

  init_C_globals_rdr() ;

  /* pthreads initialisation is required */
  if (!pthread_api->valid) {
    HQFAIL("pthreads not initialised") ;
    return FAILURE(FALSE) ;
  }

  /* Create and initialise mutex */
  if (pthread_mutex_init(&rdr_mt, NULL)) {
    HQFAIL("Can't init mutex") ;
    return FAILURE(FALSE) ;
  }
  booted |= 1 ;

  /* Create and initialise condition variable */
  if (pthread_cond_init(&rdr_cv, NULL)) {
    HQFAIL("Can't init condition var") ;
    rdr_end() ;
    return FAILURE(FALSE) ;
  }
  booted |= 2 ;

  rdr_api_20110201.valid = TRUE ;

  /* Register the RDR API as an RDR API! */
  if (SwRegisterRDR(RDR_CLASS_API, RDR_API_RDR, 20110201,
                    &rdr_api_20110201, sizeof(rdr_api_20110201), 0)) {
    HQFAIL("RDR API registration failed") ;
    rdr_end() ;
    return FAILURE(FALSE) ;
  }
  booted |= 4 ;

  /* The pthreads API was defined by ggsthreads.c but could not be registered
     then as it has to start before RDR.  Register all supported versions of
     the thread api. Version 20111021 is a strict superset of 20071026 so
     we can fake a 20071026 version. */
  if (SwRegisterRDR(RDR_CLASS_API, RDR_API_PTHREADS, 20071026,
                    pthread_api, sizeof(sw_pthread_api_20071026), 0)) {
    HQFAIL("pthreads API registration failed") ;
    rdr_end() ;
    return FAILURE(FALSE) ;
  }

  if (SwRegisterRDR(RDR_CLASS_API, RDR_API_PTHREADS, 20111021,
                    pthread_api, sizeof(sw_pthread_api_20111021), 0)) {
    HQFAIL("pthreads API registration failed") ;
    rdr_end() ;
    return FAILURE(FALSE) ;
  }

  booted |= 8 ;

  return TRUE ;
}


/** \brief  Finalise the RDR system.

    The control variable and mutex are destroyed, and allocations discarded.
*/

void HQNCALL rdr_end(void)
{
  sw_rdr * rdr, * next ;
  sw_rdr_iterator * it, * nit ;

  if ((booted & 8) != 0) {
    booted &= ~8 ;
    (void) SwDeregisterRDR(RDR_CLASS_API, RDR_API_PTHREADS, 20071026,
                           (sw_pthread_api_20071026*)pthread_api,
                           sizeof(sw_pthread_api_20071026)) ;
    (void) SwDeregisterRDR(RDR_CLASS_API, RDR_API_PTHREADS, 20111021,
                           (sw_pthread_api_20111021*)pthread_api,
                           sizeof(sw_pthread_api_20111021)) ;
  }

  if ((booted & 4) != 0) {
    booted &= ~4 ;
    (void) SwDeregisterRDR(RDR_CLASS_API, RDR_API_RDR, 20110201,
                           &rdr_api_20110201, sizeof(rdr_api_20110201)) ;
  }

  rdr_api_20110201.valid = FALSE ;

  HQASSERT(pthread_api && pthread_api->valid, "pthreads ended too soon for RDR") ;

  if ((booted & 2) != 0) {
    booted &= ~2 ;
    (void) pthread_cond_destroy(&rdr_cv) ;
  }

  if ((booted & 1) != 0) {
    booted &= ~1 ;
    (void) pthread_mutex_destroy(&rdr_mt) ;
  }

  rdr = rdr_list ;
  rdr_list = NULL ;
  while (rdr) {
    next = rdr->next ;
    rdr_free(rdr, sizeof(sw_rdr)) ;
    rdr = next ;
  }

  it = live_list ;
  live_list = NULL ;
  while (it) {
    nit = it->next ;
    rdr_free(it, sizeof(sw_rdr_iterator)) ;
    it = nit ;
  }

  it = dead_list ;
  dead_list = NULL ;
  while (it) {
    nit = it->next ;
    rdr_free(it, sizeof(sw_rdr_iterator)) ;
    it = nit ;
  }

  booted = 0 ;

  return ;
}

/* ========================================================================== */
