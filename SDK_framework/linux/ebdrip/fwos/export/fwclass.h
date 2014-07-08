/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWCLASS_H__
#define __FWCLASS_H__

/*
 * $HopeName: HQNframework_os!export:fwclass.h(EBDSDK_P.1) $
 * FrameWork External Object Oriented Class definitions
 */

/*
* Log stripped */



/* ----------------------- Includes ---------------------------------------- */

/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
                        /* No Platform Dependent */

/* ----------------------- Types ------------------------------------------- */

/* abstract instance type, do not confuse with FwObject below */
typedef void FwObj;

/* Message ID type, id is pointer itself rather than contents of the string */
typedef char * FwMessageID;

/* abstract Method procedure type */
typedef void FwMethod( void );


/* Each class can intercept messages as they pass up the class heirarchy */

typedef struct FwIntercept
{
  FwMessageID   messageID;
  FwMethod *    method;
} FwIntercept;


/*
 * A Class is described by its ClassRecord:
 */

typedef struct FwClassRecord
{
#ifdef ASSERT_BUILD
  int32                  classMagicCookie;
  char                 * pszClassName;
#endif
  int32                  nBytesInInstance;
  struct FwClassRecord * pSuperClassRecord;
  FwIntercept          * pMethodTable;
  uint32                 flags;             /* 0 is defaults */
} FwClassRecord;

/* flags bits are as follows: */

/* FW_CLASS_FLAG_NOT_HEAP should be set if objects of this class are
 * not intended to live on the heap, and hence memory for the object
 * itself will neither be allocated or freed. This is useful for
 * statically allocated and stack objects.
 */
#define FW_CLASS_FLAG_NOT_HEAP BIT( 0 )

#define FW_CLASS_FLAGS_MASK    BITS_BELOW( 1 )


/* ----------------------- Macros ----------------------------------------- */

/************************
 * Class Record Support *
 ************************/

#define FW_CLASSRECORD( pObj ) \
   ( ( (FwObject *) (pObj) ) -> pClassRecord )

/*
 * Debugging aid. Asserted builds have a magic-cookie in 
 * the class record so that asserts in various class methods
 * can check that they have been passed valid instance pointers.
 */
#ifdef ASSERT_BUILD
#define FC_MAGIC_COOKIE                0xdeaf
#define FC_CLASS_MAGIC_COOKIE(name)    FC_MAGIC_COOKIE, name,
#else
#define FC_CLASS_MAGIC_COOKIE(name)
#endif

/* Initialiser for entry in message intercept table */
#define FW_INTERCEPT( message, method ) \
  { _FW_MESSAGE_ID( message ), (FwMethod *) method }

#define FW_ASSERT_IS_CLASS( pClassRecord ) \
  HQASSERT( pClassRecord != NULL \
            && pClassRecord->classMagicCookie == FC_MAGIC_COOKIE, \
            "Not a class" )

#define FW_ASSERT_IS_OBJECT( pObj ) \
  HQASSERT( pObj != NULL \
            && pObj->pClassRecord != NULL \
            && ((FwObject *) pObj)->pClassRecord->classMagicCookie \
               == FC_MAGIC_COOKIE, \
            "Not an instance of a class" )


/**************************************
 * Message declaration and definition *
 **************************************/

/* The first parameter of messages is conventially the object on which
 * dispatch will be done, FwObj * pObj. For creation messages this
 * will be NULL except when SUPERSENDing so the second parameter for
 * creation messages is FwClassRecord * pClassRecord. 
 * For each message there is a convenience function to invoke the
 * message on an object. For creation messages this convenience function
 * should omit the first parameter pObj since it will be NULL, and can also
 * omit the second parameter pClassRecord if the message is specific to a
 * particular class, and wont be used to create instances of subclasses.
 */

/* To add a message you should add
 *
 *   to header file:
 *
 *     declare message
 *     declare message function type
 *     declare message convenience function
 *   eg
 *     FW_MESSAGE_DECLARE( <name> )
 *     typedef <return type> (<name>_Type) <parameters>;
 *     extern <name>_Type <name>;
 *
 *   to a .c file
 *     define message
 *     define mesage convenience function
 *   eg
 *     FW_MESSAGE_DEFINE( <name> )
 *     <return type> <name> <params>
 *     {
 *       return FW_MESSAGE_SELFSEND( <name>, pObj, args );
 *     }
 */


#define FW_MESSAGE_DECLARE( message ) \
extern char _FW_MESSAGE_HANDLE( message )[];

#ifndef ASSERT_BUILD
#define FW_MESSAGE_DEFINE( message ) \
  char _FW_MESSAGE_HANDLE( message ) [ 1 ];
#else
#define FW_MESSAGE_DEFINE( message ) \
  char _FW_MESSAGE_HANDLE( message ) [] = #message;
#endif


/********************
 * Message dispatch *
 ********************/

/* normal dispatch of message to object */
#define FW_MESSAGE_SELFSEND( message, pObj, args ) \
  FW_MESSAGE_SEND( message, FW_CLASSRECORD( pObj ), args )

/* message dispatch when want superclass methods */
#define FW_MESSAGE_SUPERSEND( message, pClassRecord, args ) \
  FW_MESSAGE_SEND( message, (pClassRecord)->pSuperClassRecord, args )


/* raw message dispatch, only for creation message convenience functions */
#define FW_MESSAGE_SEND( message, pClassRecord, args ) \
 (( message ## _Type * ) \
  ( FwClassFindMethod( _FW_MESSAGE_ID( message ), pClassRecord ) ) ) args


/******************************
 * Messaging internal details *
 ******************************/

/* message id */
#define _FW_MESSAGE_ID( message ) (& _FW_MESSAGE_HANDLE( message )[ 0 ] )

/* the handle of named message */
#define _FW_MESSAGE_HANDLE( message ) _ ## message ## _Handle


/* ----------------------- Classes ----------------------------------------- */

/*
 * The base level class is an object:
 */

typedef struct FwObject {

#define FW_OBJECT_FIELDS \
  FwClassRecord * pClassRecord;

  FW_OBJECT_FIELDS

} FwObject;


extern FwClassRecord FwObjectClass;

/* Messages for object class: */

FW_MESSAGE_DECLARE( Fw_msg_Create )
typedef FwObj * (Fw_msg_Create_Type)
( FwObj * pObj, FwClassRecord * pClassRecord );
extern Fw_msg_Create_Type Fw_msg_Create;

FW_MESSAGE_DECLARE( Fw_msg_Destroy )
typedef void (Fw_msg_Destroy_Type) ( FwObj * pObj );
extern Fw_msg_Destroy_Type Fw_msg_Destroy;

/* Return TRUE <=> given class is objects class or on superclass chain */
FW_MESSAGE_DECLARE( Fw_msg_classInheritsFrom )
typedef int32 (Fw_msg_classInheritsFrom_Type)
( FwObj * pObj, FwClassRecord * pClass );
extern Fw_msg_classInheritsFrom_Type Fw_msg_classInheritsFrom;

/* ----------------------- Functions -------------------------------------- */

extern FwObj * FwClassAlloc( FwClassRecord * pClassRecord, uint32 nAllocFlags );

extern FwMethod * FwClassFindMethod
 ( FwMessageID messageID, FwClassRecord * pClassRecord );


#endif /* __FWCLASS_H__ */

/* eof fwclass.h */
