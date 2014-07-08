/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:xpsbuild.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Implements an API for building XPS package parts using the in-memory
 *        virtual filesystem (MFS).
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>   /* tolower() */

#include "std.h"
#include "hqstr.h"
#include "memfs.h"
#include "mem.h"
#include "xpsbuild.h"
#include "swdevice.h"  /* DeviceVMError */

/** \brief Number of characters to allocate for a single line of XML text. */
#define XML_LINEBUFFER_SIZE 1024


/*
 * ******************************* Local Types ***********************************
 */

enum
{
  PartType_FDSEQ,
  PartType_FDOC,
  PartType_FPAGE,
  PartType_JobPT,
  PartType_DocPT,
  PartType_PagePT
};

/* NB: Members of this enum correspond to array indexes of
   pszRelationshipTypes below. Take care if changing this definition. */
enum
{
  RelType_FixedRepresentation = 0,
  RelType_RequiredResource,
  RelType_PrintTicket
};

/**
 * \brief Manages the creation of Relationship tags with a .rels file.
 *
 * <p>Each relationship has an Id attribute, which must be unique within
 * the file. This requirement is satisfied by the <code>id</code> field
 * of this structure, which is incremented each time a tag is written.
 *
 * \see openXMLRelationships()
 * \see writeXMLRelationship()
 * \see closeXMLRelationships()
 */
typedef struct _RelsFileManager
{
  MFSFILEDESC    *pFD;
  uint32          id;
} RelsFileManager;

/**
 * \brief Simple structure for maintaining a linear list of part names.
 *
 * \see ContentTypesManager
 */
typedef struct _PartNameList
{
  /** \brief NUL-terminated part name, relative to the package root. */
  char                  szPartName[ LONGESTFILENAME ];
  /** \brief Pointer to the next entry, or <code>NULL</code> if this is
   *  the final entry. */
  struct _PartNameList *pNext;
} PartNameList;

/**
 * \brief Manages the creation of Override tags within the global content
 * types streams.
 *
 * \see openContentTypes()
 * \see writeContentTypeOverride()
 * \see closeContentTypes()
 */
typedef struct _ContentTypesManager
{
  /** \brief File descriptor for "/[Content_Types].xml". */
  MFSFILEDESC     *pFD;
  /** \brief List of part names whose content type overrides have already been
   *  written. Used to avoid emitting duplicate entries, which are illegal. */
  PartNameList   *pPartNames;
} ContentTypesManager;

/**
 * \brief Provides all of the contextual data needed to add a fixed page
 * to the package.
 *
 * \see buildFixedPage()
 */
typedef struct _PageBuildContext
{
  /** \brief Description of the page, as provided by the package manager. */
  XPSPageDescription       *pPage;
  /** \brief Accumlated list of context mappings. */
  URIContextMappingsList   *pContextMappings;
  /** \brief Represents the global [Content_Types].xml file. */
  ContentTypesManager      *pCTM;
  /** \brief The MFS root under which the whole package is being built. */
  MFSNODE                  *pMFSRoot;
  /** \brief Open handle to the parent .fdoc file. */
  MFSFILEDESC              *pFDOC;
  /** \brief Zero-based document index. */
  uint32                    iDoc;
  /** \brief Zero-based page index. */
  uint32                    iPage;
} PageBuildContext;

/**
 * \brief Provides all of the contextual data needed to add a fixed document
 * to the package.
 *
 * \see startFixedDocumentBuild()
 * \see endFixedDocumentBuild()
 */
typedef struct _DocumentBuildContext
{
  /** \brief Description of the document, as provided by the package manager. */
  XPSDocumentDescription   *pDocument;
  /** \brief Accumlated list of context mappings. */
  URIContextMappingsList   *pContextMappings;
  /** \brief Represents the global [Content_Types].xml file. */
  ContentTypesManager      *pCTM;
  /** \brief The MFS root under which the whole package is being built. */
  MFSNODE                  *pMFSRoot;
  /** \brief Open handle to the parent .fdseq file. */
  MFSFILEDESC              *pFDSEQ;
  /** \brief Open handle to the .fdoc file for this document. */
  MFSFILEDESC              *pFDOC;
  /** \brief Open handle to the fixeddocument.rels file. */
  RelsFileManager           docRels;
  /** \brief Zero-based document index. */
  uint32                    iDoc;
  /** \brief Zero-based index of the next page to be built in the context
      of this document. */
  uint32                    iNextPage;
} DocumentBuildContext;

/**
 * \brief Provides all of the contextual data needed to create the
 * fixed document sequence within the package.
 *
 * \see startFixedDocumentSequenceBuild()
 * \see endFixedDocumentSequenceBuild()
 */
typedef struct _PackageBuildContext
{
  /** \brief Description of the package, as provided by the package manager. */
  XPSPackageDescription    *pPackage;
  /** \brief Accumlated list of context mappings. */
  URIContextMappingsList   *pContextMappings;
  /** \brief Represents the global [Content_Types].xml file. */
  ContentTypesManager       ctm;
  /** \brief Open handle to the "_rels/.rels" bootstrap file. */
  RelsFileManager           bootFile;
  /** \brief Open handle to the fixeddocumentsequence.rels file. */
  RelsFileManager           docSeqRels;
  /** \brief The MFS root under which the whole package is being built. */
  MFSNODE                  *pMFSRoot;
  /** \brief Pointer to the context for building the current document, or
      \c NULL if there is no current document. */
  DocumentBuildContext     *pCurrentDocBC;
  /** \brief Zero-based index of the next document to be built in this
      package. */
  uint32                    iNextDoc;
  /** \brief Open handle to the .fdseq file. */
  MFSFILEDESC              *pFDSEQ;
} PackageBuildContext;


/*
 * ******************************* Local Data ***********************************
 */


char *pszRelationshipTypes[] =
  {
    /* RelType_FixedRepresentation */
    "http://schemas.microsoft.com/xps/2005/06/fixedrepresentation",
    /* RelType_RequiredResource */
    "http://schemas.microsoft.com/xps/2005/06/required-resource",
    /* RelType_PrintTicket */
    "http://schemas.microsoft.com/xps/2005/06/printticket"
  };


/*
 * ***************************** Local Interface *********************************
 */


/**
 * \brief Write the standard XML header line to the given file.
 *
 * \param pFD MFS file descriptor open for writing.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 writeXMLHeader( MFSFILEDESC *pFD, int32 *pDevErr );

/**
 * \brief Write the standard XML header line and the correct XML opening
 * tag for XPS relationships.
 *
 * \param pRFM Management structure for the relationships file.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 openXMLRelationships( RelsFileManager *pRFM, int32 *pDevErr );

/**
 * \brief Add a relationship entry tag to the given rels file, and
 * increment the relationship ID.
 *
 * \param pRFM Management structure for the relationships file.
 *
 * \param relType One of <code>RelType_FixedRepresentation</code>,
 * <code>RelType_RequiredResource</code>, <code>RelType_PrintTicket</code>.
 *
 * \param pszTarget The relationship's target part name.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 writeXMLRelationship
  ( RelsFileManager *pRFM, uint32 relType, char *pszTarget, int32 *pDevErr );

/**
 * \brief Write the XML closing tag for a relationships file, and close
 * the file.
 *
 * <p>This function will always close the underlying file descriptor, even
 * if there is an error in writing the tag.
 *
 * \param pRFM Management structure for the relationships file. The function
 * will set the <code>pFD</code> member of this structure to <code>NULL</code>,
 * to ensure that the file descriptor is not used again.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 closeXMLRelationships( RelsFileManager *pRFM, int32 *pDevErr );

/**
 * \brief Write the XML header and opening tag for a
 * FixedDocumentSequence (.fdseq) file.
 *
 * \param pFD Open handle to the .fdseq file.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 openFixedDocumentSequence( MFSFILEDESC *pFD, int32 *pDevErr );

/**
 * \brief Write a DocumentReference element to the given
 * FixedDocumentSequence (.fdseq) file.
 *
 * \param pFD Open handle to the .fdseq file.
 *
 * \param pszUriDocument Part name of the .fdoc file being added.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 writeDocumentReference
  ( MFSFILEDESC *pFD, char *pszUriDocument, int32 *pDevErr );

/**
 * \brief Write the XML closing tag to the given FixedDocumentSequence
 * (.fdseq) file, and close the file.
 *
 * <p>This function will always close the underlying file descriptor, even
 * if there is an error in writing the tag.
 *
 * \param pFD Open handle to the .fdseq file.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 closeFixedDocumentSequence( MFSFILEDESC *pFD, int32 *pDevErr );

/**
 * \brief Write the XML header and opening tag for a FixedDocument
 * (.fdoc) file.
 *
 * \param pFD Open handle to the .fdoc file.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 openFixedDocument( MFSFILEDESC *pFD, int32 *pDevErr );

/**
 * \brief Write a PageContent element to the given FixedDocument
 * (.fdoc) file.
 *
 * \param pFD Open handle to the .fdoc file.
 *
 * \param pszUriPage Part name of the .fpage file being added. Note that the
 * .fpage file itself is not written to the package, but it is assumed to
 * exist externally.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 writePageReference
  ( MFSFILEDESC *pFD, char *pszUriPage, int32 *pDevErr );

/**
 * \brief Write the XML closing tag to the given FixedDocument (.fdoc)
 * file, and close the file.
 *
 * \param pFD Open handle to the .fdoc file.
 *
 * <p>This function will always close the underlying file descriptor, even
 * if there is an error in writing the tag.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 closeFixedDocument( MFSFILEDESC *pFD, int32 *pDevErr );

/**
 * \brief Write the XML header and opening tag for a
 * Content Types file.
 *
 * \param pCTM Structure managing the addition of Override entries, which
 * also contains a handle to the [Content_Types].xml file itself.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 openContentTypes( ContentTypesManager *pCTM, int32 *pDevErr );

/**
 * \brief Write an Override element into the given Content Types file.
 *
 * \param pCTM Structure managing the addition of Override entries, which
 * also contains a handle to the [Content_Types].xml file itself.
 *
 * \param pszPartName Part name whose content type is being specified. If
 * an override has already been written for this part name, the function
 * will do nothing (and return zero).
 *
 * \param pszType The content type corresponding to the given part name,
 * eg. <code>"application/vnd.ms-opentype"</code>.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 writeContentTypeOverride
  ( ContentTypesManager *pCTM, char *pszPartName, char *pszType, int32 *pDevErr );

/**
 * \brief Write the XML closing tag to the given Content Types file, and
 * close the file.
 *
 * <p>This function will always close the underlying file descriptor, even
 * if there is an error in writing the tag.
 *
 * \param pCTM Structure managing the addition of Override entries, which
 * also contains a handle to the [Content_Types].xml file itself.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return The number of bytes of data written to the file, or -1 if there
 * has been an error.
 */
static int32 closeContentTypes( ContentTypesManager *pCTM, int32 *pDevErr );

/**
 * \brief Build the appropriate URI for a part within the package.
 *
 * <p>By default, part URIs are constructed according to a fixed schema, which
 * is based on the recommended patterns as set out in the XPS specification. For
 * example, the FixedPage markup for Page 5 of Document 1 is always
 * <code>"/documents/1/pages/5.fpage"</code>. The default behaviour can
 * be overriden by supplying a pre-specified URI as the first argument.
 *
 * <p>The URI is written into caller-managed string memory, pointed to by
 * <code>pszURI</code>. Canonical lower case is used for all part names,
 * which makes for straightforward name matching in the XPS input device.
 *
 * \param pszPreSpecifiedUri If the caller already knows the part name,
 * then this argument points to its NUL-terminated string (whose memory
 * remains the property of the caller). This parameter may be \c NUL,
 * in which case the default URI pattern is adopted based on
 * \c iDoc and \c iPage. When using pre-specified URIs, the output
 * string is a canonical transformation of the input string. For
 * example, the pre-specified part name <code>"/FixedPage_0.xaml"</code>
 * would be canonicalized to <code>"/fixedpage_0.xaml"</code> as its
 * part name, and to <code>"/_rels/fixedpage_0.xaml.rels"</code> as
 * its relationships name.
 *
 * \param partType One of <code>PartType_FDSEQ</code>, <code>PartType_FDOC</code>,
 * <code>PartType_FPAGE</code>, <code>PartType_JobPT</code>,
 * <code>PartType_DocPT</code>, <code>PartType_PagePT</code>.
 *
 * \param iDoc Zero-based document index. This is ignored for parts that are not
 * in a per-document scope, such as <code>PartType_FDSEQ</code> or
 * <code>PartType_JobPT</code>. Note that URI formation is based on 1-based
 * indices, so the implementation will automatically increment this zero-based
 * index.
 *
 * \param iPage Zero-based page index within the scope of the document. This
 * is ignored for parts that are not in a per-page scope, such as
 * <code>PartType_FDOC</code> or <code>PartType_FDSEQ</code>. Note that
 * URI formation is based on 1-based indices, so the implementation will
 * automatically increment this zero-based index.
 *
 * \param fRels Indicates whether we want the base part name, or the
 * relationships part name. Pass <code>TRUE</code> to get the relationships
 * part name. For example, for <code>PartType_FDOC</code> and
 * <code>iDoc == 0</code>, the function (by default) constructs
 * <code>"/documents/1/fixeddocument.fdoc"</code> as the base part name,
 * but <code>"/documents/1/_rels/fixeddocument.fdoc.rels"</code> as
 * the relationships part name. If the part name is pre-specified, then
 * the corresponding relationships name is constructed by inserting
 * <code>"_rels/"</code> into the path before the leafname, and appending
 * <code>".rels"</code> to the end of the string.
 *
 * \param pszURI Receives the NUL-terminated partname. This is a pointer to
 * caller-managed memory, which must have space for at least
 * <code>LONGESTFILENAME</code> characters, including the NUL terminator.
 * If \c pszPreSpecifiedUri is supplied, then the output will simply be a
 * copy of this string (canonicalized to lower case), or a transformed
 * version of it when <code>fRels == TRUE</code>.
 */
static void makePartURI
  ( uint8 *pszPreSpecifiedUri,
    uint32 partType, uint32 iDoc, uint32 iPage, uint32 fRels, char *pszURI );

/**
 * \brief Utility function allowing \c makePartURI() to construct
 * relationship names from pre-specified part names.
 *
 * Inserts <code>"_rels/"</code> as an extra pathname element before
 * the leafname, and appends <code>".rels"</code> to the leafname.
 * For instance, the input <code>"/Documents/1/Pages/5.fpage"</code>
 * would produce the output <code>"/Documents/1/Pages/_rels/5.fpage.rels"</code>.
 *
 * \param[in] pszPartName The NUL-terminated input string.
 *
 * \param[out] pszRelsName Pointer to caller-managed string memory, which
 * receives the output. The buffer should have enough space for
 * \c LONGESTFILENAME characters.
 */
static void makeRelsNameFromPartName
  ( char *pszPartName, char *pszRelsName );

/**
 * \brief Copy the given context information into a freshly-allocated
 * <code>URIContextMapping</code> structure, and link it to the end of
 * the given list.
 *
 * \param pList The list of mappings, to which the new mapping element
 * will be added.
 *
 * \param pszURI Copied into the mapping structure.
 *
 * \param iDoc Copied into the mapping structure.
 *
 * \param iPage Copied into the mapping structure.
 *
 * \param resourceType Copied into the mapping structure.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return If the function succeeds, the return value is zero. If the function
 * fails, the return value is -1.
 */
static int32 addURIContextMapping
  ( URIContextMappingsList *pList, char *pszURI, uint32 iDoc, uint32 iPage,
    uint32 resourceType, int32 *pDevErr );

/**
 * \brief Add a new fixed page to the package (with explicit context).
 *
 * <p>The parent .fdoc file is assumed to already exist. This function will
 * add a new PageContent element to it, and leave the file open.
 *
 * <p>This function will also add the necessary required relationships and
 * content types for all resources used by the page. A .rels file for the
 * page will be created, populated and closed. The content types file may
 * be modified, and will be left open.
 *
 * See also \c addFixedPage(), which performs the same function, but
 * creates the \c PageBuildContext automatically from the current state
 * of the package.
 *
 * \param pContext Pointer to the contextual information needed to position
 * this page within the file tree.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return If the function succeeds, the return value is zero. If the function
 * fails, the return value is -1.
 */
static int32 buildFixedPage( PageBuildContext *pContext, int32 *pDevErr );

/**
 * \brief Begin a new Fixed Document context for the package.
 *
 * This function can be called an arbitrary number of times during
 * construction of a package. This matches the XPS specification, which
 * permits for any number of Fixed Documents in a single Fixed Document
 * Sequence. (However, it is quite typical for packages to contain just
 * one Fixed Document).
 *
 * The parent .fdseq file is assumed to already exist. This function will
 * add a new DocumentReference element to it, and leave the file open.
 * If there is a current .fdoc file open, this function will close it by
 * automatically calling \c endFixedDocumentBuild().
 *
 * A new .fdoc file will be created by this function, along with its
 * corresponding relationships file. If the document has a print ticket, a
 * print ticket relationship is written to the relationships file. The
 * relationships file will then be closed, while the .fdoc file itself
 * will remain open. Pages can then be added to the document by
 * calling \c addFixedPage().
 *
 * A content type Override element will be added for the .fdoc file and
 * its relationships part. The content type file itself is left open.
 *
 * \param pPC The build context.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \param pDoc Pointer to a description of the new document. Note that
 * any page descriptions nested in the document description are
 * \e not added automatically by this function.
 *
 * \return Zero on success; -1 on failure.
 */
static int32 startFixedDocumentBuild
  ( PackageBuildContext *pPC, int32 *pDevErr, XPSDocumentDescription *pDoc );

/**
 * \brief End the current Fixed Document context.
 *
 * This function writes the closing FixedDocument XML tag to the current
 * .fdoc file, and closes the file.
 *
 * It is not legal to call this function if there is no currently-open
 * Fixed Document. A prior document context must first have been created
 * by \c startFixedDocumentBuild(). Remember also that the XPS specification
 * does not allow a Fixed Document to be empty (that is, have zero pages).
 * This function <em>will not</em> enforce that, but the RIP will fail
 * to process the package later if it does not conform to the specification.
 *
 * \param pPC The build context.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return Zero on success; -1 on failure.
 */
static int32 endFixedDocumentBuild
  ( PackageBuildContext *pPC, int32 *pDevErr );

/**
 * \brief Begin the Fixed Document Sequence context for the package.
 *
 * This function can only be called once during construction of a
 * package. This matches the XPS specification, which permits for exactly
 * one Fixed Document Sequence in any one package.
 *
 * A new .fdseq file will be created, along with its corresponding
 * relationships file. If the document sequence has a print ticket, a
 * print ticket relationship is written to the relationships file. The
 * relationships file will then be closed, while the .fdseq file will
 * be left open. Documents can then be added to the sequence by calling
 * \c startFixedDocumentBuild(), and pages can subsequently be added
 * with \c addFixedPage().
 *
 * \param pContext The build context.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return Zero on success; -1 on failure.
 */
static int32 startFixedDocumentSequenceBuild
  ( PackageBuildContext *pContext, int32 *pDevErr );

/**
 * \brief End the Fixed Document Sequence context for the package.
 *
 * This function writes the closing FixedDocumentSequence XML tag to
 * the .fdseq file, and closes the file. Since a package contains only
 * on Fixed Document Sequence, this function should not be called until
 * the package is being finalized.
 *
 * This function will not automatically close any currently-open
 * Fixed Document. Call \c endFixedDocumentBuild() first.
 *
 * This function will not close the bootstrap (_rels/.rels) file, or
 * the content types file. These two files must be closed separately before
 * the package is finalized.
 *
 * \param pContext The build context.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \return Zero on success; -1 on failure.
 */
static int32 endFixedDocumentSequenceBuild
  ( PackageBuildContext *pContext, int32 *pDevErr );

/**
 * \brief Adds a Fixed Page in the context of the currently-open document.
 *
 * This function delegates to \c buildFixedPage(), having first established
 * the necessary PageBuildContext.
 *
 * It is illegal to call this function without a currently-open document
 * context, which must have been established by an earlier call to
 * \c startFixedDocumentBuild().
 *
 * \param pContext The build context.
 *
 * \param pDevErr Receives a standard core device error code if there
 * is a failure.
 *
 * \param pPage Pointer to a description of the page, and the resources
 * used by it.
 *
 * \return Zero on success; -1 on failure.
 */
static int32 addFixedPage
  ( PackageBuildContext *pContext, int32 *pDevErr, XPSPageDescription *pPage );



/*
 * ******************************* Local Data **********************************
 */


/**
 * \brief Standard fixed part name for the initial boot file in an XPS package.
 */
static char *pszBootstrapFile = "/_rels/.rels";

/**
 * \brief Standard fixed part name for the global content types file.
 */
static char *pszContentTypesFile = "/[Content_Types].xml";

/**
 * \brief Standard XML header line, assuming UTF-8 encoding.
 */
static char *pszXMLHeader = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n";


/*
 * ******************************* Local Impl **********************************
 */


static int32 writeXMLHeader( MFSFILEDESC *pFD, int32 *pDevErr )
{
  return MFSWriteString( pFD, pszXMLHeader, pDevErr );
}

static int32 openXMLRelationships( RelsFileManager *pRFM, int32 *pDevErr )
{
  int32 result = writeXMLHeader( pRFM->pFD, pDevErr );

  if ( result != -1 )
  {
    result = MFSWriteString
      (
        pRFM->pFD,
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n",
        pDevErr
      );
  }

  pRFM->id = 0;
  return result;
}

static int32 writeXMLRelationship
  ( RelsFileManager *pRFM, uint32 relType, char *pszTarget, int32 *pDevErr )
{
  char szXML[ XML_LINEBUFFER_SIZE ] = "\0";

  HQASSERT(relType <= RelType_PrintTicket , "Unknown relationship type");

  sprintf
    (
      szXML,
      "  <Relationship Target=\"%s\" Id=\"R%d\" Type=\"%s\"/>\n",
      pszTarget,
      pRFM->id,
      pszRelationshipTypes[ relType ]
    );

  pRFM->id++;

  return MFSWriteString( pRFM->pFD, szXML, pDevErr );
}

static int32 closeXMLRelationships( RelsFileManager *pRFM, int32 *pDevErr )
{
  int32 result =  MFSWriteString( pRFM->pFD, "</Relationships>\n", pDevErr );

  (void) MFSClose( pRFM->pFD ); /* Always close the file. */
  pRFM->pFD = NULL;

  return result;
}

static int32 openFixedDocumentSequence( MFSFILEDESC *pFD, int32 *pDevErr )
{
  int32 result = writeXMLHeader( pFD, pDevErr );

  if ( result != -1 )
  {
    result = MFSWriteString
      (
        pFD,
        "<FixedDocumentSequence xmlns=\"http://schemas.microsoft.com/xps/2005/06\">\n",
        pDevErr
      );
  }

  return result;
}

static int32 writeDocumentReference
  ( MFSFILEDESC *pFD, char *pszUriDocument, int32 *pDevErr )
{
  char szXML[ XML_LINEBUFFER_SIZE ] = "\0";

  sprintf
    (
      szXML,
      "  <DocumentReference Source=\"%s\"/>\n",
      pszUriDocument
    );

  return MFSWriteString( pFD, szXML, pDevErr );
}

static int32 closeFixedDocumentSequence( MFSFILEDESC *pFD, int32 *pDevErr )
{
  int32 result =  MFSWriteString( pFD, "</FixedDocumentSequence>\n", pDevErr );

  (void) MFSClose( pFD ); /* Always close the file. */

  return result;
}

static int32 openFixedDocument( MFSFILEDESC *pFD, int32 *pDevErr )
{
  int32 result = writeXMLHeader( pFD, pDevErr );

  if ( result != -1 )
  {
    result = MFSWriteString
      (
        pFD,
        "<FixedDocument xmlns=\"http://schemas.microsoft.com/xps/2005/06\">\n",
        pDevErr
      );
  }

  return result;
}

static int32 writePageReference
  ( MFSFILEDESC *pFD, char *pszUriPage, int32 *pDevErr )
{
  char szXML[ XML_LINEBUFFER_SIZE ] = "\0";

  sprintf
    (
      szXML,
      "  <PageContent Source=\"%s\"/>\n",
      pszUriPage
    );

  return MFSWriteString( pFD, szXML, pDevErr );
}

static int32 closeFixedDocument( MFSFILEDESC *pFD, int32 *pDevErr )
{
  int32 result =  MFSWriteString( pFD, "</FixedDocument>\n", pDevErr );

  (void) MFSClose( pFD ); /* Always close the file. */

  return result;
}

static int32 openContentTypes( ContentTypesManager *pCTM, int32 *pDevErr )
{
  int32 result = writeXMLHeader( pCTM->pFD, pDevErr );

  if ( result != -1 )
  {
    result = MFSWriteString
      (
        pCTM->pFD,
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "  <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "  <Default Extension=\"fdseq\" ContentType=\"application/vnd.ms-package.xps-fixeddocumentsequence+xml\"/>\n"
        "  <Default Extension=\"fdoc\" ContentType=\"application/vnd.ms-package.xps-fixeddocument+xml\"/>\n"
        "  <Default Extension=\"fpage\" ContentType=\"application/vnd.ms-package.xps-fixedpage+xml\"/>\n"
        "  <Default Extension=\"xml\" ContentType=\"application/vnd.ms-printing.printticket+xml\"/>\n",
        pDevErr
      );
  }

  return result;
}

static int32 writeContentTypeOverride
  ( ContentTypesManager *pCTM, char *pszPartName, char *pszType, int32 *pDevErr )
{
  PartNameList *pEntry = pCTM->pPartNames;
  char szXML[ XML_LINEBUFFER_SIZE ] = "\0";

  while ( pEntry != NULL )
  {
    if ( !strcmp( pszPartName, pEntry->szPartName ) )
    {
      /* An entry has already been written for this part name. */
      return 0;
    }
    else
    {
      pEntry = pEntry->pNext;
    }
  }

  /* No entry has been written, so prepare one. */
  pEntry = (PartNameList*) MemAlloc( sizeof( PartNameList ), TRUE, FALSE );

  if ( pEntry == NULL )
  {
    (*pDevErr) = DeviceVMError;
    return -1;
  }

  strcpy( pEntry->szPartName, pszPartName );

  /* Link to head of the partNames list */
  pEntry->pNext = pCTM->pPartNames;
  pCTM->pPartNames = pEntry;

  sprintf
    (
      szXML,
      "  <Override PartName=\"%s\" ContentType=\"%s\"/>\n",
      pszPartName,
      pszType
    );

  return MFSWriteString( pCTM->pFD, szXML, pDevErr );
}

static int32 closeContentTypes( ContentTypesManager *pCTM, int32 *pDevErr )
{
  int32 result =  MFSWriteString( pCTM->pFD, "</Types>\n", pDevErr );
  PartNameList *pEntry;

  (void) MFSClose( pCTM->pFD ); /* Always close the file. */

  /* Free the list of part names. */
  pEntry = pCTM->pPartNames;
  while ( pEntry != NULL )
  {
    PartNameList *pDead = pEntry;
    pEntry = pEntry->pNext;
    MemFree( (void*) pDead );
  }

  return result;
}

static void makeRelsNameFromPartName
  ( char *pszPartName, char *pszRelsName )
{
  char  szTmp[ LONGESTFILENAME ] = { 0 };
  char  *pszLeafname = NULL;
  int    i = 0;
  int    iLastSlash = 0;

  strcpy( szTmp, pszPartName );

  while ( szTmp[ i ] != '\0' )
  {
    if ( szTmp[ i ] == '/' )
    {
      pszLeafname = pszPartName + i + 1;
      iLastSlash = i;
    }

    i++;
  }

  szTmp[ iLastSlash + 1 ] = '\0';

  sprintf( pszRelsName, "%s_rels/%s.rels", szTmp, pszLeafname );
}

static void makePartURI
  ( uint8 *pszPreSpecifiedUri,
    uint32 partType, uint32 iDoc, uint32 iPage, uint32 fRels, char *pszURI )
{
  if ( pszPreSpecifiedUri != NULL )
  {
    /* A part name has been pre-specified, so just canonicalize it to lower
       case, having first transformed it into its corresponding relationships
       name if necessary. */

    int uriLen, i;

    if ( fRels )
    {
      makeRelsNameFromPartName( (char*) pszPreSpecifiedUri, pszURI );
    }
    else
    {
      strcpy( pszURI, (char*) pszPreSpecifiedUri );
    }

    uriLen = (int) strlen( pszURI );

    for ( i = 0; i < uriLen; i++ )
    {
      pszURI[ i ] = (char) tolower( (int) pszURI[ i ] );
    }
  }
  else
  {
    /* No pre-specified part name. Build a fresh URI according to the
     * conventions of the XPS spec. */

    /* Convert between zero-based indices used throughout the API, and
       one-based indices used in part names. */
    iDoc++;
    iPage++;

    switch ( partType )
    {
      case PartType_FDSEQ:
        if ( fRels )
        {
          sprintf( pszURI, "/_rels/fixeddocumentsequence.fdseq.rels" );
        }
        else
        {
          sprintf( pszURI, "/fixeddocumentsequence.fdseq" );
        }
        break;

      case PartType_FDOC:
        if ( fRels )
        {
          sprintf( pszURI, "/documents/%d/_rels/fixeddocument.fdoc.rels", iDoc );
        }
        else
        {
          sprintf( pszURI, "/documents/%d/fixeddocument.fdoc", iDoc );
        }
        break;

      case PartType_FPAGE:
        if ( fRels )
        {
          sprintf( pszURI, "/documents/%d/pages/_rels/%d.fpage.rels", iDoc, iPage );
        }
        else
        {
          sprintf( pszURI, "/documents/%d/pages/%d.fpage", iDoc, iPage );
        }
        break;

      case PartType_JobPT:
        /* No such thing as relationships from a PT. */
        HQASSERT(!fRels , "Relationships exist for job part type");
        sprintf( pszURI, "/metadata/job_pt.xml" );
        break;

      case PartType_DocPT:
        /* No such thing as relationships from a PT. */
        HQASSERT(!fRels, "Relationships exist for document part type");
        sprintf( pszURI, "/documents/metadata/doc%d_pt.xml", iDoc );
        break;

      case PartType_PagePT:
        /* No such thing as relationships from a PT. */
        HQASSERT(!fRels, "Relationships exist for page part type");
        sprintf( pszURI, "/documents/%d/metadata/page%d_pt.xml", iDoc, iPage );
        break;

      default:
        /* Don't know how to build a URI for this part type. */
        HQFAIL("Unrecognised part type");
        break;
    }
  }
}

static int32 addURIContextMapping
  ( URIContextMappingsList *pList, char *pszURI, uint32 iDoc, uint32 iPage,
    uint32 resourceType, int32 *pDevErr )
{
  URIContextMapping *pNew = (URIContextMapping*) MemAlloc
    ( sizeof( URIContextMapping ), TRUE, FALSE );

  if ( pNew == NULL )
  {
    (*pDevErr) = DeviceVMError;
    return -1;
  }

  strcpy( (char*) pNew->szURI, pszURI );
  pNew->iDocument = iDoc;
  pNew->iPage = iPage;
  pNew->resourceType = resourceType;
  /* pNew->pNext already NULL, since zeroed when allocated. */

  if ( pList->pFirst == NULL )
  {
    /* First entry. */
    pList->pFirst = pList->pLast = pNew;
  }
  else
  {
    /* Subsequent entry. Add to end of list, and update pLast */
    pList->pLast->pNext = pNew;
    pList->pLast = pNew;
  }

  return 0;
}

static int32 buildFixedPage( PageBuildContext *pContext, int32 *pDevErr )
{
  char            szPageUri[ LONGESTFILENAME ] = { 0 };
  char            szPageRelsUri[ LONGESTFILENAME ] = { 0 };
  RelsFileManager pageRels = { NULL, 0 };
  int32           result = 0;

  makePartURI
    ( pContext->pPage->pszFixedPageUri,
      PartType_FPAGE, pContext->iDoc, pContext->iPage, FALSE, szPageUri );

  makePartURI
    ( pContext->pPage->pszFixedPageUri,
      PartType_FPAGE, pContext->iDoc, pContext->iPage, TRUE, szPageRelsUri );

  /* Create the relationships file for the page. */
  if ( !MFSOpen( pContext->pMFSRoot, szPageRelsUri, SW_CREAT, &pageRels.pFD, pDevErr ) )
  {
    return -1;
  }

  result = addURIContextMapping( pContext->pContextMappings, szPageUri,
                                 pContext->iDoc, pContext->iPage,
                                 ResourceType_Page_Markup, pDevErr );

  if ( result > -1 )
  {
    result = openXMLRelationships( &pageRels, pDevErr );
  }

  if ( result > -1 )
  {
    result = writeContentTypeOverride
      (
        pContext->pCTM,
        szPageUri,
        "application/vnd.ms-package.xps-fixedpage+xml",
        pDevErr
      );
  }

  if ( result > -1 )
  {
    result = writeContentTypeOverride
      (
        pContext->pCTM,
        szPageRelsUri,
        "application/vnd.openxmlformats-package.relationships+xml",
        pDevErr
      );
  }

  /* Relate the page to each of its required resources, and add their
     content types to the global file. */
  if ( result > -1 )
  {
    XPSContentTypeMap *pMap = pContext->pPage->pContentTypeMap;
    uint32 i;
    for ( i = 0; i < pMap->nEntries && result > -1; i++ )
    {
      result = writeXMLRelationship
        ( &pageRels, RelType_RequiredResource,
          (char*) pMap->pEntries[ i ].pszResourceUri,
          pDevErr );

      if ( result > -1 )
      {
        result = writeContentTypeOverride
          ( pContext->pCTM,
            (char*) pMap->pEntries[ i ].pszResourceUri,
            (char*) pMap->pEntries[ i ].pszContentType,
            pDevErr );
      }
    }
  }

  /* Write the <PageContent> tag to the parent .fdoc file */
  if ( result > -1 )
  {
    result = writePageReference( pContext->pFDOC, szPageUri, pDevErr );
  }

  /* If there is a PT associated with this page, add its relationship
     and context mapping. */
  if ( result > -1 && pContext->pPage->fHasPrintTicket )
  {
    char       szPTURI[ LONGESTFILENAME ] = { 0 };

    makePartURI
      ( pContext->pPage->pszPrintTicketUri,
        PartType_PagePT, pContext->iDoc, pContext->iPage, FALSE, szPTURI );

    result = addURIContextMapping( pContext->pContextMappings, szPTURI,
                                   pContext->iDoc, pContext->iPage,
                                   ResourceType_Page_PT, pDevErr );

    if ( result > -1 )
    {
      result = writeXMLRelationship( &pageRels, RelType_PrintTicket, szPTURI,
                                     pDevErr );
    }

    if ( result > 1 )
    {
      result = writeContentTypeOverride( pContext->pCTM,
                                         szPTURI,
                                         "application/vnd.ms-printing.printticket+xml",
                                         pDevErr );
    }
  }

  /* Close the fdoc.rels file early, because we only write a Print Ticket
     relationship (if necessary). There is no need to keep the relationships
     open for as long as the .fdoc file. */
  result = closeXMLRelationships( &pageRels, pDevErr );

  return result;
}

static int32 startFixedDocumentBuild
  ( PackageBuildContext *pPC, int32 *pDevErr, XPSDocumentDescription *pDoc )
{
  char            szDocUri[ LONGESTFILENAME ] = { 0 };
  char            szDocRelsUri[ LONGESTFILENAME ] = { 0 };
  int32           result = 0;

  DocumentBuildContext *pContext = pPC->pCurrentDocBC;

  if ( pContext != NULL )
  {
    /* Starting a new FixedDoc, so close off the current one. */
    if ( endFixedDocumentBuild( pPC, pDevErr ) < 0 )
      return -1;
  }

  /* Now make the context for building the new document. */
  pContext = (DocumentBuildContext*) MemAlloc
    ( sizeof( DocumentBuildContext ), TRUE, FALSE );

  if ( pContext == NULL )
  {
    (*pDevErr) = DeviceVMError;
    return -1;
  }

  pContext->pDocument = pDoc;
  pContext->pContextMappings = pPC->pContextMappings;
  pContext->pCTM = &pPC->ctm;
  pContext->pMFSRoot = pPC->pMFSRoot;
  pContext->pFDSEQ = pPC->pFDSEQ;
  pContext->iDoc = pPC->iNextDoc;

  pPC->pCurrentDocBC = pContext;

  makePartURI
    ( pDoc->pszFixedDocumentUri,
      PartType_FDOC, pContext->iDoc, 0, FALSE, szDocUri );

  makePartURI
    ( pDoc->pszFixedDocumentUri,
      PartType_FDOC, pContext->iDoc, 0, TRUE, szDocRelsUri );

  /* Create the relationships file for the doc. */
  if ( !MFSOpen( pContext->pMFSRoot, szDocRelsUri, SW_CREAT, &pContext->docRels.pFD, pDevErr ) )
  {
    return -1;
  }

  /* Create the .fdoc file itself */
  if ( !MFSOpen( pContext->pMFSRoot, szDocUri, SW_CREAT, &pContext->pFDOC, pDevErr ) )
  {
    result = -1;
  }

  if ( result > -1 )
  {
    result = writeContentTypeOverride
      (
        pContext->pCTM,
        szDocUri,
        "application/vnd.ms-package.xps-fixeddocument+xml",
        pDevErr
      );
  }

  if ( result > -1 )
  {
    result = writeContentTypeOverride
      (
        pContext->pCTM,
        szDocRelsUri,
        "application/vnd.openxmlformats-package.relationships+xml",
        pDevErr
      );
  }

  if ( result > -1 )
  {
    result = openXMLRelationships( &pContext->docRels, pDevErr );
  }

  if ( result > -1 )
  {
    result = openFixedDocument( pContext->pFDOC, pDevErr );
  }

  /* Write the <DocumentReference> tag to the parent .fdseq file */
  if ( result > -1 )
  {
    result = writeDocumentReference( pContext->pFDSEQ, szDocUri, pDevErr );
  }

  /* If there is a PT associated with this document, add its relationship
     and context mapping. */
  if ( result > -1 && pContext->pDocument->fHasPrintTicket )
  {
    char       szPTURI[ LONGESTFILENAME ] = { 0 };

    makePartURI
      ( pDoc->pszPrintTicketUri,
        PartType_DocPT, pContext->iDoc, 0, FALSE, szPTURI );

    result = addURIContextMapping( pContext->pContextMappings, szPTURI,
                                   pContext->iDoc, 0,
                                   ResourceType_Document_PT, pDevErr );

    if ( result > -1 )
    {
      result = writeXMLRelationship( &pContext->docRels, RelType_PrintTicket, szPTURI,
                                     pDevErr );
    }

    if ( result > -1 )
    {
      result = writeContentTypeOverride( pContext->pCTM,
                                         szPTURI,
                                         "application/vnd.ms-printing.printticket+xml",
                                         pDevErr );
    }
  }

  if ( result > -1 )
    result = closeXMLRelationships( &pContext->docRels, pDevErr );

  return result;
}

static int32 endFixedDocumentBuild
  ( PackageBuildContext *pPC, int32 *pDevErr )
{
  DocumentBuildContext *pContext = pPC->pCurrentDocBC;
  int32 result = 0;

  if ( pContext != NULL )
  {
    /* Always close the .fdoc file, even if there has been an error. */
    if ( pContext->pFDOC != NULL )
      result = closeFixedDocument( pContext->pFDOC, pDevErr );

    MemFree( (void*) pContext );

    pPC->pCurrentDocBC = NULL;
    pPC->iNextDoc++;
  }

  return result;
}

static int32 addFixedPage
  ( PackageBuildContext *pPC, int32 *pDevErr, XPSPageDescription *pPage )
{
  DocumentBuildContext *pContext = pPC->pCurrentDocBC;

  if ( pContext != NULL )
  {
    PageBuildContext    pbc = { 0 };

    /* Populate the context for page building. */
    pbc.pPage = pPage;
    pbc.pContextMappings = pContext->pContextMappings;
    pbc.pCTM = pContext->pCTM;
    pbc.pMFSRoot = pContext->pMFSRoot;
    pbc.pFDOC = pContext->pFDOC;
    pbc.iDoc = pContext->iDoc;
    pbc.iPage = pContext->iNextPage;

    pContext->iNextPage++;

    /* Do the page build. */
    return buildFixedPage( &pbc, pDevErr );
  }
  else
  {
    /* No context in which to add a page. */
    return -1;
  }
}

static int32 startFixedDocumentSequenceBuild
  ( PackageBuildContext *pContext, int32 *pDevErr )
{
  char            szDocSeqUri[ LONGESTFILENAME ] = { 0 };
  char            szDocSeqRelsUri[ LONGESTFILENAME ] = { 0 };
  int32           result = 0;

  makePartURI
    ( pContext->pPackage->pszFixedDocumentSequenceUri,
      PartType_FDSEQ, 0, 0, FALSE, szDocSeqUri );

  makePartURI
    ( pContext->pPackage->pszFixedDocumentSequenceUri,
      PartType_FDSEQ, 0, 0, TRUE, szDocSeqRelsUri );

  /* Create the relationships file for the document sequence. */
  if ( !MFSOpen( pContext->pMFSRoot, szDocSeqRelsUri, SW_CREAT, &pContext->docSeqRels.pFD, pDevErr ) )
  {
    return -1;
  }

  /* Create the .fdseq file itself */
  if ( !MFSOpen( pContext->pMFSRoot, szDocSeqUri, SW_CREAT, &pContext->pFDSEQ, pDevErr ) )
  {
    result = -1;
  }

  if ( result > -1 )
  {
    result = writeContentTypeOverride
      (
        &pContext->ctm,
        szDocSeqUri,
        "application/vnd.ms-package.xps-fixeddocumentsequence+xml",
        pDevErr
      );
  }

  if ( result > -1 )
  {
    result = writeContentTypeOverride
      (
        &pContext->ctm,
        szDocSeqRelsUri,
        "application/vnd.openxmlformats-package.relationships+xml",
        pDevErr
      );
  }

  if ( result > -1 )
  {
    result = openXMLRelationships( &pContext->docSeqRels, pDevErr );
  }

  if ( result > -1 )
  {
    result = openFixedDocumentSequence( pContext->pFDSEQ, pDevErr );
  }

  /* Relate the bootstrap file to the .fdseq file. */
  if ( result > -1 )
  {
    result = writeXMLRelationship
      ( &pContext->bootFile, RelType_FixedRepresentation, szDocSeqUri, pDevErr );
  }

  /* If there is a PT associated with this job, add its relationship
     and context mapping. */
  if ( result > -1 && pContext->pPackage->fHasPrintTicket )
  {
    char       szPTURI[ LONGESTFILENAME ] = { 0 };

    makePartURI
      ( pContext->pPackage->pszPrintTicketUri,
        PartType_JobPT, 0, 0, FALSE, szPTURI );

    result = addURIContextMapping( pContext->pContextMappings, szPTURI,
                                   0, 0,
                                   ResourceType_Job_PT, pDevErr );

    if ( result > -1 )
    {
      result = writeXMLRelationship( &pContext->docSeqRels, RelType_PrintTicket, szPTURI,
                                     pDevErr );
    }

    if ( result > -1 )
    {
      result = writeContentTypeOverride( &pContext->ctm,
                                         szPTURI,
                                         "application/vnd.ms-printing.printticket+xml",
                                         pDevErr );
    }
  }

  /* Close the fdseq.rels file early, because we only write a Print Ticket
     relationship (if necessary). There is no need to keep the relationships
     open for as long as the .fdseq file. */
  if ( result > -1 )
    result = closeXMLRelationships( &pContext->docSeqRels, pDevErr );

  return result;
}

static int32 endFixedDocumentSequenceBuild
  ( PackageBuildContext *pContext, int32 *pDevErr )
{
  int32 result = 0;

  /* Always close the .fdseq file, even if there has been an error. */
  if ( pContext->pFDSEQ != NULL )
    closeFixedDocumentSequence( pContext->pFDSEQ, pDevErr );

  return result;
}



/*
 * ****************************** Exported API *********************************
 * ******************** (See xpsbuild.h for documentation) *********************
 */


URIContextMapping *lookupContextMapping
  ( URIContextMappingsList *pMappings, uint8 *pszPartName )
{
  URIContextMapping *pEntry = pMappings->pFirst;

  while ( pEntry != NULL )
  {
    if ( !strcmp( (char*) pEntry->szURI, (char*) pszPartName ) )
    {
      return pEntry;
    }
    else
    {
      pEntry = pEntry->pNext;
    }
  }

  return NULL;
}

void *buildXPSPackageInMemory
  ( XPSPackageDescription *pXPD, URIContextMappingsList *pMappings, int32 *pDevErr )
{
  PackageBuildContext   *pPBC = NULL;
  MFSNODE               *pMFSRoot = NULL;
  int32                  result = 0;
  uint32                 iDoc = 0;
  uint32                 iPage = 0;

  /* Allocate a fresh package build context structure, which will become the
     opaque handle passed back to the caller. */
  pPBC = (PackageBuildContext*) MemAlloc
    ( sizeof( PackageBuildContext ), TRUE, FALSE );

  /* Abort immediately if malloc fails. */
  if ( pPBC == NULL )
  {
    *pDevErr = DeviceVMError;
    return NULL;
  }

  /* Construct a fresh MFS filesystem tree */
  pMFSRoot = MFSNewRoot( "XPS" );

  /* Abort immediately if there's no root to build on. */
  if ( pMFSRoot == NULL )
  {
    *pDevErr = DeviceVMError;
    return NULL;
  }

  /* Create the global boot ("_rels/.rels") and types ("[Content_Types].xml")
     files. */
  if ( !MFSOpen( pMFSRoot, pszBootstrapFile, SW_CREAT, &pPBC->bootFile.pFD, pDevErr ) )
  {
    result = -1;
  }

  if ( result > -1 )
  {
    if ( !MFSOpen( pMFSRoot, pszContentTypesFile, SW_CREAT, &pPBC->ctm.pFD, pDevErr ) )
    {
      result = -1;
    }
  }

  if ( result > - 1 )
    result = openContentTypes( &pPBC->ctm, pDevErr );

  if ( result > -1 )
    result = openXMLRelationships( &pPBC->bootFile, pDevErr );

  /* Build all of the pre-described parts. */
  if ( result > -1 )
  {
    /* Set up context for building the FDSEQ file. */
    pPBC->pPackage = pXPD;
    pPBC->pContextMappings = pMappings;
    pPBC->pMFSRoot = pMFSRoot;

    /* Build the FDSEQ, and the tree of documents and pages beneath it. */
    result = startFixedDocumentSequenceBuild( pPBC, pDevErr );

    for ( iDoc = 0; iDoc < pXPD->nDocuments && result > -1; iDoc++ )
    {
      XPSDocumentDescription *pDoc = pXPD->pDocumentDescriptions + iDoc;
      result = startFixedDocumentBuild( pPBC, pDevErr, pDoc );
      for ( iPage = 0; iPage < pDoc->nPages && result > -1; iPage++ )
      {
        XPSPageDescription *pPage = pDoc->pPageDescriptions + iPage;
        result = addFixedPage( pPBC, pDevErr, pPage );
      }
    }
  }

  if ( result == -1 )
  {
    freeXPSPackageInMemory( (void*) pPBC, pMappings );
    return NULL;
  }
  else
  {
    return (void*) pPBC;
  }
}

MFSNODE *getXPSPackageRoot( void *pContext )
{
  PackageBuildContext  *pPBC = (PackageBuildContext*) pContext;
  if ( pPBC == NULL )
    return NULL;
  else
    return pPBC->pMFSRoot;
}

int32 addFixedPageToXPSPackage
  ( void *pContext, XPSPageDescription *pPage, int32 *pDevErr )
{
  PackageBuildContext *pPBC = (PackageBuildContext*) pContext;
  if ( pContext != NULL )
  {
    return addFixedPage( pPBC, pDevErr, pPage );
  }
  else
  {
    return -1;
  }
}

int32 addFixedDocumentToXPSPackage
  ( void *pContext, XPSDocumentDescription *pDocument, int32 *pDevErr )
{
  PackageBuildContext *pPBC = (PackageBuildContext*) pContext;
  if ( pContext != NULL )
  {
    int32 result = startFixedDocumentBuild( pPBC, pDevErr, pDocument );
    uint32 iPage;
    /* Add the pre-described pages within the document, although it is
       possible to add more with addFixedPage(). */
    for ( iPage = 0; iPage < pDocument->nPages && result > -1; iPage++ )
    {
      XPSPageDescription *pPage = pDocument->pPageDescriptions + iPage;
      result = addFixedPage( pPBC, pDevErr, pPage );
    }
    return result;
  }
  else
  {
    return -1;
  }
}

void commitXPSPackageInMemory( void *pContext, int32 *pDevErr )
{
  PackageBuildContext *pPBC = (PackageBuildContext*) pContext;

  if ( pContext != NULL )
  {
    /* End any current document. */
    endFixedDocumentBuild( pPBC, pDevErr );

    /* End the doc sequence. */
    endFixedDocumentSequenceBuild( pPBC, pDevErr );

    /* Close the global streams and their managers. */
    if ( pPBC->ctm.pFD != NULL )
      closeContentTypes( &pPBC->ctm, pDevErr );

    if ( pPBC->bootFile.pFD != NULL )
      closeXMLRelationships( &pPBC->bootFile, pDevErr );
  }
}

void freeXPSPackageInMemory
  ( void *pContext, URIContextMappingsList *pMappings )
{
  PackageBuildContext *pPBC = (PackageBuildContext*) pContext;
  URIContextMapping *pMapping = pMappings->pFirst;

  /* Free all URI->context mappings */
  while ( pMapping != NULL )
  {
    URIContextMapping *pDead = pMapping;
    pMapping = pMapping->pNext;
    MemFree( (void*) pDead );
  }

  /* And don't leave dangling pointers. */
  pMappings->pFirst = pMappings->pLast = NULL;

  if ( pPBC != NULL )
  {
    /* Free any MFS (ramdev) resources used by the building of the package. */
    if ( pPBC->pMFSRoot != NULL )
      MFSReleaseRoot( pPBC->pMFSRoot );

    /* Finally, free the package build context structure itself. */
    MemFree( (void*) pPBC );
  }
}


