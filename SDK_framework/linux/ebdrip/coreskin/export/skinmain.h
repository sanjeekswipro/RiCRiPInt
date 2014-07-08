#ifndef __SKINMAIN_H__
#define __SKINMAIN_H__

/* $HopeName: SWcoreskin!export:skinmain.h(EBDSDK_P.1) $
 * Copyright 2003-2010 Global Graphics Software.  All rights reserved.
 *
* Log stripped */

/* ----------------------------- Includes ---------------------------------- */
#include "product.h"    /* product configuration */
#include "coreskin.h"   /* includes std.h ... */

/* v20iface */
#include "swdevice.h"   /* DEVICETYPE */

/* ggcore */
#include "fwstring.h"   /* FWSTR_TEXTSTRING */

/* psio */
#include "psputype.h"   /* PS_METH_HANDLE */


#ifdef __cplusplus
extern "C" {
#endif


/* ----------------------------- Macros ------------------------------------ */

/* Useful stuff for using PSIO methods throughout the coreskin */

#define CPS_NMSV_METH   (fs_handle)
#define CPS_DFLT_METH   (os_handle)
#define CPS_FGUI_METH   (gs_handle)

#define CPS_PROF_METH   (prof_handle)


#define METHOD_READ_MODE   (SW_RDONLY)
#define METHOD_WRITE_MODE  (SW_CREAT | SW_EXCL | SW_RDWR)
#define METHOD_CLIENT_DATA (NULL)

#define PS_read_file(filename, meth_handle, pinst_handle, pobj_handle)  \
  (PS_open_read_file((filename), METHOD_READ_MODE,                      \
                     (meth_handle), NULL,                               \
                     (pinst_handle), (pobj_handle)))


#define PS_write_file(filename, inst_handle)                             \
  (PS_open_write_file((filename),                                        \
                      METHOD_WRITE_MODE,                                 \
                      TRUE,                                              \
                      (inst_handle)))


#define PS_create(meth_handle, pinst_handle, pobj_handle)   \
  (PS_create_inst((meth_handle), NULL,                      \
                  (pinst_handle), (pobj_handle)))


/* ----------------------------- Types ------------------------------------- */

typedef struct CRDeviceTypeList
{
  struct CRDeviceTypeList * pNext;
  uint32                    nDevTypes;
  struct DeviceType      ** ppDevTypes;
} CRDeviceTypeList;


/* ----------------------------- Data -------------------------------------- */

extern struct devicelist *      pBootFSDevice;
extern struct devicelist *      pBootOSDevice;

extern struct DeviceType        Exec_Device_Type;
extern struct DeviceType        Monitor_Device_Type;
extern struct DeviceType        PageBuffer_Device_Type;
extern struct DeviceType        Config_Device_Type;
extern struct DeviceType        Progress_Device_Type;
extern struct DeviceType        Input_Plugin_Device_Type;
extern struct DeviceType        Input_Plugin_Filter_Device_Type;

/* GUI filename of SW folder */
extern FwTextString ptbzGuiSWDir;

extern PS_METH_HANDLE fs_handle;
extern PS_METH_HANDLE os_handle;
extern PS_METH_HANDLE gs_handle;
extern PS_METH_HANDLE prof_handle;

extern PS_METH_HANDLE subfile_filter_handle;

/* IOFile stream subclass which supports decryption. */
extern struct FwClassRecord  DecryptFileStreamClass;


/* ----------------------------- Functions --------------------------------- */

/* The bootup sequence should be as follows
 * - platform dependent, product dependent     - main (or WinMain) called
 *   This should be provided by each product dependent skin.
 *   This in turn calls
 *
 * - platform dependent, product independent   - control_main
 *   These should live in coreskin/[pc|unix|mac]/src/ctrlmain.c
 *   This in turn calls
 *
 * - platform independent, product dependent   - pds_main
 *   This should be provided by each product dependent skin.
 *   This in turn calls
 *
 * - platform independent, product independent - coreskin_main
 */
extern void pds_main( void );

extern void coreskin_main( void );

extern int32 coreskinStartingUp( void );
extern int32 coreskinShuttingDown( void );

extern uint32 coregui_networkBuffer( uint8 ** bufferp );
extern uint32 coregui_getUsedPrinterBuffer( void );

extern void coregui_printmemorystatus( void );

extern int32 register_object_reference(char * pObjectRef);
extern int32 register_instance_number(char * pszInstance);
extern int32 register_baseport_number(char * pszBasePort);
extern int32 register_hostname(char * pszHostname);
extern int32 register_force(const char* pszForceOption);

#ifdef PRODUCT_HAS_API
extern int32 coreskinInitilizeRipOrb( void );
#endif

extern void coreskin_check_for_quit( void );

extern int32 coreskinSuppressStartupDialogs( void );

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */

/* eof skinmain.h */
