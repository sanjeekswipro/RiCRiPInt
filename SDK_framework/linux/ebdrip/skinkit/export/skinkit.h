/* Copyright (C) 2006-2014 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:skinkit.h(EBDSDK_P.1) $
 * Definition of functions making up the API
 */

#ifndef __SKINKIT_H__
#define __SKINKIT_H__

#include <stdarg.h>

#include "std.h"
#include "mem.h"
#include "skinras.h"
#include "swdevice.h"
#include "swraster.h"
#include "dlliface.h"
#include "swtrace.h"
#include "ripcall.h"

/**
 * \file
 *
 * \brief This header file defines high-level RIP control functions
 * and callback prototypes.
 *
 * The functions defined in this file provide the basic interfaces for
 * control flow: booting the RIP, and executing jobs.
 *
 * The primary functions are \c SwLeInitRuntime(), \c SwLeSDKStart(),
 * \c SwLeStart(), \c SwLeJobStart(), \c SwLePs(), \c SwLeJobEnd(),
 * \c SwLeStop(), \c SwLeSDKEnd(), and \c SwLeShutdown().
 * The RIP itself is started up on a separate thread, allowing the caller to
 * feed data to the RIP on a procedural basis, with callbacks being used to
 * receive output data \e from the RIP (both raster data and monitor messages).
 *
 * A typical RIP application, which is reading or creating a PostScript
 * language stream will, have the following structure (described here in
 * pseudocode):

  \verbatim
  SwLeInitRuntime ()

  for ( <all different RIP instantiations or configurations> ) {
    SwLeSDKStart()

    SwLeSetRipRendererThreads(...)

    <optionally initialise RIP memory, required if registering modules later>
    SwLeMemInit (...)

    SwLeSetRasterCallbacks (...)

    SwLePgbSetCallback (...)

    SwLeAddCustomDevices (...)

    <optionally register core modules and interfaces>
    SwRegisterCMM(...)
    SwRegisterHTM(...)
    SwRegisterRDR(...)
    SwRegisterPFIN(...)

    SwLeStart (...)

    for ( <all jobs to be processed> ) {

      <open input stream>

      SwLeJobStart (...)

      for (;;) {

        <get next buffer of data from input stream>

        if ( <no more incoming data> )
          break;

        if (! SwLePs (...)) {

          <handle abnormal job termination>

          break;
        }
      }

      SwLeJobEnd (...)
    }

    SwLeStop()

    SwLeSDKEnd()
  }

  SwLeShutdown()

  \endverbatim

 * The call to \c SwLeSDKStart() prepares the memory arena that the RIP will
 * use, and initialises some support libraries.
 * The call to \c SwLeStart() creates a new thread, which becomes
 * the thread on which the RIP executes. Having spawned the secondary thread
 * and booted the RIP, \c SwLeStart() returns control to the caller, allowing
 * it to go on and use the \c SwLeJobStart() and \c SwLePs() to feed
 * job data to the RIP in a loop. The RIP thread continues running
 * independently throughout. \c SwLeJobEnd() notifies the RIP thread of the
 * end of the job data. To shutdown, \c SwLeStop() runs a special job that
 * causes the RIP thread to exit, \c SwLeSDKEnd() then terminates the support
 * libraries, and \c SwLeShutdown() performs some final shutdown actions.
 */


/**
 * \cond LESDK
 * \mainpage Harlequin RIP SDK Example Skin Documentation
 * \endcond
 *
 * \section intro_sec Introduction
 *
 * The Harlequin RIP SDK allows you to use the Harlequin RIP in a
 * wide range of applications, from host-based servers and drivers, to
 * embedding in hardware devices.
 *
 * The Harlequin RIP achieves this flexibility by being completely
 * independent of OS and hardware platforms.  Because of this,
 * applications based on the RIP must wrap it with a \e skin of
 * service code to provide the necessary platform integration.
 *
 * The Harlequin RIP SDK contains an example skin implementation,
 * to show you how you would implement both required and optional skin
 * features in a real application.
 *
 * The example skin is distributed as C source code and builds on
 * various major operating systems.  In some variants of the SDK, the
 * example skin and core Harlequin RIP are additionally available
 * as a pre-built command-line executable RIP application.
 *
@ifnot EMBEDDED
@if XPS
 * Some configurations of the Harlequin RIP ship with only a special
 * "reference" API.  This API is also visible in the Harlequin RIP
 * SDK: see refapi.h and \ref refapipage.
@endif
@endif
 *
 * \section how_to_use What this documentation covers
 *
 * This documentation is API-level reference documentation for the
 * example skin, and is generated directly from comments in the source
 * code of the example skin.
 *
@if EMBEDDED
 * The majority of the documentation is about the "skinkit"
 * code module, described below.
@else
 * The majority of the documentation is about the "skinkit" and
 * "skintest" code modules, described below.
@endif
 * In addition, this
 * documentation contains interface descriptions generated from header
 * files from the core RIP interfaces, and from security and memory
 * management interfaces.
 *
@ifnot EMBEDDED
@if XPS
 * The documentation also covers example code for an Windows Vista XPS
 * filter pipeline (XPSDrv) filter, which uses the Harlequin RIP as an
 * XPS to raster output processor.
 *
@endif
@endif
@if LESEC
 * A security API is documented separately.  (See lesec.h.)
 *
@endif
 * \section skin_sec The skin code is example code
 *
 * It is important to note that the example skin is not recommended
 * for direct use in production code.  The skin is designed to be
 * simple and clear, and sometimes this means the code is not as
 * efficient, nor its features as thorough, as would be required for
 * use in a production-quality application.  Global Graphics
 * recommends treating the skin only as an illustration of how to
 * implement required RIP devices and platform-specific services.
 *
@ifnot EMBEDDED
 * \section skin_modules The skinkit and skintest modules
 *
 * The example skin comes in two main modules: one called "skinkit"
 * (see \ref skinkit) and another called "skintest" (see \ref
 * skintest).  Essentially, you can think of skinkit as implementing those
 * features that any RIP-based application might require,
 * while skintest implements the specific details of a
 * command-line RIP.  In practice, the division between the two is not
 * quite as clear as that, but it is still a useful way to think about
 * the skinkit and skintest separation.
 *
@if XPS
 * \section xpsdrv_module The xpsdrv module
 *
 * The kit contains an example Windows Vista XPS filter pipeline
 * (XPSDrv) filter, which uses the Harlequin RIP as an XPS to raster output
 * processor.  This is embodied in a filter DLL and a server process
 * called RIPServer.exe.  They are built from code found in the xpsdrv
 * folder.  See \ref xpsdrv.
 *
@endif
@endif
 * \section skin_high_level_functions Further reading
 *
 * To understand how the example skin controls the RIP at the highest level,
 * look at the file skinkit.h where the functions SwLeSDKStart(),
 * SwLeStart(), SwLeJobStart(), SwLePs(), SwLeJobEnd(), SwLeStop(),
 * SwLeSDKEnd() and various associated callback function types, are declared.
 * These example functions (implemented in skinkit.c) configure the RIP,
 * start it up, and shut it down, as well as controlling job submissions to
 * it.
 *
 * Callback functions from the RIP are set using SwLeSetRasterCallbacks().
 * These functions are used to handle raster output, to poll the skin for
 * errors, to indicate changes in the output type, and other functions.
 *
@if EMBEDDED
 * Handling output from a job is controlled in the OIL layer,
 * where OIL_RasterCallback() and OIL_AbortCallback() receive
 * calls from the RIP.  See the Embedded product documentation
 * for more information.
@else
 * Configuring the output type is handled by PageBufferSelect() in skintest.c,
 * which installs a set of callbacks specific to the output type.
 *
 * Handling output from a job is controlled by the raster callback function,
 * which in many cases calls upon RasterCallback() (skintest.c) with
 * format-specific delegate functions such as TIFF (tiffrast.h and
 * tiffrast.c).  These files define output-specific
 * raster-handling functions.  Another example, in oemraster.h and
 * oemraster.c, shows the minimum of code that forms the basis an of output
 * backend within the "skintest" command-line example application.
@endif
 */

/**
 * \defgroup skinkit Skinkit: Example RIP skin devices and other features
 * \ingroup leskin_group
 * \{
 */

/**
 * \brief Defines a callback function which receives bands of raster
 * delivered from the RIP for the current job.
 *
 * When raster data becomes available from the RIP, it is supplied one
 * band at a time to the host application by the RIP calling an
 * implementation of this callback, as supplied to SwLeSetRasterCallbacks().
 *
 * The raster data is received in \c pBuffer, and is
 * described by \c pRasterDescription, which among other
 * things gives the number of color channels, their order, the image
 * size and band size.
 *
 * The application needs to keep track of where the raster data is in
 * the overall page image, since it needs to know when the raster is
 * complete.
 *
 * The last band of data could be smaller than previous bands. Once
 * again, the application needs to work this out for itself.
 *
 * The data pointed to by the \c pRasterDescription will
 * remain constant during the repeated calls to the raster callback
 * function.  After the last band of raster is passed to the
 * application, the RIP frees up this \c pRasterDescription,
 * and so the application must take care not to reference it after
 * this point.
 *
 * At the end of a job, after a full set of raster callback
 * invocations has completed for the final page, an additional call is
 * made to the raster callback with NULL as the \c pBuffer
 * argument. The \c pRasterDescription argument is not valid
 * for this call. This last call signals the end of the job, and can
 * be used in cases where the raster handling work is not
 * self-contained on a per-page basis.
 *
 * \param pJobContext An opaque pointer to caller-managed data, as
 * originally provided by \c SwLeJobStart.
 *
 * \param pRasterDescription A pointer to a \c RasterDescription structure
 * describing the format of the data in \a pBuffer.
 *
 * \param pBuffer The rendered data for a band, or NULL for the end of job
 * callback.
 *
 * \retval TRUE
 * The buffer has been successfully handled.
 * \retval FALSE
 * If the raster callback function returns \c FALSE, the RIP will abort
 * page rendering with an I/O error, and no more calls to the raster
 * callback will be made for that page (or even job, depending on how
 * the PostScript error handling has been managed).
 *
 */
typedef HqBool (RIPCALL SwLeRASTERCALLBACK)(void *pJobContext,
                                            RasterDescription * pRasterDescription,
                                            uint8 * pBuffer);

/**
 * \brief A callback function which allows the skin to increase the
 * raster stride (byte offset between the start address of successive
 * raster lines).
 *
 * \param pJobContext An opaque pointer to caller-managed data, as
 * originally provided by \c SwLeJobStart.
 *
 * \param puStride Pointer to an unsigned integer which is set on
 * entry to the raster line length in bytes which the RIP is set to
 * use. The skin may increase this value to better suit its
 * requirements, for example to force line start addresses to coincide
 * with cache lines or DMA ranges. Of course, any increase will
 * necessarily mean a larger memory requirement to hold the raster.
 *
 * \returns This function should return 0 for success, -1 for an error (if
 * the stride value is outside of an acceptable range), or +1 to indicate
 * that the RIP should call the skin again.
 */
typedef int32 (RIPCALL SwLeRASTERSTRIDE)(void *pJobContext,
                                         uint32 *puStride);

/**
 * \brief A callback function which gives the skin the details of the
 * raster it's about to be handed, and allocate memory to contain it.
 *
 * The use of this callback is optional, and it is set via a call to
 * \c SwLeSetRasterCallbacks. Together with \c SwLeRASTERDESTINATION,
 * it gives a mechanism for the skin to provide all of the memory into
 * which the RIP should render, which in turn eliminates memory
 * copying operations and so improves performance.
 *
 * \param pJobContext An opaque pointer to caller-managed data, as
 * originally provided by \c SwLeJobStart.
 *
 * \param pRasterRequirements A pointer to the \c RASTER_REQUIREMENTS
 * structure shared by RIP and skin.
 *
 * \param fRenderingStarting There are two points in the sequence of
 * processing a job at which the RIP will call this type of function. The
 * first is when the page device has just changed, and the second is when
 * rendering is starting imminently. If the latter is true, the
 * \a fRenderingStarting flag will be TRUE and no further changes to the page
 * device are possible.
 *
 * \returns This function should return 0 for success, -1 for an error, or +1
 * to indicate that the RIP should call the skin again.
 *
 * \note It may be that the best approach is for the skin to allocate raster
 * memory during the earlier calls to this type of function, i.e. with \c
 * fRenderingStarting is FALSE, to ensure that the raster memory is always
 * available. Depending on configuration, it's possible that if raster memory
 * allocation happens later that the RIP uses all available memory and
 * there's not enough left for raster memory when needed. However, if the
 * skin is in a position to guarantee that a later allocation when \c
 * fRenderingStarting is TRUE will succeed, that approach is preferable - not
 * least because the page device changes quite often - perhaps a dozen or
 * more times per job. When Harlequin Parallel Pages is enabled, calls to
 * this function may be made from both interpreter (\c fRenderingStarting
 * FALSE) and renderer threads (\c fRenderingStarting TRUE) simultaneously.
 * The skin must \e not assume that a renderer call is related to the most
 * recent interpreter call. The \c eraseno field in the
 * \c RASTER_REQUIREMENTS structure can be used to correlate calls from the
 * interpreter with subsequent renderer calls. Not every interpreter
 * call will have an associated call from a renderer. The \c eraseno field
 * increases monotonically for each raster description, so when a renderer call
 * is received, all buffers allocated for raster descriptions with lower
 * values of \c eraseno can be discarded.
 *
 * The RIP will set all fields in the \c RASTER_REQUIREMENTS argument,
 * all of them except two are used by the RIP to pass information to the
 * skin:
 *
 * \c have_framebuffer is a flag which the callee can set to indicate that it
 * has will allocate the memory for all of the band buffers (a framebuffer).
 * This can lead to extra efficiencies, by avoiding data copying. For any
 * particular value of the \c eraseno field in \c RASTER_REQUIREMENTS
 * argument, the skin must \e not change its mind about the setting of
 * \c have_framebuffer between the interpreter and renderer calls to this
 * function.
 *
 * \c handled is a flag which the callee sets when it has decisively
 * dealt with the call. Not setting the flag before returning
 * indicates to the RIP that the callee would like to be called again
 * with the same parameters. This is used when the skin needs to delay
 * to allow some output or other work to progress before the RIP
 * continues. Note that the maximum such delay should be around 100
 * milliseconds.
 */
typedef int32 (RIPCALL SwLeRASTERREQUIREMENTS)(void *pJobContext,
                                               RASTER_REQUIREMENTS *pRasterRequirements,
                                               HqBool fRenderingStarting);

/**
 * \brief A callback function which asks the skin to provide a memory
 * address range into which to render.
 *
 * The use of this callback is optional, and it is set via a call to
 * \c SwLeSetRasterCallbacks. Together with \c SwLeRASTERREQUIREMENTS,
 * it gives a mechanism for the skin to provide all of the memory into
 * which the RIP should render, which in turn eliminates memory
 * copying operations and so improves performance.
 *
 * For the given frame and band number, the skin should calculate the
 * correct destination range and return it in \c memory_base and
 * \c memory_ceiling. This range is exclusive, i.e. \c memory_ceiling is
 * the address of the first byte that the RIP is not allowed to write into.
 *
 * \c handled is a flag which the callee sets when it has decisively
 * dealt with the call. Not setting the flag before returning
 * indicates to the RIP that the callee would like to be called again
 * with the same parameters. This is used when the skin needs to delay
 * to allow some output or other work to progress before the RIP
 * continues, or to split the rendering of a single RIP band into
 * multiple passes in order to better suit the rest of the workflow.
 *
 * \param pJobContext An opaque pointer to caller-managed data, as
 * originally provided by \c SwLeJobStart.
 *
 * \param pRasterDestination The RASTER_DESTINATION structure shared by RIP
 * and skin.
 *
 * \param nFrameNumber The index of the current frame.
 *
 * \returns This function should return 0 for success, -1 for an error (if
 * the stride value is outside of an acceptable range), or +1 to indicate
 * that the RIP should call the skin again.
 */
typedef int32 (RIPCALL SwLeRASTERDESTINATION)(void *pJobContext,
                                              RASTER_DESTINATION * pRasterDestination,
                                              int32 nFrameNumber);

/**
 * \brief Defines a callback function which receives monitor
 * information from the RIP for the current job.
 *
 * Text is streamed, so error messages and so on may span more than one call.
 * Used in \c SwLeStart().
 */
typedef void (SwLeMONITORCALLBACK)(uint32 cbBuffer, uint8 * pBuffer);

/**
 * \brief Defines a callback function which is called when the RIP
 * exits.  Used in \c SwLeSetRipExitFunction().
 */
typedef void (SwLeRIPEXITCALLBACK) (int32 errorCode, uint8 * pszText);

/**
 * \brief Defines a callback function which is called when the RIP
 * reboots.  Used in \c SwLeSetRipRebootFunction().
 */
typedef void (SwLeRIPREBOOTCALLBACK) (void);


/* Functions */

/**
 * \brief Initializes skinkit variables prior to starting the RIP.
 *
 * This function must be called before each attempt to start or
 * restart the RIP.  It initialises or re-initialises variables
 * which skinkit code expects to have known initial values.
 *
 * \param pContext Currently unused - clients should pass NULL.
 *
 * \return Currently always returns TRUE.
 *
 * \note This should be the first SDK function called by the skin.
 */
HqBool SwLeInitRuntime(void * pContext);

/**
 * \brief Initialise SDK support libraries for the RIP.
 *
 * The RIP relies upon several support libraries to operate. The MPS (Memory
 * Pool System) is used for memory management. RDR provides for decoupled
 * discovery, overriding, and priority management of resources. Events
 * provides an observer interface with prioritisation and filtering. Timelines
 * provides lifespan communication, progress and contextualisation. Timers
 * provide timed callbacks.
 *
 * <p>These support libraries are initialised and finalised separately from
 * the RIP because they provide functions generally useful for the
 * RIP skin, they allow modules to be prepared for use by the RIP, they
 * allow configuration of libraries that the RIP uses before RIP startup. The
 * lifetime of the support libraries may exceed one RIP instantiation.
 *
 * <p>The core RIP and the demonstration skin share the same memory arena.
 * However, the skin will have its own pool within that arena. This call
 * starts by creating the MPS memory arena and the skin's MPS memory pool. It
 * is possible to apply a constraint to the amount of memory that the core
 * RIP and the skin are permitted to use in combination. Use the
 * <code>RIP_workingSizeInBytes</code> parameter to do this.
 *
 * <p>It is also possible for the RIP to work within a region of
 * memory that has been pre-allocated by the host application. Use
 * the <code>pMemory</code> argument to do this.
 *
 * <p>This function <em>must</em> be called after \c SwLeInitRuntime(), but
 * before booting the RIP (i.e., \c SwLeMemInit() or \c SwLeStart()), and
 * before any use of the other skin memory-management functions defined in
 * file mem.h are called.
 *
 * \param[out] RIP_maxAddressSpaceInBytes The size of maximum address space
 * the RIP can use. This value is discovered by the memory library, it is
 * informational.
 *
 * \param[in,out] RIP_workingSizeInBytes A pointer to a value containing the
 * size of working memory permitted for the RIP, measured in bytes. If this
 * is zero on entry, the function will attempt to calculate a suitable default
 * based upon the memory configuration of the host machine. If details of the
 * memory configuration cannot be determined, a hard-coded default will be
 * applied. On exit, the value is set to the actual size of working memory
 * applied for the RIP.
 *
 * \param[in] pMemory Points to a caller-allocated memory block that the
 * RIP should use. If this is non-NULL, the RIP will work within this
 * memory block, rather than allocating its own memory. The size of
 * the block is defined by <code>RIP_workingSizeInBytes</code>, which must
 * not be zero. If this pointer is NULL, then the RIP will allocate
 * and manage its own memory, suitably constrained by
 * <code>RIP_workingSizeInBytes</code>.
 *
 * \param[in] pSysMemFns Points to a structure providing the suite of
 * system memory handling functions for use by <code>SysAlloc</code>,
 * and <code>SysFree</code>.  If this is NULL the default functions
 * are used, which are implemented using the OS
 * <code>malloc</code> and <code>free</code>. These allocation functions
 * are used by the skin's example raster handlers for large allocations that
 * should not be included in the MPS arena or constrained by
 * <code>RIP_workingSizeInBytes</code>.
 *
 * \param[out] reasonText An optional pointer to receive failure reasons. If
 * non-NULL, this will be set to the reason that this function failed, or NULL
 * if this function succeeded.
 *
 * \retval TRUE The SDK initialisation succeeded. In this case,
 *              \c RIP_maxAddressSpaceInBytes is set to size of the maximum
 *              address space the RIP can use, \c RIP_workingSizeInBytes is
 *              set to the actual size of working memory the RIP will use,
 *              and \c reasonText is set to NULL.
 * \retval FALSE The SDK initialisation failed.
 *
 * \note This function must be called after \c SwLeInitRuntime(), and before
 * \c SwLeMemInit() or \c SwLeStart(). If this function succeeds, the caller
 * should call \c SwLeSDKEnd() before ending the application. The RIP
 * may be re-started without shutting down the SDK libraries if it
 * can share the same memory configuration. If the memory configuration is to be
 * changed, \c SwLeSDKEnd() and \c SwLeSDKStart() must be called again before
 * restarting the RIP.
 */
HqBool SwLeSDKStart(
  size_t *RIP_maxAddressSpaceInBytes,
  size_t *RIP_workingSizeInBytes,
  void *pMemory,
  SysMemFns *pSysMemFns,
  uint8 **reasonText);

/**
 * \brief Shutdown the SDK support libraries.
 *
 * This call is required if \c SwLeSDKStart() succeeds. It should be called
 * before \c SwLeShutdown(), before exiting the application. The RIP may be
 * re-started without shutting down the SDK libraries if it can share the
 * same memory configuration. If the memory configuration is to be changed,
 * \c SwLeSDKEnd() and \c SwLeSDKStart() must be called again before
 * restarting the RIP.
 *
 * \param exitCode Indicates whether the application is shutting down in an
 * error state. The argument should be the return value from \c SwLeStop().
 * It will be zero if the RIP shut down successfully, non-zero if the
 * application is to indicate an error state.
 */
void SwLeSDKEnd(int32 exitCode) ;

/**
 * \brief Pre-initializes the RIP, giving it the memory arena prepared by
 * \c SwLeSDKStart().
 *
 * This call is optional, the alternative being to call \c SwLeStart()
 * directly. However this function must be called if RIP callbacks or modules
 * must be registered before the RIP is fully booted by \c SwLeStart(). If this
 * call is made, it must be after \c SwLeSDKStart().
 *
 * \param RIP_maxAddressSpaceInBytes Size of virtual memory available to RIP,
 * as returned by \c SwLeSDKStart().
 *
 * \param RIP_workingSizeInBytes Size of the memory buffer that the RIP
 * will work in, as returned by \c SwLeSDKStart().
 *
 * \param RIP_emergencySizeInBytes Additional memory beyond \c RIP_workingSizeInBytes to
 * use before resorting to partial paint. Only meaningful if \c pMemory is \c
 * NULL.
 *
 * \param pMemory A pointer to an allocated buffer of size
 * \c RIP_workingSizeInBytes, or \c NULL.  If it is \c NULL, the skin allocates
 * \c RIP_workingSizeInBytes bytes of memory itself.
 */
void SwLeMemInit(size_t RIP_maxAddressSpaceInBytes,
                 size_t RIP_workingSizeInBytes, size_t RIP_emergencySizeInBytes,
                 void * pMemory);

/**
 * \brief Adds \c DEVICETYPE objects to the array passed to the RIP
 * during startup.
 *
 * This call is optional.  It may be called once before \c SwLeStart()
 * to include additional DEVICETYPEs in the array passed to the the RIP.
 *
 * \param nCustomDevices The number of DEVICETYPEs to add.
 *
 * \param ppCustomDevices Pointer to array of DEVICETYPEs to be add.
 */
void SwLeAddCustomDevices(int32         nCustomDevices,
                          DEVICETYPE ** ppCustomDevices);

/**
 * \brief Starts the Harlequin RIP, giving it the memory arena prepared by
 * \c SwLeSDKStart().
 *
 * Text strings which the RIP outputs (error messages, information
 * messages, and jobs writing to stdout and stderr) are delivered to
 * the callback function supplied in the parameter
 * \c pfnMonitor (when not NULL) for the duration of the RIP
 * session.
 *
 * In response to this call, the skin starts a thread which calls the
 * core RIP interface function \c SwStart(), passing it the memory and
 * the device types defined in the skin.  On completion (that is, the
 * first downward call for more data, namely
 * \c bytesavailable on the \c %%config device)
 * this function returns in its own thread.
 *
 * \param RIP_maxAddressSpaceInBytes Size of virtual memory available to RIP,
 * as returned by \c SwLeSDKStart().
 *
 * \param RIP_workingSizeInBytes Size of the memory buffer that the RIP
 * will work in, as returned by \c SwLeSDKStart().
 *
 * \param RIP_emergencySizeInBytes Additional memory beyond \c RIP_workingSizeInBytes to
 * use before resorting to partial paint. Only meaningful if \c pMemory is \c
 * NULL.
 *
 * \param pMemory A pointer to an allocated buffer of size
 * \c RIP_workingSizeInBytes, or NULL.  If it is NULL, the skin allocates
 * \c RIP_workingSizeInBytes bytes of memory itself.
 *
 * \param pfnMonitor A pointer to a monitor callback function
 * which the skin must implement if it wishes to receive monitor
 * information from the RIP.  This argument can be NULL, in which case
 * no monitor information will be sent to the skin.
 *
 * \retval TRUE  The RIP started successfully.
 * \retval FALSE The RIP did not start successfully.
 */
HqBool SwLeStart(size_t RIP_maxAddressSpaceInBytes,
                 size_t RIP_workingSizeInBytes,
                 size_t RIP_emergencySizeInBytes,
                 void *pMemory, SwLeMONITORCALLBACK *pfnMonitor);

/**
 * \brief Allows the caller to supply timers for triggering the tickle
 * callbacks.
 *
 * It is optional to call this function, since the RIP skin has built-in
 * tickle timers that function adequately on most platforms.
 *
 * For more information about tickle timer management, see
 * \c SetSkinTimerExpiredFn().
 *
 * \param pfnSwStartTickleTimer Function to start the tickle timer. This
 * will be called whenever the RIP (re-)boots.
 *
 * \param pfnSwStopTickleTimer Function to stop the tickle timer. This
 * will be called whenever the RIP shuts down. When the interpreter reboots
 * due to a serious processing error, \c pfnSwStopTickleTimer will be
 * called, followed immediately by \c pfnSwStartTickleTimer, effectively
 * allowing the client code to "reboot" any state used to manage the
 * timers.
 *
 */
void SwLeSetTickleTimerFunctions(SwStartTickleTimerFn *pfnSwStartTickleTimer,
                                 SwStopTickleTimerFn *pfnSwStopTickleTimer);
/**
 * \brief Allows the caller to supply callback function to be called
 * when the RIP exits.
 *
 * It is optional to call this function.
 *
 * \param pfnRipExit Function which is to be called whenever the
 * RIP exits.
 *
 */
void SwLeSetRipExitFunction(SwLeRIPEXITCALLBACK * pfnRipExit);

/**
 * \brief Allows the caller to supply callback function to be called
 * when the RIP reboots.
 *
 * It is optional to call this function.
 *
 * \param pfnRipReboot Function which is to be called whenever the
 * RIP reboots.
 *
 */
void SwLeSetRipRebootFunction(SwLeRIPREBOOTCALLBACK * pfnRipReboot);

/**
 * \brief Sets the number of renderer threads.
 *
 * It is optional to call this function. If called, it must be before
 * the first call to \c SwLeMemInit() or \c SwLeStart() for a RIP
 * instantiation.
 *
 * \param nThreads The number of threads usable by the RIP.
 *
 */
void SwLeSetRipRendererThreads(int32 nThreads);

/**
 * \brief Request that the RIP shutdown.
 *
 * This function should only be called in between job boundaries. It submits
 * a shutdown PS job to the RIP, then waits for the RIP to exit.
 *
 * \returns The exit status of the RIP. This will be zero if the RIP shutdown
 * cleanly, non-zero otherwise. The value returned should be passed to
 * \c SwLeSDKEnd().
 */
int32 SwLeStop(void) ;

/**
 * \brief Waits for the RIP thread started via \c SwLeStart() to exit.
 *
 * This function waits for the RIP to quit cleanly.
 *
 * The preferred method to terminate the RIP is to call \c SwLeStop() between
 * jobs. However, the RIP may also be terminated by starting a job, and calling
 * \c SwLePs() with
 * <pre>
 * $printerdict /superstop dup put systemdict begin quit
 * </pre>
 * and then waiting for the RIP to finish using this function.
 */
void SwLeWaitForRIPThreadToExit(void);

/**
 * \brief Returns the exit status of the RIP.
 *
 * The exit status is only valid after the RIP thread terminates (i.e., after
 * return from the \c SwLeStop() function). It is zero if the RIP terminated
 * cleanly, non-zero otherwise.
 */
int32 SwLeExitCode(void);

/**
 * \brief Performs shutdown operations when the application exits.
 *
 * This function must be called after the RIP and the SDK have been shutdown
 * for the last time, before the application exits.
 */
void SwLeShutdown(void);

/**
 * \brief Prepares the Harlequin RIP to receive a PostScript language
 * job.
 *
 * This function must be called before feeding any job data to the RIP. It
 * must be called after booting the RIP using \c SwLeStart(). It must not be
 * called while a job is in progress, and returns \c FALSE if that happens.
 *
 * \param cbBuffer The number of characters in the (not necessarily
 * nul-terminated) string \c pBuffer.
 *
 * \param pBuffer A pointer to the "config job": a PostScript language
 * fragment which is run at save level 1 to set the correct
 * environment for the job, or NULL.  This argument can be NULL, in
 * which case \c cbBuffer should be zero.
 *
 * \param pJobContext An arbitrary pointer to caller-managed data. This
 * will be passed as the first argument to the raster data callback and
 * other callbacks during execution of the job.
 *
 * \retval TRUE  If the job was started successfully. In this case,
 * \c SwLeJobEnd() must be called after the job data has been presented to
 * the RIP, even if a subsequent \c SwLePs() call returns an error.
 * \retval FALSE If the job was not started successfully. The application may
 * call \c SwLeJobStart() again, with corrected configuration data, or may
 * terminate with an error.
 *
 * (Internally, this call causes \c bytesavailable to be set on the
 * config device, which will then cause a read on the config
 * device. That will return \c pBuffer contents for \c setrealdevice,
 * if any, and then supplies \c %%ps% (formerly \c %%console, best
 * renamed, and put in its own source file) as the file to open for
 * reading the job from \c (stdin). Open then gets called on \c %%ps%,
 * which can return a handle straight away, and then the core RIP
 * calls \c read_file on the \c %%ps% device. Monitor callbacks and
 * abort callbacks may happen in the meantime. At this point
 * \c SwLeJobStart() can return, and read waits on a subsequent host
 * call. An obvious further extension to the example skin here is a
 * function pointer for progress callbacks.)
 *
 * The configuration job in \c pBuffer should finish with either two filestreams
 * and true on the PostScript stack, or false on the stack. If the top of the
 * stack is true, the file objects will be used as the standard input and
 * standard output for a PostScript job. The SDK normally uses this setup
 * initiate a job:
 * <pre>
 *   (%%console%) dup (r) file exch (w) file true
 * </pre>
 * If the top of the stack is false, the server loop will be repeated. This
 * allows jobs containing nothing but enquiry code or configuration data to
 * be run.
 */
HqBool SwLeJobStart(uint32 cbBuffer,
                    uint8 *pBuffer,
                    void *pJobContext);

/**
 * \brief Sets the callback functions that the RIP will call back to present
 * raster data to the skin.
 *
 * \param pfnRasterStride Routine via which the RIP informs the skin
 * of the offset between successive lines in the output, and gives it
 * the opportunity to modify this if necessary.
 *
 * \param pfnRasterRequirements Routine via which the RIP informs the
 * skin of its memory requirements, and the skin can allocate the
 * buffers it requires.
 *
 * \param pfnRasterDestination Pointer to a function which returns a
 * memory range into which the RIP should render a given band.
 *
 * \param pfnRaster Called to deliver raster data to the client application.
 * (Failure to supply a raster callback means that any RIP output
 * will be lost. For jobs with graphical content, this is seldom
 * desirable. However, it can be useful for purely configurational
 * jobs, such as those used to mount devices.)
 *
 * \note \a pfnRasterRequirements and \a pfnRasterDestination work
 * in tandem. It makes no sense to define one and not the other: either both
 * should be defined, or neither.
 */
void SwLeSetRasterCallbacks(SwLeRASTERSTRIDE *pfnRasterStride,
                            SwLeRASTERREQUIREMENTS *pfnRasterRequirements,
                            SwLeRASTERDESTINATION *pfnRasterDestination,
                            SwLeRASTERCALLBACK *pfnRaster);

/**
 * \brief Passes a chunk of PostScript language data to the Harlequin
 * RIP.
 *
 * This function can be called repeatedly to pass an entire
 * PostScript job to the RIP in chunks.
 *
 * This function will fail and return \c FALSE unless
 * \c SwLeJobStart() has been called first.
 *
 * To make the RIP read directly from a file, use the PostScript
 * fragment shown in the Harlequin RIP Extensions Manual to set
 * \c stdin.  The following is a simple variation on that
 * fragment:
 *
 * <pre>
 * (\<filename\>) (r) file cvx dup
 * (%%stdout%) (w) file
 * statusdict /setstdio get exec
 * exec
 * </pre>
 *
 * To run a PDF file, use a PostScript language fragment to call the
 * Harlequin RIP extension operator \c pdfexec on an opened
 * file.
 *
 * In the event of an unstopped PostScript language error or other
 * abnormal event, the function will return \c FALSE. The caller should
 * call \c SwLeJobEnd() after detecting an error.
 *
 * (Internally, this returns the buffer contents to the read calls on
 * \c %%ps%, perhaps in chunks if bigger than the read
 * buffer the RIP supplies, and on completion waits for the next read
 * call or close call before returning. Errors are detected by an
 * unexpected call to close on \c %%ps%. While waiting for
 * these events various callbacks may also be requested of the
 * function, such as \c *pfnRaster.)
 *
 * When using Harlequin Parallel Pages, \c SwLePs() will return when the
 * interpreter has accepted all of the input. This return may be before all
 * of the pages in a job have been rendered. It is possible that a page
 * may encounter a render error after \c SwLePs() returns successfully. A
 * subsequent call to \c SwLePs() or \c SwLeJobEnd() will return this error.
 * If the skin needs to wait until pages are rendered for each buffer of data
 * it should provide its own synchronisation between the render callbacks
 * and the \c SwLePs() calls.
 *
 * The preferred method to terminate the RIP is to call \c SwLeStop() between
 * jobs. However, the RIP may also be terminated by calling this function with
 * <pre>
   $printerdict /superstop dup put systemdict begin quit
 * </pre>
 * and then waiting for the RIP to finish using \c SwLeWaitForRIPThreadToExit().
 *
 * \param cbBuffer The length, in bytes, of the PostScript buffer
 * \c pBuffer submitted to the RIP.
 *
 * \param pBuffer The PostScript buffer to submit to the RIP.
 *
 * \return \c TRUE if the PostScript was processed without error, and
 * \c FALSE otherwise.
 */
HqBool SwLePs(uint32 cbBuffer, uint8 *pBuffer);


/**
 * \brief Terminates the current job.
 *
 * It will return \c FALSE if SwLeJobStart() has not been
 * called first.
 *
 * (Internally, this function closes the \c %%ps% file by
 * returning zero bytes, which has the effect of calling close and
 * terminating the job.  Could be done by SwLePs() passing
 * a zero length buffer, but it is cleaner this way.)
 *
 * \return \c TRUE on success, and \c FALSE otherwise.
 */
HqBool SwLeJobEnd(void);

/**
 * \brief Indicates whether an error occurred in the most recent chunk
 * of job data that was processed by the RIP.
 *
 * However the return values from \c SwLePs() and \c SwLeJobEnd() are
 * probably a more convenient means of error detection.
 *
 * \return \c TRUE if an error was signalled by the RIP, otherwise
 * \c FALSE.
 */
HqBool SwLeProcessingError(void);

/**
 * \brief Indicates to the skin that the RIP is now processing the job.
 *
 * When job data is not being provided via the console device (i.e., via \c
 * SwLePs()), this function should be called when the RIP asks for job data.
 */
void SwLeProcessingPs(void);

/**
 * \brief Indicates to the skin that the RIP has finished processing the job.
 *
 * When job data is not being provided via the console device (i.e., via \c
 * SwLePs()), this function should be called when the RIP stops asking for
 * job data, e.g., by closing the stream providing the data.
 */
void SwLeProcessingJobEnd(void);

/**
 * \brief Get a skin handle to a mounted RIP device, which can then be
 * used to get more information about the device.
 *
 * The skin handle should be passed back into functions that
 * deal with the device, such as \c SwLeGetIntDevParam(). This function
 * is essentially the skin equivalent of \c SwFindDevice(). However,
 * it is somewhat less powerful, because a skin device handle cannot
 * be used to access the full range of the core RIP device interface.
 *
 * \param pszDevice NUL-terminated name of the device. If no device
 * with the given name is mounted, \c NULL will be returned.
 *
 * \return A skin handle to the device, which can subsequently be passed
 * into functions such as \c SwLeGetIntDevParam(). Once obtained, a
 * skin handle remains valid for as long as the underlying device is
 * mounted.
 */
void* SwLeGetDeviceHandle(uint8 *pszDevice);

/**
 * \brief A convenience function to get the current value of an
 * integer device parameter.
 *
 * The caller must first obtain a valid skin device handle, which
 * is then passed into this function. For example, to get the
 * \c PageNumber parameter of the \c pagebuffer device, use the
 * following code:-
 *
   \verbatim

   void *pgbdev = SwLeGetDeviceHandle( (uint8*) "pagebuffer" );
   if ( pgbdev != NULL )
   {
     int32 pageno;
     int32 result = SwLeGetIntDevParam( pgbdev, (uint8*) "PageNumber", &pageno );
   }

   \endverbatim
 *
 * \param pDeviceHandle A valid skin device handle, as obtained from a
 * prior call to \c SwLeGetDeviceHandle(). It is safe to pass \c NULL, but
 * the function will fail and return \c FALSE.
 *
 * \param pszParamName NUL-terminated name of the required parameter. This
 * should not use PostScript syntax - there should be no leading slash.
 * For example, \c "PageNumber" is correct, while \c "/PageNumber" is not.
 *
 * \param pInt If the function succeeds, this receives the current value
 * of the specified device parameter. If the function fails, the contents
 * are left undefined.
 *
 * \return \c TRUE if the function succeeds; \c FALSE if the function
 * fails. Reasons why the function may fail include the following:-
 *
 * - \c NULL was passed as \c pDeviceHandle.
 * - The device handle is no longer valid, because the device was dismounted
 *   since the handle was obtained.
 * - The name of the parameter is unknown to the device. It might be mis-spelled,
 *   for instance, or have a spurious leading slash character.
 * - The parameter is not of integer type.
 *
 * This convenience routine does not distinguish between the reasons
 * for failure. Use the core RIP interface directly for richer
 * diagnostics.
 */
HqBool SwLeGetIntDevParam(void *pDeviceHandle,
                          uint8 *pszParamName,
                          int32 *pInt);

/**
 * \brief A convenience function to get the current value of a
 * boolean device parameter.
 *
 * For full documentation, see \c SwLeGetIntDevParam(). The contract of
 * this function is the same, except that it specializes for booleans
 * instead of integers.
 *
 * \param pDeviceHandle A valid skin device handle.
 *
 * \param pszParamName NUL-terminated name of the required parameter.
 *
 * \param pBool If the function succeeds, this receives the current value
 * of the specified device parameter. If the function fails, the
 * contents are left undefined.
 *
 * \return \c TRUE if the function succeeds, otherwise \c FALSE.
 */
HqBool SwLeGetBoolDevParam(void *pDeviceHandle,
                           uint8 *pszParamName,
                           HqBool *pBool);

/**
 * \brief A convenience function to get the current value of a
 * floating-point (real)  device parameter.
 *
 * For full documentation, see \c SwLeGetIntDevParam(). The contract of
 * this function is the same, except that it specializes for floats
 * instead of integers.
 *
 * \param pDeviceHandle A valid skin device handle.
 *
 * \param pszParamName NUL-terminated name of the required parameter.
 *
 * \param pFloat If the function succeeds, this receives the current value
 * of the specified device parameter. If the function fails, the
 * contents are left undefined.
 *
 * \return \c TRUE if the function succeeds, otherwise \c FALSE.
 */
HqBool SwLeGetFloatDevParam(void *pDeviceHandle,
                            uint8 *pszParamName,
                            float *pFloat);

/**
 * \brief A convenience function to get the current value of a
 * string device parameter.
 *
 * For full documentation, see \c SwLeGetIntDevParam(). The contract of
 * this function is the same, except that it specializes for strings
 * instead of integers.
 *
 * The string memory returned by this function is owned by the
 * underlying device, not by the caller. The caller should not free
 * the string. The caller should make its own copy if the data is
 * needed persistently.
 *
 * \param pDeviceHandle A valid skin device handle.
 *
 * \param pszParamName NUL-terminated name of the required parameter.
 *
 * \param ppStr If the function succeeds, this receives a pointer to
 * the parameter value. This string data is not necessarily NUL-terminated.
 * The length of the string's valid portion is returned separately. If
 * the function fails, the contents are undefined.
 *
 * \param pStrLen If the function succeeds, this receives the number
 * of valid character bytes in the string. If the function fails, the
 * contents are undefined.
 *
 * \return \c TRUE if the function succeeds, otherwise \c FALSE.
 */
HqBool SwLeGetStringDevParam(void *pDeviceHandle,
                             uint8 *pszParamName,
                             uint8 **ppStr,
                             int32 *pStrLen);

/**
 * \brief The type of callback functions triggered when a parameter is
 * changed.
 *
 * Callback functions may be registered against some parameter sets, to
 * monitor changes to their values. This is often used to select the backend
 * behavior when the PageBufferType pagebuffer parameter is changed. The skin
 * should respond by calling \c SwLeSetRasterCallbacks with appropriate
 * callback functions for the selected backend.
 *
 * \param pJobContext An opaque pointer to caller-managed data, as
 * originally provided by \c SwLeJobStart.
 *
 * \param param A generic pointer, either pointing to an integer, a float,
 * a boolean (integer), or a NUL-terminated string containing the new value
 * of the pagebuffer parameter. The value pointed to should not be changed.
 *
 * \returns One of the Param* enumeration values from swdevice.h.
 *
 * \note This callback deliberately does not expose the name or type of the
 * parameter. Individual callbacks should be registered for each parameter of
 * interest, and the function used to register the callback should verify the
 * type of callback via a separate parameter.
 */
typedef int32 (RIPCALL SwLeParamCallback)(void *pJobContext,
                                          const void *param);

/**
 * \brief Tidy up and exit.
 *
 * \param text Reason for exit
 * \param n The exit code
 *
 * This function is called by the RIP when it exits. It is responsible for
 * calling calling the function registered by \c SwLeSetRipExitFunction().
 */
void SkinExit(int32 n, uint8 *text);

/**
 * \brief Called when RIP reboots
 *
 * This function is called by the RIP when it exits. It is responsible for
 * calling calling the function registered by \c SwLeSetRipRebootFunction().
 */
void SkinReboot(void);

/**
 * \brief Display a message through the monitor callback, if provided.
 *
 * \param cbData The length, in bytes, of a NUL-terminated monitor
 * information string.
 *
 * \param pszMonitorData The NUL-terminated monitor information
 * string.
 */
void SkinMonitorl(int32 cbData, uint8 *pszMonitorData) ;

/**
 * \brief Display a formatted message through the monitor callback, if
 * provided.
 *
 * The semantics are roughly along the lines of \c printf() or \c fprintf(),
 * but with output going to the supplied monitor callback, so as to isolate
 * the skin from the notion of standard C streams. This function should
 * be used in place of C stdio functions whenever possible.
 *
 * Messages are formatted into a fixed-size buffer. The message will be
 * truncated if it does not fit into the buffer. If this happens, an
 * additional message will be sent to the monitor callback, warning about
 * the truncation.
 *
 * If no monitor callback has been supplied, this function does nothing.
 *
 * \param pszFormat Format string, as per C library formatting functions.
 */
void SkinMonitorf(const char *pszFormat, ...);

/**
 * \brief Identical to \c SkinMonitorf(), where the vararg list is
 * explicit.
 *
 * \param pszFormat Format string, as per C library formatting functions.
 *
 * \param vlist Arguments for substitution into the format string.
 */
void SkinVMonitorf(const char *pszFormat, va_list vlist);

/**
 * \brief Set system error record level.
 *
 * \param errlevel Request level: 0 - no record;
 *                              1 - record errors except file/path not found.
 *                              2 - record all errors.
 * \return TRUE if fLevel is valid; FALSE otherwise.
 */
HqBool KSetSystemErrorLevel(int32 errlevel);

/**
 * \brief Get system error record level.
 */
int32 KGetSystemErrorLevel(void);

/**
 * \brief Record through the monitor callback system errors according
 * to the error level set by \c KSetSystemErrorLevel().
 *
 * Note that the error will not be recorded if no monitor callback has
 * been supplied.
 *
 * \param errcode Error code from the failed operation.
 *
 * \param errline Line number where error occurred. Use the \c __LINE__ macro,
 *                as per an assertion failure.
 *
 * \param pErrfile Source file where the error occurred. Use the \c __FILE__
 *                 macro, as per an assertion failure.
 *
 * \param fSysErr Indicates whether the error code corresponds to an error
 * specified by the operating system. If this is \c TRUE, the implementation
 * will use platform-specific code to transform \c errcode into a
 * human-readable string. (This can be done with \c FormatMessage() on
 * Windows, and \c strerror() on Unix-based platforms, for example).
 * If this argument is \c FALSE, only the numeric error code will be
 * reported.
 */
void PKRecordSystemError(int32 errcode,
                         int32 errline,
                         const char *pErrfile,
                         int32 fSysErr);


/* Probe logging */

/** @brief Typedef for the write log function.
 *
 * A callback function can be provided to output the log data rather than
 * using the default file output functions.
 */
typedef int (RIPCALL SwWriteProbeLogFn)(char *pBuffer,
                                        size_t nLength) ;

/**
 * @brief Initialise probe logging.
 *
 * \param ppTraceNames Override default probe names list. NULL to use default list.
 * \param nTraceNames Items in list specified above. Ignored if list is NULL.
 * \param pabTraceEnabled
 * \param ppTraceTypeNames Override default probe names list. NULL to use default list.
 * \param nTraceTypeNames Items in list specified above. Ignored if list is NULL.
 * \param pGroupDetails Override default group names list. NULL to use default list.
 * \param nGroupDetails Items in list specified above. Ignored if list is NULL.
 * \param pszArg Command line argument for usage display only.
 * \param pszLog Filename of output log.
 * \param pfnWriteLog
 */
void SwLeProbeLogInit(const char **ppTraceNames, int nTraceNames,
                      int *pabTraceEnabled,
                      const char **ppTraceTypeNames, int nTraceTypeNames,
                      const sw_tracegroup_t *pGroupDetails, int nGroupDetails,
                      char *pszArg, char *pszLog, SwWriteProbeLogFn *pfnWriteLog);

/**
 * @brief Flush probe log.
 *
 */
void SwLeProbeLogFlush(void);

/**
 * @brief Finish probe logging.
 *
 */
void SwLeProbeLogFinish(void);

/**
 * @brief Set the probe handler function for the specified arg.
 *
 * \param handler Function pointer to probe handler function.
 *
 * \param arg Group name or probe name.
 *
 * \retval TRUE  The group or probe name was enabled.
 * \retval FALSE The group or probe name does not exist.
 */
HqBool SwLeProbeOption(SwTraceHandlerFn *handler, const char *arg);


/**
 * @brief Set the profile handler function for the specified arg.
 *
 * \param handler Function pointer to profiler handler function.
 *
 * \param arg Group name or probe name.
 *
 * \retval TRUE  The group or probe name was enabled.
 * \retval FALSE The group or probe name does not exist.
 */
HqBool SwLeProfileOption(SwTraceHandlerFn *handler, const char *arg);

/**
 * @brief Display the default probe usage information.
 *
 */
void SwLeProbeOptionUsage(void);

/**
 * @brief Set a probe callback function to capture fine-grained tracing
 * information.
 *
 * \param handler Trace handler function, called by the RIP when enabled
 *                events occur.
 *
 * This function will only have an effect on the core RIP if called before \c
 * SwLeMemInit() and \c SwLeStart(). A pointer to the trace handler is
 * retained by the skinkit for use by the \c SwLeProbe() function.
 */
void SwLeSetTraceHandler(SwTraceHandlerFn *handler);

/**
 * @brief Enable a probe to capture fine-grained tracing information.
 *
 * \param trace  The trace name identifier. Individual traces should come
 *               from the SW_TRACE_* identifiers defined in swtrace.h.
 * \param enable If true, enable the probe(s). If false, disable them.
 *
 * The trace handler function must have been set using SwLeSetTraceHandler()
 * before calling this function. This function is a trampoline to call the
 * trace handler, which manages its own enable/disable state.
 */
void SwLeTraceEnable(int32 trace, int32 enable);

/**
 * @brief Call the skinkit trace handler, if installed.
 *
 * \param id      A trace ID. This may be a core, skinkit or skin ID. It will
 *                be silently ignored if it is not a valid trace ID.
 * \param type    A trace type. This may be a core, skinkit or skin type. It
 *                will be silently ignored if it is not a valid trace type.
 * \param designator A data value. The interpretation of the data value is
 *                dependent on the trace id. It is usually used for correlating
 *                objects, or supplying a value for an event.
 */
void RIPFASTCALL SwLeProbe(int id, int type, intptr_t designator) ;
/**
 * @brief Extended trace names for skinkit.
 *
 * These are defined using a macro expansion, so the names can be re-used
 * for other purposes (stringification, usage strings) without having
 * maintainers having to modify every skin which uses the trace facility.
 */
#define SKINKIT_TRACENAMES(macro_) \
  /* Skinkit extensions to core trace names. */ \
  macro_(PROBE) /* Self monitoring for log trace handler */ \
  macro_(KCALLRASTERCALLBACK) /* In KCallRasterCallback callback */ \
  macro_(KCALLRASTERREQUIREMENTSCALLBACK) /* In KCallRasterRequirements callback */ \
  macro_(KCALLRASTERDESTINATIONCALLBACK) /* In KCallRasterDestination callback */ \
  macro_(KCALLPARAMCALLBACK) /* In KCallParamCallback callback */ \
  macro_(STOREBANDINCACHE)   /* In storeBandInCache function */ \
  macro_(TIMELINE)           /* Unspecified timeline operations. */ \
  /* We only distinguish timelines which may are not covered adequately by \
     other probes, timelines which may be extended by children, and/or have \
     progress that is useful to capture. */   \
  macro_(FILE_PROGRESS) /* Core job file progress timeline */ \
  macro_(JOB_STREAM_TL) /* Core job stream timeline */ \
  macro_(JOB_TL)        /* Core job running timeline */ \
  macro_(INTERPRET_PAGE_TL) /* Core interpreting page timeline */ \
  macro_(RENDER_PAGE_TL) /* Core rendering page timeline */ \
  macro_(RR_SCANNING_TL) /* Core retained raster scan timeline */ \
  macro_(TRAP_PREPARATION_TL) /* Core trapping preparation timeline */ \
  macro_(TRAP_GENERATION_TL) /* Core trapping generation timeline */ \
  macro_(PGB_TL)        /* PGB progress timeline */ \

/**
 * @brief Enumeration of trace names for skinkit.
 */
enum {
  SKINKIT_TRACE_BASE = CORE_TRACE_N, /** Base value for skin trace names.
                                         This MUST be the first enum
                                         value. */
  SKINKIT_TRACENAMES(SW_TRACENAME_ENUM)
  SKINKIT_TRACE_N      /**< Starting point for skintest/OIL trace identifiers.
                          This MUST be the last enum value. */
} ;

/**
 * @brief Extended trace types for skinkit.
 *
 * These are defined using a macro expansion, so the names can be re-used
 * for other purposes (stringification, usage strings) without having
 * maintainers having to modify every skin which uses the trace facility.
 * The timeline types have special treatment in the skin so that the timeline
 * reference can be used as the designator.
 */
#define SKINKIT_TRACETYPES(macro_) \
  macro_(TITLE)         /* Timeline re-title. */ \
  macro_(PROGRESS)      /* Timeline progress. */ \
  macro_(EXTEND)        /* Timeline extend. */ \
  macro_(ENDING)        /* Timeline notified of end. */ \
  macro_(ABORTING)      /* Timeline notified of abort. */ \

/**
 * @brief Enumeration of trace types for skinkit.
 */
enum {
  SKINKIT_TRACETYPE_BASE = CORE_TRACETYPE_N, /** Base value for skinkit trace
                                                 types. This MUST be the
                                                 first enum value. */
  SKINKIT_TRACETYPES(SW_TRACETYPE_ENUM)
  SKINKIT_TRACETYPE_N      /**< Starting point for skintest/OIL trace type
                              identifiers. This MUST be the last enum
                              value. */
} ;

#ifdef CHECKSUM
/*
 * Set to \c TRUE if page checksums should be printed, and \c FALSE
 * otherwise.
 */
extern HqBool fPrintChecksums;
#endif

/** \} */  /* end Doxygen grouping */

#endif

