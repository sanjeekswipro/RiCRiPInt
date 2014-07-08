/* $HopeName: SWrefiface!export:refapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 */

#ifndef __REFAPI_H__
#define __REFAPI_H__

#include "streams.h"

/**
 * \file
 * \ingroup refimpl
 * \brief Defines the Global Graphics XPS Reference RIP interface.
 */

/**
 * \page refapipage Global Graphics XPS Reference RIP Interface
 *
 * \section intro_sec Introduction
 *
 * This documentation covers the Reference RIP API, which presents the
 * Harlequin RIP as a simple XPS job processor.  The documentation is
 * generated directly from comments in the source code of the API.
 *
 * \section about_api About the Reference RIP API
 *
 * <p>This API can be used whenever XPS processing is required through
 * a straightforward interface. To keep the interface simple, some
 * restrictions are placed upon the RIP's capabilities. Raster output
 * is limited to composite, contone RGB, or separated, halftone
 * CMYK. A limited range of fixed resolutions is supported. There are
 * no further configuration features supported. Input file formats
 * other than XPS are not supported.
 *
 * <p>XPS input can be consumed from files on disk, or through a
 * seekable stream implementation, in which case the caller must
 * provide a few functions to implement the streaming operations.
 * Output can be produced as TIFF files on disk, or as an XPS package
 * on disk. It is also possible to create output as an XPS package
 * that is written to an arbitrary output stream, in which case,
 * again, the caller must provide the stream implementation. When the
 * RIP is configured to produce XPS output, this output contains
 * rasterized pages, which are present within the package as TIFF
 * images.
 *
 * \section vista_integration Integration with the XPSDrv Filter Pipeline
 *
 * <p>A key application of the Reference API is to integrate the
 * Harlequin RIP with the XPSDrv Filter Pipeline architecture in
 * Windows Vista. In this case, the input and output streams can be
 * implemented directly in terms of <code>IPrintReadStream</code> and
 * <code>IPrintWriteStream</code>, as provided by the Inter-Filter
 * Communicator. However, other implementations are possible, and the
 * Reference RIP may be embedded in any application.
 *
 * <p>The Reference API provides access to a single instance of the
 * Harlequin RIP, which will be loaded into the hosting process as a
 * DLL. The stages involved in using the RIP are as follows: providing
 * the location of the "SW" folder, initializing the RIP, configuring
 * the RIP, submitting a job, and shutting down the RIP.
 *
 * \subsection sw_loc Providing the Location of the SW Folder
 *
 * <p>It is essential to call \c SetSWDir() <em>first</em>, before any
 * attempt is made to boot the RIP. The only time when this is not
 * necessary is when the SW folder is known to be in the same
 * directory as the hosting process's executable file. In particular,
 * this is certainly <em>not</em> the case in the context of an XPSDrv
 * pipeline filter, so you should always call \c SetSWDir()
 * in this context.
 *
 * \subsection init_rip Initializing the RIP
 *
 * Call \c StartXPSRIP() to boot the RIP. This function will return
 * when the RIP is fully initialized and ready to receive jobs. The
 * RIP is actually started on a separate thread, where it remains
 * running until terminated by a call to \c StopXPSRIP().
 *
 * \subsection config_rip Configuring the RIP
 *
 * A small number of configuration settings are available, and these will
 * persist from one job to the next. These settings can be changed as desired
 * while the RIP is running, and each change will take effect from the next
 * job. See <code>SetOutputMethod()</code>, <code>SetRasterStyle()</code>
 * and <code>SetDefaultResolution()</code> for further details.
 *
 * \subsection submit_job Submitting a job
 *
 * Use \c RunXPSJob() to submit a job to the RIP. This
 * method does not return until the RIP has fully processed (or
 * rejected) the job, at which point it is possible to submit the next
 * job, and so on. Any number of jobs may be submitted during the
 * lifetime of the RIP, but jobs may only be processed in sequence.
 * Concurrent use of this API is \e not supported. Where concurrent
 * processing is required, it is necessary to manage multiple RIP instances
 * in separate processes. Locking mechanisms are not implemented by
 * this API. Locking should be implemented by the caller, if there is
 * any danger of unsafe concurrent use.
 *
 * As well as \c RunXPSJob(), there are some additional functions for
 * executing XPS in specialized environments. The \c RunXPSManagedPackage()
 * function, for example, can be used to render XPS content outside of
 * the normal ZIP package framework.
 *
 * \subsection shut_down Shutting down the RIP
 *
 * Shutdown is achieved by a single call to
 * \c StopXPSRIP(). This will terminate the RIP thread, and free
 * all of the memory and other resources that were in use. It is possible
 * to start the RIP again later with \c StartXPSRIP(). This cycle can be
 * repeated any number of times within the lifetime of the hosting
 * application.
 */
#ifdef __cplusplus
extern "C"
{
#endif

/**
 * \defgroup refimpl XPS RIP Reference API, and supporting code
 * \ingroup leskin_group
 * \{
 */


/**
 * \brief Enumerates available output production methods.
 */
enum
{
  /**
   * \brief Produce output as TIFF files, with a separate file being produced
   * for page in the case of RGB output, or for each separation of each page,
   * in the case of CMYK output.
   */
  RasterOut_TIFFToDisk,

  /**
   * \brief Produce output as a single XPS ZIP package on disk.
   */
  RasterOut_XPSToDisk,

  /**
   * \brief Produce output as an XPS output stream, handed onwards to
   * an HqnWriteStream object.
   */
  RasterOut_XPSToStream,

  /**
   * \brief Produce output as a raw raster data stream, handed onwards
   * to an HqnWriteStream object.
   */
  RasterOut_RawToStream,
};

/**
 * \brief Enumerates available raster formats.
 */
enum
{
  /** \brief Separated, screened CMYK output. */
  RasterStyle_Halftone_CMYK_Separations,
  /** \brief Composite, contone RGB output, pixel interleaved. */
  RasterStyle_Contone_RGB_Pixel
};

/**
 * \brief Enumerates Print Ticket support.
 */
enum
{
  /**
   *  \brief Turn off XPS Print Ticket processing.
   */
  PrintTicketSupport_Disable,

  /**
   *  \brief Turn on XPS Print Ticket processing.
   */
  PrintTicketSupport_Enable,

  /**
   *  \brief Turn on Print Ticket processing and enable feature emulation.
   *
   *  It is not possible to handle certain XPS Print Ticket options in
   *  a meaningful way.  <code>PageMediaType</code>, for example, represents
   *  a physical property not applicable to an output raster.  Enabling
   *  Print Ticket emulation will overlay a textual representation of this
   *  information onto the output raster.
   */
  PrintTicketSupport_EnableWithEmulation
};

/**
 * \brief  Enumerates job submission flags (which can be OR'd together).
 */
enum
{
  /**
   * \brief  Submit jobs using XPS streaming functionality.
   */
  JobOption_None = 0x00,

  /**
   * \brief  Submit jobs using XPS streaming functionality.
   */
  JobOption_StreamedInput = 0x01
};

/**
 * \brief Callback that receives monitor text messages from the RIP.
 *
 * <p>An implementation of this callback should be passed into the
 * <code>StartXPSRIP()</code> function. It will be used during the entire
 * lifetime of the RIP within the host process.
 *
 * \param cbBuffer The length of the character data within the buffer.
 *
 * \param pBuffer The raw text data from the RIP.
 */
typedef void (RIPCALL *MonitorFn) ( uint32 cbBuffer, uint8 *pBuffer );


/**
 * \brief Resets the state of the RIP and skin support layers, allowing the
 * RIP to be booted multiple times in the same process.
 *
 * There is no need to call this function unless you are expecting to
 * run multiple RIP sessions within the lifetime of a single process. In
 * other words, only call this function if you are calling
 * \c StartXPSRIP() and \c StopXPSRIP() more than once, where failure
 * to call it would cause erratic behaviour.
 *
 * When this function is called, it should be called as the \e first
 * action in the reboot sequence. It causes a complete reset of the RIP's
 * global state. After calling it, you will need to re-apply any
 * initializations, such as calling \c SetSWDir(), before re-booting
 * the RIP with \c StartXPSRIP(). Put simply, the effect of calling this
 * function is very similar to the effect of killing and re-starting
 * the hosting process.
 *
 * \param pContext This argument is currently unused. Always pass
 * \c NULL.
 *
 * \return If the function succeeds, the return value is \c TRUE, otherwise
 * the return value is \c FALSE.
 */
extern int32 RIPCALL InitializeRIPRuntime( void *pContext );

/** \brief Function type for \c InitializeRIPRuntime(). */
typedef int32 (RIPCALL *InitializeRIPRuntimeFn) ( void * pContext );

/**
 * \brief Set the absolute path to the RIP's SW folder.
 *
 * <p>Calls to this function will only be effective <em>before</em> the RIP is
 * booted with <code>StartXPSRIP()</code>.
 *
 * \param pszPath NUL-terminated, platform-specific absolute path. The path,
 * inclusive of its NUL terminator, must not exceed <code>LONGESTFILENAME</code>
 * characters in length.
 *
 * \return Returns <code>TRUE</code> if the function succeeds, otherwise
 * <code>FALSE</code>.
 */
extern int32 RIPCALL SetSWDir( char *pszPath );

/** \brief Function type for <code>SetSWDir()</code>. */
typedef int32 (RIPCALL *SetSWDirFn) ( char *pszPath );


/**
 * \brief Boot the XPS RIP implementation.
 *
 * <p>This method loads the RIP, ensures any custom Core Modules
 * are loaded successfully, and waits until the RIP becomes ready to
 * receive XPS jobs.
 *
 * <p>Always ensure that you have called \c SetSWDir() with a valid
 * path, before calling this method.
 *
 * \param RIP_maxAddressSpaceInBytes Returns maximum address space that
 * the RIP will be allowed to use.
 *
 * \param RIP_workingSizeInBytes Maximum amount of memory that
 * the RIP should be allowed to use.
 *
 * \param RIP_emergencySizeInBytes Emergency memory.
 *
 * \param pfnMonitor Pointer to a function that will receive standard output
 * and logging messages from the RIP. This parameter is permitted to
 * be <code>NULL</code>, in which case all monitor messages will be discarded.
 * Note that the RIP runs on a separate thread, and \e all calls to the monitor
 * callback will actually be executed from that thread.
 *
 * \param pszFilenameFormat Pointer to a NUL-terminated string that indicates
 * how output filename(s) should be formed. This string memory should not be
 * freed until the RIP is shutdown. (In normal circumstances, the supplied
 * argument would be a literal string). This argument can be <code>NULL</code>,
 * in which case the RIP will adopt its default conventions for naming
 * output files.
 *
 * \return If the function succeeds, the return value is <code>TRUE</code>.
 * If the function fails, the return value is <code>FALSE</code>.
 */
extern int32 RIPCALL StartXPSRIP
  (
    size_t *RIP_maxAddressSpaceInBytes,
    size_t RIP_workingSizeInBytes,
    size_t RIP_emergencySizeInBytes,
    MonitorFn pfnMonitor,
    char *pszFilenameFormat
  );

/** \brief Function type for <code>StartXPSRIP()</code>. */
typedef int32 (RIPCALL *StartXPSRIPFn)
  (
    size_t *RIP_maxAddressSpaceInBytes,
    size_t RIP_workingSizeInBytes,
    size_t RIP_emergencySizeInBytes,
    MonitorFn pfnMonitor,
    char *pszFilenameFormat
  );


/**
 * \brief Select the required mode of output production.
 *
 * <p>The reference RIP can be configured to produce output as TIFF files
 * on disk (with one file per page), or as a single XPS package. For
 * the case of an XPS package, there is the option of producing this on
 * disk, or transmitting it over an output stream. The transmission of
 * XPS through an output stream is of particular interest in the context
 * of XPSDrv Filter Pipeline integration, where the output stream implementation
 * can be wired up to <code>IPrintWriteStream</code> in the
 * inter-filter communicator. This is how XPS output can be delivered to
 * the next filter in the pipeline.
 *
 * <p>When configuring for <code>RasterOut_XPSToStream</code>, it is necessary
 * to pass a non-NULL <code>HqnWriteStream</code> pointer to
 * <code>RunXPSJob()</code>.
 *
 * <p>By default, if this method is not called, TIFF output will be produced
 * on disk.
 *
 * <p>When producing disk output, the files are produced in a directory
 * specified with each individual job submission. See the documentation for
 * <code>RunXPSJob()</code> for more details.
 *
 * \param rasterOut One of <code>RasterOut_TIFFToDisk</code>,
 * <code>RasterOut_XPSToDisk</code> or <code>RasterOut_XPSToStream</code>.
 *
 * \return Returns <code>TRUE</code> if the argument is valid, otherwise
 * <code>FALSE</code>. This method will only return <code>FALSE</code> for
 * an invalid argument. There is no other reason for it to fail.
 */
extern int32 RIPCALL SetOutputMethod( uint32 rasterOut );

/** \brief Function type for <code>SetOutputMethod()</code>. */
typedef int32 (RIPCALL *SetOutputMethodFn) ( uint32 rasterOut );


/**
 * \brief Select the required raster output style.
 *
 * <p>The reference RIP supports two output raster styles:
 * Separated CMYK halftone (1-bit-per-pixel),
 * and Composite RGB contone (8-bits-per-pixel, pixel-interleaved).
 *
 * <p>By default, if this method is not called, separated CMYK output
 * is produced.
 *
 * \param rasterStyle One of
 * <code>RasterStyle_Halftone_CMYK_Separations</code> or
 * <code>RasterStyle_Contone_RGB_Pixel</code>.
 *
 * \return Returns <code>TRUE</code> if the argument is valid, otherwise
 * <code>FALSE</code>. This method will only return <code>FALSE</code> for
 * an invalid argument. There is no other reason for it to fail.
 */
extern int32 RIPCALL SetRasterStyle( uint32 rasterStyle );

/** \brief Function type for <code>SetRasterStyle()</code>. */
typedef int32 (RIPCALL *SetRasterStyleFn) ( uint32 rasterStyle );


/**
 * \brief Set the default resolution (in DPI) for the output raster.
 *
 * <p>This method sets the default resolution for all subsequent XPS jobs.
 * This resolution will be used unless it is overriden by Print Ticket
 * settings within the job itself.
 *
 * <p>The Reference RIP supports only square resolutions (that is, resolutions
 * that are equal on both the X and Y axes). Furthermore, it supports only
 * a single resolution value per page: it is not possible to use different
 * resolutions for different classes of graphical object.
 *
 * <p>Resolution values are limited to 1200dpi for RGB output, and to
 * 2000dpi for CMYK output. The default, before any call to this method,
 * is 100dpi. The minimum value is 72dpi in all cases. A value outside
 * of the valid range will be rejected by this function. Additionally,
 * any print ticket settings that specify resolutions outside of this
 * range will be ignored, and may cause the job to fail.
 *
 * \param resolution The new default resolution, which must be a value between
 * the minimum and maximum permitted values for the currently-configured
 * raster style.
 *
 * \return Returns <code>TRUE</code> if the argument is within the valid
 * range, otherwise returns <code>FALSE</code>.
 */
extern int32 RIPCALL SetDefaultResolution( double resolution );

/** \brief Function type for <code>SetDefaultResolution()</code>. */
typedef int32 (RIPCALL *SetDefaultResolutionFn) ( double resolution );


/**
 * \brief Set Print Ticket support options.
 *
 * \param  printTicketSupport  One of
 * <code>PrintTicketSupport_Disable</code>,
 * <code>PrintTicketSupport_Enable</code> or
 * <code>PrintTicketSupport_EnableWithEmulation</code>
 * \return TRUE on success
 */
extern int32 RIPCALL SetXPSPrintTicketSupport (uint32 printTicketSupport);

/** \brief Function type for <code>SetXPSPrintTicketSupport()</code>. */
typedef int32 (RIPCALL *SetXPSPrintTicketSupportFn) (uint32 printTicketSupport);


/**
 * \brief Synchronously execute an XPS job, consuming it from the given
 * input stream, and producing raster output according to the current
 * configuration settings.
 *
 * <p>This function does not return until the entire job has completed,
 * or aborted due to an error. It is possible for the caller to arrange
 * an early abort of the job by supplying an <dfn>abort callback</dfn>
 * function, which will be called regularly by the RIP.
 *
 * \param pReadStream Pointer to the <code>HqnReadStream</code> object, which
 * will provide the XPS input data. If this argument is <code>NULL</code>,
 * then the XPS package is assumed to exist on disk, as specified by
 * the <code>pszJobFilename</code> parameter.
 *
 * \param pWriteStream Pointer to the <code>HqnWriteStream</code> object,
 * which will handle the XPS output data, if required. This argument may
 * be NULL (and in any case will be ignored) if the RIP is configured to
 * produce TIFF or XPS output on disk. If the RIP is configured to produce
 * XPS output via a stream, this argument <em>must not</em> be <code>NULL</code>.
 *
 * \param pszJobFilename A fully-qualified path and filename for the job,
 * which additionally serves to determine where the output file(s) should be
 * placed. This is a platform-specific pathname, which must be NUL-terminated,
 * and which should not exceed <code>LONGESTFILENAME</code> in length
 * (including the NUL terminator).
 *
 * \param pPTHandler Pointer to a set of functions that provide print
 * ticket merging and validation, plus XPS progress monitoring facilities.
 * This argument is allowed to be NULL if no such facilities have been integrated.
 *
 * \param fDigitalSignatureValid The state of the digital signature applied to the
 * XPS input.  Set to FALSE if a signature is present and invalid.
 *
 * \param uJobFlags A bitmask allowing various job submission options to be set.
 * (e.g. JobOption_StreamedInput)
 *
 * \return If the function succeeds, the return value is <code>TRUE</code>.
 * If the function fails, the return value is <code>FALSE</code>.
 */
extern int32 RIPCALL RunXPSJob
  (
    HqnReadStream   *pReadStream,
    HqnWriteStream  *pWriteStream,
    char            *pszJobFilename,
    PTHandler       *pPTHandler,
    int32           fDigitalSignatureValid,
    uint32          uJobFlags
  );

/** \brief Function type for <code>RunXPSJob()</code>. */
typedef int32 (RIPCALL *RunXPSJobFn)
  (
    HqnReadStream   *pReadStream,
    HqnWriteStream  *pWriteStream,
    char            *pszJobFilename,
    PTHandler       *pPTHandler,
    int32           fDigitalSignatureValid,
    uint32          uJobFlags
  );


/**
 * \brief Identical to RunXPSJob(), except that the XPS input is consumed
 * from an unzipped package store on the local disk, rather than from a ZIP
 * file.
 *
 * <p>See RunXPSJob() for additional details.
 *
 * \param pReadStream Since the XPS package is unpacked on disk, no data is read
 * from this stream when processing the job. For uniformity with RunXPSJob(), it
 * is still permissible to pass a non-NULL stream object to this function. If
 * this argument is non-NULL, the RIP will simply close the stream as its first
 * processing action.
 *
 * \param pWriteStream This argument is handled as per RunXPSJob().
 *
 * \param pszJobFilename This argument is handled as per RunXPSJob(). In particular,
 * this argument does <em>not</em> specify the location of the input package.
 * A separate argument, pszPackageStoreRoot, is used for that purpose.
 *
 * \param pszPackageStoreRoot A fully-qualified path to the root directory of
 * the package, which should not have a trailing directory separator. There should
 * be a file "/_rels/.rels" relative to this directory, which will be used to
 * bootstrap the interpretation.
 *
 * \param pPTHandler Pointer to a set of functions that provide print
 * ticket merging and validation, plus XPS progress monitoring facilities.
 * This argument is allowed to be NULL if no such facilities have been integrated.
 *
 * \param fDigitalSignatureValid The state of the digital signature applied to the
 * XPS input.  Set to FALSE if a signature is present and invalid.
 *
 * \param uJobFlags A bitmask allowing various job submission options to be set.
 * This argument is currently ignored.
 *
 * \return If the function succeeds, the return value is <code>TRUE</code>.
 * If the function fails, the return value is <code>FALSE</code>.
 */
extern int32 RIPCALL RunXPSJobUnpacked
  ( HqnReadStream   *pReadStream,
    HqnWriteStream  *pWriteStream,
    char            *pszJobFilename,
    char            *pszPackageStoreRoot,
    PTHandler       *pPTHandler,
    int32           fDigitalSignatureValid,
    uint32          uJobFlags );

/** \brief Function type for <code>RunXPSJobUnpacked()</code>. */
typedef int32 (RIPCALL *RunXPSJobUnpackedFn)
  ( HqnReadStream   *pReadStream,
    HqnWriteStream  *pWriteStream,
    char            *pszJobFilename,
    char            *pszPackageStoreRoot,
    PTHandler       *pPTHandler,
    int32           fDigitalSignatureValid,
    uint32          uJobFlags );


/**
 * \brief Render an XPS document whose individual package part streams are
 * managed by the caller.
 *
 * <p>The RIP has the ability to execute XPS jobs from <em>virtual</em>
 * packages. This caters for situations where the XPS document is being
 * processed primarily by a component <em>outside</em> of the RIP, but where
 * the RIP is needed to produce raster data for all or part of the
 * document. Rather than create a self-contained ZIP container, the client
 * process can create a dynamic model of the package using
 * the <code>XPSPackageDescription</code> structure. The client can then
 * call this function to process the package. The client is the expected to
 * supply readable streams for the individual package parts on demand.
 * This is acheived through the <code>XPSPackageStreamManager</code>
 * protocol.
 *
 * <p>Like <code>RunXPSJob()</code> and <code>RunXPSJobUnpacked()</code>,
 * this function is synchronous. It does not return until the XPS package
 * has been fully processed, and all raster output has been produced. An
 * earlier return can be arranged by implementing the abort callback.
 *
 * <p>This function should only be used when the \e entire document is
 * available, meaning all pages and all of their associated resources.
 * In situations where document parts are becoming available gradually,
 * as might be the case in a print driver pipeline, you should consider
 * using the \c StartXPSManagedPackage(), \c AddXPSDocument(),
 * \c AddXPSPage() and \c EndXPSManagedPackage(). These functions allow
 * you to start processing a document, and then add further content
 * dynamically. This function does not permit the package to be extended
 * once the RIP begins to process it.
 *
 * \param pPackageDescription A description of the package, indicating the
 * number of documents, the number of pages in each document, and information
 * about content types and print tickets. This allows the RIP to model
 * the complete package internally.
 *
 * \param pStreamManager A pointer to the object that the RIP will call
 * in order to open and close individual part streams within the package.
 *
 * \param pWriteStream Pointer to the <code>HqnWriteStream</code> object,
 * which will handle the XPS output data, if required. This argument may
 * be NULL (and in any case will be ignored) if the RIP is configured to
 * produce TIFF or XPS output on disk. If the RIP is configured to produce
 * XPS output via a stream, this argument <em>must not</em> be <code>NULL</code>.
 *
 * \param pszJobFilename A fully-qualified path and filename for the job.
 * Since the package is virtual, this does not denote a physical ZIP file,
 * and the RIP will not expect this file to actually exist on disk. However, it
 * serves to determine where the output file(s) should be
 * placed. This is a platform-specific pathname, which must be NUL-terminated,
 * and which should not exceed <code>LONGESTFILENAME</code> in length
 * (including the NUL terminator).
 *
 * \param pPTHandler Pointer to a set of functions that provide print
 * ticket merging and validation, plus XPS progress monitoring facilities.
 * This argument is allowed to be NULL if no such facilities have been integrated.
 *
 * \param fDigitalSignatureValid The state of the digital signature applied to the
 * XPS input.  Set to FALSE if a signature is present and invalid.
 *
 * \return If the function succeeds, the return value is <code>TRUE</code>.
 * If the function fails, the return value is <code>FALSE</code>.
 */
extern int32 RIPCALL RunXPSManagedPackage
  (
    XPSPackageDescription   *pPackageDescription,
    XPSPackageStreamManager *pStreamManager,
    HqnWriteStream          *pWriteStream,
    char                    *pszJobFilename,
    PTHandler               *pPTHandler,
    int32                    fDigitalSignatureValid
  );

/**
 * \brief Identical to \c RunXPSManagedPackage(), but allows for further content
 * to be added to the package while it is being processed.
 *
 * Used with care, this function can provide improved performance over
 * \c RunXPSManagedPackage(), specifically in situations where package
 * parts are not all available at the same time. \c RunXPSManagedPackage()
 * requires a complete description of all parts in the package, and
 * cannot begin any rendering work until this description is provided.
 * \c StartXPSManagedPackage() is different. This function allows rendering
 * work to proceed immediately on the initial pages of the job, while
 * later pages are added in parallel as they arrive.
 *
 * This function has been introduced to allow the XPS RIP to be
 * integrated more efficiently with pipeline architectures, such as the
 * Microsoft XPSDrv filter pipeline. When a package is consumed, for
 * example, through the \c IXpsDocumentProvider interface, the pages
 * become available sequentially. In cases like this, it is inefficient
 * to wait for all pages before starting to render, since this creates
 * a bottleneck in the pipeline. For jobs with high page counts, the
 * delay in processing can be significant.
 *
 * The behaviour of this function is identical to
 * \c RunXPSManagedPackage() in most respects. It requires an \e initial
 * description of the package (although it need not be complete), and
 * a stream manager that provides access to individual package parts
 * on demand. The function is also \e synchronous. It does not return until
 * the RIP has processed the \e entire package - not only those parts
 * that are available immediately, but also the parts that will become
 * available later. It follows that the calling thread \e cannot be used
 * to deliver the remaining parts, because this thread will remain blocked
 * while the job is in progress. Client code is responsible for spawning
 * a new thread, which can then add parts to the package using the
 * \c AddXPSDocument() and \c AddXPSPage() functions. This new thread
 * \e must also call \c EndXPSManagedPackage() when no further parts
 * are available. This tells the RIP that the package is complete. Having
 * received this signal, the RIP will continue processing the pages that
 * have been delivered so far, and then consider the job to be complete.
 * When the job is complete, \c StartXPSManagedPackage() will return.
 * If the part-feeding thread fails to call \c EndXPSManagedPackage(),
 * the thread that called \c StartXPSManagedPackage() will remain
 * blocked indefinitely.
 *
 * Care must also be taken with the use of \c AddXPSDocument() and
 * \c AddXPSPage(). The part-feeding thread must not call these functions
 * \e until the RIP has started to process the initial parts of the
 * package. The best way of detecting this is to use the
 * \c XPSPackageStreamManager. When the RIP calls the
 * \c OpenResourceStreamFn for the first time, it has started to
 * process the package. At this point, the part-feeding thread can
 * begin making calls to \c AddXPSDocument() and \c AddXPSPage(), looping
 * until all package parts have been exhausted. The part-feeding thread
 * must then call \c EndXPSManagedPackage(), to notify the RIP that it
 * should not continue waiting for more parts.
 *
 * Provided that these guidelines are carefully
 * followed, the RIP can produce prompt raster output for the first page,
 * regardless of the number of pages in the job.
 *
 * \param pPackageDescription A description of the package, indicating the
 * number of documents, the number of pages in each document, and information
 * about content types and print tickets. This allows the RIP to model
 * the package internally. Further pages and documents can be added with
 * \c AddXPSDocument() and \c AddXPSPage().
 *
 * \param pStreamManager A pointer to the object that the RIP will call
 * in order to open and close individual part streams within the package.
 *
 * \param pWriteStream Pointer to the <code>HqnWriteStream</code> object,
 * which will handle the XPS output data, if required. This argument may
 * be NULL (and in any case will be ignored) if the RIP is configured to
 * produce TIFF or XPS output on disk. If the RIP is configured to produce
 * XPS output via a stream, this argument <em>must not</em> be <code>NULL</code>.
 *
 * \param pszJobFilename A fully-qualified path and filename for the job.
 * Since the package is virtual, this does not denote a physical ZIP file,
 * and the RIP will not expect this file to actually exist on disk. However, it
 * serves to determine where the output file(s) should be
 * placed. This is a platform-specific pathname, which must be NUL-terminated,
 * and which should not exceed <code>LONGESTFILENAME</code> in length
 * (including the NUL terminator).
 *
 * \param pPTHandler Pointer to a set of functions that provide print
 * ticket merging and validation, plus XPS progress monitoring facilities.
 * This argument is allowed to be NULL if no such facilities have been integrated.
 *
 * \param fDigitalSignatureValid The state of the digital signature applied to the
 * XPS input.  Set to FALSE if a signature is present and invalid.
 *
 * \return If the function succeeds, the return value is <code>TRUE</code>.
 * If the function fails, the return value is <code>FALSE</code>.
 */
extern int32 RIPCALL StartXPSManagedPackage
  (
    XPSPackageDescription   *pPackageDescription,
    XPSPackageStreamManager *pStreamManager,
    HqnWriteStream          *pWriteStream,
    char                    *pszJobFilename,
    PTHandler               *pPTHandler,
    int32                    fDigitalSignatureValid
  );

/** \brief Function type for \c RunXPSManagedPackage() and
 * \c StartXPSManagedPackage(), which both have the same signature. */
typedef int32 (RIPCALL *RunXPSManagedPackageFn)
  (
    XPSPackageDescription   *pPackageDescription,
    XPSPackageStreamManager *pStreamManager,
    HqnWriteStream          *pWriteStream,
    char                    *pszJobFilename,
    PTHandler               *pPTHandler,
    int32                    fDigitalSignatureValid
  );

/**
 * \brief Add a new Fixed Document to the currently-executing package.
 *
 * This function can only be used in combination with
 * \c StartXPSManagedPackage(). A call to \c StartXPSManagedPackage()
 * \e must be in progress (on a separate thread) whenever
 * \c AddXPSDocument() is called.
 *
 * In the Microsoft XPSDrv filter pipeline, this function should be
 * called each time a new \c IFixedDocument part is obtained from
 * the \c IXpsDocumentProvider object.
 *
 * \param pDocument A description of the document, which may or may not
 * include pages. If the document is empty, pages must be added to it
 * later by calling \c AddXPSPage(). The XPS specification requires that
 * any document must have at least one page. If the document is empty,
 * the \c nPages field of the \c XPSDocumentDescription must be zero.
 * In (only) this case, the \c pPageDescriptions field is permitted to be
 * \c NULL. If the \c nPages field has a non-zero value, the
 * \c pPageDescriptions field <em>must not</em> be \c NULL. Never use
 * the \c nPages field to indicate the number of pages \e expected; only
 * use it to indicate the number of pages actually \e described at
 * the time of the call. When receiving a new \c IFixedDocument object
 * in an XPSDrv filter, this function should be called with an empty
 * document, since the pages of the document will be added later.
 *
 * \return \c TRUE upon success; \c FALSE upon failure.
 */
extern int32 RIPCALL AddXPSDocument( XPSDocumentDescription *pDocument );

/**
 * \brief Function type for \c AddXPSDocument().
 */
typedef int32 (RIPCALL *AddXPSDocumentFn) ( XPSDocumentDescription *pDocument );

/**
 * \brief Add a new Fixed Page to the currently-executing package.
 *
 * This function can only be used in combination with
 * \c StartXPSManagedPackage(). A call to \c StartXPSManagedPackage()
 * \e must be in progress (on a separate thread) whenever
 * \c AddXPSPage() is called.
 *
 * In the Microsoft XPSDrv filter pipeline, this function should be
 * called each time a new \c IFixedPage part is obtained from
 * the \c IXpsDocumentProvider object.
 *
 * \param pPage A description of the page, which must include a content type
 * table for \e all resources used by the page. There is no interface for
 * adding further resource usage later. In the XPSDrv filter pipeline,
 * use the \c IXpsPartIterator interface to get information about the
 * URIs and content types of resources used by the \c IFixedPage.
 *
 * \return \c TRUE upon success; \c FALSE upon failure.
 */
extern int32 RIPCALL AddXPSPage( XPSPageDescription *pPage );

/**
 * \brief Function type for \c AddXPSPage().
 */
typedef int32 (RIPCALL *AddXPSPageFn) ( XPSPageDescription *pPage );


/**
 * \brief Indicate that the the currently-executing package is now complete.
 *
 * This function can only be used in combination with
 * \c StartXPSManagedPackage(). A call to \c StartXPSManagedPackage()
 * \e must be in progress (on a separate thread) whenever
 * \c EndXPSManagedPackage() is called.
 *
 * In the Microsoft XPSDrv filter pipeline, this function should be
 * called as soon as no further parts are returned from the
 * \c IXpsDocumentProvider object.
 *
 * If this function is not called, the thread that is currently running
 * \c StartXPSManagedPackage() will remain blocked indefinitely.
 *
 * \return \c TRUE upon success; \c FALSE upon failure.
 */
extern int32 RIPCALL EndXPSManagedPackage();

/**
 * \brief Function type for \c EndXPSManagedPackage().
 */
typedef int32 (RIPCALL *EndXPSManagedPackageFn) ();


/**
 * \brief Instruct the RIP to use the specified directory for temporary
 * I/O services.
 *
 * It can be highly beneficial to call this function in situations where
 * the SW resources folder has been compiled into memory using the
 * Import Tool, or has been designated as read-only. In these
 * situations, the RIP will default to using RAM space for
 * its temporary data caches. This is normally undesirable if disk
 * storage is available. Calling this function will notify the
 * RIP that a disk storage area can be used for temporary caches.
 * It will switch to using that storage area, instead of consuming
 * any additional memory.
 *
 * When multiple RIP processes are running concurrently, it is the
 * caller's responsibility to designate a directory that will not
 * cause collisions with the other processes. Using a process ID as
 * part of the directory path is a recommended way to ensure
 * this.
 *
 * The supplied directory need not already exist. If it does not, the
 * RIP will create the directory as soon as it is needed. However,
 * the RIP will not delete the directory. It is the caller's responsibility
 * to do this at the appropriate point in the lifecycle of the
 * host application or driver.
 *
 * If this function is called, it should be called just once, after
 * starting the RIP with \c StartXPSRIP(), and \e before running any
 * jobs. Subsequent calls may result in unpredictable behaviour.
 *
 * \param pszPlatformDirectory Absolute path to the desired temporary
 * directory, which should include a trailing separator.
 *
 * \return \c TRUE if the function succeeds, \c FALSE if the
 * function fails.
 */
extern int32 RIPCALL SetTmpDirectory( char *pszPlatformDirectory );

/**
 * \brief Function type for <code>SetTmpDirectory()</code>.
 */
typedef int32 (RIPCALL *SetTmpDirectoryFn) ( char *pszPlatformDirectory );

/**
 * \brief Completely shut down the XPS RIP, and release all memory and
 * other resources that were in use.
 *
 * Where feasible, this function should always be called (and allowed to
 * return) before the hosting application terminates. It can also be used
 * to release resources when the hosting application does not require
 * RIP facilities in the immediate future.
 *
 * After calling this function, \c RunXPSJob() and its related functions
 * become disabled and must not be called. However, it is possible to
 * re-start the RIP with \c StartXPSRIP(), at which point the functions
 * can be used again.
 *
 * Any number of start-up and shut-down cycles are permitted within
 * the lifetime of the hosting application.
 *
 * \return If the function succeeds, the return value is <code>TRUE</code>.
 * If the function fails, the return value is <code>FALSE</code>.
 */
extern int32 RIPCALL StopXPSRIP();

/** \brief Function type for <code>StopXPSRIP()</code>. */
typedef int32 (RIPCALL *StopXPSRIPFn) ();


#ifdef PDFSKIN
/* Small API extension, specific to the skin=regression variant, the only
   variant to define PDFSKIN. */

/**
 * \brief Begin a new PostScript job.
 *
 * This function is available in the regression testing variant of LE.
 * It is a close analog of \c SwLeJobStart(), and has similar semantics.
 * The key difference is that you don't supply a raster callback
 * function. Instead, the default raster handling function in the skintest
 * module is applied, offering selectable output support based on
 * the \c PageBufferType pagedevice key.
 *
 * The first argument is a filename, but use this carefully. It does not
 * directly refer to the input PDL file. No data will be read from this
 * file automatically. It is more like a "virtual" filename, and its
 * primary purpose is to control where the output raster files are
 * located. There may not even be a real input file. For example, you
 * could execute "100 100 rectfill showpage" as a PostScript job. This
 * would create a raster file. The \c pszJobName argument might then
 * be specified as (say) "C:\SomeDir\MyJob.ps", which is an "imaginary"
 * name for that input job. The raster file would then be created as
 * "C:\SomeDir\MyJob-1.tif" (depending on the \c PageBufferType).
 * Without such a virtual filename, the RIP would not be able to create
 * the output file, because no location would have been specified.
 * When there is a real input file (such as a PDF file), normal practice
 * would be to supply its name as \c pszJobName, and then to run some
 * additional PostScript control code to open and process the file with
 * \c pdfexec.
 *
 * Use \c RunPostScriptCode() as per \c SwLePs() to add additional code
 * to the job. Use \c EndPostScriptJob() as per \c SwLeJobEnd() when there
 * is no further code to execute. Both of these functions will return
 * \c FALSE if an unhandled PostScript error has occurred within
 * the context of the current job. However, \c EndPostScriptJob() must
 * always be called to close the job, even if an error has occurred.
 * It will then become possible to submit further jobs.
 *
 * \param pszJobName A real or virtual filename for the job. Use this argument
 * to control the location of output rasters. This file will not be opened
 * automatically. You need to call \c RunPostScriptCode() to execute
 * further PostScript that will open and process the file. The name should
 * be a platform filename, not a PostScript filename. It must be
 * NUL-terminated.
 *
 * \param cbConfigPS The number of bytes of configuration PostScript.
 *
 * \param pConfigPS Pointer to the configuration PostScript buffer.
 *
 * \return \c TRUE if the function succeeds, otherwise \c FALSE. The function
 * will fail if there is an existing PostScript execution context open.
 * Use \c EndPostScriptJob() to terminate any existing context before
 * making further calls to this function.
 */
extern int32 RIPCALL StartPostScriptJob
  ( uint8 *pszJobName,
    uint32 cbConfigPS, uint8 *pConfigPS );

/** \brief Function type for \c StartPostScriptJob(). */
typedef int32 (RIPCALL *StartPostScriptJobFn)
  ( uint8 *pszJobName,
    uint32 cbConfigPS, uint8 *pConfigPS );

/**
 * \brief Execute some PostScript code in the context of the current
 * job.
 *
 * This function is available in the regression testing variant of LE.
 * It is an exact analog of \c SwLePs(). It can be called multiple times
 * between calls to \c StartPostScriptJob() and
 * \c EndPostScriptJob().
 *
 * \param cbPS The number of bytes in the buffer.
 *
 * \param pPS Pointer to the base of the buffer.
 *
 * \return \c TRUE if the function succeeds, otherwise \c FALSE. The function
 * always returns \c FALSE if there has been a PostScript error within the
 * context of the current job.
 */
extern int32 RIPCALL RunPostScriptCode
  ( uint32 cbPS, uint8 *pPS );

/** \brief Function type for \c RunPostScriptCode(). */
typedef int32 (RIPCALL *RunPostScriptCodeFn)
  ( uint32 cbPS, uint8 *pPS );

/**
 * \brief End the currently-executing PostScript job.
 *
 * This function is available in the regression testing variant of LE.
 * It is an exact analog of \c SwLeJobEnd().
 *
 * \return \c TRUE if the function succeeds, otherwise \c FALSE. The function
 * always returns \c FALSE if there has been a PostScript error within the
 * context of the current job, but it will still close the current job
 * context, making it possible to run further jobs.
 */
extern int32 RIPCALL EndPostScriptJob();

/** \brief Function type for \c EndPostScriptJob(). */
typedef int32 (RIPCALL *EndPostScriptJobFn) ();

/* End of regression test API. */
#endif

/**
 * \brief Encapsulates the complete API as a set of function pointers.
 *
 * <p>The <code>GetXPSRIP()</code> function can be used to populate the
 * structure. This is particularly useful when binding to this interface
 * dynamically (such as with <code>GetProcAddress()</code> on Windows).
 * A single call to <code>GetXPSRIP()</code> will provide all of the
 * other entry points needed to use the API.
 */
typedef struct _XPSRIP
{
  SetSWDirFn                  pfSetSWDir;
  StartXPSRIPFn               pfStartXPSRIP;
  SetOutputMethodFn           pfSetOutputMethod;
  SetRasterStyleFn            pfSetRasterStyle;
  SetDefaultResolutionFn      pfSetDefaultResolution;
  SetXPSPrintTicketSupportFn  pfSetXPSPrintTicketSupport;
  RunXPSJobFn                 pfRunXPSJob;
  RunXPSJobUnpackedFn         pfRunXPSJobUnpacked;
  RunXPSManagedPackageFn      pfRunXPSManagedPackage;
  RunXPSManagedPackageFn      pfStartXPSManagedPackage;
  AddXPSDocumentFn            pfAddXPSDocument;
  AddXPSPageFn                pfAddXPSPage;
  EndXPSManagedPackageFn      pfEndXPSManagedPackage;
  SetTmpDirectoryFn           pfSetTmpDirectory;
  StopXPSRIPFn                pfStopXPSRIP;
#ifdef PDFSKIN
  /* API specific to the regression variant. */
  StartPostScriptJobFn        pfStartPostScriptJob;
  RunPostScriptCodeFn         pfRunPostScriptCode;
  EndPostScriptJobFn          pfEndPostScriptJob;
#endif
  InitializeRIPRuntimeFn      pfInitializeRIPRuntime;
} XPSRIP;

/**
 * \brief Returns all of the entry points that a client application needs to
 * use the RIP through this API.
 *
 * <p>When binding to the RIP API dynamically (such as with
 * <code>LoadLibrary()</code> and <code>GetProcAddress()</code> on the Windows
 * platform), it is convenient to call this function as a way of
 * obtaining the entry points for the rest of the API. <code>GetXPSRIP</code>
 * then becomes the only exported symbol that the client code need
 * resolve explicitly.
 *
 * \param pXPSRIP Pointer to a caller-allocated structure. The function will
 * populate each field of the structure.
 */
extern void RIPCALL GetXPSRIP( XPSRIP *pXPSRIP );

/** \brief Function type for <code>GetXPSRIP()</code>. */
typedef void (RIPCALL *GetXPSRIPFn) ( XPSRIP *pXPSRIP );

/** \} */  /* end Doxygen grouping */

#ifdef __cplusplus
}
#endif

#endif /* __REFAPI_H__ */

