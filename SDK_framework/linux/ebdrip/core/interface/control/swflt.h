/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swflt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * This header file provides the interface for image filter plug-ins
 *
 * Image filters can be inserted into the image processing pipeline that the
 * RIP uses.  Filters can be written to perform OEM specific functionality,
 * or to implement generic image processing e.g. interpolation & decimation.
 *
 * There are two separate defined APIs in this file.
 *
 * The sw_flt_api defines the routines that an image filter provides. The RIP
 * will call these methods.
 *
 * The sw_flt_ifstore_api defines the routines that the RIP provides for a
 * filter to call. The RIP provides methods to enable the retrieval and
 * storage of image data and other methods to enable filtering to take place.
 */


#ifndef __SWFLT_H__
#define __SWFLT_H__

/** \defgroup PLUGIN_swflt Image filtering API
    \ingroup interface */
/** \{ */

#include <stddef.h>   /* size_t */
#include "ripcall.h"  /* RIPCALL */
#include "swapi.h"    /* SW_API_REGISTERED */
#include "swmemapi.h"
#include "swdataapi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Version numbers defined for the image filtering API. */
enum {
  SW_FLT_API_VERSION_20070525 = 3, /**< Obsolete as of 20071112 */
  SW_FLT_API_VERSION_20071112      /**< Current version */
  /* new versions go here */
#ifdef CORE_INTERFACE_PRIVATE
  , SW_FLT_API_VERSION_NEXT /* Do not ever use this name. */
  /* SW_FLT_API_VERSION is provided so that the Harlequin Core RIP can test
     compatibility with current versions, without revising the registration
     code for every interface change.

     Implementations of sw_flt_api within the Harlequin Core RIP should NOT
     use this; they should have explicit version numbers. Using explicit
     version numbers will allow them to be backwards-compatible without
     modifying the code for every interface change.
  */
  , SW_FLT_API_VERSION = SW_FLT_API_VERSION_NEXT - 1
#endif
};

/** Type definition of the API implementation. */
typedef struct sw_flt_api sw_flt_api;

/** Type definition for an instance of the implementation. */
typedef struct sw_flt_instance sw_flt_instance ;

/** Type definition of the filter callback API. */
typedef struct sw_flt_ifstore_api sw_flt_ifstore_api ;

/** Internal name of the base filter used so that subclasses can be made. */
#define SW_FLT_FILTERBASE_INTERNAL_NAME ((const uint8 *)"Filterbase")

/** \brief Return values for sw_flt_api functions. */
enum {
  /* Success codes present in SW_FLT_API_VERSION_20071112: */
  SW_FLT_SUCCESS = 0,      /**< Normal successful return. */
  SW_FLT_SKIP_FILTER = -1, /**< Refuse filter presented. */
  /* End of success codes present in SW_FLT_API_VERSION_20071112 */

  /* Errors present in SW_FLT_API_VERSION_20071112: */
  SW_FLT_ERROR = 1,        /**< A non-specific error, and the minimum error
                                value. Please avoid using this if possible. */
  SW_FLT_ERROR_VERSION,    /**< Version of callback API is insufficient. */
  SW_FLT_ERROR_MEMORY,     /**< Memory exhausted. */
  SW_FLT_ERROR_TYPECHECK,  /**< Parameter is of the wrong type. */
  SW_FLT_ERROR_RANGECHECK, /**< Parameter is out of range. */
  SW_FLT_ERROR_CONFIG      /**< Filter configuration is invalid. */
  /* End of errors present in SW_FLT_API_VERSION_20071112 */
};

/** \brief Type of return values from sw_flt_api functions. */
typedef int sw_flt_result;

/** \brief A matrix, representing a 2 dimensional affine transformation.

    The matrix is specified using normal PDL conventions, defining
    the first two columns of a 3x3 matrix, assuming [0 0 1] for the final
    column.
*/
typedef struct sw_flt_matrix
{
  /* Entries present in SW_FLT_API_VERSION_20071112: */

  double elem[6]; /**< Matrix entries. */

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
} sw_flt_matrix;

/** \brief Type definition for an image data store.

    This is an opaque reference to a RIP data structure. The typedeffed name
    MUST always be used. The struct tag should NEVER be used. Global Graphics
    reserves the right to change the struct tag in future versions of this
    API. */
typedef struct IM_STORE sw_flt_store ;

/** \brief Type definition for an image block list.

    This is an opaque reference to a RIP data structure. The typedeffed name
    MUST always be used. The struct tag should NEVER be used. Global Graphics
    reserves the right to change the struct tag in future versions of this
    API. */
typedef struct IM_BLIST sw_flt_blist ;

/** A flag to indicate the caller wants all the planes of an image(block). */
#define SW_FLT_ALL_PLANES -1

/** \brief A set of bit flags representing image colorspaces. */
enum {
  /* Entries present in SW_FLT_API_VERSION_20071112: */

  SW_FLT_CS_UNKNOWN  = 0x0000, /**< Colorspace is unknown */
  SW_FLT_CS_GRAY     = 0x0001, /**< Colorspace is Gray */
  SW_FLT_CS_RGB      = 0x0002, /**< Colorspace is RGB */
  SW_FLT_CS_CMYK     = 0x0004, /**< Colorspace is CMYK */
  SW_FLT_CS_CIE      = 0x0100, /**< Colorspace is a CIE calibrated space */
  SW_FLT_CS_NCHANNEL = 0x0200, /**< Colorspace is an N-color space */
  SW_FLT_CS_INDEXED  = 0x1000, /**< Colorspace is indexed */
  SW_FLT_CS_SAME     = 0x2000, /**< Colorspace is same as previous filter. */
  SW_FLT_CS_ALL      = 0x4000  /**< Any colorspace is acceptable */

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
};

/** \brief The storage type for image colorspace bits. */
typedef uint32 sw_flt_colorspace;

/** \brief A mask to extract the number of colorants from a set of colorspace
    bits. */
#define  SW_FLT_NCOLORANTS_MASK  0x00FF

/** \brief The maximum number of colorants an image filter can handle. */
#define SW_FLT_MAX_COLORANTS 255

/** \brief A set of bit flags representing pixel formats of image data. */
enum {
  /* Entries present in SW_FLT_API_VERSION_20071112: */

  SW_FLT_PIX_FMT_NONE      = 0x0000, /**< No image data format specified */
  SW_FLT_1BIT              = 0x0001, /**< Image data is 1bpp */
  SW_FLT_8BIT              = 0x0008, /**< Image data is 8bpp */
  SW_FLT_16BIT             = 0x0010, /**< Image data is 16bpp */
  SW_FLT_16BIT_SIGNEDFIXED = 0x0810, /**< Image data is fixed-point 16bpp */
  SW_FLT_WIDE_GAMUT        = 0x1020, /**< Image data is 32bpp IEEE float */
  SW_FLT_PIX_FMT_SAME      = 0x2000, /**< Data is same as previous filter */
  SW_FLT_PIX_FMT_ALL       = 0x4000  /**< Any pixel format is acceptable */

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
};

/** \brief The storage type for pixel format bits. */
typedef uint32 sw_flt_pixfmt;

/** \brief A mask to extract the bit depth from the pixel format bits. */
#define  SW_FLT_PIX_BITS_MASK  0x003F

/** \brief Convenience macro to get the number of bits a pixel format is
    represented in. */
#define  SW_FLT_BITS(_x)  ((_x)&SW_FLT_PIX_BITS_MASK)

/** \brief A set of bit flags representing the compression originally used by
    the image. */
enum {
  /* Entries present in SW_FLT_API_VERSION_20071112: */

  SW_FLT_UNKNOWN   = 0x0000, /**< Original image format unknown. */
  SW_FLT_DCT       = 0x0001, /**< Image was JPEG. */
  SW_FLT_JPEG2000  = 0x0002, /**< Image was JPEG2000. */
  SW_FLT_PNG       = 0x0004, /**< Image was PNG. */
  SW_FLT_TIFF      = 0x0008, /**< Image was TIFF. */
  SW_FLT_HDPHOTO   = 0x0010, /**< Image was HD Photo. */
  SW_FLT_COMP_ALL  = 0x2000, /**< Any image type is acceptable. */
  SW_FLT_COMP_NONE = 0x4000  /**< Image was raw data. */

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
};

/** \brief The storage type for image compression methods. */
typedef uint32 sw_flt_compression;

/** \brief Flags specifying some details of how the filter should operate. */
enum {
  /* Entries present in SW_FLT_API_VERSION_20071112: */

  /** A lightweight filter, that doesn't modify the pixel data.
   */
  SW_FLT_NO_FLAGS = 0x0000,
#ifdef CORE_INTERFACE_PRIVATE
  /** Implicit filters are not configured as a part of a chain.
   * They just get inserted as necessary.
   *
   * The "implicit" flag should only be set for internal GG filters, so
   * this flag is hidden from the exported API.
   */
  SW_FLT_IMPLICIT = 0x0001,
#endif
  /** Heavy-weight filter (i.e. one with a large compute overhead),
   * that keeps its results in an image store cache in case the results
   * are needed again.
   */
  SW_FLT_HEAVY_WEIGHT = 0x0002,
  /** The filter modifies pixel data.
   * It is possible that we have filters that don't need to modify data,
   * in which case we can optimize things.
   */
  SW_FLT_MODIFIES_PIXEL_DATA = 0x0004,
  /** Some filters may only need to use a single row of image blocks at a time,
   * and then reuse the storage for the next row.
   * See the interpolate filter for an example.
   */
  SW_FLT_REUSE_BLISTS = 0x0008

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
};

/** \brief The storage type for the filter flags. */
typedef uint32 sw_flt_flags;

/* Images are handled in blocks which are upto 128 by 128 bytes (16Kbytes) in
   size. Whilst adding some complexity, this does provide us with the ability
   to handle large images in low memory, as we can compress, page to disk,
   or even dispose of image blocks we are not directly working on.
*/

/** This object holds the pixel data for a single plane of a single block
 * of an image.
 */
typedef struct sw_flt_image_plane
{
  /* Entries present in SW_FLT_API_VERSION_20071112: */
  uint32  n_bytes;          /**< The length of image data. */
  uint8  *pixel_data;       /**< The data. */
  /* End of entries present in SW_FLT_API_VERSION_20071112 */
} sw_flt_image_plane;

/** \brief Defines a block of image memory
 *
 * Note, 1 bit image blocks can be 1024 pixels wide, 16 bit image blocks just
 * 64 pixels wide.
 */
typedef struct sw_flt_image_block
{
  /* Entries present in SW_FLT_API_VERSION_20071112: */
  uint32  n_pix_w;             /**< Width of the block (in pixels!) */
  uint32  n_pix_h;             /**< Height of the block (in pixels). */
  uint32  n_planes;            /**< Number of planes we have. */
  sw_flt_image_plane *planes;  /**< An array [0...n_planes-1] of planes. */
  /* End of entries present in SW_FLT_API_VERSION_20071112 */
} sw_flt_image_block;

/** Rectangle than can be used to define the clipping bbox of an image
 */
typedef struct sw_flt_rect
{
  /* Entries present in SW_FLT_API_VERSION_20071112: */
  int32 x1, y1, x2, y2;        /**< Corners of a rectangular area. */
  /* End of entries present in SW_FLT_API_VERSION_20071112 */
} sw_flt_rect;

/** The filtering process involves converting images, and so will need to
 * have source and destination data for such a process. The following
 * structure defines the parameters of such an image, which may be
 * associated with either the input or output of a filter.
 */
typedef struct sw_flt_image
{
  /* Entries present in SW_FLT_API_VERSION_20071112: */
  uint32             width;        /**< Width of the image. */
  uint32             height;       /**< Height of the image. */
  uint32             ncomps;       /**< Number of color channels of the image. */
  sw_flt_pixfmt      pixfmt;       /**< The pixel format of the image. */
  sw_flt_colorspace  colorspace;   /**< The colorspace of the image. */
  sw_flt_matrix      matrix;       /**< Matrix transformation for the image. */
  sw_flt_rect        clipbbox;     /**< The image's clipping box. */
  sw_flt_compression compression;  /**< The image compression type. */
  /* End of entries present in SW_FLT_API_VERSION_20071112 */
} sw_flt_image;

/** \brief Construction parameter API and data.
 *
 * When a filter instance is constructed, it may choose to modify its behavior
 * based on data in the filter configuration dictionary, upon meta-data tags
 * extracted from the image, or upon selected pieces of device configuration.
 *
 * This structure contains the data access API and data representing these
 * data sources. These data sources are only valid for the lexical scope of
 * the sw_flt_api::construct() and sw_flt_api::present() method calls. Any
 * information contained in them that the module wished to retain MUST be
 * copied elsewhere before returning from the method.
 */
typedef struct sw_flt_construct_params {
  /* Entries present in SW_FLT_API_VERSION_20071112: */
  const sw_data_api *data_api; /**< An API to access the parameter data. */

  /** A datum containing the image filter configuration dictionary. If no
      configuration dictionary was present, then this will be a datum of type
      SW_DATUM_TYPE_NOTHING. */
  sw_datum filter_config ;

  /** A datum containing any meta data corresponding to the implementation's
      sw_flt_api::requested_meta_data field. If no meta data was requested,
      this will point to a datum of type SW_DATUM_TYPE_NOTHING. The meta-data
      requested by all filters in the filter chain will be aggregated into
      this datum. */
  sw_datum meta_data ;

  /** A datum containing any device configuration corresponding to the
      implementation's sw_flt_api::requested_device_config field. If no
      device config was requested, this will point to a datum of type
      SW_DATUM_TYPE_NOTHING. The device configuration requested by all
      filters in the filter chain will be aggregated into this datum. */
  sw_datum device_config ;

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
} sw_flt_construct_params ;

/** \brief An instance structure for the filter API implementation
 *
 * Each instance of a filter needs data describing both the input and output
 * of the filtering process, together with any instance-specific variable
 * data. All of the image instance data is bundled together in the following
 * structure.
 *
 * The instance structure may be subclassed to hold private data by defining
 * a subclass structure containing this structure as its first member, and
 * using the size of that structure as the implementation's instance size.
 * Individual methods may then downcast their instance pointer parameters to
 * subclass pointers, and use the private data. e.g.,
 *
 * \code
 * typedef struct my_instance {
 *   sw_flt_instance super ; // must be first entry
 *   struct my_data *dynamic ;
 *   int32 other_fields ;
 * } my_instance ;
 *
 * static sw_flt_result RIPCALL my_destruct(sw_flt_instance *inst)
 * {
 *   my_instance *myinst = (my_instance *)inst ; // downcast to subclass
 *   // free allocated data, if necessary:
 *   inst->mem->implementation->free(inst->mem, myinst->dynamic) ;
 *   return SW_FLT_SUCCESS ;
 * } ;
 *
 * const static sw_flt_api my_impl = {
 *   {
 *     SW_FLT_API_VERSION_20071112,
 *     (const uint8 *)"myname",
 *     (const uint8 *)("A long description of my module implementation"
 *                     "Copyright (C) 2007-2013 Global Graphics Software Ltd."),
 *     sizeof(my_instance), // RIP will allocate this amount for instance
 *   },
 *   // ...more of sw_flt_api definition...
 *   my_destruct,
 *   // ...rest of sw_flt_api definition...
 * } ;
 *
 * // Call SwRegisterFLT(&my_impl) after SwInit() to register this module.
 * \endcode
 *
 * The RIP will not touch memory beyond the size of the instance structure
 * for the implementation version registered.
 *
 * The RIP will initialise the instance variables appropriately before
 * sw_flt_api::present() or sw_flt_api::construct() are called. The RIP may
 * change so of the instance fields between filter construction and
 * execution.
 */
struct sw_flt_instance {
  /* Entries present in SW_FLT_API_VERSION_20071112: */

  /** \brief Pointer to the API implementation.

       API methods for a filter instance should always be called by indirecting
       through the instance's implementation field.

       This field is filled in by the RIP before sw_flt_api::present() or
       sw_flt_api::construct() are called.
  */
  const sw_flt_api *implementation;

  /** \brief A memory allocator instance.

       This object is supplied by the RIP so that the filter implementation can
       allocate memory using the RIP's memory allocator. Filter implementations
       should use this in preference to malloc() and free(), so that the RIP
       can track memory allocation and respond to low memory states more
       effectively.
  */
  sw_memory_instance *mem;

  /** \brief Callback API for accessing data blocks.

       This field is filled in by the RIP before sw_flt_api::present() or
       sw_flt_api::construct() are called.
   */
  const sw_flt_ifstore_api *ifstore_api; /**< Initialised by RIP. */

  /** Callback method to find the previous filter instance, initialised by
      the RIP. This will only return a non-NULL valid during image pipeline
      execution. During the sw_flt_api::present() and sw_flt_api::construct()
      calls, this will return NULL. */
  sw_flt_instance *(RIPCALL *previous)(sw_flt_instance *curr) ;

  /** This field is initialised by RIP to point at the details of the
      original image, before any filtering was applied. */
  const sw_flt_image *image_base;

  /** This field is initialised by RIP to point at the result of the previous
      step in the filtering pipeline. */
  const sw_flt_image *image_in;

  /** This field contains the details for the result of this image filtering
      step. The RIP initialises this field to the same values as the input
      image, and expects the filter to modify these details during the
      sw_flt_api::present() call. If the filter does not change any of the
      image details, it need not modify this field. */
  sw_flt_image *image_out;

  /** This field contains a reference to an image store. This should be used
      as a context parameter to all of the the sw_flt_ifstore_api callbacks
      which take a sw_flt_store parameter. This value is only valid if the
      sw_flt_api::construct() method is successful, it will be NULL before
      that point. */
  sw_flt_store *im_store;

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
} ;

/* -------------------------------------------------------------------------- */
/** \brief Collection structure for initialisation parameters. */
typedef struct sw_flt_init_params {
  /* Entries present in SW_FLT_API_VERSION_20071112: */

  /** \brief Global memory allocator.

      The memory allocators provided in the instances should only be used for
      allocations local to that instance. This memory allocator can be used
      for allocations which must survive individual instance lifetimes. */
  sw_memory_instance *mem ;

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
} sw_flt_init_params ;

/* -------------------------------------------------------------------------- */
/** \brief The definition of an implementation of the image filtering
 * interface.
 *
 * The RIP may construct multiple instances for each implementation
 * registered.
 */
struct sw_flt_api
{
  sw_api_info info; /**< Version number etc. REQUIRED to be first */

  /* Entries present in SW_FLT_API_VERSION_20071112: */

  /** The internal name of the superclass of the filter. If this is non-NULL,
      the named API implementation must have already been registered, and the
      RIP will initialise the \a superclass pointer to the discovered
      filter. */
  const uint8* superclass_name;

  /** A pointer to the superclass of the implementation. If a superclass name
      is supplied, then this should be NULL, and the value will be
      initialised by the RIP at registration. This may be initialised to a
      valid superclass address before registration if the superclass name is
      NULL. */
  const sw_flt_api *superclass ;

  /** \brief The \p init() method is called before any other calls to the
      implementation.

      This method may be used to initialise any implementation-specific data.
      This method is optional.

      \param api The registered filter implementation to be initialised.

      \param[in] params A structure containing callback APIs and parameters
      valid for the lifetime of the module. Any parameters that the
      implementation needs access to should be copied out of this structure
      into private storage for the registered implementation.

      \retval TRUE Success, indicating that the implementation is fully
      initialised.

      \retval FALSE Failure to initialise the implementation. If this is
      returned, the implementation will not be finalised, and the RIP will
      terminate.
  */
  HqBool (RIPCALL *init)(/*@in@*/ /*@notnull@*/ sw_flt_api *api,
                         /*@in@*/ /*@notnull@*/ const sw_flt_init_params *params);

  /** \brief The \p finish() method is called after all calls to the
      implementation or its instances.

      The implementation instances should not access any data owned by the
      RIP after this call, nor should they call any implementation or RIP
      callback API methods after this call. This method is optional.

      \param api A registered filter implementation to finalise.
  */
  sw_flt_result (RIPCALL *finish)(/*@in@*/ /*@notnull@*/ sw_flt_api *api);

  /** \brief Determine if a filter should be run.

      The filter instance is given a chance to examine input image data from
      the previous step in the pipeline, look at the image parameters and
      modify the output image details. The return value indicates if the
      filter should be constructed or skipped.

      All changes to the output image details should be done in the \p
      present() method.

      This method is optional. If it is NULL, the image filter will be given
      a chance to construct itself, and then enabled if that is successful,
      but may not change pertinent details of the output image.

      \param[in] idata The filter instance presented for consideration.

      \param[in] params A structure containing the image filter configuration
      data, and accessor APIs.

      \return SW_FLT_SUCCESS if the filter is to be constructed,
      SW_FLT_SKIP_FILTER if the filter is to be omitted from the pipeline,
      otherwise one of the sw_flt_result error codes. The \p destruct()
      method will only be called if the value SW_FLT_SUCCESS was returned
      by both this method and the \p construct().
   */
  sw_flt_result (RIPCALL *present)(sw_flt_instance *idata,
                                   const sw_flt_construct_params *params) ;

  /** \brief Prepare an image instance for filtering.

      After a filter says it would like to filter an image, it then gets
      called to complete initialisation of the instance data. This method
      should change the output image structure in the instance.

      This method is optional. If it is NULL, and the \p present() function
      succeeded, the image filter will be enabled.

      \param[in] idata The filter instance to be constructed.

      \param[in] params A structure containing the image filter configuration
      data, and accessor APIs.

      \return SW_FLT_SUCCESS if the filter is enabled, SW_FLT_SKIP_FILTER if
      the filter is to be omitted from the pipeline, otherwise one of the
      sw_flt_result error codes. The \p destruct() method will only be called
      if the value SW_FLT_SUCCESS was returned.
   */
  sw_flt_result (RIPCALL *construct)(sw_flt_instance *idata,
                                     const sw_flt_construct_params *params) ;

  /** \brief This method is used to destroy an image filter instance.

      This method is optional.

      The RIP will call the destructors individually for each instance
      after processing parts of the image pipeline. The filter implementation
      should free any memory it allocated for the instance.

      \param[in] instance The filter instance to destroy.

      \return SW_FLT_SUCCESS if the filter was destroyed without problems,
      otherwise one of the sw_flt_result error codes.
  */
  sw_flt_result (RIPCALL *destruct)(sw_flt_instance *idata);

  /** \brief Get image data for a particular block from an image.

      As a part of the process of getting the data for the specified block,
      the RIP may call methods from previous filters in the chain.

      Blocks are indexed by block indices. The standard block sizes can
      discovered by calling sw_flt_ifstore_api::if_blockWidth() and
      sw_flt_ifstore_api::if_blockHeight() in the sw_flt_ifstore_api
      callback interface. The block index is the pixel coordinate divided by
      the block height or width appropriately.

      This method is required. If this method pointer is NULL on registration,
      it will be inherited from the filter's superclass (if any).

      \param[in] idata The filter implementation instance from which the
      block is being requested. The instance pointer may be used to locate an
      image store to pass to the routines in the sw_flt_ifstore callback API
      to satisfy this request.

      \param x The horizontal index of the block to access.

      \param y The vertical index of the block to access.

      \param plane A zero-based index for separated color planes, or -1 to
      indicate all planes should be returned.

      \param[out] block A pointer to a filter block structure in which the
      image block data requested will be stored.

      \return SW_FLT_SUCCESS if the request was fulfilled, otherwise one
      of the sw_flt_result error codes.
  */
  sw_flt_result (RIPCALL *getimageblock)(sw_flt_instance *idata,
                                         int32 x, int32 y, int32 plane,
                                         /*@out@*/ sw_flt_image_block *block);

  /** \brief Locks or unlocks an image block.

      Locked blocks may not be purged by low-memory actions whilst the lock
      is in place.

      Blocks are indexed by block indices. The standard block sizes can
      discovered by calling the sw_flt_ifstore_api::if_blockWidth() and
      sw_flt_ifstore_api::if_blockHeight() in the sw_flt_ifstore_api callback
      interface. The block index is the pixel coordinate divided by the block
      height or width appropriately.

      This method is required. If this method pointer is NULL on registration,
      it will be inherited from the filter's superclass (if any).

      \param[in] idata The filter implementation instance in which the
      block is being locked. The instance pointer may be used to locate an
      image store to pass to the routines in the sw_flt_ifstore callback
      API to satisfy this request.

      \param lock A boolean indicating whether the block should be locked (if
      TRUE), or unlocked (if FALSE).

      \param x The horizontal index of the block to access.

      \param y The vertical index of the block to access.

      \param plane A zero-based index for separated color planes, or -1 to
      indicate all planes.

      \param[in] block A pointer to a filter block structure, previously
      filled in by \p getimageblock().

      \return SW_FLT_SUCCESS if the request was fulfilled, otherwise one
      of the sw_flt_result error codes.
  */
  sw_flt_result (RIPCALL *lockimageblock)(sw_flt_instance *idata,
                                          HqBool lock, int32 x,
                                          int32 y, int32 plane,
                                          sw_flt_image_block *block);

  /** \brief
      Frees the data structures associated with a particular block of image
      data.

      Blocks are indexed by block indices. The standard block sizes can
      discovered by calling the sw_flt_ifstore_api::if_blockWidth() and
      sw_flt_ifstore_api::if_blockHeight() in the sw_flt_ifstore_api callback
      interface. The block index is the pixel coordinate divided by the block
      height or width appropriately.

      This method is required. If this method pointer is NULL on registration,
      it will be inherited from the filter's superclass (if any).

      \param[in] idata The filter implementation instance in which the
      block is being freed. The instance pointer may be used to locate an
      image store to pass to the routines in the sw_flt_ifstore callback
      API to satisfy this request.

      \param x The horizontal index of the block to access.

      \param y The vertical index of the block to access.

      \param plane A zero-based index for separated color planes, or -1 to
      indicate all planes .

      \param[out] freed A pointer to a boolean value, which should be set to
      TRUE if any memory was actually recovered.

      \return SW_FLT_SUCCESS if the request was fulfilled, otherwise one
      of the sw_flt_result error codes.
  */
  sw_flt_result (RIPCALL *freeimageblock)(sw_flt_instance *idata,
                                          int32 x, int32 y,
                                          int32 plane, HqBool *freed);

  /** \brief Free blocks not needed from previous filters.

      This method is called in low memory situations. It should work out
      which blocks are not needed from the previous filter instance, and call
      the \p freeimageblock() method on that instance to free those blocks.
      Note that this method should not free blocks in this filter instance.

      To maximise the amount of memory recovered, this method should call \p
      free_unneeded_blocks() on the previous filter instance after making
      individual \p freeimageblock() calls.

      This method is required. If this method pointer is NULL on registration,
      it will be inherited from the filter's superclass (if any).

      \param[in] idata The filter implementation instance which should clean
      its required blocks. The instance pointer may be used to locate the
      store parameter to pass to the routines in the sw_flt_ifstore
      callback API to satisfy this request.

      \param[out] purgedBlocks A pointer a counter, which should be
      incremented with the number of calls to the previous instance's \p
      freeimageblock() method which actually freed memory, and also
      incremented by the number of blocks freed by calling the previous
      instance's \p free_unneeded_blocks() method.

      \return SW_FLT_SUCCESS if the request was fulfilled, otherwise one
      of the sw_flt_result error codes.
  */
  sw_flt_result (RIPCALL *free_unneeded_blocks)(sw_flt_instance *idata,
                                                uint32 *purgedBlocks);

  /** \brief Frees the image store associated with a filter.

      This method is typically called after filtering has occured, and if the
      filter isn't a terminal filter.

      This method is required. If this method pointer is NULL on registration,
      it will be inherited from the filter's superclass (if any).

      \param[in] idata The filter implementation instance which should clean
      its image store. The instance pointer may be used to locate the store
      parameter to pass to the routines in the sw_flt_ifstore callback
      API to satisfy this request.

      \param[out] purgedBlocks A pointer a counter, which should be
      incremented with the number of calls to the previous instance's \p
      freeimageblock() method which actually freed memory, and also
      incremented by the number of blocks freed by calling the previous
      instance's \p free_unneeded_blocks() method.

      \return SW_FLT_SUCCESS if the request was fulfilled, otherwise one
      of the sw_flt_result error codes.
  */
  sw_flt_result (RIPCALL *free_imstore)(sw_flt_instance *idata);

  /** \brief A security check.

      This method is optional. If this method pointer is
      NULL on registration, it will be inherited from the filter's superclass
      (if any).
   */
  void (RIPCALL *SW_FLT_security)(sw_flt_instance *idata,
                                  void *buffer, uint32 size);


  /** Filter flags are a combination of the sw_flt_flags values, indicating
      the filter's processing requirements. */
  sw_flt_flags flags;

  /** The default position of the filter in the pipeline. Pre-DL filters must
      have a value in the range 1..9999. Post-DL filters must have a value in
      the range 10001-19999. */
  int32 pipepos;

  /** Class variable to specify the colorspaces the image filter will get
      presented with. */
  sw_flt_colorspace notifiable_colorspaces;

  /** Class variable to specify the compressions the image filter will get
      presented with. */
  sw_flt_compression notifiable_compressions;

  /** Class variable to specify the pixel formats the image filter will get
      presented with. */
  sw_flt_pixfmt notifiable_pixel_formats;

  /** Class variable to specify the acceptable input colorspaces for the
      image filter. The RIP may choose to convert the image into one of
      these acceptable colorspaces. */
  sw_flt_colorspace acceptable_colorspaces;

  /** Class variable to specify the preferred input colorspace for the
      image filter. If the RIP is converting the image to an acceptable
      colorspace, this one will be chosen in preference. */
  sw_flt_colorspace preferred_colorspace;

  /** Class variable to specify the acceptable input pixel formats for the
      image filter. The RIP may choose to convert the image into one of these
      acceptable pixel formats. */
  sw_flt_pixfmt acceptable_pixel_formats;

  /** Class variable to specify the preferred input pixel format for the
      image filter. If the RIP is converting the image to an acceptable
      pixel format, this one will be chosen in preference. */
  sw_flt_pixfmt preferred_pixel_format;

  /** Class variable to specify the colorspace(s) the image filter can
      output. */
  sw_flt_colorspace output_colorspace;

  /** Class variable to specify the pixel format(s) the image filter can
      output. */
  sw_flt_pixfmt output_pixel_format;

  /** A datum specifying the image metadata that the image filter is
      interested in using when presented with the image. */
  const sw_datum *requested_meta_data;

  /** A datum specifying the device configuration parameters that the image
      filter is interested in using when presented with the image. */
  const sw_datum *requested_device_config;

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
};

/** \brief Callback API for image filter store
 *
 * The structure sw_flt_ifstore_api defines the RIP functions that an image
 * filter can call. It's intended that this be fairly static, and changes
 * that are needed be made in a compatible manner. There are more call-backs
 * here than is common with other plug-ins types.
 *
 * Versioning of this structure is tied to the sw_flt_api version. The RIP
 * expects only callbacks present in the implementation registered to be
 * called.
 */
struct sw_flt_ifstore_api
{
  /* Entries present in SW_FLT_API_VERSION_20071112: */

  /** The standard block width (in bytes not pixels) of a block. Individual
      blocks may be smaller but not larger than this. Normally 128 bytes. */
  uint32 (RIPCALL *if_blockWidth)(void);

  /** The standard block height of a block. Individual blocks may be smaller
      but not larger than this. Normally 128. */
  uint32 (RIPCALL *if_blockHeight)(void);

  /** Returns the size of the image store (in blocks x by y), and the number
      of planes (colorants) in the image. */
  void (RIPCALL *if_store_getblock_dims)(sw_flt_store *ims ,
                                         int32 *x, int32 *y, int32 *np);

  /** Returns TRUE/FALSE depending on if the store has all the specified planes
      for the specified block. */
 HqBool (RIPCALL *if_store_hasblock)(sw_flt_store *ims ,
                                     int32 x , int32 y, int32 plane);

  /** Returns the data for a single plane of an image block. rbuf will point
      to the data, and it'll be rbytes long. Note, make sure that the store has
      the data (use if_store_hasblock() above) before calling this method. */
  HqBool (RIPCALL *if_store_getblock_plane)(sw_flt_store *ims,
                                            int32 bx , int32 by ,
                                            int32 plane ,
                                            uint8 **rbuf , uint32 *rbytes);

  /** Makes sure that memory is allocated for the specified block/plane. */
  HqBool (RIPCALL *if_store_block_alloc)(sw_flt_store *ims,
                                         int32 bx , int32 by ,
                                         int32 plane,
                                         HqBool reuse_blists);

  /** Makes sure that memory is allocated for the specified block/plane, and
      then returns the data in rbuf/rbytes. You would use this method for
      creating a new block, and use if_store_getblock_plane() to get the data
      from an existing block. */
  HqBool (RIPCALL *if_store_block_alloc_and_get)(sw_flt_store *ims,
                                                 int32 bx , int32 by ,
                                                 int32 plane, uint8 **rbuf,
                                                 uint32 *rbytes,
                                                 HqBool reuse_blists);

  /** Locks the block (so it won't get disposed of or moved). It also
      increments a reference count on the block. */
  HqBool (RIPCALL *if_store_block_lock)(sw_flt_store *ims ,
                                        int32 bx, int32 by, int32 plane);

  /** Decrements the reference count on the block/plane and if the ref count
      goes to zero it then unlocks the block. */
  HqBool (RIPCALL *if_store_block_unlock)(sw_flt_store *ims ,
                                          int32 bx, int32 by, int32 plane,
                                          HqBool disposable);

  /** Frees the memory allocated for the block/plane */
  HqBool (RIPCALL *if_store_block_free)(sw_flt_store *ims ,
                                        int32 bx, int32 by, int32 plane,
                                        HqBool *freed);

  /** Frees the memory allocated for the image store */
  void (RIPCALL *if_storefree)(sw_flt_store *ims);

  /** Returns TRUE/FALSE depending upon us having all the planes of the
      specified block */
  HqBool (RIPCALL *if_have_complete_block)(sw_flt_store *ims,
                                           int32 x, int32 y, int32 plane);

  /** Returns TRUE/FALSE depending upon us having all the planes of the
      specified block AND all the planes of all the blocks touching */
  HqBool (RIPCALL *if_have_complete_surrounding_blocks)(sw_flt_store *ims,
                                                        int32 x, int32 y,
                                                        int32 plane);

  /** Locates memory for a block, from the global blist list, or it alloates
      one if necessary. */
  HqBool (RIPCALL *if_global_blist_locate)(uint32 abytes, uint32 tbytes,
                                           sw_flt_blist **pblist,
                                           uint8 **rbuf, uint32 *rbytes);

  /** Releases a blist back to the global blist list, it also gets removed from
      pblist. */
  void (RIPCALL *if_release_blist_to_global)(sw_flt_blist *blist,
                                             sw_flt_blist** pblist);

  /** Find a filter, given its name. */
  HqBool (RIPCALL *if_find_named_filter)(const uint8 *filter_name, size_t len,
                                         sw_flt_api **api);

  /** Make sure that we have space allocated for an image block. */
  sw_flt_result (RIPCALL *if_allocimageblock)(sw_flt_instance *idata,
                                              int32 x, int32 y,
                                              int32 plane,
                                              sw_flt_image_block* im_block);

  /** Make sure that we have space allocated for the planes in an image
      block. */
  sw_flt_result (RIPCALL *if_allocplanes)(sw_flt_instance *idata,
                                          sw_flt_image_plane **plane);


  /** Frees the data allocated above, the plane may end up on the
      filter_instance's free_plane list. */
  void (RIPCALL *if_freeplanes)(sw_flt_instance *filter_instance,
                                sw_flt_image_plane *plane);

  /** Deterimine the size (in blocks) of the image, and the number of
      planes. */
  void (RIPCALL *if_getblock_dims)(sw_flt_instance *filter,
                                   int32 *nx, int32 *ny,
                                   int32 *np );

  /** This is a routine that's intended to be used by filters that don't
      modify image data. */
  sw_flt_result (RIPCALL *if_steal_image_block)(int32 x, int32 y,
                                                int32 plane,
                                                sw_flt_instance *from_filter,
                                                sw_flt_instance *to_filter,
                                                sw_flt_image_block* from_block,
                                                sw_flt_image_block* to_block);

  /* End of entries present in SW_FLT_API_VERSION_20071112 */
};

/** \brief This routine makes an image filtering implementation known to the
 * RIP.
 *
 * It can be called any number of times with different implementations of the
 * image filtering API. Every implementation registered may have multiple
 * instances constructed.
 *
 * \param[in] implementation The API implementation to register. This pointer
 * will be returned through the sw_flt_api::init() and sw_flt_api::finish()
 * calls, and also will be in the implementation member field of every
 * instance created, so the pointer can be in dynamically allocated memory.
 * Implementations may be subclassed to hold class-specific private data by
 * defining a subclass structure containing the sw_flt_api structure as its
 * first member. Individual methods may then downcast their implementation
 * pointers to subclass pointers, and use those to get at the class data.
 * e.g.,
 *
 * \code
 * typedef struct my_implementation {
 *   sw_flt_api super ; // must be first entry
 *   sw_memory_instance *mem ;
 *   struct my_class_data *dynamic ;
 * } my_implementation ;
 *
 * static HqBool RIPCALL my_init(sw_flt_api *impl, const sw_flt_init_params *params)
 * {
 *   my_implementation *myimpl = (my_implementation *)impl ; // downcast to subclass
 *   // save global memory allocator:
 *   myimpl->mem = params->mem ;
 *   myimpl->dynamic = NULL ;
 *   return TRUE ;
 * } ;
 *
 * static sw_flt_result RIPCALL my_construct(sw_flt_instance *inst,
 *                                           const sw_flt_construct_params *params)
 * {
 *   my_implementation *myimpl = (my_implementation *)inst->implementation ; // downcast to subclass
 *   myimpl->dynamic = myimpl->mem->implementation->alloc(myimpl->mem, 1024) ;
 * } ;
 *
 * const static my_implementation module = {
 *   { // sw_flt_api initialisation
 *   },
 *   NULL, // global memory allocator
 *   NULL  // global allocations
 * } ;
 *
 * // Call SwRegisterFLT(&module.super) after SwInit() to register this module.
 * \endcode
 */
sw_api_result RIPCALL SwRegisterFLT(sw_flt_api *implementation);

typedef sw_api_result (RIPCALL *SwRegisterFLT_fp_t)(sw_flt_api *implementation);

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWFLT_H__ */
