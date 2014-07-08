#ifndef __SKINPFNS_H__
#define __SKINPFNS_H__

/* Interface in coreskin between platform-dependents and platform-independents
 * $HopeName: SWcoreskin!export:skinpfns.h(EBDSDK_P.1) $
 *
* Log stripped */

/* can now assume STDARGS since ANSIfied */
#include <stdarg.h>

#include "coreskin.h"    /* includes skinconf.h swvalues.h */

#include "fwfile.h"     /* FwFile */
#include "fwstring.h"   /* FwTextString */

#include "swdevice.h"   /* DEVICETYPE */


#ifdef __cplusplus
extern "C" {
#endif

#ifndef __FWBOOT_H__
struct FwBootContext;
#endif

#ifndef __GDEVCPCF_H__
struct deviceCapabilities;
struct deviceConfig;
#endif

#ifndef __GDEVDEFS_H__
struct deviceDefinition;
#endif

#ifndef __GGENDEFS_H__
struct genericPluginContext;
#endif

#ifndef __GDEVTYPE_H__
struct PluginID;
#endif

#ifndef PLUGININFO_T_DEFINED
struct PluginInfo_t;
#endif

extern void coreskinexiterrorf( uint8 *format, ... );
extern void coreskinexiterrorc( int32 code, uint8 *format, ... );
extern void coreskinmonitorf( const char *format, ... );
extern void coreskinvmonitorf( const char * format, va_list valist );

extern void coreskintimedmonitorf( const char *format, ... );
extern void coreskinvtimedmonitorf( const char * format, va_list valist );

extern void coreskinmonitor( uint8 * buffer );
extern void coreskinmonitorl( uint8 * buffer, int32 length );

extern void coreskinlogf( uint8 *format, ... );

extern void coreskinNoteCommandLineOption( char * pszOption, int32 cbLen, int32 fHandled );

/* guiFindOutputPlugins() 'type' parameter values
 */
#define OP_TYPE_BUILTIN 1   /* Look for builtin plugins implemented by the RIP */
#define OP_TYPE_APP     2   /* Look for builtin plugins in application file */
#define OP_TYPE_FILE    3   /* Look for plugins in the passed filename */

/* Return codes for guiServiceEnabled() */
#define SERVICE_ENABLED_NONE     -1
#define SERVICE_ENABLED_SOAR      0   /* SOAR SDK */
#define SERVICE_ENABLED_JDF       1   /* Old standalone JDF Enabler (obsolete) */
#define SERVICE_ENABLED_SWWEBGUI  2   /* SWWebGUI "Web GUI Services" */

/* ------------- Functions implemented in different platforms -------------- */

/* Miscellaneous functions (sorted approx alphabetically)
 * ------------------------------------------------------
 */

extern int32 gui_checkApplicationSizeOK( void );
extern int32 gui_checkDiskSystemOK( void );

extern int32 gui_initmachinesystem( void );
extern void gui_termmachinesystem( int32 );

extern HqBool guiMemoryFreeInBytes( size_t *pcTotalFree );

extern int32 guiGetNoOfCores( void );

extern void guiBootFrameWork( struct FwBootContext * pBootContext);

extern int32 gui_scanForDisks
 ( struct FwStrRecord * pRecord, int32 index );
extern void gui_startperiodicmanager( void );
extern void gui_stopperiodicmanager( void );

extern size_t guiAdjustAddrSpaceSizeInK( size_t addrSpaceSize );
extern HqBool guiReserveAllRipVM( void );
extern uint32 guiGetMinMemoryDefault( void );

extern int32 guiIsPlugin( FwTextString ptbzGuiFilename, FwFileInfo * pInfo );

extern int32 guiFindOutputDevices
(
  uint8 *                       plugname,
  int32                         first,
  struct deviceCapabilities *   cap,
  struct deviceConfig *         conf
);
extern void * guiFindOutputPlugins
 ( int32 type, FwTextString ptbzGuiFilename, int32 first, struct PluginID * pname );
extern void * guiFindPlugins
 ( uint8 * filename, int32 first, struct PluginID * pname );
extern int32 guiLoadOutputPlugin
 ( struct PluginInfo_t * pPlugInfo, int32 * pError );
extern int32 guiLoadPlugin( struct PluginInfo_t * pPlugInfo );
#ifdef GET_DATA_FROM_RESOURCES
int32 statusFetchText( struct deviceDefinition * device, int32 eCode, uint8 ** eText );
#endif
extern void guiUnloadOutputPlugin( struct PluginInfo_t * pPlugInfo );
extern int32 guiUnloadPlugin( struct PluginInfo_t * pPlugInfo );
extern int16 guiCallOutputDevice
 ( int16 selector, struct deviceDefinition * device, void * pv );

extern void gui_filterLOGFILEmsg( uint8 * filterbuffer, int32 length );


/* Unlike coreguiCallPlugin this can not be used for output plugins */
extern int16 guiCallPlugin(struct PluginInfo_t * pPlugin, int16 nSelector,
                 struct genericPluginContext * pContext,  void * pParam);

extern int32 gui_ResetFromFactoryDefaults(void);

extern int32 gui_tickcount( void );

/* Returns a value to indicate which service, if any, started the rip */
extern int32 guiServiceEnabled( void );

extern char * guiGetProductNameSuffix( void );

/* Do any per-platform processing required to honour preference settings */
extern void guiHonourPrefs( void );

extern int32 guiSuppressStartupDialogs( void );

#ifdef __cplusplus
}
#endif

#endif /* __SKINPFNS_H__ protection for multiple inclusion */
