/** \file
 * \ingroup gstate
 *
 * $HopeName: SWv20!export:gu_chan.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to manage rasterstyle structures, describing real or virtual
 * colorspaces.
 */

#ifndef __GU_CHAN_H__
#define __GU_CHAN_H__

#include "objecth.h"
#include "dl_color.h"
#include "gs_color.h"           /* COLORSPACE_ID */
#include "gscsmpxform.h"        /* EQUIVCOLOR */
#include "mps.h"
#include "swpgb.h"

struct CLINK;
struct blit_colormap_t;
struct mm_pool_t ;
struct DL_STATE ;
struct surface_t ;
struct GUCR_COLORANT_INFO;


/* Level 3 colorant machinery functions */

/** DEVICESPACEID is similar to the SPACE_... identifiers used
   elsewhere, but keeping these separate differentiates carefully
   between the device side represnetation of color spaces and the
   interpreters input spaces. There is a single point of transfer
   between the two (and it is assumed that if the spaces match (note -
   the numbers representing them never match though) the colorants for
   them are in the same order. */

typedef int32 DEVICESPACEID;

#define DEVICESPACE_Gray ((DEVICESPACEID) 0x47726179) /* 'Gray', well in PC byte order anyway */
#define DEVICESPACE_RGB  ((DEVICESPACEID) 0x52474220) /* 'RGB ' */
#define DEVICESPACE_CMYK ((DEVICESPACEID) 0x434D594B) /* 'CMYK' */
#define DEVICESPACE_RGBK ((DEVICESPACEID) 0x5247424B) /* 'RGBK' */
#define DEVICESPACE_CMY  ((DEVICESPACEID) 0x434D5920) /* 'CMY ' */
#define DEVICESPACE_Lab  ((DEVICESPACEID) 0x4C616220) /* 'Lab ' */
#define DEVICESPACE_N    ((DEVICESPACEID) 0x4E202020) /* 'N   ' */

typedef struct GUCR_COLORANTSET GUCR_COLORANTSET;

/** guc_colorantIndex returns the canonical numerical index for a
   colorant name identified in setcolorspace and similar. It returns
   COLORANTINDEX_UNKNOWN if the colorant is unknown. */

COLORANTINDEX guc_colorantIndex(
                                const GUCR_RASTERSTYLE *pRasterStyle,
         /*@in@*/ /*@notnull@*/ NAMECACHE * pColorantName);

/** guc_get/setBlackColorantIndex allow access to the cached black
   colorant index in the RasterStyle. They should only be used by
   gsc_blackColorantIndex and should not otherwise be called
   directly. The black colorant index is cleared in setupRasterStyle
   when a setpagedevice occurs.

   guc_getBlackColorantIndex returns the black colorant,
   COLORANTINDEX_UNKOWN meaning the black colorant must be worked out,
   COLORANTINDEX_ALL meaning the colorspace is additive and all channels must
   be zero for black, or COLORANTINDEX_NONE meaning there is no black
   colorant. */

COLORANTINDEX guc_getBlackColorantIndex(const GUCR_RASTERSTYLE *pRasterStyle);

void guc_setBlackColorantIndex(
  /*@notnull@*/ /*@in@*/       GUCR_RASTERSTYLE *pRasterStyle,
                               COLORANTINDEX ciBlack);

/** guc_setColorantMapping sets the colorant mapping from (e.g.)
 *  Cyan -> [ PhotoDarkCyan PhotoLightCyan ]
 * in terms of colorant indexes in the given raster style handle.
 * (note indices NOT names).
 * If a mapping already exists, then it replaces it.
 * If ci_map is NULL then it removes the existing mapping.
 */
Bool guc_setColorantMapping(
  /*@notnull@*/ /*@in@*/    GUCR_RASTERSTYLE *pRasterStyle,
                            COLORANTINDEX ci ,
                            const COLORANTINDEX *ci_map , int32 n ) ;

/** guc_getColorantMapping returns the mapping that exists (if it does)
 * for a given colorant (index).
 * e.g. given Cyan it returns [ PhotoDarkCyan PhotoLightCyan COLORANTINDEX_UNKNOWN ]
 *      (note indices NOT names).
 * e.g. given Fred it returns NULL.
 * note that if ci is COLORANTINDEX_ALL, this routine returns the first
 *      mapping that exists, which can be used to test if any mappings
 *      exist at all.
 */
/*@dependent@*/ /*@null@*/
COLORANTINDEX *guc_getColorantMapping(
  /*@notnull@*/                       const GUCR_RASTERSTYLE *pRasterStyle ,
                                      COLORANTINDEX ci) ;

/** guc_getInverseColorant returns the colorant (index) that maps onto
 * a colorant (index) set which contains the given colorant (index).
 * e.g. given PhotoDarkCyan, it returns Cyan (note indices NOT names).
 * e.g. given Fred is returns COLORANTINDEX_UNKNOWN.
 */
COLORANTINDEX guc_getInverseColorant(
  /*@notnull@*/                      const GUCR_RASTERSTYLE *pRasterStyle,
                                     COLORANTINDEX ci) ;

/** guc_colorantIndexPossiblyNewSeparation is like guc_colorantIndex, returning
   the index of the colorant given its name. However instead of returning
   COLORANTINDEX_UNKNOWN, it will add the colorant to the list of
   known colorants if not already there.

   guc_colorantIndexPossiblyNewName is similar again, but names
   introduced in this way are not recognised in calls to
   guc_colorantIndex. This is intended to reserve a number for a name
   in the space of colorant indexes, particularly when sethalftone
   mentions it.

   guc_colorantIndexPossiblyNewSeparation will upgrade a reserved name
   to a fully functional separation as required, but not the other way
   around.

   guc_colorantIndexReserved is like guc_colorantIndexPossiblyNewName
   except that it will return COLORANTINDEX_UNKNOWN instead of
   allocating a new number for an unidentified name.
 */

Bool guc_colorantIndexPossiblyNewName(
  /*@in@*/                            GUCR_RASTERSTYLE *pRasterStyle,
                                      NAMECACHE *pColorantName,
                                      COLORANTINDEX *ci);

Bool guc_colorantIndexPossiblyNewSeparation(GUCR_RASTERSTYLE *pRasterStyle,
                                            NAMECACHE * pColorantName,
                                            COLORANTINDEX *ci);

COLORANTINDEX guc_colorantIndexReserved(
  /*@notnull@*/                         const GUCR_RASTERSTYLE *pRasterStyle,
                                        NAMECACHE * pColorantName);

Bool guc_colorantSynonym(GUCR_RASTERSTYLE *pRasterStyle,
                         NAMECACHE * pNewName,
                         NAMECACHE * pExistingName,
                         COLORANTINDEX *ci);

/** Returns the ColorantType from ColorantDetails dictionary, if there is one,
   for the given colorant. We call this SpecialHandling, because we already
   have a colorantType field, meaning something different. */

sw_pgb_special_handling guc_colorantSpecialHandling(
  /*@notnull@*/                  const GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@in@*/         NAMECACHE * pColorantName) ;

/* Values returned by the above. */
#define SPECIALHANDLING_NONE SW_PGB_SPECIALHANDLING_NONE
#define SPECIALHANDLING_OPAQUE SW_PGB_SPECIALHANDLING_OPAQUE
#define SPECIALHANDLING_OPAQUEIGNORE SW_PGB_SPECIALHANDLING_OPAQUEIGNORE
#define SPECIALHANDLING_TRANSPARENT SW_PGB_SPECIALHANDLING_TRANSPARENT
#define SPECIALHANDLING_TRAPZONES SW_PGB_SPECIALHANDLING_TRAPZONES
#define SPECIALHANDLING_TRAPHIGHLIGHTS SW_PGB_SPECIALHANDLING_TRAPHIGHLIGHTS
#define SPECIALHANDLING_OBJECTMAP SW_PGB_SPECIALHANDLING_OBJECTMAP


/** Returns the NeutralDensity from ColorantDetails dictionary, if there is one,
   for the given colorant (0.0f otherwise). */

USERVALUE guc_colorantNeutralDensity(
  /*@notnull@*/                      const GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@in@*/             NAMECACHE * pColorantName) ;


/** guc_colorantScreenAngle:
   Looks for the colorant, or its complement, or the default colorant, in the
   default screen angles dictionary. Sets the screen angle in pScreenAngle
   and the override flag in pfOverrideScreenAngle.
   Returns TRUE/FALSE for success/failure.

   *** This routine should only be called when a new colorant is about
       to be added. The screen angle enquiry routine is guc_screenAngle. ***
 */
Bool guc_colorantScreenAngle(
  /*@notnull@*/ /*@in@*/     GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@in@*/     NAMECACHE * pColorantName,
  /*@notnull@*/ /*@out@*/    USERVALUE * pScreenAngle,
  /*@notnull@*/ /*@out@*/    Bool * pfOverrideScreenAngle);

/** guc_overrideScreenAngle finds the angle and override flag for ci.
   Returns TRUE/FALSE for success/failure. */

Bool guc_overrideScreenAngle(
  /*@notnull@*/              const GUCR_RASTERSTYLE *pRasterStyle,
                             COLORANTINDEX ci,
  /*@notnull@*/ /*@out@*/    SYSTEMVALUE * pAngle,
  /*@notnull@*/ /*@out@*/    Bool * pfOverride);

/** Returns TRUE iff fOverrideScreenAngle is TRUE for all channels. */
Bool guc_allChannelsOverrideScreenAngle(
  /*@notnull@*/                         const GUCR_RASTERSTYLE *pRasterStyle);

/** guc_CustomConversion fills in the object you supply with a
   procedure which you can then call the interpreter on. Custom
   conversions are from DeviceGray, DeviceRGB or DeviceCMYK to the
   target DeviceN color space. Therefore you must say which one you
   want (and we do not allow anything other than these three simple
   spaces) on deviceSpaceId. The procedure expects 1, 3 or 4 operands
   on the stack in natural order of the simple space and will yield as
   many values as there are colorants in the output space, which is
   the same number as returned in pnColorants in guc_deviceColorSpace
   below
 */

void guc_CustomConversion(
  /*@notnull@*/           const GUCR_RASTERSTYLE *pRasterStyle,
                          DEVICESPACEID deviceSpaceId,
  /*@notnull@*/ /*@out@*/ OBJECT * poProcedure);

/** guc_deviceColorSpace tells you what color space the output device
   is working in: essentially this is ProcessColorModel from the page
   device. It also tells you how many _process_ colorants there are in
   the color space (so for DeviceCMYK, for example, there are exactly 4).
 */

void guc_deviceColorSpace(
  /*@notnull@*/           const GUCR_RASTERSTYLE *pRasterStyle,
  /*@null@*/ /*@out@*/    DEVICESPACEID * pDeviceSpaceId,
  /*@null@*/ /*@out@*/    int32 * pnColorants);

/** guc_calibrationColorSpace tells you what color space we wish calibration
   and color management to be performed in. Normally, this will be the same
   as ProcessColorModel and will be indicated by returning SPACE_notset.
   For PhotoInk printers, we may want a different value. The only other
   supported values are for DeviceGray, DeviceRGB and DeviceCMYK.
 */

void guc_calibrationColorSpace(
  /*@notnull@*/                const GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@out@*/      COLORSPACE_ID * pCalibrationSpaceId);

/** guc_deviceColorSpaceSubtractive sets the flag according to the
   colorspace of the output device. If the colorspace is lab then it
   is not straightforward to categorize as subtractive or additive, in
   which case the routine should not be called.
 */

Bool guc_deviceColorSpaceSubtractive(
  /*@notnull@*/                      const GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@out@*/            Bool * pfSubtractivePCM );

/** guc_deviceToColorSpaceId reports a COLORSPACE_ID (ie. SPACE_...)
   equivalent of the DEIVCESPACEID colorspace the output device
   is working in: essentially this is ProcessColorModel from the page
   device.
 */

void guc_deviceToColorSpaceId(
  /*@notnull@*/               const GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@out@*/     COLORSPACE_ID *pColorSpaceId );


/** guc_colorSpace returns a valid colorspace object that characterises all
   backdrop raster styles. This could be a device independent or a device
   colorspace, but is essential for device independent blend spaces.
   This function isn't useful for device raster styles so if one is used an
   assert will be thrown.
 */

void guc_colorSpace(
  /*@notnull@*/               const GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@out@*/     OBJECT *pColorSpace );

/** guc_simpleDeviceColorSpaceMapping fills in an array pMapping with
   the colorant indexes corresponding to the colorants of the given
   device color space DeviceSpaceId (some of which may be
   COLORANTINDEX_UNKNOWN), which must be one of SPACE_DeviceGray,
   SPACE_DeviceRGB or SPACE_DeviceCMYK. pMapping must be long enough:
   the number of elements is determined by guc_deviceColorSpace (and
   which will always be 1, 3 or 4).
 */

Bool guc_simpleDeviceColorSpaceMapping(
  /*@notnull@*/ /*@in@*/               GUCR_RASTERSTYLE *pRasterStyle,
                                       DEVICESPACEID deviceSpaceId,
  /*@notnull@*/ /*@out@*/              COLORANTINDEX * pMapping,
                                       int32 nElementsInMapping);

/** guc_getColorantName() returns the name of the first fully fledged colorant
 * associated with the colorant index in the named raster style. This name
 * isn't necessarily unique, or even the primary colorant, but can be used to
 * determine if a colorant index is fully fledged.
 */

NAMECACHE* guc_getColorantName(
  /*@notnull@*/                GUCR_RASTERSTYLE *pRasterStyle,
                       COLORANTINDEX ci);

/** guc_processMapped() looks for any process colorants in the real
 * rasterstyle's process colorspace that have been mapped to a different
 * colorant.
 */

Bool guc_processMapped(
  /*@notnull@*/        const GUCR_RASTERSTYLE* p_raster) ;


/** guc_outputColorSpaceMapping produces a mapping from an output colorant
 * index array (the result of a color chain invocation) to the required
 * ordering for the (pseudo)device (and as described in the (psuedo)device
 * colorspace object if appropriate).
 */

Bool guc_outputColorSpaceMapping(
                                 mm_pool_t pool,
  /*@notnull@*/ /*@in@*/         GS_COLORinfo *colorInfo,
  /*@notnull@*/ /*@in@*/         GUCR_RASTERSTYLE *pRasterStyle,
                                 COLORSPACE_ID devColorSpaceId,
                                 OBJECT *devColorSpaceObj,
                                 int32 n_iColorants,
                                 COLORANTINDEX *oColorants,
                                 int32 n_oColorants,
  /*@notnull@*/ /*@out@*/        int32 **oToDevMapping,
  /*@notnull@*/ /*@out@*/        int32 *n_devColorants );


/* ---------------------------------------------------------------------- */
/** \page rasterstyle Rasterstyle iterators.

   Frame and colorant iterators. The internals of gu_chan.c represent the
   interleaving structure using GUCR_SHEET, GUCR_CHANNEL, and GUCR_COLORANT
   structures. A "sheet" is a separate unit of output (think; a separate
   PGB). A "channel" corresponds to the plugin's notion of a colour that may
   be output in a page. A "colorant" is a way of mapping a single component
   of a device colour to an output channel.

   Here are how sheets, channels and colorants are arranged for some common
   formats:

   Monochrome:
   \verbatim <pre>
     Sheet 0
       Channel 0
         Colorant /Gray
   </pre> \endverbatim

   CMYK separations:
   \verbatim <pre>
     Sheet 0
       Channel 0
         Colorant /Cyan
     Sheet 1
       Channel 0
         Colorant /Magenta
     Sheet 2
       Channel 0
         Colorant /Yellow
     Sheet 3
       Channel 0
         Colorant /Black
   </pre> \endverbatim

   CMYK composite (band, frame and pixel) interleaved:
   \verbatim <pre>
     Sheet 0
       Channel 0
         Colorant /Cyan
       Channel 1
         Colorant /Magenta
       Channel 2
         Colorant /Yellow
       Channel 3
         Colorant /Black
   </pre> \endverbatim

   The frame and colorant iterators expose these structures through
   incomplete typed pointers to GUCR_CHANNEL and GUCR_COLORANT, but not
   GUCR_SHEET. The frame iterators return channels, and the colorant
   iterators return colorants.

   Even though CMYK band, frame and pixel interleaved have the same
   sheet/channel/colorant structure, the frame and colorant iterators are
   mapped onto the structure in subtly different ways. For band and pixel
   interleaved, the frame iterator will iterate over each sheet in turn, and
   the colorant iterators will return the colorants of all of the channels
   within the sheet. For frame interleaved, the frame iterator will return
   each channel within each sheet in turn, and the colorant iterators will
   return each colorant within that channel. Monochrome and separations only
   have one channel per sheet. Since we do not have explicit sheet iterators,
   the predicates gucr_framesStartOfSheet() and gucr_framesEndOfSheet() are
   provided to determine if the frame is the first or last of a sheet
   respectively. gucr_colorantsBandChannelAdvance() distinguishes band
   interleaved from pixel and frame interleaved; when band interleaved, it
   will return a true value if the next colorant is on a different
   channel. When not band interleaved, it will return a false value.

   A flag is supplied to gucr_colorantsMore() to indicate whether
   pixel-interleaved styles should iterate over every colorant in a frame, or
   just the first colorant.

   Some rarer layouts with interesting mappings are:

   RGB composite progressives, frame interleaved (colored separations is
   similar, but has only one of R, G, B per sheet, in the appropriate channel):
   \verbatim <pre>
     Sheet 0
       Channel 0
         Colorant (Red)
       Channel 1
         Colorant (Unknown)
       Channel 1
         Colorant (Unknown)
     Sheet 1
       Channel 0
         Colorant (Red)
       Channel 1
         Colorant (Green)
       Channel 1
         Colorant (Unknown)
     Sheet 2
       Channel 0
         Colorant (Red)
       Channel 1
         Colorant (Green)
       Channel 2
         Colorant (Blue)
   </pre> \endverbatim

   Monochrome separation to a CMYK device:
   \verbatim <pre>
     Sheet 0
       Channel 0
         Colorant (Unknown)
       Channel 1
         Colorant (Unknown)
       Channel 2
         Colorant (Unknown)
       Channel 3
         Colorant /Black
   </pre> \endverbatim

   Multiple colorants can occur in a channel, usually with imposed
   separations or step and repeat. The /Frame and /InsertBefore entries in
   the addtoseparationorder dictionary controls whether separations are added
   to an existing channel. These can result in layouts like:

   Imposing CMYK separations onto a single monochrome separation:
   \verbatim <pre>
     Sheet 0
       Channel 0
         Colorant /Cyan, position (0,0.5)
         Colorant /Magenta, position (0.5,0.5)
         Colorant /Yellow, position (0,0)
         Colorant /Black, position (0.5,0)
   </pre> \endverbatim

   Step-and-Repeat onto CMYK separations:
   \verbatim <pre>
     Sheet 0
       Channel 0
         Colorant /Cyan, position (0,0)
         Colorant /Cyan, position (0,0.5)
         Colorant /Cyan, position (0.5,0)
         Colorant /Cyan, position (0.5,0.5)
     Sheet 1
       Channel 0
         Colorant /Magenta, position (0,0)
         Colorant /Magenta, position (0,0.5)
         Colorant /Magenta, position (0.5,0)
         Colorant /Magenta, position (0.5,0.5)
     Sheet 2
       Channel 0
         Colorant /Yellow, position (0,0)
         Colorant /Yellow, position (0,0.5)
         Colorant /Yellow, position (0.5,0)
         Colorant /Yellow, position (0.5,0.5)
     Sheet 3
       Channel 0
         Colorant /Black, position (0,0)
         Colorant /Black, position (0,0.5)
         Colorant /Black, position (0.5,0)
         Colorant /Black, position (0.5,0.5)
   </pre> \endverbatim

   The frame and colorant iterators take separation omission into account
   during rendering. Separation omission is only relevant during
   rendering. During interpretation, all frames and colorants will be
   iterated. The render omit marks are set by guc_omitSeparations() and
   guc_markBlankSeparations() (both called from renderbands_() via
   init_final_render()), and cleared by guc_resetOmitSeparations() (called
   directly from renderbands_()).

   Whilst separation omission is in effect, colorants and frames that are
   entirely blank may be skipped. The plugin can specify that channels are
   required. The colorant iterators will stop at the start of each required
   channel, regardless of whether the first (or even all) colorants in the
   channel are omitted or not. It is thus NOT safe to assume that colorant
   handles returned by the iterator are rendered. The function
   gucr_colorantDescription() should be used to test if the colorant was
   omitted, and to extract further information about the colorant. It IS
   possible to have a required channel all of whose colorants have been
   omitted. In this case, the RIP needs to see the channel in the iteration
   so that it can inform the plugin that the channel is empty.

   Before rendering, the function blit_colormap_create() should be used to
   set up a map of the colorant indices of the appropriate channel(s) to
   render. It takes separation omission into account when setting up the
   indices.

   If you are still confused, create a debug build and call the function
   debug_print_gucr_rasterstyle(gstateptr->hRasterStyle, FALSE). This will
   print out the entire rasterstyle/sheets/channels/colorants structure in
   detail. The second argument determines if underlying rasterstyles for
   group/virtual rasterstyles are also printed.

   gucr_framesStart, gucr_framesMore and gucr_framesNext are the
   functions for iterating over the frames of a page. These frames may
   end with page breaks in which case we end up with separations of
   the page (though still possibly more than one per sheet). Once a
   handle is acquired, one can make inquiries about the current frame
   or frame set, and iterate over the colorants in a frame

   Here is schematic example of how the iterator functions should be used:

  \code
  // assume you have a GUCR_RASTERSTYLE in your hand, called hr

  nInterleavingStyle = gucr_interleavingStyle(hr);
  ...
  for (hf = gucr_framesStart(hr); // returns handle
       gucr_framesMore(hf);
       gucr_framesNext(& hf))
  {
    if (gucr_framesStartOfSheet(hf, & nBandMultiplier, & nSheetNumber)) {
      // do necessary to prepare for page
      // (will always be true on first iteration)
    }

    for (all bands in frame) {
      for (hc = gucr_colorantsStart(hf);
           gucr_colorantsMore(hc, ! GUCR_INCLUDING_PIXEL_INTERLEAVED);
             // loop only iterates once for frame, mono and pixel interleaving
           gucr_colorantsNext(& hc))
      {
        const GUCR_COLORANT_INFO *colorantInfo;

        if ( !blit_colormap_create(&blitmap, mm_pool_temp,
                                   output_surface, hc,
                                   0, // Do not override depth
                                   FALSE, // Type channel
                                   FALSE // Don't force masking
                                   ) )
          break ;

        if ( !gucr_colorantDescription(hc, &colorantInfo) ) {
          // omitted colorant, do whatever is necessary here.
          goto cleanup_and_continue;
        }

        // erase

        colorantIndex = colorantInfo->colorantIndex ;
        nmColorantName = colorantInfo->name ;
        for (all objects in band) {
          // render object
          if (contone ) { // typically done by selecting the correct render function
            // usually call blit_color_pack() or similar to get packed
            // render color. Can also iterate colorants by doing:
            bitvector_iterator_t iterator ;
            for ( BITVECTOR_ITERATE_BITS(iterator, map->nchannels) ;
                  BITVECTOR_ITERATE_BITS_MORE(iterator) ;
                  BITVECTOR_ITERATE_BITS_NEXT(iterator) ) {
              if ( blitmap->rendered[iterator.element] & iterator.mask ) {
                // colorant is rendered
                COLORANTINDEX ci = blitmap->channel[iterator.bit].ci ;
                // if we have a blit_color_t:
                COLORVALUE cv = color->unpacked.channel[iterator.bit].cv ;
                ...
              }
            }
          }
          ...
        }

        if (gucr_colorantsBandChannelAdvance(hc)) {
          // do whatever necessary to move on to next colorant channel, in particular...
          HQASSERT(nInterleavingStyle == GUCR_INTERLEAVINGSTYLE_BAND, "woops");
          theFormA(retainedform) = BLIT_ADDRESS(theFormA(retainedform),
                                                theFormS(retainedform));
          // and related stuff
        }

  cleanup_and_continue:
        blit_colormap_destroy(&blitmap) ;
      }
    }

    if (gucr_framesEndOfSheet(hf)) {
      // do necessary for throwing page
    }
  }

  \endcode
*/

/*@dependent@*/ /*@null@*/
GUCR_CHANNEL* gucr_framesStart(
  /*@notnull@*/ /*@in@*/        const GUCR_RASTERSTYLE *pRasterStyle ) ;

/*@falsewhennull@*/
Bool gucr_framesMore(
  /*@null@*/ /*@in@*/           const GUCR_CHANNEL* pChannel ) ;

void gucr_framesNext(
  /*@notnull@*/ /*@in@*/        GUCR_CHANNEL** ppChannel ) ;


/** Type of callback for gucr_sheetIterateColorants.

  \param[in] colInfo  Info of a colorant on the sheet.
  \param[in] p  Data from caller of \c gucr_sheetIterateColorants.
  \return Whether to continue iteration.
 */
typedef Bool gucr_sheetIterateColorantsFn(
  const struct GUCR_COLORANT_INFO *colInfo, void *p);


/** Iterator for the colorants of a sheet.

  \param[in] hf  Sheet handle.
  \param[in] fn  Callback invoked for every colorant.
  \param[in] p  Data passed through to callback.
 */
Bool gucr_sheetIterateColorants(GUCR_CHANNEL* hf,
                                gucr_sheetIterateColorantsFn fn, void *p);


/* gucr_interleavingStyle returns the interleaving style for the
   frame set */

/* Interleavingstyle values have meaning in PostScript, so give them explicit
   numbers. */
enum {
  GUCR_INTERLEAVINGSTYLE_MONO  = 1,
  GUCR_INTERLEAVINGSTYLE_PIXEL = 2,
  GUCR_INTERLEAVINGSTYLE_BAND  = 3,
  GUCR_INTERLEAVINGSTYLE_FRAME = 4
} ;
int32 gucr_interleavingStyle (
  /*@notnull@*/ /*@in@*/      const GUCR_RASTERSTYLE *pRasterStyle ) ;

/* gucr_separationStyle returns the separation style for the
   frame set */

/* Separation style values have meaning in PostScript, so give them explicit
   numbers. */
enum {
  GUCR_SEPARATIONSTYLE_MONOCHROME = 0,
  GUCR_SEPARATIONSTYLE_SEPARATIONS = 1,
  GUCR_SEPARATIONSTYLE_COLORED_SEPARATIONS = 2,
  GUCR_SEPARATIONSTYLE_PROGRESSIVES = 3,
  GUCR_SEPARATIONSTYLE_COMPOSITE = 4
} ;
int32 gucr_separationStyle(
  /*@notnull@*/ /*@in@*/   const GUCR_RASTERSTYLE *pRasterStyle ) ;


/** Returns the number of values per color component in the final raster
 *
 * As set by setpagedevice. */
int32 gucr_valuesPerComponent(
  /*@notnull@*/ /*@in@*/      const GUCR_RASTERSTYLE *pRasterStyle ) ;

/** Returns the number of bits per color component */
int32 gucr_rasterDepth(
  /*@notnull@*/ /*@in@*/ const GUCR_RASTERSTYLE *pRasterStyle );

/** Returns the shift that multiplies by bits per color component
 *
 * Obviously this shouldn't be called for 10-bit RLE.
 */
int32 gucr_rasterDepthShift(
  /*@notnull@*/ /*@in@*/ const GUCR_RASTERSTYLE *pRasterStyle );

/** A simplified binary log */
int32 gucr_ilog2(int32 vpc);

/** Indicates whether the output is screened (incl. modular) */
Bool gucr_halftoning(const GUCR_RASTERSTYLE *pRasterStyle);


/** gucr_framesStartOfSheet is an inquiry function which returns TRUE
   if the frame identified by the handle is at the first (or only)
   frame of a sheet and FALSE otherwise. It also sets the integer
   pointed at by pnBandMultiplier (when not null) to the number of
   colorants in a band for this sheet (this will always be 1 except in
   band interleaving). It can also return the sequence number of the
   sheet (when the parameter is non-null). This is so you have an
   absolute reference should any sheets be omitted as a result of
   separation omission (but note this sequence numbering can be
   changed when adding new sheets partway through the sequence).
*/
Bool gucr_framesStartOfSheet(
  /*@notnull@*/ /*@in@*/     const GUCR_CHANNEL* pChannel ,
  /*@null@*/ /*@out@*/       int32 * pnBandMultiplier,
  /*@null@*/ /*@out@*/       int32 * pnSheetNumber) ;

/** gucr_framesEndOfSheet is the opposite function to gucr_framesStartOfSheet
   - it returns TRUE if the frame is the last (or only) frame of a sheet. */
Bool gucr_framesEndOfSheet(
  /*@notnull@*/ /*@in@*/   const GUCR_CHANNEL* pChannel) ;

/** gucr_framesChannelsTotal returns the total number of output
   channels remaining in a frame (or total when positioned at the
   start of the frame, which is not the same as the number of
   colorants (since some channels may be unassigned and some may have
   several colorants assigned).
*/

int32 gucr_framesChannelsTotal(
  /*@notnull@*/ /*@in@*/       const GUCR_CHANNEL* pChannel);

/** gucr_framesChannelsLeft returns the number of output channels remaining
   in a frame (or the total, when positioned at the start of the frame). This
   may be different to gucr_framesChannelsTotal as it takes channel omission
   into account.
   */
int32 gucr_framesChannelsLeft(
  /*@notnull@*/ /*@in@*/      const GUCR_CHANNEL* pChannel) ;

/** Given a current frame, gucr_colorantsStart, gucr_colorantsMore and
   gucr_colorantsNext are the functions to control iteration over
   colorants of the frame, that is the colorants which need to be
   rendered in each band of the frame.  In gucr_colorantsMore,
   fIncludingPixelInterleaved says whether you should get a further
   iteration for each colorant of pixel interleaved mode or not: use
   the manifest constant.  */

/*@dependent@*/ /*@null@*/
GUCR_COLORANT* gucr_colorantsStart(
  /*@notnull@*/ /*@in@*/           const GUCR_CHANNEL* pChannel ) ;

/* Would like to annotate this with @falsewhennull@, but a Splint doesn't
   match its documentation. */
Bool gucr_colorantsMore(
  /*@null@*/ /*@in@*/   const GUCR_COLORANT* pColorant,
                        Bool fIncludingPixelInterleaved) ;

#define GUCR_INCLUDING_PIXEL_INTERLEAVED TRUE
/* or ! GUCR_INCLUDING_PIXEL_INTERLEAVED as appropriate */

void gucr_colorantsNext(
  /*@notnull@*/ /*@in@*/        GUCR_COLORANT** ppColorant ) ;

/** gucr_colorantsBandChannelAdvance indicates if the next colorant iteration
   would advance the channel. Only band interleaved mode supports this, when
   it will usually be true on each call. The only case when this is not true
   is when there are imposed separations in band interleaved composites
   (e.g. step and repeat). */
Bool gucr_colorantsBandChannelAdvance(
  /*@notnull@*/ /*@in@*/          const GUCR_COLORANT* pColorant ) ;


/** gucr_colorantHandle: Do not always want to iterate over frames, sheets,
   but instead go straight to a particular colorant. This method returns the
   first GUCR_COLORANT (pColorant) for the given ci to enable all the usual
   colorant enquiry methods to be subsequently called. Note the handle will
   be null unless the colorant is fully fledged. Also, note that multiple
   colorant structures with the same index may exist, because extra
   separations (perhaps with different render properties) were introduced by
   the addtoseparationorder operator. If you care about this, iterate over
   all colorants instead of using this function. */
void gucr_colorantHandle(
  /*@null@*/              const GUCR_RASTERSTYLE *pRasterStyle,
                          COLORANTINDEX ci,
  /*@notnull@*/ /*@out@*/ GUCR_COLORANT** ppColorant);

/** Gets a consistent ID for a particular separation, regardless of
    separation omission. */
int32 guc_getSeparationId(const GUCR_CHANNEL *channel) ;

/** Sets the rendering index. Note that the presence of a positive value here
    has strong implications, namely that this channel should never be omitted as
    blank. */
void gucr_setRenderIndex(GUCR_CHANNEL* pChannel, int32 nIndex);

/** Gets the rendering index from the given channel. */
int32 gucr_getRenderIndex(const GUCR_CHANNEL* pChannel) ;

/** gucr_colorantDescription gives information about a colorant identified by
   its handle. gucr_colorantDescription returns a flag indicating if the
   colorant is renderable (it is known, and is not omitted). It is the
   caller's responsibility to check the flag and override any fields from the
   colorant info that are not relevant. The information is returned as a
   const pointer to details within the colorant structure; do NOT cast away
   the constness of this pointer, this structure should be managed from
   within gu_chan.c ONLY. */

typedef struct GUCR_COLORANT_INFO {
  COLORANTINDEX colorantIndex;
  int32 colorantType;
  /*@dependent@*/
  NAMECACHE *name; /* the (potentially detected) name of the colorant */
  /*@dependent@*/
  NAMECACHE *originalName; /* the original name of the colorant */
  int32 offsetX;
  int32 offsetY;
  int32 offsetBand;
  uint32 nRenderingProperties;
  int32 specialHandling;
  USERVALUE sRGB[3] ;
  USERVALUE CMYK[4] ;
  uint8 fBackground ;
  uint8 fAutomatic ;
  uint8 fOverrideScreenAngle;
  USERVALUE screenAngle;
  USERVALUE neutralDensity ;
} GUCR_COLORANT_INFO ;

/* Type of the colorant - Process, Spot, or ExtraSpot (dynamic spot
   separation) */
enum { COLORANTTYPE_UNKNOWN, COLORANTTYPE_PROCESS,
       COLORANTTYPE_SPOT, COLORANTTYPE_EXTRASPOT } ;

Bool gucr_colorantDescription(
  /*@notnull@*/ /*@in@*/      const GUCR_COLORANT *pColorant,
  /*@notnull@*/ /*@out@*/     const GUCR_COLORANT_INFO **info) ;

/** gucr_colorantCount/gucr_colorantIndices:
   NOTE: guc_colorantCount returns nColorants which includes duplicates.
   This routine should only be used to work out the size of the colorant
   index buffer to subsequently pass into gucr_colorantIndices.  This
   routine fills in the cis with the colorant indices known to the given
   raster style.  The colorant indices are sorted and duplicates are
   removed.  Also returns nUniqueColorants which excludes duplicates. */
void gucr_colorantCount(
  /*@notnull@*/           const GUCR_RASTERSTYLE* pRasterStyle,
  /*@notnull@*/ /*@out@*/ uint32* nColorants);
void gucr_colorantIndices(
  /*@notnull@*/           const GUCR_RASTERSTYLE* pRasterStyle,
  /*@notnull@*/ /*@out@*/ COLORANTINDEX* cis,
  /*@notnull@*/ /*@out@*/ uint32* nUniqueColorants);


/** gucr_colorantsMultiple returns a boolean saying whether a band may contain
   multiple renderable colorants, and therefore whether it is unsafe to omit
   the band completely during rendering */

Bool gucr_colorantsMultiple(
  /*@notnull@*/             const GUCR_COLORANT* pColorant);

/** gucr_getChannelAssignment is a transition function which for a given
   channel number returns a number which is 0 for Cyan or Red or Gray, 1 for
   Magenta or Green and so on, according to the output space, or if any other
   colorants are found returns -1 (including for trailing unset colorants) */

int32 gucr_getChannelAssignment(
  /*@notnull@*/                 const GUCR_RASTERSTYLE *pRasterStyle,
                                int32 nChannel);

/** guc_setupRasterStyle primes the whole subsystem on a
   setpagedevice. It allocates the initial handle. Note that
   guc_discardRasterStyle should be called first if the handle being
   assigned was already previously assigned */

Bool guc_setupRasterStyle(
  Bool screening,
  OBJECT * poProcessColorModel,
  OBJECT * poCalibrationColorModel,
  OBJECT * poInterleavingStyle,
  OBJECT * poValuesPerComponent,
  OBJECT * poSeparationStyle,
  OBJECT * poNumProcessColorants,
  OBJECT * poSeparationDetails,
  OBJECT * poSeparationOrder,
  OBJECT * poColorChannels,
  OBJECT * poFullyFledgedColorants,
  OBJECT * poReservedColorants,
  OBJECT * poDefaultScreenAngles,
  OBJECT * poCustomConversions,
  OBJECT * posRGB,
  OBJECT * poProcessColorants,
  OBJECT * poColorantPresence,
  OBJECT * poProcessColorant_Black,
  OBJECT * poColorantDetails,
  OBJECT * poObjectTypeMap,
  /*@notnull@*/ /*@out@*/ GUCR_RASTERSTYLE **ppRasterStyle);

/**
 * Rasterstyles are copied to allow pipelining of interpreting and rendering.
 * It is not quite a complete copy as not everything needs copying over for the
 * backend.  The copied rasterstyle is allocated from the dl pool memory and no
 * PSVM objects should remain.
 */
Bool guc_copyRasterStyle(mm_pool_t *dlpools, GUCR_RASTERSTYLE *rsSrc,
                         GUCR_RASTERSTYLE **rsDst);

void guc_reserveRasterStyle(
  /*@notnull@*/ /*@in@*/        GUCR_RASTERSTYLE *pRasterStyle);

void guc_discardRasterStyle(
  /*@notnull@*/ /*@in@*/    GUCR_RASTERSTYLE **pRasterStyle);

uint32 guc_rasterstyleId(/*@notnull@*/ const GUCR_RASTERSTYLE *pRasterStyle);

mps_res_t gucr_rasterstyle_scan(mps_ss_t ss, GUCR_RASTERSTYLE *pRasterStyle);

/** guc_init is only to be called from startup, to prime
   guc_setupRasterStyle with some initial values */
Bool guc_init(
  /*@notnull@*/ /*@out@*/ GUCR_RASTERSTYLE **ppRasterStyle);

/** guc_finish removes the GC root that guc_init creates. */
void guc_finish(void);

/** guc_setupBackdropRasterStyle:
   Creates a special raster style for transparency group blend spaces and
   'virtual devices' for colormetric overprinting of arbitrary spot colors.
   The raster style is used as part of rendering objects to a backdrop
   object (similar to a linework object). The backdrop object may eventually
   be rendered (after being color converted) using the normal real output
   device raster style. */
Bool guc_setupBackdropRasterStyle(
  /*@notnull@*/ /*@out@*/         GUCR_RASTERSTYLE** ppRasterStyle,
  /*@notnull@*/ /*@in@*/          GUCR_RASTERSTYLE *parentRS,
  /*@notnull@*/ /*@in@*/          GS_COLORinfo *colorInfo,
                                  OBJECT *colorSpace,
                                  Bool fVirtualDevice,
                                  Bool fAutoSeparations,
                                  Bool fInheritSeparations);

Bool guc_setup_image_filter_RasterStyle(
  /*@notnull@*/ /*@out@*/         GUCR_RASTERSTYLE** ppRasterStyle,
                                  COLORSPACE_ID processSpace,
                                  int32   valuesPerComponent);

/** Returns TRUE if the raster style is a backdrop and FALSE if
   it is a native output raster style. */
Bool guc_backdropRasterStyle(
  /*@notnull@*/              const GUCR_RASTERSTYLE *pRasterStyle);

/** Returns TRUE if the raster style is the virtual device and FALSE otherwise */
Bool guc_virtualRasterStyle(
  /*@notnull@*/             const GUCR_RASTERSTYLE *pRasterStyle);

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/** Given a ci, returns true iff there is an equivalent colorant in the real
    rasterstyle.  In the case of recipe colors, a colorant can map to several
    other colorants.  The equivalent colorant(s) are passed back in cimap,
    the last cimap entry contains COLORANTINDEX_UNKNOWN. */
Bool guc_equivalentRealColorantIndex(
  /*@notnull@*/                      const GUCR_RASTERSTYLE* pRasterStyle,
                                     COLORANTINDEX ci,
  /*@notnull@*/ /*@out@*/            COLORANTINDEX** cimap);

/** Update the list of equivalent colorants in this raster style and all its
    ancestors.  An equivalent colorant is a colorant that also exists in the
    device rasterstyle (the colorant indices may differ). */
Bool guc_updateEquivalentColorants(
  /*@notnull@*/ /*@in@*/               GUCR_RASTERSTYLE* pRasterStyle,
                                       COLORANTINDEX ci);

/* ----------------------------------------------------------------------
   The following functions (together with guc_colorantIndexPossiblyNewSeparation)
   form the C interface to dynamic separations.

   guc_addAutomaticSeparation makes a new separation automatically
   for the colorant whose name is passed to it when the SeparationDetails Add
   page device key is set. */

Bool guc_fOmitMonochrome(
  /*@notnull@*/          const GUCR_RASTERSTYLE *pRasterStyle);
Bool guc_fOmitSeparations(
  /*@notnull@*/           const GUCR_RASTERSTYLE *pRasterStyle);
Bool guc_fOmitBlankSeparations(
  /*@notnull@*/                const GUCR_RASTERSTYLE *pRasterStyle);

/** Create a dynamic separation when a new colorant is encountered */
Bool guc_addAutomaticSeparation(
  /*@notnull@*/ /*@in@*/        GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@in@*/        NAMECACHE * pColorantName,
                                Bool f_do_nci);
void guc_removeAutomaticSeparations(
  /*@notnull@*/ /*@in@*/            GUCR_RASTERSTYLE *pRasterStyle);

/** guc_clearColorant relegates the given colorant index from fully-fledgded. Any
   subsequent uses of this colorant, for example in DeviceN color spaces will
   be diverted through the tint transforms. However, the colorant index will
   still exist in the reserved colorants dictionary, and any objects already
   preapred using this color will remain on the display list, cause knockouts
   as normal, but will simply not be rendered. If ci is COLORANTINDEX_ALL,
   all colorants are removed. */

void guc_clearColorant(GUCR_RASTERSTYLE *pRasterStyle, COLORANTINDEX ci);

typedef int32 GUC_FRAMERELATION;

#define GUC_FRAMERELATION_AFTER  ((GUC_FRAMERELATION) 0x41465452) /* AFTR */
#define GUC_FRAMERELATION_BEFORE ((GUC_FRAMERELATION) 0x42465245) /* BFRE */
#define GUC_FRAMERELATION_AT     ((GUC_FRAMERELATION) 0x41542020) /* AT   */
#define GUC_FRAMERELATION_N_A    ((GUC_FRAMERELATION) 0x4E204120) /* N A  */
#define GUC_FRAMERELATION_END    ((GUC_FRAMERELATION) 0x454E4420) /* END  */
#define GUC_FRAMERELATION_START  ((GUC_FRAMERELATION) 0x53545254) /* STRT */

#define GUC_RELATIVE_TO_FRAME_UNKNOWN (-1)

/** guc_newFrame adds a named colorant for rendering, with the separation
   offset by x,y (in left handed coordinates, but in pixels). It must already
   be a known colorant. When in the rendering sequence the colorant will
   appear depends on frameRelation and nRelativeToFrame. nRelativeToFrame is
   the sequence number of the frame (separation or channel) where the new
   colorant is to be placed relative to, and frameRelation specifies tha
   relation - before, after, at that frame, or at the end or start of all
   frames (in the last two cases nRelativeToFrame must be set to the unique
   value GUC_RELATIVE_TO_FRAME_UNKNOWN). */

Bool guc_newFrame(GUCR_RASTERSTYLE *pRasterStyle,
                  NAMECACHE * pColorantName,
                  int32 x, int32 y,
                  Bool fBackground,
                  Bool fAutomatic,
                  uint32 nRenderingProperties,
                  GUC_FRAMERELATION frameRelation,
                  int32 nRelativeToFrame,
                  int32 specialHandling,
                  USERVALUE neutralDensity,
                  USERVALUE screenAngle,
                  Bool fOverrideScreenAngle,
                  Bool doingRLE,
                  COLORANTINDEX * pCi);

/** guc_frameRelation: A value for nRelativeToFrame in
   guc_colorantIndexPossiblyNewFrame can be derived from a colorant index
   (and indirectly, using the existing guc_colorantIndex, from a name) using
   guc_frameRelation. This function finds the first occurrence of the
   colorant index and returns the appropriate frame number for use as
   nRelativeToFrame. In this way one can say "insert separation after Cyan"
   or "add a new instance of Magenta on the same sheet as it already exists
   on". The routine will return the unique value
   GUC_RELATIVE_TO_FRAME_UNKNOWN if the colorant cannot be found. */

int32 guc_frameRelation(
  /*@notnull@*/         const GUCR_RASTERSTYLE *pRasterStyle,
                        COLORANTINDEX ci);

/** guc_colorantSetBackgroundSeparation marks the given colorant as a
   background separation; guc_colorantIsBackgroundSeparation answers the
   obvious question */
Bool guc_colorantSetBackgroundSeparation(
  /*@notnull@*/ /*@in@*/                 GUCR_RASTERSTYLE *pRasterStyle,
  COLORANTINDEX ci);

Bool guc_colorantAnyBackgroundSeparations(
  /*@notnull@*/                           const GUCR_RASTERSTYLE *pRasterStyle);

Bool guc_colorantIsBackgroundSeparation(
  /*@notnull@*/                         const GUCR_RASTERSTYLE *pRasterStyle,
                                        COLORANTINDEX ci);

/** guc_overrideColorantName changes the color name of all colorants with name
   colName to that of sepName and sets up the equivalent CMYK and sRGB values
   for roam. */

Bool guc_overrideColorantName(
  /*@notnull@*/ /*@in@*/      GUCR_RASTERSTYLE *pRasterStyle,
                              const NAMECACHE *colName,
                              NAMECACHE *sepName);

/** guc_abortSeparations checks to see if we're aborting when we've got mono
   or presep page in and aborts if so with an appropriate error message */

Bool guc_abortSeparations(
  /*@notnull@*/           const GUCR_RASTERSTYLE *pRasterStyle,
                          Bool fIsSeparations);

/** guc_omitSeparations checks to see if we're omitting separations when we've
   got mono or presep page in and if so then modifes the
   raster/frame/channel/colorant structure to do this */

Bool guc_omitSeparations(
  /*@notnull@*/ /*@in@*/ GUCR_RASTERSTYLE *pRasterStyle,
                         Bool fIsSeparations);

/** guc_omitDetails gets a pointer to the OMIT_DETAILS structure for a raster
   style. It is used by the separation omission DL walker. */
const struct OMIT_DETAILS *guc_omitDetails(
  /*@notnull@*/                            const GUCR_RASTERSTYLE *pRasterStyle) ;

/** Ignore knockouts by default, but mark all separations for knockouts if set.
    (Used for ContoneMask with HVD). */
void guc_omitSetIgnoreKnockouts(
  /*@notnull@*/                 GUCR_RASTERSTYLE *pRasterStyle,
                                Bool ignore_knockouts);

/** guc_markBlankSeparations modifies the raster/frame/channel/colorant
   structure to remove separations which are blank and marked as omitted when
   blank. */
Bool guc_markBlankSeparations(
  /*@notnull@*/ /*@in@*/      GUCR_RASTERSTYLE *pRasterStyle,
                              Bool rippedtodisk);

/** guc_dontOmitSeparation removes a separation from the omission set. It
   returns a boolean indicating if there are any separations left to omit. */
Bool guc_dontOmitSeparation(
  /*@notnull@*/ /*@in@*/    GUCR_RASTERSTYLE *pRasterStyle,
                            COLORANTINDEX ci);

/** guc_fOmittingSeparation tests if a separation is currently being omitted. */
Bool guc_fOmittingSeparation(
  /*@notnull@*/              const GUCR_RASTERSTYLE *pRasterStyle,
                             COLORANTINDEX ci);

/** guc_saveOmitSeparations copies and saves the omission set for the raster
   handle. guc_restoreOmitSeparations restores the omission set from a handle;
   this is used when examining a color to determine if it is a register mark or
   superblack. It returns a flag indicating if any separations are omitted. */
Bool guc_saveOmitSeparations(
  /*@notnull@*/              GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@out@*/    GUCR_COLORANTSET **hsave) ;

Bool guc_restoreOmitSeparations(
  /*@notnull@*/ /*@in@*/        GUCR_RASTERSTYLE *pRasterStyle,
  /*@notnull@*/ /*@only@*/      GUCR_COLORANTSET *hs,
                                Bool revert) ;

Bool guc_resetOmitSeparations(
  /*@notnull@*/ /*@in@*/      GUCR_RASTERSTYLE *pRasterStyle);

/** Iterate over the colorants in the passed raster style, returning the
highest colorant index used.

If 'includeOmitted' is true, all colorant indices will be checked; otherwise
only the colorants which are not marked as being omitted will be checked.

This function will return -1 if no colorants are present, or no renderable
colorants are present and 'includeOmitted' is false. */
uint32 guc_getHighestColorantIndexInRasterStyle(
  /*@notnull@*/ /*@in@*/                        GUCR_RASTERSTYLE *pRasterStyle,
                                                Bool includeOmitted);

/** guc_setEquivalentColors sets the sRGB and CMYK colors for colorants created
   by dynamic separations */
Bool guc_setEquivalentColors(GS_COLORinfo *colorInfo,
  /*@notnull@*/ /*@in@*/     GUCR_RASTERSTYLE* pRasterStyle,
                             int32 colorType,
                             COLORSPACE_ID colorSpaceId,
                             int32 nColorants,
                             COLORANTINDEX *pColorantIndexes,
                             OBJECT* PSColorSpace,
                             Bool usePSTintTransform);

Bool guc_setCMYKEquivalents(
  /*@notnull@*/ /*@in@*/    GUCR_RASTERSTYLE* pRasterStyle,
                            COLORANTINDEX ci,
  /*@notnull@*/ /*@in@*/    EQUIVCOLOR equiv);

/** guc_getCMYKEquivalents:
   Accessor for the CMYK equivalent values for the given colorant index.
   These CMYK values are used to build a tint transform to map a DeviceN
   to a CMYK alternate color space.  Initially this is for backdrop
   flavoured raster styles.
   *** Note: Assumes a raster style of hRasterStyleBackdrop. ***
 */
NAMECACHE* guc_getCMYKEquivalents(
  /*@notnull@*/                   GUCR_RASTERSTYLE* pRasterStyle,
                                  COLORANTINDEX ci,
  /*@notnull@*/ /*@out@*/         EQUIVCOLOR **equiv,
                                  void *private_data);

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
void guc_setRasterStyleBandSize(
  /*@notnull@*/ /*@in@*/        GUCR_RASTERSTYLE *pRasterStyle,
                                int32 nBandSize);

void guc_positionSeparations(GUCR_RASTERSTYLE *pRasterStyle,
                             Bool fUseRenderOmit );

int32 guc_getMaxOffsetIntoBand(
  /*@notnull@*/                const GUCR_RASTERSTYLE *pRasterStyle);

/** gucs_fullyFledgedColorants returns the fully fledged colorant dictionary. */
OBJECT *gucs_fullyFledgedColorants(
  /*@notnull@*/                    GUCR_RASTERSTYLE *pRasterStyle) ;


const GUCR_PHOTOINK_INFO *guc_photoinkInfo(const GUCR_RASTERSTYLE *pRasterStyle);

/* ---------------------------------------------------------------------- */
/** \brief Create a color mapping for the current render channels, based
    on the current raster style. */
Bool blit_colormap_create(DL_STATE *page, struct blit_colormap_t **map,
                          struct mm_pool_t *pool,
                          const struct surface_t *surface,
                          const GUCR_COLORANT* pColorant,
                          int32 override_depth,
                          /** \todo ajcd 2008-08-25: This is a hack. Object
                              types, alpha, and /All should be integrated
                              into the channel/colorant structures. */
                          Bool append_type,
                          Bool mask, Bool compositing,
                          Bool gather_all_colorants);

/** \brief Destroy an existing color mapping. */
void blit_colormap_destroy(struct blit_colormap_t **map,
                           struct mm_pool_t *pool) ;

/* ----------------------------------------------------------------------
   PostScript operators also implement this functionality, defined in devops.c
   (in addition to extensions to sethalftone and setcolorspace).
   The operators work thus:

        name      removefromseparationcolornames -

        name      addtoseparationcolornames -

        name      addtoseparationorder -
or      name dict addtoseparationorder -

        name      setbackgroundseparation -

These translate into the above calls. name can also be expressed equivalently as a
string (and can be /All in removefromseparationcolornames).

The optional dict parameter to addtoseparationorder takes the following keys

  x               As x in guc_colorantIndexNewFrame; assumed 0 if omitted or no dictionary
  y               As y in guc_colorantIndexNewFrame; assumed 0 if omitted or no dictionary
  Frame           Either a frame number for use as nRelativeToFrame in
                  guc_colorantIndexNewFrame, or a colorant name or string, in which
                  case nRelativeToFrame is derived from it as described above.
  InsertBefore    true implies GUC_FRAMERELATION_BEFORE, false implies
                  GUC_FRAMERELATION_AFTER, absence or no dictionary implies
                  GUC_FRAMERELATION_END if Frame is not also given, or
                  GUC_FRAMERELATION_AT if Frame is given. There is no equivalent of
                  GUC_FRAMERELATION_START.

Therefore, with no dictionary, the named colorant would be added at the
end in position 0,0. This is the same operation as the automatic
addition of a separation on setcolorspace. */

#endif /* __GU_CHAN_H__ */


/* Log stripped */
