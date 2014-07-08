/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_screening!swhtm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * This header file provides the interface for screening modules.
 */

#ifndef __SWHTM_H__
#define __SWHTM_H__

/** \defgroup PLUGIN_swhtm Halftone module interface
 * \ingroup interface
 * \{
 */

#include <stddef.h>             /* size_t */
#include "ripcall.h"            /* RIPCALL */
#include "swapi.h"              /* Common API return values and definitions */
#include "swmemapi.h"           /* sw_memory_instance */
#include "swdataapi.h"          /* sw_data_api */

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Version numbers defined for the HTM API
 */
enum {
  SW_HTM_API_VERSION_20070525 = 1, /**< Obsolete as of 20071110. */
  SW_HTM_API_VERSION_20071110,     /**< Obsolete as of 20100414. */
  SW_HTM_API_VERSION_20100414      /**< Current version. */
  /* new versions go here */
#ifdef CORE_INTERFACE_PRIVATE
  , SW_HTM_API_VERSION_NEXT /* Do not ever use this name. */
  /* SW_HTM_API_VERSION is provided so that the Harlequin Core RIP can test
     compatibility with current versions, without revising the registration
     code for every interface change.

     Implementations of sw_htm_api within the Harlequin Core RIP should NOT
     use this; they should have explicit version numbers. Using explicit
     version numbers will allow them to be backwards-compatible without
     modifying the code for every interface change.
  */
  , SW_HTM_API_VERSION = SW_HTM_API_VERSION_NEXT - 1
#endif
} ;

/** \brief Return values for screening modules */
enum {
  /* Success codes present in SW_HTM_API_VERSION_20071110: */
  SW_HTM_SUCCESS = 0 ,/**< Success return value for sw_htm_api methods. */
  /* End of success codes present in SW_HTM_API_VERSION_20071110 */

  /* Errors present in SW_HTM_API_VERSION_20071110: */
  /** Non-specific error, also minimum error value. Please avoid using this
      if possible. */
  SW_HTM_ERROR = 1,
  /* These map directly to PostScript errors. */
  SW_HTM_ERROR_IOERROR,     /**< Problem accessing file resource */
  SW_HTM_ERROR_LIMITCHECK,  /**< A hard limit has been exceeded. */
  SW_HTM_ERROR_RANGECHECK,  /**< Parameter values are out of range. */
  SW_HTM_ERROR_TYPECHECK,   /**< Parameter values are the wrong type. */
  SW_HTM_ERROR_MEMORY,      /**< Memory allocation failed. */
  SW_HTM_ERROR_CONFIGURATIONERROR, /**< Incorrect configuration. */
  /* The remainder are specific to this interface. */
  /** The version of a callback API is insufficient for this module. */
  SW_HTM_ERROR_VERSION,
  /** The module does not recognise the supplied halftone instance value. */
  SW_HTM_ERROR_BAD_HINSTANCE,
  /** The module does not support the source raster bit depth. */
  SW_HTM_ERROR_UNSUPPORTED_SRC_BIT_DEPTH,
  /** The module does not support the destination raster bit depth. */
  SW_HTM_ERROR_UNSUPPORTED_DST_BIT_DEPTH
  /* End of errors present in SW_HTM_API_VERSION_20071110 */
} ;

/** \brief Type of results returned by screening modules. */
typedef int32 sw_htm_result ;

/** \brief A type definition for an implementation of the modular halftone
 * interface.
 */
typedef struct sw_htm_api sw_htm_api ;

/** \brief Raster coordinates, widths, heights etc.
 */
typedef int32 sw_htm_coord ;


/** \brief Raster resolution, usually dots per inch,
 * expressed as a floating-point value.
 */
typedef double sw_htm_resolution ;


/** \brief Type of the callback to output a message to the RIP's monitor
 * device.
 *
 * \param[in] message An ASCII string to output, or a UTF-8 string
 * starting with the Unicode byte order mark: 0xEF,0xBB,0xBF.
 */
typedef void sw_message_fn(const char *message);


/** \brief Colorant types values.
 */
enum {
  /* Entries present in SW_HTM_API_VERSION_20071110: */
  SW_HTM_COLORANTTYPE_UNKNOWN = 0,
  SW_HTM_COLORANTTYPE_PROCESS,
  SW_HTM_COLORANTTYPE_SPOT,
  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} ;

/** \brief Storage unit for colorant types. */
typedef int32 sw_htm_coloranttype ;

/** \brief Values for special handling to apply to a colorant.
 *
 * A screening module which doesn't understand special handling should
 * just assume colorants are normal ones. Similarly, an unexpected
 * value should be treated the same as SW_HTM_SPECIALHANDLING_NONE.
 */
enum {
  /* Entries present in SW_HTM_API_VERSION_20071110: */
  SW_HTM_SPECIALHANDLING_NONE = 0,       /**< Nothing unusual here. */
  SW_HTM_SPECIALHANDLING_OPAQUE,         /**< Ink is opaque. */
  SW_HTM_SPECIALHANDLING_OPAQUEIGNORE,   /**< Ink is opaque. */
  SW_HTM_SPECIALHANDLING_TRANSPARENT,    /**< Varnish layers. */
  SW_HTM_SPECIALHANDLING_TRAPZONES,      /**< Trap zone overlay. */
  SW_HTM_SPECIALHANDLING_TRAPHIGHLIGHTS  /**< Trapping highlights overlay. */
  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} ;

/** \brief Storage unit for colorant handling. */
typedef int32 sw_htm_colorant_handling ;

/** \brief Information about a colorant in a raster.
 */
typedef struct sw_htm_colorant_info {
  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** The separation to which this entry refers.
    * The first separation is number 1.
   */
  unsigned int separation ;

  /** The index of the channel, within the separation, to which this
   * colorant is mapped.
   * The first channel is index 0.
   */
  unsigned int channel ;

  /** The type of colorant.
   */
  sw_htm_coloranttype colorant_type ;

  /** Any special handling which should be applied to the colorant.
   */
  sw_htm_colorant_handling special_handling ;

  /* End of entries present in SW_HTM_API_VERSION_20071110 */

  /** The name of the colorant.
   */
  uint8 *name;
  /** The length of \a name.
   */
  size_t name_len;

  /** The original name of the colorant.
   * This is often the same as \a name.
   */
  uint8 *original_name;
  /** The length of \a original_name.
   */
  size_t original_name_len;

  /* End of entries present in SW_HTM_API_VERSION_20071122 */
} sw_htm_colorant_info ;


/** \brief Raster interleaving styles values.
 */
enum {
  /* Entries present in SW_HTM_API_VERSION_20071110: */
  SW_HTM_RASTER_INTERLEAVE_BAND = 1, /**< Band-interleaved raster. */
  SW_HTM_RASTER_INTERLEAVE_FRAME     /**< Frame-interleaved raster. */
  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} ;

/** \brief Storage unit for raster interleaving. */
typedef int32 sw_htm_raster_interleaving ;

/** \brief Values for the fundamental color space of a raster.
 */
enum {
  /* Entries present in SW_HTM_API_VERSION_20071110: */
  SW_HTM_COLORSPACE_UNKNOWN = 0,  /**< Raster colorspace is unknown. */
  SW_HTM_COLORSPACE_GRAY,         /**< Raster colorspace is Gray. */
  SW_HTM_COLORSPACE_RGB,          /**< Raster colorspace is RGB. */
  SW_HTM_COLORSPACE_CMY,          /**< Raster colorspace is CMY. */
  SW_HTM_COLORSPACE_RGBK,         /**< Raster colorspace is RGBK. */
  SW_HTM_COLORSPACE_CMYK,         /**< Raster colorspace is CMYK. */
  SW_HTM_COLORSPACE_Lab,          /**< Raster colorspace is Lab. */
  SW_HTM_COLORSPACE_N             /**< Raster colorspace is N-channel. */
  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} ;

/** \brief Storage unit for raster colorspace. */
typedef int32 sw_htm_colorspace ;

/** \brief Information about the destination raster.
 */
typedef struct sw_htm_raster_info {
  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** The \a x (horizontal) resolution, in dots per inch,
   * expressed as a floating-point number.
   */
  sw_htm_resolution x_resolution ;

  /** The \a y (vertical) resolution, in dots per inch,
   * expressed as a floating-point number.
   */
  sw_htm_resolution y_resolution ;

  /** The width of the raster, in pixels */
  sw_htm_coord  width ;

  /** The height of the raster, in pixels */
  sw_htm_coord  height ;

  /** The height of each band, in lines (pixels).
   * For band interleaved, the same number of lines are present for
   * each channel of the raster.
   * Note, however, that the last band in a page, frame or separation
   * may actually contain fewer lines than the rest. The actual number
   * present is passed in the call to sw_htm_api::DoHalftone().
   */
  sw_htm_coord  band_height ;

  /** The number of bits per pixel for each channel of the raster.
   */
  unsigned int bit_depth ;

  /** The fundamental colorspace of the raster.
   * One of the \c SW_HTM_COLORSPACE_* values.
   */
  sw_htm_colorspace colorspace ;

  /** The number of process colorants in the raster.
   * This is also the number of fixed colorant channels.
   */
  unsigned int num_process_colorants ;

  /** The total number of colorants in the \a colorant_infos array.
   * This is the total number of colorants mapped to raster channels,
   * at the time the screening module is called, for all separations
   * of the page so far.
   * See \a colorant_infos, below, for more information.
   */
  unsigned int num_colorant_infos ;

  /** Array of colorant information structures.
   * This will have one entry for each colorant in each separation
   * of the page so far, giving the colorant name and the raster
   * channel index number to which the colorant is mapped.
   * Spot colors will not necessarily appear in the list at the time
   * sw_htm_api::HalftoneSelect() is called, because they may get added later.
   * Raster channels for fixed colorants may be omitted, for example
   * due to separation omission or progresive separation styles, but
   * this does not mean that those channels won't be present, merely
   * that they have no colorants mapped to them for that separation.
   */
  const sw_htm_colorant_info *colorant_infos ;

  /** Raster interleaving.
   * This is one of the the \c SW_HTM_RASTER_INTERLEAVE_* values.
   * Monochrome is treated as SW_HTM_RASTER_INTERLEAVE_BAND, but
   * with only a single channel present.
   */
  sw_htm_raster_interleaving interleaving ;

  /** Whether the raster is separated.
   *
   * The value will be \c FALSE for composite and genuinely monochrome
   * rasters.
   *
   * \a separating implies that the use of interrelated channels may not be
   * possible, because only the color values for the separation(s) being
   * produced, at any one time, will be available to the RIP.
   *
   * As an example, consider a separated job being output to a composite
   * device. When halftoning the Cyan separation, the RIP will use the
   * halftone instance for the Cyan colorant, but the channel to which
   * it is mapped could be Black (for conventional separations) or Cyan
   * (for colored or progressive separations). The other channels may or
   * may not be blank. The RIP only knows the value of the Cyan component,
   * so it could not satisfy the need of an interrelated channels halftone
   * needing all four CMYK values.
   */
  HqBool        separating ;

  /** The number of separations being produced.
   * Actually, this is better thought of as the minimum number.
   * Spot separations will not necessarily be included at the time
   * sw_htm_api::HalftoneSelect() is called, because they may get added later.
   */
  unsigned int num_separations ;

  /** The ordinal job number of the job being processed.
   */
  unsigned int job_number ;

  /** The ordinal number of the page within the job.
   * This is not necessarily the same as any document page number.
   */
  unsigned int page_number ;

  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} sw_htm_raster_info ;


/** \brief Information passed to the screening module for \c HalftoneSelect.
 */
typedef struct sw_htm_select_info {
  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** Information about the destination raster (page or separation).
   */
  const sw_htm_raster_info *dst_raster_info ;

  /** The source bit depth to be used.
   *
   * If non-zero, the screening module must adopt the same source bit depth.
   *
   * This may be zero if no other halftone instances have been used on the page
   * yet, in which case the sceening module is at liberty to choose whether to
   * opt for 8- or 16-bit source rasters, but note that any other screening
   * modules used on the page must also comply.
   */
  unsigned int src_bit_depth ;

  /** If a screening module wishes to honor the \a /ScreenRotate systemparam
   * (a Harlequin extension), this is the number of degrees of rotation
   * present in the pagedevice, and by which any screen should be rotated.
   * It is zero if \a /ScreenRotate is false.
   */
  float         screen_rotation ;

  /** The \a x (horizontal) value from the sethalftonephase operator,
   * or its equivalent.
   */
  sw_htm_coord  phasex ;

  /** The \a y (vertical) value from the sethalftonephase operator,
   * or its equivalent.
   */
  sw_htm_coord  phasey ;

  /** The maximum number of renderer threads the RIP will use.
   *
   * \note The RIP may opt to use fewer than this at render time, but
   * it will never use more.
   */
  unsigned int max_render_threads;

  /** An implementation pointer for access to the sw_datum values in this
      structure. */
  const sw_data_api *data_api;

  /**
   * A string sw_datum containing the name of the colorant for which this
   * halftone is being selected. A colorant name of Default is used for
   * halftones when the colorants not explicitly declared.
   */
  sw_datum colorantname ;

  /** A dictionary sw_datum structure that contains the keys
   * and values from the halftone dictionary. Note that some keys in halftone
   * dictionaries are consumed by the RIP and not passed on to the screening
   * module. These include \c Background, \c DCS, \c DetectSeparation, \c
   * InheritAngle, \c InheritFrequency, \c Override, \c OverrideAngle, \c
   * OverrideFrequency, \c Positions and \c TransferFunction.
   */
  sw_datum halftonedict ;

  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} sw_htm_select_info ;


/** \brief Band ordering constraints imposed by a screening module.
 *
 * Band ordering constraints apply to individual channels, or sets of
 * interrelated channels. The RIP does not guarantee the order in which
 * the individual channels, or sets of interrelated channels, are
 * halftoned with respect to any other channel, or set. It only arranges
 * to ensure that for a particular channel, or set, the bands are
 * halftoned in the appropriate order.
 *
 * For example, ascending order will ensure that channel 0 of the
 * first band is halftoned before channel 0 of the second band, which
 * in turn is before channel 0 of the third band etc.
 * Whilst the same order will apply within the bands being halftoned
 * for channel 1, there is no fixed relationship between the channels.
 * The RIP might halftone the first three bands of channel 0 before
 * commencing channel 1.
 */
enum {
  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** The module can accept bands in any order.
   */
  SW_HTM_BAND_ORDER_ANY = 0,

  /** The module needs bands to be processed in ascending order.
   * The first band will be the top of the page.
   */
  SW_HTM_BAND_ORDER_ASCENDING,

  /** The module needs bands to be processed in descending order.
   *
   * \attention This option is not yet supported.
   *
   * The first band will be the bottom of the page.
   */
  SW_HTM_BAND_ORDER_DESCENDING

  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} ;

/** \brief Storage unit for band ordering. */
typedef int32 sw_htm_band_ordering ;

/** \brief Render information passed to sw_htm_api::RenderInitiation() and
    sw_htm_api::DoHalftone().
 */
typedef struct sw_htm_render_info {
  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** The width of the output raster, in pixels.
   */
  sw_htm_coord  width ;

  /** The height of the output raster, in pixels. */
  sw_htm_coord  height ;

  /** The number of bits per pixel for each channel in the source raster.
   *
   * The same number will apply to any object properties map.
   */
  unsigned int src_bit_depth ;

  /** The number of bits per pixel for each channel in the destination raster.
   */
  unsigned int dst_bit_depth ;

  /** The number of bytes between consecutive lines of the mask raster.
   * This is the number to add to the byte address of one line in order
   * to get the byte address of the next line in the raster.
   */
  int msk_linebytes;

  /** The number of bytes between consecutive lines of the source raster.
   * This is the number to add to the byte address of one line in order
   * to get the byte address of the next line in the raster.
   *
   * The same number will apply to any object properties map.
   */
  int src_linebytes;

  /** The number of bytes between consecutive lines of the destination raster.
   * This is the number to add to the byte address of one line in order
   * to get the byte address of the next line in the raster.
   */
  int dst_linebytes;

  /** Whether this render is a partial paint.
   * If \a partial_paint is \c TRUE, the RIP is rendering the content of its
   * display list so far, usually due to running out of memory. In extreme
   * cases, several partial paints may occur before the final render which
   * completes the page.
   * During partial paint, very little memory is available (just a small
   * amount the RIP keeps in reserve) so screening modules must avoid trying
   * to allocate more for themselves.
   */
  HqBool partial_paint ;

  /** The maximum number of renderer threads the RIP will use.
   * Note that the RIP may opt to use fewer than this, but will never
   * use more.
   */
  unsigned int max_render_threads;

  /** The ordinal job number of the job being rendered.
   */
  unsigned int job_number ;

  /** The ordinal number of the page within the job.
   * This is not necessarily the same as any document page number.
   */
  unsigned int page_number ;

  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} sw_htm_render_info ;



/** \typedef sw_htm_raster_unit
 * \brief The unit of size used to access and point to rasters.
 *
 * All raster buffers and pointers are also aligned to units of at least
 * this size, though the RIP reserves the right to arrange raster alignment
 * in multiples of this size.
 *
 * All 1, 2 and 4 bit rasters should also be accessed in units of this size.
 * These rasters are physically stored in memory as a row of unsigned values
 * of the relevant number of bits in size. Accessing the raster as though it
 * were a row of bytes, instead, will cause groups of bytes to appear in the
 * reverse order on little-endian architectures such as Intel processors.
 *
 * On the other hand, 8 and 16 bit rasters are only ever accessed in units of
 * \c uint8 or \c uint16 as appropriate.
 */

/** \def SW_HTM_RASTER_UNIT_BITS
 * \brief The number of bits in each sw_htm_raster_unit.
 */

#ifndef PLATFORM_IS_64BIT
#define SW_HTM_RASTER_UNIT_BITS (32)
typedef uint32 sw_htm_raster_unit ;
#else
#define SW_HTM_RASTER_UNIT_BITS (64)
typedef uint64 sw_htm_raster_unit ;
#endif

/** \brief Type definition for a raster pointer. */
typedef sw_htm_raster_unit *sw_htm_raster_ptr ;


/** \brief These give a hint as to the state of the mask bitmap, so as to
 * permit optimisations when all bits are known to be set or no bits are set.
 *
 * \note The RIP might use \c SW_HTM_BITMAP_NORMAL when it can't be certain
 * whether either of the other conditions exists - it doesn't always guarantee
 * to carry out an exhaustive check for the boundary conditions.
 */
enum {
  /* Entries present in SW_HTM_API_VERSION_20071110: */
  SW_HTM_MASKHINT_ALL_OFF = 0 , /*!< No bits are set in the bitmap. */
  SW_HTM_MASKHINT_NORMAL ,      /*!< Some bits are set, some are not. */
  SW_HTM_MASKHINT_ALL_ON        /*!< All bits are set in the bitmap. */
  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} ;

/** \brief Storage unit for mask hints. */
typedef int32 sw_htm_mask_hint ;

/** \brief Structure describing what the RIP wants sw_htm_api::DoHalftone()
    to do.
 */
typedef struct sw_htm_dohalftone_request {
  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** \brief The screening module calls this to signal to the RIP that
   * the module has finished processing the request.
   *
   * The \a result argument indicates the success or otherwise of the
   * halftone processing.
   *
   * \note After calling this function, none of the buffers or other
   * structures referred to by this request should be touched by the
   * module - they all revert to being the exclusive property of the RIP.
   *
   * \param[in] request Pointer to the structure detailing the request.
   *
   * \param[in] result The value SW_HTM_SUCCESS if halftoning was
   * successful, otherwise one of the sw_htm_result error constants.
   */
  void (RIPCALL *DoneHalftone)(const struct sw_htm_dohalftone_request *request,
                               sw_htm_result result) ;

  /** Overall information about the render.
   */
  const sw_htm_render_info         *render_info ;

  /** The ordinal number of the band being processed, within the page.
   */
  unsigned int                      band_num ;

  /** The position of the band in the overall raster, expressed as the
   * line-index (or y coordinate) of the first line in the band.
   */
  sw_htm_coord                      first_line_y ;

  /** The number of raster lines to be halftoned (for each channel).
   * This may be less than the overall band height, such as in the last
   * band of the page or frame.
   */
  unsigned int                      num_lines ;

  /** The index within the separation of the first or only channel.
   * This corresponds to the \c channel field of
   * \a sw_htm_colorant_info.
   * This is principally to help screening modules differentiate one
   * channel from another, e.g., when the same halftone instance is being
   * used to screen several channels independently of each other, such as
   * will often be the case when screening spot colors using the /Default
   * entry of a type-5 halftone dictionary.
   */
  int                               channel_id ;

  /** The ordinal number of the separation being rendered.
   * This corresponds to the \c separation field of
   * \a sw_htm_colorant_info. For composite pages, this will always be 1.
   */
  unsigned int                      separation_number ;

  /** The pointer to the mask bitmap.
   */
  sw_htm_raster_ptr                 msk_bitmap ;

  /** This hint is one of the \c SW_HTM_MASKHINT_* constants.
   * It is intended to allow some optimisation when the mask has all of its
   * bits set, or all of its bits clear.
   */
  sw_htm_mask_hint                  msk_hint ;

  /** The pointer to the object properties map.
   * This will be \c NULL unless the Boolean \a want_object_map was set
   * to \c TRUE in the \a sw_htm_halftone_details for the halftone instance.
   */
  sw_htm_raster_ptr                 object_props_map ;

  /** The number of raster channel pointers in the \a src_channels array.
   */
  unsigned int                      num_src_channels ;
  /** The array of raster channel pointers to the source channel(s).
   */
  sw_htm_raster_ptr                *src_channels ;

  /** The number of raster channel pointers in the \a dst_channels array.
   */
  unsigned int                      num_dst_channels ;
  /** The array of raster channel pointers to the destination channel(s).
   */
  sw_htm_raster_ptr                *dst_channels ;

  /* End of entries present in SW_HTM_API_VERSION_20071110 */

  /** Index of the current thread, an int in 0...max_render_threads-1. */
  unsigned int                     thread_index;

  /* End of entries present in SW_HTM_API_VERSION_20100414 */
} sw_htm_dohalftone_request ;


/** \brief An instance structure for the modular halftone API implementation.
 *
 * This is the definition of a modular halftone instance. The RIP allocates
 * memory for the instances, calling the sw_htm_api::HalftoneSelect() method
 * to complete the details. The RIP uses this value in calls to
 * sw_htm_api::DoHalftone() and sw_htm_api::HalftoneRelease(), to tell the
 * module which halftone is being referred to.
 *
 * The instance structure may be subclassed to hold private data by defining
 * a subclass structure containing this structure as its first member, and
 * using the size of that structure as the implementation's instance size.
 * Individual methods may then downcast their instance pointer parameters to
 * subclass pointers, and use the private data. e.g.,
 *
 * \code
 * typedef struct my_instance {
 *   sw_htm_instance super ; // must be first entry
 *   struct my_data *dynamic ;
 *   int32 other_fields ;
 * } my_instance ;
 *
 * static void RIPCALL my_HalftoneRelease(sw_htm_instance *inst)
 * {
 *   my_instance *myinst = (my_instance *)inst ; // downcast to subclass
 *   // free allocated data, if necessary:
 *   inst->mem->implementation->free(inst->mem, myinst->dynamic) ;
 * } ;
 *
 * const static sw_htm_api my_impl = {
 *   {
 *     SW_HTM_API_VERSION_20071110,
 *     (const uint8 *)"myname",
 *     (const uint8 *)("A long description of my module implementation"
 *                     "Copyright (C) 2007 Global Graphics Software Ltd."),
 *     sizeof(my_instance), // RIP will allocate this amount for instance
 *   },
 *   // ...more of sw_htm_api definition...
 *   my_HalftoneRelease,
 *   // ...rest of sw_htm_api definition...
 * } ;
 *
 * // Call SwRegisterHTM(&my_impl) after SwInit() to register this module.
 * \endcode
 *
 * The RIP will not touch memory beyond the size of the instance structure
 * for the implementation version registered.
 */
typedef struct sw_htm_instance {
  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** \brief Pointer to the API implementation.

       API methods for a halftone instance should always be called by
       indirecting through the instance's implementation field.

       This field is filled in by the RIP before sw_htm_api::HalftoneSelect()
       is called.
  */
  const sw_htm_api *implementation;

  /** \brief A memory allocator instance.

       This object is supplied by the RIP so that the halftone implementation
       can allocate memory using the RIP's memory allocator. halftone
       implementations should use this in preference to malloc() and free(),
       so that the RIP can track memory allocation and respond to low memory
       states more effectively.

       This field is filled in by the RIP before sw_htm_api::HalftoneSelect()
       is called.
  */
  sw_memory_instance *mem;

  /** The source (contone) raster bit depth this instance requires.
   * All halftones in use must use the same source raster bit depth.
   */
  unsigned int  src_bit_depth ;

  /** Whether screening this instance needs the object properties map
   * provided in calls to sw_htm_api::DoHalftone().
   *
   * If this is \c FALSE, the \a object_props_map field of the structure
   * sw_htm_dohalftone_request passed to sw_htm_api::DoHalftone() will always
   * be \c NULL.
   */
  HqBool        want_object_map ;

  /** Whether the halftone demands interrelated color channels. Setting this
   * to \c TRUE means that the screening module needs the RIP to screen
   * several channels at once in each single call to sw_htm_api::DoHalftone()
   * that uses this halftone instance. This must be \c FALSE for
   * frame-interleaved output.
   *
   * \attention Must always be \c FALSE at present, because the RIP does not
   * yet support interrelated channel screening.
   */
  HqBool        interrelated_channels ;

  /** The number of source (contone) raster channels the RIP must pass
   * to the screening module with each call to sw_htm_api::DoHalftone().
   * Ignored unless \a interrelated_channels is \c TRUE.
   */
  unsigned int  num_src_channels ;

  /** The number of destination (halftone) raster channels the RIP must pass
   * to the screening module with each call to sw_htm_api::DoHalftone().
   * Ignored unless \a interrelated_channels is \c TRUE.
   */
  unsigned int  num_dst_channels ;

  /** Set this to \c TRUE if sw_htm_api::DoHalftone() must be called even
   * when there are no pixels to be processed, i.e., the mask bitmap is all
   * zeroes. If this is \c FALSE, the RIP will skip the call to
   * sw_htm_api::DoHalftone() whenever it can.
   */
  HqBool        process_empty_bands ;

  /* End of entries present in SW_HTM_API_VERSION_20071110 */

  /** The number of previous bands that are needed by the screening
   * algorithm. The destination rasters for these will be available
   * during sw_htm_api::DoHalftone() at the addresses that they were
   * earlier presented at (so the module needs to remember
   * \c dst_channels from earlier calls). The other buffers from earlier
   * calls will not be available (but the module might store some of
   * that data in its own buffers). If the algorithm just works on the
   * current band, specify 0. */
  unsigned int latency;

  /** A function to output a message to the RIP's monitor device. The
   * length is limited to 1000 bytes including the null terminator.  End
   * the message with a new line (\n) to avoid the next message
   * (possibly from a different thread) being stuck to the end of it. */
  sw_message_fn *Message;

  /* End of entries present in SW_HTM_API_VERSION_20100414 */
} sw_htm_instance ;

/* -------------------------------------------------------------------------- */
/** \brief Collection structure for initialisation parameters. */
typedef struct sw_htm_init_params {
  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** \brief Global memory allocator.

      The memory allocators provided in the instances should only be used for
      allocations local to that instance. This memory allocator can be used
      for allocations which must survive individual instance lifetimes. */
  sw_memory_instance *mem ;

  /* End of entries present in SW_HTM_API_VERSION_20071110 */
} sw_htm_init_params ;


/* -------------------------------------------------------------------------- */
/**\brief The definition of an implementation of the modular halftoning
 * interface.
 *
 * Multiple halftone implementations may be registered with the RIP. Each
 * implementation may have multiple instances active at the same time.
 */
struct sw_htm_api {
  sw_api_info info ; /**< Version number, name, display name, instance size. */

  /* Entries present in SW_HTM_API_VERSION_20071110: */

  /** \brief The \p init() method is called before any other calls to the
      implementation.

      This method may be used to initialise any implementation-specific data.
      This method is optional.

      \param implementation The registered modular halftone implementation to
      be initialised.

      \param[in] params A structure containing callback APIs and parameters
      valid for the lifetime of the module. Any parameters that the
      implementation needs access to should be copied out of this structure
      into private storage for the registered implementation.

      \retval TRUE Success, indicating that the implementation is fully
      initialised.

      \retval FALSE Failure to initialise the implementation. If this is
      returned, the implementation will not be finalised.
  */
  HqBool (RIPCALL *init)(/*@in@*/ /*@notnull@*/ sw_htm_api *implementation,
                         /*@in@*/ /*@notnull@*/ const sw_htm_init_params *params) ;

  /** \brief The \p finish() method is called after all calls to the
      implementation or its instances.

      The implementation instances should not access any data owned by the
      RIP after this call, nor should they call any implementation or RIP
      callback API methods after this call. This method is optional.

      \param implementation A registered modular halftone implementation to
      finalise.
  */
  void (RIPCALL *finish)(/*@in@*/ /*@notnull@*/ sw_htm_api *implementation) ;

  /** \brief The RIP calls \p HalftoneSelect() when the PDL instigates the
   * equivalent of a PostScript sethalftone.
   *
   * \param[in,out] instance An incomplete instance of the sw_htm_instance
   * structure to complete. The RIP will allocate a structure of the size
   * presented in the implementation's sw_htm_api::info.instance_size field,
   * fill in the implementation and callback API instance pointers, and then
   * pass it to this routine. The \p HalftoneSelect() method is expected to
   * fill in the remaining fields. The implementation may sub-class the
   * instance to allocate private workspace by initialising the
   * sw_htm_api::info.instance_size larger than the size of the
   * sw_htm_instance structure, then downcasting the instance pointer in
   * method calls.
   *
   * \param[in] info Halftone selection information from the RIP. This
   * contains things like the halftone phase and a pointer to an information
   * structure describing the destination raster and colorants etc. It also
   * contains a data access API, a datum representing the colorant name for
   * which this halftone is being selected and a datum representing the
   * halftone dictionary. Any data accessed through this pointer MUST be
   * copied if the halftone instance wished to refer to it after the \p
   * HalftoneSelect() method has returned.
   *
   * \param[out] matches If the halftone module currently has an instance
   * selected that maps onto the same screen, it should store the existing
   * instance in this pointer and return SW_HTM_SUCCESS. In this case, the
   * instance under construction will be destroyed immediately, and there
   * will be no call to \p HalftoneRelease(). This will ensure that
   * equivalent screen definitions share the same \p DoHalftone() calls, and
   * will improve rendering efficiency. It is an error to store the instance
   * under construction through this pointer.
   *
   * \return SW_HTM_SUCCESS, or one of the sw_htm_result error constants.
   */
  sw_htm_result (RIPCALL *HalftoneSelect)(sw_htm_instance *instance,
                                          const sw_htm_select_info *info ,
                                          sw_htm_instance **matches) ;

  /** \brief The RIP calls \c HalftoneRelease() when it no longer needs a
   * reference to a particular halftone instance.
   *
   * \p HalftoneRelease() will be called once for each previously successful
   * \p HalftoneSelect(), with the instance pointer that is not required. The
   * implementation should clean up any resources associated with the
   * instance.
   *
   * \param[in] instance The halftone instance value originally constructed by
   * \c HalftoneSelect.
   */
  void (RIPCALL *HalftoneRelease)(sw_htm_instance *instance) ;


  /** \brief The RIP calls \p RenderInitiation() when it prepares to render a
   * page.
   *
   * \p RenderInitiation() is a class function. It will be called once for
   * each registered modular halftone instance that may have halftones
   * used on a page.
   *
   * In the case of color-separated output, \p RenderInitiation() takes place
   * once, before the RIP renders all the separations of a page.
   *
   * Because the RIP doesn't allow memory allocations during rendering,
   * any additional memory a module would like should be allocated
   * here. Note, however, that when the \p RenderInitiation() is for a
   * partial paint, no more memory will be available, so any mandatory
   * buffers should be preallocated during \c HalftoneSelect().
   *
   * This method is optional.
   *
   * \note In preparation for a partial paint, the RIP may choose to reduce
   * the band size used for rendering. This may involve several repeated
   * calls to \p RenderInitiation() until such time as the RIP reaches a good
   * compromise. When two consecutive \p RenderInitiation() calls occur,
   * without an intervening \p RenderCompletion(), the later \p
   * RenderInitiation() supersedes the earlier one. In such cases \p
   * RenderCompletion() will only be called once, because the superseded \p
   * RenderInitiation() calls are deemed never to have taken place.
   *
   * \param[in] implementation A pointer to the implementation registered
   * with the RIP.
   *
   * \param[in] render_info Pointer to a structure containing general
   * information about the upcoming render as a whole.
   *
   * \return SW_HTM_SUCCESS, or one of the sw_htm_result error constants.
   */
  sw_htm_result (RIPCALL *RenderInitiation)(sw_htm_api *implementation,
                                            const sw_htm_render_info *render_info ) ;

  /** \brief The RIP calls \p DoHalftone() when it wants the module to render
   * a halftone instance into a channel (or channels) within the RIP's band
   * buffers.
   *
   * \param[in] instance Pointer to a halftone instance selected by \p
   * HalftoneSelect().
   *
   * \param[in] request Pointer to a structure holding details of the RIP's
   * halftone request.
   *
   * \retval FALSE The module is unable to accept the request.  In this
   * case, the module must not touch any arguments passed to it, and the
   * RIP will deem this an unrecoverable failure.
   *
   * \retval TRUE The module accepts the request and will call
   * \c DoneHalftone() when done.  The success or failure of the overall
   * request is indicated by the \p result parameter of \c DoneHalftone().
   */
  HqBool (RIPCALL *DoHalftone)(sw_htm_instance *instance,
                               const sw_htm_dohalftone_request *request) ;

  /** \brief The RIP calls \p RenderCompletion() when it successfully finishes
   * rendering a page, or when it cancels or aborts rendering, for example
   * due to an error.
   *
   * If a module allocated more memory during \p RenderInitiation(), this is
   * where it should release that memory again.
   *
   * \p RenderCompletion() is a class function, it will be called once for
   * each registered modular halftone implementation that may have halftones
   * used on a page.
   *
   * This method is optional.
   *
   * \param[in] implementation Pointer to the implementation registered with
   * the RIP.
   *
   * \param[in] render_info A pointer to the general render information
   * structure. See \link sw_htm_api::RenderInitiation() \c
   * RenderInitiation() \endlink for more information.
   *
   * \param[in] aborted A Boolean indicating whether rendering has been
   * ended prematurely due to cancellation of the job or a rendering error.
   * The renderer does not distinguish cancellation from error conditions.
   * It merely lets the screening module know that the page or job did not
   * complete its rendering phase sucessfully.
   */
  void (RIPCALL *RenderCompletion)(sw_htm_api *implementation,
                                   const sw_htm_render_info *render_info,
                                   HqBool aborted) ;

  /** \brief The \p HtmSecurity() interface is not yet used.
   *
   * Its pointer should be set to NULL by early screening modules.
   */
  void (RIPCALL *HtmSecurity)(sw_htm_instance *instance,
                              void *buffer, uint32 size) ;

  /** This is a class variable describing the band ordering needs of the
   * screening module. Its value should be one of the \c SW_HTM_BAND_ORDER_*
   * constants.
   */
  sw_htm_band_ordering  band_ordering ;

  /** This is a class variable describing whether the module is reentrant.
   * Set this to \c FALSE if the RIP must never call \p DoHalftone() from
   * more than one thread at any one time.
   *
   * \note Even when \a reentrant is \c FALSE, the RIP does not warrant that
   * consecutive calls of \p DoHalftone() will be from the same thread.
   */
  HqBool        reentrant ;

  /* End of entries present in SW_HTM_API_VERSION_20071110 */

  /** \brief The RIP calls \p AbortHalftone() when it wants the module
   * to stop rendering an outstanding request issued by \c DoHalftone()
   * with the same arguments.
   *
   * The module must stop writing to its output buffers and then invoke
   * \c DoneHalftone() before returning (it is permissible to invoke it
   * from another thread). Because the calls can't be synchronized, it
   * may happen that the module has already called \c DoneHalftone()
   * between the RIP deciding to abort and actually invoking \p
   * AbortHalftone(). This is acceptable, but the module must be so
   * constructed that any such \c DoneHalftone() has, in fact, returned
   * before \p AbortHalftone() returns. The RIP will check if the
   * request has, in fact, terminated, and it is an error if it hasn't.
   *
   * \param[in] instance Pointer to a halftone instance selected by \p
   * HalftoneSelect().
   *
   * \param[in] request Pointer to a structure holding details of the RIP's
   * halftone request.
   */
  void (RIPCALL *AbortHalftone)(sw_htm_instance *instance,
                                const sw_htm_dohalftone_request *request);

  /* End of entries present in SW_HTM_API_VERSION_20100414 */
} ;


/** \brief This routine makes a modular halftone implementation known to the
 * rip.
 *
 * It can be called any number of times with different implementations of the
 * modular halftone API. Every implementation registered may multiple
 * instances constructed.
 *
 * \param[in] implementation The API implementation to register. This pointer
 * will be returned through the sw_htm_api::init() and sw_htm_api::finish()
 * calls, and also will be in the implementation member field of every
 * instance created, so the pointer can be in dynamically allocated memory.
 * Implementations may be subclassed to hold class-specific private data by
 * defining a subclass structure containing the sw_htm_api structure as its
 * first member. Individual methods may then downcast their implementation
 * pointers to subclass pointers, and use those to get at the class data.
 * e.g.,
 *
 * \code
 * typedef struct my_implementation {
 *   sw_htm_api super ; // must be first entry
 *   sw_memory_instance *mem ;
 *   struct my_class_data *dynamic ;
 * } my_implementation ;
 *
 * static HqBool RIPCALL my_init(sw_htm_api *impl, const sw_htm_init_params *params)
 * {
 *   my_implementation *myimpl = (my_implementation *)impl ; // downcast to subclass
 *   // save global memory allocator:
 *   myimpl->mem = params->mem ;
 *   myimpl->dynamic = NULL ;
 *   return TRUE ;
 * } ;
 *
 * static void RIPCALL my_HalftoneRelease(sw_htm_instance *inst)
 * {
 *   my_implementation *myimpl = (my_implementation *)inst->implementation ; // downcast to subclass
 *   myimpl->dynamic = myimpl->mem->implementation->alloc(myimpl->mem, 1024) ;
 * } ;
 *
 * const static my_implementation module = {
 *   { // sw_htm_api initialisation
 *   },
 *   NULL, // global memory allocator
 *   NULL  // global allocations
 * } ;
 *
 * // Call SwRegisterHTM(&module.super) after SwInit() to register this module.
 * \endcode
 */
sw_api_result RIPCALL SwRegisterHTM(sw_htm_api *implementation) ;

/* Typedef with same signature. */
typedef sw_api_result (RIPCALL *SwRegisterHTM_fn_t)(sw_htm_api *implementation) ;

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWHTM_H__ */
