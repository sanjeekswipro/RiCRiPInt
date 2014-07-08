/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!src:imagecontext.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image context implementation, to extract tagged information from images.
 *
 * The Image Context interface is a way of extracting tagged information and
 * data from image streams, for use when there is no PS-like image dictionary
 * supplied (e.g. SVG, MMCF). The Image Context interface will be used
 * eventually to rip images directly (i.e., HqnJpeg will be rewritten to use
 * this interface). Breaking the processing of images into distinct phases
 * allows more flexible handling of images, allowing inspection and
 * configuration before execution. The Image Context interface comes in two
 * flavours: the C interface and the PS interface.
 *
 * An example of Image Context usage in PostScript:-
 *
 * \code
 *  (image) (r) file dup <<
 *    /ImageFormat [ /PNG /JPEG ]
 *  >> imagecontextopen {
 *    pop % Don't care about image type for this example
 *    10 dict begin
 *    dup {
 *      def currentdict length 9 eq
 *    } [ % all of these filled in by imagecontextinfo
 *      /Width /Height /DataSource /Decode /ColorSpace /ImageMatrix
 *      /BitsPerComponent /ImageType /XResolution /YResolution
 *    ] imagecontextinfo
 *    <<
 *      /HWResolution [XResolution YResolution]
 *      /PageSize [
 *        Width XResolution div 72 mul
 *        Height YResolution div 72 mul
 *      ]
 *    >> setpagedevice
 *    ColorSpace setcolorspace
 *    currentdict
 *  end image showpage
 *  imagecontextclose closefile
 * \endcode
 *
 * Note: it is still necessary to call closefile on the image file.
 *
 * imagecontextopen (PS) / imagecontext_open (C)
 * ----------------------------------------
 * Reads the image signature and detects the encoding format.
 * Layers a decoding filter on the image file.
 *
 * imagecontextclose (PS) / imagecontext_close (C)
 * ----------------------------------------
 * Frees decode filter and other allocated memory.
 * ImageContexts are also subject to save and restore.
 *
 * imagecontextinfo (PS) / imagecontext_info (C)
 * ----------------------------------------
 * Extracts tag values from image filter, returning them to PostScript/C.
 *
 * imagecontext_args (C)
 * ----------------------------------------
 * Builds an IMAGEARGS structure ready to dispatch an image.
 *
 * The C interface version uses opaque references to ImageContext structures
 * to refer to image contexts, instead of dictionaries in the PS version.
 *
 * =============================================================================
 */

#include "core.h"
#include "coreinit.h"

#include "imagecontext.h"
#include "swerrors.h"
#include "mm.h"
#include "gcscan.h"
#include "gs_color.h"       /* SPACE_DeviceGray */
#include "images.h"
#include "objstack.h"
#include "dicthash.h"
#include "stacks.h"
#include "namedef_.h"
#include "dl_image.h"
#include "graphics.h"
#include "fileio.h"
#include "filterinfo.h"
#include "pngfilter.h"
#include "jpeg2000.h"
#include "tifffilter.h"
#include "wmpfilter.h"
#include "dct.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "swmemory.h"
#include "control.h" /* interpreter */
#include "gschead.h" /* getcolorspacesizeandtype */
#include "utils.h"   /* is_matrix_noerror */
#include "gstack.h"  /* gstateptr */
#include "surfacet.h" /* SURFACE_* */


#define IMAGE_CONTEXT_NAME "ImageContext"

/** \brief The image context structure. */
struct ImageContext {
  int32 id;          /* A PS integer; C interface stores IMAGE_CONTEXT_ID_UNUSED here. */
  int32 format;      /* Name number of format. */

  int32 savelevel;

  IMAGEARGS imageargs;
  IMAGEARGS maskargs;

  OBJECT filestream;  /* gc scanned */
  OBJECT imagefilter; /* gc scanned */
  OBJECT rsdfilter;   /* gc scanned */

  ImageContext* prev;
  ImageContext* next;

  OBJECT_NAME_MEMBER
};

/** \brief An image context ID value that is never used. Image context ids
   are required for the PS interface and defined as PS integers (value zero
   is reserved for wrap protection). The C interface does not require image
   context ids (it uses opaque struct references instead) and stores
   IMAGE_CONTEXT_ID_UNUSED in the image context structure. */
#define IMAGE_CONTEXT_ID_UNUSED 0

/** \brief The next image context ID to be allocated. */
static int32 next_context_id = IMAGE_CONTEXT_ID_UNUSED + 1;

/** \brief Global list of active image contexts. */
static ImageContext* imagecontext_list = NULL;

/** \brief Root for image context GC scanning. */
static mps_root_t imagecontext_root = NULL;

static void init_C_globals_imagecontext(void)
{
  next_context_id = IMAGE_CONTEXT_ID_UNUSED + 1;
  imagecontext_list = NULL;
  imagecontext_root = NULL;
}

/* Utility functions. */

/** \brief Function to scan image contexts for PostScript object references. */
static mps_res_t MPS_CALL imagecontext_scan_root(mps_ss_t ss, void *p, size_t s)
{
  ImageContext* context;

  UNUSED_PARAM(void*, p);
  UNUSED_PARAM(size_t, s);

  for (context = imagecontext_list; context; context = context->next) {
    mps_res_t res;
    res = ps_scan_field(ss, & context->filestream);
    if (res != MPS_RES_OK)
      return res ;
    res = ps_scan_field(ss, & context->imagefilter);
    if (res != MPS_RES_OK)
      return res ;
    res = ps_scan_field(ss, & context->rsdfilter);
    if (res != MPS_RES_OK)
      return res ;
  }

  return MPS_RES_OK;
}

static Bool imagecontext_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create(& imagecontext_root, mm_arena, mps_rank_exact(),
                       0, imagecontext_scan_root, NULL, 0) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}

static void imagecontext_finish(void)
{
  mps_root_destroy(imagecontext_root);
}

void imagecontext_C_globals(core_init_fns *fns)
{
  init_C_globals_imagecontext() ;

  fns->swstart = imagecontext_swstart ;
  fns->finish = imagecontext_finish ;
}

static struct {
  int32 name ;
  uint8 *filtername ;
  uint32 filternamelen ;
  Bool (*test)(FILELIST *file) ;
} image_filters[] = {
  { NAME_PNG, NAME_AND_LENGTH("PNGDecode"), png_signature_test },
  { NAME_JFIF, NAME_AND_LENGTH("DCTDecode"), jfif_signature_test },
  { NAME_JPEG, NAME_AND_LENGTH("DCTDecode"), jpeg_signature_test },
  { NAME_JPEG2000, NAME_AND_LENGTH("JPXDecode"), jpeg2000_signature_test },
  { NAME_TIFF, NAME_AND_LENGTH("TIFFDecode"), tiff_signature_test },
  { NAME_WMPHOTO, NAME_AND_LENGTH("WMPDecode"), wmp_signature_test },
#if 0 /* NYI */
  { NAME_JBig, NAME_AND_LENGTH("JBigDecode"), jbig_signature_test },
  { NAME_CCITT, NAME_AND_LENGTH("CCITTFaxDecode"), ccitt_signature_test },
#endif
} ;

/** \brief Scan a filestream for image signatures.  If the stream is not a
    recognised image type, the stream data buffer is left positioned with no
    bytes read. */
static Bool scan_image(OBJECT *stream,
                       OBJECT *formats,
                       uint32 *image_filter_index)
{
  uint32 nformats ;
  OBJECT oarray = OBJECT_NOTVM_NOTHING, oany[6] ;

  HQASSERT(NUM_ARRAY_ITEMS(image_filters) == NUM_ARRAY_ITEMS(oany),
           "Object array length does not match format filter array") ;

  /* Indicates no match found. */
  *image_filter_index = NUM_ARRAY_ITEMS(image_filters) ;

  /* Coerce the image formats to an array. */
  if ( formats == NULL || oType(*formats) == ONULL ) {
    uint32 i ;

    for ( i = 0 ; i < NUM_ARRAY_ITEMS(oany) ; ++i )
      object_store_name(object_slot_notvm(&oany[i]),
                        image_filters[i].name, LITERAL) ;

    theTags(oarray) = OARRAY|LITERAL|READ_ONLY ;
    theLen(oarray) = NUM_ARRAY_ITEMS(oany) ;
    oArray(oarray) = oany ;
    formats = &oarray ;
  } else if ( oType(*formats) == ONAME ) {
    theTags(oarray) = OARRAY|LITERAL|READ_ONLY ;
    theLen(oarray) = 1 ;
    oArray(oarray) = formats ;
    formats = &oarray ;
  } else if ( oType(*formats) != OARRAY &&
              oType(*formats) != OPACKEDARRAY ) {
    /* Formats is not a null, a name, or an array */
    return error_handler(TYPECHECK) ;
  }

  /* Check the signature of each image format until there is a match. */
  for (  nformats = theLen(*formats), formats = oArray(*formats) ;
         nformats > 0 ;
         --nformats, ++formats ) {
    int32 name ;
    uint32 i ;

    if ( oType(*formats) != ONAME )
      return error_handler(TYPECHECK) ;

    name = oNameNumber(*formats) ;

    for ( i = 0 ; i < NUM_ARRAY_ITEMS(image_filters) ; ++i ) {
      if ( name == image_filters[i].name ) {
        FILELIST *flptr = oFile(*stream) ;

        if ( (*image_filters[i].test)(flptr) ) {
          /* Signature matches, this is the right type. */
          *image_filter_index = i ;
          return TRUE ;
        }
      }
    }
  }

  /* Image does not conform to one of the supplied image formats. This is not
     an error; false is returned to PostScript. */
  return TRUE ;
}

static Bool image_filter_layer(ImageContext *context,
                               uint32 image_filter_index,
                               OBJECT *stream,
                               int32 subimage)
{
  OBJECT argdict = OBJECT_NOTVM_NOTHING ;
  OBJECT *args = NULL ;

  /* Create dictionary for args; need /SubImage n if not zero. */
  if ( subimage != 0 ) {
    corecontext_t *context = get_core_context_interp();
    OBJECT key = OBJECT_NOTVM_NAME(NAME_SubImage, LITERAL),
      value = OBJECT_NOTVM_NOTHING ;
    Bool currentglobal;
    Bool result ;

    currentglobal = setglallocmode(context, oGlobalValue(*stream)) ;

    result = ps_dictionary(&argdict, 2) ;

    setglallocmode(context, currentglobal) ;

    if ( !result )
      return FALSE ;

    /* Insert SubImage key, to indicate which IFD/subimage to use. */
    object_store_integer(&value, subimage) ;
    if ( !insert_hash(&argdict, &key, &value) )
      return FALSE ;

    /* CloseSource is set manually later on to avoid creating a dict just for this. */

    args = &argdict ;
  }

  /* Signature matches, this is the right type, so add the image
     filter on top of the RSD. */
  if ( !filter_layer_object(stream,
                            image_filters[image_filter_index].filtername,
                            image_filters[image_filter_index].filternamelen,
                            args,
                            &context->imagefilter) )
    return FALSE;

  /* Do not set CloseTarget on the image filter; the underlying stream
     may be a RSD filter which is required to be left open (e.g. for a
     multi-part TIFF image). */

  return TRUE ;
}

/* ==========================================================================
   C interface
   ========================================================================== */
Bool imagecontext_open(
  /*@notnull@*/ /*@in@*/  OBJECT *stream,
  /*@notnull@*/ /*@in@*/  OBJECT *formats,
                          int32 subimage,
  /*@notnull@*/ /*@out@*/ ImageContext **p_context)
{
  Bool result = FALSE;
  uint32 image_filter_index ;
  ImageContext* context;
  FILELIST *flptr ;
  Hq32x2 filepos ;

  HQASSERT(stream, "No image for context");
  HQASSERT(oType(*stream) == OFILE, "Image stream is not an OFILE");
  HQASSERT(subimage >= 0, "Invalid subimage specified") ;
  HQASSERT(p_context, "p_context is null");

  *p_context = NULL ;

  /* Scan the stream looking for a recognised signature by peeking into data
     buffer without changing the file ptr/count.  This needs to be done
     before layering an RSD filter (as that will read the stream to EOD). */
  if ( !scan_image(stream, formats, &image_filter_index) )
    return FALSE ;

  if ( image_filter_index == NUM_ARRAY_ITEMS(image_filters) )
    /* Not a recognised image type from the formats list. */
    return TRUE ;

  context = mm_alloc(mm_pool_temp, sizeof(ImageContext), MM_ALLOC_CLASS_IMAGE_CONTEXT);
  if (context == NULL)
    return error_handler(VMERROR);
#define return DO_NOT_return_GO_TO_imagecontext_open_cleanup_INSTEAD!

  HqMemZero(context, sizeof(ImageContext));

  /* A valid context id is only required when using the PostScript interface.
     The C interface uses an opaque reference to the image context. */
  context->id = IMAGE_CONTEXT_ID_UNUSED;

  context->format = image_filters[image_filter_index].name ;
  context->imagefilter = context->rsdfilter = context->filestream =
    onull ; /* Struct copy to set slot properties */
  OCopy(context->filestream, *stream) ;

  /* Link maskargs into imageargs. */
  context->imageargs.maskargs = & context->maskargs;
  context->maskargs.maskargs = NULL;

  /* Image contexts can be restored away but should normally be closed
     explicitly. */
  context->savelevel = get_core_context_interp()->savelevel;

  NAME_OBJECT(context, IMAGE_CONTEXT_NAME);

  /* See if the image stream is seekable; if not, buffer in an RSD. The
     stream MUST be seekable since opening an image context means breaking
     the image process up into separate stages (open, exec, close, etc).
     Also, the stream must be seekable to allow scanning for imageargs when
     there is no image dictionary. */
  flptr = oFile(*stream) ;
  if ( !file_seekable(flptr) ||
       (*theIMyFilePos(flptr))(flptr, &filepos) == EOF ||
       !Hq32x2IsZero(&filepos) ) {
    if ( !filter_layer_object(stream,
                              NAME_AND_LENGTH("ReusableStreamDecode"),
                              NULL,
                              &context->rsdfilter) )
      goto imagecontext_open_cleanup;

    stream = & context->rsdfilter;
  }

  if ( !image_filter_layer(context, image_filter_index, stream, subimage) )
    goto imagecontext_open_cleanup;

  /* Successfully opened the image context, add it to the image context list. */
  context->prev = NULL;
  context->next = imagecontext_list;
  if (imagecontext_list)
    imagecontext_list->prev = context;
  imagecontext_list = context;

  result = TRUE;
  *p_context = context;

 imagecontext_open_cleanup:
  if (! result)
    (void)imagecontext_close(& context);

#undef return
  return result;
}

Bool imagecontext_info(ImageContext* context,
                       imagefilter_match_t *match)
{
  FILELIST *flptr ;
  Hq32x2 filepos ;

  VERIFY_OBJECT(context, IMAGE_CONTEXT_NAME);

  if ( match == NULL ) /* Nothing to match? That's OK. */
    return TRUE ;

  flptr = oFile(context->imagefilter) ;
  if ( !theIFilterDecodeInfo(flptr)(flptr, match) )
    return theIFileLastError(flptr)(flptr) ;

  if ( !isIOpenFileFilter(&context->imagefilter, oFile(context->imagefilter)) ) {
    /* The decode info must have consumed all the data and
       the image filter has been closed. */
    return error_handler(IOERROR) ;
  }

  Hq32x2FromUint32(&filepos, 0u) ;

  /* If an RSD filter was layered on top of the incoming stream, the RSD
     will be automatically rewound by FilterSetPos.  If an RSD was not
     necessary (because the file stream was seekable) then we still need to
     seek the file stream back to zero before rewinding the image filter. */
  if ( oType(context->rsdfilter) != OFILE ) {
    FILELIST *uflptr = oFile(context->filestream) ;
    HQASSERT(!isIFilter(uflptr) || isIRSDFilter(uflptr), "uflptr should be a real file or RSD") ;
    if ( !isIOpenFileFilter(&context->filestream, oFile(context->filestream)) ) {
      /* The decode info must have consumed all the data and
         the file stream has been closed. */
      return error_handler(IOERROR) ;
    }
    if ( theIMyResetFile(uflptr)(uflptr) == EOF ||
         theIMySetFilePos(uflptr)(uflptr, &filepos) == EOF ) {
      return theIFileLastError(uflptr)(uflptr) ;
    }
  }

  /* Temporarily set the rewindable flag to enable the image filter to be
     rewound after decode info.  We do not leave the rewindable flag set
     longer than necessary to ensure the filters and file stream are closed
     and available for re-use as soon as the image data has been read.  The
     rewindable method of re-initialising the image filter ensures the
     DataSource in the image dictionary stays the same. */
  SetIRewindableFlag(oFile(context->imagefilter)) ;

  if ( theIMyResetFile(flptr)(flptr) == EOF ||
       theIMySetFilePos(flptr)(flptr, &filepos) == EOF ) {
    ClearIRewindableFlag(oFile(context->imagefilter)) ;
    return theIFileLastError(flptr)(flptr) ;
  }
  ClearIRewindableFlag(oFile(context->imagefilter)) ;

  return TRUE ;
}

/** \brief Helper function to fill in imagecontext args structures from
    match list supplied by imagecontext_args(). */
static int32 imageargsinfo(imagefilter_match_t *match, OBJECT *value)
{
  ImageContext *context ;
  IMAGEARGS *args, *mask ;
  int32 i ;

  HQASSERT(value, "No value for image filter decode match") ;
  HQASSERT(match, "No tag for image filter decode match") ;
  context = match->data ;
  VERIFY_OBJECT(context, IMAGE_CONTEXT_NAME) ;
  args = &context->imageargs ;
  mask = &context->maskargs ;

  HQASSERT(oType(match->key) == ONAME, "Match tag for image context is wrong type") ;
  switch ( oNameNumber(match->key) ) {
  case NAME_Width:
    HQASSERT(oType(*value) == OINTEGER, "Image width should be an integer") ;
    args->width = oInteger(*value) ;
    break ;
  case NAME_Height:
    HQASSERT(oType(*value) == OINTEGER, "Image height should be an integer") ;
    args->height = oInteger(*value) ;
    break ;
  case NAME_BitsPerComponent:
    HQASSERT(oType(*value) == OINTEGER, "Bits per component should be an integer") ;
    args->bits_per_comp = oInteger(*value) ;
    break ;
  case NAME_ColorSpace:
    HQASSERT(oType(*value) == ONAME, "Color space object is not a name") ;
    if ( !gsc_getcolorspacesizeandtype(gstateptr->colorInfo, value,
                                       &args->image_color_space, &args->ncomps) )
      return IMAGEFILTER_MATCH_FAIL ;
    /** \todo This is dangerous for complex colourspaces. */
    HQASSERT(!isPSCompObj(*value),
             "Copying complex colorspace from filter info source") ;
    Copy(&args->image_color_object, value) ;
    break ;
  case NAME_PreMult:
    HQASSERT(oType(*value) == OBOOLEAN, "PreMult should be a boolean") ;
    args->premult_alpha = mask->premult_alpha = oBool(*value) ;
    break ;
  case NAME_ImageType:
    HQASSERT(oType(*value) == OINTEGER, "Image type is not an integer") ;
    switch ( oInteger(*value) ) {
    case 1:
      args->imagetype = TypeImageImage ;
      break ;
    case 3:
      args->imagetype = TypeImageMaskedImage ;
      mask->imagetype = TypeImageMaskedMask ;
      break ;
    case 12:
      args->imagetype = TypeImageAlphaImage ;
      mask->imagetype = TypeImageAlphaAlpha ;
      break ;
    default:
      HQFAIL("Invalid image type") ;
      (void)error_handler(RANGECHECK) ;
      return IMAGEFILTER_MATCH_FAIL ;
    }
    break ;
  case NAME_Decode:
    HQASSERT(oType(*value) == OARRAY, "Decode is not an array") ;
    HQASSERT(theLen(*value) > 0, "Decode array has no elements") ;
    HQASSERT((theLen(*value) & 1) == 0, "Decode array has odd number of elements") ;
    HQASSERT(args->ncomps == 0 || args->ncomps == theLen(*value) / 2,
             "Mismatch in number of components") ;
    args->ncomps = theLen(*value) >> 1 ;
    if ( !alloc_image_decode(args, args->ncomps) )
      return IMAGEFILTER_MATCH_FAIL;

    for (i = 0, value = oArray(*value) ; i < args->ncomps * 2; ++i, ++value ) {
      if ( !object_get_real(value, &args->decode[i]) )
        return IMAGEFILTER_MATCH_FAIL;
    }
    break ;
  case NAME_DataSource:
    /* Use the temporarystack as a place to dump the data sources. The
       caller keeps count of the items to remove from the temp stack. */
    if ( oType(*value) == OARRAY || oType(*value) == OPACKEDARRAY ) {
      args->nprocs = theLen(*value) ;
      value = oArray(*value) ;
    } else {
      args->nprocs = 1 ;
    }

    if ( !alloc_image_datasrc(args, args->nprocs) )
      return IMAGEFILTER_MATCH_FAIL ;

    for ( i = 0 ; i < args->nprocs ; ++i ) {
      OCopy(args->data_src[i], value[i]) ;
    }

    break ;
  case NAME_InterleaveType:
    HQASSERT(oType(*value) == OINTEGER, "Image type is not an integer") ;
    switch ( oInteger(*value) ) {
    case 0:
      args->interleave = INTERLEAVE_NONE;
      break ;
    case 1:
      args->interleave = INTERLEAVE_PER_SAMPLE;
      break ;
    case 2:
      args->interleave = INTERLEAVE_SCANLINE;
      break ;
    case 3:
      /* Mask provides its own data source */
      args->interleave = INTERLEAVE_SEPARATE;
      HQFAIL("No provision for mask having separate data source") ;
      break ;
    default:
      HQFAIL("Invalid interleave type") ;
      (void)error_handler(RANGECHECK) ;
      return IMAGEFILTER_MATCH_FAIL ;
    }
    mask->interleave = args->interleave ;
    break ;
  case NAME_ImageMatrix:
    if ( !is_matrix_noerror(value, &args->omatrix) ) {
      HQFAIL("Invalid image matrix") ;
      (void)error_handler(RANGECHECK) ;
      return IMAGEFILTER_MATCH_FAIL ;
    }
    break ;
  default:
    HQFAIL("Image filter decode info returned unexpected value") ;
    return IMAGEFILTER_MATCH_FAIL ;
  }

  /* Have we got enough to exit early? */
  /** \todo @@@ TODO FIXME ajcd 2005-03-19: not sufficient any more. We need
      a better test for this. */
  if ( args->width != 0 && args->height != 0 &&
       args->nprocs != 0 && args->ncomps != 0 && args->bits_per_comp != 0 &&
       oType(args->image_color_object) == ONAME &&
       args->data_src != NULL &&
       args->imagetype != -1 )
    return IMAGEFILTER_MATCH_DONE ;

  return IMAGEFILTER_MATCH_MORE ;
}

Bool imagecontext_args(
  /*@notnull@*/ /*@in@*/  ImageContext* context,
  /*@notnull@*/ /*@out@*/ IMAGEARGS **args)
{
  imagefilter_match_t match_args[10], *match = NULL ;
  IMAGEARGS *imageargs ;
  int32 i ;
  Bool result ;

  static int32 match_names[] = {
    NAME_BitsPerComponent, NAME_ColorSpace, NAME_DataSource, NAME_Decode,
    NAME_Height, NAME_ImageMatrix, NAME_ImageType, NAME_InterleaveType,
    NAME_PreMult, NAME_Width
  } ;

  VERIFY_OBJECT(context, IMAGE_CONTEXT_NAME) ;
  imageargs = &context->imageargs ;

  init_image_args(imageargs, GSC_IMAGE) ;

  HQASSERT(NUM_ARRAY_ITEMS(match_names) == NUM_ARRAY_ITEMS(match_args),
           "Name match not same length as object match") ;

  /* These are the match fields required to fill in an IMAGEARGS struct. */
  for ( i = 0 ; i < NUM_ARRAY_ITEMS(match_names) ; ++i ) {
    object_store_name(object_slot_notvm(&match_args[i].key),
                      match_names[i], LITERAL) ;
    match_args[i].callback = imageargsinfo ;
    match_args[i].data = context ;
    match_args[i].next = match ;
    match = &match_args[i] ;
  }

  imageargs->image_color_object =
    imageargs->colorspace = onull ; /* Struct copy to set slot properties */
  imageargs->imageop = NAME_image ;
  imageargs->imagetype = -1;
  imageargs->interleave = INTERLEAVE_NONE;

  result = imagecontext_info(context, match) ;

  if ( !result )
    return FALSE ;

  /** \todo @@@ TODO FIXME ajcd 2005-03-19: not sufficient any more. We need
      a better test for this. */
  if ( imageargs->imagetype == -1 ||
       imageargs->width == 0 || imageargs->height == 0 ||
       imageargs->bits_per_comp == 0 || imageargs->ncomps == 0 ||
       imageargs->nprocs == 0 ||
       oType(imageargs->image_color_object) == ONULL ||
       imageargs->data_src == NULL )
    return error_handler(UNDEFINED) ;

  imageargs->lines_per_block = imageargs->height;

  if ( imageargs->imagetype == TypeImageAlphaImage ||
       imageargs->imagetype == TypeImageMaskedImage ) {
    IMAGEARGS *maskargs = &context->maskargs ;

    HQASSERT((imageargs->imagetype == TypeImageAlphaImage &&
              maskargs->imagetype == TypeImageAlphaAlpha) ||
             (imageargs->imagetype == TypeImageMaskedImage &&
              maskargs->imagetype == TypeImageMaskedMask),
             "Mask type not consistent with image type") ;
    HQASSERT(imageargs->interleave != INTERLEAVE_NONE,
             "Mask args must be interleaved in one data source") ;

    init_image_args(maskargs, GSC_FILL) ;

    /* Ensure input is processed one line at a time to allow
       alpha splitting to be performed on the input buffer */
    imageargs->lines_per_block = maskargs->lines_per_block = 1;

    maskargs->width = imageargs->width;
    maskargs->height = imageargs->height;
    maskargs->bits_per_comp = imageargs->bits_per_comp;
    maskargs->ncomps = 1;
    maskargs->nprocs = 1;
    maskargs->image_color_space = SPACE_notset;
    maskargs->image_color_object = onull ; /* Struct copy to set slot properties */
    maskargs->omatrix = imageargs->omatrix;
    maskargs->data_src = NULL; /* alpha is interleaved */
    maskargs->imageop = imageargs->imageop;
    maskargs->interpolate = imageargs->interpolate;
    maskargs->interleave = imageargs->interleave;
    /* Allocate decode arrays for alpha channel. */
    if ( ! alloc_image_decode(maskargs, 1))
      return FALSE;
    maskargs->decode[0] = 0.0f;
    maskargs->decode[1] = 1.0f;
    maskargs->colorspace = onull ; /* Struct copy to set slot properties */

    maskargs->contoneMask = TRUE;
    maskargs->premult_alpha = imageargs->premult_alpha;

    imageargs->maskargs = maskargs;
  } else {
    imageargs->maskargs = NULL;
  }

  set_image_order(imageargs) ;

  *args = imageargs ;

  return TRUE ;
}

/* Unhook the image context from the list and free the bits off. */
Bool imagecontext_close(
  /*@notnull@*/ /*@in@*/ ImageContext **p_context)
{
  Bool result = TRUE;
  ImageContext* context;

  HQASSERT(p_context, "image context ptr is missing");
  context = *p_context;
  VERIFY_OBJECT(context, IMAGE_CONTEXT_NAME);

  if (context->prev)
    context->prev->next = context->next;
  else
    imagecontext_list = context->next;
  if (context->next)
    context->next->prev = context->prev;

  free_image_args(& context->imageargs);

  /* Normally the image filter, RSD, underlying file will have been
     closed already after reading the image data. */
  if (oType(context->imagefilter) == OFILE &&
      isIOpenFileFilter(&context->imagefilter, oFile(context->imagefilter))) {
    if ( !file_close(&context->imagefilter) )
      result = FALSE ;
  }
  if (oType(context->rsdfilter) == OFILE &&
      isIOpenFileFilter(&context->rsdfilter, oFile(context->rsdfilter))) {
    if ( !file_close(&context->rsdfilter) )
      result = FALSE ;
  }

  UNNAME_OBJECT(context);
  mm_free(mm_pool_temp, context, sizeof(ImageContext));
  *p_context = NULL;
  return result;
}

/* In the event of an error, imagecontext_restore carries on trying to close
   any other contexts being restored to try to maintain a sane state. */
Bool imagecontext_restore(int32 savelevel)
{
  Bool result = TRUE;
  ImageContext* context = imagecontext_list;

  while (context) {
    ImageContext* next = context->next;

    if (context->savelevel > savelevel)
      if ( !imagecontext_close(& context) )
        result = FALSE ;

    context = next;
  }

  return result;
}


/* ==========================================================================
   PostScript interface
   ========================================================================== */

/** \brief Get the context id from an object; validate the context id and
    find the matching image context structure. */
static Bool get_image_context(OBJECT *theo, ImageContext **p_context)
{
  ImageContext* context = imagecontext_list;
  int32 id;

  *p_context = NULL;

  if ( oType(*theo) != OINTEGER )
    return error_handler(TYPECHECK);

  id = oInteger(*theo);
  if (id == IMAGE_CONTEXT_ID_UNUSED)
    return error_handler(RANGECHECK);

  while (context && id != context->id)
    context = context->next;

  if (context == NULL)
    return error_handler(UNDEFINED);

  *p_context = context;

  return TRUE;
}

/** \brief
    Maximum depth of object nesting supported by imagecontext filters. This
    is an arbitrary choice, we don't expect deeper than this. */
#define MAX_IMAGECONTEXT_RECURSION 8

/** \brief Helper function to extract filter decode match results and push
    report them to PostScript. */
static int32 psimageinfo(imagefilter_match_t *match, OBJECT *value)
{
  OBJECT ocopy = OBJECT_NOTVM_NOTHING, *theo ;
  Bool result ;

  HQASSERT(match, "No match for PS image filter decode callback") ;
  HQASSERT(value, "No value for PS image filter decode callback") ;

  /* If the object isn't already in PSVM, copy it there recursively. */
  if ( !psvm_copy_object(&ocopy, value, MAX_IMAGECONTEXT_RECURSION,
                         get_core_context_interp()->glallocmode) )
    return IMAGEFILTER_MATCH_FAIL ;

  /* Call PS procedure with key and value, expecting a boolean return
     value. */
  theo = match->data ;
  HQASSERT(theo, "No PS proc for PS image filter decode callback") ;

  if ( !push2(&match->key, &ocopy, &operandstack) ||
       !push(theo, &executionstack) )
    return IMAGEFILTER_MATCH_FAIL ;

  if ( !interpreter(1, NULL) ) {
    return IMAGEFILTER_MATCH_FAIL ;
  }

  if ( isEmpty(operandstack) ) {
    (void)error_handler(STACKUNDERFLOW) ;
    return IMAGEFILTER_MATCH_FAIL ;
  }

  theo = theTop(operandstack) ;
  if ( oType(*theo) != OBOOLEAN ) {
    (void)error_handler(TYPECHECK) ;
    return IMAGEFILTER_MATCH_FAIL ;
  }

  result = oBool(*theo) ;

  pop(&operandstack) ;

  if ( result )
    return IMAGEFILTER_MATCH_DONE ;

  return IMAGEFILTER_MATCH_MORE ;
}

/** This operator opens an image context.

    file dict \b imagecontextopen id /\<Type\> true

    file dict \b imagecontextopen false

    The operator imagecontextinfo can be used to get information from the
    context, ready for setting up an image dictionary. */
Bool imagecontextopen_(ps_context_t *pscontext)
{
  ImageContext *context;
  int32 subimage = 0 ;
  OBJECT *stream, *dict, *subimobj ;
  FILELIST* flptr;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Check we haven't already made too many image contexts.  The number
     is limited to a PS integer (value zero is reserved). */
  if (next_context_id == IMAGE_CONTEXT_ID_UNUSED) {
    HQFAIL("Run out of image context ids (PS integer)");
    return error_handler(LIMITCHECK);
  }

  if (theStackSize(operandstack) < 1)
    return error_handler(STACKUNDERFLOW);

  dict = theTop(operandstack);
  if (oType(*dict) != ODICTIONARY)
    return error_handler(TYPECHECK);

  stream = stackindex(1, &operandstack);
  if (oType(*stream) != OFILE)
    return error_handler(TYPECHECK) ;

  flptr = oFile(*stream);
  if (! isIOpenFileFilter(stream, flptr) || ! isIInputFile(flptr) || isIEof(flptr))
    return error_handler(IOERROR);

  if ( !oCanRead(*oDict(*dict)) &&
       !object_access_override(oDict(*dict)) )
    return error_handler(INVALIDACCESS) ;

  subimobj = fast_extract_hash_name(dict, NAME_SubImage) ;
  if ( subimobj != NULL ) {
    if ( oType(*subimobj) != OINTEGER )
      return error_handler(TYPECHECK) ;
    subimage = oInteger(*subimobj) ;
  }

  if ( !imagecontext_open(stream,
                          fast_extract_hash_name(dict, NAME_ImageFormat),
                          subimage, &context) )
    return FALSE;

  if ( context == NULL ) {
    /* Not a recognised image format. Remove args and leave false on stack. */
    pop(&operandstack) ;
    Copy(stream, &fnewobj) ;
    return TRUE ;
  }

  /* Assign an image context id. */
  HQASSERT(context->id == IMAGE_CONTEXT_ID_UNUSED, "image context id should be unused");
  context->id = next_context_id++;
  HQASSERT(context->id != IMAGE_CONTEXT_ID_UNUSED, "image context id is invalid");

  /* Put image context id and type back on stack. */
  oInteger(inewobj) = context->id ;
  Copy(stream, &inewobj) ;

  oName(nnewobj) = &system_names[context->format] ;
  Copy(dict, &nnewobj) ;

  return push(&tnewobj, &operandstack) ;
}

/** Get information about an image from an imagecontext.

    id {proc} [keys...] \b imagecontextinfo

    This operator calls a procedure for every key in the array that matches
    an image context key. The procedure should return true on the stack to
    stop scanning for more tags, false to continue scanning. */
Bool imagecontextinfo_(ps_context_t *pscontext)
{
  OBJECT *idobj, *array, *proc ;
  ImageContext* context;
  imagefilter_match_t *match = NULL, **prev ;
  Bool result;
  uint32 count ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (theStackSize(operandstack) < 2)
    return error_handler(STACKUNDERFLOW);

  array = theTop(operandstack) ;
  if (oType(*array) != OARRAY && oType(*array) != OPACKEDARRAY)
    return error_handler(TYPECHECK) ;

  if ( !oCanRead(*array) && !object_access_override(array) )
    return error_handler(INVALIDACCESS) ;

  proc = &array[-1];
  if ( !fastStackAccess(operandstack) )
    proc = stackindex(1, &operandstack);

  if (oType(*proc) != OARRAY && oType(*proc) != OPACKEDARRAY)
    return error_handler(TYPECHECK) ;

  if ( !oExecutable(*proc) ||
       (!oCanExec(*proc) && !object_access_override(proc)) )
    return error_handler(INVALIDACCESS) ;

  idobj = &proc[-1];
  if ( !fastStackAccess(operandstack) )
    idobj = stackindex(2, &operandstack);

  /* get_image_context performs typecheck on id */
  if ( !get_image_context(idobj, &context) )
    return FALSE;

  VERIFY_OBJECT(context, IMAGE_CONTEXT_NAME) ;

  if ( !push(proc, &temporarystack) )
    return FALSE ;
  proc = theTop(temporarystack) ;

#define return DO_NOT_return_GO_TO_cleanup_INSTEAD!

  /* Build a match list of keys based on the key list. */
  for ( count = theLen(*array), array = oArray(*array), prev = &match ;
        count > 0 ;
        --count, ++array ) {
    imagefilter_match_t *item ;

    switch ( oType(*array) ) {
    case OSTRING:
    case ONAME:
      if ( (item = mm_alloc(mm_pool_temp,
                            sizeof(imagefilter_match_t),
                            MM_ALLOC_CLASS_IMAGE_CONTEXT)) == NULL ) {
        result = error_handler(VMERROR) ;
        goto cleanup ;
      }

      (void)object_slot_notvm(&item->key) ;
      Copy(&item->key, array) ;
      item->callback = psimageinfo ;
      item->data = proc ;
      item->next = NULL ;
      *prev = item ;
      prev = &item->next ;
      break ;
    default:
      result = error_handler(TYPECHECK) ;
      goto cleanup ;
    }
  }

  npop(3, &operandstack) ; /* Remove args from stack whilst calling */

  result = imagecontext_info(context, match) ;

 cleanup:
  pop(&temporarystack) ;

  while ( match != NULL ) {
    imagefilter_match_t *next = match->next ;
    mm_free(mm_pool_temp, match, sizeof(imagefilter_match_t)) ;
    match = next ;
  }

#undef return
  return result;
}

/** This operator closes an open image context.

    id \b imagecontextclose */
Bool imagecontextclose_(ps_context_t *pscontext)
{
  OBJECT *idobj ;
  ImageContext* context;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW);

  idobj = theTop(operandstack);
  if ( !get_image_context(idobj, &context) )
    return FALSE;

  if ( !imagecontext_close(&context) )
    return FALSE;

  pop(&operandstack);

  return TRUE;
}

/* =============================================================================
* Log stripped */
