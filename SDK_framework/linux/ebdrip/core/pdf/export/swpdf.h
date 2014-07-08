/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!export:swpdf.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions of PDF contexts and external query functions.
 */

#ifndef __SWPDF_H__
#define __SWPDF_H__

#include "objects.h"
#include "fileio.h"
#include "graphics.h"
#include "stacks.h"
#include "mm.h"
#include "mps.h"

struct core_init_fns ; /* from SWcore */

/**
 * \defgroup pdf PDF input and output.
 * \ingroup core
 * \{
 */

/* ----- External constants ----- */

/** Number of xref cache lines. */

#define XREF_CACHE_SIZE 256

/* Entries in resource cache. */

#define RES_CACHE_NA        -1
#define RES_DEFAULTGRAY     0
#define RES_DEFAULTRGB      1
#define RES_DEFAULTCMYK     2
#define RES_N_CACHE_ENTRIES 3

/** Maximum depth of recursion for pdf_objectsequal,
 * pdf_resolvexrefs, pdf_copyobject and pdf_freeobject.
 */
#define PDF_MAX_RECURSION_DEPTH 32

/** Possible values for \c XREFOBJ->objuse. */
enum { XREF_Uninitialised = 'X' , XREF_Free = 'f' , XREF_Used = 'n',
       XREF_Compressed = 'c' } ;

/* ----- Forward structures ----- */
typedef struct PDFXCONTEXT     PDFXCONTEXT;
typedef struct PDFCONTEXT      PDFCONTEXT;
typedef struct pdf_imc_params  PDF_IMC_PARAMS;
typedef struct pdf_ixc_params  PDF_IXC_PARAMS;
typedef struct pdf_omc_params  PDF_OMC_PARAMS;
typedef struct pdf_oxc_params  PDF_OXC_PARAMS;
typedef struct pdf_crypto_info PDF_CRYPTO_INFO;
/* ----- External structures ----- */

typedef struct PDF_DICTLIST {
  struct PDF_DICTLIST *next ;
  OBJECT *dict ;
  PDFCONTEXT *imc ;
} PDF_DICTLIST;

typedef struct PDF_RESOURCE_CACHE {
  OBJECT *resource ;
  uint8 valid ;
} PDF_RESOURCE_CACHE ;


/* for free xref objects */
typedef struct XREFOBJ_FREE {
  int32  objnum ;          /* objnum of next free object */
  int32  blank;            /* spacer */
  uint16 objgen ;          /* Generation number of this object. */
} XREFOBJ_FREE ;

/* for used xref objects */
typedef struct XREFOBJ_USED {
  Hq32x2 offset ;           /* Byte offset for where this object exists. */
  uint16 objgen ;           /* Generation number of this object. */
} XREFOBJ_USED ;

/* for compressed xref objects */
typedef struct XREFOBJ_COMPRESSED {
  int32  objnum ;          /* objnum of next obj stream */
  int32  blank;            /* spacer */
  uint16 sindex ;          /* compressed stream index */
} XREFOBJ_COMPRESSED ;

typedef struct XREFOBJ {
  union {
    XREFOBJ_FREE f;
    XREFOBJ_USED n;
    XREFOBJ_COMPRESSED c;
  }d;
  uint8  objuse ;
  uint8  waste ;
} XREFOBJ ;

typedef struct PDF_FILERESTORE_LIST {
  Hq32x2 position ;
  OBJECT fileobj ;
  struct PDF_FILERESTORE_LIST *next ;
} PDF_FILERESTORE_LIST ;

/* This structure was introduced for PDF 1.5 to gather information for
   xref and compressed object streams. */
typedef struct PDF_STREAM_INFO {
  /* xref stream info */
  int32     size;
  Hq32x2    prev;
  int32     Wsize[3];
  OBJECT *  index;
} PDF_STREAM_INFO ;

typedef struct PDF_METHODS {
  Bool ( *begin_execution_context )( PDFXCONTEXT *pdfxc ) ;
  Bool ( *end_execution_context )( PDFXCONTEXT *pdfxc ) ;
  Bool ( *purge_execution_context ) ( PDFXCONTEXT *pdfxc, int32 savelevel ) ;

  Bool ( *begin_marking_context )( PDFXCONTEXT *pdfxc , PDFCONTEXT *pdfc ) ;
  Bool ( *end_marking_context )( PDFCONTEXT *pdfc ) ;
  Bool ( *init_marking_context )( PDF_IMC_PARAMS *imc, void *args ) ;
  void  *init_mc_arg; /* Ptr to argument data for 'init_marking_context' callback func. */

  Bool (*get_xref_object)(PDFCONTEXT *pdfc, FILELIST *flptr, OBJECT *pdfobj, PDF_STREAM_INFO *info,
                          Bool streamDictOnly, int8 *streamDict) ;
  Bool (*get_xref_streamobj)(PDFCONTEXT *pdfc, FILELIST *flptr, OBJECT *pdfobj) ;
  Bool (*seek_to_compressedxrefobj)( PDFCONTEXT *pdfc,FILELIST **pflptr,
                                     XREFOBJ *xrefobj, int32 expectedobjnum);
  mps_res_t (*scan_context)( mps_ss_t ss, PDFXCONTEXT *pdfxc );
  mps_res_t (*scan_marking_context)( mps_ss_t ss, PDFCONTEXT *pdfc );
} PDF_METHODS ;

/**
 * Document information extracted from document-level metadata stream.
 */
typedef struct {
  OBJECT id;
  OBJECT version;
  OBJECT renditionClass;
} DocumentMetadata;

/** The PDF execution context - contains all data which is independent of
 * the current marking context. The lifetime of this structure is that of
 * the input/output PDF job. Initial value for all members is 0|NULL.
 */
struct PDFXCONTEXT {

  /** Context ID and linked list pointer. */
  int32 id ;
  PDFXCONTEXT *next ;

  /** Per-thread context */
  corecontext_t *corecontext ;

  /** Pointer to the current marking context (head of a linked
     list). */
  PDFCONTEXT *pdfc ;

  /** Document-level metadata. */
  DocumentMetadata metadata ;

  /** The ID of the current page; this is used for tracking the
   * lifetimes of objects in the xref cache. */
  int32 pageId ;

  /** Flag to indicate the occurrence of an error during the job. If
   * this is set, the code unwinding towards the end of the job should
   * not attempt to do any cleaning up like clearing the stack etc. to
   * avoid memory problems. Rely on the destruction of the MPS pool to
   * free any stragglers.
   */
  Bool error ;

  /** MM Pools for objects and infrastructure allocated in this
     context. */
  mm_pool_t mm_object_pool;
  mm_pool_t mm_structure_pool;
  int32 savelevel ;

  /** File details. */
  FILELIST *flptr ;
  Hq32x2 filepos ;
  Hq32x2 fileend ;

  /** PDF encryption information. NULL means no encryption in this PDF
     file. */
  PDF_CRYPTO_INFO *crypt_info ;

  /** PDF xref table. */
  struct XREFSEC *xrefsec ;

  /** List of streams used in this PDF file. */
  FILELIST *streams ;

  /** Do FlateDecode filters created in this context enforce the
      checksum? */
  Bool ErrorOnFlateChecksumFailure ;

  /** PDF xref object table. */
  struct XREFCACHE *xrefcache[ XREF_CACHE_SIZE ] ;

  /** Indicates the depth down the pdf-walking callstack we are. This helps
      the xref purging code to decide when an object has gone out of scope
      and can be safely purged. */
  int32 pdfwalk_depth;

  /** Page Device defaults - filled in by pdfexec_ */
  int32 pdd_orientation ; /* 0-3 => c/wise rotate = 360 - (orient. * 90 deg) */

  int32 debugtotal_cacheloads ;
  int32 debugtotal_cachehits ;
  int32 debugtotal_cachereleases ;
  int32 debugtotal_cachereclaims ;

  /** To prevent infinite recursion in copying, flattening etc. */
  int32 recursion_depth ;

  /** To prevent infinite recursion in resolving indirect objects. */
  int32 lookup_depth ;
  int32 nestedObjnum[ PDF_MAX_RECURSION_DEPTH ] ;

  /** The input or output specific routines and data. */
  PDF_METHODS methods ;
  union {
    void *v ;
    PDF_IXC_PARAMS *i ;
    PDF_OXC_PARAMS *o ;
  } u ;

  FILELIST_FLUSHBUFF FlushBuff;

  /** For assisting in the avoidance of low memory handling for PDF structures
     unless something significant has changed since the last call to low memory
     handling.
     For xref objects, there is no point either calling the low memory handler
     or re-measuring the purgeable objects  more than once per page.
     For streams, there is no point in either calling the low memory handler or
     re-measuring the purgeable streams unless at least one stream has closed
     since the last call.
     If nothing significant has changed, the low memory measurements should
     return the value from the last call. */
  Bool lowmemRedoXrefs;
  Bool lowmemXrefPageId;
  size_t lowmemXrefCount;
  Bool lowmemRedoStreams;
  size_t lowmemStreamCount;

  /** Flag indicating whether we're currently executing a deferred
      xref cache flush. This influences how we handle streams, which
      are the only thing we copy by reference rather than deep
      copy. */
  Bool in_deferred_xrefcache_flush ;
};

/** Possible values for streamtype in PDFCONTEXT */
enum {
  PDF_STREAMTYPE_PAGE ,
  PDF_STREAMTYPE_FORM ,
  PDF_STREAMTYPE_CHARPROC ,
  PDF_STREAMTYPE_PATTERN
} ;

/** The PDF marking context - contains all data relating to a particular
 * marking context as defined by the spec: page, form, pattern or Type
 * 3 glyph. All members are either initialised or inherited from the
 * enclosing marking context when the structure is created, and either
 * forgotten or cleared back to the inherited values when it is destroyed.
 * The lifetime of this structure is that of the marking context it
 * represents. There's also a 'special' marking context for the PDF
 * infrastructure (Pages tree etc.) with a nesting count of zero.
 */
struct PDFCONTEXT {

  /** Pointer for the linked list. */
  PDFCONTEXT *next ;

  /** Owning PDF execution context. */
  PDFXCONTEXT *pdfxc ;

  /** The core context. */
  corecontext_t *corecontext ;

  /** Nesting level of this marking context. */
  int32 mc ;

  /** The content stream type of marking context: page, form or char
      or pattern. */
  int streamtype ;

  /** Current contents object (could be a stream or an array). */
  OBJECT *contents ;

  /** Current index into the contents object if it is an array */
  int32 contentsindex ;

  /** Current contents stream object. */
  OBJECT *contentsStream ;

  /** Resource list for the pages. Stores the 'Resources' attribute from
   * the current page to the root Pages object.
   */
  PDF_DICTLIST *pdfenv ;

  /** List of files to restore to their original offsets. */
  PDF_FILERESTORE_LIST *restorefiles ;

  /** Cache for frequently required resources, DefaultGray etc. */
  PDF_RESOURCE_CACHE resource_cache[ RES_N_CACHE_ENTRIES ] ;

  /** Input or output specific data. */
  union {
    void *v ;
    PDF_IMC_PARAMS *i ;
    PDF_OMC_PARAMS *o ;
  } u ;
};

/* ----- External global variables ----- */

/* ----- Exported macros ----- */

#define PDF_CHECK_METHOD( _method ) \
  HQASSERT( pdfxc->methods._method, "Can't invoke a PDF method." )

#define PDF_CHECK_MC( _mc ) \
  HQASSERT(( _mc ) , "pdfc NULL." ) ;

#define PDF_GET_XC( _xc ) \
  ( _xc ) = pdfc->pdfxc ; \
  HQASSERT(( _xc ) , "pdfc->pdfxc NULL." ) ;

#define PDF_GET_IMC( _imc ) \
  ( _imc ) = pdfc->u.i ; \
  HQASSERT(( _imc ) , "pdfc->u.i NULL." ) ;

#define PDF_GET_IXC( _ixc ) \
  ( _ixc ) = pdfxc->u.i ; \
  HQASSERT(( _ixc ) , "pdfxc->u.i NULL." ) ;

#define PDF_GET_OMC( _omc ) \
  ( _omc ) = pdfc->u.o ; \
  HQASSERT(( _omc ) , "pdfc->u.o NULL." ) ;

#define PDF_GET_OXC( _oxc ) \
  ( _oxc ) = pdfxc->u.o ; \
  HQASSERT(( _oxc ) , "pdfxc->u.o NULL." ) ;

/** Place this macro at the end of a variable block to define and initialise the
standard PDF variables for the PDFXCONTEXT and PDF_IXC_PARAMS structures from
the current context (an implicit PDFCONTEXT variable called 'pdfc'). */
#define GET_PDFXC_AND_IXC \
  PDFXCONTEXT *pdfxc; \
  PDF_IXC_PARAMS *ixc; \
  PDF_CHECK_MC(pdfc); \
  PDF_GET_XC(pdfxc); \
  PDF_GET_IXC(ixc);


Bool pdf_register_execution_context_base( PDFXCONTEXT **base ) ;
Bool pdf_purge_execution_contexts( int32 savelevel ) ;

void pdf_C_globals(struct core_init_fns *fns) ;

/** \} */

#endif /* protection for multiple inclusion */

/* ============================================================================
* Log stripped */
