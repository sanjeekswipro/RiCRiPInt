#ifndef __RIPAPI_H__
#define __RIPAPI_H__

/* $HopeName: SWripiimpl!export:ripapi.h(EBDSDK_P.1) $
 *
 */

/*
 * This module defines generally useful things for C and C++ world
 */


#ifdef PRODUCT_HAS_API

/* ----------------------- Includes ---------------------------------------- */

/* standard */
#include "std.h"            /* standard includes */

/* fwos */
#include "fwstring.h"       /* FwTextString */
#include "fwunicod.h"       /* FwUniString */

/* HQN_CPP_CORBA */
#include "hqn_cpp_corba.h"  /* Interface definitions */


/* ----------------------- Macros ------------------------------------------ */

/* return codes for most functions returning an int which serve C and C++ worlds */

#define  RIPAPI_SUCCESS        1
#define  RIPAPI_FAILURE        0

/* macro for checking on return status and returning on failure */

#define  CHECK_RIPAPI_ERROR(f) \
         if (!(f))  return  RIPAPI_FAILURE      /* left off trailing ; on purpose */

#ifdef __cplusplus

/* alternative methodology using exceptions for C++ world */

#define  CHECK_RIPAPI_THROW(f) \
         if (!(f))  throw RIPAPI_GenericException()
                                                /* left off trailing ; on purpose */

/* ----------------------- Classes ----------------------------------------- */

/* definition of GenericException, can be used to pass error message */

class RIPAPI_GenericException  /* usually memory allocation */
{
private:
    const char *m_error_msg;

public:
    RIPAPI_GenericException() : m_error_msg(0) {};  /* does a copy need to be made? */
    RIPAPI_GenericException(const char *error_msg) :m_error_msg(error_msg) {};

    const char *error_message() { return  m_error_msg; }
};  /* class GenericException */

#endif /* __cplusplus */

/* ----------------------- Definitions ------------------------------------- */

/* some type definitions for C and C++ worlds (do they belong here?) */

typedef int32 RIPAPI_JobID;

typedef uint8 RIPAPI_PostScriptChar;
typedef RIPAPI_PostScriptChar *RIPAPI_PostScriptString;

typedef uint32 RIPAPI_ProgressType;
typedef uint32 RIPAPI_ProgressTypesMask;
typedef uint32 RIPAPI_JobStatusType;
typedef uint32 RIPAPI_JobStatusTypesMask;


/* The order of these enum tags MUST match those defined in the IDL enum */
enum
{
  RIPAPI_PublishingProgress = 0,      /* the setting up of input channels, eg AppleTalk */
  RIPAPI_BoundProgress,               /* Bound inputs to the RIP, eg. file. */
  RIPAPI_UnboundProgress,             /* Unbound inputs to the RIP, eg. channel. */
  RIPAPI_CRDGenerationProgress,       /* the generation of CRD caches */
  RIPAPI_RecombinationProgress,       /* the display list fix-up for recombine */
  RIPAPI_ScreeningGenerationProgress, /* the generation of Halftone caches */
  RIPAPI_TrappingPreparationProgress, /* the first display list scan in TrapPro */
  RIPAPI_TrappingGenerationProgress,  /* the generation of traps in TrapPro */
  RIPAPI_PaintingToDiskProgress,      /* the generation of PGBs on disk */
  RIPAPI_OutputProgress,              /* the transfer of raster data to the output device in non-throughput modes */
  RIPAPI_CompositingProgress,         /* colour compositing of a page */
  RIPAPI_PreparingToRenderProgress,   /* after compositing but before final rendering */
  RIPAPI_ThroughputProgress,          /* the transfer of raster data to the output device in throughput modes */
  RIPAPI_PDFScanningProgress,         /* progress while scanning the PDF file for pages which might share a common background. */
  RIPAPI_PDFCountingPagesProgress     /* progress while counting the number of pages in the file */
};

/* The order of these enum tags MUST match those defined in the IDL enum */
enum
{
  RIPAPI_OutputEnabled = 0,           /* changes in output enabledness */
  RIPAPI_PagebufferAdded,             /* additions of pagebuffer to queue(s) */
  RIPAPI_PagebufferRemoved,           /* removals of pagebuffer from queue(s) */
  RIPAPI_OutputError,                 /* errors during output */
  RIPAPI_AllPagesOutput               /* all pages output */
};

/* more type definitions, C++ world only */

#ifdef __cplusplus

/* ----------------------- Macros ------------------------------------------ */

#ifndef TRUE
#define  TRUE  1
#endif /* TRUE */

#ifndef FALSE
#define  FALSE  0
#endif /* FALSE */


/* ----------------------- Classes ----------------------------------------- */

/* RIPAPI_NoMemoryException converts to a CORBA::NOMEMORY exception */

class RIPAPI_NoMemoryException
{
};

#endif /* __cplusplus */

/* ----------------------- Functions --------------------------------------- */

extern void initialize_ripapi();
  /* initializes APItoRIP queue */
  /* throws RIPAPI_NoMemoryException() on failure */


#endif /* PRODUCT_HAS_API */

/*
* Log stripped */

#endif /* __RIPAPI_H__ */

/* eof ripapi.h */
