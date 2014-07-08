/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:rsdstore.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * ReusableStreamDecode block storage.
 *
 * The callers are responsible for serializing access to each RSD
 * filter; this code will then keep the filters safe from low-memory
 * actions.
 */

#include "core.h"
#include "rsdstore.h"
#include "rsdblist.h"

#include "swerrors.h"
#include "swdevice.h"
#include "swcopyf.h"
#include "mm.h"
#include "mmcompat.h"
#include "lowmem.h"
#include "mps.h"
#include "gcscan.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "objstack.h"
#include "mlock.h"
#include "swtrace.h"
#include "fileio.h"
#include "fileparam.h"          /* FileIOParams */

#include "metrics.h"


/* -------------------------------------------------------------------------- */
struct RSD_STORE {
  int16 saverestorefilepos ;      /* Set if necs to save/restore source. */
  int16 freecblocks ;             /* Set if rand acces or small diff between
                                    comp/uncomp data size. */
  RSD_BLOCKLIST *dblocks ;       /* Blocklist for decoded/raw data. */

  RSD_BLOCKLIST *cblocks ;       /* Blocklist for compressed data. */

  RSD_STORE *prev ;
  RSD_STORE *next ;
} ;

/* -------------------------------------------------------------------------- */
/* Each new RSD Store is pushed on to this linked list of stores.
 *
 * This is a GC root, because it's easier than marking via the RSD filters.
 */
static RSD_STORE *rsd_stores ;
static mps_root_t rsd_storesroot ;

/** Mutex to protect access to the rsd_stores list, and also the *block
   fields in the blocklists and the block file, although these may
   someday get their own mutex in the store. */
static multi_mutex_t rsd_mutex;


/** Flag to indicate when any of the RSD handlers is releasing. */
static Bool rsd_handler_releasing;


/** Flag to stop RSD handler from offering after a disk purge failure.

  Needed to stop an infinite loop of futile offers. Could be reset as soon as
  the allocator breaks out of the loop, but we'll keep it set until there's some
  reason to believe a disk purge might succeed.
 */
static Bool rsd_offers_limited;


/** Memory available at the latest purge failure.

  Since a purge failure is usually due to lack of memory for the file buffer,
  track this to determine when to try again.
 */
static size_t rsd_limit_memory_size;


/* -------------------------------------------------------------------------- */
static void rsd_analysefilterchain( FILELIST *source ,
                                    Bool seekable ,
                                    FILELIST **csource ,
                                    FILELIST **rcompfilter ,
                                    Bool *encoded ) ;
#ifdef RSD_STORE_STATS
static void rsd_storestats( RSD_STORE *rsds ) ;
#endif
static mps_res_t rsd_storescan( mps_ss_t scan_state, RSD_STORE *rsds );

/* -------------------------------------------------------------------------- */
#ifdef METRICS_BUILD
struct rsd_metrics rsd_metrics ;

static Bool rsd_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("RSD")) )
    return FALSE ;

  SW_METRIC_INTEGER("PeakPoolSize",
                    rsd_metrics.rsd_pool_max_size) ;
  SW_METRIC_INTEGER("PeakPoolObjects",
                    rsd_metrics.rsd_pool_max_objects) ;
  SW_METRIC_INTEGER("PeakPoolFragmentation",
                    rsd_metrics.rsd_pool_max_frag);

  sw_metrics_close_group(&metrics) ;
  sw_metrics_close_group(&metrics) ;

  return TRUE ;
}

static void rsd_metrics_reset(int reason)
{
  struct rsd_metrics init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  rsd_metrics = init ;
}

static sw_metrics_callbacks rsd_metrics_hook = {
  rsd_metrics_update,
  rsd_metrics_reset,
  NULL
} ;
#endif

/* -------------------------------------------------------------------------- */
/* rsd_storeslistscan - scanning function for rsd_stores */
static mps_res_t MPS_CALL rsd_storeslistscan(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res = MPS_RES_OK;
  RSD_STORE *rsds;

  UNUSED_PARAM( void*, p ); UNUSED_PARAM( size_t, s );
  rsds = rsd_stores;
  while ( rsds != NULL && res == MPS_RES_OK ) {
    res = rsd_storescan( ss, rsds );
    rsds = rsds->next;
  }
  return res;
}


Bool rsd_storepostboot(void)
{
  rsd_device_iterator_t rsd_iter ;
  DEVICELIST *device ;

#define RSD_PATTERN (uint8 *)"RSD/*"
  /* Ensures temporary files created by RSD Store are removed; normally
     the files are removed when the RSD filter is closed or restored away. */
  for ( device = rsd_device_first(&rsd_iter) ;
        device ;
        device = rsd_device_next(&rsd_iter) ) {
    Bool disable = FALSE ;
    struct old_file_list {
      struct old_file_list *next ;
      uint8 filename[1] ; /* Extendable allocation */
    } *files = NULL ;
    void *handle ;

    if ( !isDeviceEnabled(device) ) {
      SetEnableDevice(device) ;
      disable = TRUE ;
    }

    /* Ignore errors from devicelist functions and file delete */
    if ( (handle = theIStartList(device)(device, RSD_PATTERN)) != NULL ) {
      FILEENTRY file ;
      while ( theINextList(device)(device, &handle, RSD_PATTERN, &file) == FileNameMatch ) {
        /* Stash away name; the DEVICELIST semantics do not specify whether
           delete operations can be performed in the middle of an
           enumeration, so assume not. If the memory cannot be allocated to
           store the name, quietly ignore the error, leaving the file on the
           device until next time. */
        struct old_file_list *match = mm_alloc_with_header(mm_pool_temp,
                                                           sizeof(struct old_file_list) + file.namelength,
                                                           MM_ALLOC_CLASS_RSDSTORE) ;
        if ( match != NULL ) { /* Ignore MM failure */
          HqMemCpy(&match->filename[0], file.name, file.namelength) ;
          match->filename[file.namelength] = '\0' ;
          match->next = files ;
          files = match ;
        }
      }
      (void)theIEndList(device)(device, handle) ;
    }

    /* Delete any stored names */
    while ( files != NULL ) {
      struct old_file_list *next = files->next ;
      /* Note that the delete may fail; who knows what processes (e.g. virus
         checkers) may have the file open. If so we just ignore the failure and
         hope to catch it next time we boot. */
      (void)theIDeleteFile(device)(device, files->filename) ;
      mm_free_with_header(mm_pool_temp, files) ;
      files = next ;
    }

    if ( disable ) {
      ClearEnableDevice(device) ;
    }
  }

  return TRUE ;
}


/* -------------------------------------------------------------------------- */
RSD_STORE *rsd_storeopen(FILELIST *dsource, Bool seekable, int accesshint)
{
  RSD_STORE *rsds ;
  FILELIST *csource , *flptr , *compfilter ;
  Bool encoded ;

  HQASSERT( dsource , "rsd_storeopen: dsource NULL" ) ;
  HQASSERT( BOOL_IS_VALID(seekable) ,
            "rsd_storeopen: seekable must be boolean" ) ;
  HQASSERT( accesshint == RSD_ACCESS_SEQN || accesshint == RSD_ACCESS_RAND ,
            "rsd_storeopen: acceshint not RSD_ACCESS_SEQN/RAND" ) ;

  rsds = ( RSD_STORE * ) mm_alloc( mm_pool_temp ,
                                   sizeof( RSD_STORE ) ,
                                   MM_ALLOC_CLASS_RSDSTORE ) ;
  if ( rsds == NULL ) {
    (void)error_handler( VMERROR );
    return NULL ;
  }

  rsds->saverestorefilepos     = CAST_SIGNED_TO_INT16( seekable ) ;
  rsds->freecblocks            = -1 ; /* Not Set */
  rsds->dblocks                = NULL ;
  rsds->cblocks                = NULL ;
  rsds->next                   = NULL ;
  rsds->prev                   = NULL ;

  if ( ! rsd_datapools_create()) {
    rsd_storeclose( rsds ) ;
    return NULL ;
  }

  /* Analyse filter chain. */

  rsd_analysefilterchain( dsource , seekable ,
                          & csource , & compfilter , & encoded ) ;

  /* Set-up blocklists. */

  if ( csource ) {
    FILELIST *rsdsfilter ;
    Bool cencoded ;
    OBJECT rsdargs = OBJECT_NOTVM_NULL ;

    HQASSERT( encoded , "Expected encoded to be TRUE" ) ;
    HQASSERT( seekable , "Expected seekable to be TRUE" ) ;

    rsdsfilter = filter_standard_find(NAME_AND_LENGTH("%rsdstore")) ;
    HQASSERT(rsdsfilter, "No standard %rsdstore filter") ;
    if ( ! filter_create(rsdsfilter, &rsdsfilter, &rsdargs, NULL) ) {
      rsd_storeclose( rsds ) ;
      return NULL ;
    }

    theIFilterPrivate( rsdsfilter ) = rsds ;

    /* Swap in the seekable rsd store. */
    theIUnderFile( compfilter ) = rsdsfilter ;
    theIUnderFilterId( compfilter ) = theIFilterId( rsdsfilter ) ;

    /* Decoded/Raw Data. */
    rsds->dblocks = rsd_blistnew( dsource , FALSE , TRUE , TRUE , accesshint ) ;
    if ( rsds->dblocks == NULL ) {
      rsd_storeclose( rsds ) ;
      return NULL ;
    }

    /* Compressed Data. */
    rsd_analysefilterchain( csource , seekable ,
                            & flptr , & compfilter , & cencoded ) ;
    rsds->cblocks = rsd_blistnew( csource , TRUE , seekable , cencoded , RSD_ACCESS_SEQN ) ;
    if ( rsds->cblocks == NULL ) {
      rsd_storeclose( rsds ) ;
      return NULL ;
    }
  }
  else {
    /* Decoded/Raw Data. */
    rsds->dblocks = rsd_blistnew( dsource , TRUE , seekable , encoded , accesshint ) ;
    if ( rsds->dblocks == NULL ) {
      rsd_storeclose( rsds ) ;
      return NULL ;
    }
  }

  /* Read all of the data into the RSD Store. */

  {
    /* No lazy reading for now. */
    uint8 *buf ;
    int32 bytes ;
    rsds->saverestorefilepos = FALSE ;
    do {
      if ( ! rsd_storeread( rsds , & buf , & bytes )) {
        rsd_storeclose( rsds ) ;
        return NULL ;
      }
      /* Clear the lock on the block of mem since nothing is actually
         using the data at the moment (this allows an extra block of
         memory to be recycled if necessary). */
      rsd_blistclearlock( rsds->dblocks ) ;
    } while ( bytes > 0 ) ;
    rsd_blistreset( rsds->dblocks ) ;
    rsds->saverestorefilepos = CAST_SIGNED_TO_INT16( seekable ) ;

    if ( seekable && ! encoded && theIUnderFile( dsource )) {
      /* Don't need to use the SubFileDecode or Stream Filter again,
         just the file itself. */
      HQASSERT( isIDelimitsData( dsource ) ,
                "Expected a filter that delimits the data only" ) ;
      HQASSERT( theIUnderFile( theIUnderFile( dsource )) == NULL ,
                "Must only be one filter on top of the file" ) ;
      rsd_blistsetsource( rsds->dblocks , theIUnderFile( dsource )) ;
    }
  }
  multi_mutex_lock(&rsd_mutex);
  rsds->next                   = rsd_stores ;
  if ( rsd_stores )
    rsd_stores->prev           = rsds ;
  rsd_stores                   = rsds ;
  multi_mutex_unlock(&rsd_mutex);
  return rsds ;
}

/* -------------------------------------------------------------------------- */
void rsd_storeclose( RSD_STORE *rsds )
{
  size_t i;

  HQASSERT( rsds , "rsd_storeclose: rsds NULL" ) ;

  multi_mutex_lock(&rsd_mutex);
  /* Unlink the store from the chain. */
  if ( rsds->prev )
    rsds->prev->next = rsds->next ;
  if ( rsds->next )
    rsds->next->prev = rsds->prev ;
  if ( rsds == rsd_stores )
    rsd_stores = rsds->next ;
  multi_mutex_unlock(&rsd_mutex);

#ifdef RSD_STORE_STATS
  rsd_storestats( rsds ) ;
#endif

  for ( i = 0 ; i < 2 ; ++i ) {
    RSD_BLOCKLIST **pblocklist ;
    pblocklist = ( i == 0 ) ? & rsds->dblocks : & rsds->cblocks ;
    if ( *pblocklist ) {
      rsd_blistfree( pblocklist ) ;
    }
  }

  mm_free( mm_pool_temp ,
           ( mm_addr_t ) rsds ,
           sizeof( RSD_STORE )) ;

  if ( rsd_stores == NULL )
    rsd_datapools_destroy();
}


/* rsd_storescan - scan an RSD store */
static mps_res_t rsd_storescan( mps_ss_t scan_state, RSD_STORE *rsds )
{
  mps_res_t res;

  /* This will only be called when single-threading, so no synchronization. */
  res = rsd_blistscan( scan_state, rsds->dblocks );
  if ( res != MPS_RES_OK )
    return res;
  if ( rsds->cblocks != NULL )
    res = rsd_blistscan( scan_state, rsds->cblocks );
  return res;
}


/* -------------------------------------------------------------------------- */
Bool rsd_storeread(RSD_STORE *rsds, uint8 **rbuf, int32 *rbytes)
{
  RSD_BLOCKLIST *blocklist ;
  Bool saverestorefilepos;
  Bool res;

  HQASSERT( rsds , "rsd_storeread: rsds NULL" ) ;
  blocklist = rsds->dblocks ;
  HQASSERT( blocklist , "rsd_storeread: rsds->dblocks NULL" ) ;

  saverestorefilepos = ( rsds->saverestorefilepos &&
                         rsds->cblocks == NULL ) ;
  multi_mutex_lock(&rsd_mutex);
  res = rsd_blistread( blocklist, saverestorefilepos, rbuf, rbytes );
  multi_mutex_unlock(&rsd_mutex);
  rsd_offers_limited = FALSE; /* As the state has changed, try again. */
  if ( !res )
    return FALSE;
#if RSD_CHECK_COMP_RATIO
  /* Could lock more efficiently, but this not used ATM. */
  multi_mutex_lock(&rsd_mutex);
  /* Compressed and uncompressed data: delete compressed if have all
     the uncompressed data in memory or on blockfile and: i. Accessing
     data sequentially and compressed data not much smaller than
     uncompressed or, ii. Accessing data randomly. */
  if ( rsds->cblocks &&
       rsd_blistlength( rsds->dblocks ) != RSD_LENGTH_UNKNOWN ) {
    if ( rsds->freecblocks == -1 ) {
      if ( rsd_blistaccesshint( blocklist ) == RSD_ACCESS_SEQN ) {
        if ( rsd_blistlength( rsds->cblocks ) == RSD_LENGTH_UNKNOWN )
          rsd_blistdolength( rsds->cblocks ) ;
        /* Must have at least 60 % compression for it to be worth
           keeping the compressed blocks around. */
        if ( rsd_blistlength( rsds->cblocks ) > ( rsd_blistlength( blocklist ) * 0.6 ))
          rsds->freecblocks = TRUE ;
        else
          rsds->freecblocks = FALSE ;
      }
      else
        rsds->freecblocks = TRUE ;
    }
    if ( rsds->freecblocks && rsd_blistcomplete( blocklist )) {
      FILELIST *cflptr ;
#ifdef RSD_STORE_STATS
      rsd_storestats( rsds ) ;
      monitorf((uint8*)"RSD Store: Not worth keeping compressed data in store (Compression rate less than 60%%).\n" ) ;
      monitorf((uint8*)"RSD Store: Will therefore now free the store of the compressed data.\n\n" ) ;
#endif
      cflptr = rsd_blistgetsource( rsds->cblocks ) ;
      if ( cflptr ) {
        FILELIST *flptr , *uflptr ;
        /* Merge the two filter chains back together (without the
           rsdstore filter of course). */
        flptr = rsd_blistgetsource( rsds->dblocks ) ;
        uflptr = theIUnderFile( flptr ) ;
        while ( theIUnderFile( uflptr )) {
          uflptr = theIUnderFile( uflptr ) ;
          flptr  = theIUnderFile( flptr ) ;
        }
        theIUnderFile( flptr ) = cflptr ;
        theIUnderFilterId( flptr ) = theIFilterId( cflptr ) ;
        rsd_blistsetsource( blocklist , flptr ) ;
      }
      else
        /* Not seekable. */
        rsd_blistsetsource( blocklist , NULL ) ;
      rsd_blistfree( & ( rsds->cblocks )) ;
    }
    else
      rsd_blistforcetodisk( blocklist ) ;
  }
  multi_mutex_unlock(&rsd_mutex);
#endif /* RSD_CHECK_COMP_RATIO */
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool rsd_storeseek( RSD_STORE *rsds , int32 offset ,
                    int32 *roffset )
{
  Bool res;

  HQASSERT( rsds , "rsd_storeseek: rsds NULL" ) ;
  HQASSERT( roffset , "rsd_storeseek: roffset NULL" ) ;
  multi_mutex_lock(&rsd_mutex);
  res = rsd_blistseek( rsds->dblocks , offset , roffset ) ;
  multi_mutex_unlock(&rsd_mutex);
  return res;
}

/* -------------------------------------------------------------------------- */
int32 rsd_storelength( RSD_STORE *rsds )
{
  RSD_BLOCKLIST *blocklist ;

  HQASSERT( rsds , "rsd_storelength: rsds NULL" ) ;

  blocklist = rsds->dblocks ;
  HQASSERT( blocklist , "rsd_storelength: blocklist NULL" ) ;

  /* It's thread-safe to read the length, because purging doesn't change it. */
  if ( rsd_blistlength( blocklist ) == RSD_LENGTH_UNKNOWN ) {
    uint8 *buf ;
    int32 bytes ;

    multi_mutex_lock(&rsd_mutex);
    do {
      if ( ! rsd_storeread( rsds , & buf , & bytes )) {
        multi_mutex_unlock(&rsd_mutex);
        return 0 ;
      }
      /* Clear the lock on the block of mem since nothing is actually
         using the data at the moment (this allows an extra block of
         memory to be recycled if necessary). */
      rsd_blistclearlock( blocklist ) ;
    } while ( bytes > 0 ) ;
    multi_mutex_unlock(&rsd_mutex);
  }
  HQASSERT( rsd_blistlength( blocklist ) >= 0 , "rsd_storelength: length < 0" ) ;
  return rsd_blistlength( blocklist ) ;
}

/* -------------------------------------------------------------------------- */
static int32 rsd_storefilterfillbuff( FILELIST *filter )
{
  RSD_STORE *rsds ;
  RSD_BLOCKLIST *blocklist ;
  uint8 *buf ;
  int32 bytes ;
  Bool res;

  rsds = ( RSD_STORE * ) theIFilterPrivate( filter ) ;
  HQASSERT( rsds , "rsd_storefilterfillbuff: rsds NULL" ) ;

  if ( theIFilterState( filter ) == FILTER_INIT_STATE )
    theIFilterState( filter ) = FILTER_EMPTY_STATE ;

  if ( isIEof( filter ))
    return EOF ;

  multi_mutex_lock(&rsd_mutex);
  blocklist = rsds->cblocks ;
  HQASSERT( blocklist , "rsd_storefilterfillbuff: blocklist NULL" ) ;
  res = rsd_blistread( blocklist, rsds->saverestorefilepos, &buf, &bytes );
  multi_mutex_unlock(&rsd_mutex);
  if ( !res )
    return FALSE;

  if ( bytes < 0 ) {
    SetIEofFlag( filter ) ;
    bytes = -bytes ;
  }

  theIBufferSize( filter ) = bytes ;
  theIReadSize( filter ) = bytes ;
  theICount( filter ) = bytes - 1 ;
  theIBuffer( filter ) = buf ;
  theIPtr( filter ) = buf + 1 ;
  return ( int32 ) *( theIBuffer( filter )) ;
}

/* -------------------------------------------------------------------------- */
static int32 rsd_storefiltersetfilepos(FILELIST *flptr, const Hq32x2 *position)
{
  RSD_STORE *rsds ;

  HQASSERT( flptr , "rsd_storefiltersetfilepos: flptr NULL" ) ;
  HQASSERT( position != NULL , "rsd_storefiltersetfilepos: NULL position pointer" ) ;
  HQASSERT( Hq32x2CompareInt32(position, 0) == 0 , "rsd_storefiltersetfilepos: position != 0" ) ;
  UNUSED_PARAM(const Hq32x2 *, position) ;

  rsds = ( RSD_STORE * ) theIFilterPrivate( flptr ) ;
  HQASSERT( rsds , "rsd_storefiltersetfilepos: rsds NULL" ) ;
  HQASSERT( rsds->cblocks , "rsd_storefiltersetfilepos: cblocks NULL" ) ;

  /* No sync needed because purging doesn't care about the read position. */
  rsd_blistreset( rsds->cblocks ) ;

  return 0 ;
}

/* -------------------------------------------------------------------------- */

static Bool rsd_storefilterinit(FILELIST *filter, OBJECT *args, STACK *stack)
{
  UNUSED_PARAM(FILELIST *, filter) ;
  UNUSED_PARAM(OBJECT *, args) ;

  /* Since the %rsdstore filter is only used internally, and that call does
     not use the stack, we should throw an error if there is a stack supplied;
     this probably means the filter was called from PostScript. */
  if ( stack )
    return error_handler(UNDEFINED) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */

static Bool rsd_allow_purge_to_disk = FALSE;


/** Finds a blocklist with a recyclable block, possibly writing the
    block to disk

  \param[in] ablocklist  If non-NULL, search this blocklist only; otherwise, search all
  \param[in] tbytes  Size of block being sought, or -1.
  \param[in] accesstype  Search random/sequential access blocklists.
  \param[in] actions  An array of flag masks.
  \param[in] numactions  Length of \p actions.
  \param[in] allow_open  Is it allowed to open a file to purge.
  \param[out] rblocklist  Variable to receive the blocklist found
 */
static Bool rsd_reclaim( corecontext_t *context, RSD_BLOCKLIST *ablocklist,
                         int32 tbytes , int accesstype ,
                         int *actions, size_t numactions, Bool allow_open,
                         RSD_BLOCKLIST **rblocklist, size_t *purge_size )
{
  Bool gotone ;
  size_t i, j;
  int32 saved_error_code = 0;

  *rblocklist = NULL ;

  for ( i = 0 ; i < numactions ; ++i ) {
    int32 action = actions[ i ] ;
    if ( ablocklist ) {
      if ( ! rsd_blistfindreclaim( context, ablocklist, tbytes, accesstype,
                                   action, allow_open, &gotone, purge_size )) {
        /* No fatal errors, just can't recover space. */
        error_save_context(context->error, &saved_error_code);
        allow_open = FALSE; /* try without opening files */
        continue;
      }
      if ( gotone ) {
        *rblocklist = ablocklist ;
        if ( saved_error_code != 0 ) {
          error_restore_context(context->error, saved_error_code);
          error_clear_context(context->error);
        }
        return TRUE ;
      }
    }
    else {
      for ( j = 0 ; j < 2 ; j++ ) {
        RSD_STORE *store ;
        for ( store = rsd_stores ; store ; store = store->next ) {
          RSD_BLOCKLIST *blocklist ;
          blocklist = ( j == 0 ? store->dblocks : store->cblocks ) ;
          if ( blocklist ) {
            if ( ! rsd_blistfindreclaim( context, blocklist, tbytes, accesstype,
                                         action, allow_open,
                                         &gotone, purge_size )) {
              /* No fatal errors, just can't recover space. */
              error_save_context(context->error, &saved_error_code);
              allow_open = FALSE; /* try without opening files */
              continue;
            }
            if ( gotone ) {
              *rblocklist = blocklist ;
              if ( saved_error_code != 0 ) {
                error_restore_context(context->error, saved_error_code);
                error_clear_context(context->error);
              }
              return TRUE ;
            }
          }
        }
      }
    }
  }
  if ( saved_error_code != 0 ) {
    error_restore_context(context->error, saved_error_code);
    return FALSE;
  } else
    return TRUE;
}

/* -------------------------------------------------------------------------- */
/* Finds a recyclable block and steals its databuffer.

  Arguments like rsd_reclaim. Finds a recyclable block, frees it -
  writing data to disk, if necessary - and returns its databuffer. */
Bool rsd_recycle( RSD_BLOCKLIST *ablocklist, int32 tbytes, int accesstype,
                  uint8 **rdata, int32 *rbytes )
{
  int seqn_actions[ 8 ] = {
    /* no disk write */   /* from headblock */   RSD_EXACTBYTES         ,
    /* no disk write */   RSD_FROMFILLBLOCK |    RSD_EXACTBYTES         ,
    0 /* no disk write */ /* from headblock */ /* allow larger block */ ,
    /* no disk write */   RSD_FROMFILLBLOCK    /* allow larger block */ ,
    RSD_ALLOWDISKWRITE |  /* from headblock */   RSD_EXACTBYTES         ,
    RSD_ALLOWDISKWRITE |  RSD_FROMFILLBLOCK |    RSD_EXACTBYTES         ,
    RSD_ALLOWDISKWRITE    /* from headblock */ /* allow larger block */ ,
    RSD_ALLOWDISKWRITE |  RSD_FROMFILLBLOCK    /* allow larger block */
  } ;
  int rand_actions[ 4 ] = {
    /* no disk write */   RSD_EXACTBYTES           ,
    0 /* no disk write */ /* allow larger block */ ,
    RSD_ALLOWDISKWRITE |  RSD_EXACTBYTES           ,
    RSD_ALLOWDISKWRITE    /* allow larger block */
  } ;
  int *actions;
  size_t n;
  RSD_BLOCKLIST *blocklist ;
  size_t purge_size;
  corecontext_t *context = get_core_context();
  FILEIOPARAMS *file_params = context->fileioparams;
  Bool no_error;

  HQASSERT( accesstype == RSD_ACCESS_SEQN || accesstype == RSD_ACCESS_RAND ,
            "rsd_storepurge: accesstype unexpected" ) ;

  if ( accesstype == RSD_ACCESS_SEQN ) {
    actions = seqn_actions ;
    n = NUM_ARRAY_ITEMS(seqn_actions) ;
  }
  else {
    actions = rand_actions ;
    n = NUM_ARRAY_ITEMS(rand_actions) ;
  }
  if ( file_params != NULL ) /* only read when accessible */
    rsd_allow_purge_to_disk = file_params->LowMemRSDPurgeToDisk;
  if ( !rsd_allow_purge_to_disk )
    n = n / 2; /* drop disk actions */

  rsd_handler_releasing = TRUE; /* Note rsd_mutex is always locked here. */
  no_error = rsd_reclaim( context, ablocklist, tbytes, accesstype, actions, n,
                          context->is_interpreter, &blocklist, &purge_size );
  if ( no_error && blocklist != NULL )
    rsd_blistrecycle( blocklist , rdata , rbytes ) ;
  rsd_handler_releasing = FALSE;
  return no_error;
}

/* -------------------------------------------------------------------------- */


/** Estimate memory available for RSD disk purges.

  This doesn't have to be very good, just stable and likely to grow when there
  is actually memory available. That avoids an infinite reclaim retry and
  probably a permanent offer shutdown.
 */
static size_t rsd_available_memory(void)
{
  return mps_arena_commit_limit(mm_arena) - mps_arena_committed(mm_arena);
}


/** Type for the details of an RSD low-memory offer. */
typedef struct {
  low_mem_offer_t lm_offer;
  int32 accesstype;
} rsd_offer_t;


/** Solicit method of the RSD low-memory handler, the actual code.

  This is shared by both the sequential and the random handlers.
 */
static low_mem_offer_t *rsd_solicit(low_mem_handler_t *handler,
                                    corecontext_t *context,
                                    rsd_offer_t *offer,
                                    int32 accesstype,
                                    int32 *actions, size_t n_actions)
{
  RSD_BLOCKLIST *blocklist = NULL;
  Bool res;
  size_t i;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");

  if ( context->fileioparams != NULL ) /* only read when accessible */
    rsd_allow_purge_to_disk = context->fileioparams->LowMemRSDPurgeToDisk;
  if ( !rsd_allow_purge_to_disk && handler->tier == memory_tier_disk )
    return NULL;
  for ( i = 0 ; i < n_actions ; ++i )
    if ( handler->tier == memory_tier_disk )
      actions[i] |= RSD_ALLOWDISKWRITE;
    else
      actions[i] &= ~RSD_ALLOWDISKWRITE;

  /* The rsd_handler_releasing check doesn't have to be synchronized, because
     the mutex protects against other threads, this just prevents nesting. */
  if ( rsd_handler_releasing )
    return FALSE; /* don't offer inside another RSD handler */
  if ( !multi_mutex_trylock(&rsd_mutex) )
    return NULL; /* give up if can't get the lock */

  if ( rsd_offers_limited )
    if ( rsd_limit_memory_size < rsd_available_memory() )
      /* If more memory is available, can try again. */
      rsd_offers_limited = FALSE;

  if ( !rsd_offers_limited || handler->tier != memory_tier_disk ) {
    res = rsd_reclaim( context, NULL, -1, accesstype, actions, n_actions,
                       context->is_interpreter,
                       &blocklist, &offer->lm_offer.offer_size );
    HQASSERT(res, "rsd_reclaim can't fail here");
  }

  multi_mutex_unlock(&rsd_mutex);

  if ( !blocklist )
    return NULL;
  offer->lm_offer.pool = mm_pool_rsd;
  offer->lm_offer.offer_cost = accesstype == RSD_ACCESS_SEQN ? 1.0f : 32.0f;
  offer->lm_offer.next = NULL;
  offer->accesstype = accesstype;
  return &offer->lm_offer;
}


/** Solicit method of the RSD low-memory handler, sequential version. */
static low_mem_offer_t *rsd_seqn_solicit(low_mem_handler_t *handler,
                                         corecontext_t *context,
                                         size_t count,
                                         memory_requirement_t* requests)
{
  static int actions[ 2 ] = { /* check if there are any blocks to free */
    RSD_ALLOWDISKWRITE                      | RSD_NO_WRITE,
    RSD_ALLOWDISKWRITE |  RSD_FROMFILLBLOCK | RSD_NO_WRITE
  };
  static rsd_offer_t offer;
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  return rsd_solicit(handler, context,
                     &offer, RSD_ACCESS_SEQN, actions, 2);
}


/** Solicit method of the RSD low-memory handler, random-access version. */
static low_mem_offer_t *rsd_rand_solicit(low_mem_handler_t *handler,
                                         corecontext_t *context,
                                         size_t count,
                                         memory_requirement_t* requests)
{
  static int actions[ 1 ] = { /* check if there are any blocks to free */
    RSD_ALLOWDISKWRITE                      | RSD_NO_WRITE
  };
  static rsd_offer_t offer;
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  return rsd_solicit(handler, context,
                     &offer, RSD_ACCESS_RAND, actions, 1);
}


/** Release method of the RSD low-memory handler */
static Bool rsd_release(low_mem_handler_t *handler,
                        corecontext_t *context, low_mem_offer_t *offer)
{
  static int seqn_actions[ 4 ] = {
    0 /* no disk write */ /* from headblock */,
    /* no disk write */   RSD_FROMFILLBLOCK,
    RSD_ALLOWDISKWRITE    /* from headblock */,
    RSD_ALLOWDISKWRITE |  RSD_FROMFILLBLOCK
  };
  static int rand_actions[ 2 ] = {
    0 /* no disk write */,
    RSD_ALLOWDISKWRITE
  };
  int *actions;
  size_t n, purge_size;
  RSD_BLOCKLIST *blocklist;
  int accesstype;

  HQASSERT(handler != NULL, "No handler");
  HQASSERT(context != NULL, "No context");
  HQASSERT(offer != NULL, "No offer");
  HQASSERT(offer->next == NULL, "Multiple offers");
  UNUSED_PARAM(low_mem_handler_t *, handler);

  accesstype = ((rsd_offer_t*)offer)->accesstype;
  HQASSERT( accesstype == RSD_ACCESS_SEQN || accesstype == RSD_ACCESS_RAND,
            "rsd_storepurge: accesstype unexpected" );
  if ( accesstype == RSD_ACCESS_SEQN ) {
    actions = seqn_actions;
    n = sizeof( seqn_actions ) / sizeof( *seqn_actions );
  } else {
    actions = rand_actions;
    n = sizeof( rand_actions ) / sizeof( *rand_actions );
  }
  if ( handler->tier != memory_tier_disk )
    n = n / 2; /* omit disk actions */

  if ( !multi_mutex_trylock(&rsd_mutex) )
    return TRUE; /* give up if can't get the lock */
  rsd_handler_releasing = TRUE;

  if ( !rsd_reclaim( context, NULL, -1, accesstype, actions, n,
                     context->is_interpreter, &blocklist, &purge_size )) {
    HQASSERT(!rsd_offers_limited, "Shouldn't fail to purge if already limited");
    rsd_offers_limited = TRUE;
    rsd_limit_memory_size = rsd_available_memory();
    /* No fatal errors, just means we can't recover space. */
    error_clear_context(context->error);
  } else
    if ( blocklist != NULL )
      rsd_blistpurge( blocklist );

  rsd_handler_releasing = FALSE;
  multi_mutex_unlock(&rsd_mutex);
  return TRUE;
}


/** The RSD low-memory handler, sequential RAM version. */
static low_mem_handler_t seqn_RAM_handler = {
  "RSD sequential RAM block purge",
  memory_tier_ram, rsd_seqn_solicit, rsd_release, TRUE, 0, FALSE };
/** The RSD low-memory handler, random-access RAM version. */
static low_mem_handler_t rand_RAM_handler = {
  "RSD random RAM block purge",
  memory_tier_ram, rsd_rand_solicit, rsd_release, TRUE, 0, FALSE };
/** The RSD low-memory handler, sequential disk version. */
static low_mem_handler_t seqn_disk_handler = {
  "RSD sequential disk block purge",
  memory_tier_disk, rsd_seqn_solicit, rsd_release, TRUE, 0, FALSE };
/** The RSD low-memory handler, random-access disk version. */
static low_mem_handler_t rand_disk_handler = {
  "RSD random disk block purge",
  memory_tier_disk, rsd_rand_solicit, rsd_release, TRUE, 0, FALSE };


/* -------------------------------------------------------------------------- */
static void rsd_analysefilterchain( FILELIST *source ,
                                    Bool seekable ,
                                    FILELIST **csource ,
                                    FILELIST **rcompfilter ,
                                    Bool *encoded )
{
  FILELIST *flptr ;
  /* Look for an uncompressed disk file or with exactly one filter
     that only delimits the data (eg Stream, SubFileDecode). */
  flptr = theIUnderFile( source ) ;
  if ( ! seekable ||
       flptr == NULL ||
       ( theIUnderFile( flptr ) == NULL &&
         isIDelimitsData( source ))) {
    *csource = NULL ;
    *rcompfilter = NULL ;
    *encoded = FALSE ; /* N/A if ! seekable */
  }
  else {
    FILELIST *compfilter ;
    /* Typical filter layout maybe:
       RSD -> LZW -> ASCII85 -> Stream -> RealFile
       In which case, the source for the compressed blocks comes from
       after the ASCII85 filter has been applied. */

    /* Traverse the filter chain top to bottom until hit a filter that
       contracts the data (eg ASCII85), or a filter that delimits the
       data (eg Stream). */
    flptr = source ;
    compfilter = NULL ;
    while ( flptr && isIExpandsData( flptr ) &&
            isIOpenFileFilterById( theIUnderFilterId( flptr ) ,
                                   theIUnderFile( flptr ) ) ) {
      compfilter = flptr ;
      flptr = theIUnderFile( flptr ) ;
    }
    if ( compfilter ) {
      *csource = flptr ;
      *rcompfilter = compfilter ;
      *encoded = TRUE ;
    }
    else {
      *csource = NULL ;
      *rcompfilter = NULL ;
      *encoded = TRUE ;
    }
  }
}

/* -------------------------------------------------------------------------- */
#ifdef RSD_STORE_STATS
static void rsd_storestats( RSD_STORE *rsds )
{
  int32 totalbytes ;
  int purge0, purge1, purge2, purgen;
  static Bool fullstoredump = FALSE;
  HQASSERT( rsds , "rsd_debugstats: rsds NULL" ) ;
  HQASSERT( rsds->dblocks , "rsd_debugstats: rsds->dblocks NULL" ) ;

  /* Not synchronized, because it's safe to traverse the list and the
     stats don't need to be consistent. */
  monitorf((uint8*)"==========================================================================\n" ) ;
  monitorf((uint8*)"RSD Store Usage Stats:\n" ) ;

  purge0 = purge1 = purge2 = purgen = 0 ;

  monitorf((uint8*)"\nDecoded Blocks:\n" ) ;
  totalbytes = sizeof( RSD_STORE ) ;
  totalbytes += rsd_bliststats( rsds->dblocks , fullstoredump ,
                                & purge0 , & purge1 ,
                                & purge2 , & purgen ) ;

  if ( rsds->cblocks ) {
    monitorf((uint8*)"\nEncoded Blocks:\n" ) ;
    totalbytes += rsd_bliststats( rsds->cblocks , fullstoredump ,
                                  & purge0 , & purge1 ,
                                  & purge2 , & purgen ) ;
  }
  monitorf((uint8*)"\nTotal bytes for RSD Store: %d\n\n" , totalbytes ) ;
  monitorf((uint8*)"Purge Frequency:\npurge0: %d \t purge1: %d \t purge2: %d \t purgen: %d\n" ,
           purge0 , purge1 , purge2 , purgen ) ;
  monitorf((uint8*)"==========================================================================\n\n" ) ;
}
#endif

void rsd_store_filter(FILELIST *flptr)
{
  /* RSD store decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("%rsdstore"),
                       FILTER_FLAG | READ_FLAG | OPEN_FLAG | REWINDABLE ,
                       0, NULL , 0 ,
                       rsd_storefilterfillbuff,   /* fillbuff */
                       FilterFlushBufError,       /* flushbuff */
                       rsd_storefilterinit,       /* initfile */
                       FilterCloseNoOp,           /* closefile */
                       FilterDispose,             /* disposefile */
                       FilterBytesAvailNoOp,      /* bytesavail */
                       FilterReset,               /* resetfile */
                       FilterFilePosNoOp,         /* filepos */
                       rsd_storefiltersetfilepos, /* setfilepos */
                       FilterNoOp,                /* flushfile */
                       FilterEncodeError,         /* filterencode */
                       FilterDecodeError,         /* filterdecode */
                       FilterLastError ,          /* lasterror */
                       0, NULL, NULL, NULL ) ;
}


/* -------------------------------------------------------------------------- */
void init_C_globals_rsdstore(void)
{
  rsd_stores = NULL;
  rsd_handler_releasing = FALSE; rsd_offers_limited = FALSE;
#ifdef METRICS_BUILD
  rsd_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&rsd_metrics_hook) ;
#endif
}


/* rsd_storeinit - initialize RSD store module */
Bool rsd_storeinit(void)
{
  if ( mps_root_create( &rsd_storesroot, mm_arena, mps_rank_exact(),
                        0, rsd_storeslistscan, NULL, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  multi_mutex_init(&rsd_mutex, RSD_LOCK_INDEX, TRUE,
                   SW_TRACE_RSD_ACQUIRE, SW_TRACE_RSD_HOLD);

  if ( !low_mem_handler_register(&seqn_RAM_handler)
       || !low_mem_handler_register(&rand_RAM_handler)
       || !low_mem_handler_register(&seqn_disk_handler)
       || !low_mem_handler_register(&rand_disk_handler) ) {
    multi_mutex_finish(&rsd_mutex) ;
    mps_root_destroy(rsd_storesroot) ;
    return FAILURE(FALSE) ;
  }
  return TRUE ;
}


/* rsd_storefinish - finish RSD store module */
void rsd_storefinish(void)
{
  low_mem_handler_deregister(&rand_disk_handler);
  low_mem_handler_deregister(&seqn_disk_handler);
  low_mem_handler_deregister(&rand_RAM_handler);
  low_mem_handler_deregister(&seqn_RAM_handler);
  multi_mutex_finish(&rsd_mutex);
  mps_root_destroy( rsd_storesroot );
}


/* Log stripped */
