/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfin.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Input main entry point
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"
#include "objects.h"
#include "mmcompat.h"
#include "hqmemcmp.h"
#include "hqmemcpy.h"     /* HqMemMove */
#include "hqmemset.h"
#include "monitor.h"
#include "fileio.h"
#include "namedef_.h"
#include "ripdebug.h" /* register_ripvar */

#include "devices.h"
#include "matrix.h"
#include "graphics.h"
#include "gstack.h"
#include "gs_color.h"     /* GSC_FILL */
#include "idlom.h"

#include "swpdfin.h"
#include "pdfcntxt.h"
#include "pdfclip.h"
#include "pdfcolor.h"
#include "pdfin.h"
#include "pdfmem.h"
#include "pdfsgs.h"
#include "pdffont.h"
#include "pdfops.h"
#include "pdftxt.h"
#include "pdfscan.h"      /* pdf_xrefobject */
#include "pdfattrs.h"     /* pdf_getpagedevice */
#include "pdfdefs.h"      /* PDF/X defs */
#include "pdfparam.h"     /* PDFParams */
#include "pdfrr.h"        /* pdf_rr_end */
#include "pdfstrm.h"
#include "pdfstrobj.h"    /* pdf_seek_to_compressedxrefobj */
#include "pdfxref.h"
#include "streamd.h"      /* stream_decode_filter */
#include "mps.h"
#include "gcscan.h"
#include "pdfjtf.h"
#include "pdfx.h"
#include "render.h"       /* delete_retained_raster */
#include "routedev.h"     /* optional_content_on */

#include "aes.h"
#include "rc4.h"
#include "cryptFilter.h"
#include "pdfinmetrics.h"
#include "metrics.h"

static mps_res_t pdf_in_scan_context(mps_ss_t ss, PDFXCONTEXT *pdfxc);
static Bool pdf_in_begin_execution_context( PDFXCONTEXT *pdfxc ) ;
static Bool pdf_in_end_execution_context( PDFXCONTEXT *pdfxc ) ;
static Bool pdf_in_purge_execution_context( PDFXCONTEXT *pdfxc , int32 savelevel ) ;

static mps_res_t pdf_in_scan_marking_context( mps_ss_t ss, PDFCONTEXT *pdfc );
static Bool pdf_in_begin_marking_context( PDFXCONTEXT *pdfxc , PDFCONTEXT *pdfc ) ;
static Bool pdf_in_end_marking_context( PDFCONTEXT *pdfc ) ;


#ifdef METRICS_BUILD
pdfin_metrics_t pdfin_metrics ;

static Bool pdfin_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Images")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Compression")) )
    return FALSE ;
  SW_METRIC_INTEGER("JPX", pdfin_metrics.JPX);
  sw_metrics_close_group(&metrics) ; /*Compression*/
  sw_metrics_close_group(&metrics) ; /*Images*/

  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Transparency")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("SoftMasks")) )
    return FALSE ;
  SW_METRIC_INTEGER("alpha", pdfin_metrics.softMaskCounts.alpha);
  SW_METRIC_INTEGER("luminosity", pdfin_metrics.softMaskCounts.luminosity);
  SW_METRIC_INTEGER("image", pdfin_metrics.softMaskCounts.image);
  sw_metrics_close_group(&metrics) ; /*SoftMasks*/
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("BlendSpaces")) )
    return FALSE ;
  SW_METRIC_INTEGER("rgb", pdfin_metrics.blendSpaceCounts.rgb);
  SW_METRIC_INTEGER("cmyk", pdfin_metrics.blendSpaceCounts.cmyk);
  SW_METRIC_INTEGER("gray", pdfin_metrics.blendSpaceCounts.gray);
  SW_METRIC_INTEGER("icc3", pdfin_metrics.blendSpaceCounts.icc3Component);
  SW_METRIC_INTEGER("icc4", pdfin_metrics.blendSpaceCounts.icc4Component);
  SW_METRIC_INTEGER("iccN", pdfin_metrics.blendSpaceCounts.iccNComponent);
  sw_metrics_close_group(&metrics) ; /*BlendSpaces*/

  sw_metrics_close_group(&metrics) ; /*Transparency*/

  return TRUE ;
}

static void pdfin_metrics_reset(int reason)
{
  pdfin_metrics_t init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  pdfin_metrics = init ;
}

static sw_metrics_callbacks pdfin_metrics_hook = {
  pdfin_metrics_update,
  pdfin_metrics_reset,
  NULL
} ;
#endif

#if defined( DEBUG_BUILD )
int32 pdf_debug = 0;
#endif


mps_res_t MPS_CALL pdfparams_scan(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res;
  PDFPARAMS *params;

  UNUSED_PARAM( size_t, s );
  params = (PDFPARAMS *)p;
  res = ps_scan_field( ss, &params->PageRange );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &params->OwnerPasswords );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &params->UserPasswords );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &params->PrivateKeyFiles );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &params->PrivateKeyPasswords );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &params->OptimizedPDFCacheID );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &params->OptimizedPDFSetupID );

  return res;
}


static mps_root_t pdfparams_root;

PDFXCONTEXT *pdfin_xcontext_base = NULL ;

static void pdfin_finish(void) ;

/** Main PDF module initialisation. Must be called before any other PDF
   function. */
static Bool pdfin_swstart(struct SWSTART *params)
{
  corecontext_t *corecontext = get_core_context();
  PDFPARAMS *pdfparams ;
  FILELIST *flptr, *filters ;

  UNUSED_PARAM(struct SWSTART*, params) ;

  corecontext->pdfparams = pdfparams = mm_alloc_static(sizeof(PDFPARAMS)) ;
  if ( pdfparams == NULL )
    return FALSE ;

  initpdfparams(pdfparams) ;

#define N_PDF_FILTERS 4 /* PDF-specific filters */
  filters = flptr = mm_alloc_static(N_PDF_FILTERS * sizeof(FILELIST)) ;
  if ( filters == NULL )
    return FALSE ;

  stream_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;

  rc4_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;

  aes_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;

  crypt_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;

  HQASSERT( flptr - filters == N_PDF_FILTERS ,
            "didn't allocate correct amount of memory for filters" ) ;

#if defined( DEBUG_BUILD )
  register_ripvar(NAME_debug_pdf, OINTEGER, &pdf_debug);
#endif

  if ( !pdf_rr_init() || ! pdf_irrc_init())
    return FALSE ;

  /* Create roots last so we force cleanup on success. */
  if ( mps_root_create(&pdfparams_root, mm_arena, mps_rank_exact(),
                       0, pdfparams_scan, pdfparams, 0) != MPS_RES_OK ) {
    pdf_rr_finish() ;
    pdf_irrc_finish() ;
    return FAILURE(FALSE) ;
  }

  if ( !pdf_register_execution_context_base(&pdfin_xcontext_base) ) {
    pdfin_finish() ;
    return FALSE ;
  }

  return TRUE ;
}


/** pdf_finish -- clean up. */
static void pdfin_finish(void)
{
  pdf_rr_finish();
  pdf_irrc_finish();
  mps_root_destroy(pdfparams_root);
}


/** The following is ok as a global in that it is
   'read-only'.  It is used to initialise the 'methods'
   in the execution context when the latter is created. */
PDF_METHODS pdf_input_methods = {
  pdf_in_begin_execution_context ,
  pdf_in_end_execution_context ,
  pdf_in_purge_execution_context ,

  pdf_in_begin_marking_context ,
  pdf_in_end_marking_context ,
  NULL ,  NULL ,

  pdf_xrefobject ,
  pdf_xrefstreamobj ,
  pdf_seek_to_compressedxrefobj,
  pdf_in_scan_context ,
  pdf_in_scan_marking_context
} ;


/** pdf_in_scan_context - scan input context
 *
 * Only scan the fields that come from the PS parameters. */
static mps_res_t pdf_in_scan_context( mps_ss_t ss, PDFXCONTEXT *pdfxc )
{
  mps_res_t res;
  PDF_IXC_PARAMS *ixc;

  HQASSERT( pdfxc , "pdfxc NULL in pdf_in_scan_context." );
  ixc = ( PDF_IXC_PARAMS * )pdfxc->u.i;

  MPS_SCAN_BEGIN( ss )
    MPS_RETAIN( &ixc->pagerange, TRUE );
    MPS_RETAIN( &ixc->ownerpasswords_local, TRUE );
    MPS_RETAIN( &ixc->userpasswords_local, TRUE );
    MPS_RETAIN( &ixc->ownerpasswords_global, TRUE );
    MPS_RETAIN( &ixc->userpasswords_global, TRUE );
    MPS_RETAIN( &ixc->private_key_files_local, TRUE );
    MPS_RETAIN( &ixc->private_key_passwords_local, TRUE );

    MPS_RETAIN( &ixc->private_key_files_global, TRUE );
    MPS_RETAIN( &ixc->private_key_passwords_global, TRUE );
    MPS_RETAIN( &ixc->OptimizedPDFCacheID, TRUE );
    MPS_RETAIN( &ixc->OptimizedPDFSetupID, TRUE );
    MPS_RETAIN( &ixc->pPageRef, TRUE );

    MPS_RETAIN( &ixc->pPageLabelsDict, TRUE );

    /* *passwords_global are from PDFParams, which is a root, so skip them. */
  MPS_SCAN_END( ss );

  res = pdf_scancachedfonts( ss, ixc->cached_fontdetails );
  if ( res != MPS_RES_OK )
    return res;

  res = ps_scan_field( ss, &ixc->pdfroot );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &ixc->pdftrailer );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &ixc->pdfinfo );
  if ( res != MPS_RES_OK )
    return res;
  res = ps_scan_field( ss, &ixc->trailer_encrypt );
  if ( res != MPS_RES_OK )
    return res;
  return ps_scan_field( ss, &ixc->trailer_id );
}



static Bool pdf_in_begin_execution_context( PDFXCONTEXT *pdfxc )
{
  PDFPARAMS *pdfparams;
  PDF_IXC_PARAMS *ixc ;

  HQASSERT( pdfxc , "pdfxc NULL in pdf_in_begin_execution_context." ) ;
  HQASSERT( !pdfxc->u.i , "pdfxc->u.i non-NULL in pdf_in_begin_execution_context." ) ;

  pdfparams = pdfxc->corecontext->pdfparams;

  /* Load some page device defaults for use with PDF */
  pdf_getpagedevice( pdfxc ) ;

  ixc = ( PDF_IXC_PARAMS * )mm_alloc( pdfxc->mm_structure_pool ,
                                      sizeof( PDF_IXC_PARAMS ) ,
                                      MM_ALLOC_CLASS_PDF_CONTEXT ) ;

  if ( !ixc )
    return error_handler( VMERROR ) ;

  /* Clear the structure (NULL any pointers, integer values, etc). */
  HqMemZero(ixc, sizeof(PDF_IXC_PARAMS));

  pdfxc->u.i = ixc ;

  ixc->majorversion = 0 ;
  ixc->minorversion = 0 ;
  ixc->pdfXversion = 0 ;

  Hq32x2FromInt32( &ixc->pdfxref, 0 ) ;
  ixc->pdfroot =
    ixc->pdftrailer =
    ixc->pdfinfo =
    ixc->trailer_encrypt =
    ixc->trailer_id = onull ; /* Struct copy to set slot properties */

  ixc->gotinfo = FALSE ;
  ixc->infoTrapped = INFO_TRAPPED_NONE;
  Hq32x2FromInt32( &ixc->trailer_prev, 0 ) ;
  Hq32x2FromInt32( &ixc->trailer_xrefstm, 0 ) ;
  ixc->oc_props = NULL ;
  ixc->tmp_file_used = FALSE ;

  /* Copy PDF parameters from current pdfparams
   * - defaults defined in pdf_init()
   */
  ixc->encapsulated = FALSE ;
  ixc->ignore_setpagedevice = FALSE ;
  ixc->ignore_showpage = FALSE ;
  ixc->strictpdf = pdfparams->Strict ;

  HQASSERT( oType(pdfparams->PageRange) == OARRAY ||
            oType(pdfparams->PageRange) == OPACKEDARRAY,
            "PageRange is not an array in pdf_in_begin_execution_context" ) ;
  /* If PageRange is an empty array, then use NULL */
  if ( theLen( pdfparams->PageRange ) == 0 )
    ixc->pagerange = NULL ; /* Print all pages */
  else
    ixc->pagerange = & pdfparams->PageRange ;

  ixc->ownerpasswords_local = NULL ;
  ixc->userpasswords_local = NULL ;

  HQASSERT( oType(pdfparams->OwnerPasswords) == OSTRING ||
            oType(pdfparams->OwnerPasswords) == OARRAY  ||
            oType(pdfparams->OwnerPasswords) == OPACKEDARRAY,
            "OwnerPasswords is not a string or array in pdf_in_begin_execution_context" ) ;
  ixc->ownerpasswords_global = & pdfparams->OwnerPasswords ;

  HQASSERT( oType(pdfparams->UserPasswords) == OSTRING ||
            oType(pdfparams->UserPasswords) == OARRAY  ||
            oType(pdfparams->UserPasswords) == OPACKEDARRAY,
            "UserPasswords is not a string or array in pdf_in_begin_execution_context" ) ;
  ixc->userpasswords_global = & pdfparams->UserPasswords ;

  ixc->private_key_files_local = NULL ;
  ixc->private_key_passwords_local = NULL ;

  HQASSERT( oType(pdfparams->PrivateKeyFiles) == OSTRING ||
            oType(pdfparams->PrivateKeyFiles) == OARRAY  ||
            oType(pdfparams->PrivateKeyFiles) == OPACKEDARRAY,
            "PrivateKeyFiles is not a string or array in pdf_in_begin_execution_context" ) ;
  ixc->private_key_files_global = & pdfparams->PrivateKeyFiles ;

  HQASSERT( oType(pdfparams->PrivateKeyPasswords) == OSTRING ||
            oType(pdfparams->PrivateKeyPasswords) == OARRAY  ||
            oType(pdfparams->PrivateKeyPasswords) == OPACKEDARRAY,
            "PrivateKeyPasswords is not a string or array in pdf_in_begin_execution_context" ) ;
  ixc->private_key_passwords_global = & pdfparams->PrivateKeyPasswords ;

#if defined( DEBUG_BUILD )
  ixc->honor_print_permission_flag = pdfparams->HonorPrintPermissionFlag ;
#endif

  ixc->missing_fonts = pdfparams->MissingFonts ;
  ixc->pagecropto = pdfparams->PageCropTo ;
  ixc->EnforcePDFVersion = pdfparams->EnforcePDFVersion;
  ixc->abort_for_invalid_types = pdfparams->AbortForInvalidTypes ;

  /* Copy annotation parameters */
  ixc->PrintAnnotations = pdfparams->PrintAnnotations;
  ixc->AnnotParams = pdfparams->AnnotationParams;

  ixc->ErrorOnFlateChecksumFailure = pdfparams->ErrorOnFlateChecksumFailure ;
  ixc->IgnorePSXObjects = pdfparams->IgnorePSXObjects;
  ixc->WarnSkippedPages = pdfparams->WarnSkippedPages;

  ixc->SizePageToBoundingBox = pdfparams->SizePageToBoundingBox;
  ixc->OptionalContentOptions = pdfparams->OptionalContentOptions;

  ixc->EnableOptimizedPDFScan = pdfparams->EnableOptimizedPDFScan;
  ixc->OptimizedPDFScanLimitPercent = pdfparams->OptimizedPDFScanLimitPercent;
  ixc->OptimizedPDFExternal = pdfparams->OptimizedPDFExternal;
  ixc->OptimizedPDFCacheSize = pdfparams->OptimizedPDFCacheSize;
  ixc->OptimizedPDFCacheID = & pdfparams->OptimizedPDFCacheID;
  ixc->OptimizedPDFSetupID = & pdfparams->OptimizedPDFSetupID;
  ixc->OptimizedPDFCrossXObjectBoundaries = pdfparams->OptimizedPDFCrossXObjectBoundaries ;
  ixc->OptimizedPDFSignificanceMask = pdfparams->OptimizedPDFSignificanceMask ;
  ixc->OptimizedPDFScanWindow = pdfparams->OptimizedPDFScanWindow ;
  ixc->OptimizedPDFImageThreshold = pdfparams->OptimizedPDFImageThreshold ;
  ixc->ErrorOnPDFRepair = pdfparams->ErrorOnPDFRepair;
  ixc->PDFXVerifyExternalProfileCheckSums = pdfparams->PDFXVerifyExternalProfileCheckSums;
  ixc->TextStrokeAdjust = pdfparams->TextStrokeAdjust;
  ixc->XRefCacheLifetime = pdfparams->XRefCacheLifetime;

  ixc->conformance_pdf_version = 0 ;
  ixc->suppress_duplicate_warnings = FALSE ;
  ixc->icc_cpc = NULL ;
  ixc->icc_cpc_length = 0 ;

  /* Initialise values returned by pdfxstatus operator */
  ixc->PDFXVersionClaimed =
    ixc->PDFXVersionTreated =
    ixc->PDFXVersionValid =
    ixc->PDFXOutputCondition =
    ixc->PDFXRegistryProfile = onull; /* Struct copy to set slot properties */

  ixc->PDFXOutputProfilePresent = FALSE;

  ixc->pageno = 0 ;
  ixc->pageFound = FALSE;
  ixc->pPageRef = NULL;
  ixc->pPageLabelsDict = NULL;
  ixc->repaired = FALSE ;
  ixc->cached_fontdetails = NULL ;

  /* Set defaults for AcroForm parameters */
  ixc->AcroForm.NeedAppearances = FALSE;
  ixc->AcroForm.pDefAppear = NULL;
  ixc->AcroForm.pDefRsrcs  = NULL;
  ixc->AcroForm.Quadding   = ACROFORM_ATTR_UNDEFINED;

  ixc->pdfxState.usingBleedBoxInViewerPrefs = FALSE;
  ixc->pdfxState.conditionDeviceSpace = SPACE_notset;

  /* PJTF parameters */
  ixc->pPJTFinfo = NULL;

  ixc->pPageStartGState = NULL ;

  ixc->rr_state = NULL ;
  ixc->page_continue = TRUE ;
  ixc->page_discard = FALSE ;

  /* This is a bit of a pain. Whilst pdfparams are (at least for
   * the moment) input-specific, the stream creation code is in the
   * base compound. So ErrorOnFlateChecksumFailure has to go into
   * the top-level context. Not ideal.
   */
  pdfxc->ErrorOnFlateChecksumFailure = ixc->ErrorOnFlateChecksumFailure ;

  return TRUE ;
}


static Bool pdf_in_end_execution_context( PDFXCONTEXT *pdfxc )
{
  PDF_IXC_PARAMS *ixc ;
  Bool result = TRUE ;

  HQASSERT( pdfxc , "pdfxc NULL in pdf_in_end_execution_context." ) ;

  ixc = ( PDF_IXC_PARAMS * )pdfxc->u.i ;

  if ( ixc != NULL ) {
    result = result && pdf_rr_end( pdfxc ) ;

    /* Free PDF/X compound objects. */
    pdf_freeobject_from_xc(pdfxc, &ixc->PDFXVersionClaimed);
    pdf_freeobject_from_xc(pdfxc, &ixc->PDFXOutputCondition);

    if ( ixc->oc_props != NULL ) {
      pdf_oc_freedata(pdfxc->mm_structure_pool,ixc->oc_props);
    }

    if ( ixc->pPJTFinfo != NULL ) {
      mm_free( pdfxc->mm_structure_pool,
               ( mm_addr_t )( ixc->pPJTFinfo ) ,
               sizeof(PJTFinfo) );
    }

    if ( ixc->icc_cpc != NULL ) {
      mm_free( pdfxc->mm_structure_pool ,
               ( mm_addr_t )( ixc->icc_cpc ) ,
               ixc->icc_cpc_length ) ;
    }

    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )( ixc ) ,
             sizeof( PDF_IXC_PARAMS )) ;
  }

  /* Reset the overall file position to what it was in
     pdf_open(). Must be done this late because
     pdf_in_end_marking_context() deliberately seeks to the end. This
     allows us to call pdfexec multiple times on the same file object,
     even if the correct place to start isn't at offset zero for
     whatever reason. */

  if ( result && isIOpenFile( pdfxc->flptr )) {
    FILELIST *flptr = pdfxc->flptr ;

    if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
      result = (*theIFileLastError( flptr ))( flptr ) ;

    if ( result &&
         (*theIMySetFilePos( flptr ))( flptr , &pdfxc->filepos ) == EOF )
      result = (*theIFileLastError( flptr ))( flptr ) ;
  }

  pdfxc->u.i = NULL ;

  return result ;
}


static Bool pdf_in_purge_execution_context(PDFXCONTEXT *pdfxc, int32 savelevel)
{
  /* Flush font cache entries which reference objects being restored away. */
  pdf_flushfontdetails( pdfxc, savelevel ) ;

  /* Each time around the server loop, we need to check for any PDF
     execution contexts left hanging around (this can happen if a
     pdfexec callback suspended normal execution, e.g. for a JTF), and
     add them to the pdfxcontext purge queue.*/
  if ( NUMBERSAVES( savelevel ) <= 1 ) {
    if ( ! pdf_end_execution_context( pdfxc , & pdfin_xcontext_base )) {
      return FALSE ;
    }
  }

  return TRUE ;
}


/** pdf_in_scan_marking_context - scan input marking context */
static mps_res_t pdf_in_scan_marking_context(mps_ss_t ss, PDFCONTEXT *pdfc)
{
  mps_res_t res;
  PDF_IMC_PARAMS *imc;

  HQASSERT( pdfc , "pdfc NULL in pdf_in_scan_marking_context." );
  imc = ( PDF_IMC_PARAMS * )pdfc->u.i;
  if ( imc == NULL )
    return MPS_RES_OK;

  /* The stack points to PS VM. */
  res = ps_scan_stack( ss, &imc->pdfstack );
  if ( res != MPS_RES_OK )
    return res;

  /* So does the pattern cache dictionary. */
  res = ps_scan_field( ss , &imc->patterncache );
  return res;
}


static Bool pdf_in_begin_marking_context( PDFXCONTEXT *pdfxc , PDFCONTEXT *pdfc )
{
  int32 i ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;

  HQASSERT( pdfxc , "pdfxc NULL in pdf_init_imc_params." ) ;
  HQASSERT( !pdfc->u.i , "pdfc->u.i non-NULL in pdf_init_imc_params." ) ;

  imc = ( PDF_IMC_PARAMS * )mm_alloc( pdfxc->mm_structure_pool ,
                                      sizeof( PDF_IMC_PARAMS ) ,
                                      MM_ALLOC_CLASS_PDF_CONTEXT ) ;

  if ( !imc )
    return error_handler( VMERROR ) ;

  /* Clear the structure (NULL any pointers, integer values, etc). */
  HqMemZero(imc, sizeof(PDF_IMC_PARAMS));

  pdfc->u.i = imc ;

  imc->pdfstack.size = EMPTY_STACK ;
  imc->pdfstack.fptr = ( & imc->pdfstackframe ) ;
  imc->pdfstack.limit = PDFIN_MAX_STACK_SIZE ;
  imc->pdfstack.type = STACK_TYPE_OPERAND ;
  imc->pdfstackframe.link = NULL ;

  MATRIX_COPY(&imc->defaultCTM, &thegsPageCTM(*gstateptr)) ;

  imc->pdfclipmode = PDF_NO_CLIP ;
  imc->seenclosepath = FALSE;
  imc->pdf_graphicstack = NULL ;
  imc->pdf_inlineopbase = 0;

  imc->pFieldRect = NULL;
  imc->pFieldValues = NULL;

  /* Text state doesn't need explicit initialisation here since BT does that. */

  imc->bxex = 0 ;
  for ( i = 0 ; i < BXEX_SEQ_MAX ; i++ )
    imc->bxex_seq[ i ] = 0 ;

  /* Initialise text state counter in case there are nested BT/ET. */
  imc->textstate_count = 0 ;

  imc->handlingImage = FALSE ;

  /* marked context stack (for optional content "OC") */
  imc->mc_stack = NULL;
  imc->mc_oc_initial_state = optional_content_on;
  imc->mc_DP_oc_status = TRUE;

  /* The pattern cache dictionary is in PSVM, which means it's safe
     for the pattern instances created by gs_makepattern to live
     there, and as a bonus it is allowed to expand - would not be so
     if it was in PDF memory. */
  if ( ! ps_dictionary(&imc->patterncache, 32) ) {
    return FALSE ;
  }

  return TRUE ;
}


/** Dispose of the marking context input params of the given context. */
static Bool pdf_in_end_marking_context( PDFCONTEXT *pdfc )
{
  DEVICELIST *dev ;
  FILELIST *flptr ;
  PDFXCONTEXT *pdfxc ;
  PDF_IMC_PARAMS *imc ;
  PDF_IXC_PARAMS *ixc ;
  Bool result = TRUE ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* If this is the bottom-most marking context then we have extra
   * work to do in tidying up prior to ending the whole execution
   * context. This code was moved here from the old pdf_close()
   * function so that it is always called, even if we're not taking
   * the normal route out of PDF - for instance when there's been an
   * error after a call to pdfopen_() with no stopped context
   * providing an explicit pdfclose_(). See bug 50574 for details.
   */

  if ( pdfc->mc == 0 ) {
    /* These is here in case we get an error; normally this is
     * flushed after the restore that we do around each page.
     */
    pdf_flushfontdetails( pdfxc, -1 ) ;

    /* When the last marking context with mc == 1 closes, any
     * resource stragglers should have been accounted for.
     */
    HQASSERT( pdfc->pdfenv == NULL , "Resources not all flushed in pdf_close" ) ;

    /* All xref cache entries should be freeable by now. */
    (void) pdf_sweepxref( pdfc, TRUE /* xrefcache is closing */, -1) ;

    /* Purge the objects in the xref location cache. */
    pdf_flushxrefsec( pdfc ) ;

#if defined( ASSERT_BUILD )
    /* If there are still xref cache entries hanging around, I've done
     * something wrong. */
    if ( ixc->XRefCacheLifetime == 1 ) {
      int32 i ;

      for ( i = 0 ; i < XREF_CACHE_SIZE ; i++ )
        HQASSERT( pdfxc->xrefcache[ i ] == NULL ,
                  "Lingering xref cache entry after final mark'n'sweep!" ) ;
    }
#endif

    /* Restore file positions and free PDF_FILERESTORE_LIST entries.
     * This must be done before the FILELISTs are freed when the
     * streams are flushed below.
     */
    result = pdf_restorestreams( pdfc, result ) ;

    /* Purge the memory to store the cached streams. This must be done
     * after the XREF cache is swept since the FILELIST structure of
     * a stream is accessed to free memory for the Parameter Dictionary.
     */
    pdf_flushstreams( pdfc ) ;

    /* Free the memory for the trailer dictionary */
    pdf_freeobject( pdfc , & ixc->pdftrailer ) ;

    /* Clear up the input stream. */
    flptr = pdfxc->flptr ;
    HQASSERT( flptr , "flptr field NULL in pdf_close" ) ;

    /* Delete the temp file if we created one */
    if ( ixc->tmp_file_used ) {
      if ( ! pdf_close_tmp_file( pdfc, pdfxc->flptr ) )
        result = FALSE ;
    }
    else {
      /* Reset the input stream back to the end. */
      /* Go back to PS streams to continue with file... */
      /* NB: device may not exist due to failure in pdf_open */
      dev = theIDeviceList( flptr ) ;
      if ( dev != NULL && isIOpenFile( flptr )) {
        if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
          result = (*theIFileLastError( flptr ))( flptr ) ;

        if ( ! (*theISeekFile( dev ))( dev , theIDescriptor( flptr ) ,
                                       & pdfxc->fileend , SW_SET ))
          result = device_error_handler( dev ) ;
      }
    }
  }

  imc = pdfc->u.i ;

  if ( imc ) {
    HQASSERT(imc->textstate_count == 0, "Text states not cleaned up") ;

    pdf_flush_gstates( pdfc ) ;

    pdf_mc_freeall(imc, pdfxc->mm_structure_pool);

    mm_free( pdfxc->mm_structure_pool ,
             ( mm_addr_t )( imc ) ,
             sizeof( PDF_IMC_PARAMS )) ;
  }

  pdfc->u.i = NULL ;

  return result ;
}

/** Check when creating filter for PDF/X that correct decompression is used.
   In PostScript, we leave objects on the stack when an error occurs. In this
   case we remove the underlying file object from the stack. */
Bool pdf_x_filter_preflight( FILELIST *flptr, OBJECT *args, STACK *stack )
{
  UNUSED_PARAM(OBJECT *, args) ;

  HQASSERT(flptr, "No file for PDF filter creation check") ;

  if ( theIPDFContextID(flptr) > 0 && isIInputFile(flptr) ) {
    PDFXCONTEXT *pdfxc ;

    HQASSERT(args != NULL, "No argument dictionary to PDF filter") ;
    HQASSERT(oType(*args) == ODICTIONARY || oType(*args) == ONULL,
             "Argument is not dictionary or null") ;
    HQASSERT(stack != NULL, "No stack for PDF filter") ;

    if ( !pdf_find_execution_context(theIPDFContextID(flptr),
                                     pdfin_xcontext_base,
                                     &pdfxc) ) {
      pop(stack) ;
      return error_handler(UNDEFINED) ;
    }

    return pdfxCheckFilter(pdfxc->pdfc, flptr, stack);
  }

  return TRUE ;
}

Bool pdf_getStrictpdf(PDFXCONTEXT *pdfxc)
{
  PDF_IXC_PARAMS  *ixc ;

  PDF_GET_IXC( ixc ) ;

  return ixc->strictpdf ;
}

static void init_C_globals_pdfin(void)
{
#if defined( DEBUG_BUILD )
  pdf_debug = 0;
#endif
  pdfparams_root = NULL ;
  pdfin_xcontext_base = NULL ;
#ifdef METRICS_BUILD
  pdfin_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&pdfin_metrics_hook) ;
#endif
}

/* Declare global init functions here to avoid header inclusion
   nightmare. */
IMPORT_INIT_C_GLOBALS( pdfannot )
IMPORT_INIT_C_GLOBALS( pdfexec )
IMPORT_INIT_C_GLOBALS( pdffont )
IMPORT_INIT_C_GLOBALS( pdfops )
IMPORT_INIT_C_GLOBALS( pdfrepr )
IMPORT_INIT_C_GLOBALS( pdfrr )

void pdfin_C_globals(core_init_fns *fns)
{
  init_C_globals_pdfannot() ;
  init_C_globals_pdfexec() ;
  init_C_globals_pdffont() ;
  init_C_globals_pdfin() ;
  init_C_globals_pdfops() ;
  init_C_globals_pdfrepr() ;
  init_C_globals_pdfrr() ;

  fns->swstart = pdfin_swstart ;
  fns->finish = pdfin_finish ;
}

/* Log stripped */
