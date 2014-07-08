/** \file
 * \ingroup recombine
 *
 * $HopeName: CORErecombine!adjust:src:rcbadjst.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Recombined objects require post-interpretation processing before they can be
 * rendered to the backdrop or straight out on to the page.  This includes
 * shfill decomposition to Gouraud objects; fixing up recombine-intercepted
 * objects in composite jobs; setting the region map according to what regions
 * require composting. Notionally, this is the "Recombining separations"
 * stage.
 */

#include "core.h"
#include "rcbadjst.h"
#include "rcbmapping.h"
#include "debugging.h"
#include "swerrors.h"
#include "swstart.h"
#include "objects.h"
#include "mm.h"
#include "monitor.h"   /* monitorf */
#include "namedef_.h"
#include "tranState.h"
#include "dl_color.h"
#include "display.h"
#include "stacks.h"
#include "gs_color.h"
#include "gschead.h"
#include "gstack.h"
#include "graphics.h"
#include "vndetect.h"
#include "control.h"
#include "gscdevci.h"
#include "gschcms.h"
#include "dl_image.h"
#include "imageadj.h"    /* im_prepareRecombinedImage */
#include "imageo.h" /* IMAGEOBJECT */
#include "imexpand.h" /* im_expandplanefree et. al. */
#include "dlstate.h"
#include "often.h"
#include "gu_chan.h"
#include "gschtone.h"
#include "dl_store.h"
#include "dl_free.h"  /* free_dl_object */
#include "dl_bres.h"
#include "gu_ctm.h"   /* gs_setctm */
#include "swmemory.h" /* gs_cleargstates */
#include "psvm.h"     /* save_() */
#include "hdl.h" /* HDL operations */
#include "shadesetup.h" /* vertex_pool_ */
#include "dl_foral.h"
#include "halftone.h"
#include "shadex.h"
#include "rcbcntrl.h"
#include "cmpprog.h"
#include "rcbshfil.h"
#include "rcbinit.h"
#include "dl_bbox.h"
#include "plotops.h"
#include "swevents.h"
#include "corejob.h"
#include "interrupts.h"
#include "gscxfer.h" /* gsc_setTransfersPreapplied */
#include "imstore.h"

/*----------------------------------------------------------------------------*/
typedef struct rcba_context_t {
  DL_STATE *page ;
  Bool fallsepsdefined ; /**< Determines whether fuzzy matches can be dealt with */
  Bool doingprogress ;   /**< Opened progress/phase. */

  GUCR_RASTERSTYLE* virtualRasterStyle;

  mm_size_t workspacesize ; /**< Size of workspace for colorant indices and values */
  void  *workspace ;        /**< Workspace for colorant indices and values */
} rcba_context_t ;

static rcba_context_t grcba = {
  NULL ,
  FALSE ,
  FALSE ,
  NULL,
  0 ,
  NULL
} ;

typedef struct rcba_color_t {
  int32            colorstyle ;         /**< Style is process, single or multiple spot */
  Bool             foverprint ;         /**< Overprinting or knocking out for this dl color */
  Bool             flobjknockout ;      /**< Knockout flag in the dl object */

  int32            cpcmprocess ;        /**< Count of process colorants in current PCM */
  COLORSPACE_ID    icolorspace ;        /**< Color space of current PCM */
  int32            cprocessseps ;       /**< Count of process separations in job */
  int32            cprocess ;           /**< Count of process colorants in dl color */
  int32            cprocesszero ;       /**< Count of zero process colorants in dl color */
  COLORANTINDEX*   processcolorants ;   /**< Process colorant indices (cpcmprocess many) */
  USERVALUE*       processcolorvalues ; /**< Process colorant values (cpcmprocess many) */

  int32            cspotseps ;          /**< Count of spot separations in job */
  int32            cspot ;              /**< Count of spot colorants in dl color */
  int32            cspotzero ;          /**< Count of zero spot colorants in dl color */
  COLORANTINDEX*   spotcolorants ;      /**< Spot colorant indices (cspot many) */
  USERVALUE*       spotcolorvalues ;    /**< Spot colorant values (cspot many) */

  COLORSPACE_ID    compcolorspace ;     /**< Original PCM of the DL object (for composite only) */
  USERVALUE        compcolorvalues[4] ; /**< Colorvalues for composite DL object (CMYK or RGB) */
} rcba_color_t ;


/** Colors must match exactly one of these styles. */
enum {
  RCBA_UNKNOWN_STYLE = -1 ,
  RCBA_NONE_STYLE = 0 ,
  RCBA_PROCESS_STYLE = 1 ,
  RCBA_SPOTS_STYLE = 2
} ;

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
static Bool grcba_trace_trapped_overprints = FALSE ;

/* Switch on to print information for every object being
   adjusted. Can print the rcbcolors or the dlcolors (or
   both). Warning: only turn this on with small jobs... */
static Bool grcba_dump_rcbacolors = FALSE ;
static Bool grcba_dump_dlcolors = FALSE ;

/* Notify if the spotno has changed */
static Bool grcba_dump_spot = FALSE ;
#endif

/*----------------------------------------------------------------------------*/
#if defined( DEBUG_BUILD )
void debug_print_rcba_color( rcba_color_t *rcbacolor )
{
  int32 i ;

  if ( rcbacolor->foverprint )
    monitorf( (uint8*) "OP " ) ;
  else
    monitorf( (uint8*) "KO " ) ;

  if ( rcbacolor->colorstyle == RCBA_PROCESS_STYLE ) {
    monitorf( (uint8*) "Process " ) ;

    for ( i = 0 ; i < rcbacolor->cpcmprocess ; ++i )
      monitorf( (uint8*) "(%d) %F " , rcbacolor->processcolorants[ i ] ,
                rcbacolor->processcolorvalues[ i ] ) ;
  }
  else {
    monitorf( (uint8*) "Spot(%d) " , rcbacolor->cspot ) ;

    for ( i = 0 ; i < rcbacolor->cspot ; ++i )
      monitorf( (uint8*) "(%d) %F " , rcbacolor->spotcolorants[ i ] ,
                rcbacolor->spotcolorvalues[ i ] ) ;
  }
  monitorf( (uint8*) "\n" ) ;
}

static Bool d_print_char  = TRUE ;
static Bool d_print_fill  = TRUE ;
static Bool d_print_image = TRUE ;
static Bool d_print_other = TRUE ;

void debug_print_rcba_info( rcba_color_t *rcbacolor ,
                            LISTOBJECT *lobj )
{
  switch ( lobj->opcode ) {
  case RENDER_char :
    if ( d_print_char ) {
      monitorf( (uint8*) "Char (0x%X) " , lobj ) ;
      if ( grcba_dump_rcbacolors )
        debug_print_rcba_color( rcbacolor ) ;
      if ( grcba_dump_dlcolors )
        debug_print_ncolor(lobj->p_ncolor) ;
    }
    break ;
  case RENDER_rect :
    HQFAIL( "Got (forbidden) RENDER_rect opcode for recombine object" ) ;
    break ;
  case RENDER_quad:
    HQFAIL( "Got (forbidden) RENDER_quad opcode for recombine object" ) ;
    break ;
  case RENDER_fill :
    if ( d_print_fill ) {
      monitorf( (uint8*) "Fill (0x%X) " , lobj ) ;
      if ( grcba_dump_rcbacolors )
        debug_print_rcba_color( rcbacolor ) ;
      if ( grcba_dump_dlcolors )
        debug_print_ncolor(lobj->p_ncolor) ;
    }
    break ;
  case RENDER_image :
    if ( d_print_image ) {
      IMAGEOBJECT * imageobj ;
      monitorf( (uint8*) "Image (0x%X) " , lobj ) ;
      imageobj = lobj->dldata.image;
      if ( grcba_dump_rcbacolors )
        debug_print_rcba_color( rcbacolor ) ;
      if ( grcba_dump_dlcolors )
        debug_print_ncolor(lobj->p_ncolor) ;
    }
    break ;
  default :
    if ( d_print_other ) {
      monitorf( (uint8*) "Other (0x%X) " , lobj ) ;
      if ( grcba_dump_rcbacolors )
        debug_print_rcba_color( rcbacolor ) ;
      if ( grcba_dump_dlcolors )
        debug_print_ncolor(lobj->p_ncolor) ;
    }
    break ;
  }
}
#endif /* defined( DEBUG_BUILD ) */

/* -------------------------------------------------------------------------- */
static Bool rcba_cpcmprocess( void )
{
  /* Number of colorants in the PCM for recombine adjustment.
   * For jobs that do not contain process seps this value is 0.
   */
  return ( rcbn_cprocessseps() > 0 ? rcbn_ncolorspace() : 0 ) ;
}

/* -------------------------------------------------------------------------- */
static void rcba_color_setup( rcba_color_t *rcbacolor , LISTOBJECT * lobj )
{
  int32 nprocess = rcba_cpcmprocess() ;
  COLORANTINDEX *colorants ;
  USERVALUE *colorvalues ;

  HQASSERT( rcbacolor != NULL , "rcba_color_setup: rcbacolor null" ) ;
  HQASSERT( lobj != NULL , "rcba_color_setup: lobj null" ) ;
  HQASSERT( grcba.workspace != NULL , "rcba_color_setup: workspace null" ) ;
  HQASSERT( grcba.workspacesize ==
           (mm_size_t) (( nprocess + rcbn_cspotseps()) *
                        ( sizeof( COLORANTINDEX ) + sizeof( USERVALUE ))) ,
           "rcba_color_setup: workspace unexpected size" ) ;

  colorants = ( COLORANTINDEX * ) grcba.workspace ;
  colorvalues = ( USERVALUE * )( colorants + nprocess + rcbn_cspotseps()) ;

  rcbacolor->colorstyle = RCBA_UNKNOWN_STYLE ;
  rcbacolor->foverprint = -1 ;
  rcbacolor->flobjknockout = (( lobj->spflags & RENDER_KNOCKOUT ) != 0 ) ;

  rcbacolor->cpcmprocess = nprocess ;
  rcbacolor->icolorspace = rcbn_icolorspace() ;
  rcbacolor->cprocessseps = rcbn_cprocessseps() ;
  rcbacolor->cprocess = 0 ;
  rcbacolor->cprocesszero = 0 ;
  rcbacolor->processcolorants = colorants ;
  rcbacolor->processcolorvalues = colorvalues ;

  /* Gray objects occurring before the job is known to be composite will
     require adjusting. The PCM for these composite objects is determined
     by the last color operator used when they were created (must be one
     of setgray, setrgbcolor or setcmykcolor. See compcolorspace for more */
  HQASSERT( rcbacolor->icolorspace == SPACE_DeviceGray || ! rcbn_composite_page() ,
            "PCM must be gray for a composite job at this point" ) ;
  HQASSERT( rcbacolor->icolorspace == SPACE_DeviceCMYK ||
            rcbn_composite_page() || nprocess == 0 ,
            "PCM must be CMYK for a preseparated job with process seps" ) ;

  rcbacolor->cspotseps = rcbn_cspotseps() ;
  rcbacolor->cspot = 0 ;
  rcbacolor->cspotzero = 0 ;
  rcbacolor->spotcolorants = colorants + nprocess ;
  rcbacolor->spotcolorvalues = colorvalues + nprocess ;

  HQASSERT( lobj->objectstate->lateColorAttrib, "lateColorAttrib is missing" );

  rcbacolor->compcolorspace = SPACE_DeviceGray ;
  if ( lobj->objectstate->lateColorAttrib ) {
    switch (lobj->objectstate->lateColorAttrib->origColorModel) {
    case REPRO_COLOR_MODEL_GRAY:
      rcbacolor->compcolorspace = SPACE_DeviceGray;
      break;
    case REPRO_COLOR_MODEL_RGB:
      rcbacolor->compcolorspace = SPACE_DeviceRGB;
      break;
    case REPRO_COLOR_MODEL_CMYK:
      rcbacolor->compcolorspace = SPACE_DeviceCMYK;
      break;
    case REPRO_COLOR_MODEL_NAMED_COLOR:
      rcbacolor->compcolorspace = SPACE_Separation;
      break;
    case REPRO_COLOR_MODEL_CIE:
    default:
      HQFAIL("Invalid origColorModel");
      /* DROP THROUGH for safety */
      rcbacolor->compcolorspace = SPACE_DeviceGray;
      break;
    }
  }
}

/* -------------------------------------------------------------------------- */
static COLORANTINDEX * rcba_colorants( rcba_color_t *rcbacolor )
{
  HQASSERT( rcbacolor != NULL , "rcba_colorants: rcbacolor null" ) ;
  HQASSERT( rcbacolor->colorstyle != RCBA_UNKNOWN_STYLE ,
            "rcba_colorants: colorstyle not yet setup" ) ;
  return rcbacolor->colorstyle == RCBA_PROCESS_STYLE ?
         rcbacolor->processcolorants : rcbacolor->spotcolorants ;
}

/* -------------------------------------------------------------------------- */
static USERVALUE * rcba_colorvalues( rcba_color_t *rcbacolor )
{
  HQASSERT( rcbacolor != NULL , "rcba_colorvalues: rcbacolor null" ) ;
  HQASSERT( rcbacolor->colorstyle != RCBA_UNKNOWN_STYLE ,
            "rcba_colorvalues: colorstyle not yet setup" ) ;
  return rcbacolor->colorstyle == RCBA_PROCESS_STYLE ?
         rcbacolor->processcolorvalues : rcbacolor->spotcolorvalues ;
}

/* -------------------------------------------------------------------------- */
static int32 rcba_ncolorvalues( rcba_color_t *rcbacolor )
{
  HQASSERT( rcbacolor != NULL , "rcba_ncolorvalues: rcbacolor null" ) ;
  HQASSERT( rcbacolor->colorstyle != RCBA_UNKNOWN_STYLE ,
            "rcba_ncolorvalues: colorstyle not yet setup" ) ;
  return rcbacolor->colorstyle == RCBA_PROCESS_STYLE ?
         rcbacolor->cpcmprocess : rcbacolor->cspot ;
}

/* -------------------------------------------------------------------------- */
static void rcba_process_sep( rcba_color_t *rcbacolor ,
                              COLORANTINDEX ci , COLORVALUE cv ,
                              int32 isep )
{
  USERVALUE cuv ;

  HQASSERT( rcbacolor != NULL , "rcba_process_sep: rcbacolor null" ) ;
  HQASSERT( cv <= COLORVALUE_MAX ,
            "rcba_process_sep: colorant value out of range" ) ;

  cuv = COLORVALUE_TO_USERVALUE(cv) ;

  if ( ci != COLORANTINDEX_NONE ) {
    rcbacolor->cprocess += 1 ;
    if ( cuv == 0.0f )
      rcbacolor->cprocesszero += 1 ;
  }
  rcbacolor->processcolorants[ isep ] = ci ;
  rcbacolor->processcolorvalues[ isep ] = cuv ;
}

/* -------------------------------------------------------------------------- */
static void rcba_spot_sep( rcba_color_t *rcbacolor ,
                           COLORANTINDEX ci , COLORVALUE cv ,
                           int32 isep )
{
  USERVALUE cuv ;

  HQASSERT( rcbacolor != NULL , "rcba_spot_sep: rcbacolor null" ) ;
  HQASSERT( cv <= COLORVALUE_MAX ,
            "rcba_spot_sep: colorant value out of range" ) ;

  cuv = COLORVALUE_TO_USERVALUE(cv) ;

  rcbacolor->cspot += 1 ;
  if ( cuv == 0.0f )
    rcbacolor->cspotzero += 1 ;
  rcbacolor->spotcolorants[ isep ] = ci ;
  rcbacolor->spotcolorvalues[ isep ] = cuv ;
}

/* -------------------------------------------------------------------------- */
static void rcba_preset_seps( dl_color_t *pdlc1 ,
                              rcba_color_t *rcbacolor )
{
  int32 ncolorants ;
  COLORANTINDEX *colorants ;
  USERVALUE *colorvalues ;

  HQASSERT( pdlc1 != NULL , "rcba_preset_seps: pdlc1 null" ) ;
  HQASSERT( rcbacolor != NULL , "rcba_preset_seps: rcbacolor null" ) ;

  colorants = rcba_colorants( rcbacolor ) ;
  colorvalues = rcba_colorvalues( rcbacolor ) ;

  ncolorants = rcba_ncolorvalues( rcbacolor ) ;

  while ((--ncolorants) >= 0 ) {
    COLORANTINDEX ci ;
    USERVALUE cuv ;

    ci = *colorants++ ;
    if ( ci == COLORANTINDEX_NONE ) {
      cuv = 0.0f ;
    }
    else {
      int32 res ;
      COLORVALUE cv ;
      res = dlc_get_indexed_colorant( pdlc1 , ci , & cv ) ;
      HQASSERT( res , "should not have failed to get colorant color" ) ;

      HQASSERT( cv <= COLORVALUE_MAX ,
                "rcba_preset_seps: colorant value out of range" ) ;

      /* dl color contains additive values, convert to subtractive. */
      COLORVALUE_FLIP(cv, cv);

      cuv = COLORVALUE_TO_USERVALUE(cv) ;
    }
    *colorvalues++ = cuv ;
  }
}

/* -------------------------------------------------------------------------- */
static void rcba_process_seps( dl_color_t *pdlc1 ,
                               rcba_color_t *rcbacolor ,
                               uint8 *poverprintprocess )
{
  NAMECACHE **psepnames ;
  int32 cseps ;
  int32 isep ;
  uint8 overprintprocess ;

  HQASSERT( pdlc1 != NULL , "rcba_process_seps: pdlc1 null" ) ;
  HQASSERT( rcbacolor != NULL , "rcba_process_seps: rcbacolor null" ) ;
  HQASSERT( poverprintprocess != NULL , "rcba_process_seps: poverprintprocess null" ) ;

  overprintprocess = *poverprintprocess = 0u;

  psepnames = rcbn_nmcolorspace() ;
  cseps = rcbacolor->cpcmprocess ;
  HQASSERT( cseps > 0 , "rcba_process_seps: cseps <= 0" ) ;

  for ( isep = 0 ; isep < cseps ; ++isep ) {
    NAMECACHE *sepname = psepnames[isep] ;
    COLORANTINDEX ci ;
    COLORVALUE cv ;

    ci = rcbn_nm_ciPseudo( sepname ) ;

    if ( ci != COLORANTINDEX_NONE &&
         dlc_get_indexed_colorant( pdlc1 , ci , & cv )) {
      /* dl color contains additive values, convert to subtractive. */
      COLORVALUE_FLIP(cv, cv);
    } else {
      cv = 0 ;
      ci = COLORANTINDEX_NONE ;

      if ( rcbacolor->icolorspace == SPACE_DeviceCMYK )
        /* Set bit to explicitly overprint this colorant */
        overprintprocess |= ( 1 << isep ) ;
    }

    rcba_process_sep( rcbacolor , ci , cv , isep ) ;
  }

  *poverprintprocess = overprintprocess ;
}

/* -------------------------------------------------------------------------- */
static void rcba_spot_seps( dl_color_t *pdlc1 ,
                            rcba_color_t *rcbacolor )
{
  RCBSEPARATION *sep ;
  int32 isep = 0 ;

  HQASSERT( pdlc1 != NULL , "rcba_spot_seps: pdlc1 null" ) ;
  HQASSERT( rcbacolor != NULL , "rcba_spot_seps: rcbacolor null" ) ;

  sep = rcbn_iterate( NULL ) ;

  while ( sep != NULL ) {

    if ( ! rcbn_sepisprocess( sep )) {

      COLORANTINDEX ci ;
      COLORVALUE cv ;

      ci = rcbn_sepciPseudo( sep ) ;
      HQASSERT( ci != COLORANTINDEX_NONE , "rcba_spot_seps: ci none" ) ;

      if ( dlc_get_indexed_colorant( pdlc1 , ci , & cv )) {
        /* dl color contains additive values, convert to subtractive. */
        COLORVALUE_FLIP(cv, cv);
        rcba_spot_sep( rcbacolor , ci , cv , isep ) ;
        ++isep ;
      }
    }

    sep = rcbn_iterate( sep ) ;
  }
}

/* -------------------------------------------------------------------------- */
static void rcba_convert_to_spots(rcba_color_t *rcbacolor,
                                  Bool fremoveknockouts )
{
  COLORANTINDEX *pcolorants ;
  USERVALUE *pcolorvalues ;
  int32 shift , cseps , isep ;

  /* Removes knockouts from the list and converts a mix of process
     and spot colorants to a multiple spots */

  HQASSERT( rcbacolor != NULL , "rcba_convert_to_spots: rcbacolor null" ) ;
  HQASSERT( ( ! rcbacolor->foverprint &&
            ( rcbacolor->cprocesszero + rcbacolor->cspotzero ) !=
            ( rcbacolor->cprocessseps + rcbacolor->cspotseps )) ||
            ( rcbacolor->cprocess > 0 ) ,
            "Must be removing KOs or have some process colorants" ) ;
  HQASSERT( ( rcbacolor->processcolorants + rcba_cpcmprocess()) == rcbacolor->spotcolorants ,
            "rcba_convert_to_multiplespots: process/spot colorants not contiguous" ) ;
  HQASSERT( rcbacolor->foverprint != -1 , "rcba_convert_to_multiplespots: foverprint unset" ) ;

  shift = 0 ;

  if ( fremoveknockouts || rcbacolor->cprocess != rcbacolor->cpcmprocess ) {

    if ( rcbacolor->cprocess > 0 ) {

      cseps = rcbacolor->cpcmprocess ;
      pcolorants = rcbacolor->processcolorants ;
      pcolorvalues = rcbacolor->processcolorvalues ;

      /* Remove knockouts and any missing process colorants */
      for ( isep = 0 ; isep < cseps ; ++isep ) {

        if ( pcolorants[ isep ] == COLORANTINDEX_NONE) {
          ++shift ;
        }
        else if ( fremoveknockouts && pcolorvalues[ isep ] == 0.0f ) {
          ++shift ;
        }
        else if ( shift > 0 ) {
          pcolorants[ isep - shift ] = pcolorants[ isep ] ;
          pcolorvalues[ isep - shift ] = pcolorvalues[ isep ] ;
        }
      }
    }

    /* Remove knockouts and shift spot seps down to be contiguous again */
    if ( fremoveknockouts || shift > 0 ) {

      cseps = rcbacolor->cspot ;
      pcolorants = rcbacolor->spotcolorants ;
      pcolorvalues = rcbacolor->spotcolorvalues ;

      for ( isep = 0 ; isep < cseps ; ++isep ) {
        if ( fremoveknockouts && pcolorvalues[ isep ] == 0.0f ) {
          ++shift ;
        } else if ( shift > 0 ) {
          /* May assign into now unused process slots */
          pcolorants[ isep - shift ] = pcolorants[ isep ] ;
          pcolorvalues[ isep - shift ] = pcolorvalues[ isep ] ;
        }
      }
    }
  }

  rcbacolor->cspot += rcbacolor->cpcmprocess - shift ;
  rcbacolor->cspotzero =
    ( fremoveknockouts
      ? 0 : rcbacolor->cprocesszero + rcbacolor->cspotzero ) ;

  if ( rcbacolor->cprocess > 0 ) {
    rcbacolor->spotcolorants = rcbacolor->processcolorants ;
    rcbacolor->spotcolorvalues = rcbacolor->processcolorvalues ;
  }

  rcbacolor->cprocess = 0 ;
  rcbacolor->cprocesszero = 0 ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_colorsetup_process( rcba_color_t *rcbacolor ,
                                     uint8 overprintprocess ,
                                     int32 colortype)
{
  COLORSPACE_ID icolorspace = rcbacolor->icolorspace ;

  HQASSERT( rcbacolor != NULL , "rcba_colorsetup_process: rcbacolor null" ) ;
  HQASSERT( rcbacolor->colorstyle == RCBA_PROCESS_STYLE ,
            "rcba_colorsetup_process: Not dealing with a process color" ) ;
  HQASSERT( rcbacolor->foverprint != -1 , "rcba_colorsetup_process: foverprint unset" ) ;

  if ( ! gsc_setcolorspacedirect( gstateptr->colorInfo , colortype , icolorspace ))
    return FALSE ;

  if (! gsc_setoverprint( gstateptr->colorInfo , colortype , rcbacolor->foverprint ))
    return FALSE ;

  if (! gsc_setoverprintprocess( gstateptr->colorInfo , colortype , overprintprocess ))
    return FALSE ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_colorsetup_spots( rcba_color_t *rcbacolor ,
                                   int32 colortype ,
                                   LISTOBJECT *lobj )
{
  OBJECT *cspace ;
  int32 i;
  LateColorAttrib *lca = lobj->objectstate->lateColorAttrib;

  HQASSERT( rcbacolor != NULL , "rcba_colorsetup_spots: rcbacolor null" ) ;
  HQASSERT( rcbacolor->cprocess == rcbacolor->cprocesszero ,
            "rcba_colorsetup_spots: must have either no process or all zero" ) ;
  HQASSERT( rcbacolor->colorstyle == RCBA_SPOTS_STYLE ,
            "rcba_colorsetup_spots: Not dealing with a spots color" ) ;
  HQASSERT( rcbacolor->foverprint != -1 , "rcba_colorsetup_spots: foverprint unset" ) ;
  HQASSERT( lobj != NULL , "rcba_colorsetup_spots: lobj is NULL" ) ;
  /* The call chain to this function indicates that it is only called from
     rcba_shfill() */
  HQASSERT(lobj->opcode != RENDER_image, "Image object in recombined shfill") ;

  if (rcbn_composite_page() && lca != NULL &&
      lca->origColorModel == REPRO_COLOR_MODEL_NAMED_COLOR) {
    /* The job is composite but this object was recombine-intercepted as a
       Separation Black color and requires special handling to undo the
       recombine interception.  The other kinds of gray/black can be undone
       using simple process color spaces. */
    COLORANTINDEX ciBlack = guc_colorantIndex(grcba.virtualRasterStyle,
                                              system_names + NAME_Black);

    if (ciBlack == COLORANTINDEX_UNKNOWN) {
      HQFAIL("Black should have been added to this raster style already");
      return error_handler(UNREGISTERED);
    }

    cspace = gsc_spacecache_getcolorspace(gstateptr->colorInfo,
                                          grcba.virtualRasterStyle,
                                          1, & ciBlack,
                                          guc_getCMYKEquivalents, NULL) ;
    if ( ! cspace )
      return FALSE ;

  } else {

    for (i = 0; i < rcbacolor->cspot; ++i)
      rcbacolor->spotcolorants[i] = rcbn_ciActual(rcbacolor->spotcolorants[i]);

    cspace = gsc_spacecache_getcolorspace(gstateptr->colorInfo,
                                          grcba.virtualRasterStyle,
                                          rcbacolor->cspot ,
                                          rcbacolor->spotcolorants ,
                                          guc_getCMYKEquivalents, NULL) ;
    if (cspace == NULL)
      return FALSE ;

    for (i = 0; i < rcbacolor->cspot; ++i)
      rcbacolor->spotcolorants[i] = rcbn_ciPseudo(rcbacolor->spotcolorants[i]);
  }

  if ( ! gsc_setcustomcolorspacedirect( gstateptr->colorInfo , colortype ,
                                        cspace ,
                                        FALSE /* fCompositing */))
    return FALSE ;

  if (! gsc_setoverprint( gstateptr->colorInfo , colortype ,
                          rcbacolor->foverprint ))
    return FALSE ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static void rcba_selectprocesscolorspace( rcba_color_t *rcbacolor ,
                                          int32 colortype )
{
  HQASSERT( rcbacolor != NULL , "rcba_selectprocesscolorspace: rcbacolor null" ) ;

  /* Got a Gray overprinted image object in CMYK mode, so switch to that. */
  if ( colortype == GSC_IMAGE &&
       rcbacolor->foverprint &&
       rcbacolor->cpcmprocess == 4 &&
       rcbacolor->cprocess == 1 &&
       rcbacolor->processcolorants[ 3 ] != COLORANTINDEX_NONE ) {
    rcbacolor->cpcmprocess = 1 ;
    rcbacolor->icolorspace = SPACE_DeviceGray ;
    rcbacolor->processcolorants[ 0 ] =
      rcbacolor->processcolorants[ 3 ] ;
    rcbacolor->processcolorvalues[ 0 ] =
      rcbacolor->processcolorvalues[ 3 ] ;
  }

  /* Use the original process colorspace to ensure overprints etc are correct */
  if ( rcbn_composite_page()) {
    switch ( rcbacolor->compcolorspace ) {
    case SPACE_DeviceCMYK :
      rcbacolor->cpcmprocess = 4 ;
      rcbacolor->icolorspace = SPACE_DeviceCMYK ;
      rcbacolor->compcolorvalues[ 0 ] =
      rcbacolor->compcolorvalues[ 1 ] =
      rcbacolor->compcolorvalues[ 2 ] = 0.0f ;
      rcbacolor->compcolorvalues[ 3 ] = rcbacolor->processcolorvalues[ 0 ] ;
      /* process colorants not required when going to process color space */
      rcbacolor->processcolorvalues = rcbacolor->compcolorvalues ;
      break ;
    case SPACE_DeviceRGB :
      rcbacolor->cpcmprocess = 3 ;
      rcbacolor->icolorspace = SPACE_DeviceRGB ;
      rcbacolor->compcolorvalues[ 0 ] =
      rcbacolor->compcolorvalues[ 1 ] =
      rcbacolor->compcolorvalues[ 2 ] = 1.0f - rcbacolor->processcolorvalues[ 0 ] ;
      /* process colorants not required when going to process color space */
      rcbacolor->processcolorvalues = rcbacolor->compcolorvalues ;
      break ;
    case SPACE_Separation :
      /* Nothing here.  We'll force it through the spot route later. */
      break ;
    case SPACE_DeviceGray :
      rcbacolor->processcolorvalues[ 0 ] =
        1.0f - rcbacolor->processcolorvalues[ 0 ] ;
      break ;
    default :
      HQFAIL( "rcbacolor->compcolorspace must be gray, cmyk or rgb" ) ;
    }
  }
}

/* -------------------------------------------------------------------------- */
static Bool rcba_try_process_style(rcba_color_t *rcbacolor ,
                                   int32         colortype )
{
  HQASSERT( rcbacolor != NULL , "rcba_try_process_style: rcbacolor NULL" ) ;

  if ( rcbacolor->cprocess == 0 )
    return FALSE ;

  if ( rcbn_composite_page() && rcbacolor->compcolorspace == SPACE_Separation )
    return FALSE ;

  if ( rcbacolor->cprocess != rcbacolor->cpcmprocess ) {
    if ( colortype == GSC_IMAGE || colortype == GSC_SHFILL ||
         colortype == GSC_VIGNETTE )
      /* Must have the full set of process colorants for these object types */
      return FALSE ;
  }

  if ( rcbacolor->foverprint ) {
    /* Object is overprinting */

    /* Spot colorants of any value imply this is not a process color! */
    if ( rcbacolor->cspot != 0 )
      return FALSE ;

    /* Need to be careful with objects that have missing process seps and
       have some white values.

       C M Y K
           0 0 => DeviceN(Y,K)
           X X => DeviceCMYK
       0 0 0 0 => DeviceCMYK (Result depends on OverprintWhite)
       X X X X => DeviceCMYK

       Note, test against cprocessseps not cpcmprocess so a job with only
       CMY seps that has an object C=0, M=0, Y=0 will go to DeviceCMYK.
     */
    if ( rcbacolor->cprocess < rcbacolor->cprocessseps &&
         rcbacolor->cprocesszero > 0 )
      return FALSE ;

  } else {
    /* Object is knocking out */

    /* Not a process color if some of the spots are non-zero
       (if the spots are all zeros, the spots are just knockouts) */
    if ( rcbacolor->cspotzero != rcbacolor->cspotseps )
      return FALSE ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static int32 rcba_selectcolorstyle(rcba_color_t *rcbacolor, int32 colortype)
{
  HQASSERT( rcbacolor != NULL , "rcba_selectcolorstyle: rcbacolor null" ) ;
  HQASSERT( rcbacolor->colorstyle == RCBA_UNKNOWN_STYLE ,
            "rcba_selectcolorstyle: colorstyle already set" ) ;
  HQASSERT( rcbacolor->foverprint == -1 , "rcba_selectcolorstyle: foverprint not -1" ) ;
  HQASSERT( colortype == GSC_FILL   ||
            colortype == GSC_STROKE ||
            colortype == GSC_IMAGE  ||
            colortype == GSC_SHFILL || /* Note: not GSC_SHFILL_INDEXED_BASE */
            colortype == GSC_VIGNETTE ,
            "invalid colortype parameter" ) ;

  /* Only knockout when color contains all process and spot seps
     or if composite according to the lobj knockout flag */
  rcbacolor->foverprint =
    ( rcbn_composite_page()
      ? ! rcbacolor->flobjknockout
      : (( rcbacolor->cprocessseps + rcbacolor->cspotseps ) !=
         ( rcbacolor->cprocess + rcbacolor->cspot ))) ;

  if ( rcbacolor->cprocess == 0 && rcbacolor->cspot == 0 ) {

    rcbacolor->colorstyle = RCBA_NONE_STYLE ;

  } else if ( rcba_try_process_style(rcbacolor, colortype) ) {

    /* Process only (excluding any knockouts in the spot seps) */

    rcbacolor->colorstyle = RCBA_PROCESS_STYLE ;

    rcba_selectprocesscolorspace( rcbacolor , colortype ) ;

  } else {

    /* For any combination of process and spot colorants */

    Bool fremoveknockouts ;

    rcbacolor->colorstyle = RCBA_SPOTS_STYLE ;

    /* Do not remove knockouts when object completely white */
    fremoveknockouts =
      ( ! rcbacolor->foverprint &&
        ( rcbacolor->cprocesszero + rcbacolor->cspotzero ) > 0 &&
        ( rcbacolor->cprocesszero + rcbacolor->cspotzero ) !=
        ( rcbacolor->cprocessseps + rcbacolor->cspotseps )) ;

    /* Color contains knockouts, must be removed, or
       have some process seps, convert to a DeviceN */
    if ( fremoveknockouts || rcbacolor->cprocess > 0 )
      rcba_convert_to_spots(rcbacolor, fremoveknockouts) ;
  }

  return rcbacolor->colorstyle ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_dl_color( dl_color_t *pdlc ,
                           LISTOBJECT *lobj ,
                           rcba_color_t *rcbacolor ,
                           int32 colortype )
{
  int32 colorstyle ;
  uint8 overprintprocess = 0;

  HQASSERT( pdlc != NULL , "rcba_dl_color: pdlc null" ) ;
  HQASSERT( lobj != NULL , "rcba_dl_color: lobj null" ) ;
  HQASSERT( rcbacolor != NULL , "rcba_dl_color: rcbacolor null" ) ;

  colorstyle = rcbacolor->colorstyle ;
  if ( colorstyle == RCBA_UNKNOWN_STYLE ) {
    if ( rcbacolor->cprocessseps > 0 )
      rcba_process_seps(pdlc, rcbacolor, &overprintprocess) ;

    if ( rcbacolor->cspotseps > 0 )
      rcba_spot_seps(pdlc, rcbacolor) ;

    colorstyle = rcba_selectcolorstyle(rcbacolor, colortype) ;
  }
  else {
    /* The color style is only preset when we are color adjusting a vignette,
     * when we want to apply the same color style across all the sub objects
     * of the vignette.
     */
    rcba_preset_seps( pdlc , rcbacolor ) ;
  }

  switch ( colorstyle ) {
  case RCBA_NONE_STYLE :
    /* This case typically only arises when we get part way through a page and
     * end up with an error, or there are marks drawn after the final showpage.
     * In both cases we see a dl color with no recognizeable colorants in it.
     */
    dlc_clear( pdlc ) ;
    dlc_get_none(grcba.page->dlc_context, pdlc) ;
    return TRUE ;

  case RCBA_PROCESS_STYLE :
    if ( ! rcba_colorsetup_process(rcbacolor, overprintprocess, colortype) )
      return FALSE ;

    break ;
  case RCBA_SPOTS_STYLE :
    if ( ! rcba_colorsetup_spots( rcbacolor , colortype , lobj ))
      return FALSE ;

    break ;
  default :
    HQFAIL( "rcba_dl_color: unrecognised recombine color style" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  /* Obtain the final recombined dl color */
  if ( ! gsc_setcolordirect( gstateptr->colorInfo , colortype ,
                             rcba_colorvalues( rcbacolor )) ||
       ! gsc_invokeChainSingle( gstateptr->colorInfo , colortype ))
    return FALSE ;

  /* Overprint flag may have changed owing to 100% black overprinting */
  rcbacolor->foverprint = (dl_currentspflags(grcba.page->dlc_context)
                           & RENDER_KNOCKOUT) == 0 ;

  /* Use the color that has not been overprint reduced */
  dlc_copy_release(grcba.page->dlc_context, pdlc,
                   dlc_currentcolor(grcba.page->dlc_context)) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_fuzzy_overprints( rcba_color_t *rcbacolor ,
                                   p_ncolor_t *ppnc_color ,
                                   p_ncolor_t *ppnc_overprints)
{
  dl_color_t        dlc_color ;
  dl_color_t        dlc_overprints ;
  dl_color_iter_t   dlci ;
  dlc_iter_result_t iter_res ;
  COLORANTINDEX     ci ;
  COLORVALUE        cv , cvt ;

  HQASSERT( rcbacolor != NULL , "rcba_fuzzy_overprints: rcbacolor null" ) ;
  HQASSERT( ppnc_color != NULL , "rcba_fuzzy_overprints: ppnc_color null" ) ;
  HQASSERT( ppnc_overprints != NULL , "rcba_fuzzy_overprints: ppnc_overprints null" ) ;

  dlc_from_dl_weak( *ppnc_color , & dlc_color ) ;           /* device colorants */
  dlc_from_dl_weak( *ppnc_overprints , & dlc_overprints ) ; /* presep colorants */

  HQASSERT( DLC_TINT_OTHER == dlc_check_black_white( & dlc_overprints ) ,
            "rcba_fuzzy_overprints: dlc_overprints must be DLC_TINT_OTHER" ) ;

  iter_res = dlc_first_colorant( & dlc_overprints , & dlci , & ci , & cv ) ;

  while ( iter_res == DLC_ITER_COLORANT ) {
    NAMECACHE *nm ;

    /* dl color contains additive values, convert to subtractive. */
    COLORVALUE_FLIP(cv, cv);

    nm = rcbn_nmActual( ci ) ;
    if ( nm != NULL ) {
      COLORANTINDEX cit ;
      COLORANTINDEX *cis = NULL ;
      COLORANTINDEX cis2[ 2 ] ;
      /* See if the plugin (or other code) has set up any mappings. These may be
       * added either internally (by the PhotoInk detection code) or by a plugin.
       * If not, then as we were before; a single colorant.
       */
      cit = rcbn_ciActual( ci ) ;
      HQASSERT( cit != COLORANTINDEX_ALL &&
                cit != COLORANTINDEX_NONE &&
                cit != COLORANTINDEX_UNKNOWN ,
                "rcba_fuzzy_overprints: cit either all, none or unknown" ) ;
      cis = guc_getColorantMapping( gsc_getRS(gstateptr->colorInfo) , cit ) ;
      if ( cis == NULL ) {
        cis = cis2 ;
        cis2[ 0 ] = cit ;
        cis2[ 1 ] = COLORANTINDEX_UNKNOWN ;
      }
      else /* NULL nm as an alternative test for if the colorant is being produced. */
        nm = NULL ;

      while (( ci = *cis++ ) != COLORANTINDEX_UNKNOWN ) {
        HQASSERT( ci != COLORANTINDEX_ALL &&
                  ci != COLORANTINDEX_NONE ,
                  "rcba_fuzzy_overprints: ci either none or unknown" ) ;
        /* If fuzzy matched color is a knockout (aka 0) then we always maxblt it.
         * Otherwise we remove it (which for an overprinted object means just that,
         * and for a knockout object means add a minimum value maxblt.
         */
        if ( cv != 0 && rcbacolor->foverprint ) {
          /* Overprinting object, therefore simply need to remove colorant
           * if exists.
           */
          if ( dlc_set_indexed_colorant( & dlc_color , ci )) {
            if ( !dlc_remove_colorant(grcba.page->dlc_context, &dlc_color, ci) )
              return FALSE ;
          }
        }
        else {
          /* Knocking out object, therefore need to set fuzzy matched colorants
           * to maxblt overprint to avoid producing knockouts for the colorants.
           * Instead the contribution for fuzzy matched colorants will come from
           * another dl object that has been fuzzy matched with this one.
           */
          Bool fMaxBlt = FALSE ;
          if ( ! dlc_set_indexed_colorant( & dlc_color , ci )) {
            /* Check that we are actually going to render this colorant. */
            if ( ( nm == NULL ||
                   guc_colorantIndex( gsc_getRS(gstateptr->colorInfo) , nm ) !=
                                      COLORANTINDEX_UNKNOWN ) &&
                  ! rcbacolor->foverprint ) {
              /* Must add colorant to dlc_color before overprints can be set */
              Bool result ;
              dl_color_t dlc_temp ;
              dlc_clear( & dlc_temp ) ;

              fMaxBlt = TRUE ;

              /* Set cv to the maxtones value */
              cvt = COLORVALUE_MAX ;
              result = dlc_alloc_fillin(grcba.page->dlc_context, 1, &ci, &cvt, &dlc_temp) &&
                       dlc_merge(grcba.page->dlc_context, &dlc_color, &dlc_temp) ;
              dlc_release(grcba.page->dlc_context, &dlc_temp) ;
              if ( ! result )
                return FALSE ;
            }
          }
          else {
            fMaxBlt = TRUE ;

            if ( cv != 0 ) {
              /* Set cv to the maxtones value */
              cvt = COLORVALUE_MAX ;
              if ( !dlc_replace_indexed_colorant(grcba.page->dlc_context,
                                                 &dlc_color, ci, cvt) )
                return FALSE ;
            }
          }

          /* Set the fuzzy matched colorant to overprint */
          if ( fMaxBlt ) {
            if ( !dlc_apply_overprints(grcba.page->dlc_context,
                                       DLC_MAXBLT_OVERPRINTS, DLC_UNION_OP,
                                       1, &ci, &dlc_color) )
              return FALSE ;
          }
        }
      }
    }
    iter_res = dlc_next_colorant( & dlc_overprints , & dlci , & ci , & cv ) ;
  }

  dlc_to_dl_weak( ppnc_color , & dlc_color ) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_ncolor( p_ncolor_t *ppnc_color ,
                         p_ncolor_t *ppnc_overprints ,
                         LISTOBJECT *lobj ,
                         rcba_color_t *rcbacolor ,
                         int32 colortype )
{
  dl_color_t dlc ;

  HQASSERT( ppnc_color != NULL , "rcba_ncolor: ppnc_color null" ) ;
  HQASSERT( ppnc_overprints != NULL , "rcba_ncolor: ppnc_overprints null" ) ;
  HQASSERT( lobj != NULL , "rcba_ncolor: lobj null" ) ;
  HQASSERT( rcbacolor != NULL , "rcba_ncolor: rcbacolor null" ) ;

  dlc_from_dl_weak( *ppnc_color , & dlc ) ;

  if ( ! rcba_dl_color( & dlc , lobj , rcbacolor , colortype ))
    return FALSE ;

  dl_release(grcba.page->dlc_context, ppnc_color) ;
  dlc_to_dl_weak( ppnc_color , & dlc ) ;

#if defined( DEBUG_BUILD )
  {
    static Bool op_removedlobjects = FALSE ;
    /* op_removedlobjects=TRUE: show only overprinted objects */
    if ( op_removedlobjects ) {
      if ( ! rcbacolor->foverprint ) {
        dl_to_none(grcba.page->dlc_context, ppnc_color) ;
        return TRUE ;
      }
    }
  }
#endif

  HQTRACE( grcba_trace_trapped_overprints &&
           *ppnc_overprints != NULL &&
           ! rcbacolor->foverprint &&
           ! grcba.fallsepsdefined ,
           ( "Recombine adjust: Not all seps in job are defined by the device, "
             "may not overprint trapped object correctly" )) ;

  return
    ( *ppnc_overprints == NULL ||
      rcba_fuzzy_overprints(rcbacolor, ppnc_color, ppnc_overprints) ) ;
}

/*----------------------------------------------------------------------------*/
static Bool rcba_dlobj_setup( LISTOBJECT *lobj , int32 colortype )
{
  uint8 reproType ;

  HQASSERT( lobj != NULL , "rcba_dlobj_setup: lobj null" ) ;

  theITrapIntent( gstateptr ) = (int8)((lobj->spflags & RENDER_UNTRAPPED) == 0);

  /* Update the CRD selection if the repro type of the dl object
     differs from the gstate */
  reproType = DISPOSITION_REPRO_TYPE(lobj->disposition) ;
  HQASSERT(reproType < REPRO_N_TYPES,
           "Object does not have a valid repro type") ;

  if (!gsc_setRequiredReproType(gstateptr->colorInfo, colortype, reproType))
    return FALSE;

  if ( !gsc_setSpotno(gstateptr->colorInfo, lobj->objectstate->spotno) )
    return FALSE;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_dlobj_spotno(LISTOBJECT *lobj)
{
  SPOTNO spotno = gsc_getSpotno(gstateptr->colorInfo);

  if ( lobj->objectstate->spotno != spotno ) {
    STATEOBJECT newstate;

#if defined( DEBUG_BUILD )
    if ( grcba_dump_spot ) {
      monitorf((uint8 *)"Spotno change from %d to %d\n",
               lobj->objectstate->spotno, spotno) ;
    }
#endif
    newstate = *lobj->objectstate ;
    newstate.spotno = spotno ;
    lobj->objectstate = (STATEOBJECT*)dlSSInsert(grcba.page->stores.state,
                                                 &newstate.storeEntry, TRUE);
    if ( lobj->objectstate == NULL )
      return FALSE;
  }

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
/* Shfill vignettes need reconstructing from the stored patches. To do this,
   we remove the DL from this vignette object, and divert output to another
   vignette object. We then walk the current DL, expanding patches into
   their full DL representation, and passing on blend extension fills. */

static Bool rcba_shfill_gstate(LISTOBJECT *lobj)
{
  CLIPOBJECT *clipstate = lobj->objectstate->clipstate ;

  degenerateClipping = FALSE;

  /* Setup clip rectangle */
  cclip_bbox = clipstate->bounds;

  /* Setup DL state. We must retain the current gstag structure because the
   * one in the listobject may not match, and the data with which it was
   * originally created may have been restored away. This mismatch will be
   * corrected when the generated sub-objects are added to the shfill DL.
   */
  if ( grcba.page->currentdlstate == NULL ) {
    STATEOBJECT newstate;

    newstate = *lobj->objectstate ;
    newstate.gstagstructure = gstateptr->theGSTAGinfo.structure ;

    grcba.page->currentdlstate =
      (STATEOBJECT*)dlSSInsert(grcba.page->stores.state, &newstate.storeEntry,
                               TRUE);
    if ( grcba.page->currentdlstate == NULL )
      return FALSE;
  }

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
/* Prototype required for shfills and vignettes */
static Bool rcba_shfill_background(/*@notnull@*/ /*@in@*/ LISTOBJECT *lobj,
                                   int32 colortype,
                                   rcba_color_t *rcbacolor) ;

/*----------------------------------------------------------------------------*/
static Bool rcba_shfill_subdl(/*@in@*/ /*@notnull@*/ HDL *hdl,
                              int32 colortype,
                              rcba_color_t *rcbacolor,
                              SHADINGinfo *sinfo)
{
  DLREF *links ;

  HQASSERT(hdl, "No shfill sub-DL") ;

  while ( (links = hdlOrderList(hdl)) != NULL ) {
    LISTOBJECT *patch_lobj = dlref_lobj(links);

    HQASSERT(patch_lobj, "No patch object on shfill sub-DL") ;
    HQASSERT(patch_lobj->opcode == RENDER_fill ||
             patch_lobj->opcode == RENDER_shfill_patch,
             "Unexpected patch object opcode") ;

    /* Remove links to this object from start of HDL list */
    hdlRemoveFirstObject(hdl) ;

    if ( patch_lobj->opcode == RENDER_shfill_patch ) {
      rcbs_patch_t *patch = patch_lobj->dldata.patch;

      switch ( rcbs_patch_type(patch) ) {
      case 1: /* function based */
        if ( !rcbs_adjust_function(patch, sinfo) )
          return FALSE ;
        break ;
      case 2: /* axial blend */
        if ( !rcbs_adjust_axial(patch, sinfo) )
          return FALSE ;
        break ;
      case 3: /* radial blend */
        if ( !rcbs_adjust_radial(patch, sinfo) )
          return FALSE ;
        break ;
      case 4: case 5: /* gourauds */
        if ( !rcbs_adjust_gouraud(patch, sinfo, rcba_colorants(rcbacolor)) )
          return FALSE ;
        break ;
      case 6: case 7: /* Coons & Tensor mesh */
        if ( !rcbs_adjust_tensor(patch, sinfo, rcba_colorants(rcbacolor)) )
          return FALSE ;
        break ;
      default:
        HQFAIL("Unexpected or unimplemented shfill patch type") ;
      }

      free_dl_object(patch_lobj, grcba.page) ;

      updateDLProgressTotal( 1.0, RECOMBINE_PROGRESS ) ;
    }
    else { /* Background fill */
      dbbox_t bbox ;
      CLIPOBJECT *clipobject;
      Bool clipped, added;

      HQASSERT(patch_lobj->opcode == RENDER_fill,
               "Recombine shfill sub-dl should only have RENDER_shfill_patch or RENDER_fill") ;

      /* Get bbox of fill */
      clipobject = patch_lobj->objectstate->clipstate;

      bbox_nfill(patch_lobj->dldata.nfill,
                 &clipobject->bounds, &bbox, &clipped);

      /* Color adjust each element. Also updates progress total. */
      if ( ! rcba_shfill_background(patch_lobj, colortype, rcbacolor) )
        return FALSE ;

      if ( !add_listobject(sinfo->page, patch_lobj, &added) )
        return FALSE;
      if ( added )
        if ( !rcba_dlobj_spotno(patch_lobj) )
          return FALSE;
    }
  }
  return TRUE;
}

/*----------------------------------------------------------------------------*/
Bool rcba_shfill(HDL *parentHdl, LISTOBJECT *lobj, int32 colortype)
{
  Bool result = FALSE ;
  int32 gid ;
  uint8 overprintprocess = 0 ;
  SHADINGinfo *sinfo ;
  SHADINGOBJECT *sobj ;
  OMATRIX *matrix ;
  dl_color_t dlc ;
  rcba_color_t rcbacolor ;
  HDL *subHdl ;

  HQASSERT( lobj != NULL , "Invalid LISTOBJECT parameter" ) ;
  HQASSERT(lobj->opcode == RENDER_shfill, "Object should be shaded fill") ;

  if ( !rcba_dlobj_setup(lobj, colortype) )
    return FALSE ;

  subHdl = lobj->dldata.shade->hdl ;
  HQASSERT(subHdl, "No HDL in recombined shfill object") ;
  sobj = lobj->dldata.shade;
  HQASSERT(sobj, "No stored shading object") ;
  sinfo = sobj->info;
  HQASSERT(sinfo, "No stored shading info") ;
  matrix = (OMATRIX *)(sinfo + 1) ;

  /* Construct colorspace and install in gstate */

  dlc_from_dl_weak(lobj->p_ncolor, &dlc) ;

  rcba_color_setup(&rcbacolor, lobj) ;

  if ( rcbacolor.cprocessseps > 0 )
    rcba_process_seps(&dlc, &rcbacolor, &overprintprocess) ;

  if ( rcbacolor.cspotseps > 0 )
    rcba_spot_seps(&dlc, &rcbacolor) ;

  ( void )rcba_selectcolorstyle(&rcbacolor, colortype) ;

  switch ( rcbacolor.colorstyle ) {
  case RCBA_NONE_STYLE :
    /* This case typically only arises when we get part way through a page and
     * end up with an error, or there are marks drawn after the final showpage.
     * In both cases we see a dl color with no recognizeable colorants in it.
     */
    HQFAIL("Don't know what to do, can I return safely?") ;

    return TRUE ;

  case RCBA_PROCESS_STYLE :
    if ( ! rcba_colorsetup_process(&rcbacolor, overprintprocess, colortype) )
      return FALSE ;

    break ;
  case RCBA_SPOTS_STYLE :
    if ( ! rcba_colorsetup_spots( &rcbacolor , colortype , lobj ))
      return FALSE ;

    break ;
  default :
    HQFAIL( "unrecognised recombine color style" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  /* Obtain the final recombined dl color (needed in case no other colour is
     set up for axial blends, before calling gsc_applyblockoverprints). */
  if ( ! gsc_setcolordirect( gstateptr->colorInfo , colortype ,
                             rcba_colorvalues( &rcbacolor )) ||
       ! gsc_invokeChainSingle( gstateptr->colorInfo , colortype ))
    return FALSE ;

  /* We may have more colorvalues than anticipated because of overprints */
  sinfo->ncolors = rcba_ncolorvalues(&rcbacolor) ;

#if defined( ASSERT_BUILD )
  {
    Bool presep;
    (void)gsc_isPreseparationChain(gstateptr->colorInfo, colortype, &presep);
    HQASSERT(!presep, "Do not expect a presep chain here");
  }
#endif
  sinfo->preseparated = FALSE;

  if ( sinfo->rfuncs ) {
    if ( !rcbs_adjust_fn_order(sinfo, rcba_colorants(&rcbacolor)) )
      return FALSE ;
  } else { /* No functions, include all colorants in components */
    sinfo->ncomps = sinfo->ncolors ;
  }

  /* Save gstate for matrix manipulation */
  if ( !gs_gpush( GST_SHADING ))
    return FALSE ;
#define return DO_NOT_RETURN_-_SET_result_INSTEAD!

  gid = gstackptr->gId ;

  if ( gs_gpush( GST_GSAVE ) &&
       gs_setctm(matrix, FALSE) &&
       rcba_shfill_gstate(lobj) ) {
    USERVALUE scratch4[4] ;

    theFlatness(theILineStyle(gstateptr)) = sinfo->rflat ;

    HQASSERT(sinfo->ncolors == gsc_dimensions(gstateptr->colorInfo, sinfo->base_index),
             "Number of separations combined not equal to colorspace dimension") ;

    /* Setup scratch pad */
    sinfo->scratch = scratch4 ;
    if ( sinfo->ncolors <= 4 ||
         (sinfo->scratch = (USERVALUE *)mm_alloc(mm_pool_temp,
                                                 sizeof(USERVALUE) * sinfo->ncolors,
                                                 MM_ALLOC_CLASS_SHADING))
         != NULL ) {
      /** \todo ajcd 2009-02-10: The user_label usage is probably wrong, it
          gives the user label at the end of the page, rather than at the
          time the recombined objects were created. */
      DISPOSITION_STORE(dl_currentdisposition, REPRO_TYPE_VIGNETTE, colortype,
                        gstateptr->user_label ? DISPOSITION_FLAG_USER : 0);

      if ( vertex_pool_create(sinfo->ncomps) ) {
        dbbox_t bbox ;
        HDL *currentHdl = grcba.page->currentHdl ;

        /* Restore the page's HDL context while we re-create the shfill HDL,
           so that the hdlOpen in setup_shfill_dl will use the same band
           range as the original HDL. */
        grcba.page->currentHdl = parentHdl ;

        hdlBBox(subHdl, &bbox);
        if ( !setup_shfill_dl(sinfo->page, lobj) ) {
          result = FALSE;
        } else {
          /* Now move down chain, either moving object to new chain or
           * expanding patch
           */
          result = rcba_shfill_subdl(subHdl, colortype, &rcbacolor, sinfo);
          result = reset_shfill_dl(sinfo->page, sinfo, result, sobj->mbands,
                                   sobj->noise, sobj->noisesize, &bbox);
        }

        grcba.page->currentHdl = currentHdl ;

        vertex_pool_destroy() ;

        if ( result ) {
          /* No sub-object produced if bbox is empty; assign none color */
          if ( bbox_is_empty(&bbox) ) {
            dl_color_t none ;

            /* Force object not to be displayed */
            dlc_clear(&none) ;
            dlc_get_none(grcba.page->dlc_context, &none) ;

            dlc_to_lobj_release(lobj, &none) ;
          } else {
            Range extent = hdlExtentOnParent(lobj->dldata.shade->hdl) ;
            Range oldextent = hdlExtentOnParent(subHdl) ;

            /* It is possible that on expansion, we have a different size of
               bbox than we originally estimated, and therefore touch a
               smaller number of bands. We have to adjust the bands in which
               the original object appears to compensate. */
            HQASSERT(oldextent.origin <= extent.origin &&
                     rangeTop(oldextent) >= rangeTop(extent),
                     "BBox of shfill HDL has increased on recombine expansion") ;
            if ( oldextent.origin != extent.origin ||
                 oldextent.length != extent.length )
              result = hdlAdjustBandRange(parentHdl, lobj, oldextent, extent) ;
          }
        }
      }

      /* Destroy array of colorant indices */
      if ( sinfo->scratch != scratch4 ) {
        mm_free(mm_pool_temp, (mm_addr_t)sinfo->scratch,
                sizeof(USERVALUE) * sinfo->ncolors) ;
      }
    } else {
      result = error_handler(VMERROR) ;
    }
  }

  if ( !gs_cleargstates(gid, GST_SHADING, NULL))
    result = FALSE ;

  if ( !rcba_dlobj_spotno(lobj) )
    result = FALSE ;

#if defined( DEBUG_BUILD )
  if ( grcba_dump_rcbacolors || grcba_dump_dlcolors )
    debug_print_rcba_info( & rcbacolor , lobj ) ;
#endif

  hdlDestroy(&subHdl) ;

#undef return
  return result ;
}

/*----------------------------------------------------------------------------*/
#if defined( DEBUG_BUILD )
/* Enables one to easily set a break point on a small number of DL objects. */
LISTOBJECT *debug_lobj_break1 = NULL ;
LISTOBJECT *debug_lobj_break2 = NULL ;
LISTOBJECT *debug_lobj_break3 = NULL ;
LISTOBJECT *debug_lobj_break4 = NULL ;
#endif

/*----------------------------------------------------------------------------*/
static Bool rcba_shfill_background(/*@notnull@*/ /*@in@*/ LISTOBJECT *lobj,
                                   int32 colortype,
                                   rcba_color_t *rcbacolor)
{
  HQASSERT( lobj != NULL , "rcba_dlobj: lobj null" ) ;

  if ( ! interrupts_clear(allow_interrupt))
    return report_interrupt(allow_interrupt) ;

#if defined( DEBUG_BUILD )
  if ( lobj == debug_lobj_break1 ||
       lobj == debug_lobj_break2 ||
       lobj == debug_lobj_break3 ||
       lobj == debug_lobj_break4 )
    EMPTY_STATEMENT() ;
#endif

  /* Clear the recombine flag to adjust this dl object only once */
  lobj->spflags &= (~RENDER_RECOMBINE) ;

  /* dl object with none dl colors are just ignored */
  if ( ! dl_is_none( lobj->p_ncolor )) {
    rcba_color_t tmp_rcbacolor ;

    HQASSERT(lobj->opcode == RENDER_fill,   /* Possible background */
             "Shfill background object is not a fill") ;

    /* Setup everything required for this dl object */
    if ( ! rcba_dlobj_setup( lobj , colortype ))
      return FALSE ;

    if ( rcbacolor == NULL ) {
      rcbacolor = & tmp_rcbacolor ;
      rcba_color_setup( rcbacolor , lobj ) ;
    }

    if ( ! rcba_ncolor( & lobj->p_ncolor , &lobj->attr.planes, lobj ,
                        rcbacolor , colortype ))
      return FALSE ;

    if ( ! dl_is_none( lobj->p_ncolor )) {
      if ( rcbacolor->foverprint )
        lobj->spflags &= ~RENDER_KNOCKOUT ;
      else
        lobj->spflags |= RENDER_KNOCKOUT ;

      if ( !rcba_dlobj_spotno(lobj) )
        return FALSE ;

#if defined( DEBUG_BUILD )
      if ( grcba_dump_rcbacolors || grcba_dump_dlcolors )
        debug_print_rcba_info( rcbacolor , lobj ) ;
#endif
    }
  }

  dl_release(grcba.page->dlc_context, &lobj->attr.planes);
  lobj->attr.rollover = DL_NO_ROLL ;

  updateDLProgressTotal( 1.0, RECOMBINE_PROGRESS ) ;

  return TRUE ;
}

/*----------------------------------------------------------------------------*/
Bool rcba_lobj(LISTOBJECT *lobj, int32 colortype)
{

  uint8 overprintprocess = 0 ;
  dl_color_t dlc ;
  rcba_color_t rcbacolor ;

  HQASSERT( lobj != NULL , "Invalid LISTOBJECT parameter" ) ;
  HQASSERT(lobj->opcode != RENDER_shfill &&
           lobj->opcode != RENDER_image,
           "Object should be not shaded fill or image") ;

  if ( !rcba_dlobj_setup(lobj, colortype) )
    return FALSE ;

  /* Construct colorspace and install in gstate */

  dlc_from_dl_weak(lobj->p_ncolor, &dlc) ;

  rcba_color_setup(&rcbacolor, lobj) ;

  if ( rcbacolor.cprocessseps > 0 )
    rcba_process_seps(&dlc, &rcbacolor, &overprintprocess) ;

  if ( rcbacolor.cspotseps > 0 )
    rcba_spot_seps(&dlc, &rcbacolor) ;

  ( void )rcba_selectcolorstyle(&rcbacolor, colortype) ;

  switch ( rcbacolor.colorstyle ) {
  case RCBA_NONE_STYLE :
    /* This case typically only arises when we get part way through a page and
     * end up with an error, or there are marks drawn after the final showpage.
     * In both cases we see a dl color with no recognizeable colorants in it.
     */
    HQFAIL("Don't know what to do, can I return safely?") ;

    return TRUE ;

  case RCBA_PROCESS_STYLE :
    if ( ! rcba_colorsetup_process(&rcbacolor, overprintprocess, colortype) )
      return FALSE ;

    break ;
  case RCBA_SPOTS_STYLE :
    if ( ! rcba_colorsetup_spots( &rcbacolor , colortype , lobj ))
      return FALSE ;

    break ;
  default :
    HQFAIL( "unrecognised recombine color style" ) ;
    return error_handler( UNREGISTERED ) ;
  }

  /* Obtain the final recombined dl color (needed in case no other colour is
     set up for axial blends, before calling gsc_applyblockoverprints). */
  if ( ! gsc_setcolordirect( gstateptr->colorInfo , colortype ,
                             rcba_colorvalues( &rcbacolor )) ||
       ! gsc_invokeChainSingle( gstateptr->colorInfo , colortype ))
    return FALSE ;

  if ( !rcba_dlobj_spotno(lobj) )
    return FALSE ;

  dl_release(grcba.page->dlc_context, &lobj->p_ncolor) ;
  dlc_to_lobj_release(lobj, dlc_currentcolor(grcba.page->dlc_context)) ;

  /* DL color no longer contains presep pseudo colorants. */
  lobj->spflags &= ~RENDER_RECOMBINE ;

#if defined( DEBUG_BUILD )
  if ( grcba_dump_rcbacolors || grcba_dump_dlcolors )
    debug_print_rcba_info( & rcbacolor , lobj ) ;
#endif

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_define_all_seps(void)
{
  RCBSEPARATION *sep ;
  NAMECACHE *sepname ;

  /* Can only do in job trapped objects correctly when all seps are defined, ie
     separation colorants are a subset of the device colorants */

  grcba.fallsepsdefined = FALSE ;

  /* Should not call rcba_define_all_seps when we get a composite page
   * (as then the only 'seps' defined is a single one called "Composite",
   * which we then iterate over, whereupon guc_addAutomaticSeparation adds
   * it to the list of seps to produce).
   */
  HQASSERT( ! rcbn_composite_page() ,
            "should not call rcba_define_all_seps on composite page" ) ;

  sep = rcbn_iterate( NULL ) ;

  while ( sep != NULL ) {

    sepname = rcbn_sepnmActual( sep ) ;
    HQASSERT( sepname != NULL , "rcba_define_all_seps: sepname null" ) ;

    if ( ! guc_addAutomaticSeparation( gsc_getRS(gstateptr->colorInfo) ,
                                       sepname, FALSE ) )
      return FALSE ;

    if ( guc_colorantIndex( gsc_getRS(gstateptr->colorInfo) , sepname )
         == COLORANTINDEX_UNKNOWN )
      return TRUE ; /* Separation not defined by device */

    sep = rcbn_iterate( sep ) ;
  }

  grcba.fallsepsdefined = TRUE ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_begin( DL_STATE *page, GUCR_RASTERSTYLE* virtualRasterStyle,
                        int32 *gId )
{
  HQASSERT( grcba.workspacesize == 0 , "rcba_begin: workspacesize not 0" ) ;
  HQASSERT( grcba.workspace == NULL , "rcba_begin: workspace not null" ) ;

  *gId = GS_INVALID_GID ;
  if ( !gs_gpush(GST_GSAVE) )
    return FALSE ;
  *gId = gstackptr->gId ;

  grcba.page = page ;

  HQASSERT(grcba.virtualRasterStyle == NULL,
           "Previous virtualRasterStyle not discarded");
  guc_reserveRasterStyle(virtualRasterStyle) ;
  grcba.virtualRasterStyle = virtualRasterStyle ;

  /* Must set grcba.fallsepsdefined to deal with composite input jobs when
   * the routine rcba_define_all_seps is not called. A useful default value is
   * FALSE in this case (see code at end of routine rcba_ncolor).
   */
  grcba.fallsepsdefined = FALSE ;

  HQASSERT(!rcbn_intercepting(), "Expected recombine interception to be disabled");

  /* Allocate buffers for colorants indices and values */
  grcba.workspacesize = sizeof( COLORANTINDEX ) + sizeof ( USERVALUE ) ;
  grcba.workspacesize *= rcba_cpcmprocess() + rcbn_cspotseps() ;

  grcba.workspace = ( void * )
    mm_alloc( mm_pool_temp , grcba.workspacesize , MM_ALLOC_CLASS_RCB_ADJUST ) ;

  if ( grcba.workspace == NULL )
    return error_handler(VMERROR) ;

  /* Report if starting recombine and open progress device */
  if ( ! rcbn_composite_page()) {
    /* Affects whether in job trapped objects can be overprinted correctly,
     * as overprinting in inherently device dependent. Should not be called
     * when we've got a composite input job.
     */
    if ( ! rcba_define_all_seps())
      return FALSE ;

    openDLProgress(page, RECOMBINE_PROGRESS) ;

    grcba.doingprogress = TRUE ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rcba_end(Bool result, int32 gId)
{
  if ( grcba.doingprogress ) {
    updateDLProgress(RECOMBINE_PROGRESS);

    if ( ! rcbn_composite_page()) {
      closeDLProgress(grcba.page, RECOMBINE_PROGRESS);
    }

    grcba.doingprogress = FALSE ;
  }

  grcba.page = NULL ;

  /* No longer doing dl color adjust, explicit reset now */
  if ( grcba.virtualRasterStyle != NULL )
    guc_discardRasterStyle(&grcba.virtualRasterStyle) ;

  if ( grcba.workspace != NULL ) {
    mm_free( mm_pool_temp , grcba.workspace , grcba.workspacesize ) ;
    grcba.workspace = NULL ;
  }
  else
    HQASSERT(grcba.workspacesize == 0, "workspacesize should be zero");
  grcba.workspacesize = 0 ;

  if ( result )
    rcbn_set_recombine_object( 0 ) ;

  if ( gId != GS_INVALID_GID && !gs_cleargstates(gId, GST_GSAVE, NULL) )
    result = FALSE ;

  return result ;
}

/*----------------------------------------------------------------------------*/
static Bool rcba_remove_colorant(DL_STATE *page, LISTOBJECT *object,
                                 COLORANTINDEX ci)
{
  dl_color_t dlColor;

  /* Remove ci from the main dl color. */
  dlc_from_dl_weak(object->p_ncolor, & dlColor);
  if (dlc_set_indexed_colorant(& dlColor, ci)) {
    HQASSERT(object->opcode != RENDER_shfill,
             "Need to remove colorant from all shfill colors") ;
    if ( !dl_remove_colorant(grcba.page->dlc_context, &object->p_ncolor, ci) )
      return FALSE;
    if (object->opcode == RENDER_image) {
      IMAGEOBJECT* image = object->dldata.image;
      int32 plane = im_ci2plane(image->ime, ci) ;
      if ( plane >= 0 ) {
        im_planefree(image->ims, plane);
        im_expandplanefree(page, image->ime, plane);
      } else {
        HQFAIL("Colorant index was not present in image planes") ;
      }
    }
  }

  /* Remove ci from the fuzzy match dl color. */
  if ( object->attr.planes != NULL ) {
    dlc_from_dl_weak(object->attr.planes, & dlColor);
    if (dlc_set_indexed_colorant(& dlColor, ci)) {
      HQASSERT(object->opcode != RENDER_shfill && object->opcode != RENDER_image,
               "Don't expect fuzzy matched shfills or images") ;
      if ( !dl_remove_colorant(grcba.page->dlc_context, &object->attr.planes, ci) )
        return FALSE;
    }
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
typedef struct {
  int32 nSeparations;
  COLORANTINDEX *rcbmap; /* Mapping from pseudo to actual colorant index */
  int32 maplength;
  GS_COLORinfo *colorInfo;
  GUCR_RASTERSTYLE *inputRasterStyle;
  Bool savedOverprintBlack;
  dl_color_t dlc_fullset;
} RecombineData;

static Bool rcba_prepare_lobj(DL_FORALL_INFO *info)
{
  RecombineData* callbackdata = info->data;
  LISTOBJECT *object = info->lobj;
  LateColorAttrib *lca = object->objectstate->lateColorAttrib;
  dl_color_t dlColor;
  dbbox_t bbox;
  COLORANTINDEX ci;
  GS_COLORinfo *colorInfo = callbackdata->colorInfo;
  GUCR_RASTERSTYLE *inputRasterStyle = callbackdata->inputRasterStyle;
  int32 colorType;

  HQASSERT((info->reason & DL_FORALL_NONE) == 0 &&
           !dl_is_none(object->p_ncolor),
           "DL callback object should not be None colour");

  HQASSERT((object->marker & MARKER_DEVICECOLOR) == 0,
           "Do not expect real device codes here");

  /* Update the progress dial before any early returns. */
  if ((object->spflags & RENDER_RECOMBINE) != 0) {
    updateDLProgressTotal(1.0, RECOMBINE_PROGRESS);
    SwOftenUnsafe();
  }

  dlobj_bbox(object, & bbox);

#if defined ( DEBUG_BUILD )
  {
    /* Useful debug code for inspecting colors and screens etc. */
    static uint8 debug_opcode = N_RENDER_OPCODES;
    static Bool debug_recombine = FALSE;
    static Bool debug_composite = FALSE;
    static COLORANTINDEX debug_screenci = COLORANTINDEX_NONE;
    static dbbox_t bboxFocus = { MINDCOORD, MINDCOORD, MAXDCOORD, MAXDCOORD };
                                            /* i.e., interested in everything */

    /* Skip (and do not render) objects that do not intersect the focus area. */
    if ( (info->reason & DL_FORALL_SHFILL) == 0 ) {
      if ( !bbox_intersects(&bbox, &bboxFocus) ) {
        dl_to_none(grcba.page->dlc_context, &object->p_ncolor);
        return TRUE;
      }
    }

    /* Print out useful information about the remaining objects. */
    if (debug_opcode == object->opcode ||
        debug_opcode == N_RENDER_OPCODES /* ie, all opcodes */) {
      Bool recombined = ((object->spflags & RENDER_RECOMBINE) != 0);
      if ((recombined && debug_recombine) ||
          (! recombined && debug_composite)) {
        monitorf((uint8*)"%s: (0x%X), opcode: %d, spotno: %d",
                 (recombined ? "Recombine" : "Composite"),
                 object, object->opcode, object->objectstate->spotno);
        if (! recombined)
          monitorf((uint8*)", %s",
                   ((object->spflags & RENDER_KNOCKOUT) != 0
                    ? "Knocking out" : "Overprinting"));
        if (debug_screenci != COLORANTINDEX_NONE)
          ht_print_screen(object->objectstate->spotno,
                          DISPOSITION_REPRO_TYPE(object->disposition),
                          debug_screenci);
        monitorf((uint8*)"\n");
        monitorf((uint8*)"bbox (x1 x2 y1 y2) %d %d %d %d\n",
                 bbox.x1, bbox.x2, bbox.y1, bbox.y2);
        debug_print_ncolor(object->p_ncolor);
        monitorf((uint8*)"\n");
      }
    }
  }
#endif

  if (object->opcode == RENDER_erase ||
      object->opcode == RENDER_shfill_patch)
    return TRUE;

  colorType = DISPOSITION_COLORTYPE(object->disposition);

  /* Ignore objects which have passed through a normal composite chain. */
  if ((object->spflags & RENDER_RECOMBINE) == 0)
    return TRUE;

  /* When recombining, a repeated separation (part of the next page)
     signals a showpage, but a few objects from the repeated separation
     may already have been encountered and added to the DL for the current
     page.  The dl colors of these objects contain an extra invalid pseudo
     colorant from the aborted repeated separation.  The extra pseudo
     colorant is simply removed and ignored (as in Recombine1). */
  ci = rcbn_aborted_colorant();
  if (ci != COLORANTINDEX_NONE) {
    if ( !rcba_remove_colorant(info->page, object, ci) )
      return FALSE;
  }
  /* Similarly, remove any pseudo colorants that are no longer being tracked
     (from dummy separations in Illustrator jobs). */
  for ( ci = 0; ci < callbackdata->maplength; ++ci ) {
    if ( callbackdata->rcbmap[ci] == COLORANTINDEX_UNKNOWN ) {
      if ( !rcba_remove_colorant(info->page, object, ci) )
        return FALSE;
    }
  }

  dlc_from_dl_weak(object->p_ncolor, & dlColor);

  /* If the job is composite then no further overprint work is required
     (the presep chain has set the object's overprint flag already).
     If the job is preseparated and this object has been recombined then
     the object only KO's if it is present in every separation. */
  if (! rcbn_composite_page()) {
    int32 nComps = dl_num_channels(object->p_ncolor);

    if (nComps < callbackdata->nSeparations)
      /* Object not present in every separation, must be overprinting.
         Must do this regardless of Overprint system param otherwise
         will not be able to recombine properly. */
      object->spflags &= ~RENDER_KNOCKOUT;
    else
      object->spflags |= RENDER_KNOCKOUT;
  }

  if (object->attr.planes != NULL) {
    /* A Quark pretrapped object, remove fuzzy matched planes.  This is
       done *after* overprint handling to avoid unnecessarily setting
       regions to backdrop render for virtual colorants, but allowing
       overprint reduction.  The pair of objects may together paint all
       channels, but the channels are split between the two objects (ie,
       individually they overprint). */
    dl_color_t dlcFuzzy;
    dl_color_iter_t iterator;
    dlc_iter_result_t more;
    COLORVALUE cv;

    HQASSERT(object->opcode != RENDER_image,
             "Cannot trap match recombine pretrapped images");

    dlc_from_dl_weak(object->attr.planes, & dlcFuzzy);

    more = dlc_first_colorant(& dlcFuzzy, & iterator, & ci, & cv);
    while (more == DLC_ITER_COLORANT) {
      if ( !dl_remove_colorant(grcba.page->dlc_context, &object->p_ncolor, ci) )
        return FALSE;
      more = dlc_next_colorant(& dlcFuzzy, & iterator, & ci, & cv);
    }

    dl_release(grcba.page->dlc_context, &object->attr.planes);

    /* Individually, pretrapped objects always overprint. */
    object->spflags &= ~RENDER_KNOCKOUT;
  }

  /* Set the group's inputRasterStyle as the target. Required for virtual
     colorants and also converting preseparated shfill patches to
     Gouraud shfills. */
  gsc_setTargetRS(colorInfo, inputRasterStyle);

  /* Make sure the object uses the appropriate spotno in any subsequent
     conversion. */
  if ( !gsc_setSpotno(colorInfo, object->objectstate->spotno) )
    return FALSE;

  if (rcbn_composite_page()) {
    /* Implicit overprinting is normally applied in the front-end chain only,
       the exception is recombining a composite job, when implicit overprint is
       applied late for gray objects. */
    Bool overprintMode = FALSE;
    Bool overprintBlack = FALSE;

    if ( lca )
      overprintMode = lca->overprintMode;

    /** \todo @@@ TODO FIXME Cannot allow implicit overprinting on vignettes
       until we have code to ensure colorants are uniform through the
       vignette. */
    if ( object->opcode == RENDER_vignette ||
         (info->reason & DL_FORALL_SHFILL) != 0 ) {
      overprintMode = FALSE;
      overprintBlack = FALSE;
    }
    else {
      /* Put the OverprintBlack user param back to its original setting.
         OverprintBlack has not yet been done on this object because it went
         through recombine interception. */
      overprintBlack = callbackdata->savedOverprintBlack;
    }

    HQASSERT(!gsc_getignoreoverprintmode(colorInfo), "Bad overprintmode");
    if ( !gsc_setoverprintmode(colorInfo, overprintMode) ||
         !gsc_setoverprintblack(colorInfo, overprintBlack) )
      return FALSE ;
  }

  if ( object->opcode == RENDER_image ) {
    IMAGEOBJECT* image = object->dldata.image;

    /* Remap pseudo colorants to normal colorants. */
    if ( !im_expandrecombined(image->ime, grcba.page,
                              callbackdata->rcbmap, callbackdata->maplength) ||
         !dl_remap_colorants(grcba.page->dlc_context, object->p_ncolor,
                             &object->p_ncolor, callbackdata->rcbmap,
                             callbackdata->maplength) )
      return FALSE ;

    /* Check if we need KOs in the process colorants (cf. gray->CMYK front-end
       chain in the no recombine-interception case). */
    if ( rcbn_composite_page() &&
         !gsc_getoverprintgrayimages(colorInfo) &&
         lca != NULL && lca->origColorModel == REPRO_COLOR_MODEL_GRAY ) {
      /* Image was originally specified in a DeviceGray colorspace. */
      COLORSPACE_ID pcm;
      guc_deviceToColorSpaceId(inputRasterStyle, & pcm);
      if (pcm == SPACE_DeviceCMYK)
        image->flags |= IM_FLAG_PRESEP_KO_PROCESS;
    }
  }
  else if ( object->opcode == RENDER_shfill ) {
    /* Decompose shfill patch into gourauds (includes colorant remapping). */
    if ( !rcba_shfill(info->hdl, object, colorType) )
      return FALSE;
  }
  else if ( rcbn_composite_page() ) {
    /* A composite job with recombine enabled. */
    if ( object->opcode != RENDER_hdl ) {
      /* Revert intercepted objects back to their original input color space,
         which could be gray, rgb or cmyk.  This is necessary to get the same
         overprinting behaviour as if the object had not been intercepted. */
      if ( !rcba_lobj(object, colorType) )
        return FALSE;
    } else {
      /* HDLs may contain a mixture of pseudo and normal colorants (reserved in
         the same rasterstyle) and therefore rcba_lobj() cannot be applied.
         Remapping can't be done for composite jobs because the pseudo colorant
         may be expanded to rgb or cmyk for any object in the HDL.  Instead,
         replace the HDL color with a dl color made containing the full set of
         normal colorants.  This dl color shouldn't have any significant effect
         on separation omission or rendering. */
      dl_release(grcba.page->dlc_context, &object->p_ncolor);
      if ( !dlc_to_dl(grcba.page->dlc_context, &object->p_ncolor,
                      &callbackdata->dlc_fullset) )
        return FALSE;
    }
  }
  else {
    /* Remap pseudo colorants to normal colorants. */
    if ( !dl_remap_colorants(grcba.page->dlc_context, object->p_ncolor,
                             &object->p_ncolor, callbackdata->rcbmap,
                             callbackdata->maplength) )
      return FALSE;
  }

  /* DL color no longer contains pseudo colorants. */
  object->spflags &= ~RENDER_RECOMBINE;

  /* HDL wants to know if any of its contents overprints. */
  if ( (object->spflags & RENDER_KNOCKOUT) == 0 )
    hdlSetOverprint(info->hdl);

  return TRUE;
}

/**
 * Preprocess the DL before rendering or compositing.  Do shfill decomposition
 * to Gouraud objects; fix up recombine-intercepted objects in composite jobs.
 */
Bool rcba_prepare_dl(Group *pageGroup, Bool savedOverprintBlack)
{
  Bool success = TRUE;
  GUCR_RASTERSTYLE *inputRasterStyle = groupInputRasterStyle(pageGroup);
  RecombineData callbackdata = {0};
  DL_FORALL_INFO info;
  COLORANTINDEX ciGray;
  int32 gId = GS_INVALID_GID;

  HQASSERT(groupGetUsage(pageGroup) == GroupPage,
           "pageGroup is not actually a page group");

  callbackdata.nSeparations = rcbn_cseps();
  callbackdata.inputRasterStyle = inputRasterStyle;
  callbackdata.savedOverprintBlack = savedOverprintBlack;
  dlc_clear(&callbackdata.dlc_fullset);

  /* Gray always needs to be present in case the job turns out to be composite.
     Some gray objects may have been sent through the presep chain and they will
     require special handling. */
  ciGray = guc_colorantIndex(inputRasterStyle, system_names + NAME_Black);
  if (ciGray == COLORANTINDEX_UNKNOWN)
    ciGray = guc_colorantIndex(inputRasterStyle, system_names + NAME_Gray);
  if ( ciGray == COLORANTINDEX_UNKNOWN ) {
    HQFAIL("Failed to find Gray or equivalent colorant present");
    return error_handler(UNREGISTERED);
  }

  if ( !rcbn_mapping_create(&callbackdata.rcbmap, &callbackdata.maplength,
                            ciGray) )
    return FALSE ;
#define return DO_NOT_RETURN_SET_success_INSTEAD

  if ( !rcba_begin(groupPage(pageGroup), inputRasterStyle, &gId) )
    success = FALSE;
  /* Always call rcbn_end to handle a partial constructed rcba_begin */

  /* Updating the dl color for HDL lobjs is tricky when the job turns out to
     be composite.  So cop out and make a dl color with the full set of
     colorants instead.  A color value of a half is used arbitrarily but should
     not affect separation omission or rendering significantly. */
  /* if ( rcbn_composite_page() ) */ /** \todo just use the dlc_fullset for
                                         hdlColor() below, but maybe it should
                                         use dl_remap_colorants() instead. */
  if ( !dlc_from_rs(grcba.page,inputRasterStyle, &callbackdata.dlc_fullset,
                    COLORVALUE_HALF) )
      success = FALSE;

  /* after rcba_begin and gsave */
  callbackdata.colorInfo = gstateptr->colorInfo;

  debug_display_ht_table(groupPage(pageGroup)->eraseno);

  /* Set the transparency state to default when doing preconverting to
     ensure devicecode c-link and other places which test on this do not
     have unexpected behaviour (it's unsafe to leave it floating at
     whatever it was set to last).  The recombine prepare stage uses the
     colorInfo in the gstate as recombine prepare does conversion to
     inputRasterStyle from the pseudo colorants. */
  tsDefault(&gstateptr->tranState, gstateptr->colorInfo);

  /* Implicit overprinting is normally applied in the front-end chain only, the
     exception is recombining a composite job, when implicit overprint is
     applied late for gray objects by rcba_prepare_dl.
     NB. Although there are other overprint params, turning off these three is
         sufficient to turn off all implicit overprinting. */
  if ( !gsc_setignoreoverprintmode(gstateptr->colorInfo, FALSE) ||
       !gsc_setoverprintmode(gstateptr->colorInfo, FALSE) ||
       !gsc_setoverprintblack(gstateptr->colorInfo, FALSE) )
    success = FALSE ;

  /* Transfer functions are applied in the frontend and therefore we don't want
     to apply them again when doing recombine adjustment or compositing. */
  if ( !gsc_setTransfersPreapplied(gstateptr->colorInfo, TRUE) )
    success = FALSE;

  /* Prepare all the recombined objects for compositing. */
  info.page    = groupPage(pageGroup);
  info.hdl     = groupHdl(pageGroup);
  info.data    = &callbackdata;
  info.inflags = DL_FORALL_USEMARKER|DL_FORALL_PATTERN|DL_FORALL_SOFTMASK|
                 DL_FORALL_SHFILL|DL_FORALL_GROUP;
  if ( !dl_forall(&info, rcba_prepare_lobj) )
    success = FALSE;

  if ( success ) {
    dl_color_t *dlc_merged = hdlColor(info.hdl);
    dlc_release(grcba.page->dlc_context, dlc_merged);
    if ( !dlc_copy(grcba.page->dlc_context, dlc_merged,
                   &callbackdata.dlc_fullset) )
      success = FALSE;
  }

  rcbn_mapping_free(&callbackdata.rcbmap, callbackdata.maplength) ;

  if ( !dlc_is_clear(&callbackdata.dlc_fullset) )
    dlc_release(grcba.page->dlc_context, &callbackdata.dlc_fullset);

  if ( !rcba_end(success, gId) )
    success = FALSE;

#undef return
  return success;
}

/*----------------------------------------------------------------------------*/
static void init_C_globals_rcbadjst(void)
{
  rcba_context_t grcba_init = {
    NULL ,
    FALSE ,
    FALSE ,
    NULL,
    0 ,
    NULL
  } ;
  grcba = grcba_init;
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  grcba_trace_trapped_overprints = FALSE ;
  grcba_dump_rcbacolors = FALSE ;
  grcba_dump_dlcolors = FALSE ;
  grcba_dump_spot = FALSE ;
#endif
#if defined( DEBUG_BUILD )
  d_print_char  = TRUE ;
  d_print_fill  = TRUE ;
  d_print_image = TRUE ;
  d_print_other = TRUE ;
  debug_lobj_break1 = NULL ;
  debug_lobj_break2 = NULL ;
  debug_lobj_break3 = NULL ;
  debug_lobj_break4 = NULL ;
#endif
}

IMPORT_INIT_C_GLOBALS( rcbshfil )

void rcbn_adjust_C_globals(struct core_init_fns *fns)
{
  UNUSED_PARAM(struct core_init_fns *, fns) ;

  init_C_globals_rcbadjst() ;
  init_C_globals_rcbshfil() ;
}

/* Log stripped */
