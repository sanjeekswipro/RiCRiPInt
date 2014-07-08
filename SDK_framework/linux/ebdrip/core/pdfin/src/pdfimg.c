/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfimg.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF image handling.
 */

#include "core.h"
#include "fileio.h" /* FILELIST */
#include "filterinfo.h"
#include "swpdf.h"
#include "pdfimg.h"

#include "coremaths.h" /* ulcm */
#include "swerrors.h"
#include "swdevice.h"  /* for EOF */
#include "dictscan.h"
#include "namedef_.h"  /* for system_names */

#include "images.h"
#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "gstate.h"
#include "execops.h"
#include "gstack.h"    /* for gs_gpush et al */
#include "swmemory.h"  /* for gs_cleargstates */
#include "gschead.h"
#include "gscpdf.h"    /* gsc_LabDefaultRange */
#include "jpeg2000.h"

#include "stream.h"    /* for streamLookupDict */
#include "pdfmem.h"
#include "pdfstrm.h"
#include "pdfxref.h"
#include "pdfmatch.h"
#include "pdfdefs.h"

#include "pdfcolor.h"
#include "pdfexec.h"
#include "pdfinlop.h"
#include "pdfops.h"
#include "pdfin.h"
#include "pdfopi.h"
#include "pdfgs4.h"
#include "pdfx.h"
#include "miscops.h"
#include "imagecontext.h"
#include "hqmemcmp.h"
#include "params.h"
#include "tranState.h"
#include "monitor.h"
#include "dlstate.h" /* DOING_TRANSPARENT_RLE */
#include "render.h" /* inputpage */

#include "pdfinmetrics.h"


typedef struct PS_IMAGE_DICT PS_IMAGE_DICT ;
struct PS_IMAGE_DICT {
  /* Members used to create a Postscript style image dictionary. */
  int32   imageType ;
  int32   width ;
  int32   height ;
  int32   bpc ;
  OBJECT* interpolate ;
  OBJECT  imageMatrix ;
  OBJECT  decode ;
  OBJECT  datasource ;
  OBJECT  matteColor ;

  /* Other members used during construction. */
  Bool    isSoftMask;
  OBJECT* mask;
  OBJECT* softMaskStream;
  int32   sMaskInData; /* its a JPXDecode thang */

  OBJECT *softMaskDict; /* For softmask images converted to type 12 images */
} ;

/* A few constants */

#define MATRIX_VALUES 6             /* The size of the ImageMatrix array */

/* Image parameter dictionary size - should be large enough for any type of
image. */
#define IMAGE_DICT_SIZE 16

/* static defs */
static Bool readImageAndSetColorSpace( PDFCONTEXT *pdfc, OBJECT *dict,
                                       OBJECT *source, PS_IMAGE_DICT *d,
                                       Bool* ismask, int32 *rendered ) ;

static Bool createType12SoftMaskDict(PDFCONTEXT* pdfc,
                                     PS_IMAGE_DICT* image,
                                     PS_IMAGE_DICT* softMask,
                                     OBJECT *decode);


/** \ingroup pdfops
 * \brief Begin image data for an inline image.
 * The key/value pairs on the stack (up to the base pointed to by BI) are
 * rolled into a dictionary with any abbreviations expanded on the way and then
 * passed to the image dispatcher.
 */
Bool pdfop_ID( PDFCONTEXT *pdfc )
{
  Bool result ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  imc->handlingImage = TRUE ;
  result = pdfop_IVD( pdfc, pdfimg_dispatch ) ;
  imc->handlingImage = FALSE ;

  return result ;
}

/** \brief
Create a dictionary suitable for dispatching the image described by 'content'
as a PostScript image.

Use destroyPSImageDictionary() to destroy the returned dictionary.

NOTE: Any compound objects inserted into the resulting dictionary from the
'content' structure are inserted directly - they are NOT copied (i.e. only the
containing dictionary is actually allocated). Thus this function transfers
ownership of any compound objects to the resulting dictionary (and thus all
compound objects will be destroyed by destroyPSImageDictionary()).
*/
static Bool createPSImageDictionary(PDFCONTEXT *pdfc,
                                    PS_IMAGE_DICT* content,
                                    OBJECT* dict)
{
  OBJECT value = OBJECT_NOTVM_NOTHING ;

  HQASSERT( pdfc , "pdfc NULL in pdfimg_create_psdict" ) ;
  HQASSERT( content , "content NULL in pdfimg_create_psdict" ) ;

  HQASSERT( content->imageType == 1 ||
            content->imageType == 3 ||
            content->imageType == 4 ||
            content->imageType == 12 ,
            "content->imageType unset in pdfimg_create_psdict" ) ;

  if ( ! pdf_create_dictionary(pdfc, IMAGE_DICT_SIZE, dict))
    return FALSE ;

  object_store_integer(&value,
                       (content->imageType == 12) ? 1: content->imageType) ;
  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_ImageType, &value) )
    return FALSE;

  object_store_integer(&value, content->width) ;
  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_Width, &value) )
    return FALSE;

  object_store_integer(&value, content->height) ;
  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_Height, &value) )
    return FALSE;

  object_store_integer(&value, content->bpc) ;
  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_BitsPerComponent, &value) )
    return FALSE;

  if (content->interpolate) {
    if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_Interpolate,
                                    content->interpolate))
      return FALSE;
  }

  if (! pdf_resolvexrefs(pdfc, &content->imageMatrix) ||
      ! pdf_fast_insert_hash_name(pdfc, dict, NAME_ImageMatrix,
                                  &content->imageMatrix))
    return FALSE;

  if (! pdf_resolvexrefs(pdfc, &content->decode) ||
      ! pdf_fast_insert_hash_name(pdfc, dict, NAME_Decode, &content->decode))
    return FALSE;

  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_DataSource,
                                  &content->datasource))
    return FALSE;

  if (content->isSoftMask)
    if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_IsSoftMask, &tnewobj))
      return FALSE;

  if (content->imageType == 12) {
    OBJECT parent = OBJECT_NOTVM_NOTHING;

    /* pour the bulk of the imagedict into a subdict called
       DataDict and the jpxmaskdict into a subdict called MaskDict */
    PDFXCONTEXT *pdfxc ;
    PDF_GET_XC( pdfxc ) ;

    if ( ! pdf_create_dictionary(pdfc, 4, &parent))
      return FALSE ;

    object_store_integer(&value, 12) ;
    if (! pdf_fast_insert_hash_name(pdfc, &parent, NAME_ImageType,
                                    &value))
      return FALSE;

    object_store_integer(&value, content->sMaskInData > 0 ? 1 : 3);
    /* JPX SMaskInData is sample-interleaved, others separate */
    if (! pdf_fast_insert_hash_name(pdfc, &parent, NAME_InterleaveType,
                                    &value))
      return FALSE;

    if (! pdf_fast_insert_hash_name(pdfc, &parent, NAME_MaskDict,
                                    content->softMaskDict))
      return FALSE;

    if (! pdf_fast_insert_hash_name(pdfc, &parent, NAME_DataDict,
                                    dict))
      return FALSE;

    OCopy(*dict, parent) ;
  }

  return TRUE;
}

/** \brief
Create a masked image dictionary in 'dict', using the passed image and
mask. See the section on type 3 masked images in the postscipt specification.

NOTE: This function calls createPSImageDictionary() - see the note on composite
object ownership for that function.
*/
static Bool createPSMaskedImageDictionary(PDFCONTEXT* pdfc,
                                          PS_IMAGE_DICT* image,
                                          PS_IMAGE_DICT* mask,
                                          OBJECT* dict,
                                          OBJECT* finalImageDict,
                                          OBJECT* finalMaskDict)
{
  OBJECT imageDict = OBJECT_NOTVM_NOTHING;
  OBJECT maskDict = OBJECT_NOTVM_NOTHING;
  OBJECT value = OBJECT_NOTVM_NOTHING ;

  PDF_CHECK_MC( pdfc ) ;
  HQASSERT(dict != NULL && image != NULL && mask != NULL,
           "createMaskedImageDictionary - parameters cannot be NULL");

  /* both data dict and mask dict must have image type of 1 (that's the way
  masked images are specified). */
  image->imageType = 1 ;
  mask->imageType = 1 ;

  if (! createPSImageDictionary(pdfc, image, &imageDict) ||
      ! createPSImageDictionary(pdfc, mask, &maskDict) ||
      ! pdf_create_dictionary(pdfc, IMAGE_DICT_SIZE, dict))
    return FALSE;

  /* Give the client copies of the constructed image/mask dictionaries if they
  are interested. */
  if (finalImageDict != NULL)
    *finalImageDict = imageDict;

  if (finalMaskDict != NULL)
    *finalMaskDict = maskDict;

  /* Masked images are type 3 in the PostScript world. */
  object_store_integer(&value, 3) ;
  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_ImageType, &value))
    return FALSE;

  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_DataDict, &imageDict))
    return FALSE;

  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_MaskDict, &maskDict))
    return FALSE;

  /* The /InterleaveType key is always 3, specifying that the image and mask
  are separate. */
  object_store_integer(&value, 3) ;
  if (! pdf_fast_insert_hash_name(pdfc, dict, NAME_InterleaveType, &value) )
    return FALSE;

  return TRUE;
}

/** \brief
Destroy a PS image dictionary previously created by createPSImageDictionary()
or createPSMaskedImageDictionary().

NOTE: All subobjects are destroyed. See the note on object ownship for
createPSImageDictionary().
*/
static void destroyPSImageDictionary(PDFCONTEXT* pdfc, OBJECT *dict)
{
  PDF_CHECK_MC(pdfc);

  pdf_freeobject(pdfc, dict);
}

/** \brief
Create a PDF image matrix.

The ImageMatrix is defined in ss7.10.1 of the PDF spec - PDF images are
always stored left-to-right, top-to- bottom, which corresponds to an image
matrix of [width 0 0 -height 0 height] (red book ss4.10.3).
*/
static Bool createImageMatrix(PDFCONTEXT* pdfc,
                              PS_IMAGE_DICT* image)
{
  OBJECT *matrix ;

  if ( ! pdf_create_array( pdfc , MATRIX_VALUES , & image->imageMatrix ))
    return error_handler( VMERROR ) ;

  matrix = oArray( image->imageMatrix ) ;

  object_store_numeric(&matrix[0], image->width) ;
  object_store_numeric(&matrix[1], 0) ;
  object_store_numeric(&matrix[2], 0) ;
  object_store_numeric(&matrix[3], -image->height) ;
  object_store_numeric(&matrix[4], 0) ;
  object_store_numeric(&matrix[5], image->height) ;

  return TRUE ;
}

/** \brief
Structure to capture required info from a JPEG 2000 image for PDF images.
*/
typedef struct {
  int32   width, height, bpc ;
  int32   imagetype,premult ;
  OBJECT  * maskcolor; /* chroma key*/
  OBJECT  colorspace;
  uint32  flags;
} pdfimg_jpx_data ;

enum { e_jpx_width = 0, e_jpx_height, e_jpx_bits,
       e_jpx_colspace, e_jpx_type, e_jpx_premult,
       e_jpx_maskcolor,
       e_jpx_max };
#define JPX_ALLDONE ((1<<e_jpx_max)-1)
#define BITSHIFT(x) (1<<(x))
#define JPX_REQUIRED ( BITSHIFT(e_jpx_width) && \
  BITSHIFT(e_jpx_height) && \
  BITSHIFT(e_jpx_bits) && \
  BITSHIFT(e_jpx_colspace) && \
  BITSHIFT(e_jpx_type) )

static int32 match_names[e_jpx_max] = {
  NAME_Width, NAME_Height,
  NAME_BitsPerComponent, NAME_ColorSpace,
  NAME_ImageType, NAME_PreMult,
  NAME_MaskColor
} ;



/** \brief
Callback to get required info from a JPEG 2000 image.
*/
static int32 pdfimg_jpx_callback(/*@notnull@*/ /*@in@*/ imagefilter_match_t *match,
                                 /*@notnull@*/ /*@in@*/ OBJECT *value)
{
  pdfimg_jpx_data *data  ;

  HQASSERT(match, "No imagefilter match") ;
  HQASSERT(oType(match->key) == ONAME, "Wrong match type") ;

  data = match->data ;
  HQASSERT(data != NULL, "Nowhere to put value") ;

  switch ( oNameNumber(match->key) ) {
  case NAME_Width:
    HQASSERT(oType(*value) == OINTEGER, "Width is not an integer") ;
    data->width = oInteger(*value) ;
    data->flags |= 1<< e_jpx_width;
    break ;
  case NAME_Height:
    HQASSERT(oType(*value) == OINTEGER, "Height is not an integer") ;
    data->height = oInteger(*value) ;
    data->flags |= 1<< e_jpx_height;
    break ;
  case NAME_BitsPerComponent:
    HQASSERT(oType(*value) == OINTEGER, "BitsPerComponent is not an integer") ;
    data->bpc = oInteger(*value) ;
    data->flags |= 1<< e_jpx_bits;
    break ;
  case NAME_ColorSpace:
    {
      HQASSERT((oType(*value) == ONAME)||(oType(*value) == OARRAY),
              "ColorSpace is not a name or array") ;
      /* If the object isn't already in PSVM, copy it there recursively. */
#define MAX_COLORSPACE_RECURSION 4
      if ( !psvm_copy_object(&data->colorspace, value, MAX_COLORSPACE_RECURSION,
                             get_core_context_interp()->glallocmode) )
        return IMAGEFILTER_MATCH_FAIL ;

      data->flags |= 1<< e_jpx_colspace;
    }
    break ;
  case NAME_ImageType:
    HQASSERT(oType(*value) == OINTEGER, "ImageType is not an integer") ;
    data->imagetype = oInteger(*value) ;
    data->flags |= 1<< e_jpx_type;
    break ;
  case NAME_PreMult:
    HQASSERT(oType(*value) == OBOOLEAN, "PreMult is not an integer") ;
    data->premult = oInteger(*value) ;
    data->flags |= 1<< e_jpx_premult;
    break ;
  case NAME_MaskColor:
    HQASSERT(oType(*value) == OARRAY, "MaskColor is not an array") ;
    data->maskcolor = value ;
    data->flags |= 1<< e_jpx_maskcolor;
    break ;
  default:
    HQFAIL("Unknown key in pdf jpx info callback") ;
    return IMAGEFILTER_MATCH_FAIL ;
  }

  if ( data->flags == JPX_ALLDONE )
    return IMAGEFILTER_MATCH_DONE ;

  return IMAGEFILTER_MATCH_MORE ;
}

/** \brief
Get required info from a JPEG2000 image using filter decode info.
*/
static Bool pdfimg_jpx_getinfo(FILELIST *flptr,
                               PS_IMAGE_DICT *imageinfo,
                               OBJECT *colorspace)
{
  uint32 i ;
  Hq32x2 filepos ;
  pdfimg_jpx_data data = { 0 } ;
  imagefilter_match_t match_args[e_jpx_max], *match = NULL ;

  /* These are the match fields required to fill in an IMAGEARGS struct.
     Form them into a linked list, but only include NAME_ColorSpace if
     colorspace is supplied. */
  for ( i = 0 ; i < e_jpx_max ; ++i ) {
    if ( match_names[i] != NAME_ColorSpace || colorspace != NULL ) {
      object_store_name(object_slot_notvm(&match_args[i].key),
                        match_names[i], LITERAL) ;
      match_args[i].data = &data ;
      match_args[i].callback = pdfimg_jpx_callback ;
      match_args[i].next = match ;
      match = &match_args[i] ;
    }
  }

  /* Placeholder to make early return test easier. */
  if ( colorspace == NULL )
    object_store_name(&data.colorspace, NAME_Default, LITERAL) ;

#if defined( METRICS_BUILD )
  pdfin_metrics.JPX++;
#endif

  /* Assume we are at the start of the stream */
  if ( !(*theIFilterDecodeInfo(flptr))(flptr, match) )
    return FALSE ;

  /* rewind to start of stream */
  if ( ! isIOpenFileFilterById(theIUnderFilterId(flptr), theIUnderFile(flptr)) ||
       ! isIInputFile(flptr) )
    return error_handler(IOERROR) ;

  Hq32x2FromUint32(&filepos, 0u);
  if ( (*theIMyResetFile(flptr))(flptr) == EOF ||
       (*theIMySetFilePos(flptr))(flptr, &filepos) == EOF )
    return (*theIFileLastError(flptr))(flptr);

  /* Are all of the required fields defined? */
  if ( (data.flags & JPX_REQUIRED) != JPX_REQUIRED ||
       (oType(data.colorspace) != ONAME && oType(data.colorspace) != OARRAY) )
    return error_handler(UNDEFINED) ;

  imageinfo->width = data.width ;
  imageinfo->height = data.height ;
  imageinfo->bpc = data.bpc ;
  imageinfo->imageType = data.imagetype;
  if ( colorspace )
    OCopy(*colorspace, data.colorspace) ;

  /* check for a chroma keyed image */
  if (data.flags & BITSHIFT(e_jpx_maskcolor)) {
    imageinfo->imageType = data.imagetype = 4; /*chroma keyed */
    imageinfo->mask = data.maskcolor;
  }
  return TRUE ;
}


#if defined(DEBUG_BUILD)
static void pdf_cs_output(PDFCONTEXT *pdfc, OBJECT *cs)
{
  if ( cs == NULL )
    return;
  if (!pdf_resolvexrefs(pdfc, cs))
    return;
  if ( oType(*cs) == ONAME )
    monitorf((uint8*)" %.*s", oName(*cs)->len, (char*)oName(*cs)->clist);
  else if ( oType(*cs) == OARRAY ) {
    int32 cspace = pdf_getColorSpace(cs);
    switch ( cspace ) {
    case NAME_Indexed: {
      monitorf((uint8*)" Indexed over");
      pdf_cs_output(pdfc, &oArray(*cs)[1]);
    } break;
    case NAME_DeviceN:
      monitorf((uint8*)" DeviceN[%d]", theLen(oArray(*cs)[1]));
      break;
    default:
      monitorf((uint8*)" %.*s",
               oName(oArray(*cs)[0])->len, oName(oArray(*cs)[0])->clist);
    }
  } else
    monitorf((uint8*)" ???");
}
#endif


#define DESCRIBE_PDF_IMAGE(pdfc, dict, match, softmask) MACRO_START \
  OBJECT *xref_ = fast_extract_hash_name(dict, NAME_XRef); \
  \
  monitorf((uint8*)"%%%% PDF %d %4d: %dx%d", \
           (pdfc)->pdfxc->pageId, \
           xref_ != NULL ? oInteger(*xref_) : -1, \
           oInteger(*(match)[e_width].result), \
           oInteger(*(match)[e_height].result)); \
  if ( (match)[e_bitspercomponent].result != NULL ) \
    monitorf((uint8*)" %2d bpc", \
             oInteger(*(match)[e_bitspercomponent].result)); \
  pdf_cs_output(pdfc, (match)[e_colorspace].result); \
  if ( (match)[e_decode].result != NULL ) \
    monitorf((uint8*)" decoded"); \
  if ( (match)[e_imagemask].result != NULL ) \
    monitorf((uint8*)" mask"); \
  if ( softmask ) \
    monitorf((uint8*)" softmask"); \
  monitorf((uint8*)"\n"); \
MACRO_END


#if defined( DEBUG_BUILD )
static Bool image_dict_output(PDFCONTEXT* pdfc, OBJECT *dict)
{
  if ( !pdf_resolvexrefs(pdfc, dict)
       || !push(dict, &operandstack) )
    return FALSE;
  return run_ps_string((uint8*)
          "1 dict begin "
            "/print-object { "
              "dup type /dicttype eq "
                "{(<< ) print {exch == print-object} forall "
                 "(>>) print () =}"
                "{dup type /arraytype eq "
                   "{([ ) print {print-object} forall (]) print () =}"
                   "{==}"
                 "ifelse}"
              "ifelse"
            "} def "
            "print-object "
          "end");
}
#endif


/** \brief
Read a soft mask image dictionary. Returns FALSE on error.
The 'softMask' parameter will be filled with the soft mask details.
*/
static Bool readSoftMaskImage(PDFCONTEXT* pdfc,
                              OBJECT softMaskStream,
                              PS_IMAGE_DICT* softMask)
{
  enum {
    e_type,e_subtype,e_width,e_height,e_colorspace,e_bitspercomponent,
        e_imagemask,e_mask,e_smask,e_decode,e_interpolate,e_matte, e_smaskindata, e_max};
  static NAMETYPEMATCH match[e_max + 1] = {
    { NAME_Type | OOPTIONAL, 2, { ONAME, OINDIRECT }},
    { NAME_Subtype, 2, { ONAME, OINDIRECT }},
    { NAME_Width, 2, { OINTEGER, OINDIRECT }},
    { NAME_Height, 2, { OINTEGER, OINDIRECT }},
    { NAME_ColorSpace, 2, { ONAME, OINDIRECT }},
    { NAME_BitsPerComponent, 2, { OINTEGER, OINDIRECT }},
    { NAME_ImageMask | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
    { NAME_Mask | OOPTIONAL, 4, { OARRAY, OPACKEDARRAY, OFILE,
                                  OINDIRECT }},
    { NAME_SMask | OOPTIONAL, 2, { OFILE, OINDIRECT }},
    { NAME_Decode | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_Interpolate | OOPTIONAL, 2, { OBOOLEAN, OINDIRECT }},
    { NAME_Matte | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_SMaskInData | OOPTIONAL,  2,  { OINTEGER, OINDIRECT }},
    DUMMY_END_MATCH
  };

  OBJECT* dict;
  Bool jpxdecodefile;
  FILELIST * flptr;
  int32 smaskindata = 0 ; /* its a JPXDecode thang */

  PDF_CHECK_MC(pdfc);
  HQASSERT(softMask != NULL, "readSoftMaskImage - 'softMask' cannot be NULL");

#if defined( METRICS_BUILD )
  pdfin_metrics.softMaskCounts.image ++;
#endif

  dict = streamLookupDict(&softMaskStream);
  if (! pdf_dictmatch(pdfc, dict, match))
    return FALSE ;

  flptr = oFile(softMaskStream);
  jpxdecodefile = (HqMemCmp(flptr->clist, flptr->len,
                            NAME_AND_LENGTH("JPXDecode")) == 0);
  if ( jpxdecodefile ) {
    if ( !pdfimg_jpx_getinfo(flptr, softMask, NULL) )
      return FALSE ;
    jpeg2000_setflags(flptr, jpeg2000_flag_override_cs, TRUE);
    /* Kakadu can't return packed samples, only 8/16/32 bpc, so ask
     * it to scale the samples to those widths, except for Indexed */
    jpeg2000_setflags(flptr, jpeg2000_flag_scale_to_byte, TRUE);
  }
#if defined( DEBUG_BUILD )
  if ( jpxdecodefile || (pdf_debug & PDF_DEBUG_IMAGE_JPX) == 0 ) {
    if ( (pdf_debug & PDF_DEBUG_IMAGE) != 0 )
      DESCRIBE_PDF_IMAGE(pdfc, dict, match, TRUE);
    if ( (pdf_debug & PDF_DEBUG_IMAGE_DICT) != 0 )
      if ( !image_dict_output(pdfc, dict) )
        return FALSE;
  }
#endif

  /* Enforce soft-mask image restrictions. */
  if ( jpxdecodefile && match[e_smaskindata].result != NULL ) {
    smaskindata = oInteger(*match[e_smaskindata].result);
    if ( smaskindata != 0 )
      return detail_error_handler(RANGECHECK,
                                  "SMaskInData specified for a softmask.");
  }

  /* "Type" must be absent or "XObject." */
  if (match[e_type].result != NULL &&
      oName(*match[e_type].result) != system_names + NAME_XObject)
    return error_handler(TYPECHECK);

  /* Subtype must be "Image." */
  if (oName(*match[e_subtype].result) != system_names + NAME_Image)
    return error_handler(TYPECHECK);

  /* Colorspace must be "DeviceGray." */
  if (oName(*match[e_colorspace].result) != system_names + NAME_DeviceGray)
    return error_handler(RANGECHECK);

  /* "ImageMask" must be FALSE. */
  if (match[e_imagemask].result && oBool(*match[e_imagemask].result))
    return error_handler(RANGECHECK);

  /* The soft mask cannot be masked or softmasked. */
  if (match[e_mask].result != NULL || match[e_smask].result != NULL)
    return error_handler(RANGECHECK) ;

  if ( jpxdecodefile ) {
    if ( softMask->width != oInteger(*match[e_width].result) ||
         softMask->height != oInteger(*match[e_height].result) )
      return error_handler(RANGECHECK) ;
  }

  /* Fill-out the image details structure. */
  softMask->imageType = 1;
  softMask->width = oInteger(*match[e_width].result);
  softMask->height = oInteger(*match[e_height].result);
  softMask->bpc = oInteger(*match[e_bitspercomponent].result);
  softMask->interpolate = match[e_interpolate].result;
  softMask->isSoftMask = TRUE;
  softMask->matteColor = onull ; /* Struct copy to set slot properties */

  if (match[e_matte].result != NULL) {
    /* Insert a copy to allow deallocation later. */
    if (! pdf_copyobject(pdfc, match[e_matte].result, &softMask->matteColor))
      return FALSE;
  }

  /* Create a matrix mapping the image to the unit square. */
  if ( ! createImageMatrix( pdfc , softMask ))
    return FALSE;

  if (match[e_decode].result != NULL) {
    /* Insert a copy to allow deallocation later. */
    if ( ! pdf_copyobject(pdfc, match[e_decode].result, &softMask->decode))
      return FALSE ;
  }
  else {
    /* Create a default decode array. */
    if (! pdf_create_array(pdfc, 2, &softMask->decode))
      return FALSE ;

    object_store_real(&oArray(softMask->decode)[0], 0.0f);
    object_store_real(&oArray(softMask->decode)[1], 1.0f);
  }

  /* Take a copy of the data source so it can be deallocated later. */
  if (! pdf_copyobject(pdfc, &softMaskStream, &softMask->datasource))
    return FALSE;

  return TRUE;
}


/** \brief Extract the image parameters from the 'dict', storing the
  results in the passed PS_IMAGE_DICT 'ps_image', allowing a PDF image
  to ultimately be dispatched as a regular PS image. The colorspace for
  the image will be set, unless it's an imagemask.

  Note that PDF1.5 has brought JPEG2000 in as an image type. This means
  that some image parameters may be within the image. So we need to peek
  at the image first to get these.
*/
static Bool readImageAndSetColorSpace( PDFCONTEXT *pdfc ,
                                        OBJECT *dict ,
                                        OBJECT *source ,
                                        PS_IMAGE_DICT *ps_image,
                                        Bool* ismask,
                                        Bool* rendered )
{
  enum {/* e_type,e_subtype,e_name,*/e_width,e_height,e_bitspercomponent,
        e_colorspace,e_decode,e_interpolate,e_imagemask,e_intent,e_OPI,
        e_mask,e_alternates,e_smask,e_smaskindata, e_max};
  static NAMETYPEMATCH pdfimg_dispatchdict[e_max + 1] = {
    /*{ NAME_Type,                     1,  { ONAME }},
    { NAME_Subtype,                  1,  { ONAME }},
    { NAME_Name,                     1,  { ONAME }}, */
    { NAME_Width,                    2,  { OINTEGER, OINDIRECT }},
    { NAME_Height,                   2,  { OINTEGER, OINDIRECT }},
    { NAME_BitsPerComponent | OOPTIONAL, 2,  { OINTEGER, OINDIRECT }},

    { NAME_ColorSpace | OOPTIONAL,   4,  { ONAME, OARRAY, OPACKEDARRAY,OINDIRECT }},
    { NAME_Decode | OOPTIONAL,       3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_Interpolate | OOPTIONAL,  2,  { OBOOLEAN, OINDIRECT }},
    { NAME_ImageMask | OOPTIONAL,    2,  { OBOOLEAN, OINDIRECT }},
    { NAME_Intent | OOPTIONAL,       2,  { ONAME, OINDIRECT }},
    { NAME_OPI | OOPTIONAL,          2,  { ODICTIONARY, OINDIRECT }},
    { NAME_Mask | OOPTIONAL,         4,  { OARRAY, OPACKEDARRAY, OFILE, OINDIRECT }},
    { NAME_Alternates | OOPTIONAL,   3,  { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_SMask | OOPTIONAL,        2,  { OFILE, OINDIRECT }},
    { NAME_SMaskInData | OOPTIONAL,  2,  { OINTEGER, OINDIRECT }},

    DUMMY_END_MATCH
  } ;
  Bool jpxdecodefile = FALSE, check_decode = FALSE ;
  FILELIST * flptr = oFile(*source);
  STACK *stack ;
  OBJECT JPXcolorspace = OBJECT_NOTVM_NULL;
  OBJECT *colorspace;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;
  PDF_GET_IMC( imc ) ;

  HQASSERT( ps_image != NULL && ismask != NULL,
            "readImageAndSetColorSpace - parameters cannot be NULL." ) ;

  stack = ( & imc->pdfstack ) ;

  object_store_null(&ps_image->matteColor);
  ps_image->isSoftMask = FALSE;
  ps_image->imageType = 0;

  if ( oType(*dict) != ODICTIONARY )
    return FALSE ;
  if ( ! pdf_dictmatch( pdfc , dict , pdfimg_dispatchdict ))
    return FALSE ;
  colorspace = pdfimg_dispatchdict[e_colorspace].result;

  /* ImageMask: if present, bitspercomponent should be 1 and Mask and
     ColorSpace should not be specified. */
  *ismask = FALSE ;
  if ( pdfimg_dispatchdict[ e_imagemask ].result ) {
    HQASSERT(oType(*pdfimg_dispatchdict[e_imagemask].result) == OBOOLEAN ,
             "ImageMask not Boolean in pdfimg_dispatch." ) ;

    *ismask = oBool(*pdfimg_dispatchdict[e_imagemask].result) ;
  }

  jpxdecodefile = (HqMemCmp(flptr->clist, flptr->len,
                            NAME_AND_LENGTH("JPXDecode")) == 0);
  /* On JPXDecode image, get params from the image into ps_image. */
  if ( jpxdecodefile ) {
    /* Colorspace is not required. Use the entry in the dictionary if
       present, otherwise get the native colorspace. */
    Bool get_cs = colorspace == NULL && !*ismask;

    if ( !pdfimg_jpx_getinfo(flptr, ps_image, get_cs ? &JPXcolorspace : NULL) )
      return FALSE ;
    if ( get_cs ) {
      HQASSERT(oType(JPXcolorspace) != ONULL, "No JPEG2000 colorspace");
      colorspace = &JPXcolorspace;
    } else
      jpeg2000_setflags(flptr, jpeg2000_flag_override_cs, TRUE);
    if ( colorspace == NULL || pdf_getColorSpace(colorspace) != NAME_Indexed )
      /* Kakadu can't return packed samples, only 8/16/32 bpc, so ask
       * it to scale the samples to those widths, except for Indexed */
      jpeg2000_setflags(flptr, jpeg2000_flag_scale_to_byte, TRUE);

    /* For jpeg2000 images with no ColorSpace, check the Decode array isn't
       illegal (later, after setting the colorspace). [12856] */
    if (get_cs && pdfimg_dispatchdict[ e_decode ].result)
      check_decode = TRUE ;
  }
#if defined( DEBUG_BUILD )
  if ( jpxdecodefile || (pdf_debug & PDF_DEBUG_IMAGE_JPX) == 0 ) {
    if ( (pdf_debug & PDF_DEBUG_IMAGE) != 0 )
      DESCRIBE_PDF_IMAGE(pdfc, dict, pdfimg_dispatchdict, FALSE);
    if ( (pdf_debug & PDF_DEBUG_IMAGE_DICT) != 0 )
      if ( !image_dict_output(pdfc, dict) )
        return FALSE;
  }
#endif

  /* Set the soft mask stream - this will be NULL if no soft mask is present. */
  ps_image->softMaskStream = pdfimg_dispatchdict[e_smask].result;

  ps_image->sMaskInData = 0;
  if ( jpxdecodefile && pdfimg_dispatchdict[e_smaskindata].result != NULL ) {
    ps_image->sMaskInData = oInteger(*pdfimg_dispatchdict[e_smaskindata].result);
    if ( ps_image->sMaskInData < 0 || ps_image->sMaskInData > 2 )
      return detail_error_handler(RANGECHECK, "SMaskInData.");
    /* SMask required to be undefined */
    if ( pdfimg_dispatchdict[e_smask].result != NULL )
      return detail_error_handler(RANGECHECK, "SMaskInData and SMask.");
  } else {
    if ( ps_image->softMaskStream != NULL )
      if ( ! pdfxSoftMaskedImageDetected( pdfc ))
        return FALSE ;
  }
  if ( ps_image->sMaskInData != 0 ) {
    OBJECT decode = OBJECT_NOTVM_NOTHING;

    if ( ps_image->imageType != 12 )
      return detail_error_handler(TYPECHECK, "No alpha channel for SMaskInData.");
    if ( ps_image->sMaskInData == 2 )
      return detail_error_handler(RANGECHECK, "Premultiplied transparency on JPX not supported yet.");

    if (! createImageMatrix(pdfc, ps_image) )
      return FALSE;

    /* Create a default decode array. */
    if (! pdf_create_array(pdfc, 2, &decode) )
      return FALSE;
    object_store_real(&oArray(decode)[0], 0.0f);
    object_store_real(&oArray(decode)[1], 1.0f);

    if ( !createType12SoftMaskDict(pdfc, ps_image, ps_image, &decode) )
      return FALSE;
  }

  /* Set the mask object - this will be NULL if the image is not masked. */
  if ( !jpxdecodefile || ps_image->imageType != 4 )
    ps_image->mask = pdfimg_dispatchdict[e_mask].result;

  /* Perform PDF/X checks on the Alternates first. */
  if ( pdfimg_dispatchdict[ e_alternates ].result != NULL )
    if ( ! pdfxCheckImageAlternates( pdfc, pdfimg_dispatchdict[ e_alternates ].result ))
      return FALSE ;

  /* I need to know the dimensions of the image */
  HQASSERT( oType( *(pdfimg_dispatchdict[ e_width ].result) ) == OINTEGER,
            "Width not an integer in pdfimg_dispatch." ) ;
  HQASSERT( oType( *(pdfimg_dispatchdict[ e_height ].result) ) == OINTEGER,
            "Height not an integer in pdfimg_dispatch." ) ;

  if ( jpxdecodefile ) {
    if ( ps_image->width != oInteger(*pdfimg_dispatchdict[e_width].result) ||
         ps_image->height != oInteger(*pdfimg_dispatchdict[e_height].result) )
      return error_handler(RANGECHECK) ;
  } else {
    ps_image->width = oInteger(*pdfimg_dispatchdict[e_width].result);
    ps_image->height = oInteger(*pdfimg_dispatchdict[e_height].result);

    if ( pdfimg_dispatchdict[e_bitspercomponent].result != NULL ) {
      ps_image->bpc = oInteger(*pdfimg_dispatchdict[e_bitspercomponent].result);
    } else if ( *ismask ) { /* BitsPerComponent is optional for imagemask. */
      ps_image->bpc = 1;
    } else { /* BitsPerComponent was required, but wasn't supplied. */
      return error_handler(TYPECHECK) ;
    }
  }

  if ( *ismask ) { /* Image mask */
    /* PDF 1.6, p. 310: BitsPerComponent must be 1 if specified. */
    if ( ps_image->bpc != 1 )
      return error_handler(RANGECHECK) ;

    /* If strict PDF is off just pretend a DeviceGray color space
       never existed. */
    if ( !ixc->strictpdf && colorspace != NULL ) {
      int32 cspace = pdf_getColorSpace(colorspace);
      if ( cspace == NAME_DeviceGray )
        colorspace = NULL;
    }

    /* PDF 1.6, p. 311: Mask and ColorSpace should not be specified. */
    if ( pdfimg_dispatchdict[e_mask].result != NULL ||
         colorspace != NULL )
      return error_handler(TYPECHECK) ;

  } else { /* Normal images */
    /* PDF 1.6, p. 310: ColorSpace is required. */
    if ( colorspace == NULL )
      return error_handler(TYPECHECK) ;
  }

  if ( ps_image->bpc > 8 ) {
    if ( ! pdfx16BitImageDetected(pdfc) )
      return FALSE;
  }

  /* Intent */
  if ( pdfimg_dispatchdict[ e_intent ].result ) {
    OBJECT *theo = pdfimg_dispatchdict[ e_intent ].result ;
    if (! pdf_set_rendering_intent( pdfc , theo ))
      return FALSE ;
  }

  /* ColorSpace: required for images, not allowed for image masks */
  if ( colorspace != NULL ) {
    OBJECT mappedObj = OBJECT_NOTVM_NOTHING ;

    /* Map the colorspace onto our names, see pdfcolor.c */
    if ( ! pdf_mapcolorspace( pdfc, colorspace, &mappedObj, NULL_COLORSPACE ))
      return FALSE ;

    if ( ! push( &mappedObj , stack ))
      return FALSE ;

    if ( ! gsc_setcolorspace( gstateptr->colorInfo , stack , GSC_FILL ))
      return FALSE ;
  }

  /* For jpeg2000 images with no ColorSpace but a Decode, suppress it if
     illegal to avoid RANGECHECK from get_image_dictargs() [12856] */
  if (check_decode) {
    int32 ndecodes = gsc_dimensions( gstateptr->colorInfo, GSC_FILL ) ;
    /* We don't need to check for PreMult and increment ndecodes as we only
       get here for non-mask JPXs. */
    if (theLen(*pdfimg_dispatchdict[ e_decode ].result) != 2*ndecodes)
      pdfimg_dispatchdict[ e_decode ].result = NULL ; /* force a new one */
  }

  /* Call out to OPI code if necessary. Ignored if ImageMask is true. */
  if ( pdfimg_dispatchdict[ e_OPI ].result && !*ismask ) {

    if ( ! pdfOPI_dispatch( pdfc , pdfimg_dispatchdict[ e_OPI ].result ,
                            rendered ))
      return FALSE ;

    if ( *rendered )
      return TRUE ;
  }

  ps_image->interpolate = pdfimg_dispatchdict[ e_interpolate ].result;

  /* Create a matrix mapping the image to the unit square. */
  if ( !createImageMatrix(pdfc, ps_image) )
    return FALSE ;

  /* If no custom Decode array has been supplied (by far the most common
   * case), we need to synthesise one of the default ones described in
   * Table 7.27 (of PDF spec 1.2). Oddly, non-mask JPXDecode images are
   * supposed to ignore it but demonstrably don't do so for Adobe RIPs,
   * Ghostscript and Jaws. Example jobs suggest that images are indeed
   * created that require this (request 50842). */
  if ( pdfimg_dispatchdict[ e_decode ].result != NULL ) {
    int32 len = theLen(* pdfimg_dispatchdict[ e_decode ].result ) ;

    /* Must copy the array into a fixed size copy so that the
     * free can be hardwired. */
    if ( len > 0 ) {
      int32 i ;
      OBJECT *srcalist ;
      OBJECT *destalist ;

      if ( !pdf_create_array( pdfc, len, &ps_image->decode ))
        return FALSE ;

      srcalist = oArray( *(pdfimg_dispatchdict[ e_decode ].result) ) ;
      destalist = oArray( ps_image->decode );

      for ( i = len ; i > 0 ; --i , srcalist++ , destalist++ )
        Copy( destalist , srcalist ) ;
    } else {
      oArray( ps_image->decode ) = NULL;
      theLen( ps_image->decode ) = ( uint16 )0;
    }
  } else {
    static USERVALUE decodearraydefaults[ 2 ] = { 0.0f, 1.0f } ;
    OBJECT *decodevalue ;
    uint8 colorspace ;

    if ( *ismask )
      /* Just to get the right number of dimensions */
      colorspace = SPACE_DeviceGray ;
    else
      colorspace = gsc_getcolorspace( gstateptr->colorInfo, GSC_FILL ) ;

    switch ( colorspace ) {
      case SPACE_Lab: {
        int32 i, decodearraylength ;
        OBJECT *range , *theo ;
        int32 dims ;

        theo = gsc_getcolorspaceobject( gstateptr->colorInfo , GSC_FILL ) ;
        /* [ /Lab << ... >> ] */
        theo = oArray( *theo ) + 1 ;

        HQASSERT( oType( *theo ) == ODICTIONARY,
                  "pdfimg_dispatch: theo not a dictionary" ) ;
        dims = 3 ;

        /* We have to allocate a new decode array because the Lab Range object
         * only covers two dimensions
         */
        decodearraylength = 2 * dims ;
        if ( ! pdf_create_array( pdfc, decodearraylength, &ps_image->decode ))
          return FALSE ;
        decodevalue = oArray( ps_image->decode );

        range = fast_extract_hash_name( theo , NAME_Range ) ;
        if ( range ) {
          OBJECT *rangeValue ;

          HQASSERT( ( oType( *range ) == OARRAY ||
                      oType( *range ) == OPACKEDARRAY ) ,
                      "pdfimg_dispatch: range not an array/packedarray" ) ;
          /* Yes, there are 3 dims, but the range of L is not user specified */
          HQASSERT( theLen(* range ) == 4 ,
                    "pdfimg_dispatch: length of range not correct" ) ;

          rangeValue  = oArray( *range ) ;

          for ( i = 0 ; i < 2 ; ++i ) {
            object_store_real(&decodevalue[i], gsc_LabDefaultRange(i)) ;
          }
          for ( i = 2 ; i < decodearraylength ; ++i )
            Copy( &decodevalue[i], &rangeValue[i - 2] ) ;
        }
        else {
          for ( i = 0 ; i < decodearraylength ; ++i ) {
            object_store_real(&decodevalue[i], gsc_LabDefaultRange(i)) ;
          }
        }
      }
      break;

      case SPACE_ICCBased :
        {
          SYSTEMVALUE icc_range[ 2 ];
          OBJECT *range , *theo ;
          int32 i, j, dims ;

          dims = gsc_dimensions( gstateptr->colorInfo , GSC_FILL );

          theo = gsc_getcolorspaceobject( gstateptr->colorInfo , GSC_FILL ) ;
          /* [ /ICCBased <filestream> ] */
          theo = oArray( *theo ) + 1 ;

          HQASSERT( oType( *theo ) == OFILE,
                    "ICCBased parameter not a file" ) ;
          theo = streamLookupDict( theo ) ;

          range = fast_extract_hash_name( theo , NAME_Range ) ;
          if ( range ) {
            HQASSERT( ( oType( *range ) == OARRAY ||
                        oType( *range ) == OPACKEDARRAY ) ,
                      "range not an array/packedarray" ) ;
            HQASSERT( theLen(* range ) == 2 * dims ,
                      "length of range not correct" ) ;
            if ( ! pdf_copyobject( pdfc, range, &ps_image->decode ))
              return FALSE ;
          }
          else {
            /* Strictly the default should be [0 1 0 1...0 1], but get the
               range from the icc profile colorspace to allow for e.g. Lab
               colorconversion profiles */
            if ( ! pdf_create_array( pdfc, 2 * dims, &ps_image->decode ))
              return FALSE ;
            decodevalue = oArray( ps_image->decode );

            for ( i = 0; i < dims; ++i ) {
              if ( ! gsc_range( gstateptr->colorInfo , GSC_FILL , i , icc_range ))
                return FALSE ;

              for ( j = 0; j < 2; ++j ) {
                object_store_real(&decodevalue[(2*i)+j], (USERVALUE)icc_range[j]);
              }
            }
          }
        }
        break;

      case SPACE_DeviceCMYK:
      case SPACE_DeviceGray:
      case SPACE_DeviceRGB:
      case SPACE_Separation:
      case SPACE_DeviceN:
      case SPACE_CalGray:
      case SPACE_CalRGB:
        {
          int32 i, decodearraylength ;

          if ( *ismask )
            decodearraylength = 2 ;
          else
            decodearraylength = 2 * gsc_dimensions( gstateptr->colorInfo , GSC_FILL ) ;

          if ( ! pdf_create_array( pdfc, decodearraylength, &ps_image->decode ))
            return FALSE ;
          decodevalue = oArray( ps_image->decode );

          for ( i = 0 ; i < decodearraylength ; ++i ) {
            object_store_real(decodevalue++, decodearraydefaults[i & 1]) ;
          }
        }
        break ;

      case SPACE_Indexed:
        /* Special case: don't use the above default decode array */
        if ( ! pdf_create_array( pdfc, 2, &ps_image->decode ))
          return FALSE ;
        decodevalue = oArray( ps_image->decode ) ;
        object_store_real(&decodevalue[0], 0.0f) ;
        object_store_real(&decodevalue[1], (USERVALUE)((1L << ps_image->bpc) - 1));
        break ;

      case SPACE_notset:
      case SPACE_Pattern:
      case SPACE_Preseparation:
      default:
        /* The color space is either not supported or not allowed */
        return error_handler( TYPECHECK ) ;
    }
  }
  /* 'source' is the source passed in - any filters should already be in
  place. */
  if (! pdf_copyobject(pdfc, source, &ps_image->datasource))
    return FALSE;

  return TRUE ;
}

/** \brief
Call out to postscript to draw the image described by 'imageDict', which
must be a dictionary compatible with the 'image' operator in post script.
*/
static Bool drawPSImage(PDFCONTEXT* pdfc, OBJECT imageDict, Bool isMask)
{
  STACK *stack;
  PDF_IMC_PARAMS *imc;
  OBJECT psDict = OBJECT_NOTVM_NOTHING;

  PDF_CHECK_MC(pdfc);
  PDF_GET_IMC(imc);

  /* Copy the pdf dictionary into PostScript memory as we're calling a
  PostScript function (we don't want to risk having a mixed pdf/ps memory
  dictionary). */
  if (! pdf_copyobject(NULL, &imageDict, &psDict))
    return FALSE;

  /* Now push the dictionary and invoke the image operator. */
  stack = (&imc->pdfstack);
  if (! (push(&psDict, stack) &&
         (isMask ? gs_imagemask(pdfc->corecontext, stack)
                 : gs_image(pdfc->corecontext, stack))))
    return FALSE;

  return TRUE;
}

/** \brief
Dispatch a unmasked image/imagemask, with an optional soft mask.
*/
static Bool dispatchImage(PDFCONTEXT* pdfc, PS_IMAGE_DICT* image, Bool isMask)
{
  OBJECT imageDict = OBJECT_NOTVM_NOTHING;

  PDF_CHECK_MC(pdfc);
  HQASSERT(image != NULL, "dispatchImage - 'image' cannot be NULL");

  /* Create a temporary PostScript image dictionary to dispatch this image. */
  if ((image->imageType != 12) && (image->imageType != 4))
      image->imageType = 1;
  if (! createPSImageDictionary(pdfc, image, &imageDict))
    return FALSE;

  /* Draw the image. */
  if (! drawPSImage(pdfc, imageDict, isMask))
    return FALSE;

  /* Destroy the dispatch dictionary. */
  destroyPSImageDictionary(pdfc, &imageDict);

  return TRUE;
}

/**
\brief
Dispatch a masked image. This can be a color-key masked image (Postscript
type 4 masked image), or a regular masked image (Postscript type 3 masked
image).
*/
static Bool dispatchMaskedImage(PDFCONTEXT* pdfc, PS_IMAGE_DICT* image)
{
  Bool keyedMask = FALSE;
  OBJECT imageDict = OBJECT_NOTVM_NOTHING;
  PS_IMAGE_DICT mask = {0} ;
  Bool rendered = FALSE ;

  HQASSERT(image != NULL, "dispatchMaskedImage - 'image' cannot be NULL");
  HQASSERT(image->mask != NULL, "dispatchMaskedImage - image is not masked.");

  /* Setup the masked image dictionary depending on the type of mask (either
     a color key or regular mask). */
  if (oType(*image->mask) == OARRAY || oType(*image->mask) == OPACKEDARRAY) {
    OBJECT maskColor = OBJECT_NOTVM_NULL;
    keyedMask = TRUE;

    /* Color key mask. Postscript LL3 type 4 image dictionary */
    image->imageType = 4;
    if (! createPSImageDictionary(pdfc, image, &imageDict))
      return FALSE;

    if (! pdf_copyobject(pdfc, image->mask, &maskColor))
      return FALSE;

    if (! pdf_resolvexrefs(pdfc, &maskColor) ||
        ! pdf_fast_insert_hash_name(pdfc, &imageDict, NAME_MaskColor, &maskColor)) {
      pdf_freeobject(pdfc, &maskColor);
      return FALSE;
    }
  }
  else {
    /* Masked image. Postscript LL3 type 3 image dictionary. */
    OBJECT* maskDict;
    Bool isMask;

    if (oType(*image->mask) != OFILE)
      return error_handler(TYPECHECK);

    maskDict = streamLookupDict(image->mask);
    if (maskDict == NULL)
      return error_handler(UNDEFINED);

    if (! readImageAndSetColorSpace(pdfc, maskDict, image->mask, &mask,
                                    &isMask, &rendered))
      return FALSE ;
    HQASSERT( !rendered, "readImageAndSetColorSpace returns rendered=TRUE when retrieving mask" );

    /* Ensure that the mask is actually a mask. Additionally it may not be
    softMasked. */
    if (! isMask || mask.softMaskStream != NULL)
      return error_handler(TYPECHECK);

    if (! createPSMaskedImageDictionary(pdfc, image, &mask, &imageDict, NULL,
                                        NULL))
      return FALSE;
  }

  /* Draw the image. */
  if (! drawPSImage(pdfc, imageDict, FALSE))
    return FALSE;

  /* Destroy the dispatch dictionary. */
  destroyPSImageDictionary(pdfc, &imageDict);

  return TRUE;
}

static Bool createType12SoftMaskDict(PDFCONTEXT* pdfc,
                                     PS_IMAGE_DICT* image,
                                     PS_IMAGE_DICT* softMask,
                                     OBJECT *decode)
{
  OBJECT *softMaskDict ;
  PDFXCONTEXT *pdfxc ;
  OBJECT value = OBJECT_NOTVM_NOTHING ;

  PDF_GET_XC( pdfxc ) ;

  softMaskDict = image->softMaskDict = PDF_ALLOCOBJECT( pdfxc, 1 ) ;
  if ( softMaskDict == NULL )
    return error_handler(VMERROR);
  if ( ! pdf_create_dictionary(pdfc, IMAGE_DICT_SIZE, softMaskDict))
    return FALSE ;

  object_store_integer(&value, 1) ;
  if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_ImageType, &value) )
    return FALSE ;

  object_store_integer(&value, softMask->width) ;
  if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_Width, &value) )
    return FALSE ;

  object_store_integer(&value, softMask->height) ;
  if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_Height, &value) )
    return FALSE ;

  object_store_integer(&value, softMask->bpc) ;
  if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_BitsPerComponent, &value) )
    return FALSE ;

  if ( !pdf_resolvexrefs(pdfc, &softMask->imageMatrix) ||
       !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_ImageMatrix, &softMask->imageMatrix) ||
       !pdf_resolvexrefs(pdfc, decode) ||
       !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_Decode, decode) )
    return FALSE ;

  return TRUE ;
}

/**
 * Softmask images allow the width and height to vary independently between
 * image and alpha, but type 12 images do not.  Therefore we need to rescale the
 * image or the alpha to make them compatible.
 */
static Bool rescaleImageType12(PDFCONTEXT *pdfc,
                               PS_IMAGE_DICT *image, OBJECT *imageDict,
                               PS_IMAGE_DICT *softMask, OBJECT *softMaskDict)
{
  OBJECT value = OBJECT_NOTVM_NOTHING;
  int32 targetW = image->width, targetH = image->height;

#define SOFTMASK_MIN_FACTOR (10) /* chosen arbitrarily */
#define ANY_RESCALING_DIM (20) /* chosen arbitrarily, but don't make too big */
#define ROUNDUP(_a, _b) ((((_a) + (_b) - 1) / (_b)) * (_b))

  /* On very small images ensure any rescaling is a multiple of the original
     size to avoid the risk of interpolation introducing artifacts.  Otherwise
     rescale the alpha to match the image, except when the alpha is considerably
     larger and then the image is upscaled to match the alpha. */
  if ( image->width != softMask->width ) {
    if ( image->width < ANY_RESCALING_DIM && softMask->width < ANY_RESCALING_DIM )
      targetW = ulcm(image->width, softMask->width); /* lowest commom multiple */
    else if ( image->width < ANY_RESCALING_DIM )
      targetW = ROUNDUP(softMask->width, image->width);
    else if ( softMask->width < ANY_RESCALING_DIM )
      targetW = ROUNDUP(image->width, softMask->width);
    else if ( softMask->width >= image->width * SOFTMASK_MIN_FACTOR )
      targetW = softMask->width;
  }

  if ( image->height != softMask->height ) {
    if ( image->height < ANY_RESCALING_DIM && softMask->height < ANY_RESCALING_DIM )
      targetH = ulcm(image->height, softMask->height); /* lowest commom multiple */
    else if ( image->height < ANY_RESCALING_DIM )
      targetH = ROUNDUP(softMask->height, image->height);
    else if ( softMask->height < ANY_RESCALING_DIM )
      targetH = ROUNDUP(image->height, softMask->height);
    else if ( softMask->height >= image->height * SOFTMASK_MIN_FACTOR )
      targetH = softMask->height;
  }

#if 0
  monitorf((uint8*)"Rescaling type 12: im (%d, %d); sm (%d, %d) -> res (%d, %d)\n",
           image->width, image->height, softMask->width, softMask->height,
           targetW, targetH);
#endif

  /* If setting the interpolate flag set both interpolation and averaging
     targets, otherwise filtering may activate incorrectly.  Averaging is a
     no-op. */
  if ( targetW != image->width || targetH != image->height ) {
    HQASSERT(targetW >= image->width && targetH >= image->height,
             "Shouldn't be sub-sampling image data");

    object_store_integer(&value, targetW);
    if ( !pdf_fast_insert_hash_name(pdfc, imageDict, NAME_GG_TargetInterpWidth, &value) ||
         !pdf_fast_insert_hash_name(pdfc, imageDict, NAME_GG_TargetAverageWidth, &value) )
      return FALSE;

    object_store_integer(&value, targetH);
    if ( !pdf_fast_insert_hash_name(pdfc, imageDict, NAME_GG_TargetInterpHeight, &value) ||
         !pdf_fast_insert_hash_name(pdfc, imageDict, NAME_GG_TargetAverageHeight, &value) )
      return FALSE;

    object_store_bool(&value, TRUE);
    if ( !pdf_fast_insert_hash_name(pdfc, imageDict, NAME_Interpolate, &value) )
      return FALSE;
  }

  /* For the softmask, may need to super-sample in one dimension and sub-sample
     in the other.  This assumes interpolation filtering happens before averaging. */
  if ( targetW != softMask->width || targetH != softMask->height ) {
    object_store_integer(&value, targetW >= softMask->width ? targetW : softMask->width);
    if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_GG_TargetInterpWidth, &value) )
      return FALSE;

    object_store_integer(&value, targetW);
    if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_GG_TargetAverageWidth, &value) )
      return FALSE;

    object_store_integer(&value, targetH >= softMask->height ? targetH : softMask->height) ;
    if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_GG_TargetInterpHeight, &value) )
      return FALSE;

    object_store_integer(&value, targetH);
    if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_GG_TargetAverageHeight, &value) )
      return FALSE;

    object_store_bool(&value, TRUE);
    if ( !pdf_fast_insert_hash_name(pdfc, softMaskDict, NAME_Interpolate, &value) )
      return FALSE;
  }

  return TRUE;
}

/**
 * Softmask images are preferably handled by converting them to type 12 images
 * (like a masked image but with an alpha channel instead of a 1 bit mask).
 *
 * Softmask images with a matte color set up an image filter to unblend the
 * matte color and alpha from the image data.  If doing transparent RLE output
 * the mask must additionally be placed into a separate softmask group, and the
 * alpha channel in the type 12 is used only for unblending.  Reading the alpha
 * channel twice requires layering an RSD on the mask data source.
 */
static Bool dispatchImageType12(PDFCONTEXT *pdfc, PS_IMAGE_DICT *image,
                                PS_IMAGE_DICT *softMask)
{
  USERPARAMS *userparams = pdfc->corecontext->userparams;
  OBJECT imageDict = OBJECT_NOTVM_NOTHING, *dataDict, *maskDict;

  HQASSERT(!DOING_TRANSPARENT_RLE(pdfc->corecontext->page) ||
           oType(softMask->matteColor) == OARRAY ||
           oType(softMask->matteColor) == OPACKEDARRAY,
           "Only matted softmasks allowed if doing transparent RLE output");

  /* For performance type 12 images ignore the interpolate flag supplied by the
     job and only interpolate when image and alpha dimensions differ. */
  image->interpolate = NULL;
  softMask->interpolate = NULL;

  image->imageType = 12;
  softMask->imageType = 1;

  if ( !createType12SoftMaskDict(pdfc, image, softMask, &softMask->decode) ||
       !createPSImageDictionary(pdfc, image, &imageDict) )
    return FALSE;

  dataDict = fast_extract_hash_name(&imageDict, NAME_DataDict);
  maskDict = image->softMaskDict;

  if ( DOING_TRANSPARENT_RLE(pdfc->corecontext->page) ) {
    /* Using the softmask twice, so layer an RSD on top. */
    OBJECT emptyDict = OBJECT_NOTVM_NOTHING;
    OBJECT rsdName = OBJECT_NOTVM_NAME(NAME_ReusableStreamDecode, LITERAL);
    if ( !pdf_create_dictionary(pdfc, 0, &emptyDict) ||
         !pdf_createfilter(pdfc, &softMask->datasource, &rsdName, &emptyDict,
                           FALSE) )
      return FALSE;
  }

  if ( !pdf_fast_insert_hash_name(pdfc, maskDict, NAME_DataSource,
                                  &softMask->datasource) )
    return TRUE;

  if ( DOING_TRANSPARENT_RLE(pdfc->corecontext->page) ) {
    FILELIST *softMaskData;
    Hq32x2 filepos;

    /* Install the soft mask as a gray image in a softmask group. */
    if ( !pdf_setGrayImageAsSoftMask(pdfc, *maskDict) )
      return FALSE;

    /* Rewind the softmask file stream. */
    softMaskData = oFile(softMask->datasource);
    Hq32x2FromUint32(&filepos, 0);
    if ( (*theIMyResetFile(softMaskData))(softMaskData) == EOF ||
         (*theIMySetFilePos(softMaskData))(softMaskData, &filepos) == EOF )
      return (*theIFileLastError(softMaskData))(softMaskData);
  }

  /* Rescale either the image or alpha so they have the same dimensions. */
  if ( softMask->width != image->width || softMask->height != image->height )
    if ( !rescaleImageType12(pdfc, image, dataDict, softMask, maskDict) )
      return FALSE;

  /* The image data is preblended with the matte color and alpha. Add Matted
     entries to activate the alphaDiv image filter to undo the preblending. */
  if ( oType(softMask->matteColor) == OARRAY ||
       oType(softMask->matteColor) == OPACKEDARRAY )
    if ( !pdf_resolvexrefs(pdfc, &softMask->matteColor) ||
         !pdf_fast_insert_hash_name(pdfc, maskDict, NAME_Matte, &softMask->matteColor) ||
         !pdf_fast_insert_hash_name(pdfc, maskDict, NAME_Matted, &tnewobj) ||
         !pdf_fast_insert_hash_name(pdfc, dataDict, NAME_Matted, &tnewobj) )
      return FALSE;

  {
    QuadState InterpolateAllImages = userparams->InterpolateAllImages;
    QuadState InterpolateAllMasks  = userparams->InterpolateAllMasks;
    Bool result;

    /* Type 12 images only interpolate if rescaleImageType12 says so.
       Otherwise the image and alpha are in danger of being incompatible. */
    if ( ! quadStateSetFromName(&userparams->InterpolateAllImages,
                                NAME_DefaultFalse) ||
         ! quadStateSetFromName(&userparams->InterpolateAllMasks,
                                NAME_DefaultFalse))
      return FALSE;

    result = drawPSImage(pdfc, imageDict, FALSE);

    userparams->InterpolateAllImages = InterpolateAllImages;
    userparams->InterpolateAllMasks  = InterpolateAllMasks;

    destroyPSImageDictionary(pdfc, &imageDict);
    return result;
  }
}

/**
 * Softmasked images have image and mask data.  Ideally they are converted into
 * type 12 images but this relies on image and mask data having the same
 * dimensions or being able to turn on image filtering to make them the same.
 * If this isn't possible or is disallowed, the mask data is put into a separate
 * softmask group as a gray image. The image data is then handled as a normal
 * image.
 *
 * In all cases the soft mask overrides any regular mask (if provided), as
 * specified in the PDF manual.
 */
static Bool dispatchSoftMaskedImage(PDFCONTEXT *pdfc, PS_IMAGE_DICT *image)
{
  PS_IMAGE_DICT softMask = { 0 };

  PDF_CHECK_MC(pdfc);
  HQASSERT(image != NULL, "dispatchSoftMaskedImage - 'image' cannot be NULL");

  /* A softmask image overrides a softmask group.
     The softmask group is grestored in the caller. */
  if ( !tsSetSoftMask(gsTranState(gstateptr), EmptySoftMask,
                      HDL_ID_INVALID, gstateptr->colorInfo) )
    return FALSE;

  /* Get hold of the soft mask details. */
  if ( !readSoftMaskImage(pdfc, *image->softMaskStream, &softMask) )
    return FALSE;

  /* Matted softmask images must have equal image and mask dimensions. */
  if ( oType(softMask.matteColor) != ONULL &&
       (softMask.width != image->width || softMask.height != image->height) )
    return error_handler(RANGECHECK);

  /* Dispatching the softmask image as a type 12 image is substantially faster
     than putting the softmask into a separate softmask group, but transparent
     RLE output doesn't support type 12 images.  Softmasks with matte create
     type 12 images for the matte unblending, but must also put the mask into a
     separate softmask group for transparent RLE output.  Note, when transparent
     RLE does support type 12 images, code for transparent RLE in
     dispatchImageType12 can be removed. */
  if ( !DOING_TRANSPARENT_RLE(pdfc->corecontext->page) ||
       oType(softMask.matteColor) != ONULL ) {
    if ( !dispatchImageType12(pdfc, image, &softMask) )
      return FALSE;
  } else {
    OBJECT softMaskDict = OBJECT_NOTVM_NOTHING;

    /* Install the soft mask as a gray image in a softmask group. */
    if ( !createPSImageDictionary(pdfc, &softMask, &softMaskDict) ||
         !pdf_setGrayImageAsSoftMask(pdfc, softMaskDict) )
      return FALSE;

    destroyPSImageDictionary(pdfc, &softMaskDict);

    /* Dispatch the image data as a normal image;
       the mask has been handled separately. */
    if ( !dispatchImage(pdfc, image, FALSE) )
      return FALSE;
  }

  return TRUE;
}

/** \brief
Dispatch a PDF image.

Images get wrapped in PostScript structures and are ultimately executed by
PostScript routines. Several types of image are supported; pure images, pure
masks, masked images, soft masked images, and matted soft masked images.
*/
Bool pdfimg_dispatch(PDFCONTEXT *pdfc, OBJECT *dict, OBJECT *source)
{
  ps_context_t *pscontext ;
  Bool isMask;
  Bool result;
  PS_IMAGE_DICT data = {0} ;
  Bool rendered = FALSE ;
  int32 maindictresult ;

  PDF_CHECK_MC(pdfc);
  HQASSERT(dict != NULL && source != NULL,
           "pdfimg_dispatch - parameters cannot be NULL");

  HQASSERT(pdfc->pdfxc != NULL, "No PDF execution context") ;
  HQASSERT(pdfc->pdfxc->corecontext != NULL, "No core context") ;
  pscontext = pdfc->pdfxc->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  /* Enclose the image operation in a gsave/restore. */
  if (! gsave_(pscontext))
    return FALSE;

  /* Extract the main dictionary */
  maindictresult = readImageAndSetColorSpace(pdfc, dict, source,
                                             &data, &isMask, &rendered) ;
  if ( ! maindictresult ) {
    (void)grestore_(pscontext) ;
    return FALSE;
  }
  if ( maindictresult && rendered )
    /* The image was rendered throught the OPI procset */
    return grestore_(pscontext) ;

  /* Dispatch this image to the correct handler. */
  if (isMask) {
    /* This image is a simple imagemask. */
    result = dispatchImage(pdfc, &data, isMask);
  } else if (data.softMaskStream != NULL) {
    /* This is soft masked image. The soft mask overrides any regular mask. */
    result = dispatchSoftMaskedImage(pdfc, &data);
  } else if (data.mask != NULL) { /* This is a masked image. */
    result = dispatchMaskedImage(pdfc, &data);
  } else { /* This is a simple image. */
    result = dispatchImage(pdfc, &data, FALSE);
  }

  /* Grestore regardless of the image dispatch result. */
  if (! grestore_(pscontext))
    result = FALSE;

  return result;
}


/* Log stripped */
