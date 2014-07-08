/* Copyright (c) 2006-2007 Global Graphics Software Ltd. All Rights Reserved.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_cmm.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
/**
 * @file
 * @ingroup CMMexamples
 * @brief  Simple example showing how to create a color management module
 * (CMM) that implements custom color mapping.
 *
 * This example provides an implementation of a CMM that
 * can be loaded into the RIP and used to provide alternative color
 * mapping functionality.
 *
 * To tell the RIP which CMM to use when rendering the coming
 * page you should use a PostScript language command similar
 * to the following:
 * <pre>
 *   <</AlternateCMM (CMM_OILExample) >> setpagedevice
 * </pre>
 *
 */

#include "oil.h"
#include "oil_cmm.h"
#include "oil_interface_oil2pms.h"

#include <string.h>  /* memcpy */

#define kInternalName  "CMM_OILExample"  /**< Short CMM name for configuration. */
#define kDisplayName   "Custom color space example OIL CMM"  /**< UTF-8 CMM name for display. */

#define kRGBChannelCount   (3)
#define kCMYKChannelCount  (4)

/* extern variables */
extern OIL_TyJob *g_pstCurrentJob;

/**
 * @brief  Array of supported custom color spaces.
 */
static sw_cmm_custom_colorspace gCCSArray[] = {
  #define kCCS_TextColorMapFeaturesCMYKIndex  (0)
  { (uint8*) "TextColorMapFeaturesCMYK", kCMYKChannelCount, kCMYKChannelCount },
  #define kCCS_OtherColorMapFeaturesCMYKIndex  (1)
  { (uint8*) "OtherColorMapFeaturesCMYK", kCMYKChannelCount, kCMYKChannelCount },
  #define kCCS_DefaultColorMapFeaturesRGBIndex  (2)
  { (uint8*) "DefaultColorMapFeaturesRGB", kRGBChannelCount, kCMYKChannelCount },
};

typedef void (*CMYKColorMappingFunc)(float cmykValue[]); /**< Typedef for the color modifying callback function. */

/**
 * @brief  Description of a profile specific to this CMM.
 */
typedef struct CMM_PROFILE
{
  CMYKColorMappingFunc pMappingFunc; /**< Function pointer to CMYK modifying code. */
  uint32 nInputChannels;            /**< The number of input color channels. */
  uint32 nOutputChannels;           /**< The number of output color channels. */
} CMM_PROFILE;

/**
 * @brief  Description of a transform specific to this CMM.
 */
typedef struct CMM_TRANSFORM
{
  CMM_PROFILE* pProfileArray;  /**< Array of profile objects required in the transform. */
  uint32 nProfiles;            /**< The number of profile entries in the array. */
} CMM_TRANSFORM;

/* Forward declarations */
static sw_cmm_result RIPCALL ccs_construct(sw_cmm_instance *instance);
static sw_cmm_result RIPCALL ccs_open_profile(sw_cmm_instance *instance,
                                              sw_blob_instance *profile_source,
                                              sw_cmm_profile *handle);
static void RIPCALL ccs_close_profile(sw_cmm_instance *instance,
                                      sw_cmm_profile profile);
static sw_cmm_custom_colorspace* RIPCALL ccs_declare_custom_colorspace(sw_cmm_instance *instance,
                                                                       uint32 index);
static sw_cmm_result RIPCALL ccs_open_custom_colorspace(sw_cmm_instance *instance,
                                                        uint32 index,
                                                        sw_cmm_profile *handle);
static sw_cmm_result RIPCALL ccs_open_transform(sw_cmm_instance *instance,
                                                sw_cmm_profile profiles[],
                                                uint32 num_profiles,
                                                int32 intents[],
                                                HqBool black_point_compensations[],
                                                uint32* num_input_channels,
                                                uint32* num_output_channels,
                                                sw_cmm_transform *handle);
static void RIPCALL ccs_close_transform(sw_cmm_instance *instance,
                                        sw_cmm_transform transform);
static sw_cmm_result RIPCALL ccs_invoke_transform(sw_cmm_instance *instance,
                                                  sw_cmm_transform transform,
                                                  float* input_data,
                                                  float* output_data,
                                                  uint32 num_pixels);

/**
 * @brief Function to implement black substitution.
 *
 * This function detects grey tones made up of a mixture of cyan,
 * magenta and yellow inks and replaces them with black only.
 * If the cyan, magenta and yellow values are equal, they are set to
 * zero and an equivalent amount of black is added to the existing
 * black value, capped at full black.
 *
 * If C=M=Y  -> C=M=Y=0, K=K+C (capped at 1.0f)
 *
 * @param[in,out]  cmykValue  An array containing a single CMYK value,
 * which is modified by the function, if required.
 */
static void BlackSubstitute(float cmykValue[])
{
  HQASSERT((cmykValue != NULL), "CMM: Black substitute invalid CMYK");

  /* if C M and Y components are equal, replace with K */
  if ((cmykValue[0] == cmykValue[1])
    && (cmykValue[0] == cmykValue[2]))
  {
    cmykValue[3] = cmykValue[3] + cmykValue[0];
    if (cmykValue[3] > 1.0f)
    {
      cmykValue[3] = 1.0f;
    }
    cmykValue[0] = cmykValue[1] = cmykValue[2] = 0;
  }
}

/**
 * @brief Function to implement pure black text feature.
 *
 * This function ensures that all black text is printed with 100% black ink,
 * and no colored ink.
 *
 * Convert C=M=Y=100% to K=100% C=M=Y=0
 *
 * @param[in,out]  cmykValue  An array containing a single CMYK value,
 * which is modified by the function, if required.
 */
static void PureBlackText(float cmykValue[])
{
  HQASSERT((cmykValue != NULL), "CMM: Pure Black Text invalid CMYK");

  /* if C M and Y components are all 1.0, replace with K */
  if ((cmykValue[0] == 1.0f)
    &&(cmykValue[1] == 1.0f)
    &&(cmykValue[2] == 1.0f))
  {
    cmykValue[3] = 1.0f;
    cmykValue[0] = cmykValue[1] = cmykValue[2] = 0;
  }
}

/**
 * @brief Function to implement all text black feature.
 *
 * This function forces all text to be rendered as soid black, using only
 * black ink, regardless of the requested color.
 *
 * Conversion forces K=100% and C=M=Y=0%
 *
 * @param[in,out]  cmykValue  An array containing a single CMYK input value,
 * which is modified by the function, if required.
 */
static void AllBlackText(float cmykValue[])
{
  HQASSERT((cmykValue != NULL), "CMM: All Black Text invalid CMYK");

  cmykValue[3] = 1.0f;
  cmykValue[0] = cmykValue[1] = cmykValue[2] = 0;
}

/**
 * @brief  Function to manage color mapping features for CMYK text.
 *
 * This function is called for all CMYK text. It calls various color
 * mapping functions, depending on the enabled features.
 *
 * The PMS function @c PMS_CMYKtoCMYK is also called to allow the
 * PMS code to apply any CMYK to CMYK color transform.
 *
 * @param[in,out]  cmykValue  An array containing a single CMYK input value,
 * which is modified by the function, if required.
 */
static void TextColorMapFeaturesCMYK(float cmykValue[])
{
  HQASSERT((cmykValue != NULL), "CMM: TextColorMapFeaturesCMYK invalid CMYK");

  if (g_pstCurrentJob->bBlackSubstitute)
    BlackSubstitute(cmykValue);
  if (g_pstCurrentJob->bPureBlackText)
    PureBlackText(cmykValue);
  if (g_pstCurrentJob->bAllTextBlack)
    AllBlackText(cmykValue);

  /* The PMS now has a chance to change the values */
  (void)PMS_CMYKtoCMYK(cmykValue);
}

/**
 * @brief  Function to implement color mapping for 'Other' CMYK objects.
 *
 * This function is called for all CMYK object in the 'Other' category.
 * 'Other' objects are all objects excluding images, vignettes, and text.
 *
 * In this example, this function only calls the black substitute color
 * mapping function, if the black substitute feature is enabled.
 *
 * The PMS function @c PMS_CMYKtoCMYK is also called to allow the
 * PMS code to apply a CMYK to CMYK color transform.
 *
 * @param[in,out]  cmykValue  An array containing a single CMYK input value,
 * which is modified by the function, if required.
 */
static void OtherColorMapFeaturesCMYK(float cmykValue[])
{
  HQASSERT((cmykValue != NULL), "CMM: OtherColorMapFeaturesCMYK invalid CMYK");

  if (g_pstCurrentJob->bBlackSubstitute)
    BlackSubstitute(cmykValue);

  /* The PMS now has a chance to change the values */
  (void)PMS_CMYKtoCMYK(cmykValue);
}

/**
 * @brief  Function to implement RGB to CMYK transformation.
 *
 * In this example, the function merely calls the PMS function
 * @c PMS_RGBtoCMYK to allow PMS to apply a RGB to CMYK color
 * transform.
 *
 * @param[in, out] rgbInCMYKOutValue  An array of floating point values which is
 * assumed to contain an RGB color when it is passed in, and is populated with
 * the corresponding CMYK value by the call.
 */
static void DefaultColorMapFeaturesRGB(float rgbInCMYKOutValue[])
{
  float rgbValue[3];
  HQASSERT((rgbInCMYKOutValue != NULL), "CMM: DefaultColorMapFeaturesRGB invalid color array");

  rgbValue[0] = rgbInCMYKOutValue[0];
  rgbValue[1] = rgbInCMYKOutValue[1];
  rgbValue[2] = rgbInCMYKOutValue[2];

  /* The PMS now has a chance to change the values */
  (void)PMS_RGBtoCMYK(rgbValue, rgbInCMYKOutValue);
}

/**
 * @brief  Singleton instance describing this alternate CMM.
 */
static sw_cmm_api gCCSExampleImplementation =
{
  {
    SW_CMM_API_VERSION_20071109,    /* version */
    (uint8*) kInternalName,         /* name */
    (uint8*) kDisplayName,          /* name_display */
    sizeof(sw_cmm_instance)         /* instance size */
  },
  NULL,                           /* init callback */
  NULL,                           /* finish callback */
  ccs_construct,                  /* construct callback. */
  NULL,                           /* destruct callback */
  ccs_open_profile,               /* open_profile callback. */
  ccs_close_profile,              /* close_profile callback. */
  ccs_declare_custom_colorspace,  /* declare_custom_colorspace callback. */
  ccs_open_custom_colorspace,     /* open_custom_colorspace callback. */
  ccs_open_transform,             /* open_transform callback. */
  ccs_close_transform,            /* close_transform callback. */
  ccs_invoke_transform,           /* invoke_transform callback. */
  NULL,                           /* security callback. */
};

/**
 * @brief Retrieve a pointer to the sample
 * CMM implemented by this code.
 */
sw_cmm_api* oilccs_getInstance(void)
{
  return &gCCSExampleImplementation;
}

/**
 * @brief Allocate memory for use by the CMM.
 */
static void* cmmMemAlloc(sw_memory_instance *instance, size_t n)
{
  HQASSERT(instance, "No memory API instance") ;
  HQASSERT(instance->implementation != NULL, "No memory API implementatio");
  HQASSERT(instance->implementation->info.version >= SW_MEMORY_API_VERSION_20071110,
           "Memory API version insufficient");
  HQASSERT(instance->implementation->alloc != NULL, "No memory API alloc");
  return instance->implementation->alloc(instance, n);
}

/**
 * @brief  Release memory allocated by the CMM.
 */
static void cmmMemFree(sw_memory_instance *instance, void* p)
{
  HQASSERT(instance, "No memory API instance") ;
  HQASSERT(instance->implementation != NULL, "No memory API implementatio");
  HQASSERT(instance->implementation->info.version >= SW_MEMORY_API_VERSION_20071110,
           "Memory API version insufficient");
  HQASSERT(instance->implementation->free != NULL, "No memory API free");
  instance->implementation->free(instance, p);
}

/**
 * @brief Construct an instance of the @c sw_cmm_api interface.
 *
 * The RIP constructs an instance of each CMM module after booting the
 * interpreter. The RIP fills in the implementation pointer and pointers to
 * the memory API and blob API instances. The module is expected to fill in
 * the remaining fields, which contain flags defining the capabilities of
 * the module. Some of the information returned will be used by the RIP to
 * determine whether a particular profile or transform can be handled by
 * a given module, and in other cases the RIP will use this information to
 * divert color conversions through its built-in CMM if appropriate.
 *
 * @param[in,out]  instance
 * @return A @c sw_cmm_result result code.
 */
static sw_cmm_result RIPCALL ccs_construct(sw_cmm_instance *instance)
{
  HQASSERT(instance != NULL, "No CMM instance");
  HQASSERT(instance->implementation == &gCCSExampleImplementation,
           "Invalid CMM instance");
  HQASSERT(instance->mem != NULL, "No memory API");

  /* The alloc/free functions need at least this version. */
  if ( instance->mem->implementation->info.version < SW_MEMORY_API_VERSION_20071110 )
    return SW_CMM_ERROR_VERSION ;

  instance->support_input_profiles = FALSE;
  instance->support_output_profiles = FALSE;
  instance->support_devicelink_profiles = FALSE;
  instance->support_display_profiles = FALSE;
  instance->support_colorspace_profiles = FALSE;
  instance->support_abstract_profiles = FALSE;
  instance->support_named_color_profiles = FALSE;
  instance->support_ICC_v4 = FALSE;
  instance->support_black_point_compensation = FALSE;
  instance->support_extra_absolute_intents = FALSE;
  instance->maximum_input_channels = kCMYKChannelCount;
  instance->maximum_output_channels = kCMYKChannelCount;
  instance->allow_retry = FALSE;

  return SW_CMM_SUCCESS ;
}

/**
 * @brief  Create an internal profile structure for ICC profile data accessed
 * using a seekable file stream.
 *
 * @note Not supported in this implementation.
 *
 * @param[in]      instance         The alternate CMM instance.
 * @param[in]      profile_source   A blob reference to access the profile data.
 * @param[in,out]  handle           A pointer to the profile created by the function.
 * @return A @c sw_cmm_result result code.
 *
 * @see ccs_close_profile()
 */
static sw_cmm_result RIPCALL ccs_open_profile(sw_cmm_instance *instance,
                                              sw_blob_instance *profile_source,
                                              sw_cmm_profile *handle)
{
  HQASSERT(instance, "No CMM instance") ;
  HQASSERT(profile_source != NULL, "No CMM profile data");

  UNUSED_PARAM(sw_cmm_instance *, instance);
  UNUSED_PARAM(sw_blob_instance*, profile_source);
  UNUSED_PARAM(sw_cmm_profile*, handle);

  /* Should never be called in this implementation */
  HQFAIL("Should never call this function");

  return SW_CMM_ERROR;
}

/**
 * @brief  Close a profile with a handle previously created
 * with ccs_open_profile().
 *
 * @param[in]  instance         The alternate CMM instance.
 * @param[in]  profile          Pointer to the CMM profile to be closed.
 *
 * @see ccs_open_profile()
 */
static void RIPCALL ccs_close_profile(sw_cmm_instance *instance,
                                      sw_cmm_profile profile)
{
  HQASSERT(instance, "No CMM instance") ;
  HQASSERT(profile != NULL, "No CMM profile");
  cmmMemFree(instance->mem, profile);
}

/**
 * @brief  Declare a custom color space to the RIP.
 *
 * @param[in]  instance         The alternate CMM instance.
 * @param[in]  index            Zero-based index into an array of color spaces
 * handled by this CMM.
 * @return                      A pointer to a sw_cmm_custom_colorspace object,
 *                              or <code>NULL</code> if index is out of bounds.
 */
static sw_cmm_custom_colorspace* RIPCALL ccs_declare_custom_colorspace(sw_cmm_instance *instance,
                                                               uint32 index)
{
  const uint32 kCustomColorSpaceCount = sizeof (gCCSArray) / sizeof (gCCSArray[0]);

  UNUSED_PARAM(sw_cmm_instance *, instance);
  HQASSERT(instance, "No CMM instance") ;

  if (index < kCustomColorSpaceCount)
    return &gCCSArray[index];

  /* index is out of bounds */
  return NULL;
}


/**
 * @brief  Create an internal profile structure for a custom colorspace.
 *
 * This function initializes a function pointer which is later used to perform
 * the color transform.
 *
 * @param[in]      instance         The alternate CMM instance.
 * @param[in]      index            Zero-based index into an array of color spaces
 *                                  handled by this CMM.
 * @param[in,out]  handle           A pointer to the profile created by the function.
 * @return A @c sw_cmm_result result code.
 *
 * @see ccs_declare_custom_colorspace()
 */
static sw_cmm_result RIPCALL ccs_open_custom_colorspace(sw_cmm_instance *instance,
                                                        uint32 index,
                                                        sw_cmm_profile *handle)
{
  const uint32 kCustomColorSpaceCount = sizeof(gCCSArray) / sizeof(gCCSArray[0]);
  CMM_PROFILE* pProfile;

  HQASSERT(instance, "No CMM instance") ;
  HQASSERT(handle, "No CMM profile") ;

  if (index >= kCustomColorSpaceCount)
    return SW_CMM_ERROR_INVALID; /* index out of bounds */

  pProfile = cmmMemAlloc(instance->mem, sizeof(CMM_PROFILE));
  if (! pProfile)
    return SW_CMM_ERROR_MEMORY;

  switch (index)
  {
  case kCCS_TextColorMapFeaturesCMYKIndex:
    pProfile->pMappingFunc = TextColorMapFeaturesCMYK;
    pProfile->nInputChannels = kCMYKChannelCount;
    pProfile->nOutputChannels = kCMYKChannelCount;
    break;

  case kCCS_OtherColorMapFeaturesCMYKIndex:
    pProfile->pMappingFunc = OtherColorMapFeaturesCMYK;
    pProfile->nInputChannels = kCMYKChannelCount;
    pProfile->nOutputChannels = kCMYKChannelCount;
    break;

  case kCCS_DefaultColorMapFeaturesRGBIndex:
    pProfile->pMappingFunc = DefaultColorMapFeaturesRGB;
    pProfile->nInputChannels = kRGBChannelCount;
    pProfile->nOutputChannels = kCMYKChannelCount;
    break;

  default:
    /* index out of bounds - Should never be reached */
    HQFAIL("Colorspace index out of bounds");
    cmmMemFree(instance->mem, pProfile);
    return SW_CMM_ERROR_INVALID;
  }

  *handle = pProfile;
  return SW_CMM_SUCCESS;
}

/**
 * @brief  Create a transform structure.
 *
 * @param[in]  instance         The alternate CMM instance.
 * @param[in]  profiles  An array of sw_cmm_profile handles, each of which refers to
 * profiles in the same sequence as required by the transform.
 * @param[in]  num_profiles  Number of entries in profiles array.
 * @param[in]  intents Array indicating the rendering intent to use when converting
 * colors between neighboring profiles. Contains (num_profiles - 1) entries.
 * @param[in]  black_point_compensations  Array indicating whether to apply black
 * point compensation when converting colors between neighboring profiles.
 * Contains (num_profiles - 1) entries.
 * @param[out]  num_input_channels  Should be set as the number of color
 * channels in the input space of profiles[0].
 * @param[out]  num_output_channels  Should be set as the number of color
 * channels in the output space of profiles[num_profiles - 1].
 * @param[out]  handle  A pointer to the created@c  sw_cmm_transform strucure.
 * @return A @c sw_cmm_result result code.
 *
 * @see ccs_close_transform(), ccs_invoke_transform()
 */
static sw_cmm_result RIPCALL ccs_open_transform(sw_cmm_instance *instance,
                                                sw_cmm_profile profiles[],
                                                uint32 num_profiles,
                                                int32 intents[],
                                                HqBool black_point_compensations[],
                                                uint32* num_input_channels,
                                                uint32* num_output_channels,
                                                sw_cmm_transform *handle)
{
  CMM_TRANSFORM* pTransform;
  CMM_PROFILE* pProfiles;
  uint32 i;

  HQASSERT(instance, "No CMM instance") ;
  HQASSERT(profiles != NULL, "No CMM profiles");
  HQASSERT(num_input_channels != NULL, "No input data");
  HQASSERT(num_output_channels != NULL, "No output data");
  HQASSERT(num_profiles > 0, "Not enough profiles");
  HQASSERT(handle, "No CMM transform") ;

  UNUSED_PARAM(int32*,intents);
  UNUSED_PARAM(HqBool*, black_point_compensations);

  /* Allocate memory for a transform object */
  pTransform = cmmMemAlloc(instance->mem, sizeof(CMM_TRANSFORM));
  if (! pTransform)
    return SW_CMM_ERROR_MEMORY;

  pTransform->nProfiles = num_profiles;
  pTransform->pProfileArray = cmmMemAlloc(instance->mem, sizeof(CMM_PROFILE) * num_profiles);
  if (! pTransform->pProfileArray)
  {
    cmmMemFree(instance->mem, pTransform);
    return SW_CMM_ERROR_MEMORY;
  }

  /* Create transform */
  pProfiles = (CMM_PROFILE*) profiles;
  for (i = 0; i < num_profiles; i ++)
    pTransform->pProfileArray[i] = pProfiles[i];

  /* Set channel counts */
  *num_input_channels = pProfiles[0].nInputChannels;
  *num_output_channels = pProfiles[num_profiles - 1].nOutputChannels;

  *handle = pTransform;
  return SW_CMM_SUCCESS;
}

/**
 * @brief  Close a transform previously opened with ccs_open_transform().
 *
 * @param[in]  instance         The alternate CMM instance.
 * @param[in]  transform        Pointer to a CMM transform structure.
 *
 * @see ccs_open_transform(), ccs_invoke_transform()
 */
static void RIPCALL ccs_close_transform(sw_cmm_instance *instance,
                                        sw_cmm_transform transform)
{
  CMM_TRANSFORM* pTransform = (CMM_TRANSFORM*) transform;

  HQASSERT(instance, "No CMM instance") ;
  HQASSERT(pTransform != NULL, "No CMM transform");
  cmmMemFree(instance->mem, pTransform->pProfileArray);
  cmmMemFree(instance->mem, pTransform);
}

/**
 * @brief  Use the specified transform to adjust color data.
 *
 * The RIP will place color data in memory beginning at input_data. This
 * function should converts the data using the transform and places the
 * output values in memory beginning at output_data.
 *
 * @param[in]  instance      The alternate CMM instance.
 * @param[in]  transform     The transform to use for color conversions.
 * @param[in]  input_data    RIP-allocated array of input color values.
 * @param[out]  output_data  RIP-allocated array to fill with output color values.
 * @param[in]  num_pixels    Number of color values to transform.
 * @return <code>TRUE</code> on success, <code>FALSE</code> otherwise.
 *
 * @see ccs_open_transform(), ccs_close_transform()
 */
static sw_cmm_result RIPCALL ccs_invoke_transform(sw_cmm_instance *instance,
                                                  sw_cmm_transform transform,
                                                  float* input_data,
                                                  float* output_data,
                                                  uint32 num_pixels)
{
  uint32 i;
  CMM_TRANSFORM* pTransform = (CMM_TRANSFORM*) transform;
  uint32 nPixelInIndex;
  uint32 nPixelOutIndex;

  HQASSERT(instance, "No CMM instance") ;
  HQASSERT(pTransform != NULL, "No CMM transform");
  HQASSERT(input_data != NULL, "No input data");
  HQASSERT(output_data != NULL, "No output data");

  UNUSED_PARAM(sw_cmm_instance *, instance);

  /* Call array of CMYKColorMappingFunc values for each pixel */
  nPixelInIndex = 0;
  nPixelOutIndex = 0;
  for (i = 0; i < num_pixels; i ++)
  {
    uint32 nMappingFunc;
    float cmykOrRGBValue[kCMYKChannelCount];
    uint32 nInputChannels;
    uint32 nOutputChannels;

    /* Store input value */
    nInputChannels = pTransform->pProfileArray[0].nInputChannels;
    nOutputChannels = pTransform->pProfileArray[pTransform->nProfiles - 1].nOutputChannels;
    memcpy(cmykOrRGBValue, &input_data[nPixelInIndex], sizeof(float) * nInputChannels);

    /* Apply transform */
    for (nMappingFunc = 0; nMappingFunc < pTransform->nProfiles; nMappingFunc ++)
    {
      if (pTransform->pProfileArray[nMappingFunc].pMappingFunc)
        pTransform->pProfileArray[nMappingFunc].pMappingFunc(cmykOrRGBValue);
    }

    /* Store result */
    memcpy(&output_data[nPixelOutIndex], cmykOrRGBValue, sizeof(float) * nOutputChannels);

    nPixelInIndex += nInputChannels;
    nPixelOutIndex += nOutputChannels;
  }

  return SW_CMM_SUCCESS;
}

