/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:tiffexec.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * TIFF execution operators.
 *
 */

/** \todo ajcd 2005-02-24: Review this file to avoid going
 * through PS operators for trivial functionality.
 */

#include "core.h"
#include "coreinit.h"
#include "swerrors.h"   /* STACKUNDERFLOW */
#include "swcopyf.h"    /* swcopyf() */
#include "mm.h"         /* mm_pool_size */
#include "mmcompat.h"   /* mm_alloc_static */
#include "objects.h"    /* OBJECT */
#include "dictscan.h"   /* NAMETYPEMATCH */
#include "fileio.h"     /* FILELIST */
#include "monitor.h"
#include "namedef_.h"   /* NAME_... */

#include "stacks.h"     /* theStackSize */
#include "params.h"     /* SystemParams */
#include "psvm.h"       /* theISaveLangLevel */
#include "stackops.h"   /* fnewobj */
#include "dicthash.h"   /* fast_insert_hash */
#include "graphics.h"   /* GSTATE */
#include "gschcms.h"    /* gsc_getColorSpaceOverride */
#include "gstate.h"     /* gstateptr */
#include "gs_color.h"   /* COLORSPACE_ID */
#include "gschead.h"    /* gsc_setcolorspacedirect() */
#include "hqmemcmp.h"   /* HqMemCmp */
#include "miscops.h"    /* run_ps_string() */

#include "tiffmem.h"    /* tiff_alloc_psarray() */
#include "ifdreadr.h"
#include "tifcntxt.h"   /* tiff_context_t */
#include "tifftags.h"   /* tiff_orientation_string() */
#include "tiffexec.h"
#include "t6params.h"   /* TIFF6Params */

#include "tiffclsp.h"
#include "tifffilter.h"

/*
 * Sundry magic numbers
 */
#define TIFF_IMAGE_DICT_SIZE  (10)

/*
 * Array of PS transformation matrixes.
 */
static USERVALUE orientation_matrix[9][6] = {
  {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f },   /* UNUSED */
  {  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f },   /* As-is */
  { -1.0f,  0.0f,  0.0f,  1.0f,  1.0f,  0.0f },   /* LR swap */
  { -1.0f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f },   /* 180 rotate */
  {  1.0f,  0.0f,  0.0f, -1.0f,  0.0f,  1.0f },   /* TB flip */
  {  0.0f, -1.0f, -1.0f,  0.0f,  1.0f,  1.0f },   /* TLBR axis */
  {  0.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f },   /* 90 CW */
  {  0.0f,  1.0f,  1.0f,  0.0f,  0.0f,  0.0f },   /* TRBL axis */
  {  0.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f }    /* 90 CCW */
};

/*
 * Gray decode array values
 */
static NAMECACHE* gray_channel_name[1] = {
  &system_names[NAME_Gray]
};

/*
 * 1976 CIE L*a*b* decode array values
 */
static NAMECACHE* lab_channel_name[3] = {
  &system_names[NAME_L],
  &system_names[NAME_a],
  &system_names[NAME_b]
};

/*
 * RGB decode array values
 */
static NAMECACHE* rgb_channel_name[3] = {
  &system_names[NAME_Red],
  &system_names[NAME_Green],
  &system_names[NAME_Blue]
};

/*
 * CMYK decode array values - overwritten on setup
 */
static NAMECACHE* cmyk_channel_name[4] = {
  &system_names[NAME_Cyan],
  &system_names[NAME_Magenta],
  &system_names[NAME_Yellow],
  &system_names[NAME_Black]
};

/*
 * Color space information indexed of the PhotometricInterpretation
 */
static tiff_color_space_t color_spaces[PMI_MaxCount] = {
  { TRUE,  1, SPACE_DeviceGray, &system_names[NAME_DeviceGray],
    gray_channel_name},                                   /* Gray, 0 is white */
  { TRUE,  1, SPACE_DeviceGray, &system_names[NAME_DeviceGray],
    gray_channel_name},                                   /* Gray, 1 is white */
  { TRUE,  3, SPACE_DeviceRGB,  &system_names[NAME_DeviceRGB],
    rgb_channel_name},                                    /* RGB */
  { TRUE,  1, SPACE_Indexed,  &system_names[NAME_Indexed],
    rgb_channel_name},                                    /* Palette */
  { FALSE, },                                             /* Transparency */
  { TRUE,  4, SPACE_DeviceCMYK, &system_names[NAME_DeviceCMYK],
    cmyk_channel_name},                                   /* CMYK */
  { FALSE, },                                             /* YCbCr */
  { FALSE, },                                             /* Unspecified */
  { TRUE,  3, SPACE_Lab,  &system_names[NAME_Lab],
    lab_channel_name},                                    /* CIE L*a*b* */
  { TRUE,  3, SPACE_Lab,  &system_names[NAME_Lab],
    lab_channel_name},                                    /* ICC Lab */
};

/*
 * Image matrix values - overwritten on setup
 */
static USERVALUE image_matrix_values[6] = {
  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
};


/*
 * tiffexec_start()
 */
static Bool tiffexec_start(
  corecontext_t     *corecontext,
  tiff_context_t**  pp_context,     /* O */
  int32*            p_num_args)     /* O */
{
  /*
   * tiffexec dict entries.
   */
  enum {
    eIgnoreOrientation = 0,
    eAdjustCTM,
    eDoSetColorSpace,
    eInstallICCProfile,
    eInvertImage,
    eDoImage,
    eDoPageSize,
    eDefaultResolution,

    eMaxTiffExecIndexes
  };

  static NAMETYPEMATCH tiffexec_match[eMaxTiffExecIndexes + 1] = {
    { NAME_IgnoreOrientation | OOPTIONAL , 1, { OBOOLEAN }},
    { NAME_AdjustCTM | OOPTIONAL , 1, { OBOOLEAN }},
    { NAME_DoSetColorSpace | OOPTIONAL , 1, { OBOOLEAN }},
    { NAME_InstallICCProfile | OOPTIONAL , 1, { OBOOLEAN }},
    { NAME_InvertImage | OOPTIONAL , 1, { OBOOLEAN }},
    { NAME_DoImageMask | OOPTIONAL , 1, { OBOOLEAN }},
    { NAME_DoPageSize | OOPTIONAL , 1, { OBOOLEAN }},
    { NAME_DefaultResolution | OOPTIONAL , 2, { OPACKEDARRAY,OARRAY }},

    DUMMY_END_MATCH
  };

  int32           stack_size;
  int32           type;
  OBJECT*         odict;
  OBJECT*         ofile;
  FILELIST*       flptr;
  tiff_file_t*    p_file;
  OBJECT*         obj;
  tiff_context_t*  p_context;


  HQASSERT((pp_context != NULL),
           "tiffexec_start: NULL pointer to returned context pointer");
  HQASSERT((p_num_args != NULL),
           "tiffexec_start: NULL pointer to returned number of args to pop");

  *pp_context = NULL;

  /* Initially nothing should be popped */
  *p_num_args = 0;

  /* Check for some candidate args */
  if ( isEmpty(operandstack) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  /* Get first arg from top of stack */
  stack_size = theStackSize(operandstack);
  obj = TopStack(operandstack, stack_size);
  type = oType(*obj);

  if ( type == ODICTIONARY ) {
    /* Got dict - look for file next */
    odict = obj;

    if ( stack_size < 1 ) {
      /* Got dict but there is no file object for sure */
      return error_handler(STACKUNDERFLOW) ;
    }

    /* Check next arg is a file */
    ofile = (&odict[-1]);
    if ( !fastStackAccess(operandstack) ) {
      ofile = stackindex(1, &operandstack);
    }
    type = oType(*ofile);
    if ( type != OFILE ) {
      return error_handler(TYPECHECK) ;
    }

    /* 2 arg form so pop 2 */
    *p_num_args = 2;

    /* Check dict entries */
    if ( !dictmatch(odict, tiffexec_match) ) {
      return FALSE ;
    }

  } else if ( type == OFILE ) {
    /* Single arg form - just a file */
    ofile = obj;
    odict = NULL;

    /* 1 arg form so pop 1 */
    *p_num_args = 1;

  } else { /* Top arg was neither file or dict - whoops */
    return error_handler(TYPECHECK) ;
  }

  /* Check TIFF6 input is enabled */
  if ( !corecontext->systemparams->Tiff6 ) {
    return error_handler(INVALIDACCESS) ;
  }

  /* Check file is ok to use */
  flptr = oFile(*ofile);
  if ( !isIOpenFileFilter(ofile, flptr) ) {
    return detail_error_handler(IOERROR, "TIFF input is not open.") ;
  }
  if ( !isIInputFile(flptr) ) {
    return detail_error_handler(IOERROR,
                                "TIFF input is not open for reading.");
  }
  if ( (!isIFilter(flptr) || !isIRSDFilter(flptr)) && isIEof(flptr) ) {
    return detail_error_handler(IOERROR, "TIFF input is already at end.") ;
  }
  if ( !isIFilter(flptr) && isIStdFile(flptr) ) {
    return detail_error_handler(IOERROR,
                                "Trying to read TIFF embedded in current file.");
  }

  /* The source needs to be seekable - if it's not, then layer an RSD on it. */
  if ( !tiff_file_seekable(flptr))  {
    if ( !filter_layer(flptr,
                       NAME_AND_LENGTH("ReusableStreamDecode"),
                       NULL, &flptr) )
      return FALSE ;
  }

  /* Ok, all args are ok, start creating all the contexts required. */
  if ( !tiff_new_context(corecontext, pp_context) ) {
    return FALSE ;
  }

  p_context = *pp_context;

  if ( !tiff_new_file(flptr, p_context->mm_pool, &p_file) ) {
    return FALSE ;
  }
  if ( !tiff_new_reader(corecontext, p_file, p_context->mm_pool,
                        &(p_context->p_reader)) ) {
    tiff_free_file(&p_file);
    return FALSE ;
  }

  if ( odict != NULL ) {
    /* Got arg dict - check entry values */
    /* IgnoreOrientation */
    if ( (obj = tiffexec_match[eIgnoreOrientation].result) != NULL ) {
      p_context->f_ignore_orientation = oBool(*obj);
    }
    /* AdjustCTM */
    if ( (obj = tiffexec_match[eAdjustCTM].result) != NULL ) {
      p_context->f_adjust_ctm = oBool(*obj);
    }
    /* DoSetColorSpace */
    if ( (obj = tiffexec_match[eDoSetColorSpace].result) != NULL ) {
      p_context->f_do_setcolorspace = oBool(*obj);
    }
    /* InstallICCProfile */
    if ( (obj = tiffexec_match[eInstallICCProfile].result) != NULL ) {
      p_context->f_install_iccprofile = oBool(*obj);
    }
    /* InvertImage */
    if ( (obj = tiffexec_match[eInvertImage].result) != NULL ) {
      p_context->f_invert_image = oBool(*obj);
    }
    /* DoImageMask */
    if ( (obj = tiffexec_match[eDoImage].result) != NULL ) {
      p_context->f_do_imagemask = oBool(*obj);
    }

    /* DoPageSize */
    if ( (obj = tiffexec_match[eDoPageSize].result) != NULL ) {
      p_context->f_do_pagesize = oBool(*obj);
    }

    if ( (obj = tiffexec_match[eDefaultResolution].result) != NULL ) {
      OBJECT * arr = oArray(*obj);
      int32 len = theLen(*obj);

      if (len != 2)
        return error_handler( RANGECHECK ) ;

      if (!object_get_real(arr++, &p_context->defaultresolution[0]))
        return FALSE;

      if (!object_get_real(arr, &p_context->defaultresolution[1]))
        return FALSE;

      if ((p_context->defaultresolution[0] <= 0.0)||
          (p_context->defaultresolution[1] <= 0.0)) {
        return error_handler( RANGECHECK ) ;
      }
    }
  }

  /* Remember the seekable TIFF file object */
  Copy(&(p_context->ofile_tiff), ofile);

  /* Read in file header, first IFD, and check for valid tiff 6 */
  if ( !tiff_read_header(corecontext, p_context->p_reader) ||
       !ifd_read_ifds(ifd_reader_from_tiff(p_context->p_reader), &(p_context->number_images)) ) {
    return FALSE ;
  }

  /* Check that we did actually find some IFDs! */
  if ( p_context->number_images == 0 ) {
    return detail_error_handler(CONFIGURATIONERROR,
                                "TIFF file has no images in it.");
  }

  return TRUE ;

} /* Function tiffexec_start */


/*
 * tiffexec_end()
 */
static Bool tiffexec_end(
  tiff_context_t**  pp_context)     /* O */
{
  Bool   result = TRUE;

  HQASSERT((pp_context != NULL),
           "tiffexec_end: NULL pointer to context pointer");

  /* Free off tiff context if we have one */
  if ( *pp_context != NULL ) {
    tiff_free_context(pp_context);
  }

  return result ;

} /* Function tiffexec_end */


/*
 * tiff_init_real_array()
 */
static Bool tiff_init_real_array(
  mm_pool_t   mm_pool,      /* I */
  USERVALUE*  values,       /* I */
  uint32      count,        /* I */
  uint32      count_zero,   /* I */
  OBJECT*     oarray)       /* O */
{
  uint32   i;
  uint32   size;
  OBJECT* oelement;

  HQASSERT((values != NULL),
           "tiff_init_real_array: NULL pointer to values");
  HQASSERT((count > 0),
           "tiff_init_real_array: no values to init array with");
  HQASSERT((oarray != NULL),
           "tiff_init_array: NULL array object pointer");

  /* Size of array including extra 0s */
  size = count + count_zero;

  /* Create PS array */
  if ( !tiff_alloc_psarray(mm_pool, size, oarray) ) {
    return FALSE ;
  }

  /* Filll array with values */
  oelement = oArray(*oarray);
  for ( i = 0; i < count; i++ ) {
    object_store_real(&oelement[i], values[i]);
  }
  for ( ; i < size; i++ ) {
    object_store_real(&oelement[i], 0.0f);
  }

  return TRUE ;

} /* Function tiff_init_real_array */


/*
 * tiff_init_dict_common()
 */
static Bool tiff_init_dict_common(
  tiff_image_data_t*  p_data,       /* I */
  OBJECT*             omatrix_image, /* I */
  OBJECT*             odict)        /* O */
{
  OBJECT intobj = OBJECT_NOTVM_NOTHING;

  /* Fill the image dictionary in ... */
  /* ImageType - 1 */
  object_store_integer(&intobj, 1);
  if ( !tiff_insert_hash(odict, NAME_ImageType, &intobj) ) {
    return FALSE ;
  }

  /* Width */
  object_store_integer(&intobj, (int32)(p_data->image_width));
  if ( !tiff_insert_hash(odict, NAME_Width, &intobj) ) {
    return FALSE ;
  }

  /* Height */
  object_store_integer(&intobj, (int32)(p_data->image_length));
  if ( !tiff_insert_hash(odict, NAME_Height, &intobj) ) {
    return FALSE ;
  }

  /* ImageMatrix */
  if ( !tiff_insert_hash(odict, NAME_ImageMatrix, omatrix_image) ) {
    return FALSE ;
  }

  /* BitsPerComponent */
  object_store_integer(&intobj, (int32)(p_data->bits_per_sample));
  if ( !tiff_insert_hash(odict, NAME_BitsPerComponent, &intobj) ) {
    return FALSE ;
  }

  /* Interpolate - always FALSE */
  if ( !tiff_insert_hash(odict, NAME_Interpolate, &fnewobj) ) {
    return FALSE ;
  }

  return TRUE;
}



/*
 * tiff_init_dict()
 */
static Bool tiff_init_dict(
  mm_pool_t           mm_pool,      /* I */
  tiff_image_data_t*  p_data,       /* I */
  OBJECT*             omatrix_image, /* I */
  OBJECT*             decode,       /* I */
  OBJECT*             data_source,  /* I */
  OBJECT*             odict)        /* O */
{
  HQASSERT((p_data != NULL),
           "tiff_init_dict: NULL pointer to image data");

  /* Create the image dictionary */
  if ( !tiff_alloc_psdict(mm_pool, TIFF_IMAGE_DICT_SIZE, odict) ) {
    return FALSE ;
  }


  if (!tiff_init_dict_common(p_data, omatrix_image, odict))
    return FALSE;

  /* Decode */
  if ( !tiff_insert_hash(odict, NAME_Decode, decode) ) {
    return FALSE ;
  }

  /* MultipleDataSources - TRUE iff planar TIFF image */
  if ( !tiff_insert_hash(odict, NAME_MultipleDataSources,
                         (p_data->planar_config == PLANAR_CONFIG_PLANAR)
                         ? &tnewobj : &fnewobj) ) {
    return FALSE ;
  }

  /* DataSource */
  if ( !tiff_insert_hash(odict, NAME_DataSource, data_source) ) {
    return FALSE ;
  }

  return TRUE ;

} /* Function tiff_init_dict */


/* Create a type image dict with associated image and mask dicts
   The mask dict is altered to indicate its a softmask.*/
static Bool tiff_init_alphadict(
  ps_context_t       *pscontext,    /* I */
  mm_pool_t           mm_pool,      /* I */
  tiff_image_data_t*  p_data,       /* I */
  OBJECT*             omatrix_image, /* I */
  OBJECT*             decode,       /* I */
  OBJECT*             data_source, /* I */
  int32               fImagemask)  /* I */
{
  OBJECT odict = OBJECT_NOTVM_NULL ;
  OBJECT idict = OBJECT_NOTVM_NULL ;
  OBJECT mdict = OBJECT_NOTVM_NULL ;
  Bool result;
  OBJECT intobj = OBJECT_NOTVM_NOTHING ;
  OBJECT oarray_decode = OBJECT_NOTVM_NULL ;

  HQASSERT((p_data != NULL),
           "tiff_init_alphadict: NULL pointer to image data");

  /* Create the parent dictionary */
  result = tiff_alloc_psdict(mm_pool, TIFF_IMAGE_DICT_SIZE, &odict) ;

  /* Fill the image dictionary in ... */
  if ( result ) {
    /* ImageType - 12 */
    object_store_integer(&intobj, 12);
    result = tiff_insert_hash(&odict, NAME_ImageType, &intobj);
  }

  if (result) {
    object_store_integer(&intobj, p_data->planar_config == 1 ? 1 : 3);
    result = tiff_insert_hash(&odict, NAME_InterleaveType, &intobj);
  }

  if (result) {
    /* image dictionary */
    result = tiff_init_dict(mm_pool,p_data,omatrix_image,decode,data_source,&idict);
  }

  if (result) {
    result = tiff_insert_hash(&idict, NAME_PreMult, p_data->premult ? &tnewobj : &fnewobj);
  }

  if (result) {
    result = tiff_insert_hash(&odict, NAME_DataDict, &idict);
  }

  if ( result )
    result = tiff_alloc_psdict(mm_pool, TIFF_IMAGE_DICT_SIZE, &mdict);

  if (result) {
    /* mask dictionary */
    result = tiff_init_dict_common(p_data, omatrix_image, &mdict);
  }

  if (result) {
    USERVALUE maskdecode[2] = { 0.0f, 1.0f } ;
    result = tiff_init_real_array(mm_pool, maskdecode, 2, 0, &oarray_decode);
  }

  if (result) {
    /* Decode */
    result = tiff_insert_hash(&mdict, NAME_Decode, &oarray_decode);
  }

  if (result) {
    result = tiff_insert_hash(&mdict, NAME_ContoneMask, &tnewobj);
  }

  if (result) {
    result = tiff_insert_hash(&odict, NAME_MaskDict, &mdict);
  }

  if ( result ) {
    result = push(&odict, &operandstack) &&
      (fImagemask ? imagemask_(pscontext) : image_(pscontext));
  }

  if ( oType(odict) == ODICTIONARY )
    tiff_free_psdict(mm_pool, &odict);
  if ( oType(mdict) == ODICTIONARY )
    tiff_free_psdict(mm_pool, &mdict);
  if ( oType(idict) == ODICTIONARY)
    tiff_free_psdict(mm_pool, &idict);
  if ( oType(oarray_decode) == OARRAY )
    tiff_free_psarray(mm_pool, &oarray_decode);

  return result ;

} /* Function tiff_init_alphadict */


/*
 * tiff_setpagedevice()
 * This function changes PageSize appropriately for the job.  The context in
 * which we are called, the HqnImage procset, ensures that the setpagedevice is
 * restored away.
 */
static Bool tiff_setpagedevice(
  tiff_context_t*     p_context,    /* I */
  tiff_image_data_t*  p_image_data) /* I */
{
  double width;
  double height;
  Bool callPS = FALSE;

#define PAGEDEVICE_BUF_SIZE 512
  uint8 buf[PAGEDEVICE_BUF_SIZE];
  uint8 *ptr = buf;

  HQASSERT((p_context != NULL),
           "tiff_setpagedevice: NULL context pointer");
  HQASSERT((p_image_data != NULL),
           "tiff_setpagedevice: NULL image data pointer");

  swcopyf(ptr, (uint8 *)"<<");
  ptr += strlen((char *) ptr);

  if ( p_context->f_adjust_ctm && p_context->f_do_pagesize ) {
    /* Calculate width and height */
    width = p_image_data->image_width/p_image_data->x_resolution;
    height = p_image_data->image_length/p_image_data->y_resolution;

    /* Swap values if doing any rotation */
    if ( p_context->f_adjust_ctm && (p_image_data->orientation > 4) ) {
      double tmp = width;
      width = height;
      height = tmp;
    }

    width *= p_image_data->scale_factor;
    height *= p_image_data->scale_factor;

    swcopyf(ptr, (uint8 *)"/PageSize[%f %f] ", width, height);
    ptr += strlen((char *) ptr);

    callPS = TRUE;
  }

  swcopyf(ptr, (uint8 *) ">> setpagedevice");

  if (callPS)
    return run_ps_string(buf) ;
  else
    return TRUE;
} /* Function tiff_setpagedevice */


/*
 * tiff_adjust_ctm()
 */
static Bool tiff_adjust_ctm(
  corecontext_t       *corecontext, /* I */
  tiff_context_t*     p_context,    /* I */
  tiff_image_data_t*  p_image_data) /* I */
{
  Bool   result;
  double  x_size;
  double  y_size;
  double  x_scale;
  double  y_scale;
  double  x_res = p_image_data->x_resolution;
  double  y_res = p_image_data->y_resolution;
  OBJECT  orient_matrix = OBJECT_NOTVM_NOTHING;

  HQASSERT((p_context != NULL),
           "tiff_adjust_ctm: NULL context pointer");
  HQASSERT((p_image_data != NULL),
           "tiff_adjust_ctm: NULL image data pointer");

  if ( p_context->f_ignore_orientation && (p_image_data->orientation != 1) ) {
    /* Reset orientation if we want to ignore what it is */
    if ( corecontext->tiff6params->f_verbose ) {
      monitorf(UVM("%%%%[ Warning: ignoring TIFF image orientation - %s ]%%%%\n"),
               tiff_orientation_string((uint32)p_image_data->orientation));
    }
    p_image_data->orientation = 1;
  }

  if ( p_image_data->orientation < 5 ) {
    /* Image in portrait aspect */
    x_size = (double)p_image_data->image_width;
    y_size = (double)p_image_data->image_length;

  } else { /* Image in landscape aspect */
    double swapper;
    x_size = (double)p_image_data->image_length;
    y_size = (double)p_image_data->image_width;

    swapper = y_res;
    y_res = x_res;
    x_res = swapper;
  }

  x_scale = (x_size/x_res)*p_image_data->scale_factor;
  y_scale = (y_size/y_res)*p_image_data->scale_factor;

  /* Set scale for image */
  if ( !stack_push_real(x_scale, &operandstack) ||
       !stack_push_real(y_scale, &operandstack) ||
       !scale_(corecontext->pscontext) ) {
    return FALSE ;
  }

  if ( p_image_data->orientation > 1 ) {
    /* Adjust CTM for non-trivial orientations */
    theTags(orient_matrix) = OARRAY|LITERAL|UNLIMITED;
    oArray(orient_matrix) = NULL;
    theLen(orient_matrix) = CAST_TO_UINT16(0);

    if ( !tiff_init_real_array(p_context->mm_pool, orientation_matrix[p_image_data->orientation],
                               6, 0, &orient_matrix) ) {
      return FALSE ;
    }
    result = (push(&orient_matrix, &operandstack) && concat_(corecontext->pscontext));

    tiff_free_psarray(p_context->mm_pool, &orient_matrix);

    return result ;
  }

  return TRUE ;

} /* Function tiff_adjust_ctm */


/*
 * tiff_install_iccprofile()
 */
static Bool tiff_install_iccprofile(
  corecontext_t       *corecontext,
  tiff_context_t*     p_context,      /* I */
  tiff_image_data_t*  p_image_data)
{
  int32   icc_channels;
  int32   stack_size;
  OBJECT  *oint, *obool;
  Bool    success = FALSE;

  HQASSERT((p_context != NULL),
           "tiff_install_iccprofile: NULL context pointer");
  HQASSERT((p_image_data != NULL),
           "tiff_install_iccprofile: NULL image data pointer");

  /* Don't bother if overriding CM.
   * N.B. Following assumes we are only interested in intercepting DeviceCMYK,
   * DeviceRGB or DeviceGray.
   */
  if ( ( (int32)(p_image_data->samples_per_pixel) == 4 &&
         gsc_getColorSpaceOverride(gstateptr->colorInfo, SPACE_DeviceCMYK ) ) ||
       ( (int32)(p_image_data->samples_per_pixel) == 3 &&
         gsc_getColorSpaceOverride(gstateptr->colorInfo, SPACE_DeviceRGB  ) ) ||
       ( (int32)(p_image_data->samples_per_pixel) == 1 &&
         gsc_getColorSpaceOverride(gstateptr->colorInfo, SPACE_DeviceGray ) ) ) {
    return TRUE;
  }

  if ( corecontext->tiff6params->f_verbose ) {
    monitorf(UVS("%%%%[ tiffexec: attempting to install ICC profile ]%%%%\n"));
  }

  /* Put profile data length, offset, and file on the stack */
  if ( !stack_push_integer((int32)(p_image_data->icc_length), &operandstack) ||
       !stack_push_integer((int32)(p_image_data->icc_offset), &operandstack) ||
       !push(&(p_context->ofile_tiff), &operandstack) ) {
    return FALSE ;
  }

  /* Create the ICCBased space and get the number of dimensions and devicespace */
  if ( !run_ps_string((uint8*)
                       "dup 3 -1 roll setfileposition exch () /SubFileDecode filter \n"
                      "[/ICCBased 3 -1 roll] dup geticcbasedinfo exch \n") ) {
    return FALSE ;
  }

  /* We should have [/ICCBased OFILE] profile-devicespace profile-dims */
  stack_size = theStackSize(operandstack) ;
  if ( stack_size < 3 ) {
    return detail_error_handler(STACKUNDERFLOW, "Failed to read ICC profile header.") ;
  }

  /* Check the number of profile dimensions */
  oint = TopStack(operandstack, stack_size) ;
  HQASSERT((oType(*oint) == OINTEGER),
           "tiff_install_iccprofile: non-integer dimensions from geticcbasedinfo") ;
  icc_channels = oInteger(*oint) ;

  if (icc_channels > (int32)(p_image_data->samples_per_pixel)) {
    monitorf(UVS("%%%%[ Warning: ignoring TIFF image ICC profile - number of channels unsuitable for image data ]%%%%\n"));
    npop(3, &operandstack) ;
    return TRUE ;
  }

  /* Intercept suitable devicespaces with the ICCBased space.
   * Leave a bool to indicate whether or not the devicespace was suitable.
   * N.B. We only handle DeviceCMYK, DeviceRGB or DeviceGray here.
   */
  if ( !run_ps_string((uint8*)
                      "pop dup dup dup /DeviceCMYK eq \n"
                      "3 -1 roll /DeviceRGB eq 3 -1 roll /DeviceGray eq or or \n"
                      "{ \n"
                      "  << exch 3 -1 roll >> setinterceptcolorspace true \n"
                      "} { \n"
                      "  pop pop false \n"
                      "} ifelse") ) {
    return FALSE ;
  }

  /* Check the stack size - there should be at least the success-bool */
  stack_size = theStackSize(operandstack) ;
  HQASSERT( stack_size >= 1, "tiff_install_iccprofile: unexpected stack size") ;

  /* Check the succes-bool */
  obool = TopStack(operandstack, stack_size) ;
  HQASSERT((oType(*obool) == OBOOLEAN),
           "tiff_install_iccprofile: expected bool on stack") ;

  success = oBool(*obool) ;

  pop(&operandstack);

  if ( !success ) {
    monitorf(UVS("%%%%[ Warning: ignoring TIFF image ICC profile with unexpected colorspace ]%%%%\n"));
  }

  return TRUE ;

} /* Function tiff_install_iccprofile */


/*
 * tiff_setcolorspace()
 */
static Bool tiff_setcolorspace(
  tiff_color_space_t* p_color_space,  /* I */
  tiff_image_data_t*  p_image_data,   /* I */
  mm_pool_t   mm_pool)                /* I */
{
  Bool    result;
  OBJECT  ocspace = OBJECT_NOTVM_NOTHING;
  OBJECT  oclut = OBJECT_NOTVM_NOTHING;

  HQASSERT((p_color_space != NULL),
           "tiff_setcolorspace: NULL color space pointer");
  HQASSERT((p_image_data != NULL),
           "tiff_setcolorspace: NULL image data pointer");

  /* Set colorspace based on PMI interpretation */
  if (!p_image_data->f_plain) {
    if (!tiff_init_noncmyk_colorspace(p_image_data , mm_pool ))
      return FALSE;
    return tiff_set_noncmyk_colorspace(p_image_data);
  } else if ( (p_image_data->samples_per_pixel - p_image_data->alpha) == p_color_space->count_channels ) {

    if ( p_image_data->colormap != NULL) {

      /* setp an indexed colorspace for RGB data */

      /* Set PS arrays to be empty - makes tidy up easier */
      theTags(ocspace) = OARRAY|LITERAL|UNLIMITED;
      oArray(ocspace) = NULL;
      theLen(ocspace) = CAST_TO_UINT16(0);

      /* Create the arrays for setcolorspace, list of colorants ,and the tint transform */
      result = ps_array(&ocspace, 4);

      if ( result ) {
        /* Defining a DeviceN colorspace */
        object_store_name(&oArray(ocspace)[0], NAME_Indexed, LITERAL) ;

        object_store_name(&oArray(ocspace)[1], NAME_DeviceRGB, LITERAL) ;

        object_store_integer(&oArray(ocspace)[2], (1 << p_image_data->bits_per_sample) - 1) ;

        theTags(oclut) = OSTRING | LITERAL ;
        theLen(oclut) = (uint16)(3 << p_image_data->bits_per_sample) ;
        oString(oclut) = p_image_data->colormap ;

        oArray(ocspace)[3] = oclut;

        /* Setup the DeviceN color space */
        result = push(&ocspace, &operandstack) &&
                 gsc_setcolorspace(gstateptr->colorInfo, &operandstack, GSC_FILL);
      }

      return result ;
    } else {
      if ( (p_image_data->pmi == PMI_ICCLab) ||
           (p_image_data->pmi == PMI_CIE_L_a_b_) ) {
        SYSTEMVALUE x,z,xX,yY;
        uint8 buff[128];

        /* here we calculate the reference x and z values from the following formulae
           that converts from 1931 CIE chormaticity to the whitepoint
           refWhite_Y = 1.0
           refWhite_X = whitePoint_x / whitePoint_y
           refWhite_Z  = (1.0 - (whitePoint_x + whitePoint_y)) / whitePoint_y

           Similarly ICCLab is handled this way too with some range changes.
        */

        xX = (SYSTEMVALUE)p_image_data->whitepoint_x[RATIONAL_NUMERATOR]
                  / (SYSTEMVALUE)p_image_data->whitepoint_x[RATIONAL_DENOMINATOR];
        yY = (SYSTEMVALUE)p_image_data->whitepoint_y[RATIONAL_NUMERATOR]
                  / (SYSTEMVALUE)p_image_data->whitepoint_y[RATIONAL_DENOMINATOR];

        x = xX / yY;
        z  = (1.0f - (xX + yY)) / yY;

        if (p_image_data->pmi == PMI_CIE_L_a_b_)
          swcopyf(buff,(uint8 *)"[ /Lab << /WhitePoint [%.4f 1 %.4f] /Range [-128 127 -128 127] >>]",x,z);
        else
          swcopyf(buff,(uint8 *)"[ /Lab << /WhitePoint [%.4f 1 %.4f] /Range [0 255 0 255] >>]",x,z);

        if (!run_ps_string(buff))
          return (FALSE);
        gsc_setcolorspace(gstateptr->colorInfo, &operandstack, GSC_FILL);
      }else{
        /* Can use the base colorspace as is */
        if ( !gsc_setcolorspacedirect(gstateptr->colorInfo, GSC_FILL, p_color_space->space_id) )
          return(FALSE);
      }
    }

    return TRUE ;

  } else if ( p_image_data->samples_per_pixel > p_color_space->count_channels ) {
    return tiff_set_DeviceN_colorspace(p_color_space, p_image_data);
  } else {
    /* Too few samples per pixel - whinge time */
      return detail_error_handler(CONFIGURATIONERROR,
                                  "TIFF file has too few samples per pixel for color space.") ;
  }

} /* Function tiff_setcolorspace */


/** tiffexec_do() does the biz!  Note that we only currently read the
 * first IFD, so second and subsequent IFDs (and hence pages) will
 * be ignored.
 * tiffexec() implements the following effective PS for tiff6 files:
 *
 * \code
 * adjustctm setpagesize and {
 *   << /PageSize [...] >> setpagedevice
 * } if
 * save
 *  adjustctm {
 *    xs ys scale          % Scaling according to res units
 *    [...] concat         % Orientation matrix
 *  } if
 *  dosetcolorspace {
 *    [...] setcolorspace  % According to num samples per pixel
 *  } if
 *  <<...>> image        % Blats the TIFF image
 * restore
 * \endcode
 */
static Bool tiffexec_do(
  ps_context_t       *pscontext,      /* I */
  tiff_context_t*     p_context,      /* I */
  tiff_image_data_t*  p_image_data)   /* I */
{
  corecontext_t *corecontext = ps_core_context(pscontext);
  uint32            i;
  uint32            channel_count;
  uint32            datasrc_count;
  uint32            empty_channels;
  int32             result;
  USERVALUE         dmin;
  USERVALUE         dmax;
  USERVALUE         step;
  USERVALUE         temp;
  OBJECT            omatrix_image = OBJECT_NOTVM_NOTHING;
  OBJECT            oarray_decode = OBJECT_NOTVM_NOTHING;
  OBJECT            data_source = OBJECT_NOTVM_NOTHING;
  OBJECT            oarray_cspace = OBJECT_NOTVM_NOTHING;
  OBJECT*           oelements;
  tiff_color_space_t* p_color_space;
  USERVALUE*        decode_array;

  HQASSERT((p_context != NULL),
           "tiffexec_do: NULL context pointer");
  HQASSERT((p_image_data != NULL),
           "tiffexec_do: NULL image data pointer");

  /* If doing an imagemask then check we have a bilevel image in the first place */
  if ( p_context->f_do_imagemask ) {
    if ( !((p_image_data->pmi == PMI_WhiteIsZero) || (p_image_data->pmi == PMI_BlackIsZero)) ||
         (p_image_data->bits_per_sample > 1) ) {
      return detail_error_handler(UNDEFINED, "Cannot use TIFF image as a mask if it is not bilevel.") ;
    }
  }

  /* Apply scale and concat if requested */
  if ( p_context->f_adjust_ctm &&
       !tiff_adjust_ctm(corecontext, p_context, p_image_data) ) {
    return FALSE ;
  }

  /* If there is an ICC profile try and install it if requested */
  if ( p_image_data->f_icc_profile ) {
    if ( p_context->f_install_iccprofile ) {
      if ( !tiff_install_iccprofile(corecontext, p_context, p_image_data) ) {
        return FALSE ;
      }

    } else if ( corecontext->tiff6params->f_verbose ) {
      monitorf(UVS("%%%%[ Warning: ignoring TIFF image ICC profile ]%%%%\n"));
    }
  }

  /* Define default decode array values */
  dmin = 0.0f;
  dmax = 1.0f;

  /* Set colorspace from PMI if requested */
  p_color_space = &color_spaces[p_image_data->pmi];
  HQASSERT((p_color_space->f_supported),
           "tiffexec_do: pmi color space not supported");
  if ( p_context->f_do_setcolorspace ) {
    if ( !tiff_setcolorspace(p_color_space, p_image_data,p_context->mm_pool) ) {
      return FALSE ;
    }

  } else { /* Check current colorspace is usable unless we are doing an imagemask */

    if (!gsc_currentcolorspace(gstateptr->colorInfo, GSC_FILL, &oarray_cspace))
      return FALSE;

    HQASSERT(oType(oarray_cspace) == OARRAY,
             "tiffexec_do: gsc_currentcolorspace did not return an array");
    oelements = oArray(oarray_cspace);
    HQASSERT(oType(oelements[0]) == ONAME,
             "tiffexec_do: gsc_currentcolorspace array first element not a name");

    switch ( oNameNumber(oelements[0]) ) {
    case NAME_DeviceRGB:
      if ( !p_context->f_do_imagemask && (p_image_data->pmi != PMI_RGB) ) {
        return detail_error_handler(UNDEFINED, "Current colorspace is incompatible with TIFF image.") ;
      }
      break;

    case NAME_DeviceCMYK:
      if ( !p_context->f_do_imagemask && (p_image_data->pmi != PMI_Separated) ) {
        return detail_error_handler(UNDEFINED, "Current colorspace is incompatible with TIFF image.") ;
      }
      break;

    case NAME_DeviceN:
      break;

    case NAME_Indexed:
      HQASSERT((theLen(oarray_cspace) == 4),
               "tiffexec_do: indexed colorspace array incorrect size");
      /* The indexed space hival is needed to update the decode array correctly below */
      dmax = (USERVALUE)(oInteger(oelements[2]));
      /* FALLTHROUGH */
    case NAME_DeviceGray:
    case NAME_Separation:
      if ( !p_context->f_do_imagemask &&
           ((p_image_data->pmi != PMI_WhiteIsZero) && (p_image_data->pmi != PMI_BlackIsZero)) ) {
        return detail_error_handler(UNDEFINED, "Current colorspace is incompatible with TIFF image.") ;
      }
      break;

    default:
      return detail_error_handler(UNDEFINED, "Current colorspace is unknown.") ;
    }
  }

  decode_array = mm_alloc( p_context->mm_pool ,
                     sizeof(USERVALUE) *  2 * p_image_data->samples_per_pixel,
                     MM_ALLOC_CLASS_TIFF_DECODE ) ;
  if (decode_array == NULL)
    return error_handler(VMERROR);

  i = 0;

  /* Set up the decode array based on the colorspace and ht compression */
  switch ( p_image_data->pmi ) {
  case PMI_Separated:
    {
      uint32 count = 4;
      uint32 imax;

      /* Have to set up decode array for CMYK color space based on dot range */
      step = 1.0f / (p_image_data->dot_range[1] - p_image_data->dot_range[0]);
      dmin = -(step*(p_image_data->dot_range[0]));
      imax = (1<<p_image_data->bits_per_sample) - 1;
      dmax = step*(imax - p_image_data->dot_range[1]) + 1;

      if (p_image_data->samples_per_pixel < count)
        count = p_image_data->samples_per_pixel;

      while (i < count * 2) {
        decode_array[i++] = dmin;
        decode_array[i++] = dmax;
      }
    }

    break;

  case PMI_Palette:
    decode_array[i++] = dmin;
    decode_array[i++] = (USERVALUE)((1<<p_image_data->bits_per_sample) -1);

    break;

  case PMI_ICCLab:
    /* For ICCLab colorspace we require L to run [0,100]
        with both a and b running[0,255] to help conversion to
        the RIP's own Lab colorspace */
    decode_array[i++] = 0.0;
    decode_array[i++] = 100.0;
    decode_array[i++] = 0.0;
    decode_array[i++] = 255.0;
    decode_array[i++] = 0.0;
    decode_array[i++] = 255.0;

    break;
  case PMI_CIE_L_a_b_:
    /* For 1976 CIE L*a*b* colorspace we require L* to run [0,100]
        with both a* and b* running[-128,127] to help conversion to
        the RIP's own Lab colorspace */
    decode_array[i++] = 0.0;
    decode_array[i++] = 100.0;
    decode_array[i++] = -128.0;
    decode_array[i++] = 127.0;
    decode_array[i++] = -128.0;
    decode_array[i++] = 127.0;
    break;

  case PMI_RGB:
    if (p_image_data->samples_per_pixel - p_image_data->alpha == 3) {
      while (i < 3 * 2) {
        decode_array[i++] = dmin;
        decode_array[i++] = dmax;
      }
    }

    break;

  case PMI_BlackIsZero:
    if ( (p_image_data->photoshopinks != e_photoshopinks_gray) &&
         (p_image_data->compression != COMPRESS_CCITT) &&
         (p_image_data->compression != COMPRESS_CCITT_T4) &&
         (p_image_data->compression != COMPRESS_CCITT_T6) ) {
      /* Negation of non CCITT images handled through decode array */
      decode_array[i++] = dmin;
      decode_array[i++] = dmax;
      break;
    }
    /* FALLTHROUGH */
  case PMI_WhiteIsZero:
    decode_array[i++] = dmax;
    decode_array[i++] = dmin;

    break;

  default:
    /* This should have been caught earlier, but you never know */
    return detail_error_handler(UNDEFINED, "Unknown PhotometricInterpretation encountered in TIFF image.") ;
  }

  while (i < p_image_data->samples_per_pixel * 2) {
    decode_array[i++] = dmax;
    decode_array[i++] = dmin;
  }

  /* Set up the image matrix entries */
  image_matrix_values[0] = (USERVALUE)p_image_data->image_width;
  image_matrix_values[3] = (USERVALUE)(-(int32)(p_image_data->image_length));
  image_matrix_values[5] = (USERVALUE)p_image_data->image_length;

  /* Setup PS arrays for image dict */
  theTags(data_source) = OARRAY|LITERAL|UNLIMITED;
  oArray(data_source) = NULL;
  theLen(data_source) = CAST_TO_UINT16(0);
  OCopy(omatrix_image, data_source);
  OCopy(oarray_decode, data_source);

  channel_count = p_image_data->samples_per_pixel - (p_image_data->extra_samples - p_image_data->alpha);
  empty_channels = p_image_data->samples_per_pixel - channel_count;

  datasrc_count = (p_image_data->planar_config == PLANAR_CONFIG_PLANAR)
                    ? channel_count
                    : 1;

  result = tiff_alloc_psarray(p_context->mm_pool, datasrc_count, &data_source) &&
           tiff_init_real_array ( p_context->mm_pool,
                                  image_matrix_values, 6, 0, &omatrix_image) &&
           tiff_init_real_array ( p_context->mm_pool,
                                  decode_array, 2*channel_count, 2*empty_channels,
                                  &oarray_decode);


  mm_free( p_context->mm_pool ,
          ( mm_addr_t )( decode_array ) ,
          sizeof(USERVALUE) *  2 * p_image_data->samples_per_pixel) ;
  decode_array = NULL; /* Just in case */

  if ( result ) {
    /* Got arrays - init data source procedure */
    theTags(data_source) |= EXECUTABLE ;
    for ( i = 0; i < datasrc_count; i++ ) {
      theTags(oArray(data_source)[i]) = OOPERATOR|EXECUTABLE ;
      oOp(oArray(data_source)[i]) = &system_ops[NAME_tiffimageread];
    }

    if ( p_context->f_invert_image ) {
      OBJECT* decodeobj;
      /* Invert the image decode array for final image inversion
       * Only bother with channels used - extra channels are there to
       * just swallow data so not important */
      decodeobj = oArray(oarray_decode);
      for ( i = 0; i < channel_count * 2; i += 2 ) {
        temp = oReal(decodeobj[i]);
        oReal(decodeobj[i]) = oReal(decodeobj[i + 1]);
        oReal(decodeobj[i + 1]) = temp;
      }
    }

    if ( !p_image_data->alpha ) {
      OBJECT odict_tiffimage = OBJECT_NOTVM_NOTHING;

      /* Set up the image dictionary and go */
      result = tiff_init_dict(p_context->mm_pool, p_image_data, &omatrix_image,
                              &oarray_decode, &data_source, &odict_tiffimage);
      if ( result ) {
        result = push(&odict_tiffimage, &operandstack) &&
                          (p_context->f_do_imagemask ? imagemask_(pscontext) : image_(pscontext));
        tiff_free_psdict(p_context->mm_pool, &odict_tiffimage);
      }
    } else
    {
      /* Set up the image dictionary and go */
      result = tiff_init_alphadict(pscontext, p_context->mm_pool, p_image_data,
                                   &omatrix_image, &oarray_decode,
                                   &data_source,p_context->f_do_imagemask);
    }
  }

  /* Free off arrays */
  tiff_free_psarray(p_context->mm_pool, &oarray_decode);
  tiff_free_psarray(p_context->mm_pool, &omatrix_image);
  tiff_free_psarray(p_context->mm_pool, &data_source);

  return result ;
} /* Function tiffexec_do */

/*
 * tiffexec_init() initialises the tiffexec internals - it must
 * be called before any tiff operator.
 */
static Bool tiffexec_swstart(struct SWSTART *params)
{
  corecontext_t *context = get_core_context_interp();
  FILELIST *flptr ;
  TIFF6PARAMS *tiff6params ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  /* Set default values of TIFF 6 Params */
  context->tiff6params = tiff6params = mm_alloc_static(sizeof(TIFF6PARAMS)) ;
  if ( tiff6params == NULL )
    return FALSE ;

#define TIFF_DEFAULT_RESOLUTION 72.0

  tiff6params->f_abort_on_unknown = FALSE;
  tiff6params->f_verbose = FALSE;
  tiff6params->f_list_ifd_entries = FALSE;
  tiff6params->f_ignore_orientation = TRUE;
  tiff6params->f_adjust_ctm = TRUE;
  tiff6params->f_do_setcolorspace = TRUE;
  tiff6params->f_install_iccprofile = TRUE;
  tiff6params->f_invert_image = FALSE;
  tiff6params->f_do_imagemask = FALSE;
  tiff6params->f_do_pagesize = TRUE;
  tiff6params->f_ignore_ES0 = FALSE;
  tiff6params->f_ES0_as_ES2 = FALSE;
  tiff6params->f_no_units_same_as_inch = FALSE;
  tiff6params->f_strict = FALSE;
  tiff6params->defaultresolution[0] = TIFF_DEFAULT_RESOLUTION;
  tiff6params->defaultresolution[1] = TIFF_DEFAULT_RESOLUTION;

  if ( (flptr = mm_alloc_static(sizeof(FILELIST))) == NULL )
    return FALSE ;

  tiff_decode_filter(flptr) ;
  filter_standard_add(flptr) ;

  /* Create root last so we force cleanup on success. */
  return tiff_init_contexts();
} /* Function tiffexec_init */


/*
 * tiffexec_finish() deinitialises the tiffexec internals.
 */
static void tiffexec_finish(void)
{
  corecontext_t *context = get_core_context_interp();

  tiff_finish_contexts();
  context->tiff6params = NULL ;
}

IMPORT_INIT_C_GLOBALS(tifcntxt)

void tiffexec_C_globals(core_init_fns *fns)
{
  init_C_globals_tifcntxt() ;

  fns->swstart = tiffexec_swstart ;
  fns->finish = tiffexec_finish ;
}


/*
 * tiffexec_() implements the PS tiffexec operator
 */
Bool tiffexec_(ps_context_t *pscontext)
{
  corecontext_t     *corecontext = ps_core_context(pscontext);
  Bool              result = FALSE;
  int32             num_args;
  tiff_context_t*   p_context;

  /* Check for LL2 as we use a image dictionary */
  if ( theISaveLangLevel(workingsave) < 2 ) {
    return detail_error_handler(CONFIGURATIONERROR,
                                "TIFF input requires language level 2 or greater.");
  }

  if ( tiffexec_start(corecontext, &p_context, &num_args) ) {
    /* TIFF file is setup - lets do it */

    /* We only handle first image in a TIFF file for now */
    result = tiff_set_image(p_context->p_reader, 1);

    if (result) {
      result = tiff_check_tiff6(corecontext, p_context->p_reader);

      if ( result ) {
        tiff_image_data_t image_data;

        result = (tiff_get_image_data(corecontext, p_context->p_reader, &image_data) &&
                  tiff_setup_read_data(corecontext, p_context->p_reader, &image_data) &&
                  tiff_setup_read(corecontext, p_context->p_reader, &image_data,
                                  &p_context->ofile_tiff));

        if ( result ) {
#define return DO_NOT_RETURN_SET_result_INSTEAD!

          /* Set PageSize if requested - have to do this outside of
           * save/restore */
          result = tiff_setpagedevice(p_context, &image_data) ;

          /* Wrap the image call in a save-restore */
          result = result && save_(pscontext) ;
          if ( result ) {
            result = tiffexec_do(pscontext, p_context, &image_data);

            /* We don't want to restore if there was an error in the
            interpreter, as this would restore away the $error dictionary
            setup by handleerror() in the interpreter call. The server loop
            will automatically restore to the server level. */
            result = result && restore_(pscontext);
          }

          /* Free off image data regardless */
          tiff_destroy_noncmyk_colorspace(&image_data); /* closes cmyk equivalent data if required */
          tiff_free_image_data(p_context->p_reader, &image_data);
#undef return
        }
      }
    }
  }


  /* Tidy up the context */
  result = tiffexec_end(&p_context) && result;

  if ( num_args > 0 ) {
    /* Remove args from the stack */
    npop(num_args, &operandstack);
    /* NB: It's important this happens after tiffexec_end, because pointers */
    /* to these args are hidden from GC in the context. */
  }

  return result ;

} /* Function tiffexec_ */


/*
 * tiffimageread_()
 */
Bool tiffimageread_(ps_context_t *pscontext)
{
  tiff_context_t* p_context;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Pick up the tiff context */
  p_context = tiff_first_context();
  if ( p_context == NULL ) {
    return detail_error_handler(UNDEFINED, "No TIFF context.") ;
  }

  /* Read data from the file */
  return tiff_read_file(p_context->p_reader, &operandstack) ;

} /* Function tiffimageread_ */


/*
 * tiffbyteflip_()
 */
Bool tiffbyteflip_(ps_context_t *pscontext)
{
  tiff_context_t* p_context;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Pick up the tiff context */
  p_context = tiff_first_context();
  if ( p_context == NULL ) {
    return detail_error_handler(UNDEFINED, "No TIFF context.") ;
  }

  /* Read data from the file */
  return tiff_byte_flip(p_context->p_reader, &operandstack) ;

} /* Function tiffbyteflip_ */


/*
 * reversebyte_() support operator for byte flipping TIFF image data in
 * the TIFF6 procset when FillOrder is 2.
 */
Bool reversebyte_(ps_context_t *pscontext)
{
  int32   stack_size;
  int32   type;
  uint8*  p_buffer;
  uint32  len;
  OBJECT* obj;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Check for some candidate args */
  if ( isEmpty(operandstack) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  /* Get arg from stack */
  stack_size = theStackSize(operandstack);
  obj = TopStack(operandstack, stack_size);
  type = oType(*obj);
  if ( type != OSTRING ) {
    return error_handler(TYPECHECK) ;
  }

  len = theLen(*obj);
  if ( len < 1 ) {
    /* Nothing to do */
    return TRUE ;
  }

  /* Flip all bytes in string */
  p_buffer = oString(*obj);
  do_byteflip(p_buffer, len);

  return TRUE ;

} /* Function reversebyte_ */


/* Log stripped */
