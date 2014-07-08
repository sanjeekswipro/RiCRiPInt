/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:spdetect.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1995-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Separation detection module
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "dicthash.h"
#include "dictscan.h"
#include "fileio.h"
#include "namedef_.h"

#include "mathfunc.h" /* NORMALISE_ANGLE */
#include "constant.h" /* EPSILON */
#include "params.h"
#include "matrix.h"
#include "bitblts.h"
#include "display.h"
#include "graphics.h"
#include "gstate.h"
#include "stacks.h"
#include "render.h"
#include "dlstate.h"
#include "psvm.h"
#include "control.h"
#include "halftone.h" /* ht_set_page_default */

#include "spdetect.h"
#include "statops.h"
#include "plotops.h"

#include "gschtone.h"
#include "gu_chan.h"

#include "swmemory.h"
#include "vndetect.h"

#include "rcbcntrl.h"

#include "pcmnames.h"


Bool new_color_detected = TRUE ;
Bool new_screen_detected = FALSE ;

typedef struct _sep_method {
  uint8 *name ;
  int32  priority ;
} SEP_METHOD ;

#if defined( ASSERT_BUILD )
static int32 debug_separation = FALSE ;
#endif

#define SEP_METHOD_APPLICATION  (&sep_methods[0])
#define SEP_METHOD_SEPARATION   (&sep_methods[1])
#define SEP_METHOD_SETCOLOR     (&sep_methods[2])
#define SEP_METHOD_SETCMYKCOL0R (&sep_methods[3])
#define SEP_METHOD_ANGLE        (&sep_methods[4])
#define SEP_METHOD_NOMETHOD     (&sep_methods[5])

#define ADJUST_SCREEN_YES       TRUE
#define ADJUST_SCREEN_NO        FALSE

#define ADJUST_GRAY_YES         TRUE
#define ADJUST_GRAY_NO          FALSE

static Bool adjust_gray_separation( GS_COLORinfo *colorInfo ) ;
static Bool ignore_detection_method(SEP_METHOD *sep_method, Bool ignore_same_level) ;
static Bool store_separation( SEP_METHOD *sep_method ,
                              NAMECACHE *sepname ,
                              int32 adjustscreen , int32 adjustgray ,
                              GS_COLORinfo *colorInfo ) ;
static Bool reiterate_setsystemparam_separation( GS_COLORinfo *colorInfo ) ;
static void reset_separation_detection_globals(void) ;

/* Don't the capitalised ones since they are used from PS. */
static SEP_METHOD sep_methods[] =
{
  { (uint8 *)"Application"  , 5 } ,
  { (uint8 *)"Separation"   , 4 } ,
  { (uint8 *)"setcolor"     , 3 } ,
  { (uint8 *)"setcmykcolor" , 2 } ,
  { (uint8 *)"setscreen"    , 1 } ,
  /* Must be last */
  { NULL                    , 0 }
} ;

uint8 *primary_colors [] = {
  (uint8 *) "Cyan",
  (uint8 *) "Magenta",
  (uint8 *) "Yellow",
  (uint8 *) "Black"
};

/* Used to store the current pages sep detected */
static SEP_METHOD *pge_detected = NULL ;
/* Used to store the current separations sep detected */
static SEP_METHOD *sep_detected = NULL ;

static Bool multiple_separation_detected = FALSE ;

/* Used to store the current pages color */
static NAMECACHE *pge_colorname = NULL ;
/* Used to store the current separations color */
static NAMECACHE *sep_colorname = NULL ;

/* Used to store the current detected color. */
static NAMECACHE *sep_detectedcolorname = system_names + NAME_Unknown ;

/* Used to store the current pages adjusted gray name */
static NAMECACHE *pge_adjusted_gray = NULL ;
/* Used to store the current separations adjusted gray name */
static NAMECACHE *sep_adjusted_gray = NULL ;

/* Separation detection based on the /Separation systemparam should last
 * until the next showpage, copypage (LL3), or EOJ.  [22312]
 */
static NAMECACHE * sep_sysparamname = NULL ;  /* the systemparam sepname */


/* Non-zero means seperation detection is disabled. */
static uint32 spd_level = 0u ;

void init_C_globals_spdetect(void)
{
  new_color_detected = TRUE ;
  new_screen_detected = FALSE ;
#if defined( ASSERT_BUILD )
  debug_separation = FALSE ;
#endif
  pge_detected = NULL ;
  sep_detected = NULL ;
  multiple_separation_detected = FALSE ;
  pge_colorname = NULL ;
  sep_colorname = NULL ;
  sep_detectedcolorname = system_names + NAME_Unknown ;
  pge_adjusted_gray = NULL ;
  sep_adjusted_gray = NULL ;
  sep_sysparamname = NULL ;
  spd_level = 0u ;
}

void enable_separation_detection( void )
{
  HQASSERT(IS_INTERPRETER(), "Separation detection outside interpretation");
  ++spd_level ;
}

void disable_separation_detection( void )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );
  --spd_level ;
}

NAMECACHE *get_separation_name(
  Bool f_ignore_disable)
{
  corecontext_t *context = get_core_context_interp();
  SEP_METHOD *sep_d = sep_detected ;

  if ( !context->systemparams->DetectSeparation ||
       (!f_ignore_disable && spd_level != 0) ||
       sep_d == NULL ) {
    return system_names + NAME_Unknown ;
  }
  else {
    return sep_detectedcolorname ;
  }
}

void get_separation_method(
  Bool   f_ignore_disable,
  uint8** sepmethodname,
  int32*  sepmethodlength)
{
  corecontext_t *context = get_core_context_interp();
  SEP_METHOD *sep_d = sep_detected ;

  HQASSERT( sepmethodname != NULL , "sepmethodname NULL" ) ;
  HQASSERT( sepmethodlength != NULL , "sepmethodlength NULL" ) ;

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation ||
       (!f_ignore_disable && spd_level != 0) ||
       sep_d == NULL ) {
    *sepmethodname = NULL ;
    *sepmethodlength = 0 ;
  }
  else {
    HQASSERT( sep_d->name != NULL  , "sep_d->name somehow NULL" ) ;
    *sepmethodname = ( uint8 * )sep_d->name ;
    *sepmethodlength = strlen_int32(( char * )sep_d->name ) ;
  }
}

static Bool ignore_detection_method( SEP_METHOD *sep_method, Bool ignore_same_level )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );

  if (( sep_detected == NULL ) ||
      ( sep_method->priority > sep_detected->priority ) ||
      (( sep_method->priority == sep_detected->priority ) &&
       ( !ignore_same_level ))) {
    /* Can't ignore, may override current situation. */
    return FALSE ;
  }
  /* Can't change, so don't bother doing anything. */
  return TRUE ;
}


static Bool adjust_screens(DL_STATE *page, GS_COLORinfo *colorInfo )
{
  SPOTNO new_spotno;

  invalidate_gstate_screens() ;
  HQASSERT(colorInfo == gstateptr->colorInfo, "Not updating current gstate.");
  if ( !gsc_redo_setscreen( colorInfo ))
    return FALSE;
  new_spotno = gsc_getSpotno( colorInfo );
  page->default_spot_no = new_spotno;
  ht_set_page_default( new_spotno );
  return TRUE;
}


static Bool store_separation( SEP_METHOD *sep_method ,
                              NAMECACHE *sepname ,
                              int32 adjustscreen , int32 adjustgray ,
                              GS_COLORinfo *colorInfo )
{
  corecontext_t *context = get_core_context_interp();

  HQASSERT( sepname != NULL , "sepname NULL in store_separation" ) ;

  if ( sepname == system_names + NAME_Unknown ||
       sepname->len == 0 ) {
    NAMECACHE *psepname = sep_detectedcolorname ;
    HQTRACE(debug_separation,("Nuke sep detection"));
    new_color_detected = TRUE ;
    sep_detected = NULL ;

    sep_detectedcolorname = system_names + NAME_Unknown ;

    if ( psepname == sep_detectedcolorname )
      return TRUE ;     /* Don't waste time if nowt's changing. */

    /* If we definitely know what color this is, then adjust the page */
    if ( adjustgray == ADJUST_GRAY_YES ) {
      if (!adjust_gray_separation( colorInfo )) {
        return FALSE;
      }


      if ( rcbn_intercepting())
        ( void )rcbn_register_separation( system_names + NAME_Gray ,
                                          RCBN_CONFIDENCE_NONE ) ;
    }

    /* Reset the screen if we need to */
    if ( adjustscreen == ADJUST_SCREEN_YES )
      return adjust_screens(context->page, colorInfo );
    return TRUE;
  }

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation || spd_level != 0 )
    return TRUE ;

  HQTRACE(debug_separation,("sep detected: %.*s",sepname->len,sepname->clist));
  HQTRACE(debug_separation,("   by method: %s",sep_method->name));

  if ( sep_detected == NULL ||
       sep_method->priority >= sep_detected->priority ) {
    Bool new_sep = TRUE ;
    Bool new_method = TRUE ;

    if ( sep_detected == NULL ) {
      /* First time we're trying this method on this page */
    }
    else if ( sep_method->priority == sep_detected->priority ) {
      /* Re-using the same method on this page; see if it's the same color. */
      new_method = FALSE ;
      if ( sepname == sep_detectedcolorname )
        new_sep = FALSE ;
    }
    else {
      /* We can now use a better method in the hierarchy to work out the color. */
      /* Once screen detection is obsolete, don't bother with it. */
      if ( new_screen_detected ) {
        if ( sep_detected == SEP_METHOD_ANGLE )
          new_screen_detected = FALSE ;
      }
    }

    /* Note we must deal with Recombine updates before using this new separation. */
    if ( new_sep ) {
      if ( rcbn_intercepting()) {
        if ( ! rcbn_register_separation( sepname ,
                                         sep_method == SEP_METHOD_SEPARATION ?
                                         RCBN_CONFIDENCE_MAX :
                                         ( sep_method == SEP_METHOD_SETCMYKCOL0R ?
                                           ( adjustgray == ADJUST_GRAY_YES ?
                                             RCBN_CONFIDENCE_HI :
                                             RCBN_CONFIDENCE_LO ) :
                                           RCBN_CONFIDENCE_LO )))
          return FALSE ;
      }
    }

    if ( new_method ) {
      sep_detected = sep_method ;
      sep_colorname = NULL ;
      sep_adjusted_gray = NULL ;
    }

    if ( new_sep ) {
      NAMECACHE *psepname = sep_detectedcolorname ;

      if ( sep_method == SEP_METHOD_ANGLE )
        new_screen_detected = TRUE ;

      /* Store the new separation name */
      sep_detectedcolorname = sepname ;

      /* If we definitely know what color this is, then adjust the page */
      if ( adjustgray == ADJUST_GRAY_YES ) {
        if (!adjust_gray_separation( colorInfo ) ) {
          return FALSE;
        }
        /* Since we know this page is definitely this color, then we can update it. */
        if ( sep_colorname != NULL &&
             sep_colorname != sep_adjusted_gray )
          multiple_separation_detected = TRUE ;
        sep_colorname = sep_adjusted_gray ;
      }

      if ( psepname == sep_detectedcolorname ) {
        /* Account for the RejectPreseparatedJobs switch being on. Exclude angle
         * detection here as it is unreliable in this context.
         */
        if ( context->userparams->RejectPreseparatedJobs &&
             page_is_separations() && ( sep_method != SEP_METHOD_ANGLE )) {
          return detail_error_handler(LIMITCHECK, "Preseparated job rejected.") ;
        }
        return TRUE ;   /* Don't waste time if nowt's changing. */
      }

      /* Need to invalidate the color chains.
       * Can't do it directly here as this can be called during an invoke.
       */
      gs_invalidateAllColorChains();

      /* Reset the screen if we need to */
      if ( adjustscreen == ADJUST_SCREEN_NO )
        return TRUE ;

      /* Account for the RejectPreseparatedJobs switch being on. */
      if ( context->userparams->RejectPreseparatedJobs && page_is_separations())
        return detail_error_handler(LIMITCHECK, "Preseparated job rejected.") ;

      return adjust_screens(context->page, colorInfo );
    }
  }
  return TRUE ;
}

/** This routine changes the name and equivalent process color of all
 * gray separations to the process color separation identified by the
 * Separation system parameter.
 */
static Bool adjust_gray_separation( GS_COLORinfo *colorInfo )
{
  NAMECACHE *colName ;
  NAMECACHE *sepName ;
  int32 nColorants ;
  DEVICESPACEID deviceSpace ;

  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );

  guc_deviceColorSpace( gsc_getRS(colorInfo) , & deviceSpace , & nColorants ) ;

  colName = deviceSpace == DEVICESPACE_Gray ?
    system_names + NAME_Gray :
    system_names + NAME_Black ;

  sepName = sep_detectedcolorname ;
  if ( sepName == system_names + NAME_Unknown ) {
    HQTRACE(debug_separation,("adjust_gray_separation: Gray"));
    sepName = colName ;
    sep_adjusted_gray = NULL ;
  }
  else {
    /* Determine the equivalent process color. */
    sep_adjusted_gray = sepName ;

    HQTRACE(debug_separation,("adjust_gray_separation: %.*s",sepName->len,sepName->clist));
  }

  /* Only adjust if we're not-auto-separating or recombining */
  if ( rcbn_enabled())
    return TRUE;

  if ( sepName == system_names + NAME_Composite ) {
    /* Needed since reset sets the Black sep name to Gray. */
    sepName = colName ;
  }

  return (guc_overrideColorantName( gsc_getRS(colorInfo) , colName , sepName ) );
}

Bool sub_page_is_composite( void )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );
  return ( sep_detected == SEP_METHOD_SETCOLOR ) ;
}

Bool sub_page_is_separations( void )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );
  return ( sep_detected == SEP_METHOD_SEPARATION ||
           sep_detected == SEP_METHOD_SETCMYKCOL0R ||
           ( sep_detected == SEP_METHOD_ANGLE &&
             sep_colorname != NULL )
         ) ;
}

Bool page_is_composite( void )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );
  return ( pge_detected == SEP_METHOD_SETCOLOR ||
           sep_detected == SEP_METHOD_SETCOLOR ) ;
}

Bool page_is_separations( void )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );
  return ( pge_detected == SEP_METHOD_SEPARATION ||
           pge_detected == SEP_METHOD_SETCMYKCOL0R ||
           ( pge_detected == SEP_METHOD_ANGLE &&
             pge_colorname != NULL ) ||
           sep_detected == SEP_METHOD_SEPARATION ||
           sep_detected == SEP_METHOD_SETCMYKCOL0R ||
           ( sep_detected == SEP_METHOD_ANGLE &&
             sep_colorname != NULL )
         ) ;
}

Bool finalise_sep_detection( GS_COLORinfo *colorInfo )
{
  corecontext_t *context = get_core_context_interp();
  Bool result = TRUE ;

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation || spd_level != 0 )
    return TRUE ;

  /* If the current method is to use setcmykcolor and all we've seen is Black,
   * then we know that this sep really is black.
   */
  if ( sep_adjusted_gray == NULL ) {
    if ( sep_detected == SEP_METHOD_SETCMYKCOL0R ) {
      if (!adjust_gray_separation( colorInfo ) ) {
        return FALSE;
      }
      /* Since we know this page is definitely this color, then we can update it.*/
      if ( sep_colorname != NULL &&
           sep_colorname != sep_adjusted_gray )
        multiple_separation_detected = TRUE ;
      sep_colorname = sep_adjusted_gray ;

      if ( rcbn_intercepting())
        result = rcbn_register_separation( sep_colorname , RCBN_CONFIDENCE_HI ) ;

      sep_adjusted_gray = NULL ;
    }
  }

  return result ;
}

/** This routine is called to see if we've found more than one color on a page.
 */
Bool detected_multiple_seps( void )
{
  corecontext_t *context = get_core_context_interp();

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation || spd_level != 0 )
    return FALSE ;

  if ( ! rcbn_enabled())
    if ( multiple_separation_detected )
      return TRUE ;

  if ( rcbn_enabled())
    return FALSE ;

  if ( sep_colorname != NULL &&
       pge_colorname != NULL )
    return ( sep_colorname != pge_colorname ) ;
  else
    return FALSE ;
}

/** This routine is called when we have detected a new color for the
 * page and are about to draw a graphical object on the page. Currently
 * we only use this routine for the setscreen case.
 */
Bool setscreen_separation( int32 screened )
{
  corecontext_t *context = get_core_context_interp();

  HQASSERT( new_screen_detected , "wasted setscreen_separation" ) ;

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation || spd_level != 0 )
    return TRUE;

  if ( sep_detected == SEP_METHOD_ANGLE ) {
    /* If the current method is to use the angle (a weak method) then if
     * the color is black, then we don't know if the page is really part
     * of a separation and so is truly Black, or if it's actually a Gray
     * job.
     *
     * Currently, we therefore leave the page annotation alone in such a
     * case.  If the color is not Black, however, then we truly do know
     * that this is correct (apart from the case of angles possibly
     * being swapped over which we can't get right), so in this case we
     * do set the page annotation.
     *
     * This means that we currently get Gray for true Gray jobs, but for
     * old color jobs where we have to rely on screen angles, we get
     * C,M,Y,(nothing) as opposed to C,M,Y,K. Alternatively, if we
     * remove the test on Black, then all color jobs will come out as
     * C,M,Y,K, but, all Gray jobs will also come out as K.
     */
    if ( screened ) {
      /* If we've seen some pages before that have C or M (not Y) in
       * them, then we make this one K after all. We don't use Y because
       * it is common to have 0 degree screens amongst pages in mono
       * jobs for special effects.
       */
      Bool detected;
      NAMECACHE *sepname ;
      static Bool cm_detected = FALSE;
      static int32 cm_detected_swJobNumber = -1 ;

      /* Only set this if we've got a non b/w object so that otherwise
         we can try again. */
      new_screen_detected = FALSE ;

      if ( cm_detected_swJobNumber != context->page->job_number ) {
        cm_detected_swJobNumber = context->page->job_number ;
        cm_detected = FALSE ;
      }

      sepname = sep_detectedcolorname ;
      detected = FALSE ;
      if ( cm_detected ||
           sepname != system_names + NAME_Black ) {
        detected = TRUE ;
        if ( sepname != system_names + NAME_Yellow )
          cm_detected = TRUE ;

        if (!adjust_gray_separation( gstateptr->colorInfo ) ) {
          return FALSE;
        }
      }

      /* If we've not marked this page as being a certain color, then do
       * so.  If we've already marked this page as being of a certain
       * color, then if the new color is different, then we know that
       * we've (possibly) got more than one color on the page.
       *
       * Note that this is not (and can not) be 100% correct. It
       * virtually only is wrong when we are falling back on using
       * screen angles to detect the color. That's because we don't
       * know the difference between a page that contains imposed pages
       * of different colors and a page that contains multiple screens.
       */
      if ( sep_colorname != NULL &&
           sep_colorname != sep_adjusted_gray )
        multiple_separation_detected = TRUE ;
      sep_colorname = sep_adjusted_gray ;

      if ( rcbn_intercepting()) {
        if ( detected )
          ( void )rcbn_register_separation( sep_colorname , RCBN_CONFIDENCE_MED3 ) ;
        else if ( sep_detectedcolorname )
          ( void )rcbn_register_separation( sep_detectedcolorname , RCBN_CONFIDENCE_MED2 ) ;
      }
    }
    else {
      if ( rcbn_intercepting()) {
        if ( sep_detectedcolorname )
          ( void )rcbn_register_separation( sep_detectedcolorname ,
                                            RCBN_CONFIDENCE_MED1 ) ;
      }
    }
  }

  return TRUE;
}

/** This routine is called when we have a new physical page.
 * Note: it does not reset systemparam sepname (if any).
 */
Bool reset_separation_detection_on_new_page( GS_COLORinfo *colorInfo )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );

  reset_separation_detection_globals();

  if ( ! store_separation( SEP_METHOD_NOMETHOD ,
                           system_names + NAME_Unknown ,
                           ADJUST_SCREEN_YES ,
                           ADJUST_GRAY_YES ,
                           colorInfo ))
    return FALSE ;

  return reiterate_setsystemparam_separation(colorInfo) ;
}

/** This routine is called when we have a new imposed page.
 * Note: it does not reset systemparam sepname (if any).
 */
Bool reset_separation_detection_on_sub_page( GS_COLORinfo *colorInfo )
{
  corecontext_t *context = get_core_context_interp();

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation || spd_level != 0 )
    return TRUE ;

  if (!finalise_sep_detection(colorInfo))
    return FALSE;

  if ( sep_colorname != NULL ) {
    if ( pge_colorname != NULL &&
         pge_colorname != sep_colorname )
      multiple_separation_detected = TRUE ;
    pge_colorname = sep_colorname ;
  }

  sep_adjusted_gray = NULL ;

  if ( pge_detected == NULL ||
       ( sep_detected &&
         ignore_detection_method( pge_detected , FALSE )))
    pge_detected = sep_detected ;

  if ( ! store_separation( SEP_METHOD_NOMETHOD ,
                           system_names + NAME_Unknown ,
                           ADJUST_SCREEN_YES ,
                           ADJUST_GRAY_YES ,
                           colorInfo ))
    return FALSE ;

  return reiterate_setsystemparam_separation(colorInfo) ;
}

/** This routine is called when we are doing a restore
 * to reset NAMECACHE entries in particular.
 */
void reset_separation_detection_on_restore( void )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );
  reset_separation_detection_globals() ;
  reset_setsystemparam_separation() ;
}

/** This routine is called when we get a setsystemparams. We get passed the
 * separation name (which may be a transitory name, eg. a spot color).
 *
 * Sepdetect holds pointers into the namecache.  These must be cleared before
 * the final restore at EOJ calls purge_ncache, or they will point at freed
 * memory.  Just before restore, the EOJ PS in jobpss calls setsystemparam
 * with /Separation set to an empty name.  This is sepdetect's chance to
 * clear all its namecache pointers (unless they are known safe, eg.
 * NAME_Unknown).
 */
Bool detect_setsystemparam_separation( NAMECACHE *sepname, GS_COLORinfo *colorInfo )
{
  corecontext_t *context = get_core_context_interp();

  HQASSERT( sepname , "sepname NULL in detect_setsystemparam_separation" ) ;

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation || spd_level != 0 )
    return TRUE ;

  HQTRACE( debug_separation &&
           ( sepname != system_names + NAME_Unknown &&
             sepname->len != 0 ) &&
           sep_detected == SEP_METHOD_SETCOLOR ,
           ("using method setsystemparam after method setcolor") ) ;

  /* Separation detection based on the /Separation systemparam should last
   * until the next showpage, copypage (LL3), or EOJ.  [22312]
   * Record the systemparam sepname (superseding previous one if any).
   */
  sep_sysparamname = sepname ;

  /* need to clear all namecache pointers? */
  if ( sepname->len == 0 )
    reset_setsystemparam_separation() ;

  return store_separation( SEP_METHOD_SEPARATION ,
                           sepname ,
                           ADJUST_SCREEN_YES ,
                           ADJUST_GRAY_YES ,
                           colorInfo ) ;
}

/* Reiterate (repeat) the recorded systemparam sepname.
 */
static Bool reiterate_setsystemparam_separation( GS_COLORinfo *colorInfo )
{
  if ( sep_sysparamname == NULL )
    return TRUE ;

  HQASSERT(
    get_core_context_interp()->systemparams->DetectSeparation && spd_level == 0u,
    "Attempt to reiterate a (non-null) setsystemparam sepdetect, when illegal to do so"
  ) ;

  /* reiterate, as though setsystemparam had just been called */
  return store_separation( SEP_METHOD_SEPARATION ,
                           sep_sysparamname ,
                           ADJUST_SCREEN_YES ,
                           ADJUST_GRAY_YES ,
                           colorInfo ) ;
}

/** Reset global NAMECACHE and SEP_METHODs
 */
static void reset_separation_detection_globals(void)
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );

  multiple_separation_detected = FALSE ;

  pge_colorname = NULL ;
  sep_colorname = NULL ;

  pge_adjusted_gray = NULL ;
  sep_adjusted_gray = NULL ;

  pge_detected = NULL ;
  sep_detected = NULL ;
}

/* Reset the recorded systemparam sepname.
 */
void reset_setsystemparam_separation( void )
{
  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );
  sep_sysparamname = NULL ;
}

/** This routine is called when we're about to add a graphical object to the DL
 * and we have definitely redefined setcmykcolor. It calls the PostScript proc
 * DetectSeparation in order to try to determine what process color the job is
 * trying to produce in a shade of gray. If we end up with a result that is
 * non-black, then we know the page is that color. If we end up with a result
 * that is black, then we only know that the page really is black if we get to
 * the end of the page and haven't seen any other color.
 */
Bool detect_setcmykcolor_separation( GS_COLORinfo *colorInfo )
{
  corecontext_t *context = get_core_context_interp();
  NAMECACHE *sepname ;
  OBJECT *theo ;

  static Bool detecting_setcmykcolor = FALSE ;

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation || spd_level != 0 )
    return TRUE ;

  theICMYKDetect( workingsave ) = FALSE ;

  /* Why bother... */
  if ( ignore_detection_method( SEP_METHOD_SETCMYKCOL0R , FALSE ))
    return TRUE ;

  /* Put in a safety check so that we don't go recursive... */
  if ( detecting_setcmykcolor )
    return TRUE ;

  detecting_setcmykcolor = TRUE ;
  theo = fast_extract_hash_name( & internaldict , NAME_DetectSeparation ) ;
  if ( ! push( theo , & executionstack ) ||
       ! interpreter( 1 , NULL )) {
    HQFAIL( "interpreter call failed" ) ;
    detecting_setcmykcolor = FALSE ;
    return FALSE ;
  }
  detecting_setcmykcolor = FALSE ;

  /* The procedure should leave a color name on the stack - just process
   * color or /Separation if it doesnt know.
   */
  if ( theStackSize( operandstack ) < 0 ) {
    HQFAIL( "DetectSeparation stackunderflow" ) ;
    return FALSE ;
  }
  theo = theTop( operandstack ) ;
  if ( oType( *theo ) != ONAME ) {
    HQFAIL( "DetectSeparation returned non-name" ) ;
    return FALSE ;
  }

  sepname = oName( *theo ) ;

  pop( & operandstack ) ;

  if ( sepname == system_names + NAME_Separation ) {
    /* We couldn't decide what color it was. */
    return TRUE ;
  }

  HQTRACE( debug_separation &&
           sep_detected == SEP_METHOD_SETCOLOR ,
           ("using method setcmykcolor after method setcolor") ) ;

  if ( sepname == system_names + NAME_Black ) {
    /* We only really know what color it is if it's non black.
     * However, the page really may be black, so we want to switch
     * the screening to black, if at the right level of our hierarchy,
     * but we don't want to set the page color. We also only call this
     * function if setcmykcolor has been redefined, thus if all we do get
     * are black colors, then we do know that the page is black. However,
     * we aren't sure of this until either the end of the page, or when we
     * get a non black result.
     */
    return store_separation( SEP_METHOD_SETCMYKCOL0R ,
                             sepname ,
                             ADJUST_SCREEN_YES ,
                             ADJUST_GRAY_NO ,
                             colorInfo ) ;
  }
  /* The color has come out non-black, so we know that the page really is so.
   * We can therefore set the page color.
   */
  return store_separation( SEP_METHOD_SETCMYKCOL0R ,
                           sepname ,
                           ADJUST_SCREEN_YES ,
                           ADJUST_GRAY_YES ,
                           colorInfo ) ;
}

/* This routine is called when we get an input color that is non-gray (eg RGB or CMYK).
 */
Bool detect_setcolor_separation( void )
{
  corecontext_t *context = get_core_context_interp();

  HQASSERT( new_color_detected , "wasted compositecolor_separation" ) ;

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !context->systemparams->DetectSeparation || spd_level != 0 )
    return TRUE ;

  new_color_detected = FALSE ;

  HQTRACE(debug_separation,("detect_setcolor_separation"));

  /* Why bother... */
  if ( ignore_detection_method( SEP_METHOD_SETCOLOR , TRUE ))
    return TRUE ;

  return store_separation( SEP_METHOD_SETCOLOR ,
                           system_names + NAME_Composite ,
                           ADJUST_SCREEN_NO ,
                           ADJUST_GRAY_YES ,
                           gstateptr->colorInfo ) ;
}

/** This routine is called when we're about to go setscreen (or setcolorscreen...)
 * and we know the screen is not a pattern screen. We match the screen angle
 * against (for now) the angle set for HPS. We delay use of this knowledge until
 * we actually get a graphical object that is used (and in fact is screened).
 */
Bool detect_setscreen_separation( SYSTEMVALUE frequency_detected ,
                                   SYSTEMVALUE angle_detected ,
                                   GS_COLORinfo *colorInfo )
{
  SYSTEMPARAMS *systemparams = get_core_context_interp()->systemparams;
  int32 i ;

  /* Only guess if we're allowed to and not-auto-separating... */
  if ( !systemparams->DetectSeparation || spd_level != 0 )
    return TRUE ;

  if ( ! systemparams->DoDetectScreenAngles )
    return TRUE ;

  /* Do not try the setcmykcolor trick directly here, because it will succeed
   * at the beginning of a sep and possibly inherit the color from the previous
   * one. Because of the promotion hierarchy, this then sticks, because we
   * ignore any subsequent black ones, and setcmykcolor (currently) takes
   * preference over setscreen.
   */

  /* Why bother... */
  if ( ignore_detection_method( SEP_METHOD_ANGLE , FALSE )) {
    /* Why bother with detect_setcmykcolor... */
    if ( ! ignore_detection_method( SEP_METHOD_SETCMYKCOL0R , FALSE ))
      theICMYKDetect( workingsave ) = TRUE ;
    return TRUE ;
  }

  /* As a safety measure, we re-check the result of using setcmykcolor detection.
   * This can only be done when we get a graphical object and if setcmykcolor has
   * been redefined, so we set a flag to say attempt it (later on). Note this
   * flag only means do it if it can be done (ie if there is a redefinition of
   * setcmykcolor in force), rather than do it.
   * This "safety measure" should be harmless and probably could be removed.
   */
  /* Why bother with detect_setcmykcolor... */
  if ( ! ignore_detection_method( SEP_METHOD_SETCMYKCOL0R , FALSE ))
    theICMYKDetect( workingsave ) = TRUE ;

  /* Normalise and snap angle_detected */
  screen_normalize_angle( &angle_detected);

  /* Ignore low frequencies used for special effects; (could do for only 0 degree screens). */
  /* e.g. if ( angle_detected == 0.0 ) */
  if ( frequency_detected <= ( SYSTEMVALUE )systemparams->MinScreenDetected )
    return TRUE ;

  for ( i = 0 ; i < 4 ; ++i ) {
    if ( fabs( systemparams->DetectScreenAngles[ i ] - angle_detected ) < EPSILON ) {
      return store_separation( SEP_METHOD_ANGLE ,
                               pcmCMYKNames[ i ] ,
                               ADJUST_SCREEN_NO ,
                               ADJUST_GRAY_NO ,
                               colorInfo ) ;
    }
  }
  return TRUE ;
}


/*
 * Normalize an angle first to the range 0 to 360 then to
 * 0 to 90. Also used in setsystemparam_.
 */
void screen_normalize_angle( SYSTEMVALUE *angle )
{
  SYSTEMVALUE angle_detected = *angle ;

  HQASSERT( IS_INTERPRETER(), "Separation detection outside interpretation" );

  NORMALISE_ANGLE(angle_detected) ;
  HQASSERT( angle_detected >= 0.0 && angle_detected < 360.0 , "normalisation gone wrong" ) ;

  while ( angle_detected >= 90.0 )
    angle_detected -=  90.0 ;

  /* We always snap the angle_detected to 7.5 */
  { int32 nearest7dot5 ;

    nearest7dot5  = ( int32 )( 2.0 * angle_detected + 7.5 ) ;
    nearest7dot5 -= ( nearest7dot5 % 15 ) ;
    if ( nearest7dot5 == 720 )
      nearest7dot5 = 0 ;

    angle_detected = ( SYSTEMVALUE )nearest7dot5 ;
    angle_detected *= 0.5 ;
  }

  *angle = angle_detected ;
}


/* If recombining, separation interception is applied to the 'pseudo' screen
   which will patched over the appropriate real screen later on when we're sure
   the job is preseparated.  Otherwise, separation interception is applied to
   the Black screen. */
inline Bool interceptSeparation(NAMECACHE *colname, NAMECACHE *sepname,
                                DEVICESPACEID devicespace)
{
  corecontext_t *context = get_core_context_interp();

  return ( context->systemparams->DetectSeparation &&
           sepname != system_names + NAME_Unknown &&
           sepname != system_names + NAME_Composite &&
           ( devicespace == DEVICESPACE_CMYK ||
             devicespace == DEVICESPACE_RGBK ) &&
           ( ( colname == system_names + NAME_Black && !rcbn_intercepting() ) ||
             rcbn_is_pseudo_separation(colname) ) );
}


/* Log stripped */
