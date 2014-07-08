/** \file
 * \ingroup halftone
 *
 * $HopeName: SWv20!src:gu_htm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for interfacing with screening modules.
 */

#include "core.h"
#include "coreinit.h"
#include "hqunicode.h"    /* utf8_validate */
#include "uvms.h"         /* UVS */
#include "debugging.h"
#include "dlstate.h"      /* DL_STATE */
#include "graphict.h"     /* GUCR_RASTERSTYLE */
#include "gschtone.h"     /* gsc_halftonePhaseX */
#include "gu_chan.h"      /* GUCR_INTERLEAVING_STYLE_BAND */
#include "gu_htm.h"       /* MODHTONE_REF */
#include "gs_color.h" /* gsc_getRS */
#include "hqmemcmp.h"     /* HqMemCmp */
#include "hqmemcpy.h"     /* HqMemCpy */
#include "hqmemset.h"
#include "mm.h"           /* mm_alloc */
#include "mmcompat.h"     /* mm_alloc_with_header */
#include "namedef_.h"     /* NAME_TransferFunction */
#include "objects.h"      /* ODICTIONARY */
#include "params.h"       /* SystemParams */
#include "ripmulti.h"     /* NUM_THREADS */
#include "swerrors.h"     /* UNREGISTERED */
#include "swhtm.h"        /* sw_htm_api */
#include "ht_module.h"    /* htm_init et al */
#include "mm_swmemapi.h"  /* mm_swmemory_create */
#include "swdataimpl.h"   /* sw_data_api_virtual */
#include "swdatafilter.h"
#include "surface.h"
#include "monitor.h"
#include "taskres.h"
#include "corejob.h"
#include "display.h" /* dl_reserve_band */
#include "bandtable.h"    /* mht_band_resources */

#include <stdlib.h>       /* bsearch */
#include "hqspin.h" /* Must be last include because of hqwindows.h */


#if defined( ASSERT_BUILD )

static Bool debug_htm_instance = FALSE ;

#define HTM_HALFTONE_ASSERTS( p ) htm_HalftoneRefAsserts( p )

/** Perform some sanity-check assertions on a modular instance.
 */
static void htm_HalftoneRefAsserts( MODHTONE_REF* mhtref ) ;

#else /* !ASSERT_BUILD */

#define HTM_HALFTONE_ASSERTS( p ) EMPTY_STATEMENT()

#endif /* ASSERT_BUILD */


/** The mm pool & class used for memory we allocate to manage them etc. */
#define HTM_ALLOCATION_POOL_OURS         mm_pool_temp


/** The list of selected modular halftone instances. */
static dll_list_t g_htmHalftones ;


/** The semaphore for accessing \ref g_htmHalftones. */
static hq_atomic_counter_t g_htmHalftones_access;


/* ------------------------------------------------------------------------ */
/** The internal htm module list.
 */
static htm_module_entry *g_htmModuleList ;

static sw_memory_instance *htm_memory_callbacks ;

/* ------------------------------------------------------------------------ */

/** Dispose of an individual HTM entry, removing it from the list. This does
    not call the finish() method, because this may be called during startup,
    before initialisation of the registered API. */
static void htm_entry_dispose(htm_module_entry **pentry)
{
  htm_module_entry *entry ;

  HQASSERT(pentry, "Nowhere to find HTM entry") ;

  entry = *pentry ;
  HQASSERT(entry, "No HTM entry") ;

  *pentry = entry->next ;

  mm_free(HTM_ALLOCATION_POOL_OURS, entry, sizeof(htm_module_entry)) ;
}

/** Initialise an individual HTM implementation. If the initialisation fails,
    the entry is cleaned up and disposed of. */
static Bool htm_entry_init(htm_module_entry **pentry)
{
  htm_module_entry *entry ;
  sw_htm_api *impl ;
  sw_htm_init_params params ;

  HQASSERT(pentry, "Nowhere to find HTM entry") ;

  entry = *pentry ;
  HQASSERT(entry, "No HTM entry") ;

  impl = entry->impl ;
  HQASSERT(impl, "No implementation in HTM entry") ;

  HQASSERT(htm_memory_callbacks, "HTM memory callbacks not initialised") ;
  params.mem = htm_memory_callbacks ;

  /* Initialise the implementation. */
  if ( impl->init == NULL || impl->init(impl, &params) )
    return TRUE ;

  /* Initialisation failed. Remove from the entry list. */
  htm_entry_dispose(pentry) ;
  return FAILURE(FALSE) ;
}

/** Destruct and finalise an individual HTM entry, removing it from the
    list. */
static void htm_entry_finish(htm_module_entry **pentry)
{
  htm_module_entry *entry ;
  sw_htm_api *impl ;

  HQASSERT(pentry, "Nowhere to find HTM entry") ;

  entry = *pentry ;
  HQASSERT(entry, "No HTM entry") ;

  impl = entry->impl ;
  HQASSERT(impl, "No implementation in HTM entry") ;

  /* Finalise the implementation. */
  if ( impl->finish != NULL )
    impl->finish(impl) ;

  htm_entry_dispose(pentry) ;
}

/* ------------------------------------------------------------------------ */

enum {
  HTM_NOT_INITIALISED,
  HTM_PREBOOT,
  HTM_STARTED
} ;

static int htm_api_state = HTM_NOT_INITIALISED ;

/** Pre-boot initialisation for screening modules.
 * This is called from SwInit(), before the interpreter is booted.
 */
static Bool htm_swinit(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  HQASSERT(htm_api_state == HTM_NOT_INITIALISED, "HTM API state incorrect") ;
  htm_api_state = HTM_PREBOOT ;

  return TRUE ;
}

/** Post-boot initialisation for modulate halftones. This is called after the
 * interpreter is booted. We jump through some hoops to ensure that if an
 * init or construct failure happens in the middle of the list, we cleanup
 * the previously-constructed modules (the htm_finish function will not be
 * called if we return FALSE from this routine).
 */
static Bool htm_postboot(void)
{
  htm_module_entry **pentry = &g_htmModuleList;

  HQASSERT(htm_api_state == HTM_PREBOOT, "HTM API state incorrect") ;

  if ( (htm_memory_callbacks = mm_swmemory_create(HTM_ALLOCATION_POOL_OURS,
                                                  MM_ALLOC_CLASS_HTM_SCRNMOD)) == NULL )
    return FALSE ;

  while ( *pentry != NULL ) {
    if ( !htm_entry_init(pentry) ) {
      /* Failed to construct an entry. Start by disposing of all not-yet
         constructed entries. */
      while ( *pentry )
        htm_entry_dispose(pentry) ;

      /* Now dispose of all previously-constructed entries. */
      while ( g_htmModuleList )
        htm_entry_finish(&g_htmModuleList) ;

      /* Destroy the memory callback instance. */
      mm_swmemory_destroy(&htm_memory_callbacks) ;

      return FALSE ;
    }

    pentry = &(*pentry)->next ;
  }

  htm_api_state = HTM_STARTED ;
  return TRUE ;
}

/** Finalisation for modulate halftones.
 * This is called just before shutdown.
 */
static void htm_finish(void)
{
  while ( g_htmModuleList )
    htm_entry_finish(&g_htmModuleList) ;
  mm_swmemory_destroy(&htm_memory_callbacks) ;
  htm_api_state = HTM_NOT_INITIALISED ;
}

/* Register a screening module.
 * Note that this function is declared in swhtm.h, not gu_htm.h, and is the
 * only function herein exported to the OEM world.
 */

sw_api_result RIPCALL SwRegisterHTM(sw_htm_api *impl)
{
  htm_module_entry **entry;
  htm_module_entry *newEntry;

  if ( htm_api_state == HTM_NOT_INITIALISED )
    return FAILURE(SW_API_TOO_EARLY) ;

  if ( impl == NULL ||
       impl->info.name == NULL || *impl->info.name == '\0' ||
       impl->info.display_name == NULL || *impl->info.display_name == '\0' ||
       impl->HalftoneSelect == NULL ||
       impl->HalftoneRelease == NULL ||
       impl->DoHalftone == NULL )
    return FAILURE(SW_API_INCOMPLETE) ;

  /* Check versions and sizes. Every supported version should have a case to
     check its instance size here. */
  switch (impl->info.version) {
  case SW_HTM_API_VERSION_20071110:
    if ( impl->info.instance_size < offsetof(sw_htm_instance, latency) )
      /* latency is the first field not in this version */
      return FAILURE(SW_API_INSTANCE_SIZE);
    break ;
  case SW_HTM_API_VERSION:
    if (impl->info.instance_size < sizeof(sw_htm_instance))
      return FAILURE(SW_API_INSTANCE_SIZE);
    break ;
  default:
    if (impl->info.version > SW_HTM_API_VERSION)
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

  if ( impl->band_ordering == SW_HTM_BAND_ORDER_DESCENDING )
    return FAILURE(SW_API_ERROR); /* NYI */

  /* Find the end of the list of htm's */
  for ( entry = &g_htmModuleList; (*entry) != NULL ; entry = &(*entry)->next ) {
    /* Validate the uniqueness of 'name' */
    if (strcmp((char *) (*entry)->impl->info.name,
               (char *) impl->info.name) == 0)
      return FAILURE(SW_API_NOT_UNIQUE);
  }

  /* Allocate new list entry */
  newEntry = mm_alloc(HTM_ALLOCATION_POOL_OURS, sizeof(htm_module_entry),
                      MM_ALLOC_CLASS_HTM_LISTMOD);
  if (newEntry == NULL)
    return FAILURE(SW_API_ERROR) ;

  /* Append new instance to end of list */
  newEntry->impl = impl ;
  newEntry->render_inited = FALSE ;
  newEntry->next = NULL ;

  *entry = newEntry ;

  /* If we've started up the RIP already, immediately initialise and
     construct the entry. */
  if ( htm_api_state == HTM_STARTED && !htm_entry_init(entry) )
    return FAILURE(SW_API_INIT_FAILED) ;

  return SW_API_REGISTERED;
}


/* ------------------------------------------------------------------------ */
/** Locate the registration entry for a screening module.
 * Exported only to allow existence check early in sethalftone.
 */
static htm_module_entry * findHalftoneModule( OBJECT *poName )
{
  htm_module_entry *entry ;
  uint8            *modname ;
  int32             modnamelen ;

  HQASSERT( NULL != poName, "NULL poName in findHalftoneModule" ) ;

  if ( oType( *poName ) == ONAME ) {
    modname = theICList ( oName( *poName ) ) ;
    modnamelen = theINLen ( oName ( *poName ) ) ;
  } else {
    HQASSERT( oType( *poName ) == OSTRING ,
              "poName not name or string in findHalftoneModule" ) ;
    modname = oString( *poName ) ;
    modnamelen = theLen(*poName) ;
  }

  for ( entry = g_htmModuleList ; NULL != entry ; entry = entry->next ) {
    if ( HqMemCmp( entry->impl->info.name,
                   strlen_int32((char*)entry->impl->info.name),
                   modname, modnamelen ) == 0 )
      return entry ;
  }
  return NULL ;
}

/* ------------------------------------------------------------------------ */

Bool htm_IsModuleNameRegistered( OBJECT *modname )
{
  return NULL != findHalftoneModule( modname ) ;
}

/* ------------------------------------------------------------------------ */

const htm_module_entry *htm_GetFirstModule()
{
  return g_htmModuleList ;
}

/* ------------------------------------------------------------------------ */

int32 htm_MapHtmResultToSwError( sw_htm_result htmResult )
{
  switch ( htmResult ) {
  case SW_HTM_SUCCESS:            return 0 ;
  case SW_HTM_ERROR_IOERROR:      return IOERROR ;
  case SW_HTM_ERROR_LIMITCHECK:   return LIMITCHECK ;
  case SW_HTM_ERROR_RANGECHECK:   return RANGECHECK ;
  case SW_HTM_ERROR_TYPECHECK:    return TYPECHECK ;
  case SW_HTM_ERROR_MEMORY:       return VMERROR ;
  case SW_HTM_ERROR_CONFIGURATIONERROR: return CONFIGURATIONERROR ;
  case SW_HTM_ERROR_BAD_HINSTANCE: return TYPECHECK;
  case SW_HTM_ERROR_UNSUPPORTED_SRC_BIT_DEPTH:
  case SW_HTM_ERROR_UNSUPPORTED_DST_BIT_DEPTH: return CONFIGURATIONERROR;
  case SW_HTM_ERROR_VERSION:      return CONFIGURATIONERROR;
  case SW_HTM_ERROR:              return UNREGISTERED;
  default:
    HQFAIL("Unrecognized HTM error code.");
    return UNREGISTERED;
  }
}


static void htm_Message(const char *message)
{
  if ( message != NULL )
    monitorf((uint8*)"%s", message);
  else
    HQFAIL("Null message from screening module");
}


/* ------------------------------------------------------------------------ */

#define HTM_SUPERCLASS_COOKIE (0xAD151337)

/** Allocate a MODHTONE_REF.
 * Returns NULL if the allocation fails (error_handler NOT called).
 */
static MODHTONE_REF *htm_AllocateHalftoneRef(sw_htm_api *impl)
{
  MODHTONE_REF *mhtref ;

  HQASSERT(impl, "No implementation for modular halftone instance") ;
  HQASSERT(htm_memory_callbacks,
           "No memory callbacks for modular halftone instance") ;

  mhtref = mm_alloc(HTM_ALLOCATION_POOL_OURS,
                    offsetof(MODHTONE_REF, instance) + impl->info.instance_size,
                    MM_ALLOC_CLASS_HTM_LISTHTINST) ;
  if (NULL != mhtref) {
    sw_htm_instance *instance = &mhtref->instance ;
    size_t i;

    HqMemZero(instance, impl->info.instance_size);
    instance->implementation = impl ;
    instance->mem = htm_memory_callbacks ;
    instance->num_src_channels = 1 ;
    instance->num_dst_channels = 1 ;
    if ( impl->info.version >= SW_HTM_API_VERSION_20100414 )
      instance->Message = htm_Message;

    mhtref->cookie = 0 ; /* invalid until fully constructed. */
    mhtref->mod_entry = NULL ;
    for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i )
      mhtref->usage[i] = INVALID_DL;
    mhtref->last_set_usage_index = 0;
    mhtref->reported = FALSE;
    DLL_RESET_LINK(mhtref, link) ;
    mhtref->refcount = 1 ;
    HQTRACE(debug_htm_instance, ("mhtref@ 0x%08X: Alloc;", mhtref ) ) ;
  }
  return mhtref ;
}


/* ------------------------------------------------------------------------ */

void htm_AddRefHalftoneRef( MODHTONE_REF *mhtref  )
{
  HQASSERT( NULL != mhtref, "NULL mhtref in htm_AddRefHalftoneRef" ) ;

  mhtref->refcount++ ;
  HQASSERT( mhtref->refcount > 0, "Unexpected negative or zero ref-count" ) ;
  HQTRACE(debug_htm_instance,
          ("mhtref@ 0x%08X: AddRef; Refs now = %d", mhtref, mhtref->refcount ) ) ;
}


/** Dispose of a MODHTONE_REF, without calling the release method. This is
 * used in error recovery, when the halftone wasn't selected properly.
 */
static void htm_DisposeHalftoneRef( MODHTONE_REF *mhtref )
{
  const sw_htm_api *implementation ;

  HQASSERT( NULL != mhtref, "NULL mhtref in htm_ReleaseHalftoneRef" ) ;

  /* Remove from the list */
  DLL_REMOVE(mhtref, link) ;

  implementation = mhtref->instance.implementation ;

  /* Destroy cookie to act as some defense against this being reused */
  mhtref->cookie = 0 ;

  /* Now we can bin it */
  mm_free(HTM_ALLOCATION_POOL_OURS, (mm_addr_t) mhtref,
          offsetof(MODHTONE_REF, instance) + implementation->info.instance_size);
}


void htm_ReleaseHalftoneRef( MODHTONE_REF *mhtref )
{
  sw_htm_instance *instance ;

  HQASSERT( NULL != mhtref, "NULL mhtref in htm_ReleaseHalftoneRef" ) ;

  HQASSERT(mhtref->refcount > 0, "Unexpected zero reference count" ) ;

  mhtref->refcount-- ;
  HQTRACE(debug_htm_instance,
          ("mhtref@ 0x%08X: Release; Refs now= %d", mhtref, mhtref->refcount )) ;
  if ( mhtref->refcount > 0 )
    return;

  /* Call the screening module to release the halftone */
  instance = &mhtref->instance ;
  HQASSERT( NULL != instance, "NULL halftone instance" ) ;
  HQASSERT( NULL != instance->implementation, "NULL halftone implementation" ) ;

  instance->implementation->HalftoneRelease(instance) ;

  HQTRACE(debug_htm_instance,
          ("mhtref@ 0x%08X: HalftoneRelease; Mod=%s, Inst=0x%08X",
           mhtref, mhtref->mod_entry->impl->info.name,
           mhtref->instance ));

  htm_DisposeHalftoneRef(mhtref) ;
}


Bool htm_is_used(const MODHTONE_REF *mhtref, dl_erase_nr erasenr)
{
  size_t i;

  for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i )
    if ( mhtref->usage[i] == erasenr )
      return TRUE;
  return FALSE;
}


dl_erase_nr mhtref_last_used(const MODHTONE_REF *mhtref)
{
  size_t i;
  dl_erase_nr largest = INVALID_DL;
  /* The last-used tracking relies on INVALID_DL < FIRST_DL. */

  /* This is really meaningful only in the interpreter, as that's the only
     thread that changes it. */
  HQASSERT(IS_INTERPRETER(), "Trying to get last used DL outside interpreter");
  for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i )
    if ( mhtref->usage[i] > largest )
      largest = mhtref->usage[i];
  return largest;
}


Bool htm_set_used(MODHTONE_REF *mhtref, dl_erase_nr erasenr)
{
  size_t i;

  /* This is thread-safe, because reset is never called at the same time. */
  if ( mhtref->usage[mhtref->last_set_usage_index] == erasenr )
    return TRUE;
  for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i ) /* search the array */
    if ( mhtref->usage[i] == erasenr ) {
      mhtref->last_set_usage_index = i;
      return TRUE;
    }
  /* Not in the array, allocate new slot. */
  /* This is thread-safe, because only the main interpreter thread does it. */
  HQASSERT(IS_INTERPRETER(), "Marking modular screen outside interpreter");
  for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i )
    if ( mhtref->usage[i] == INVALID_DL )
      break;
  HQASSERT(i < NUM_DISPLAY_LISTS, "Ran out of slots in usage array");
  mhtref->usage[i] = erasenr;
  mhtref->last_set_usage_index = i;
  return htm_ReserveResources(CoreContext.page, mhtref);
}


void htm_reset_used(MODHTONE_REF *mhtref, dl_erase_nr erasenr)
{
  size_t i;

  /* This is thread-safe, because no other thread will be using this DL. */
  for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i ) /* search the array */
    if ( mhtref->usage[i] == erasenr ) {
      mhtref->usage[i] = INVALID_DL;
      return;
    }
}


/* ------------------------------------------------------------------------ */

MODHTONE_REF *htm_first_halftone_ref(dl_erase_nr erasenr)
{
  MODHTONE_REF *mhtref;

  spinlock_counter(&g_htmHalftones_access, 10);
  mhtref = DLL_GET_HEAD(&g_htmHalftones, MODHTONE_REF, link);
  while ( mhtref != NULL && !htm_is_used(mhtref, erasenr) ) {
    mhtref = DLL_GET_NEXT(mhtref, MODHTONE_REF, link);
  }
  spinunlock_counter(&g_htmHalftones_access);
  if ( mhtref != NULL )
    HTM_HALFTONE_ASSERTS(mhtref);
  return mhtref;
}


MODHTONE_REF *htm_next_halftone_ref(dl_erase_nr erasenr, MODHTONE_REF *mhtref)
{
  spinlock_counter(&g_htmHalftones_access, 10);
  do {
    mhtref = DLL_GET_NEXT(mhtref, MODHTONE_REF, link);
  } while ( mhtref != NULL && !htm_is_used(mhtref, erasenr) );
  spinunlock_counter(&g_htmHalftones_access);
  /* It's safe to unlock and return the instance, because the iteration is not
     used at any point where the instance could be deselected. */
  return mhtref;
}


/* ------------------------------------------------------------------------ */

unsigned int htm_SrcBitDepth(const MODHTONE_REF *mhtref)
{
  HQASSERT(mhtref, "No modular halftone reference") ;

  return mhtref->instance.src_bit_depth ;
}


Bool htm_WantObjectMap(const MODHTONE_REF *mhtref)
{
  HQASSERT(mhtref, "No modular halftone reference") ;

  return mhtref->instance.want_object_map ;
}


Bool htm_ProcessEmptyBands(const MODHTONE_REF *mhtref)
{
  HQASSERT(mhtref, "No modular halftone reference") ;

  return mhtref->instance.process_empty_bands ;
}


unsigned int htm_Latency(const MODHTONE_REF *mhtref)
{
  HQASSERT(mhtref, "No modular halftone reference") ;

  return
    mhtref->instance.implementation->info.version >= SW_HTM_API_VERSION_20100414
    ? mhtref->instance.latency : 0;
}

/* ------------------------------------------------------------------------ */
/** Set up the raster information for HalftoneSelect.
 * We might eventually cache this, and only recreate it when it changes.
 * Returns FALSE on failure, and error_handler will have been called.
 */

static Bool prep_raster_info( DL_STATE *page,
                              const GUCR_RASTERSTYLE *rs,
                              sw_htm_raster_info **ppInfo,
                              unsigned int *maxChannelOut )
{
  sw_htm_raster_info       *pInfo ;
  GUCR_CHANNEL             *hf ;
  GUCR_COLORANT            *hc ;
  DEVICESPACEID             colorspace ;
  sw_htm_colorant_info     *pClrInfos ;
  size_t                    nMaxColorants;
  unsigned int              nColorant;
  unsigned int              nChannel;
  int32                     nSheet ;
  int32                     n;
  sw_htm_colorspace         cs;
  size_t                    i;

  /* Work out how many colorant info pointers we need. */
  nMaxColorants = 0 ;
  for (hf = gucr_framesStart(rs); /* returns handle */
       gucr_framesMore(hf); gucr_framesNext(&hf)) {
    for (hc = gucr_colorantsStart(hf);
         gucr_colorantsMore(hc, ! GUCR_INCLUDING_PIXEL_INTERLEAVED);
         /* loop only iterates once for frame-, mono- and pixel-interleaving */
         gucr_colorantsNext(& hc))
      nMaxColorants++;
  }

  /* Now allocate the memory we need */
  pInfo = (sw_htm_raster_info*)
    mm_alloc_with_header(HTM_ALLOCATION_POOL_OURS,
                         (nMaxColorants * sizeof(sw_htm_colorant_info))
                         + sizeof(sw_htm_raster_info),
                         MM_ALLOC_CLASS_HTM_DICTPARAMS) ;
  if ( NULL == pInfo)
    return error_handler( VMERROR );
  /* Set up the array of channels info (behind the raster info). */
  pClrInfos = (sw_htm_colorant_info*) (
                      ((uint8*)pInfo) + sizeof(sw_htm_raster_info) ) ;

  nColorant = 0 ;
  nChannel = 0 ; /* Principally to avoid a 'used when uninitialized' warning. */
  *maxChannelOut = 0;
  nSheet = 0 ; /* Principally to avoid a 'used when uninitialized' warning. */
  for (hf = gucr_framesStart(rs); /* returns handle */
       gucr_framesMore(hf); gucr_framesNext(&hf)) {
    if ( gucr_framesStartOfSheet(hf, NULL, & nSheet) )
      nChannel = 0 ;
    else
      ++nChannel;

    for (hc = gucr_colorantsStart(hf);
         gucr_colorantsMore(hc, ! GUCR_INCLUDING_PIXEL_INTERLEAVED);
         /* loop only iterates once for frame-, mono- and pixel-interleaving */
         gucr_colorantsNext(& hc)) {
      /* First, see if this is a duplicate colorant. */
      const GUCR_COLORANT_INFO *colorantInfo ;
      sw_htm_colorant_info *pClrInfo = NULL ;
      sw_htm_coloranttype ct;
      sw_htm_colorant_handling ch;
      unsigned int thisChannel = nChannel;

      /* Compute channel# for next colorant before possible skipping. */
      if ( gucr_colorantsBandChannelAdvance(hc) )
        ++nChannel;

      if (! gucr_colorantDescription( hc, &colorantInfo ) )
        continue ; /* It's not renderable, so skip it. */

      for ( i = 0 ; i < nColorant ; i++ ) {
        if (  ( HqMemCmp( pClrInfos[i].name,
                          strlen_int32((const char*)pClrInfos[i].name),
                          theICList(colorantInfo->name),
                          theINLen(colorantInfo->name)) == 0 ) &&
              ( thisChannel == pClrInfos[i].channel ) &&
              ( (unsigned)nSheet + 1 == pClrInfos[i].separation ) ) {
          pClrInfo = &pClrInfos[i] ;
          break ;
        }
        if ( thisChannel == pClrInfos[i].channel
             && (unsigned)nSheet + 1 == pClrInfos[i].separation )
          return detail_error_handler(
                   CONFIGURATIONERROR,
                   "Modular screening doesn't support shared channels");
      }
      if ( pClrInfo )
        continue ; /* We've already included this colorant, so skip it. */

      pClrInfo = &pClrInfos[ nColorant++ ];
      pClrInfo->separation = (unsigned)nSheet + 1;
      pClrInfo->channel = thisChannel ;
      if ( *maxChannelOut < thisChannel )
        *maxChannelOut = thisChannel;

      switch ( colorantInfo->colorantType ) {
      case COLORANTTYPE_PROCESS:
        ct = SW_HTM_COLORANTTYPE_PROCESS; break;
      case COLORANTTYPE_SPOT:
      case COLORANTTYPE_EXTRASPOT:
        ct = SW_HTM_COLORANTTYPE_SPOT; break;
      default:
      case COLORANTTYPE_UNKNOWN:
        ct = SW_HTM_COLORANTTYPE_UNKNOWN;
      }
      pClrInfo->colorant_type = ct;

      pClrInfo->name = theICList(colorantInfo->name);
      pClrInfo->name_len = theINLen(colorantInfo->name);
      if ( !colorantInfo->originalName ) {
        pClrInfo->original_name = theICList(colorantInfo->name);
        pClrInfo->original_name_len = theINLen(colorantInfo->name);
      } else {
        pClrInfo->original_name = theICList(colorantInfo->originalName);
        pClrInfo->original_name_len = theINLen(colorantInfo->originalName);
      }

      switch ( colorantInfo->specialHandling ) {
      case SPECIALHANDLING_OPAQUE:
        ch = SW_HTM_SPECIALHANDLING_OPAQUE; break;
      case SPECIALHANDLING_OPAQUEIGNORE:
        ch = SW_HTM_SPECIALHANDLING_OPAQUEIGNORE; break;
      case SPECIALHANDLING_TRANSPARENT:
        ch = SW_HTM_SPECIALHANDLING_TRANSPARENT; break;
      case SPECIALHANDLING_TRAPZONES:
        ch = SW_HTM_SPECIALHANDLING_TRAPZONES; break;
      case SPECIALHANDLING_TRAPHIGHLIGHTS:
        ch = SW_HTM_SPECIALHANDLING_TRAPHIGHLIGHTS; break;
      case SPECIALHANDLING_OBJECTMAP:
        return detail_error_handler(CONFIGURATIONERROR,
                                    "Object map channel not supported for modular halftones.");
      case SPECIALHANDLING_NONE:
      default:
        ch = SW_HTM_SPECIALHANDLING_NONE; break;
      }
      pClrInfo->special_handling = ch;
    }
  }

  /* Now we can start to initialize the main structure */

  /* Resolution(s) */
  pInfo->x_resolution = page->xdpi ; pInfo->y_resolution = page->ydpi ;

  /* Width, height and bandheight. */
  pInfo->width = page->page_w ; pInfo->height = page->page_h ;
  pInfo->band_height = page->band_lines ;

  /* Bit depth */
  pInfo->bit_depth = gucr_rasterDepth( rs );

  /* Colorspace and number of process colors */
  guc_deviceColorSpace( rs, &colorspace, &n) ;
  pInfo->num_process_colorants = (unsigned)n;
  switch ( colorspace ) {
    case DEVICESPACE_Gray: cs = SW_HTM_COLORSPACE_GRAY; break;
    case DEVICESPACE_RGB:  cs = SW_HTM_COLORSPACE_RGB; break;
    case DEVICESPACE_CMY:  cs = SW_HTM_COLORSPACE_CMY; break;
    case DEVICESPACE_RGBK: cs = SW_HTM_COLORSPACE_RGBK; break;
    case DEVICESPACE_CMYK: cs = SW_HTM_COLORSPACE_CMYK; break;
    case DEVICESPACE_Lab:  cs = SW_HTM_COLORSPACE_Lab; break;
    case DEVICESPACE_N:    cs = SW_HTM_COLORSPACE_N; break;
    default:               cs = SW_HTM_COLORSPACE_UNKNOWN; break;
  }
  pInfo->colorspace = cs;

  /* Total (valid) page colorants and the array of colorant infos. */
  pInfo->num_colorant_infos = nColorant;
  pInfo->colorant_infos = pClrInfos ;

  /* Raster interleaving. */
  switch ( gucr_interleavingStyle( rs  ) ) {
  case GUCR_INTERLEAVINGSTYLE_MONO:  /* Sort of a special case of 'band' */
  case GUCR_INTERLEAVINGSTYLE_BAND:
    pInfo->interleaving = SW_HTM_RASTER_INTERLEAVE_BAND; break;
  case GUCR_INTERLEAVINGSTYLE_FRAME:
    pInfo->interleaving = SW_HTM_RASTER_INTERLEAVE_FRAME; break;
  case GUCR_INTERLEAVINGSTYLE_PIXEL:
    return detail_error_handler(CONFIGURATIONERROR,
                                "Pixel interleaving not supported for modular halftones." ) ;
  default:
    return detail_error_handler( CONFIGURATIONERROR,
                                 "Unknown interleaving style" ) ;
  }

  switch ( gucr_separationStyle( rs ) ) {
  case GUCR_SEPARATIONSTYLE_COMPOSITE:
  case GUCR_SEPARATIONSTYLE_MONOCHROME:
    pInfo->separating = FALSE; break;
  default:
    pInfo->separating = TRUE ;
  }
  pInfo->num_separations = (unsigned)nSheet + 1;
  pInfo->job_number = (unsigned)page->job_number;
  pInfo->page_number = page->pageno;

  *ppInfo = pInfo ;
  return TRUE ;
}


/** Delete the raster information set up above.
 */
static void free_raster_info( sw_htm_raster_info *pInfo )
{
  mm_free_with_header(HTM_ALLOCATION_POOL_OURS, (mm_addr_t) pInfo);
}


/* ------------------------------------------------------------------------ */
#ifdef ASSERT_BUILD
/** See if a halftone reference is an instance already in the list.
 * Returns the pointer to the list entry if found, NULL otherwise.
 */
static MODHTONE_REF * htm_FindHalftoneInstance( sw_htm_instance *instance )
{
  MODHTONE_REF *p ;

  HQASSERT( NULL != instance, "NULL instance in htm_FindHalftoneInstance" ) ;

  spinlock_counter(&g_htmHalftones_access, 10);
  for ( p = DLL_GET_HEAD(&g_htmHalftones, MODHTONE_REF, link) ;
        NULL != p ;
        p = DLL_GET_NEXT(p, MODHTONE_REF, link) ) {
    if ( &p->instance == instance )
      break;
  }
  spinunlock_counter(&g_htmHalftones_access);
  return p;
}
#endif

/* ------------------------------------------------------------------------ */

/* Data API filtering for halftone dict. This is sufficient for the current
   purposes (filtering out a few keys and key types), but can be got around
   by creative use of the API. */

/** \brief bsearch comparator to find name matching datum in name list. */
static int CRT_API htdict_bsearch_cmp(const void *one, const void *two)
{
  const sw_datum *key = one ;
  NAMECACHE *name = &system_names[*(int32 *)two] ;

  return HqMemCmp((const uint8 *)key->value.string, (int32)key->length,
                  name->clist, name->len) ;
}

/** \brief Condition for refusing name key in HT dict. */
static Bool htdict_exclude_key(const sw_datum *key)
{
  static int32 ignore_names[] = {
    /* Keep these in lexicographical order; we use bsearch to
       find the name. */
    NAME_Background,        /* Separation control extensions */
    NAME_DCS,               /* Separation control extensions */
    NAME_DetectSeparation,  /* General control extensions */
    NAME_InheritAngle,      /* General control extensions */
    NAME_InheritFrequency,  /* General control extensions */
    NAME_Override,          /* General control extensions */
    NAME_OverrideAngle,     /* General control extensions */
    NAME_OverrideFrequency, /* General control extensions */
    NAME_Positions,         /* Separation control extensions */
    NAME_TransferFunction   /* Handled in the RIP. */
  };

  /* Ignore non-strings */
  if ( key->type != SW_DATUM_TYPE_STRING )
    return TRUE ;

  if ( bsearch(key, ignore_names,
               NUM_ARRAY_ITEMS(ignore_names), sizeof(ignore_names[0]),
               htdict_bsearch_cmp)
       != NULL )
    return TRUE ; /* skip */

  return FALSE ;
}

/** Filter for HT dict get_keyed routine. */
static sw_data_result RIPCALL htdict_get_keyed(const sw_datum *dict,
                                               const sw_datum *key,
                                               sw_datum *value)
{
  if ( htdict_exclude_key(key) )
    return SW_DATA_ERROR_UNDEFINED ;

  return next_get_keyed(dict, key, value) ;
}

/* Filter iterate_next routine. */
static sw_data_result RIPCALL htdict_iterate_next(const sw_datum *composite,
                                                  sw_data_iterator *iterator,
                                                  sw_datum *key,
                                                  sw_datum *value)
{
  sw_data_result result ;

  do {
    result = next_iterate_next(composite, iterator, key, value) ;
  } while ( result == SW_DATA_OK && htdict_exclude_key(key) ) ;

  return result ;
}

/* ------------------------------------------------------------------------ */

Bool htm_SelectHalftone(
    OBJECT        *modname ,
    NAMECACHE     *htcolor , /* e.g. color name in a type-5 */
    OBJECT        *htdict ,  /* Can be the subdict of a type-5 */
    GS_COLORinfo  *colorInfo,
    MODHTONE_REF **pmhtref )
{
  corecontext_t *context = get_core_context_interp();
  DL_STATE                *page = context->page;
  MODHTONE_REF            *mhtref;
  htm_module_entry        *mod_entry ;
  sw_htm_raster_info      *pRasterInfo = NULL /* reassure compiler */;
  sw_htm_select_info       selectInfo ;
  sw_htm_result            htmResult ;
  sw_data_result           datumResult ;
  sw_htm_instance         *cached_instance ;
  sw_data_api_filter       htdict_data_api ;
  unsigned int             maxChannel = 0 /* pacify compiler */;

  HQASSERT( htcolor , "NULL htcolor in htm_SelectHalftone" ) ;
  HQASSERT( oType( *htdict ) == ODICTIONARY,
            "htdict not dictionary in htm_SelectHalftone" ) ;
  HQASSERT( colorInfo , "NULL colorInfo in htm_SelectHalftone" ) ;

  /* Locate the screening module */
  mod_entry = findHalftoneModule( modname ) ;
  HQASSERT( mod_entry, "failed to find halftone module registration" ) ;

  /* See if a source raster bit depth has been established. */
  if ( (mhtref = htm_first_halftone_ref(page->eraseno)) != NULL )
    selectInfo.src_bit_depth = mhtref->instance.src_bit_depth ;
  else
    selectInfo.src_bit_depth = 0 ;  /* undecided */
  selectInfo.max_render_threads = NUM_THREADS() ;

  /* Prepare the raster info */
  if ( !prep_raster_info(page, gsc_getRS(colorInfo), &pRasterInfo, &maxChannel) )
    return FALSE ;  /* error_handler already called */
  selectInfo.dst_raster_info = pRasterInfo;
  if ( pRasterInfo->interleaving != SW_HTM_RASTER_INTERLEAVE_BAND )
    maxChannel = 0;

  /* Allocate the MODHTONE_REF */
  mhtref = htm_AllocateHalftoneRef(mod_entry->impl);
  if ( NULL == mhtref ) {
    free_raster_info( pRasterInfo );
    return error_handler( VMERROR );
  }

  mhtref->mod_entry = mod_entry ;

  /* Set up the colorant and HT dict datum objects. Make the htdict owner
     a filtering interface, which removes certain keys from iterations
     and keyed queries. */
  selectInfo.data_api = &sw_data_api_virtual ;

  selectInfo.colorantname.type = SW_DATUM_TYPE_STRING ;
  selectInfo.colorantname.owner = NULL ;
  selectInfo.colorantname.length = theINLen(htcolor) ;
  selectInfo.colorantname.value.string = (const char *)theICList(htcolor) ;

  if ( (datumResult = swdatum_from_object(&selectInfo.halftonedict, htdict)) != SW_DATA_OK ) {
    htm_DisposeHalftoneRef(mhtref) ;
    free_raster_info( pRasterInfo );
    return error_handler(error_from_sw_data_result(datumResult)) ;
  }

  /* Override the owner of the htdict_datum with a read-only data API that
     filters out access to certain keys. */
  selectInfo.halftonedict.owner =
    sw_data_api_filter_construct(&htdict_data_api,
                                 selectInfo.halftonedict.owner,
                                 "htfilter",
                                 NULL, /*get_indexed*/
                                 invalid_set_indexed,
                                 htdict_get_keyed,
                                 invalid_set_keyed,
                                 htdict_iterate_next,
                                 NULL /* open_blob */) ;

  /* Set up the sw_htm_select_info. */
  selectInfo.phasex = gsc_getHalftonePhaseX( colorInfo ) ;
  selectInfo.phasey = gsc_getHalftonePhaseY( colorInfo ) ;
  if ( context->systemparams->ScreenRotate )  /* Rotate screen according to page.  */
    selectInfo.screen_rotation = gsc_getScreenRotate( colorInfo ) ;
  else
    selectInfo.screen_rotation = 0.0 ;

  /* And finally, call the screening module's HalftoneSelect function. */
  cached_instance = NULL ;
  htmResult = mhtref->instance.implementation->HalftoneSelect(&mhtref->instance,
                                                              &selectInfo,
                                                              &cached_instance) ;

  free_raster_info( pRasterInfo ) ;

  /* Did it work? */
  if ( htmResult != SW_HTM_SUCCESS ) {
    HQTRACE(debug_htm_instance,
            ("HalftoneSelect failed; Module=%s, Color='%.*s', result=%u",
             mhtref->instance.implementation->info.name,
             theINLen( htcolor ), theICList( htcolor ),
             htmResult )) ;
    htm_DisposeHalftoneRef( mhtref );
    return error_handler(htm_MapHtmResultToSwError(htmResult));
  }

  if ( cached_instance == &mhtref->instance ) {
    /* Can't refer back to the instance we're currently constructing. */
    htm_DisposeHalftoneRef( mhtref );
    return error_handler(CONFIGURATIONERROR);
  }

  if ( cached_instance != NULL ) {
    /* Upcast to the instance's superclass (the containing MODHTONE_REF). */
    MODHTONE_REF *existmhtref = (MODHTONE_REF *)((char *)cached_instance - offsetof(MODHTONE_REF, instance)) ;

    /* The module asserts that we have already got a suitable selected
       instance. Check whether we already have this instance in the list. */
    HQASSERT(htm_FindHalftoneInstance(cached_instance) == existmhtref,
             "Upcast and halftone instance search don't agree") ;

    /* Paranoid checks to see that the instance passed to us is valid. */
    if ( existmhtref->cookie == HTM_SUPERCLASS_COOKIE &&
         existmhtref->refcount > 0 &&
         existmhtref->mod_entry != NULL ) {
      /* Use the existing one to make the instance unique to that pointer.
       * This becomes an implicit additional reference to the existing one,
       * because our caller should match every htm_SelectHalftone() with a
       * corresponding htm_ReleaseHalftoneRef().
       */
      htm_AddRefHalftoneRef( existmhtref ) ;
      mhtref = existmhtref ;
    } else {
      /* They lied. We don't know what they passed us, but it wasn't a
         selected halftone instance. */
      htm_DisposeHalftoneRef( mhtref );
      return error_handler(CONFIGURATIONERROR);
    }
  } else {
    /* New instance, add it to the list. We add interrelated-channel ones to
       the head, the rest to the tail. */
    spinlock_counter(&g_htmHalftones_access, 10);
    if ( mhtref->instance.interrelated_channels )
      DLL_ADD_HEAD(&g_htmHalftones, mhtref, link) ;
    else
      DLL_ADD_TAIL(&g_htmHalftones, mhtref, link) ;
    spinunlock_counter(&g_htmHalftones_access);
  }

  /* Success - assert some basic details. */
  HTM_HALFTONE_ASSERTS( mhtref ) ;

  /* We don't yet support interrelated channels. */
  if ( ( mhtref->instance.interrelated_channels ) ||
       ( mhtref->instance.num_src_channels > 1 ) ||
       ( mhtref->instance.num_dst_channels > 1 ) ) {
    htm_ReleaseHalftoneRef( mhtref );
    return detail_error_handler( CONFIGURATIONERROR,
                       "Interrelated channels halftones not yet supported.") ;
  }

  /* Check that there is a modular halftone surface support. */
  if ( page->surfaces == NULL ||
       surface_find(page->surfaces, SURFACE_MHT_MASK) == NULL ) {
    htm_ReleaseHalftoneRef( mhtref );
    return detail_error_handler( CONFIGURATIONERROR,
                       "Modular halftoning not supported by surface.") ;
  }

  /* And the modular halftone surface support is of the right depth. */
  if ( (mhtref->instance.src_bit_depth != 8 ||
        surface_find(page->surfaces, SURFACE_MHT_CONTONE_FF) == NULL) &&
       (mhtref->instance.src_bit_depth != 16 ||
        surface_find(page->surfaces, SURFACE_MHT_CONTONE_FF00) == NULL) ) {
    htm_ReleaseHalftoneRef( mhtref );
    return detail_error_handler( CONFIGURATIONERROR,
                       "Modular halftoning depth not supported by surface.") ;
  }

  /** \todo Latency implies process_empty_bands, for now. Removing this
      means this module has to be told when a band is skipped. */
  if ( mhtref->instance.implementation->info.version >= SW_HTM_API_VERSION_20100414
       && mhtref->instance.latency > 0 ) {
    mhtref->instance.process_empty_bands = TRUE;
  }

  /* This reference is valid. */
  mhtref->cookie = HTM_SUPERCLASS_COOKIE ;

  /* Now we can set the caller's pointer to the MODHTONE_REF. */
  *pmhtref = mhtref ;

  HQTRACE(debug_htm_instance,
          ("mhtref@ 0x%08X: HalftoneSelect; Module=%s, Color='%.*s', "
           "Inst=%d, Channels=%d->%d",
           mhtref, mhtref->mod_entry->impl->info.name,
           theINLen( htcolor ), theICList( htcolor ),
           (intptr_t)&mhtref->instance,
           mhtref->instance.num_src_channels, mhtref->instance.num_dst_channels ));
  return TRUE ;
}


Bool htm_ReserveResources(DL_STATE *page, const MODHTONE_REF *mhtref)
{
  size_t band_lc ;
  unsigned int nbands ;
  static const char *msg = "Two modular screens with different contone depths used on the same page.";

  /* The source bit depth must be the same for all modules, so we can set the
     band contone bytes now. This must come before mht_band_resources(). */
  band_lc = FORM_LINE_BYTES(page->page_w * htm_SrcBitDepth(mhtref));
  if ( page->band_lc != 0 && page->band_lc != band_lc ) {
    /* Many callers will drop the error, so assert to help debugging. */
    HQFAIL(msg);
    detail_error_handler(CONFIGURATIONERROR, msg);
  }
  page->band_lc = band_lc ;

  /* Ensure resource pool for MHT bands is allocated. */
  if ( !mht_band_resources(page) )
    return FALSE ;

  /* Set minimum number of bands to provision simultaneously for latency.
     Whatever the module wants, we can't provision more bands than there are
     in the display list. */
  nbands = htm_Latency(mhtref) + 1 ;
  if ( nbands > (unsigned int)page->sizedisplaylist )
    nbands = (unsigned int)page->sizedisplaylist;
  if ( nbands > 1 ) {
    requirement_node_t *expr = requirement_node_find(page->render_resources,
                                                     REQUIREMENTS_BAND_GROUP) ;
    HQASSERT(expr != NULL, "No band group requirements") ;
    if ( !requirement_node_simmin(expr, nbands) )
      return FALSE ;
  }

  if ( !dl_reserve_band(page, RESERVED_BAND_MODULAR_SCREEN) )
    return FALSE ;

  if ( htm_WantObjectMap(mhtref) &&
       !dl_reserve_band(page, RESERVED_BAND_MODULAR_MAP) )
    return FALSE ;

  return TRUE ;
}

/* ------------------------------------------------------------------------ */

sw_htm_result htm_RenderInitiation( sw_htm_render_info *render_info,
                                    Bool retry, dl_erase_nr erasenr )
{
  htm_module_entry *entry ;
  MODHTONE_REF     *mhtref ;
  sw_htm_result result = SW_HTM_SUCCESS ;

  HQASSERT( render_info, "NULL render_info in htm_RenderInitiation" ) ;

  if ( !retry ) {
    /* Clear down all the render_inited flags. */
    for ( entry = g_htmModuleList ; NULL != entry ; entry = entry->next )
      entry->render_inited = FALSE ;
  }

  /* Note that we walk the list of halftones, rather than maintain a count
   * of selected halftones for each module.
   */
  for ( mhtref = htm_first_halftone_ref(erasenr) ;
        NULL != mhtref ;
        mhtref = htm_next_halftone_ref(erasenr, mhtref) ) {
    HTM_HALFTONE_ASSERTS( mhtref ) ;

    entry = mhtref->mod_entry ;
    if ( entry->render_inited )
      continue ;
    if ( entry->impl->RenderInitiation != NULL ) {
      if ( (result = entry->impl->RenderInitiation(entry->impl, render_info))
           == SW_HTM_SUCCESS )
        entry->render_inited = TRUE;
      else
        break;
    }
  }
  return result ;
}


/* ------------------------------------------------------------------------ */

void htm_RenderCompletion( sw_htm_render_info *render_info,
                           dl_erase_nr erasenr, Bool aborted )
{
  htm_module_entry *entry ;

  UNUSED_PARAM(dl_erase_nr, erasenr);
  for ( entry = g_htmModuleList ; NULL != entry ; entry = entry->next )
    if (entry->render_inited) {
      if ( entry->impl->RenderCompletion != NULL )
        entry->impl->RenderCompletion(entry->impl, render_info, aborted) ;
      entry->render_inited = FALSE ;
    }
}


/* ------------------------------------------------------------------------ */

Bool htm_DoHalftone( MODHTONE_REF *mhtref,
                     sw_htm_dohalftone_request *request )
{
  HQASSERT( NULL != mhtref->mod_entry, "NULL halftone module pointer" ) ;
  HQASSERT( NULL != mhtref->mod_entry->impl, "NULL halftone API pointer" ) ;

  HQASSERT( request->DoneHalftone,
            "NULL DoneHalftone pointer in htm_DoHalftone" ) ;
  HQASSERT( request->first_line_y >= 0,
            "Negative line Y offset in htm_DoHalftone" ) ;
  HQASSERT( request->num_lines > 0,
            "Negative or zero height number of lines in htm_DoHalftone" ) ;

  HQASSERT( request->render_info,
            "Null render info in htm_DoHalftone request" ) ;
  HQASSERT( (request->render_info->width > 0) &&
            (request->render_info->height > 0),
            "Zero raster width or height in render info" ) ;
  HQASSERT( request->render_info->src_bit_depth
            == mhtref->instance.src_bit_depth,
            "Render info src_bit_depth differs from halftone instance" ) ;
  HQASSERT( (request->render_info->dst_bit_depth == 1) ||
            (request->render_info->dst_bit_depth == 2) ||
            (request->render_info->dst_bit_depth == 4) ||
            (request->render_info->dst_bit_depth == 8) ||
            (request->render_info->dst_bit_depth == 16),
            "Render info dst_bit_depth unexpected" ) ;
  HQASSERT( request->render_info->msk_linebytes > 0,
            "Negative or zero msk_linebytes in render info" ) ;
  HQASSERT( request->render_info->src_linebytes > 0,
            "Negative or zero src_linebytes in render info" ) ;
  HQASSERT( request->render_info->dst_linebytes > 0,
            "Negative or zero dst_linebytes in render info" ) ;
  HQASSERT( request->render_info->max_render_threads > 0,
            "Negative or zero max_render_threads in render info" ) ;

  HQASSERT( request->msk_bitmap,
            "NULL mask bitmap pointer in htm_DoHalftone" ) ;

  HQASSERT( request->object_props_map || !mhtref->instance.want_object_map,
            "NULL object properties map pointer in htm_DoHalftone" ) ;

  HQASSERT( request->num_src_channels > 0,
            "Negative or zero source channel count in htm_DoHalftone" ) ;
  HQASSERT( request->src_channels,
            "NULL source channels pointer in htm_DoHalftone" ) ;
  HQASSERT( request->num_dst_channels > 0,
            "Negative or zero destination channel count in htm_DoHalftone" ) ;
  HQASSERT( request->dst_channels,
            "NULL destination channels pointer in htm_DoHalftone" ) ;

  return mhtref->instance.implementation->DoHalftone(&mhtref->instance, request) ;
}


void htm_AbortHalftone( MODHTONE_REF *mhtref,
                        sw_htm_dohalftone_request *request )
{
  HQASSERT( mhtref != NULL, "NULL halftone entry pointer" );
  HQASSERT( mhtref->instance.implementation != NULL, "NULL halftone API pointer" );
  HQASSERT( request != NULL, "NULL request pointer" );

  if ( mhtref->instance.implementation->info.version >= SW_HTM_API_VERSION_20100414 )
    mhtref->instance.implementation->AbortHalftone(&mhtref->instance, request);
}


#ifdef ASSERT_BUILD


static void htm_HalftoneRefAsserts( MODHTONE_REF* mhtref )
{
  MODHTONE_REF* p ;

  HQASSERT( (mhtref->instance.src_bit_depth == 8) ||
            (mhtref->instance.src_bit_depth == 16),
            "Unsupported value of src_bit_depth in MODHTONE_REF" ) ;
  HQASSERT( mhtref->instance.num_src_channels > 0,
            "Number of source channels <=0 in MODHTONE_REF" ) ;
  HQASSERT( mhtref->instance.num_dst_channels > 0,
            "Number of destination channels <=0 in MODHTONE_REF" ) ;
  HQASSERT( (mhtref->instance.interrelated_channels) ||
            ( (mhtref->instance.num_src_channels == 1) &&
              (mhtref->instance.num_dst_channels == 1) ),
            "Interrelated_channels is false but channel counts aren't one") ;
  HQASSERT( mhtref->mod_entry, "NULL module pointer in MODHTONE_REF" ) ;
  /* Check chain pointers and can we find it in the list */
  spinlock_counter(&g_htmHalftones_access, 10);
  for ( p = DLL_GET_HEAD(&g_htmHalftones, MODHTONE_REF, link) ;
        NULL != p ;
        p = DLL_GET_NEXT(p, MODHTONE_REF, link) ) {
    if ( p == mhtref )
      break ;
  }
  /* If it's in a list, we should have found it */
  HQASSERT(!DLL_IN_LIST(mhtref, link) || p == mhtref,
           "Chained MODHTONE_REF not found in list" ) ;
  spinunlock_counter(&g_htmHalftones_access);

  /* Prove that the module pointer is in the module list */
  if (mhtref->mod_entry) {
    const htm_module_entry *pTargetMod = mhtref->mod_entry ;
    htm_module_entry *pMod = g_htmModuleList ;

    while (pMod) {
      if ( pTargetMod == pMod )
        break;  /* got a match */
      pMod= pMod->next ;
    }
    HQASSERT( NULL != pMod,
              "MODHTONE_REF refers to module not in the module list" ) ;
  }
}


#endif  /* ASSERT_BUILD */


static void init_C_globals_gu_htm(void)
{
#if defined( ASSERT_BUILD )
  debug_htm_instance = FALSE ;
#endif
  DLL_RESET_LIST(&g_htmHalftones) ;
  g_htmModuleList = NULL ;
  htm_memory_callbacks = NULL ;

  htm_api_state = HTM_NOT_INITIALISED ;
}

void htm_C_globals(core_init_fns *fns)
{
  init_C_globals_gu_htm() ;

  fns->swinit = htm_swinit ;
  fns->postboot = htm_postboot ;
  fns->finish = htm_finish ;
}

/* Log stripped */
