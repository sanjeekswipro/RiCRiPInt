/** \file
 * \ingroup devices
 *
 * $HopeName: COREdevices!export:devices.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to the core "device" subsystem
 */

#ifndef __DEVICES_H__
#define __DEVICES_H__

#include "swdevice.h" /* Needs DEVICELIST */

struct core_init_fns ;

/**
 * \defgroup devices Device subsystem.
 * \ingroup core
 * \brief
 * Harlequin device abstraction.
 * API defined in \ref devices.h
 *
 * A software module that provides a specific set of I/O services for the RIP.
 */

/** Runtime initialisation for devices. */
void devices_C_globals(struct core_init_fns *fns) ;

/*----------------------------- DEVICE TYPES --------------------------------*/

#define isProtectedDeviceNumber(_n) ((_n) == FONT_ND_CRYPT_DEVICE_TYPE || \
                                     (_n) == SEMAPHORE_DEVICE_TYPE)

/**
 * This call should be made to add a new device type. Device types can be
 * added at any time, and will be included in the tickle list if they have a
 * tickle procedure. Note that PostScript requires the skin to supply an OS
 * device.
 * \param[in] devtype Device type to be added
 * \return    Success or failure
 */
Bool device_type_add(DEVICETYPE *devtype);

/**
 * Find a device type, by number. Include protected device types if
 * protected_ok is TRUE (this is FALSE for PostScript operator calls).
 * \param[in] devnum        The device number to search for
 * \param[in] protected_ok  Can we search protected devices ?
 * \return                  The device found or NULL if not found
 */
DEVICETYPE *find_device_type(int32 devnum, int32 protected_ok) ;

/** \name Device type iterators
 * Iterators for the device type list. Slightly more complicated than the
 * device iterator, because the next pointer is not visible in the DEVICETYPE
 * structure. The functions return NULL when there are no more devices that
 * match the search criteria. If protected_ok is set, then protected device
 * types (font decryption, etc) will be included in the iteration. The device
 * type list may *not* be modified while an iterator is active. The contents
 * of the iterator structure are private and should not be inspected or
 * modified by clients.
 */
/** \{ */

typedef struct device_type_iterator_t {
  struct devicetypelist *current ;
  int32 flags ;
} device_type_iterator_t, *device_type_iterator_h;

/** Find the first device */
DEVICETYPE *device_type_first(device_type_iterator_h iter, int32 protected_ok);
/** Find the next device */
DEVICETYPE *device_type_next(device_type_iterator_h iter);

/** \}*/

/** Predicate to determine if any devices support tickles */
Bool device_tickles_required(void);

/** Report any devicetypes using a tickle function */
void device_type_report_ticklefuncs(void);

/*--------------------------- DEVICE INSTANCES ------------------------------*/

/** Find a device, by a (null-terminated) name */
/*@dependent@*/
DEVICELIST *find_device( /*@notnull@*/ /*@in@*/ uint8 *name ) ;

/**
 * Iterators for device list. These are very simple, and serve to keep
 * manipulation of the device list contained. The routines return NULL when
 * there are no more devices that match the search criteria. If any flags are
 * set, then only devices with those flags set will be returned. The device
 * list may *not* be modified while an iterator is active. The contents of
 * the iterator structure are private and should not be inspected or modified
 * by clients.
 */
typedef struct device_iterator_t {
  /*@dependent@*/
  DEVICELIST *current ;
  int32 flags ;
} device_iterator_t, *device_iterator_h ;

DEVICELIST *device_first(device_iterator_h iter, int32 flags) ;
DEVICELIST *device_next(device_iterator_h iter) ;

/**
 * Functions to maintain device list. These merely allocate and add or
 * deallocate and remove the device structure to or from the device list.
 * They do not perform device initialisation (see device_connect()).
 */
DEVICELIST *device_mount(uint8 *device_name) ;
Bool device_dismount(uint8 *device_name) ;

/**
 * Raw devicelist allocation and de-allocation. These functions do not add or
 * remove the device structure to/from the device list.
 */
DEVICELIST *device_alloc(uint8 *device_name, size_t len) ;
void device_free(DEVICELIST *dev) ;

/**
 * Helper functions to initialise and add devices to device list.
 * device_connect() can be called on a mounted or stack allocated device to
 * connect a device to a the device type and initialise the device. If it
 * fails, it leaves the device connected to the null device type, with an
 * appropriate error ready for immediate retrieval by theILastErr() or
 * signalling through device_error_handler().
 */
Bool device_connect(DEVICELIST *dev, int32 devnum, char *name, int32 flags,
                    Bool protected_ok) ;

/**
 * Add a device to the searchable device list. Devices may be allocated and
 * referenced privately (generic filter devices do this), or may be added to
 * the standard device list.
 */
void device_add(DEVICELIST *dev) ;

/**
 * Add a device to the front of the searchable device list.
 */
void device_add_first(DEVICELIST* dev);

/**
 * The standard devices; %os% and %null%.  The %os% device is used for
 * all open()'s of working files.  The devices_init() routine creates
 * and enables an entry for the OS_DEVICE_TYPE in osdevice.  The
 * osdevice and nulldevice are added to the searchable device list by
 * devices_init().
 */
extern DEVICELIST *osdevice, *nulldevice;


/*--------------------------- PROGRESS DEVICE ------------------------------*/

/**
 * The progress device is used to send reports to the skin. It is
 * automagically detected, when a device of name "progress" is connected to a
 * device type. Modules reporting progress should test that this pointer is
 * not NULL before trying to send progress reports to it.
 */
extern /*@null@*/ DEVICELIST *progressdev ;

/*--------------------------- DEVICE ERRORS ------------------------------*/

/** Get last error from device and translate to standard CORE error call. */
Bool device_error_handler( DEVICELIST *dev ) ;


/*--------------------------- IMPORTED FUNCTIONS ----------------------------*/

/**
 * This callback is performed when a device is to be removed from the devices
 * list. It should return TRUE if it is OK to remove the device, or FALSE and
 * signal a core error if it is not OK (e.g. there are still open files on the
 * device).
 */
Bool device_closing(DEVICELIST *dev) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
