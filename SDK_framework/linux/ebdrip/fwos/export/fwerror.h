/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWERROR_H__
#define __FWERROR_H__

/*
 * $HopeName: HQNframework_os!export:fwerror.h(EBDSDK_P.1) $
 *
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
#include "fxerror.h"    /* Platform Dependent */

/* fwos */
#include "fwstring.h"   /* FwTextString */
#include "fwtree.h"     /* FwTree */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ----------------------- Overview ---------------------------------------- */

/* This module aims to provide a richer model of errors than the simple
 * numerical error code model provided by operating systems. It adds:
 *
 * 1) Heirarchical organisation.
 *
 * The errors are organised into a heirarchy, which the client can
 * extend. This allows clients to examine the error at whatever level
 * of detail is required. The nodes as well as the leaves are
 * legitimate as errors, and each is an FwClass style object. The
 * client should normally use Fw_msg_IsDescendant( e1, e2 ) to find
 * whether e2 is equal to e1 or a sub error of it.
 *
 * 2) Error strings with variable fields for additional details.
 *
 * When an error is detected there may be additional details which
 * would be useful in forming an error string, such as a filename.
 * For simplicity and regularity we assume there are at most two items
 * we are interested in detailing in an error string, and for each of
 * them we are only interested in their type and name. An FwErrorState
 * structure is passed down to which freshly allocated copies of these
 * details can be optionally attatched.
 *
 *
 * The intended use is as follows:
 *
 * a/ FwErrorState structures should only be modified via the functions
 * below and not directly by the clients.
 *
 * b/ The highest level code creates an FwErrorState to collect error
 * information, and initialises it with FW_ERROR_STATE_INIT. This can
 * either be a local variable on the stack, or global, with the former
 * being preferred for reentrancy, and modularity reasons.
 *
 * c/ This error state can then be passed down to functions to collect
 * error information. The error state should only be set when an error
 * occurs, AND LEFT UNMODIFIED ON SUCCESS. This allows a client to
 * call several functions, and examine the error state at the end to
 * see if an error occurred in any of them. In particular the only
 * module that should clear the error state is the one that created it.
 *
 * d/ If you pass an FwErrorState which already has an error set to a
 * function, the function may return immediately or abort the
 * operation early . Thus the client should only do this if it wants
 * to give up on the first error, otherwise it should always
 * FwErrorClear an FwErrorState after an error befire reusing it.
 *
 * e/ If a module wants to handle some errors and not report them to
 * its calling clients it should use a local FwErrorState of its own.
 * It should use TRUE for fAlloc in case the caller wants full
 * details.  When it detects an error it can optionally report it to
 * the caller by FwErrorCopy-ing its local FwErrorState to the callers
 * FwErrorState, or recover from the error itself. Whichever it does
 * it should FwErrorClear its local FwErrorState before returning.
 *
 * f/ As described in 2) above an FwErrorStates can optionally
 * allocate memory.  This memory should be freed by calling
 * FwErrorClear before the FwErrorState is discarded.
 */


/* ----------------------- Classes ----------------------------------------- */

/*
 * Error class
 */

typedef struct FwError {

#define FW_ERROR_FIELDS \
  FW_TREE_FIELDS \
  FwTextString  ptbzFormat;

  FW_ERROR_FIELDS

} FwError;

extern FwClassRecord FwErrorClass;

/* Fw_Msg_CreateError
 * This message can be used by the client to add errors to the tree.
 * Error objects are statically rather than dynamically allocated, so pObj
 * must be non NULL.
 */
FW_MESSAGE_DECLARE( Fw_msg_CreateError )
typedef FwObj * (Fw_msg_CreateError_Type)
(
  FwObj *               pObj,
  FwObj *               pParent,
  FwObj *               pAfterThis,
  FwTextString          ptbzFormat
);
Fw_msg_CreateError_Type Fw_msg_CreateError;

/* psuedo messages */
#define Fw_msg_GetString( pObj )        ( ((FwError *)(pObj))->ptbzFormat )


/* ----------------------- Macros ------------------------------------------ */

/* FwStrPrintf formats for the item types and names. These can be used in
 * error strings via ANSI C string concatenation.
 * Note that types but not names get translated.
 */

#define FW_ERROR_ITEM0_TYPE     "%[0]t"
#define FW_ERROR_ITEM0_NAME     "%[1]s"
#define FW_ERROR_ITEM1_TYPE     "%[2]t"
#define FW_ERROR_ITEM1_NAME     "%[3]s"

/* Types of item which may be returned by FrameWork in error state.
 * The client may add additional types of its own.
 */

#define FW_ERROR_ITEM_TYPE_FILE UVS( "File" )
#define FW_ERROR_ITEM_TYPE_COMMAND UVS( "Command" )
#define FW_ERROR_ITEM_TYPE_ERROR_STATUS UVS( "Error status" )


/* ----------------------- Types ------------------------------------------ */

/* An item which is referenced in an error string */
typedef struct FwErrorItem
{
  FwTextString  type;
  FwTextString  name;
} FwErrorItem;

/* An (FwErrorState *) is passed down to calls that use the FwError mechanism.
 * If they detect an error they:
 * 1) set the pError field
 * 2) set the platform error field if OS error.
 * 3) if fAlloc is TRUE set items to hold freshly allocated copies of the names
 *    and types of any item referenced in the error string ( freeing previous
 *    ones if any first. FwErrorClear will free and reset these.
 */

#define FW_ERROR_MAX_ITEMS      2
#define FW_ERROR_MAX_STRINGS    ( 2 * FW_ERROR_MAX_ITEMS ) /* type and name */

typedef struct FwErrorState
{
  int32                 fAlloc; /* TRUE <=> alloc item details in items */
#if defined( ASSERT_BUILD )
#define FW_ERROR_STATE_MAGIC 0x86752947u
  uint32                magic;  /* for detecting unitialised */
#endif
  FwError *             pError;
  FwPlatformError       platformError;
  FwErrorItem           items[ FW_ERROR_MAX_ITEMS ];
} FwErrorState;

/* Initialiser to use for an FwErrorState - FW_ERROR_STATE_INIT
 * If you use TRUE for fAlloc the you must call FwErrorClear before
 * discarding the FwErrorState to free any allocated item details.
 * You should always use TRUE for local FwErrorStates as described above.
 * FW_ERROR_STATE_ASSERT can be used to assert an FwErrorState is consistent.
 */
#if defined( ASSERT_BUILD )
#define FW_ERROR_STATE_INIT( fAlloc ) \
 { fAlloc, FW_ERROR_STATE_MAGIC, FW_SUCCESS, FW_PLATFORM_SUCCESS }
#define FW_ERROR_STATE_ASSERT( pState ) FwErrorStateAssert( pState );
 extern void FwErrorStateAssert( FwErrorState * pState );
#else
#define FW_ERROR_STATE_INIT( fAlloc ) \
 { fAlloc, FW_SUCCESS, FW_PLATFORM_SUCCESS }
#define FW_ERROR_STATE_ASSERT( pState ) EMPTY_STATEMENT()
#endif

/* Context for the library */
typedef struct FwErrorContext
{
  int32                         _dummy_;        /* none yet */
#ifdef FW_PLATFORM_ERROR_CONTEXT
  FwPlatformErrorContext        platform;
#endif
} FwErrorContext;


/* ----------------------- Functions --------------------------------------- */

/* reset error state to success and free any memory allocated for item details
 * returns pState->pError
 */
extern FwError * FwErrorClear( FwErrorState * pState );

/* This does FwErrorSet on pState from the contents of pLocalState.
 * The copy is a deep copy so pState gets its own copies of the error strings.
 * This is typically used to selectively pass errors back to the caller.
 * It is illegal for pState to have fAlloc TRUE, and pLocalState to have
 * fAlloc FALSE, since you are then returning the full error details
 * requested
 */
extern void FwErrorCopy( FwErrorState * pState, FwErrorState * pLocalState );

/* Set error state when error is in FrameWork or client.
 *
 * nStrings says how many additional strings are supplied, these will be in the
 * order: item 0 type, item 0 name, item 1 type, item 1 name.
 * NULL can be passed for any of these that are not available.
 * Freshly allocated copies of these will be allocated and attatched to the
 * FwErrorState if it has fAlloc set.
 * For Example:
 *   FwErrorSet( pState, pError, 0 )
 *   FwErrorSet( pState, pError, 2, FW_ERROR_ITEM_TYPE_FILE, ptbzFilename )
 *
 * pState->platformError is set to FW_PLATFORM_SUCCESS since not OS error.
 */
extern void FwErrorSet
 ( FwErrorState * pState, FwError * pError, uint32 nStrings, ... );

/* Set error state when error is from underlying OS.
 * The behaviour is the same as FwErrorSet except that platformError is
 * recorded in the FwErrorState and converted to an FwError using
 * FwPlatformErrorMap
 */
extern void FwPlatformErrorSet
 ( FwErrorState * pState, FwPlatformError platformError, uint32 nStrings,... );

/* Convert a platform dependent error code to an FwError.
 * This deliberately does not cover all error codes, since:
 * 1) If a system call is returning something unexpected the calling code
 *    needs greater thought.
 * 2) Many of the possible error codes are for unused system calls, and might
 *    require the error heirarchy to be extended.
 * Thus 
 Whenever a new system call
 * is used you should check that its possible error codes are mapped. If no
 * mapping is present for a platform error that indicates that the system call
 * that generated it was insufficiently understood, and possibly that the
 * FwError heirarchy needs to be extended. Therefore this function will
 * HQTRACE, HQFAIL and return &fwErrorRoot for unrecognised ones.
 */
extern FwError * FwPlatformErrorMap( FwPlatformError platformError );

/* Generate an error string from an FwErrorState, replacing any missing item
 * types and names with the empty string. Returns bytes output.
 */
extern uint32 FwErrorPrintf( FwStrRecord * pRecord, FwErrorState * pState );

/* ----------------------- Data -------------------------------------------- */

/* Success is indicated by a NULL error object */
#define FW_SUCCESS      (FwError *) NULL

/*
 * Error heirarchy
 */

/* "Uncategorised error" */
extern FwError fwErrorRoot;
  /* "Parameter rejected" */
  extern FwError fwErrorParameter;
    /* "Pointer parameter rejected" */
    extern FwError fwErrorPointer;
      /* "Pointer parameter NULL" */
      extern FwError fwErrorPointerNull;
    /* "Numeric parameter rejected" */
    extern FwError fwErrorNumeric;
      /* "Numeric parameter out of range" */
      extern FwError fwErrorNumericRange;
      /* "Numeric parameter illegal value" */
      extern FwError fwErrorNumericValue;
    /* "%[0]t %[1]s String parameter rejected" */
    extern FwError fwErrorString;
      /* "%[0]t String parameter empty" */
      extern FwError fwErrorStringEmpty;
      /* "%[0]t %[1]s String parameter too long" */
      extern FwError fwErrorStringLength;
      /* "%[0]t %[1]s String parameter illegal character" */
      extern FwError fwErrorStringCharacter;
      /* "%[0]t %[1]s String parameter syntax incorrect" */
      extern FwError fwErrorStringSyntax;
      /* "%[0]t %[1]s String parameter illegal value" */
      extern FwError fwErrorStringValue;
  /* "Operation denied" */
  extern FwError fwErrorOperationDenied;
    /* "%[0]t %[1]s Does not exist" */
    extern FwError fwErrorNonExistent;
    /* "%[0]t %[1]s Already exists" */
    extern FwError fwErrorAlreadyExists;
    /* "%[0]t %[1]s Access denied" */
    extern FwError fwErrorAccessDenied;
    /* "%[0]t %[1]s In use" */
    extern FwError fwErrorInUse;
  /* "Operation failed" */
  extern FwError fwErrorOperationFailed;
    /* "Operation aborted" */
    extern FwError fwErrorAbort;
    /* "Limit reached" */
    extern FwError fwErrorLimit;
      /* "Hardware limit reached" */
      extern FwError fwErrorHardwareLimit;
        /* "Memory exhausted" */
        extern FwError fwErrorNoMemory;
        /* "File storage device full" */
        extern FwError fwErrorDiskFull;
      /* "Software limit" */
      extern FwError fwErrorSoftwareLimit;
        /* "Could not invent temporary file name" */
        extern FwError fwErrorNoTempName;
    /* "%[0]t %[1]s Hardware error" */
    extern FwError fwErrorHardware;
    /* "Unimplemented operation" */
    extern FwError fwErrorUnimplemented;
    /* "Blessing errors occurred" */
    extern FwError fwErrorBlessingErrors;
    /* "Fatal internal error" */
    extern FwError fwErrorFatal;
      /* "Program logic error" */
      extern FwError fwErrorBug;
    /* "Error running command" */
    extern FwError fwErrorCommand;
      /* "Could not call %[0]t %[1]s" */
      extern FwError fwErrorCallingCommand;
        /* "Could not call %[0]t %[1]s : Argument list exceeds 1024 bytes */
        extern FwError fwErrorArgListTooLong;
        /* "Could not call %[0]t %[1]s : Mode argument is invalid */
        extern FwError fwErrorInvalidArgument;
        /* "Could not call %[0]t %[1]s : File or path is not found */
        extern FwError fwErrorNoSuchFile;
        /* "Could not call %[0]t %[1]s : Specified file is not executable or 
                                         has invalid executable-file format */
        extern FwError fwErrorExecFormat;
        /* "Could not call %[0]t %[1]s : Not enough memory is available to 
                                         execute new process */
        extern FwError fwErrorOutOfMemory;
      /* "%[0]t %[1]s from %[2]t %[3]s" */
      extern FwError fwErrorFromCommand;

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* ! __FWERROR_H__ */

/* eof fwerror.h */
