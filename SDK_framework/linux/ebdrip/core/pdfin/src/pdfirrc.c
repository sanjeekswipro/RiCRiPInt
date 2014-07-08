/** \file
 * \ingroup pdfrr
 *
 * $HopeName: SWpdf!src:pdfirrc.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Internal Retained Raster cache implementation
 */

#include "core.h"

#include "control.h"    /* allow_interrupt */
#include "corejob.h"    /* corejob_t */
#include "interrupts.h" /* interrupts_clear */
#include "irr.h"        /* irr_store_free */
#include "mlock.h"      /* multi_* */
#include "pdfin.h"      /* pdf_ixc_params */
#include "swcopyf.h"    /* swncopyf */
#include "swerrors.h"   /* error_handler */
#include "swevents.h"   /* SWEVT_RR_CONNECT */
#include "taskh.h"      /* task_group_is_cancelled */
#include "timing.h"     /* SW_TRACE_* */

#include "hqmemcmp.h"   /* HqMemCmp */

#include "namedef_.h"

#if defined( DEBUG_BUILD )
#include "ripdebug.h"   /* register_ripvar */

#define DEBUG_IRRC_SEQUENCE_POINTS 1

static int32 debug_irrc = 0 ;

static int32 tweak_irrc = 0 ;

/* PS file object for the current debug log target. */

static OBJECT irrc_dbg_file = { 0 } ;

#define IRRC_DBG_BUF_SIZE 2048

/** Buffer for assembling debug log messages. Always use swncopyf,
    although the size should be plenty. */
static uint8 irrc_dbg_buf[ IRRC_DBG_BUF_SIZE ] ;

/** Open a debug log file. It's the caller's responsibility to deal
    with any currently open log as appropriate. We never close the
    logs: we rely on the PS restore to blow them away. */

static void pdf_irrc_open_debug_log( char *cache_id , char *setup_id )
{
  int32 len ;

  len = swncopyf( irrc_dbg_buf , IRRC_DBG_BUF_SIZE ,
                  ( uint8 * )"%%os%%/IRRC_logs/%s(%s).log",
                  cache_id , setup_id ) ;

  theLen( snewobj )  = CAST_SIZET_TO_UINT16( len ) ;
  oString( snewobj ) = irrc_dbg_buf ;

  if ( ! file_open( & snewobj , SW_WRONLY | SW_CREAT | SW_TRUNC ,
                    WRITE_FLAG , FALSE , FALSE , & irrc_dbg_file )) {
    HQFAIL( "Failed to open debug log" ) ;
  }
}

/** Macro to output debug information which boils away to nothing in
    non-debug builds. */

#define IRRC_DBG( _switch , _args )                             \
  MACRO_START                                                   \
  if (( debug_irrc & DEBUG_IRRC_##_switch ) != 0 ) {            \
    pdf_irrc_debug_log _args ;                                  \
  }                                                             \
  MACRO_END

#define IRRC_DBG_OPEN( _switch , _cache_id , _setup_id )        \
  MACRO_START                                                   \
  if (( debug_irrc & DEBUG_IRRC_##_switch ) != 0 ) {            \
    pdf_irrc_open_debug_log(( char * )_cache_id ,               \
                            ( char * )_setup_id  ) ;            \
  }                                                             \
  MACRO_END

/** Write debug logging to the current output target. */

static void pdf_irrc_debug_log( uint8 *format , ... )
{
  int32 len ;
  Bool written ;
  va_list argp ;

  va_start( argp , format ) ;

  len = vswncopyf( irrc_dbg_buf , IRRC_DBG_BUF_SIZE , format , argp ) ;

  va_end( argp ) ;

  /* If this wasn't debug-only code it'd be revolting. As it is, it's
     mildy icky. */
  HQASSERT( irrc_dbg_file._d1.transfer != 0xdeadbeef ,
            "Debug log restored away?" ) ;

  written = file_write( oFile( irrc_dbg_file ) , irrc_dbg_buf , len ) ;

  HQASSERT( written , "Debug log write failed!" ) ;
}

/** Write just the node hash to the log file. Prefix for most log
    messages. */

static void pdf_irrc_debug_nodehash( RBT_NODE *node )
{
  uint8 *hash = ( uint8 * )rbt_get_node_key( node ) ;

  pdf_irrc_debug_log(( uint8 * )"Node "
                     "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                     "%02x%02x%02x%02x%02x%02x%02x%02x " ,
                     hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
                     hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
                     hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
                     hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ]) ;
}

#define IRRC_DBG_NODE( _switch , _node )                        \
  MACRO_START                                                   \
  if (( debug_irrc & DEBUG_IRRC_##_switch ) != 0 ) {            \
    pdf_irrc_debug_nodehash( _node ) ;                          \
  }                                                             \
  MACRO_END

#else
#define IRRC_DBG( _switch , _args ) EMPTY_STATEMENT()
#define IRRC_DBG_NODE( _switch , _node ) EMPTY_STATEMENT()
#define IRRC_DBG_OPEN( _switch , _cache_id , _setup_id ) EMPTY_STATEMENT()
#endif

/** The internal retained raster cache instance. */

typedef struct IRRC {
  /** Singly-linked list */
  struct IRRC *next ;

  /** Setup ID */
  uint8 setup_id[ RR_SETUP_ID_LENGTH ] ;

  /** Memory pool for the cache. */
  mm_pool_t mm_pool ;

  /** Base of the raster element cache tree. */
  RBT_ROOT *cache_tree ;
} IRRC ;

/** Number of event handlers which accompany an IRR cache
    connection. */

#define IRRC_HANDLER_COUNT 8

/** Structure holding the data for a connection between the RIP and an
    IRR cache instance. */

typedef struct IRRC_CONNECTION {
  /** The cache instance to which this connection points. */
  IRRC *irrc ;

  /** The internal mode of operation for the connection. */
  enum {
    IRRC_MODE_CACHE ,
    IRRC_MODE_DUMB0 ,
    IRRC_MODE_DUMB1 ,
    IRRC_MODE_CACHEONLY ,
    IRRC_MODE_VARONLY
  } mode ;

  /** Event handlers table. There's one of these per connection
      because each carries a context pointer which links back to the
      connection when the handlers are called. */
  sw_event_handlers handlers[ IRRC_HANDLER_COUNT ] ;
} IRRC_CONNECTION ;

/** A node in the Internal Retained Raster cache. */

typedef struct {
  enum {
    NODE_NEW = 0, /** New node. */
    NODE_QUERIED, /** RIP has queried node (for IRRC_MODE_DUMB[123]
                      debug mode). */
    NODE_PENDING, /** RIP has started creating the raster for this node. */
    NODE_FILLED   /** RIP has delivered the raster for this node. */
  } stage ;

  /** The raster data associated with this node. Starts off NULL. Note
      that this is an opaque pointer: the definition of irr_store_t is
      not available to the code, but it is handy sometimes to be able
      to have a look around the values in the debugger. */
  irr_store_t *raster ;

  /** The size of the cached raster in bytes. */
  size_t size ;

  /** The peak hit count for this node. */
  uint32 hits_peak ;

  /** Count of how many hits remain for this node. */
  uint32 hits_remaining ;

  /** Bounding box for this node in device coordinates. */
  dbbox_t bbox ;
}
IRRC_NODE ;

/** Base of the linked list of all known cache instances. */

static IRRC *irrc_base = NULL ;

/** Mutex and condvar for waiting for rasters to turn up. There can only
    be one QUERY at a time, so there's no point having one of these per
    node... though multiple UPDATE_RASTER events may occur while waiting
    for a specific raster. Waking for the wrong raster is no problem though. */

static multi_mutex_t mutex ;
static multi_condvar_t condvar ;

/** Just a placeholder: neither IRR nor the debug modes actually do
    anything with the page definitions yet. */

static sw_event_result HQNCALL pdf_irrc_page_define( void *context ,
                                                     sw_event *evt )
{
  SWMSG_RR_PAGE_DEFINE *msg = evt->message ;
  IRRC_CONNECTION *connection = ( IRRC_CONNECTION * )context ;

  if ( msg == NULL || evt->length < sizeof( *msg ) ||
       msg->connection != connection ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  return SW_EVENT_HANDLED ;
}

/** The RIP says that the given page is ready: for the dumb debug
    modes this is enough for us to tell the RR code the page is
    complete. For IRR proper we have to wait for rendering to
    complete. */

static sw_event_result HQNCALL pdf_irrc_page_ready( void *context ,
                                                    sw_event *evt )
{
  SWMSG_RR_PAGE_REF *msg = evt->message ;
  IRRC_CONNECTION *connection = ( IRRC_CONNECTION * )context ;

  if ( msg == NULL || evt->length < sizeof( *msg ) ||
       msg->connection != connection ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  if ( connection->mode != IRRC_MODE_CACHE ) {
    /* In the dumb debug modes we can just use the same event message
       to signal the page is immediately complete. */
    return SwEvent( SWEVT_RR_PAGE_COMPLETE , msg , sizeof( *msg )) ;
  }

  return SW_EVENT_HANDLED ;
}

/** Event handler for Raster Element Define events. */

static sw_event_result HQNCALL pdf_irrc_element_define( void *context ,
                                                        sw_event *evt )
{
  SWMSG_RR_ELEMENT_DEFINE *msg = evt->message ;
  IRRC_CONNECTION *connection = ( IRRC_CONNECTION * )context ;
  IRRC *irrc = connection->irrc ;
  RBT_NODE *node ;

  if ( msg == NULL || evt->length < sizeof( *msg ) ||
       msg->connection != connection ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  HQASSERT( irrc->cache_tree != NULL ,
            "IRR cache tree should exist by now. " ) ;

  IRRC_DBG( SEQUENCE_POINTS ,
            (( uint8 * )"Define "
             "0x%02x%02x%02x%02x%02x%02x%02x%02x"
             "%02x%02x%02x%02x%02x%02x%02x%02x [%d,%d,%d,%d] - " ,
             msg->id[ 0 ] , msg->id[ 1 ] , msg->id[ 2 ] , msg->id[ 3 ] ,
             msg->id[ 4 ] , msg->id[ 5 ] , msg->id[ 6 ] , msg->id[ 7 ] ,
             msg->id[ 8 ] , msg->id[ 9 ] , msg->id[ 10 ] , msg->id[ 11 ] ,
             msg->id[ 12 ] , msg->id[ 13 ] , msg->id[ 14 ] , msg->id[ 15 ] ,
             msg->x1 , msg->y1 , msg->x2 , msg->y2 )) ;

  node = rbt_search( irrc->cache_tree , ( uintptr_t )msg->id ) ;

  if ( node == NULL ) {
    IRRC_NODE cn ;

    cn.stage = NODE_NEW ;
    cn.raster = NULL ;
    cn.size = 0 ;
    cn.hits_peak = 0 ;
    cn.hits_remaining = 0 ;
    cn.bbox.x1 = msg->x1 ;
    cn.bbox.y1 = msg->y1 ;
    cn.bbox.x2 = msg->x2 ;
    cn.bbox.y2 = msg->y2 ;

    node = rbt_allocate_node( irrc->cache_tree , ( uintptr_t )msg->id , & cn ) ;

    if ( node == NULL ) {
      ( void )error_handler( VMERROR ) ;
      return SW_EVENT_ERROR ;
    }

    IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"node created\n" )) ;

    rbt_insert( irrc->cache_tree , node ) ;
  }
  else {
#if defined( ASSERT_BUILD )
    IRRC_NODE *cn = rbt_get_node_data( node ) ;

    HQASSERT( cn->bbox.x1 == msg->x1 && cn->bbox.y1 == msg->y1 &&
              cn->bbox.x2 == msg->x2 && cn->bbox.y2 == msg->y2 ,
              "Bounding box of extant node doesn't match." ) ;
#endif

    IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"node extant\n" )) ;
  }

  return SW_EVENT_HANDLED ;
}

/** Event handler for Raster Element Pending events. */

static sw_event_result HQNCALL pdf_irrc_element_pending( void *context ,
                                                         sw_event *evt )
{
  SWMSG_RR_ELEMENT_REF *msg = evt->message ;
  IRRC_CONNECTION *connection = ( IRRC_CONNECTION * )context ;
  IRRC *irrc = connection->irrc ;
  RBT_NODE *node ;

  if ( msg == NULL || evt->length < sizeof( *msg ) ||
       msg->connection != connection ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  HQASSERT( irrc->cache_tree != NULL ,
            "IRR cache tree should exist by now. " ) ;

  node = rbt_search( irrc->cache_tree , ( uintptr_t )msg->id ) ;

  IRRC_DBG( SEQUENCE_POINTS ,
            (( uint8 * )"Pending "
             "0x%02x%02x%02x%02x%02x%02x%02x%02x"
             "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
             msg->id[ 0 ] , msg->id[ 1 ] , msg->id[ 2 ] , msg->id[ 3 ] ,
             msg->id[ 4 ] , msg->id[ 5 ] , msg->id[ 6 ] , msg->id[ 7 ] ,
             msg->id[ 8 ] , msg->id[ 9 ] , msg->id[ 10 ] , msg->id[ 11 ] ,
             msg->id[ 12 ] , msg->id[ 13 ] , msg->id[ 14 ] , msg->id[ 15 ])) ;

  if ( node != NULL ) {
    IRRC_NODE *cn = rbt_get_node_data( node ) ;

    HQASSERT( cn->stage < NODE_PENDING , "Node already pending") ;
    if ( cn->stage < NODE_PENDING ) {
      cn->stage = NODE_PENDING ;
    }
    return SW_EVENT_HANDLED ;
  }

  return SW_EVENT_CONTINUE ;
}

/** Event handler for Raster Element Query events. */

static sw_event_result HQNCALL pdf_irrc_element_query( void *context ,
                                                       sw_event *evt )
{
  SWMSG_RR_ELEMENT_QUERY *msg = evt->message ;
  IRRC_CONNECTION *connection = ( IRRC_CONNECTION * )context ;
  IRRC *irrc = connection->irrc ;
  RBT_NODE *node ;

  if ( msg == NULL || evt->length < sizeof( *msg ) ||
       msg->connection != connection ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  HQASSERT( irrc->cache_tree != NULL ,
            "IRR cache tree should exist by now. " ) ;

  node = rbt_search( irrc->cache_tree , ( uintptr_t )msg->id ) ;

  IRRC_DBG( SEQUENCE_POINTS ,
            (( uint8 * )"Query "
             "0x%02x%02x%02x%02x%02x%02x%02x%02x"
             "%02x%02x%02x%02x%02x%02x%02x%02x - " ,
             msg->id[ 0 ] , msg->id[ 1 ] , msg->id[ 2 ] , msg->id[ 3 ] ,
             msg->id[ 4 ] , msg->id[ 5 ] , msg->id[ 6 ] , msg->id[ 7 ] ,
             msg->id[ 8 ] , msg->id[ 9 ] , msg->id[ 10 ] , msg->id[ 11 ] ,
             msg->id[ 12 ] , msg->id[ 13 ] , msg->id[ 14 ] , msg->id[ 15 ])) ;

  if ( node != NULL ) {
    IRRC_NODE *cn = rbt_get_node_data( node ) ;
    Bool ok = ( cn->stage == NODE_FILLED ) ;

    if ( ! ok ) {
      if (( connection->mode == IRRC_MODE_DUMB0 ) ||
          ( cn->stage > NODE_NEW &&
            ( connection->mode == IRRC_MODE_DUMB1 ||
              connection->mode == IRRC_MODE_CACHEONLY ||
              connection->mode == IRRC_MODE_VARONLY )) ||
          ( connection->mode == IRRC_MODE_CACHEONLY &&
            cn->hits_remaining <= 1 ) ||
          ( connection->mode == IRRC_MODE_VARONLY &&
            cn->hits_remaining > 1 )) {
        /* We've been promised an update raster event for this node or
           we wish to skip the rendering of it for debugging
           purposes. Note that it's perfectly valid for cn->raster to
           be NULL here. That just means that a mark or series of
           marks in the content stream lead to an empty raster for
           whatever reason: the object(s) could be off the page or
           clipped out, for instance. It's very valuable to cache the
           fact that we can skip these marks next time we see them
           without affecting the output at all. */
        ok = TRUE ;
      } else {
        if ( cn->stage == NODE_PENDING ) {
          Bool interrupted = FALSE ;
          DL_STATE *page = CoreContext.page ;

          /** Wait for raster to turn up */
          multi_mutex_lock( & mutex ) ;
          while ( cn->stage == NODE_PENDING ) {
            HqU32x2 when ;

            /* Note. As we're not locking a mutex around EVERY write, it's
               possible for stage to change after the if and before the wait.
               However, we do lock around the change to FILLED, we don't expect
               to go to any other stage from PENDING *and* we do a short wait
               anyway to be on the safe side. */
            HqU32x2FromUint32( & when , 1000000 ) ;
            get_time_from_now( & when ) ;
            ( void )multi_condvar_timedwait( & condvar , when ) ;
            if ( ! interrupts_clear( allow_interrupt )) {
              interrupted = TRUE ;
              ( void )report_interrupt( allow_interrupt ) ;
              break ;
            }
            if ( page->job == NULL ||
                 task_group_is_cancelled(page->job->task_group) ) {
              ( void )error_handler(task_group_error(page->job->task_group)) ;
              interrupted = TRUE ;
              break ;
            }
          }
          multi_mutex_unlock( & mutex ) ;
          ok = ! interrupted ;
          if ( interrupted )
            return SW_EVENT_ERROR ;
        }
      }
    }

    if ( ok ) {
      if ( cn->stage == NODE_NEW ) {
        cn->stage = NODE_QUERIED ;
      }
      IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"returning %p\n" , cn->raster )) ;
      msg->handle = ( uintptr_t )cn->raster ;
      return SW_EVENT_HANDLED ;
    }
    else {
      IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"empty\n" )) ;
      /* We haven't had a raster update for this one yet. */
    }
  }
  else {
    IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"unknown\n" )) ;
  }

  return SW_EVENT_CONTINUE ;
}

/** Event handler for Raster Element Update Raster events. */

static sw_event_result HQNCALL pdf_irrc_element_update_raster( void *context ,
                                                               sw_event *evt )
{
  SWMSG_RR_ELEMENT_UPDATE_RASTER *msg = evt->message ;
  IRRC_CONNECTION *connection = ( IRRC_CONNECTION * )context ;
  IRRC *irrc = connection->irrc ;
  RBT_NODE *node ;
  IRRC_NODE *cn ;

  if ( msg == NULL || evt->length < sizeof( *msg ) ||
       msg->connection != connection ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  HQASSERT( irrc->cache_tree != NULL ,
            "IRR cache tree should exist by now. " ) ;

  node = rbt_search( irrc->cache_tree , ( uintptr_t )msg->id ) ;

  if ( node == NULL ) {
    HQFAIL( "Node should always be present here - "
            "what happened to the raster define?" ) ;
    return FAILURE( SW_EVENT_ERROR ) ;
  }

  cn = rbt_get_node_data( node ) ;

  IRRC_DBG_NODE( SEQUENCE_POINTS , node ) ;
  IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"raster %p, size %u->" ,
                               cn->raster , cn->size )) ;

  HQASSERT( cn->stage == NODE_PENDING , "UPDATE_RASTER when not pending" ) ;
  HQASSERT( cn->raster == NULL , "Already had a RASTER" ) ;

  cn->raster = ( struct irr_store_t * )msg->raster ;
  cn->size = msg->size ;

  /* Change to FILLED. Note that we lock and signal on this write, whereas
     other stage changes aren't critical. In any case, the wait on this is
     a short timed wait for safety. */
  multi_mutex_lock( & mutex ) ;
  cn->stage = NODE_FILLED ;
  multi_condvar_signal( & condvar ) ;
  multi_mutex_unlock( & mutex) ;

  IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"raster %p, size %u\n" ,
                               cn->raster , cn->size )) ;

  return SW_EVENT_HANDLED ;
}

/** Event handler for Raster Element Update Hits events. */

static sw_event_result HQNCALL pdf_irrc_element_update_hits( void *context ,
                                                             sw_event *evt )
{
  SWMSG_RR_ELEMENT_UPDATE_HITS *msg = evt->message ;
  IRRC_CONNECTION *connection = ( IRRC_CONNECTION * )context ;
  IRRC *irrc = connection->irrc ;
  RBT_NODE *node ;
  IRRC_NODE *cn ;

  if ( msg == NULL || evt->length < sizeof( *msg ) ||
       msg->connection != connection ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  node = rbt_search( irrc->cache_tree , ( uintptr_t )msg->id ) ;

  HQASSERT( node != NULL , "Node should always be present here." ) ;

  cn = rbt_get_node_data( node ) ;

  IRRC_DBG_NODE( SEQUENCE_POINTS , node ) ;
  IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"hit count %u->" ,
                               cn->hits_remaining )) ;

  if ( msg->raise ) {
    cn->hits_remaining += msg->hits_delta ;
    if ( cn->hits_remaining > cn->hits_peak ) {
      cn->hits_peak = cn->hits_remaining ;
    }
  }
  else {
    HQASSERT( cn->hits_remaining >= msg->hits_delta ,
              "Hit count underflow" ) ;
    cn->hits_remaining -= msg->hits_delta ;
  }

  IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"%u (peak %u)\n" ,
                               cn->hits_remaining , cn->hits_peak )) ;

#undef IRRC_EAGER_FREE
#if defined( IRRC_EAGER_FREE )
  /* Here's where we could eagerly free any cached element when its
     hit count reaches zero. Currently we hold on to them until
     disconnect time (this means, for instance, that caching across
     multiple pdfexecid chunks works). We can reconsider this if/when
     we get a low memory handler for iHVD. */

  if ( cn->hits_remaining == 0 ) {
    irr_store_free( & cn->raster ) ;
  }
#endif

  return SW_EVENT_HANDLED ;
}

/** Free the irr store belonging to the given node (safe if it's
    NULL). */

static Bool pdf_irrc_destroy_node( RBT_ROOT *root , RBT_NODE *node ,
                                   void *walk_data )
{
  IRRC_NODE *cn = rbt_get_node_data( node ) ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;
  UNUSED_PARAM( void * , walk_data ) ;

  irr_store_free( & cn->raster ) ;

  return TRUE ;
}

/** Tear down the given cache instance. Safe if it's partially
    constructed. */

static void pdf_irrc_destroy_instance( IRRC *irrc )
{
  HQASSERT( irrc != NULL , "Null cache instance" ) ;

  if ( irrc->cache_tree != NULL ) {
    ( void )rbt_walk( irrc->cache_tree , pdf_irrc_destroy_node , NULL ) ;

    rbt_dispose( irrc->cache_tree ) ;
  }

  /* This used to free all the stored irr data for this
     connection. Now it's a backstop: provided the hit counts are
     consistent then pdf_irrc_free_cn will have done the work before
     we get here. */

  mm_pool_destroy( irrc->mm_pool ) ;
  mm_free( mm_pool_temp , irrc , sizeof( *irrc )) ;
}

/** Event handler for disconnect events. */

static sw_event_result HQNCALL pdf_irrc_disconnect( void *context ,
                                                    sw_event *evt )
{
  SWMSG_RR_DISCONNECT *msg = evt->message ;
  IRRC_CONNECTION *connection = ( IRRC_CONNECTION * )context ;
  IRRC *irrc = connection->irrc ;

  if ( msg == NULL || evt->length < sizeof( *msg ) ||
       msg->connection != connection ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  /* There's a special case here: if the setup ID is the empty string,
     then we destroy the cache instance as soon as it's
     disconnected. In other words, you only get inter-job caching when
     you set setup_id to something meaningful (and unique). */

  if ( irrc->setup_id[ 0 ] == 0 ) {
    /* Remove it from the linked list. */

    if ( irrc_base == irrc ) {
      irrc_base = irrc->next ;
    }
    else {
      IRRC *base = irrc_base ;

      while ( base->next != irrc ) {
        base = base->next ;
        if ( base == NULL ) {
          HQFAIL( "Cache instance not found!" ) ;
          return SW_EVENT_ERROR ;
        }
      }

      HQASSERT( base->next == irrc ,
                "Should be looking at our cache instance by now" ) ;

      base->next = irrc->next ;
    }

    pdf_irrc_destroy_instance( irrc ) ;
  }

  /* SwDeregisterHandlers cannot be used to deregister ourself */
  ( void )SwDeregisterHandlers( connection->handlers + 1 ,
                                IRRC_HANDLER_COUNT - 1 ) ;
  ( void )SwDeregisterHandler( SWEVT_RR_DISCONNECT ,
                               ( sw_event_handler * )connection->handlers ) ;

  mm_free( mm_pool_temp , connection , sizeof( *connection )) ;

  return SW_EVENT_HANDLED ;
}

/** Allocation function for the IRR cache. */

/*@null@*/ /*@out@*/ /*@only@*/
static mm_addr_t pdf_irrc_alloc( mm_size_t size , /*@null@*/ void *data )
/*@ensures MaxSet(result) == (size - 1); @*/
{
  IRRC *irrc = ( IRRC * )data ;
  mm_addr_t *result ;

  result = mm_alloc( irrc->mm_pool , size , MM_ALLOC_CLASS_PDF_IRRC ) ;

  if ( result == NULL ) {
    ( void )error_handler( VMERROR ) ;
  }

  return result ;
}

/** Free function for the IRR cache. */

static void pdf_irrc_free( /*@out@*/ /*@only@*/ mm_addr_t what ,
                           mm_size_t size , /*@null@*/ void *data )
{
  IRRC *irrc = ( IRRC * )data ;

  mm_free( irrc->mm_pool , what , size ) ;
}

/** Compare two hashes (used as a callback in building red-black trees
    keyed on them). Same as strcmp and qsort callbacks, returns < 0 if
    key1 < key2 etc. */

static int32 pdf_irrc_compare( RBT_ROOT *root , uintptr_t key1 ,
                               uintptr_t key2 )
{
  const uint32 *hash1 = ( const uint32 * )key1 ;
  const uint32 *hash2 = ( const uint32 * )key2 ;

  UNUSED_PARAM( struct rbt_root * , root ) ;

  if ( hash1[ 0 ] < hash2[ 0 ] ) {
    return -1 ;
  }
  else if ( hash1[ 0 ] > hash2[ 0 ] ) {
    return 1 ;
  }

  if ( hash1[ 1 ] < hash2[ 1 ] ) {
    return -1 ;
  }
  else if ( hash1[ 1 ] > hash2[ 1 ] ) {
    return 1 ;
  }

  if ( hash1[ 2 ] < hash2[ 2 ] ) {
    return -1 ;
  }
  else if ( hash1[ 2 ] > hash2[ 2 ] ) {
    return 1 ;
  }

  if ( hash1[ 3 ] < hash2[ 3 ] ) {
    return -1 ;
  }
  else if ( hash1[ 3 ] > hash2[ 3 ] ) {
    return 1 ;
  }

  return 0 ;
}

/** The number of different cache IDs we know about. */

#define NUM_IDS 5

/** Event handler for connect events. */

static sw_event_result HQNCALL pdf_irrc_connect( void *context ,
                                                 sw_event *evt )
{
  sw_event_result result = SW_EVENT_ERROR ;
  SWMSG_RR_CONNECT *msg = evt->message ;
  IRRC_CONNECTION *new_connection = NULL ;
  IRRC *new_irrc = NULL ;
  IRRC *irrc = NULL ;
  unsigned int i ;
  uint8 myids[ NUM_IDS ][ 12 ] = { "GGIRR" , "GGDUMB0" , "GGDUMB1" ,
                                   "GGCACHEONLY"  , "GGVARONLY" } ;

  UNUSED_PARAM( void * , context ) ;

  if ( msg == NULL || evt->length < sizeof(SWMSG_RR_CONNECT) ) {
    return SW_EVENT_CONTINUE ;
  }

  for ( i = 0 ; i < NUM_IDS ; i++ ) {
    if ( HqMemCmp( msg->cache_id , strlen_int32(( char * )msg->cache_id ) ,
                   myids[ i ] , strlen_int32(( char * )myids[ i ])) == 0 ) {
      break ;
    }
  }

  if ( i == NUM_IDS ) {
    /* Not an IRRC connection. */
    return SW_EVENT_CONTINUE ;
  }

  new_connection = mm_alloc( mm_pool_temp , sizeof( *new_connection ) ,
                             MM_ALLOC_CLASS_PDF_IRRC ) ;

  if ( new_connection == NULL ) {
    ( void )error_handler( VMERROR ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }

  switch ( i ) {
    case 0:
      new_connection->mode = IRRC_MODE_CACHE ;
      break ;
    case 1:
      new_connection->mode = IRRC_MODE_DUMB0 ;
      break ;
    case 2:
      new_connection->mode = IRRC_MODE_DUMB1 ;
      break ;
    case 3:
      new_connection->mode = IRRC_MODE_CACHEONLY ;
      break ;
    case 4:
      new_connection->mode = IRRC_MODE_VARONLY ;
      break ;
    default:
      HQFAIL( "Unexpected value" ) ;
  }

  if ( irrc_base != NULL ) {
    for ( irrc = irrc_base ; irrc != NULL ; irrc = irrc->next ) {
      if ( HqMemCmp(( const uint8 * )msg->setup_id ,
                    strlen_int32(( char * )msg->setup_id ) ,
                    ( const uint8 * )irrc->setup_id ,
                    strlen_int32(( char * )irrc->setup_id )) != 0 ) {
        break ;
      }
    }
  }

  msg->connection = new_connection ;

  if ( irrc == NULL ) {
    new_irrc = mm_alloc( mm_pool_temp , sizeof( *new_irrc ) ,
                         MM_ALLOC_CLASS_PDF_IRRC ) ;

    if ( new_irrc == NULL ) {
      ( void )error_handler( VMERROR ) ;
      FAILURE_GOTO( CLEANUP ) ;
    }

    new_irrc->next = NULL ;
    ( void )swncopyf( new_irrc->setup_id , 256 , ( uint8 * )"%s" ,
                      msg->setup_id ) ;
    new_irrc->mm_pool = NULL ;
    new_irrc->cache_tree = NULL ;

    if ( mm_pool_create( & new_irrc->mm_pool,
                         IRR_POOL_TYPE, PDF_POOL_PARAMS ) != MM_SUCCESS ) {
      ( void )error_handler( VMERROR ) ;
      FAILURE_GOTO( CLEANUP ) ;
    }

    new_irrc->cache_tree = rbt_init( new_irrc , pdf_irrc_alloc ,
                                     pdf_irrc_free , pdf_irrc_compare ,
                                     MD5_OUTPUT_LEN , sizeof( IRRC_NODE )) ;

    if ( new_irrc->cache_tree == NULL ) {
      FAILURE_GOTO( CLEANUP ) ;
    }

    new_irrc->next = irrc_base ;
    irrc_base = new_irrc ;

    new_connection->irrc = new_irrc ;

    IRRC_DBG_OPEN( SEQUENCE_POINTS , msg->cache_id , msg->setup_id ) ;
    IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"Cache instance created\n" )) ;
  }
  else {
    IRRC_DBG( SEQUENCE_POINTS , (( uint8 * )"Cache instance selected\n" )) ;
    new_connection->irrc = irrc ;
  }

  { /* Initialise Handlers */
    static sw_event_handlers init[ IRRC_HANDLER_COUNT ] = {
      /* Disconnect must be first in this array - see pdf_irrc_disconnect() */
      { pdf_irrc_disconnect ,            0 , 0 , SWEVT_RR_DISCONNECT } ,
      { pdf_irrc_page_define ,           0 , 0 , SWEVT_RR_PAGE_DEFINE } ,
      { pdf_irrc_page_ready ,            0 , 0 , SWEVT_RR_PAGE_READY } ,
      { pdf_irrc_element_define ,        0 , 0 , SWEVT_RR_ELEMENT_DEFINE } ,
      { pdf_irrc_element_query ,         0 , 0 , SWEVT_RR_ELEMENT_QUERY } ,
      { pdf_irrc_element_pending ,       0 , 0 , SWEVT_RR_ELEMENT_PENDING } ,
      { pdf_irrc_element_update_raster , 0 , 0 , SWEVT_RR_ELEMENT_UPDATE_RASTER } ,
      { pdf_irrc_element_update_hits ,   0 , 0 , SWEVT_RR_ELEMENT_UPDATE_HITS } ,
    } ;

    for ( i = 0 ; i < IRRC_HANDLER_COUNT ; i++ ) {
      new_connection->handlers[ i ] = init[ i ] ;
      new_connection->handlers[ i ].context = new_connection ;
    }
  }

  if ( SwRegisterHandlers( new_connection->handlers , IRRC_HANDLER_COUNT ) !=
       SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }

  result = SW_EVENT_HANDLED ;

 CLEANUP:

  if ( result != SW_EVENT_HANDLED ) {
    if ( new_connection != NULL ) {
      mm_free( mm_pool_temp , new_connection , sizeof( *new_connection )) ;
    }

    if ( new_irrc != NULL ) {
      pdf_irrc_destroy_instance( new_irrc ) ;
    }
  }

  return result ;
}

/** Accessor method to get the mm pool from a connection handle. One
    of the very few ways that the RIP talks to the internal cache
    instance other than via events - done this way so that it doesn't
    pollute the externally visible interface. */

mm_pool_t pdf_irrc_get_pool( void *connection )
{
  return (( IRRC_CONNECTION * )connection )->irrc->mm_pool ;
}

/** Event handler table for the constantly-available handlers. */

static sw_event_handlers connection_handlers[] = {
  { pdf_irrc_connect, NULL, 0, SWEVT_RR_CONNECT, SW_EVENT_NORMAL }
} ;

/** Main initialisation. */

Bool pdf_irrc_init( void )
{
#if defined( DEBUG_BUILD )
  register_ripvar( NAME_debug_irrc , OINTEGER , & debug_irrc ) ;
  register_ripvar( NAME_tweak_irrc , OINTEGER , & tweak_irrc ) ;
#endif

  /* Initialise mutex & condvar. We reuse the RR metrics because we're cheap */
  multi_mutex_init( & mutex , RETAINEDRASTER_LOCK_INDEX , FALSE ,
                    SW_TRACE_RETAINEDRASTER_ACQUIRE ,
                    SW_TRACE_RETAINEDRASTER_HOLD ) ;
  multi_condvar_init( & condvar , & mutex ,
                      SW_TRACE_RETAINEDRASTER_WAIT ) ;

  /* Register the base handlers. */

  return ( SwRegisterHandlers( connection_handlers ,
                               NUM_ARRAY_ITEMS( connection_handlers )) ==
           SW_RDR_SUCCESS ) ;
}

/** Tear-down. */

void pdf_irrc_finish( void )
{
  ( void )SwDeregisterHandlers( connection_handlers ,
                                NUM_ARRAY_ITEMS( connection_handlers )) ;

  multi_condvar_finish( & condvar ) ;
  multi_mutex_finish( & mutex ) ;
}

/* Log stripped */
