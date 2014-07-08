/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5devs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of our internal PCL5 devices.
 */

#include "core.h"
#include "pcl5devs.h"
#include "pcl5resources.h"

/* There are always at least two PCL5 devices. The internal device and
   the downloaded device. These devices are managed within the RIP. */
#define INTERNAL_LOCATION_TYPE -200
#define INTERNAL_LOCATION_UNIT -2

#define DOWNLOADED_LOCATION_TYPE -100
#define DOWNLOADED_LOCATION_UNIT -2

/* Pointer to the skin interface, if registered. */
static sw_pcl5_resources_api *pcl5_resources_api = NULL ;

/* Initialised via pcl5_resourcecache_init() */
static pcl5_device internal_device ;
static pcl5_device downloaded_device ;

typedef struct PCL5DeviceProxy {
  /* Device details as provided by the skin or initernal and
     downloaded devices. */
  pcl5_device *details ;

  /* All resources on this device. Apart from the internal and
     downloaded devices, these will be NULL as we do not cache
     external resources within the RIP. */
  PCL5IdCache *pattern_cache ;
  PCL5IdCache *macro_cache ;

  /* Next device in list. */
  struct PCL5DeviceProxy *next ;
} PCL5DeviceProxy ;

/* Linked list of PCL5 devices. The list is kept in location type
   order (lowest at the beginning, highest at the end. */
static PCL5DeviceProxy *pcl5_device_proxies ;

/* ============================================================================
 * PCL5 device handling. In RIP proxy object for all PCL5 devices.
 * ============================================================================
 */

/* Returns TRUE if the device is found, FALSE if not found. */
static Bool find_device_proxy(pcl5_device *device, PCL5DeviceProxy **device_proxy)
{
  PCL5DeviceProxy *curr ;

  HQASSERT(device != NULL, "device is NULL") ;
  HQASSERT(device_proxy != NULL, "device_proxy is NULL") ;

  for (curr = pcl5_device_proxies; curr != NULL; curr = curr->next) {
    if (curr->details == device) {
      *device_proxy = curr ;
      return TRUE ;
    }
  }
  *device_proxy = NULL ;
  return FALSE ;
}

/* Returns zero if the device already exists. -1 on error otherwise 1
   for success. */
static int32 create_device_proxy(pcl5_device *device, PCL5DeviceProxy **device_proxy)
{
  PCL5DeviceProxy *new_proxy, *curr, *prev ;

  HQASSERT(device != NULL, "device is NULL") ;
  HQASSERT(device_proxy != NULL, "device_proxy is NULL") ;

  if (find_device_proxy(device, device_proxy)) {
    *device_proxy = NULL ;
    return 0 ;
  }

  if ((new_proxy = mm_alloc(mm_pcl_pool, sizeof(PCL5DeviceProxy), MM_ALLOC_CLASS_PCL_CONTEXT)) == NULL) {
    *device_proxy = NULL ;
    return -1 ;
  }

  new_proxy->details = device ;
  new_proxy->pattern_cache = NULL ;
  new_proxy->macro_cache = NULL ;
  new_proxy->next = NULL ;

  /* Insert the new device proxy into the proxies list keeping in
     location type order (lowest number to highest). */
  if (pcl5_device_proxies == NULL) {
    pcl5_device_proxies = new_proxy ;
  } else {
    prev = NULL ;
    for (curr = pcl5_device_proxies; curr != NULL; curr = curr->next) {
      if (curr->details->location_type > device->location_type) {
        new_proxy->next = curr ;
        if (curr == pcl5_device_proxies) {
          pcl5_device_proxies = new_proxy ;
        } else {
          HQASSERT(prev != NULL, "prev is NULL") ;
          prev->next = new_proxy ;
        }
        return 1 ;
      }
      prev = curr ;
    }

    /* Must be the very last entry. */
    HQASSERT(prev != NULL, "prev is NULL") ;
    prev->next = new_proxy ;
  }

  return 1 ;
}

static void destroy_all_device_proxies()
{
  PCL5DeviceProxy *curr, *next_proxy ;

  curr = pcl5_device_proxies ;
  while (curr != NULL) {
    next_proxy = curr->next ;

    if (pcl5_resources_api != NULL) {
      /* Allow skin to deallocate its device structures. */
      if (curr->details != &internal_device && curr->details != &downloaded_device) {
        pcl5_resources_api->PCL5DeviceFree(curr->details) ;
      }
    }

    mm_free(mm_pcl_pool, curr, sizeof(PCL5DeviceProxy)) ;
    curr = next_proxy ;
  }
  pcl5_device_proxies = NULL ;
}

/* ============================================================================
 * Init/Finish PCL5 devices.
 * ============================================================================
 */
void init_C_globals_pcl5devs(void)
{
  pcl5_resources_api = NULL ;

  /* Init in RIP PCL5 devices. */
  internal_device.location_type = INTERNAL_LOCATION_TYPE ;
  internal_device.location_unit = INTERNAL_LOCATION_UNIT ;
  internal_device.device_properties = PCL5_DEVICE_IS_PERMANENT ;
  internal_device.PCL5LoadPattern = NULL ;
  internal_device.PCL5UnloadPatternData = NULL ;
  internal_device.PCL5OpenMacroStreamForRead = NULL ; /* No internal macros */
  internal_device.PCL5ReadMacroStream = NULL ; /* No internal macros */
  internal_device.PCL5CloseMacroStream = NULL ; /* No internal macros */
  internal_device.PCL5ResourceEnumerationStart = NULL ;
  internal_device.PCL5ResourceEnumerationNext = NULL ;
  internal_device.PCL5ResourceFree = NULL ;

  downloaded_device.location_type = DOWNLOADED_LOCATION_TYPE ;
  downloaded_device.location_unit = DOWNLOADED_LOCATION_UNIT ;
  downloaded_device.device_properties = PCL5_DEVICE_IS_PERMANENT | PCL5_DEVICE_IS_WRITABLE ;
  downloaded_device.PCL5LoadPattern = NULL ;
  downloaded_device.PCL5UnloadPatternData = NULL ;
  downloaded_device.PCL5OpenMacroStreamForRead = NULL ;
  downloaded_device.PCL5ReadMacroStream = NULL ;
  downloaded_device.PCL5CloseMacroStream = NULL ;
  downloaded_device.PCL5ResourceEnumerationStart = NULL ;
  downloaded_device.PCL5ResourceEnumerationNext = NULL ;
  downloaded_device.PCL5ResourceFree = NULL ;

  pcl5_device_proxies = NULL ;
}

Bool pcl5_devices_init(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  PCL5DeviceProxy *temp_proxy ;
  pcl5_device *next_external_device ;

  UNUSED_PARAM(PCL5_RIP_LifeTime_Context*, pcl5_rip_context) ;

  if (create_device_proxy(&internal_device, &temp_proxy) != 1)
    goto cleanup ;

  if (create_device_proxy(&downloaded_device, &temp_proxy) != 1)
    goto cleanup ;

  /* The skin has registered a resources API. Enumerate all permanent
     devices at RIP boot time. */
  if (pcl5_resources_api != NULL) {
    if (! pcl5_resources_api->PCL5DeviceEnumerationStart(PCL5_DEVICE_ENUMERATE_PERMANENT))
      goto cleanup ;

    while (pcl5_resources_api->PCL5DeviceEnumerationNext(&next_external_device)) {
      if (create_device_proxy(next_external_device, &temp_proxy) != 1)
        goto cleanup ;
    }
  }

  return TRUE ;

 cleanup:
  destroy_all_device_proxies() ;
  return FALSE ;
}

void pcl5_devices_finish(PCL5_RIP_LifeTime_Context *pcl5_rip_context)
{
  UNUSED_PARAM(PCL5_RIP_LifeTime_Context*, pcl5_rip_context) ;

  destroy_all_device_proxies() ;

  pcl5_device_proxies = NULL ;
  pcl5_resources_api = NULL ;
}

/* ============================================================================
* Log stripped */
