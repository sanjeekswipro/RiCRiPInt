/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:devices.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of PS device abstraction
 */

#include "core.h"
#include "coreinit.h"
#include "devices.h"
#include "devs.h"
#include "swdevice.h"
#include "swerrors.h"
#include "swstart.h"
#include "swoften.h" /* SwOften */
#include "objects.h"
#include "fileio.h"
#include "monitor.h"
#include "mm.h"
#include "timing.h"
#include "mmcompat.h"
#include "namedef_.h"

#include "pscontext.h"
#include "std_file.h"
#include "stacks.h"
#include "asyncps.h"
#include "progupdt.h"
#include "control.h" /* aninterrupt */
#include "interrupts.h"
#include "params.h"
#include "gofiles.h" /* get_startup_string */

#include "dicthash.h"
#include "dictops.h"
#include "filename.h" /* match_on_device */
#include "fileops.h" /* free_flist */
#include "progress.h"
#include "halftone.h" /* updateScreenProgress */
#include "cmpprog.h" /* updateProgress */
#include "devops.h" /* securityTickle */
#include "ripmulti.h" /* IS_INTERPRETER */


/* static prototypes */
static int32 RIPCALL boot_device_init(
              DEVICELIST * dev );
static DEVICE_FILEDESCRIPTOR RIPCALL boot_open_file(
              DEVICELIST *dev ,
              uint8 *filename ,
              int32 openflags );
static int32 RIPCALL boot_read_file( DEVICELIST *dev ,
              DEVICE_FILEDESCRIPTOR descriptor ,
              uint8 *buff ,
              int32 len );
static int32 RIPCALL boot_write_file(
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
              uint8 * buff,
              int32 len);
static int32 RIPCALL boot_close_file(
              DEVICELIST *dev ,
              DEVICE_FILEDESCRIPTOR descriptor );
static int32 RIPCALL boot_seek_file(
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
              Hq32x2 * destn,
              int32 flags);
static int32 RIPCALL boot_bytes_file(
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
              Hq32x2 * bytes,
              int32 reason);
static int32 RIPCALL boot_status_file(
              DEVICELIST * dev,
              uint8 * filename,
              STAT * statbuff);
static void * RIPCALL boot_start_list(
              DEVICELIST * dev,
              uint8 * pattern);
static int32 RIPCALL boot_next_list(
              DEVICELIST * dev,
              void ** handle,
              uint8 * pattern,
              FILEENTRY *entry);
static int32 RIPCALL boot_end_list(
              DEVICELIST * dev,
              void * handle);
static int32 RIPCALL boot_rename_file(
              DEVICELIST * dev,
              uint8 * file1,
              uint8 * file2);
static int32 RIPCALL boot_delete_file(
              DEVICELIST * dev,
              uint8 * filename);
static int32 RIPCALL boot_set_param(
              DEVICELIST * dev,
              DEVICEPARAM * param);
static int32 RIPCALL boot_start_param(
              DEVICELIST * dev);
static int32 RIPCALL boot_get_param(
              DEVICELIST * dev,
              DEVICEPARAM * param);
static int32 RIPCALL boot_status_device(
              DEVICELIST * dev,
              DEVSTAT * devstat);
static int32 RIPCALL boot_dismount(
              DEVICELIST * dev);
static int32 RIPCALL boot_buffer_size(
              DEVICELIST * dev);
static int32 RIPCALL boot_spare( void );

/* ---------------------------------------------------------------------- */
#define BOOT_DEVICE_TYPE 3 /* for hysterical raisins */

/* ----------------------------------------------------------------------
The Boot Device type (3), the implementation of which is internal to the rip
---------------------------------------------------------------------- */

static uint8 * boot_pointer = NULL;
static int32 boot_remaining = 0;

static int32 RIPCALL boot_device_init( DEVICELIST * dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceNoError);
  boot_pointer = NULL;
  boot_remaining = 0;

  return 0;
}

static DEVICE_FILEDESCRIPTOR RIPCALL boot_open_file( DEVICELIST *dev , uint8 *filename , int32 openflags )
{
  uint8 * boot_in_use;

  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(int32, openflags);

  devices_set_last_error(DeviceNoError);
  boot_in_use = boot_pointer;

  get_startup_string (filename, (int32)strlen ( (char *)filename),
                      (uint8**) & boot_pointer, & boot_remaining);

  if (boot_pointer) {
    if (boot_in_use) {
      devices_set_last_error(DeviceIOError);
      boot_pointer = boot_in_use;
      return -1; /* only one open at once */
    }
    return 1;
  } else {
    devices_set_last_error(DeviceUndefined);
    boot_pointer = boot_in_use;
    return -1;
  }
}

static int32 RIPCALL boot_read_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor , uint8 *buff , int32 len )
{
  register int32 i;

  UNUSED_PARAM(DEVICELIST *, dev);

  if (descriptor != 1)
    return -1; /* not open */

  /* on end of file just keep on returning zero */
  if (len > boot_remaining)
    len = boot_remaining;

  for (i = len; i; i--) {
    * buff ++ = (uint8)((* boot_pointer ++) ^ ((uint8)0xaa));
  }
  boot_remaining -= len;
  return len;
}

static int32 RIPCALL boot_write_file(
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
              uint8 * buff,
              int32 len)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8 *, buff);
  UNUSED_PARAM(int32, len);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL boot_close_file( DEVICELIST *dev , DEVICE_FILEDESCRIPTOR descriptor )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  if (descriptor != 1)
    return -1;
  boot_pointer = NULL;
  return 0;
}

static int32 RIPCALL boot_seek_file(
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
              Hq32x2 * destn,
              int32 flags
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(Hq32x2 *, destn);
  UNUSED_PARAM(int32, flags);

  devices_set_last_error(DeviceIOError);
  return FALSE;
}

static int32 RIPCALL boot_bytes_file
 ( DEVICELIST * dev, DEVICE_FILEDESCRIPTOR descriptor, Hq32x2 * bytes, int32 reason )
{
  UNUSED_PARAM( DEVICELIST *, dev );
  UNUSED_PARAM( DEVICE_FILEDESCRIPTOR, descriptor );
  UNUSED_PARAM( Hq32x2 *, bytes );
  UNUSED_PARAM( int32, reason );

  devices_set_last_error(DeviceIOError);
  return FALSE;
}

static int32 RIPCALL boot_status_file(
              DEVICELIST * dev,
              uint8 * filename,
              STAT * statbuff
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(STAT *, statbuff);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static void * RIPCALL boot_start_list(
              DEVICELIST * dev,
              uint8 * pattern
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, pattern);

  devices_set_last_error(DeviceNoError);
  return NULL;
}

static int32 RIPCALL boot_next_list(
              DEVICELIST * dev,
              void ** handle,
              uint8 * pattern,
              FILEENTRY *entry
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL boot_end_list(
              DEVICELIST * dev,
              void * handle
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(void *, handle);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL boot_rename_file(
              DEVICELIST * dev,
              uint8 * file1,
              uint8 * file2
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL boot_delete_file(
              DEVICELIST * dev,
              uint8 * filename
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL boot_set_param(
              DEVICELIST * dev,
              DEVICEPARAM * param
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  devices_set_last_error(DeviceNoError);
  return ParamIgnored;
}

static int32 RIPCALL boot_start_param(
              DEVICELIST * dev
)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL boot_get_param(
              DEVICELIST * dev,
              DEVICEPARAM * param
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  devices_set_last_error(DeviceNoError);
  return ParamIgnored;
}

static int32 RIPCALL boot_status_device(
              DEVICELIST * dev,
              DEVSTAT * devstat
)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVSTAT *, devstat);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL boot_dismount(
              DEVICELIST * dev
)
{
  /* The boot device is dismountable at RIP shutdown. */
  if ( isDeviceUndismountable(dev) ) {
    devices_set_last_error(DeviceIOError);
    return -1;
  } else {
    devices_set_last_error(DeviceNoError);
    return 0;
  }
}

static int32 RIPCALL boot_buffer_size(
              DEVICELIST * dev
)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  devices_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL boot_spare( void )
{
  devices_set_last_error(DeviceIOError);
  return -1;
}

static DEVICETYPE Boot_Device_Type = {
  BOOT_DEVICE_TYPE,               /* the device ID number */
  DEVICERELATIVE,                 /* flags to indicate specifics of device */
  0,                              /* the size of the private data */
  0,                              /* minimum ticks between tickle functions */
  NULL,                           /* procedure to service the device */
  devices_last_error,             /* return last error for this device */
  boot_device_init,               /* call to initialise device */
  boot_open_file,                 /* call to open file on device */
  boot_read_file,                 /* call to read data from file on device */
  boot_write_file,                /* call to write data to file on device */
  boot_close_file,                /* call to close file on device */
  boot_close_file,                /* call to abort action on the device */
  boot_seek_file,                 /* call to seek file on device */
  boot_bytes_file,                /* call to get bytes avail on an open file */
  boot_status_file,               /* call to check status of file */
  boot_start_list,                /* call to start listing files */
  boot_next_list,                 /* call to get next file in list */
  boot_end_list,                  /* call to end listing */
  boot_rename_file,               /* rename file on the device */
  boot_delete_file,               /* remove file from device */
  boot_set_param,                 /* call to set device parameter */
  boot_start_param,               /* call to start getting device parameters */
  boot_get_param,                 /* call to get the next device parameter */
  boot_status_device,             /* call to get the status of the device */
  boot_dismount,                  /* call to dismount the device */
  boot_buffer_size,
  NULL,                         /* ignore ioctl calls */
  boot_spare                      /* spare slots */
};

/* ----------------------------------------------------------------------------
   function:            currentdevparams_    author:              Luke Tunmer
   creation date:       07-Oct-1991          last modification:   ##-###-####
   arguments:
   description:
      See page 384 L2 Red book
---------------------------------------------------------------------------- */
Bool currentdevparams_(ps_context_t *pscontext)
{
  register int32    ssize ;
  register OBJECT * theo , *valo ;
  int32             len ;
  DEVICELIST      * dev ;
  DEVICEPARAM       param ;
  uint8           * device_name , * file_name ;
  OBJECT            tempo = OBJECT_NOTVM_NOTHING ;
  Bool              fTypeSet = FALSE;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType( *theo ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead( *theo ))
    if ( ! object_access_override(theo))
      return error_handler( INVALIDACCESS ) ;

  switch ( parse_filename( oString( *theo ) , (int32)theLen(* theo ) ,
                          &device_name , &file_name )) {
  case JUSTDEVICE :
    break ;
  case JUSTFILE :
  case DEVICEANDFILE :
  case NODEVICEORFILE :
    return error_handler( UNDEFINED ) ;
  default:
    return error_handler( LIMITCHECK ) ;
  }

  if ( NULL == ( dev = find_device( device_name ))) {
    if (strcmp ((char *) device_name, "EtherTalk_Pending") != 0)
      return error_handler( UNDEFINED ) ;
    /* this is revolting. It is here because the apple 7.4 font downloader
       tries to obtain the ethernet address froma device called
       EtherTalk_Pending. I (DE) think it is better than putting
       a device in called EtherTalk_Pending, and I don't want to
       implement all of the mechanism Adobe describe in their extensions
       stuff for devices, is way OTT */

    pop (& operandstack);

    theTags( tempo ) = OSTRING | EXECUTABLE | READ_ONLY ;
    oString( tempo ) =
      (uint8 *) "<< /EthernetAddress (0:0:0:0:0:0) >>" ;
    theLen( tempo ) = (uint16)strlen(( char * ) oString( tempo )) ;

    execStackSizeNotChanged = FALSE ;
    return push( & tempo , & executionstack ) ;
  }

  /* check that the device type has been initialised */
  if ( theIDevType( dev ) == NULL )
    return error_handler( INVALIDACCESS ) ;
  if (( len = (*theIStartParam( dev ))( dev )) < 0 )
    return error_handler( UNREGISTERED ) ;

  /* +11 below to allow for standard entries */
  if ( ! ps_dictionary(theo, len+11) )
    return FALSE ;

  /* insert the standard entries */
  oName( nnewobj ) = &system_names[ NAME_DeviceType ] ;
  oInteger( inewobj ) = theIDevTypeNumber( theIDevType( dev )) ;
  if ( ! insert_hash( theo , &nnewobj , &inewobj ))
    return FALSE ;
  oName( nnewobj ) = &system_names[ NAME_Enable ] ;
  valo = isDeviceEnabled( dev ) ? &tnewobj : &fnewobj ;
  if ( ! insert_hash( theo , &nnewobj , valo ))
    return FALSE ;
  oName( nnewobj ) = &system_names[ NAME_HasNames ] ;
  valo = isDeviceEnabled( dev ) && isDeviceRelative( dev ) ?
                                              &tnewobj : &fnewobj ;
  if ( ! insert_hash( theo , &nnewobj , valo ))
    return FALSE ;
  oName( nnewobj ) = &system_names[ NAME_InitializeAction ] ;
  oInteger( inewobj ) = 0 ;
  if ( ! insert_hash( theo , &nnewobj , &inewobj ))
    return FALSE ;
  oName( nnewobj ) = &system_names[ NAME_Mounted ] ;
  valo = isDeviceEnabled( dev ) ? &tnewobj : &fnewobj ;
  if ( ! insert_hash( theo , &nnewobj , valo ))
    return FALSE ;
  oName( nnewobj ) = &system_names[ NAME_PrepareAction ] ;
  oInteger( inewobj ) = 0 ;
  if ( ! insert_hash( theo , &nnewobj , &inewobj ))
    return FALSE ;
  oName( nnewobj ) = &system_names[ NAME_Removable ] ;
  valo = isDeviceRemovable( dev ) ? &tnewobj : &fnewobj ;
  if ( ! insert_hash( theo , &nnewobj , valo ))
    return FALSE ;
  oName( nnewobj ) = &system_names[ NAME_Searchable ] ;
  valo = isDeviceNoSearch( dev ) ? &fnewobj : &tnewobj ;
  if ( ! insert_hash( theo , &nnewobj , valo ))
    return FALSE ;
  /** \todo at the moment, SearchOrder is implemented as an alternative
   * API of Searchable. We will probably implement its full semantics
   * when requests arise.
   */
  oName( nnewobj ) = &system_names[ NAME_SearchOrder ] ;
  oInteger( inewobj ) = isDeviceNoSearch( dev ) ? -1 : 0 ;
  if ( ! insert_hash( theo , &nnewobj , &inewobj ))
    return FALSE ;
  oName( nnewobj ) = &system_names[ NAME_Writeable ] ;
  valo = isDeviceWritable( dev ) ? &tnewobj : &fnewobj ;
  if ( ! insert_hash( theo , &nnewobj , valo ))
    return FALSE ;

   /* NAME_Type will be appended at last: if the underlying device reports
    * its type, that is taken, otherwise a default type of /Parameters is
    * assumed.
    */
  fTypeSet = FALSE;

  /* get the device specific entries, and insert them into the dict */
  /* fetch (at most) the number of parameters theIStartParam() indicated */
  while ( --len >= 0 ) {
    /* force theIGetParam() to return the next, rather than a specific entry */
    theDevParamName( param ) = NULL;
    theDevParamNameLen( param ) = 0;
    switch ((*theIGetParam( dev ))( dev , &param )) {
    case ParamAccepted:
      break ;
    case ParamIgnored:
      continue ;
    case ParamTypeCheck:
    case ParamRangeCheck:
    case ParamConfigError:
      HQFAIL( "Illegal return value from get_param" ) ;
      return error_handler( UNREGISTERED ) ;
    case ParamError:
    default:
      /* return with the last error */
      return device_error_handler( dev ) ;
    }
    if ( NULL == ( oName( nnewobj ) =
            cachename( theDevParamName( param ), (uint32)theDevParamNameLen( param ))))
      return FALSE ;
    switch ( theDevParamType( param )) {
    case ParamInteger :
      oInteger( inewobj ) = theDevParamInteger( param ) ;
      valo = & inewobj ;
      break ;
    case ParamBoolean :
      valo = theDevParamBoolean( param ) ? &tnewobj : &fnewobj ;
      break ;
    case ParamFloat :
      oReal( rnewobj ) = theDevParamFloat( param ) ;
      valo = &rnewobj ;
      break ;
    case ParamString :
      if (oName( nnewobj ) == &system_names[ NAME_Type ]) {
        /* The underlying device has told us about its PS3010 Type */
        fTypeSet = TRUE;
        OCopy(tempo, nnewobj);
        if (strncmp("Communications", (char *) theDevParamString( param ) ,
                     theDevParamStringLen( param )) == 0)
          oName( tempo ) = &system_names[ NAME_Communications ] ;
        else if (strncmp("Parameters", (char *) theDevParamString( param ) ,
                     theDevParamStringLen( param )) == 0)
          oName( tempo ) = &system_names[ NAME_Parameters ] ;
        else if (strncmp("FileSystem", (char *) theDevParamString( param ) ,
                     theDevParamStringLen( param )) == 0)
          oName( tempo ) = &system_names[ NAME_FileSystem ] ;
        else if (strncmp("Emulator", (char *) theDevParamString( param ) ,
                     theDevParamStringLen( param )) == 0)
          oName( tempo ) = &system_names[ NAME_Emulator ] ;
        else
          HQFAIL("Error: Unknown PS3010 Device Type");
        valo = &tempo ;
      }
      else {
        if ( !ps_string(&tempo, theDevParamString(param),
                        theDevParamStringLen(param)) )
          return FALSE ;
        valo = &tempo ;
      }
      break ;
    }
    if ( ! insert_hash( theo , &nnewobj , valo ))
      return FALSE ;
  }

  if (! fTypeSet) {
    /* we take Parameters for default */
    OCopy(tempo, nnewobj);
    oName( nnewobj ) = &system_names[ NAME_Type ] ;
    oName( tempo ) = &system_names[ NAME_Parameters ] ;
    if ( ! insert_hash( theo , &nnewobj , &tempo ))
      return FALSE ;
  }
  return TRUE ;
}

/* The following routines support the sending of compound objects to plugins
 * via setdevparams, and external modules such as screening modules via
 * their dedicated APIs.
 * The supported object types include arrays (ParamArray) and dictionaries
 * (ParamDict). These will allocate in temporary memory an array of DEVICEPARAM
 * structures, which will hold the actual parameters (key and value for
 * dictionaries, just value for arrays). They may nest.
 * After the compound types are sent to the plugin, the temporary memory must
 * be freed via freeDeviceParamMem.
 *
 * Last Modified:       10-Oct-2006
 * Modification history:
 *      06-Oct-95 (N.Speciner); created
 *  10-Oct-06 (derekp); Export via dev_if.h for use with screening modules etc.
 *                      Allow for passing file objects as blob-memmap.
 */
static Bool fillDeviceParamVal(DEVICEPARAM *param, OBJECT *thekey, OBJECT *theval);

/* called via walk_dictionary */
static Bool fillDeviceParamDict(OBJECT *thekey, OBJECT *theval, void *argBlockPtr)
{
  DEVICEPARAM *param = (DEVICEPARAM *)argBlockPtr;
  int32 index = theIDevParamDictLen(param);

  theIDevParamDictLen(param) += 1;
  return fillDeviceParamVal( theIDevParamDict( param ) + index, thekey, theval );
}

static Bool fillDeviceParamVal(DEVICEPARAM *param, OBJECT *thekey, OBJECT *theval)
{
  DEVICEPARAM *parptr;
  OBJECT *optr;
  int32 arraycount;

  if (thekey != NULL) {
    theIDevParamName( param ) = theICList( oName( *thekey )) ;
    theIDevParamNameLen( param ) =
      (int32)theINLen( oName( *thekey )) ;
  }
  else {
    theIDevParamName( param ) = NULL;
    theIDevParamNameLen( param ) = (int32)0;
  }

  switch ( oType( *theval )) {
  case OINTEGER :
    theIDevParamType( param ) = ParamInteger ;
    theIDevParamInteger( param ) = oInteger( *theval ) ;
    break ;

  case OBOOLEAN :
    theIDevParamType( param ) = ParamBoolean ;
    theIDevParamBoolean( param ) = oBool( *theval ) ;
    break ;

  case OSTRING :
    theIDevParamType( param ) = ParamString ;
    theIDevParamString( param ) = oString( *theval ) ;
    theIDevParamStringLen( param ) = (int32)theLen(* theval ) ;
    break ;

  case OLONGSTRING :
    theIDevParamType( param ) = ParamString ;
    theIDevParamString( param ) = theLSCList(*oLongStr(*theval)) ;
    theIDevParamStringLen( param ) = theLSLen(*oLongStr(*theval)) ;
    break ;

    /* Name objects are passed to the device as strings */
  case ONAME :
    theIDevParamType( param ) = ParamString ;
    theIDevParamString( param ) = theICList( oName( *theval )) ;
    theIDevParamStringLen( param ) =
      (int32)theINLen( oName( *theval )) ;
    break ;

  case OREAL :
    theIDevParamType( param ) = ParamFloat ;
    theIDevParamFloat( param ) = oReal( *theval ) ;
    break ;

  case OARRAY:
  case OPACKEDARRAY:
    arraycount = theLen(* theval );
    theIDevParamType( param ) = ParamArray ;
    if (arraycount == 0)
      theIDevParamArray(param) = NULL;
    else if ((theIDevParamArray(param) = (DEVICEPARAM *)
              mm_alloc_with_header(mm_pool_temp,arraycount * sizeof(DEVICEPARAM),
                                   MM_ALLOC_CLASS_REL_ARRAY)) == NULL)
      return error_handler( VMERROR );

    /* Now need to fill param array... */
    theIDevParamArrayLen( param ) = arraycount;
    for (parptr = theIDevParamArray(param), optr = oArray(*theval);
         arraycount-- ; parptr++,optr++) {
      if (!fillDeviceParamVal(parptr,NULL,optr))
        return FALSE;
    }
    break;

  case ODICTIONARY:
    getDictLength(arraycount,theval);   /* macro fills arraycount */
    theIDevParamType( param ) = ParamDict ;
    if (arraycount == 0)
      theIDevParamDict(param) = NULL;
    else if ((theIDevParamDict(param) = (DEVICEPARAM *)
              mm_alloc_with_header(mm_pool_temp,arraycount * sizeof(DEVICEPARAM),
                                   MM_ALLOC_CLASS_REL_DICT)) == NULL)
      return error_handler( VMERROR );

    /* Now need to fill dict array... */
    theIDevParamDictLen( param ) = 0;
    return walk_dictionary( theval, fillDeviceParamDict, (void *)param );

  case ONULL :
    theIDevParamType( param ) = ParamNull ;
    theIDevParamInteger( param ) = 0 ; /* just to put a value there */
    break ;

  case OFILE :
    /* Placeholder for where we'll add the blobbing-up of the file. */
    /* FALLTHROUGH */

  default:
    return error_handler( TYPECHECK ) ;
  }
  return TRUE;
}

static void freeDeviceParamMem( DEVICEPARAM *param )
{
  int32 count;
  uint8 *mem;
  DEVICEPARAM *parptr;

  switch (theIDevParamType( param )) {
  case ParamArray:
    if ((mem = (uint8*)theIDevParamArray( param )) == NULL)
      break;
    for (count = theIDevParamArrayLen(param), parptr=theIDevParamArray(param);
         count-- ; parptr++)
      freeDeviceParamMem( parptr );
    mm_free_with_header(mm_pool_temp, (mm_addr_t) mem);
    break;
  case ParamDict:
    if ((mem = (uint8 *)theIDevParamDict( param )) == NULL)
      break;
    for (count = theIDevParamDictLen(param), parptr=theIDevParamDict(param);
         count-- ; parptr++)
      freeDeviceParamMem( parptr );
    mm_free_with_header(mm_pool_temp, (mm_addr_t) mem );
    break;
  default:
    break;
  }
}

/* End of routines to support compound object passing via setdevparams */

/* ----------------------------------------------------------------------------
   function:            setdevparams_     author:              Luke Tunmer
   creation date:       08-Oct-1991       last modification:   06-Oct-1995
   arguments:
   description:

   The routine device_set_params is passed as an argument to the
   walk_dictionary procedure. It is called for key/value pairs in the
   dictionary. The parameter argBlockPtr is a pointer to data for this
   callback routine, which in this case is just the DEVICELIST.

   Modification history:
   06-Oct-95 (N.Speciner); modified to fill parameter structure with a separate
        routine (fillParamVal) to support passing of compound objects.
---------------------------------------------------------------------------- */
Bool device_set_params( OBJECT *thekey , OBJECT *theval , void *argBlockPtr )
{
  DEVICELIST *dev = (DEVICELIST *)argBlockPtr ;
  DEVICEPARAM param ;
  int32 rval;

  if ( oType( *thekey ) != ONAME )
    return error_handler( TYPECHECK ) ;
  switch ( theINameNumber(oName(*thekey)) ) {
  case NAME_Password:   /* nothing to set there */
  case NAME_DeviceType: /* handled by setdevparams_ */
  case NAME_Mounted:
  case NAME_Enable:
  case NAME_Searchable:
  case NAME_SearchOrder:
    return TRUE ;
  }

  if ( !fillDeviceParamVal( &param, thekey, theval ) ) {
    freeDeviceParamMem( &param );
    return FALSE;
  }
  rval = (*theISetParam( dev ))( dev , &param );
  freeDeviceParamMem ( &param );
  switch (rval) {
  case ParamIgnored :
  case ParamAccepted :
    break ;
  case ParamTypeCheck :
    return error_handler( TYPECHECK ) ;
  case ParamRangeCheck :
    return error_handler( RANGECHECK ) ;
  case ParamConfigError :
    /* set up the errorinfo array in the $error dictionary */
    return errorinfo_error_handler( CONFIGURATIONERROR , thekey , theval ) ;
  case ParamError:
    /* return with the last error */
    return device_error_handler( dev ) ;
  default:
    return error_handler( UNREGISTERED ) ;
  }

  return TRUE ;
}


Bool setdevparams_(ps_context_t *pscontext)
{
  corecontext_t *corecontext = ps_core_context(pscontext);
  register OBJECT *o1 , *o2 ;
  uint8 *device_name , *file_name ;
  DEVICELIST *dev ;
  OBJECT *theo ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = theTop( operandstack ) ;
  if ( oType( *o2 ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  o1 = stackindex( 1 , &operandstack ) ;
  if ( oType( *o1 ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( (!oCanRead(*o1) && !object_access_override(o1)) ||
       (!oCanRead(*oDict(*o2)) && !object_access_override(oDict(*o2))) )
    return error_handler( INVALIDACCESS ) ;

  switch ( parse_filename( oString( *o1 ) , (int32)theLen(* o1 ) ,
                          &device_name , &file_name )) {
  case JUSTDEVICE :
    break ;
  case JUSTFILE :
  case DEVICEANDFILE :
  case NODEVICEORFILE :
    return error_handler( UNDEFINED ) ;
  default:
    return error_handler( LIMITCHECK ) ;
  }

  if ( NULL == ( dev = find_device( device_name )))
    return error_handler( UNDEFINED ) ;

  /* extract the Password - only check it if there is one defined [#10014] */
  /* Current Adobe RIPs do not require it, Genoa CET 27-03,08      */
  /* assume this - and feed current... back into setdevparams!     */
  /* Check for passwords only if the system password is set */
  if ( corecontext->systemparams->SystemPasswordLen != 0 ) {
    oName( nnewobj ) = &system_names[ NAME_Password ] ;
    if ( NULL == ( theo = extract_hash( o2 , &nnewobj )) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! check_sys_password(corecontext, theo) )
      return error_handler( INVALIDACCESS ) ;
  }

  /* extract Enable - if it's there */
  oName( nnewobj ) = &system_names[ NAME_Enable ] ;
  if ( (theo = extract_hash( o2 , &nnewobj )) != NULL ) {
    if ( oType( *theo ) != OBOOLEAN )
      return error_handler( TYPECHECK ) ;
    if ( oBool( *theo ))
      SetEnableDevice( dev ) ;
    else
      ClearEnableDevice( dev ) ;
  }

  /* extract Mounted - if it's there. Note for us, PS3 Mounted param is the
   * same as our Enable due to difference of devmount (see task 21454).
   */
  oName( nnewobj ) = &system_names[ NAME_Mounted ] ;
  if ( (theo = extract_hash( o2 , &nnewobj )) != NULL ) {
    if ( oType( *theo ) != OBOOLEAN )
      return error_handler( TYPECHECK ) ;
    if ( oBool( *theo ))
      SetEnableDevice( dev ) ;
    else
      ClearEnableDevice( dev ) ;
  }

  /* extract Searchable - if it's there. */
  oName( nnewobj ) = &system_names[ NAME_Searchable ] ;
  if ( (theo = extract_hash( o2 , &nnewobj )) != NULL ) {
    if ( oType( *theo ) != OBOOLEAN )
      /* PS3010 does not define errors returned on this, but i think
       * a typecheck error returned in this case is appropriate.
       */
      return error_handler( TYPECHECK ) ;

    if ( oBool( *theo ))
      ClearNoSearchDevice( dev ) ;
    else
      SetNoSearchDevice( dev ) ;
  }

  /* extract SearchOrder - if it's there, at the moment, we implement
   * SearchOrder as an additional API for Searchable. In future we
   * might implement its full semantics as defined in PS3010, ie, gives
   * the priority at which the device participate in searches.
   */
  oName( nnewobj ) = &system_names[ NAME_SearchOrder ] ;
  if ( (theo = extract_hash( o2 , &nnewobj )) != NULL ) {
    if ( oType( *theo ) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    if ( oInteger( *theo ) < 0 )
      SetNoSearchDevice( dev ) ;
    else
      ClearNoSearchDevice( dev ) ;
  }

  /* extract the device type - if it's there */
  if ( ! theIDevType( dev )) {
    oName( nnewobj ) = &system_names[ NAME_DeviceType ] ;
    if ((theo = extract_hash( o2 , &nnewobj )) != NULL) {
      if ( oType( *theo ) != OINTEGER )
        return error_handler( TYPECHECK ) ;

      if ( !device_connect(dev, oInteger(*theo), (char *)theIDevName(dev),
                           theIDeviceFlags(dev), FALSE) ) {
        (void)device_error_handler(dev) ;
        theIDevType(dev) = NULL ; /* Disconnect uninitialised device type */
        return FALSE ;
      }
    } else /* if the device type is not installed, cannot set any parameters */
      return error_handler( INVALIDACCESS ) ;
  }

  /* walk over the dictionary to get device specific stuff */
  if ( ! walk_dictionary( o2 , device_set_params , ( void* )dev ))
    return FALSE ;

  npop( 2 , &operandstack ) ;
  return TRUE;
}



/* ----------------------------------------------------------------------------
   function:            devmount_         author:              Luke Tunmer
   creation date:       15-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool devmount_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  int32 len ;
  uint8 *device_name , *file_name ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType( *theo ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead( *theo ))
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
  len = (int32)theLen(* theo ) ;

  switch ( parse_filename( oString( *theo ) , len ,
                           &device_name , &file_name )) {
  case JUSTDEVICE :
    break ;
  case JUSTFILE :
  case DEVICEANDFILE :
  case NODEVICEORFILE :
    return error_handler( UNDEFINEDFILENAME ) ;
  default:
    return error_handler( LIMITCHECK ) ;
  }

  if ( ! device_mount(device_name) )
    return error_handler(VMERROR) ;

  Copy( theo , &tnewobj ) ;

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            devdismount_      author:              Luke Tunmer
   creation date:       15-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool devdismount_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo ;
  int32 len ;
  uint8 *device_name , *file_name ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType( *theo ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead( *theo ))
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
  len = (int32)theLen(* theo ) ;

  switch ( parse_filename( oString( *theo ) , len ,
                           &device_name , &file_name )) {
  case JUSTDEVICE :
    break ;
  case JUSTFILE :
  case DEVICEANDFILE :
  case NODEVICEORFILE :
    return error_handler( UNDEFINEDFILENAME ) ;
  default:
    return error_handler( LIMITCHECK ) ;
  }

  if ( !device_dismount(device_name) )
    return FALSE;

  pop( &operandstack ) ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            devstatus_        author:              Luke Tunmer
   creation date:       15-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool devstatus_(ps_context_t *pscontext)
{
  register int32 ssize ;
  register OBJECT *theo , *o1 ;
  int32 len ;
  DEVICELIST *dev , *dp ;
  DEVSTAT statdev ;
  uint8 *device_name , *file_name ;
  SYSTEMVALUE val;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType( *theo ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead( *theo ))
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
  len = (int32)theLen(* theo ) ;

  switch ( parse_filename( oString( *theo ) , len ,
                           &device_name , &file_name )) {
  case JUSTDEVICE :
  case DEVICEANDFILE :
    break ;
  case JUSTFILE :
  case NODEVICEORFILE :
    Copy( theo , &fnewobj ) ;
    return TRUE ;
  default:
    return error_handler( LIMITCHECK ) ;
  }
  if ( NULL == ( dev = find_device( device_name ))) {
    /* just return false on the stack */
    Copy( theo , &fnewobj ) ;
  } else {
    device_iterator_t iter ;

    if ( theIDevType( dev ) == NULL ) /* device not initialised */
      return error_handler( INVALIDACCESS ) ;
    /* writable */
    o1 = isDeviceWritable( dev ) ? &tnewobj : &fnewobj ;
    if ( ! push( o1 , & operandstack ))
      return FALSE ;
    /* hasnames ie. device relative */
    o1 = isDeviceRelative( dev ) ? &tnewobj : &fnewobj ;
    if ( ! push( o1 , &operandstack )) {
      pop( &operandstack ) ;
      return FALSE ;
    }
    /* mounted - for us this is true if device enabled */
    o1 = isDeviceEnabled( dev ) ? &tnewobj : &fnewobj ;
    if ( ! push( o1 , &operandstack )) {
      npop( 2 , &operandstack ) ;
      return FALSE ;
    }
    /* removable */
    o1 = isDeviceRemovable( dev ) ? &tnewobj : &fnewobj ;
    if ( ! push( o1 , &operandstack )) {
      npop( 3 , &operandstack ) ;
      return FALSE ;
    }
    /* search order */
    len = 1 ;
    for ( dp = device_first(&iter, 0) ; dp != dev ; dp = device_next(&iter) ) {
      ++len ;
    }
    oInteger( inewobj ) = len ;
    if ( ! push( &inewobj , &operandstack )) {
      npop( 4 , &operandstack ) ;
      return FALSE ;
    }
    if ( (*theIStatusDevice( dev ))( dev , &statdev ) == EOF ) {
      statdev.block_size = 1;
      HqU32x2FromInt32(&statdev.size, 0);
      HqU32x2FromInt32(&statdev.free, 0);
    }

    /* The size and free amount must be reported in Kb - font down
     * loaders expect it. Note it is also defined in gfiledef.c as a
     * constant. Any change here should be followed by a check there.
     */
#define BLOCK_SIZE  1024

    /* free - limit to maxint if it is too large */
    val = HqU32x2ToDouble(&statdev.free);
    val = (val + (SYSTEMVALUE)(BLOCK_SIZE - 1)) / (SYSTEMVALUE)BLOCK_SIZE;
    if (val > (SYSTEMVALUE)MAXINT32 || (int32)val < 0)
      val = (SYSTEMVALUE)MAXINT32;

    oInteger( inewobj ) = (int32)val;
    if ( ! push( &inewobj , &operandstack )) {
      npop( 5 , &operandstack ) ;
      return FALSE ;
    }

    /* size - limit to maxint if it is too large */
    val = HqU32x2ToDouble(&statdev.size);
    val = (val + (SYSTEMVALUE)(BLOCK_SIZE - 1)) / (SYSTEMVALUE)BLOCK_SIZE;
    if (val > (SYSTEMVALUE)MAXINT32 || (int32)val < 0)
      val = (SYSTEMVALUE)MAXINT32;
    oInteger( inewobj ) = (int32)val;
    if ( ! push( &inewobj , &operandstack )) {
      npop( 6 , &operandstack ) ;
      return FALSE ;
    }
    /* true */
    if ( ! push( &tnewobj , &operandstack )) {
      npop( 7 , &operandstack ) ;
      return FALSE ;
    }
    /* searchable ie. device enabled */
    o1 = isDeviceNoSearch( dev ) ? &fnewobj : &tnewobj ;
    Copy( theo , o1 ) ;
  }

  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            devforall_        author:              Luke Tunmer
   creation date:       27-Feb-1992       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool devforall_(ps_context_t *pscontext)
{
  uint8 *dev_pattern = (uint8 *) "*" ;
  uint8 *file_pattern = (uint8 *) "*" ;
  OBJECT *proc , *scratch , *pat ;
  DEVICELIST *dev ;
  SLIST * matchlist ;
  int32 nstack ;
  Bool ok = TRUE;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  nstack = 2 ;
  pat = NULL ;
  proc = stackindex( 1 , & operandstack ) ;
  if (oType (*proc) == OSTRING) {
    if ( theStackSize( operandstack ) < 2 )
      return error_handler( STACKUNDERFLOW ) ;
    pat = proc ;
    proc = stackindex( 2 , & operandstack ) ;
    nstack = 3 ;
  }
  scratch = theTop( operandstack ) ;
  switch ( oType( *proc )) {
  case OARRAY :
  case OPACKEDARRAY :
  case OOPERATOR :
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  if (oType( *scratch ) != OSTRING )
    return error_handler( TYPECHECK ) ;

  if ( (pat && !oCanRead(*pat) && !object_access_override(pat)) ||
       (!oCanExec(*proc) && !object_access_override(proc)) ||
       (!oCanWrite(*scratch) && !object_access_override(scratch)) )
    return error_handler( INVALIDACCESS ) ;

  if ( pat ) {
    if ( ! push3( proc , pat , scratch , &temporarystack ))
      return FALSE ;
    scratch = theTop( temporarystack ) ;
    pat = stackindex( 1 , &temporarystack ) ;
    proc = stackindex( 2 , &temporarystack ) ;
  } else {
    if ( ! push2( proc , scratch , &temporarystack ))
      return FALSE ;
    scratch = theTop( temporarystack ) ;
    proc = stackindex( 1 , &temporarystack ) ;
  }

  npop( nstack , & operandstack ) ;

  switch (pat ? parse_filename( oString( *pat ) , (int32)theLen(* pat ) ,
                &dev_pattern , &file_pattern ) : JUSTDEVICE ) {
  case JUSTFILE :
    dev_pattern = file_pattern ;
    /* fall through - assume it is a device name without a leading % */
  case JUSTDEVICE :
    {
      uint8 dev_cache[ LONGESTDEVICENAME ] ;
      device_iterator_t iter ;
      DEVICEPARAM param;
      Bool matchdev;

      theDevParamName(param) = (uint8*)"Type";
      theDevParamNameLen(param) = strlen_int32("Type");

      /* cache the device pattern */
      strcpy( (char *)dev_cache , (char *)dev_pattern ) ;

      matchlist = NULL ;
      for ( dev = device_first(&iter, DEVICEENABLED) ; dev ; dev = device_next(&iter) ) {
        if ( !isDeviceNoSearch( dev )) {
          /* Match all enabled devices if using a pattern else only match
           * filesystems */
          matchdev = (pat != NULL);
          if ( !matchdev ) {
            switch ( (*theIGetParam(dev))(dev, &param) ) {
            case ParamAccepted:
              if ( theDevParamType(param) == ParamString ) {
                matchdev = (strncmp("FileSystem", (char*)theDevParamString(param),
                                    theDevParamStringLen(param)) == 0);
              }
              /*@fallthrough@*/
            case ParamIgnored:
              break;
            case ParamError:
              free_flist(matchlist);
              npop(nstack, &temporarystack);
              return(device_error_handler(dev));
            case ParamTypeCheck:
            case ParamRangeCheck:
            case ParamConfigError:
            default:
              HQFAIL("Illegal return value from theIGetParam");
              free_flist(matchlist);
              npop(nstack, &temporarystack);
              return(error_handler(UNREGISTERED));
            }
          }
          if ( matchdev && SwPatternMatch( dev_cache , theIDevName( dev ))) {
            if ( ! match_on_device( dev , & matchlist )) {
              free_flist ( matchlist ) ;
              npop( nstack , &temporarystack ) ;
              return FALSE ;
            }
          }
        }
      }
      ok = execute_filenameforall (scratch, matchlist, proc);
      break ;
    }
  case DEVICEANDFILE :
    npop( nstack , &temporarystack ) ;
    return error_handler( UNDEFINEDFILENAME ) ;

  case NODEVICEORFILE :
    /* Empty string given as pattern; nothing can match. */
    break ;

  default:
    npop( nstack , &temporarystack ) ;
    return error_handler( LIMITCHECK ) ;
  }

  npop( nstack , &temporarystack ) ;

  return ok ;
}



/* Initialise standard device types and devices for PostScript. Boot device
 * serves startup files from compiled-in strings. */
static Bool init_devices_table(struct SWSTART *params)
{
  DEVICELIST *dev ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  /* Add remaining standard device types */
  if (! device_type_add(&Boot_Device_Type))
    return FALSE ;

  /* Set up the boot device entry */
  if ( (dev = device_mount((uint8*)"boot")) == NULL)
    return FALSE ;

  return device_connect(dev, BOOT_DEVICE_TYPE, (char*)theIDevName( dev ), DEVICEENABLED, TRUE) ;
}

void ps_boot_C_globals(core_init_fns *fns)
{
  fns->swstart = init_devices_table ;
}

/* ---------------------------------------------------------------------- */

static int32 tickle_recursion = -1;
static int32 tickle_smallest = 1;
static Bool tickle_loop = FALSE;

static int async_unsafe = 0;

#define is_async_action_allowed() (async_unsafe==0)
#define block_async_action() async_unsafe++
#define unblock_async_action() async_unsafe--

static int32 default_timer = 0;
#ifndef FAR
#define FAR
#define NULL_FAR_DEFINITION
#endif

#ifdef FAR_INITIALISE_BUG
int32 FAR * SwTimer = NULL ;
#else /* !FAR_INITIALISE_BUG */
int32 FAR * SwTimer = & default_timer ;
#endif /* !FAR_INITIALISE_BUG */

SkinTimerExpiredFn pfSkinTimerExpired = NULL;

void RIPCALL SetSwTimer( int32 FAR * timer )
{
  SwTimer = timer;
}

void RIPCALL SetSkinTimerExpiredFn( SkinTimerExpiredFn pf )
{
  pfSkinTimerExpired = pf;
}

#ifdef NULL_FAR_DEFINITION
#undef FAR
#undef NULL_FAR_DEFINITION
#endif

int32 RIPCALL SwOftenCore( void )
{
  return SwOften();
}

/* External API solely to allow async PS to run when the RIP is idling in the
 * config device.  As such there will never be a pending interrupt and never any
 * interpretation progress to report.
 */
int32 RIPCALL SwOftenActivateSafe( void )
{
  /* Call to get_core_context() is necessary since app threads wont have a core context. */
  HQASSERT((get_core_context() != NULL) && IS_INTERPRETER(),
           "SwOftenActivateSafe: Called on non-interpreter thread.");

  /* Do any pending async actions */
  if (is_async_action_allowed() && async_ps_pending()) {
    do_pending_async_ps();
  }

  return (0);
}

int32 SwOftenActivateSafeInternal(void)
{
  corecontext_t* corecontext;
  int32 local_adjust, local_tickle_smallest;
  int32 new_tickle_smallest, tickle_result;
  int32 retval = 0;

/* Don't tickle when in an assert */
#if defined( ASSERT_BUILD )
  if ( HQASSERT_DEPTH != 0 )
  {
    return 0;
  }
#endif

  /* If the mm system hasn't been started up yet, or has already been
   * shut down, then we shouldn't be tickling any devices
   */
  if ( mm_pool_temp == NULL )
    return 0 ;

  /** \todo Only the interpreter tickles.  Later, redesign. */
  if (((corecontext = get_core_context()) == NULL) || !corecontext->is_interpreter)
    return 0 ;

  /* Check to see if an interrupt needs actioning */
  if (!interrupts_clear(allow_interrupt)) {
    dosomeaction = TRUE;
    retval = -1; /* Covers timeout and interrupt, skin doesn't care */
  }

  /* Do any pending async actions */
  if (is_async_action_allowed() && async_ps_pending()) {
    do_pending_async_ps();
  }

  /* Do any progress updates */
  if (do_progress_updates) {
    do_progress_updates = FALSE;
    updateScreenProgress();
    updateReadFileProgress();
    updateDLProgress(RECOMBINE_PROGRESS);
    updateDLProgress(PRECONVERT_PROGRESS);
  }

  if ( !device_tickles_required() )
    return (retval);

  local_tickle_smallest = tickle_smallest;
  local_adjust = * SwTimer; /* local assignment in case it changes
                               under our feet */
  * SwTimer = local_tickle_smallest;
  /* so that recursive calls don't immediately call - note
     that we could just miss a tick between those two
     instructions */

  local_adjust -= local_tickle_smallest;

  ++ tickle_recursion;

  do {
    device_type_iterator_t iter ;
    DEVICETYPE *d ;

    new_tickle_smallest = 0x7fffffff; /* a suitably large integer */

    for (d = device_type_first(&iter, TRUE); d; d = device_type_next(&iter) ) {
      if (theIDevTypeTickle (d)) {
        if ((theIDevTypeTickleControl (d) += local_adjust) <= 0) {
          PROBE(SW_TRACE_TICKLE, (intptr_t)d,
                tickle_result = (*theIDevTypeTickle(d))(d, tickle_recursion)) ;
          switch (tickle_result) {
          case 0:
            break;
          case -3:
            tickle_loop = TRUE;
            break;
          default:
            break;
          }
        }
        if (theIDevTypeTickleControl (d) < new_tickle_smallest)
          new_tickle_smallest = theIDevTypeTickleControl (d);
      }
    }
    local_adjust = 0 ;

  } while (tickle_loop && tickle_recursion == 0 && (tickle_loop = FALSE,TRUE));

  -- tickle_recursion;

  * SwTimer += new_tickle_smallest - local_tickle_smallest;
  tickle_smallest = new_tickle_smallest;

  return (retval);
}

/* ---------------------------------------------------------------------- */

/** Same as SwOftenActivateSafe, but just doesn't do the async actions.
 *
 * Tickle can go recursive: once a unsafe tickle is called, all subsequent
 * tickles are unsafe as well.
 */
int32 RIPCALL SwOftenActivateUnsafe( void )
{
  corecontext_t*  corecontext;
  int32 ret;

  /** \todo Only the interpreter tickles.  Later, redesign. */
  if (((corecontext = get_core_context()) == NULL) || !corecontext->is_interpreter)
    return 0;

  block_async_action();
  ret =  SwOftenActivateSafeInternal();
  unblock_async_action();
  return ret;
}


void init_C_globals_devices(void)
{
  boot_pointer = NULL;
  boot_remaining = 0;
  tickle_recursion = -1;
  tickle_smallest = 1;
  tickle_loop = FALSE;
  async_unsafe = 0;
  default_timer = 0;

  /*
   Do not reset SwTimer as this is setup before SwMemInit(). Not ideal
   but thats how it works now. --johnk

#ifdef FAR_INITIALISE_BUG
  SwTimer = NULL ;
#else
  SwTimer = & default_timer ;
#endif
  pfSkinTimerExpired = NULL;
  */
}

/* Log stripped */
