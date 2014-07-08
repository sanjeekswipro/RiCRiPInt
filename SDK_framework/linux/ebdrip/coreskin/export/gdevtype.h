#ifndef __GDEVTYPE_H__
#define __GDEVTYPE_H__

/*
$HopeName: SWcoreskin!export:gdevtype.h(EBDSDK_P.1) $

Log stripped */

/* gdevtype.h */

/*  FILE:     gdevtype.h
  PURPOSE:    header file to define types for identifiers used by the 
              Generic Device system
  ORIGINATOR: Ian W. Robinson
  CREATED:    Thursday November 14th 1991
*/

/*------------------------------------------------------------------------------
  Contents of file:
  
  typedefs for identifier types used for devices, device addresses, plugins,
  device types.
    
  typedef for MDP_DeviceInfo structure - specifies an MDP Device to a
  Multiple Device Plugin.
  
  typedef for DevClass - provides information on what operations a particular
  plugin supports.
    
------------------------------------------------------------------------------*/

/* For the DeviceAddress, DeviceID, DeviceTypeID, and PluginID types below,
the access "function" TEXT_FROM_X returns a uint8 * to a string which is the
textual representation of the type X.  This string should not be changed using
this uint8 *.  The access "function" TEXT_TO_X takes a uint8 * and an instance of
the type X, and sets the instance appropriately.  This should always be used to
set the type - at the moment it is a simple strcpy but this may be changed (and
should be to ensure length checking on the fields involved).

The COMPARE_X is an ordered comparison of the two values that are its arguments,
returning <0, 0 for equality, or >0.

To test for equality, use EQUAL_X.  This returns FALSE if not equal, or TRUE
if its arguments are equal.
*/
		
enum {  /* DONT CHANGE THESE NUMBERS - THEY ARE HYSTERICAL! */
  NONEDEVICE    = 0,	
  SCREENDEVICE  = 3,
  DEVDEVICE     = 6
};
typedef int32 DeviceTypeNumber;

/* The following DevClass structure contains the information specifying what
class a particular plugin or device is - e.g. whether it supports dynamic
configurations, whether it is single or multiple, if it is multiple whether
it supports D_FIND_DEVICE_TYPE or D_FIND_DEVICE calls (or both), etc. */

enum {
  DEVCLASS_UNKNOWN,     	/* unknown type - will be ignored */
  DEVCLASS_SINGLE,      	/* old style with config and capability resources */
  DEVCLASS_MULTIPLE,      	/* multiple device plugin of some sort */
  DEVCLASS_DYNAMIC_CONFIG,  /* old-style ECRM type single device plugin */
  DEVCLASS_BUILTIN      	/* Preview etc - not really plugins at all! */
};

typedef int32 DevClass;

#define MAX_DEVICEADDRESS_LEN 256

typedef struct DeviceAddress {
  uint8 string[ MAX_DEVICEADDRESS_LEN ] ;
} DeviceAddress ;
#define COPY_DeviceAddress( from, to ) ((*to) = (*from))
#define COMPARE_DeviceAddress( arg1, arg2 ) \
  		( strcmp( (char *) (arg1)->string, (char *) (arg2)->string ) )
#define EQUAL_DeviceAddress( arg1, arg2 ) \
  		( COMPARE_DeviceAddress( arg1, arg2 ) == 0 )
#define TEXT_FROM_DeviceAddress( arg ) ((arg)->string)
#define TEXT_TO_DeviceAddress( charstar, arg ) \
  		( strncpy(( char * )(arg)->string, ( char * )charstar, MAX_DEVICEADDRESS_LEN ),  \
   		 (arg)->string[MAX_DEVICEADDRESS_LEN - 1] = '\0' )
  
  
#define MAX_DEVICEID_LEN 32

  /* This length is the maximum length of the character array
     required to hold the text representation of the ID */
typedef struct DeviceID {
  uint8 string[ MAX_DEVICEID_LEN ] ;
} DeviceID ;
#define COPY_DeviceID( from, to ) ((*to) = (*from))
#define COMPARE_DeviceID( arg1, arg2 ) \
  		( strcmp( (char *) (arg1)->string, (char *) (arg2)->string ) )
#define EQUAL_DeviceID( arg1, arg2 ) \
  		( COMPARE_DeviceID( arg1, arg2 ) == 0 )
#define TEXT_FROM_DeviceID( arg ) ((arg)->string)
#define TEXT_TO_DeviceID( charstar, arg ) \
 		 ( strncpy(( char * )(arg)->string, ( char * )charstar, MAX_DEVICEID_LEN ), \
   		 (arg)->string[MAX_DEVICEID_LEN - 1] = '\0' )


#define MAX_DEVICETYPEID_LEN 32

typedef struct DeviceTypeID {
  uint8 string[ MAX_DEVICETYPEID_LEN ] ;
} DeviceTypeID ;
#define COPY_DeviceTypeID( from, to ) ((*to) = (*from))
#define COMPARE_DeviceTypeID( arg1, arg2 ) \
 		 ( strcmp( (char *) (arg1)->string, (char *) (arg2)->string ) )
#define EQUAL_DeviceTypeID( arg1, arg2 ) \
  		( COMPARE_DeviceTypeID( arg1, arg2 ) == 0 )
#define TEXT_FROM_DeviceTypeID( arg ) ((arg)->string)
#define TEXT_TO_DeviceTypeID( charstar, arg ) \
  		( strncpy(( char * )(arg)->string, ( char * )charstar, MAX_DEVICETYPEID_LEN ), \
    		(arg)->string[MAX_DEVICETYPEID_LEN - 1] = '\0' )


#define MAX_PLUGINID_LEN 32

typedef struct PluginID {
  uint8 string[ MAX_PLUGINID_LEN ] ;
} PluginID ;
#define COPY_PluginID( from, to ) ((*to) = (*from))
#define COMPARE_PluginID( arg1, arg2 ) \
 		 ( strcmp( (char *) (arg1)->string, (char *) (arg2)->string ) )
#define EQUAL_PluginID( arg1, arg2 ) \
 		 ( COMPARE_PluginID( arg1, arg2 ) == 0 )
#define TEXT_FROM_PluginID( arg ) ((arg)->string)
#define TEXT_TO_PluginID( charstar, arg ) \
 		 ( strncpy(( char * )(arg)->string, ( char * )charstar, MAX_PLUGINID_LEN ), \
   		 (arg)->string[MAX_PLUGINID_LEN - 1] = '\0' )


#endif /* ! __GDEVTYPE_H__ */

/* eof gdevtype.h */
