/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfin.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Input API
 */

#ifndef __PDFIN_H__
#define __PDFIN_H__

#include "swpdf.h"

#include "objects.h"
#include "display.h"      /* DLRANGE */
#include "graphics.h"
#include "gs_color.h"     /* COLORSPACE_ID */
#include "rbtree.h"
#include "stacks.h"
#include "mm.h"

#include "pdfparam.h"
#include "pdfacrof.h"    /* Acroform field-value structure */
#include "pdfxPrivate.h"
#include "pdfopt.h"
#include "pdfmc.h"

#include "md5.h"

/** \addtogroup pdfin
    \{ */

/* ----- External constants ----- */

#define BXEX_SEQ_MAX 16            /* Nesting limit of BX/EX pairs */

#define PDFIN_MAX_STACK_SIZE 0x7ffff


#define PDF_MAJOR_VERSION           1
#define PDF_MINOR_VERSION           7


/* ----- External structures ----- */

/** Values obtained from a PDF document's "AcroForm" dictionary. */
typedef struct pdf_acroform_values {
  Bool    NeedAppearances;
  OBJECT *pDefRsrcs;    /**< Default resources dictionary. */
  OBJECT *pDefAppear;   /**< Default appearances string. */
  int32   Quadding;
} PDF_ACROFORM_VALUES;

/** Values for the Trapped item in the Info dictionary. */
typedef int32 INFO_DICT_TRAPPED;
enum {
  /** A name or string of 'True' */
  INFO_TRAPPED_TRUE,

  /** A name or string of 'False' */
  INFO_TRAPPED_FALSE,

  /** Trapped value present, but not a name or string of 'True' or 'False' */
  INFO_TRAPPED_INVALID,

  /** Trapped value not present */
  INFO_TRAPPED_NONE
};

typedef struct {
  /* Page's limiting box. */
  sbbox_t pageLimitBounds;

  /* This is either the Trim or Art box. */
  sbbox_t innerPageBounds;

  Bool usingBleedBoxInViewerPrefs;

  /* The device space of the output condition. */
  COLORSPACE_ID conditionDeviceSpace;
} PdfXState;

/** "execution" context specific to pdfin. */
struct pdf_ixc_params {

  /** Version from the comment in the header. */
  int32 majorversion ;
  int32 minorversion ;
  int32 pdfXversion ;

  /* PDF details. */
  Hq32x2 pdfxref ;
  OBJECT pdfroot ;
  OBJECT pdftrailer ;
  OBJECT pdfinfo ;

  OBJECT trailer_encrypt ;
  OBJECT trailer_id ;

  Bool gotinfo;
  INFO_DICT_TRAPPED infoTrapped;
  Hq32x2 trailer_prev;
  Hq32x2 trailer_xrefstm;
  pdf_ocproperties * oc_props;

  int32 tmp_file_used ;

  /* PDF parameters
   * These are only used for encapsulated jobs (i.e. passed in dictionary
   * to pdfexec.
   * Largely, these are fields are shared with PDFPARAMS, if new params are
   * added, please consider if they should be added to PDFPARAMS as well.
   *
   * NB. This section is indented to make the params obvious.
   */

      /** 'encapsulated' is set for OPI and PDF external file references.  The
          flag affects page group behaviour.  The operators setpagedevice and
          showpage are controlled separately as OPI and external files have
          differing requirements. */
      Bool encapsulated ;

      /** setpagedevice and showpage are ignored for PDF external file
          references and some.  OPI shadows the operators and
          therefore they are safe and required to be called even
          though OPI sets encapsulated. */
      Bool ignore_setpagedevice, ignore_showpage ;

      /* The remaining parameters may also be accessed using
       * currentpdfparams/setpdfparams
       */
      Bool strictpdf ;

      OBJECT *pagerange ;

      /* Owner/User passwords in pdfparams are stored in ..._global
       * These may be supplemented by params dict to pdfexec, in ..._local
       */
      OBJECT *ownerpasswords_local ;
      OBJECT *userpasswords_local ;

      OBJECT *ownerpasswords_global ;
      OBJECT *userpasswords_global ;

      /* The private keys and their passwords. */
      OBJECT *private_key_files_local ;
      OBJECT *private_key_passwords_local ;

      OBJECT *private_key_files_global ;
      OBJECT *private_key_passwords_global ;

      Bool honor_print_permission_flag ;
      Bool missing_fonts ;

      /** page cropping. */
      int32 pagecropto ;

      /* user enforced PDF version */
      int32 EnforcePDFVersion ;
      Bool  abort_for_invalid_types ;

      /** Annotation parameters. */
      Bool PrintAnnotations;
      PDF_ANNOT_PARAMS AnnotParams;

      Bool ErrorOnFlateChecksumFailure ;
      Bool IgnorePSXObjects ;
      Bool WarnSkippedPages ;

      Bool SizePageToBoundingBox ;
      PDF_OC_PARAMS OptionalContentOptions;

      Bool EnableOptimizedPDFScan ;
      int32 OptimizedPDFScanLimitPercent ;
      Bool OptimizedPDFExternal ;
      int32 OptimizedPDFCacheSize ;
      OBJECT *OptimizedPDFCacheID ;
      OBJECT *OptimizedPDFSetupID ;
      Bool OptimizedPDFCrossXObjectBoundaries ;
      int32 OptimizedPDFSignificanceMask ;
      int32 OptimizedPDFScanWindow ;
      int32 OptimizedPDFImageThreshold ;
      Bool ErrorOnPDFRepair;
      Bool PDFXVerifyExternalProfileCheckSums ;
      USERVALUE TextStrokeAdjust ;
      int32 XRefCacheLifetime ;

  /* END of PDF Params */

  /* enforcing conformance to various pdf versions in the job */
  int32 conformance_pdf_version ;

  Bool suppress_duplicate_warnings ;
  int32 enforce_pdf_warnings[NUM_PDFXERRS] ;
  uint8 *icc_cpc ;
  int32 icc_cpc_length ;

  /* values used by the pdfxstatus operator */
  OBJECT PDFXVersionClaimed;
  OBJECT PDFXVersionTreated;
  OBJECT PDFXVersionValid;
  OBJECT PDFXOutputCondition;

  /* Do PDF/X-3 colorspace verification if this isn't present */
  OBJECT PDFXRegistryProfile;
  Bool PDFXOutputProfilePresent;

  /** Current page number */
  int32 pageno ;
  Bool pageFound;

  OBJECT *pPageRef;   /* Single page reference used by
                         pdf_exec_page() */
  OBJECT *pPageLabelsDict;

  /** PDF Repair. */
  Bool repaired ;

  struct pdf_cached_fontdetails *cached_fontdetails ;


  /** AcroForm dictionary values. */
  PDF_ACROFORM_VALUES  AcroForm;

  /** State used to track PDF/X conformance. */
  PdfXState pdfxState;

  /** Trapping params obtained from PJTF structures. */
  struct pdf_jt_info *pPJTFinfo;

  /** GState which was in force when the current page (NOT marking
   * context, page) was begun. Used to get proper defaults for UCR, BG
   * and TR when UCR2, BG2 or TR2 have the value /Default.
   */
  GSTATE *pPageStartGState ;

  /** Opaque reference to the retained raster state. Null if we're not
      doing any retained raster processing. */
  struct RR_STATE *rr_state ;

  /** Flag indicating whether interpretation of the current page's
      content stream(s) should continue - with this flag we can abort
      pages cheaply when necessary but also loop around to interpret a
      page multiple times. */
  Bool page_continue ;

  /** Controls whether a page DL should be passed on to showpage or
      just discarded with an erasepage: used by retained raster during
      its various passes of scanning content streams. */
  Bool page_discard ;
} ;

typedef struct pdf_text_state {
  /* Current text state.
   * Changes here will require changes to pdfop_BT() and pdfop_ET() in
   * pdftxt.c to copy text state for nested BT operations
   */
  OMATRIX TLM ; /**< Text Line Matrix. */

  Bool newTM ;
  OMATRIX TCM ; /**< Text Matrix (used for character & word spacing). */
  OMATRIX TJM ; /**< Text 1000 Matrix (used for the TJ operator offsets). */
  OMATRIX TRM ; /**< Text Rendering Matrix (used for character widths). */
  OMATRIX PQM ; /**< 'Temporary' result matrix (used in pdf_show to
                     concatenate CMap matrices with, to obtain
                     matrices for base fonts */

  Bool newCP ;  /**< Do we need a new CP? */
  FPOINT CP ;   /**< Current Point (used instead of Text Matrix). */

  Bool used_clipmode ; /**< Have we used a clip text rendering mode in
                            this text object. non-strict => may be
                            reset by Tr op. */

  HDLTinfo savedHDLT ; /**< Saved HDLT information. */

  struct pdf_text_state *nextstate ; /**< Next one in the list. */

} PDF_TEXT_STATE ;


/** "marking" context (i.e. per contents stream) specific to pdfin. */
struct pdf_imc_params {

  /** PDF permanent stack. */
  STACK  pdfstack ;
  SFRAME pdfstackframe ;

  /** Current Clip mode. */
  int32 pdfclipmode ;
  /** Have we seen a closepath. */
  Bool seenclosepath;

  /** Stack of graphics states */
  GSTATE *pdf_graphicstack ;

  /** Base of inline operation params on stack
   *  - used by BI/EI (images) and BV/EV (vignettes) etc.
   */
  int32 pdf_inlineopbase;

  /* Default matrix for contents stream is required to create
     patterns. We can't use initmatrix, because that refers to the
     output device, not the input context. */
  OMATRIX defaultCTM ;

  /* Current text state.
   * Changes here will require changes to pdfop_BT() and pdfop_ET() in
   * pdftxt.c to copy text state for nested BT operations
   */
  PDF_TEXT_STATE textstate ;
  int32 textstate_count ;

  /** Nesting count of BX/EX operators and their stack sequence
      points. */
  int32 bxex ;
  int32 bxex_seq[ BXEX_SEQ_MAX ] ;

  /** A flag used in PDF/X validation to indicate that an image is
      being processed */
  Bool handlingImage ;

  /**< For AcroForm fields (used dynamically for each field). */
  OBJECT *pFieldRect;           /* Field rectangle array */
  PDF_FORMFIELD *pFieldValues;  /* Attributes & values of the field */

  /**< Marked content state stack. Not to be confused with marking
       context. */
  pdf_mc_stack  * mc_stack;
  Bool            mc_oc_initial_state; /* This is the state that
                                          optional_content_on is in at
                                          the beginning */
  Bool            mc_DP_oc_status;     /**< FALSE if mark content tag
                                            DP sets OC to OFF. */

  /* Cached PS objects from calls to gs_makepattern within this
     marking context. Note that this is in PSVM, and hence it's also a
     GC root. */
  OBJECT patterncache ;
} ;

/* ----- External global variables ----- */

extern PDF_METHODS pdf_input_methods ;


#if defined( DEBUG_BUILD )

/* Debug flags for PDF */
extern int32 pdf_debug;

/** Flag bits for pdf_debug */
enum {
  PDF_DEBUG_IMAGE = 0x1, /**< List images */
  PDF_DEBUG_IMAGE_DICT = 0x2, /**< List image dicts */
  PDF_DEBUG_IMAGE_JPX = 0x4, /**< List JPX images only */
};

#endif


/** \} */

#endif /* protection for multiple inclusion */


/* Log stripped */
