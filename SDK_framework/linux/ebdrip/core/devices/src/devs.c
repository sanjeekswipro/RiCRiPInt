/** \file
 * \ingroup devices
 *
 * $HopeName: COREdevices!src:devs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Device initialisation and storage routines.
 */

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swoften.h"
#include "swstart.h"
#include "swctype.h"
#include "coreinit.h"
#include "monitor.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "dictscan.h"
#include "coreparams.h"
#include "namedef_.h"
#include "debugging.h"
#include "devices.h"        /* find_device_type */
#include "devparam.h"       /* DEVICESPARAMS */
#include "nulldev.h"        /* Null_Device_Type */
#include "absdev.h"         /* Abs_Device_Type */
#include "reldev.h"         /* Rel_Device_Type */
#include "threadapi.h"
#include "devs.h"

/* Device are held in a simple list, allocated through device_type_add */
typedef struct devicetypelist {
  DEVICETYPE *devicetype ;
  struct devicetypelist *next ;
} DEVICETYPELIST ;

/*
 * TLS key for last error.  One last_error TLS value is used for all the core
 * devices.  This works because the device protocol is that last_error() must be
 * called immediately on failure.
 */
static pthread_key_t devices_key;

/*
 * The osdevice is the device on which all working directories are made.
 */
DEVICELIST *osdevice = NULL, *nulldevice = NULL, *progressdev = NULL;

/*
 * Global list of devices. The type 0 and 1 are initialised to the os
 * device and null device respectively; subsequent ones are mounted by PS.
 */
static DEVICELIST *device_list = NULL ;

/* A list of device types, constructed from the device types passed in from
   SwStart and any standard device types added by the Core RIP. */
static DEVICETYPELIST *device_types = NULL, **device_types_tail = NULL ;

/* Number of devices supporting tickles */
static int32 n_tickle_devices = 0 ;

static Bool devices_set_systemparam(corecontext_t *context, uint16 name, OBJECT *theo) ;
static Bool devices_get_systemparam(corecontext_t *context, uint16 name, OBJECT *result) ;

static NAMETYPEMATCH system_match[] = {
  { NAME_ObeySearchDevice | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
  DUMMY_END_MATCH
} ;

static module_params_t devices_system_params = {
  system_match,
  devices_set_systemparam,
  devices_get_systemparam,
  NULL
} ;

static void init_C_globals_devs(void)
{
  osdevice = NULL ;
  nulldevice = NULL ;
  progressdev = NULL ;
  device_list = NULL ;
  device_types = NULL ;
  device_types_tail = &device_types ;
  n_tickle_devices = 0 ;
  devices_system_params.next = NULL ;
}

static Bool devices_swstart(SWSTART *params)
{
  corecontext_t *context = get_core_context() ;
  int32 i ;

  HQASSERT(device_list == NULL, "Device list already present, duplicate init?") ;

  if ( pthread_key_create(&devices_key, NULL) != 0 )
    return FALSE;
  devices_set_last_error(DeviceNoError);

  for (i = 0; params[i].tag != SWEndTag; i++) {
    DEVICETYPE **Device_Type_List ;

    if (params[i].tag == SWDevTypesTag) {
      Device_Type_List = (DEVICETYPE **) (params[i].value.pointer_value);
      if ( Device_Type_List ) {
        while ( *Device_Type_List ) {
          if ( !device_type_add(*Device_Type_List) )
            return FALSE ;
          ++Device_Type_List ;
        }
      }
      /* don't break, we'll allow multiple device type tags. */
    }
  }

  /* Add null device to device list */
  HQASSERT(!find_device_type(NULL_DEVICE_TYPE, TRUE),
           "Null device type already added.") ;
  if (! device_type_add(&Null_Device_Type))
    return FALSE ;
  if (! device_type_add(&Abs_Device_Type))
    return FALSE ;
  if (! device_type_add(&Rel_Device_Type))
    return FALSE ;

  /* Initialise devices configuration parameters */
  if ( (context->devicesparams = mm_alloc_static(sizeof(DEVICESPARAMS))) == NULL )
    return FALSE ;

  context->devicesparams->ObeySearchDevice[0] = TRUE ;  /* Filenameforall obeys search */
  context->devicesparams->ObeySearchDevice[1] = FALSE ; /* No others obey search */

  HQASSERT(devices_system_params.next == NULL,
           "Already linked system params accessor") ;

  /* Link accessors into global list */
  devices_system_params.next = context->systemparamlist ;
  context->systemparamlist = &devices_system_params ;

  /* Set up the null device entry */
  nulldevice = device_mount( (uint8*)"null" ) ;
  if (nulldevice == NULL)
    return FALSE ;
  if ( !device_connect(nulldevice, NULL_DEVICE_TYPE, (char*)theIDevName( nulldevice ),
                       DEVICEUNDISMOUNTABLE, TRUE) )
    return FAILURE(FALSE) ;

  /* Set up the standard OS device. Cannot remove these devices without
     leaving dangling pointers in the Core RIP, so mark them as
     undismountable. */
  osdevice = device_mount( (uint8*)"os" ) ;
  if (osdevice == NULL)
    return FALSE ;

  if ( !device_connect(osdevice, OS_DEVICE_TYPE, (char *)theIDevName( osdevice ),
                       DEVICEUNDISMOUNTABLE|DEVICEENABLED, TRUE) ) {
    return FAILURE(FALSE) ;
  }

  return TRUE ;
}

static void devices_finish(void)
{
  DEVICELIST *dev, *nextdev ;
  int res;

  HQASSERT(osdevice != NULL, "osdevice is NULL") ;
  HQASSERT(nulldevice != NULL, "nulldevice is NULL") ;

  ClearUndismountableDevice(nulldevice) ;
  if (! device_dismount((uint8*)"null")) {
    HQFAIL("Unable to dismount null device.") ;
  }
  ClearUndismountableDevice(osdevice) ;
  if (! device_dismount((uint8*)"os")) {
    HQFAIL("Unable to dismount os device.") ;
  }

  dev = device_list ;

  /* Do our best to dismount all remaining devices. */
  while ( dev != NULL ) {
    nextdev = theINextDev( dev ) ; /* Get next dev before destroying the memory. */
    HQASSERT( theIDevName( dev ) != NULL, "The device has not been initialised yet" ) ;
    ClearUndismountableDevice( dev ) ;
    /* Seems many of devices cause an error when we attempt to
       dismount them. Its not clear whether we should change the
       device code or not. */
#if 0
    if (! device_dismount(theIDevName( dev ))) {
      HQFAIL("Unable to dismount a device.") ;
    }
#else
    (void)device_dismount(theIDevName( dev )) ;
#endif
    dev = nextdev ;
  }
  device_list = NULL ;

  res = pthread_key_delete(devices_key);
  UNUSED_PARAM(int, res);
  HQASSERT(res == 0, "pthread_key_delete failed");
}


/*
 * devices_last_error/devices_set_last_error
 *
 * One last_error TLS value is used for all the core devices.  This works
 * because the device protocol is that last_error() must be called immediately
 * on failure.
 */

int32 RIPCALL devices_last_error(DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICELIST*, dev);
  return CAST_INTPTRT_TO_INT32((intptr_t)pthread_getspecific(devices_key));
}

void devices_set_last_error(int32 error)
{
  int res = pthread_setspecific(devices_key, (void*)((intptr_t)error));
  UNUSED_PARAM(int, res);
  HQASSERT(res == 0, "pthread_setspecific failed");
}


IMPORT_INIT_C_GLOBALS(reldev)

void devices_C_globals(core_init_fns *fns)
{
  init_C_globals_devs() ;
  init_C_globals_reldev() ;

  fns->swstart = devices_swstart ;
  fns->finish = devices_finish ;
}

Bool device_type_add(DEVICETYPE *devtype)
{
  DEVICETYPELIST *entry ;

  if ( (entry = mm_alloc_static(sizeof(DEVICETYPELIST))) == NULL )
    return FALSE ;

  HQASSERT(devtype, "No device type to add") ;
  HQASSERT(device_types_tail, "COREdevices not initialised") ;
  HQASSERT(*device_types_tail == NULL, "Device type list corrupted") ;

  entry->devicetype = devtype ;
  entry->next = NULL ;

  *device_types_tail = entry ;
  device_types_tail = &entry->next ;

  if ( theIDevTypeTickle(devtype) ) /* Device needs tickling */
    ++n_tickle_devices ;

  return TRUE ;
}

/*
 * SwTestAndTickle is part of exported core RIP interface. Do not change !
 */
int32 RIPCALL SwTestAndTickle(void)
{
  return SwOften();
}

void device_type_report_ticklefuncs(void)
{
  DEVICETYPELIST* type_entry = device_types;
  Bool title_done = FALSE;

  while (type_entry != NULL) {
    if (type_entry->devicetype->tickle_proc != NULL) {
      if (!title_done) {
        monitorf((uint8*)"Warning, tickle function exists for devicetype numbers:\n");
        title_done = TRUE;
      }
      monitorf((uint8*)"  %d (0x%08x)\n", type_entry->devicetype->devicenumber,
               type_entry->devicetype->devicenumber);
    }
    type_entry = type_entry->next;
  }
}

/*
 * SwFindDevice is part of exported core RIP interface. Do not change !
 */
DEVICELIST * RIPCALL SwFindDevice( uint8 * deviceName )
{
  return find_device( deviceName );
}

DEVICELIST *find_device( uint8 *name ) /* null terminated name */
{
  register DEVICELIST *dev = device_list ;

  HQASSERT(name, "No device name to find") ;
  /* We allow an empty device list so we can device_mount() the OS and
     null device. */
  if (dev == NULL)
    return NULL ;

  do {
    HQASSERT( theIDevName( dev ) != NULL, "The device has not been initialised yet" ) ;
    if ( theIDevName( dev ) != NULL )
      if ( strcmp( (char *)name , (char *)theIDevName( dev )) == 0 )
        return dev ;
    dev = theINextDev( dev ) ;
  } while ( dev ) ;

  return NULL ;
}

/* Device list iterators */
DEVICELIST *device_first(device_iterator_h iter, int32 flags)
{
  HQASSERT(iter, "Device iterator invalid") ;

  iter->current = device_list ;
  iter->flags = flags ;

  return device_next(iter) ;
}

DEVICELIST *device_next(device_iterator_h iter)
{
  DEVICELIST *device ;

  HQASSERT(iter, "Device iterator invalid") ;

  do {
    device = iter->current ;

    if ( device == NULL ) /* No more devices */
      return NULL ;

    iter->current = theINextDev(device) ;
  } while ( (iter->flags & theIDeviceFlags(device)) != iter->flags ) ;

  return device ;
}

DEVICETYPE *find_device_type(int32 devnum, Bool protected_ok)
{
  register DEVICETYPELIST *dt ;

  /* Do we permit find protected devices? This might be better done through
     a flag in the device type struct, but that would involve core RIP
     interface changes. */
  if ( !protected_ok ) {
    if ( isProtectedDeviceNumber(devnum) )
      return NULL ;
  }

  for (dt = device_types ; dt ; dt = dt->next ) {
    if ( theIDevTypeNumber( dt->devicetype ) == devnum )
      return dt->devicetype ;
  }

  return NULL ;
}

/* Device type iterators */
DEVICETYPE *device_type_first(device_type_iterator_h iter, int32 flags)
{
  HQASSERT(iter, "Device type iterator invalid") ;

  iter->current = device_types ;
  iter->flags = flags ;

  return device_type_next(iter) ;
}

DEVICETYPE *device_type_next(device_type_iterator_h iter)
{
  DEVICETYPE *devtype = NULL ;

  HQASSERT(iter, "Device type iterator invalid") ;

  do {
    if ( iter->current == NULL ) /* No more device types */
      return NULL ;

    devtype = iter->current->devicetype ;
    HQASSERT(devtype, "Missing device type in list entry") ;
    iter->current = iter->current->next ;
  } while ( !iter->flags &&
            isProtectedDeviceNumber(theIDevTypeNumber(devtype)) ) ;

  return devtype ;
}

Bool device_tickles_required(void)
{
  return (n_tickle_devices != 0) ;
}


/** Translate last device error code to standard CORE error code. */
static int32 device_error_translate(DEVICELIST *dev)
{
  HQASSERT(dev, "No device to get last error from") ;

  switch ( (*theILastErr( dev ))( dev ) ) {
  case DeviceNoError :
    return FALSE;
  case DeviceInterrupted:
    return INTERRUPT ;
  case DeviceIOError :
    return IOERROR ;
  case DeviceInvalidAccess :
    return INVALIDACCESS ;
  case DeviceLimitCheck :
    return LIMITCHECK ;
  case DeviceUndefined :
    return UNDEFINEDFILENAME ;
  case DeviceTimeout:
    return TIMEOUT ;
  case DeviceVMError:
    return VMERROR ;
  case DeviceUnregistered :
    break ;
  default:
    HQFAIL("Unrecognised device error code") ;
  }

  /* Failsafe: all unknown errors return UNREGISTERED */
  return UNREGISTERED ;
}


Bool device_error_handler(DEVICELIST *dev)
{
  int32 deverror = device_error_translate(dev) ;

  /* Return TRUE if no error; devices can return DeviceNoError in
     filenameforall */
  if ( !deverror )
    return TRUE ;

  return error_handler(deverror) ;
}


/* Allocate a device structure */
DEVICELIST *device_alloc(uint8 *device_name, size_t len)
{
  DEVICELIST *dev ;

  HQASSERT(device_name, "No name for new device") ;
  HQASSERT(len > 0, "Invalid name length for new device") ;

  /* create new device - cannot use PS VM because device mounts are not
   * undone on a restore
   */
  dev = (DEVICELIST *) mm_alloc_with_header(mm_pool_temp,
                                            sizeof(DEVICELIST) + len + 1,
                                            MM_ALLOC_CLASS_DEVICELIST);

  if (dev != NULL) {
    register uint8 *str = (uint8 *)(dev + 1) ;

    (void)strncpy((char *)str, (char *)device_name, len);
    str[len] = (uint8)'\0';
    theIDevName( dev ) = str;
    theIDeviceFlags( dev ) = 0;
    theIDevType( dev ) = NULL;
    theIPrivate( dev ) = NULL;
    theINextDev( dev ) = NULL;

    return dev;
  }

  (void) error_handler(VMERROR);
  return NULL ;
}

/* Free a device structure */
void device_free(DEVICELIST *dev)
{
  HQASSERT(dev, "No device list to free") ;

  if ( theIPrivate(dev) != NULL ) {
    DEVICETYPE *devtype = theIDevType(dev) ;

    HQASSERT(devtype != NULL,
             "Should have device type if private data allocated") ;
    HQASSERT(theIDevTypePrivSize(devtype) > 0,
             "Device type private size should be positive if private data allocated") ;
    mm_free(mm_pool_temp, theIPrivate(dev), theIDevTypePrivSize(devtype));
  }

  /*
  HQASSERT(theIDevName(dev) == (uint8 *)(dev + 1),
           "Freeing device name memory incorrectly") ;
  */
  mm_free_with_header(mm_pool_temp, dev) ;
}

/* Central C core of devmount operator without PostScript interpreter wrapper,
 * also used by rel_open_device() to create underling devices.
 */
DEVICELIST *device_mount( uint8 *device_name )
{
  DEVICELIST     *dev;

  HQASSERT(device_name, "No device name to mount") ;

  if ( (dev = find_device( device_name )) != NULL ) /* Don't duplicate mount */
    return dev ;

  /* create new device - cannot use PS VM because device mounts are not
   * undone on a restore
   */
  if ( NULL != (dev = device_alloc(device_name, strlen((char*)device_name))) ) {
    /* Add new device to device list */
    device_add(dev) ;
    return dev;
  }

  return NULL;
}


/* Central C core of dismount operator without PostScript interpreter wrapper,
 * also used by 'relative device'.
 */
Bool device_dismount(uint8 *device_name)
{
  DEVICELIST     *dev, **dp;

  HQASSERT(device_name, "No device to dismount") ;

  if ( NULL == ( dev = find_device( device_name )))
    return error_handler( UNDEFINEDFILENAME ) ;

  if ( isDeviceUndismountable(dev) )
    return error_handler( INVALIDACCESS );

  /* go through all the open files checking if there are any files
   * open on this device
   */
  if ( !device_closing(dev) )
    return FALSE ;

  /*  call the device dismount routine */
  if ( theIDevType( dev ))
    if ( (*theIDevDismount( dev ))( dev ) == -1)
      /* Not safe to call device_error_handler after dismount failure. */
      return error_handler(IOERROR);

  dp = &device_list ;
  while ( *dp != dev )
    dp = &theINextDev( *dp );
  HQASSERT(*dp != NULL, "Device not found in list while dismounting") ;
  *dp = theINextDev( dev );

  device_free(dev) ;

  return TRUE;
}

/* Fill in fields in devicelist structure for device type specified. If it
   fails, it sets up the devicelist so the error so it can be retrieved with
   theILastErr() or signalled with device_error_handler(). On error, a null
   device will be connected with an appropriate error set. */
Bool device_connect(DEVICELIST *dev, int32 devnum, char *name, int32 flags,
                    Bool protected_ok)
{
  DEVICETYPE *devtype ;

  HQASSERT(dev, "No devicelist to initialise") ;
  HQASSERT(name, "No device name") ;

  theIDevName( dev ) = (uint8 *)name ;
  theIDeviceFlags( dev ) = DEVICENOSEARCH ; /* Not enabled or searchable */
  theIPrivate( dev ) = NULL ;

  devtype = find_device_type(devnum, protected_ok) ;
  if ( devtype == NULL ) {
    devices_set_last_error(DeviceUndefined) ;
    theIDevType(dev) = &Null_Device_Type ;
    return FALSE ;
  }

  /* Allocate private storage, if necessary */
  if ( theIDevTypePrivSize( devtype ) > 0 ) {
    if ( (theIPrivate(dev) = (uint8 *)mm_alloc(mm_pool_temp,
                                               theIDevTypePrivSize(devtype),
                                               MM_ALLOC_CLASS_DEVICE_PRIVATE)) == NULL ) {
      devices_set_last_error(DeviceVMError) ;
      theIDevType(dev) = &Null_Device_Type ;
      return FALSE ;
    }
  }

  theIDeviceFlags( dev ) = flags | theIDevTypeFlags(devtype) ;
  theIDevType( dev ) = devtype ;

  /* initialise the device */
  if ( (*theIDevInit(dev))(dev) < 0 ) {
    /* Don't leave device partially initialised. Set null device type in case
       devtype's last error routine uses private data, and disable device. */
    devices_set_last_error(DeviceIOError) ;
    theIDevType(dev) = &Null_Device_Type ;
    ClearEnableDevice(dev) ;

    if ( theIPrivate(dev) != NULL ) {
      mm_free(mm_pool_temp, (mm_addr_t)theIPrivate(dev),
              theIDevTypePrivSize(devtype)) ;
      theIPrivate(dev) = NULL ;
    }
    return FALSE ;
  }

  /* Automagically connect progress device. Cannot remove this without leaving
     a dangling pointer, so mark it undismountable. */
  if ( progressdev == NULL && strcmp(name, SW_DEVICE_NAME_PROGRESS) == 0 ) {
    SetUndismountableDevice(dev) ;
    progressdev = dev ;
  }

  return TRUE ;
}

/* Add device to device list. Used when adding standard devices or mounting
   new devices. */
void device_add(DEVICELIST *dev)
{
  DEVICELIST **device_list_tail ;

  HQASSERT(dev, "No devicelist to add") ;

  theINextDev(dev) = NULL ;

  /* Append device to end of list */
  device_list_tail = &device_list ;
  while ( *device_list_tail ) {
    HQASSERT(dev != *device_list_tail, "Device is already on device list") ;
    device_list_tail = &theINextDev(*device_list_tail) ;
  }

  *device_list_tail = dev ;
}

/* Add new device at front of device list.  Only used when starting up the RIP
 * and replacing the %os% file device. */
void device_add_first(
  DEVICELIST* dev)
{
  HQASSERT((dev != NULL),
           "device_add_first: NULL device pointer");

  /* Add device to start of device list */
  theINextDev(dev) = device_list;
  device_list = dev;
}

static Bool devices_set_systemparam(corecontext_t *context, uint16 name, OBJECT *theo)
{
  DEVICESPARAMS *devparams = context->devicesparams ;

  HQASSERT((theo && name < NAMES_COUNTED) ||
           (!theo && name == NAMES_COUNTED),
           "name and parameter object inconsistent") ;

  switch ( name ) {
  case NAME_ObeySearchDevice:
    {
      int32 i;
      Bool flags[OSD_ELEMENTS];

      if ( theLen(*theo) != OSD_ELEMENTS )
        return error_handler(RANGECHECK);
      theo = oArray(*theo);
      for ( i = 0 ; i < OSD_ELEMENTS ; i++, theo++ ) {
        if ( oType(*theo) != OBOOLEAN)
          return error_handler(TYPECHECK);
        flags[i] = oBool(*theo) ;
      }
      for ( i = 0 ; i < OSD_ELEMENTS ; i++ ) {
        devparams->ObeySearchDevice[i] = (int8) flags[i];
      }
    }
    break ;
  }

  return TRUE ;
}

static Bool devices_get_systemparam(corecontext_t *context, uint16 name, OBJECT *result)
{
  DEVICESPARAMS *devparams = context->devicesparams ;

  HQASSERT(result, "No object for userparam result") ;

  switch ( name ) {
  case NAME_ObeySearchDevice:
    {
      int32 i ;
      static OBJECT searchflags[ OSD_ELEMENTS ] ;

      for ( i = 0 ; i < OSD_ELEMENTS ; ++i ) {
        /* Make sure static object array is marked as a NOTVM array. Since
           it's read-only, it shouldn't be changed, so check_asave shouldn't
           be called on it. */
        object_store_bool(object_slot_notvm(&searchflags[i]),
                          devparams->ObeySearchDevice[i]) ;
      }
      theTags( *result ) = OARRAY | LITERAL | READ_ONLY ;
      SETGLOBJECTTO(*result, TRUE) ;
      theLen(*result) = OSD_ELEMENTS ;
      oArray(*result) = searchflags ;
    }
    break;
  }

  return TRUE ;
}

#if defined( DEBUG_BUILD )
static inline void debug_print_indent(debug_buffer_t *buffer,
                                      unsigned int indent)
{
  while ( indent-- > 0 )
    dmonitorf(buffer, "  ") ;
}

static void debug_print_deviceparam_value(debug_buffer_t *buffer,
                                          DEVICEPARAM *param,
                                          unsigned int indent) ;

void debug_print_deviceparam_indented(DEVICEPARAM *param, char *pre, char *post)
{
  debug_buffer_t buffer = { 0 } ;
  unsigned int indent = 0 ;

  if ( pre && *pre ) {
    dmonitorf(&buffer, "%s", pre) ;

    while ( pre[indent] == ' ' )
      ++indent ;
  }

  dmonitorf(&buffer, "%.*s ", param->paramnamelen, param->paramname) ;
  debug_print_deviceparam_value(&buffer, param, indent) ;

  if ( post && *post )
    dmonitorf(&buffer, "%s", post) ;

  dflush(&buffer) ;
}

void debug_print_deviceparam(DEVICEPARAM *param)
{
  debug_print_deviceparam_indented(param, NULL, "\n") ;
}

static void debug_print_deviceparam_value(debug_buffer_t *buffer,
                                          DEVICEPARAM *param,
                                          unsigned int indent)
{
  int32 i ;

  switch ( param->type ) {
  case ParamBoolean:
    dmonitorf(buffer, param->paramval.boolval ? "true" : "false") ;
    break ;
  case ParamInteger:
    dmonitorf(buffer, "%d", param->paramval.intval) ;
    break ;
  case ParamString:
    {
      int32 len = 0 ;
      uint8 buf[1024] ;

      buf[len++] = '(' ;

      for ( i = 0 ; i < param->strvallen ; ++i ) {
        uint8 ch = param->paramval.strval[i] ;
        Bool slash = TRUE ;

        switch ( ch ) { /* Convert known control codes */
        case 8:  ch = 'b' ; break ;
        case 9:  ch = 't' ; break ;
        case 10: ch = 'n' ; break ;
        case 12: ch = 'f' ; break ;
        case 13: ch = 'r' ; break ;
        case '(': case '\\': case ')': /* needs slash */
          break ;
        default:
          slash = FALSE ;
          break ;
        }

        if ( !isprint(ch) ) { /* Still not printable, use octal escape */
          buf[len++] = '\\' ;
          buf[len++] = (uint8)('0' + ((ch >> 6) & 7));
          buf[len++] = (uint8)('0' + ((ch >> 3) & 7)) ;
          buf[len++] = (uint8)('0' + ((ch >> 0) & 7)) ;
        } else { /* Printable, possibly needs escaping */
          if ( slash )
            buf[len++] = '\\' ;

          buf[len++] = ch ;
        }

        if ( len > sizeof(buf) - 4 ) {
          dmonitorf(buffer, "%.*s", len, buf) ;
          len = 0 ;
        }
      }

      buf[len++] = ')' ;
      dmonitorf(buffer, "%.*s", len, buf) ;
    }
    break ;
  case ParamFloat:
    dmonitorf(buffer, "%f", param->paramval.floatval) ;
    break ;
  case ParamArray:
    dmonitorf(buffer, "[\n") ;
    ++indent ;
    for ( i = 0 ; i < param->strvallen ; ++i ) {
      debug_print_indent(buffer, indent) ;
      debug_print_deviceparam_value(buffer, &param->paramval.compobval[i],
                                    indent) ;
      dmonitorf(buffer, "\n") ;
    }
    --indent ;
    debug_print_indent(buffer, indent) ;
    dmonitorf(buffer, "]") ;
    break ;
  case ParamDict:
    dmonitorf(buffer, "<<\n") ;
    ++indent ;
    for ( i = 0 ; i < param->strvallen ; ++i ) {
      DEVICEPARAM *ob = &param->paramval.compobval[i] ;
      debug_print_indent(buffer, indent) ;
      dmonitorf(buffer, "%.*s ", ob->paramnamelen, ob->paramname) ;
      debug_print_deviceparam_value(buffer, ob, indent) ;
      dmonitorf(buffer, "\n") ;
    }
    --indent ;
    debug_print_indent(buffer, indent) ;
    dmonitorf(buffer, ">>") ;
    break ;
  case ParamNull:
    dmonitorf(buffer, "null") ;
    break ;
  default:
    dmonitorf(buffer, "--unknown<%d>--", param->type) ;
    break ;
  }
}
#endif /* defined( DEBUG_BUILD ) */


/* Log stripped */
