/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfexec.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of PDF exec and friend operators.
 */

#include "core.h"

#include "chartype.h"           /* IsWhiteSpace */
#include "control.h"            /* interpreter */
#include "devices.h"            /* device_error_handler */
#include "dicthash.h"           /* fast_insert_hash */
#include "execops.h"            /* setup_pending_exec */
#include "fileio.h"             /* FILELIST */
#include "functns.h"            /* fn_evaluate */
#include "gsc_icc.h"            /* gsc_purgeInactiveICCProfileInfo() */
#include "gstack.h"             /* gs_gpush */
#include "gu_ctm.h"             /* gs_translatectm */
#include "gu_rect.h"            /* cliprectangles */
#include "hqmemcmp.h"           /* HqMemCmp */
#include "hqunicode.h"          /* utf8_buffer */
#include "miscops.h"            /* run_ps_string */
#include "mm.h"                 /* mm_alloc */
#include "mm_core.h"            /* gc_safe_in_this_operator */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "params.h"             /* UserParams */
#include "psvm.h"               /* save_() */
#include "rcbcntrl.h"           /* rcbn_enabled */
#include "saves.h"              /* save_level */
#include "spdetect.h"           /* detect_setsystemparam_separation */
#include "swcopyf.h"            /* swcopyf */
#include "swerrors.h"           /* INVALIDACCESS */
#include "swmemory.h"           /* gs_cleargstates */
#include "progupdt.h"           /* end_file_progress */
#include "riptimeline.h"        /* CHECK_TL_VALID */
#include "timing.h"             /* SW_TRACE_* */

#include "swpdf.h"              /* PDFXCONTEXT */
#include "pdfmatch.h"           /* pdf_dictmatch */
#include "pdfres.h"             /* pdf_add_resource */
#include "pdfcntxt.h"           /* pdf_begin_execution_context */
#include "pdfxref.h"            /* pdf_lookupxref */
#include "swpdfin.h"            /* pdfin_xcontext_base */
#include "swpdfout.h"           /* PDFOUT_ENABLED */

#include "pdfannot.h"           /* pdf_do_annots */
#include "pdfattrs.h"           /* PDF_PAGEDEV */
#include "pdfcolor.h"           /* pdf_mapBlendSpace */
#include "pdfdefs.h"            /* PDF_PAGECROPTO_CROPBOX */
#include "pdffont.h"            /* PDF_FONTDETAILS */
#include "pdfin.h"              /* pdf_input_methods */
#include "pdfjtf.h"             /* pdf_jt_get_trapinfo */
#include "pdflabel.h"           /* pdf_make_page_label */
#include "pdfmem.h"             /* pdf_freeobject */
#include "pdfMetadata.h"        /* pdfMetadataParse */
#include "pdfncryp.h"           /* pdf_end_decryption */
#include "pdfops.h"             /* pdf_execops */
#include "pdfrepr.h"            /* pdf_repair */
#include "pdfrr.h"              /* pdf_rr_begin */
#include "pdfscan.h"            /* pdf_readobject */
#include "pdfstrobj.h"          /* streaminfo */
#include "pdftxt.h"             /* pdf_inittextstate */
#include "pdftextstr.h"         /* pdf_text_to_utf8 */
#include "pdfx.h"               /* pdfxCheckForPageGroupCSOverride */
#include "streamd.h"            /* streamLookupDict */

#include "pdfexec.h"


/** PDF_SEPARATION_INFO contains relevant bits from SeparationInfo */
typedef struct {
  NAMECACHE *device_colorant;
  OBJECT *color_space;
  OBJECT *pages;
} PDF_SEPARATION_INFO;

/** PDF_SEPARATIONS_CONTROL: stores control data for handling separations
 * when recombining. It has no effect when not recombining.
 * We take each page from the job in the same order as we would without
 * recombine. For each page we compare the SeparationInfo/Pages object for
 * the current page with 'previous_pages'. If they are the same then we
 * consider them part of the same logical page (by calling recombineshowpage_)
 * and continue recombining. Otherwise we render the current logical page and
 * start a new recombined page.
 *
 * The above would be lovely if jobs conformed to the spec. Unfortunately,
 * distiller can produce preseparated jobs with just the one (the current) page
 * in a SeparationInfo/Pages array. This means that it is impossible to
 * distinguish in general a job produced by distiller with a legal job that
 * deliberately only has one colorant on the current page. BUT, this problem
 * will only appear if we are recombining a job with a) a single colorant on
 * two neighbouring pages, and b) they are different colorants, eg. Brown & Gold.
 * So, given the prevalence of Distiller based jobs and the almost non-existence
 * of jobs containing pages of a single colorant we will attempt to recombine
 * distiller jobs in preference using a heuristic to cope with the above
 * conditions. One common case is for a job to have mostly black only pages along
 * with some colored pages. We will handle that case as well as we ever have done.
 * Yuck, it would have been better if Distiller didn't emit the array.
 *
 * When a page range is requested in pdfparams, we base the page numbering
 * on the logical page number and not the page numbering in the job that
 * we would use without recombine. To aid that, we have a 'printPage' attribute
 * that is set for all job pages in the current logical page.
 *
 * A certain amount of verification is also done on SeparationInfo. We verify
 * that the current page is contained in SeparationInfo/Pages. For that, we
 * have a 'page' attribute that holds a copy of the original indirect object
 * for the page. We also verifiy the SeparationInfo/ColorSpace & DeviceColorant.
 * If a problem is found on any page in the job, then 'separationinfo_dubious'
 * is set and a warning issued at job end.
 *
 * If SeparationInfo is missing for any page, 'separationinfo_missing' is set
 * and we have to fall back on the recombine heuristics to throw pages at the
 * correct points. Also, the page numbering will be the same as without recombine
 * so page ranges will not follow the logical pages. A warning is issued if a
 * page range is requested and separationinfo_missing is true.
 */
struct PDF_SEPARATIONS_CONTROL {
  OBJECT *previous_pages;         /* Points to SeparationInfo/Pages for previous page */
  Bool printPage;                 /* False if skipping the current logical page */
  OBJECT page;                    /* Holds the indirect object for the current leaf node */
  Bool separationinfo_missing;    /* True if SeparationInfo is absent on any page */
  Bool separationinfo_dubious;    /* True if a any SeparationInfo fails validation */
  NAMECACHE *previous_colorant;   /* The name of the colorant on the previous page */
} ;

#if defined( ASSERT_BUILD )
static Bool pdftrace_separation_info = FALSE ;
#endif

static Bool pdf_open( PDFCONTEXT *pdfc , FILELIST **flptr ) ;
static Bool pdf_read_header( PDFCONTEXT *pdfc ) ;
static Bool pdf_find_trailer( PDFCONTEXT *pdfc ) ;
static Bool pdf_read_trailer( PDFCONTEXT *pdfc , Bool *repairable ) ;
static Bool pdf_read_xref( PDFCONTEXT *pdfc , Hq32x2 pdfxref, Bool * stream ) ;
static Bool pdf_walk_contents( PDFCONTEXT *pdfc , OBJECT *page ) ;
static Bool pdf_walk_separation_info( PDFCONTEXT *pdfc , OBJECT *theo ,
                                       PDF_SEPARATION_INFO *pdfsi ) ;
static Bool pdf_walk_tree( PDFCONTEXT *pdfc ) ;

static Bool pdf_unpack_params( PDFCONTEXT *pdfc , OBJECT *thedict ) ;
static Bool pdf_pageref_required( PDFCONTEXT *pdfc, Bool *dopage ) ;

static Bool pdfopen_internal(PDFCONTEXT **new_pdfc, corecontext_t *corecontext) ;

static Bool pdf_file_is_seekable( FILELIST *flptr ) ;
static Bool pdf_copy_to_tmp_file( FILELIST *flptri , FILELIST *flptro ,
                                   Hq32x2 *nbytes ) ;
static Bool pdf_open_tmp_file( PDFCONTEXT *pdfc , OBJECT *fileobj );
static Bool pdf_walk_infodict( PDFCONTEXT *pdfc ) ;

static Bool pdf_check_separation_info(PDF_SEPARATION_INFO*  pdfsi,
                                      OBJECT *page ) ;

#define XREF_SIZE     20


#if defined( ASSERT_BUILD )
static Bool debug_xrefscan = FALSE ;
#endif

/* ---------------------------------------------------------------------- */
static Bool pdfopen_internal(PDFCONTEXT **new_pdfc, corecontext_t *corecontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *o1 , *o2 ;
  FILELIST *flptr ;
  PDFXCONTEXT *pdfxc = NULL ;
  PDFCONTEXT *pdfc ;
  Bool repairable = TRUE ;
  Bool result;

  if ( ! corecontext->systemparams->PDF )
    return error_handler( INVALIDACCESS ) ;

  /* Arguments: -file- << ... >> pdfopen or pdfexec */

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  type = oType(*o1) ;
  if ( type != OFILE )
    return error_handler( TYPECHECK ) ;

  type = oType(*o2) ;
  if ( type != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  flptr = oFile( *o1 ) ;
  if ( ! isIOpenFileFilter( o1 , flptr ) ||
       ! isIInputFile( flptr ) ||
       isIEof( flptr ))
    return error_handler( IOERROR ) ;

  if ( !pdf_begin_execution_context( & pdfxc , & pdfin_xcontext_base ,
                                     & pdf_input_methods, corecontext) )
    return FALSE;

  pdfc = pdfxc->pdfc ;
  *new_pdfc = pdfc ;
  pdfxc->flptr = flptr ;
  corecontext->pdfin_h = pdfc ;
  /* Don't use flptr from stack object after this point as pdf_open may replace
   * the context flptr */

  if (! ( pdf_open( pdfc, &pdfxc->flptr ) &&
          pdf_unpack_params( pdfc , o2 )) ) {
    ( void )pdf_end_execution_context( pdfxc , & pdfin_xcontext_base ) ;
    return FALSE;
  }

  /* Call pdf_rr_begin after a call to pdf_unpack_params,
   * so that the encapsulated flag is set */
  if ( ! pdf_rr_begin( pdfxc )) {
    ( void )pdf_end_execution_context( pdfxc , & pdfin_xcontext_base ) ;
    return FALSE ;
  }

  result = pdf_read_header( pdfc ) &&
           pdf_find_trailer( pdfc ) &&
           pdf_read_trailer( pdfc , & repairable ) ;

  if (! result && isIOpenFile(pdfxc->flptr) && repairable) {
    /* repairable is false when pdf_begin_decryption fails.
       In this case there is no point trying to repair the job. */
    result = pdf_repair( pdfc , pdfxc->flptr ) ;
    if ( result )
      error_clear_context(corecontext->error);
  }

  if (! result ) {
    ( void )pdf_end_execution_context( pdfxc , & pdfin_xcontext_base ) ;
    return FALSE;
  }

  result = pdf_walk_infodict( pdfc ) ;

  if (! result ) {
    pdf_end_decryption( pdfc ) ;
    ( void )pdf_end_execution_context( pdfxc , & pdfin_xcontext_base ) ;
    return FALSE;
  }

  npop( 2 , & operandstack ) ;

  return TRUE ;
}

/* See header for doc. */
Bool pdf_close_internal( PDFCONTEXT *pdfc )
{
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  HQASSERT( pdfc->mc == 0 ,
            "Marking context nesting count should be 0 by now" ) ;

  pdf_end_decryption( pdfc ) ;

  /* purge the ICC cache */
  gsc_purgeInactiveICCProfileInfo(frontEndColorState);

  return pdf_end_execution_context( pdfxc , & pdfin_xcontext_base ) ;
}

Bool pdfopen_(ps_context_t *pscontext)
{
  PDFCONTEXT *pdfc ;
  PDFXCONTEXT *pdfxc ;

  if ( ! pdfopen_internal(&pdfc, ps_core_context(pscontext)) )
    return FALSE ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  return stack_push_integer(pdfxc->id, &operandstack) ;
}

Bool pdfexecid_(ps_context_t *pscontext)
{
  SYSTEMPARAMS *systemparams = ps_core_context(pscontext)->systemparams;
  int32 ssize ;
  int32 type ;
  OBJECT *o1 , *o2 ;
  int32 id ;
  PDFXCONTEXT *pdfxc = NULL ;
  PDF_IXC_PARAMS *ixc ;

  gc_safe_in_this_operator();

  if ( ! systemparams->PDF )
    return error_handler( INVALIDACCESS ) ;

  /* Arguments: -context_id- << ... >> pdfexecid */
  /* Any args supplied in the dict replace those given to pdfopen */

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o2 = TopStack( operandstack , ssize ) ;
  o1 = ( & o2[ -1 ] ) ;
  if ( ! fastStackAccess( operandstack ))
    o1 = stackindex( 1 , & operandstack ) ;

  type = oType(*o1) ;
  if ( type != OINTEGER )
    return error_handler( TYPECHECK ) ;

  type = oType(*o2) ;
  if ( type != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  id = oInteger( *o1 ) ;
  if ( ! pdf_find_execution_context( id , pdfin_xcontext_base , & pdfxc ))
    return error_handler( UNDEFINED ) ;

  npop( 2 , & operandstack ) ;

  PDF_GET_IXC( ixc ) ;
  ixc->pageno = 0 ;

  PROBE(SW_TRACE_INTERPRET_PDF, 1,
        pdfxc->error = ( ! pdf_unpack_params( pdfxc->pdfc , o2 ) ||
                         !pdf_walk_tree(pdfxc->pdfc))) ;

  return ( ! pdfxc->error ) ;
}

Bool pdfclose_(ps_context_t *pscontext)
{
  int32 ssize ;
  int32 type ;
  OBJECT *o1 ;
  int32 id ;
  PDFXCONTEXT *pdfxc = NULL ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Arguments: -context_id- pdfclose */

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = TopStack( operandstack , ssize ) ;

  type = oType(*o1) ;
  if ( type != OINTEGER )
    return error_handler( TYPECHECK ) ;

  id = oInteger( *o1 ) ;
  if ( ! pdf_find_execution_context( id , pdfin_xcontext_base , & pdfxc ))
    return error_handler( UNDEFINED ) ;

  pop( & operandstack ) ;

  return pdf_close_internal( pdfxc->pdfc ) ;
}


Bool pdfexec_(ps_context_t *pscontext)
{
  PDFCONTEXT *pdfc ;
  PDFXCONTEXT *pdfxc ;

  gc_safe_in_this_operator();

  if ( !pdfopen_internal(&pdfc, ps_core_context(pscontext)) )
    return FALSE;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  PROBE(SW_TRACE_INTERPRET_PDF, 0,
        pdfxc->error = ! pdf_walk_tree(pdfc)) ;

  /* Must call pdfclose_internal() to ensure resources are freed correctly. */
  return pdf_close_internal( pdfc ) && ! pdfxc->error ;
}

/* See header for doc. */
Bool pdf_open_refd_file( PDFCONTEXT **new_pdfc,
                         PDF_IXC_PARAMS *prior_ixc,
                         FILELIST *pFlist,
                         corecontext_t *corecontext )
{
  Bool result = FALSE ;
  PDFXCONTEXT *pdfxc = NULL ;
  PDF_IXC_PARAMS *ixc ;
  PDFCONTEXT *pdfc ;
  Bool repairable = TRUE ;

  if (! pdf_begin_execution_context(&pdfxc, &pdfin_xcontext_base,
                                    &pdf_input_methods, corecontext) )
    return FALSE;

  pdfc = pdfxc->pdfc;
  *new_pdfc = pdfc;
  pdfxc->flptr = pFlist;
  ixc = pdfxc->u.i;
  corecontext->pdfin_h = pdfc ;
  /* Don't use pFlist after this point as pdf_open may replace the context flptr */

  if (! pdf_open( pdfc, &pdfxc->flptr ) ) {
    ( void )pdf_end_execution_context( pdfxc , & pdfin_xcontext_base ) ;
    return FALSE;
  }

  /* Inherit certain pdf parameters from the prior context (since no pdf
     params dictionary is being passed through). See pdf_unpack_params(). */
#if defined( DEBUG_BUILD )
  ixc->honor_print_permission_flag = prior_ixc->honor_print_permission_flag;
#endif

  /* page range not applicable to refd file */
  ixc->pagerange = NULL;

  ixc->strictpdf = prior_ixc->strictpdf;
  ixc->ownerpasswords_local = prior_ixc->ownerpasswords_local;
  ixc->userpasswords_local  = prior_ixc->userpasswords_local;
  ixc->private_key_files_local = prior_ixc->private_key_files_local;
  ixc->private_key_passwords_local  = prior_ixc->private_key_passwords_local;
  ixc->missing_fonts = prior_ixc->missing_fonts;

  /* Set the file as 'encapsulated' to ensure correct page group behaviour;
     stop a setpagedevice and showpage from being generated. */
  ixc->encapsulated = TRUE;
  ixc->ignore_setpagedevice = ixc->ignore_showpage = TRUE;

  /* Process the referenced PDF file's header and trailer. */
  result = pdf_read_header( pdfc ) &&
           pdf_find_trailer( pdfc ) &&
           pdf_read_trailer( pdfc , & repairable ) ;

  if ( ! result && isIOpenFile(pdfxc->flptr) && repairable ) {
    /* repairable is false when pdf_begin_decryption fails. In this case
     * there is no point trying to repair the job. */
    result = pdf_repair( pdfc , pdfxc->flptr ) ;
  }

  if (! result ) {
    ( void )pdf_end_execution_context( pdfxc , & pdfin_xcontext_base ) ;
    return FALSE;
  }

  if (! pdf_walk_infodict( pdfc ) ) {
    pdf_end_decryption( pdfc ) ;
    ( void )pdf_end_execution_context( pdfxc , & pdfin_xcontext_base ) ;
    return FALSE;
  }

  return TRUE;
}

/* See header for doc. */
Bool pdf_exec_page(PDFCONTEXT *pdfc, OBJECT *pPageRef, Bool *pPageFound)
{
  int32 ret;
  GET_PDFXC_AND_IXC;

  /* Copy required page reference to the ixc.  pdf_walk_tree/pages etc. will
     use it to ensure only the required page is processed (if found at all). */
  ixc->pPageRef = pPageRef;

  /* Walk the page hierarchy (but only taking action on the required page). */
  PROBE(SW_TRACE_INTERPRET_PDF, 2,
        ret = pdf_walk_tree(pdfc));

  /* Return whether or not the required page was located. */
  *pPageFound = ixc->pageFound;

  return ret;
}

/* Stream dictionary */
enum {
  stream_dict_Resources, stream_dict_Length, stream_dict_n_entries
} ;
static NAMETYPEMATCH stream_dict[stream_dict_n_entries + 1] = {
  { NAME_Resources | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
  { NAME_Length               ,  2, { OINTEGER, OINDIRECT }},
  DUMMY_END_MATCH
} ;

/* ---------------------------------------------------------------------- */
Bool pdf_exec_stream(struct OBJECT *stream, int stream_type)
{
  SYSTEMPARAMS *systemparams = get_core_context_interp()->systemparams;
  PDFXCONTEXT *pdfxc = NULL ;
  PDFCONTEXT *pdfc = NULL ;
  PDF_IXC_PARAMS *ixc ;
  FILELIST *flptr ;
  Bool result = FALSE ;
  OBJECT *resource ;
  Hq32x2 filepos;
  Bool NZstream = TRUE;

  HQASSERT( stream , "stream NULL in pdf_exec_stream" ) ;

  if ( ! systemparams->PDF )
    return error_handler( INVALIDACCESS ) ;

  if (oType(*stream) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile( *stream ) ;

  HQASSERT( flptr , "flptr NULL in pdf_exec_stream" ) ;
   if ( ! isIOpenFileFilter( stream , flptr ) ||
        ! isIInputFile( flptr ))
      return detail_error_handler(IOERROR, "Error reading PDF input stream.") ;

  Hq32x2FromInt32(&filepos, 0);
  if ((*theIMyResetFile( flptr ))( flptr ) == EOF ||
      (*theIMySetFilePos( flptr ))( flptr , &filepos ) == EOF )
    return detail_error_handler(IOERROR, "Error rewinding PDF input stream.") ;

  if ( ! pdf_find_execution_context( theIPDFContextID( flptr ) ,
                                     pdfin_xcontext_base ,
                                     & pdfxc )) {
    HQFAIL( "Couldn't find context in pdf_exec_stream" ) ;
    return FALSE ;
  }

  PDF_GET_IXC( ixc ) ;

  /* Get the resource dictionary. Don't resolve any indirect
   * references since we want to mark the resource dictionary
   * with the correct marking context so that the dictionaries
   * do not build up in the XREF cache. */

  if ( ! dictmatch( streamLookupDict( stream ), stream_dict ))
    return FALSE ;

  resource = stream_dict[stream_dict_Resources].result ;

  NZstream = (oInteger( *(stream_dict[stream_dict_Length].result) )  != 0);

  result = pdf_begin_marking_context( pdfxc, &pdfc, resource , stream_type );
  if (result) {
    /* Will add the resources if there are any explicitly included in the
     * stream dictionary. In other circumstances (like Type 3 fonts)
     * the caller will have to set the resources up, since the
     * Resources dictionary belongs to the font, not each individual
     * PDF stream.
     */
    pdfc->contents = stream ;

    /*
     * Don't bother executing pdf contents if it comes from a zero-length
     * stream. Stream spec says zero length LZW or DCT should error, but
     * Acrobat does not. So for compatibility, supress execution.
     */
    if ( NZstream ) {
      if ( pdf_execops(pdfc, ((stream_type == PDF_STREAMTYPE_CHARPROC) ?
                              OPSTATE_TYPE3FONT : OPSTATE_PDL),
                       stream_type) == OPSTATE_ERROR ) {
        result = FALSE ;
      }
    }

    if ( ! pdf_end_marking_context( pdfc , resource )) {
      result = FALSE ;
    }
  }

  if ( ! result )
    pdfxc->error = TRUE ;

  return result ;
}

/* ---------------------------------------------------------------------- */

static Bool pdf_open( PDFCONTEXT *pdfc , FILELIST **flptrin )
{
  FILELIST *flptr ;
  DEVICELIST *dev ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT* storestandardfonts ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* Take snapshot of standard fonts at start of PDF [12448] */
  storestandardfonts =
    fast_extract_hash_name( &internaldict, NAME_storestandardfonts ) ;
  if ( storestandardfonts == NULL ||
       !setup_pending_exec(storestandardfonts,TRUE) )
    return FALSE ;

  flptr = pdfxc->flptr ;
  HQASSERT( flptr , "flptr field NULL in pdf_open" ) ;
  dev = theIDeviceList( flptr ) ;

  /* If flptr is not seekable, then copy to file in tmp directory */
  if (!pdf_file_is_seekable(flptr)) {
    Hq32x2 nbytes;
    OBJECT tmp_file_obj = OBJECT_NOTVM_NOTHING;

    /*
     * 1. Open a tmp file
     * 2. Copy the input data to the tmp file
     * 3. Close the input channel
     * 4. Setup the tmp file as SW file and process it
     * 5. On closing remember to delete the tmp file
     */
    if (!pdf_open_tmp_file( pdfc, &tmp_file_obj ))
      return (dev ? device_error_handler( dev ) : FALSE);

    monitorf(UVS("Non-seekable input for a PDF file, writing a temporary copy\n"));

    if (!pdf_copy_to_tmp_file( flptr, oFile(tmp_file_obj), &nbytes )) {
      (void) pdf_close_tmp_file( pdfc, flptr );  /* Delete tmp file */
      return (dev ? device_error_handler( dev ) : FALSE);
    }

    /* Close the old device */
    if ( (*theIMyCloseFile(flptr))(flptr, CLOSE_EXPLICIT) == EOF ) {
      (void) pdf_close_tmp_file( pdfc, flptr );  /* Delete tmp file */
      return (dev ? device_error_handler( dev ) : FALSE);
    }

    /* Now make the flptr point to the temporary file */
    flptr = oFile(tmp_file_obj) ;
    dev = theIDeviceList( oFile(tmp_file_obj) );
    pdfxc->flptr = flptr;
    *flptrin = flptr;

    ixc->tmp_file_used = TRUE;

    /* Set the current file pos to 0 and end */
    Hq32x2FromInt32( &pdfxc->filepos, 0 );
    pdfxc->fileend = nbytes;

  } else {  /* file is seekable */

    HQASSERT( dev, "dev field NULL in pdf_open" ) ;

    /* Find the current position... */
    Hq32x2FromInt32( & pdfxc->filepos , 0 ) ;
    if ( ! (*theISeekFile( dev ))( dev , theIDescriptor( flptr ) ,
                                   & pdfxc->filepos , SW_INCR ))
      return device_error_handler( dev ) ;
    if ( isIReadWriteFile( flptr )) {
      if ( isIDoneFill( flptr ))    /* we last did a read */
        Hq32x2SubtractInt32( &pdfxc->filepos, &pdfxc->filepos, theICount( flptr ));
      else
        Hq32x2AddInt32( &pdfxc->filepos, &pdfxc->filepos, theICount( flptr ));
    }
    else /* if ( isIInputFile( flptr )) */
      Hq32x2SubtractInt32( &pdfxc->filepos, &pdfxc->filepos, theICount( flptr ));

    /* ...and the end of the file (or in other words how far we can seek). */
    if ( ! (*theIBytesFile( dev ))( dev , theIDescriptor( flptr ) ,
                                    & pdfxc->fileend , SW_BYTES_TOTAL_ABS )) {
      return device_error_handler( dev ) ;
    }
  }

  return TRUE ;
}

/**
 * Verify that the incoming file is seekable, code taken
 * from progress code in progress.c
 */
static Bool pdf_file_is_seekable(FILELIST *flptr)
{
  DEVICE_FILEDESCRIPTOR d;
  DEVICELIST            *dev;
  Hq32x2                offset;

  DEVICELIST_SEEK seekFn;

  HQASSERT( flptr , "flptri NULL in pdf_file_is_seekable" ) ;

  d = theIDescriptor( flptr );

  /* if its a filter its not seekable */
  if ( ( dev = theIDeviceList( flptr ) ) == NULL )
   return FALSE;

  /*
   * Find our location within the device and then seek to that.  If both
   * of these were possible, then the file is seekable.
   */
  Hq32x2FromInt32(&offset, 0);
  seekFn = theISeekFile(dev);
  return seekFn(dev, d, &offset, SW_INCR)
    && seekFn(dev, d, &offset, SW_SET);
}

/**
 * Copy from the incoming file to a temporary file, also
 * return the number of bytes in the file.
 */
static Bool pdf_copy_to_tmp_file(FILELIST *flptri, FILELIST *flptro, Hq32x2 *nbytes)
{
  int32 bytes_in;
  uint8 *buff;

  HQASSERT( flptri , "flptri NULL in pdf_copy_to_tmp_file" ) ;
  HQASSERT( flptro , "flptro NULL in pdf_copy_to_tmp_file" ) ;

  Hq32x2FromInt32(nbytes, 0);

  /* Loop around reading bytes from the input */
  while (GetFileBuff( flptri, MAXINT32, &buff, &bytes_in)) {
    if ( !file_write(flptro, buff, bytes_in) )
      return FALSE ;

    /* Keep track of the bytes in the file */
    Hq32x2AddInt32( nbytes, nbytes, bytes_in);
  }

  return (*theIMyFlushFile(flptro))(flptro) != EOF ;
}

/**
 * Open a temporary PDF file
 */
static Bool pdf_open_tmp_file( PDFCONTEXT *pdfc, OBJECT *tmp_file_obj )
{
  OBJECT     file_name_obj = OBJECT_NOTVM_NOTHING;
  uint8      file_name[32];
  DEVICELIST *tmp_dev;
  Bool       result;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( tmp_file_obj , "object NULL in pdf_open_tmp_file" ) ;

  swcopyf(file_name, (uint8 *)"%%tmp%%P%03X.PDF", pdfxc->id);
  if (!pdf_buildstring(pdfc, file_name, &file_name_obj))
    return FALSE;

  /* Enable the tmp device temporarily */
  if (( tmp_dev = find_device( (uint8*)"tmp" )) == NULL )
    return FALSE;
  theIDeviceFlags( tmp_dev ) |= DEVICEENABLED ;

  /* Now we can create the file */
  result = file_open(&file_name_obj, SW_RDWR | SW_CREAT | SW_TRUNC,
                     READ_FLAG | WRITE_FLAG,
                     FALSE, 0, tmp_file_obj);

  /* Disable the tmp device */
  theIDeviceFlags( tmp_dev ) &= ~DEVICEENABLED ;

  return result;
}

/**
 * Close amd delete a temporary PDF file
 */
Bool pdf_close_tmp_file( PDFCONTEXT *pdfc, FILELIST *flptr )
{
  uint8      file_name[32];
  DEVICELIST *tmp_dev;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  HQASSERT( flptr , "flptr NULL in pdf_close_tmp_file" ) ;

  if (( tmp_dev = find_device( (uint8*)"tmp" )) == NULL )
    return FALSE;

  HQASSERT( tmp_dev == theIDeviceList(flptr), "flptr doesn't point to tmp_device" );

  swcopyf(file_name, (uint8 *)"P%03X.PDF", pdfxc->id);

  if ((*theIMyCloseFile(flptr))(flptr, CLOSE_EXPLICIT) == EOF )
    return FALSE;

  if ((*theIDeleteFile( tmp_dev ))( tmp_dev, file_name ) == EOF ) {
    return FALSE;
  }

  return TRUE;
}

/**
   pdf_clip_to_cropbox()
   If the PDF parameter 'SizePageToBoundingBox' is set to false, it means that
   the page has to be clipped to the selected bounding box rather than sized
   to it.
*/
static Bool pdf_clip_to_cropbox( PDF_IXC_PARAMS *ixc, PDF_PAGEDEV *pagedev )
{
  OBJECT *cropbox;
  sbbox_t cropbbox;
  RECTANGLE cliprect;

  if (ixc->SizePageToBoundingBox)  /* i.e. not clipping */
    return TRUE;

  if (ixc->pagecropto >= PDF_PAGECROPTO_CROPBOX  && pagedev->CropBox != NULL)
    cropbox = pagedev->CropBox ;
  else if (ixc->pagecropto >= PDF_PAGECROPTO_ARTBOX && pagedev->ArtBox != NULL)
    cropbox = pagedev->ArtBox ;
  else if (ixc->pagecropto >= PDF_PAGECROPTO_TRIMBOX && pagedev->TrimBox != NULL)
    cropbox = pagedev->TrimBox ;
  else if (ixc->pagecropto >= PDF_PAGECROPTO_BLEEDBOX && pagedev->BleedBox != NULL)
    cropbox = pagedev->BleedBox ;
  else
    cropbox = pagedev->MediaBox ;

  if (cropbox != NULL) {

    if ( !object_get_bbox( cropbox, &cropbbox ))
      return FALSE ;

    bbox_to_rectangle( &cropbbox, &cliprect );

    if (!cliprectangles( &cliprect, 1 ))
      return FALSE;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
#define PDF_HEADBUF_SIZE 12

#if defined( ASSERT_BUILD )
static Bool pdftrace_header = TRUE ;
#endif



static Bool pdf_read_header( PDFCONTEXT *pdfc )
{
  Hq32x2 filestart ;
  FILELIST *flptr ;
  DEVICELIST *dev ;
  uint8 headbuf[ PDF_HEADBUF_SIZE ] ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  flptr = pdfxc->flptr ;
  HQASSERT( flptr , "flptr field NULL in pdf_read_header" ) ;
  dev = theIDeviceList( flptr ) ;
  HQASSERT( dev , "dev field NULL in pdf_read_header" ) ;

  Hq32x2FromInt32( & filestart , 0 ) ;

  if ( ! (*theISeekFile( dev ))( dev , theIDescriptor( flptr ) ,
                                   & filestart , SW_SET ))
    return device_error_handler( dev ) ;
  if ((*theIReadFile( dev ))( dev , theIDescriptor( flptr ) ,
                                headbuf ,
                                PDF_HEADBUF_SIZE ) != PDF_HEADBUF_SIZE )
    return device_error_handler( dev ) ;

  if (( HqMemCmp( headbuf , 5 , ( uint8 * )"%PDF-" , 5 ) != 0 ) &&
      ( HqMemCmp( headbuf , 5 , ( uint8 * )"%JTF-" , 5 ) != 0 )) {
    HQTRACE( pdftrace_header , ( "\"%%PDF-\" or \"%%JTF-\" not found in pdf_read_header." )) ;
    return error_handler( SYNTAXERROR ) ;
  }

  {
    uint8 *ptr = & headbuf[ 5 ] ;

    ixc->majorversion = 0 ;

    while ( *ptr <= '9' && *ptr >= '0' ) {
      ixc->majorversion = ixc->majorversion * 10 + ( *ptr++ - '0' ) ;
      if ( ptr - headbuf >= PDF_HEADBUF_SIZE) {
        HQTRACE( pdftrace_header , ( "Buffer overflow in pdf_read_header." )) ;
        return error_handler( SYNTAXERROR ) ;
      }
    }

    if ( *ptr++ != '.' ) {
      HQTRACE( pdftrace_header , ( "Bad version comment in pdf_read_header." )) ;
      return error_handler( SYNTAXERROR ) ;
    }

    ixc->minorversion = 0 ;

    while ( *ptr <= '9' && *ptr >= '0' ) {
      ixc->minorversion = ixc->minorversion * 10 + ( *ptr++ - '0' ) ;
      if ( ptr - headbuf >= PDF_HEADBUF_SIZE) {
        HQTRACE( pdftrace_header , ( "Buffer overflow in pdf_read_header." )) ;
        return error_handler( SYNTAXERROR ) ;
      }
    }

    if ( !IsWhiteSpace( *ptr )) {
      HQTRACE( pdftrace_header , ( "Bad version comment in pdf_read_header." )) ;
      return error_handler( SYNTAXERROR ) ;
    }
  }

  /* Check the PDF file's version against what we're capable of. */
  if (( ixc->majorversion > PDF_MAJOR_VERSION ) ||
      ( ixc->majorversion == PDF_MAJOR_VERSION  && ixc->minorversion > PDF_MINOR_VERSION )) {

    monitorf(UVM("PDF Warning: Unexpected PDF version - %d.%d\n"),
             ixc->majorversion, ixc->minorversion ) ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
#if defined( ASSERT_BUILD )
static Bool pdftrace_psop = FALSE ;
#endif

Bool pdf_execop(ps_context_t *pscontext, int32 opnumber)
{
  OBJECT tmp_errobject ;
  OPERATOR *op ;
  int saved_gc_safety_level = gc_safety_level;
  Bool res;

  HQASSERT( opnumber >= 0 , "pdf_execop: invalid opnumber" ) ;
  op = & system_ops[ opnumber ] ;

  /* Save old error object */
  tmp_errobject = errobject ;

  /* Setup the error object for this operator. */
  object_store_operator(&errobject, opnumber) ;
  op = oOp(errobject) ;

  ++gc_safety_level;
  HQTRACE( pdftrace_psop , ("op: %.*s\n", op->opname->len,op->opname->clist));
  res = (op->opcall)(pscontext);
  gc_safety_level = saved_gc_safety_level;
  if ( !res )
    return FALSE;

  /* Reset the error object back to pdfexec. */
  errobject = tmp_errobject ;
  return TRUE ;
}

enum { pagedict_Annots, pagedict_Contents, pagedict_dummy } ;
static NAMETYPEMATCH page_dict[pagedict_dummy + 1] = {
  { NAME_Annots   | OOPTIONAL,  4, { OARRAY, OPACKEDARRAY, ONULL, OINDIRECT }},
  { NAME_Contents | OOPTIONAL,  4, { OFILE, OARRAY, OPACKEDARRAY, OINDIRECT }},
  DUMMY_END_MATCH
} ;

static Bool pdf_walk_contents( PDFCONTEXT *pdfc , OBJECT *page )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT *annots = NULL ;

  HQASSERT( pdfc , "pdfc NULL in pdf_walk_contents" ) ;
  HQASSERT( page , "page NULL in pdf_walk_contents" ) ;
  HQASSERT( oType(*page) == ODICTIONARY , "Page type be ODICTIONARY" ) ;

  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  {
    int32 state = OPSTATE_PDL ;
    pdfc->contentsindex = 0 ;

    if ( ! pdf_dictmatch( pdfc , page , page_dict ))
      return FALSE ;

    /* The spec states the Annots key should be omitted if there are no
     * annotations, however at least one job contains Annots with a null
     * object. */
    annots = page_dict[pagedict_Annots].result;
    if ( ixc->strictpdf && annots && oType(*annots) == ONULL )
      return error_handler( TYPECHECK ) ;

    /* Obtain the page's contents and execute them
     */
    pdfc->contents = page_dict[pagedict_Contents].result ;
    if (pdfc->contents != NULL) {
      GSTATE * temp_gstate;

      if (!gs_gpush( GST_SAVE ))
        return FALSE;

      temp_gstate = gstackptr;

      state = pdf_execops( pdfc , state , PDF_STREAMTYPE_PAGE ) ;

      /* Restore graphics state & CTM. */
      if (!gs_setgstate( temp_gstate, GST_GSAVE, FALSE, TRUE, FALSE, NULL ))
        return FALSE;

      if ( state == OPSTATE_ERROR ) {
        pdfxc->error = TRUE ;
        return FALSE ;
      }
    }
  }

  /* If there are any Annotations, go and do them. */
  if (ixc->PrintAnnotations && annots != NULL  &&  oType(*annots) != ONULL) {
    GSTATE * temp_gstate;
    Bool   ret;

    /*
    ** The 'annots' should be a pointer to an array-object of indirect
    ** references to annotation dictionaries
    */
    HQASSERT( oType(*annots) == OARRAY  ||  oType(*annots) == OPACKEDARRAY,
              "Annotations list not an array in pdf_walk_contents" );

    if (!gs_gpush( GST_SAVE ))
      return FALSE;

    temp_gstate = gstackptr;

    ret = pdf_do_annots( pdfc, oArray(*annots), theLen(*annots) );

    /* Restore graphics state & CTM.  (NB: gs_gpop() is inappropriate here
       as the content stream could fail with extra gstates stacked) */
    ret = gs_setgstate( temp_gstate, GST_GSAVE, FALSE, TRUE, FALSE, NULL ) &&
      ret;

    return ret;
  }

  return TRUE ;
}

/** Close a page subgroup previously opened by openPageSubgroup(). This method
should always be called when openPageSubgroup() has been successfully called.
*/
static Bool closePageSubgroup(Bool rcb_enable, Group **group, int32 gid,
                              Bool success)
{
  GUCR_RASTERSTYLE *targetRS;

  if (rcb_enable)
    rcbn_enable_interception(gstateptr->colorInfo);

  if (*group != NULL && !groupClose(group, success))
    success = FALSE;

  /* After the groupClose the target rasterstyle is correct, but the grestore
     will change it to whatever was set at the start of openPageSubgroup.  So
     reset the target RS after the grestore. */
  targetRS = gsc_getTargetRS(gstateptr->colorInfo);

  if (gid != GS_INVALID_GID && !gs_cleargstates(gid, GST_GROUP, NULL))
    success = FALSE ;

  gsc_setTargetRS(gstateptr->colorInfo, targetRS);

  return success;
}

/** Create a new page-level subgroup if the any of values in the passed
Transparency Group dictionary 'pageGroupDict' (which may be NULL) do not match
those used to create the default top-level group. The default group is:

  Isolated
  Non-knockout
  CMYK Colorspace

If 'group' is not NULL after this function is called, closePageSubgroup() must
be called once the page contents have been processed.
*/
static Bool openPageSubgroup(PDFCONTEXT* pdfc, OBJECT* pageGroupDict,
                             Bool *rcb_enable, Group **group, int32 *gid)
{
  enum { gm_CS, gm_I, gm_K, gm_dummy } ;
  static NAMETYPEMATCH groupMatch[gm_dummy + 1] = {
    { NAME_CS | OOPTIONAL, 3, { ONAME, OARRAY, OINDIRECT }},
    { NAME_I | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
    { NAME_K | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
    DUMMY_END_MATCH } ;

  DL_STATE *page = pdfc->corecontext->page;
  Bool isolated = FALSE, knockout = FALSE;
  int32 virtualSpaceName;
  OBJECT colorspace = OBJECT_NOTVM_NULL;

  GET_PDFXC_AND_IXC;

  /* Initialise result. */
  *rcb_enable = FALSE;
  *group = NULL;
  *gid = GS_INVALID_GID;

  /* The default page group we have is already correct */
  if ( pageGroupDict == NULL && !ixc->encapsulated )
    return TRUE;

  /* Start off with colorspace set to virtual device space. */
  dlVirtualDeviceSpace(page, &virtualSpaceName, NULL);
  object_store_name(&colorspace, virtualSpaceName, LITERAL);

  /* Read the page group dictionary. */
  if ( pageGroupDict != NULL ) {
    OBJECT *groupColorSpace, pdfxOverrideSpace = OBJECT_NOTVM_NULL;

    if ( !pdf_dictmatch(pdfc, pageGroupDict, groupMatch) )
      return FALSE;

    groupColorSpace = groupMatch[gm_CS].result;

    /* Does PDF/X need to override the page group colorspace? */
    if ( !pdfxCheckForPageGroupCSOverride(pdfc, groupColorSpace,
                                          &pdfxOverrideSpace) )
      return FALSE;
    if ( oType(pdfxOverrideSpace) != ONULL )
      groupColorSpace = &pdfxOverrideSpace;

    if ( groupColorSpace != NULL ) {
      /* Map the blend space; this also checks for blend space validity. */
      if ( !pdf_mapBlendSpace(pdfc, *groupColorSpace, &colorspace) )
        return FALSE;
    }

    /* Read the isolated status; this is only honored if the page is
       encapsulated (i.e. being included as part of some other job). */
    if ( groupMatch[gm_I].result != NULL )
      isolated = oBool(*groupMatch[gm_I].result);

    /* Read the knockout status. */
    if ( groupMatch[gm_K].result != NULL )
      knockout = oBool(*groupMatch[gm_K].result);
  }

  if ( !gs_gpush(GST_GROUP) )
    return FALSE;
  *gid = gstackptr->gId ;
  /* MUST CALL closePageSubgroup() on error from now on. */

  /* The page group can be treated in two distinctly different ways
     (paraphrasing the spec):

     -Ordinarily, the page is imposed directly on an output medium, such as
     paper or a display screen.  Use dlSetPageGroup to open a new page group
     with the attributes given in the job.  The previous page group is either
     destroyed (if empty) or added to the DL (not empty).
     Even if the groups are otherwise identical, there is a subtlety. We have
     removed the virtual device and replaced it with a page group, the
     difference being a flag in the group's inputRasterStyle. OverprintPreview
     is one feature that relies on this.

     -A page of a PDF file can be treated as a graphics object to be used as
     an element of a page of some other document. The PDF page is treated as
     an ordinary transparency group, which can be either isolated or
     non-isolated and is composited with its backdrop in the normal way.
  */
  if ( !ixc->encapsulated ) {
    if ( !dlSetPageGroup(page, colorspace, knockout) )
      return closePageSubgroup(*rcb_enable, group, *gid, FALSE);
    *group = page->currentGroup;
  }
  else {
    if ( !groupOpen(page, colorspace, isolated, knockout,
                    TRUE /*banded*/, NULL /*bgcolor*/, NULL /*xferfn*/,
                    NULL /*patternTA*/, GroupSubGroup, group) )
      return closePageSubgroup(*rcb_enable, group, *gid, FALSE);
  }

  if ( !gs_gpush(GST_GSAVE) )
    return closePageSubgroup(*rcb_enable, group, *gid, FALSE);

  /* If the job specifies a page group or sub-group then recombine interception
     must be disabled. Preseparated jobs shouldn't do this anyway. */
  rcbn_disable_interception(gstateptr->colorInfo);
  *rcb_enable = TRUE;

  return TRUE;
}

/** Walk the contents of the page, enclosed in a marking context.
'pageDict' should be the page dictionary.
'mcResource' will be passed into the pdf_begin_marking_context() call, and may
be NULL.
Returns true if the page contents were successfully processed.
*/
static Bool walkContentsInMarkingContext(PDFCONTEXT *pdfc,
                                         PDF_PAGEDEV *pageDev,
                                         OBJECT *pageDict,
                                         OBJECT *mcResource,
                                         SYSTEMVALUE translation[2])
{
  enum { pgm_Group, pgm_dummy } ;
  static NAMETYPEMATCH pageGroupMatch[pgm_dummy + 1] = {
    { NAME_Group | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    DUMMY_END_MATCH
  };

  PDFCONTEXT *pdfcPage = NULL;
  GET_PDFXC_AND_IXC;

  HQASSERT(pageDev != NULL && pageDict != NULL,
           "walkContentsInMarkingContext - parameters cannot be NULL");

  if (pdf_dictmatch(pdfc, pageDict, pageGroupMatch) &&
      pdf_begin_marking_context(pdfxc, &pdfcPage, mcResource,
                                PDF_STREAMTYPE_PAGE)) {
    Bool result = pdf_rr_start_page( pdfcPage, pageDict, pageDev ) ;
    if ( result ) {

      /* When pdf_setpagedevice() was called earlier, it returned 'translation'
         containing the x,y translation required to complete 'crop_box' et.al.
         page cropping.  The translation is done here, rather than in
         pdf_setpagedevice(), because of the setpagedevices that are potentially
         called from pdf_jt_get_trapinfo() and pdf_rr_start_page(). */
      if ( translation[0] != 0.0 || translation[1] != 0.0 ) {
        gs_translatectm(-translation[0], -translation[1]);
        MATRIX_COPY(&pdfcPage->u.i->defaultCTM, &thegsPageCTM(*gstateptr)) ;
      }

      /* Check if the media page needs clipping rather than cropping. */
      result = pdf_clip_to_cropbox(ixc, pageDev) ;

      if ( result && ixc->page_continue ) {
        ps_context_t *pscontext = pdfxc->corecontext->pscontext ;
        OBJECT pageSaveObj = OBJECT_NOTVM_NOTHING;
        Bool rcb_enable ;
        Group *group ;
        int32 gid ;

        if (!pdfxc->corecontext->pdfparams->PoorShowPage) {
          /* The page is now interpreted, within a save/restore context to free
             off any PS objects that might be created as side effects. */
          result = save_(pscontext);
          if ( result ) {
            Copy( &pageSaveObj, theTop( operandstack ) ) ;
            pop( &operandstack ) ;
          }

          /* Squirrel the default gstate for Default extgstate settings from the
             save placed around the pdf page. */
          HQASSERT(gstackptr->gType == GST_SAVE, "Expected gstate to be a SAVE");
          ixc->pPageStartGState = gstackptr;
        }

        if ( result ) {
          /* If the page's dictionary contained a 'Group' entry, we may need to
             create a new page group. This function must be called even when
             'pageGroupDict' is NULL. */
          result = openPageSubgroup(pdfcPage, pageGroupMatch[pgm_Group].result,
                                    &rcb_enable, &group, &gid);
          if ( result ) {
            /* render the page's contents */
            result = pdf_walk_contents(pdfcPage, pageDict);

            /* Always close the page subgroup if it has been opened.
               The group may have changed if a partial paint was done. */
            if ( group != NULL )
              group = pdfcPage->corecontext->page->currentGroup;
            result = closePageSubgroup(rcb_enable, &group, gid, result);
          }
        }

        if (!pdfxc->corecontext->pdfparams->PoorShowPage) {
          /* We don't want to restore if there was an error in the interpreter, as
             this would restore away the $error dictionary setup by handleerror()
             in the interpreter call. The server loop will automatically restore
             to the server level. */
          result = result &&
            push( &pageSaveObj, &operandstack ) && restore_(pscontext);
        }
      }

      /* Always end the RR page if it started */
      result = pdf_rr_end_page( pdfcPage ) && result;
    }

    /* Always end the marking context once it has been begun. */
    return pdf_end_marking_context(pdfcPage, mcResource) && result;
  }

  return FALSE;
}

Bool pdf_get_content( PDFCONTEXT *pdfc, OBJECT **contents )
{
  PDF_CHECK_MC( pdfc ) ;
  HQASSERT( contents , "content is NULL in pdf_get_content" ) ;

  HQASSERT( pdfc->contents , "pdfc->contents is NULL in pdf_get_content" ) ;

  switch (oType( *(pdfc->contents) )) {
  case OFILE:
    *contents = pdfc->contentsStream = pdfc->contents ;
    if ( pdfc->streamtype == PDF_STREAMTYPE_PAGE ) {
      pdf_xrefthispageonly( pdfc->pdfxc , pdfc->contentsStream ) ;
    }
    return TRUE ;

  case OARRAY:
  case OPACKEDARRAY:
    {
      OBJECT *olist , *obj ;
      int32 index ;

      HQASSERT( pdfc->contentsindex >= 0 &&
                pdfc->contentsindex < theLen(* pdfc->contents ),
                "Invalid contents index in pdf_get_content" ) ;

      olist = oArray( *pdfc->contents ) + pdfc->contentsindex ;

      for ( index = pdfc->contentsindex; index < theLen(* pdfc->contents );
            index++ ) {
        if ( oType(*olist) != OINDIRECT )
          return error_handler( TYPECHECK ) ;
        if ( ! pdf_lookupxref( pdfc , & obj ,
                               oXRefID( *olist ) ,
                               theGen(* olist ) ,
                               FALSE ))
          return FALSE ;
        /* Skip if didn't find a stream or found an ONULL object. */
        if ( obj != NULL ) {
          if (oType(*obj) == OFILE ) {
            *contents = pdfc->contentsStream = obj ;
            if ( pdfc->streamtype == PDF_STREAMTYPE_PAGE ) {
              pdf_xrefthispageonly( pdfc->pdfxc , pdfc->contentsStream ) ;
            }
            pdfc->contentsindex = index ;
            return TRUE ;
          }
          else if (oType(*obj) != ONULL )
            return error_handler( TYPECHECK ) ;
        }

        olist++;
      }

      *contents = pdfc->contentsStream = NULL ;
      return TRUE ;
    }

  default:
    return error_handler( TYPECHECK ) ;
  }
  /* Not reached */
}

Bool pdf_next_content( PDFCONTEXT *pdfc , FILELIST **flptr )
{
  PDF_CHECK_MC( pdfc ) ;
  HQASSERT( flptr , " flptr is NULL in pdf_next_content" ) ;
  HQASSERT( pdfc->contents, "pdfc->contents is NULL in pdf_next_content" ) ;

  switch ( oType( *(pdfc->contents) )) {

  case OFILE:
    *flptr = NULL ; /* No more content */
    pdfc->contentsStream = NULL ;
    return TRUE ;

  case OARRAY:
  case OPACKEDARRAY:
    {
      OBJECT *contents = NULL ;

      HQASSERT( pdfc->contentsindex >= 0 &&
                pdfc->contentsindex < theLen(* pdfc->contents ),
                "Invalid contents index in pdf_next_content" ) ;
      pdfc->contentsindex++ ;

      if ( pdfc->contentsindex < theLen(* pdfc->contents ) ) {
        if ( ! pdf_get_content( pdfc, &contents ))
          return FALSE ;
        if ( contents != NULL ) {
          *flptr = oFile( *contents ) ;
          return TRUE ;
        }
      }

      *flptr = NULL ;
      pdfc->contentsStream = NULL ;
      return TRUE ;
    }

  default:
    return error_handler( TYPECHECK ) ;
  }
  /* Not reached */
}



/* Count should also be required, but it appears Acrobat can do without it. */
enum {
  pages_dict_Kids, pages_dict_Count, pages_dict_n_entries
} ;
static NAMETYPEMATCH pages_dict[pages_dict_n_entries + 1] = {
  { NAME_Kids | OOPTIONAL,  5, { OARRAY, OPACKEDARRAY,
                                 OLONGARRAY, OLONGPACKEDARRAY, OINDIRECT }},
  { NAME_Count | OOPTIONAL, 2, { OINTEGER, OINDIRECT }},
  DUMMY_END_MATCH
} ;



/* If we have a DeviceCMYK tint transform then use it to get the CMYK equivalent.
 * This function provides information which the rip can often carry on without, so
 * in the event of non-conformant input data we would want to pretend that the input
 * data didn't exist rather than erroring. The extra complexity of returning TRUE
 * for that case, and FALSE for catastrophic errors has led to making this function
 * a void rather than a Bool. Any catastrophic errors, such as VMerror, will most
 * likely be picked up very soon.
 */
static void tryfetchCMYKequiv(PDFCONTEXT *pdfc, OBJECT * color_space, NAMECACHE * platename)
{
static USERVALUE singleip[1] = { 1.0 };
static intptr_t ondims = 4;
static int32 funcid = 1;
  int32 num_ipcols = 1;
  USERVALUE *inp = singleip;

  OBJECT * tint;
  OBJECT * arr;
  OBJECT * sepnameobj;
  USERVALUE equiv[4];
  NAMECACHE * sepname;
  OBJECT *statusdict;
  OBJECT *dict;
  OBJECT nameobj = OBJECT_NOTVM_NOTHING ;
  OBJECT* olist;
  OBJECT values = OBJECT_NOTVM_NOTHING;
  OBJECT tdict = OBJECT_NOTVM_NOTHING;
  int32 i;

  PDF_CHECK_MC( pdfc ) ;
  HQASSERT(color_space != NULL, "color_space NULL");
  HQASSERT(platename != NULL, "platename NULL");

  #define DEVICECMYKSPACESIZE 4

  /* e.g. [ /Separation /PANTONE#20376#20CV /DeviceCMYK 73 0 R ] */

  if (oType(*color_space) != OARRAY)
    return;

  /* normally 4 items but can be 5
     [ /Separation colorantname /DeviceCMYK tint {attribs} ] */
  if ((theLen(*color_space) < 4)||(theLen(*color_space) > 5))
    return;

  arr = oArray(*color_space);

  if (oName(arr[2]) != &system_names[NAME_DeviceCMYK])
    return;

  sepnameobj = arr + 1;
  tint = arr + 3;

  if (oName(arr[0]) == &system_names[NAME_Separation]) {
    if (theLen(*color_space) != 4)
      return;

    switch (oType(*sepnameobj)) {
      case OSTRING:
        sepname = cachename( oString(*sepnameobj),
                             ( uint32 )theLen( *sepnameobj )) ;
        break;
      case ONAME:
        sepname = oName(*sepnameobj);
        break;
      default:
        return;
    }

    if (sepname != platename)
      return;

  } else {
    OBJECT *thisname = NULL;
    if (oName(arr[0]) != &system_names[NAME_DeviceN])
      return;

    if (oType(*sepnameobj) != OARRAY)
      return;

    if (theLen(*sepnameobj) <= 0)
      return;

    /* we have an array of colorants for DeviceN. We need to
       extract the current one only.
    */
    for (i = 0;i < theLen(*sepnameobj);i++) {
      thisname = oArray(*sepnameobj) + i;
      if (oType(*thisname) == ONAME) {
        if (oName(*thisname) == platename)
          break;
      }
    }

    if (i >= theLen(*sepnameobj))
      return;

    sepname = oName(*thisname);

    if (theLen(*sepnameobj) != 1) {
      if (theLen(*color_space) == 5) {
        /* we have an additional attributes dictionary we can use.
           The /Colorants dictionary contains a set of /Separation
           colorspaces, one for each colorant (keyed by the colorant name).
        */
        enum { attribs_Colorants, attribs_n_entries } ;
        static NAMETYPEMATCH attribs_dictmatch[attribs_n_entries + 1] = {
          { NAME_Colorants, 2 , { ODICTIONARY, OINDIRECT }},
          DUMMY_END_MATCH
        } ;
        OBJECT * result;
        OBJECT * attribs = oArray(*color_space) + 4;


        if (oType(*attribs) == OINDIRECT) {
          if ( ! pdf_lookupxref( pdfc, &attribs, oXRefID(*attribs),
                                 theGen(*attribs), FALSE ))
            return;
        }
        if (oType(*attribs) != ODICTIONARY)
          return;

        if ( !pdf_dictmatch( pdfc , attribs , attribs_dictmatch ))
          return;

        /* extract the appropriate /Separation colorspace and recurse */
        result = attribs_dictmatch[attribs_Colorants].result;
        oName(nnewobj) = platename ;
        dict = fast_extract_hash( result , & nnewobj ) ;

        if (oType(*dict) == OINDIRECT) {
          if ( ! pdf_lookupxref( pdfc, &dict, oXRefID(*dict),
                                 theGen(*dict), FALSE ))
            return;
        }

        tryfetchCMYKequiv(pdfc, dict, platename);
        return;

      } else {
        int32 j;
        /* this is the less appealing solution where we must
           try and obtain the CMYK equivalent from a composite
           tint transform by setting other colroants to zero.
           I'd presume we are assuming these are all orthogonal
        */
        num_ipcols = theLen(*sepnameobj);
        inp = mm_alloc( mm_pool_temp,
                        num_ipcols * sizeof( USERVALUE ),
                        MM_ALLOC_CLASS_REL_ARRAY );
        if (inp == NULL) {
          (void) error_handler(VMERROR);
          return;
        }
        for (j = 0;j < num_ipcols;j++)
          inp[j] = 0.0;

        inp[i] = 1.0; /* our platename index from above */
      }
    }
  }

  if (oType(*tint) == OINDIRECT) {
    if ( ! pdf_lookupxref( pdfc, &tint, oXRefID(*tint),
                           theGen(*tint), FALSE )) {
      if (num_ipcols != 1)
        mm_free( mm_pool_temp, inp, num_ipcols * sizeof( USERVALUE ) );
      return;
    }
  }

  /* use slot #1 to quickly evaluate equivalelent CMYK*/
  if ( ! fn_evaluate( tint ,
                      inp, equiv,
                      FN_TINT_TFM, 1,
                      funcid++, FN_GEN_NA ,
                      ( void * ) ondims )) {
    if (num_ipcols != 1)
      mm_free( mm_pool_temp, inp, num_ipcols * sizeof( USERVALUE ) );
    return;
  }

  if (num_ipcols != 1)
    mm_free( mm_pool_temp, inp, num_ipcols * sizeof( USERVALUE ) );

  /* Get statusdict from systemdict */
  statusdict = fast_extract_hash_name(&systemdict, NAME_statusdict);
  if (! statusdict)
    return;

  /* Get NAME_CMYKEquivalents dictionary from statusdict */
  dict = fast_extract_hash_name(statusdict, NAME_CMYKEquivalents);
  if ( ! dict ) {
    /* if this is new then create a dictionary big enough to
       hold a reasonable number of separations*/
    dict = &tdict;
    if ( ! ps_dictionary(dict, 6) )
      return;
  } else {
    if ( oType(*dict) != ODICTIONARY )
      return;
  }

  /* put CMYK equivalents in PS object */
  if ( !ps_array(&values, DEVICECMYKSPACESIZE) )
    return;

  olist = oArray(values);
  for (i = 0;i < DEVICECMYKSPACESIZE;i++) {
    object_store_real(&olist[i], equiv[i]) ;
  }

  theTags(nameobj) = ONAME | LITERAL;
  oName(nameobj) = sepname;
  if ( ! insert_hash( dict, &nameobj, &values ))
    return ;

  oName(nameobj) = &system_names[NAME_CMYKEquivalents] ;
  if ( ! insert_hash(statusdict, &nameobj, dict) )
    return ;
}


/* pdf_count_pages recursively increments ixc->pageno by the number of
 * pages below the given Pages dict */
Bool pdf_count_pages( PDFCONTEXT *pdfc ,
                      PDF_IXC_PARAMS *ixc,
                      OBJECT *pages ,
                      PDF_SEPARATIONS_CONTROL *pdfsc)
{
  /* ---------------------------------------------------------------------- */
  /* Type, according to the Spec, should be required. This is a case of
   * doing what Adobe does and not what the spec says.
   */
  enum {
    epage_basic_type = 0,
    epage_basic_separationinfo,

    epage_basic_max
  };

  static NAMETYPEMATCH page_basic_dict[epage_basic_max + 1] = {
    { NAME_Type,                   2, { ONAME, OINDIRECT }},
    { NAME_SeparationInfo | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},

    DUMMY_END_MATCH
  } ;

  HQASSERT( pages , "pages NULL in pdf_count_pages" ) ;

  if ( oType(*pages) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! ixc->strictpdf )
    page_basic_dict[ epage_basic_type ].name |= OOPTIONAL ;
  else
    page_basic_dict[ epage_basic_type ].name &= ~OOPTIONAL ;

  if ( ! pdf_dictmatch( pdfc , pages , page_basic_dict ))
    return FALSE ;

  if ( ( page_basic_dict[ epage_basic_type ].result == NULL ) ||
       ( oName( *(page_basic_dict[ epage_basic_type ].result) ) == system_names + NAME_Pages )) {
    int32 i , len ;
    OBJECT *olist ;

    if (!pdf_dictmatch( pdfc , pages , pages_dict ))
      return FALSE;

    if (pages_dict[pages_dict_Kids].result) {
      /* Found a Kids key, so assume we have a Pages object */

      /* possible quick skip past low pages */
      if (!rcbn_enabled() && pages_dict[pages_dict_Count].result) {
        ixc->pageno += oInteger(*pages_dict[pages_dict_Count].result) ;
        return TRUE;
      }

      /* failed to find the (required) count so resort to other methods */
      len = theLen(* pages_dict[pages_dict_Kids].result ) ;
      olist = oArray(*pages_dict[pages_dict_Kids].result) ;

      for ( i = 0 ; i < len ; ++i ) {

        if (oType(*olist) != OINDIRECT ) {
          return error_handler( TYPECHECK ) ;
        }
        if ( ! pdf_lookupxref( pdfc , & pages ,
                               oXRefID( *olist ) ,
                               theGen(* olist ) ,
                               FALSE ))
          return FALSE;
        if ( pages == NULL )
          return error_handler( UNDEFINEDRESOURCE ) ;

        Copy(&pdfsc->page, olist) ;

        if ( ! pdf_count_pages( pdfc, ixc, pages, pdfsc))
          return FALSE;

        ++olist ;
      }

      return TRUE ;
    }
  }


  /* Beyond this point, we have done any necessary recursion in the
     Pages tree to a Page object, so "pages" actually points to a
     Page. */

  if ( ( page_basic_dict[ epage_basic_type ].result != NULL ) &&
       ( oName( *(page_basic_dict[ epage_basic_type ].result) ) != system_names + NAME_Page )) {
    return error_handler( SYNTAXERROR ) ;
  } else {
    Bool incrementPageCount = TRUE ;

    HQASSERT(pdfsc, "pdfsc NULL in pdf_count_pages on a page object");

    /* Decide whether to move on to the next logical page when recombining. The
     * existence and correctness of SeparationInfo/Pages is crucial to getting
     * both logical page boundaries and logical page range control right. If we
     * detect that a job fails to sufficiently conform we let the recombine
     * module decide the logical page boundaries based on the page colorant
     * heuristics.
     */
    if ( page_basic_dict[ epage_basic_separationinfo ].result ) {

      PDF_SEPARATION_INFO separation_info = {0} ;

      if ( !pdf_walk_separation_info(pdfc,
                                     page_basic_dict[ epage_basic_separationinfo ].result,
                                     &separation_info) )
        return FALSE;

      if ( rcbn_enabled() && separation_info.pages != NULL ) {
        if ( pdfsc->previous_pages != NULL ) {  /* Excludes the first page */
          if ( compare_objects( separation_info.pages, pdfsc->previous_pages ))
            incrementPageCount = FALSE ;
        }
        pdfsc->previous_pages = separation_info.pages ;
      }
    }

    if ( incrementPageCount ) {
      ixc->pageno++ ;
    }
  }

  return TRUE ;
}

/* walk and render pages numbered between first and last
 * if first < 0 then walk all pages up to last,
 * if last < 0 then we walk all the pages to the end
 * if first > last, print pages in reverse order
 */
static Bool pdf_walk_page_range( PDFCONTEXT *pdfc ,
                                 OBJECT *pages ,
                                 PDF_PAGEDEV *pagedev,
                                 PDF_SEPARATIONS_CONTROL *pdfsc,
                                 int32 first,
                                 int32 last,
                                 int32 num_pages_in_job,
                                 Bool is_reverse_page_range,
                                 Bool *was_page)
{
  PDF_PAGEDEV lpagedev ;
  PDF_SEPARATION_INFO separation_info = {0} ;
  OBJECT *resource ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  SYSTEMVALUE translation[2] = { 0.0, 0.0 };
  int32 pres_pageId ;

  /* ---------------------------------------------------------------------- */
  /* Type, according to the Spec, should be required. This is a case of
   * doing what Adobe does and not what the spec says.
   */
  enum {
    epage_basic_type = 0,
    epage_basic_parent,
    epage_basic_mediabox,
    epage_basic_cropbox,
    epage_basic_artbox,
    epage_basic_trimbox,
    epage_basic_bleedbox,
    epage_basic_rotate,
    epage_basic_userunit,
    epage_basic_separationinfo,
    epage_basic_group,
    epage_basic_presSteps,
    epage_basic_platecolor,

    epage_basic_max
  };

  static NAMETYPEMATCH page_basic_dict[epage_basic_max + 1] = {
    { NAME_Type,                   2, { ONAME, OINDIRECT }},
    { NAME_Parent | OOPTIONAL,     2, { OINDIRECT, ODICTIONARY }},
    { NAME_MediaBox | OOPTIONAL,   3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_CropBox | OOPTIONAL,    3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_ArtBox | OOPTIONAL,     3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_TrimBox | OOPTIONAL,    3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_BleedBox | OOPTIONAL,   3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_Rotate | OOPTIONAL,     2, { OINTEGER, OINDIRECT }},
    { NAME_UserUnit | OOPTIONAL,   3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_SeparationInfo | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_Group | OOPTIONAL,      2, { ODICTIONARY, OINDIRECT }},
    { NAME_PresSteps | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},

    /* PlateColor is an undocumented feature of PDF 1.2 */
    { NAME_PlateColor | OOPTIONAL, 2, { OSTRING, OINDIRECT }},

    DUMMY_END_MATCH
  } ;

  /* Resource dictionary */
  enum {
    resource_dict_Resources,
    resource_dict_n_entries
  } ;
  static NAMETYPEMATCH resource_dict[resource_dict_n_entries + 1] = {
    { NAME_Resources | OOPTIONAL,  2, { ODICTIONARY, OINDIRECT }},
    DUMMY_END_MATCH
  } ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( pages , "pages NULL" ) ;
  if ( was_page != NULL ) {
    *was_page = FALSE ;
  }

  /* Remember the page number we're on, then set a rogue value
     reflecting our recursion depth. This ensures that any objects
     loaded have exactly the right lifetime, i.e. they're guaranteed
     to stay around until the "pdf_sweepxrefpage( pdfc,
     -pdfxc->pdfwalk_depth )" below. Note that every non-error return
     path from this function must restore the value of
     pdfxc->pageId. */
  pres_pageId = pdfxc->pageId ;
  pdfxc->pageId = -pdfxc->pdfwalk_depth ;

  if ( oType(*pages) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! ixc->strictpdf )
    page_basic_dict[ epage_basic_type ].name |= OOPTIONAL ;
  else
    page_basic_dict[ epage_basic_type ].name &= ~OOPTIONAL ;

  if ( ! pdf_dictmatch( pdfc , pages , page_basic_dict ))
    return FALSE ;

  if ( ! pagedev ) {
    pagedev = & lpagedev ;
    pagedev->MediaBox = NULL ;
    pagedev->CropBox = NULL ;
    pagedev->ArtBox = NULL ;
    pagedev->TrimBox = NULL ;
    pagedev->BleedBox = NULL ;
    pagedev->Rotate = NULL ;
    pagedev->UserUnit = NULL ;
    pagedev->PlateColor = NULL ;
  }
  else {
    /* Subsidary must have Parent. */
    if ( ! page_basic_dict[ epage_basic_parent ].result )
      return error_handler( TYPECHECK ) ;

    /* Copy page device. */
    lpagedev = *pagedev ;
    pagedev = & lpagedev ;
  }

  /* Over-ride previous attributes if necessary. */
  if ( page_basic_dict[ epage_basic_mediabox ].result )
    pagedev->MediaBox = page_basic_dict[ epage_basic_mediabox ].result ;
  if ( page_basic_dict[ epage_basic_cropbox ].result )
    pagedev->CropBox = page_basic_dict[ epage_basic_cropbox ].result ;
  if ( page_basic_dict[ epage_basic_artbox ].result )
    pagedev->ArtBox = page_basic_dict[ epage_basic_artbox ].result ;
  if ( page_basic_dict[ epage_basic_trimbox ].result )
    pagedev->TrimBox = page_basic_dict[ epage_basic_trimbox ].result ;
  if ( page_basic_dict[ epage_basic_bleedbox ].result )
    pagedev->BleedBox = page_basic_dict[ epage_basic_bleedbox ].result ;

  if ( page_basic_dict[ epage_basic_rotate ].result )
    pagedev->Rotate = page_basic_dict[ epage_basic_rotate ].result ;
  if (page_basic_dict[ epage_basic_userunit ].result )
    pagedev->UserUnit = page_basic_dict[ epage_basic_userunit ].result;
  /* PlateColor isn't inheritable so no need to be in this list */

  /* Get the resource dictionary. Don't resolve any indirect
   * references since we want to mark the resource dictionary with the
   * correct page number so that the objects do not build up in the
   * XREF cache. */
  if ( ! dictmatch( pages, resource_dict ) )
    return FALSE ;
  resource = resource_dict[resource_dict_Resources].result ;

  if ( ( page_basic_dict[ epage_basic_type ].result == NULL ) ||
       ( oNameNumber( *(page_basic_dict[ epage_basic_type ].result) ) == NAME_Pages )) {
    int32 i , len ;
    OBJECT *olist ;
    Bool result;

    if (!pdf_dictmatch( pdfc , pages , pages_dict ))
      return FALSE;

    if (pages_dict[pages_dict_Kids].result) {
      /* Found a Kids key, so assume we have a Pages object */

      /* possible quick skip past low pages */
      if (is_reverse_page_range) {
        /* Do nothing. */
      } else {
        if (first > 0 && !rcbn_enabled() && pages_dict[pages_dict_Count].result) {
          if (ixc->pageno + oInteger(*pages_dict[pages_dict_Count].result) < first) {
            ixc->pageno += oInteger(*pages_dict[pages_dict_Count].result) ;
            pdfxc->pageId = pres_pageId ;
            return TRUE;
          }
        }
      }

      /* The dictmatch only allows arrays (short, long and/or packed) so... */
      if (oXType(* pages_dict[pages_dict_Kids].result) < OVIRTUAL) {
        /* either ARRAY or PACKEDARRAY */
        len = theLen(* pages_dict[pages_dict_Kids].result ) ;
        olist = oArray(*pages_dict[pages_dict_Kids].result) ;
      } else {
        /* either OLONGARRAY or OLONGPACKEDARRAY */
        len = oLongArrayLen(* pages_dict[pages_dict_Kids].result ) ;
        olist = oLongArray(*pages_dict[pages_dict_Kids].result) ;
      }

      if ( resource ) {
        if ( ! pdf_add_resource( pdfc , resource ))
          return FALSE ;
      }

      if (is_reverse_page_range) {
        olist = &olist[len - 1] ;
      }

      for ( i = 0 ; i < len ; ++i ) {
        Bool walk_ok;
        Bool kid_was_page = FALSE;

        pdfxc->pageId = -pdfxc->pdfwalk_depth ;

        if (is_reverse_page_range) {
          if ((last >= 0) && (ixc->pageno < last))
            break;
        } else {
          if ((last >= 0) && (ixc->pageno > last))
            break;
        }

        if (oType(*olist) != OINDIRECT ) {
          (void) error_handler( TYPECHECK ) ;
          break;
        }

        if ( ! pdf_lookupxref( pdfc, &pages, oXRefID( *olist ), theGen(*olist), FALSE )) {
          break;
        }

        if ( pages == NULL ) {
          (void) error_handler( UNDEFINEDRESOURCE ) ;
          break;
        }

        Copy(&pdfsc->page, olist);

        pdfxc->pdfwalk_depth++;
        pdfxc->pageId = pres_pageId ;
        walk_ok = pdf_walk_page_range(pdfc, pages, pagedev, pdfsc, first, last,
                                      num_pages_in_job, is_reverse_page_range,
                                      &kid_was_page);
        pres_pageId = pdfxc->pageId ;
        pdfxc->pdfwalk_depth--;
        if ( kid_was_page &&
             ixc->XRefCacheLifetime > 0 &&
             pdfxc->pageId >= ixc->XRefCacheLifetime ) {
          pdf_sweepxrefpage( pdfc, pdfxc->pageId - ixc->XRefCacheLifetime ) ;
        }
        pdf_deferred_xrefcache_flush( pdfxc ) ;
        if ( !walk_ok )
          break;

        if (is_reverse_page_range) {
          olist-- ;
        } else {
          olist++ ;
        }
      }

      pdfxc->pageId = pres_pageId ;

      if (is_reverse_page_range) {
        result = ((last >= 0) && (ixc->pageno < last)) || (i == len);
      } else {
        result = ((last >= 0) && (ixc->pageno > last)) || (i == len);
      }

      if ( resource )
        pdf_remove_resource( pdfc ) ;

      if ( pdfxc->pdfwalk_depth > 1 ) {
        pdf_sweepxrefpage( pdfc, -pdfxc->pdfwalk_depth ) ;
      }

      return result ;
    }
  }

  /* Beyond this point, we have done any necessary recursion in the
     Pages tree to a Page object, so "pages" actually points to a
     Page. */

  pdfxc->pageId = pres_pageId ;

  if ( was_page != NULL ) {
    *was_page = TRUE ;
  }

  if ( ( page_basic_dict[ epage_basic_type ].result != NULL ) &&
       ( oNameNumber( *(page_basic_dict[ epage_basic_type ].result) ) != NAME_Page )) {
    return error_handler( SYNTAXERROR ) ;
  }
  else {
    Bool result = TRUE ;
    Bool incrementPageCount = TRUE ;
    Bool dopage = TRUE;
    ps_context_t *pscontext = pdfxc->corecontext->pscontext ;
    OBJECT pageSaveObj = OBJECT_NOTVM_NOTHING;

    HQASSERT(pdfsc, "pdfsc NULL");
    HQTRACE( pdftrace_separation_info , ( "Ignoring separations" )) ;

    if (page_basic_dict[epage_basic_presSteps].result != NULL) {
      if ( !pdfxPresStepsDictionaryDetected(pdfc) )
        return FALSE;
    }

    /* Decide whether to move on to the next logical page when recombining. The
     * existence and correctness of SeparationInfo/Pages is crucial to getting
     * both logical page boundaries and logical page range control right. If we
     * detect that a job fails to sufficiently conform we let the recombine
     * module decide the logical page boundaries based on the page colorant
     * heuristics.
     * Also sets the PlateColor for a current preseparated page regardless of recombine.
     */
    if ( page_basic_dict[ epage_basic_separationinfo ].result ) {
      if ( !pdf_walk_separation_info(pdfc,
                                     page_basic_dict[ epage_basic_separationinfo ].result,
                                     &separation_info) )
        return FALSE;
      else {
        Bool valid = TRUE;

        valid = pdf_check_separation_info(&separation_info, &pdfsc->page);

        if ( !valid ) {
          if ( ixc->strictpdf )

            return FALSE;
          pdfsc->separationinfo_dubious = TRUE;
        }

        if ( rcbn_enabled() && separation_info.pages != NULL ) {
          if ( pdfsc->previous_pages != NULL ) {  /* Excludes the first page */
            if ( !compare_objects( separation_info.pages, pdfsc->previous_pages )) {
              /* We have detected different SeparationInfo/Pages objects on
               * neighbouring pages. Normally, we would interpret this as meaning
               * the job has moved on to the next logical page, so we would be
               * wise to inform the recombine module of this.
               * If jobs obeyed the spec. then that would be fine. Unfortunately,
               * there are jobs that only list one page (the current one), such as
               * those produced by Distiller from preseparated postscript devoid of
               * %%QRKPageBegin: comments (and friends) to give a clue about the
               * logical structure. For these, we must avoid calling recombineshowpage_
               * and rely on recombine heuristics.
               */
              if (theLen(*pdfsc->previous_pages) > 1 ||
                  theLen(*separation_info.pages) > 1 ||
                  pdfsc->previous_colorant == separation_info.device_colorant) {
                /* Tell the rip to throw the current logical page */
                if ( !recombineshowpage_(pscontext) )
                  return FALSE ;
              }
            }
            else
              incrementPageCount = FALSE ;
          }
          pdfsc->previous_pages = separation_info.pages ;
          pdfsc->previous_colorant = separation_info.device_colorant ;
        }

        pagedev->PlateColor = separation_info.device_colorant;
        if (separation_info.color_space && valid) {
          tryfetchCMYKequiv(pdfc,
                  separation_info.color_space, pagedev->PlateColor);
        }
      }
    }
    else {
      OBJECT *plateColor = page_basic_dict[ epage_basic_platecolor ].result;

      if ( plateColor != NULL ) {
        if ( oType(*plateColor) == OSTRING ) {
          pagedev->PlateColor = cachename( oString(*plateColor),
                                           ( uint32 )theLen( *plateColor )) ;
          if ( pagedev->PlateColor == NULL )
            return FALSE;
        } else {
          pagedev->PlateColor = oName(*plateColor); /* should be a name */
        }
      }
      else
        pagedev->PlateColor = NULL;

      pdfsc->separationinfo_missing = TRUE;
    }

    /* The shenanigans with printPage here are because we only want 1 warning
     * for a skipped logical page with recombine. printPage is sticky between
     * separations, otherwise we'd get one warning per job page. */
    if ( incrementPageCount ) {
      if (is_reverse_page_range) {
        ixc->pageno-- ;
        HQASSERT(ixc->pageno >= 1, "pageno is less than zero?") ;
      } else {
        ixc->pageno++ ;
      }

      /* check for potential page reference */
      if ( ! pdf_pageref_required( pdfc, &pdfsc->printPage ))
        return FALSE ;
    }

    /* return if skipped for a page reference */
    if (ixc->pPageRef != NULL) {
      if ( !pdfsc->printPage )
        return TRUE;
    } else {
      /* return if out of range */
      if (is_reverse_page_range) {
        if ( (first >= 0 && ixc->pageno > first) ||
             (last >= 0 && ixc->pageno < last) ) {
          return TRUE;
        }
      } else {
        if ( (first >= 0 && ixc->pageno < first) ||
             (last >= 0 && ixc->pageno > last) ) {
          return TRUE;
        }
      }
    }

    if ( ! pdfxCheckPageBounds( pdfc , pagedev ))
      return FALSE ;

    /* Update default attributes for page boundaries if none supplied */
    if ( ! pagedev->CropBox )
      pagedev->CropBox = pagedev->MediaBox ;
    if ( ! pagedev->BleedBox )
      pagedev->BleedBox = pagedev->CropBox ;
    if ( ! pagedev->TrimBox )
      pagedev->TrimBox = pagedev->CropBox ;
    if ( ! pagedev->ArtBox )
      pagedev->ArtBox = pagedev->CropBox ;

    /* Setup the media box, the crop box and the rotation. dopage is false if
       the mediabox is less than 72 in one or both dimensions. Only do this if
       the ignore_setpagedevice flag is false */
    if ( ! ixc->ignore_setpagedevice ) {
      if ( ! pdf_setpagedevice( pdfxc , pagedev , & dopage, translation ) )
        return FALSE ;
    }

    if ( pagedev->UserUnit != NULL) {
      USERVALUE scale = (USERVALUE) object_numeric_value( pagedev->UserUnit );

      if ( !stack_push_real(scale, &operandstack) ||
           !dup_(pscontext) ||
           !scale_(pscontext) ) {
        return FALSE ;
      }
    }

    if ( pagedev->PlateColor ) {
      /* We have a colorant name, e.g., /Cyan, which can be
       * passed to detect_setsystemparam_separation.
       */
      if ( ! detect_setsystemparam_separation( pagedev->PlateColor,
                                               gstateptr->colorInfo ))
        return FALSE;
    }

#define return (InvalidReturn)

    ixc->page_continue = TRUE ;

    /* Adobe print a blank page if the mediabox is smaller than 72 in
     * at least one dimension. */
    while ( result && ixc->page_continue ) {
      result = pdf_rr_setup_page( pdfc ) ;

#ifndef REMOVE_PARAM_WHEN_POSSIBLE
      /* Unfortunately, PoorShowPage is required for compatibility with the
         plugin of an important customer. It means the internal save/restore can
         be either around just the page interpretation, or around the showpage
         as well. The former is the way we'd like it. The latter means that the
         effects of running BeginPage & EndPage procedures get restored away. */
      if (pdfxc->corecontext->pdfparams->PoorShowPage) {
        result = result && save_(pscontext) ;
        if ( result ) {
          Copy( &pageSaveObj, theTop( operandstack ) ) ;
          pop( &operandstack ) ;
        }

        /* Squirrel the default gstate for Default extgstate settings from the
           save placed around the pdf page. */
        HQASSERT(gstackptr->gType == GST_SAVE, "Expected gstate to be a SAVE");
        ixc->pPageStartGState = gstackptr;
      }
#endif

      if ( result && ( ! ixc->strictpdf || dopage )) {
        DL_STATE *page = pdfc->corecontext->page ;

        probe_begin(SW_TRACE_PDF_PAGE, ixc->pageno);

        /* If enabled, obtain embedded PJTF trapping parameters.
         * Beware of moving it. This contains a possible call to
         * setpagedevice.
         */
        if (ixc->pPJTFinfo != NULL && !ixc->ignore_setpagedevice) {
          if ( ! pdf_jt_get_trapinfo( pdfc, pagedev->MediaBox ))
            result = FALSE;
        }

        if ( result ) {
          uint32 start_groupid, end_groupid;

          start_groupid = page->currentGroup != NULL
            ? groupId(page->currentGroup) : HDL_ID_INVALID;

          result = walkContentsInMarkingContext(pdfc, pagedev, pages, resource,
                                                translation);

          end_groupid = page->currentGroup != NULL
            ? groupId(page->currentGroup) : HDL_ID_INVALID;

          /* Set a new page group if the page group changed in
           * walkContentsInMarkingContext. */
          result = result &&
            (start_groupid == end_groupid ||
             dlSetPageGroup(page,
                            onull /* default page group colorspace */,
                            DEFAULT_PAGE_GROUP_KNOCKOUT));
        }

        probe_end(SW_TRACE_PDF_PAGE, ixc->pageno);

        HQASSERT(page == pdfc->corecontext->page,
                 "Page changed in context during PDF content walk") ;
      }

      /* Only perform a 'showpage' if ignore_showpage is false. */
      if ( result ) {
        if ( ixc->page_discard ) {
          result = (pdf_execop(pscontext, NAME_erasepage) &&
                    initgraphics_(pscontext)) ;
        }
        else if ( !ixc->ignore_showpage ) {
          result = pdf_execop(pscontext, NAME_showpage) ;
        }
      }

#ifndef REMOVE_PARAM_WHEN_POSSIBLE
      if (pdfxc->corecontext->pdfparams->PoorShowPage) {
        /* We don't want to restore if there was an error in the interpreter, as
           this would restore away the $error dictionary setup by handleerror()
           in the interpreter call. The server loop will automatically restore
           to the server level. */
        result = result &&
          push( &pageSaveObj, &operandstack ) && restore_(pscontext);
      }
#endif

      HQASSERT(!result || ixc->cached_fontdetails == NULL,
               "Expected pdf font cache to have been cleared");
    }

    /* Increment the current page ID. */
    pdfxc->pageId ++;

#undef return

    return result ;
  }
}

/*----------------------------------------------------------------------------*/

#define PPPW_BUFLEN 1024

#define MONITOR0(str) MACRO_START \
    if (i < PPPW_BUFLEN) \
      i += swncopyf(buffer + i, PPPW_BUFLEN - i, (uint8*)(str)) ; \
    if (i > PPPW_BUFLEN) \
      i = PPPW_BUFLEN ; \
  MACRO_END

#define MONITOR1(str, a) MACRO_START \
    if (i < PPPW_BUFLEN) \
      i += swncopyf(buffer + i, PPPW_BUFLEN - i, (uint8*)(str), (a)) ; \
    if (i > PPPW_BUFLEN) \
      i = PPPW_BUFLEN ; \
  MACRO_END

#define MONITOR2(str, a, b) MACRO_START \
    if (i < PPPW_BUFLEN) \
      i += swncopyf(buffer + i, PPPW_BUFLEN - i, (uint8*)(str), (a), (b)) ; \
    if (i > PPPW_BUFLEN) \
      i = PPPW_BUFLEN ; \
  MACRO_END

/* Report which pages in the PageRange are printed and note those which are out
   of range */
static void pdf_print_pagerange_warnings(int32 maxpage, OBJECT * pagerange)
{
  int32 first, last;
  Bool out_range;
  Bool comma;
  int32 tick;
  OBJECT * el;
  uint8 buffer[PPPW_BUFLEN] ;
  int i = 0 ;

  out_range = FALSE;
  buffer[0] = '\0';

  comma = FALSE;
  for (el = oArray(*pagerange), tick = theLen(*pagerange);
         (tick > 0);tick--, el++) {

    if (oType(*el) == OINTEGER) {
      first = oInteger(*el);
      if (first <= maxpage) {
        MONITOR1(comma ? ", %d" : " %d", first) ;
        comma = TRUE;
      } else {
        out_range = TRUE;
      }
    } else {
      HQASSERT(oType(*el) == OARRAY,"array required");
      switch (theLen(*el)) {
        case 1:
          first = oInteger(oArray(*el)[0]);
          if (first <= maxpage) {
            MONITOR2(comma ? ", %d-%d" : " %d-%d", first, maxpage) ;
            comma = TRUE;
          } else {
            out_range = TRUE;
          }
          break;
        case 2:
          first = oInteger(oArray(*el)[0]);
          last = oInteger(oArray(*el)[1]);
          if (first <= maxpage) {
            if (last > maxpage) {
              last = maxpage;
              out_range = TRUE;
            }
            if (i < PPPW_BUFLEN) {
              if (first == last)
                MONITOR1(comma ? ", %d" : " %d", first) ;
              else
                MONITOR2(comma ? ", %d-%d" : " %d-%d", first, last) ;
            }
            if (i > PPPW_BUFLEN)
              i = PPPW_BUFLEN ;
            comma = TRUE;
          } else {
            /* first > maxpage */
            out_range = TRUE;
            first = maxpage;
            if (i < PPPW_BUFLEN) {
              if (last < maxpage) {
                MONITOR2(comma ? ", %d-%d" : " %d-%d", first, last) ;
                comma = TRUE;
              }
              else if (last == maxpage) {
                MONITOR1(comma ? ", %d" : " %d", last) ;
                comma = TRUE;
              }
            }
            if (i > PPPW_BUFLEN)
              i = PPPW_BUFLEN ;
          }
          break;
        default:
          HQFAIL("bad page range.");
      }
    }
  }

  monitorf((uint8*)UVM("%%%%[ Warning: Page range restricts printing to the following page(s): %s from an available %d]%%%%\n"),
           buffer, maxpage) ;
  i = 0 ;

  if (out_range) {
    if (i > PPPW_BUFLEN)
      i = PPPW_BUFLEN ;
    comma = FALSE;
    for (el = oArray(*pagerange), tick = theLen(*pagerange);
          (tick > 0);tick--, el++) {
      if (oType(*el) == OINTEGER) {
        first = oInteger(*el);
        if (first > maxpage) {
          MONITOR1(comma ? ", %d" : " %d", oInteger(*el));
          comma = TRUE;
        }
      } else {
        HQASSERT(oType(*el) == OARRAY,"array required");
        switch (theLen(*el)) {
          case 1:
            first = oInteger(oArray(*el)[0]);
            last = maxpage;
            if (first > maxpage) {
              MONITOR1(comma ? ", %d" : " %d", first);
              comma = TRUE;
            }
            break;
          case 2:
            first = oInteger(oArray(*el)[0]);
            last = oInteger(oArray(*el)[1]);
            out_range = first > maxpage;
            out_range = out_range || (last > maxpage);
            if (out_range) {
              if (first <= maxpage)
                first = maxpage + 1;
              if (last <= maxpage)
                last = maxpage + 1;
              if (first == last)
                MONITOR1(comma ? ", %d" : " %d", first);
              else
                MONITOR2(comma ? ", %d-%d" : " %d-%d", first, last);
              comma = TRUE;
            }
            break;
          default:
            HQFAIL("bad page range.");
        }
      }
    }
    monitorf((uint8*)UVM("%%%%[ Warning: The following page(s) out of range:%s ]%%%%\n"),
             buffer) ;
  }
}

#undef PPPW_BUFLEN
#undef MONITOR2
#undef MONITOR1
#undef MONITOR0

/*----------------------------------------------------------------------------*/
/** Return the document catalog dictionary.
*/
static Bool getCatalogDict(PDFCONTEXT* pdfc, PDF_IXC_PARAMS* ixc,
                           OBJECT* catalogDict)
{
  OBJECT* catalogPointer;
  HQASSERT(oType(ixc->pdfroot) == OINDIRECT, "root object not OINDIRECT");

  if (! pdf_lookupxref(pdfc, &catalogPointer, oXRefID(ixc->pdfroot),
                       theGen(ixc->pdfroot), FALSE))
    return FALSE;

  if (catalogPointer == NULL)
    return error_handler(UNDEFINEDRESOURCE);

  *catalogDict = *catalogPointer;
  if (oType(*catalogDict) != ODICTIONARY)
    return error_handler(TYPECHECK);

  return TRUE;
}

Bool pdf_walk_pages( PDFCONTEXT *pdfc , OBJECT *pages ,
                     PDF_SEPARATIONS_CONTROL *pdfsc)
{
  Bool ret = TRUE ;
  PDF_IXC_PARAMS *ixc ;
  PDFXCONTEXT *pdfxc ;
  int32 tick ;
  OBJECT * el ;
  OBJECT * arr ;
  int32 num_pages = -1 ; /* Invalid value as we should not use it. */
  Bool is_reverse_page_range = FALSE ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT(pdfxc->pdfwalk_depth == 1, "Not at top-level of pdf-walk chain");

  if (ixc->pagerange == NULL || ixc->pPageRef != NULL) {
    /* print all pages */
    return pdf_walk_page_range( pdfc, pages, NULL, pdfsc, -1, -1,
                                num_pages, is_reverse_page_range , NULL ) ;
  }


  /* Work our way over the page range array from left to right. */
  for (el = oArray(*ixc->pagerange), tick = theLen(*ixc->pagerange);
       ret && (tick > 0); tick--, el++) {
    int32 first ;
    int32 last ;

    is_reverse_page_range = FALSE ;
    ixc->pageno = 0;
    if (oType(*el) == OINTEGER) {
      /* print a single page */
      ret = pdf_walk_page_range( pdfc, pages, NULL, pdfsc,
                                 oInteger(*el), oInteger(*el),
                                 num_pages, is_reverse_page_range, NULL );
    } else {
      HQASSERT(oType(*el) == OARRAY,"array required");

      arr = oArray(*el);
      switch (theLen(*el)) {
      case 1:
        if (oType(*arr) != OINTEGER)
          return error_handler( TYPECHECK );
        /* print from page N to the end */
        ret = pdf_walk_page_range( pdfc, pages, NULL, pdfsc, oInteger(*arr), -1,
                                   num_pages, is_reverse_page_range, NULL );
        break;
      case 2:
        if ((oType(arr[0]) != OINTEGER)||(oType(arr[1]) != OINTEGER))
          return error_handler( TYPECHECK );

        first = oInteger(arr[0]) ;
        last = oInteger(arr[1]) ;

        /* Hmm, we need to know how may pages there are so we can
           reverse them. So we need to count the pages first. */
        if (first > last) {
          ixc->pageno = 0;
          pdfsc->previous_pages = NULL;
          if (! pdf_count_pages(pdfc, ixc, pages, pdfsc))
            return FALSE;

          num_pages = ixc->pageno ;
          /* We increment beyond the page count simply because this
             is how the code works. Basically it does an immediate
             decrement. When counting in the positive way, pageno is
             zero and gets incremented so its the same principle, if
             a little confusing, but only because people who write
             code in C are used to counting from zero so this is how
             its going to stay for now. */
          ixc->pageno++ ;
          is_reverse_page_range = TRUE ;
        }

        /* print from page N to page M */
        ret = pdf_walk_page_range( pdfc, pages, NULL, pdfsc, first, last,
                                   num_pages, is_reverse_page_range, NULL ) ;
        break;
      default:
        HQFAIL("pdf_walk_pages: bad page range.");
        return error_handler(RANGECHECK);
      }
    }
  }


  /* if we're not using a page reference then allow us to report
     which pages we are printing */
  if (ret && ixc->pPageRef == NULL && ixc->pagerange != NULL) {
    ixc->pageno = 0;
    pdfsc->previous_pages = NULL;
    if (!pdf_count_pages(pdfc, ixc, pages, pdfsc))
      return FALSE;

    if ( ixc->WarnSkippedPages && (pdfxc->pageId > 0) ) {
      pdf_print_pagerange_warnings(ixc->pageno, ixc->pagerange);
    }
  }

  return ret;
}

/* ----------------------------------------------------------------------
 * SeparationInfo dictionary is a PDF1.3 feature.
 */

enum {
  separation_info_DeviceColorant,
  separation_info_ColorSpace,
  separation_info_Pages,
  separation_info_n_entries
} ;
static NAMETYPEMATCH separation_info_dict[separation_info_n_entries + 1] = {
  { NAME_DeviceColorant,         3 , { ONAME, OSTRING, OINDIRECT }},
  { NAME_ColorSpace | OOPTIONAL, 4 , { ONAME, OARRAY, OPACKEDARRAY, OINDIRECT }},
  { NAME_Pages | OOPTIONAL,      2 , { OARRAY, OINDIRECT }},
  DUMMY_END_MATCH
} ;

static Bool pdf_walk_separation_info( PDFCONTEXT *pdfc , OBJECT *dict ,
                                      PDF_SEPARATION_INFO *pdfsi )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( pdfsi, "pdfsi NULL in pdf_walk_separation_info");
  HQASSERT( dict, "dict NULL in pdf_walk_separation_info");
  HQASSERT( oType(*dict) == ODICTIONARY,
            "dict must be a dictionary in pdf_walk_separation_info");

  if ( !pdfxPreseparatedJob( pdfc ))
    return FALSE;

  /* Parse the separation info dictionary */
  if ( !pdf_dictmatch( pdfc , dict , separation_info_dict ))
    return FALSE;

  /* Pages is required according to the spec, but Acrobat happily proceeds without it */
  if (ixc->strictpdf && !separation_info_dict[separation_info_Pages].result)
    return error_handler(UNDEFINED) ;

  pdfsi->color_space = separation_info_dict[ 1 ].result;
  pdfsi->pages = separation_info_dict[ 2 ].result;

  /* DeviceColorant should be name or string - coerce to name if string */
  if ( oType(*(separation_info_dict[ 0 ].result)) == OSTRING ) {
    pdfsi->device_colorant = cachename( oString(*(separation_info_dict[ 0 ].result)),
                                        theLen(*separation_info_dict[ 0 ].result) );
    if ( pdfsi->device_colorant == NULL )
      return FALSE;
  }
  else {
    HQASSERT( oType(*(separation_info_dict[ 0 ].result)) == ONAME, "Invalid type");
    pdfsi->device_colorant = oName(*(separation_info_dict[ 0 ].result));
  }

  return TRUE;
}


/**
 * pdf_check_separation_info() checks that 'currentPage' exists in SeparationInfo/
 * Pages array and that the device colorant in a separation info object matches
 * the given colorspace, either Separation or one of the DeviceN colorants.
 */
static Bool pdf_check_separation_info(PDF_SEPARATION_INFO*  pdfsi,
                                      OBJECT *currentPage )
{
  int32   i;
  int32   num_colorants;
  OBJECT* cs;
  OBJECT* oarray_cs;
  OBJECT* oarray_colorants;
  int32 length;

  HQASSERT((pdfsi != NULL),
           "pdf_check_separation_info: NULL separation info pointer");
  HQASSERT((pdfsi->device_colorant != NULL),
           "pdf_check_separation_info: NULL device_colorant in separation info");
  HQASSERT((currentPage != NULL),
           "pdf_check_separation_info: NULL currentPage");
  HQASSERT((oType(*currentPage) == OINDIRECT),
           "pdf_check_separation_info: invalid Type");

  /* Test for inclusion of current page in SeparationInfo
     This should not be OPTIONAL but is. */
  if ( pdfsi->pages ) {
    length = theLen(*pdfsi->pages);
    for ( i = 0; i < length; i++) {
      if (compare_objects( &oArray(*pdfsi->pages)[i], currentPage) )
        break;
    }
    if ( i == length )
      return error_handler(RANGECHECK);
  }

  /* ColorSpace is an optional array with at least 1 element in it */
  cs = pdfsi->color_space;
  if (cs != NULL) {
    if ( oType(*cs) != OARRAY ) {
      return error_handler(TYPECHECK);
    }
    if ( theLen(*cs) == 0 ) {
      return error_handler(RANGECHECK);
    }

    /* Check first element of colorspace array is a name */
    oarray_cs = oArray(*cs);
    if ( oType(oarray_cs[0]) != ONAME ) {
      return error_handler(TYPECHECK);
    }

    /* ColorSpace must be Separation or DeviceN */
    if ( oName(oarray_cs[0]) == system_names + NAME_Separation ) {
      /* Separation colorspace must have 4 elements */
      if ( theLen(*cs) != 4 ) {
        return error_handler(RANGECHECK);
      }

      HQTRACE( (oType(oarray_cs[1]) == OSTRING),
               ("pdf_check_separation_info: separation colorant is a string - forward to core RIP group please" ));

      if (oType(oarray_cs[1]) != ONAME )
        return error_handler(TYPECHECK);

      if ( oName(oarray_cs[1]) != pdfsi->device_colorant )
        return error_handler(RANGECHECK);

    } else if ( oName(oarray_cs[0]) == system_names + NAME_DeviceN ) {

      HQFAIL("pdf_check_separation_info: pre-separated DeviceN PDF job - forward to core RIP group please");

      /* DeviceN colorspace must have 4 or 5 elements */
      if ( (theLen(*cs) != 4) && (theLen(*cs) != 5) ) {
        return error_handler(RANGECHECK);
      }

      if (oType(oarray_cs[1]) != OARRAY )
        return error_handler(TYPECHECK);

      oarray_colorants = oArray(oarray_cs[1]);
      num_colorants = theLen(oarray_cs[1]);
      for ( i = 0; i < num_colorants; i++ ) {

        HQTRACE((oType(oarray_colorants[i]) == OSTRING),
                ("pdf_check_separation_info: DeviceN colorant array contains string - forward to core RIP group please"));

        if (oType(oarray_colorants[i]) != ONAME )
          return error_handler(TYPECHECK);

        if (oName(oarray_colorants[i]) == pdfsi->device_colorant )
          return TRUE;
      }

      return error_handler(RANGECHECK);
    } else {

      return error_handler(TYPECHECK);
    }
  }

  return TRUE;
} /* Function pdf_check_separation_info */


/* ---------------------------------------------------------------------- */
static Bool pdf_walk_tree( PDFCONTEXT *pdfc )
{
  Bool ret = FALSE ;
  OBJECT *pages ;
  OBJECT catalog = OBJECT_NOTVM_NOTHING ;
  PDF_SEPARATIONS_CONTROL pdfsc = { 0 } ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT contextSaveObj = OBJECT_NOTVM_NOTHING;
  ps_context_t *pscontext ;
  Bool saved_ignore_setpagedevice, saved_ignore_showpage ;

  enum {
    e_cat_dict_type,
    e_cat_dict_pages,
    e_cat_dict_OCProperties,
    e_cat_dict_acroform,
    e_cat_dict_openaction,
    e_cat_dict_outputintents,
    e_cat_dict_pagelabels,
    e_cat_dict_JT,
    e_cat_dict_AA,
    e_cat_dict_max
  };

  static NAMETYPEMATCH catalog_dict[e_cat_dict_max + 1] = {
    { NAME_Type ,                  2, { ONAME, OINDIRECT }},
    { NAME_Pages,                  2, { OINDIRECT, ODICTIONARY }},
    { NAME_OCProperties | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_AcroForm | OOPTIONAL,   2, { ODICTIONARY, OINDIRECT }},
    { NAME_OpenAction | OOPTIONAL, 4, { ODICTIONARY, OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_OutputIntents | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_PageLabels | OOPTIONAL, 2, {  ODICTIONARY, OINDIRECT }},
    { NAME_JT | OOPTIONAL,         2, { ODICTIONARY, OINDIRECT }},
    { NAME_AA | OOPTIONAL,         2, { ODICTIONARY, OINDIRECT }},
    DUMMY_END_MATCH
  } ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;
  pscontext = pdfxc->corecontext->pscontext;

  /* This jettisons all the objects which were loaded into the xref
     cache by the PDF Checker or indeed any other PS which called
     getPDFobject_ before any processing of pages began. */

  pdf_sweepxrefpage( pdfc, -1 ) ;
  pdf_deferred_xrefcache_flush( pdfxc ) ;

  saved_ignore_setpagedevice = ixc->ignore_setpagedevice ;
  saved_ignore_showpage = ixc->ignore_showpage ;

  if (! getCatalogDict(pdfc, ixc, &catalog))
    return FALSE;

  if ( ! ixc->strictpdf )
    catalog_dict[ e_cat_dict_type ].name |= OOPTIONAL ;
  else
    catalog_dict[ e_cat_dict_type ].name &= ~OOPTIONAL ;

  if ( ! pdf_dictmatch( pdfc , &catalog , catalog_dict ))
    return FALSE ;

  HQASSERT( catalog_dict[ e_cat_dict_type ].result || ! ixc->strictpdf ,
            "Catalog Type entry missing" ) ;
  HQASSERT( catalog_dict[ e_cat_dict_pages ].result ,
            "Catalog Pages entry missing" ) ;

  if ( catalog_dict[ e_cat_dict_type ].result != NULL &&
       ( oName( *(catalog_dict[e_cat_dict_type].result) ) != system_names + NAME_Catalog ) &&
       ixc->strictpdf )
    return namedinfo_error_handler( RANGECHECK , NAME_Type ,
                                    catalog_dict[ e_cat_dict_type ].result ) ;

  pages = catalog_dict[ e_cat_dict_pages ].result ;

  if (catalog_dict[e_cat_dict_OCProperties].result != NULL) {
    /* hey we have optional content. Go find the OCProperties etc.*/
    if (!pdf_oc_getproperties( pdfc,pdfxc->mm_structure_pool, &ixc->oc_props,
                               catalog_dict[e_cat_dict_OCProperties].result ))
      return FALSE;
  }

  /* If the AcroForm entry is defined, load its values */
  if (catalog_dict[e_cat_dict_acroform].result != NULL) {
    if (!pdf_get_AcroForm( pdfc, ixc, catalog_dict[e_cat_dict_acroform].result ))
      return FALSE;
  }

  /* Are any actions present? */
  if (catalog_dict[e_cat_dict_openaction].result != NULL ||
      catalog_dict[e_cat_dict_AA].result != NULL) {
    if (! pdfxActionDetected( pdfc ))
      return FALSE;
  }

#define return (InvalidReturn)
  /* Protect the current gstate from changes within the pdf context */
  if ( !save_(pscontext) ) {
    FAILURE_GOTO( cleanup ) ;
  }
  Copy( &contextSaveObj, theTop( operandstack ) ) ;
  pop( &operandstack ) ;
  /* Augment the context's savelevel, to be picked up by pdf filestreams */
  pdfxc->savelevel = pdfc->corecontext->savelevel ;

  /* Initialise text state here, not in pdf_in_begin_marking_context() as that
     stops Forms from inheriting the state from their parent. [12777] */
  pdf_inittextstate() ;

  if ( ! pdfxProcessOutputIntents( pdfc, catalog_dict[e_cat_dict_outputintents].result )) {
    FAILURE_GOTO( cleanup ) ;
  }
  if ( !gsc_setAdobeRenderingIntent(gstateptr->colorInfo,
                                    pdfxc->corecontext->pdfparams->AdobeRenderingIntent) ) {
    FAILURE_GOTO( cleanup ) ;
  }

  /* Disable trapping if the info dict in the job says it is already trapped.
   * Siting it here is important because it will be restored by the grestore at
   * the end of this function.
   */
  if (ixc->infoTrapped == INFO_TRAPPED_TRUE) {
    char *psString = "<< /OverrideKeys << /Trapping true >> /Trapping false >> setpagedevice";
    if ( ! run_ps_string( (uint8 *) psString )) {
      FAILURE_GOTO( cleanup ) ;
    }
  }

  /* If JobTicket info is present, go retain what's needed.
     Currently, this is only done to extract any trapping parameters
     that are defined, and so we only do this now if trapping is
     enabled. */
  if (catalog_dict[e_cat_dict_JT].result != NULL &&
      (pdfxc->corecontext->systemparams->TrapProLite ||
       pdfxc->corecontext->systemparams->TrapPro)) {
    if ( ! pdf_jt_contents( pdfc, catalog_dict[e_cat_dict_JT].result )) {
      FAILURE_GOTO( cleanup ) ;
    }
  }

  /* If the PageLabels dictionary is present, retain a link to it
     in case we're asked (later) to match specific pages against
     a page's label. */
  ixc->pPageLabelsDict = catalog_dict[e_cat_dict_pagelabels].result;

  /* Use a default of false, these will be made true in a leaf page if
   * appropriate and that value will be picked up in the warnings
   * based on it */
  pdfsc.separationinfo_missing = FALSE;
  pdfsc.separationinfo_dubious = FALSE;
  pdfsc.page = onothing ; /* set slot properties */
  HQTRACE( pdftrace_separation_info ,
           ( rcbn_enabled()
             ? "Recombining, so prepare to notice SeparationInfo>Pages"
             : "Not recombining, so ignore SeparationInfo>Pages" ));

  /* Ready to start the first page: up to now, anything loaded into
     the xref cache will have a lastAccessId of -1 and will live for
     the lifetime of the execution context unless explicitly
     purged. Make sure every xref object that will be needed for the
     whole job (e.g. job ticket) is in the cache before here - bearing
     in mind that such memory usage should be kept as low as
     possible. */
  pdfxc->pageId = 0;

  if ( ! pdf_rr_walk_pages( pdfc , pages , & pdfsc )) {
    FAILURE_GOTO( cleanup ) ;
  }

  pdfsc.previous_pages = NULL;
  pdfsc.previous_colorant = NULL;

  ret = pdf_walk_pages( pdfc, pages, &pdfsc ) ;

  /* WARNING: after this point (i.e. pdf_walk_pages()), do NOT refer
     to any static data (i.e. catalog_dict) due to recursion (via the
     pdf /Ref route) changing its values. */

 cleanup:

  ret = pdfxCloseRegistryProfile( pdfc ) && ret;

  if ( ret ) {
    if ( ixc->pagerange != NULL ) {
      if ( pdfxc->pageId == 0 ) {
        /* The requested page range meant no pages were output at all. */
        monitorf( UVS("%%%%[ Warning: No pages in requested page range ]%%%%\n") ) ;
      }

      if ( rcbn_enabled() && !rcbn_composite_page() ) {
        if ( pdfsc.separationinfo_missing ) {
          /* In a preseparated job, each set of separations constitutes one logical
             page providing SeparationInfo details are present.  If there are no
             SeparationInfo objects, pages and separations are treated equivalently
             and therefore page range may not be applied as expected. */
          monitorf( UVS("%%%%[ Warning: SeparationInfo details missing from preseparated job; "
                        "requested page range may not be applied correctly ]%%%%\n") ) ;
        }
      }
    }

    if ( rcbn_enabled() && !rcbn_composite_page() ) {
      if ( pdfsc.separationinfo_dubious ) {
        /* In a preseparated job, each separation must be present in it's
           associated SeparationInfo. If a page is missing from SeparationInfo
           then we'll carry on regardless, but who knows what we'll get out. */
        monitorf( UVS("%%%%[ Warning: SeparationInfo details invalid in preseparated job; "
                      "pages may not recombine correctly ]%%%%\n") ) ;
      }
    }
  }

  /* Restore the state at the beginning of pdf_walk_tree(), especially for the
   * gstate and page device */
  if (oType(contextSaveObj) == OSAVE) {
    if (ret) {
      if (oType(contextSaveObj) == OSAVE)
        ret = push( &contextSaveObj, &operandstack ) && restore_(pscontext);
    }
    else {
      /* If we avoided or failed to call restore we must still restore the
      fileio system; PS filters are allowed to reference PDF filters - this call
      ensures that no PS filters hang on to PDF filters in this context.
      Ditto for the color module because it can cache references to files */
      (void) gsc_restore(save_level(oSave(contextSaveObj)));
      fileio_restore(save_level(oSave(contextSaveObj)));

      /* In low memory, the garbage collector may run when we return to the
      interpreter. At this stage we may have references to PDF memory on the
      stacks (because we avoided restoring), which will be walked over by the
      collector. So to prevent this, as a nasty hack, we disable GC. This will
      be re-enabled when the restore is finally done by the server loop. */
      pdfxc->corecontext->userparams->VMReclaim = -2;
    }
  }

  /* This is required because currentpdfcontext may otherwise dereference it
   * when VM holding the PageRange entry in pdfexecid dict is out of scope. */
  ixc->pagerange = NULL;
  /* Must also ensure pageId goes back to its initial value as it may be used
   * by PDFChecker or any other code that calls getPDFobject_ directly, and
   * is needed to ensure the correct purge lifetime of any xref objects.
   */
  pdfxc->pageId = -1;

  /* Remove any remaining cache entries associated with any page, so
     that if we come here again (perhaps resulting from multiple
     pdfexecid calls) we don't get confused over page indices and
     cache lifetimes. */
  pdf_xrefreset( pdfc ) ;

#undef return

  return ret;
}

/* ---------------------------------------------------------------------- */
static Bool pdf_find_trailer( PDFCONTEXT *pdfc )
{
  FILELIST *flptr ;
  DEVICELIST *dev ;

  Hq32x2 filepos ;

#define LINE_SIZE     16
#define TRAILER_SIZE 512
  uint8 linebuf[ LINE_SIZE ] ;
  uint8 readbuf[ TRAILER_SIZE ] ;
  uint8 *lineptr , *lineend ;
  uint8 *readptr , *readend ;

  uint8 ch ;
  Bool shortbadfile = FALSE ;

  int32 size ;
  Bool result ;

  Bool goteof = FALSE ;
  Bool gotxrefaddr = FALSE ;

  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  flptr = pdfxc->flptr ;
  HQASSERT( flptr , "flptr field NULL in pdf_find_trailer" ) ;
  dev = theIDeviceList( flptr ) ;
  HQASSERT( dev , "dev field NULL in pdf_find_trailer" ) ;

  /* We perform this as follows:
   * a) Read a [suitable] buffer.
   * b) Scan back two lines and check for:
   *     {{BOF}|{whitespace}+}
   *     startxref{whitespace}+
   *     nnnn{whitespace}+
   *     %%EOF{whitespace}+{^(%%EOF)}*
   *    i.e. search backwards for the second half of the trailer.
   * c) The xref table is obtained from nnnn, and later seeked to
   */

  lineend = linebuf + LINE_SIZE ;
  lineptr = lineend ;
  filepos = pdfxc->fileend ;
  for (;;) {
    size = TRAILER_SIZE ;
    Hq32x2SubtractInt32( & filepos , & filepos , size ) ;
    result = Hq32x2Compare( & filepos , & pdfxc->filepos ) ;
    if ( result <= 0 ) {
      if ( shortbadfile ) {
        if ( ! gotxrefaddr )
          return error_handler( SYNTAXERROR ) ;
        if ( HqMemCmp( lineptr , CAST_PTRDIFFT_TO_INT32(lineend - lineptr) ,
                       ( uint8 * )"startxref" , 9) != 0 )
          return error_handler( SYNTAXERROR ) ;
        return TRUE ;
      }
      shortbadfile = TRUE ;
      /* Need to work out how much to consume (<TRAILER_SIZE!) */
      Hq32x2AddInt32( & filepos , & filepos , size ) ;
      Hq32x2Subtract( & filepos , & filepos , & pdfxc->filepos ) ;
      size = Hq32x2AssertToInt32( & filepos ) ;
      filepos = pdfxc->filepos ;
    }
    if ( ! (*theISeekFile( dev ))( dev , theIDescriptor( flptr ) ,
                                   & filepos , SW_SET ))
      return device_error_handler( dev ) ;
    if ((*theIReadFile( dev ))( dev , theIDescriptor( flptr ) ,
                                readbuf ,
                                size ) != size )
      return device_error_handler( dev ) ;
    /* Go back newline at a time until we find what we want. */
    readend = readbuf + size ;
    readptr = readend ;
    while ( readptr != readbuf ) {
      ch = (*(--readptr)) ;
      if ( IsWhiteSpace(( int32 )ch )) { /* Got a token */
        if ( lineptr != lineend ) {
          if ( ! goteof ) {
            if ( HqMemCmp( lineptr , CAST_PTRDIFFT_TO_INT32(lineend - lineptr) ,
                           ( uint8 * )"%%EOF" , 5) == 0 )
              goteof = TRUE ;
          }
          else if ( ! gotxrefaddr ) {
            Hq32x2 xrefaddr ;
            /* Scan the line_buffer to read the cross reference table start address. */
            if ( ! pdf_scan_large_integer( lineptr , lineend , & xrefaddr ))
              return FALSE ;
            ixc->pdfxref = xrefaddr ;
            gotxrefaddr = TRUE ;
          }
          else {
            if ( HqMemCmp( lineptr , CAST_PTRDIFFT_TO_INT32(lineend - lineptr) ,
                           ( uint8 * )"startxref" , 9) != 0 )
              return error_handler( SYNTAXERROR ) ;
            return TRUE ;
          }
          lineptr = lineend ;
        }
      }
      else {
        if ( lineptr == linebuf ) {
          if ( goteof )
            return error_handler( SYNTAXERROR ) ;
        } /* Don't use this char if we've gone too far for a valid token */
        else
          *(--lineptr) = ch ;
      }
    }
  }
  /* not reached */
}

/* ---------------------------------------------------------------------- */

/* enum for both trailer_dict and prev_trailer_dict */
enum {
  ETD_Size, ETD_Prev, ETD_Root, ETD_Info, ETD_ID, ETD_Encrypt, ETD_XRefStm,
  ETD_n_entries
};
NAMETYPEMATCH trailer_dict[ETD_n_entries + 1] = {
  { NAME_Size               ,     1, { OINTEGER }},
  { NAME_Prev    | OOPTIONAL,     2, { OINTEGER, OFILEOFFSET }},
  { NAME_Root               ,     1, { OINDIRECT }},
  { NAME_Info    | OOPTIONAL,     1, { OINDIRECT }},
  { NAME_ID      | OOPTIONAL,     3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  { NAME_Encrypt | OOPTIONAL,     2, { ODICTIONARY,OINDIRECT }},
  { NAME_XRefStm | OOPTIONAL,     2, { OINTEGER, OFILEOFFSET }},
  DUMMY_END_MATCH
} ;


/* Root here is optional since it won't be present for Linearized PDF */

static NAMETYPEMATCH prev_trailer_dict[ETD_n_entries + 1] = {
  { NAME_Size               ,     1, { OINTEGER }},
  { NAME_Prev    | OOPTIONAL,     2, { OINTEGER, OFILEOFFSET }},
  { NAME_Root    | OOPTIONAL,     1, { OINDIRECT }},
  { NAME_Info    | OOPTIONAL,     1, { OINDIRECT }},
  { NAME_ID      | OOPTIONAL,     3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  { NAME_Encrypt | OOPTIONAL,     2, { ODICTIONARY,OINDIRECT }},
  { NAME_XRefStm | OOPTIONAL,     1, { OINTEGER }},
  DUMMY_END_MATCH
} ;


/** Extract data from trailer dict or trailer elements from xref stream dict
    (>pdf1.5). */
Bool pdf_extract_trailer_dict(PDFCONTEXT *pdfc, OBJECT * dict)
{
  PDF_IXC_PARAMS *ixc ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  Hq32x2FromInt32( &ixc->trailer_prev,-1 ) ; /*ie not set*/
  ixc->gotinfo = FALSE;

  /* dictmatch, instead of pdf_dictmatch, to avoid trying to resolve
   * indirect objects before all the xref tables have been read. */
  if (!dictmatch( dict , trailer_dict ))
    return FALSE;

  /* Extract the Root object. */
  Copy( & ixc->pdfroot , trailer_dict[ ETD_Root ].result ) ;

  /* Check if got Info dictionary. */
  if ( trailer_dict[ ETD_Info ].result ) {
    ixc->gotinfo = TRUE ;
    Copy( & ixc->pdfinfo , trailer_dict[ ETD_Info ].result ) ;
  }

  /* Read all the Prev dictionaries. */
  if ( trailer_dict[ ETD_Prev ].result ) {
    if (oType(*(trailer_dict[ ETD_Prev ].result)) == OINTEGER)
      Hq32x2FromInt32( &ixc->trailer_prev, oInteger( *(trailer_dict[ETD_Prev].result) )) ;
    else
      FileOffsetToHq32x2(ixc->trailer_prev, *(trailer_dict[ETD_Prev].result) ) ;
  }

  Hq32x2FromInt32( &ixc->trailer_xrefstm, -1) ;
  if ( trailer_dict[ ETD_XRefStm ].result ) {
    if (oType(*(trailer_dict[ ETD_XRefStm ].result)) == OINTEGER)
      Hq32x2FromInt32( &ixc->trailer_xrefstm, oInteger( *(trailer_dict[ETD_XRefStm].result) )) ;
    else
      FileOffsetToHq32x2(ixc->trailer_xrefstm, *(trailer_dict[ETD_XRefStm].result) ) ;
  }

  /* ID is required in PDF/X versions, but this has to be checked later when
     we know which PDF/X version we've got (i.e. when the info dictionary has
     been obtained). */

  if ( trailer_dict[ ETD_Encrypt ].result ) {
    Copy( & ixc->trailer_encrypt , trailer_dict[ ETD_Encrypt ].result ) ;
  }

  if ( trailer_dict[ ETD_ID ].result ) {
    Copy( & ixc->trailer_id , trailer_dict[ ETD_ID ].result ) ;
  }

  return TRUE;
}

Bool pdf_extract_prevtrailer_dict(PDFCONTEXT *pdfc, OBJECT * prevobject)
{
  PDF_IXC_PARAMS *ixc ;
  PDFXCONTEXT *pdfxc ;
  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* dictmatch, instead of pdf_dictmatch, to avoid trying to resolve
   * indirect objects before all the xref tables have been read. */
  if (!dictmatch( prevobject , prev_trailer_dict ) )
    return FALSE;

  /* Check if got Info dictionary. */
  if ( prev_trailer_dict[ ETD_Info].result ) {
    if ( ! ixc->gotinfo ) {
      ixc->gotinfo = TRUE ;
      Copy( & ixc->pdfinfo , prev_trailer_dict[ ETD_Info ].result ) ;
    }
  }

  /* Check if got another Prev. */
  if ( prev_trailer_dict[ ETD_Prev ].result ) {
    if (oType(*(trailer_dict[ ETD_Prev ].result)) == OINTEGER)
      Hq32x2FromInt32( &ixc->trailer_prev, oInteger( *(prev_trailer_dict[ETD_Prev].result) )) ;
    else
      FileOffsetToHq32x2(ixc->trailer_prev, *(prev_trailer_dict[ETD_Prev].result) ) ;
  }

  return TRUE;
}

/* Check that the info dict we think we have actually exists.  If it doesn't
 * (indirect reference lookup fails or it is a null value) then forget that we
 * saw an info dict including removing any entry in the tracked trailer dict.
 */
Bool pdf_validate_info_dict(
  PDFCONTEXT *pdfc)
{
  PDFXCONTEXT *pdfxc;
  PDF_IXC_PARAMS *ixc;

  PDF_CHECK_MC(pdfc);
  PDF_GET_XC(pdfxc);
  PDF_GET_IXC(ixc);

  if (ixc->gotinfo) {
    OBJECT *pinfo = &ixc->pdfinfo;
    if (oType(*pinfo) == OINDIRECT) {
      if (!pdf_lookupxref(pdfc, &pinfo, oXRefID(*pinfo), theGen(*pinfo), FALSE)) {
        return (FALSE);
      }
    }
    ixc->gotinfo = (pinfo && oType(*pinfo) != ONULL);
    if (!ixc->gotinfo) {
      OBJECT oinfokey = OBJECT_NOTVM_NAME(NAME_Info, LITERAL);
      ixc->pdfinfo = onull;
      if (!remove_hash(&ixc->pdftrailer, &oinfokey, FALSE)) {
        return (FALSE);
      }
    }
  }
  return (TRUE);
}


static Bool pdf_read_trailer( PDFCONTEXT *pdfc , Bool *repairable )
{
  Bool result ;
  FILELIST *flptr ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  Bool stream = FALSE;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  flptr = pdfxc->flptr ;
  HQASSERT( flptr , "flptr field NULL in pdf_read_trailer" ) ;

  /* For encrypted jobs, do not want to repair the job if it is
   * pdf_begin_decryption that has failed. */
  *repairable = TRUE ;

  /* Read main xref section. */
  Hq32x2FromInt32( &ixc->trailer_prev, -1) ; /* set to unused*/
  if ( ! pdf_read_xref( pdfc , ixc->pdfxref, &stream ))
    return FALSE ;

  while ( stream && (Hq32x2CompareInt32( &ixc->trailer_prev, 0) >= 0) ) {
    Hq32x2 prev = ixc->trailer_prev;
    Hq32x2FromInt32( & ixc->trailer_prev, -1 );
    if ( ! pdf_read_xref( pdfc , prev, &stream ))
      return FALSE ;
  };

  /* If we have read in a stream (PDF 1.5 rather than a table then we
     are basically done. */
  if (stream) {
    result = TRUE ;
  } else {

    /* Then read the main dictionary. */
    if ( ! pdf_readobject( pdfc , flptr , & ixc->pdftrailer ))
      return FALSE ;
    if (oType( ixc->pdftrailer ) != ODICTIONARY ) {
      pdf_freeobject( pdfc , & ixc->pdftrailer ) ;
      return error_handler( TYPECHECK ) ;
    }
    /* The trailer dict in the ixc won't be freed until the mm pools are
     * destroyed or the PDF file is closed. This is so getPDFtrailer
     * can be called at any point. We use result to check whether we should
     * be doing anything. */

    result = pdf_extract_trailer_dict(pdfc, &ixc->pdftrailer);

    /* extract info from any previous dictionaries */
    while ( result && ((Hq32x2CompareInt32( &ixc->trailer_prev, 0) >= 0) ||
                       (Hq32x2CompareInt32( &ixc->trailer_xrefstm, 0) >= 0)) ) {
      Hq32x2 prev;

      /* Hybrid PDF1.4/1.5 files can feature a reference to an xref stream
         in a PDF1.4 style trailer dict. In this case we extract these object
         references before the "prev" table */
      prev = ixc->trailer_xrefstm;
      if (Hq32x2CompareInt32(&prev , 0) < 0)
        prev = ixc->trailer_prev;
      if (Hq32x2Compare(&prev, &ixc->trailer_prev) == 0)
        Hq32x2FromInt32( & ixc->trailer_prev, -1) ;
      Hq32x2FromInt32( &ixc->trailer_xrefstm, -1);
      if (!pdf_read_xref( pdfc , prev, &stream ))
        return FALSE;
      if (!stream) {
        /* check for further PDF1.4 Prev entries in the old trailer dict */
        OBJECT prevobject = OBJECT_NOTVM_NOTHING ;

        if (!pdf_readobject( pdfc , flptr , & prevobject ) )
          return FALSE;

        if (oType(prevobject) != ODICTIONARY)
          return FALSE;

        result = pdf_extract_prevtrailer_dict(pdfc,&prevobject);
        pdf_freeobject( pdfc ,&prevobject ) ;
      }
    }
  }

  /* Is it encrypted? */
  if (result && ( oType(ixc->trailer_encrypt) == ODICTIONARY ||
                  oType(ixc->trailer_encrypt) == OINDIRECT )) {

    result = pdf_begin_decryption( pdfc ,
                                   &ixc->trailer_encrypt ,
                                   &ixc->trailer_id ) ;
    if ( ! result )
      *repairable = FALSE ; /* No point trying to repair. */
  }

  /* Now any decryption is set up validate the info dict */
  result = (result && pdf_validate_info_dict(pdfc));

  return result ;
}

/* ---------------------------------------------------------------------- */

/** Reads a dictionary overriding info, encrypt and id, if they are
 * present after the dictmatch. The caller is responsible for freeing
 * the final 3 objects.
 */
Bool pdf_read_trailerdict( PDFCONTEXT *pdfc , FILELIST *flptr ,
                            OBJECT *encrypt , OBJECT *id )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  Bool result = TRUE ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( flptr , "flptr is null in pdf_read_trailerdict" ) ;
  HQASSERT( encrypt , "encrypt is null in pdf_read_trailerdict" ) ;
  HQASSERT( id , "id is null in pdf_read_trailerdict" ) ;

  /* Then read the main dictionary. */
  if ( ! pdf_readobject( pdfc , flptr , & ixc->pdftrailer ))
    return FALSE ;

  if ( oType(ixc->pdftrailer) != ODICTIONARY ) {
    pdf_freeobject( pdfc , & ixc->pdftrailer ) ;
    result = error_handler( TYPECHECK ) ;
  }

  /* dictmatch, instead of pdf_dictmatch, to avoid trying to resolve
   * indirect objects before all the xref tables have been read. */
  result = result &&
           dictmatch( & ixc->pdftrailer , prev_trailer_dict ) ;

  /* Extract the Root object. */
  if ( result && prev_trailer_dict[ETD_Root].result )
    Copy( & ixc->pdfroot , prev_trailer_dict[ETD_Root].result ) ;

  /* Check if got Info dictionary. */
  if ( result && prev_trailer_dict[ETD_Info].result )
    Copy( & ixc->pdfinfo , prev_trailer_dict[ETD_Info].result ) ;

  /* Check if got an ID. */
  if ( result && prev_trailer_dict[ETD_ID].result ) {
    if ( oType(*id) != ONULL )
      pdf_freeobject( pdfc , id ) ;
    result = pdf_copyobject( pdfc , prev_trailer_dict[ETD_ID].result , id ) ;
  }

  /* Is it encrypted? */
  if ( result && prev_trailer_dict[ETD_Encrypt].result ) {
    if ( oType(*encrypt) == ODICTIONARY )
      pdf_freeobject( pdfc , encrypt ) ;
    result = pdf_copyobject( pdfc , prev_trailer_dict[ETD_Encrypt].result , encrypt ) ;
  }

  return result ;
}

/* ---------------------------------------------------------------------- */
/** Do not change the order of the entries of this array.  If this array needs to
 * have a new entry added and the type according to the PDF spec is text string,
 * then add it before CreationDate (and yes, rework subsequent indexes) and then
 * up the constant of the index of creationdate.
 */
enum {
  e_infodict_keywords = 0,  /* Keywords is not output on the console */
  e_infodict_title,  /* Title is the first one to be output on the console */
  e_infodict_subject,
  e_infodict_author,
  e_infodict_creator,
  e_infodict_producer,
  e_infodict_creationdate,
  e_infodict_moddate,
  e_infodict_trapped,
  e_infodict_max
};
static NAMETYPEMATCH info_dict[e_infodict_max + 1] = {
  { NAME_Keywords            | OOPTIONAL , 2 , { OSTRING , OINDIRECT }} ,
  { NAME_Title               | OOPTIONAL , 2 , { OSTRING , OINDIRECT }} ,
  { NAME_Subject             | OOPTIONAL , 2 , { OSTRING , OINDIRECT }} ,
  { NAME_Author              | OOPTIONAL , 2 , { OSTRING , OINDIRECT }} ,
  { NAME_Creator             | OOPTIONAL , 2 , { OSTRING , OINDIRECT }} ,
  { NAME_Producer            | OOPTIONAL , 2 , { OSTRING , OINDIRECT }} ,
  { NAME_CreationDate        | OOPTIONAL , 2 , { OSTRING , OINDIRECT }} ,
  { NAME_ModDate             | OOPTIONAL , 2 , { OSTRING , OINDIRECT }} ,
  { NAME_Trapped             | OOPTIONAL , 3 , { ONAME , OSTRING, OINDIRECT }} ,
  DUMMY_END_MATCH
} ;

/**
 * Populate document metadata from the passed dictionary. Any fields not present
 * in the passed dictionary will left untouched.
 *
 * Memory allocated will be from the PDF object pool of the execution context,
 * and so will be automatically released at the end of the job.
 */
static Bool populateDocumentMetadata(PDFCONTEXT* pdfc,
                                     OBJECT* metadataDictionary)
{
  enum {docId = 0, versionId = 1, rc = 2};
  NAMETYPEMATCH mdMatch[] = {
    {NAME_xmpMM_DocumentID | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    {NAME_xmpMM_VersionID | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    {NAME_xmpMM_RenditionClass | OOPTIONAL, 2, {OSTRING, OINDIRECT}},
    DUMMY_END_MATCH
  };
  GET_PDFXC_AND_IXC

  if (! pdf_dictmatch(pdfc, metadataDictionary, mdMatch))
    return FALSE;

  if (mdMatch[docId].result != NULL) {
    if (! pdf_copyobject(pdfc, mdMatch[docId].result, &pdfxc->metadata.id))
      return FALSE;
  }

  if (mdMatch[versionId].result != NULL) {
    if (! pdf_copyobject(pdfc, mdMatch[versionId].result, &pdfxc->metadata.version))
      return FALSE;
  }

  if (mdMatch[rc].result != NULL) {
    if (! pdf_copyobject(pdfc, mdMatch[rc].result, &pdfxc->metadata.renditionClass))
      return FALSE;
  }

  return TRUE;
}


/* Given a terminated line of text potentially in UTF8 and a proposed truncation
   length, adjust the truncation to avoid splitting UTF8 characters and whole
   words if possible. */
static void split_info_line(size_t * pfits, uint8 * text, uint8 * bom)
{
  int j, max ;
  size_t fits ;

  if (text == NULL || pfits == NULL)
    return ;
  fits = *pfits ;

  /* Don't split a UTF8 multibyte sequence */
  if (bom && bom[0] != 0) {
    while (fits &&
           (text[fits] & 0xC0) == 0x80)
      --fits ; /* next byte was a continuation, so truncate earlier */
  }

  /* Avoid splitting mid-word if possible */
  max = (fits < 17) ? (int)fits : 17 ;
  for (j = 1; j < max; ++j) {
    if (text[(fits)-j] < 'A') {
      fits -= j-1 ;
      break ;
    }
  }

  *pfits = fits ;
}


#define IDX_CREATIONDATE (6)

/**
 * Read and print the Info dict, and act on any "hints" it contains (e.g.
 * Trapped). This will also cause any metadata in the document catalog to be
 * processed.
 */
static Bool pdf_walk_infodict( PDFCONTEXT *pdfc )
{
  enum { catalog_Metadata, catalog_dummy } ;
  NAMETYPEMATCH catalogMatches[catalog_dummy + 1] = {
    { NAME_Metadata | OOPTIONAL, 2, { OFILE, OINDIRECT }},
    DUMMY_END_MATCH
  };

#define MAXINFOCHRS 1000  /* max chrs on a line */
#define MAXINFOSTRING 1000  /* was 128 */
/* Worst case, each Unicode character requires 3 bytes in UTF8 */
#define MAXINFOSTRING_UTF8 (MAXINFOSTRING*3)
  uint8 text_buffer[MAXINFOSTRING_UTF8];
  utf8_buffer text_string;
  OBJECT *info = NULL;
  OBJECT catalog = OBJECT_NOTVM_NULL ;
  OBJECT* metadataStream = NULL;
  OBJECT metadataDictionary = OBJECT_NOTVM_NULL ;
  OBJECT* metadataDictPointer = NULL;
  int32 i ;
  GET_PDFXC_AND_IXC

  /* The Info dict is optional so may not exist. However, if enforcing PDF/X,
     then need to report a warning if Info is absent.
  */
  if (oType(ixc->pdfinfo) != ONULL) {
    Bool status ;

    HQASSERT( oType(ixc->pdfinfo) == OINDIRECT, "Unexpected type for pdfinfo" ) ;

    /* This allows the string decryption code to check if the metadata
       strings ought to be decrypted or not. */
    set_reading_metadata( pdfc, TRUE ) ;

    status = pdf_lookupxref( pdfc , & info ,
                             oXRefID(ixc->pdfinfo) ,
                             theGen( ixc->pdfinfo ) ,
                             FALSE ) ;

    set_reading_metadata( pdfc, FALSE ) ;

    if ( ! status )
      return FALSE ;
  }

  /* Validate the /Info dictionary and use it. */
  if ( info != NULL && oType(*info) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  /* Determine the value of the Trapped key before the PDF/X checking.
   * We also want to disable trapping (in pdf_walk_tree()) if the value is /True.
   */
  if ( info != NULL ) {
    if ( ! pdf_dictmatch( pdfc , info , info_dict ))
      return FALSE ;

    if ( info_dict[ e_infodict_trapped ].result )
      pdf_setTrapStatus(pdfc, info_dict[ e_infodict_trapped ].result);
  }

  /* Parse document metadata in the catalog dictionary. */
  if (! getCatalogDict(pdfc, ixc, &catalog))
    return FALSE;

  if (! pdf_dictmatch(pdfc, &catalog, catalogMatches))
    return FALSE;

  metadataStream = catalogMatches[catalog_Metadata].result;
  if (metadataStream != NULL) {
    if (! pdfMetadataParse(pdfc, oFile(*metadataStream), &metadataDictionary))
      return FALSE;
    metadataDictPointer = &metadataDictionary;

    if (! populateDocumentMetadata(pdfc, &metadataDictionary)) {
      return FALSE;
    }
  }

  /* Initialise the PDF/X checker; this must be done even if the info dictionary
  is null. */
  if (! pdfxInitialise(pdfc, info, metadataDictPointer))
    return FALSE ;

  if (! pdfxCheckCatalogDictionary(pdfc, &catalog))
    return FALSE;

  pdf_freeobject(pdfc, &metadataDictionary);

  if ( info == NULL )
    return TRUE ;

  /* We handle the out-of-spec situation where Trapped is a string rather than a name
     So if we are being strict then let's not be so nice.*/
  if ( ixc->strictpdf ) {
    if ( info_dict[ e_infodict_trapped ].result != NULL &&
         oType(*info_dict[ e_infodict_trapped ].result) == OSTRING )
        return error_handler( TYPECHECK ) ;
  }

  if ( pdfout_enabled() ) {
    struct PDFCONTEXT *pdfout_h = pdfc->corecontext->pdfout_h;
    /* we supply the ModDate */
    if ( (!pdfout_UpdateInfoString(pdfout_h, NAME_Title,        info_dict[ e_infodict_title ].result)) ||
         (!pdfout_UpdateInfoString(pdfout_h, NAME_Subject,      info_dict[ e_infodict_subject ].result)) ||
         (!pdfout_UpdateInfoString(pdfout_h, NAME_Author,       info_dict[ e_infodict_author ].result)) ||
         (!pdfout_UpdateInfoString(pdfout_h, NAME_Keywords,     info_dict[ e_infodict_keywords ].result)) ||
         (!pdfout_UpdateInfoString(pdfout_h, NAME_Creator,      info_dict[ e_infodict_creator ].result)) ||
         (!pdfout_UpdateInfoString(pdfout_h, NAME_Producer,     info_dict[ e_infodict_producer ].result)) ||
         (!pdfout_UpdateInfoString(pdfout_h, NAME_CreationDate, info_dict[ e_infodict_creationdate ].result)) /* ||
          trapped should NOT be a string - but we have had files that do have it as a string rather than a name.
         (!pdfout_UpdateInfoString(pdfout_h, NAME_Trapped,      info_dict[ e_infodict_trapped ].result))*/ )
      return FALSE ;
  }

  if (info_dict[ e_infodict_title ].result != NULL &&
      oString(*info_dict[ e_infodict_title ].result) != NULL &&
      !ixc->encapsulated) {
    OBJECT jobnameobj = OBJECT_NOTVM_NOTHING ;

    if ( ! pdf_copyobject( NULL , /* null means uses PS VM. */
                           info_dict[ e_infodict_title ].result , & jobnameobj ))
      return FALSE ;

    if ( ! setjobnameandflag(pdfc->corecontext, & jobnameobj, & fnewobj ))
      return FALSE ;
  }

  /* No special encoding handling for remaining items from creation date onwards. */
  for ( i = e_infodict_title ; i < e_infodict_max ; ++i ) {
    NAMECACHE* name = theIMName(&info_dict[i]);
    OBJECT* string  = info_dict[i].result;
    uint8 * bom     = (uint8*)"";
    uint8 * text    = (uint8*)"Not Available";
    size_t length   = 13; /* length of the above */
    size_t fits;

    if ( string ) {
      if ( i < e_infodict_creationdate ) {
        /* First lot are PDF strings and need conversion to UTF8 */

        text_string.codeunits = text_buffer;
        if (!pdf_text_to_utf8(pdfc, string, MAXINFOSTRING_UTF8, &text_string))
          return FAILURE(FALSE);

        bom    = (uint8*)UTF8_BOM;
        text   = text_string.codeunits;
        length = text_string.unitlength;

      } else {
        /* Remainder simple strings or names */
        if ( oType(*string) == OSTRING ) {

          length = theLen(*string);
          text   = oString(*string);

        } else { /* Trapped is a name, not a string! */
          HQASSERT((oType(*string) == ONAME),
                   "pdf_walk_infodict: did not get name when expected");

          length = theINLen(oName(*string));
          text   = theICList(oName(*string));
        }
      }
    }

    /* Suppress final line endings because we add one */
    while (length && text[length-1] == (uint8)'\n')
      --length;

    /* Output as one line with 'text' overflowing onto additional lines */
    /* The amount of room left for 'text' */
    fits = MAXINFOCHRS - theINLen(name) - 7; /* 7 = "<BOM>: \n\0" */
    if (fits > length)
      fits = length;
    if (fits < length)
      split_info_line(&fits, text, bom) ;
    monitorf((uint8*)"%s%.*s: %.*s\n", bom, theINLen(name), theICList(name),
                                       fits, text);

    /* now any overflow (also with BOMs) */
    while (length > fits) {
      text   += fits;
      length -= fits;
      fits    = min(MAXINFOCHRS-8, length);
      if (fits < length)
        split_info_line(&fits, text, bom) ;
      monitorf((uint8*)"%s+: %.*s\n", bom, fits, text);
    }
  }

  return TRUE ;
}

void pdf_setTrapStatus(PDFCONTEXT *pdfc, OBJECT *trapped)
{
  GET_PDFXC_AND_IXC;

  if (( oType(*trapped) == ONAME && oNameNumber(*trapped) == NAME_True ) ||
      ( oType(*trapped) == OSTRING &&
        HqMemCmp( oString(*trapped) , theLen(*trapped) , (uint8 *)"True", 4) == 0 ))
    ixc->infoTrapped = INFO_TRAPPED_TRUE;
  else if (( oType(*trapped) == ONAME && oNameNumber(*trapped) == NAME_False ) ||
      ( oType(*trapped) == OSTRING &&
        HqMemCmp( oString(*trapped) , theLen(*trapped) , (uint8 *)"False", 5) == 0 ))
    ixc->infoTrapped = INFO_TRAPPED_FALSE;
  else
    ixc->infoTrapped = INFO_TRAPPED_INVALID;
}

static uint8 *read_field_uint32(
  uint8   *pb,
  int32   size,
  uint32  *pvalue)
{
  uint32 value = 0;

  while (size-- > 0) {
    value = value << 8 | *pb++;
  }
  *pvalue = value;
  return (pb);
}

static uint8 *read_field_hq32x2(
  uint8   *pb,
  int32   size,
  Hq32x2  *pvalue)
{
  int32   high = 0;
  uint32  low = 0;

  while (size-- > 0) {
    high = high << 8 | low >> 24;
    low = low << 8 | *pb++;
  }
  pvalue->high = high;
  pvalue->low = low;
  return (pb);
}


#define XSTRM_FREE_OBJ          (0)
#define XSTRM_UNCOMPRESSED_OBJ  (1)
#define XSTRM_COMPRESSED_OBJ    (2)

/**
 * pdf_read_xrefstream()
 * Read in an xref stream (version >= PDF1.5).
 * This function is a child of pdf_read_xref.
 */
static Bool pdf_read_xrefstreams(PDFCONTEXT *pdfc,
  FILELIST *flptr, XREFSEC *xrefsec)
{
  int32 totalbytes;
  uint32 field_type;
#define XREFSTMENTRY_SZ (16)  /* 4 bytes for 1st and 3rd field, 8 for 2nd */
  uint8 linebuf[XREFSTMENTRY_SZ];
  uint8 * nextbyte;
  OBJECT pdfobj = OBJECT_NOTVM_NOTHING;
  uint32 streamindex;
  FILELIST *stream;
  int32 startobjnum,objcount;
  PDF_STREAM_INFO info;
  PDFXCONTEXT *pdfxc ;
  XREFTAB *xreftab ;
  XREFOBJ *xrefobj ;
  PDF_IXC_PARAMS *ixc ;
  Hq32x2 bigint;
  int32 indexcount;
  int8 streamDict ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  if (! pdf_xrefobject(pdfc, flptr, &pdfobj, &info,
                       FALSE /* Not just the dictionary, but full stream. */,
                       &streamDict))
    return FALSE ;

  /* If its not a full stream, give up here, as we may be able to
     repair later on. */
  if (streamDict != XREF_FullStream)
    return (error_handler(TYPECHECK));

  stream = oFile(pdfobj);

  if ( ! isIOpenFile( stream ) ||
       ! isIRewindable( stream ))
    return (error_handler(IOERROR));

  totalbytes = info.Wsize[0] + info.Wsize[1] + info.Wsize[2];

  indexcount = 0;

  do {
    if (info.index) {
      startobjnum = oInteger(oArray(*info.index)[indexcount++]);
      objcount = oInteger(oArray(*info.index)[indexcount++]);

      if (startobjnum < 0 || objcount < 1 ||
          startobjnum >= info.size ||
          (startobjnum + objcount >= info.size &&
           ixc->strictpdf)) {
        return (error_handler(RANGECHECK));
      }
      if (startobjnum + objcount >= info.size) {
        objcount = info.size - startobjnum;
      }

    } else {
      startobjnum = 0;
      objcount = info.size - startobjnum;
    }

    xreftab = pdf_allocxreftab( pdfc , xrefsec , startobjnum , objcount ) ;
    if ( ! xreftab )
      return error_handler( VMERROR ) ;

    xrefobj = pdf_allocxrefobj( pdfc , xreftab , objcount ) ;
    if ( ! xrefobj )
      return error_handler( VMERROR ) ;

    /* fill buffer*/
    while (objcount-- > 0) {
      uint32 objnum, objgen ;
      if (!pdf_readdata( stream , linebuf , linebuf + totalbytes ))
        return FALSE;

      nextbyte = linebuf;

      field_type = XSTRM_UNCOMPRESSED_OBJ;
      if (info.Wsize[0] > 0) {
        nextbyte = read_field_uint32(nextbyte, info.Wsize[0], &field_type);
      }

      nextbyte = read_field_hq32x2(nextbyte, info.Wsize[1], &bigint);

      /* For all field types, the third field always defaults to 0.  Not
       * documented up to 1.7 but as seen in the field with jobs generated by
       * Adobe's PDF Library. */
      switch (field_type)
      {
        case XSTRM_FREE_OBJ:
          if (!Hq32x2ToUint32(&bigint, &objnum)) {
            return (error_handler(RANGECHECK));
          }
          (void)read_field_uint32(nextbyte, info.Wsize[2], &objgen);
          pdf_storefreexrefobj( xrefobj , objnum ,
                                CAST_UNSIGNED_TO_UINT16(objgen) ) ;
          break;

        case XSTRM_UNCOMPRESSED_OBJ:
          (void)read_field_uint32(nextbyte, info.Wsize[2], &objgen);
          pdf_storexrefobj( xrefobj , bigint ,
                            CAST_UNSIGNED_TO_UINT16(objgen) ) ;
          break;

        case XSTRM_COMPRESSED_OBJ:
          if (!Hq32x2ToUint32(&bigint, &objnum)) {
            return (error_handler(RANGECHECK));
          }
          (void)read_field_uint32(nextbyte, info.Wsize[2], &streamindex);
          pdf_storecompressedxrefobj( xrefobj , objnum ,
                                      CAST_UNSIGNED_TO_UINT16(streamindex) ) ;
          break;

        default:
          if (ixc->strictpdf) {
            return (error_handler(RANGECHECK));
          }
      }
      xrefobj++;
    }
  } while (info.index && indexcount < theLen(*info.index));

  return TRUE;
}


/**
 * pdf_read_xref()
 * Reads in the xref table up until the 'trailer' keyword.
 * Note that the trailer keyword is itself consumed here.
 * The file position of the start of the xref section is given by the
 * 'xref_filepos' argument.
 */
static Bool pdf_read_xref( PDFCONTEXT *pdfc , Hq32x2 xref_filepos, Bool * stream )
{
  Bool first = TRUE ;
  FILELIST *flptr ;
  DEVICELIST *dev ;
  XREFSEC *xrefsec ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  *stream = FALSE;
  xrefsec = pdf_allocxrefsec( pdfc , xref_filepos ) ;
  if ( ! xrefsec )
    return error_handler( VMERROR ) ;

  flptr = pdfxc->flptr ;
  HQASSERT( flptr , "flptr field NULL in pdf_read_xref" ) ;
  dev = theIDeviceList( flptr ) ;
  HQASSERT( dev , "dev field NULL in pdf_read_xref" ) ;

  /* We perform this as follows:
   * a) Read a [suitable] buffer.
   * b) Scan a line and check for:
   *     xref
   *     n1 n2
   *     nnnnnnnnnn nnnnn X
   *    i.e. read all the xref table (until we reach trailer).  Note:
   *    we allow whitespaces around the xref, and n1/n2 pair but not
   *    the nn/n/x entry (this is supposed to be fixed format). But
   *    the data we require must be in the first 20 bytes - this is
   *    the line length for each xref table entry. If the format is
   *    invalid, then the repair function should kick in.
   * c) Add all the xrefs "n1 n2" to the xref table.
   * d) Add all the object refs "nnnnnnnnnn nnnnn X" to the object table.
   */

  /* Use PS streams to read lines... */
  if ((*theIMyResetFile( flptr ))( flptr ) == EOF )
    return (*theIFileLastError( flptr ))( flptr ) ;
  if ( ! (*theISeekFile( dev ))( dev , theIDescriptor( flptr ) ,
                                 & xref_filepos , SW_SET ))
    return device_error_handler( dev ) ;

  for (;;) {
    int32 objnum , objcnt ;
    XREFTAB *xreftab ;
    XREFOBJ *xrefobj ;

    if ( first ) {
      first = FALSE ;

      if (! pdf_scan_xref_optional_string(flptr, NAME_AND_LENGTH("xref"))) {
        /* If this is a PDF 1.5 file then we may have an xref stream
           rather than table. */

        if (isIIOError( flptr ))
          return (*theIFileLastError( flptr ))( flptr ) ;

        if (! pdf_read_xrefstreams( pdfc, flptr, xrefsec ))
          return FALSE ;

        if ( ! pdfxXrefStreamDetected( pdfc ))
          return FALSE ;

        *stream = TRUE ;
        return TRUE ;
      }
      continue ;
    }

    /* Stop when we run off the end of the xref table */
    if (pdf_scan_xref_optional_string(flptr, NAME_AND_LENGTH("trailer")))
      return TRUE ;  /* No need to check for an IOERROR as that will
                        be picked up by the next scan function. */

    if (! pdf_scan_xref_required_integer(flptr, &objnum))
      return FALSE ;
    if ( objnum < 0 )
      return error_handler( RANGECHECK ) ;
    HQTRACE(debug_xrefscan,("objnum: %d\n" , objnum ));

    if (! pdf_scan_xref_required_whitespace(flptr))
      return FALSE ;

    if (! pdf_scan_xref_required_integer(flptr, &objcnt))
      return FALSE ;
    if ( objcnt == 0 )
      continue ;
    if ( objcnt < 0 )
      return error_handler( RANGECHECK ) ;
    HQTRACE(debug_xrefscan,("objcnt: %d\n" , objcnt ));

    if (! pdf_scan_xref_required_whitespace(flptr))
      return FALSE ;

    xreftab = pdf_allocxreftab( pdfc , xrefsec , objnum , objcnt ) ;
    if ( ! xreftab )
      return error_handler( VMERROR ) ;
    xrefobj = pdf_allocxrefobj( pdfc , xreftab , objcnt ) ;
    if ( ! xrefobj )
      return error_handler( VMERROR ) ;

    /* This block is supposed to be fixed format! So we don't do much
     * to cope with garbage whitespace - if the format is violated
     * (with respect to byte offsets) then the repair function will
     * kick in.
     */
    while ((--objcnt) >= 0 ) {
      uint8 linebuf[ XREF_SIZE ] ;
      Hq32x2 objoff ;
      int32 objgen ;
      int32 objuse ;

      if ( ! pdf_readdata( flptr , linebuf , linebuf + XREF_SIZE ))
        return FALSE ;
      if ( ! pdf_scan_large_integer( linebuf +  0 , linebuf + 10 , & objoff ))
        return FALSE ;
      HQTRACE(debug_xrefscan,("objoff: %d\n" , objoff ));
      if ( linebuf[ 10 ] != ' ' )
        return error_handler( SYNTAXERROR ) ;
      if ( ! pdf_scan_integer_allowing_spaces( linebuf + 11 , linebuf + 16 , & objgen ))
        return FALSE ;
      HQTRACE(debug_xrefscan,("objgen: %d\n" , objgen ));
      if ( linebuf[ 16 ] != ' ' )
        return error_handler( SYNTAXERROR ) ;
      if ( linebuf[ 17 ] != XREF_Free && linebuf[ 17 ] != XREF_Used )
        return error_handler( SYNTAXERROR ) ;
      objuse = linebuf[ 17 ] ;
      if ( objgen > 0xFFFF )
        objgen = 0xFFFF ;    /* A hack to cope with out-of-spec jobs */
      HQTRACE(debug_xrefscan,("objuse: %c\n" , objuse ));

      /* We do not bother checking that byte 18 and 19 are space
         characters. i.e. S_LF, S_CR, CR_LF or S_S (allow anything in
         last two bytes - we don't care). */

      if (objuse == XREF_Used) {
        pdf_storexrefobj( xrefobj , objoff , ( uint16 )objgen ) ;
      } else {
        /* in this case the offset is actually the obj number of the next
           free object */
        pdf_storefreexrefobj( xrefobj , ( int32 )objoff.low , ( uint16 )objgen ) ;
      }

      ++xrefobj ;
    }
  }
  /* NOT REACHED */
}

/* ---------------------------------------------------------------------- */
enum {
  params_match_Encapsulated, /* appears here only ( not in PDFParams ) */
  params_match_IgnoreSetPageDevice, /* appears here only ( not in PDFParams ) */
  params_match_IgnoreShowpage, /* appears here only ( not in PDFParams ) */

  params_match_Strict,
  params_match_PageRange,
  params_match_OwnerPasswords,
  params_match_UserPasswords,
  params_match_PrivateKeyFiles,
  params_match_PrivateKeyPasswords,
  params_match_HonorPrintPermissionFlag,
  params_match_MissingFonts,
  params_match_PageCropTo,
  params_match_EnforcePDFVersion,
  params_match_AbortForInvalidTypes,
  params_match_PrintAnnotations,
  params_match_AnnotationParams,
  params_match_ErrorOnFlateChecksumFailure,
  params_match_IgnorePSXObjects,
  params_match_WarnSkippedPages,

  params_match_SizePageToBoundingBox,
  params_match_OptionalContentOptions,
  params_match_EnableOptimizedPDFScan,
  params_match_OptimizedPDFScanLimitPercent,
  params_match_OptimizedPDFExternal,
  params_match_OptimizedPDFCacheSize,
  params_match_OptimizedPDFCacheID,
  params_match_OptimizedPDFSetupID,
  params_match_OptimizedPDFCrossXObjectBoundaries,
  params_match_OptimizedPDFSignificanceMask,
  params_match_OptimizedPDFScanWindow,
  params_match_OptimizedPDFImageThreshold,
  params_match_ErrorOnPDFRepair,
  params_match_PDFXVerifyExternalProfileCheckSums,
  params_match_TextStrokeAdjust,
  params_match_XRefCacheLifetime,

  params_match_n_entries
} ;


static NAMETYPEMATCH params_dictmatch[params_match_n_entries + 1] = {
  { NAME_Encapsulated | OOPTIONAL, 1, { OBOOLEAN }}, /* appears here only ( not in PDFParams ) */
  { NAME_IgnoreSetPageDevice | OOPTIONAL, 1, { OBOOLEAN }}, /* appears here only ( not in PDFParams ) */
  { NAME_IgnoreShowpage | OOPTIONAL, 1, { OBOOLEAN }}, /* appears here only ( not in PDFParams ) */

  { NAME_Strict | OOPTIONAL,                      1, { OBOOLEAN }},
  { NAME_PageRange | OOPTIONAL,                   2, { OARRAY, OPACKEDARRAY }},
  { NAME_OwnerPasswords | OOPTIONAL,              3, { OSTRING, OARRAY, OPACKEDARRAY }},
  { NAME_UserPasswords | OOPTIONAL,               3, { OSTRING, OARRAY, OPACKEDARRAY }},
  { NAME_PrivateKeyFiles | OOPTIONAL,             3, { OSTRING, OARRAY, OPACKEDARRAY }},
  { NAME_PrivateKeyPasswords | OOPTIONAL,         3, { OSTRING, OARRAY, OPACKEDARRAY }},
  { NAME_HonorPrintPermissionFlag | OOPTIONAL,    1, { OBOOLEAN }},
  { NAME_MissingFonts | OOPTIONAL,                1, { OBOOLEAN }},
  { NAME_PageCropTo | OOPTIONAL,                  1, { OINTEGER }},
  { NAME_EnforcePDFVersion | OOPTIONAL,           1, { OINTEGER }},
  { NAME_AbortForInvalidTypes | OOPTIONAL,        1, { OINTEGER }},
  { NAME_PrintAnnotations | OOPTIONAL,            1, { OBOOLEAN }},
  { NAME_AnnotationParams | OOPTIONAL,            1, { ODICTIONARY }},
  { NAME_ErrorOnFlateChecksumFailure | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_IgnorePSXObjects | OOPTIONAL,            1, { OBOOLEAN }},
  { NAME_WarnSkippedPages | OOPTIONAL,            1, { OBOOLEAN }},

  { NAME_SizePageToBoundingBox | OOPTIONAL,       1, { OBOOLEAN }},
  { NAME_OptionalContentOptions | OOPTIONAL,      1, { ODICTIONARY }},
  { NAME_EnableOptimizedPDFScan | OOPTIONAL,      1, { OBOOLEAN }},
  { NAME_OptimizedPDFScanLimitPercent | OOPTIONAL, 1, { OINTEGER }},
  { NAME_OptimizedPDFExternal | OOPTIONAL,        1, { OBOOLEAN }},
  { NAME_OptimizedPDFCacheSize | OOPTIONAL,       1, { OINTEGER }},
  { NAME_OptimizedPDFCacheID | OOPTIONAL,         1, { OSTRING }},
  { NAME_OptimizedPDFSetupID | OOPTIONAL,         1, { OSTRING }},
  { NAME_OptimizedPDFCrossXObjectBoundaries | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OptimizedPDFSignificanceMask | OOPTIONAL,  1, { OINTEGER }},
  { NAME_OptimizedPDFScanWindow | OOPTIONAL,      1, { OINTEGER }},
  { NAME_OptimizedPDFImageThreshold | OOPTIONAL,  1, { OINTEGER }},
  { NAME_ErrorOnPDFRepair | OOPTIONAL,            1, { OBOOLEAN }},
  { NAME_PDFXVerifyExternalProfileCheckSums | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_TextStrokeAdjust | OOPTIONAL,            2, { OREAL, OINTEGER }},
  { NAME_XRefCacheLifetime | OOPTIONAL,           1, { OINTEGER }},

  DUMMY_END_MATCH
} ;

static Bool pdf_unpack_params( PDFCONTEXT *pdfc , OBJECT *thedict )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT * theo ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( thedict , "thedict is NULL in pdf_unpack_params" ) ;
  HQASSERT( oType(*thedict) == ODICTIONARY ,
            "thedict must be a dictionary in pdf_unpack_params" ) ;

  if ( ! pdf_dictmatch( pdfc , thedict , params_dictmatch ))
    return FALSE ;

  /* 'encapsulated' is set for OPI and PDF external file references.  The flag
     affects page group behaviour.  The operators setpagedevice and showpage are
     controlled separately as OPI and external files have differing
     requirements. */
  if ( params_dictmatch[params_match_Encapsulated].result )
    ixc->encapsulated = oBool(*params_dictmatch[params_match_Encapsulated].result) ;

  /* setpagedevice and showpage are ignored for PDF external file references and
     some retained-raster modes.  OPI shadows the operators and therefore they
     are safe and required to be called even though OPI sets encapsulated. */
  if ( params_dictmatch[params_match_IgnoreSetPageDevice].result )
    ixc->ignore_setpagedevice =
      oBool(*params_dictmatch[params_match_IgnoreSetPageDevice].result) ;
  if ( params_dictmatch[params_match_IgnoreShowpage].result )
    ixc->ignore_showpage =
      oBool(*params_dictmatch[params_match_IgnoreShowpage].result) ;

  /* If /Strict is true then the PDF file must rigorously conform to the PDF
   * specificaton. If /Strict is false, the spec is interpreted with some
   * flexibility. For example, the spec states a Page/Pages object is required
   * to have a /Type key, however in practice Acro Reader allows files without a
   * /Type key. */
  if ( params_dictmatch[params_match_Strict].result )
    ixc->strictpdf = oBool(*params_dictmatch[params_match_Strict].result) ;

  /* Page ranges may be selected by supplying a job with the key /PageRange and
   * an array. An example of the array is: [ 1 3 [5 9] [14] ], where the actual
   * pages printed would be 1,3,5,6,7,8,9,14+. */
  if ( params_dictmatch[params_match_PageRange].result ) {
    /* Override PageRange set in currentpdfparams dictionary, treating
     * zero-length array as 'print all pages'
     */
    if ( theLen(* params_dictmatch[params_match_PageRange].result ) != 0 )
      ixc->pagerange = params_dictmatch[params_match_PageRange].result ;
    else
      ixc->pagerange = NULL ; /* Default - print all pages */
  }

  /* Password lists. */
  if ( params_dictmatch[params_match_OwnerPasswords].result ) {
    if ( theLen(* params_dictmatch[params_match_OwnerPasswords].result ) != 0 )
      ixc->ownerpasswords_local = params_dictmatch[params_match_OwnerPasswords].result ;
  }
  if ( params_dictmatch[params_match_UserPasswords].result ) {
    if ( theLen(* params_dictmatch[params_match_UserPasswords].result ) != 0 )
      ixc->userpasswords_local = params_dictmatch[params_match_UserPasswords].result ;
  }

  /* Private key lists. */
  if ( params_dictmatch[params_match_PrivateKeyFiles].result ) {
    if ( theLen(* params_dictmatch[params_match_PrivateKeyFiles].result ) != 0 )
      ixc->private_key_files_local = params_dictmatch[params_match_PrivateKeyFiles].result ;
  }
  if ( params_dictmatch[params_match_PrivateKeyPasswords].result ) {
    if ( theLen(* params_dictmatch[params_match_PrivateKeyPasswords].result ) != 0 )
      ixc->private_key_passwords_local = params_dictmatch[params_match_PrivateKeyPasswords].result ;
  }

  /* Print permissions */
#if defined( DEBUG_BUILD )
  if ( params_dictmatch[params_match_HonorPrintPermissionFlag].result )
    ixc->honor_print_permission_flag = oBool(*params_dictmatch[params_match_HonorPrintPermissionFlag].result) ;
#endif

  /* Abort on missing fonts
   * => If true, PDF job aborts when a font is missing (as if the GUI option
   *    "Abort job if any fonts are missing" is checked), overriding the GUI
   *    option
   * => If false, behaviour depends (i.e. falls thru to) GUI option
   * Default is FALSE for all builds.
   */
  if ( params_dictmatch[params_match_MissingFonts].result )
    ixc->missing_fonts = oBool(*params_dictmatch[params_match_MissingFonts].result) ;
  if ( ixc->missing_fonts ) {
    OBJECT obj = OBJECT_NOTVM_STRING("/HqnMissingFonts /ProcSet findresource begin deferredmissingfonts end") ;
    theTags(obj) |= EXECUTABLE ;
    execStackSizeNotChanged = FALSE ;
    if ( ! push(&obj, &executionstack) )
      return FALSE ;
    if ( ! interpreter(1, NULL) )
      return FALSE;
  }

  /* PageCropTo */
  if ((theo = params_dictmatch[ params_match_PageCropTo ].result) != NULL) {
    if ( oInteger(*theo) < 0 ||
         oInteger(*theo) >= PDF_PAGECROPTO_N_VALUES )
      return error_handler( RANGECHECK ) ;
    ixc->pagecropto = oInteger(*theo) ;
  }

  /* EnforcePDFVersion */
  if ((theo = params_dictmatch[ params_match_EnforcePDFVersion ].result) != NULL) {
    ixc->EnforcePDFVersion = oInteger( *theo ) ;
  }

  /* AbortForInvalidTypes */
  if ((theo = params_dictmatch[ params_match_AbortForInvalidTypes ].result) != NULL) {
    if ( oInteger(*theo) < 0 ||
         oInteger(*theo) >= PDF_INVALIDTYPES_N_VALUES )
      return error_handler( RANGECHECK ) ;
    ixc->abort_for_invalid_types = oInteger(*theo) ;
  }

  /* PrintAnnotations */
  if ((theo = params_dictmatch[ params_match_PrintAnnotations ].result) != NULL)
    ixc->PrintAnnotations = oBool(*theo) ;

  /* AnnotationParams */
  if ((theo = params_dictmatch[ params_match_AnnotationParams ].result) != NULL) {
    if (!pdf_set_annotation_params( theo, &(ixc->AnnotParams) ))
      return FALSE ;
  }

  if ((theo = params_dictmatch[ params_match_ErrorOnFlateChecksumFailure ].result) != NULL) {
    ixc->ErrorOnFlateChecksumFailure = oBool( *theo ) ;
  }

  if ((theo = params_dictmatch[ params_match_IgnorePSXObjects ].result) != NULL) {
    ixc->IgnorePSXObjects = oBool( *theo ) ;
  }

  if ((theo = params_dictmatch[ params_match_WarnSkippedPages ].result) != NULL) {
    ixc->WarnSkippedPages = oBool( *theo ) ;
  }

  if ((theo = params_dictmatch[ params_match_SizePageToBoundingBox ].result) != NULL) {
    ixc->SizePageToBoundingBox = oBool( *theo ) ;
  }

  /* OptionalContentOptions */
  if ((theo = params_dictmatch[ params_match_OptionalContentOptions ].result) != NULL) {
    if (!pdf_set_oc_params( theo, &(ixc->OptionalContentOptions) ))
      return FALSE ;
  }

  /* EnableOptimizedPDFScan */
  if ((theo = params_dictmatch[ params_match_EnableOptimizedPDFScan ].result) != NULL) {
    ixc->EnableOptimizedPDFScan = oBool( *theo ) ;
  }

  /* OptimizedPDFScanLimitPercent */
  if ((theo = params_dictmatch[ params_match_OptimizedPDFScanLimitPercent ].result) != NULL) {
    ixc->OptimizedPDFScanLimitPercent = oInteger( *theo ) ;
  }

  /* OptimizedPDFExternal */
  if ((theo = params_dictmatch[ params_match_OptimizedPDFExternal ].result) != NULL) {
    ixc->OptimizedPDFExternal = oBool( *theo ) ;
  }

  /* OptimizedPDFCacheSize */
  if ((theo = params_dictmatch[ params_match_OptimizedPDFCacheSize ].result) != NULL) {
    ixc->OptimizedPDFCacheSize = oInteger( *theo ) ;
  }

  /* OptimizedPDFCacheID */
  if ((theo = params_dictmatch[ params_match_OptimizedPDFCacheID ].result) != NULL) {
    ixc->OptimizedPDFCacheID = theo ;
  }

  /* OptimizedPDFSetupID */
  if ((theo = params_dictmatch[ params_match_OptimizedPDFSetupID ].result) != NULL) {
    ixc->OptimizedPDFSetupID = theo ;
  }

  /* OptimizedPDFCrossXObjectBoundaries */
  if ((theo = params_dictmatch[ params_match_OptimizedPDFCrossXObjectBoundaries ].result) != NULL) {
    ixc->OptimizedPDFCrossXObjectBoundaries = oBool( *theo ) ;
  }

  /* OptimizedPDFSignificanceMask */
  if (( theo = params_dictmatch[ params_match_OptimizedPDFSignificanceMask ].result) != NULL) {
    ixc->OptimizedPDFSignificanceMask = oInteger( *theo ) ;
  }

  /* OptimizedPDFScanWindow */
  if (( theo = params_dictmatch[ params_match_OptimizedPDFScanWindow ].result) != NULL) {
    ixc->OptimizedPDFScanWindow = oInteger( *theo ) ;
  }

  /* OptimizedPDFImageThreshold */
  if (( theo = params_dictmatch[ params_match_OptimizedPDFImageThreshold ].result) != NULL) {
    ixc->OptimizedPDFImageThreshold = oInteger( *theo ) ;
  }

  /* ErrorOnPDFRepair */
  if ((theo = params_dictmatch[params_match_ErrorOnPDFRepair].result) != NULL)
    ixc->ErrorOnPDFRepair = oBool(*theo);

  /* PDFXVerifyExternalProfileCheckSums */
  if ((theo = params_dictmatch[params_match_PDFXVerifyExternalProfileCheckSums].result) != NULL)
    ixc->PDFXVerifyExternalProfileCheckSums = oBool(*theo);

  /* TextStrokeAdjust */
  if ((theo = params_dictmatch[params_match_TextStrokeAdjust].result) != NULL)
    ixc->TextStrokeAdjust = (oType(*theo) == OREAL) ? oReal(*theo) : oInteger(*theo) ;

  /* XRefCacheLifetime */
  if (( theo = params_dictmatch[ params_match_XRefCacheLifetime ].result) != NULL) {
    ixc->XRefCacheLifetime = oInteger( *theo ) ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool pdf_pageref_required( PDFCONTEXT *pdfc, Bool *dopage )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( ixc->pageno > 0 , "ixc->pageno invalid" ) ;

  *dopage = TRUE;

  /* Checks whether or not the current page is that required by a reference
   * (from another PDF file).  The page reference can be either a page index
   * (which is zero based), or a page label (which is a string).
   */
  if (ixc->pPageRef == NULL)
    return TRUE;

  /* Being executed by pdf_exec_page(). */
  /* Need to find one page only.        */
  if (ixc->pageFound) { /* Required page already done; don't bother with any more. */
    *dopage = FALSE;
    return TRUE;
  }

  /* The page reference can either be an integer or a string. */
  if (oType(*(ixc->pPageRef)) == OINTEGER) {
    /* The reference is a page index (i.e. a page number minus one) */
    if (ixc->pageno - 1 == oInteger(*(ixc->pPageRef)))
      ixc->pageFound = TRUE;
  } else if (oType(*(ixc->pPageRef)) == OSTRING) {
    /* The reference is a page label. Generate the page label for
       the current page and see how it compares. */
    OBJECT label = OBJECT_NOTVM_NOTHING;
    if (!pdf_make_page_label( pdfc, &label ))
      return FALSE;

    if (oType(label) == OSTRING) {
      if ((theLen(*ixc->pPageRef) == 0  &&  theLen(label) == 0) ||
          (HqMemCmp( oString(label), theLen(label),
                     oString(*ixc->pPageRef), theLen(*ixc->pPageRef) ) == 0))
        ixc->pageFound = TRUE;

      pdf_destroy_string( pdfc, theLen(label), &label );
    }

  } else {
    return error_handler(TYPECHECK);
  }

  if (!ixc->pageFound)   /* This page is not the required page so don't print it. */
    *dopage = FALSE;

  return TRUE ;
}




/* PostScript callbacks for poking around in PDF files. */

Bool getPDFobject_(ps_context_t *pscontext)
{
  OBJECT *pdfobj ;
  OBJECT result = OBJECT_NOTVM_NOTHING ;
  OBJECT *theid ;
  OBJECT *ther ;
  OBJECT *thed = NULL ;
  int32 ssize = theStackSize( operandstack ) ;
  int32 index = 0 ;
  Bool streamDictOnly = FALSE ;
  PDFXCONTEXT *pdfxc ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( oType( *TopStack( operandstack , ssize )) == ODICTIONARY ) {
    /* We have the optional params dictionary: look one slot further
       down the stack for the rest of the getPDFobject params. */

    thed = TopStack( operandstack , ssize ) ;
    index++ ;

    if ( ssize < 2 )
      return error_handler( STACKUNDERFLOW ) ;
  }

  ther = stackindex( index , & operandstack ) ;
  if ( oType(*ther) != OINDIRECT )
    return error_handler( TYPECHECK ) ;

  theid = stackindex( index + 1 , & operandstack ) ;
  if ( oType(*theid) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  if ( ! pdf_find_execution_context( oInteger(*theid) ,
                                     pdfin_xcontext_base ,
                                     & pdfxc ))
    return error_handler( UNDEFINED ) ;

  if ( thed != NULL ) {
    enum { gpo_StreamDictOnly, gpo_n_entries } ;
    static NAMETYPEMATCH gpo_dict[gpo_n_entries + 1] = {
      { NAME_StreamDictOnly | OOPTIONAL, 1, { OBOOLEAN }},
      DUMMY_END_MATCH
    } ;

    if ( ! dictmatch( thed , gpo_dict ))
      return FALSE ;

    if ( gpo_dict[gpo_StreamDictOnly].result != NULL ) {
      streamDictOnly = oBool(*gpo_dict[gpo_StreamDictOnly].result) ;
    }
  }

  npop( index + 2 , & operandstack ) ;

  if ( ! pdf_lookupxref( pdfxc->pdfc ,
                         & pdfobj ,
                         oXRefID(*ther),
                         theGen(* ther ) ,
                         streamDictOnly ))
    return FALSE ;

  if ( pdfobj )
    return ( pdf_copyobject( NULL , pdfobj , & result ) &&
             push( & result , & operandstack )) ;
  else
    return error_handler( UNDEFINED ) ;
}

/** On the stack: the context id of the PDF file in question. Returns a PS
 * copy of the trailer dictionary (stored in the execution context).
 */
Bool getPDFtrailer_(ps_context_t *pscontext)
{
  OBJECT result = OBJECT_NOTVM_NOTHING ;
  OBJECT *theid ;
  int32 ssize = theStackSize( operandstack ) ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ssize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theid = TopStack( operandstack , ssize ) ;
  if (oType(*theid) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  if ( ! pdf_find_execution_context( oInteger(*theid) ,
                                     pdfin_xcontext_base ,
                                     & pdfxc ))
    return error_handler( UNDEFINED ) ;

  PDF_GET_IXC( ixc ) ;

  pop( & operandstack ) ;

  return ( pdf_copyobject( NULL , & ixc->pdftrailer , & result ) &&
           push( & result , & operandstack )) ;
}

/** Given a PDF file object, returns the associated dictionary. Will return
 * an ONULL if passed a PS stream.
 */
Bool getPDFstreamdict_(ps_context_t *pscontext)
{
  OBJECT result = OBJECT_NOTVM_NOTHING ;
  OBJECT *theo ;
  OBJECT strict = OBJECT_NOTVM_NAME(NAME_Strict, LITERAL) ;
  int32 ssize = theStackSize( operandstack ) ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ssize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK ) ;

  theo = streamLookupDict( theo ) ;

  if ( ! remove_hash(theo , &strict, FALSE /* no access check */) )
    return FALSE ;

  pop( & operandstack ) ;

  return ( pdf_copyobject( NULL , theo , & result ) &&
           push( & result , & operandstack )) ;
}

/** On the stack: the context id of the PDF file in question. Returns a PS
 * copy of the info dictionary.
 */
Bool getPDFinfo_(ps_context_t *pscontext)
{
  OBJECT *info ;
  OBJECT result = OBJECT_NOTVM_NOTHING ;
  OBJECT *theid ;
  int32 ssize = theStackSize( operandstack ) ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ssize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theid = TopStack( operandstack , ssize ) ;
  if (oType(*theid) != OINTEGER )
    return error_handler( TYPECHECK ) ;

  if ( ! pdf_find_execution_context( oInteger(*theid) ,
                                     pdfin_xcontext_base ,
                                     & pdfxc ))
    return error_handler( UNDEFINED ) ;

  PDF_GET_IXC( ixc ) ;
  HQASSERT( pdfxc->pdfc , "pdfc NULL in getPDFinfo" ) ;
  HQASSERT( oType(ixc->pdfinfo) == OINDIRECT ||
            oType(ixc->pdfinfo) == ONULL ,
            "pdfinfo should be indirect or null" ) ;

  if (oType(ixc->pdfinfo) == OINDIRECT ) {

    if ( ! pdf_lookupxref( pdfxc->pdfc , & info ,
                           oXRefID( ixc->pdfinfo ) ,
                           theGen( ixc->pdfinfo ) ,
                           FALSE ) ||
         ! pdf_resolvexrefs( pdfxc->pdfc , info ))
      return FALSE ;

    if ( ! pdf_copyobject( NULL , info , & result ) )
      return FALSE ;
  }
  else {

    /* The info dictionary does not exist so return an empty dictionary */
    if ( ! ps_dictionary(&result, 0) )
      return FALSE ;
  }

  pop( & operandstack ) ;

  return push( &result, &operandstack );
}

/** Given a PDF stream object on the stack, return its PDF execution context
 * ID. A zero return value means the stream doesn't have such an ID (i.e. it's
 * a PostScript object).
 */
Bool getPDFcontext_(ps_context_t *pscontext)
{
  OBJECT *theo ;
  int32 ssize = theStackSize( operandstack ) ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ssize < 0 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = TopStack( operandstack , ssize ) ;
  if (oType(*theo) != OFILE )
    return error_handler( TYPECHECK ) ;

  theTags(*theo) = OINTEGER | LITERAL ;
  oInteger(*theo) = theIPDFContextID( oFile(*theo) );

  return TRUE ;
}

/** Return a dictionary describing the current PDF context.
 * "Current" has to mean the first on the list..
 */
Bool currentpdfcontext_(ps_context_t *pscontext)
{
  PDFXCONTEXT *pdfxc = pdfin_xcontext_base ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT dict = OBJECT_NOTVM_NOTHING ;
  OBJECT theo = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! pdfxc )
    return error_handler( UNDEFINED ) ;

  PDF_GET_IXC( ixc ) ;

  if ( ! ps_dictionary(&dict, 3) )
    return FALSE ;

  object_store_integer(&theo, pdfxc->id) ;
  if ( ! fast_insert_hash_name(&dict, NAME_ContextID, &theo) )
    return FALSE ;

  object_store_integer(&theo, ixc->pageno) ;
  if ( ! fast_insert_hash_name(&dict, NAME_PageIndex, &theo) )
    return FALSE ;

  /* Return the page range from the context, or an empty array */
  if (ixc->pagerange == NULL) {
    if ( ! ps_array(&theo, 0)) {
      return FALSE ;
    }
  }
  else {
    Copy(&theo, ixc->pagerange);
  }
  if ( ! object_access_reduce(READ_ONLY, &theo) )
    return FALSE ;
  if ( ! fast_insert_hash_name(&dict, NAME_PageRange, &theo) )
    return FALSE ;

  return push( & dict , & operandstack ) ;
}


/** pdfxstatus_()
 * Return a dictionary containing status information about the current PDF/X
 * file.
 */
Bool pdfxstatus_(ps_context_t *pscontext)
{
  PDFXCONTEXT *pdfxc = pdfin_xcontext_base;
  PDF_IXC_PARAMS *ixc;
  OBJECT dict = OBJECT_NOTVM_NOTHING;
  OBJECT versionClaimed = OBJECT_NOTVM_NOTHING,
    outputCondition = OBJECT_NOTVM_NOTHING;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (!pdfxc)
    return error_handler( UNDEFINED ) ;

  PDF_GET_IXC( ixc );

  if (!ps_dictionary(&dict, 4) )
    return FALSE;

  /* Copy the compound objects into PS VM. */
  if (! pdf_copyobject(NULL, &ixc->PDFXVersionClaimed, &versionClaimed) ||
      ! pdf_copyobject(NULL, &ixc->PDFXOutputCondition, &outputCondition))
    return FALSE;

  if ( !fast_insert_hash_name(&dict, NAME_PDFXVersionClaimed, &versionClaimed) ||
       !fast_insert_hash_name(&dict, NAME_PDFXVersionTreated, &ixc->PDFXVersionTreated) ||
       !fast_insert_hash_name(&dict, NAME_PDFXVersionValid, &ixc->PDFXVersionValid) ||
       !fast_insert_hash_name(&dict, NAME_PDFXOutputCondition, &outputCondition) )
    return FALSE ;

  if (!push( &dict, &operandstack ))
    return FALSE;

  return TRUE;
}

void init_C_globals_pdfexec(void)
{
#if defined( ASSERT_BUILD )
  pdftrace_separation_info = FALSE ;
  debug_xrefscan = FALSE ;
  pdftrace_header = TRUE ;
  pdftrace_psop = FALSE ;
#endif
}

/*
* Log stripped */

