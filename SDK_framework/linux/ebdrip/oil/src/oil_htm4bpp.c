/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_htm4bpp.c(EBDSDK_P.1) $
 */
/*! \file
 *  \ingroup OIL
 *  \brief HTM example screening module for 4bpp
 *
 * A primitive screening module example.
 *
 * The aim of this sample code is to implement a multi level threshold screen.
 *
 * The HTM API provides memory allocation and destruction functions via the
 * sw_memory_api structure. These functions are expected to be used by 
 * screening modules whenever possible rather than OS supplied functions. 
 * Failure to comply with this may result in difficulties in configuring 
 * the RIP memory for optimal performance, especially in memory 
 * constrained environments.
 *
 * @note This code should be used for reference purposes only.
 */

#include "oil.h"
#include "pms_export.h"
#include "oil_htm.h"
#include "oil_interface_oil2pms.h"
#include "hqnstrutils.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#ifndef MACRO_START
#ifndef lint
#define MACRO_START do {
#define MACRO_END } while(0)
#else
static int lint_flag = 0 ;
#define MACRO_START do {
#define MACRO_END } while ( lint_flag )
#endif
#endif

#ifdef GG_OBJ_TYPES
static uint32 uaMaps[256];
#endif
/**
 * @brief The @a sw_htm_api instance used to register the module with the RIP.
 *
 * It is initialized by @c htm_getInstance().
 */
static sw_htm_api htmApi = { 0 };

/** @brief Lookup table which maps halftone tables to objects.
 *
 * Function htm4bpp_remapHTtables() can reset these mappings as required for 
 * the specified screen mode.
 * @see htm4bpp_remapHTtables().
 */
static OIL_TyScreens st4bppScreenLookup[]=
{
  { GG_OBJ_GFX,
    { PMS_SCREEN_4BPP_GFX_CYAN,
      PMS_SCREEN_4BPP_GFX_MAGENTA,
      PMS_SCREEN_4BPP_GFX_YELLOW,
      PMS_SCREEN_4BPP_GFX_BLACK } },
  { GG_OBJ_IMAGE,
    { PMS_SCREEN_4BPP_IMAGE_CYAN,
      PMS_SCREEN_4BPP_IMAGE_MAGENTA,
      PMS_SCREEN_4BPP_IMAGE_YELLOW,
      PMS_SCREEN_4BPP_IMAGE_BLACK } },
  { GG_OBJ_TEXT,
    { PMS_SCREEN_4BPP_TEXT_CYAN,
      PMS_SCREEN_4BPP_TEXT_MAGENTA,
      PMS_SCREEN_4BPP_TEXT_YELLOW,
      PMS_SCREEN_4BPP_TEXT_BLACK } },
};

#define OIL_EBD_SCRN_OBJECTS_4BPP (sizeof(st4bppScreenLookup) / sizeof(st4bppScreenLookup[0]))

extern HqBool RIPCALL do4bppHalftone(sw_htm_instance *instance,
                                     const sw_htm_dohalftone_request *request);

/** @brief The individual static halftone instances.
 *
 * We only reference these from the static table @c g_HTIs.
 *
 * @note Of course, our use of the letters C, M, Y and K in these names
 * are purely for our convenience - the PostScript sethalftone really
 * determines which screen will be used for which colorant.
 */
static HTI g_HTI_0;
static HTI g_HTI_1;
static HTI g_HTI_2;
static HTI g_HTI_3;

/** @brief A table of halftone instances.
 * 
 * So simple, in our case, that we can keep them static.
 */
HTI* g_4bppHTI[4] = { &g_HTI_0, &g_HTI_1, &g_HTI_2, &g_HTI_3 };


static void htm4bpp_setupHTtables();

/** @brief Select and configure an instance of a specified halftone screen.
 *
 * @c HalftoneSelect() is called when the page description language (PDL) does
 * the equivalent of a PostScript \c sethalftone operation.
 *
 * This function accepts a partially-constructed halftone instance from the RIP,
 * along with an information structure, determines which screen is required, and 
 * completes the configuration of the instance.  If the required screen 
 * is the same as the currently selected screen, the currently selected instance
 * is returned instead of completing the new one.
 *
 * The RIP calls this function for each halftone needed, so in the case
 * of multi-colorants, such as a PostScript type 5 halftone dictionary,
 * the RIP will call this function for each colorant sub-dictionary therein.
 *
 * Therefore, any one call is only concerned with a single halftone instance.
 *
 * The key names this module understands from the halftone dictionary are:
 *
 * @arg @c Screen - @e Integer - Mandatory.\n
 *   Which halftone cell to use.\n
 *   Must be in the range 0 to 3.
 *
 * @arg @c Inverse - @e Boolean - Optional.\n
 *   Whether to light pixels in inverse order.\n
 *   Default is @a false.
 *
 * This example is so simple that the instance structures can be declared
 * statically. Therefore, all this function need to is validate the
 * parameters, initialize the inverse screen cells if required, determine
 * the appropriate halftone instance, increment its reference count, and
 * fill in the details for the RIP.
 *
 * In the case of the bit depth of the source raster, both 8 and 16 bits
 * per pixel can be handled.  if the RIP hasn't already selected a bit depth,
 * 8 bits per pixel is used by default.  This is a logical choice, since 
 * the extra bit depth is of no benefit to the very primitive screens
 * used in this example, but the performance implications would be significant.
 *
 * \todo The existing documentation for this talks about initializing inverse 
 * screen cells and incrementing reference counts, neither of which I see 
 * evidence of in the code.  Is either incorrect, or is it my misunderstanding?
 * \param[out]  instance    A partially-completed halftone module instance, provided by 
                            the RIP.  
 * \param[in]   info        Pointer to a structure containing information on the RIP's 
                            current settings and requirements
 * \param[out]  matches     Pointer-to-a-pointer to a halftone instance.  If the required
                            screen is the same as the currently selected screen, the existing
                            instance will be returned in this parameter.
 * \retval      SW_HTM_ERROR_VERSION    This value is returned if the API version does not support
                                        the required functionality.
 * \retval      SW_HTM_ERROR_CONFIGURATIONERROR    This value is returned if a configuration error 
                                        is detected in the information structure.
 * \retval      SW_HTM_ERROR_TYPECHECK  This value is returned if the information structure does not
                                        contain data of the expected type.
 * \retval      SW_HTM_ERROR_RANGECHECK This value is returned if the required screen is not
                                        recognised by the module.
 * \retval      SW_HTM_SUCCESS          This value is returned if the operation completes successfully.
 */

static sw_htm_result RIPCALL doHalftoneSelect(sw_htm_instance *instance,
                                              const sw_htm_select_info *info,
                                              sw_htm_instance **matches)
{
  int         iscreen ;
  HTI        *ourInst ;
  const sw_data_api *dapi ;

  /* Match structure to simplify typechecking of halftone parameters. */
  static sw_data_match htmatch[] = {
    {SW_DATUM_BIT_INTEGER, SW_DATUM_STRING("Screen")},
  } ;

  HQASSERT(NULL != instance , "No Halftone instance") ;
  HQASSERT(NULL != info, "No halftone info") ;
  HQASSERT(NULL != matches, "No halftone matches") ;

  /* Check that the data API has the minimum required version for the
     operation we want to do. */
  dapi = info->data_api ;
  HQASSERT(dapi, "No data API") ;

  if ( dapi->info.version < SW_DATA_API_VERSION_20071111 )
    return SW_HTM_ERROR_VERSION ;

  /* Look for the required key and check it.
   */
  switch ( dapi->match(&info->halftonedict, htmatch, sizeof(htmatch)/sizeof(htmatch[0])) ) {
  default:
    return SW_HTM_ERROR_CONFIGURATIONERROR ;
  case SW_DATA_ERROR_TYPECHECK:
    return SW_HTM_ERROR_TYPECHECK ;
  case SW_DATA_OK:
    break ;
  }

  iscreen = htmatch[0].value.value.integer ;
  if ( iscreen < 0 || iscreen > 3 )
    return SW_HTM_ERROR_RANGECHECK ;

  ourInst = g_4bppHTI[ iscreen ] ;

  /* If we have already got a selected instance in the table slot, return
     that cached instance and don't bother completing the rest of the current
     instance (it will be discarded by the RIP). If there is a selected
     screen, then all of the information should be the same as last time, so
     we shouldn't need to check the bit depth. */
  if ( ourInst->selected ) {
    *matches = ourInst->selected ;
    return SW_HTM_SUCCESS ;
  }

  /* Fill in the details for the RIP. */
  if (info->src_bit_depth)
  {
    if (  (info->src_bit_depth != 8)
       && (info->src_bit_depth != 16) )
      return SW_HTM_ERROR_UNSUPPORTED_SRC_BIT_DEPTH ;
    instance->src_bit_depth = info->src_bit_depth ;
  }
  else
  {
    instance->src_bit_depth = 8 ;
  }

 /*  details->want_object_map = FALSE ; */
  instance->want_object_map = TRUE ;
  instance->interrelated_channels = FALSE ;
  instance->num_src_channels = instance->num_dst_channels = 1 ;
  instance->process_empty_bands = FALSE ;

  /* Finally, store this instance in the screen table. */
  ourInst->selected = instance ;

  return SW_HTM_SUCCESS ;
}


/** @brief Implementation of HalftoneRelease().
 *
 * @c HalftoneRelease will be called once for each previously successful
 * @c HalftoneSelect when the RIP no longer needs the halftone instance.
 *
 * Because this example uses statically declared halftone instances,
 * we don't need to free any memory or do anything very complex at all.
 *
 * All we do, therefore, is check that the halftone instance value
 * matches one of our instances, and that its reference count hasn't
 * gone awry.
 *
 * \param[in]  instance    A pointer to the halftone instance to be released.
 */

static void RIPCALL doHalftoneRelease(sw_htm_instance *instance)
{
  int i ;

  HQASSERT(NULL != instance , "No halftone instance") ;

  /* Locate the instance in our list. If we had many entries and did not want
     to search all of them for the selected entry, we could subclass the
     instance, and put the screen index or a table pointer into the subclass.
     We would then downcast the instance pointer to the subclass and extract
     the screen index or table pointer in two lines of code. */
  for ( i = 0 ; i < 4 ; i++ )
  {
    HTI *ptr = g_4bppHTI[ i ];

    if ( ptr->selected == instance )
    {
      ptr->selected = NULL ;
      break ;
    }
  }

  HQASSERT(i < 4, "Failed to find halftone instance") ;
}

/** @brief Implementation of RenderInitiation().
 *
 * The RIP calls @c RenderInitiation when it prepares to render a page.
 *
 * This primitive example won't benefit from more memory, so we don't
 * really have much to do in this function. We just check that the source
 * (contone) and destination (halftone) raster bit depths are appropriate,
 * and let the RIP know our rendering needs.
 *
 * \todo This declares implementation as an unused parameter, but it is, at least,
 * involved in an assert().
 * \param[in]  implementation   Unused parameter.
 * \param[in]  render_info      Information from the RIP on the rendering requirements.
 * \retval     SW_HTM_ERROR_UNSUPPORTED_SRC_BIT_DEPTH  Returned if the contone bit depth is neither 8
                                nor 16 bpp.
 * \retval     SW_HTM_ERROR_UNSUPPORTED_DST_BIT_DEPTH  Returned if the halftone bit depth is not 2 bpp.
 * \retval     SW_HTM_SUCCESS          This value is returned if the operation completes successfully.
 */

static sw_htm_result RIPCALL doRenderInit(sw_htm_api *implementation,
                                          const sw_htm_render_info *render_info)
{
  UNUSED_PARAM(sw_htm_api *, implementation) ;

  HQASSERT(implementation == &htmApi, "Invalid halftone implementation") ;
  HQASSERT(NULL != render_info, "No halftone render info") ;

  if (  ( render_info->src_bit_depth != 8)
      &&( render_info->src_bit_depth != 16) )
    return SW_HTM_ERROR_UNSUPPORTED_SRC_BIT_DEPTH ;


  if ( render_info->dst_bit_depth != 4 )
    return SW_HTM_ERROR_UNSUPPORTED_DST_BIT_DEPTH ;

  /* OK, let the RIP know what we need. */

  return SW_HTM_SUCCESS ;
}

#ifdef DEBUG_WRITE_OBJ_MAP
static FILE *fileObjMap[4] = {NULL,NULL,NULL,NULL};
static int32 fileheight = 0;
static int32 colorant = 0;
static char szFile[4][260] = {"C:\\objmapC.raw", "C:\\objmapM.raw", "C:\\objmapY.raw", "C:\\objmapK.raw"};
#endif

/** \brief   Retrieve an instance of the screening module implemented by this file.
 *
 * This function populates the \c htmApi structure with appropriate data and 
 * function pointers and configures the halftone tables by querying the PMS for
 * the required information.
 *
 * \note Screening modules will need to provide new functions in the future,
 * such as for using interrelated channels. Furthermore, later RIPS
 * will also provide us with more than just sw_memory_api.
 *
 * \return  This function returns a reference to the completed \c htmApi structure.
 */
sw_htm_api *htm4bpp_getInstance()
{
#ifdef GG_OBJ_TYPES
  int32 x;
#endif
  htmApi.info.version = SW_HTM_API_VERSION_20071110 ;
  htmApi.info.name = (uint8*)"htm4bpp" ;
  htmApi.info.display_name = (uint8*)"htm4bpp example screening module" ;
  htmApi.info.instance_size = sizeof(sw_htm_instance);

  htmApi.HalftoneSelect = doHalftoneSelect ;
  htmApi.HalftoneRelease = doHalftoneRelease ;
  htmApi.RenderInitiation = doRenderInit ;
  htmApi.DoHalftone = do4bppHalftone ;
  htmApi.band_ordering = SW_HTM_BAND_ORDER_ANY ;
  htmApi.reentrant = TRUE ;

  /* The following initialisation could be performed in the init() routine,
     if desired. The tables could be stored in private class variables, by
     subclassing the implementation structure. */
  htm4bpp_setupHTtables();

#ifdef GG_OBJ_TYPES
  for(x = 0; x<256; x++)
      uaMaps[x]=0;
#endif

  return &htmApi ;

}

/**
 * \brief Configure the screen tables by retrieving information from the PMS.
 *
 * This function get the screen table information from the PMS for each oject type and color,
 * and sets them up in the OIL structures.
 * 
 * st4bppScreenLookup array must be initialized before this function can be called.
 */
static void htm4bpp_setupHTtables()
{
  PMS_TyScreenInfo *pScreenInfo;
  int obj, color;

  /* setup g_4bppHTI */
  for(obj = 0; obj < OIL_EBD_SCRN_OBJECTS_4BPP; obj++)
  {
    for(color = OIL_Cyan; color <= OIL_Black; color++)
    {
      if(!GetScreenInfoFromPMS(st4bppScreenLookup[obj].eHtTables[color] , &pScreenInfo))
      {
        HQFAILV(("ebd_scrn_open_file: failed to get screen info for object=%d, colorant=%d", obj, color));
          return;
      }
      g_4bppHTI[color]->pHTables[obj].uWidth = (unsigned short)pScreenInfo->nCellWidth;
      g_4bppHTI[color]->pHTables[obj].uHeight = (unsigned short)pScreenInfo->nCellHeight;
      g_4bppHTI[color]->pHTables[obj].ditherMatrix = &pScreenInfo->pTable;
    }
  }
}

/**
 * \brief Reconfigure the screen lookup table based on the screen mode(s) requested by the job.
 *
 * This function configures the \c st4bppScreenLookup structure with appropriate values so that 
 * subsequent calls to htm4bpp_setupHTtables() will fetch the correct tables from the PMS.
 */
void htm4bpp_remapHTtables(OIL_eScreenQuality eImageQuality, OIL_eScreenQuality eGraphicsQuality, OIL_eScreenQuality eTextQuality)
{
  /* screen tables for Images/Photo */
  switch(eImageQuality)
  {
  case OIL_Scrn_LowLPI:
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_IMAGE_CYAN;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_IMAGE_MAGENTA;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_IMAGE_YELLOW;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_IMAGE_BLACK;
    break;
  case OIL_Scrn_MediumLPI:
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_GFX_CYAN;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_GFX_MAGENTA;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_GFX_YELLOW;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_GFX_BLACK;
    break;
  case OIL_Scrn_HighLPI:
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_TEXT_CYAN;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_TEXT_MAGENTA;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_TEXT_YELLOW;
    st4bppScreenLookup[GG_OBJ_IMAGE].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_TEXT_BLACK;
    break;
  }

  /* screen tables for Graphics */
  switch(eGraphicsQuality)
  {
  case OIL_Scrn_LowLPI:
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_IMAGE_CYAN;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_IMAGE_MAGENTA;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_IMAGE_YELLOW;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_IMAGE_BLACK;
    break;
  case OIL_Scrn_MediumLPI:
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_GFX_CYAN;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_GFX_MAGENTA;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_GFX_YELLOW;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_GFX_BLACK;
    break;
  case OIL_Scrn_HighLPI:
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_TEXT_CYAN;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_TEXT_MAGENTA;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_TEXT_YELLOW;
    st4bppScreenLookup[GG_OBJ_GFX].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_TEXT_BLACK;
    break;
  }

  /* screen tables for Text */
  switch(eTextQuality)
  {
  case OIL_Scrn_LowLPI:
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_IMAGE_CYAN;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_IMAGE_MAGENTA;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_IMAGE_YELLOW;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_IMAGE_BLACK;
    break;
  case OIL_Scrn_MediumLPI:
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_GFX_CYAN;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_GFX_MAGENTA;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_GFX_YELLOW;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_GFX_BLACK;
    break;
  case OIL_Scrn_HighLPI:
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Cyan] = PMS_SCREEN_4BPP_TEXT_CYAN;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Magenta] = PMS_SCREEN_4BPP_TEXT_MAGENTA;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Yellow] = PMS_SCREEN_4BPP_TEXT_YELLOW;
    st4bppScreenLookup[GG_OBJ_TEXT].eHtTables[OIL_Black] = PMS_SCREEN_4BPP_TEXT_BLACK;
    break;
  }

  htm4bpp_setupHTtables();
}



