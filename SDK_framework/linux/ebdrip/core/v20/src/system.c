/** \file
 * \ingroup mm
 *
 * $HopeName: SWv20!src:system.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * System memory allocations.
 */

#include "core.h"
#include "swerrors.h"
#include "mm.h"

#include "graphics.h"
#include "gstack.h"
#include "devops.h"
#include "system.h"
#include "idlom.h"
#include "swoften.h"
#include "often.h"
#include "gu_path.h"
#include "metrics.h"

#include "namedef_.h"

/*
 * SAC allocation sizes - to ensure what is alloc'd/free'd matches what
 * is used to create the SAC.
 */
#define SAC_ALLOC_CLIPSIZE  (DWORD_ALIGN_UP(size_t, sizeof(CLIPRECORD)))   /* 96 bytes - 31/7/00 */
#define SAC_ALLOC_PATHSIZE  (DWORD_ALIGN_UP(size_t, sizeof(PATHLIST)))     /* 12 bytes - 31/7/00 */
#define SAC_ALLOC_LINESIZE  (DWORD_ALIGN_UP(size_t, sizeof(LINELIST)))     /* 24 bytes - 31/7/00 */

#ifdef METRICS_BUILD
struct system_metrics {
  int32 cliprecords, maxcliprecords ;
  int32 pathlists, maxpathlists ;
  int32 linelists, maxlinelists ;
  int32 charpaths, maxcharpaths ;
  int32 clippaths, maxclippaths ;
} system_metrics ;

static Bool system_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("MM")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("System")) )
    return FALSE ;
  SW_METRIC_INTEGER("max_pathlists", system_metrics.maxpathlists);
  SW_METRIC_INTEGER("max_linelists", system_metrics.maxlinelists);
  SW_METRIC_INTEGER("max_cliprecords", system_metrics.maxcliprecords);
  SW_METRIC_INTEGER("max_charpaths", system_metrics.maxcharpaths);
  SW_METRIC_INTEGER("max_clippaths", system_metrics.maxclippaths);
  sw_metrics_close_group(&metrics) ; /*System*/
  sw_metrics_close_group(&metrics) ; /*MM*/

  return TRUE ;
}

static void system_metrics_reset(int reason)
{
  struct system_metrics init = { 0 } ;
  UNUSED_PARAM(int, reason) ;
  system_metrics = init ;
}

static sw_metrics_callbacks system_metrics_hook = {
  system_metrics_update,
  system_metrics_reset,
  NULL
} ;

#define METRIC_INCREMENT(name_) MACRO_START \
  ++system_metrics.name_ ; \
  if ( system_metrics.name_ > system_metrics.max ## name_ ) \
    system_metrics.max ## name_ = system_metrics.name_ ; \
MACRO_END

#define METRIC_DECREMENT(name_) MACRO_START \
  --system_metrics.name_ ; \
MACRO_END

#else /*!METRICS_BUILD*/

#define METRIC_INCREMENT(x) EMPTY_STATEMENT()
#define METRIC_DECREMENT(x) EMPTY_STATEMENT()

#endif /*!METRICS_BUILD*/

/* ----------------------------------------------------------------------------
   function:            various            author:              Andrew Cave
   creation date:       24-Feb-1993        last modification:   ##-###-####
   description:

  Cache routines for CLIPRECORDs.

---------------------------------------------------------------------------- */

#ifdef ASSERT_BUILD
enum {
  CLIP_TRACE_REC = 1,
  CLIP_TRACE_PATH = 2,
  CLIP_TRACE_REF = 4
} ;

static int32 trace_clips = 0 ;
#endif

CLIPRECORD* get_cliprec(
  mm_pool_t   pool)     /* I */
{
  CLIPRECORD* retclip;

  retclip = mm_sac_alloc(pool, SAC_ALLOC_CLIPSIZE, MM_ALLOC_CLASS_CLIPRECORD);
  if ( retclip ) {
    METRIC_INCREMENT(cliprecords) ;

    /* Initialise the path: copypath now requires that its target is empty, to
     * catch potential leaks.
     */
    theISystemAlloc(retclip) = PATHTYPE_DYNMALLOC;
    path_init( &theClipPath(*retclip)) ;
    retclip->refcount = 0 ;
    retclip->clipflat = 0.0f ;
    bbox_store(&retclip->bounds, 0, 0, 0, 0) ;
    retclip->clipno = CLIPID_INVALID ;
    retclip->pagebaseid = PAGEBASEID_INVALID ;
    retclip->next = NULL ;

    HQTRACE(trace_clips & CLIP_TRACE_REC,
            ("New CLIPRECORD %p", retclip)) ;
  } else
    (void)error_handler(VMERROR);
  return(retclip);
}

void free_cliprec(
  CLIPRECORD*   clipptr,    /* I */
  mm_pool_t     pool)       /* I */
{
  HQTRACE(trace_clips & CLIP_TRACE_REC,
          ("Free CLIPRECORD %p", clipptr)) ;

  HQASSERT(NULL != clipptr, "NULL cliprecord pointer") ;
  HQASSERT(theISystemAlloc(clipptr) == PATHTYPE_DYNMALLOC,
           "freeing non-allocated cliprecord");
  theISystemAlloc(clipptr) = PATHTYPE_FREED;
  METRIC_DECREMENT(cliprecords) ;
  mm_sac_free(pool, (mm_addr_t)clipptr, SAC_ALLOC_CLIPSIZE);
}

/* ---------------------------------------------------------------------------*/
CLIPPATH *gs_newclippath(CLIPPATH *top)
{
  CLIPPATH *clippath = mm_alloc( mm_pool_temp , sizeof( CLIPPATH ) ,
                                 MM_ALLOC_CLASS_CLIP_PATH ) ;
  if ( clippath == NULL ) {
    (void)error_handler( VMERROR ) ;
    return NULL ;
  }

  METRIC_INCREMENT(clippaths) ;

  *clippath = *top ;
  theClipRefCount(*clippath) = 1 ;
  gs_reservecliprec(theClipRecord(*clippath)) ;
  /* Link in underneath top, because top is a part of the gstate
     structure. */

  HQTRACE((trace_clips & CLIP_TRACE_REC),
          ("New CLIPPATH %p rec=%p", clippath, theClipRecord(*clippath))) ;

  return clippath ;
}

/* ---------------------------------------------------------------------------*/
void gs_reserveclippath( CLIPPATH *clippath )
{
  if ( clippath ) {
    HQTRACE((trace_clips & CLIP_TRACE_REF),
            ("Reserve CLIPPATH %p refcount now %d next=%p rec=%p",
             clippath,theClipRefCount(*clippath) + 1, clippath->next,
             theClipRecord(*clippath))) ;
    ++theClipRefCount(*clippath) ;
    HQASSERT(theClipRefCount(*clippath) > 0, "clip ref counts overflowed") ;
  }
}

/* ---------------------------------------------------------------------------*/
void gs_freeclippath(CLIPPATH **clipstack)
{
  CLIPPATH *clippath = *clipstack ;

  while ( clippath ) {
    CLIPPATH *nextpath ;

    HQTRACE(trace_clips & CLIP_TRACE_REF,
            ("Release CLIPPATH %p refcount now %d next=%p rec=%p",
             clippath,theClipRefCount(*clippath) - 1, clippath->next,
             theClipRecord(*clippath))) ;

    HQASSERT(theClipRefCount(*clippath) >= 1, "Invalid clip record ref count") ;

    if ( --theClipRefCount(*clippath) != 0 )
      break ;

    nextpath = clippath->next ;

    gs_freecliprec(&theClipRecord(*clippath)) ;

    HQTRACE(trace_clips & CLIP_TRACE_PATH,
            ("Free CLIPPATH %p", clippath)) ;
    METRIC_DECREMENT(clippaths) ;
    mm_free( mm_pool_temp , ( mm_addr_t )clippath , sizeof( CLIPPATH )) ;

    clippath = nextpath ;
  }

  *clipstack = NULL ;
}

/* ---------------------------------------------------------------------------*/
void gs_reservecliprec( CLIPRECORD *theclip )
{
  if ( theclip ) {
    HQTRACE((trace_clips & CLIP_TRACE_REF),
            ("Reserve CLIPRECORD %p refcount now %d next=%p",
             theclip,theClipRefCount(*theclip) + 1, theclip->next)) ;
    ++theClipRefCount(*theclip) ;
    HQASSERT(theClipRefCount(*theclip) > 0, "clip ref count overflowed") ;
  }
}

/* ---------------------------------------------------------------------------*/
void gs_freecliprec(CLIPRECORD **clipptr)
{
  CLIPRECORD *theclip = *clipptr ;

  while ( theclip ) {
    CLIPRECORD *nextclip ;

    HQTRACE(trace_clips & CLIP_TRACE_REF,
            ("Release CLIPRECORD %p refcount now %d next=%p",
             theclip,theClipRefCount(*theclip) - 1, theclip->next)) ;

    HQASSERT(theClipRefCount(*theclip) >= 1, "Invalid clip record ref count") ;

    if ( --theClipRefCount(*theclip) != 0 )
      break ;

    nextclip = theclip->next ;
    HQASSERT( theclip->clipflat >= 0.0 ,
              "About to call path_free_list in a cliprec when nfill is being used." ) ;
    IDLOM_UNCACHECLIP( &theClipPath(*theclip));
    path_free_list( thePath(theClipPath(*theclip)), mm_pool_temp) ;

    free_cliprec(theclip, mm_pool_temp) ;

    theclip = nextclip ;
  }

  *clipptr = NULL ;
}

/* ----------------------------------------------------------------------------
   function:            various            author:              Andrew Cave
   creation date:       24-Feb-1993        last modification:   ##-###-####
   description:

  Cache routines for PATHLISTs.

---------------------------------------------------------------------------- */
PATHLIST* get_path(
  mm_pool_t     pool)   /* I */
{
  PATHLIST *retpath ;

  retpath = mm_sac_alloc(pool, SAC_ALLOC_PATHSIZE, MM_ALLOC_CLASS_PATHLIST);
  if ( retpath ) {
    METRIC_INCREMENT(pathlists) ;
    theISystemAlloc(retpath) = PATHTYPE_DYNMALLOC;
    /* Zero the share count */
    retpath->shared = 0 ;
  } else
    (void)error_handler(VMERROR);

  return(retpath);
}

#define FREE_PATH(ppath, pool) MACRO_START \
  HQASSERT((NULL != ppath), "NULL path pointer"); \
  HQASSERT((theISystemAlloc(ppath) == PATHTYPE_DYNMALLOC), "freeing non-allocated path"); \
  theISystemAlloc(ppath) = PATHTYPE_FREED; \
  METRIC_DECREMENT(pathlists) ; \
  mm_sac_free((pool), (mm_addr_t)(ppath), SAC_ALLOC_PATHSIZE); \
MACRO_END

void free_path(
  PATHLIST*     pathptr,    /* I */
  mm_pool_t     pool)       /* I */
{
  FREE_PATH(pathptr, pool);
}

/* ----------------------------------------------------------------------------
   function:            various            author:              Andrew Cave
   creation date:       24-Feb-1993        last modification:   ##-###-####
   description:

  Cache routines for CLIPRECORDs.

---------------------------------------------------------------------------- */
#define GET_LINE(pline, pool) MACRO_START \
  (pline) = mm_sac_alloc((pool), SAC_ALLOC_LINESIZE, MM_ALLOC_CLASS_LINELIST); \
  if ( (pline) ) { \
    METRIC_INCREMENT(linelists) ; \
    pline->systemalloc = PATHTYPE_DYNMALLOC; \
    pline->order = 0; \
    INIT_LINELIST_FLAGS(pline); \
  } \
MACRO_END

#define FREE_LINE(pline, pool) MACRO_START \
  HQASSERT((NULL != pline), "NULL line pointer"); \
  HQASSERT((pline->systemalloc == PATHTYPE_DYNMALLOC), "freeing non-allocated LINELIST"); \
  pline->systemalloc = PATHTYPE_FREED; \
  METRIC_DECREMENT(linelists) ; \
  mm_sac_free((pool), (mm_addr_t)(pline), SAC_ALLOC_LINESIZE); \
MACRO_END


/*
 * get_line() - sac allocate a LINELIST in specified pool but (I believe)
 * it is uncheckable that the pool has a SAC allocator - caveat emptor!
 */
LINELIST *get_line(
  mm_pool_t     pool)     /* I */
{
  LINELIST *retline ;

  GET_LINE(retline, pool);
  if ( retline == NULL )
    (void)error_handler(VMERROR);
  return(retline);
}


LINELIST *get_3line(
  mm_pool_t     pool)     /* I */
{
  LINELIST *l1 , *l2 , *l3 ;

  GET_LINE(l1, pool);
  GET_LINE(l2, pool);
  GET_LINE(l3, pool);
  if ( l1 && l2 && l3 ) {
    l1->next = l2;
    l2->next = l3;
    l3->next = NULL;
    return(l1);
  }
  if ( l1 ) {
    FREE_LINE(l1, pool);
  }
  if ( l2 ) {
    FREE_LINE(l2, pool);
  }
  if ( l3 ) {
    FREE_LINE(l3, pool);
  }
  (void)error_handler(VMERROR);
  return(NULL);
}

void free_line(
  LINELIST*     lineptr,        /* I */
  mm_pool_t     pool)           /* I */
{
  FREE_LINE(lineptr, pool);
}

void path_free_list(
  PATHLIST*     thepath,    /* I */
  mm_pool_t     pool)       /* I */
{
  LINELIST *tmpline ;
  LINELIST *theline ;
  PATHLIST *tmppath ;

  /* PATHLISTs only deleted when no longer shared */
  if (thepath && thepath->shared) {
    thepath->shared-- ;
    return ;
  }

  while ( thepath ) {
    SwOftenUnsafe() ;

    theline = theSubPath(*thepath) ;
    while ( theline ) {
      tmpline = theline ;
      theline = theline->next ;
      HQASSERT( tmpline != theline , "recursive line chain" ) ;
      FREE_LINE(tmpline, pool);
    }

    tmppath = thepath ;
    thepath = thepath->next ;
    HQASSERT( tmppath != thepath , "recursive path chain" ) ;
    FREE_PATH(tmppath, pool);
  }
}

/* ----------------------------------------------------------------------------
   function:            various            author:              Andrew Cave
   creation date:       24-Feb-1993        last modification:   ##-###-####
   description:

  Memory cache routines for CHARPATHS.

---------------------------------------------------------------------------- */
CHARPATHS *thecharpaths = NULL ;

CHARPATHS *get_charpaths( void )
{
  CHARPATHS *temp ;

  if ( NULL == (temp = (CHARPATHS *)mm_alloc(mm_pool_temp, sizeof(CHARPATHS),
                                      MM_ALLOC_CLASS_CHARPATHS))) {
    (void)error_handler( VMERROR );
    return NULL ;
  }

  METRIC_INCREMENT(charpaths) ;
  temp->next = thecharpaths ;
  thecharpaths = temp ;
  return ( temp ) ;
}

void free_charpaths( void )
{
  CHARPATHS *temp ;

  temp = thecharpaths ;
  thecharpaths = thecharpaths->next ;
  METRIC_DECREMENT(charpaths) ;
  mm_free(mm_pool_temp, (mm_addr_t)temp, sizeof(CHARPATHS)) ;
}

/* ----------------------------------------------------------------------------
   function:            various            author:              Andrew Cave
   creation date:       21-Aug-1987        last modification:   ##-###-####
   description:

  Initialise all memory caches.

---------------------------------------------------------------------------- */
Bool initSystemMemoryCaches(
  mm_pool_t   pool)       /* I */
{
  /* Array must be sorted into ascending block size */
  struct mm_sac_classes_t sac_classes[3] = { /* size, num, freq */
    { SAC_ALLOC_PATHSIZE,  512, 20 },
    { SAC_ALLOC_LINESIZE, 1024, 30 },
    { SAC_ALLOC_CLIPSIZE,  128, 10 },
  } ;

  if ( mm_sac_create(pool, sac_classes, NUM_ARRAY_ITEMS(sac_classes)) != MM_SUCCESS ) {
    return FALSE;
  }

  return TRUE;
}

void clearSystemMemoryCaches(
  mm_pool_t   pool)       /* I */
{
  HQASSERT((NULL != mm_pool_temp),
           "clearSystemMemoryCaches: NULL temp pool ptr");

  /* Only destroy the sac if it has been created */
  if (mm_sac_present(pool) == MM_SUCCESS)
    mm_sac_destroy(pool);
}

void init_C_globals_system(void)
{
#ifdef METRICS_BUILD
  system_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&system_metrics_hook) ;
#endif

#ifdef ASSERT_BUILD
  trace_clips = 0 ;
#endif

  thecharpaths = NULL ;
}

/*
Log stripped */
