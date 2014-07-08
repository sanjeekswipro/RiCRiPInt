/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWskinkit!export:streams.h(EBDSDK_P.1) $
 */

#ifndef __STREAMS_H__
#define __STREAMS_H__

#include "std.h"
#include "ripcall.h"

/**
 * \file
 * \ingroup skinkit
 *
 * \brief A simple streaming abstraction to handle RIP I/O when using the
 * Reference API.
 *
 * <p>The Reference API permits jobs to be submitted as static files on
 * disk, and for the output to be written to disk in various supported
 * formats. However, it is also possible for the input and output to
 * be handled via arbitrary streams, provided that those streams are implemented
 * by the caller. This header file provides the signatures for such
 * implementations.
 *
 * <p>To implement an input stream, the calling code must supply an
 * implementation of <code>StreamCloseFn</code>, <code>StreamBytesFn</code>,
 * <code>ReadStreamSeekFn</code> and <code>ReadStreamReadFn</code>.
 * The seeking function must be implemented, although it is permitted to
 * return an error in cases where random access is not supported.
 * These four implementations must be packaged into a
 * <code>HqnReadStream</code> object structure, also defined in this
 * file.
 *
 * <p>To implement an output stream, the calling code must supply an
 * implementation of <code>StreamCloseFn</code>, <code>StreamBytesFn</code>,
 * <code>WriteStreamSeekFn</code> and <code>WriteStreamWriteFn</code>.
 * These are then packaged into a <code>HqnWriteStream</code> structure.
 *
 * <p>A stream is <em>per-job</em>. There is no call to open a stream, or
 * to reset it, either for input or output. A close call is provided in
 * case the implementation needs to perform cleanup actions at the end of
 * a job.
 *
 * <p>For further information about the use of <code>HqnReadStream</code>
 * and <code>HqnWriteStream</code>, please refer to the documentation for
 * the \ref refimpl Reference API.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_BYTES_AVAILABLE 0
#define STREAM_BYTES_TOTAL 1

#define STREAM_POSITION_START 0
#define STREAM_POSITION_CURRENT 1
#define STREAM_POSITION_END 2

/**
 * \brief Called by the RIP to close the stream. The implementation may perform
 * any cleanup actions that are required by the underlying streaming protocol.
 *
 * \param pPrivate The private data pointer from the stream structure. This
 * must be supplied to every function call made on the stream, in order for
 * the implementation to access any internal state that it needs to store.
 *
 * \return Return a value less than zero to indicate an error in closing
 * the stream, otherwise return zero.
 */
typedef int32 (RIPCALL *StreamCloseFn) ( void *pPrivate );

/**
 * \brief Determine the bytes available or the total size of the stream.
 *
 * \param pPrivate The private data pointer from the stream structure. This
 * must be supplied to every function call made on the stream, in order for
 * the implementation to access any internal state that it needs to store.
 *
 * \param pBytes A signed 64-bit value to be updated with the bytes
 * available or total size as appropriate.
 *
 * \param reason Either <code>STREAM_BYTES_AVAILABLE</code> or
 * <code>STREAM_BYTES_TOTAL</code> to indicate what <code>pBytes</code>
 * should be set to.
 *
 * \return Return a value of zero to indicate an error, a non-zero value
 * to indicate success.
 */
typedef int32 (RIPCALL *StreamBytesFn) ( void *pPrivate, Hq32x2 *pBytes, int32 reason );

/**
 * \brief Perform a seek on the input stream.
 *
 * <p>If random access is not supported by the stream implementation, the
 * RIP may still call this function as a way of obtaining the current
 * value of the stream pointer. It does this by requesting a seek of
 * zero, relative to the current file position.
 *
 * <p>A seek of zero relative to the end of the stream must be
 * interpreted as a flush operation.
 * For a seekable stream this will simply be a seek to the end of
 * the stream.  For a non-seekable stream it requires all pending
 * input to be discarded.
 *
 * \param pPrivate The private data pointer from the stream structure. This
 * must be supplied to every function call made on the stream, in order for
 * the implementation to access any internal state that it needs to store.
 *
 * \param pPos A signed 64-bit value indicating the target movement. The
 * implementation should always update this value with the current
 * stream pointer after performing the seek.
 *
 * \param flag One of <code>STREAM_POSITION_START</code>,
 * <code>STREAM_POSITION_CURRENT</code> or
 * <code>STREAM_POSITION_END</code>. The <code>pPos</code> argument
 * is then interpreted as being relative to the start of the stream,
 * relative to the current location, or relative to the end of the
 * stream respectively.
 *
 * \return Return a value of zero to indicate an error, a non-zero value
 * to indicate success.
 */
typedef int32 (RIPCALL *ReadStreamSeekFn) ( void *pPrivate, Hq32x2 *pos, int32 flag );

/**
 * \brief Read some bytes from the input stream.
 *
 * \param pPrivate The private data pointer from the stream structure. This
 * must be supplied to every function call made on the stream, in order for
 * the implementation to access any internal state that it needs to store.
 *
 * \param pBuffer Pointer to a byte buffer, into which the stream implementation
 * should copy the next chunk of input data. This buffer memory is managed by
 * the RIP. The stream implementation should not attempt to free this memory,
 * nor should it ever store away the pointer for later use.
 *
 * \param cbRequested Maximum number of bytes that should be copied.
 *
 * \return A value less than zero should be returned to indicate an
 * IO error. If the read is successful, the function should return the actual
 * number of bytes copied.
 */
typedef int32 (RIPCALL *ReadStreamReadFn) ( void *pPrivate, uint8 *pBuffer, int32 cbRequested );

/**
 * \brief Perform a seek on the output stream.
 *
 * <p>If random access is not supported by the stream implementation, the
 * RIP may still call this function as a way of obtaining the current
 * value of the stream pointer. It does this by requesting a seek of
 * zero, relative to the current file position.
 *
 * \param pPrivate The private data pointer from the stream structure. This
 * must be supplied to every function call made on the stream, in order for
 * the implementation to access any internal state that it needs to store.
 *
 * \param pPos A signed 64-bit value indicating the target movement. The
 * implementation should always update this value with the current
 * stream pointer after performing the seek.
 *
 * \param flag One of <code>STREAM_POSITION_START</code>,
 * <code>STREAM_POSITION_CURRENT</code> or
 * <code>STREAM_POSITION_END</code>. The <code>pPos</code> argument
 * is then interpreted as being relative to the start of the stream,
 * relative to the current location, or relative to the end of the
 * stream respectively.
 *
 * \return Return a value of zero to indicate an error, a non-zero value
 * to indicate success.
 */
typedef int32 (RIPCALL *WriteStreamSeekFn) ( void *pPrivate, Hq32x2 *pPos, int32 flag );

/**
 * \brief Write some bytes to the output stream.
 *
 * \param pPrivate The private data pointer from the stream structure. This
 * must be supplied to every function call made on the stream, in order for
 * the implementation to access any internal state that it needs to store.
 *
 * \param pBuffer Pointer to a byte buffer, from which the stream implementation
 * should copy the next chunk of output data. This buffer memory is managed by
 * the RIP. The stream implementation should not attempt to free this memory,
 * nor should it ever store away the pointer for later use.
 *
 * \param cbAvailable Maximum number of bytes that should be copied.
 *
 * \return A value less than zero should be returned to indicate an
 * IO error. If the read is successful, the function should return the actual
 * number of bytes written.
 */
typedef int32 (RIPCALL *WriteStreamWriteFn) ( void *pPrivate, uint8 *pBuffer, int32 cbAvailable );

/**
 * \brief This structure encapsulates the functions required to implement
 * an input stream.
 */
typedef struct _HqnReadStream
{
  StreamCloseFn        pfClose;
  StreamBytesFn        pfBytes;
  ReadStreamSeekFn     pfSeek;
  ReadStreamReadFn     pfRead;
  void                *pPrivate;
} HqnReadStream;

/**
 * \brief This structure encapsulates the functions required to implement
 * an output stream.
 */
typedef struct _HqnWriteStream
{
  StreamCloseFn        pfClose;
  StreamBytesFn        pfBytes;
  WriteStreamSeekFn    pfSeek;
  WriteStreamWriteFn   pfWrite;
  void                *pPrivate;
} HqnWriteStream;

#define XPSPT_LEVEL_DEFAULT 0
#define XPSPT_LEVEL_JOB 1
#define XPSPT_LEVEL_DOCUMENT 2
#define XPSPT_LEVEL_PAGE 3
#define XPSPT_LEVELS_MAX  4

#define XPSPT_DIGSIG_NOTCHECKED 0
#define XPSPT_DIGSIG_NOTSIGNED 1
#define XPSPT_DIGSIG_INVALID 2
#define XPSPT_DIGSIG_VALID 3
#define XPSPT_DIGSIG_ERROR 4

/**
 * \brief Obtain a stream into which a new Print Ticket part can be
 * written.
 *
 * <p>This function is part of a protocol that allows the Harlequin RIP
 * SDK to use an exterior component for handling print tickets. When a
 * new print ticket XML part is discovered, this method can be used to
 * transmit the XML text out to the hosting application.
 *
 * \param pPrivate The private data pointer of the <code>PTHandler</code>
 * structure, of which this function is a part. This <em>must</em> be supplied
 * to every call, in order for the implementation to access any internal
 * state that it needs.
 */
typedef HqnWriteStream* (RIPCALL *PTHandlerOpenDeltaFn) ( void *pPrivate );

/**
 * \brief Merge a print ticket delta with the current
 *
 * <p>This function is part of a protocol that allows the Harlequin RIP
 * SDK to use an exterior component for handling print tickets.
 *
 * \param pPrivate The private data pointer of the <code>PTHandler</code>
 * structure, of which this function is a part. This <em>must</em> be supplied
 * to every call, in order for the implementation to access any internal
 * state that it needs.
 *
 * \param pDelta A stream that was previously obtained from a call to
 * the <code>PTHandlerOpenDeltaFn</code>. The stream is expected to have
 * been populated with the new print ticket XML fragment. If this function
 * succeeds, it will close and dispose of the stream, and the caller
 * should not use it again. It is legal for the stream to be empty. In
 * this case, a degenerate merge will take place, returning the current
 * effective print ticket unchanged.
 *
 * \param nLevel The scope level for the merged print ticket. This must
 * be greater than or equal to <code>XPSPT_LEVEL_JOB</code>, but not greater
 * than <code>XPSPT_LEVEL_PAGE</code>.
 */
typedef HqnReadStream* (RIPCALL *PTHandlerMergeDeltaFn)
   ( void *pPrivate, HqnWriteStream *pDelta, int32 nLevel );

/**
 * \brief Signal XPS progress information.
 *
 * <p>This function is part of a protocol that allows the Harlequin RIP
 * SDK to use an exterior component to monitor progress.
 *
 * \param pPrivate The private data pointer of the <code>PTHandler</code>
 * structure, of which this function is a part. This <em>must</em> be supplied
 * to every call, in order for the implementation to access any internal
 * state that it needs.
 *
 * \param nLevel The XPS level at which processing is currently being
 * performed. This must be greater than or equal to
 * <code>XPSPT_LEVEL_JOB</code>, and not greater than
 * <code>XPSPT_LEVEL_PAGE</code>.
 * \param fEntered <code>TRUE</code> if processing is beginning at the specified
 * <code>nLevel</code>, <code>FALSE</code> if processing is complete.
 */
typedef void (RIPCALL *PTHandlerProgressCallbackFn)
   ( void *pPrivate, int32 nLevel, int32 fEntered );

/**
 * \brief Check the digital signing status of the current job.
 *
 * This function is part of a protocol that allows the Harlequin RIP SDK
 * to use an exterior component for handling print tickets. Digital signatures
 * are not directly related to print tickets. However, it is a print
 * ticket setting (\c JobDigitalSignatureProcessing) that dictates
 * whether the signing status needs to be verified.
 *
 * The RIP calls this function when it determines that the job needs a
 * signature check. This will happen at most once per job. For performance
 * reasons, digital signature checks are skipped unless the print ticket
 * requires them. Also, checks are not performed via this protocol if
 * they have already been performed prior to starting the job.
 *
 * \param pPrivate The private data pointer of the <code>PTHandler</code>
 * structure, of which this function is a part. This <em>must</em> be supplied
 * to every call, in order for the implementation to access any internal
 * state that it needs.
 *
 * \return One of \c XPSPT_DIGSIG_NOTCHECKED, \c XPSPT_DIGSIG_NOTSIGNED,
 * \c XPSPT_DIGSIG_INVALID, \c XPSPT_DIGSIG_VALID,
 * \c XPSPT_DIGSIG_ERROR.
 */
typedef uint32 (RIPCALL *PTHandlerGetDigitalSignatureStatusFn) ( void *pPrivate );

/**
 * \brief This structure encapsulates the protocol that allows the Harlequin RIP
 * SDK to use an exterior component for handling print tickets.
 *
 * <p>If the hosting application uses the Reference API to drive the RIP,
 * it can pass a pointer to this structure through the
 * <code>RunXPSJob()</code> entry point.
 * <p>If pfProgressCallback is NULL then no progress callbacks will occur.
 */
typedef struct _PTHandler
{
  PTHandlerOpenDeltaFn                 pfOpenDelta;
  PTHandlerMergeDeltaFn                pfMergeDelta;
  void                                *pPrivate;
  PTHandlerProgressCallbackFn          pfProgressCallback;
  PTHandlerGetDigitalSignatureStatusFn pfGetDigitalSignatureStatus;
} PTHandler;

/**
 * \brief Encapsulates the mapping between a single XPS document part
 * and its corresponding content type.
 *
 * <p>This structure is analogous to a single <b>&lt;Override&gt</b> element
 * in the <code>[Content_Types].xml</code> file of an XPS package. It maps
 * a part name URI, such as <code>"/Documents/1/Resources/Fonts/Times.ttf"</code>
 * onto its content type, such as <code>"application/vnd.ms-opentype"</code>.
 */
typedef struct _XPSContentTypeEntry
{
  /** \brief The part name, relative to the root of the package. */
  uint8                 *pszResourceUri;
  /** \brief The content type. */
  uint8                 *pszContentType;
} XPSContentTypeEntry;

/**
 * \brief Describes a bounded array of content type mappings.
 *
 * <p>An <code>XPSContentTypeMap</code> is required as part of the description
 * of a single page within an XPS package. The map must have an entry for
 * every resource that is known to be used by that page.
 */
typedef struct _XPSContentTypeMap
{
  /** \brief The number of entries in the map. */
  uint32                 nEntries;
  /** \brief Pointer to the base of an array of exactly <code>nEntries</code>
   *  elements, where each entry describes a single mapping. */
  XPSContentTypeEntry   *pEntries;
} XPSContentTypeMap;

/**
 * \brief Describes a single page within an XPS package.
 */
typedef struct _XPSPageDescription
{
  /**
   * \brief Flag indicating whether this page has its own print ticket.
   *
   * <p>Set this to <code>TRUE</code> if this page has its own associated
   * print ticket stream, otherwise set it to <code>FALSE</code>. The RIP
   * uses this flag to determine whether it should call out to the
   * <code>XPSPackageStreamManager</code> with a resource type of
   * <code>ResourceType_Page_PT</code> for this page. If the flag is set
   * to <code>FALSE</code>, the RIP will not make such a call for this
   * page. This can improve efficiency, and avoid the need to implement
   * empty streams where print tickets are not present.
   */
  uint32               fHasPrintTicket;

  /**
   * \brief Maps every resource used by the page to its correct content type.
   *
   * <p>There <em>must</em> be an entry in this map corresponding to
   * every different resource that is known to be used by the markup for
   * this page. Failure to include every resource may result in processing
   * errors as the RIP attempts to interpret the virtual package.
   */
  XPSContentTypeMap   *pContentTypeMap;

  /**
   * \brief The absolute URI of the FixedPage part itself.
   *
   * If this is \c NUL, the URI of the page is assumed to follow the
   * conventional XPS package structure where, for instance, the URI
   * for page 5 of document 1 would be "/Documents/1/Pages/5.fpage".
   * If the managed package is using a different structure, the caller
   * should always use this field to override the default. Failure to
   * do this can cause problems when there are relative resource
   * references within the page markup.
   */
  uint8               *pszFixedPageUri;

  /**
   * \brief The absolute URI of the Print Ticket part associated with
   * this page, if any.
   *
   * If \c fHasPrintTicket is \c FALSE, then this field will be ignored,
   * and would conventionally be set to \c NUL by the caller.
   * Otherwise, if this field is \c NUL, the RIP will assume a default
   * URI for the Print Ticket, appropriate to the scope of the page.
   * If a specific Print Ticket URI is known to the caller, it should
   * always be supplied in this field.
   */
  uint8               *pszPrintTicketUri;

} XPSPageDescription;

/**
 * \brief Describes a single document within an XPS package.
 */
typedef struct _XPSDocumentDescription
{
  /**
   * \brief Flag indicating whether this document has its own print ticket.
   *
   * <p>Set this to <code>TRUE</code> if this document has its own associated
   * print ticket stream, otherwise set it to <code>FALSE</code>. The RIP
   * uses this flag to determine whether it should call out to the
   * <code>XPSPackageStreamManager</code> with a resource type of
   * <code>ResourceType_Document_PT</code> for this document. If the flag is set
   * to <code>FALSE</code>, the RIP will not make such a call for this
   * document. This can improve efficiency, and avoid the need to implement
   * empty streams where print tickets are not present.
   */
  uint32               fHasPrintTicket;

  /**
   * \brief The number of pages within this document.
   */
  uint32               nPages;

  /**
   * \brief Pointer to the base of an array of exactly <code>nPages</code>
   * elements, describing each page in the document.
   */
  XPSPageDescription  *pPageDescriptions;

  /**
   * \brief The absolute URI of the FixedDocument part itself.
   *
   * If this is \c NUL, the URI of the document is assumed to follow the
   * conventional XPS package structure where, for instance, the URI
   * for document 1 would be "/Documents/1/FixedDocument.fdoc".
   * If the managed package is using a different structure, the caller
   * should always use this field to override the default. Failure to
   * do this can cause problems when there are relative resource
   * references within the document markup.
   */
  uint8               *pszFixedDocumentUri;

  /**
   * \brief The absolute URI of the Print Ticket part associated with
   * this document, if any.
   *
   * If \c fHasPrintTicket is \c FALSE, then this field will be ignored,
   * and would conventionally be set to \c NUL by the caller.
   * Otherwise, if this field is \c NUL, the RIP will assume a default
   * URI for the Print Ticket, appropriate to the scope of the document.
   * If a specific Print Ticket URI is known to the caller, it should
   * always be supplied in this field.
   */
  uint8               *pszPrintTicketUri;

} XPSDocumentDescription;

/**
 * \brief Describes a complete XPS package, whose individual part streams are
 * managed outside of the RIP.
 *
 * <p>The RIP has the ability to render XPS files from "virtual" packages,
 * without needing them to be contained within a physical ZIP file. The
 * <code>XPSPackageDescription</code> and <code>XPSPackageStreamManager</code>
 * structures allow the RIP client to describe the contents of an XPS
 * package abstractly, and then to provide streams on individual package
 * parts as and when the RIP needs to read them.
 *
 * <p>This structure, along with those nested beneath it, is responsible
 * for describing the overall <dfn>shape</dfn> of the package: the number
 * of documents in the package, the number of pages in each document,
 * the content types of the resources used by each page. None of the actual
 * package <em>contents</em> are contained within this structure: there are
 * no pointers to XML markup strings, nor to any image or font data.
 * Instead, the package description can be seen as an implicit declaration
 * that a certain number of <dfn>part streams</dfn> exist, and are known
 * to the client. Given this declaration, the RIP can process the package,
 * as long as it also has a means of opening each part stream on demand.
 * This facility is provided by the <code>XPSPackageStreamManager</code>.
 *
 * <p>Suppose that we have a single-page XPS package. The page markup is
 * known to reference a font resource whose URI is
 * <code>"/Documents/1/Resources/Fonts/Times.ttf"</code>. The package has
 * a job-level print ticket, but no document or page-level print tickets.
 * This example package could be described to the RIP using the following
 * sequence of declarations:
 *
  \verbatim
  static XPSContentTypeEntry ctEntries[] =
  {
    { "/Documents/1/Resources/Fonts/Times.ttf", "application/vnd.ms-opentype" }
  };
 
  static XPSContentTypeMap ctMap = { 1, ctEntries };
 
  static XPSPageDescription pages[] = { { FALSE, &ctMap } };
 
  static XPSDocumentDescription docs[] = { { FALSE, 1, pages } };
 
  static XPSPackageDescription package = { TRUE, 1, docs };
  \endverbatim
 *
 * <p>If the caller were to describe this package to the RIP, it would also
 * need to be prepared to provide <code>HqnReadStream</code>s for:
 * the job-level print ticket, the page markup, and the font resource
 * whose URI is <code>"/Documents/1/Resources/Fonts/Times.ttf"</code>. As long
 * as the RIP can read from these three external streams, it can interpret and
 * render the complete package.
 *
 * \see XPSPackageStreamManager
 */
typedef struct _XPSPackageDescription
{
  /**
   * \brief Flag indicating whether this package has a job-level print ticket.
   *
   * <p>Set this to <code>TRUE</code> if this package has an associated job-wide
   * print ticket stream, otherwise set it to <code>FALSE</code>. The RIP
   * uses this flag to determine whether it should call out to the
   * <code>XPSPackageStreamManager</code> with a resource type of
   * <code>ResourceType_Job_PT</code> for this job. If the flag is set
   * to <code>FALSE</code>, the RIP will not make such a call for this
   * job. This can improve efficiency, and avoid the need to implement
   * empty streams where print tickets are not present.
   */
  uint32                  fHasPrintTicket;

  /**
   * \brief The number of documents contained within the package.
   */
  uint32                  nDocuments;

  /**
   * \brief Pointer to the base of an array of exactly <code>nDocuments</code>
   * elements, describing each document in the package.
   */
  XPSDocumentDescription *pDocumentDescriptions;

  /**
   * \brief The absolute URI of the FixedDocumentSequence part.
   *
   * If this is \c NUL, the URI of the document sequence is assumed to follow the
   * conventional XPS package structure where the URI would be
   * "/FixedDocumentSequence.fdseq". If the managed package is using a
   * different structure, the caller should always use this field to override
   * the default. Failure to do this can cause problems when there are relative
   * resource references within the document sequence markup.
   */
  uint8                  *pszFixedDocumentSequenceUri;

  /**
   * \brief The absolute URI of the Print Ticket part associated with
   * the FixedDocumentSequence, if any.
   *
   * If \c fHasPrintTicket is \c FALSE, then this field will be ignored,
   * and would conventionally be set to \c NUL by the caller.
   * Otherwise, if this field is \c NUL, the RIP will assume a default
   * URI for the Print Ticket, appropriate to the root scope of the package.
   * If a specific Print Ticket URI is known to the caller, it should
   * always be supplied in this field.
   */
  uint8                  *pszPrintTicketUri;

} XPSPackageDescription;

/**
 * \brief Classifies XPS package stream types.
 *
 * <p>When the RIP renders an XPS document whose part streams are
 * managed by the caller, the <code>XPSPackageStreamManager</code> protocol
 * is used by the RIP to open streams on the individual package parts.
 * For arbitrary resources (such as fonts or images), the RIP addresses
 * the stream using the original URI of the part, as described by the
 * caller in the <code>XPSPackageDescription</code>. For more specific
 * streams, such as page markup and print tickets, the part is addressed
 * solely by its overall type and its context within the package.
 * (The "context" is the page number within the document, and the
 * document number within the package). This avoids the RIP needing to
 * make any assumptions about the URIs for parts that were not
 * explicitly declared. For instance, the markup for page 5 of document
 * 1 would <em>usually</em> be <code>"/Documents/1/Pages/5.fpage"</code>,
 * but it could just as easily be something quite different. Rather
 * than opening the markup using this URI, the RIP can open the markup
 * by using <code>ResourceType_Page_Markup</code>, and then just specifying
 * the document number and page number as integers. The RIP client can
 * interpret these according to its own URI-allocation scheme.
 *
 * \see OpenResourceStreamFn
 */
enum
{
   /** \brief The job-level print ticket stream. */
   ResourceType_Job_PT,
   /** \brief The document-level print ticket stream. */
   ResourceType_Document_PT,
   /** \brief The page-level print ticket stream. */
   ResourceType_Page_PT,
   /** \brief The XML markup for a specific page. */
   ResourceType_Page_Markup,
   /** \brief An arbitrary package resource. */
   ResourceType_URI
};

/**
 * \brief Function called by the RIP to open a stream on a specified resource
 * within the context of an XPS package.
 *
 * <p>This function signature forms part of the
 * <code>XPSPackageStreamManager</code> protocol, which allows the RIP
 * to consume XPS from a virtual package, whose separate part streams
 * are managed by the RIP's client.
 *
 * \param pPrivate Pointer to arbitrary, client-managed data. The client
 * stores this pointer as the <code>pPrivate</code> member of the
 * <code>XPSPackageStreamManager</code> structure.
 *
 * \param iDocument Zero-based index indicating the document context. The
 * number of documents in the package will have been described earlier
 * by the caller using an <code>XPSPackageDescription</code> structure.
 * This index will never exceed one less than the number of documents.
 *
 * \param iPage Zero-based index indicating the page context, relative to
 * the document context given by <code>iDocument</code>. The number of
 * pages in the document will have been described earlier by the caller
 * using an <code>XPSDocumentDescription</code> structure, nested within
 * an <code>XPSPackageDescription</code>. This index will never exceed
 * one less than the number of pages in the relevant document.
 *
 * \param resourceType A member of the <code>ResourceType_*</code>
 * enumeration.
 *
 * \param pszUri When the resource type is <code>ResourceType_URI</code>,
 * this argument is a pointer to a NUL-terminated string, which gives
 * the part name of the resource that the RIP needs to read. This string
 * should be treated in a <em>case-insensitive</em> fashion, and should
 * be used to lookup the correct resource, in the context of the given
 * document and page.
 *
 * \param pStream If the function succeeds, this parameter receives a
 * pointer to the <code>HqnReadStream</code>, from which the RIP can read
 * the required data. The stream should be fully initialized, with the
 * data pointer set at the beginning. If the function fails, this pointer
 * should be set to <code>NULL</code>. The stream returned <em>must</em>
 * support the <code>Seek()</code> operation.
 *
 * \return <code>TRUE</code> if the function succeeds, otherwise
 * <code>FALSE</code>.
 *
 * \see XPSPackageDescription
 * \see XPSPackageStreamManager
 * \see CloseResourceStreamFn
 */
typedef int32 (RIPCALL *OpenResourceStreamFn)
  (
    void            *pPrivate,
    uint32           iDocument,
    uint32           iPage,
    uint32           resourceType,
    uint8           *pszUri,
    HqnReadStream  **pStream
  );

/**
 * \brief Function called by the RIP to close a stream on an XPS package
 * resource.
 *
 * <p>This function signature forms part of the
 * <code>XPSPackageStreamManager</code> protocol, which allows the RIP
 * to consume XPS from a virtual package, whose separate part streams
 * are managed by the RIP's client.
 *
 * \param pPrivate Pointer to arbitrary, client-managed data. The client
 * stores this pointer as the <code>pPrivate</code> member of the
 * <code>XPSPackageStreamManager</code> structure.
 *
 * \param pStream A stream that was previously opened by a call to
 * <code>OpenResourceStreamFn</code> within the scope of the same
 * <code>XPSPackageStreamManager</code>. The stream's own <code>Close()</code>
 * function will already have been called by the RIP.
 *
 * \return <code>TRUE</code> if the function succeeds, otherwise
 * <code>FALSE</code>.
 *
 * \see XPSPackageDescription
 * \see XPSPackageStreamManager
 * \see OpenResourceStreamFn
 */
typedef int32 (RIPCALL *CloseResourceStreamFn)
  (
    void            *pPrivate,
    HqnReadStream   *pStream
  );

/**
 * \brief Combines the function protocols for opening and closing individual
 * XPS part streams, along with the client data pointer that should be passed
 * to each function.
 */
typedef struct _XPSPackageStreamManager
{
  /** \brief Pointer to client-managed data. This will be passed as the first
   * argument to each of <code>pfnOpenResourceStream()</code> and
   * <code>pfnCloseResourceStream()</code>.
   */
  void                  *pPrivate;
  /** \brief Function to open a stream on a specified package resource. */
  OpenResourceStreamFn   pfOpenResourceStream;
  /** \brief Function to close a stream on a package resource. */
  CloseResourceStreamFn  pfCloseResourceStream;
} XPSPackageStreamManager;


#ifdef __cplusplus
}
#endif

#endif

