/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:images.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Parsing of image arguments, and handling of image data in characters.
 */

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "swctype.h"
#include "hqmemcpy.h"
#include "mm.h"
#include "mmcompat.h"
#include "lowmem.h"
#include "objects.h"
#include "gcscan.h"
#include "dicthash.h"
#include "dictscan.h"
#include "fileio.h"
#include "fonts.h"   /* charcontext_t */
#include "progress.h"
#include "swpdfout.h"
#include "namedef_.h"

#include "often.h"
#include "matrix.h"
#include "psvm.h"
#include "graphics.h"
#include "control.h"
#include "fileops.h"
#include "stacks.h"
#include "miscops.h"
#include "utils.h"
#include "gs_color.h"
#include "gschead.h"
#include "gstate.h"
#include "routedev.h"
#include "devops.h"
#include "idlom.h"
#include "plotops.h"
#include "dl_bres.h"
#include "gu_path.h"
#include "dl_image.h"
#include "imaskgen.h"
#include "imskgen4.h"
#include "images.h"
#include "gscxfer.h"
#include "render.h"       /* render_blit_t */
#include "display.h"      /* IMAGE_OPTIMISE_* */
#include "params.h"
#include "vndetect.h"
#include "bitblts.h" /* DO_BLOCK */
#include "constant.h" /* EPSILON */

#include "fltrdimg.h"
#include "chardevim.h"

#include "timing.h"

/* ---------------------------------------------------------------------- */

/* Tri-state for argsdone in check_image_misc_hooks, so that we don't do the same calculations
   every time we call execute_image_misc_hook
*/
enum {
  eNoArgs = 0,
  eBboxArgs,
  eBboxAndClipArgs
};

static Bool check_image_misc_hooks(IMAGEARGS *imageargs) ;
static Bool execute_image_misc_hook(IMAGEARGS *imageargs, OBJECT *hook_proc,
                                    int32 showdevice, int32 *argsknown,
                                    SYSTEMVALUE args[], SYSTEMVALUE vals[]) ;
static Bool image_misc_hooks_corners(IMAGEARGS *imageargs, SYSTEMVALUE args[]) ;

/* ---------------------------------------------------------------------- */
/**
 * Get the string from the top of the stack, and do simple checks on it.
 *
 * bytes_remaining is the maximum number of bytes which the caller requires
 * immediately following this call. In the case of reading from an OFILE, this
 * should be used to determine the number of bytes actually consumed. However
 * get_image_string() can and should return more if the source is an OSTRING
 * so that a caller which wants to process data in smaller chunks than the
 * length of the string can record and use the remaining data in the string
 * correctly.
 */
Bool get_image_string( OBJECT *theo , int32 bytes_remaining ,
                       uint8 **pclist , int32 *pclen )
{
  HQASSERT( theo , "get_image_string(): theo is NULL" ) ;
  HQASSERT( bytes_remaining >= 0 , "get_image_string(): bytes_remaining should be >= 0" );
  HQASSERT( pclist , "get_image_string(): pclist is NULL" ) ;
  HQASSERT( pclen , "get_image_string(): pclen is NULL" ) ;

  SwOftenUnsafe() ;

  switch ( oType( *theo )) {
  case OSTRING :
    *pclist = oString( *theo ) ;
    *pclen  = theLen(*theo) ;
    return TRUE ;
  case OFILE :
    if ( theISaveLangLevel( workingsave ) >= 2 ) {
      FILELIST *flptr = oFile( *theo ) ;
      if ( ! isIOpenFileFilter( theo, flptr )) {
        /* We can't proceed to do anything else with a dead filter,
         * and we might as well stop here if the file is not open.
         */
        *pclen = 0 ;
        return TRUE ;
      }

      /* Level 2 reads in enough data from the file.
       * Use the filestream's buffer directly.
       */
      if ( ! GetFileBuff( flptr, bytes_remaining , pclist , pclen )) {
        if ( isIIOError( flptr )) {
          ( void )(*theIFileLastError( flptr ))( flptr ) ;
          return FALSE ;
        }
        else {
          /* premature end of image */
          *pclen = 0 ;
          return TRUE ;
        }
      }
      HQASSERT( *pclen <= bytes_remaining ,
                "get_image_string(): OFILE object read too much data" ) ;
      return setReadFileProgress( flptr ) ;
    }
    /* FALL THROUGH */
  default : /* or OFILE with PS level 1 */
    if ( oExecutable( *theo )) {
      Bool ret;
      if ( oType(*theo) == OOPERATOR ) {
        /* Short-circuit for operator calls. This may change some error
           propagation behaviour. */
        OPFUNCTION function = theIOpCall(oOp(*theo)) ;
        ret = (*function)(get_core_context_interp()->pscontext) ;
      } else {
        Bool gs_lock_state;

        execStackSizeNotChanged = FALSE ;
        currfileCache = NULL ;
        if ( ! push( theo , & executionstack ))
          return FALSE ;
        gs_lock_state = gs_lock(TRUE);
        ret = interpreter( 1, NULL );
        (void)gs_lock(gs_lock_state);
      }

      if ( !ret )
        return FALSE;
      if ( isEmpty( operandstack ))
        return error_handler( STACKUNDERFLOW ) ;
      theo = theTop( operandstack ) ;

      if (oType( *theo ) == OFILE) {
        /* recurse to get the string */
        ret = get_image_string(theo, bytes_remaining, pclist, pclen) ;
        /* pop the file */
        pop( &operandstack ) ;
        return ret;
      }
    }
    else {
      /* Push the non-executable object onto the operand stack */
      if ( ! push( theo , & operandstack ))
        return FALSE ;
    }

    if ( oType( *theo ) != OSTRING )
      return error_handler( TYPECHECK ) ;

    *pclist = oString( *theo ) ;
    *pclen = theLen(*theo) ;

    pop( &operandstack ) ;
    return TRUE ;
  }
  /* NOT REACHED */
}


/* ---------------------------------------------------------------------- */
/**
 * Get the raw color mask information. Rather than the maskgen structure
 * externally, add an API to get the detail. Also, I did not want to
 * extend the size of the image args structure.
 */
void get_image_genmask_data( MASKGENDATA *maskgen ,
                             int32 *nvalues ,
                             int32 **colorinfo )
{
  im_genmask_data( maskgen , nvalues , colorinfo ) ;
}

/* ---------------------------------------------------------------------- */
/**
 * Since Adobe lie about the type checking that happens on the procedure
 * argument to the image, imagemask and colorimage operators, this procedure
 * checks that the argument can indirectly leave a string on the stack.
 */
static Bool check_image_proc( OBJECT * theo )
{
  switch ( oType( *theo )) {
  case ONAME :
  case OOPERATOR :
    if ( ! oExecutable( *theo ))
      return error_handler( TYPECHECK ) ;
    break ;
  case OFILE :
  case OARRAY :
  case OPACKEDARRAY :
  case OSTRING :
    if ( ! oCanExec( *theo ))
      if ( ! object_access_override(theo) )
        return error_handler( INVALIDACCESS ) ;

    if ( oType( *theo ) == OFILE ) {
      FILELIST *flptr = oFile( *theo ) ;

      /* It is valid to test isIInputFile even if it is a dead filter - not
       * that a dead filter is much use to us, but we check that later.
       */
      if ( ! isIInputFile( flptr ))
        return error_handler( IOERROR ) ;

      /* If the data source is a file just used by the scanner (e.g.
       * currentfile) then we could get our knickers in a twist over
       * the line-end characters. The token kicking off the image,
       * e.g. 'image' or 'exec', may have been terminated by a CR, but
       * there might have been an LF following the CR which we should
       * count as part of the token terminator and not part of the
       * image data. I guess the Adobe scanner just skips LFs after
       * CRs rather than this sort of the special-casing, but what the
       * heck. I've been asked to make this smaller-scope change.
       * This code assumes that only the scanner and other line-based
       * stuff like readline_ set the CRFlags.
       */

      if ( isICRFlags( flptr )) {
        int32 c = Getc( flptr ) ;

        ClearICRFlags( flptr ) ;

        if ( c != LF )
          UnGetc( c , flptr ) ;
      }
    }
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** GC scanner for IMAGEARGS structures. */
static mps_res_t imageargs_scan(mps_ss_t ss, void *data, size_t s)
{
  mps_res_t res ;
  IMAGEARGS *imageargs = data ;

  UNUSED_PARAM(size_t, s) ;

  if ( imageargs->data_src != NULL ) {
    res = ps_scan(ss, (mps_addr_t)imageargs->data_src,
                  (mps_addr_t)(imageargs->data_src + imageargs->nprocs)) ;
    if ( res != MPS_RES_OK ) return res ;
  }

  if ( imageargs->data_src_flush != NULL ) {
    res = ps_scan(ss, (mps_addr_t)imageargs->data_src_flush,
                  (mps_addr_t)(imageargs->data_src_flush + imageargs->n_src_flush)) ;
    if ( res != MPS_RES_OK ) return res ;
  }

  if ( imageargs->data_src_close != NULL ) {
    res = ps_scan(ss, (mps_addr_t)imageargs->data_src_close,
                  (mps_addr_t)(imageargs->data_src_close + imageargs->n_src_close)) ;
    if ( res != MPS_RES_OK ) return res ;
  }

  if ( imageargs->dictionary != NULL ) {
    res = ps_scan_field(ss, imageargs->dictionary) ;
    if ( res != MPS_RES_OK ) return res ;
  }

  res = ps_scan_field(ss, &imageargs->image_color_object) ;
  if ( res != MPS_RES_OK ) return res ;

  res = ps_scan_field(ss, &imageargs->colorspace) ;
  if ( res != MPS_RES_OK ) return res ;

  return MPS_RES_OK ;
}

/* ---------------------------------------------------------------------- */
Bool alloc_image_datasrc( IMAGEARGS *imageargs , int32 nprocs )
{
  mps_res_t res ;
  int32 i ;
  Bool retry;
  memory_requirement_t request =
    { NULL, 32, { memory_tier_reserve_pool, 1e3f }};
  corecontext_t *context = get_core_context_interp();

  HQASSERT( imageargs , "imageargs NULL in alloc_image_datasrc" ) ;
  HQASSERT( nprocs > 0 , "nprocs should be +ve" ) ;

  /* Make this a GC root for the data source. */
  HQASSERT(imageargs->gc_root == NULL, "Image args already has GC root") ;
  do { /** \todo Package root creation up as a service. */
    res = mps_root_create(&imageargs->gc_root, mm_arena, mps_rank_exact(),
                          0, imageargs_scan, imageargs, 0);
    if ( res == MPS_RES_OK )
      break;
    if ( !low_mem_handle(&retry, context, 1, &request) )
      return FALSE;
  } while ( retry );
  if ( res != MPS_RES_OK )
    return error_handler(VMERROR) ;

  HQASSERT(imageargs->data_src == NULL ,
            "Should not have already allocated data src" ) ;
  imageargs->data_src =
    mm_alloc_with_header(mm_pool_temp, nprocs * sizeof(OBJECT),
                         MM_ALLOC_CLASS_IMAGE) ;
 /* An early return on error is OK, imageargs_scan can handle NULL
    data_src, data_src_flush, and data_src_close lists. */
  if ( imageargs->data_src == NULL )
    return error_handler( VMERROR ) ;

  /* Set imageargs nprocs here, so GC scan will scan right number of
     objects. */
  imageargs->nprocs = nprocs ;

  /* Initialise imageargs, for safety in GC scanning */
  for ( i = 0 ; i < nprocs ; ++i )
    imageargs->data_src[i] = onull ; /* struct copy to set slot properties */

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/** Alloc's an array of floats (two floats for each colorant) to represent
 * the /Decodes entry in the image/mask dictionary.
*/
Bool alloc_image_decode( IMAGEARGS *imageargs , int32 ncomps )
{
  HQASSERT( imageargs , "imageargs NULL in alloc_image_decode" ) ;
  HQASSERT( ncomps > 0 , "ncomps should be +ve" ) ;
  HQASSERT( imageargs->decode == NULL ,
            "Should not have already allocated decode" ) ;

  imageargs->decode =
    mm_alloc_with_header( mm_pool_temp ,
                          2 * ncomps * sizeof( float ) ,
                          MM_ALLOC_CLASS_IMAGE ) ;
  if ( imageargs->decode == NULL )
    return error_handler( VMERROR ) ;
  else
    return TRUE ;
}

/* ---------------------------------------------------------------------- */
void finish_image_args(IMAGEARGS *imageargs)
{
  OBJECT *datasrc ;

  datasrc = imageargs->data_src_flush ;
  if ( datasrc ) {
    int32 i ;
    /* Don't reset the flushed file to ONULL if the storage is shared with
       the close list, or the data source list. */
    Bool reset = (datasrc != imageargs->data_src_close &&
                  datasrc != imageargs->data_src) ;

    /* These data sources need flushing to EOF, they were introduced by the
       image subsystem itself. */
    for ( i = 0 ; i < imageargs->n_src_flush ; ++i ) {
      OBJECT *fileo = &datasrc[i] ;
      if ( oType(*fileo) == OFILE ) {
      FILELIST *flptr = oFile(*fileo) ;

      HQASSERT(isIInputFile(flptr), "Image DataSource is not an input file") ;

      /* Same as the guts of flushfile_(), followed by closefile_(). */
      if ( isIOpenFileFilter(fileo, flptr) ) {
        if ( theIMyFlushFile(flptr)(flptr) == EOF ) {
          HQFAIL("Couldn't flush image DataSource") ;
        }
      }
        if ( reset )
          datasrc[i] = onull ;
    }
    }
  }

  datasrc = imageargs->data_src_close ;
  if ( datasrc ) {
    int32 i ;
    /* Don't reset the closed file to ONULL if the storage is shared with
       the close list, or the data source list. */
    Bool reset = (datasrc != imageargs->data_src_flush &&
                  datasrc != imageargs->data_src) ;

    /* These data sources need closing, they were introduced by the image
       subsystem itself. */
    for ( i = 0 ; i < imageargs->n_src_close ; ++i ) {
      OBJECT *fileo = &datasrc[i] ;
      if ( oType(*fileo) == OFILE ) {
      FILELIST *flptr = oFile(*fileo) ;

      HQASSERT(isIInputFile(flptr), "Image DataSource is not an input file") ;

      if ( isIOpenFileFilter(fileo, flptr) ) {
        if ( theIMyCloseFile(flptr)(flptr, CLOSE_EXPLICIT) == EOF ) {
          HQFAIL("Couldn't close image DataSource") ;
        }
      }
        if ( reset )
          datasrc[i] = onull ;
    }
  }
  }

  /* Delete the filtered image data. It's ok to try delete a NULL pointer. */
  filteredImageDelete( imageargs->filteredImage);
  imageargs->filteredImage = NULL;

  if ( imageargs->maskargs )
    finish_image_args(imageargs->maskargs) ;
}

void free_image_args( IMAGEARGS *imageargs )
{
  IMAGEARGS *maskargs ;

  finish_image_args(imageargs) ;

  if ( imageargs->data_src_flush ) {
    mm_free_with_header(mm_pool_temp, imageargs->data_src_flush) ;
    imageargs->data_src_flush = NULL ;
    imageargs->n_src_flush = 0 ;
  }

  if ( imageargs->data_src_close ) {
    mm_free_with_header(mm_pool_temp, imageargs->data_src_close) ;
    imageargs->data_src_flush = NULL ;
    imageargs->n_src_flush = 0 ;
  }

  if ( imageargs->decode ) {
    mm_free_with_header(mm_pool_temp, imageargs->decode) ;
    imageargs->decode = NULL;
  }

  if ( imageargs->data_src ) {
    mm_free_with_header(mm_pool_temp, imageargs->data_src) ;
    imageargs->data_src = NULL;
  }

  if ( imageargs->maskgen ) {
    im_genmaskfree( imageargs->maskgen ) ;
    imageargs->maskgen = NULL;
  }

  if ( imageargs->gc_root != NULL ) {
    mps_root_destroy(imageargs->gc_root) ;
    imageargs->gc_root = NULL ;
  }

  maskargs = imageargs->maskargs ;
  if ( maskargs ) {
    HQASSERT(maskargs->maskgen == NULL,
             "Mask generation found in masked image mask args" ) ;
    HQASSERT(maskargs->filteredImage == NULL,
             "Filtering found in masked image mask args" ) ;

    free_image_args(maskargs) ;
  }
}

/* ---------------------------------------------------------------------- */
static Bool get_image_dictargs( OBJECT *imagedict ,
                                int32 imageType ,
                                int32 ncomps ,
                                Bool needDataSource ,
                                IMAGEARGS *imageargs ,
                                Bool isImage )
{
  enum {
    e_ImageType = 0,
    e_Width,
    e_Height,
    e_ImageMatrix,
    e_MultipleDataSources,
    e_DataSource,
    e_BitsPerComponent,
    e_Decode,
    e_Interpolate,
    e_IsSoftMask,
    e_Matted,
    e_ContoneMask,
    e_PreMult,
    e_FitEdges,
    e_ImagePixelCenters,
    e_CleanMatrix,
    e_Max
  };

  static NAMETYPEMATCH imagedictmatch[e_Max + 1] = {
    { NAME_ImageType   , 1 , { OINTEGER }} ,
    { NAME_Width       , 1 , { OINTEGER }} ,
    { NAME_Height      , 1 , { OINTEGER }} ,
    { NAME_ImageMatrix , 2 , { OARRAY , OPACKEDARRAY }} ,

    { NAME_MultipleDataSources | OOPTIONAL ,
                                     1 , { OBOOLEAN }} ,
    { NAME_DataSource  ,             0 } ,
    { NAME_BitsPerComponent ,        1 , { OINTEGER }} ,
    { NAME_Decode ,                  2 , { OARRAY , OPACKEDARRAY }} ,
    { NAME_Interpolate | OOPTIONAL , 1 , { OBOOLEAN }} ,
    { NAME_IsSoftMask | OOPTIONAL , 1 , { OBOOLEAN }} ,
    { NAME_Matted | OOPTIONAL , 1 , { OBOOLEAN }} ,
    { NAME_ContoneMask | OOPTIONAL , 1 , { OBOOLEAN }} ,
    { NAME_PreMult | OOPTIONAL , 1 , { OBOOLEAN }} ,
    { NAME_FitEdges | OOPTIONAL , 1 , { OBOOLEAN }} ,
    { NAME_ImagePixelCenters | OOPTIONAL , 1 , { OBOOLEAN }} ,
    { NAME_CleanMatrix | OOPTIONAL , 1 , { OBOOLEAN }} ,
    DUMMY_END_MATCH
  } ;

  USERPARAMS *userparams = get_core_context_interp()->userparams;
  OBJECT *theo ;
  int32 multiproc ;
  int32 i ;
  int32 nprocs ;
  int32 ndecodes ;

  imageargs->dictionary = imagedict;

  /* If there should be no data source, make it optional and check later
   * that it has been omitted
   */
  if ( needDataSource )
    imagedictmatch[ e_DataSource ].name &= ~OOPTIONAL ;
  else
    imagedictmatch[ e_DataSource ].name |=  OOPTIONAL ;

  if ( ! dictmatch( imagedict , imagedictmatch ))
    return FALSE ;

  imageargs->ncomps = ncomps;

  /* ImageType */
  theo = imagedictmatch[ e_ImageType ].result ;
  if ( oInteger( *theo ) != imageType )
    return error_handler( TYPECHECK ) ;

  /* Width */
  theo = imagedictmatch[ e_Width ].result ;
  imageargs->width = oInteger( *theo );

  /* Height */
  theo = imagedictmatch[ e_Height ].result ;
  imageargs->height = oInteger( *theo );

  /* By default allow the whole image to be read contiguously. */
  imageargs->lines_per_block = imageargs->height;

  /* ImageMatrix */
  theo = imagedictmatch[ e_ImageMatrix ].result ;
  if ( ! oCanRead( *theo ))  /* must be readable */
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
  if ( ! is_matrix( theo , & imageargs->omatrix))
    return FALSE ;

  /* MultipleDataSources */
  multiproc = FALSE ;
  if (( theo = imagedictmatch[ e_MultipleDataSources ].result ) != NULL )
    multiproc = oBool( *theo ) ;

  /* DataSource */
 retryDataSource:
  if (( theo = imagedictmatch[ e_DataSource ].result ) == NULL ) {
    /* We can only get here if a data source should be absent
     * e.g. for /ImageType 3, /InterleaveType != 3
     */
    HQASSERT( ! needDataSource, "get_image_dictargs: DataSource required" ) ;
    if ( multiproc )
      return error_handler( TYPECHECK ) ;
    nprocs = 0 ;
  }
  else {
    /* The L3 spec - Page 74 Table 4.3 says this must not be present.
       Relax this test slightly for the benefit of HDLT, to say that if it
       was not desired, null is acceptable. */
    if ( ! needDataSource ) {
      if ( oType(*theo) == ONULL ) {
        /* Let's pretend it was really not present, and try this again. */
        imagedictmatch[e_DataSource].result = NULL ;
        goto retryDataSource ;
      }
      return error_handler( TYPECHECK ) ;
    }

    nprocs = 1 ;
    if ( multiproc ) {
      /* We used to forbid a one-element array for imagemask (since that is
       * what L2 RIPs do) but the LW8500 allows it. This means our L2
       * compatibility mode is slightly less strict than before
       */
      /* DataSource must be an array */
      nprocs = ncomps ;
      if (( oType( *theo ) != OARRAY ) &&
          ( oType( *theo ) != OPACKEDARRAY ))
        return error_handler( TYPECHECK ) ;
      if ( ! oCanRead( *theo ))
        if ( ! object_access_override(theo) )
          return error_handler( INVALIDACCESS ) ;
      if ( theLen(*theo) != nprocs )
        return error_handler( RANGECHECK ) ;
      theo = oArray( *theo ) ;
    }

    /* Allocate N-color arrays for image data src. */
    if ( ! alloc_image_datasrc( imageargs , nprocs ))
      return FALSE ;

    for ( i = 0 ; i < nprocs ; ++i ) {
      /* DataSource must be a file, procedure or string */
      if ( ! check_image_proc( theo ))
        return FALSE ;
      Copy(&imageargs->data_src[i], theo) ;
      ++theo ;
    }
  }
  imageargs->nprocs = nprocs;

  /* BitsPerComponent */
  theo = imagedictmatch[ e_BitsPerComponent ].result ;
  imageargs->bits_per_comp = oInteger( *theo );

  ndecodes = ncomps;

  imageargs->premult_alpha = FALSE;
  if (imagedictmatch[e_PreMult].result != NULL) {
    imageargs->premult_alpha = oBool(*imagedictmatch[e_PreMult].result);
    /* if we have an associated alpha then count that channel */
    if (imageargs->premult_alpha) {
      /* A non-premultiplied alpha channel will be generated, allocate
         an extra decode for it, so later parts of the image flow are
         simplified. */
      ndecodes++;
    }
  }

  /* Decode */
  theo = imagedictmatch[ e_Decode ].result ;
  if (( oType( *theo ) != OARRAY ) &&
      ( oType( *theo ) != OPACKEDARRAY ))
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead( *theo ))
    if ( ! object_access_override(theo) )
      return error_handler( INVALIDACCESS ) ;
  if ( theLen(*theo) != ( 2 * ndecodes ))
    return error_handler( RANGECHECK ) ;

  /* Allocate N-color arrays for image decode. */
  if ( ! alloc_image_decode( imageargs , ndecodes ))
    return FALSE ;

  /* Initialize decode array */
  theo = oArray( *theo ) ;
  for ( i = 0 ; i < 2 * ndecodes ; ++i, ++theo ) {
    if ( !object_get_real(theo, &imageargs->decode[i]) )
      return FALSE ;
  }

  /* Interpolate */
  {
    QuadState* defaultValue = &userparams->InterpolateAllImages;
    if (! isImage)
      defaultValue = &userparams->InterpolateAllMasks;

    imageargs->interpolate =
      (int8)quadStateApplyToObject(defaultValue,
                                   imagedictmatch[ e_Interpolate ].result);
  }

  /* IsSoftMask */
  if (imagedictmatch[e_IsSoftMask].result != NULL)
    imageargs->isSoftMask = oBool(*imagedictmatch[e_IsSoftMask].result);
  else
    imageargs->isSoftMask = FALSE;

  /* Matted */
  if (imagedictmatch[e_Matted].result != NULL)
    imageargs->matted = oBool(*imagedictmatch[e_Matted].result);
  else
    imageargs->matted = FALSE;

  /* ContoneMask - only pertinent for masks. This value is always true if we're
     doing an alpha channel, regardless of the ContoneMask key/value. */
  imageargs->contoneMask = FALSE;
  if (imageargs->imagetype == TypeImageAlphaAlpha) {
    imageargs->contoneMask = TRUE;
  } else {
    if (imagedictmatch[e_ContoneMask].result != NULL)
      imageargs->contoneMask = oBool(*imagedictmatch[e_ContoneMask].result);
  }

  /* FitEdges */
  if (imagedictmatch[e_FitEdges].result != NULL)
    imageargs->fit_edges = (int8)oBool(*imagedictmatch[e_FitEdges].result);

  /* ImagePixelCenters */
  if (imagedictmatch[e_ImagePixelCenters].result != NULL)
    imageargs->image_pixel_centers =
      (int8)oBool(*imagedictmatch[e_ImagePixelCenters].result);

  /* CleanMatrix */
  if (imagedictmatch[e_CleanMatrix].result != NULL)
    imageargs->clean_matrix = oBool(*imagedictmatch[e_CleanMatrix].result);

  return TRUE ;
}

/** get_mask_polarity()
 * Given that we're dealing with a mask, the /Decode array values should
 * be essentially boolean.  This routine forces that to be the case.
 */
static void get_mask_polarity( IMAGEARGS *imageargs )
{
  /* This is apparently what Adobe do!! */
  if ( imageargs->decode[ 0 ] <
       imageargs->decode[ 1 ] ) {
    imageargs->decode[ 0 ] = 0.0f;
    imageargs->decode[ 1 ] = 1.0f;
    imageargs->polarity = FALSE;
  }
  else {
    imageargs->decode[ 0 ] = 1.0f;
    imageargs->decode[ 1 ] = 0.0f;
    imageargs->polarity = TRUE;
  }
}

static Bool check_dict_access( OBJECT *theo )
{
  HQASSERT( theo , "theo NULL in check_dict_access" ) ;
  HQASSERT( oType( *theo ) == ODICTIONARY,
            "check_dict_access: not a dict!" ) ;

  if ( ! oCanRead( *( oDict( *theo ))))
    if ( ! object_access_override(oDict(*theo)) )
      return error_handler( INVALIDACCESS ) ;

  return TRUE ;
}

/** Relative-error check for cross-multiple of image and mask width/height
    with matrix components. */
static Bool mask_matrix_check(SYSTEMVALUE wh1, SYSTEMVALUE wh2)
{
  SYSTEMVALUE amax = max(fabs(wh1), fabs(wh2)) ;

  if ( amax > 0 && fabs(wh1 - wh2) / amax > EPSILON )
    return FALSE ;

  return TRUE ;
}

static Bool parseType3ImageDict(IMAGEARGS *imageargs, int32 ncomps, OBJECT * o1)
{
  Bool needData ;
  IMAGEARGS *maskargs ;
  OBJECT * theo ;
  int i ;

  enum {
    image3_InterleaveType, image3_MaskDict, image3_DataDict, image3_max
  } ;
  static NAMETYPEMATCH imagedict3match[image3_max + 1] = {
    { NAME_InterleaveType , 1, { OINTEGER }},                 /* 0 */
    { NAME_MaskDict , 1, { ODICTIONARY }},                    /* 1 */
    { NAME_DataDict , 1, { ODICTIONARY }},                    /* 2 */
    DUMMY_END_MATCH
  } ;


  if ( ! dictmatch( o1 , imagedict3match ))
    return FALSE ;

  needData = FALSE ;  /* assume mask data comes from image's datasource */

  /* InterleaveType: i.e. whether mask data is interleaved with
     the image data through the same source, or not.
  */
  theo = imagedict3match[image3_InterleaveType].result ;
  switch ( oInteger( *theo )) {
  case 1:   /* for each pixel/sample; mask bits first, then colorant bits */
    imageargs->interleave = INTERLEAVE_PER_SAMPLE;
    break ;

  case 2:   /* for each row/scanline; mask row first, then colorants */
    imageargs->interleave = INTERLEAVE_SCANLINE;
    break ;

  case 3:   /* image and mask provide their own datasources */
    imageargs->interleave = INTERLEAVE_SEPARATE;
    needData = TRUE ; /* mask provides its own datasource */
    break ;

  default:
    return error_handler( RANGECHECK ) ;
  }

  /* MaskDict */
  o1 = imagedict3match[image3_MaskDict].result ;
  if ( ! check_dict_access( o1 ) )
    return FALSE ;

  maskargs = imageargs->maskargs;

  HQASSERT( maskargs != NULL, "no maskargs when called from image" );

  imageargs->imagetype = TypeImageMaskedImage;
  maskargs->imagetype = TypeImageMaskedMask;

  if ( ! get_image_dictargs(o1, 1, 1, needData, maskargs, FALSE) ) {
    return FALSE ;
  }

  /* image's own DataDict */
  o1 = imagedict3match[image3_DataDict].result ;
  if ( ! check_dict_access( o1 ) )
    return FALSE ;

  if ( ! get_image_dictargs(o1, 1, ncomps, TRUE, imageargs, TRUE) )
    return FALSE ;

  /* Allow Decode array in the mask to retain its given values
     if doing a contone mask; otherwise force their polarity/boolean
     nature. */
  if (!maskargs->contoneMask)
    get_mask_polarity( maskargs ) ;
  else if (maskargs->isSoftMask)
    maskargs->polarity = TRUE;

  switch ( imageargs->interleave ) {
  case INTERLEAVE_PER_SAMPLE :
    if ( maskargs->width != imageargs->width ||
         maskargs->height != imageargs->height ||
         maskargs->bits_per_comp != imageargs->bits_per_comp ||
         maskargs->nprocs != 0 ||
         imageargs->nprocs != 1 )
      return error_handler( RANGECHECK ) ;

    /* Modify mask data to reflect stored depth rather than input
       depth. This is because when mask data is pixel interleaved
       with the image data, it comes with the same bit depth as
       for the colorants. However, if doing a contone mask, the
       mask's bit depth must be _retained_. */
    if (!maskargs->contoneMask)
      maskargs->bits_per_comp = 1;

    maskargs->nprocs = 1;

    /* Ensure input is processed one line at a time to allow
       mask generation to be performed in the input buffer */
    maskargs->lines_per_block  = 1;
    imageargs->lines_per_block = 1;
    break ;

  case INTERLEAVE_SCANLINE :

    /* Unless we have a contone mask, the bitdepth must be 1. */
    if (! maskargs->contoneMask) {
      if ( maskargs->bits_per_comp != 1)
        return error_handler( RANGECHECK ) ;
    }

    if (maskargs->nprocs != 0 ||
        imageargs->nprocs != 1)
      return error_handler( RANGECHECK ) ;

    {
      /* When row interleaved, image and mask may be specified with
         different heights, but only so long as one is a whole multiple
         of the other (see PLRM.3). This further means that there will
         be more rows per block (= combination of mask-then-image data)
         for the higher than the other. */
      int32 hm = maskargs->height;
      int32 hi = imageargs->height;

      if (hm > hi) {
        if ( hi < 1 || hm % hi != 0 )
          return error_handler( RANGECHECK ) ;

        maskargs->lines_per_block = hm / hi;
        imageargs->lines_per_block = 1;
      }
      else {
        if ( hm < 1 || hi % hm != 0 )
          return error_handler( RANGECHECK ) ;

        maskargs->lines_per_block = 1;
        imageargs->lines_per_block = hi / hm;
      }

      maskargs->nprocs = 1;
    }
    break ;

  case INTERLEAVE_SEPARATE :
    /* Unless we have a contone mask, the bitdepth must be 1. */
    if (! maskargs->contoneMask) {
      if ( maskargs->bits_per_comp != 1)
        return error_handler( RANGECHECK ) ;
    }

    if ( maskargs->nprocs != 1 )
      return error_handler( RANGECHECK ) ;
    break ;

  default :
    HQFAIL( "bad interleave" ) ;
    return error_handler( UNDEFINEDRESULT ) ;
  }

  /* The mask's ImageMatrix must align the corresponding corners of the image
     (PLRM3, p.306, table 4.24, ImageMatrix). PDF doesn't allow ImageMatrix
     to be specified, so this isn't a problem for SMask images. We'll check
     this by cross-multiplying the image and mask matrices by their
     corresponding width and height, and checking the relative error is
     sufficiently small. */
  for ( i = 0 ; i < 3 ; ++i ) {
    if ( !mask_matrix_check(imageargs->omatrix.matrix[i][0] * maskargs->width,
                            maskargs->omatrix.matrix[i][0] * imageargs->width) ||
         !mask_matrix_check(imageargs->omatrix.matrix[i][1] * maskargs->height,
                            maskargs->omatrix.matrix[i][1] * imageargs->height) )
      return error_handler(RANGECHECK) ;
  }

  return TRUE;
}


static Bool parseType12ImageDict(IMAGEARGS *imageargs,
                                 int32 ncomps, OBJECT * o1)
{
  Bool needData ;
  IMAGEARGS *alpha_args ;
  OBJECT * theo ;
  int i ;

  enum {
    image12_InterleaveType, image12_MaskDict, image12_DataDict, image12_max
  } ;
  static NAMETYPEMATCH imagedict12match[image12_max + 1] = {
    { NAME_InterleaveType , 1, { OINTEGER }},                 /* 0 */
    { NAME_MaskDict , 1, { ODICTIONARY }},                    /* 1 */
    { NAME_DataDict , 1, { ODICTIONARY }},                    /* 2 */
    DUMMY_END_MATCH
  } ;


  if ( ! dictmatch( o1 , imagedict12match ))
    return FALSE ;

  needData = FALSE ;  /* assume mask data comes from image's datasource */

  /* InterleaveType: i.e. whether mask data is interleaved with
     the image data through the same source, or not.
  */
  theo = imagedict12match[image12_InterleaveType].result ;
  switch ( oInteger( *theo )) {
  case 1:   /* for each pixel/sample; mask bits first, then colorant bits */
    imageargs->interleave = INTERLEAVE_PER_SAMPLE;
    break ;

  case 2:   /* for each row/scanline; mask row first, then colorants */
    imageargs->interleave = INTERLEAVE_SCANLINE;
    break ;

  case 3:   /* image and mask provide their own datasources */
    imageargs->interleave = INTERLEAVE_SEPARATE;
    needData = TRUE ; /* mask provides its own datasource */
    break ;

  default:
    return error_handler( RANGECHECK ) ;
  }

  /* MaskDict */
  o1 = imagedict12match[image12_MaskDict].result ;
  if ( ! check_dict_access( o1 ) )
    return FALSE ;

  alpha_args = imageargs->maskargs;

  HQASSERT( alpha_args != NULL, "no alpha_args when called from image" );

  imageargs->imagetype = TypeImageAlphaImage;
  alpha_args->imagetype = TypeImageAlphaAlpha;

  if ( ! get_image_dictargs(o1, 1, 1, needData, alpha_args, FALSE) )
    return FALSE ;

  /* image's own DataDict */
  o1 = imagedict12match[image12_DataDict].result ;
  if ( ! check_dict_access( o1 ) )
    return FALSE ;

  if ( ! get_image_dictargs(o1, 1, ncomps, TRUE, imageargs, TRUE) )
    return FALSE ;

  switch ( imageargs->interleave ) {
  case INTERLEAVE_PER_SAMPLE :
    if ( alpha_args->width != imageargs->width ||
         alpha_args->height != imageargs->height ||
         alpha_args->bits_per_comp != imageargs->bits_per_comp ||
         alpha_args->nprocs != 0 ||
         imageargs->nprocs != 1 )
      return error_handler( RANGECHECK ) ;

    alpha_args->nprocs = 1;

    /* Ensure input is processed one line at a time to allow
       mask generation to be performed in the input buffer */
    alpha_args->lines_per_block  = 1;
    imageargs->lines_per_block = 1;
    break ;

  case INTERLEAVE_SCANLINE :

    if (alpha_args->nprocs != 0 ||
        imageargs->nprocs != 1)
      return error_handler( RANGECHECK ) ;

    {
      /* When row interleaved, image and mask may be specified with
         different heights, but only so long as one is a whole multiple
         of the other (see PLRM.3). This further means that there will
         be more rows per block (= combination of mask-then-image data)
         for the higher than the other. */
      int32 hm = alpha_args->height;
      int32 hi = imageargs->height;

      if (hm > hi) {
        if ( hi < 1 || hm % hi != 0 )
          return error_handler( RANGECHECK ) ;

        alpha_args->lines_per_block = hm / hi;
        imageargs->lines_per_block = 1;
      }
      else {
        if ( hm < 1 || hi % hm != 0 )
          return error_handler( RANGECHECK ) ;

        alpha_args->lines_per_block = 1;
        imageargs->lines_per_block = hi / hm;
      }

      alpha_args->nprocs = 1;
    }
    break ;

  case INTERLEAVE_SEPARATE :

    if ( alpha_args->nprocs != 1 )
      return error_handler( RANGECHECK ) ;
    break ;

  default :
    HQFAIL( "bad interleave" ) ;
    return error_handler( UNDEFINEDRESULT ) ;
  }

  /* This is a restriction applied to Type 3 images, which also makes sense
     for Type 12 images (it means we can combined image and alpha stores).

     The mask's ImageMatrix must align the corresponding corners of the image
     (PLRM3, p.306, table 4.24, ImageMatrix). PDF doesn't allow ImageMatrix
     to be specified, so this isn't a problem for SMask images. We'll check
     this by cross-multiplying the image and mask matrices by their
     corresponding width and height, and checking the relative error is
     sufficiently small. */
  /** \todo ajcd 2014-01-14: Move the automatic re-sampling of Type 12 images
      from pdfimg.c to here, so it applies to all image types. Be careful
      because we can't insert into the image dictionary to control the
      filtering target width and height. */
  for ( i = 0 ; i < 3 ; ++i ) {
    if ( !mask_matrix_check(imageargs->omatrix.matrix[i][0] * alpha_args->width,
                            alpha_args->omatrix.matrix[i][0] * imageargs->width) ||
         !mask_matrix_check(imageargs->omatrix.matrix[i][1] * alpha_args->height,
                            alpha_args->omatrix.matrix[i][1] * imageargs->height) )
      return error_handler(RANGECHECK) ;
  }

  return TRUE;
}

/* See header for doc. */
void init_image_args(IMAGEARGS *imageargs, int8 colortype)
{
  IMAGEARGS template = {0};
  *imageargs = template;
  imageargs->colorType = colortype;
  imageargs->image_pixel_centers = TRUE;
  imageargs->clean_matrix = TRUE;
}

static void do_maxd(uint8 *str, int ii, int32 *maxd)
{
  int32 d = (int32)str[ii] - (int32)str[ii-3];
  if ( d > *maxd )
    *maxd = d;
}

static Bool setup_downsample(DL_STATE *page, IMAGEARGS *imageargs)
{
  int32 tw = imageargs->width, th = imageargs->height, fw, fh;
  int32 downLen = 16, downAreaPC = 60, downXYLenPC = 75;
  int32 downPC = (page->xdpi < 888.8)?25:15;
  SYSTEMVALUE w[2], h[2], scale;
  int32 downx = 1, downy = 1;
  OMATRIX scaleMatrix, i2d;
  Bool linear_cs = TRUE;

  /* No downsampling by default */
  imageargs->downsample.enabled = FALSE;
  imageargs->downsample.method = 0;
  imageargs->downsample.x = imageargs->downsample.y = 1;
  imageargs->downsample.w0 = imageargs->width;
  imageargs->downsample.h0 = imageargs->height;
  imageargs->downsample.rowrepeats_near = FALSE;
  imageargs->downsample.rowrepeats_2rows = FALSE;

  /* Calculate the image to device transform */
  if ( !im_calculatematrix(imageargs, &i2d, TRUE))
    return FALSE;
  /* Calculate the size of the image in device pixels */
  MATRIX_TRANSFORM_DXY(imageargs->width, 0, w[0], w[1], &i2d);
  MATRIX_TRANSFORM_DXY(0, imageargs->height, h[0], h[1], &i2d);
  fw = (int32)(sqrt(w[0]*w[0] + w[1]*w[1]) + 0.5);
  fh = (int32)(sqrt(h[0]*h[0] + h[1]*h[1]) + 0.5);

  /* Don't try down-sampling images that are to be scaled to nothing ! */
  if ( fw == 0 || fh == 0 )
    return TRUE;

  if ( imageargs->filteredImage )
    return TRUE;
  if ( imageargs->imagetype != TypeImageImage &&
       imageargs->imagetype != TypeImageAlphaImage &&
       imageargs->imagetype != TypeImageAlphaAlpha )
    return TRUE;
  if ( imageargs->imagetype == TypeImageAlphaImage &&
       (imageargs->width != imageargs->maskargs->width ||
        imageargs->height != imageargs->maskargs->height) )
    return TRUE;
  if ( imageargs->bits_per_comp != 8 && imageargs->bits_per_comp != 1 )
    return TRUE;
  if ( imageargs->ncomps != 1 && imageargs->ncomps != 3 &&
       imageargs->ncomps != 4 )
    return TRUE;
  if ( imageargs->bits_per_comp == 1 && imageargs->ncomps != 1 )
    return TRUE;
  if ( tw <= 0 || th <= 0 )
    return TRUE;

  if ( get_core_context_interp()->userparams->ImageDownsampling )
    downx = downy = 4;

  if ( downx < 2 )
    return TRUE;

  /* Turn off >2 down-sampling except for 1:1 or better 8bit images. */
  if ( downx > 2 && (fw > tw || imageargs->bits_per_comp != 8) )
    downx = 2;
  if ( downy > 2 && (fh > th || imageargs->bits_per_comp != 8) )
    downy = 2;
  if ( tw <= downLen || tw*1.0/fw < downPC/100.0 )
    downx = 1;
  if ( th <= downLen || th*1.0/fh < downPC/100.0 )
    downy = 1;
  if ( downx == 1 && downy == 1 )
    return TRUE;
  if ( tw/downx <= 0 || th/downy <= 0 )
    return TRUE;

  /*
   * Can only do near row-repeats if the colorants behave linearly, i.e.
   * not for example with a indexed colorspace with uneven steps. Only really
   * care about PCL jobs, and these often have an indexed colorspace, but with
   * every colorant being +1 on the previous. So test for this explicitly, and
   * for anything else problematic just dont do near-enough row repeats.
   */
  switch ( imageargs->image_color_space ) {
    case SPACE_DeviceGray:
    case SPACE_DeviceRGB:
    case SPACE_DeviceCMY:
    case SPACE_DeviceCMYK:
      break;
    case SPACE_Indexed: {
      OBJECT *cso = &imageargs->image_color_object;

      linear_cs = FALSE;
      if ( oType(*cso) == OARRAY && theLen(*cso) == 4 ) {
        OBJECT *obj = &oArray(*cso)[1];
        if ( oType(*obj) == ONAME &&
             oName(*obj) == &system_names[NAME_DeviceRGB] ) {
          obj = &oArray(*cso)[3];
          if ( oType(*obj) == OSTRING ) {
            uint8 *str = oString(*obj);
            int32 i, maxd = 0 , len = theLen(*obj);
            if ( len == 256*3 ) {
              for ( i = 1; i < 256; i++ ) {
                do_maxd(str, 3*i+0, &maxd);
                do_maxd(str, 3*i+1, &maxd);
                do_maxd(str, 3*i+2, &maxd);
              }
              if ( maxd == 1 )
                linear_cs = TRUE;
            }
          }
        }
      }
      break;
    }
    default:
      linear_cs = FALSE;
      break;
  }

  /* Start by assuming we can downsample provided we have a linear colorspace,
     but look more closely below */
  imageargs->downsample.rowrepeats_near = linear_cs;
  imageargs->downsample.rowrepeats_2rows = linear_cs;
  if ( downAreaPC > 0 ) {
    int pagearea100, imagearea;
    /* Calculate the area of the page */
    pagearea100 = (page->page_w * page->page_h)/100;
    /* Calculate the area of the image */
    imagearea = fw * fh;
    if (imagearea/pagearea100 > downAreaPC)
      return TRUE;
  }
  if ( downXYLenPC > 0 ) {
    if ( i2d.opt == MATRIX_OPT_1001 ) {
      /* rotated page */
      if ( ((fw * 100)/page->page_h) > downXYLenPC )
        return TRUE;

      /* If it was not a very wide image but is so tall that we are not
       * going to downsample then do not do the row repeat either. */
      imageargs->downsample.rowrepeats_near = FALSE;

      if ( ((fh * 100)/page->page_w) > downXYLenPC )
        return TRUE;
      /* temporary optimisation for text issues with x4 downsample */
      if ( (downx > 2) || (downy > 2) ) {
        if ( (((fw * 100)/page->page_h) > downXYLenPC/8) &&
             (((fw * 100)/page->page_h) < downXYLenPC/4) ) {
          if ( downx > 2 )
            downx = 2;
          if ( downy > 2 )
            downy = 2;
        }
        if ( (((fh * 100)/page->page_w) > downXYLenPC/8) &&
             (((fh * 100)/page->page_w) < downXYLenPC/4) ) {
          if ( downx > 2 )
            downx = 2;
          if ( downy > 2 )
            downy = 2;
        }
      }
    } else {
      if ( ((fw * 100)/page->page_w) > downXYLenPC )
        return TRUE;
      if ( ((fh * 100)/page->page_h) > downXYLenPC )
        return TRUE;
      /* temporary optimisation for text issues with x4 downsample */
      if ( (downx > 2) || (downy > 2) ) {
        if ( (((fw * 100)/page->page_w) > downXYLenPC/8) &&
             (((fw * 100)/page->page_w) < downXYLenPC/4) ) {
          if ( downx > 2 )
            downx = 2;
          if ( downy > 2 )
            downy = 2;
        }
        if ( (((fh * 100)/page->page_h) > downXYLenPC/8) &&
             (((fh * 100)/page->page_h) < downXYLenPC/4) ) {
          if ( downx > 2 )
            downx = 2;
          if ( downy > 2 )
            downy = 2;
        }
      }
    }
  }
  /* Set this false here just in case as we will be downsampling */
  imageargs->downsample.rowrepeats_near = FALSE;

  /* Suitable for downsampling, go for it */
  imageargs->downsample.enabled = TRUE;
  imageargs->downsample.x = downx;
  imageargs->downsample.y = downy;
  imageargs->width = tw/downx;
  imageargs->height = th/downy;
  imageargs->lines_per_block /= downy;

  scaleMatrix = identity_matrix;
  scaleMatrix.opt = MATRIX_OPT_0011;
  scale = (SYSTEMVALUE)(tw/downx)/(SYSTEMVALUE)tw;
  scaleMatrix.matrix[0][0] = scale;
  scale = (SYSTEMVALUE)(th/downy)/(SYSTEMVALUE)th;
  scaleMatrix.matrix[1][1] = scale;
  matrix_mult(&imageargs->omatrix, &scaleMatrix, &imageargs->omatrix);

  /* Downsample the alpha channel to match the main image. */
  if ( imageargs->imagetype == TypeImageAlphaImage ) {
    if ( !setup_downsample(page, imageargs->maskargs) )
      return FALSE;
    HQASSERT(imageargs->width == imageargs->maskargs->width &&
             imageargs->height == imageargs->maskargs->height,
             "Failed to downsample an alpha-channel image correctly");
  }
  return TRUE;
}

void set_image_order(IMAGEARGS *imageargs)
{
  int32 devtype = CURRENT_DEVICE() ;

  HQASSERT(imageargs->imagetype == TypeImageMask ||
           imageargs->imagetype == TypeImageImage ||
           imageargs->imagetype == TypeImageMaskedImage ||
           imageargs->imagetype == TypeImageAlphaImage,
           "Should only apply image order to top-level image") ;

  /* Don't apply image render order optimisations if in nulldevice, char
     device, or if in error or suppressing output: it would just be a waste
     of time. char device rendering explicitly requires no optimisation, it
     uses destination to source space transformation. */
  if ( devtype == DEVICE_BAND ||
       devtype == DEVICE_PATTERN1 || devtype == DEVICE_PATTERN2 ) {
    corecontext_t *context = get_core_context() ;
    const surface_set_t *surfaces = context->page->surfaces ;
    IMAGEARGS *maskargs = imageargs->maskargs ;
    int image_order = surface_find(surfaces, SURFACE_OUTPUT)->render_order ;

    if ( (image_order & SURFACE_ORDER_DEVICELR) != 0 )
      imageargs->flipswap |= IMAGE_OPTIMISE_SWAP|IMAGE_OPTIMISE_XFLIP ;
    if ( (image_order & SURFACE_ORDER_DEVICETB) != 0 )
      imageargs->flipswap |= IMAGE_OPTIMISE_YFLIP ;
    if ( (image_order & SURFACE_ORDER_IMAGEROW) != 0 )
      imageargs->flipswap |= IMAGE_OPTIMISE_SWAP4SPEED ;
    /* Disable 1:1 optimisation if inside a pattern. */
    if ( devtype == DEVICE_BAND &&
         context->systemparams->DeviceImageOptimization &&
         (image_order & SURFACE_ORDER_COPYDOT) != 0 )
      imageargs->flipswap |= IMAGE_OPTIMISE_1TO1 ;


    /* Masked images require the same optimisation from the mask and the
       image. In theory, we could allow separate image/mask sources to have
       different optimisations, however that could result in missing image
       sections if we run out of memory in the middle of the image and stripe
       it. So we force the mask */
    /** \todo ajcd 2013-12-23: We don't actually check that the mask matrix
        is similar anywhere. So this might be a problem. */
    if ( maskargs != NULL ) {
      int mask_order ;

      HQASSERT(maskargs->imagetype == TypeImageMaskedMask ||
               maskargs->imagetype == TypeImageAlphaAlpha,
               "Unknown image mask type") ;

      mask_order = surface_find(surfaces,
                                maskargs->imagetype == TypeImageAlphaAlpha
                                ? SURFACE_TRANSPARENCY
                                : SURFACE_CLIP)->render_order ;

      /* The mask's swap is inherited from the image, so there's no point
         setting SWAP4SPEED or SWAP. */
      if ( (mask_order & SURFACE_ORDER_DEVICELR) != 0 )
        maskargs->flipswap |= IMAGE_OPTIMISE_XFLIP ;
      if ( (mask_order & SURFACE_ORDER_DEVICETB) != 0 )
        maskargs->flipswap |= IMAGE_OPTIMISE_YFLIP ;
      /* Disable 1:1 optimisation if inside a pattern. */
      if ( devtype == DEVICE_BAND &&
           context->systemparams->DeviceImageOptimization &&
           (mask_order & SURFACE_ORDER_COPYDOT) != 0 )
        maskargs->flipswap |= IMAGE_OPTIMISE_1TO1 ;
    }
    /** \todo ajcd 2013-12-21: Can we include knockout, image type, on the
        fly, clip type and render map properties early enough to prevent
        non-optimal data swap/flip? */
  }
}

/* See header for doc. */
Bool get_image_args( corecontext_t *context,
                     STACK *stack,         /* operandstack or pdf stack */
                     IMAGEARGS *imageargs, /* to return all the args' values */
                     int32 imageop )       /* operator name- who's calling us */
{
  enum {
    image_ImageType, image_max
  } ;
  static NAMETYPEMATCH imagetypematch[image_max + 1] = {
    { NAME_ImageType , 1, { OINTEGER }},                      /* 0 */
    DUMMY_END_MATCH
  } ;

  enum {
    image4_MaskColor, image4_max
  } ;
  static NAMETYPEMATCH imagedict4match[image4_max + 1] = {
    { NAME_MaskColor , 3, { OINTEGER, OARRAY, OPACKEDARRAY }}, /* 0 */
    DUMMY_END_MATCH
  } ;

  uint8 tags1 ;
  int32 width , height , i , stack_args ;
  int32 ncomps , nprocs , bits_per_comp ;
  Bool interpolate, multiproc, polarity ;
  COLORSPACE_ID input_color_space ;
  OBJECT input_color_object = OBJECT_NOTVM_NOTHING ;
  OBJECT *theo , *o1 ;
  IMAGEARGS *maskargs = imageargs->maskargs ;

  /* init to keep compiler quiet */
  interpolate = FALSE ;
  polarity = FALSE ;
  bits_per_comp = 1 ;

  imageargs->imageop = imageop;
  imageargs->imagetype = TypeImageImage; /* default */
  imageargs->interleave = INTERLEAVE_NONE; /* default (no mask) */

  switch ( imageop ) {

  case NAME_imagemask :
    HQASSERT( maskargs == NULL ,
              "get_image_args: maskargs when called from imagemask" ) ;
    imageargs->imagetype = TypeImageMask;
    /* FALL THROUGH... */

  case NAME_image :
    if ( theISaveLangLevel( workingsave ) >= 2 ) {
      /* check to see if the top arg is a dictionary */
      if ( theIStackSize( stack ) < 0 )
        return error_handler( STACKUNDERFLOW ) ;
      o1 = theITop( stack ) ;
      if ( oType( *o1 ) == ODICTIONARY ) {
        if ( ! check_dict_access( o1 ) )
          return FALSE ;

        /* we've got the dictionary form of image/imagemask */
        stack_args = 1 ;

        if ( imageop == NAME_imagemask ) {
          ncomps = 1 ;
          input_color_space = SPACE_DeviceGray ;
          theTags( input_color_object ) = ONULL ;

          if ( ! get_image_dictargs(o1, 1, 1, TRUE, imageargs, FALSE) )
            return FALSE ;

          get_mask_polarity( imageargs ) ;

          polarity = imageargs->polarity;
        }
        else {
          /* the number of components in the image is determined by the
           * current colour space.
           */
          input_color_space = gsc_getcolorspace( gstateptr->colorInfo , GSC_FILL ) ;
          input_color_object = *gsc_getcolorspaceobject( gstateptr->colorInfo , GSC_FILL ) ;
          ncomps = gsc_dimensions( gstateptr->colorInfo , GSC_FILL ) ;

          /* images not allowed using pattern space - only imagemask */
          if ( input_color_space == SPACE_Pattern )
            return error_handler( RANGECHECK ) ;

          if (! dictmatch( o1 , imagetypematch ))
            return FALSE ;

          /* ImageType */
          theo = imagetypematch[image_ImageType].result ;
          switch ( oInteger( *theo ) ) {
          case 1 :
            if ( ! get_image_dictargs(o1, 1, ncomps, TRUE, imageargs, TRUE) )
              return FALSE ;
            break ;

          case 3 :
            if (!parseType3ImageDict(imageargs, ncomps, o1))
              return FALSE;
            break;

          case 4 :
            {
              if ( ! dictmatch( o1 , imagedict4match ))
                return FALSE ;

              HQASSERT( maskargs != NULL ,
                        "get_image_args: no maskargs when called from image" ) ;

              imageargs->imagetype = TypeImageMaskedImage;
              maskargs->imagetype = TypeImageMaskedMask;

              /* Get the common arguments */
              if ( ! get_image_dictargs(o1, 4, ncomps, TRUE, imageargs, TRUE) )
                return FALSE ;

              imageargs->interleave = INTERLEAVE_CHROMA;

              /* MaskColor */
              imageargs->maskgen = im_genmaskopen( ncomps ,
                                                   imageargs->width ,
                                                   imagedict4match[image4_MaskColor].result ) ;
              if ( imageargs->maskgen == NULL )
                return FALSE ;

              /* Create an appropriate mask description */
              maskargs->ncomps = 1;
              maskargs->width = imageargs->width;
              maskargs->height = imageargs->height;
              maskargs->omatrix = imageargs->omatrix;
              maskargs->nprocs = 1;
              maskargs->bits_per_comp = 1;

              /* Allocate N-color arrays for image decode. */
              if ( ! alloc_image_decode( maskargs , 1 ))
                return FALSE ;

              maskargs->decode[ 0 ] = 0.0f;
              maskargs->decode[ 1 ] = 1.0f;
              maskargs->interpolate = imageargs->interpolate;

              maskargs->lines_per_block = 1;
              imageargs->lines_per_block = 1;

              get_mask_polarity( maskargs ) ;
            }
            break ;

          case 12 :       /* TIFF alpha channel images */
            if (!parseType12ImageDict(imageargs, ncomps, o1))
              return FALSE;
            break;

          case 13 : /* Same as type 1, but allows 32bit data for debugging */
            imageargs->imagetype = TypeImageImage;
            if ( ! get_image_dictargs(o1, 13, ncomps, TRUE, imageargs, TRUE) )
              return FALSE ;
            break ;


          default :
            return error_handler( TYPECHECK ) ;

          }
        }

        width = imageargs->width;
        height = imageargs->height;
        nprocs = imageargs->nprocs;
        bits_per_comp = imageargs->bits_per_comp;
        interpolate = imageargs->interpolate;
        break ;
      }
    }
    /* if operand is not a dictionary, fall through to get args off the stack */

  case NAME_colorimage :

    if ( imageop == NAME_colorimage ) {

      HQASSERT( maskargs == NULL ,
                "get_image_args: maskargs when called from colorimage" ) ;

      /* colorimage with args on the stack */
      if ( theIStackSize( stack ) < 6 )
        return error_handler( STACKUNDERFLOW ) ;
      o1 = theITop( stack ) ;     /* the number of components */
      if ( oType( *o1 ) != OINTEGER )
        return error_handler( TYPECHECK ) ;
      ncomps = oInteger( *o1 ) ;

      switch ( ncomps ) {   /* only 1 , 3 , 4 are allowed */
      case 1 :
        input_color_space = SPACE_DeviceGray ; break ;
      case 3 :
        input_color_space = SPACE_DeviceRGB ; break ;
      case 4 :
        input_color_space = SPACE_DeviceCMYK ; break ;
      default:
        return error_handler( RANGECHECK ) ;
      }
      theTags( input_color_object ) = ONULL ;

      /* the flag to say multiproc or not */
      o1 = stackindex( 1 , stack ) ;
      if ( oType( *o1 ) != OBOOLEAN )
        return error_handler( TYPECHECK ) ;
      multiproc = oBool( *o1 ) ;

      /* Now we have enough information to say how many objects
       * should be on the stack...
       */
      if ( multiproc )
        nprocs = ncomps ;
      else
        nprocs = 1 ;

      stack_args = 6 + nprocs ;

    } else {
      /* image or imagemask with args on the stack */
      stack_args = 5 ;
      multiproc = FALSE ;
      input_color_space = SPACE_DeviceGray ;
      theTags( input_color_object ) = ONULL ;
      ncomps = 1 ;
      nprocs = 1 ;
      bits_per_comp = 1 ;
    }

    /* Get the interpolate from the default. */
    if (imageop == NAME_colorimage || imageop == NAME_image)
      interpolate = quadStateApply(&context->userparams->InterpolateAllImages, NULL);
    else
      interpolate = quadStateApply(&context->userparams->InterpolateAllMasks, NULL);

    if ( theIStackSize( stack ) < ( stack_args - 1 ))
      return error_handler( STACKUNDERFLOW ) ;

    /* width must be an integer */
    o1 = stackindex( stack_args - 1 , stack ) ;
    if ( oType( *o1 ) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    width = oInteger( *o1 ) ;

    /* height must be an integer */
    o1 = stackindex( stack_args - 2 , stack ) ;
    if ( oType( *o1 ) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    height = oInteger( *o1 ) ;
    /* by default allow the whole image to be read contiguously */
    imageargs->lines_per_block = height;

    /* and the bits/sample (for image and colorimage) */
    o1 = stackindex( stack_args - 3 , stack ) ;
    if ( imageop != NAME_imagemask ) {
      if ( oType( *o1 ) != OINTEGER )
        return error_handler( TYPECHECK ) ;
      bits_per_comp = oInteger( *o1 ) ;
    }
    else {
      /* the polarity for imagemask */
      if ( oType( *o1 ) != OBOOLEAN )
        return error_handler( TYPECHECK ) ;
      polarity = oBool( *o1 ) ;
      if ( context->systemparams->ImagemaskBug )
        if ( context->page->forcepositive )
          polarity = ( ! polarity ) ;
    }

    /* get the matrix too */
    o1 = stackindex( stack_args - 4 , stack ) ;
    if ( ! oCanRead( *o1 ))  /* must be readable */
      if ( ! object_access_override(o1) )
        return error_handler( INVALIDACCESS ) ;
    if ( ! is_matrix( o1 , & imageargs->omatrix))
      return FALSE ;

    /* Allocate N-color arrays for image data src. */
    if ( ! alloc_image_datasrc( imageargs , nprocs ))
      return FALSE ;

    /* Possibly deal with multiple procedures, ordered for compatability. */
    for ( i = nprocs - 1 ; i >= 0 ; i-- ) {
      theo = stackindex( stack_args - 5 - i , stack ) ;
      if ( ! check_image_proc( theo ))
        return FALSE ;
      Copy(&imageargs->data_src[i], theo) ;
    }

    /* Allocate N-color arrays for image decode. */
    if ( ! alloc_image_decode( imageargs , ncomps ))
      return FALSE ;

    /* Set the decode array in the structure */
    for ( i = 0 ; i < 2 * ncomps ; i += 2 ) {
      if ( !polarity ) {
        imageargs->decode[ i + 0 ] = 0.0f;
        imageargs->decode[ i + 1 ] = 1.0f;
      }
      else {
        imageargs->decode[ i + 0 ] = 1.0f;
        imageargs->decode[ i + 1 ] = 0.0f;
      }
    }
    break ;

  default:
    return error_handler( UNREGISTERED ) ;
  }
  /* finished extracting the args for image/imagemask/colorimage */

  /* Check a few bits-n-bobs. */
  if (( width < 0 ) || ( height < 0 )) /* Must have positive width, height */
    return error_handler( RANGECHECK ) ;

  /* Only 1, 2, 4, 8, 12, 16 allowed for bits/sample. */
  switch ( bits_per_comp ) {
  case 1 :
    break ;
  case 2 :
  case 4 :
  case 8 :
  case 12 :
  case 16:
    /* This is apparently what Adobe do!! */
    if ( imageop == NAME_imagemask )
      bits_per_comp = 1 ;
    break ;
  case 32: /* 32bit data in type 13  images for easy debugging */
    if ( oInteger( *imagetypematch[image_ImageType].result ) == 13 )
      break;
  default:
    return error_handler( RANGECHECK ) ;
  }

  /* Check all the procedures are of the same type. For the purposes
   * of this test, array and packed array count as the same type.
   */
  HQASSERT( imageargs->data_src != NULL ,
            "Should have already allocated & filled in data src" ) ;
  tags1 = ( uint8 ) oType(imageargs->data_src[0]) ;
  for ( i = 1 ; i < nprocs ; i++ ) {
    uint8 tags2 = ( uint8 ) oType(imageargs->data_src[i]) ;
    if (( tags1 != tags2 ) &&
        !((( tags1 == OARRAY ) && ( tags2 == OPACKEDARRAY )) ||
          (( tags1 == OPACKEDARRAY ) && ( tags2 == OARRAY ))))
      return error_handler( TYPECHECK ) ;
  }

  /* insert value into the IMAGEARGS structure */
  imageargs->width = width;
  imageargs->height = height;
  imageargs->bits_per_comp = bits_per_comp;
  imageargs->ncomps = ncomps;
  imageargs->nprocs = nprocs;
  imageargs->image_color_space = input_color_space;
  imageargs->image_color_object = input_color_object;
  imageargs->stack_args = stack_args;
  imageargs->polarity = ( int8 )polarity;
  imageargs->interpolate = ( int8 )interpolate;
  imageargs->colorspace = input_color_object;

  /* handle misc hooks for EP2000 preflight etc */
  if ( ! check_image_misc_hooks( imageargs ))
    return FALSE ;

  if (( imageargs->imagetype != TypeImageMaskedImage ) &&
      ( imageargs->imagetype != TypeImageAlphaImage ) ) {
    maskargs = imageargs->maskargs = NULL;
  }

  /* Copy the coerceToFill flag over to the mask args */
  if ( maskargs != NULL ) {
    maskargs->coerce_to_fill = imageargs->coerce_to_fill ;

    /* Copy interleave type into mask, so we can tell whether it has an
       independent datasource from just examining the mask. It isn't used for
       anything else, anyway. */
    maskargs->interleave = imageargs->interleave ;
  }

  set_image_order(imageargs) ;

  return filter_image_args(context, imageargs) ;
}

Bool filter_image_args(corecontext_t *context, IMAGEARGS *imageargs)
{
  /* Submit this image to the image filtering systems */
  if ( !filteredImageNew(context->page, imageargs) )
    return FALSE;

  if ( !setup_downsample(context->page, imageargs) )
    return FALSE;

  return TRUE ;
}


static Bool check_image_misc_hooks( IMAGEARGS *imageargs )
{
  int32 width = imageargs->width;
  int32 height = imageargs->height;
  COLORSPACE_ID input_color_space = imageargs->image_color_space;
  SYSTEMVALUE args[ 8 ] ;
  SYSTEMVALUE vals[ 4 ] ;
  int32 argsdone = eNoArgs ;
  COLORSPACE_ID basecolorspace ;
  MISCHOOKPARAMS *mischookparams = get_core_context_interp()->mischookparams ;

  /* RGB images */
  if (( oType(mischookparams->ImageRGB) != ONULL ) &&
      (( input_color_space == SPACE_DeviceRGB ) ||
       (( input_color_space == SPACE_Indexed ) &&
        ( gsc_getbasecolorspace( gstateptr->colorInfo , GSC_FILL , & basecolorspace )) &&
        ( basecolorspace == SPACE_DeviceRGB ))) &&
      (( height > 1 ) &&        /* ignore vignette images */
       ( width > 1 )) &&
      ( ! CURRENT_DEVICE_SUPPRESSES_MARKS())) {
    /* llx, lly, urx, ury, /Page -> ImageRGB
     * or
     * /Character -> ImageRGB
     * or
     * /Pattern -> ImageRGB
     */
    if ( ! execute_image_misc_hook(imageargs, &mischookparams->ImageRGB,
                                   TRUE, &argsdone, args, vals) )
      return FALSE ;
  }

  /* now check for images at too low a resolution */
  if ( oType(mischookparams->ImageLowRes) != ONULL &&
       (height > 1 && width > 1) &&       /* ignore vignette images */
      ( CURRENT_DEVICE() == DEVICE_BAND ||
        (CURRENT_DEVICE() == DEVICE_SUPPRESS && ! routedev_currentDLsuppression ())))
  {
    /* MinRes, bitdepth, llx, lly, urx, ury -> ImageLowRes */
    SYSTEMVALUE res1, res2, llx, lly ;
    USERVALUE thisRes = (imageargs->bits_per_comp == 1
                         ? mischookparams->ImageLowLW
                         : mischookparams->ImageLowCT) ;

    /* find image bounding box */
    if ( argsdone == eNoArgs ) {
      if ( ! image_misc_hooks_corners( imageargs , args ))
        return FALSE ;
      argsdone = eBboxArgs ;
    }

    /* args 0 & 1 are X and Y for LL, 2 and 3 for LR
     * 4 and 5 for UL and 6 and 7 for UR corners.
     * This calculation ignores strange effects from sheered images,
     * but it's hard to see what would be 'right' there anyway.
     */
    llx = args[ 0 ] ;
    lly = args[ 1 ] ;

    {
      SYSTEMVALUE d1 = sqrt(( args[ 2 ] - llx ) * ( args[ 2 ] - llx ) + ( args[ 3 ] - lly ) * ( args[ 3 ] - lly ));
      SYSTEMVALUE d2 = sqrt(( args[ 4 ] - llx ) * ( args[ 4 ] - llx ) + ( args[ 5 ] - lly ) * ( args[ 5 ] - lly ));
      res1 = ( SYSTEMVALUE )width  * 72.0 / d1 ;
      res2 = ( SYSTEMVALUE )height * 72.0 / d2 ;
    }

    if ( res1 > res2 )
      res1 = res2 ;

    if ( res1 < thisRes && thisRes >= 0.0 ) {
      if ( ! stack_push_real( res1, &operandstack ) ||
           ! stack_push_integer(imageargs->bits_per_comp, &operandstack) )
        return FALSE ;

      if ( ! execute_image_misc_hook( imageargs ,
                                      &mischookparams->ImageLowRes,
                                      FALSE , & argsdone , args , vals ))
        return FALSE ;
    }
  }

  /* and bitmap fonts */
  if ( oType(mischookparams->FontBitmap) != ONULL &&
       CURRENT_DEVICE() == DEVICE_CHAR ) {
    /* FontName, UniqueID -> FontBitmap */
    OBJECT *poFontName ;
    OBJECT *poUniqueID ;

    poFontName = fast_extract_hash_name(&theMyFont(theFontInfo(*gstateptr)), NAME_FontName) ;
    if ( poFontName == NULL )
      poFontName = &onull;

    poUniqueID = fast_extract_hash_name(&theMyFont(theFontInfo(*gstateptr)), NAME_UniqueID) ;
    if ( poUniqueID == NULL )
      poUniqueID = &onull;

    if ( ! push2(poFontName, poUniqueID , &operandstack) ||
         ! push(&mischookparams->FontBitmap, &executionstack) ||
         ! interpreter( 1 , NULL ))
      return FALSE ;
  }
  return TRUE ;
}

static Bool execute_image_misc_hook( IMAGEARGS *imageargs ,
                                     OBJECT *hook_proc , int32 showdevice ,
                                     int32 *argsknown ,
                                     SYSTEMVALUE args[] , SYSTEMVALUE vals[])
{
  int32 devtype = CURRENT_DEVICE() ;

  HQASSERT(! CURRENT_DEVICE_SUPPRESSES_MARKS(),
           "execute_image_misc_hook should not be called for suppressed marks or null device");

  if ( dev_is_bandtype(devtype) ) {
    int32 i ;

    if ((*argsknown) != eBboxAndClipArgs ) {
      OMATRIX matrix2 ;
      CLIPPATH cp ;
      SYSTEMVALUE cpl, cpr, cpt, cpb, cptemp ;
      SYSTEMVALUE rx, ry ;

      if ( *argsknown == eNoArgs && ! image_misc_hooks_corners( imageargs , args ))
        return FALSE ;

      vals[ 0 ] = vals[ 1 ] = OINFINITY_VALUE ;
      vals[ 2 ] = vals[ 3 ] = 0.0 ;

      for ( i = 0 ; i < 8 ; i += 2 ) {
        rx = args[ i ] ;
        ry = args[ i + 1 ] ;
        if ( rx < vals[ 0 ] ) vals[ 0 ] = rx ;
        if ( rx > vals[ 2 ] ) vals[ 2 ] = rx ;
        if ( ry < vals[ 1 ] ) vals[ 1 ] = ry ;
        if ( ry > vals[ 3 ] ) vals[ 3 ] = ry ;
      }

      /* now check for clipping - I'm ignoring the fact that an image may be obscured by
       * other objects drawn over the top of it.
       */
      cp = thegsPageClip(*gstateptr) ;
      if (doing_imposition) {
        if ( ! matrix_inverse( & pageBaseMatrix, & matrix2 ))
          return FALSE ;
      }
      else {
        if ( !matrix_inverse(&thegsDeviceCTM(*gstateptr), &matrix2) )
          return FALSE ;
      }
      MATRIX_TRANSFORM_XY(theXd1Clip(cp), theYd1Clip(cp), cpl, cpb, &matrix2) ;
      MATRIX_TRANSFORM_XY(theXd2Clip(cp), theYd2Clip(cp), cpr, cpt, &matrix2) ;
      if ( cpl > cpr ) {
        cptemp = cpr ; cpr = cpl ; cpl = cptemp ;
      }
      if ( cpb > cpt ) {
        cptemp = cpt ; cpt = cpb ; cpb = cptemp ;
      }
      if ( cpl > vals[ 0 ] ) vals[ 0 ] = cpl ;
      if ( cpb > vals[ 1 ] ) vals[ 1 ] = cpb ;
      if ( cpr < vals[ 2 ] ) vals[ 2 ] = cpr ;
      if ( cpt < vals[ 3 ] ) vals[ 3 ] = cpt ;

      *argsknown = eBboxAndClipArgs ;
    }

    for ( i = 0 ; i < 4 ; ++i ) {
      if ( ! stack_push_real( vals[ i ], &operandstack ) )
        return FALSE ;
    }
  }

  /* push the device type on the stack */
  if ( showdevice ) {
    NAMECACHE * pcache ;

    if ( dev_is_bandtype(devtype) ) {
      pcache = & system_names[ NAME_Page ] ;
    } else {
      switch ( devtype ) {
      case DEVICE_CHAR :
        pcache = & system_names[ NAME_Character ] ;
        break ;
      case DEVICE_PATTERN1 :
      case DEVICE_PATTERN2 :
        pcache = & system_names[ NAME_Pattern ] ;
        break ;
      default:
        HQFAIL( "RGB image in illegal device type" ) ;
        return FALSE ;        /* keep compiler quiet */
      }
    }
    oName( nnewobj ) = pcache ;
    if ( ! push( & nnewobj , & operandstack ))
      return FALSE ;
  }

  return push( hook_proc , & executionstack ) &&
         interpreter( 1 , NULL ) ;
}

static Bool image_misc_hooks_corners( IMAGEARGS *imageargs , SYSTEMVALUE args[])
{
  int32 width = imageargs->width;
  int32 height = imageargs->height;
  OMATRIX matrix1, matrix2 ;

  /* generate the matrix within which the image will be processed */
  if ( ! matrix_inverse( & imageargs->omatrix , & matrix2 ))
    return FALSE ;
  matrix_mult(&matrix2, &thegsPageCTM(*gstateptr), &matrix1) ;

  if ( ! matrix_inverse(&thegsDeviceCTM(*gstateptr), &matrix2) )
    return FALSE ;
  matrix_mult( & matrix1 , & matrix2 , & matrix2 ) ;

  if ( doing_imposition )
    matrix_mult( & matrix2 , & pageBaseMatrix , & matrix2 ) ;

  /* matrix2 now defines what we need to adjust the four image corners by
   * to get default user space coordinates
   */
  MATRIX_TRANSFORM_XY( 0.0 , 0.0 , args [ 0 ] , args [ 1 ] , & matrix2 ) ;
  MATRIX_TRANSFORM_XY( width , 0.0 , args [ 2 ] , args [ 3 ] , & matrix2 ) ;
  MATRIX_TRANSFORM_XY( 0.0 , height , args [ 4 ] , args [ 5 ] , & matrix2 ) ;
  MATRIX_TRANSFORM_XY( width , height , args [ 6 ] , args [ 7 ] , & matrix2 ) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */

Bool gs_image(corecontext_t *context, STACK *stack)
{
  IMAGEARGS imageargs ;
  IMAGEARGS maskargs ;
  Bool result ;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if (DEVICE_INVALID_CONTEXT ())
    return error_handler (UNDEFINED) ;

  probe_begin(SW_TRACE_INTERPRET_IMAGE, 0);
  init_image_args(&imageargs, GSC_IMAGE) ;
  init_image_args(&maskargs, GSC_FILL) ;
  imageargs.maskargs = &maskargs ;

  result = get_image_args(context, stack, &imageargs, NAME_image) ;

  if ( result ) {
    result = DEVICE_IMAGE(context->page, stack, &imageargs) ;
  }
  free_image_args( & imageargs ) ;

  probe_end(SW_TRACE_INTERPRET_IMAGE, 0);
  return result ;
}

Bool gs_imagemask(corecontext_t *context, STACK *stack)
{
  IMAGEARGS imageargs ;
  Bool result ;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  probe_begin(SW_TRACE_INTERPRET_IMAGE, 0);
  init_image_args(&imageargs, GSC_FILL) ;

  result = get_image_args(context, stack, &imageargs, NAME_imagemask) ;
  if ( result ) {
    result = DEVICE_IMAGE(context->page, stack, &imageargs) ;
  }
  free_image_args( & imageargs ) ;
  probe_end(SW_TRACE_INTERPRET_IMAGE, 0);

  return result ;
}

Bool im_cleanup_files(IMAGEARGS *imageargs, int32 n, uint32 flags)
{
  HQASSERT(imageargs != NULL, "No image to add files to") ;
  HQASSERT(n > 0, "Number of files to cleanup should be positive") ;
  HQASSERT((flags & (IM_CLEANUP_FLUSH|IM_CLEANUP_CLOSE)) != 0,
           "Files to cleanup should be flushed and/or closed") ;

  if ( (flags & IM_CLEANUP_FLUSH) != 0 ) {
    int32 flush_index, i ;
    OBJECT *data_src_flush = mm_alloc_with_header(mm_pool_temp,
                                                  (imageargs->n_src_flush + n) * sizeof(OBJECT),
                                                  MM_ALLOC_CLASS_IMAGE) ;
    if ( data_src_flush == NULL )
      return error_handler(VMERROR) ;

    /* Add new cleanup files first, in reverse order. The cleanup order is
       unspecified, but we might as well try to close and flush in reverse
       order of creation. */
    for ( flush_index = 0 ; flush_index < n ; ++flush_index ) {
      /* Struct copy to set slot properties */
      data_src_flush[flush_index] = *stackindex(flush_index, &temporarystack) ;
      HQASSERT(oType(data_src_flush[flush_index]) == OFILE,
               "Image DataSource flush object is invalid") ;
    }

    /* Copy existing cleanup files. */
    for ( i = 0 ; i < imageargs->n_src_flush ; ++i, ++flush_index ) {
      /* Struct copy to set slot properties */
      data_src_flush[flush_index] = imageargs->data_src_flush[i] ;
    }

    if ( imageargs->data_src_flush )
      mm_free_with_header(mm_pool_temp, imageargs->data_src_flush) ;

    imageargs->data_src_flush = data_src_flush ;
    imageargs->n_src_flush += n ;
  }

  if ( (flags & IM_CLEANUP_CLOSE) != 0 ) {
    int32 close_index, i ;
    OBJECT *data_src_close = mm_alloc_with_header(mm_pool_temp,
                                                  (imageargs->n_src_close + n) * sizeof(OBJECT),
                                                  MM_ALLOC_CLASS_IMAGE) ;
    if ( data_src_close == NULL )
      return error_handler(VMERROR) ;

    /* Add new cleanup files first, in reverse order. The cleanup order is
       unspecified, but we might as well try to close and flush in reverse
       order of creation. */
    for ( close_index = 0 ; close_index < n ; ++close_index ) {
      /* Struct copy to set slot properties */
      data_src_close[close_index] = *stackindex(close_index, &temporarystack) ;
      HQASSERT(oType(data_src_close[close_index]) == OFILE,
               "Image DataSource close object is invalid") ;
    }

    /* Copy existing cleanup files. */
    for ( i = 0 ; i < imageargs->n_src_close ; ++i, ++close_index ) {
      /* Struct copy to set slot properties */
      data_src_close[close_index] = imageargs->data_src_close[i] ;
    }

    if ( imageargs->data_src_close )
      mm_free_with_header(mm_pool_temp, imageargs->data_src_close) ;

    imageargs->data_src_close = data_src_close ;
    imageargs->n_src_close += n ;
  }

  npop(n, &temporarystack) ;

  return TRUE ;
}

Bool im_datasource_currentfile(IMAGEARGS *imageargs)
{
  HQASSERT(imageargs->n_src_flush == 0 &&
           imageargs->data_src_flush == NULL &&
           imageargs->n_src_close == 0 &&
           imageargs->data_src_close == NULL &&
           !imageargs->no_currentfile,
           "Image data sources already prepared for flushing") ;

  if ( currfileCache != NULL || currfile_cache() ) {
    IMAGEARGS *maskargs = imageargs->maskargs ;
    int32 i, nprocs = imageargs->nprocs ;
    FILELIST *curr_file = oFile(*currfileCache) ;
    int32 tempstacksize = theStackSize(temporarystack) ;

    imageargs->no_currentfile = TRUE ;

    for ( i = 0 ; i < nprocs ; ++i ) {
      OBJECT *source = &imageargs->data_src[i] ;
      Bool needs_filter = FALSE ;

      if ( oType(*source) == OFILE ) {
        FILELIST *flptr ;
        /* Determine if this file, or its underlying file (if a filter),
           is being used as currentfile. */
        for ( flptr = oFile(*source) ; flptr ; flptr = theIUnderFile(flptr) ) {
          if ( flptr == curr_file ) { /* Yes, this is based on currentfile */
            needs_filter = TRUE ;
            break ;
          }
        }
      } else if ( oType(*source) == OARRAY || oType(*source) == OPACKEDARRAY ) {
        /* Procedure DataSources may include calls to currentfile in them, be
           conservative in the test. */
        needs_filter = TRUE ;
      } else {
        HQASSERT(oType(*source) == OSTRING || oType(*source) == OLONGSTRING,
                 "Image DataSource is unexpected type") ;
      }

      if ( needs_filter ) {
        corecontext_t *context = get_core_context_interp();
        OBJECT ofiltered = OBJECT_NOTVM_NOTHING ;
        FILELIST *subfilt ;
        Bool currentglobal ;
        Bool ok ;
        int32 bytes_to_read ;
        int32 image_bits_per_line = 0 ;
        int32 mask_bits_per_line = 0 ;
        int32 mask_height = 0 ;

        HQTRACE(!imageargs->no_currentfile,
                ("Multiple datasources in image reference currentfile.")) ;
        imageargs->no_currentfile = FALSE ;

        /* Calculate the number of bytes to limit this source to. Start with
           the basics; the number of bytes for a line of data in one
           component. */
        image_bits_per_line = imageargs->width * imageargs->bits_per_comp ;

        /* If image data is interleaved, then get the number of bits for
           all components. */
        if ( imageargs->nprocs == 1 )
          image_bits_per_line *= imageargs->ncomps ;

        /* Handle the mask data for interleaved masks; separate masks are
           handled by recursion into the maskargs. */
        switch ( imageargs->interleave ) {
        case INTERLEAVE_PER_SAMPLE:
          /* We should be looking at the top-level image, not the mask,
             because we don't recurse into it for sample interleaved. */
          HQASSERT(imageargs->imagetype == TypeImageMaskedImage ||
                   imageargs->imagetype == TypeImageAlphaImage,
                   "Interleave not consistent with image type") ;

          HQASSERT(maskargs, "No mask args for sample interleaved") ;

          /* Add the mask samples to the image bits, because they are
             interleaved before row padding. */
          image_bits_per_line += imageargs->width * imageargs->bits_per_comp ;
          break ;
        case INTERLEAVE_SCANLINE:
          /* We should be looking at the top-level image, not the mask,
             because we don't recurse into it for row interleaved. */
          HQASSERT(imageargs->imagetype == TypeImageMaskedImage ||
                   imageargs->imagetype == TypeImageAlphaImage,
                   "Interleave not consistent with image type") ;

          HQASSERT(maskargs, "No mask args for row interleaved") ;

          mask_bits_per_line = maskargs->width * maskargs->bits_per_comp ;
          mask_height = maskargs->height ;
          break ;
        }

        /* Get total bytes by padding the image and mask line widths to the
           next byte boundary, and multiplying them by the respective
           heights. */
        bytes_to_read = (imageargs->height * ((image_bits_per_line + 7) >> 3) +
                         mask_height * ((mask_bits_per_line + 7) >> 3)) ;

        /** \todo ajcd 2008-03-30: In trunk, merge this into
            filter_layer_object() by using dict arg, and ditching the
            requirement that the input to that function is a file. */

        /* Create this filter in local memory, it's closed at the end of the
           image anyway. */
        currentglobal = setglallocmode(context, FALSE) ;

        /* Layer a SubFileDecode of the appropriate size on top of this
           DataSource, and mark that this source should be flushed to
           EOF when the image is freed. The SubFileDecode created should
           have CloseSource false (this is the default). */
        subfilt = filter_standard_find(NAME_AND_LENGTH("SubFileDecode")) ;
        HQASSERT(subfilt != NULL, "Lost SubFileDecode") ;


        oInteger(inewobj) = bytes_to_read ;
        theLen(snewobj) = 0;
        oString(snewobj) = NULL;
        ok = (push3(source, &inewobj, &snewobj, &temporarystack) &&
              filter_create_object(subfilt, &ofiltered, NULL, &temporarystack)) ;

        setglallocmode(context, currentglobal) ;

        /* Push the new filtered object onto temporarystack, until we know
           how many files need filtering. */
        if ( !ok || !push(&ofiltered, &temporarystack) ) {
          npop(theStackSize(temporarystack) - tempstacksize, &temporarystack) ;
          return FALSE ;
        }

        /* Replace DataSource with filtered source. */
        Copy(source, &ofiltered) ;
      }
    }

    /* If we pushed anything on temporarystack, it was the filter objects
       that we've just created. We want to flush and close them at the end
       of processing the image, so add them to both cleanup lists. */
    if ( theStackSize(temporarystack) > tempstacksize ) {
      int32 n_src_dispose = theStackSize(temporarystack) - tempstacksize ;
      if ( !im_cleanup_files(imageargs, n_src_dispose, IM_CLEANUP_FLUSH|IM_CLEANUP_CLOSE) ) {
        npop(n_src_dispose, &temporarystack) ;
        return FALSE ;
      }
    }

    if ( maskargs ) {
      /* Chroma, row, and scanline interleave don't have separate data
         sources. */
      if ( imageargs->interleave == INTERLEAVE_SEPARATE ) {
        if ( !im_datasource_currentfile(maskargs) )
          return FALSE ;

        /* Combine mask's currentfile flag into image's, for easy access. */
        imageargs->no_currentfile &= maskargs->no_currentfile ;
      }
    }
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
typedef struct {
  uint8 *data ;
} char_image_state ;

static Bool char_image_begin(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                             void *data)
{
  char_image_state *state = data ;
  int32 width, height ;

  HQASSERT(imageargs, "No image args") ;
  HQASSERT(imagedata, "No image data") ;
  HQASSERT(state, "No state") ;
  state->data = NULL ;

  if ( imagedata->degenerate || /* Ignore degenerate image */
       char_doing_charpath() )
    return TRUE ;

  width = imageargs->width;
  height = imageargs->height;

  HQASSERT( imageargs->colorType == GSC_FILL,
            "Strange colorType in char image" ) ;

  if ( pdfout_enabled() &&
       ! pdfout_beginimage( get_core_context_interp()->pdfout_h , GSC_FILL ,
                            imageargs , NULL ))
    return FALSE ;

  switch ( IDLOM_BEGINIMAGE( GSC_FILL ,
                             imageargs ,
                             FALSE , /* do not want to do any color conversion */
                             0 , /* out ncomps n/a */
                             0 , /* out16 n/a */
                             NULL /* ndecodes n/a */ ) ) {
  case NAME_false:   /* Error in callback or in setup */
    return FALSE ;
  case NAME_Discard: /* Single-callback form wants to throw it away */
    /* Pretend image is degenerate, so data and end callback are ignored. */
    imagedata->degenerate = TRUE ;
    return TRUE ;
  case NAME_Add:     /* Single-callback form wants to add it without further ado */
    /* Disable HDLT until the end of im_common, so image data and End
       callbacks won't be called. */
    theIdlomState(*gstateptr) = HDLT_DISABLED ;
    break ;
  default:
    HQFAIL("HDLT image Begin callback returned unexpected value") ;
    /*@fallthrough@*/
  case NAME_End:     /* Defer what to do to End callback */
    break ;
  }

  /* This is a DEVICE_SETG_NORMAL because we can only be doing an imagemask if
   * we are here, and imagemasks can use pattern screens sensibly.
   */
  if ( !DEVICE_SETG(imagedata->page, GSC_FILL, DEVICE_SETG_NORMAL) )
    return FALSE ;

  /* IDLOM needs to see all the data before it gets blt'ed, so we've got to
     buffer it away first. */
  state->data = (uint8*)mm_alloc_with_header(mm_pool_temp,
                                             imagedata->bytes_in_image_line * height,
                                             MM_ALLOC_CLASS_IMAGE_DATA);
  if ( state->data == NULL )
    return error_handler(VMERROR);

  return TRUE ;
}

static Bool char_image_end(IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                           void *data, Bool abort)
{
  char_image_state *state = data ;
  Bool result = TRUE ;

  HQASSERT(state, "No state") ;

  if ( imagedata->degenerate || /* Ignore degenerate image */
       char_doing_charpath() )
    return TRUE ;

  switch ( IDLOM_ENDIMAGE( GSC_FILL , imageargs )) {
  case NAME_false:                /* PS error in IDLOM callbacks */
    result = FALSE;
    break ;
  case NAME_Discard:                /* just pretending */
    break;
  default:                        /* only add, for now */
    HQFAIL("HDLT image End call returned unexpected value") ;
    /*@fallthrough@*/
  case NAME_Add:
    if ( imagedata->image_lines > 0 && !degenerateClipping && !abort ) {
      charcontext_t *context = char_current_context();
      render_blit_t *rb = context->rb;
      ibbox_t imsbbox ;
      bbox_store(&imsbbox, 0, 0, imagedata->rwidth - 1, imagedata->image_lines - 1) ;
      char_image_render(rb, imagedata, &imsbbox, state->data,
                        (uint8)(imageargs->polarity ? 0xff : 0)) ;
    }
    break ;
  }

  if ( pdfout_enabled() &&
       ! pdfout_endimage(get_core_context_interp()->pdfout_h,
                         imagedata->image_lines, imageargs))
    result = FALSE ;

  mm_free_with_header(mm_pool_temp, state->data);

  return result ;
}

static Bool char_image_data(int32 data_type,
                            IMAGEARGS *imageargs, IMAGEDATA *imagedata,
                            uint32 lines, void *data)
{
  char_image_state *state = data ;
  int32 length, offset ;

  UNUSED_PARAM(int32, data_type) ;

  HQASSERT(state, "No state") ;

  if ( imagedata->degenerate || /* Ignore degenerate image */
       char_doing_charpath() )
    return TRUE ;

  length = imagedata->bytes_in_image_line * lines ;
  offset = imagedata->bytes_in_image_line * imagedata->image_lines ;

  if (pdfout_enabled())
    /* If we are dealing with image data, PDFout needs this hook.  */
    if (! pdfout_imagedata( get_core_context_interp()->pdfout_h , 1 ,
                            imagedata->imb->obuf , length, FALSE ))
      return FALSE ;

  if ( ! IDLOM_IMAGEDATA(GSC_FILL, imageargs,
                         1, imagedata->imb->obuf, length,
                         1, imagedata->imb->obuf[0], length,
                         imagedata->out16) )
    return FALSE ;                /* PS error in IDLOM callbacks */

  /* copy it into the buffer */
  HqMemCpy(state->data + offset, imagedata->imb->obuf[0], length);

  return TRUE ;
}

static void char_image_early_end(IMAGEARGS *imageargs,
                                 IMAGEDATA *imagedata, void *data)
{
  UNUSED_PARAM(IMAGEARGS *, imageargs);
  UNUSED_PARAM(IMAGEDATA *, imagedata);
  UNUSED_PARAM(void *, data);
}

static void char_image_stripe(IMAGEARGS *imageargs,
                                 IMAGEDATA *imagedata, void *data)
{
  UNUSED_PARAM(IMAGEARGS *, imageargs);
  UNUSED_PARAM(IMAGEDATA *, imagedata);
  UNUSED_PARAM(void *, data);
}

static void char_image_clip_optimize( IMAGEARGS *imageargs,
                                      IMAGEDATA *imagedata,
                                      void *data, int32 *iy1, int32 *iy2,
                                      int32 *my1, int32 *my2)
{
  UNUSED_PARAM(IMAGEARGS *, imageargs);
  UNUSED_PARAM(IMAGEDATA *, imagedata);
  UNUSED_PARAM(void *, data);
  UNUSED_PARAM(int32 *, iy1);
  UNUSED_PARAM(int32 *, iy2);
  UNUSED_PARAM(int32 *, my1);
  UNUSED_PARAM(int32 *, my2);
}

Bool docharimage(DL_STATE *page, STACK *stack, IMAGEARGS *imageargs)
{
  char_image_state state ;

  return im_common(page, stack, imageargs, &state,
                   char_image_begin, char_image_end, char_image_data,
                   char_image_early_end, char_image_stripe,
                   char_image_clip_optimize) ;
}

/*
Log stripped */
