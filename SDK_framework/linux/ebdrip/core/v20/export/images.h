/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!export:images.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image argument handling and character image data handling.
 */

#ifndef __IMAGES_H__
#define __IMAGES_H__

#include "imaget.h"

#include "gs_color.h"       /* COLORSPACE_ID */
#include "imcolcvt.h"
#include "mm.h"
#include "matrix.h"         /* OMATRIX */
#include "objects.h"        /* OBJECT */
#include "packdata.h"

struct im_decodesobj_t;

/**
 * \defgroup images Image handling.
 * \ingroup core
 * \{ */

/** \brief Front-end image arguments structure. */
struct IMAGEARGS {
  int32 width ;                /**< width of image. */
  int32 height ;               /**< height of image. */
  int32 bits_per_comp ;        /**< bits per component per sample. */
  int32 ncomps ;               /**< number of components. */
  int32 nprocs ;               /**< number of procedures. */
  struct {
    Bool enabled;              /**< is it turned on for this image */
    int32 method;              /**< What downsampling method ? */
    int32 x, y;                /**< horiz. and vertical downsampling factors */
    int32 w0, h0;              /**< Pre-downsampled width and height */
    Bool rowrepeats_near;      /**< Do row repeats in imstore on rows that are
                                    near matches */
    Bool rowrepeats_2rows;     /**< Restrict to 2 row matches for this image */
  } downsample;                /**< image downsampling params */
  COLORSPACE_ID image_color_space ; /**< colour space of the image data. */
  OBJECT image_color_object ;  /**< color space OBJECT of the image data. */
  OMATRIX omatrix ;            /**< image matrix. */
  OBJECT *data_src ;           /**< Sources of image data. */
  OBJECT *data_src_flush ;     /**< Data sources that need flushing. */
  int32 n_src_flush ;          /**< Number of data sources to flush. */
  OBJECT *data_src_close ;     /**< Data sources that need closed. */
  int32 n_src_close ;          /**< Number of data sources to close. */
  int32 stack_args ;           /**< number of args on the stack. */
  int32 imageop ;              /**< NAME_imagemask/image/colorimage. */
  int8  imagetype ;            /**< TypeImageMask/Image/MaskedImage. */
  int8  polarity ;             /**< for imagemask only. */
  int8  interpolate ;          /**< interpolate image data. */
  int8  interleave ;           /**< INTERLEAVE_CHROMA/PER_SAMPLE/SCANLINE/NONE. */
  int32 lines_per_block ;      /**< contiguous lines in input stream. */
  USERVALUE *decode ;          /**< decode array, passed in image dictionary. */
  struct maskgendata *maskgen ;/**< for generation of mask for chroma-key images.. */
  OBJECT colorspace;           /**< used when tiff images use spot colors. */

  struct IMAGEARGS *maskargs;  /**< arguments for mask of ImageType 2 and 3. */
  int8  fit_edges ;            /**< fit edges exactly. */
  int8  coerce_to_fill ;       /**< HDLT: coerce image directly to fills. */
  int8  colorType ;            /**< What color chain should we use? */
  int8  image_pixel_centers ;  /**< use image pixel centers. */
  struct FilteredImage_s *filteredImage;/**< Image filtering info for this image. */
  struct IMAGEPIPE *pipe;      /**< Image filtering pipeline for this image. */
  Bool isSoftMask;             /**< This image is intended to be used as a soft mask. */
  Bool matted;                 /**< Softmask image with a matte color. */
  Bool contoneMask;            /**< This mask contains contone data. */
  Bool premult_alpha;          /**< color values premultiplied by alpha (applies
                                    to TypeImageAlphaImage). */

  OBJECT* dictionary;          /**< The original Postscript dictionary used to
                                    specify this image/mask.  This will be NULL
                                    if the image was not specified by a
                                    dictionary (i.e. old-style image/mask
                                    operator). */

  mps_root_t gc_root ;         /**< GC root for data source and cleanup list. */
  Bool no_currentfile ;        /**< There are no currentfile-based sources. */

  Bool clean_matrix;           /**< True if the image matrix should be 'cleaned'
                                    before use. */
  uint32 flipswap ;            /**< IMAGE_OPTIMISE_* values to indicate
                                    possible X and Y swap and flip. */
} ;

/** \brief
   This structure contains the input data for an image input block (the
   maximum scanlines readable between interleaved mask/image pairs). */
struct IM_BUFFER {
  int32 ibytes ;               /**< Number of bytes in  input buffers. */
  uint8 **ibuf ;               /**< Array of pointers to  input bytes. */
  uint8 **obuf ;               /**< Array of pointers to output bytes. */

  int32 tbytes ;               /**< Number of bytes in temporary buffers. */
  uint8 **tbuf ;               /**< Array of pointers to temporary bytes. */
  int32 tbufsize ;             /**< Size of the tbuf allocations. */
  uint8 **dbuf ;               /**< Array of pointers to downsampled bytes. */
  int32 dbufsize ;             /**< Size of the dbuf allocations. */
} ;

/** \brief
 * This structure is used for temporary workspace variables used when
 * dealing with the image in preparation to adding it to the DL in the
 * correct format.
 */
struct IMAGEDATA {
  corecontext_t *context ;     /**< TLS core context. */
  DL_STATE *page ;             /**< The DL the image will ultimately be added to. */
  GS_COLORinfo *colorInfo ;    /**< May or may not be the same colorInfo in the gstate. */
  mm_pool_t *pools;            /**< The MM pools for sub-allocations. */
  Bool out16 ;                 /**< If we think we're going to do 16 bit output. */
  int32 oncomps ;              /**< Number of output components. */

  int32 plane ;                /**< Plane number to start of with when writing in planar format. */
  Bool presep ;                /**< If we think this is a pre-separated image[mask]. */

  int32 expandformat ;         /**< Format expected by the image store. */

  int32 bytes_in_input_line ;  /**< number of bytes in image + mask for sample interleaved. */
  int32 bytes_in_image_line ;  /**< number of bytes in image scanline. */

  int32 method ;               /**< Color conversion method; probably goes soon. */

  struct im_decodesobj_t *decodesobj; /**< Decodes array object. */
  union {
    int32 **idecodes ;         /**< Arrays of LUTs to apply Decode array. */
    float **fdecodes ;         /**< Arrays of LUTs to apply Decode array. */
  } u ;
  int32 **ndecodes ;           /**< Arrays of normalised LUTs to apply Decode array. */

  PACKDATA *pd ;               /**< Pointer to pack/unpack structure. */
  int32 pd_use ;               /**< Need to PD_UNPACK/PD_PLANAR/PD_PACK. */

  IM_COLCVT *imc ;             /**< Pointer to color convert structure. */
  IM_BUFFER *imb ;             /**< Pointer to buffering structure. */
  Bool sharedIMB ;             /**< For controlling the sharing of buffers between image & mask. */

  int32 image_lines ;          /**< Number of lines read from image (only valid
                                  at end of reading phase). */
  Bool degenerate ;            /**< This image (or its mask) have a zero w/h. */
  int32 rwidth ;               /**< Adjusted width for matrix tweaks. */
  int32 rheight ;              /**< Adjusted height for matrix tweaks. */
  int32 optimise ;             /**< Matrix adjustments performed. */
  OMATRIX opt_matrix ;         /**< Adjusted image matrix. */
  OMATRIX orig_matrix ;        /**< Original image matrix. */

  im_transform_t geometry ;    /**< Quantised image transform. */

  int32 *ubuf ;                /**< Unpacked buffer. */
  uint8 *tbuf ;                /**< Planarized/color converted buffer. */
  enum {
    IMD_raw, IMD_unpacked, IMD_converted
  } state ;                    /**< Processing steps taken on data. */
  int32 state_line ;           /**< Line to which processing has been applied. */

  int32 lines_left_in_block ;  /**< Number of lines left to read in this block. */
  size_t free_before;          /**< Amount of free space at the start. */
  Bool all_components_same ;   /**< All components are the same. */
  struct IMAGEDATA *imaskdata ;
} ;

/** Interleaving of mask data w.r.t. image data.
    NOTE: testing for INTERLEAVE_NONE can be used to avoid testing imagetype */
enum {
  INTERLEAVE_NONE = 0,       /**< /ImageType 1 */
  INTERLEAVE_PER_SAMPLE = 1, /**< /ImageType 3 + /Interleave 1 */
  INTERLEAVE_SCANLINE = 2,   /**< /ImageType 3 + /Interleave 2 */
  INTERLEAVE_SEPARATE = 3,   /**< /ImageType 3 + /Interleave 3 */
  INTERLEAVE_CHROMA = 4      /**< /ImageType 4 */
} ;

/* prototypes */

Bool get_image_string(
  /*@notnull@*/ /*@in@*/ OBJECT *theo ,
  int32 bytes_remaining ,
  /*@notnull@*/ /*@out@*/ uint8 **pclist ,
  /*@notnull@*/ /*@out@*/ int32 *plen
);

void get_image_genmask_data(
  /*@notnull@*/ /*@in@*/ struct maskgendata *maskgen ,
  /*@notnull@*/ /*@out@*/ int32 *nvalues ,
  /*@notnull@*/ /*@out@*/ int32 **colorinfo
);

/**
 * Initialise the passed imageargs structure to sensible defaults.
 *
 * \param[out] imageargs  The image args structure to initialise.
 * \param colortype       Typically this should be GSC_IMAGE for images or
 *                        GSC_FILL for masks.
 */
void init_image_args(IMAGEARGS *imageargs, int8 colortype);

/**
 * Set the use type for this image, after the image type and mask args have
 * been set and linked together. This function prepares the flags that will
 * be used by im_optimisematrix() to mirror and rotate image data
 * appropriately for the output, clip or transparency surfaces.
 */
void set_image_order(IMAGEARGS *imageargs) ;

/**
 * Populate the passed 'imageargs' from 'stack', which should contain the
 * stack appropriate for a Postscript image operator call.
 *
 * \param stack Postscript stack object containing the arguments to the image
 *              operator.
 * \param imageargs Internal image argument structure pointer. This should be
 *                  initialised by the caller via init_image_args(). If a mask
 *                  is present, the 'maskargs' field should point to a valid
 *                  IMAGEARGS structure (client-initialised with
 *                  init_image_args()) , which will also be populated.
 */
Bool get_image_args(
  /*@notnull@*/ /*@in@*/ corecontext_t *context,
  /*@notnull@*/ /*@in@*/ STACK *stack,
  /*@notnull@*/ /*@in@*/ IMAGEARGS *imageargs,
  int32 imageop
);

Bool filter_image_args(corecontext_t *context, IMAGEARGS *imageargs) ;

Bool alloc_image_datasrc(
  /*@notnull@*/ /*@in@*/ IMAGEARGS *imageargs ,
  int32 nprocs
);

Bool alloc_image_decode(
  /*@notnull@*/ /*@in@*/ IMAGEARGS *imageargs ,
  int32 ncomps
);

/** Close and flush image data sources, destroy image filtering. This should
    only be called for cases where free_image_args() is not called. */
void finish_image_args(
  /*@notnull@*/ /*@in@*/ IMAGEARGS *imageargs
);

void free_image_args(
  /*@notnull@*/ /*@in@*/ IMAGEARGS *imageargs
);

Bool docharimage(
  /*@notnull@*/ /*@in@*/ DL_STATE *page ,
  /*@notnull@*/ /*@in@*/ STACK *stack ,
  /*@notnull@*/ /*@in@*/ IMAGEARGS *imageargs
);

Bool gs_image(/*@notnull@*/ /*@in@*/ corecontext_t *context,
              /*@notnull@*/ /*@in@*/ STACK *stack);
Bool gs_imagemask(/*@notnull@*/ /*@in@*/ corecontext_t *context,
                  /*@notnull@*/ /*@in@*/ STACK *stack);


/** Flags for im_cleanup_files. */
enum {
  IM_CLEANUP_FLUSH = 1,
  IM_CLEANUP_CLOSE = 2
} ;

/** Add a number of files to the cleanup lists for an image. A number of file
    objects are removed from temporarystack, and added to the appropriate
    cleanup lists. */
Bool im_cleanup_files(IMAGEARGS *imageargs, int32 n, uint32 flags) ;

/** Make image args datasources suitable for pulling data from. This is used
   when doing HDLT on an image, both to limit the amount of data the callback
   procedure can suck from any currentfile source, and to ensure that
   currentfile sources are read if the HDLT callback does /Replace or
   /Discard. imageargs->no_currentfile will be set by this function. */
Bool im_datasource_currentfile(IMAGEARGS *imageargs) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
