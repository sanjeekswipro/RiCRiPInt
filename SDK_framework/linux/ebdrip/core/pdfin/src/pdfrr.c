/** \file
 * \ingroup pdfrr
 *
 * $HopeName: SWpdf!src:pdfrr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Retained Raster implementation
 */

#include "core.h"

#include "control.h"     /* interpreter */
#include "corejob.h"     /* corejob_t */
#include "debugging.h"   /* debug_print_object_indented */
#include "devices.h"     /* progressdev */
#include "dl_image.h"    /* im_erase */
#include "gu_chan.h"     /* guc_omitSetIgnoreKnockouts */
#include "hqmemcmp.h"    /* HqMemCmp */
#include "hqmemcpy.h"    /* HqMemCpy */
#include "interrupts.h"  /* interrupts_clear */
#include "irr.h"         /* irr_setdestination */
#include "metrics.h"     /* sw_metrics_group */
#include "miscops.h"     /* run_ps_string */
#include "mlock.h"       /* multi_mutex_init */
#include "monitor.h"     /* monitorf */
#include "murmurhash3.h" /* Murmurhash3 */
#include "params.h"      /* USERPARAMS */
#include "progupdt.h"    /* start_file_progress */
#include "rcbcntrl.h"    /* rcbn_enabled */
#include "region.h"      /* lobj_maybecompositing */
#include "render.h"      /* external_retained_raster */
#include "ripmulti.h"    /* NUM_THREADS */
#include "riptimeline.h" /* CHECK_TL_VALID */
#include "routedev.h"    /* SET_DEVICE */
#include "stream.h"      /* streamLookup */
#include "streamd.h"     /* stream_attach_rr_state */
#include "swcopyf.h"     /* swncopyf */
#include "swerrors.h"    /* error_handler */
#include "swevents.h"    /* SWEVT_RR_ELEMENT_DEFINE */
#include "swmemory.h"    /* gs_cleargstates */
#include "swpdfin.h"     /* PDF_STREAMTYPE_PAGE */
#include "swtrace.h"     /* SW_TRACE_RETAINEDRASTER_ACQUIRE */
#include "trap.h"        /* isTrappingActive */
#include "vntypes.h"     /* VDL_None */

#include "pdfattrs.h"    /* pdf_get_resource */
#include "pdfclip.h"     /* pdf_check_clip */
#include "pdfin.h"       /* pdf_ixc_params */
#include "pdfmatch.h"    /* pdf_dictmatch */
#include "pdfops.h"      /* OPSTATE_PDL */
#include "pdfrr.h"       /* pdf_rr_end */
#include "pdfxref.h"     /* pdf_lookupxref */

#include "namedef_.h"
#include "timing.h"      /* probe_* */

/** Default size of the \c rr_state->nodes and marks arrays. They get
    reallocated as necessary. \see RR_STATE */

#define RR_DEFAULT_BUCKET_SIZE 32

/** Abstract the hash length in bytes because we may well change it in
    the future. */

#define RR_HASH_LENGTH (16)

/** A node in a tree which maps hash values onto sets of page indices
    - used in pagestree. */

typedef struct {
  /** The number of indices currently stored: obviously must be less
      than or equal to \c size. */
  uint32 count ;

  /** The allocated size of the \c index array. */
  uint32 size ;

  /** A variably-sized array of indices. */
  uint32 index[ 1 ] ;
}
RR_PAGES_NODE ;

/** A node in the principal PDF Retained Raster data structure - a
    tree of these objects. If \c hits is > 1 then it might be worth
    caching. */

typedef struct RR_SCAN_NODE {
  /** We store a pointer to our node key here for convenience. The
      tree has nodes coming and going, and maintaining a pointer to
      the node data rather than the node itself means we don't fall
      foul of node contents being copied around during tree
      rebalancing after node deletion. */
  uint8 *hash ;

  /** Count of how many times this node appears in the the current
      page range. */
  uint32 hits ;

  /** Bounding box for this node in device coordinates. */
  dbbox_t bbox ;

  /** Union containing different kinds of pointers depending on the
      kind of node. An ordinary singleton node will have NULL here. */
  union {
    /** If this node is a supernode, this slot stores the original
        node which the supernode replaced in list of scan nodes. */
    struct RR_SCAN_NODE *orig ;

    /** Unused at present, but it's a placeholder for future Form
        handling. */
    struct RR_SCAN_NODE **form_nodes ;
  } u ;

  /** The last page on which this node was seen. */
  uint32 last_page ;

  /** Cumulative 32 bit hash of the indices of the pages upon which
      this mark appears. Makes for better clustering than simply using
      hit count. */
  uint32 pages_hash ;

  /** The pages tree node associated with this scan node
      (i.e. corresponding to \c pages_hash). We could look up
      pages_hash in the tree each time, but one extra pointer here
      buys us better performance. */
  RR_PAGES_NODE *ptn ;

  /** Flag which is set iff this node represents a Form XObject. */
  unsigned is_form:1 ;

  /** Flag which is set if and only if we're looking at a Form XObject
      which has a /Matrix entry in its dictionary. */
  unsigned has_matrix:1 ;

  /** Flag which is set if and only if we're looking at a Form XObject
      which has a /Ref entry in its dictionary. */
  unsigned has_ref:1 ;

  /** Is this mark to be composited? Note this is only relevant for
      regular scan nodes: by the time we are creating supernodes, the
      effects on the scan of needing compositing for certain objects
      have already been applied. */
  unsigned composited:1 ;

  /** Is the mark drawn in a pattern colorspace? The fact that this is
      a simple flag can mean that consecutive marks drawn in different
      patterns end up being made part of a single atom, but it's
      probably not worth doing anything more complicated. */
  unsigned pattern:1 ;

  /** Flag indicating whether the mark passed the significance test. */
  unsigned significant:1 ;
}
RR_SCAN_NODE ;

/** A node in the resource tree. We pre-calculate the hash
    contribution for a resource lookup, along with noting whether or
    not the resource is a Form XObject. */

typedef struct {
  /** Hash for the contents of this resource. */
  uint8 hash[ RR_HASH_LENGTH ] ;

  /** Flag indicating whether or not this resource is a Form
      XObject. */
  unsigned is_form:1 ;

  /** Flag which is set when the resource's dictionary contains a
      Matrix entry. */
  unsigned has_matrix:1 ;

  /** Flag which is set when the resource's dictionary contains a
      Ref entry. */
  unsigned has_ref:1 ;

  /** Flag indicating whether or not this resource is an Image
      XObject. */
  unsigned is_image:1 ;

  /** Stream length for this resource (or zero if none has been
      encountered). */
  int32 stream_len ;
}
RR_RESOURCE_NODE ;

/* How much input to store while calculating hashes. */

#define RR_HASH_INPUT_BUF_SIZE 64

/** Retained raster hashing state. There's one of these per aspect of
    the state built up during the scan of the content stream. It used
    to be more complicated when we used md5. We'll keep it as a
    defined struct to hang on to the right level of abstraction. */

typedef struct {
  /** Container for the retained raster hash (calculated
      incrementally). */
  uint8 hash[ RR_HASH_LENGTH ] ;
}
RR_HASH_STATE ;

/** Member of the \c marks array for each page. There's one of these
    for each marking operator in the page's content stream and they're
    always stored in the same order as they appear in that stream
    (unlike previous incarnations of this module). */

typedef struct {
  /** Whether the mark with this index should be omitted or not. When
      this flag is true, we've added a cached raster to the display
      list so we can optimise the interpretation away when
      replaying. */
  unsigned omit:1 ;

  /** Flag indicating whether this mark is a Do operator on a Form
      XObject. If so, it's a placeholder for the storing/exporting and
      replaying phases and won't be included in page links. */
  unsigned is_form:1 ;

  /** Flag indicating whether this mark is a Do operator on a Form
      XObject which has a /Ref entry. If so, we treat it as a closed
      object rather than a container since the scanner can't (and
      indeed shouldn't) recursively scan into such referenced
      streams. */
  unsigned has_ref:1 ;

  /** This flag indicates that the mark in question is the first in a
      sequence which must be treated as a single unit (an atomic
      supernode). */
  unsigned atom_start:1 ;

  /** This flag indicates that the mark in question is the last in a
      sequence which must be treated as a single unit (an atomic
      supernode). */
  unsigned atom_end:1 ;

  /** This is the identifier of the operator making the mark.
      This is only used for logging and debugging purposes. */
  unsigned pdfop:7 ;

  /** The nesting level of the mark, that is, the number of recursive
      Do operators containing it. */
  unsigned nesting:8 ;

  /** The amount of inline data to skip when skipping this mark. */
  uint32 inline_data_bytes ;
}
RR_PAGE_MARK ;

/** For mapping content stream marking operators onto cache node
    lookups. */

typedef struct RR_PAGE_LINK {
  /** Next node in the doubly-linked list. */
  struct RR_PAGE_LINK *next ;

  /** Previous node in the doubly-linked list. */
  struct RR_PAGE_LINK *prev ;

  /** This pointer is for when we have a supernode. Points to the last
      link in the subnodes if this node is the first of a supernode,
      otherwise it's NULL. */
  struct RR_PAGE_LINK *sub_last ;

  /** This pointer is for when we have a supernode. Points to the
      first link in the subnodes if this node is the last of a
      supernode, otherwise it's NULL. */
  struct RR_PAGE_LINK *sub_first ;

  /** The cache node associated with this marking operator. Must never
      be null: will point to the nil sentinel if there's no node
      present. */
  RR_SCAN_NODE *sn ;

  /** The index of the mark in the current content stream: the index
      into the page's \c marks array. */
  uint32 mark_num ;
}
RR_PAGE_LINK ;

/** A tree node in \c rr_state->elemtree. There's one of these for
    each unique id which appears on at least one page's elements
    list. */

typedef struct RR_ELEM_NODE {
  /** The bounding box of this element. */
  dbbox_t bbox ;

  /** The hit count (to be passed to the PGB device in ERR). */
  uint32 hits ;

  /** Flag which is set when \c pdf_rr_page_totals has noted that this
      element is to be cached. */
  Bool counted ;

#if defined( DEBUG_BUILD )
  /** The zero-based index of when this element first appears
      (i.e. the same thing as the showpage number in GGDUMB1 debug
      mode.) */
  uint32 index ;
#endif
}
RR_ELEM_NODE ;

/** The type for storing the final list of raster elements for a
    page. */

typedef struct RR_PAGE_ELEMENT {
  /** Pointer to this element's node key in \c rr_state->elemtree. */
  uint8 *id ;

  /** The hash value which is the key for retrieving the sequence of
      page marks which makes up this element. Note that the set of
      mark indices which make up a given element are potentially
      unique to each page on which it appears. That is, this quantity
      definitely does not belong in \c RR_ELEM_NODE. */
  uint32 marks_hash ;
}
RR_PAGE_ELEMENT ;

/** Represents data we've gathered about each page _in the requested
    page range_ during the retained raster scanning phase. */

typedef struct {
  /** Size of the \c marks and \c links arrays. */
  uint32 mark_count ;

  /** One entry per marking operator on the page. */
  RR_PAGE_MARK *marks ;

  /** One slot per marking operator on the page: fewer will end up
      being used if supernodes are created. */
  RR_PAGE_LINK *links ;

  /** Sentinel for the beginning of the linked list. */
  RR_PAGE_LINK head ;

  /** Sentinel for the end of the linked list. */
  RR_PAGE_LINK tail ;

  /** The list of raster elements for the page. */
  RR_PAGE_ELEMENT *elements ;

  /** The size of the \c elements array. */
  uint32 element_count ;

  /** Quad-state indicating where this page has got to. Generally a
      page will progress from WAITING to READY to COMPLETE but note
      that it is possible to sublime straight from WAITING to COMPLETE
      in certain circumstances. */
  enum {
    RR_PAGE_WAITING ,
    RR_PAGE_READY ,
    RR_PAGE_COMPLETE ,
    RR_PAGE_COMPLETE_EARLY
  } state ;
}
RR_PAGE ;

/** A node in a tree which maps hash values onto arrays of page marks
    - used in markstree. */

typedef struct {
  /** The number of marks stored: the size of the \c marks array. */
  uint32 count ;

  /** A variably-sized array of marks. */
  RR_PAGE_MARK marks[ 1 ] ;
}
RR_MARKS_NODE ;

/** The state needed to track stream gstates. */

typedef struct RR_GSTATE {
  /** The PDL hashing state. */
  RR_HASH_STATE saved_pdl_state ;

  /** Parent pointer. */
  struct RR_GSTATE *enclosing_state ;
}
RR_GSTATE ;

/** Context used by the scanner to track the arc of recursive
    execution in PDF content streams: \see pdf_rr_pre_op,
    pdf_rr_post_op */

typedef struct RR_OP_CONTEXT {
  /* Simple z-order index of the current mark at the time of
     execution. */
  uint32 mark_number ;

  /** The display list opcode of the mark. */
  uint8 opcode ;

  /** The stream offset of the current operator. */
  uint32 offset ;

  /** The bounding box of the mark made by the current operator. */
  dbbox_t bbox ;

  /* RR scanner hash for tidying up when a recursive invocation
     returns. */
  uint8 hash[ RR_HASH_LENGTH ] ;

  /** Flag indicating whether this resource is a Form XObject. They
      are treated specially during \c RR_MODE_SCANNING. */
  unsigned is_form:1 ;

  /** Flag which is set if and only if we're looking at a Form XObject
      which has a /Matrix entry in its dictionary. */
  unsigned has_matrix:1 ;

  /** Flag which is set if and only if we're looking at a Form XObject
      which has a /Ref entry in its dictionary. */
  unsigned has_ref:1 ;

  /** When this flag is set, the current object is image XObject whose
      length meets or exceeds the OptimizedPDFImageThreshold param. */
  unsigned is_significant_image:1 ;
}
RR_OP_CONTEXT ;

/** State structure for the retained raster code. */

typedef struct RR_STATE {
  enum {
    /** No special action: identical to what happened before the
        retained raster code existed. */
    RR_MODE_NORMAL ,

    /** Simple: skip the page content. Used when counting the number
        of pages in the file or folding up execution when we decide to
        abort scanning early (to avoid scanning overhead when it looks
        like not enough retained rasters will be used). */
    RR_MODE_SKIPPING ,

    /** Scanning the resource hierarchy before embarking on the page
        content scan. In this mode, no output is produced. */
    RR_MODE_PRE_SCANNING ,

    /** Scanning the PDF file for pages which might share a common
        background. In this mode, no output is produced. */
    RR_MODE_SCANNING ,

    /** An extra state after scanning which was useful during
        development and may be again. Currently unused. */
    RR_MODE_POST_SCANNING ,

    /** Imaging the invariant part of the page ready to be stored on
        disk for later retrieval. Parts of the page identified by the
        scanning stage as variable are skipped. */
    RR_MODE_STORING ,

    /** Imaging the invariant part of the page to an external
        pagebuffer store for later retrieval. Parts of the page
        identified by the scanning stage as variable are skipped. */
    RR_MODE_EXPORTING ,

    /** Imaging the variable data parts of the page - the background
        will already have been loaded, and so the invariant parts of
        the content stream are skipped. */
    RR_MODE_REPLAYING
  } mode ;

  /** The memory pool from which scan nodes will be allocated. */
  mm_pool_t mark_pool ;

  /** Our nil sentinel node: created at the beginning so we have
      something valid to point to in order to denote a node which does
      not exist. Doing it this way means we don't have to check
      pointers for NULL in the myriad places we dereference them. */
  RR_SCAN_NODE *nil_sentinel ;

  /** Array of pages we're in the process of outputting: those in the
      requested page range, in order of output. */
  RR_PAGE *pages ;

  /** The page index offset to apply when calling the API
      etc. (i.e. the total number of pages in previous iterations
      (pdfexecid chunks)). */
  uint32 page_offset ;

  /** Number of pages we're going to output in this iteration: hence
      also the size of the \c pages array. */
  uint32 page_count ;

  /** Number of pages yet to be completed. */
  uint32 pages_remaining ;

  /** Number of unique pages found in the scan so far. Used in
      determining whether we've exceeded the ScanLimitPercent
      threshold. */
  uint32 unique_page_count ;

  /** PDL hashing state. */
  RR_HASH_STATE pdl_state ;

  /** Text hashing state. */
  RR_HASH_STATE text_state ;

  /** The current operator context - tracks the recursive path through
      PDF interpretation. */
  RR_OP_CONTEXT *op_context ;

  /** The current size of the retained raster cache. */
  mm_size_t cache_size ;

  /** The base of the scan hash tree. */
  RBT_ROOT *scantree ;

  /** The base of the resource hash tree. */
  RBT_ROOT *restree ;

  /** The base of the pages hash tree. */
  RBT_ROOT *pagestree ;

  /** The tree holding unique sequences of page mark indices. Maps
      marks_hash values onto a set of omit/don't omit flags for
      storing, exporting or replaying. */
  RBT_ROOT *markstree ;

  /** The base of the tree containing the minimal data representing
      each unique page element encountered during the scan. */
  RBT_ROOT *elemtree ;

  /** Collection bucket for the array of marks on the current page. */
  RR_PAGE_MARK *marks ;

  /** Collection bucket for the linked list of nodes on the current
      page. */
  RR_PAGE_LINK *links ;

  /** Size of the nodes and marks arrays - this grows as a high water
      mark is reached. */
  uint32 bucket_size ;

  /** Number of items currently in the nodes array. */
  uint32 nodes_count ;

  /** Number of marking operators seen on the current page. Used to
      synchronise the scanning and replaying phases of retained
      raster. */
  uint32 mark_count ;

  /** Pointer to the current gstate tracker. */
  RR_GSTATE *current_gstate ;

  /** A recursion count for tracking Do operators. If we go recursive
      in Do, that means we must be executing a Form XObject and we
      need to track the implicit gsaves and grestores associated with
      that. */
  uint8 nested_Do ;

  /** The \c nested_Do level at which we last did an implicit
      gsave. */
  uint8 last_implicit_gsave ;

  /** Flag indicating whether there's a hash value waiting to be
      incorporated into the running PDL hash right after next time an
      implicit gsave happens. */
  Bool deferred_pdl_hash ;

  /** Hash value which is to be incorporated into the running PDL hash
      after the next implicit gsave. */
  uint8 deferred_pdl_hash_contrib[ RR_HASH_LENGTH ] ;

  /** The current PDF State Machine value. Transitions of this value
      can give rise to the need for replicating implicit behaviours
      such as `newpath` to keep output in sync. */
  int32 pdf_state ;

  /** Nesting count of atomic mark contexts. When this transitions
      from 1 to 0 we're leaving the outermost enclosing context, which
      is the only one that counts. */
  uint32 current_atomic_number ;

  /** Mark number where \c current_atomic_number first changed from 0
      to 1. Rogue value for "never" is MAXUINT32. */
  uint32 current_atom_start ;

  /** The mark number to which the current atom extends. Moved along
      every time we see a mark while there is an atom candidate
      present. */
  uint32 current_atom_end ;

  /** Current state of play with regard to whether objects being added
      to the display list will need compositing. */
  Bool current_compositing ;

  /** Flag indicating whether we're currently in a pattern drawing
      state. */
  Bool current_pattern ;

  /** Current state of play with regard to whether objects being added
      to the display list are drawn in a pattern colorspace. */
  void *current_patternstate ;

  /** A flag which is set when we're currently between a BI and EI
      operator. */
  Bool in_inline_image ;

  /** Handle for this invocation's connection to the raster element
      cache. */
  void *cache ;

  /** Reference for scanning or other process timelines. */
  sw_tl_ref tl_ref ;

  /** Job timeline reference. */
  sw_tl_ref job ;

  /** The gstate id of the gsave around processing each page. We use
      this to isolate gstate changes which would normally be blown
      away by a showpage, for instance. */
  int32 gid ;

  /** PS Procedure for updating progress. */
  OBJECT SetExplicitPageNumber ;

  /** Event handler for a timeline having started. Needed to track
      when we have an IRR page in flight. */
  sw_event_handler start ;

  /** Event handler for a timeline being aborted. Needed to prevent
      lockups when pages are outstanding but will never arrive. */
  sw_event_handler aborted ;

  /** Event handler for a timeline having ended normally. Needed to
      track when we're done with an IRR page. */
  sw_event_handler ended ;

  /** Handler for page define events. */
  sw_event_handler page_define ;

  /** Handler for page ready events. */
  sw_event_handler page_ready ;

  /** Event handler for Page Complete events - so we can know when the
      cache instance has (asynchronously) done with the rasters of a
      given page. */
  sw_event_handler page_complete ;

  /** Handler for element define events. */
  sw_event_handler element_define ;

  /** Handler for element lock events. */
  sw_event_handler element_lock ;

  /** Handler for element unlock events. */
  sw_event_handler element_unlock ;

  /** Handler for element pending events. */
  sw_event_handler element_pending ;

  /** Handler for element query events. */
  sw_event_handler element_query ;

  /** Handler for element update raster events. */
  sw_event_handler element_update_raster ;

  /** Handler for element update hits events. */
  sw_event_handler element_update_hits ;

  /** Condition variable for the predicate \c pages_remaining. */
  multi_condvar_t condvar ;

  /** Mutex for \c condvar. */
  multi_mutex_t mutex ;

  /** Special case flag for signifying when we've hit a condition in
      the scan which we can't cope with, and we should force the scan
      to be aborted at the end of the page. */
  Bool abandon_scan ;

#if defined( DEBUG_BUILD )
  /** Stack of debug files: handy so we can dump a subordinate log (so
      to speak) and then carry on where we left off. */
  STACK dbg_file_stack ;

  /** The single frame belonging to \c dbg_file_stack. */
  SFRAME dbg_file_stackframe ;

  /*! The debug file is a garbage collection root in debug builds. */
  mps_root_t dbg_file_root ;

  /* The gsave indentation level for debug formatting */
  int dbg_gstate_depth ;

  /* Have we already indented the current debug line? */
  Bool dbg_indented ;

#define RR_DBG_BUF_SIZE 2048

  /** Buffer for assembling debug log messages. Always use swncopyf,
      although the size should be plenty. */
  uint8 dbg_buf[ RR_DBG_BUF_SIZE ] ;

  /** Number of showpages so far (not the same as logical pages). */
  int dbg_showpage_count ;

  /** The current logical page number. */
  int dbg_page_num ;

  /** The current element number. */
  int dbg_element_num ;

  /** The total number of unique elements defined so far. */
  uint32 unique_element_count ;
#endif
}
RR_STATE ;

#if defined( DEBUG_BUILD )
#include "ripdebug.h"  /* register_ripvar */
#include "gcscan.h"    /* ps_scan_field */

enum {
  DEBUG_RR_HASHES = 1 << 0 ,
  DEBUG_RR_INTERMEDIATE_HASHES = 1 << 1 ,
  DEBUG_RR_OPERATORS = 1 << 2 ,
  DEBUG_RR_OPERANDS = 1 << 3 ,
  DEBUG_RR_Z_ORDER = 1 << 4 ,
  DEBUG_RR_SUPERNODES = 1 << 5 ,
  DEBUG_RR_SEQUENCE_POINTS = 1 << 6 ,
  DEBUG_RR_IMPLICIT_GSTACK = 1 << 7 ,
  DEBUG_RR_STATE_CHANGES = 1 << 8 ,
  DEBUG_RR_ACTIVE_NODES = 1 << 9 ,
  DEBUG_RR_HASH_INNARDS = 1 << 10 ,
  DEBUG_RR_RESOURCES = 1 << 11 ,
  DEBUG_RR_NODE_STATS = 1 << 12 ,
  DEBUG_RR_UNIQUE_PAGES = 1 << 13 ,
  DEBUG_RR_SCAN_WINDOW = 1 << 14 ,
  DEBUG_RR_PAGES_TREE = 1 << 15 ,
  DEBUG_RR_MARKS_TREE = 1 << 16 ,
  DEBUG_RR_PAGE_ELEMENTS = 1 << 17 ,
  DEBUG_RR_EVENTS = 1 << 18 ,

  DEBUG_RR_ANY = 0xffffffff
} ;

static int32 debug_retained_raster = 0 ;

#define TWEAK_RR_DISABLE_AUTO_SUPERNODES  1
#define TWEAK_RR_DISABLE_Z_ORDER_MOVES    2

static int32 tweak_retained_raster = 0 ;

static char * tokens = /* These must be in the same order as the PDFOP_* enum */
  "?  b  B  B1sb1sBDCBIVBMCBT BX c  cm cQ cq cs CS d  d0 d1 Do DP EIV"
  "EMCET EX f  f1sG  g  gs h  i  ID j  J  k  K  l  m  M  MP n  PS q  "
  "Q  re RG rg ri s  S  SC sc scnSCNsh T1qT1sT2qTc Td TD Tf TJ Tj TL "
  "Tm Tr Ts Tw Tz v  W  w  W1sy  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  "
  "?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  "
  "?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  ?  " ;

/** Macro for quickly establishing how many string digits are needed
    to represent _v, a 32 bit unsigned integer. Lowest comparisons are
    first because the values are expected to be skewed towards the
    zero. */

#define UNSIGNED_DIGITS( _v )                                           \
  (( _v ) < 10 ) ? 1 :                                                  \
  (( _v ) < 100 ) ? 2 :                                                 \
  (( _v ) < 1000 ) ? 3 :                                                \
  (( _v ) < 10000 ) ? 4 :                                               \
  (( _v ) < 100000 ) ? 5 :                                              \
  (( _v ) < 1000000 ) ? 6 :                                             \
  (( _v ) < 10000000 ) ? 7 :                                            \
  (( _v ) < 100000000 ) ? 8 : 9                                         \

/** Open a debug log file for the given phase (and page number and
    element if they're not the rogue value \c MAXUINT32). It's then
    pushed on the debug file stack. */

static void pdf_rr_open_debug_log( RR_STATE *rr_state , char *phase ,
                                   uint32 page_num , uint32 element )
{
  RR_PAGE *page = & rr_state->pages[ page_num ] ;
  int32 page_digits = UNSIGNED_DIGITS( rr_state->page_offset +
                                       rr_state->page_count ) ;
  OBJECT dbg_file ;
  DEVICELIST *dev ;
  FILELIST *flptr ;
  Hq32x2 filepos ;
  uint8 buf[ 256 ] ;
  int32 len ;

  if ( page_num == MAXUINT32 ) {
    page_num = 0 ;
  }
  else {
    page_num += rr_state->page_offset ;
  }

  if ( element != MAXUINT32 ) {
    int32 element_digits = UNSIGNED_DIGITS( page->element_count ) ;

    ( void )swncopyf( buf , 256 ,
                      ( uint8 * )"%%%%os%%%%/RR_logs/p%%0%du_e%%0%du_%%s.log",
                      page_digits , element_digits ) ;

    len = swncopyf( rr_state->dbg_buf , RR_DBG_BUF_SIZE , buf ,
                    page_num , element , phase ) ;
  }
  else {
    ( void )swncopyf( buf , 256 ,
                      ( uint8 * )"%%%%os%%%%/RR_logs/p%%0%du_%%s.log",
                      page_digits ) ;

    len = swncopyf( rr_state->dbg_buf , RR_DBG_BUF_SIZE , buf ,
                    page_num , phase ) ;
  }

  theLen( snewobj )  = CAST_SIZET_TO_UINT16( len ) ;
  oString( snewobj ) = rr_state->dbg_buf ;

  /* Windows has no SW_APPEND so we have to do this instead. */

  if ( ! file_open( & snewobj , SW_WRONLY | SW_CREAT ,
                    WRITE_FLAG , FALSE , FALSE , & dbg_file ) ||
       ! push( & dbg_file , & rr_state->dbg_file_stack )) {
    HQFAIL( "Failed to open debug log" ) ;
  }

  flptr = oFile( dbg_file ) ;
  dev = theIDeviceList( flptr ) ;
  if ( ! ( *theIBytesFile( dev ))( dev , theIDescriptor( flptr ) , & filepos ,
                                   SW_BYTES_TOTAL_ABS )) {
    HQFAIL( "Failed to get length of debug log" ) ;
  }

  if (( *theIMySetFilePos( flptr ))( flptr , & filepos ) == EOF ) {
    HQFAIL( "Failed to seek to end of debug log" ) ;
  }
}

/** Close the current debug log file and pop it off the stack. */

static void pdf_rr_close_debug_log( RR_STATE *rr_state )
{
  int32 ssize = theStackSize( rr_state->dbg_file_stack ) ;
  OBJECT *dbgf = TopStack( rr_state->dbg_file_stack , ssize ) ;

  /* It's convenient to refuse to close the lowest file on the stack
     so it can act as a backstop. */
  if ( ssize > 0 ) {
    HQASSERT( ! EmptyStack( ssize ) , "No debug file" ) ;
    HQASSERT( oType( *dbgf ) == OFILE , "Debug file is wrong type" ) ;

    ( void )file_close( dbgf ) ;
    pop( & rr_state->dbg_file_stack ) ;
  }
}

/** Get the current debug output file from the stack. */

static FILELIST *pdf_rr_debug_file( RR_STATE *rr_state )
{
  int32 ssize = theStackSize( rr_state->dbg_file_stack ) ;
  OBJECT *dbgf = TopStack( rr_state->dbg_file_stack , ssize ) ;

  HQASSERT( ! EmptyStack( ssize ) , "No debug file" ) ;
  HQASSERT( oType( *dbgf ) == OFILE , "Debug file is wrong type" ) ;

  return oFile( *dbgf ) ;
}

/** Macro to output debug information which boils away to nothing in
    non-debug builds. */

#define RR_DBG( _switch , _args )                               \
  MACRO_START                                                   \
  if (( debug_retained_raster & DEBUG_RR_##_switch ) != 0 ) {   \
    pdf_rr_debug_log _args ;                                    \
  }                                                             \
  MACRO_END

#define RR_DBG_INDENT( _switch , _rr_state , _depth )           \
  MACRO_START                                                   \
  if (( debug_retained_raster & DEBUG_RR_##_switch ) != 0 ) {   \
    pdf_rr_debug_indent( _rr_state , _depth ) ;                 \
  }                                                             \
  MACRO_END

#define RR_DBG_OPEN( _switch , _rr_state , _phase ,             \
                     _page_num , _element )                     \
  MACRO_START                                                   \
  if (( debug_retained_raster & DEBUG_RR_##_switch ) != 0 ) {   \
    pdf_rr_open_debug_log( _rr_state , _phase ,                 \
                           _page_num , _element ) ;             \
  }                                                             \
  MACRO_END

#define RR_DBG_CLOSE( _switch , _rr_state )                     \
  MACRO_START                                                   \
  if (( debug_retained_raster & DEBUG_RR_##_switch ) != 0 ) {   \
    pdf_rr_close_debug_log( _rr_state ) ;                       \
  }                                                             \
  MACRO_END

#define RR_DBG_STACK( _imc , _rr_state , _op , _skip )          \
  MACRO_START                                                   \
  pdf_rr_log_stack( _imc , _rr_state , _op , _skip ) ;          \
  MACRO_END

/** Indentation reflecting the gsave depth. */

static void pdf_rr_debug_indent( RR_STATE *rr_state , int depth )
{
  static uint8 spaces[] = "                                        " ;
  int indent ;

  if ( ! rr_state->dbg_indented ) {
    for ( indent = depth ; indent > 0 ; indent -= 20 ) {
      ( void )file_write( pdf_rr_debug_file( rr_state ) , spaces ,
                          ( indent * 2 ) % 40 ) ;
    }
    rr_state->dbg_indented = TRUE ;
  }
}

/** Write debug logging to the current output target. */

static void pdf_rr_debug_log( RR_STATE *rr_state , uint8 *format , ... )
{
  int32 len ;
  va_list argp ;
  FILELIST *flptr ;

  pdf_rr_debug_indent( rr_state , rr_state->dbg_gstate_depth ) ;

  va_start( argp , format ) ;

  len = vswncopyf( rr_state->dbg_buf , RR_DBG_BUF_SIZE , format , argp ) ;

  va_end( argp ) ;

  flptr = pdf_rr_debug_file( rr_state ) ;
  if ( isIOpenFile( flptr )) {
#if defined( ASSERT_BUILD )
    Bool written = file_write( flptr , rr_state->dbg_buf , len ) ;

    HQASSERT( written , "Debug log write failed!" ) ;
#endif
  }
  else {
    HQFAIL( "Debug output failing over to monitor" ) ;
    monitorf(( uint8* )"%.*s" , len , rr_state->dbg_buf ) ;
  }

  rr_state->dbg_indented = FALSE ;
}

/** Print everything on the stack to the current debug log. */

static void pdf_rr_log_stack( PDF_IMC_PARAMS *imc , RR_STATE *rr_state ,
                              void *op , Bool skip )
{
  if (( debug_retained_raster &
        ( DEBUG_RR_OPERANDS | DEBUG_RR_OPERATORS )) != 0 ) {
    pdf_rr_debug_indent( rr_state , rr_state->dbg_gstate_depth ) ;
    if ( skip ) {
      ( void )file_write( pdf_rr_debug_file( rr_state ) ,
                          ( uint8 * )"%% " , 3 ) ;
    }
  }

  if (( debug_retained_raster & DEBUG_RR_OPERANDS ) != 0 ) {
    int32 si ;

    for ( si = 0 ; si <= imc->pdfstack.size ; si++ ) {
      OBJECT *param = & theIFrameOList( imc->pdfstack.fptr )[ si ] ;

      debug_print_object_indented( param , NULL , " " ,
                                   pdf_rr_debug_file( rr_state )) ;
    }
  }

  RR_DBG( OPERATORS ,
          ( rr_state , ( uint8 * )"%s\n" , pdf_op_name( op ))) ;
}

/** For human-readable debug messages. */
static char rr_mode_names[ 9 ][ 9 ] = {
  "normal" ,
  "skip" ,
  "prescan" ,
  "scan" ,
  "postscan" ,
  "store" ,
  "export" ,
  "replay" ,
  "flush"
} ;
#else
#define RR_DBG( _switch , _args ) EMPTY_STATEMENT()
#define RR_DBG_INDENT( _switch , _rr_state , _depth ) EMPTY_STATEMENT()
#define RR_DBG_OPEN( _switch , _rr_state , _phase , \
                     _page_num , _mark ) EMPTY_STATEMENT()
#define RR_DBG_CLOSE( _switch , _rr_state ) EMPTY_STATEMENT()
#define RR_DBG_STACK( _imc , _rr_state , _op , _skip ) EMPTY_STATEMENT()
#endif

#if defined( DEBUG_BUILD )
/** MM root scanner for RR debug file. */

static mps_res_t MPS_CALL pdf_rr_root_scan( mps_ss_t ss , void *p , size_t s )
{
  UNUSED_PARAM( size_t , s ) ;

  return ps_scan_stack( ss , p );
}
#endif

#ifdef METRICS_BUILD
typedef struct pdf_rr_metrics_t {
  /* The prototype for sw_metric_integer uses int32, even though it
     might seem more natural to use int. */
  int32 inline_image_count ;
  int32 internal_uncacheable_count ;
  int32 pattern_count ;
  int32 form_count ;
  int32 form_matrix_count ;
  int32 form_ref_count ;
  int32 image_count ;
  int32 significant_image_count ;
  int32 pages_variable ;
  int32 pages_fixed ;
  int32 pages_combined ;
  int32 pages_total ;
  int32 element_count ;
} pdf_rr_metrics_t ;

pdf_rr_metrics_t pdf_rr_metrics ;

static Bool pdf_rr_metrics_update( sw_metrics_group *metrics )
{
  if ( ! sw_metrics_open_group( & metrics ,
                                METRIC_NAME_AND_LENGTH( "HVD" ))) {
    return FALSE ;
  }

  SW_METRIC_INTEGER( "InlineImages" , pdf_rr_metrics.inline_image_count ) ;
  SW_METRIC_INTEGER( "IRRUncacheableElements" ,
                     pdf_rr_metrics.internal_uncacheable_count ) ;
  SW_METRIC_INTEGER( "PatternCount" , pdf_rr_metrics.pattern_count ) ;
  SW_METRIC_INTEGER( "FormCount" , pdf_rr_metrics.form_count ) ;
  SW_METRIC_INTEGER( "FormMatrixCount" , pdf_rr_metrics.form_matrix_count ) ;
  SW_METRIC_INTEGER( "FormRefCount" , pdf_rr_metrics.form_ref_count ) ;
  SW_METRIC_INTEGER( "ImageCount" , pdf_rr_metrics.image_count ) ;
  SW_METRIC_INTEGER( "SignificantImageCount" ,
                     pdf_rr_metrics.significant_image_count ) ;
  SW_METRIC_INTEGER( "PagesVariable" , pdf_rr_metrics.pages_variable ) ;
  SW_METRIC_INTEGER( "PagesFixed" , pdf_rr_metrics.pages_fixed ) ;
  SW_METRIC_INTEGER( "PagesCombined" , pdf_rr_metrics.pages_combined ) ;
  SW_METRIC_INTEGER( "PagesTotal" , pdf_rr_metrics.pages_total ) ;
  SW_METRIC_INTEGER( "ElementCount" , pdf_rr_metrics.element_count ) ;

  sw_metrics_close_group( & metrics ) ;

  return TRUE ;
}

static void pdf_rr_metrics_reset( int reason )
{
  pdf_rr_metrics_t init = { 0 } ;

  UNUSED_PARAM( int , reason ) ;

  pdf_rr_metrics = init ;
}

static sw_metrics_callbacks pdf_rr_metrics_hook = {
  pdf_rr_metrics_update ,
  pdf_rr_metrics_reset ,
  NULL
} ;
#endif

/** C globals initialisation. */

void init_C_globals_pdfrr( void )
{
#ifdef METRICS_BUILD
  pdf_rr_metrics_reset( SW_METRICS_RESET_BOOT ) ;
  sw_metrics_register( & pdf_rr_metrics_hook ) ;
#endif
}

/** We were expecting some Page Complete events, but something's
    happened which means we no longer should. */

static void pdf_rr_reset_pages_remaining( RR_STATE *rr_state )
{
  HQASSERT( rr_state != NULL , "Null rr_state" ) ;

  multi_mutex_lock( & rr_state->mutex ) ;
  if ( rr_state->pages_remaining > 0 ) {
    rr_state->pages_remaining = 0 ;
    multi_condvar_broadcast( & rr_state->condvar ) ;
  }
  multi_mutex_unlock( & rr_state->mutex ) ;
}

/** Set the given page to the complete state: deccrement the number of
    Page Complete events we're expecting if appropriate. The
    page_index parameter has already taken page_offset into account
    (i.e. it's an index into the current chunk of pages). */

static void pdf_rr_set_page_complete( RR_STATE *rr_state , uint32 page_index ,
                                      Bool need_lock )
{
  HQASSERT( rr_state != NULL , "Null rr_state" ) ;

  if ( need_lock ) {
    multi_mutex_lock( & rr_state->mutex ) ;
  }

  /* The page may already be marked as complete. Note that this can
     happen when using IRR and GGDUMB[01] debug mode, but it's
     harmless and not worth breaking data encapsulation to fix because
     it's just an internal testing mode. This is also a backstop for
     erronous non-debug-build behaviour where for whatever reason we
     see more than one page complete event for a given page. It's also
     possible that a page goes straight from WAITING to COMPLETE if
     the cache implementation tells us it's done very early (it's all
     asynchronous). */

  if ( rr_state->pages[ page_index ].state == RR_PAGE_READY ) {
    if ( rr_state->pages_remaining > 0 ) {
      rr_state->pages_remaining-- ;
      if ( rr_state->pages_remaining == 0 ) {
        multi_condvar_broadcast( & rr_state->condvar ) ;
      }
    }
    else {
      HQFAIL( "Underflowing pages_remaining!" ) ;
    }

    rr_state->pages[ page_index ].state = RR_PAGE_COMPLETE ;
  }
  else {
    rr_state->pages[ page_index ].state = RR_PAGE_COMPLETE_EARLY ;
  }

  if ( need_lock ) {
    multi_mutex_unlock( & rr_state->mutex ) ;
  }
}

/** Set the given page to the ready state: increment the number of
    Page Complete events we're expecting if appropriate. The
    page_index parameter has already taken page_offset into account
    (i.e. it's an index into the current chunk of pages). */

static Bool pdf_rr_set_page_ready( RR_STATE *rr_state , uint32 page_index )
{
  Bool complete_early = FALSE ;
  SWMSG_RR_PAGE_REF pr ;

  HQASSERT( rr_state != NULL , "Null rr_state" ) ;

  /* Either we're done exporting all the elements for the page
     at hand (ERR) or we're just going to RIP a normal page
     because nothing is cached. In either of these cases, the
     right thing to do is to issue a "page ready" event. */

  pr.connection = rr_state->cache ;
  pr.timeline = rr_state->tl_ref ;
  pr.page_index = page_index ;

  if ( SwEvent( SWEVT_RR_PAGE_READY , & pr , sizeof( pr )) >=
       SW_EVENT_ERROR ) {
    return FALSE ;
  }

  /* The Page Ready event is purely informational: the cache
     implementation doesn't have to handle it if it doesn't want
     to. So UNHANDLED is acceptable here. */

  multi_mutex_lock( & rr_state->mutex ) ;

  /* If the page is already marked as complete then we don't need to
     wait for it. */
  if ( rr_state->pages[ page_index ].state != RR_PAGE_COMPLETE ) {
    complete_early =
      ( rr_state->pages[ page_index ].state == RR_PAGE_COMPLETE_EARLY ) ;

    rr_state->pages[ page_index ].state = RR_PAGE_READY ;
  }

  if ( complete_early ) {
    pdf_rr_set_page_complete( rr_state , page_index , FALSE ) ;
  }

  multi_mutex_unlock( & rr_state->mutex ) ;

  return TRUE ;
}

/** Event handler for noticing when a timeline is starting. We need to
    track the beginning of rendering for an IRR replaying showpage. */

static sw_event_result HQNCALL pdf_rr_timeline_start( void *context ,
                                                      sw_event *evt )
{
  SWMSG_TIMELINE * msg = evt->message ;
  RR_STATE * rr_state = context ;

  if ( msg == NULL || evt->length < sizeof( SWMSG_TIMELINE )) {
    return SW_EVENT_CONTINUE ;
  }

  switch ( msg->type ) {
    case SWTLT_RENDER_PAGE:
      {
        DL_STATE *page = ( DL_STATE * )msg->context ;

        /* We only care if this is an IRR replaying showpage. */

        if ( page->irr.state != NULL && ! page->irr.generating ) {
          if ( ! pdf_rr_set_page_ready( rr_state ,
                 ( uint32 )irr_getprivid( page->irr.state ))) {
            HQFAIL( "Setting page to ready failed" ) ;
          }
        }
      }
      break ;

    case SWTLT_INTERPRET_PAGE:
      if (SwTimelineOfType(msg->parent, SWTLT_JOB) == rr_state->job)
        rr_state->tl_ref = msg->ref ;
      break ;
  }

  return SW_EVENT_CONTINUE ;
}

/** Event handler for noticing when a timeline has been aborted for
    whatever reason. There might be some tidying up to do. */

static sw_event_result HQNCALL pdf_rr_timeline_aborted( void *context ,
                                                        sw_event *evt )
{
  SWMSG_TIMELINE *msg = evt->message ;
  RR_STATE *rr_state = context ;

  if ( msg == NULL || evt->length < sizeof( SWMSG_TIMELINE )) {
    return SW_EVENT_CONTINUE ;
  }

  switch ( msg->type ) {
    case SWTLT_INTERPRET_PAGE:
    case SWTLT_RENDER_PAGE:
      if ( msg->ref == rr_state->tl_ref ) {
        rr_state->tl_ref = rr_state->job ;
      }
      /*@fallthrough@*/

    case SWTLT_SCANNING_PAGES:
    case SWTLT_JOB:
      pdf_rr_reset_pages_remaining( rr_state ) ;
      break ;
  }

  return SW_EVENT_CONTINUE ;
}

/** Event handler for noticing when a timeline has ended. We need to
    track completed rendering of an IRR replaying showpage. */

static sw_event_result HQNCALL pdf_rr_timeline_ended( void *context ,
                                                      sw_event *evt )
{
  SWMSG_TIMELINE *msg = evt->message ;
  RR_STATE *rr_state = context ;

  if ( msg == NULL || evt->length < sizeof( SWMSG_TIMELINE )) {
    return SW_EVENT_CONTINUE ;
  }

  switch ( msg->type ) {
    case SWTLT_INTERPRET_PAGE:
      /* Our reference is about to become invalid */
      if ( msg->ref == rr_state->tl_ref ) {
        rr_state->tl_ref = rr_state->job ;
      }
      break ;

    case SWTLT_RENDER_PAGE:
      {
        DL_STATE *page = ( DL_STATE * )msg->context ;

        if ( msg->ref == rr_state->tl_ref ) {
          rr_state->tl_ref = rr_state->job ;
        }

        /* Interested in non-capturing page completions only. */
        if ( page->irr.state != NULL && ! page->irr.generating ) {
          SWMSG_RR_PAGE_REF pc ;

          pc.connection = rr_state->cache ;
          pc.timeline = rr_state->tl_ref ;
          pc.page_index = ( uint32 )irr_getprivid( page->irr.state ) ;

          if ( SwEvent( SWEVT_RR_PAGE_COMPLETE , & pc , sizeof( pc )) >=
               SW_EVENT_ERROR ) {
            /* There's not much we can do if the page completion
               failed. We certainly don't want to stop the timeline
               ended event. */
            HQFAIL( "RR page completion failed" ) ;
          }
          /* SW_EVENT_UNHANDLED is not an error here. */
        }
      }
      break ;
  }

  return SW_EVENT_CONTINUE ;
}

/** Event handler for Page Complete events. We have to be able to cope
    with cache instances telling us asynchronously when they're done
    with a given page: for ERR this will be when all the raster
    elements on the page have finally arrived and been packaged up for
    stitching into the final output page. For IRR it will be when the
    Replaying phase is done. In the dumb debug modes, it's just as
    soon as the Page Ready event has arrived. */

static sw_event_result HQNCALL pdf_rr_page_complete( void *context ,
                                                     sw_event *evt )
{
  SWMSG_RR_PAGE_REF *pc = evt->message ;
  RR_STATE *rr_state = context ;
  RR_PAGE *page ;
  uint32 i ;

  if ( pc == NULL || evt->length < sizeof( SWMSG_RR_PAGE_REF ) ||
       pc->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_PAGE_COMPLETE "
                     "                                         %d\n" ,
                     pc->page_index + 1 )) ;

  HQASSERT( pc->page_index >= 0 && pc->page_index < rr_state->page_count ,
            "Page index out of range" ) ;

#if defined( DEBUG_BUILD )
  /* For now, fake the same flavour message as the existing
     Rendered Page messages, which come from PS hooks. */
  /** \todo FIXME +500 to be formalised once this becomes an API */
  emonitorf(rr_state->job, MON_CHANNEL_MONITOR, MON_TYPE_JOB + 500,
            (uint8 *)("%%%%[Completed HVD Page #%d]%%%%\n"),
            1 + pc->page_index ) ,
#endif

  page = & rr_state->pages[ pc->page_index ] ;

  for ( i = 0 ; i < page->element_count ; i++ ) {
    SWMSG_RR_ELEMENT_UPDATE_HITS uh ;

    uh.connection = rr_state->cache ;
    uh.timeline = rr_state->tl_ref ;
    uh.id = page->elements[ i ].id ;
    uh.raise = FALSE ;
    uh.hits_delta = 1u ;

    if ( SwEvent( SWEVT_RR_ELEMENT_UPDATE_HITS , & uh ,
                  sizeof( uh )) >= SW_EVENT_ERROR ) {
      return FAILURE( SW_EVENT_ERROR ) ;
    }
    /* SW_EVENT_UNHANDLED is not an error here. */
  }

  for ( i = 0 ; i < page->element_count ; i++ ) {
    SWMSG_RR_ELEMENT_REF eu ;

    eu.connection = rr_state->cache ;
    eu.timeline = rr_state->tl_ref ;
    eu.id = page->elements[ i ].id ;

    if ( SwEvent( SWEVT_RR_ELEMENT_UNLOCK , & eu ,
                  sizeof( eu )) >= SW_EVENT_ERROR ) {
      return FAILURE( SW_EVENT_ERROR ) ;
    }
    /* SW_EVENT_UNHANDLED is not an error here. */
  }

  pdf_rr_set_page_complete( rr_state , pc->page_index , TRUE ) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_HANDLED ;
}

#if defined( DEBUG_BUILD )
/** Handler for page define events. Just for debug logging. */

static sw_event_result HQNCALL pdf_rr_page_define( void *context ,
                                                   sw_event *evt )
{
  SWMSG_RR_PAGE_DEFINE *pd = evt->message ;
  RR_STATE *rr_state = context ;

  if ( pd == NULL || evt->length < sizeof( SWMSG_RR_PAGE_DEFINE ) ||
       pd->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  if (( debug_retained_raster & DEBUG_RR_EVENTS ) != 0 ) {
    uint32 i ;

    RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
    RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_PAGE_DEFINE "
                       "- %d page%s {\n" , pd->page_count ,
                       pd->page_count > 1 ? "s" : "" )) ;

    for ( i = 0 ; i < pd->page_count ; i++ ) {
      uint32 j ;

      RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%%   Page %d {\n" , i )) ;
      for ( j = 0 ; j < pd->pages[ i ].element_count ; j++ ) {
        uint8 *hash = pd->pages[ i ].elements[ j ].id ;
        RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%%     "
                           "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                           "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
                           hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
                           hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
                           hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
                           hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ])) ;
      }
      RR_DBG( ANY , ( rr_state , ( uint8 * )"%%%%   }\n" )) ;
    }

    RR_DBG( ANY , ( rr_state , ( uint8 * )"%%%% }\n" )) ;
    RR_DBG_CLOSE( EVENTS , rr_state ) ;
  }

  return SW_EVENT_CONTINUE ;
}

/** Handler for page ready events. Just for debug logging. */

static sw_event_result HQNCALL pdf_rr_page_ready( void *context ,
                                                  sw_event *evt )
{
  SWMSG_RR_PAGE_REF *pr = evt->message ;
  RR_STATE *rr_state = context ;

  if ( pr == NULL || evt->length < sizeof( SWMSG_RR_PAGE_REF ) ||
       pr->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_PAGE_READY    "
                     "                                         %d\n" ,
                     pr->page_index + 1 )) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_CONTINUE ;
}

/** Handler for element define events. Just for debug logging. */

static sw_event_result HQNCALL pdf_rr_element_define( void *context ,
                                                      sw_event *evt )
{
  SWMSG_RR_ELEMENT_DEFINE *ed = evt->message ;
  RR_STATE *rr_state = context ;

  if ( ed == NULL || evt->length < sizeof( SWMSG_RR_ELEMENT_DEFINE ) ||
       ed->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_ELEMENT_DEFINE      "
                     "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                     "%02x%02x%02x%02x%02x%02x%02x%02x [%d,%d,%d,%d]\n" ,
                     ed->id[ 0 ] , ed->id[ 1 ] , ed->id[ 2 ] , ed->id[ 3 ] ,
                     ed->id[ 4 ] , ed->id[ 5 ] , ed->id[ 6 ] , ed->id[ 7 ] ,
                     ed->id[ 8 ] , ed->id[ 9 ] , ed->id[ 10 ] , ed->id[ 11 ] ,
                     ed->id[ 12 ] , ed->id[ 13 ] , ed->id[ 14 ] , ed->id[ 15 ] ,
                     ed->x1 , ed->y1 , ed->x2 , ed->y2 )) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_CONTINUE ;
}

/** Handler for element lock events. Just for debug logging. */

static sw_event_result HQNCALL pdf_rr_element_lock( void *context ,
                                                    sw_event *evt )
{
  SWMSG_RR_ELEMENT_REF *er = evt->message ;
  RR_STATE *rr_state = context ;

  if ( er == NULL || evt->length < sizeof( SWMSG_RR_ELEMENT_REF ) ||
       er->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_ELEMENT_LOCK        "
                     "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                     "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
                     er->id[ 0 ] , er->id[ 1 ] , er->id[ 2 ] , er->id[ 3 ] ,
                     er->id[ 4 ] , er->id[ 5 ] , er->id[ 6 ] , er->id[ 7 ] ,
                     er->id[ 8 ] , er->id[ 9 ] , er->id[ 10 ] , er->id[ 11 ] ,
                     er->id[ 12 ] , er->id[ 13 ] ,
                     er->id[ 14 ] , er->id[ 15 ])) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_CONTINUE ;
}

/** Handler for element unlock events. Just for debug logging. */

static sw_event_result HQNCALL pdf_rr_element_unlock( void *context ,
                                                      sw_event *evt )
{
  SWMSG_RR_ELEMENT_REF *er = evt->message ;
  RR_STATE *rr_state = context ;

  if ( er == NULL || evt->length < sizeof( SWMSG_RR_ELEMENT_REF ) ||
       er->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_ELEMENT_UNLOCK      "
                     "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                     "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
                     er->id[ 0 ] , er->id[ 1 ] , er->id[ 2 ] , er->id[ 3 ] ,
                     er->id[ 4 ] , er->id[ 5 ] , er->id[ 6 ] , er->id[ 7 ] ,
                     er->id[ 8 ] , er->id[ 9 ] , er->id[ 10 ] , er->id[ 11 ] ,
                     er->id[ 12 ] , er->id[ 13 ] ,
                     er->id[ 14 ] , er->id[ 15 ])) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_CONTINUE ;
}

/** Handler for element pending events. Just for debug logging. */

static sw_event_result HQNCALL pdf_rr_element_pending( void *context ,
                                                       sw_event *evt )
{
  SWMSG_RR_ELEMENT_REF *er = evt->message ;
  RR_STATE *rr_state = context ;

  if ( er == NULL || evt->length < sizeof( SWMSG_RR_ELEMENT_REF ) ||
       er->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_ELEMENT_PENDING     "
                     "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                     "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
                     er->id[ 0 ] , er->id[ 1 ] , er->id[ 2 ] , er->id[ 3 ] ,
                     er->id[ 4 ] , er->id[ 5 ] , er->id[ 6 ] , er->id[ 7 ] ,
                     er->id[ 8 ] , er->id[ 9 ] , er->id[ 10 ] , er->id[ 11 ] ,
                     er->id[ 12 ] , er->id[ 13 ] ,
                     er->id[ 14 ] , er->id[ 15 ])) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_CONTINUE ;
}

/** Handler for element query events. Just for debug logging. */

static sw_event_result HQNCALL pdf_rr_element_query( void *context ,
                                                     sw_event *evt )
{
  SWMSG_RR_ELEMENT_QUERY *eq = evt->message ;
  RR_STATE *rr_state = context ;

  if ( eq == NULL || evt->length < sizeof( SWMSG_RR_ELEMENT_QUERY ) ||
       eq->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_ELEMENT_QUERY       "
                     "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                     "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
                     eq->id[ 0 ] , eq->id[ 1 ] , eq->id[ 2 ] , eq->id[ 3 ] ,
                     eq->id[ 4 ] , eq->id[ 5 ] , eq->id[ 6 ] , eq->id[ 7 ] ,
                     eq->id[ 8 ] , eq->id[ 9 ] , eq->id[ 10 ] , eq->id[ 11 ] ,
                     eq->id[ 12 ] , eq->id[ 13 ] ,
                     eq->id[ 14 ] , eq->id[ 15 ])) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_CONTINUE ;
}

/** Handler for element update raster events. Just for debug
    logging. */

static sw_event_result HQNCALL pdf_rr_element_update_raster( void *context ,
                                                             sw_event *evt )
{
  SWMSG_RR_ELEMENT_UPDATE_RASTER *ur = evt->message ;
  RR_STATE *rr_state = context ;

  if ( ur == NULL || evt->length < sizeof( SWMSG_RR_ELEMENT_UPDATE_RASTER ) ||
       ur->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state ,
                     ( uint8 * )"%%%% SWEVT_RR_ELEMENT_UPDATE_RASTER        "
                     "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                     "%02x%02x%02x%02x%02x%02x%02x%02x %p (%u bytes)\n" ,
                     ur->id[ 0 ] , ur->id[ 1 ] , ur->id[ 2 ] , ur->id[ 3 ] ,
                     ur->id[ 4 ] , ur->id[ 5 ] , ur->id[ 6 ] , ur->id[ 7 ] ,
                     ur->id[ 8 ] , ur->id[ 9 ] , ur->id[ 10 ] , ur->id[ 11 ] ,
                     ur->id[ 12 ] , ur->id[ 13 ] , ur->id[ 14 ] , ur->id[ 15 ] ,
                     ur->raster , ur->size )) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_CONTINUE ;
}

/** Handler for element update hits events. Just for debug logging. */

static sw_event_result HQNCALL pdf_rr_element_update_hits( void *context ,
                                                           sw_event *evt )
{
  SWMSG_RR_ELEMENT_UPDATE_HITS *uh = evt->message ;
  RR_STATE *rr_state = context ;

  if ( uh == NULL || evt->length < sizeof( SWMSG_RR_ELEMENT_UPDATE_HITS ) ||
       uh->connection != rr_state->cache ) {
    /* This event isn't for me. */
    return SW_EVENT_CONTINUE ;
  }

  RR_DBG_OPEN( EVENTS , rr_state , "events" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( EVENTS , ( rr_state , ( uint8 * )"%%%% SWEVT_RR_ELEMENT_UPDATE_HITS "
                     "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                     "%02x%02x%02x%02x%02x%02x%02x%02x %c%u\n" ,
                     uh->id[ 0 ] , uh->id[ 1 ] , uh->id[ 2 ] , uh->id[ 3 ] ,
                     uh->id[ 4 ] , uh->id[ 5 ] , uh->id[ 6 ] , uh->id[ 7 ] ,
                     uh->id[ 8 ] , uh->id[ 9 ] , uh->id[ 10 ] , uh->id[ 11 ] ,
                     uh->id[ 12 ] , uh->id[ 13 ] , uh->id[ 14 ] , uh->id[ 15 ] ,
                     uh->raise ? '+' : '-' , uh->hits_delta )) ;
  RR_DBG_CLOSE( EVENTS , rr_state ) ;

  return SW_EVENT_CONTINUE ;
}
#endif

/** Main initialisation. */

Bool pdf_rr_init( void )
{
#if defined( DEBUG_BUILD )
  register_ripvar( NAME_debug_rr , OINTEGER , & debug_retained_raster ) ;
  register_ripvar( NAME_tweak_rr , OINTEGER , & tweak_retained_raster ) ;
#endif

  return TRUE ;
}

/** Tear-down. */

void pdf_rr_finish( void )
{
}

/** Allocation function for hashing. */

/*@null@*/ /*@out@*/ /*@only@*/
static mm_addr_t pdf_rr_alloc( mm_size_t size , /*@null@*/ void *data )
  /*@ensures MaxSet(result) == (size - 1); @*/
{
  mm_pool_t pool = ( mm_pool_t )data ;
  mm_addr_t *result ;

  result = mm_alloc( pool , size , MM_ALLOC_CLASS_PDF_RR_HASHTREE ) ;

  if ( result == NULL ) {
    ( void )error_handler( VMERROR ) ;
  }

  return result ;
}

/** Free function for hashing. */

static void pdf_rr_free( /*@out@*/ /*@only@*/ mm_addr_t what ,
                         mm_size_t size , /*@null@*/ void *data )
{
  mm_free(( mm_pool_t )data , what , size ) ;
}

/** Compare two hashes (used as a callback in building red-black trees
    keyed on them). Same as strcmp and qsort callbacks, returns < 0 if
    key1 < key2 etc. */

static int32 pdf_rr_compare( RBT_ROOT *root , uintptr_t key1 ,
                             uintptr_t key2 )
{
  const uint32 *hash1 = ( const uint32 * )key1 ;
  const uint32 *hash2 = ( const uint32 * )key2 ;

  UNUSED_PARAM( struct rbt_root * , root ) ;

  if ( hash1[ 0 ] < hash2[ 0 ] ) {
    return -1 ;
  }
  else if ( hash1[ 0 ] > hash2[ 0 ] ) {
    return 1 ;
  }

  if ( hash1[ 1 ] < hash2[ 1 ] ) {
    return -1 ;
  }
  else if ( hash1[ 1 ] > hash2[ 1 ] ) {
    return 1 ;
  }

  if ( hash1[ 2 ] < hash2[ 2 ] ) {
    return -1 ;
  }
  else if ( hash1[ 2 ] > hash2[ 2 ] ) {
    return 1 ;
  }

  if ( hash1[ 3 ] < hash2[ 3 ] ) {
    return -1 ;
  }
  else if ( hash1[ 3 ] > hash2[ 3 ] ) {
    return 1 ;
  }

  return 0 ;
}

/** Update the retained raster hash (if appropriate) with the given
    buffer full of data. */

static void pdf_rr_hash( RR_HASH_STATE *state , void *buf , uint32 len )
{
  if ( state != NULL && buf != NULL ) {
    MurmurHash3_128( buf , len , state->hash , & state->hash ) ;
  }
}

/** Blank the hash state. A separate function so we can re-use
    existing structures. */

static void pdf_rr_hash_reset( PDF_IXC_PARAMS *ixc , RR_HASH_STATE *state )
{
  HQASSERT( state != NULL , "No RR state" ) ;

  HqMemZero((uint8 *)state->hash , sizeof( state->hash )) ;

  /* The initial value of every hash is derived from the setup
     ID. This is central to inter-job caching. */

  pdf_rr_hash( state , oString( *ixc->OptimizedPDFSetupID ) ,
               ( uint32  )theLen( *ixc->OptimizedPDFSetupID )) ;
}

/** Deep copy one state into another. */

static void pdf_rr_hash_copy( RR_HASH_STATE *src , RR_HASH_STATE *dest )
{
  HQASSERT( src != NULL && dest != NULL , "Null pointer in pdf_rr_hash_copy" ) ;

  HqMemCpy((uint8 *)dest->hash , (uint8 *)src->hash , sizeof( dest->hash )) ;
}

/** Finalise the retained raster page hash and copy the calculated
    value to the given destination. Doesn't free anything, so hash
    calculation can continue later with this state if required. */

void pdf_rr_hash_finalise( RR_HASH_STATE *state , uint8 *out )
{
  if ( state != NULL ) {
    HqMemCpy( out , state->hash , RR_HASH_LENGTH ) ;
  }
}

/** Stack-borne parameters passed throughout the hashing of PDF
    objects - arising both from resource trees and content streams. */

typedef struct RR_HASH_PARAMS {
  /** The owning PDF context. */
  PDFCONTEXT *pdfc ;

  /** The hashing state associated with this tier of hashing. */
  RR_HASH_STATE hash_state ;

  /** Scanning of both resources and stacks is recursive: this is the
      pointer to the parent params, or NULL if this is the
      outermost. */
  struct RR_HASH_PARAMS *parent ;

  union {
    /** Opaque reference to the operator involved when scanning the
        stack. */
    void *op ;

    /** Pointer to the key for the entry in the parent resource
        dictionary when scanning the resources tree. */
    OBJECT *pkey ;
  } u ;

  /** Flag which is set when the resource scanned is an XObject, or
      the operand passed to \c pdf_rr_hash_operand() was the name of a
      Form Xobject resource. */
  unsigned is_form:1 ;

  /** Flag which is set when the Form XObject has a /Matrix. */
  unsigned has_matrix:1 ;

  /** Flag which is set when the Form XObject has a /Ref. */
  unsigned has_ref:1 ;

  /** Flag which is set iff the object is an Image XObject. */
  unsigned is_image:1 ;

  /** Stream length for this level of recursion (or zero if none has
      been encountered). */
  int32 stream_len ;
}
RR_HASH_PARAMS ;

static Bool pdf_rr_hash_operand( RR_HASH_PARAMS *params , OBJECT *theo ) ;
static void pdf_rr_hash_object( RR_HASH_PARAMS *params , OBJECT *theo ) ;

/** The dictionary walk callback for \c pdf_rr_hash_operand. */

static Bool pdf_rr_hash_op_dictwalkfn( OBJECT *thek , OBJECT *theo ,
                                       void *priv )
{
  RR_HASH_PARAMS *params = ( RR_HASH_PARAMS * )priv ;

  pdf_rr_hash_object( params , thek ) ;

  return pdf_rr_hash_operand( params , theo ) ;
}

/** Calculate the hash for the given PDF object. This is only for
    simple objects: composite objects needing recursion should be
    handled by the caller. */

static void pdf_rr_hash_object( RR_HASH_PARAMS *params , OBJECT *theo )
{
  PDFCONTEXT *pdfc = params->pdfc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  uint8 type ;
  uintptr_t d1 ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  HQASSERT( rr_state != NULL , "Shouldn't be here without an RR state" ) ;
  type = oType( *theo ) ;

  switch ( type ) {
    case ONULL:
      /* As a special case since null has no value by definition, we
         hash the object type. */
      pdf_rr_hash( & params->hash_state , & type , sizeof( type )) ;
      break ;
    case ONOTHING:
    case OINFINITY:
    case OOPERATOR:
    case OMARK:
    case OFONTID:
    case OSAVE:
    case OGSTATE:
      HQFAIL( "Hashing this type shouldn't be necessary, should it?" ) ;
      RR_DBG( HASH_INNARDS ,
              ( rr_state , ( uint8 * )"%%## Unsupported object type!\n" )) ;
      break ;
    case ODICTIONARY:
      HQFAIL( "Dictionaries not expected here." ) ;
      break ;
    case OFILE:
      HQFAIL( "Streams not expected here." ) ;
      break ;
    case OARRAY:
    case OPACKEDARRAY:
      HQFAIL( "Arrays not expected here." ) ;
      break ;
    case OINTEGER:
    case OREAL:
    case OBOOLEAN:
      d1 = OBJECT_GET_D1( *theo ) ;
      pdf_rr_hash( & params->hash_state , & d1 , sizeof( d1 )) ;
      break ;
    case OINDIRECT:
      HQFAIL( "Indirect objects should have been resolved upstream." ) ;
      break ;
    case ONAME:
      pdf_rr_hash( & params->hash_state , theICList( oName( *theo )) ,
                   oName( *theo )->len ) ;
      break ;
    case OSTRING:
      pdf_rr_hash( & params->hash_state , oString( *theo ) , theLen( *theo )) ;
      break ;
    case OLONGSTRING:
      pdf_rr_hash( & params->hash_state , theLSCList(*oLongStr( *theo )) ,
                   theLSLen(*oLongStr( *theo ))) ;
      break ;
    case OFILEOFFSET:
      pdf_rr_hash( & params->hash_state , & theLen( *theo ) ,
                   sizeof( theLen( *theo ))) ;
      d1 = OBJECT_GET_D1( *theo ) ;
      pdf_rr_hash( & params->hash_state , & d1 , sizeof( d1 )) ;
      break ;
  }

#if defined( DEBUG_BUILD )
  if (( debug_retained_raster & DEBUG_RR_HASH_INNARDS ) != 0 ) {
    RR_DBG( HASH_INNARDS , ( rr_state , ( uint8 * )"%%## " )) ;
    if ( ! isPSCompObj( *theo )) {
      debug_print_object_indented( theo , NULL , " " ,
                                   pdf_rr_debug_file( rr_state )) ;
    }
    RR_DBG( HASH_INNARDS , ( rr_state , ( uint8 * )"\n" )) ;
  }
#endif
}

/** During scanning, this routine is used to calculate the hash for
    the given PDF operand to the operator \c op. This is a stateful
    operation: we have to know the operator for which this is an
    operand, since we must know what resource type to look up. */

static Bool pdf_rr_hash_operand( RR_HASH_PARAMS *params , OBJECT *theo )
{
  PDFCONTEXT *pdfc = params->pdfc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL ) {
    HQFAIL( "Shouldn't be here without an RR state" ) ;
    return TRUE ;
  }
  else if ( theo == NULL ) {
    /* Can happen if we're hashing a PDF_PAGEDEV. */
    return TRUE ;
  }

  params->is_form = FALSE ;
  params->has_matrix = FALSE ;
  params->has_ref = FALSE ;
  params->is_image = FALSE ;
  params->stream_len = 0 ;

  switch ( oType( *theo )) {
    case ONOTHING:
    case OINFINITY:
    case OOPERATOR:
    case OMARK:
    case ONULL:
    case OFONTID:
    case OSAVE:
    case OGSTATE:
    case OINTEGER:
    case OREAL:
    case OBOOLEAN:
    case OSTRING:
    case OLONGSTRING:
    case OFILEOFFSET:
      pdf_rr_hash_object( params , theo ) ;
      break ;

    case ODICTIONARY:
      {
        RR_HASH_PARAMS inner_params = *params ;

        inner_params.parent = params ;
        inner_params.is_form = FALSE ;
        inner_params.has_matrix = FALSE ;
        inner_params.has_ref = FALSE ;
        inner_params.is_image = FALSE ;
        inner_params.stream_len = 0 ;

        RR_DBG( HASH_INNARDS ,
                ( rr_state , ( uint8 * )"%%## Recursing into dictionary\n" )) ;

        if ( ! walk_dictionary_sorted( theo , pdf_rr_hash_op_dictwalkfn ,
                                       & inner_params )) {
          return FAILURE( FALSE ) ;
        }
      }
      break ;

    case OFILE:
      HQFAIL( "Shouldn't see streams as direct operands" ) ;
      break ;
    case OARRAY:
    case OPACKEDARRAY:
      {
        int32 len = theLen(*theo) ;
        OBJECT *olist = oArray( *theo ) ;
        int32 i ;

        RR_DBG( HASH_INNARDS ,
                ( rr_state , ( uint8 * )"%%## Iterating over array\n" )) ;

        for ( i = 0 ; i < len ; i ++ ) {
          if ( ! pdf_rr_hash_operand( params , olist++ )) {
            return FALSE ;
          }
        }
      }
      break ;
    case OINDIRECT:
      HQFAIL( "Shouldn't see indirect references as operands" ) ;
      break ;
    case ONAME:
      {
        uint16 type = NAME_ProcSet ;
        OBJECT res ;
        uint8 pdfop = pdf_op_number( params->u.op ) ;
        Bool hashed = FALSE ;

        switch ( pdfop ) {
          case PDFOP_cs:
          case PDFOP_CS :
            switch ( oNameNumber( *theo )) {
              case NAME_DeviceGray:
              case NAME_DeviceRGB:
              case NAME_DeviceCMYK:
              case NAME_Pattern:
              case NAME_Lab:
              case NAME_CalGray:
              case NAME_CalRGB:
              case NAME_ICCBased:
              case NAME_Indexed:
              case NAME_Separation:
              case NAME_DeviceN:
                pdf_rr_hash( & params->hash_state , theICList( oName( *theo )) ,
                             theINLen( oName( *theo ))) ;
                hashed = TRUE ;
                break ;

              default:
                type = NAME_ColorSpace ;
                break ;
            }
            break ;

          case PDFOP_scn:
          case PDFOP_SCN:
            type = NAME_Pattern ;
            break ;

          case PDFOP_gs:
            type = NAME_ExtGState ;
            break ;

          case PDFOP_sh:
            type = NAME_Shading ;
            break ;

          case PDFOP_Do:
            type = NAME_XObject ;
            break ;

          case PDFOP_Tf:
            type = NAME_Font ;
            break ;

          default:
            if ( pdfop == PDFOP_ri || rr_state->in_inline_image ) {
              /* The ri operator takes a literal name, not a named
                 resource. Equally the parameters of inline images should
                 be hashed literally. */
              pdf_rr_hash( & params->hash_state , theICList( oName( *theo )) ,
                           theINLen( oName( *theo ))) ;
              hashed = TRUE ;
            }
            break ;
        }

        if ( ! hashed && type != NAME_ProcSet ) {
          if ( ! pdf_get_resourceid( pdfc , type , theo , & res )) {
            return FAILURE( FALSE ) ;
          }

          if ( oType( res ) == OINDIRECT ) {
            RBT_NODE *resnode = rbt_search( rr_state->restree ,
                                            ( uintptr_t )& res ) ;

            RR_DBG( HASH_INNARDS ,
                    ( rr_state ,
                      ( uint8 * )"%%## Resource lookup /%.*s /%.*s\n" ,
                      theINLen( & system_names[ type ]) ,
                      theICList( & system_names[ type ]) ,
                      oName( *theo )->len , theICList( oName( *theo )))) ;

            if ( resnode != NULL ) {
              RR_RESOURCE_NODE *rn = rbt_get_node_data( resnode ) ;
              uint8 *hash = rn->hash ;

              pdf_rr_hash( & params->hash_state , hash , RR_HASH_LENGTH ) ;
              params->is_form = rn->is_form ;
              params->has_matrix = rn->has_matrix ;
              params->has_ref = rn->has_ref ;
              params->is_image = rn->is_image ;
              params->stream_len = rn->stream_len ;

              RR_DBG( HASH_INNARDS ,
                      ( rr_state ,
                        ( uint8 * )"%%## %d %d R :: "
                        "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                        "%02x%02x%02x%02x%02x%02x%02x%02x%s%s%s%s" ,
                        oXRefID( res ) , theGen( res ) ,
                        hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
                        hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
                        hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
                        hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ] ,
                        rn->is_form ? " FORM" : "" ,
                        rn->is_image ? " IMAGE" : "" ,
                        rn->has_matrix ? " MATRIX" : "" ,
                        rn->has_ref ? " REF" : "" )) ;
              if ( rn->stream_len > 0 ) {
                RR_DBG( HASH_INNARDS , ( rr_state , ( uint8 * )" (%d)\n" ,
                                         rn->stream_len )) ;
              }
              else {
                RR_DBG( HASH_INNARDS , ( rr_state , ( uint8 * )"\n" )) ;
              }
            }
            else {
              HQFAIL( "Resource tree hash lookup failed." ) ;
              return FAILURE( FALSE ) ;
            }
          }
          else {
            /* It must have been a direct object in the owning
               resources dictionary. Illegal, but sometimes
               happens. */
            pdf_rr_hash_object( params , theo ) ;
          }
        }
        else {
          HQTRACE( ! hashed , ( "Unhashed: /%.*s" ,
                                theINLen( oName( *theo )) ,
                                theICList( oName( *theo )))) ;
        }
      }
      break ;
  }

#if defined( DEBUG_BUILD )
  if (( debug_retained_raster & DEBUG_RR_HASH_INNARDS ) != 0 ) {
    RR_DBG( HASH_INNARDS , ( rr_state , ( uint8 * )"%%## " )) ;
    if ( ! isPSCompObj( *theo )) {
      debug_print_object_indented( theo , NULL , " " ,
                                   pdf_rr_debug_file( rr_state )) ;
    }
    RR_DBG( HASH_INNARDS , ( rr_state , ( uint8 * )"\n" )) ;
  }
#endif

  return TRUE ;
}

/** Add contributions from all the operands on the given stack to the
    given hash state. */

static Bool pdf_rr_hash_stack( RR_HASH_PARAMS *params , STACK *stack )
{
  PDFCONTEXT *pdfc = params->pdfc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  int32 si ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL ) {
    HQFAIL( "Shouldn't be here without an RR state" ) ;
    return TRUE ;
  }

  for ( si = 0 ; si <= stack->size ; si++ ) {
    OBJECT *theo = & theIFrameOList( stack->fptr )[ si ] ;

    if ( ! pdf_rr_hash_operand( params , theo )) {
      return FALSE ;
    }
  }

  return TRUE ;
}

/** Allocate and insert a new node into the given tree. Also return
    it, or NULL if unsuccessful. */

static RBT_NODE *pdf_rr_hashtree_createnode( RBT_ROOT *root , uintptr_t key ,
                                             void *data )
{
  RBT_NODE *node ;

  node = rbt_allocate_node( root , key , data ) ;

  if ( node == NULL ) {
    ( void )error_handler( VMERROR ) ;
    return NULL ;
  }

  rbt_insert( root , node ) ;

  return node ;
}

/** Create a node in the resource hash tree. */

static RBT_NODE *pdf_rr_restree_createnode( PDFXCONTEXT *pdfxc , OBJECT *theo ,
                                            RR_RESOURCE_NODE *data )
{
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;

  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  return pdf_rr_hashtree_createnode( rr_state->restree , ( uintptr_t )theo ,
                                     data ) ;
}

/** Create a node in the scan tree, and for convenience update the
    node data's pointer to its key. */

static RBT_NODE *pdf_rr_scantree_createnode( PDFXCONTEXT *pdfxc ,
                                             RBT_ROOT *root , uint8 *hash ,
                                             RR_SCAN_NODE *data )
{
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RBT_NODE *node ;

  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  node = pdf_rr_hashtree_createnode( root , ( uintptr_t )hash , data ) ;

  if ( node != NULL ) {
    RR_SCAN_NODE *nsn = rbt_get_node_data( node ) ;
    nsn->hash = ( uint8 * )rbt_get_node_key( node ) ;
  }

  return node ;
}

static Bool pdf_rr_hash_resource( RR_HASH_PARAMS* params ,
                                  OBJECT *theo ) ;

#if defined( DEBUG_BUILD )
/** Calculate how much to indent resource hashing debug output. */

static inline int pdf_rr_indent_depth( RR_HASH_PARAMS *params )
{
  int depth = 0 ;

  while ( params->parent != NULL ) {
    depth++ ;
    params = params->parent ;
  }

  return depth ;
}
#endif

/** The dictionary walk callback for \c pdf_rr_hash_resource. */

static Bool pdf_rr_hash_res_dictwalkfn( OBJECT *thek , OBJECT *theo ,
                                        void *priv )
{
  RR_HASH_PARAMS* params = ( RR_HASH_PARAMS * )priv ;
  PDFCONTEXT *pdfc = params->pdfc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL ) {
    HQFAIL( "Shouldn't be here without an RR state" ) ;
    return TRUE ;
  }

  params->u.pkey = thek ;

  if ( oType( *thek ) == ONAME ) {
    if ( oName( *thek ) == system_names + NAME_HqnCacheSlot ||
         oName( *thek ) == system_names + NAME_HqEmbeddedStream ||
         oName( *thek ) == system_names + NAME_Position ||
         oName( *thek ) == system_names + NAME_Strict ||
         oName( *thek ) == system_names + NAME_StreamRefCount ||
         oName( *thek ) == system_names + NAME_XRef ) {
      /* We avoid hashing entries in the dicts that we put there
         ourselves (apart from possibly "Length", which we won't worry
         too much about). */
      return TRUE ;
    }

    if ( params->parent != NULL &&
         params->parent->parent != NULL &&
         params->parent->parent->u.pkey != NULL ) {
      if ( oType( *theo ) == ONAME &&
           oName( *thek ) == system_names + NAME_Subtype ) {
        if ( oName( *theo ) == system_names + NAME_Form ) {
          if ( oType( *params->parent->parent->u.pkey ) == ONAME &&
               ( oName( *params->parent->parent->u.pkey ) ==
                 system_names + NAME_XObject )) {
            params->parent->is_form = TRUE ;
          }
        }
        else if ( oName( *theo ) == system_names + NAME_Image ) {
          if ( oType( *params->parent->parent->u.pkey ) == ONAME &&
               ( oName( *params->parent->parent->u.pkey ) ==
                 system_names + NAME_XObject )) {
            params->parent->is_image = TRUE ;
          }
        }
      }
      else if ( oType( *theo ) == OINTEGER &&
                oName( *thek ) == system_names + NAME_Length ) {
        params->parent->stream_len = oInteger( *theo ) ;
      }
      else if ( oName( *thek ) == system_names + NAME_Matrix ) {
        params->parent->has_matrix = TRUE ;
      }
      else if ( oName( *thek ) == system_names + NAME_Ref ) {
        params->parent->has_ref = TRUE ;
      }
    }
  }

  RR_DBG_INDENT( RESOURCES , rr_state , pdf_rr_indent_depth( params )) ;

  if ( ! pdf_rr_hash_resource( params , thek ) ||
       ! pdf_rr_hash_resource( params , theo )) {
    return FALSE ;
  }

  RR_DBG( RESOURCES , ( rr_state , ( uint8 * )"\n" )) ;

  return TRUE ;
}

/** During pre-scanning, this routine is used to calculate the hash for
    the given resource. */

static Bool pdf_rr_hash_resource( RR_HASH_PARAMS* params ,
                                  OBJECT *theo )
{
  PDFCONTEXT *pdfc = params->pdfc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL ) {
    HQFAIL( "Shouldn't be here without an RR state" ) ;
    return TRUE ;
  }

  RR_DBG_INDENT( RESOURCES , rr_state , pdf_rr_indent_depth( params )) ;

  switch ( oType( *theo )) {
    case ONOTHING:
    case OINFINITY:
    case OOPERATOR:
    case OMARK:
    case ONULL:
    case OFONTID:
    case OSAVE:
    case OGSTATE:
    case OINTEGER:
    case OREAL:
    case OBOOLEAN:
    case OSTRING:
    case OLONGSTRING:
    case OFILEOFFSET:
    case ONAME:
      pdf_rr_hash_object( params , theo ) ;
      break ;

    case ODICTIONARY:
      {
        RR_HASH_PARAMS inner_params = *params ;

        inner_params.parent = params ;
        inner_params.is_form = FALSE ;
        inner_params.has_matrix = FALSE ;
        inner_params.has_ref = FALSE ;
        inner_params.is_image = FALSE ;
        inner_params.stream_len = 0 ;

        RR_DBG( RESOURCES , ( rr_state , ( uint8 * )"<<\n" )) ;

        if ( ! walk_dictionary_sorted( theo , pdf_rr_hash_res_dictwalkfn ,
                                       & inner_params )) {
          return FAILURE( FALSE ) ;
        }

        params->hash_state = inner_params.hash_state ;

        RR_DBG_INDENT( RESOURCES , rr_state , pdf_rr_indent_depth( params )) ;
        RR_DBG( RESOURCES , ( rr_state , ( uint8 * )">> " )) ;
      }
      break ;

    case OFILE:
      {
        FILELIST *flptr = oFile( *theo ) ;
        OBJECT *streamdict = streamLookupDict( theo ) ;
        Hq32x2 filepos ;
        uint8 *buf ;
        int32 bytes ;

        if ( ! pdf_rr_hash_resource( params , streamdict )) {
          return FALSE ;
        }

        Hq32x2FromInt32( & filepos , 0 ) ;
        if (( * theIMyResetFile( flptr ))( flptr ) == EOF ||
            ( * theIMySetFilePos( flptr ))( flptr , &filepos ) == EOF )
          return ( *theIFileLastError( flptr ))( flptr ) ;

        /* Find the underlying stream decode filter and calculate the hash on
           the compressed data.  This will avoid decompression time and should
           also mean hashing less data. */
        while ( isIFilter(flptr) &&
                HqMemCmp(theICList(flptr), theINLen(flptr),
                         NAME_AND_LENGTH("StreamDecode")) != 0 ) {
          flptr = theIUnderFile(flptr);
        }
        HQASSERT(isIFilter(flptr) &&
                 HqMemCmp(theICList(flptr), theINLen(flptr),
                          NAME_AND_LENGTH("StreamDecode")) == 0,
                 "Failed to find the stream decode filter");

        while ( GetFileBuff(flptr, MAXINT32, &buf, &bytes) ) {
          pdf_rr_hash( & params->hash_state , buf , bytes ) ;
        }

        if ( isIIOError(flptr) )
          return (*theIFileLastError(flptr))(flptr) ;

        /* The filters above stream decode were bypassed and are not
           at EOF.  Close these filters to reclaim their memory.  Do
           an implicit close because this is what happens when filters
           are normally read to EOF. */
        flptr = oFile( *theo ) ;
        (void)(*theIMyCloseFile(flptr))(flptr, CLOSE_IMPLICIT);
      }
      break ;

    case OARRAY:
    case OPACKEDARRAY:
      {
        int32 len = theLen(*theo) ;
        OBJECT *olist = oArray( *theo ) ;
        int32 i ;

        RR_DBG( RESOURCES , ( rr_state , ( uint8 * )"[\n" )) ;
        RR_DBG_INDENT( RESOURCES , rr_state ,
                       pdf_rr_indent_depth( params ) + 1 ) ;

        for ( i = 0 ; i < len ; i ++ ) {
          if ( ! pdf_rr_hash_resource( params , olist++ )) {
            return FALSE ;
          }
        }

        RR_DBG( RESOURCES , ( rr_state , ( uint8 * )"\n" )) ;
        RR_DBG_INDENT( RESOURCES , rr_state , pdf_rr_indent_depth( params )) ;
        RR_DBG( RESOURCES , ( rr_state , ( uint8 * )"] " )) ;
        RR_DBG_INDENT( RESOURCES , rr_state , 0 ) ;

        break ;
      }

    case OINDIRECT:
      {
        RBT_NODE *node ;

        node = rbt_search( rr_state->restree , ( uintptr_t )theo ) ;

        if ( node == NULL ) {
          RR_HASH_PARAMS inner_params = *params ;
          RR_RESOURCE_NODE rn = { 0 } ;
          uint8 *hash = rn.hash ;
          OBJECT *obj ;

          if ( ! pdf_lookupxref( pdfc , & obj , oXRefID( *theo ) ,
                                 theGen( *theo ) , FALSE )) {
            return FALSE ;
          }

          /* Explicitly don't modify the 'parent' pointer: the
             distinct params here aren't for recursion, they're for
             keeping the hashes separate. */
          pdf_rr_hash_reset( ixc , & inner_params.hash_state ) ;
          inner_params.is_form = FALSE ;
          inner_params.has_matrix = FALSE ;
          inner_params.has_ref = FALSE ;
          inner_params.is_image = FALSE ;
          inner_params.stream_len = 0 ;

          /* Insert the node into the tree before the hash is
             calculated. Seems peculiar, but this allows us to proceed
             normally when there are circular references in the
             resource hierarchy. */

          node = pdf_rr_restree_createnode( pdfxc , theo , & rn ) ;

          if ( node == NULL ) {
            return FALSE ;
          }

          if ( ! pdf_rr_hash_resource( & inner_params , obj )) {
            return FALSE ;
          }
          pdf_rr_hash_finalise( & inner_params.hash_state , hash ) ;
          rn.is_form = inner_params.is_form ;
          rn.has_matrix = inner_params.has_matrix ;
          rn.has_ref = inner_params.has_ref ;
          rn.is_image = inner_params.is_image ;
          rn.stream_len = inner_params.stream_len ;

          /* Defensive programming here: we need to look it up in the
             tree again. Seems odd until you realise that the
             recursion above might have modified the tree, and in
             rebalancing afterwards the rbt_ code may have copied
             entries around. */
          node = rbt_search( rr_state->restree , ( uintptr_t )theo ) ;
          rbt_set_node_data( rr_state->restree , node , & rn ) ;

          RR_DBG( RESOURCES ,
                  ( rr_state , ( uint8 * )"%% %d %d R :: "
                    "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                    "%02x%02x%02x%02x%02x%02x%02x%02x (new node)%s%s%s%s" ,
                    oXRefID( *theo ) , theGen( *theo ) ,
                    hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
                    hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
                    hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
                    hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ] ,
                    rn.is_form ? " FORM" : "" ,
                    rn.is_image ? " IMAGE" : "" ,
                    rn.has_matrix ? " MATRIX" : "" ,
                    rn.has_ref ? " REF" : "" )) ;
          if ( rn.stream_len > 0 ) {
            RR_DBG( RESOURCES , ( rr_state , ( uint8 * )" (%d)" ,
                                  rn.stream_len )) ;
          }

          pdf_rr_hash( & params->hash_state , hash , RR_HASH_LENGTH ) ;
        }
        else {
          RR_RESOURCE_NODE *rn = rbt_get_node_data( node ) ;
          uint8 *hash = rn->hash ;

          RR_DBG( RESOURCES ,
                  ( rr_state , ( uint8 * )"%% %d %d R :: "
                    "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                    "%02x%02x%02x%02x%02x%02x%02x%02x "
                    "(existing node)%s%s%s%s " ,
                    oXRefID( *theo ) , theGen( *theo ) ,
                    hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
                    hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
                    hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
                    hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ] ,
                    rn->is_form ? " FORM" : "" ,
                    rn->is_image ? " IMAGE" : "" ,
                    rn->has_matrix ? " MATRIX" : "" ,
                    rn->has_ref ? " REF" : "" )) ;
          if ( rn->stream_len > 0 ) {
            RR_DBG( RESOURCES , ( rr_state , ( uint8 * )"(%d) " ,
                                  rn->stream_len )) ;
          }

          pdf_rr_hash( & params->hash_state , hash , RR_HASH_LENGTH ) ;
        }
      }
      break ;
  }

#if defined( DEBUG_BUILD )
  if (( debug_retained_raster & DEBUG_RR_RESOURCES ) != 0 ) {
    if (( ! isPSCompObj( *theo ) && oType( *theo ) != OINDIRECT ) ||
        oType( *theo ) == OSTRING ) {
      debug_print_object_indented( theo , NULL , " " ,
                                   pdf_rr_debug_file( rr_state )) ;
    }
  }
#endif

  return TRUE ;
}

/** Compare two PDF resource indirect references (used as a callback
    in building red-black trees keyed on them). Same as strcmp and
    qsort callbacks, returns < 0 if key1 < key2 etc. */

static int32 pdf_rr_res_compare( RBT_ROOT *root , uintptr_t key1 ,
                                 uintptr_t key2 )
{
  const OBJECT *o1 = ( const OBJECT * )key1 ;
  const OBJECT *o2 = ( const OBJECT * )key2 ;

  UNUSED_PARAM( struct rbt_root * , root ) ;

  if ( oXRefID( *o1 ) < oXRefID( *o2 )) {
    return -1 ;
  }
  else if ( oXRefID( *o1 ) > oXRefID( *o2 )) {
    return 1 ;
  }

  if ( theGen( *o1 ) < theGen( *o2 )) {
    return -1 ;
  }
  else if ( theGen( *o1 ) > theGen( *o2 )) {
    return 1 ;
  }

  return 0 ;
}

/** Recursively walk the current resource dictionaries, precalculating
    hashes for use later when we look up named resources. */

static Bool pdf_rr_hash_resources( PDFCONTEXT *pdfc, OBJECT *page_dict )
{
  Bool result = FALSE ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_HASH_PARAMS params ;
  RR_STATE *rr_state ;
  PDF_DICTLIST *ptr ;
  int32 saved_conformance_pdf_version ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL ) {
    HQFAIL( "Shouldn't be here without an RR state" ) ;
    return TRUE ;
  }

  /* Suspend PDF/X conformance checking during the resource scan, because
     calling context flags like ixc->handlingImage are not set.  The resources
     will still be checked for conformance during normal interpretation. */
  saved_conformance_pdf_version = ixc->conformance_pdf_version ;
  ixc->conformance_pdf_version = 0 ;

  params.pdfc = pdfc ;
  pdf_rr_hash_reset( ixc , & params.hash_state ) ;
  params.parent = NULL ;
  params.u.pkey = NULL ;
  params.is_form = FALSE ;
  params.has_matrix = FALSE ;
  params.has_ref = FALSE ;
  params.is_image = FALSE ;
  params.stream_len = 0 ;

  RR_DBG( RESOURCES , ( rr_state , ( uint8 * )"%% -*-ps-*-\n\n" )) ;

  for ( ptr = pdfc->pdfenv ; ptr != NULL ; ptr = ptr->next ) {
    if ( ! walk_dictionary_sorted( ptr->dict , pdf_rr_hash_res_dictwalkfn ,
                                   & params )) {
      goto CLEANUP ;
    }
  }

  /* Annotations are scanned if they are to be drawn.  They aren't strictly
     resources, but can be handled similarly. */
  if ( ixc->PrintAnnotations ) {
    static NAMETYPEMATCH page_dict_match[] = {
      { NAME_Annots | OOPTIONAL , 4 ,
        { OARRAY , OPACKEDARRAY , ONULL , OINDIRECT }} ,
      DUMMY_END_MATCH
    } ;
    OBJECT *annots ;

    if ( ! pdf_dictmatch( pdfc , page_dict , page_dict_match )) {
      return FALSE ;
    }

    annots = page_dict_match[ 0 ].result ;

    if ( annots != NULL && oType( *annots ) != ONULL ) {
      if ( ! pdf_rr_hash_resource( & params , page_dict )) {
        goto CLEANUP ;
      }
    }
  }

  result = TRUE ;

 CLEANUP:

  ixc->conformance_pdf_version = saved_conformance_pdf_version ;

  return result ;
}

#if defined( DEBUG_BUILD )
/** Dump a sequence node (marks or pages tree). */

static void pdf_rr_dump_ptn( RR_STATE *rr_state , RR_PAGES_NODE *ptn ,
                             uint32 offset )
{
  RR_DBG( ANY , ( rr_state , ( uint8 * )"(" )) ;

  if ( ptn != NULL ) {
    uint32 i ;

    if ( ptn->count < 7 ) {
      for ( i = 0 ; i < ptn->count ; i++ ) {
        RR_DBG( ANY , ( rr_state , ( uint8 * )"%u" ,
                        ptn->index[ i ] + offset )) ;
        if ( i != ptn->count - 1 ) {
          RR_DBG( ANY , ( rr_state , ( uint8 * )"," )) ;
        }
      }
    }
    else {
      for ( i = 0 ; i < 3 ; i++ ) {
        RR_DBG( ANY , ( rr_state , ( uint8 * )"%u" ,
                        ptn->index[ i ] + offset )) ;
        if ( i != 2 ) {
          RR_DBG( ANY , ( rr_state , ( uint8 * )"," )) ;
        }
      }

      RR_DBG( ANY , ( rr_state , ( uint8 * )" ... " )) ;

      for ( i = ptn->count - 3 ; i < ptn->count ; i++ ) {
        RR_DBG( ANY , ( rr_state , ( uint8 * )"%u" ,
                        ptn->index[ i ] + offset )) ;
        if ( i != ptn->count - 1 ) {
          RR_DBG( ANY , ( rr_state , ( uint8 * )"," )) ;
        }
      }
    }
  }
  else {
    RR_DBG( ANY , ( rr_state , ( uint8 * )"??" )) ;
  }

  RR_DBG( ANY , ( rr_state , ( uint8 * )")" )) ;
}

/** Debug log dump of a pages tree node. */

static void pdf_rr_dump_pages_node( RR_STATE *rr_state , RR_SCAN_NODE *sn )
{
  RBT_NODE *pagesnode = rbt_search( rr_state->pagestree ,
                                    ( uintptr_t )sn->pages_hash ) ;

  RR_DBG( ANY , ( rr_state , ( uint8 * )"0x%08x " , sn->pages_hash )) ;

  if ( pagesnode != NULL ) {
    pdf_rr_dump_ptn( rr_state , rbt_get_node_data( pagesnode ) ,
                     rr_state->page_offset + 1 ) ;
  }
  else {
    RR_DBG( ANY , ( rr_state , ( uint8 * )"(->%u)" , sn->last_page + 1 )) ;
  }
}

/** A simple function for dumping the quadruply-linked list that is
    \c page->links. */

static void pdf_rr_dump_page_link( RR_STATE *rr_state , RR_PAGE_LINK *link ,
                                   uint32 mark_digits )
{
  if ( link != NULL ) {
    if ( link->mark_num == MAXUINT32 ) {
      RR_DBG( ANY , ( rr_state , ( uint8 * )" <%.*s>" , mark_digits ,
                      "------------" )) ;
    }
    else {
      RR_DBG( ANY , ( rr_state , ( uint8 * )" <%.*u>" , mark_digits ,
                      link->mark_num )) ;
    }
  }
  else {
    RR_DBG( ANY , ( rr_state , ( uint8 * )" <%.*s>" , mark_digits ,
                    "            " )) ;
  }
}

/** Dump the page's node links. Format is:
    mark_num @nesting [hits] is_form atom_start atom_end
      id !sig_mask <sub_first> <prev> <next> <last> */

static void pdf_rr_dump_page_links( RR_STATE *rr_state , uint32 page_num ,
                                    char *title , Bool hit_counts )
{
  RR_PAGE *page = & rr_state->pages[ page_num ] ;
  RR_PAGE_LINK *link ;
  int32 mark_digits ;
  int32 hits_digits ;
  int32 nesting_digits ;
  uint32 max_hits = 0 ;
  uint32 max_nesting = 0 ;
  uint32 i ;

  for ( link = page->head.next ; link != & page->tail ;
        link = link->next ) {
    if ( hit_counts && link->sn->hits > max_hits ) {
      max_hits = link->sn->hits ;
    }
    if ( page->marks[ link->mark_num ].nesting > max_nesting ) {
      max_nesting = page->marks[ link->mark_num ].nesting ;
    }
  }

  mark_digits = UNSIGNED_DIGITS( page->mark_count ) ;
  hits_digits = UNSIGNED_DIGITS( max_hits ) ;
  nesting_digits = UNSIGNED_DIGITS( max_nesting ) ;

  pdf_rr_open_debug_log( rr_state , title , page_num + 1 , MAXUINT32 ) ;

  for ( link = page->head.next ; link != & page->tail ;
        link = link->next ) {
    RR_SCAN_NODE *sn = link->sn ;
    uint8 *hash ;

    if ( link == NULL || link->sn == NULL ) {
      HQFAIL( "Page link pointers don't make sense" ) ;
      goto CLEANUP ;
    }

    hash = sn->hash ;
    RR_DBG( ANY ,
            ( rr_state , ( uint8 * )"%.*u @%.*u " ,
              mark_digits , link->mark_num , nesting_digits ,
              page->marks[ link->mark_num ].nesting )) ;
    if ( hit_counts ) {
      RR_DBG( ANY ,
              ( rr_state , ( uint8 * )"[%.*u] " , hits_digits , sn->hits )) ;
    }
    RR_DBG( ANY ,
            ( rr_state , ( uint8 * )"%c%c%c%c%c%c %.*s "
              "0x%02x%02x%02x%02x%02x%02x%02x%02x"
              "%02x%02x%02x%02x%02x%02x%02x%02x   " ,
              sn->composited ? 'C' : ' ' , sn->pattern ? 'P' : ' ' ,
              sn->significant ? '!' : ' ' ,
              page->marks[ link->mark_num ].is_form ? '*' : ' ' ,
              page->marks[ link->mark_num ].atom_start ? '{' : ' ' ,
              page->marks[ link->mark_num ].atom_end ?   '}' : ' ' ,
              3 , tokens + 3 * page->marks[ link->mark_num ].pdfop ,
              hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
              hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
              hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
              hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ] )) ;

    pdf_rr_dump_page_link( rr_state , link->sub_first , mark_digits ) ;
    pdf_rr_dump_page_link( rr_state , link->prev , mark_digits ) ;
    pdf_rr_dump_page_link( rr_state , link->next , mark_digits ) ;
    pdf_rr_dump_page_link( rr_state , link->sub_last , mark_digits ) ;
    if ( hit_counts ) {
      RR_DBG( ANY , ( rr_state , ( uint8 * )" " )) ;
      pdf_rr_dump_pages_node( rr_state , sn ) ;
    }
    if ( link->sub_last != NULL ) {
      RR_DBG( ANY , ( rr_state , ( uint8 * )" {" )) ;
    }
    RR_DBG( ANY , ( rr_state , ( uint8 * )"\n" )) ;

    if ( link->sub_last != NULL ) {
      RR_PAGE_LINK *limit = link->sub_last->next ;

      for ( link = link->next ; link != limit ; link = link->next ) {
        if ( link == NULL || link->sn == NULL ) {
          HQFAIL( "Page link pointers don't make sense" ) ;
          goto CLEANUP ;
        }

        sn = link->sn ;

        RR_DBG( ANY ,
                ( rr_state , ( uint8 * )"%.*u @%.*u " ,
                  mark_digits , link->mark_num , nesting_digits ,
                  page->marks[ link->mark_num ].nesting )) ;
        if ( hit_counts ) {
          RR_DBG( ANY ,
                  ( rr_state , ( uint8 * )"[%.*s] " , hits_digits ,
                    "                                " )) ;
        }
        RR_DBG( ANY ,
                ( rr_state , ( uint8 * )"%c%c%c%c%c%c %.*s "
                  "                                     " , ' ' , ' ' , ' ' ,
                  page->marks[ link->mark_num ].is_form ? '*' : ' ' ,
                  page->marks[ link->mark_num ].atom_start ? '{' : ' ' ,
                  page->marks[ link->mark_num ].atom_end ?   '}' : ' ' ,
                  3 , tokens + 3 * page->marks[ link->mark_num ].pdfop )) ;

        pdf_rr_dump_page_link( rr_state , link->sub_first , mark_digits ) ;
        pdf_rr_dump_page_link( rr_state , link->prev , mark_digits ) ;
        pdf_rr_dump_page_link( rr_state , link->next , mark_digits ) ;
        pdf_rr_dump_page_link( rr_state , link->sub_last , mark_digits ) ;
        if ( link->sub_first != NULL ) {
          RR_DBG( ANY , ( rr_state , ( uint8 * )" }" )) ;
        }
        RR_DBG( ANY , ( rr_state , ( uint8 * )"\n" )) ;
      }

      link = link->prev ;
    }
  }

  RR_DBG( ANY , ( rr_state , ( uint8 * )"\n" )) ;

  for ( i = 0 ; i < page->mark_count ; i ++ ) {
    if ( page->marks[ i ].is_form &&
         ! page->marks[ i ].has_ref ) {
      RR_PAGE_LINK *link = & page->links[ i ] ;
      RR_SCAN_NODE *sn = link->sn ;
      uint8 *hash = sn->hash ;

      RR_DBG( ANY ,
              ( rr_state , ( uint8 * )"%.*u @%.*u " ,
                mark_digits , link->mark_num , nesting_digits ,
                page->marks[ link->mark_num ].nesting )) ;
      if ( hit_counts ) {
        RR_DBG( ANY ,
                ( rr_state , ( uint8 * )"[%.*u] " , hits_digits , sn->hits )) ;
      }
      RR_DBG( ANY ,
              ( rr_state , ( uint8 * )"%c%c%c%c%c%c "
                "%.*s 0x%02x%02x%02x%02x%02x%02x%02x%02x"
                "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
                sn->composited ? 'C' : ' ' , sn->pattern ? 'P' : ' ' ,
                sn->significant ? '!' : ' ' ,
                page->marks[ link->mark_num ].is_form ? '*' : ' ' ,
                page->marks[ link->mark_num ].atom_start ? '{' : ' ' ,
                page->marks[ link->mark_num ].atom_end ?   '}' : ' ' ,
                3 , tokens + 3*page->marks[ link->mark_num ].pdfop ,
                hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
                hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
                hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
                hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ] )) ;
    }
  }

 CLEANUP:

  RR_DBG_CLOSE( ANY , rr_state ) ;
}

/** Used when dumping the whole pages tree. */

static Bool pdf_rr_pages_tree_dump( RBT_ROOT *root , RBT_NODE *node ,
                                    void *walk_data )
{
  RR_PAGES_NODE *ptn = rbt_get_node_data( node ) ;
  RR_STATE *rr_state = ( RR_STATE * )walk_data ;
  uint32 i ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;

  if ( ptn == NULL ) {
    HQFAIL( "Null ptn" ) ;
    return TRUE ;
  }

  RR_DBG( PAGES_TREE ,
          ( rr_state , ( uint8 * )"%p: [" , rbt_get_node_key( node ))) ;

  for ( i = 0 ; i < ptn->count ; i++ ) {
    RR_DBG( PAGES_TREE , ( rr_state , ( uint8 * )" %u" , ptn->index[ i ] + 1 )) ;
  }

  RR_DBG( PAGES_TREE , ( rr_state , ( uint8 * )" ]\n" )) ;

  return TRUE ;
}

/** Used when dumping the whole marks tree. */

static Bool pdf_rr_marks_tree_dump( RBT_ROOT *root , RBT_NODE *node ,
                                    void *walk_data )
{
  RR_MARKS_NODE *mn = rbt_get_node_data( node ) ;
  RR_STATE *rr_state = ( RR_STATE * )walk_data ;
  uint32 i ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;

  if ( mn == NULL ) {
    HQFAIL( "Null mn" ) ;
    return TRUE ;
  }

  RR_DBG( MARKS_TREE , ( rr_state , ( uint8 * )"%p: %u marks [" ,
                         rbt_get_node_key( node ) , mn->count )) ;

  for ( i = 0 ; i < mn->count ; i++ ) {
    RR_DBG( MARKS_TREE , ( rr_state , ( uint8 * )"%c" ,
                           mn->marks[ i ].omit ? ' ' : 'x' )) ;
  }

  RR_DBG( MARKS_TREE , ( rr_state , ( uint8 * )"]\n" )) ;

  return TRUE ;
}

/** Dump an active element node: can be called after the scan so must
    be robust against absent scan nodes. */

static void pdf_rr_dump_active_node( RR_STATE *rr_state , RR_ELEM_NODE *en ,
                                     uint32 page_num , uint32 elem_num ,
                                     uint8 *hash , char *prefix )
{
  RR_PAGE *page = & rr_state->pages[ page_num ] ;
  RBT_NODE *node ;

  RR_DBG( ACTIVE_NODES ,
          ( rr_state , ( uint8 * )"%sp%.*u_e%.*u <%.*u>: "
            "0x%02x%02x%02x%02x%02x%02x%02x%02x"
            "%02x%02x%02x%02x%02x%02x%02x%02x [%.*u] " , prefix ,
            UNSIGNED_DIGITS( rr_state->page_offset + rr_state->page_count ) ,
            rr_state->page_offset + page_num + 1 ,
            UNSIGNED_DIGITS( page->element_count ) , elem_num ,
            UNSIGNED_DIGITS( max( rr_state->unique_element_count ,
                                  rr_state->page_count )) , en->index + 1 ,
            hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
            hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
            hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
            hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ] ,
            UNSIGNED_DIGITS( rr_state->page_count ) , en->hits )) ;

  node = rbt_search( rr_state->scantree , ( uintptr_t )hash ) ;
  if ( node != NULL ) {
    RR_SCAN_NODE *sn = rbt_get_node_data( node ) ;
    HQASSERT( sn != NULL , "Should be a valid scan node here" ) ;

    pdf_rr_dump_pages_node( rr_state , sn ) ;
  }

  RR_DBG( ACTIVE_NODES , ( rr_state , ( uint8 * )"\n" )) ;
}
#endif /* defined( DEBUG_BUILD ) */

#if defined( ASSERT_BUILD )
/** Assertions that the page links and pages tree nodes are internally
    consistent. Does nothing if the scan window has already closed on
    the page in question. */

static void pdf_rr_page_links_assertions( RR_STATE *rr_state , RR_PAGE *page )
{
  uint32 count = 0 ;
  RR_PAGE_LINK *link ;

  UNUSED_PARAM( RR_STATE * , rr_state ) ;

  for ( link = page->head.next ; link != & page->tail ; link = link->next ) {
    HQASSERT( link != NULL , "Null page link" ) ;
    HQASSERT( link->mark_num != MAXUINT32 , "Reached sentinel in error" ) ;

    if ( link->sub_last != NULL ) {
      RR_PAGE_LINK *last = link->sub_last ;

      HQASSERT( last->sub_first == link ,
                "sub_first doesn't point back where it should" ) ;
      HQASSERT( link->sub_first == NULL ,
                "First subnodes shouldn't have sub_first pointers" ) ;

      for ( link = link->next ; link != last ; link = link->next ) {
        if ( link == NULL ) {
          HQFAIL( "Null page link" ) ;
          return ;
        }
        HQASSERT( link != & page->tail , "Got to tail sentinel too soon" ) ;
        HQASSERT( link->sub_last == NULL ,
                  "Internal subnodes shouldn't have sub_last pointers" ) ;
        HQASSERT( link->sub_first == NULL ,
                  "Internal subnodes shouldn't have sub_first pointers" ) ;
      }

      if ( link == NULL ) {
        HQFAIL( "Null page link" ) ;
        return ;
      }
      HQASSERT( link->sub_last == NULL ,
                "Last subnodes shouldn't have sub_last pointers" ) ;
      HQASSERT( link->mark_num != MAXUINT32 , "Reached sentinel in error" ) ;
    }

    if ( ++count > page->mark_count ) {
      HQFAIL( "Circular reference in page links" ) ;
    }
  }
}

/** Run assertions for all pages. */

static void pdf_rr_all_pages_assertions( RR_STATE *rr_state )
{
  uint32 i ;

  for ( i = 0 ; i < rr_state->page_count ; i++ ) {
    pdf_rr_page_links_assertions( rr_state , & rr_state->pages[ i ]) ;
  }
}

/** Assert that every node in every page is a member of the permanent
    node tree and not the temporary supernodes tree. Used to avoid
    dangling pointers before going ahead and freeing the supernodes
    tree. */

static void pdf_rr_live_nodes_check( RR_STATE *rr_state )
{
  uint32 i ;

  for ( i = 0 ; i < rr_state->page_count ; i++ ) {
    RR_PAGE *page = & rr_state->pages[ i ] ;
    RR_PAGE_LINK *link ;

    for ( link = page->head.next ; link != & page->tail ; link = link->next ) {
      RR_SCAN_NODE *sn = link->sn ;

      HQASSERT( rbt_search( rr_state->scantree ,
                            ( uintptr_t )sn->hash ) != NULL ,
                "Node is not present in the scan tree" ) ;
      HQASSERT( ! sn->is_form , "All nodes reachable from page links "
                "should be regular ones by now" ) ;
    }
  }
}

#define RR_ASRT_PAGE( _state , _page )                          \
  MACRO_START                                                   \
  pdf_rr_page_links_assertions( _state , _page ) ;              \
  MACRO_END
#define RR_ASRT_ALL_PAGES( _state )                             \
  MACRO_START                                                   \
  pdf_rr_all_pages_assertions( _state ) ;                       \
  MACRO_END
#define RR_ASRT_LIVE_NODES( _state )                            \
  MACRO_START                                                   \
  pdf_rr_live_nodes_check( _state ) ;                           \
  MACRO_END
#else
#define RR_ASRT_PAGE( _state , _page ) EMPTY_STATEMENT()
#define RR_ASRT_ALL_PAGES( _state ) EMPTY_STATEMENT()
#define RR_ASRT_LIVE_NODES( _state ) EMPTY_STATEMENT()
#endif

/** Add a reference to the given node to the current page. Note that
    marks may not arrive in strict \c mark_num order because of
    recursion in the scanner but they are always stored in a
    contigious, ascending sorted range starting at zero. */

static Bool pdf_rr_add_node_to_page( PDFCONTEXT *pdfc , RR_SCAN_NODE *sn ,
                                     uint32 mark_num , Bool is_form ,
                                     Bool has_ref , uint32 bytes , uint8 pdfop ,
                                     uint8 nesting , uint32 page_index )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL ) {
    HQFAIL( "Shouldn't be here without an RR state" ) ;
    return TRUE ;
  }

  if ( mark_num >= rr_state->bucket_size ) {
    uint32 new_size = rr_state->bucket_size * 2 ;
    RR_PAGE_MARK *new_marks ;
    RR_PAGE_LINK *new_links ;

    while ( mark_num >= new_size ) {
      new_size *= 2 ;
    }

    new_links = mm_alloc( pdfxc->mm_structure_pool ,
                          ( sizeof( *rr_state->links ) * new_size ) ,
                          MM_ALLOC_CLASS_PDF_RR_LINKS ) ;

    if ( new_links == NULL ) {
      return FAILURE( error_handler( VMERROR )) ;
    }

    new_marks = mm_alloc( pdfxc->mm_structure_pool ,
                          ( sizeof( *rr_state->marks ) * new_size ) ,
                          MM_ALLOC_CLASS_PDF_RR_MARKS ) ;

    if ( new_marks == NULL ) {
      mm_free( pdfxc->mm_structure_pool , new_links ,
                ( sizeof( *rr_state->links ) * new_size )) ;
      return FAILURE( error_handler( VMERROR )) ;
    }

    HqMemCpy( new_links , rr_state->links ,
              sizeof( *rr_state->links ) * rr_state->bucket_size ) ;
    mm_free( pdfxc->mm_structure_pool , rr_state->links ,
             ( sizeof( *rr_state->links ) * rr_state->bucket_size )) ;

    HqMemCpy( new_marks , rr_state->marks ,
              sizeof( *rr_state->marks ) * rr_state->bucket_size ) ;
    mm_free( pdfxc->mm_structure_pool , rr_state->marks ,
             ( sizeof( *rr_state->marks ) * rr_state->bucket_size )) ;

    rr_state->links = new_links ;
    rr_state->marks = new_marks ;
    rr_state->bucket_size = new_size ;
  }

  /* We don't set up the doubly-linked list until the links in the
     collection bucket are copied to the page's array, which is when
     the scan for the page is complete. */

  rr_state->links[ mark_num ].next = NULL ;
  rr_state->links[ mark_num ].prev = NULL ;
  rr_state->links[ mark_num ].sub_first = NULL ;
  rr_state->links[ mark_num ].sub_last = NULL ;
  rr_state->links[ mark_num ].sn = sn ;
  rr_state->links[ mark_num ].mark_num = mark_num ;

  rr_state->marks[ mark_num ].omit = FALSE ;
  rr_state->marks[ mark_num ].is_form = is_form ;
  rr_state->marks[ mark_num ].has_ref = has_ref ;
  rr_state->marks[ mark_num ].atom_start = FALSE ;
  rr_state->marks[ mark_num ].atom_end = FALSE ;
  rr_state->marks[ mark_num ].pdfop = pdfop ;
  rr_state->marks[ mark_num ].inline_data_bytes = bytes ;
  rr_state->marks[ mark_num ].nesting = nesting ;

  if ( rr_state->nodes_count < ( mark_num + 1 )) {
    rr_state->nodes_count = mark_num + 1 ;
  }

  /* Update the last_page and pages_hash values as appropriate. We'll
     make a pages tree node later. */

  if ( sn->last_page != page_index ) {
    MurmurHash3_32( & page_index , sizeof( page_index ) ,
                    sn->pages_hash , & sn->pages_hash ) ;
    sn->last_page = page_index ;
  }

  return TRUE ;
}

/** Increment the atomic number in the state. Isolated in its own
    function so we're ready to use atomic supernodes in more than one
    set of circumstances. */

static void pdf_rr_increment_atomic_number( RR_STATE *rr_state ,
                                            uint32 start_mark )
{
  if ( rr_state->current_atomic_number == 0 ) {
    rr_state->current_atom_start = start_mark ;
    rr_state->current_atom_end = start_mark ;
  }
  HQASSERT( rr_state->current_atomic_number < MAXUINT32 ,
            "current_atomic_number overflowing" ) ;
  rr_state->current_atomic_number++ ;
  RR_DBG( SEQUENCE_POINTS ,
          ( rr_state ,
            ( uint8 * )"%% Increment atomic number to %u\n" ,
            rr_state->current_atomic_number )) ;
}

/** Decrement the atomic number in the state and finish off the
    current atomic supernode range if it becomes zero. */

static void pdf_rr_decrement_atomic_number( RR_STATE *rr_state )
{
  if ( rr_state->current_atomic_number == 1 ) {
    if ( rr_state->current_atom_end > rr_state->current_atom_start ) {
      rr_state->marks[ rr_state->current_atom_start ].atom_start = TRUE ;
      rr_state->marks[ rr_state->current_atom_end ].atom_end = TRUE ;
    }
    rr_state->current_atom_start = MAXUINT32 ;
    rr_state->current_atom_end = 0 ;
  }
  if ( rr_state->current_atomic_number > 0 ) {
    rr_state->current_atomic_number-- ;
  }
  else {
    HQFAIL( "current_atomic_number underflowing" ) ;
  }
  RR_DBG( SEQUENCE_POINTS ,
          ( rr_state ,
            ( uint8 * )"%% Decrement atomic number to %u\n" ,
            rr_state->current_atomic_number )) ;
}

/** Callback for deciding whether to let a mark through to the display
    list, and in some circumstances to siphon off information about
    what's being added for later on. */

static Bool pdf_rr_dl_object_callback( LISTOBJECT *lobj , void *priv_data ,
                                       Bool *suppress )
{
  PDFXCONTEXT *pdfxc = ( PDFXCONTEXT * )priv_data ;
  PDFCONTEXT *pdfc ;
  RR_STATE *rr_state ;

  HQASSERT_LPTR( pdfxc ) ;
  pdfc = pdfxc->pdfc ;
  /* Be careful not to assume there's an execution context there - we
     could be cleaning up after an error. */
  if ( pdfxc->u.i != NULL ) {
    PDF_IXC_PARAMS *ixc ;

    PDF_GET_IXC( ixc ) ;

    *suppress = FALSE ;
    rr_state = ixc->rr_state ;
    if ( rr_state != NULL && lobj->opcode != RENDER_erase &&
         rr_state->op_context != NULL && rr_state->mode == RR_MODE_SCANNING ) {
      DL_STATE *dl_page = pdfxc->corecontext->page ;
      Bool is_composited = lobj_maybecompositing( lobj ,
                                                  dl_page->currentGroup ,

!ixc->OptimizedPDFExternal ) ;

      *suppress = TRUE ;

      rr_state->op_context->opcode = lobj->opcode ;
      bbox_union( & rr_state->op_context->bbox , & lobj->bbox ,
                  & rr_state->op_context->bbox ) ;

      if ( rr_state->current_compositing != is_composited ) {
        RR_DBG( SEQUENCE_POINTS ,
                ( rr_state ,
                  ( uint8 * )"%% Transparency marker changed to %s\n" ,
                  is_composited ? "ON" : "OFF" )) ;

        rr_state->current_compositing = is_composited ;
      }

      if ( lobj->objectstate != NULL &&
           ( rr_state->current_patternstate !=
             lobj->objectstate->patternstate )) {
        rr_state->current_patternstate = lobj->objectstate->patternstate ;
      }
    }
  }

  return TRUE ;
}

/** Find the next set of marks which are tagged with flags indicating
    that they must be treated as a single raster element. Begins at
    index \c *super_start and returns the supernode (if any) in \c
    *super_start and \c *super_end. We deal in indices rather than \c
    RR_PAGE_LINKs because the links array is always in z-order when
    this routine is called. */

static void pdf_rr_next_atomic_supernode( RR_PAGE *page , uint32 *start ,
                                          uint32 *end , dbbox_t *bbox ,
                                          Bool *significant )
{
  uint32 mark ;

  bbox_clear( bbox ) ;
  *significant = FALSE ;
  *end = *start ;

  /* Find the first mark at or after *super_start which is flagged as
     the start of an atom. */

  for ( mark = *start ; mark < page->mark_count ; mark++ ) {
    if ( page->marks[ *start ].atom_start ) {
      if ( ! page->marks[ *start ].atom_end ) {
        /* It's valid for a mark to be flagged as both atom_start and
           atom_end, but we don't need to make a supernode if that's
           the case. */
        break ;
      }
    }
    else {
      *start = *end = mark ;
    }
  }

  if ( mark == page->mark_count ) {
    return ;
  }

  /* This function is called before any z-order adjustment so it's
     safe to index directly into the links array rather than following
     the linked list. */

  for ( mark = *start ; mark < page->mark_count ; mark++ ) {
    RR_SCAN_NODE *sn = page->links[ mark ].sn ;

    if ( page->marks[ mark ].is_form &&
         ! page->marks[ mark ].has_ref ) {
      continue ;
    }

    *end = mark ;
    bbox_union( bbox , & sn->bbox , bbox ) ;
    *significant |= sn->significant ;

    if ( page->marks[ mark ].atom_end ) {
      break ;
    }
  }
}

/** Move \c link from its current location in the adjusted z-order to
    immediately after \c before. */

void pdf_rr_move_node( RR_STATE *rr_state , RR_PAGE *page ,
                       RR_PAGE_LINK *link , RR_PAGE_LINK *before )
{
  RR_PAGE_LINK **onward ;
  RR_PAGE_LINK *after ;

  UNUSED_PARAM( RR_STATE * , rr_state ) ;
  UNUSED_PARAM( RR_PAGE * , page ) ;

  /* When we're figuring out where to move the link we need "before"
     to be pointing at the last subnode, not the supernode. */

  if ( before->sub_last != NULL ) {
    HQASSERT( before != & page->head ,
              "Page head shouldn't be the start of a supernode" ) ;
    if ( before->sub_last == link ) {
      RR_DBG( Z_ORDER ,
              ( rr_state , ( uint8 * )"Refusing to move %d to before %d\n" ,
                link->mark_num , before->mark_num )) ;
      return ;
    }
    before = before->sub_last ;
  }

  after = before->next ;

  if ( after == link ) {
    return ;
  }

  RR_DBG( Z_ORDER ,
          ( rr_state , ( uint8 * )"Moving %d to between %d and %d\n" ,
            link->mark_num , before->mark_num , after->mark_num )) ;

  if ( link->sub_last != NULL ) {
    onward = & link->sub_last->next ;
  }
  else {
    onward = & link->next ;
  }

  /* Remove link from the old location. */
  ( *onward )->prev = link->prev ;
  link->prev->next = *onward ;

  /* Insert it in the new one. */
  before->next = link ;
  after->prev = link->sub_last != NULL ? link->sub_last : link ;
  link->prev = before ;
  *onward = after ;

  RR_ASRT_PAGE( rr_state , page ) ;
}

/** Decrement the hit count of a scan node, removing it if the count
    goes to zero. */

static void pdf_rr_decrement_scan_node_hits( PDFXCONTEXT *pdfxc ,
                                             RR_STATE *rr_state ,
                                             RR_SCAN_NODE **snp )
{
  RR_SCAN_NODE *sn = *snp ;

  /* If this is a supernode, we need to decrement the original scan
     node's hit count too. This can recurse more than once. Note that
     once more than one field in the union are in use we'll need to
     discriminate between types of scan node here. */

  if ( sn->u.orig != NULL ) {
    pdf_rr_decrement_scan_node_hits( pdfxc , rr_state , & sn->u.orig ) ;
  }

  if ( sn != rr_state->nil_sentinel && --sn->hits == 0 ) {
    RBT_NODE *node = rbt_search( rr_state->scantree ,
                                 ( uintptr_t )sn->hash ) ;
    RBT_NODE *removed ;

    RR_DBG( SEQUENCE_POINTS ,
            ( rr_state , ( uint8 * )"%%%% Purging "
              "(0x%02x%02x%02x%02x%02x%02x%02x%02x"
              "%02x%02x%02x%02x%02x%02x%02x%02x) [%d,%d,%d,%d]\n" ,
              sn->hash[ 0 ] , sn->hash[ 1 ] ,
              sn->hash[ 2 ] , sn->hash[ 3 ] ,
              sn->hash[ 4 ] , sn->hash[ 5 ] ,
              sn->hash[ 6 ] , sn->hash[ 7 ] ,
              sn->hash[ 8 ] , sn->hash[ 9 ] ,
              sn->hash[ 10 ] , sn->hash[ 11 ] ,
              sn->hash[ 12 ] , sn->hash[ 13 ] ,
              sn->hash[ 14 ] , sn->hash[ 15 ] ,
              sn->bbox.x1 , sn->bbox.y1 ,
              sn->bbox.x2 , sn->bbox.y2 )) ;
    HQASSERT( node != NULL , "Node not found!" ) ;

    removed = rbt_remove( rr_state->scantree , node ) ;
    rbt_free_node( rr_state->scantree , removed ) ;

    *snp = rr_state->nil_sentinel ;
  }
}

/** Finish the supernode identified by \c start and \c end. Decrements
    hit counts on subnodes and makes them candidates for freeing if
    that count reaches zero. Installs the new supernode at the
    appropriate place in the page's link array. Returns the new start
    and end in \c *new_start and \c *new_end because they may change:
    we move the lowest z-order subnode to the start so the overall
    z-order is representative. */

static Bool pdf_rr_finish_supernode( PDFXCONTEXT *pdfxc , uint32 page_index ,
                                     dbbox_t *bbox , Bool significant ,
                                     RR_PAGE_LINK *start , RR_PAGE_LINK *end ,
                                     RR_PAGE_LINK **new_start ,
                                     RR_PAGE_LINK **new_end )
{
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RR_PAGE *page ;
  RR_HASH_STATE hash_state ;
  uint8 hash[ RR_HASH_LENGTH ] ;
  RBT_NODE *supernode ;
  RR_SCAN_NODE *sn ;
  RR_PAGE_LINK *link ;
  RR_PAGE_MARK *mark ;
  RR_PAGE_LINK *lowest = start ;

  if ( new_start != NULL ) {
    *new_start = start ;
  }

  if ( new_end != NULL ) {
    *new_end = end ;
  }

  if ( start == end ) {
    /* Nothing to do. */
    return TRUE ;
  }

  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  page = & rr_state->pages[ page_index ] ;

  HQASSERT( rr_state != NULL , "Null retained raster state" ) ;
  HQASSERT( page_index < rr_state->page_count , "Page index out of range" ) ;
  HQASSERT( end != & page->tail , "Supernode must end with a real node" ) ;

  pdf_rr_hash_reset( ixc , & hash_state ) ;

  /* Calculate the final hash for the supernode. */

  for ( link = start ; link != end->next ; link = link->next ) {
    if ( link->mark_num < lowest->mark_num ) {
      lowest = link ;
    }

    sn = link->sn ;

    if ( sn != rr_state->nil_sentinel ) {
      HQASSERT( sn->hits > 0 , "Underflowing hit count" ) ;

      pdf_rr_hash( & hash_state , sn->hash , RR_HASH_LENGTH ) ;

      RR_DBG( SUPERNODES ,
              ( rr_state ,
                ( uint8 * )"  [%6d]%c 0x%02x%02x%02x%02x%02x%02x%02x%02x"
                "%02x%02x%02x%02x%02x%02x%02x%02x [%d,%d,%d,%d]\n" ,
                sn->hits , sn->is_form ? '*' : ' ' ,
                sn->hash[ 0 ] , sn->hash[ 1 ] ,
                sn->hash[ 2 ] , sn->hash[ 3 ] ,
                sn->hash[ 4 ] , sn->hash[ 5 ] ,
                sn->hash[ 6 ] , sn->hash[ 7 ] ,
                sn->hash[ 8 ] , sn->hash[ 9 ] ,
                sn->hash[ 10 ] , sn->hash[ 11 ] ,
                sn->hash[ 12 ] , sn->hash[ 13 ] ,
                sn->hash[ 14 ] , sn->hash[ 15 ] ,
                sn->bbox.x1 , sn->bbox.y1 ,
                sn->bbox.x2 , sn->bbox.y2 )) ;
    }

    link->sub_first = NULL ;
    link->sub_last = NULL ;
  }

  pdf_rr_hash_finalise( & hash_state , hash ) ;

  supernode = rbt_search( rr_state->scantree , ( uintptr_t )hash ) ;

  if ( supernode == NULL ) {
    RR_SCAN_NODE new_sn ;

    new_sn.hash = NULL ;
    new_sn.hits = 1 ;
    new_sn.bbox = *bbox ;
    new_sn.u.orig = NULL ;
    new_sn.last_page = MAXUINT32 ;
    new_sn.pages_hash = 0 ;
    new_sn.ptn = NULL ;
    new_sn.is_form = FALSE ;
    new_sn.has_matrix = FALSE ;
    new_sn.has_ref = FALSE ;
    new_sn.composited = FALSE ;
    new_sn.pattern = FALSE ;
    new_sn.significant = significant ;

    RR_DBG( SUPERNODES ,
            ( rr_state , ( uint8 * )"New supernode: "
              "0x%02x%02x%02x%02x%02x%02x%02x%02x"
              "%02x%02x%02x%02x%02x%02x%02x%02x [%d,%d,%d,%d]\n" ,
              hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
              hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
              hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
              hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ] ,
              bbox->x1 , bbox->y1 , bbox->x2 , bbox->y2 )) ;

    supernode = pdf_rr_scantree_createnode( pdfxc , rr_state->scantree ,
                                            hash , & new_sn ) ;
    if ( supernode == NULL ) {
      return FALSE ;
    }

    sn = rbt_get_node_data( supernode ) ;
  }
  else {
    sn = rbt_get_node_data( supernode ) ;
    sn->hits++ ;
    HQASSERT( sn->u.orig == start->sn , "Orig pointers should be identical" ) ;
  }

  if ( lowest != start ) {
    /* We must move the lowest mark in the original z-order which is
       part of this supernode to be at the start. This is so that IRR
       works correctly when it examines z-order indices to decide when
       to flush. Moving this one mark within a supernode can be done
       with impunity because marks belonging to the same supernode
       will always be drawn, in their original z-order, in a single
       pass over the content stream. */
    if ( lowest == end ) {
      end = end->prev ;
    }
    pdf_rr_move_node( rr_state , page , lowest , start->prev ) ;
    start = lowest ;
  }

  sn->u.orig = start->sn ;
  start->sn = sn ;
  start->sub_last = end ;
  end->sub_first = start ;

  mark = & page->marks[ start->mark_num ] ;

  if ( ! pdf_rr_add_node_to_page( pdfxc->pdfc , sn , start->mark_num ,
                                  FALSE , mark->has_ref ,
                                  mark->inline_data_bytes ,
                                  ( uint8 )mark->pdfop ,
                                  ( uint8 )mark->nesting , page_index )) {
    return FAILURE( FALSE ) ;
  }

  RR_ASRT_PAGE( rr_state , page ) ;

  if ( new_start != NULL ) {
    *new_start = start ;
  }

  if ( new_end != NULL ) {
    *new_end = end ;
  }

  return TRUE ;
}

/** Atomic supernodes are a specialisation of supernodes: they
    represent a set of nodes from the unadjusted z-order list which
    _must_ be handled as a single entity. Examples include text show
    operators bracketed by BT/ET pairs and groups of objects involved
    in a transparency context - although they're only included in this
    process if the \c transparency flag is set. */

static Bool pdf_rr_make_atomic_supernodes( PDFCONTEXT *pdfc ,
                                           uint32 page_num ,
                                           Bool transparency )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RR_PAGE *page ;
  uint32 super_start = 0 ;
  uint32 super_end = 0 ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  HQASSERT( rr_state != NULL , "Null retained raster state" ) ;

  page = & rr_state->pages[ page_num ] ;

  /* Now we must look for marks with transparency and make atomic
     nodes encompassing them and their background. For now, we make
     one huge one going from the first mark to the last transparent
     one. */

  if ( page->mark_count == 0 ) {
    return TRUE ;
  }

  if ( transparency ) {
    for ( super_end = page->mark_count - 1 ; super_end > 0 ; super_end-- ) {
      if ( page->links[ super_end ].sn->composited ) {
        break ;
      }
    }

    if ( super_end > 0 ) {
      Bool in_atom = FALSE ;

      for ( super_start = 1 ; super_start < super_end ; super_start++ ) {
        if ( page->marks[ super_start ].atom_start ) {
          in_atom = TRUE ;
          page->marks[ super_start ].atom_start = FALSE ;
        }

        if ( page->marks[ super_start ].atom_end ) {
          in_atom = FALSE ;
          page->marks[ super_start ].atom_end = FALSE ;
        }
      }

      /* Now set our new transparency atomic supernode markers. If the
         last transparent mark was in the middle of an existing atomic
         supernode, we don't need to set an end flag because we'll just
         re-use the one that's already set. */

      page->marks[ page->head.next->mark_num ].atom_start = TRUE ;
      if ( ! in_atom ) {
        page->marks[ super_end ].atom_end = TRUE ;
      }
    }

#if defined( DEBUG_BUILD )
    if (( debug_retained_raster & DEBUG_RR_Z_ORDER ) != 0 ) {
      pdf_rr_dump_page_links( rr_state , page_num , "02-tran-flags" , TRUE ) ;
    }
#endif
  }
  else {
#if defined( DEBUG_BUILD )
    if (( debug_retained_raster & DEBUG_RR_Z_ORDER ) != 0 ) {
      pdf_rr_dump_page_links( rr_state , page_num , "00-orig" , FALSE ) ;
    }
#endif
  }

#if defined( ASSERT_BUILD )
  {
    uint32 check ;
    uint32 atom_starts = 0 ;
    uint32 atom_ends = 0 ;

    for ( check = 0 ; check < page->mark_count ; check++ ) {
      if ( page->marks[ check ].atom_start ) {
        atom_starts++ ;
      }

      if ( page->marks[ check ].atom_end ) {
        atom_ends++ ;
      }
    }

    HQASSERT( atom_starts == atom_ends ,
              "Atom encapsulation's gone wonky" ) ;
  }
#endif

  super_start = 0 ;
  super_end = 0 ;

  while ( super_start < page->mark_count ) {
    dbbox_t bbox ;
    Bool significant ;

    pdf_rr_next_atomic_supernode( page , & super_start , & super_end , & bbox ,
                                  & significant ) ;

    HQASSERT( super_end != page->mark_count ,
              "Super end should be a page node, not the tail sentinel." ) ;

    if ( super_start == super_end ) {
      /* It's degenerate: nothing more to do. */
      super_start = super_end + 1 ;
      continue ;
    }

    if ( ! pdf_rr_finish_supernode( pdfxc , page_num , & bbox , significant ,
                                    & page->links[ super_start ] ,
                                    & page->links[ super_end ] ,
                                    NULL , NULL )) {
      return FALSE ;
    }

    /* Update the flags so we don't try to promote this atom again. */

    page->marks[ super_end ].atom_end = FALSE ;
    page->marks[ super_start ].atom_start = FALSE ;

    /* Begin looking for the next atom. */

    super_start = super_end + 1 ;
  }

#if defined( DEBUG_BUILD )
  if (( debug_retained_raster & DEBUG_RR_SUPERNODES ) != 0 ) {
    pdf_rr_dump_page_links( rr_state , page_num ,
                            transparency ? "03-tran-atom" : "01-atom" ,
                            transparency ) ;
  }
#endif

  return TRUE ;
}

/** Make a new pages tree node. This will be populated later. */

static RR_PAGES_NODE *pdf_rr_make_ptn( PDFXCONTEXT *pdfxc , RR_SCAN_NODE *sn )
{
  RBT_NODE *node ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RR_PAGES_NODE *ptn = NULL ;
  uint32 i ;

  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  node = rbt_allocate_node( rr_state->pagestree ,
                            ( uintptr_t )sn->pages_hash , NULL ) ;

  if ( node == NULL ) {
    ( void )error_handler( VMERROR ) ;
    return NULL ;
  }

  ptn = mm_alloc( pdfxc->mm_structure_pool , sizeof( *ptn ) +
                  ( sizeof( ptn->index[ 0 ]) * ( sn->hits - 1 )) ,
                  MM_ALLOC_CLASS_PDF_RR_PAGES_NODE ) ;

  if ( ptn == NULL ) {
    ( void )error_handler( VMERROR ) ;
    rbt_free_node( rr_state->pagestree , node ) ;
    return NULL ;
  }

  rbt_insert( rr_state->pagestree , node ) ;
  ptn->size = sn->hits ;
  ptn->count = 0 ;

  for ( i = 0 ; i < ptn->size ; i++ ) {
    ptn->index[ i ] = MAXUINT32 ;
  }

  rbt_set_node_data( rr_state->pagestree , node , ptn ) ;
  return ptn ;
}

/** Make pages tree nodes for the scan nodes on the given page, and
    update extant pages tree nodes with the \c page_num. */

static Bool pdf_rr_make_pages_tree_nodes( PDFCONTEXT *pdfc , uint32 page_num )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RR_PAGE *page ;
  RR_PAGE_LINK *link ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  HQASSERT( rr_state != NULL , "Null retained raster state" ) ;

  page = & rr_state->pages[ page_num ] ;

  for ( link = page->head.next ; link != & page->tail ;
        link = link->sub_last != NULL ? link->sub_last->next : link->next ) {
    RR_SCAN_NODE *sn = link->sn ;

    if ( sn->ptn == NULL ) {
      RBT_NODE *node = rbt_search( rr_state->pagestree ,
                                   ( uintptr_t )sn->pages_hash ) ;

      if ( node != NULL ) {
        sn->ptn = ( RR_PAGES_NODE * )rbt_get_node_data( node ) ;
      }
      else {
        sn->ptn = pdf_rr_make_ptn( pdfxc , sn ) ;
      }
    }

    HQASSERT( sn->ptn != NULL , "Should have a ptn in place by now" ) ;

    if ( sn->ptn->count == 0 ||
         sn->ptn->index[ sn->ptn->count - 1 ] != page_num ) {
      /* This ignores the second and subsequent instances of the mark
         on a single page, but that shouldn't be a problem. */
      HQASSERT( sn->ptn->count < sn->ptn->size , "Writing off end of ptn" ) ;
      sn->ptn->index[ sn->ptn->count++ ] = page_num ;
    }
  }

  return TRUE ;
}

/** Simple function to determine whether \c link should be allowed to
    be part of the same supernode as \c *primary. If both scan nodes
    appear on exacly the same set of pages then the answer is "yes" -
    but it should also be so in certain other circumstances too. When
    we spot a node that should become the new focus for building the
    supernode in hand, this function will update the value of \c
    *primary accordingly. */

static Bool pdf_rr_supernode_test( RR_STATE *rr_state , RR_SCAN_NODE **primary ,
                                   RR_PAGE_LINK *link )
{
  Bool result = FALSE ;
  RBT_NODE *node ;
  RR_PAGES_NODE *ppn = NULL ;
  RR_PAGES_NODE *pn = NULL ;
  RR_SCAN_NODE *sn = link->sn ;

  node = rbt_search( rr_state->pagestree ,
                     ( uintptr_t )( *primary )->pages_hash ) ;

  if ( node != NULL ) {
    ppn = rbt_get_node_data( node ) ;
  }

  node = rbt_search( rr_state->pagestree , ( uintptr_t )sn->pages_hash ) ;

  if ( node != NULL ) {
    pn = rbt_get_node_data( node ) ;
  }

  if ( ppn == NULL || pn == NULL ) {
    HQFAIL( "Pages node(s) not found" ) ;
    return FALSE ;
  }
  else if ( ppn->count == 0 || pn->count == 0 ) {
    /* The nil sentinel has an empty pages tree node: existing
       subnodes never pass the supernode test. */
    return FALSE ;
  }

  if ( ppn == pn ) {
    /* Easy shortcut: if the page ranges are identical then the
       addition of sn to the supernode is always allowed. */
    RR_DBG( Z_ORDER , ( rr_state , ( uint8 * )"++ identical: %d\n" ,
                        link->mark_num )) ;
    result = TRUE ;
  }
  else if (( *primary )->significant && ! sn->significant &&
           ppn->count > pn->count ) {
    /* An insignificant mark isn't allowed to reduce the hit count of
       a significant supernode. */
    RR_DBG( Z_ORDER , ( rr_state , ( uint8 * )"-- insignificant lesser: %d\n" ,
                        link->mark_num )) ;
    result = FALSE ;
  }
  else if ( pn->count == 1 ) {
    /* A variable-data node can only be added to an insignificant
       or variable-data supernode. */
    result = ( ! ( *primary )->significant || ppn->count == 1 ) ;
    RR_DBG( Z_ORDER , ( rr_state ,
                        ( uint8 * )"%s insignificant variable: %d\n" ,
                        result ? "++" : "--" , link->mark_num )) ;
  }
  else {
    RR_PAGES_NODE *inner ;
    RR_PAGES_NODE *outer ;
    uint32 common = 0 ;
    uint32 i = 0 ;
    uint32 j = 0 ;

    if ( ppn->count > pn->count ) {
      inner = pn ;
      outer = ppn ;
    }
    else {
      inner = ppn ;
      outer = pn ;
    }

    while ( i < inner->count && j < outer->count ) {
      if ( inner->index[ i ] == outer->index[ j ]) {
        common++ ; i++ ; j++ ;
      }
      else if ( inner->index[ i ] < outer->index[ j ]) {
        i++ ;
      }
      else {
        j++ ;
      }
    }

    if (( *primary )->significant ) {
      if ( sn->significant ) {
        result = ( common * 2 >= ppn->count ) ;
        RR_DBG( Z_ORDER , ( rr_state ,
                            ( uint8 * )"%s both significant: %d\n" ,
                            result ? "++" : "--" , link->mark_num )) ;
      }
      else {
        result = ( common == ppn->count ) ;
        RR_DBG( Z_ORDER , ( rr_state ,
                            ( uint8 * )"%s insignificant subnode: %d\n" ,
                            result ? "++" : "--" , link->mark_num )) ;
      }
    }
    else {
      if ( sn->significant ) {
        result = ( common == pn->count ) ;
        RR_DBG( Z_ORDER , ( rr_state ,
                            ( uint8 * )"%s significant subnode: %d\n" ,
                            result ? "++" : "--" , link->mark_num )) ;
      }
      else {
        result = ( common * 2 >= ppn->count ) ;
        RR_DBG( Z_ORDER , ( rr_state ,
                            ( uint8 * )"%s both insignificant: %d\n" ,
                            result ? "++" : "--" , link->mark_num )) ;
      }
    }
  }

  if ( result && ( ppn->count > pn->count ||
                   ( ! ( *primary )->significant && sn->significant ))) {
    *primary = sn ;
    RR_DBG( Z_ORDER , ( rr_state ,
                        ( uint8 * )"(New primary: %d)\n" , link->mark_num )) ;
  }

  return result ;
}

/** Find the next supernode in the given page's node list. Updates the
    hashing state \c rr_super_state as it goes along with \c *bbox and
    \c *sig_mask. Also \c *count is set to the number of subnodes in
    the supernode. Starts at node index \c *super_start and on exit
    sets \c *super_start and \c *super_end to the indices for the
    supernode it's found. They could be equal if we're looking at the
    last node on the page, which of course means the supernode isn't
    so super. If the \c everything flag is set, the supernode must
    encompass all nodes from *start to the tail (i.e. the normal
    supernode tests don't apply). */

static void pdf_rr_next_supernode( PDFXCONTEXT *pdfxc , RR_PAGE *page ,
                                   RR_PAGE_LINK **start , RR_PAGE_LINK **end ,
                                   dbbox_t *bbox , Bool *significant ,
                                   Bool everything )
{
  RR_PAGE_LINK *link ;
  RR_SCAN_NODE *primary ;
#if defined( METRICS_BUILD )
  Bool counted = FALSE ;
#endif
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;

  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  HQASSERT( rr_state != NULL , "Shouldn't be here without an RR state" ) ;

  bbox_clear( bbox ) ;
  *end = *start ;
  primary = ( *start )->sn ;
  *significant = primary->significant ;

  for ( link = *start ; link != & page->tail ; link = link->sub_last != NULL ?
          link->sub_last->next : link->next ) {
    Bool supernode = pdf_rr_supernode_test( rr_state , & primary , link ) ;

#if defined( METRICS_BUILD )
    if ( ! counted && ! supernode && everything ) {
      counted = TRUE ;
      pdf_rr_metrics.internal_uncacheable_count++ ;
    }
#endif

    if ( supernode || everything ) {
      *end = link->sub_last != NULL ? link->sub_last : link ;
      bbox_union( bbox , & link->sn->bbox , bbox ) ;
      *significant |= link->sn->significant ;
    }
    else {
      /* Look further in the z-order for more nodes which could be
         part of this supernode. */

#if defined( DEBUG_BUILD )
      if (( tweak_retained_raster & TWEAK_RR_DISABLE_Z_ORDER_MOVES ) != 0 ) {
        return ;
      }
#endif

      for ( ; ; ) {
        for ( ; link != & page->tail ; link = link->sub_last != NULL ?
                link->sub_last->next : link->next ) {
          if ( pdf_rr_supernode_test( rr_state , & primary , link )) {
            break ;
          }
        }

        if ( link == & page->tail ) {
          /* We got to the end without finding any more nodes to add. */
          return ;
        }
        else {
          /* Attempt to move the new subnode down in the z-order to
             immediately after the current supernode's end. If there
             are any intersections then this is not possible and it
             will not be part of the current supernode. Note that the
             z-order move is all or nothing: there's no point moving
             it if it can't move all the way to where it needs to
             be. */
          RR_PAGE_LINK *prev ;
          Bool do_move = FALSE ;

          /* When checking for intersections with previous objects we
             need to ensure we're always looking at the head of any
             supernode rather than the last of its subnodes. */

          for ( prev = link->prev->sub_first != NULL ?
                  link->prev->sub_first : link->prev ; prev != & page->head ;
                prev = prev->prev->sub_first != NULL ?
                  prev->prev->sub_first : prev->prev ) {
            if (( prev->sub_last != NULL && prev->sub_last == *end ) ||
                ( prev->sub_last == NULL && prev == *end )) {
              /* Note that the page->head termination test in the loop
                 is just a backstop: we should always come through
                 here. */
              do_move = TRUE ;
              break ;
            }

            if ( bbox_intersects( & prev->sn->bbox , & link->sn->bbox )) {
              RR_DBG( Z_ORDER ,
                      ( rr_state , ( uint8 * )"Intersection: %d and %d\n" ,
                        prev->mark_num , link->mark_num )) ;
              break ;
            }
          }

          if ( do_move ) {
            pdf_rr_move_node( rr_state , page , link , prev ) ;

            /* Resume creating the supernode from where we left off. */
            link = *end ;
            /*@innerbreak@*/
            break ;
          }
          else {
            RR_DBG( Z_ORDER , ( rr_state , ( uint8 * )"Not moving: %d\n" ,
                                link->mark_num )) ;
            link = ( link->sub_last != NULL ?
                     link->sub_last->next : link->next ) ;
          }
        }
      }
    }
  }
}

/** Move nodes down in the z-order if allowed (i.e. if the page output
    won't change) in order to converge cacheable nodes together, so we
    can minimise the number of nodes we're dealing with by creating
    supernodes. These are simply a list of subnodes which are
    consecutive in the (adjusted) z-order and can safely be treated
    alike. */

static Bool pdf_rr_find_supernodes( PDFCONTEXT *pdfc , uint32 min_page ,
                                    uint32 max_page )
{
  PDFXCONTEXT *pdfxc ;
  RR_HASH_STATE hash_state ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  uint32 i ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  pdf_rr_hash_reset( ixc , & hash_state ) ;

  HQASSERT( rr_state != NULL , "Null retained raster state" ) ;

  for ( i = min_page ; i <= max_page ; i++ ) {
    if ( ! pdf_rr_make_atomic_supernodes( pdfc , i , TRUE )) {
      return FAILURE( FALSE ) ;
    }

    if ( rr_state->tl_ref != SW_TL_REF_INVALID ) {
      CHECK_TL_SUCCESS( SwTimelineSetProgress( rr_state->tl_ref ,
                                               ( sw_tl_extent )i )) ;
    }
  }

  for ( i = min_page ; i <= max_page ; i++ ) {
    if ( ! pdf_rr_make_pages_tree_nodes( pdfc , i )) {
      return FAILURE( FALSE ) ;
    }

    RR_ASRT_PAGE( rr_state , & rr_state->pages[ i ]) ;

    if ( rr_state->tl_ref != SW_TL_REF_INVALID ) {
      CHECK_TL_SUCCESS( SwTimelineSetProgress( rr_state->tl_ref ,
                          ( sw_tl_extent )2 * max_page + i )) ;
    }
  }

#if defined( DEBUG_BUILD )
  if (( tweak_retained_raster & TWEAK_RR_DISABLE_AUTO_SUPERNODES ) != 0 ) {
    return TRUE ;
  }
#endif

  /* Move all single-hit or insignificant nodes as high as they'll go
     in the z-order. All those which make it all the way to the top
     are then made into a supernode to keep them together during the
     second pass which makes all the other supernodes. This gives us a
     better chance of a) representing the page contents in as few
     raster elements as possible and b) having cacheable content left
     as the lowest in the adjusted z-order so that IRR has something
     to work with. */

  for ( i = min_page ; i <= max_page ; i++ ) {
    RR_PAGE *page = & rr_state->pages[ i ] ;
    RR_PAGE_LINK *link ;
    RR_PAGE_LINK *start = NULL ;
    RR_PAGE_LINK *last_hoisted = NULL ;

    RR_DBG_OPEN( Z_ORDER , rr_state , "z-var" , i , MAXUINT32 ) ;

    for ( link = page->tail.prev->sub_first != NULL ?
            page->tail.prev->sub_first : page->tail.prev ;
          link != & page->head ; ) {
      Bool hoist_insignificant_mark = FALSE ;

      /* Remember the next value for the loop index before we mess
         with the z-order. */

      RR_PAGE_LINK *prev_link = link->prev->sub_first != NULL ?
        link->prev->sub_first : link->prev ;

      if ( link->sn->hits > 1 && ! link->sn->significant ) {
        RR_PAGE_LINK *prev ;

        /* We'll hoist this insignificant mark high in the z-order
           only if there's a genuine variable data mark underneath
           it. */
        for ( prev = link->sub_first != NULL ?
                link->sub_first->prev : link->prev ;
              prev != & page->head ;
              prev = prev->sub_first != NULL ?
                prev->sub_first->prev : prev->prev ) {
          if ( prev->sn->hits <= 1 &&
               bbox_intersects( & link->sn->bbox , & prev->sn->bbox )) {
            RR_DBG( Z_ORDER ,
                    ( rr_state , ( uint8 * )"Hoisting %d\n" ,
                      link->mark_num )) ;
            hoist_insignificant_mark = TRUE ;
            break ;
          }
        }
      }

      if ( link->sn->hits <= 1 || hoist_insignificant_mark ) {
        /* This is a variable data node or one that we don't mind
           treating as such: move it as high in the z-order as it will
           go. */

        RR_PAGE_LINK *up ;

        for ( up = link->sub_last != NULL ? link->sub_last->next : link->next ;
              up != & page->tail ;
              up = up->sub_last != NULL ? up->sub_last->next : up->next ) {
          if ( bbox_intersects( & link->sn->bbox , & up->sn->bbox )) {
            RR_DBG( Z_ORDER ,
                    ( rr_state , ( uint8 * )"Intersection: %d and %d\n" ,
                      link->mark_num , up->mark_num )) ;
            /*@innerbreak@*/
            break ;
          }
        }

        if (( link->sub_last != NULL ?
              link->sub_last->next : link->next ) != up ) {
          pdf_rr_move_node( rr_state , page , link , up->prev ) ;
          if ( hoist_insignificant_mark ) {
            last_hoisted = link ;
          }
        }
      }

      link = prev_link ;
    }

    RR_DBG( Z_ORDER , ( rr_state , ( uint8 * )"\n\n" )) ;

    if ( last_hoisted != NULL ) {
      link = ( last_hoisted->prev->sub_first != NULL ?
               last_hoisted->prev->sub_first : last_hoisted->prev ) ;
    }
    else {
      link = ( page->tail.prev->sub_first != NULL ?
               page->tail.prev->sub_first : page->tail.prev ) ;
    }

    for ( ; link != & page->head && link->sn->hits <= 1 ;
          link = link->prev->sub_first != NULL ?
            link->prev->sub_first : link->prev ) {
      start = link ;
    }

    RR_DBG_CLOSE( Z_ORDER , rr_state ) ;

#if defined( DEBUG_BUILD )
    if (( debug_retained_raster & DEBUG_RR_SUPERNODES ) != 0 ) {
      pdf_rr_dump_page_links( rr_state , i , "04-z-var" , TRUE ) ;
    }
#endif

    if ( start != NULL ) {
      dbbox_t bbox ;
      Bool significant = FALSE ;
      RR_PAGE_LINK *end = start ;

      RR_DBG_OPEN( SUPERNODES , rr_state , "var" , i , MAXUINT32 ) ;

      bbox_clear( & bbox ) ;

      /* This loop deliberately iterates into the subnodes of existing
         supernodes. */

      for ( link = start ; link != & page->tail ; link = link->next ) {
        RR_SCAN_NODE *sn = link->sn ;

        if ( page->marks[ link->mark_num ].is_form &&
             ! page->marks[ link->mark_num ].has_ref ) {
          continue ;
        }

        end = link ;
        bbox_union( & bbox , & sn->bbox , & bbox ) ;
        significant |= sn->significant ;
      }

      if ( ! pdf_rr_finish_supernode( pdfxc , i , & bbox , significant ,
                                      start , end , NULL , NULL )) {
        return FALSE ;
      }

      RR_DBG_CLOSE( SUPERNODES , rr_state ) ;
    }

#if defined( DEBUG_BUILD )
    if (( debug_retained_raster & DEBUG_RR_SUPERNODES ) != 0 ) {
      pdf_rr_dump_page_links( rr_state , i , "05-z-super" , TRUE ) ;
    }
#endif

    if ( rr_state->tl_ref != SW_TL_REF_INVALID ) {
      CHECK_TL_SUCCESS( SwTimelineSetProgress( rr_state->tl_ref ,
                          ( sw_tl_extent )2 * max_page + i )) ;
    }
  }

  /* Second pass: make all the remaining supernodes for the page -
     move nodes with matching sets of pages down in the z-order so
     they're all together, intersections allowing. */

  for ( i = min_page ; i <= max_page ; i++ ) {
    RR_PAGE *page = & rr_state->pages[ i ] ;
    RR_PAGE_LINK *super_start = page->head.next ;
    dbbox_t bbox ;
    Bool significant ;

    RR_DBG_OPEN( Z_ORDER , rr_state , "z-super" , i , MAXUINT32 ) ;

    while ( super_start != & page->tail ) {
      RR_PAGE_LINK *super_end = super_start ;

      pdf_rr_next_supernode( pdfxc , page , & super_start , & super_end ,
                             & bbox , & significant ,
                             ( ! ixc->OptimizedPDFExternal &&
                               ( super_start != page->head.next ))) ;

      HQASSERT( super_end != & page->tail ,
                "Super end should be a page node, not the tail sentinel." ) ;

      if ( super_start == super_end ||
           super_start->sub_last == super_end ) {
        /* It's degenerate or already a supernode: nothing more to
           do. */
        if ( super_end->sub_last != NULL ) {
          super_start = super_end->sub_last->next ;
        }
        else {
          super_start = super_end->next ;
        }
        continue ;
      }

      RR_DBG( Z_ORDER , ( rr_state , ( uint8 * )"\n\n" )) ;

      if ( ! pdf_rr_finish_supernode( pdfxc , i , & bbox , significant ,
                                      super_start , super_end ,
                                      & super_start , & super_end )) {
        return FALSE ;
      }

      RR_ASRT_PAGE( rr_state , page ) ;

      /* Begin looking for the next supernode. */

      super_start = super_end->next ;
    }

    RR_DBG_CLOSE( Z_ORDER , rr_state ) ;

    if ( rr_state->tl_ref != SW_TL_REF_INVALID ) {
      CHECK_TL_SUCCESS( SwTimelineSetProgress( rr_state->tl_ref ,
                          ( sw_tl_extent )3 * max_page + i )) ;
    }
  }

#if defined( DEBUG_BUILD )
  for ( i = min_page ; i <= max_page ; i++ ) {
    if (( debug_retained_raster & DEBUG_RR_SUPERNODES ) != 0 ) {
      pdf_rr_dump_page_links( rr_state , i , "06-super" , TRUE ) ;
    }
  }
#endif

  return TRUE ;
}

/** Hook which is called when a new setpagedevice comes along. We need
    this because when RR scanning is active we need to reinstate our
    suppress device setting. */

Bool pdf_rr_newpagedevice( PDFXCONTEXT *pdfxc )
{
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  DL_STATE *page = pdfxc->corecontext->page ;

  HQASSERT_LPTR( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  if ( rr_state != NULL &&
       ( rr_state->mode == RR_MODE_SCANNING ||
         rr_state->mode == RR_MODE_EXPORTING ||
         rr_state->mode == RR_MODE_STORING )) {
    SET_DEVICE( DEVICE_SUPPRESS ) ;
  }

  /* Knockouts are output in the ContoneMask value, and separation omission
     must include the knockouts when determining separation marks to ensure
     correct output when the page elements are stitched together. */
  if ( page->colorPageParams.contoneMask != 0 ) {
    guc_omitSetIgnoreKnockouts( page->hr , FALSE ) ;
  }

  return TRUE ;
}

/** Close the scanning window on the given page. Finalise scanning
    results and jettison the scanning state which is no longer
    needed. */

static Bool pdf_rr_close_window( PDFCONTEXT *pdfc , uint32 page_num )
{
  Bool result = FALSE ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RR_PAGE *page ;
  RR_PAGE_LINK *link ;
  uint32 count = 0 ;
  uint32 ln ;
  uint32 i ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  HQASSERT( rr_state != NULL , "No rr_state" ) ;
  page = & rr_state->pages[ page_num ] ;

  RR_DBG_OPEN( SCAN_WINDOW , rr_state , "window" , page_num , MAXUINT32 ) ;

  RR_DBG( SCAN_WINDOW ,
          ( rr_state , ( uint8 * )"%%%% [] Closing scan window: "
            "%d nodes %u bytes\n" , rbt_node_count( rr_state->scantree ) ,
            mm_pool_alloced_size( rr_state->mark_pool ))) ;

  /* Count up the number of elements on this page. */

  for ( link = page->head.next ; link != & page->tail ;
        link = link->sub_last != NULL ? link->sub_last->next : link->next ) {
    count++ ;
  }

  page->elements = mm_alloc( pdfxc->mm_structure_pool ,
                             ( sizeof( *page->elements ) * count ) ,
                             MM_ALLOC_CLASS_PDF_RR_ELEMENTS ) ;

  if ( page->elements == NULL ) {
    ( void )error_handler( VMERROR ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }

#if defined( METRICS_BUILD )
  pdf_rr_metrics.element_count += count ;
#endif
  page->element_count = count ;
  count = 0 ;

  for ( link = page->head.next ; link != & page->tail ;
        link = link->sub_last != NULL ? link->sub_last->next : link->next ) {
    RBT_NODE *node ;
    RR_MARKS_NODE *mn ;
    RR_ELEM_NODE *en ;
    uint32 marks_hash = 0 ;
    uint32 mark ;

    /* We start each time around by marking everything which isn't
       a form placeholder mark as omitted. */

    for ( mark = 0 ; mark < page->mark_count ; mark++ ) {
      page->marks[ mark ].omit = ( ! page->marks[ mark ].is_form ||
                                   page->marks[ mark ].has_ref ) ;
    }

    node = rbt_search( rr_state->elemtree , ( uintptr_t )link->sn->hash ) ;

    if ( node == NULL ) {
      RR_ELEM_NODE new_en ;
      SWMSG_RR_ELEMENT_DEFINE ed ;
      sw_event_result er ;

      new_en.bbox = link->sn->bbox ;
      new_en.hits = 0 ;
      new_en.counted = FALSE ;
#if defined( DEBUG_BUILD )
      new_en.index = rr_state->unique_element_count++ ;
#endif
      node = pdf_rr_hashtree_createnode( rr_state->elemtree ,
                                         ( uintptr_t )link->sn->hash ,
                                         & new_en ) ;

      /** \todo Need to document that it's possible we'll send more
          than one element define because it can exist in more than
          one disparate scanning window. */

      ed.connection = rr_state->cache ;
      ed.timeline = rr_state->tl_ref ;
      ed.id = ( uint8 * )link->sn->hash ;
      ed.x1 = link->sn->bbox.x1 ;
      ed.y1 = link->sn->bbox.y1 ;
      ed.x2 = link->sn->bbox.x2 ;
      ed.y2 = link->sn->bbox.y2 ;

      er = SwEvent( SWEVT_RR_ELEMENT_DEFINE , & ed , sizeof( ed )) ;

      if ( er >= SW_EVENT_ERROR || er == SW_EVENT_UNHANDLED ) {
        FAILURE_GOTO( CLEANUP ) ;
      }
    }

    if ( node == NULL ) {
      FAILURE_GOTO( CLEANUP ) ;
    }

    en = rbt_get_node_data( node ) ;
    HQASSERT( en != NULL , "Didn't find elem node data" ) ;

    en->hits++ ;

    page->marks[ link->mark_num ].omit = FALSE ;

    if ( link->sub_last != NULL ) {
      RR_PAGE_LINK *limit = link->sub_last->next ;
      RR_PAGE_LINK *sublink ;

      for ( sublink = link->next ; sublink != limit ;
            sublink = sublink->next ) {
        page->marks[ sublink->mark_num ].omit = FALSE ;
      }
    }

    page->elements[ count ].id = ( uint8 * )rbt_get_node_key( node ) ;
    MurmurHash3_32(( uint8 * )page->marks , ( sizeof( *page->marks ) *
                                              page->mark_count ) ,
                   marks_hash , & marks_hash ) ;
    page->elements[ count ].marks_hash = marks_hash ;

    node = rbt_search( rr_state->markstree , ( uintptr_t )marks_hash ) ;

    if ( node == NULL ) {
      node = rbt_allocate_node( rr_state->markstree , ( uintptr_t )marks_hash ,
                                NULL ) ;

      if ( node == NULL ) {
        ( void )error_handler( VMERROR ) ;
        FAILURE_GOTO( CLEANUP ) ;
      }

      rbt_insert( rr_state->markstree , node ) ;
    }

    mn = rbt_get_node_data( node ) ;

    if ( mn == NULL ) {
      mn = mm_alloc( pdfxc->mm_structure_pool , sizeof( *mn ) +
                     ( sizeof( mn->marks[ 0 ]) * ( page->mark_count - 1 )) ,
                     MM_ALLOC_CLASS_PDF_RR_MARKS_NODE ) ;

      if ( mn == NULL ) {
        ( void )error_handler( VMERROR ) ;
        FAILURE_GOTO( CLEANUP ) ;
      }

      mn->count = page->mark_count ;
      HqMemCpy( & mn->marks[ 0 ] , page->marks ,
                ( sizeof( *page->marks ) * page->mark_count )) ;
      rbt_set_node_data( rr_state->markstree , node , mn ) ;
    }

    RR_DBG( PAGE_ELEMENTS ,
            ( rr_state , ( uint8 * )"%%%% Page %u, element %d: %u marks [" ,
              page_num + 1 , count , mn->count )) ;
    for ( i = 0 ; i < mn->count ; i++ ) {
      RR_DBG( PAGE_ELEMENTS , ( rr_state , ( uint8 * )"%c" ,
                                mn->marks[ i ].omit ? ' ' : 'x' )) ;
    }
    RR_DBG( PAGE_ELEMENTS ,
            ( rr_state , ( uint8 * )"]\n" )) ;

    {
      SWMSG_RR_ELEMENT_UPDATE_HITS uh ;

      uh.connection = rr_state->cache ;
      uh.timeline = rr_state->tl_ref ;
      uh.id = ( uint8 * )link->sn->hash ;
      uh.raise = TRUE ;
      uh.hits_delta = 1u ;

      if ( SwEvent( SWEVT_RR_ELEMENT_UPDATE_HITS , & uh ,
                    sizeof( uh )) >= SW_EVENT_ERROR ) {
        FAILURE_GOTO( CLEANUP ) ;
      }
      /* SW_EVENT_UNHANDLED is not an error here. */
    }

    count++ ;
  }

#if defined( DEBUG_BUILD )
  if (( debug_retained_raster & DEBUG_RR_ACTIVE_NODES ) != 0 ) {
    RR_DBG_OPEN( ACTIVE_NODES , rr_state , "active" ,
                 MAXUINT32 , MAXUINT32 ) ;
    RR_DBG( ACTIVE_NODES , ( rr_state , ( uint8 * )"{\n" )) ;

    for ( count = 0 ; count < page->element_count ; count++ ) {
      RR_PAGE_ELEMENT *e = & page->elements[ count ] ;
      RBT_NODE *node ;
      RR_ELEM_NODE *en ;

      node = rbt_search( rr_state->elemtree , ( uintptr_t )e->id ) ;
      HQASSERT( node != NULL , "Didn't find elem tree node" ) ;
      en = rbt_get_node_data( node ) ;
      HQASSERT( node != NULL , "Didn't find elem tree node data" ) ;

      pdf_rr_dump_active_node( rr_state , en , page_num , count ,
                               e->id , "  " ) ;
    }

    RR_DBG( ACTIVE_NODES , ( rr_state , ( uint8 * )"}\n" )) ;
    RR_DBG_CLOSE( ACTIVE_NODES , rr_state ) ;
  }
#endif

  for ( ln = 0 ; ln < page->mark_count ; ln++ ) {
    RR_SCAN_NODE *sn = page->links[ ln ].sn ;

    if ( sn != rr_state->nil_sentinel ) {
      pdf_rr_decrement_scan_node_hits( pdfxc , rr_state ,
                                       & page->links[ ln ].sn ) ;
    }
  }

  mm_free( pdfxc->mm_structure_pool , page->marks ,
           ( sizeof( *page->marks ) * page->mark_count )) ;
  page->marks = NULL ;
  mm_free( pdfxc->mm_structure_pool , page->links ,
           ( sizeof( *page->links ) * page->mark_count )) ;
  page->links = NULL ;
  page->head.next = & page->tail ;
  page->tail.prev = & page->head ;

  RR_DBG( SCAN_WINDOW ,
          ( rr_state , ( uint8 * )"%%%% [] After window close: "
            "%d nodes %u bytes\n" , rbt_node_count( rr_state->scantree ) ,
            mm_pool_alloced_size( rr_state->mark_pool ))) ;
  RR_DBG_CLOSE( SCAN_WINDOW , rr_state ) ;

  result = TRUE ;

 CLEANUP:

  return result ;
}

/** Walk the completed list of pages and their elements and output the
    summary statistics. */

static void pdf_rr_page_totals( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  uint32 pages_variable = 0 ;
  uint32 pages_fixed = 0 ;
  uint32 pages_combined = 0 ;
  uint32 i ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  HQASSERT( rr_state != NULL , "No rr_state" ) ;

  RR_DBG_OPEN( ACTIVE_NODES , rr_state , "retained" , MAXUINT32 , MAXUINT32 ) ;

  for ( i = 0 ; i < rr_state->page_count ; i++ ) {
    RR_PAGE *page = & rr_state->pages[ i ] ;
    uint32 cached_count = 0 ;
    uint32 hits_total = 0 ;
    Bool variable = FALSE ;
    Bool cached = FALSE ;
    uint32 ei ;

    for ( ei = 0 ; ei < page->element_count ; ei++ ) {
      RR_PAGE_ELEMENT *e = & page->elements[ ei ] ;
      RBT_NODE *node ;
      RR_ELEM_NODE *en ;

      node = rbt_search( rr_state->elemtree , ( uintptr_t )e->id ) ;
      HQASSERT( node != NULL , "Didn't find elem tree node" ) ;
      en = rbt_get_node_data( node ) ;
      HQASSERT( node != NULL , "Didn't find elem tree node data" ) ;

      if ( en->hits > 1 && ( ixc->OptimizedPDFExternal || ei == 0 )) {
        if ( ! en->counted ) {
          cached_count++ ;
          hits_total += en->hits ;
          en->counted = TRUE ;

#if defined( DEBUG_BUILD )
          if (( debug_retained_raster & DEBUG_RR_ACTIVE_NODES ) != 0 ) {
            pdf_rr_dump_active_node( rr_state , en , i , ei , e->id , "" ) ;
          }
#endif
        }

        cached = TRUE ;
      }
      else {
        variable = TRUE ;
      }
    }

    if ( cached ) {
      if ( variable ) {
        pages_combined++ ;
      }
      else {
        pages_fixed++ ;
      }
    }
    else {
      HQASSERT( variable , "There must be at least one element per page" ) ;
      pages_variable++ ;
    }

    if ( cached_count > 0 ) {
      /* UVM( "Retaining %[0]d rasters from page %[1]d which will be used a total of %[2]d times." ) */
      monitorf(( uint8 * )"Retaining %d rasters from page %d which will "
                          "be used a total of %d times.\n" ,
               cached_count , i + 1 , hits_total ) ;
    }
  }

  RR_DBG_CLOSE( ACTIVE_NODES , rr_state ) ;

  monitorf(( uint8 * )UVM( "Page totals: %d (%d%%) "
                           "variable, %d (%d%%) fixed and "
                           "%d (%d%%) combined.\n" ) ,
           pages_variable ,
           ( int )( 0.5 + 100.0 * pages_variable / rr_state->page_count ) ,
           pages_fixed ,
           ( int )( 0.5 + 100.0 * pages_fixed / rr_state->page_count ) ,
           pages_combined ,
           ( int )( 0.5 + 100.0 * pages_combined / rr_state->page_count )) ;

#if defined( METRICS_BUILD )
  pdf_rr_metrics.pages_variable = pages_variable ;
  pdf_rr_metrics.pages_fixed = pages_fixed ;
  pdf_rr_metrics.pages_combined = pages_combined ;
  pdf_rr_metrics.pages_total = rr_state->page_count ;
#endif
}

/**
 * Wait for all HVD pages started asynchronously to finish rendering.
 *
 * A number of different bugs have meant we get stuck forever in this
 * loop. It would be nice to have some sort of fail-safe exit, i.e.
 * a test to see if there is any hope of pages_remaining going down
 * because of active render tasks etc. At the moment we do at least raise
 * a timeout error with the interrupt test, but as this defaults at 6000hrs
 * it will not be seen very often.
 * \todo BMJ 05-Dec-12 : panic exit if no rendering activitity at all
 */
static Bool pdf_rr_wait_for_pages( PDFXCONTEXT *pdfxc , RR_STATE *rr_state )
{
  Bool result = TRUE ;

  multi_mutex_lock( & rr_state->mutex ) ;
  while ( ! pdfxc->error && rr_state->pages_remaining > 0 ) {
    HqU32x2 when ;

    /* Wait for 10 seconds each time around. */
    HqU32x2FromUint32( & when , 10000000 ) ;
    get_time_from_now( & when ) ;
    ( void )multi_condvar_timedwait( & rr_state->condvar , when ) ;
    if ( ! interrupts_clear( allow_interrupt )) {
      result = FAILURE( FALSE ) ;
      ( void )report_interrupt( allow_interrupt ) ;
      break ;
    }
  }
  multi_mutex_unlock( & rr_state->mutex ) ;
  return result ;
}
/** Reset the hit counts and pages tree nodes of a scan node. */

static Bool pdf_rr_scan_tree_reset( RBT_ROOT *root , RBT_NODE *node ,
                                    void *walk_data )
{
  RR_SCAN_NODE *sn = rbt_get_node_data( node ) ;

  UNUSED_PARAM( RBT_ROOT * , root ) ;
  UNUSED_PARAM( void * , walk_data ) ;

  sn->hits = 0 ;
  sn->pages_hash = 0 ;
  sn->ptn = NULL ;

  return TRUE ;
}

/** Called from \c pdf_walk_tree before a job starts if retained
    raster is enabled. Here's where we prepare the extra state and
    start the work of processing the job looking for repeated
    marks. */

Bool pdf_rr_walk_pages( PDFCONTEXT *pdfc , OBJECT *pages ,
                        PDF_SEPARATIONS_CONTROL *pdfsc )
{
  Bool result = FALSE ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  uint32 i ;
  RR_STATE *rr_state ;
  sw_tl_ref prescanning_tl = SW_TL_REF_INVALID ;
  sw_tl_ref scanning_tl = SW_TL_REF_INVALID ;
  sw_tl_ref postscanning_tl = SW_TL_REF_INVALID ;
  int32 gid ;
  Bool end_early = FALSE ;
  Bool pres_WarnSkippedPages ;
  SWMSG_RR_PAGE_DEFINE pd ;
  RR_PAGE_DEFINE_PAGE *pdp = NULL ;
  sw_event_result er ;
  uint32 page_offset_delta ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  if ( ixc->rr_state == NULL || ixc->encapsulated ) {
    return TRUE ;
  }

  pres_WarnSkippedPages = ixc->WarnSkippedPages ;
  ixc->WarnSkippedPages = FALSE ;

  if ( ! gs_gpush( GST_GSAVE )) {
    return FALSE ;
  }

  gid = gstackptr->gId ;

#define return DO_NOT_return_GOTO_CLEANUP!
  rr_state = ixc->rr_state ;

  pdfxc->pageId = 0 ;
  /* Note how many pages were in the previous chunk: obviously this
     will be zero if we're starting the first pdfexecid() call. */
  page_offset_delta = rr_state->page_count ;
  rr_state->mode = RR_MODE_SKIPPING ;
  if ( ! pdf_walk_pages( pdfc , pages , pdfsc )) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  rr_state->page_count = pdfxc->pageId ;
  rr_state->unique_page_count = 0 ;

  RR_DBG_OPEN( ANY , rr_state , "debug" , MAXUINT32 , MAXUINT32 ) ;
  RR_DBG( ANY , ( rr_state ,
                  ( uint8 * )"%%%% This is a backstop debug log - really, if "
                  "everything goes well it will wind up empty.\n" )) ;

  if ( rr_state->page_count <= 1 || 100 / rr_state->page_count >
       ( uint32 )ixc->OptimizedPDFScanLimitPercent ) {
    monitorf( UVS( "Not performing retained raster scan: too few pages\n" )) ;
    rr_state->mode = RR_MODE_NORMAL ;
    ixc->page_discard = FALSE ;
    end_early = TRUE ;
    result = TRUE ;
    goto CLEANUP ;
  }
  else if ( rr_state->page_count > 0 ) {
    monitorf(UVM( "Found %d pages\n" ) , rr_state->page_count ) ;
  }

  /** Reinitialise the base timeline here for each chunk of execution
      (pdfexecid call). */
  rr_state->tl_ref = pdfxc->corecontext->page->timeline ;

  CHECK_TL_VALID( prescanning_tl =
                  timeline_push( & rr_state->tl_ref , SWTLT_PRESCANNING_PAGES ,
                                 ( sw_tl_extent )rr_state->page_count ,
                                 SW_TL_UNIT_PAGES , rr_state , NULL , 0 )) ;

  pdfxc->pageId = 0 ;
  rr_state->mode = RR_MODE_PRE_SCANNING ;
  if ( ! pdf_walk_pages( pdfc , pages , pdfsc )) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  rr_state->mode = RR_MODE_NORMAL ;

  ( void )timeline_pop( & rr_state->tl_ref , SWTLT_PRESCANNING_PAGES , TRUE ) ;
  prescanning_tl = SW_TL_REF_INVALID ;

  if ( rr_state->page_count > 0 ) {
    /* We need to wait for any pages from previous chunks of execution
       (pdfexecid calls) before continuing. */

    if ( ! pdf_rr_wait_for_pages( pdfxc , rr_state )) {
      goto CLEANUP ;
    }

    multi_mutex_lock( & rr_state->mutex ) ;
    rr_state->pages_remaining = rr_state->page_count ;
    multi_mutex_unlock( & rr_state->mutex ) ;
    rr_state->page_offset += page_offset_delta ;

    CHECK_TL_VALID( scanning_tl =
                    timeline_push( & rr_state->tl_ref , SWTLT_SCANNING_PAGES ,
                                   ( sw_tl_extent )rr_state->page_count ,
                                   SW_TL_UNIT_PAGES , rr_state , NULL , 0 )) ;

    pdfxc->pageId = 0 ;

    rr_state->pages = mm_alloc( pdfxc->mm_structure_pool ,
                                sizeof( RR_PAGE ) * rr_state->page_count ,
                                MM_ALLOC_CLASS_PDF_RR_STATE ) ;

    if ( rr_state->pages == NULL ) {
      ( void )error_handler( VMERROR ) ;
      goto CLEANUP ;
    }
    else {
      Bool ok ;

      for ( i = 0 ; i < rr_state->page_count ; i++ ) {
        rr_state->pages[ i ].mark_count = 0 ;
        rr_state->pages[ i ].links = NULL ;
        rr_state->pages[ i ].marks = NULL ;
        rr_state->pages[ i ].head.next = NULL ;
        rr_state->pages[ i ].head.prev = NULL ;
        rr_state->pages[ i ].head.sub_last = NULL ;
        rr_state->pages[ i ].head.sub_first = NULL ;
        rr_state->pages[ i ].head.next = & rr_state->pages[ i ].tail ;
        rr_state->pages[ i ].head.sn = NULL ;
        rr_state->pages[ i ].head.mark_num = MAXUINT32 ;
        rr_state->pages[ i ].tail.next = NULL ;
        rr_state->pages[ i ].tail.prev = & rr_state->pages[ i ].head ;
        rr_state->pages[ i ].tail.sub_last = NULL ;
        rr_state->pages[ i ].tail.sub_first = NULL ;
        rr_state->pages[ i ].tail.sn = NULL ;
        rr_state->pages[ i ].tail.mark_num = MAXUINT32 ;
        rr_state->pages[ i ].elements = NULL ;
        rr_state->pages[ i ].element_count = 0 ;
        rr_state->pages[ i ].state = RR_PAGE_WAITING ;
      }

      if ( rr_state->pagestree != NULL ) {
        rbt_dispose( rr_state->pagestree ) ;
        if ( ! rbt_walk( rr_state->scantree , pdf_rr_scan_tree_reset , NULL )) {
          FAILURE_GOTO( CLEANUP ) ;
        }
      }

      rr_state->pagestree = rbt_init( rr_state->mark_pool ,
                                      pdf_rr_alloc , pdf_rr_free ,
                                      rbt_compare_integer_keys , 0 , 0 ) ;

      if ( rr_state->pagestree == NULL ) {
        FAILURE_GOTO( CLEANUP ) ;
      }
      else {
        RR_PAGES_NODE *nil_pn ;
        RBT_NODE *node ;

        nil_pn = mm_alloc( pdfxc->mm_structure_pool , sizeof( *nil_pn ) ,
                           MM_ALLOC_CLASS_PDF_RR_PAGES_NODE ) ;

        if ( nil_pn == NULL ) {
          FAILURE_GOTO( CLEANUP ) ;
        }

        nil_pn->count = 0 ;
        nil_pn->index[ 0 ] = 0 ;

        node = rbt_allocate_node( rr_state->pagestree , 0 , nil_pn ) ;

        if ( node == NULL ) {
          ( void )error_handler( VMERROR ) ;
          FAILURE_GOTO( CLEANUP ) ;
        }

        rbt_insert( rr_state->pagestree , node ) ;
      }

      ixc->pageno = 0 ;
      rr_state->mode = RR_MODE_SCANNING ;
      SET_DEVICE( DEVICE_SUPPRESS ) ;

      routedev_setAddObjectToDLCallback( pdf_rr_dl_object_callback , pdfxc ) ;

      ok = pdf_walk_pages( pdfc , pages , pdfsc ) ;

      ( void )timeline_pop( & rr_state->tl_ref , SWTLT_SCANNING_PAGES , TRUE ) ;
      scanning_tl = SW_TL_REF_INVALID ;

      if ( rr_state->mode == RR_MODE_SKIPPING ) {
        /* We must have ended the scan early. Kick over our traces. */
        pdfc->corecontext->page->pageno = 1 ;
        pdf_rr_reset_pages_remaining( rr_state ) ;
        rr_state->mode = RR_MODE_NORMAL ;
        ixc->page_discard = FALSE ;
        end_early = TRUE ;

        if ( ok && pdfc->corecontext->page->irr.generating ) {
          ok = run_ps_string((uint8*)"<< /HVDInternal false >> "
                             "setpagedevice") ;
        }

        result = ok ;
        goto CLEANUP ;
      }

      CHECK_TL_VALID( postscanning_tl =
                      timeline_push( & rr_state->tl_ref ,
                                     SWTLT_POSTSCANNING_PAGES ,
                                     ( sw_tl_extent )4 * rr_state->page_count ,
                                     SW_TL_UNIT_PAGES , rr_state , NULL , 0 )) ;

      rr_state->mode = RR_MODE_POST_SCANNING ;
      pdfxc->pageId = 0 ;

      if ( ! ok ) {
        FAILURE_GOTO( CLEANUP ) ;
      }
    }

#if defined( DEBUG_BUILD )
    if (( debug_retained_raster & DEBUG_RR_MARKS_TREE ) != 0 ) {
      RR_DBG_OPEN( MARKS_TREE , rr_state , "marksstree" ,
                   MAXUINT32 , MAXUINT32 ) ;
      if ( ! rbt_walk( rr_state->markstree , pdf_rr_marks_tree_dump ,
                       rr_state )) {
        FAILURE_GOTO( CLEANUP ) ;
      }
      RR_DBG_CLOSE( MARKS_TREE , rr_state ) ;
    }
#endif

    if ( ixc->OptimizedPDFScanWindow > 0 ) {
      uint32 start =
        rr_state->page_count > ( uint32 )ixc->OptimizedPDFScanWindow ?
        rr_state->page_count - ixc->OptimizedPDFScanWindow : 0 ;

      if ( rr_state->page_count < ( uint32 )ixc->OptimizedPDFScanWindow &&
           ! pdf_rr_find_supernodes( pdfc , start ,
                                     rr_state->page_count - 1 )) {
        FAILURE_GOTO( CLEANUP ) ;
      }

      for ( i = start ; i < rr_state->page_count ; i++ ) {
        if ( ! pdf_rr_close_window( pdfc , i )) {
          FAILURE_GOTO( CLEANUP ) ;
        }
      }
    }
    else {
      if ( ! pdf_rr_find_supernodes( pdfc , 0 , rr_state->page_count - 1 )) {
        FAILURE_GOTO( CLEANUP ) ;
      }

      for ( i = 0 ; i < rr_state->page_count ; i++ ) {
        if ( ! pdf_rr_close_window( pdfc , i )) {
          FAILURE_GOTO( CLEANUP ) ;
        }
      }
    }

    RR_ASRT_ALL_PAGES( rr_state ) ;
    RR_ASRT_LIVE_NODES( rr_state ) ;

#if defined( DEBUG_BUILD )
    if (( debug_retained_raster & DEBUG_RR_PAGES_TREE ) != 0 ) {
      RR_DBG_OPEN( PAGES_TREE , rr_state , "pagestree" ,
                   MAXUINT32 , MAXUINT32 ) ;
      if ( ! rbt_walk( rr_state->pagestree , pdf_rr_pages_tree_dump ,
                       rr_state )) {
        FAILURE_GOTO( CLEANUP ) ;
      }
      RR_DBG_CLOSE( PAGES_TREE , rr_state ) ;
    }
#endif

    pdf_rr_page_totals( pdfc ) ;

    pd.connection = rr_state->cache ;
    pd.timeline = rr_state->tl_ref ;
    pd.page_count = rr_state->page_count ;

    pdp = mm_alloc( pdfxc->mm_structure_pool ,
                    ( sizeof( *pdp ) * rr_state->page_count ) ,
                    MM_ALLOC_CLASS_PDF_RR_EVENT ) ;
    if ( pdp == NULL ) {
      ( void )error_handler( VMERROR ) ;
      FAILURE_GOTO( CLEANUP ) ;
    }

    pd.pages = pdp ;

    for ( i = 0 ; i < rr_state->page_count ; i++ ) {
      pdp[ i ].elements = NULL ;
    }

    for ( i = 0 ; i < rr_state->page_count ; i++ ) {
      uint32 j ;

      pdp[ i ].element_count = rr_state->pages[ i ].element_count ;
      pdp[ i ].elements = mm_alloc( pdfxc->mm_structure_pool ,
                                    ( sizeof( *pdp[ i ].elements ) *
                                      pdp[ i ].element_count ) ,
                                    MM_ALLOC_CLASS_PDF_RR_EVENT ) ;

      if ( pdp[ i ].elements == NULL ) {
        ( void )error_handler( VMERROR ) ;
        FAILURE_GOTO( CLEANUP ) ;
      }

      for ( j = 0 ; j < rr_state->pages[ i ].element_count ; j++ ) {
        pdp[ i ].elements[ j ].id = rr_state->pages[ i ].elements[ j ].id ;
        pdp[ i ].elements[ j ].x = 0 ;
        pdp[ i ].elements[ j ].y = 0 ;
      }
    }

    er = SwEvent( SWEVT_RR_PAGE_DEFINE , & pd , sizeof( pd )) ;

    if ( er >= SW_EVENT_ERROR || er == SW_EVENT_UNHANDLED ) {
      FAILURE_GOTO( CLEANUP ) ;
    }
  }

  result = TRUE ;

 CLEANUP:
  rr_state->mode = RR_MODE_NORMAL ;

  if ( pdp != NULL ) {
    for ( i = 0 ; i < rr_state->page_count ; i++ ) {
      if ( pdp[ i ].elements != NULL ) {
        mm_free( pdfxc->mm_structure_pool , pdp[ i ].elements ,
                 ( sizeof( *pdp[ i ].elements ) * pdp[ i ].element_count )) ;
      }
    }

    mm_free( pdfxc->mm_structure_pool , pdp ,
             ( sizeof( *pdp ) * rr_state->page_count )) ;
  }

  if ( prescanning_tl != SW_TL_REF_INVALID ) {
    ( void )timeline_pop( & rr_state->tl_ref , SWTLT_PRESCANNING_PAGES ,
                          result ) ;
  }

  if ( scanning_tl != SW_TL_REF_INVALID ) {
    ( void )timeline_pop( & rr_state->tl_ref , SWTLT_SCANNING_PAGES , result ) ;
  }

  if ( postscanning_tl != SW_TL_REF_INVALID ) {
    ( void )timeline_pop( & rr_state->tl_ref , SWTLT_POSTSCANNING_PAGES ,
                          result ) ;
  }

  if ( result && ! gs_cleargstates( gid , GST_GSAVE , NULL )) {
    result = FALSE ;
  }

  if ( end_early ) {
    if ( ! pdf_rr_end( pdfxc )) {
      result = FALSE ;
    }
  }

  ixc->WarnSkippedPages = pres_WarnSkippedPages ;

  /* Remove any xref cache entries so that when we walk the page range
     again as part of the output phase we don't get confused about
     page indices and cache lifetimes. */
  pdf_xrefreset( pdfc ) ;

#undef return
  return result ;
}

/** Called from \c pdf_in_begin_execution_context. A quick no-op if
    retained raster is turned off or we're being called recursively -
    e.g. from OPI. */

Bool pdf_rr_begin( PDFXCONTEXT *pdfxc )
{
  corecontext_t *corecontext = pdfxc->corecontext;
  SYSTEMPARAMS *systemparams = corecontext->systemparams;
  Bool result = FALSE ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  SWMSG_RR_CONNECT connect ;
  sw_event_result er ;
  int32 len ;

  PDF_GET_IXC( ixc ) ;

  if ( ixc->EnableOptimizedPDFScan && ! ixc->encapsulated &&
       ( ixc->OptimizedPDFExternal
          ? systemparams->HVDExternal
          : systemparams->HVDInternal )) {
    if ( rcbn_enabled()) {
      /* Recombine and HVD are mutually exclusive. */
      monitorf( UVS( "%%%%[ Warning: Recombine enabled - "
                     "disabling Harlequin VariData ]%%%%\n" )) ;
      return TRUE ;
    }

    if ( isTrappingActive( corecontext->page )) {
      /* TrapPro and HVD are mutually exclusive. */
      monitorf( UVS( "%%%%[ Warning: TrapPro enabled - "
                     "disabling Harlequin VariData ]%%%%\n" )) ;
      return TRUE ;
    }

    if ( systemparams->HVDInternal &&
         DOING_RUNLENGTH(corecontext->page) ) {
      /* Internal HVD and RLE output are mutually exclusive.
         The IRR pgb device does not support RLE currently. */
      monitorf( UVS( "%%%%[ Warning: RLE output enabled - "
                     "disabling Harlequin VariData ]%%%%\n" )) ;
      return TRUE ;
    }

    /* We must force vignette detection off because the scanner can't
       cope with deferred addition of objects to the display list. For
       RR to work, there must be a 1:1 correspondence between marking
       operators and their marks. */
    corecontext->userparams->VignetteDetect = VDL_None ;
  }
  else {
    return TRUE ;
  }

#if defined( DEBUG_BUILD )
  if ( NUM_THREADS() > 1 &&
       (( debug_retained_raster & DEBUG_RR_EVENTS ) != 0 )) {
    monitorf((uint8*)"%%%%[ Warning: Can't log events on non-interpreter "
             "threads: disabling EVENTS logging ]%%%%\n" ) ;
    debug_retained_raster &= ~DEBUG_RR_EVENTS ;
  }

  ( void )run_ps_string(( uint8 * )"(%os%/RR_logs/*) { deletefile } "
                        "1024 string filenameforall" ) ;
#endif

  rr_state = mm_alloc( pdfxc->mm_structure_pool , sizeof( *rr_state ) ,
                       MM_ALLOC_CLASS_PDF_RR_STATE ) ;

  if ( rr_state == NULL ) {
    ( void )error_handler( VMERROR ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }
  HqMemZero((uint8 *)rr_state, sizeof(*rr_state));
  ixc->rr_state = rr_state ;

  rr_state->mode = RR_MODE_NORMAL ;
  rr_state->mark_pool = NULL ;
  rr_state->nil_sentinel = NULL ;
  rr_state->pages = NULL ;
  rr_state->page_offset = 0 ;
  rr_state->page_count = 0 ;
  rr_state->pages_remaining = 0 ;
  rr_state->unique_page_count = 0 ;
  pdf_rr_hash_reset( ixc , & rr_state->pdl_state ) ;
  pdf_rr_hash_reset( ixc , & rr_state->text_state ) ;
  rr_state->op_context = NULL ;
  rr_state->cache_size = 0 ;
  rr_state->scantree = NULL ;
  rr_state->restree = NULL ;
  rr_state->pagestree = NULL ;
  rr_state->markstree = NULL ;
  rr_state->marks = NULL ;
  rr_state->links = NULL ;
  rr_state->bucket_size = 0 ;
  rr_state->nodes_count = 0 ;
  rr_state->mark_count = 0 ;
  rr_state->current_gstate = NULL ;
  rr_state->cache = NULL ;
  rr_state->gid = GS_INVALID_GID ;
  rr_state->start.handler = pdf_rr_timeline_start ;
  rr_state->start.context = rr_state ;
  rr_state->start.reserved = 0 ;
  rr_state->aborted.handler = pdf_rr_timeline_aborted ;
  rr_state->aborted.context = rr_state ;
  rr_state->aborted.reserved = 0 ;
  rr_state->ended.handler = pdf_rr_timeline_ended ;
  rr_state->ended.context = rr_state ;
  rr_state->ended.reserved = 0 ;
  rr_state->page_complete.handler = pdf_rr_page_complete ;
  rr_state->page_complete.context = rr_state ;
  rr_state->page_complete.reserved = 0 ;
#if defined( DEBUG_BUILD )
  rr_state->page_define.handler = pdf_rr_page_define ;
  rr_state->page_define.context = rr_state ;
  rr_state->page_define.reserved = 0 ;
  rr_state->page_ready.handler = pdf_rr_page_ready ;
  rr_state->page_ready.context = rr_state ;
  rr_state->page_ready.reserved = 0 ;
  rr_state->element_define.handler = pdf_rr_element_define ;
  rr_state->element_define.context = rr_state ;
  rr_state->element_define.reserved = 0 ;
  rr_state->element_lock.handler = pdf_rr_element_lock ;
  rr_state->element_lock.context = rr_state ;
  rr_state->element_lock.reserved = 0 ;
  rr_state->element_unlock.handler = pdf_rr_element_unlock ;
  rr_state->element_unlock.context = rr_state ;
  rr_state->element_unlock.reserved = 0 ;
  rr_state->element_pending.handler = pdf_rr_element_pending ;
  rr_state->element_pending.context = rr_state ;
  rr_state->element_pending.reserved = 0 ;
  rr_state->element_query.handler = pdf_rr_element_query ;
  rr_state->element_query.context = rr_state ;
  rr_state->element_query.reserved = 0 ;
  rr_state->element_update_raster.handler = pdf_rr_element_update_raster ;
  rr_state->element_update_raster.context = rr_state ;
  rr_state->element_update_raster.reserved = 0 ;
  rr_state->element_update_hits.handler = pdf_rr_element_update_hits ;
  rr_state->element_update_hits.context = rr_state ;
  rr_state->element_update_hits.reserved = 0 ;
#endif
  rr_state->abandon_scan = FALSE ;

  if ( mm_pool_create( & rr_state->mark_pool ,
                       PDF_POOL_TYPE , PDF_POOL_PARAMS ) != MM_SUCCESS ) {
    ( void )error_handler( VMERROR ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }

  /* Initialise timeline state from job. We can push and pop RR states
     on top of this as desired. */
  rr_state->tl_ref = corecontext->page->timeline ;
  CHECK_TL_VALID( rr_state->job =
                  SwTimelineOfType( rr_state->tl_ref , SWTLT_JOB )) ;

  if ( SwRegisterHandler( EVENT_TIMELINE_START , & rr_state->start ,
                          SW_EVENT_DEFAULT ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( EVENT_TIMELINE_ABORTED , & rr_state->aborted ,
                          SW_EVENT_DEFAULT ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( EVENT_TIMELINE_ENDED , & rr_state->ended ,
                          SW_EVENT_DEFAULT ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_PAGE_COMPLETE , & rr_state->page_complete ,
                          SW_EVENT_DEFAULT ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }

#if defined( DEBUG_BUILD )
  /* These handlers are just for debugging - high priority passthrough. */

  if ( SwRegisterHandler( SWEVT_RR_PAGE_DEFINE , & rr_state->page_define ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_PAGE_READY , & rr_state->page_ready ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_ELEMENT_DEFINE ,
                          & rr_state->element_define ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_ELEMENT_LOCK , & rr_state->element_lock ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_ELEMENT_UNLOCK ,
                          & rr_state->element_unlock ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_ELEMENT_PENDING ,
                          & rr_state->element_pending ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_ELEMENT_QUERY , & rr_state->element_query ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_ELEMENT_UPDATE_RASTER ,
                          & rr_state->element_update_raster ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
  if ( SwRegisterHandler( SWEVT_RR_ELEMENT_UPDATE_HITS ,
                          & rr_state->element_update_hits ,
                          SW_EVENT_OVERRIDE ) != SW_RDR_SUCCESS ) {
    FAILURE_GOTO( CLEANUP ) ;
  }
#endif

  multi_mutex_init( & rr_state->mutex , RETAINEDRASTER_LOCK_INDEX , FALSE ,
                    SW_TRACE_RETAINEDRASTER_ACQUIRE ,
                    SW_TRACE_RETAINEDRASTER_HOLD ) ;
  multi_condvar_init( & rr_state->condvar , & rr_state->mutex ,
                      SW_TRACE_RETAINEDRASTER_WAIT ) ;

  rr_state->scantree = rbt_init( rr_state->mark_pool ,
                                 pdf_rr_alloc , pdf_rr_free ,
                                 pdf_rr_compare , RR_HASH_LENGTH ,
                                 sizeof( RR_SCAN_NODE )) ;

  if ( rr_state->scantree == NULL ) {
    FAILURE_GOTO( CLEANUP ) ;
  }

  rr_state->restree = rbt_init( rr_state->mark_pool ,
                                pdf_rr_alloc , pdf_rr_free ,
                                pdf_rr_res_compare , sizeof( OBJECT ) ,
                                sizeof( RR_RESOURCE_NODE )) ;

  if ( rr_state->restree == NULL ) {
    FAILURE_GOTO( CLEANUP ) ;
  }

  rr_state->markstree = rbt_init( rr_state->mark_pool ,
                                  pdf_rr_alloc , pdf_rr_free ,
                                  rbt_compare_integer_keys , 0 , 0 ) ;

  if ( rr_state->markstree == NULL ) {
    FAILURE_GOTO( CLEANUP ) ;
  }

  rr_state->elemtree = rbt_init( rr_state->mark_pool ,
                                 pdf_rr_alloc , pdf_rr_free ,
                                 pdf_rr_compare , RR_HASH_LENGTH ,
                                 sizeof( RR_ELEM_NODE )) ;

  if ( rr_state->elemtree == NULL ) {
    FAILURE_GOTO( CLEANUP ) ;
  }

  rr_state->links = mm_alloc( pdfxc->mm_structure_pool ,
                              ( sizeof( *rr_state->links ) *
                                RR_DEFAULT_BUCKET_SIZE ) ,
                              MM_ALLOC_CLASS_PDF_RR_LINKS ) ;

  if ( rr_state->links == NULL ) {
    ( void )error_handler( VMERROR ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }

  rr_state->marks = mm_alloc( pdfxc->mm_structure_pool ,
                              ( sizeof( *rr_state->marks ) *
                                RR_DEFAULT_BUCKET_SIZE ) ,
                              MM_ALLOC_CLASS_PDF_RR_MARKS ) ;

  if ( rr_state->marks == NULL ) {
    ( void )error_handler( VMERROR ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }

  rr_state->bucket_size = RR_DEFAULT_BUCKET_SIZE ;
  rr_state->nodes_count = 0 ;

  rr_state->mark_count = 0 ;
  rr_state->nested_Do = 0 ;
  rr_state->last_implicit_gsave = 0 ;
  rr_state->deferred_pdl_hash = FALSE ;
  HqMemZero( rr_state->deferred_pdl_hash_contrib ,
             sizeof( rr_state->deferred_pdl_hash_contrib )) ;
  rr_state->pdf_state = OPSTATE_PDL ;
  rr_state->current_atomic_number = 0 ;
  rr_state->current_atom_start = MAXUINT32 ;
  rr_state->current_atom_end = 0 ;
  rr_state->current_compositing = FALSE ;
  rr_state->current_pattern = FALSE ;
  rr_state->current_patternstate = NULL ;
  rr_state->in_inline_image = FALSE ;
#if defined( DEBUG_BUILD )
  rr_state->dbg_file_stack.size = EMPTY_STACK ;
  rr_state->dbg_file_stack.fptr = & rr_state->dbg_file_stackframe ;
  rr_state->dbg_file_stack.limit = FRAMESIZE ;
  rr_state->dbg_file_stack.type = STACK_TYPE_OPERAND ;
  rr_state->dbg_file_stackframe.link = NULL ;
  if ( mps_root_create( & rr_state->dbg_file_root , mm_arena ,
                        mps_rank_exact() , 0 , pdf_rr_root_scan ,
                        & rr_state->dbg_file_stack , 0 ) != MPS_RES_OK ) {
    mps_root_destroy( rr_state->dbg_file_root ) ;
    FAILURE( FALSE ) ;
    goto CLEANUP ;
  }
  rr_state->dbg_gstate_depth = 0 ;
  rr_state->dbg_indented = FALSE ;
  rr_state->dbg_showpage_count = 0 ;
  rr_state->dbg_page_num = 0 ;
  rr_state->dbg_element_num = 0 ;
#endif

  {
    RR_SCAN_NODE nil_data ;
    RBT_NODE *node ;
    uint8 hash[ RR_HASH_LENGTH ] ;

    /* Store a nil sentinel mark node with the initial PDL hash as its
       key: that way we don't need to check for NULL sn pointers
       everywhere. */

    nil_data.hash = NULL ;
    nil_data.hits = 0 ;
    bbox_clear( & nil_data.bbox ) ;
    nil_data.u.orig = NULL ;
    nil_data.last_page = MAXUINT32 ;
    nil_data.pages_hash = 0 ;
    nil_data.ptn = NULL ;
    nil_data.is_form = FALSE ;
    nil_data.has_matrix = FALSE ;
    nil_data.has_ref = FALSE ;
    nil_data.composited = FALSE ;
    nil_data.pattern = FALSE ;
    nil_data.significant = FALSE ;

    pdf_rr_hash_finalise( & rr_state->pdl_state , hash ) ;

    node = pdf_rr_scantree_createnode( pdfxc , rr_state->scantree ,
                                       hash , & nil_data ) ;

    if ( node == NULL ) {
      FAILURE_GOTO( CLEANUP ) ;
    }

    rr_state->nil_sentinel = rbt_get_node_data( node ) ;
    HQASSERT( rr_state->nil_sentinel != NULL ,
              "Nil sentinel mark node not created" ) ;
  }

  /* Find the procedures we'll need to call often: done once so we
     don't have to allocate PSVM for procedures etc. all the time. */

  if ( ! run_ps_string(( uint8 * )
                       "/HqnPageCounter /ProcSet resourcestatus {"
                       "  pop pop /HqnPageCounter /ProcSet findresource"
                       "  /SetExplicitPageNumber get"
                       "} { /pop load } ifelse" )) {
    FAILURE_GOTO( CLEANUP ) ;
  }

  Copy( & rr_state->SetExplicitPageNumber , theTop( operandstack )) ;

  pop( & operandstack ) ;

  connect.timeline = rr_state->tl_ref ;
  if ( oString( *ixc->OptimizedPDFCacheID ) != NULL &&
       theLen( *ixc->OptimizedPDFCacheID ) > 0 ) {
    len = swncopyf( connect.cache_id , RR_CACHE_ID_LENGTH - 1 ,
                    ( uint8 * )"%.*s" ,
                    theLen( *ixc->OptimizedPDFCacheID ) ,
                    oString( *ixc->OptimizedPDFCacheID )) ;
  }
  else {
    len = swncopyf( connect.cache_id , RR_CACHE_ID_LENGTH - 1 ,
                    ( uint8 * )"%s" , "GGIRR" ) ;
  }
  connect.cache_id[ len ] = '\0' ;

  /* Check OptimizedPDFExternal and cache id are compatible. */
  if ( ixc->OptimizedPDFExternal !=
       ( HqMemCmp( connect.cache_id , len ,
                   NAME_AND_LENGTH( "GGIRR" )) != 0 )) {
    ( void )detail_error_handler( CONFIGURATIONERROR ,
                                  "OptimizedPDFExternal param "
                                  "is incompatible with cache id" ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }

  if ( oString( *ixc->OptimizedPDFSetupID ) != NULL &&
       theLen( *ixc->OptimizedPDFSetupID ) > 0 ) {
    len = swncopyf( connect.setup_id , RR_SETUP_ID_LENGTH - 1 ,
                    ( uint8 * )"%.*s" ,
                    theLen( *ixc->OptimizedPDFSetupID ) ,
                    oString( *ixc->OptimizedPDFSetupID )) ;
  }
  else {
    len = swncopyf( connect.setup_id , RR_SETUP_ID_LENGTH - 1 ,
                    ( uint8 * )"%s" , "" ) ;
  }
  connect.setup_id[ len ] = '\0' ;
  connect.connection = NULL ;

  er = SwEvent( SWEVT_RR_CONNECT , & connect , sizeof( connect )) ;

  if ( er >= SW_EVENT_ERROR ) {
    ( void )error_handler( CONFIGURATIONERROR ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }
  else if ( er == SW_EVENT_UNHANDLED ) {
    ( void )detail_error_handler( UNREGISTERED ,
                                  "No retained raster cache connection" ) ;
    FAILURE_GOTO( CLEANUP ) ;
  }

  rr_state->cache = connect.connection ;

  result = TRUE ;

 CLEANUP:

  if ( ! result ) {
    ( void )pdf_rr_end( pdfxc ) ;
  }

  return result ;
}

/** Called when a context during which retained raster processing
    could have happened is ending. It should free any structures
    allocated. Safe if partially constructed. */

Bool pdf_rr_end( PDFXCONTEXT *pdfxc )
{
  Bool result = TRUE ;
  PDF_IXC_PARAMS *ixc ;

  PDF_GET_IXC( ixc ) ;

  if ( ixc->rr_state != NULL ) {
    RR_STATE *rr_state = ixc->rr_state ;

    result = pdf_rr_wait_for_pages( pdfxc , rr_state ) ;

    if ( rr_state->cache != NULL ) {
      SWMSG_RR_DISCONNECT disconnect ;

      disconnect.connection = rr_state->cache ;
      disconnect.timeline = rr_state->tl_ref ;

      if ( SwEvent( SWEVT_RR_DISCONNECT , & disconnect ,
                    sizeof( disconnect )) >= SW_EVENT_ERROR ) {
        /* Need to continue freeing things so don't return early */
        result = FAILURE( FALSE ) ;
      }
      /* SW_EVENT_UNHANDLED is not an error here: if the disconnect
         had no effect, there's nothing we can do about it here
         anyway. */
    }

    ( void )SwDeregisterHandler( EVENT_TIMELINE_START ,
                                 & rr_state->start ) ;
    ( void )SwDeregisterHandler( EVENT_TIMELINE_ABORTED ,
                                 & rr_state->aborted ) ;
    ( void )SwDeregisterHandler( EVENT_TIMELINE_ENDED ,
                                 & rr_state->ended ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_PAGE_COMPLETE ,
                                 & rr_state->page_complete ) ;

#if defined( DEBUG_BUILD )
    ( void )SwDeregisterHandler( SWEVT_RR_PAGE_DEFINE ,
                                 & rr_state->page_define ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_PAGE_READY ,
                                 & rr_state->page_ready ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_ELEMENT_DEFINE ,
                                 & rr_state->element_define ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_ELEMENT_LOCK ,
                                 & rr_state->element_lock ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_ELEMENT_UNLOCK ,
                                 & rr_state->element_unlock ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_ELEMENT_PENDING ,
                                 & rr_state->element_pending ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_ELEMENT_QUERY ,
                                 & rr_state->element_query ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_ELEMENT_UPDATE_RASTER ,
                                 & rr_state->element_update_raster ) ;
    ( void )SwDeregisterHandler( SWEVT_RR_ELEMENT_UPDATE_HITS ,
                                 & rr_state->element_update_hits ) ;
#endif

    multi_condvar_finish( & rr_state->condvar ) ;
    multi_mutex_finish( & rr_state->mutex ) ;

    if ( rr_state->pages != NULL ) {
      uint32 i ;

      HQASSERT( rr_state->page_count > 0 , "No pages to free" ) ;

      for ( i = 0 ; i < rr_state->page_count ; i++ ) {
        RR_PAGE *page = & rr_state->pages[ i ] ;

        if ( page->links != NULL) {
          mm_free( pdfxc->mm_structure_pool ,
                   ( mm_addr_t )( page->links ) ,
                   sizeof( *page->links ) * page->mark_count ) ;
        }
      }

      mm_free( pdfxc->mm_structure_pool ,
               ( mm_addr_t )( rr_state->pages ) ,
               sizeof( *rr_state->pages ) * rr_state->page_count ) ;
    }

    if ( rr_state->mark_pool != NULL ) {
      mm_pool_destroy( rr_state->mark_pool ) ;
    }

    if ( rr_state->links != NULL ) {
      mm_free( pdfxc->mm_structure_pool , rr_state->links ,
               ( sizeof( *rr_state->links ) * rr_state->bucket_size )) ;
    }

    if ( rr_state->marks != NULL ) {
      mm_free( pdfxc->mm_structure_pool , rr_state->marks ,
               ( sizeof( *rr_state->marks ) * rr_state->bucket_size )) ;
    }

#if defined( DEBUG_BUILD )
    mps_root_destroy( rr_state->dbg_file_root ) ;
#endif

    mm_free( pdfxc->mm_structure_pool , ( mm_addr_t )rr_state ,
             sizeof( *rr_state )) ;
    ixc->rr_state = NULL ;

    /* Reset any flags we might have manipulated. */

    ixc->page_discard = FALSE ;
    ixc->ignore_setpagedevice = FALSE ;
    ixc->ignore_showpage = FALSE ;
  }

  return result ;
}

/** Hook called during internal retained raster operation when a new cached
    raster element has been captured. It should attach the IRR store in the
    appropriate place and may store extra information such as the size. */

static Bool pdf_rr_cache_element( void *priv_context , uintptr_t priv_id ,
                                  irr_store_t *irr_store )
{
  uint8 *hash = ( uint8 * )priv_id ;
  SWMSG_RR_ELEMENT_UPDATE_RASTER ur ;
  RR_STATE *rr_state = priv_context ;

  ur.connection = rr_state->cache ;
  ur.timeline = rr_state->tl_ref ;
  ur.id = hash ;
  ur.raster = ( uintptr_t )irr_store ;
  if ( irr_store != NULL ) {
    ur.size = irr_store_footprint( irr_store ) ;
  }
  else {
    ur.size = 0 ;
  }

  if ( SwEvent( SWEVT_RR_ELEMENT_UPDATE_RASTER , & ur ,
                sizeof( ur )) >= SW_EVENT_ERROR ) {
    return FAILURE( FALSE ) ;
  }
  /* SW_EVENT_UNHANDLED is not an error here. */

  return TRUE ;
}

/** Little hook that's called each time \c pdf_walk_page_range is
    going to give us a page to handle. This allows us keep in step
    with things which track pages using BeginPage and StartRender,
    neither of which can be guaranteed to be called when HVD is
    switched on. This used to include the transparency checker. In its
    current incarnation it's a no-op but there's no harm in keeping it
    around for future expansion. */

Bool pdf_rr_setup_page( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  if ( ixc->rr_state != NULL && ! ixc->encapsulated ) {
  }

  return TRUE ;
}

/* Hashing filter with file position */

static FILELIST rrHashFilter = {tag_LIMIT};

/* Private data for the rrHash filter */
typedef struct {
  uint32  offset ;        /* File offset of the end of the buffer */
  uint8 * start ;         /* Value of theIPtr() when starting hashing */
  RR_HASH_STATE * state ; /* Non-null if hashing */
} rrHashPriv ;

#define RRHASHBUFFSIZE 8192

/* Initialise the rrHash filter. arg must be the underlying filter, stack must
   be null */
static Bool rrHashFilterInit(FILELIST * filter, OBJECT * arg, STACK * stack)
{
  static rrHashPriv zero = {0} ;
  rrHashPriv * priv ;
  uint8 * buff ;

  UNUSED_PARAM( STACK * , stack ) ;

  HQASSERT(filter, "No filter in rrHashFilterInit") ;
  HQASSERT(arg != NULL && stack == NULL, "Arg but no stack please") ;

  if ( !filter_target_or_source(filter, arg) )
    return FALSE;

  /* Allocate and initialise private data */
  priv = mm_alloc(mm_pool_temp, sizeof(rrHashPriv),
                  MM_ALLOC_CLASS_FILTER_BUFFER) ;
  if (!priv)
    return error_handler(VMERROR) ;
  *priv = zero ;

  /* Allocate the buffer */
  buff = mm_alloc(mm_pool_temp, RRHASHBUFFSIZE + 1,
                  MM_ALLOC_CLASS_FILTER_BUFFER) ;
  if (!buff) {
    mm_free(mm_pool_temp, priv, sizeof(rrHashPriv)) ;
    return error_handler(VMERROR) ;
  }

  /* Initialise the filter */
  theIBuffer(filter)        = buff + 1 ;
  theIPtr(filter)           = buff + 1 ;
  theICount(filter)         = 0 ;
  theIBufferSize(filter)    = RRHASHBUFFSIZE ;
  theIFilterState(filter)   = FILTER_INIT_STATE;
  theIFilterPrivate(filter) = priv ;

  return TRUE ;
}


static void rrHashFlush(FILELIST * filter, rrHashPriv * priv, uint8 * end)
{
  uint32 len = CAST_PTRDIFFT_TO_UINT32(end - priv->start) ;

  HQASSERT(end >= priv->start &&
           end <= theIBuffer(filter) + theIBufferSize(filter),
           "Silly end in rrHashFlush") ;
  HQASSERT(priv->start >= theIBuffer(filter) &&
           priv->start - theIBuffer(filter) <= theIBufferSize(filter),
           "Silly start in rrHashFlush") ;

  if (len)
    pdf_rr_hash(priv->state, priv->start, len) ;

  priv->start = theIBuffer(filter) ;
}


/* Discard the rrHash filter, hashing remaining bytes if necessary. This copes
   with being called more than once, because PDF is weird like that. */
static void rrHashFilterDispose(FILELIST * filter)
{
  rrHashPriv * priv ;
  uint8 * buff ;

  HQASSERT(filter, "No filter in rrHashFilterDispose") ;
  priv = theIFilterPrivate(filter) ;
  buff = theIBuffer(filter) ;

  if (priv) {
    theIFilterPrivate(filter) = NULL ;

    HQASSERT(priv->state == NULL,
             "Didn't hash remaining bytes in rrHashFilterDispose") ;

    mm_free(mm_pool_temp, priv, sizeof(rrHashPriv)) ;
  }

  if (buff) {
    theIBuffer(filter) = NULL ;

    mm_free(mm_pool_temp, buff - 1, RRHASHBUFFSIZE + 1) ;
  }
}


/* Called to start the rrHashFilter hashing bytes */
static void rrHashBegin(FILELIST * filter, RR_HASH_STATE * state)
{
  rrHashPriv * priv ;

  HQASSERT(filter, "No filter in rrHashBegin") ;
  HQASSERT(filter->myinitfile == rrHashFilterInit,
           "Wrong filter in rrHashBegin") ;
  HQASSERT(state, "No state in rrHashBegin") ;

  priv = theIFilterPrivate(filter) ;
  HQASSERT(priv, "No private data in rrHashBegin") ;
  HQASSERT(priv->state == 0, "Already hashing in rrHashBegin") ;

  priv->state = state ;
  priv->start = theIPtr(filter) ;
}


/* Called to stop the rrHashFilter hashing bytes */
static void rrHashEnd(FILELIST * filter)
{
  rrHashPriv * priv ;

  HQASSERT(filter, "No filter in rrHashEnd") ;
  HQASSERT(filter->myinitfile == rrHashFilterInit,
           "Wrong filter in rrHashEnd") ;

  priv = theIFilterPrivate(filter) ;
  HQASSERT(priv, "No private data in rrHashEnd") ;
  HQASSERT(priv->state, "Not hashing in rrHashEnd") ;

  rrHashFlush(filter, priv, theIPtr(filter)) ;

  priv->state = NULL ;
  priv->start = NULL ;
}


/* Fill the rrHash buffer.
   It retrospectively hashes the buffer contents if necessary. */

static Bool rrHashDecodeBuffer(FILELIST * filter, int32 * bytes)
{
  rrHashPriv * priv ;
  FILELIST * under ;
  int32 c, i, count = 0 ;
  uint8 * buff ;

  HQASSERT(filter, "No filter in rrHashDecodeBuffer") ;
  buff = theIBuffer(filter) ;
  HQASSERT(buff, "No buffer in rrHashDecodeBuffer") ;
  priv = theIFilterPrivate(filter) ;
  HQASSERT(priv, "No private data in rrHashDecodeBuffer") ;
  under = theIUnderFile(filter) ;
  HQASSERT(under, "No underlying file in rrHashDecodeBuffer") ;

  /* Retrospectively hash the previous contents */
  if (priv->state)
    rrHashFlush(filter, priv, buff + theIReadSize(filter)) ;

  /* Refill the buffer */
  for (i = 0; i < RRHASHBUFFSIZE; i++) {
    if ( (c = Getc(under)) == EOF) {
      count = -count ;
      break ;
    } else {
      *buff++ = (uint8) c ;
      count++ ;
    }
  }

  /* Update the offset of the end of the buffer */
  if (count < 0)
    priv->offset -= count ;
  else
    priv->offset += count ;

  *bytes = count ;
  return TRUE ;
}


/* Read the effective file position */
static Bool rrHashPos(FILELIST * filter, uint32 * pos)
{
  rrHashPriv * priv ;

  HQASSERT(filter, "No filter in rrHashPos") ;
  HQASSERT(filter->myinitfile == rrHashFilterInit,
           "Wrong filter in rrHashPos") ;
  priv = theIFilterPrivate(filter) ;
  HQASSERT(priv, "No private data in rrHashPos") ;

  *pos = priv->offset - theICount(filter) - 1 ;

  return TRUE ;
}


/* Reset the rrHashFilter - is this necessary? */

int32 rrHashFilterReset( FILELIST *filter )
{
  rrHashPriv * priv ;
  FILELIST * under ;
  int32 result ;

  HQASSERT(filter, "No filter in rrHashFilterReset") ;
  priv = theIFilterPrivate(filter) ;

  under = theIUnderFile(filter) ;
  HQASSERT(under, "No underlying file in rrHashFilterReset") ;

  if (priv && priv->state) {
    HQFAIL("Filter reset while hashing") ;
    rrHashFlush(filter, priv, theIPtr(filter)) ;
  }

  result = FilterReset(filter) ||
           theIMyResetFile(under)(under) ;

  if (priv && result == 0) {
    priv->offset = 0 ;
    theICount( filter ) = 0 ;
    theIPtr( filter ) = theIBuffer( filter ) ;
    ClearIEofFlag( filter ) ;
    if (priv->state)
      priv->start = theIPtr(filter) ;
  }

  return result ;
}


int32 rrHashFilterClose(FILELIST * filter, int32 flag)
{
  rrHashPriv * priv ;

  HQASSERT(filter, "No filter in rrHashFilterClose") ;
  priv = theIFilterPrivate(filter) ;

  if (priv && priv->state) {
    rrHashFlush(filter, priv, theIPtr(filter)) ;
    priv->state = NULL ;
    priv->start = NULL ;
  }

  return FilterCloseFile(filter, flag) ;
}


/* Ensure the rrHashFilter structure has been filled in */
FILELIST * rrHash_decode_filter()
{
  if ( rrHashFilter.typetag == tag_LIMIT ) {
    init_filelist_struct(&rrHashFilter,
                         NAME_AND_LENGTH("rrHashDecode"),
                         FILTER_FLAG | READ_FLAG,
                         0, NULL, 0,
                         FilterFillBuff,                   /* fillbuff */
                         FilterFlushBufError,              /* flushbuff */
                         rrHashFilterInit,                 /* initfile */
                         rrHashFilterClose,                /* closefile */
                         rrHashFilterDispose,              /* disposefile */
                         FilterBytes,                      /* bytesavail */
                         rrHashFilterReset,                /* resetfile */
                         FilterPos,                        /* filepos */
                         FilterSetPos,                     /* setfilepos */
                         FilterFlushFile,                  /* flushfile */
                         FilterEncodeError,                /* filterencode */
                         rrHashDecodeBuffer,               /* filterdecode */
                         FilterLastError,                  /* lasterror */
                         -1, NULL, NULL, NULL);
    rrHashFilter.filter_id = 1 ;
  }
  return &rrHashFilter;
}

/* Called by pdf_execops before executing the stream */

Bool pdf_rr_start_exec( PDFCONTEXT *pdfc , FILELIST **pflptr , int streamtype ,
                        OBJECT *obj , FILELIST **rr_flptr , void **priv )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT arg = OBJECT_NOTVM_NULL ;
  FILELIST *filter ;

  if ( streamtype != PDF_STREAMTYPE_PAGE &&
       streamtype != PDF_STREAMTYPE_FORM ) {
    return TRUE ;
  }

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  if ( ixc->rr_state == NULL || ixc->encapsulated ) {
    return TRUE ;
  }

  HQASSERT( *priv == NULL , "Non-null slot for op context" ) ;
  *priv = mm_alloc( pdfxc->mm_structure_pool , sizeof( RR_OP_CONTEXT ) ,
                    MM_ALLOC_CLASS_PDF_RR_STATE ) ;
  if ( *priv == NULL ) {
    return error_handler( VMERROR ) ;
  }

  /* Layer hashing counting filter on top of stream.
     Note: We could defer this until we actually see an inline image. */

  HQASSERT(pflptr && *pflptr, "No contents stream in pdf_rr_start_exec") ;
  HQASSERT((*pflptr)->myinitfile != rrHashFilterInit,
           "Already layered an rrHashFilter on top of this contents stream") ;

  file_store_object(&arg, *pflptr, CAST_UNSIGNED_TO_UINT8(LITERAL)) ;

  /* Close any RR hash filter from the last contents stream. */
  if ( *rr_flptr != NULL ) {
    pdf_rr_end_exec( pdfc , rr_flptr , NULL ) ;
  }

  /* Now create and add the filter. We have to do this manually because
     pdf_createfilter() does unwanted things such as calling
     filter_create_hook() - we don't need PDF-X preflight here!  This is the
     gist of pdf_createfilter and filter_create_with_alloc. */

  filter = mm_alloc(pdfxc->mm_structure_pool, sizeof(FILELIST),
                    MM_ALLOC_CLASS_PDF_FILELIST) ;
  if (filter == NULL)
    return error_handler(VMERROR) ;

  /* Copy filter template */
  *filter = *rrHash_decode_filter() ;

  theISaveLevel(filter) = CAST_UNSIGNED_TO_UINT8(pdfxc->savelevel) ;
  theIPDFContextID(filter) = pdfxc->id ;

  /* Initialise it */
  if ( ! (*theIMyInitFile(filter))(filter, &arg, NULL) ) {
    Bool result = (*theIFileLastError(filter))(filter) ;
    mm_free( pdfxc->mm_structure_pool , filter , sizeof( FILELIST )) ;
    return result ;
  }

  /* Mark filter as open if we succeeded; leave CST flag unset */
  SetIOpenFlag(filter) ;
  SetIRewindableFlag(filter) ;

  /* Return the new filtered stream */
  *pflptr = *rr_flptr = filter ;
  file_store_object(obj, filter, CAST_UNSIGNED_TO_UINT8(LITERAL)) ;
  pdfc->contentsStream = obj ;

  return TRUE ;
}

/* Called by pdf_execops after executing the stream */

void pdf_rr_end_exec( PDFCONTEXT *pdfc , FILELIST **rr_flptr , void **priv )
{
  PDFXCONTEXT *pdfxc ;
  FILELIST *flptr = *rr_flptr ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  if ( flptr != NULL ) {
    HQASSERT(flptr->myinitfile == rrHashFilterInit, "Expected rrHashFilter") ;

    ClearIRewindableFlag( flptr ) ;
    if ( isIOpenFile( flptr ) )
      ( void )(*theIMyCloseFile( flptr ))( flptr, CLOSE_EXPLICIT ) ;
    mm_free( pdfxc->mm_structure_pool , flptr , sizeof( FILELIST )) ;

    *rr_flptr = NULL ;
  }

  pdfc->contentsStream = NULL ;

  if ( priv != NULL && *priv != NULL ) {
    mm_free( pdfxc->mm_structure_pool , *priv , sizeof( RR_OP_CONTEXT )) ;
    *priv = NULL ;
  }
}

/** Retrieve the marks array for the given page and element number and
    copy it into rr_state->marks ready for replaying or storing. */

static Bool pdf_rr_load_marks( RR_STATE *rr_state , RR_PAGE *page ,
                               uint32 element_num )
{
  RBT_NODE *node =
    rbt_search( rr_state->markstree ,
                ( uintptr_t )page->elements[ element_num ].marks_hash ) ;
  RR_MARKS_NODE *mn ;

  if ( node == NULL ) {
    HQFAIL( "Marks node not found" ) ;
    return error_handler( UNDEFINEDRESULT ) ;
  }
  mn = rbt_get_node_data( node ) ;
  if ( mn == NULL ) {
    HQFAIL( "Marks node not found" ) ;
    return error_handler( UNDEFINEDRESULT ) ;
  }
  HqMemCpy( rr_state->marks , mn->marks ,
            ( sizeof( *page->marks ) * page->mark_count )) ;

  return TRUE ;
}

/** Called every time a page starts, unconditionally. A cheap no-op if
    retained raster is not in use. This routine can, exceptionally,
    reset \c *ixc->page_continue to indicate to the caller that it
    should not do anything further with the current page. */

Bool pdf_rr_start_page( PDFCONTEXT *pdfc, OBJECT *page_dict ,
                        PDF_PAGEDEV *pageDev )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  uint32 element_num = MAXUINT32 ;
  RR_STATE *rr_state ;
  RR_PAGE *page ;
  DL_STATE *dl_page ;
  uint32 ei ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL || ixc->encapsulated ) {
    return TRUE ;
  }

  HQASSERT( rr_state != NULL , "Null retained raster state" ) ;

  rr_state->nodes_count = 0 ;
  page = & rr_state->pages[ pdfxc->pageId ] ;
  dl_page = pdfc->corecontext->page ;

  ixc->page_discard = FALSE ;
  ixc->ignore_setpagedevice = FALSE ;
  ixc->ignore_showpage = FALSE ;

  rr_state->mark_count = 0 ;

  if ( dl_page->irr.generating ) {
    /* Must be done before gsave below, or the corresponding grestore
       will clear this. */
    if ( ! run_ps_string((uint8*)"<< /HVDInternal false >> setpagedevice") )
      return FALSE ;
  }

  if ( ! gs_gpush( GST_GSAVE )) {
    return FALSE ;
  }

  rr_state->gid = gstackptr->gId ;

  switch ( rr_state->mode ) {
    case RR_MODE_SKIPPING:
      ixc->page_continue = FALSE ;
      ixc->ignore_setpagedevice = TRUE ;
      ixc->ignore_showpage = TRUE ;
      break ;

    case RR_MODE_PRE_SCANNING:
      RR_DBG_OPEN( RESOURCES , rr_state , "resources" ,
                   pdfxc->pageId , MAXUINT32 ) ;
      if ( ! pdf_rr_hash_resources( pdfc, page_dict )) {
        return FALSE ;
      }
      RR_DBG_CLOSE( RESOURCES , rr_state ) ;
      ixc->page_continue = FALSE ;
      ixc->ignore_setpagedevice = TRUE ;
      ixc->ignore_showpage = TRUE ;
      if ( rr_state->tl_ref != SW_TL_REF_INVALID ) {
        CHECK_TL_SUCCESS(
          SwTimelineSetProgress( rr_state->tl_ref ,
                                 ( sw_tl_extent )pdfxc->pageId )) ;
      }
      break ;

    case RR_MODE_SCANNING:
      RR_DBG_OPEN( ANY , rr_state , "scan" , pdfxc->pageId , MAXUINT32 ) ;
      RR_DBG( SEQUENCE_POINTS , ( rr_state , ( uint8 * )"%% -*-ps-*-\n" )) ;

      pdf_rr_hash_reset( ixc , & rr_state->pdl_state ) ;
      pdf_rr_hash_reset( ixc , & rr_state->text_state ) ;
      rr_state->current_atomic_number = 0 ;
      rr_state->current_atom_start = MAXUINT32 ;
      rr_state->current_atom_end = 0 ;
      rr_state->current_compositing = FALSE ;
      rr_state->current_pattern = FALSE ;
      rr_state->current_patternstate = NULL ;
      rr_state->in_inline_image = FALSE ;
      rr_state->abandon_scan = FALSE ;

      {
        RR_HASH_PARAMS params ;

        /* The page device contributions from the PDF page hierarchy
           are the starting point for the PDL hash. That way two marks
           which differ only in, say, the /Rotate value in their
           respective pages will get different hashes. */

        params.pdfc = pdfc ;
        pdf_rr_hash_reset( ixc , & params.hash_state ) ;
        params.parent = NULL ;
        params.u.pkey = NULL ;
        params.is_form = FALSE ;
        params.has_matrix = FALSE ;
        params.has_ref = FALSE ;
        params.is_image = FALSE ;
        params.stream_len = 0 ;

        if ( ! pdf_rr_hash_operand( & params , pageDev->MediaBox ) ||
             ! pdf_rr_hash_operand( & params , pageDev->CropBox ) ||
             ! pdf_rr_hash_operand( & params , pageDev->ArtBox ) ||
             ! pdf_rr_hash_operand( & params , pageDev->TrimBox ) ||
             ! pdf_rr_hash_operand( & params , pageDev->BleedBox ) ||
             ! pdf_rr_hash_operand( & params , pageDev->Rotate ) ||
             ! pdf_rr_hash_operand( & params , pageDev->UserUnit )) {
          return FALSE ;
        }

        if ( pageDev->PlateColor != NULL ) {
          pdf_rr_hash( & params.hash_state , theICList( pageDev->PlateColor ) ,
                       pageDev->PlateColor->len ) ;
        }

        pdf_rr_hash_copy( & params.hash_state , & rr_state->pdl_state ) ;
      }

      if ( rr_state->tl_ref != SW_TL_REF_INVALID ) {
        CHECK_TL_SUCCESS(
          SwTimelineSetProgress( rr_state->tl_ref ,
                                 ( sw_tl_extent )pdfxc->pageId )) ;
      }

      ixc->page_discard = TRUE ;
      break ;

    case RR_MODE_NORMAL:
      multi_mutex_lock( & rr_state->mutex ) ;
      if ( page->state == RR_PAGE_COMPLETE_EARLY ) {
        pdf_rr_set_page_complete( rr_state , pdfxc->pageId , FALSE ) ;
      }

      if ( page->state == RR_PAGE_COMPLETE ) {
        /* The page was already completed: this means all the rasters
           necessary for it were delivered already as part of preceding
           pages. */
        multi_mutex_unlock( & rr_state->mutex ) ;
        ixc->page_continue = FALSE ;
        ixc->page_discard = TRUE ;
        return TRUE ;
      }
      multi_mutex_unlock( & rr_state->mutex ) ;

      ixc->page_discard = FALSE ;

      /* Check to see if there are any cacheable nodes on the current
         page. If there are and any of them don't exist in the cache,
         our next move is to go to RR_MODE_STORING or
         RR_MODE_EXPORTING. If all are already cached, we go straight
         to RR_MODE_REPLAYING. If there are none, we remain in
         RR_MODE_NORMAL. */

      for ( ei = 0 ; ei < page->element_count ; ei++ ) {
        RBT_NODE *node ;
        RR_ELEM_NODE *en ;

        node = rbt_search( rr_state->elemtree ,
                           ( uintptr_t )page->elements[ ei ].id ) ;
        HQASSERT( node != NULL , "Didn't find elem tree node" ) ;
        en = rbt_get_node_data( node ) ;
        HQASSERT( node != NULL , "Didn't find elem tree node data" ) ;

        if ( ixc->OptimizedPDFExternal || ( ei == 0 && en->hits > 1 )) {
          SWMSG_RR_ELEMENT_QUERY query ;
          sw_event_result er ;

          query.connection = rr_state->cache ;
          query.timeline = rr_state->tl_ref ;
          query.id = page->elements[ ei ].id ;
          query.handle = ( uintptr_t )NULL ;

          er = SwEvent( SWEVT_RR_ELEMENT_QUERY , & query , sizeof( query )) ;

          if ( er >= SW_EVENT_ERROR ) {
            return FALSE ;
          }
          else if ( er == SW_EVENT_HANDLED ) {
            /* This may be overridden: we're not necessarily done
               yet. */
            if ( ! ixc->OptimizedPDFExternal ) {
              irr_addtodl( pdfc->corecontext->page ,
                           ( irr_store_t * )query.handle ) ;
            }
            rr_state->mode = RR_MODE_REPLAYING ;
          }
          else { /* SW_EVENT_UNHANDLED */
            SWMSG_RR_ELEMENT_REF pending ;

            pending.connection = rr_state->cache ;
            pending.timeline = rr_state->tl_ref ;
            pending.id = query.id ;

            er = SwEvent( SWEVT_RR_ELEMENT_PENDING , & pending ,
                          sizeof( pending )) ;
            if ( er >= SW_EVENT_ERROR ) {
              return FALSE ;
            }
            /* SW_EVENT_UNHANDLED is not an error here. */

            element_num = ei ;

            if ( ixc->OptimizedPDFExternal ) {
              if ( ! external_retained_raster( pdfc->corecontext ,
                                               page->elements[ ei ].id ,
                                               en->hits )) {
                return FALSE ;
              }
              rr_state->mode = RR_MODE_EXPORTING ;
            } else {
              /* Do a setpagedevice to activate the IRR pgb device. Must be
                 done outside of the gsave/grestore to ensure the grestore
                 doesn't deactivate the pagedevice early. */
              if ( ! gs_cleargstates( rr_state->gid , GST_GSAVE , NULL ) ||
                   ! run_ps_string(( uint8 * )"<< /HVDInternal true >> "
                                   "setpagedevice") ||
                   ! irr_setdestination( dl_page ,
                                         pdf_irrc_get_pool( rr_state->cache ) ,
                                         & en->bbox , rr_state ,
                                         ( uintptr_t )page->elements[ ei ].id ,
                                         pdf_rr_cache_element ) ||
                   ! gs_gpush( GST_GSAVE )) {
                return FALSE ;
              }

              rr_state->gid = gstackptr->gId ;

              rr_state->mode = RR_MODE_STORING ;
            }
            SET_DEVICE( DEVICE_SUPPRESS ) ;

            /* Copy the correct marks array into place so the omit
               flags are ready for exporting/storing. */

            if ( ! pdf_rr_load_marks( rr_state , page , ei )) {
              return FALSE ;
            }

            break ;
          }

          /* Either the element is already cached or it's about to
             be. Here's where we want to lock it until the page is
             complete. */

          /** \todo There's potential for the element to be purged
              after the query but before this lock: we need to close
              that gap in the API. */

          {
            SWMSG_RR_ELEMENT_REF lock ;

            lock.connection = rr_state->cache ;
            lock.timeline = rr_state->tl_ref ;
            lock.id = query.id ;

            er = SwEvent( SWEVT_RR_ELEMENT_LOCK , & lock , sizeof( lock )) ;
            if ( er >= SW_EVENT_ERROR ) {
              return FALSE ;
            }
            /* SW_EVENT_UNHANDLED is not an error here. */
          }
        }
      }

      if ( rr_state->mode == RR_MODE_REPLAYING ) {
        if ( ixc->OptimizedPDFExternal ) {
          /* Replaying is not relevant for ERR. We've already
             exported all there is to export for this page so we can
             just move on to the next */
          rr_state->mode = RR_MODE_NORMAL ;
          ixc->page_continue = FALSE ;
          ixc->page_discard = TRUE ;
        }
        else {
          /* Copy the right marks array into place ready for replaying
             the variable marks on the page. If the page's element
             count is exactly one then there are no variable marks at
             all. */

          if ( page->element_count > 1 ) {
            HQASSERT( page->element_count == 2 ,
                      "Expecting a maximum of two elements per IRR page" ) ;
            element_num = 1 ;

            if ( ! pdf_rr_load_marks( rr_state , page , element_num )) {
              return FALSE ;
            }
          }
          else {
            uint32 mark ;

            /* There is no variable data for this page: the cached
               background is all we need. Load the marks array for the
               cached background and then set all the omit flags
               (saves storing a dummy element for an empty variable
               data set). */

            if ( ! pdf_rr_load_marks( rr_state , page , 0 )) {
              return FALSE ;
            }

            for ( mark = 0 ; mark < page->mark_count ; mark++ ) {
              rr_state->marks[ mark ].omit =
                ( ! rr_state->marks[ mark ].is_form ||
                  rr_state->marks[ mark ].has_ref ) ;
            }
          }

          RR_DBG_OPEN( ANY , rr_state , rr_mode_names[ rr_state->mode ] ,
                       pdfxc->pageId , element_num ) ;
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state , ( uint8 * )"%% -*-ps-*-\n" )) ;
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state ,
                    ( uint8 * )"%% Replaying element #%d\n" ,
                    element_num )) ;
        }
      }
#if defined( DEBUG_BUILD )
      else {
        uint8 *hash ;

        if ( element_num != MAXUINT32 ) {
          hash = page->elements[ element_num ].id ;

          RR_DBG_OPEN( ANY , rr_state , rr_mode_names[ rr_state->mode ] ,
                       pdfxc->pageId , element_num ) ;
          RR_DBG( SEQUENCE_POINTS , ( rr_state , ( uint8 * )"%% -*-ps-*-\n" )) ;
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state , ( uint8 * )"%% Selected element %d - id "
                    "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                    "%02x%02x%02x%02x%02x%02x%02x%02x\n" , element_num ,
                    hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
                    hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
                    hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
                    hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ])) ;
          rr_state->dbg_page_num = rr_state->page_offset + pdfxc->pageId + 1 ;
          rr_state->dbg_element_num = element_num ;
        }
        else {
          RR_DBG_OPEN( ANY , rr_state , rr_mode_names[ rr_state->mode ] ,
                       pdfxc->pageId , MAXUINT32 ) ;
          RR_DBG( SEQUENCE_POINTS , ( rr_state , ( uint8 * )"%% -*-ps-*-\n" )) ;
        }
      }
#endif

      if ( rr_state->mode == RR_MODE_NORMAL && ixc->OptimizedPDFExternal ) {
        /* Since every element for the page has been dispatched, here
           is where we set the page state to ready. It's done later
           for IRR (when the rendering has completed). */
        if ( ! pdf_rr_set_page_ready( rr_state , pdfxc->pageId )) {
          return FALSE ;
        }
      }

      if (( rr_state->mode == RR_MODE_NORMAL ||
            rr_state->mode == RR_MODE_REPLAYING ) &&
          ! ixc->OptimizedPDFExternal ) {
#if defined( DEBUG_BUILD )
        monitorf((uint8 *)( "showpage %d - Page %d %s\n" ) ,
                 ++( rr_state->dbg_showpage_count ) ,
                 rr_state->page_offset + pdfxc->pageId + 1 ,
                 ( rr_state->mode == RR_MODE_REPLAYING ?
                   "replaying" : "normal mode" )) ;
#endif

        /* In IRR we use the IRR state hanging off the DL page to
           store the page index ready for generating a page complete
           event when the rendering timeline ends. */

        if ( ! irr_setdestination( dl_page ,
                                   pdf_irrc_get_pool( rr_state->cache ) ,
                                   NULL , rr_state , pdfxc->pageId , NULL )) {
          return FALSE ;
        }
      }
      break ;

    default:
      break ;
  }

  return TRUE ;
}

/** Called every time a page ends. There's a fair bit of processing to
    be done here, principally to do with deciding whether to let
    execution continue as normal or whether to set or reset \c
    ixc->page_continue, which will force the containing loop to go
    around again when true. We do this so we can allow through a
    different set of marking operators next time. If retained raster
    is not enabled, this routine must guarantee to change \c
    ixc->page_continue from \c TRUE to \c FALSE. In particular, if we
    ever stub out retained raster, that means the stub version of this
    function must do this. */

Bool pdf_rr_end_page( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  ixc->page_continue = FALSE ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL || ixc->encapsulated ) {
    return TRUE ;
  }

  if ( rr_state->mode == RR_MODE_REPLAYING ) {
    rr_state->mode = RR_MODE_NORMAL ;
  }

  switch ( rr_state->mode ) {
    case RR_MODE_SCANNING: {
      RR_PAGE *page = & rr_state->pages[ pdfxc->pageId ] ;
      RR_PAGE_LINK *new_links ;
      RR_PAGE_MARK *new_marks ;
      RR_PAGE_LINK *link ;
      uint32 mark ;
      Bool page_empty = TRUE ;
      Bool cacheable_mark = FALSE ;
      Bool unique_mark = FALSE ;

      /* We have to do this clumsy test rather than just
         "rr_state->nodes_count == 0" for degenerate pages because
         some jobs go to the trouble of opening a single form and then
         making no mark inside it when all they're trying to output is
         an empty page. */

        for ( mark = 0 ; mark < rr_state->nodes_count ; mark++ ) {
          if ( ! rr_state->marks[ mark ].is_form ||
               rr_state->marks[ mark ].has_ref ) {
            page_empty = FALSE ;
            break ;
          }
        }

        if ( page_empty ) {
          uint8 hash[ RR_HASH_LENGTH ] ;
          RBT_NODE *node ;

          /* Special case: there were no marks at all on the
             page. Insert a faux node just so everything works the same
             as if there was a single mark - including setting
             OptimizedPDFId when ERR is on. */

          pdf_rr_hash_finalise( & rr_state->pdl_state , hash ) ;
          node = rbt_search( rr_state->scantree , ( uintptr_t )hash ) ;

          if ( node == NULL ) {
            RR_SCAN_NODE new_sn ;

            new_sn.hash = NULL ;
            new_sn.hits = 1 ;
            bbox_clear( & new_sn.bbox ) ;
            new_sn.u.orig = NULL ;
            new_sn.last_page = MAXUINT32 ;
            new_sn.pages_hash = 0 ;
            new_sn.ptn = NULL ;
            new_sn.is_form = FALSE ;
            new_sn.has_matrix = FALSE ;
            new_sn.has_ref = FALSE ;
            new_sn.composited = FALSE ;
            new_sn.pattern = FALSE ;
            new_sn.significant = FALSE ;

            RR_DBG( HASHES ,
                    ( rr_state , ( uint8 * )"%% Empty page node - NEW hash: "
                      "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                      "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
                      hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
                      hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
                      hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
                      hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ])) ;

            node = pdf_rr_scantree_createnode( pdfxc , rr_state->scantree ,
                                               hash , & new_sn ) ;

            if ( node == NULL ) {
              return FAILURE( FALSE ) ;
            }
          }
          else {
            RR_SCAN_NODE *sn = rbt_get_node_data( node ) ;

            sn->hits++ ;
          }

          if ( ! pdf_rr_add_node_to_page( pdfc , rbt_get_node_data( node ) , 0 ,
                                          FALSE , FALSE , 0 , 0 , 0 ,
                                          pdfxc->pageId )) {
            return FAILURE( FALSE ) ;
          }
        }

        /* We need to tidy up if there were any unmatched BT/ET pairs or
           similar. */

        if ( rr_state->nodes_count > 0 ) {
          while ( rr_state->current_atomic_number > 0 ) {
            HQTRACE( TRUE , ( "Unbalanced atomic mark points on page %d" ,
                              rr_state->page_offset + pdfxc->pageId + 1 )) ;
            pdf_rr_decrement_atomic_number( rr_state ) ;
          }
        }

        /* Store this page's hash link sequence. We'll allow for the
           possibility of manipulating it once the whole scan is
           complete. */

        new_links = mm_alloc( pdfxc->mm_structure_pool ,
                              ( sizeof( *new_links ) *
                                rr_state->nodes_count ) ,
                              MM_ALLOC_CLASS_PDF_RR_LINKS ) ;

        if ( new_links == NULL ) {
          return FAILURE( error_handler( VMERROR )) ;
        }

        new_marks = mm_alloc( pdfxc->mm_structure_pool ,
                              ( sizeof( *new_marks ) *
                                rr_state->nodes_count ) ,
                              MM_ALLOC_CLASS_PDF_RR_MARKS ) ;

        if ( new_marks == NULL ) {
          mm_free( pdfxc->mm_structure_pool , new_links ,
                   ( sizeof( *new_links ) * rr_state->nodes_count )) ;
          return FAILURE( error_handler( VMERROR )) ;
        }

        HqMemCpy( new_links , rr_state->links ,
                  sizeof( *new_links ) * rr_state->nodes_count ) ;
        HqMemCpy( new_marks , rr_state->marks ,
                  sizeof( *new_marks ) * rr_state->nodes_count ) ;
        page->mark_count = rr_state->nodes_count ;
        page->links = new_links ;
        page->marks = new_marks ;

        /* Build the initial state of the linked list, ready for it to
           be manipulated in our quest for supernodes. */

        page->head.next = & page->links[ 0 ] ;
        page->links[ 0 ].prev = & page->head ;
        page->tail.prev = & page->links[ page->mark_count - 1 ] ;
        page->links[ page->mark_count - 1 ].next = & page->tail ;
        page->state = RR_PAGE_WAITING ;

        for ( mark = 0 ; mark < page->mark_count - 1 ; mark++ ) {
          page->links[ mark ].next = & page->links[ mark + 1 ] ;
        }

        for ( mark = page->mark_count - 1 ; mark > 0 ; mark-- ) {
          page->links[ mark ].prev = & page->links[ mark - 1 ] ;
        }

        /* This step removes any links which point to placeholder form
           marks - they get in the way of the supernode
           calculations. Plus while we're at it, detect marks with
           only one hit which signify a unique page. */

        for ( link = page->head.next ; link != & page->tail ; ) {
          if ( link->sn != rr_state->nil_sentinel ) {
            if ( link->sn->hits <= 1 ) {
              unique_mark = TRUE ;
            }
            else if ( link->sn->significant || link->sn->composited ) {
              cacheable_mark = TRUE ;
            }
          }

          if ( page->marks[ link->mark_num ].is_form &&
               ! page->marks[ link->mark_num ].has_ref ) {
            RR_PAGE_LINK *temp = link->next ;

            link->next->prev = link->prev ;
            link->prev->next = link->next ;
            link->next = NULL ;
            link->prev = NULL ;

            link = temp ;
          }
          else {
            link = link->next ;
          }
        }

        /* Now we decide if it's worth carrying on with the scan: does
           the number of unique pages scanned so far exceed the scan
           limit percentage? */

        if ( ! cacheable_mark && unique_mark ) {
          rr_state->unique_page_count++ ;
          RR_DBG( UNIQUE_PAGES ,
                  ( rr_state ,
                    ( uint8 * )"\n%% Page %d is unique: total now %d (%u%%)\n" ,
                    rr_state->page_offset + pdfxc->pageId + 1 ,
                    rr_state->unique_page_count ,
                    ( rr_state->unique_page_count * 100 /
                      rr_state->page_count ))) ;
        }

        if ( rr_state->abandon_scan ) {
          monitorf( UVM( "Abandoning retained raster scan: "
                         "unsupported marks found on page %d\n" ) ,
                    rr_state->page_offset + pdfxc->pageId + 1 ) ;
          rr_state->mode = RR_MODE_SKIPPING ;
        }
        else if ( rr_state->unique_page_count * 100 / rr_state->page_count >
                  ( uint32 )ixc->OptimizedPDFScanLimitPercent ) {
          monitorf( UVM( "Aborting retained raster scan: %u unique "
                         "pages found (%u%% of total pages, "
                         "limit is %d%%)\n" ) ,
                    rr_state->unique_page_count ,
                    rr_state->unique_page_count * 100 / rr_state->page_count ,
                    ixc->OptimizedPDFScanLimitPercent ) ;
          rr_state->mode = RR_MODE_SKIPPING ;
        }

        RR_ASRT_PAGE( rr_state , page ) ;

        if ( ! pdf_rr_make_atomic_supernodes( pdfc , pdfxc->pageId ,
                                              FALSE )) {
          return FAILURE( FALSE ) ;
        }

        RR_ASRT_PAGE( rr_state , page ) ;

        if ( ixc->OptimizedPDFScanWindow > 0 ) {
          if ( rr_state->page_offset + pdfxc->pageId + 1 ==
               ( uint32 )ixc->OptimizedPDFScanWindow ) {
            if ( ! pdf_rr_find_supernodes( pdfc , 0 , pdfxc->pageId )) {
              return FAILURE( FALSE ) ;
            }
          }
          else if ( rr_state->page_offset + pdfxc->pageId + 1 >
                    ( uint32 )ixc->OptimizedPDFScanWindow ) {
            if ( ! pdf_rr_find_supernodes( pdfc , pdfxc->pageId ,
                                           pdfxc->pageId )) {
              return FAILURE( FALSE ) ;
            }

            if ( ! pdf_rr_close_window( pdfc ,
                                        ( pdfxc->pageId -
                                          ixc->OptimizedPDFScanWindow ))) {
              return FAILURE( FALSE ) ;
            }
          }
        }
      }
      break ;

    case RR_MODE_STORING:
    case RR_MODE_EXPORTING:
      RR_DBG( SEQUENCE_POINTS ,
              ( rr_state , ( uint8 * )"%% Storing/exporting\n" )) ;
#if defined( DEBUG_BUILD )
      monitorf( (uint8 *)( "showpage %u - Page %u , element %u\n" ) ,
                ++( rr_state->dbg_showpage_count ) , rr_state->dbg_page_num ,
                rr_state->dbg_element_num ) ;
#endif

      /* We used to have more complex logic here, but for convenience
         in issuing "page ready" events and for simplicity in general,
         now we always go around again. */
      ixc->page_continue = TRUE ;
      rr_state->mode = RR_MODE_NORMAL ;
      break ;

    default:
      break ;
  }

  SET_DEVICE( DEVICE_BAND ) ;

  /* Tidy up any gstate modification and update the world on where we
     are in the job. */
  if ( ! gs_cleargstates( rr_state->gid , GST_GSAVE , NULL )) {
    return FALSE ;
  }

  pdfc->corecontext->page->pageno = pdfxc->pageId + 1 ;

  if ( ! error_signalled_context( pdfc->corecontext->error ) &&
       ( ! stack_push_integer( pdfxc->pageId , & operandstack ) ||
         ! push( & ixc->rr_state->SetExplicitPageNumber ,
                 & executionstack ) ||
         ! interpreter( 1 , NULL ))) {
    return FALSE ;
  }

  RR_DBG_CLOSE( ANY , rr_state ) ;

  return TRUE ;
}

/** Allocate and fill a structure capturing the current gstate,
    including PDL hash. */

static Bool pdf_rr_gsave( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  RR_STATE *rr_state ;
  RR_GSTATE *gstate ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  HQASSERT( rr_state != NULL , "rr_state NULL" ) ;

  gstate = mm_alloc( pdfxc->mm_structure_pool ,
                     sizeof( RR_GSTATE ) ,
                     MM_ALLOC_CLASS_PDF_RR_GSTATE ) ;
  if ( gstate == NULL ) {
    return error_handler( VMERROR ) ;
  }

  pdf_rr_hash_copy( & rr_state->pdl_state ,
                    & gstate->saved_pdl_state ) ;
  gstate->enclosing_state = rr_state->current_gstate ;
  rr_state->current_gstate = gstate ;

#if defined( DEBUG_BUILD )
  rr_state->dbg_gstate_depth = 0 ;

  for ( gstate = rr_state->current_gstate ; gstate != NULL ;
        gstate = gstate->enclosing_state ) {
    rr_state->dbg_gstate_depth++ ;
  }
#endif

  return TRUE ;
}

/** Reinstate the gstate and PDL hash from the top of our stack and
    then pop and free it. */

static Bool pdf_rr_grestore( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RR_GSTATE *gstate ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  HQASSERT( rr_state != NULL , "rr_state NULL" ) ;

  gstate = rr_state->current_gstate ;

  pdf_rr_hash_copy( & gstate->saved_pdl_state ,
                    & rr_state->pdl_state ) ;
  rr_state->current_gstate = gstate->enclosing_state ;
  mm_free( pdfxc->mm_structure_pool , gstate , sizeof( *gstate )) ;

#if defined( DEBUG_BUILD )
  rr_state->dbg_gstate_depth = 0 ;

  for ( gstate = rr_state->current_gstate ; gstate != NULL ;
        gstate = gstate->enclosing_state ) {
    rr_state->dbg_gstate_depth++ ;
  }
#endif

  return TRUE ;
}

/** Called from the PDF interpreter when the PDF state machine index
    changes. Some transitions (e.g. from Path Object to PDL state)
    require action by the retained raster scanner in order to stay in
    sync. */

Bool pdf_rr_state_change( PDFCONTEXT *pdfc , Bool skipped , int32 state )
{
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;

  if ( rr_state != NULL && ! ixc->encapsulated ) {
    RR_DBG( STATE_CHANGES ,
            ( rr_state , ( uint8 * )"%% State changed to %d\n" , state )) ;

    if ( skipped &&
         rr_state->pdf_state == OPSTATE_PATHOBJECT && state == OPSTATE_PDL ) {
      /* We need to make sure the tidying up after path-consuming
         operators still happens even if we've skipped them. */
      if ( ! pdf_check_clip( pdfc )) {
        return FALSE ;
      }
    }

    if ( state == OPSTATE_TEXTOBJECT ) {
      pdf_rr_hash_copy( & rr_state->pdl_state , & rr_state->text_state ) ;
    }

    rr_state->pdf_state = state ;
  }

  return TRUE ;
}

/** Calculate the current operator's contribution to the content
    stream hash. */

static Bool pdf_rr_hash_op( PDFCONTEXT *pdfc , void *op , Bool *skip ,
                            RR_OP_CONTEXT *rr_context )
{
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RBT_NODE *node ;
  RR_HASH_PARAMS hash_params ;
  RR_HASH_STATE *base_hash_state ;
  uint8 pdfop = pdf_op_number( op ) ;
  RR_SCAN_NODE *sn = NULL ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( skip != NULL , "Flag pointer is NULL" ) ;
  rr_state = ixc->rr_state ;
  HQASSERT( rr_state != NULL , "rr_state NULL" ) ;

  switch ( pdfop ) {
    case PDFOP_BMC:
    case PDFOP_BDC:
    case PDFOP_EMC:
    case PDFOP_DP:
    case PDFOP_MP:
      /* We don't care about marked content. */
      *skip = TRUE ;
      return TRUE ;

    case PDFOP_Tc:
    case PDFOP_Tf:
    case PDFOP_TL:
    case PDFOP_Tr:
    case PDFOP_Ts:
    case PDFOP_Tw:
    case PDFOP_Tz:
    case PDFOP_Tj:
    case PDFOP_TJ:
    case PDFOP_T1q:
    case PDFOP_T2q:
    case PDFOP_Td:
    case PDFOP_TD:
    case PDFOP_Tm:
    case PDFOP_T1s:
      base_hash_state = & rr_state->text_state ;
      break ;

    default:
      base_hash_state = & rr_state->pdl_state ;
      break ;
  }

  probe_begin( SW_TRACE_RR_HASHOP , op ) ;

  hash_params.pdfc = pdfc ;
  pdf_rr_hash_reset( ixc , & hash_params.hash_state ) ;
  hash_params.parent = NULL ;
  hash_params.u.op = op ;
  hash_params.is_form = FALSE ;
  hash_params.has_matrix = FALSE ;
  hash_params.has_ref = FALSE ;
  hash_params.is_image = FALSE ;
  hash_params.stream_len = 0 ;

  /* The current PDL hash is the starting point for the operator
     hash. */
  pdf_rr_hash_finalise( base_hash_state , rr_context->hash ) ;
  RR_DBG( INTERMEDIATE_HASHES ,
          ( rr_state , ( uint8 * )"%% %s hash: "
            "0x%02x%02x%02x%02x%02x%02x%02x%02x"
            "%02x%02x%02x%02x%02x%02x%02x%02x\n" ,
            base_hash_state == & rr_state->pdl_state ? "PDL" : "text" ,
            rr_context->hash[ 0 ] , rr_context->hash[ 1 ] ,
            rr_context->hash[ 2 ] , rr_context->hash[ 3 ] ,
            rr_context->hash[ 4 ] , rr_context->hash[ 5 ] ,
            rr_context->hash[ 6 ] , rr_context->hash[ 7 ] ,
            rr_context->hash[ 8 ] , rr_context->hash[ 9 ] ,
            rr_context->hash[ 10 ] , rr_context->hash[ 11 ] ,
            rr_context->hash[ 12 ] , rr_context->hash[ 13 ] ,
            rr_context->hash[ 14 ] , rr_context->hash[ 15 ])) ;
  pdf_rr_hash( & hash_params.hash_state , rr_context->hash ,
               sizeof( rr_context->hash )) ;
  if ( ! pdf_rr_hash_stack( & hash_params , & imc->pdfstack )) {
    probe_end( SW_TRACE_RR_HASHOP , op ) ;
    return FALSE ;
  }
  pdf_rr_hash( & hash_params.hash_state , & pdfop , 1 ) ;

  /* Any further hashing goes here - inline images for example */
  if ( pdfop == PDFOP_ID ) {
    /* We have to do our inline image data hashing now so that the following
       check for a match can work, so we must call pdfop_ID early, and skip
       the one that pdf_execops would have done. */
    FILELIST * flptr = oFile( *pdfc->contentsStream ) ;
    Bool result ;
    uint32 start, end ;

#if defined( METRICS_BUILD )
    pdf_rr_metrics.inline_image_count++ ;
#endif

    /* Read the current stream position */
    if ( ! rrHashPos( flptr , & start )) {
      HQFAIL( "Failed to read stream position" ) ;
    }

    /* Switch on stream hashing */
    rrHashBegin( flptr , & hash_params.hash_state ) ;

    /* Call the ID operator early (ugly, but unavoidable).
       Note: We don't do everything that pdf_execops does, because we don't
       expect this to go recursive nor close the stream. Famous last words. */
    result = pdf_op_call( op )( pdfc ) ;

    /* Switch off the hashing */
    rrHashEnd( flptr ) ;

    /* Get the number of consumed bytes */
    if ( ! rrHashPos( flptr , & end )) {
      HQFAIL( "Failed to read stream position" ) ;
    }
    HQASSERT( end > start , "Don't seem to have consumed any inline data." ) ;
    /* We put this in here for node creation in pdf_rr_post_op */
    rr_state->op_context->offset = end - start ;

    *skip = TRUE ;
  }

  /* Hash computed, off we go */
  pdf_rr_hash_finalise( & hash_params.hash_state , rr_context->hash ) ;
  rr_context->is_form = hash_params.is_form ;
  rr_context->has_matrix = hash_params.has_matrix ;
  rr_context->has_ref = hash_params.has_ref ;
  rr_context->is_significant_image =
    ( hash_params.is_image &&
      (( hash_params.stream_len / 1024 ) >= ixc->OptimizedPDFImageThreshold )) ;

#if defined( METRICS_BUILD )
  if ( rr_context->is_form ) {
    pdf_rr_metrics.form_count++ ;
  }
  if ( rr_context->has_matrix ) {
    pdf_rr_metrics.form_matrix_count++ ;
  }
  if ( rr_context->has_ref ) {
    pdf_rr_metrics.form_ref_count++ ;
  }
  if ( hash_params.is_image ) {
    pdf_rr_metrics.image_count++ ;
  }
  if ( rr_context->is_significant_image ) {
    pdf_rr_metrics.significant_image_count++ ;
  }
#endif

  if ( pdf_op_marking( op )) {
    node = rbt_search( rr_state->scantree , ( uintptr_t )rr_context->hash ) ;

    if ( node != NULL ) {
      sn = rbt_get_node_data( node ) ;

      HQASSERT( hash_params.is_form == sn->is_form ,
                "Scan result form flag doesn't match node lookup result." ) ;

      if ( pdfop != PDFOP_ID ) {
        *skip = (( ! rr_context->is_form || rr_context->has_ref ) &&
                 ! rr_state->in_inline_image ) ;
      }

      sn->hits++ ;

      if ( ! pdf_rr_add_node_to_page( pdfc , sn ,
                                      rr_context->mark_number ,
                                      rr_context->is_form ,
                                      rr_context->has_ref ,
                                      rr_context->offset , pdfop ,
                                      rr_state->nested_Do ,
                                      pdfxc->pageId )) {
        probe_end( SW_TRACE_RR_HASHOP , op ) ;
        return FALSE ;
      }
    }
    else if ( rr_context->is_form && rr_context->has_ref ) {
      bbox_store( & rr_context->bbox , MININT , MININT , MAXINT , MAXINT ) ;
      rr_context->opcode = RENDER_hdl ;
      *skip = TRUE ;
    }
  }

#if defined( DEBUG_BUILD )
  RR_DBG_STACK( imc , rr_state , op , *skip ) ;
  if ( sn != NULL ) {
    /* A non-null sn here means this is a hit on an existing node: do
       our debug logging this way around so that diffs with the first
       page that this node appeared on make sense. */
    uint8 *hash = sn->hash ;

    RR_DBG( HASHES ,
            ( rr_state , ( uint8 * )"%% Mark %d: "
              "0x%02x%02x%02x%02x%02x%02x%02x%02x"
              "%02x%02x%02x%02x%02x%02x%02x%02x%s%s%s%s\n" ,
              rr_state->mark_count ,
              hash[ 0 ] , hash[ 1 ] , hash[ 2 ] , hash[ 3 ] ,
              hash[ 4 ] , hash[ 5 ] , hash[ 6 ] , hash[ 7 ] ,
              hash[ 8 ] , hash[ 9 ] , hash[ 10 ] , hash[ 11 ] ,
              hash[ 12 ] , hash[ 13 ] , hash[ 14 ] , hash[ 15 ] ,
              sn->is_form ? " FORM" : "" ,
              sn->has_matrix ? " MATRIX" : "" ,
              sn->has_ref ? " REF" : "" ,
              sn->significant ? " SIGNIFICANT" : "" )) ;
    RR_DBG( NODE_STATS ,
            ( rr_state , ( uint8 * )"%% Existing node (%s) - hits: %d\n" ,
              pdf_op_name( op ) , sn->hits )) ;
  }
#endif

  if ( ! pdf_op_marking( op ) ||
       ( base_hash_state == & rr_state->text_state )) {
    /* A mark can contribute to the base hash if it's a text one,
       since for example Tj advances the text matrix. */
    pdf_rr_hash( base_hash_state , rr_context->hash ,
                 sizeof( rr_context->hash )) ;
  }
  else if ( rr_context->has_matrix ) {
    /* Executing a Form XOjbect with a Matrix entry potentially
       modifies the gstate, but only inside the form execution. Make a
       note to take account of this after the implicit gsave so that
       the PDL hash is restored properly once the Form is done. */
    HqMemCpy( rr_state->deferred_pdl_hash_contrib , rr_context->hash ,
              sizeof( rr_context->hash )) ;
    rr_state->deferred_pdl_hash = TRUE ;
  }

  probe_end( SW_TRACE_RR_HASHOP , op ) ;

  return TRUE ;
}

/** Called before every operator in the content stream. Here we decide
    whether to proceed with the operator (*skip). This is the crux of
    the optimisation: when we have something cached, we skip the
    lengthy interpretation of the operator. */

Bool pdf_rr_pre_op( PDFCONTEXT *pdfc , void *op , Bool *skip , void *priv )
{
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  RR_PAGE *page ;
  Bool result = FALSE ;
  uint8 pdfop = pdf_op_number( op ) ;
  RR_OP_CONTEXT *rr_context ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL || ixc->encapsulated ) {
    return TRUE ;
  }
  HQASSERT( priv != NULL , "No op context" ) ;
  rr_context = rr_state->op_context = priv ;

  probe_begin( SW_TRACE_RR_PREOP , op ) ;

  rr_context->mark_number = rr_state->mark_count ;
  rr_context->opcode = RENDER_void ;
  rr_context->offset = 0 ;
  bbox_clear( & rr_context->bbox ) ;
  rr_context->is_form = FALSE ;
  rr_context->has_matrix = FALSE ;
  rr_context->has_ref = FALSE ;
  rr_context->is_significant_image = FALSE ;

  rr_state->op_context = rr_context ;

  if ( pdfop == PDFOP_BIV ) {
    rr_state->in_inline_image = TRUE ;
  }

  page = & rr_state->pages[ pdfxc->pageId ] ;
  if ( rr_state->mode != RR_MODE_NORMAL && page->mark_count > 0 &&
       pdf_op_marking( op ) && rr_state->mark_count >= page->mark_count ) {
    /* We're walking off end of page marks: this can happen if there
       are annotations. The right thing to do is nothing at all. */
    return TRUE ;
  }

  switch ( rr_state->mode ) {
    case RR_MODE_NORMAL:
      break ;

    case RR_MODE_SKIPPING:
    case RR_MODE_PRE_SCANNING:
    case RR_MODE_POST_SCANNING:
      HQFAIL( "Shouldn't get here in this mode." ) ;
      break ;

    case RR_MODE_SCANNING:
      /* This could be the first operator inside a recursive form
         context. If so, here's where we execute our lazily evaluated
         gsave. */
      if ( rr_state->nested_Do > rr_state->last_implicit_gsave ) {
        RR_DBG( IMPLICIT_GSTACK ,
                ( rr_state , ( uint8 * )"%% Implicit gsave (level %d)\n" ,
                  rr_state->nested_Do )) ;

        if ( ! pdf_rr_gsave( pdfc )) {
          FAILURE_GOTO( CLEANUP ) ;
        }

        if ( rr_state->deferred_pdl_hash ) {
          pdf_rr_hash( & rr_state->pdl_state ,
                       rr_state->deferred_pdl_hash_contrib ,
                       sizeof( rr_state->deferred_pdl_hash_contrib )) ;
          rr_state->deferred_pdl_hash = FALSE ;
        }
        rr_state->last_implicit_gsave = rr_state->nested_Do ;
      }

      switch ( pdfop ) {
        case PDFOP_q:
          if ( ! pdf_rr_gsave( pdfc )) {
            FAILURE_GOTO( CLEANUP ) ;
          }
          break ;

        case PDFOP_BT:
          pdf_rr_increment_atomic_number( rr_state , rr_state->mark_count ) ;
          break ;

        case PDFOP_Tm:
          /** The Tm operator is defined as not being cumulative, so it
              may be useful to be able to sub-divide BT/ET delineated
              text. It's a two-sided coin, though, because decreasing the
              size and increasing the number of atomic supernodes is bad
              for memory consumption. */
          pdf_rr_decrement_atomic_number( rr_state ) ;
          pdf_rr_increment_atomic_number( rr_state , rr_state->mark_count ) ;
          break ;
      }

      /* Now hash the operator */
      if ( ! pdf_rr_hash_op( pdfc , op , skip , rr_context )) {
        FAILURE_GOTO( CLEANUP ) ;
      }

      if ( rr_context->is_form && ! rr_context->has_ref ) {
        if ( rr_state->nested_Do == MAXUINT8 ) {
          ( void )error_handler( LIMITCHECK ) ;
          FAILURE_GOTO( CLEANUP ) ;
        }
        rr_state->nested_Do++ ;
      }
      break ;

    case RR_MODE_STORING:
    case RR_MODE_EXPORTING:
      if ( pdf_op_marking( op )) {
        if ( rr_state->marks[ rr_state->mark_count ].omit ) {
          *skip = TRUE ;
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state ,
                    ( uint8 * )"%% Mark #%d - store/export skip\n" ,
                    rr_state->mark_count )) ;
        }
        else {
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state , ( uint8 * )"%% Mark #%d - store/export %s\n" ,
                    rr_state->mark_count ,
                    ( rr_state->marks[ rr_state->mark_count ].is_form ?
                      "form" : "MARK" ))) ;
        }
      }
      break ;

    case RR_MODE_REPLAYING:
      if ( pdf_op_marking( op )) {
        /* Note that this is Internal Retained Raster specific: ERR
           exports everything via the same mechanism and doesn't use
           REPLAYING mode. */

        if ( rr_state->marks[ rr_state->mark_count ].is_form &&
             ! rr_state->marks[ rr_state->mark_count ].has_ref ) {
          *skip = FALSE ;
          if ( rr_state->nested_Do == MAXUINT8 ) {
            ( void )error_handler( LIMITCHECK ) ;
            FAILURE_GOTO( CLEANUP ) ;
          }
          rr_state->nested_Do++ ;
          rr_context->is_form = TRUE ;
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state , ( uint8 * )"%% Mark #%d - form exec\n" ,
                    rr_state->mark_count )) ;
        }
        else if ( rr_state->marks[ rr_state->mark_count ].omit ) {
          *skip = TRUE ;
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state , ( uint8 * )"%% Mark #%d - omit\n" ,
                    rr_state->mark_count )) ;
        }
        else {
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state ,
                    ( uint8 * )"%% Mark #%d - replay MARK\n" ,
                    rr_state->mark_count )) ;
        }
      }
      break ;
  } /* switch rr_state->mode */

  if ( pdfop == PDFOP_ID && *skip && rr_state->mode != RR_MODE_SCANNING ) {
    /* Skipping an inline image requires skipping its data too */
    uint32 amount = rr_state->marks[ rr_state->mark_count ].inline_data_bytes ;
    FILELIST * flptr = oFile( *pdfc->contentsStream ) ;

    for ( ; amount > 0 ; --amount ) {
      ( void )Getc( flptr ) ;
    }
  }

#if defined( DEBUG_BUILD )
  switch ( rr_state->mode ) {
    case RR_MODE_STORING:
    case RR_MODE_EXPORTING:
    case RR_MODE_REPLAYING:
      RR_DBG_STACK( imc , rr_state , op , *skip ) ;
      break ;

    default:
      break ;
  }
#endif

  result = TRUE ;

 CLEANUP:

  if ( pdf_op_marking( op )) {
    rr_state->mark_count++ ;
  }

  probe_end( SW_TRACE_RR_PREOP , op ) ;

  return result ;
}

/** Called after every operator. Used to finalize hashing when
    scanning, and to keep track of marks when we're not. */

Bool pdf_rr_post_op( PDFCONTEXT *pdfc , void *op , void *priv )
{
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  RR_STATE *rr_state ;
  uint8 pdfop = pdf_op_number( op ) ;
  RR_OP_CONTEXT *rr_context ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  rr_state = ixc->rr_state ;
  if ( rr_state == NULL || ixc->encapsulated ) {
    return TRUE ;
  }
  HQASSERT( priv != NULL , "No op context" ) ;
  rr_context = rr_state->op_context = priv ;

  probe_begin( SW_TRACE_RR_POSTOP , op ) ;

  if ( rr_context->is_form &&
       ! rr_context->has_ref ) {
    if ( rr_state->nested_Do == 0 ) {
      ( void )error_handler( LIMITCHECK ) ;
      return FAILURE( FALSE ) ;
    }
    rr_state->nested_Do-- ;
  }

  if ( rr_state->mode == RR_MODE_SCANNING ) {
    switch ( pdfop ) {
      case PDFOP_Do:
        if ( rr_state->last_implicit_gsave > rr_state->nested_Do ) {
          RR_DBG( IMPLICIT_GSTACK ,
                  ( rr_state , ( uint8 * )"%% Implicit grestore (level %d)\n" ,
                    rr_state->nested_Do )) ;

          if ( ! pdf_rr_grestore( pdfc )) {
            return FAILURE( FALSE ) ;
          }

          rr_state->last_implicit_gsave = rr_state->nested_Do ;
        }
        break ;

      case PDFOP_Q:
        if ( ! pdf_rr_grestore( pdfc )) {
          return FAILURE( FALSE ) ;
        }
        break ;
    }

    if ( pdf_op_marking( op )) {
      RBT_NODE *node ;
      RR_SCAN_NODE *sn ;

      node = rbt_search( rr_state->scantree , ( uintptr_t )rr_context->hash ) ;

      if ( node == NULL ) {
        RR_SCAN_NODE new_sn ;

        /* Sometimes a marking operator can lead to no mark (clipped
           out, translated off the page etc.) but we need the
           significance mask to reflect the cost of interpreting the
           mark regardless. */

        if ( rr_context->opcode == RENDER_void ) {
          switch ( pdfop ) {
            case PDFOP_b:
            case PDFOP_b1s:
            case PDFOP_B:
            case PDFOP_B1s:
            case PDFOP_f:
            case PDFOP_f1s:
            case PDFOP_s:
            case PDFOP_S:
              /* Best guess (could actually have been a rect or a
                 quad): probably doesn't matter too much. Doesn't
                 matter at all unless the default value of
                 ixc->OptimizedPDFSignificanceMask is changed. */
              rr_context->opcode = RENDER_fill ;
              break ;

            case PDFOP_Do:
              /* It doesn't matter if a Form placeholder node gets a
                 significance mask claiming to be an image: it will be
                 removed from the z-order linked list before it has
                 any effect. */
              /*@fallthrough@*/

            case PDFOP_ID:
              rr_context->opcode = RENDER_image ;
              break ;

            case PDFOP_PS:
              /* Not sure there's anything sensible to be done here;
                 it's extremely unlikely we'll ever see one, but if we
                 do then sticking with an opcode of RENDER_void will
                 mean we don't try any caching. */
              break ;

            case PDFOP_T1q:
            case PDFOP_T2q:
            case PDFOP_TJ:
            case PDFOP_Tj:
              rr_context->opcode = RENDER_char ;
              break ;

            case PDFOP_sh:
              rr_context->opcode = RENDER_shfill ;
              break ;
          }
        }

        new_sn.hash = NULL ;
        new_sn.hits = 1 ;
        new_sn.bbox = rr_context->bbox ;
        new_sn.u.orig = NULL ;
        new_sn.last_page = MAXUINT32 ;
        new_sn.pages_hash = 0 ;
        new_sn.ptn = NULL ;
        new_sn.is_form = rr_context->is_form ;
        new_sn.has_matrix = rr_context->has_matrix ;
        new_sn.has_ref = rr_context->has_ref ;
        new_sn.composited = ( rr_state->current_compositing &&
                              ! rr_context->is_form ) ;
        new_sn.pattern = ( rr_state->current_patternstate != NULL &&
                           ! rr_context->is_form ) ;
        new_sn.significant = ((( 1 << ( rr_context->opcode - RENDER_char )) &
                               ixc->OptimizedPDFSignificanceMask ) != 0 ) ;

        /* We have to assume an external reference Form contains
           significant marks. */
        if ( rr_context->has_ref ) {
          new_sn.significant = TRUE ;
        }
        else if ( new_sn.significant &&
                  ( rr_context->opcode == RENDER_mask ||
                    rr_context->opcode == RENDER_image )) {
          new_sn.significant = rr_context->is_significant_image ;
        }

        RR_DBG( HASHES ,
                ( rr_state , ( uint8 * )"%% Mark %d: "
                  "0x%02x%02x%02x%02x%02x%02x%02x%02x"
                  "%02x%02x%02x%02x%02x%02x%02x%02x%s%s%s%s\n" ,
                  rr_context->mark_number ,
                  rr_context->hash[ 0 ] , rr_context->hash[ 1 ] ,
                  rr_context->hash[ 2 ] , rr_context->hash[ 3 ] ,
                  rr_context->hash[ 4 ] , rr_context->hash[ 5 ] ,
                  rr_context->hash[ 6 ] , rr_context->hash[ 7 ] ,
                  rr_context->hash[ 8 ] , rr_context->hash[ 9 ] ,
                  rr_context->hash[ 10 ] , rr_context->hash[ 11 ] ,
                  rr_context->hash[ 12 ] , rr_context->hash[ 13 ] ,
                  rr_context->hash[ 14 ] , rr_context->hash[ 15 ] ,
                  new_sn.is_form ? " FORM" : "" ,
                  new_sn.has_matrix ? " MATRIX" : "" ,
                  new_sn.has_ref ? " REF" : "" ,
                  new_sn.significant ? " SIGNIFICANT" : "" )) ;
        RR_DBG( NODE_STATS ,
                ( rr_state , ( uint8 * )"%% New %s node (%s) [%d %d %d %d]\n" ,
                  debug_opcode_names[ rr_context->opcode ] ,
                  pdf_op_name( op ) ,
                  new_sn.bbox.x1 , new_sn.bbox.y1 ,
                  new_sn.bbox.x2 , new_sn.bbox.y2 )) ;

        node = pdf_rr_scantree_createnode( pdfxc , rr_state->scantree ,
                                           rr_context->hash , & new_sn ) ;
        if ( node == NULL ||
             ! pdf_rr_add_node_to_page( pdfc , rbt_get_node_data( node ) ,
                                        rr_context->mark_number ,
                                        rr_context->is_form ,
                                        rr_context->has_ref ,
                                        rr_context->offset , pdfop ,
                                        rr_state->nested_Do ,
                                        pdfxc->pageId )) {
          return FAILURE( FALSE ) ;
        }
      } /* if node == NULL */

      sn = rbt_get_node_data( node ) ;

      if ( rr_state->current_pattern ) {
        if ( ! sn->pattern ) {
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state , ( uint8 * )"%% Pattern ended\n" )) ;
          pdf_rr_decrement_atomic_number( rr_state ) ;
          rr_state->current_pattern = FALSE ;
        }
      }
      else {
        if ( sn->pattern ) {
          RR_DBG( SEQUENCE_POINTS ,
                  ( rr_state , ( uint8 * )"%% Pattern started\n" )) ;
          pdf_rr_increment_atomic_number( rr_state , rr_context->mark_number ) ;
          rr_state->current_pattern = TRUE ;
#if defined( METRICS_BUILD )
          pdf_rr_metrics.pattern_count++ ;
#endif
        }
      }

      if ( rr_state->current_atom_start < MAXUINT32 &&
           ( ! rr_context->is_form || rr_context->has_ref )) {
        rr_state->current_atom_end = rr_context->mark_number ;
      }
    }

    if ( pdfop == PDFOP_ET ) {
      pdf_rr_decrement_atomic_number( rr_state ) ;
    }
  } /* if rr_state->mode == RR_MODE_SCANNING */

  if ( pdfop == PDFOP_EIV ) {
    rr_state->in_inline_image = FALSE ;
  }

  probe_end( SW_TRACE_RR_POSTOP , op ) ;

  rr_state->op_context = NULL ;

  return TRUE ;
}

/* Log stripped */
