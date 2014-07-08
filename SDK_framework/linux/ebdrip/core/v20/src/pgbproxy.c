/** \file
 * \ingroup otherdevs
 *
 * $HopeName: SWv20!src:pgbproxy.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2000-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of %pgbproxy% device.
 *
 * The PostScript interpreter can address the pgb device directly via calls
 * of the type "(%pagebuffer%) << /NumCopies 1 >> setdevparams". Read/Write
 * access to the PGB is also required during rendering. This is a problem
 * with multi-page pipelining, as two different pages (one interpreting and
 * one rendering) may be attempting to address the same PGB at the same time.
 *
 * In order to solve this problem, the pgbproxy is introduced as a filter
 * between the core code and the actual PGB device. It is repsonsible for
 * multiplexing requests from different clients and ensuring they only get
 * passed to the real PGB serialised by instance.
 *
 * The pgbproxy device is not instantiated as other devices, instead it steals
 * the %pagebuffer% slot in the device table, intercepting all calls to the
 * real device. It saves up and manages all set_param/get_param calls during
 * interpretation, and only flushes them through to the real device during
 * the rendering phase. This is sufficient, as it is guaranteed that only one
 * page will be in the rendering phase at one time. The switch from saving to
 * flushing mode is triggered by the open_file and close_file calls on the PGB.
 * All other device calls are just passed directly from the proxy to the
 * underlying real PGB device.
 *
 * The queued-up params are stored per DL page being created. This can be
 * identified via the active DL_STATE pointer, i.e. get_core_context()->page.
 */

#include "core.h"
#include "namedef_.h"
#include "hqmemcmp.h"
#include "swdevice.h"
#include "swerrors.h"
#include "devices.h"
#include "devs.h"
#include "swcopyf.h"
#include "devices.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "monitor.h"
#include "ripdebug.h"
#include "dlstate.h"
#include "debugging.h"
#include "objects.h" /* OINTEGER */
#include "objnamer.h"

/** Device type for the per-page PGB proxy. */
#define PGBPROXY_DEVICE_TYPE 36

/** Device type for the generic PGB proxy. The generic proxy calls to the
    context's page proxy. */
#define PGBINTERCEPT_DEVICE_TYPE 37


/**
 * List of device params saved up for sending later to the real PGB
 */
typedef struct PGBPROXY_LIST {
  DEVICEPARAM param;
  struct PGBPROXY_LIST *next;
} PGBPROXY_LIST;


#define PGB_INTERCEPT_NAME "PGB Intercept"

/** Parameters for the pgb intercept device. */
typedef struct pgb_intercept_t {
  DEVICELIST realpgb ;    /**< The original %pagebuffer% device. */
  int refcount ;          /**< How many intercepts are set up? */
  OBJECT_NAME_MEMBER
} pgb_intercept_t ;


#define PGB_PROXY_NAME "PGB Proxy"

/** Parameters for each page's proxy device. */
typedef struct pgb_proxy_t {
  Bool flushing;          /**< Send direct to the real PGB device? */
  PGBPROXY_LIST *params;  /**< Head of parameter list for this page. */
  PGBPROXY_LIST **tail;   /**< Tail of parameter list for this page. */
  mm_pool_t pool;         /**< Pool to allocate this page's params from. */
  pgb_intercept_t *intercept ; /**< Intercept details. */
  OBJECT_NAME_MEMBER
} pgb_proxy_t ;


#if defined(DEBUG_BUILD)
enum {
  DEBUG_PROXY_CALLS = 1,
  DEBUG_PROXY_GETPARAM = 2,
  DEBUG_PROXY_SETPARAM = 4
} ;

static int32 proxy_dbg;


/**
 * Debug routine to print out pgbproxy activity
 */
static void pgbproxy_dbg(char *mess)
{
  if ( (proxy_dbg & DEBUG_PROXY_CALLS) != 0 ) {
    monitorf((uint8 *)"PGB:%s\n", mess);
  }
}


static void param_dbg(char *mess, DEVICEPARAM *param, int32 reason)
{
  char buffer[1000] ;

  buffer[0] = '\0' ;
  if ( (proxy_dbg & DEBUG_PROXY_CALLS) != 0 )
    swcopyf((uint8 *)buffer, (uint8 *)"PGB:%s ", mess) ;

  if ( (proxy_dbg & reason) != 0 )
    debug_print_deviceparam_indented(param, buffer, "\n") ;
  else if ( (proxy_dbg & DEBUG_PROXY_CALLS) != 0 )
    monitorf((uint8 *)"%s\n", buffer);
}


void pgbproxy_debug_init(void)
{
  register_ripvar(NAME_debug_pgbproxy, OINTEGER, &proxy_dbg) ;
}
#else /* !DEBUG_BUILD */
#define pgbproxy_dbg(msg) EMPTY_STATEMENT()
#define param_dbg(msg, param, reason) EMPTY_STATEMENT()
#endif /* !DEBUG_BUILD */


/**
 * Free a local copy of a DEVICEPARAM made by the pgbproxy code.
 */
static void free_devparam(mm_pool_t pool, DEVICEPARAM *dp)
{
  int32 i;

  if ( dp ) {
    if ( dp->paramname != NULL ) {
      mm_free(pool, (mm_addr_t)dp->paramname, dp->paramnamelen);
      dp->paramname = NULL;
    }
    dp->paramnamelen = 0;
    switch ( dp->type ) {
    case ParamString:
      if ( dp->paramval.strval ) {
        mm_free(pool, (mm_addr_t)dp->paramval.strval, dp->strvallen);
        dp->paramval.strval = NULL;
      }
      dp->strvallen = 0;
      break;
    case ParamDict:
    case ParamArray:
      if ( dp->paramval.compobval ) {
        for ( i = 0; i < dp->strvallen; i++ )
          free_devparam(pool, &(dp->paramval.compobval[i]));
        mm_free(pool, (mm_addr_t)dp->paramval.compobval,
                dp->strvallen * sizeof(DEVICEPARAM));
      }
      dp->paramval.compobval = NULL;
      dp->strvallen = 0;
      break;
    default:
      break;
    }
    dp->type = ParamNull;
  }
}

/**
 * Free all of the deviceparams that the proxy has saved up.
 */
static void free_savedparams(pgb_proxy_t *proxy)
{
  PGBPROXY_LIST *p ;

  VERIFY_OBJECT(proxy, PGB_PROXY_NAME) ;
  p = proxy->params;
  while ( p ) {
    PGBPROXY_LIST *next = p->next ;
    free_devparam(proxy->pool, &(p->param));
    mm_free(proxy->pool, (mm_addr_t)p, sizeof(PGBPROXY_LIST));
    p = next;
  }
  proxy->params = NULL;
  proxy->tail = &proxy->params ;
}


static DL_STATE *flushing_page = NULL;


Bool pgbproxy_setflush(DL_STATE *page, Bool flush)
{
  DEVICELIST *dev = page->pgbdev ;
  Bool ok = TRUE ;

  if ( dev != NULL &&
       theIDevTypeNumber(theIDevType(dev)) == PGBPROXY_DEVICE_TYPE ) {
    pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;

    VERIFY_OBJECT(proxy, PGB_PROXY_NAME) ;
    HQASSERT(proxy->flushing ? flushing_page == page
             : !flush || flushing_page == NULL,
             "Multiple pagedevs flushing");

    /* Flush the accumulated queue now. But always set the flushing state
       first, because we may choose to ignore the saved param return
       state. */
    if ( flush != proxy->flushing ) {
      DEVICELIST *pgb ;
      PGBPROXY_LIST *p ;

#if defined(DEBUG_BUILD)
      if ( (proxy_dbg & DEBUG_PROXY_CALLS) != 0 )
        monitorf((uint8 *)"PGB:setflush(%d)\n", flush);
#endif

      proxy->flushing = flush ;
      flushing_page = flush ? page : NULL;

      VERIFY_OBJECT(proxy->intercept, PGB_INTERCEPT_NAME) ;
      pgb = &proxy->intercept->realpgb ;
      HQASSERT(flush || proxy->params == NULL, "Flushing stale params");
      for ( p = proxy->params ; p != NULL ; p = p->next ) {
        int32 sp_err ;

        param_dbg("flush_param", &p->param, DEBUG_PROXY_SETPARAM) ;

        sp_err = (*theISetParam(pgb))(pgb, &p->param) ;
        switch ( sp_err ) {
        case ParamError:
          devices_set_last_error((*theILastErr(pgb))(pgb)) ;
          ok = FALSE ;
          break ;
        case ParamTypeCheck:
        case ParamRangeCheck:
        case ParamConfigError:
          devices_set_last_error(sp_err) ;
          ok = FALSE ;
          break ;
        }
      }

      free_savedparams(proxy) ;
    }
  }

  return ok ;
}


void pgbproxy_reset(DL_STATE *page)
{
  DEVICELIST *dev = page->pgbdev ;

  /* May be called from setpagedevice before proxy is initialised, or after
     finalised, so be cautious about existence of the params. */
  if ( dev != NULL &&
       theIDevTypeNumber(theIDevType(dev)) == PGBPROXY_DEVICE_TYPE ) {
    pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
    PGBPROXY_LIST **pp, *p, *pagebuffertype = NULL ;

    VERIFY_OBJECT(proxy, PGB_PROXY_NAME) ;

    pgbproxy_dbg("reset");

    /* This is called when resetting the page device. The /resetpagedevice
       procedure in pagedev.pss sends /PageBufferType to the PGB device
       immediately before the pagedevice_() call. We should retain the last
       PageBufferType parameter from the list, so we unlink it here, then
       re-link it afterwards. */
    pp = &proxy->params ;
    while ( (p = *pp) != NULL ) {
      if ( HqMemCmp(p->param.paramname, p->param.paramnamelen,
                    STRING_AND_LENGTH("PageBufferType")) == 0 ) {
        /* Remove from the list */
        *pp = p->next ;
        p->next = NULL ;

        if ( pagebuffertype != NULL ) {
          /* If we've already found /PageBufferType, free it, we'll replace
             it with the new one. */
          free_devparam(proxy->pool, &pagebuffertype->param);
          mm_free(proxy->pool, (mm_addr_t)pagebuffertype, sizeof(PGBPROXY_LIST));
        }

        pagebuffertype = p ;
      } else {
        pp = &p->next ;
      }
    }
    free_savedparams(proxy) ;

    if ( pagebuffertype != NULL ) { /* Re-insert last PageBufferType */
      proxy->params = pagebuffertype ;
      proxy->tail = &pagebuffertype->next ;
    }
  }
}

/*---------------------------------------------------------------------------*/
/**
 * Return the real underlying PGB plus some universal asserts on
 * various parameters.
 */
static inline DEVICELIST *real_pgb(pgb_proxy_t *proxy)
{
  VERIFY_OBJECT(proxy, PGB_PROXY_NAME) ;
  VERIFY_OBJECT(proxy->intercept, PGB_INTERCEPT_NAME) ;
  HQASSERT(proxy->intercept->refcount > 0, "Intercept not live") ;

  return &proxy->intercept->realpgb;
}

/**
 * If there is an error, cache the LastErr return value so that we can
 * cache it and correctly manage the pgbproxy last_error API.
 */
static int32 save_lasterr(pgb_proxy_t *proxy, int32 err)
{
  VERIFY_OBJECT(proxy, PGB_PROXY_NAME) ;

  if ( err < 0 ) {
    DEVICELIST *pgb ;
    VERIFY_OBJECT(proxy->intercept, PGB_INTERCEPT_NAME) ;
    pgb = &proxy->intercept->realpgb ;
    devices_set_last_error((*theILastErr(pgb))(pgb));
  } else
    devices_set_last_error(DeviceNoError);

  return err;
}


static int32 RIPCALL pgbproxy_status_file(DEVICELIST *dev, uint8 *filename,
                                          STAT *statbuff)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("status_file");
  return save_lasterr(proxy, (*theIStatusFile(pgb))(pgb, filename, statbuff));
}


static void *RIPCALL pgbproxy_start_list(DEVICELIST *dev, uint8 *pattern)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("start_list");
  return (*theIStartList(pgb))(pgb, pattern);
}


static int32 RIPCALL pgbproxy_next_list(DEVICELIST *dev, void **handle,
                                        uint8 *pattern, FILEENTRY *entry)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("next_list");
  return save_lasterr(proxy, (*theINextList(pgb))(pgb, handle, pattern, entry));
}


static int32 RIPCALL pgbproxy_end_list(DEVICELIST *dev, void *handle)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("end_list");
  return save_lasterr(proxy, (*theIEndList(pgb))(pgb, handle));
}


static int32 RIPCALL pgbproxy_rename_file(DEVICELIST *dev, uint8 *file1,
                                          uint8 *file2)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("rename_file");
  return save_lasterr(proxy, (*theIRenameFile(pgb))(pgb, file1, file2));
}

/**
 * Init the pgbproxy instance.
 */
static int32 RIPCALL pgbproxy_init_device(DEVICELIST *dev)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  pgb_proxy_t init = { 0 } ;

  /* Real initialisation happens in pgbproxy_init, because the intercept
     reference is not known here. */
  *proxy = init ;
  pgbproxy_dbg("init_device");

  return 0;
}


static int32 RIPCALL pgbproxy_read_file(DEVICELIST *dev,
                                        DEVICE_FILEDESCRIPTOR descriptor,
                                        uint8 * buff, int32 len)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("read_file");
  return save_lasterr(proxy, (*theIReadFile(pgb))(pgb, descriptor, buff, len));
}


static int32 RIPCALL pgbproxy_write_file(DEVICELIST *dev,
                                         DEVICE_FILEDESCRIPTOR descriptor,
                                         uint8 * buff, int32 len)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("write_file");
  return save_lasterr(proxy, (*theIWriteFile(pgb))(pgb, descriptor, buff, len));
}


static int32 RIPCALL pgbproxy_close_file(DEVICELIST *dev,
                                         DEVICE_FILEDESCRIPTOR descriptor)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("close_file");
  return save_lasterr(proxy, (*theICloseFile(pgb))(pgb, descriptor));
}


static int32 RIPCALL pgbproxy_abort_file(DEVICELIST *dev,
                                         DEVICE_FILEDESCRIPTOR descriptor)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("abort_file");
  return save_lasterr(proxy, (*theIAbortFile(pgb))(pgb, descriptor));
}


static int32 RIPCALL pgbproxy_seek_file(DEVICELIST *dev,
                                        DEVICE_FILEDESCRIPTOR descriptor,
                                        Hq32x2 * destn, int32 flags)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("seek_file");
  if ( !(*theISeekFile(pgb))(pgb, descriptor, destn, flags) ) {
    devices_set_last_error((*theILastErr(pgb))(pgb));
    return FALSE ;
  }
  return TRUE;
}


static int32 RIPCALL pgbproxy_bytes_file(DEVICELIST *dev,
                                         DEVICE_FILEDESCRIPTOR descriptor,
                                         Hq32x2 * bytes, int32 rn)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("bytes_file");
  if ( !(*theIBytesFile(pgb))(pgb, descriptor, bytes, rn) ) {
    devices_set_last_error((*theILastErr(pgb))(pgb));
    return FALSE ;
  }
  return TRUE ;
}


static int32 RIPCALL pgbproxy_delete_file(DEVICELIST * dev, uint8 * filename)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("delete_file");
  return save_lasterr(proxy, (*theIDeleteFile(pgb))(pgb, filename));
}


/**
 * Duplicated the given deviceparam, allocating memory as required.
 */
static Bool dup_devparam(DEVICEPARAM *src, DEVICEPARAM *dst,
                         mm_pool_t pool)
{
  int32 i;

  if ( src->paramnamelen > 0 ) {
    if ( (dst->paramname = mm_alloc(pool, src->paramnamelen,
                                    MM_ALLOC_CLASS_DEVICE_PRIVATE)) == NULL )
      return FALSE;
    HqMemCpy(dst->paramname, src->paramname, src->paramnamelen);
  } else
    dst->paramname = NULL;
  dst->paramnamelen = src->paramnamelen;

  dst->type = src->type;
  switch ( src->type ) {
  case ParamInteger:
    dst->paramval.intval = src->paramval.intval;
    break;
  case ParamBoolean:
    dst->paramval.boolval = src->paramval.boolval;
    break;
  case ParamFloat:
    dst->paramval.floatval = src->paramval.floatval;
    break;
  case ParamNull:
    break;
  case ParamString:
    if ( src->strvallen > 0 ) {
      if ( (dst->paramval.strval = mm_alloc(pool, src->strvallen,
                                            MM_ALLOC_CLASS_DEVICE_PRIVATE)) == NULL )
        return FALSE;
      HqMemCpy(dst->paramval.strval, src->paramval.strval, src->strvallen);
    } else
      dst->paramval.strval = NULL;
    dst->strvallen = src->strvallen;
    break;
  case ParamArray:
  case ParamDict:
    if ( src->strvallen > 0 ) {
      if ( (dst->paramval.compobval = mm_alloc(pool, src->strvallen *
                                               sizeof(DEVICEPARAM), MM_ALLOC_CLASS_DEVICE_PRIVATE)) == NULL )
        return FALSE;
      dst->strvallen = src->strvallen;
      for ( i = 0; i < src->strvallen; i++ ) {
        dst->paramval.compobval[i].type = ParamNull;
        dst->paramval.compobval[i].paramname = NULL;
      }
      for ( i = 0; i < src->strvallen; i++ ) {
        if ( !dup_devparam(&(src->paramval.compobval[i]),
                           &(dst->paramval.compobval[i]), pool) ) {
          return FALSE;
        }
      }
    } else {
      dst->strvallen = 0;
      dst->paramval.compobval = NULL;
    }
    break;
  }
  return TRUE;
}

/**
 * Saves-up parameters during interpretation, and then streams them all through
 * to the real PGB when rendering starts.
 */
static int32 RIPCALL pgbproxy_set_param(DEVICELIST *dev, DEVICEPARAM *param)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  if ( proxy->flushing ) {
    param_dbg("set_param", param, DEBUG_PROXY_SETPARAM) ;
    return save_lasterr(proxy, (*theISetParam(pgb))(pgb, param));
  } else {
    PGBPROXY_LIST *dup;

    param_dbg("save_param", param, DEBUG_PROXY_SETPARAM);

    if ( (dup = mm_alloc(proxy->pool, sizeof(PGBPROXY_LIST),
                         MM_ALLOC_CLASS_DEVICE_PRIVATE)) == NULL ) {
      devices_set_last_error(ParamConfigError);
      return ParamConfigError;
    }

    dup->param.paramname = NULL;
    dup->param.type = ParamNull;
    dup->next = NULL;
    if ( !dup_devparam(param, &(dup->param), proxy->pool) ) {
      free_devparam(proxy->pool, &(dup->param));
      mm_free(proxy->pool, (mm_addr_t)dup, sizeof(PGBPROXY_LIST));
      devices_set_last_error(ParamConfigError);
      return ParamConfigError;
    }
    *(proxy->tail) = dup ;
    proxy->tail = &dup->next ;
  }

  devices_set_last_error(DeviceNoError);
  return ParamAccepted;
}


static int32 RIPCALL pgbproxy_start_param(DEVICELIST *dev)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("start_param");
  HQASSERT(proxy->flushing, "Iterating over the wrong params");

  return save_lasterr(proxy, (*theIStartParam(pgb))(pgb));
}


static int32 RIPCALL pgbproxy_get_param(DEVICELIST *dev, DEVICEPARAM *param)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;
  int32 result = save_lasterr(proxy, (*theIGetParam(pgb))(pgb, param));

  /* HQASSERT(proxy->flushing, "Getting the wrong param value"); */
  param_dbg("get_param", param, DEBUG_PROXY_GETPARAM);

  return result ;
}

/**
 * pgbproxy open_file call.
 *
 * Flush all saved params through then call the underlying open_file.
 * Turn off param saving mechanism while the pgb is open.
 */
static DEVICE_FILEDESCRIPTOR RIPCALL pgbproxy_open_file(DEVICELIST *dev,
                                                        uint8 * filename,
                                                        int32 openflags)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;
  DEVICE_FILEDESCRIPTOR fd;

  pgbproxy_dbg("open_file");

  devices_set_last_error(DeviceNoError);
  fd = (*theIOpenFile(pgb))(pgb, filename, openflags);
  if (fd < 0) {
    devices_set_last_error(theILastErr(pgb)(pgb));
  }
  return (fd);
}


static int32 RIPCALL pgbproxy_status_device(DEVICELIST *dev, DEVSTAT *devstat)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("status_device");
  return save_lasterr(proxy, (*theIStatusDevice(pgb))(pgb, devstat));
}


static int32 RIPCALL pgbproxy_dismount(DEVICELIST *dev)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;

  pgbproxy_dbg("dismount");

  /* Real destruction happens in pgbproxy_finish(). Just clean up the saved
     parameters here. */
  free_savedparams(proxy);

  return 0;
}


static int32 RIPCALL pgbproxy_buffer_size(DEVICELIST * dev)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

  pgbproxy_dbg("buffer_size");
  return (*theIGetBuffSize(pgb))(pgb);
}


static int32 RIPCALL pgbproxy_ioctl(DEVICELIST * dev,
                                    DEVICE_FILEDESCRIPTOR fileDescriptor,
                                    int32 opcode, intptr_t arg)
{
  pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
  DEVICELIST *pgb = real_pgb(proxy) ;

#if defined(DEBUG_BUILD)
  if ( (proxy_dbg & DEBUG_PROXY_CALLS) != 0 )
    monitorf((uint8 *)"PGB:ioctl(%d)\n", opcode);
#endif

  return save_lasterr(proxy, (*theIIoctl(pgb))(pgb, fileDescriptor, opcode, arg));
}


static int32 RIPCALL pgbproxy_spare(void)
{
  pgbproxy_dbg("spare");
  devices_set_last_error(DeviceIOError);
  return -1;
}


static DEVICETYPE Pgbproxy_Device_Type = {
  PGBPROXY_DEVICE_TYPE,           /* the device ID number */
  DEVICEWRITABLE,                 /* flags to indicate specifics of device */
  sizeof(pgb_proxy_t),            /* the size of the private data */
  0,                              /* minimum ticks between tickle functions */
  NULL,                           /* procedure to service the device */
  devices_last_error,             /* return last error for this device */
  pgbproxy_init_device,           /* call to initialise device */
  pgbproxy_open_file,             /* call to open file on device */
  pgbproxy_read_file,             /* call to read data from file on device */
  pgbproxy_write_file,            /* call to write data to file on device */
  pgbproxy_close_file,            /* call to close file on device */
  pgbproxy_abort_file,            /* call to abort action on the device */
  pgbproxy_seek_file,             /* call to seek file on device */
  pgbproxy_bytes_file,            /* call to get bytes avail on an open file */
  pgbproxy_status_file,           /* call to check status of file */
  pgbproxy_start_list,            /* call to start listing files */
  pgbproxy_next_list,             /* call to get next file in list */
  pgbproxy_end_list,              /* call to end listing */
  pgbproxy_rename_file,           /* rename file on the device */
  pgbproxy_delete_file,           /* remove file from device */
  pgbproxy_set_param,             /* call to set device parameter */
  pgbproxy_start_param,           /* call to start getting device parameters */
  pgbproxy_get_param,             /* call to get the next device parameter */
  pgbproxy_status_device,         /* call to get the status of the device */
  pgbproxy_dismount,              /* call to dismount the device */
  pgbproxy_buffer_size,           /* Call to determine buffer size */
  pgbproxy_ioctl,                 /* call to pass ioctl */
  pgbproxy_spare                  /* spare slots */
};

/*---------------------------------------------------------------------------*/
/* %pagebuffer% generic interception device. This interception device exists
   for proxied calls made by the PostScript interpreter, or directly through
   the %pagebuffer% device. The intercept device forwards calls to the current
   context's page proxy. */

static inline DEVICELIST *page_proxy(void)
{
  corecontext_t *context = get_core_context() ;
  return context->page->pgbdev ;
}

static int32 RIPCALL intercept_last_error(DEVICELIST *dev)
{
  dev = page_proxy() ;
  return (*theILastErr(dev))(dev);
}

static int32 RIPCALL intercept_status_file(DEVICELIST *dev, uint8 *filename,
                                           STAT *statbuff)
{
  dev = page_proxy();
  return (*theIStatusFile(dev))(dev, filename, statbuff);
}

static void *RIPCALL intercept_start_list(DEVICELIST *dev, uint8 *pattern)
{
  dev = page_proxy();
  return (*theIStartList(dev))(dev, pattern);
}

static int32 RIPCALL intercept_next_list(DEVICELIST *dev, void **handle,
                                         uint8 *pattern, FILEENTRY *entry)
{
  dev = page_proxy();
  return (*theINextList(dev))(dev, handle, pattern, entry);
}

static int32 RIPCALL intercept_end_list(DEVICELIST *dev, void *handle)
{
  dev = page_proxy();
  return (*theIEndList(dev))(dev, handle);
}

static int32 RIPCALL intercept_rename_file(DEVICELIST *dev, uint8 *file1,
                                           uint8 *file2)
{
  dev = page_proxy();
  return (*theIRenameFile(dev))(dev, file1, file2);
}

static int32 RIPCALL intercept_init_device(DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICELIST *, dev) ;
  return 0;
}

static int32 RIPCALL intercept_read_file(DEVICELIST *dev,
                                         DEVICE_FILEDESCRIPTOR descriptor,
                                         uint8 * buff, int32 len)
{
  dev = page_proxy();
  return (*theIReadFile(dev))(dev, descriptor, buff, len);
}

static int32 RIPCALL intercept_write_file(DEVICELIST *dev,
                                          DEVICE_FILEDESCRIPTOR descriptor,
                                          uint8 * buff, int32 len)
{
  dev = page_proxy();
  return (*theIWriteFile(dev))(dev, descriptor, buff, len);
}

static int32 RIPCALL intercept_close_file(DEVICELIST *dev,
                                          DEVICE_FILEDESCRIPTOR descriptor)
{
  dev = page_proxy();
  return (*theICloseFile(dev))(dev, descriptor);
}

static int32 RIPCALL intercept_abort_file(DEVICELIST *dev,
                                          DEVICE_FILEDESCRIPTOR descriptor)
{
  dev = page_proxy();
  return (*theIAbortFile(dev))(dev, descriptor);
}

static int32 RIPCALL intercept_seek_file(DEVICELIST *dev,
                                         DEVICE_FILEDESCRIPTOR descriptor,
                                         Hq32x2 * destn, int32 flags)
{
  dev = page_proxy();
  return (*theISeekFile(dev))(dev, descriptor, destn, flags) ;
}

static int32 RIPCALL intercept_bytes_file(DEVICELIST *dev,
                                          DEVICE_FILEDESCRIPTOR descriptor,
                                          Hq32x2 * bytes, int32 rn)
{
  dev = page_proxy();
  return (*theIBytesFile(dev))(dev, descriptor, bytes, rn) ;
}

static int32 RIPCALL intercept_delete_file(DEVICELIST * dev, uint8 * filename)
{
  dev = page_proxy();
  return (*theIDeleteFile(dev))(dev, filename);
}

static int32 RIPCALL intercept_set_param(DEVICELIST *dev, DEVICEPARAM *param)
{
  dev = page_proxy();
  return (*theISetParam(dev))(dev, param);
}

static int32 RIPCALL intercept_start_param(DEVICELIST *dev)
{
  dev = page_proxy();
  return (*theIStartParam(dev))(dev);
}

static int32 RIPCALL intercept_get_param(DEVICELIST *dev, DEVICEPARAM *param)
{
  dev = page_proxy();
  return (*theIGetParam(dev))(dev, param);
}

static DEVICE_FILEDESCRIPTOR RIPCALL intercept_open_file(DEVICELIST *dev,
                                                         uint8 * filename,
                                                         int32 openflags)
{
  dev = page_proxy();
  return (*theIOpenFile(dev))(dev, filename, openflags);
}


static int32 RIPCALL intercept_status_device(DEVICELIST *dev, DEVSTAT *devstat)
{
  dev = page_proxy();
  return (*theIStatusDevice(dev))(dev, devstat);
}

static int32 RIPCALL intercept_dismount(DEVICELIST *dev)
{
  dev = page_proxy();
  return (*theIDevDismount(dev))(dev) ;
}

static int32 RIPCALL intercept_buffer_size(DEVICELIST * dev)
{
  dev = page_proxy();
  return (*theIGetBuffSize(dev))(dev);
}

static int32 RIPCALL intercept_ioctl(DEVICELIST * dev,
                                     DEVICE_FILEDESCRIPTOR fileDescriptor,
                                     int32 opcode, intptr_t arg)
{
  dev = page_proxy();
  return (*theIIoctl(dev))(dev, fileDescriptor, opcode, arg);
}

static int32 RIPCALL intercept_spare(void)
{
  pgbproxy_dbg("spare");
  return -1;
}


static DEVICETYPE Intercept_Device_Type = {
  PGBINTERCEPT_DEVICE_TYPE,           /* the device ID number */
  DEVICEWRITABLE,                 /* flags to indicate specifics of device */
  sizeof(pgb_intercept_t),        /* the size of the private data */
  0,                              /* minimum ticks between tickle functions */
  NULL,                           /* procedure to service the device */
  intercept_last_error,           /* return last error for this device */
  intercept_init_device,          /* call to initialise device */
  intercept_open_file,            /* call to open file on device */
  intercept_read_file,            /* call to read data from file on device */
  intercept_write_file,           /* call to write data to file on device */
  intercept_close_file,           /* call to close file on device */
  intercept_abort_file,           /* call to abort action on the device */
  intercept_seek_file,            /* call to seek file on device */
  intercept_bytes_file,           /* call to get bytes avail on an open file */
  intercept_status_file,          /* call to check status of file */
  intercept_start_list,           /* call to start listing files */
  intercept_next_list,            /* call to get next file in list */
  intercept_end_list,             /* call to end listing */
  intercept_rename_file,          /* rename file on the device */
  intercept_delete_file,          /* remove file from device */
  intercept_set_param,            /* call to set device parameter */
  intercept_start_param,          /* call to start getting device parameters */
  intercept_get_param,            /* call to get the next device parameter */
  intercept_status_device,        /* call to get the status of the device */
  intercept_dismount,             /* call to dismount the device */
  intercept_buffer_size,          /* Call to determine buffer size */
  intercept_ioctl,                /* call to pass ioctl */
  intercept_spare                 /* spare slots */
};

/*---------------------------------------------------------------------------*/

static pgb_intercept_t *pgb_intercept_acquire(void)
{
  DEVICELIST *pagebuffer ;
  pgb_intercept_t *intercept ;

  /* By now, the pagebuffer device should be mounted. */
  if ( (pagebuffer = find_device((uint8 *)"pagebuffer")) == NULL ||
       pagebuffer->devicetype == NULL ) {
    return error_handler(CONFIGURATIONERROR), NULL ;
  }

  if ( pagebuffer->devicetype->devicenumber != PGBINTERCEPT_DEVICE_TYPE ) {
    DEVICELIST dev ;

    HQASSERT(pagebuffer->devicetype->devicenumber == 4 /*HSR PAGEBUFFER_DEVICE_TYPE*/ ||
             pagebuffer->devicetype->devicenumber == 0xffff0003 /*HHR PAGEBUFFER_DEVICE_TYPE*/,
             "Not intercepting a pagebuffer device that we know about");

    /* This is the first interception. Steal the DEVICELIST from the real
       device, because it's already mounted as %pagebuffer%, and other
       components may have references to it. Re-direct it at a new intercept
       device. Mark the intercepted pagebuffer as undismountable, until all
       of the proxies are removed from it. */

    /* But first, some first-time-only housekeeping. */
    if ( !device_type_add(&Pgbproxy_Device_Type) ||
         !device_type_add(&Intercept_Device_Type)) {
      return NULL;
    }

    /* The intercept device struct is a stack local as it only needs to exist
       for the duration of this call, do not want it mounted or to appear
       explicitly in the list of devices. But connect it with the same flags
       as the target originally had, which will get the private data
       allocated and the init routine called. */
    if ( !device_connect(&dev, PGBINTERCEPT_DEVICE_TYPE, "intercept",
                         pagebuffer->flags, TRUE) )
      return NULL;

    intercept = (pgb_intercept_t *)dev.private_data;
    intercept->realpgb = *pagebuffer ;
    /* Abuse the next reference to point to the %pagebuffer% mount. */
    intercept->realpgb.next = pagebuffer ;
    intercept->refcount = 1 ;
    NAME_OBJECT(intercept, PGB_INTERCEPT_NAME) ;

    /* Patch the pagebuffer mount with the intercept structs. */
    pagebuffer->devicetype   = dev.devicetype;
    pagebuffer->private_data = dev.private_data;

    /* Can't dismount %pagebuffer% until all proxy references removed. */
    SetUndismountableDevice(pagebuffer) ;
  } else {
    intercept = (pgb_intercept_t *)pagebuffer->private_data ;
    VERIFY_OBJECT(intercept, PGB_INTERCEPT_NAME) ;
    ++intercept->refcount ;
  }

  return intercept ;
}


static void pgb_intercept_release(pgb_intercept_t *intercept)
{
  VERIFY_OBJECT(intercept, PGB_INTERCEPT_NAME) ;
  if ( --intercept->refcount == 0 ) {
    /* We abused the next reference to point to the %pagebuffer% mount. */
    DEVICELIST *pagebuffer = intercept->realpgb.next ;

    /* Restore the original device type and private data. */
    pagebuffer->devicetype = intercept->realpgb.devicetype ;
    pagebuffer->private_data = intercept->realpgb.private_data ;

    /* We can now dismount the real %pagebuffer%. */
    ClearUndismountableDevice(pagebuffer) ;

    /* Free the intercept's private data. This is the converse of the
       device_connect() used when setting up the interception, which allocates
       the private data. We can't use device_free() because it's only
       applicable to heap mounted devices, and we don't want to free the
       underlying device because we don't own it. */
    UNNAME_OBJECT(intercept) ;
    mm_free(mm_pool_temp, intercept, sizeof(pgb_intercept_t)) ;
  }
}


Bool pgbproxy_init(DL_STATE *page, mm_pool_t pool)
{
  DEVICELIST *dev ;
  pgb_proxy_t *proxy ;
  pgb_intercept_t *intercept ;

  if ( (intercept = pgb_intercept_acquire()) == NULL )
    return FALSE ;

  if ( (dev = device_alloc(STRING_AND_LENGTH("pgbproxy"))) == NULL ) {
    pgb_intercept_release(intercept) ;
    return FALSE ;
  }

  if ( !device_connect(dev, PGBPROXY_DEVICE_TYPE, (char *)theIDevName(dev),
                       intercept->realpgb.flags, TRUE) ) {
    device_free(dev) ;
    pgb_intercept_release(intercept) ;
    return FALSE ;
  }

  proxy = (pgb_proxy_t *)dev->private_data ;
  proxy->flushing = FALSE ;
  proxy->params = NULL ;
  proxy->tail = &proxy->params ;
  proxy->pool = pool;
  proxy->intercept = intercept ;
  NAME_OBJECT(proxy, PGB_PROXY_NAME) ;

  devices_set_last_error(DeviceNoError);

  page->pgbdev = dev ;

  return TRUE ;
}


void pgbproxy_finish(DL_STATE *page)
{
  DEVICELIST *dev ;

  HQASSERT(page != NULL, "No page for proxy params") ;
  dev = page->pgbdev ;
  if ( dev != NULL &&
       theIDevTypeNumber(theIDevType(dev)) == PGBPROXY_DEVICE_TYPE ) {
    pgb_proxy_t *proxy = (pgb_proxy_t *)dev->private_data ;
    (void)(*theIDevDismount(dev))(dev) ;
    pgb_intercept_release(proxy->intercept) ;
    UNNAME_OBJECT(proxy) ;
    device_free(page->pgbdev) ;
    page->pgbdev = NULL ;
  }
}


void init_C_globals_pgbproxy(void)
{
#ifdef DEBUG_BUILD
  proxy_dbg = 0 ;
#endif
  flushing_page = NULL;
}

/*
* Log stripped */
