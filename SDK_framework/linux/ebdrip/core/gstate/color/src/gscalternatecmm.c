/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscalternatecmm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Link into Custom Color Management modules
 */

#include "core.h"
#include "coreinit.h"

#include "swcmm.h"              /* sw_cmm_instance */
#include "swblobapi.h"          /* sw_blob_instance */
#include "uvms.h"               /* UVS */
#include "hqmemcmp.h"           /* HqMemCmp */
#include "hqmemset.h"
#include "mmcompat.h"           /* mm_alloc_with_header */
#include "objects.h"            /* oType */
#include "swerrors.h"           /* error_handler */
#include "swdevice.h"           /* SW_RDONLY */
#include "hqunicode.h"          /* utf8_validate */

#include "gs_colorpriv.h"       /* CLINK */
#include "gsccmmpriv.h"         /* TRANSFORM_LINK_INFO */
#include "gschead.h"            /* gsc_getcolorspacesizeandtype */
#include "gschcmspriv.h"        /* cc_getAlternateCMM */
#include "gscicc.h"             /* cc_get_icc_details */
#include "cmm_module.h"         /* cmm_init et al */
#include "mm_swmemapi.h"        /* mm_swmemory_create et al */

#include "gscalternatecmmpriv.h"

#include "namedef_.h"           /* NAME_* */

#define CLID_SIZEcustomcmm      (2)

static void  alternatecmm_destroy(CLINK *pLink);
static Bool alternatecmm_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool alternatecmm_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t alternatecmm_scan( mps_ss_t ss, CLINK *pLink );

static size_t alternatecmmStructSize(void);
static void alternatecmmUpdatePtrs(CLINK *pLink);

#if defined( ASSERT_BUILD )
static void alternatecmmAssertions(CLINK *pLink);
#else
#define alternatecmmAssertions(_pLink) EMPTY_STATEMENT()
#endif


static void  customcmm_destroy(CLINK *pLink);
static int32 customcmm_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static int32 customcmm_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t customcmm_scan( mps_ss_t ss, CLINK *pLink );

static size_t customcmmStructSize(void);
static void customcmmUpdatePtrs(CLINK *pLink);
static size_t customcmmInfoStructSize(void);

#if defined( ASSERT_BUILD )
static void customcmmAssertions(CLINK *pLink);
static void customcmmInfoAssertions(CUSTOM_CMM_INFO  *customcmmInfo);
#else
#define customcmmAssertions(_pLink) EMPTY_STATEMENT()
#define customcmmInfoAssertions(_pLink) EMPTY_STATEMENT()
#endif

/** Structure to hold registered alternate CMMs. The CMM API is instantiated
    as a singleton, so we store the instance in the same memory allocation. */
typedef struct CMM_API_LIST {
  /** Quick reference to the implementation. This is also present as a
      pointer in the instance. */
  sw_cmm_api          *cmmApi;

  /** Singleton instance, allocated at the end of this structure. */
  sw_cmm_instance     *instance ;

  /** Number of custom colorspaces detected when we constructed the
      instance. */
  int32               n_CustomColorSpaces;

  /** Pointer to next registered module. */
  struct CMM_API_LIST *next;
} CMM_API_LIST;

static CMM_API_LIST   *gCMM_apiList = NULL;

static sw_memory_instance *cmm_memory_callbacks = NULL ;

/*---------------------------------------------------------------------------*/

int32 error_from_sw_cmm_result(sw_cmm_result error)
{
  switch (error) {
  case SW_CMM_ERROR:
    return UNDEFINED ;
  case SW_CMM_ERROR_IOERROR:
    return IOERROR;
  case SW_CMM_ERROR_MEMORY:
    return VMERROR;
  case SW_CMM_ERROR_INVALID:
    return SYNTAXERROR;
  case SW_CMM_ERROR_VERSION:
  case SW_CMM_ERROR_UNSUPPORTED:
    return CONFIGURATIONERROR;
  case SW_CMM_SUCCESS:
    HQFAIL("Alternate CMM should not have returned success") ;
    return UNREGISTERED;
  }

  HQFAIL("No translation for CMM error code") ;
  return UNREGISTERED ;
}

/*---------------------------------------------------------------------------*/

/** Dispose of an individual CMM entry, removing it from the list. This does
    not call the finish() method, because this may be called during startup,
    before initialisation of the registered API. */
static void cmm_entry_dispose(CMM_API_LIST **pentry)
{
  CMM_API_LIST *entry ;
  sw_cmm_api *api ;

  HQASSERT(pentry, "Nowhere to find CMM entry") ;

  entry = *pentry ;
  HQASSERT(entry, "No CMM entry") ;

  api = entry->cmmApi ;
  HQASSERT(api, "No implementation in CMM entry") ;

  *pentry = entry->next ;

  mm_free(mm_pool_color, entry,
          sizeof(CMM_API_LIST) + api->info.instance_size) ;
}

/** Initialise an individual CMM entry, and construct an instance for it. If
    the initialisation or construction fails, the entry is cleaned up and
    disposed of. */
static Bool cmm_entry_init_construct(CMM_API_LIST **pentry)
{
  CMM_API_LIST *entry ;
  sw_cmm_api *api ;
  sw_cmm_instance *instance ;
  sw_cmm_init_params params ;

  HQASSERT(pentry, "Nowhere to find CMM entry") ;

  entry = *pentry ;
  HQASSERT(entry, "No CMM entry") ;

  api = entry->cmmApi ;
  HQASSERT(api, "No implementation in CMM entry") ;

  instance = entry->instance ;
  HQASSERT(instance, "No instance in CMM entry") ;

  HQASSERT(cmm_memory_callbacks, "CMM memory callbacks not initialised") ;
  params.mem = cmm_memory_callbacks ;

  /* Initialise the implementation. */
  if ( api->init == NULL || api->init(api, &params) ) {
    /* Construct a singleton instance, starting by filling in RIP pointers. */
    instance->implementation = api ;
    instance->mem = cmm_memory_callbacks ;

    if ( api->construct(instance) == SW_CMM_SUCCESS ) {
      int32 i = 0 ;

      /* Find number of custom colorspaces */
      if ( api->declare_custom_colorspace != NULL ) {
        while ( api->declare_custom_colorspace(instance, i) != NULL )
          ++i ;
      }

      entry->n_CustomColorSpaces = i ;
      return TRUE ;
    } else if ( api->finish != NULL ) {
      /* If the construction failed, finalise the API. */
      api->finish(api) ;
    }
  }

  /* Initialisation failed. Remove from the entry list. */
  cmm_entry_dispose(pentry) ;

  return FAILURE(FALSE) ;
}

/** Destruct and finalise an individual CMM entry, removing it from the
    list. */
static void cmm_entry_destruct_finish(CMM_API_LIST **pentry)
{
  CMM_API_LIST *entry ;
  sw_cmm_api *api ;
  sw_cmm_instance *instance ;

  HQASSERT(pentry, "Nowhere to find CMM entry") ;

  entry = *pentry ;
  HQASSERT(entry, "No CMM entry") ;

  api = entry->cmmApi ;
  HQASSERT(api, "No implementation in CMM entry") ;

  instance = entry->instance ;
  HQASSERT(instance, "No instance in CMM entry") ;

  /* Destroy singleton instance. */
  if ( api->destruct != NULL )
    api->destruct(instance) ;

  /* Finalise the implementation. */
  if ( api->finish != NULL )
    api->finish(api) ;

  cmm_entry_dispose(pentry) ;
}

/*---------------------------------------------------------------------------*/

enum {
  CMM_NOT_INITIALISED,
  CMM_PREBOOT,
  CMM_STARTED
} ;

static int cmm_api_state = CMM_NOT_INITIALISED ;

static void init_C_globals_gscalternatecmm(void)
{
  cmm_api_state = CMM_NOT_INITIALISED ;
  gCMM_apiList = NULL ;
  cmm_memory_callbacks = NULL ;
}

/** Pre-boot initialisation for alternate CMMs.
 * This is called from SwInit(), before the interpreter is booted.
 */
static Bool cmm_swinit(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  HQASSERT(cmm_api_state == CMM_NOT_INITIALISED, "CMM API state incorrect") ;
  cmm_api_state = CMM_PREBOOT ;

  return TRUE ;
}

/** Post-boot initialisation for alternate CMMs. This is called after the
 * interpreter is booted. We jump through some hoops to ensure that if an
 * init or construct failure happens in the middle of the list, we cleanup
 * the previously-constructed modules (the cmm_finish function will not be
 * called if we return FALSE from this routine).
 */
static Bool cmm_postboot(void)
{
  CMM_API_LIST **pentry = &gCMM_apiList;

  HQASSERT(cmm_api_state == CMM_PREBOOT, "CMM API state incorrect") ;

  if ( (cmm_memory_callbacks = mm_swmemory_create(mm_pool_color,
                                                  MM_ALLOC_CLASS_CMM)) == NULL )
    return FALSE ;

  while ( *pentry != NULL ) {
    if ( !cmm_entry_init_construct(pentry) ) {
      /* Failed to construct an entry. Start by disposing of all not-yet
         constructed entries. */
      while ( *pentry )
        cmm_entry_dispose(pentry) ;

      /* Now dispose of all previously-constructed entries. */
      while ( gCMM_apiList )
        cmm_entry_destruct_finish(&gCMM_apiList) ;

      /* Destroy the memory callback instance. */
      mm_swmemory_destroy(&cmm_memory_callbacks) ;

      return FALSE ;
    }

    pentry = &(*pentry)->next ;
  }

  cmm_api_state = CMM_STARTED ;

  return TRUE ;
}

/** Finalisation for alternate CMMs.
 * This is called just before shutdown.
 */
static void cmm_finish(void)
{
  while ( gCMM_apiList )
    cmm_entry_destruct_finish(&gCMM_apiList) ;

  mm_swmemory_destroy(&cmm_memory_callbacks) ;

  cmm_api_state = CMM_NOT_INITIALISED ;
}

void cmm_C_globals(struct core_init_fns *fns)
{
  init_C_globals_gscalternatecmm() ;

  fns->swinit = cmm_swinit ;
  fns->postboot = cmm_postboot ;
  fns->finish = cmm_finish ;
}

sw_api_result RIPCALL SwRegisterCMM(sw_cmm_api *impl)
{
  CMM_API_LIST  **entry;
  CMM_API_LIST  *newEntry;

  /* Need to call SwInit before trying to register CMMs */
  if ( cmm_api_state == CMM_NOT_INITIALISED )
    return FAILURE(SW_API_TOO_EARLY) ;

  if ( impl == NULL ||
       impl->info.name == NULL || *impl->info.name == '\0' ||
       impl->info.display_name == NULL || *impl->info.display_name == '\0' ||
       impl->construct == NULL ||
       /* If we can declare custom colorspaces, we must be able to open them. */
       (impl->declare_custom_colorspace != NULL &&
        impl->open_custom_colorspace == NULL) ||
       impl->open_profile == NULL ||
       impl->close_profile == NULL ||
       impl->open_transform == NULL ||
       impl->invoke_transform == NULL ||
       impl->close_transform == NULL )
    return FAILURE(SW_API_INCOMPLETE) ;

  /* Check versions and sizes. Every supported version should have a case to
     check its instance size here. */
  switch ( impl->info.version ) {
  case SW_CMM_API_VERSION:
    /* The current version should always request at least enough for the
       current instance. */
    if ( impl->info.instance_size < sizeof(sw_cmm_instance) )
      return FAILURE(SW_API_INSTANCE_SIZE);
    break ;
  default:
    if (impl->info.version > SW_CMM_API_VERSION)
      return FAILURE(SW_API_VERSION_TOO_NEW);
    return FAILURE(SW_API_VERSION_TOO_OLD);
  }

  /* Validate display_name for UTF-8 encoding. This call returns the number
     of invalid code points. */
  if ( utf8_validate(impl->info.display_name,
                     NULL, /* zero terminated */
                     NULL, /* don't need number of UTF8 units */
                     NULL, /* don't need number of UTF16 units */
                     NULL  /* don't need number of code points units */
                     ) > 0 )
    return FAILURE(SW_API_BAD_UNICODE) ;

  /* Find the end of the list of CMM's */
  for ( entry = &gCMM_apiList; *entry != NULL ; entry = &(*entry)->next ) {
    /* Validate the uniqueness of 'name' */
    if (strcmp((char *) (*entry)->cmmApi->info.name,
               (char *) impl->info.name) == 0)
      return FAILURE(SW_API_NOT_UNIQUE);
  }

  /* Allocate new list entry */
  newEntry = mm_alloc(mm_pool_color,
                      sizeof(CMM_API_LIST) + impl->info.instance_size,
                      MM_ALLOC_CLASS_NCOLOR);
  if (newEntry == NULL)
    return FAILURE(SW_API_ERROR);

  /* Append new instance to end of list */
  newEntry->cmmApi = impl;
  newEntry->instance = (sw_cmm_instance *)(newEntry + 1) ;
  HqMemZero(newEntry->instance, impl->info.instance_size) ;
  newEntry->n_CustomColorSpaces = 0;
  newEntry->next = NULL;

  *entry = newEntry;

  /* If we've started up the RIP already, immediately initialise and
     construct the entry. */
  if ( cmm_api_state == CMM_STARTED && !cmm_entry_init_construct(entry) )
    return FAILURE(SW_API_INIT_FAILED) ;

  return SW_API_REGISTERED;
}

/*---------------------------------------------------------------------------*/

sw_cmm_instance *cc_findAlternateCMM(OBJECT *cmmName)
{
  CMM_API_LIST *entry;
  int32 length;

  HQASSERT(oType(*cmmName) == OSTRING, "Expected a string object");

  length = theLen(*cmmName);

  for ( entry = gCMM_apiList; entry != NULL ; entry = entry->next ) {
    if (HqMemCmp(entry->cmmApi->info.name, strlen_int32((char *)entry->cmmApi->info.name),
                 oString(*cmmName), length) == 0)
      return entry->instance;
  }

  return NULL;
}


Bool cc_cmmSupportInputProfiles(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_input_profiles;
}

Bool cc_cmmSupportOutputProfiles(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_output_profiles;
}

Bool cc_cmmSupportDevicelinkProfiles(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_devicelink_profiles;
}

Bool cc_cmmSupportColorspaceProfiles(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_colorspace_profiles;
}

Bool cc_cmmSupportAbstractProfiles(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_abstract_profiles;
}

Bool cc_cmmSupportNamedColorProfiles(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_named_color_profiles;
}

Bool cc_cmmSupportICCv4Profiles(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_ICC_v4;
}

Bool cc_cmmSupportBlackPointCompensation(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_black_point_compensation;
}

Bool cc_cmmSupportExtraAbsoluteIntents(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->support_extra_absolute_intents;
}

uint32 cc_cmmMaxInputChannels(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->maximum_input_channels;
}

uint32 cc_cmmMaxOutputChannels(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->maximum_output_channels;
}

Bool cc_cmmAllowRetry(sw_cmm_instance *cmmInstance)
{
  HQASSERT(cmmInstance != NULL, "No CMM instance") ;
  return cmmInstance->allow_retry;
}

/* ---------------------------------------------------------------------- */

/*
 * Alternate CMM Link Data Access Functions
 * ========================================
 */

struct CLINKalternatecmm {
  cc_counter_t      refCnt;
  size_t            structSize;

  uint32            inputDimension;
  uint32            outputDimension;
  COLORSPACE_ID     inputSpaceId;
  COLORSPACE_ID     outputSpaceId;

  sw_cmm_instance   *cmmInstance;
  uint32            numProfiles;
  sw_cmm_profile    *cmmProfiles;               /* num_profiles */
  int32             *intents;                   /* num_profiles - 1 */
  Bool              *blackPointCompensations;   /* num_profiles - 1 */
  sw_cmm_transform  cmmTransform;
};

static CLINKfunctions CLINKalternatecmm_functions =
{
  alternatecmm_destroy,
  alternatecmm_invokeSingle,
  NULL /* alternatecmm_invokeBlock */,
  alternatecmm_scan
};


CLINK *cc_alternatecmm_create(TRANSFORM_LINK_INFO   *transformList,
                              sw_cmm_instance       *cmmInstance,
                              COLORSPACE_ID         *oColorSpace,
                              int32                 *colorspacedimension)
{
  int32 i;
  int32 numProfiles = 0;
  int32 idCount;
  sw_cmm_profile *cmmProfiles = NULL;
  int32 *intents = NULL;
  Bool *blackPointCompensations = NULL;
  sw_cmm_transform cmmTransform = NULL;
  CLINK *pLink = NULL;
  TRANSFORM_LINK_INFO *currentInfo;
  uint32 inputDimensions = 0;
  uint32 outputDimensions = 0;
  uint32 inputChannels;
  uint32 outputChannels;
  CLINKalternatecmm  *alternatecmm;
  COLORSPACE_ID inputSpaceId = SPACE_notset;
  COLORSPACE_ID outputSpaceId = SPACE_notset;
  const sw_cmm_api *cmmImplementation ;
  sw_cmm_result cmmresult ;

  HQASSERT(cmmInstance != NULL, "cmmInstance NULL");

  cmmImplementation = cmmInstance->implementation ;
  HQASSERT(cmmImplementation != NULL, "cmmImplementation NULL");

  /* Establish how many profiles are in the transform */
  for (currentInfo = transformList;
       currentInfo != NULL;
       currentInfo = currentInfo->next) {
    numProfiles++;

    /* Check that each currentInfo is an ICC struct */
    switch (currentInfo->inputColorSpaceId) {
    case SPACE_ICCBased:
    case SPACE_ICCXYZ:
    case SPACE_ICCLab:
      break;
    default:
      switch (currentInfo->outputColorSpaceId) {
      case SPACE_ICCXYZ:
      case SPACE_ICCLab:
        break;
      default:
        HQFAIL("Not an ICC list item");
        break;
      }
    }
  }
  HQASSERT(numProfiles > 0, "numProfiles zero");

  cmmProfiles = mm_sac_alloc(mm_pool_color,
                             numProfiles * sizeof(sw_cmm_profile),
                             MM_ALLOC_CLASS_NCOLOR);
  if (cmmProfiles == NULL) {
    (void) error_handler(VMERROR);
    goto tidyup;
  }
  /* Null the profiles */
  for (i = 0; i < numProfiles; i++)
    cmmProfiles[i] = NULL;

  if (numProfiles > 1) {
    intents = mm_sac_alloc(mm_pool_color,
                           (numProfiles - 1) * sizeof(int32),
                           MM_ALLOC_CLASS_NCOLOR);
    blackPointCompensations = mm_sac_alloc(mm_pool_color,
                                           (numProfiles - 1) * sizeof(Bool),
                                           MM_ALLOC_CLASS_NCOLOR);
    if (intents == NULL || blackPointCompensations == NULL) {
      (void) error_handler(VMERROR);
      goto tidyup;
    }
  }


  /* Open the profiles in the Alternate CMM */
  currentInfo = transformList;
  for (i = 0; i < numProfiles; i++) {
    int32 dimensions;
    COLORSPACE_ID deviceSpaceId;
    COLORSPACE_ID pcsSpaceId;
    sw_blob_instance *blob ;

    if (!cc_get_icc_details(currentInfo->u.icc,
                            TRUE,
                            &dimensions,
                            &deviceSpaceId,
                            &pcsSpaceId))
      goto tidyup;

    if (i == 0) {
      inputDimensions = dimensions;
      inputSpaceId = transformList->inputColorSpaceId;
    }
    if (i == numProfiles - 1) {
      outputDimensions = dimensions;
      outputSpaceId = deviceSpaceId;
    }

    /* Get the blob. If this succeeds, we must close it afterwards. */
    if ( (blob = cc_get_icc_blob(currentInfo->u.icc)) == NULL )
      goto tidyup;

    HQASSERT(blob->implementation, "Blob doesn't have an implementation") ;

    if ( (cmmresult = cmmImplementation->open_profile(cmmInstance, blob, &cmmProfiles[i])) != SW_CMM_SUCCESS ||
         cmmProfiles[i] == NULL ) {
      blob->implementation->close(&blob) ;
      (void)detail_error_handler(error_from_sw_cmm_result(cmmresult),
                                 "Error opening alternate CMM profile");
      goto tidyup ;
    }

    blob->implementation->close(&blob) ;

    /* There are one fewer intents than profiles, we'll skip over the one from
     * the first element in the list (they are normally all the same except when
     * using the SourceIntent hack in setreproduction).
     */
    if (i != 0) {
      intents[i - 1] = currentInfo->intent;
      blackPointCompensations[i - 1] = currentInfo->blackPointComp;
    }
    currentInfo = currentInfo->next;
  }
  HQASSERT(currentInfo == NULL, "currentInfo not NULL");

  /* Create a transform in the Alternate CMM */
  if ( (cmmresult = cmmImplementation->open_transform(cmmInstance,
                                                      cmmProfiles,
                                                      numProfiles,
                                                      intents,
                                                      blackPointCompensations,
                                                      &inputChannels,
                                                      &outputChannels,
                                                      &cmmTransform)) != SW_CMM_SUCCESS ||
       cmmTransform == NULL ) {
    (void)detail_error_handler(error_from_sw_cmm_result(cmmresult),
                               "Error opening alternate CMM transform");
    goto tidyup;
  }

  if (inputChannels != inputDimensions || outputChannels != outputDimensions) {
    alternatecmm_destroy(pLink);
    (void) detail_error_handler(RANGECHECK,
                                "CMM_open_transform hasn't returned the correct number of channels");
    goto tidyup;
  }


  *oColorSpace = outputSpaceId;
  *colorspacedimension = outputDimensions;
  idCount = 1 +                   /* alternate CMM */
            numProfiles +         /* profiles */
            numProfiles - 1 +     /* intents */
            numProfiles - 1;      /* black point compensations */

  pLink = cc_common_create(inputChannels,
                           NULL,
                           inputSpaceId,
                           outputSpaceId,
                           CL_TYPEalternatecmm,
                           alternatecmmStructSize(),
                           &CLINKalternatecmm_functions,
                           idCount);
  if ( pLink == NULL )
    goto tidyup ;

  alternatecmmUpdatePtrs(pLink);

  alternatecmm = pLink->p.alternatecmm ;
  alternatecmm->cmmInstance = cmmInstance;

  alternatecmm->numProfiles = numProfiles;
  alternatecmm->cmmProfiles = cmmProfiles;
  alternatecmm->intents = intents;
  alternatecmm->blackPointCompensations = blackPointCompensations;
  alternatecmm->cmmTransform = cmmTransform;

  /* Now populate the CLID slots:
   * We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   * For CL_TYPEalternatecmm (looking at invokeSingle) we have:
   * a) cmmInstance
   * b) the profiles
   * c) the rendering intents
   * d) the black point compensation
   * =
   * numProfiles + 1 slots.
   *
   */
  {
    CLID *idslot = pLink->idslot ;
    HQASSERT( pLink->idcount == idCount, "Didn't create as requested" ) ;
    idslot[0] = (CLID) alternatecmm->cmmInstance;

    currentInfo = transformList;
    for (i = 0; i < numProfiles; i++) {
      idslot[i + 1] = (CLID) currentInfo->u.icc;
      currentInfo = currentInfo->next;
    }
    currentInfo = transformList;
    for (i = 0; i < numProfiles - 1; i++) {
      idslot[i + numProfiles + 1] = (CLID) intents[i];
      currentInfo = currentInfo->next;
    }
    currentInfo = transformList;
    for (i = 0; i < numProfiles - 1; i++) {
      idslot[i + numProfiles * 2] = (CLID) blackPointCompensations[i];
      currentInfo = currentInfo->next;
    }
  }

  alternatecmmAssertions(pLink);

  return pLink;

tidyup:
  /*** @@JJ Make this lot common with alternatecmm_destroy() */
  if (cmmProfiles != NULL) {
    for (i = 0; i < numProfiles; i++) {
      if (cmmProfiles[i] != NULL) {
        cmmImplementation->close_profile(cmmInstance, cmmProfiles[i]);
      }
    }
    mm_sac_free(mm_pool_color, cmmProfiles, numProfiles * sizeof(sw_cmm_profile));
  }
  if (intents != NULL)
    mm_sac_free(mm_pool_color, intents, (numProfiles - 1) * sizeof(int32));
  if (blackPointCompensations != NULL)
    mm_sac_free(mm_pool_color, blackPointCompensations, (numProfiles - 1) * sizeof(Bool));
  if (cmmTransform) {
    cmmImplementation->close_transform(cmmInstance, cmmTransform);
  }
  if (pLink) {
    CLINKalternatecmm  *alternatecmm = pLink->p.alternatecmm;
    alternatecmm->cmmProfiles = NULL;
    alternatecmm->intents = NULL;
    alternatecmm->blackPointCompensations = NULL;
    alternatecmm->cmmTransform = NULL;

    alternatecmm_destroy(pLink);
  }

  return NULL;
}

static void alternatecmm_destroy(CLINK *pLink)
{
  int32 i;
  CLINKalternatecmm  *alternatecmm = pLink->p.alternatecmm;
  int32 numProfiles = alternatecmm->numProfiles;

  sw_cmm_instance *cmmInstance = alternatecmm->cmmInstance ;
  const sw_cmm_api *cmmImplementation = cmmInstance->implementation ;

  alternatecmmAssertions(pLink);

  if (alternatecmm->cmmProfiles != NULL) {
    for (i = 0; i < numProfiles; i++) {
      if (alternatecmm->cmmProfiles[i] != NULL) {
        cmmImplementation->close_profile(cmmInstance, alternatecmm->cmmProfiles[i]);
      }
    }
    mm_sac_free(mm_pool_color, alternatecmm->cmmProfiles, numProfiles * sizeof(sw_cmm_profile));
  }
  if (alternatecmm->intents != NULL)
    mm_sac_free(mm_pool_color, alternatecmm->intents, (numProfiles - 1) * sizeof(int32));
  if (alternatecmm->blackPointCompensations != NULL)
    mm_sac_free(mm_pool_color, alternatecmm->blackPointCompensations, (numProfiles - 1) * sizeof(Bool));
  if (alternatecmm->cmmTransform)
    cmmImplementation->close_transform(cmmInstance, alternatecmm->cmmTransform);

  cc_common_destroy(pLink);
}

static int32 alternatecmm_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  CLINKalternatecmm  *alternatecmm = pLink->p.alternatecmm;
  sw_cmm_instance *cmmInstance ;
  const sw_cmm_api *cmmImplementation ;
  sw_cmm_result cmmresult ;

  alternatecmmAssertions(pLink);

  cmmInstance = alternatecmm->cmmInstance ;
  cmmImplementation = cmmInstance->implementation ;

  if ( (cmmresult = cmmImplementation->invoke_transform(cmmInstance,
                                                        alternatecmm->cmmTransform,
                                                        pLink->iColorValues,
                                                        oColorValues,
                                                        1)) != SW_CMM_SUCCESS ) {
    return detail_error_handler(error_from_sw_cmm_result(cmmresult),
                                "Error invoking Alternate CMM transform");
  }

  return TRUE;
}


#ifdef INVOKEBLOCK_NYI
static int32 alternatecmm_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  alternatecmmAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */

/* alternatecmm_scan - Not currently necessary */
static mps_res_t alternatecmm_scan( mps_ss_t ss, CLINK *pLink )
{
  UNUSED_PARAM(mps_ss_t, ss);
  UNUSED_PARAM(CLINK *, pLink);

  return MPS_RES_OK;
}

static size_t alternatecmmStructSize(void)
{
  return sizeof(CLINKalternatecmm);
}

static void alternatecmmUpdatePtrs(CLINK *pLink)
{
  pLink->p.alternatecmm = (CLINKalternatecmm *)((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void alternatecmmAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEalternatecmm,
                      alternatecmmStructSize(),
                      &CLINKalternatecmm_functions);

  switch (pLink->iColorSpace) {
  case SPACE_ICCBased:
  case SPACE_CMM:
    break;
  default:
    HQFAIL("Bad input color space");
    break;
  }
}
#endif


/* ---------------------------------------------------------------------- */

/*
 * Custom CMM Link Data Access Functions
 * =====================================
 */

struct CLINKcustomcmm {
  CUSTOM_CMM_INFO   *customcmmInfo;

  sw_cmm_transform    customTransform;
};

struct CUSTOM_CMM_INFO {
  cc_counter_t   refCnt;
  size_t         structSize;

  sw_cmm_instance *cmmInstance;
  uint32          customcmmXformIndex;
  uint32          inputDimension;
  uint32          outputDimension;
  COLORSPACE_ID   inputSpaceId;
  COLORSPACE_ID   outputSpaceId;
  OBJECT          *outputCSAObj;
  sw_cmm_profile  customProfile;
};

static CLINKfunctions CLINKcustomcmm_functions =
{
  customcmm_destroy,
  customcmm_invokeSingle,
  NULL /* customcmm_invokeBlock */,
  customcmm_scan
};


CLINK *cc_customcmm_create(CUSTOM_CMM_INFO  *customcmmInfo,
                           COLORSPACE_ID    *oColorSpace,
                           int32            *colorspacedimension,
                           OBJECT           **outputPSColorSpace)
{
  CLINK *pLink;
  CLINKcustomcmm  *customcmm;
  uint32 inputChannels;
  uint32 outputChannels;
  sw_cmm_instance *cmmInstance ;
  const sw_cmm_api *cmmImplementation ;
  sw_cmm_result cmmresult ;

  HQASSERT(customcmmInfo != NULL, "customcmmInfo NULL");
  customcmmInfoAssertions(customcmmInfo);

  *oColorSpace = customcmmInfo->outputSpaceId;
  *colorspacedimension = customcmmInfo->outputDimension;
  *outputPSColorSpace = customcmmInfo->outputCSAObj;

  pLink = cc_common_create(customcmmInfo->inputDimension,
                           NULL,
                           SPACE_CMM,
                           customcmmInfo->outputSpaceId,
                           CL_TYPEcustomcmm,
                           customcmmStructSize(),
                           &CLINKcustomcmm_functions,
                           CLID_SIZEcustomcmm);
  if ( pLink == NULL )
    return NULL ;

  customcmmUpdatePtrs(pLink);

  customcmm = pLink->p.customcmm ;
  customcmm->customcmmInfo = customcmmInfo;
  cc_reservecustomcmminfo(customcmmInfo);

  customcmm->customTransform = NULL;

  cmmInstance = customcmmInfo->cmmInstance ;
  cmmImplementation = cmmInstance->implementation ;

  if ( (cmmresult = cmmImplementation->open_transform(cmmInstance,
                                                      customcmmInfo->customProfile,
                                                      1,
                                                      NULL,
                                                      NULL,
                                                      &inputChannels,
                                                      &outputChannels,
                                                      &customcmm->customTransform)) != SW_CMM_SUCCESS ||
       customcmm->customTransform == NULL) {
    (void)detail_error_handler(error_from_sw_cmm_result(cmmresult),
                               "Error opening alternate CMM transform");
    customcmm_destroy(pLink);
    return NULL ;
  }

  if (inputChannels != customcmmInfo->inputDimension ||
      outputChannels != customcmmInfo->outputDimension) {
    customcmm_destroy(pLink);
    (void) detail_error_handler(RANGECHECK,
                                "CMM_open_transform hasn't returned the correct number of channels");
    return NULL;
  }

/******** VALIDATE the next colorspace **************/

  /* Now populate the CLID slots:
   * We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   * For CL_TYPEcustomcmm (looking at invokeSingle) we have:
   * a) cmmInstance
   * b) customcmmXformIndex
   * =
   * 2 slots.
   */
  { CLID *idslot = pLink->idslot ;
    HQASSERT( pLink->idcount == CLID_SIZEcustomcmm, "Didn't create as requested" ) ;
    idslot[ 0 ] = (CLID) customcmmInfo->cmmInstance;
    idslot[ 1 ] = customcmmInfo->customcmmXformIndex;
  }

  customcmmAssertions(pLink);

  return pLink;
}

static void customcmm_destroy(CLINK *pLink)
{
  CLINKcustomcmm  *customcmm = pLink->p.customcmm;
  CUSTOM_CMM_INFO  *customcmmInfo = customcmm->customcmmInfo;

  sw_cmm_instance *cmmInstance = customcmmInfo->cmmInstance ;
  const sw_cmm_api *cmmImplementation = cmmInstance->implementation ;

  customcmmAssertions(pLink);

  cmmImplementation->close_transform(cmmInstance, customcmm->customTransform);

  cc_destroycustomcmminfo(&pLink->p.customcmm->customcmmInfo);
  cc_common_destroy(pLink);
}

static Bool customcmm_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
  CLINKcustomcmm  *customcmm = pLink->p.customcmm;
  CUSTOM_CMM_INFO  *customcmmInfo = customcmm->customcmmInfo;
  sw_cmm_instance *cmmInstance ;
  const sw_cmm_api *cmmImplementation ;
  sw_cmm_result cmmresult ;

  customcmmAssertions(pLink);

  cmmInstance = customcmmInfo->cmmInstance ;
  cmmImplementation = cmmInstance->implementation ;
  if ( (cmmresult = cmmImplementation->invoke_transform(cmmInstance,
                                                        customcmm->customTransform,
                                                        pLink->iColorValues,
                                                        oColorValues,
                                                        1)) != SW_CMM_SUCCESS ) {
    return detail_error_handler(error_from_sw_cmm_result(cmmresult),
                                "Error invoking alternate CMM transform");
  }

  return TRUE;
}


#ifdef INVOKEBLOCK_NYI
static int32 customcmm_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  customcmmAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */

/* customcmm_scan - Not currently necessary */
static mps_res_t customcmm_scan( mps_ss_t ss, CLINK *pLink )
{
  UNUSED_PARAM(mps_ss_t, ss);
  UNUSED_PARAM(CLINK *, pLink);

  return MPS_RES_OK;
}

static size_t customcmmStructSize(void)
{
  return sizeof(CLINKcustomcmm);
}

static void customcmmUpdatePtrs(CLINK *pLink)
{
  pLink->p.customcmm = (CLINKcustomcmm *)((uint8 *)pLink + cc_commonStructSize(pLink));
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void customcmmAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEcustomcmm,
                      customcmmStructSize(),
                      &CLINKcustomcmm_functions);

  if (pLink->p.customcmm != NULL)
    customcmmInfoAssertions(pLink->p.customcmm->customcmmInfo);

  switch (pLink->iColorSpace) {
  case SPACE_CMM:
    break;
  default:
    HQFAIL("Bad input color space");
    break;
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Custom CMM Info Data Access Functions
 * =====================================
 */

Bool cc_createcustomcmminfo(CUSTOM_CMM_INFO   **customcmmInfo,
                            COLORSPACE_ID     *outputColorSpaceId,
                            GS_COLORinfo      *colorInfo,
                            OBJECT            *colorSpaceObject,
                            sw_cmm_instance   *cmmInstance)
{
  CUSTOM_CMM_INFO  *pInfo;
  sw_cmm_custom_colorspace *customColorSpace;
  int32 inputDimension;
  int32 outputDimension;
  COLORSPACE_ID inputSpaceId;
  COLORSPACE_ID outputSpaceId;
  uint32 customcmmXformIndex;
  OBJECT *customArray;
  const sw_cmm_api *cmmImplementation ;
  sw_cmm_result cmmresult ;

  HQASSERT(customcmmInfo != NULL, "customcmmInfo NULL");
  HQASSERT(colorSpaceObject != NULL, "colorSpaceObject NULL");

  if (cmmInstance == NULL) {
    return detail_error_handler(CONFIGURATIONERROR, "CMM colorspace with no AlternateCMM in pagedevice");
  }

  cmmImplementation = cmmInstance->implementation ;
  HQASSERT(cmmImplementation != NULL, "cmmImplementation NULL");

  HQASSERT(oType(*colorSpaceObject) == OARRAY &&
           oType(oArray(*colorSpaceObject)[0]) == ONAME &&
           oNameNumber(oArray(*colorSpaceObject)[0]) == NAME_CMM &&
           theLen(*colorSpaceObject) == 4,
           "Expected a CMM colorspace");

  customArray = oArray(*colorSpaceObject);

  if (oType(customArray[1]) != OSTRING || theLen(customArray[1]) <= 0)
    return detail_error_handler(RANGECHECK, "The custom colorspace name should be a string");


  customcmmXformIndex = 0;
  customColorSpace = NULL;
  if (cmmImplementation->declare_custom_colorspace != NULL) {
    for (;;) {
      customColorSpace = cmmImplementation->declare_custom_colorspace(cmmInstance, customcmmXformIndex);
      if (customColorSpace == NULL)
        break;
      if (HqMemCmp(customColorSpace->name, strlen_int32((char *) customColorSpace->name),
                   oString(customArray[1]), theLen(customArray[1])) == 0)
        break;

      customcmmXformIndex++;
    };
  }

  if (customColorSpace == NULL)
    return detail_error_handler(CONFIGURATIONERROR, "Unknown custom colour space");

  if (!gsc_getcolorspacesizeandtype(colorInfo,
                                    &customArray[2],
                                    &inputSpaceId,
                                    &inputDimension))
    return FALSE;

  if (!gsc_getcolorspacesizeandtype(colorInfo,
                                    &customArray[3],
                                    &outputSpaceId,
                                    &outputDimension))
    return FALSE;

  if (customColorSpace->num_input_channels != CAST_SIGNED_TO_UINT32(inputDimension))
    return detail_error_handler(CONFIGURATIONERROR, "Custom colorspace has invalid input colorspace");
  if (customColorSpace->num_output_channels != CAST_SIGNED_TO_UINT32(outputDimension))
    return detail_error_handler(CONFIGURATIONERROR, "Custom colorspace has invalid output colorspace");

  pInfo = mm_alloc(mm_pool_color,
                   customcmmInfoStructSize(),
                   MM_ALLOC_CLASS_NCOLOR);
  if (pInfo == NULL)
    return error_handler(VMERROR);

  pInfo->refCnt = 1;
  pInfo->structSize = customcmmInfoStructSize();

  pInfo->cmmInstance = cmmInstance;
  pInfo->customcmmXformIndex = customcmmXformIndex;
  pInfo->inputDimension = inputDimension;
  pInfo->outputDimension = outputDimension;
  pInfo->inputSpaceId = inputSpaceId;
  pInfo->outputSpaceId = outputSpaceId;
  pInfo->outputCSAObj = &customArray[3];
  pInfo->customProfile = NULL ;

  HQASSERT(cmmImplementation->open_custom_colorspace,
           "No method to open custom colorspace; should have been caught at registration") ;
  if ( (cmmresult = cmmImplementation->open_custom_colorspace(cmmInstance, customcmmXformIndex, &pInfo->customProfile)) != SW_CMM_SUCCESS ||
       pInfo->customProfile == NULL ) {
    cc_destroycustomcmminfo(&pInfo);
    return detail_error_handler(error_from_sw_cmm_result(cmmresult),
                                "Alternate CMM failed to open custom colorspace");
  }

  customcmmInfoAssertions(pInfo);

  *customcmmInfo = pInfo;
  *outputColorSpaceId = pInfo->outputSpaceId;

  return TRUE;
}

static void freecustomcmminfo(CUSTOM_CMM_INFO *customcmmInfo)
{
  sw_cmm_instance *cmmInstance = customcmmInfo->cmmInstance ;
  const sw_cmm_api *cmmImplementation = cmmInstance->implementation ;

  cmmImplementation->close_profile(cmmInstance, customcmmInfo->customProfile);

  mm_free(mm_pool_color, customcmmInfo, customcmmInfo->structSize);
}

void cc_destroycustomcmminfo(CUSTOM_CMM_INFO **customcmmInfo)
{
  if ( *customcmmInfo != NULL ) {
    customcmmInfoAssertions(*customcmmInfo);
    CLINK_RELEASE(customcmmInfo, freecustomcmminfo);
  }
}

void cc_reservecustomcmminfo(CUSTOM_CMM_INFO *customcmmInfo)
{
  if (customcmmInfo != NULL) {
    customcmmInfoAssertions(customcmmInfo);
    CLINK_RESERVE(customcmmInfo);
  }
}

static size_t customcmmInfoStructSize(void)
{
  return sizeof(CUSTOM_CMM_INFO);
}

#if defined( ASSERT_BUILD )
static void customcmmInfoAssertions(CUSTOM_CMM_INFO  *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->structSize == customcmmInfoStructSize(),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif

int32 cc_customcmm_nOutputChannels(CUSTOM_CMM_INFO *customcmmInfo)
{
  customcmmInfoAssertions(customcmmInfo);

  return customcmmInfo->outputDimension;
}

/* Log stripped */
