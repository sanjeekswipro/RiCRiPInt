/** \file
 * \ingroup ps
 *
 * $HopeName: COREgstate!color:src:gscxfer.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS transfer array functions
 */

#include "core.h"

#include "control.h"            /* topDictStackObj */
#include "dlstate.h"            /* page->color */
#include "fileio.h"             /* FILELIST */
#include "functns.h"            /* fn_evaluate */
#include "gcscan.h"             /* ps_scan_field */
#include "gu_chan.h"            /* guc_colorantIndexPossiblyNewName */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "miscops.h"            /* in_super_exec */
#include "mm.h"                 /* mm_sac_alloc */
#include "mps.h"                /* mps_res_t */
#include "namedef_.h"           /* NAME_* */
#include "objects.h"            /* oType */
#include "objstack.h"           /* theIStackSize */
#include "spdetect.h"           /* enable_separation_detection */
#include "stacks.h"             /* temporarystack */
#include "rcbcntrl.h"           /* rcbn_enabled */
#include "routedev.h"           /* DEVICE_INVALID_CONTEXT */
#include "swerrors.h"           /* TYPECHECK */

#include "gs_colorpriv.h"       /* CLINK */
#include "gs_callps.h"          /* call_psproc */
#include "gsccalibpriv.h"       /* cc_applyCalibrationInterpolation */
#include "gschtonepriv.h"       /* cc_halftonetransferinfo */

#include "gscxferpriv.h"        /* extern's */


struct CLINKTRANSFERinfo {
  Bool              isFirstLink ;
  Bool              isFinalLink ;

  Bool              flipColorValueAtStart ;
  Bool              flipColorValueAtEnd ;

  OBJECT            **htTransfersForLink ;       /* [n_iColorants], points to halftonetransfers[n]*/
  OBJECT            **xfTransfersForLink ;       /* [n_iColorants], points to transferInfo->transfers[n] */

  int32*            htTransferOffsets ;          /* [n_iColorants] xf transfer offset for each colorant */
  int32*            xfTransferOffsets ;          /* [n_iColorants] ht transfer offset for each colorant */

  int32             halftoneId ;
  int32             transferId ;

  Bool              forcePositiveEnabled ;
} ;

/* Array to give names for Cyan, Magenta, Yellow, Black,
 * Red, Green, Blue, Gray  Default.
 */
static struct stdNames {
  int32 name ;
  int32 complementaryIndex;
  int32 index ;
} xfer_cmykrgbg[] = {
  { NAME_Cyan    , 4, 0 } ,
  { NAME_Magenta , 5, 1 } ,
  { NAME_Yellow  , 6, 2 } ,
  { NAME_Black   , 7, 3 } ,
  { NAME_Red     , 0, 0 } ,
  { NAME_Green   , 1, 1 } ,
  { NAME_Blue    , 2, 2 } ,
  { NAME_Gray    , 3, 3 } ,
} ;

#define TRANSFER_INDBLACK   ( 3 )
#define TRANSFER_NAMES_MAX  NUM_ARRAY_ITEMS(xfer_cmykrgbg)

struct GS_TRANSFERinfo {
  cc_counter_t      refCnt ;
  size_t            structSize ;

  int32             transferId ;
  OBJECT            transfers[ 4 ] ;

  /* Transfer functions are applied in the frontend and therefore we don't want
     to apply them again when doing recombine adjustment or compositing. */
  Bool              transfersPreapplied ;

  /* A cache of colorant indexes for the standard colors defined in
   * xfer_cmykrgbg, based on the rasterstyle id.  The colorant index of the
   * complementary color can be found by indexing via complementaryIndex in
   * xfer_cmykrgbg.
   */
  uint32            rasterstyle_id;
  COLORANTINDEX     std_colorant[TRANSFER_NAMES_MAX];
} ;

static void  transfer_destroy( CLINK *pLink ) ;
static Bool transfer_invokeSingle( CLINK *pLink , USERVALUE *oColorValues ) ;
#ifdef INVOKEBLOCK_NYI
static Bool cc_transfer_invokeBlock( CLINK *pLink , CLINKblock *pBlock ) ;
#endif /* INVOKEBLOCK_NYI */
static mps_res_t transfer_scan( mps_ss_t ss, CLINK *pLink );

static Bool finish_initialising_transfer_link( CLINK *pLink ,
                                               Bool jobColorSpaceIsGray ,
                                               GS_TRANSFERinfo *transferInfo ,
                                               GS_HALFTONEinfo *halftoneInfo ,
                                               uint8 reproType,
                                               COLOR_PAGE_PARAMS *colorPageParams ,
                                               Bool *pfUseAllSetTransfer ,
                                               Bool *pfTransferFn ,
                                               Bool *pfHalftoneFn ,
                                               Bool applyJobPolarity ,
                                               GUCR_RASTERSTYLE* rasterstyle) ;
static void finish_dummy_transfer_link( CLINK *pLink ,
                                        Bool *pfUseAllSetTransfer ,
                                        Bool *pfTransferFn ,
                                        Bool *pfHalftoneFn ,
                                        Bool applyJobPolarity ,
                                        Bool negativeJob ) ;
static void get_flipColorValues( CLINK  *pLink,
                                 Bool   applyJobPolarity,
                                 Bool   negativeJob ) ;
static Bool apply_01_callback( USERVALUE value ,
                               USERVALUE *result ,
                               OBJECT *proc ) ;
static Bool apply_ht_transfer_function( USERVALUE *color ,
                                        OBJECT    *htxfer ,
                                        int32     htfnindex ,
                                        int32     halftoneId ) ;
static Bool apply_transfer_function( USERVALUE *color ,
                                     OBJECT    *xfer ,
                                     int32     setfnindex ,
                                     int32     transferId ) ;

#if defined( ASSERT_BUILD )
static void transferAssertions( CLINK *pLink ) ;
static void transferInfoAssertions( GS_TRANSFERinfo *pInfo ) ;
#else
#define transferAssertions( pLink )     EMPTY_STATEMENT()
#define transferInfoAssertions( pInfo ) EMPTY_STATEMENT()
#endif

static size_t transferStructSize( int32 nColorants ) ;
static void transferUpdatePtrs( CLINK *pLink ,
                                int32 nColorants ) ;
static Bool immediate( OBJECT *psObject[ 4 ] , int32 nargs ) ;

static Bool createtransferinfo( GS_TRANSFERinfo **transferInfo ) ;
static Bool updatetransferinfo( GS_TRANSFERinfo **transferInfo ) ;
static Bool copytransferinfo( GS_TRANSFERinfo *transferInfo ,
                              GS_TRANSFERinfo **transferInfoCopy ) ;

static Bool finalise_xfer( GS_COLORinfo *colorInfo ,
                           int32 *pnUniqueID ,
                           OBJECT *psObject[ 4 ] , int32 nargs ,
                           Bool fImmediate ) ;


static CLINKfunctions CLINKtransfer_functions =
{
    transfer_destroy ,
    transfer_invokeSingle ,
    NULL /* cc_transfer_invokeBlock */,
    transfer_scan
} ;

/* The transferId of a set of empty transfer functions. This allows optimisations
 * and doesn't require bumping TransferFunctionId.
 */
#define TRANSFER_ID_EMPTY   ( 0 )

/* ---------------------------------------------------------------------- */

/*
 * Transfer Link Data Access Functions
 * ===================================
 */

CLINK *cc_transfer_create(int32             nColorants,
                          COLORANTINDEX     *colorants,
                          COLORSPACE_ID     colorSpace,
                          uint8             reproType,
                          int32             jobColorSpaceIsGray,
                          int32             isFirstTransferLink,
                          int32             isFinalTransferLink,
                          int32             applyJobPolarity,
                          GS_TRANSFERinfo   *transferInfo,
                          GS_HALFTONEinfo   *halftoneInfo,
                          GUCR_RASTERSTYLE  *rasterstyle,
                          Bool              forcePositiveEnabled,
                          COLOR_PAGE_PARAMS *colorPageParams)
{
  CLINK *pLink ;
  CLINKTRANSFERinfo *transfer ;

  Bool fUseAllSetTransfer = FALSE;
  Bool fTransferFn = FALSE;
  Bool fHalftoneFn = FALSE;

  HQASSERT(BOOL_IS_VALID(jobColorSpaceIsGray),
           "jobColorSpaceIsGray is not normalised in cc_transfer_create");

  pLink = cc_common_create( nColorants ,
                            colorants ,
                            colorSpace ,
                            colorSpace ,
                            CL_TYPEtransfer ,
                            transferStructSize( nColorants ) ,
                            & CLINKtransfer_functions ,
                            CLID_SIZEtransfer ) ;
  if ( pLink == NULL )
    return NULL ;

  transferUpdatePtrs( pLink , nColorants ) ;

  transfer = pLink->p.transfer ;

  transfer->isFirstLink = isFirstTransferLink ;
  transfer->isFinalLink = isFinalTransferLink ;

  transfer->halftoneId = cc_gethalftoneid( halftoneInfo ) ;
  transfer->transferId = transferInfo->transferId ;

  transfer->forcePositiveEnabled = forcePositiveEnabled;

  if (!finish_initialising_transfer_link( pLink ,
                                          jobColorSpaceIsGray ,
                                          transferInfo ,
                                          halftoneInfo, reproType,
                                          colorPageParams,
                                          & fUseAllSetTransfer ,
                                          & fTransferFn ,
                                          & fHalftoneFn ,
                                          applyJobPolarity ,
                                          rasterstyle )) {
    cc_common_destroy(pLink);
    return NULL;
  }

  /* Now populate the CLID slots:
   * We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   * For CL_TYPEtransfer (looking at invokeSingle) we have:
   * a) flipColorValueAtStart (1 bit)
   * b) isFirstLink (1 bit (use isFirstTransferLink because isFirstLink may be
                            modified and cause asserts in device code link))
   * c) htTransfersForLink (32 bits (halftoneId if some exist))
   * d) xfTransfersForLink (32 bits (transferId if some exist))
   * e) flipColorValueAtEnd (1 bit)
   * +
   * f) application of colorPageParams->useAllSetTransfer (1 bit)
   * g) application of jobColorSpaceIsGray (1 bit)
   * =
   * 3 slots.
   */
  { CLID *idslot = pLink->idslot ;
    HQASSERT( pLink->idcount == CLID_SIZEtransfer , "Didn't create as requested" ) ;
    HQASSERT( !fTransferFn || transfer->transferId > 0 ,
              "population of idslot[ 1 ] will be incorrect" ) ;
    HQASSERT( !fHalftoneFn || transfer->halftoneId > 0 ,
              "population of idslot[ 2 ] will be incorrect" ) ;
    idslot[ 0 ] = ( isFirstTransferLink             ? 0x01 : 0x00 ) |
                  ( transfer->flipColorValueAtStart ? 0x02 : 0x00 ) |
                  ( transfer->flipColorValueAtEnd   ? 0x04 : 0x00 ) |
                  ( fUseAllSetTransfer              ? 0x08 : 0x00 );
    idslot[ 1 ] = fTransferFn ? transfer->transferId : 0 ;
    idslot[ 2 ] = fHalftoneFn ? transfer->halftoneId : 0 ;
  }

  transferAssertions( pLink ) ;

  return pLink ;
}

CLINK *cc_dummy_transfer_create( int32           nColorants ,
                                 COLORANTINDEX   *colorants ,
                                 COLORSPACE_ID   colorSpace ,
                                 Bool            isFirstTransferLink ,
                                 Bool            isFinalTransferLink ,
                                 Bool            applyJobPolarity ,
                                 Bool            negativeJob )
{
  CLINK *pLink ;
  CLINKTRANSFERinfo *transfer ;

  Bool fUseAllSetTransfer ;
  Bool fTransferFn ;
  Bool fHalftoneFn ;

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   */
  pLink = cc_common_create( nColorants ,
                            colorants ,
                            colorSpace ,
                            colorSpace ,
                            CL_TYPEtransfer ,
                            transferStructSize( nColorants ) ,
                            & CLINKtransfer_functions ,
                            CLID_SIZEdummytransfer ) ;
  if ( pLink == NULL )
    return NULL ;

  transferUpdatePtrs( pLink , nColorants ) ;

  transfer = pLink->p.transfer ;

  transfer->isFirstLink = isFirstTransferLink ;
  transfer->isFinalLink = isFinalTransferLink ;

  /* These Ids are invalid, but never used. */

  transfer->halftoneId = 0 ;
  transfer->transferId = 0 ;

  transfer->forcePositiveEnabled = FALSE;

  finish_dummy_transfer_link( pLink ,
                              & fUseAllSetTransfer ,
                              & fTransferFn ,
                              & fHalftoneFn ,
                              applyJobPolarity ,
                              negativeJob ) ;

  /* Now populate the CLID slots:
   * We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants are defined as fixed.
   * For CL_TYPEdummytransfer (looking at invokeSingle) we have:
   * a) flipColorValueAtStart (1 bit)
   * b) flipColorValueAtEnd (1 bit)
   * 3 slots assumed elsewhere, but only one slot actually used.
   */
  {
    CLID *idslot = pLink->idslot ;
    HQASSERT( pLink->idcount == CLID_SIZEdummytransfer ,
              "Didn't create as requested" ) ;
    idslot[ 0 ] = ( transfer->flipColorValueAtStart ? 0x02 : 0x00 ) |
                  ( transfer->flipColorValueAtEnd   ? 0x04 : 0x00 );
    idslot[ 1 ] = 0 ;
    idslot[ 2 ] = 0 ;
  }

  transferAssertions( pLink ) ;

  return pLink ;
}


static void transfer_destroy( CLINK *pLink )
{
  transferAssertions( pLink ) ;

  cc_common_destroy( pLink ) ;
}

/* WARNING; IF YOU UPDATE THIS ROUTINE MAKE SURE YOU MODIFY THE DEPENDENCIES IN
 *          cc_transfer_create FOR SETTING UP THE CLID slots.
 */
static Bool transfer_invokeSingle( CLINK *pLink, USERVALUE *oColorValues )
{
  int32             i ;
  CLINKTRANSFERinfo *transfer ;

  transferAssertions( pLink ) ;
  HQASSERT( oColorValues != NULL , "oColorValues == NULL" ) ;

  transfer = pLink->p.transfer ;
  /* If this is the first transfer link in the chain, apply transfers. */
  if ( transfer->isFirstLink ) {
    GSC_BLACK_TYPE blackType = pLink->blackType ;
    for ( i = 0 ; i < pLink->n_iColorants ; ++i ) {
      USERVALUE colorValue = pLink->iColorValues[ i ] ;
      USERVALUE tcolorValue = colorValue ;
      COLOR_01_ASSERT( tcolorValue , "transfer input" ) ;
      if ( transfer->flipColorValueAtStart )
        tcolorValue = 1.0f - tcolorValue ;
      { OBJECT *thexfer = transfer->xfTransfersForLink[ i ] ;
        if ( thexfer != NULL ) {
          if ( ! apply_transfer_function( & tcolorValue , thexfer ,
                                          transfer->xfTransferOffsets[ i ],
                                          transfer->transferId ))
            return FALSE ;
        }
      }
      { OBJECT *thehtxfer = transfer->htTransfersForLink[ i ] ;
        if ( thehtxfer != NULL ) {
          if ( ! apply_ht_transfer_function( & tcolorValue , thehtxfer ,
                                             transfer->htTransferOffsets[ i ],
                                             transfer->halftoneId ))
            return FALSE ;
          COLOR_01_ASSERT( tcolorValue , "halftone transfer output" ) ;
        }
      }
      if ( transfer->flipColorValueAtEnd )
        tcolorValue = 1.0f - tcolorValue ;
      oColorValues[ i ] = tcolorValue ;

      /* We had 100% black, but now we don't, so don't preserve it after all. */
      if (blackType == BLACK_TYPE_100_PC && colorValue != tcolorValue)
        blackType = BLACK_TYPE_TINT ;
      COLOR_01_ASSERT( tcolorValue , "transfer output" ) ;
    }
    pLink->blackType = blackType ;
  }
  else {
    GSC_BLACK_TYPE blackType = pLink->blackType ;
    for ( i = 0 ; i < pLink->n_iColorants ; ++i ) {
      USERVALUE colorValue = pLink->iColorValues[ i ] ;
      USERVALUE tcolorValue = colorValue ;
      COLOR_01_ASSERT( colorValue , "transfer function input" ) ;
      if ( transfer->flipColorValueAtStart )
        tcolorValue = 1.0f - tcolorValue ;
      if ( transfer->flipColorValueAtEnd )
        tcolorValue = 1.0f - tcolorValue ;
      oColorValues[ i ] = tcolorValue ;

      /* We had 100% black, but now we don't (inverted), so don't preserve it after all. */
      if (blackType == BLACK_TYPE_100_PC && colorValue != tcolorValue)
        blackType = BLACK_TYPE_TINT ;
      COLOR_01_ASSERT( tcolorValue , "transfer function output" ) ;
    }
  }
  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool cc_transfer_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM( CLINK * , pLink ) ;
  UNUSED_PARAM( CLINKblock * , pBlock ) ;

  transferAssertions( pLink ) ;

  return TRUE ;
}
#endif


/*  transfer_scan - scan the transfer section of a CLINK */
static mps_res_t transfer_scan( mps_ss_t ss, CLINK *pLink )
{
  UNUSED_PARAM( mps_ss_t, ss ); UNUSED_PARAM( CLINK *, pLink );
  /* transfer->htTransfersForLink[i] are pointers into another color pool */
  /* structure, the halftoneInfo in the gstate.  We already mark that */
  /* through the gstate.  Presumably the lifetime of that includes the */
  /* lifetime of this color chain, and we don't need to mark here. */
  /* Likewise transfer->xfTransfersForLink[i] to transferInfo. */
  return MPS_RES_OK;
}


static size_t transferStructSize( int32 nColorants )
{
  return sizeof( CLINKTRANSFERinfo ) +
         nColorants * sizeof( OBJECT * ) +    /* htTransfersForLink */
         nColorants * sizeof( OBJECT * ) +    /* xfTransfersForLink */
         nColorants * sizeof( int32 ) +       /* htTransferOffsets */
         nColorants * sizeof( int32 ) ;       /* xfTransferOffsets */
}

static void transferUpdatePtrs( CLINK *pLink ,
                                int32 nColorants )
{
  pLink->p.transfer = ( CLINKTRANSFERinfo * )(( uint8 * )pLink + cc_commonStructSize( pLink )) ;
  pLink->p.transfer->htTransfersForLink = ( OBJECT ** )( pLink->p.transfer + 1 ) ;
  pLink->p.transfer->xfTransfersForLink = ( OBJECT ** )( pLink->p.transfer->htTransfersForLink + nColorants ) ;
  pLink->p.transfer->htTransferOffsets = ( int32* )( pLink->p.transfer->xfTransfersForLink + nColorants ) ;
  pLink->p.transfer->xfTransferOffsets = ( int32* )( pLink->p.transfer->htTransferOffsets + nColorants ) ;
}

#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void transferAssertions( CLINK *pLink )
{
  cc_commonAssertions( pLink ,
                       CL_TYPEtransfer ,
                       transferStructSize( pLink->n_iColorants ) ,
                       & CLINKtransfer_functions ) ;

  HQASSERT( pLink->p.transfer == ( CLINKTRANSFERinfo * ) (( uint8 * )pLink + cc_commonStructSize( pLink )) ,
            "transfer data not set" ) ;
  HQASSERT( pLink->p.transfer->htTransfersForLink == ( OBJECT ** )( pLink->p.transfer + 1 ) ,
            "htTransfersForLink not set" ) ;
  HQASSERT( pLink->p.transfer->xfTransfersForLink == ( OBJECT ** )( pLink->p.transfer->htTransfersForLink + pLink->n_iColorants ) ,
            "xfTransfersForLink not set" ) ;
  HQASSERT( pLink->p.transfer->htTransferOffsets == ( int32* )( pLink->p.transfer->xfTransfersForLink + pLink->n_iColorants ) ,
            "htTransferOffset not set" ) ;
  HQASSERT( pLink->p.transfer->xfTransferOffsets == ( int32* )( pLink->p.transfer->htTransferOffsets + pLink->n_iColorants ) ,
            "xfTransferOffset not set" ) ;

  switch ( pLink->iColorSpace ) {
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGBK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceCMY:
  case SPACE_DeviceGray:
  case SPACE_DeviceK:
  case SPACE_Separation:
  case SPACE_DeviceN:
  case SPACE_TrapDeviceN:
    break ;
  default:
    HQFAIL( "Bad input color space" ) ;
    break ;
  }
}
#endif

/* ---------------------------------------------------------------------- */

/*
 * Transfer Info Data Access Functions
 * ===================================
 */

static Bool createtransferinfo( GS_TRANSFERinfo   **transferInfo )
{
  GS_TRANSFERinfo *pInfo ;
  size_t          structSize ;
  int32           i ;

  structSize = sizeof( GS_TRANSFERinfo ) ;

  pInfo = ( GS_TRANSFERinfo * ) mm_sac_alloc( mm_pool_color ,
                                              structSize ,
                                              MM_ALLOC_CLASS_NCOLOR ) ;
  *transferInfo = pInfo ;
  if ( pInfo == NULL )
    return error_handler( VMERROR ) ;

  pInfo->refCnt = 1 ;
  pInfo->structSize = structSize ;

  for ( i = 0 ; i < 4 ; ++i )
    pInfo->transfers[i] = onull; /* set slot properties */
  pInfo->transferId = 0 ;

  pInfo->transfersPreapplied = FALSE ;

  for ( i = 0 ; i < TRANSFER_NAMES_MAX; i++ ) {
    pInfo->std_colorant[i] = COLORANTINDEX_UNKNOWN;
  }
  pInfo->rasterstyle_id = 0;

  transferInfoAssertions( pInfo ) ;

  return TRUE ;
}

Bool cc_sametransferhalftoneinfo (void * pvHalftoneInfo, CLINK * pLink)
{
  /* Is the transfer link referring to the same halftone transfers as the halftone
     info structure. This is so if they share id's */

  GS_HALFTONEinfo * pHalftoneInfo = (GS_HALFTONEinfo *) pvHalftoneInfo;

  HQASSERT (pvHalftoneInfo != NULL, "pvHalftoneInfo is NULL in cc_sametransferhalftoneinfo");
  transferAssertions (pLink);

  return cc_gethalftoneid (pHalftoneInfo) == pLink->p.transfer->halftoneId;
}


Bool cc_sametransferinfo (void * pvTransferInfo, CLINK * pLink)
{
  /* Is the transfer link referring to the same information as the transfer
     info. These aren't the same structures (unlike some links) but do contain the
     same ids when they share pointers to common objects */

  GS_TRANSFERinfo * pTransferInfo = (GS_TRANSFERinfo *) pvTransferInfo;

  transferInfoAssertions (pTransferInfo);
  transferAssertions (pLink);

  return pTransferInfo->transferId == pLink->p.transfer->transferId;
}

static void freetransferinfo( GS_TRANSFERinfo *transferInfo )
{
  mm_sac_free( mm_pool_color, transferInfo, transferInfo->structSize ) ;
}

void cc_destroytransferinfo( GS_TRANSFERinfo **transferInfo )
{
  if ( *transferInfo != NULL ) {
    transferInfoAssertions( *transferInfo ) ;
    CLINK_RELEASE(transferInfo, freetransferinfo);
  }
}

void cc_reservetransferinfo( GS_TRANSFERinfo *transferInfo )
{
  if ( transferInfo != NULL ) {
    transferInfoAssertions( transferInfo ) ;
    CLINK_RESERVE( transferInfo ) ;
  }
}

static Bool updatetransferinfo( GS_TRANSFERinfo **transferInfo )
{
  if ( *transferInfo == NULL )
    return createtransferinfo(transferInfo);

  transferInfoAssertions(*transferInfo);
  CLINK_UPDATE(GS_TRANSFERinfo, transferInfo,
               copytransferinfo, freetransferinfo);
  return TRUE;
}

static Bool copytransferinfo( GS_TRANSFERinfo *transferInfo ,
                              GS_TRANSFERinfo **transferInfoCopy )
{
  GS_TRANSFERinfo *pInfoCopy ;

  pInfoCopy = ( GS_TRANSFERinfo * )mm_sac_alloc( mm_pool_color ,
                                                 transferInfo->structSize ,
                                                 MM_ALLOC_CLASS_NCOLOR ) ;
  if ( pInfoCopy == NULL )
    return error_handler( VMERROR ) ;

  *transferInfoCopy = pInfoCopy ;
  HqMemCpy( pInfoCopy , transferInfo , transferInfo->structSize ) ;

  pInfoCopy->refCnt = 1 ;

  return TRUE ;
}

Bool cc_aretransferobjectslocal(corecontext_t *corecontext,
                                GS_TRANSFERinfo *transferInfo )
{
  int32 i ;

  if ( transferInfo == NULL )
    return FALSE ;

  for ( i = 0 ; i < 4 ; ++i ) {
    if ( illegalLocalIntoGlobal(&transferInfo->transfers[i], corecontext) )
      return TRUE ;
  }
  return FALSE ;
}


/* cc_scan_transfer - scan the given transfer info
 *
 * This should match gsc_aretransferobjectslocal, since both need look at
 * all the VM pointers. */
mps_res_t cc_scan_transfer( mps_ss_t ss, GS_TRANSFERinfo *transferInfo )
{
  mps_res_t res;
  size_t i;

  if ( transferInfo == NULL )
    return MPS_RES_OK;

  for ( i = 0 ; i < 4 ; ++i ) {
    res = ps_scan_field( ss, &transferInfo->transfers[ i ] );
    if ( res != MPS_RES_OK ) return res;
  }
  return MPS_RES_OK;
}


#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the transfer info access functions.
 */
static void transferInfoAssertions(GS_TRANSFERinfo *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->structSize == sizeof(GS_TRANSFERinfo),
           "structure size not correct");
  HQASSERT(pInfo->transferId >= 0, "transferid not set");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
}
#endif

/* ---------------------------------------------------------------------- */

/* Find which transfer functions to use.
 *
 * The rules here determine which of the transfer functions will be applied
 * (which of the transfer functions are non-null).
 * When intercepting, we only apply transfers in the first of 2 transfer links,
 * the other transfer link is restricted to applying any necessary
 * forcepositive inversion.
 * With no interception, there is only one transfer link after all color space
 * conversions.
 *
 * There are several cases to consider.
 * 1a. cmyk/rgb/gray       -> cmyk/rgb/gray  no interception
 * 1b. cmyk/rgb/gray       -> cmyk/rgb/gray  with interception
 * 2a. Separation/deviceN  -> cmyk/rgb/gray  no interception
 * 2b. Separation/deviceN  -> cmyk/rgb/gray  with interception
 * 3a. cmyk/rgb/gray       -> deviceN        no interception
 * 3b. cmyk/rgb/gray       -> deviceN        with interception, crd in device space
 * 3c. cmyk/rgb/gray       -> deviceN        with interception, crd in cmyk/rgb/gray space
 * 4a. Separation/deviceN  -> deviceN        no interception, direct rendering
 * 4b. Separation/deviceN  -> deviceN        no interception, indirect rendering
 * 4c. Separation/deviceN  -> deviceN        with interception, indirect rendering
 *
 */
static Bool finish_initialising_transfer_link( CLINK *pLink ,
                                               Bool jobColorSpaceIsGray ,
                                               GS_TRANSFERinfo *transferInfo ,
                                               GS_HALFTONEinfo *halftoneInfo ,
                                               uint8 reproType,
                                               COLOR_PAGE_PARAMS *colorPageParams ,
                                               Bool *pfUseAllSetTransfer ,
                                               Bool *pfTransferFn ,
                                               Bool *pfHalftoneFn ,
                                               Bool applyJobPolarity ,
                                               GUCR_RASTERSTYLE* rasterstyle)
{
  int32 i , j ;
  CLINKTRANSFERinfo *transfer ;
  int32         nColorants ;
  OBJECT        *defaultHtTransfer ;
  int32         defaultHtOffset ;

  nColorants = pLink->n_iColorants ;

  transfer = pLink->p.transfer ;

  /* Update cache of colorant indexes for standard colors if the rasterstyle has
   * changed based on its id.
   */
  if ( transferInfo->rasterstyle_id != guc_rasterstyleId(rasterstyle) ) {
    transferInfo->rasterstyle_id = guc_rasterstyleId(rasterstyle);
    for ( i = 0; i < TRANSFER_NAMES_MAX; ++i ) {
      if ( !guc_colorantIndexPossiblyNewName(rasterstyle,
                                             system_names + xfer_cmykrgbg[i].name,
                                             &transferInfo->std_colorant[i]) ) {
        return FALSE;
      }
    }
  }

  /* First find the default halftone entry (index of COLORANTINDEX_NONE). */
  if (!cc_halftonetransferinfo( halftoneInfo, COLORANTINDEX_NONE, reproType,
                                & defaultHtTransfer, & defaultHtOffset,
                                rasterstyle ))
    return FALSE;

  /* Map the htTransfersForLink for each input colorant onto elements of the
   * halftonetransfers array
   */
  for ( i = 0 ; i < nColorants ; ++i ) {
    OBJECT *htTransfer = NULL;
    int32 htOffset = -1;

    /* First look for the real colorant. */
    if (!cc_halftonetransferinfo( halftoneInfo, pLink->iColorants[i], reproType,
                                  & htTransfer, & htOffset,
                                  rasterstyle))
      return FALSE;

    /* If that fails, look for the complementary color (for std device colorants). */
    if ( htTransfer == NULL ) {
      for ( j = 0 ; j < TRANSFER_NAMES_MAX ; ++j ) {
        if ( pLink->iColorants[i] == transferInfo->std_colorant[j] ) {
          if (!cc_halftonetransferinfo( halftoneInfo,
                                        transferInfo->std_colorant[xfer_cmykrgbg[j].complementaryIndex],
                                        reproType,
                                        & htTransfer, & htOffset,
                                        rasterstyle))
            return FALSE;
          break ;
        }
      }

      /* If that fails, use the default. */
      if ( htTransfer == NULL ) {
        htTransfer = defaultHtTransfer ;
        htOffset = defaultHtOffset ;
      }
    }

    /* Don't apply ht transfers if the object is null. [This means that a
     * dictionary entry for the colorant exists, but it doesn't contain a
     * TransferFunction.]
     */
    if ( htTransfer != NULL )
      if ( oType( *htTransfer ) == ONULL ) {
        htTransfer = NULL ;
        htOffset = -1 ;
      }

    transfer->htTransfersForLink[ i ] = htTransfer ;
    transfer->htTransferOffsets[ i ] = htOffset ;
  }

  *pfTransferFn = *pfHalftoneFn = FALSE ;
  *pfUseAllSetTransfer = FALSE ;

  /* Map the xfTransfersForLink for each input colorant onto elements of the
   * transfers array
   */
  for ( i = 0 ; i < nColorants ; ++i ) {
    COLORANTINDEX iColorant ;
    OBJECT *xfTransfer ;
    OBJECT *htTransfer ;
    int32 xfOffset ;

    /* Start with the black/gray as the default */
    xfTransfer = & transferInfo->transfers[ TRANSFER_INDBLACK ] ;
    xfOffset = TRANSFER_INDBLACK ;

    iColorant = pLink->iColorants[ i ] ;
    for ( j = 0 ; j < TRANSFER_NAMES_MAX ; ++j )
      if ( iColorant == transferInfo->std_colorant[j] ) {
        xfTransfer = & transferInfo->transfers[ xfer_cmykrgbg[ j ].index ] ;
        xfOffset = xfer_cmykrgbg[ j ].index ;
        break ;
      }

    /* Don't apply transfers if the object is null. */
    if ( oType( *xfTransfer ) == ONULL ) {
      xfTransfer = NULL ;
      xfOffset = -1 ;
    }

    /* Dont apply transfers if the ht transfers will be applied (as per RB2)
     * unless our user param is on.
     */
    htTransfer = transfer->htTransfersForLink[ i ] ;
    if ( htTransfer != NULL ) {
      if ( oType( *htTransfer ) != OFILE &&
           theLen(*htTransfer) == 0 ) {
        transfer->htTransfersForLink[ i ] = NULL ;
        transfer->htTransferOffsets[ i ] = -1 ;
      } else
        *pfHalftoneFn = TRUE ;
      if ( ! colorPageParams->useAllSetTransfer ) {
        if ( xfTransfer != NULL )
          *pfUseAllSetTransfer = TRUE ;
        xfTransfer = NULL ;
        xfOffset = -1 ;
      }
    }

    /* Dont apply CMY transfers if the current space is gray and the device
     * space is CMYK, see last paragraph of RB2, sec 6.3 p309.
     */
    if (pLink->oColorSpace == SPACE_DeviceCMYK) {
      if (jobColorSpaceIsGray &&
          ! rcbn_enabled() &&
          iColorant != transferInfo->std_colorant[TRANSFER_INDBLACK] ) {
        xfTransfer = NULL ;
      }
    }

    /* Optimise out execution of zero length procedures (and pick up fn flags). */
    if ( xfTransfer != NULL ) {
      if ( oType( *xfTransfer ) != OFILE &&
           theLen( *xfTransfer ) == 0 ) {
        xfTransfer = NULL ;
        xfOffset = -1 ;
      } else
        *pfTransferFn = TRUE ;
    }

    transfer->xfTransfersForLink[ i ] = xfTransfer ;
    transfer->xfTransferOffsets[ i ] = xfOffset ;
    HQASSERT( xfTransfer == NULL ||
              ( xfTransfer >= & transferInfo->transfers[ 0 ] &&
                xfTransfer <  & transferInfo->transfers[ 4 ] ) ,
              "Invalid transfer address");
  }

  get_flipColorValues( pLink, applyJobPolarity, colorPageParams->negativeJob ) ;

  return TRUE;
}


/* ---------------------------------------------------------------------- */

/* No transfer functions are used in the dummy transfer link for halftone
 * only chains (e.g. for trap insertion). But we do want the colorant
 * flipping which would normally be the responsibility of the fully-
 * fledged transfer clink.
 */

static void finish_dummy_transfer_link( CLINK *pLink ,
                                        Bool *pfUseAllSetTransfer ,
                                        Bool *pfTransferFn ,
                                        Bool *pfHalftoneFn ,
                                        Bool applyJobPolarity ,
                                        Bool negativeJob )
{
  int32 i ;
  CLINKTRANSFERinfo *transfer ;
  int32 nColorants ;

  nColorants = pLink->n_iColorants ;

  transfer = pLink->p.transfer ;

  for ( i = 0 ; i < nColorants ; ++i ) {
    transfer->htTransfersForLink[ i ] = NULL ;
    transfer->xfTransfersForLink[ i ] = NULL ;
  }

  *pfTransferFn = *pfHalftoneFn = FALSE ;
  *pfUseAllSetTransfer = FALSE ;

  get_flipColorValues( pLink, applyJobPolarity, negativeJob ) ;
}

static void get_flipColorValues( CLINK *pLink,
                                 Bool applyJobPolarity,
                                 Bool negativeJob)
{
  CLINKTRANSFERinfo *transfer ;
  Bool forcePositive = CoreContext.page->forcepositive;

  transfer = pLink->p.transfer ;

  /* Anything which uses 'tint' values is a subtractive space; transfer functions
   * are in additive according to the red book. We will switch back after the
   * transfers have been applied if neccessary. RGB and Gray are additive spaces anyway.
   */
  if ( DeviceColorspaceIsAdditive(pLink->iColorSpace) )
    transfer->flipColorValueAtStart = FALSE ;
  else {
    transfer->flipColorValueAtStart = TRUE ;
    HQASSERT( ColorspaceIsSubtractive(pLink->iColorSpace), "unexpected space" ) ;
  }

  transfer->flipColorValueAtEnd = FALSE ;
  if ( transfer->isFirstLink != transfer->isFinalLink ) {
    /* In the first of two links, reverse the inversion that was applied in the
     * transfer. Any color processing will then have the color in the correct
     * sense.
     * In the second of two transfer links, reverse the inversion that was
     * applied in the first link.
     * This stage is applied when we're, a) recombining pre-separated,
     * jobs, b) color correcting composite ones, c) handling n-color output.
     */
    transfer->flipColorValueAtEnd = forcePositive ;
  }

  /* Switch back to a subtractive space after the transfers have been applied.
   * Don't bother though, if this is the final transfer link because our
   * definition of calibration is also in additive space.
   */
  if ( ! transfer->isFinalLink && transfer->flipColorValueAtStart )
    transfer->flipColorValueAtEnd = ! transfer->flipColorValueAtEnd ;

  if ( transfer->isFinalLink && applyJobPolarity ) {
    /* If ForcePositive is TRUE, and forcePositive is set, then we need
     * to flip the negative back to a positive.
     */
    if ( transfer->forcePositiveEnabled && rcbn_enabled() && forcePositive )
      transfer->flipColorValueAtEnd = ! transfer->flipColorValueAtEnd ;

    /* If the job wants to be inverted, flip the color over. Note
     * this (or Negative in setpagedevice) should never get set if
     * we're in composite mode, since it doesn't make sense. Leave
     * it in though in case anyones ever wants to use it. i.e. don't
     * turn it (or Negative) off with ForcePositive.
     */
    if ( negativeJob )
      transfer->flipColorValueAtEnd = ! transfer->flipColorValueAtEnd ;
  }
}

/* ---------------------------------------------------------------------- */
static Bool apply_ht_transfer_function( USERVALUE *color ,
                                        OBJECT    *htxfer ,
                                        int32     htfnindex ,
                                        int32     halftoneId )
{
  HQASSERT( color , "color NULL in apply_ht_transfer_function." ) ;
  HQASSERT( htxfer , "htxfer NULL in apply_ht_transfer_function." ) ;

  if (( oType( *htxfer ) == OFILE ) ||
      ( oType( *htxfer ) == ODICTIONARY )) {
    if ( ! fn_evaluate( htxfer , color , color ,
                        FN_HALFTONE_TFR , htfnindex ,
                        halftoneId , FN_GEN_NA ,
                        NULL ))
      return FALSE ;
    NARROW_01( *color ) ;
  }
  else {
    HQASSERT( theLen(*htxfer) > 0 , "Should have filtered out 0 length htxfers" ) ;
    if ( oExecutable( *htxfer )) {
      if ( ! apply_01_callback( *color , color , htxfer ))
        return FALSE ;
    }
    else
      if ( ! cc_applyCalibrationInterpolation( *color , color , htxfer ))
        return FALSE ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool apply_transfer_function( USERVALUE *color ,
                                     OBJECT    *xfer ,
                                     int32     setfnindex ,
                                     int32     transferId )
{
  HQASSERT( color , "color NULL in apply_transfer_function." ) ;
  HQASSERT( xfer , "xfer NULL in apply_transfer_function." ) ;
  HQASSERT( oType( *xfer ) == OARRAY ||
            oType( *xfer ) == OPACKEDARRAY ||
            oType( *xfer ) == OFILE  ||
            oType( *xfer ) == ODICTIONARY ,
            "type of xfer should be OARRAY/OPACKEDARRAY/OFILE/ODICTIONARY" ) ;

  if (( oType( *xfer ) == OFILE ) ||
      ( oType( *xfer ) == ODICTIONARY )) {
    if ( ! fn_evaluate( xfer , color , color ,
                        FN_TRANSFER , setfnindex ,
                        transferId , FN_GEN_NA ,
                        NULL ))
      return FALSE ;
    NARROW_01( *color ) ;
  }
  else {
    HQASSERT( theLen(*xfer) > 0 , "Should have filtered out 0 length xfers" ) ;
    if (!apply_01_callback( *color , color , xfer ))
      return FALSE;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool apply_01_callback( USERVALUE value,
                               USERVALUE *result,
                               OBJECT *proc )
{
  SYSTEMVALUE tmpValue;

  HQASSERT( oExecutable( *proc ) ,
            "callback not executable" ) ;
  HQASSERT( theLen(*proc) > 0 ,
            "wasting callback on empty array" ) ;
  HQASSERT( oType( *proc ) == OARRAY ||
            oType( *proc ) == OPACKEDARRAY ,
            "type of _proc should be OARRAY/OPACKEDARRAY" ) ;

  tmpValue = value;
  if ( !call_psproc(proc, tmpValue, &tmpValue, 1) )
    return FALSE;
  value = (USERVALUE) tmpValue;

  /* Bring number back into range again if necessary */
  NARROW_01( value ) ;

  *result = value ;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/* With some work this code could use GS_PROCs ala ucr & bg. */
static Bool setxferobject( corecontext_t      *corecontext,
                           GS_COLORinfo       *colorInfo,
                           STACK              *stack,
                           int32              nargs /* 1 or 4 */,
                           int32              *pnUniqueID )
{
  Bool fImmediate ;
  OBJECT *theo[ 4 ] ;
  int32 i , si ;
  int32 oldTransferId;
  COLOR_PAGE_PARAMS *colorPageParams = &corecontext->page->colorPageParams;

  HQASSERT( colorInfo , "colorInfo NULL in setxferobject" ) ;
  HQASSERT( stack , "stack NULL in setxferobject" ) ;
  HQASSERT( pnUniqueID , "pnUniqueID NULL in setxferobject" ) ;

  HQASSERT( nargs == 1 || nargs == 4 , "setxferobject called with funny nargs" ) ;

  if ( DEVICE_INVALID_CONTEXT())
    return error_handler( UNDEFINED ) ;

  if ( theIStackSize( stack ) < ( nargs - 1 ))
    return error_handler( STACKUNDERFLOW ) ;

  fImmediate = TRUE ;

  si = 4 - nargs ;      /* si is start index for stack indexing */
  for ( i = si ; i < 4 ; ++i ) {
    theo[ i ] = stackindex( 3 - i , stack ) ;
    switch ( oType( *theo[ i ] )) {
    case ODICTIONARY:
      break ;
    case OFILE:
      { FILELIST *flptr ;
        flptr = oFile( *theo[ i ] ) ;
        if ( ! isIInputFile( flptr ) ||
             ! isIOpenFileFilter( theo[ i ] , flptr ) ||
             ! isIRewindable( flptr ))
          return error_handler( IOERROR ) ;
        fImmediate = FALSE ;
      }
      /* Drop through. */
    case OARRAY:
    case OPACKEDARRAY:
      if ( ! oExecutable( *theo[ i ] ))
        return error_handler( TYPECHECK ) ;
      if ( ! oCanExec( *theo[ i ] ) && !object_access_override(theo[i]) )
        return error_handler( INVALIDACCESS ) ;
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
  }

  if ( colorPageParams->ignoreSetTransfer ) {
    npop( nargs , stack ) ;
    return TRUE ;
  }

  /* Copy a single transfer function to the other 3 channels; backwards. */
  if ( nargs == 1 )
    theo[ 0 ] = theo[ 1 ] = theo[ 2 ] = theo[ 3 ] ;

  for ( i = 0 ; i < nargs ; ++i ) {
    if ( ! push( theo[ i ] , & temporarystack )) {
      npop( i , & temporarystack ) ;
      return FALSE ;
    }
    theo[ i ] = theTop( temporarystack ) ;
  }
  /* Copy a single transfer function to the other 3 channels; forwards. */
  if ( nargs == 1 )
    theo[ 3 ] = theo[ 2 ] = theo[ 1 ] = theo[ 0 ] ;

  npop( nargs , stack ) ;

  /* This is a work round for a problem in Photoshop where they use a
   * variable 'negative' which goes out of scope. The variable is in the top
   * level array We used to substitute the (boolean) value of negative in
   * PostScript, but we can't now rely on knowing it's Photoshop, so we'll do
   * the same thing in C for all applications now. It's really nasty, but the
   * other solution of computing the transfer function in advance and
   * limiting the number of gray levels is even nastier.
   */
  for ( i = si ; i < 4 ; ++i ) {
    if ( oType( *theo[ i ] ) != OFILE && theLen(*theo[ i ]) > 0 ) {
      OBJECT *tmpo ;
      OBJECT *entry , *lastentry ;
      entry = oArray( *theo[ i ] ) ;
      lastentry = entry + theLen(*theo[ i ]) - 1 ;
      while ( entry <= lastentry ) {
        if ( oType( *entry ) == ONAME &&
             oName( *entry ) == system_names + NAME_negative ) {
          tmpo = fast_extract_hash( topDictStackObj , entry ) ;
          if ( tmpo && oType( *tmpo ) == OBOOLEAN ) {
            Copy( entry , tmpo ) ;
            break ;
          }
        }
        ++entry ;
      }
    }
  }

  oldTransferId = colorInfo->transferInfo->transferId;
  fImmediate = fImmediate && colorPageParams->immediateRepro ;
  if ( ! finalise_xfer( colorInfo ,
                        pnUniqueID ,
                        theo , nargs ,
                        fImmediate )) {
    npop( nargs , & temporarystack ) ;
    return FALSE ;
  }
  npop( nargs , & temporarystack ) ;

  /* The color chain cache is unaware of transfers, so generally we need to
   * invalidate it when the transfers change. We can optimise that for one case
   * where it's obvious they haven't actually changed.
   */
  if (oldTransferId != TRANSFER_ID_EMPTY ||
      colorInfo->transferInfo->transferId != TRANSFER_ID_EMPTY) {
    if ( ! cc_invalidateColorChains( colorInfo, TRUE ))
      return FALSE ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
Bool gsc_settransfers(corecontext_t *corecontext,
                      GS_COLORinfo  *colorInfo,
                      STACK         *stack,
                      int32         nargs)
{
  static int32 TransferFunctionId = 0 ;

  GS_TRANSFERinfo *transferInfo = colorInfo->transferInfo ;

  HQASSERT( stack , "stack NULL in gsc_settransfers" ) ;

  if ( transferInfo == NULL ) {
    if ( ! createtransferinfo( &transferInfo ))
      return FALSE ;
    colorInfo->transferInfo = transferInfo;
  }

  return setxferobject(corecontext, colorInfo, stack, nargs,
                       &TransferFunctionId ) ;
}

/* ---------------------------------------------------------------------- */
Bool gsc_currenttransfers( GS_COLORinfo *colorInfo, STACK *stack, int32 i , int32 j )
{
  while ( i <= j ) {
    if (!push(&colorInfo->transferInfo->transfers[i], stack))
      return FALSE;
    i++;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool finalise_xfer( GS_COLORinfo *colorInfo,
                           int32 *pnUniqueID ,
                           OBJECT *psObject[ 4 ] , int32 nargs ,
                           Bool fImmediate )
{
  int32 transferId ;
  GS_TRANSFERinfo *transferInfo;

  if (theLen(*psObject[0]) == 0 &&
      theLen(*psObject[1]) == 0 &&
      theLen(*psObject[2]) == 0 &&
      theLen(*psObject[3]) == 0) {
    transferId = TRANSFER_ID_EMPTY ;
  }
  else {
    transferId = ++(*pnUniqueID) ;
  }

  if ( fImmediate ) {
    if ( ! immediate( psObject , nargs )) {
      return FALSE ;
    }
  }

  /* Finally load the transferInfo */
  if ( ! updatetransferinfo( &colorInfo->transferInfo ))
    return FALSE ;
  transferInfo = colorInfo->transferInfo ;

  transferInfo->transferId = transferId ;
  Copy( & transferInfo->transfers[ 0 ] , psObject[ 0 ] ) ;
  Copy( & transferInfo->transfers[ 1 ] , psObject[ 1 ] ) ;
  Copy( & transferInfo->transfers[ 2 ] , psObject[ 2 ] ) ;
  Copy( & transferInfo->transfers[ 3 ] , psObject[ 3 ] ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool immediate( OBJECT *psObject[ 4 ] , int32 nargs )
{
  /* Calls the callback specified by theo 256 times, purely for the side effects */

  HQASSERT( psObject , "psObject NULL in immediate" ) ;
  HQASSERT( nargs == 1 || nargs == 4 , "immediate called with funny nargs" ) ;

  /* Note this does KYMC for the 4 color case. */
  while ((--nargs) >= 0 ) {
    int32 i ;
    SYSTEMVALUE tmparg ;
    OBJECT *theo = psObject[ nargs ] ;
    HQASSERT( theo , "theo unexpectedly NULL" ) ;
    HQASSERT( oType( *theo ) != OFILE ,
              "As otherwise should not be calling this routine" ) ;
    for ( i = 0 ; i < 256 ; ++i ) {
      tmparg = ( SYSTEMVALUE )i / 255.0f ;
      if ( !call_psproc(theo, tmparg, &tmparg, 1) )
        return FALSE;
    }
  }
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
OBJECT *gsc_gettransferobjects( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo, "gsc_copytransferobjects: colorInfo is NULL" ) ;
  HQASSERT( colorInfo->transferInfo, "gsc_copytransferobjects: transferInfo is NULL" ) ;
  return &colorInfo->transferInfo->transfers[ 0 ] ;
}

/* ---------------------------------------------------------------------- */
int32 gsc_gettransferid( GS_COLORinfo *colorInfo )
{
  HQASSERT( colorInfo, "gsc_copytransferid: colorInfo is NULL" ) ;
  HQASSERT( colorInfo->transferInfo, "gsc_copytransferid: transferInfo is NULL" ) ;
  return colorInfo->transferInfo->transferId ;
}

/* ---------------------------------------------------------------------- */
Bool gsc_transfersPreapplied(GS_COLORinfo *colorInfo)
{
  return colorInfo->transferInfo->transfersPreapplied;
}

/* ---------------------------------------------------------------------- */
Bool gsc_setTransfersPreapplied(GS_COLORinfo *colorInfo, Bool preapplied)
{
  if ( colorInfo->transferInfo->transfersPreapplied != preapplied ) {
    if ( !updatetransferinfo(&colorInfo->transferInfo) )
      return FALSE;
    colorInfo->transferInfo->transfersPreapplied = preapplied;
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
/*
 * analyze_for_forcepositive pushes white and black through the transfer
 * functions and sets forcePositive if the result is an inverted transfer
 * for all transfers.
 */
Bool gsc_analyze_for_forcepositive( corecontext_t *context,
                                    GS_COLORinfo *colorInfo, int32 colorType,
                                    Bool *forcePositive )
{
  int32         i ;
  Bool          result ;
  CLINK         *pLink ;
  COLORANTINDEX cmykColorants[ 4 ] ;
  USERVALUE     oColorValuesWhite[ 4 ] ;
  USERVALUE     oColorValuesBlack[ 4 ] ;

  int32         nDeviceColorants ;
  DEVICESPACEID deviceColorSpace ;

  transferInfoAssertions( colorInfo->transferInfo ) ;

  /* Create a temporary transfer link to push colors through the transfer curves.
   * Fudge the link by making it appear the first of two links so that any
   * inversion that occurs due to DeviceCMYK is reversed.
   * Note that we are setting a job colorspace of cmyk so the gray-to-cmyk
   * condition will not apply.
   */
  guc_deviceColorSpace( colorInfo->deviceRS ,
                        & deviceColorSpace ,
                        & nDeviceColorants ) ;
  if (!guc_simpleDeviceColorSpaceMapping( colorInfo->deviceRS ,
                                          DEVICESPACE_CMYK , cmykColorants , 4 ))
    return FALSE;

  *forcePositive = FALSE ;

  pLink = cc_transfer_create( 4 ,
                              cmykColorants ,
                              SPACE_DeviceCMYK ,
                              gsc_getRequiredReproType(colorInfo, colorType),
                              FALSE,     /* jobColorSpaceIsGray */
                              TRUE ,     /* isFirstTransferLink */
                              FALSE ,    /* isLastTransferLink */
                              guc_backdropRasterStyle(colorInfo->deviceRS),
                              colorInfo->transferInfo ,
                              colorInfo->halftoneInfo ,
                              colorInfo->deviceRS,
                              context->color_systemparams->ForcePositive,
                              &context->page->colorPageParams) ;
  if (pLink == NULL)
    return FALSE ;

  /* Push both black and white through. */
  disable_separation_detection() ;
  result = (( pLink->iColorValues[ 0 ] = 0.0f ,
              pLink->iColorValues[ 1 ] = 0.0f ,
              pLink->iColorValues[ 2 ] = 0.0f ,
              pLink->iColorValues[ 3 ] = 0.0f ,
              transfer_invokeSingle( pLink , oColorValuesWhite )) &&
            ( pLink->iColorValues[ 0 ] = 1.0f ,
              pLink->iColorValues[ 1 ] = 1.0f ,
              pLink->iColorValues[ 2 ] = 1.0f ,
              pLink->iColorValues[ 3 ] = 1.0f ,
              transfer_invokeSingle( pLink , oColorValuesBlack ))) ;
  enable_separation_detection() ;

  transfer_destroy( pLink ) ;

  if ( ! result )
    return FALSE ;

  /* Set forcePositive to TRUE if all 4 transfer curves are inverted such that
   * 0 => 1 and 1 => 0. But if the device space is gray, only bother testing
   * the gray transfer.
   */
  *forcePositive = TRUE ;
  for ( i = 0 ; i < 4 ; ++i ) {
    if (( deviceColorSpace != DEVICESPACE_Gray ) ||
        ( i == 3 )) {
      if ( oColorValuesWhite[ i ] < 1.0f ||
           oColorValuesBlack[ i ] > 0.0f ) {
        *forcePositive = FALSE ;
        break ;
      }
    }
  }
  return TRUE ;
}

/* eof */

/* Log stripped */
