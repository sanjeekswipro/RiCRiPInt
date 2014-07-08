/** \file
 * \ingroup interface
 *
 * $HopeName: SWcore_interface!pcl5:pcl5resources.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * Interface to gain access to PCL5 permanent macros and patterns outside
 * of the RIP.
 */

#ifndef __PCL5RESOURCES_H__
#define __PCL5RESOURCES_H__

#include "hqtypes.h"
#include "ripcall.h"
#include "swapi.h"
#include "swdevice.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Version numbers defined for the PCL5 resource interface.
 */
enum {
  SW_PCL5RESOURCES_VERSION_20080415 = 1 /**< Current version. */
  /* new versions go here */
} ;

/* Numeric ID's can be used for macros, fonts and patterns. */
typedef uint16 pcl5_resource_numeric_id ;

/* String ID's can be used for macros and fonts. */
typedef struct pcl5_resource_string_id {
  uint8 *buf ;
  int32 length ;
} pcl5_resource_string_id ;

/* ============================================================================
 * PCL5 devices
 * ============================================================================
 */

/* The device is a permanent device and can not be removed from the
   printer. If this bit is not set, the device can be removed from the
   printer between job boundries. For example, a USB flash drive. */
#define PCL5_DEVICE_IS_PERMANENT 0x1
/* The device is a writable device such as a hard disk or writable USB
   flash drive. Removable read only devices such as catridges would
   not be marked writable. */
#define PCL5_DEVICE_IS_WRITABLE  0x2


/* PCL5 predefined location types as per spec. When a PCL5 resource is
   searched for, location types are searched for in the following
   order:

   1. Downloaded
   2. Location type zero to N.

   Location type numbers do not need to be consecutive.

   We have numbered cartridge and SIMM as high numbers so that one can
   specify intermediate devices should the need arise. None-the-less,
   the devices with the below location types will be used for status
   read back. */

/* A cartridge device. */
#define PCL5_LOCATION_TYPE_CARTRIDGE 100
/* A SIMM device. */
#define PCL5_LOCATION_TYPE_SIMM 200

typedef struct pcl5_device pcl5_device ;

/* ============================================================================
 * PCL5 resources
 * ============================================================================
 */

/* PCL5 resource types. */
enum {
  SW_PCL5_FONT = 0,
  SW_PCL5_MACRO,
  SW_PCL5_PATTERN,
  SW_PCL5_SYMBOL_SET,
  SW_PCL5_FONT_EXTENDED,
  SW_PCL5_ALL
} ;

typedef struct pcl5_resource {
  /* One of the PCL5 resource types listed in the above
     enumeration. */
  int32 resource_type ;

  /* The numeric id of the resource, if one is used. */
  pcl5_resource_numeric_id numeric_id ;

  /* The string id of the resource, if one is used. */
  pcl5_resource_string_id string_id ;

  /* Is the resource permanent or not? */
  HqBool permanent ;

  /* The device this resource is on. */
  pcl5_device *device ;

  /* Private data for the resource implementation. Can be NULL. */
  void *private_data ;

  /* Function to free any private data. Can be NULL. */
  void (RIPCALL *PCL5FreePrivateData)(void *private_data) ;
} pcl5_resource ;

typedef struct pcl5_pattern {
  pcl5_resource detail ; /* MUST be first member of structure. */

  int32 width, height ;
  int32 x_dpi, y_dpi ;

  /* True when the pattern uses the palette (and is not fixed
     black/white). */
  HqBool color ;

  /* The highest-numbered pen used in the pattern; only valid for HPGL
   * patterns. */
  int32 highest_pen;

  /* The number of bits per pixel; this is only valid for color
     patterns, and is either 1 or 8 bits. */
  int32 bits_per_pixel ;

  /* The number of bytes in each line. */
  int32 stride ;

  /* Binary pattern data. Each line is padded to the nearest byte. */
  uint8* data ;
} pcl5_pattern ;

typedef struct pcl5_macro {
  pcl5_resource detail ; /* MUST be first member of structure. */
  struct pcl5_macro *alias ;
} pcl5_macro ;

struct pcl5_device {
  /* A numeric location type. */
  int16 location_type ;

  /* A numeric unit number on the above device location. */
  int16 location_unit ;

  /* A combination of the above PCL5_DEVICE_PROPERTY flags. */
  uint32 device_properties ;

  /**
   * \brief Obtain a pointer to the pattern data.
   *
   * \param[out] pattern The pattern which is wanted. The skin must allocate
   * and pass back a pattern.
   *
   * \retval TRUE if the pattern data was loaded, FALSE if the pattern
   * data could not be loaded.
   */
  HqBool (RIPCALL *PCL5LoadPattern)(pcl5_device *the_device, pcl5_resource_numeric_id numeric_id, pcl5_pattern **pattern) ;

  /**
   * \brief Unload the pattern data.
   *
   * The skin may unload the pattern data because the RIP no longer
   * requires it.
   */
  void (RIPCALL *PCL5UnloadPatternData)(pcl5_pattern *pattern) ;

  /**
   * \brief Open a macro for reading.
   *
   * \param[in] macro A pointer to a PCL5 resource structure. The skin
   * may access this structure for the duration of this function call.
   *
   * \param[in] macro_fd A pointer to a macro file descriptor. The
   * skin MUST fill this in. The skin uses this to track open macro
   * file descriptors. There will never be more than 3 open macros at
   * any one time. The same macro might be opened more than once.
   *
   * \retval 1 if the macro stream could be opened. 0 if the macro id
   * does not exist on this device, -1 for any other reason the macro
   * could not be opened (typically an IO error or memory allocation
   * error).
   */
  int32 (RIPCALL *PCL5OpenMacroStreamForRead)(pcl5_device *the_device, pcl5_resource_numeric_id numeric_id, DEVICE_FILEDESCRIPTOR *macro_fd) ;

  /**
   * \brief Read a chunk of bytes from a macro.
   *
   * \param[in] macro_fd The macro file descriptor.
   *
   * \param[in] buf The RIP will always provide a buffer in which the
   * skin code can copy buf_len bytes. The skin may also overwrite the
   * buf pointer with a new buffer pointer. The RIP will detect this
   * pointer change and will use that memory directly. This avoids any
   * byte copying (but either model may be used).
   *
   * \param[in] buf_len The RIP will set this to the size of its
   * provided buffer.
   *
   * \retval 0 for EOF, -1 if an error occurred otherwise the number
   * of bytes available within buf. The RIP will always consume all
   * bytes provided.
   */
  int32 (RIPCALL *PCL5ReadMacroStream)(DEVICE_FILEDESCRIPTOR macro_fd, uint8 **buf) ;

  /**
   * \brief Close a macro stream.
   *
   * \param[in] macro_fd The macro file descriptor.
   */
  void (RIPCALL *PCL5CloseMacroStream)(DEVICE_FILEDESCRIPTOR macro_fd) ;

  /**
   * \brief The OEM skin is required to implement this function.
   *
   * When the RIP calls this function it is about to start asking the
   * skin for an enumeration of all resources.
   *
   * \retval TRUE for success, FALSE for failure.
   */
  HqBool (RIPCALL *PCL5ResourceEnumerationStart)(pcl5_device *the_device, uint8 enumerate_resource_type) ;

  /**
   * \brief The OEM skin is required to implement this function.
   *
   * \param[in] the_resource A pointer to one of the resource
   * structures above held within the RIP. The skin code should fill
   * in the structure.
   *
   * \retval 1 if another enumeration has been returned. 0 means that
   * no more enumerations exist and the_resource ought to be set to
   * NULL, -1 if an error occurred.
   */
  int32 (RIPCALL *PCL5ResourceEnumerationNext)(pcl5_resource **the_resource) ;

  /**
   * \brief The OEM skin is required to implement this function.
   *
   * When the RIP calls this function it has completed enumerating
   * permanent resources (patterns and macros).
   */
  void (RIPCALL *PCL5ResourceFree)(pcl5_resource *the_resource) ;

} ;

/* ============================================================================
 * PCL5 resource interface. Optionally implemented by the OEM within
 * the skin.
 * ============================================================================
 */

/* RIP function which can be called to purge all permanent downloaded
   PCL5 resources from within the RIP. */
typedef void (RIPCALL *SwPCL5FlushPermanentResources_fn_t)(void) ;

typedef struct sw_pcl5_resources_api {
  sw_api_info info ; /**< Version number, name, display name, instance size. */

  /* The RIP will enumerate PCL5 devices at two different stages. RIP
     boot time (or printer power on) and just before the beginning of
     a PCL5 job.

     o At RIP boot time the RIP will ask for all permanent
       devices. These devices become the default device list for any
       PCL5 job.

     o Just before a PCL5 job begins, the RIP will ask for all
       removable devices. This list gets appended to the RIP's default
       device list for that PCL5 job making up all devices available
       to the job.

     o When the PCL5 job has completed, the RIP will make calls to
       PCL5DeviceFree() on all removable devices so that the skin can
       free any memory allocated.

     o When the RIP shuts down, the RIP will make calls to
       PCL5DeviceFree() on all permanent devices so that the skin can
       free any memory allocated. */
#define PCL5_DEVICE_ENUMERATE_PERMANENT   0x1
#define PCL5_DEVICE_ENUMERATE_REMOVABLE   0x2

#define PCL5_DEVICE_ENUMERATE_ALL_DEVICES ( \
  PCL5_DEVICE_ENUMERATE_PERMANENT | PCL5_DEVICE_ENUMERATE_REMOVABLE )

  /**
   * \brief The OEM skin is required to implement this function.
   *
   * When the RIP calls this function it is about to start asking the
   * skin for an enumeration of devices.
   *
   * \param[in] enumerate_device_type Which device types do we want an
   * enumeration of?
   *
   * \retval TRUE for success, FALSE for failure.
   */
  HqBool (RIPCALL *PCL5DeviceEnumerationStart)(uint8 enumerate_device_type) ;

  /**
   * \brief The OEM skin is required to implement this function.
   *
   * \param[in] the_device A pointer to one of the device structures
   * above needs to be allocated by the skin. The skin code should
   * fill in the structure with the appropriate details.
   *
   * \retval TRUE if another enumeration has been returned. FALSE
   * means that no more enumerations exist and the_device should be
   * set to NULL (although that is not required).
   */
  HqBool (RIPCALL *PCL5DeviceEnumerationNext)(pcl5_device **the_device) ;

  /**
   * \brief The OEM skin is required to implement this function.
   *
   * \param[in] the_device The RIP no longer needs the pointer to the
   * supplied device pointer. This is a hint to the skin that this
   * structure could be freed to save memory although the skin is not
   * required to do so.
   */
  void (RIPCALL *PCL5DeviceFree)(pcl5_device *the_device) ;

  /**
   * \brief Flush all permanent downloaded resources.
   *
   * The OEM skin is required to implement this function.
   *
   * The RIP will call this function immediately after the interface
   * has been registered. The skin should cache the function pointer
   * passed to it for future use.
   *
   * The skin can call the SwPCL5FlushPermanentResources() RIP
   * function to purge all permanent downloaded resources from within
   * the RIP between job boundries.
   */
  void (RIPCALL *PCL5RegisterFlushResourcesFunction)(SwPCL5FlushPermanentResources_fn_t function) ;
} sw_pcl5_resources_api ;

/**
 * \brief This routine makes a PCL5 resource implementation known to
 * the RIP. Only one instance can exist at any time.
 *
 * It can only be called once during RIP startup.
 */
sw_api_result RIPCALL SwRegisterPCL5Resources(sw_pcl5_resources_api *implementation) ;
/* Typedef with same signature. */
typedef sw_api_result (RIPCALL *SwRegisterPCL5Resources_fn_t)(sw_pcl5_resources_api *implementation) ;

#ifdef __cplusplus
}
#endif


#endif  /* __PCL5RESOURCES_H__ */
