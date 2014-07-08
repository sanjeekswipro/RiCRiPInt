/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:xpsdev.h(EBDSDK_P.1) $
 */

#ifndef __XPSDEV_H__
#define __XPSDEV_H__

#include "std.h"
#include "ripcall.h"
#include "streams.h" /* XPSPackageDescription, XPSPackageStreamManager */

/**
 * \file
 * \ingroup skinkit
 *
 * \brief Functions for interfacing the skinkit XPS input device with the
 * client stream implementations.
 *
 * The XPS device (or "xpsdev") is an input-only skin device, which permits
 * the RIP to interpret XPS packages outside of ZIP containers. This can
 * be a very useful integration pathway when package parts are presented
 * separately, without a ZIP file. Microsoft's \c IXpsDocumentProvider
 * interface is a prime example of this situation.
 *
 * Xpsdev is aimed at supporting the two convenience functions
 * \c RunXPSManagedPackage() and \c StartXPSManagedPackage(). However, if
 * neither of these functions suits your needs, you can consider working
 * with xpsdev directly, using the documentation provided in this file.
 *
 * Xpsdev is a little unusual, in that it requires a mixture of C code
 * and PostScript code to be used. However, the skin offers the
 * \c SwLePs() function as a simple means of running PostScript code
 * from C, so this mixture needn't be a major concern.
 *
 * To use xpsdev, you first need to provide two things: a package
 * description, and a stream manager. The package description tells the
 * device how the package is structured: how many Fixed Documents it
 * contains, and how many Fixed Pages are in each document. It also describes
 * the URIs and content types of resources used by each page. The
 * package description is a nested C data structure
 * (see XPSPackageDescription in \ref streams.h). The package description
 * does \e not include any of the actual content: there is no XML page
 * markup, nor any font or image data. Provision of content is the
 * responsibility of the stream manager, which is implemented as a
 * set of C functions (see XPSPackageStreamManager in \ref streams.h).
 * The stream manager provides the functionality for reading data from
 * named parts. For example, if the package description says that the
 * package has a single document with five pages, the stream manager
 * must be prepared to open a file such as
 * "/Documents/1/Pages/3.fpage", from which the XML markup for page 3 can
 * be read on demand. This extends to font, image, print ticket and all
 * other resources, as well as page markup. The package description and
 * the stream manager combine to provide enough structure and function for the
 * RIP to read the package. Xpsdev simply pulls these strands together,
 * and presents them to the RIP using the only I/O interface that the
 * RIP understands: the device interface.
 *
 * Xpsdev supports streamed packages. Sometimes, the complete contents
 * of an XPS package are not known in advance, but it is still desirable
 * to start rendering the parts that are available, while waiting for
 * the rest. (In a print driver environment, for example, it is usually
 * undesirable to block execution until the entire job is available.)
 * Xpsdev offers an interface to add additional pages and documents while
 * existing content is being processed by the RIP.
 *
 * The stages involved in using xpsdev are as follows:
 *
 * - Form the package description, as described above.
 *
 * - Implement the stream manager, as described above.
 *
 * - Call the \c xpsdevSetPackage() function, passing pointers to the
 *   package description and stream manager. If the package is being
 *   streamed, pass \c FALSE as the last argument, indicating that the
 *   package is not yet complete.
 *
 * - Run PostScript code to mount an instance of xpsdev, for example:-
 *
  \verbatim
       (%XPSPackage%) dup devmount pop
       <<
         /DeviceType 16#ffff00c
         /Password 0
         /Enable true
       >> setdevparams
  \endverbatim
 *
 * - Run PostScript code to interpret and render the package, for
 *   example:-
 *
  \verbatim
      (%XPSPackage%/_rels/.rels) (r) file << >> xmlexec
  \endverbatim
 *
 * - If the package is being streamed, spawn a background thread to deliver
 *   the remaining document parts using the functions
 *   \c xpsdevAddFixedDocument() and \c xpsdevAddFixedPage(). When all parts
 *   have been delivered, call \c xpsdevSetPackageComplete(). When the RIP
 *   reaches the apparent end of a streamed package, it will block until
 *   more parts become available. It is vital to call
 *   \c xpsdevSetPackageComplete() to tell the RIP that there will be no
 *   more parts, otherwise the xmlexec operator will stall indefinitely.
 *
 * - Wait for the RIP to finish processing the package.
 *
 * - Run PostScript code to dismount the xpsdev device instance. (Failure
 *   to do this can incur substantial resource leaks). For example:-
 *
  \verbatim
     mark (%XPSPackage%) devstatus
     {
       cleartomark
       (%XPSPackage%) devdismount
     }
     {
       pop
     } ifelse
  \endverbatim
 *
 * - Call \c xpsdevClearPackage().
 */

#ifdef __cplusplus
extern "C" {
#endif


/**
 * \brief Establish the package description and stream manager that should
 * be adopted in the device state when the \e next instance of
 * xpsdev is mounted.
 *
 * <p>Xpsdev is not a "pure" PostScript device. The context required to
 * set up an instance of xpsdev goes beyond what is possible using
 * simple device parameters. To mount an xpsdev instance successfully,
 * you must call this C function <em>first</em>, and then execute the
 * sequence of PostScript code to mount a new instance of the device.
 * The pointers to the <code>XPSPackageDescription</code> and the
 * <code>XPSPackageStreamManager</code> are then copied into the device
 * state.
 *
 * <p>Between the call to <code>xpsdevSetPackage()</code> and the call to
 * execute the PostScript code that will mount the device, the two pointers
 * need to be stored in global variables. It is possible to mount multiple
 * instances of xpsdev, but you must take care to mount each instance
 * separately, each time you call <code>xpsdevSetPackage()</code>. If you make
 * a second call to <code>xpsdevSetPackage()</code> before mounting the
 * first instance of the device, the pointers from the first call will be
 * overwritten, causing unexpected behaviour.
 *
 * When the next xpsdev device is mounted by PostScript code, the
 * supplied \c XPSPackageDescription will be converted into a "virtual"
 * XPS package, which the RIP can actually interpret and render. Like
 * all devices, the xpsdev instance will have a name, which can be used
 * as the root of the package. So, for example, if the device is called
 * "MyVirtualXPS", the package can be rendered using the following
 * PostScript control sequence:-
 *
   \verbatim
       (%MyVirtualXPS%/_rels/.rels) (r) file << >> xmlexec
   \endverbatim
 *
 * Once the device has been mounted, and even while the RIP is processing
 * the package, it is possible to add further documents and pages
 * dynamically. See \c xpsdevAddFixedDocument() and
 * \c xpsdevAddFixedPage(). When there are no further package parts
 * to be added, call \c xpsdevSetPackageComplete(), which will notify
 * the RIP that it should not wait for additional parts. Failure to call
 * this function might cause a deadlock in the interpreter. If the package
 * is already complete when supplied to \c xpsdevSetPackage(), pass
 * \c TRUE as the \c fComplete argument.
 *
 * \param pPD A pointer to the package description. A copy of this pointer
 * will be stored in the device state when the device is mounted. Note
 * that only the pointer value is copied. There will be no deep copy made
 * of the underlying structures. The caller is responsible for managing
 * the memory, and <em>must not</em> free the memory until the device
 * is dismounted.
 *
 * \param pPSM A pointer to the package stream manager. A copy of this
 * pointer will be stored in the device state when the device is mounted.
 * This memory is managed by the caller, and it <em>most not</em> be freed
 * until the device is dismounted.
 *
 * \param fComplete A flag indicating whether the package is fully-formed.
 * If this is \c TRUE, then the XPS package has all of its parts available.
 * If \c FALSE, then additional parts (such as pages and documents) can
 * be added to the package later with \c xpsdevAddFixedDocument() and
 * \c xpsdevAddFixedPage().
 */
extern void xpsdevSetPackage
  ( XPSPackageDescription *pPD, XPSPackageStreamManager *pPSM, uint32 fComplete );

/**
 * \brief Add a new XPS Fixed Document to an unfinished package.
 *
 * This function cannot be called until a named instance of xpsdev has
 * been mounted from PostScript.
 *
 * This function can be used to extend the contents of a package while
 * it is being interpreted. It is intended for use in "streaming consumption"
 * scenarios, where the complete content of the package is not known in
 * advance. It can only be called when the original package was
 * specified as \e incomplete, which means that \c FALSE must have
 * been passed as the final argument to \c xpsdevSetPackage().
 *
 * \param pszDevice Nul-terminated name of the xpsdev instance that was
 * mounted from PostScript. This name should \e not include any
 * delimiting percent characters. A device of this name must have been
 * mounted, otherwise the function will fail.
 *
 * \param pDocument Pointer to a description of the document and its pages.
 * It is permitted for the document to be empty (meaning that its
 * \c pPageDescriptions field is \c NULL, and its \c nPages field is
 * zero). This would be typical when adding a document before adding any
 * of its pages. If the document is empty, at least one page must be
 * added to it by a subsequent call to \c xpsdevAddFixedPage(), otherwise
 * the package will not conform to the XPS specification. (It is not legal
 * for a Fixed Document to have zero Fixed Pages). If the document is
 * non-empty, this function will add all of its pages automatically, but it
 * remains possible to add further pages with explicit calls to
 * \c xpsdevAddFixedPage(). Note that if the \c nPages field of
 * the document description is greater than zero, the
 * \c pPageDescriptions field <em>must not</em> be \c NULL.
 *
 * \return TRUE upon success; FALSE upon failure. If the function fails,
 * the named device will have its last error code set appropriately, as long
 * as the device is mounted.
 */
extern int32 xpsdevAddFixedDocument
  ( uint8 *pszDevice, XPSDocumentDescription *pDocument );

/**
 * \brief Add a new XPS Fixed Page to an unfinished package.
 *
 * This function cannot be called until a named instance of xpsdev has
 * been mounted from PostScript.
 *
 * This function can be used to extend the contents of a package while
 * it is being interpreted. It is intended for use in "streaming consumption"
 * scenarios, where the complete content of the package is not known in
 * advance. It can only be called when the original package was
 * specified as \e incomplete, which means that \c FALSE must have
 * been passed as the final argument to \c xpsdevSetPackage().
 *
 * \param pszDevice Nul-terminated name of the xpsdev instance that was
 * mounted from PostScript. This name should \e not include any
 * delimiting percent characters. A device of this name must have been
 * mounted, otherwise the function will fail.
 *
 * \param pPage Pointer to a description of the page and its resources.
 * All resources used by the page must be described. There is no interface
 * for adding additional resource usage later.
 *
 * \return TRUE upon success; FALSE upon failure. If the function fails,
 * the named device will have its last error code set appropriately, as long
 * as the device is mounted.
 */
extern int32 xpsdevAddFixedPage
  ( uint8 *pszDevice, XPSPageDescription *pPage );

/**
 * \brief Signal that an unfinished package is now complete, and that no
 * further pages or documents will be added.
 *
 * This function cannot be called until a named instance of xpsdev has
 * been mounted from PostScript.
 *
 * This function must be called exactly once if the package was originally
 * specified as being incomplete. Failure to do so may cause a deadlock
 * in the RIP, since it will stall indefinitely, waiting for additional
 * parts.
 *
 * After calling this function, it is no longer valid to call
 * \c xpsdevAddFixedDocument() or \c xpsdevAddFixedPage() for the
 * specified device. As soon as the RIP finishes processing the current
 * contents of the XPS package, the job will terminate.
 *
 * \param pszDevice Nul-terminated name of the xpsdev instance that was
 * mounted from PostScript. This name should \e not include any
 * delimiting percent characters. A device of this name must have been
 * mounted, otherwise the function will fail.
 *
 * \return TRUE upon success; FALSE upon failure. If the function fails,
 * the named device will have its last error code set appropriately, as long
 * as the device is mounted.
 */
extern int32 xpsdevSetPackageComplete( uint8 *pszDevice );

/**
 * \brief Disassociate the xpsdev device type from any notion of the
 * "current" package.
 *
 * <p>Use this function to guard against accidentally mounting an instance
 * of xpsdev with the wrong package management context. If you attempt to
 * mount an xpsdev instance when there is no package context, a
 * PostScript error will occur.
 *
 * Note that this function does not actually \e destroy the current package.
 * This only happens when the xpsdev instance is dismounted.
 */
extern void xpsdevClearPackage();

#ifdef __cplusplus
}
#endif

#endif

