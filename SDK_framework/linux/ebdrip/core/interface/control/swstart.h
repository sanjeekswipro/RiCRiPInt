/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swstart.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Entry points for Harlequin RIP
 *
 * This header file contains detail enabling the rip to be started by
 * the downward call to SwStart().
 *
 * SwStart is passed a pointer to an array of SWSTART structures. Each
 * element contains one item of information which the RIP may need to
 * start up properly, and consists of two fields: a tag field to
 * identify what aspect of the RIP this element affects, and a value
 * field, the meaning of which depends on the tag. If this is used
 * as a pointer to a string, the string should be null terminated.
 *
 * The last such element (there must always be at least one) contains
 * a tag SWEndTag; the value of this one is ignored.
 *
 * The reason for this mechanism is to allow compatibility with later
 * versions of the system, and different requirements of different
 * customers.
 *
 * NOTE: this file is now set up by default to use prototyped
 * functions. If you require not to use prototypes you should arrange
 * for the macro SW_NO_PROTOTYPES to be defined when compiling.
 */

/**
 * \defgroup interface Harlequin RIP core interface.
 * \ingroup core
 */

#ifndef SWSTART_H_INCLUDED
#define SWSTART_H_INCLUDED 1

#include "swenv.h" /* hack because they used to be here */
#include "mps.h"
#include "ripcall.h"
#include <stddef.h>  /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Container for a single, specific start-up directive given to the RIP.
 *
 * The \c SwStart() function is passed an array of these structures. The size
 * of the array is not specified, but the final entry must have a tag value
 * of \c SWEndTag to terminate the list.
 */
typedef struct SWSTART {
  /**
   * \brief The tag, which controls how the rest of the structure is
   * interpreted by the RIP. This must be set to \c SWEndTag for the
   * final entry.
   */
  int32 tag;
  /**
   * \brief The control parameter, whose meaning is determined by the tag.
   */
  union {
    int32 int_value;
    void * pointer_value;
    float float_value;
    void (RIPFASTCALL *probe_function)(int, int, intptr_t) ;
  } value;
} SWSTART;

#define SWEndTag         (-1)   /**< \brief Terminator for array of SWSTART
                                    structures */
#define SWMemoryTag       (1)   /**< \brief Memory required by the RIP; value
                                    is pointer_value type to
                                    SWSTART_MEMORY */
#define SWLocalMemoryTag  (2)   /**< \brief In multiprocess version, the above
                                  memory is shared, and this memory is
                                  local to each renderer. */

#define SWBandSizeTag     (3)   /**< \brief Amount of memory to dedicate to
                                   bands/frame initially (in Kb):
                                   default 512Kb  */
#define SWDevTypesTag     (4)   /* */

/* The next three tags are only required (and examined) if the RIP
 * supports multi-processing */

#define SWNThreadsTag     (5)   /**< \brief The total number of processes -
                                    'np' */

#define SWThreadTag       (6)   /**< \brief Unique id [0..np-1], 0 ->
                                    interpreter */

#define SWBandFactTag     (8)   /**< \brief Defines if we do Band Factoring;
                                    default 0.0  */

#define SWMemCfgTag       (9)   /**< \brief Use instead of SWMemoryTag for RIP
                                  to manage its own memory, growing as
                                  required
                                 */

#define SWMemSharedTag   (10)   /**< \brief Use instead of SWMemoryTag to give
                                  RIP a shared memory arena */

#define SWMvarTag  (11)   /**< \brief pointer_value is shared variables block
                              (mvar), as passed to SwSpawnRenderers */

#define SWZipArchiveNameTag  (12)  /**< \brief Defines the name of the zipped
                                       SW folder archive. */

#define SWDllNamePthreadsTag (13)  /**< \brief Defines the name of the
                                       pthreads DLL on Windows. If not
                                       specified, pthreads.dll is
                                       assumed. */

#define SWSwStartReturnsTag (14)  /**< \brief Boolean value stored in
                                     int_value (0|1). Informs the RIP
                                     whether \c SwStart() should
                                     return or not. If not specified,
                                     \c SwStart() will never
                                     return. i.e. \c exit() will be
                                     called when RIP shuts down. */

#define SWTraceHandlerTag (15)  /**< \brief Trace handler function is
                                   stored in probe_function. The trace
                                   function should match the
                                   \c SwTraceHandlerFn typedef from
                                   swtrace.h. The trace handler is called
                                   in some special builds to record profiling
                                   information. */

#define SWNThreadsMaxTag (16)  /**< \brief Maximum size of thread pool. */

#define SWRDRAPITag (17)  /**< \brief The RDR api from which all other APIs can
                               be found */

#define SWTimelineParentTag (18)  /**< \brief The parent timeline for the
                                     core RIP instance. */

/**
 * \brief Specifies a memory arena and its size.
 */
typedef struct {
  size_t sizeA;      /** Size of the block of memory (arena) */
  mps_arena_t arena; /** The memory arena */
} SWSTART_MEMORY;

/**
 * \brief Specifies a memory configuration.
 */
typedef struct {
  size_t maxAddrSpaceSize; /**< \brief Address range RIP may use, and max it may grow to */
  size_t workingSize;      /**< \brief Normal working size to restrict RIP to */
  size_t emergencySize;    /**< \brief Additional mem to use rather than partial paint */
  int32  allowUseAllMem;   /**< \brief Allow usage of mem up to max (after trying to
                                partial paint) rather than fail */
  mps_arena_t arena; /**< \brief The memory arena */
} SWSTART_MEMCFG;

/**
 * \brief Specifies a shared memory arena and its size.
 */
typedef struct {
  size_t memorySize; /**< \brief size of shared memory to use */
  mps_arena_t arena; /**< \brief The memory arena */
} SWSTART_SHMEM;

/* ============================================================================
 * The core RIP provides these functions.
 * ============================================================================
 */

/**
 * \brief The core RIP provides this function.
 */
extern HqBool RIPCALL SwInit RIPPROTO((SWSTART * mem));
typedef HqBool (RIPCALL *SwInit_fp_t) RIPPROTO((SWSTART * mem));

/**
 * \brief Start the RIP. The core RIP provides this function.
 *
 * \param start Pointer to the base of an array of \c SWSTART structures. Each
 * entry provides a configuration parameter. The final entry must have a tag
 * value of \c SWEndTag to terminate the list.
 */
extern HqBool RIPCALL SwStart RIPPROTO((SWSTART * start));
typedef HqBool (RIPCALL *SwStart_fp_t) RIPPROTO((SWSTART * start));

/**
 * \brief Stop the RIP. The core RIP provides this function.
 *
 * This only has an effect after SwStart() has been called and before
 * SwStart() returns. This function will return immediately.
 */
extern void RIPCALL SwStop RIPPROTO((void));
typedef void (RIPCALL *SwStop_fp_t) RIPPROTO((void));

/**
 * \brief The core RIP provides this function.
 */
extern uint32 RIPCALL SwThreadIndex RIPPROTO((void));
typedef uint32 (RIPCALL *SwThreadIndex_fp_t) RIPPROTO((void));

/* ============================================================================
 * The OEM is required to provide an implementation of the functions
 * below here.
 * ============================================================================
 */

/**
 * \brief The OEM skin is required to implement this function.
 */
extern void RIPCALL SwTerminating RIPPROTO((int32 code, uint8 * string));

/**
 * \brief The OEM skin is required to implement this function.
 */
extern void RIPCALL SwReboot RIPPROTO((void));

#ifdef __cplusplus
}
#endif


#endif /* SWSTART_H_INCLUDED */
