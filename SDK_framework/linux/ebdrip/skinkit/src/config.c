/* Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:config.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Implementation of the config (configuration) device type.
 *
 * This is a minimal implementation of the config device
 * type. It provides all the required functions, but only limited
 * functionality.
 *
 * The purpose of the configuration device type is twofold:
 *
 *  - to set up the environment in which PostScript jobs run
 *  - to provide the RIP with jobs to run.
 *
 * In addition, this file illustrates the use of tickle functions to
 * provide interrupts to the interpreter.
 *
 * The RIP simply runs the configuration file. Therefore the read
 * function of the configuration device provides PostScript fragments
 * which the RIP executes. Like a PostScript procedure, this
 * PostScript is expected to manipulate the operand stack. In
 * particular it consumes no operands, and leaves a boolean object on
 * top of the stack. If the boolean is true, two further items are
 * expected on the stack: a pair of file objects, the first of which
 * is a file open for reading which is the PostScript job to be
 * executed, and second is a file opened for writing, to which
 * anything written to PostScript's %stdout% pseudo-device will be
 * directed. If the boolean is false, the system will re-run the
 * config device again.
 *
 * Therefore the read call for the config device could just return in
 * the supplied buffer the characters
 *
 * <pre>
 *   (%%console%)(r) file (%%console%)(w) file true
 * </pre>
 *
 * on the first call; and no characters on the second call (indicating
 * end-of-file to the RIP) - and the RIP would then act on the
 * PostScript shown as described above, causing it to read PostScript
 * from a file opened on a device called %%console%. %%console% must
 * have been mounted and had its device type set in order for this to
 * succeed.  Normally this would be done in the file
 * %%os%Sys/ExtraDevices, but could be done anywhere - for example it
 * could be included in the PostScript emitted by the %%config%
 * device.
 *
 * Normally, however, the PostScript emitted by %%config% will be
 * somewhat more complex. The device may require to choose its input
 * from among several possible input sources; and it will certainly
 * want to set up some default environment in which the job may
 * run. Even if the PostScript emitted is quite simple, the
 * implementation of the %%config% device may be quite complex -
 * involving perhaps manipulating user interface dialogs which allow
 * the user to select the job to be run.
 *
 * A useful technique appropriate in some circumstances is to implement
 * the function of the config device wholly in PostScript in which case
 * the PostScript emitted by the %config% device would simply be
 *
 * <pre>
 *   (%%os%my-config-file) run
 * </pre>
 * and the PostScript file my-config-file on the %%os% device would then
 * be responsible for putting the two files and true onto the operand
 * stack.
 *
 * However, in our example below, we obtain this configuration
 * PostScript by calling KGetConfigData(), repeatedly if necessary.
 * This returns the PostScript provided to SwLeJobStart().
 *
 * Finally, in order for the RIP to run %%config%, the device must
 * exist.  It should be created (that is, mounted, enabled and
 * assigned a device type number) in the Sys/ExtraDevices file, using
 * PostScript similar to this:
 *
 * <pre>
 *   statusdict begin
 *   (%config%) dup devmount pop
 *   <<
 *     /Password 0
 *     /DeviceType 16\#ffff0001
 *     /Enable true
 *   >> setdevparams
 *   end
 * </pre>
 *
 * (See ripthread.h for a discussion of how 16\#ffff0001 arises as the
 * device type number).
 *
 * This config device implementation accepts no device parameters. It
 * is a non-relative device, so therefore many routines can be
 * provided as stubs only.  Furthermore, it will not be written to by
 * the RIP as supplied so that functionality need not be
 * supported. (However, you might consider implementing a write call on
 * %%config% for your own use, in place of a separate device type to
 * receive PostScript messages written to standard error or standard
 * output - see the discussion alongside the definition of the %%console%
 * device in \ref monitor.c).
 *
 * Note however that config_bytes_file() (bytesavailable in
 * PostScript) MUST be implemented for the config device in order that
 * the RIP can determine whether or not to run the %%config% file -
 * see Programmer's Reference Manual Chapter 7 entitled "The
 * Configuration Device".
 *
 * This example also illustrates the use of the buffer size function,
 * config_buffsize().  This is not a requirement, but since only a
 * very small buffer is required in this example, we choose not to
 * allow the RIP to waste space on a larger buffer as it would if this
 * routine were not implemented.
 */

/* ----------------------------------------------------------------------
 * Include files, principally the swdevice.h header exported from the
 * corerip.
 */

#include "kit.h"
#include "ripthread.h"
#include "swdevice.h"
#include "skindevs.h"
#ifdef LESEC
#include "lesec.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef UNIX
#include <unistd.h>
#endif

#define CONFIG_BUFF_SIZE 1024

static int32 config_fd = 0;   /**< "file descriptor" for config device */

/* group these for same doc? */
#define CONFIG_FIRST 0
#define CONFIG_START 1
#define CONFIG_DATA  2
#define CONFIG_END   3

/**
 * \brief Used to maintain the state of the device between read
 * calls.
 *
 * For a more comprehensive implementation this would be
 * considerably more complex since more than one buffer full of
 * information may have to be returned by the read file function.
 */
static int32 config_status = CONFIG_FIRST;

/* ----------------------------------------------------------------------
  All the routines for the device types can be static within the file because
  they are only referenced via the device type structure.
*/

static int32 RIPCALL config_ioerror       ( DEVICELIST *dev );
static int32 RIPCALL config_ioerror_false ( DEVICELIST *dev );
static int32 RIPCALL config_init          ( DEVICELIST *dev );
static void * config_void         ( DEVICELIST *dev );

static DEVICE_FILEDESCRIPTOR RIPCALL config_open_file     ( DEVICELIST *dev,
                                                       uint8 *filename,
                                                       int32 openflags );
static int32 RIPCALL config_read_file     ( DEVICELIST *dev,
                                            DEVICE_FILEDESCRIPTOR descriptor,
                                            uint8 *buff,
                                            int32 len );
static int32 RIPCALL config_write_file    ( DEVICELIST *dev,
                                            DEVICE_FILEDESCRIPTOR descriptor,
                                            uint8 *buff,
                                            int32 len );
static int32 RIPCALL config_close_file    ( DEVICELIST *dev,
                                            DEVICE_FILEDESCRIPTOR descriptor);
static int32 RIPCALL config_seek_file     ( DEVICELIST *dev,
                                            DEVICE_FILEDESCRIPTOR descriptor,
                                            Hq32x2 *destination,
                                            int32 flags);
static int32 RIPCALL config_bytes_file    ( DEVICELIST *dev,
                                            DEVICE_FILEDESCRIPTOR descriptor,
                                            Hq32x2 * bytes,
                                            int32 reason);
static int32 RIPCALL config_status_file   ( DEVICELIST *dev,
                                            uint8 *filename,
                                            STAT *statbuff );
static void* RIPCALL config_start_file_list ( DEVICELIST *dev,
                                              uint8 *pattern);
static int32 RIPCALL config_next_file     ( DEVICELIST *dev,
                                            void **handle,
                                            uint8 *pattern,
                                            FILEENTRY *entry);
static int32 RIPCALL config_end_file_list ( DEVICELIST *dev,
                                            void * handle);
static int32 RIPCALL config_rename_file   ( DEVICELIST *dev,
                                            uint8 *file1,
                                            uint8 *file2);
static int32 RIPCALL config_delete_file   ( DEVICELIST *dev,
                                            uint8 *filename );
static int32 RIPCALL config_set_param     ( DEVICELIST *dev,
                                            DEVICEPARAM *param);
static int32 RIPCALL config_start_param   ( DEVICELIST *dev );
static int32 RIPCALL config_get_param     ( DEVICELIST *dev,
                                            DEVICEPARAM *param);
static int32 RIPCALL config_status_device ( DEVICELIST *dev,
                                            DEVSTAT *devstat);
static int32 RIPCALL config_buffsize      ( DEVICELIST *dev );
static int32 RIPCALL config_spare         ( void );


/* ---------------------------------------------------------------------- */

/** \brief The config device type structure. */
DEVICETYPE Config_Device_Type = {
  CONFIG_DEVICE_TYPE,     /**< the device ID number */
  0,                      /**< flags to indicate specifics of device */
  0,                      /**< the size of the private data */
  0,                      /**< minimum ticks between tickle functions */
  NULL,                   /**< procedure to service the device */
  skindevices_last_error, /**< return last error for this device */
  config_init,            /**< call to initialise device */
  config_open_file,       /**< call to open file on device */
  config_read_file,       /**< call to read data from file on device */
  config_write_file,      /**< call to write data to file on device */
  config_close_file,      /**< call to close file on device */
  config_close_file,      /**< call to abort action on the device */
  config_seek_file,       /**< call to seek file on device */
  config_bytes_file,      /**< call to get bytes avail on an open file */
  config_status_file,     /**< call to check status of file */
  config_start_file_list, /**< call to start listing files */
  config_next_file,       /**< call to get next file in list */
  config_end_file_list,   /**< call to end listing */
  config_rename_file,     /**< rename file on the device */
  config_delete_file,     /**< remove file from device */
  config_set_param,       /**< call to set device parameter */
  config_start_param,     /**< call to start getting device parameters */
  config_get_param,       /**< call to get the next device parameter */
  config_status_device,   /**< call to get the status of the device */
  config_init,            /**< call to dismount the device */
  config_buffsize,        /**< call to return buffer size */
  NULL,                   /**< ioctl slot (optional) */
  config_spare,           /**< spare slot */
};

/* ---------------------------------------------------------------------- */

/** \brief The stub functions for the configuration device to provide appropriate
   return codes for slots which have nothing else to do.
*/

static void config_set_last_error(int32 error)
{
  skindevices_set_last_error(error);
}

static int32 RIPCALL config_ioerror( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  config_set_last_error(DeviceIOError);
  return -1;
}

static int32 RIPCALL config_ioerror_false( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  config_set_last_error(DeviceIOError);
  return FALSE;
}

static int32 RIPCALL config_noerror( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  config_set_last_error(DeviceNoError);
  return 0;
}

static int32 RIPCALL config_init( DEVICELIST *dev )
{
  config_status = CONFIG_FIRST;
  config_fd = 0;

  return config_noerror(dev);
}

static void * config_void( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);
  config_set_last_error(DeviceNoError);
  return NULL;
}

 /* Currently interrupt_signal_seen is not used anywhere, so we don't
    register the tickle function and we do not define the tickle
    function so that gcc does not warn about an unused function, but I
    did want to keep the function in place along with its
    comments.  */
#ifdef CONFIG_TICKLE_FUNCTION
/**
 * \brief The tickle function for the config device type. See
 * Programmer's Reference Manual 5.5 and 5.8.5. See also
 * DEVICELIST_TICKLE().
 *
 * Tickle functions are associated with device types not devices
 * themselves. Indeed, there could only be one tickle routine in the
 * whole program which services all asynchronous actions, but some
 * need to do minimal work very frequently, and others a lot of work
 * infrequently.
 *
 * They are associated with device types because their function is
 * frequently associated with the action of a device: for example, an
 * AppleTalk device type needs to "tickle" its connection periodically
 * (hence the name), and the pagebuffer device may require to service
 * interrupts from a printer.
 *
 * However there are also general actions such as servicing
 * user-interface events which can be attached to any suitable device,
 * and this is a simple example, where the user-interface is just the
 * primitive action of signalling an interrupt.
 *
 * A signal handler in the main program (example.c) catches the signal
 * and records the fact in a global variable. Then when the tickle
 * function is called some time later, it just returns the appropriate
 * code to the caller, and its clears the flag.
 *
 * Tickle functions need to be as efficient as possible: time given to
 * them is time lost to the RIP. Also any necessary time-consuming
 * actions must contain calls to the SwOften macro so that other
 * tickle functions are not locked out by this one. However, this
 * example is simple enough not to require that (and is also the onl
 * tickle function).
 */
static int32 RIPCALL config_tickle(DEVICETYPE *devicetype, /* not DEVICELIST */
                                   int32 recursion )
{
  UNUSED_PARAM(DEVICETYPE *, devicetype);
  UNUSED_PARAM(int32, recursion);

  if ( ! interrupt_signal_seen )
    return 0;

  interrupt_signal_seen = FALSE;
  return -1; /* indicate interrupt */
}
#endif

/**
 * \brief The open file routine for the config device type. See DEVICELIST_OPEN().
 *
 * Since we are not implementing write functionality, it will be an
 * error to attempt an open for other than reading. Also the semantics
 * of the device mean that it does not make sense to have more than
 * one file open at once for the whole device type, therefore this too
 * is an error.
 */

static DEVICE_FILEDESCRIPTOR RIPCALL config_open_file(DEVICELIST *dev,
                                                      uint8 *filename,
                                                      int32 openflags)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(uint8 *, filename);

  config_set_last_error(DeviceNoError);
  if ( ! (openflags & SW_RDONLY) || config_fd > 0 ) {
    config_set_last_error(DeviceIOError);
    return -1;
  }

  /* reset the status if need be */
  if ( config_status == CONFIG_END ) {
    config_status = CONFIG_START;
  }

  config_fd = 1;
  return config_fd;
}


/**
 * \brief The read_file routine for the config device type. See DEVICELIST_READ().
 *
 * On the first read, we emit the PostScript required to set up the
 * environment and set up the two files and true on the operand stack
 * as described at the head of the file.
 *
 * On the second call we return zero bytes which indicates end of file
 * to the RIP.
 */

static int32 RIPCALL config_read_file( DEVICELIST *dev, DEVICE_FILEDESCRIPTOR descriptor,
                                       uint8 *buff, int32 len )
{
  uint8 * pAppPS;
  uint32  cbAppPS;

  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  config_set_last_error(DeviceNoError);

  if (descriptor != config_fd) {
    config_set_last_error(DeviceIOError);
    return -1;
  }

  switch ( config_status ) {
  case CONFIG_FIRST : {
    config_status = CONFIG_START;
#ifdef EMBEDDED
    sprintf( (char *)buff,
             "<</RevisionPassword 0 "
             "/PlatformPassword 0 "
             "/Password 0>> setsystemparams ");

    return (int32)strlen((char *)buff);
#else
#ifdef LESEC
    SwSecGetConfig( buff, len );
    return (int32)strlen((char *)buff);
#else
    /* RevisionPassword and PlatformPassword are set in ExtraStart file */
    /* FALLTHROUGH */
#endif
#endif
  }

  case CONFIG_START :
    { /* Reset the softwareiomode */
      static const char iomode[] = "statusdict begin 0 setsoftwareiomode end\n";
      memcpy(buff, iomode, sizeof(iomode)-1);
      config_status = CONFIG_DATA;
      return (int32)(sizeof(iomode)-1);
    }

  case CONFIG_DATA :
    /* Acquire (another) chunk of config data */
    if (KGetConfigData(len, &pAppPS, &cbAppPS))
    {
      memcpy(buff, pAppPS, CAST_UNSIGNED_TO_SIZET(cbAppPS));
      return (int32)cbAppPS;
    }

    config_status = CONFIG_END;
    /* FALLTHROUGH */

  case CONFIG_END :
    return 0; /* end of file */

  }

  return -1;
}

/**
 * \brief Stub with correct protopype function pointer. See DEVICELIST_WRITE().
 */
static int32 RIPCALL config_write_file(DEVICELIST *dev,
                                       DEVICE_FILEDESCRIPTOR descriptor,
                                       uint8 *buff, int32 len )
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(uint8 *, buff);
  UNUSED_PARAM(int32, len);

  return config_ioerror(dev);
}


/**
 * \brief The close_file routine for the config device type. See DEVICELIST_CLOSE().
 */
static int32 RIPCALL config_close_file(DEVICELIST *dev,
                                       DEVICE_FILEDESCRIPTOR descriptor )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  config_set_last_error(DeviceNoError);

  if (descriptor != config_fd) {
    /* it was not already open */
    config_set_last_error(DeviceIOError);
    return -1;
  }

  config_fd = -1;
  config_status = CONFIG_START;

  return 0;
}


/**
 * \brief The seek_file routine for the config device type. See DEVICELIST_SEEK().
 */
static int32 RIPCALL config_seek_file(DEVICELIST *dev,
                                      DEVICE_FILEDESCRIPTOR descriptor,
                                      Hq32x2 *destination,
                                      int32 flags)
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);
  UNUSED_PARAM(Hq32x2 *, destination);
  UNUSED_PARAM(int32, flags);

  return config_ioerror_false(dev);
}


/**
 * \brief The bytes_file routine for the config device type, the
 * manifestation in C of the bytesavailable PostScript operator.
 * See DEVICELIST_BYTES().
 *
 * This routine has special significance for the config device. This
 * implementation simply returns the length of the PostScript string
 * we would supply. If the amount of data which will be emitted is not
 * known exactly, any strictly positive number (e.g. 1) will cause the
 * RIP to proceed to open the %config% file rather than continuing to
 * idle.
 *
 * If there is no input pending, this function should return 0 (NOT -1
 * which would be an error condition), so that the RIP can continue to
 * do any idle-time tasks allocated to it. It will then call
 * bytesavailable at frequent intervals until there is some input
 * pending. This is preferable to blocking either here or in the read
 * or open functions when this condition arises.
 */
static int32 RIPCALL config_bytes_file(DEVICELIST *dev,
                                       DEVICE_FILEDESCRIPTOR descriptor,
                                       Hq32x2 * bytes, int32 reason )
{
  int32 nBytes;

  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, descriptor);

  if ( reason == SW_BYTES_TOTAL_ABS )
  {
    config_set_last_error(DeviceIOError);
    return FALSE;
  }
  /* else reason == SW_BYTES_AVAIL_REL */

  config_set_last_error(DeviceNoError);
  nBytes = KGetConfigAvailable();
  Hq32x2FromInt32(bytes, nBytes);
  return TRUE;
}

/**
 * \brief Call to check status of file. See DEVICELIST_STATUS_FILE().
 */
static int32 RIPCALL config_status_file(DEVICELIST *dev,
                                        uint8 *filename,
                                        STAT *statbuff )
{
  UNUSED_PARAM(uint8 *, filename);
  UNUSED_PARAM(STAT *, statbuff);

  return config_ioerror(dev);
}

/**
 * \brief Call to start listing files. See DEVICELIST_START_LIST().
 */
static void* RIPCALL config_start_file_list(DEVICELIST *dev,
                                            uint8 *pattern)
{
  UNUSED_PARAM(uint8 *, pattern);

  return config_void(dev);
}


/**
 * \brief The buffsize routine for the config device type.  Not
 * required, but provided for example purposes.  The 'len' parameter
 * of the config_read_file() function will never exceed the amount we
 * indicate with this function. See DEVICELIST_BUFFER_SIZE().
 */
static int32 RIPCALL config_buffsize( DEVICELIST *dev )
{
  UNUSED_PARAM(DEVICELIST *, dev);

  config_set_last_error(DeviceNoError);
  return CONFIG_BUFF_SIZE;
}


/**
 * \brief Call to get next file in list. See DEVICELIST_NEXT().
 */
static int32 RIPCALL config_next_file(DEVICELIST *dev,
                                      void **handle,
                                      uint8 *pattern,
                                      FILEENTRY *entry)
{
  UNUSED_PARAM(void **, handle);
  UNUSED_PARAM(uint8 *, pattern);
  UNUSED_PARAM(FILEENTRY *, entry);

  return config_noerror(dev);
}

/**
 * \brief Call to end listing. See DEVICELIST_END_LIST().
 */
static int32 RIPCALL config_end_file_list(DEVICELIST *dev,
                                          void * handle)
{
  UNUSED_PARAM(void **, handle);

  return config_noerror(dev);
}

/**
 * \brief Rename file on the device. See DEVICELIST_RENAME().
 */
static int32 RIPCALL config_rename_file(DEVICELIST *dev,
                                        uint8 *file1,
                                        uint8 *file2)
{
  UNUSED_PARAM(uint8 *, file1);
  UNUSED_PARAM(uint8 *, file2);

  return config_ioerror(dev);
}

/**
 * \brief Remove file from device. See DEVICELIST_DELETE().
 */
static int32 RIPCALL config_delete_file(DEVICELIST *dev,
                                        uint8 *filename )
{
  UNUSED_PARAM(uint8 *, filename);

  return config_ioerror(dev);
}

/**
 * \brief Call to set device parameter. See DEVICELIST_SET_PARAM().
 */
static int32 RIPCALL config_set_param(DEVICELIST *dev,
                                      DEVICEPARAM *param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  config_set_last_error(DeviceNoError);

  return ParamIgnored;
}

/**
 * \brief Call to start getting device parameters. See DEVICELIST_START_PARAM().
 */
static int32 RIPCALL config_start_param(DEVICELIST *dev)
{
  UNUSED_PARAM(DEVICELIST *, dev);

  config_set_last_error(DeviceNoError);

  return 0; /* There are no parameters for this device. */
}

/**
 * \brief Call to start getting device parameters. See DEVICELIST_GET_PARAM().
 */
static int32 RIPCALL config_get_param(DEVICELIST *dev,
                                      DEVICEPARAM *param)
{
  UNUSED_PARAM(DEVICELIST *, dev);
  UNUSED_PARAM(DEVICEPARAM *, param);

  config_set_last_error(DeviceNoError);

  return ParamIgnored;
}

/**
 * \brief Call to get the status of the device. DEVICELIST_STATUS_DEVICE().
 */
static int32 RIPCALL config_status_device(DEVICELIST *dev,
                                          DEVSTAT *devstat)
{
  UNUSED_PARAM(DEVSTAT *, devstat);

  return config_ioerror(dev);
}

/**
 * \brief Spare slot. See DEVICELIST_SPARE().
 */
static int32 RIPCALL config_spare(void)
{
  config_set_last_error(DeviceNoError);
  return 0;
}


/* end of config.c */
