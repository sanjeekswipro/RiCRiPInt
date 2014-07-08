/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
/* $HopeName: SWskinkit!export:xpsbuild.h(EBDSDK_P.1) $ */


#ifndef __XPSBUILD_H__
#define __XPSBUILD_H__

/**
 * \file xpsbuild.h
 * \ingroup skinkit
 * \brief Provides an API for building XPS package parts using the in-memory
 *        virtual filesystem (MFS).
 *
 * <p>This interface is primarily aimed at supporting the
 * <dfn>XPS Input Device Type</dfn> (xpsdev), as implemented in xpsdev.c in the
 * skinkit. Independent use of this interface for other purposes is
 * not recommended. Future releases might change this interface in
 * arbitrary ways.
 *
 * <p>The purpose of this interface is to transform an abstract description
 * of an XPS package into a RAM filesystem. Package descriptions are
 * provided by the <code>XPSPackageDescription</code> structure and its
 * various nested elements, as defined in streams.h in the skinkit.
 * The package description gives the number of documents in the package,
 * the number of pages in each document, and the content types for the
 * resources used by each page. (It does not include actual page markup
 * or resource data). This information is used to build a tree of
 * in-memory files, which form the backbone of the XPS container. This
 * includes the Fixed Document Sequence (.fdseq) file for the package,
 * the Fixed Document (.fdoc) file for each document, and all of
 * the relationships (.rels) files. It also includes a
 * single [Content_Types].xml file. The <code>XPSPackageDescription</code>
 * contains enough information for all of this to be created. The
 * in-memory files are built using the Memory File System (MFS) interface,
 * as defined in memfs.h within the skinkit.
 *
 * <p>Once the virtual filesystem is built, it is captured by the
 * xpsdev device state. This package backbone provides a basic fileset
 * that guides the RIP through the interpretation and rendering of the
 * package. Of course, most of the package is effectively "missing" from
 * this fileset. The markup and resource files are not present. When
 * the RIP attempts to open these, xpsdev intercepts the calls, and
 * transforms them into corresponding <code>OpenResourceStream()</code>
 * calls in its <code>XPSPackageStreamManager</code>, which is also part
 * of the device state. As the RIP executes a package from xpsdev, it
 * is transparently using a mixture of in-memory files (for overall
 * structure and relationships) and externally-implemented streams
 * (for actual markup and resource content). This part of the interface
 * just takes care of generating the in-memory parts.
 */

#include "std.h"
#include "file.h"     /* LONGESTFILENAME */
#include "memfs.h"    /* MFSNODE */
#include "streams.h"  /* XPSPackageDescription */


/*
 * ******************************* Data Types *********************************
 */

/**
 * \brief Linked list entry describing the document, page and resource type
 * context for a given URI.
 *
 * <p>This structure is used to help the xpsdev make outgoing
 * calls to the <code>OpenResourceStream()</code> function in
 * <code>XPSPackageStreamManager</code>. The URI scheme used internally
 * by xpsdev need not necessarily match the scheme being used by
 * the external package manager. The only cases where a precise match
 * can be assumed is where the URIs have been explicitly declared by
 * the external manager already, as is the case with resource streams
 * such as fonts and images. For other parts, such as page markups and
 * print tickets, the stream is addressed by its <dfn>context</dfn>,
 * rather than by a URI string. This structure maps the internal part
 * name onto the correct context, allowing xpsdev to pass out the context
 * information instead of the URI itself.
 *
 * <p>For example, suppose the external manager has described an XPS
 * package with a single document of 10 pages. When this package representation
 * is built within the MFS virtual file tree, the URI of the fifth page
 * will be <code>"/documents/1/pages/5.fpage"</code>. When the RIP needs
 * to read the markup for this page, the part will not be addressed
 * using this URI. This because the URI is an internal fabrication within xpsdev,
 * and the external package manager may not understand it. The external manager
 * might employ a completely different scheme, whereby the URI of the
 * markup part does not match <code>"/documents/1/pages/5.fpage"</code>.
 * So, instead of passing this internal URI out to the stream manager,
 * it will simply ask for a resource of type <code>ResourceType_Page_Markup</code>,
 * where <code>(iDocument == 0 && iPage == 4)</code> (rather than 1 and
 * 5, since the indexes are zero-based). The external manager can then
 * transform this logical part address according to its own scheme for
 * finding the markup. The same applies to print tickets. The RIP would
 * address the print ticket for page 5 (if there is one), using
 * <code>ResourceType_Page_PT</code>, with the same values for
 * <code>iDocument</code> and <code>iPage</code>.
 *
 * <p>When an XPS document description is built using MFS, a list of these
 * mappings is produced as a by-product. When a particular part file
 * is opened on xpdev, the list can be scanned for an entry whose
 * URI matches the file name. If a match is found, then this determines
 * the context. If no match is found, then the document and page
 * contexts are assumed to be unchanged, and the resource type is
 * the generic <code>ResourceType_URI</code>.
 */
typedef struct _URIContextMapping
{
  /** \brief NUL-terminated part name relative to the package root, such as
   *  <code>"/documents/1/pages/1.fpage"</code>.
   */
  uint8                      szURI[ LONGESTFILENAME ];

  /** \brief Zero-based index to the document context. For instance, this would
   *   be <code>0</code> where <code>szURI</code> 
   *   is <code>"/documents/1/pages/5.fpage"</code>.
   */
  uint32                     iDocument;

  /** \brief Zero-based index to the page context. For instance, this would
   *   be <code>4</code> where <code>szURI</code>
   *   is <code>"/documents/1/pages/5.fpage"</code>.
   */
  uint32                     iPage;

  /**
   * \brief A member of the <code>ResourceType_*</code> enumeration.
   */
  uint32                     resourceType;

  /**
   * \brief A pointer to the next entry in the list, or <code>NULL</code> if
   * this is the final entry.
   */
  struct _URIContextMapping *pNext;
} URIContextMapping;

/**
 * \brief Encapsulates a single linked list of <code>URIContextMapping</code>
 * objects.
 */
typedef struct _URIContextMappingsList
{
  /** \brief Pointer to the first entry in the list. */
  URIContextMapping         *pFirst;
  /** \brief Pointer to the current final entry in the list. */
  URIContextMapping         *pLast;
} URIContextMappingsList;


/*
 * ******************************* Functions ***********************************
 */

/**
 * \brief Helper utility for looking up a single entry in a list of URI
 * context mappings.
 *
 * \param pMappings The list to be searched.
 *
 * \param pszPartName The part name acting as the search key.
 *
 * \return A pointer to a matching entry within the given list, or <code>NULL</code>
 * if there is no matching entry. When a pointer is returned, it is always
 * a direct pointer to an entry within the given list. It is not a pointer
 * to a copy of the entry. If a fresh copy of the entry is required, the caller
 * is responsible for creating it. The caller should not free the returned
 * structure directly. The mappings list as a whole will be freed automatically
 * when the XPS package is destroyed with <code>freeXPSPackageInMemory()</code>.
 */
extern URIContextMapping *lookupContextMapping
  ( URIContextMappingsList *pMappings, uint8 *pszPartName );

/**
 * \brief Build an XPS package in memory, and return a pointer to the build
 * context.
 *
 * <p>Use <code>freeXPSPackageInMemory()</code> to completely delete the package
 * when it is no longer needed. Failure to do this will incur memory leaks.
 *
 * <p>XPS Packages built using this function are built using the Memory File
 * System (MFS). Therefore, they only persist while the host process is
 * alive. The root directory of the package is returned by
 * \c getXPSPackageRoot(). This root directory should be used to mount
 * a new instance of the XPS input device (\ref xpsdev.c). It is then
 * possible to render the package using the \c xmlexec operator,
 * using the "_rels/.rels" filename to bootstrap the interpretation.
 *
 * It is possible to add further pages and documents to the package while
 * it is being interpreted. The XPS input device arranges to automatically
 * block the interpreter when the RIP reaches a part that is not yet
 * available. For this reason, it is \e vital to call the
 * \c commitXPSPackageInMemory() function when there are no more parts
 * to be added. Failure to call this function will cause the RIP to
 * stall indefinitely, expecting further parts to be added. The
 * functions \c addFixedPageToXPSPackage() and
 * \c addFixedDocumentToXPSPackage() can be used to extend the package
 * while the RIP is processing it. This method of delivery is very useful
 * when integrating the RIP with pipelined architectures, such as the
 * XPSDrv Filter Pipeline. When pre-RIP components are generating package
 * parts incrementally, it is more efficient to start RIPping the package
 * immediately, rather than to wait until all parts are available.
 *
 * \param pXPD Pointer to the complete description of the XPS package.
 *
 * \param pMappings Pointer to an empty list of URI context mappings. The list
 * will be populated by this function, if it succeeds. The caller should
 * not free this list, or any of its elements. The list as a whole will be
 * freed by the later call to <code>freeXPSPackageInMemory()</code>, which
 * the caller is expected to make when the package is no longer required.
 *
 * \param pDevErr If there is a failure, this parameter will receive a standard
 * error code, according to the conventions of the Core RIP Device
 * Interface.
 *
 * \return If the function succeeds, the return value is a pointer to a
 * build context. This is an opaque token that must be used in all later
 * calls to \c addFixedDocumentToXPSPackage(), \c addFixedPageToXPSPackage(),
 * \c getXPSPackageRoot(), \c commitXPSPackageInMemory() and
 * \c freeXPSPackageInMemory(). If the function fails, the return value
 * is \c NULL.
 */
extern void *buildXPSPackageInMemory
  ( XPSPackageDescription *pXPD, URIContextMappingsList *pMappings, int32 *pDevErr );

/**
 * \brief Add a new XPS Fixed Page to a package.
 *
 * \param pContext A pointer to the build context. This \e must be a
 * pointer that was returned from an earlier, successful call to
 * <code>buildXPSPackageInMemory()</code>..
 *
 * \param pPage A pointer to a description of the new page, including
 * the URIs and content types of all resources used by the page. This
 * interface does not support incremental process at any level finer
 * than a single page. For instance, it is not possible to add further
 * resource URIs or content types for a page, after calling this
 * function.
 *
 * \param pDevErr If there is a failure, this parameter will receive a standard
 * error code, according to the conventions of the Core RIP Device
 * Interface.
 */
extern int32 addFixedPageToXPSPackage
  ( void *pContext, XPSPageDescription *pPage, int32 *pDevErr );

/**
 * \brief Add a new XPS Fixed Document to a package.
 *
 * \param pContext A pointer to the build context. This \e must be a
 * pointer that was returned from an earlier, successful call to
 * <code>buildXPSPackageInMemory()</code>..
 *
 * \param pDocument A pointer to a description of the new document,
 * which may include one or more pages. It is permissible to add a new
 * document /e before adding any of its pages. This, in fact, would
 * be typical when integrating the RIP with a pipeline architecture.
 * It might be better to think of this function as /e starting a new
 * document, rather than adding it in its entirity (although the interface
 * also allows that). Subsequent calls to \c addFixedPageToXPSPackage()
 * can be considered as adding pages to this document, until a further
 * document is added, or the package is closed.
 *
 * \param pDevErr If there is a failure, this parameter will receive a standard
 * error code, according to the conventions of the Core RIP Device
 * Interface.
 */
extern int32 addFixedDocumentToXPSPackage
  ( void *pContext, XPSDocumentDescription *pDocument, int32 *pDevErr );

/**
 * \brief Returns the root node of RAM filesystem storage for the XPS
 * package.
 *
 * The node returned by this function will be a directory node. A file
 * of the name "_rels/.rels" will exist relative to this root. This file
 * can be used as input to the \c xmlexec operator, in order to
 * interpret and render the package.
 *
 * The node returned by this function should not be freed directly with
 * \c MFSReleaseRoot(). Instead, always free the package and all of its
 * resources using \c freeXPSPackageInMemory().
 *
 * \param pContext A pointer to the build context. This \e must be a
 * pointer that was returned from an earlier, successful call to
 * <code>buildXPSPackageInMemory()</code>.
 */
extern MFSNODE *getXPSPackageRoot( void *pContext );

/**
 * \brief Signal that an XPS package is complete.
 *
 * This function should be called when there are no further pages or
 * documents to be added to the package.
 *
 * The RIP cannot finish rendering an XPS package until this function
 * has been called. Even if the initial package description is complete
 * when passed to \c buildXPSPackageInMemory(), it is still necessary
 * to call \c commitXPSPackageInMemory() afterwards. Failure to call
 * this function will cause a deadlock in the RIP.
 *
 * Once this function has been called for a particular XPS package, it
 * is no longer legal to add any further contents to the package
 * with \c addFixedPageToXPSPackage() or \c addFixedDocumentToXPSPackage().
 * Any attempt to do so may have undesirable consequences.
 *
 * \param pContext A pointer to the build context. This \e must be a
 * pointer that was returned from an earlier, successful call to
 * <code>buildXPSPackageInMemory()</code>.
 *
 * \param pDevErr If there is a failure, this parameter will receive a standard
 * error code, according to the conventions of the Core RIP Device
 * Interface.
 */
extern void commitXPSPackageInMemory( void *pContext, int32 *pDevErr );

/**
 * \brief Release all memory associated with an XPS package that was previously
 * built with <code>buildXPSPackageInMemory()</code>.
 *
 * \param pContext A pointer to the build context. This \e must be a
 * pointer that was returned from an earlier, successful call to
 * <code>buildXPSPackageInMemory()</code>.
 *
 * \param pMappings Pointer to a populated list of URI context mappings.
 * This <em>must</em> be a list that was populated by an earlier, successful
 * call to <code>buildXPSPackageInMemory()</code>.
 */
extern void freeXPSPackageInMemory
  ( void *pContext, URIContextMappingsList *pMappings );

#endif

