/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swcmm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed
 * for any reason except as set forth in the applicable Global Graphics
 * license agreement.
 *
 * \brief This header file provides the interface for alternate color
 * management modules (CMMs) as required for pluggable color management.
 */


#ifndef __SWCMM_H__
#define __SWCMM_H__

/** \defgroup swcmm Color management module interface.
 * \ingroup interface
 * \{
 */


#include <stddef.h> /* size_t */
#include "ripcall.h" /* RIPCALL */
#include "swapi.h" /* SW_API_REGISTERED */
#include "swmemapi.h" /* sw_memory_instance */
#include "swblobapi.h" /* sw_blob_instance */

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Version numbers defined for the CMM API. */
enum {
  SW_CMM_API_VERSION_20070525 = 2, /**< Obsolete as of 20071109. */
  SW_CMM_API_VERSION_20070614,     /**< Obsolete as of 20071109. */
  SW_CMM_API_VERSION_20071109      /**< Current version. */
  /* new versions go here */
#ifdef CORE_INTERFACE_PRIVATE
  , SW_CMM_API_VERSION_NEXT /* Do not ever use this name. */
  /* SW_CMM_API_VERSION is provided so that the Harlequin Core RIP can test
     compatibility with current versions, without revising the registration
     code for every interface change.

     Implementations of sw_cmm_api within the Harlequin Core RIP should NOT
     use this; they should have explicit version numbers. Using explicit
     version numbers will allow them to be backwards-compatible without
     modifying the code for every interface change.
  */
  , SW_CMM_API_VERSION = SW_CMM_API_VERSION_NEXT - 1
#endif
} ;

/** \brief Return values for sw_cmm_api CMM functions. */
enum {
  /* Success codes present in SW_CMM_API_VERSION_20071109: */
  SW_CMM_SUCCESS = 0,      /**< Success return value for sw_cmm_api methods. */
  /* End of success codes present in SW_CMM_API_VERSION_20071109 */

  /* Errors present in SW_CMM_API_VERSION_20071109: */
  SW_CMM_ERROR = 1,         /**< Non-specific error, also minimum error
                                 value. Please avoid using this if
                                 possible. */
  SW_CMM_ERROR_IOERROR,     /**< Problem accessing blob */
  SW_CMM_ERROR_MEMORY,      /**< Memory allocation failed */
  SW_CMM_ERROR_INVALID,     /**< Invalid profile or transform */
  SW_CMM_ERROR_UNSUPPORTED, /**< Unsupported configuration */
  SW_CMM_ERROR_VERSION      /**< Callback API version is insufficient */
  /* End of errors present in SW_CMM_API_VERSION_20071109 */
} ;

/** \brief Type of return values from sw_cmm_api functions. */
typedef int sw_cmm_result ;

/** \brief Values of rendering intent for use in the intents array as
 * passed to sw_cmm_api::open_transform().
 *
 * The first four of these intents correspond to the standard ICC rendering
 * intents: SW_CMM_INTENT_PERCEPTUAL, SW_CMM_INTENT_RELATIVE_COLORIMETRIC,
 * SW_CMM_INTENT_SATURATION, and SW_CMM_INTENT_ABSOLUTE_COLORIMETRIC.
 *
 * The remaining two of these intents may also be used if supported by the
 * alternate CMM: SW_CMM_INTENT_ABSOLUTE_PERCEPTUAL,
 * SW_CMM_INTENT_ABSOLUTE_SATURATION. They are absolute variants of
 * Perceptual and Saturation that are derived in the same way that
 * AbsoluteColorimetric is derived from RelativeColorimetric.
 */
enum {
  /* Entries present in SW_CMM_API_VERSION_20071109: */
  SW_CMM_INTENT_PERCEPTUAL = 0,        /**< ICC perceptual. */
  SW_CMM_INTENT_RELATIVE_COLORIMETRIC, /**< ICC relative colorimetric. */
  SW_CMM_INTENT_SATURATION,            /**< ICC saturation. */
  SW_CMM_INTENT_ABSOLUTE_COLORIMETRIC, /**< ICC absolute colorimetric. */
  SW_CMM_INTENT_ABSOLUTE_PERCEPTUAL,   /**< Extended absolute perceptual. */
  SW_CMM_INTENT_ABSOLUTE_SATURATION    /**< Extended absolute saturation. */
  /* End of entries present in SW_CMM_API_VERSION_20071109 */
} ;

/** \brief The number of ICC rendering intents. */
#define SW_CMM_N_ICC_RENDERING_INTENTS           (4)

/** \brief The number of extended rendering intents, including absolute
    derivations of the ICC perceptual and saturation intents. */
#define SW_CMM_N_SW_RENDERING_INTENTS            (6)


/** \brief A structure containing information about a custom color space.
 *
 * To the rip, a custom color space is an arbitrary conversion of
 * color values. To the alternate CMM, it is a means of implementing
 * non-ICC color conversions.
 */
typedef struct sw_cmm_custom_colorspace {
  /* Entries present in SW_CMM_API_VERSION_20071109: */
  const unsigned char   *name;               /**< A nul-terminated name, used
                                                  to select between the
                                                  implementation's custom
                                                  colorspaces. */
  uint32                num_input_channels;  /**< The number of input channels
                                                  for the custom colorspace. */
  uint32                num_output_channels;  /**< The number of output
                                                  channels for the custom
                                                  colorspace. */
  /* End of entries present in SW_CMM_API_VERSION_20071109 */
} sw_cmm_custom_colorspace;

/** \brief An opaque structure defined by the alterate CMM implementation to
 * represent profiles.
 *
 * The alternate CMM implementation can use this type to supply the RIP with
 * references to opened profiles, which will be used when constructing
 * transforms later.
 */
typedef void *sw_cmm_profile;

/** \brief An opaque structure defined by the alternate CMM implementation to
 * represent transforms.
 *
 * The alternate CMM implementation can use this type to supply the RIP with
 * references to opened transforms, which will be used when invoking the
 * transforms to convert colors.
 */
typedef void *sw_cmm_transform;

/** \brief Type definition of the alternate CMM interface implementation. */
typedef struct sw_cmm_api sw_cmm_api ;

/** \brief An instance structure for the CMM API implementation
 *
 * This is the definition of an alternate CMM instance. The RIP allocates
 * memory for the instances, fills in the implementation and memory instance
 * fields, and calls the implementation's constructor to complete the
 * remaining details. The RIP will only construct one instance of each
 * alternate CMM implementation.
 *
 * The instance structure may be subclassed to hold private data by defining
 * a subclass structure containing this structure as its first member, and
 * using the size of that structure as the implementation's instance size.
 * Individual methods may then downcast their instance pointer parameters to
 * subclass pointers, and use the private data. e.g.,
 *
 * \code
 * typedef struct my_instance {
 *   sw_cmm_instance super ; // must be first entry
 *   struct my_data *dynamic ;
 *   int32 other_fields ;
 * } my_instance ;
 *
 * static void RIPCALL my_destruct(sw_cmm_instance *inst)
 * {
 *   my_instance *myinst = (my_instance *)inst ; // downcast to subclass
 *   // free allocated data, if necessary:
 *   inst->mem->implementation->free(inst->mem, myinst->dynamic) ;
 * } ;
 *
 * const static sw_cmm_api my_impl = {
 *   {
 *     SW_CMM_API_VERSION_20071109,
 *     (const uint8 *)"myname",
 *     (const uint8 *)("A long description of my module implementation"
 *                     "Copyright (C) 2007-2013 Global Graphics Software Ltd."),
 *     sizeof(my_instance), // RIP will allocate this amount for instance
 *   },
 *   // ...more of sw_cmm_api definition...
 *   my_destruct,
 *   // ...rest of sw_cmm_api definition...
 * } ;
 *
 * // Call SwRegisterCMM(&my_impl) after SwInit() to register this module.
 * \endcode
 *
 * The RIP will not touch memory beyond the size of the instance structure
 * for the implementation version registered.
 */
typedef struct sw_cmm_instance {
  /* Entries present in SW_CMM_API_VERSION_20071109: */

  /** \brief Pointer to the API implementation.

       API methods for a blob instance should always be called by indirecting
       through the instance's implementation field.

       This field is filled in by the RIP before sw_cmm_api::construct()
       is called.
  */
  const sw_cmm_api *implementation;

  /** \brief A memory allocator instance.

       This object is supplied by the RIP so that the CMM implementation can
       allocate memory using the RIP's memory allocator. CMM implementations
       should use this in preference to malloc() and free(), so that the RIP
       can track memory allocation and respond to low memory states more
       effectively.

       This field is filled in by the RIP before sw_cmm_api::construct()
       is called.
  */
  sw_memory_instance *mem;

  /* These fields should be filled in by the sw_cmm_api::construct() method: */

  /** Does the implementation support ICC input profiles? */
  HqBool  support_input_profiles;

  /** Does the implementation support ICC output profiles? */
  HqBool  support_output_profiles;

  /** Does the implementation support ICC devicelink profiles? */
  HqBool  support_devicelink_profiles;

  /** Does the implementation support ICC display profiles? */
  HqBool  support_display_profiles;

  /** Does the implementation support ICC colorspace profiles? */
  HqBool  support_colorspace_profiles;

  /** Does the implementation support ICC abstract profiles? */
  HqBool  support_abstract_profiles;

  /** Does the implementation support ICC named color profiles? */
  HqBool  support_named_color_profiles;

  /** Does the implementation support ICC version 4 profiles? */
  HqBool  support_ICC_v4;

  /** Does the implementation support black point compensation? */
  HqBool  support_black_point_compensation;

  /** Does the implementation support the extended intents between
      SW_CMM_N_ICC_RENDERING_INTENTS and SW_CMM_N_SW_RENDERING_INTENTS? */
  HqBool  support_extra_absolute_intents;

  /** The maximum number of input channels the implementation
      supports. */
  uint32  maximum_input_channels;

  /** The maximum number of output channels the implementation
      supports. */
  uint32  maximum_output_channels;

  /** If the alternate CMM implementation fails to open a profile or create a
      transform, this boolean indicates if the RIP should try to handle the
      transformation itself. */
  HqBool  allow_retry;

  /* End of entries present in SW_CMM_API_VERSION_20071109 */
} sw_cmm_instance ;

/* -------------------------------------------------------------------------- */
/** \brief Collection structure for initialisation parameters. */
typedef struct sw_cmm_init_params {
  /* Entries present in SW_CMM_API_VERSION_20071109: */

  /** \brief Global memory allocator.

      The memory allocators provided in the instances should only be used for
      allocations local to that instance. This memory allocator can be used
      for allocations which must survive individual instance lifetimes. */
  sw_memory_instance *mem ;

  /* End of entries present in SW_CMM_API_VERSION_20071109 */
} sw_cmm_init_params ;


/* -------------------------------------------------------------------------- */
/** \brief The definition of an implementation of the alternate CMM
 * interface.
 *
 * The RIP will construct a singleton instance for each implementation
 * registered.
 */
struct sw_cmm_api {
  sw_api_info info ; /**< Version number, name, display name, instance size. */

  /* Entries present in SW_CMM_API_VERSION_20071109: */

  /** \brief The \p init() method is called before any other calls to the
      implementation.

      This method may be used to initialise any implementation-specific data.
      This method is optional.

      \param implementation The registered alternate CMM implementation to be
      initialised.

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
  HqBool (RIPCALL *init)(/*@in@*/ /*@notnull@*/ sw_cmm_api *implementation,
                         /*@in@*/ /*@notnull@*/ const sw_cmm_init_params *params) ;

  /** \brief The \p finish() method is called after all calls to the
      implementation or its instances.

      The implementation instances should not access any data owned by the
      RIP after this call, nor should they call any implementation or RIP
      callback API methods after this call. This method is optional.

      \param implementation A registered alternate CMM implementation to
      finalise.
  */
  void (RIPCALL *finish)(/*@in@*/ /*@notnull@*/ sw_cmm_api *implementation) ;

  /** \brief This method is used to construct an instance of an alternate
      CMM implementation.

      \param[in,out] instance An incomplete instance of the sw_cmm_instance
      structure to complete. The RIP will allocate a structure of the size
      presented in the implementation's sw_cmm_api::info.instance_size field,
      fill in the implementation and callback API instance pointers, and then
      pass it to this routine. The \p construct() method is expected to fill
      in the remaining fields. The implementation may sub-class the instance
      to allocate private workspace by initialising the implementation's
      sw_cmm_api::info.instance_size larger than the size of the
      sw_cmm_instance structure, then downcasting the instance pointer in
      method calls.

      \return SW_CMM_SUCCESS if the instance is fully constructed, otherwise
      one of the sw_cmm_result error codes.
  */
  sw_cmm_result (RIPCALL *construct)(sw_cmm_instance *instance) ;

  /** \brief This method is used to destroy an alternate CMM instance.

      This method is optional.

      The RIP will call the destructors individually for each instance
      shortly before terminating the RIP. The RIP may also destroy and
      re-construct alternate CMM instances in low-memory situations.

      \param[in] instance An instance of the sw_cmm_api
      implementation to destroy.
  */
  void (RIPCALL *destruct)(sw_cmm_instance *instance) ;

  /** \brief Open an ICC profile.

      \param[in] instance The alternate CMM implementation instance owning the
      ICC profile.

      \param[in] ICC_profile A blob data source, giving access to the raw ICC
      profile data through the blob's sw_blob_api implementation methods.
      This blob will have been opened for non-exclusive read access. If the
      alternate CMM wishes to use the data source after the \p open_profile()
      method returns, it must create a new blob reference using the
      sw_blob_api::open_blob() method, and use that reference for all access
      after \p open_profile() terminates. If a new blob reference is created,
      the alternate CMM implementation must close that reference when it has
      finished accessing to it.

      \param[out] handle A pointer in which a CMM profile handle is stored.
      This handle will be used to refer to the profile by \p close_profile()
      and \p open_transform().

      \return SW_CMM_SUCCESS if the profile was successfully opened, in
      which case a non-NULL profile pointer should have been stored in \a
      handle. If the profile could not be opened, one of the sw_cmm_result
      error codes is returned.

      If a valid profile handle is returned, the \p close_profile() method
      will be called to destroy the profile later.
  */
  sw_cmm_result (RIPCALL *open_profile)(sw_cmm_instance *instance,
                                        sw_blob_instance *ICC_profile,
                                        sw_cmm_profile *handle);

  /** \brief Close an alternate CMM profile.

      \param[in] instance The alternate CMM instance owning the profile.

      \param[in] profile A profile handle previous opened by an \p
      open_profile() or \p open_custom_colorspace() calls on the same
      alternate CMM instance. The alternate CMM implementation should discard
      any resources associated with the profile.
  */
  void (RIPCALL *close_profile)(sw_cmm_instance *instance,
                                sw_cmm_profile  profile);

  /** \brief Declare support for a custom colorspace.

      This method is optional. If this method is not supported, then the
      \p open_custom_colorspace() method will never be called.

      \param[in] instance An alternate CMM instance queried for custom
      colorspaces.

      \param index A zero-based index used to identify the custom colorspaces
      defined by the alternate CMM instance. When trying to set custom
      colorspaces, the RIP will call this method with indices starting at 0,
      and increasing until either a match is found or this method returns
      NULL.

      \return A pointer to a custom colorspace definition, or NULL if the
      index is out of the range supported by the alternate CMM
      implementation.
  */
  sw_cmm_custom_colorspace *(RIPCALL *declare_custom_colorspace)(sw_cmm_instance *instance,
                                                                 uint32  index);

  /** \brief Open a custom colorspace profile.

      The RIP does not interpret the profile handle returned by this call.
      The profile handle will be passed back to the alternate CMM
      implementation as a means of identifying the custom colorspace when
      creating a transform, or closing the profile.

      \param[in] instance An alternate CMM instance which previously declared
      support for the custom colorspace through a call to \p
      declare_custom_colorspace().

      \param index A zero-based index used to identify the custom colorspace
      defined by the alternate CMM instance.

      \param[out] handle A pointer in which a CMM profile handle is stored.
      This handle will be used to refer to the profile by \p close_profile()
      and \p open_transform().

      \return SW_CMM_SUCCESS if the profile was successfully opened, in
      which case a non-NULL profile pointer should have been stored in \a
      handle. If the profile could not be opened, one of the sw_cmm_result
      error codes is returned.

      If a valid profile handle is returned, the \p close_profile() method
      will be called to destroy the profile later.
  */
  sw_cmm_result (RIPCALL *open_custom_colorspace)(sw_cmm_instance *instance,
                                                  uint32  index,
                                                  sw_cmm_profile *handle);

  /** \brief Create a color transformation comprising one or more alternate
      CMM profiles.

      \param[in] instance The alternate CMM instance owning the profiles.

      \param[in] profiles An array of \a num_profiles profile handles,
      returned by calls to \p open_profile() or \p open_custom_colorspace().

      \param num_profiles The number of profiles in the transform chain.

      \param[in] intents An array of length \a num_profiles-1 specifying the
      rendering intents use for color conversions between adjacent profiles
      in the \a profiles parameter.

      \param[in] black_point_compensations An array of length \a
      num_profiles-1 containing boolean flags, indicating if black point
      compensation is performed when converting colors between adjacent
      profiles in the \a profiles parameter.

      \param[out] num_input_channels A location for the alternate CMM to
      fill in the number of color channels in the input space of the first
      profile in the transform.

      \param[out] num_output_channels A location for the alternate CMM to
      fill in the number of color channels in the output space of the last
      profile in the transform.

      \param[out] handle A pointer in which a CMM transform handle is stored
      by the alternate CMM implementation. This handle will be used to refer
      to the transform by \p close_transform() and \p invoke_transform()
      methods.

      \return SW_CMM_SUCCESS if the transform was successfully opened, in
      which case a non-NULL transform pointer should have been stored in \a
      handle. If the profile could not be opened, one of the sw_cmm_result
      error codes is returned.

      If a valid transform handle is returned, the \p close_transform() method
      will be called to destroy the transform later.
  */
  sw_cmm_result (RIPCALL *open_transform)(sw_cmm_instance *instance,
                                          sw_cmm_profile profiles[],               /* N profile handles */
                                          uint32      num_profiles,                /* N */
                                          int32       intents[],                   /* (N-1) values */
                                          HqBool      black_point_compensations[], /* (N-1) values */
                                          uint32      *num_input_channels,
                                          uint32      *num_output_channels,
                                          sw_cmm_transform *handle);

  /** \brief Close a color transformation.

      \param[in] instance The alternate CMM instance owning the color
      transform.

      \param[in] transform A valid transform handle created by an \p
      open_transform() call on the same alternate CMM instance. The alternate
      CMM implementation should discard any resources associated with the
      transform.
  */
  void (RIPCALL *close_transform)(sw_cmm_instance *instance,
                                  sw_cmm_transform  transform);

  /** \brief Invoke a color transformation.

      \param[in] instance The alternate CMM instance owning the color
      transform.

      \param[in] transform A valid transform handle created by an \p
      open_transform() call on the same alternate CMM instance.

      \param[in] input_data An array of \a num_pixels sets of
      pixel-interleaved color values to transform. Colors are interleaved in
      the order specified by the input space of the first profile in the
      transform.

      \param[out] output_data An array in which to store \a num_pixels sets
      of pixel-interleaved color values. Colors are interleaved in the order
      specified by the output space of the last profile in the transform.

      \param num_pixels The number of sets of input pixel colorvalues to
      convert from the input space to the output space of the transform.

      \return SW_CMM_SUCCESS if the invocation succeeded, one of the
      sw_cmm_result error codes otherwise.
  */
  sw_cmm_result (RIPCALL *invoke_transform)(sw_cmm_instance *instance,
                                            sw_cmm_transform  transform,
                                            float             *input_data,
                                            float             *output_data,
                                            uint32            num_pixels);

  /** \brief A security challenge. */
  void (RIPCALL *security)(sw_cmm_instance *instance,
                           void *buffer, uint32 size);

  /* End of entries present in SW_CMM_API_VERSION_20071109 */
} ;


/** \brief This routine makes an alternate CMM implementation known to the
 * rip.
 *
 * It can be called any number of times with different implementations of the
 * alternate CMM API. Every implementation registered may have an instance
 * constructed, however only one alternate CMM can be configured at a time,
 * so profiles will only be opened on one instance at any time.
 *
 * \param[in] implementation The API implementation to register. This pointer
 * will be returned through the sw_cmm_api::init() and sw_cmm_api::finish()
 * calls, and also will be in the implementation member field of every
 * instance created, so the pointer can be in dynamically allocated memory.
 * Implementations may be subclassed to hold class-specific private data by
 * defining a subclass structure containing the sw_cmm_api structure as its
 * first member. Individual methods may then downcast their implementation
 * pointers to subclass pointers, and use those to get at the class data.
 * e.g.,
 *
 * \code
 * typedef struct my_implementation {
 *   sw_cmm_api super ; // must be first entry
 *   sw_memory_instance *mem ;
 *   struct my_class_data *dynamic ;
 * } my_implementation ;
 *
 * static HqBool RIPCALL my_init(sw_cmm_api *impl, const sw_cmm_init_params *params)
 * {
 *   my_implementation *myimpl = (my_implementation *)impl ; // downcast to subclass
 *   // save global memory allocator:
 *   myimpl->mem = params->mem ;
 *   myimpl->dynamic = NULL ;
 *   return TRUE ;
 * } ;
 *
 * static void RIPCALL my_close_profile(sw_cmm_instance *inst, sw_cmm_profile profile)
 * {
 *   my_implementation *myimpl = (my_implementation *)inst->implementation ; // downcast to subclass
 *   myimpl->dynamic = myimpl->mem->implementation->alloc(myimpl->mem, 1024) ;
 * } ;
 *
 * const static my_implementation module = {
 *   { // sw_cmm_api initialisation
 *   },
 *   NULL, // global memory allocator
 *   NULL  // global allocations
 * } ;
 *
 * // Call SwRegisterCMM(&module.super) after SwInit() to register this module.
 * \endcode
 */
sw_api_result RIPCALL SwRegisterCMM(sw_cmm_api *implementation);

/* Typedef with same signature. */
typedef sw_api_result (RIPCALL *SwRegisterCMM_fn_t)(sw_cmm_api *implementation);

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWCMM_H__ */
